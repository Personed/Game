/*!
\file
Interface for EntityAI class
*/

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _VEHICLE_AI_HPP
#define _VEHICLE_AI_HPP

#include "object.hpp"
#include "vehicle.hpp"
#include "AI/aiTypes.hpp"
#include "animation.hpp"
#include "recoil.hpp"
#include "memberCalls.hpp"
#include <El/ParamArchive/serializeClass.hpp>
#include <El/Evaluator/expressImpl.hpp>
#include "memberCalls.hpp"
#include "pathAction.hpp"
#include "lights.hpp"
#include "smokes.hpp"
#include "weapons.hpp"
#include "json.hpp"

class WeaponModeType;
class MuzzleType;
class WeaponType;
class Weapon;

class MagazineType;
class Magazine;

#define DIAG_AGGREGATED_TARGETS 0

class Turret;
class ParamEntry;
class KBCenter;

#ifndef DECL_ENUM_ABILITY_KIND
#define DECL_ENUM_ABILITY_KIND
DECL_ENUM(AbilityKind)
#endif
#ifndef DECL_ENUM_COMBAT_MODE
#define DECL_ENUM_COMBAT_MODE
DECL_ENUM(CombatMode)
#endif

/// Suppression mode enum
#define SUPPRESS_ENUM(type, prefix, XX) \
  XX(type, prefix, No) /* not suppressing */ \
  XX(type, prefix, Auto) /* staying ready in a fire position, but not suppressing */ \
  XX(type, prefix, Yes) /* providing full suppressive fire */ \
  XX(type, prefix, Intense) /* providing intense suppressive fire - ordered by the leader */ \

#ifndef DECL_ENUM_SUPPRESS
#define DECL_ENUM_SUPPRESS
DECL_ENUM(SuppressState)
#endif
DECLARE_ENUM(SuppressState, Suppress, SUPPRESS_ENUM)

#define LOG_TARGET_CHANGES 0

UNIQUE_TYPE(SensorRowID, int);
UNIQUE_TYPE(SensorColID, int);


DECL_ENUM(MoveFinishF)
// note: value zero is reserved as no action

enum VehicleKind {VSoft,VArmor,VAir,NVehicleKind};

enum ShowAllTargets
{
 showAllNoST = 0,
 showAllCadetST =	1,
 showAllYesST	=	2
};

class Threat
{
  float _data[NVehicleKind];

public:
  Threat() {_data[0]=_data[1]=_data[2]=0;}
  Threat(float soft, float armor, float air)
  {
    _data[VSoft]=soft,_data[VArmor]=armor,_data[VAir]=air;
  }
  Threat(VehicleKind kind, float value)
  {
    _data[0]=_data[1]=_data[2]=0;
    _data[kind] = value;
  }

  float operator [] (VehicleKind kind) const {return _data[kind];}
  float &operator [] (VehicleKind kind) {return _data[kind];}

  /// weighted exposure - "dot product"
  float operator * (const Threat &a) const
  {
    return _data[0]*a._data[0] + _data[1]*a._data[1] + _data[2]*a._data[2]; 
  }
  /// weighted exposure with AICenter map values - "dot product"
  float operator * (const AIThreat &t) const
  {
    return t.u.soft*_data[0]+t.u.armor*_data[1]+t.u.air*_data[2];
  }

  Threat operator + (const Threat &a) const
  {
    return Threat(
      _data[0]+a._data[0],
      _data[1]+a._data[1],
      _data[2]+a._data[2]
    );
  }
  void operator += (const Threat &a)
  {
    _data[0]+=a._data[0];
    _data[1]+=a._data[1];
    _data[2]+=a._data[2];
  }
  Threat operator * (float c) const
  {
    return Threat(_data[VSoft]*c,_data[VArmor]*c,_data[VAir]*c);
  }
};

enum LightType
{
  LightTypeMarker,
  LightTypeMarkerBlink,
};

struct LightInfo
{
  LightType type;
  Vector3 position;
  Vector3 direction;
  Color color;
  Color ambient;
  float radius;
  bool nvgMarker;
};
TypeIsSimple(LightInfo);

struct ExtSoundInfo
{
  RStringB name;
  AutoArray<SoundPars> pars;
};
TypeIsMovableZeroed(ExtSoundInfo);

struct ExtAttrSoundInfo
{
  RStringB name;
  AutoArray<ExtSoundInfo> pars;
};
TypeIsMovableZeroed(ExtAttrSoundInfo);

struct ExtSoundInfo2D
{
  RStringB name;
  AutoArray<ExtAttrSoundInfo> infos3; // 3D items
  AutoArray<ExtSoundInfo> infos; // 2D items
  AutoArray<SoundPars> pars; // 1D items
};
TypeIsMovableZeroed(ExtSoundInfo2D);


struct MissileCounterItem
{
  mutable int _total;
  int _maverickIndexOffset;

  MissileCounterItem() { _total = 0; _maverickIndexOffset = -1;}
  MissileCounterItem(const int maverickIndexOffset): _maverickIndexOffset(maverickIndexOffset), _total(0) {}
  bool IsEmpty() const { return _maverickIndexOffset < 0; }

  int Add(const int count) const { _total += count; return _total; }
};

TypeIsMovableZeroed(MissileCounterItem);

template <>
struct MapClassTraits<MissileCounterItem>: public DefMapClassTraits<MissileCounterItem>
{
  /// key type
  typedef const int &KeyType;
  typedef MissileCounterItem Type;

  static unsigned int CalculateHashValue(KeyType key) { return key; }
  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2) { return k1 - k2; }
  static KeyType GetKey(const Type &item) {return item._maverickIndexOffset;}
};

typedef MapStringToClass<MissileCounterItem, AutoArray<MissileCounterItem> > MissileCounter;

struct PointLightInfo : public RefCount
{
  Color color;
  Color colorAmbient;
  bool dayLight;
	float heatHazeRadius;
	float heatHazePower;
  int positionIndex;
  Vector3 position;
  float size;
  float radius;
  float speed; // inverted blinking period (still lights for speed <= 0)
  float phaseLimit; // how much of cycle reflector is on
  bool fireEffect;
  unsigned fireEffectOctaves;
  float fireEffectPersistence;
  float fireEffectFract;
  AnimationSection selection; // used to Hide/Unhide
  int hitPoint;

  void Load(EntityAIType &type, LODShape *shape, ParamEntryPar cls, float armor, Ref<WoundInfo> wound);
};


struct ReflectorInfo : public PointLightInfo
{
  int directionIndex;
  Vector3 direction;
  float angle;
  float angleInnerRatio;

  void Load(EntityAIType &type, LODShape *shape, ParamEntryPar cls, float armor, Ref<WoundInfo> wound);
};


/// default traits for int anti-cheat "encryption"
template <class Type>
struct EncryptedTraits
{
  typedef Type EncryptedType;
  enum {Key=0xbabac8b6};
  static EncryptedType Encrypt(Type val)
  {
    return val^Key;
  }
  static Type Decrypt(EncryptedType val)
  {
    return val^Key;
  }
  static EncryptedType Combine(EncryptedType v1, EncryptedType v2)
  {
    return v1+v2;
  }
  static void Decombine(EncryptedType &v1, EncryptedType &v2, EncryptedType src)
  {
    v1 = src>>1;
    v2 = src-v1;
  }
};

template <class Type, class Traits=EncryptedTraits<Type> >
class Encrypted;

template <class Type, class Traits=EncryptedTraits<Type> >
class EncryptedSupport
{
  friend class Encrypted<Type,Traits>;
  typedef typename Traits::EncryptedType EncryptedType;

  EncryptedType _value;

public:
  bool operator == (const EncryptedSupport &with) const {return _value==with._value;}
};

template <class Type, class Traits>
class Encrypted
{
  typedef typename Traits::EncryptedType EncryptedType;
  typedef EncryptedSupport<Type,Traits> Support;

  EncryptedType _value;

  static EncryptedType Encrypt(Type value) {return Traits::Encrypt(value);}
  static Type Decrypt(EncryptedType value) {return Traits::Decrypt(value);}

public:
  Type GetValue(const Support &support) const {return Decrypt(Traits::Combine(_value,support._value));}
  explicit Encrypted(){}
  void SetValue(Support &support, Type val){Traits::Decombine(_value,support._value,Encrypt(val));}

  Type Add(Type val, EncryptedSupport<Type,Traits> &support)
  {
    Type res = Decrypt(Traits::Combine(_value,support._value))+val;
    EncryptedType encValue = Encrypt(res);
    Traits::Decombine(_value,support._value,encValue);
    return res;
  }

  // for comparison purposes raw value will do
  bool IsEqual(const Support &support, const Encrypted &with, const Support &withSupport) const
  {
    return with._value!=_value || support._value!=withSupport._value;
  }

  bool operator == (const Encrypted &with) const {return _value==with._value;}
};

typedef Encrypted<int> EncryptedInt;
typedef EncryptedSupport<int> EncryptedIntSupport;
#define ADD_ENCRYPTED(var,support,val) (var).Add(val,support)
#define GET_ENCRYPTED(var,support) (var).GetValue(support)
#define SET_ENCRYPTED(var,support,val) (var).SetValue(support,val)

struct WeaponCargoItem
{
  Ref<WeaponType> weapon;
  int count;
};
TypeIsMovableZeroed(WeaponCargoItem)

struct MagazineCargoItem
{
  Ref<MagazineType> magazine;
  int count;
};
TypeIsMovableZeroed(MagazineCargoItem)

typedef AutoArray<WeaponCargoItem> WeaponCargo;
typedef AutoArray<MagazineCargoItem> MagazineCargo;
// different units have access to different sensors

enum CanSee
{
  CanSeeRadar=1,
  CanSeeEye=2,
  CanSeeOptics=4,
  CanSeeEar=8,
  CanSeeCompass=16,
  CanSeePeripheral=32,
  //CanSeeAll=~0
};

enum RadarType
{
  None=0,
  TacticalDisplay=1,
  TankRadar=2,
  AirRadar=4,
};

struct HeadPars
{
  float _initAngleY,_minAngleY,_maxAngleY;
  float _initAngleX,_minAngleX,_maxAngleX;
  float _minAngleZ,_maxAngleZ; //roll angle is stored in radians!
  float _rotZRadius; //distance from neck to eye to rotate camera with shifted center of rotation
  void Load(ParamEntryPar cfg);
  void InitVirtual(CameraType camType, float &heading, float &dive) const;
  void LimitVirtual(CameraType camType, float &heading, float &dive) const;
  void LimitRoll(float &roll) const { saturate(roll, _minAngleZ, _maxAngleZ); }
};

class PlateInfo
{
public:
  Vector3 _center;
  Vector3 _normal;
  float _size;

  void Init(const Shape *shape, Offset face);
};
TypeIsSimpleZeroed(PlateInfo)

#include "font.hpp"

class PlateInfos
{
protected:
  Buffer< AutoArray<PlateInfo> > _plates;
  Ref<Font> _font;
  PackedColor _color;
  RStringB _name;

public:
  void Init(LODShape *shape, ParamEntryPar cls, FontID font);
  /// simplified initialization just to dim the array
  void Init(LODShape *shape)
  {
    _plates.Init(shape->NLevels());
  }
  void InitLevel(LODShape *shape, int level);
  void DeinitLevel(LODShape *shape, int level)
  {
    _plates[level].Clear();
  }
  void Draw(int cb, int level, ClipFlags clipFlags, const PositionRender &pos, const RString &text) const;
};

struct UserTypeAction
{
  RString name;
  RString displayName;
  RString displayNameDefault;

  float radius;
  bool onlyForPlayer;
  RString condition;
  RString statement;
  int modelPositionIndex;

#if _HELISIM_PRECISE_INTERACTION
  int positionIndex;
  float radiusView;
  bool show;
  int showIn3D;
  int available;
  RString textToolTip;
  int userActionID;
#endif

  float priority;
  bool showWindow;
  bool hideOnUse;
  // show the action for item in hands
  bool showInHands;
  UserAction shortcut;
};

TypeIsMovableZeroed(UserTypeAction)

#include <El/Enum/enumNames.hpp>

//! entity event enum defined using enum factory
#define ENTITY_EVENT_ENUM(type,prefix,XX) \
  XX(type, prefix, Killed) /*object:killer*/ \
  XX(type, prefix, Hit) /*object:causedBy,scalar:howmuch*/ \
  XX(type, prefix, Engine) /*bool:engineState*/ \
  \
  XX(type, prefix, GetIn) /*string:position,object:unit*/ \
  XX(type, prefix, GetOut) /*string:position,object:unit*/ \
  \
  XX(type, prefix, Fired) /*string:weapon,string:muzzle,string:mode,string:ammo*/ \
  XX(type, prefix, FiredNear) /*object:whoFired,scalar:distance,string:weapon,string:muzzle,string:mode,string:ammo*/ \
  XX(type, prefix, IncomingMissile) /*string:ammo,object:whoFired*/ \
  XX(type, prefix, Dammaged) /*string:name,scalar:howmuch*/ \
  XX(type, prefix, Gear) /*bool:gearState*/ \
  XX(type, prefix, Fuel) /*bool:fuelState*/ \
  \
  XX(type, prefix, AnimChanged) /*string:animation*/ \
  XX(type, prefix, AnimDone) /*string:animation*/ \
  \
  XX(type, prefix, Init) /**/ \
  \
  XX(type, prefix, Delete) /*object:deletedUnit*/ \
  XX(type, prefix, LandedTouchDown) /*scalar:airport*/ \
  XX(type, prefix, LandedStopped) /*scalar:airport*/ \
  \
  XX(type, prefix, HandleDamage) /*string:name,scalar:howmuch,object:offender,string:ammo*/ \
  XX(type, prefix, HitPart) /*VBS*/ \
  XX(type, prefix, Suppressed) /*VBS*/\
  XX(type, prefix, AttachTo) /*VBS*/\
  XX(type, prefix, HandleHeal) /*AIS takes over healing*/ \
  \
  XX(type, prefix, HandleIdentity) /**/ \
  \
  XX(type, prefix, AnimStateChanged) /*string:animation*/ \
  \
  XX(type, prefix, Local) /*bool:local*/ \
  \
  XX(type, prefix, SelectedActionChanged) \
  XX(type, prefix, SelectedActionPerformed) \
  XX(type, prefix, SelectedRotorLibActionChanged) \
  XX(type, prefix, SelectedRotorLibActionPerformed) \
  XX(type, prefix, ControlsShifted) \
  \
  XX(type, prefix, PowerOn) \
  XX(type, prefix, PowerOff) \
  \
  XX(type, prefix, ItemAttached) \
  XX(type, prefix, ItemDetached) \
  \
  XX(type, prefix, Used) /*object:person*/ \
  XX(type, prefix, AnimHook) /*int:userType,string:param*/ \


#ifndef DECL_ENUM_ENTITY_EVENT
#define DECL_ENUM_ENTITY_EVENT
DECL_ENUM(EntityEvent)
#endif
DECLARE_ENUM(EntityEvent,EE,ENTITY_EVENT_ENUM)

//! mission event enum defined using enum factory
#define MISSION_EVENT_ENUM(type,prefix,XX) \
  XX(type, prefix, Ended) /*mission has ended*/ \
  XX(type, prefix, SelectedActionPerformed) \
  XX(type, prefix, SelectedRotorLibActionPerformed) \
  XX(type, prefix, SelectedActionChanged) \
  XX(type, prefix, SelectedRotorLibActionChanged) \
  XX(type, prefix, ControlsShifted) \

#ifndef DECL_ENUM_MISSION_EVENT
#define DECL_ENUM_MISSION_EVENT
DECL_ENUM(MissionEvent)
#endif
DECLARE_ENUM(MissionEvent,ME,MISSION_EVENT_ENUM)


#ifndef DECL_ENUM_OPER_ITEM_TYPE
#define DECL_ENUM_OPER_ITEM_TYPE
DECL_ENUM(OperItemType)
#endif

#ifndef DECL_ENUM_OPER_ITEM_TYPE
#define DECL_ENUM_OPER_ITEM_TYPE
DECL_ENUM(OperItemType)
#endif

class Path;

typedef RefArray<ReflectorInfo> ReflectorInfoArray;
typedef RefArray<PointLightInfo> PointLightInfoArray;

//! Information shared between EntityAI objects of the same type

class EntityAI;
class EntityAIFull;
class VehicleTypeBank;
class Transport;
class DoorsHolder;

/// information about type of weapons controlled by a single gunner
struct WeaponsType
{
  /// types of weapons
  RefArray<WeaponType> _weapons;
  /// types of magazines
  RefArray<MagazineType> _magazines;
  /// weapon fire animation
  AnimationAnimatedTexture _animFire;

  /// basic config load
  void Load(ParamEntryPar cfg);
  /// called when model is loaded
  void Init(LODShape *shape, ParamEntryPar cfg)
  {
    _animFire.Init(shape, (cfg >> "selectionFireAnim").operator RString(), NULL);
  }
  /// called when model is unloaded
  void Deinit(LODShape *shape) {}
  /// called when model level is loaded
  void InitLevel(LODShape *shape, int level)
  {
    _animFire.InitLevel(shape, level);
  }
  /// called when model level is unloaded
  void DeinitLevel(LODShape *shape, int level)
  {
    _animFire.DeinitLevel(shape, level);
  }
  /// check max. engagement range for all weapons stored here
  float GetMaxRange() const;
};

class TurretType;
typedef AutoArray<int, MemAllocLocal<int, 16> > TurretPath;

/// interface for functor passed to EntityAIType::ForEachTurret
class ITurretTypeFunc
{
public:
  virtual bool operator () (const EntityAIType *entityType, const TurretType &turretType, TurretPath &path) const = 0;
};

//! Class that is usually associated with an object stores lights in an aggregated form
class ObjectTypeLights
{
  friend class ObjectLights;
protected:
  //! Lights grouped in aggregation chunks
  AutoArray<ReflectorInfoArray> _aggReflectors;
  AutoArray<PointLightInfoArray> _aggPointLights;
public:
  void Init(EntityAIType *type, LODShape *shape, const ParamEntry &par, float objectArmor, Ref<WoundInfo> wound);
  void InitLevel(LODShape *shape, int level);
  void DeinitLevel(LODShape *shape, int level);
  bool SomeLights() const {return _aggReflectors.Size() || _aggPointLights.Size();}
};

/// support for more variants of vehicle name dubbing (different languages, grammar categories etc.)
struct EntitySpeechVariant
{
  /// name of the variant
  RString _variant;
  /// list of words for singular
  AutoArray<RString> _speechSingular;
  /// list of words for plural
  AutoArray<RString> _speechPlural;
};
TypeIsMovable(EntitySpeechVariant)

template <>
struct FindArrayKeyTraits<EntitySpeechVariant>
{
  typedef const RString &KeyType;
  static bool IsEqual(const RString &a, const RString &b) {return stricmp(a, b) == 0;}
  static const RString &GetKey(const EntitySpeechVariant &a) {return a._variant;}
};

/// set of speech variants
typedef FindArrayKey<EntitySpeechVariant> EntitySpeechVariants;

/// encapsulation of a single event handler
class EventHandler : public SerializeClass
{
protected:
  /// the unique identification
  int _id;
  /// the expression (TODO: precompiled code support)
  RString _handler;

public: 
  EventHandler() : _id(0), _handler("") {}
  EventHandler(int id, RString handler) : _id(id), _handler(handler) {}
  int GetId() const {return _id;}
  /// execute the event handler
  void Process(const GameValue &pars) const;
  /// execute the event handler with return value
  GameValue ProcessRet(const GameValue &pars) const;

  virtual LSError Serialize(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) { _id=0; _handler=RString(); }
};
TypeIsMovable(EventHandler);

/// set of event handlers attached to a single event type
class EventHandlers : public SerializeClass
{
protected:
  /// the first free id
  int _nextId;
  /// list of event handlers
  AutoArray<EventHandler> _handlers;

public:
  EventHandlers() {_nextId = 0;}
  /// Check if some handler is registered
  bool IsEmpty() const {return _handlers.Size() == 0;}
  /// Add a new handler, return its id
  int Add(RString expression);
  /// Remove the handler
  void Delete(int id);
  /// Remove all handlers
  void Clear();
  /// execute the event handlers
  void Process(const GameValue &pars) const;
  /// execute the event handlers, return last return value from last event handler processed
  GameValue ProcessRet(const GameValue &pars) const;

  virtual LSError Serialize(ParamArchive &ar);
  virtual bool IsDefaultValue(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) { _nextId=0; _handlers.Clear(); }
};

struct SynchronizedVariableInfo
{
  RString _name;
  float _coef;
  GameDataCode _handler;
};
TypeIsMovable(SynchronizedVariableInfo)

/// basic type description of any entity interacting with AI
class EntityAIType: public EntityType
{
  typedef EntityType base;

  // information common to all vehicles of the same type
  friend class EntityAI;
  friend class EntityAIFull;
  friend class VehicleTypeBank;
  friend class Transport;
  friend class Person;

protected:

  Ref<Texture> _picture; // UI picture
  Ref<Texture> _icon;    // UI Map icon
  Ref<Texture> _portrait; //command bar silet

  // side markings
  //AnimationSection _unitNumber,_groupSign,_sectorSign,_sideSign;
  AnimationSection _clan;
  AnimationSection _dashboard;

  AnimationSection _backLights;

  /// show total vehicle damage
  WoundTextureSelections _totalDamage;

  WeaponsType _weapons;

  RString _eventHandlers[NEntityEvent];

  AutoArray<RStringB> _storeVariables;

  RString _targetCategory;

public: // tired of writing access functions
  AutoArray<SynchronizedVariableInfo> _synchronizedVariables;

  // all attributes made public
  bool _shapeReversed;

  bool _forceSupply;
  bool _showWeaponCargo;

  float _camouflage; // some targets are very difficult to see
  float _audible; // audible/visible ratio (0..1)

  float _spotableNightLightsOff; // night spotability coefficients
  float _spotableNightLightsOn;
  float _spotableDarkNightLightsOff;

  float _visibleNightLightsOff; // night target recognition 
  float _visibleNightLightsOn;

  int _commanderCanSee;
  int _driverCanSee;
  int _gunnerCanSee;

  int _radarType;

  RString _displayName;
  RString _shortName;
  RString _nameSound;

  RString _textSingular;
  AutoArray<RString> _speechSingular;
  RString _textPlural;
  AutoArray<RString> _speechPlural;

  EntitySpeechVariants _speechVariants;

  TargetSide _typicalSide; // which side uses it normally
  DestructType _destrType;
  // typical destruction type
  // DestructDefault means auto detect

  int _weaponSlots;

  int _supplyPointIndex;
  float _supplyRadius;
  Vector3 _extCameraPosition;
  // Vector3 _extCameraUp;
  Vector3 _groupCameraPosition;

  bool _enableGPS; // true when minimap GPS should be enabled for this vehicle
  bool _enableWatch; // true if watch device is available in the vehicle
  bool _enableRadio; // true if radio station is available in the vehicle

  float _armor;
  float _invArmor;
  float _logArmor;
  float _cost;
  float _fuelCapacity;
  float _fuelConsumptionRate;

  /// limit telling when worth to fire to the vehicle
  float _damageResistance;

  float _secondaryExplosion;

  float _sensitivity; // sensitivity compared to man
  float _sensitivityEar; // sensitivity compared to man

  float _brakeDistance;
  float _precision;
  float _formationX,_formationZ;
  float _formationTime;
  float _invFormationTime;

  float _steerAheadSimul;
  float _steerAheadPlan;

  float _predictTurnSimul;
  float _predictTurnPlan;

  float _minFireTime;

  float _irScanRangeMin; //! min. IR scanner range
  float _irScanRangeMax; //! max. IR scanner range
  float _irScanToEyeFactor; //! ratio of IR scanner range to eye distance

  bool _alwaysTarget;
  bool _irTarget; // is IR lock possible?

  bool _irScanGround; // IR capable of tracking ground targets

  bool _attendant;
  bool _engineer;

  bool _nightVision;

  bool _preferRoads;
  bool _hideUnitInfo;

  ViewPars _viewPilot;
  HeadPars _headLimits;
  VehPosRelatives _pilotSpec;

  /// resources for unit info in HUD 
  AutoArray<RStringB> _unitInfoTypes;
  /// resources for unit info in HUD  - Lite version
  AutoArray<RStringB> _unitInfoTypesLite;

  UserAction _lockTargetAction;
  UserAction _lockEmptyTargetAction;

  int _itemsCargoWidth;
  int _itemsCargoHeight;

  int _clothingType;

  float _maxFuelCargo;
  float _maxRepairCargo;
  float _maxAmmoCargo;
  WeaponCargo _weaponCargo;
  MagazineCargo _magazineCargo;
  VehicleKind _kind;
  Threat _threat;

  int _lockDetectionSystem;
  int _incommingMisslieDetectionSystem;

  bool _driverCanEject;
  bool _cargoCanEject;

  bool _allowTabLock;
  ShowAllTargets _showAllTargets;

#if _HELISIM_PRECISE_INTERACTION
  bool _usePreciseGetInAction;
#endif

  float _minCost;
  float _maxSpeed; // max speed - level road
  /// how fast to move when speed mode is SpeedLimited (relative to _maxSpeed)
  float _limitedSpeedCoef;
  SoundPars _mainSound;
  SoundPars _envSound;
  SoundPars _dmgSound;
  SoundPars _crashSound;
  SoundPars _getInSound,_getOutSound;
  SoundPars _servoSound;
  SoundPars _landCrashSound;
  SoundPars _waterCrashSound;
  SoundPars _engineOnSoundInt;
  SoundPars _engineOffSoundInt;
  SoundPars _engineOnSoundExt;
  SoundPars _engineOffSoundExt;
  SoundPars _incommingSound;
  SoundPars _lockedSound;

  RandomSound _woodCrashSound;
  RandomSound _armorCrashSound;
  RandomSound _buildingCrashSound;

  // define sounds for surfaceSound, soundOverride pairs
  AutoArray<ExtSoundInfo2D> _extEnvSounds2D;
  /// max. volume of all _extEnvSounds
  float _extEnvSoundsMaxVol;

  AutoArray<ExtSoundInfo2D> _equipSounds2D;
  AutoArray<ExtSoundInfo2D> _gearSounds2D;

  //! Lights grouped in aggregation chunks
  ObjectTypeLights _lights;

  AutoArray<LightInfo> _lightinfos;
  AnimationSection _showDmg;
  Vector3 _showDmgPoint;

  //! Hidden selections - areas where texture can be hidden or changed
  AutoArray<AnimationSection> _hiddenSelections;
  //! Initial textures on hidden selections
  RefArray<Texture> _hiddenSelectionsInitTextures;
  RefArray<TexMaterial> _hiddenSelectionsInitMaterials;

  AutoArray<UserTypeAction> _userTypeActions;

  // if player is driver and in cocpit force view down by config value headAimDown
  float _headAimDown;

#if _ENABLE_INDEPENDENT_AGENTS && _ENABLE_IDENTITIES
  AutoArray<RString> _agentTasks;
#endif

public:
  EntityAIType( ParamEntryPar param );
  ~EntityAIType();

  /// create source
  AnimationSource *CreateAnimationSource(const AnimationType *type, RStringB source);
  /// create source with additional arguments
  virtual AnimationSource *CreateAnimationSourcePar(const AnimationType *type, ParamEntryPar entry);

  virtual void Load(ParamEntryPar par, const char *shape);

  // should be in World::_slowVehicles list
  virtual bool IsSlow() const;

  float GetCamouflage() const {return _camouflage;}
  float GetAudible() const {return _audible;}

  float GetSpotableDarkNightLightsOff() const {return _spotableDarkNightLightsOff;}
  float GetSpotableNightLightsOff() const {return _spotableNightLightsOff;}
  float GetSpotableNightLightsOn() const {return _spotableNightLightsOn;}

  float GetVisibleNightLightsOff() const {return _visibleNightLightsOff;}
  float GetVisibleNightLightsOn() const {return _visibleNightLightsOn;}

  TargetSide GetTypicalSide() const {return _typicalSide;}
  DestructType GetDestructType() const {return _destrType;}

  // who knows how much memory is controlled?
  virtual size_t GetMemoryControlled() const {return sizeof(*this);}
  // after shape is loaded
  virtual void InitShape();
  // before shape is unloaded
  virtual void DeinitShape();

  virtual bool IsShapeReady(const LODShape *lodShape, int level,
    const Ref<ProxyObject> *proxies, int nProxies, bool requestIfNotReady);
  virtual void InitShapeLevel(int level); //!< after particular LOD level is loaded
  virtual void DeinitShapeLevel(int level); // before particular LOD level is unloaded

#if _VBS3
  //returns index of HiddenSelection or -1 if not found
  int GetHiddenSelectionIndex(RString selection) const;
#endif

  /// Enumerate all turret types
  virtual bool ForEachTurret(ITurretTypeFunc &func) const {return false;}
  /// Check if any turrets are present
  virtual bool HasTurret() const {return false;}
  /// Find the turret by the path
  virtual const TurretType *GetTurret(const int *path, int size) const {return NULL;}

  /// can see targets? (used to disable target lists for animals, game logics ...)
  virtual bool ScanTargets() const {return true;}
  /// can be seen as targets? (used to disable target list presence for animals, game logics ...)
  virtual bool ScannedAsTarget() const {return true;}
  /// can provide actions? (used to disable invisible doors)
  virtual bool ShowGetIn() const {return true;}

  /// radius when the unit is being viewed (used to estimate the area)
  float VisibleSize() const;
  /// unit cost - price we need to pay to buy/produce the unit
  __forceinline float GetCost() const {return _cost;}
  /// armor strength- mm
  __forceinline float GetArmor() const {return _armor;}
  //// 1/GetArmor()
  __forceinline float GetInvArmor() const {return _invArmor;}
  __forceinline float GetLogArmor() const {return _logArmor;}
  /// fuel tank capacity (litres)
  __forceinline float GetFuelCapacity() const {return _fuelCapacity;}
  __forceinline float GetFuelConsumptionRate() const {return _fuelConsumptionRate;}


  virtual float GetEntityArmor() const {return _armor;}

  __forceinline UserAction GetLockTargetAction() const {return _lockTargetAction;}
  __forceinline UserAction GetLockEmptyTargetAction() const {return _lockEmptyTargetAction;}

  // cost: time necessary for 1m travel
  virtual float GetBaseCost(const GeographyInfo &info, bool operative, bool includeGradient) const {return 1.0f;}
  // cost: multiplier based on the slope gradient
  virtual float GetGradientPenalty(float gradient) const {return 1.0f;}
  // cost: 16 segments
  virtual float GetCostTurn(int difDir, int &debet, bool forceFullTurn) const;
  virtual float GetFieldCost( const GeographyInfo &info, CombatMode mode ) const {return 1.0f;}
  // cost based on buildings height, used for airplanes and helicopters
  virtual float GetAirCost(float x, float z) const {return 1;};
  virtual FieldCost GetRoadCost(CombatMode mode) const;
  virtual FieldCost GetTypeCost(OperItemType type, CombatMode mode, Clearance clearance) const;

  /// estimate how much can the cost be underestimated in the operative planning
  virtual float GetCostModeFactor(CombatMode mode) const {return 1.0f;}

  virtual float GetPathCost(const GeographyInfo &info, float dist, CombatMode mode) const;
  /// some vehicles (e.g. boats) are somewhat more tolerant to straying off the planned path
  virtual void GetPathPrecision(float speedAtCost, float &safeDist, float &badDist) const;

  __forceinline bool GetAlwaysTarget() const {return _alwaysTarget;}
  __forceinline bool GetIRTarget() const {return _irTarget;} // is IR lock possible?
  __forceinline bool GetLaserTarget() const {return false;} // is laser lock possible?
  __forceinline bool GetNvTarget() const {return false;} // is nv lock possible
  float GetIRScanRange() const;
  __forceinline bool GetLaserScanner() const {return false;}
  __forceinline bool GetIRScanGround() const {return _irScanGround;} // IR tracks ground targets
  __forceinline bool GetNVScanner() const {return true;}
  /// check if type is known to be flying
  virtual bool CanBeAirborne() const {return false;}

  __forceinline bool GetNightVision() const {return _nightVision;}

  __forceinline bool PreferRoads() const {return _preferRoads;}
  __forceinline const AutoArray<RStringB> &GetUnitInfoTypes() const {return _unitInfoTypes;}
  __forceinline const AutoArray<RStringB> &GetUnitInfoTypesLite() const {return _unitInfoTypesLite.Size() > 0? _unitInfoTypesLite:_unitInfoTypes;}

  __forceinline float GetMaxSpeed() const {return _maxSpeed;}
  float GetTypSpeed() const {return _maxSpeed*0.66f;}
  float GetLimitedSpeed() const {return _maxSpeed * _limitedSpeedCoef;}

  float GetMaxSpeedMs() const {return _maxSpeed*(1.0f/3.6f);}
  float GetTypSpeedMs() const {return _maxSpeed*(1.0f/3.6f)*0.66f;}
  float GetLimitedSpeedMs() const {return _maxSpeed*(1.0f/3.6f)*_limitedSpeedCoef;}

#if _HELISIM_IMPERIAL_SYSTEM
  //convert units to imperial system
  virtual float ConvertDistance(float meters, int &stringIDS) const;
  virtual float ConvertSpeed(float metersPerSecond) const;
  virtual float ConvertHeight(float height) const;
  virtual float ConvertVerticalSpeed(float metersPerSecond) const;
#endif

  __forceinline float GetMinCost() const {return _minCost;}

  __forceinline float GetSteerAheadSimul() const {return _steerAheadSimul;}
  __forceinline float GetSteerAheadPlan() const {return _steerAheadPlan;}

  __forceinline float GetPredictTurnSimul() const {return _predictTurnSimul;}
  __forceinline float GetPredictTurnPlan() const {return _predictTurnPlan;}

  /// some vehicles are unable or do not need to use better precision than a strategic plan gives
  virtual bool StrategicPlanningOnly() const {return false;}
  /// some vehicles cannot use roads (aircraft, boats)
  virtual bool CanUseRoads() const {return true;}

  __forceinline float GetMinFireTime() const {return _minFireTime;} 

  virtual bool HasDriver() const {return true;}
  virtual bool HasGunner() const {return false;}
  virtual bool HasObserver() const {return false;}
  virtual bool HasCargo() const {return false;}

  virtual bool IsAnimal() const {return false;}

  // how far is it necessary to plan for get out and other stopping
  virtual float GetStopDistance() const {return floatMax(GetPrecision(),GetBrakeDistance());}

  __forceinline float GetBrakeDistance() const {return _brakeDistance;}
  __forceinline float GetPrecision() const {return _precision;}
  __forceinline float GetFormationX() const {return _formationX;}
  __forceinline float GetFormationZ() const {return _formationZ;}
  __forceinline float GetFormationTime() const {return _formationTime;}
  __forceinline float GetInvFormationTime() const {return _invFormationTime;}

  __forceinline int GetSupplyPointIndex() const {return _supplyPointIndex;}
  __forceinline float GetSupplyRadius() const {return _supplyRadius;}

  __forceinline float GetMaxFuelCargo() const {return _maxFuelCargo;}
  __forceinline float GetMaxRepairCargo() const {return _maxRepairCargo;}
  __forceinline float GetMaxAmmoCargo() const {return _maxAmmoCargo;}
  __forceinline int GetItemsCargoWidth() const {return _itemsCargoWidth;}
  __forceinline int GetItemsCargoHeight() const {return _itemsCargoHeight;}

  __forceinline const WeaponCargo &GetWeaponCargo() const {return _weaponCargo;}
  __forceinline const MagazineCargo &GetMagazineCargo() const {return _magazineCargo;}
  __forceinline bool IsAttendant() const {return _attendant;}
  __forceinline bool IsEngineer() const {return _engineer;}

  __forceinline VehicleKind GetKind() const {return _kind;}
  __forceinline Threat GetThreat() const {return _threat;}

  __forceinline int GetLockDetectionSystem() const {return _lockDetectionSystem;}
  __forceinline int GetIncommingMissileDetectionSystem() const {return _incommingMisslieDetectionSystem;}

#if _HELISIM_PRECISE_INTERACTION
  __forceinline bool UsePreciseGetInAction() const {return _usePreciseGetInAction;}
#endif

  __forceinline const SoundPars &GetMainSound() const {return _mainSound;}
  __forceinline const SoundPars &GetEnvSound() const {return _envSound;}
  __forceinline const SoundPars &GetDmgSound() const {return _dmgSound;}
  __forceinline const SoundPars &GetCrashSound() const {return _crashSound;}
  __forceinline const SoundPars &GetGetInSound() const {return _getInSound;}
  __forceinline const SoundPars &GetGetOutSound() const {return _getOutSound;}
  __forceinline const SoundPars &GetServoSound() const {return _servoSound;}
  __forceinline const SoundPars &GetLandCrashSound() const {return _landCrashSound;}
  __forceinline const SoundPars &GetWaterCrashSound() const {return _waterCrashSound;}
  __forceinline const SoundPars &GetBuildingCrashSound(float probability) const {return _buildingCrashSound.SelectSound(probability);}
  __forceinline const SoundPars &GetWoodCrashSound(float probability) const {return _woodCrashSound.SelectSound(probability);}
  __forceinline const SoundPars &GetArmorCrashSound(float probability) const {return _armorCrashSound.SelectSound(probability);}
  __forceinline const SoundPars &GetIncommingMissileSound() const {return _incommingSound;}
  __forceinline const SoundPars &GetLockedSound() const {return _lockedSound;}

  const SoundPars &GetEnvSoundExtRandom(RString surfaceSound, RString soundOverride, const char * feetWear) const;
  const SoundPars* GetEquipSoundExtRandom(RString side, RString soundOverride) const {return GetRandomSound(_equipSounds2D, side, soundOverride);}
  const SoundPars* GetMunitionSoundRandom(RString type, RString soundOverride) const {return GetRandomSound(_gearSounds2D, type, soundOverride);}
  const SoundPars* GetRandomSound(const AutoArray<ExtSoundInfo2D> &data, RString first, RString second) const;
  void LoadExtSoundInfo2D(ParamEntryVal &entry, AutoArray<ExtSoundInfo2D> &data);
  void LoadExtSoundInfo(ParamEntryVal &entry, AutoArray<ExtSoundInfo> &data);

  /// get max. possible volume of env. sound
  /**
  Used for heuristics to determine if the sound can be audible or not
  */
  float GetEnvSoundMaxVol() const {return _extEnvSoundsMaxVol;}

  //RString GetName() const {return _parClass->GetName();}

  __forceinline const RString &GetDisplayName() const {return _displayName;}
  __forceinline const RString &GetShortName() const {return _shortName;}
  __forceinline const RString &GetNameSound() const {return _nameSound;}

  __forceinline const RString &GetTextSingular() const {return _textSingular;}
  __forceinline const AutoArray<RString> &GetSpeechSingular() const {return _speechSingular;}
  __forceinline const RString &GetTextPlurar() const {return _textPlural;}
  __forceinline const AutoArray<RString> &GetSpeechPlurar() const {return _speechPlural;}

  const EntitySpeechVariant *GetSpeechVariant(const RString &variant) const
  {
    int index = _speechVariants.FindKey(variant);
    if (index < 0) return NULL;
    return &_speechVariants[index];
  }

#if _ENABLE_CONVERSATION
  virtual const KBCenter *GetKBCenter() const {return NULL;}
#endif

  __forceinline Texture *GetIcon() const {return _icon;}

  /// check max. weapon range for given vehicle type
  virtual float GetMaxWeaponRange() const;
  // weapons and magazines
  int NWeaponSystems() const {return _weapons._weapons.Size();}
  const WeaponType *GetWeaponSystem(int i) const {return _weapons._weapons[i];}
  int NMagazines() const {return _weapons._magazines.Size();}
  const MagazineType *GetMagazine(int i) const {return _weapons._magazines[i];}

  bool IsSupply() const;
  /// check whether supply can contain weapons, magazines or other items
  bool CanStoreWeapons() const;

  virtual const DoorsHolder *GetDoors() const {return NULL;};
  int GetTargetCategory() const;
};

typedef EntityAIType VehicleType;

LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityType> &value, int minVersion);
LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityType> &value, int minVersion, const EntityType *defValue);

LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityAIType> &value, int minVersion);
LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityAIType> &value, int minVersion, const EntityAIType *defValue);

const float MaxDammageWorking=0.75; // can move/attack

class Person; // basic driver type
struct TurretContext;
struct WeaponModePtr;

struct UNIT_STATE;

//! recalculate and reuse if possible line of sight to target
class VisibilityTracker
{
  friend class VisibilityTrackerCache;

  OLinkO(EntityAI) _obj;

  /// which turret is this from (multiple turrets may give different results)
  Turret * _turret;
  /// position for which we performed the last test
  //Vector3 _lastPos;
  /// time when the last test was performed
  Time _lastTime;
  /// result of the last test
  float _lastValue;
  /// was the last test result we need to aim at the head?
  bool _lastHeadOnly;
  //@{ suppressive fire support
  /// was _lastVisiblePosition ever set?
  bool _lastVisiblePositionValid;
  /// last position where we have seen the unit
  Vector3 _lastVisiblePosition;
  /// time when _lastVisiblePosition was acquired (last time we have clear LOF to the target)
  Time _lastVisiblePositionTime;
  /// result of the last test for _lastVisiblePosition
  float _lastValueForLastVisiblePos;
  /// time when the last test for _lastVisiblePosition was performed
  Time _lastTimeForLastVisiblePos;
  //@}

public:
  VisibilityTracker();
  VisibilityTracker( EntityAI *obj );
  ~VisibilityTracker();

  //! get visibility value or calculate a new one if recent is not available
  float Value(bool &headOnly, const EntityAIFull *sensor, const TurretContext &context, const WeaponModePtr &ptr, float reserve, float maxDelay);
  /// suppressive fire alternative for Value
  float ValueForLastVisible(Time &time, const EntityAIFull *sensor, const TurretContext &context, const WeaponModePtr &ptr, float reserve, float maxDelay);
};

TypeContainsOLink(VisibilityTracker);

//! calculate and cache visibility value for several targets

class VisibilityTrackerCache
{
  AutoArray<VisibilityTracker> _trackers;

public:
  VisibilityTrackerCache();
  ~VisibilityTrackerCache();

  void Clear();

  //! check for a known value, never compute a new one
  float CheckKnownValue(bool &headOnly, const EntityAIFull *sensor, const TurretContext &context, const WeaponModePtr &ptr, EntityAI *obj,
    float reserve=1.0f, float maxDelay=0.3f);
  //! calculate and cache visibility value
  float Value(bool &headOnly, const EntityAIFull *sensor, const TurretContext &context, const WeaponModePtr &ptr, EntityAI *obj,
    float reserve=1.0f, float maxDelay=0.3f);
  //! calculate and cache visibility value unless too many values are already cached
  float KnownValue(bool &headOnly, const EntityAIFull *sensor, const TurretContext &context, const WeaponModePtr &ptr, EntityAI *obj,
    float reserve=1.0f, float maxDelay=0.3f);
  bool GetLastKnownVisiblePosition(Vector3 &ret, EntityAI *obj) const;
  float ValueForLastVisible(Time &time, const EntityAIFull *sensor, const TurretContext &context, const WeaponModePtr &ptr, EntityAI *obj,
    float reserve=1.0f, float maxDelay=0.3f);
};

class EntityAI;

typedef EntityAI TargetType;

typedef OLink(TargetType) TargetId;

class Target;
class TargetList;

class LinkTarget: public LLink<Target>
{
public:
  LinkTarget(){}
  LinkTarget( Target *tgt ):LLink<Target>(tgt){}

  TargetType *IdExact() const;
};


class ClothObject;
class ClothObjectType;

class FlagType: public EntityType
{
  typedef EntityType base;

  friend class Flag;

  SRef<ClothObjectType> _clothType;

  AnimationSection _fabric;

public:
  explicit FlagType(ParamEntryPar param);

  bool AbstractOnly() const {return false;}

  void InitShape();
  void DeinitShape();
  void InitShapeLevel(int level);
  void DeinitShapeLevel(int level);

  virtual Object* CreateObject(bool unused) const;

  void InitFabric();
};

class Flag : public Entity
{
protected:
  typedef Entity base;
  SRef<ClothObject> _cloth;

  Ref<Texture> _texture;

public:
  Flag(const EntityType *name, const CreateObjectId &id);
  ~Flag();

  const FlagType *Type() const {return static_cast<const FlagType *>(GetNonAIType());}

  /// non-blocking init, returns true once completed
  bool InitAsync(Matrix4Par pos);
  void Init(Matrix4Par pos, bool init);
  void FlagSimulate( Matrix4Par pos,float deltaT, SimulationImportance prec );

  AnimationStyle IsAnimated(int level) const {return AnimatedGeometry;}

  virtual void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  virtual void Deanimate(int level, bool setEngineStuff);

  void SetFlagTexture(Texture *texture);

  USE_CASTING(base)
};

//!< Combat state of target

enum TargetState
{
  TargetDestroyed, //! Any target destroyed
  TargetAlive, //! Any target alive
  TargetEnemyEmpty, //! Enemy target, cannot move
  TargetEnemy, //! Enemy target, cannot fire (no weapons or weapons broken)
  TargetEnemyCombat, //! Enemy target, can both move and fire
  TargetFireMin=TargetEnemy
};

struct WeaponSounds;
struct Muzzle;

// Pointer to the full definition of the weapon's mode 
struct WeaponModePtr : public SerializeClass
{
  OLinkPermO(Weapon) _weapon;
  int _muzzle;
  int _mode;

  WeaponModePtr()
  {
    _muzzle = -1;
    _mode = -1;
  }
  
  const WeaponType *GetWeaponType() const;
  const MuzzleType *GetMuzzleType() const;
  const WeaponModeType *GetWeaponModeType() const;
  Magazine *GetMagazine();
  const Magazine *GetMagazine() const;
  const AmmoType *GetAmmoType() const;
  const OpticsInfo *GetOpticsInfo() const;
  const float SumDispersionModifiers(const EntityAIFull *agent) const;
  const float SumDexterityModifiers(const EntityAIFull *agent) const;

  const OpticsCameraInfo *GetOpticsCameraInfo() const;
  Muzzle &GetMuzzle() const;
  int GetDiscreteIndex() const;
  int GetDiscreteDistanceIndex() const;
  WeaponSounds *GetSound() const;
  bool ShowToPlayer() const;

  void SetReloadMagazineProgress(float progress) const;
  void SetDrawBowProgress(float progress) const;

  bool IsValid() const;
  bool operator ==(const WeaponModePtr &with) const;
  bool operator !=(const WeaponModePtr &with) const;

  // SerializeClass implementation
  virtual bool IsDefaultValue(ParamArchive &ar) const override;
	virtual LSError Serialize(ParamArchive &ar) override;
	virtual void LoadDefaultValues(ParamArchive &ar) override;
};

TypeContainsOLink(WeaponModePtr)

#ifndef DECL_ENUM_UI_ACTION_TYPE
#define DECL_ENUM_UI_ACTION_TYPE
DECL_ENUM(UIActionType)
#endif

//! description of user (designer) defined action
struct UserActionDescription
{
  int id;
  RString text;
  RString script;
  GameValue param;

  float priority;
  bool showWindow;
  bool hideOnUse;
  UserAction shortcut;

#if _HELISIM_PRECISE_INTERACTION
  RString modelPositions;
  float radius;
  float radiusView;
  int showIn3D;
  int available;
  RString textDefault;
  RString textToolTip;
  int userActionID;
#endif

  /// when the action can be added
  RString condition;
#if USE_PRECOMPILATION
  CompiledExpression conditionExp;
#endif

  LSError Serialize(ParamArchive &ar);
};
TypeIsMovable(UserActionDescription)

/// information about the sound
struct WeaponFiredSound
{
  /// sound
  RefAbstractWave _sound;
  /// time when the sound started to play
  Time _time;
};

TypeIsGeneric(WeaponFiredSound)

///
struct WeaponSounds
{
private:
  // time when the sound started to play
  //Time _lastAutoTime;
  // entity
  EntityAIFull *_entity;
  // fire sound
  WeaponFiredSound _fireSound;
  // bullet sound
  RefAbstractWave _bulletSound;
  // empty magazine sound
  RefAbstractWave _emptyMagSound;
  // weapon position
  Vector3 _position;
  // number of bullets
  int _count;
  // time in which first bullet fall to the ground
  Time _delay;
  // time in which next bullet fall to the ground
  Time _nextBulletTime;
  // weapon reload time
  float _reloadTime;

public:
  WeaponSounds(): _count(0), _entity(0) {}

  // play shot sound
  void ShotSound(EntityAIFull *entity, const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine);
  void UpdateSound(const TurretContext &context, const WeaponModePtr &ptr, const Vector3 &pos, const Vector3 &speed);
  void PlayEmptyMagazineSound(EntityAIFull *entity, const TurretContext &context, const WeaponModePtr &ptr, const SoundPars &pars);
  void SetStopTime(float time);
  void UnloadSound();

private:
  void UpdateBulletSound(const TurretContext &context, const WeaponModePtr &ptr);
  void RegisterBulletSound();
};

TypeIsMovable(WeaponSounds);

/// interface for an owner of WeaponssState, weapons and magazines
class IWeaponsHolder
{
public:
  IWeaponsHolder() {}

  virtual void Compact() = 0;

  virtual bool ForEachWeapon(const std::function<bool(Weapon *weapon)> &func) = 0;
  virtual bool ForEachWeapon(const std::function<bool(const Weapon *weapon)> &func) const = 0;
  virtual bool AddWeapon(EntityAIFull *entity, Weapon *weapon) = 0;
  virtual bool RemoveWeapon(Weapon *weapon) = 0;
  virtual bool DeleteWeapon(RStringB name) = 0;
  virtual void DeleteWeapons(const std::function<bool(const Weapon *weapon)> &func) = 0;
  virtual void DeleteAllWeapons() = 0;

  virtual bool ForEachMagazine(const std::function<bool(Magazine *)> &func) = 0;
  virtual bool AddMagazine(EntityAIFull *entity, Magazine *magazine) = 0;
  virtual bool RemoveMagazine(Magazine *magazine) = 0;
  virtual bool DeleteMagazine(RStringB name) = 0;
  virtual void DeleteMagazines(const std::function<bool(const Magazine *magazine)> &func) = 0;
  virtual void DeleteAllMagazines() = 0;

  virtual WeaponModePtr GetCurrentWeapon() const = 0;
  virtual void SetCurrentWeapon(const WeaponModePtr &ptr) = 0;
};

/// information about state of weapons controlled by a single gunner
struct WeaponsState
{
  IWeaponsHolder &_owner;

  Link<AbstractWave> _reloadSound; //!< Playing sound of reloading weapon
  Link<AbstractWave> _reloadMagazineSound; //!< Playing sound of reloading magazine

  //!< Playing sound of the weapon
  //AutoArray<WeaponFiredSound> _firedSound;

  //////////////////////////////////////////////////////////////////////////
  // Playing sound of the weapon
  // TODO: remove _firedSound
  RandomSound _bullets;
  //////////////////////////////////////////////////////////////////////////

  WeaponModePtr _forceFireWeapon; //!< Index of weapon to fire ordered by GameStateExt::ObjFire

  WeaponModePtr _currentCounterMeasures;  //!< Index of CM to launch
  /// safe access to _currentWeapon including a range check
  WeaponModePtr ValidatedCurrentWeapon() const;
  /// safe access to _currentCounterMeasures including a range check
  WeaponModePtr ValidatedCurrentCounterMeasures() const {return ValidateWeapon(_currentCounterMeasures);}

  // select the current weapon only, no additional actions and checks
  void SetCurrentWeapon(const WeaponModePtr &ptr);

  //}@

  //@{ parameters of particle effects
  WeaponFireSource _gunFire;
  WeaponCloudsSource _gunClouds;
  WeaponLightSource _mGunFire;
  WeaponCloudsSource _mGunClouds;
  //@}

  //@{ weapon fire animation
  int _mGunFireUntilFrame;
  Time _mGunFireTime;
  int _mGunFirePhase;
  //@}

  /// Last shot fired (useful e.g. for manual mission control)
  OLinkPermO(Entity) _lastShot; 
  /// Time of last shot fired
  Time _lastShotTime; 

  /// Time of last performed suppressive fire trace
  Time _lastTrace;

  Ref<Target> _lockingTarget;
  Ref<Target> _aimedTarget;

  WeaponsState(IWeaponsHolder &owner);
  ~WeaponsState();
  
  void Init(const WeaponsType &type, ParamEntryPar cfg, EntityAIFull *owner, bool createWeapons);
  void Compact();

  // check if some weapons / muzzles / modes are present 
  bool HasWeapons() const;
  /// check if weapon mode is consistent, return invalid if not
  WeaponModePtr ValidateWeapon(const WeaponModePtr &ptr) const;
  /// check if the given weapon mode is valid, if not, find a first valid
  WeaponModePtr FindValidWeapon(const WeaponModePtr &ptr) const;
  /// check if the weapon is the first valid one
  bool IsDefaultWeapon(const WeaponModePtr &ptr) const;

  // traverse all magazines (loaded magazines are excluded)
  // when some call of func() returns true, ForEachMagazine ends immediatelly with true
  // otherwise returns false
  bool ForEachMagazine(const std::function<bool(Magazine *)> &func);

  // traverse all weapons
  // when some call of func() returns true, ForEachMagazine ends immediatelly with true
  // otherwise returns false
  bool ForEachWeapon(const std::function<bool(Weapon *weapon)> &func);

  // traverse all muzzles of all weapons
  // when some call of func() returns true, ForEachWeaponMode ends immediatelly with true
  // otherwise returns false
  bool ForEachMuzzle(const std::function<bool(const WeaponModePtr &)> &func) const;

  // traverse all modes in all muzzles of all weapons
  // when some call of func() returns true, ForEachWeaponMode ends immediatelly with true
  // otherwise returns false
  bool ForEachWeaponMode(const std::function<bool(const WeaponModePtr &)> &func) const;

  //! add a weapon
  bool AddWeapon(EntityAIFull *entity, Weapon *weapon, bool reload=true, bool checkSelected=true);
  //! remove a weapon
  void RemoveWeapon(EntityAIFull *entity, Weapon *weapon, bool checkSelected=true);
  //! add new weapon
  bool CreateWeapon(EntityAIFull *entity, RStringB name, bool reload=true, bool checkSelected=true);
  //! remove and destroy weapon
  void DeleteWeapon(EntityAIFull *entity, RStringB name, bool checkSelected=true);
  //! remove and destroy all weapons
  void DeleteAllWeapons(EntityAIFull *entity, bool removeItems);

  //! add a magazine
  bool AddMagazine(EntityAIFull *entity, Magazine *magazine, bool autoload=false);
  //! remove a magazine
  void RemoveMagazine(EntityAIFull *entity, Magazine *magazine);
  //! add new magazine
  bool CreateMagazine(EntityAIFull *entity, RStringB name);
  //! add new magazine and set ammo count
  bool CreateMagazineAmmoCount(EntityAIFull *entity, RStringB name, int ammoCount);
  //! remove magazine
  void DeleteMagazine(EntityAIFull *entity, RStringB name);
  //! remove all magazines of given type
  void DeleteMagazines(EntityAIFull *entity, RStringB name);
  //! remove all magazines
  void DeleteAllMagazines(EntityAIFull *entity);

  void SelectWeapon(EntityAIFull *entity, const WeaponModePtr &ptr, bool changed=false);
  void SelectCounterMeasures(const EntityAIFull *entity, const WeaponModePtr &ptr, bool changed=false);

  //! find magazine - candidate for automatic reload
  Magazine *FindMagazineByType(const MuzzleType *muzzle, const MagazineType *oldMagazineType=NULL);
  //! find best (the most full) magazine of given type
  Magazine *FindBestMagazine(const MagazineType *type, int ammo);

  //! Check if slot is empty
  bool EmptySlot(const WeaponModePtr &ptr) const;

  //! Check max. primary level of non-empty weapons
  int MaxPrimaryLevel() const;
  //! Get index of first most primary weapon in magazineSlots
  WeaponModePtr FirstWeapon(const EntityAIFull *entity) const;
  //! Switch to next primary weapon
  WeaponModePtr NextWeapon(const EntityAIFull *entity, const WeaponModePtr &start) const;
  //! Switch the mode of the weapon
  WeaponModePtr NextWeaponMode(const EntityAIFull *entity, const WeaponModePtr &start) const;
  //! Switch to next possible optics mode
  void NextOpticsMode(Person *person);

  WeaponModePtr FirstCM(const EntityAIFull *entity);
  WeaponModePtr NextCM(const EntityAIFull *entity);

  //! Function returns whether fire of the weapon should be shown or not
  bool ShowFire() const;

  //! Serialization of weapons state/
  LSError Serialize(EntityAIFull *owner, ParamArchive &ar);

  void GetVisionModePars(VisionMode &vMode, int &flirIndex) const;
  bool NextVisionMode();
  bool HasVisionModes() const;
  int VisionModesCount() const;
  bool HasCurrentWeaponTi() const;
};

class LightReflectorOnVehicle;

class ResourceSupply;
struct NetworkMessageUpdateSupply;
struct NetworkMessageMagazine;

#define UPDATE_VEHICLE_AI_MSG(MessageName, XX) \
  XX(MessageName, bool, pilotLight, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Lights are on / off"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, bool, collisionLight, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("collision lights are on / off"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, SRef<ResourceSupply>, supply, NDTObjectSRef, REF_MSG(NetworkMessageUpdateSupply), NCTNone, DEFVALUE_MSG(NMTUpdateSupply), DOC_MSG("Resource supply object"), TRANSF_CONTENT, ET_ABS_DIF, 1) \
  XX(MessageName, OLink(EntityAI), assembleTo, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("weapon to be assembled"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, EntityAI *, variables, NDTVariables, Variables, NCTNone, DEFVALUE(Variables, Variables()), DOC_MSG("synchronized variables"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, AutoArray<RStringB>, textures, NDTStringBArray, AutoArray<RStringB>, NCTNone, DEFVALUESTRINGBARRAY, DOC_MSG("setObjectTexture synchronization"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_NORMAL) \
  XX(MessageName, AutoArray<RStringB>, materials, NDTStringBArray, AutoArray<RStringB>, NCTNone, DEFVALUESTRINGBARRAY, DOC_MSG("setObjectMaterial synchronization"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_NORMAL) \

DECLARE_NET_INDICES_EX_ERR(UpdateVehicleAI, UpdateVehicle, UPDATE_VEHICLE_AI_MSG)

#define CREATE_VEHICLE_AI_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<SynchronizedVariableInfo>, variables, NDTSynchronizedVariables, AutoArray<SynchronizedVariableInfo>, NCTNone, AutoArray<SynchronizedVariableInfo> defValue, DOC_MSG("synchronized variables"), TRANSF) \

DECLARE_NET_INDICES_EX(CreateVehicleAI, CreateVehicle, CREATE_VEHICLE_AI_MSG)


#define UPDATE_DAMMAGE_VEHICLE_AI_MSG(MessageName, XX) \
  XX(MessageName, bool, isDead, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Unit is destroyed (unusable)"), TRANSF, ET_NOT_EQUAL, ERR_COEF_STRUCTURE)

DECLARE_NET_INDICES_EX_ERR(UpdateDamageVehicleAI, UpdateDamageVehicle, UPDATE_DAMMAGE_VEHICLE_AI_MSG)

#define UPDATE_VEHICLE_AI_FULL_MSG(MessageName, XX)

DECLARE_NET_INDICES_EX_ERR(UpdateVehicleAIFull, UpdateVehicleAI, UPDATE_VEHICLE_AI_FULL_MSG)

class GameValue;
//enum UserAction;

typedef OLinkPermNOArray(AIBrain) SupplyUnitList;

typedef float (EntityAI::*SupportCheckF)() const;

struct TurretContext;
struct TurretContextV;
struct TurretContextEx;
struct AILockerSavedState;

//! Structure to hold group of aggregated lights
struct AggLightGroup
{
  RefArray<LightReflectorOnVehicle> _realReflectors;
  Ref<LightReflectorOnVehicle> _aggReflector;

  RefArray<LightPointOnVehicle> _realPointLights;
  Ref<LightPointOnVehicle> _aggPointLight;
};
TypeIsMovable(AggLightGroup)

/**
alowDammage was not enabled on Retail PC because of possible cheating, but the cheating potential of this particular
seems no longer important.
*/
#define ENABLE_ALLOW_DAMAGE 1

/// interface for functor passed to Transport::ForEachCrew or Person::ForEachCrew
class Person;
class ICrewFunc
{
public:
  virtual bool operator () (Person *entity) = 0;
};

//! Class that is usually associated with an object stores lights in an aggregated form
class ObjectLights
{
protected:
  //! Each item of the array contains group of aggregated lights
  AutoArray<AggLightGroup> _aggLights;
  /// is the object already listed via GWorld->AddAttachment?
  bool _objectIsAttached;
public:
  ObjectLights();
  //! Updates scene with the aggregated lights
  void UpdateAggLights(EntityAI *object);
  //! Hide or show selections for lights depending on damage (hit) and
  void Animate(AnimationContext &animContext, int level, const EntityAI *object, const ObjectTypeLights &typeLights);

  void Animate(AnimationContext &animContext, int level, const EntityAI *object, const Turret *turret, const ObjectTypeLights &typeLights);
  void Create(EntityAI *object, const ObjectTypeLights &typeLights);
  void Switch(bool on, const EntityAI *object, const ObjectTypeLights &typeLights);
  void Update(const EntityAI *object, const ObjectTypeLights &typeLights);
  void AnimatePosition(const EntityAI *object, const ObjectTypeLights &typeLights);
  void TransformPosition(const PositionRender &parentTransform);
  void Clear();
  void SwitchLight(bool on);
  bool SomeLights() const {return _aggLights.Size() > 0;}
};

#if _VBS2
#include "hla/VBSVisuals.hpp"
#endif

/// describe position of sensor / target for visibility testing
/** including important state, like stance or leaning */

struct SensorPos
{
  /// basic property for all entities is a position
  Vector3 pos;
  /// any entity may define any 2 more optional properties with any meaning
  /** 4 makes sure we are still 128b aligned on SIMD platforms */
  float optional[4];

  void Init()
  {
    pos = VZero;
    for (int i=0; i<4; i++)
      optional[i] = 0.0f;
  }
};


// used for VBS
struct HitEventElement
{
  const AmmoType *_shot;
  EntityAI *_shoter;
  Vector3   _pos;
  Vector3   _velocity;
  AutoArray<RString> _component;
  Vector3   _normal;
  float     _radius;
  RString   _surfaceType;
  float     _impulse;
  bool      _directHit;
  SkinnedPosition _skinning;

  void Init
    (
    const AmmoType *shot,
    EntityAI *shoter, 
    Vector3   pos,
    Vector3   velocity,
    AutoArray<RString> component,
    Vector3  normal,
    float    Radius,
    RString  surfaceType,
    bool     directHit,
    float    impulse,
    SkinnedPosition *skinning
    )
  {
    _shot = shot;
    _shoter = shoter;
    _pos = pos;
    _velocity = velocity;
    _component = component;
    _normal = normal;
    _radius = Radius;
    _surfaceType = surfaceType;
    _directHit = directHit;
    _impulse = impulse;
    if (skinning) _skinning = *skinning;
  }
};
TypeIsGeneric(HitEventElement)


const float MaxSimplePathAge = 0.5f;
const float MaxSearchedPathAge = 2.0;

/// AI enabled entity
/** entity which can interact with AI or act as AI */

class EntityAI: public Entity, public JoinedObject, public IVariables
{
  typedef Entity base;

public:

  //! State during pausing for fire
  enum FireState
  {
    FireInit, //!< Fire decision was just made
    FireAim, //!< During aiming
    FireAimed, //!< Aimed, wait for shot
    FireDone, //!< Success or time-out, continue normal activity
  };

#if _VBS2
  HistoryLine &GetHistoryLine(){ return _historyLine; };
# if _EXT_CTRL
  //! Query the objects replay statistics
  const AARStat &GetStat(){ return _replayStat[GetTargetSideAsIfAlive()];};
  const AARStat &GetStat(TargetSide side){ return _replayStat[side]; };
  //! Sets the objects statistics during replay,
  //! Called directly from AAR, during replay
  void SetStat(const AARStat &stat, TargetSide side){ _replayStat[side] = stat; };
# endif
#endif
protected:  
  Vector3 _stratGoToPos; //!< Position the vehicle is trying to reach

#if _VBS2 //convoy trainer
  Matrix4 _postFrame;
  HistoryLine _historyLine; 
  // Entities after action information
# if _EXT_CTRL
  AARStat _replayStat[TSideUnknown];
# endif
#endif
  bool _isDead; //!< Entity dead and no longer usable
  bool _isStopped; //!< Entity is stable - no simulation required
  bool _inFormation; //!< Pilot has reached destination
  bool _showFlag; //!< Draw flag (Capture the flag support)

  bool _isUpsideDown; //!< Vehicle is upside down (crew must get out)

  bool _userStopped; //!< Stopped by user
  bool _pilotLight; //!< Light on
  bool _collisionLight; //!< Light on
#if _VBS3 //specifies when the light is visible, see also VisionMode
  int _lightMode; //!used for LaserPointer
#endif

  // patched
#if ENABLE_ALLOW_DAMAGE
  bool _allowDamage; //!< Damage enabled
#endif

  //@{ locking
  bool _locked; //!< Should be locked
  bool _lockedAsWaiting; //!< locked only because of waiting
  Ref<AILocker> _locker; //!< locker helper
  //@}

  SRef<ResourceSupply> _supply; //!< Resource supply implementation

  RString _surfaceSound; //!< Sound on current surface (used in Man)

  Time _lastMovement; //!< When last movement prevented stopping

  //@{ collision avoidance
  /// list of vehicles we are avoiding
  struct AvoidItem
  {
    /// whom are we avoiding?
    OLinkO(EntityAI) _who;
    /// what time do we expect to stop avoiding?
    Time _until;
  
    AvoidItem(){}
    AvoidItem(const EntityAI *ai, Time until=TIME_MIN):_who(unconst_cast(ai)),_until(until){}
    bool operator == (const AvoidItem &a) const {return _who==a._who;}
    ClassContainsOLink(AvoidItem);
  };
  /** once we start avoiding someone, prediction will no longer indicate a collision */
  FindArrayKey<AvoidItem> _avoid;
  
  float _avoidSpeed; //!< If we decide to brake, we will brake for some time
  Time _avoidSpeedTime; //!< When we decided to brake
  float _avoidAside; //< Obstacle avoidance offset (current)
  float _avoidAsideWanted; //< Obstacle avoidance offset (target value)
  float _limitSpeed; //!< Speed limit (used to slow down formation leader)
  float _limitSpeedForced; //!< Scripted speed limit (used to slow down formation leader); _limitSpeed is defined by formation
  //@}

  OLinkPermO(EntityAI) _lastDammage; //!< Last entity causing damage to this
  Time _lastDammageTime; //!< Time of last damage caused by crash

  SensorColID _sensorColID; //!< Column is SensorList matrix

  int _targetCategory;
  //! delayed destruction recognition
  mutable Time _whenDestroyed;

  /// Last time when simple path was successfully found
  /**
  Used in order to avoid repeating simple path test too often

  This is how simple path and regular path work together:
  when simple path is found, planning mode is set to DoNotPlanFormation (this also clears the path)
  and _lastSimplePath, _simplePathFrom and _simplePathDst are set
  
  When simple path cannot be used, planning mode is set to FormationPlanned,
  search state and time is tracked in unit->_path
  */
  Time _lastSimplePath;

  /// position the simple path was planned to
  Vector3 _simplePathFrom;
  /// position the simple path was planned to
  Vector3 _simplePathDst;

  //! Each item of the array contains group of aggregated lights
  ObjectLights _lights;

  //! Flag (support for Capture the flag)
  Ref<Flag> _flag;

  //! Squad texture
  Ref<Texture> _squadTexture;

  //! Textures on hidden selections
  RefArray<Texture> _hiddenSelectionsTextures;
  //! Materials on hidden selections
  RefArray<TexMaterial> _hiddenSelectionsMaterials;

  //@{
  /*!
  \name user defined actions
  \patch 1.01 Date 06/15/2001 by Jirka
  - Added: user (designer) defined actions
  */
  //! List of all user defined actions
  AutoArray<UserActionDescription> _userActions;
  //! Id for next action generated by this entity
  int _nextUserActionId;
  //! all event handlers for given event
  EventHandlers _eventHandlers[NEntityEvent];
  //@}

  RefArray<LightPointOnVehicle> _markers;
  RefArray<LightPointOnVehicle> _markersBlink;
  RefArray<LightPointOnVehicle> _nvgMarkers; // markers only visible in nvg
  RefArray<LightPointOnVehicle> _nvgMarkersBlink;
  InitVal<bool,false> _lightsAttached;
  Time _markersOn;
  float _timeElapsed;
  bool _nvBlink;

  /// attached space of variables
  GameVarSpace _vars;

  /// snapped position is used to have hysteresis in target assignment
  Vector3 _snappedPosition;


  AutoArray<SynchronizedVariableInfo> _synchronizedVariables;

public:
  // constructor
  EntityAI(const EntityAIType *type, bool fullCreate = true);
  // destructor
  ~EntityAI();

  /// access to attached space of variables
  virtual GameVarSpace *GetVars() override {return &_vars;}
  virtual const GameVarSpace * GetVars() const override {return &_vars;}
  virtual void SetVariable( RString name, const GameValue &value) override;

  //! Get name of object in script environment
  RString GetVarName() const {return _varName;}
  //! Remember name of object in script environment
  void SetVarName(RString name) {_varName = name;}

  void JSONSaveVariables(JSONValue &container, JSONAllocator &allocator) const;
  bool JSONLoadVariables(JSONValue &container);

  const Array<SynchronizedVariableInfo> &GetSynchronizedVariables() const;
  const SynchronizedVariableInfo *GetSynchronizedVariable(RString name ) const;
  void SynchronizeVariable(RString name, float coef, const GameDataCode &handler);

  /// client received announcement about variable being synchronized
  void OnSynchronizeVariable(RString name, float coef, const GameDataCode &handler);
  
  virtual bool ChildUpdateByParent(const Entity *obj) const override;

  virtual void SetLocal(bool local=true);

  //! access to _isDead flag
  bool IsDeadSet() const {return _isDead;}

  //! Enable drawing flag
  void ShowFlag(bool showFlag=true) {_showFlag = showFlag;}

  //! Set target position unit is moving to
  void GoToStrategic(Vector3Par pos);
  //! Set if unit should stop when target position is reached
  /*! Used to brake before reaching.*/
  virtual bool StopAtStrategicPos() const {return true;}

  //! check if damaging object is enabled
  bool GetAllowDammage() const
#if ENABLE_ALLOW_DAMAGE
  {return _allowDamage;}
#else
  {return true;}
#endif
  //! Allow damaging object
  //!\todo Remove this function. Note demo mission ending will be unplayable then.
  void SetAllowDammage(bool val)
#if ENABLE_ALLOW_DAMAGE
  {_allowDamage=val;}
#else
  {}
#endif

  //! Notify entity formation has been changed
  void FormationChanged() {_inFormation = false;}

  //! Set speed limit (used to limit speed of formation leader).
  void LimitSpeed(float speed) {_limitSpeed = speed;}
  float GetLimitSpeed() const {return _limitSpeed;}
  //! Scripted Set speed limit (used to limit speed of formation leader), _limitSpeed is used by formation
  void LimitSpeedForced(float speed) {_limitSpeedForced = speed;}
  float GetLimitSpeedForced() const {return _limitSpeedForced;}

  //! Check how long must unit fire on single target,
  //! before enabling switching to another.
  virtual float FireValidTime() const {return 15.0f;}

  void SetSensorColID(SensorColID sensorColID) {_sensorColID = sensorColID;}
  SensorColID GetSensorColID() const {return _sensorColID;}

  int GetTargetCategory() const {return _targetCategory;}
  void SetTargetCategory(int val) { _targetCategory = val; }
  void SetTargetCategory(const char *name);

  /// returns true for animals (class Man with _isMan == false)
  virtual bool IsAnimal() const {return false;}

  //! Check if HUD should be shown
  //! \deprecated Obsolete function -  never used. Use config instead.
  virtual bool HasHUD() const {return false;}
  //! Check if weapons are enabled (especially firing)
  virtual bool DisableWeapons() const {return false;}

  /// check if the unit can fight - this makes it more important
  // only EntityAIFull can have weapons
  virtual bool HasSomeWeapons() const {return false;}

  /// check if the vehicle has GPS minimap enabled
  bool GetEnableGPS() const {return GetType()->_enableGPS;}
  /// check if the vehicle has a watch device
  bool GetEnableWatch() const {return GetType()->_enableWatch;}
  /// check if the vehicle has a radio device
  bool GetEnableRadio() const {return GetType()->_enableRadio;}

  //@{ Actions
  //! Get parameters of action as structured texts
  virtual bool GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const override;
  //! Perform action
  virtual void PerformAction(const Action *action, AIBrain *unit) override;
  //! Get list of available actions
  virtual void GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit) override;
  //! Add user type actions
  void GetUserActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit, bool inHands);

#if _HELISIM_PRECISE_INTERACTION
  bool CheckDrawActions3D(const UIAction *action, AIBrain *unit, CameraType cameraType);
  void DrawActionsAnimatePosition(UIActions *actions,CameraType cameraType,const Camera *camera, bool isUnitIn);
  //! Get list of available actions in player surrounding
  virtual void GetDrawActions3D( UIActions &actions, AIBrain *unit);
  virtual void GetDrawGetInActions3D( UIActions &actions, AIBrain *unit) {};
#endif

  //! Process user action (ATUserType)
  void ProcessUserAction(RString name);

  //! Check if user action (ATUserType) is done
  bool DoneUserAction(RString name);

  //! Check if some action is being processed (used for actions connected to animation); default is to process all action immediately
  virtual bool CheckActionProcessing(UIActionType action, AIBrain *unit) const {return false;}
  //! Start processing action (start playing animation if necessary)
  virtual void StartActionProcessing(Action *action, AIBrain *unit);

  //! Add new user action
#if _HELISIM_PRECISE_INTERACTION
  int AddUserAction(RString text, RString script, GameValuePar param, float priority, bool showWindow, bool hideOnUse, UserAction shortcut, RString condition,
    RString modelPosition, float radius, float radiusView, int showIn3D, int available, RString textDefault, RString textToolTip);
  void SetUserActionText(int index, RString text, RString textDefault, RString textToolTip);
#else
  int AddUserAction(RString text, RString script, GameValuePar param, float priority, bool showWindow, bool hideOnUse, UserAction shortcut, RString condition);
  void SetUserActionText(int index, RString text);
#endif
  

  //! Remove user action
  void RemoveUserAction(int id);
  //! Find user action
  const UserActionDescription *FindUserAction(int id) const;
  //@}

  //@{ Event handlers
  //! add event handler (return handle
  int AddEventHandler(EntityEvent event, RString expression)
  {
    return _eventHandlers[event].Add(expression);
  }
  //! remove given event handler
  void RemoveEventHandler(EntityEvent event, int handle)
  {
    _eventHandlers[event].Delete(handle);
  }

  //! set list of event handlers
  void SetEventHandlers(EntityEvent event, const EventHandlers &handlers) 
  { 
    _eventHandlers[event] = handlers; 
  }

  //! get list of event handlers
  const EventHandlers &GetEventHandlers(EntityEvent event) const
  {
    return _eventHandlers[event];
  }
  //! remove all event handlers
  void ClearEventHandlers(EntityEvent event)
  {
    _eventHandlers[event].Clear();
  }

  void DoVehicleInit(RString init);
  void OnVehicleInit(RString init);
  //! generic event handler with typical parameter sets
  GameValue OnEventRet(EntityEvent event, const GameValue &pars);
  //! generic event handler with typical parameter sets
  void DoEvent(EntityEvent event, const GameValue &pars);
  //! event recording for postpone evaluation
  void OnEvent(EntityEvent event, const GameValue &pars);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, EntityAI *par1);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, EntityAI *par1, EntityAI *par2);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, EntityAI *par1, float par2);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, bool par1);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, float par1);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, RString par1);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, RString par1, EntityAI *par2);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, RString par1, float par2);
  //! generic event handler tailored first for suppression
  void OnEvent(EntityEvent event, EntityAI *ea2, RString name, float dist, Vector3 where);
  //! generic event handler with typical parameter sets
  void OnEvent(EntityEvent event, EntityAI *ea2, float par1, float par2);
  //! generic event handler with no parameters
  void OnEvent(EntityEvent event);
  //! event handler for anim-hooks
  void OnEvent(EntityEvent event, int userType, RString par2);

  template <class Ret, class InputTuple>
  Ret OnEventAnyRet(EntityEvent event, const InputTuple &input);

  template <class InputTuple>
  void OnEventAny(EntityEvent event, const InputTuple &input);

  void OnHitPartEvent(EntityEvent event,AutoArray<HitEventElement> &data);

  void OnHitPart(const CollisionInfo &info, const AmmoType *shot, EntityAI *parent);

  //! check if there is some event handler
  bool IsEventHandler(EntityEvent event) const;
  //@}

  //! Check if entity is placed on road (works for both AI and manual) 
  int IsOnRoad() const;
  //! Check if entity is placed on road and is moving
  bool IsOnRoadMoving(float minSpeed=3.0f) const;

  virtual void OnAddImpulse(Vector3Par force, Vector3Par torque) {}

  //! Get main direction entity eyes are watching
  virtual Vector3 GetEyeDirection(ObjectVisualState const& vs, const TurretContext &context) const {
    return vs.Direction();
  }

  //! Get main direction entity is watching
  virtual Vector3 GetLookAroundDirection(ObjectVisualState const& vs, const TurretContext &context) const {
    return vs.Direction();
  }

  //! Check what is this entity firing at
  virtual TargetType *GetFiredAt() const {return NULL;}

  /// Time of last shot fired
  virtual Time GetLastShotTime() const;

  //! Diagnostic only: get speed from planned path.
  virtual float PilotSpeed() const;

  //@{ Spotability related functions
  //! Check how much are lights of this entity visible
  virtual float VisibleLights() const {return 0.0f;}
  //! Check how much is movement of this entity visible
  virtual float VisibleMovement() const;
  /// check how much is movement visible, do not consider size
  virtual float VisibleMovementNoSize() const;
  //! Check how much is sound of this entity audible
  virtual float Audible() const;
  //! Check how much entity hidden by surroundings
  /**
  @return 0 means completely hiden, 1 means full sight
  */
  virtual float GetHidden(float dist) const {return 1.0f;}

  //! Check how much entity lit by light sources
  /**
  @return relative night brigtness 1 means fully recognizable, as in daylight
  */
  virtual float GetLit() const {return 0.0f;}
  
  //! Check how much is firing weapons from this entity visible
  virtual float VisibleFire() const {return 0.0f;}
  //! Check how much is firing weapons from this entity audible
  virtual float AudibleFire() const {return 0.0f;}

  /// precision required when aiming / firing at the target
  virtual float AimingSize() const {return VisibleSize(FutureVisualState()) * 0.25f;}

  /// check distance metrics from given sensor info
  virtual float DistanceFrom(const SensorPos &pos) const
  {
    return pos.pos.Distance(FutureVisualState().Position());
  }
  /// store sensor info
  virtual void GetSensorInfo(SensorPos &pos) const
  {
    pos.pos = FutureVisualState().Position();
  }
  //@}

  /// who killed this unit - for most purposes it is the one causing the last damage
  EntityAI *GetKiller() const {return _lastDammage;}

  /// get list of HUD elements which should be visible, list can be masked using bitmask
  virtual const char **HUDMode(int &mask) const
  {
    mask = 0;
    return NULL;
  }
  /// rpm is used for HUD - mostly for Transport, but can the interface needs to be here
  virtual float GetRPM() const {return 0.0f;}
  virtual float GetRenderVisualStateRPM() const {return 0.0f;}

  virtual RString HitpointName(int i) const;

  virtual float DirectLocalHit(DoDamageResult &result, int component, float val);
  virtual float LocalHit(DoDamageResult &result, Vector3Par pos, float val, float valRange);
  virtual void ApplyDoDamage(const DoDamageResult &result, EntityAI *owner, RString ammo, const SkinnedPosition &pos) override;

  /// helper for special case of LocalDamage - shot = NULL, owner = this
  /** no accumulation */
  void LocalDamageMyself(Vector3Par modelPos, float val, float valRange);
  virtual float HandleDamage(int part, float val, EntityAI *owner, RString ammo, const SkinnedPosition &pos, bool &handlerCalled) override;
  virtual void ShowDamage(int part, EntityAI *killer);

  virtual void HitBy(EntityAI *owner, float howMuch, RString ammo, bool wasDestroyed);
  virtual void Destroy(EntityAI *killer, float overkill, float minExp, float maxExp);
  //! check if entity is complete nonoperational due to damage received
  virtual bool IsDamageDestroyed() const {return _isDead || base::IsDamageDestroyed();}
  //! check how long IsDamageDestroyed is true
  virtual Time GetDestroyedTime() const;

  //! allows AIS to take over healing
  bool HandleHeal(EntityAI *medic, bool attendant, bool &handlerCalled);

  virtual float GetExplosives() const; // how much explosives is in

  virtual void OnNewTarget(Target * target, Time knownSince);

  virtual void SetTotalDamageHandleDead(float damage, bool setHitpoints) override;

  //! Check if can fire (if not damaged to much)
  bool CanFire() const {return !_isDead && !_isUpsideDown;}

  virtual bool LockPossible(const AmmoType *ammo) const;

  //! Check if can move (if not damaged to much)
  virtual bool IsAbleToMove() const {return CanFire();}
  //! Check if can move (if not damaged to much or out of ammo)
  virtual bool IsAbleToFire(const TurretContext &context) const {return CanFire();}

  virtual bool IsAbleToFireAnyTurret() const {return CanFire();}
  //! Check if can be moving on the road and lights can be on
  virtual bool IsCautious() const;

  virtual bool IsCrewed() const;
  //! Same as IsCautious, but react not only to combat mode,
  //! but also to Danger state
  bool IsCautiousOrDanger() const;

  //! Get entity cost (from config)
  virtual float CalculateTotalCost() const {return GetType()->GetCost();}

  /// OperMap needs direct access to the locker
  const AILocker *GetLocker() const {return _locker;}

  //! Unlock fields in locker map that have been locked
  void PerformUnlock(AILockerSavedState *state=NULL) const;
  /// re-lock after PerformUnlock - vehicle must not move in between
  void PerformRelock(AILockerSavedState &state) const;
  /// check if position is locked (LockPosition was called )
  bool IsLockedPosition() const {return _locked;}
  //! When vehicle is stopped or moving slow, it calls this function to maintain corresponding locker map fields locked as necessary
  void LockPosition();
  //! When vehicle moving significantly it calls this function to stop maintaining locker map fields locked
  void UnlockPosition();

  //! Perform force feedback effects on FF input device
  virtual void PerformFF(FFEffects &effects);
  //! Cancel any outstanding force feedback effects on FF input device
  virtual void ResetFF();

  //! Avoid collision with other EntityAI moving near
  void AvoidCollision(float deltaT, float &speedWanted, float &headChange);
  //! Guarantee currently planned path is a fresh one (max. 5 sec old allowed)
  void CreateFreshPlan();

  /// Get steering point (point on planned path in near future vehicle would be steering to)
  virtual Vector3 SteerPoint(float spdTime, float costTime, Ref<const PathAction> *action=NULL, OLink(Object) *building=NULL, Clearance *clearance=NULL, bool updateCost=false);

  /// check what is current distance from nearest point on the planned path (in XZ plane only)
  float DistanceXZFromPath() const;
  /// side offset to the path (used on roads for better avoidance)
  float DistanceXFromPath() const;

  //! Get picture to show in group list UI
  Texture *GetPicture() const {return GetType()->_picture;}
  //! Get picture to show in command bar
  Texture *GetPortrait() const {return GetType()->_portrait;}
  //! Get icon to draw in mission editor / map
  Texture *GetIcon() const {return GetType()->_icon;}

  virtual Vector3 ExternalCameraPosition(CameraType camType) const;

  virtual void LimitVirtual(CameraType camType, float &heading, float &dive, float &fov) const;
  virtual void InitVirtual(CameraType camType, float &heading, float &dive, float &fov) const;

  //! Get diagnostics text (some info about entity state - especially path)
  virtual RString DiagText() const;

  RString RemoteDiagText() const;

  virtual Matrix4 GetProxyTransform(int level, int i, Matrix4Par proxyPos, Matrix4Val parent) const;
  //! Get matrix that should be used to draw given proxy
  Matrix4 AnimateProxyMatrix(ObjectVisualState const& vs, int level, Matrix4 proxyTransform, SkeletonIndex boneIndex) const;
  virtual int GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const;
  virtual bool NeedsPrepareProxiesForDrawing() const
  {
    return (_flag && _showFlag) || base::NeedsPrepareProxiesForDrawing();
  }
  virtual void PrepareProxiesForDrawing(Object *rootObj, int rootLevel, const AnimationContext &animContext,
    const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so);

  void AdjustFlagTransform(Matrix4 &pTransform);
  /// some vehicles may force vertical flag depending on their state (dead soldiers)
  virtual float ForceVerticalFlag() const {return 0.0f;}

  bool PreloadProxies(int level, float dist2, bool preloadTextures=false) const;

  /// preload what is necessary while switching the view
  virtual bool PreloadView(Person *person, CameraType camType) const;

  //! Get size for main mouse cursor
  virtual float GetCursorSize(Person *person) const {return 1.0f;}
  //! Get texture for main mouse cursor
  virtual const CursorTextureInfo *GetCursorTexture(Person *person) {return NULL;}
  //! Get texture dot cursor (indicating weapon aim)
  virtual const CursorTextureInfo *GetCursorAimTexture(Person *person) {return NULL;}
  //! Get mouse cursor color
  virtual PackedColor GetCursorColor(Person *person) {return PackedColor(0);}
  //! check if cursor should be displayed in locked color
  virtual bool GetCursorLocked(Person *person) const {return false;}

  /// Get model to be drawn as screen overlay (Object::Draw2D is used to draw this model)
  virtual LODShape *GetOpticsModel(const Person *person) const {return NULL;}
  /// check if optics can be drawn
  /** used for 3D optics when GetOpticsModel() returned NULL */
  virtual bool GetEnableOptics(ObjectVisualState const& vs, const Person *person) const {return false;}
  //! Get color of optics overlay (color multiplication is applied)
  virtual PackedColor GetOpticsColor(const Person *person) {return PackedBlack;}
  //! Check if optics must be used
  /*!
  Example of situation when true is returned is when binocular is used.
  Any other weapon that can be used only through optics could do the same.
  */
  virtual bool GetForceOptics(const Person *person, CameraType camType) const {return false;}
  /// some entities can use optics transition effects
  virtual bool HasOpticsTransitionFx(Person *person, CameraType camTypeNew, CameraType camTypeOld) const {return false;}
  /// some entities can use blending effects when switching camera
  // this function is used only when HasOpticsTransitionFx is false
  // by default all camera transitions use fadein/fadeout
  virtual bool HasCameraTransitionFx(Person *person, CameraType camTypeNew, CameraType camTypeOld) const {return true;}

  /// entity can control transition speed - higher value, higher speed
  virtual float CameraTransitionFxSpeed(Person *person, CameraType camTypeNew, CameraType camTypeOld) const {return 1.5f;}

  /// transition callback (used to update zoom during camera transition)
  virtual void OnCameraTransition(Person *person, CameraType camTypeNew, CameraType camTypeOld, float newFactor) const {}

  //! Get texture that is currently used to draw flag
  //! This function is implemented directly in Flag
  virtual Texture *GetFlagTextureInternal() {return NULL;}

  //! Get texture that should be used to draw flag
  virtual Texture *GetFlagTexture() {return NULL;}
  //! Change flag texture
  virtual void SetFlagTexture(RString name) {}

  //! Change texture in hiddenSelections
  void SetObjectTexture(int index, Texture *texture);
  //! Change material in hiddenSelections
  void SetObjectMaterial(int index, TexMaterial *mat);


#if _VBS3
  //! Change texture in hiddenSelections by using the selection name
  void SetObjectTexture(RString selection, Texture *texture);
  //! Resets the texture to the initTexture
  void ResetObjectTexture(int index);
  void ResetObjectTexture(RString selection);
  //! returns the texture assign to the hidden selection
  RString GetHiddenSelectionTexture(int index);
#endif

  /// enumerate all crew
  virtual bool ForEachCrew(ICrewFunc &func) const {return false;}

  //! Simulation
  void Simulate(float deltaT, SimulationImportance prec);

  void UpdateLights();

  /// implementation of JoinedObject
  virtual void UpdatePosition();

  //! Simulation that must be performed after transformation change.
  void SimulatePost(float deltaT, SimulationImportance prec);

  virtual void SimulateWeaponActivity(float deltaT, SimulationImportance prec) {}

  //! clean-up before landscape release (object is preparing to destruction)
  void CleanUp();

  /// clean-up before removed from the landscape
  void CleanUpMoveOut();

  //! Perform sounds
  void Sound(bool inside, float deltaT)
  {
    base::Sound(inside,deltaT);
  }
  //! Unload any sounds currently playing
  void UnloadSound()
  {
    base::UnloadSound();
  }

  virtual bool CanBeInstanced(int level, float distance2, const DrawParameters &dp) const;

  enum ObstructType {Different,SameInside,SameOutside};
  /// simulate occlusion + obstruction for given vehicle, by default we assume no occlusion/obstruction
  virtual void OccludeAndObstructSound(EntityAI *focus, float &occlusion, float &obstruction, ObstructType soundInside) const {}

  // all vehicles are supposed to be animated unless explicitly overridden
  AnimationStyle IsAnimated(int level) const {return AnimatedGeometry;}

  void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  void Deanimate(int level, bool setEngineStuff)
  {
    base::Deanimate(level, setEngineStuff);
  }

  /// Helper function (for Transport::PassNum, Man::PassNum etc.), true when object need to be drawn in pass 3
  bool DrawInPass3() const;

  virtual bool IsActionInProgress(MoveFinishF action) const {return false;}
  virtual bool IsLadderActionInProgress() const {return false;}

  /*!
  \name Config parameters
  Various parameters from config file.
  */
  //@{
  //!<Time to reach pos. in formation
  virtual float GetFormationTime() const {return GetType()->GetFormationTime();}
  //<Inverse GetFormationTime()
  virtual float GetInvFormationTime() const {return GetType()->GetInvFormationTime();}

  virtual float GetSteerAheadSimul() const {return GetType()->GetSteerAheadSimul();}
  virtual float GetSteerAheadPlan() const {return GetType()->GetSteerAheadPlan();}

  float GetMinFireTime() const {return GetType()->GetMinFireTime();}

  float GetPredictTurnSimul() const {return GetType()->GetPredictTurnSimul();}
  float GetPredictTurnPlan() const {return GetType()->GetPredictTurnPlan();}

  //! How is the entity able to be precise when moving
  //! to given target
  virtual float GetPrecision() const {return GetType()->GetPrecision();}
  //@}

  //! Check how much is this vehicle afraid of collision
  virtual float AfraidOfCollision(VehicleKind with) const;

  //! Check height the center of the vehicle is normally moving in
  /*!
  For most entities this is very low (0-5 m), as land vehicles or men move on the ground.
  This is significant for aerial vehicles.
  */
  virtual float GetCombatHeight() const {return -_shape->Min()[1];}
  //! Check min. allowed combat height (see GetCombatHeight)
  virtual float GetMinCombatHeight() const {return -_shape->Min()[1];}
  //! Check max. allowed combat height (see GetCombatHeight)
  virtual float GetMaxCombatHeight() const {return -_shape->Min()[1];}

  virtual float GetMajorAnimationTime() const {return 0.0f;}
  virtual void UseAudioTimeForMoves(bool toggle) {}

  //! Check whether action is available in the current state
  virtual bool IsAction(RStringB action) const {return false;}

  //! Interface to motion capture animations - smooth change
  virtual bool PlayMoveGlobal(RStringB move) {return false;}
  virtual bool PlayMoveGlobal(RStringB move, const GameDataCode &handler) {return false;}
  //! Interface to motion capture animations - smooth change
  virtual bool PlayMoveNowGlobal(RStringB move) {return false;}
  //! Interface to motion capture animations - immediate change
  virtual void SwitchMoveGlobal(RStringB move) {}

  //! Interface to motion capture animations - smooth change
  virtual bool PlayActionGlobal(RString action) {return false;}
  virtual bool PlayActionGlobal(RString move, const GameDataCode &handler) {return false;}
  //! Interface to motion capture animations - smooth change
  virtual bool PlayActionNowGlobal(RString action) {return false;}
  //! Interface to motion capture animations - immediate change
  virtual void SwitchActionGlobal(RString action) {}

  /*!
  \name Hide decision
  */
  //@{
  virtual void SwitchToFormation(){}
  virtual void SwitchToLeader(){}
  //@}

  /// primary gunner turret (see turret.hpp for description)
  virtual bool GetPrimaryGunnerTurret(TurretContext &context) const {return false;}
  /// primary gunner turret (see turret.hpp for description)
  virtual bool GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextV &context) const {return false;}
  /// primary gunner turret (see turret.hpp for description)
  virtual bool GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextEx &context) const {return false;}
  /// primary observer turret (see turret.hpp for description)
  virtual bool GetPrimaryObserverTurret(TurretContext &context) const {return false;}
  /// primary observer turret (see turret.hpp for description)
  virtual bool GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextV &context) const {return false;}
  /// primary observer turret (see turret.hpp for description)
  virtual bool GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextEx &context) const {return false;}

  virtual void Refuel(float ammount) {}

  virtual float GetFuel() const {return 0.0f;}
  virtual float GetRenderVisualStateFuel() const {return 0.0f;}
  // used for explosion calculation
  virtual float GetFuelTotal() const {return GetFuel() + GetFuelCargo();}

  //!\name Implementation of Object interface
  //@{
  virtual float GetArmor() const {return GetType()->_armor;} // armor in mm
  virtual float GetInvArmor() const {return GetType()->_invArmor;} // armor in mm
  virtual float GetLogArmor() const {return GetType()->_logArmor;} // armor in mm
  //@}

  //! Easy access to commander properties (see AIUnit::GetAbility)
  float GetAbility(AbilityKind kind) const; // returns from 1 (maximal) to 0.2 (unable)
  //! Easy access to commander properties (see AIUnit::GetInvAbility)
  float GetInvAbility(AbilityKind kind) const; // returns from 5 (unable) to 1 (maximal)

  //! include correction based on difficulty settings
  float GetAbility(AIBrain *unit, AbilityKind kind) const; // returns from 1 (maximal) to 0.2 (unable)
  //! include correction based on difficulty settings
  float GetInvAbility(AIBrain *unit, AbilityKind kind) const; // returns from 5 (unable) to 1 (maximal)


  __forceinline TargetSide GetVehicleTargetSide() const {return Entity::GetTargetSide();}

  __forceinline const EntityAIType *GetType() const
  {
    return static_cast<const EntityAIType *>(_type.GetRef());
  }
  virtual TargetSide GetTargetSide() const;
  /// check target side, do not return Civilian for dead vehicles
  virtual TargetSide GetTargetSideAsIfAlive() const;

  __forceinline RString GetDisplayName() const {return GetType()->GetDisplayName();}
  __forceinline RString GetShortName() const {return GetType()->GetShortName();}
  __forceinline RString GetNameSound() const {return GetType()->GetNameSound();}

  // no vehicles with AI may be move targets
  bool IsMoveTarget() const {return false;}

  virtual bool SeenByPeriperalVision() const {return true;}

  const EntityAIType *GetTypeAtLeast(float accuracy) const;
  const EntityAIType *GetType(float accuracy) const;
  TargetSide GetTargetSide(float accuracy) const;
  virtual bool IsRenegade() const;

  RString GetDebugName() const;
  //needed to make ObjSetPos working with tanks (forcing UseSimpleSimulation to return false)
  void SetLandContact(bool val) {_landContact = val;}

  // getting in/out of vehicles
  // how long vehicle must be without movement before stopped
  //! Allow simulation of the object
  virtual void OnMoved(); // move condition detected
  void StopDetected(); // stop condition detected
  virtual float TimeToStop() const {return 5.0f;}

  //! Cancel the stop flag
  void CancelStop() {_isStopped = false;}
  //! Stop simulation of the object
  void Stop() {_isStopped = true;}
  //! Retrieve whether object is being simulated or not
  bool GetStopped() const {return _isStopped;}

  virtual bool IsMoving() {return !GetStopped();}

  void UserStop(bool stop) {_userStopped = stop;}
  bool IsUserStopped() const {return _userStopped;}

  virtual bool EngineIsOn() const {return true;}
  /// radar IR signature active?
  virtual bool IRSignatureOn() const {return EngineIsOn();}
  virtual void EngineOn() {}
  virtual float MakeAirborne(bool playerIn) {return 0.0f;}
  virtual float MakeAirborne(Vector3Par dir, bool playerIn) { return MakeAirborne(playerIn); }
  virtual void EngineOff() {}

  virtual void SetFlyingHeight(float val) {}
  /// initiate landing at given airport (-1 performs auto-selection)
  virtual void LandAtAirport(int index) {}
  /// set airport to land on get out (-1 cancels the selection)
  virtual void AssignToAirport(int index) {}
  /// cancel landing initiated by LandAtAirport
  virtual void CancelLanding() {}
  /// tell the entity it is not on surface and it should adjust its rendering accordingly
  virtual void SetNotOnSurface(bool notOnSurface){}

  virtual void EngineOnAction() {EngineOn();}
  virtual void EngineOffAction() {EngineOff();}

  virtual void Init(Matrix4Par pos, bool init);
  virtual void InitPositionHistory(Matrix4Par pos) {}
  virtual void OnInitEvent() {OnEvent(EEInit);}
  // called after vehicle is placed in landscape, units are set-up and initialized
  virtual void InitUnits() {}

  virtual bool IsPilotLight() const {return _pilotLight;}
  virtual bool IsCollisionLight() const {return _collisionLight;}
  virtual bool IsLightOn(ObjectVisualState const& vs) const {return IsPilotLight();}
  virtual bool IsCollisionLightOn(ObjectVisualState const& vs) const {return IsCollisionLight();}
  void SetPilotLight(bool on) {_pilotLight = on;}
  void SetCollisionLight(bool on) {_collisionLight = on;}
#if _HELISIM_ROTORLIB
  virtual bool IsElectricity () const {return true;}
#endif

  void SwitchLight(bool on);

  void AttachLights();

#if _VBS3 //only used for soldiers
  void SetLightMode(int lm) {_lightMode = lm;}
  int GetLightMode() {return _lightMode;}
#endif

  virtual bool QIsManual() const = 0;
  /// check if movement is controlled by a player
  virtual bool QIsManualMovement() const {return QIsManual();}

  /// check if crew can be attacked with small arms
  // by default assume there is no crew or it is nor vulnerable to small arms fire
  virtual bool CheckCrewVulnerable() const {return false;}

  virtual float NeedsAmbulance() const; // support need (0..1)
  virtual float NeedsRepair() const;
  virtual float NeedsRefuel() const;
  virtual float NeedsRearm() const {return 0.0f;}
  //! check if can walk
  virtual bool IsAbleToStand() const;

  // cargo filling need (0..1) (from static only?)
  virtual float NeedsLoadFuel() const {return 0.0f;}
  virtual float NeedsLoadAmmo() const {return 0.0f;}
  virtual float NeedsLoadRepair() const {return 0.0f;}

  virtual AIBrain *PilotUnit() const {return NULL;}
  virtual AIBrain *GunnerUnit() const {return NULL;}
  virtual AIBrain *ObserverUnit() const {return NULL;}
  /// commander is responsible for commanding a driver
  virtual AIBrain *CommanderUnit() const {return NULL;}
  /// effective observer is the one responsible for commanding gunners
  virtual AIBrain *EffectiveObserverUnit() const {return CommanderUnit();}
  virtual AIBrain *EffectiveGunnerUnit(const TurretContext &context) const;
  virtual AIBrain *CopilotUnit() const {return NULL;}

  virtual bool MustBeSaved() const;
  virtual LSError Serialize(ParamArchive &ar);

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
  TMError TransferMsg(NetworkMessageContext &ctx) override;
  bool OnSendCreateMessage(NetworkMessageContext &ctx) override;
  virtual bool RejectMsg(NetworkMessageClass cls, NetworkMessage *msg) override;

  float CalculateError(NetworkMessageContextWithError &ctx);
  virtual float CalculateErrorInitial(NetworkMessageClass cls);
  static float CalculateErrorInitialDamage(NetworkMessage *msg);
  virtual GameValue GetVariableValue( const RString &name ) const override;

  virtual void CreateSubobjects() override;
  virtual void UpdateSubobjects() override;

  virtual void ResetStatus();

  const ResourceSupply *GetSupply() const {return _supply;}
  ResourceSupply *GetSupply() {return _supply;}

  virtual bool IsCombatUnit() const {return false;}

  //@{
  /*!\name ResourceSupply related interface and function */
  bool HasSupply() const {return _supply != NULL;}

  void SupplyStarted(AIBrain *unit);
  void SupplyFinished(AIBrain *unit);
  void WaitForSupply(AIBrain *unit);

  const SupplyUnitList &GetSupplyUnits() const;
  SupplyUnitList &GetSupplyUnits();

  virtual bool RemoveOwnedItem(InventoryItem *item) override;
  virtual bool AddOwnedItem(InventoryItem *item) override;
  virtual bool CanAddOwnedItem(const InventoryItem *item) const override;
  virtual int CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const override;
  // traverse all items owned by the entity (excluding subitems of these items)
  virtual bool ForEachOwnedItem(const std::function<bool(InventoryItem *)> &func);

  // Inventory UI support
  virtual bool CanMoveItem(int index, const InventoryItem *item) const;
  virtual void MoveItem(int index, InventoryItem *item);

  void UpdateStop(); // something has been changed
  virtual bool CanCancelStop() const;

  void SetAllocSupply( EntityAIFull *vehicle );
  EntityAIFull *GetAllocSupply() const;
  //use when action is performed on supply unit
  virtual bool Supply(EntityAIFull *vehicle, const Action *action);
  //use when supply unit starts action
  virtual bool SupplyInverse(EntityAIFull *vehicle, const Action *action);
  void CancelSupply();
  EntityAIFull *GetSupplying() const;

  // direct mapping to SUPPLY functions
  // here is the interface only. To use it vehSupply must be included
  float GetFuelCargo() const;
  float GetRepairCargo() const;
  float GetAmmoCargo() const;

  bool ForEachItemCargo(const std::function<bool(InventoryItem *item)> &func, bool recursive = true);
  bool ForEachItemCargo(const std::function<bool(const InventoryItem *item)> &func, bool recursive = true) const;
  bool ForEachWeaponCargo(const std::function<bool(const Weapon *weapon)> &func) const;
  bool ForEachMagazineCargo(const std::function<bool(const Magazine *magazine)> &func) const;
  bool FindItemCargo(const InventoryItem *item) const;

  void LoadFuelCargo(float cargo);
  void LoadRepairCargo(float cargo);
  void LoadAmmoCargo(float cargo);

  void DeleteIfNotNeeded();
  void LockResources(AIBrain *user);
  void UnlockResources(AIBrain *user);
  bool AreResourcesLocked() const;

  void ClearWeaponCargo( bool localOnly);
  bool AddWeaponCargo(const WeaponType *weapon, int count);

  void ClearMagazineCargo(bool localOnly);
  bool AddMagazineCargo(Magazine *magazine, bool localOnly);
  bool AddMagazineCargoCount(const MagazineType *type, int count);
  //@}

  float GetBaseCost(const GeographyInfo &info, bool operative, bool includeGradient) const
  {
    return GetType()->GetBaseCost(info, operative, includeGradient);
  }
  float GetGradientPenalty(float gradient) const
  {
    return GetType()->GetGradientPenalty(gradient);
  }
  virtual float GetFieldCost(const GeographyInfo &info) const;
  // cost based on buildings height, used for airplanes and helicopters
  virtual float GetAirCost(float x, float z) const;

  Vector3 SnappedPosition() const { return _snappedPosition; }
  void UpdateSnappedPosition();

  INHERIT_SOFTLINK(EntityAI,base);

  USE_CASTING(base)

  static void ClassCleanUp();
};

struct ValueWithCurve;

/// vehicle/body steering info
/**
result of AI Pilot support functions, like LeaderPilot ...
*/
struct SteerInfo
{
  /// desired speed
  float speedWanted;
  /// desired strafing speed
  float sideSpeedWanted;
  /// desired heading change
  float headChange;
  /// turning not needed yet, but predicted to be necessary soon
  float turnPredict;
  /// action needed instead of movement
  Ref<const PathAction> action;
  /// clearance along the path ahead
  Clearance clearance;
  /// clearance predicted further along the path ahead
  Clearance clearancePredict;
  /// building we are moving in
  OLink(Object) building;
  /// sometimes the AI pilot needs to prevent turning based on fire sectors (e.g. in cover)
  bool allowFreeTurn;
  /// now only for animals
  bool slowDownWhenTurning;

  SteerInfo()
  : sideSpeedWanted(0),allowFreeTurn(true),clearance(ClearanceNormal),clearancePredict(ClearanceNormal)

  {}
};

struct RemoteFireWeaponInfo
{
  Vector3 _position; // position in world coordinates
  Vector3 _direction; // direction in world coordinates
  float _visible; // visual size

#if _VBS3
  OLink(Entity) _projectile;
#endif
};

class Turret;
class TurretVisualState;

/// Basic information about weapons state
struct TurretContext
{
  /// turret instance
  Turret *_turret;
  /// turret type
  const TurretType *_turretType;
  /// person controlling the turret
  Person *_gunner;
  /// weapons of the turret
  WeaponsState *_weapons;
};

/// Information about weapons state (corresponding to the visual state)
struct TurretContextV : public TurretContext
{
  /// turret visual state
  TurretVisualState *_turretVisualState;
};

/// Extended information about weapons state
struct TurretContextEx : public TurretContextV
{
  /// orientation (in the world space) of the turret container
  Matrix3 _parentTrans;
};


/*
/// Advanced turret contexts sans inheritance (for debugging)
struct TurretContextV
{
  Turret *_turret;
  const TurretType *_turretType;
  Person *_gunner;
  WeaponsState *_weapons;
  TurretVisualState *_turretVisualState;
};
struct TurretContextEx
{
  Turret *_turret;
  const TurretType *_turretType;
  Person *_gunner;
  WeaponsState *_weapons;
  TurretVisualState *_turretVisualState;
  Matrix3 _parentTrans;
};
*/

/// interface for functor passed to Transport::ForEachTurretVisualState
class ITurretFuncV
{
public:
  virtual bool operator () (EntityAIFull *entity, TurretContextV &context) = 0;
};

/// interface for functor passed to Transport::ForEachTurretEx
class ITurretFuncEx
{
public:
  virtual bool operator () (EntityAIFull *entity, TurretContextEx &context) = 0;
};

class SpeedEstimation
{
  Vector3 lastPos;
  dword timeStamp;
  float speed;
public:
  SpeedEstimation() : lastPos(VZero), timeStamp(0), speed(0) {}
  void Update(Vector3Par pos);
  float Speed() {return speed;}
};


typedef OLink(EntityAI) LinkVehicleWithAI;
typedef OLink(EntityAI) LinkEntityAI;

struct SideInfo;

enum Rank
{
  RankUndefined = -1,
  RankPrivate,
  RankCorporal,
  RankSergeant,
  RankLieutenant,
  RankCaptain,
  RankMajor,
  RankColonel,
  NRanks
};

#include <Es/Memory/normalNew.hpp>

//!Target status (position, spotability etc.)
class Target: public RemoveLLinks, private NoCopy
{
public:
  TargetId idExact;

  // functions
  virtual const EntityAIType *GetType() const = 0;
  virtual Vector3 GetSpeed(const AIBrain *who) const = 0;


  /// IsVanished or IsDestroyed, avoiding two virtual calls
  virtual bool IsVanishedOrDestroyed() const = 0;
  virtual bool IsDestroyed() const = 0;
  virtual bool IsVanished() const = 0;
  virtual bool HasDiclosedUs() const = 0;
  virtual TargetSide GetSide() const = 0;
  virtual bool GetSideChecked() const = 0;
  virtual Time GetLastSeen() const = 0;
  virtual float Distance(Vector3Par pos) const = 0;
  /// get perceived position of the target
  virtual Vector3 GetPosition() const = 0;
  /// get perceived direction of the target
  virtual Vector3 GetDirection() const = 0;
  virtual float GetSubjectiveCost() const = 0;
  virtual bool IsKindOf(const EntityType *supertype) const = 0;
  /// used in MP to make sure target is position is accurate even for remote gunner
  virtual void UpdateRemotePosition(EntityAI *ent) = 0;

  virtual AIBrain *SeenBy() const = 0;
  virtual void MarkKiller(EntityAI *by) = 0;
  /// mark target as suppressed when we know somebody was firing at it
  virtual void OnSuppressed(const AmmoType *ammo) {}

  /// limit when side accuracy starts giving exact answer
  static const float GetSideAccuracyExact() {return 1.5f;}
  /// assume visual error of plain eye is about 10 cm per 100 m
  static const float GetVisualError() {return 0.10f/100;}
  
  virtual bool NeedsZoom(float dist) const {return false;}

  virtual float FadingSideAccuracy() const = 0;
  virtual float FadingAccuracy() const = 0;
  virtual float FadingSpotability() const = 0;
  virtual float FadingPosAccuracy() const = 0;

  virtual void UpdatePosition(Vector3Par pos, float typeAccuracy, float posAccuracy) = 0;

  /// check if known by a given unit
  virtual bool IsKnownBy(const AIBrain *unit) const = 0;
  /// check if known by all group members
  virtual bool IsKnownByAll() const = 0;
  /// check if known by some group member
  virtual bool IsKnownBySome() const = 0;
  /// set target as known
  virtual void Reveal(const SideInfo &side, TargetList *list, float accuracy) = 0;
  /// reveal target to all units in the vehicle
  virtual void RevealInVehicle(Transport *veh) = 0;
  /// check if target info is accurate enough so that we can fire using given ammo
  virtual bool AccurateEnoughForHit(const AmmoType *ammo, AIBrain *brain) const = 0;
  /// check what is the accuracy of the position information, considering age as well
  virtual float PositionAccuracy(const AIBrain *brain) const = 0;
  /// check if target was seen recently (can be aimed upon)
  virtual bool SeenRecently(AIBrain *brain, float maxAge=10.0f) const = 0;
  // TODO: remove IsKnown member - compatibility only
  virtual bool IsKnown() const = 0;

  virtual float VisibleSize() const = 0;
  virtual Vector3 AimingPosition() const = 0;
  /// get aiming position, be smart and know some entities cannot fly
  virtual Vector3 LandAimingPosition() const = 0;
  /// enhanced LandAimingPosition - when knowing who is aiming, close aiming may be improved
  virtual Vector3 LandAimingPosition(const AIBrain *who, const AmmoType *ammo=NULL, bool head=false, bool *isExact=NULL) const = 0;

  /// last known (or assumed) position, with no prediction - used e.g. for suppressive fire
  virtual Vector3 LastKnownPosition(const AIBrain *who, const AmmoType *ammo=NULL, bool head=false, bool *isExact=NULL) const
  {
    return LandAimingPosition(who,ammo,head);
  }

  virtual TargetState State(AIBrain *sensor) const = 0;

  /// during serialization we need to know if the target is minimal or not
  virtual bool IsMinimal() const = 0;

  virtual LSError Serialize(ParamArchive &ar);
  static Target *LoadRef(ParamArchive &ar);
  virtual LSError SaveRef(ParamArchive &ar) = 0;

  //
  static LSError LoadLLinkRef(ParamArchive &ar, LLink<Target> &link)
  {
    link = LoadRef(ar);
    return LSOK;
  }
  static LSError SaveLLinkRef(ParamArchive &ar, LLink<Target> &link)
  {
    return link->SaveRef(ar);
  }

  Target();

  float HowMuchInteresting() const;

  RString GetDebugName() const {return idExact->GetDebugName();}
};


/// target category
enum TgtCategory
{
  /// all enemies (identified, unidentified)
  TgtCatEnemy,
  /// unknown enemies (never seen)
  TgtCatEnemyUnknown,
  /// all friendly, neutral and empty (identified, unidentified and unknown)
  //TgtCatFriendly, //we don't store friendly in TargetList anymore
  NTgtCategory,
};

/// typical target - state is tracked per group
class TargetNormal: public Target, public CountInstances<TargetNormal>
{
  typedef Target base;
  friend class TargetListInAgent;

public:

  /// last known aiming position of the vehicle
  Vector3 position;
  Vector3 snappedPosition;  //aiming position snapped to 5m grid with hysteresis
  /// last known direction of the vehicle
  /** used to estimated enemy fire sectors */
  Vector3 dir;
  /** for consistent simulation of sensor error we create a notion of "error direction"
  and error magnitude. Error magnitude is given by posAccuracy and is changing almost constantly.
  Error direction is not changing often.
  */
  Vector3 posErrorDirection; // audible results are very inaccurate
  Vector3 speed;
  Vector3 posReported; // which position was reported

  /// side info merged from side and type
  TargetSide side;
  /// target category - which list this target should belong to
  TgtCategory category;

  bool sideChecked; // side info obtained from observation
  Ref<const EntityAIType> type;

  // fading values work like this
  // if current value is detected to be better than the stored one,
  // the current value is stored and time is marked
  // when checking the value, it is assumed to be "fading" - and getting worse as it grows older
  // therefore unless refreshed, it will gradually degrade

  /// position error
  float posAccuracy;
  /// position error without considering skill (used to determine when to use binocs)
  float posAccuracyReal;

  /// time when position error was valid
  Time posAccuracyTime;
  float spotability; // fading
  Time spotabilityTime;
  float accuracy; // fading
  float sideAccuracy; // fading
  Time lastSeen;

  /// time when it was last know to be suppressed
  Time _timeSuppressed;
  /// reliability of the suppression at time _timeSuppressed
  float _suppressed;

  /// we should consider all data valid at least until the next check
  float _validTime;

  /// delay for all group members
  /** units are supposed not to know about the target until some delay valid for them expired
  */
  Time delay; 
  /// delay for units in the same vehicle as idSensor
  Time delayVehicle;
  /// delay for the unit which has seen the target (if bigger that delay -> invalid)
  Time delaySensor;

  /// time when idSensor started reporting the target
  Time timeReported;

  //@{ Helper members for Target Aggregation for Radio Report purposes
  /// backup of GetSubjectiveCost() used when target was inserted into TargetList::_aggrTargets
  float aggrGroupCost;
  bool  aggrIsReported; //it has been reported  and is not changed enough to be reported again
  bool  aggrPresent;    //it has been added into aggregated structures and should can be reported to center only from SendRadioReport
  Vector3 aggrPosition; //position it was added/updated inside _aggrTargets
  bool CanSendReport() const {return (aggrIsReported || !aggrPresent);}
  //@}
protected:
  // access only through IsKnownNoDelay()
  bool _isKnown; // do we remember it (potential)

public:
  /// target vanished - probably GetIn
  bool vanished;
  /// target is dead
  bool destroyed;
  /// the group should react to destruction only once
  bool reactedToDestruction;

  /// target has disclosed somebody from our group (is aware about the group)
  bool hasDisclosedUs;

  OLinkPermNO(AIBrain) idSensor; // who sees the target best
  OLinkPermO(EntityAI) idKiller; // who killed this target

  /// target importance (including how dangerous it seems to be)
  float subjectiveCost;

  /// create a random error direction
  void RandomPosError();

  /// limit (make lower f necessary) position error
  bool LimitError(float error, float errorReal, float *oldError=NULL);

  void Init(EntityAI *ai);
  void Init();

  void UpdateSnappedPosition();

  struct TrackContext;

  void SeesItself(AIBrain *unit, const SideInfo &sideInfo, float trackTargetsPeriod);
  void Sees(AIBrain *unit, float dist2, TrackContext &ctx, float trackTargetsPeriod);

  void UpdateSideAccuracy(float sensorSideAccuracy, const SideInfo &ctx, bool forceCheckCategory=false);

  float AdjustMyDelay(AIBrain *unit, float spotability) const;

  /// used when we see the unit being killed, or see it dead
  void SeenKill(EntityAI * killer, TrackContext &ctx, EntityAI *killed, AIBrain *seenKill, AIBrain *killedUnit,
    float basicReactionTime) const;
  TgtCategory CheckCategory(const SideInfo &who);

  TargetNormal();
  ~TargetNormal();

  //@{ Target serialization
  virtual LSError Serialize(ParamArchive &ar);
  static TargetNormal *LoadRef(ParamArchive &ar);

  static LSError LoadRefRef(ParamArchive &ar, Ref<TargetNormal> &link)
  {
    link = LoadRef(ar);
    return LSOK;
  }
  static LSError SaveRefRef(ParamArchive &ar, Ref<TargetNormal> &link)
  {
    return link->SaveRef(ar);
  }
  static LSError LoadLLinkRef(ParamArchive &ar, LLink<TargetNormal> &link)
  {
    link = LoadRef(ar);
    return LSOK;
  }
  static LSError SaveLLinkRef(ParamArchive &ar, LLink<TargetNormal> &link)
  {
    return link->SaveRef(ar);
  }
  //@}

  // comparison operator to QSort targetList
  struct BetterTarget
  {
    int operator () (const Ref<TargetNormal> *v0, const Ref<TargetNormal> *v1) const
    {
      TargetNormal *t0 = *v0;
      TargetNormal *t1 = *v1;
      int retVal = sign(t1->subjectiveCost - t0->subjectiveCost);
      if (!retVal && t0->idExact && t1->idExact)
        return sign(t1->idExact->ID().Encode() - t0->idExact->ID().Encode());
      return retVal;
    }
  };

  //@{ Target implementation
  static TargetNormal *CreateObject(ParamArchive &ar);
  virtual bool IsMinimal() const {return false;}

  virtual const EntityAIType *GetType() const {return type;}
  virtual Vector3 GetSpeed(const AIBrain *who) const;
  virtual bool NeedsZoom(float dist) const;

  virtual bool IsVanishedOrDestroyed() const;
  virtual bool IsDestroyed() const {return destroyed;}
  virtual bool IsVanished() const {return vanished;}
  virtual bool HasDiclosedUs() const {return hasDisclosedUs;}
  virtual TargetSide GetSide() const {return side;}
  virtual bool GetSideChecked() const {return sideChecked;}
  virtual Time GetLastSeen() const {return lastSeen;}
  virtual float Distance(Vector3Par pos) const {return position.Distance(pos);}
  virtual Vector3 GetPosition() const {return position;}
  virtual Vector3 GetDirection() const {return dir;}
  virtual float GetSubjectiveCost() const {return subjectiveCost;}
  virtual bool IsKindOf(const EntityType *supertype) const {return type->IsKindOf(supertype);}
  virtual void UpdateRemotePosition(EntityAI *ent);

  virtual AIBrain *SeenBy() const;
  virtual void MarkKiller(EntityAI *by) {idKiller=by;}
  virtual void OnSuppressed(const AmmoType *ammo);

  virtual float FadingSideAccuracy() const;
  virtual float FadingAccuracy() const;
  virtual float FadingSpotability() const;
  virtual float FadingPosAccuracy() const;

  float FadingSuppressed() const;

  virtual void UpdatePosition(Vector3Par pos, float typeAccuracy, float posAccuracy);

  virtual bool IsKnownBy(const AIBrain *unit) const;
  virtual bool IsKnownByAll() const; // known by all group member
  virtual bool IsKnownBySome() const; // known by all group member
  virtual void Reveal(const SideInfo &side, TargetList *list, float accuracy);
  virtual void RevealInVehicle(Transport *veh);
  void Forget();
  bool IsKnownNoDelay() const; // exact state (without initial delay)

  virtual float PositionAccuracy(const AIBrain *brain) const;
  virtual bool AccurateEnoughForHit(const AmmoType *ammo, AIBrain *brain) const;
  virtual bool SeenRecently(AIBrain *brain, float maxAge=10.0f) const;

  float AccuracyRequiredForHit() const  {return type->GetShape() ? type->GetShape()->GeometrySphere()*0.5f : 0.5f;}

  virtual bool IsKnown() const;

  virtual float VisibleSize() const;
  virtual Vector3 AimingPosition() const;
  virtual Vector3 LandAimingPosition() const;
  virtual Vector3 LandAimingPosition(const AIBrain *who, const AmmoType *ammo=NULL, bool head=false, bool *isExact=NULL) const;
  virtual Vector3 LastKnownPosition(const AIBrain *who, const AmmoType *ammo=NULL, bool head=false, bool *isExact=NULL) const;

  virtual TargetState State(AIBrain *sensor) const;

  /// side information for the owner
  virtual const SideInfo *GetOwnerSideInfo() const = 0;

  //@}

  USE_FAST_ALLOCATOR

protected:
  Vector3 ExactAimingPosition(const AIBrain *who, const AmmoType *ammo, float &moved, bool head, float maxPredict, bool *isExact) const;
};

#if _ENABLE_INDEPENDENT_AGENTS

class AIAgent;

class TargetInAgent : public TargetNormal
{
  typedef TargetNormal base;

protected:
  OLinkPermNO(AIAgent) _agentOwner; /// owner of the target lists

public:
  struct TrackContext;

  TargetInAgent(AIAgent *agent);

  virtual LSError Serialize(ParamArchive &ar);
  virtual LSError SaveRef(ParamArchive &ar);
  virtual const SideInfo *GetOwnerSideInfo() const;

  USE_FAST_ALLOCATOR
};

#endif

/// target stored in long-term knowledge base
class TargetKnowledge : public Target
{
  typedef Target base;

public:
  /// last known aiming position of the vehicle
  Vector3 position;
  /// side info merged from side and type
  TargetSide side;
  Ref<const EntityAIType> type;
  Time lastSeen;

  /// target vanished - probably GetIn
  bool vanished;
  /// target is dead
  bool destroyed;

  void Init();
  void Init(Target *src);

  TargetKnowledge() {Init();}
  TargetKnowledge(Target *src) {Init(src);}

  //@{ Target implementation
  static TargetKnowledge *CreateObject(ParamArchive &ar);
  virtual LSError Serialize(ParamArchive &ar);
  virtual LSError SaveRef(ParamArchive &ar);
  virtual bool IsMinimal() const {return false;}

  virtual const EntityAIType *GetType() const {return type;}
  virtual Vector3 GetSpeed(const AIBrain *who) const {return VZero;}

  virtual bool IsVanishedOrDestroyed() const {return vanished || destroyed;}
  virtual bool IsDestroyed() const {return destroyed;}
  virtual bool IsVanished() const {return vanished;}
  virtual bool HasDiclosedUs() const {return false;}
  virtual TargetSide GetSide() const {return side;}
  virtual bool GetSideChecked() const {return true;}
  virtual Time GetLastSeen() const {return lastSeen;}
  virtual float Distance(Vector3Par pos) const {return position.Distance(pos);}
  virtual Vector3 GetPosition() const {return position;}
  virtual Vector3 GetDirection() const {return VForward;}
  virtual float GetSubjectiveCost() const {return 0.0f;}
  virtual bool IsKindOf(const EntityType *supertype) const {return type->IsKindOf(supertype);}
  virtual void UpdateRemotePosition(EntityAI *ent) {}

  virtual AIBrain *SeenBy() const {return NULL;}
  virtual void MarkKiller(EntityAI *by) {}

  virtual float FadingSideAccuracy() const;
  virtual float FadingAccuracy() const;
  virtual float FadingSpotability() const;
  virtual float FadingPosAccuracy() const;

  virtual void UpdatePosition(Vector3Par pos, float typeAccuracy, float posAccuracy);

  virtual bool IsKnownBy( const AIBrain *unit ) const;
  virtual bool IsKnownByAll() const; // known by all group member
  virtual bool IsKnownBySome() const; // known by all group member
  virtual void Reveal(const SideInfo &side, TargetList *list, float accuracy);
  virtual void RevealInVehicle(Transport *veh);

  virtual float PositionAccuracy(const AIBrain *brain) const;
  virtual bool AccurateEnoughForHit(const AmmoType *ammo, AIBrain *brain) const;
  virtual bool SeenRecently(AIBrain *brain, float maxAge=10.0f) const;
  virtual bool IsKnown() const;

  virtual float VisibleSize() const;
  virtual Vector3 AimingPosition() const;
  virtual Vector3 LandAimingPosition() const;
  virtual Vector3 LandAimingPosition(const AIBrain *who, const AmmoType *ammo=NULL, bool head=false, bool *isExact=NULL) const;

  virtual TargetState State(AIBrain *sensor) const;
  //@}

  USE_FAST_ALLOCATOR
};


/// virtual target - assumed to be always knows, created as needed, shared by all groups
class TargetMinimal: public Target, public CountInstances<TargetMinimal>
{
  typedef Target base;

public:
  TargetMinimal(TargetId entity);

  //@{ Target implementation
  virtual LSError Serialize(ParamArchive &ar);
  virtual LSError SaveRef(ParamArchive &ar);
  virtual bool IsMinimal() const {return true;}

  virtual const EntityAIType *GetType() const;
  virtual Vector3 GetSpeed(const AIBrain *who) const {return idExact->FutureVisualState().Speed();}

  virtual bool IsVanishedOrDestroyed() const {return idExact->IsDamageDestroyed();}
  virtual bool IsDestroyed() const {return idExact->IsDamageDestroyed();}
  virtual bool IsVanished() const {return false;}
  virtual bool HasDiclosedUs() const {return false;}
  virtual TargetSide GetSide() const {return idExact->GetTargetSide();}
  virtual bool GetSideChecked() const {return true;}
  virtual Time GetLastSeen() const;
  virtual float Distance(Vector3Par pos) const {return idExact->FutureVisualState().Position().Distance(pos);}
  virtual Vector3 GetPosition() const {return idExact->FutureVisualState().Position();}
  virtual Vector3 GetDirection() const {return idExact->FutureVisualState().Direction();}
  virtual float GetSubjectiveCost() const {return 0;}
  virtual bool IsKindOf(const EntityType *supertype) const {return idExact->GetType()->IsKindOf(supertype);}
  virtual void UpdateRemotePosition(EntityAI *ent) {}

  virtual AIBrain *SeenBy() const {return NULL;}
  virtual void MarkKiller(EntityAI *by) {}

  virtual float FadingSideAccuracy() const;
  virtual float FadingAccuracy() const;
  virtual float FadingSpotability() const;
  virtual float FadingPosAccuracy() const;

  virtual void UpdatePosition(Vector3Par pos, float typeAccuracy, float posAccuracy);

  virtual bool IsKnownBy( const AIBrain *unit ) const;
  virtual bool IsKnownByAll() const; // known by all group member
  virtual bool IsKnownBySome() const; // known by all group member
  virtual void Reveal(const SideInfo &side, TargetList *list, float accuracy); // set target as known
  virtual void RevealInVehicle(Transport *veh);

  virtual float PositionAccuracy(const AIBrain *brain) const;
  virtual bool AccurateEnoughForHit(const AmmoType *ammo, AIBrain *brain) const;
  virtual bool SeenRecently(AIBrain *brain, float maxAge=10.0f) const;
  virtual bool IsKnown() const;

  virtual float VisibleSize() const;
  virtual Vector3 AimingPosition() const;
  virtual Vector3 LandAimingPosition() const;
  virtual Vector3 LandAimingPosition(const AIBrain *who, const AmmoType *ammo=NULL, bool head=false, bool *isExact=NULL) const;

  virtual TargetState State(AIBrain *sensor) const;
  //@}
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>


/// traits to enable set of EntityAI
/**
Caution - what about NULL - Link may become NULL once EntityAI is destroyed?
*/

struct EntityAIPresentTraits: public DefMapClassTraits< Ref<TargetNormal> >
{
  /// stored type
  typedef Ref<TargetNormal> Type;
  /// key type
  typedef EntityAI *KeyType;
  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    intptr_t pointerInt = (int)key;
    // we can assume pointer is DWORD aligned
    return (unsigned int)(pointerInt>>2);
  }

  /// compare keys, return no-zero when not equal
  static int CmpKey(KeyType k1, KeyType k2) {return k1!=k2;}

  static int CoefExpand() {return 8;}

  /// get a key for given a item
  static KeyType GetKey(const Type &item) {return item->idExact;}

  struct DeleteNullTarget
  {
    bool operator () (const Ref<TargetNormal> &ref) const {return ref->idExact.IsNull();}
  };

  /// cleanup zombies - handle NULL links
  static int CleanUp(RefArray<TargetNormal> &array)
  {
    int oldCount = array.Size();
    // remove any targets with idExact = NULL
    array.ForEachWithDelete(DeleteNullTarget());
    // return how many items did we remove
    return oldCount- array.Size();
  }
};


/// list of all targets handled by AI structure
/**
Contains all targets, both known and unknown, which are withing reach of given AI.

Static passive targets (buildings, TargetMinimal) are not stored here,
only normal targets (TargetNormal, from World->_vehicles and World->_slowVehicles)
*/

class TargetList: public CountInstances<TargetList>
{
protected:
  RefArray<TargetNormal> _list[NTgtCategory]; 
  class AggregatedTargets /// Targets are divided into NAggrGroups parts
  {
    class AggregatedGroup /// Support of one group of nearby targets
    {
      friend class TargetList::AggregatedTargets;

      // AggrTargetsArray is helper Array to have a control over used operations
      // TargetNormal.aggrPresent should keep track whether the target is inside some AggrTargetsArray or not
      template <class Type=TargetNormal> 
      class AggrTargetsArray: private RefArray<Type>
      {
        typedef RefArray<Type> base;
      public:
        // The following are the only allowed methods to be used (private inherit)
        int Add(const Ref<Type> &src) { src->aggrPresent=true; return base::Add(src); }
        void DeleteAt(int index) 
        { 
          if (!base::Get(index).IsNull()) base::Get(index)->aggrPresent=false; 
          base::DeleteAt(index); 
        }
        int Size() const { return base::Size(); }
        void Clear()
        {
          for (int i=0; i<Size(); i++) 
          {
            if (!base::Get(i).IsNull())
              base::Get(i)->aggrPresent = false;
          }
          base::Clear(); 
        }
        Ref<Type> &operator [] (int i) {return base::Set(i);}
        // bypass error C2243: 'type cast' : conversion from 'TargetList::AggregatedTargets::AggregatedGroup::AggrTargetsArray<Type> *' to 'RefArray<Type> &' exists, but is inaccessible
        RefArray<Type> &GetRefArray() {return *this;}
      };
      // list of targets
      AggrTargetsArray<TargetNormal> _targets;
      // cost of _targets with aggrIsReported
      float _reportedCost;
      // cost of _targets with !aggrIsReported
      float _toReportCost;

      // simple add to the group, updates costs, position, radius
      void Add(TargetNormal *target);
      // removes targets ix from reported or toReport, updates costs and position (preserves radius)
      void Remove(int ix, TargetList::AggregatedTargets *parent);
      // suppose the position of _toReport[ix] has changed - update costs, position, radius of group
      void UpdatePosition(int ix, float tgtNewCost);
      // start this group having only the tgt target
      void StartNewGroup(TargetNormal *tgt);
    public:
      // position of the target group center
      Vector3 position;
      // radius of the target group (the circle(position,radius) should contain all targets)
      float   radius;
      // interface to private members
      float GetRCost() const {return _reportedCost;}
      float GetTRCost() const {return _toReportCost;}
      float GetGRCost() const {return _reportedCost+_toReportCost;}
      int TargetsCount() const {return _targets.Size();}

      AggregatedGroup()
      : _reportedCost(0), _toReportCost(0), position(VZero), radius(0)
      {}

      // when AIGroup containing this targetList becomes empty, all aggregation is pointless and can be cleared
      void Clear();

      LSError Serialize(ParamArchive &ar) 
      { // position, radius and costs are recomputed by GlobalUpdate call at the end of serialization (2nd pass)
        CHECK(ar.SerializeRefs("AggrGroup",_targets.GetRefArray(),1));
        return LSOK; 
      }

      ClassIsMovable(AggregatedGroup);
    };
    static const int NAggrGroups = 4;
    AutoArray<AggregatedGroup, MemAllocLocal<AggregatedGroup, NAggrGroups> > _targetGroup;
    // Every ReportDelay period the DoReport is called and decreasing change factor is used
    static const float ReportDelay; //seconds
    Time _reportTime;       //time of the next aggregated report
    static const float GlobalUpdateDelay; //seconds
    Time _globalUpdateTime; //time of the next global update 
    bool _finished; //true when ClearGroups was already called
    // index into RTChangeFactors array, which value is used to say whether the change is relevant enough to report it
    int _reportChangeFactorIx;

    /// Add target to some aggregated group and possibly reorganize the groups
    void AddNewTarget(TargetNormal *target);
    /// returns index to _targetGroup containing nearest one to target
    int FindNearestTargetGroup(TargetNormal *target);
    /// helper to possibly move targets from _reported or _toReported depending on useReported bool parameter
    void MoveTargetsToNearestGroup(AggregatedGroup &tgtGrp, Vector3 &newPos, TargetNormal *target, int ignoreGrp);
    void MoveTargetsToNearestGroup(RefArray<TargetNormal> &targets);
    /// Move target and update position, radius, cost
    void MoveTarget(AggregatedGroup &tgtGrpFrom, int ixFrom, int groupIxTo);
  public:
#if DIAG_AGGREGATED_TARGETS
    bool _debugMe;
    void DumpState(const AIGroup *group);
#endif
    AggregatedTargets()
    : _finished(false), _reportChangeFactorIx(0), _tryToSplitGroup(false)
    {
      // the following makes no allocation at all (MemAllocLocal)
      _targetGroup.Realloc(NAggrGroups);
      _targetGroup.Resize(NAggrGroups);
#if DIAG_AGGREGATED_TARGETS
      _debugMe = false;
#endif
    }

    /// Add target to be reported with delay
    void AddTarget(TargetNormal *target, float cost);
    /// move all targets from _toReport into _reported list
    void MakeAllReported();
    bool SomethingToReport() const;

    // Save/Load
    LSError Serialize(ParamArchive &ar);
    // When some slot is empty after target removal, other group can be probably split to use all slots available
    void SetEmptySlotAvailable();

    void ForceReport(Time time);

  private:
    void RecomputePositionAndRadius();
    // When some slot is empty after target removal, other group can be probably split to use all slots available
    void TryToSplitGroup();
  public:
    // change factors read from config
    static AutoArray<float> _reportTargetsChangeFactors;
    // cost of empty group for the sake of relevant change measure read from config
    static float _reportTargetsEmptyGroupCost;
    /// when some slot become empty after target removal, some group can be split to use all slots available
    bool _tryToSplitGroup;
  };
  AggregatedTargets _aggrTargets;
  /// fast way to check for presence of EntityAI
  MapStringToClass< Ref<TargetNormal>, RefArray<TargetNormal>, EntityAIPresentTraits> _presence;

public:
  static void LoadTargetAggregationStuff();
  //@{ Targets Aggregation
  void AddAggrTarget(TargetNormal *vtar, float cost);
  // moves all targets from _toReport into _reported list
  void ConsiderAggrTargetsReported();
  bool SomethingToReport() const {_aggrTargets.SomethingToReport();}

  /// force target report to happen withing a given time
  void ForceReport(Time time){_aggrTargets.ForceReport(time);}

#if DIAG_AGGREGATED_TARGETS
  void DebugAggrTargets(bool debugme) {_aggrTargets._debugMe = debugme;}
#endif
  //@}

  const TargetNormal *FindTarget( TargetType *ai ) const;
  TargetNormal *FindTarget( TargetType *ai );
  bool IsDisclosing(TargetType *ai, EntityAI *who) const;

  //@{ RefArray - like interface
  TargetNormal *GetAny(int i) const;
  TargetNormal *GetEnemy(int i) const {return _list[TgtCatEnemy].Get(i);}
  TargetNormal *GetEnemyUnknown(int i) const {return _list[TgtCatEnemyUnknown].Get(i);}
  //TargetNormal *GetFriendly(int i) const {return _list[TgtCatFriendly].Get(i);}

  int EnemyCount() const {return _list[TgtCatEnemy].Size();}
  int EnemyUnknownCount() const {return _list[TgtCatEnemyUnknown].Size();}
  //int FriendlyCount() const {return _list[TgtCatFriendly].Size();}
  int AnyCount() const
  {
    return _list[TgtCatEnemy].Size() + _list[TgtCatEnemyUnknown].Size()/* + _list[TgtCatFriendly].Size()*/;
  }

  /// count - pretend two categories are one list
  template <int sel1, int sel2>
  int SelectedCount() const {return _list[sel1].Size()+_list[sel2].Size();}

  /// get - pretend two categories are one list
  template <int sel1, int sel2>
  TargetNormal *GetSelected(int i) const
  {
    if (i<_list[sel1].Size()) return _list[sel1].Get(i);
    i -= _list[sel1].Size();
    return _list[sel2].Get(i);
  }


  bool DeleteKey(TargetNormal *tgt);
  void AnyDeleteAt(int i);
  /// add target into the corresponding slot
  void Add(const SideInfo &who, TargetNormal *tgt);

  void Clear();

  template <class Functor>
  void QSort(const Functor &func)
  {
    ::QSort(_list[TgtCatEnemy],func);
    // do not sort TgtCatEnemyUnknown - subjective cost is the same for all of them
    //::QSort(_list[TgtCatFriendly],func);
  }
  void CompactIfNeeded();
  LSError Serialize(ParamArchive &ar, const RStringB &name, int minVersion);

  bool IsPresent(EntityAI *ai);
  //@}

  virtual const SideInfo *GetSideInfo(AIBrain *brain) const = 0;
  virtual TargetNormal::TrackContext &InitTrackContext(AIBrain *brain) = 0;
  virtual TargetNormal *CreateTarget(AIBrain *brain) = 0;

  void TrackTargets(AIBrain *unit, EntityAIFull *veh, float trackTargetsPeriod[NTgtCategory], float maxDist, TargetNormal::TrackContext &ctx);

  /// move targets from one list into other lists as needed
  void DistributeTargets(TgtCategory from);
};

//! full featured implementation of EntityAI
class EntityAIFull: public EntityAI
{
  typedef EntityAI base;

protected:  

  /// camera interest bonus
  float _cameraInterest;

  //@{ firing related functionality
  float _shootAudible; //!< How much is our weapon firing audible (0 default)
  float _shootVisible; //!< How much is our weapon firing visible (0 default)
  float _shootTimeRest; //!< How long will current fire visibility last (before returning to 1.0)
#if _VBS3
  float _shootTimeRestAudible; //!< How long will current fire audibility last (before returning to 1.0)
#endif
  OLinkO(TargetType) _shootTarget; //!< What we fired at

  // so that it can be easily moved to AIUnit
  FireState _fireState;
  Time _fireStateDelay;

  mutable Time _lastWeaponReady; //!< Time when last "Ready to fire" was reported
  mutable Time _lastWeaponNotReady; //!< Time when "Cannot fire was reported"

  //@{ target tracking functionality
  //! Time when function EntityAI::TrackTargets(TargetList &res, bool initialize)
  // was last performed
  Time _trackTargetsTime[NTgtCategory];
  //! Time when function EntityAI::NewTargets was last performed
  Time _newTargetsTime;

  //! Distance to nearest known enemy (used to change helicopter behavior)
  //\todo Use CombatMode instead
  float _nearestEnemy;

  /// reload processed immediately when magazine is empty
  bool _reloadEnabled;

  //@}
  SpeedEstimation _wantedPosSpeed;
  float _rearmCredit; //!< Used during rearm (when using cost instead of magazine)

  /// user friendly description of the unit
  RString _description;

  //! Continuous visibility tracker - remember results and reuse them
  //! Normal visibility (SensorList) is not precise enough when firing
  mutable VisibilityTrackerCache _visTracker;


  //! if player, auto-aim status is stored here
  LinkTarget _autoAimLocked;
  /*!
  \name Recoil
  \patch_internal 1.12 Date 08/07/2001 by Ondra
  - Moved: was in Man class
  */
  //@{
  Ref<RecoilFunction> _recoil;
  float _recoilTime; //!< current position in recoil
  float _recoilFactor; //!< recoil factor - used when prone or crouch
  /// which recoil item is currently played
  int _recoilIndex;
  //@{ random values for recoil
  float _recoilRandomX,_recoilRandomY,_recoilRandomZ;
  //@}
  /// randomize aiming requirements to make sure even dumb AI sometimes hits
  float _aimRandom;

  /// unit should fire at the assigned target to suppress it when it cannot fire to hit
  SuppressState _useSuppressiveFire;

  /// suppressive fire ordered (via script or command interface) until some time
  Time _suppressUntil;

  //! Start recoil effect
  void StartRecoil(RecoilFunction *recoil, float recoilFactor);
  //! Start one force feedback ramp based on recoil
  void StartRecoilFF();

  /// prepare variables needed for the next shot
  void AfterFire();
  /// recoil impulses are requested - apply them as needed, by default there is no reaction to the recoil
  virtual void ApplyRecoil(float impulseAngX, float impulseAngY, float offsetZ) {}

  /// select recoil table based on current stance or other status
  virtual RStringB GetRecoilName(const WeaponModeType *mode) const;
  //@}

  //@{ vehicle is going to stop engine 
  /// engine is waiting for stop, but still on 
  bool _engineOffTimeOutInProgress;
  /// time when engine will be stopped
  Time _engineOffTimeOut; 
  //@}
 
  //! Heat source direction of the object. This value changes smoothly and size of the vector is telling us how much ambient we should be used instead (the smaller the vector, the more ambient should be used)
  Vector3 _heatSourceDirection;
  
  float _surfaceAudibility;
  float _surfaceVisibility;
  float _poseAudibility;
  float _poseVisibility;

public:
  // constructor
  EntityAIFull(const EntityAIType *type, bool fullCreate = true);
  // destructor
  ~EntityAIFull();

  // post process effect assigned for entity
  virtual bool GetPostProcessEffects(const TurretContextV &context, AutoArray<PPEffectType> **ppEffects) const {return false;}

  virtual void Init(Matrix4Par pos, bool init);

  virtual void OnSetDelete() override;

  TargetType *GetShootTarget() const {return _shootTarget;}

  virtual WeaponsState *GetWeaponsState() {return NULL;}

  virtual Time GetLastShotTime() const;
  Entity *GetLastShot() const;

  bool CheckTargetKnown(const TurretContext &context, Target *target) const;

  RString GetDescription() const {return _description;}
  void SetDescription(RString description) {_description = description;}
#if _VBS3
  virtual float GetTotalWeightKg();
#endif

  /*!
  \name Hide decision
  */
  //@{
  Target *FindNearestEnemy(Vector3Par pos);
  bool IsInFormation() const {return _inFormation;}

  /// check current speed, consult movement history
  bool IsStandingStill() const;

  /// find a suitable cover
  virtual bool FindCover(CoverVars &coverVars, const OperCover &cover, Vector3Par tgtPos) {return false;}

  /// react to a cover found by path-finding
  virtual bool OnPathFoundToCover(){return true;}

  /// instruct the soldier to leave the cover
  virtual void LeaveCover() {}

  // vehicles, which are not able to use cover, always report as not having entered any cover
  virtual bool CheckCoverEntered() {return false;}
  /// check if the LeaveCover instruction was already executed
  virtual bool CheckCoverLeft() {return true;}

  /// check if unit is exposed and needs to be covered
  virtual bool CheckSupressiveFireRequired() const {return false;}

  /// check if given cover is occupied by this unit
  virtual bool IsOccupyingCover(const CoverInfo &info) const {return false;}

  enum CoverStyle
  {
    NotInCover,
    InOpenCover,
    InRealCover,
  };
  /// report to others is we are in cover, so that they can decide if they need to be covering us
  /** vehicles which are not able to use cover report themselves as always in cover */
  virtual CoverStyle IsInCover() const {return InOpenCover;}
  /// how far are we on a way to a cover
  /* 1 = we are there, 0 = we have just started, 0.5 = halfway to a cover */
  virtual float GoingToCover() const {return IsInCover()>NotInCover ? 1.0f : 0.0f;}

  /// check combat mode based on a state of the vehicle, as determined by the given crew member
  virtual CombatMode GetCombatModeAutoDetected(const AIBrain*brain) const;
  
  bool IsReloadEnabled() const {return _reloadEnabled;}
  void EnableReload(bool enable) {_reloadEnabled = enable;}

  //@}

  // vehicle has become a formation member
  virtual void SwitchToFormation()
  {
    // reset to initiate movement
    _inFormation = false;
  }
  // vehicle has become a formation leader
  virtual void SwitchToLeader()
  {
    // reset to initiate movement
    _inFormation = false;
  }

  //! Simulation
  bool SimulationReady(SimulationImportance prec) const;
  void Simulate(float deltaT, SimulationImportance prec);

#if _VBS2   //convoy trainer
  void SimulatePost(float deltaT, SimulationImportance prec){ base::SimulatePost(deltaT,prec); };
#endif

  virtual LSError Serialize(ParamArchive &ar);

  /// draw in-game vision helpers
  void DrawPeripheralVision(Person *person, CameraType camType);

  virtual bool EnableOptics(ObjectVisualState const& vs) const {return true;}

  float Pass3IntensityMod(int level, const DrawParameters &dp) const;

  virtual void DrawDiags();

  Vector3 PredictFormationSpeed() const;
  virtual void DrawFocusedDiags();

  /// check if simple path is still valid
  bool IsSimplePathValid(Time time) const {return time-MaxSimplePathAge<_lastSimplePath;}
#if _ENABLE_AI
  //! Adjust controlling values accordingly to planned path of driver
  virtual bool PathPilot(SteerInfo &info, float speedCoef = 1.0f);

  virtual bool UseBackwardMovement(Vector3Par toMove, EntityAI *leaderVehicle) const;
  
  Vector3 GetTargetAimDir(Vector3Par dir, bool preferTarget=false) const;

  /// when not moving, we may rotate to allow the primary weapon to aim at the target if needed
  void AlignToTarget(SteerInfo &info) const;

  //! Perform all (including path planning) so that unit is moving toward subgroup target point.
  void LeaderPathPilot(AIBrain *unit, SteerInfo &info, float speedCoef=1.0f);

  /// when we are stopped, micro AI may act to certain extent
  virtual void StopPilot(AIBrain *unit, SteerInfo &info);

  /// sometimes movement may be necessary even when we are seemingly in formation
  /** like when we are moving into a cover */
  virtual bool CheckMovementNeeded() const {return false;}

  /// check if plan change is possible
  /** it is not possible when running into a cover */
  virtual bool PlanChangePossible() const {return true;}

  /// what position should we plan from - considering the cover we are in  
  virtual Vector3 PositionToPlanFrom() const {return FutureVisualState().Position();}

  /// smooth braking before reaching the goal
  float BrakeBeforeReached(float finalDist2) const;
  //! Hide if necessary, otherwise perform LeaderPathPilot
  virtual void LeaderPilot(SteerInfo &info);

  /// updating suppressive fire maps (SuppressCache)
  void TraceFireSector(TurretContextEx &context);

#endif

  //! Return LockTarget action for this vehicle
  UserAction GetLockTargetAction() const {return GetType()->GetLockTargetAction();}
  UserAction GetLockEmptyTargetAction() const {return GetType()->GetLockEmptyTargetAction();}

  //! Calculate visibility to given unit
  float CalcVisibility( EntityAI *ai, float dist2, float *audibility=NULL, float *sharpness=NULL, bool assumeLOS=false );

  //! Track near targets
  virtual void TrackNearTargets(TargetList &res, float maxDist);

  /// track targets which we are locked to (assigned, preferred...)
  virtual void TrackLockedTargets(TargetList &res);

  //! Track all targets that might be visible (used regularly, about 1-5 sec)
  virtual void TrackTargets(TargetList &res, bool initialize, float trackTargetsPeriod[NTgtCategory]);
  //! each vehicle should be known by all units sitting in it
  virtual void TrackMyVehicle(TargetNormal *target, AIBrain *unit, float trackTargetsPeriod, const SideInfo &ctx);
  //! Track one target using all relevant units in the vehicle
  virtual void TrackOneTarget(TargetList &res, Target *target, float trackTargetsPeriod);

  //! Low level target tracking
  void TrackTargets(TargetList &res, AIBrain *unit, int canSee,
    bool initialize, float maxDist, float trackTargetsPeriod[NTgtCategory]);

  TargetNormal::TrackContext *InitTrackContext( TargetList &res, AIBrain * unit, int canSee, bool initialize, float maxDist );

  /// low-level helper for TrackTargets
  void TrackTargetsPart(RefArray<TargetNormal> &tgt, AIBrain *unit,
    float trackTargetsPeriod, float maxDist, TargetNormal::TrackContext &ctx);

  /// helper for selective target tracking
  void TrackOneTarget(Target *target, AIBrain *unit,
    float trackTargetsPeriod, float maxDist, TargetNormal::TrackContext &ctx);
  struct AddNewTargetContext;
  /// helper for AddNewTargets
  void AddNewTarget(TargetList &res, bool initialize, AddNewTargetContext &ctx, Entity *obj);
  /// we can react to seeing somebody getting in
  void ReactToGetIn(TargetList &res, EntityAI *obj, Transport *trans);
  //! Add targets that might be visible (are in range) to list 
  void AddNewTargets(TargetList &res, bool initialize);
  //! Perform target tracking
  void WhatIsVisible(TargetList &res, bool initialize);

  /// used when a specific target may need to be added
  void AddNewTarget(TargetList &res, bool initialize, EntityAI *obj);
  void CheckKiller(TargetList &res, EntityAI *killer, EntityAI *obj);

  using Entity::ForEachTurret;
  /// enumerate all turrets
  virtual bool ForEachTurret(EntityVisualState& vs, ITurretFuncV &func) const {return false;}
  /// enumerate all turrets
  virtual bool ForEachTurretEx(EntityVisualState& vs, ITurretFuncEx &func) const {return false;}
  /// find the turret (weapons state) controlled by the given person
  /**
  use only in functions where person is on input (player person etc.)
  */
#if _VBS3 //Commander override
  /// find the turret the gunner looks through (can be an overridden turret)
  virtual bool FindOpticsTurret(const Person *gunner, TurretContext &context) const {return false;}
  /// find the turret the gunner currently controls
  virtual bool FindControlledTurret(const Person *gunner, TurretContext &context) const {return false;}  // TODO: should have VisualState version
  /// get the factor the initspeed is modified by (stance for throwing objects)
  virtual float GetAmmoInitSpeedFactor(const MagazineType *aInfo) const {return 1.0f;}
#endif
  virtual bool FindTurret(const Person *gunner, TurretContext &context) const {return false;}
  /// find the turret (weapons state) controlled by the given person
  /**
  use only in functions where person is on input (player person etc.)
  */
  virtual bool FindTurret(EntityVisualState& vs, const Person *gunner, TurretContextV &context) const {return false;}
  /// retrieve the context for the given turret
  virtual bool FindTurret(const Turret *turret, TurretContext &context) const {return false;}
  /// retrieve the context for the given turret and its visual state
  virtual bool FindTurret(EntityVisualState& vs, const Turret *turret, TurretContextV &context) const {return false;}

  /// return if the vehicle can fire using given turret
  virtual bool CanFireUsing(const TurretContext &context) const;

#if _ENABLE_AI
  /// Commander to primary gunner commanding
  virtual void CommandPrimaryGunner(AIBrain *unit, TurretContext &context) {}
#endif

  float GetCameraInterest() const {return _cameraInterest;}
  void SetCameraInterest(float interest) {_cameraInterest = interest;}

  virtual void PerformAction(const Action *action, AIBrain *unit) override;
  virtual bool GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const override;
  virtual void GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit) override;
#if _HELISIM_PRECISE_INTERACTION
  void GetDrawActions3D(UIActions &actions, AIBrain *unit);
#endif

  virtual float GetExplosives() const; // how much explosives is in
  /// get inaccuracy (0 means accurate, 0.01 means 1 m/100m)
  virtual float GetCursorInaccuracy() const {return 0.0f;}
  //! Get texture for main mouse cursor
  virtual const CursorTextureInfo *GetCursorTexture(Person *person);
  //! Get texture dot cursor (indicating weapon aim)
  virtual const CursorTextureInfo *GetCursorAimTexture(Person *person);
  // Get weapon cursor size
  virtual float GetCursorSize(Person *person) const;
  //! Get mouse cursor color
  virtual PackedColor GetCursorColor(Person *person) {return base::GetCursorColor(person);}

  /// sometimes lights may be needed to be on (planes while landing / taxiing)
  virtual bool LightsNeeded() const {return false;}

  //! check auto-aim lock
  virtual bool GetCursorLocked(Person *person) const;

  //! Check if weapon aiming dot should be drawn
  virtual bool ShowAim(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const;

  //! Get model to be drawn as screen overlay
  virtual LODShape *GetOpticsModel(const Person *person) const {return NULL;}
  //! Get color of optics overlay (color multiplication is applied)
  virtual PackedColor GetOpticsColor(const Person *person) {return PackedBlack;}
  //! Check if optics must be used
  virtual bool GetForceOptics(const Person *person, CameraType camType) const {return false;}
  /// check if optics can be drawn
  virtual bool GetEnableOptics(ObjectVisualState const& vs, const Person *person) const {return false;} 

  virtual float DetectStall() const {return -1.0f;}

  float GetAmmoCost() const; // weapon resources
  float GetAmmoHit() const; // weapon resources
  /// check if there are enough ammo resources
  bool CheckAmmoHitOver(float hit) const;

  float GetMaxAmmoCost() const;
  float GetMaxAmmoHit() const;

  float Rearm(float resources); // transfer resources

  float EstimateTravelCost(Vector3Par from, Vector3Par to, float costPerSecond) const;

  virtual void OnAddImpulse(Vector3Par force, Vector3Par torque) {}
  /// event handler - some stress factor nearby
  virtual void OnStress(Vector3Par pos, float hit) {}

  /// Start recoil effect based on name
  void StartRecoil(RStringB name, float recoilFactor);
  //! ask vehicle about recoil factor
  // TODO: get from config
  virtual float GetRecoilFactor() const {return 0.5f;}
  /// returns coef for camera shake in current state,  when the weapon is fired
  virtual float GetCamShakeFireCoef() const {return 1.0f;}

  //! Fire missile - perform actual releasing
  bool FireMissile(const TurretContext &context, const WeaponModePtr &ptr,
    Vector3Par offset, Vector3Par direction, Vector3Par initSpeed,
    TargetType *target, bool createNetworkObject, bool remote, bool forceLock);
  //! Fire shell, throw grenade
  bool FireShell(const TurretContext &context, const WeaponModePtr &ptr,
    Vector3Par offset, Vector3Par direction,
    TargetType *target, bool createNetworkObject, bool remote);
  //! Fire bullet
  bool FireMGun(const TurretContext &context, const WeaponModePtr &ptr,
    Vector3Par offset, Vector3Par direction,
    TargetType *target, bool createNetworkObject, bool remote);

  virtual float GetWeaponCursorScale() const {return 1.0f;}

  virtual float NeedsRearm() const;
  virtual void ResetStatus();

  // check how much need to reload magazines
  float GetReloadNeeded() const;
  // reload magazines
  void Reload();

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
  TMError TransferMsg(NetworkMessageContext &ctx);
  float CalculateError(NetworkMessageContextWithError &ctx);

  //! Perform sounds
  void Sound(bool inside, float deltaT);
  //! Unload any sounds currently playing
  void UnloadSound();

  //! if tracked visibility is known, use it, if not, use visibility matrix
  float GetTrackedVisibility(bool &headOnly,
    AIBrain *unit, const TurretContext &context, const WeaponModePtr &ptr, const Target &tgt, float reserve) const;

  /// check how much is movement visible, consider size
  virtual float VisibleMovement() const;

  /// check how much is movement visible, do not consider size
  virtual float VisibleMovementNoSize() const;

  //! Check how much is sound of this entity audible
  virtual float Audible() const;

  //! Check how much is firing weapons from this entity visible
  float VisibleFire() const {return _shootVisible;}
  //! Check how much is firing weapons from this entity audible
  float AudibleFire() const {return _shootAudible;}

  //! Check what is this entity firing at
  TargetType *GetFiredAt() const {return _shootTarget;}

  // TODO: make member of EntityAI
  Vector3 GetWeaponSoundPos(const TurretContext &context, const WeaponModePtr &ptr) const
  {
    const VisualState &vs = RenderVisualState();
    return vs.PositionModelToWorld(GetWeaponPoint(vs, context, ptr));
  }

  // Start auto-reload of empty slot
  bool AutoReload(WeaponsState &weapons, const WeaponModePtr &ptr, bool reloadAI, bool loaded);
  // Start auto-reload of all empty slots
  WeaponModePtr AutoReloadAll(WeaponsState &weapons, bool loaded);
  // Start auto-reload of all empty slots, when loaded is set, magazines will be loaded immediately
  void AutoReloadAll(bool loaded);

  void GunnerLoadMagazine(const TurretContext &context, Magazine *magazine, RString weapon, RString muzzle);
  virtual void CommanderLoadMagazine(Magazine *magazine, RString weapon, RString muzzle);

  //! Simulation of mostly weapon related things
  void SimulateWeaponActivity(float deltaT, SimulationImportance prec);

  /// start camera effect
  void StartFadeIn(AIBrain *unit);

#if _VBS3_LASE_LEAD
  /// Calculate aiming position necessary to hit the target position
  bool CalculateAimWeaponPos(const TurretContext &context, const WeaponModePtr &ptr, Vector3 &pos, float &timeToLead, const Vector3 tgtPos) const;
  /// Calculate the aim direction necessary to hit the target position
  bool CalculateAimWeapon(const TurretContext &context, const WeaponModePtr &ptr, Vector3 &dir, float &timeToLead, const Vector3 tgtPos) const;
#endif

  /// Calculate aiming position necessary to hit the target; when exact is true, the real speed and position are used
  virtual bool CalculateAimWeaponPos(const TurretContext &context, const WeaponModePtr &ptr, Vector3 &pos, float &timeToLead, Target *target, bool exact) const {return false;}
  /// Calculate direction of the weapon necessary to hit the target
  virtual bool CalculateAimWeapon(const TurretContext &context, const WeaponModePtr &ptr, Vector3 &dir, float &timeToLead, Target *target, bool exact) const;

  /// compute bullet/shell ballistics - time to fly, fall down
  float PreviewBallistics(Vector3Par tgtPos, Vector3Par shotPos, const MagazineType *aInfo, const AmmoType *ammo, float &fall, const WeaponModeType *mode = NULL) const;
  
  /// compute rocket/missile ballistics - time to fly, fall down
  float PreviewRocketBallistics(Vector3Par tgtPos, Vector3Par shotPos, const MagazineType *aInfo, const AmmoType *ammo, float &fall, const WeaponModeType *mode = NULL) const;
  
  //! aim weapon - adjusted by auto-aim
  virtual bool AimWeaponAdjusted(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction) {return AimWeapon(context, ptr, direction);}

  virtual bool IsAutoAimEnabled(const TurretContext &context, const WeaponModePtr &ptr) const;

  //! Weapon aiming interface from UI (manual)
  virtual void AimWeaponManDir(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction);

  /// select target that is close to the aimed direction
  Target *SelectAutoAimTarget(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction, float enlarge=1.0f, bool limitSize=true) const;

  /// select target that is close to the aimed direction - auto locking devices
  Target *SelectLockTarget(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction, const AmmoType *ammo, float minAngle = -1.0f) const;

  virtual void AutoAimCursor(const TurretContextEx &context, ValueWithCurve &aimX, ValueWithCurve &aimY, float deltaT, float fov);

  //! Aim weapon to given direction (world space)
  virtual bool AimWeapon(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction) {return AimWeaponAdjusted(context, ptr, direction);}
  //! Aim weapon to given target
  virtual bool AimWeaponTgt(const TurretContextEx &context, const WeaponModePtr &ptr, Target *target)
  {
    Vector3 dir;
    float timeToLead;
    return CalculateAimWeapon(context, ptr, dir, timeToLead, target, false) && AimWeapon(context, ptr, dir);
  }

  //! Aim weapon - response to fire procedure
  virtual bool AimWeaponForceFire(const TurretContextEx &context, const WeaponModePtr &ptr);

  //! Check if can move (if not damaged to much)
  virtual bool IsAbleToMove() const {return CanFire();}
  //! Check if can move (if not damaged to much or out of ammo)
  virtual bool IsAbleToFire(const TurretContext &context) const {return (context._weapons->HasWeapons()) && CanFire();}

  /// return true when soldier is swimming
  virtual bool IsSwimming() const {return false;}

  //! Check if can be moving on the road and lights can be on
  virtual bool IsCautious() const {return base::IsCautious();}

  //! Check if weapon manipulation is enabled
  virtual bool EnableWeaponManipulation() const;

  //! Check if entity is able to use optics
  virtual bool EnableViewThroughOptics() const {return true;}

  //! Get main direction entity wants to be watching - (world space)
  virtual Vector3 GetEyeDirectionWanted(ObjectVisualState const& vs, const TurretContextEx &context) const {return GetEyeDirection(vs, context);}
  //! Get wanted direction of weapon - (world space)
  virtual Vector3 GetWeaponDirectionWanted(const TurretContextEx &context, const WeaponModePtr &ptr) const {return GetWeaponDirection(FutureVisualState(), context, ptr);}

  //! Get main direction entity is watching
  // This direction returned is typically the head (eye) direction or direction of observer turret when appropriate
  virtual Vector3 GetEyeDirection(ObjectVisualState const& vs, const TurretContext &context) const {return GetWeaponDirection(vs, context, WeaponModePtr());}
  //! Get direction of weapon
  virtual Vector3 GetWeaponDirection(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const {return vs.Direction();}
  //! Get direction of weapon to be displayed to the user
  virtual Vector3 GetAimCursorDirection(const TurretContext &context, const WeaponModePtr &ptr) const {return GetWeaponDirection(RenderVisualState(), context, ptr);}
  //! Get center of weapon rotation
  virtual Vector3 GetWeaponCenter(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const {return VZero;}

  //! Get weapon position (where the projectiles leave the weapon)
  virtual Vector3 GetWeaponPoint(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const {return GetWeaponCenter(vs, context, ptr); /* Rrrola: why Center? */ }
  //! Get weapon dumped cartridge position (how should be cartridge disposed)
  virtual bool GetWeaponCartridgePos(const TurretContext &context, const WeaponModePtr &ptr, Matrix4 &pos, Vector3 &vel) const;

  //! Check if weapon is loaded
  bool GetWeaponLoaded(const WeaponsState &weapons, const WeaponModePtr &ptr) const;
  //! Check if AI is ready to fire next shot.
  bool GetWeaponReady(const WeaponsState &weapons, const WeaponModePtr &ptr, Target *target) const;
  //! Check probability weapon will hit target
  virtual float GetAimed(const TurretContext &context, const WeaponModePtr &ptr, Target *target, bool exact=false, bool checkLockDelay = false) const;
  
  float CheckAimed(Vector3Par ap, Vector3 speed, const TurretContext &context, const WeaponModePtr &ptr, Target *target, bool exact, const AmmoType *ammo) const;

  float ComputeAimError(Vector3Par ap, const TurretContext & context, const WeaponModePtr &ptr, Target * target, bool exact) const;
  //! Safety check to avoid friendly fire
  virtual bool CheckFriendlyFire(const TurretContext &context, const WeaponModePtr &ptr, Target *target) const;
  //! Check if target is in weapon fire angle
  virtual float FireInRange(const TurretContext &context, const WeaponModePtr &ptr, float &timeToAim, const Target &target) const {return 1.0f;}
  //! Check if direction is in weapon fire angle
  virtual float FireAngleInRange(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par rel) const;

  /// Check fire direction limits
  virtual void FireAimingLimits(float &minAngle, float &maxAngle, const TurretContext &context, const WeaponModePtr &ptr) const;

  /// helper to obtain aiming position for all common scenarios (normal, auto-aim, suppressive fire)
  float GetAimingPosition(Vector3 &pos, Vector3 &speed, float &seenBefore,
    bool exact, const TurretContext &context, Target *target, const AmmoType * ammo) const;
  /// check user manual aim accuracy for purpose of auto-aim indication
  float GetAimAccuracy(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction) const;

  virtual bool GetWeaponAim(TurretContext context,Vector3 &position, Vector3 &direction) {return false;};

  virtual Vector3 AdjustWeapon(const TurretContext &context, const WeaponModePtr &ptr, float fov, Vector3 direction) const {return Vector3();};
  virtual bool GIsManual(const Person *gunner) const {return false;};

  /// helper - compute firing interval for the suppressive fire
  float SuppressiveFireInterval(float seenBefore, const TurretContext &context, float boost=1.0f) const;

  /// helper - compute boost value for SuppressiveFireInterval from CheckSuppressiveFire
  float GetSuppressiveFireBoost() const;

  void AllowSuppressiveFire(SuppressState val) {_useSuppressiveFire = val;}
  /// return last value set by AllowSuppressiveFire
  SuppressState GetAllowSuppressiveFire() const {return _useSuppressiveFire;}
  /// check current suppressive fire state based on combination of SuppressUntil/AllowSuppressiveFire inputs
  SuppressState CheckSuppressiveFire() const;

  void SuppressUntil(Time until) {_suppressUntil = until;}
  Time GetSuppressUntil() const {return _suppressUntil;}

  virtual void SelectWeaponCommander(AIBrain *unit, const TurretContext &context, const WeaponModePtr &ptr);
  virtual void SimulateWeaponInputs(InputInterface *input, AIBrain *unit, const TurretContext &context);

  bool CanLock(const TargetType *type, const TurretContext &context, WeaponModePtr ptr = WeaponModePtr()) const;

  bool IsCombatUnit() const;

  /// check if some bomb attack is pending
  virtual bool HasPendingBomb() const {return false;}
  /// touch off any pending bombs
  virtual void TouchOffBombs() {}

  // check if fire line is clear
  bool CheckFireWeapon(int weapon, TargetType *target, Vector3Par weaponPos);
  virtual bool FireWeapon(const TurretContextV &context, const WeaponModePtr &ptr, TargetType *target, bool forceLock);
  // prepares throwing 
  virtual bool PrepareThrow(const TurretContext &context, const WeaponModePtr &ptr) {return false;}

  virtual Vector3 GetWeaponManualControlDirection(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const;
  virtual Vector3 GetWeaponManualControlPoint(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const;

  void ManualControlMissile(TurretContext &context);

#if _VBS3
  bool PostFireWeapon(const TurretContext &context, const WeaponModePtr &ptr, TargetType *target, RemoteFireWeaponInfo &remoteInfo);
#else
  bool PostFireWeapon(const TurretContext &context, const WeaponModePtr &ptr, TargetType *target, const RemoteFireWeaponInfo &remoteInfo);
#endif

  int RemoveAmmoAfterShot(const WeaponModeType * mode, Magazine * magazine);

  const AmmoType *ValidateFireWeaponWithAmmo(const WeaponModePtr &ptr, const Magazine *magazine);
  /// creates shot from current vehicle state or from state received through network
  /**
  @param remoteInfo when remote fire is processed, parameters of the fire are passed here
  @param localInfo when local fire is simulated, it will the parameters for remote processing
  */
  virtual bool ProcessFireWeapon(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo *remoteInfo, RemoteFireWeaponInfo *localInfo, bool forceLock) {return false;}
  /// perform effects after weapon is actually fired
  virtual bool FireWeaponEffects(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo *remoteInfo);
  /// preload anything needed for FireWeaponEffects
  bool PreloadFireWeaponEffects(const WeaponsState &weapons, const WeaponModePtr &ptr);
  /// preload muzzle flash for given weapon
  // this class has no idea what muzzle flash is used
  virtual bool PreloadMuzzleFlash(const WeaponsState &weapons, const WeaponModePtr &ptr) const {return true;}

  /// automatically select any suitable weapon
  void AutoselectWeapon(const Person *gunner);

  /*!
  \name Weapon processing
  Function related to adding, removing or finding weapons,
  magazines, muzzles, modes ...
  */
  //@{
  //! event handler: some weapon added
  virtual void OnWeaponAdded() {}
  //! event handler: some weapon removed
  virtual void OnWeaponRemoved() {}
  //! event handler: new weapon selected from action menu
  virtual void OnWeaponSelected() {}
  //! event callback: danger detected
  virtual void OnDanger() {}
  //! check if reload of current weapon is enabled
  virtual bool CanReloadCurrentWeapon() {return true;}

public:
  //! attach magazine to given muzzle (reload low level implementation)
  void AttachMagazine(const TurretContext &context, const MuzzleType *muzzle, Magazine *magazine);

  virtual bool HasSomeWeapons() const;

  //! reload implementation
  bool ReloadMagazineTimed(WeaponsState &weapons, const WeaponModePtr &ptr, bool afterAnimation);

  //! find and reload magazine
  bool ReloadMagazine(WeaponsState &weapons, const WeaponModePtr &ptr, bool loaded);
  /// request to start loading of magazine (set wanted value)
  bool ReloadMagazine(WeaponsState &weapons, const WeaponModePtr &ptr, Magazine *magazine);
  //! reload magazine implementation (hi level implementation)
  // slotIndex magazine slot to reload, iMagazine index of reloading magazine
  virtual bool DoReloadMagazine(WeaponsState &weapons, const WeaponModePtr &ptr)
  {
    return ReloadMagazineTimed(weapons, ptr, false);
  }


  //! play sound for magazine reload
  void PlayReloadMagazineSound(const TurretContext &context, const WeaponModePtr &ptr, const MuzzleType *muzzle);
  //! play "dry" sound
  void PlayEmptyMagazineSound(const TurretContext &context, const WeaponModePtr &ptr);
  //! play flatling bullet sound
  virtual const SoundPars& SelectBulletSoundPars(const TurretContext &context, const WeaponModePtr &ptr) const;
  //@}

  //! Virtual method
  virtual bool GetOwnHeatSource(Vector3 &ownHeatSource) const;

  //set max. damage for all hit zones (engineer's fix)
  virtual void SetMaxHitZoneDamage(float dammage);
  void SetMaxHitZoneDamageNetAware(float dammage);

  // check whether vehicle is equipped Ti optics
  virtual bool HasTiOptics() const {return false;}

#if 1//_VBS3_WEAPON_STATES
public:
  bool SetWeaponReloadTime(Person *gunner, RString muzzleName, float reloadTime);
#endif

  INHERIT_SOFTLINK(EntityAIFull,base);
  USE_CASTING(base)
};

inline TargetType *LinkTarget::IdExact() const
{
  Target *link=GetLink();
  if( link ) return link->idExact;
  return NULL;
}

#if _ENABLE_INDEPENDENT_AGENTS

class TargetListInAgent : public TargetList
{
public:
  void Manage(AIAgent *agent);
  virtual const SideInfo *GetSideInfo(AIBrain *brain) const;
  virtual TargetNormal::TrackContext &InitTrackContext(AIBrain *brain);
  virtual TargetNormal *CreateTarget(AIBrain *brain);
};

#endif


// WIP:BALLISTICS_COMPUTER:BEGIN

// status of the ballistics computer
enum BCStatus
{
  // parameters are invalid, computation cannot be done
  BCinvalid = -1,
  // ready for computation, but target correction is not valid yet
  BCready,
  // target correction has been computed/updated
  BCupdated
};

/// ballistics computer
class BallisticsComputer
{
private:
  // parameters
  const EntityAIFull *_shooter;
  TurretContext _context;
  Target *_target;
  // helper for setting the parameters
  bool SetParams(const EntityAIFull *shooter, const TurretContext &context, Target *target);
  // ballistic computer
  bool SimulateBallistics(Vector3Par relPos, Vector3Par relSpeed, const MagazineType *ammoInfo, float& time, Vector3& correction);
public:
  // corresponding info
  WeaponModePtr _weapon;
  const MagazineType *_aInfo;
  const AmmoType *_ammo;
  // status
  BCStatus _status;
  // shooter position
  Vector3 _weaponPos;
  // original target position
  Vector3 _targetPos;
  // estimated target speed
  Vector3 _targetSpeed;
  // correction of the target position due to ballistics, leading, etc.
  Vector3 _targetCorrection;
  // time needed to hit the target
  float _time;
  // how much time elapsed since we had clean line of sight to the target
  float _seenBefore;

  BallisticsComputer() {_status = BCinvalid;}
  // compute ballistics
  bool Compute(const EntityAIFull *shooter, const TurretContext &context, Target *target, bool exact=false);
  // provide appropriate view correction
  Matrix3 ViewCorrection() const;
};
// WIP:BALLISTICS_COMPUTER:END

#endif

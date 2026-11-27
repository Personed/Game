/*!
  \file
  Interface of Entity
*/

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _VEHICLE_HPP
#define _VEHICLE_HPP

#include "object.hpp"
#include "soundsys.hpp"
#include "soundScene.hpp"
#include "handledList.hpp"
#include "paramFileExt.hpp"

#include <Es/Types/taskRef.hpp>

#include <El/Activity/activity.hpp>
#include <El/SimpleExpression/simpleExpression.hpp>

#ifdef _WIN32
#include <Java/jni.h>
#else
#include <jni.h>
#endif
#include <El/Evaluator/jniFactory.hpp>

#include "AI/aiTypes.hpp"

#include "animation.hpp"
#include "head.hpp"
#include "animSource.hpp"
#include "wounds.hpp"
#include "global.hpp"

#include <functional>
#include <utility>

//#include "Physics\enf_worldphysics.h"

#if defined _MSC_VER && _MSC_VER<1700

namespace std
{
  template <typename T>
  typename std::add_rvalue_reference<T>::type declval(); // no definition required
};

#endif


struct DestructionEffectType
{
  RStringB _simulation;
  RStringB _type;
  RStringB _position;
  // multipliers
  float _intensity;
  float _interval;
  float _lifeTime;

  void Load(ParamEntryVal cls);  
};
TypeIsMovableZeroed(DestructionEffectType)


// Simul is basic simulation class - no rendering
// most vehicles are visible objects

// autopilot state is used for autopilots in different vehicles
enum AutopilotState
{
  AutopilotFar,
  AutopilotBrake,
  AutopilotNear,
  AutopilotAlign,
  AutopilotReached
};

enum Condition {ConditionGreen,ConditionYellow,ConditionRed};

typedef enum {GETIN_OLD,GETIN_ONPROXY,GETIN_CENTER} GetInOutMode;
TypeIsSimple(GetInOutMode);


struct FFEffects
{
  //float forceX,forceY;
  float engineFreq,engineMag;
  //float gunFreq,gunMag;
  //int gunCount;
  float stiffnessX, stiffnessY; // auto-centering forces
  float surfaceMag;

  FFEffects();
};

struct AnimateTextureInfo
{
  AnimationAnimatedTexture animation;
  float animSpeed;
};
TypeIsMovable(AnimateTextureInfo)

class EntitiesDistributed;
class EntityList;

class InventoryItem;

//! proxy object
/*!
Object included as inner level of hierarchy (sub-object)
*/

#include <Es/Memory/normalNew.hpp>

struct ProxyObjectTyped: public RefCount
{
  // note: scale allowed in transform

  Ref<Object> obj;

  __forceinline LODShape *GetShape() const {return obj->GetShape();}
  __forceinline const Matrix4 &GetTransform() const {return obj->FutureVisualState().Transform();}

  RStringB name; // original proxy name
  //Matrix4 transform,invTransform;
  Matrix4 invTransform;
  // color/state identical to parent object
  int id;
  /// source selection index
  int selection;
  USE_FAST_ALLOCATOR

  ProxyObjectTyped();
  void SerializeBin(SerializeBinStream &f);
};
#include <Es/Memory/debugNew.hpp>

struct ThingEffectItem
{
  RStringB _type;
  float _probab;

  ThingEffectItem(RStringB type, float probab)
    :_type(type),_probab(probab)
  {}
  ThingEffectItem():_probab(-1){}
};

TypeIsMovable(ThingEffectItem);

/// identifier of type
struct EntityTypeName
{
  RString _className;
  RString _shapeName;
};

#include <El/ParamFile/paramFile.hpp>

#define ENT_PLACEMENT_ENUM(type,prefix,XX) \
  XX(type, prefix, Default) \
  XX(type, prefix, Vertical) \
  XX(type, prefix, Slope)

#ifndef DECL_ENUM_ENTITY_PLACEMENT
#define DECL_ENUM_ENTITY_PLACEMENT
DECL_ENUM(EntityPlacement)
#endif
DECLARE_ENUM(EntityPlacement,Placement,ENT_PLACEMENT_ENUM)

class ObjectDestroyAnim;

class FSMEntity;

/// Delegate for member function called from FSMEntity
class FSMEntityConditionFunc
{
public:
  virtual float operator () (const Entity *type, const FSMEntity *fsm, const float *params, int paramsCount) const = 0;
  virtual bool operator !=(float (Entity::*member)(const FSMEntity *fsm, const float *, int) const) const = 0;
};

/// creating function for FSMEntityConditionFunc delegate
template <class Type>
FSMEntityConditionFunc *CreateEntityConditionFunc(float (Type::*member)(const FSMEntity *fsm, const float *, int) const)
{
  return new ConstMembFunc3<Type,float,const FSMEntity *,const float *,int,Entity,FSMEntityConditionFunc>(member);
};

/// Delegate for member function called from FSMEntity
class FSMEntityActionFunc
{
public:
  virtual void operator () (Entity *type, FSMEntity *fsm, const float *params, int paramsCount) = 0;
};

/// creating function for FSMEntityActionFunc delegate
template <class Type>
FSMEntityActionFunc *CreateEntityActionFunc(void (Type::*member)(FSMEntity *fsm, const float *, int))
{
  return new MembFunc3<Type,void,FSMEntity *,const float *,int,Entity,FSMEntityActionFunc>(member);
};

/// Mapping identifiers to FSMEntityConditionFunc
typedef FSMEntityConditionFunc *MapNameToConditionFunc(RString name);
/// Mapping identifiers to FSMEntityActionFunc
typedef FSMEntityActionFunc *MapNameToActionFunc(RString name);

// Entity FSM factory
#define DECLARE_ENTITY_FSM_CONDITION(fsmName, type, function) \
  float function(const FSMEntity *fsm, const float *params, int paramsCount) const;

#define REGISTER_ENTITY_FSM_CONDITION(fsmName, type, function) \
  if (stricmp(name, #fsmName) == 0) \
    return ::CreateEntityConditionFunc(&type::function);

#define DECLARE_ENTITY_FSM_ACTION(fsmName, type, function) \
  void function##In(FSMEntity *fsm, const float *params, int paramsCount); \
  void function##Out(FSMEntity *fsm, const float *params, int paramsCount);

#define REGISTER_ENTITY_FSM_ACTION_IN(fsmName, type, function) \
  if (stricmp(name, #fsmName) == 0) \
  return ::CreateEntityActionFunc(&type::function##In);

#define REGISTER_ENTITY_FSM_ACTION_OUT(fsmName, type, function) \
  if (stricmp(name, #fsmName) == 0) \
  return ::CreateEntityActionFunc(&type::function##Out);

class FSMEntityType;
class FSMEntity;
#ifndef DECL_ENUM_INVENTORY_CONTAINER_ID
#define DECL_ENUM_INVENTORY_CONTAINER_ID
DECL_ENUM(InventoryContainerId)
#endif
#ifndef DECL_ENUM_INVENTORY_SLOT_ID
#define DECL_ENUM_INVENTORY_SLOT_ID
DECL_ENUM(InventorySlotId)
#endif

class ProxiesHolder
{
protected:
  Buffer< RefArray<ProxyObjectTyped> > _proxies;

public:
  void Init(LODShape *shape);
  void Deinit(LODShape *shape);
  void InitLevel(LODShape *shape, int level);
  void DeinitLevel(LODShape *shape, int level);

  int GetProxyCountForLevel(LODShape *lodShape, int level) const;
  const ProxyObjectTyped &GetProxyForLevel(LODShape *lodShape, int level, int i) const;
  SkeletonIndex GetProxyBoneForLevel(LODShape *lodShape, int level, int i) const;
  int GetProxySectionForLevel(LODShape *lodShape, int level, int i) const;
};

/// represent a hitpoint (functional damageable part, corresponds to a selection in the hitpoints LOD)
class HitPoint
{
  RString _name; //!< name of the config class where HitPoint is defined, used as an unique id
  int _selection; //!< selection index (in Hitpoints LOD)
  float _armor; //!< hit point armor
  float _invArmor; //!< inverse hit point armor
  float _passThrough; //!< how much of the hit will this part pass
  float _minHit; //!< minimal hit allowed
  // to further processing
  int _material; // material index
  FindArray<int> _indexCC; // index of corresponding convex component
  /// visual representation of the damage, applied on the same selection as the hitpoint
  WoundTextureSelections _visual;
  /// formulate which other hit-points (or total damage) this depends on
  ExpressionCode _depends;
  /// parameters of effects used during destruction
  AutoArray<DestructionEffectType> _destructionEffects;

public:
  HitPoint();
  HitPoint(LODShape *lodShape, const Shape *shape, const char *name, const char *altName, float armor, int material, Ref<WoundInfo> wound);
  HitPoint(LODShape *lodShape, const Shape *shape, ParamEntryPar par, float armor, Ref<WoundInfo> wound);

  RStringVal GetName() const {return _name;}

  void ApplyVisual(AnimationContext &animContext, LODShape *shape, int level, int woundLevel, float dist2) const
  {
    _visual.Apply(animContext, shape, level, woundLevel, dist2);
  }
  void InitLevelVisual(LODShape *shape, int level)
  {
    _visual.InitLevel(shape,level);
  }
  void DeinitLevelVisual(LODShape *shape, int level)
  {
    _visual.DeinitLevel(shape,level);
  }

  void CompileDepends(RString expression, const Array<HitPoint> &hitpoints);
  float EvaluateDepends(float totalDamage, const Array<float> &hits) const;
  
  /// load additional parameters (dependencies, destruction effects)
  void LoadExtPars(const Array<HitPoint> &hitpoints, ParamEntryPar par);
  
  Entity *CreateDestructionEffects(Entity *parent, EntityAI *killer, bool doDamage) const;
  
  __forceinline int GetSelection() const {return _selection;}
  __forceinline float GetArmor() const {return _armor;}
  __forceinline float GetInvArmor() const {return _invArmor;}
  __forceinline float GetPassThrough() const {return _passThrough;}
  __forceinline float GetMinHit() const {return _minHit;}
  __forceinline int GetMaterial() const {return _material;}
  __forceinline bool IsConnectedCC(int i) const {return _indexCC.Find(i)>=0;}
  ClassIsMovable(HitPoint);
};

typedef AutoArray<HitPoint> HitPointList;

enum AssembleType
{
  PrimaryPart,
  SecondaryPart,
  Ammo,
  Assembled,
  Fixed
};

struct AssembleInfo
{
  AssembleType type;
  bool primaryPart;
  RString base;
  RString assembleTo; 
  RString displayName;
  RString dissasembleTo[2];
};


class EntityType;

class EntityVisualState : public ObjectVisualState
{
  typedef ObjectVisualState base;
  friend class Entity;

public:
  EntityVisualState(EntityType const& type);
  virtual void Interpolate(Object *obj, ObjectVisualState const& t1state, float t, ObjectVisualState& interpolatedResult) const;
  virtual void Copy(ObjectVisualState const& src) {*this=static_cast_checked<const EntityVisualState &>(src);}
  virtual EntityVisualState *Clone() const {return new EntityVisualState(*this);}

  // Time difference between this state and the next (older) state. Used in Entity::CreateCurrentVisualState().
public:
  float _deltaT;

  // Ugly hack: these are used SO OFTEN that I'll better make them public.
public:  
  Vector3 _speed; // speed in world coordinates
  Vector3 _modelSpeed; // speed in model coordinates (updated in Move())
  Vector3 _acceleration;

  Vector3Val Speed() const { return _speed; }
  void SetSpeed(Vector3Par speed) { _speed = speed; DirectionWorldToModel(_modelSpeed, speed); }
  Vector3Val ModelSpeed() const { return _modelSpeed; } // camera needs to know object speed
  Vector3Val Acceleration() const { return _acceleration; }
  Vector3 GetAcceleration() const { return _acceleration; }

  AutoArray<AnimationInstance> _animationStates;

  // Cache the world-->model transformation (inverse of Frame).
protected:
  mutable bool _invDirty;
  mutable Matrix4 _invTransform;

public:
  void CalculateInv() const;
  void InvDirty() const;
  const Matrix4& InvTransform() const;

  // override FrameBase functions - invalidate _invTransform cache on changes
  virtual Matrix4 GetInvTransform() const override;

  virtual void SetPosition(Vector3Par pos) override;
  virtual void SetTransform(const Matrix4 &transform) override;
  virtual void SetTransformPossiblySkewed(const Matrix4 &transform) override;

  virtual void SetOrient(const Matrix3 &dir, const char* name = NULL) override;
  virtual void SetOrient(Vector3Par dir, Vector3Par up) override;
  virtual void SetOrientScaleOnly( float scale) override;

  // new inverse transformations
  const Matrix4& WorldToModel() const { return InvTransform(); }
  const Matrix3& DirWorldToModel() const { return InvTransform().Orientation(); }

  Vector3 PositionWorldToModel(Vector3Par v) const {return Vector3(VFastTransform, InvTransform(), v);}
  Vector3 DirectionWorldToModel(Vector3Par v) const {return Vector3(VRotate, InvTransform(), v);}

  void PositionWorldToModel(Vector3 &res, Vector3Par v) const {res.SetFastTransform(InvTransform(), v);}
  void DirectionWorldToModel(Vector3 &res, Vector3Par v) const {res.SetRotate(InvTransform(), v);}
};


//! Information shared between Entity object of the same type
class EntityType:
  public TaskEnabledRemoveLLinks, public LODShapeLoadHandler,
  public AnimatedType, public LinkBidirWithFrameStore
{
  friend class Entity;
protected:
  mutable int _refVehicles; // count vehicles of this type
  mutable Ref<LODShape> _shape;
  mutable LLink<LODShape> _shapeWithHandler;
  mutable Ref<LODShape> _shapeDecal;
  
  EntityTypeName _name;
  /// resolved shape name - may be from _name._className entry or _name._shapeName
  RString _shapeName;

  public: // tired of writing access functions
  ConstParamClassDeepPtr _par;
  Ref<EntityType> _parentType; // for uncertain determination
  float _accuracy;
  
  bool _shapeReversed;
  bool _shadowEnabled;
  bool _shapeAutoCentered;
  bool _useRoadwayForVehicles;
  bool _alwaysShow;

  SizedEnum<EAnimationType,char> _shapeAnimated;
  SizedEnum<EntityPlacement,char> _placement;

  //new parameter MapUseRealSize to use real object size instead of mapsize property
  //bool _mapUseRealSize;

  //!{ TI properties // _VBS3_TI
  float _htMin;
  float _htMax;
  float _afMax;
  float _mfMax;
  float _mFact;
  float _tBody;
  bool _loadedFromCfg;
  //!}

  RString _simName;

  // some types are created without vehicle
  // and cannot be used as full type info
  int _scopeLevel;

  AutoArray<AnimateTextureInfo> _animateTextures;
  /// source values for _animations
  AnimationSourceHolder _animSources;
  
  /// some objects can use destruction animation
  RefR<ObjectDestroyAnim> _destroyAnim;

  /// parameters of effects used during destruction
  AutoArray<DestructionEffectType> _destructionEffects;

  float _structuralDamageCoef;
  /// features can be visible from far away. 0 = not a feature, >0 feature size in m
  float _featureSize;

  /// texture replacement info shared across hit-points
  Ref<WoundInfo> _damageInfo;

  HitPointList _hitPoints;

  /** currently we support supersonic crack for ammo only, but any entity might want it */
  SoundPars _supersonicCrackNear;
  SoundPars _supersonicCrackFar;
  
  /// _destructionEffects will be used
  bool _destructionEffectsValid;
  
  /// container for proxy objects
  ProxiesHolder _proxies;

  /// index of selection used to calculate bounding sphere for the vehicle
  int _bounding;

  /// type of FSM controlling behavior (used for ambient life)
  AutoArray<Ref<FSMEntityType> > _fsm;

  //texture UVs for hitzone indicator
  AutoArray<int> _hitZoneTextureUV;

public:
  /// implicit shape - based on config
  explicit EntityType(ParamEntryPar param);
  /// explicit unnamed shape - no shape streaming possible
  explicit EntityType(LODShape *shape);
  /// explicit shape (named, streamed)
  explicit EntityType(const char *shapeName);
  ~EntityType();

  
  void Init(ParamEntryPar param, const char *shapeName) {Load(param,shapeName);}
  
  void LoadInternal(const char *simName, const char *shapeName);
  virtual void Load(ParamEntryPar par, const char *shapeName);


  // Create the corresponding object.
  // Expected to be called only from NewVehicle-like functions.
  virtual Object* CreateObject(bool fullcreate) const;
  // Create the corresponding visual state. Called from the ctor of ObjectTyped. ObjectVisualState (which corresponds to no type) is handled in the ctor of Object.
  virtual EntityVisualState* CreateVisualState() const { return new EntityVisualState(*this); }

  __forceinline float GetStructuralDamageCoef() const {return _structuralDamageCoef;}

  __forceinline const HitPointList &GetHitPoints() const {return _hitPoints;}
  __forceinline HitPointList &GetHitPoints() {return _hitPoints;}
  
  /// add the hitpoint, return his index in the array of hitpoints (index can be used as an unique identifier)
  int AddHitPoint(ParamEntryPar par, float armor);
  int AddHitPoint(const char *name, float armor, int material);

  int FindHitPoint(const char *name) const;
  void CompactHitpoints() {_hitPoints.Compact();}
  
  virtual float GetEntityArmor() const {return 1;}

  /// max. orientation correction speed for remote units
  virtual float GetMaxRemoteAngleCorrection() const;

  LODShape *GetShape() const {return _shape;}
  RString GetShapeName() const {return _shapeName;}

  virtual bool IsPerson() const {return false;}

  virtual float GetFeatureSize(Vector3Par pos) const {return _featureSize;}

  virtual int GetProxyCountForLevel(int level) const;
  virtual const ProxyObjectTyped &GetProxyForLevel(int level, int i) const;
  virtual SkeletonIndex GetProxyBoneForLevel(int level, int i) const;
  virtual int GetProxySectionForLevel(int level, int i) const;

  virtual InventoryContainerId GetInventoryContainerId() const {return (InventoryContainerId)-1;}
  virtual InventorySlotId GetInventorySlot() const {return (InventorySlotId)-1;}

  float GetAccuracy() const {return _accuracy;}

  Ref<WoundInfo> GetDamageInfo() {return _damageInfo;};

  // check if given shape is already loaded, if not, request it
  bool IsReadyForVehicle() const;
  void VehicleAddRef() const; // vehicle created
  void VehicleRelease() const; // vehicle destroyed

  /// should be used only when proxies are part of type, not instance
  virtual bool PreloadProxies(int level, float dist2, float scale, bool preloadTextures) const;

  /// request lod level to be loaded
  virtual bool PreloadShapeLevel(FileRequestPriority prior, LODShape *shape, int level) const;
  
  /// request shape to be loaded
  bool PreloadShape(FileRequestPriority prior) const;
  /// request shape to be loaded, and get the shape if it is
  bool PreloadShape(FileRequestPriority prior, Ref<LODShape> &shape) const;

  /// request all data which are needed for existence
  bool PreloadData(FileRequestPriority prior, float distance) const;

  const ParamClass *GetParamEntry() const;

  virtual void InitShape(); //!< after shape is loaded
  virtual void DeinitShape(); //!< before shape is unloaded

  #if _DEBUG
  virtual void OnUsed() const;
  #endif
  virtual void OnUnused() const;
  virtual size_t GetMemoryControlled() const;

  /// wrapper - create animation source value for smooth user animation
  AnimationSource *CreateAnimationSourceUser(ParamEntryPar cfg);
  /// wrapper - create animation source value for direct user animation
  AnimationSource *CreateAnimationSourceDirect(const char *name);

  /// create source with additional arguments
  virtual AnimationSource *CreateAnimationSourcePar(const AnimationType *type, ParamEntryPar entry);
  /// various types may provide various animation source values
  virtual AnimationSource *CreateAnimationSource(const AnimationType *type, RStringB source);
  
  /// encapsulate access to animations
  const AnimationHolder &GetAnimations() const
  {
    // if there are aby animatiobs, they need to be defined in the LODShape
    if (_shape && _shape->GetAnimations())
      return *_shape->GetAnimations();
    return AnimationHolder::Empty();
  }

  /// mapping of identifiers to FSMEntity condition function
  static FSMEntityConditionFunc *CreateEntityConditionFunc(RString name);
  /// mapping of identifiers to FSMEntity action function
  static FSMEntityActionFunc *CreateEntityActionInFunc(RString name);
  /// mapping of identifiers to FSMEntity action function
  static FSMEntityActionFunc *CreateEntityActionOutFunc(RString name);
  /// return current mapping of identifiers to FSMEntity condition function
  virtual MapNameToConditionFunc *GetCreateEntityConditionFunc() const {return CreateEntityConditionFunc;}
  /// return current mapping of identifiers to FSMEntity action state-in function
  virtual MapNameToActionFunc *GetCreateEntityActionInFunc() const {return CreateEntityActionInFunc;}
  /// return current mapping of identifiers to FSMEntity action state-out function
  virtual MapNameToActionFunc *GetCreateEntityActionOutFunc() const {return CreateEntityActionOutFunc;}

  //@{ implementation of LODShapeLoadHandler
  virtual void ShapeLoaded(LODShape *shape, int level);
  virtual void ShapeUnloaded(LODShape *shape, int level);
  virtual void LODShapeLoaded(LODShape *shape);
  virtual void LODShapeUnloaded(LODShape *shape);
  virtual bool IsShapeReady(const LODShape *lodShape, int level, const Ref<ProxyObject> *proxies, int nProxies, bool requestIfNotReady);
  //@}
  
  virtual void InitShapeLevel(int level); //!< after particular LOD level is loaded
  virtual void DeinitShapeLevel(int level); // before particular LOD level is unloaded

  void AttachShape(LODShape *shape); // force some shape
  void ReloadShape(QIStream &f, const char *filename);

  RStringVal GetName() const {return _name._className;}
  
  const EntityTypeName &GetEntityTypeName() const {return _name;}

  /// check if we are of the given type (including being derived from it)
  bool IsKindOf(const EntityType *predecessor) const;
  /// check if we are of the given type (including being derived from it)
  /** Type need not be loaded - only name given */
  bool IsKindOf(const char *typeName) const;

  //! return true if object state needs to persist (no streaming)
  virtual bool IsPersistenceRequired() const;
  //! return true if simulation has to be executed (nontrivial)
  virtual bool IsSimulationRequired() const;
  //! return true if sounds have to be executed (nontrivial)
  virtual bool IsSoundRequired() const;

  virtual bool AbstractOnly() const {return true;}
  bool IsAbstract() const {return _scopeLevel <= 0;}
  bool IsShaped() const {return _scopeLevel >= 2;}

  //! some roadways (houses, guard towers) should be used for soldiers only
  bool GetUseRoadwayForVehicles() const {return _useRoadwayForVehicles;}

  // fake bullet samples
  virtual const SoundPars& FakeBulletSound(float prob) const
  {
    static SoundPars nilPars;
    return nilPars;
  }

protected:
  /// helper to be used in Load(), loads skeleton definition
  void SetHardwareAnimation(ParamEntryPar skeleton);
  /// called after InitShape to prepare the weights
  virtual void PrepareHardwareAnimation();
};


//! Information shared between Entity object of the same type
class EntityPlainType: public EntityType
{
  typedef EntityType base;

public:
  explicit EntityPlainType(const char *shapeName);
  explicit EntityPlainType(LODShape *shape);
  explicit EntityPlainType(ParamEntryPar param);
  ~EntityPlainType();

  virtual Object* CreateObject(bool unused) const;

  bool AbstractOnly() const {return false;}
};

/// entity which is plain, but contains no proxies
class EntityPlainTypeNoProxy: public EntityPlainType
{
  typedef EntityPlainType base;
public:
  explicit EntityPlainTypeNoProxy(ParamEntryPar param);
  int GetProxyCountForLevel(int level) const;
  const ProxyObjectTyped &GetProxyForLevel(int level, int i) const;
  SkeletonIndex GetProxyBoneForLevel(int level, int i) const;

  virtual Object* CreateObject(bool unused) const;
};

//! Internal types - no corresponding config exists
class EntityInternalType: public EntityType
{
  typedef EntityType base;
public:
  EntityInternalType();
  ~EntityInternalType();

  virtual Object* CreateObject(bool unused) const;

  bool AbstractOnly() const {return false;}
};


typedef EntityType VehicleNonAIType;


class ObjectTyped: public Object
{
  typedef Object base;

protected:
  Ref<EntityType> _type;
public:
  ObjectTyped(LODShape *shape, const EntityType *type, const CreateObjectId &id, bool hasSimulation);
  ~ObjectTyped();

  // @{ Visual state
public:
  friend class EntityVisualState;
  EntityVisualState typedef VisualState;

  virtual void CreateCurrentVisualState() { /* empty */ }

// same visual state as in Object used here
//   VisualState& FutureVisualState() { return static_cast<VisualState &>(base::FutureVisualState()); }
//   VisualState const& FutureVisualState() const { return static_cast_checked<VisualState const &>(base::FutureVisualState());}
//   VisualState const& CurrentVisualState() const { return static_cast_checked<VisualState const &>(base::CurrentVisualState());}
//   const VisualState& RenderVisualState() const{ return static_cast<const VisualState &>(base::RenderVisualState()); }
// 
//   ProtectedVisualState<const VisualState> RenderVisualStateScope(bool ignoreParent=false) const {return ProtectedVisualState<const VisualState>(base::RenderVisualStateScope(ignoreParent));}
//   ProtectedVisualState<VisualState> FutureVisualStateScope(bool ignoreParent=false) { return ProtectedVisualState<VisualState>(base::FutureVisualStateScope(ignoreParent)); }
//   ProtectedVisualState<const VisualState> FutureVisualStateScope(bool ignoreParent=false) const { return ProtectedVisualState<const VisualState>(base::FutureVisualStateScope(ignoreParent)); }
  // @}

  /// use GetNonAIType if you know it is typed, as it is not virtual
  virtual const EntityType *GetEntityType() const {return _type;}
  const EntityType *Type() const {return _type;}
  const EntityType *GetNonAIType() const {return _type;}
  const char *GetName() const {return _type->GetName();}

  /// get Type type corresponding to the object type (like EntityType for Entity)
  template <class Ent>
  struct GetTypeType
  {
    typedef typename std::remove_reference<decltype(*(std::declval<Ent>().Type()))>::type type;
  };

  virtual bool DrawProxiesNeeded(int level) const;
  //! Virtual access to list of all proxy objects
  virtual int GetProxyCount(int level) const;
  //! get one particular proxy object
  virtual Object *GetProxy(VisualStateAge age, LODShape *&shape, Object *&parentObject, int level, Matrix4 &transform, const Matrix4 &parentPos, int i) const;

  /// get proxy matrix including possible animation
  virtual Matrix4 GetProxyTransform(int level, int i, Matrix4Par proxyPos, Matrix4Val parent) const;
  
  virtual float GetFeatureSize() const;

  virtual bool NeedsPrepareProxiesForDrawing() const;
  /// draw all proxy objects using linear scene manager
  virtual void PrepareProxiesForDrawing(Object *rootObj, int rootLevel, const AnimationContext &animContext,
    const PositionRender &transform,int level, ClipFlags clipFlags, SortObject *so);
  bool PreloadProxies(int level, float dist2, bool preloadTextures=false) const;
  //! Calculate (or estimate) complexity of given proxy
  virtual int GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const;
  int GetComplexity(int level, const Matrix4 &pos) const;

  USE_CASTING(base)
};

#include <Es/Containers/bankArray.hpp>

/// EntityTypeName traits for VehicleTypeBank
struct VehicleTypeBankTraits: public DefBankTraits<EntityType>
{
  typedef EntityTypeName NameType;
  // default name comparison
  static int CompareNames(const EntityTypeName &n1, const EntityTypeName &n2) {return strcmpi(n1._className,n2._className) || strcmpi(n1._shapeName,n2._shapeName);}
  static EntityTypeName GetName(const EntityType *item) {return item->GetEntityTypeName();}
  static void ReuseCachedItem(EntityType *item);
  
  struct EntityMapHashTraits: public DefMapClassTraits< LLink<EntityType> >
  {
    //! key type
    typedef NameType KeyType;
    //! calculate hash value
    static unsigned int CalculateHashValue(KeyType key)
    {
      unsigned hashValue = CalculateStringHashValueCI(key._className);
      return CalculateStringHashValueCI(key._shapeName,hashValue);
    }
    //! compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
    static int CmpKey(KeyType k1, KeyType k2) {return CompareNames(k1,k2);}
    static const KeyType &GetKey(EntityType *item) {return item->GetEntityTypeName();}
  };

  /// replace default container
  typedef MapStringToClass<
    LLink<EntityType>, LLinkArray<EntityType>,
    EntityMapHashTraits
  > ContainerType;
};

struct VehicleTypeBankTraitsExt //: public DefBankTraitsExt<VehicleTypeBankTraits>
{
  typedef VehicleTypeBankTraits Traits;
  typedef Traits::Type Type;
  typedef Traits::NameType NameType;
  typedef Traits::ContainerType ContainerType;
  /// get a name
  static NameType GetName(const Type *item) {return item->GetEntityTypeName();}
  /// perform a search
  static Type *Find(const ContainerType &container, NameType name)
  {
    const LLink<Type> &item = container.Get(name);
    return container.IsNull(item) ? NULL : item;
  }
  /// add item
  static void AddItem(ContainerType &container, Type *item)
  {
    // adding NULL into the hashmap could cause a crash
    Assert(item);
    container.Add(item);
  }
  static bool DeleteItem(ContainerType &container, NameType name)
  {
    return container.Remove(name);
  }
  /// create an object based on the name
  EntityType *Create(const EntityTypeName &name);

  /// request config entry corresponding to the name, once found, return it back
  bool RequestEntry(const char *className);

  //@{ we want to avoid looking up some typical locations
  ConstParamClassPtr _cfgVehicles;
  ConstParamClassPtr _cfgAmmo;
  ConstParamClassPtr _cfgNonAIVehicles;
  ConstParamClassPtr _cfgWeapons;
  ConstParamClassPtr _cfgMagazines;
  
  void CacheCommonEntries();
  //@}
  
  VehicleTypeBankTraitsExt();
  ~VehicleTypeBankTraitsExt();
};

//! entity type manager
class VehicleTypeBank: public BankArrayCached<EntityType,VehicleTypeBankTraits,VehicleTypeBankTraitsExt>
{
  typedef BankArrayCached<EntityType,VehicleTypeBankTraits,VehicleTypeBankTraitsExt> base;

public:
  /// init before first use
  void Init();
  void Preload(); // preload types, no shapes
  void CleanUp(); // discard types that are not used now
  /// delete all content
  void Clear();
  //clears container
  void SimpleClear();

  VehicleTypeBank();
  ~VehicleTypeBank();

  /// request based on the name
  bool Request(const char *name);
  
  /// request based on the shape name
  bool RequestShapeDirect(const char *shape);
  
  EntityType *New(const RString &name, const RString &shapeName=RString());
  //! find class based on the shape name
  EntityType *FindShapeDirect( const char *shape, ShapeParameters pars=ShapeShadow ); 
  //! find any class with corresponding shape
  EntityType *FindShape( const char *shape ); 
  //! find any class with corresponding shape and simulation
  EntityType *FindShapeAndSimulation( const char *shape, const char *sim);

  /// check if plain shape (no config) is already loaded
  /** used as optimization, as this is very common case */
  bool CheckPlainShapeLoaded( const char *shape, ShapeParameters pars );
  //@{ BankArrayCached implementation
  virtual float Priority() const;
  virtual RString GetDebugName() const;
  //@}
};

extern VehicleTypeBank VehicleTypes;

DECL_ENUM(GroundType)

struct ContactPoint
{
  // generic contact point (common for landscape / objects)
  Vector3 pos; // world space position
  Vector3 dirOut; // and direction
  float under; // how much under surface
  GroundType type; // type of collision (water, solid...)
  Object *obj; // contact object (for transfering momentum)
};

TypeIsMovableZeroed(ContactPoint)

//! Point in which friction should be applied
/*!
Used in friction calculation, introduced in Thing class,
in future it should be used more.
See also functions Entity::ApplyForcesAndFriction and Entity::ConvertContactsToFrictions.
*/
struct FrictionPoint
{ 
  Vector3 pos; //!< world space coordinates
  Vector3 outDir; //!< world space out-of-solid direction
  //! friction coefficient, determines how much of total friction
  //! is distributed to this point
  float frictionCoef;
  Object *obj; // which object causes the friction
};
TypeIsSimpleZeroed(FrictionPoint);

typedef StaticArrayAuto<ContactPoint> ContactArray;
typedef StaticArrayAuto<FrictionPoint> FrictionArray;

#define CREATE_VEHICLE_MSG(MessageName, XX) \
  XX(MessageName, int, list, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Container, which manage this entity"), TRANSF) \
  XX(MessageName, RString, type, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Entity type"), TRANSF) \
  XX(MessageName, RString, shape, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Entity shape (model)"), TRANSF) \
  XX(MessageName, Vector3, position, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current position"), TRANSF) \
  XX(MessageName, Matrix3, orientation, NDTMatrix, Matrix3, NCTNone, DEFVALUE(Matrix3, M3Identity), DOC_MSG("Current orientation"), TRANSF) \
  XX(MessageName, RString, varName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of variable"), TRANSF) \
  XX(MessageName, int, idVehicle, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("ID in map of vehicles (used in waypoints and triggers)"), TRANSF) \
  XX(MessageName, OLink(Object), hierParent, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Parent in hierarchy"), TRANSF_REF)

// derived directly form NetworkObject, not Object !!!
DECLARE_NET_INDICES_EX(CreateVehicle, NetworkObject, CREATE_VEHICLE_MSG)

#define UPDATE_VEHICLE_MSG(MessageName, XX) \
  XX(MessageName, bool, simulationDisabled, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Is simulation disabled?"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, int, targetSide, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, TWest), DOC_MSG("Side"), TRANSF, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
  XX(MessageName, StaticArrayAuto<float>, animations, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Current state of users defined animations"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdateVehicle, UpdateObject, UPDATE_VEHICLE_MSG)

#define UPDATE_DAMMAGE_VEHICLE_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<float>, hit, NDTFloatArray, AutoArray<float>, NCTFloat0To1, DEFVALUEFLOATARRAY, DOC_MSG("Damage of parts of entity"), TRANSF_CONTENT, ET_ABS_DIF, ERR_COEF_STRUCTURE)

DECLARE_NET_INDICES_EX_ERR(UpdateDamageVehicle, UpdateDamageObject, UPDATE_DAMMAGE_VEHICLE_MSG)

#define UPDATE_POSITION_VEHICLE_MSG(MessageName, XX) \
  XX(MessageName, Vector3, position, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current position"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_NORMAL) \
  XX(MessageName, Matrix3, orientation, NDTMatrix, Matrix3, NCTMatrixOrientation, DEFVALUE(Matrix3, M3Identity), DOC_MSG("Current orientation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_NORMAL) \
  XX(MessageName, Vector3, speed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current speed"), TRANSF, ET_DER_DIF, ERR_COEF_VALUE_NORMAL) \
  XX(MessageName, Vector3, angVelocity, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current angular velocity"), TRANSF, ET_DER_DIF, ERR_COEF_VALUE_NORMAL) \
  XX(MessageName, int, posFlags, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Position transfer flags"), TRANSF, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdatePositionVehicle, NetworkObject, UPDATE_POSITION_VEHICLE_MSG)

//! current in-landscape state of the vehicle
#ifndef DECL_ENUM_MOVE_OUT_STATE
#define DECL_ENUM_MOVE_OUT_STATE
DECL_ENUM(MoveOutState)
#endif
DEFINE_ENUM_BEG(MoveOutState)
  MOIn, //! is present in the landscape
  MOMovingOut, //! should be moved out from the landscape, but is still in the normal list
  MOMovedOut //! is moved out from the landscape and is in the outVehicles list
DEFINE_ENUM_END(MoveOutState)

class Entity;

/// entity specific activity
class EntityActivity: public Activity
{
  typedef Activity base;
public:
  /// called regularly
  /** @return false means activity is terminated and should be deleted */
  virtual bool Simulate(Entity *entity, float deltaT, SimulationImportance prec) = 0;
  /// perform any additional rendering
  virtual void Draw(Entity *entity) = 0;
  /// check if given entity should be suspended
  virtual bool SuspendEntity(const Entity *entity) const {return false;}
  /// serialization function
  static EntityActivity *CreateObject(ParamArchive &ar);
};

/// information about a FSM state for any activity
template <class ActivityType>
struct ActivityState : public SerializeClass
{
  typedef void (ActivityType::*StateSimulation)(Entity *entity, float deltaT);
  typedef void (ActivityType::*StateDraw)(Entity *entity);
  
  /// called when state should be simulated - NULL means FSM has terminated
  StateSimulation _sim;
  /// called when state should be rendered
  StateDraw _draw;
  
  ActivityState():_sim(NULL),_draw(NULL){}
  ActivityState(StateSimulation sim, StateDraw draw):_sim(sim),_draw(draw){}

  // Serialization works only if you register the type (call RegisterType in an INIT_MODULE for every subtype)!
  LSError Serialize(ParamArchive &ar);
};

template <class ActivityType>
LSError ActivityState<ActivityType>::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving())
  {
    int func = -1;
    if (_sim)
    {
      for (int i=0; i<ActivityType::NStateSimulation(); i++)
      {
        if (_sim == ActivityType::GetStateSimulation(i))
        {
          func = i;
          break;
        }
      }
    }
    CHECK(ar.Serialize("simulation", func, 1))
    func = -1;
    if (_draw)
    {
      for (int i=0; i<ActivityType::NStateDraw(); i++)
      {
        if (_draw == ActivityType::GetStateDraw(i))
        {
          func = i;
          break;
        }
      }
    }
    CHECK(ar.Serialize("draw", func, 1))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    int func;
    _sim = NULL;
    _draw = NULL;
    CHECK(ar.Serialize("simulation", func, 1))
    if (func >= 0) _sim = ActivityType::GetStateSimulation(func);
    CHECK(ar.Serialize("draw", func, 1))
    if (func >= 0) _draw = ActivityType::GetStateDraw(func);
  }
  return LSOK;
}


#include <El/Rectangle/rectangle.hpp>

/// object lists around player should stay locked to avoid requesting them
class RectangleObjectLock
{
  // max. fields to be traversed in one step
  int _maxFields;
  // max. fields to be requested in one step
  int _maxRequests;
  /// what is already guaranteed to be locked
  GridRectangle _locked;
  /// what was requested
  GridRectangle _requested;
  
  /// track progress - avoid checking the areas which were already checked
  /**
  We traverse whole _requested rect, skip anything which is in locked and preloading the rest.
  What is "before" _lockPos is always locked
  */
  Position _lockPos;

  /// remove all temporary locks
  void ResetLockPos();
  
public:
  RectangleObjectLock(int maxFields=50, int maxRequests=5)
  :_locked(0,0,0,0),_requested(0,0,0,0),_maxFields(maxFields),_maxRequests(maxRequests)
  {}
  ~RectangleObjectLock() {ChangeRectangle(GridRectangle(0,0,0,0));}
  /// set a request to rectangle change
  void ChangeRectangle(const GridRectangle &rect);

  const GridRectangle &GetRectangle() const {return _requested;}
  
  /// check if given rectangle is locked by this lock
  bool CheckLocked(const GridRectangle &rect) const;
  /// need to be called regularly
  bool DoWork();

  /// count of fields already locked in the area
  int LeftToLock() const;
  /// total area of the size requested to be locked
  int AreaSize() const {return _requested.Area();}
  
  /// release a list
  class ReleaseList;
  /// release a list if it is "before" (in sense of ordering) a reference position
  class ReleaseListBefore;
};

class IAmbLifeArea;

// FSM functions factories

#define ENTITY_FSM_CONDITIONS(XX) \
  XX(true, Entity, FSMTrue) \
  XX(false, Entity, FSMFalse) \
  XX(const, Entity, FSMConst) \
  XX(fsmFinished, Entity, FSMFinished)

#define ENTITY_FSM_ACTIONS(XX) \
  XX(nothing, Entity, FSMNothing) \
  XX(createFSM, Entity, FSMCreateFSM) \
  XX(deleteFSM, Entity, FSMDeleteFSM)


struct AcclerationSample
{
  Vector3 speed;
  float dt;
};
TypeIsSimple(AcclerationSample);

/// store last known remote state for needs of "prediction" pilot
struct RemoteState
{
  Matrix3 orient;
  Vector3 pos;
  Vector3 speed;
  Vector3 angVelocity;
  Time time;
  /// remote position offset to simulated position at the time when the update was received
  Vector3 posOffset;
  Vector3 speedOffset;
#define ORIENT_PREDICT 1
#if ORIENT_PREDICT
  Matrix3 orientOffset;
#endif
};

// JNI scripting support
template <>
class JavaTypes<Vector3>
{
public:
  typedef Vector3 NativeType;
  typedef jobject JNIType;

  static RString GetJavaType() {return "Vector3";}
  static RString GetSignature() {return "Lcom/bistudio/JNIScripting/Vector3;";}
  
  static jweak _class;
  static jmethodID _constructor;
  static jfieldID _x;
  static jfieldID _y;
  static jfieldID _z;

  static jclass InitClass(JNIEnv *env)
  {
    // activate JVM FPU settings
    CHECK_FPU_JVM

    // check the cached class
    jclass result = (jclass)env->NewLocalRef(_class);
    if (result == NULL) 
    {
      // class is not valid, initialize and store
      result = env->FindClass("com/bistudio/JNIScripting/Vector3");
      _class = env->NewWeakGlobalRef(result);
      _constructor = env->GetMethodID(result, "<init>", "(FFF)V");
      _x = env->GetFieldID(result, "_x", "F");
      _y = env->GetFieldID(result, "_y", "F");
      _z = env->GetFieldID(result, "_z", "F");
    }
    return result;
  }

  static JNIType FromNative(JNIEnv *env, const NativeType &native)
  {
    // activate JVM FPU settings
    SCOPE_FPU_JVM

    jclass cls = InitClass(env);

    // create Java object
    return env->NewObject(cls, _constructor, native.X(), native.Y(), native.Z());
  }
  static NativeType ToNative(JNIEnv *env, JNIType jni)
  {
    if (jni == NULL) return VZero;

    // activate JVM FPU settings
    SCOPE_FPU_JVM

    InitClass(env);

    float x = env->GetFloatField(jni, _x);
    float y = env->GetFloatField(jni, _y);
    float z = env->GetFloatField(jni, _z);
    return Vector3(x, y, z);
  }
};
template <>
class JavaTypes<Entity>
{
public:
  typedef Entity *NativeType;
  typedef jobject JNIType;

  static RString GetJavaType() {return "Entity";}
  static RString GetSignature() {return "Lcom/bistudio/JNIScripting/Entity;";}

  static jweak _class;
  static jmethodID _constructor;
  static jfieldID _jID;

  static jclass InitClass(JNIEnv *env)
  {
    // activate JVM FPU settings
    CHECK_FPU_JVM

    // check the cached class
    jclass result = (jclass)env->NewLocalRef(_class);
    if (result == NULL) 
    {
      // class is not valid, initialize and store
      result = env->FindClass("com/bistudio/JNIScripting/Entity");
      _class = env->NewWeakGlobalRef(result);
      _constructor = env->GetMethodID(result, "<init>", "()V");
      _jID = env->GetFieldID(result, "_jID", "I");
    }
    return result;
  }
  static jobject FromNative(JNIEnv *env, const Entity *native);
  static Entity *ToNative(JNIEnv *env, jobject jni);
};

class EntityAIFull;
DECL_ENUM(VehicleListType)

/// interface for functor passed to Transport::ForEachTurret
class ITurretFunc
{
public:
  virtual bool operator () (EntityAIFull *entity, TurretContext &context) = 0;
};

//! any object that needs simulation
/*!
  Principal type for any simulated or active objects.
*/

class Entity: public ObjectTyped, public Animated
{
  typedef ObjectTyped base;
  friend class EntityVisualState;
  friend class AnimationSourceMemberFuncVisualState;

protected:
  // @{ Visual state
  /// how much in the future is this Entity simulated
  float _timeOffset;
  /// max. allowed time offset (used for "light" interpolation)
  float _maxTimeOffset;
  /// remember history of time deltas so that we can smooth it as needed
  /** used for interpolation "light" */
  AutoArray<float> _deltaHistory;

  RefArray<ObjectVisualState> _visualStateHistory;  // _visualStateHistory.Last() is the newest visual state.

  /// the interpolated visual state corresponding to the present
  Ref<ObjectVisualState> _currentVisualState;

  SRef<RemoteState> _remoteState;

  /// a discreete position update required
  bool _posCut;

  Matrix4 GetTransformFromRemoteState(Matrix3Par orient, Vector3Par pos, const VisualState &vs) const;

  // Frame ID of the newest visual state.
  mutable int _frameID;

  static CriticalSection _currentVisualStateCS;

  VisualState const& CurrentVisualState() const
  {
    if (_frameID != Glob.frameID) // double-checked lock to prevent taking CS when it is known to be not needed
    {
      // use helper to keep the most common path short and inlined
      CurrentVisualStateHelper(); 
    }
    return _currentVisualState->cast<Entity>();
  }

  void CurrentVisualStateHelper() const;

  /// max. time step for Simulate function
  float _simulationPrecision;
  /// max. time step for SimulateAI function
  float _simulationPrecisionAI;
  /// how much has been skipped for Simulate
  float _simulationSkipped;
  /// how much has been skipped for SimulateAI
  float _simulationAISkipped;
  //! last used precision - important for MP prediction
  SizedEnum<SimulationImportance> _prec;
  /// simulation needed in the nearest simulation step
  /** used to guarantee collision response */
  bool _simulationNeeded;
  /// cached result of SimulationReady
  mutable bool _simulationReadyResult;
//#if _ENABLE_EDITOR2
  bool _simulationDisabled;
//#endif
  /// is _simulationReadyResult recent
  /** note: this is often artificial set back when we know the result may have become invalid */
  mutable int _simulationReadyFromFrame;
  /// set for ambient entities which should be destroyed once leaving given area
  InitPtr<IAmbLifeArea> _ambient;

  /// locked objects area around the entity
  /** SRef might be more compact here? */ 
  mutable RectangleObjectLock _objLock;
  
  AutoArray<float> _hit; //!< HitPoints (local damage)

  /// each hitpoint may have a destruction effect associated with it
  AutoArray< OLink(Entity) > _hitEffects;

  /// FSM controlling behaviour (used for ambient life)
  AutoArray<SRef<FSMEntity> > _fsm;

  Color _constantColor;

  /// which list contains this vehicle?
  /**
  This field exists so that deleting from the list can be faster, done using the means of the Vehicle list itself.
  
  Note: rule that no object should know its parent is broken here.
  This seems unavoidable, as entities are often accessed though other means that through their parents.
  */
  InitPtr<EntityList> _list;

  Vector3 _angVelocity; // angular velocity (omega)
  Vector3 _angMomentum; // angular momentum (L)
  Matrix3 _invAngInertia; // inverse world-space inertia tensor (inv I)
  
  int _avgAccelIndex;
  AutoArray<AcclerationSample> _avgAccels;

  Vector3 _impulseForce; // propagated collision result (1 sec long)
  Vector3 _impulseTorque;

  /// diagnostics - draw all forces
  void AddForce(Vector3Par pos, Vector3Par force, Color color=HWhite);

  TargetSide _targetSide; // easy identification

  // body constants
  // mass and inverse body space inertia tensor are parts of shape
  // body quantities
  // position and orientation are parts of Object

  //!{ Auxiliary quantities
  //! Flag to determine the object is in contact with another object (and that f.i. compex simulation should be used)
  bool _objectContact;
  bool _landContact,_waterContact;
  bool _delete;  
  bool _local;  // local / remote in network game
  //!}
  /// scripting can hide any entity
  bool _invisible;


  SizedEnum<MoveOutState,char> _moveOutState; // moved someplace else
private:
  OLink(Entity) _hierParent; // parent in hierarchy - do not ask this member directly, use GetHierarchyParent instead

protected:
  Time _disableDamageUntil; // no damage when emerged from far sim

  NetworkId _networkId;

  // Java scripting ID
  mutable int _jID;

  AutoArray<float> _animateTexturesTimes; //!< Times for texture animations

  /// all activities being currently performed on given entity
  RefArray<EntityActivity> _activities;

  //@{ bounding sphere state - animations may change it
  float _boundingSphere;
  Vector3 _minmax[2];
  //@}

#if _ENABLE_CHEATS
  Time _lastGenericUpdate;
  Time _lastPositionUpdate;
  Time _lastDamageUpdate;
#endif

  // VBS3: setVehicleVarName now works for triggers
  RString _varName; //!< Name of scripting variable 

#if _ENABLE_ATTACHED_OBJECTS
  public:
  OLink(Entity) _attachedTo;        // to whom this is attached
  Vector3 _attachedOffset;          // offset based on object center or on memPoint if provided
  Matrix3 _attachedTrans;           // Transformation matrix for object, modified when setDir and setVectorUp are executed
  int _attachedMemPointIndex;       //index of memory point
  OLinkArray(Entity) _attachedObjects; //which entities are attached to this
  int _attachedToFlags;
  OLink(Entity) _disableCollisionWith;
#endif

public:
  Entity(LODShape *shape, const EntityType *type, const CreateObjectId &id);
  ~Entity();

  // VBS3: setVehicleVarName now works for triggers - made virtual
  //! Get name of object in script environment
  virtual RString GetVarName() const {return _varName;}

#if _VBS3 
  Vector3 _attachedToPos;
  Vector3 _attachedFromPos;
#endif

  void SetPosition() {}

  //!{ Access to TI properties // _VBS3_TI
  virtual float HTMin() const { return _type->_loadedFromCfg ? _type->_htMin : _shape->HTMin(); }
  virtual float HTMax() const { return _type->_loadedFromCfg ? _type->_htMax : _shape->HTMax(); }
  virtual float AFMax() const { return _type->_loadedFromCfg ? _type->_afMax : _shape->AFMax(); }
  virtual float MFMax() const { return _type->_loadedFromCfg ? _type->_mfMax : _shape->MFMax(); }
  virtual float MFact() const { return _type->_loadedFromCfg ? _type->_mFact : _shape->MFact(); }
  virtual float TBody() const { return _type->_loadedFromCfg ? _type->_tBody : _shape->TBody(); }
  virtual bool LoadedFromEntityCfg() const { return _type->_loadedFromCfg; }

  virtual void SetVarName(RString name) {}

  virtual const char *GetControllerScheme() const;

  /// entity is marked as ambient
  /** used also to handle ambient state initialization */
  virtual void SetAmbient(IAmbLifeArea *area, Vector3Par camPos){_ambient = area;}
  /// check whether entity is ambient
  bool IsAmbient() const {return _ambient != NULL;}
//#if _ENABLE_EDITOR2
  bool IsSimulationDisabled() const {return _simulationDisabled;}
  virtual void DisableSimulation(bool disable) {_simulationDisabled = disable;}
///#endif

#if _ENABLE_CHEATS
  Time GetLastGenericUpdate() const {return _lastGenericUpdate;}
  Time GetLastPositionUpdate() const {return _lastPositionUpdate;}
  Time GetLastDamageUpdate() const {return _lastDamageUpdate;}
#endif

  /// @{ Visual state
  EntityVisualState typedef VisualState;

protected:
  /// maximum time offset, considering relation to camera as well
  float ComputeMaxTimeOffset() const;
  /// Get the current interpolation coefficient to interpolate between _visualStateHistory[index] and [index+1].
  void GetCurrentInterpolationInfo(int& index, float& coefficient);

  bool IsVisualStateHistoryUsed() { return JumpySim>=0 && JumpySim<3; }

public:
  Landscape* GetWorld() const;

#if _ENABLE_PHYSICS
  void GetSkeletonMatrices(Matrix4* cpose, Matrix4* tpose);
  Matrix4Array PrepareRagdollMatrices();
	Matrix4Array PrepareRagdollMatricesT();
#endif

	SkeletonIndex GetBone(const char* name)const;

  /// Compute the current visual state. Delete old states that won't be used anymore.
  void CreateCurrentVisualState();

  VisualState *CloneFutureVisualState() const {return FutureVisualState().Clone();}

  /// Create a new visual state (a copy of the previously newest one). Use before Xxx::Simulate().
  void AgeVisualStates(float deltaT);

  float GetSimulationSkipped() const;
  virtual VisualStateAge GetRenderVisualStateAge() const {return 0;}
  virtual VisualStateAge GetFutureVisualStateAge() const;
  virtual const ObjectVisualState &GetVisualStateByAge(VisualStateAge age) const;
  virtual VisualStateAge GetVisualStateAge(const ObjectVisualState &vs) const;

  /// Create a new visual state after a non-interpolating cut (e.g. teleportation).
  void VisualCut();

  virtual const ObjectVisualState &RenderVisualStateRaw() const;

  VisualState& FutureVisualState() { return static_cast<VisualState &>(base::FutureVisualState()); }
  VisualState const& FutureVisualState() const { return static_cast_checked<VisualState const &>(base::FutureVisualState());}
  //VisualState const& CurrentVisualState() const { return static_cast_checked<VisualState const &>(base::CurrentVisualState());}
  const VisualState& RenderVisualState() const{ return static_cast<const VisualState &>(base::RenderVisualState()); }

  ProtectedVisualState<const VisualState> RenderVisualStateScope(bool ignoreParent=false) const {return ProtectedVisualState<const VisualState>(base::RenderVisualStateScope(ignoreParent));}
  ProtectedVisualState<VisualState> FutureVisualStateScope(bool ignoreParent=false) { return ProtectedVisualState<VisualState>(base::FutureVisualStateScope(ignoreParent)); }
  ProtectedVisualState<const VisualState> FutureVisualStateScope(bool ignoreParent=false) const { return ProtectedVisualState<const VisualState>(base::FutureVisualStateScope(ignoreParent)); }
  /// @}

  /// speed including impulses waiting for processing
  Vector3 SpeedWithImpulse() { return FutureVisualState().Speed() + _impulseForce * GetInvMass();}
  virtual void SetSpeed(Vector3Par speed) { FutureVisualState().SetSpeed(speed); } // camera needs to know object speed
  virtual void SetModelSpeed(Vector3Par speed) { FutureVisualState()._modelSpeed = speed; } // ModelSpeed is also updated in Move()
  Vector3Val ObjectSpeed() const { return FutureVisualState().Speed(); } // virtual member of Object

  Entity *GetHierarchyParent() const {return (_moveOutState == MOIn) ? NULL : _hierParent.GetLink();} // parent in hierarchy

  virtual bool ChildUpdateByParent(const Entity *obj) const;


protected:  
  // switch owner of the weapon already in GWorld->_outVehicles
  void SetHierarchyParent(Entity *parent);
  void OnHierParentChange();

public:
  // remove the inventory item from inventory / cargo / slot
  virtual bool RemoveOwnedItem(InventoryItem *item) {return false;}
  virtual bool AddOwnedItem(InventoryItem *item) {return false;}
  virtual bool CanRemoveOwnedItem(const InventoryItem *item) const {return true;} // only checks if explicitely forbidden
  virtual bool CanAddOwnedItem(const InventoryItem *item) const {return false;}
  // check if oldItem can be replaced by newItem, return the index of the item if yes, -1 if no
  virtual int CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const {return -1;}


  /// enumerate all turrets (here to have an effective way how to call it for a hierarchy parent)
  virtual bool ForEachTurret(ITurretFunc &func) const {return false;}

  virtual void PerformFF( FFEffects &effects );
  virtual void ResetFF();

  void SetConstantColor(ColorVal color) {_constantColor = color;}
  ColorVal GetConstantColor() const;
  
  // more functions
  void SetList(EntityList *list) {_list = list;}
  EntityList *GetList() const {return _list;}
  VehicleListType GetListType() const;

  // angular velocity (omega)
  Vector3Val AngVelocity() const {return _angVelocity;}
  void SetAngVelocity(Vector3Par val) {_angVelocity = val;}

//  Vector3Val Acceleration() const { return FutureVisualState().Acceleration(); }
  Vector3Val AngMomentum() const {return _angMomentum;}
  void SetAngMomentum(Vector3Par val) {_angMomentum = val;}

  NetworkId GetNetworkId() const;
  void SetNetworkId(NetworkId &id) {_networkId = id;}
  bool IsLocal() const {return _local;}
  void SetLocal(bool local=true) {_local = local;}

  ObjectDestroyAnim *GetDestroyAnimation() const;
  
  //@{ user animations access
  float GetAnimationPhase(EntityVisualState const& vs, RString animation) const;
  void SetAnimationPhase(RString animation, float phase);
  float GetAnimationInstancePhase(EntityVisualState const& vs, int index) const {return vs._animationStates[index].GetPhase();} 
  float GetAnimationInstancePhaseWanted(int index) const {return FutureVisualState()._animationStates[index].GetPhaseWanted();} 
  void SetAnimationInstancePhase(int index, float phase) {FutureVisualState()._animationStates[index].SetPhase(phase);}
  void SetAnimationInstancePhaseWanted(int index, float phase) {FutureVisualState()._animationStates[index].SetPhaseWanted(phase);}
  //@}
  
  /// create an animator for this entity
  void StartAnimator();
  /// if necessary, create an animator for this entity
  virtual void OnAnimationStarted();
  /// called when animator terminated (all animations ran out)
  virtual void OnAnimatorTerminated();

  /// ammo trainer (cheat) detected - give opportunity to react
  virtual void AmmoChangeExpectedFailed(){}
  
public:
  virtual void Init(Matrix4Par pos, bool init);
  virtual void OnInitEvent() {}

  virtual bool CanFloat() const {return false;}


  virtual void PlaceOnSurface(Matrix4 &trans, bool underWater); // place in steady position on surface including geometries
  virtual void PlaceOnSurface(Matrix4 &trans) override // place in steady position on surface including geometries
  {
    PlaceOnSurface(trans, false);
  }
  virtual void PlaceOnSurfaceUnderWater(Matrix4 &trans) // place in steady position on surface including geometries
  {
    PlaceOnSurface(trans, true);
  }

  // helper for PlaceOnSurface functions  
  void PlaceOnSurfacePoint(Matrix4 &trans, Vector3Val surfPos, float dx, float dz);
  virtual Vector3 PlacingPoint() const;

  /// check how should be model placed so that it stays model
  Plane LandContactMinY(LODShape * shape) const;
  
  virtual SimulationImportance WorstImportance() const {return SimulateInvisibleFar;}
  virtual SimulationImportance BestImportance() const {return SimulateVisibleNear;}

  void SetLastImportance(SimulationImportance prec) {_prec = prec;}
  SimulationImportance GetLastImportance() const {return _prec.GetEnumValue();}

  SimulationImportance CalculateImportance(const Vector3 *viewerPos, int nViewers) const;
  bool EnableVisualEffects(SimulationImportance prec, float maxDist=FLT_MAX) const;

  /// allow custom damage overriding
  virtual float HandleDamage(int part, float val, EntityAI *owner, RString ammo, const SkinnedPosition &pos, bool &handlerCalled);
  
  void DisableDammageUntil(Time time) {_disableDamageUntil = time;}
  //void SwitchImportance( SimulationImportance importance ); // place in steady position

	virtual Visible VisibleStyle() const;
  virtual bool HasGeometry() const;
  virtual bool OcclusionFire() const;
  virtual bool OcclusionView() const;
  
  void SetInvisible(bool invisible) {_invisible=invisible;}
  
  /// check if this object acts as a wind emitter within a given range
  virtual bool EmittingWind(Vector3Par pos, float radius) const {return false;}

  /// perform wind emitting - modify a given global wind
  virtual Vector3 WindEmit(Vector3Par pos, Vector3Par wind) const {return wind;}

  /// check if the object can flatten some grass withing a given range
  virtual float IsGrassFlatenner(Vector3Par pos, float radius, Vector3 &flattenPos) const {return 0.0f;}

  /// perform grass flattening - return ratio (1=no flattening)
  virtual float FlattenGrass(float &skewX, float &skewZ, Vector3Par pos, float radius) const
  {
    skewX = 0.0f;
    skewZ = 0.0f;
    return 0.0f;
  }

  Matrix4 ApplySpeed(float deltaT) const;
  
  template <class VisualStateType>
  VisualStateType PredictPos(float deltaT) const
  {
    VisualStateType vs = static_cast_checked<const VisualStateType &>(FutureVisualState());
    vs.SetTransform(ApplySpeed(deltaT));
    return vs;
  }

  void SetAngVelocity(Vector3Val angVelocity, Matrix3Val orientation);

  void ApplyForces(float deltaT, Vector3Par force, Vector3Par torque, Vector3Par friction, Vector3Par torqueFriction, float staticFric=0.0f, bool aHist = false);
  void ApplyForcesAndFriction(float deltaT, Vector3Par force, Vector3Par torque, const FrictionPoint *fric, int nFric);

  /** Return speed at point. 
    * @param trans is transformation from entity coordinate system to result coordinate system.
    * @param point point in result coordinate system
    * @return speed in result coordinate system
    */
  Vector3 SpeedAtPoint(Matrix4Val trans, Vector3Par point) const;
  Vector3 SpeedAtPoint(Vector3Par cPoint) const;

  Vector3 DiffAccelerationAtPointOnForceAtPoint(Vector3Par cAccelPoint, Vector3Par cForce, Vector3Par cForcePoint) const;

  /// Returns acceleration in given point as a result of force at given point. trans is transformation of entity
  Vector3 DiffAccelerationOnForce(Matrix4Val trans, Vector3Par point, Vector3Par force) const;

  void ApplyImpulses();
  void ApplyImpulseAtPoint(Vector3Par cImpulse, Vector3Par cImpulsePoint);
  void ApplyImpulseNetAware(Vector3Par cImpulse, Vector3Par cImpulseTorque);
  void ApplyImpulse(Vector3Par cImpulse, Vector3Par cImpulseTorque);

  //! event handler - called in AddImpulse()
  virtual void OnAddImpulse(Vector3Par force, Vector3Par torque);

  void AddImpulse(Vector3Par force, Vector3Par torque);
  void AddImpulseNetAware(Vector3Par force, Vector3Par torque);

  virtual float Rigid() const {return 0.9f;} // reduce incoming impulse by this value
  virtual float RigidityCoef() const {return 0.1f;} // Rigidity coef used by marina

  void SetTargetSide(TargetSide side) {_targetSide = side;}
  TargetSide GetTargetSide() const {return _targetSide;}

  virtual bool IsMarkedToDelete() const {return ToDelete();}
  /** we do not want ToDelete to be virtual - causes serious performance hit */
  bool ToDelete() const {return _delete;}
  bool ToMoveOut() const {return _moveOutState.GetEnumValue() >= MOMovingOut;}
  bool IsMoveOutInProgress() const {return _moveOutState == MOMovingOut;}
  void CancelMoveOutInProgress();

  virtual void OnSetDelete();

  /// mark for deletion, but do not call a handler
  void SetDeleteRaw();
  /// set delete, call corresponding handler
  void SetDelete();
  void SetMoveOut(Entity *parent);
  void SetMoveOutDone(Entity *parent);
  void SetMoveOutFlag();
  void MoveOut(Entity *parent);

  /// mark for later processing (see EntityList::_toMoveOutOrDelete)
  void MarkForMoveOutAndDeleteProcessing();
  
  // access to world space transformation
  virtual Matrix4 WorldTransform(ObjectVisualState const& vs) const;
  virtual Matrix4 WorldInvTransform(ObjectVisualState const& vs) const;
  virtual Vector3 WorldPosition(ObjectVisualState const& vs) const;
  virtual Vector3 WorldSpeed() const;
  virtual bool IsInLandscape() const;

  //! Accept a valid position update even when this object is not in the landscape yet.
  /*!
    Used by items that can be moved between an inventory and the world. A
    dropped item may receive the position update before the update which adds
    it to the landscape, and the transform must not be lost in that case.
  */
  virtual bool AcceptPositionOutsideLandscape() const {return false;}

  void ResetDelete() {_delete = false;}
  void ResetMoveOut();
  
  virtual bool EnabledCommanding() {return true;}

  //! Get damage state of given hit point (0 or 1)
  float GetHit(int index) const;
  //! Get damage state of given hitpoint (continuous) 
  float GetHitCont(int index) const; // used for indication
  //! Get damage state of given hitpoint (continuous, without total damage saturation) 
  float GetHitContRaw(int index) const;

  //! Set damage state of given hitpoint (continuous, without total damage saturation) 
  void SetHitContRaw(int index, float value);

  float GetMaxHitCont() const;

  void HitDestructionEffects(EntityAI *killer, int i);
  void ChangeHit(EntityAI *killer, int i, float newHit, bool showDamage=true);

  //! Used to notify entity it has been damaged
  //!and may need to update its state
  virtual void ReactToDamage();
  virtual void ShowDamage(int part, EntityAI *killer);

  //! Get damage state for vehicle state ingame-ui display
  virtual float GetHitForDisplay(int kind) const;
  virtual AutoArray<int> GetHitTextureUVForDisplay() const {return Type()->_hitZoneTextureUV;;};

  virtual float DirectLocalHit(DoDamageResult &result, int component, float val) override;
  virtual float LocalHit(DoDamageResult &result, Vector3Par pos, float val, float valRange ) override;
  virtual void ApplyDoDamageLocal(const DoDamageResult &result, EntityAI *owner, RString ammo, const SkinnedPosition &pos) override;

  void UpdateHitDependencies(EntityAI *owner);
  virtual void Repair(float ammount=1.0f);

  virtual void SetTotalDamageHandleDead(float damage, bool setHitpoints) override;

  /// terminate given activity
  void TerminateActivity(EntityActivity *act) {_activities.DeleteKey(act);}
  /// draw all activities
  void DrawActivities();
  /// perform simulation of all activities
  void SimulateActivities(float deltaT, SimulationImportance prec);
  /// check if any activity required suspending
  bool IsSuspendedByActivity() const;

  //@{ FSM functions
  void FSMNothingIn(FSMEntity *fsm, const float *params, int paramsCount) {}
  void FSMNothingOut(FSMEntity *fsm, const float *params, int paramsCount) {}
  void FSMCreateFSMIn(FSMEntity *fsm, const float *params, int paramsCount);
  void FSMDeleteFSMIn(FSMEntity *fsm, const float *params, int paramsCount);
  void FSMCreateFSMOut(FSMEntity *fsm, const float *params, int paramsCount) {}
  void FSMDeleteFSMOut(FSMEntity *fsm, const float *params, int paramsCount) {}

  float FSMTrue(const FSMEntity *fsm, const float *params, int paramsCount) const {return 1.0f;}
  float FSMFalse(const FSMEntity *fsm, const float *params, int paramsCount) const {return 0.0f;}
  float FSMConst(const FSMEntity *fsm, const float *params, int paramsCount) const {return (paramsCount > 0) ? params[0] : 0.0f;}
  float FSMFinished(const FSMEntity *fsm, const float *params, int paramsCount) const;

  virtual void FSMUnhandledAction(RString text);
  virtual float FSMUnhandledCondition(RString text) const;
  //@}

  virtual void ResetStatus();
  /// reset all user animations to default status
  void ResetAnimationStatus();

  // generic simulation helpers
  void ScanContactPoints(ContactArray &contacts, const ObjectVisualState &moveTrans, SimulationImportance prec, float above, bool ignoreObjects=false);
  void ConvertContactsToFrictions(const ContactArray &contacts, FrictionArray &frictions, const ObjectVisualState &moveTrans,
    Vector3 &offset, Vector3 &force, Vector3 &torque, float crash, float maxColSpeed2);

  /// start frame - used for motion blur (bullets)
  virtual void StartFrame() {}
  /// end frame started by StartFrame
  virtual void EndFrame() {}
  //! perform simulation
  virtual void Simulate(float deltaT, SimulationImportance prec);
  /// AI simulation - may be performed less often  
  virtual void SimulateAI(float deltaT, SimulationImportance prec);

  /// apply state into the future visual state
  void ApplyRemoteState(float deltaT);

  void ApplyRemoteState(float deltaT, VisualState &moveTrans);

  void ApplyRemoteStateAdjustSpeed(float deltaT, VisualState &moveTrans);

  /// some objects have no simulation and cannot therefore apply the remote state
  /** position changes (usually result of setPos/setDir need to be applied directly for them */
  virtual bool RemoteStateSupported() const {return false;}

  virtual void ResetRemoteState();
  virtual void SimulateSimple(float deltaT, SimulationImportance prec) {}
  /// returns true if object is touching ground
  virtual bool IsLandContact() {return _landContact;}
  virtual bool IsWaterContact() {return _waterContact;}

#if _ENABLE_ATTACHED_OBJECTS
  //! Attach object to object 
  virtual bool AttachTo(Object *obj, Vector3Par modelPos=VZero, int memIndex=-1, int flags=0);
  //! Detach an object
  virtual void Detach();
  //! get the object this is attached to
  Entity* GetAttachedTo() const {return _attachedTo;}
  //! get a root of the attachment chain
  Entity *GetAttachedToRoot()
  {
    Entity *veh = this;
    for(; Entity *next = veh->GetAttachedTo(); veh=next){}
    return veh;
  }
  //! Check if object are attached together
  virtual bool AreAttached(const Object *obj) const;
  //! Disable physics
  virtual bool CheckPredictionFrozen() const
  {
    return base::CheckPredictionFrozen();
  };
  //! Disable collision between this and spec. vehicle
  Entity *GetDisableCollisionWith() const{return _disableCollisionWith;}
  //! Allow collision between vehicles
  void SetDisableCollisionWith(Entity *with) {_disableCollisionWith = with;}

  bool IsAttached() const { return _attachedTo != NULL;}
  //! add attached to the list of attached objects
  void AddAttached(Entity *attached);
  //! remove attached from the list of attached objects
  void RemoveAttached(Entity *attached);
  //! recalculate the position in regards to the object it is attached to
  virtual void CalcAttachedPosition(float deltaT);
  //! access to the orientation of the attached object
  virtual Matrix3Par GetAttachMatrix() const {return _attachedTrans;}
  //! set the orientation of the attached object
  void SetAttachMatrix(Matrix3Par matrix) {_attachedTrans = matrix;}
  //! get attached offset
  Vector3Val GetAttachedOffset() const { return _attachedOffset; }
  //! set attached offset
  void SetAttachedOffset(Vector3Val offset) { _attachedOffset = offset; }
  //! process all attachments
  void MoveAttached(float deltaT);

  bool ContainsAttached(Entity *attached) const {return _attachedObjects.Find(attached)>=0;}
#endif // _ENABLE_ATTACHED_OBJECTS

  /// simulation of animations
  virtual bool SimulateAnimations(float deltaT, SimulationImportance prec);

  /// calculate bounding info depending on animation state
  void CalculateBoundingInfo();
  
  /// offset used for hiding (disappearing) bodies, wrecks, etc.
  /** This offset is used for network hiding compensation, to make sure
  transferred position does not include hiding. See Entity::TransferMsg()
  */
  virtual Vector3 HideOffset() const {return VZero;}

  /// clean-up before landscape release (object is preparing to destruction)
  virtual void CleanUp();

  /// clean-up before removed from the landscape
  virtual void CleanUpMoveOut();
  
  // entity is requested to terminate its existence smoothly, but quickly
  virtual void DisappearASAP(float time) {}
  /// check if simulation is ready (all data loaded)
  virtual bool SimulationReady(SimulationImportance prec) const;

  /// check for a status of a particular rectangle
  bool ObjRectangleReady(int xMin, int xMax, int zMin, int zMax) const;
  
  /// cache SimulationReady results
  bool SimulationReadyCached(SimulationImportance prec, int frameId) const
  {
    if (_simulationReadyFromFrame>=frameId)
    {
      Assert(_simulationReadyFromFrame==frameId);
      return _simulationReadyResult;
    }
    bool ret = SimulationReady(prec);
    _simulationReadyResult = ret;
    _simulationReadyFromFrame = frameId;
    return ret;
  }
  /// called once caller knows SimulationReady may have changed
  void SimulationReadyReset(int frameId) const
  {
    _simulationReadyFromFrame = frameId-1;
  }

  virtual void SimulationNeeded(float age){_simulationNeeded=true;}

  /// Simulate one step. Handle frame id change and visual state history.
  void SimulateStep(float step, SimulationImportance prec);

  /// similar to SimulateOptimized, but additional limit may be applied
  void SimulateOptimizedAdaptive(float deltaT, SimulationImportance prec, float step);

  //! simulate only when step is above given limit
  void SimulateOptimized(float deltaT, SimulationImportance prec);
  //! simulate what was left from SimulateOptimized
  void SimulateRest(float deltaT, SimulationImportance prec);
  //! call SimulateRest for entities with no interpolation, SimulateOptimized for all others
  void SimulateOptimizedOrRest(float deltaT, SimulationImportance prec);

  /// simulate given time, respect the required step
  void SimulateFastOptimized(float deltaT, SimulationImportance prec);
  /// simulate what was left from SimulateFastOptimized, always respect the required step
  void SimulateFastRest(float deltaT, SimulationImportance prec);

  void SimulateAIOptimized(float deltaT, SimulationImportance prec);
  void SimulateAIRest(float deltaT, SimulationImportance prec);
  
  /// simulate rest, but only when the entity is visible
  void SimulateRestWhenVisible( float deltaT, SimulationImportance prec );

  ///to synchronize with RotorLib simulation step
  virtual float StepSimulated(float step) {return step;}

  //! get required simulation granularity
  __forceinline float SimulationPrecision() const {return _simulationPrecision;}
  __forceinline float SimulationPrecisionAI() const {return _simulationPrecisionAI;}

  //! set required simulation granularity
  void SetTimeOffset(float val);
  void SetSimulationPrecision( float val );
  void SetSimulationPrecisionAI( float val );
  void SetSimulationPrecision( float val, float valAI );
  /// used to make sure various entities can be simulated in different frames
  void RandomizeSimulationTime();
  
  //! houses have slow animation step and need animator, other objects can be animated by own simulation
  virtual bool IsAnimatorRequired() const {return false;}
  
  //! perform sounds
  virtual void Sound( bool inside, float deltaT ){}
  //! stop any sounds
  virtual void UnloadSound(){}

  bool NeedsPrepareProxiesForDrawing() const;
  void PrepareProxiesForDrawing(Object *rootObj, int rootLevel, const AnimationContext &animContext,
    const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so);
  bool CanBeInstancedShadow(int level) const;
  virtual bool CanBeInstanced(int level, float distance2, const DrawParameters &dp) const;
  virtual AnimationStyle IsAnimated(int level) const;

	virtual float GetCollisionRadius(const ObjectVisualState &vs) const;
  virtual float ClippingInfo(const ObjectVisualState &vs, Vector3 *minMax, ClippingType clip) const;
  virtual void AnimatedBSphere(int level, Vector3 &bCenter, float &bRadius, Matrix4Par pos, float posMaxScale) const;

  virtual void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  virtual void Deanimate(int level, bool setEngineStuff);

  virtual bool PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level);
//	virtual void SetBoneMatrix(Matrix4 mat, const ObjectVisualState &vs, SkeletonIndex boneIndex, RString name);
  virtual void AnimateBoneMatrix(Matrix4 &mat, const ObjectVisualState &vs, int level, SkeletonIndex boneIndex) const;
  virtual Vector3 AnimatePoint( const ObjectVisualState &vs, int level, int index ) const;
  virtual bool IsPointAnimated(int level, int index) const;

  // animate point inside my proxy
  virtual Vector3 AnimateProxyPoint(const Object *proxy, Vector3Par modelPos);

  float GetTotalDamageCtrl(const ObjectVisualState &vs) const;
  
  virtual bool MustBeSaved() const;

  virtual LSError Serialize(ParamArchive &ar);
  static Entity *CreateObject(ParamArchive &ar);

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
  static Entity *CreateObject(NetworkMessageContext &ctx, std::function<Entity *(RString,RString)> create);
  static Entity *CreateObject(NetworkMessageContext &ctx);
  void DestroyObject();
  virtual TMError TransferMsg(NetworkMessageContext &ctx);
  virtual bool CheckSendUpdateToOwner(NetworkMessageClass cls) override;
  virtual bool CheckSendUpdateToServer(NetworkMessageClass cls) override;

  virtual float CalculateError(NetworkMessageContextWithError &ctx);
  virtual float CalculateErrorInitial(NetworkMessageClass cls);
  static float CalculateErrorInitialDamage(NetworkMessage *msg);
  virtual Time GetLastSimulationTime() const;

  virtual void CreateSubobjects() {}
  virtual void UpdateSubobjects() {}

  // easier InvTransform calculation: pass to EntityVisualState
  // TODO: shouldn't these be explicit?
  void CalculateInv() const { FutureVisualState().CalculateInv(); }
  void InvDirty() const { FutureVisualState().InvDirty(); }
  const Matrix4 &InvTransform() const { return FutureVisualState().InvTransform(); }

  // overload Object functions
  virtual void SetPosition(Vector3Par pos) { FutureVisualState().SetPosition(pos); }
  virtual void SetTransform(Matrix4 const& transform) { FutureVisualState().SetTransform(transform); }
  virtual void SetTransformPossiblySkewed(Matrix4 const& transform) { FutureVisualState().SetTransformPossiblySkewed(transform); }

  virtual void SetOrient(Matrix3 const& dir) { FutureVisualState().SetOrient(dir); }
  virtual void SetOrient(Vector3Par dir, Vector3Par up) { FutureVisualState().SetOrient(dir, up); }
  virtual void SetOrientScaleOnly(float scale) { FutureVisualState().SetOrientScaleOnly(scale); }
  
  // TODO: shouldn't these be explicit?
  Matrix4 const& WorldToModel() const { return FutureVisualState().WorldToModel(); }
  Matrix3 const& DirWorldToModel() const { return FutureVisualState().DirWorldToModel(); }

  Vector3 PositionWorldToModel(Vector3Par v) const { return FutureVisualState().PositionWorldToModel(v); }
  Vector3 DirectionWorldToModel(Vector3Par v) const { return FutureVisualState().DirectionWorldToModel(v); }

  void PositionWorldToModel(Vector3& res, Vector3Par v) const { FutureVisualState().PositionWorldToModel(res, v); }
  void DirectionWorldToModel(Vector3& res, Vector3Par v) const { FutureVisualState().DirectionWorldToModel(res, v); }


  virtual IExplosion *GetExplosionInterface() {return NULL;}

  INHERIT_SOFTLINK(Entity,base);
  USE_CASTING(base)

  // JNI scripting support
  int GetJID() const;

  Vector3 GetPosition() const {return FutureVisualState().Position();}

#define FUNCTIONS_ENTITY(Class, XX) \
  XX(Class, "returns the entity position", Vector3, NULL, GetPosition) \
  XX(Class, "sets the entity position", void, (void)NULL, SetPosition, Vector3)

  JNI_REGISTER_FUNCTIONS(Entity, NO_STATIC_FUNCTIONS, FUNCTIONS_ENTITY)
  JNI_EXPORT_FUNCTIONS(Entity, NO_STATIC_FUNCTIONS, FUNCTIONS_ENTITY)
};

/// check if visual effects make any sense in given context
bool EnableVisualEffects(Vector3Par effPos, SimulationImportance prec, float maxDist=FLT_MAX);

/// accumulator to avoid sending multiple impulses to one vehicle
class AddImpulseNetAwareAccumulator
{
  Vector3 _force;
  Vector3 _torque;
  Entity *_obj;
  
  // flush what has been accumulated so far
  void Flush()
  {
    if (_obj)
    {
      _obj->AddImpulseNetAware(_force,_torque);
      // reset could be done here, but values are not used anyway
    }
  }
    
public:
  AddImpulseNetAwareAccumulator():_obj(NULL){}
  ~AddImpulseNetAwareAccumulator(){Flush();}

  /// add another force/torque pair  
  void Add(Entity *obj, Vector3Par force, Vector3Par torque)
  {
    if (obj!=_obj)
    {
      // when object is changed, flush
      Flush();
      // start a new accumulation
      _force = force;
      _torque = torque;
      _obj = obj;
    }
    else
    {
      // accumulate
      _force += force;
      _torque += torque;
    }
  }
};

typedef Entity Vehicle;

typedef OLink(Vehicle) LinkVehicle;
typedef OLink(Entity) LinkEntity;

/// objects that need some processing between simulation and rendering
/* joined objects need to stay in sync */
class JoinedObject
{
  public:
  virtual void UpdatePosition() {}

//   JoinedObject();
//   ~JoinedObject();
};

class AttachedOnVehicle: public JoinedObject, virtual public Frame
{
protected:
  // lights can be relative to vehicle
  OLink(Object) _vehicle;
  Vector3 _pos,_dir; // relative to vehicle
  bool _isAttached;
public:
  AttachedOnVehicle(){}
  AttachedOnVehicle(Object *object, Vector3Par pos, Vector3Par dir);
  ~AttachedOnVehicle();

  Object *AttachedOn(){return _vehicle;}
  void Detach();
  void SetAttachedPos(Vector3Par pos, Vector3Par dir) {_pos=pos,_dir=dir;}
  virtual void UpdatePositionTo(Matrix4Par toWorld);
  virtual void UpdatePosition();
  Vector3 GetAttachedPosition() const {return _pos;}
  Vector3 GetAttachedDirection() const {return _dir;}

  LSError Serialize(ParamArchive &ar);
};

// general simulation functions
#define G_CONST 9.8066f

void Friction(float &speed, float friction, float accel, float deltaT);
void Friction(Vector3 &speed, Vector3Par friction, Vector3Par accel, float deltaT);

static inline float TowardZero(float value, float ignore)
{
  if (fabs(value)<ignore) return 0;
  else return value - fSign(value)*ignore;
}

// all sets of effect variables (declared below)
enum EVarSet
{
  EVarSet_Bullet,
  EVarSet_Exhaust,
  EVarSet_Dust,
  EVarSet_Water,
  EVarSet_Damage,
  EVarSet_Effect,
  EVarSet_Fire,
  EVarSet_SmokeShell,
  EVarSet_CounterMessure,
  EVarSet_COUNT
};

#define EVAR_SET_BULLET(XX) \
  XX(intensity) \
  XX(interval) \
  XX(fireIntensity) \
  XX(fireInterval) \
  XX(lifeTime) \
  XX(surfNormalX) \
  XX(surfNormalY) \
  XX(surfNormalZ) \
  XX(inSpeed) \
  XX(inDirX) \
  XX(inDirY) \
  XX(inDirZ) \
  XX(outSpeed) \
  XX(outDirX) \
  XX(outDirY) \
  XX(outDirZ) \

#define EVAR_SET_EXHAUST(XX) \
  XX(intensity) \
  XX(damage) \
  XX(positionX) \
  XX(positionY) \
  XX(positionZ) \
  XX(speedX) \
  XX(speedY) \
  XX(speedZ)

#define EVAR_SET_DUST(XX) \
  XX(density) \
  XX(dustColor) \
  XX(positionX) \
  XX(positionY) \
  XX(positionZ) \
  XX(speedX) \
  XX(speedY) \
  XX(speedZ)

#define EVAR_SET_WATER(XX) \
  XX(density) \
  XX(size) \
  XX(positionX) \
  XX(positionY) \
  XX(positionZ) \
  XX(speedX) \
  XX(speedY) \
  XX(speedZ)

#define EVAR_SET_DAMAGE(XX) \
  XX(damage) \
  XX(speed) \
  XX(positionX) \
  XX(positionY) \
  XX(positionZ)

#define EVAR_SET_EFFECT(XX) \
  XX(speed) \
  XX(density) \
  XX(positionX) \
  XX(positionY) \
  XX(positionZ)

#define EVAR_SET_FIRE(XX) \
  XX(burning) \
  XX(power)

#define EVAR_SET_SMOKESHELL(XX) \
  XX(colorR) \
  XX(colorG) \
  XX(colorB) \
  XX(colorA) 

#define EVAR_SET_COUNTER_MESSURE(XX) \
  XX(time)

// helpers
#define EVAR_NAME(X) #X,
#define EVAR_BULLET(X) EVarBullet_##X,
#define EVAR_EXHAUST(X) EVarExhaust_##X,
#define EVAR_DUST(X) EVarDust_##X,
#define EVAR_WATER(X) EVarWater_##X,
#define EVAR_DAMAGE(X) EVarDamage_##X,
#define EVAR_EFFECT(X) EVarEffect_##X,
#define EVAR_FIRE(X) EVarFire_##X,
#define EVAR_SMOKE_SHELL(X) EVarSmokeShell_##X,
#define EVAR_COUNTER_MESSURE(X) EVarCounterMessure_##X,

// symbolic EffectVariables indices (including counts)
enum EVarBulletEnum
{
  EVAR_SET_BULLET(EVAR_BULLET)
  EVarBullet_COUNT
};

enum EVarExhaustEnum
{
  EVAR_SET_EXHAUST(EVAR_EXHAUST)
  EVarExhaust_COUNT
};

enum EVarDustEnum
{
  EVAR_SET_DUST(EVAR_DUST)
  EVarDust_COUNT
};

enum EVarWaterEnum
{
  EVAR_SET_WATER(EVAR_WATER)
  EVarWater_COUNT
};

enum EVarDamageEnum
{
  EVAR_SET_DAMAGE(EVAR_DAMAGE)
  EVarDamage_COUNT
};

enum EVarEffectEnum
{
  EVAR_SET_EFFECT(EVAR_EFFECT)
  EVarEffect_COUNT
};

enum EVarFireEnum
{
  EVAR_SET_FIRE(EVAR_FIRE)
  EVarFire_COUNT
};
enum EVarSmokeShellEnum
{
  EVAR_SET_SMOKESHELL(EVAR_SMOKE_SHELL)
  EVarSmokeShell_COUNT
};

enum EVarCounterMessureEnum
{
  EVAR_SET_COUNTER_MESSURE(EVAR_COUNTER_MESSURE)
  EVarCounterMessure_COUNT
};

/// structure used for loading of effects parameters from config
class EffectVariables: private NoCopy
{
private:
  /// lists of all variable names
  static const int _count[EVarSet_COUNT];
  static const RString _namesBullet[EVarBullet_COUNT];
  static const RString _namesExhaust[EVarExhaust_COUNT];
  static const RString _namesDust[EVarDust_COUNT];
  static const RString _namesWater[EVarWater_COUNT];
  static const RString _namesDamage[EVarDamage_COUNT];
  static const RString _namesEffect[EVarEffect_COUNT];
  static const RString _namesFire[EVarFire_COUNT];
  static const RString _namesSmokeShell[EVarSmokeShell_COUNT];
  static const RString _namesCounterMessure[EVarCounterMessure_COUNT];
  static const RString *_names[EVarSet_COUNT];

  /// variable set used and corresponding values
  EVarSet _set;
  float *_values;

public:
  static int GetCount(EVarSet set);
  static const RString* GetNames(EVarSet set);
  static int FindName(RStringVal name, EVarSet set);

  EffectVariables(EVarSet set, float defval=0);
  // special constructor for EVarSet_Bullet
  EffectVariables(const struct HitInfo& hitInfo, float defval=0);
  ~EffectVariables();

  EVarSet GetSet() const { return _set; }
  int GetCount() const { return GetCount(_set); }
  float* GetValues() const { return _values; }

  float operator [](int index) const;
  float& operator [](int index);
  float operator [](RStringVal name) const { return operator [](FindName(name, _set)); }
  float& operator [](RStringVal name) { return operator [](FindName(name, _set)); }

  void UpdateVector(int startIndex, Vector3Par vec, bool splitSize=false);
  void UpdateBulletParameters(float intensity, float interval, float fireIntensity, float fireInterval, float lifeTime);

  float GetValue(ParamEntryPar entry) const;
  float GetValue(const IParamArrayValue &entry) const;
  void GetValue(Color &color, ParamEntryPar entry) const;
};

/// helper entity used to animate entities which do not have its own simulation
class EntityAnimator: public Entity
{
  typedef Entity base;

  OLinkO(Entity) _actor;

public:
  EntityAnimator(Entity *actor);
  ~EntityAnimator();

  void Simulate(float deltaT, SimulationImportance prec);
  LSError Serialize(ParamArchive &ar);
};

typedef const EffectVariables &EffectVariablesPar;

#endif

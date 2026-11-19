/*!
\file
Implementation for EntityAI class - general functions
*/

#include "wpch.hpp"

#if _PROFILE
# pragma optimize( "", off )
#endif

#include "dikCodes.h"
#include "transport.hpp"
#include "AI/ai.hpp"
#include "AI/aiRadio.hpp"
#include "world.hpp"
#include "landscape.hpp"
#ifdef _WIN32
  #include "joystick.hpp"
#endif
#include "shots.hpp"
#include "keyInput.hpp"
#include <El/Common/randomGen.hpp>
#include <Es/Strings/bString.hpp>
#include "camera.hpp"
#include "visibility.hpp"
#include "roads.hpp"
#include "thing.hpp"
#include "pathSteer.hpp"
#include "AI/operMap.hpp"
#include "txtPreload.hpp"
#include "diagModes.hpp"
#include "fileLocator.hpp"

#include "Shape/specLods.hpp"
#include "engine.hpp"
#include "integrity.hpp"

#include "tlVertex.hpp"
#include "stringtableExt.hpp"

#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <Es/ErrorProp/errorProp.hpp>
#include <El/Evaluator/express.hpp>

#include "Network/network.hpp"
#include "UI/uiActions.hpp"

#include <Es/Algorithms/qsort.hpp>

#include "move_actions.hpp"

#include "detector.hpp" // flag

#include <El/Enum/enumNames.hpp>

#include "gameStateExt.hpp"
#include "paramArchiveExt.hpp"

#include "entityEventHandler.hpp"

#include "mbcs.hpp"
#include "drawText.hpp"
#include "baseWorld/enf_shapes.h"
#include <lib/Inventory/clothing.hpp>
#include <lib/Inventory/itemoptics.hpp>

static const Color MarkerRedColor(1.0, 0.1, 0.1);
static const Color MarkerRedAmbient(0.1, 0.01, 0.01);
static const float MarkerRedBrightness = 0.002;

static const Color MarkerGreenColor(0.1, 1.0, 0.1);
static const Color MarkerGreenAmbient(0.01, 0.1, 0.01);
static const float MarkerGreenBrightness = 0.002;

static const Color MarkerBlueColor(0.1, 0.1, 1.0);
static const Color MarkerBlueAmbient(0.01, 0.01, 0.1);
static const float MarkerBlueBrightness = 0.002;

static const Color MarkerWhiteColor(1.0, 1.0, 1.0);
static const Color MarkerWhiteAmbient(0.1, 0.1, 0.1);
static const float MarkerWhiteBrightness = 0.001;

static BankArray<RecoilFunction> RecoilFunctions;

const float LostUnitMin = 150.0f;
const float LostUnitMax = 500.0f;

#pragma warning(disable:4355)

template <class T> void LoadLightCommonParams (T & t, EntityAIType & type, LODShape * shape, ParamEntryPar cls, float armor, Ref<WoundInfo> wound)
{
  t.color = GetColor(cls >> "color");
  t.colorAmbient = GetColor(cls >> "ambient");
	
	t.dayLight = cls.GetBoolDef("dayLight", false);
	t.heatHazeRadius = cls.GetFloatDef("heatHazeRadius", 0.0f);
	t.heatHazePower = cls.GetFloatDef("heatHazePower", 0.0f);

  t.size = cls >> "size";
  t.radius = cls >> "radius";
  const RString selName = cls >> "selection";
  t.selection.Init(shape, selName, NULL);
  const RString hitName = cls >> "hitpoint";
  t.hitPoint = type.AddHitPoint(hitName, armor, -1);

  t.positionIndex = -1;
  t.position = VZero; 
  const Shape * memory = shape->MemoryLevel();
  if (memory)
  {
    const RString posName = cls >> "position";
    t.positionIndex = shape->PointIndex(memory, posName);
    if (t.positionIndex >= 0)
      t.position = memory->Pos(t.positionIndex);
  }

  t.speed = -1;
  t.phaseLimit = 0.5;
  const ConstParamEntryPtr entry = cls.FindEntry("period");
  if (entry)
  {
    if (entry->IsArray())
    {
      DoAssert(entry->GetSize() == 2);
      const float on = (*entry)[0];
      const float off = (*entry)[1];
      const float period = on + off;
      if (period > 0)
      {
        t.speed = 1.0f / period;
        t.phaseLimit = on / period;
      }
    }
    else
    {
      const float period = *entry;
      if (period > 0)
        t.speed = 1.0f / period;
    }
  }

  t.fireEffect = false;
  if (const ConstParamEntryPtr entry = cls.FindEntry("fireEffect"))
  {
    t.fireEffect = cls >> "fireEffect";
    t.fireEffectOctaves = 8;
    t.fireEffectPersistence = 1.0f;
    t.fireEffectFract = 1 / 3.0f;
    if (const ConstParamEntryPtr oct = cls.FindEntry("fireEffectOctaves"))
      t.fireEffectOctaves = static_cast<int>(cls >> "fireEffectOctaves");
    if (const ConstParamEntryPtr per = cls.FindEntry("fireEffectPersistence"))
      t.fireEffectPersistence = cls >> "fireEffectPersistence";
    if (const ConstParamEntryPtr frc = cls.FindEntry("fireEffectFract"))
      t.fireEffectFract = cls >> "fireEffectFract";
  }
}

void ReflectorInfo::Load (EntityAIType &type, LODShape *shape, ParamEntryPar cls, float armor, Ref<WoundInfo> wound)
{
  LoadLightCommonParams(*this, type, shape, cls, armor, wound);

  directionIndex = -1;
  direction = VForward;
  const Shape * memory = shape->MemoryLevel();
  if (memory)
  {
    const RString name = cls >> "direction";
    directionIndex = shape->PointIndex(memory, name);

    if (directionIndex >= 0 && positionIndex >= 0)
    {
      direction = memory->Pos(directionIndex) - position;
      direction.Normalize();
    }
  }

  const ConstParamEntryPtr angle_entry = cls.FindEntry("angle");
  angle = angle_entry ? angle_entry->operator float() : 36.0f; 
	
	const ConstParamEntryPtr angratio_entry = cls.FindEntry("angleInnerRatio");
	angleInnerRatio = angratio_entry ? floatMinMax(angratio_entry->operator float(), 0.0f, 1.0f) : 0.25f; 
}

void PointLightInfo::Load (EntityAIType &type, LODShape *shape, ParamEntryPar cls, float armor, Ref<WoundInfo> wound)
{
  LoadLightCommonParams(*this, type, shape, cls, armor, wound);
}

void PlateInfo::Init(const Shape *shape, Offset face)
{
  //_face = face;
  const Poly &f = shape->Face(face);
  
  _center = VZero;
  for (int v=0; v<f.N(); v++ )
  {
    int vertex = f.GetVertex(v);
    _center += shape->Pos(vertex);
  }
  float invFN = 1.0f/f.N();
  _center *=invFN;
  Vector3 normal = f.GetNormal(shape->GetPosArray()); 
  _normal = normal.Normalized();
  // TODO: calculate actual size (based on max. distance of points)
  _size = sqrt(normal.Size())*0.4;
}

void PlateInfos::Init(LODShape *shape, ParamEntryPar cls, FontID font)
{
  _plates.Init(shape->NLevels());

  // TODO: create font indirection
  _font = GEngine->LoadFont(font);
  GetValue(_color, cls >> "color");
  _name = cls >> "name";
}

void PlateInfos::InitLevel(LODShape *shape, int level)
{
  ShapeUsed sShape = shape->Level(level);
  int sel = shape->FindNamedSel(sShape,_name);
  if (sel>=0)
  {
    ShapeUsedGeometryLock<> lock(shape,level);
    const FaceSelection &faces = sShape->NamedSel(sel).FaceOffsets(sShape.GetShape());
    Assert(faces.GetNeedsSections());
    // scan sections
    for (int s=0; s<faces.NSections(); s++)
    {
      const ShapeSection &ss = sShape->GetSection(faces.GetSection(s));
      //const Texture *tex = ss.GetTexture();
      int nFaces = 0;
      for (Offset off=ss.beg; off<ss.end; sShape->NextFace(off))
        nFaces++;
      _plates[level].Realloc(nFaces);
      _plates[level].Resize(nFaces);
      int f;
      Offset off;
      for (f=0,off=ss.beg; off<ss.end; sShape->NextFace(off),f++)
        _plates[level][f].Init(sShape, off);
    }
  }
}

void PlateInfos::Draw(int cb, int level, ClipFlags clipFlags, const PositionRender &pos, const RString &text) const
{
  // find position to draw at (based on _plate selection)
  for (int plate=0; plate<_plates[level].Size(); plate++)
  {
    const PlateInfo &pi = _plates[level][plate];

    // plate rendering is very inefficient
    // perform some culling to avoid rendering the plates too far
    
    // estimate size
    const Camera &camera = *GScene->GetCamera();
    Matrix4Val project=camera.Projection();

    float dist2 = pos.PositionWorld(camera).Distance2(camera.Position());

    // perspective screen size - based on Object::DrawDecal code
    float area = project(0,0)*-project(1,1)*camera.InvLeft()*camera.InvTop()*Square(pi._size);
    // note: similar to Camera::_aotFactor

    // area/dist2 should be approximate area of one letter in square pixels
    static float minArea = 40.0f;
    if( area>=minArea*dist2 )
    {
      // plates are never visible in 1st person view, we do not mind if we lose camSpace for them
      Matrix4 transWorld = pos.TransformWorld(camera);
      Vector3 center = transWorld.FastTransform(pi._center);
      Vector3 normal = transWorld.Rotate(pi._normal);
    
      Vector3 up = transWorld.DirectionUp();
      Vector3 aside = up.CrossProduct(normal).Normalized();
      up = normal.CrossProduct(aside).Normalized();
      
      /*
      // render axis diagnostics
      GEngine->DrawLine3D(center,center+aside,PackedColor(Color(1,0,0)),0);
      GEngine->DrawLine3D(center,center+up,PackedColor(Color(0,1,0)),0);
      GEngine->DrawLine3D(center,center-normal,PackedColor(Color(0,0,1)),0);
      */

      // when not caring about predication, we should be hopefully recorded in the part
      // which is played back in all predicated versions
      up *= pi._size;
      aside *= pi._size*0.8;
      center -= normal*0.01;
      GEngine->DrawText3DCenteredAsync(center, up, aside, clipFlags, _font, _color, 0, text);
    }
  }
}

/// animation of reloading weapon attached to vehicle
class AnimationSourceReload : public AnimationSource
{
  typedef AnimationSource base;
  
protected:
  Ref<WeaponType> _weapon;

public:  
  AnimationSourceReload(const RStringB &name, WeaponType *weapon) :base(name),_weapon(weapon) {}
  float GetPhase(const ObjectVisualState &vs, const Animated *obj) const;
  void SetPhaseWanted(Animated *obj, float phase) {}
};

/// animation of revolving weapon attached to vehicle
class AnimationSourceRevolving : public AnimationSource
{
  typedef AnimationSource base;
  
protected:
  Ref<WeaponType> _weapon;

public:  
  AnimationSourceRevolving(const RStringB &name, WeaponType *weapon) :base(name),_weapon(weapon) {}
  float GetPhase(const ObjectVisualState &vs, const Animated *obj) const;
  void SetPhaseWanted(Animated *obj, float phase) {}
};

/// animation of reloading magazine of weapon attached to vehicle
class AnimationSourceReloadMagazine : public AnimationSource
{
  typedef AnimationSource base;
  
protected:
  Ref<WeaponType> _weapon;

public:  
  AnimationSourceReloadMagazine(const RStringB &name, WeaponType *weapon) :base(name),_weapon(weapon) {}
  float GetPhase(const ObjectVisualState &vs, const Animated *obj) const;
  void SetPhaseWanted(Animated *obj, float phase) {}
};


class FindWeaponsFunc : public ITurretFunc
{
protected:
  const WeaponsState *&_weapons; // out
  const WeaponType *_weapon;

public:
  FindWeaponsFunc(const WeaponsState *&weapons, const WeaponType *weapon) : _weapons(weapons), _weapon(weapon) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    return context._weapons->ForEachWeapon([this, &context](Weapon *weapon) -> bool
    {
      if (weapon->Type() == _weapon)
      {
        // found
        _weapons = context._weapons;
        return true; // found
      }
      return false; // continue
    });
  }
};

float AnimationSourceReload::GetPhase(const ObjectVisualState &vs, const Animated *obj) const
{
  Assert(dynamic_cast<const EntityAIFull *>(obj));
  const EntityAIFull *entity = static_cast<const EntityAIFull *>(obj);

  // search for the weapon in all weapon systems
  const WeaponsState *weapons = NULL;
  FindWeaponsFunc func(weapons, _weapon);
  if (!entity->ForEachTurret(func))
    return 0;
  Assert(weapons);

  // TODO: non-active weapons can reload as well
  WeaponModePtr ptr = weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid())
    return 0;

  const WeaponType *weapon = ptr.GetWeaponType();
  if (weapon != _weapon) return 0;

  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return 0;
  float reload = magazine->_reload;
  saturate(reload, 0, 1);
  return reload;
}

float AnimationSourceRevolving::GetPhase(const ObjectVisualState &vs, const Animated *obj) const
{
  Assert(dynamic_cast<const EntityAIFull *>(obj));
  const EntityAIFull *entity = static_cast<const EntityAIFull *>(obj);

  // search for the weapon in all weapon systems
  const WeaponsState *weapons = NULL;
  FindWeaponsFunc func(weapons, _weapon);
  if (!entity->ForEachTurret(func))
    return 0;
  Assert(weapons);

  // TODO: non-active weapons can reload as well
  WeaponModePtr ptr = weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid())
    return 0;

  const WeaponType *weapon = ptr.GetWeaponType();
  if (weapon != _weapon) return 0;

  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return 0;
  const MagazineType *magazineType = magazine->Type();
  if (!magazineType) return 0;
  const WeaponModeType *mode = ptr.GetWeaponModeType();
  if (!mode) return 0;

  float reload = magazine->_reload;
  saturate(reload, 0, 1);
  const float rotPerBurst = 1.0f / magazineType->_maxAmmo;
  float bulletsPerBurst = mode->_mult;
  float rotPerBullet = bulletsPerBurst> 0 ? rotPerBurst / bulletsPerBurst : 0;
  int ammo = magazine->GetAmmo();
  float prevRot = rotPerBullet * (ammo + bulletsPerBurst);
  float currRot = rotPerBullet * (ammo);
  return fastFmod(currRot - (currRot - prevRot) * reload, 1);
}

float AnimationSourceReloadMagazine::GetPhase(const ObjectVisualState &vs, const Animated *obj) const
{
  Assert(dynamic_cast<const EntityAIFull *>(obj));
  const EntityAIFull *entity = static_cast<const EntityAIFull *>(obj);

  // search for the weapon in all weapon systems
  const WeaponsState *weapons = NULL;
  FindWeaponsFunc func(weapons, _weapon);
  if (!entity->ForEachTurret(func))
    return 0;
  Assert(weapons);

  // TODO: non-active weapons can reload as well
  WeaponModePtr ptr = weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return 0;

  const WeaponType *weapon = ptr.GetWeaponType();
  if (weapon != _weapon) return 0;
  
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return 0;
  float byTime = magazine->_reloadMagazineTotal>0 ? magazine->_reloadMagazine/magazine->_reloadMagazineTotal : 0;
  const Muzzle &muzzle = ptr.GetMuzzle();
  float byAnim = muzzle._reloadMagazineProgress;
  // combine both ways of animation
  return floatMin(byTime+byAnim,1);
}

void ObjectTypeLights::Init (EntityAIType *type, LODShape *shape, const ParamEntry &par, float objectArmor, Ref<WoundInfo> wound)
{
  // point lights first
  const bool ptlight_exists = par.CheckIfEntryExists("PointLights");
  if (ptlight_exists)
  {
    ParamEntryVal pointlights = par >> "PointLights";
    if (const int pl_n = pointlights.GetEntryCount())
    {
      float armor = objectArmor * (float)(par >> "armorLights");
      _aggPointLights.Realloc(pl_n); // pl_n is a good upper limit
      _aggPointLights.Resize(0);

      // Prepare the touched array
      VerySmallArray<int, sizeof(int) * 10 + sizeof(int)> touched;
      touched.Resize(pl_n);
      for (int i = 0; i < pl_n; i++)
        touched[i] = false;

      // Then add the rest (untouched reflectors)
      for (int i = 0; i < pl_n; i++)
      {
        if (!touched[i])
        {
          PointLightInfoArray & aggGroup = _aggPointLights.Append();
          aggGroup.Realloc(1);
          aggGroup.Resize(1);
          PointLightInfo * info = new PointLightInfo;
          aggGroup[0] = info;
          info->Load(*type, shape, pointlights.GetEntry(i), armor, wound);
        }
      }

      // Compact the arrays
      _aggPointLights.Compact();
      type->CompactHitpoints();

      // Final check
      if ((_aggPointLights.Size() > 0) && (_aggPointLights[0].Size() == 0))
        Fail("Error: Not valid EntityAIType::_aggPointLights array");
    }
  }

  // aggregated reflectors
  ParamEntryVal reflectors = par >> "Reflectors";
  if (const int n = reflectors.GetEntryCount())
  {
    float armor = objectArmor * (float)(par >> "armorLights");
    _aggReflectors.Realloc(n); // n is a good upper limit
    _aggReflectors.Resize(0);

    // Prepare the touched array
    VerySmallArray<int, sizeof(int) * 10 + sizeof(int)> touched;
    touched.Resize(n);
    for (int i = 0; i < n; i++)
      touched[i] = false;

    // First add aggregated lights
    {
      ParamEntryVal aggregateReflectors = par >> "aggregateReflectors";
      if (aggregateReflectors.IsArray())
      {
        int size = aggregateReflectors.GetSize();
        for (int i = 0; i < size; i++)
        {
          ReflectorInfoArray aggGroup;
          const IParamArrayValue &a = aggregateReflectors[i];
          if (a.IsArrayValue())
          {
            int itemSize = a.GetItemCount();
            aggGroup.Realloc(itemSize);
            aggGroup.Resize(0);
            for (int j = 0; j < itemSize; j++)
            {
              RStringB reflectorName = a[j];
              for (int k = 0; k < n; k++)
              {
                if (reflectorName == reflectors.GetEntry(k).GetName())
                {
                  // Load particular reflector
                  ReflectorInfo *reflectorInfo = new ReflectorInfo;
                  aggGroup.Add(reflectorInfo);
                  reflectorInfo->Load(*type, shape, reflectors.GetEntry(k), armor, wound);
                  touched[k] = true;
                  break;
                }
              }
            }
            if (aggGroup.Size() > 0)
            {
              _aggReflectors.Add(aggGroup);
              _aggReflectors[_aggReflectors.Size() - 1].Compact();
            }
          }
          else
            RptF("Error: Item of aggregateReflectors is not an array");
        }
      }
      else
        RptF("Error: aggregateReflectors is not an array");
    }

    // Then add the rest (untouched reflectors)
    for (int i = 0; i < n; i++)
    {
      if (!touched[i])
      {
        ReflectorInfoArray &aggGroup = _aggReflectors.Append();
        aggGroup.Realloc(1);
        aggGroup.Resize(1);
        ReflectorInfo *info = new ReflectorInfo;
        aggGroup[0] = info;
        info->Load(*type, shape, reflectors.GetEntry(i), armor, wound);
      }
    }

    // Compact the arrays
    _aggReflectors.Compact();
    type->CompactHitpoints();

    // Final check
    if ((_aggReflectors.Size() > 0) && (_aggReflectors[0].Size() == 0))
      Fail("Error: Not valid EntityAIType::_aggReflectors array");
  }
}

void ObjectTypeLights::InitLevel (LODShape * shape, int level)
{
  for (int i=0; i<_aggReflectors.Size(); i++)
  {
    ReflectorInfoArray &ria = _aggReflectors[i];
    for (int j = 0; j < ria.Size(); j++)
    {
      ReflectorInfo & info = *ria[j];
      info.selection.InitLevel(shape, level);
    }
  }
  for (int i=0; i<_aggPointLights.Size(); i++)
  {
    PointLightInfoArray &ria = _aggPointLights[i];
    for (int j = 0; j < ria.Size(); j++)
    {
      PointLightInfo & info = *ria[j];
      info.selection.InitLevel(shape, level);
    }
  }
}

void ObjectTypeLights::DeinitLevel (LODShape * shape, int level)
{
  for (int i=0; i<_aggReflectors.Size(); i++)
  {
    ReflectorInfoArray &ria = _aggReflectors[i];
    for (int j = 0; j < ria.Size(); j++)
    {
      ReflectorInfo &info = *ria[j];
      info.selection.DeinitLevel(shape, level);
    }
  }
  for (int i=0; i<_aggPointLights.Size(); i++)
  {
    PointLightInfoArray &ria = _aggPointLights[i];
    for (int j = 0; j < ria.Size(); j++)
    {
      PointLightInfo &info = *ria[j];
      info.selection.DeinitLevel(shape, level);
    }
  }
}

EntityAIType::EntityAIType( ParamEntryPar param )
:EntityType(param)
{
  _scopeLevel=0;
}

EntityAIType::~EntityAIType()
{
  // make sure all vehicles of this type are already released
  Assert( _refVehicles==0 );
}

AnimationSource *EntityAIType::CreateAnimationSourcePar(const AnimationType *type, ParamEntryPar entry)
{
  // TODO: type is not needed here
  RStringB sourceType = entry>>"source";
  if (!strcmpi(sourceType,"reload"))
  {
    RStringB weapon = entry >> "weapon";
    return new AnimationSourceReload(entry.GetName(), dynamic_cast<WeaponType *>(VehicleTypes.New(weapon)));
  }
  else if (!strcmpi(sourceType,"revolving"))
  {
    RStringB weapon = entry >> "weapon";
    return new AnimationSourceRevolving(entry.GetName(), dynamic_cast<WeaponType *>(VehicleTypes.New(weapon)));
  }
  else if (!strcmpi(sourceType, "reloadmagazine"))
  {
    RStringB weapon = entry >> "weapon";
    return new AnimationSourceReloadMagazine(entry.GetName(), dynamic_cast<WeaponType *>(VehicleTypes.New(weapon)));
  }
  else if (!strcmpi(sourceType,"user"))
  {
    return CreateAnimationSourceUser(entry);
  }
  else if (!strcmpi(sourceType,"direct"))
  {
    return CreateAnimationSourceDirect(sourceType);
  }
  /// argument not known, call a non-argument version
  return base::CreateAnimationSourcePar(type, entry);
}

AnimationSource *EntityAIType::CreateAnimationSource(const AnimationType *type, RStringB source)
{
  if (!strcmpi(source,"reload"))
  {
    ConstParamEntryPtr animSources = _par ? _par->FindEntry("AnimationSources") : ParamEntryPtr(NULL);
    if (animSources)
    {
      ParamEntryVal entry = (*animSources)>> type->GetName();
      RStringB weapon = entry >> "weapon";
      // TODO: do not allow anonymous reload animations
      RptF("Old style reload used in %s",cc_cast(animSources->GetContext()));
      return new AnimationSourceReload(RStringB(), dynamic_cast<WeaponType *>(VehicleTypes.New(weapon)));
    }
  }

  return base::CreateAnimationSource(type, source);
}

void HeadPars::Load(ParamEntryPar cfg)
{
#define GET_PAR(x) _##x=cfg>>#x
  GET_PAR(initAngleY);
  GET_PAR(minAngleY);
  GET_PAR(maxAngleY);
  GET_PAR(initAngleX);
  GET_PAR(minAngleX);
  GET_PAR(maxAngleX);
  GET_PAR(minAngleZ);
  GET_PAR(maxAngleZ);
  GET_PAR(rotZRadius);
  _minAngleZ *= H_PI/180;
  _maxAngleZ *= H_PI/180;
#undef GET_PAR
}

void HeadPars::InitVirtual(CameraType camType, float &heading, float &dive) const
{
  dive = _initAngleX*(H_PI/180);
  heading = _initAngleY*(H_PI/180);
}

void HeadPars::LimitVirtual(CameraType camType, float &heading, float &dive) const
{
  float minHeading = _minAngleY*(H_PI/180);
  float maxHeading = _maxAngleY*(H_PI/180);
  if( maxHeading-minHeading<H_PI*15/8 )
  {
    float initHeading = _initAngleY*(H_PI/180);
    minHeading = AngleDifference(minHeading,initHeading);
    maxHeading = AngleDifference(maxHeading,initHeading);
    heading = AngleDifference(heading,initHeading);

    saturate(heading,minHeading,maxHeading);

    heading += initHeading;
  }
  float minDive = _minAngleX*(H_PI/180);
  float maxDive = _maxAngleX*(H_PI/180);
  saturate(dive,minDive,maxDive);
}

RString GetVehicleIcon(RString name)
{
  ConstParamEntryPtr entry = (Pars >> "CfgVehicleIcons").FindEntry(name);
  if (entry) name = *entry;
  return GetPictureName(name);
}

#if _ENABLE_CONVERSATION
//note: renamed from LoadSpeech as there are multiple LoadSpeech functions, not all of them static
//      GCC: error: 'void LoadSpeech(AutoArray<RStringCT<char>, MemAllocD>&, ParamEntryVal)' was declared 'extern' and later 'static'
static void LoadSpeechVeh(AutoArray<RString> &result, ParamEntryVal entry)
{
  Assert(entry.IsArray());
  int n = entry.GetSize();
  result.Realloc(n);
  result.Resize(n);
  for (int i=0; i<n; i++) result[i] = entry[i];
}
#endif

void WeaponsType::Load(ParamEntryPar cfg)
{
  // weapons
  ParamEntryVal weapons = cfg >> "weapons";
  int n = weapons.GetSize();
  _weapons.Realloc(n);
  _weapons.Resize(n);
  for (int i=0; i<n; i++)
  {
    RString name = weapons[i];
    _weapons[i] = dynamic_cast<WeaponType *>(VehicleTypes.New(name));
  }

  // magazines
  ParamEntryVal magazines = cfg >> "magazines";
  n = magazines.GetSize();
  _magazines.Realloc(n);
  _magazines.Resize(n);
  for (int i=0; i<n; i++)
  {
    RString name = magazines[i];
    _magazines[i] = dynamic_cast<MagazineType *>(VehicleTypes.New(name));
  }
  _magazines.Compact();
}

float WeaponsType::GetMaxRange() const
{
  float maxRange = 0;
  for (int w=0; w<_weapons.Size(); w++)
  {
    const WeaponType *weapon = _weapons[w];
    for (int i=0; i<weapon->_muzzles.Size(); i++)
    {
      const MuzzleType *muzzle = weapon->_muzzles[i];
      if (!muzzle) continue;
      if (muzzle->_magazines.Size()<=0) continue; // nothing to load - not a real weapon
      for (int j=0; j<muzzle->_modes.Size(); j++)
      {
        const WeaponModeType *mode = muzzle->_modes[j];
        if (mode) saturateMax(maxRange, mode->maxRange);
      }
    }
  }
  return maxRange;
}

namespace util {
  template <class ContainerT>
  typename ContainerT::DataType & findOrCreateSoundKey (ContainerT & array, const RString & name)
  {
    for (int i = 0; i < array.Size(); i++)
    {
      if (stricmp(array[i].name, name) == 0)
        return array[i];
    }

    int const index = array.Add();
    array[index].name = name;
    return array[index];
  }

  template <class ContainerT>
  void addSoundParams (ContainerT & array, const IParamArrayValue & args, float & maxvolume)
  {
      // add the sound
      int const index = array.Add();
      SoundPars & pars = array[index];
      GetValue(pars, args);
      if (pars.name.GetLength() > 0 && pars.vol > maxvolume)
        maxvolume = pars.vol;
  }
}

void EntityAIType::Load(ParamEntryPar par, const char *shape)
{
  // ERROR_CATCH_ANY can be executed multiple times, but we want only one error message
  bool errorOnce = false;
  ERROR_TRY()
  
  base::Load(par,shape);

  #define GET_PAR(x) _##x=par>>#x

  GET_PAR(accuracy);
  GET_PAR(camouflage);
  GET_PAR(audible);
  GET_PAR(displayName);

  ConstParamEntryPtr entry = par.FindEntry("displayNameShort");
  if (entry) _shortName = *entry;
  if (_shortName.IsEmpty()) _shortName = _displayName;

#if _ENABLE_CONVERSATION
  GET_PAR(textSingular);
  LoadSpeechVeh(_speechSingular, par >> "speechSingular");
  GET_PAR(textPlural);
  LoadSpeechVeh(_speechPlural, par >> "speechPlural");

  _speechVariants.Resize(0);
  entry = par.FindEntry("SpeechVariants");
  if (entry)
  {
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal cls = entry->GetEntry(i);
      if (!cls.IsClass()) continue;
      EntitySpeechVariant variant;
      variant._variant = cls.GetName();
      LoadSpeechVeh(variant._speechSingular, cls >> "speechSingular");
      LoadSpeechVeh(variant._speechPlural, cls >> "speechPlural");
      _speechVariants.AddUnique(variant);
    }
  }
  _speechVariants.Compact();
#endif

  _targetCategory = par>>"targetCategory";

  {
    ParamEntryVal vars = par >> "storeVariables";
    int count = vars.GetSize();
    _storeVariables.Realloc(count); _storeVariables.Resize(count);
    for (int i=0; i<count; i++) _storeVariables[i] = RStringB::LowCase(vars[i]);
  }

  {
    ParamEntryVal vars = par >> "synchronizedVariables";
    int count = vars.GetSize() / 2;
    if (2 * count != vars.GetSize())
    {
      RptF("Wrong size of synchronizedVariables: %d", vars.GetSize());
    }
    else
    {
      _synchronizedVariables.Realloc(count); _synchronizedVariables.Resize(count);
      for (int i=0; i<count; i++)
      {
        SynchronizedVariableInfo &info = _synchronizedVariables[i];
        info._name = vars[2 * i];
        info._coef = vars[2 * i + 1];
      }
    }
  }

  GET_PAR(weaponSlots);

  GET_PAR(spotableNightLightsOff);
  GET_PAR(spotableDarkNightLightsOff);
  GET_PAR(spotableNightLightsOn);
    
  GET_PAR(visibleNightLightsOff);
  GET_PAR(visibleNightLightsOn);

  _nameSound=par>>"nameSound";
  _typicalSide=(TargetSide)(par>>"side").GetInt();
  _destrType=(DestructType)(par>>"destrType").GetInt();

  //Assert( _parClass );
  //RString model=par>>"model";
  RString picture=par>>"picture";
  // some parent vehicle classes have no model or picture defined
  //if( !picture || !picture[0] ) picture=model; 
  if( picture.GetLength()>0 )
    _picture=GlobLoadTexture(GetVehicleIcon(picture));
  RString portrait=par>>"portrait";
  if( portrait.GetLength()>0 )
    _portrait=GlobLoadTexture(GetVehicleIcon(portrait));

  RString icon=par>>"icon";
  if( icon.GetLength()>0 )
    _icon=GlobLoadTexture(GetVehicleIcon(icon));

  GET_PAR(cost);
  GET_PAR(fuelCapacity);
  GET_PAR(fuelConsumptionRate);
  GET_PAR(armor);

  GET_PAR(damageResistance);

  _extCameraPosition.Init();
  _extCameraPosition[0]=(par>>"extCameraPosition")[0];
  _extCameraPosition[1]=(par>>"extCameraPosition")[1];
  _extCameraPosition[2]=(par>>"extCameraPosition")[2];

  _groupCameraPosition.Init();
  _groupCameraPosition[0]=(par>>"groupCameraPosition")[0];
  _groupCameraPosition[1]=(par>>"groupCameraPosition")[1];
  _groupCameraPosition[2]=(par>>"groupCameraPosition")[2];

  GET_PAR(maxSpeed);
  GET_PAR(limitedSpeedCoef);
  GET_PAR(secondaryExplosion);
  GET_PAR(sensitivity);
  GET_PAR(sensitivityEar);

  GET_PAR(brakeDistance);
  GET_PAR(precision);
  GET_PAR(formationX);
  GET_PAR(formationZ);
  GET_PAR(formationTime);
  GET_PAR(steerAheadSimul);
  GET_PAR(steerAheadPlan);

  GET_PAR(driverCanEject);
  GET_PAR(cargoCanEject);


  // get camera limits / initial values
  _viewPilot.Load(par>>"ViewPilot");
  _headLimits.Load(par>>"HeadLimits");
  _pilotSpec.Load(par>>"PilotSpec");

  // get control / simulation properties
  GET_PAR(predictTurnSimul);
  GET_PAR(predictTurnPlan);
  GET_PAR(minFireTime);

  GET_PAR(enableWatch);
  GET_PAR(enableRadio);

  if (_armor>1e-10)
  {
    _invArmor = 1/_armor;
    _logArmor = log(_armor);
  }
  else
  {
    _invArmor = 1e10;
    _logArmor = 25;
  }

  _invFormationTime = _formationTime> 0 ? 1/_formationTime : 1e10f;

  // cargo definition in config
  _maxFuelCargo=par>>"transportFuel";
  _maxRepairCargo=par>>"transportRepair";
  _maxAmmoCargo=par>>"transportAmmo";

  ParamEntryVal itemsCargoSize = par >> "itemsCargoSize";
  if (itemsCargoSize.IsArray() && itemsCargoSize.GetSize() == 2)
  {
    _itemsCargoWidth = itemsCargoSize[0];
    _itemsCargoHeight = itemsCargoSize[1];
  }
  else
  {
    _itemsCargoWidth = 0;
    _itemsCargoHeight = 0;
  }

  RString clothingType = par >> "clothingType";
  if (clothingType.IsEmpty()) _clothingType = -1;
  else _clothingType = ClothingTypes.AddValue(clothingType, false);

  if(par.FindEntry("assembleInfo"))
  {
    RptF("assembleInfo no longer supported (in %s)",cc_cast(par.GetContext()));
  }

  
  _lockDetectionSystem=par>>"lockDetectionSystem";
  _incommingMisslieDetectionSystem =par>>"incommingMisslieDetectionSystem";

  _allowTabLock = par>>"allowTabLock";
  _showAllTargets = (ShowAllTargets) (par>>"showAllTargets").GetInt();

#if _HELISIM_PRECISE_INTERACTION
  _usePreciseGetInAction = par>>"usePreciseGetInAction";
#endif

  ParamEntryVal weaponCargo = par >> "TransportWeapons";
  int n = weaponCargo.GetEntryCount();
  _weaponCargo.Resize(n);
  for (int i=0; i<n; i++)
  {
    ParamEntryVal par = weaponCargo.GetEntry(i);
    RStringB weapon = par >> "weapon";
    _weaponCargo[i].weapon = dynamic_cast<WeaponType *>(VehicleTypes.New(weapon));
    _weaponCargo[i].count = par >> "count";
  }
  ParamEntryVal magazineCargo = par >> "TransportMagazines";
  n = magazineCargo.GetEntryCount();
  _magazineCargo.Resize(n);
  for (int i=0; i<n; i++)
  {
    ParamEntryVal par = magazineCargo.GetEntry(i);
    RStringB magazine = par >> "magazine";
    _magazineCargo[i].magazine = dynamic_cast<MagazineType *>(VehicleTypes.New(magazine));
    _magazineCargo[i].count = par >> "count";
  }
  
  _minCost=_maxSpeed>0 ? 3.6/_maxSpeed : 1e10;

  GET_PAR(alwaysTarget);
  
  GET_PAR(irTarget);
  if (!par.FindEntry("irScanRange"))
  {
    GET_PAR(irScanToEyeFactor);
    GET_PAR(irScanRangeMin);
    GET_PAR(irScanRangeMax);
  }
  else
  {
    LogF("Old irScanRange used in %s",(const char *)GetName());
    // compatibility settings
    float irRange = par>>"irScanRange";
    _irScanRangeMin = irRange;
    _irScanRangeMax = irRange;
    _irScanToEyeFactor = 1;
  }

  GET_PAR(irScanGround);
  GET_PAR(nightVision);
  if (_nightVision)
  {
    LogF("Obsolete nightVision=true used for %s, use NVG equipment or weapon parameters instead",cc_cast(GetName()));
    _nightVision = false;
  }

  GET_PAR(commanderCanSee);
  GET_PAR(driverCanSee);
  GET_PAR(gunnerCanSee);

  GET_PAR(radarType);

  GET_PAR(attendant);
  GET_PAR(engineer);
  GET_PAR(preferRoads);

  //unit info - full
  ParamEntryVal types = par >> "unitInfoType";
  if (types.IsArray())
  {
    int n = types.GetSize();
    _unitInfoTypes.Realloc(n);
    _unitInfoTypes.Resize(n);
    for (int i=0; i<n; i++) _unitInfoTypes[i] = types[i];
  }
  else
  {
    _unitInfoTypes.Realloc(1);
    _unitInfoTypes.Resize(1);
    _unitInfoTypes[0] = types;
  }
  //unit info - lite
  if(par.FindEntry("unitInfoTypeLite"))
  {
    types = par >> "unitInfoTypeLite";
    if (types.IsArray())
    {
      int n = types.GetSize();
      _unitInfoTypesLite.Realloc(n);
      _unitInfoTypesLite.Resize(n);
      for (int i=0; i<n; i++) _unitInfoTypesLite[i] = types[i];
    }
    else
    {
      if(types.IsTextValue())
      {
        _unitInfoTypesLite.Realloc(1);
        _unitInfoTypesLite.Resize(1);
        _unitInfoTypesLite[0] = types;
      }
    }
  }

  GET_PAR(hideUnitInfo);
  
  GetValue(_mainSound, par>>"soundEngine");
  GetValue(_envSound, par>>"soundEnviron");
  GetValue(_dmgSound, par>>"soundDammage");
  GetValue(_crashSound, par>>"soundCrash");
  GetValue(_landCrashSound, par>>"soundLandCrash");
  GetValue(_waterCrashSound, par>>"soundWaterCrash");
  GetValue(_getInSound, par>>"soundGetIn");
  GetValue(_getOutSound, par>>"soundGetOut");
  GetValue(_servoSound, par>>"soundServo");
  GetValue(_engineOnSoundInt, par>>"soundEngineOnInt");
  GetValue(_engineOffSoundInt, par>>"soundEngineOffInt");
  GetValue(_engineOnSoundExt, par>>"soundEngineOnExt");
  GetValue(_engineOffSoundExt, par>>"soundEngineOffExt");
  GetValue(_lockedSound, par>>"soundLocked");
  GetValue(_incommingSound, par>>"soundIncommingMissile");


  ConstParamEntryPtr woodCrashEntry = par.FindEntry("soundWoodCrash");
  if (woodCrashEntry)
  {
    _woodCrashSound.Load(par, "soundWoodCrash");
    //GetValue(_woodCrashSound, par>>"soundWoodCrash");
  }

  ConstParamEntryPtr buildCrashEntry = par.FindEntry("soundBuildingCrash");
  if (buildCrashEntry)
  {
    _buildingCrashSound.Load(par, "soundBuildingCrash");
    //GetValue(_buildingCrashSound, par>>"soundBuildingCrash");
  }

  ConstParamEntryPtr armorCrashEntry = par.FindEntry("soundArmorCrash");
  if (armorCrashEntry)
  {
    _armorCrashSound.Load(par, "soundArmorCrash");
    //GetValue(_armorCrashSound, par >> "soundArmorCrash");
  }

  _extEnvSoundsMaxVol = 0;
  if (_envSound.name.GetLength()>0)
    _extEnvSoundsMaxVol = _envSound.vol;

  ParamEntryVal extSounds = par >> "SoundEnvironExt";
  n = extSounds.GetEntryCount();
  _extEnvSounds2D.Realloc(n);
  _extEnvSounds2D.Resize(n);
  for (int i=0; i<n; i++)
  {
    ParamEntryVal par = extSounds.GetEntry(i);
    int const m = par.GetSize();
    ExtSoundInfo2D &info2D = _extEnvSounds2D[i];
    // par should be array of arrays
    info2D.name = par.GetName();
    info2D.infos.Resize(0);
    info2D.pars.Resize(0);
    for (int j=0; j<m; j++)
    {
      // check which format is used
      const IParamArrayValue & item = par[j];
      int const count_level0 = item.GetItemCount();
      if (count_level0 == 2) // format is either 2D or 3D
      {
        RString const & name_level0 = item[0];
        int const count_level1 = item[1].GetItemCount();

        if (count_level1 == 2) // format is 3D
        {
          ExtAttrSoundInfo & infoext = util::findOrCreateSoundKey(info2D.infos3, name_level0);

          IParamArrayValue const & item_l1 = item[1];
          RString name_level1 = item_l1[0];
          ExtSoundInfo & info = util::findOrCreateSoundKey(infoext.pars, name_level1);

          util::addSoundParams(info.pars, item_l1[1], _extEnvSoundsMaxVol);
        }
        else if (count_level1 == 4) // format is 2D
        {
          RString name = item[0];
          ExtSoundInfo & info = util::findOrCreateSoundKey(info2D.infos, name);
          util::addSoundParams(info.pars, item[1], _extEnvSoundsMaxVol);
        }
      }
      else // 1D format
      {
        if(count_level0 != 4)
          RptF("4 items expected in %s, got %d",cc_cast(extSounds.GetContext(par.GetName())),count_level0);
        util::addSoundParams(info2D.pars, item, _extEnvSoundsMaxVol);
      }
    }
    info2D.pars.Compact();
    info2D.infos.Compact();
    info2D.infos3.Compact();
    for (int j=0; j<info2D.infos3.Size(); j++)
      for (int k=0; k<info2D.infos3[j].pars.Size(); k++)
        info2D.infos3[j].pars[k].pars.Compact();
    for (int j=0; j<info2D.infos.Size(); j++)
      info2D.infos[j].pars.Compact();
  }
  _extEnvSounds2D.Compact();

  ParamEntryVal soundMunition = par >> "SoundGear";
  if (soundMunition.IsClass())
  {
    LoadExtSoundInfo2D(soundMunition, _gearSounds2D);
    _gearSounds2D.Compact();
  }

  ParamEntryVal equipPars = par >> "SoundEquipment";
  if (equipPars.IsClass())
  {
    LoadExtSoundInfo2D(equipPars, _equipSounds2D);
    _equipSounds2D.Compact();
  }

  // Load weapons info
  _weapons.Load(par);

  // threat
  ParamEntryVal threat=par>>"threat";
  _threat[VSoft]=threat[0];
  _threat[VArmor]=threat[1];
  _threat[VAir]=threat[2];

  // cargo
  #if _DEBUG
    if (strstr(_name._className,"GRAD"))
    {
      __asm nop;
    }
  #endif

  // TODO: walk all weapons, use heuristics to adjust threats accordingly, e.g. check if there are any weapons able to hit air targets

  _kind=(VehicleKind)(par>>"type").GetInt();

  entry = par.FindEntry("forceSupply");
  if (entry) _forceSupply = *entry;
  else _forceSupply = false;

  entry = par.FindEntry("showWeaponCargo");
  if (entry) _showWeaponCargo = *entry;
  else _showWeaponCargo = false;

  _showDmgPoint = VZero;
  _shapeReversed=par>>"reversed";
  EntityType::_shapeReversed=_shapeReversed;

  _lockTargetAction = UALockTargets;
  _lockEmptyTargetAction = UALockEmptyTargets;
  
#if _ENABLE_INDEPENDENT_AGENTS && _ENABLE_IDENTITIES
  entry = par.FindEntry("agentTasks");
  if (entry)
  {
    int n = entry->GetSize();
    _agentTasks.Realloc(n);
    _agentTasks.Resize(n);
    for (int i=0; i<n; i++)
      _agentTasks[i] = (*entry)[i];
  }
#endif

  _headAimDown=-(float)(par>>"HeadAimDown")*(H_PI/180);


  ERROR_CATCH_ANY(exc)
    if (!errorOnce)
    {
      errorOnce = true;
      LogF("Error loading EntityAIType %s",cc_cast(par.GetContext()));
    }
  ERROR_END()
}

bool EntityAIType::IsSlow() const
{
  return IsKindOf(GWorld->Preloaded(VTypeStatic));
}

void EntityAIType::LoadExtSoundInfo2D(ParamEntryVal &entry, AutoArray<ExtSoundInfo2D> &data)
{
  int n = entry.GetEntryCount();

  data.Realloc(n);
  data.Resize(n);

  if (entry.IsClass())
  {
    for (int i = 0; i < n; i++)
    {
      // ExtSoundInfo2D arrays
      ParamEntryVal arrayPars = entry.GetEntry(i);

      if (arrayPars.IsArray())
      {
        ExtSoundInfo2D &info2D = data[i];

        info2D.name = arrayPars.GetName();
        info2D.infos.Resize(0);

        // number of items in array
        int m = arrayPars.GetSize();

        if (m > 0 && info2D.name.GetLength() > 0)
          LoadExtSoundInfo(arrayPars, info2D.infos);

        info2D.infos.Compact();
      }
    }
  }
}

void EntityAIType::LoadExtSoundInfo(ParamEntryVal &entry, AutoArray<ExtSoundInfo> &data)
{
  // number of items in array
  int n = entry.GetSize();
  
  for (int i = 0; i < n; i++)
  {
    // {"name", {"wav_name", db0, 1}}
    const IParamArrayValue &item = entry[i];

    if (item.GetItemCount() == 2)
    {
      // string group identifier
      RString distrType = item[0];

      if (distrType.GetLength() == 0) continue;

      // find / create the table
      int index = -1;

      for (int k = 0; k < data.Size(); k++)
      {
        if (stricmp(data[k].name, distrType) == 0)
        {
          index = k;
          break;
        }
      }
      // new item
      if (index < 0)
      {
        index = data.Add();
        data[index].name = distrType;
      }

      ExtSoundInfo &info = data[index];
      SoundPars &sPars = info.pars.Append();
      // sound pars
      GetValue(sPars, item[1]);            
    }
  }

  for (int i = 0; i < data.Size(); i++)
    data[i].pars.Compact();
}

const SoundPars &EntityAIType::GetEnvSoundExtRandom(RString surfaceSound, RString soundOverride, const char * feetWear) const
{
  const AutoArray<SoundPars> *pars = NULL;

  // phase 1 - search for sound for [surfaceSound, soundOverride]
  const ExtSoundInfo2D *info2D = NULL;
  for (int i=0; i<_extEnvSounds2D.Size(); i++)
  {
    if (stricmp(_extEnvSounds2D[i].name, surfaceSound) == 0)
    {
      info2D = &_extEnvSounds2D[i];
      break;
    }
  }
  if (info2D)
  {
    for (int i=0; i<info2D->infos3.Size(); i++) // lookup in 3d first
    {
      if (stricmp(info2D->infos3[i].name, soundOverride) == 0)
      {
        for (int j=0; j<info2D->infos3[i].pars.Size(); j++)
        {
          if (stricmp(info2D->infos3[i].pars[j].name, feetWear) == 0)
          {
            pars = &info2D->infos3[i].pars[j].pars;
            break;
          }
        }
        if (pars)
          break;
      }
    }

    if (!pars) // lookup in 2d tables
    {
      for (int i=0; i<info2D->infos.Size(); i++)
      {
        if (stricmp(info2D->infos[i].name, soundOverride) == 0)
        {
          pars = &info2D->infos[i].pars;
          break;
        }
      }
    }
  }

  // phase 2 - search for sound for soundOverride only
  if (!pars)
  {
    for (int i=0; i<_extEnvSounds2D.Size(); i++)
    {
      if (stricmp(_extEnvSounds2D[i].name, soundOverride) == 0)
      {
        pars = &_extEnvSounds2D[i].pars;
        break;
      }
    }
  }

  // phase 3 - search for sound for surfaceSound only
  if (!pars && info2D)
    pars = &info2D->pars;

  // select the random sound
  if (pars)
  {
    int n = pars->Size();
    if (n > 0)
    {
      if (n == 1)
        return (*pars)[0];
      float val = GRandGen.RandomValue();
      int i = toIntFloor(val * n);
      saturate(i, 0, n - 1);
      return (*pars)[i];
    }
  }
  // use the default
  return _envSound;
}

const SoundPars* EntityAIType::GetRandomSound(const AutoArray<ExtSoundInfo2D> &data, RString first, RString second) const
{
  const ExtSoundInfo2D *info2D = NULL;

  // select side
  for (int i = 0; i < data.Size(); i++)
  {
    if (stricmp(data[i].name, first) == 0)
    {
      info2D = &data[i];
      break;
    }
  }

  const AutoArray<SoundPars> *pars = NULL;

  // select action
  if (info2D)
  {
    for (int i = 0; i < info2D->infos.Size(); i++)
    {
      if (stricmp(info2D->infos[i].name, second) == 0)
      {
        pars = &info2D->infos[i].pars;
        break;
      }
    }
  }

  // select the random sound
  if (pars)
  {
    int n = pars->Size();

    if (n > 0)
    {
      if (n == 1) return &((*pars)[0]);
      float val = GRandGen.RandomValue();
      int i = toIntFloor(val * n);
      saturate(i, 0, n - 1);

      return &((*pars)[i]);
    }
  }
  return NULL;
}

bool EntityAIFull::CanLock(const TargetType *type, const TurretContext &context, WeaponModePtr ptr) const
{
  // check current ammo
  if (!ptr.IsValid())
  {
    ptr = context._weapons->ValidatedCurrentWeapon();
    if (!ptr.IsValid()) return false;
  }

  const AmmoType *ammo = ptr.GetAmmoType();
  if (!ammo)
  {
    // FIX
    // if there is no mode, use some other way to check if it is LAW
    // check weapon typical magazine
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (!muzzle) return false;
    
    if (muzzle->_magazines.Size() <= 0) return false;
    MagazineType *magazine = muzzle->_magazines[0];
    if (!magazine) magazine = muzzle->_typicalMagazine;
    if (!magazine) return false;

    // check first magazine mode
    ammo = magazine->_ammo;
  }
  if (!ammo) return false;
  return type->LockPossible(ammo);
}

/*!
\patch 1.33 Date 11/28/2001 by Ondra.
- Fixed: Laser guided bomb lock was possible on IR targets.
*/

bool EntityAI::LockPossible( const AmmoType *ammo ) const
{
  const EntityAIType *type=GetType();
  // laser target can locked with laser capable weapon only
  if (type->GetLaserTarget())
    return ammo->laserLock;
  else if (type->GetNvTarget())
    return ammo->nvLock;
  if (ammo->laserLock && !ammo->irLock && !type->GetLaserTarget() && !type->GetNvTarget())
    return false;
  //if (ammo->artilleryLock && !type->GetArtilleryTarget())
  //  return false;
  // disable locking of airborne targets
  if (Airborne())
  {
    if(ammo->airLock == AmmoLockGroundTT) return false;
  } 
  else
  {
    if(ammo->airLock == AmmoLockAirTT ) return false;
  }
  

  // some weapons can lock only ir targets
  if (!type->GetIRTarget() && ammo->irLock)
    return false;
  return true;
}

#if _VBS3
float EntityAI::GetTotalWeightKg()
{
  float weight = 0.0;
  if(_supply)
  {
    weight += _supply->GetWeaponWeightKg();
    weight += _supply->GetMagazineWeightKg();
  }

  return weight;
}
#endif

/*!
\patch 1.78 Date 7/16/2002 by Ondra
- Fixed: setObjectTexture often did not work when repeating mission.
*/

void EntityAIType::InitShape()
{
  base::InitShape();

  // after shape is loaded
  const ParamEntry &par = *_par;

  _weapons.Init(_shape, par);

  _clan.Init(_shape,(par >> "selectionClan").operator RString(),NULL);
  _dashboard.Init(_shape,(par >> "selectionDashboard").operator RString(),NULL);
  _showDmg.Init(_shape,(par >> "selectionShowDamage").operator RString(),NULL);

  _backLights.Init(_shape,(par >> "selectionBackLights").operator RString(),NULL);

  _lightinfos.Clear();

  RStringB nameSupplyPoint = par >> "memoryPointSupply";
  _supplyPointIndex = -1;
  _supplyRadius = 1;
  if (_shape)
  {
    const Shape *memory = _shape->MemoryLevel();
    if (memory) _supplyPointIndex = _shape->PointIndex(memory, nameSupplyPoint);
    if (_supplyPointIndex >= 0)
    {
      _supplyRadius = par >> "supplyRadius";
      if (_supplyRadius < 0) _supplyRadius = _shape->GeometrySphere() * -_supplyRadius;
    }
    else
    {
      _supplyRadius = (_shape->GeometrySphere() + 1) * 1.1f;
    }
  }

  _enableGPS = par >> "enableGPS";

  const Shape *memory = _shape ? _shape->MemoryLevel() : NULL;
  if (memory)
  {
    ParamEntryVal list = par >> "MarkerLights";
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      if (!entry.IsClass()) continue;
      
      int index = _shape->FindNamedSel(memory,(entry >> "name").operator RString());
      if (index < 0) continue;

      PackedColor color; GetValue(color, entry >> "color");
      PackedColor ambient; GetValue(ambient, entry >> "ambient");
      float radius = entry >> "radius";
      bool blinking = entry >> "blinking";

      const NamedSelection::Access sel(memory->NamedSel(index),_shape,memory);
      for (int i=0; i<sel.Size(); i++)
      {
        int pt = sel[i];
        Vector3Val point = memory->Pos(pt);
        LightInfo &light = _lightinfos.Set(_lightinfos.Add());
        light.type = blinking ? LightTypeMarkerBlink : LightTypeMarker;
        light.position = point;
        light.direction = VUp;
        light.color = color;
        light.ambient = ambient;
        light.radius = radius;
        light.nvgMarker = false;
      }
    }

    list = par >> "NVGMarkers";
    for (int i = 0; i < list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      if (!entry.IsClass()) continue;

      int index = _shape->FindNamedSel(memory,(entry >> "name").operator RString());
      if (index < 0) continue;

      PackedColor color; GetValue(color, entry >> "color");
      PackedColor ambient; GetValue(ambient, entry >> "ambient");
      float radius = entry >> "radius";
      bool blinking = entry >> "blinking";

      const NamedSelection::Access sel(memory->NamedSel(index),_shape,memory);
      for (int i=0; i<sel.Size(); i++)
      {
        int pt = sel[i];
        Vector3Val point = memory->Pos(pt);
        LightInfo &light = _lightinfos.Append();
        light.type = blinking ? LightTypeMarkerBlink : LightTypeMarker;
        light.position = point;
        light.direction = VUp;
        light.color = color;
        light.ambient = ambient;
        light.radius = radius;
        light.nvgMarker = true;
      }
    }
  }


  if (_shape)
  {
    // hidden selections
    ParamEntryVal hiddenSelections = par >> "hiddenSelections";
    int n = hiddenSelections.GetSize();
    _hiddenSelections.Realloc(n);
    _hiddenSelections.Resize(n);
    for (int i=0; i<n; i++)
    {
      RString name = hiddenSelections[i];
      _hiddenSelections[i].Init(_shape, name, NULL);
    }
    // textures on hidden selections
    hiddenSelections = par >> "hiddenSelectionsTextures";
    int m = hiddenSelections.GetSize();
    saturateMin(m, n);
    _hiddenSelectionsInitTextures.Realloc(n);
    _hiddenSelectionsInitTextures.Resize(n);
    for (int i=0; i<m; i++)
    {
      RString name = hiddenSelections[i];
      if (name.GetLength() > 0)
      {
        name.Lower();
        _hiddenSelectionsInitTextures[i] = GlobLoadTexture(GetPictureName(name));
      }
    }

    // materials on hidden selections
	ParamEntryPtr entry = par.FindEntry("hiddenSelectionsMaterials");
	if (entry && entry->IsArray())
	{
		hiddenSelections = par >> "hiddenSelectionsMaterials";
		m = hiddenSelections.GetSize();
		saturateMin(m, n);
		_hiddenSelectionsInitMaterials.Realloc(n);
		_hiddenSelectionsInitMaterials.Resize(n);
		for (int i=0; i<m; i++)
		{
		  RString name = hiddenSelections[i];
		  if (name.GetLength() > 0)
		  {
			name.Lower();
			_hiddenSelectionsInitMaterials[i] = GTexMaterialBank.New(TexMaterialName(name));
		  }
		}
	}

    RStringB damageSel = par >> "selectionDamage";
    _totalDamage.Init(_shape, _damageInfo, damageSel, NULL);
  }
  else
  {
    _hiddenSelections.Clear();
    _hiddenSelectionsInitTextures.Clear();
	_hiddenSelectionsInitMaterials.Clear();
  }

  // Lights
  _lights.Init(this, _shape, par, GetArmor(), _damageInfo);

  ConstParamEntryPtr array = par.FindEntry("EventHandlers");
  if (array)
  {
    for (int i=0; i<NEntityEvent; i++)
    {
      EntityEvent e = (EntityEvent)i;
      RString name = FindEnumName(e);
      ConstParamEntryPtr entry = array->FindEntry(name);
      if (entry)
      {
        RStringB expression = *entry;
        _eventHandlers[e] = expression;
      }
    }
  }

  // user type actions
  ConstParamEntryPtr userActions = par.FindEntry("UserActions");
  if(userActions)
  {
    const UIActionTypeDesc &desc = UIActions::actionTypes[ATUserType];
    for (ParamClass::Iterator<> i(userActions->GetClassInterface()); i; ++i)
    {
      UserTypeAction &action = _userTypeActions.Append();
      ParamEntryVal entry = *i;

      action.name = entry.GetName();
      action.displayName = entry >> "displayName";
#if _VBS3 //store pointIndex to animate
      const Shape* mem = _shape->MemoryLevel();
      action.positionIndex = (_shape && mem) ? _shape->PointIndex(mem,pos) : -1;
#endif

      action.radius = entry >> "radius";
      action.condition = entry >> "condition";
      action.statement = entry >> "statement";
      action.onlyForPlayer = entry>>"onlyForPlayer";

#if _HELISIM_PRECISE_INTERACTION
      RString pos = entry >> "position";
      //find memory point
      const Shape* mem = _shape->MemoryLevel();
      action.positionIndex = (_shape && mem) ? _shape->PointIndex(mem,pos) : -1;
      action.modelPosition = VZero;

      if( entry.FindEntry("radiusView")) action.radiusView = entry >> "radiusView";
      else action.radiusView = -1;
      if( entry.FindEntry("show"))       action.show = entry >> "show";
      else action.show = true;

      if( entry.FindEntry("showIn3D")) action.showIn3D = entry >> "showIn3D";
      else action.showIn3D = 0;
      if( entry.FindEntry("available")) action.available = entry >> "available";
      else action.available = 0;

      if( entry.FindEntry("textToolTip")) action.textToolTip = entry >> "textToolTip";
      if( entry.FindEntry("userActionID")) action.userActionID = entry >> "userActionID";
      else action.userActionID = -1;
#else 
      RString pos = entry >> "position";
      action.modelPositionIndex = -1;
      if (_shape)
      {
        const Shape *mem = _shape->MemoryLevel();
        if (mem) action.modelPositionIndex = _shape->PointIndex(mem, pos);
      }
#endif

      ConstParamEntryPtr subentry;
      
      subentry = entry.FindEntry("displayNameDefault");
      action.displayNameDefault = subentry ? *subentry : action.displayName;

      subentry = entry.FindEntry("priority");
      action.priority = subentry ? *subentry : desc.priority;
      
      subentry = entry.FindEntry("showWindow");
      action.showWindow = subentry ? *subentry : desc.showWindow;
      
      subentry = entry.FindEntry("showInHands");
      action.showInHands = subentry ? *subentry : false;

      subentry = entry.FindEntry("hideOnUse");
      action.hideOnUse = subentry ? *subentry : desc.hideOnUse;

      action.shortcut = desc.shortcut;
      subentry = entry.FindEntry("shortcut");
      if (subentry)
      {
        RString name = *subentry;
        if (name.GetLength() > 0) 
        {
          for (int i=0; i<UAN; i++)
          {
            if (stricmp(name, Input::userActionDesc[i].name) == 0)
            {
              action.shortcut = (UserAction)i;
              break;
            }
          }
        }
      }
    }
  }
}

void EntityAIType::DeinitShape()
{
  _weapons.Deinit(_shape);

  _hiddenSelections.Clear();
  _hiddenSelectionsInitTextures.Clear();
  _hiddenSelectionsInitMaterials.Clear();
  _totalDamage.Unload();

  base::DeinitShape();
}

bool EntityAIType::IsShapeReady
(
  const LODShape *lodShape, int level,
  const Ref<ProxyObject> *proxies, int nProxies,
  bool requestIfNotReady
)
{
  bool ret = base::IsShapeReady(lodShape,level,proxies,nProxies,requestIfNotReady);
  return ret;
}

void EntityAIType::InitShapeLevel(int level)
{
  base::InitShapeLevel(level);

  _weapons.InitLevel(_shape, level);

  _clan.InitLevel(_shape,level);
  _dashboard.InitLevel(_shape,level);
  _showDmg.InitLevel(_shape,level);

  _backLights.InitLevel(_shape,level);

  if (level==_shape->FindSpecLevel(VIEW_PILOT))
  {
    _showDmgPoint = _shape->NamedPoint(level,"poskozeni");
  }
  
  _lights.InitLevel(_shape, level);

  for (int i=0; i<_hiddenSelections.Size(); i++)
    _hiddenSelections[i].InitLevel(_shape,level);
  if (_damageInfo && !_damageInfo->IsEmpty())
    _totalDamage.InitLevel(_shape,level);
}

void EntityAIType::DeinitShapeLevel(int level)
{
  _weapons.DeinitLevel(_shape, level);

  _clan.DeinitLevel(_shape,level);
  _dashboard.DeinitLevel(_shape,level);
  _showDmg.DeinitLevel(_shape,level);

  _backLights.DeinitLevel(_shape,level);

  _lights.DeinitLevel(_shape, level);

  for (int i=0; i<_hiddenSelections.Size(); i++)
    _hiddenSelections[i].DeinitLevel(_shape,level);
  if (_damageInfo && !_damageInfo->IsEmpty())
    _totalDamage.DeinitLevel(_shape,level);
  base::DeinitShapeLevel(level);
}

#if _VBS3
int EntityAIType::GetHiddenSelectionIndex(RString selection) const
{
  selection.Lower();
  for(int i=0; i < _hiddenSelections.Size(); ++i)
  {
    const AnimationDef *def = _hiddenSelections[i].GetDef();
    if(def && def->name == selection)
      return i;
  }
  return -1;
}
#endif
/*!
\patch 1.50 Date 4/15/2002 by Ondra
- Fixed: When setViewDistance is used to increase visible range,
tanks did not adjust their IR detector range accordingly.
*/

float EntityAIType::GetIRScanRange() const
{
  //float eyeRange = TACTICAL_VISIBILITY;
  //float eyeRange = Glob.config.tacticalZ;
  float eyeRange = Glob.config.tacticalZ;
  float irRange = eyeRange*_irScanToEyeFactor;
  saturate(irRange,_irScanRangeMin,_irScanRangeMax);
  return irRange;
}

float EntityAIType::VisibleSize() const
{
  // assume default vehicle radius is 3 m
  return _shape ? _shape->BoundingSphere()*0.5f : 3.0f;
}

LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityType> &value, int minVersion)
{
  if (ar.GetArVersion() < minVersion) return LSOK;
  if (ar.IsSaving())
  {
    RString str = value ? value->GetName() : "";
    CHECK(ar.Serialize(name, str, minVersion));
  }
  else
  {
    if (ar.GetPass() != ParamArchive::PassFirst) return LSOK;
    RString str;
    CHECK(ar.Serialize(name, str, minVersion));
    if (str.GetLength() > 0)
      value = VehicleTypes.New(str);
    else
      value = NULL;
  }
  return LSOK;
}

LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityType> &value, int minVersion, const EntityType *defValue)
{
  if (ar.GetArVersion() < minVersion)
  {
    if (!ar.IsSaving()) value = defValue;
    return LSOK;
  }
  if (ar.IsSaving())
  {
    RString str = value ? value->GetName() : "";
    CHECK(ar.Serialize(name, str, minVersion));
  }
  else
  {
    if (ar.GetPass() != ParamArchive::PassFirst) return LSOK;
    RString str;
    CHECK(ar.Serialize(name, str, minVersion));
    if (str.GetLength() > 0)
      value = VehicleTypes.New(str);
    else
      value = NULL;
  }
  return LSOK;
}

LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityAIType> &value, int minVersion)
{
  Ref<const EntityType> tValue(value);
  LSError ok = Serialize(ar,name,tValue,minVersion);
  value = static_cast<const EntityAIType *>(tValue.GetRef());
  return ok;
}

LSError Serialize(ParamArchive &ar, RString name, Ref<const EntityAIType> &value, int minVersion, const EntityAIType *defValue)
{
  Ref<const EntityType> tValue(value);
  LSError ok = Serialize(ar,name,tValue,minVersion,defValue);
  value = static_cast<const EntityAIType *>(tValue.GetRef());
  return ok;
}

VehicleTypeBank VehicleTypes;

void ObjectLights::UpdateAggLights(EntityAI *object)
{
  // Go through all the lights groups
  for (int i = 0; i < _aggLights.Size(); i++)
  {
    AggLightGroup & aggGroup = _aggLights[i];

    if (const int realLightsSize = aggGroup._realReflectors.Size())
    { // reflectors first
      // Get aggregated values
      Color aggColor = HBlack;
      Color aggColorAmbient = HBlack;
      Vector3 aggPosition = Vector3(0, 0, 0);
      Vector3 aggDirection = Vector3(0, 0, 0);
      float aggAngle = 0.0f;
      float aggSize = 0.0f;
      float aggBrightness = 0.0f;
      float aggInnerAngleRatio = 0.0f;
      float aggRadius = 0.0f;

      int aggOnCount = 0;
      bool isAttached = false;
      bool dayLight = false;
			bool heatHaze = false;
			float aggHeatHazeRadius = 0.0f;
			float aggHeatHazePower = 0.0f;
      for (int j = 0; j < realLightsSize; j++)
      {
        const Ref<LightReflectorOnVehicle> & reflector = aggGroup._realReflectors[j];
        if (reflector->IsShining())
        {
          aggColor += reflector->GetDiffuse();
          aggColorAmbient += reflector->GetAmbient();
          aggPosition += reflector->GetAttachedPosition();
          aggDirection += reflector->GetAttachedDirection();
          aggAngle += reflector->Angle();
          aggSize += reflector->GetSize();
          aggRadius += reflector->GetRadius();
          aggInnerAngleRatio += reflector->GetInnerAngleRatio();
          //aggBrightness += Square(reflector->GetBrightness());
          aggBrightness += reflector->GetBrightness();
          dayLight |= reflector->IsDayLight();
					heatHaze |= reflector->IsHeatHaze();
					aggHeatHazeRadius += reflector->GetHeatHazeRadius();
					aggHeatHazePower += reflector->GetHeatHazePower();
          isAttached |= reflector->AttachedOn() != NULL;
          aggOnCount++;
        }
      }

      // Add aggregated light into scene
      const float invLightsSize = (aggOnCount > 0) ? 1.0f / aggOnCount : 1.0f;
      //aggColor = aggColor * invLightsSize;
      //aggColorAmbient = aggColorAmbient * invLightsSize;
      aggPosition *= invLightsSize;
      aggDirection *= invLightsSize;
      aggAngle *= invLightsSize;
      aggInnerAngleRatio *= invLightsSize;
      aggSize *= invLightsSize;
      aggRadius *= invLightsSize;
			aggHeatHazeRadius *= invLightsSize;
			aggHeatHazePower *= invLightsSize;
      //aggBrightness = sqrt(aggBrightness);
      aggBrightness *= invLightsSize;
      if (aggGroup._aggReflector.IsNull())
      {
        LightReflectorOnVehicle *light = new LightReflectorOnVehicle(
          aggGroup._realReflectors[0]->GetShape(), aggColor, aggColorAmbient,
          isAttached ? object : NULL, aggPosition, aggDirection,
          aggAngle, aggInnerAngleRatio, aggSize);

        light->SwitchDayLight(dayLight);
				light->SwitchHeatHaze(heatHaze);
        light->SetBrightness(aggBrightness);
        light->SetRadius(aggRadius);
				light->SetHeatHazeRadius(aggHeatHazeRadius);
				light->SetHeatHazePower(aggHeatHazePower);
        GScene->AddLightAgg(light);
        aggGroup._aggReflector = light;
      }
      else
      {
				aggGroup._aggReflector->SetHeatHazeRadius(aggHeatHazeRadius);
				aggGroup._aggReflector->SetHeatHazePower(aggHeatHazePower);
				aggGroup._aggReflector->SetRadius(aggRadius);
        aggGroup._aggReflector->SetDiffuse(aggColor);
        aggGroup._aggReflector->SetAmbient(aggColorAmbient);
        aggGroup._aggReflector->SetAttachedPos(aggPosition, aggDirection);
        aggGroup._aggReflector->SetAngle(aggAngle);
        aggGroup._aggReflector->SetInnerAngleRatio(aggInnerAngleRatio);
        aggGroup._aggReflector->SetSize(aggSize);
        aggGroup._aggReflector->SetBrightness(aggBrightness);
      }
      aggGroup._aggReflector->Switch(aggOnCount > 0);
    }

    { // point lights next -> WHY no point lights aggregation like with spot lights?
      const int realLightsSize = aggGroup._realPointLights.Size();

      for (int j = 0; j < realLightsSize; j++)
      {
        const Ref<LightPointOnVehicle> & pl = aggGroup._realPointLights[j];
        if (pl->IsShining())
        {
          if (aggGroup._aggPointLight.IsNull())
          {
            aggGroup._aggPointLight = pl;
          }
          else
          {
            const Color aggColor        = pl->GetDiffuse();
            const Color aggColorAmbient = pl->GetAmbient();
            const Vector3 aggPosition   = pl->GetAttachedPosition();
            const float aggSize         = pl->GetSize();
            const float aggBrightness   = pl->GetBrightness();
            const float radius          = pl->GetRadius();
            const bool dayLight         = pl->IsDayLight();
						const bool heatHaze         = pl->IsHeatHaze();
						const float heatHazeRadius  = pl->GetHeatHazeRadius();
						const float heatHazePower		= pl->GetHeatHazePower();

            aggGroup._aggPointLight->SetDiffuse(aggColor);
            aggGroup._aggPointLight->SetAmbient(aggColorAmbient);
            aggGroup._aggPointLight->SetAttachedPos(aggPosition, Vector3(0,0,1));
            aggGroup._aggPointLight->SetSize(aggSize);
            aggGroup._aggPointLight->SetBrightness(aggBrightness);
            aggGroup._aggPointLight->SetRadius(radius);
            aggGroup._aggPointLight->SwitchDayLight(dayLight);
						aggGroup._aggPointLight->SwitchHeatHaze(heatHaze);
						aggGroup._aggPointLight->SetHeatHazeRadius(heatHazeRadius);
						aggGroup._aggPointLight->SetHeatHazePower(heatHazePower);
          }

          break; // no point light aggregation @TODO if necessary
        }
      }
    }
  }
}

template <class T>
void animate (const Ref<T> & info, AnimationContext & animContext, int level, const EntityAI * object)
{
  const bool on = object->IsLightOn(animContext.GetVisualState()) && object->GetHit(info->hitPoint) < 0.9f;
  if (on && !GEngine->GetThermalVision())
    info->selection.Unhide(animContext, object->GetShape(), level);
  else
    info->selection.Hide(animContext, object->GetShape(), level);
}

void ObjectLights::Animate (AnimationContext &animContext, int level, const EntityAI *object, const ObjectTypeLights &typeLights)
{
  for (int i=0; i<_aggLights.Size(); i++)
  {
    AggLightGroup & arg = _aggLights[i];

    for (int j = 0; j < arg._realReflectors.Size(); j++)
      animate(typeLights._aggReflectors[i][j], animContext, level, object);

    for (int j = 0; j < arg._realPointLights.Size(); j++)
      animate(typeLights._aggPointLights[i][j], animContext, level, object);
  }
}

static bool NeedsPositionUpdates(Object *object, int positionIndex)
{
  LODShape *objShape = object->GetShape();
  if (objShape && positionIndex>=0)
  {
    int objLevel = objShape->FindMemoryLevel();
    if (object->IsPointAnimated(objLevel,positionIndex))
      return true;
  }
  return !object->Static() || object->GetDestructType()!=DestructNo;
}

ObjectLights:: ObjectLights()
  : _objectIsAttached(false)
{

}

void ObjectLights::Animate(AnimationContext &animContext, int level, const EntityAI *object, const Turret *turret, const ObjectTypeLights &typeLights)
{

  for (int i=0; i<_aggLights.Size(); i++)
  {
    AggLightGroup &arg = _aggLights[i];
    const ReflectorInfoArray &ria = typeLights._aggReflectors[i];
    for (int j = 0; j < arg._realReflectors.Size(); j++)
    {
      const ReflectorInfo &info = *ria[j];
      bool on = turret->IsLightOn() 
#if _HELISIM_ROTORLIB
        && object->IsElectricity()
#endif
        && object->GetHit(info.hitPoint) < 0.9;

      if (on && !GEngine->GetThermalVision())
        info.selection.Unhide(animContext, object->GetShape(), level);
      else
        info.selection.Hide(animContext, object->GetShape(), level);
    }
  }
}

void ObjectLights::Create (EntityAI * object, const ObjectTypeLights & typeLights)
{
  const int ref_n = typeLights._aggReflectors.Size();
  const int ptl_n = typeLights._aggPointLights.Size();
  const int n = ref_n + ptl_n;

  _aggLights.Realloc(n);
  _aggLights.Resize(n);
  if (GScene)
  {
    LODShape * shape = GScene->Preloaded(CobraLight);
    if (shape)
    {
      for (int i = 0; i < ref_n; i++)
      {
        AggLightGroup & arg = _aggLights[i];

        const ReflectorInfoArray & ria = typeLights._aggReflectors[i];
        const int riaSize = ria.Size();
        arg._realReflectors.Realloc(riaSize);
        arg._realReflectors.Resize(riaSize);
        for (int j = 0; j < ria.Size(); j++)
        {
          const ReflectorInfo & info = *ria[j];
          const float angle = info.angle; // 36.0f;
          // when the light is permanent and the point not animated, we can do with a static permanent light
          const bool needsPositionUpdates = NeedsPositionUpdates(object,info.positionIndex);
          if (needsPositionUpdates)
            object->AttachLights();
          const bool needsAttachment = needsPositionUpdates || info.speed>0;
          LightReflectorOnVehicle *light = new LightReflectorOnVehicle(
              shape, info.color, info.colorAmbient,
              needsAttachment ? object : NULL,
              info.position, info.direction,
              angle, info.angleInnerRatio, info.size);

					light->SwitchDayLight(info.dayLight);
					light->SwitchHeatHaze(info.heatHazeRadius > 0.0f);
					light->SetHeatHazeRadius(info.heatHazeRadius);
					light->SetHeatHazePower(info.heatHazePower);
          light->SetRadius(info.radius);
          GScene->AddLightReal(light);
          arg._realReflectors[j] = light;
          if (!needsAttachment)
          {
            object->UpdateLights(); // we need to positions to be updated so that we can create the lights
            light->UpdatePositionTo(object->FutureVisualState().Transform());// TODO: updating this particular reflector would be enough
          }
        }
      }

      for (int i = ref_n; i < n; i++)
      {
        AggLightGroup & arg = _aggLights[i];
        const PointLightInfoArray & plia = typeLights._aggPointLights[i];
        const int pliaSize = plia.Size();
        arg._realPointLights.Realloc(pliaSize);
        arg._realPointLights.Resize(pliaSize);
        for (int j = 0; j < plia.Size(); j++)
        {
          const PointLightInfo &info = *plia[j];
          //const float angle = info.angle; // 36.0f;
          // when the light is permanent and the point not animated, we can do with a static permanent light
          const bool needsPositionUpdates = NeedsPositionUpdates(object,info.positionIndex);
          if (needsPositionUpdates)
            object->AttachLights();
          const bool needsAttachment = needsPositionUpdates || info.speed>0;
          LightPointOnVehicle *light = new LightPointOnVehicle(
              shape, info.color, info.colorAmbient,
              needsAttachment ? object : NULL,
              info.position, info.size);

					light->SwitchDayLight(info.dayLight);
					light->SwitchHeatHaze(info.heatHazeRadius > 0.0f);
					light->SetHeatHazeRadius(info.heatHazeRadius);
					light->SetHeatHazePower(info.heatHazePower);
          light->SetRadius(info.radius);
          GScene->AddLight(light); 
          arg._realPointLights[j] = light;
          if (!needsAttachment)
          {
            object->UpdateLights(); // we need to positions to be updated so that we can create the lights
            light->UpdatePositionTo(object->FutureVisualState().Transform());// TODO: updating this particular reflector would be enough
          }
        }
      }
      UpdateAggLights(object);
    }
  }
}

template <class InfosT, class LightsT>
void switchLights (bool on, InfosT & infos, LightsT & lights, const EntityAI * object)
{
  for (int j = 0; j < lights.Size(); j++)
  {
    const typename InfosT::DataType & info = infos[j]; // beware, info is a ref<T>
    bool switchOn = (on && object->GetHit(info->hitPoint) < 0.9f);
    if (switchOn && info->speed > 0)
    {
      // blinking
      float phase = info->speed * Glob.time.toFloat();
      phase -= toIntFloor(phase);
      switchOn = phase < info->phaseLimit;
    }

    Light * light = lights[j];
    light->Switch(switchOn);
  }
}

namespace noise {

  float cubicInterpolate (float p0, float p1, float p2, float p3, float x)
  {
    const float P = (p3 - p2) - (p0 - p1);
    const float Q = (p0 - p1) - P;
    const float R = p2 - p0;
    const float S = p1;
    return P * x * x * x + Q * x * x + R * x + S;
  }

  float sCurveInterpolate (float x)
  {
    return (x * x * (3.0f - 2.0f * x));
  }

  float cosInterpolate (float a, float b, float x)
  {
      const float ft = x * H_PI;
      const float f = (1.0f - cos(ft)) * 0.5f;
      const float val =  a * (1.0f - f) + b * f;
      return val;
  }

  inline float lerp (float p0, float p1, float x)
  {
    return ((1.0f - x) * p0) + (x * p1);
  }

  float integerNoise (int n)
  {
    n = (n >> 13) ^ n;
    const int nn = (n * (n * n * 60493 + 19990303) + 1376312589) & 0x7fffffff;
    const float f = static_cast<float>(nn);
    return 1.0f - (f / 1073741824.0f);
  }

  float coherentNoise (float x)
  {
    const int i = static_cast<int>(floor(x));
    const float n0 = integerNoise(i);
    const float n1 = integerNoise(i + 1);
    const float weight = x - floor(x);
    const float noise = lerp(n0, n1, sCurveInterpolate(weight));
    return noise;
  }

  float coherentNoiseComposed (float x, int octaves, float persistence)
  {
      float sum = 0.0f;
      for (unsigned octave = 0; octave < octaves; ++octave)
      {
          const unsigned freq = 1 << octave;
          const float amplitude = pow(persistence, static_cast<int>(octave));
          sum += coherentNoise(x * freq) * amplitude;
      }
      return sum;
  }
  //float smoothNoise (float x) { return 0.5f * IntegerNoise(x) + (IntegerNoise(x - 1.0f) + IntegerNoise(x + 1.0f)) * 0.25f; }
}

template <class InfosT, class LightsT>
void updateFireEffect (bool on, InfosT & infos, LightsT & lights, const EntityAI * object)
{
  for (int j = 0; j < lights.Size(); j++)
  {
    const typename InfosT::DataType & info = infos[j]; // beware, info is a ref<T>
    const bool is_on = lights[j]->IsOn();
    const bool have_fire = info->fireEffect;
    if (is_on && have_fire)
    {
      const float t = Glob.time.toFloat();
      const float b_N = noise::coherentNoiseComposed(t, info->fireEffectOctaves, info->fireEffectPersistence);
      const float brightnessNorm = (b_N + 1.0f) / 2.0f;
      const float brightness = 1.0f - brightnessNorm * info->fireEffectFract;

      LightPositionedColored * light = lights[j];
      light->SetBrightness(brightness);

      //TRACE_PLOT_XY(trace::e_Info, trace::CTX_Default, t, b_N, "lights/noise_%i", j);
      //TRACE_PLOT_XY(trace::e_Info, trace::CTX_Default, t, brightness, "lights/brightness_%i", j);
    }
  }
}

void ObjectLights::Switch (bool on, const EntityAI *object, const ObjectTypeLights &typeLights)
{
  for (int i=0; i<_aggLights.Size(); i++)
  {
    AggLightGroup & arg = _aggLights[i];

    switchLights(on, typeLights._aggReflectors[i], arg._realReflectors, object);
    switchLights(on, typeLights._aggPointLights[i], arg._realPointLights, object);

    updateFireEffect(on, typeLights._aggReflectors[i], arg._realReflectors, object);
    updateFireEffect(on, typeLights._aggPointLights[i], arg._realPointLights, object);
  }
}

void ObjectLights::AnimatePosition(const EntityAI *object, const ObjectTypeLights &typeLights)
{
  if (!object->GetShape()) return;

  int level = object->GetShape()->FindMemoryLevel();
  if (level < 0) return;

  const ObjectVisualState &vs = object->RenderVisualState();
  for (int i=0; i<_aggLights.Size(); i++)
  {
    AggLightGroup & arg = _aggLights[i];

    const ReflectorInfoArray & ria = typeLights._aggReflectors[i];
    for (int j = 0; j < arg._realReflectors.Size(); j++)
    {
      const ReflectorInfo &info = *ria[j];
      if (info.positionIndex >= 0)
      {
        Vector3Val position = object->AnimatePoint(vs,level, info.positionIndex);
        Vector3 direction = VForward;
        if (info.directionIndex >= 0)
        {
          direction = object->AnimatePoint(vs, level, info.directionIndex) - position;
          direction.Normalize();
        }
        LightReflectorOnVehicle *light = arg._realReflectors[j];
        light->SetAttachedPos(position, direction);
      }
    }

    const PointLightInfoArray & plia = typeLights._aggPointLights[i];
    for (int j = 0; j < arg._realPointLights.Size(); j++)
    {
      const PointLightInfo & info = *plia[j];
      if (info.positionIndex >= 0)
      {
        Vector3Val position = object->AnimatePoint(vs,level, info.positionIndex);
        LightPointOnVehicle *light = arg._realPointLights[j];
        light->SetAttachedPos(position, Vector3(0,0,1));
      }
    }
  }
}

template <class RealLightsT, class AggLightT>
void transform_position (RealLightsT & lights, AggLightT & aggLight, const PositionRender & parentTransform)
{
  for (int j = 0; j < lights.Size(); j++)
  {
    const typename RealLightsT::DataType & light = lights[j];
    if (light)
      light->UpdatePositionTo(parentTransform);
  }
  if (aggLight)
    aggLight->UpdatePositionTo(parentTransform);
}

void ObjectLights::TransformPosition (const PositionRender & parentTransform)
{
  for (int i=0; i<_aggLights.Size(); i++)
  {
    AggLightGroup & arg = _aggLights[i];

    transform_position(arg._realReflectors, arg._aggReflector, parentTransform);
    transform_position(arg._realPointLights, arg._aggPointLight, parentTransform);
  }
}

void ObjectLights::Clear()
{
  _aggLights.Clear();
}

void ObjectLights::SwitchLight(bool on)
{
  for (int i = 0; i < _aggLights.Size(); i++)
  {
    AggLightGroup & aggGroup = _aggLights[i];

    aggGroup._aggReflector->Switch(on);
    for (int j = 0; j < aggGroup._realReflectors.Size(); j++)
    {
      aggGroup._realReflectors[j]->Switch(on);
    }

    aggGroup._aggPointLight->Switch(on);
    for (int j = 0; j < aggGroup._realPointLights.Size(); j++)
    {
      aggGroup._realPointLights[j]->Switch(on);
    }
  }
}

DEFINE_CASTING(EntityAI)

#if _VBS2
#include "hla/AAR.hpp"

#if _MOTIONCONTROLLER
#include "hla/MotionController.hpp"
#endif 

#endif


/*!
\param type Entity type
\param fullCreate When fullCreate is false, entity will be serialized
   or transfered over network and many thing need not be initialized
   (e.g. weapon list).
*/

EntityAI::EntityAI(const EntityAIType *type, bool fullCreate)
:Entity(type->_shape, type, CreateObjectId()),  // bubble allocateVisualState to Entity

_stratGoToPos(VZero),

_avoidSpeed(1e5),
_avoidSpeedTime(TIME_MIN),

_avoidAside(0),_avoidAsideWanted(0), // obstacle avoidance offset
_lastSimplePath(TIME_MIN),_simplePathFrom(VZero),_simplePathDst(VZero),

_isDead(false),_isStopped(false),_isUpsideDown(false),
_userStopped(false), _pilotLight(false), _collisionLight(false),
#if _VBS3
  _lightMode(-1),
#endif

_showFlag(true),

_inFormation(true),
_whenDestroyed(TIME_MAX),

_sensorColID(-1),
_targetCategory(type->GetTargetCategory()),

#if ENABLE_ALLOW_DAMAGE
_allowDamage(true),
#endif

_locked(false),_lockedAsWaiting(false),
_locker(new AILocker()),

_lastDammageTime(Glob.time - 120),
_limitSpeed(GetType()->GetMaxSpeedMs()*2),
_limitSpeedForced(GetType()->GetMaxSpeedMs()*2),

_nextUserActionId(0),
_vars(true),
_snappedPosition(VZero)

// fireat/goto automat
//_moveMode(gotoWait),
{
  Assert(type == GetType())
  Assert( type );
  Assert( !type->IsAbstract() );
  Assert( _shape==type->_shape );

  _targetSide=type->_typicalSide;
  _lastMovement=Glob.time;

  _hiddenSelectionsTextures = type->_hiddenSelectionsInitTextures;

  /*
  populates initial material definition, if it exists in correct size, otherwise creates blank array
  */
  int n = type->_hiddenSelections.Size();
  if (type->_hiddenSelectionsInitMaterials.Size() != n)
  {
    _hiddenSelectionsMaterials.Realloc(n);
	_hiddenSelectionsMaterials.Resize(n);
  }
  else
  {
	_hiddenSelectionsMaterials = type->_hiddenSelectionsInitMaterials;
  }

  if( type->IsSupply() )
  {
    _supply=new ResourceSupply(this, fullCreate);
  }

#if _VBS2
  _postFrame = M4Identity;
#endif

}

EntityAI::~EntityAI()
{
  if (_supply)
    _supply->Destroy(this);

  if (_lightsAttached)
  {
    GWorld->RemoveAttachment(this);
  }
  PerformUnlock();
}

static void JSONSaveArray(JSONValue &result, const GameArrayType &array, JSONAllocator &allocator)
{
  result.SetArray();

  for (int i=0; i<array.Size(); i++)
  {
    const GameValue &value = array[i];
    const GameType &t = value.GetType();
    if (t == GameBool)
      result.PushBack((bool)value, allocator);
    else if (t == GameScalar)
      result.PushBack((float)value, allocator);
    else if (t == GameString)
      result.PushBack((RString)value, allocator);
    else if (t == GameArray)
    {
      JSONValue subarray;
      JSONSaveArray(subarray, value, allocator);
      result.PushBack(subarray, allocator);
    }
    else
    {
      RptF("Saving array to database: type %s is not supported", cc_cast(GGameState.GetTypeName(t)));
      result.PushBack(false, allocator); // do not mix indices
    }
  }
}

void EntityAI::SetVariable( RString name, GameValuePar value)
{
  name.Lower();

  bool executeHandler = false;
  const SynchronizedVariableInfo *info = GetSynchronizedVariable(name);
  if (info && !info->_handler.IsEmpty())
  {
    GameValue oldValue;
    if (!_vars.VarGet(name, oldValue) || !(oldValue==value))
    {
      executeHandler = true;
    }

  }
  _vars.VarSet(name, value);

  if (executeHandler)
  {
    GameState *gstate = GWorld->GetGameState();
    GameVarSpace local(false);
    int gshandle = gstate->BeginContext(&local);
    gstate->VarSetLocal("_this",GameValueExt(this,GameValExtObject),true);
    gstate->VarSetLocal("_variable",name,true);
    gstate->VarSetLocal("_value",value,true);
    RString expression = info->_handler.GetString();
    const CompiledExpression &compiled = info->_handler.GetCode();
    gstate->Execute(expression,compiled, GameState::EvalContext::_reportUndefined, GWorld->GetMissionNamespace());
    gstate->EndContext(gshandle);
  }
}


void EntityAI::JSONSaveVariables(JSONValue &container, JSONAllocator &allocator) const
{
  const EntityAIType *type = GetType();
  if (!type) return;

  // first check if some variables are set
  bool found = false;
  GameValue value;
  for (int i=0; i<type->_storeVariables.Size(); i++)
  {
    RStringB name = type->_storeVariables[i];
    if (_vars.VarGet(name, value))
    {
      found = true; break;
    }
  }
  if (!found) return;

  JSONValue newState;
  if (!container.HasMember("state"))
  {
    newState.SetObject();
    container.AddMember("state", newState, allocator);
  }

  JSONValue &state = container["state"];
  if (state.IsObject())
  {
    JSONValue vars;
    vars.SetObject();
    for (int i=0; i<type->_storeVariables.Size(); i++)
    {
      RStringB name = type->_storeVariables[i];
      if (_vars.VarGet(name, value))
      {
        // save the variable
        const GameType &t = value.GetType();
        if (t == GameBool)
          vars.AddMember(name, (bool)value, allocator);
        else if (t == GameScalar)
          vars.AddMember(name, (float)value, allocator);
        else if (t == GameString)
          vars.AddMember(name, (RString)value, allocator);
        else if (t == GameArray)
        {
          JSONValue array;
          JSONSaveArray(array, value, allocator);
          vars.AddMember(name, array, allocator);
        }
        else
        {
          RptF("Saving variable %s to database: type %s is not supported", cc_cast(name), cc_cast(GGameState.GetTypeName(t)));
        }
      }
    }
    state.AddMember("vars", vars, allocator);
  }
}

static GameValue JSONLoadArray(const JSONValue &value)
{
  GameValue result = GGameState.CreateGameValue(GameArray);
  GameArrayType &array = result;

  int n = value.Size();
  array.Realloc(n); array.Resize(n);
  for (int i=0; i<n; i++)
  {
    const JSONValue &item = value[i];
    if (item.IsBool())
      array[i] = item.GetBool();
    else if (item.IsNumber())
      array[i] = (float)item.GetDouble();
    else if (item.IsString())
      array[i] = item.GetString();
    else if (item.IsArray())
      array[i] = JSONLoadArray(item);
  }

  return result;
}

bool EntityAI::JSONLoadVariables(JSONValue &container)
{
  const EntityAIType *type = GetType();
  if (!type) return true;

  JSONValue &state = container["state"];
  if (state.IsObject())
  {
    JSONValue &vars = state["vars"];
    if (vars.IsObject())
    {
      for (JSONValue::MemberIterator i=vars.MemberBegin(); i!=vars.MemberEnd(); i++)
      {
        RString name = i->name.GetString();
        if (i->value.IsBool())
          _vars.VarSet(name, i->value.GetBool());
        else if (i->value.IsNumber())
          _vars.VarSet(name, (float)i->value.GetDouble());
        else if (i->value.IsString())
          _vars.VarSet(name, i->value.GetString());
        else if (i->value.IsArray())
          _vars.VarSet(name, JSONLoadArray(i->value));
      }
    }
  }
  return true;
}

const Array<SynchronizedVariableInfo> &EntityAI::GetSynchronizedVariables() const
{
  const EntityAIType *type = GetType();
  if (type && _synchronizedVariables.Size() == 0)
    return type->_synchronizedVariables; // only when not overriden
  else
    return _synchronizedVariables;
}

const SynchronizedVariableInfo *EntityAI::GetSynchronizedVariable(RString name) const
{
  DoAssert(name.IsLowCase());
  const Array<SynchronizedVariableInfo> &infos = GetSynchronizedVariables();
  for (int i=0; i<infos.Size(); i++)
  {
    if (!strcmp(infos[i]._name,name)) return &infos[i];
  }
  return nullptr;
}


void EntityAI::SynchronizeVariable(RString name, float coef, const GameDataCode &handler)
{
  DoAssert(GetNetworkManager().IsServerOwner());
  OnSynchronizeVariable(name,coef,handler);
  // announce the variable handler to the client
  if (handler.GetCode().Size()>0)
  { // when there is no handler, there is no need to announce anything
    GetNetworkManager().SendCreateMessage(this);
    GetNetworkManager().SynchronizeVariable(this,name,coef,handler);
  }
}


void EntityAI::OnSynchronizeVariable(RString name, float coef, const GameDataCode &handler)
{
  name.Lower();
  for (int i=0; i<_synchronizedVariables.Size(); i++)
  {
    if (strcmp(_synchronizedVariables[i]._name, name) == 0)
    {
      // variable already defined
      if (coef > 0)
      {
        _synchronizedVariables[i]._coef = coef;
        _synchronizedVariables[i]._handler = handler;

      }
      else
        _synchronizedVariables.Delete(i);
      return;
    }
  }
  // new variable
  if (coef > 0)
  {
    SynchronizedVariableInfo &info = _synchronizedVariables.Append();
    info._name = name;
    info._coef = coef;
    info._handler = handler;
  }
}

bool EntityAI::ChildUpdateByParent(const Entity *obj) const
{
  if (_supply)
  {
    if (_supply->ForEachItemCargo([obj](const InventoryItem *item) {return item==obj;})) return true;
  }
  return false;
}

void EntityAI::SetObjectTexture(int index, Texture *texture)
{
  if (index >= 0 && index < _hiddenSelectionsTextures.Size())
    _hiddenSelectionsTextures[index] = texture;
}

void EntityAI::SetObjectMaterial(int index, TexMaterial *mat)
{
  if (index >= 0 && index < _hiddenSelectionsMaterials.Size())
    _hiddenSelectionsMaterials[index] = mat;
}

#if _VBS3
void EntityAI::SetObjectTexture(RString selection, Texture *texture)
{
  SetObjectTexture(GetType()->GetHiddenSelectionIndex(selection), texture);
}

void EntityAI::ResetObjectTexture(int index)
{
  if (index >= 0 && index < GetType()->_hiddenSelectionsInitTextures.Size())
    SetObjectTexture(index, GetType()->_hiddenSelectionsInitTextures[index]);
}
void EntityAI::ResetObjectTexture(RString selection)
{
  ResetObjectTexture(GetType()->GetHiddenSelectionIndex(selection));
}
RString EntityAI::GetHiddenSelectionTexture(int index)
{
  if (index >= 0 && index < _hiddenSelectionsTextures.Size())
  {
    if(_hiddenSelectionsTextures[index])
      return _hiddenSelectionsTextures[index]->GetName();
  }
  return RString();
}
#endif
//bool EntityAI::HasSomeWeapons() const
//{
//  return false;
//}

class HasWeaponsFunc : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    // if we have no weapon or no ammo, we are out of fight
    bool found = context._weapons->ForEachWeapon([](Weapon *weapon) -> bool
    {
      return true; // found
    });
    if (!found) return false; // no weapons
    
    found = context._weapons->ForEachMagazine([](Magazine *magazine) -> bool
    {
      return true; // found
    });
    if (found) return true; // some reserve magazines

    // search for loaded magazines
    context._weapons->ForEachMuzzle([](const WeaponModePtr &ptr) -> bool
    {
      const Magazine *magazine = ptr.GetMagazine();
      return magazine != NULL; // done or continue
    });
    return false;
  }
};

bool EntityAIFull::HasSomeWeapons() const
{
  HasWeaponsFunc func;
  return ForEachTurret(func);
}

class FindWeaponFunc : public ITurretFunc
{
protected:
  int *_count; // out
  const WeaponType *_type;
  const Weapon * &_weapon;

public:
  FindWeaponFunc(const WeaponType *type, int *count, const Weapon * &weapon) : _count(count), _type(type), _weapon(weapon) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (_count)
    {
      *_count = 0;
      context._weapons->ForEachWeapon([this](Weapon *weapon) -> bool
      {
        if (weapon->Type() == _type)
        {
          (*_count)++;
          _weapon = weapon;
        }
        return false; // continue
      });
      return false; // count them all
    }
    else
    {
      return context._weapons->ForEachWeapon([this](Weapon *weapon) -> bool
      {
        if (weapon->Type() == _type)
        {
          _weapon = weapon;
          return true; // found
        }
        return false; // continue;
      });
    }
  }
};

void EntityAIFull::AutoselectWeapon(const Person *gunner)
{
  TurretContext context;
  if (!FindTurret(gunner, context)) return;

  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid())
  {
    ptr = context._weapons->FirstWeapon(this);
    ptr = context._weapons->ValidateWeapon(ptr);
  }
  // check if we are gunner now. If yes, make sure some weapon is active
  context._weapons->SelectWeapon(this, ptr);
}

bool CheckAccess(ParamEntryPar entry);
bool CheckAccessCreate(ParamEntryPar entry);


/*!
\param muzzle muzzle where magazine will be attached
\param magazine attached magazine
*/
void EntityAIFull::AttachMagazine(const TurretContext &context, const MuzzleType *muzzle, Magazine *magazine)
{
  if (!muzzle->CanUse(magazine->Type()))
  {
    RptF("Cannot use magazine %s in muzzle %s",
      (const char *)magazine->Type()->GetName(),
      (const char *)muzzle->GetName());
    return;
  }
  context._weapons->ForEachMuzzle([&context, muzzle, magazine, this](const WeaponModePtr &ptr) -> bool
  {
    if (ptr.GetMuzzleType() == muzzle)
    {
      Muzzle &muzzle = ptr.GetMuzzle();
      if (muzzle._magazineWanted && muzzle._magazineWanted != muzzle._magazine)
      {
        // other reload already in progress
      }
      else
      {
        // switch the parent
        magazine->ItemMoveOut(ptr._weapon);

        // move the magazine to the weapon
        muzzle._magazineWanted = magazine;
        context._weapons->RemoveMagazine(this, magazine);
      }
      return true; // done
    }
    return false; // continue
  });
}

void EntityAIFull::SelectWeaponCommander(AIBrain *unit, const TurretContext &context, const WeaponModePtr &ptr)
{
  WeaponModePtr selected = context._weapons->ValidateWeapon(ptr);
  context._weapons->SelectWeapon(this, selected);
}

void EntityAIFull::SimulateWeaponInputs(InputInterface *input, AIBrain *unit, const TurretContext &context)
{
  /*
  if (input->GetActionDo(UAReloadMagazine))
  {
  ShowWeaponInfo();
  }
  */
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  bool weaponChanged = 0; 
  if (input->GetActionToDo(UAToggleWeapons))
  { 
    // TODO: if Menu with weapon is hidden, first show menu
    bool handledToggleWeapons = false;
    if(!handledToggleWeapons)
    {
      ptr = context._weapons->NextWeaponMode(this, ptr);
      weaponChanged = true;
    }
  }
  else if (ptr.IsValid())
  {
    // check if the current weapon is valid
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (!muzzle->_showEmpty && context._weapons->EmptySlot(ptr))
    {
      // do not show empty weapon when not enabled
      ptr = context._weapons->NextWeaponMode(this, ptr);
      weaponChanged = true;
    }
    else if (!ptr.ShowToPlayer() /*&& (isGunnerFire || transport && transport->IsManualFire())*/)
    {
      // do not show weapons disabled for player when directly controlling the weapons (but avoid switching from weapon used autonomously by the gunner)
      ptr = context._weapons->NextWeaponMode(this, ptr);
      weaponChanged = true;
    }
  }

  if (weaponChanged)
  {
    ptr = context._weapons->ValidateWeapon(ptr);
    SelectWeaponCommander(unit, context, ptr);
  }

  if (input->GetActionToDo(UAOpticsMode))
  { 
    if(unit && unit->IsFreeSoldier())
      context._weapons->NextOpticsMode(unit->GetPerson());
  }


  if(!unit->IsFreeSoldier() && context._turret)
  {
    if(context._turretType->_viewOptics.Size()>1 && GWorld->IsCameraActive(CamGunner) )
    {
      if(input->GetActionToDo(UAZoomIn))
      {
        context._turret->NextOpticsMode(unit->GetPerson(),context._turretType);
      }
      if(input->GetActionToDo(UAZoomOut))
      {
        context._turret->PreviousOpticsMode(unit->GetPerson(),context._turretType);
      }
    }

    if(input->GetActionToDo(UAZeroingUp))
    {
      context._turret->ZeroingUp(context._turretType);
    }
    if(input->GetActionToDo(UAZeroingDown))
    {
      context._turret->ZeroingDown(context._turretType);
    }
  }

}

/*!
During some operations weapon manipulation is not possible
like during burst or during reload animation
*/

bool EntityAIFull::EnableWeaponManipulation() const
{
  // TODO: check for all turrets
  TurretContext context;
  if (!GetPrimaryGunnerTurret(context)) return true;

  WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();

  /**/
  // during some operations weapon manipulation is not possible
  // like during burst or during reload animation
  // check if we are in the middle of the burst
  if (selected.IsValid())
  {
    // safety patch:
    // if new weapon is selected, cancel rest of the burst
    Magazine *magazine = selected.GetMagazine();
    if (magazine && magazine->_burstLeft > 0) return false;
  }
  return true;
}

bool EntityAI::IsCautious() const
{
  AIBrain *unit = PilotUnit();
  if (!unit)
    return false;
  CombatMode mode = unit->GetCombatMode();
  return mode == CMStealth || mode == CMCombat || (mode == CMAware && unit->GetPerson()->GetTargetSide() != TCivilian);
}

bool EntityAI::IsCrewed() const
{
  return false;
}

bool EntityAI::IsCautiousOrDanger() const
{
  AIBrain *unit = PilotUnit();
  return unit && (unit->IsDanger() || IsCautious());
}

inline float FloatSign(float x)
{
  if (x>=0) return 1;
  return -1;
}

float EntityAIFull::EstimateTravelCost(Vector3Par from, Vector3Par to, float costPerSecond) const
{
  PROFILE_SCOPE_EX(etcEA,ai);
  float invLandGrid = InvLandGrid;
  float landGrid = LandGrid;

  // traversal adapted from Landscape::IntersectWithGround
  
  Vector3 dir = to-from;
  float size2 = dir.SquareSize();
  if (size2<=0)
  {
    return 0;
  }
  float invSize = InvSqrt(size2);
  float size = invSize * size2;
  Vector3 dNorm = dir*invSize;
  
  // initialize
  Vector3 pos = from;

  float deltaX = dNorm.X();
  float deltaZ = dNorm.Z();

  int xInt = toIntFloor(pos.X()*invLandGrid);
  int zInt = toIntFloor(pos.Z()*invLandGrid);

  float invDeltaX = fabs(deltaX)<1e-10 ? FloatSign(deltaX)*1e10 : 1/deltaX;
  float invDeltaZ = fabs(deltaZ)<1e-10 ? FloatSign(deltaZ)*1e10 : 1/deltaZ;
  int ddx = deltaX>=0 ? 1 : -1;
  int ddz = deltaZ>=0 ? 1 : -1;
  float dnx = deltaX>=0 ? landGrid : 0;
  float dnz = deltaZ>=0 ? landGrid : 0;

  // maintain beg, end on current square
  float tRest = size;
  float tDone = 0;
  float totalCost = 0;
  while (tRest>0)
  {
    Vector3 beg = pos;

    //advance to next relevant neighbour
    int xio = xInt, zio = zInt;
    float tx = (xInt*landGrid+dnx-pos.X()) * invDeltaX;
    float tz = (zInt*landGrid+dnz-pos.Z()) * invDeltaZ;
    //Assert( tx>=-0.01 );
    //Assert( tz>=-0.01 );
    //float tDoneBeg = tDone;
    if (tx<=tz)
    {
      saturateMin(tx,tRest);
      xInt += ddx;
      tRest -= tx;
      tDone += tx;
      pos += dNorm*tx;
    }
    else
    {
      saturateMin(tz,tRest);
      zInt += ddz;
      tRest -= tz;
      tDone += tz;
      pos += dNorm*tz;
    }

    // pos-beg segment is the line we are traversing in the xio / zio grid
    float dist = pos.Distance(beg);

    // estimate time we will spend in this grid    
    GeographyInfo geogr = GLandscape->GetGeography(xio,zio);
    float costPerM = GetBaseCost(geogr, false, true)*GetFieldCost(geogr);
    // if the field is inaccessible, we still want to get some exposure estimation
    // it is likely path finder will select a different path in such case
    float maxCost = GetType()->GetMinCost()*10;
    if (costPerM>maxCost) costPerM = maxCost;
    float time = dist*costPerM;
    
    totalCost += time*costPerSecond;
  }

  return totalCost;
}

/**
Based on ShotShell::SimulateMovement
@return time to fly
*/

float EntityAIFull::PreviewBallistics(Vector3Par tgtPos, Vector3Par shotPos, const MagazineType *aInfo, const AmmoType *ammo, float &fall, const WeaponModeType *mode) const
{
  Vector3 shotDir = (tgtPos - shotPos).Normalized();
  Vector3 speed = aInfo->_initSpeed * shotDir;
  float time = 0.0f;
  const float maxDeltaT = 0.2f; // experimental: 0.5 sec is too much
  float timeToLive = ammo->_timeToLive;
  Vector3 curShotPos = shotPos;

  while(true)
  {
    float distLeft = shotDir*(tgtPos-curShotPos);
    // speed we are closing to the target
    float relSpeed = speed*shotDir;
    float traveled = relSpeed*maxDeltaT;
    // we assume at 1% of original velocity we are not interested at all
    // however, most often we will expire because of timeToLive
    if (relSpeed < aInfo->_initSpeed*0.01f || timeToLive < 0.0f)
      return FLT_MAX;
    if (distLeft < traveled)
    {
      float deltaT = distLeft / (speed * shotDir);
      speed[1] -= ammo->_coefGravity * G_CONST * deltaT;
      curShotPos += speed * deltaT;
      time += deltaT;
      // in the last iteration there is no need to update speed any more
      break;
    }
    else
    {
      float deltaT = maxDeltaT;
      curShotPos += speed * deltaT;
      time += deltaT;
      timeToLive -= deltaT;
      speed += speed * (speed.Size() * ammo->_airFriction * deltaT);
      speed[1] -= ammo->_coefGravity * G_CONST * deltaT;
    }
  }
  fall = tgtPos.Y() - curShotPos.Y();
  return time;
}

/**
Based on Missile::Simulate
@return time to fly
*/

float EntityAIFull::PreviewRocketBallistics(Vector3Par tgtPos, Vector3Par shotPos, const MagazineType *aInfo, const AmmoType *ammo, float &fall, const WeaponModeType *mode) const
{
  Vector3 shotDir = (tgtPos - shotPos).Normalized();

  // we assume rocket orientation does not change during the flight
  // this assumption is correct for unguided rockets (see usage of torque in Missile::Simulate)
  Matrix3 toWorld(MDirection,shotDir,VUp);
  Matrix3 toModel(MInverseRotation,toWorld);

  // gravity converted to model space  
  Vector3 gModel = toModel * Vector3(0.0f, -G_CONST, 0.0f);
  // we need to consider "model space" speed
  Vector3 speed = Vector3(0,0,aInfo->_initSpeed) + FutureVisualState().ModelSpeed();
  float time = 0.0f;
  const float maxDeltaT = 0.2f; // experimental: 0.5 sec is too much
  float timeToLive = ammo->_timeToLive;
  Vector3 curShotPos = shotPos;


  Missile::EngineState engine = Missile::Init;
  float initTime = ammo->initTime;
  float thrustTime = ammo->thrustTime;
  while(true)
  {
    float distLeft = shotDir*(tgtPos-curShotPos);

    // body air friction
    Vector3 accel(
      speed[0] * speed[0] * speed[0] * 5e-4f + speed[0] * fabs(speed[0]) * 10.0f + speed[0] * 10.0f,
      speed[1] * speed[1] * speed[1] * 5e-4f + speed[1] * fabs(speed[1]) * 10.0f + speed[1] * 10.0f,
      speed[2] * speed[2] * speed[2] * 1e-5f + speed[2] * fabs(speed[2]) * 0.01f + speed[2] * 2.0f
      );
    accel *= ammo->sideAirFriction * ( -1.0f / 10.0f);

    //for small distances 0.2 was not precise enough
    float deltaT = (time > 1.5)? maxDeltaT : maxDeltaT*0.5f;
    switch (engine)
    {
    case Missile::Init:
      if (deltaT>=initTime)
      {
        deltaT = initTime;
        initTime = 0;
        engine = thrustTime > 0.0f ? Missile::Thrust : Missile::Fly;
      }
      else
        initTime -= deltaT;
      break;
    case Missile::Thrust:
      {
        thrustTime -= deltaT;
        if (deltaT>=thrustTime)
        {
          deltaT = thrustTime;
          thrustTime = 0;
          engine = Missile::Fly;
        }
        else
        {
          // fade in the last part (1/4) of flight
          accel[2] += ammo->thrust * floatMin(thrustTime * 4.0f / ammo->thrustTime, 1.0f);
        }
      }
      break;
    }

    // add gravity
    accel += gModel;

    // speed we are closing to the target
    float traveled = speed.Z()*deltaT;
    // prevent infinite loop: once we are not closing to the target any more, we are not interested in the result
    // however, most often we will expire because of timeToLive
    if (speed.Z() < (aInfo->_initSpeed+1)*0.01f || timeToLive < 0.0f)
      return FLT_MAX;

    if (distLeft < traveled)
    {
      deltaT = distLeft / speed.Z();
      curShotPos += toWorld*( ( speed + accel*0.5*deltaT ) * deltaT );
      time += deltaT;
      // in the last iteration there is no need to update speed any more
      break;
    }
    else
    {
      curShotPos += toWorld*( ( speed + accel*0.5*deltaT ) * deltaT );
      speed += accel * deltaT;
      time += deltaT;
      timeToLive -= deltaT;
    }
  }
  fall = tgtPos.Y() - curShotPos.Y();
  return time;
}

bool EntityAIFull::CalculateAimWeapon(const TurretContext &context, const WeaponModePtr &ptr, Vector3 &dir, float &timeToLead, Target *target, bool exact) const
{
  Vector3 tgtPos;
  bool ret = CalculateAimWeaponPos(context, ptr, tgtPos, timeToLead, target, exact);
  if (ret)
  {
    const ProtectedVisualState<const VisualState> &vs = RenderVisualStateScope(true);
    Vector3 myPos=vs->PositionModelToWorld(GetWeaponPoint(*vs, context, ptr));
    #if 0 // _PROFILE
    GScene->DrawDiagArrow("AimT",tgtPos,(myPos-tgtPos).Normalized(),1.5f,Color(0,1,1));
    GScene->DrawDiagArrow("AimT",tgtPos,VUp,2.5f,Color(0,1,1));
    #endif
    dir = tgtPos - myPos;
  }
  return ret;
}

void EntityAI::GoToStrategic(Vector3Par pos)
{
  // if destination has changed, we cannot be in position
  if (_stratGoToPos.Distance2(pos) > 0.5f)
    _inFormation = false;
  _stratGoToPos=pos;
}

#define DIAG_SPEED 0
#define DIAG_COL 0

const float MaxBackAngle = H_PI*0.25;

#if _ENABLE_AI

Vector3 EntityAIFull::PredictFormationSpeed() const
{
  Vector3 predSpeed = PredictVelocity();
  float spSize2 = predSpeed.SquareSize();
  float assumedSpeed = GetType()->GetMaxSpeedMs()*0.5;
  if (spSize2<Square(assumedSpeed) && spSize2>Square(assumedSpeed*0.2))
  {
    predSpeed.Normalize();
    // when leader is moving, but slow, assume he is moving at least at nominal speed
    predSpeed *= assumedSpeed;
  }
  #if 0
  float assumedSpeed = floatMin(GetLimitSpeed(),GetType()->GetMaxSpeedMs()*0.5f);
  // when leader is moving too slow, assume he is moving at least at nominal speed in the formation direction
  if (spSize2<Square(assumedSpeed) && PilotUnit())
  {
    AIUnit *unit = PilotUnit()->GetUnit();
    if (unit && unit->GetSubgroup() && unit->GetCombatMode()>=CMCombat && unit->IsFreeSoldier())
    {
      Vector3Val formDir = unit->GetSubgroup()->GetMoveDirection();
    // the lower the predicted speed, the lower its importance
    Vector3 assumedDir = predSpeed+formDir*assumedSpeed;
    predSpeed = assumedDir.Normalized()*assumedSpeed;
  }
  }
  #endif
  return predSpeed;
}

/*! Common functionality for FormationPilot and LeaderPilot*/

/*!
\patch 1.63 Date 6/1/2002 by Ondra
- Fixed: AIPilot headChange was wrong if vehicle was banked (like motorcycle).
\patch 5117 Date 1/16/2007 by Bebul
- Fixed: swimming AI were unable to get in
*/

bool EntityAIFull::PathPilot(SteerInfo &info, float speedCoef)
{
  // check path position
  AIBrain *unit = PilotUnit();
  Path &path=unit->GetPath();

  // FSM needs to confirm any cover leaving/entering cover
  if (!OnPathFoundToCover())
    // if we are not cleared to use the path, do not move
    // we still may need to report the path is complete to initiate a new planning
    return path.CheckCompleted();

  Assert(path.Size()>=2);
  Vector3 steerPos=SteerPoint(GetSteerAheadSimul(),GetSteerAheadPlan(),&info.action,&info.building,&info.clearance,true);
  Vector3Val steerPredict=SteerPoint(GetPredictTurnSimul(),GetPredictTurnPlan(),NULL,NULL,&info.clearancePredict);
  
  // we are not interested in Y distance
  float distFromPath = DistanceXZFromPath();
  float hcOffset=0;

  float spdFactor=FutureVisualState().ModelSpeed()[2]*(1.0/15);
  saturate(spdFactor,0,1);

  Vector3 steerWant;
  if (spdFactor>0)
  {
    steerWant=PositionWorldToModel(steerPos);
    if (steerWant.Z()>0)
    {
      hcOffset=steerWant.X()*0.02;
      saturate(hcOffset,-0.25,+0.25);
    }
  }

  float cost=path.CostAtPos(FutureVisualState().Position());
  Vector3Val pos=path.PosAtCost(cost,0,FutureVisualState().Position(),cost);

  // for soldiers, use side steps instead of turning
  bool sideStep = unit->GetVehicleIn() == NULL;
  float avoidX = 0;
  if (!sideStep)
  {
    steerPos += FutureVisualState().DirectionAside() * _avoidAside;
  }
  else
  {
    Vector3Val offToPathRel = DirectionWorldToModel(FutureVisualState().Position()-pos);

    avoidX = offToPathRel.X();
    saturate(avoidX,-fabs(_avoidAside),+fabs(_avoidAside));
    if (fabs(avoidX)<0.2f) avoidX = 0;
    steerPos += FutureVisualState().DirectionAside() * avoidX;
  }

  Matrix4 vertical, invVertical;
  vertical.SetUpAndDirection(VUp,FutureVisualState().Direction());
  vertical.SetPosition(FutureVisualState().Position());
  invVertical = vertical.InverseRotation();

  steerWant = invVertical.FastTransform(steerPos);

  Vector3Val steerPredictRel = invVertical.FastTransform(steerPredict);

  if (speedCoef >= 0)
  {
    info.headChange = atan2(steerWant.X(), steerWant.Z()) + hcOffset * spdFactor;
    info.turnPredict=atan2(steerPredictRel.X(),steerPredictRel.Z());
  }
  else
  {
    info.headChange = atan2(-steerWant.X(), -steerWant.Z()) - hcOffset * spdFactor;
    info.turnPredict=atan2(-steerPredictRel.X(),-steerPredictRel.Z());
  }


  float precision=GetPrecision();

  EngineOn();
  //_moveMode=gotoNormal;

  float distPath2=FutureVisualState().Position().Distance2(pos);
  float distEnd2=FutureVisualState().Position().Distance2(path.End());

  AILockerSavedState state;
  PerformUnlock(&state);
  float speedAtCost = path.SpeedAtCost(cost,unit->IsFreeSoldier());
  PerformRelock(state);

  info.speedWanted = speedAtCost * speedCoef;

  // hotfix - if planned cost is too low, force some minimum speed
  float minSpeedNeeded = floatMax(GetType()->GetMaxSpeedMs()*0.05f,0.5f);
  if (info.speedWanted<minSpeedNeeded && info.speedWanted>0)
  {
    if (path.Size() == 2 && path[1]._cost == 0)
    {
      // do not report, degenerate (zero length) path
    }
    else
      LogF("Too slow speed in planned path (%g<%g)",info.speedWanted,minSpeedNeeded);
    info.speedWanted = minSpeedNeeded;
  }

  { // when we strayed off the path, we should slow down  
    // path is planned with OperGrid precision
    // the more we are outside of this corridor, the more careful we should be
    float safeDist,badDist;
    GetType()->GetPathPrecision(speedAtCost,safeDist,badDist);


    // when path is planned fast, there is probably less penalty for straying away from it a little bit

    // speedAtCost
    
    // if moving on road, a stray can be reasonable, as it can be caused by avoidance
    if (unit->IsOnRoad())
    {
      // TODO: straying to left (overtaking) less dangerous than straying to right (out of the road)
      float distXFromPath = DistanceXFromPath();
      // moving 3 m right and 1 m left is considered safe on the road
      if (distXFromPath<0) // negative means right
      {
        const float safeRight = 3.0f;
        if (-distFromPath>safeRight) distFromPath = -distXFromPath-safeRight;
        else distFromPath = 0;
      }
      else
      {
        const float safeLeft = 1.0f;
        if (distFromPath>safeLeft) distFromPath = distXFromPath-safeLeft;
        else distFromPath = 0;
      }

    }
    float maxPathSpeed = Interpolativ(distFromPath,safeDist,badDist,1.0f,0.05f)*GetType()->GetMaxSpeedMs();
    // nothing should be forced to move slower than 1.8 km/h
    saturateMax(maxPathSpeed,0.5f);
    
#if _ENABLE_REPORT
      if (CHECK_DIAG(DEPath) && this==GWorld->CameraOn())
        DIAG_MESSAGE(500,Format("%s: path speed %.1f, stray %.1f, avoid %.1f (for %.1fs, aside %.1f), time left %.1f of %.1f",
          cc_cast(GetDebugName()),info.speedWanted,maxPathSpeed,_avoidSpeed,_avoidSpeedTime.Diff(Glob.time),_avoidAsideWanted,path.EndCost()-cost,path.EndCost()));
#endif

    saturate(info.speedWanted,-maxPathSpeed,+maxPathSpeed);
  }


  float tholdDist2 = Square(precision);
  if (unit->GetPlanningMode() == AIBrain::LeaderDirect)
  {
    tholdDist2 = Square(precision*0.9);
    // if swimming, we should force nonzero speedWanted (if the end of path not reached)
    if (IsSwimming())
      saturateMax(info.speedWanted,1.2f);
  }
  else if (_inFormation)
    tholdDist2 = Square(precision*3);

  // note: path pilot may be used for the leader as well
  if(
    // close enough
    distEnd2 < tholdDist2
    // when the path is completed, soldier will no longer follow it. We need to report it as done
    || path.CheckCompleted()
  )
  {
    _inFormation=true;
    StopPilot(unit,info);
  }
  else
  {
    _inFormation=false;
    if (distPath2>tholdDist2 && info.speedWanted!=0)
      saturateMax(info.speedWanted,GetType()->GetMaxSpeedMs()*0.25);
  }

  if( Glob.time<_avoidSpeedTime )
    saturate(info.speedWanted,-_avoidSpeed,+_avoidSpeed);

  if (sideStep)
  {
    // try to avoid collision in 0.5 s, but do not move aside faster than forward
    info.sideSpeedWanted = (_avoidAside-avoidX)*0.5;
    // avoid using slow side speed with fast forward speed
    if (fabs(_avoidAside)<=fabs(avoidX) )
    {
      info.sideSpeedWanted = 0;
    }
    saturate(info.sideSpeedWanted, -fabs(info.speedWanted), +fabs(info.speedWanted));
  }

  return _inFormation;
}

/*!
\patch 5142 Date 3/21/2007 by Ondra
- Fixed: AI vehicle braking now smoother.
*/
float EntityAIFull::BrakeBeforeReached(float finalDist2) const
{
  float bDist=GetType()->GetBrakeDistance();
  float maxSpeed=GetType()->GetMaxSpeedMs();
  float refDist = bDist*4;
  if( finalDist2<Square(refDist) )
  {
    float limSpd = maxSpeed;
    // really close - brake hard
    const float minNearSpeed = 0.1f;

    //float minNearSpeed = finalDist2/Square(refDist);

    //float minNearSpeed = sqrt(finalDist2)/refDist;
    //Square(minNearSpeed*refDist) = finalDist2;
    if (finalDist2<Square(minNearSpeed*refDist))
      limSpd=maxSpeed*minNearSpeed;
    else
    {
      // smooth slowing down
      float dist = finalDist2*InvSqrt(finalDist2);
      float nearFactor = dist/refDist;
      // was:
      //   bDist*2 -> maxSpeed*0.3
      //   bDist*3 -> maxSpeed*0.5
      //   bDist*4 -> maxSpeed*0.5
      // now:
      //   bDist*4 -> maxSpeed*1.0
      //   bDist*3 -> maxSpeed*0.75
      //   bDist*2 -> maxSpeed*0.5
      limSpd = maxSpeed * nearFactor;
    }
    // never slow down to less than 1 m/s
    saturateMax(limSpd,1);
    return limSpd;
  }
  return FLT_MAX;
}

void EntityAIFull::StopPilot(AIBrain *unit, SteerInfo &info)
{
  info.speedWanted=0;
  info.turnPredict=0;
  info.headChange=0;
}

void EntityAIFull::LeaderPathPilot(AIBrain *unit, SteerInfo &info, float speedCoef)
{
  // check path position
  const Path &path=unit->GetPath();
  if (path.Size()>=2 || path.IsIntoCover())
  {
    bool done=PathPilot(info, speedCoef);

    //_moveMode=gotoNormal;
    float cost=path.CostAtPos(FutureVisualState().Position());
    Vector3Val pos=path.PosAtCost(cost,0,FutureVisualState().Position(),cost);

    // measure distance from corresponding surface point?
    float distPath2=(FutureVisualState().Position()-pos).SquareSizeXZ();
    float distEnd2=FutureVisualState().Position().Distance2(path.End());

    float precision=GetPrecision();
    AILockerSavedState state;
    PerformUnlock(&state);
    float speedAtCost = path.SpeedAtCost(cost,unit->IsFreeSoldier());
    PerformRelock(state);
    float safeDist,badDist;
    GetType()->GetPathPrecision(speedAtCost,safeDist,badDist);
    // using pathPrecision only could be more logical, however we want to limit the possible impact of the change
    // only precision was used before 5143
    // without the GetPathPrecision the boats were updating path too often
    float pathPrecision = floatMax(badDist,precision);

#if DIAG_SPEED
    if( this==GWorld->CameraOn() )
      LogF("SpeedAtCost %.1f",speedWanted*3.6);
#endif


    if (unit->GetPlanningMode() != AIBrain::LeaderDirect) // do not report in DirectGo mode
    {
      if (distEnd2 < Square(precision) || done)
      {
#if DIAG_WANTED_POSITION
if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
{
  LogF(
    "Vehicle %s: step completed - %.0f, %.0f",
    cc_cast(GetDebugName()),
    path.End().X(), path.End().Z());
}
#endif
        unit->OnStepCompleted();
      }
      else if( distPath2>Square(pathPrecision*0.8f) )
      {
        if( distPath2>Square(pathPrecision*2) || path.GetSearchTime()<Glob.time-2 && unit->GetState() != AIBrain::Planning)
        {
          // path is not recent or the error is very high
          // unit->OnStepTimedOut();
          // do not use the old path when far from it
          // note: we may be using this path because we are recovering from an impossible position
          // how can we recognize that?
          unit->UpdateOperativePlan();
        }
      }
      else
      {
        if (path.GetForceReplanCost() > 0 && cost > path.GetForceReplanCost())
          // we need to avoid further movement on this path (may lead through objects)
          unit->UpdateOperativePlan();
        else if (path.GetReplanCost() > 0 && cost > path.GetReplanCost())
          // we need to replan
          unit->OnStepTimedOut();
      }
    }
  }
  else
    StopPilot(unit,info);

  // saturate speed (may even stop us - used for danger.fsm)
  float forceSpeed=unit->GetForceSpeed();
  if (forceSpeed>=0) //use forcedSpeed only for saturation (forcing FAST speed possibly do not work in all cases)
    saturate(info.speedWanted, -forceSpeed, forceSpeed);
  
  AlignToTarget(info);

  //LogF("LeaderPilot: speedWanted1 = %.1f", info.speedWanted);

  if( GetStopped() && fabs(info.headChange)<0.05f && fabs(info.speedWanted)<0.1f) 
  {
    if (_engineOffTimeOutInProgress)
    {
      if (Glob.time > _engineOffTimeOut) 
      {
        _engineOffTimeOutInProgress = false;
        EngineOff();
      }
    }
    else
    {
      _engineOffTimeOutInProgress = true;
      _engineOffTimeOut = Glob.time + GRandGen.RandomValue()*20+20; // from 20 to 40 second
    }
  }
  else
    _engineOffTimeOutInProgress = false;

  if(
    _stratGoToPos.SquareSize()>0.1 &&
    unit->GetPlanningMode() != AIBrain::LeaderDirect &&
    StopAtStrategicPos()
  )
  {
    // in some modes we should not stop
    // we will re-plan route before we reach the position

    // strategic target known
    float finalDist2=_stratGoToPos.Distance2(FutureVisualState().Position());
    float limSpd = BrakeBeforeReached(finalDist2);
    if (limSpd<FLT_MAX)
      saturate(info.speedWanted, -limSpd,+limSpd);
  }

  // DirectGo is issued only on short distance
  // we should not move too fast
  if (unit->GetPlanningMode() == AIBrain::LeaderDirect)
  {
    float limSpd = 4; // 4m/s - i.e. 14 km/h
    saturate(info.speedWanted,-limSpd,+limSpd);
  }

  // check exposure
  AIBrain *commanderAgent = CommanderUnit();
  if (!commanderAgent)
  {
    LogF("Pilot, no commander");
    return;
  }

  float limitSpeed =  floatMin(_limitSpeed, _limitSpeedForced);
  saturateMin(info.speedWanted, limitSpeed); // move max. by given speed
}


bool EntityAIFull::IsStandingStill() const
{
  float maxSpeed = GetType()->GetMaxSpeedMs();
  // when moving very slow, assume we are stopped
  return FutureVisualState()._speed.SquareSize() < maxSpeed*0.1f;
}

void EntityAIFull::LeaderPilot(SteerInfo &info)
{ // subgroup leader -
  // if we are near the target we have to operate more precisely
  PROFILE_SCOPE(entLP);
  if (_userStopped)
  {
    info.speedWanted=0;
    info.headChange=0;
    info.turnPredict=0;
    return;
  }
    
  info.speedWanted=0; // go faster
  AIBrain *unit = PilotUnit();
  
  if (unit->GetAIDisabled() & (AIBrain::DAMove)) return;

#if DIAG_SPEED
  if( this==GWorld->CameraOn() )
    LogF("Basic speed %.1f",speedWanted*3.6);
#endif

  // check if hiding (similar to keeping formation)
  LeaderPathPilot(unit, info);
}

#endif //_ENABLE_AI

float EntityAI::PilotSpeed() const
{
  float speedWanted=0; // go faster 
  // check path position
  const Path &path=PilotUnit()->GetPath();
  if (path.Size()>=2)
  {
    float cost=path.CostAtPos(FutureVisualState().Position());
    AILockerSavedState state;
    PerformUnlock(&state);
    speedWanted=path.SpeedAtCost(cost,PilotUnit()->IsFreeSoldier());
    PerformRelock(state);
  }
  return speedWanted;
}

#if _ENABLE_AI

void SpeedEstimation::Update(Vector3Par pos)
{
  const float lastSpeedInterval = 0.5f;
  dword tmstm = GetTickCount();
  float deltaT = (tmstm-timeStamp)/1000.0f;
  float dist = lastPos.Distance(pos);
  speed = (speed*lastSpeedInterval + dist)/(lastSpeedInterval+deltaT);
  lastPos=pos;
  timeStamp = tmstm;
}

 // ignore vehicles - we need permanent information
 struct FilterStaticOnly
 {
   bool operator()(const Object *obj) const {return obj->Static();};
 };

void EntityAIFull::TraceFireSector(TurretContextEx &context)
{
  PROFILE_SCOPE_EX(enTFS, sim);

  // if we have recently fired, do not update
  if (context._weapons->_lastShotTime>Glob.time-1) return;
  // assume we would be firing with the currently selected weapon
  
  if (context._weapons->_lastTrace>Glob.time-0.2f) return;
  
  // if we are moving fast, rasterizing anything has little sense - it would become obsolete soon anyway
  if (FutureVisualState()._speed.SquareSize()>Square(10)) return;
  
  context._weapons->_lastTrace = Glob.time;
  
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  ptr = context._weapons->FindValidWeapon(ptr);
  ptr = context._weapons->ValidateWeapon(ptr);
  if (!ptr.IsValid()) return;
  
  Vector3 pos = FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(), context, ptr));
  // we use GetWeaponDirectionWanted rather than GetWeaponDirection
  // with Wanted we get:
  // - in Aware we still get direction where the soldier would be aiming
  // - when weapon is moving in animations we still get the intended weapon direction
  Vector3 dir = GetWeaponDirectionWanted(context, ptr);
  
  const Magazine *mag = ptr.GetMagazine();
  if (!mag || !mag->Type()) return;
  
  const AmmoType *ammo = mag->Type()->_ammo;
  if (!ammo) return;
  
  //  never rasterize beyond weapon range
  float dist = 150.0f;
  // both AI and player can switch weapon quickly.
  if (context._turretType)
  {
    // TODO: consider checking the range for the same ammo only
    saturateMin(dist, context._turretType->_weapons.GetMaxRange());
  }

  ADD_COUNTER(tfsRa, 1);

  // randomly change firing angle
  const float period = 10.0f;
  const float angle = H_PI*0.167f;
  
  // by applying a reversed bit pattern we perform big steps first, small later
  // with 10 seconds period and step 0.2 we have 50 steps
  // reversing 8 bits should be quite enough
  const int bits = 8;
  const int maxVal = (1<<bits)-1;
  int gradual = toInt(Glob.time.Mod(period)*(maxVal/period));
  int swizzled = 0;
  for (int i=0; i<bits; i++)
    swizzled |= ((gradual>>i)&1)<<(bits-1-i);
  float angleMod = swizzled*(1.0f/maxVal)*2*angle-angle;
  // randomize between steps to get better coverage
  angleMod += GRandGen.RandomValue()*angle-angle*0.5f;
  Matrix3 mod(MRotationY,angleMod);
  
  // sweep left/right with the tracing
  dir = mod*dir;
  
  { // always trace assuming the unit is not aiming into the ground, as that would lead to a short trace
    const float traceRange = GRandGen.PlusMinus(80.0,30.0);
    float curY = GLandscape->SurfaceYAboveWaterNoWaves(pos.X(),pos.Z());
    float rangeY = GLandscape->SurfaceYAboveWaterNoWaves(pos.X()+dir.X()*traceRange,pos.Z()+dir.Z()*traceRange);
    float minElevation = (rangeY-curY)*(1.0f/traceRange);
    if (dir[1]<minElevation)
    {
      dir[1] = minElevation;
    }
    // avoid firing too much above the ground - this helps when covering downhill locations
    dir[1] = Lerp(dir[1],minElevation,GRandGen.RandomValue());
    dir.Normalize();
  }
  
  // compute intersection of a predicted bullet
  float tRet = GLandscape->IntersectWithGroundOrSea(NULL,pos,dir,0,dist);
  if (tRet>dist) tRet = dist;
  if (tRet>OperItemGrid)
  {
    CollisionBuffer col;
    Vector3 end = pos+ dir*tRet;
    float dist = tRet;

    GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),col,Landscape::FilterCombine(Landscape::FilterIgnoreOne(this),FilterStaticOnly()),pos,end,0);
    if (col.Size()>0)
    {
      for (int i=0; i<col.Size(); i++)
      {
        const CollisionInfo &ci = col[i];
        #if _DEBUG
          Object *obj = ci.hierLevel>0 ? ci.parentObject : ci.object;
          Assert(obj->Static()); // vehicles should already be handled by a filter
        #endif
        if (tRet>ci.under*dist) tRet = ci.under*dist;
      }
      end = pos+ dir*tRet;
    }
    if (pos.Distance2(end)>Square(OperItemGrid))
      // when rasterizing fire sectors we use somewhat lower costs
      GLandscape->SuppressCache()->Trace(this,ammo->hit*0.3f,dist,pos,end);
  }
}

/**
@param preferTarget return direction to a weapon target even when we are only "preparing" (watching) it
@return world space direction to the watched target (returned as offset - not normalized)
*/

Vector3 EntityAIFull::GetTargetAimDir( Vector3Par dir, bool preferTarget ) const
{
  AIBrain *commander = CommanderUnit();

  // pilot checks only the main weapon system
  float dirSize2 = dir.SquareSize();
  if (commander && commander->GetWatchMode() != AIBrain::WMNo && dirSize2>=0.1f)
    // watch is commanded - look in the given direction
    return dir;
  else if (dirSize2>0)
    // formation watch - if no interesting target
    return dir;
  else
    return VZero;
}


bool EntityAIFull::UseBackwardMovement(Vector3Par toMove, EntityAI *leaderVehicle) const
{
  return FutureVisualState().Direction() * leaderVehicle->FutureVisualState().Direction() > 0;
}

#endif //_ENABLE_AI

int EntityAI::IsOnRoad() const
{
  AIBrain *unit = PilotUnit();
  if( !unit ) return false;
  return unit->IsOnRoad();
}

bool EntityAI::IsOnRoadMoving( float minSpeed ) const
{
  if( FutureVisualState().Speed().SquareSize()<Square(minSpeed) ) return false;
  return IsOnRoad()>0;
}

#if _ENABLE_AI

void EntityAIFull::AlignToTarget( SteerInfo &info ) const
{
#if _ENABLE_CHEATS
  if (GWorld->CameraOn()==this)
    __asm nop;
#endif
  // when not moving, we may rotate to aim at the target
  if (fabs(info.speedWanted)<0.1f && fabs(info.sideSpeedWanted)<0.1f && info.allowFreeTurn)
  {
    AIBrain *commander = CommanderUnit();
    if (commander)
    {
      Vector3Val watchDir = commander->GetWatchDirection();
      Vector3Val tgtDir = GetTargetAimDir(watchDir);
      if (tgtDir.SquareSize()>0.1f)
      {
        Vector3Val tgtDirRel = DirectionWorldToModel(tgtDir);
        info.headChange = atan2(tgtDirRel.X(),tgtDirRel.Z());
        // check if we could aim primary weapon there
        // if we could, no need to turn body
        TurretContext context;
        if (GetPrimaryGunnerTurret(context))
        {
          // check what are the aiming limits of the weapon
          float minAngle,maxAngle;
          FireAimingLimits(minAngle, maxAngle, context, context._weapons->ValidatedCurrentWeapon());

          if (info.headChange<minAngle || info.headChange>maxAngle)
          {
            // TODO: we still may want to turn not a lot more than needed
          }
          else
            info.headChange = 0;
        }
      }
    }
  }
}

#endif

void EntityAI::PerformFF( FFEffects &effects )
{
  //effects=_ff;
  //_ff.gunCount=0; // reset guns
  // calculate acceleration effects
  //Vector3Val accel=DirectionWorldToModel(Acceleration());
  //Vector3Val accelR=accel*0.1;
  //effects.forceX=-accelR[0]*0.5;
  //effects.forceY=-accelR[1]-accelR[2];
  effects.engineMag=0;
  effects.engineFreq=1;
}

void EntityAI::ResetFF()
{
  //_ff.gunCount=0; // reset guns
}

void EntityAI::SetTargetCategory(const char *name)
{
  _targetCategory = GWorld->GetTargetCategories().GetCategory(name);
}

/*!
\patch 2.06 Date 1/5/2004 by Jirka
- Fixed: AI units could not take any magazines.
*/

bool EntityAI::GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const
{
  char buffer[1024]; buffer[0] = 0;
  switch (action->GetType())
  {
    case ATUser:
      {
        const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
        const UserActionDescription *desc = FindUserAction(actionTyped->GetIndex());
        if (!desc) return false;
        params.Add(CreateTextStructured(NULL, desc->text));
#if _HELISIM_PRECISE_INTERACTION
        params.Add(CreateTextStructured(NULL, desc->textDefault));
        params.Add(CreateTextStructured(NULL, desc->textToolTip));
#else
        params.Add(CreateTextStructured(NULL, desc->text));
#endif
        return true;
      }
    case ATUserType:
      {
        const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
        const EntityAIType *type = GetType();
        const UserTypeAction &info = type->_userTypeActions[actionTyped->GetIndex()];
        params.Add(CreateTextStructured(NULL, info.displayName));
        params.Add(CreateTextStructured(NULL, info.displayNameDefault));
#if _HELISIM_PRECISE_INTERACTION
        params.Add(CreateTextStructured(NULL, info.textToolTip));
#endif
        return true;
      }
  }
  return base::GetActionParams(params, action, unit);
}

static bool FormatWeapon(UIActionParams &params, UIActionType action, const EntityAIFull *veh, const TurretContext &context, const WeaponModePtr &ptr)
{
  if (veh)
  {
    if (ptr.IsValid())
    {
      const WeaponType *weapon = ptr.GetWeaponType();
      RStringB displayName = weapon->_displayName;
/*
      RStringB displayName = slot._muzzle->_displayName;
      const WeaponModeType *mode = slot._weaponMode;
      if (mode) displayName = mode->GetDisplayName();
*/
      params.Add(CreateTextASCII(NULL, displayName));
      return true;
    }
    else
    {
      RptF("Action %s - wrong weapon", cc_cast(FindEnumName(action)));
      return false;
    }
  }
  else
  {
    RptF("Action %s - wrong target", cc_cast(FindEnumName(action)));
    return false;
  }
}

INode *FormatText(const char *format, RefR<INode> *args, int nArgs, bool structured);

bool EntityAIFull::GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const
{
  switch (action->GetType())
  {
    case ATSwitchMagazine:
      {
        const ActionWeaponIndex *actionTyped = static_cast<const ActionWeaponIndex *>(action);
        TurretContext context;
        if (FindTurret(actionTyped->GetGunner(), context))
        {
          WeaponModePtr ptr = actionTyped->GetWeaponIndex();
          if (!ptr.IsValid()) ptr = context._weapons->ValidatedCurrentWeapon();
          return FormatWeapon(params, action->GetType(), this, context, ptr);
        }
        else
        {
          RptF("Action %s - wrong weapon system", cc_cast(FindEnumName(action->GetType())));
          return false;
        }
      }
    case ATUseWeapon:
      {
        const ActionWeaponIndex *actionTyped = static_cast<const ActionWeaponIndex *>(action);

        TurretContext context;
        if (FindTurret(actionTyped->GetGunner(), context))
        {
          WeaponModePtr ptr = actionTyped->GetWeaponIndex();
          const WeaponModeType *mode = ptr.GetWeaponModeType();
          if (mode)
          {
            int left = 0;
            const Magazine *magazine = ptr.GetMagazine();
            // count magazines
            context._weapons->ForEachMagazine([&left, magazine](Magazine *mag) -> bool
            {
              if (!mag || mag->GetAmmo() == 0) return false; // continue
              if (mag->Type() == magazine->Type()) left++;
              return false; // continue
            });
            RefR<INode> subparams[2];
            subparams[0] = CreateTextASCII(NULL, mode->GetDisplayName());
            subparams[1] = CreateTextASCII(NULL, Format("%d", left));
            params.Add(FormatText(mode->_useActionTitle, subparams, 2, true));
            return true;
          }
          else
          {
            RptF("Action %s - wrong weapon %d", cc_cast(FindEnumName(action->GetType())), actionTyped->GetWeaponIndex());
            return false;
          }
        }
        else
        {
          RptF("Action %s - wrong weapon system", cc_cast(FindEnumName(action->GetType())));
          return false;
        }
      }
    case ATLoadMagazine:
    case ATLoadOtherMagazine:
    case ATLoadEmptyMagazine:
    case ATPutItem:
      {
        // ActionMagazineWithMuzzle not needed here
        const ActionMagazine *actionTyped = static_cast<const ActionMagazine *>(action);
        const Magazine *magazine = actionTyped->GetMagazine();
        if (magazine)
        {
          params.Add(CreateTextASCII(NULL, magazine->Type()->GetDisplayName()));
          return true;
        }
        else
        {
          RptF("Action %s - wrong magazine", cc_cast(FindEnumName(action->GetType())));
          return false;
        }
      }
  }
  return base::GetActionParams(params, action, unit);
}

void EntityAI::ProcessUserAction(RString name)
{
  const EntityAIType *type = GetType();
  for (int i=0; i<type->_userTypeActions.Size(); i++)
  {
    const UserTypeAction &act = GetType()->_userTypeActions[i];
    if (stricmp(act.name, name) == 0)
    {
      GameState *state = GWorld->GetGameState();
      GameVarSpace local(state->GetContext(), false);
      int gshandle = state->BeginContext(&local);

      state->VarSetLocal("this", GameValueExt(this), true, true);
      state->VarSetLocal("_name", name, true, true);
      state->Execute(act.statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

      state->EndContext(gshandle);
      return;
    }
  }
}

bool EntityAI::DoneUserAction(RString name)
{
  const EntityAIType *type = GetType();
  for (int i=0; i<type->_userTypeActions.Size(); i++)
  {
    const UserTypeAction &act = GetType()->_userTypeActions[i];
    if (stricmp(act.name, name) == 0)
    {
      GameState *state = GWorld->GetGameState();
      GameVarSpace local(state->GetContext(), false);
      int gshandle = state->BeginContext(&local);

      state->VarSetLocal("this", GameValueExt(this), true, true);
      state->VarSetLocal("_name", name, true, true);
      bool notDone = state->EvaluateBool(act.condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

      state->EndContext(gshandle);

      return !notDone;
    }
  }
  return true;
}

void EntityAI::StartActionProcessing(Action *action, AIBrain *unit)
{
  // process action directly by default
  action->Process(unit);
}


/*!
\patch 1.61 Date 5/30/2002 by Jirka
- Fixed: Enable put action for all owned explosives
*/
void DeleteVehicle(Entity *veh);
void EntityAI::PerformAction(const Action *action, AIBrain *unit)
{
  EntityAIFull *veh = unit->GetVehicle();
  switch (action->GetType())
  {
    case ATLightOn:
      DoAssert(GetNetworkManager().IsServerOwner());
      SetPilotLight(true);
      SetCollisionLight(true);
      return;
    case ATLightOff:
      DoAssert(GetNetworkManager().IsServerOwner());
      SetPilotLight(false);
      SetCollisionLight(false);
      return;
    case ATUser:
      if (unit)
      {
        const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
        const UserActionDescription *desc = FindUserAction(actionTyped->GetIndex());
        if (desc && desc->script.GetLength() > 0)
        {
          GameArrayType arguments;
          arguments.Add(GameValueExt(this));
          arguments.Add(GameValueExt(unit->GetPerson()));
          arguments.Add(GameValue((float)desc->id));
          arguments.Add(desc->param);
#if USE_PRECOMPILATION
          const char *ext = strrchr(desc->script, '.');
          if (ext && stricmp(ext, ".sqf") == 0)
          {
            RString FindScript(RString name);
            RString fullName = FindScript(desc->script);
            if (fullName.GetLength() > 0)
            {
              ScriptVM *script = new ScriptVM(fullName, GameValue(arguments), GWorld->GetMissionNamespace()); // mission namespace
              GWorld->AddScriptVM(script, false);
            }
          }
          else
#endif
          {
            Script *script = new Script(desc->script, GameValue(arguments), GWorld->GetMissionNamespace()); // mission namespace
            GWorld->AddScript(script, false);
          }
        }
      }
      return;
    case ATUserType:
      if (unit)
      {
        const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
        if(actionTyped->GetIndex() < GetType()->_userTypeActions.Size())
        {
          const UserTypeAction &act = GetType()->_userTypeActions[actionTyped->GetIndex()];

          GameState *state = GWorld->GetGameState();
          GameVarSpace local(state->GetContext(), false);
          int gshandle = state->BeginContext(&local);

          state->VarSetLocal("this", GameValueExt(this), true, true);
          state->VarSetLocal("_name", act.name, true, true);
          state->VarSetLocal("_person", GameValueExt(unit->GetPerson()), true, true);
          state->Execute(act.statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

          state->EndContext(gshandle);
        }
      }
      return;
    case ATHeal:
    case ATRepair:
    case ATRefuel:
      if (!veh->IsActionInProgress(MFReload))
        Supply(veh, action);
      return;
    case ATHealSoldier:
      if (!veh->IsActionInProgress(MFReload))
        SupplyInverse(veh, action);
      return;
    case ATRepairVehicle:
      if (!veh->IsActionInProgress(MFReload))
        SupplyInverse(veh, action);
      return;
    case ATFirstAid:
      if (!veh->IsActionInProgress(MFReload))
      {

      }
      return;
/*
    case ATCancelAction:
      CancelSupply();
      {
        // Attendant
        Person *person = dyn_cast<Person>(this);
        if (person) person->PutDownEnd();
      }
      unit->GetPerson()->PutDownEnd();
      return;
*/
  }
  base::PerformAction(action,unit);
}

void EntityAIFull::PerformAction(const Action *action, AIBrain *unit)
{
  //EntityAIFull *veh = unit->GetVehicle();
  switch (action->GetType())
  {
    case ATSwitchMagazine:
      {
        const ActionWeaponIndex *actionTyped = static_cast<const ActionWeaponIndex *>(action);

        TurretContext context; 
        if (FindTurret(actionTyped->GetGunner(), context))
        {
          SelectWeaponCommander(unit, context, actionTyped->GetWeaponIndex());
          OnWeaponSelected();
        }
      }
      return;
    case ATUseWeapon:
      {
        const ActionWeaponIndex *actionTyped = static_cast<const ActionWeaponIndex *>(action);
        
        TurretContextV context; 
        if (FindTurret(unconst_cast(FutureVisualState()), actionTyped->GetGunner(), context))
          FireWeapon(context, actionTyped->GetWeaponIndex(), NULL, false);
      }
      return;
    case ATLoadMagazine:
    case ATLoadOtherMagazine:
    case ATLoadEmptyMagazine:
      DoAssert(GetNetworkManager().IsServerOwner());
      if ( unit &&!IsActionInProgress(MFReload) && this==unit->GetVehicle())
      {
        const ActionMagazineAndMuzzle *actionTyped = static_cast<const ActionMagazineAndMuzzle *>(action);
       
        TurretContext context;
        if(actionTyped->GetWeapon() && actionTyped->GetMuzzle())
        {
          if (FindTurret(unit->GetPerson(), context) && CanFireUsing(context) && unit->GetPerson()==actionTyped->GetGunner())
            GunnerLoadMagazine(context, actionTyped->GetMagazine(), actionTyped->GetWeapon()->GetName(), actionTyped->GetMuzzle()->GetName());
          else
            CommanderLoadMagazine(actionTyped->GetMagazine(), actionTyped->GetWeapon()->GetName(), actionTyped->GetMuzzle()->GetName());
          GWorld->UI()->ShowWeaponInfo();
        }
      }
      return;
  }
  base::PerformAction(action,unit);
}

void EntityAIFull::GunnerLoadMagazine(const TurretContext &context, Magazine *magazine, RString weapon, RString muzzle)
{
  if (!magazine) return;

  WeaponModePtr ptr;
  context._weapons->ForEachMuzzle([&ptr, weapon, muzzle](const WeaponModePtr &p) -> bool
  {
    if (p.GetWeaponType()->GetName() == weapon && p.GetMuzzleType()->GetName() == muzzle)
    {
      ptr = p;
      return true; // done
    }
    return false; // continue
  });
  if (!ptr.IsValid()) return;

  ReloadMagazine(*context._weapons, ptr, magazine);
}

void EntityAIFull::CommanderLoadMagazine(Magazine *magazine, RString weapon, RString muzzle)
{
  // default implementation, changed in Transport
  TurretContext context;
  if (GetPrimaryGunnerTurret(context))
    GunnerLoadMagazine(context, magazine, weapon, muzzle);
}

static void GetReloadActions(EntityAIFull *veh, const std::function<void(const UIAction &action)> &add,
  const TurretContext &context, const WeaponModePtr &ptr, const MuzzleType *currentMuzzle)
{
  if (!ptr.IsValid()) 
    return; // HOTFIX: index out of range
  const Magazine *magazine = ptr.GetMagazine();
  const MagazineType *mType = magazine ? magazine->Type() : NULL;
  bool empty = false;
  bool shortcut = veh->GetType()->IsPerson(); // allow reload only for soldiers, 'R' is reserved for CMs in vehicles
  if (mType)
  {
    if (mType->_ammo && mType->_maxAmmo > 0)
      empty = magazine->GetAmmo() == 0;
    if (!empty && currentMuzzle->_autoReload &&
      (mType->_maxAmmo == 1 || (ptr.GetWeaponType()->_weaponType & MaskHardMounted) != 0))
    {
      // do not offer reload HandGrenade etc.
    }
    else
    {
      // magazine of the same type
      Magazine *best = context._weapons->FindBestMagazine(mType, 0);
      if (best)
      {
        UIAction action(new ActionMagazineAndMuzzle(
          empty ? ATLoadEmptyMagazine : ATLoadMagazine, veh, context._gunner, best,
          ptr.GetWeaponType(), ptr.GetMuzzleType()));
        add(action);
        shortcut = false;
      }
    }
  }
  else empty = true;
  // magazines of other type
  for (int i=0; i<currentMuzzle->_magazines.Size(); i++)
  {
    const MagazineType *change = currentMuzzle->_magazines[i];
    if (change == mType) continue;
    Magazine *best = context._weapons->FindBestMagazine(change, 0);
    if (best)
    {
      UIAction action(new ActionMagazineAndMuzzle(
        empty ? ATLoadEmptyMagazine : ATLoadOtherMagazine, veh, context._gunner, best,
        ptr.GetWeaponType(), ptr.GetMuzzleType()));
      if (!shortcut) action.shortcut = (UserAction)-1; 
      add(action);
      shortcut = false;
    }
  }
}

TypeIsSimple(MagazineType *)

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

void EntityAI::GetUserActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit, bool inHands)
{
  if (!unit) return;

#if _ENABLE_DATADISC
  const EntityAIType *type = GetType();
  if (type->_userTypeActions.Size() > 0)
  {
    GameState *state = GWorld->GetGameState();
    GameVarSpace local(state->GetContext(), false);
    int gshandle = state->BeginContext(&local);
    state->VarSetLocal("this", GameValueExt(this), true, true);
    state->VarSetLocal("_person", GameValueExt(unit->GetPerson()), true, true);
#if _HELISIM_PRECISE_INTERACTION
    bool inRadiusShow = true;
#endif
    for (int i=0; i<type->_userTypeActions.Size(); i++)
    {
      const UserTypeAction &act = type->_userTypeActions[i];
      if (inHands && !act.showInHands) continue; // when checking item in hands, action have to be enabled explicitly

      state->VarSetLocal("_name", act.name, true, true);

      // when gathering actions for commanding menu (!now), skip actions which are marked as "for player only"

#if _HELISIM_PRECISE_INTERACTION
      if(act.available & 1)
        if(unit != PilotUnit() && unit != CopilotUnit()) continue;
      if(act.available & 2)
        if(unit->GetVehicleIn() != this) continue;
      if(act.available & 4)
        if(unit->GetVehicleIn() == this) continue;

      Vector3 modelPos = VZero;
      if(act.positionIndex >= 0 && GetType()->GetShape())
      {
        //since in vehicle you don't have proper cursor, skip action 
        if(unit->GetVehicleIn() && GWorld->GetCameraType() == CamExternal) continue;

        int memLevel = GetType()->GetShape()->FindMemoryLevel();
        if(memLevel > -1) modelPos = AnimatePoint(RenderVisualState(), memLevel, act.positionIndex);
      }
#else
      int modelPosIndex = act.modelPositionIndex;
#endif
      
      {
#if _HELISIM_PRECISE_INTERACTION 

        //UserTypeAction already had position based action - do test now
        // userActions and UIActions will be solved separately at then end
        float radiusView = act.radiusView;
        //check if we ar looking at modelPos
        if(radiusView > 0 && GScene->GetCamera())
        {
          //action position direction
          Vector3 targetDir = RenderVisualState().PositionModelToWorld(modelPos) - cameraPosition;
          //minimal distance
          Vector3 distnceVect = cameraPosition +  camDir * (targetDir * camDir) - RenderVisualState().PositionModelToWorld(modelPos);

          //only hide action, so shortcut can be still used
          if( distnceVect.Size() > radiusView) 
          {
            if(act.shortcut >= 0) inRadiusShow = false;
            else continue;
          }
        }
#endif
      }

      if (!state->EvaluateBool(act.condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) continue; // mission namespace
   
      //vehicle type action
#if _HELISIM_PRECISE_INTERACTION    
      UIAction action(new ActionIndex(ATUserType, this, i, act.userActionID));
      action.show = act.show && inRadiusShow;
      action.showIn3D = act.showIn3D;
      action.available = act.available;
      action.radiusView = act.radiusView;
      action.textToolTip = act.textToolTip;
      action.userActionID = act.userActionID;
#else
      UIAction action(new ActionIndex(ATUserType, this, i));
#endif
      if (modelPosIndex >= 0) action._memoryPoints.Add(modelPosIndex);
      action.priority = act.priority -0.001 * i;
      action.showWindow = act.showWindow;
      action.hideOnUse = act.hideOnUse;
      action.shortcut = act.shortcut;

      add(action);
    }

    state->EndContext(gshandle);
  }
#endif
}

/*!
\param actions List to which result should be appended.
\param unit Unit that tries to perform action on this entity
\param now If true, only actions that can be performed
from current unit position are listed.
\patch 1.11 Date 08/03/2001 by Jirka
- Improved: user actions enabled also for AI units
\patch 1.34 Date 12/6/2001 by Jirka
- Fixed: commander can switch ligths only if not in cargo
\patch 1.89 Date 10/25/2002 by Ondra
- Fixed: Custom action inside vehicle was enable only until vehicle moved.
*/

void EntityAI::GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit)
{
  if (!unit) return;

  base::GetActions(add, unit);

  // user actions
  // FIX
#if _HELISIM_PRECISE_INTERACTION
  Camera *camera = GScene->GetCamera();
  if(!camera) return;
  Vector3 cameraPosition = camera->Position();

  Vector3Val cameraRelPos = PositionWorldToModel(cameraPosition);
#endif

  if (_userActions.Size() > 0)
  {
    for (int i=0; i<_userActions.Size(); i++)
    {
      UserActionDescription &desc = _userActions[i];

#if _HELISIM_PRECISE_INTERACTION
      //if radiusView > 0, ok is checked elswhere against memory point
      if(desc.radiusView <= 0 && !ok) continue;

      if(desc.available & 1)
        if(unit != PilotUnit() && unit != CopilotUnit()) continue;
      if(desc.available & 2)
        if(unit->GetVehicleIn() != this) continue;
      if(desc.available & 4)
        if(unit->GetVehicleIn() == this) continue;
#endif

      if (desc.condition.GetLength() > 0)
      {
        GameVarSpace local(false);
        GameState *gstate = GWorld->GetGameState();
        int gshandle = gstate->BeginContext(&local);
        gstate->VarSetLocal("_this", GameValueExt(unit->GetPerson()), true);
        gstate->VarSetLocal("_target", GameValueExt(this), true);
#if USE_PRECOMPILATION
        bool ok = gstate->EvaluateBool(desc.condition, desc.conditionExp, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
#else
        bool ok = gstate->EvaluateBool(desc.condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
#endif
        gstate->EndContext(gshandle);
        if (!ok) continue;
      }

      //user defined action
#if _HELISIM_PRECISE_INTERACTION
      UIAction action(new ActionIndex(ATUser, this, desc.id, desc.userActionID));
      if(desc.modelPositions.GetLength()>0) action.modelPositions.Add(desc.modelPositions);
      action.modelPositions.Compact();
      action.radius = desc.radius;
      action.radiusView = desc.radiusView;
      action.showIn3D = desc.showIn3D;
      action.available = desc.available;
      action.textToolTip = desc.textToolTip;
      action.userActionID = desc.userActionID;
#else
      UIAction action(new ActionIndex(ATUser, this, desc.id));
#endif
      action.priority = desc.priority - 0.001 * desc.id;
      action.showWindow = desc.showWindow;
      action.hideOnUse = desc.hideOnUse;
      action.shortcut = desc.shortcut;
      add(action);
    }
  }

  // user type actions
  GetUserActions(add, unit, false);

  if (unit && !IsDamageDestroyed() )
  {
    if (_supply)
    {
      if (unit->IsEnemy(GetTargetSide())) return;
      _supply->GetActions(this, add, unit, true);
    }

    if(this->CommanderUnit() &&  this->CommanderUnit()->IsFreeSoldier()) //you cannot take action from medic
    { //unit does not have supply, but needs to be healed 
      EntityAIFull *veh = unit->GetVehicle();
      if (veh->IsLocal() && this->CommanderUnit() && this->CommanderUnit()->IsFreeSoldier()) 
      {
        bool soldier = unit->IsFreeSoldier();
        const EntityAIType *vehType = veh->GetType();

        if (vehType->IsAttendant() && soldier)
        {
          if (!this->IsAnimal() && this->NeedsAmbulance() > 0)
          {
            UIAction action(new ActionBasic(ATHealSoldier, this));
            int index = GetType()->GetSupplyPointIndex();
            if (index >= 0) action._memoryPoints.Add(index);
            add(action);
          }
        }
        else
        {
          if (this->CommanderUnit())
            if (!this->IsAnimal() && this->CommanderUnit()->GetLifeState() == LifeStateUnconscious && soldier)
            {
              UIAction action(new ActionBasic(ATFirstAid, this));
              add(action);
            }
        }
      }
    }

    Transport *transport = dyn_cast<Transport>(this);
    if(transport) //you cannot take action from medic
    { //unit does not have supply, but needs to be healed 
      EntityAIFull *veh = unit->GetVehicle();
      if (veh->IsLocal()) 
      {
        bool soldier = unit->IsFreeSoldier();
        const EntityAIType *vehType = veh->GetType();
        if (vehType->IsEngineer() && soldier)
        {
          float damage =  transport->GetMaxHitCont();
          if (damage > 0.7f)
          {
            UIAction action(new ActionBasic(ATRepairVehicle, this));
            add(action);
          }
        }
      }
    }
  }
}



#if _HELISIM_PRECISE_INTERACTION
bool EntityAI::CheckDrawActions3D(const UIAction *action, AIBrain *unit, CameraType cameraType)
{
  if(action->modelPositions.Size()<=0) return false;

  if((action->showIn3D & 1) == 0) return false;
  if((action->showIn3D & 2) > 0 && (unit != PilotUnit() && unit != CopilotUnit())) return false;
  if((action->showIn3D & 4) > 0 && (unit->GetVehicleIn() != this)) return false;
  if((action->showIn3D & 8) > 0 && (unit->GetVehicleIn() == this)) return false;
  if((action->showIn3D & 16) > 0 && (cameraType == CamExternal)) return false;
  if((action->showIn3D & 32) > 0 && (cameraType == CamInternal)) return false;
  if((action->showIn3D & 64) > 0 && (cameraType == CamGunner)) return false;

  //since in vehicle in external camera you don't have proper cursor, skip action 
  if(unit->GetVehicleIn() && GWorld->GetCameraType() == CamExternal) return false;


  return true;
}

void EntityAI::DrawActionsAnimatePosition(UIActions *actions,CameraType cameraType,const Camera *camera,bool isUnitIn)
{
  //const Camera *camera = GScene->GetCamera();
  //find memory point
  LODShape *shape = GetType()->GetShape(); 
  if(!shape) return;
  const Shape* mem = shape->MemoryLevel();

  int n = actions->Size();
  for(int i = 0; i< n; i++)
  {
    for(int j = 0; j< (*actions)[i].modelPositions.Size(); j++)
    {
      int positionIndex = (shape && mem) ? shape->PointIndex(mem,(*actions)[i].modelPositions[j]) : -1;
      //if memory point not found - do not show in 3D, skip radius and radiusView testing
      if(positionIndex<0) continue;

      Vector3 modelPosition;
      int memLevel = shape->FindMemoryLevel();
      if(memLevel > -1) 
      {
        //if this mem. point is valid, add an action with animated position(this is only for drawing, so we dont mind duplicity)
        UIAction act = (*actions)[i];


        Vector3 pos = AnimatePoint(RenderVisualState(), memLevel, positionIndex);
        if (isUnitIn && cameraType == CamInternal)
        { //to reduce float errors
          pos -= GWorld->GetCamaraTransform().Position();
          act.position = RenderVisualState().DirectionModelToWorld(pos);
        }
        else
        {
          act.position = RenderVisualState().PositionModelToWorld(pos) - camera->Position();
        }
        //add at the end of array
        actions->Add(act);
      }
      else continue;
    }
  }
  //delete elements without updated position
  actions->Delete(0, n);
}

void EntityAI::GetDrawActions3D(UIActions &actions, AIBrain *unit)
{
  if (!unit) return;
  bool isUnitIn = unit->GetVehicleIn() == this;
  const Camera* camera = GScene->GetCamera();

  CameraType cameraType = GWorld->GetCameraType();
  ProtectedVisualState<const VisualState> vs = RenderVisualStateScope();
  // user actions
  // FIX

  //filter engine action out
  for (int i = actions.Size() -1 ; i>= 0; i--)
  {
    if(!CheckDrawActions3D(&actions[i],unit, cameraType)) actions.Delete(i);
  }
  DrawActionsAnimatePosition(&actions,cameraType, camera, isUnitIn);

  //resolve get in action after DrawActionsAnimatePosition (get in action use diferent action positions)
  GetDrawGetInActions3D(actions, unit);


  //user actions
  if (_userActions.Size() > 0)
  {
    bool ok = true;

    if (ok)
    {
      for (int i=0; i<_userActions.Size(); i++)
      {
        UserActionDescription &desc = _userActions[i];

        if((desc.showIn3D & 1) == 0) continue;
        if((desc.showIn3D & 2) > 0 && (unit != PilotUnit() && unit != CopilotUnit())) continue;
        if((desc.showIn3D & 4) > 0 && (unit->GetVehicleIn() != this)) continue;
        if((desc.showIn3D & 8) > 0 && (unit->GetVehicleIn() == this)) continue;
        if((desc.showIn3D & 16) > 0 && (cameraType == CamExternal)) continue;
        if((desc.showIn3D & 32) > 0 && (cameraType == CamInternal)) continue;
        if((desc.showIn3D & 64) > 0 && (cameraType == CamGunner)) continue;

        //since in vehicle you don't have proper cursor, skip action 
        if(unit->GetVehicleIn() && GWorld->GetCameraType() == CamExternal) continue;

        if (desc.condition.GetLength() > 0)
        {
          GameVarSpace local(false);
          GameState *gstate = GWorld->GetGameState();
          int gshandle = gstate->BeginContext(&local);
          gstate->VarSetLocal("_this", GameValueExt(unit->GetPerson()), true);
          gstate->VarSetLocal("_target", GameValueExt(this), true);
#if USE_PRECOMPILATION
          bool ok = gstate->EvaluateBool(desc.condition, desc.conditionExp, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
#else
          bool ok = gstate->EvaluateBool(desc.condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
#endif
          gstate->EndContext(gshandle);
          if (!ok) continue;
        }

        //find memory point
        LODShape *shape = GetType()->GetShape(); 
        const Shape* mem = shape->MemoryLevel();
        int positionIndex = (shape && mem) ? shape->PointIndex(mem,desc.modelPositions) : -1;
        //if memory point not found - do not show in 3D, skip radius and radiusView testing
        if(positionIndex<0) continue;

        Vector3 modelPosition;
        int memLevel = shape->FindMemoryLevel();
        if(memLevel > -1) modelPosition = AnimatePoint(RenderVisualState(), memLevel, positionIndex);
        else continue;

        //user defined action
        UIAction action(new ActionIndex(ATUser, this, desc.id, desc.userActionID));
        if (isUnitIn && cameraType == CamInternal)
        { //to reduce float errors
          modelPosition -= GWorld->GetCamaraTransform().Position();
          action.position = RenderVisualState().DirectionModelToWorld(modelPosition);
        }
        else
        {
          action.position = RenderVisualState().PositionModelToWorld(modelPosition) - camera->Position();
        }
        action.priority = desc.priority - 0.001 * desc.id;
        action.showWindow = desc.showWindow;
        action.hideOnUse = desc.hideOnUse;
        action.shortcut = desc.shortcut;  
        action.showIn3D = desc.showIn3D;
        action.radiusView = desc.radiusView;
        action.radius = desc.radius;
        action.textToolTip = desc.textToolTip;
        action.userActionID  = desc.userActionID;
        actions.Add(action);
      }
    }
  }

  // user type actions
#if _ENABLE_DATADISC
  const EntityAIType *type = GetType();
  if (type->_userTypeActions.Size() > 0)
  {
    // TODO: when in vehicle, we should consider aiming position of the person instead
    // however, AimingPosition currently does not work for men inside vehicles

    GameState *state = GWorld->GetGameState();
    GameVarSpace local(state->GetContext(), false);
    int gshandle = state->BeginContext(&local);
    state->VarSetLocal("this", GameValueExt(this), true, true);

    for (int i=0; i<type->_userTypeActions.Size(); i++)
    {
      const UserTypeAction &act = type->_userTypeActions[i];
      // when gathering actions for commanding menu (!now), skip actions which are marked as "for player only"
      if((act.showIn3D & 1) == 0) continue;
      if((act.showIn3D & 2) > 0 && (unit != PilotUnit() && unit != CopilotUnit())) continue;
      if((act.showIn3D & 4) > 0 && (unit->GetVehicleIn() != this)) continue;
      if((act.showIn3D & 8) > 0 && (unit->GetVehicleIn() == this)) continue;
      if((act.showIn3D & 16) > 0 && (cameraType == CamExternal)) continue;
      if((act.showIn3D & 32) > 0 && (cameraType == CamInternal)) continue;
      if((act.showIn3D & 64) > 0 && (cameraType == CamGunner)) continue;

      //since in vehicle you don't have proper cursor, skip action 
      if(unit->GetVehicleIn() && GWorld->GetCameraType() == CamExternal) continue;

      Vector3 modelPos = VZero;
      if(act.positionIndex >= 0 && GetType()->GetShape())
      {
        int memLevel = GetType()->GetShape()->FindMemoryLevel();
        if(memLevel > -1) modelPos = AnimatePoint(RenderVisualState(), memLevel, act.positionIndex);
      }

      if (!state->EvaluateBool(act.condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) continue; // mission namespace
      //vehicle type action
      UIAction action(new ActionIndex(ATUserType, this, i, act.userActionID));
      if(isUnitIn && cameraType == CamInternal)
      {
        modelPos -= GWorld->GetCamaraTransform().Position();
        action.position = RenderVisualState().DirectionModelToWorld(modelPos);
      }
      else
      {
        action.position = RenderVisualState().PositionModelToWorld(modelPos) - camera->Position();
      }
      action.priority = act.priority -0.001 * i;
      action.showWindow = act.showWindow;
      action.hideOnUse = act.hideOnUse;
      action.shortcut = act.shortcut;
      action.show = act.show;
      action.showIn3D = act.showIn3D;
      action.radiusView = act.radiusView;
      action.radius = act.radius;
      action.textToolTip = act.textToolTip;
      action.userActionID  = act.userActionID;
      actions.Add(action);
    }

    state->EndContext(gshandle);
  }
#endif
}
#endif

void EntityAIFull::GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit)
{
  if (!unit || !unit->GetPerson())
    return;

  PROFILE_SCOPE_EX(entAc,colAc);
  base::GetActions(add,unit);
  
  if (unit->GetVehicle() == this && !unit->IsInCargo())
  {
    if (CommanderUnit() == unit)
    {
      bool primGunnerDone = false;
      TurretContext context;
      if (
        (FindTurret(unit->GetPerson(), context) && CanFireUsing(context)) || // controls own weapons
        (primGunnerDone=GetPrimaryGunnerTurret(context))) // controls primary turret
      {
        WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();

        if (EnableWeaponManipulation())
        {
          // magazines
          const MuzzleType *currentMuzzle = NULL;
          if (selected.IsValid())
          {
            currentMuzzle = selected.GetMuzzleType();
            if (CanReloadCurrentWeapon())
            {
              GetReloadActions(this, add, context, selected, currentMuzzle);
            }
            else
            {
              UIAction action(new ActionWeaponIndex(ATSwitchMagazine, this, context._gunner, selected));
              action.priority += -0.01 * selected._muzzle - 0.001 * selected._mode; // TODO: selected._weapon
              add(action);
            }
          }
          // background reloading
          context._weapons->ForEachMuzzle([this, currentMuzzle, &add, &context](const WeaponModePtr &ptr) -> bool
          {
            const MuzzleType *muzzle = ptr.GetMuzzleType();
            if (muzzle != currentMuzzle && muzzle->_backgroundReload)
            {
              GetReloadActions(this, add, context, ptr, muzzle);
            }
            return false; // continue
          });
        }
        else // !EnableWeaponManipulation()
        {
          if (selected.IsValid() && !CanReloadCurrentWeapon())
          {
            UIAction action(new ActionWeaponIndex(ATSwitchMagazine, this, context._gunner, selected));
            action.priority += -0.01 * selected._muzzle - 0.001 * selected._mode; // TODO: selected._weapon
            add(action);
          }
        }
      }

      //if prim. gunner wasn't set in preview block and prim. gunner exist and unit(player) isn't in prim.turret then list prim. gunner weapons too
      if (!primGunnerDone && GetPrimaryGunnerTurret(context) && context._gunner!=unit->GetPerson()) 
      {
        WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();
        const MuzzleType *currentMuzzle = NULL;
        if (selected.IsValid())
        {
          currentMuzzle = selected.GetMuzzleType();
          if (CanReloadCurrentWeapon())
          {
            GetReloadActions(this, add, context, selected, currentMuzzle);
          }
          else
          {
            UIAction action(new ActionWeaponIndex(ATSwitchMagazine, this, context._gunner, selected));
            action.priority += -0.01 * (selected._muzzle + 100); // TODO: selected._weapon
            add(action);
          }
        }
      }
    }
    else
    {
      // gunner units
      TurretContext context;
      if (FindTurret(unit->GetPerson(), context))
      {
        WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();
        if (EnableWeaponManipulation())
        {
          // magazines
          const MuzzleType *currentMuzzle = NULL;

          if (selected.IsValid() && CanReloadCurrentWeapon())
          {
            currentMuzzle = selected.GetMuzzleType();
            GetReloadActions(this, add, context, selected, currentMuzzle);
          }
          // background reloading
          context._weapons->ForEachMuzzle([this, &add, &context, currentMuzzle](const WeaponModePtr &ptr) -> bool
          {
            const MuzzleType *muzzle = ptr.GetMuzzleType();
            if (muzzle != currentMuzzle && muzzle->_backgroundReload)
            {
              GetReloadActions(this, add, context, ptr, muzzle);
            }
            return false; // continue
          });
        }
      }
    }

    TurretContext context;
    if (FindTurret(unit->GetPerson(), context))
    {
      // lights on / off
      if (context._turret &&  context._turret->SomeLights())
      {
        if (context._turret->IsLightOn())
        {
          UIAction action(new ActionBasic(ATSearchLightOff, unit->GetVehicle()));
          add(action);
        }
        else
        {
          //if (GScene->MainLight()->NightEffect() > 0)
          {
            UIAction action(new ActionBasic(ATSearchLightOn, unit->GetVehicle()));
            add(action);
          }
        }
      }
    }
  }
}

#if _HELISIM_PRECISE_INTERACTION
void EntityAIFull::GetDrawActions3D(UIActions &actions, AIBrain *unit)
{
  if (!unit || !unit->GetPerson())
    return;

  base::GetDrawActions3D(actions,unit);
}
#endif


/*!
  \param text displayed text of action
  \param script this script will be performed when action is activated
  \return id of action
*/
#if _HELISIM_PRECISE_INTERACTION
int EntityAI::AddUserAction(RString text, RString script, GameValuePar param, float priority, bool showWindow, bool hideOnUse, UserAction shortcut, RString condition,
                            RString modelPosition, float radius, float radiusView, int showIn3D, int available, RString textDefault, RString textToolTip)
#else
int EntityAI::AddUserAction(RString text, RString script, GameValuePar param, float priority, bool showWindow, bool hideOnUse, UserAction shortcut, RString condition)
#endif
{
  int index = _userActions.Add();
  UserActionDescription &action = _userActions[index];
  action.id = _nextUserActionId++;
  action.text = text;
  action.script = script;
  action.param = param;
  action.priority = priority;
  action.showWindow = showWindow;
  action.hideOnUse = hideOnUse;
  action.shortcut = shortcut;
  action.condition = condition;

#if _HELISIM_PRECISE_INTERACTION
  action.modelPositions = modelPosition;
  action.radius = radius;
  action.radiusView = radiusView;
  action.showIn3D = showIn3D;
  action.available = available;
  action.textDefault = textDefault;
  action.textToolTip = textToolTip;
  action.userActionID = action.id;
#endif

#if USE_PRECOMPILATION
  GGameState.CompileMultiple(condition, action.conditionExp, GWorld->GetMissionNamespace()); // mission namespace
#endif
  return action.id;
}


#if _HELISIM_PRECISE_INTERACTION
void EntityAI::SetUserActionText(int index, RString text, RString textDefault, RString textToolTip)
#else
void EntityAI::SetUserActionText(int index, RString text)
#endif
{
  for(int i = 0; i< _userActions.Size(); i++)
  {
    UserActionDescription &action = _userActions[i];
    if (action.id == index)
    {
      action.text = text;
#if _HELISIM_PRECISE_INTERACTION
      action.textDefault = textDefault;
      action.textToolTip = textToolTip;
#endif
      break;;
    }
  }
}

/*!
  \param id of action
*/
void EntityAI::RemoveUserAction(int id)
{
  for (int i=0; i<_userActions.Size(); i++)
  {
    if (_userActions[i].id == id)
    {
      _userActions.Delete(i);
      return;
    }
  }
}

/*!
  \param id of action
*/
const UserActionDescription *EntityAI::FindUserAction(int id) const
{
  for (int i=0; i<_userActions.Size(); i++)
  {
    if (_userActions[i].id == id)
      return &_userActions[i];
  }
  return NULL;
}

DEFINE_ENUM(EntityEvent,EE,ENTITY_EVENT_ENUM)

DEFINE_ENUM(MissionEvent,ME,MISSION_EVENT_ENUM)

class GameArrayTypeRef
{
  const GameArrayType &_ref;
  public:
  GameArrayTypeRef(const GameArrayType &ref):_ref(ref){}
  operator const GameArrayType &() const {return _ref;}
  operator GameValue() const {return GameValue(_ref);}
};

bool EntityAI::IsEventHandler(EntityEvent event) const
{
  const EventHandlers &handlers = GetEventHandlers(event);
  if (!handlers.IsEmpty()) return true;
  RString handler = GetType()->_eventHandlers[event];
  if (handler.GetLength()>0) return true;
  return false;
}

// Postpone void event handlers evaluation wrapper, used for EH evaluation after simulation
class EventVoidCall: public IPostponedEvent
{
  OLinkO(EntityAI) _entityAI;
  EntityEvent _event;
  const GameValue _value;

public:
  EventVoidCall(EntityAI *entityAI, EntityEvent event, const GameValue &value)
    : _entityAI(entityAI), _event(event), _value(value) 
  {
  }

  void DoEvent() const
  {
    if (_entityAI)
    {
      _entityAI->DoEvent(_event, _value);
    }
  }

  USE_FAST_ALLOCATOR
};

void EntityAI::SetLocal(bool local)
{
  if (local != _local) OnEvent(EELocal, local);
  base::SetLocal(local);
}

void EntityAI::DoVehicleInit(RString init)
{
  GameState *state = GWorld->GetGameState();
  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);

  state->VarSetLocal("this", GameValueExt(this, GameValExtObject), true, true);
  state->Execute(init, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

  state->EndContext(gshandle);
}

void EntityAI::OnVehicleInit(RString init)
{
  if (init.IsEmpty()) return;
  #if 0
  struct PostponedInit: public IPostponedEvent
  {
    OLink(EntityAI) vehicle;
    RString init;

    PostponedInit(EntityAI *vehicle, RString init):vehicle(vehicle),init(init){}
    virtual void DoEvent() const
    {
      if (vehicle)
      {
        vehicle->DoVehicleInit(init);
      }
    }
          
  };
  PostponedInit *initEvent = new PostponedInit(this,init);
  GWorld->AppendPostponedEvent(initEvent);
  #else
  DoVehicleInit(init);
  #endif
}

void EntityAI::OnEvent(EntityEvent event, const GameValue &pars)
{
  #if 1
  // done by postponing init instead
  if (event==EEInit)
  {
    // init events need to be processed immediately to prevent issues with missing "init" field
    DoEvent(event,pars);
  }
  else
  #endif
  {
    GWorld->AppendPostponedEvent(new EventVoidCall(this, event, pars));
  }
}

/*!
\patch 1.82 Date 8/20/2002 by Ondra
- New: Addons and scripting: Customizable event handlers for events:
  Killed, Hit, Dammaged, GetIn, GetOut, Init,
  Engine, Gear, Fuel, Fired, IncomingMissile.
*/
void EntityAI::DoEvent(EntityEvent event, const GameValue &pars)
{
  PROFILE_SCOPE_EX(entEH, *);
  if (PROFILE_SCOPE_NAME(entEH).IsActive())
  {
    PROFILE_SCOPE_NAME(entEH).AddMoreInfo(Format("%s:%s", cc_cast(GetDebugName()), cc_cast(FindEnumName(event))));
  }

  const EventHandlers &handlers = GetEventHandlers(event);
  handlers.Process(pars);

  {
    // execute type based event handler
    RString handler = GetType()->_eventHandlers[event];
    GameVarSpace local(false);
    GameState *gstate = GWorld->GetGameState();
    int gshandle = gstate->BeginContext(&local);
    gstate->VarSetLocal("_this",pars,true);
    gstate->Execute(handler, GameState::EvalContext::_reportUndefined, GWorld->GetMissionNamespace()); // mission namespace
    gstate->EndContext(gshandle);
  }
}

GameValue EntityAI::OnEventRet(EntityEvent event, const GameValue &pars)
{
  PROFILE_SCOPE_EX(entEH, *);
  if (PROFILE_SCOPE_NAME(entEH).IsActive())
  {
    PROFILE_SCOPE_NAME(entEH).AddMoreInfo(Format("%s:%s", cc_cast(GetDebugName()), cc_cast(FindEnumName(event))));
  }

  GameValue last;
  {
    // execute type based event handler
    RString handler = GetType()->_eventHandlers[event];
    GameVarSpace local(false);
    GameState *gstate = GWorld->GetGameState();
    int gshandle = gstate->BeginContext(&local);
    gstate->VarSetLocal("_this",pars,true);
    last = gstate->EvaluateMultiple(handler, GameState::EvalContext::_reportUndefined, GWorld->GetMissionNamespace()); // mission namespace
    gstate->EndContext(gshandle);
  }
  const EventHandlers &handlers = GetEventHandlers(event);
  GameValue retval = handlers.ProcessRet(pars);
  if (retval.GetType() & GameNothing) return last;
  else return retval;
  }


void EntityAI::OnEvent(EntityEvent event, bool par1)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event, float par1)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  OnEvent(event,value);
}


void EntityAI::OnEvent(EntityEvent event, RString par1)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event, RString par1, EntityAI *par2)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValueExt(par2));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event, EntityAI *ea2, RString name, float dist, Vector3 where) {
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(ea2));
  arguments.Add(GameValue(name));
  arguments.Add(GameValue(dist));
  GameValue v2 = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arg2 = v2;
  arg2.Add(GameValue(where[0]));
  arg2.Add(GameValue(where[1]));
  arg2.Add(GameValue(where[2]));
  arguments.Add(GameValue(arg2));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event, RString par1, float par2)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event, EntityAI *ea2, float par1, float par2)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(ea2));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  OnEvent(event,value);
}


static GameValue SkinningToGameValue(const SkinnedPosition &pos)
{
  GameValue skinningValue = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &skinning = skinningValue;
  {
    GameValue originalValue = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType &original = originalValue;
    original.Add(pos.originalPosition.X());
    original.Add(pos.originalPosition.Z());
    original.Add(pos.originalPosition.Y());
    skinning.Add(original);
  }
  for (int i=0; i<pos.bones.Size(); i++)
  {
    GameValue boneValue = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType &bone = boneValue;
    bone.Add(GameValue(pos.bones[i].boneName));
    bone.Add(GameValue(pos.bones[i].weight));
    skinning.Add(boneValue);
  }
  return skinningValue;
}

void EntityAI::OnHitPartEvent(EntityEvent event,AutoArray<HitEventElement> &data)
{
  if (!IsEventHandler(event)) return;

  GameValue gvHitEventsOfArray = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &gatHitEventsOfArray = gvHitEventsOfArray;

  for( int i = 0; i < data.Size(); ++i)
  {
    HitEventElement &hEvent = data[i];

    GameValue gpos      = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameValue gvel      = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameValue gcomStr   = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameValue gAmmoStat = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameValue gNormal   = GWorld->GetGameState()->CreateGameValue(GameArray);
    
    GameArrayType &posArgu = gpos;
    posArgu.Add(GameValue(hEvent._pos.X()));
    posArgu.Add(GameValue(hEvent._pos.Z()));
    posArgu.Add(GameValue(hEvent._pos.Y()));

    GameArrayType &velArgu = gvel;
    velArgu.Add(GameValue(hEvent._velocity.X()));
    velArgu.Add(GameValue(hEvent._velocity.Z()));
    velArgu.Add(GameValue(hEvent._velocity.Y())); 

    GameArrayType &normArgu = gNormal;
    normArgu.Add(GameValue(hEvent._normal.X()));
    normArgu.Add(GameValue(hEvent._normal.Z()));
    normArgu.Add(GameValue(hEvent._normal.Y())); 

    GameArrayType &comArgu = gcomStr;
    for( int i = 0; i < hEvent._component.Size(); ++i)
      comArgu.Add(GameValue(hEvent._component[i]));

    GameArrayType &ammoStat = gAmmoStat;

    if(hEvent._shot) // Quick fix, requires looking into how to create a munition ID
    {
      ammoStat.Add(GameValue(hEvent._shot->hit)); 
      ammoStat.Add(GameValue(hEvent._shot->indirectHit)); 
      ammoStat.Add(GameValue(hEvent._shot->indirectHitRange)); 
      ammoStat.Add(GameValue(hEvent._shot->explosive)); 
      ammoStat.Add(hEvent._shot->GetName());
    }
    else //use the impulse value as hit
    {
      ammoStat.Add(hEvent._impulse);
      ammoStat.Add(0.0f);
      ammoStat.Add(0.0f);
      ammoStat.Add(0.0f);
      ammoStat.Add(RString(""));
    }

    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType &arguments = value;
    arguments.Add(GameValueExt(this));  // object it hit.
    arguments.Add(GameValueExt(hEvent._shoter));// who fired the shot
    arguments.Add(GameValueExt(nullptr,GameValExtObject));  // shot cannot be passed, not MP compatible
    arguments.Add(gpos);
    arguments.Add(gvel);
    arguments.Add(gcomStr);
    arguments.Add(gAmmoStat);
    arguments.Add(gNormal);
    arguments.Add(hEvent._radius);
    arguments.Add(GameValue(hEvent._surfaceType));
    arguments.Add(hEvent._directHit);

    arguments.Add(SkinningToGameValue(hEvent._skinning));

    gatHitEventsOfArray.Add(value);
  }

  OnEvent(event,gatHitEventsOfArray);
}

void EntityAI::OnHitPart(const CollisionInfo &info, const AmmoType *shot, EntityAI *parent)
{
  if(IsEventHandler(EEHitPart))
  {
    SkinnedPosition skinning(this);
    AutoArray<RString> namedComponents;
    RString surfaceType; 
    float radius = 1.0f;

    LODShape *shp = GetShape();
    int componentIndex = info.component;
    Vector3 pos = info.pos;
    if (shp && componentIndex >= 0)
    {
      const Shape *fire = shp->FireGeometryLevel();
      if (fire)
      {
        const ConvexComponents &fireComponent = shp->GetFireComponents();
        Assert(componentIndex >= 0 && componentIndex < fireComponent.Size());
        const Ref<ConvexComponent> component = fireComponent.Get(componentIndex);
        radius = component->GetRadius();
        const SurfaceInfo* surf = component->GetSurfaceInfo();
        if (surf)
          surfaceType = surf->_surfaceType;

        for (int i = 0; i < fire->NNamedSel(); ++i)
        {
          // get the first named selection
          // each named selection has an array of faces indices.
          // those face indices, have indices of vertex's.
          const NamedSelection namSel = fire->NamedSel(i);
          const Selection faceIndecies = namSel.Faces();

          if(component->NPlanes() > 0)
          {
            int cFaceIndicie = component->GetPlaneIndex(0);
            for (int x = 0; x < faceIndecies.Size(); ++x)
            {
              if (faceIndecies[x] == cFaceIndicie)
              {
                namedComponents.Add(namSel.Name());
                break;
              }
            }
          }
        }
        // get skinning information
        component->GetSkinning(skinning,fire,shp);
        Vector3 modelPos = RenderVisualState().PositionWorldToModel(info.pos);
        auto deanimated = DeanimatePoint(RenderVisualState(),shp->FindFireGeometryLevel(),skinning,modelPos);
        if (!deanimated.second)
        {
          skinning.bones.Clear();
        }
        else
        {
          skinning.originalPosition = deanimated.first;
        }
      }

    }
    AutoArray<HitEventElement> list;
    int index = list.Add();
    Vector3Val normal = info.dirOutNotNorm.Normalized();
    list[index].Init(shot, parent, pos,  FutureVisualState()._speed, namedComponents, normal, radius, surfaceType, true, 0, &skinning);
    OnHitPartEvent(EEHitPart, list);
  }
}

void EntityAI::OnEvent(EntityEvent event, EntityAI *par1, float par2)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(par1));
  arguments.Add(GameValue(par2));
  OnEvent(event,value);
}


void EntityAI::OnEvent(EntityEvent event, EntityAI *par1)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(par1));
  OnEvent(event,value);
}

void EntityAI::OnEvent(EntityEvent event, EntityAI *par1, EntityAI *par2)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(par1));
  arguments.Add(GameValueExt(par2));
  OnEvent(event,value);
}

//! event handler for anim-hooks
void EntityAI::OnEvent(EntityEvent event, int userType, RString par2)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValue((float)userType));
  arguments.Add(GameValue(par2));
  OnEvent(event,value);
}

void EntityAI::PerformRelock(AILockerSavedState &state) const
{
  if (_locked)
    _locker->Relock(state);
}

void EntityAI::PerformUnlock(AILockerSavedState *state) const
{
  if (_locked)
    _locker->Unlock(state);
}

void EntityAI::LockPosition()
{
  if (_static)
    return; // no lock for buildings ...

  // helper functions for lock/unlock
  // lock current position and area around it
  float radius=CollisionSize()*1.5f+2.5f;
  saturateMax(radius,4);
  _locked=true;
  // make some shift aside - we prefer having right side free
  // FIX: AimingPosition did not work well in Y axis
  Vector3Val lockPos = FutureVisualState().Position();
  // this also helps getting in (drivers gets in from the left)
  // the only place where Lock is performed (_locked == true)
  bool soldier = CommanderUnit() ? CommanderUnit()->GetPerson() == this : false;
  bool canWait = PilotUnit() && IsAbleToMove() && !IsDamageDestroyed();
  _locker->Lock(lockPos, radius, soldier, canWait, GetShape()->GeometrySphere());
}

void EntityAI::UnlockPosition()
{
  // lock some area around vehicle
  PerformUnlock();
  _locker->Move();
  _locked=false;
}

#if 0 // _PROFILE
#pragma optimize("",off)
#endif

void EntityAI::AvoidCollision(float deltaT, float &speedWanted, float &headChange)
{
  PROFILE_SCOPE_EX(entAC,sim);

  AIBrain *agent = PilotUnit();
  if (!agent)
    return;

  const float neutralAside = 0.0f;

  _avoidAsideWanted=neutralAside;

  if (agent->GetPlanningMode() == AIBrain::LeaderDirect) return;
  // if we are stopped, do not try to avoid
  // TODO: some vehicles (esp. men) should go out of way of tanks
  const float maxSpeedStopped=0.05;
  if( fabs(speedWanted)<=maxSpeedStopped ) return;
  // avoid collisions
  float mySize = CollisionSize(); // assume vehicle is not round
  float mySpeedSize = fabs(FutureVisualState().ModelSpeed()[2]);
  float maxSpeed = GetType()->GetMaxSpeedMs();
  float myBrakeDist = Square(mySpeedSize)*0.2;
  float myBrakeTime = mySpeedSize*0.05;
  float myMaxBrakeTime = maxSpeed*0.05;
  float maxDist=floatMax(GetPrecision()*4,5)+myBrakeDist*1.3;
  // check if we are on collision course
  VehicleCollisionBuffer ret;
  float gapFactor=Interpolativ(mySpeedSize,maxSpeed*0.5,maxSpeed,0.5,1);
  float gap=mySize*gapFactor;
  float maxTime=1.5+myBrakeTime*1.3;

  //LogF("%s: Predict gap %.1f, maxDist %.1f",(const char *)GetDebugName(),gap,maxDist);

  GLOB_LAND->PredictCollision(ret,this,maxTime,gap,maxDist);

  // TODO: PredictCollision can be quite expensive
  // as we are merging with _avoid anyway, there is no need to call PredictCollision in each simulation step

  // merge PredictCollision results with _avoid list
  for (int i=0; i<_avoid.Size(); i++)
  {
    const EntityAI *ai = _avoid[i]._who;
    if (_avoid[i]._until<Glob.time || !ai)
    {
      _avoid.DeleteAt(i);
      continue;
    }
    int index=ret.Find(ai);
    if (index>=0) continue;
    // adapted from PredictCollision
    float vRadius=CollisionSize()*0.8;
    float maxDist2=FutureVisualState().Speed().SquareSize()*Square(maxTime);
    saturateMax(maxDist2,Square(maxDist));


    // find analytical solution
    float minTime=NearestPoint(FutureVisualState().Position(),FutureVisualState().Speed(),ai->FutureVisualState().Position(),ai->FutureVisualState().Speed());
    saturate(minTime,0,maxTime);
    Vector3 pt1 = FutureVisualState().Position()+FutureVisualState().Speed()*minTime;
    Vector3 pt2 = ai->FutureVisualState().Position()+ai->FutureVisualState().Speed()*minTime;
    float minDist = pt1.Distance(pt2);

    float cRadius=ai->CollisionSize();

    VehicleCollision &info=ret.Append();
    // calculate collision point
    info.pos = pt2;
    info.who = ai;
    // use at most gap to make sure the collision will not be ignored in the future
    info.time = minTime;
    info.distance = floatMin(minDist-vRadius-cRadius,gap);
    info.xSeparation = (pt2-pt1)*FutureVisualState().DirectionAside();
    
  }

  if (ret.Size()<=0)
    return;
  

  bool iAmMan = agent->IsSoldier();
  bool iAmHeavy = GetMass() > 5000;

  // precalculate
  float invMaxSpeed=1/maxSpeed;

  saturate(speedWanted,-maxSpeed,maxSpeed);
  Vector3 mySpeed=FutureVisualState().Direction()*speedWanted;

  float maxAvoid=GetPrecision()*0.5f;
  if (iAmMan)
    // man often needs to avoid multiple other mans
    // he may also need to avoid a large vehicle
    saturateMax(maxAvoid,CollisionSize()*2+2.0f);
  else
    saturateMax(maxAvoid,CollisionSize());
  saturate(maxAvoid,1,4); // 4m is enough to avoid on any road with any vehicle
  float wantAvoid=0;

  for( int i=0; i<ret.Size(); i++ )
  {
    const VehicleCollision &info=ret[i];
    const EntityAI *who=info.who;

    // something is near
    // some vehicle
    // determine who should slow down
    // if who is in front of us, slow down to his speed
    // if we are heavy and he is enemy soldier, ignore him
    if (iAmHeavy)
    {
      AIBrain *whoUnit = who->CommanderUnit();
      if (whoUnit && whoUnit->IsEnemy(agent->GetSide())) continue;
    }

#if _ENABLE_CHEATS
    if( CHECK_DIAG(DEPath) )
    {
      Ref<Object> obj=new ObjectColored(GScene->Preloaded(SphereModel),VISITOR_NO_ID);
      obj->SetPosition(info.pos);
      Color color(1,1,0,0.1f);
      obj->SetScale(info.distance<gap ? 0.5f : 0.25f);
      obj->SetConstantColor(color);
      GScene->ShowObject(obj);
    }
#endif

    if( info.distance<=gap )
    {
      // check relative speed and position

#if DIAG_COL
      if( this==GWorld->CameraOn() )
        LogF("%s vs %s",(const char *)GetDebugName(),(const char *)who->GetDebugName());
#endif

      Vector3Val v = mySpeed-who->PredictVelocity();
      Vector3Val r = who->FutureVisualState().Position() - FutureVisualState().Position();

      float nearer = 0;
      float actDistance = 0;
      float inbound = 0; // inbound factor: -1 = going straight away, +1 going directly towards a collision
      float timeToCollision = 0; // estimate how long would it take until we touch
      if (r.SquareSize()>0)
      {
        // projection of v to r is how much we are getting nearer
        Vector3 nearerV=v.Project(r);
        // nearer is oriented relative speed size
        Vector3 rNorm = r.Normalized(); 
        nearer = nearerV*rNorm;
        actDistance = r*rNorm;
        inbound = rNorm*v.Normalized();
        timeToCollision = nearer>0 ? actDistance/nearer : FLT_MAX;
      }

      // predicated closest distance after info.time
      float distance=info.distance;
      float isNear;
      if( distance<gap )
        isNear=Interpolativ(distance,0,gap,-maxSpeed*0.5,-maxSpeed*0.1);
      else
        isNear=Interpolativ(distance,gap,gap+myBrakeDist,-maxSpeed*0.1,maxSpeed);
      
      float dirFactor=nearer*invMaxSpeed;

#if DIAG_COL
      if( this==GWorld->CameraOn() )
        LogF("  nearer %.3f, dirFactor %.3f",nearer,dirFactor);
#endif
      if( dirFactor<=-0.1 )
      {
#if DIAG_COL
        if( this==GWorld->CameraOn() )
          LogF("  going away");
#endif
        continue; // safe: going away
      }

      if( Airborne() )
      {
        // check if obstacle is dangerous to airborne vehicle
        float maxY=who->FutureVisualState().Position().Y()+who->GetShape()->Max().Y();
        if( maxY<FutureVisualState().Position().Y()-10 ) continue;
      }

      if (
        who->Static() ||
        // soldier vs. soldier needs always to be handled dynamically
        who->IsLockedPosition() && (!iAmMan || !who->CommanderUnit() || !who->CommanderUnit()->IsFreeSoldier())
      )
      {
        float limSpeed=isNear;
        // static object - should be considered in path planner
        saturateMax(limSpeed,maxSpeed*0.5);
        saturate(speedWanted,-limSpeed,+limSpeed);

#if DIAG_COL
        if( this==GWorld->CameraOn() )
          LogF("  Obj %.1f, idist %.1f, dist %.1f, gap %.1f, brakeDist %.1f",
            speedWanted*3.6,info.distance,distance,gap,myBrakeDist);
#endif
      }
      else
      {
        Vector3Val hisSpeedR=DirectionWorldToModel(who->PredictVelocity());
        float hisSpeedRZ=hisSpeedR.Z();

        // check collision on predicted position

        float t=info.time;
        Vector3 myPos=FutureVisualState().Position()+t*PredictVelocity();
        Vector3 whoPos=who->FutureVisualState().Position()+t*who->PredictVelocity();

        bool dangerous=true;
        AIBrain *whoUnit = who->CommanderUnit();
        if( whoUnit && !whoUnit->IsFreeSoldier() )
        {
          Ref<ObjectVisualState> myTrans,whoTrans;
          myTrans = CloneFutureVisualState();
          myTrans->SetPosition(myPos);

          whoTrans = who->CloneFutureVisualState();
          whoTrans->SetPosition(whoPos);
          // enlarge slightly - be carefull
          myTrans->SetScale(1.1);
          whoTrans->SetScale(1.1);
          
          // do not check collision with men
          // assume collision will happen
          CollisionBuffer objCol;
          Intersect(objCol,const_cast<EntityAI *>(who),*myTrans,*whoTrans);
          dangerous=( objCol.Size()>0 );
        }

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEPath))
        {
          Ref<Object> obj=new ObjectColored(GScene->Preloaded(SphereModel),VISITOR_NO_ID);
          float scale=(maxTime-info.time)/maxTime;
          //float scale=
          saturate(scale,0.1f,0.5f);
          Color color;
          obj->SetPosition(info.pos);
          float danger = -hisSpeedRZ*(1.0f/10);
          saturate(danger,0,1);
          color=Color(1,0,dangerous)*danger+Color(0,1,dangerous)*(1-danger);
          color.SetA(0.1f);
          //color=Color(1,-1,0)*danger+Color(0,1,0);
          obj->SetScale(scale);
          obj->SetConstantColor(color);
          GScene->ShowObject(obj);

          GScene->DrawCollisionStar(myPos,0.1f);
          GScene->DrawCollisionStar(whoPos,0.1f);
        }
#endif  

        // check direction of target
        bool overtaken = false;
        bool overtaking = false;
        float whoSpeedD=who->PredictVelocity()*FutureVisualState().Direction();
        float mySpeedD=PredictVelocity()*FutureVisualState().Direction();
        float timeSeparation = myMaxBrakeTime*4.0f;
        if (iAmMan)
        {
          // man is using a slightly different collision avoidance
          // based on potential fields
          // around each soldier there is an elliptic potential field repulsing any obstacle
          // ellipse two foci: A = the soldier itself, B = where the soldier is moving to
          Vector3Val focusA = FutureVisualState().Position();
          const float predictTime = 2.0f;
          Vector3Val focusB = FutureVisualState().Position()+PredictVelocity()*predictTime;
          // ellipse "diameter" (sum of distances to foci): needed separation value
          float diameter =  who->CollisionSize()+CollisionSize()+focusA.Distance(focusB);
          // how deep is the obstacle in the ellipsis now
          float sumDistToFoci = who->FutureVisualState().Position().Distance(focusA) + who->FutureVisualState().Position().Distance(focusB);
          if (sumDistToFoci<diameter)
          { // obstacle is in the ellipse
            // how much in
            float inFactor = sumDistToFoci / diameter;
            // repulsion direction
            float isAside = PositionWorldToModel(who->FutureVisualState().Position()).X();
            // to prevent discontinuity we avoid +1/-1 type decision
            // if he is left, we want to avoid right
            float avoidThis = inFactor*floatMinMax(-isAside*4.0f,-1,+1);
            
            // analyze the situation: current distance, approach speed
            float collisionFactor = InterpolativC(timeToCollision,timeSeparation*0.1f,timeSeparation,1,0);
            // sum all avoidance impulses
            wantAvoid += avoidThis*collisionFactor;
          } 
        }
        else
        {
          // no overtaking/avoiding in same group
          float avoidThis = (who->CollisionSize()+CollisionSize())*0.6;
          Vector3 whoRelPos=PositionWorldToModel(who->FutureVisualState().Position());
          if( fabs(whoRelPos.X())>20 && fabs(whoRelPos.X())>whoRelPos.Z()*0.5 )
            // aside of our direction - do not avoid
            avoidThis = 0;
          else if( FutureVisualState().Direction()*who->FutureVisualState().Direction()<-0.2 )
          {
            // opposite direction - both should avoid
          }
          else if( whoRelPos.Z()<0 && whoRelPos.X()<0 )
            // he's overtaking us
            overtaken = true;
          else if( whoRelPos.Z()>0 && whoRelPos.X()>0 )
            // we're overtaking him
            overtaking = true;
          else if( mySpeedD>5 )
          {
            // I am moving forward
            overtaking = whoSpeedD<mySpeedD;
            overtaken = !overtaking;
          }
          
          if (!dangerous)
          {
            avoidThis *= 0.5;
            saturate(avoidThis,-maxAvoid*0.5,+maxAvoid*0.5);
#if DIAG_COL
            if( this==GWorld->CameraOn() )
              LogF("  not dangerous");
#endif
          }

          if (overtaking)
          {
            // overtake - same direction
            // positive separation is good - this means he is on our left
            // assume we already do avoid to some degree (as indicated by _avoidAside)
            // 
            float timeFactor = InterpolativC(info.time,1.0f,3.5f,1,0);
            float wantAvoidThis = _avoidAside-(avoidThis-floatMin(+info.xSeparation,avoidThis))*timeFactor;
            // if there is a different direction avoidance, let it take precedence
            if (wantAvoid<=0 && wantAvoidThis<wantAvoid) wantAvoid = wantAvoidThis;
#if DIAG_COL
            if( this==GWorld->CameraOn() )
              LogF("  overtaking %.2f",wantAvoid);
#endif
          }
          else
          { // avoid - different direction, or being overtaken
            // negative separation is good - this means he is on our right
            // assume we already do avoid to some degree (as indicated by _avoidAside)
            float timeFactor = InterpolativC(info.time,1.0f,3.5f,1,0);
            float wantAvoidThis = _avoidAside+(avoidThis-floatMin(-info.xSeparation,avoidThis))*timeFactor;
            if (wantAvoidThis>wantAvoid) wantAvoid = wantAvoidThis;
#if DIAG_COL
            if( this==GWorld->CameraOn() )
              LogF("  overtaken %.2f",wantAvoid);
#endif
          }
        }

        // check if he is behind us

        Vector3 whoRelPos=PositionWorldToModel(who->FutureVisualState().Position());
        if( whoRelPos.Z()<0 )
        {
          // he's behind us
          // do not brake - it would be only worse
          if( speedWanted>0 )
          {
#if DIAG_COL
            if( this==GWorld->CameraOn() )
              LogF("  behind us");
#endif
            continue;
          }
        }
        else if( hisSpeedRZ>maxSpeed*0.1 )
        {
          // he is is front of us - going forward
          // we can limit speed to his speed
          float curDist=who->FutureVisualState().Position().Distance(FutureVisualState().Position());
          float maxNear=mySize*2+myBrakeDist*0.6;
          float curNear=(curDist>1000*maxNear ? 1000 : curDist/maxNear)-1;
          if (!dangerous)
            // he is not dangerous - move
            saturateMax(curNear,0.1*maxSpeed);
          saturateMin(isNear,curNear);

          //LogF("%s: in front of us %.2f",(const char *)GetDebugName(),curNear);
        }

        if (who->PredictVelocity().SquareSize()<Square(2))
        {
          // he is stopped - we have to move
          saturateMax(isNear,0.1*maxSpeed);
        }

        {
          float limSpeed=floatMax(isNear+hisSpeedRZ,0);
          // check relative position of collision
          //Vector3Val relPos=PositionWorldToModel(pos);
          float carelessSpeed=hisSpeedRZ+maxSpeed*0.3;
          //saturateMax(limSpeed,GetType()->GetMaxSpeedMs()*0.05);
          //saturateMax(limSpeed,0);
          limSpeed=Interpolativ(dirFactor,0,0.1,carelessSpeed,limSpeed);
          // moving vehicle very near - slow down
          if (!whoUnit)
          {
            // vehicle empty - should be considered in path planner
            limSpeed=carelessSpeed;
          }
          else
          {
            float limSpeedMin = 0;
            if (iAmMan && whoUnit->IsSoldier())
              limSpeedMin = Interpolativ(timeToCollision,timeSeparation*0.1f,timeSeparation,1,maxSpeed);
            else if( who->IsOnRoadMoving(1.0f) && IsOnRoadMoving(1.0f) )
            {
              // if both are moving and on road, there is no need to stop
              if (overtaking)
                limSpeedMin = floatMax(hisSpeedRZ+maxSpeed*0.2,maxSpeed*0.5f);
              else
                limSpeedMin = floatMax(hisSpeedRZ,0) + (dangerous ? 6 : 12);
              // if we are heading same direction
              // we will overtake and should not brake under his speed + some reserve
            }
            else
              // stop if necessary
              limSpeedMin = floatMax(hisSpeedRZ,0) + (dangerous ? 0 : 6);
            saturateMax(limSpeed,limSpeedMin);
          }

          // add into the avoid list as well
          //int index = _avoid.FindOrAdd(who);
          //_avoid[index]._until = Glob.time+4; // make sure it is considered even when its effects have already expired
          
          // keep braking for some time
          if( Glob.time>_avoidSpeedTime || _avoidSpeed>=limSpeed )
          {
            _avoidSpeedTime = Glob.time+2;
            _avoidSpeed = limSpeed;
#if DIAG_COL
            if( this==GWorld->CameraOn() )
              LogF("  limit speed %.1f",limSpeed);
#endif
          }
          saturate(speedWanted,-limSpeed,+limSpeed);
        }
        
#if DIAG_COL
        if (this==GWorld->CameraOn())
          LogF("  Col %.1f dist %.1f, gap %.1f, brakeDist %.1f, dir %.2f",
            speedWanted*3.6,distance,gap,myBrakeDist,dirFactor);
#endif
      }
    } // if( info.distance<=gap )
    else
    {
#if DIAG_COL
      if (this==GWorld->CameraOn())
        LogF("%s vs %s ignored (%.2f>%.2f)",
          (const char *)GetDebugName(),(const char *)who->GetDebugName(),
          info.distance,gap);
#endif
    }
  } // for(i)

  saturate(wantAvoid,-maxAvoid,+maxAvoid);
  _avoidAsideWanted=wantAvoid+neutralAside;

  if (fabs(speedWanted)<maxSpeedStopped || _objectContact)
    CreateFreshPlan();
}

#if 0 // _PROFILE
#pragma optimize("",on)
#endif

void EntityAI::CreateFreshPlan()
{
  // do not report in DirectGo mode
  AIBrain *unit = PilotUnit();
  if (!unit) return;
  if (unit->GetPlanningMode() == AIBrain::LeaderDirect) return;
  Path &path=unit->GetPath();
  if( path.Size()>=2 )
  {
    // check if we have recent path
    if (path.GetSearchTime()<Glob.time-5 && unit->GetState() != AIBrain::Planning)
    {
      // re-plan
      unit->OnStepTimedOut();
    }
  }
}

float EntityAI::DistanceXZFromPath() const
{
  // calculate point on trajectory in time (relative in sec)
  // estimate position after time
  AIBrain *unit = PilotUnit();
  if (!unit)
    return 0;
  Path &path=unit->GetPath();
  // no path - no steering
  if (path.Size()<1)
    return 0;
  // we might consider better calculation, which would handle first and last segments as half-infite
  // however, when moving somewhere where path is not planned we should be careful anyway
  const VisualState &vs = FutureVisualState();
  Vector3Val pos=path.NearestPos(vs.Position());
  return pos.DistanceXZ(vs.Position());
}


float EntityAI::DistanceXFromPath() const
{
  // calculate point on trajectory in time (relative in sec)
  // estimate position after time
  AIBrain *unit = PilotUnit();
  if (!unit)
    return 0;
  Path &path=unit->GetPath();
  // no path - no steering
  if (path.Size()<1)
    return 0;
  // we might consider better calculation, which would handle first and last segments as half-infite
  // however, when moving somewhere where path is not planned we should be careful anyway
  const VisualState &vs = FutureVisualState();
  Vector3Val pos=path.NearestPos(vs.Position());
  return (vs.Position()-pos)*vs.DirectionAside();
}

Vector3 EntityAI::SteerPoint(float spdTime, float costTime, Ref<const PathAction> *action, OLink(Object) *building, Clearance *clearance, bool updateCost)
{
  // calculate point on trajectory in time (relative in sec)
  // estimate position after time
  AIBrain *unit = PilotUnit();
  if( !unit ) return FutureVisualState().Position();
  Path &path=unit->GetPath();
  // no path - no steering
  if( path.Size()<2 ) return FutureVisualState().Position()+FutureVisualState().Direction();
  // predict simulation result
  Vector3Val sPos=FutureVisualState().Position()+FutureVisualState().Speed()*spdTime+0.5*spdTime*spdTime*FutureVisualState()._acceleration;
  // find cost corresponding to the simulated result
  float cost=path.CostAtPos(sPos, updateCost);
  if (updateCost)
    // set min. cost to prevent steer point going back from a position it has already reached
    path.SetMinCost(cost);

  // steer point needs to be at least radius of the vehicle away, otherwise it is useless
  // min. distance - make dependent on precision and object radius
  float minDist = floatMax(GetRadius()*0.5f,GetPrecision()*0.1f);
  Vector3Val pos=path.PosAtCost(cost+costTime,minDist,sPos,cost,action,building,clearance);

  if (clearance)
  {
    Vector3Val nowPos=FutureVisualState().Position();
    // find cost corresponding to the simulated result
    float costNow=path.CostAtPos(nowPos, updateCost);
    path.PosAtCost(cost+costTime,minDist,nowPos,costNow,NULL,NULL,clearance);
  }

  return pos;
}

float EntityAIType::GetPathCost( const GeographyInfo &info, float dist, CombatMode mode ) const
{
  // note: we are not passing mode into GetCost - the argument is used only for searching cover
  float cost=GetFieldCost(info,mode)*GetBaseCost(info, false, true);
  if( cost>GET_UNACCESSIBLE ) cost=GetMinCost()*2;
  return cost*dist;
}


void EntityAIType::GetPathPrecision(float speedAtCost, float &safeDist, float &badDist) const
{
  safeDist = OperItemGrid*0.5f;
  badDist = OperItemGrid*1.5f;
}

#if _HELISIM_IMPERIAL_SYSTEM
//convert units to imperial system
float EntityAIType::ConvertDistance(float meters, int &stringIDS) const 
{
  if(Glob.config.useImperialSystem == 2)
  {
    if(meters >= 1609) 
    {
      stringIDS = IDS_UI_POSITION_DISTANCE_MILES;
      return METER_TO_MILE(meters);
    }
    else
    {
      stringIDS = IDS_UI_POSITION_DISTANCE_FEET;
      return METER_TO_FEET(meters);
    }
  }
  else
  {
    if(meters > 1000) 
    {
      stringIDS = IDS_UI_POSITION_DISTANCE_KILOMETERS;
      return METER_TO_KILOMETER(meters);
    }
    else
    {
      stringIDS = IDS_UI_POSITION_DISTANCE;
      return meters;
    }
  }
};

float EntityAIType::ConvertSpeed(float metersPerSecond) const 
{
  return  Glob.config.useImperialSystem == 2? METER_TO_MILE_PER_HOUR(metersPerSecond) : METER_TO_KM_PER_HOUR(metersPerSecond);
};

float EntityAIType::ConvertHeight(float meters) const 
{
  return Glob.config.useImperialSystem == 2? METER_TO_FEET(meters) : meters;
};

float  EntityAIType::ConvertVerticalSpeed(float metersPerSecond) const
{
 return  Glob.config.useImperialSystem > 0? METER_TO_FEET_PER_MINIT(metersPerSecond) : metersPerSecond;
}
#endif

float EntityAI::GetFieldCost( const GeographyInfo &info) const
{
  AIBrain *unit = PilotUnit();
  CombatMode cm = unit ? unit->GetCombatMode() : CMSafe;
  return GetType()->GetFieldCost(info,cm);
}

float EntityAI::GetAirCost(float x, float z) const
{
  return GetType()->GetAirCost(x,z);
}

float EntityAI::VisibleMovementNoSize() const
{
  float camouflage=GetType()->_camouflage;
  float vis=camouflage;

  float rSpeed=fabs(FutureVisualState().ModelSpeed().Z())*3;
  if( rSpeed>vis*GetRadius() )
  { // moving target is more visible
    float relSpeed=rSpeed/GetRadius();
    saturateMin(relSpeed,8);
    saturateMax(vis,relSpeed*camouflage);
  }
  return vis;
}

float EntityAI::VisibleMovement() const
{
  return EntityAI::VisibleMovementNoSize();
}

float EntityAIFull::VisibleMovement() const
{
  float vis=base::VisibleMovement();
  saturateMax(vis,_shootVisible); // firing target is more visible
  return vis;
}

float EntityAIFull::VisibleMovementNoSize() const
{
  float vis=base::VisibleMovementNoSize();
  saturateMax(vis,_shootVisible); // firing target is more visible
  return vis;
}

float EntityAI::Audible() const
{
  static float offCoef = 0.1f; // engine off
  static float stopCoef = 0.75f; // engine on, but vehicle not moving
  static float onCoef = 1.5f; // engine on, vehicle moving
  static float moveCoef = 0.6f;
  static float moveMaxCoef = 4.0f;
  float radius = GetRadius();
  float aud = EngineIsOn() ? stopCoef : offCoef;
  float rSpeed=fabs(FutureVisualState().ModelSpeed().Z());
  if( rSpeed>radius*0.1f )
  { // moving target is more audible
    aud = onCoef + floatMin(rSpeed*moveCoef/radius,moveMaxCoef);
  }
  aud *= GetType()->GetAudible();
  return aud;

}

float EntityAIFull::Audible() const
{
  float aud = base::Audible();
  saturateMax(aud,_shootAudible); // firing target is more audible
  return aud;
}

float EntityAI::GetExplosives() const
{
  float expl=GetType()->_secondaryExplosion;
  if( expl>=0 ) return expl;
  // how much explosives is in
  // how much explosives is in
  // convert fuel and ammo to common value
  const float AmmoCostToExplosion=0.05;
  //const float IAmmoCostToExplosion=0.01;
  const float FuelCostToExplosion=0.10;
  // estimate explosiveness from cost
  float explosion = 0;
  // calculate total fuel load
  float fuelExplosion = (GetFuel()+GetFuelCargo())*FuelCostToExplosion;
  // calculate total ammo load
  float cargoExplosion = GetAmmoCargo()*AmmoCostToExplosion;
  //LogF("Ammo explosion %.1f",explosion);
  //LogF("Fuel explosion %.1f",fuelExplosion);
  //LogF("Ammo cargo explosion %.1f",cargoExplosion);
  explosion += fuelExplosion;
  explosion += cargoExplosion;
  // multiply result by config factor
  return explosion*fabs(expl);
}

float EntityAIFull::GetExplosives() const
{
  float expl=GetType()->_secondaryExplosion;
  if( expl>=0 ) return expl;
  float explosion = GetAmmoHit()*0.015f; // ammunition
  float explo = base::GetExplosives();
  explo += explosion*fabs(expl);
  return explo;
}

RString EntityAI::HitpointName(int i) const
{
  const HitPointList &hitpoints=GetType()->GetHitPoints();
  if (i>=hitpoints.Size()) return RString();
  if (i<0) return RString();
  int sel = hitpoints[i].GetSelection();
  LODShape *lShape = GetShape();
  if (!lShape) return RString();

  if (sel < 0)
    // hitpoint may be attached to no selection
    return RString();
  
  const Shape *hits = lShape->HitpointsLevel();
  if (!hits) return RString();
  const NamedSelection &nsel = hits->NamedSel(sel);
  return nsel.GetName();
}

float EntityAI::DirectLocalHit(DoDamageResult &result, int component, float val)
{
#if ENABLE_ALLOW_DAMAGE
  if (!_allowDamage) return 0;
#endif
  return base::DirectLocalHit(result, component, val);
}

float EntityAI::LocalHit( DoDamageResult &result, Vector3Par pos, float val, float valRange )
{
#if ENABLE_ALLOW_DAMAGE
  if (!_allowDamage) return 0;
#endif
  return base::LocalHit(result, pos, val, valRange);
}

float EntityAI::HandleDamage(int part, float val, EntityAI *owner, RString ammo, const SkinnedPosition &pos, bool &handlerCalled)
{
  if (!IsEventHandler(EEHandleDamage))
  {
    handlerCalled = false;
    return val;
  }
  
  RString name;
  if (part>=0)
  {
    name = HitpointName(part);
    // prevent empty name, as it has a special meaning (total damage)
    if (name.GetLength()<=0) name = "?";
  }
  handlerCalled = true;
  GameValue skinning = SkinningToGameValue(pos);
  return OnEventAnyRet<float>(EEHandleDamage, EEVENT_ARGS(name, val, owner, ammo, skinning));
}

void EntityAI::ShowDamage(int part, EntityAI *killer)
{
  if (part<0 || part>=_hit.Size())
    return;
  RString name = HitpointName(part);
  if (name.GetLength()>0)
    OnEvent(EEDammaged, name, GetHitCont(part));
  base::ShowDamage(part, killer);
}

void EntityAI::HitBy( EntityAI *killer, float howMuch, RString ammo, bool wasDestroyed)
{
#if _VBS2
  if (IsLocal() && howMuch >= 0.05 && killer && !wasDestroyed)
  {
    GStats.OnVehicleDamaged(this, killer, howMuch, ammo);
    GetNetworkManager().OnVehicleDamaged(this, killer, howMuch, ammo);
  }
#endif

  if (IsLocal() && howMuch >= 0.05 && !wasDestroyed)
  {
    OnEventAny(EEHit,EEVENT_ARGS(killer,howMuch));
  }
  base::HitBy(killer,howMuch,ammo,wasDestroyed);

  EntityAIFull *killerVehicle = NULL;
  if (killer)
  {
    AIBrain *killerUnit = killer->CommanderUnit();
    if (killerUnit) killerVehicle = killerUnit->GetVehicle();
  }

  if (howMuch>0)
  {
    ReactToDamage();
    if (!_isDead && killerVehicle)
    {
      AIBrain *brain = CommanderUnit();
      if (brain && !brain->IsAnyPlayer() && brain->LSIsAlive())
      {
        // if we are dead, we cannot reveal anyone any longer
        // reveal killer (at random position around us)
        Vector3 pos=FutureVisualState().Position();
        float accuracy = 0.1;
        float sideAcc = Target::GetSideAccuracyExact();
        float delay = 0.8f*brain->GetInvAbility(AKSpotTime);
        float delayOtherUnits = 15;
        // reveal side of killer
        // reporting units knows almost immediately about the target
        // other units will notice it after a short delay
        brain->AddTarget(killerVehicle,accuracy,sideAcc,delayOtherUnits,&pos,brain,delay);
      }
    }
  }
}

bool EntityAI::HandleHeal(EntityAI *medic, bool attendant, bool &handlerCalled)
{
  if (!IsEventHandler(EEHandleHeal))
  {
    handlerCalled = false;
    return false;
  }
  handlerCalled = true;
  return OnEventAnyRet<bool>(EEHandleHeal, EEVENT_ARGS(medic, attendant));
}



void EntityAI::OnNewTarget(Target *target, Time knownSince)
{
  // let us change the fire decision once the target is known
}


void EntityAI::LocalDamageMyself(Vector3Par modelPos, float val, float valRange)
{
  // if the vehicle has a driver, he is responsible for the crash
  AIBrain *brain = PilotUnit();
  EntityAI *responsible = (brain && brain->GetPerson()) ? brain->GetPerson() : this;
  DoAssert(GetNetworkManager().IsServerOwner());
  Vector3 originDir = FutureVisualState().DirectionModelToWorld(modelPos);
  DoDamageResult result;
  DoDamage(result, modelPos, val, valRange, RString(), originDir, false);
  ApplyDoDamageLocalHandleHitBy(result,responsible,RString(), SkinnedPosition());
}

void EntityAI::ApplyDoDamage(const DoDamageResult &result, EntityAI *owner, RString ammo, const SkinnedPosition &pos)
{
  if (owner && !IsDamageDestroyed())
  {
    // if there is a recent information about being damaged by somebody else,
    // ignore self damage
    if (owner!=this || _lastDammageTime<Glob.time-60)
    {
      _lastDammage = owner;
      _lastDammageTime = Glob.time;
    }
  }
  base::ApplyDoDamage(result,owner,ammo,pos);
}

Time EntityAI::GetDestroyedTime() const
{
  if (!_isDead)
    return TIME_MAX;
  // if kill time is not known, assume it was killed recently
  if (_whenDestroyed>Glob.time)
    _whenDestroyed = Glob.time;
  return _whenDestroyed; // helpers for suspending dead body after a while
}

void EntityAI::SetTotalDamageHandleDead(float damage, bool setHitpoints)
{
  base::SetTotalDamageHandleDead(damage,setHitpoints);
  ReactToDamage();
  _isDead = IsDamageDestroyed();
  if (_isDead && (_whenDestroyed>Glob.time))
    _whenDestroyed = Glob.time;
}

void EntityAI::Destroy(EntityAI *killer, float overkill, float minExp, float maxExp)
{
  if (GetNetworkManager().IsServerOwner())
  {
    OnEvent(EEKilled,killer);
    GStats.OnVehicleDestroyed(this, killer);
    GetNetworkManager().OnVehicleDestroyed(this, killer);
  }

  // other units which have seen me being killed might want to react as well
  GWorld->OnKill(killer,this);

  base::Destroy(killer,overkill,minExp,maxExp);
  if (_whenDestroyed>Glob.time)
    _whenDestroyed = Glob.time;

  if (killer)
  {
    // use Entity member to get original target side
    // all dead bodies are considered civilian
    TargetSide origSide = Entity::GetTargetSide();

    // increase killer's experience
    AIBrain *kBrain=killer->CommanderUnit();
    if (kBrain)
    {
      kBrain->IncreaseExperience(*GetType(),origSide);
      // send radio message
      if (kBrain->IsEnemy(origSide))
      {
        // find corresponding target
        Target *tar = kBrain->FindTargetAll(this);
        if (tar)
        {
          // mark killer
          // when destroyed will be set, it will be marked for reporting
          tar->MarkKiller(killer);
        }
      }
    }
    if (killer->GunnerUnit() && killer->GunnerUnit() != kBrain)
      killer->GunnerUnit()->IncreaseExperience(*GetType(),origSide);
    if (killer->PilotUnit() && killer->PilotUnit() != kBrain)
      killer->PilotUnit()->IncreaseExperience(*GetType(),origSide);
  }
}

/*!
\param s index of slot reload to
\param m index of reloading magazine
\param afterAnimation true if reload immediately (no delay)
*/
bool EntityAIFull::ReloadMagazineTimed(WeaponsState &weapons, const WeaponModePtr &ptr, bool afterAnimation)
{
  Muzzle &muzzle = ptr.GetMuzzle();

  Magazine *magazine = muzzle._magazineWanted;
  if (!magazine) return false;

  if (QIsManual())
  {
    if (magazine->GetAmmo() > 1)
      DecFadeSemaphore(FadeReloadMag);
  }

#if 0
  LogF("%s: ReloadMagazineTimed slot %d, magazine %s",
    (const char *)GetDebugName(),s,
    (const char *)magazine->Type()->GetName());
#endif

  const Magazine *oldMagazine = ptr.GetMagazine();
  if (oldMagazine == magazine) return false; // do not reload with the same magazine
  
  const MuzzleType *muzzleType = ptr.GetMuzzleType();
  if (!muzzleType->CanUse(magazine->Type()))
  {
    RptF("Cannot use magazine %s in muzzle %s",
      (const char *)magazine->Type()->GetName(),
      (const char *)muzzleType->GetName());
    return false;
  }

  // remove the current magazine, destroy it when empty for men
  if (oldMagazine)
  {
    weapons.AddMagazine(this, unconst_cast(oldMagazine), false);
    muzzle._magazine = NULL;
  }

  // prepare magazine
  magazine->_reloadMagazine = afterAnimation ? 0 :
    muzzleType->_magazineReloadTime *
#if !_VBS3 
    // ignore skill when reloading vehicle weapons. personal weapons use animation
    // M1A1 had very long reload times when commander wasn't skilled
    GetInvAbility(AKReloadSpeed) *
#endif
    GRandGen.PlusMinus(1, 0.2);
  // remember the total time so that controller may return 0..1
  magazine->_reloadMagazineTotal = magazine->_reloadMagazine;

  // vary reload time depending on skill in range 0..2
#if _VBS3
  float reloadAbility = 1.0f;
#else
  float reloadAbility = (GetInvAbility(AKReloadSpeed)-1)*0.25+1;
#endif
  // compress reload ability to range 1..2

  if (magazine->Type() && magazine->Type()->_quickReload)
  {
    magazine->_reload = 0;
    magazine->_reloadDuration = 1;
  }
  else
  {
    magazine->_reload = 1;
    magazine->_reloadDuration = reloadAbility * GRandGen.PlusMinus(1, 0.1);
  }

  LogF("ReloadMagazine %.2f, coef %.2f", magazine->_reloadMagazine, magazine->_reload);

  // switch the parent
  magazine->ItemMoveOut(ptr._weapon);
  // replace the magazine
  muzzle._magazine = magazine;

  return true;
}

bool EntityAIFull::ReloadMagazine(WeaponsState &weapons, const WeaponModePtr &ptr, Magazine *magazine)
{
  if (!ptr.IsValid()) return false;

  Muzzle &muzzle = ptr.GetMuzzle();
  if (muzzle._magazineWanted && muzzle._magazineWanted != muzzle._magazine) return false; // other reload already in progress

  weapons.RemoveMagazine(this, magazine);

  // move in world
  magazine->ItemMoveOut(ptr._weapon);

  muzzle._magazineWanted = magazine;
  return true;
}

/*!
\param slotIndex magazine slot to reload
*/
bool EntityAIFull::ReloadMagazine(WeaponsState &weapons, const WeaponModePtr &ptr, bool loaded)
{
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  const Magazine *oldMagazine = ptr.GetMagazine();
  const MagazineType *oldMagazineType = oldMagazine ? oldMagazine->Type() : NULL;
  Magazine *newMagazine = weapons.FindMagazineByType(muzzle, oldMagazineType);
  if (!newMagazine) return false;
  // ask for reload
  if (!ReloadMagazine(weapons, ptr, newMagazine)) return false;
  if (!loaded) return true;
  
  // when loaded, process loading immediately
  if (!ReloadMagazineTimed(weapons, ptr, true)) return false;
  // load not only the magazine, but also the bullet
  newMagazine->_reload = 0;
  return true;
}

/*!
\param weapon magazine slot index
\param muzzle reloading muzzle
*/
void EntityAIFull::PlayReloadMagazineSound(const TurretContext &context, const WeaponModePtr &ptr, const MuzzleType *muzzle)
{
  const SoundPars &pars = muzzle->_reloadMagazineSound;
  if (pars.name.GetLength() > 0)
  {
    float rndFreq = GRandGen.RandomValue() * 0.1 + 0.95;
    AbstractWave *wave = GSoundScene->OpenAndPlayOnce(pars.name, this, true, GetWeaponSoundPos(context, ptr), FutureVisualState().Speed(),
      pars.vol, pars.freq * rndFreq, pars.distance);
    if (wave)
    {
      GSoundScene->SimulateSpeedOfSound(wave);
      GSoundScene->AddSound(wave);
      context._weapons->_reloadMagazineSound = wave;
      if ( IsLocal() )
        GetNetworkManager().PlaySound(pars.name, FutureVisualState().Position(), FutureVisualState().Speed(), pars.vol, pars.freq, wave);
    }
  }
}

/*!
\param weapon magazine slot index
\patch 1.24 Date 09/26/2001 by Ondra
- Fixed: Speed of sound delay was missing on weapon firing sound.
*/
void EntityAIFull::PlayEmptyMagazineSound(const TurretContext &context, const WeaponModePtr &ptr)
{
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  const SoundPars &pars = muzzle->_sound;
  WeaponSounds *ws = ptr.GetSound();
  if (ws) ws->PlayEmptyMagazineSound(this, context, ptr, pars);
}

bool EntityAIFull::GetOwnHeatSource(Vector3 &ownHeatSource) const
{
  ownHeatSource = _heatSourceDirection;
  return true;
}

void EntityAIFull::SetMaxHitZoneDamage(float dammage)
{
  for (int i=0; i< _hit.Size(); i++)
  {
    _hit[i] = floatMin(_hit[i], dammage);
  }
}

void EntityAIFull::SetMaxHitZoneDamageNetAware(float dammage)
{
  DoAssert(GetNetworkManager().IsServerOwner());
  SetMaxHitZoneDamage(dammage);
}

bool EntityAIFull::AutoReload(WeaponsState &weapons, const WeaponModePtr &ptr, bool reloadAI, bool loaded)
{
  // this should be called whenever when:
  // slot is empty
  // there is some magazine that fits into the slot
  // such cases are:
  //   FireWeapon
  //   AddMagazine ????
  //   AddWeapon ????

  const MuzzleType *muzzle = ptr.GetMuzzleType();
  // TODO: flag reload on background
  //Magazine *magazine = slot._magazine;
  if (muzzle->_autoReload || Glob.config.GetAutoReload() ||
    reloadAI && (!GWorld->PlayerOn() || !GWorld->PlayerOn()->Brain() || GWorld->PlayerOn()->Brain() != CommanderUnit()))
    return ReloadMagazine(weapons, ptr, loaded);
  return false;
}

WeaponModePtr EntityAIFull::AutoReloadAll(WeaponsState &weapons, bool loaded)
{
  WeaponModePtr ret;

  weapons.ForEachMuzzle([&ret, this, &weapons, loaded](const WeaponModePtr &ptr) -> bool
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (!muzzle) return false; // continue

    Magazine *magazine = unconst_cast(ptr.GetMagazine());
    if (!magazine)
    {
      if (AutoReload(weapons, ptr, true, loaded) && !ret.IsValid())
        ret = ptr;
    }
    else if (loaded)
    {
      // load immediately
      magazine->_reload = 0;
    }

    return false; // continue
  });

  return ret;
}

class AutoReloadFunc : public ITurretFunc
{
protected:
  /// magazine will be loaded immediately
  bool _loaded;

public:
  AutoReloadFunc(bool loaded) : _loaded(loaded) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    entity->AutoReloadAll(*context._weapons, _loaded);
    return false; // for all turrets
  }
};

void EntityAIFull::AutoReloadAll(bool loaded)
{
  AutoReloadFunc func(loaded);
  ForEachTurret(func);
}

void EntityAIFull::DrawPeripheralVision(Person *person, CameraType camType)
{
  int canSee = 0;
  if (person && person->Brain() && person->Brain()->LSIsAlive())
  {
    if (person->Brain()==ObserverUnit())
      canSee = GetType()->_commanderCanSee;
    else if (person->Brain()==PilotUnit())
      canSee = GetType()->_driverCanSee;
    else if (person->Brain()==GunnerUnit())
      canSee = GetType()->_gunnerCanSee;
  }
  float visionAid = Glob.config.GetVisionAid();
  float peripheralVisionAid = Glob.config.GetPeripheralVisionAid();
  
  bool drawPeripheralVision = (
    !IsDamageDestroyed() && (visionAid>0 || peripheralVisionAid>0) &&
    (canSee&CanSeePeripheral)!=0
  );
  
  if (drawPeripheralVision && camType == CamGunner)
  {
    TurretContext context;
    if (FindTurret(person, context))
    {
      WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();

      // isn't drawing disabled in optics?
      if (selected.IsValid())
      {
        const OpticsInfo *opticsInfo = selected.GetOpticsInfo();
        if (opticsInfo)
        {
          float factor = opticsInfo->_opticsDisablePeripherialVision;
          if (factor>=0.95f) drawPeripheralVision = false;
          visionAid *= 1-factor;
          peripheralVisionAid *= 1-factor;
        }
      }
    }
  }

  if (drawPeripheralVision)
  {
    SCOPE_GRF("drawPV",0);
    // identify visible / audible cues
    AIBrain *unit = CommanderUnit();
    if (!unit) return;
    const TargetList *tgts = unit->AccessTargetList();
    if (!tgts) return;
    if (!GScene) return;
    Camera *camera = GScene->GetCamera();
    if (!camera) return;
    Matrix4 camSpace = camera->GetInvTransform();
    Matrix4 project = camera->Projection();
    
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    
    float safeLeft, safeTop, safeRight, safeBottom;
    as.GetScreenSafeArea(safeLeft, safeTop, safeRight, safeBottom);

    float w = GEngine->WidthBB();
    float h = GEngine->HeightBB();
    
    float minXV = safeLeft*2-1;
    float maxXV = safeRight*2-1;
    float minYV = safeTop*2-1;
    float maxYV = safeBottom*2-1;
    
    float minX = safeLeft*w;
    float maxX = safeRight*w;
    float minY = safeTop*h;
    float maxY = safeBottom*h;
    
    float xBorder = 0.03f;
    float yBorder = 0.05f;
     
    Texture *tex = GWorld->UI()->GetCueTexture();
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(tex,0,0);
    pars.spec = IsAlpha|IsAlphaFog|DstBlendOne|NoZBuf|ClampU|ClampV;
    pars.spec |= (GWorld->UI()->GetCueShadow() == 2)?UISHADOW : 0; 
    pars.SetU(0,1);
    pars.SetV(0,1);
    
    int anyCount = tgts->AnyCount();
    for (int i=0; i<anyCount; i++)
    {
      TargetNormal *tgt = tgts->GetAny(i);
      // check if the target is visible
      // if it is, check how much is is visible/audible
      //if (!tgt->IsKnownBy(unit)) return;
      EntityAI *ai = tgt->idExact;
      if (!ai)
        continue;
      // do not check static objects
      if (ai->Static())
        continue;
      if (!ai->SeenByPeriperalVision())
        continue;
      if (!ai->IsInLandscape())
        continue;
      // check if the cue is visible
      if (ai==this)
        continue;
      // note: far cues can be checked not as often
      float distAprox = ai->FutureVisualState().Position().Distance(camera->Position());
      float maxAge = InterpolativC(distAprox,20.0f,700.0f,0.5f,12.0f);
      float visible = GWorld->Visibility(unit,ai,maxAge);

      // consider fog
      if( distAprox>TACTICAL_VISIBILITY ) visible = 0;
      visible *= 1-GScene->TacticalFog8(Square(distAprox))*(1.0f/255);

      const LightSun *sun = GScene->MainLight();
      float night=sun->NightEffect();
      if (night>0)
      {
        // check light intensity
        float dark = 1.02f-sun->GetAmbient().Brightness()-sun->GetDiffuse().Brightness();
        saturateMin(night,dark);
        saturateMin(night,0.97f);
      }
      visible *= 1-night;
      if (visible<0.1f)
        continue;
      
      //saturate(markerAlpha,0.05f,0.25f);
      Vector3 tgtPos = ai->AimingPosition(ai->RenderVisualState());
      // project the position into the camera space
      Vector3 modelDir = camera->GetInvTransform()*tgtPos;
      //Vector3 camSpacePos = GScene->ScaledInvTransform()*tgtPos;

      //Vector3 camSpacePos = camSpace.FastTransform(tgtPos);
      // project into the screen space
      if (modelDir.Z()>=1e-3)
      {
        // front half-space - it might be visible
        float invZ = 1/modelDir.Z();
        float invDist = InvSqrt(tgtPos.Distance2(camera->Position()));

        float markerSize = ai->VisibleSize(ai->RenderVisualState())*invDist*4.0f*camera->InvTop();
        float markerAlpha = ai->VisibleMovementNoSize()*0.05f*visible;

        // following factor is 1 when target is in most sensitive area
        // and goes to zero when out of sight
        float aside2 = Square(modelDir.X())+Square(modelDir.Y());
        float peripheralFactor = modelDir.Z()*InvSqrt(aside2)*2;
        
        saturate (peripheralFactor,0,1);
        
        markerAlpha *= peripheralFactor;
        
        if (markerSize<0.01f) continue;
        if (markerAlpha<0.01f) continue;
        saturate(markerSize,0.01f,0.15f);
        saturate(markerAlpha,0.02f,0.3f);

        float scrX = modelDir.X()*invZ*camera->InvLeft();
        float scrY = -modelDir.Y()*invZ*camera->InvTop();
        
        // check if we are inside of visible area, and if not, how far we are
        
        // x borders are minX, maxX
        // y borders are minY, maxY
        
        if (scrX>=minXV && scrX<=maxXV && scrY>=minYV && scrY<=maxYV)
        {
          float insideX = floatMin(scrX-minXV,maxXV-scrX)*(1/xBorder);
          float insideY = floatMin(scrY-minYV,maxYV-scrY)*(1/yBorder);
          // both insideX and insideY is always >=0
          float inside = floatMax(0,floatMin(insideX,insideY));
          markerAlpha *= visionAid*inside+peripheralVisionAid*(1-inside);
        }
        else
          markerAlpha *= peripheralVisionAid;

        if (markerAlpha<0.01f)
          continue;
        
        Rect2DAbs rect;
        
        // check cue "color"
        PackedColor cueColor;
        
        TargetSide side = tgt->GetType()->_typicalSide;
        if (!ai->IRSignatureOn())
        {
          side = TCivilian; // empty or inactive from other groups marked as civilian
        }
        else if (tgt->GetSide()==TEnemy) // check for renegade
          side = TEnemy;
        
        if (Glob.config.IsEnabled(DTEnemyTag) && tgt->destroyed)
          side = TCivilian;

        if (unit->IsEnemy(side))
          cueColor = GWorld->UI()->GetCueEnemyColor();
        else if (side==TCivilian || unit->IsNeutral(side))
          cueColor = GWorld->UI()->GetCueColor();
        else if (unit->IsFriendly(side))
          cueColor = GWorld->UI()->GetCueFriendlyColor();
        else
          cueColor = GWorld->UI()->GetCueColor();
        
        int a = PackColorComponent(markerAlpha)* (cueColor.A8()/255.0f);
        PackedColor color = PackedColorRGB(cueColor, a);

        pars.SetColor(color);
        
        rect.w = +project(0,0)*markerSize*camera->Top()*camera->InvLeft()*0.5f;
        rect.h = -project(1,1)*markerSize*0.5f;
        
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

        rect.x = (scrX+1)*(w*0.5f);
        rect.y = (scrY+1)*(h*0.5f);
        
        saturate(rect.x,minX,maxX);
        saturate(rect.y,minY,maxY);
        
        rect.x -= rect.w*0.5f;
        rect.y -= rect.h*0.5f;
        
        GEngine->Draw2D(pars,rect);
      }
    }
  }
}

bool EntityAIFull::AimWeaponForceFire(const TurretContextEx &context, const WeaponModePtr &ptr)
{
  Vector3 dir = FutureVisualState().Direction();
  dir[1] = 3;
  dir.Normalize();
#if _VBS3
  if(context._weapons->_forceFirePosition != Vector3(0,0,0))
  {
    float timeToLead = 10.0f;
    CalculateAimWeapon(context,weapon,dir,timeToLead,context._weapons->_forceFirePosition);
  }
#endif
  return AimWeapon(context, context._weapons->ValidatedCurrentWeapon(), dir);
}

/*!
\patch 5117 Date 1/11/2007 by Jirka
- Fixed: Burst simulation in MP (Blackhawk M134 fired even when key released)
\patch 5153 Date 4/19/2007 by Jirka
- Fixed: AI - gunners was sometimes unable to fire simultaneously
*/

class SimulateWeaponActivityFunc : public ITurretFuncEx
{
protected:
  float _deltaT;
  SimulationImportance _prec;

public:
  SimulateWeaponActivityFunc(float deltaT, SimulationImportance prec)
    : _deltaT(deltaT), _prec(prec) {}

  bool operator () (EntityAIFull *entity, TurretContextEx &context)
  {
    // reload all muzzles
    const Magazine *curMag = NULL; // magazine loaded in the current weapon
    const Magazine *curCM = NULL; // magazine loaded in the current counter measures system
    WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
    if (ptr.IsValid())
      curMag = ptr.GetMagazine();
    WeaponModePtr ptrCM = context._weapons->ValidatedCurrentCounterMeasures();
    if (ptrCM.IsValid())
      curCM = ptrCM.GetMagazine();

    context._weapons->ForEachMuzzle([entity, &context, curMag, curCM, &ptr, &ptrCM, this](const WeaponModePtr &p) -> bool
    {
      /* Moved to Weapon::Simulate
      {
        // check if reload is required
        Muzzle &muzzle = p.GetMuzzle();
        if (muzzle._magazine != muzzle._magazineWanted)
        {
          if (muzzle._magazineWanted.NotNull())
            entity->DoReloadMagazine(*context._weapons, p);
          else
            muzzle._magazine = NULL; // no process attached to magazine removal
        }
      }
      */
      
      // TODO: flag reload on background
      
      // select muzzle and magazine from the slot
      const MuzzleType *muzzle = p.GetMuzzleType();
      Magazine *magazine = unconst_cast(p.GetMagazine());
      
      // for the current weapon/CM, select the current mode, for others the first one
      WeaponModePtr index = p;
      if (curMag && magazine == curMag) index = ptr;
      if (curCM && magazine == curCM) index = ptrCM;
      
      const WeaponModeType *mode = index.GetWeaponModeType();
      
      if (magazine && magazine->GetAmmo() > 0)
      {
        if (magazine->_reloadMagazine > 0)
        {
          magazine->_reloadMagazine -= _deltaT;
          if (magazine->_reloadMagazine <= muzzle->_reloadMagazineSoundDuration &&
            !context._weapons->_reloadMagazineSound)
          {
            // make sound when reloaded
            entity->PlayReloadMagazineSound(context, p, muzzle);
            saturateMax(magazine->_reloadMagazine, 0);
          }
        }
        else if (magazine->_reload > 0)
        {
          if (mode->_reloadTime <= 0) magazine->_reload = 0;
          else magazine->_reload -= _deltaT / (mode->_reloadTime * magazine->_reloadDuration);
          if(magazine->_reload * magazine->_reloadDuration * mode->_reloadTime <= muzzle->_reloadSoundDuration &&
            !context._weapons->_reloadSound)
          {
            // make sound when reloaded
            const SoundPars &pars = muzzle->_reloadSound;
            if (pars.name.GetLength() > 0)
            {
              float rndFreq = GRandGen.RandomValue() * 0.1 + 0.95;
              AbstractWave *wave = GSoundScene->OpenAndPlayOnce(pars.name, entity, true, entity->FutureVisualState().Position(), entity->FutureVisualState().Speed(),
                pars.vol, pars.freq * rndFreq, pars.distance);
              if (wave)
              {
                GSoundScene->SimulateSpeedOfSound(wave);
                GSoundScene->AddSound(wave);
                context._weapons->_reloadSound = wave;
                //FIX: do not call PlaySound here, as this sound is already played on client correctly (so it was duplicated, see https://dev-heaven.net/issues/28201)
                //GetNetworkManager().PlaySound(pars.name, entity->FutureVisualState().Position(), entity->FutureVisualState().Speed(), pars.vol, pars.freq, wave);
              }
            }
            saturateMax(magazine->_reload, 0);
          }
        }
      }
      return false; // continue
    });

    if (context._weapons->_forceFireWeapon.IsValid() && !entity->IsDeadSet())
    {
      WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();
      if (context._weapons->_forceFireWeapon == selected)
      {
        bool aimed = entity->AimWeaponForceFire(context, selected);
        if (
          entity->GetWeaponLoaded(*context._weapons, selected) &&
          (entity->GetWeaponDirection(entity->RenderVisualState(), context, selected).Y() >= 0.7 || aimed))
        {
          const WeaponModeType *mode = selected.GetWeaponModeType();
          if (mode && mode->_useAction)
          {
            if (context._gunner && context._gunner->Brain())
              entity->StartActionProcessing(new ActionWeaponIndex(ATUseWeapon, entity, context._gunner, selected), context._gunner->Brain());
            context._weapons->_forceFireWeapon = WeaponModePtr();
          }
          else
          {
            if (entity->FireWeapon(context, selected, NULL, false))
              context._weapons->_forceFireWeapon = WeaponModePtr();
          }
        }
      }
    }

    // advance time for all looping weapon sounds
    context._weapons->ForEachMuzzle([](const WeaponModePtr &ptr) -> bool
    {
      WeaponSounds &ws = ptr.GetMuzzle()._sound;
      ws.SetStopTime(Glob.time.toFloat());
      return false; // continue
    });
    // simulate burst fire

    bool isLocal = context._turret ? context._turret->IsLocal() : entity->IsLocal();
    if (isLocal)
    {
      WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
      if (ptr.IsValid())
      {
        const Magazine *magazine = ptr.GetMagazine();
        if (magazine && magazine->_burstLeft > 0)
          // fire on same target as before
          entity->FireWeapon(context, ptr, entity->GetShootTarget(), false);
      }

      // simulate burst couterMeasures
      WeaponModePtr counterMeasures = context._weapons->ValidatedCurrentCounterMeasures();
      if (ptr != counterMeasures && counterMeasures.IsValid())
      {
        const Magazine *magazine = counterMeasures.GetMagazine();
        if (magazine && magazine->_burstLeft > 0)
          // fire on same target as before
          entity->FireWeapon(context, counterMeasures, NULL, false);
      }
    }
    
    return false; // continue
  }
};

void EntityAIFull::SimulateWeaponActivity( float deltaT, SimulationImportance prec )
{
  SimulateWeaponActivityFunc func(deltaT, prec);
  ForEachTurretEx(unconst_cast(FutureVisualState()), func);

  // simulate shooting visibility
  _shootTimeRest -= deltaT;
#if _VBS3 //adding _shootTimeRestAudible
  _shootTimeRestAudible -= deltaT;
  if( _shootTimeRestAudible<0 || IsDamageDestroyed())
  {
    // return to default
    _shootTimeRestAudible = FLT_MAX;
    _shootAudible = 0;
  }
#endif

  if( _shootTimeRest<0 || IsDamageDestroyed())
  {
    // return to default
    _shootTimeRest = FLT_MAX;
    _shootVisible = 0;
#if !_VBS3
    _shootAudible = 0;
#endif
  }

  AIBrain *unit = GWorld->FocusOn();
  if (unit && unit == CommanderUnit() && !unit->IsInCargo() && !GWorld->GetCameraEffect())
  {
    if (GInput.GetActionToDo(UAHeadlights))
    {
      _pilotLight=!_pilotLight;
      if ( !IsLocal() ) 
      {
        GetNetworkManager().AskForPilotLight(this, _pilotLight);
      }
      _collisionLight=_pilotLight;
    }
  }

  //((ARMAFIX
  // recoil simulation moved into Man::MoveWeapons

  if (this==GWorld->CameraOn())
  {
    // force feedback simulation is necessary
    // StartRecoilFF will both start and stop the effect
    StartRecoilFF();
  }
  //))ARMAFIX
}

void EntityAI::OnMoved()
{
  // move condition detected
  _lastMovement = Glob.time;
  CancelStop();
}
void EntityAI::StopDetected()
{
  // stop condition detected
  if (Glob.time>_lastMovement+TimeToStop())
  {
    Stop();
    FutureVisualState()._speed=VZero;
    _angMomentum=VZero;
    _angVelocity=VZero; //Added by pf. also velocity must ve zero.
  }
}

void EntityAI::SwitchLight(bool on)
{
  Assert(!on); // Only switching off is working (switching on could use damaged light)
  _lights.SwitchLight(on);
}

void WeaponSounds::RegisterBulletSound()
{
  if (_count == 0)
    _delay = Glob.time + 0.5f;
  static const int MaxBulletsSound = 10;
  if (_count < MaxBulletsSound)
    _count++;
}

void WeaponSounds::UpdateSound(const TurretContext &context, const WeaponModePtr &ptr, const Vector3 &pos, const Vector3 &speed)
{
  // play bullets sounds
  UpdateBulletSound(context, ptr);

  // shot sound
  if (_fireSound._sound)
  {
    if (_fireSound._sound->IsTerminated())
      _fireSound._sound.Free();
    else
      _fireSound._sound->SetPosition(pos, speed);
  }

  // bullet sound
  if (_bulletSound)
  {
    if (_bulletSound->IsTerminated()) 
      _bulletSound.Free();
    else
      _bulletSound->SetPosition(pos, speed);
  }

  // empty magazine sound
  if (_emptyMagSound)
  {
    if (!_emptyMagSound->IsTerminated())
      _emptyMagSound->SetPosition(pos, speed);
    else
      _emptyMagSound.Free();
  }
}

void WeaponSounds::PlayEmptyMagazineSound(EntityAIFull *entity, const TurretContext &context, const WeaponModePtr &ptr, const SoundPars &pars)
{
  if (pars.name.GetLength() > 0 && entity)
  {
    _entity = entity;
    _position = _entity->GetWeaponSoundPos(context, ptr);

    if (pars.name.GetLength() > 0 && entity && _emptyMagSound.IsNull())
      // create new wave
      _emptyMagSound = GSoundScene->OpenAndPlayOnce(pars.name, _entity, false, _position, _entity->FutureVisualState().Speed(), pars.vol, pars.freq, pars.distance);
    else if (_emptyMagSound && !_emptyMagSound->IsWaiting())
       _emptyMagSound->Restart();
    if ( _emptyMagSound && pars.name.GetLength() > 0 && entity->IsLocal() )
      GetNetworkManager().PlaySound(pars.name, _position, _entity->FutureVisualState().Speed(), pars.vol, pars.freq, _emptyMagSound);
  }
}

void WeaponSounds::SetStopTime(float time)
{
  _fireSound._time = Time(time);
  if (_fireSound._sound)
    _fireSound._sound->SetStopValue(time);
}

void WeaponSounds::UnloadSound()
{
  // shoot sound
  _fireSound._sound.Free();
  _bulletSound.Free();
  _emptyMagSound.Free();
}

void WeaponSounds::UpdateBulletSound(const TurretContext &context, const WeaponModePtr &ptr)
{
  // delay is set when first shot is fired
  if (_delay < Glob.time)
  {
    if (_count != 0)
    {
      if (_nextBulletTime < Glob.time)
      {
        const Entity::VisualState &vs = _entity->RenderVisualState();
        if (_bulletSound.IsNull() && _entity)
        {
          const SoundPars& pars = _entity->SelectBulletSoundPars(context, ptr);

          if (pars.name.GetLength() > 0)
            // create new wave
            _bulletSound = GSoundScene->OpenAndPlayOnce(pars.name, _entity, false, _position, vs.Speed(), pars.vol, pars.freq, pars.distance);
        }
        else
        {
          if (_count % 3 == 0)
          {
            _bulletSound.Free();

            // fixed - entity == NULL
            if (_entity)
            {
              // select new sample
              const SoundPars& pars = _entity->SelectBulletSoundPars(context, ptr);
              _bulletSound = GSoundScene->OpenAndPlayOnce(pars.name, _entity, false, _position, vs.Speed(), pars.vol, pars.freq, pars.distance);
            }
          }
          else if (_bulletSound.NotNull() && !_bulletSound->IsWaiting())
            _bulletSound->Restart();
        }
        _count--;
        _nextBulletTime = Glob.time + _reloadTime;
      }
    }
  }
}

const SoundPars& EntityAIFull::SelectBulletSoundPars(const TurretContext &context, const WeaponModePtr &ptr) const
{
  static SoundPars empty;
  return empty;
}

void WeaponSounds::ShotSound(EntityAIFull *entity, const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine)
{
  const WeaponModeType *mode = ptr.GetWeaponModeType();

  if (!entity || !magazine) return;

  _entity = entity;

  // position used as position of sound
  _position = entity->GetWeaponSoundPos(context, ptr);

  // reload time
  _reloadTime = mode->_reloadTime;

  // check if player is inside
  bool isInside = GWorld->IsCameraInsideVeh();

  if (_fireSound._sound.IsNull())
  {
    float rand = GRandGen.RandomValue();
    // select sample ... select one from group
    const SoundPars &pars = mode->_beginSound.SelectSound(rand);
    
    if (pars.name.GetLength() > 0)
    {
      // create wave
      _fireSound._sound = GSoundScene->OpenAndPlayOnce(pars.name, entity, false, _position, _entity->FutureVisualState().Speed()
        , pars.vol, pars.freq, pars.distance);

      if (_fireSound._sound)
      {
        GSoundScene->SimulateSpeedOfSound(_fireSound._sound);
        float obstruction, occlusion;
        GWorld->CheckSoundObstruction(entity, false, obstruction, occlusion);
        _fireSound._sound->SetObstruction(obstruction, occlusion);
        _fireSound._sound->EnableFilter(true, isInside ? InternalFireLowPFilter : GenericLowPFilter);
      }
      if (!mode->_soundContinuous) RegisterBulletSound();

      /*
      static const float MinDist2 = 169;

      // fast test - distance listener and entity center
      if (magazine->Type() && MinDist2 > GSoundsys->GetListenerPosition().Distance2(_position))
      {
        const AmmoType *ammoType = magazine->Type()->_ammo;

        // effect enable on fire
        if (ammoType && (ammoType->_whistleOnFire != 0) && ammoType->_whistleDist > 0)
        {
          Transport *tr = dynamic_cast<Transport*>(entity);

          Transport *tFocOn = GWorld->FocusOn() ? GWorld->FocusOn()->GetVehicleIn() : NULL;
          
          if (tr != tFocOn && tr && context._turret && context._turretType && !GWorld->GetCameraEffect())
          {
            Matrix4Val shootTrans = tr->GunTurretTransform(*context._turretType);
            Vector3Val gPosL = context._turretType->GetTurretPos(shootTrans);
            Vector3Val gPosW = entity->PositionModelToWorld(gPosL);

            _fireSound._sound->SetEWPars(true, gPosW, ammoType->_whistleDist);
          }
        }
      }*/
    }
  }
  else
  {
    // restart sample
    if (!mode->_soundContinuous)
    {
      if (!_fireSound._sound->IsWaiting())
      {
        float obstruction, occlusion;
        GWorld->CheckSoundObstruction(entity, false, obstruction, occlusion);
        //GSoundScene->SimulateSpeedOfSound(_fireSound._sound); // removed for patch 1.04: cause sounds cutting
        _fireSound._sound->Restart(); 
        _fireSound._sound->EnableFilter(true, isInside ? InternalFireLowPFilter : GenericLowPFilter);
        _fireSound._sound->SetPosition(_position, _entity->FutureVisualState().Speed(), true);
        RegisterBulletSound();
      }
    }
    else
    {
      _fireSound._time = Glob.time;
      _fireSound._sound->EnableFilter(true, isInside ? InternalFireLowPFilter : GenericLowPFilter);
      _fireSound._sound->PlayUntilStopValue(Glob.time.toFloat() + _fireSound._sound->GetLength());
    }
  }
}


/// Functor for sound simulation
class WeaponsSoundFunc : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMuzzle([entity, &context](const WeaponModePtr &ptr) -> bool
    {
      WeaponSounds &ws = ptr.GetMuzzle()._sound;
      ws.UpdateSound(context, ptr, entity->GetWeaponSoundPos(context, ptr), entity->RenderVisualState().Speed());
      return false; // continue
    });

    WeaponModePtr selected = context._weapons->ValidatedCurrentWeapon();
    if (selected.IsValid())
    {
      AbstractWave *wave = context._weapons->_reloadMagazineSound;
      if (wave) wave->SetPosition(entity->GetWeaponSoundPos(context, selected), entity->RenderVisualState().Speed());
    }
    {
      AbstractWave *wave = context._weapons->_reloadSound;
      if (wave) wave->SetPosition(entity->RenderVisualState().Position(), entity->RenderVisualState().Speed());
    }
    return false; // for all weapon systems
  }
};


void EntityAIFull::Sound( bool inside, float deltaT )
{
  WeaponsSoundFunc func;
  ForEachTurret(func);
  base::Sound(inside,deltaT);
}


/// Functor for sound unload
class WeaponsUnloadSoundFunc : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMuzzle([](const WeaponModePtr &ptr) -> bool
    {
      ptr.GetMuzzle()._sound.UnloadSound();
      return false; // continue
    });

    if (context._weapons->_reloadMagazineSound)
    {
      context._weapons->_reloadMagazineSound->Stop();
      context._weapons->_reloadMagazineSound = NULL;
    }
    if (context._weapons->_reloadSound)
    {
      context._weapons->_reloadSound->Stop();
      context._weapons->_reloadSound = NULL;
    }
    return false; // for all weapon systems
  }
};

class TurretsClearReflectors : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if(context._turret) context._turret->_lights.Clear();
    return false; // for all weapon systems
  }
};



void EntityAIFull::UnloadSound()
{
  WeaponsUnloadSoundFunc funcWeaponsUnload;
  ForEachTurret(funcWeaponsUnload);

  TurretsClearReflectors funcReflectorsClear;
  ForEachTurret(funcReflectorsClear);

  base::UnloadSound();
}

void EntityAIFull::StartRecoilFF()
{
#ifdef _WIN32
  if (_recoil)
  {
    RecoilFFRamp ramp;
    if (_recoil->GetFFRamp(_recoilTime,ramp))
    {
      float ffCoef = 10;
      float begMag = ramp._begAmplitude * _recoilFactor*ffCoef;
      float endMag = ramp._endAmplitude * _recoilFactor*ffCoef;
      if (GJoystickDevices)
        GJoystickDevices->PlayRamp(begMag,endMag,ramp._duration);
      if (GInput.IsXInputPresent())
        XInputDev->PlayRamp(begMag,endMag,ramp._duration);
    }
    else
    {
      if (GJoystickDevices)
        GJoystickDevices->PlayRamp(0,0,0);
      if (XInputDev)
        XInputDev->PlayRamp(0,0,0);
    }      
  }
  else
  {
    if (GJoystickDevices)
      GJoystickDevices->PlayRamp(0,0,0);
    if (XInputDev)
      XInputDev->PlayRamp(0,0,0);
  }
#endif
}


/*!
React to impulse by force feedback / recoil effect
*/

void EntityAIFull::StartRecoil(RecoilFunction *recoil, float recoilFactor)
{
  if (recoil->GetTerminated(0,0))
    // no need to start recoil that has already been terminated
    return;
  if (_recoil)
  {
    // TODO: several recoils may be running at the same time
    if (recoilFactor<_recoilFactor)
      // current recoil is stronger - prefer it
      return;
  }
  _recoil = recoil;
  _recoilTime = 0;
  _recoilFactor = recoilFactor;
  _recoilIndex = 0;
  _recoilRandomX=GRandGen.RandomValue()+0.5;
  _recoilRandomY=GRandGen.RandomValue()+0.5;
  _recoilRandomZ=GRandGen.RandomValue()+0.5;
  
  if (this==GWorld->CameraOn())
    StartRecoilFF();
}

void EntityAIFull::StartRecoil( RStringB name, float recoilFactor )
{
  Ref<RecoilFunction> recoil = RecoilFunctions.New(name);
  //LogF("Impulse %.2f",factor);
  StartRecoil(recoil,recoilFactor);
}

void EntityAI::CleanUp()
{
  // lights contain links back to the entity - make sure they are released
  _lights.Clear();
  _markers.Clear();
  _markersBlink.Clear();
  _nvgMarkers.Clear();
  _nvgMarkersBlink.Clear();
  UnloadSound();
  PerformUnlock();
  base::CleanUp();
}

void EntityAI::CleanUpMoveOut()
{
  UnloadSound();
  PerformUnlock();
  base::CleanUpMoveOut();

}

#if _VBS3
#include "soldierOld.hpp"
#endif


struct LightInfoExt: public LightInfo
{
  Ref<LightPointOnVehicle> light;
  InitPtr<Object> attachTo;

  float TotalBrightness() const {return (color.Brightness()+ambient.Brightness());}
};

TypeIsMovableZeroed(LightInfoExt);

void EntityAI::AttachLights()
{
  if (!_lightsAttached)
  {
    GWorld->AddAttachment(this);
    _lightsAttached = true;
  }
}

void EntityAI::Simulate(float deltaT, SimulationImportance prec)
{
  // avoid call of ResourceSupply::Simulate when _supplying != NULL && _action == NULL
  // this can happen on remote entity - _supplying is synchronized, _action is not
  // however we need to call remote ResourceSupply::Simulate for taking weapons / magazines
  if (_supply && !IsDamageDestroyed() && (IsLocal() || _supply->GetAction()))
    _supply->Simulate(this,deltaT,SimulateVisibleFar);

  if (_shape && !_static) // do not lock buildings
  {
    if
    (
      HasGeometry() && ( _isStopped || FutureVisualState()._speed.SquareSize()<Square(1.0f) ) &&
      (
        !Airborne() || // lock for vehicle that are not airborne
        // lock also for low static airborne vehicle
        FutureVisualState().Position().Y() < GLandscape->SurfaceYAboveWater(FutureVisualState().Position().X(),FutureVisualState().Position().Z())+15
      )
    )
      LockPosition();
    else
      UnlockPosition();
  }
  float delta;
  delta=_avoidAsideWanted-_avoidAside;
  // if we need more avoidance, or avoidance in a different direction, react fast
  if (fabs(_avoidAsideWanted)>fabs(_avoidAside) || _avoidAside*_avoidAsideWanted<0)
    saturate(delta,-20*deltaT,+20*deltaT);
  else
    saturate(delta,-0.2*deltaT,+0.2*deltaT);
  _avoidAside+=delta;

  SimulateWeaponActivity(deltaT,prec);

  // marker lights
  if (_markers.Size() == 0 && _markersBlink.Size() == 0 && _nvgMarkers.Size() == 0 && _nvgMarkersBlink.Size() == 0)
  {
    if (IsCollisionLight())
    {
      const int maxLights = 4;
      
      LODShape *shape = GLOB_SCENE->Preloaded(Marker);
        
      AutoArray<LightInfoExt, MemAllocLocal<LightInfoExt,8,AllocAlign16> > aggLights;
      for (int i=0; i<GetType()->_lightinfos.Size(); i++)
      {
        const LightInfo &info = GetType()->_lightinfos[i];
        // when the light is permanent and the point not animated, we can do with a static permanent light
        bool needsPositionUpdates = NeedsPositionUpdates(this,-1);
        if (needsPositionUpdates) AttachLights();
        bool needsAttachment = needsPositionUpdates || info.type!=LightTypeMarker;

        Ref<LightPointOnVehicle> light = new LightPointOnVehicle(
          shape, info.color, info.ambient,needsAttachment ? this : NULL,info.position
        );
        if (!needsAttachment)
          light->UpdatePositionTo(FutureVisualState().Transform());
        light->SetRadius(info.radius);

        if (!info.nvgMarker)
        {
          if (info.type == LightTypeMarker)
            _markers.Add(light);
          else if (info.type == LightTypeMarkerBlink)
            _markersBlink.Add(light);
          else
            ErrF("Bad light type %d in %s",info.type,cc_cast(GetType()->GetName()));
        }
        else
        {
          if (info.type == LightTypeMarker)
            _nvgMarkers.Add(light);
          else if (info.type == LightTypeMarkerBlink)
            _nvgMarkersBlink.Add(light);
          else
            ErrF("Bad light type %d in %s",info.type,cc_cast(GetType()->GetName()));
        }

        LightInfoExt &aggInfo = aggLights.Append();
        aggInfo.LightInfo::operator =(info);
        aggInfo.light = light;
        aggInfo.attachTo = needsAttachment ?  this : NULL;

        GLOB_SCENE->AddLightReal(light);
      }

      // caution: this loop is O(N^3)
      while (aggLights.Size()>maxLights)
      {
        // select two most similar lights and merge them
        float minDist = FLT_MAX;
        int bestA=-1,bestB=-1;
        for (int i=0; i<aggLights.Size(); i++) for (int j=i+1; j<aggLights.Size(); j++)
        {
          const LightInfoExt &l1 = aggLights[i];
          const LightInfoExt &l2 = aggLights[j];
          // consider: blink / no blink, distance, color and brightness
          if (l1.type!=l2.type) continue; // different blink never matches
          if (l1.nvgMarker != l2.nvgMarker) continue; // both should be nvgMarker
          if (l1.attachTo != l2.attachTo) continue; // both should have the same attachment status
          float dist = 
          (
            l1.position.Distance(l2.position)
            +(l1.color-l2.color).Brightness()*0.5
            // prefer merging lights with lower brightness
            +floatMin(l1.TotalBrightness(),l2.TotalBrightness())*3.0
          );
          if (minDist>dist)
          {
            bestA = i;
            bestB = j;
            minDist = dist;
          }
        }
        Assert(bestA>=0 && bestB>=0); // some match must be found, only blinking prevents matching
        { // now merge A and B and delete B
          LightInfoExt &a = aggLights[bestA];
          LightInfoExt &b = aggLights[bestB];
          // all parameters as weighted average
          a.ambient = (a.ambient + b.ambient)*0.5;
          a.color = (a.color + b.color)*0.5;
          a.position = (a.position + b.position)*0.5;
          // only radius as as sum
          a.radius = a.radius + b.radius;
          a.light = NULL; // light parameters changed, we need to recreate it
          aggLights.Delete(bestB);
        }
      }

      // process top selected lights
      for (int i=0; i<aggLights.Size(); i++)
      {
        if (i>=maxLights)
          break;
        
        LightInfoExt &info = aggLights[i];
        if (!info.light)
        {
          info.light = new LightPointOnVehicle(shape, info.color, info.ambient,info.attachTo, info.position);
          if (!info.attachTo)
          {
            info.light->UpdatePositionTo(FutureVisualState().Transform());
          }
          info.light->SetRadius(info.radius);
          // add new light into markers/_markersBlink so that we can control it
          (info.type==LightTypeMarkerBlink ? _markersBlink : _markers).Add(info.light);
        }

        info.light->SwitchDayLight(true);
				info.light->SwitchHeatHaze(false);

        GLOB_SCENE->AddLightAgg(info.light);
      }
      
      _markers.Compact();
      _markersBlink.Compact();
      _nvgMarkers.Compact();
      _nvgMarkersBlink.Compact();
      _markersOn = Glob.time - GRandGen.RandomValue();
      _nvBlink = true;
      _timeElapsed = 0.0f;
    }
  }
  else
  {
    if (!IsCollisionLight())
    {
      _markers.Resize(0);
      _markersBlink.Resize(0);
      _nvgMarkers.Resize(0);
      _nvgMarkersBlink.Resize(0);
    }
  }
  if (IsCollisionLight())
  {
    bool on = (toInt(2.0 * (Glob.time - _markersOn)) % 2) != 0; 
    for (int i=0; i<_markersBlink.Size(); i++)
      _markersBlink[i]->Switch(on);

    const Person *person = GWorld->FocusOn() ? GWorld->FocusOn()->GetPerson() : NULL;
    bool nightVision = person ? person->IsNVWanted() : false;

    // marker light visible only in nvg
    for (int i = 0; i < _nvgMarkers.Size(); i++)
      _nvgMarkers[i]->Switch(nightVision);

    static float blinkingPeriod = 1.5f;

    _timeElapsed += deltaT;
    if (_timeElapsed > blinkingPeriod)
    {
      _nvBlink = !_nvBlink;
      _timeElapsed = 0.0f;
    }

    for (int i = 0; i < _nvgMarkersBlink.Size(); i++)
      _nvgMarkersBlink[i]->Switch(_nvBlink & nightVision);
  }

  // Lights

  base::Simulate(deltaT, prec);
}

void EntityAI::SimulatePost(float deltaT, SimulationImportance prec)
{
  // simulate flag
  Texture *texture = GetFlagTexture();
  if (texture)
  {
    int level = _shape->FindSimplestLevel();
    if (level>=0)
    {
      ShapeUsed lock = _shape->Level(level);
      for (int i=0; i<GetType()->GetProxyCountForLevel(level); i++)
      {
        const ProxyObjectTyped &proxy = GetType()->GetProxyForLevel(level,i);
        Flag *veh = dyn_cast<Flag>(proxy.obj.GetRef());
        if (veh)
        {
          SkeletonIndex boneIndex = GetType()->GetProxyBoneForLevel(level,i);
          const VisualState &proxyVs = RenderVisualState();
          Matrix4 proxyTransform = AnimateProxyMatrix(proxyVs, level,proxy.GetTransform(),boneIndex);

          Matrix4 pTransform = proxyVs.Transform() * proxyTransform;

          AdjustFlagTransform(pTransform);
          Vector3 speed(VZero);
          if (_flag)
          {
            if (deltaT > 0)
              speed = (1.0 / deltaT) * (pTransform.Position() - _flag->FutureVisualState().Position());           
          }
          else
          {
            RString shapeName = ::GetShapeName(proxy.name);
            // copy flag - create a new one with the same type, but different shape
            Ref<Entity> flag = GWorld->NewNonAIVehicleWithID(veh->GetNonAIType()->GetName(), shapeName);
            _flag = dyn_cast<Flag,Entity>(flag);
            if (!_flag)
            {
              RptF("No flag - %s,%s,%s",(const char *)flag->GetDebugName(),(const char *)shapeName,
                (const char *)veh->GetNonAIType()->GetName());
              Fail("No flag");
            }
          }
          // proxy may be animation
          if (_flag && _flag->InitAsync(pTransform))
          {
            _flag->SetTransformPossiblySkewed(pTransform);
            _flag->SetSpeed(speed);
            _flag->FlagSimulate(pTransform,deltaT, prec);
          }
          // if we have one flag, we are done - multiple flags are not supported
          break;
        }
      }
    }
  }
  else if (_flag)
    _flag = NULL;

#if _ENABLE_ATTACHED_OBJECTS
  MoveAttached(deltaT);
#endif

#if _VBS2
  _postFrame = *this;   //convoy trainer
#endif
}

void EntityAI::Init(Matrix4Par pos, bool init)
{
  base::Init(pos, init);
  // called before vehicle is placed
  if (_flag)
  {
    Matrix4 trans = pos * _flag->FutureVisualState().Transform();
    _flag->Init(trans, init);
  }

  const EntityAIType *type = GetType();
  
  // create lights
  _lights.Create(this, type->_lights);

  // Moved outside function to avoid launching in some situations
  // OnEvent(EEInit);
}


bool EntityAI::CanBeInstanced(int level, float distance2, const DrawParameters &dp) const
{
  const EntityAIType *type = GetType();
  if (type->_hiddenSelections.Size() > 0) return false;
  return base::CanBeInstanced(level, distance2, dp);
}

/*!
\patch 1.75 Date 3/7/2002 by Jirka
- Added: Support for reload animations for vehicles
\patch 1.78 Date 7/16/2002 by Ondra
- Fixed: AH1 cannon was aiming, but not moving (wrong animation).
*/

void EntityAI::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  ShapeUsed shape=_shape->Level(level);
  if (shape.IsNull())
    return;
  base::Animate(animContext, level, setEngineStuff, parentObject, dist2);
  const EntityAIType *type=GetType();
  Texture *tex;
/*
  tex = GetUnitSign();
  type->_unitNumber.SetTexture(_shape,level,tex);
  if (tex) type->_unitNumber.Unhide(_shape,level);
  else  type->_unitNumber.Hide(_shape,level);
  tex = GetGroupSign();
  type->_groupSign.SetTexture(_shape,level,tex);
  if (tex) type->_groupSign.Unhide(_shape,level);
  else  type->_groupSign.Hide(_shape,level);
  tex = GetSectorSign();
  type->_sectorSign.SetTexture(_shape,level,tex);
  if (tex) type->_sectorSign.Unhide(_shape,level);
  else  type->_sectorSign.Hide(_shape,level);
 */

  if (CommanderUnit() && CommanderUnit()->GetPerson())
    tex = CommanderUnit()->GetPerson()->GetInfo()._squadPicture;
  else
    tex = NULL;
  if (tex != _squadTexture)
    _squadTexture = tex;
  type->_clan.SetTexture(animContext, _shape, level, tex);
  if (tex)
    type->_clan.Unhide(animContext, _shape, level);
  else
    type->_clan.Hide(animContext, _shape, level);

  if( _pilotLight )
    type->_backLights.Unhide(animContext, _shape, level);
  else
    type->_backLights.Hide(animContext, _shape, level);

  // lights
  _lights.Animate(animContext, level, this, type->_lights);

  // hidden selections
  for (int i=0; i<type->_hiddenSelections.Size(); i++)
  {
    if (_hiddenSelectionsTextures[i] || _hiddenSelectionsMaterials[i])
    {
      type->_hiddenSelections[i].Unhide(animContext, _shape, level);
      if (_hiddenSelectionsTextures[i]) type->_hiddenSelections[i].SetTexture(animContext, _shape, level, _hiddenSelectionsTextures[i]);
      if (_hiddenSelectionsMaterials[i]) type->_hiddenSelections[i].SetMaterial(animContext, _shape, level, _hiddenSelectionsMaterials[i]);
    }
    else
      type->_hiddenSelections[i].Hide(animContext, _shape, level);
  }


  // animate materials (we could do hitpoint based animation, but we currently do whole veh. only)
  // check wound level
  if (type->_damageInfo && !type->_damageInfo->IsEmpty())
  {
    float dam = GetRawTotalDamage();
    int damLevel = 0;
    if (dam>=1.0)
      damLevel = 2;
    else if (dam>=0.5)
      damLevel = 1;
    if (damLevel>0)
      type->_totalDamage.Apply(animContext, _shape, level, damLevel, dist2);
  }
}

/// implement ClassTypeInfo for ActivityFadeIn
class ActivityClassTypeInfoFadeIn: public ActivityClassTypeInfo
{
  typedef ActivityClassTypeInfo base;
  
public:
  ActivityClassTypeInfoFadeIn():base("FadeIn"){}
  //@{ implement ActivityClassTypeInfo
  virtual Activity *CreateObject(ParamArchive &ar) const {return NULL;}
  virtual Activity *LoadRef(ParamArchive &ar) const {return NULL;}
  //@}  
};


#include <Es/Memory/normalNew.hpp>

/// perform a fade out to hide rendering artifacts or camera change
class ActivityFadeIn: public EntityActivity
{
  typedef EntityActivity base;
  
  static ActivityClassTypeInfoFadeIn _typeInfo;
  
  OLinkPermNO(AIBrain) _unit;
  float _fadeIn;
  
public:
  ActivityFadeIn(AIBrain *unit);

  //@{ implemented Activity
  virtual void OnEvent(ActivityEvent *event){}
  virtual const ActivityClassTypeInfo &GetTypeInfo() const {return _typeInfo;}
  virtual LSError Serialize(ParamArchive &ar){return LSOK;}
  //@}

  //@{ implementation of EntityActivity
  virtual bool Simulate(Entity *entity, float deltaT, SimulationImportance prec);
  virtual void Draw(Entity *entity);
  //@}
  
  USE_FAST_ALLOCATOR
};

ActivityClassTypeInfoFadeIn ActivityFadeIn::_typeInfo;

CameraType ValidateCamera(CameraType cam);


#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(ActivityFadeIn)

ActivityFadeIn::ActivityFadeIn(AIBrain *unit)
:_unit(unit),_fadeIn(1)
{
}

bool ActivityFadeIn::Simulate(Entity *entity, float deltaT, SimulationImportance prec)
{
  if (_unit && GWorld->FocusOn() == _unit)
    _fadeIn -= deltaT*3;
  else
    _fadeIn = 0;
  return _fadeIn>0;
}

void ActivityFadeIn::Draw(Entity *entity)
{
  if (_unit && GWorld->FocusOn() == _unit && _unit->GetVehicle()==entity)
    GEngine->Draw2DWholeScreen(PackedBlack,_fadeIn);
}


/**
@param unit camera focus unit
*/
void EntityAIFull::StartFadeIn(AIBrain *unit)
{
  _activities.Add(new ActivityFadeIn(unit));
}

bool EntityAI::DrawInPass3() const
{
  Object *parent = GetHierarchyParent();
  if (parent)
  {
    EntityAI *veh = dyn_cast<EntityAI>(parent);
    if (veh && veh->DrawInPass3())
      return true;
  }

  CameraType cam = GWorld->GetCameraTypePreferInternal();
  // when there is a camera effect, we cannot use cockpit pass
  if (GWorld->CameraOn() == this && GWorld->GetCameraEffect() == NULL)
  {
    // when 1st person, pass 3 is always needed
    if (cam == CamInternal) return true;  // cockpits
    // when looking through 3d optics, pass 3 is always needed
    if (cam == CamGunner)
    {
      AIBrain *unit = GWorld->FocusOn();
      Person *person = unit ? unit->GetPerson() : NULL;
      if (person)
      {
        LODShape *oShape = GetOpticsModel(person);
        if (oShape)
          return true;
      }
    }
  }
  // otherwise perform default processing
  return false;
};

/*!
\patch 5127 Date 2/7/2007 by Ondra
- Fixed: Fully closed vehicle interiors were rendered with strange gray shadows with shadow detail Normal or lower.
*/

float EntityAIFull::Pass3IntensityMod(int level, const DrawParameters &dp) const
{
  // Determine own z-space usage
  bool zSpace = dp.ZSpace()!=ZSpaceNormal;
  // Determine whether shadows should be casted and get the shadow coefficients
  float castShadowDiff;
  float castShadowAmb;
  CastPass3VolShadow(level, zSpace, castShadowDiff, castShadowAmb);
  return castShadowDiff;
}

/**
@return tan of the angle difference between current and wanted weapon direction
*/
float EntityAIFull::ComputeAimError(Vector3Par ap, const TurretContext & context, const WeaponModePtr &ptr, Target * target, bool exact) const
{
  // world space direction we need to aim accurately
  Vector3 aimDir;
  // calculate what direction should the weapon be aiming to
  float timeToLead;
  // if aim is not possible, return impossible
  if (!CalculateAimWeapon(context, ptr, aimDir, timeToLead, target, exact))
    return FLT_MAX;
  Vector3 curDir = GetWeaponDirection(FutureVisualState(), context, ptr);

  // check angular difference in X/Y separately

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DECombat) && this == GWorld->CameraOn())
  {
    Matrix3 weaponToWorld(MDirection,curDir,VUp);
    Matrix3 worldToWeapon(MInverseRotation,weaponToWorld);
    Vector3 aimDirRel = worldToWeapon*aimDir;
    float dist = ap.Distance(FutureVisualState().Position());
    float alphaX = atan2(aimDirRel.X(),aimDirRel.Z());
    float alphaY = atan2(aimDirRel.Y(),aimDirRel.Z());
    DIAG_MESSAGE(2000,"Error X %.3f, Y %.3f (dist %.1f, alt %.1f)",alphaX*dist,alphaY*dist,dist,ap.Y()-FutureVisualState().Position().Y());
  }
#endif


  if (GIsManual(context._gunner))
  {
    // get fov from the current camera settings
    //CameraType camType = GWorld->GetCameraType();
    //float fov = GScene->GetCamera()->Left();
    float fov = GetCameraFOV();
    // let vehicle adjust the weapon depending on fov
    curDir = AdjustWeapon(context, ptr, fov, curDir);
  }

  // check current direction, and depending on weapon type estimate the error
  // assume the error is proportional to distance and angle difference

  float cosAlpha = curDir.CosAngle(aimDir);
  if (cosAlpha <= 0.0f)
    return FLT_MAX;
  // if we have cosAlpha^2, following computation could be optimized somewhat
  // probably not worth the effort, though
  // cosAlpha may be above 1 due to rounding errors
  float tanAlpha = (cosAlpha < 1.0f) ? sqrt(1.0f - Square(cosAlpha)) / cosAlpha : 0.0f;

  return tanAlpha;
}


float EntityAIFull::CheckAimed(
  Vector3Par ap, Vector3 speed, const TurretContext &context, const WeaponModePtr &ptr, Target *target, bool exact, const AmmoType *ammo
) const
{
  float dist = ap.Distance(FutureVisualState().Position());
  float error = ComputeAimError(ap, context, ptr, target, exact);
  if (error>=FLT_MAX)
    return 0.0f;

  error *= dist;

  Vector3 leadSpeed = speed - FutureVisualState().Speed();

  float aimPrecision = (GetInvAbility(AKAimingAccuracy) - 1.0f) * 1.5f + 1.0f;
  saturateMax(aimPrecision, 1.0f);

  float tgtSize = target->idExact->AimingSize() * aimPrecision;
  float maxError = tgtSize + ammo->indirectHitRange * 0.3f;
  const WeaponModeType *mode = ptr.GetWeaponModeType();
  maxError += dist * ptr.SumDispersionModifiers(this) * aimPrecision;

  if (leadSpeed.SquareSize() > Square(10))
    maxError *= 2.0f;
  else if (mode->_autoFire && mode->_reloadTime < 0.1f)
    maxError *= 2.0f; // more aggressive use of high rate continuous fire weapon - suppress the enemy

#if _ENABLE_CHEATS
  if ((Object *)this == GWorld->CameraOn() && CHECK_DIAG(DECombat))
    DIAG_MESSAGE(2000,Format("Error %.1f, tgtSize %.1f, maxError %.1f",error,tgtSize,maxError));
#endif
  return error < maxError;
}

float EntityAIFull::GetAimed(const TurretContext &context, const WeaponModePtr &ptr, Target *target, bool exact, bool checkLockDelay) const
{
  if (!ptr.IsValid())
    return 0.0f;
  if (!target || !target->idExact)
    return 0.0f;

  const Magazine *magazine = ptr.GetMagazine();
  const MagazineType *aInfo = magazine ? magazine->Type() : NULL;
  const AmmoType *ammo = aInfo ? aInfo->_ammo : NULL;
  if (!ammo)
    return 1.0f;
  
  if (!CheckTargetKnown(context,target))
    return 0.0f;
  if (!context._gunner)
    return 0.0f;

  AIBrain *unit = context._gunner ? context._gunner->Brain() : NULL;
  if (!unit)
    return 0.0f;
  
  if (!exact && (CheckSuppressiveFire() < SuppressYes) && !target->AccurateEnoughForHit(ammo,unit))
    return 0.0f;

  Vector3 speed;
  Vector3 ap;
  float seenBefore;
  float visible = GetAimingPosition(ap, speed, seenBefore, exact, context, target, ammo);

  if (!exact)
  {
    // even when suppressive fire is allowed, we require a visibility
    // in such case the visibility returned from GetAimingPosition is against the last seen position
    if (visible < 0.3f)
      return 0.0f;
    if (CheckSuppressiveFire()<SuppressYes)
    {
      // check if target was seen recently
      if (!target->SeenRecently(unit))
        return 0.0f;
    }
    else
    {
      float pauseWanted = SuppressiveFireInterval(seenBefore, context, GetSuppressiveFireBoost());
      // if pause was not long enough, wait some more
      if (pauseWanted>=FLT_MAX || context._weapons->_lastShotTime>Glob.time-pauseWanted)
        return 0.0f;
    }
  }

  if (ammo && ammo->_simulation != AmmoShotMissile && ammo->_simulation!=AmmoShotRocket)
  {
    return CheckAimed(ap, speed, context, ptr, target, exact, ammo)*visible;
  }
  else
  {
    Vector3 relPos=ap-FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(),context, ptr));
    // check if target is in front of us
    if (relPos.SquareSize() <= Square(30))
      return 0.0f; // missile fire impossible
    // check if target position is withing missile lock cone
    Vector3 wDir=GetWeaponDirection(FutureVisualState(), context, ptr);
    float wepCos=relPos.CosAngle(wDir);
    float wepAimed = 1.0f - (1.0f - wepCos) * 30.0f;
    saturateMax(wepAimed, 0.0f);


    return visible * wepAimed;
  }
}

typedef StaticArrayAuto< OLink(EntityAI) > EntityAIList;

static void CheckNearFriendlies(EntityAIList &res, const EntityAI *from, const EntityAI *target,
  Vector3Par dir, float maxDistance, float maxAside, float shotSpread)
{
  // check all near friendly troops
  // do not check myself and target
  // collect information about near troops
  int xMin,xMax,zMin,zMax;
  Vector3Val fromPos = from->FutureVisualState().Position();
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,fromPos,fromPos,maxDistance);
  AIBrain *brain = from->CommanderUnit();
  if (!brain) return;

  for (int z=zMin; z<=zMax; z++) for (int x=xMin; x<=xMax; x++)
  {
    const ObjectList &list = GLandscape->GetObjects(z,x);
    for (int oi=0; oi<list.Size(); oi++)
    {
      Object *obj = list[oi];
      Vector3Val tgtPos = obj->FutureVisualState().Position();
      
      // if the object is static, we do not have to care
      // if line-of-sight test returned we are clear, we know we are
      if (obj->Static())
        continue;
      
      if (tgtPos.Distance2(fromPos)>Square(maxDistance))
      {
#if 0 //_ENABLE_CHEATS
        EntityAI *tgt = dyn_cast<EntityAI>(obj);
        if (!tgt)
          continue;
        LogF("%s: Friendly %s too far (%.1f)",
          (const char *)from->GetDebugName(),
          (const char *)tgt->GetDebugName(),
          tgtPos.Distance(fromPos));
#endif
        continue;
      }
      EntityAI *tgt = dyn_cast<EntityAI>(obj);
      if (!tgt)
        continue;
      // target or we cannot prevent LOS
      if (tgt==target || tgt==from)
        continue;
      // check side of the unit
      TargetSide side = tgt->GetTargetSide();
      if (!brain->IsFriendly(side))
        continue;
      if (tgt->IsDamageDestroyed())
        continue;

      // if the object is empty, it will not move, and if we hit it by accident, we will do little harm anyway
      if (!tgt->CommanderUnit())
        continue;      

      // calculate aside distance
      // this is distance of tgt from line fromPos,dir

      Vector3 tgtAimPos = tgt->AimingPosition(tgt->FutureVisualState());
      // find nearest point on line fromPos,dir to tgtAimPos
      float t = dir*(tgtAimPos-fromPos);
      // t is front-back distance from fromPos to tgtAimPos
      if (t<-maxAside*0.5f)
      { // if the friendly unit is behind us, we do not care about it
        /*
        LogF
        (
          "%s: Friendly %s out of aside range (back %.1f)",
          (const char *)from->GetDebugName(),
          (const char *)tgt->GetDebugName(),
          -t
        );
        */
        continue;
      }

      // dir is normalized - for such case NearestPointInfinite can be simplified a lot
      //Vector3 nearestLOF = NearestPointInfinite(fromPos,fromPos+dir,tgtAimPos);
      Vector3 nearestLOF = fromPos+dir*t;
      float asideWanted = floatMin(maxAside,tgt->CollisionSize()*2.0f);
      
      //shotgun spread
      if(shotSpread >0) asideWanted += tan(shotSpread) * fromPos.Distance(nearestLOF);

      // nearest point on the line of fire
      // we predict units movement - how fast is it closing to the line of fire
      Vector3 dirToLOF = (nearestLOF-tgtAimPos).Normalized();
      float movingIntoFire = tgt->PredictVelocity()*dirToLOF;
      // if standing still or moving away, we should not be afraid to fire even when it is quite close
      if (movingIntoFire>0)
      {
        const float estT = 0.5f;
        asideWanted = maxAside+movingIntoFire*estT;
      }

      float nDist2 = nearestLOF.Distance2(tgtAimPos);
      if (nDist2>Square(asideWanted))
      {
        /*
        LogF
        (
          "%s: Friendly %s out of aside range (aside %.1f)",
          (const char *)from->GetDebugName(),
          (const char *)tgt->GetDebugName(),
          nearest.Distance(tgtPos)
        );
        */
        continue;
      }
      /*
      LogF
      (
        "%s: Near friendly - %s",
        (const char *)from->GetDebugName(),
        (const char *)tgt->GetDebugName()
      );
      */
      res.Add(tgt);
    }
  }
}

bool EntityAIFull::CheckFriendlyFire(const TurretContext &context, const WeaponModePtr &ptr, Target *target) const
{
  // if we have no ammo, we can safely fire, we can do no harm
  const AmmoType *ammo = ptr.GetAmmoType();
  if (!ammo) return true;

  float shotSpread =0;
  switch(ammo->_simulation)
  {
    case AmmoShotSpread: //case AmmoShotSpread:
      if (ptr.IsValid())
      {
        const MuzzleType *muzzle = ptr.GetMuzzleType();
        shotSpread = (H_PI/180) * muzzle->_shotSpreadAngle;
      }
    case AmmoShotBullet: //case AmmoShotShell:
    {
      typedef OLink(EntityAI) HelperType;
      AUTO_STATIC_ARRAY(HelperType,nearFriendlies,32);
      // check near part of bullet trajectory
      // 
      Vector3 dir = GetWeaponDirection(FutureVisualState(), context, ptr);

      float distToTarget = target->AimingPosition().Distance(FutureVisualState().Position());
      // if target is very near, be less careful about friendly fire
      // note: the radius seems to be tuned for humans only
      // however, CheckNearFriendlies checks other vehicle types as well
      // still, friendly firing at human is most common and most fatal, therefore we are most interested in it
      float radius = 2.0f;
      float maxDist = 50;
      if (distToTarget<maxDist)
      {
        maxDist = distToTarget;
        radius = 1.0f;
        if (distToTarget<25) radius = 0.5f;
      }
      CheckNearFriendlies(nearFriendlies,this,target->idExact,dir,maxDist,radius,shotSpread);
      if (nearFriendlies.Size()>0)
        // some friendly unit blocks fire - we have to wait
        return false;
    }
  }
  // default: unknown weapon - assume it is safe to fire
  return true;
}

/*!
Armored vehicles do not care much for collisions, but
for soft vehicles, air vehicles or men collision can do much damage
or it can be even critical. 
*/

float EntityAI::AfraidOfCollision(VehicleKind with) const
{
  switch( GetType()->GetKind())
  {
    case VArmor:
      return 1;
    case VSoft:
      if (with==VSoft)
        return 1;
      return 2;
    default:
      return 4; // be very careful
  }
}

/// magnetize aiming speed
// if difference is very low, there is no need to "magnetize"
// if difference is very high, user probably wants to to somethings else
static float Magnetize(float aimX, float autoAimX, float factor, float veryLow, float normal, float veryHigh)
{
  float diff = fabs(aimX-autoAimX);
  float magFactor = 0;
  const float veryLowMag = 0.3f;
  const float normalMag = 1.0f;
  const float veryHighMag = 0.1f;
  if (diff>=veryHigh)
    magFactor = veryHighMag;
  else if (diff>=normal)
    // normal..veryHigh max. magnetization with normal, veryHighMag with veryHigh
    magFactor = (diff-normal)/(veryHigh-normal)*(veryHighMag-normalMag)+normalMag;
  else if (diff>=veryLow)
    // veryLow..normal max. magnetization with normalMag, veryLowMag with veryLow 
    magFactor = (diff-veryLow)/(normal-veryLow)*(normalMag-veryLowMag)+veryLowMag;
  else
    magFactor = veryLowMag;
  /*
  DIAG_MESSAGE(
    500,Format(
      "Mag factor %+.3f, diff %+.3f, normal %+.3f",
      magFactor,diff,normal
    )
  );
  */
  magFactor *= factor;
  
  // if seems some things should never be done:
  // 1) never change sign
  // 2) never move when user does not move
  //float magnetized = aimX*(1-factor)+autoAimX*factor;
  //return magFactor*magnetized+(1-magFactor)*aimX;
  float magnetized = aimX*(1-magFactor)+autoAimX*magFactor;
  
  if (magnetized*aimX<0)
    // we attempt to change a sign - this is bad
    return 0.01f*fSign(aimX);
  return magnetized;
}

/// adjust cursor movement based on user input and 
void EntityAIFull::AutoAimCursor(const TurretContextEx &context, ValueWithCurve &aimX, ValueWithCurve &aimY, float deltaT, float fov)
{
  if (/*Square(aimX._value)+Square(aimY._value)<=Square(0.005) ||*/ deltaT<=0)
    return;
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  bool autoAim = true;
  Vector3 weaponPos = FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(), context, ptr));
  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    autoAim = muzzle->_canAutoAim;
  }
  if(!autoAim || !Glob.config.IsEnabled(DTAutoAim)) return;
  Vector3 cursorDir = GetWeaponDirectionWanted(context, ptr);
  Target *autoAimTarget = _autoAimLocked.NotNull() ?
    safe_cast<Target *>(_autoAimLocked) : SelectAutoAimTarget(context, ptr, cursorDir, 4, false);
  if (autoAimTarget)
  {
    // check which target is autoaim-locked
    TargetType *tgt = autoAimTarget->idExact;
    if (tgt && ptr.IsValid())
    {
      Vector3 weaponPosModel = GetWeaponPoint(FutureVisualState(), context, ptr);
      //Vector3 weaponPos = PositionModelToWorld(GetWeaponPoint(weapon));
      Vector3 tgtSpd = DirectionWorldToModel(tgt->PredictVelocity() - PredictVelocity());
      // this requires z-coordinate from the position
      // we need predicted aiming position for given weapon
      Vector3 aimRelPos = PositionWorldToModel(tgt->AimingPosition(tgt->FutureVisualState()))-weaponPosModel;
      
      Vector3 aimPos;
      float timeToLead;
      bool aim = CalculateAimWeapon(context, ptr, aimRelPos, timeToLead,autoAimTarget, true);
      if (aim)
        aimRelPos = DirectionWorldToModel(aimRelPos);     
      
      // calculate angular speed
      // this is a speed needed to keep cursor position to the relative target unchanged
      float autoAimX = atan2(tgtSpd.X(),aimRelPos.Z());
      float autoAimY = atan2(tgtSpd.Y(),aimRelPos.Z());

      // besides of magnetization apply slight auto-aim
      if (aim)
      {
        // calculate angular speed needed to aim at the target within given time
        // auto-aim time should depend on current fov
        const float invAimTime = fov/2.0f;
        Vector3 wepDir = GetWeaponDirectionWanted(context, ptr);
        // convert to model space
        Vector3 wepDirModel = DirectionWorldToModel(wepDir);
        // aimRelPos is already in model space
        // x,y difference
        // note: if aimRelPos.Z() is big, atan2 may be replaced with 1/aimRelPos.Z()
        float aimDifX = atan2(aimRelPos.X(),aimRelPos.Z())-atan2(wepDirModel.X(),wepDirModel.Z());
        float aimDifY = atan2(aimRelPos.Y(),aimRelPos.Z())-atan2(wepDirModel.Y(),wepDirModel.Z());
        
        aimDifX *= invAimTime;
        aimDifY *= invAimTime;
  
#if 0
        DIAG_MESSAGE(500,Format("Aim %+.3f,%+.3f -> %+.3f,%+.3f, spd %+.3f,%+.3f",
            aimX._value,aimY._value,autoAimX,autoAimY,aimDifX,aimDifY));
#endif
        autoAimX += aimDifX;
        autoAimY += aimDifY;
      }
      
      //const float factor = GWorld->GetCameraType()!=CamGunner ? 0.9f : 0.5f;
      //const float factor = 0.9f;
      // magnetize aiming
      // what is "low" depends on target distance
      //float veryLow = ( aimRelPos.Z()>25 ? 2.5f/aimRelPos.Z() : 2.5f/100 );
      //float normal = veryLow*2;
      //float veryHigh = veryLow*4;

      // apply curve centered around autoAimX, autoAimY
      // centering should be done by stretching the curve
      // we need both positive and negative area
      //IActionResponseCurve curve;
      
      //aimX._value = Magnetize(aimX._value,autoAimX,factor,veryLow,normal,veryHigh);
      //aimY._value = Magnetize(aimY._value,autoAimY,factor,veryLow,normal,veryHigh);
      aimX._center = autoAimX;
      aimY._center = autoAimY;

      //DIAG_MESSAGE(500,Format(" -> %.2f,%.2f",aimX._value,aimY._value));
    }
  }
}

/*!
\patch 2.01 Date 1/8/2003 by Ondra
- New: Auto aiming option.
@param enlarge multiplying factor controlling autoaim area
@param limitSize when true, only reasonably large targets are auto aimed
*/

Target *EntityAIFull::SelectAutoAimTarget(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction, float enlarge, bool limitSize) const
{
  const float maxAllowDist = Glob.config.GetAutoAimDistance()*enlarge;
  const float maxAngle = Glob.config.GetAutoAimAngle()*enlarge;
  const float autoAimSizeFactor = Glob.config.GetAutoAimSizeFactor();
  
  AIBrain *unit = EffectiveGunnerUnit(context);
  if (!unit) return NULL;
  
  //bool autoAim = true;
  Vector3 weaponPos = FutureVisualState().PositionModelToWorld(GetWeaponPoint(FutureVisualState(),context, ptr));
  //if (weapon>=0 && weapon<NMagazineSlots())
  //{
  //  const MagazineSlot &slot = GetMagazineSlot(weapon);
  //  const MuzzleType *muzzle = slot._muzzle;
  //  autoAim = muzzle->_canAutoAim;
  //}
  
  // check if any target is close enough to the aiming direction
  const TargetList *tgts = unit->AccessTargetList();
  if (!tgts) return NULL;
  float minDist = FLT_MAX;
  Target *nearestTgt = NULL;
  // TODO: use Unknown as well
  for (int i=0; i<tgts->EnemyCount(); i++)
  {
    Target *tgt = tgts->GetEnemy(i);
    if (!tgt) continue;
    //if (!tgt->isKnown) continue;
    TargetType *tgtExact = tgt->idExact;
    if (tgtExact==NULL || tgtExact==this) continue;
    if (!tgtExact->IsInLandscape()) continue;
    //if (tgt->posError.SquareSize()>Square(1)) continue;
    // check based on real side
    TargetSide side = tgtExact->GetTargetSide();
    if (!unit->IsEnemy(side) && side!=TSideUnknown) continue;
    
    // if angle to target is too high, do not aim
    Vector3 aimPos = tgtExact->AimingPosition(tgtExact->FutureVisualState());
    float cosAlpha = direction.CosAngle(aimPos-weaponPos);
    if (cosAlpha<0) continue; // behind us - ignore

    // if possible, calculate position including lead
    //bool aimed =
    float timeToLead;
    CalculateAimWeaponPos(context, ptr,aimPos,timeToLead,tgt,true);
    
    // check distance of the target from the direction line
    float dist = NearestPointDistance(weaponPos,weaponPos+direction,aimPos,0,FLT_MAX);

    LODShape *shape = tgtExact->GetShape();
    float visibleRadius = shape ? shape->GeometrySphere() : 0.1f;

    float tgtRadius = visibleRadius;
    tgtRadius *= autoAimSizeFactor;
    if (dist-tgtRadius>maxAllowDist) continue;
    if (dist>minDist) continue;
    // if we are pointing outside of the aiming sphere, check how much out we are
    if (dist>tgtRadius)
    {
      float tgtDist = aimPos.Distance(weaponPos);
      // check angular distance
      // angular size of given target
      float gamma = atan2(tgtRadius,tgtDist);
      // angular size of distance of given target from the weapon line
      float alpha = atan2(dist,tgtDist);
      float angle = alpha-gamma;
      // note: break was here - but it makes no sense.
      // If the target is ignored, we should check another one
      if (angle>maxAngle) continue;
    }
    if (limitSize)
    {
      // check visual size of the target
      // check fogginess
      // reasonable size calculated on TV screen (480 lines display)
      const float minSize = 20.0f/480;
      float tgtDist = aimPos.Distance(weaponPos);
      // we use linear approximation, however we want the attenuation
      // to start quite far from the viewer
      const float fogFactor = 1.6f;
      float fogAttenuation = floatMin(1,fogFactor-tgtDist*fogFactor/Glob.config.tacticalZ);
      
      #if _DEBUG
        float screenSize = GScene->GetCamera()->InvTop()*(2*visibleRadius)/tgtDist;
        (void)screenSize;
      #endif //if (screenSize<minSize) continue;
      if (GScene->GetCamera()->InvTop()*visibleRadius*fogAttenuation<(minSize*0.5f)*tgtDist) continue;
    }
    // possible optimization:
    // atg x1 - atg x2 = atg ((x1-x2)/(1+x1*x2))
    // alpha-gamma>maxAngle should be equivalent to
    // atg(dist/tgtDist) - atg(tgtRadius/tgtDist) > maxAngle
    // ((dist-tgtRadius)*tgtDist) /(tgtDist^2+dist*tgtRadius)> tg maxAngle
    // ((dist-tgtRadius)*tgtDist) > tg maxAngle * (tgtDist^2+dist*tgtRadius)

    // check if engaging target with the weapon makes sense in given situation
    // ShootHasSense - no reason to use this for Dayz, targets are almost always zombies or players
    //bool fire = ptr.IsValid() && ShootHasSense(*tgt,context,ptr,dist);
    //if (!fire) continue;
    // check if there is currently some visibility
    // if not, ignore the target
    bool headOnly = false;
    float visibility = GetTrackedVisibility(headOnly,unit,context,ptr,*tgt,0.9f);
    if (visibility<0.1f) continue;
    // GetTrackedVisibility return if we can shoot through
    // we should check if we can really see the target
    /*float visible=GLandscape->Visible
    (
      weaponPos,this,tgtExact,0.9f,ObjIntersectView
    );*/
    float visible = GWorld->Visibility(unit,tgtExact,0.15f);
    if (visible<0.1f) continue;

    minDist = dist;
    nearestTgt = tgt;
  }
  return nearestTgt;
}

/**
This function is used to select a target for weapons which can auto-lock on a plane
*/
Target *EntityAIFull::SelectLockTarget(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction, const AmmoType *ammo, float minAngle) const
{
  const float minCosAngle = (minAngle < 0)? 0.93969262079 : minAngle; // cos 20deg
  
  AIBrain *unit = EffectiveGunnerUnit(context);
  if (!unit) return NULL;
  
  Vector3 weaponPos = RenderVisualState().PositionModelToWorld(GetWeaponPoint(RenderVisualState(),context, ptr));
  
  // physical device - check real positions
  const TargetList *tgts = unit->AccessTargetList();
  if (!tgts) return NULL;
  float maxCosAngle = minCosAngle;
  Target *nearestTgt = NULL;
  // we might optimize this, but as it is used only for player, it does not seem necessary
  for (int i=0; i<tgts->AnyCount(); i++)
  {
    Target *tgt = tgts->GetAny(i);
    if (!tgt) continue;
    //if (!tgt->isKnown) continue;
    TargetType *tgtExact = tgt->idExact;
    if (tgtExact==NULL || tgtExact==this) continue;
    if (!tgtExact->IsInLandscape()) continue;
    
    // if angle to target is too high, do not aim
    Vector3 aimPos = tgtExact->AimingPosition(tgtExact->RenderVisualState());
    float cosAlpha = direction.CosAngle(aimPos-weaponPos);
    if (cosAlpha<maxCosAngle) continue; // no better - ignore

    if (!tgtExact->LockPossible(ammo))
      continue;

    // if not, ignore the target
    bool headOnly = false;
    float visibility = GetTrackedVisibility(headOnly,unit,context,ptr,*tgt,0.9f);
    if (visibility<0.1f)
      continue;
    // GetTrackedVisibility return if we can shoot through
    // we should check if we can really see the target
    float visible = GWorld->Visibility(unit,tgtExact,0.15f);
    if (visible<0.1f)
      continue;

    maxCosAngle = cosAlpha;
    nearestTgt = tgt;
  }
  return nearestTgt;
}

bool EntityAIFull::IsAutoAimEnabled(const TurretContext &context, const WeaponModePtr &ptr) const
{
  bool autoAim = true;
  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (muzzle) autoAim = muzzle->_canAutoAim;
  }
  return autoAim;
}

/// in a small area around the aim point the player may override autoaim

const float AutoAimOverride = 0.15f;

void EntityAIFull::AimWeaponManDir(const TurretContextEx &context, const WeaponModePtr &ptr, Vector3Par direction )
{
  _autoAimLocked = NULL;
  AIBrain *unit = EffectiveGunnerUnit(context);
  
  bool autoAim = IsAutoAimEnabled(context, ptr);
  
  if( unit && autoAim && Glob.config.IsEnabled(DTAutoAim) && GWorld->GetCameraType()!=CamGunner)
  {
    Target *nearestTgt = SelectAutoAimTarget(context, ptr, direction);
    if (nearestTgt)
    {
      Vector3 adjDir;
      // lead target, consider gravity ...
      float timeToLead;
      if (!CalculateAimWeapon(context, ptr,adjDir,timeToLead,nearestTgt,true)) return;
      
      // calculate how large the hit area around the target is
      EntityAI *tgtExact = nearestTgt->idExact;
      if (!tgtExact) return;
      LODShape *shape = tgtExact->GetShape();
      float visibleRadius = shape ? shape->GeometrySphere()*AutoAimOverride : 0.1f;
      // angle of the hit area
      float maxAngle = visibleRadius*adjDir.InvSize();
      
      // force direction toward the adjDir, keep max. difference at angle
      float diffCosAngle = direction.CosAngle(adjDir);
      float diffAngle = acos(diffCosAngle);
      if (diffAngle<maxAngle)
      {
        // we are within the autoaim area, do not change direction at all
#if 0 //_PROFILE
        DIAG_MESSAGE(100,Format("Angle %g",diffAngle/maxAngle));
#endif
        adjDir = direction;
      }
      else
      {
        Vector3 adNorm = adjDir.Normalized();
        Vector3 dirDiff = direction.Normalized()-adNorm;
        dirDiff.Normalize();
        adjDir = adNorm + dirDiff*maxAngle;
      }
      
      AimWeaponAdjusted(context, ptr, adjDir);
      _autoAimLocked = nearestTgt;
      return;
    }
  }
  AimWeapon(context, ptr, direction);
}

float EntityAIFull::GetAimAccuracy(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par direction) const
{
  Target *nearestTgt = SelectAutoAimTarget(context, ptr, direction);
  if (!nearestTgt)
    return 0.0f;
  // calculate how large the hit area around the target is
  float autoaimAcc = 1.0f;
  EntityAI *tgtExact = nearestTgt->idExact;
  if (tgtExact)
  {
    Vector3 adjDir;
    // lead target, consider gravity ...
    float timeToLead;
    if (CalculateAimWeapon(context, ptr, adjDir, timeToLead, nearestTgt, true))
    {
      LODShape *shape = tgtExact->GetShape();
      float visibleRadius = shape ? shape->GeometrySphere() * AutoAimOverride : 0.1f;
      // angle of the hit area
      float maxAngle = visibleRadius * adjDir.InvSize();
      
      // force direction toward the adjDir, keep max. difference at angle
      float diffAngle = acos(direction.CosAngle(adjDir));
      autoaimAcc = (diffAngle < maxAngle) ? 1.0f - 0.5f * diffAngle / maxAngle : 0.5f;
    }
  }
  // if not aimed, do not indicated auto-aim accuracy  
  float aimed = GetAimed(context, ptr, nearestTgt, true, false);
  return aimed * autoaimAcc;
}

bool EntityAIFull::GetWeaponCartridgePos(const TurretContext &context, const WeaponModePtr &ptr, Matrix4 &pos, Vector3 &vel) const
{
  ObjectVisualState const& vs = RenderVisualState();

  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();

    if (muzzle->_cartridgeOutPosIndex<0 || muzzle->_cartridgeOutEndIndex<0)
      return false;
    int memLevel = _shape->FindMemoryLevel();

    Vector3 cPos = AnimatePoint(vs,memLevel,muzzle->_cartridgeOutPosIndex);
    Vector3 cEnd = AnimatePoint(vs,memLevel,muzzle->_cartridgeOutEndIndex);

    Vector3 cDir = GetWeaponDirection(vs, context, ptr);

    vs.PositionModelToWorld(cPos,cPos);
    vs.PositionModelToWorld(cEnd,cEnd);

    vel = (cEnd - cPos)*3;

    // create matrix for cartridge position
    pos.SetDirectionAndUp(cDir,VUp);
    pos.SetPosition(cPos);
    return true;
  }
  // convert neutral gun position
  pos = MIdentity;
  vel = VZero;
  return false;
}

bool EntityAIFull::SetWeaponReloadTime(Person *gunner, RString muzzleName, float reloadTime)
{
  saturate(reloadTime,0,1);
  if(!gunner || muzzleName.GetLength() == 0) return false;
  
  //gunner is able
  if (!gunner->Brain() || gunner->Brain()->GetLifeState() != LifeStateAlive) return false;

  TurretContextEx currContext; 

  if (FindTurret(gunner, currContext))
  {
    WeaponsState *weapons = currContext._weapons;

    return weapons->ForEachMuzzle([muzzleName, reloadTime](const WeaponModePtr &ptr) -> bool
    {
      RString curName = ptr.GetMuzzleType()->GetName();
      if (!strnicmp(muzzleName.Data(), curName.Data(), muzzleName.GetLength()))
      {
        Magazine *magazine = unconst_cast(ptr.GetMagazine());
        if (magazine) magazine->_reload = reloadTime;
        return true; // found
      }
      return false; // continue
    });
  }
  return false;
}

bool EntityAIFull::GetWeaponLoaded(const WeaponsState &weapons, const WeaponModePtr &ptr) const
{
  if (!ptr.IsValid()) return true;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine || !magazine->Type()) return true; // no ammo
  const AmmoType *ammo = magazine->Type()->_ammo;
  if (!ammo) return true; // no ammo
  if (ammo->_simulation == AmmoNone) return true; // no ammo
  if (magazine->_reload > 0) return false; // not loaded
  if( magazine->_reloadMagazine>0 ) return false; // not loaded
  if (IsActionInProgress(MFReload)) return false; // not loaded

  if( magazine->GetAmmo()<=0 ) return false; // no ammo
  return true;
}

bool EntityAIFull::GetWeaponReady(const WeaponsState &weapons, const WeaponModePtr &ptr, Target *target) const
{
  if (!ptr.IsValid()) return false;
  const WeaponModeType *mode = ptr.GetWeaponModeType();
  if (!mode) return false;

  if (!target) return true;
  // for guided missiles / bombs wait until previous shot is done
  
  if (GetType()->CanBeAirborne())
  {
    const Magazine *magazine = ptr.GetMagazine();
    if (weapons._lastShot && magazine && magazine->Type()->_ammo)
    {
      const AmmoType *ammo = magazine->Type()->_ammo;
      if (ammo->_simulation==AmmoShotMissile && ammo->maxControlRange>10)
      {
        // we are getting ready to fire a guided weapon
        // check if the last shot is a guided weapon
        Missile *pending = dyn_cast<Missile,Entity>(weapons._lastShot);
        if (pending && pending->Type()->maxControlRange>10)
        {
          // a guided shot is pending - wait until it explodes
          // TODO: after a hit we should still wait for a while to check if the target is destroyed
          return false;
        }
        // TODO: if the explosion is pending, wait until we see the result
      }
    }
  }
  float timeFromLastShot = Glob.time-weapons._lastShotTime;
  float rateOfFire = mode->_aiRateOfFire;
  if (timeFromLastShot>rateOfFire) return true;
  float dist2 = target->AimingPosition().Distance2(FutureVisualState().Position());
  float maxDist2 = Square(mode->_aiRateOfFireDistance);
  if (dist2<maxDist2)
    return Square(timeFromLastShot)*maxDist2>Square(rateOfFire)*dist2;
  else
    return timeFromLastShot>rateOfFire;
}


bool EntityAIFull::FireMissile(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par offset, Vector3Par direction, Vector3Par initSpeed,
  TargetType *target, bool createNetworkObject, bool remote, bool forceLock)
{
  if (_isDead || _isUpsideDown)
    return false;

  const AmmoType *type = ptr.GetAmmoType();
  //if( !type || !aInfo ) return false;

  const WeaponModeType *mode = ptr.GetWeaponModeType();
  Assert(mode);

  bool real;
  if (mode->_autoFire)
  {
    // in this case, fire is processed where the effective gunner is (workaround)
    AIBrain *gunnerUnit = EffectiveGunnerUnit(context);
    real = gunnerUnit ? gunnerUnit->GetPerson()->IsLocal() : IsLocal();
  }
  else
    // correctly, the fire is simulated where the weapon system is local
    real = context._turret ? context._turret->IsLocal() : IsLocal();
  if (!real && !remote)
    return true;
  
  const VisualState &vs = RenderVisualState();
  Vector3 initSpeedWorld = vs.DirectionModelToWorld(initSpeed);

  Shot *shot = NewShot(this, type, &ptr, target, !remote);
  shot->SetOrient(direction, VUp);
  shot->SetSpeed(vs._speed + initSpeedWorld);
  shot->SetPosition(vs.PositionModelToWorld(offset));
  GWorld->AddFastVehicle(shot);
  GWorld->AddSupersonicSource(shot);

  if (createNetworkObject && GWorld->GetMode() == GModeNetware)
    // shot created
    GetNetworkManager().CreateObject(shot);
#if _ENABLE_CHEATS
  if (this==GWorld->CameraOn() && CHECK_DIAG(DECombat))
    GWorld->SwitchCameraTo(shot,CamInternal,true);
#endif

  context._weapons->_lastShot = shot;
  AfterFire();
  return true;
}

bool EntityAIFull::FireShell(const TurretContext &context, const WeaponModePtr &ptr, Vector3Par offset, Vector3Par direction,
  TargetType *target, bool createNetworkObject, bool remote)
{
  // fire
  if (_isDead || _isUpsideDown)
    return false;

  if (!ptr.IsValid()) return false;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return false;
  const MagazineType *aInfo = magazine->Type();
  const WeaponModeType *mode = ptr.GetWeaponModeType();
  Assert(mode);
  const AmmoType *type = aInfo ? aInfo->_ammo : NULL;
  if (!type) return false;

  bool real;
  if (mode->_autoFire)
  {
    // in this case, fire is processed where the effective gunner is (workaround)
    AIBrain *gunnerUnit = EffectiveGunnerUnit(context);
    real = gunnerUnit ? gunnerUnit->GetPerson()->IsLocal() : IsLocal();
  }
  else
    // correctly, the fire is simulated where the weapon system is local
    real = context._turret ? context._turret->IsLocal() : IsLocal();
  if (!real && !remote)
    return true;
  

  bool showTracer = magazine->GetAmmo() <= aInfo->_lastRoundsTracer
      || (aInfo->_tracersEvery > 0 
          && ((magazine->GetAmmo() - aInfo->_lastRoundsTracer) % aInfo->_tracersEvery) == 0
         );
  Shot *shot = NewShot(this,type, &ptr, NULL,!remote, showTracer);
  
  const VisualState &vs = RenderVisualState();
  const Shot::VisualState &svs = shot->FutureVisualState();

  shot->SetOrient(direction,VUp);
  // do random dispersion of projectiles
  const int gausPassNum=4;
  float gaussX=0,gaussY=0;
  for( int c=gausPassNum; --c>=0;  )
  {
    gaussX+=GRandGen.RandomValue();
    gaussY+=GRandGen.RandomValue();
  }
  gaussX*=2.0/gausPassNum;
  gaussY*=2.0/gausPassNum;

  float randomX=(gaussX-1)*ptr.SumDispersionModifiers(this);
  float randomY=(gaussY-1)*ptr.SumDispersionModifiers(this);

  if( QIsManual() )
  {
    IF_FADE()
    {
      randomX *= 20;
      randomY *= 20;
    }
  }

  Vector3 dir(randomX,randomY,1);
  svs.DirectionModelToWorld(dir,dir);
  shot->SetOrient(dir,svs.DirectionUp());
  // final speed and position

  float ammoSpeed = aInfo->_initSpeed;

#if _VBS3 //reduce speed of throwing objects when laying down
  ammoSpeed *= GetAmmoInitSpeedFactor(aInfo);
#endif

  Vector3 initSpeedWorld = svs.Direction()*ammoSpeed;

  shot->SetSpeed(vs._speed+ initSpeedWorld);
  shot->SetPosition(vs.PositionModelToWorld(offset));
#if _VBS2 // suppression and Morale
  // Inform the soldier's morale class that he's fired a round.
  Man* theShooter = dyn_cast<Man>(this);
  if (theShooter) theShooter->HaveFired();

  // Make a list of soldiers who may be suppressed by round
  ShotShell *ss = dyn_cast<ShotShell>(shot);
  ss->BuildSuppressionList();
#endif
  GWorld->AddFastVehicle(shot);
  GWorld->AddSupersonicSource(shot);
  GWorld->AddFakeBulletsSource(shot);

  if (createNetworkObject && GWorld->GetMode() == GModeNetware)
    // shot created
    GetNetworkManager().CreateObject(shot);

  context._weapons->_lastShot = shot;

  //ADD_COUNTER(wFire,1);
  AfterFire();
  return true;
}

void EntityAIFull::AfterFire()
{
  _aimRandom = GRandGen.RandomValue();
}

RStringB EntityAIFull::GetRecoilName(const WeaponModeType *mode) const
{
  return mode->_recoilName;
}

bool EntityAIFull::FireMGun(const TurretContext &context, const WeaponModePtr &ptr,
  Vector3Par offset, Vector3Par direction,
  TargetType *target, bool createNetworkObject, bool remote)
{
  if( _isDead || _isUpsideDown ) return false;

  // fire
  if (!ptr.IsValid()) return false;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return false;
  const MagazineType *aInfo = magazine->Type();
  if (!aInfo) return false;
  const AmmoType *type = aInfo->_ammo;
  if (!type) return false;
  const WeaponModeType *mode = ptr.GetWeaponModeType();
  const MuzzleType &info = *ptr.GetMuzzleType();

  bool real;
  if (mode->_autoFire)
  {
    // in this case, fire is processed where the effective gunner is (workaround)
    AIBrain *gunnerUnit = EffectiveGunnerUnit(context);
    real = gunnerUnit ? gunnerUnit->GetPerson()->IsLocal() : IsLocal();
  }
  else
  {
    // correctly, the fire is simulated where the weapon system is local
    real = context._turret ? context._turret->IsLocal() : IsLocal();
  }
  if (!real && !remote) return true;

  bool showTracer = magazine->GetAmmo() <= aInfo->_lastRoundsTracer
    || (aInfo->_tracersEvery > 0 
         && ((magazine->GetAmmo() - aInfo->_lastRoundsTracer) % aInfo->_tracersEvery) == 0
       );
  Shot *shell = NewShot(this,type, &ptr, NULL,!remote, showTracer);
  const Shot::VisualState &svs = shell->FutureVisualState();
  const VisualState &vs = RenderVisualStateScope(true);

  shell->SetOrient(direction,VUp);
  // do random dispersion of projectiles
  const int gausPassNum=4;
  float gaussX=0,gaussY=0;
  for( int c=gausPassNum; --c>=0; )
  {
    gaussX+=GRandGen.RandomValue();
    gaussY+=GRandGen.RandomValue();
  }
  gaussX*=2.0/gausPassNum;
  gaussY*=2.0/gausPassNum;
  float randomX=(gaussX-1)*(ptr.SumDispersionModifiers(this));
  float randomY=(gaussY-1)*(ptr.SumDispersionModifiers(this));
  if( !QIsManual() )
  {
    AIBrain *unit = context._gunner ? context._gunner->Brain() : NULL;
    float invAimingAccuracy = unit ? unit->GetInvAbility(AKAimingAccuracy) : 1;
    float coefX,coefY;
    // never allow AI to have more precise weapons than the player has
    info.GetDispersionCoefs(coefX,coefY,floatMinMax(invAimingAccuracy,1,1e3));
    randomX*=coefX;
    randomY*=coefY;
  }
  else
  {
    IF_FADE()
    {
      randomX *= 20;
      randomY *= 20;
    }
  }


  Vector3 dir(randomX,randomY,1);
  svs.DirectionModelToWorld(dir,dir);
  shell->SetOrient(dir,svs.DirectionUp());
  // final speed and position
  shell->SetSpeed(vs._speed+dir*aInfo->_initSpeed);
  shell->SetPosition(vs.PositionModelToWorld(offset));
#if _VBS2 // suppression
  // Inform the soldier's morale class that he's fired a round.
  Man* theShooter = dyn_cast<Man>(this);
  if (theShooter) theShooter->HaveFired();

  // Make a list of soldiers who may be suppressed by round.
  ShotShell *ss = dyn_cast<ShotShell>(shell);
  ss->BuildSuppressionList();
#endif
  GWorld->AddFastVehicle(shell);
  GWorld->AddSupersonicSource(shell);
  GWorld->AddFakeBulletsSource(shell);

  if (createNetworkObject && GWorld->GetMode() == GModeNetware)
  {
    // shot created
    GetNetworkManager().CreateObject(shell);
  } 

  context._weapons->_lastShot = shell;

  //ADD_COUNTER(wFire,1);
  AfterFire();
  return true;
}



const float DeadAbility=0.2; // private

float EntityAI::GetAbility(AIBrain *unit, AbilityKind kind) const
{
  if (!unit) unit = CommanderUnit();
  if( !unit ) return 1/DeadAbility;
  return unit->GetInvAbility(kind);
}

float EntityAI::GetInvAbility(AIBrain *unit, AbilityKind kind) const
{
  if (!unit) unit = CommanderUnit();
  if( !unit ) return DeadAbility;
  return unit->GetAbility(kind);
}

/**
@return value from 1 (able) to 0.2 (unable)
*/
float EntityAI::GetAbility(AbilityKind kind) const
{
  AIBrain *brain=CommanderUnit();
  if( !brain ) return DeadAbility;
  return brain->GetAbility(kind);
}

/**
@return value from 1 (able) to 5 (unable)
*/
float EntityAI::GetInvAbility(AbilityKind kind) const
{
  AIBrain *brain=CommanderUnit();
  if( !brain ) return 1/DeadAbility;
  return brain->GetInvAbility(kind);
}

float EntityAIType::GetCostTurn(int difDir, int &debet, bool forceFullTurn) const
{
  debet = 0;
  return 1.0;
}

FieldCost EntityAIType::GetRoadCost(CombatMode mode) const
{
  return GetTypeCost(OITRoad, mode, ClearanceNormal)*0.7f;
}

FieldCost EntityAIType::GetTypeCost( OperItemType type, CombatMode mode, Clearance clearance ) const
{
  static const float costs[] =
  {
    1.0, // OITNormal,
    3.0, 3.0, 3.0, //OITAvoidBush, OITAvoidTree, OITAvoid
    SET_UNACCESSIBLE,  // OITWater,
    SET_UNACCESSIBLE, // OITSpaceRoad
    0.5, //  OITRoad
    SET_UNACCESSIBLE, // OITSpaceBush
    SET_UNACCESSIBLE, // OITSpaceHardBush
    SET_UNACCESSIBLE, // OITSpaceTree
    SET_UNACCESSIBLE, // OITSpace
    0.5 // OITRoadForced
  };
  Assert(sizeof(costs)/sizeof(*costs)==NOperItemType);
  return FieldCost(costs[type],costs[type]);
}

int EntityAIType::GetTargetCategory() const
{
  return GWorld->GetTargetCategories().GetCategory(_targetCategory);
}


/*!
\patch 1.12 Date 08/07/2001 by Ondra
- New: Force feedback effect on fire / hit.
\patch 1.30 Date 11/02/2001 by Ondra
- Fixed: Speed of sound simulation caused distant fullauto sounds muted (since 1.27)

\patch 5143 Date 3/22/2007 by Ondra
- New: New config value soundBurst to allow playing sound for each bullet in a burst
*/

bool EntityAIFull::FireWeaponEffects(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine, EntityAI *target, const RemoteFireWeaponInfo *remoteInfo)
{
  if (!ptr.IsValid()) return false;
  if (ptr.GetMagazine() != magazine) return false;

  if (remoteInfo)
  {
    // create a fake shot if necessary for remote units
    ProcessFireWeapon(context, ptr, magazine, target, remoteInfo, NULL, false);
  }
 
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  const MagazineType *mType = magazine ? magazine->Type() : NULL;
  const WeaponModeType *mode = ptr.GetWeaponModeType();
  bool continuous = muzzle ? muzzle->_soundContinuous : false;
  bool noSound = false;
  /**/
  float soundDuration = muzzle ? muzzle->_soundDuration : 0;
  if (mType && mode)
  {
    soundDuration = mode->_soundDuration;
    continuous = mode->_soundContinuous;
    // sound only on first shot in burst
    if (magazine->_burstLeft>0 && mode->_soundBurst) noSound = true;
  }
  /**/

  // play sound
  if (!noSound)
  {
    // select corresponding weapon sounds
    WeaponSounds &wSounds = ptr.GetMuzzle()._sound;
    // play shot sound
    wSounds.ShotSound(this, context, ptr, magazine);
  }

  const VisualState &vs = RenderVisualState();
  const AmmoType *type = mType ? mType->_ammo : NULL;
  if (type)
  {
    _shootVisible = type->visibleFire;
    _shootAudible = type->audibleFire;
    _shootTimeRest = type->visibleFireTime;
#if _VBS3
    _shootTimeRestAudible = type->audibleFireTime;
#endif
    _shootTarget = target;
    // perform cartridge effects
    #if 1
    if (type->_cartridgeType && EnableVisualEffects(SimulateVisibleNear,100))
    {
      Vector3 viewerPos=GScene->GetCamera()->Position();

      SimulationImportance importance = CalculateImportance(&viewerPos,1);
      if (importance<=SimulateVisibleNear)
      {
        // check current muzzle cartridge position / velocity
        // create a transformation from weapon direction
        Matrix4 pos;
        Vector3 vel;
        if (GetWeaponCartridgePos(context, ptr, pos, vel))
        {
          pos = vs.Transform() * pos;
          vel = vs.Transform().Rotate(vel);
          pos.Orthogonalize();

          Entity *fx = CreateThing(type->_cartridgeType,pos,vel,true);
          if (fx)
          {
            // any additional processing
          }
        }
      }
    }
    #endif
  }

  // check which recoil table should be used
  RStringB recoilName = GetRecoilName(mode);
  Ref<RecoilFunction> recoil = RecoilFunctions.New(recoilName);
  StartRecoil(recoil,GetRecoilFactor());

  if (IsEventHandler(EEFired) 
#if _VBS3 //MuzzleEvent
    || slot._muzzle->IsEventHandler(MEFired)
#endif
    )
  {
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType &arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(RString(ptr.GetWeaponType()->GetName()));
    arguments.Add(RString(ptr.GetMuzzleType()->GetName()));
    arguments.Add(mode ? RString(mode->GetName()) : RString());
    arguments.Add(type ? RString(type->GetName()) : RString());
    arguments.Add(ptr.GetMagazine() ? RString(ptr.GetMagazine()->Type()->GetName()) : RString());
    // Add the object that was spawned

    // here we handle remote objects passed to us from
    // the NetworkClient
    Object *missle = context._weapons->_lastShot;
#if _VBS3
    if(!missle && remoteInfo)
      missle = remoteInfo->_projectile;
#endif

    // add missle to the end of the event
    arguments.Add(GameValueExt(missle));
    
    if(IsEventHandler(EEFired))
      OnEvent(EEFired,value);
#if _VBS3//MuzzleEvent
    if(slot._muzzle->IsEventHandler(MEFired))
      slot._muzzle->OnEvent(MEFired,value);
#endif
  }

  // process all nearby entities
  int xMin,xMax,zMin,zMax;
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,vs.Position(),50); // TODO: 50 - make distance adjustable?
  for (int z=zMin; z<=zMax; z++) for (int x=xMin; x<=xMax; x++)
  {
    const ObjectList &list = GLandscape->GetObjects(z,x);
    for (int i=0,n=list.Size(); i<n; i++)
    {
      Object *obj = list[i];
      if (!obj) continue;
      EntityAI *ai = dyn_cast<EntityAI>(obj);
      if (!ai) continue;
      if (ai->IsEventHandler(EEFiredNear))
      {
        GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
        GameArrayType &arguments = value;
        arguments.Add(GameValueExt(ai));
        arguments.Add(GameValueExt(this));
        arguments.Add(vs.Position().Distance(obj->RenderVisualState().Position()));
        arguments.Add(RString(ptr.GetWeaponType()->GetName()));
        arguments.Add(RString(ptr.GetMuzzleType()->GetName()));
        arguments.Add(RString(mode ? RString(mode->GetName()) : RString()));
        arguments.Add(RString(type ? RString(type->GetName()) : RString()));
        ai->OnEvent(EEFiredNear,value);
      }
    }
  }

  // camera shake effect
  GWorld->GetCameraShakeManager().AddSource(CameraShakeParams(CameraShakeSource::CSWeaponFire, vs.Position(), type));

  return true;
}

/**
Preloaded content currently includes:
  sound
  cartridge model
  muzzle flash animated texture
  
More can be added:
  bullet model, particle textures, cartridge model.
  
Does not need preloading:
    cartridge type (stored in the weapon)
*/
bool EntityAIFull::PreloadFireWeaponEffects(const WeaponsState &weapons, const WeaponModePtr &ptr)
{
  // if the unit is far, do not preload anything for it
  if (!EnableVisualEffects(SimulateVisibleNear,200)) return true;
  if (!ptr.IsValid()) return true;
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine) return true;

  //const MuzzleType *muzzle = slot._muzzle;
  //const SoundPars *sound = &muzzle->_sound;
  const MagazineType *mType = magazine ? magazine->Type() : NULL;
  const WeaponModeType *mode = ptr.GetWeaponModeType();

  bool ret = true;

  if (mode)
  {
    // preload shot samples
    if (!mode->_beginSound.PreloadSounds())
      ret = false;
  }

  if (!PreloadMuzzleFlash(weapons, ptr))
    ret = false;
  
  const AmmoType *type = mType ? mType->_ammo : NULL;
  if (type)
  {
    // perform cartridge effects
#if 1
    if (type->_cartridgeType && EnableVisualEffects(SimulateVisibleNear,100))
    {
      Vector3 viewerPos=GScene->GetCamera()->Position();

      SimulationImportance importance = CalculateImportance(&viewerPos,1);
      if (importance<=SimulateVisibleNear)
      {
        // we can preload model for type->_cartridgeType now
        if (!type->_cartridgeType->PreloadShape(FileRequestPriority(200)))
          ret = false;
      }
    }
#endif
  }
  return ret;
}

Vector3 EntityAIFull::GetWeaponManualControlDirection(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const
{
  return GetWeaponDirection(vs,context,ptr);
}

Vector3 EntityAIFull::GetWeaponManualControlPoint(ObjectVisualState const& vs, const TurretContext &context, const WeaponModePtr &ptr) const
{
  return GetWeaponPoint(vs,context,ptr);
}

void EntityAIFull::ManualControlMissile( TurretContext &context )
{
  Missile *missile = dyn_cast<Missile,Vehicle>(context._weapons->_lastShot);
  if (missile)
  {
    // check cursor position
    WeaponModePtr selectedWeapon = context._weapons->ValidatedCurrentWeapon();
    Vector3 dir = FutureVisualState().Position()+GetWeaponDirection(FutureVisualState(), context, selectedWeapon)*1000;
    missile->SetControlDirection(dir);

    //// check cursor position
    //int selectedWeapon = context._weapons->ValidatedCurrentWeapon();
    //const VisualState &vs = FutureVisualState();
    //float dist = missile->FutureVisualState().Position().Distance(vs.Position());
    //const AmmoType *aType = missile->Type();
    //float missileSpeed = aType->maxSpeed;
    //// once past default distance, use a sliding distance
    //float aimDist = floatMax(1000.0f,dist+missileSpeed);
    //Vector3 dir = vs.Position()+GetWeaponManualControlDirection(vs, context, selectedWeapon)*aimDist;
    //missile->SetControlDirection(dir);
  }
}


bool EntityAIFull::FireWeapon(const TurretContextV &context, const WeaponModePtr &ptr, TargetType *target, bool forceLock)
{
  Fail("Entity cannot fire unless it defines FireWeapon");
  // by default entity cannot does not fire
  RemoteFireWeaponInfo localInfo;
  localInfo._direction = VForward;
  return PostFireWeapon(context, ptr, target, localInfo);
}

int EntityAIFull::RemoveAmmoAfterShot(const WeaponModeType *mode, Magazine *magazine)
{
  int burst = mode->_mult;
  saturateMin(burst, magazine->GetAmmo());
#if _ENABLE_CHEATS
  static bool testAmmoCheat = false;
#else
  const bool testAmmoCheat = false;
#endif
  if (!testAmmoCheat)
  {
    ADD_ENCRYPTED(magazine->_ammo,magazine->_ammoSupport, -burst);
  }
  return burst;
}

const AmmoType *EntityAIFull::ValidateFireWeaponWithAmmo(const WeaponModePtr &ptr, const Magazine *magazine)
{
  if (!ptr.IsValid()) return NULL;

  if (!magazine || ptr.GetMagazine() != magazine) return NULL;

  return ptr.GetAmmoType();
}


#if _VBS3
bool EntityAIFull::PostFireWeapon(const TurretContext &context, const WeaponModePtr &ptr, TargetType *target, RemoteFireWeaponInfo &remoteInfo)
#else
bool EntityAIFull::PostFireWeapon(const TurretContext &context, const WeaponModePtr &ptr, TargetType *target, const RemoteFireWeaponInfo &remoteInfo)
#endif
{
  if (!ptr.IsValid())
    return false; // nothing to fire
  
  context._weapons->_lastShotTime = Glob.time;

  Magazine *magazine = unconst_cast(ptr.GetMagazine());

  FireWeaponEffects(context,ptr,magazine,target,NULL);

  if (context._turret && context._turretType && context._turretType->_primaryGunner)
  {
    static const float HeatUpFactor = 1.0f / 30.0f;
    context._turret->_heatFactor += HeatUpFactor;
    saturate(context._turret->_heatFactor, 0.0f, 1.0f);
  }

#if _VBS3
  Entity *proj = context._weapons->_lastShot;
  remoteInfo._projectile = proj;

  // Every shot other than shot bullet spawns a network object
  // for bullets we just assign null, because they're spawned localy
  // and mostly harmless
  ShotBullet *bullet = dyn_cast<ShotBullet>(proj);
  if(bullet)
    remoteInfo._projectile = NULL;   
  else
    remoteInfo._visible = FLT_MAX;  
#endif

  GetNetworkManager().FireWeapon(this, context._gunner, ptr, magazine, target, remoteInfo);

  if (!magazine)
    return false;

  const WeaponModeType *mode = ptr.GetWeaponModeType();

  if (!mode)
    return false;

  // only last bullet in burst should be reloaded based on ability

  if (magazine->_burstLeft<=0)
  {
    magazine->_burstLeft = mode->_burst;
    if (mode->_mult>0) saturateMin(magazine->_burstLeft, magazine->GetAmmo()/mode->_mult);
  }

  if (!mode->_autoFire && (magazine->_burstLeft==1 || mode->_burst == 0))
  {
    AIBrain *unit = EffectiveGunnerUnit(context);
    if (!unit) unit = CommanderUnit();
    float invAbility = unit ? unit->GetInvAbility(AKReloadSpeed) : 1/DeadAbility;
    magazine->_reloadDuration = invAbility * GRandGen.PlusMinus(1, 0.1);
  }
  else
    magazine->_reloadDuration = 1.0;
  magazine->_reload = 1.0;

  magazine->_burstLeft--; // count shots in burst

  int burst = RemoveAmmoAfterShot(mode, magazine);
  // TODO: ?? auto reload magazine here?
  if (context._turret)
  {
    if (!context._turret->IsLocal())
      GetNetworkManager().AskForAmmo(this, context._turret, ptr, burst);
  }
  else if (!IsLocal())
    GetNetworkManager().AskForAmmo(this, context._turret, ptr, burst);

  // remove magazine
  if (magazine->GetAmmo() <= 0)
  {
    magazine->_reload = 0; // for better weapon animation
    // following code was never hit during normal game-play (active weapon is always fired) and caused issue http://dev-heaven.net/issues/24046
//     if (weapon != context._weapons->ValidatedCurrentWeapon() && weapon != context._weapons->ValidatedCurrentCounterMeasures() )
//       context._weapons->RemoveMagazine(this, magazine);
    // try to start auto-reloading
    AutoReload(*context._weapons, ptr, _reloadEnabled, false);
  }
  return true;
}

RefArray<Object> MapDiags;

#if _RELEASE
  #define DIAG_VEHICLE 0
#else
  #define DIAG_VEHICLE 0
#endif

static const EnumName SimulationImportanceNames[]=
{
  EnumName(SimulateVisibleNear,"Near"),
  EnumName(SimulateVisibleFar,"Far"),
  EnumName(SimulateInvisibleNear,"InvisNear"),
  EnumName(SimulateInvisibleFar,"InvisFar"),
  EnumName(SimulateDefault,"Default"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(SimulationImportance dummy) {return SimulationImportanceNames;}


RString EntityAI::RemoteDiagText() const
{
  BString<512> buf;
#if _ENABLE_CHEATS
  if (!IsLocal())
  {
    static const char *nmcName[]=
    {
      "gen", //NMCUpdateGeneric
      "pos", //NMCUpdatePosition,
      "dam", //NMCUpdateDamage,
    };


    bool once = false;
    for (int i=0; i<lenof(_lastUpdateTime); i++)
    {
      BString<32> time; 
      if (_lastUpdateTime[i]==TIME_MIN)
      {
        strcpy(time,"No");
      }
      else if (Glob.time-_lastUpdateTime[i]>0.100)
      {
        sprintf(time,"%.3f",Glob.time-_lastUpdateTime[i]);
      }
      if (time[0])
      {
        if (!once++) strcat(buf," last");
        BString<64> add; 
        sprintf(add," %s: %s",nmcName[i],cc_cast(time));
        strcat(buf,add);
      }
    }
    if (CheckPredictionFrozen())
      strcat(buf," Frozen");
    strcat(buf, GetNetworkManager().GetDiagText(this));
  }
  else
  {
    strcat(buf, GetNetworkManager().GetDiagText(this));
  }
#endif
  return buf.cstr();
}


RString EntityAI::DiagText() const
{
  AIBrain *unit = PilotUnit();
  BString<4096> buf;
  strcpy(buf,"");
  if (unit && IsLocal())
  {
    float pSpeed=0;
    const Path &path=unit->GetPath();
    if (path.Size()>=2)
    {
      float cost=path.CostAtPos(FutureVisualState().Position());
      AILockerSavedState state;
      PerformUnlock(&state);
      pSpeed=path.SpeedAtCost(cost,unit->IsFreeSoldier());
      PerformRelock(state);
    }
    sprintf(buf, "Spd %.1f (path %d: v:%.1f, vMax:%.1f: t:%.2f)",
      FutureVisualState().ModelSpeed().Z()*3.6,path.Size(),pSpeed*3.6,_limitSpeed*3.6,
      Glob.time-path.GetSearchTime());
  }
  if (GetStopped())
    strcat(buf," STOP");
  strcat(buf," ");
  strcat(buf,FindEnumName(_prec.GetEnumValue()));

  if (!IsLocal())
  {
    strcat(buf,RemoteDiagText());
  }

  const Shape *hitShape=_shape->HitpointsLevel();
  if( hitShape )
  {
    //bool first=true;
    // report damages
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEDammage))
    {
      char hits[512];
      hits[0]=0;
      if (GetRawTotalDamage()>0)
        sprintf(hits+strlen(hits)," Tot:%.2f",GetRawTotalDamage());
      const HitPointList &hitpoints=GetType()->GetHitPoints();
      for( int i=0; i<hitpoints.Size(); i++ )
      {
        const HitPoint &hit = hitpoints[i];
        int index=hit.GetSelection();
        if( index<0 ) continue;
        const NamedSelection &sel=hitShape->NamedSel(index);
        float hitVal=_hit[i];
        if( hitVal>0.3 )
          sprintf(hits+strlen(hits)," %s:%.2f",sel.Name(),hitVal);
      }
      strcat(buf,hits);
    }
#endif
  }
  return (const char *)buf;
}

/*!
Check what axis is controlled by what peripheral.
Principal axes are:
  mouse cursor
  thrust

\patch_internal 1.11 Date 7/27/2001 by Ondra.
Different control device detection method
(decentralized, vehicle simulation is responsible for decision).
*/

inline PackedColor CostToColor(float cost)
{
  static const float int1 = 1.0;
  static const float invint1 = (1.0 / int1);
  static const float int2 = 4.0;
  static const float invint2 = (1.0 / int2);
  static const float int12 = int1 + int2;

  static const float alpha = 0.5;
  static const Color colorR(1, 0.5f, 0, alpha);
  static const Color colorG(0, 1, 0, alpha);
  static const Color colorY(1, 1, 0, alpha);

  saturateMin(cost, int12);
  if (cost <= int1)
    return PackedColor(colorY * (cost * invint1)  + colorG * (1 - cost * invint1));
  cost -= int1;
  return PackedColor(colorR * (cost * invint2) + colorY * (1 - cost * invint2));
}


Matrix4 EntityAI::AnimateProxyMatrix(ObjectVisualState const& vs, int level, Matrix4 proxyTransform, SkeletonIndex boneIndex) const
{
  AnimateBoneMatrix(proxyTransform,vs,level,boneIndex);
  return proxyTransform;
}

int EntityAI::GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const
{
  // TODO: calculate flag
  // consider all proxies with no type or always show ones

  int nFaces = 0;

  // calculate default proxies (included in shape)
  ShapeUsed lock = _shape->Level(level);
  int n = _type->GetProxyCountForLevel(level);
  for( int i=0; i<n; i++ )
  {
    const ProxyObjectTyped &proxy=_type->GetProxyForLevel(level,i);
    
    const EntityType *type = proxy.obj->GetEntityType();
    if (type && strcmp(type->_simName, "alwaysshow")!= 0)
      // ignore typed proxies - they are handled in derived classes
      continue;

    // smart clipping par of obj->Draw
    Matrix4Val pTransform=pos*proxy.obj->RenderVisualState().Transform();

    // LOD detection
    LODShape *pshape = proxy.obj->GetShape();
    if (!pshape)
      continue;
    int level = GScene->LevelFromDistance2(pshape,dist2,pTransform.Scale());
    if (level==LOD_INVISIBLE)
      continue;
    nFaces += proxy.obj->GetComplexity(level,pTransform);
  }
  return nFaces;
}

bool EntityAI::PreloadProxies(int level, float dist2, bool preloadTextures) const
{
  bool ret = true;
  // draw flag
  ShapeUsed lock = _shape->Level(level);
  for (int i=0; i<GetType()->GetProxyCountForLevel(level); i++)
  {
    const ProxyObjectTyped &proxy = GetType()->GetProxyForLevel(level,i);
    Object *obj = proxy.obj;
    const EntityType *type = obj->GetEntityType();
    LODShape *pshape = NULL;
    if (!type || strcmp(type->_simName, "alwaysshow") == 0)
      pshape = obj->GetShape();
    else if (_flag && _showFlag && !stricmp(type->_simName, "flag"))
      pshape = obj->GetShape();
    else if (!stricmp(type->_simName, "windsock"))
      pshape = obj->GetShape();
    if (!pshape)
      continue;
    int pLevel = GScene->PreloadLevelFromDistance2(pshape, dist2, FutureVisualState().Scale()*obj->FutureVisualState().Scale());     
    if (pLevel == LOD_INVISIBLE)
      continue;
    if (pLevel<0)
    {
      ret = false;
      continue;
    }
    // preload textures
    ShapeUsed shape = pshape->Level(pLevel);
    if (!shape->PreloadTextures(dist2, NULL))
      ret = false;
  }
  return ret;
}

void EntityAI::AdjustFlagTransform(Matrix4 &pTransform)
{
  float forceVertical = ForceVerticalFlag();
  if (forceVertical>0)
  {
    if (forceVertical>=1)
      // we want to force vertical, b-center should be on the ground; interpolate between normal and vertical
      pTransform.SetOrientation(M3Identity);
    else
    {
      Matrix3 orient = M3Identity*forceVertical+pTransform.Orientation()*(1-forceVertical);
      orient.Orthogonalize();
      pTransform.SetOrientation(orient);
    }
  }
}

Matrix4 EntityAI::GetProxyTransform(int level, int i, Matrix4Par proxyPos, Matrix4Val parent) const
{
  SkeletonIndex boneIndex = _type->GetProxyBoneForLevel(level, i);
  Matrix4Val proxyTransform = AnimateProxyMatrix(RenderVisualState(), level, proxyPos, boneIndex);
  return parent * proxyTransform;
}

void EntityAI::PrepareProxiesForDrawing(Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so)
{
  // prepare the flag proxy (when need to draw)
  if (_flag && _showFlag)
  {
    float dist2 = so->distance2;
    const EntityType *entType = Type();
    for (int i=0; i<entType->GetProxyCountForLevel(level); i++)
    {
      const ProxyObjectTyped &proxy = entType->GetProxyForLevel(level, i);
      Object *obj = proxy.obj;
      const EntityType *type = obj->GetEntityType();
      if (type && stricmp(type->_simName, "flag") == 0)
      {
        // flag proxy found
        LODShape *lodShape = obj->GetShape();

        SkeletonIndex boneIndex = entType->GetProxyBoneForLevel(level, i);
        Matrix4 proxyTransform = AnimateProxyMatrix(RenderVisualState(), level, proxy.GetTransform(), boneIndex);

        // TODO: smart clipping
        Matrix4 pTransform = transform * proxyTransform;

        AdjustFlagTransform(pTransform);
        int pLevel = GScene->LevelFromDistance2(lodShape, dist2, pTransform.Scale());
        if (pLevel != LOD_INVISIBLE)
        {
          Texture *tex = GetFlagTexture();
          if (tex)
          {
            _flag->SetFlagTexture(tex); // flag texture can be set here (_flag is the only instance for this proxy)
            _flag->SetTransform(pTransform); // store the matrix, it is needed from Flag::Animate
            // bounding box of the flag proxy does not cover the flag well, so avoid clipping at all
            GScene->ProxyForDrawing(_flag, rootObj, pLevel, rootLevel, 0, dist2, pTransform);
          }
        }
        // no more flag proxies supported
        break;
      }
    }
  }
  base::PrepareProxiesForDrawing(rootObj, rootLevel, animContext, transform, level, clipFlags, so);
}

Vector3 EntityAI::ExternalCameraPosition(CameraType camType) const
{
  return (camType == CamGroup) ? GetType()->_groupCameraPosition : GetType()->_extCameraPosition;
}

bool EntityAI::PreloadView(Person *person, CameraType camType) const
{
  bool ret = true;
  LODShape *shape = GetShape();
  int level = -1;
  // if loading a different shape, it makes no sense to preload entity proxies
  bool proxies = true;
  switch (camType)
  {
    case CamInternal:
      level = InsideLOD(camType);
      break;
    case CamExternal:
      if (shape->NLevels()>0) level = 0;
      break;
    case CamGunner:
      {
        LODShape *optics = GetOpticsModel(person);
        if (optics && optics->NLevels()>0)
        {
          shape = optics;
          level = 0;
          proxies = false;
        }
      }
      break;
  }
  if (level>=0 && shape && level<shape->NLevels())
  {
    if (!shape->CheckLevelLoaded(level,true))
      ret = false;
    else
    {
      ShapeUsed lock = shape->Level(level);
      if (!lock->PreloadTextures(0, NULL))
        ret = false;
      if (proxies && !PreloadProxies(level,0))
        ret = false;
    }
  }
  return ret;
}

bool EntityAIFull::ShowAim(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  if (camType==CamGunner)
    return false;
  bool autoAim = true;

  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    autoAim = muzzle->_canAutoAim;

    if (camType == CamInternal && !muzzle->_showAimCursorInternal)
      return false;
  }
  AIBrain *unit = person->Brain();
  if (unit==GunnerUnit() && autoAim && // FIX: was test to EffectiveGunnerUnit()
    Glob.config.IsEnabled(DTAutoAim))
    return false; // no aiming point drawn
  if (CommanderUnit()!=unit && GunnerUnit()!=unit && EffectiveGunnerUnit(context)!=unit)
    return false;
  return base::ShowAim(context, ptr, camType, person);
}

float EntityAIFull::GetCursorSize(Person *person) const
{
  // check actual weapon
  TurretContext context;
  if (!FindTurret(person, context))
    return 1.0f;
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid())
    // return no weapon cursor
    return 1.0f;
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  return muzzle->GetCursorSize();
}

const CursorTextureInfo *EntityAIFull::GetCursorTexture(Person *person)
{
  // check actual weapon
  TurretContext context;
  if (!FindTurret(person, context))
    return NULL;
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid())
    // return no weapon cursor
    return NULL;
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  return muzzle->GetCursorTexture();
}

const CursorTextureInfo *EntityAIFull::GetCursorAimTexture(Person *person)
{
  // check actual weapon
  TurretContext context;
  if (!FindTurret(person, context))
    return NULL;
  WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
  if (!ptr.IsValid())
    // return no weapon cursor
    return NULL;
  const MuzzleType *muzzle = ptr.GetMuzzleType();
  return muzzle->GetCursorAimTexture();
}

bool EntityAIFull::GetCursorLocked(Person *person) const
{
  Fail("Not used");
  return false;
/*
  if (person->Brain()!=EffectiveGunnerUnit()) return false;
  return _autoAimLocked.NotNull();
*/
}

void EntityAI::LimitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  //if( camType==CamInternal ) fov=GetType()->_fov;
  if (camType==CamInternal)
    GetType()->_viewPilot.LimitVirtual(camType,heading,dive,fov);
  else
    base::LimitVirtual(camType,heading,dive,fov);
}

void EntityAI::InitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  if( camType==CamInternal )
    GetType()->_viewPilot.InitVirtual(camType,heading,dive,fov);
  else
    base::InitVirtual(camType,heading,dive,fov);
}


class AmmoCostFunc : public ITurretFunc
{
protected:
  float &_curAmmo; // out

public:
  AmmoCostFunc(float &curAmmo) : _curAmmo(curAmmo) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      if (magazine->GetAmmo() == 0) return false; // continue
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      const AmmoType *ammo = type->_ammo;
      if (!ammo) return false; // continue
      _curAmmo += magazine->GetAmmo() * ammo->cost;
      return false; // continue;
    });
    return false; // for all turrets
  }
};

float EntityAIFull::GetAmmoCost() const
{
  // sum-up ammo state
  float curAmmo = 0;
  AmmoCostFunc func(curAmmo);
  ForEachTurret(func);
  return curAmmo;
}

/// functor - sum all ammo values
class AmmoHitFunc : public ITurretFunc
{
protected:
  float &_curAmmo; // out

public:
  AmmoHitFunc(float &curAmmo) : _curAmmo(curAmmo) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      if (magazine->GetAmmo() == 0) return false; // continue
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      const AmmoType *ammo = type->_ammo;
      if (!ammo) return false; // continue
      _curAmmo += magazine->GetAmmo() * ammo->hit;
      return false; // continue
    });
    return false; // for all turrets
  }
};

float EntityAIFull::GetAmmoHit() const
{
  // sum-up ammo state
  float curAmmo = 0;
  AmmoHitFunc func(curAmmo);
  ForEachTurret(func);
  return curAmmo;
}

/// functor - sum all ammo values
class CheckAmmoHitFunc : public ITurretFunc
{
protected:
  float _curAmmo;
  float _limit;

public:
  CheckAmmoHitFunc(float limit) : _curAmmo(0),_limit(limit) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    return context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      if (magazine->GetAmmo() == 0) return false; // continue
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      const AmmoType *ammo = type->_ammo;
      if (!ammo) return false; // continue
      _curAmmo += magazine->GetAmmo() * ammo->hit;
      // once we are over, no need to continue
      return _curAmmo >= _limit; // done or continue
    });
  }
};

/// check if there are enough ammo resources
bool EntityAIFull::CheckAmmoHitOver(float hit) const
{
  // sum-up ammo state
  CheckAmmoHitFunc func(hit);
  return ForEachTurret(func);
}

/*!
\patch 5139 Date 3/13/2007 by Jirka
- Fixed: Vehicles did not rearm when no ammo was remaining
*/
class MaxAmmoCostFunc : public ITurretFunc
{
protected:
  float &_curAmmo; // out

public:
  MaxAmmoCostFunc(float &curAmmo) : _curAmmo(curAmmo) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      if (type->_maxAmmo == 0) return false; // continue
      const AmmoType *ammo = type->_ammo;
      if (!ammo) return false; // continue
      _curAmmo += type->_maxAmmo * ammo->cost;
      return false; // continue
    });
    return false; // for all turrets
  }
};

float EntityAIFull::GetMaxAmmoCost() const
{
  // sum-up ammo state
  float curAmmo = 0;
  MaxAmmoCostFunc func(curAmmo);
  ForEachTurret(func);
  return curAmmo;
}

class MaxAmmoHitFunc : public ITurretFunc
{
protected:
  float &_curAmmo; // out

public:
  MaxAmmoHitFunc(float &curAmmo) : _curAmmo(curAmmo) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      if (type->_maxAmmo == 0) return false; // continue
      const AmmoType *ammo = type->_ammo;
      if (!ammo) return false; // continue

      _curAmmo += type->_maxAmmo * ammo->hit;
      return false; // continue
    });
    return false; // for all turrets
  }
};

float EntityAIFull::GetMaxAmmoHit() const
{
  // sum-up ammo state
  float curAmmo = 0;
  MaxAmmoHitFunc func(curAmmo);
  ForEachTurret(func);
  return curAmmo;
}

class RearmFunc : public ITurretFunc
{
protected:
  float &_resources; // out

public:
  RearmFunc(float &resources) : _resources(resources) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      if (type->_maxAmmo == 0) return false; // continue
      const AmmoType *ammo = type->_ammo;
      if (!ammo) return false; // continue
      float cost = ammo->cost;
      int need = type->_maxAmmo - magazine->GetAmmo();
      if (need * cost > _resources)
        need = toIntFloor(_resources / cost);
      if (need == 0) return false; // continue

      ADD_ENCRYPTED(magazine->_ammo,magazine->_ammoSupport, need);
      _resources -= need * cost;
      // ?? TODO: ?? change _reload
      return false; // continue;
    });
    return false; // for all turrets
  }
};

float EntityAIFull::Rearm( float resources )
{
  resources += _rearmCredit;

  RearmFunc func(resources);
  ForEachTurret(func);

  _rearmCredit = resources; // accumulate what's rest
  return resources;
}


class NeedRearmFunc : public ITurretFunc
{
protected:
  float &_minWeapon; // out

public:
  NeedRearmFunc(float &minWeapon) : _minWeapon(minWeapon) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    context._weapons->ForEachMagazine([this](Magazine *magazine) -> bool
    {
      const MagazineType *type = magazine->Type();
      if (!type) return false; // continue
      if (type->_maxAmmo == 0) return false; // continue
      float ratio = (float)magazine->GetAmmo() / type->_maxAmmo;
      saturateMin(_minWeapon, ratio * 1.5);
      return false; // continue
    });
    return false; // for all turrets
  }
};

float EntityAIFull::NeedsRearm() const
{
  if (IsDamageDestroyed())
    return 0;

  // sum-up ammo state
  float minWeapon = 1.0;
  NeedRearmFunc func(minWeapon);
  ForEachTurret(func);
  saturate(minWeapon, 0, 1);

  float max = GetMaxAmmoHit();
  if (max <= 0)
    return 0;
  float act = GetAmmoHit();

  return 1.0 - floatMin(act / max, minWeapon);
}

float EntityAIFull::GetReloadNeeded() const
{
  float value = 0;
  TurretContext context;
  if (GetPrimaryGunnerTurret(context))
  {
    context._weapons->ForEachMuzzle([&value, &context](const WeaponModePtr &ptr) -> bool
    {
      const MuzzleType *muzzle = ptr.GetMuzzleType();
      const Magazine *magazine = ptr.GetMagazine();
      const MagazineType *type = magazine ? magazine->Type() : NULL;
      Magazine *newMagazine = context._weapons->FindMagazineByType(muzzle, type);
      if (newMagazine)
      {
        if (magazine)
        {
          if (newMagazine->Type() == type && type->_maxAmmo > 0 && newMagazine->GetAmmo() > magazine->GetAmmo())
            saturateMax(value, 1 - magazine->GetAmmo() / (float)type->_maxAmmo);
        }
        else
        {
          value = 1.0f; 
          return true; // max. value can be returned immediately
        }
      }
      return false; // continue
    });
  }
  return value;
}

void EntityAIFull::Reload()
{
  TurretContext context;
  if (GetPrimaryGunnerTurret(context))
  {
    context._weapons->ForEachMuzzle([this, &context](const WeaponModePtr &ptr) -> bool
    {
      const MuzzleType *muzzle = ptr.GetMuzzleType();
      const Magazine *magazine = ptr.GetMagazine();
      const MagazineType *type = magazine ? magazine->Type() : NULL;
      Magazine *newMagazine = context._weapons->FindMagazineByType(muzzle, type);
      if (newMagazine)
      {
        if (magazine)
        {
          if (newMagazine->Type() == type && type->_maxAmmo > 0 && newMagazine->GetAmmo() > magazine->GetAmmo())
            ReloadMagazine(*context._weapons, ptr, newMagazine);
        }
        else
          ReloadMagazine(*context._weapons, ptr, newMagazine);
      }
      return false; // continue
    });
  }
}

float EntityAI::NeedsAmbulance() const
{
  return CommanderUnit() ? CommanderUnit()->GetPerson()->NeedsAmbulance() : 0.0f;
}

float EntityAI::NeedsRepair() const
{
  return IsDamageDestroyed() ? 1.0f : GetTotalDamage();
}

float EntityAI::NeedsRefuel() const
{
  if (IsDamageDestroyed())
    return 0.0f;
  float max = GetType()->GetFuelCapacity();
  if (max <= 0.0f)
    return 0.0f;
  float act = GetFuel();
  return 1.0f - act / max;
}

bool EntityAI::IsAbleToStand() const
{
  return CommanderUnit() && CommanderUnit()->GetPerson()->IsAbleToStand();
}


// accuracy simulation

const EntityAIType *EntityAI::GetTypeAtLeast(float accuracy) const
{
  // start with certain information
  const EntityType *type = GetType();
  while (type->_parentType.NotNull() && type->_parentType->_accuracy>=accuracy)
    type = type->_parentType;
  const EntityAIType *vType = dynamic_cast<const EntityAIType *>(type);
  if (!vType)
    Fail("Non-ai type");
  return vType;
}

const EntityAIType *EntityAI::GetType(float accuracy) const
{
  // start with certain information
  const EntityType *type = GetType();
  while (type->_parentType.NotNull() && type->_accuracy>=accuracy)
    type = type->_parentType;
  Assert(dynamic_cast<const EntityAIType *>(type));
  const EntityAIType *vType = static_cast<const EntityAIType *>(type);
  return vType;
}

TargetSide EntityAI::GetTargetSideAsIfAlive() const
{
  AIBrain *unit = CommanderUnit();
  if (unit)
  {
    Person *person = unit->GetPerson();
    // for vehicles, redirect to the commander
    if (person && person != this)
      return person->GetTargetSideAsIfAlive();
    // for person, check special cases
    if (unit->GetCaptive())
      return TCivilian;
    if (person && person->IsRenegade())
      // he is crazy - shooting at friendlies - enemy to all sides
      return TEnemy;
  }
  return base::GetTargetSide();
}


TargetSide EntityAI::GetTargetSide() const
{
  return _isDead ? TCivilian : GetTargetSideAsIfAlive();
}

bool EntityAI::IsRenegade() const
{
#if _VBS3_DISABLE_RENEGADE
  return false;
#endif
  AIBrain *unit = CommanderUnit();
  return unit && unit->GetPerson()->GetExperience() < ExperienceRenegadeLimit;
}


/*
TargetSide EntityAI::GetTargetSide() const
{
  if ( !CommanderUnit() && GetTotalDamage()<1 && !_fake ) return TCivilian; // empty - report neutral
  return base::GetTargetSide();
}
*/

TargetSide EntityAI::GetTargetSide(float accuracy) const
{
#if _VBS3_DEADENEMYDISABLESVEHICLEFIX
  if (_isDead) return TCivilian;
#endif
  AIBrain *unit = CommanderUnit();
  if (accuracy < Target::GetSideAccuracyExact())
  {
    const EntityAIType *type = GetType(accuracy);
    TargetSide side = type->_typicalSide;
    if (side != TSideUnknown && unit && unit->GetCaptive())
      return TCivilian;
    return side;
  }
  return GetTargetSideAsIfAlive();
}

RString EntityAI::GetDebugName() const
{
  RString id = Format("%d:%"OID_FORMAT, GetNetworkId().creator.CreatorInt(), GetNetworkId().id);
  if (_varName.GetLength()>0)
  {
    return _varName;
  }
  const EntityAIType *type = GetType();
  if (type)
  {
    return Format("%s:%s",cc_cast(type->GetName()),cc_cast(id));
  }

  return Format("#%p",this);
}

void EntityAI::ResetStatus()
{
  if( GetType()->IsSupply() )
  {
    if (_supply) _supply->Destroy(this);
    _supply=new ResourceSupply(this);
  }

  _isDead = false;

  _whenDestroyed = TIME_MAX;
  _sensorColID=SensorColID(-1);
  for (int i=0; i<NEntityEvent; i++)
    _eventHandlers[i].Clear();

  _userActions.Clear();

  base::ResetStatus();
}

void EntityAIFull::ResetStatus()
{
  _visTracker.Clear();
  base::ResetStatus();
}

AIBrain *EntityAI::EffectiveGunnerUnit(const TurretContext &context) const
{
  return context._gunner ? context._gunner->Brain() : NULL;
}

static const EnumName FireStateNames[]=
{
  EnumName(EntityAI::FireInit,"INIT"),
  EnumName(EntityAI::FireAim,"AIM"),
  EnumName(EntityAI::FireAimed,"AIMED"),
  EnumName(EntityAI::FireDone,"DONE"),
  EnumName()
};

template<>
const EnumName *GetEnumNames( EntityAI::FireState dummy ) {return FireStateNames;}

static const EnumName TargetStateNames[]=
{
  EnumName(TargetDestroyed,"DESTROYED"),
  EnumName(TargetAlive,"ALIVE"),
  EnumName(TargetEnemyEmpty,"ENEMYEMPTY"),
  EnumName(TargetEnemy,"ENEMY"),
  EnumName(TargetEnemyCombat,"COMBAT"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(TargetState dummy)
{
  return TargetStateNames;
}

LSError UserActionDescription::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("id", id, 1))
  CHECK(ar.Serialize("text", text, 1, ""))
  CHECK(ar.Serialize("script", script, 1, ""))
  void *old=ar.GetParams();
  ar.SetParams(&GGameState);
  CHECK(ar.Serialize("param", param, 1))
  CHECK(ar.Serialize("priority", priority, 1, 0))
  CHECK(ar.Serialize("showWindow", showWindow, 1, true))
  CHECK(ar.Serialize("hideOnUse", hideOnUse, 1, false))
  CHECK(ar.Serialize("shortcut", (int &)shortcut, 1, -1))
  CHECK(ar.Serialize("condition", condition, 1, RString()))

#if _HELISIM_PRECISE_INTERACTION
  CHECK(ar.Serialize("modelPositions", modelPositions, 1, ""))
  CHECK(ar.Serialize("radius",radius, 1, 0))
  CHECK(ar.Serialize("radiusView",radiusView, 1, 0))
  CHECK(ar.Serialize("showIn3D",showIn3D, 1, 0))
  CHECK(ar.Serialize("available",available, 1, 0))
  CHECK(ar.Serialize("textDefault", textDefault, 1, ""))
  CHECK(ar.Serialize("textToolTip", textToolTip, 1, ""))
  CHECK(ar.Serialize("userActionID", userActionID, 1, 0))
#endif

#if USE_PRECOMPILATION
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    GGameState.CompileMultiple(condition, conditionExp, GWorld->GetMissionNamespace()); // mission namespace
#endif
  ar.SetParams(old);

  return LSOK;
}

bool EntityAI::MustBeSaved() const
{
  // save whenever the unit has the brain, otherwise invalid references will occur
  return CommanderUnit() || base::MustBeSaved();
}

/*!
\patch_internal 1.23 Date 09/11/2001 by Ondra
- New: Some more flags serialized.
\patch 5153 Date 4/20/2007 by Jirka
- Fixed: Functions getVariable, setVariable can be used for more object types now
*/

LSError EntityAI::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  if (ar.IsSaving())
  {
    RString type = GetType()->GetName();
    CHECK(ar.Serialize("type", type, 1))
  }
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    bool moveOut = _moveOutState.GetEnumValue()>MOIn;
    DoAssert(_moveOutState!=MOMovingOut);
    CHECK(ar.Serialize("moveOut", moveOut, 1, false))
    if (ar.IsLoading())
      _moveOutState = moveOut ? MOMovedOut : MOIn;

    CHECK(::Serialize(ar,"stratGoToPos", _stratGoToPos, 1, VZero))
    CHECK(ar.Serialize("limitSpeed", _limitSpeed, 1, 0))
    CHECK(ar.Serialize("limitSpeedForced",_limitSpeedForced, 1, 0))
  }
  SerializeBitBool(ar, "isDead", _isDead, 1, false)
  CHECK(::Serialize(ar,"whenDestroyed", _whenDestroyed, 1, TIME_MAX))
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {

  #if ENABLE_ALLOW_DAMAGE
    CHECK(ar.Serialize("allowDamage", _allowDamage, 1, true))
  #endif

    CHECK(ar.Serialize("sensorColID", _sensorColID, 1, SensorColID(-1)))

    SerializeBitBool(ar,"landContact",_landContact,1,false); // TODO: default is true
    SerializeBitBool(ar,"objectContact",_objectContact,1,false);
    SerializeBitBool(ar,"objectContact",_waterContact,1,false);
    SerializeBitBool(ar,"inFormation",_inFormation,1,true);
    
    CHECK(ar.Serialize("isStopped", _isStopped, 1, false))
    CHECK(ar.Serialize("upsideDown", _isUpsideDown, 1, false))

    CHECK(ar.Serialize("nextUserActionId", _nextUserActionId, 1, 0))
    CHECK(ar.Serialize("UserActions", _userActions, 1))
    for (int i=0; i<NEntityEvent; i++)
    {
      const char *eventName = FindEnumName((EntityEvent)i);
      BString<80> eHandlerName;
      sprintf(eHandlerName,"EventHandlers%s",eventName);
      CHECK(ar.Serialize(eHandlerName.cstr(), _eventHandlers[i], 1))
    }

    // variables
    {
      void *old = ar.GetParams();
      ar.SetParams(&GGameState);
      DoAssert(_vars.IsSerializationEnabled());
      LSError result = ar.Serialize("Variables", _vars._vars, 1);
      ar.SetParams(old);
      // class Variables can be missing in older saves
      if (result != LSOK && result != LSNoEntry) return result;
    }
    
    
    if (ar.IsLoading())
    {
      if (ar.GetPass() == ParamArchive::PassFirst)
      {
        AutoArray<RStringB> textures;
        CHECK(ar.SerializeArray("setObjectTextures",textures,0))
        for (int i=0; i<_hiddenSelectionsTextures.Size(); i++)
        {
          if (i>=textures.Size()) break;
          Ref<Texture> texture = !textures[i].IsEmpty() ? GlobLoadTexture(textures[i]) : NULL;
          SetObjectTexture(i, texture);
        }

        AutoArray<RStringB> materials;
        CHECK(ar.SerializeArray("setObjectMaterials",materials,0))
        for (int i=0; i<_hiddenSelectionsMaterials.Size(); i++)
        {
          if (i>=materials.Size()) break;
          Ref<TexMaterial> mat = !materials[i].IsEmpty() ? GTexMaterialBank.New(TexMaterialName(materials[i])) : NULL;
          SetObjectMaterial(i, mat);
        }
      }
    }
    else
    {
      AutoArray<RStringB> textures;
      textures.Resize(_hiddenSelectionsTextures.Size());
      for (int i=0; i<_hiddenSelectionsTextures.Size(); i++)
      {
        textures[i] = _hiddenSelectionsTextures[i].NotNull() ? _hiddenSelectionsTextures[i]->GetName() : RStringB();
      }
      CHECK(ar.SerializeArray("setObjectTextures",textures,0))
    
      AutoArray<RStringB> materials;
      materials.Resize(_hiddenSelectionsMaterials.Size());
      for (int i=0; i<_hiddenSelectionsMaterials.Size(); i++)
      {
        materials[i] = _hiddenSelectionsMaterials[i].NotNull() ? _hiddenSelectionsMaterials[i]->GetName() : RStringB();
      }
      CHECK(ar.SerializeArray("setObjectMaterials",materials,0))
    }
    
    
  }

  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
  {
    // destroy the current supply
    if (_supply)
    {
      _supply->Destroy(this);
      _supply = NULL;
    }
  }
  void *oldParams = ar.GetParams();
  ar.SetParams(this);
  CHECK(ar.Serialize("Supply", _supply, 1))
  ar.SetParams(oldParams);
  if (_supply)
  {
    SupplyUnitList &list = GetSupplyUnits();
    CHECK(ar.SerializeRefs("SupplyUnits", list, 1))
  }

  CHECK(ar.Serialize("userStopped", _userStopped, 1, false))

  if (ar.IsLoading())
  {
    _snappedPosition = VZero;
    UpdateSnappedPosition();
  }
  
  CHECK(ar.Serialize("pilotLight", _pilotLight, 1, false))
  CHECK(ar.Serialize("collisionLight", _collisionLight, 1, false))
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
  {
    SwitchLight(_collisionLight);
  }

  // TODO: check and remove _condition
  return LSOK;

/* TODO: need serialization ?
  
  Vector3 _lockedBeg; // where is the lock?

  // locking
  bool _locked :1,_tempLocked:1; // is locked?
  bool _isUpsideDown:1;
  bool _lockedSoldier; // type of lock
  Time _lastMovement; // when last movement prevented stopping
  float _shootVisible; // how much are we visible (1.0 default)
  float _shootTimeRest; // how long will current visibility last (before returning to 1.0)
  OLink(Object) _shootTarget; // what we fired at
  float _lockedRadius; // how large is the lock?
  //Ref<AILocker> _locker; // who locked - no real AI ...
  Ptr<AILocker> _locker; // who locked - no real AI ...
  SensorColID _sensorColID;
  AutoArray<MuzzleState> _muzzleState;
  OLink(FlashGunFire) _gunFlash;
  mutable VisibilityTrackerCache _visTracker;
  float _rearmCredit; // used during rearm
  // GoTo/FireAt
  float _avoidAside,_avoidAsideWanted; // obstacle avoidance offset
  Time _lastSimplePath; // formation pilot helper
  LinkTarget _fireTarget;

  FFEffects _ff; // result of ff simulation - only in SimCamera mode

  // support for getting in/out
  float _nearestEnemy; // nearest known enemy
*/
}

#define ENTITY_AI_MSG_LIST(XX) \
  XX(Create, CreateVehicleAI) \
  XX(UpdateGeneric, UpdateVehicleAI) \
  XX(UpdateDamage, UpdateDamageVehicleAI)

DEFINE_NETWORK_OBJECT(EntityAI, base, ENTITY_AI_MSG_LIST)

DEFINE_NET_INDICES_EX(CreateVehicleAI, CreateVehicle, CREATE_VEHICLE_AI_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateVehicleAI, UpdateVehicle, UPDATE_VEHICLE_AI_MSG, NoErrorInitialFunc)
DEFINE_NET_INDICES_EX_ERR(UpdateDamageVehicleAI, UpdateDamageVehicle, UPDATE_DAMMAGE_VEHICLE_AI_MSG, EntityAI::CalculateErrorInitialDamage)

#define ENTITY_AI_FULL_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateVehicleAIFull)

DEFINE_NETWORK_OBJECT(EntityAIFull, base, ENTITY_AI_FULL_MSG_LIST)

DEFINE_NET_INDICES_EX_ERR(UpdateVehicleAIFull, UpdateVehicleAI, UPDATE_VEHICLE_AI_FULL_MSG, NoErrorInitialFunc)

/*!
\patch 1.24 Date 09/26/2001 by Ondra
- Fixed: vehicle light state was not transferred across network.
*/
NetworkMessageFormat &EntityAI::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
  case NMCCreate:
    base::CreateFormat(cls, format);
    CREATE_VEHICLE_AI_MSG(CreateVehicleAI, MSG_FORMAT)
    break;
  case NMCUpdateDamage:
    base::CreateFormat(cls, format);
    UPDATE_DAMMAGE_VEHICLE_AI_MSG(UpdateDamageVehicleAI, MSG_FORMAT_ERR)
    break;
  case NMCUpdateGeneric:
    base::CreateFormat(cls, format);
    UPDATE_VEHICLE_AI_MSG(UpdateVehicleAI, MSG_FORMAT_ERR)
    break;
  default:
    base::CreateFormat(cls, format);
    break;
  }
  return format;
}

NetworkMessageFormat &EntityAIFull::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
  case NMCUpdateDamage:
    base::CreateFormat(cls, format);
    break;
  case NMCUpdateGeneric:
    base::CreateFormat(cls, format);
    UPDATE_VEHICLE_AI_FULL_MSG(UpdateVehicleAIFull, MSG_FORMAT_ERR)
    break;
  default:
    base::CreateFormat(cls, format);
    break;
  }
  return format;
}

bool EntityAI::OnSendCreateMessage(NetworkMessageContext &ctx)
{
  VehicleListType list = GetListType();
  RString varName = _varName;
  int idVeh = ID();
  PREPARE_TRANSFER(CreateVehicle)
  if (TRANSF_BASE(list, (int &)list) != TMOK) return false;
  if (TRANSF_BASE(varName, varName) != TMOK) return false;
  if (TRANSF_BASE(idVehicle, idVeh) != TMOK) return false;
  return true;
}

bool EntityAI::RejectMsg(NetworkMessageClass cls, NetworkMessage *msg)
{
  // if the position is unexpectedly different (movement way too fast), we reject the update, forcing our version of the simulation
  if (!GetNetworkManager().IsServer()) return false;
  if (GetNetworkManager().GetClientState() != NCSPlaying) return false;
  switch (cls)
  {
  case NMCUpdatePosition:
    {
      PREPARE_TRANSFER_MSG(UpdatePositionVehicle,msg)
        if (IsInLandscape())
        {
          // TODO: implement some history tracking, so that we can use stricter limits
          float difference2 = message->_position.DistanceXZ2(_futureVisualState->Position());
          float maxSpeed = GetType()->GetMaxSpeedMs();
          const float maxOffShortTerm = 5.0f; // we allow 5 m difference
          const float maxMulShortTerm = 2.0f; // we allow maxspeed difference

          /*
          const float maxOffLongTerm = 5.0f; // we allow 5 m different
          const float maxMulLongTerm = 0.1f; // we allow little difference in speed
          */
          if (difference2>Square(maxSpeed*maxMulShortTerm+maxOffShortTerm))
          {
            // suspicion movement, reject
            return true;
          }
        }
        break;
    }

  }
  return false;
}



TMError EntityAI::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCCreate:
    TMCHECK(base::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(CreateVehicleAI)
      TMCHECK(ctx.Transfer(message->_variables, _synchronizedVariables));
    }
    break;
  case NMCUpdateDamage:
    TMCHECK(base::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateDamageVehicleAI)
      TRANSF(isDead)
    }
    break;
  case NMCUpdateGeneric:
    TMCHECK(base::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateVehicleAI)

      TRANSF(pilotLight)
      TRANSF(collisionLight)
      TRANSF_CONTENT_PAR(supply, this)
      TRANSF_EX(variables, this)
      TRANSF_EX(textures,_hiddenSelectionsTextures)
      TRANSF_EX(materials,_hiddenSelectionsMaterials)
    }
    break;
  default:
    return base::TransferMsg(ctx);
  }
  return TMOK;
}

TMError EntityAIFull::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCUpdateDamage:
    TMCHECK(base::TransferMsg(ctx))
    break;
  case NMCUpdateGeneric:
    TMCHECK(base::TransferMsg(ctx))
    break;
  default:
    return base::TransferMsg(ctx);
  }
  return TMOK;
}

GameValue EntityAI::GetVariableValue( const RString &name ) const
{
  GameValue ret;
  _vars.VarGet(name,ret);
  return ret;
}

float EntityAI::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = 0;
  switch (ctx.GetClass())
  {
  case NMCUpdateDamage:
    {
      error += base::CalculateError(ctx);
      {
        PREPARE_TRANSFER(UpdateDamageVehicleAI)
        ICALCERR_NEQ(bool, isDead, ERR_COEF_STRUCTURE)
      }
    }
    break;
  case NMCUpdateGeneric:
    {
      error += base::CalculateError(ctx);
      {
        PREPARE_TRANSFER(UpdateVehicleAI)

        ICALCERR_NEQ(bool, pilotLight, ERR_COEF_VALUE_MAJOR)
        ICALCERR_NEQ(bool, collisionLight, ERR_COEF_VALUE_MAJOR)
#if _VBS3
        ICALCERR_NEQ(int, lightMode, ERR_COEF_VALUE_MAJOR)
#endif
        if (_supply)
        {
          NetworkMessageContextWithError subctx(message->_supply, ctx);
          error += _supply->CalculateError(subctx);
        }
        error += IVariables::CalculateError(this,message->_variables) * ERR_COEF_VALUE_MAJOR;
        if (_hiddenSelectionsTextures.Size()!=message->_textures.Size())
          error += ERR_COEF_VALUE_NORMAL;
        else
        {
          for (int i=0; i<_hiddenSelectionsTextures.Size(); i++)
          {
            const RStringB& iName = _hiddenSelectionsTextures[i].NotNull() ? _hiddenSelectionsTextures[i]->GetName() : RStringBEmpty;
            if (iName != message->_textures[i]) error += ERR_COEF_VALUE_NORMAL;
          }
        }
        if (_hiddenSelectionsMaterials.Size()!=message->_materials.Size())
          error += ERR_COEF_VALUE_NORMAL;
        else
        {
          for (int i=0; i<_hiddenSelectionsMaterials.Size(); i++)
          {
            const RStringB& iName = _hiddenSelectionsMaterials[i].NotNull() ? _hiddenSelectionsMaterials[i]->GetName() : RStringBEmpty;
            if (iName != message->_materials[i]) error += ERR_COEF_VALUE_NORMAL;
          }
        }
      }
    }
    break;
  default:
    error += base::CalculateError(ctx);
    break;
  }
  return error;
}

float EntityAI::CalculateErrorInitial(NetworkMessageClass cls)
{
  float error = Entity::CalculateErrorInitial(cls);
  switch (cls)
  {
  case NMCUpdateDamage:
    {
      if (_isDead) error += ERR_COEF_STRUCTURE;
    }
    break;
  }
  return error;
}

float EntityAI::CalculateErrorInitialDamage(NetworkMessage *msg)
{
  float error = Entity::CalculateErrorInitialDamage(msg);

  NetworkMessageUpdateDamageVehicleAI *message = static_cast<NetworkMessageUpdateDamageVehicleAI *>(msg);
  if (message->_isDead) error += ERR_COEF_STRUCTURE;

  return error;
}

void EntityAI::CreateSubobjects()
{
  if (_supply)
  {
    _supply->ForEachItemCargo([](InventoryItem *item) -> bool
    {
      GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);
      return false; // continue
    });
  }
}

void EntityAI::UpdateSubobjects()
{
  if (_supply)
  {
    _supply->ForEachItemCargo([](InventoryItem *item) -> bool
    {
      item->UpdateSubobjects();
      GetNetworkManager().UpdateObject(item);
      return false; // continue
    });
  }
}

float EntityAIFull::CalculateError(NetworkMessageContextWithError &ctx)
{
  return base::CalculateError(ctx);
}


DEFINE_CASTING(EntityAIFull)

EntityAIFull::EntityAIFull(const EntityAIType *type, bool fullCreate)
:base(type, fullCreate),  // bubble allocateVisualState to EntityAI

_shootVisible(0),_shootAudible(0),_shootTimeRest(1e10),
#if _VBS3
  _shootTimeRestAudible(1e10),
#endif
_lastWeaponNotReady(Glob.time),_lastWeaponReady(Glob.time-10),

_fireState(FireDone),
_fireStateDelay(Glob.time-60),

// when we scanned for visible targets
_newTargetsTime(TIME_MIN),
_nearestEnemy(1e10), // nearest known enemy

_cameraInterest(0),

_rearmCredit(0), // used during rearm
//_autoAimLocked(false),

_reloadEnabled(true),
_useSuppressiveFire(SuppressNo),
_suppressUntil(TIME_MIN),
 
_engineOffTimeOutInProgress(false),
_heatSourceDirection(VZero),
_surfaceAudibility(1.0),
_surfaceVisibility(1.0),
_poseAudibility(1.0),
_poseVisibility(1.0)
{
  for (int i=0; i<NTgtCategory; i++)
    _trackTargetsTime[i] = TIME_MIN;

  // prepare everything for the next shot
  AfterFire();
}
  // destructor
EntityAIFull::~EntityAIFull()
{
}

void EntityAIFull::Init(Matrix4Par pos, bool init)
{
  base::Init(pos, init);
}

class RemoveWeaponsFunc : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._weapons) context._weapons->DeleteAllWeapons(entity, false);
    return false; // continue
  }
};

void EntityAIFull::OnSetDelete()
{
  base::OnSetDelete();

  // remove all weapons both from cargo and WeaponStates
  ClearWeaponCargo(false);

  RemoveWeaponsFunc func;
  ForEachTurret(func);
}

/// find the weapons system shoot last
class FindLastShotFunc : public ITurretFunc
{
private:
  Time &_lastShotTime;
  WeaponsState *&_lastShotWeapons;

public:
  FindLastShotFunc(Time &lastShotTime, WeaponsState *&lastShotWeapons)
    : _lastShotTime(lastShotTime), _lastShotWeapons(lastShotWeapons) {}

  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._weapons->_lastShotTime > _lastShotTime)
    {
      _lastShotTime = context._weapons->_lastShotTime;
      _lastShotWeapons = context._weapons;
    }
    return false; // continue
  }
};

Time EntityAIFull::GetLastShotTime() const
{
  Time lastShotTime = TIME_MIN;
  WeaponsState *lastShotWeapons = NULL;
  FindLastShotFunc func(lastShotTime, lastShotWeapons);
  ForEachTurret(func);
  return lastShotTime;
}

Entity *EntityAIFull::GetLastShot() const
{
  Time lastShotTime = TIME_MIN;
  WeaponsState *lastShotWeapons = NULL;
  FindLastShotFunc func(lastShotTime, lastShotWeapons);
  ForEachTurret(func);
  return lastShotWeapons ? lastShotWeapons->_lastShot : NULL;
}

#if 0 // _PROFILE // _ENABLE_CHEATS
#pragma optimize("",off)
#endif

static inline Vector3 Nlerp(Vector3Par v0, Vector3Par v1, float t)
{
  return (v0*(1-t)+v1*t).Normalized();
}

static Vector3 Slerp(Vector3Par v0, Vector3Par v1, float t)
{ // see http://number-none.com/product/Understanding%20Slerp,%20Then%20Not%20Using%20It/
  float dot = v0*v1;
  const float tolerance = 0.0005f;
  if ((1-dot)<tolerance && t<2)
  {
    return Nlerp(v0,v1,t);
  }
  else
  {
    float theta_0 = acos(dot);
    float theta = theta_0*t;

    Vector3 v2 = (v1 - v0*dot).Normalized();
    return v0*cos(theta)+v2*sin(theta);
  }
}

#if 0 // _PROFILE // _ENABLE_CHEATS
#pragma optimize("",on)
#endif

bool EntityAIFull::SimulationReady(SimulationImportance prec) const
{
  int xMin,xMax,zMin,zMax;
  float radius = GetRadius()*2+5;
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,FutureVisualState().Position(),FutureVisualState().Position(),radius);
  // all data near any entity need to be ready so that it can be simulated
  // we need to load enough so that RoadSurfaceY is ready
  xMin--,xMax++;
  zMin--,zMax++;
  bool ret = ObjRectangleReady(xMin, xMax, zMin, zMax);
  return ret;
}

void EntityAIFull::Simulate(float deltaT, SimulationImportance prec)
{
#if _EXT_CTRL  //ext. Controller like HLA, AAR: apply the current speed
  if(IsExternalControlled() && !GAAR.IsPlaying())
  {
//    Matrix4 movePos;
//    ApplySpeed(movePos,deltaT);
    Vector3 position=Position();
    position+=FutureVisualState()._speed*deltaT;
    
    Frame moveTrans = *this;
//    moveTrans.SetTransform(movePos);
    moveTrans.SetPosition(position);
    Move(moveTrans); // finally apply move
  }
#endif

  base::Simulate(deltaT,prec);

  // Simulate the heat source transition
  {
    // Get the heat source direction from the sun
    // See HEATSOURCEDIRECTION
    LightSun *sun = GScene->MainLight();
    Vector3 worldHeatDirection = sun->Direction();

    // Transform the heat source direction into model coordinates and normalize it (since orientation may contain a scale)
    Vector3 objectHeatDirection = FutureVisualState().Orientation().InverseRotation() * sun->Direction();
    objectHeatDirection.Normalize();

    // Interpolate between the current local heat source and actual value
    float invFullTransitionTime = 1.0f / 60.0f;
    _heatSourceDirection = _heatSourceDirection + (objectHeatDirection - _heatSourceDirection) * invFullTransitionTime * deltaT;
  }
}

LSError EntityAIFull::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    CHECK(ar.SerializeEnum("fireState", _fireState, 1, FireDone))
    CHECK(::Serialize(ar, "fireStateDelay", _fireStateDelay, 1))

    CHECK(::Serialize(ar, "newTargetsTime", _newTargetsTime, 1, Glob.time))
    CHECK(::Serialize(ar, "trackTargetsTimeEn", _trackTargetsTime[TgtCatEnemy], 1, Glob.time))
    //CHECK(::Serialize(ar, "trackTargetsTimeFr", _trackTargetsTime[TgtCatFriendly], 1, Glob.time))
    CHECK(ar.Serialize("cameraInterest", _cameraInterest, 1, 0))

    CHECK(ar.SerializeEnum("useSuppressiveFire", _useSuppressiveFire, 1, SuppressNo))
    CHECK(::Serialize(ar, "suppressUntil", _suppressUntil, 1, Time(0)))
  }

  return LSOK;
}

/*!
\patch_internal 5090 Date 11/28/2006 by Jirka
- Fixed: Diagnostics DECostMap did not show water fields
*/

void EntityAIFull::DrawDiags()
{
  Vector3 camPos = GScene->GetCamera()->Position();
    
#if _ENABLE_CHEATS
  AIBrain *unit=CommanderUnit();
  if( !unit )
  {
    base::DrawDiags();
    return;
  }
#define DRAW_OBJ(obj) GScene->DrawObject(obj)

  LODShape *forceArrow=GScene->ForceArrow();

  if (!QIsManual() && CHECK_DIAG(DECombat))
  {
    // let brain draw diagnostics as well    
    AIBrain *driver = PilotUnit();
    if (driver)
      driver->DrawDiags();
  }

  if (CHECK_DIAG(DECombat) || CHECK_DIAG(DEPath))
  {
    RString fsm,path;
    AIBrain *driver = PilotUnit();
    if (driver)
    {
      if (driver->IsLocal())
      {
        int nBeh = driver->NBehaviourDiags();
        for (int i=0; i<nBeh; i++)
        {
          RString str = driver->BehaviourDiags(i);
          if (str.GetLength()>0)
            fsm = fsm + " " + str;
        }
        path = Format(" path %d",driver->GetPathId());
      }
      if (GetNetworkManager().IsClient())
      {
        fsm = fsm + " " + RemoteDiagText();
      }
    }
    GScene->DrawDiagText(GetDebugName()+fsm+path,AimingPosition(RenderVisualState())+VUp*CollisionSize());
  }

#if 1

  if (CHECK_DIAG(DECombat) || CHECK_DIAG(DEVisualStates))
  {
    LODShape *sphere = GScene->Preloaded(SphereModel);
    { // future visual state - green/blue
      Ref<Object> obj = new ObjectColored(sphere, VISITOR_NO_ID);
      obj->SetPosition(AimingPosition(FutureVisualState()));
      obj->SetConstantColor(Color(0, 1.0, 0.0, 0.5));
      obj->SetScale(0.50f);
      DRAW_OBJ(obj);
    }
    if (_remoteState)
    { 
      { // remote state
        Ref<Object> obj = new ObjectColored(sphere, VISITOR_NO_ID);
        obj->SetPosition(_remoteState->pos+Vector3(0,4.7,0));
        obj->SetConstantColor(Color(0.5, 0.5, 0.0, 1.0));
        obj->SetScale(0.35f);
        DRAW_OBJ(obj);
      }
      { // extrapolated remote state - blue
        Ref<Object> obj = new ObjectColored(sphere, VISITOR_NO_ID);
        float age = Glob.time-_remoteState->time;
        obj->SetPosition(_remoteState->pos+age*_remoteState->speed+Vector3(0,4.5,0));
        obj->SetConstantColor(Color(0, 0.0, 0.8, 1.0));
        obj->SetScale(0.30f);
        DRAW_OBJ(obj);

        // pos+posOffset (received remote pos)
        GScene->DrawDiagArrow(_remoteState->pos+_remoteState->posOffset+Vector3(0,3.5,0),(FutureVisualState().Orientation()+_remoteState->orientOffset).Direction(),1.0f,Color(0.8,0.4,0.0,1.0),true);

        // visual state
        GScene->DrawDiagArrow(RenderVisualState().Position()+Vector3(0,3.8,0),_remoteState->orient.Direction(),3.0f,Color(0.5, 0.5, 0.0, 1.0),true);

        #if ORIENT_PREDICT
        GScene->DrawDiagArrow(RenderVisualState().Position()+Vector3(0,4,0),(FutureVisualState().Orientation()+_remoteState->orientOffset).Direction(),2.0f,Color(0,0,0.8,1.0),true);
        #endif
      }
    }
  }
    
  if ((CHECK_DIAG(DEPath) || CHECK_DIAG(DEPathFind)) &&
    !GetType()->IsSlow() && // static object or inventory item
    !_isDead &&
    !(GWorld->PlayerManual() && GWorld->PlayerOn() && GWorld->PlayerOn()->Brain() == PilotUnit()))
  {
    // steerpoints
    {
      Point3 steerPos=SteerPoint(GetSteerAheadSimul(),GetSteerAheadPlan());
      //AvoidCollision(steerPos);

      {
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
        Point3 pos=steerPos;

        float size=0.3f;
        arrow->SetPosition(pos);
        arrow->SetOrient(VUp,VForward);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(Color(0.7,0.5,0));
        //#define DRAW_OBJ(obj) (obj)->Draw(0,ClipAll,*(obj))
        DRAW_OBJ(arrow);
      }
      
      steerPos += RenderVisualState().DirectionAside()*_avoidAside;
      
      {
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
        Point3 pos=steerPos;

        float size=0.1f;
        arrow->SetPosition(pos);
        arrow->SetOrient(VUp,VForward);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(Color(0.7,0.5,0));
        //#define DRAW_OBJ(obj) (obj)->Draw(0,ClipAll,*(obj))
        DRAW_OBJ(arrow);
      }
    }

    {
      Point3 steerPos=SteerPoint(GetPredictTurnSimul(),GetPredictTurnPlan());

      Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
      Point3 pos=steerPos;

      float size=0.2f;
      arrow->SetPosition(pos);
      arrow->SetOrient(VUp,VForward);
      arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
      arrow->SetScale(size);
      arrow->SetConstantColor(Color(0.9,0.3,0.3));
      DRAW_OBJ(arrow);
    }

    // draw strategic GoTo destination
    float size=GLOB_WORLD->CameraOn()==this ? 8 : 4;
    GScene->DrawDiagArrow(GetDebugName()+" Go To",_stratGoToPos,VUp,size,Color(1,1,1),true);
  }
#endif

#if 1
    // draw current weapon direction
  if (CHECK_DIAG(DECombat))
  {
    // TODO: which turret?
    TurretContextEx context;
    bool valid = GetPrimaryGunnerTurret(unconst_cast(RenderVisualState()), context);

    {
      Vector3 dir;
      if (valid)
        dir = GetWeaponDirection(RenderVisualState(), context, WeaponModePtr());
      else
        dir = RenderVisualState().Direction();
      if (dir.SquareSize()>0.1)
      {
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

        float size=0.1;
        arrow->SetPosition(RenderVisualState().Position());
        // avoid singular matrix
        if (dir.Y()<0.9f) arrow->SetOrient(dir,VUp);
        else arrow->SetOrient(dir,VAside);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(Color(1,0,0,0.5));

        DRAW_OBJ(arrow);
      }
    }
    {
      Vector3 dir;
      if (valid)
        dir = GetWeaponDirectionWanted(context, WeaponModePtr());
      else
        dir = RenderVisualState().Direction();
      if (dir.SquareSize()>0.1)
      {
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

        float size=0.2;
        arrow->SetPosition(RenderVisualState().Position());
        arrow->SetOrient(dir,VUp);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(Color(1,0.1,0,0.5));

        DRAW_OBJ(arrow);
      }
    }
    {
      Vector3 dir;
      if (valid)
        dir = GetEyeDirection(RenderVisualState(), context);
      else
        dir = RenderVisualState().Direction();
      if (dir.SquareSize()>0.1)
      {
        Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

        float size=0.2;
        arrow->SetPosition(RenderVisualState().Position());
        arrow->SetOrient(dir,VUp);
        arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
        arrow->SetScale(size);
        arrow->SetConstantColor(Color(1,1,0,0.5));

        DRAW_OBJ(arrow);
      }
    }
  }
#endif

  if (CHECK_DIAG(DEPath) && GetType()->IsSupply())
  {
    const Entity::VisualState &vs = RenderVisualState();
    int index = GetType()->GetSupplyPointIndex();
    if (index >= 0)
    {
      const LODShape *shape = GetShape();
      if (shape)
      {
        int mem = shape->FindMemoryLevel();
        if (mem >= 0)
        {
          Vector3 aPos = vs.PositionModelToWorld(AnimatePoint(vs, mem, index));
          Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
          float size=0.2;

          arrow->SetPosition(aPos);
          arrow->SetOrient(Vector3(0,-1,0),Vector3(0,0,-1));
          arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(-forceArrow->BoundingCenter()*size));
          arrow->SetScale(size);
          arrow->SetConstantColor(Color(0,1,0));
          DRAW_OBJ(arrow);
        }
      }
    }
  }

  // draw path
  if (PilotUnit())
  {
    if(CHECK_DIAG(DEPath))
    {
      const Path &path=PilotUnit()->GetPath();
      float size=0.05;
      PackedColor color(Color(1,1,0,1));
      if (this==GLOB_WORLD->CameraOn())
        size*=2;
      if(path.Size())
      {
        Vector3* points = ALLOCA(Vector3, path.Size());
        for (int i=0; i<path.Size(); i++)
        {
          Vector3 cPos = path[i]._pos;
          saturateMax(cPos[1],GLandscape->SurfaceYAboveWater(cPos[0],cPos[2])+0.05f);
          points[i] = cPos;
        }
        enf::Shape::CreateLines(points, path.Size(), color, enf::SS_ONCE);
      
        for (int i=0; i<path.Size(); i++)
        {
          const OperInfoResult &info=path[i];

          Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

          Vector3Val pos = info._pos;
          arrow->SetPosition(pos);
          bool revert = info._cost > path.GetReplanCost();
          if (revert)
          {
            arrow->SetOrient(Vector3(0,-1,0),Vector3(0,0,-1));
            arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(-forceArrow->BoundingCenter()*size));
          }
          else
          {
            arrow->SetOrient(Vector3(0,+1,0),Vector3(0,0,+1));
            arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
          }
          arrow->SetScale(size);
          if (info._type == NTRoad) color=PackedColor(Color(0,1,1,1));

          arrow->SetConstantColor(color);

          DRAW_OBJ(arrow);
        }
        // some special points on the path
        if (path.Size() >= 2)
        {
          // replan point
          Vector3 pos = path.PosAtCost(path.GetReplanCost());
          enf::Shape::CreateDiamond(pos - Vector3(4.0f * size), pos + Vector3(4.0f * size), PackedColor(Color(1, 1, 0, 1)), enf::SS_ONCE);
        }
        if (path.Size() >= 2)
        {
          // force replan point
          Vector3 pos = path.PosAtCost(path.GetForceReplanCost());
          enf::Shape::CreateDiamond(pos - Vector3(4.0f * size), pos + Vector3(4.0f * size), PackedColor(Color(1, 0, 0, 1)), enf::SS_ONCE);
        }
      }
      {
        // replanning from
        Vector3Val pos = PilotUnit()->GetPlanningFrom();
        enf::Shape::CreateDiamond(pos - Vector3(4.0f * size), pos + Vector3(4.0f * size), PackedColor(Color(0, 1, 0, 1)), enf::SS_ONCE);
      }
    }
  }

  base::DrawDiags();
#endif
}

void EntityAIFull::DrawFocusedDiags()
{
#if _ENABLE_CHEATS
  AIBrain *unit=CommanderUnit();
  if (unit)
  {
    const float maxDist = 50.0f;
    const float maxDistDetailed = 30.0f;
    const float maxDistExtended = 20.0f;
    int diagRange = toIntCeil(maxDist*InvLandGrid);
    
    Vector3 camPos = GScene->GetCamera()->Position();
      
    // draw cost for a focused unit, or if there is none, for a player (usefull for free camera)
    {
      LODShape *rectShape = GScene->Preloaded(RectangleModel);
      int index=0;
  
#if 1
      // draw fields costs
      if (CHECK_DIAG(DECostMap))
      {
        bool soldier = unit->IsFreeSoldier();
        LandIndex xLand(toIntFloor(camPos.X() * InvLandGrid));
        LandIndex zLand(toIntFloor(camPos.Z() * InvLandGrid));

        Vector3 hideL, hideR;

        // fallback - get values as given by FSM, works only for AI units
        unit->GetHideFrom(hideL,hideR);

        for (LandIndex z(zLand-diagRange); z<=zLand+diagRange; z++) for (LandIndex x(xLand-diagRange); x<=xLand+diagRange; x++)
        {
          Ref<OperField> fld = GLOB_LAND->OperationalCache()->GetOperField(
            x, z, MASK_AVOID_OBJECTS | MASK_PREFER_ROADS | MASK_USE_BUFFER
          );
          if (!fld)
            continue;
          // we want to display the cover as well
          fld->PrepareForPathing();

          for (OperFieldIndex zz(0); zz<OperItemRange; zz++) for (OperFieldIndex xx(0); xx<OperItemRange; xx++)
          {

            Vector3 pos;
            pos.Init();
            pos[0] = x * LandGrid + xx * OperItemGrid + 0.5 * OperItemGrid;
            pos[2] = z * LandGrid + zz * OperItemGrid + 0.5 * OperItemGrid;
            pos[1] = 0;

            if (pos.DistanceXZ2(camPos) <= Square(maxDist))
            {
              OperItem item = fld->GetItem(xx, zz);
              OperItemType type = soldier ? item.GetTypeSoldier() : item.GetType();
              static const Color typeColor[NOperItemType]=
              {
                Color(1, 1, 1, 0), // OITNormal,
                Color(0, 1, 1, 0.25), // OITAvoidBush,
                Color(0, 1, 0, 0.25), // OITAvoidTree,
                Color(0, 0, 0, 0.25), // OITAvoid,
                Color(0, 0, 1, 1), // OITWater,
                Color(1, 0, 1, 1), // OITSpaceRoad,
                Color(1, 1, 1, 1), // OITRoad,
                Color(0, 1, 1, 1), // OITSpaceBush,
                Color(0, 1, 0.5, 1), // OITSpaceHardBush,
                Color(0, 1, 0, 1), // OITSpaceTree,
                Color(0, 0, 0, 1), // OITSpace
                Color(1, 1, 0, 1), // OITRoadForced
              };
              ColorVal color = typeColor[type];

              pos[1] = GLOB_LAND->SurfaceYAboveWater(pos[0],pos[2]) + 0.2;
              Ref<Object> rect;
              if (index<MapDiags.Size())
                rect = MapDiags[index];
              else
              {
                rect = new ObjectColored(rectShape, VISITOR_NO_ID);
                // scale the rectangle by the grid
                MapDiags.Access(index);
                MapDiags[index]=rect;
              }
              index++;
              Clearance clearance = item.GetClearance();
              float size = rectShape->BoundingSphere() * H_SQRT2;
              rect->SetScale(clearance<ClearanceNormal ? H_INVSQRT2*OperItemGrid / size : OperItemGrid / size);
              if (color.A()>0)
              {
                rect->SetPosition(pos);
                rect->SetConstantColor(color);
                DRAW_OBJ(rect);
              }
              else if (clearance<ClearanceNormal)
              {
                rect->SetPosition(pos+Vector3(0,0.7f,0));
                rect->SetConstantColor(Color(1,1,1,0.5f));
                DRAW_OBJ(rect);
              }

              // we want to show the cover only close
              if (pos.DistanceXZ2(camPos) <= Square(maxDistDetailed))
              {
                float CoverSuitableAgainst(const OperCover &cover, Vector3Par hideLPos, Vector3Par hideRPos);

                int nCover = 0;

                // we want to show diagnostics about cover suitability only when being very close
                bool extended = pos.DistanceXZ2(camPos) <= Square(maxDistExtended);

                // display diagnostics for each cover candidate
                const OperCover *cover = fld->GetCover(xx,zz,nCover);
                for (int c=0; c<nCover; c++)
                {
                  Matrix3 orient(MRotationY,-cover[c]._heading);
                  Vector3Val cPos = cover[c]._pos;

                  float suitable = 0.5; 
                  if (extended)
                  {
                    suitable = CoverSuitableAgainst(cover[c],hideL,hideR);
                    
#if DIAG_FALSE_COVER
                      bool valid = cover[c]._valid==OperCover::Valid;
#else
                      const bool valid = true;
#endif
                      // indicate what field is this cover associated with (such field is an entry)
                      enf::Shape::CreateLine(cPos+orient.Direction()*0.45f,pos,PackedColor(valid ? Color(0,0.5,0) : Color(0.25,0.25,0)), enf::SS_ONCE);
                    }
                  
                  LODShape *forceArrow=GScene->ForceArrow();

                  Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
                  float size=0.05f*(suitable+1);
                  arrow->SetPosition(cPos);
                  arrow->SetOrient(orient);
                  arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
                  arrow->SetScale(size);
                  
#if DIAG_FALSE_COVER
                    // when not valid, suitable would be 0
                    switch (cover[c]._valid)
                    {
                      case OperCover::Valid: arrow->SetConstantColor(Color(1-suitable,suitable,0)); break; // red..green
                      case OperCover::NoFreeSpace: arrow->SetConstantColor(Color(0,0,1.0)); break; // light blue
                      case OperCover::NoCollision: arrow->SetConstantColor(Color(0.5,0,0.5)); break; // dark magenta
                      case OperCover::Unaccessible: arrow->SetConstantColor(Color(0.1,0.1,0.0)); break; // very dark yellow
                      default: arrow->SetConstantColor(Color(0.3,0.3,0.3)); break; // dark gray
                    }
#else
                    arrow->SetConstantColor(Color(1-suitable,suitable,0));
#endif
                  DRAW_OBJ(arrow);
                }
              }
            }
          }
        }
      }
#endif
      // done: draw field costs

      // prepare list of targets for suppress calculations
      SuppressTargetList suppressTargets;
      if (CHECK_DIAG(DERoads) || CHECK_DIAG(DECombat))
        suppressTargets.CollectTargets(unit);

      // draw road connections
      if (CHECK_DIAG(DERoads))
      {
        bool soldier = CommanderUnit() && CommanderUnit()->IsFreeSoldier();
        LODShape *shape = GetShape();
        float vehWidth = 0;
        if (shape) vehWidth = shape->Max().X() - shape->Min().X();

        int x, xLand = toIntFloor(camPos.X() * InvLandGrid);
        int z, zLand = toIntFloor(camPos.Z() * InvLandGrid);
        for (z=zLand-diagRange; z<=zLand+diagRange; z++) for (x=xLand-diagRange; x<=xLand+diagRange; x++)
        if (InRange(x,z))
        {
          // adapted from  CStaticMapMain.DrawRoadNet
          const RoadList &list = GLandscape->GetRoadNet()->GetRoadList(x, z);
          for( int r=0; r<list.Size(); r++ )
          {
            const RoadLink *item = list[r];
            if (!item) continue;

            static const Vector3 above(0, 1, 0);
            static const Vector3 aboveOff(0, 1.2f, 0);
            static const PackedColor colorCenter(Color(1, 1, 1, 0.5));
            static const PackedColor colorCenterBridge(Color(1, 1, 0.5, 0.5));
            static const PackedColor colorCenterLocked(Color(1, 0, 0, 0.5));
            static const PackedColor colorCenterWaiting(Color(1, 1, 0, 0.5));
            static const PackedColor colorConnection(Color(0, 0.5, 1, 0.5));
            static const PackedColor colorConnectionLine(Color(0, 1, 0, 1));
            static const PackedColor colorLinkLine(Color(0, 0.5, 0.4, 1));
            static const PackedColor colorNoConnectionLine(Color(1, 0, 0, 1));
            Vector3 center = item->AIPosition() + above;
            // road center
            {
              PackedColor color = item->IsBridge() ? colorCenterBridge : colorCenter;
              if (item->IsLocked(unit->GetPerson() == this)) color = colorCenterLocked;
              else if (item->IsLockedByWaiting(unit->GetPerson() == this)) color = colorCenterWaiting;
              enf::Shape::CreatePyramid(center - Vector3(0.1, 0, 0.1), center + Vector3(0.1, 0.1, 0.1), color, enf::SS_ONCE);
            }
            {
              float supCost = item->GetSuppressionCost(suppressTargets);
              if (supCost>0)
              {
                float safe = InterpolativC(supCost,0,27,1,0);
                float danger = InterpolativC(supCost,27,1000,0,1);
                Color color(danger,1-danger-safe,safe);
                Vector3 posE = center+VUp*0.2f;
                enf::Shape::CreatePyramid(posE - Vector3(0.1, 0, 0.1), posE + Vector3(0.1, 0.1, 0.1), colorConnection, enf::SS_ONCE);
              }
            }

            for (int j=0; j<item->NNodes(soldier); j++)
            {
              RoadNode node = item->GetNode(j);
              if (!node.IsValid()) continue;

              Vector3 posB = node.Position(soldier, vehWidth);

              for (int k=0; k<node.NConnections(soldier); k++)
              {
                RoadNode to = node.GetConnection(soldier, k);
                if (!to.IsValid()) continue;

                Vector3 posE = to.Position(soldier, vehWidth);

                enf::Shape::CreatePyramid(posE - Vector3(0.07, 0, 0.07), posE + Vector3(0.07, 0.07, 0.07), colorConnection, enf::SS_ONCE);
              // connection link
                GEngine->DrawVerticalArrow(posB+aboveOff, posE+aboveOff,colorConnectionLine, 3, 0, 0.7);
            }
          }
        }
      }
      }

#if 1
      if (CHECK_DIAG(DELockMap))
      {
        bool soldier = unit->IsFreeSoldier();
        OperMapIndex operX(toInt(camPos.X()*InvOperItemGrid));
        OperMapIndex operZ(toInt(camPos.Z()*InvOperItemGrid));
        ILockCache *locks=GLandscape->LockingCache();
        const int range=50;
        for (OperMapIndex x(operX-range); x<=operX+range; x++)
        for (OperMapIndex z(operZ-range); z<=operZ+range; z++)
        {
          bool lockV = locks->IsLocked(x,z,false) > 0;
          bool lockS = locks->IsLocked(x,z,true) > 0;

          if (!lockV && !lockS)
            continue;

          float alpha = (soldier ? lockS : lockV) ? 1.0f : 0.3f;
          Color color(Color(lockV,0,lockS,alpha));

          Vector3 pos;
          pos.Init();
          pos[0] = x * OperItemGrid + 0.5 * OperItemGrid;
          pos[2] = z * OperItemGrid + 0.5 * OperItemGrid;
          pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0],pos[2])+0.2;

          if ((pos - camPos).SquareSizeXZ() <= Square(50))
          {
            Ref<Object> rect;
            if (index < MapDiags.Size())
              rect = MapDiags[index];
            else
            {
              rect = new ObjectColored(rectShape, VISITOR_NO_ID);
              float size = rectShape->BoundingSphere() * H_SQRT2;
              rect->SetScale(OperItemGrid / size);
              MapDiags.Access(index);
              MapDiags[index]=rect;
            }
            index++;
            rect->SetPosition(pos);
            rect->SetConstantColor(color);
            DRAW_OBJ(rect);
          }
        }
      }
#endif

#if 1
      // draw suppressive fire maps
      if (CHECK_DIAG(DECostMap))
      {
        LandIndex xLand(toIntFloor(camPos.X() * InvLandGrid));
        LandIndex zLand(toIntFloor(camPos.Z() * InvLandGrid));
        for (LandIndex z(zLand-diagRange); z<=zLand+diagRange; z++) for (LandIndex x(xLand-diagRange); x<=xLand+diagRange; x++)
        {
          Ref<SuppressField> fld = GLOB_LAND->SuppressCache()->GetField(x, z, MASK_NO_UPDATE|MASK_NO_CREATE);
          if (!fld) continue;
          
          for (OperFieldIndex zz(0); zz<OperItemRange; zz++) for (OperFieldIndex xx(0); xx<OperItemRange; xx++)
          {
            Vector3 pos;
            pos.Init();
            pos[0] = x * LandGrid + xx * OperItemGrid + 0.5 * OperItemGrid;
            pos[2] = z * LandGrid + zz * OperItemGrid + 0.5 * OperItemGrid;
            pos[1] = 0;

            if (pos.DistanceXZ2(camPos) <= Square(maxDist))
            {
              // get cost as viewed by focused unit
              
              // typical friendly cost is something like 270
              // typical enemy cost is something like 9000
              float supCost = fld->GetCost(xx,zz,suppressTargets);
              if (supCost==0)
                continue;
              
              float safe = InterpolativC(supCost,0,27,1,0);
              float danger = InterpolativC(supCost,27,1000,0,1);
              Color color(danger,1-danger-safe,safe);

              pos[1] = GLOB_LAND->SurfaceYAboveWater(pos[0],pos[2]) + 0.2f;
              Ref<Object> rect;
              if (index<MapDiags.Size())
                rect = MapDiags[index];
              else
              {
                rect = new ObjectColored(rectShape, VISITOR_NO_ID);
                // scale the rectangle by the grid
                float size = rectShape->BoundingSphere() * H_SQRT2;
                rect->SetScale(OperItemGrid / size);
                MapDiags.Access(index);
                MapDiags[index]=rect;
              }
              index++;
              rect->SetPosition(pos);
              rect->SetConstantColor(color);
              DRAW_OBJ(rect);
            }
          }
        }
      }
#endif
    }
    
    if (CHECK_DIAG(DEPathFind))
    {
      AIBrain *unit = PilotUnit();
      if (unit)
      {
        const OperMap &map = unit->GetOperMap();
        map.Path3DDiags(unit);
      }
    }
    
  }
#endif
  base::DrawDiags();
}


class IsCombatUnitFunc : public ITurretFunc
{
public:
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    bool found = context._weapons->ForEachMagazine([](Magazine *magazine) -> bool
    {
      return true; // some magazine found
    });
    if (found) return true;

    if (!context._weapons->HasWeapons()) return false;
    // we do not have magazines, but we have some magazine slots
    // check if we have some real weapon
    return context._weapons->ForEachMuzzle([](const WeaponModePtr &ptr) -> bool
    {
      const MuzzleType *muzzle = ptr.GetMuzzleType();
      if (muzzle->_magazines.Size() > 0) return true; // found
  
      return false; // continue
    });
  }
};

/*!
\patch 1.17 Date 8/14/2001 by Ondra.
- Fixed: AI not attacking on weapon-less targets.
*/

bool EntityAIFull::IsCombatUnit() const
{
  IsCombatUnitFunc func;
  return ForEachTurret(func);
}

SuppressState EntityAIFull::CheckSuppressiveFire() const
{
  if (Glob.time<_suppressUntil) return SuppressIntense;
  return _useSuppressiveFire;
}

CombatMode EntityAIFull::GetCombatModeAutoDetected(const AIBrain*brain) const
{
  return CMSafe;
}

void EntityAI::ClassCleanUp()
{
  RecoilFunctions.Clear();
}

bool WeaponModePtr::IsValid() const
{
  return _weapon && _muzzle >= 0 && _mode >= 0;
}

bool WeaponModePtr::operator ==(const WeaponModePtr &with) const
{
  return with._weapon == _weapon && with._muzzle == _muzzle && with._mode == _mode;
}

bool WeaponModePtr::operator !=(const WeaponModePtr &with) const
{
  return with._weapon != _weapon || with._muzzle != _muzzle || with._mode != _mode;
}

const WeaponType *WeaponModePtr::GetWeaponType() const
{
  if (!_weapon) return NULL;
  return _weapon->Type();
}

const MuzzleType *WeaponModePtr::GetMuzzleType() const
{
  if (_muzzle < 0) return NULL;

  if (!_weapon) return NULL;
  const WeaponType *weaponType = _weapon->Type();
  if (!weaponType) return NULL;
  DoAssert(_muzzle < weaponType->_muzzles.Size());
  return weaponType->_muzzles[_muzzle];
}

const WeaponModeType *WeaponModePtr::GetWeaponModeType() const
{
  if (_muzzle < 0) return NULL;
  if (_mode < 0) return NULL;

  if (!_weapon) return NULL;
  const WeaponType *weaponType = _weapon->Type();
  if (!weaponType) return NULL;
  DoAssert(_muzzle < weaponType->_muzzles.Size());
  const MuzzleType *muzzleType = weaponType->_muzzles[_muzzle];
  if (!muzzleType) return NULL;
  DoAssert(_mode < muzzleType->_modes.Size());
  return muzzleType->_modes[_mode];
}

Magazine *WeaponModePtr::GetMagazine()
{
  if (!_weapon) return NULL;
  return _weapon->GetMagazine(_muzzle);
}

const Magazine *WeaponModePtr::GetMagazine() const
{
  if (!_weapon) return NULL;
  return _weapon->GetMagazine(_muzzle);
}

const AmmoType *WeaponModePtr::GetAmmoType() const
{
  if (!_weapon) return NULL;
  const Magazine *magazine = _weapon->GetMagazine(_muzzle);
  if (!magazine) return NULL;
  const MagazineType *type = magazine->Type();
  return type ? type->_ammo : NULL;
}

const OpticsInfo *WeaponModePtr::GetOpticsInfo() const
{
  if (_muzzle < 0) return NULL;
  if (!_weapon) return NULL;

  const WeaponType *weaponType = _weapon->Type();
  if (!weaponType) return NULL;

  // preffer attached optics if present
  const OpticsInfo *info = _weapon->GetAttachedOpticsInfo();
  if (info) return info;

  DoAssert(_muzzle < weaponType->_muzzles.Size());
  const MuzzleType *type = weaponType->_muzzles[_muzzle];
  if (!type) return NULL;

  const Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  return &type->_opticsInfo[muzzle._opticsMode];
}

const float WeaponModePtr::SumDispersionModifiers(const EntityAIFull *agent) const
{
  const WeaponModeType *mode = GetWeaponModeType();
  float dispersion = 0;
  if (mode)
  {
    dispersion = mode->_dispersion;
    if (_weapon) dispersion += _weapon->SumDispersionModifiers(agent);
  }
  saturateMax(dispersion, 0); //no negative!
  return dispersion;
}

const float WeaponModePtr::SumDexterityModifiers(const EntityAIFull *agent) const
{
  const WeaponType *info = GetWeaponType();
  float dexterity = 0;
  if (info)
  {
    dexterity = info->_dexterity;
    if (_weapon) dexterity += _weapon->SumDexterityModifiers(agent);
  }
  saturateMax(dexterity, 0); //no negative!
  return dexterity;
}

const OpticsCameraInfo *WeaponModePtr::GetOpticsCameraInfo() const
{
  if (_muzzle < 0) return NULL;
  if (!_weapon) return NULL;

  const WeaponType *weaponType = _weapon->Type();
  if (!weaponType) return NULL;

  DoAssert(_muzzle < weaponType->_muzzles.Size());
  const MuzzleType *type = weaponType->_muzzles[_muzzle];
  if (!type) return NULL;
 
  // handle attached optics if present
  ItemOptics *optics = _weapon->GetAttachedOptics();
  if (optics)
  {
    const OpticsInfo *info = &optics->Type()->_opticInfo;
    if (info->_memoryPointCamera.IsEmpty())
      return &type->_opticsCameraInfo;
    // memoryPointCamera defined in the optics attachment
    LODShape *shape = weaponType->GetShape();
    if (!shape) return &type->_opticsCameraInfo;
    if (!optics->ValidateCameraParamsFor(shape))
    {
      type->InitCameraParams(*optics->Type()->_par, optics->GetShape(), optics->_opticsCameraInfo);
      // the optics->_opticsCameraInfo._cameraPos got from InitCameraParams is relative to optics shape by design decision (see https://projects.bistudio.com/dayz/Optics_Camera_position_loaded_from_Weapon_memory_LOD.txt)
      // find proxy containing our optics and use its Transform to find position relative to weapon
      InventorySlotId opticsSlotId = optics->Type()->GetInventorySlot();
      for (int i=0; weaponType->GetProxyCountForLevel(0); i++)
      {
        const ProxyObjectTyped &proxy = weaponType->GetProxyForLevel(0,i);
        if (!proxy.obj) continue;
        const EntityType *proxytype = proxy.obj->GetEntityType();
        if (!proxytype) continue;
        // Find what slot this proxy shows 
        InventorySlotId slotId = proxytype->GetInventorySlot();
        if (slotId==opticsSlotId)
        {
          Matrix4 trans;
          // note: shape is ignored
          LODShape *shape = NULL;
          Object *parentObject = NULL;
          /*Object *obj =*/_weapon->GetProxy(_weapon->GetRenderVisualStateAge(),shape,parentObject,0,trans,MIdentity,i);
          optics->_opticsCameraInfo._cameraPos = trans.FastTransform(optics->_opticsCameraInfo._cameraPos);
          /*
          // commented example of BUG follows, things are not such simple, as the shape can be REVERTED. Using GetProxy() above is example of coding it in "DRY" style
          optics->_opticsCameraInfo._cameraPos = proxy.GetTransform().FastTransform(optics->_opticsCameraInfo._cameraPos);
          */
          break; // we are done
        }
      }
    }
    return &optics->_opticsCameraInfo;
  }

  const Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  return &type->_opticsInfo[muzzle._opticsMode];
}

Muzzle &WeaponModePtr::GetMuzzle() const
{
  DoAssert(_weapon);
  DoAssert(_muzzle >= 0);
  return _weapon->GetMuzzle(_muzzle);
}

int WeaponModePtr::GetDiscreteIndex() const
{
  if (_muzzle < 0) return 0;
  if (!_weapon) return 0;

  const Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  return muzzle._discreteIndex;
}

int WeaponModePtr::GetDiscreteDistanceIndex() const
{
  if (_muzzle < 0) return 0;
  if (!_weapon) return 0;

  const Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  return muzzle._discreteDistanceIndex;
}

WeaponSounds *WeaponModePtr::GetSound() const
{
  if (_muzzle < 0) return NULL;
  if (!_weapon) return NULL;

  const Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  return unconst_cast(&muzzle._sound);
}

bool WeaponModePtr::ShowToPlayer() const
{
  const WeaponModeType *mode = GetWeaponModeType();
  return !mode || mode->_showToPlayer;
}

void WeaponModePtr::SetReloadMagazineProgress(float progress) const
{
  if (_muzzle < 0) return;
  if (!_weapon) return;

  Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  muzzle._reloadMagazineProgress = progress;
}

void WeaponModePtr::SetDrawBowProgress(float progress) const
{
  if (_muzzle < 0) return;
  if (!_weapon) return;

  Muzzle &muzzle = _weapon->GetMuzzle(_muzzle);
  muzzle._drawBowProgress = progress;
}

LSError WeaponModePtr::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeRef("weapon", _weapon, 1));
  CHECK(ar.Serialize("muzzle", _muzzle, 1, -1));
  CHECK(ar.Serialize("mode", _mode, 1, -1));
  return LSOK;
}

bool WeaponModePtr::IsDefaultValue(ParamArchive &ar) const
{
  return !_weapon && _muzzle == -1 && _mode == -1;
}

void WeaponModePtr::LoadDefaultValues(ParamArchive &ar)
{
  _weapon = NULL;
  _muzzle = -1;
  _mode = -1;
}

WeaponsState::WeaponsState(IWeaponsHolder &owner)
: _owner(owner),
  _mGunFireUntilFrame(0),
  _mGunFireTime(TIME_MIN),
  _mGunFirePhase(0),
  _lastShotTime(Glob.time-120)
{
#if _VBS3
  _forceFirePosition = VZero;
#endif
  _lastTrace = Glob.time;
}

void WeaponsState::Init(const WeaponsType &type, ParamEntryPar cfg, EntityAIFull *owner, bool createWeapons)
{
  // on remote clients, do not create magazines and weapons, but still initialize the parameters of effects
  if (createWeapons)
  {
    // Magazines
    int n = type._magazines.Size();
    for (int i=0; i<n; i++)
    {
      Magazine *magazine = dyn_cast<Magazine>(GWorld->NewVehicleWithID(type._magazines[i]));
      if (magazine)
      {
        magazine->NeedsReload();

        // add to the world
        GWorld->AddOutVehicle(magazine);
        magazine->SetMoveOutFlag();

        AddMagazine(owner, magazine);
      }
    }
 
    // Weapons
    n = type._weapons.Size();
    for (int i=0; i<n; i++)
    {
      if (type._weapons[i]==nullptr) continue;
      Weapon *weapon = dyn_cast<Weapon>(GWorld->NewVehicleWithID(type._weapons[i]));
      if (weapon)
      {
        // add to the world
        GWorld->AddOutVehicle(weapon);
        weapon->SetMoveOutFlag();

        AddWeapon(owner, weapon);
      }
    }

    if (owner)
      owner->AutoReloadAll(*this, true);
  }
  Compact();

  _mGunClouds.Load(cfg >> "MGunClouds");
  _gunClouds.Load(cfg >> "GunClouds");
  _gunFire.Load(cfg >> "GunFire");

  _lockingTarget = NULL;
  _aimedTarget = NULL;
}

void WeaponsState::Compact()
{
   _owner.Compact();
}

WeaponsState::~WeaponsState()
{
}

void WeaponsState::GetVisionModePars(VisionMode &vMode, int &flirIndex) const
{
  WeaponModePtr ptr = ValidatedCurrentWeapon();
  Assert(ptr.IsValid());
  const Muzzle &muzzle = ptr.GetMuzzle();

  const VisionModePars &vPars = muzzle._visionPars;
  vMode = vPars._visionMode;
  flirIndex = vPars._flirIndex;
}

bool WeaponsState::HasCurrentWeaponTi() const
{
  WeaponModePtr ptr = ValidatedCurrentWeapon();
  if (ptr.IsValid())
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (muzzle) return muzzle->HasTiVision();
  }

  return false;
}

bool WeaponsState::NextVisionMode()
{
  WeaponModePtr ptr = ValidatedCurrentWeapon();
  if (ptr.IsValid())
  {
    const MuzzleType *muzzleType = ptr.GetMuzzleType();
    Muzzle &muzzle = ptr.GetMuzzle();
    
    if (muzzleType && muzzleType->HasVisionModes(muzzle._opticsMode))
    {
      VisionModePars &vPars = muzzle._visionPars;
      // select next vision mode based on config optics params
      muzzleType->NextVisionMode(vPars._visionMode, vPars._flirIndex, muzzle._opticsMode);
      return true;
    }
  }

  return false;
}

bool WeaponsState::HasVisionModes() const
{
  WeaponModePtr ptr = ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return false;

  const MuzzleType *muzzleType = ptr.GetMuzzleType();
  const Muzzle &muzzle = ptr.GetMuzzle();

  if (muzzleType) return muzzleType->HasVisionModes(muzzle._opticsMode);
  return false;
}

int WeaponsState::VisionModesCount() const
{
  WeaponModePtr ptr = ValidatedCurrentWeapon();
  if (!ptr.IsValid()) return 0;

  const MuzzleType *muzzleType = ptr.GetMuzzleType();
  const Muzzle &muzzle = ptr.GetMuzzle();

  if (muzzleType) return muzzleType->VisionModesCount(muzzle._opticsMode);
  return 0;
}

bool WeaponsState::HasWeapons() const
{
  return _owner.ForEachWeapon([](Weapon *weapon) -> bool
  {
    const WeaponType *weaponType = weapon->Type();
    if (!weaponType) return false; // continue
    for (int j=0; j<weaponType->_muzzles.Size(); j++)
    {
      const MuzzleType *muzzleType = weaponType->_muzzles[j];
      if (muzzleType && muzzleType->_modes.Size() > 0) return true; // done
    }
    return false; // continue
  });
}

WeaponModePtr WeaponsState::ValidatedCurrentWeapon() const
{
  return _owner.GetCurrentWeapon();
}

WeaponModePtr WeaponsState::ValidateWeapon(const WeaponModePtr &ptr) const
{
  WeaponModePtr result;
  if (ptr.IsValid())
  {
    _owner.ForEachWeapon([&result, &ptr](Weapon *weapon) -> bool
    {
      if (ptr._weapon == weapon)
      {
        result = ptr;
        return true; // found
      }
      return false; // continue
    });
  }
  return result;
}

WeaponModePtr WeaponsState::FindValidWeapon(const WeaponModePtr &ptr) const
{
  if (ptr.IsValid()) return ptr;

  WeaponModePtr result;
  ForEachMuzzle([&result](const WeaponModePtr &ptr) -> bool
  {
    result = ptr;
    return true; // done
  });
  return result;
}

bool WeaponsState::IsDefaultWeapon(const WeaponModePtr &ptr) const
{
  if (ptr._mode != 0) return false; // default is the first mode

  bool isDefault = false;
  _owner.ForEachWeapon([&isDefault, &ptr](Weapon *weapon) -> bool
  {
    const WeaponType *weaponType = weapon->Type();
    if (!weaponType) return false; // continue
    for (int j=0; j<weaponType->_muzzles.Size(); j++)
    {
      const MuzzleType *muzzleType = weaponType->_muzzles[j];
      if (muzzleType && muzzleType->_modes.Size() > 0)
      {
        // the first valid weaspon found 
        isDefault = ptr._weapon == weapon && ptr._muzzle == j;
        return true; // done
      }
    }
    return false; // continue
  });
  return isDefault;
}

bool WeaponsState::ForEachMagazine(const std::function<bool(Magazine *)> &func)
{
  return _owner.ForEachMagazine(func);
}

bool WeaponsState::ForEachWeapon(const std::function<bool(Weapon *weapon)> &func)
{
  return _owner.ForEachWeapon(func);
}

bool WeaponsState::ForEachMuzzle(const std::function<bool(const WeaponModePtr &)> &func) const
{

  return _owner.ForEachWeapon([&func](const Weapon *weapon) -> bool
  {
    const WeaponType *weaponType = weapon->Type();
    if (!weaponType) return false; // continue
    
    WeaponModePtr iterator;
    iterator._weapon = unconst_cast(weapon);

    for (int j=0; j<weaponType->_muzzles.Size(); j++)
    {
      const MuzzleType *muzzleType = weaponType->_muzzles[j];
      if (!muzzleType) continue;

      iterator._muzzle = j;
      if (muzzleType->_modes.Size() > 0)
      {
        iterator._mode = 0;
        if (func(iterator)) return true; // done
      }
    }
    return false; // continue
  });
}

bool WeaponsState::ForEachWeaponMode(const std::function<bool(const WeaponModePtr &)> &func) const
{
  return _owner.ForEachWeapon([&func](const Weapon *weapon) -> bool
  {
    const WeaponType *weaponType = weapon->Type();
    if (!weaponType) return false; // continue
    
    WeaponModePtr iterator;
    iterator._weapon = unconst_cast(weapon);
    for (int j=0; j<weaponType->_muzzles.Size(); j++)
    {
      const MuzzleType *muzzleType = weaponType->_muzzles[j];
      if (!muzzleType) continue;

      iterator._muzzle = j;
      for (int k=0; k<muzzleType->_modes.Size(); k++)
      {
        iterator._mode = k;
        if (func(iterator)) return true; // done
      }
    }
    return false; // continue
  });
}


bool WeaponsState::CreateWeapon(EntityAIFull *entity, RStringB name, bool reload, bool checkSelected)
{
  Weapon *weapon = dyn_cast<Weapon>(GWorld->NewVehicleWithID(name));
  if (!weapon) return false;

  // add to the world
  GWorld->AddOutVehicle(weapon);
  weapon->SetMoveOutFlag();

  bool ok = AddWeapon(entity, weapon, reload, checkSelected);

  // create network object
  if (GWorld->GetMode() == GModeNetware)
    GetNetworkManager().CreateVehicle(weapon, VLTOut, "", -1);

  return ok;
}

bool WeaponsState::AddWeapon(EntityAIFull *entity, Weapon *weapon, bool reload, bool checkSelected)
{
  DoAssert(weapon);
  const WeaponType *type = weapon->Type();
  if (!type) return false;
  if (!CheckAccessCreate(*type->_par)) return false;

  // test whether the same weapon is already added before
  if (entity && !entity->IsDeadSet()) // dead soldiers can be used as weapon containers
  {
    // TODO: later duplicite weapons will be enabled
    bool found = _owner.ForEachWeapon([entity, type](const Weapon *w) -> bool
    {
      const WeaponType *t = w->Type();
      if (t && t == type)
      {
        RptF("Duplicate weapon %s detected for %s", cc_cast(type->GetName()), entity->GetName());
        return true; // found
      }
      return false; // continue
    });
    if (found) return false;
  }

  if (!_owner.AddWeapon(entity, weapon)) return false;
  
  if (reload)
  {
    // reload magazines
    for (int j=0; j<type->_muzzles.Size(); j++)
    {
      MuzzleType *muzzleType = type->_muzzles[j];
      Magazine *magazine = FindMagazineByType(muzzleType);
      if (magazine)
      {
        // move magazine to the weapon
        _owner.RemoveMagazine(magazine);
        Muzzle &muzzle = weapon->GetMuzzle(j);
        muzzle._magazineWanted = magazine;
        // set the parent
        magazine->ItemMoveOut(weapon);
      }
    }
  }

  if (checkSelected) entity->OnWeaponAdded();
  return true;
}

void WeaponsState::DeleteWeapon(EntityAIFull *entity, RStringB name, bool checkSelected)
{
  if (!_owner.DeleteWeapon(name)) return;

  if (checkSelected)
  {
    entity->OnWeaponRemoved();
    // TODO: move to OnWeaponRemoved
    SelectWeapon(entity, FirstWeapon(entity), true);
    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == entity)
      GWorld->UI()->ResetVehicle(entity);
  }
}

void WeaponsState::RemoveWeapon(EntityAIFull *entity, Weapon *weapon, bool checkSelected)
{
  if (!weapon) return;

  if (!_owner.RemoveWeapon(weapon)) return; // not found
  
  if (ValidatedCurrentWeapon()._weapon == weapon)
  {
    // current weapon was removed
    SetCurrentWeapon(FirstWeapon(entity));
  }

  if (checkSelected)
  {
    entity->OnWeaponRemoved();
    // TODO: move to OnWeaponRemoved
    SelectWeapon(entity, FirstWeapon(entity), true);
    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == entity)
      GWorld->UI()->ResetVehicle(entity);
  }
}

void WeaponsState::DeleteAllWeapons(EntityAIFull *entity, bool removeItems)
{
  if (removeItems) _owner.DeleteAllWeapons();
  else _owner.DeleteWeapons([](const Weapon *weapon) -> bool
  {
    const WeaponType *type = weapon->Type();
    return !type || (type->_weaponType & MaskSlotInventory) == 0; // delete if have no type or if not in the inventory slot
  });

  entity->OnWeaponRemoved();
  SelectWeapon(entity, WeaponModePtr(), true);
  _forceFireWeapon = WeaponModePtr();
  if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == entity)
    GWorld->UI()->ResetVehicle(entity);
}

bool WeaponsState::CreateMagazine(EntityAIFull *entity, RStringB name)
{
  Magazine *magazine = dyn_cast<Magazine>(GWorld->NewVehicleWithID(name));
  if (!magazine) return false;

  magazine->NeedsReload();

  // add to the world
  GWorld->AddOutVehicle(magazine);
  magazine->SetMoveOutFlag();

  bool ok = AddMagazine(entity, magazine);

  // create network object
  if (GWorld->GetMode() == GModeNetware)
    GetNetworkManager().CreateVehicle(magazine, VLTOut, "", -1);

  return ok;
}

bool WeaponsState::CreateMagazineAmmoCount(EntityAIFull *entity, RStringB name, int ammoCount)
{
  Magazine *magazine = dyn_cast<Magazine>(GWorld->NewVehicleWithID(name));
  if (!magazine) return false;

  magazine->NeedsReload();
  saturate(ammoCount, 0, magazine->GetAmmo());
  magazine->SetAmmo(ammoCount);

  // add to the world
  GWorld->AddOutVehicle(magazine);
  magazine->SetMoveOutFlag();

  bool ok = AddMagazine(entity, magazine);

  // create network object
  if (GWorld->GetMode() == GModeNetware)
    GetNetworkManager().CreateVehicle(magazine, VLTOut, "", -1);

  return ok;
}

bool WeaponsState::AddMagazine(EntityAIFull *entity, Magazine *magazine, bool autoload)
{
  if (!CheckAccessCreate(*magazine->Type()->_par))
    return false;

  if (!_owner.AddMagazine(entity, magazine)) return false;

  if (autoload)
  {
    ForEachMuzzle([entity, magazine, this](const WeaponModePtr &ptr) -> bool
    {
      const MuzzleType *muzzleType = ptr.GetMuzzleType();
      if (!muzzleType) return false; // continue

      Muzzle &muzzle = ptr.GetMuzzle();
      // if not empty, there is no need to reload
      if (muzzle._magazine && muzzle._magazine->GetAmmo() > 0) return false; // continue

      for (int k=0; k<muzzleType->_magazines.Size(); k++)
        if (muzzleType->_magazines[k] == magazine->Type())
        {
          entity->AutoReload(*this, ptr, true, false);
          return true; // done
        }
      return false; // continue
    });
  }
#if _VBS3
  entity->OnEvent(EELoadOutChanged);
#endif
  return true;
}

void WeaponsState::DeleteMagazine(EntityAIFull *entity, RStringB name)
{
  if (!_owner.DeleteMagazine(name)) return;

#if _VBS3
  entity->OnEvent(EELoadOutChanged);
#endif
}

void WeaponsState::RemoveMagazine(EntityAIFull *entity, Magazine *magazine)
{
  if (!_owner.RemoveMagazine(magazine)) return; // not found
}

void WeaponsState::DeleteMagazines(EntityAIFull *entity, RStringB name)
{
  _owner.DeleteMagazines([name](const Magazine *magazine) -> bool
  {
    const MagazineType *type = magazine->Type();
    return !type || stricmp(type->GetName(), name) == 0;
  });
}

void WeaponsState::DeleteAllMagazines(EntityAIFull *entity)
{
  _owner.DeleteAllMagazines();
}

void WeaponsState::SetCurrentWeapon(const WeaponModePtr &ptr)
{
  _owner.SetCurrentWeapon(ptr);
}

void WeaponsState::SelectWeapon(EntityAIFull *entity, const WeaponModePtr &ptr, bool changed)
{
  WeaponModePtr current = ValidatedCurrentWeapon();
  if (!changed && current == ptr) return;
  
  if (current.IsValid())
  {
    // safety patch:
    // if new weapon is selected, cancel rest of the burst
    Magazine *magazine = current.GetMagazine();
    if (magazine) magazine->_burstLeft = 0;
  }

  SetCurrentWeapon(ptr);
}

void WeaponsState::SelectCounterMeasures(const EntityAIFull *entity, const WeaponModePtr &counterMeasures, bool changed)
{
  WeaponModePtr current = ValidatedCurrentCounterMeasures();
  if (!changed && _currentCounterMeasures == counterMeasures) return;

  if (current.IsValid())
  {
    // safety patch:
    // if new weapon is selected, cancel rest of the burst
    Magazine *magazine = current.GetMagazine();
    if (magazine) magazine->_burstLeft = 0;
  }

  _currentCounterMeasures = counterMeasures;
}

/*!
\param muzzle reloaded muzzle
\param oldMagazineType preferred magazine type
\return index of magazine if found, otherwise -1
*/
Magazine *WeaponsState::FindMagazineByType(const MuzzleType *muzzle, const MagazineType *oldMagazineType)
{
  // search for reserve magazine
  // first search the same type as old magazine
  if (oldMagazineType)
  {
    int ammoMax = 0;
    Magazine *best = NULL;
    ForEachMagazine([&ammoMax, &best, oldMagazineType](Magazine *magazine) -> bool
    {
      if (magazine && magazine->Type() == oldMagazineType && magazine->GetAmmo() > ammoMax)
      {
        ammoMax = magazine->GetAmmo();
        best = magazine;
      }
      return false; // continue
    });
    if (best) return best;
  }
  // then other magazines fits into muzzle
  for (int i=0; i<muzzle->_magazines.Size(); i++)
  {
    const MagazineType *type = muzzle->_magazines[i];
    if (type == oldMagazineType) continue;
    int ammoMax = 0;
    Magazine *best = NULL;
    ForEachMagazine([&ammoMax, &best, type](Magazine *magazine) -> bool
    {
      if (magazine && magazine->Type() == type && magazine->GetAmmo() > ammoMax)
      {
        ammoMax = magazine->GetAmmo();
        best = magazine;
      }
      return false; // continue
    });
    if (best) return best;
  }
  return NULL;
}

WeaponModePtr WeaponsState::FirstWeapon(const EntityAIFull *entity) const
{
  int maxPrimary = MaxPrimaryLevel();
  
  WeaponModePtr result;

  // check max. primary level available
  // try to find primary weapon with max level
  ForEachMuzzle([&result, maxPrimary, this, entity](const WeaponModePtr &ptr) -> bool
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (muzzle->_primary < maxPrimary) return false; // continue
    
    if (!ptr.ShowToPlayer()) return false; // continue

    const WeaponType *weapon = ptr.GetWeaponType();
    if (weapon->GetSimulation() == WSCMLauncher) return false; // continue
    
    if (!muzzle->_showEmpty && EmptySlot(ptr)) return false; // continue

    result = ptr;
    return true; // done
  });
  if (result.IsValid()) return result;

  // find any weapon with max level
  ForEachMuzzle([&result, maxPrimary, this, entity](const WeaponModePtr &ptr) -> bool
  {
    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (muzzle->_primary < maxPrimary) return false; // continue
    
    if (!ptr.ShowToPlayer()) return false; // continue

    const WeaponType *weapon = ptr.GetWeaponType();
    if (weapon->GetSimulation() == WSCMLauncher) return false; // continue
    
    if (!muzzle->_showEmpty && EmptySlot(ptr)) return false; // continue

    result = ptr;
    return true; // done
  });
  return result;
}

static void InitZoom(CameraType camTypeAct, float &zoom, Object* object, const ViewPars *viewPars)
{
  float dummyHead=0, dummyDive=0;
  if (viewPars)
    viewPars->InitVirtual(camTypeAct, dummyHead, dummyDive, zoom);
  else 
    object->InitVirtual(camTypeAct, dummyHead, dummyDive, zoom);
}


WeaponModePtr WeaponsState::NextWeapon(const EntityAIFull *entity, const WeaponModePtr &start) const
{
  // check max. primary level
  int maxPrimary = MaxPrimaryLevel();

  if (start.IsValid())
  {
    const MuzzleType *muzzle = start.GetMuzzleType();
    if (muzzle && muzzle->_primary < maxPrimary)
      return FirstWeapon(entity);
  }

  bool handleFirst = true;
  bool handleBest = false;

  WeaponModePtr first, best;

  ForEachWeaponMode([&start, &handleFirst, &handleBest, maxPrimary, this, entity, &first, &best](const WeaponModePtr &ptr) -> bool
  {
    bool selected = ptr == start;
    if (selected) handleBest = true;
    // if the first already found, wait for the next after start
    if (!handleFirst && !(handleBest && !selected)) return false; // continue

    const MuzzleType *muzzle = ptr.GetMuzzleType();
    if (muzzle->_primary < maxPrimary) return false; // continue
    
    if (!ptr.ShowToPlayer()) return false; // continue

    const WeaponType *weapon = ptr.GetWeaponType();
    if (weapon->GetSimulation() == WSCMLauncher) return false; // continue
    
    if (!muzzle->_showEmpty && EmptySlot(ptr)) return false; // continue

    if (handleBest && !selected)
    {
      best = ptr;
      return true; // done
    }
    if (handleFirst)
    {
      first = ptr;
      handleFirst = false; // the first found, continue to try to find the best
    }

    return false;
  });

  if (best.IsValid()) return best;
  return first;
}

WeaponModePtr WeaponsState::NextWeaponMode(const EntityAIFull *entity, const WeaponModePtr &start) const
{
  const WeaponType *weapon = start.GetWeaponType();
  if (!weapon) return WeaponModePtr();

  bool handleFirst = true;
  bool handleBest = false;

  WeaponModePtr first;

  WeaponModePtr ptr; ptr._weapon = start._weapon;
  for (int i=0; i<weapon->_muzzles.Size(); i++)
  {
    const MuzzleType *muzzle = weapon->_muzzles[i];
    ptr._muzzle = i;
    for (int j=0; j<muzzle->_modes.Size(); j++)
    {
      ptr._mode = j;

      bool selected = ptr == start;
      if (selected) handleBest = true;
      // if the first already found, wait for the next after start
      if (!(handleFirst || (handleBest && !selected))) continue;

      if (!ptr.ShowToPlayer()) continue;
      if (!muzzle->_showEmpty && EmptySlot(ptr)) continue;

      if (handleBest && !selected)
      {
        return ptr; // best found
      }
      if (handleFirst)
      {
        first = ptr;
        handleFirst = false; // the first found, continue to try to find the best
      }
    }
  }

  // best not found, return the first
  return first;
}

void WeaponsState::NextOpticsMode(Person *person)
{
  WeaponModePtr ptr = ValidatedCurrentWeapon();
  if (ptr.IsValid())
  {
    const MuzzleType *muzzleType = ptr.GetMuzzleType();
    if (!muzzleType || muzzleType->_opticsInfo.Size() <= 1) return;

    Muzzle &muzzle = ptr.GetMuzzle();
    muzzle._opticsMode = ((muzzle._opticsMode + 1) % muzzleType->_opticsInfo.Size());
    const OpticsInfo &opticsInfo = muzzleType->_opticsInfo[muzzle._opticsMode];

    //reset fov
    muzzle._discreteIndex = opticsInfo._discreteInitIndex;
    muzzle._discreteDistanceIndex = opticsInfo._discreteDistanceInitIndex;
    //reset vision mode, if needed
    if (muzzleType->HasVisionModes(muzzle._opticsMode))
    {
      bool allowed = false;
      VisionModePars &vPars = muzzle._visionPars;
      //check if current vision mode is enabled
      for (int i=0; i<opticsInfo._visionModes.Size(); i++)
      {
        if (opticsInfo._visionModes[i] == vPars._visionMode) 
        { //check flir mode
          if(vPars._visionMode == VMTi)
          {
            for (int j=0; j<opticsInfo._thermalModes.Size(); j++)
            {
              if (opticsInfo._thermalModes[j] == vPars._flirIndex) 
              { 
                allowed = true;
                break;
              }
            }
          }
          else allowed = true;
        }
      }
      if(!allowed)
      {
        muzzleType->GetInitVisionMode(vPars._visionMode, vPars._flirIndex, muzzle._opticsMode);
      }
    }

    float initFOV;
    CameraType camTypeAct = GWorld->GetCameraType();
    InitZoom(camTypeAct, initFOV, person, NULL);

    Object::Zoom *zoom = person->GetZoom();
    zoom->Init(initFOV);
  }
}

WeaponModePtr WeaponsState::FirstCM(const EntityAIFull *entity) 
{
  WeaponModePtr ptr;
  ForEachMuzzle([&ptr, this, entity](const WeaponModePtr &p) -> bool
  {
    if (!p.ShowToPlayer()) return false; // continue

    const WeaponType *type = p.GetWeaponType();
    const Magazine *magazine = p.GetMagazine();
    if (magazine && type->GetSimulation() == WSCMLauncher)
    {
      SelectCounterMeasures(entity, p);
      ptr = p;
      return true; // found
    }
    return false; // continue
  });
  return ptr;
}

WeaponModePtr WeaponsState::NextCM(const EntityAIFull *entity)
{
  bool handleFirst = true;
  bool handleBest = false;

  WeaponModePtr first, best;
  WeaponModePtr start = _currentCounterMeasures;

  ForEachWeaponMode([&start, &handleFirst, &handleBest, &first, &best](const WeaponModePtr &ptr) -> bool
  {
    bool selected = ptr == start;
    if (selected) handleBest = true;
    // if the first already found, wait for the next after start
    if (!handleFirst && !(handleBest && !selected)) return false; // continue

    if (!ptr.ShowToPlayer()) return false; // continue

    const WeaponType *weapon = ptr.GetWeaponType();
    if (weapon->GetSimulation() != WSCMLauncher) return false; // continue

    if (!ptr.GetMagazine()) return false; // continue
    
    if (handleBest && !selected)
    {
      best = ptr;
      return true; // done
    }
    if (handleFirst)
    {
      first = ptr;
      handleFirst = false; // the first found, continue to try to find the best
    }

    return false;
  });

  if (best.IsValid()) return best;
  if (first.IsValid()) return first;
  return start;
}

bool WeaponsState::ShowFire() const
{
  return Glob.frameID <= _mGunFireUntilFrame || Glob.time < _mGunFireTime + 0.05f;
}

bool WeaponsState::EmptySlot(const WeaponModePtr &ptr) const
{
  const Magazine *magazine = ptr.GetMagazine();
  if (!magazine)
    return true;
  if (magazine->GetAmmo() > 0)
    return false;
  const MagazineType *type = magazine->Type();
  if (!type)
    return true;
  if (!type->_ammo)
    return false;

  return !_owner.ForEachMagazine([magazine](Magazine *reserve) -> bool
  {
    if (reserve == magazine) return false; // continue
    if (reserve->Type() != magazine->Type()) return false; // continue
    if (reserve->GetAmmo() > 0) return true; // found
    return false; // continue
  });
}

int WeaponsState::MaxPrimaryLevel() const
{
  int maxPrimary = 0;
  ForEachMuzzle([&maxPrimary, this](const WeaponModePtr &ptr) -> bool
  {
    if (!ptr.ShowToPlayer()) return false; // continue
    if (ptr.GetWeaponType()->GetSimulation() == WSCMLauncher) return false; // continue
    if (!ptr.GetMuzzleType()->_showEmpty && EmptySlot(ptr)) return false; // continue
    const Magazine *magazine = ptr.GetMagazine();
    if (!magazine) return false; // continue
    if (magazine->GetAmmo() <= 0) return false; // continue
    saturateMax(maxPrimary, ptr.GetMuzzleType()->_primary);
    return false; // continue
  });
  if (maxPrimary > 0) return maxPrimary;

  // check max. primary level of all weapons
  ForEachMuzzle([&maxPrimary, this](const WeaponModePtr &ptr) -> bool
  {
    if (!ptr.ShowToPlayer()) return false; // continue
    if (ptr.GetWeaponType()->GetSimulation() == WSCMLauncher) return false; // continue
    if (!ptr.GetMuzzleType()->_showEmpty && EmptySlot(ptr)) return false; // continue
    saturateMax(maxPrimary, ptr.GetMuzzleType()->_primary);
    return false; // continue
  });
  return maxPrimary;
}

/*!
\param type given magazine type
\param ammo minimal amount of ammo magazine must contain
\return index of magazine in array or -1 if not found
*/
Magazine *WeaponsState::FindBestMagazine(const MagazineType *type, int ammo)
{
  Magazine *best = NULL;
  ForEachMagazine([&best, type, &ammo](Magazine *reserve) -> bool
  {
    if (reserve->Type() != type) return false; // continue
    if (reserve->GetAmmo() > ammo)
    {
      ammo = reserve->GetAmmo();
      best = reserve;
    }
    return false; // continue
  });
  return best;
}

LSError WeaponsState::Serialize(EntityAIFull *owner, ParamArchive &ar)
{
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    CHECK(ar.SerializeDef("forceFireWeapon", _forceFireWeapon, 1))
    CHECK(ar.SerializeDef("gunFire", _gunFire, 1))
    CHECK(ar.SerializeDef("gunClouds", _gunClouds, 1))
    CHECK(ar.SerializeDef("mGunFire", _mGunFire, 1))
    CHECK(ar.SerializeDef("mGunClouds", _mGunClouds, 1))
    CHECK(ar.Serialize("mGunFireUntilFrame", _mGunFireUntilFrame, 1, 0))
    CHECK(::Serialize(ar,"mGunFireTime", _mGunFireTime, 1, TIME_MIN))
    CHECK(ar.Serialize("mGunFirePhase", _mGunFirePhase, 1, 0))

    CHECK(::Serialize(ar, "lastShotTime", _lastShotTime, 1, Time(0)))
    CHECK(ar.SerializeRef("lastShot", _lastShot, 1))
  }

  // serialize weapons
  if (ar.GetArVersion() >= 7)
  {
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
      CHECK(ar.SerializeDef("currentCounterMeasures", _currentCounterMeasures, 1))   
    }
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
      if (!ValidatedCurrentWeapon().IsValid()) SetCurrentWeapon(FirstWeapon(owner));
      /* Call to owner-AutoReloadAll removed, as we suppose it is a relict trying to ensure AI have their guns reloaded after Load.
         It is now obsolete as animations are serialized. (see news:gu11b2$hih$1@new-server.localdomain)
      */
      // owner->AutoReloadAll(*this, false);
    }
  }

  return LSOK;
}

void EntityAI::UpdateSnappedPosition()
{
  // update snappedPosition
  float gridDist = GetType()->GetTypSpeed() * 5.0f/3.6f; //distance using typical speed after 5 seconds
  saturate(gridDist, 20.0f, 500.0f);
  const float gridDist2 = gridDist*gridDist;
  if (FutureVisualState().Position().Distance2(_snappedPosition)>gridDist2)
  {
    _snappedPosition = (FutureVisualState().Position() + Vector3(gridDist/2, gridDist/2, gridDist/2));
    _snappedPosition = _snappedPosition - Vector3(fmod(_snappedPosition.X(),gridDist), fmod(_snappedPosition.Y(),gridDist), fmod(_snappedPosition.Z(),gridDist));
  }
}

void EntityAI::UpdateLights()
{ // TODO: this may be called from multiple attachments. Make sure we do it once per frame only
  // animate position
  _lights.AnimatePosition(this, GetType()->_lights);

  // switch on / off
  _lights.Switch(IsLightOn(FutureVisualState()), this, GetType()->_lights);

  // Update the lights after changes
  _lights.UpdateAggLights(this);
}

void EntityAI::UpdatePosition()
{
  UpdateLights();
}

// WIP:BALLISTICS_COMPUTER:BEGIN
bool BallisticsComputer::SetParams(const EntityAIFull *shooter, const TurretContext &context, Target *target)
{
  _shooter = shooter;
  _context = context;
  _target = target;
  if (!_shooter || !_target || !_context._turret)
    return false;
  const WeaponsState *weapons = _context._weapons;
  if (!weapons)
    return false;
  _weapon = weapons->ValidatedCurrentWeapon();
  if (!_weapon.IsValid())
  {
    if (!weapons->HasWeapons()) return false;
    _weapon = weapons->FindValidWeapon(_weapon);
  }
  if (!_shooter->CheckTargetKnown(_context, _target))
    return false;
  const MuzzleType *muzzle = _weapon.GetMuzzleType();
  if (!muzzle || muzzle->_ballisticsComputer != 1)
    return false;
  const Magazine *magazine = _weapon.GetMagazine();
  if (!magazine)
    return false;
  _aInfo = magazine->Type();
  if (!_aInfo)
    return false;
  _ammo = _aInfo->_ammo;
  if (!_ammo)
    return false;
  return true;
}

bool BallisticsComputer::SimulateBallistics(Vector3Par relPos, Vector3Par relSpeed, const MagazineType *ammoInfo, float& time, Vector3& correction)
{
  // rotate so the shot is aligned with the X axis
  float angle = -atan2(relPos.Z(), relPos.X());
  Matrix3 aimRot(MRotationY, angle);
  Vector3 pos = aimRot * relPos;
  Vector3 speed = aimRot * relSpeed;
  Vector3 shotSpeed = pos.Normalized() * ammoInfo->_initSpeed;
  ///LogF("rotation by %.1f deg -> relPos = (%.2f, %.2f, %.2f), relSpeed = (%.2f, %.2f, %.2f), shotSpeed = (%.2f, %.2f, %.2f), ideal time = %.3f", angle * 180.0f / H_PI, pos.X(), pos.Y(), pos.Z(), speed.X(), speed.Y(), speed.Z(), shotSpeed.X(), shotSpeed.Y(), shotSpeed.Z(), pos.X() / shotSpeed.X());

  const float dist = pos.X();
  float posX = -pos.X();
  float posY = -pos.Y();
  float speedX = shotSpeed.X();
  float speedY = shotSpeed.Y();

  // just to be sure
  Assert (fabs(pos.Z()) < 1e-3);
  Assert (fabs(shotSpeed.Z()) < 1e-3);
  Assert((posX < 0.0f) && (speedX > 0.0f));

  const float relSpeedX = speed.X();
  const float relSpeedY = speed.Y();
  const float relSpeedZ = speed.Z();
  const AmmoType *_ammo = ammoInfo->_ammo;
  const float fCoef = _ammo->_airFriction;
  const float gAcc = _ammo->_coefGravity * G_CONST;
  // conditions for stopping the simulation (we assume at 1% of original velocity we are not interested at all)
  const float stopTime = _ammo->_timeToLive;
  const float stopSpeed = speedX * 0.01f;
  // maximum simulation time step
  // const float maxDeltaT = 0.01f; FIXME RHAL: use smaller delta for improved simulation (or use time step dependent on the distance and/or speed?)
  const float maxDeltaT = 0.1f;

  time = 0.0f;
  bool terminate = false;
  while(!terminate)
  {
    // should the simulation be stopped?
    if ((time > stopTime) || (speedX < stopSpeed))
    {  // target cannot be hit
      correction = Vector3(posX, posY, dist);
      return false;
    }
    // update time (note that delta should be exact in the last step)
    terminate = speedX * maxDeltaT > -posX;
    float deltaT = terminate ? -posX / speedX : maxDeltaT;
    time += deltaT;
    // update pos
    posX += (speedX - relSpeedX) * deltaT;
    posY += (speedY - relSpeedY) * deltaT;
    // get current friction
    float speed = hypot(speedX, speedY);
    // this is speed^2 * f * sin(a)
    float fAccX = speed * fCoef * speedX;
    // and this one speed^2 * f * cos(a)
    float fAccY = speed * fCoef * speedY;
    // update speed
    speedX += fAccX * deltaT;
    speedY += (fAccY - gAcc) * deltaT;
  }
  correction = Vector3(relSpeedZ * time, posY, dist);
  return true;
}

bool BallisticsComputer::Compute(const EntityAIFull *shooter, const TurretContext &context, Target *target, bool exact)
{
  if (!SetParams(shooter, context, target))
  { 
    _status = BCinvalid;
    return false;
  }

  // position of the shooter (in world coords)
  _weaponPos = _shooter->FutureVisualState().PositionModelToWorld(_shooter->GetWeaponPoint(_shooter->FutureVisualState(),_context, _weapon));
  // estimate target info
  _shooter->GetAimingPosition(_targetPos, _targetSpeed, _seenBefore, exact, _context, _target, _ammo);
  // simulate ballistics
  float time;
  Vector3 correction;
  bool canHit = SimulateBallistics(_targetPos - _weaponPos, _targetSpeed - _shooter->FutureVisualState().Speed(), _aInfo, time, correction);
#if 1
  if (canHit)
  { // on big angle differences, assume that we can't hit the target at all
    float tana = -correction.Y() / correction.Z();
    ///LogF("tana = %.2f, angle = %.0f", tana, atan2(-correction.Y(), correction.Z()) * 180.0f / H_PI);
    if (fabs(tana) > 0.1)
      canHit = false;
  }
#endif
  if (!canHit)
  { // target cannot be hit
    _status = BCready;
    ///LogF("BC: cannot hit!");
    return false;
  }
  _time = time;
  _targetCorrection = correction;
  _status = BCupdated;
  ///LogF("BC: can hit at %.3f sec with correction (%.3f, %.3f, %.3f) (angle %.1f deg)", _time, _targetCorrection.X(), _targetCorrection.Y(), _targetCorrection.Z(), atan2(_targetCorrection.Y(), _targetCorrection.Z()) * 180.0f / H_PI);
  return true;
}

Matrix3 BallisticsComputer::ViewCorrection() const
{
  if ((_status == BCinvalid) || (_status == BCready))
    return M3Identity;
  return Matrix3(MDirection, _targetCorrection, VUp);
}


// WIP:BALLISTICS_COMPUTER:END

void EventHandler::Process(const GameValue &pars) const
{
  GameVarSpace local(false);
  if (!GWorld) return;
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&local);
  gstate->VarSetLocal("_this",pars,true);
  gstate->Execute(_handler, GameState::EvalContext::_reportUndefined, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);
}

GameValue EventHandler::ProcessRet(const GameValue &pars) const
{
  GameVarSpace local(false);
  if (!GWorld) return NOTHING;
  GameState *gstate = GWorld->GetGameState();
   int gshandle = gstate->BeginContext(&local);
  gstate->VarSetLocal("_this",pars,true);
  GameValue result = gstate->EvaluateMultiple(_handler, GameState::EvalContext::_reportUndefined, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);
  return result;
}

LSError EventHandler::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("id", _id, 1, 0))
  CHECK(ar.Serialize("handler", _handler, 1, ""))
  return LSOK;
}

LSError EventHandlers::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("nextid", _nextId, 1, 0))
  CHECK(ar.Serialize("handlers", _handlers, 1))
  return LSOK;
}

bool EventHandlers::IsDefaultValue(ParamArchive &ar)
{
  return (!_handlers.Size()); //no handler was added or no handler remains
}

int EventHandlers::Add(RString expression)
{
  int id = _nextId++;
  _handlers.Add(EventHandler(id, expression));
  return id;
}

void EventHandlers::Delete(int id)
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

void EventHandlers::Clear()
{
  _handlers.Clear();
}

void EventHandlers::Process(const GameValue &pars) const
{
  for (int i=0; i<_handlers.Size(); i++)
  {
    _handlers[i].Process(pars);
  }
}

GameValue EventHandlers::ProcessRet(const GameValue &pars) const
{
  GameValue last;
  for (int i=0; i<_handlers.Size(); i++)
  {
    last = _handlers[i].ProcessRet(pars);
  }
  return last;
}


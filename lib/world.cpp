/*!
  \file
  Implementation of World class
*/

#include "wpch.hpp"
#include "dikCodes.h"

#include "world.hpp"
#include "scene.hpp"
#include "engine.hpp"
#include "keyInput.hpp"
#include "lights.hpp"
#include "speaker.hpp"
#include "integrity.hpp"
#include <Es/Containers/forEach.hpp>
#include <Es/Containers/offTree.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/Files/fileContainer.hpp>
#include <El/SimpleExpression/simpleExpression.hpp>
#include <El/DataSignatures/dataSignatures.hpp>
#include <lib/Inventory/transmitter.hpp>
#include <lib/Network/voiceXA2.hpp>

#if _ENABLE_PHYSICS
#include "Physics\enf_physicsbulletinternal.h"
#include "BulletCollision/CollisionShapes/btBoxShape.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#endif

#include "versionNo.h"
#include "baseWorld/enf_shapes.h"

#if _VBS2 && !_VBS2_LITE
  #include "UI/dispMissionEditor.hpp"
  #include "HLA/Tracking.hpp"
#endif

#if _HLA
  #include "hla/HLA_base.hpp"
#endif

#ifdef _WIN32
  #include "joystick.hpp"
#endif

#include "landscape.hpp"
#include "visibility.hpp"
#include <El/Common/randomGen.hpp>
#include "camEffects.hpp"
#include "titEffects.hpp"
#include "timeManager.hpp"
#include "scripts.hpp"
#include <El/Debugging/debugTrap.hpp>
#include <El/HiResTime/hiResTime.hpp>
#include "txtPreload.hpp"
#include "Shape/material.hpp"
#include "Protect/selectProtection.h"
#include "oggtext.h"

#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include "diagModes.hpp"

#include "progress.hpp"
#include "fileLocator.hpp"

#include "allVehicles.hpp"
#include "seaGull.hpp"
#include "camera.hpp"

#include <El/Evaluator/express.hpp>
#include "gameStateExt.hpp"
#if USE_PRECOMPILATION
#include <El/Evaluator/scriptVM.hpp>
#endif

#include "shots.hpp"
#include "AI/ai.hpp"
#include "AI/fsmScripted.hpp"
#include "AI/aiRadio.hpp"

#include <Es/Threads/threadSync.hpp>

#include "UI/chat.hpp"
#include "UI/missionDirs.hpp"

#include "Network/network.hpp"

#include "arcadeTemplate.hpp"
#include "UI/uiControls.hpp"
#include "UI/uiMap.hpp"
#include "UI/progressDisplay.hpp"

#include <El/QStream/qbStream.hpp>
#include "paramArchiveExt.hpp"
#include <El/ParamArchive/paramArchiveDb.hpp>
#include "objectClasses.hpp"

#include "detector.hpp"

#include "stringtableExt.hpp"

#include <El/Clipboard/clipboard.hpp>

#include "saveGame.hpp"
#include "mbcs.hpp"

#include "gameDirs.hpp"
#include "Es/Common/langExt.hpp"
#define ENABLED_NOOPT_Bebul 1
#if ENABLED_FEATURE(NOOPT)
#if _ENABLE_CHEATS
#pragma optimize ("", off)
#endif
#endif

#if _GAMES_FOR_WINDOWS
# include "X360/ArmA2.spa.h"
#endif

RString GetTmpSaveDirectory();
RStringB GetExtensionSave();
RStringB GetExtensionProfile();
RStringB GetExtensionWizardMission();

#undef GetObject
#undef DrawText
// #undef LoadString

#include "UI/resincl.hpp"

bool DisableTextures=false;
bool StopLoadingTextures=false;
bool StopLoadingModels=false;
int LoadTexturesInFrame;


extern bool LandEditor;
extern bool NoLandscape;

bool IsAppPaused();
bool IsAppFocused();


#if _ENABLE_REPORT
  #define LOG_ADD_REMOVE_VEHICLE 0
#endif


#if 0 //_DEBUG
  static bool timeJumpPreview=true; // TODO: move to class World
#else
  static bool timeJumpPreview=false;
#endif

#if _VBS3_CRATERS_LATEINIT

LateInitManager::~LateInitManager()
{
  // Destruct all objects inside the array
  while (!_objects.Empty())
  {
    LateInitItem *item = _objects.Start();
    _objects.Delete(item);
    delete item;
  }
}

void LateInitManager::Register(Object *object, float priority)
{
  // Get the first item with smaller priority or the head
  LateInitItem *i = _objects.Start();
  for (; _objects.NotEnd(i); i = _objects.Advance(i))
  {
    if (i->_priority < priority) break;
  }

  // Insert item before the item we found
  LateInitItem *newItem = new LateInitItem;
  newItem->_object = object;
  newItem->_priority = priority;
  _objects.InsertBefore(i, newItem);

  // Clear the queue in two steps (delete all items designed to be deleted and delete all references to the same item)
  {
    // Clear all objects designed to delete
    LateInitItem *item = _objects.Start();
    while (_objects.NotEnd(item))
    {
      if (item->_object->IsMarkedToDelete())
      {
        LateInitItem *nextItem = _objects.Advance(item);
        _objects.Delete(item);
        delete item;
        item = nextItem;
      }
      else
      {
        item = _objects.Advance(item);
      }
    }

    // Consider the object we added and keep only the reference with highest priority
    {
      // Find first reference in the queue (it has the biggest priority)
      LateInitItem *item = _objects.Start();
      while (_objects.NotEnd(item))
      {
        if (item->_object == object) break;
        item = _objects.Advance(item);
      }

      // If we found something, move to the next item and delete rest of the references
      if (_objects.NotEnd(item))
      {
        item = _objects.Advance(item);
        while (_objects.NotEnd(item))
        {
          if (item->_object == object)
          {
            LateInitItem *nextItem = _objects.Advance(item);
            _objects.Delete(item);
            delete item;
            item = nextItem;
          }
          else
          {
            item = _objects.Advance(item);
          }
        }
      }
    }
  }
}

void LateInitManager::LateInit()
{
  PROFILE_SCOPE_GRF_EX(lateI,*,SCOPE_COLOR_BLUE);

  // Display the near plane distance
  if (CHECK_DIAG(DELateInit))
  {
    DIAG_MESSAGE(200, Format("Late init objects: %d", _objects.Size()));
  }

  static int delItemsInOneFrame = 1;
  for (int i = 0; i < delItemsInOneFrame; i++)
  {
    if (!_objects.Empty())
    {
      // Get first item in the list and invoke its late initialization
      LateInitItem *item = _objects.Start();
      item->_object->LateInit();

      // Delete the initialized item
      _objects.Delete(item);
      delete item;
    }
    else
    {
      return;
    }
  }
}

#endif

// =================================================
void PPEManager::Update(const AutoArray<PPEffectType> &ppeTypes)
{
  // add new staff / update exists
  for (int i = 0; i < ppeTypes.Size(); i++)
  {
    const PPEffectType &pType = ppeTypes[i];

    int index = _hndls.FindKey(pType ._effectType);

    if (index == -1)
    {
      // create new post effect
      int hndl = CreateEffect(pType._effectType, pType._priority);

      Assert(hndl != -1);

      if (hndl == -1) 
      {
        RptF("Error: Post effect %s creation failed", cc_cast(pType._effectType));
        break;
      }

      PPEItem &item = _hndls.Append();
      item._key = pType._effectType;
      item._value = hndl;
      item._time = Glob.time; // update time
      item._enabled = true;

      continue;
    }

    // update existing - no need to create / destroy
    PPEItem &item = _hndls[index];

    if (item._value != -1)
    {
      static const float UpdateTime = 0.10f; // 10x per second pars update is enough

      if ((item._time + UpdateTime) < Glob.time)
      {
        GEngine->SetPostprocessParams(item._value, const_cast<AutoArray<float> &>(pType._pars));
        GEngine->EnablePostprocess(item._value, true);
        item._time = Glob.time; // update time
        item._enabled = true;
      }
    }
  }

  static const float TimeToLive = 0.50f;
  // remove unused
  int toDelete = -1;
  for (int i = 0; i < _hndls.Size(); i++)
  {
    // no update for 500ms - destroy post effect
    if ((_hndls[i]._time + TimeToLive) < Glob.time)
    {
      DestroyEffect(_hndls[i]._value);
      toDelete = i;
      break;
    }
  }

  if (toDelete != -1)
  {
    _hndls.DeleteAt(toDelete);
  }
}

int PPEManager::CreateEffect(const RString &name, int priority)
{
  return GEngine->CreatePostprocess(name, priority);
}

void PPEManager::DestroyEffect(int hndl)
{
  GEngine->DestroyPostprocess(hndl);
}

void PPEManager::EnableEffects(bool enable)
{
  for (int i = 0; i < _hndls.Size(); i++)
  {
    PPEItem &item = _hndls[i];

    if (enable != item._enabled)
    {
      GEngine->EnablePostprocess(item._value, enable);
      item._enabled = enable;
    }
  }
}

void PPEManager::Clear()
{
  for (int i = 0; i < _hndls.Size(); i++)
  {
    DestroyEffect(_hndls[i]._value);
  }

  _hndls.Clear();
}

Person *World::PlayerOn() const
{
  return _playerOn;
}

void World::SwitchPlayerTo(Person *veh)
{
  _playerOn = veh;
#if !_RELEASE
  Log("SwitchPlayerTo %s",veh ? (const char *)veh->GetDebugName() : "<null>");
#endif
  SetActiveChannels();
}

Person *World::GetRealPlayer() const
{
  return _realPlayer;
}

void World::SetRealPlayer(Person *veh)
{
  if (_realPlayer==veh) return;
  Person *old = _realPlayer;
  _realPlayer = veh;
  if (GetMode() == GModeNetware)
  {
    if (_realPlayer)
    {
      _realPlayer->SetRemotePlayer( GetNetworkManager().GetPlayer() );
      GetNetworkManager().UpdateObject(_realPlayer);
    }
    if (old)
    {
      old->SetRemotePlayer(AI_PLAYER);
      GetNetworkManager().UpdateObject(old);
    }
  }
}

bool World::CheckNativeEvent( NativeEvent condition ) const
{
  return _nativeEvent[condition];
}

void World::SetNativeEvent( NativeEvent condition )
{
  _nativeEvent[condition] = true;
}
void World::ResetNativeEvents()
{
  for (int i=0; i<lenof(_nativeEvent); i++)
  {
    _nativeEvent[i] = false;
  }
}

bool CheckNativeEvent(NativeEvent condition)
{
  return GWorld->CheckNativeEvent(condition);
}


void World::SetSpeaker(RString speaker, float pitch)
{
  Ref<BasicSpeaker> basic = new BasicSpeaker(speaker);
  _speaker = new Speaker(basic, pitch); 
}

AIBrain *World::FocusFromCamera(Object *camera) const
{
  EntityAI *vehicle = dyn_cast<EntityAI>(camera);
  if (!vehicle)
    return NULL;
  Person *player = PlayerOn();

#if _VBS3
  if(_cameraTarget)
  {
    Object *obj = _cameraTarget;
    Person *per = dyn_cast<Person>(obj);
    if(per)
      player = per;
  }
#endif
 
  AIBrain *unit = player ? player->CommanderUnit() : NULL;
  if (!unit || unit->GetVehicle() != vehicle)
    unit = vehicle->CommanderUnit();
  if (!unit)
    return NULL;
  if (!unit->GetPerson())
    return NULL;
  return unit;
}

/// command line argument
extern char LoadFile[];
extern const char DefLoadFile[];

#ifdef _MSC_VER
  #pragma warning(disable: 4355)
#endif

#ifdef _MSC_VER
  #pragma warning(default: 4355)
#endif

void World::InitGeneral()
{
  _wantedOvercast=0.3;
  _actualOvercast=_wantedOvercast;
  _wantedFog=0.0;
  _actualFog=_wantedFog;
  _wantedWind=0.1;
  _actualWind=_wantedWind;
  _speedOvercast=1.0/(60*60);
  _speedFog=1.0/(60*60);
  _speedWind=1.0/(60*60);
  _weatherTime=0;
  _nextWeatherChange=0;
  _eyeAccom = 1;
  _eyeAccomCut = true;
  _customEyeAccom = -1;
  _camUseNVG = false;
  _camUseTi = false;
  _camTiIndex = 0;
  OnCameraChanged();

  _showMiniMap = false;
  _showMiniMapToggle = false;

#if _VBS3
  _pauseSimulation = false;
#endif

  _moveMinBordersHUD = VZero;
  _moveMaxBordersHUD = VZero;
  _movePositionHUD = VZero;
  _targetHUD = NULL;

  //_vsyncErrorApplied = 0;

  Glob.clock.SetTime(8 * OneHour, 130 * OneDay, 1985);
	Glob.clockWeather.SetTime(8 * OneHour, 130 * OneDay, 1985);

  if (_scene.GetLandscape())
  {
    // we have to reset weather before recalculating lighting (because it is using values from SimulWEather - so it already has to be set)
    _scene.GetLandscape()->ResetWeather();
    _scene.GetLandscape()->SetOvercast(_actualOvercast,true);
    _scene.GetLandscape()->SetFog(_actualFog);
    _scene.GetLandscape()->SetWind(_actualWind);
    _scene.ResetLandShadows();
  }

  LightSun *sun=_scene.MainLight();
  sun->Recalculate(this);
  _scene.RecalculateLighting(1,_scene.GetLandscape());
  sun->Recalculate(this);
  _scene.MainLightChanged();

  ClearMissionEventHandlers();
}

void World::InitGeneral(ParamEntryPar cfg)
{
  _actualOvercast=cfg>>"startWeather";
  _wantedOvercast=cfg>>"forecastWeather";
  _actualFog=cfg>>"startFog";
  _wantedFog=cfg>>"forecastFog";
  _actualWind=cfg>>"startWind";
  _wantedWind=cfg>>"forecastWind";
  _speedOvercast=fabs(_wantedOvercast-_actualOvercast)/(30*60);
  _speedFog=fabs(_wantedFog-_actualFog)/(30*60);
  _speedWind=fabs(_wantedWind-_actualWind)/(30*60);
  _weatherTime=0;
  _nextWeatherChange=30*60;
  _nearImportanceDistributionTime=Time(0); // when distributions were calculated
  _farImportanceDistributionTime=Time(0); // when distributions were calculated
  _eyeAccom = 1;
  _eyeAccomCut = true;
  _customEyeAccom = -1;
  _camUseNVG = false;
  _camUseTi = false;
  _camTiIndex = 0;
  OnCameraChanged();

  RStringB time = cfg >> "startTime";
  RStringB date = cfg >> "startDate";
  int year;
  float timeInYear = Glob.clock.ScanDateTime("1/1/2000", time, year);
  float dateInYear = Glob.clock.ScanDateTime(date, "0:00", year);
  if (year < 100) year += 2000;
  Glob.clock.SetTime(timeInYear, dateInYear, year);
	Glob.clockWeather.SetTime(timeInYear, dateInYear, year);

  if (_scene.GetLandscape())
  {
    // we have to reset weather before recalculating lighting (because it is using values from SimulWEather - so it already has to be set)
    _scene.GetLandscape()->ResetWeather();
    _scene.GetLandscape()->SetOvercast(_actualOvercast,true);
    _scene.GetLandscape()->SetFog(_actualFog);
    _scene.GetLandscape()->SetWind(_actualWind);
    _scene.GetLandscape()->SetWindSpeed(
      Vector3(
      GRandGen.PlusMinus(0, 25.0f * _actualWind),
      0,
      GRandGen.PlusMinus(0, 25.0f * _actualWind)), false);

    _scene.ResetLandShadows();
  }

  LightSun *sun=_scene.MainLight();
  sun->Recalculate(this);
  // FIX _scene.MainLightChanged() was missing
  _scene.RecalculateLighting(1,_scene.GetLandscape());
  sun->Recalculate(this);
  _scene.MainLightChanged();
  Log("World set time %s, %s",(const char *)date,(const char *)time);
  // note: year is ignored

  ClearMissionEventHandlers();
}

/// information about one ambient life species
struct AmbLifeItem
{
  Ref<EntityType> _type;
  float _cost;
  ExpressionCode _probability;

  void Load(ParamEntryPar entry);
};


TypeIsMovableZeroed(AmbLifeItem);

/// infinite ambient life layer - simulated inside of player bubble
class AmbLifeArea: public IAmbLifeArea, public RefCount
{
  /// the unique identification of area in the manager (for debugging and serialization)
  RStringB _name;
  
  /// bubble (sphere around camera) radius
  float _bubbleRadius;
  
  /// total cost of all existing entities
  float _costExisting;

  /// cell position - ignored when _radius==FLT_MAX
  float _xCenter,_zCenter;
  /// cell radius - FLT_MAX means infinite cell
  float _radius;
  
  //@{ OffTree related information
  OffTreeRegion<float> _region;
  //@}

  /// target total cost
  ExpressionCode _costWanted;
  
  /// species distribution
  AutoArray<AmbLifeItem> _species;
  FindArrayKey<int> _speciesVars;
  
  /// index of species to be created is already known - wait until we can create it
  int _nextIndex;
  
  /// request units to move out
  float _moveOutCost;
  
  /// time skipped in the simulation
  float _timeSkipped;
  /// time skipped in the re-fill
  float _refillSkipped;
  
public:
  AmbLifeArea(ParamEntryPar cfg);

  //@{ IAmbLifeArea implementation
  void Init(const Camera &camera);
  bool CheckInArea(const Camera &camera, Entity *ent) const;
  float CheckMoveOut(const Camera &camera, Entity *ent) const;
  void OnEntityLeft(const Camera &camera, Entity *ent);
  void Simulate(const Camera &camera, float deltaT);
  //@}
  
  /// spawn as many entities as necessary to fill the area
  void RefillArea(const Camera &camera, bool border, int maxSpawned=INT_MAX);
  
  /// spawn one entity
  Entity *SpawnEntity(const Camera &camera, bool border, float costWanted);

  /// check entity cost
  float GetEntityCost(const EntityType *type) const;
  
  /// calculate cost wanted based on the position in the world
  float GetCostWanted(const Camera &camera) const;
  
#if _ENABLE_CHEATS
  RString GetDebugText() const;
#endif

  //@{ OffTree related interface
  const OffTreeRegion<float> &GetRegion() const {return _region;}
  bool IsIntersection(const OffTreeRegion<float> &reg) const 
  {
    // TODO: implement sphere vs. sphere test
    return reg.IsIntersection(_region);
  }
  //@}

  virtual RString GetName() const {return _name;}

  virtual LSError SaveRef(ParamArchive &ar);
};

#if _ENABLE_CHEATS
RString AmbLifeArea::GetDebugText() const
{
  float costWanted = GetCostWanted(*GScene->GetCamera());
  return _name + Format(" %.1f->%.1f",_costExisting,costWanted);
}
#endif

/// list of all ambient life parameters
#define AMBIENT_LIFE_PARAMETERS(XXX) \
  XXX(Rain) \
  XXX(Night) \
  XXX(Meadow) \
  XXX(Trees) \
  XXX(Hills) \
  XXX(Houses) \
  XXX(Windy) \
  XXX(Forest) \
  XXX(DeadBody) \
  XXX(Sea)

#define AMBIENT_LIFE_PAR_NAME(name) #name,

static const RString AmbLifeParName[]=
{
  AMBIENT_LIFE_PARAMETERS(AMBIENT_LIFE_PAR_NAME)
};

#define AMBIENT_LIFE_PAR_FUNC(name) GetAmbLife##name,

typedef float AmbLifeFuncType(Vector3Par camPos);

static float GetAmbLifeRain(Vector3Par camPos) {return GLandscape->GetRainDensity();}
static float GetAmbLifeNight(Vector3Par camPos) {return GScene->MainLight()->NightEffect();}
static float GetAmbLifeHills(Vector3Par camPos)
{
  float height = GLandscape->SurfaceY(camPos.X(),camPos.Z());
  const float minHills = 160;
  const float fullHills = 400;
  return InterpolativC(height,minHills,fullHills,0,1);
}
static float GetAmbLifeWindy(Vector3Par camPos)
{
  float windSpeed = GLandscape->GetWindSpeed().Size();
  return InterpolativC(windSpeed,0,20,0,1);
}

/// helper for ambient par function based on EnvType map
#define AMB_ENVTYPE(name) \
  static float GetAmbLife##name(Vector3Par camPos) \
  {return GLandscape->GetEnvTypeDensity(camPos,Env##name);}

AMB_ENVTYPE(Meadow)
AMB_ENVTYPE(Trees)
AMB_ENVTYPE(Sea)
AMB_ENVTYPE(Houses)

static float GetAmbLifeForest(Vector3Par camPos) {return GLandscape->GeographicalDensity(Landscape::FilterGeogrForest(), camPos);}

static bool IsDeadBody(Object *obj, float dist2, void *context)
{
	Man *man = dyn_cast<Man>(obj);
	if (!man)
    return false;
	AIBrain *brain = man->Brain();
	if (brain && brain->LSIsAlive())
	  // match only dead body
	  return false;
	return true;
}

static float GetAmbLifeDeadBody(Vector3Par camPos)
{
  // check if there is any dead body nearby
  // we are interested only in bodies which are very close
  const float maxDist = 10;
  
	Object *obj = GLandscape->NearestObject(camPos, maxDist, IsDeadBody, NULL, true);
	if (!obj)
    return 0.0f;
	Man *man = dyn_cast<Man>(obj);
	if (!man)
    return 0.0f;
  // check distance
  float dist = obj->FutureVisualState().Position().DistanceXZ(camPos);
  return floatMax(1-dist/maxDist, 0.0f);
}

static AmbLifeFuncType *AmbLifeFunc[]=
{
  AMBIENT_LIFE_PARAMETERS(AMBIENT_LIFE_PAR_FUNC)
};

// Helper Ambient Life functions for scripting

/// Compile the ambient life expression
bool CompileAmbLifeExpression(ExpressionCode &result, RString expression)
{
  return result.Compile(expression, AmbLifeParName, lenof(AmbLifeParName));
}

/// Evaluate the ambient life expression
float EvaluateAmbLifeExpression(ExpressionCode code, Vector3Par position)
{
  // prepare all variables
  const int nValues = lenof(AmbLifeFunc);
  float varValues[nValues];

  ExpressionCodeVars vars = code.GetUsedVariables();
  for (int i=0; i<vars.Size(); i++)
  {
    int vi = vars[i];
    varValues[vi] = AmbLifeFunc[vi](position);
  }

  // evaluate the expression
  float result;
  code.Evaluate(result, varValues, nValues);
  return result;
}

static inline int CmpAmbLifePlaces(const AmbLifePlace *p1, const AmbLifePlace *p2)
{
  return sign(p2->_value - p1->_value);
}

/// Find the places in the area with the max. values of the given expression
bool AmbLifeSelectBestPlaces(AutoArray<AmbLifePlace> &result, Vector3Val center, float radius, RString expression, float precision)
{
  if (radius <= 0)
    return false;
  if (precision <= 0)
    return false;

  ExpressionCode code;
  if (!code.Compile(expression, AmbLifeParName, lenof(AmbLifeParName)))
    return false;

  // list of variables
  const int nValues = lenof(AmbLifeFunc);
  float varValues[nValues];
  // list of used variables
  ExpressionCodeVars vars = code.GetUsedVariables();

  // calculate the number of samples
  int n = toInt(Square(2 * radius / precision));
  for (int i=0; i<n; i++)
  {
    float x = radius * (2.0f * GRandGen.RandomValue() - 1); 
    float z = radius * (2.0f * GRandGen.RandomValue() - 1); 
    if (Square(x) + Square(z) >= Square(radius))
      continue;

    AmbLifePlace &item = result.Append();
    // the position
    item._position = Vector3(center.X() + x, 0, center.Z() + z);
    // prepare the used variables
    for (int j=0; j<vars.Size(); j++)
    {
      int vi = vars[j];
      varValues[vi] = AmbLifeFunc[vi](item._position);
    }
    // the value
    code.Evaluate(item._value, varValues, nValues);
  }

  // sort by the value
  QSort(result.Data(), result.Size(), CmpAmbLifePlaces);

#if 0 // diagnostics
if (result[0]._value < 0.001)
{
  LogF("No good source found in %d iterations", n);
  for (int i=0; i<result.Size(); i++)
  {
    Vector3Val pos = result[i]._position;
    LogF("Position %.f, %.f", pos.X(), pos.Z());
    for (int j=0; j<vars.Size(); j++)
    {
      int vi = vars[j];
      varValues[vi] = AmbLifeFunc[vi](pos);
      LogF("  - %s = %.f", cc_cast(AmbLifeParName[vi]), varValues[vi]);
    }
    // the value
    float value;
    code.Evaluate(value, varValues, nValues);
    LogF("  - %s = %.f", cc_cast(expression), value);
  }
}
#endif
  return true;
}

void AmbLifeItem::Load(ParamEntryPar entry)
{
  RStringB type = entry.GetName();
  _type = VehicleTypes.New(type);
  if (!_type)
    RptF("%s: Cannot create ambient entity type %s", cc_cast(entry.GetContext()),cc_cast(type));
  
  _cost = entry>>"cost";
  RStringB propCode = entry>>"probability";
  if (!_probability.Compile(propCode,AmbLifeParName,lenof(AmbLifeParName)))
    RptF("Bad expression in %s",cc_cast(entry.GetContext("probability")));
}

AmbLifeArea::AmbLifeArea(ParamEntryPar cfg)
{
  // the area is exactly identified by the class name
  _name = cfg.GetName();

  _bubbleRadius = cfg>>"radius";
  RStringB costExpression = cfg>>"cost";
  if (!_costWanted.Compile(costExpression,AmbLifeParName,lenof(AmbLifeParName)))
    RptF("Bad expression in %s",cc_cast(cfg.GetContext("cost")));
  _costExisting = 0;
  _nextIndex = -1;
  _timeSkipped = 0;
  _refillSkipped = 0;
  
  ParamEntryVal species = cfg>>"Species";
  for (ParamClass::Iterator<> i(species.GetClassInterface()); i; ++i)
  {
    if (!(*i).IsClass())
      continue;
    AmbLifeItem &item = _species.Append();
    item.Load(*i);
    _speciesVars.MergeUnique(item._probability.GetUsedVariables());
  }
  _species.Compact();
  _speciesVars.Compact();
  
  if (cfg.FindEntry("position"))
  {
    ParamEntryVal pos = cfg>>"position";
    _xCenter = pos[0];
    _zCenter = pos[1];
    _radius = pos[2];
  }
  else
  {
    _xCenter = 0;
    _zCenter = 0;
    _radius = FLT_MAX;
  }
}

static bool DenseAmbient = false;
float AmbLifeArea::GetCostWanted(const Camera &camera) const
{
  float result;
  const int nValues = lenof(AmbLifeFunc);
  float varValues[nValues];

  const FindArrayKey<int> &vars = _costWanted.GetUsedVariables();
  for (int i=0; i<vars.Size(); i++)
  {
    int vi = vars[i];
    varValues[vi] = AmbLifeFunc[vi](camera.Position());
  }
  _costWanted.Evaluate(result,varValues,nValues);
  
  float densityBoost = DenseAmbient ? 10 : 1;
  
  return result * densityBoost;
}

float AmbLifeArea::GetEntityCost(const EntityType *type) const
{
  for (int i=0; i<_species.Size(); i++)
  {
    if (_species[i]._type==type)
      return _species[i]._cost;
  }
  return 1;
}

#if _ENABLE_CHEATS
static bool EnableAmbientLife = true;
#endif

bool DisableEnvironment;


bool AmbLifeArea::CheckInArea(const Camera &camera, Entity *ent) const
{
  // when amb. life is disabled, force immediate destruction
  if (DisableEnvironment) return false;
#if _ENABLE_CHEATS
  if (!EnableAmbientLife) return false;
#endif

  float distXZ2 = camera.Position().DistanceXZ2(ent->FutureVisualState().Position());
  float hysteresis = 1.1f;
  return distXZ2<Square(_bubbleRadius*hysteresis);
}

float AmbLifeArea::CheckMoveOut(const Camera &camera, Entity *ent) const
{
  if (_radius<FLT_MAX)
  {
    // finite cell handling:
    // we want to keep all entities inside of the cell
  }
  if (_moveOutCost<=0)
    return 0.0f;
  // we need to request some entity to move out
  // as we do not have an overview of all entities, we create a function covering the area
  // and based on entity position we decide how much should it move out
  Assert(_costExisting>=0);
  //Assert(_moveOutCost<=_costExisting); //this asserted too often (maybe leaves are unable to move out?)
  // whole area moving out tendency
  float m = _moveOutCost/_costExisting;

  // TODO: handle moving camera better, check TODO where CheckMoveOut is used
  
  // check relative distance from the bubble center
  float x = ent->FutureVisualState().Position().DistanceXZ(camera.Position())/_bubbleRadius;
  // we need to saturate,
  // otherwise function goes wild for small m (large exponent)
  if (x>1) x = 1;
  if (m<1e-3)
    // handle singular case: m close to 0
    return 0.0f;
  // function x^a where a = 2/m - 2
  float a = 2/m-2;
  
  float moveOut = pow(x,a);
  return moveOut;
}

void AmbLifeArea::Simulate(const Camera &camera, float deltaT)
{
  _timeSkipped += deltaT;
  const float simulationPeriod = 0.2f;
  if (_timeSkipped>simulationPeriod)
    _timeSkipped = 0;
  _refillSkipped += deltaT;
  const float refillPeriod = 0.1f;
  if (_refillSkipped>refillPeriod)
  {
    _refillSkipped = 0;
    // avoid spawning too much at once
    RefillArea(camera,true,3);
  }
}

void AmbLifeArea::OnEntityLeft(const Camera &camera, Entity *ent)
{
  // assume cost of any entity is 1
  // check what was the cost of the entity simulation
  const EntityType *type = ent->Type();
  float cost = GetEntityCost(type);
  _costExisting -= cost;
  // refill will be done in the next simulation step
}
  
void AmbLifeArea::RefillArea(const Camera &camera, bool border, int maxSpawned)
{
  float costWanted = GetCostWanted(camera);
  if (_costExisting>costWanted)
    // if cost is too high, request some entity to move out
    _moveOutCost = _costExisting-costWanted;
    // it is unlikely we will realize another one, but it is possible - different cost may exist
  else
    // no moving out necessary
    _moveOutCost = 0;
  
  for (int i=0; i<maxSpawned && _costExisting<costWanted; i++)
  {
    Entity *ent = SpawnEntity(camera,border,costWanted);
    if (!ent)
      break;
  }
}

void AmbLifeArea::Init(const Camera &camera)
{
  RefillArea(camera,false);
  _timeSkipped = 0;
  _refillSkipped = 0;
}

Entity *CreateVehicle(Vector3 &pos, RString type, bool local, bool init = true, ArcadeUnitSpecial special = ASpNone, bool expectedEmpty = false, bool keepHeight = false, Vector3 forward = VForward, Vector3 up = VUp);

/**
@param border create on the area border only
  - used to distinguish between full init and incremental update
*/
Entity *AmbLifeArea::SpawnEntity(const Camera &camera, bool border, float costWanted)
{
  if (DisableEnvironment)
    return NULL;
#if _ENABLE_CHEATS
  if (!EnableAmbientLife)
    return NULL;
#endif
  // if random selection was already done, wait until construction point allow it
  if (_nextIndex<0)
  {
    if (_species.Size()==0)
      return NULL;
    // select random species
    float randomValue = GRandGen.RandomValue();
    int i;
    
    // prepare all variables
    const int nValues = lenof(AmbLifeFunc);
    float varValues[nValues];

    for (int i=0; i<_speciesVars.Size(); i++)
    {
      int vi = _speciesVars[i];
      varValues[vi] = AmbLifeFunc[vi](camera.Position());
    }
    
    for (i=0; i<_species.Size()-1; i++)
    {
      float probability;
      _species[i]._probability.Evaluate(probability,varValues,nValues);
      
      randomValue -= probability;
      if (randomValue<=0)
        break;
    }
    _nextIndex = i;
  }
  const AmbLifeItem &item =_species[_nextIndex];
  
  if (_costExisting+item._cost>costWanted)
    // avoid getting over the wanted cost
    return NULL;

  // if type is invalid, cancel current selection and to nothing
  if (!item._type)
  {
    _nextIndex = -1;
    return NULL;
  }
  
  float r = _bubbleRadius;
  if (!border)
    // direct mapping uniform distribution for circle
    r = sqrt(GRandGen.RandomValue())*_bubbleRadius;

  if (!item._type->PreloadData(FileRequestPriority(10),r))
    // do not create until ready
    return NULL;

  _nextIndex = -1;
  
  float angle = GRandGen.RandomValue()*(H_PI*2);
  float rx = sin(angle), rz = cos(angle);

  float posX = camera.Position().X()+rx*r;
  float posZ = camera.Position().Z()+rz*r;
  float posY = GLandscape->SurfaceYAboveWater(posX,posZ);
  Vector3 pos(posX,posY,posZ);
  
  // TODO: avoid GetName
  Entity *ent = CreateVehicle(pos,item._type->GetName(),true);
  if (ent)
  {
    ent->SetAmbient(this,camera.Position());
    _costExisting += item._cost;
  }
  return ent;
}

IAmbLifeArea *IAmbLifeArea::LoadRef(ParamArchive &ar)
{
  RString name;
  if (ar.Serialize("name", name, 1) != LSOK)
    return NULL;
  return GWorld->FindAmbientArea(name);
}

LSError AmbLifeArea::SaveRef(ParamArchive &ar)
{
  CHECK(ar.Serialize("name", _name, 1))
  return LSOK;
}

#include <Es/Containers/offTree.hpp>

/// ambient life manager
class AmbLifeManager: public IAmbLifeManager
{
  RefArray<AmbLifeArea> _list;

public:
  //@{ IAmbLifeManager implementation
  virtual void Load(ParamEntryPar cfg);
  virtual void Clear();
  virtual void Init(const Camera &cam);
  virtual void Simulate(const Camera &cam, float deltaT);
  virtual IAmbLifeArea *FindArea(RString name) const;
  //@}
};

void AmbLifeManager::Load(ParamEntryPar cfg)
{
  _list.Clear();
#if _ENABLE_DEDICATED_SERVER
    // no ambient life on dedicated server
    if (IsDedicatedServer()) return;
#endif
  //@{ _DEDICATED_CLIENT
    if ( IsDedicatedClient() )
  return;
  //@}
  for (ParamClass::Iterator<> i(cfg.GetClassInterface()); i; ++i)
  {
    ParamEntryVal item = *i;
    if (!item.IsClass())
      continue;
    if (item.FindEntry("position"))
      continue;
    _list.Add(new AmbLifeArea(item));
  }
  _list.Compact();
}
void AmbLifeManager::Clear()
{
  _list.Clear();
}

void AmbLifeManager::Init(const Camera &cam)
{
  for (int i=0; i<_list.Size(); i++)
    _list[i]->Init(cam);
}
void AmbLifeManager::Simulate(const Camera &cam, float deltaT)
{
  // toggle ambient life density
  if (GInput.GetCheatXToDo(CXTDenseAmbient))
  {
    DenseAmbient = !DenseAmbient;
    GlobalShowMessage(500, "A lot of life %s", DenseAmbient ? "On" : "Off");
  }
  
#if _ENABLE_CHEATS
  static InitVal<int,-1> handles[30];
  int handle = 0;
#endif
  for(int i=0; i<_list.Size(); i++)
  {
    _list[i]->Simulate(cam,deltaT);
  
#if _ENABLE_CHEATS
      if (CHECK_DIAG(DEAmbient) && handle<lenof(handles))
        DIAG_MESSAGE_ID(200,i,_list[i]->GetDebugText());
#endif  
  }
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEAmbient))
  {
    for (int i=0; i<lenof(AmbLifeFunc); i++)
    {
      if (handle>=lenof(handles)) break;
      const char *name = AmbLifeParName[i];
      float value = (AmbLifeFunc[i])(cam.Position());
      if (value>0)
        DIAG_MESSAGE_ID(200,i,Format("%s %.3f",name,value));
    }
  }
#endif
}

IAmbLifeArea *AmbLifeManager::FindArea(RString name) const
{
  for (int i=0; i<_list.Size(); i++)
  {
    IAmbLifeArea *area = _list[i];
    if (area && stricmp(area->GetName(), name) == 0)
      return area;
  }
  return NULL;
}

//static const int daysInMonth[]={31,28,31,30,31,30,31,31,30,31,30,31};
int GetDaysInMonth(int year, int month);

void World::InitGeneral(const ArcadeIntel &intel)
{
  _actualOvercast=intel.weather;
  _wantedOvercast=intel.weatherForecast;
  _actualFog=intel.fog;
  _wantedFog=intel.fogForecast;
  _actualWind=intel.wind;
  _wantedWind=intel.windForecast;
  _speedOvercast=fabs(_wantedOvercast-_actualOvercast)/(30*60);
  _speedFog=fabs(_wantedFog-_actualFog)/(30*60);
  _speedWind =fabs(_wantedWind-_actualWind)/(30*60);
  _weatherTime=0;
  _nextWeatherChange=30*60;
  _nearImportanceDistributionTime=Time(0); // when distributions were calculated
  _farImportanceDistributionTime=Time(0); // when distributions were calculated
  _eyeAccom = 1;
  _eyeAccomCut = true;
  _customEyeAccom = -1;
  _camUseNVG = false;
  _camUseTi = false;
  _camTiIndex = 0;
  OnCameraChanged();

  int day = intel.day - 1;
  int year = intel.year;
  for (int m=0; m<intel.month-1; m++)
    day += GetDaysInMonth(year, m);
  float time = intel.hour * OneHour + intel.minute * OneMinute;
  float date = day * OneDay;
  Glob.clock.SetTime(time, date, year);
	Glob.clockWeather.SetTime(time, date, year);

  if (GLandscape)
  {
    // we have to reset weather before recalculating lighting (because it is using values from SimulWEather - so it already has to be set)
    GLandscape->ResetWeather();
    GLandscape->SetOvercast(_actualOvercast,true);
    GLandscape->SetFog(_actualFog);
    GLandscape->SetWind(_actualWind);
    _scene.GetLandscape()->SetWindSpeed(
      Vector3(
      GRandGen.PlusMinus(0, 25.0f * _actualWind),
      0,
      GRandGen.PlusMinus(0, 25.0f * _actualWind)), false);
    _scene.ResetLandShadows();
  }

  LightSun *sun=_scene.MainLight();
  // set sun and moon direction
  sun->Recalculate(this);
  // calculate sunOrMoon
  _scene.RecalculateLighting(1,_scene.GetLandscape());
  // set lighting and shadow direction
  sun->Recalculate(this);
  // FIX _scene.MainLightChanged() was missing
  _scene.MainLightChanged();
  // note: year is ignored

  if ((Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
  {
    ParamEntryVal world=Pars>>"CfgWorlds">> Glob.header.worldname;
    InitAmbientLife(world>>"Ambient");
  }

  ClearMissionEventHandlers();
}

#if _VBS3
class ResetAmbientLife
{
public:
  ResetAmbientLife(){};
  __forceinline bool operator () (Object *veh) const
  {
    Entity *obj = dyn_cast<Entity>(veh);
    if(obj)
      obj->SetAmbient(NULL,VZero);
    return false;
  }
};
#endif

void World::InitAmbientLife(ParamEntryPar cfg)
{
#if _VBS3
  ResetAmbientLife resetEntitys;
  _slowVehicles.ForEach(resetEntitys);
  _verySlowVehicles.ForEach(resetEntitys);
  _animals.ForEach(resetEntitys);
  _vehicles.ForEach(resetEntitys);
#endif

  _ambientLife->Load(cfg);
}

int World::GetAirportCount() const
{
  // check how many runways / taxiways are defined
  // airports need to be ordered: west, east, civilian
  // the sides have no real meaning - side management is done using other means (mission markers)
  return _airports.Size();
}

void World::GetAirportRunwayRect(int id, Vector3 *runway, Vector3 *taxi)
{
  const AirportInfo &info = _airports[id];
  
  // assume runway is a little bit longer than the taxiway
  float runwayBegLength = 15;
  float runwayEndLength = 30;

  Vector3 runwayDir(-info._ilsDir[0],0,-info._ilsDir[2]);
  runwayDir.Normalize();
  runwayDir.Normalize();
  Vector3 runwaySide(-runwayDir[2],0,runwayDir[0]);

  runway[0] = info._runwayBeg-runwaySide*info._runwayWidth*0.5f - runwayDir*runwayBegLength;
  runway[1] = info._runwayBeg+runwaySide*info._runwayWidth*0.5f - runwayDir*runwayBegLength;
  runway[2] = info._runwayEnd-runwaySide*info._runwayWidth*0.5f + runwayDir*runwayEndLength;
  runway[3] = info._runwayEnd+runwaySide*info._runwayWidth*0.5f + runwayDir*runwayEndLength;

  if (info._taxiwayBeg.SquareSize()>0 && info._drawTaxiway)
  {
    taxi[0] = info._taxiwayBeg-runwaySide*info._taxiwayWidth*0.5f;
    taxi[1] = info._taxiwayBeg+runwaySide*info._taxiwayWidth*0.5f;
    taxi[2] = info._taxiwayEnd-runwaySide*info._taxiwayWidth*0.5f;
    taxi[3] = info._taxiwayEnd+runwaySide*info._taxiwayWidth*0.5f;
  }
  else
  {
    taxi[0] = VZero;
    taxi[1] = VZero;
    taxi[2] = VZero;
    taxi[3] = VZero;
  }
}

/// check if airport is enemy one
static bool CheckEnemy(TargetSide mySide, TargetSide airportSide)
{
  // make sure any army assumes civilian airports as non-enemy, even if civilians are hostile
  return airportSide!=TCivilian && airportSide!=TSideUnknown && airportSide!=mySide;
}

/// check if airport is non-enemy (friendly, neutral, free)
static bool CheckNotEnemy(TargetSide mySide, TargetSide airportSide)
{
  return !CheckEnemy(mySide,airportSide);
}

static bool CheckAny(TargetSide mySide, TargetSide airportSide)
{
  return true;
}

/**
find nearest airport based on the corridor we are in
Filter based on airport side requirements
@param check filter
If no suitable is found, return -1
*/

static int FindAirport(Vector3Val pos, Vector3Val dir, const Array<World::AirportInfo> &airports,
  bool (*check)(TargetSide mySide, TargetSide airportSide), TargetSide mySide, bool canSTOL)
{
  // find nearest airport based on the corridor we are in
  // used for player initiated operations and ILS HUD
  int bestI = -1;
  float bestCost = FLT_MAX;
  for (int i=0; i<airports.Size(); i++)
  {
    const World::AirportInfo &info = airports[i];
    float cost = 0;
    if (info.STOL())
    {
      if (!canSTOL)
        continue;
      // penalize short runways - use only when really suitable
      cost += 2000;
    }
    if (!check(mySide,info._side))
      continue;
    // aligned along the glide slope
    float distAside = NearestPointInfiniteDistance(info._ilsPos,info._ilsPos+info._ilsDir,pos);  
    float dist = info._ilsPos.Distance(pos);
    // +1 = perfectly oriented, -1 = flying in the opposite direction
    float aligned = -dir.CosAngle(info._ilsDir);
    // when not inside of the landing cone, assume both max side distance and worst alignment
    float approaching = (pos-info._ilsPos).CosAngle(info._ilsDir);
    if (approaching<0.5f)
      aligned = -1, distAside = floatMax(distAside,dist);
    // distance is considered only a little - moving along is very fast
    // if is aligning with the glide slope which is difficult
    cost += InterpolativC(aligned,-1,+1,4000,0) + distAside*4 + dist;
    if (cost<bestCost)
    {
      bestCost = cost;
      bestI = i;
    }
  }
  return bestI;
}

/**
@param side which airport to use. When TSideUnknown, select the nearest one. Otherwise prefer as defined by markers.

\patch 5161 Date 5/29/2007 by Ondra
- New: Multiple airports supported on one island.
*/

int World::GetAirportId(Vector3Par pos, Vector3Par dir, TargetSide side, bool canSTOL, int airportId) const
{
  // if searching fails, return the main one
  int id = 0;
  if (side==TSideUnknown)
  {
    // find nearest airport based on the corridor we are in
    // used for player initiated operations and ILS HUD
    int bestI = FindAirport(pos,dir,_airports,CheckAny,side,canSTOL);
    if (bestI>=0)
      id = bestI;
  }
  else
  {
    // check if there is a preferred airport
    if (airportId>=0 && airportId<_airports.Size())
      // preferred airport id - if it exists, use it, even if it is currently held by the enemy
      id = airportId;
    else
    {
      // auto-select airport
      // search for a non-hostile airport
      id = FindAirport(pos,dir,_airports,CheckNotEnemy,side,canSTOL);
      // when not available, any will do
      if (id<0)
        id = FindAirport(pos,dir,_airports,CheckAny,side,canSTOL);
    }    
  }
  return id>=0 ? id : 0;
}

const World::AirportInfo *World::GetAirport(Vector3Par pos, Vector3Par dir, TargetSide side, bool canSTOL, int airportId) const
{
  int id = GetAirportId(pos,dir,side,canSTOL,airportId);
  return &_airports[id];
}
int World::GetILS(Vector3 &pos, Vector3 &dir, bool &stol, TargetSide side, bool canSTOL, int airportId) const
{
  int id = GetAirportId(pos,dir,side,canSTOL,airportId);
  const AirportInfo &airport = _airports[id];
  pos=airport._ilsPos;
  dir=airport._ilsDir;
  stol = airport.STOL();
  return id;
}

bool World::AirportInfo::STOL() const
{
  float runwayLength = _runwayBeg.Distance(_runwayEnd);
  // Arma 2 carrier has runway ~250 m, Utes runway is ~720 m
  return runwayLength<500;
}

TargetSide World::GetAirportSide(int airportId) const
{
  return (airportId>=0 && airportId<_airports.Size()) ? _airports[airportId]._side : TSideUnknown;
}

void World::SetAirportSide(int airportId, TargetSide side)
{
  if (airportId>=0 && airportId<_airports.Size())
    _airports[airportId]._side = side;
}

static int FindNearestAirport(const Array<World::AirportInfo> &airports, Vector3Par pos, Vector3Par dir, int *airportId)
{
  // TODO: optimize: it would be faster to keep minmax for each taxiway
  // we assume taxiways do not overlap
  // the nearest we find is the one we are interested in
  int bestI = 0;
  float bestDist = FLT_MAX;
  for (int i=0; i<airports.Size(); i++)
  {
    const World::AirportInfo &info = airports[i];
    float dist = NearestPointDistance(info._runwayBeg,info._runwayEnd,pos);
    if (dist<bestDist)
    {
      bestDist = dist;
      bestI = i;
    }
  }
  // if we see it is a real solution, we can set the home id
  if (bestDist<200 && airportId)
    *airportId = bestI;
  // the solution we find should be very close to our position
  return bestI;
}

/** pos always find a taxiway which is nearest to given point */
const AutoArray<Vector3> &World::GetTaxiInPath(TargetSide side, Vector3Par pos, Vector3Par dir, int *airportId) const
{
  int id = FindNearestAirport(_airports,pos,dir,airportId);
  return _airports[id]._taxiIn;
}

/** pos always find a taxiway which is nearest to given point */
const AutoArray<Vector3> &World::GetTaxiOffPath(TargetSide side, Vector3Par pos, Vector3Par dir, int *airportId) const
{
  int id = FindNearestAirport(_airports,pos,dir,airportId);
  return _airports[id]._taxiOff;
}

/**
This is intended to be used when the airport is on the ground
*/
int World::CheckAirport(Vector3Par pos, Vector3Par dir) const
{
  int id = -1;
  return FindNearestAirport(_airports,pos,dir,&id);
}

/* Used for additional environment random sounds */
struct EnvSoundPars: public SoundPars
{
  float _prob;
  float _min;
  float _mid;
  float _max;
};

TypeIsMovableZeroed(EnvSoundPars);

// define environmental sounds
struct EnvSoundConfig
{
  SoundPars _pars;
  ExpressionCode _volume;
  mutable Time _time; // time when next sample can be select
  AutoArray<EnvSoundPars> _randPars; // additional random sounds
  
  void Load(ParamEntryPar entry);
};

void EnvSoundConfig::Load(ParamEntryPar entry)
{
  GetValue(_pars,entry>>"sound");
  RStringB volumeCode = entry>>"volume";
  if (!_volume.Compile(volumeCode,AmbLifeParName,lenof(AmbLifeParName)))
    RptF("Bad expression in %s",cc_cast(entry.GetContext("volume")));

  // additional random sounds
  ConstParamEntryPtr entryPtr = entry.FindEntry("random");
  if (entryPtr)
  {
    // random start time
    _time = Glob.time + GRandGen.Gauss(8, 14 ,20);

    ParamEntryVal list = *entryPtr;;

    for (int i = 0; i < list.GetSize(); i++)
    {
      RStringB sampleName = list[i];

      ParamEntryVal locEntry = entry >> sampleName;
      EnvSoundPars &pars = _randPars.Append();
      GetValue(pars, locEntry);

      if (locEntry.IsArray() && locEntry.GetSize() == 8)
      {
        pars._prob = locEntry[4];
        pars._min = locEntry[5];
        pars._mid = locEntry[6];
        pars._max = locEntry[7];
      }
    }
    _randPars.Compact();
  }
}

TypeIsMovable(EnvSoundConfig)

/// manage environmental sounds
class EnvSoundManager: public IEnvSoundManager
{
  /// all vars used for volume calculation
  FindArrayKey<int> _vars;
  
  AutoArray<EnvSoundConfig> _sounds;
  float _varValue[lenof(AmbLifeFunc)];
#if _ENABLE_CHEATS
  mutable bool _showDiag;
#endif

public:  
  EnvSoundManager(ParamEntryPar entry);
  //@{ IEnvSoundManager implementation
  virtual int Count(const Camera &camera);
  const SoundPars &GetSound(int i, float &volume) const;
  bool GetRandSound(int i, SoundPars &pars) const;

#if _ENABLE_CHEATS
  virtual void ShowDiag() const { _showDiag = !_showDiag; }
#endif
  //@}
};

EnvSoundManager::EnvSoundManager(ParamEntryPar entry)
{
  for (ParamClass::Iterator<> i(entry.GetClassInterface()); i; ++i)
  {
    ParamEntryVal e = *i;
    if (!e.IsClass()) continue;
    // if there is no volume, it is not a sound we can load
    if (!e.FindEntry("volume"))
      continue;
    EnvSoundConfig &item = _sounds.Append();
    item.Load(*i);
    _vars.MergeUnique(item._volume.GetUsedVariables());
  }
  _sounds.Compact();
  _vars.Compact();

#if _ENABLE_CHEATS
  _showDiag = false;
#endif
}

int EnvSoundManager::Count(const Camera &camera)
{
  // called once per frame
  // we init context for GetSound here
  for (int i=0; i<_vars.Size(); i++)
  {
    int vi = _vars[i];
    _varValue[vi] = AmbLifeFunc[vi](camera.Position());
  }

#if _ENABLE_CHEATS
  if (_showDiag)
  {
    const int BaseID = 333;
    for (int i=0; i<lenof(AmbLifeFunc); i++)
    {
      DIAG_MESSAGE_ID(100, BaseID + i, Format("%s: %.3f", cc_cast(AmbLifeParName[i]), AmbLifeFunc[i](camera.Position())));
    }
  }
#endif

  return _sounds.Size();
}

const SoundPars &EnvSoundManager::GetSound(int i, float &volume) const
{
  PROFILE_SCOPE_EX(sndGS,sound);
  _sounds[i]._volume.Evaluate(volume,_varValue,lenof(_varValue));
  return _sounds[i]._pars;
}

bool EnvSoundManager::GetRandSound(int i, SoundPars &pars) const
{
  PROFILE_SCOPE_EX(snGRS,sound);

  // no additional sounds defined
  if (_sounds[i]._randPars.Size() <= 0) { return false; }

  // time to select next sample
  if (Glob.time > _sounds[i]._time)
  {
    const AutoArray<EnvSoundPars> &sPars = _sounds[i]._randPars;

    const EnvSoundPars *envPars = NULL;
    float probab = GRandGen.RandomValue();

    // random sample selection
    int index = 0;
    for (; index < sPars.Size() - 1; index++)
    {
      probab -= sPars[index]._prob;
      if (probab <= 0) 
      {
        envPars = &(sPars[index]);
        break;
      }
    }
    if (envPars == NULL && index == sPars.Size())
      envPars = &(sPars[sPars.Size()-1]);

    if (envPars)
    {
      // time to next sample
      _sounds[i]._time = Glob.time + GRandGen.Gauss(envPars->_min, envPars->_mid, envPars->_max);
      // set parameters
      pars = *envPars;
      return true;
    }
  }
  return false;
}

SoundPars ExplicitEnvSound;

void World::InitEditor(Landscape *landscape, Entity *cursor)
{
  ParamEntryVal cfgWorlds = Pars>>"CfgWorlds";
  ParamEntryVal defWorld = cfgWorlds>>"DefaultWorld";
  RString skyTexture = defWorld>>"skyTexture";
  RString skyTextureR = defWorld>>"skyTextureR";

  skyTexture.Lower();
  skyTextureR.Lower();

  _scene.Init(_engine, landscape, skyTexture, skyTextureR);

  GScene = GetScene();
  GLandscape = _scene.GetLandscape();

  // Load island if set... 
  if (*Glob.header.worldname && cfgWorlds.FindEntry(Glob.header.worldname))
    GLandscape->Init(cfgWorlds >> Glob.header.worldname);
  else
    GLandscape->Init(defWorld);

  InitGeneral();
  InitCameraPos();
  _scene.GetLandscape()->InitObjectVehicles();

  VehicleTypes.Preload();

  ExplicitEnvSound.name = "";
  ExplicitEnvSound.freq = ExplicitEnvSound.vol = 1;
  DisableEnvironment = false;

#if _ENABLE_BULDOZER
  const char *ext = strrchr(LoadFile,'.');
  if (ext && QFBankQueryFunctions::FileExists(LoadFile))
  {
    if (!strcmpi(ext,".p3d"))
    {
      ReloadViewer(LoadFile, "");
      InitCameraPos();
      GLOB_ENGINE->ReinitCounters();
      return;
    }
    else
    {
      // extract world name from filename
      char islandName[256];
      GetFilename(islandName,LoadFile);
      SwitchLandscape(islandName);
      Shapes.OptimizeAll();
    }
  }
#endif

  {
    if (cursor)
    {
      Vector3 cPos(2525,5,2925);
      cursor->SetPosition(cPos);
      AddVehicle(cursor);
      GetGameState()->VarSet("bis_buldozer_cursor", GameValueExt(cursor), true, false, GetMissionNamespace()); // what namespace?
      _cameraOn = cursor;
    }
    InitCameraPos();
    _scene.GetLandscape()->InitGeography();
  }
  
  RString initScript = RString("scripts\\editor.sqs");
  if (QFBankQueryFunctions::FileExists(initScript))
  {
    Script *script = new Script("editor.sqs", GameValue(), GetMissionNamespace(), INT_MAX); // what namespace?
    AddScript(script, true); // simulate now
  }
}

void World::FreelookChange(bool active)
{
  OLinkObject cameraVehicle=_cameraOn;
  if (!active)
  {
    float initDive = 0, initHead = 0, initFOV = 0;
    if (cameraVehicle)
      cameraVehicle->InitVirtual(_camTypeNew, initHead, initDive, initFOV);

    // center camera

    // when free-look goes off, reset cursor to forward in some situations
    CameraType camTypeAct = GetCameraType();

    if (cameraVehicle && !cameraVehicle->IsContinuous(camTypeAct)
      && !cameraVehicle->IsExternal(camTypeAct) && !cameraVehicle->IsVirtualX(camTypeAct))
    {
      if (_ui)
      {
        const Camera &camera = *_scene.GetCamera();
        _ui->SetCursorDirection(camera.Direction());
      }
    }
  }
  if (cameraVehicle)
    cameraVehicle->FreelookChange(active);
}

void World::SetCameraType(CameraType cam)
{
  _camTypeOld = _camTypeNew = _camTypeMain = cam;
  _camTypeNewFactor=1;
  _camTypeTransition = 1;
}

void World::DisableTacticalView()
{
  if (_camTypeMain == CamGroup && !IsShowMapWanted())
  {
    if (_cameraExternal)
      SetCameraType(CamExternal);
    else
      SetCameraType(CamInternal);
  }
}

CameraType World::GetCameraTypePreferInternal() const
{
  if (_camTypeNewFactor<=0) return _camTypeOld;
  if (_camTypeNewFactor>=1) return _camTypeNew;
  if (_camTypeNew==CamInternal) return _camTypeNew;
  if (_camTypeOld==CamInternal) return _camTypeOld;
  if (_camTypeNew==CamGunner) return _camTypeNew;
  if (_camTypeOld==CamGunner) return _camTypeOld;
  return _camTypeNewFactor>0.5f ? _camTypeNew : _camTypeOld ;
}


void RunInitScript();

bool World::InitVehicles(IMission &t)
{
  SECUROM_MARKER_SECURITY_ON(13)
  // should always be called after SwitchLandscape
  ProgressReset();

  Assert(CheckVehicleStructure());

  _camUseNVG = false;
  _camUseTi = false;
  _camTiIndex = 0;
  SetEndMode(CAST_TO_ENUM(EEndMode, EMContinue));
  _endMissionCheated = false;
  _autoSaveRequest = false;
  _loadAutoSaveRequest = false;
  _disableSavingRequest = false;
  _savingEnabled = true;
  _endFailed = false;
  _artilleryEnabled = true;
  _enableItemsDropping = true;

  _moveMinBordersHUD = VZero;
  _moveMaxBordersHUD = VZero;
  _movePositionHUD = VZero;
  _targetHUD = NULL;

  // indicate game was paused during the save
  _gameWasNotPaused = false;

  _ppeManager.Clear();

  // set post effects to default
  GEngine->ResetGameState();

  GCustomChannels.ClearCustomChannels();
  EnableRadio();
  EnableSentences();

  SetViewDistanceHard(0);
  SetTerrainGridHard(0,false);
  SetViewDistanceMin(t.Intel().viewDistance);
  AdjustSubdivision(_mode);

  if (_ui)
    _ui->ResetHUD();

  DestroyUserDialog();

  ProgressRefresh();

  AIGlobalInit();

  ProgressRefresh();

#if _ENABLE_INDEPENDENT_AGENTS
  _agents.Clear();
  _teams.Clear();
#endif
  
  ConstParamEntryPtr enableItemsDropping = ExtParsMission.FindEntry("enableItemsDropping");
  if (enableItemsDropping) _enableItemsDropping = *enableItemsDropping;
  
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  void RunInitFunctionsScript();
  RunInitFunctionsScript();
  ProgressRefresh();

  // calculate number of vehicles to create

  AUTO_STATIC_ARRAY(VehicleInitMessage, inits, 128);
  
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  InitNoCenters(t, (VehicleInitMessages &)inits, _mode);
  ProgressRefresh();

  Assert(CheckVehicleStructure());

  // init & identity EH 
  EvalPostponedEvents();

  // solve moving entities out prior calling any scripts
  MoveOutAndDelete(_vehicles);
  MoveOutAndDelete(_animals);
  MoveOutAndDelete(_fastVehicles);
  MoveOutAndDelete(_slowVehicles);
  MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);

  for (int i=0; i<inits.Size(); i++)
  {
    inits[i]._vehicle->OnVehicleInit(inits[i]._init);
  }

  if (_mode == GModeArcade) RunInitScript();
  Assert(CheckVehicleStructure());

  for (int i=0; i<inits.Size(); i++)
  {
    inits[i]._vehicle->InitUnits();
    // init transport channel?
    //inits[i].vehicle->GetRadio().SilentProcess();
  }

  { // inits may request some moving in/out - process them
    MoveOutAndDelete(_vehicles);
    MoveOutAndDelete(_animals);
    MoveOutAndDelete(_fastVehicles);
    MoveOutAndDelete(_slowVehicles);
    MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);
  }

  Assert(CheckVehicleStructure());

  // with client-server executing VehicleInit on the server is enough
  
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  UpdateAttachedPositions();
  
  // vehicle init might have caused a weather change, fog settings may need recalc
  AdjustSubdivision(GetMode());
  
  // mode may have changed, as a result terrain grid may have changed as well
  _scene.GetLandscape()->OnTerrainGridChanged();

  FinishMissionInit(true);

  SECUROM_MARKER_SECURITY_OFF(13)

  return GetMaxError()<EMError;
}


void World::FinishMissionInit( bool progress )
{
  if (progress) ProgressRefresh();
  GetSensorList()->UpdateAll();

  FreeOnDemandGarbageCollect(1024*1024,256*1024);

  if (progress) ProgressRefresh();
  // new data ready - adapt texture cache
  // unlock all types and textures
  extern void ManCompact();
  ManCompact();
  VehicleTypes.CleanUp();

  FreeOnDemandGarbageCollect(1024*1024,256*1024);

  _engine->TextBank()->Preload();
  Shapes.OptimizeAll();


  _sensorList->UpdateAll();
  EntityAIFull *ai=dyn_cast<EntityAIFull, Object>(_cameraOn);
  if (_ui && ai)
    _ui->ResetVehicle(ai);
  if (progress) ProgressRefresh();

  ExplicitEnvSound.name = "";
  ExplicitEnvSound.freq = ExplicitEnvSound.vol = 1;
  DisableEnvironment = false;

  FreeOnDemandGarbageCollect(1024*1024,256*1024);

  _engine->ReinitCounters();
}

  // on PC we need to make sure a substantial virtual and "new heap" space is left
  // there is no LL recovery available due to fact MemStore is used for the file cache
  size_t systemFreeRequired = 10*1024*1024;
  size_t mainHeapFreeRequired = systemFreeRequired;

bool World::PreloadAroundCamera(float maxTime)
{
  if ( !IsDedicatedServer() && !IsDedicatedClient() )
  {
  Camera *cam = _scene.GetCamera();
  if (!cam) return true;
  float maxShadowDist = _scene.GetObjectShadows() ? Glob.config.GetMaxShadowSize()+Glob.config.GetShadowZ() : 0;
  const float maxPreloadRange = 600.0f;
  float maxPreloadDist = floatMin(maxPreloadRange,floatMax(Glob.config.objectsZ,maxShadowDist));
  FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
  bool ret = Preload1stPerson()
    && GLandscape->PreloadData(cam->Position(),maxPreloadDist,maxTime,0.2f,NULL,true)
    && PreloadBasicData();
  return ret;
}
  else return true;
}

bool World::PreloadBasicData(bool logging)
{
  if ( !IsDedicatedServer() && !IsDedicatedClient() )
  {
  bool ret = true;
  // preload sky textures
  // preload cloud textures
  if (!GLandscape->PreloadSkyAndClouds())
  {
    if (logging)
    {
      LogF("  not done, sky and clouds");
      // debugging opportunity
      //GLandscape->PreloadSkyAndClouds();
    }
    ret = false;
  }
  return ret;
}
  return true;
}

bool World::Preload1stPerson(bool logging)
{
  if ( !IsDedicatedServer() && !IsDedicatedClient() )
  {
  bool ret = true;
  if (_cameraOn)
  {
    int level = _cameraOn->InsideLOD(CamInternal);
    if (level>=0 && level!=LOD_INVISIBLE)
    {
      LODShape *lShape = _cameraOn->GetShape();
      // if the unit is logic, it has no shape
      if (lShape && lShape->NLevels()>0)
      {
        // preload 1st person
        if (lShape->CheckLevelLoaded(level,true))
        {
          ShapeUsed shape = lShape->Level(level);
          if (!shape->PreloadTextures(0, NULL))
          {
            if (logging)
              LogF("  not done, textures of %s:%d",cc_cast(lShape->GetName()),level);
            ret = false;
          }
        }
        else
        {
          if (logging)
            LogF("  not done, shape %s:%d",cc_cast(lShape->GetName()),level);
          ret = false;
        }
        // preload 3rd person as well
        if (lShape->CheckLevelLoaded(0,true))
        {
          ShapeUsed shape = lShape->Level(0);
          if (!shape->PreloadTextures(0, NULL))
          {
            if (logging)
              LogF("  not done, textures of %s:%d",cc_cast(lShape->GetName()),0);
            ret = false;
          }
        }
        else
        {
          if (logging)
            LogF("  not done, shape %s:%d",cc_cast(lShape->GetName()),0);
          ret = false;
        }
      }
    }
    OLink(EntityAI) camAI = dyn_cast<EntityAI, Object>(_cameraOn);
    OLink(Person) person = FocusOn() ? FocusOn()->GetPerson() : NULL;
    if (camAI)
    {
      EntityAIFull *aiFull = dyn_cast<EntityAIFull,EntityAI>(camAI);
      if (aiFull)
      {
        TurretContext context;
        if (aiFull->FindTurret(person, context))
        {
          WeaponModePtr ptr = context._weapons->ValidatedCurrentWeapon();
          if (ptr.IsValid())
            aiFull->PreloadFireWeaponEffects(*context._weapons, ptr);
        }
      }

//       if (!camAI->PreloadInsideView())
//       {
//         if (logging)
//         {
//           LogF("  not done, inside view");
//         }
//         ret = false;
//       }
    }
    if (camAI && person)
    {
      // preload weapon model
      LODShape *optics = camAI->GetOpticsModel(person);
      if (optics && optics->NLevels()>0)
      {
        // make sure optics model for the weapon is always ready to be used
        if (optics->CheckLevelLoaded(0,true))
        {
          // preload all textures needed
          ShapeUsed shape = optics->Level(0);
          if (!shape->PreloadTextures(0, NULL, true))
          {
            if (logging)
              LogF("  not done, textures of %s:%d",cc_cast(optics->GetName()),0);
            ret = false;
          }
        }
        else
        {
          if (logging)
            LogF("  not done, shape %s:%d",cc_cast(optics->GetName()),0);
          ret = false;
        }
      }
      // preload weapon model
      if (!person->PreloadInsideView())
      {
        if (logging)
          LogF("  not done, inside view");
        ret = false;
      }
    }
  }
  return ret;
}
  else  return true;
}

struct World::PreloadAsyncContext
{
  int _state;
  DWORD _startTickCount;
  DWORD _untilTickCount;
  float _preloadRange;
  float _rangeReported;
  /// true when onPreloadFinished event handler should be called
  bool _callHandler;

  PreloadAsyncContext(float maxTime, bool callHandler)
  {
#if _ENABLE_PERFLOG
    LogF("<ignore>");
#endif
    _state = 0;
    _preloadRange = 0;
    _rangeReported = 0;
    _startTickCount = GlobalTickCount();
    _untilTickCount = GlobalTickCount() + toInt(maxTime*1000);
    _callHandler = callHandler;
  }
  ~PreloadAsyncContext()
  {
#if _ENABLE_PERFLOG
    LogF("</ignore>");
    LogF("Async preload time: %g %d, loaded %g m",
      int(GlobalTickCount()-_startTickCount)*0.001f,_state,_preloadRange);
#endif

  }
};


bool World::PreloadAroundCameraAsync(PreloadAsyncContext &ctx)
{
  Camera *cam = _scene.GetCamera();
  if (!cam)
    return true;
  if (!_cameraEffect && !_cameraOn)
    return true;

  // simple FSM, counter is a helper variable - we could use switch instead
  int counter = 0;
  if (ctx._state==counter++)
  {

    FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
    FreeOnDemandFrame();

    // make sure initial camera is processed - no need to adjust the engine camera matrix though
    InitCameraPos();

    // gradually increase range - first of all preload close data
    ctx._preloadRange = 100.0f;
    ctx._rangeReported = 0;

    ctx._state++;
  }
  else if (ctx._state==counter++)
  {
    float maxShadowDist = _scene.GetObjectShadows() ? Glob.config.GetMaxShadowSize()+Glob.config.GetShadowZ() : 0;
    // always preload in some given range around the camera, even when not visible
    const float minPreloadRange = 600.0f;
    float maxPreloadDist = floatMax(minPreloadRange,floatMax(Glob.config.objectsZ,maxShadowDist));
    float maxIterTime = 0.2f;

    bool done = false;

    FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
    // no aborts because of time manager wanted during preloads
    // we have our own time management
    TimeManagerDisabled disableTM;

    // we pretend we are rendering the scene
    // this should prepare almost everything, and initialize complexity management
    PrepareDraw(GetCamInsideVehicle(),true,ctx._preloadRange);
    Scene::DrawContext drawContext;
    _scene.BeginObjects();
    _scene.DrawPreparation(drawContext,0.0f,false);
    _scene.EndObjects();
    // context may require cleanup
    _scene.ObjectsCleanUp(drawContext);
    _scene.LightsCleanUp();
    GEngine->FinishPresent(); // needed so that we can call PerformMipmapLoading
    GEngine->FinishFinishPresent();
    // we need to commit texture preloads here
    GEngine->TextBank()->PerformMipmapLoading();

#if _ENABLE_PERFLOG
    bool distlogging = true;
    bool logging = GlobalTickCount()>ctx._untilTickCount-2000;
#else
    const bool distlogging = false;
    const bool logging = false;
#endif
    // preload 1st person view of the player's entity
    done = Preload1stPerson(logging) && PreloadBasicData(logging)
      && GLandscape->PreloadData(cam->Position(),ctx._preloadRange,maxIterTime,0.2f,CameraOn(),true,FLT_MAX,logging);
    // if successful, increase range
    if (done && ctx._preloadRange<maxPreloadDist)
    {
      if (distlogging && (ctx._preloadRange>ctx._rangeReported*1.5f || ctx._preloadRange>ctx._rangeReported+300.0f))
      {
        ctx._rangeReported = ctx._preloadRange;
        LogF("  %.1f: loaded %g m of %g m",int(ctx._untilTickCount-GlobalTickCount())*0.001f,ctx._preloadRange,maxPreloadDist);
      }
      ctx._preloadRange *= 1.2f;
      done = false;
      if (ctx._preloadRange>maxPreloadDist)
        ctx._preloadRange = maxPreloadDist;
      if (_objAroundCam->LeftToLock()>0)
      {
        done = false;
        if (!done && logging)
          LogF(" Not done:Locking object around camera, %d left (of %d)", _objAroundCam->LeftToLock(),_objAroundCam->AreaSize());
      }
    }

    if (done || GlobalTickCount()>ctx._untilTickCount)
    {
      ctx._state++;
      LogF("Async preload time: %g %s, loaded %g m of %g m",
        int(GlobalTickCount()-ctx._startTickCount)*0.001f,done ? "Done" : "Not done",ctx._preloadRange,maxPreloadDist);
    }
  }
  else if (ctx._state==counter++)
  {
    // most likely we have finished a substantial texture allocation / deallocation
    // moreover, we are non-interactive now and we will not mind a slight delay
    // we want the texture memory stats to get a picture of the current state
    GEngine->FlushMemory(false);
    return true;
  }
  return false;
}

extern GameValue GOnPreloadStarted;
extern GameValue GOnPreloadFinished;

void World::PreloadAroundCameraSync(float maxTime)
{
  Camera *cam = _scene.GetCamera();
  if (!cam)
    return;
  if (!_cameraEffect && !_cameraOn)
    return;
  _preloadAsked = false;

#if _ENABLE_PERFLOG
  LogF("<ignore>");
#endif

  // call event handler prior preload screen shown
  if (!GOnPreloadStarted.GetNil())
  {
    GameState *state = GetGameState();
    if (state)
    {
      GameVarSpace vars(false);
      int gshandle = state->BeginContext(&vars);
      state->VarSetLocal("_maxTime", maxTime, true);

#if USE_PRECOMPILATION
      if (GOnPreloadStarted.GetType() == GameCode)
      {
        GameDataCode *code = static_cast<GameDataCode *>(GOnPreloadStarted.GetData());
        if (code->IsCompiled() && code->GetCode().Size() > 0)
          state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
      }
      else
#endif
      if (GOnPreloadStarted.GetType() == GameString)
      {
        // make sure string is not destructed while being evaluated
        RString code = GOnPreloadStarted;
        if (code.GetLength() > 0)
          state->EvaluateMultiple(code, GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
      }
      state->EndContext(gshandle);
    }
  }

  // progress during preload - try to use _loadingScreen if available
  IProgressDisplayRef display = _loadingScreen;
  if (!display)
  {
    IProgressDisplay *CreateDisplayProgress(RString text);
    display = CreateDisplayProgress(LocalizeString(IDS_LOAD_WORLD));
  }

  if (_mode == GModeNetware)
  {
    // initiate a preload, but the preload needs to be asynchronous
    // we need to make sure simulation runs and server requests are handled in a timely manner
    // this is esp. important for BattlEye anti-cheat
    _showMapUntil = GlobalTickCount()+toInt(1000*maxTime);
    SetShowMapProgress(display);
    _preloadAsyncCtx = new PreloadAsyncContext(maxTime, true);
    // show a progress, but not a map
    _showMap = false;
    return;
  }

  // TODO: implement Sync using Async, or always use Async
  FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
  FreeOnDemandFrame();

  // make sure initial camera is processed
  InitCameraPos();

  // if there is any async context, we may discard it now
  _preloadAsyncCtx.Free();
  float maxShadowDist = _scene.GetObjectShadows() ? Glob.config.GetMaxShadowSize()+Glob.config.GetShadowZ() : 0;
  // always preload in some given range around the camera, even when not visible
  const float minPreloadRange = 600.0f;
  float maxPreloadDist = floatMax(minPreloadRange,floatMax(Glob.config.objectsZ,maxShadowDist));
  float maxIterTime = floatMin(0.2f,maxTime);
  SectionTimeHandle section = StartSectionTime();
  // no time management during sync. preloading
  TimeManagerDisabled disableTM;
  Ref<ProgressHandle> p = ProgressStart(display);
  p->Add(maxTime);
  
  { // TODO: merge with the same function in CalculateLandShadows
    Vector3Val pos = _scene.GetCamera()->Position();
    GridRectangle camRect;
    // do not use ObjRadiusRectangle, we handle out of range coordinates well here
    float radius = _scene.GetFogMaxRange();
    float xMinF = pos.X()-radius;
    float xMaxF = pos.X()+radius;
    float zMinF = pos.Z()-radius;
    float zMaxF = pos.Z()+radius;
    camRect.xBeg=toIntFloor(xMinF*InvObjGrid);
    camRect.xEnd=toIntCeil(xMaxF*InvObjGrid);
    camRect.zBeg=toIntFloor(zMinF*InvObjGrid);
    camRect.zEnd=toIntCeil(zMaxF*InvObjGrid);
#if _VBS3_LAND_SHADOW
    _scene.CalculateLandShadows();
#else
    _scene.CalculateLandShadows(camRect,true);
#endif
  }
  
  float progressSet = 0;
  // gradually increase range - first of all preload close data
  float preloadRange = 100.0f;
  float rangeReported = 0;
  bool done = false;
  do
  {
    float progress = GetSectionTime(section);
    // if it takes significant amount of time, display a progress bar
    p->Advance(progress-progressSet);
    progressSet = progress;
    ProgressRefresh();
    // if we lost the device, we can no longer preload
    // if the device was reset, we consume the reset and continue preloading
    if (GEngine->IsAbleToDraw(true)==ATDUnable) break;
    I_AM_ALIVE();
    // regular framing should be done here like we are running
    FreeOnDemandFrame();
    FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
    GTimeManager.Frame(maxIterTime);

    // Simulate scripts (enable them to do their own preload)
    SimulateScripts(0, 0.05f);

    // we pretend we are rendering the scene
    // this should prepare almost everything, and initialize complexity management
    PrepareDraw(GetCamInsideVehicle(),true,preloadRange);
    Scene::DrawContext drawContext;
    _scene.BeginObjects();
    _scene.DrawPreparation(drawContext,0.0f,false);
    _scene.EndObjects();
    // context may require cleanup
    _scene.ObjectsCleanUp(drawContext);
    _scene.LightsCleanUp();
    GEngine->FinishPresent(); // needed so that we can call PerformMipmapLoading
    GEngine->FinishFinishPresent(); // needed so that we can call PerformMipmapLoading
    // we need to commit texture preloads here
    GEngine->TextBank()->PerformMipmapLoading();
#if ANIMATION_RT_SLIST
    // load also all needed animations
    PerformAnimationRTLoading();
#endif

#if _ENABLE_PERFLOG
    bool distlogging = true;
    bool logging = CompareSectionTimeGE(section,maxTime-1);
#else
    const bool distlogging = false;
    const bool logging = false;
#endif
    // preload 1st person view of the player's entity
    done = Preload1stPerson(logging) && PreloadBasicData(logging)
      && GLandscape->PreloadData(cam->Position(),preloadRange,maxIterTime,0.2f,CameraOn(),true,FLT_MAX,logging);
    // if successful, increase range
    if (done && preloadRange<maxPreloadDist)
    {
      if (distlogging && (preloadRange>rangeReported*1.5f || preloadRange>rangeReported+300.0f))
      {
        rangeReported = preloadRange;
        LogF("  %.1f: loaded %g m of %g m",GetSectionTime(section),preloadRange,maxPreloadDist);
      }
      preloadRange *= 1.2f;
      done = false;
      if (preloadRange>maxPreloadDist)
        preloadRange = maxPreloadDist;
    }
    if (_objAroundCam->LeftToLock()>0)
    {
      done = false;
      if (!done && logging)
        LogF(" Not done:Locking object around camera, %d left (of %d)", _objAroundCam->LeftToLock(),_objAroundCam->AreaSize());
    }
  } while (!done && !CompareSectionTimeGE(section,maxTime));
  
  LogF("Sync preload time: %g (limit %g) %s, loaded %g m of %g m", GetSectionTime(section),maxTime,done ? "Done" : "Not done",preloadRange,maxPreloadDist);
  if (p)
    ProgressFinish(p);

  // call event handler after preload screen finished
  if (!GOnPreloadFinished.GetNil())
  {
    GameState *state = GetGameState();
    if (state)
    {
      GameVarSpace vars(false);
      int gshandle = state->BeginContext(&vars);
#if USE_PRECOMPILATION
      if (GOnPreloadFinished.GetType() == GameCode)
      {
        GameDataCode *code = static_cast<GameDataCode *>(GOnPreloadFinished.GetData());
        if (code->IsCompiled() && code->GetCode().Size() > 0)
          state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
      }
      else
#endif
        if (GOnPreloadFinished.GetType() == GameString)
        {
          // make sure string is not destructed while being evaluated
          RString code = GOnPreloadFinished;
          if (code.GetLength() > 0)
            state->EvaluateMultiple(code, GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
        }
        state->EndContext(gshandle);
    }
  }

  // most likely we have finished a substantial texture allocation / deallocation
  // moreover, we are non-interactive now and we will not mind a slight delay
  // we want the texture memory stats to get a picture of the current state
  GEngine->FlushMemory(false,false);
  // debugging aid for detecting textures which are not preloaded
#if 0 //_ENABLE_CHEATS
    StopLoadingTextures = true;
    StopLoadingModels = true;
#endif

#if _ENABLE_PERFLOG
  LogF("</ignore>");
#endif
}

void World::ClearInitMessage(EntityAI *veh)
{
  for (int i=0; i<_initMessages.Size(); i++)
    if (_initMessages[i]._vehicle == veh)
    {
      _initMessages.Delete(i);
      i--;
    }
}

void World::AddInitMessage(EntityAI *veh, RString init)
{
  int index = _initMessages.Add();
  _initMessages[index]._vehicle = veh;
  _initMessages[index]._init = init;
}

void World::ProcessInitMessages()
{
  GameState *state = GetGameState();

  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);
  for (int i=0; i<_initMessages.Size(); i++)
  {
    if (!_initMessages[i]._vehicle)
      continue;
    state->VarSetLocal("this", GameValueExt(_initMessages[i]._vehicle,GameValExtObject), true, true);
    state->Execute(_initMessages[i]._init, GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
  }
  state->EndContext(gshandle);

  for (int i=0; i<_initMessages.Size(); i++)
  {
    if (!_initMessages[i]._vehicle)
      continue;
    _initMessages[i]._vehicle->InitUnits();
  }

  _initMessages.Clear();
}

#if _ENABLE_CHEATS
void World::StartVideo(RString folderName, RString filePrefix, float frameDuration, float timeToReadResources)
{
  _videoCaptureFolderName = folderName;
  _videoCaptureFilePrefix = filePrefix;
  _videoCaptureFrameDuration = frameDuration;
  _videoCaptureTimeToReadResources = timeToReadResources;
  _videoCaptureFrameID = 0;
}

void World::StopVideo()
{
  _videoCaptureFolderName = RString("");
  _videoCaptureFilePrefix = RString("");
  _videoCaptureFrameDuration = -1.0f;
  _videoCaptureTimeToReadResources = -1.0f;
  _videoCaptureFrameID = -1;
}
#endif

void World::ResetTiming()
{
  if (GEngine)
    GEngine->ResetTimingVSync();
  _vsyncErrorApplied = 0;
}

void World::PreloadMission()
{
  _ambientLife->Init(*_scene.GetCamera());
  static float syncPreloadTime = 15.0f;
  PreloadAroundCameraSync(syncPreloadTime);
}

bool RaiseWeaponActions::CheckAndUpdate(InputInterface *input)
{
  // Hold action to raise/lower weapon during the action is active
  bool holdActive = (input->GetAction(UATempRaiseWeapon) > 0.5f);
  // Raise weapon for given amount of time
  bool tempRaise = false;
  if (_startTime)
  {
    DWORD now = ::GetTickCount();
    if (now>=_startTime && (now-_startTime)<_raiseWeaponTimeout)
      tempRaise = true;
    else
      _startTime = 0; //reset
  }
  // Toggle Raise/Lower
  if (input->GetActionToDo(UAToggleRaiseWeapon) && !holdActive) //eat the action but do not apply when temp action is hold
  {
    if (tempRaise)
    { // player expect toggle would lower the weapon
      _startTime = 0;  //stop tempRaise
      _toggleON = false;
    }
    else _toggleON = !_toggleON;
  }
  _weaponHoldActive = holdActive;

  return (holdActive || _toggleON || tempRaise);
}

/**
@param preload sometimes we want to forget the old state, but not preload the new one yet
 */
void World::OnInitMission(bool preload)
{
  DecFadeSemaphore(FadeMissionInit);
  if (preload)
  {
    // FIXME: nothing here?
  }
  _scene.GetLandscape()->OnInitMission();
  
  _endDialogEnabled = true;

  _showMiniMap = false;
  _showMiniMapToggle = false;

  Glob.header.aiFeatures = 0;
  Glob.header.EnableAIFeature(AIFCombatFormationSoft);
  Glob.header.EnableAIFeature(AIFAwareFormationSoft);
  
  // force recalculating HDR
  OnCameraChanged();
  
  ResetTiming();

  _cameraShakeManager.Clear();

  if (_ui)
    _ui->InitMission();
#if _ENABLE_CONVERSATION
  void ClearConversationHistory();
  ClearConversationHistory();
#endif
#if _ENABLE_CHEATS
  // make sure diags are not carried over from previous missions
  GDiagsShown.Reset();
#endif

  if (preload)
    PreloadMission();
}

void World::OnFinishMission()
{
  UnloadSounds();
  DestroyUserDialog();
  _scripts.Clear();
#if USE_PRECOMPILATION
  _scriptVMs.Clear();
#endif
  _FSMs.Clear();
  _nextFSMId = 1;
  _jvmFSMs.Clear();
  _nextJID = 1;
  _jEntities.reset();

  _userInputDisabled = 0;
  //save person for MP debriefing
  if(GetMode()  == GModeNetware && !GStats._playerInfo.unit)
    if(GetRealPlayer() && GetRealPlayer()->CommanderUnit()) 
      GStats._playerInfo.unit = GetRealPlayer()->CommanderUnit();

  SwitchCameraTo(NULL, GetCameraType(), true);
  SwitchPlayerTo(NULL);
  SetRealPlayer(NULL);
}

#if _ENABLE_CHEATS
  static bool EnableFadeInMission = true;
#endif

void World::FadeInMission()
{
#if _ENABLE_CHEATS
  if (!EnableFadeInMission)
    return;
#endif
  SetCutEffect(0, CreateTitleEffect(TitBlackIn, ""));
}

void World::InitClient()
{
  SetEndMode(CAST_TO_ENUM(EEndMode, EMContinue));
  _endMissionCheated = false;
  _endFailed = false;
  EnableRadio();
  EnableSentences();

  // ItemTransmitters helpers
  _nearbyItemTransmitters.Clear();
  _nearbyItemTransmittersChanged = false;
  _transmittersFreqSent.Clear();

  if (_ui)
    _ui->ResetHUD();
 
  AIGlobalInit();

  _camUseNVG = false;
  _camUseTi = false;
  _camTiIndex = 0;

  ExplicitEnvSound.name = "";
  ExplicitEnvSound.freq = ExplicitEnvSound.vol = 1;
  DisableEnvironment = false;

  // mode may have changed, as a result terrain grid may have changed as well
  _scene.GetLandscape()->OnTerrainGridChanged();
}

static Ref<EntityAIType> NewAIType(const char *name)
{
  Ref<EntityType> vType=VehicleTypes.New(name);
  if (!vType)
    return NULL;
  EntityAIType *type=dynamic_cast<EntityAIType *>(vType.GetRef());
  if (!type)
    ErrF("Type %s is not EntityAIType",(const char *)vType->GetName());
  return type;
}

static void GetPosArray(AutoArray<Vector3> &tgt, ParamEntryPar cfg)
{
  tgt.Resize(0);
  for (int i=0; i<cfg.GetSize()-1; i+=2)
  {
    float x = cfg[i];
    float z = cfg[i+1];
    float y = GLandscape->SurfaceYAboveWater(x,z);
    Vector3 &pos = tgt[tgt.Add()];
    pos = Vector3(x,y,z);
  }
}

void World::PreloadVehicles(bool progressRefresh)
{
  VehicleTypes.Init();
  // preload vehicle types
  _preloadedVType[VTypeStatic]=NewAIType("Static");
  _preloadedVType[VTypeBuilding]=NewAIType("Building");
  _preloadedVType[VTypeStrategic]=NewAIType("Strategic");
  _preloadedVType[VTypeNonStrategic]=NewAIType("NonStrategic");
  if(progressRefresh) ProgressRefresh();
  //_preloadedVType[VTypeObjective]=NewAIType("Objective");
  _preloadedVType[VTypeTarget]=NewAIType("Target");
  //_preloadedVType[VTypePrimaryObjective]=NewAIType("PrimaryObjective");
  //_preloadedVType[VTypeSecondaryObjective]=NewAIType("SecondaryObjective");
  _preloadedVType[VTypeAllVehicles]=NewAIType("AllVehicles");
  _preloadedVType[VTypeAir]=NewAIType("Air");
  _preloadedVType[VTypePlane]=NewAIType("Plane");
  _preloadedVType[VTypeShip]=NewAIType("Ship");
  _preloadedVType[VTypeBigShip]=NewAIType("BigShip");
  if(progressRefresh) ProgressRefresh();
  _preloadedVType[VTypeAPC]=NewAIType("APC");
  _preloadedVType[VTypeTank]=NewAIType("Tank");
  _preloadedVType[VTypeCar]=NewAIType("Car");
  if(progressRefresh) ProgressRefresh();
  _preloadedVType[VTypeMan]=NewAIType("Man");
  _preloadedVType[VTypeHelipad]=NewAIType("HeliH");
  _preloadedVType[VTypePaperCar]=NewAIType("PaperCar");
  _preloadedVType[VTypeFireSectorTarget]=NewAIType("FireSectorTarget");
  if(progressRefresh) ProgressRefresh();

  _preloadedEType[ETypeMark]=VehicleTypes.New("#mark");
  _preloadedEType[ETypeSmoke]=VehicleTypes.New("#smoke");
  _preloadedEType[ETypeAnimator] = VehicleTypes.New("#animator");
  if(progressRefresh) ProgressRefresh();

  _preloadedType.Clear();
  ParamEntryVal list = Pars >> "PreloadVehicles";
  _preloadedType.Realloc(list.GetEntryCount());
  for (int i=0; i<list.GetEntryCount(); i++)
  {
    ParamEntryVal cls = list.GetEntry(i);
    if (!cls.IsClass())
      continue;
    if(progressRefresh) ProgressRefresh();
    Ref<EntityAIType> type = NewAIType(cls.GetName());
    if (!type)
      continue;
    int loadStyle = cls >> "loadStyle";
    _preloadedType.Add(PreloadedType(type,loadStyle));
    if (loadStyle >= 1)
      type->VehicleAddRef();
  }
  _preloadedType.Compact();
}

#if 0 // config example
class PreloadVehicles
{
  class SomeManClassName
  {
    loadStyle = 1; // make sure the shape is loaded as well, not only type
  };
};

#endif
void World::UnpreloadVehicles()
{
  for (int i=0; i<_preloadedType.Size(); i++)
  {
    PreloadedType &type = _preloadedType[i];
    if (type._style>=1)
    {
      type._type->VehicleRelease();
      type._type.Free();
    }
  }
  _preloadedType.Clear();
  for (int i=0; i<NPreloadedETypes; i++)
    _preloadedEType[i].Free();
  for (int i=0; i<NPreloadedVTypes; i++)
    _preloadedVType[i].Free();
}

void World::CreateMiniMaps()
{
  //In-game GPS minimap
  Display *map = new Display(NULL);
  map->Load("RscMiniMap");
  map->SetCursor(NULL);
  CStaticMapMain *mapCtrl = static_cast<CStaticMapMain*>(map->GetCtrl(IDC_MINIMAP));
  if (mapCtrl)
    mapCtrl->_miniMapMode = true; //show player icon even in veteran difficulty 
  _miniMap = map;
  //In-game GPS minimap small
  map = new Display(NULL);
  map->Load("RscMiniMapSmall");
  map->SetCursor(NULL);
  mapCtrl = static_cast<CStaticMapMain*>(map->GetCtrl(IDC_MINIMAP));
  if (mapCtrl) mapCtrl->_miniMapMode = true; //show player icon even in veteran difficulty 
  _miniMapSmall = map;
}

static void ReadAirport(ParamEntryVal entry, World::AirportInfo &info)
{
  // get ils position / direction
  ParamEntryVal ilsPosEntry=entry>>"ilsPosition";
  ParamEntryVal ilsDirEntry=entry>>"ilsDirection";
  info._ilsPos.Init();
  info._ilsDir.Init();
  info._ilsPos[0]=ilsPosEntry[0];
  info._ilsPos[2]=ilsPosEntry[1];
  // handling roads and water helps to artificial runways or water strips
  info._ilsPos[1] = GLandscape->RoadSurfaceYAboveWater(info._ilsPos[0],info._ilsPos[2]);
  info._ilsDir[0]=ilsDirEntry[0];
  info._ilsDir[1]=ilsDirEntry[1];
  info._ilsDir[2]=ilsDirEntry[2];
  info._ilsDir.Normalize();
  // by default assume the airport empty
  info._side = TSideUnknown;

  info._drawTaxiway = true;
  if (entry.FindEntry("drawTaxiway"))
    info._drawTaxiway = entry>>"drawTaxiway";

  // create taxi-in and taxi-off  paths 
  GetPosArray(info._taxiIn,entry>>"ilsTaxiIn");
  GetPosArray(info._taxiOff,entry>>"ilsTaxiOff");

  info._runwayBeg = info._ilsPos;
  Vector3 runwayDir(-info._ilsDir[0],0,-info._ilsDir[2]);
  runwayDir.Normalize();
  runwayDir.Normalize();
  Vector3 runwaySide(-runwayDir[2],0,runwayDir[0]);
  // check length based on taxi paths  
  // find taxipoint which is most distance in from the runwayBeg in runwayDir direction
  float maxDist = 0;
  float minDist = FLT_MAX;
  float maxAside = 0;
  // minDist ... maxDist is a taxiway span
  // for minDist we consider only points which are detected not to belong to the main runway
  for (int i=0; i<info._taxiOff.Size(); i++)
  {
    Vector3Val pos = info._taxiOff[i];
    float dist = (pos-info._runwayBeg).DotProduct(runwayDir);
    float aside = (pos-info._runwayBeg).DotProduct(runwaySide);
    if (maxDist<dist) maxDist = dist;
    if (fabs(aside)>5)
    {
      if (minDist>dist)
        minDist = dist;
    }
    if (fabs(maxAside)<fabs(aside))
      maxAside = aside;
  }
  for (int i=0; i<info._taxiIn.Size(); i++)
  {
    Vector3Val pos = info._taxiIn[i];
    float dist = (pos-info._runwayBeg).DotProduct(runwayDir);
    float aside = (pos-info._runwayBeg).DotProduct(runwaySide);
    if (maxDist<dist) maxDist = dist;
    if (fabs(aside)>5)
    {
      if (minDist>dist)
        minDist = dist;
    }
    if (fabs(maxAside)<fabs(aside))
      maxAside = aside;
  }

  // runway must leave some space for the taxiway
  info._runwayWidth = floatMin(40,fabs(maxAside)*0.8f);
  info._taxiwayWidth = floatMin(10,fabs(maxAside)*0.2f);

  info._runwayEnd = info._runwayBeg + runwayDir*maxDist;

  if (minDist<FLT_MAX)
  {
    info._taxiwayBeg = info._runwayBeg + runwayDir*minDist + runwaySide*maxAside;
    info._taxiwayEnd = info._runwayBeg + runwayDir*maxDist + runwaySide*maxAside;
  }
  else
  {
    info._taxiwayBeg = VZero;
    info._taxiwayEnd = VZero;
  }

}


#include "objectClasses.hpp"

void World::InitLandscape(Landscape *landscape)
{
  // remove all vehicles
  Clear();

  // if (!LandEditor || (Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
  if (Glob.header.worldname[0]!=0 && (Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
  {
    ParamEntryVal world=Pars>>"CfgWorlds">> Glob.header.worldname;
    InitGeneral(world);

    ProgressRefresh();

    _airports.Clear();
    // main airport needs to be always there
    ReadAirport(world,_airports.Append());
    // default airport is by default assumed unoccupied
    // check for secondary airports
    ConstParamEntryPtr moreAirports = world.FindEntry("SecondaryAirports");
    if (moreAirports)
    {
      // enumerate their sub-entries
      for (ParamClass::Iterator<> entry = moreAirports; entry; ++entry)
      {
        ParamEntryVal airport = *entry;
        if (!airport.IsClass()) continue;
        ReadAirport(airport,_airports.Append());
      }
    }
    _airports.Compact();
#if _VBS3
    SetUtmInfo(_utmInfo);
#endif
  }
  else
  {
    // we need to initialize the world so that it is kind of usable
    InitGeneral();
    // we want some camera which will show a reasonable picture
#if !_DEDICATED_CLIENT
    if ( !IsDedicatedServer() && !IsDedicatedClient() )
    {
      // 10 m above sea level could do
      Camera &camera = *_scene.GetCamera();

      camera.SetUpAndDirection(VUp,Vector3(1,0,0));
      camera.SetPosition(Vector3(0,10,0));
      camera.SetSpeed(VZero);
    }
#endif
    //InitEditor(landscape,NULL);
  }

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  RString skyTexture = Pars>>"CfgWorlds">>"DefaultWorld">>"skyTexture";  
  RString skyTextureR = Pars>>"CfgWorlds">>"DefaultWorld">>"skyTextureR";  
  skyTexture.Lower();
  skyTextureR.Lower();
  _scene.Init(_engine, landscape, skyTexture, skyTextureR);

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  ProgressRefresh();
  
  if (!_ui)
    _ui=CreateInGameUI();
  ProgressRefresh();

  GScene = GetScene();
  GLandscape = _scene.GetLandscape();
  // set elevation offset
  GLandscape->SetElevationOffset(_elevationOffset);
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  
  _ambientLife->Clear();
  
  /// if (!LandEditor || (Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
  if (Glob.header.worldname[0] && (Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
  {
    ParamEntryVal world=Pars>>"CfgWorlds">> Glob.header.worldname;
    _scene.GetLandscape()->InitDynSounds(world>>"Sounds");
    ProgressRefresh();
    _envSound = new EnvSoundManager(world>>"EnvSounds");
    ProgressRefresh();
  }

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  
  ProgressRefresh();

  // init all vehicles (load type information)
  InitCameraPos();
  _scene.GetLandscape()->InitObjectVehicles();
  //CreateMainOptions();

  ProgressRefresh();

  _showMap = false;
  ShowMap(false);
  _forceMap = false;
  _showMapforced = false;

#if _HELISIM_ROTORLIB
  _enableUserActions = true;
  _enableStressDamage = true;
#endif

  // CreateMainMap();
  Log("InitLandscape ResetIDs");
  ResetIDs();

  FreeOnDemandGarbageCollect(1024*1024,256*1024);

#if _ENABLE_PHYSICS
  //init physical world
  //if(GetNetworkManager().IsPhysicsMaster())
  {
    enf::PhysicsWorld* pw = landscape->GetPhysicsWorld();
    if(pw)
    {
      //odstran stary controller
      Object* te = landscape->GetTerrain();//->_terrEntity;
      if(te)
      {
        if( te->GetPhysics() )
	      {
          pw->DestroyBody(te);
        }

        pw->InitTerrain(landscape->GetTerrain());
      }
    }
  }
#endif
}

const GridInfo *World::GetGridInfo(float zoom) const
{
#if _VBS3_MAPGRIDIMPROVED

  // Calculate number of meters shown on the screen (in X direction - see where GetGridInfo was called)
  float sizeLand = LandGrid * LandRange;
  float numberOfMetersOnScreen = sizeLand * zoom;

  // Get the step threshold
  static float thresholdCoef = 1.0f / 3.0f;
  float stepXThreshold = numberOfMetersOnScreen * thresholdCoef;

  // Find grid item with biggest stepX but smaller than threshold (_gridInfo needs not to be sorted)
  int smallestItemIndex = 0;
  int smallestRightBelowThreshold = 0;
  for (int i = 1; i < _gridInfo.Size(); i++)
  {
    if (_gridInfo[i].stepX < _gridInfo[smallestItemIndex].stepX) smallestItemIndex = i;
    if (_gridInfo[i].stepX > _gridInfo[smallestRightBelowThreshold].stepX && _gridInfo[i].stepX < stepXThreshold) smallestRightBelowThreshold = i;
  }
  if (_gridInfo[smallestRightBelowThreshold].stepX < stepXThreshold)
  {
    return &_gridInfo[smallestRightBelowThreshold];
  }
  else
  {
    return &_gridInfo[smallestItemIndex];
  }

  // Old version of the previous paragraph relying on the _gridInfo to sorted according to stepX
  //   for (int i = _gridInfo.Size() - 1; i > 0; i--)
  //   {
  //     if (_gridInfo[i].stepX < stepXThreshold) return &_gridInfo[i];
  //   }
  //   return &_gridInfo[0];
#else
  // _gridInfo is always sorted ascending by zoomMax

  for (int i=0; i<_gridInfo.Size(); i++)
  {
    if (zoom <= _gridInfo[i].zoomMax)
      return &_gridInfo[i];
  }
  return NULL;
#endif //_VBS3_MAPGRIDIMPROVED
}


class RectangleObjectLock::ReleaseList
{
public:
  bool operator()(int x, int z) const
  {
    GLandscape->ReleaseObjects(x,z);
    return false;
  }
};

class RectangleObjectLock::ReleaseListBefore
{
  Position _pos;

public:    
  explicit ReleaseListBefore(const Position &pos):_pos(pos){}
  bool operator()(int x, int z) const
  {
    if (Position(x,z)<_pos)
      GLandscape->ReleaseObjects(x,z);
    return false;
  }
};

void RectangleObjectLock::ResetLockPos()
{
  // reset what is locked by _lockPos part of _requested but not by _locked

  _locked.ForEachInComplement(_requested,ReleaseListBefore(_lockPos));
  _lockPos = Position(_requested.xBeg,_requested.zBeg);
}

bool RectangleObjectLock::CheckLocked(const GridRectangle &rect) const
{
  if (rect.IsInside(_locked)) return true;
  // even when not contained in _locked, we might be covered by the lock in progress
  
  // when:
  if (rect.IsInside(_requested))
  {
    // once _lockPos has passed us, we are fully covered
    if (_lockPos.z>rect.zEnd || _lockPos.z==rect.zEnd && _lockPos.x>=rect.xEnd)
      return true;
  }
  return false;
  // 
}


void RectangleObjectLock::ChangeRectangle(const GridRectangle &rect)
{
  if (rect!=_requested)
  {
    ResetLockPos();
    // do not cancel old requests - let them expire
    _requested = rect;
    
    // we need to release locks on everything in _locked and not in _requested
    _requested.ForEachInComplement(_locked,ReleaseList());
    // what was locked is still locked
    _locked = _locked&_requested;
    _lockPos = Position(_requested.xBeg,_requested.zBeg);
  }
}

int RectangleObjectLock::LeftToLock() const
{
  // _lockPos is from _requested.xBeg,_requested.zBeg
  if (!_lockPos.IsInside(_requested))
    return 0;
  // lines left - lines width + left in the current line
  return (_requested.zEnd-_lockPos.z)*_requested.Width()+(_requested.xEnd-_lockPos.x);
}

bool RectangleObjectLock::DoWork()
{
  // check if there are some requests
  if (!_lockPos.IsInside(_requested))
  {
    // no more requests: everything is locked
    _locked = _requested;
    return true;
  }
  
  int requests = _maxRequests; // count slow preload requests
  int fields = _maxFields; // count all preload requests (do not count fields not in range)
  
  // pos is tracking _lockPos, unless some preload request is not completed
  // in that case _lockPos stays pointing the first incomplete request
  // note: once some field is skipped, condition pos==_lockPos will not be satisfied any more
  Position pos = _lockPos;
  bool allComplete = true;
  for(;;)
  {
    if (pos.IsInside(_locked))
    {
      // if it was already locked, skip it now
      if (pos==_lockPos)
        _lockPos = _lockPos.Advance(_requested);
    }
    else
    {
      Landscape::PreloadResult res = Landscape::PreloadFast;
      if (InRange(pos.z,pos.x))
      {
        res = GLandscape->PreloadObjects(FileRequestPriority(100),pos.x,pos.z);
        if (res!=Landscape::PreloadRequested)
        {
          if (pos==_lockPos)
          {
            // field is ready, we can advance
            // all fields before _lockPos need to be locked
            GLandscape->AddRefObjects(_lockPos.x,_lockPos.z);
            _lockPos = _lockPos.Advance(_requested);
            // sometimes we could extend _lockPos now
          }
        }
        else
          allComplete = false;
        if (--fields<=0)
          break;
      }
      else
        _lockPos = _lockPos.Advance(_requested);

      if (res!=Landscape::PreloadFast)
      {
        // too many slow requests, terminate
        if (--requests<=0)
          break;
      }
      
    }

    pos = pos.Advance(_requested);

    Assert((pos==_lockPos) == allComplete); // check invariant
    if (!pos.IsInside(_requested))
      // once we are out, we are done
      break;
  }
  // check if we have loaded everything
  if (!_lockPos.IsInside(_requested))
  {
    // no more requests: everything is locked
    _locked = _requested;
    return true;
  }
  return false;
}

/**
@param fast request no data be ever loaded, only test against the _objAroundCam rectangle

This function should be very efficient, as it uses information about grids which are
locked around the player
*/

bool World::CheckObjectsReady(int xMin, int xMax, int zMin, int zMax, bool fast) const
{
  // scope not needed - the only slow operation is PreloadObjects, which already has a scope
  //PROFILE_SCOPE(chORd);
  saturate(xMin,0,LandRangeMask);
  saturate(xMax,0,LandRangeMask);
  saturate(zMin,0,LandRangeMask);
  saturate(zMax,0,LandRangeMask);
  GridRectangle rect(xMin,zMin,xMax+1,zMax+1);
  // optimization - if grid is fully loaded, we can skip it
  if (_objAroundCam->CheckLocked(rect))
    return true;
  if (!fast)
  {
    for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
    {
      if (GLandscape->PreloadObjects(FileRequestPriority(100),x,z)==Landscape::PreloadRequested)
        return false;
    }
    return true;
  }
  return false;
}

static int CmpGridInfo(const GridInfo *i1, const GridInfo *i2)
{
  return sign(i1->zoomMax-i2->zoomMax);
}

void World::ParseCfgWorld(ParamEntryPar cls)
{
  //ParamEntryVal cls = Pars >> "CfgWorlds" >> Glob.header.worldname;
  
  _latitude = cls.ReadValue("latitude", -40.0f) *(H_PI/180); // -40 - Croatia, -90 - north pole;
  _longitude = cls.ReadValue("longitude", +15.0f) *(H_PI/180);
  _elevationOffset = cls.ReadValue("elevationOffset", +0.0f);
  
  // parse grid parameters
  ParamEntryVal clsGrid = cls >> "Grid";
  _gridOffsetX = clsGrid >> "offsetX";
  _gridOffsetY = clsGrid >> "offsetY";
  _gridInfo.Resize(0);
  for (int i=0; i<clsGrid.GetEntryCount(); i++)
  {
    ParamEntryVal entry = clsGrid.GetEntry(i);
    if (entry.IsClass())
    {
      int index = _gridInfo.Add();
      GridInfo &info = _gridInfo[index];
      info.zoomMax = entry >> "zoomMax";
      info.format = entry >> "format";
      info.formatX = entry >> "formatX";
      info.formatY = entry >> "formatY";
      info.stepX = entry >> "stepX";
      info.stepY = entry >> "stepY";
      info.invStepX = info.stepX == 0 ? 0 : 1.0f / info.stepX;
      info.invStepY = info.stepY == 0 ? 0 : 1.0f / info.stepY;
    }
  }
  _gridInfo.Compact();

  if (_gridInfo.Size()<=0)
  {
    RptF("No grid info given in %s",cc_cast(clsGrid.GetContext()));
  }
  QSort(_gridInfo,CmpGridInfo);

  // force sun light to load corresponding info
  _scene.MainLight()->Load(cls>>"Lighting");

#if _VBS3 //read in new UTM INFO
  _utmInfo.Reset();
  _waterLevel = cls.ReadValue("waterlevel", 0);

  ConstParamEntryPtr entry = cls.FindEntry("UTM");
  if(entry && entry->IsClass())
  {
    _utmInfo.easting = (*entry) >> "west" ;
    _utmInfo.northing = (*entry) >> "south" ;
    _utmInfo.zone = (*entry) >> "zone" ;
    RString hemi = (*entry) >> "hemisphere";
    _utmInfo.north = (strcmpi(hemi, "n") == 0);
  }
  else
    LogF("Warning: no UTM entry found in map: %s", cc_cast(Glob.header.worldname));
#endif
}

#if _VBS3 
//setOrigin changes the current UTM info and repositions the grid
//also sends it to hla if running
void World::SetUtmInfo(UTMInfo utmInfo)
{
  if(utmInfo.easting != -1)
  {
    //Change map grid offset
    _gridOffsetX = -utmInfo.easting;

    float sizeLand = LandGrid * LandRange;
    _gridOffsetY = utmInfo.northing + sizeLand;
  }
  Glob.header.utmInfo = utmInfo;
}
#endif

#if _VBS2 // key bindings
int World::AddKeyBinding(RString keycode,RString command)
{
	// find keycode
	int code = -1;

	for( int i=0; i<UAN; i++ )
		if (!strcmpi(keycode,GInput.userActionDesc[i].name))
		{
			code = i;
			break;
		}

	// code not found?
	if (code == -1) return -1;

	int index = _boundkeys.Add();
	KeyBinding &binding = _boundkeys[index];

	binding.userKeyCode = (UserAction) code;
	binding.command = command;
	binding.ID = _boundKeyCount++;

	return binding.ID;
}

//version based on the DIK code
int World::AddKeyBinding(int code,RString command)
{
  int index = _boundkeys.Add();
  KeyBinding &binding = _boundkeys[index];

  binding.userKeyCode = code;
  binding.command = command;
  binding.ID = _boundKeyCount++;
  binding.isDik = true;

  return binding.ID;
}

void World::RemoveKeyBinding(int index)
{
	for( int i=0; i<_boundkeys.Size(); i++ )
	{
		if (index == _boundkeys[i].ID)
		{
			_boundkeys.Delete(i);
			break;
		}
	}

	return;
}
#endif


extern RString GBriefingOnPlan;
extern RString GBriefingOnNotes;
extern RString GBriefingOnGroup;
extern GameValue GMapOnSingleClick;
extern GameValue GMapOnSingleClickParams;
extern GameValue GOnCommandModeChanged;
extern GameValue GNetworkPlayerConnected;
extern GameValue GNetworkPlayerDisconnected;
extern GameValue GOnFrameEventHandler;// this number is dependent on CPU/HW T&L performance

// at this moment no FSMs should be loaded
struct VerifyFSMNotUsed
{
  bool operator () (const FSMScriptedType *fsmType) const
  {
    DoAssert(fsmType->RefCounter()==1);
    return false;
  }
};


void ResetGlobalEventHandlers()
{
  GBriefingOnPlan = RString();
  GBriefingOnNotes = RString();
  GBriefingOnGroup = RString();
  GMapOnSingleClick = GameValue();
  GMapOnSingleClickParams = GameValue();
  GOnCommandModeChanged = GameValue();
  GOnPreloadStarted = GameValue();
  GOnPreloadFinished = GameValue();
  GOnFrameEventHandler = GameValue();

  void ResetPlayerEventHandlers();
  ResetPlayerEventHandlers();
}

void World::CleanUpDeinit()
{
  MoveOutAndDelete(_vehicles);
  MoveOutAndDelete(_animals);
  MoveOutAndDelete(_fastVehicles);
  MoveOutAndDelete(_slowVehicles);
  MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);

  TerminateVideoBuffers();

  #if _ENABLE_CHEATS
    // make sure diags are not carried over from previous missions
    GDiagsShown.Reset();
  #endif
  // clean up world - delete any simulation results, states and temps
  _timeToSkip=0;

  _cameraEffect.Free();
  Log("CleanUp world");

  // title and cut effect should be destroyed when closing the missions
  // they may be used for ProgressScript
  extern Ref<Script> ProgressScript;
  if (!ProgressScript)
  {
    _titleEffect.Free();
    _cutEffects.Clear();
    if (GSoundScene)
      GSoundScene->StopMusicTrack();
  }

  _scripts.Clear();
#if USE_PRECOMPILATION
  _scriptVMs.Clear();
#endif
  _FSMs.Clear();
  _nextFSMId = 1;
  _jvmFSMs.Clear();
  _nextJID = 1;
  _jEntities.reset();
  _playerSuspended = false;

  _sensorList.Free();
  _sensorList=new SensorList;
  _objAroundCam.Free();
  _objAroundCam = new RectangleObjectLock(/*fields*/100);
  // clear all mission specific global variables
  _missionNamespace->Reset();

  ResetGlobalEventHandlers();
  
  AIGlobalCleanUp();
  
  _postponedEvents.Clear();

  _scene.CleanUp();
  Scene::DrawContext dummy;
  // we call this function to make sure lights no longer reference the scene  
  _scene.LightsCleanUp();

#if _HELISIM_RENDER_TO_TEXTURE
  RemoveR2TSourceCameras();
#endif

  // destroy all non-primary vehicle objects? Not really needed, but we need to cleanup them

  ForEachVehicle(CallVoidObjMember<Entity>(&Entity::CleanUp));
  ForEachAnimal(CallVoidObjMember<Entity>(&Entity::CleanUp));

  // buildings have no simulation, and therefore cannot have any cleanup
  ForEachSlowVehicle(CallVoidObjMember<Entity>(&Entity::CleanUp));

  // clear all vehicles
  _vehicles.Clear();
  _fastVehicles.Clear();
  _animals.Clear();
  //_buildings.Clear();
  _slowVehicles.Clear();
  _verySlowVehicles.Clear();
  _verySlowToSimulate = 0;
  _outVehicles.Clear();
  _cloudlets.Clear();
  _miniMap.Free();
  _miniMapSmall.Free();
  _targetCategories.Reset();
  
#if _ENABLE_INDEPENDENT_AGENTS
  _agents.Clear();
  _teams.Clear();
#endif

  _artilleryEnabled = true;
  _enableItemsDropping = true;

  _moveMinBordersHUD = VZero;
  _moveMaxBordersHUD = VZero;
  _movePositionHUD = VZero;
  _targetHUD = NULL;

  _locations.Clear();

  //FSMScriptedTypes.ForEachF(VerifyFSMNotUsed());
  FSMScriptedTypes.Clear();
  // all vehicles are deleted - delete the area
  _ambientLife->Clear();

  extern RefArray<Object> MapDiags;
  MapDiags.Clear();

  _ppeManager.Clear();

  _cameraShakeManager.Clear();
}

void ResetErrors();

void World::CleanUpInit()
{
  _mode = GModeArcade;
  ResetNativeEvents();

  ResetErrors();

  // DestroyMap(IDC_OK);
  _showMap = false;
  ShowMap(false);
  _forceMap = false;
  _showMapforced = false;
  // delete all pilots

#if _HELISIM_ROTORLIB
  _enableUserActions = true;
  _enableStressDamage = true;
#endif


  // reset all variable parameters
  Glob.clock.SetTime(8 * OneHour, 130 * OneDay, 1985);
	Glob.clockWeather.SetTime(8 * OneHour, 130 * OneDay, 1985);

  _scene.MainLight()->Recalculate(this);
  _scene.MainLightChanged();
	_scene.ResetLensFlaresTime();

#if _VBS3_LAND_SHADOW
  _scene.ResetLandShadows(true);
#else
  _scene.ResetLandShadows();
#endif

  for (int i=0; i<MaxCameraType; i++)
    _camMaxDist[i] = 1e10; // smooth camera distance
  

  const MissionHeader* mh = GetNetworkManager().GetMissionHeader();

	if (mh && mh->_environmentTimeMul > 0.01)
	{
		Glob.clockWeather.SetTimeMultiplier(mh->_environmentTimeMul);
	}
	else
	{
		Glob.clockWeather.SetTimeMultiplier(1.0f);
	}

  if(mh && mh->_forceCameraType)
  {
    GInput.perspective= 1;
  }

  if (GInput.perspective==1) //first person view
  {
    SetCameraType(CamInternal);
    _cameraExternal = false;
  }
  else
  {
    SetCameraType(CamExternal);
    _cameraExternal = true;
  }

  //if (_editor) _camType=_camTypeMain=CamInternal;
  
  _cameraEffect = NULL;

  _playerManual=false;
  _playerSuspended=false;
  
  _acceleratedTime=1.0;

  // Glob.time is serialized during Load, so we do must not reset it again
  if (!IsInsideSerialization())
    Glob.time=Time(0);
  _channelChanged = UITIME_MIN;

  //FIX: cheat variables present even in Retail, but only as readOnly and having always false values
  //     see news:gp2o3h$ffd$1@new-server.localdomain
  for (int i=0; i<=9; i++)
  {
    BString<64> varName;
    sprintf(varName,"cheat%d",i);
    GetGameState()->VarSet(varName, false, false, false, GetMissionNamespace()); // mission namespace
  }
#if _VBS3 // support for dynamic briefing
  _alternateBriefing = RString();
#endif

  // good time to clean up allocator
  // now there should quite a lot of free memory
#ifdef _WIN32
  FastCAlloc::CleanUpAll();
#endif
}

void World::CleanUp()
{
  CleanUpDeinit();
  CleanUpInit();
}

void World::Reset()
{

  // restore initial world state (as after Load)
  CleanUp();
  _scene.GetLandscape()->InitObjectVehicles();
  
#if _VBS3 
  _lockTitleEffect = false;
	_boundKeyCount = 0;
	_boundkeys.Clear();
  _allowMovementCtrlsInDlg = false;
#endif


#if _VBS2 // death message difficulty option
  _friendlyFireMsgSideOnly = false;
#endif

  ParamEntryVal cfgWorlds = Pars>>"CfgWorlds";
  
  if (*Glob.header.worldname)
  {
    ConstParamEntryPtr world= cfgWorlds.FindEntry(Glob.header.worldname);
    if (world)
      _scene.GetLandscape()->InitDynSounds((*world)>>"Sounds");
  }

  if (GSoundScene)
	GSoundScene->Reset();

  _ambientLife->Clear();
}

void World::DeleteAnyVehicle(Entity *vehicle)
{
  EntitiesDistributed *list=vehicle->GetList() ? vehicle->GetList()->GetParent() : NULL;
  if (list)
    list->Delete(vehicle);
  else
  {
    LogF("DeleteAnyVehicle: Unknown list (%s)", (const char *)vehicle->GetDebugName());
    DeleteVehicle(vehicle);
  }
}

void World::RemoveAnyVehicle(Entity *vehicle)
{
  EntitiesDistributed *list=vehicle->GetList() ? vehicle->GetList()->GetParent() : NULL;
  if (list)
    list->Remove(vehicle);
  else
  {
    LogF("RemoveAnyVehicle: Unknown list (%s)", (const char *)vehicle->GetDebugName());
    RemoveVehicle(vehicle);
  }
}

#if _ENABLE_CHEATS
class CheckNullBrains
{
public:
  static int _count;
  CheckNullBrains() { _count = 0; }

  __forceinline bool operator()(Entity *veh) const
  {
    Person *pers = dyn_cast<Person>(veh);
    if (pers && !pers->Brain())
    {
      LogF("   person with no brain: %s", cc_cast(veh->GetDebugName()));
      _count++;
    }
    return false; //continue
  }
};

int CheckNullBrains::_count = 0;
#endif

//  ScanPlayers note: incremental maintenance is used
//  it is based on remotePlayer
//     _remotePlayer can be set via functions - (by owner) or by network transfer
//     but SetRemotePlayer function should be always called!
void World::ScanPlayers(StaticArrayAuto< OLink(Person) > &players)
{
  players.Resize(0);
  for (int i=0; i<_clientCameraPositions.Size(); i++)
  {
    // note: we add person to the list even when _clientCameraPositions[i]._cameraPosition!=InvalidCameraPos and camera is outside the Person body
    if (_clientCameraPositions[i]->_person)
    {
      Person *person = _clientCameraPositions[i]->_person;
      players.Add(person);
    }
  }
  Person *person = _playerOn;
  if (person) players.Add(person);
}

/// functor for abrupt movement notification

  class EntityMovedFar
  {
    EntityAI *_obj;
    
  public:
    EntityMovedFar(EntityAI *obj):_obj(obj){}
    
    __forceinline bool operator () (Entity *veh) const
    {
      EntityAIFull *ai = dyn_cast<EntityAIFull>(veh);
      if (!ai)
        return false;
      AIBrain *unit = ai->CommanderUnit();
      if (!unit)
        return false;
      TargetList *res = unit->AccessTargetList();
      if (!res)
        return false;
      ai->AddNewTarget(*res,false,_obj);
      return false;
    }
  };

void World::OnEntityMovedFar(EntityAI *obj)
{
  // process all entities and let them react to this one
  EntityMovedFar doThis(obj);
  ForEachVehicle(doThis);
}

/// functor for get in vehicle notification
class EntityGetIn
{
  EntityAI *_obj;
  Transport *_trans;
  
public:
  EntityGetIn(EntityAI *obj, Transport *trans):_obj(obj),_trans(trans){}
  
  __forceinline bool operator()(Entity *veh) const
  {
    EntityAIFull *ai = dyn_cast<EntityAIFull>(veh);
    if (!ai)
      return false;
    AIBrain *unit = ai->CommanderUnit();
    if (!unit)
      return false;
    TargetList *res = unit->AccessTargetList();
    if (!res)
      return false;
    // if we have seen the unit, we should reconsider the target side
    ai->ReactToGetIn(*res,_obj,_trans);
    return false;
  }
};

void World::OnEntityGetIn(EntityAI *soldier,Transport *trans)
{
  EntityGetIn doThis(soldier,trans);
  ForEachVehicle(doThis);
}

/// functor for World::OnKill

class EntityOnKill
{
  EntityAI *_obj;
  EntityAI *_killer;
  
public:
  EntityOnKill(EntityAI *killer, EntityAI *obj):_killer(killer),_obj(obj){}
  
  __forceinline bool operator()(Entity *veh) const
  {
    EntityAIFull *ai = dyn_cast<EntityAIFull>(veh);
    if (!ai)
      return false;
    AIBrain *unit = ai->CommanderUnit();
    if (!unit)
      return false;
    TargetList *res = unit->AccessTargetList();
    if (!res)
      return false;
    ai->CheckKiller(*res,_killer,_obj);
    return false;
  }
};


/** somebody is killed - let other units seeing this react */

void World::OnKill(EntityAI *killer, EntityAI *obj)
{
  // process all entities and let them react to this one
  EntityOnKill kill(killer,obj);
  ForEachVehicle(kill);
}

bool IsOutOfMemory();

bool IsAppPaused();

World::~World()
{
  SetShowMapProgress(NULL);
  Ref<ProgressHandle> p;
  if (!IsOutOfMemory() && !IsAppPaused())
  {
    ProgressReset();
    p = ProgressStart(LocalizeString(IDS_SHUTDOWN));
  }
  Log("World destruct");

  GCustomChannels.ClearCustomChannels();

  _displays.Clear();
  _userDlg.Free();
  _options.Free();
  _channel.Free();
  _chat.Free();
  _voiceChat.Free();
  _warningMessage.Free();
  // UI may contain various links to the world as well
  _ui.Free();
  
  _map.Free();
  _miniMap.Free();
  _miniMapSmall.Free();

  CleanUpDeinit();
  _profileNamespace->Reset();
  
  _scene.CleanUpDrawObjects();
  if (!IsOutOfMemory())
  {
    if (_scene.GetLandscape())
      _scene.GetLandscape()->Init(Pars >> "CfgWorlds" >> "DefaultWorld");
  }
  UnpreloadVehicles();
  if (p)
    ProgressFinish(p);

#if _HELISIM_ROTORLIB && !_DUMMY_ROTORLIB

#if _HELISIM_ROTORLIB_SIM_THREAD
  _rtdThread.Done();
#endif

  RTD::shutdown();
#endif

#if _ENABLE_PHYSICS
  if(GetNetworkManager().IsPhysicsMaster() && _pPhysicsManager)
    delete _pPhysicsManager;
#endif
}

void World::SetShowMapProgress(IProgressDisplay *progressDisplay)
{
  // Make sure the game doesn't get inputs when progress is displayed (or is restored when progress is not displayed)
  bool curSet = _showMapProgress.NotNull();
  bool newSet = progressDisplay != NULL;
  if (curSet != newSet)
  {
    if (newSet)
      GInput.ChangeGameFocus(+1);
    else
      GInput.ChangeGameFocus(-1);
  }
  // Set the progress object
  _showMapProgress = progressDisplay;
}

/// helper for World::DistributeImportances
class ChangeImportance
{
  EntitiesDistributed &_target;
  const Vector3 *_viewerPos;
  int _nViewers;
  SimulationImportance _prec;
  
public:
  ChangeImportance(EntitiesDistributed &target, SimulationImportance prec, const Vector3 *viewerPos, int nViewers)
  :_target(target),_prec(prec),_viewerPos(viewerPos),_nViewers(nViewers)
  {}
  __forceinline bool operator()(Entity *vehicle) const
  {
    const SimulationImportance maxPrecG=SimulateInvisibleFar;
    SimulationImportance iPrec = vehicle->CalculateImportance(_viewerPos,_nViewers);
    if (iPrec > maxPrecG)
      iPrec = maxPrecG;
    if (iPrec < SimulateVisibleNear)
      iPrec = SimulateVisibleNear;
    if (iPrec == _prec)
      return false;
    _target.Insert(iPrec,vehicle);
    return true;
  }
};

void World::DistributeImportances(EntitiesDistributed &target, EntityList &list,
  SimulationImportance prec, const Vector3 *viewerPos, int nViewers)
{
  // no items may be marked for handling, as redistribution invalidates indices
  // ForEachWithDelete would handle this on the removed side, but not on the receiving one
  DoAssert(!list.IsMoveOutOrDeletePending());
  // redistribute list into other lists
  list.ForEachWithDelete(ChangeImportance(target,prec,viewerPos,nViewers));
}

void World::GetViewerList(StaticArrayAuto<Vector3> &viewers)
{
  if (GetNetworkManager().IsServer())
  {
    // server watches all the players
    AutoArray< NetPlayerInfo> netplayers;
    GetNetworkManager().GetPlayers(netplayers);
    for (int i=0; i<netplayers.Size(); i++)
    {
      const NetPlayerInfo& npInfo = netplayers[i];
      if (npInfo.person)
      {
        viewers.Add(npInfo.person->GetCurrentPosition());
      }
    }
  }
  else
  {
    if (_cameraEffect)
    {
      viewers.Add(_cameraEffect->GetTransform().Position());
      return;
    }
    // SP: check which vehicle is followed by the camera
    if (_cameraOn!=NULL)
      viewers.Add(_cameraOn->RenderVisualState().Position());
    if (viewers.Size()<=0)
      viewers.Add(_scene.GetCamera()->Position());
  }
}

void World::DistributeNearImportances(EntitiesDistributed &list)
{
  // DistributeImportances cannot handle if there are any entities marked for processing - process them now
  MoveOutAndDelete(list);
  // redistribute vehicles into near...far lists
  _nearImportanceDistributionTime=Glob.time; // when distributions were calculated
  // redistribute vehicles into near...far lists
  AUTO_STATIC_ARRAY_16(Vector3,viewers,128)
  GetViewerList(viewers);
  DistributeImportances(list,list._visibleNear,SimulateVisibleNear,viewers.Data(),viewers.Size());
  DistributeImportances(list,list._visibleFar,SimulateVisibleFar,viewers.Data(),viewers.Size());
}

void World::DistributeFarImportances(EntitiesDistributed &list)
{
  // DistributeImportances cannot handle if there are any entities marked for processing - process them now
  MoveOutAndDelete(list);
  // redistribute vehicles into near...far lists
  // simulation of what is rest is done in Entity::SwitchImportance
  _farImportanceDistributionTime=Glob.time; // when distributions were calculated
  AUTO_STATIC_ARRAY_16(Vector3,viewers,128)
  GetViewerList(viewers);
  DistributeImportances(list,list._invisibleNear,SimulateInvisibleNear,viewers.Data(),viewers.Size());
  DistributeImportances(list,list._invisibleFar,SimulateInvisibleFar,viewers.Data(),viewers.Size());
}

void World::DistributeNearImportances()
{
  DistributeNearImportances(_vehicles);
  DistributeNearImportances(_animals);
  DistributeNearImportances(_slowVehicles);
}

void World::DistributeFarImportances()
{
  DistributeFarImportances(_vehicles);
  DistributeFarImportances(_animals);
  DistributeFarImportances(_slowVehicles);
}

void World::AddSensor(Person *vehicle)
{
  if (!vehicle->GetType()->ScanTargets())
    return;
  // TODO: check if the sensor will be used
  _sensorList->AddRow(vehicle);
}

void World::AddTarget(EntityAI *vehicle)
{
  if (!vehicle->GetType()->ScannedAsTarget())
    return;
  _sensorList->AddCol(vehicle);
}

void World::RemoveTarget(Entity *vehicle)
{
  // target should be removed when getting into any vehicle
  EntityAI *ai=dyn_cast<EntityAI>(vehicle);
  if (ai)
    _sensorList->DeleteCol(ai);
}

void World::RemoveSensor(Entity *vehicle)
{
  // this should be used when sensor is no longer present
  // like when getting into vehicle cargo, or being killed
  Person *driver=dyn_cast<Person>(vehicle);
  if (driver)
    _sensorList->DeleteRow(driver);
}

// suspending almost all activities of the vehicle (performance)
void World::SuspendEntity(Entity *vehicle)
{
#if defined SERVER_CORPSE_PERFORMANCE_PATCH
  //ErrF("SPAWN: Suspending %s after_refount=%i",(const char *)vehicle->GetDebugName(), vehicle->RefCounter());
  RemoveTarget(vehicle);
  RemoveSensor(vehicle);
  vehicle->VisualCut();
  vehicle->UnloadSound();

  //@TODO: move to slower vehicles _slowVehicles in entityList

  //@TODO: do not know if following works
  //vehicle->DisableSimulation(true);
#endif
}

void World::DeleteSuspendedEntity(Entity *vehicle)
{
#if defined SERVER_CORPSE_PERFORMANCE_PATCH
  _scene.GetLandscape()->RemoveObject(vehicle);
  //@TODO: remove vehicle moved near zombiescheduler, this could crash
  //GWorld->RemoveVehicle(vehicle);
#endif
}

void World::DeleteEntity(Entity *vehicle)
{
  // no need to reset the delete flag - the vehicle is removed from the list, and is going to be destructed anyway
  // by keeping it we inform anyone still holding a Ref to it that it is going to be destroyed soon
  //vehicle->ResetDelete();
  vehicle->UnloadSound();
  _scene.GetLandscape()->RemoveObject(vehicle);
  RemoveTarget(vehicle);
  RemoveSensor(vehicle);
}

void World::MoveOutEntity(Entity *vehicle)
{
  //vehicle->ResetMoveOut(); // flag will be reset after moving from Out list
  LogF("Moving %s from landscape",(const char *)vehicle->GetDebugName());
  //RptF("Moving %s from landscape",(const char *)vehicle->GetDebugName());
  DoAssert(vehicle->IsMoveOutInProgress());
  vehicle->SetList(NULL); // no longer in any list - we have moved it out
  vehicle->SetMoveOutFlag();
  // need to clean-up any caches before removing from the landscape
  vehicle->CleanUpMoveOut();
  _outVehicles.Add(vehicle);
  vehicle->UnloadSound();
  // interpolation accross move out not possible
  vehicle->VisualCut();
  _scene.GetLandscape()->RemoveObject(vehicle);
  RemoveTarget(vehicle);
  RemoveSensor(vehicle);
}

class World::MoveOutAndDeleteEntity
{
public:
  World *_world;
  
  explicit MoveOutAndDeleteEntity(World *world):_world(world){}
  bool operator()(Entity *vehicle) const
  {
    if (vehicle->ToDelete())
    {
      _world->DeleteEntity(vehicle);
      return true;
    }
    else if (vehicle->ToMoveOut())
    {
      _world->MoveOutEntity(vehicle);
      return true;
    }
    return false;
  }
};

struct NotifyDeleted
{
  int &keepIndex;

  NotifyDeleted(int &keepIndex):keepIndex(keepIndex) {}
  void operator()(int deleted) const {if (deleted<keepIndex) keepIndex--;}
};

void World::MoveOutAndDelete(EntityList &vehicles, int *keepIndex)
{
  // process the entities which are marked for deletion
  if (!keepIndex)
    vehicles.ForEachToMoveOutOrDelete(MoveOutAndDeleteEntity(this));
  else
  {
    vehicles.ForEachToMoveOutOrDeleteWithCallback(MoveOutAndDeleteEntity(this),NotifyDeleted(*keepIndex));
  }
}

void World::MoveOutAndDelete(EntitiesDistributed &list)
{
  MoveOutAndDelete(list._visibleNear);
  MoveOutAndDelete(list._visibleFar);
  MoveOutAndDelete(list._invisibleNear);
  MoveOutAndDelete(list._invisibleFar);
}

/// helper for World::SimulateOnly
class VehSimulate
{
  VehicleSimulation _simul;
  float _deltaT;
  SimulationImportance _prec;

public:  
  VehSimulate(VehicleSimulation simul,float deltaT, SimulationImportance prec)
  :_simul(simul),_deltaT(deltaT),_prec(prec)
  {}
  __forceinline bool operator()(Entity *vehicle) const
  {
    if (vehicle->IsAttached()) return false;
    if(vehicle->IsSimulationDisabled()) return false;

    Object::ProtectedVisualState<const ObjectVisualState> vs = vehicle->FutureVisualStateScope();

    if (vehicle->IsLocal())
    {
      vehicle->SetLastImportance(_prec);
    }
    (vehicle->*_simul)(_deltaT,_prec);
    return false;
  }
};

/// helper for World::SimulateOnly - multiplayer version
/** use remote precision for remote entities */

class VehSimulateMP
{
  VehicleSimulation _simul;
  float _deltaT;
  SimulationImportance _prec;

public:
  VehSimulateMP(VehicleSimulation simul, float deltaT, SimulationImportance prec)
  :_simul(simul),_deltaT(deltaT),_prec(prec)
  {}
  __forceinline bool operator()(Entity *vehicle) const
  {
    if (vehicle->IsAttached()) return false;
    Object::ProtectedVisualState<const ObjectVisualState> vs = vehicle->FutureVisualStateScope();

    if (vehicle->IsLocal())
    {
      vehicle->SetLastImportance(_prec);
      (vehicle->*_simul)(_deltaT,_prec);
    }
    else
    {
      SimulationImportance p = vehicle->GetLastImportance();
      if (p==SimulateDefault)
        p = _prec;
      (vehicle->*_simul)(_deltaT,p);
    }
    return false;
  }
};


void World::SimulateOnly(EntityList &vehicles, float deltaT, VehicleSimulation simul, SimulationImportance prec)
{
  // simulate all vehicles, cameras ...
  // calculate all simulation parameters
  // includes collision testing
  if (prec == SimulateVisibleNear && (GetMode()==GModeNetware))
  {
    vehicles.ForEach(VehSimulateMP(simul,deltaT,prec));
  }
  else
    vehicles.ForEach(VehSimulate(simul,deltaT,prec));
}

inline SimulationImportance MinPrec(SimulationImportance p1, SimulationImportance p2)
{
  return (p1 > p2) ? p1 : p2;
}

/**
@param minPrec entities with this and finer precision only are simulated with simul,
others are simulated with simulOver
*/
void World::SimulateOnly(EntitiesDistributed &vehicles, float deltaT, VehicleSimulation simul, VehicleSimulation simulOver, SimulationImportance minPrec)
{
  SimulateOnly(vehicles._visibleNear, deltaT, minPrec>=SimulateVisibleNear ? simul : simulOver, SimulateVisibleNear);
  SimulateOnly(vehicles._visibleFar, deltaT, minPrec>=SimulateVisibleFar ? simul : simulOver, SimulateVisibleFar);
  SimulateOnly(vehicles._invisibleNear, deltaT, minPrec>=SimulateInvisibleNear ? simul : simulOver, SimulateInvisibleNear);
  SimulateOnly(vehicles._invisibleFar, deltaT, minPrec>=SimulateInvisibleFar ? simul : simulOver, SimulateInvisibleFar);
}

void World::AddAttachment(JoinedObject *attach)
{
  //Log("Add attachment %x",attach);
  _attached.Add(attach);
}

void World::RemoveAttachment(JoinedObject *attach)
{
  //Log("Remove attachment %x",attach);
  int index = _attached.Find(attach);
  Assert(index >= 0);
  if (index >= 0)
    _attached.Delete(index);
}

/// helper functor for GetWindEmitterList
class SelectWindEmitters
{
  WindEmitterList &_list;
  Vector3 _pos;
  float _radius;
  
public: 
  SelectWindEmitters(WindEmitterList &list, Vector3Par pos, float radius)
  :_list(list),_pos(pos),_radius(radius)
  {}
  bool operator()(Entity *entity) const
  {
    if (!entity->EmittingWind(_pos,_radius)) return false;
    _list.Add(entity);
    return false;
  }
};

WindEmitterList::WindEmitterList()
{
  _windSlowSize = 0;
}
void WindEmitterList::Begin()
{
  _windSlowSize = GLandscape->GetWindSpeed().Size();
}

void World::BeginWindEmitterScope(const Camera *cam, float radius)
{
  ForEachVehicle(SelectWindEmitters(_windEmitterScope,cam->Position(),radius));
  _windEmitterScope.Begin();
}

void World::EndWindEmitterScope()
{
  _windEmitterScope.Clear();
}

const WindEmitterList &World::GetWindEmitterList(WindEmitterList &work, Vector3Par pos, float radius)
{
  // scan for potential emittors
  // currently only helicopters are considered
  ForEachVehicle(SelectWindEmitters(work,pos,radius));
  return work;
}

Vector3 World::GetWind(Vector3Par pos, const WindEmitterList &emittors)
{
  Vector3 base = GLandscape->GetWindSpeed();
  for (int i=0; i<emittors.Size(); i++)
    base = emittors[i]->WindEmit(pos,base);
  return base;
}

Vector3 World::GetWindSlow(Vector3Par pos, const WindEmitterList &emittors)
{
  Vector3 base = GLandscape->GetWindSpeedSlow();
  for (int i=0; i<emittors.Size(); i++)
  {
    const Entity *emittor = emittors[i];
    if (emittor)
      base = emittor->WindEmit(pos,base);
  }
  return base;
}

void EntityList::Add(Entity *object)
{
  // TODO: BUG vehicle listed twice
#if 1
    int index = FindKey(object);
    if (index >= 0)
    {
      LogF("Entity listed twice %s",(const char *)object->GetDebugName());
      Fail("Entity listed twice.");
      return;
    }
#endif

  object->StartFrame();
  Insert(object);
  GLOB_LAND->AddObject(object);
}

Entity *EntityList::Find(const ObjectId &id) const
{
  for (int v=0; v<Size(); v++)
  {
    Entity *obj = Get(v);
    if (obj->GetObjectId() == id)
      return obj;
  }
  return NULL;
}

LSError EntityList::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving())
  {
    RefArray<Entity> mustBeSaved;
    for (int i=0; i<Size(); i++)
    {
      Entity *veh = Get(i);
      // do not save primary objects
      if (veh->Object::GetType() == Primary)
        continue;
      if (!veh->MustBeSaved())
        continue;
      mustBeSaved.Add(veh);
    }
    CHECK(ar.Serialize("Vehicles", mustBeSaved, 1))
  }
  else
  {
    FreeOnDemandGarbageCollect(1024*1024,256*1024);
    CHECK(ar.Serialize("Vehicles", *(base *)this, 1))
    if (ar.GetPass() == ParamArchive::PassFirst)
    {
      for (int i=0; i<Size(); i++)
      {
        Entity *entity = Set(i);
        if (!entity)
          continue;
        entity->StartFrame();
        entity->SetList(this);
        GLOB_LAND->AddObject(entity);
      }
    }
    else
      base::RemoveNulls();
  }
  return LSOK;
}

/// reset any list information
class EntityResetList
{
  EntitiesDistributed *_list;
public:
  explicit EntityResetList(EntitiesDistributed *list):_list(list){}
  __forceinline bool operator()(Entity *obj) const
  {
    obj->SetList(NULL);
    return false;
  }
};

#pragma warning(disable:4355)
EntitiesDistributed::EntitiesDistributed()
:_visibleNear(this),_visibleFar(this),_invisibleNear(this),_invisibleFar(this) // connect each list with its parent
{}
#pragma warning(default:4355)

void EntitiesDistributed::Clear()
{
  // clear all list identifiers
  ForEach(EntityResetList(NULL));
  // clear all lists
  _visibleNear.Clear();
  _visibleFar.Clear();
  _invisibleNear.Clear();
  _invisibleFar.Clear();
}

void EntitiesDistributed::Add(Entity *vehicle)
{
  // add both to the list and into the landscape
  // always add to visible near list
  _visibleNear.Add(vehicle);
#if LOG_ADD_REMOVE_VEHICLE
  LogF("Vehicle %s added to list %x", (const char *)vehicle->GetDebugName(), this);
#endif
}

void EntitiesDistributed::Insert(Entity *vehicle)
{
  // add only to the list, not into the landscape
  //Fail("Obsolete");
  // always add to visible near list
  // TODO: make sure the SetList follows the distribution
  vehicle->StartFrame();
  _visibleNear.Insert(vehicle);
#if LOG_ADD_REMOVE_VEHICLE
  LogF("Vehicle %s inserted to list %x", (const char *)vehicle->GetDebugName(), this);
#endif
}

void EntitiesDistributed::Insert(SimulationImportance prec, Entity *vehicle)
{
  switch (prec)
  {
    case SimulateVisibleNear: _visibleNear.Insert(vehicle);break;
    case SimulateVisibleFar: _visibleFar.Insert(vehicle);break;
    case SimulateInvisibleNear: _invisibleNear.Insert(vehicle);break;
    case SimulateInvisibleFar: _invisibleFar.Insert(vehicle);break;
    default:
      Fail("Bad precision");
  }
}

bool EntitiesDistributed::IsPresent(Entity *vehicle) const
{
  // note: this function may be quite slow
  #define ONE_LIST(list) if (list.IsPresent(vehicle)) return true;
  ONE_LIST(_visibleNear)
  ONE_LIST(_visibleFar)
  ONE_LIST(_invisibleNear)
  ONE_LIST(_invisibleFar)
  return false;
}

void EntitiesDistributed::Delete(Entity *vehicle)
{
  Remove(vehicle);
  GLOB_LAND->RemoveObject(vehicle);
}

void EntitiesDistributed::Remove(Entity *vehicle)
{
  // note: this function may be quite slow
  if (!vehicle->GetList())
  {
    Fail("Deleting vehicle which is not present");
    return;
  }
  DoAssert(this == vehicle->GetList()->GetParent());
  int deleted=0;
  if (_visibleNear.Delete(vehicle))
    deleted++;
  if (_visibleFar.Delete(vehicle))
    deleted++;
  if (_invisibleNear.Delete(vehicle))
    deleted++;
  if (_invisibleFar.Delete(vehicle))
    deleted++;
  DoAssert(deleted == 1);
  vehicle->SetList(NULL);
}

class EntityCompareId
{
  ObjectId _id;
  Entity *&_result;
  
public:
  EntityCompareId(const ObjectId &id, Entity *&result)
  :_id(id),_result(result)
  {
    _result = NULL;
  }
  bool operator()(Entity *obj) const
  {
    if (obj->GetObjectId()==_id)
    {
      _result = obj;
      return true;
    }
    return false;
  }
  Entity *Result() const {return _result;}
};

const EntityList &EntitiesDistributed::Select(SimulationImportance sim) const
{
  switch (sim)
  {
    case SimulateVisibleNear: return _visibleNear;
    case SimulateVisibleFar: return _visibleFar;
    case SimulateInvisibleNear: return _invisibleNear;
    /*case SimulateInvisibleFar:*/ 
    default: return _invisibleFar;
  }
}

Entity *EntitiesDistributed::Find(const ObjectId &id) const
{
  Entity *result;
  EntityCompareId compare(id,result);
  ForEach(compare);
  return result;
}

LSError EntitiesDistributed::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("VisibleNear", _visibleNear, 1))
  CHECK(ar.Serialize("VisibleFar", _visibleFar, 1))
  CHECK(ar.Serialize("InvisibleNear", _invisibleNear, 1))
  CHECK(ar.Serialize("InvisibleFar", _invisibleFar, 1))
  // FIX: _list is set correctly already in EntityList::Serialize
/*
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
  {
    ForEach(EntityResetList(this));
  }
*/
  return LSOK;
}

void StaticEntityList::Clear()
{
  _list.Clear();
}

void StaticEntityList::Insert(const ObjectId &id, const EntityAIType *type, Vector3Par pos)
{
  StaticEntityLink link;
  OLinkL(Object) linkFromId = GLandscape->GetObjectNoLock(id);
  // we know link is to EntityAI
  link.SetLink(linkFromId);
  link._type = type;
  link._pos = pos;
  _list.Add(link);
}

void StaticEntityList::Remove(const ObjectId &id)
{
  Verify(_list.DeleteKey(id));
}

int StaticEntityList::Size() const
{
  return _list.Size();
}

OLinkL(Object) StaticEntityList::Get(int index) const
{
  return _list.Get(index);
}

const StaticEntityLink *Landscape::FindBuildingInfo(const ObjectId &id) const
{
  return _buildings.FindInfo(id);
}

void StaticEntityLink::ClearVirtualTarget() const
{
  _virtualTarget = NULL;
}

Target *StaticEntityLink::GetVirtualTarget() const
{
  // once the target is created, it will exist "forever", until cleaned
  if (_virtualTarget)
    return _virtualTarget;

  OLinkLPtr(Object) objLock = GetLock();
  EntityAI *ai = dyn_cast<EntityAI,Object>(objLock);
  if (!ai)
  {
    // check if object is marked as classed
    RptF("No AI type for object %s: %d,%d, property class='%s'",
      cc_cast(objLock->GetShape()->GetName()),GetId().GetObjX(),GetId().GetObjZ(),cc_cast(objLock->GetShape()->GetPropertyClass()));
    return NULL;
  }
  Assert(ai->IsInLandscape());

  _virtualTarget = new TargetMinimal(ai);
  return _virtualTarget;
}

const StaticEntityLink *StaticEntityList::FindInfo(const ObjectId &id) const
{
  int index = _list.FindKey(id);
  if (index<0)
    return NULL;
  return &_list.Get(index);
}

OLinkL(Object) StaticEntityList::Find(const ObjectId &id) const
{
  int index = _list.FindKey(id);
  if (index<0)
    return OLinkL(Object)();
  return _list.Get(index);
}

void World::SetActiveChannels()
{
  // FIX: Refs are used instead of simple pointers as _radio->SetAudible can possibly destroy a lot of structures
  //      see news:gu8uai$ue5$1@new-server.localdomain
  Ref<AIBrain> unit = FocusOn();

  bool enableRadio = _enableRadio;
  if (GetMode() != GModeNetware)
  {
    AIBrain *playerUnit = _realPlayer ? _realPlayer->Brain() : NULL;
    if (!playerUnit || !playerUnit->LSIsAlive())
      enableRadio = false;
  }

  // global channel
  _radio->SetAudible(enableRadio);

  // vehicle intercom
  Transport *transport = unit ? unit->GetVehicleIn() : NULL;
  if (enableRadio && transport && _mode != GModeIntro)
  {
    if (_channelVehicle != &transport->GetRadio())
    {
      if (_channelVehicle)
        _channelVehicle->SetAudible(false);
      _channelVehicle = &transport->GetRadio();
    }
    if (_channelVehicle)
      _channelVehicle->SetAudible(true);
  }
  else if (_channelVehicle)
  {
    _channelVehicle->SetAudible(false);
    _channelVehicle = NULL;
  }

  // custom channels
  for (int i=0; i<CCCustomN; i++)
  {
    RadioChannel &custom = GWorld->GetCustomRadio(i);
    if (!enableRadio) custom.SetAudible(enableRadio);
    else
    {
      custom.SetAudible(GCustomChannels.ChannelActive(CCCustom1+i));
    }
  }
}

void World::BrowseCamera(Entity *vehicle)
{
  if (vehicle == _cameraOn)
    return;
  _ui->ResetHUD();
  //Object *oldCamera=_cameraOn;
  //EntityAI *oldCamAI=dyn_cast<EntityAI>(oldCamera);
  _cameraOn=vehicle;
  SetActiveChannels();

  //Entity *trackV=_trackingCamera;
  //TrackingCamera *track=static_cast<TrackingCamera *>(trackV);
  //_trackingCamera->LinkTo(_cameraOn);
#if 1
    // leave AI/manual as it was
    /*
    if( oldCamAI )
    {
      #if !_RELEASE
        Log("BrowseCamera SetManual(false) %s",(const char *)oldCamAI->GetDebugName());
      #endif
      SetPlayerManual(false);
      AIUnit *brain=oldCamAI->CommanderUnit();
      if( brain ) brain->RefreshMission();
    }
    else
    {
      SeaGullAuto *gull=dynamic_cast<SeaGullAuto *>(oldCamera);
      if( gull ) gull->SetManual(false);
    }
    */
    EntityAIFull *ai = dyn_cast<EntityAIFull>(vehicle);
    if (ai && _ui)
      _ui->ResetVehicle(ai);
#endif
  // any camera switch  can result in considerable redistribution
  DistributeNearImportances();
  DistributeFarImportances();
}

struct NearestCamEntity
{
  Entity *nearestDown;
  Entity *nearestUp;
  Entity *mostDown;
  Entity *mostUp;
  
  NearestCamEntity()
  {
    nearestDown=NULL;
    nearestUp=NULL;
    mostDown=NULL;
    mostUp=NULL;
  }
};

class WorldBrowseCamera
{
  World *_world;
  NearestCamEntity &_res;
  
public:
  WorldBrowseCamera(World *world, NearestCamEntity &res):_world(world),_res(res){}
  bool operator () (Entity *vehicle) const
  {
    Object *camVehicle=_world->CameraOn();
    if (vehicle==camVehicle ) return false;
    //if (vehicle==_trackingCamera ) continue;
    if (vehicle->Static())
      return false;
    if (vehicle->GetType() == TypeTempVehicle)
      return false;
    if (vehicle->IsDamageDestroyed())
      return false;
    if (vehicle->IsAmbient())
      return false;
    EntityAI *vehicleAI = dyn_cast<EntityAI>(vehicle);
    if (!vehicleAI )
      return false;
    if (!vehicleAI->CommanderUnit())
      return false;
    if (vehicle > camVehicle)
    {
      if (!_res.nearestUp || vehicle<_res.nearestUp)
        _res.nearestUp = vehicle;
    }
    if (vehicle<camVehicle)
    {
      if (!_res.nearestDown || vehicle>_res.nearestDown)
        _res.nearestDown = vehicle;
    }
    if (!_res.mostUp || vehicle>_res.mostUp)
      _res.mostUp = vehicle;
    if (!_res.mostDown || vehicle<_res.mostDown)
      _res.mostDown = vehicle;
    return false;    
  }
};

void World::BrowseCamera(int dir)
{
#if 1
    NearestCamEntity res;
    ForEachVehicle(WorldBrowseCamera(this,res));
    if (dir > 0)
    {
      if (res.nearestUp)
        BrowseCamera(res.nearestUp);
      else if (res.mostDown)
        BrowseCamera(res.mostDown);
    }
    else if (dir < 0)
    {
      if (res.nearestDown)
        BrowseCamera(res.nearestDown);
      else if (res.mostUp)
        BrowseCamera(res.mostUp);
    }
#endif
}

void World::VehicleSwitched(Object *from, Object *to)
{
  if( _cameraEffect && _cameraEffect->GetObject()==from )
  {
    _cameraEffect->SetObject(to);
    _cameraEffect->Simulate(0);
  }
}

bool World::QueryObjects(ObjectQuery & query, Vector3Par origin, float radius, unsigned int queryFlags)
{
  float radius2 = Square(radius);

  int xMin, xMax, zMin, zMax;
  ObjRadiusRectangle(xMin, xMax, zMin, zMax, origin, radius);
  for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
  {
    const ObjectList &list = GLandscape->GetObjects(z, x);
    if(list.Null())
      continue;

    //TODO: filter based on 2D AAB vs circle
    bool checkStatic = false;
    if(queryFlags & World::QF_STATIC)
    {
      if(list->GetBSphereCenter().Distance2(origin) <= Square(radius + list->GetBSphereRadius()))
      {
        const Vector3 *minMax = list->GetMinMax();
        if(IsIntersectionSphereAABB(origin, Square(radius), minMax[0], minMax[1]))
          checkStatic = true;
      }
    }

    int from = checkStatic ? 0 : (list->Size() - list->GetNonStaticCount());
    int to = ((queryFlags & World::QF_DYNAMIC) == 0) ? (list->Size() - list->GetNonStaticCount()) : list->Size();

    for(int i = from; i < to; i++)
    {
      Object *obj = list[i];
      if (i < (to - 1))
        PrefetchT0Off(list[i + 1], 0);

      if(query.Check(obj))
      {
        Vector3Val pos = obj->WorldTransform(obj->FutureVisualState()).Position();
        float dist2 = pos.Distance2(origin);
        float locRadius = (queryFlags & QF_ORIGIN_DISTANCE) ? radius2 : Square(radius + obj->GetRadius()); 
        if (dist2 <= locRadius)
        {
					if(!query(obj))
						return false;
        }
      }
    }
  }
  return true;
}

namespace {
  bool isListNull (const ObjectListUsed & list) { return list.IsNull(); }
  bool isListNull (const ObjectList & list) { return list.Null(); }

  template <class T>
  bool queryHelper (const World::ObjectQuery & query, const T & list, Vector3Par mins, Vector3Par maxs, unsigned int queryFlags)
  {
    if (isListNull(list))
      return true;

    bool checkStatic = false;
    if (queryFlags & World::QF_STATIC)
    {
      const Vector3 * minMax = list->GetMinMax();
      if (IsIntersectionAABBAABB(mins, maxs, minMax[0], minMax[1]))
        checkStatic = true;
    }

    int from = checkStatic ? 0 : (list->Size() - list->GetNonStaticCount());
    int to = ((queryFlags & World::QF_DYNAMIC) == 0) ? (list->Size() - list->GetNonStaticCount()) : list->Size();

    for (int i = from; i < to; i++)
    {
      Object * obj = list[i];
      if (i < to - 1)
        PrefetchT0Off(list[i + 1], 0);

      if (query.Check(obj))
      {
        Vector3Val pos = obj->WorldTransform(obj->FutureVisualState()).Position();
        if (queryFlags & World::QF_ORIGIN_DISTANCE)
        {
          if (!IsIntersectionAABBPoint(mins, maxs, pos))
            continue;
        }
        else
        {
          if (!IsIntersectionSphereAABB(pos, Square(obj->GetRadius()), mins, maxs))
            continue;
        }
        if (!query(obj))
          return false;
      }
    }
    return true;
  }
}

bool World::QueryObjects(ObjectQuery & query, Vector3Par mins, Vector3Par maxs, unsigned int queryFlags)
{
  int xMin, xMax, zMin, zMax;
  ObjRectangle(xMin, xMax, zMin, zMax, mins, maxs, 0);
  for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
  {
    const bool cont = queryHelper(query, GLandscape->GetObjects(z, x), mins, maxs, queryFlags);
    if (!cont)
      return false;
  }
  return true;
}

bool World::UseObjects(ObjectQuery & query, Vector3Par mins, Vector3Par maxs, unsigned int queryFlags)
{
  int xMin, xMax, zMin, zMax;
  ObjRectangle(xMin, xMax, zMin, zMax, mins, maxs, 0);
  for (int x=xMin; x<=xMax; x++) for (int z=zMin; z<=zMax; z++)
  {
    const bool cont = queryHelper(query, GLandscape->UseObjects(x, z), mins, maxs, queryFlags);
    if (!cont)
      return false;
  }
  return true;
}

bool World::QueryObjects(ObjectQuery& query, Vector3Par origin, float radius, Vector3Par direction, float fov, unsigned int queryFlags)
{
  //TODO: implementation
  return true;
}

void World::SwitchCameraTo(Object *vehicle, CameraType camType, bool cameraHasChanged)
{
#if _VBS3
  // Reset the camera target
  _cameraTarget = NULL;
#endif

  //bool reset=( _cameraOn==NULL );
  bool change=( vehicle!=_cameraOn );
  float changeDist2=1e10;
  if( vehicle && _cameraOn )
    changeDist2 = vehicle->FutureVisualState().Position().Distance2(_cameraOn->FutureVisualState().Position());
  AIBrain *brain = FocusFromCamera(vehicle);
  OLink(Person) person = brain ? brain->GetPerson() : NULL;

  OLink(EntityAI) camAI = dyn_cast<EntityAI,Object>(vehicle);
  if (camAI && camAI->GetForceOptics(person,camType))
  {
    if (camType==CamInternal)
      camType = CamGunner;
  }
  if( vehicle!=_cameraOn || _camTypeMain!=camType)
  {
    _cameraOn=vehicle;
    SetCameraType(camType);
    _cameraExternal = true; //FIX: for drivers in tank with _driverForceOptics set, the UAPersonView changed CamGunner to CamInternal first, now it swaps to CamExternal immediately
    if (_camTypeMain == CamExternal) _cameraExternal = true;
    else if (_camTypeMain == CamInternal) _cameraExternal = false;
    SetActiveChannels();
    if (change)
    {
      EntityAIFull *ai=dyn_cast<EntityAIFull>(vehicle);
      if ( _ui && ai )
      {
        ai->ResetFF();
        _ui->ResetVehicle(ai);
#ifdef _WIN32
        if (GJoystickDevices)
          GJoystickDevices->PlayRamp(0,0,0);
        if (XInputDev)
          XInputDev->PlayRamp(0,0,0);
#endif
      }
    }
    LogF("Camera switched to %s", vehicle ? (const char *)vehicle->GetDebugName() : "NULL");
    // reset occlusion history - if any
    _scene.OnCameraChanged();
    if (cameraHasChanged) OnCameraChanged();
  }
  
  // force redistribution as soon as possible
  _nearImportanceDistributionTime=Glob.time-60; // when distibutions were calculated
  _farImportanceDistributionTime=Glob.time-60; // when distibutions were calculated
  // TODO: recreate landscape/object cache
  if( vehicle && change && changeDist2>Square(400) )
    _scene.GetLandscape()->FillCache(vehicle->GetFrameBase());
}

float World::IntersectWithCockpit(Vector3Par from, Vector3Par dir, float minDist, float maxDist) const
{
  if (GetCameraType()==CamInternal && !GetCameraEffect())
  {
    Object *obj = CameraOn();
    if (obj)
    {
      LODShape *lShape = obj->GetShape();
      if (lShape)
      {
        int view = obj->InsideViewGeomLOD(CamInternal);
        if (view!=LOD_INVISIBLE && view>=0)
        {
          ShapeUsedGeometryLock<> shape(obj->GetShape(),view);
          // draw that particular view geometry
          // check if there is any intersection with this component level
          CollisionBuffer col;
          const ConvexComponents *cc = lShape->GetConvexComponents(view);
          obj->IntersectLine(obj->RenderVisualState(),col,from,from+dir*maxDist,0,ObjIntersectView,cc,view, 0, CF_FIRST_CONTACT);
          if(col.Size())
            return minDist;
        }
      }
    }
  }
  return maxDist;
}

inline Matrix3 CameraChange(float heading, float dive)
{
  return Matrix3(MRotationY,heading)*Matrix3(MRotationX,dive);
}

const float IntroRiseTimeOfDay=6*OneHour+15*OneMinute;

void World::SimulateLandscape(float deltaT)
{
  LightSun *sun = _scene.MainLight();
  float timeD = deltaT*OneSecond;

  if (!IsSimulationEnabled())
    timeD = 0.0f;
//  if (GInput.keys[DIK_RWIN])
#if _ENABLE_CHEATS
  {
    timeD += deltaT*OneHour*GInput.GetCheat1(DIK_T);
    timeD -= deltaT*OneHour*GInput.GetCheat1(DIK_Y);
#if _VBS3_LAND_SHADOW
    if ((GInput.GetCheat1(DIK_T) || GInput.GetCheat1(DIK_Y)) && GScene)
      GScene->ResetLandShadows(false);
#endif
  }
  if (GInput.GetCheat1ToDo(DIK_G))
    timeD += OneDay;
  if (GInput.GetCheat1ToDo(DIK_H))
    timeD -= OneDay;
#endif

  timeD += _timeToSkip;
  _timeToSkip = 0.0f;
  // 
 	
	Glob.clock.AdvanceTime(timeD);

	bool recalcNeeded = false;
	if (Glob.clockWeather.AdvanceTime(timeD))
  {
    sun->Recalculate(this);
    recalcNeeded = true;
  }

  float scaledTime = timeD / OneSecond;
  PROFILE_SCOPE_EX(sLand,*);
  _scene.GetLandscape()->Simulate(scaledTime);
  _scene.MainLightChanged();

	if (recalcNeeded)
  {
    sun->Recalculate(this);
#if 0
    // if the change is strong enough, we may need to invalidate the shadows
    if (fabs(timeD)>OneHour)
      _scene.ResetLandShadows();
#endif
  }

  if( scaledTime>0 )
  {
    // change toward wanted weather
    saturate(_wantedOvercast,0,1);
    saturate(_wantedFog,0,1);
    saturate(_wantedWind,0,1);
    float diffOvercast=_wantedOvercast-_actualOvercast;
    float diffFog=_wantedFog-_actualFog;
    float diffWind=_wantedWind-_actualWind;
    saturate(diffOvercast,-_speedOvercast*scaledTime,+_speedOvercast*scaledTime);
    saturate(diffFog,-_speedFog*scaledTime,+_speedFog*scaledTime);
    saturate(diffWind,-_speedWind*scaledTime,+_speedWind*scaledTime);
    _actualOvercast+=diffOvercast;
    _actualFog+=diffFog;
    _actualWind+=diffWind;

    _weatherTime+=scaledTime;

	if(GetNetworkManager().IsServer() || (!GetNetworkManager().IsServer() && !GetNetworkManager().IsClient()))
	{
		while (_weatherTime >= _nextWeatherChange)
		{
			// create next weather change
			_actualOvercast=_wantedOvercast;
			_actualFog=_wantedFog;
			_actualWind=_wantedWind;
			float gRand=(GRandGen.RandomValue()+GRandGen.RandomValue())*0.5;
			float time=(gRand+0.1)*12*60*60+20*60; // in sec;
			_nextWeatherChange+=time;
			const float maxChange=0.5;
			const float maxFogChange=0.2;
			_wantedOvercast+=GRandGen.RandomValue()*(maxChange*2)-maxChange;
			_wantedFog+=GRandGen.RandomValue()*(maxFogChange*2)-maxFogChange;
			_speedOvercast=fabs(_wantedOvercast-_actualOvercast)/time;
			_speedFog=fabs(_wantedFog-_actualFog)/time;
			_speedWind=fabs(_wantedWind-_actualWind)/time;

			// send update to clients
			GLandscape->UpdateRemote();
		}
	}
  }
  _scene.GetLandscape()->SetOvercast(_actualOvercast);
  _scene.GetLandscape()->SetFog(_actualFog);
  _scene.GetLandscape()->SetWind(_actualWind);
}

void World::UpdateTemperatureTable(float temperature, float temperatureBlack, float temperatureWhite, float deltaT)
{
  // Update the Tc table
  for (int k = 0; k < TT_K; k++)
  {
    float tK = -log(1.0f/2.0f)/(exp((float)k));
    for (int j = 0; j < TT_Dot; j++)
    {
      float tDot = (float)j;
      for (int i = 0; i < TT_G; i++)
      {
        float tG = (float)i;

        // Calculate the Te (temperature the surface converges to)
        float tTe = (temperatureWhite * tG + temperatureBlack * (1.0f - tG)) * tDot + temperature * (1.0f - tDot);

        // Update the Tc value
        _temperatureTable[i][j][k] = (_temperatureTable[i][j][k] - tTe) * exp(-tK * deltaT) + tTe;
      }
    }
  }
}

extern bool thermalCheat;

void World::SimulateTemperature(float deltaT, bool enabled)
{
  PROFILE_SCOPE_EX(oTemp,*);
  // Compare the last time in year the table was recalculated with the current time in year. If they differ more than by a couple of minutes, do the recalculation
  // We know the time was updated already (see when Clock::AdvanceTime is called), thus we need to add the deltaT to the last value
  // If reset not needed, do the regular update by deltaT
  const float sec2TimeInYearCoef = 1.0f / (365.25f * 24.0f * 60.0f * 60.0f);
  float diffTimeInYear = fabs((_lastTemperatureTableResetTimeInYear + deltaT * sec2TimeInYearCoef) - Glob.clockWeather.GetTimeInYear());
  if (diffTimeInYear >= 60.0f * 1.0f * sec2TimeInYearCoef)
  {
    const int simulationSteps = 10 * 24 * 10;
    const float simulationInterval = (1.0f / 24.0f) * 0.1f;
    const float simulationIntervalSec = simulationInterval * 24.0f * 60.0f * 60.0f;
    float dayOffset = -simulationInterval * simulationSteps;
    for (int i = 0; i < simulationSteps; i++)
    {
      dayOffset += simulationInterval; // We want to call the offset update at the first place (last cycle should have dayOffset==0)
      float temperature, temperatureBlack, temperatureWhite;
      GLandscape->GetTemperature(temperature, temperatureBlack, temperatureWhite, dayOffset);
      UpdateTemperatureTable(temperature, temperatureBlack, temperatureWhite, simulationIntervalSec);
    }
  }
  else
  {
    // Get the current temperatures
    float temperature, temperatureBlack, temperatureWhite;
    GLandscape->GetTemperature(temperature, temperatureBlack, temperatureWhite);

    // Update the Tc table
    UpdateTemperatureTable(temperature, temperatureBlack, temperatureWhite, deltaT);
  }

  // Remember the last time the table was updated or reseted
  _lastTemperatureTableResetTimeInYear = Glob.clockWeather.GetTimeInYear();

  // Pass the table into the engine
  if (enabled)
  {
    GEngine->SetTemperatureTable(&_temperatureTable[0][0][0], TT_G, TT_Dot, TT_K);
  }

  // Visualize the table
  if (CHECK_DIAG(DETemperature))
  {
    DIAG_MESSAGE(200, Format("Tc table: B - black surface, W - white surface, L - lit, U unlit"));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][0],  _temperatureTable[1][1][0],  _temperatureTable[0][0][0],  _temperatureTable[1][0][0]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][1],  _temperatureTable[1][1][1],  _temperatureTable[0][0][1],  _temperatureTable[1][0][1]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][2],  _temperatureTable[1][1][2],  _temperatureTable[0][0][2],  _temperatureTable[1][0][2]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][3],  _temperatureTable[1][1][3],  _temperatureTable[0][0][3],  _temperatureTable[1][0][3]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][4],  _temperatureTable[1][1][4],  _temperatureTable[0][0][4],  _temperatureTable[1][0][4]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][5],  _temperatureTable[1][1][5],  _temperatureTable[0][0][5],  _temperatureTable[1][0][5]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][6],  _temperatureTable[1][1][6],  _temperatureTable[0][0][6],  _temperatureTable[1][0][6]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][7],  _temperatureTable[1][1][7],  _temperatureTable[0][0][7],  _temperatureTable[1][0][7]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][8],  _temperatureTable[1][1][8],  _temperatureTable[0][0][8],  _temperatureTable[1][0][8]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][9],  _temperatureTable[1][1][9],  _temperatureTable[0][0][9],  _temperatureTable[1][0][9]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][10], _temperatureTable[1][1][10], _temperatureTable[0][0][10], _temperatureTable[1][0][10]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][11], _temperatureTable[1][1][11], _temperatureTable[0][0][11], _temperatureTable[1][0][11]));
    DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][12], _temperatureTable[1][1][12], _temperatureTable[0][0][12], _temperatureTable[1][0][12]));
//     for (int i = 0; i < TT_K; i++)
//     {
//       DIAG_MESSAGE(200, Format("Tc BL:%0.4f, WL:%0.4f, BU:%0.4f, WU:%0.4f: ", _temperatureTable[0][1][i], _temperatureTable[1][1][i], _temperatureTable[0][0][i], _temperatureTable[1][0][i]));
//     }
  }
}

void World::SetWeather(float overcast, float fog, float wind, float time)
{
  if (overcast < 0)
    overcast = _actualOvercast;
  if (fog < 0)
    fog = _actualFog;
  if (wind < 0)
    wind = _actualWind;
  saturate(overcast, 0, 1);
  saturate(fog, 0, 1);
  saturate(wind, 0, 1);

  if (time <= 0)
  {
    _actualOvercast = _wantedOvercast = overcast;
    _actualFog = _wantedFog = fog;
    _actualWind = _wantedWind = wind;
    _nextWeatherChange = _weatherTime;
    _speedOvercast = 0;
    _speedFog = 0;

    GLandscape->SetOvercast(_actualOvercast);
    GLandscape->SetFog(_actualFog);
    GLandscape->SetWind(_actualWind);
  }
  else
  {
    _wantedOvercast = overcast;
    _wantedFog = fog;
    _wantedWind = wind;
    _nextWeatherChange = _weatherTime + time;
    _speedOvercast = fabs(_wantedOvercast - _actualOvercast) / time;
    _speedFog = fabs(_wantedFog - _actualFog) / time;
    _speedWind = fabs(_wantedWind - _actualWind) / time;
  }
}

void World::GetForecast(float &overcast, float &fog, float &wind)
{
	overcast = _wantedOvercast;
	fog = _wantedFog;
	wind = _wantedWind;
}

void World::SetForecast(float overcast, float fog, float wind, float time)
{
    _wantedOvercast = overcast;
    _wantedFog = fog;
    _wantedWind = wind;
    _speedOvercast = fabs(_wantedOvercast - _actualOvercast) / time;
    _speedFog = fabs(_wantedFog - _actualFog) / time;
    _speedWind = fabs(_wantedWind - _actualWind) / time;
}

void World::SetNextWeatherChange(float time)
{
	_nextWeatherChange = time;
}

void World::GetDate(int &year, int &month, int &day, int &hour, int &minute)
{
  tm tmDate = {0, 0, 0, 0, 0, 0};
  Glob.clockWeather.GetDate(tmDate);

  year = tmDate.tm_year + 1900;
  month = tmDate.tm_mon + 1;
  day = tmDate.tm_mday;
  hour = tmDate.tm_hour;
  minute = tmDate.tm_min;
}

void World::SetDate(int year, int month, int day, int hour, int minute)
{
  saturate(year,1000,9999);
  saturate(day,1,31);
  saturate(month,1,12);
  saturate(hour,0,24);
  saturate(minute,0,60);
  day--;
  for (int m=0; m<month-1; m++) day += GetDaysInMonth(year, m);
  
  float time = hour * OneHour + minute * OneMinute;
  float date = day * OneDay;
  Glob.clock.SetTime(time, date, year);
	Glob.clockWeather.SetTime(time, date, year);
  
  LightSun *sun = _scene.MainLight();
  sun->Recalculate(this);
  _scene.MainLightChanged();
  _scene.ResetLandShadows();
}

void World::ProcessNetwork()
{
  GetNetworkManager().OnSimulate();
  EvalPostponedEvents();
}

bool World::IsUserInputEnabled() const
{
#if _GAMES_FOR_WINDOWS
  if (GSaveSystem.IsUIShown()) return false;
#endif
  return _userInputDisabled <= 0;
}

void World::DisableUserInput(bool disable)
{
  if (disable)
  {
    if (_userInputDisabled++ == 0)
      GInput.ForgetKeys();
  }
  else
    _userInputDisabled--;
}

void World::SetCameraEffect(CameraEffect *effect)
{
  _cameraEffect=effect;
  _nearImportanceDistributionTime=Glob.time-60; // when distibutions were calculated
  _farImportanceDistributionTime=Glob.time-60; // when distibutions were calculated
  _playerSuspended = (effect!=NULL);
}

void World::SetTitleEffect(TitleEffect *effect)
{
#if _VBS2 // allow seagull difficulty option
  if (_lockTitleEffect) return;
#endif
  _titleEffect=effect;
}

void World::TerminateTitleEffect(float fadeOutTime)
{
  if (_titleEffect)
    _titleEffect->Terminate(fadeOutTime);
}

void World::SetCutEffect(int layer, TitleEffect *effect)
{
#if _VBS2 // allow seagull difficulty option
  if (_lockTitleEffect) return;
#endif
  if (layer >= 0)
  {
    _cutEffects.Access(layer);
    _cutEffects[layer] = effect;
  }
}

void World::TerminateCutEffect(int layer, float fadeOutTime)
{
  if (layer < 0 || layer >= _cutEffects.Size())
    return;
  if (_cutEffects[layer])
    _cutEffects[layer]->Terminate(fadeOutTime);
}

void World::ClearCutEffects()
{
  _cutEffects.Clear();
}

void World::StartLoadingScreen(RString text, RString resource)
{
  if (resource.GetLength() == 0)
  {
    IProgressDisplay *CreateDisplayProgress(RString text);
    _loadingScreen = CreateDisplayProgress(text);
  }
  else
  {
    IProgressDisplay *CreateDisplayProgress(RString text, RString resource);
    _loadingScreen = CreateDisplayProgress(text, resource);
  }
  _loadingScreen->SetProgress(::GlobalTickCount(), 0, 10000);
}

void World::UpdateLoadingScreen(float progress)
{
  if (_loadingScreen)
  {
    int current = toInt(10000.0 * progress);
    saturate(current, 0, 10000);
    _loadingScreen->SetProgress(::GlobalTickCount(), current, 10000);
  }
}

void World::AddScript(Script *script, bool simulate)
{
  int i = _scripts.Add(ScriptItem(script));
  if (simulate)
  {
    script->WaitUntilLoaded();
    if (script->OnSimulate())
      _scripts.DeleteAt(i);
  }
}

#if USE_PRECOMPILATION
void World::AddScriptVM(ScriptVM *vm, bool simulate)
{
  int i = _scriptVMs.Add(ScriptVMItem(vm));
  if (simulate)
  {
    vm->WaitUntilLoaded();
    if (vm->Simulate(0, 0.010f)) // 10 ms - may be not enough for things like setViewDistance
      _scriptVMs.DeleteAt(i);
  }
}
#endif

void World::AddFSM(FSM *fsm)
{
  _FSMs.Add(FSMItem(fsm));
}

FSM *World::FindFSM(int id) const
{
  if (id > 0)
  {
    for (int i=0; i<_FSMs.Size(); i++)
    {
      FSM *fsm = _FSMs[i]._fsm;
      if (fsm && fsm->GetId() == id)
        return fsm;
    }
  }
  return NULL;
}

void World::AddJVMFSM(JVMFSM *fsm)
{
  _jvmFSMs.Add(fsm);
}

void World::TerminateScript(Script *script)
{
  // TODO: consider some safe script termination

  int index = _scripts.Find(script);
  if (index < 0) return;
  script->DetachDebugger();
  _scripts.DeleteAt(index);
}

enum ScriptType
{
  STScript,
#if USE_PRECOMPILATION
  STScriptVM,
#endif
  STFSM
};

struct ScriptAgeInfo
{
  ScriptType _type;
  int _index;
  float _age;
  ScriptAgeInfo(ScriptType type, int index, float age) : _type(type), _index(index), _age(age) {}
};
TypeIsSimple(ScriptAgeInfo);

static inline int CmpScriptInfo(const ScriptAgeInfo *i1, const ScriptAgeInfo *i2)
{
  return sign(i2->_age - i1->_age);
}

/// helper traits for locking the World::SimulateScripts
struct BoolLockTraits
{
  static void Lock(bool &lock) {lock = true;}
  static void Unlock(bool &lock) {lock = false;}
};

void World::SimulateScripts(float deltaT, float timeLimit)
{
  if (_insideSimulateScripts) return; // avoid call from inside script
  ScopeLock<bool, BoolLockTraits> lock(_insideSimulateScripts); // lock the function

#if _ENABLE_CHEATS && !_SUPER_RELEASE
  bool enable = CHECK_DIAG(DEProfileScripts) != 0;
  if (enable != GGameState.IsProfilingEnabled())
  {
    GGameState.EnableProfiling(enable);
    DIAG_MESSAGE(500, Format("Scripts profiling %s", enable ? "enabled" : "disabled"));
  }
#endif

  {
    PROFILE_SCOPE_EX(siFEH,*);
    bool eventHandlerValid = !GOnFrameEventHandler.GetNil();
    if (eventHandlerValid)
    {
  #if USE_PRECOMPILATION
      if (GOnFrameEventHandler.GetType() == GameCode)
      {
        GameDataCode *code = static_cast<GameDataCode *>(GOnFrameEventHandler.GetData());
        eventHandlerValid = code->IsCompiled() && code->GetCode().Size() > 0;
      }
      else
  #endif
      {
        RString code = GOnFrameEventHandler;
        eventHandlerValid = code.GetLength() > 0;
      }
    }

    if (eventHandlerValid)
    {
      GameState *state = GWorld->GetGameState();

      GameVarSpace vars(false);
      int gshandle = state->BeginContext(&vars);
  #if USE_PRECOMPILATION
      if (GOnFrameEventHandler.GetType() == GameCode)
      {
        GameDataCode *code = static_cast<GameDataCode *>(GOnFrameEventHandler.GetData());
        state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
      }
      else
  #endif
      {
        // make sure string is not destructed while being evaluated
        RString code = GOnFrameEventHandler;
        state->EvaluateMultiple(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
      }
      state->EndContext(gshandle);
    }

  }
  PROFILE_SCOPE_EX(siScr,*);

// LogF("Script simulation after %.3f s", deltaT);

  SectionTimeHandle sectionTime = StartSectionTime();

  // update the age, remove the vanished (done) items, collect all types of scripts
  AutoArray< ScriptAgeInfo, MemAllocLocal<ScriptAgeInfo, 128> > ages;
  for (int i=0; i<_scripts.Size();)
  {
    if (_scripts[i]._script)
    {
      _scripts[i]._age += deltaT;
      ages.Add(ScriptAgeInfo(STScript, i, _scripts[i]._age));
      i++;
    }
    else
      _scripts.DeleteAt(i);
  }
#if USE_PRECOMPILATION
  for (int i=0; i<_scriptVMs.Size();)
  {
    if (_scriptVMs[i]._script)
    {
      _scriptVMs[i]._age += deltaT;
      ages.Add(ScriptAgeInfo(STScriptVM, i, _scriptVMs[i]._age));
      i++;
    }
    else
      _scriptVMs.DeleteAt(i);
  }
#endif
  for (int i=0; i<_FSMs.Size();)
  {
    if (_FSMs[i]._fsm)
    {
      _FSMs[i]._age += deltaT;
      ages.Add(ScriptAgeInfo(STFSM, i, _FSMs[i]._age));
      i++;
    }
    else
      _FSMs.Delete(i);
  }

  // Sort scripts
  QSort(ages.Data(), ages.Size(), CmpScriptInfo);

  // time limit for a single precompile script (can be interrupted if take too much of time), at least 0.5 ms
  float scriptTimeLimit = timeLimit - GetSectionTime(sectionTime);
  saturateMax(scriptTimeLimit, 0.0005f);

  // Simulate scripts
  for (int i=0; i<ages.Size(); i++)
  {
    const ScriptAgeInfo &age = ages[i];
    int index = age._index;
    switch (age._type)
    {
    case STScript:
      if (_scripts[index]._script->OnSimulate())
        _scripts[index]._script = NULL; // delete in the next World::SimulateScripts (now we need to be careful with indices)
// LogF("  Script #%d simulated after %.3f s", index, _scripts[index]._age);
      _scripts[index]._age = 0;
      break;
#if USE_PRECOMPILATION
    case STScriptVM:
      if (_scriptVMs[index]._script->Simulate(deltaT, scriptTimeLimit))
        _scriptVMs[index]._script = NULL; // delete in the next World::SimulateScripts (now we need to be careful with indices)
// LogF("  ScriptVM #%d simulated after %.3f s", index, _scriptVMs[index]._age);
      _scriptVMs[index]._age = 0;
      break;
#endif
    case STFSM:
      if (_FSMs[index]._fsm->Update(NULL))
        _FSMs[index]._fsm = NULL; // delete in the next World::SimulateScripts (now we need to be careful with indices)
// LogF("  FSM #%d simulated after %.3f s", index, _FSMs[index]._age);
      _FSMs[index]._age = 0;
      break;
    }

    // update the script time limit, break when no more time remains
    float scriptTimeLimit = timeLimit - GetSectionTime(sectionTime);
    if (scriptTimeLimit <= 0) break; // no time to update more
    saturateMax(scriptTimeLimit, 0.0005f);
  }

  // Java based FSMs
  for (int i=0; i<_jvmFSMs.Size();)
  {
    JVMFSM *fsm = _jvmFSMs[i];
    if (fsm && !fsm->Update()) i++;
    else _jvmFSMs.Delete(i);
  }
}

void World::SetCameraScript(Script *script) {_cameraScript = script;}

void World::TerminateCameraScript()
{
  if (_cameraScript)
  {
    // force script termination
    _cameraScript->TerminateFamily();
    _cameraScript = NULL;
  }
}

void World::StartCameraScript( Script *script )
{
  Ref<Script> lock = script;
  TerminateCameraScript();
  SetCameraScript(script);
  AddScript(script, true); // simulate now
}

float HowMuchInteresting(Entity *entity)
{
  float interesting = 2;
  const EntityType *type = entity->GetEntityType();
  EntityAI *ai = dyn_cast<EntityAI>(entity);
  if (ai)
  {
    // any AI entity is more interesting than a plain one
    interesting += 2;
    // we get some points for being human
    if (type->IsKindOf(GWorld->Preloaded(VTypeMan))) interesting += 2;
    // we get many points for firing, screaming and moving
    // this is all calculated in audibility - making noise
    interesting += ai->Audible()*5;
    // we get some points for movement
    interesting += ai->VisibleMovement();
    // when unit is named, it is important - it is also very likely to speak
    // if unit is know to be important, we prefer its importance
    float missionImportance = floatMax(
      ai->GetCameraInterest(),
      (ai->GetVarName().GetLength()>0)*5
    );
    interesting += missionImportance;
  }
  return interesting;
}

struct SelectInterestingResult
{
  Entity *_mostInteresting;
  float _mostInterestingScore;
  
  SelectInterestingResult():_mostInteresting(NULL),_mostInterestingScore(0){}
};

/// functor for MostInteresting - performs single target evaluation
class SelectInteresting
{
  Vector3 _position;
  Vector3 _direction;
  Object *_ignore;
  float _maxAngle;
  float _distanceOffset,_facingImportance;
  float _minDistance;
  SelectInterestingResult &_result;
  
public:
  SelectInteresting(SelectInterestingResult &result, Vector3Par pos, Vector3Par dir, Object *ignore,
    float distanceOffset, float facingImportance, float minDistance)
  :_result(result),_position(pos),_direction(dir),
  _ignore(ignore),_distanceOffset(distanceOffset),_facingImportance(facingImportance),
  _minDistance(minDistance),
  _maxAngle(atan(GScene->GetCamera()->Top()))
  {}
  
  bool operator()(Entity *entity) const
  {
    if (entity==_ignore) return false;
    //if (tgt->type->IsKindOf(GWorld->Preloaded(VTypeStatic))) continue;
    //if (tgt->idExact && tgt->idExact->Static()) continue;
    
    Vector3 visualPos = entity->AimingPosition(entity->FutureVisualState());
    float tgtDist = visualPos.Distance(_position);
    if (tgtDist<_minDistance) return false;
    Vector3 dir = visualPos-_position;
    float cosAngle = dir.CosAngle(_direction);
    // we cannot consider point only 
    // a part of the object may be visible even when the object center is not

    // if the object is behind, we can ignore it
    if (cosAngle<0) return false;
    
    // similiar to EntityAIFull::SelectAutoAimTarget
    // check distance of the target from the direction line
    float dist = NearestPointDistance(_position,_position+_direction,visualPos,0,FLT_MAX);

    LODShape *shape = entity->GetShape();
    float visibleRadius = shape ? shape->GeometrySphere() : 0.1f;

    const float sizeFactor = 1;
    const float maxAllowDist = 1.3;
    
    float tgtRadius = visibleRadius;
    tgtRadius *= sizeFactor;
    if (dist-tgtRadius>maxAllowDist) return false;
    //if (dist>minDist) return false;
    
    float angle = cosAngle<=1 ? acosf(cosAngle) : 0;
    float tgtInCenter = 1-angle/_maxAngle;
    saturateMax(tgtInCenter,0.05f);
    
    // if unit is not facing the camera, it should not be focused if there is any other target
    float facing = -entity->FutureVisualState().Direction()*_direction;
    facing = facing>=0 ? 1 : 0.001f;
    facing = facing*_facingImportance+(1-_facingImportance);
    float interestingScore = HowMuchInteresting(entity)*tgtInCenter/(tgtDist+_distanceOffset)*facing;
    if (interestingScore>_result._mostInterestingScore)
    {
      _result._mostInterestingScore = interestingScore;
      _result._mostInteresting = entity;
    }
    return false;
  }
};

/**
@param distanceFactor how much distance should affect the results
*/
static Entity *MostInteresting(Object *ignore, Vector3Par position, Vector3Par direction,
  float distanceFactor, float facingImportance, float minDistance)
{
  SelectInterestingResult result;
  // what is close to the camera direction is more interesting
  float distOffset = distanceFactor>1e-5 ? 1/distanceFactor-1 : 1e5;
  SelectInteresting select(result,position,direction,ignore,distOffset,facingImportance,minDistance);
  GWorld->ForEachVehicle(select);
  return result._mostInteresting;  
}

/**
@param distanceFactor how much distance should affect the results
*/
float World::AutoFocus(VisualStateAge age, Object *ignore, Vector3Par pos, Vector3Par dirNN, float distanceFactor, float facingImportance, float minDistance)
{
  // autofocus, scan all vehicles,  select what is interesting here
  Vector3 dir = dirNN.Normalized();

	Entity *interesting = MostInteresting(ignore,pos,dir,distanceFactor,facingImportance,minDistance);
  if (interesting)
    return interesting->FutureVisualState().Position().Distance(pos);
	
	// if there is nothing interesting, check first collision with any object or ground
  const float maxDist = floatMin(Glob.header.MaxDOFFocusDistance, Glob.config.horizontZ);
  float t = GLandscape->IntersectWithGroundOrSea(NULL,pos,dir,minDistance,maxDist);
  
	if (t < minDistance) 
		return minDistance;

	// check line against view geometries
	CollisionBuffer col;
	GLandscape->ObjectCollisionLine(age,col,Landscape::FilterIgnoreOne(ignore),pos,pos+dir*maxDist,0,ObjIntersectView);
			
	// find nearest intersection, TODO: can we use CF_NEAREST_CONTACT here???
	Object *minObj = NULL;
	float minDist2 = Square(t);
	float limitDistance2 = Square(minDistance);

	for (int i = 0; i < col.Size(); i++)
	{
		Object *obj = col[0].object;
		float dist2 = obj->FutureVisualState().Position().Distance2(pos);
			
		if (dist2 < limitDistance2)
			return minDistance;

		if (dist2 < minDist2)
		{
			minDist2 = dist2;
			minObj = obj;
		}
	}
		
	return minObj ? sqrt(minDist2) : t;
}

#if _ENABLE_CHEATS
void DebugOperMap();
void DebugOperMapCover();
void DebugOperMap(AIBrain *unit);
void DebugOperMapTrouble();
#endif

static bool showCinemaBorder = true;

void ShowCinemaBorder(bool show)
{
  showCinemaBorder = show;
}

bool IsShownCinemaBorder()
{
  return showCinemaBorder;
}

GameValue ShowCinemaBorder(const GameState *state, GameValuePar oper1)
{
  showCinemaBorder = oper1;
  return GameValue();
}


float GetClosestCollisionInCameraEnvelope(Object *focus, Vector3Par dirNorm, float dirSize, Vector3Par up, Vector3Par aside, Vector3Par camBase, Vector3Par focusPos, Vector3Par cameraPos, const Vector3 *coefsFocus, const Vector3 *coefsCamera, int nCoefs, bool considerTrees, bool ignoreObjectsApartOfFocus)
{
  // Collision buffer storage
  CollisionBuffer col;

  // Calculate the most pessimistic collision distance
  float maxDistance = dirSize;
  for (int c = 0; c < nCoefs; c++)
  {
    // Get the focus point (with some radius)
    Vector3 focusPoint = focusPos + up * coefsFocus[c].X() + aside * coefsFocus[c].Y() + dirNorm * coefsFocus[c].Z();

    // Get the half-sphere surface point
    Vector3 hsSurfacePoint = cameraPos + up * coefsCamera[c].X() + aside * coefsCamera[c].Y() + dirNorm * coefsCamera[c].Z();

    // Remember the distance to surface
    float focusToSurfaceDistance = focusPos.Distance(hsSurfacePoint);


    VisualStateAge renderAge = focus->GetRenderVisualStateAge();
    // Get the intersection with line starting at camera focus (inside the model) and ending at half-sphere surface
    if (ignoreObjectsApartOfFocus)
      focus->IntersectLine(renderAge, col, camBase + focusPoint, camBase + hsSurfacePoint, 0.0f, ObjIntersectView);
    else
      GLandscape->ObjectCollisionLine(renderAge, col, Landscape::FilterIgnoreOne(focus), camBase + focusPoint, camBase + hsSurfacePoint, 0.0f, ObjIntersectView);

    // No collision
    if (col.Size()<=0) continue;

    // Find nearest t, detect if some collision was found (don't test collision with some sparse models like trees)
    bool someCol = false;
    float minT = 1;
    for (int i=0; i<col.Size(); i++)
    {
      const CollisionInfo &info = col[i];

      // Ignore obstacle that are not opaque enough (some sparse trees)
      if (info.object->ViewDensity()>-3.0f) continue;
      float objRadius = info.object->GetRadius();
      if (objRadius<=0) continue;

      float objDistance = info.object->RenderVisualState().Position().Distance(camBase+cameraPos);
      float objScreenSize = objRadius/objDistance;
      float tgtScreenSize = focus->GetRadius()/dirSize;
      if (objScreenSize<tgtScreenSize*0.5f)
      {// if the object is small on screen compared to the target, we can ignore it
        #if _PROFILE
        //LogF("obj %s: %.3f, tgt %.3f",cc_cast(info.object->GetDebugName()),objScreenSize,tgtScreenSize);
        #endif
        continue;
      }
      // If object is normally transparent (regular trees), limit the T by some constant,
      // else consider it as a regular object and take the calculated T
      if (info.object->ViewDensity()>-9.0f)
      {
        // If trees are not to be considered, continue
        if (!considerTrees) continue;

        // check if tree is limiting us at all
        if (info.under<minT)
        {
          // Limit by tree constant, or the tree distance, whichever is further away
        const float treeDistance = 1.2f; // Distance in meters
        float treeT = treeDistance / focusToSurfaceDistance;
          float limitTree = floatMax(info.under,treeT);
          if (limitTree < minT) minT = limitTree;
        }
      }
      else if (info.under<minT)
        minT = info.under;

      

      someCol = true;
    }

    // No collision
    if (!someCol)
      continue;

    // Get the closest focus to collision distance
    float focusToColisionDistance = focusToSurfaceDistance * minT;

    // Get the collision distance as a minimum of the current distance and calculated collision distance
    saturateMin(maxDistance, focusToColisionDistance);
  }

  // Return the value
  return maxDistance;
}

/*!
@param cam in/out camera position relative to camBase
@param focusPos focus point relative to camBase
*/
static void ClipCamera(Vector3 &cam, Object *focus, Vector3Val camBase, Vector3Val focusPos, float &maxDistSmooth, float deltaT)
{
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DECollision) || CHECK_DIAG(DETransparent))
    return;
#endif

  // Get half-sphere with radius radiusSphere that represents the camera back and approximate it by 5 points. This
  // will be the back of the wrapper. Front of the wrapper are points around the focus (radiusFocus). Check the closest
  // collision.
  {
    // Get the camera dir, up and aside vectors, up and aside normalized
    Vector3 dir = cam-focusPos;
    Vector3 up = VUp;
    Vector3 aside = dir.CrossProduct(up);
    aside.Normalize();
    up = dir.CrossProduct(aside);
    up.Normalize();

    // Get the dir vector size and normalized dir vector
    Vector3 dirNorm;
    float dirSize;
    {
      float dirSize2 = dir.SquareSize();
      float invDirSize = InvSqrt(dirSize2);
      dirSize = dirSize2*invDirSize;
      dirNorm = dir*invDirSize;
    }

    // Get the values that approximates the focus neighborhood
    const float radiusFocus = 0.05f;
    static const Vector3 coefsFocus[] =
    {
      // up distance, aside distance, along distance, distance coef used
      Vector3(0,             0,            0),
      Vector3(radiusFocus,   0,            0),
      Vector3(-radiusFocus,  0,            0),
      Vector3(0,             radiusFocus,  0),
      Vector3(0,             -radiusFocus, 0),
    };
    const int nCoefs = lenof(coefsFocus);

    // Get the values that approximates the camera back sphere
    // dirSize is considered here, because we want the envelope angles to be same regardless on dirSize (see how is coefsSphere used)
    const float radiusSphere = dirSize * 0.2f;
    static const Vector3 coefsSphere[] =
    {
      // up distance, aside distance, along distance, distance coef used
      Vector3(0,             0,              0),
      Vector3(radiusSphere,  0,              0),
      Vector3(-radiusSphere, 0,              0),
      Vector3(0,             radiusSphere,   0),
      Vector3(0,             -radiusSphere,  0),
    };
    DoAssert(lenof(coefsSphere) == lenof(coefsFocus));

    // Get the values that approximates the camera back sphere - big version
    // dirSize is considered here, because we want the envelope angles to be same regardless on dirSize (see how is coefsSphere used)
    const float radiusSphereBig = dirSize * 0.5f;
    static const Vector3 coefsSphereBig[] =
    {
      // up distance, aside distance, along distance, distance coef used
      Vector3(0,                0,                  0),
      Vector3(radiusSphereBig,  0,                  0),
      Vector3(-radiusSphereBig, 0,                  0),
      Vector3(0,                radiusSphereBig,    0),
      Vector3(0,                -radiusSphereBig,   0),
    };
    DoAssert(lenof(coefsSphereBig) == lenof(coefsFocus));

    // maxDistSmooth is initialized big at the beginning, move it to a reasonable interval
    saturateMin(maxDistSmooth, dirSize);

    // First test the regular envelope, if collision then saturate, else test the big envelope (for smooth transition)
    float maxDistance = GetClosestCollisionInCameraEnvelope(focus, dirNorm, dirSize, up, aside, camBase, focusPos, focusPos + dirNorm * dirSize/*cam*/, coefsFocus, coefsSphere, nCoefs, false, false);
    if (maxDistance <= maxDistSmooth)
      saturateMin(maxDistSmooth, maxDistance);
    else
    {
      // Second test the big envelope, if collision then slide one direction, else the other one
      float maxDistanceBig = GetClosestCollisionInCameraEnvelope(focus, dirNorm, dirSize, up, aside, camBase, focusPos, focusPos + dirNorm * dirSize/*cam*/, coefsFocus, coefsSphereBig, nCoefs, true, false);
      if (maxDistanceBig <= maxDistSmooth)
      {
        // Slide towards the focus
        const float slideSpeed = 4.0f;
        saturateMin(maxDistSmooth, floatMax(maxDistanceBig, maxDistSmooth - deltaT * slideSpeed));
      }
      else
      {
        // Slide away from the focus. Note it is possible, that small envelope was almost collided whereas big envelope
        // is quite far from colliding. That's why we have to consider maxDistance from small envelope here
        const float slideSpeed = 1.0f;
        saturateMax(maxDistSmooth, floatMin(maxDistance, floatMin(maxDistanceBig, maxDistSmooth + deltaT * slideSpeed)));
      }
    }

    // Find the collisions with the object on focus and consider the most away as a primary limit
    // This code will secure the camera won't be inside of the model we're looking at 
    float invEnvelopeDistance = GetClosestCollisionInCameraEnvelope(focus, dirNorm, dirSize, up, aside, camBase, focusPos + dirNorm * dirSize, focusPos, coefsSphere, coefsFocus, nCoefs, false, true);
    const float surfaceOffset = 0.2f;
    saturateMax(maxDistSmooth, (dirSize - invEnvelopeDistance) + surfaceOffset);

    // Get the new camera position
    cam = focusPos + dirNorm * maxDistSmooth;
  }
}

void World::CalculateCameraTransform(Matrix4 &base, Matrix4 &transform, CameraType camType, float deltaT)
{
  Entity *camVehicle = dyn_cast<Entity,Object>(_cameraOn);
  OLink(EntityAI) camAI = dyn_cast<EntityAI, Entity>(camVehicle);
  switch (camType)
  {
  case CamGunner:
    {
      transform = camVehicle->InsideCamera(camType);
      base = camVehicle->RenderVisualState().Transform();
    }
    break;
  default: //case CamInternal:
    // this is very similar to CamGunner,
    // but InternalCameraTransform sometimes does more than just calling InsideCamera
    transform=camVehicle->InternalCameraTransform(base,camType);
    break;
  case CamExternal:
    {
      const ObjectVisualState &vs = camVehicle->RenderVisualStateScope();
      Matrix3 vehOrient;
      Vector3Val dist = camVehicle->ExternalCameraPosition(camType);
      Vector3 wdir = camVehicle->GetCameraDirection(camType);
      vehOrient.SetUpAndDirection(VUp,wdir);
      //only head/look direction is used
      Vector3 nDir = vs.GetInvTransform().Rotate(wdir);
      saturate(nDir[1],-1,1);
      nDir[2]=sqrt(1-nDir[1]*nDir[1]); nDir[0]=0; //no heading
      Vector3 dir = vehOrient*nDir;
      vehOrient.SetDirectionAndUp(dir,VUp);

      // FIX camera clipping
      Vector3 focPos = vs.DirectionModelToWorld(camVehicle->InsideCamera(camType).Position());
      Vector3 camPos = vehOrient*dist+focPos;
      ClipCamera(camPos,camVehicle,vs.Position(),focPos,_camMaxDist[camType],deltaT);
      // transform back into the local vehicle space
      Matrix4 toCamVehicle = vs.GetInvTransform();
      transform.SetOrientation(toCamVehicle.Orientation()*vehOrient);
      transform.SetPosition(toCamVehicle.Orientation()*camPos);
      base = vs.Transform();
    }
    break;
  case CamGroup:
    {
      if (_ui)
      {
        //const ObjectVisualState &vs = cameraVehicle->RenderVisualStateScope();
        float dist = camVehicle->OutsideCameraDistance(camType);
        Vector3 cgPos = _ui->GetCamGroupCameraPos();
        cgPos = cgPos * dist;
        Matrix3 vehOrient;
        Vector3 focPos = camVehicle->CameraPosition();
#if _VBS2 // convoy trainer
        // if we're in the vehicle use the vehicles position instead
        // to prevent flickering
        Object *obj = cameraVehicle;
        Person *person = dyn_cast<Person>(obj);
        if(person&&person->Brain()&&person->Brain()->GetVehicleIn())
          focPos = person->Brain()->GetVehicleIn()->CameraPosition();
#endif
        Vector3 cgPosWorld = focPos+cgPos;
        //camera cannot be under the ground or water
        //float minCamY = _scene.GetLandscape()->SurfaceYAboveWater(cgPosWorld.X(),cgPosWorld.Z());
        float minCamY = _scene.GetLandscape()->SurfaceY(cgPosWorld.X(),cgPosWorld.Z());
        minCamY += 0.1;
        if( cgPosWorld.Y()<minCamY ) cgPosWorld[1]=minCamY;
        Vector3 dir = focPos-cgPosWorld; dir.Normalize();
        vehOrient.SetDirectionAndUp(dir, VUp);
        transform.SetOrientation(vehOrient);
        transform.SetPosition(cgPosWorld);
        base = MIdentity;
      }
      else Assert(false);
    }
    break;
    break;
  }
}

void World::CalculateCameraTransform(Matrix4 &base, Matrix4 &transform, float deltaT)
{
  if (_camTypeOld==_camTypeNew)
    CalculateCameraTransform(base,transform,_camTypeNew,deltaT);
  else
  {
    // the initialization is here only "just in case"
    // CalculateCameraTransform always fills in new values
    Matrix4 transOld = transform;
    Matrix4 transNew = transform;
    Matrix4 baseOld = MIdentity;
    Matrix4 baseNew = MIdentity;
    CalculateCameraTransform(baseOld,transOld,_camTypeOld,deltaT);
    CalculateCameraTransform(baseNew,transNew,_camTypeNew,deltaT);
    // when there is a common base, avoid converting to world space
    if (baseOld.Distance2(baseNew)<1e-8)
    {
      // during interpolation we need to interpolate the result, not the base matrices
      // we will use world space for this
      // assume no shaking will be visible here during the movement
      // we could also use one of the base spaces
      float smoothedFactor = InterpolationFunctions::SmoothSimple3Parts(_camTypeNewFactor);
      transform = transNew*smoothedFactor+transOld*(1-smoothedFactor);
      transform.Orthogonalize();
      base = baseNew;
    }
    else
    {
      // base is changing, we do not want to interpolate base and relative matrices separately
      // use world space instead, assume no shaking will be visible here during the movement
      // we could also use one of the base spaces and transform the computation into it
      transOld = baseOld*transOld;
      transNew = baseNew*transNew;
      transform = transNew*_camTypeNewFactor+transOld*(1-_camTypeNewFactor);
      transform.Orthogonalize();
      base = MIdentity;
    }
  }
}

bool IsPlayerDead()
{
  Person *player = GWorld->GetRealPlayer();
  if (!player) return true;
  if (!player->Brain()) return true;
  return player->Brain()->GetLifeState() == LifeStateDead;
}

ChatChannel ActualChatChannel();
void SetChatChannel(ChatChannel channel);
void PrevChatChannel();
void NextChatChannel();
bool ChannelValid();
void BackupChatChannel();
void RestoreChatChannel();

/// when excessing hard limit, delete cloudlets immediately
static int CloudletCountLimitHard = 10000;
/// when excessing soft limit, delete cloudlets using alpha
static int CloudletCountLimitSoft = 9000;

#if _ENABLE_CHEATS
bool forceControlsPaused = false;
#endif

#if _ENABLE_CHEATS
static bool PruneDiagNowOn = true;
static int PruneMinX = 0, PruneMinZ = 0;
static int PruneMaxX = INT_MAX, PruneMaxZ = INT_MAX;
//static int PruneMinX = 325, PruneMinZ = 224;
//static int PruneMaxX = 326, PruneMaxZ = 225;
#else
const bool PruneDiagNowOn = false;
const int PruneMinX = 0, PruneMinZ = 0;
const int PruneMaxX = 0, PruneMaxZ = 0;
#endif


/// record all areas which are only partially covering given region
/**
A continuous structure may consist of many lines, none of them providing
full coverage for border regions.

At any time the partial coverage can be collapsed into a full coverage.

Too old information is discarded during collapsing, as it is not likely to add anything any more.
*/

class PartialCoverage
{
  /// one area
  struct Area
  {
    float _xBeg,_xEnd;
    float _y;
    int _t;
    
    Area() {}
    Area(float xBeg, float xEnd, float y, int t)
    :_xBeg(xBeg),_xEnd(xEnd),_y(y),_t(t)
    {}
    ClassIsSimple(Area)
  };
  static const int MaxItems = 8;
  /// we keep items sorted
  AutoArray< Area,MemAllocLocal<Area,MaxItems> > _items;

public:  
  void Add(float xBeg, float xEnd, float y, int t)
  {
    //Assert(xBeg>0 || xEnd<1);
    Assert(xBeg>=0 && xEnd<=1);
    Assert(xBeg<=xEnd);
    // do not insert empty items - zero area has no effect whatsoever
    if (xBeg>=xEnd) return;
    // rather than adding too many items, we rather do not add at all
    // cost of allocation would probably be higher than saved time
    if (_items.Size()>MaxItems) return;
    _items.Add(Area(xBeg,xEnd,y,t));
  }
  float Collapse(float oldY, int minT);
};

// avoid being too close of the projection plane to avoid precision issues
const float HorizonMinZ = 1.0f;

/**
Horizon based occlusion culling
Cf. https://wiki.bistudio.com/index.php/Zakr%C3%BDv%C3%A1n%C3%AD_%C4%8D%C3%A1st%C3%AD_sc%C3%A9ny_%28Scene_Occlusions%29
*/
class HorizonPrune
{
  static const int xSamples = 64;
  //static const int xSamples = 16;
  //! Number of pruning buffers
  static const int nBuffers = 3;
  
  /** -1 means bottom, +1 mean top*/
  float _y[nBuffers][xSamples];
	float _maxMinimum; //maximum of minimums of previous buffers. Used for quick reject
	float _maxMinimum2; //maximum of minimums of buffer before previous buffer. Used for quick reject

  /// index of the active buffer
  int _actBuffer;
  
  /// partial covered recorded
  PartialCoverage _yPart[xSamples];
  
  /// "time" elapsed - how many lines are already closed
  /**
  This is incremented each time Advance is called
  */
  int _t;
  
  /// view space matrix
  Matrix4 _viewSpace;
  /// inverse view space matrix 
  /** used to transform into view space */
  Matrix4 _invViewSpace;
  mutable Matrix4 _modelToView;
  mutable const Object* _lastObject;

public:
  void SetModel(const Object* obj) const
  {
    //avoid twice transform when model is tested and added too
    if(obj != _lastObject)
    {
      _lastObject = obj;
      _modelToView = _invViewSpace*obj->FutureVisualState().Transform();
    }
  }

  //! Constructor
  HorizonPrune(Vector3Par dir, Vector3Par pos, float fov);

  /// add horizon line
  void AddLineScreen(Vector3Par line0, Vector3Par line1);
  
  /// add horizon line - used mostly for terrain
	void AddHorizonLine(const Vector3 *line, int nPoints);
	void AddTransformedHorizonLine(const Vector3 *line, int nPoints);

  /// used for object view geometry rendering
  void AddObjectHorizon(const Object* obj, const Shape *view, const AutoArray<OcclusionEdge> &edges);
	
  /// check visibility of a line - screen space coordinates
  bool CheckLineScreen(Vector3Par line0, Vector3Par line1) const;

  /// check visibility of a point - screen space coordinates
  inline bool CheckPointScreen(Vector3Par p0) const;
  
  /// check visibility of a line - view space coordinates
  bool CheckLineView(Vector3Par v0, Vector3Par v1) const;
  
  /// check visibility of a point - view space coordinates
  bool CheckPointView(Vector3Par v0) const;
  
  /// check visibility of a min max box - world space coordinates
  bool CheckMinmaxBox(const Vector3 minmax[2], ClipFlags mayBeClipped) const;
  
  /// check visibility of a min max box - model space coordinates
  bool CheckMinmaxBox(const Object* obj, const Vector3 minmax[2], ClipFlags mayBeClipped) const;
  
  void AdvanceBuffer();
  
	/// transform position from world space into a virtual view space
	inline Vector3 Transform(Vector3Val pos) const {return _invViewSpace.FastTransform(pos);}

private:
  //! Index of the buffer before _actBuffer
  int Prev() const {return (_actBuffer + (nBuffers - 1)) % nBuffers;}
  //! Index of the buffer before Prev()
  int Prev2() const {return (_actBuffer + (nBuffers - 2)) % nBuffers;}
  

  /// 2D layer (virtual screen) diagnostics
  void DiagHor2DSpace(int x, float y, ColorVal color, int t) const;

  /// diagnostics of virtual screen space converted back to world space
  void DiagHorSpace(float x, float y, float z, ColorVal color, float size) const;

  void DiagScreenSpace(Vector3Par pos, ColorVal color, float size) const;

  void DiagViewSpace(Vector3Par pos, ColorVal color, float size) const;
  void DiagWorldSpace(Vector3Par pos, ColorVal color, float size) const;
  
  /// clip into a virtual view space
	bool ViewSpaceClip(Vector3 &c0, Vector3 &c1, Vector3Val v0, Vector3Val v1) const;
	void ViewSpaceClipCC(Vector3 &c0, Vector3 &c1, Vector3Val v0, int cc0, Vector3Val v1, int cc1) const;
  
	inline int ViewSpaceClipPoint(Vector3Par v0) const
	{
		float clip0 = HorizonMinZ-v0.Z();

		int clip = 0;

		// first clip against z-plane
		if (clip0>=0) clip |= 1;

		//clipping: positive X plane
		clip0 = +v0.X()-v0.Z();
		if (clip0>=0) clip |= 2;

		//clipping: negative X plane
		clip0 = -v0.X()-v0.Z();
		if (clip0>=0) clip |= 4;

		return clip;
	}

  /// check virtual view space clipping
  bool TestViewSpaceClip(Vector3Val v0) const;
  
  /// transform position virtual view-space into a virtual screen space
  inline Vector3 Project(Vector3Val vPos) const
  {
    Assert(vPos.Z()>0);
    float invZ = 1.0f/vPos.Z();
    return Vector3(vPos.X()*invZ,vPos.Y()*invZ,vPos.Z());
  }

  /// return buffer from the previous line
  const float *Prev2Buffer() const {return _y[Prev2()];}
  /// return buffer which is currently being constructed
  float *CurrBuffer() {return _y[_actBuffer];}
  const float *CurrBuffer() const {return _y[_actBuffer];}

	bool CheckHorizon(const float *buffer, int i, float y) const
	{
		// when out of the virtual view space, always assume not covered
		// this is because real view space may be still there
		if (i<=0 || i>=xSamples)
      return false;
		return buffer[i]>=y;
	}

  void SetHorizon(float *buffer, int i, float y)
  {
    // when out of the screen, set nothing
    if (i<=0 || i>=xSamples)
      return;
    saturateMax(buffer[i],y);
  }
};

/**
@param dir camera direction
@param pos position of the first line 
@param fov field of view of the virtual screen (width in 1m distance)
*/
HorizonPrune::HorizonPrune(Vector3Par dir, Vector3Par pos, float fov)
{
  _actBuffer = 0;
  _t = 0;
  
  // when starting assume no horizon (in any frame)
  for (int i = 0; i < nBuffers; i++)
  {
    for (int x=0; x<xSamples; x++)
      _y[i][x] = -FLT_MAX;
  }

	//not covering anything yet
	_maxMinimum = -FLT_MAX;
	_maxMinimum2 = -FLT_MAX;
  
  // virtual view-space: z along camera direction, y up
  
  // we need to respect up to avoid non-convexities in Y direction
  Matrix4 orient(MUpAndDirection, VUp, dir);
  Assert(fabs(orient(1,0))<1e-4f);
  Assert(fabs(orient(1,2))<1e-4f);
  orient(1,0) = 0, orient(1,2) = 0; // we enforce some elements to be null to prevent rounding errors
  Matrix4 fovScale(MScale,fov,1,1);
  Matrix4 translate(MTranslation,pos);
  Matrix4 invFovScale(MScale,1/fov,1,1);
  Matrix4 invOrient(MInverseRotation,orient);
  Matrix4 invTranslate(MTranslation,-pos);
  // from view space to world space: first scale, then rotate, last position
  _viewSpace = translate*orient*fovScale;
  // computing the inverse by piecewise inversion of individual matrices is faster and numerically more robust
  _invViewSpace = invFovScale*invOrient*invTranslate;
  // FIXME: according to Ondra, this is OK to ignore the following assert
  //Assert(_invViewSpace.Distance2(_viewSpace.InverseGeneral())<1e-4f);
}

void HorizonPrune::AdvanceBuffer()
{
  Assert(_actBuffer >= 0 && _actBuffer < nBuffers);

  // Move to next item
  _actBuffer = (_actBuffer + 1) % nBuffers;

  // Remember this and last values
  float *prev = _y[Prev()];
  float *curr = _y[_actBuffer];

	//find minimum of this buffer
	float minLevel = FLT_MAX;

  // copy the information from the last one to the recent one
  for (int i=0; i<xSamples; i++)
  {
    // collapse any partial coverage, discard old one
    float h = _yPart[i].Collapse(prev[i],_t-2);
		minLevel = floatMin(minLevel, h);
		prev[i] = h;
    curr[i] = h;
  }

	//update
	_maxMinimum2 = _maxMinimum;
	_maxMinimum = floatMax(minLevel, _maxMinimum);

  if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
  {
    for (int i=0; i<xSamples; i++)
    {
      if (prev[i]>-FLT_MAX)
        DiagHor2DSpace(i,prev[i],Color(0,1,0,0.3),_t);
    }
  }
  _t++;
}

/**
note: may be inaccurate, as z may be inaccurately interpolated
*/
void HorizonPrune::DiagHorSpace(float x, float y, float z, ColorVal color, float size) const
{
  Vector3 scr(x/(xSamples*0.5f)-1,y,z);
  DiagScreenSpace(scr,color,size);
}


void HorizonPrune::DiagHor2DSpace(int x, float y, ColorVal color, int t) const
{
  // we cannot use 2D, as at this point there is nothing rendered yet
  // we need to create world space objects instead
  // we will do it relative to the camera
  const Camera &cam = *GScene->GetCamera();
  static float yScale = 0.7;
  static float yOffset = 0.5;
  float xRelBeg = float(x)/xSamples*2-1;
  float xRelEnd = float(x+1)/xSamples*2-1;
  float yRel = (y*yScale+yOffset)*2-1;
  // keep y in view space
  saturate(yRel,-1,1);
  static float z = 1.0f;
  static float size = 0.05f;
  int tFactor = t+1;
  saturate(tFactor,1,10);
  Vector3 posBeg(cam.Left()*xRelBeg*z,cam.Top()*yRel*z,z);
  Vector3 posEnd(cam.Left()*xRelEnd*z,cam.Top()*yRel*z,z);
  
  enf::Shape::CreateLine(cam.PositionModelToWorld(posBeg), cam.PositionModelToWorld(posEnd), PackedColor(color), enf::SS_ONCE);
}

void HorizonPrune::DiagScreenSpace(Vector3Par pos, ColorVal color, float size) const
{
  // convert from screen space to view space first
  
  Vector3 view(pos.X()*pos.Z(),pos.Y()*pos.Z(),pos.Z());
  DiagViewSpace(view,color,size);
}

void HorizonPrune::DiagWorldSpace(Vector3Par pos, ColorVal color, float size) const
{
  Vector3 offsetPos = Vector3(0,0.5,0) + pos;
  enf::Shape::CreateDiamond(offsetPos - Vector3(size), offsetPos + Vector3(size), PackedColor(color), enf::SS_ONCE);
}

void HorizonPrune::DiagViewSpace(Vector3Par pos, ColorVal color, float size) const
{
  Vector3 wPos = _viewSpace.FastTransform(pos);
  DiagWorldSpace(wPos,color,size);
}

/**
@return the clip result is not empty -something was left
*/
bool HorizonPrune::ViewSpaceClip(Vector3 &c0, Vector3 &c1, Vector3Val v0, Vector3Val v1) const
{
  // check clip status for both points against important planes
  // those are: x positive / x negative, near z plane
  // note: we have no imposed limits on y, as the space is virtual only
  
  // first clip against z-plane if needed
  {
    float clip0 = HorizonMinZ-v0.Z();
    float clip1 = HorizonMinZ-v1.Z();
    
    if (clip0>=0 && clip1>=0) return false;
    
    c0 = v0;
    c1 = v1;
    
    if (clip0>0)
    {
      // clip 0 end so that HorizonMinZ = c0.Z()
      // we know v1.Z()>v0.Z(), otherwise the whole line would be clipped
      float f = clip0/(clip0-clip1); // = clip0/(v1.Z()-v0.Z());
      Assert(f>=0 && f<=1);
      //float c0z = f*(v1.Z()-v0.Z())+v0.Z() = (HorizonMinZ-v0.Z())+v0.Z() ;
      c0[0] = f*c1.X()+(1-f)*c0.X();
      c0[1] = f*c1.Y()+(1-f)*c0.Y();
      c0[2] = HorizonMinZ;
    }
    else if (clip1>0)
    {
      float f = clip1/(clip1-clip0);
      Assert(f>=0 && f<=1);
      c1[0] = f*c0.X()+(1-f)*c1.X();
      c1[1] = f*c0.Y()+(1-f)*c1.Y();
      c1[2] = HorizonMinZ;
    }
  }
  //clipping: positive X plane
  {
    float clip0 = +c0.X()-c0.Z();
    float clip1 = +c1.X()-c1.Z();
    
    if (clip0>=0 && clip1>=0) return false;

    if (clip0>0)
    {
      float f = clip0/(clip0-clip1);
      Assert(f>=0 && f<=1);
      c0 = c1*f+c0*(1-f);
    }
    else if (clip1>0)
    {
      float f = clip1/(clip1-clip0);
      Assert(f>=0 && f<=1);
      c1 = c0*f+c1*(1-f);
    }
  }
  
  //clipping: negative X plane
  {
    float clip0 = -c0.X()-c0.Z();
    float clip1 = -c1.X()-c1.Z();
    
    if (clip0>=0 && clip1>=0) return false;

    if (clip0>0)
    {
      float f = clip0/(clip0-clip1);
      Assert(f>=0 && f<=1);
      c0 = c1*f+c0*(1-f);
    }
    else if (clip1>0)
    {
      float f = clip1/(clip1-clip0);
      Assert(f>=0 && f<=1);
      c1 = c0*f+c1*(1-f);
    }
  } 
  return true;
}

void HorizonPrune::ViewSpaceClipCC(Vector3 &c0, Vector3 &c1, Vector3Val v0, int cc0, Vector3Val v1, int cc1) const
{
	// check clip status for both points against important planes
	// those are: x positive / x negative, near z plane
	// note: we have no imposed limits on y, as the space is virtual only

	c0 = v0;
	c1 = v1;

	// first clip against z-plane if needed
	if((cc0|cc1) & 1)
	{
		float clip0 = HorizonMinZ-v0.Z();
		float clip1 = HorizonMinZ-v1.Z();

		if (clip0>0)
		{
			// clip 0 end so that HorizonMinZ = c0.Z()
			// we know v1.Z()>v0.Z(), otherwise the whole line would be clipped
			float f = clip0/(clip0-clip1); // = clip0/(v1.Z()-v0.Z());
			Assert(f>=0 && f<=1);
			//float c0z = f*(v1.Z()-v0.Z())+v0.Z() = (HorizonMinZ-v0.Z())+v0.Z() ;
			c0[0] = f*c1.X()+(1-f)*c0.X();
			c0[1] = f*c1.Y()+(1-f)*c0.Y();
			c0[2] = HorizonMinZ;
		}
		else if (clip1>0)
		{
			float f = clip1/(clip1-clip0);
			Assert(f>=0 && f<=1);
			c1[0] = f*c0.X()+(1-f)*c1.X();
			c1[1] = f*c0.Y()+(1-f)*c1.Y();
			c1[2] = HorizonMinZ;
		}
	}
	//clipping: positive X plane
	if((cc0|cc1) & 2)
	{
		float clip0 = +c0.X()-c0.Z();
		float clip1 = +c1.X()-c1.Z();

		if (clip0>0)
		{
			float f = clip0/(clip0-clip1);
			Assert(f>=0 && f<=1);
			c0 = c1*f+c0*(1-f);
		}
		else if (clip1>0)
		{
			float f = clip1/(clip1-clip0);
			Assert(f>=0 && f<=1);
			c1 = c0*f+c1*(1-f);
		}
	}

	//clipping: negative X plane
	if((cc0|cc1) & 4)
	{
		float clip0 = -c0.X()-c0.Z();
		float clip1 = -c1.X()-c1.Z();

		if (clip0>0)
		{
			float f = clip0/(clip0-clip1);
			Assert(f>=0 && f<=1);
			c0 = c1*f+c0*(1-f);
		}
		else if (clip1>0)
		{
			float f = clip1/(clip1-clip0);
			Assert(f>=0 && f<=1);
			c1 = c0*f+c1*(1-f);
		}
	}
}

/**
@return is the whole line full inside?
*/
inline bool HorizonPrune::TestViewSpaceClip(Vector3Val v0) const
{
  // check clip status for both points against important planes
  // those are: x positive / x negative, near z plane
  // note: we have no imposed limits on y, as the space is virtual only
  
  // z-plane
  if (HorizonMinZ-v0.Z()>0)
    return false;
  //clipping: positive X plane
  if (+fabs(v0.X())>v0.Z())
    return false;  
  return true;
}

/**
@param minT after collapsing remove any items older than this
*/
float PartialCoverage::Collapse(float oldY, int minT)
{
  // we seek for max. y which would cover the whole area
  // we can remove any items below this y
  // we will remove older items as well
  
  // we start with the most pessimistic entry and check if corresponding y can cover 
  // we advance with x
  // if there is any hole, there is no solution
  float y = FLT_MAX;
  float x = 0;
  for(;;)
  {
    // check max. y which could advance current x
    // TODO: _items are sorted - we should use this to select starting index and to break loop early
    float curY = -FLT_MAX;
    float curX = x;
    for (int j=0; j<_items.Size(); j++)
    {
      const Area &area = _items[j];
      if (area._xBeg<=x && area._xEnd>x && area._y>curY)
      {
        curY = area._y;
        curX = area._xEnd;
      }
    }
    // if there is no way to continue, terminate
    if (curX<=x) break;
    x = curX;
    y = floatMin(y,curY);
    // items are sorted: we can skip all which have xEnd<=x
  }
  // if the field already had some y, any items below it can be discarded
  if (x>=1 && oldY<y) oldY = y;
  // now remove what is no longer needed
  int s,d;
  for (s=0,d=0; s<_items.Size(); s++)
  {
    const Area &area = _items[s];
    if (area._y>oldY && area._t>=minT)
    {
      if (s!=d) _items[d] = area;
      d++;
    }
  }
  _items.Resize(d);
  return oldY;
}

//int addrejected=0;
//int addpassed=0;
//int checkrejected=0;
//int checkpassed=0;

void HorizonPrune::AddLineScreen(Vector3Par line0, Vector3Par line1)
{
  //float x0 = (x-xSamples*0.5f)/xSamples;
  //float x1 = (x+1-xSamples*0.5f)/xSamples;
  // we need to convert from screen-space to view-space first
  // we know only approximate z, therefore we will use it

	//check y first, if it's worth to rasterize
	float yBeg = line0.Y();
	float yEnd = line1.Y();

	float yMax = floatMax(yBeg, yEnd);

	//max minimum is higher, no chance to cover anything
	if(yMax < _maxMinimum)
	{
		//addrejected++;
		return;
	}
	//addpassed++;

	if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
	{
		float size = 0.3f;
		Color color(Color(0,0,1,0.4));
		DiagScreenSpace(line0,color,size);
		DiagScreenSpace(line1,color,size);
	}

	/// apply small epsilon to make sure numeric precision errors
  // are not preventing from covering whole slot
  
  // with 1024 resolution and 64 xSamples one pixel is 0.06
  const float epsilon = 1e-4f;
  float xBeg = line0.X()*xSamples*0.5f+xSamples*0.5f;
  float xEnd = line1.X()*xSamples*0.5f+xSamples*0.5f;
  float zBeg = line0.Z();
  float zEnd = line1.Z();
  // make sure x order is known
  if (xBeg>xEnd) SwapQ(&xBeg,&xEnd),SwapQ(&yBeg,&yEnd),SwapQ(&zBeg,&zEnd);
  // when zero length, there is nothing to draw
  if (xEnd-xBeg<=0) return;
  xBeg -= epsilon;
  xEnd += epsilon;
  // rasterize into a horizon
  float *horizon = CurrBuffer();

  // each segment maintains minimum Y
  int iBeg = toIntCeil(xBeg); // first fully covered
  int iEnd = toIntFloor(xEnd); // first not fully covered
	float fBeg = iBeg; //avoid many LHS on int->float conversion!

  // we can fill only slots which are fully covered
  // if no slot covered, nothing can be filled
  
  // fill partial coverage for the beginning and the ending slot
  // note: this may be even the same one
  if (iBeg>iEnd)
  {
    // with one slot only
    float xPartBeg = xBeg-(fBeg-1);
    float xPartEnd = xEnd-(fBeg-1);
		float yPart = floatMin(yBeg,yEnd);
		Assert(xPartBeg>=0 && xPartBeg<=1);
    Assert(xPartEnd>=0 && xPartEnd<=1);
    if (iBeg-1>=0 && iBeg-1<xSamples && !CheckHorizon(horizon,iBeg-1,yPart))
      _yPart[iBeg-1].Add(xPartBeg,xPartEnd,yPart,_t);
    return;
  }
  
	float ddiv = 1.0f / (xEnd-xBeg);
  float dydx = (yEnd-yBeg)*ddiv;
  float dzdx = (zEnd-zBeg)*ddiv;

  // calculate min for the first slot
  // advance to first slot
  float y1 = yBeg+dydx*(fBeg-xBeg);
  float z1 = zBeg+dzdx*(fBeg-xBeg);
  float y0 = y1;
  float z0 = z1;


  // provide partial coverage for the first slot
  float xPartBeg = xBeg-(fBeg-1);
  float yPart = floatMin(yBeg,y0);
  Assert(xPartBeg>=0 && xPartBeg<=1);
  if (iBeg-1>=0 && iBeg-1<xSamples && !CheckHorizon(horizon,iBeg-1,yPart))
    _yPart[iBeg-1].Add(xPartBeg,1,yPart,_t);

  //Color color(0,1,1,0.7);
  //float size = 0.1;
  // process all following covered slots
  for (int i=iBeg; i<iEnd; i++)
  {
    y0 = y1;
    z0 = z1;
    y1 += dydx;
    float yMin = floatMin(y0,y1);
    SetHorizon(horizon,i,yMin);
    if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
    {
			z1 += dzdx;
      DiagHorSpace(i,y0,z0,Color(0,1,1,0.5),0.25f);
      //DiagHor2DSpace(i,y0,Color(0,1,1,0.3),_t);
    }
  }

  // provide partial coverage for the last slot
  float xPartEnd = xEnd-iEnd;
  Assert(xPartEnd>=0 && xPartEnd<=1);
  yPart = floatMin(y1,yEnd);
  if (iEnd>=0 && iEnd<xSamples && !CheckHorizon(horizon,iEnd,yPart))
    _yPart[iEnd].Add(0,xPartEnd,yPart,_t);
}

/**
@return true when the line is fully occluded
*/
bool HorizonPrune::CheckLineScreen(Vector3Par line0, Vector3Par line1) const
{
  // transform end points
  // find corresponding samples
  // check in the old horizon
  
	float yBeg = line0.Y();
	float yEnd = line1.Y();

	{
		float yMax = floatMax(yBeg, yEnd);

		//max minimum is higher, no chance to reach above horizon
		if(yMax < _maxMinimum2)
		{
			//checkrejected++;
			return true;
		}
	}
	//checkpassed++;

  float xBeg = line0.X()*xSamples*0.5f+xSamples*0.5f;
  float zBeg = line0.Z();
  float xEnd = line1.X()*xSamples*0.5f+xSamples*0.5f;
  float zEnd = line1.Z();
  // make sure x order is known
  if (xBeg>xEnd) SwapQ(&xBeg,&xEnd),SwapQ(&yBeg,&yEnd),SwapQ(&zBeg,&zEnd);
  // when zero length, there is nothing to test
  if (xEnd-xBeg<=0) return true;
  // check against the function
  const float *horizon = Prev2Buffer();

  // we need to include both endpoints in the check
  int iBeg = toIntFloor(xBeg);
  int iEnd = toIntFloor(xEnd);
  if (iBeg==iEnd)
  {
    // singular case - only one checkpoint
    float yMax = floatMax(yBeg,yEnd);
    if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
      DiagHor2DSpace(iBeg,yMax,Color(1,0,0,0.3),5);
    return CheckHorizon(horizon,iBeg,yMax);
  }
	float fBeg = iBeg; //avoid many LHS on int->float conversion

	float ddiv = 1.0f/(xEnd-xBeg);
  float dydx = (yEnd-yBeg)*ddiv;
  float dzdx = (zEnd-zBeg)*ddiv;
  
  float y1 = yBeg+dydx*(fBeg+1-xBeg);
  float z1 = zBeg+dzdx*(fBeg-xBeg);
  float yMax = floatMax(yBeg,y1);

  if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
  {
    DiagHor2DSpace(fBeg,yMax,Color(1,0,0,0.3),5);
    DiagHorSpace(fBeg,yMax,z1,Color(1,0,0,0.5),0.2);
  }

  if (!CheckHorizon(horizon,iBeg,yMax))
    return false;
  for (int i=iBeg+1; i<iEnd; i++)
  {
    float yMax = floatMax(y1,y1+dydx);
    
    if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
    {
      DiagHor2DSpace(i,yMax,Color(1,0,0,0.3),5);
      DiagHorSpace(i,yMax,z1,Color(1,0,0,0.5),0.2);
			z1 += dzdx;
    }
    if (!CheckHorizon(horizon,i,yMax))
      return false;
    y1 += dydx;
  }
  //float y0 = yEnd+dydx*(iEnd-xEnd);
  yMax = floatMax(y1,yEnd);

  if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
  {
    DiagHor2DSpace(iEnd,yMax,Color(1,0,0,0.3),5);
    DiagHorSpace(iEnd,yMax,z1,Color(1,0,0,0.5),0.2);
  }
  if (!CheckHorizon(horizon,iEnd,yMax))
    return false;
  return true;
}

inline bool HorizonPrune::CheckPointScreen(Vector3Par p0) const
{
	float x = p0.X()*xSamples*0.5f+xSamples*0.5f;
	float y = p0.Y();
  // check against the function
  const float *horizon = Prev2Buffer();

  int i = toIntFloor(x);
  return CheckHorizon(horizon,i,y);
}

inline bool HorizonPrune::CheckPointView(Vector3Par v0) const
{
  Assert(TestViewSpaceClip(v0));
  Vector3Val p0 = Project(v0);
  return CheckPointScreen(p0);
}

bool HorizonPrune::CheckLineView(Vector3Par v0, Vector3Par v1) const
{
  // if we are not fully inside, we cannot tell if visible or not
  
  Assert(TestViewSpaceClip(v0));
  Assert(TestViewSpaceClip(v1));

#if 1
  if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
  {
    Color color(1,0.5,0,0.7);
    float size = 0.1f;
    DiagViewSpace(v0,color,size);
    DiagViewSpace(v1,color,size);
  }
#endif

  Vector3Val p00 = Project(v0);
  Vector3Val p10 = Project(v1);
  return CheckLineScreen(p00,p10);
}

/**
@param mayBeClipped what clipping is possible in the camera view space
@return true when minmax box is fully occluded
*/
bool HorizonPrune::CheckMinmaxBox(const Vector3 minmax[2], ClipFlags mayBeClipped) const
{
  // TODO: consider line clipping by a projection plane?
  // we know checking top face of the box is enough
  float y = minmax[1].Y();
  
  Vector3 w00(minmax[0].X(),y,minmax[0].Z());
  Vector3 w10(minmax[1].X(),y,minmax[0].Z());
  Vector3 w11(minmax[1].X(),y,minmax[1].Z());
  Vector3 w01(minmax[0].X(),y,minmax[1].Z());
  
  if (CHECK_DIAG(DEPrune) && PruneDiagNowOn)
  {
    Color color(1,1,0,0.7);
    float size = 0.2f;
    DiagWorldSpace(w00,color,size);
    DiagWorldSpace(w01,color,size);
    DiagWorldSpace(w10,color,size);
    DiagWorldSpace(w11,color,size);
  }

  // if any of the points is outside of the pruning view space
  // we cannot decide
  Vector3 v00 = Transform(w00);
  Vector3 v10 = Transform(w10);
  if (!TestViewSpaceClip(v00)) return false;
  if (!TestViewSpaceClip(v10)) return false;
	Vector3 p00 = Project(v00);
	Vector3 p10 = Project(v10);
  if (!CheckLineScreen(p00,p10)) return false;
  
  Vector3 v11 = Transform(w11);
  if (!TestViewSpaceClip(v11)) return false;
	Vector3 p11 = Project(v11);
  if (!CheckLineScreen(p10,p11)) return false;
  
  Vector3 v01 = Transform(w01);
  if (!TestViewSpaceClip(v01)) return false;
	Vector3 p01 = Project(v01);
  if (!CheckLineScreen(p11,p01)) return false;
  if (!CheckLineScreen(p01,p00)) return false;
  return true;
}

/**
@param mayBeClipped what clipping is possible in the camera view space
*/

bool HorizonPrune::CheckMinmaxBox(const Object* obj, const Vector3 minmax[2], ClipFlags mayBeClipped) const
{
  SetModel(obj);
  // first of all check if center is hidden - if not, it has no sense to try any other point
  Vector3Val v0 = Transform(obj->FutureVisualState().Position());
  if (!TestViewSpaceClip(v0)) return false;
  if (!CheckPointView(v0)) return false;

  float y = minmax[1].Y();  
  Vector3 m00(minmax[0].X(),y,minmax[0].Z());
  Vector3 m10(minmax[1].X(),y,minmax[0].Z());
  Vector3 m11(minmax[1].X(),y,minmax[1].Z());
  Vector3 m01(minmax[0].X(),y,minmax[1].Z());
  
  // if any of the points is outside of the pruning view space
  // we cannot decide
  Vector3 v00 = _modelToView.FastTransform(m00);
  Vector3 v10 = _modelToView.FastTransform(m10);
  if (!TestViewSpaceClip(v00)) return false;
  if (!TestViewSpaceClip(v10)) return false;
	Vector3 p00 = Project(v00);
	Vector3 p10 = Project(v10);
  if (!CheckLineScreen(p00,p10)) return false;
  
  Vector3 v11 = _modelToView.FastTransform(m11);
  if (!TestViewSpaceClip(v11)) return false;
	Vector3 p11 = Project(v11);
  if (!CheckLineScreen(p10,p11)) return false;
  
  Vector3 v01 = _modelToView.FastTransform(m01);
  if (!TestViewSpaceClip(v01)) return false;
	Vector3 p01 = Project(v01);
  if (!CheckLineScreen(p11,p01)) return false;
  if (!CheckLineScreen(p01,p00)) return false;
  return true;
}

void HorizonPrune::AddObjectHorizon(const Object* obj, const Shape *view, const AutoArray<OcclusionEdge> &edges)
{
  SetModel(obj);
  // TODO: if the object is too small to be any significant, do not add it any more
  // TODO: optimization: transform each vertex to view/screen space only once
  //Matrix4 modelToView = _invViewSpace*toWorld;

  const AutoArray<Vector3>& posarray = view->GetPosArray();

  for (int e=0; e<edges.Size(); e++)
  {
    Vector3 pos0 = _modelToView.FastTransform(posarray[edges[e].a]);
    Vector3 pos1 = _modelToView.FastTransform(posarray[edges[e].b]);
    int cc0 = ViewSpaceClipPoint(pos0);
    int cc1 = ViewSpaceClipPoint(pos1);

    if((cc0 & cc1) != 0)
      continue; //line is completely behind something

    if((cc0 | cc1) == 0)
    {
      //Vector3 lp0 = obj->FutureVisualState().Transform().FastTransform(posarray[edges[e].a]);
      //Vector3 lp1 = obj->FutureVisualState().Transform().FastTransform(posarray[edges[e].b]);
      //enf::Shape::CreateLine(lp0, lp1, 0xffff0000, enf::SS_ONCE);

      //line is completely unclipped
      AddLineScreen(Project(pos0), Project(pos1));
      continue;
    }

    Vector3 c0,c1;
    bool somethingLeft = ViewSpaceClip(c0,c1,pos0,pos1);
    if (somethingLeft)
    {
      //DiagViewSpace(c0,color,0.15f);
      //DiagViewSpace(c1,color,0.15f);
      AddLineScreen(Project(c0), Project(c1));
    }
  }
}

void HorizonPrune::AddHorizonLine(const Vector3 * __restrict line, int nPoints)
{
  Assert(nPoints>=2);

  //float x0 = (x-xSamples*0.5f)/(xSamples*0.5f);
  //float x1 = (x+1-xSamples*0.5f)/(xSamples*0.5f);
  // we need to convert from screen-space to view-space first
  // we know only approximate z, therefore we will use it

  AutoArray<Vector3, MemAllocDataStack<Vector3, 1024, AllocAlign16> > tpos;
  AutoArray<int, MemAllocDataStack<int,1024> > ccodes;
  tpos.Resize(nPoints);
  ccodes.Resize(nPoints);

	int andcc = 1|2|4, orcc = 0;

  for(int i=0; i < nPoints; i++)
  {
    Vector3 pos = Transform(line[i]);
    tpos[i] = pos;
		int cc = ViewSpaceClipPoint(pos);
		andcc &= cc;
		orcc |= cc;
    ccodes[i] = cc;
  }

	//fast reject completely clipped lines
	if(andcc != 0)
		return;

	//something is clipped, take slower way
	if(orcc)
	{
		//DiagViewSpace(v0,color,0.2f);
		Vector3 proj;
		int cc0 = ccodes[0], cc1;
		if(cc0 == 0)
			proj = Project(tpos[0]);

		for (int i=0; i<(nPoints-1); i++,cc0 = cc1)
		{
			// rasterize only lines which are not behind the projection plane

			//DiagViewSpace(v1,color,0.2f);
			// perform basic clipping to avoid rendering outside of the view-space
			cc1=ccodes[i+1];
	    
			if((cc0 & cc1) != 0)
			{
				continue; //line is completely behind something
			}

			if((cc0 | cc1) == 0)
			{
				//line is completely unclipped
				Vector3Val pc1 = Project(tpos[i+1]);
				AddLineScreen(proj,pc1);
				proj = pc1;
				continue;
			}

			Vector3 c0,c1;
#if 0
//this *SHOULD* work, but it isn't. WHY???

			ViewSpaceClipCC(c0,c1,tpos[i],cc0,tpos[i+1],cc1);

			Vector3Val pc0 = Project(c0);
			proj = Project(c1);
			AddLineScreen(pc0,proj);
#else
			bool somethingLeft = ViewSpaceClip(c0,c1,tpos[i],tpos[i+1]);
			if (somethingLeft)
			{

				//DiagViewSpace(c0,color,0.15f);
				//DiagViewSpace(c1,color,0.15f);
	      
				Vector3Val pc0 = Project(c0);
				proj = Project(c1);
				AddLineScreen(pc0,proj);
			}
#endif
		}
	}
	else
	{
		//fast unclipped way
		//DiagViewSpace(v0,color,0.2f);
		
		Vector3 proj = Project(tpos[0]);

		for (int i=1; i<nPoints; i++)
		{
			//DiagViewSpace(v1,color,0.2f);

			//line is completely unclipped
			Vector3Val pc1 = Project(tpos[i]);
			AddLineScreen(proj,pc1);
			proj = pc1;
		}
	}
}

void HorizonPrune::AddTransformedHorizonLine(const Vector3 * __restrict tpos, int nPoints)
{
	Assert(nPoints>=2);

	//float x0 = (x-xSamples*0.5f)/(xSamples*0.5f);
	//float x1 = (x+1-xSamples*0.5f)/(xSamples*0.5f);
	// we need to convert from screen-space to view-space first
	// we know only approximate z, therefore we will use it

  AutoArray<int, MemAllocDataStack<int,1024> > ccodes;
	ccodes.Resize(nPoints);

	int andcc = 1|2|4, orcc = 0;

	for(int i=0; i < nPoints; i++)
	{
		int cc = ViewSpaceClipPoint(tpos[i]);
		andcc &= cc;
		orcc |= cc;
		ccodes[i] = cc;
	}

	//fast reject completely clipped lines
	if(andcc != 0)
		return;

	//something is clipped, take slower way
	if(orcc)
	{
		//DiagViewSpace(v0,color,0.2f);
		Vector3 proj;
		int cc0 = ccodes[0], cc1;
		if(cc0 == 0)
			proj = Project(tpos[0]);

		for (int i=0; i<(nPoints-1); i++,cc0 = cc1)
		{
			// rasterize only lines which are not behind the projection plane

			//DiagViewSpace(v1,color,0.2f);
			// perform basic clipping to avoid rendering outside of the view-space
			cc1=ccodes[i+1];

			if((cc0 & cc1) != 0)
				continue; //line is completely behind something

			if((cc0 | cc1) == 0)
			{
				//line is completely unclipped
				Vector3Val pc1 = Project(tpos[i+1]);
				AddLineScreen(proj,pc1);
				proj = pc1;
				continue;
			}

			Vector3 c0,c1;
#if 0
			//this *SHOULD* work, but it isn't. WHY???

			ViewSpaceClipCC(c0,c1,tpos[i],cc0,tpos[i+1],cc1);

			Vector3Val pc0 = Project(c0);
			proj = Project(c1);
			AddLineScreen(pc0,proj);
#else
			bool somethingLeft = ViewSpaceClip(c0,c1,tpos[i],tpos[i+1]);
			if (somethingLeft)
			{

				//DiagViewSpace(c0,color,0.15f);
				//DiagViewSpace(c1,color,0.15f);

				Vector3Val pc0 = Project(c0);
				proj = Project(c1);
				AddLineScreen(pc0,proj);
			}
#endif
		}
	}
	else
	{
		//fast unclipped way
		//DiagViewSpace(v0,color,0.2f);

		Vector3 proj = Project(tpos[0]);

		for (int i=1; i<nPoints; i++)
		{
			//DiagViewSpace(v1,color,0.2f);

			//line is completely unclipped
			Vector3Val pc1 = Project(tpos[i]);
			AddLineScreen(proj,pc1);
			proj = pc1;
		}
	}
}

#ifdef GEOM_OCCLUDERS
	#define HORIZON_PRUNE 0
#else
	#define HORIZON_PRUNE 1
#endif

#define _SPARSE_HORIZON
#define _FILTER_OCCLUDERS

static float PROJ_NEAR = 1.0f;

namespace enf
{
struct WorldOccluder
{
	ENF_DECLARE_SMALLOBJECT(WorldOccluder);
	AAB		LocalBounds;
	AAB		OriginalBounds;
	Matrix43	Matrix;

	WorldOccluder() {}
};


//-----------------------------------------------------------------------
struct TraverseOccluder
{
	static const uint MAX_PLANES = 10;
	static const uint MAX_TUPLES = 8;
	uint		NumPlanes;
	Vector4	Planes[MAX_PLANES];
	Vector3	Tuples[MAX_TUPLES];

	ENF_INLINE bool CheckPlane(Vector4& plane, uint except1, uint except2)
	{
		uint mask = 0;
		for(uint n = 0; n < MAX_TUPLES; n++)
		{
			if(n == except1 || n == except2)
				continue;
			float dist = plane.Dot(Tuples[n]) - plane.w;
			if(dist >= 0)
				mask |= 1;
			else
				mask |= 2;
		}

		if(mask == 3)
			return false;
		if(mask == 2)
			plane = -plane;
		return true;
	}

	bool CreateOccluder(const Camera &cam, const WorldOccluder* occ);

	ENF_INLINE bool TestAABB(const AAB& bbox, uint mask = 0xffffffff) const
	{
		mask &= (1 << NumPlanes) - 1;

		float extx = bbox.GetMaxs().x, exty = bbox.GetMaxs().y, extz = bbox.GetMaxs().z;
		float cx = (bbox.GetMins().x + extx) * 0.5f;
		float cy = (bbox.GetMins().y + exty) * 0.5f;
		float cz = (bbox.GetMins().z + extz) * 0.5f;
		extx -= cx;
		exty -= cy;
		extz -= cz;
		for(uint n = 0; n < NumPlanes && mask; n++)
		{
			if(((1 << n) & mask) == 0)
				continue;
			mask &= ~(1 << n);
			float px = Planes[n].x;
			float py = Planes[n].y;
			float pz = Planes[n].z;
			float d2 = fabsf(extx * px) + fabsf(exty * py) + fabsf(extz * pz);
			float d1 = (px * cx + py * cy + pz * cz) - Planes[n].w;
			if ( d1 - d2 > 0 )
				continue; //all inside

			//something outside
			return false;
		}
		return true;
	}

	ENF_INLINE bool TestAABBUpdate(const AAB& bbox, uint& _mask) const
	{
		uint mask = (1 << NumPlanes) - 1;

		float extx = bbox.GetMaxs().x, exty = bbox.GetMaxs().y, extz = bbox.GetMaxs().z;
		float cx = (bbox.GetMins().x + extx) * 0.5f;
		float cy = (bbox.GetMins().y + exty) * 0.5f;
		float cz = (bbox.GetMins().z + extz) * 0.5f;
		extx -= cx;
		exty -= cy;
		extz -= cz;
		for(uint n = 0, b = 1; n < NumPlanes; n++, b <<= 1)
		{
			float px = Planes[n].x;
			float py = Planes[n].y;
			float pz = Planes[n].z;
			float d2 = fabsf(extx * px) + fabsf(exty * py) + fabsf(extz * pz);
			float d1 = (px * cx + py * cy + pz * cz) - Planes[n].w;

			if ( d1 - d2 > 0 )
			{
				mask ^= b;
				continue;
			}
		}
		if(mask == 0)
			return true;

		_mask = mask;
		return false;
	}

};

//-----------------------------------------------------------------------
typedef Pair<ushort, ushort> TOccluderMask;

class OccluderSystem
{
	static const uint MAX_OCCLUDERS = 128;
	static const uint MAX_OCCLUDER_MASKS = 4096;

	static uint OccluderPos;
	static uint OccluderMaskPos;
	static uint TestCount;
	static TraverseOccluder	Occluders[MAX_OCCLUDERS];
	static TOccluderMask OccluderMasks[MAX_OCCLUDER_MASKS];

public:

	static bool CheckAgainstOccluders(const AAB& wbbox)
	{
		for(uint n = 0; n < OccluderPos; n++)
		{
			TestCount++;
			if(Occluders[n].TestAABB(wbbox))
			{
				//		if(ENF_DIAG_RBOOL(DM_REND_OCCLUDERS))
				//			Shape::CreateBox(wbbox.GetMins(), wbbox.GetMaxs(), 0x1f007f7f, SS_NOZWRITE|SS_TRANSP|SS_WIREFRAME);
				return false;
			}
		}
		//if(ENF_DIAG_RBOOL(DM_REND_OCCLUDERS))
		//	Shape::CreateBox(wbbox.GetMins(), wbbox.GetMaxs(), 0x1f7f7f00, SS_NOZWRITE|SS_TRANSP|SS_WIREFRAME);
		return true;
	}

	static bool CheckAgainstOccluders(const AAB& wbbox, const TOccluderMask* occluders, uint numOccluders)
	{
		for(uint n = 0; n < numOccluders; n++)
		{
			TestCount++;
			if(Occluders[occluders[n].Key].TestAABB(wbbox, occluders[n].Data))
			{
				//		if(ENF_DIAG_RBOOL(DM_REND_OCCLUDERS))
				//			Shape::CreateBox(wbbox.GetMins(), wbbox.GetMaxs(), 0x1f007f7f, SS_NOZWRITE|SS_TRANSP|SS_WIREFRAME);
				return false;
			}
		}
		//if(ENF_DIAG_RBOOL(DM_REND_OCCLUDERS))
		//	Shape::CreateBox(wbbox.GetMins(), wbbox.GetMaxs(), 0x1f7f7f00, SS_NOZWRITE|SS_TRANSP|SS_WIREFRAME);
		return true;
	}


	static uint CheckAgainstOccludersUpdate(const AAB& wbbox, const TOccluderMask*& pmasks)
	{
		uint firstOccluderMask = OccluderMaskPos;
		if(firstOccluderMask == MAX_OCCLUDER_MASKS)
		{
			return CheckAgainstOccluders(wbbox) ? 0 : -1;
		}

		TOccluderMask* masks = &OccluderMasks[firstOccluderMask];
		uint numOccluders = 0;
		for(uint n = 0; n < OccluderPos; n++)
		{
			uint mask = 0;
			TestCount++;
			if(Occluders[n].TestAABBUpdate(wbbox, mask))
			{
				//		if(ENF_DIAG_RBOOL(DM_REND_OCCLUDERS))
				//			Shape::CreateBox(wbbox.GetMins(), wbbox.GetMaxs(), 0x1f007f7f, SS_NOZWRITE|SS_TRANSP|SS_WIREFRAME);
				return -1;
			}
			if((numOccluders + firstOccluderMask) < MAX_OCCLUDER_MASKS)
			{
				masks[numOccluders].Key = n;
				masks[numOccluders].Data = mask;
				numOccluders++;
			}
		}
		//if(ENF_DIAG_RBOOL(DM_REND_OCCLUDERS))
		//	Shape::CreateBox(wbbox.GetMins(), wbbox.GetMaxs(), 0x1f7f7f00, SS_NOZWRITE|SS_TRANSP|SS_WIREFRAME);
		OccluderMaskPos += numOccluders;
		pmasks = masks;
		return numOccluders;
	}

	static bool CreateOccluder(const Camera &cam, const WorldOccluder* occ)
	{
		if(OccluderPos == MAX_OCCLUDERS)
			return false;

		AAB wbbox;
		wbbox.InitFromOBB(occ->Matrix, occ->LocalBounds);

		if(occ->LocalBounds.Intersects(occ->Matrix.InvTransformCoord(cam.Position())))
			return false;

		//avoid tiny occluders
		//TODO: transform it to camera space, make 2D bound-box and check only the smaller axis!
		float distance = (wbbox.GetCenter() - cam.Position()).Size();
		float rad = wbbox.GetSize().Size() * 0.5f * enf_inve(distance + PROJ_NEAR);

		if(rad < 0.007f)
			return false;

		//fast test against frustum
		if(cam.IsClipped(wbbox.GetMinMax(), ClipAll))
			return false;

		//fast test against occluders
		if(!CheckAgainstOccluders(wbbox))
			return false;

		if(Occluders[OccluderPos].CreateOccluder(cam, occ))
		{
			OccluderPos++;
			return true;
		}
		return false;
	}

	static void Begin()
	{
		OccluderPos = 0;
		OccluderMaskPos = 0;
		TestCount = 0;
	}
};

//-----------------------------------------------------------------------
ENF_INLINE bool PlaneFromPoints(Vector4& plane, const Vector3& p0, const Vector3& p1, const Vector3& p2)
{
	Vector3 edge1 = p1 - p0;
	Vector3 edge2 = p2 - p0;
	Vector3 norm = edge2.CrossProduct(edge1);
	double sqLength = norm.SquareSize();
	if(sqLength < 0.001)
		return false;
	
	double ilength = 1.0 / sqrt(sqLength);
	norm.x *= ilength;
	norm.y *= ilength;
	norm.z *= ilength;
	plane.x = norm.x;
	plane.y = norm.y;
	plane.z = norm.z;
	plane.w = norm.DotProduct(p0);
	return true;
}
bool showOccluders = false;

//-----------------------------------------------------------------------
bool TraverseOccluder::CreateOccluder(const Camera& cam, const WorldOccluder* occ)
{
	Vector3 boxCorners[MAX_TUPLES];

	NumPlanes = 0;
	const Vector3& mins = occ->LocalBounds.GetMins();
	const Vector3& maxs = occ->LocalBounds.GetMaxs();
	Tuples[0] = occ->Matrix.FastTransform(Vector3(mins[0], maxs[1], maxs[2]));
	Tuples[1] = occ->Matrix.FastTransform(Vector3(maxs[0], maxs[1], maxs[2]));
	Tuples[2] = occ->Matrix.FastTransform(Vector3(maxs[0], mins[1], maxs[2]));
	Tuples[3] = occ->Matrix.FastTransform(Vector3(mins[0], mins[1], maxs[2]));
	Tuples[4] = occ->Matrix.FastTransform(Vector3(mins[0], maxs[1], mins[2]));
	Tuples[5] = occ->Matrix.FastTransform(Vector3(maxs[0], maxs[1], mins[2]));
	Tuples[6] = occ->Matrix.FastTransform(Vector3(maxs[0], mins[1], mins[2]));
	Tuples[7] = occ->Matrix.FastTransform(Vector3(mins[0], mins[1], mins[2]));

	//slow test tuples against frustum
/*
	uint and = 7;
	for(uint n = 0; n < 8; n++)
	{
		if(frustum.TestPoint(Tuples[n])
			and &= 1 << n;
	}
	if(and)
		return;
*/
	//Shape::CreateLine(cameraPos + Vector3(16,16,16), cameraPos + Vector3(16,16,16) + cameraMat.GetRow(0) * 32, 0xffff0000, SS_ONCE);

	const Vector3& cameraPos = cam.Position();

	//find front planes

	for(uint n = 0; n < 3; n++)
	{
		Vector3 plane = occ->Matrix[n];
		float planeDist = plane.DotProduct(Tuples[1]); //maxs

		float dist = plane.DotProduct(cameraPos) - planeDist;
		if(dist > 0)
		{
			Planes[NumPlanes] = -plane;
			Planes[NumPlanes].w = -planeDist;
			NumPlanes++;
			continue;
		}

		plane = -plane;
		planeDist = plane.DotProduct(Tuples[7]); //mins
		dist = plane.DotProduct(cameraPos) - planeDist;
		if(dist > 0)
		{
			Planes[NumPlanes] = -plane;
			Planes[NumPlanes].w = -planeDist;
			NumPlanes++;
		}
	}

	static const ushort indices[12][2] = {
		0, 1,
		1, 2,
		2, 3,
		3, 0,
		0, 4,
		1, 5,
		2, 6,
		3, 7,
		4, 5,
		5, 6,
		6, 7,
		7, 4
	};

	for(uint n = 0; n < 12; n++)
	{
		uint p0 = indices[n][0];
		uint p1 = indices[n][1];
		if(PlaneFromPoints(Planes[NumPlanes], cameraPos, Tuples[p0], Tuples[p1]) &&
			CheckPlane(Planes[NumPlanes], p0, p1))
		{
			NumPlanes++;
		}
	}

	if(NumPlanes > 0)
	{
//		if (ENF_DIAG_BOOL(DM_REND_OCCLUDERS_VOL))
		{
			//revert planes before that
			if(showOccluders)
			{
				Vector4 temp[MAX_PLANES];
				for(uint n = 0; n < NumPlanes; n++)
					temp[n] = Planes[n] * -1.0f;
				enf::Shape::CreateConvex(temp, sizeof(Vector4), NumPlanes, 0x4f7f0000, SS_TRANSP|SS_NOZWRITE|SS_DOUBLESIDE)->AddRef();
				Ref<enf::Shape> sh = enf::Shape::CreateBox(mins, maxs, 0x7f007f00, SS_WIREFRAME|SS_TRANSP|SS_NOZWRITE);
				sh->SetMatrix(occ->Matrix);
				sh->AddRef();
			}
//			enf::Shape::CreateBox(mins, maxs, 0x7f007f00, SS_ONCE|SS_WIREFRAME|SS_TRANSP|SS_NOZWRITE)->SetMatrix(occ->Matrix);
		}
	}

	return true;
}

TraverseOccluder OccluderSystem::Occluders[MAX_OCCLUDERS];
uint OccluderSystem::OccluderPos = 0;
TOccluderMask OccluderSystem::OccluderMasks[MAX_OCCLUDER_MASKS];
uint OccluderSystem::OccluderMaskPos = 0;
uint OccluderSystem::TestCount = 0;

}

bool CheckAgainstOccluders(const enf::AAB& wbbox)
{
	return enf::OccluderSystem::CheckAgainstOccluders(wbbox);
}


static bool DoGeomOccluder = true;

/*!
@param inPreload during preload some functionality may be a bit different
@param preloadDist during the preload the preloading distance is gradually increasing
*/
void World::PrepareDraw(Object *camInside, bool inPreload, float preloadDist)
{
	PROFILE_SCOPE_GRF_EX(wPrep,*,SCOPE_COLOR_RED);
  //TrapFor0x8000Scope lock("World::PrepareDraw");

//	enf::showOccluders = false;

	// give the scene opportunity to precalculate needed constants
	_scene.PrepareDraw();
	Vector3Val pos = _scene.GetCamera()->Position();
#if HORIZON_PRUNE
	Vector3Val dir = _scene.GetCamera()->Direction();
#endif

#if _ENABLE_CHEATS
	if (GInput.GetCheat3ToDo(DIK_MINUS))
	{
		Glob.config.objectsZ = toInt(Glob.config.objectsZ/1.1f);
		GlobalShowMessage(500,"Obj draw dist %.3f",Glob.config.objectsZ);
	}
	if (GInput.GetCheat3ToDo(DIK_EQUALS))
	{
		Glob.config.objectsZ = toInt(Glob.config.objectsZ*1.1f);
		saturateMin(Glob.config.objectsZ,Glob.config.horizontZ);
		GlobalShowMessage(500,"Obj draw dist %.3f",Glob.config.objectsZ);
	}
#endif
	int objRange = ObjRange;
	float maxObjDist = floatMin(_scene.GetFogMaxRange(),Glob.config.objectsZ);
	// we need to be sure any objects which could cast a shadow
	// into the view frustum will be handled
	float shadowRadius = Glob.config.GetMaxShadowSize();
	float maxShadowDist = _scene.GetObjectShadows() ? Glob.config.GetMaxShadowSize()+Glob.config.GetShadowZ() : 0.0f;

	float maxDist = floatMax(maxObjDist,maxShadowDist);

	GridRectangle boundRect;
  bool camInsideVisited = false;
#if _ENABLE_REPORT
  bool myGridVisited = false;
  int myGridX = toIntFloor(pos.X()*InvLandGrid);
  int myGridZ = toIntFloor(pos.Z()*InvLandGrid);
  saturate(myGridX,0,LandRange-1);
  saturate(myGridZ,0,LandRange-1);
#endif
  
	FlexibleLoop2 floop;
	bool doPrepare = _scene.Calcul2DEdges(maxDist, ObjGrid, objRange, true, floop, boundRect);

	// convert end inclusive to end exclusive
	boundRect.xEnd++,boundRect.zEnd++;

  // we want the locked rectangle around the camera to be rotation invariant
  // turning around is a frequent thing and should not case any locking/unlocking traffic
	GridRectangle circleRect;

  float iGrid = _scene.GetLandscape()->GetInvLandGrid();
  circleRect.xBeg = toIntFloor((pos.X()-maxDist)*iGrid);
  circleRect.zBeg = toIntFloor((pos.Z()-maxDist)*iGrid);
  circleRect.xEnd = toIntCeil((pos.X()+maxDist)*iGrid);
  circleRect.zEnd = toIntCeil((pos.Z()+maxDist)*iGrid);
  
  // make sure it always covers the rectangle which is needed for rendering
  GridRectangle objRect = circleRect + boundRect;

	saturate(objRect.xBeg,0,objRange);
	saturate(objRect.xEnd,0,objRange);
	saturate(objRect.zBeg,0,objRange);
	saturate(objRect.zEnd,0,objRange);

  if (inPreload)    
  {
    //TrapFor0x8000Scope lock("World::PrepareDraw inPreload");

    // in preload use incremental locking to make sure we are not saturating the input system
    int xCam = toIntFloor(pos.X()*iGrid);
    int zCam = toIntFloor(pos.Z()*iGrid);
    GridRectangle rectCam(xCam,zCam,xCam+1,zCam+1);
    const GridRectangle &reqRect = _objAroundCam->GetRectangle();
    if (!rectCam.IsInside(reqRect))
      // if rectangle does not contain the camera, reset it
      _objAroundCam->ChangeRectangle(rectCam);
    else
    {
      if (_objAroundCam->LeftToLock()==0)
      {
        Assert(rectCam.Height()>0);
        Assert(rectCam.Width()>0);
        // increase the rectangle step by step, but never exceed the preloadDist
        int curRange = intMax(intMax(reqRect.Height()-1,reqRect.Width()-1)/2,0);
        int maxRange = toIntCeil(preloadDist*iGrid);
        int nextRange = intMin(curRange+1,maxRange);
        if (nextRange>curRange)
        {
          GridRectangle aroundCam(xCam-nextRange,zCam-nextRange,xCam+nextRange+1,zCam+nextRange+1);
          _objAroundCam->ChangeRectangle(aroundCam&objRect);
        }
      }
	  }
  }
  else
  {
    //TrapFor0x8000Scope lock("World::PrepareDraw else inPreload");
	 
	  _objAroundCam->ChangeRectangle(objRect);
  }
  _objAroundCam->DoWork();
	if (camInside)
		camInside->PrepareInsideRendering();

	// we need to avoid empty rectangle, as the flexible loop cannot handle it
	if(doPrepare)
	{
#if HORIZON_PRUNE
		// traverse land grid
		// flexible  loop - minor/major can be x or z
		bool zMajor = fabs(dir.Z())>fabs(dir.X());
		int zStep = dir.Z()<0 ? -1 : +1;
		int xStep = dir.X()<0 ? -1 : +1;
#endif

		const Landscape *l =  _scene.GetLandscape();
#if _ENABLE_CHEATS
		static int disablePruning = 0;
		static bool fixedPruningPos = false;
		static Vector3 pruningPos = VZero;
#else
		const int disablePruning = 0;
		const bool fixedPruningPos = false;
		const Vector3 pruningPos = VZero;
#endif
		enf::OccluderSystem::Begin();
#if HORIZON_PRUNE
		float fov = floatMax(_scene.GetCamera()->Left(),_scene.GetCamera()->Top());
		// starting corner for the loop    
		//Vector3 p00(loop.BegX()*LandGrid,0,loop.BegZ()*LandGrid);
		// initialize horizon pruning

		// We need some guard-band area around the maximum fov
		// this is because we cannot assume parts of the objects outside of the prune view space
		// are not on the screen and we do not want to test real view space clipping as well
		// however, we cannot perform pruning in more than 90 degree fov    
		float prunedFov = floatMin(fov*1.2,1);
		// view space origin is the camera position
		HorizonPrune prune(dir,fixedPruningPos ? pruningPos : pos,prunedFov);

#if _ENABLE_CHEATS
		if (int dif=GInput.GetCheat3ToDo(DIK_5)+GInput.GetCheatXToDo(CXTPruneMode))
		{
			disablePruning += dif;
			disablePruning %= 3;
			static const char *prune[]={"Full","Grid","None"};
			GlobalShowMessage(500, "Pruning mode %s", prune[disablePruning]);
		}
		if (GInput.GetCheat3ToDo(DIK_6))
		{
			fixedPruningPos = !fixedPruningPos;
			if (pruningPos==VZero) pruningPos = pos;
			GlobalShowMessage(500, "Fixed pruning pos %s", fixedPruningPos ? "On" : "Off");
		}
		if (GInput.GetCheat3ToDo(DIK_7))
		{
			GlobalShowMessage(500, "Fixed pruning pos captured");
			pruningPos = pos;
		}

// 		if (GInput.GetCheat3ToDo(DIK_W))
// 		{
// 			if (PruneMinZ<=0)
// 				PruneMinZ = toInt(_scene.GetCamera()->Position().Z()*InvLandGrid);
// 			else
// 				PruneMinZ++;
// 			PruneMaxZ = PruneMinZ+1;
// 		}
// 		if (GInput.GetCheat3ToDo(DIK_S))
// 		{
// 			if (PruneMinZ<=0)
// 				PruneMinZ = toInt(_scene.GetCamera()->Position().Z()*InvLandGrid);
// 			else
// 				PruneMinZ--;
// 			PruneMaxZ = PruneMinZ+1;
// 		}
// 
// 		if (GInput.GetCheat3ToDo(DIK_A))
// 		{
// 			if (PruneMinX<=0)
// 				PruneMinX = toInt(_scene.GetCamera()->Position().X()*InvLandGrid);
// 			else
// 				PruneMinX++;
// 			PruneMaxX = PruneMinX+1;
// 		}
// 		if (GInput.GetCheat3ToDo(DIK_D))
// 		{
// 			if (PruneMinX<=0)
// 				PruneMinX = toInt(_scene.GetCamera()->Position().X()*InvLandGrid);
// 			else
// 				PruneMinX--;
// 			PruneMaxX = PruneMinX+1;
// 		}
#endif

		AutoArray<Vector3, MemAllocDataStack<Vector3,1024,AllocAlign16> > lineTerrain;
		int subdivLog = l->GetSubdivLog();
#endif

		//prepare as much constants as possible
		float objGrid = ObjGrid; //avoid many reads from Landscape
		float sqMaxDistObjGrid = Square(maxDist+objGrid);
		// we may not quick-cull rectangles or objects which may cast shadow into the frustum
		const float maxShadowSize = Glob.config.GetMaxShadowSize();
		// one ObjGrid is for possible object radius, 2nd is for grid size
		float sqMaxShadowDist = Square(Glob.config.GetShadowZ()+maxShadowSize+objGrid+objGrid);
		const Camera &cam=*_scene.GetCamera();

    _scene.DrawIRRays();
    

		// line index
		int lineI=0;
		DoAssert((floop.GetMajorBegin()<floop.GetMajorEnd()) || (floop.GetMajorIncrement()<0));
		DoAssert((floop.GetMajorBegin()>floop.GetMajorEnd()) || (floop.GetMajorIncrement()>0));
  

#ifdef TRAP_FOR_0x8000
    //GEngine->TrapFor0x8000("World.PrepareDraw ENTERING major grid traversal loop");
#endif
		for (int major = floop.GetMajorBegin(); major != floop.GetMajorEnd(); major+=floop.GetMajorIncrement(),lineI++)
		{
			int minorBegin = floop.GetMinorBegin(major);
			int minorEnd = floop.GetMinorEnd(major);

      // FIXME: there are situations in which the assert appears and we don't have time to fix it now.
      // In addition, the for cycle below handles that case properly.
			// DoAssert(minorBegin<minorEnd);

			for (int minor = minorBegin; minor < minorEnd; minor++)
			{
				int x = floop.GetX(minor, major);
				int z = floop.GetZ(minor, major);

				DoAssert(x >= 0 && z >= 0 && x < objRange && z < objRange);

				// some lists may be rejected even before loading the list
				// this is easy depending on distance
				// compute XZ (horizontal) distance of the grid center from the camera
				float distXZ2 = Square(x*objGrid+objGrid*0.5f-pos.X())+Square(z*objGrid+objGrid*0.5f-pos.Z());

				if (distXZ2>sqMaxDistObjGrid)
				{
					// we may quick reject some grids as not necessary
					// we may not quick reject border grid, as them may contain
					// vehicles outside of the landscape
					if (x!=0 && x!=LandRangeMask && z!=0 && z!=LandRangeMask)
						continue;
				}

#if _ENABLE_REPORT
        if (x==myGridX && z==myGridZ)
        {
          myGridVisited = true;
        }
#endif

				bool shadowPossible = distXZ2<sqMaxShadowDist;

				// if the list distance is high, we prefer not to wait for it if it is not loaded yet
				Landscape::UseObjectsMode noWait = distXZ2>Square(200) ? Landscape::UONoWait : Landscape::UOWait;
				const ObjectListUsed &listUsed=l->UseObjects(x,z,noWait);
				if( listUsed.IsNull() ) continue;
				const ObjectListFull *list = listUsed.GetList();

				Vector3Val bCenter=list->GetBSphereCenter();
				float bRadiusObj = list->GetBSphereRadius();
				// we need to consider shadow radius as well, otherwise shadows will disappear very quickly
				float bRadius = bRadiusObj+shadowRadius;
				// andClip is used for static models only
				ClipFlags orClip;
				ClipFlags andClip;
				ClipFlags andClipObj;
				if (list->GetNonStaticCount()<list->Size())
        {
					andClip = cam.IsClipped(bCenter,bRadius,&orClip);
					andClipObj = cam.IsClipped(bCenter,bRadiusObj,&orClip);
        }
				else
				{
					// if there are no static objects, we may assume them clipped
					// no need to test clipping or pruning
					andClip = ClipAll;
					andClipObj = ClipAll;
					orClip = ClipAll;
				}
				if (andClip==0)
				{
#if _ENABLE_CHEATS
					PruneDiagNowOn = PruneMinZ<=z && PruneMaxZ>z && PruneMinX<=x && PruneMaxX>x;
#endif
					// when biggest static object is too small, pretend all static objects are clipped
					if (!_scene.CanBeDrawing(distXZ2,list->GetMaxStaticArea())) andClip = ClipAll;
				}

				if (shadowPossible)
				{
					/// more accurate per-grid shadow decision
					const bool accurateShadowPossible = false;
					if (accurateShadowPossible)
						shadowPossible = !_scene.IsShadowInvisible(Glob.config.GetShadowZ(), bCenter, distXZ2, bRadius, 0, false);
				}

				// we can force invisible LOD here
				int forceLOD = -1;
				// check for visibility
#if HORIZON_PRUNE
        if (andClip==0)
				{
          if (andClipObj)
          {
						if (!shadowPossible)
							andClip = ClipAll;
						else
							forceLOD = LOD_INVISIBLE;
					}
				  else if (disablePruning<2 && prune.CheckMinmaxBox(list->GetMinMax(),orClip))
				  {
					  if (!shadowPossible)
              andClip = ClipAll;
					  else
              forceLOD = LOD_INVISIBLE;
            if (CHECK_DIAG(DEPruneX))
              GScene->DiagBBox(list->GetMinMax(),MIdentity,Color(0.5,1,0),&Scene::ObjectForDrawing);
				  }
					else if (CHECK_DIAG(DEPruneX))
					{
						GScene->DiagBBox(list->GetMinMax(),MIdentity,Color(1,1,0),&Scene::ObjectForDrawing);
					}
        }
#endif
#ifdef GEOM_OCCLUDERS
				const enf::TOccluderMask* masks;
				enf::uint numOccluders;

				if(DoGeomOccluder && (numOccluders = enf::OccluderSystem::CheckAgainstOccludersUpdate(enf::AAB(list->GetMinMax()[0], list->GetMinMax()[1]), masks)) == -1)
				{
					numOccluders = 0;
					if (!shadowPossible)
						andClip = ClipAll;
					else
						forceLOD = LOD_INVISIBLE;
				}
#endif
        // check for forest / tree coverage

        // we could optimize the loop below for a special case, but measurements show it brings no acceleration

				// traverse groups first
				int index = 0;
				// if clipped, no need to render any grouped objects
				if (andClip==0)
				{
					for (int g=0; g<list->NGroups(); g++)
					{
						const ObjectListGroup &grp = list->Group(g);
						// we can make some decisions for the whole group
						// if we skip this group, we know we can skip all the rest
						if (!_scene.CanBeDrawing(distXZ2,grp._maxArea))
              break;

						for ( ;index<grp._end; index++)
						{
							Object *obj = list->Get(index);
							Assert(obj);

							//prefetch next object, if any
							if((index+1)<list->Size())
								PrefetchT0Off(list->Get(index+1), 0);

              Object::Visible visibleStyle = obj->VisibleStyle();
							if (visibleStyle==Object::ObjInvisible) continue;
							// grouped objects are always static (ArmA)
							// this might change with more dynamic world (Game2)
							Assert( obj->Static() );
							ClipFlags clip=orClip;
							int lod = forceLOD;

							// we know shape!=NULL, because we passed Invisible test
							LODShape *shape = obj->GetShape();
							if (!shape->CanOcclude())
							{
								// special case optimization for trees / bushes (forests)
								// we could do it more flexible, however other objects would usually probably get little benefit

								// from certain distance on, we perform only grid pruning on instanced and non-occluding objects
								// we can apply this optimization only for objects which can be instanced
								// we therefore need to avoid animated objects

								static float gridPruningDistance = 200.0f;
								if (!shadowPossible && distXZ2>Square(gridPruningDistance) && shape->GetAnimationType()==AnimTypeNone)
								{
									_scene.ObjectsForDrawing(Scene::ObjectGroup(list,index,grp._end),clip,shadowPossible);
									index = grp._end;
									break;
								}
							}

#if HORIZON_PRUNE
							// check pruning before rendering both occlusion and the object
              if (disablePruning<1 && lod<0)
              {
                Vector3 minmax[2];
  							obj->ClippingInfo(obj->RenderVisualState(),minmax,Object::ClipVisual);
                if (prune.CheckMinmaxBox(obj,minmax,clip))
                {
                  // when casting shadows, we cannot skip the object - we need to mark it as invisible only
                  if (!shadowPossible)
										continue;
                  else
										lod = LOD_INVISIBLE;
                }
              }
#endif
#ifdef GEOM_OCCLUDERS
							bool occluderVisible = false;

							if(DoGeomOccluder)
							{
								Vector3 minmax[2];
								obj->ClippingInfo(obj->RenderVisualState(),minmax,Object::ClipVisual);
								enf::AAB wbbox;
								wbbox.InitFromOBB(obj->RenderVisualState().Transform(), enf::AAB(minmax[0], minmax[1]));
								if(enf::OccluderSystem::CheckAgainstOccluders(wbbox, masks, numOccluders))
								{
									occluderVisible = true;
								}
/*								else
								{
									if (!shadowPossible)
										andClip = ClipAll;
									else
										forceLOD = LOD_INVISIBLE;
								}*/
							}
#endif
							// if needed, render occlusion
							if (shape->CanOcclude() && disablePruning<2 && lod<0)
							{

								// TODO: if we are to occlude, up must be vertical and the object cannot be flying
								const float minCos = 0.99984769516f; // cos 1 deg
								if (obj->RenderVisualState().DirectionUp().CosAngle(VUp)>minCos)
								{
									// Remember the view geometry
									const Shape *view = shape->ViewGeometryLevel();

#ifdef _FILTER_OCCLUDERS
									// ignore too small objects            
									if(_scene.CanDrawOcclusion(obj, obj->RenderVisualState().Position()))
#endif
									{
#if HORIZON_PRUNE
										prune.AddObjectHorizon(obj,view,shape->HorizonEdges());
#endif
									}
								}

#ifdef GEOM_OCCLUDERS
								if(occluderVisible &&	andClipObj != ClipAll && forceLOD != LOD_INVISIBLE)
								{
									const Vector3* minMax = shape->HorizonMinMax();
									
									ClipFlags mayBeClipped = ClipAll;
									if(!cam.IsClipped(obj->RenderVisualState().Transform(), minMax, mayBeClipped))
									{
										enf::WorldOccluder occ;
										occ.Matrix = obj->RenderVisualState().Transform();
										occ.OriginalBounds = occ.LocalBounds = enf::AAB(minMax[0],minMax[1]);
										enf::OccluderSystem::CreateOccluder(cam, &occ);
									}
								}
#endif
							}
							_scene.ObjectForDrawing(obj,lod,clip,shadowPossible);
						} // for (object in group)
					} // for (group)
				} // if (!andClip)
				// skip the rest of grouped objects
				int nGroups = list->NGroups();
				if (nGroups>0)
					index = list->Group(nGroups-1)._end;

				// traverse the rest of the objects
				for ( ;index<list->Size(); index++)
				{
					Object *obj=list->Get(index);
					Assert( obj );
					//prefetch next object, if any
					if((index+1)<list->Size())
						PrefetchT0Off(list->Get(index+1), 0);

          Object::Visible visibleStyle = obj->VisibleStyle();
					if (visibleStyle==Object::ObjInvisible) continue;
					// some object may be static even here
					bool isStatic = obj->Static();
					ClipFlags clip;
					int lod = -1;
					if( isStatic )
					{
						if( andClip ) continue;
						// for static objects lod may be suppressed by pruning
						lod = forceLOD;
						clip = orClip;
					}
					else
						clip = ClipAll;
					if( obj==camInside )
					{
            #if _DEBUG
              Matrix4 trans = _scene.GetCamera()->Transform()*_cameraOnRelativeToCamera;
              Assert(trans.Distance2(obj->RenderVisualState().Transform())<0.3f);
            #endif
					  // this is the only place where pass3 (camSpace) objects originate
						_scene.ObjectForDrawing(
							obj,obj->InsideLOD(GetCameraTypePreferInternal()),clip,PositionRender(_cameraOnRelativeToCamera,true),true
						);
            camInsideVisited = true;
					}
					else
					{
#if HORIZON_PRUNE
						// check pruning before rendering both occlusion and the object
						if (disablePruning<1 && lod<0)
						{
							Vector3 minmax[2];
							obj->ClippingInfo(obj->RenderVisualState(),minmax,Object::ClipVisual);
							if (prune.CheckMinmaxBox(obj,minmax,clip))
							{
								if (!shadowPossible)
                  continue;
								else
                  lod = LOD_INVISIBLE;
							}
						}
#endif
#ifdef GEOM_OCCLUDERS
						if(DoGeomOccluder)
						{
							Vector3 minmax[2];
							obj->ClippingInfo(obj->RenderVisualState(),minmax,Object::ClipVisual);
							enf::AAB wbbox;
							wbbox.InitFromOBB(obj->RenderVisualState().Transform(), enf::AAB(minmax[0], minmax[1]));
							if(!enf::OccluderSystem::CheckAgainstOccluders(wbbox))
							{
								if (!shadowPossible)
									continue;
								else
									lod = LOD_INVISIBLE;
							}
						}
#endif
						// we might render occlusions here
						// however most static objects are already processed
						_scene.ObjectForDrawing(obj,lod,clip,true);
					}
				} // for (object outside the groups)
			} // for (minor grid traversal loop)

			// object line finished
#if HORIZON_PRUNE
#ifdef _SPARSE_HORIZON
			if (disablePruning<2 && (lineI & 1) == 0)
#else
			if (disablePruning<2)
#endif
			{
			  PROFILE_SCOPE_DETAIL_EX(hPrun,drw);
				//Get it once, avoid memory read in innerloop
				float landGrid = LandGrid;
				float terrainGrid = TerrainGrid;
				int lineMin = intMax(minorBegin, 0);
				int lineMax = intMin(minorEnd, objRange);
				int lineTerrainRange = (lineMax - lineMin)<<subdivLog;

				//This really may happen! keep the check here!
				if(lineTerrainRange >= 1)
				{
					lineTerrain.Resize(lineTerrainRange+1);

					if (zMajor)
					{
						int z = major;
						int zAdd = (zStep > 0) ? 1 : 0;
						// once we are behind object rendering distance, it has no sense to render any pruning information
						if (fabs((z+zAdd)*landGrid-pos.Z())<Glob.config.objectsZ)
						{
							//precompute in-loop constant data
							float fXMin = lineMin*landGrid;
							float fZ = (z + zAdd) * landGrid;
							for (int i=0; i<=lineTerrainRange; i++)
							{	
								lineTerrain[i] = prune.Transform(Vector3(fXMin,l->ClippedDataXZ((lineMin<<subdivLog)+i,(z + zAdd)<<subdivLog), fZ));
								fXMin += terrainGrid;
							}
							prune.AddTransformedHorizonLine(lineTerrain.Data(),lineTerrain.Size());
						}
						// draw when next coord is the one we are interested in?
#if _ENABLE_CHEATS
						PruneDiagNowOn = PruneMinZ<=z && PruneMaxZ>z;
#endif
					}
					else
					{
						int x = major;
						int xAdd = (xStep > 0) ? 1 : 0;
						if (fabs((x+xAdd)*landGrid-pos.X())<Glob.config.objectsZ)
						{
							//precompute in-loop constant data
							float fX = (x + xAdd) * landGrid;
							float fZMin = lineMin*landGrid;

							for (int i=0; i<=lineTerrainRange; i++)
							{
								lineTerrain[i] = prune.Transform(Vector3(fX,l->ClippedDataXZ((x + xAdd)<<subdivLog,(lineMin<<subdivLog)+i),fZMin));
								fZMin += terrainGrid;
							}

							prune.AddTransformedHorizonLine(lineTerrain.Data(),lineTerrain.Size());
						}
#if _ENABLE_CHEATS
						PruneDiagNowOn = PruneMinX<=x && PruneMaxX>x;
#endif
					}
				}
				prune.AdvanceBuffer();
			}
#endif
		} // for (major grid traversal loop)
#ifdef TRAP_FOR_0x8000
    //GEngine->TrapFor0x8000("World.PrepareDraw LEAVING major grid traversal loop");
#endif
		
#if _ENABLE_REPORT
    if (!myGridVisited)
    {
      LogF("Camera grid not visited");
    }
#endif
    if (camInside && !camInsideVisited)
    {
      int lod = camInside->InsideLOD(GetCameraTypePreferInternal());
      if (lod!=LOD_INVISIBLE)
      {
        LogF("Camera vehicle rendering patched");
			  _scene.ObjectForDrawing(
				  camInside,lod,ClipAll,PositionRender(_cameraOnRelativeToCamera,true),true
			  );
      }
    }

		// always check all features for rendering, reject them with the view distance used for landscape
		// TODO: consider using OffTree here
		const Array<Landscape::FeatureInfo> &features = l->GetFeatures();
		for (int i=0; i<features.Size(); i++)
		{
      const Landscape::FeatureInfo &feature = features[i];
		  // clip feature - distance and view frustum
		  float dist2 = pos.Distance2(feature._pos);
		  if (dist2>Square(Glob.config.horizontZ+l->GetLandGrid())) continue;
		  // try clipping based on approximate information first?
		  if (!feature._obj.IsReady()) continue;
		  OLinkLPtr(Object) objLock = feature._obj.GetLock();
			
#ifdef GEOM_OCCLUDERS
			if(DoGeomOccluder)
			{
				Vector3 minmax[2];
				objLock->ClippingInfo(objLock->RenderVisualState(),minmax,Object::ClipVisual);
				enf::AAB wbbox;
				wbbox.InitFromOBB(objLock->RenderVisualState().Transform(), enf::AAB(minmax[0], minmax[1]));
				if(!enf::OccluderSystem::CheckAgainstOccluders(wbbox))
				{
					continue;
				}
			}
#endif

			// close enough - render it. If submitting for a 2nd time, the request will be ignored
			// no shadow here - if the objects needs a shadow, it was already submitted
			// let ObjectForDrawing do all the necessary clipping
			_scene.ObjectForDrawing(objLock,-1,ClipAll,false);
		}
		// note: we reject based on object id, which contains x and z?

#if _VBS3_LARGE_OBJECTS
#if _ENABLE_CHEATS
    // true to see the count and possible duplicate entries in GLandscape->_largeStaticObjectInfos
    static bool testDuplos = false;
    if (testDuplos)
    {
      testDuplos = false;
      int siz = GLandscape->_largeStaticObjectInfos.Size();
      LogF("Large static object infos size: %d", siz);
      int duploCOunt = 0;
      for (int i=0; i<siz; i++)
        for (int j=i+1; j<siz; j++)
        {
          if (GLandscape->_largeStaticObjectInfos[i].id==GLandscape->_largeStaticObjectInfos[j].id)
          {
            LogF("  %d. dupla: %d (%d,%d)", ++duploCOunt, GLandscape->_largeStaticObjectInfos[i].id.Encode(),i,j);
          }
        }
    }

    //set false to see the Large Object over 50m disappearing again (for testing purposses)
    static bool drawLargeObjects = true;
    if (drawLargeObjects)
#endif
    {
      // check large objects for rendering, search for those overlapping the boundRect
      Landscape::RTreeStaticObject& largeObjects = GLandscape->LargeStaticObjects();
      float min[2]; min[0] = boundRect.xBeg*ObjGrid; min[1] = boundRect.zBeg*ObjGrid;
      float max[2]; max[0] = boundRect.xEnd*ObjGrid; max[1] = boundRect.zEnd*ObjGrid;
      largeObjects.Search(min, max, [this](int objIx)->bool {
        LargeObjectInfo &objInfo = _scene.GetLandscape()->_largeStaticObjectInfos[objIx];  //TODO: check the range
        if (!objInfo.object.IsReady()) return true; //continue, this one cannot be rendered now
        OLinkLPtr(Object) object = objInfo.object.GetLock();
        if (object.NotNull())
          GWorld->GetScene()->ObjectForDrawing(object,-1,ClipAll,true); //with shadows ???
        return true; //continue searching
      });


      // check large dynamic object for rendering
      GLandscape->LargeDynamicObjects().Search(min, max, [](Object *object)->bool {
        if ( object && !object->Invisible() ) GWorld->GetScene()->ObjectForDrawing(object,-1,ClipAll,true); //with shadows ???
        return true; //continue searching
      });
    }
#endif
	}
	{
    //TrapFor0x8000Scope lock("World::PrepareDraw clPrp");
		PROFILE_SCOPE_GRF(clPrp,SCOPE_COLOR_RED);
		// to keep frame rate smooth remove any cloudlets that are over certain limit
		if (_cloudlets.Size()>CloudletCountLimitHard)
		{
			// first cloudlets are the oldest one - remove them first
			int overLimit = _cloudlets.Size()-CloudletCountLimitHard;
			// cost of removing any number of cloudlets is almost identical
			// therefore we avoid to remove only a few of them, but rather wait
			// until there is a substantial amount to remove
			const int minRemove = 100;
			if (overLimit>minRemove)
				_cloudlets.Delete(0,overLimit);
		}
		// any cloudlets over the limit should disappear ASAP
		if (_cloudlets.Size()>CloudletCountLimitSoft)
		{
			float urgent = float(_cloudlets.Size()-CloudletCountLimitSoft) / CloudletCountLimitHard/CloudletCountLimitSoft;
			for (int i=0; i<_cloudlets.Size()-CloudletCountLimitSoft; i++)
				_cloudlets[i]->DisappearASAP(0.5f-0.5f*urgent);
		}
		// add all cloudlets
		for (int i = 0; i < NCloudlets(); i++)
			// no LOD management possible, no reflections
			_scene.CloudletForDrawing(GetCloudlet(i));
	}

#if 1
  // prepare 3D UI objects
  if (IsUIEnabled())
  {
    if (_playerOn)
    {
      if (_watch)
      {
        if (_playerOn->FindItem(_watch))
        {
          Matrix4 transform(NoInit);
          // TODO: read placement form config
          bool PlaceItemInRectangle(Matrix4 &result, const InventoryItem *item, float x, float y, float w, float h, float z);
          if (PlaceItemInRectangle(transform, _watch, _watchInfo._x, _watchInfo._y, _watchInfo._w, _watchInfo._h, _watchInfo._z))
            _scene.ObjectForDrawing(_watch, 0, ClipAll, PositionRender(transform, true));
        }
        else
        {
          _watch = NULL; // no longer owned by player
        }
      }
      if (_compass)
      {
        if (_playerOn->FindItem(_compass))
        {
          Matrix4 transform(NoInit);
          // TODO: read placement form config
          bool PlaceItemInRectangle(Matrix4 &result, const InventoryItem *item, float x, float y, float w, float h, float z);
          if (PlaceItemInRectangle(transform, _compass, _compassInfo._x, _compassInfo._y, _compassInfo._w, _compassInfo._h, _compassInfo._z))
            _scene.ObjectForDrawing(_compass, 0, ClipAll, PositionRender(transform, true));
        }
        else
        {
          _compass = NULL; // no longer owned by player
        }
      }
    }
  }
#endif
	_scene.EndObjects(); // prepare objects for drawing
}

void RenderDiags()
{
  GDiagsShown.Render();
}


void World::DrawDiags(Object *camInsideVehicle)
{
#if _ENABLE_CHEATS
  if (DiagMode != 0 || DiagMode2!=0)
  {
    // we need to flush the background thread, so that DrawDiags can perform direct rendering
    GEngine->FlushBackgroundScope();

    Vector3Val pos = _scene.GetCamera()->Position();
    float maxObjDist = floatMin(_scene.GetFogMaxRange(), Glob.config.objectsZ);

    GridRectangle boundRect;
    _scene.CalculBoundingRect(boundRect, maxObjDist, ObjGrid);

    int xMin=boundRect.xBeg,xMax=boundRect.xEnd;
    int zMin=boundRect.zBeg,zMax=boundRect.zEnd;
    if( xMin<0 ) xMin=0;if( xMin>ObjRange-1 ) xMin=ObjRange-1;
    if( xMax<0 ) xMax=0;if( xMax>ObjRange-1 ) xMax=ObjRange-1;
    if( zMin<0 ) zMin=0;if( zMin>ObjRange-1 ) zMin=ObjRange-1;
    if( zMax<0 ) zMax=0;if( zMax>ObjRange-1 ) zMax=ObjRange-1;

    for( int z=zMin; z<=zMax; z++ ) for( int x=xMin; x<=xMax; x++ )
    {
      // some lists may be rejected even before loading the list
      // this is easy depending on distance
      // compute XZ (horizontal) distance from the camera
      float distXZ2 = Square(x*LandGrid-pos.X())+Square(z*LandGrid-pos.Z());
      if (distXZ2>Square(maxObjDist+LandGrid))
      {
        // we may quick reject some grids as not necessary
        // we may not quick reject border grid, as them may contain
        // vehicles outside of the landscape
        if (x!=0 && x!=LandRangeMask && z!=0 && z!=LandRangeMask)
          continue;
      }

      // if the list distance is high, we prefer not to wait for it if it is not loaded yet
      Landscape::UseObjectsMode noWait = distXZ2>Square(200) ? Landscape::UONoWait : Landscape::UOWait;
      const ObjectListUsed &list=GLandscape->UseObjects(x,z,noWait);
      if( list.IsNull() ) continue;
      Vector3Val bCenter=list->GetBSphereCenter();
      // we need to consider shadow radius as well, otherwise shadows will disappear very quickly
      float bRadius=list->GetBSphereRadius();
      const Camera &cam=*_scene.GetCamera();
      ClipFlags andClip=cam.IsClipped(bCenter,bRadius);
      if( andClip && list->GetNonStaticCount()<=0 ) continue;
      int n=list.SizeNotEmpty();
      for (int i = 0; i < n; i++)
      {
        Object *obj = list[i];
        if (obj->Static())
        {
          if (andClip)
            continue;
        }
        Object::ProtectedVisualState<const ObjectVisualState> vs = obj->RenderVisualStateScope();
        obj->DrawDiags();
      }
    }

#if _ENABLE_PHYSICS
    if (CHECK_DIAG(DEPhys))
    {
      ;//GlobalShowMessage(100, Format("Bullet memory %u, static object count: %u, shape count: %u", GetPhysicsManager()->GetAllocation(), Object::ObjectCnt(), LODShape::GetPhysShapeCnt()));
    }
#endif

    if (EntityAI *cameraOnAI = dyn_cast<EntityAI>(CameraOn()))
      cameraOnAI->DrawFocusedDiags();
    else if (PlayerOn())
    {
      AIBrain *playerBrain = PlayerOn()->Brain();
      if (playerBrain && playerBrain->GetVehicle())
        playerBrain->GetVehicle()->DrawFocusedDiags();
    }
    _scene.EndObjects(); // prepare objects for drawing
  }
#endif
}

/**
Based on how NVG is implemented we may want fixed gain factor when NVG are on
*/
static inline bool EyeGainFixed(bool wantNV)
{
  //return wantNV;
  return false;
}

#if _VBS2
  #include "hla/VBSVisuals.hpp"
#endif

void World::DrawAmbientTraffic()
{
#if _HELISIM_AMBIENT_TRAFFIC
  Vector3 pl = GScene->GetCamera()->Position();
  float dist = GScene->GetPreferredTrafficViewDistance();
#if _HELISIM_VIEW_DIST
  saturateMin(dist,  GScene->GetPreferredObjectViewDistance());  // no need to generate objects that won't be displayed
#endif
  float xMin = pl.X() - dist, zMin = pl.Z() - dist; // TODO: find out the true displayed rectangle
  float xMax = pl.X() + dist, zMax = pl.Z() + dist;

  GLandscape->GenerateAmbientTraffic(Glob.time.toFloat(), xMin, xMax, zMin, zMax);  // TODO: should probably be based on Glob.clock instead
#endif
}

void World::Draw(Scene::DrawContext &drawContext, const GridRectangle &groundPrimed, Entity *camInsideVehicle,
  bool frameRepeated, bool wantNV, bool wantFlir, float deltaT, const UseWaitingTime &useTime)
{
  if (IsDisplayEnabled())
  {
    float modulate = GEngine->GetHDRFactor();
    GEngine->SetModulateColor(Color(modulate,modulate,modulate,1));

    // Draw the landscape
    _scene.GetLandscape()->Draw(drawContext,groundPrimed,_scene,deltaT,frameRepeated
#if _HELISIM_RENDER_TO_TEXTURE
      , false
#endif
      );

		//heat haze before lights clean
		//GScene->DrawLightHeatHaze();

    if (_ui) _ui->Draw3D();
    
    // Draw rain and pass3 and clear the arrays
    _scene.ObjectsDrawn(drawContext
#if _HELISIM_RENDER_TO_TEXTURE
      , _scene.GetDrawObjectIndex()
#endif
      );

    // Draw diagnostics objects - must be after pass 3, because if will flush the BG thread
    DrawDiags(camInsideVehicle);
    
    // once we are done with rendering, we can reasonable assume no more time will be needed
    // in graphics related tasks
    GTimeManager.Finished(TCCreateVertexBuffer);
    GTimeManager.Finished(TCLoadObjects);
    GTimeManager.Finished(TCLoadShape);
    GTimeManager.Finished(TCPrepareShadow);
    // flush the background thread before calling the useTime callback    
    GEngine->PreparePostprocess();
    
    // use the time available - it is likely the thread work will take some time to finish
    {
      // we may clean-up the object lists now
      _scene.ObjectsCleanUp(drawContext);
      useTime();
    }

    if (wantFlir)
    {
      GEngine->EnableNightEye(0);
    }
    
    PostFxSettings pfx;
    pfx.rotBlur = _camTypeMain == CamInternal || _camTypeMain==CamGunner;
    GEngine->Postprocess(deltaT,pfx);

		// Draw flares and invalidate lights in ObjectsCleanUp
    _blindIntensity = _scene.DrawFlares(deltaT);
		
		_scene.LightsCleanUp();
    
    GEngine->EnableNightEye(0);
    GEngine->SetNightVision(false,false,3.0f,true);

#if _VBS3
    // Draw lines with post processing off
   GVBSVisuals.Draw3DLines(false); 
#endif

    if( _cameraEffect)
      _cameraEffect->Draw();
    if (_camTypeTransition<1)
    {
	    float alpha = 1-fabs(_camTypeTransition-0.5)*2;
    	GEngine->Draw2DWholeScreen(PackedBlack,alpha);
    }
  }
  else
  {
    // this call makes sure GC is called even on DS or when no rendering is done
    FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
    _scene.GetLandscape()->DrawNull(_scene);
    _scene.CleanUp();

    useTime();
    
    GEngine->NoPostprocess();
#if !_VBS3
    GEngine->SetModulateColor(Color(1,1,1,1));
#endif
  }
  if (!IsDisplayEnabled())
    ProgressDraw();
}

float World::CalculateFrameDuration(int &wantedVSyncDuration, float &maxVSyncEndTime, float vsyncTime)
{
  // base all timing on vsync information
  int presentationInterval = GEngine->GetPresentationIntervalVSync();
  // if the current framerate is low, we may improve the prediction
  float predictFrameTime = GEngine->GetAvgFrameDuration(6);;

  wantedVSyncDuration = presentationInterval;

  // check when the frame we are processing now needs to end
  maxVSyncEndTime = GEngine->GetLastFrameEndVSync()+wantedVSyncDuration;
  float currVSyncTime = GEngine->GetTimeVSync();

  // this is the max time we have for this frame if we want to catch wantedVSyncDuration
  // we keep some reserve (5ms) to be sure we do not miss the frame
  float maxCPUTime = (maxVSyncEndTime-currVSyncTime)*vsyncTime-0.005f;

  // TODO: add estimations
  (void)maxCPUTime,(void)predictFrameTime;

  // deltaT is normally based on estimated vsync time
  float deltaT = wantedVSyncDuration*vsyncTime;
  // apply correction based on what error was detected
  float knownError = GEngine->GetKnownErrorVsync()*vsyncTime-_vsyncErrorApplied;
  float errorCorrection = knownError;
  /// make sure time is not going back
  saturateMax(errorCorrection,-deltaT*0.99);
//   #if _ENABLE_CHEATS && _ENABLE_REPORT
//     extern bool FpsCap;
//     if (FpsCap)
//     {
//       if (knownError!=0)
//         LogF("error %g, applied %g, corr %g (deltaT %g->%g)",
//           knownError,_vsyncErrorApplied,errorCorrection,deltaT,deltaT+errorCorrection);
//     }
//   #endif
  // if skipping forward, assume we applied whole correction even when we did not
  _vsyncErrorApplied += errorCorrection;
  /// make sure it is not going too fast as well
  // in multiplayer time axis is more important to be correct
  saturateMin(errorCorrection,_mode != GModeNetware ? 0.2f : 2.0f);
  deltaT += errorCorrection;
  return deltaT;
}

static void FixCameraMatrix(Matrix4 &transform)
{
  if (
    fabs(transform.Direction().SquareSize()-1.0f)>0.1f || 
    fabs(transform.DirectionUp().SquareSize()-1.0f)>0.1f ||
    fabs(transform.DirectionAside().SquareSize()-1.0f)>0.1f
  )
  {
    // if it is real bad, we need to replace with an identity
    if (
      transform.Direction().SquareSize()<0.1f ||
      transform.DirectionUp().SquareSize()<0.1f ||
      transform.DirectionAside().SquareSize()<0.1f
    )
      transform.SetOrientation(M3Identity);
    else
      transform.Orthogonalize();
  }
}

void World::SelectCameraType(float deltaT)
{
  OLinkObject cameraVehicle=_cameraOn;
  OLink(Entity) camVehicle=dyn_cast<Entity,Object>(cameraVehicle);
  OLink(EntityAI) camAI=dyn_cast<EntityAI,Entity>(camVehicle);
  OLink(Person) person = FocusOn() ? FocusOn()->GetPerson() : NULL;
  // for remote controlled vehicles, switching of camera type is disabled

  if(!_cameraEffect && IsSimulationEnabled())
  {
    if (!HasMap() || _map->IsTopmost())
    {
      if (IsShowMapWanted())
      {
        // higher priority to override actions when map is shown
        if (GInput.GetActionToDo(UAHideMap, true, true, false, 2))
          ShowMap(false);
      }
      else
      {
        if (GInput.GetActionToDo(UAShowMap))
        {
          if (_map) _map->ResetHUD();
#if _ENABLE_IDENTITIES
          // When UAShowMap is triggered, the UADiary should be triggered, see news:glpp66$pr4$1@new-server.localdomain
          if (person) person->PlayAction(CAST_TO_ENUM(ManAction, ManActDiary));
#endif
          ShowMap(true);
        }
      }

      
      if (!(GetNetworkManager().GetMissionHeader() && GetNetworkManager().GetMissionHeader()->_forceCameraType) && GInput.GetActionToDo(UAPersonView))
      {
        // switch internal / external view
        if (_cameraExternal)
        {
          if ((_camTypeMain == CamExternal || _camTypeMain == CamGroup) && !IsShowMapWanted())
          {
            _cameraExternal = false;
            if (_camTypeMain == CamExternal && _cameraOn) _cameraOn->CamIntExtSwitched();
            _camTypeMain = CamInternal;
          }
          else
            _camTypeMain = CamExternal;
        }
        else
        {
          if (_camTypeMain == CamInternal && !IsShowMapWanted())
          {
            _cameraExternal = true;
            if (_cameraOn) _cameraOn->CamIntExtSwitched();
            _camTypeMain = CamExternal;
          }
          else
            _camTypeMain = CamInternal;
        }
        ShowMap(false);
      }
      if (!IsShowMapWanted() && GInput.GetActionToDo(UAOptics))
      {
        // toggle optics
        bool forceOptics = camAI && camAI->GetForceOptics(person, _camTypeMain);
        if (!IsShowMapWanted() && (_camTypeMain == CamGunner || forceOptics))
        {
          // switch optics off
          if (_camTypeMain == CamGunner)
          {
#if _VBS3 //Commander override
            bool override = false;
            TurretContext context;
            Transport* transport = dyn_cast<Transport>(camVehicle.GetLink());
            if(person && transport && transport->FindTurret(person, context))
            {
              if(context._turret && context._turret->_overrideTurret)
              {
                if(context._turret->NextOverrideOptics())
                {
                  override = true;
                }
              }
            }
            if(!override)
#endif
            if (_cameraExternal)
            {
              // want CamExternal
              if (camAI && camAI->GetForceOptics(person, CamExternal) && !camAI->GetForceOptics(person, CamInternal))
              {
                // CamExternal disabled
                _camTypeMain = CamInternal;
                _cameraExternal = false;
              }
              else _camTypeMain = CamExternal;
            }
            else
            {
              // want CamInternal
              if (camAI && camAI->GetForceOptics(person, CamInternal) && !camAI->GetForceOptics(person, CamExternal))
              {
                // CamInternal disabled
                _camTypeMain = CamExternal;
                _cameraExternal = true;
              }
              else _camTypeMain = CamInternal;
            }
          }
          else // force optics
          {
            if (_camTypeMain == CamInternal)
            {
              if (!camAI->GetForceOptics(person, CamExternal))
              {
                _camTypeMain = CamExternal;
                _cameraExternal = true;
              }
            }
            else if (_camTypeMain == CamExternal)
            {
              if (!camAI->GetForceOptics(person, CamInternal))
              {
                _camTypeMain = CamInternal;
                _cameraExternal = false;
              }
            }
          }
        }
        else
        {
          // switch optics on
          _camTypeMain = CamGunner;
          // init fov
          AIBrain *brain = FocusOn();
          if (brain && !brain->GetVehicleIn())
          { // not inside vehicle
            Person *person = PlayerOn();
            float dummy;
            if (person) 
            {
              if (!person->GetOpticsModel(person))
              {
                Object::Zoom *zoom = person->GetZoom();
                person->InitVirtual(CamGunner, dummy, dummy, zoom->_camFOVNoCont);
              }
            }
          }
        }
      }

      if (GInput.GetActionToDo(UATacticalView))
      {
        if (_camTypeMain == CamGroup && !IsShowMapWanted())
        {
          if (_cameraExternal)
            _camTypeMain = CamExternal;
          else
            _camTypeMain = CamInternal;
        }
        ShowMap(false);
      }
    }
  }
  /*
  else
  {
  _showMap=false;
  }
  */

  if (_cameraEffect)
    ShowMap(false);

  bool enableExternal = Glob.config.IsEnabled(DT3rdPersonView);
  if (!enableExternal)
  {
    _cameraExternal = false;
    if (_camTypeMain == CamExternal)
      _camTypeMain = CamInternal;
  }

  if( FocusOn() )
  {
  #if _ENABLE_CHEATS
    static bool enableAnyCamera=false;
    if( GInput.GetCheat2ToDo(DIK_P) )
      enableAnyCamera = !enableAnyCamera;
    if( enableAnyCamera )
    {
      _camTypeMain=CamGroup;
      goto CameraOK;
    }
  #endif
    if (_camTypeMain == CamGroup)
    {
      if (_cameraExternal)
        _camTypeMain = CamExternal;
      else
        _camTypeMain = CamInternal;
    }
    // _camTypeMain != CamGroup

  CameraOK:

    // select _camType (restriction of _camTypeMain)

    bool enableOptics = false;
    bool forceOptics = false;

    if (camAI)
    {
      LODShape *optics = camAI->GetOpticsModel(person);
      if (optics && optics->NLevels()>0)
      {
        // make sure optics model for the weapon is always ready to be used including the muzzle flash
        if (optics->CheckLevelLoaded(0,true))
        {
          // preload all textures needed
          ShapeUsed shape = optics->Level(0);
          shape->PreloadTextures(0, NULL, true);
        }
      }
      if (!_cameraEffect && !camAI->DisableWeapons())
      {
        if (person)
        {
          enableOptics = optics != NULL || camAI->GetEnableOptics(camAI->RenderVisualState(), person);
          forceOptics = camAI->GetForceOptics(person, _camTypeMain);
        }
      }
    }

    // based on _camTypeMain we select current camera
    // this may include transition states
    
    CameraType camTypeNewWanted = _camTypeMain;
    if (camTypeNewWanted == CamGunner)
    {
      if (enableOptics) {}
      else if (_cameraExternal)
        camTypeNewWanted = CamExternal;
      else
        camTypeNewWanted = CamInternal;
    }
    
    if ((camTypeNewWanted == CamInternal || camTypeNewWanted == CamExternal) && enableOptics && forceOptics)
      camTypeNewWanted = CamGunner;
    
    // On Xbox Group camera is on whenever camera is external and commanding menu is on
    if (_ui->IsXBOXStyle())
    {
      if (camTypeNewWanted == CamGroup)
        camTypeNewWanted = CamExternal;
    }

    if (person)
    {
      bool zoomInsteadOfOptics = (
        _camTypeMain==CamGunner && (camTypeNewWanted==CamInternal || camTypeNewWanted==CamExternal)
        );
      Object::Zoom *zoom = person->GetZoom();
      if (zoom) zoom->_zoomInsteadOfOptics=zoomInsteadOfOptics;
    }
    
    // make transition as necessary
    if (_camTypeNew==camTypeNewWanted)
    {
      // if both camera types are same, there is nothing to do
      if (_camTypeOld!=_camTypeNew)
      {
        if (!camAI->HasOpticsTransitionFx(person,camTypeNewWanted,_camTypeOld))
        {
          // check camera transition in progress
          if (!camAI->HasCameraTransitionFx(person,camTypeNewWanted,_camTypeOld))
          {
            _camTypeNewFactor = 1;
            _camTypeOld = _camTypeNew = camTypeNewWanted;
          }
          else
          {
            // process camera transition
            _camTypeTransition += deltaT*camAI->CameraTransitionFxSpeed(person,camTypeNewWanted,_camTypeOld);
            _camTypeNewFactor = _camTypeTransition>=0.5f;
            if (_camTypeTransition>=1)
            {
              _camTypeTransition = 1;
              _camTypeOld = _camTypeNew;
            }
            else if (_camTypeNewFactor==0)
            {
              // preload what will be necessary for the new camera
              camAI->PreloadView(person,_camTypeNew);
            }
          }
        }
        else
        {
          // transition on the way - process it
          _camTypeNewFactor += deltaT*camAI->CameraTransitionFxSpeed(person,camTypeNewWanted,_camTypeOld);
          _camTypeTransition = 1;
          camAI->OnCameraTransition(person,camTypeNewWanted,_camTypeOld,_camTypeNewFactor);
          if (_camTypeNewFactor>=1)
          {
            _camTypeNewFactor=1;
            _camTypeOld = _camTypeNew;
          }
          else
          {
            // during transition preload the new view
            camAI->PreloadView(person,_camTypeNew);
          }
        }
      }
    }
    else if (_camTypeOld==camTypeNewWanted)
    {
      if (!camAI->HasOpticsTransitionFx(person,_camTypeOld,_camTypeNew))
      {
        if (!camAI->HasCameraTransitionFx(person,_camTypeOld,_camTypeNew))
        {
          _camTypeNewFactor = 1;
          _camTypeOld = _camTypeNew = camTypeNewWanted;
        }
        else
        {
          // reverse camera transition
          _camTypeTransition = 1-_camTypeTransition;
          _camTypeOld = _camTypeNew;
          _camTypeNew = camTypeNewWanted;
          _camTypeNewFactor = _camTypeTransition>=0.5f;
        }
      }
      else
      {
        // reverse transition in progress - revert it
        _camTypeOld = _camTypeNew;
        _camTypeNew = camTypeNewWanted;
        _camTypeNewFactor = 1-_camTypeNewFactor;
      }
    }
    else
    {
      if (!camAI->HasOpticsTransitionFx(person,camTypeNewWanted,_camTypeOld))
      {
        if (!camAI->HasCameraTransitionFx(person,_camTypeOld,_camTypeNew))
        {
          _camTypeNewFactor = 1;
          _camTypeOld = _camTypeNew = camTypeNewWanted;
        }
        else
        {
          // start a new camera transition
          _camTypeTransition = 0;
          _camTypeNewFactor = 0;
          _camTypeOld = _camTypeNew;
          _camTypeNew = camTypeNewWanted;
        }
      }
      else
      {
        // start a new optics transition
        _camTypeNew = camTypeNewWanted;
        _camTypeNewFactor = 0;
      }
    }
  }
  else
  {
    // no AI focus
    // ?? other camera types ??
    if (_cameraExternal)
      _camTypeMain = CamExternal;
    else
      _camTypeMain = CamInternal;
    SetCameraType(_camTypeMain);
  }
}

/**
@return intensity of the camera water effects
*/
void World::SimulateCamera(float deltaT)
{
  // simulate camera shake
  _cameraShakeManager.Simulate(deltaT);

  OLinkObject cameraVehicle=_cameraOn;
  //OLink(Entity) camVehicle=dyn_cast<Entity,Object>(cameraVehicle);

  if (_cameraEffect)
    _cameraEffect->Simulate(deltaT);
  else if (cameraVehicle)
  {
    if (!HasOptions() && !_showMap && !_showMapProgress)
    {
      CameraType camTypeAct = GetCameraType();
      cameraVehicle->SimulateHUD(camTypeAct, deltaT);
      
      if (cameraVehicle->IsVirtual(camTypeAct))
      {
        // mouse cursor is free to move in neutral zone
        if (GInput.GetActionToDo(UALookCenter, false))
        {
          _camMaxDist[camTypeAct] = 1e10;
          if (_ui)
          {
            // cursor must be set to the screen center
            Camera &camera = *_scene.GetCamera();
            _ui->SetCursorDirection(camera.Direction());
          }
        }
      }
    }
  }
}
/**
@return intensity of the camera water effects
*/
float World::SelectCameraPosition(Engine *engine, float deltaT, float aspectOverride)
{
  float inWater = 0.0f;
  OLinkObject cameraVehicle = _cameraOn;
  //OLink(Entity) camVehicle=dyn_cast<Entity,Object>(cameraVehicle);

  Camera &camera = *_scene.GetCamera();
  float fov = 0.7f;
  Matrix4 transform = camera.Transform();
  Matrix4 base = MIdentity;


  if (_cameraEffect)
  {
    Object *object = _cameraEffect->GetObject();
    fov = _cameraEffect->GetFOV();
    if (fov < 0.0f)
    {
      if (object)
      {
        Object::ProtectedVisualState<const ObjectVisualState> vs = object->RenderVisualStateScope();
        fov = object->CamEffectFOV();
      }
      else
      {
        // default fov
        fov = 0.7f;
      }
    }
    transform=_cameraEffect->GetTransform();
  }
  else if (cameraVehicle)
  {
    Object::ProtectedVisualState<const ObjectVisualState> vs = cameraVehicle->RenderVisualStateScope();
    
    fov = cameraVehicle->GetCameraFOV();
    transform = vs->Transform();

    // no camera effect - use normal camera
    CalculateCameraTransform(base, transform, deltaT);
    
    // base should not need any fixing
    FixCameraMatrix(transform);

    _camaraBase = base;
    _camaraTransform = transform;
    // base is often the cameraVehicle transform
    if (base.Distance2(vs->Transform()) < 1e-8f)
    {
      // store the accurate position
      _cameraOnRelativeToCamera = transform.InverseGeneral();
      transform = base * transform;
    }
    else
    {
      transform = base * transform;
      _cameraOnRelativeToCamera = transform.InverseGeneral() * vs->Transform();
    }
  }
  // avoid camera getting under the ground or sea
  {
    Vector3 camPos = transform.Position();
   
  

    //float minCamY = _scene.GetLandscape()->SurfaceYAboveWater(camPos.X(),camPos.Z());
    float minCamY = _scene.GetLandscape()->SurfaceY(camPos.X(), camPos.Z());
    // check water-road surface (pond) as well
    bool changed = false;
#if 0
    float waterY = _scene.GetLandscape()->WaterSurfaceY(camPos);
    if (minCamY<waterY)
    {
      minCamY = waterY;
      changed = true;
    }
#endif
    minCamY += 0.1f;
    if (camPos.Y() < minCamY)
    {
      camPos[1] = minCamY;
      changed = true;
    }
    transform.SetPosition(camPos);
    
    if (changed && cameraVehicle)
    {
      _cameraOnRelativeToCamera = transform.InverseGeneral() * cameraVehicle->RenderVisualState().Transform();
    }
  }
  // make sure camera transform is not invalid
  // it should be always orthonormal - zooming is done in the projection
  FixCameraMatrix(transform);

  // check if camera change is significant enough to be considered a "cut" (abrupt change)
  if (
    transform.Position().Distance2(camera.Position())>100 ||
    transform.Orientation().Distance2(camera.Orientation())>0.1f
  )
  { // note: position changes are already mostly handled by Scene::OnCameraChanged
    _scene.OnCameraCut();
    DoAssert(cameraVehicle==_cameraOn);
  }
  camera.SetTransform(transform); 
  camera.SetSpeed(cameraVehicle ? cameraVehicle->ObjectSpeed() : VZero);
  camera.SetAge(cameraVehicle ? cameraVehicle->GetRenderVisualStateAge() : 0);
  
  // when camera was changed - FOV may change
  // near, far, left, top
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  // when under water, we need to adjust fog max range

#if _HELISIM_RENDER_TO_TEXTURE
  if (aspectOverride > 0)
  {
    as.leftFOV = aspectOverride;
    as.topFOV = 1.0f;
  }
#endif

  float underWater = _scene.GetLandscape()->WaterSurfaceY(camera.Position()) - camera.Position().Y();
  // see also Scene::RecalculateLighting
  const float noWaterFog = -0.3f;
  const float fullWaterFog = 0.1f;
  const float waterFogDistance = 20.0f;
  float fogDistance = InterpolativC(underWater, noWaterFog, fullWaterFog, _scene.GetFogMaxRange(), waterFogDistance);

  const float noWaterBlur = -0.4f;
  const float fullWaterBlur = 0.0f;
  if (underWater > noWaterBlur)
    inWater = InterpolativC(underWater,noWaterBlur,fullWaterBlur,0,1);

  const float skyZ = 1250; // see also 12000  in Landscape::DrawSky
  float oldTop = camera.Top();
  camera.SetPerspective(0.07f, floatMax(skyZ,_scene.GetFogMaxRange()), fov * as.leftFOV, fov * as.topFOV, Glob.config.GetShadowZ(), Glob.config.GetProjShadowsZ(), fogDistance);
  //GlobalShowMessage(100,"cNear %g",cNear);
  camera.Adjust(engine);
  if (fabs(oldTop-camera.Top())/camera.Top()>0.1f)
  {
    _scene.OnCameraCut();
    DoAssert(cameraVehicle==_cameraOn);
  }


  return inWater;
}

void World::SimulateDisplays(float deltaT)
{
  if (_showMapUntil != 0)
  {
    bool done = _preloadAsyncCtx ? PreloadAroundCameraAsync(*_preloadAsyncCtx) : PreloadAroundCamera(0.045f);
    {
      if ( done || GlobalTickCount() >= _showMapUntil)
      {
        _showMap = false;
        _showMapUntil = 0;
        SetShowMapProgress(NULL);

        // call event handler after preload screen finished
        if (_preloadAsyncCtx && _preloadAsyncCtx->_callHandler && !GOnPreloadFinished.GetNil())
        {
          GameState *state = GetGameState();
          if (state)
          {
            GameVarSpace vars(false);
            int gshandle = state->BeginContext(&vars);
#if USE_PRECOMPILATION
            if (GOnPreloadFinished.GetType() == GameCode)
            {
              GameDataCode *code = static_cast<GameDataCode *>(GOnPreloadFinished.GetData());
              if (code->IsCompiled() && code->GetCode().Size() > 0)
                state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
            }
            else
#endif
              if (GOnPreloadFinished.GetType() == GameString)
              {
                // make sure string is not destructed while being evaluated
                RString code = GOnPreloadFinished;
                if (code.GetLength() > 0)
                  state->EvaluateMultiple(code, GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
              }
              state->EndContext(gshandle);
          }
        }

        _preloadAsyncCtx.Free();
      }
    }
  }

  if (_loadingScreen)
    return;

  PROFILE_SCOPE(wDisp);

  bool cheats = false;
#if _ENABLE_CHEATS
  if (GInput.cheat1 || GInput.cheat2) cheats = true;
#endif

#if _ENABLE_PERFLOG
  if (GPerfProfilers.IsCaptureReady())
  {
    void CreateCaptureDialog();
    CreateCaptureDialog();
    GPerfProfilers.ResetCaptureReady();
  }
#endif

  if (!cheats)
  {
    DWORD sysTime = ::GetTickCount();

    // Keyboard - offer key events to displays
  for (int k=0; k<256; k++)
  {
    if (GInput.keysToDo[k])
    {
      if (DoKeyDown(k))
      {
        // different handling of modifiers 
        GInput.keysToDo[k] = false;
        switch (k)
        {
        case DIK_LSHIFT:
        case DIK_RSHIFT:
        case DIK_LCONTROL:
        case DIK_RCONTROL:
        case DIK_LMENU:
        case DIK_RMENU:
          break;
        default:
          GInput.keys[k] = 0;
          GInput.keysProcessed[k] = true;
          break;
        }
      }
    }
    else if (GInput.keyPressed[k])
    {
      DWORD nextTime = GInput.keyAutorepeat[k] ?
        GInput.keyAutorepeat[k] + GInput.delayAutorepeat :
        GInput.keyPressed[k] + GInput.delayAutorepeatFirst;
      if (sysTime >= nextTime)
      {
        if (DoKeyDown(k))
        {
          switch (k)
          {
          case DIK_LSHIFT:
          case DIK_RSHIFT:
          case DIK_LCONTROL:
          case DIK_RCONTROL:
          case DIK_LMENU:
          case DIK_RMENU:
            break;
          default:
            GInput.keys[k] = 0;
            GInput.keysProcessed[k] = true;
            break;
          }
        }
        GInput.keyAutorepeat[k] = nextTime;
      }
    }
    if (GInput.keysUp[k] && DoKeyUp(k))
      GInput.keysUp[k] = false;
  }

    // Xbox controller - offer button events to displays
    if (GInput.IsXInputPresent())
    {
      // buttons autorepeat in UI
      static struct
      {
        bool autorepeat;
        float delay;
        float rate;
      } uiAutorepeat[N_JOYSTICK_BUTTONS] =
      {
        {false, 0, 0},  // XBOX_A
        {false, 0, 0},  // XBOX_B
        {false, 0, 0},  // XBOX_X
        {false, 0, 0},  // XBOX_Y
        {true, 0.333, 0.085}, // XBOX_Up
        {true, 0.333, 0.085}, // XBOX_Down
        {true, 0.333, 0.085}, // XBOX_Left
        {true, 0.333, 0.085}, // XBOX_Right
        {false, 0, 0},  // XBOX_Start
        {false, 0, 0},  // XBOX_Back
        {false, 0, 0},  // XBOX_LeftBumper
        {false, 0, 0},  // XBOX_RightBumper
        {false, 0, 0},  // XBOX_LeftTrigger
        {false, 0, 0},  // XBOX_RightTrigger
        {false, 0, 0},  // XBOX_LeftThumb
        {false, 0, 0},  // XBOX_RightThumb
        {true, 0.333, 0.085}, // XBOX_LeftThumbXRight
        {true, 0.333, 0.085}, // XBOX_LeftThumbYUp
        {true, 0.333, 0.085}, // XBOX_RightThumbXRight
        {true, 0.333, 0.085}, // XBOX_RightThumbYUp
        {true, 0.333, 0.085}, // XBOX_LeftThumbXLeft
        {true, 0.333, 0.085}, // XBOX_LeftThumbYDown
        {true, 0.333, 0.085}, // XBOX_RightThumbXLeft
        {true, 0.333, 0.085}, // XBOX_RightThumbYDown
        {false, 0, 0},
        {false, 0, 0},
        {false, 0, 0},
        {false, 0, 0},
        {false, 0, 0},
        {false, 0, 0},
        {false, 0, 0},
        {false, 0, 0}
      };

      // direction buttons - react only when single button is pressed
      const static int dirButtons[] =
      {
        XBOX_Up, XBOX_Down, XBOX_Left, XBOX_Right,
        XBOX_LeftThumbXRight, XBOX_LeftThumbYUp, XBOX_LeftThumbXLeft, XBOX_LeftThumbYDown
      };
      const int dirButtonsCount = lenof(dirButtons);
      // how many dir buttons are pressed
      int countPressed = 0;
      for (int ib=0; ib<dirButtonsCount; ib++)
      {
        int b = dirButtons[ib];
        if (GInput.xInputButtonsPressed[b]) countPressed++;
      }

      // handle each button
      for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
      {
        if (i == XBOX_NoController || i == XBOX_NewController) continue;

        // check if pressed, including autorepeat
        bool pressed = false;
        if (uiAutorepeat[i].autorepeat)
          pressed = GInput.GetXInputButtonAutorepeat(i, uiAutorepeat[i].delay, uiAutorepeat[i].rate, false, false);
        else
          pressed = GInput.xInputButtonsToDo[i];

        // if more than one dir button is pressed, ignore dir buttons
        if (pressed && countPressed > 1)
        {
          for (int ib=0; ib<dirButtonsCount; ib++)
          {
            if (i == dirButtons[ib])
              pressed = false;
          }
        }

        // handle pressed edge
        if (pressed)
        {
          bool ok = DoKeyDown(INPUT_DEVICE_XINPUT + i);
          if (ok)
            GInput.xInputButtonsToDo[i] = false;
        }

        // handle released edge
        if (GInput.xInputButtonsUp[i] && DoKeyUp(INPUT_DEVICE_XINPUT + i))
          GInput.xInputButtonsUp[i] = false;
      }
    }
  }

  // reported user file errors
  ProcessUserFileErrors();

  // multiplayer chat control 
  if (GetNetworkManager().GetClientState() >= NCSConnected)
  {
    if (
      IsPlayerDead() ||
      GetNetworkManager().GetClientState() < NCSMissionReceived || GetNetworkManager().GetClientState()>NCSGameFinished
    )
    {
      // when channel is not global or is global, but we are not an admin, we may need to switch to another channel
      if (ActualChatChannel()!=CCGlobal || !GetNetworkManager().IsGameMaster() && !GetNetworkManager().IsServer())
      {
        ::ChatChannel ccLobby = CCGlobal;
        if (ActualChatChannel() != ccLobby)
        {
          SetChatChannel(ccLobby);
          if (_channel)
            _channel->ResetHUD();
          if (_voiceChat)
            _voiceChat->ResetHUD();
        }
      }
    }
    if (GInput.GetActionToDo(UAPrevChannel, true, false))
    {
      PrevChatChannel();
      if (_channel)
        _channel->ResetHUD();
      if (_voiceChat)
        _voiceChat->ResetHUD();
      OnChannelChanged();
    }

    if (GInput.GetActionToDo(UANextChannel, true, false))
    {
      NextChatChannel();
      if (_channel)
        _channel->ResetHUD();
      if (_voiceChat)
        _voiceChat->ResetHUD();
      OnChannelChanged();
    }

    if (GInput.GetActionToDo(UAChat, true, false))
      CreateChat();

    if ((!_voiceChat || !GInput.VoNToggleOn) && GInput.GetActionToDo(UAVoiceOverNet, true, false))
    {
      if (!_voiceChat)
      {
        CreateVoiceChat();
        GetNetworkManager().SetVoiceON();
      }
      GInput.VoNToggleOn = true;
      GetNetworkManager().SetVoiceToggleOn(true);
    }
    else if (!_voiceChat && !GInput.VoNToggleOn)
    {
      if (GInput.GetAction(UAPushToTalk))
      {
        BackupChatChannel();
        CreateVoiceChat();
        GetNetworkManager().SetVoiceON();
      }
      else 
      {
        ::ChatChannel channel = CCN;
        if (GInput.GetAction(UAPushToTalkAll)) channel = CCGlobal;
        else if (GInput.GetAction(UAPushToTalkVehicle)) channel = CCVehicle;
        else if (GInput.GetAction(UAPushToTalkDirect)) channel = CCDirect;
        if (channel!=CCN)
        {
          BackupChatChannel();
          SetChatChannel(channel);
          if (_channel) _channel->ResetHUD();
          if (_voiceChat) _voiceChat->ResetHUD();
          OnChannelChanged();
          CreateVoiceChat();
          GetNetworkManager().SetVoiceON();
        }
      }
    }
  }
  else
  {
    _channel.Free();
    _chat.Free();
    _voiceChat.Free();
    GInput.VoNToggleOn = false;
    GetNetworkManager().SetVoiceToggleOn(false);
  }


  if (_chat || _voiceChat || _channelChanged >= Glob.uiTime - 3.0f)
  {
    AbstractOptionsUI *CreateChannelUI();
    if (!_channel)
      _channel = CreateChannelUI();
  }
  else
  {
    if (_channel)
      _channel = NULL;
  }

#ifdef _WIN32
  if (_jUI)
  {
    _jUI->Simulate(deltaT);
  }
#endif

  if (_warningMessage)
  {
    //@{ _DEDICATED_CLIENT
    if ( IsDedicatedClient() )
    {
    _warningMessage = NULL; //ignore all warnings on DedicatedClient (as it is written in console window and nobody can close it)
    }
    else
    {
    _warningMessage->OnSimulate(NULL);
    if (_warningMessage->GetExitCode() >= 0) _warningMessage = NULL;
  }
    //@}
  }
#if _USE_BATTL_EYE_SERVER
    if (_beLicenceMsg)
    { 
      _beLicenceMsg->OnSimulate(NULL);
      int exit = _beLicenceMsg->GetExitCode();
      if (exit >= 0)
      {
#if !_BINARIZE
        // try to reconnect if there is address available
        if (exit == IDC_OK)
        {
          RString guid = _beLicenceMsg->GetReconnectGUID();
          int index = guid.Find(':');
          if (index > 0)
          {
            RString ip = guid.Substring(0, index);
            int port = atoi(guid.Substring(index + 1, INT_MAX));
            void RequestConnect(RString ip, int port, RString password, bool useMPDisplay = false);
            RequestConnect(ip, port, _beLicenceMsg->GetReconnectPassword());
          }
        }
#endif
        _beLicenceMsg = nullptr;
      }
    }
    else
#endif
  {
    if (_voiceChat)
      _voiceChat->SimulateHUD(NULL);
    if (_chat)
      _chat->SimulateHUD(NULL);
    for (int i=0; i<_displays.Size(); i++)
    {
      AbstractOptionsUI *display = _displays[i]._display;
      if (display)
      {
        display->SimulateHUD(NULL);
        return;
      }
    }


    
#if _VBS3
    if (!HasOptions())
#else
    if (!HasOptions() && _cameraOn != NULL)
#endif
    {
      if (_userDlg)
        _userDlg->SimulateHUD(NULL);
      else if( _map && _showMap && IsShowMapWanted())
      {
        OLink(EntityAI) camAI = dyn_cast<EntityAI,Object>(_cameraOn);
#if _VBS3
        if(camAI)
#endif
        _map->SimulateHUD(camAI);
      }
    }
    if (_options)
      _options->SimulateHUD(NULL);
    if (_resetOptions)
    {
      _resetOptions = false;
      _options = CreateMainOptionsUI();
    }
  }
  if (_showMapProgress)
    _showMapProgress->SetProgress(GlobalTickCount(), 0, 0);
}

bool World::HasPlayerGPS() const
{
  AIBrain *player = FocusOn();
  if (!player)
    return false;

  // check if device is available in the vehicle
  Transport *veh = player->GetVehicleIn();
  if (veh && !player->IsInCargo() && veh->GetEnableGPS())
    return true;
  
  // TODO: GPS inventory item is using

  return false;
}

bool World::HasPlayerMap() const
{
  return _mapItem != nullptr;
}

bool World::HasUnitRadio(AIBrain *unit) const
{
  // check if device is available in the vehicle
  Transport *veh = unit->GetVehicleIn();
  if (veh && !unit->IsInCargo() && veh->GetEnableRadio())
    return true;

  // TODO: radio inventory item is using

  return false;
}

void World::SimulateUI(float deltaT)
{
  OLinkObject cameraVehicle=_cameraOn;
  OLink(Entity) camVehicle=dyn_cast<Entity,Object>(cameraVehicle);
  OLink(EntityAI) camAI=dyn_cast<EntityAI,Entity>(camVehicle);

  // check the ownership of map item
  if (_mapItem)
  {
    if (!(_playerOn && _playerOn->FindItem(_mapItem)))
    {
      ShowMap(false);
    }
  }

  if (!_showMap && !_cameraEffect && !_showMapProgress)
  {
    DisplayMap *map = static_cast<DisplayMap *>((AbstractOptionsUI *)_map);
    // GPS / minimap
    if (
      map && map->IsShownGPS() && // the GPS is enabled by the mission and scripting
      GetCameraType()!=CamGunner && // avoid in optics
      HasPlayerGPS()) // player or his vehicle has the watch available
    {
      _showMiniMap = GInput.GetAction(UAMiniMap) > 0;
      if (GInput.GetActionToDo(UAMiniMapToggle))
        _showMiniMapToggle = !_showMiniMapToggle;
      if (_showMiniMapToggle)
        _showMiniMap = true;
    }
    else
      _showMiniMap = _showMiniMapToggle = false;

    if(Glob.config.IsEnabled(DTExtendetInfoType) && GInput.GetActionToDo(UALiteUnitInfoToggle))
    {
      Glob.config.SetUseExtendetInfoType(!Glob.config.UseExtendetInfoType());
      if(UI()) UI()->ResetUnitInfo(); 
    }
  }

  // condition equal to drawing
  if (IsUIEnabled())
  { 
    if( _titleEffect && _titleEffect->IsTerminated() )
    {
      _titleEffect.Free();
      Log("_titleEffect.Free()");
    }
    if( _titleEffect )
      _titleEffect->Simulate(deltaT);
  }
  //if (IsDisplayEnabled()) - changed to allow in map
  if (IsUIEnabled() || IsDisplayEnabled())
  {
    for (int i=0; i<_cutEffects.Size(); i++)
    {
      TitleEffect *effect = _cutEffects[i];
      if (!effect)
        continue;
      if (effect->IsTerminated())
      {
        _cutEffects[i] = NULL;
        continue;
      }
      effect->Simulate(deltaT);
    }
  }

  if (!_warningMessage)
  {
    if( !HasOptions() && _cameraOn!=NULL && !_userDlg)
    {
      if (_ui && IsUIEnabled() && _mode != GModeIntro)
      {
        if (camAI)
          _ui->SimulateHUD(*_scene.GetCamera(),GetCameraType(),deltaT);
        else if (camVehicle)
          _ui->SimulateHUDNonAI(*_scene.GetCamera(),camVehicle,GetCameraType(),deltaT);
      }
    }
  }
  //GInput.cursorMovedZ = 0;

}

void World::CheckChatChannel()
{
  if (!ChannelValid())
  {
    NextChatChannel();
    if (_channel)
      _channel->ResetHUD();
    if (_voiceChat)
      _voiceChat->ResetHUD();
    OnChannelChanged();
  }
}

#if _HELISIM_RENDER_TO_TEXTURE

bool World::IsR2TSrcCamUsed(const RString &name) const
{
  return _r2tSourceCamera.FindKey(name) != -1;
}

void World::AddR2TSourceCamera(CameraEffect *effect, Object *camObject, RString name)
{
  if (effect && name.GetLength() > 0)
  {
    if (!IsR2TSrcCamUsed(name))
    {
      R2TCamera &r2tCam = _r2tSourceCamera.Append();
      r2tCam.SetDefault(effect);
      r2tCam._custom = true;
      
      R2TTgtDescription &tgtDesc = effect->GetTgtDesc();
      tgtDesc._name = name;
      R2TCameraDescription &camDesc = tgtDesc._camDesc.Append();
      camDesc.SetToDefault();
      tgtDesc._camDesc.Compact();

      Ref<R2TTgtPars> _tgtPars = new R2TTgtPars(tgtDesc);
      _tgtPars->Reset();
      r2tCam._tgtPars = _tgtPars;
    }
#if _ENABLE_REPORT
    else
    {
      LogF("R2T render texture %s is already used", cc_cast(name));
    }
#endif
  }
}

R2TCamera::~R2TCamera()
{
  _camEffect.Free();
  _tgtPars.Free();
}

LSError R2TCamera::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("camEffect", _camEffect, 1));

  int idAsInt = _id;
  CHECK(ar.Serialize("rtid", idAsInt, 1, R2TUninitialized));
  if (ar.IsLoading()) 
    _id = (idAsInt >= 0 && idAsInt < R2TRenderTargetCount) ? (RenderTargetID) idAsInt : R2TUninitialized;

  CHECK(ar.Serialize("deltaT", _deltaT, 1, 0.0f));
  CHECK(ar.Serialize("eyeAccom", _eyeAccom, 1, 1.0f));
  CHECK(ar.Serialize("eyeAccomCut", _eyeAccomCut, 1, false));
  CHECK(ar.Serialize("customEyeAccom", _customEyeAccom, 1, 0.0f));
  CHECK(ar.Serialize("allowCockpitDrawing", _allowCockpitDrawing, 1, false));
  CHECK(ar.Serialize("custom", _custom, 1, false));

  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
  {
    _tgtPars = new R2TTgtPars(_camEffect->GetTgtDesc());
    _tgtPars->Reset();
  }

  return LSOK;
}

R2TTransportTgt::~R2TTransportTgt()
{
  GWorld->RemoveR2TSourceCamera(_targetName);
}

void World::RemoveR2TVehicleCameras()
{
  _r2tTransportTgts.Resize(0);
}

struct DeleteR2TCamFunc
{
  __forceinline bool operator()(const R2TCamera &cam) const
  {
    return cam._custom;
  }
};

void World::RemoveR2TCustomCameras()
{
  DeleteR2TCamFunc func;
  _r2tSourceCamera.ForEachWithDelete(func);
}

void World::RemoveR2TSourceCameras()
{
  _r2tTransportTgts.Clear();

  for (int i = 0; i < _r2tSourceCamera.Size(); i++)
  {
    R2TCamera &srcCam = _r2tSourceCamera[i];
    GEngine->UnBindR2T(srcCam._id);
  }

  _r2tLastTransport = NULL;
  _r2tSourceCamera.Clear();
}

void World::RemoveR2TSourceCamera(RString name)
{
  if (name.IsEmpty() || !_r2tSourceCamera.Size()) return;

  int index = _r2tSourceCamera.FindKey(name);
  
  if (index != -1)
  {
    R2TCamera &r2tCam = _r2tSourceCamera[index];
    GEngine->UnBindR2T(r2tCam._id);

    _r2tSourceCamera.DeleteAt(index);
  }
  else
  {
    LogF("Removed render target camera (%s) not found", cc_cast(name));
  }
}

void World::GetEyeAccomPars(float &eyeAccom, bool &eyeAccomCut, float &customEyeAccom) const
{
  eyeAccom = _eyeAccom;
  eyeAccomCut = _eyeAccomCut;
  customEyeAccom = _customEyeAccom;
}

void World::SetEyeAccomPars(float eyeAccom, bool eyeAccomCut, float customEyeAccom)
{
  _eyeAccom = eyeAccom;
  _eyeAccomCut = eyeAccomCut;
  _customEyeAccom = customEyeAccom;
}

struct R2TDrawingState
{
  World &_world;
  Scene &_scene;

  int _shadows;
  float _complexity;
  int _drawObjectId;
  int _drawObjId;
  bool _simulWeatherEnabled;

  float _customEyeAccom;
  bool _eyeAccomCut;
  float _eyeAccom;

  Ref<CameraEffect> _cEffect;
  const R2TCamera &_r2tCam;

  float _tacticalZ;

  R2TDrawingState(World &world, Scene &scene, const R2TCamera &r2tCam)
    : _world(world), _scene(scene), _r2tCam(r2tCam)
  {
    _tacticalZ = Glob.config.tacticalZ;
    _shadows = _scene.GetShadowQuality();
    _complexity = _scene.GetComplexityTarget();
    _drawObjectId = _scene.GetDrawObjectIndex();
#if _ARMA3_SIMUL_WEATHER
    _simulWeatherEnabled = GLandscape->IsSimulWeatherEnabled();
#endif

    // _drawObjectId = 0 -> main scene
    Assert(_drawObjectId == 0);

    R2TCameraDescription::R2TRenderQuality quality = r2tCam.CamDesc()._renderQuality;
    float qualityMult = quality / (float) (R2TCameraDescription::QHigh);

    Glob.config.tacticalZ = GEngine->GetPipQuality() * 1000;
    _scene.ResetFog();

    _drawObjId = _r2tCam._id + 1;
    _scene.SetDrawObjectsIndex(_drawObjId);

    _cEffect = world.GetCameraEffect();
    _scene.SetComplexityTarget(_complexity * qualityMult * 0.5f);
#if _ARMA3_SIMUL_WEATHER
    GLandscape->EnableSimulWeather(false);
#endif
    _world.GetEyeAccomPars(_eyeAccom, _eyeAccomCut, _customEyeAccom);
    GEngine->BeginR2T(_r2tCam._id);
  }

  ~R2TDrawingState()
  {
    Glob.config.tacticalZ = _tacticalZ;
    _scene.ResetFog();
    _scene.SetShadowQuality(_shadows);
    _scene.SetComplexityTarget(_complexity);
    _scene.SetDrawObjectsIndex(_drawObjectId);
#if _ARMA3_SIMUL_WEATHER
    GLandscape->EnableSimulWeather(_simulWeatherEnabled);
#endif
    GEngine->EndR2T(_r2tCam._id);

    _world.SetEyeAccomPars(_eyeAccom, _eyeAccomCut, _customEyeAccom);

    // restore camera effect
    _world.SetCameraEffect(_cEffect);
  }
};

void World::DrawSceneR2T(Entity *camInVehicle, bool frameRepeated, float deltaT)
{
  PROFILE_SCOPE_EX(dsr2t, *);

  if (!_r2tSourceCamera.Size()) return;

  if (!GEngine->IsR2TEnabled()) return;

  int selR2TCamera = -1;
  {
    static int LastR2TCameraIndex = 0;

    bool camSync = GEngine->R2TCamsSyncNeeded();

    // update cam pars
    for (int i = 0; i < _r2tSourceCamera.Size(); i++)
    {
      R2TCamera &r2tCam = _r2tSourceCamera[i];
      r2tCam._deltaT += deltaT;

      if (camSync) r2tCam._id = R2TUninitialized;
    }

    // select next R2T camera
    LastR2TCameraIndex = (++LastR2TCameraIndex) % _r2tSourceCamera.Size();
    selR2TCamera = LastR2TCameraIndex;
  }

  if (selR2TCamera == -1) return;

  R2TCamera &r2tCam = _r2tSourceCamera[selR2TCamera];

  if (!r2tCam.IsTgtValid())
  {
    if (r2tCam._tgtPars) r2tCam._tgtPars->Reset();

    if (!r2tCam.IsTgtValid())
      return;
  }

  const R2TCameraDescription &r2tCamDesc = r2tCam.CamDesc();

  // draw vehicle interior (cockpit)
  Entity *allowCamInsideVeh = r2tCam._allowCockpitDrawing ? camInVehicle : NULL;

  if (!GEngine->IsBindR2T(r2tCam._id))
  {
    RenderTargetID bindRTID = GEngine->BindR2T(r2tCam.Name());
    r2tCam._id = bindRTID;
    if (R2TUninitialized == bindRTID) return;
  }

  // check whether RT was used in last 8 frames in main scene
  if (!GEngine->WasR2TUsed(r2tCam._id)) return;


#if _ENABLE_CHEATS
  static int TargetFilter = -1;
  if (TargetFilter != -1 && selR2TCamera != TargetFilter)
    return;
#endif

  // select vision
  bool tiWanted = r2tCamDesc._postEffect == R2TCameraDescription::PEThermal;
  bool nvWanted = tiWanted ? false : (r2tCamDesc._postEffect == R2TCameraDescription::PENightVision);
  int effect = (int) r2tCamDesc._postEffect;
  deltaT = r2tCam._deltaT;
  r2tCam._deltaT = 0.0f;

  Scene::DrawContext drawContext;
  GridRectangle groundPrimed;

  DoAssert(_scene.GetDrawObjectIndex() == 0);
  {
    R2TDrawingState drawState(*this, _scene, r2tCam);

    _cameraEffect = r2tCam._camEffect;
    _cameraEffect->SetFOV(r2tCamDesc._fov);

    SimulateCamera(deltaT);
    SelectCameraPosition(GEngine, deltaT);

    // set R2T eye values
    SetEyeAccomPars(r2tCam._eyeAccom, r2tCam._eyeAccomCut, r2tCam._customEyeAccom);
    AdjustEyeAccom(nvWanted, true, r2tCam._id);

    // sometimes we want the adaptation to be fixed at certain level  
    GEngine->SetNightVision(nvWanted, tiWanted, _eyeAccom, _customEyeAccom>0 || EyeGainFixed(false));
    _scene.RecalculateLighting(1,_scene.GetLandscape());

    if (tiWanted)
      SimulateTemperature(0.0f, tiWanted);

    GEngine->InitR2TDraw();

    DrawAmbientTraffic();

    const float PreloadDist = 500.0f;
    // prepare list of scene objects for drawing
    PrepareDraw(allowCamInsideVeh, false, PreloadDist);

    // selects reasonable number of lights for the scene
    _scene.SelectActiveLights(allowCamInsideVeh);

    // Fill out the light grid with lights
    _scene.SelectLightsIntoGrid(true);

    // Preparation to be done prior landscape drawing
    _scene.GetLandscape()->DrawRectPrepare(drawContext, _scene, deltaT, frameRepeated);

    // render the depth buffer (may perform priming as well, unless separate priming is required)
    _scene.GetLandscape()->PrimeDepthBuffer(groundPrimed, _scene, drawContext, false, frameRepeated, 
      drawState._drawObjId, true /*debugMe*/);

    // Render the shadow buffer
     _scene.GetLandscape()->DrawRectShadowBuffer(drawContext, _scene);
    if (GEngine->GetFSAA()>0)
    {
      // PC with FSAA required separate depth priming
      _scene.GetLandscape()->PrimeDepthBuffer(groundPrimed, _scene, drawContext, true, frameRepeated, 
        drawState._drawObjId);
    }

    // need to be called several times during the frame
    GetNetworkManager().FastUpdate();

    Assert(_scene.GetDrawObjectIndex() == drawState._drawObjId);
    GEngine->SetR2TRenderTargets();

    _scene.BeginObjects();

    if (_scene.GetLandscape())
    {
#if !_VBS3
      float modulate = GEngine->GetHDRFactor();
      GEngine->SetModulateColor(Color(modulate,modulate,modulate,1));
#endif
      // draw the landscape
      _scene.GetLandscape()->Draw(drawContext, groundPrimed, _scene, deltaT, frameRepeated, true);
    }

    if (r2tCamDesc._postEffect == R2TCameraDescription::PEColors && r2tCamDesc._peParsUpdated)
    {
      GEngine->R2TSetPEColorsPars(r2tCam._id, r2tCamDesc._pePars.Data(), r2tCamDesc._pePars.Size());
      r2tCamDesc._peParsUpdated = false;
    }
    
    GEngine->R2TPostProcess(effect, deltaT, r2tCam._id);
    GEngine->RestoreR2TRenderTargets(r2tCam._id, false);

    // get eye pars
    GetEyeAccomPars(r2tCam._eyeAccom, r2tCam._eyeAccomCut, r2tCam._customEyeAccom);
  }

  Assert(_scene.GetDrawObjectIndex() == 0);

  _scene.EndObjects();
  _scene.ObjectsCleanUp(drawContext, false);
}

#endif

void World::UpdateTransmitterItem(InventoryItem *item)
{
  bool changeNearbyIT = false;
  ItemTransmitter *radio = static_cast<ItemTransmitter *>(item);
  if (radio->isDirty)
  {
    changeNearbyIT = true;  // change can be inside the same transmitter
    radio->isDirty = false;
    SetChatChannel(ActualChatChannel());
  }
  if (radio->isAudible)
  { // add to _nearbyItemTransmitters
    if (_nearbyItemTransmitters.Find(radio)<0)
    {
      changeNearbyIT = true;
      _nearbyItemTransmitters.Add(radio);
      SetChatChannel(ActualChatChannel());
    }
  }
  else
  {
    int ix = _nearbyItemTransmitters.Find(radio);
    if (ix>=0) 
    {
      changeNearbyIT = true;
      _nearbyItemTransmitters.Delete(ix);
      SetChatChannel(ActualChatChannel());
    }
  }
  if (changeNearbyIT && !_nearbyItemTransmittersChanged)
  { // maybe, the change does not add/remove the frequency from the list, as there can be different nearby transmitter already present
    if (radio->isAudible)
    {
      for (int i=0; i<_transmittersFreqSent.Size(); i++)
      {
         if (_transmittersFreqSent[i] == radio->tunedFrequency) 
         {
           changeNearbyIT = false; // frequency is already in the list of sent frequencies
           break;
         }
      }
    }
    else
    {
      changeNearbyIT = false;
      for (int i=0; i<_transmittersFreqSent.Size(); i++)
      {
         if (_transmittersFreqSent[i] == radio->tunedFrequency)
         {
           changeNearbyIT = true; // removed radio frequency is in the list last sent
           break;
         }
      }
      if (changeNearbyIT)
      {
        for (int i=0; i<_nearbyItemTransmitters.Size(); i++)
        {
          ItemTransmitter *radio = static_cast<ItemTransmitter *>(_nearbyItemTransmitters[i].GetRef());
          if (radio->tunedFrequency == radio->tunedFrequency) 
          {
            changeNearbyIT = false; // but some other nearby radio is still tuned to this frequency
            break;
          }
        }
      }
    }
    // note: this can only set the previously unset _nearbyItemTransmittersChanged. Unset is possible only from NetworkClient after the message send
    _nearbyItemTransmittersChanged = changeNearbyIT; 
  }
}

void World::DrawScene(
  Scene::DrawContext &drawContext, const GridRectangle &groundPrimed, bool enableSceneDraw, Entity *camInsideVehicle,
  bool frameRepeated, bool wantNV, bool wantFlir, float deltaT, const UseWaitingTime &useTime
)
{
  PROFILE_SCOPE_GRF_EX(wDraw,*,SCOPE_COLOR_YELLOW);

  if (enableSceneDraw)
  {
    _scene.BeginObjects();

    // camera may override DOF
    if( _cameraEffect )
    {
      Object *object = _cameraEffect->GetObject();
      float focusDistance = 0,focusBlur = 0;
      if (object)
        object->CamEffectFocus(focusDistance,focusBlur);
      if (focusBlur>0)
        GEngine->EnableDepthOfField(focusDistance,focusBlur,false);
    }
  }

  {        
    // if waiting is likely, we would rather want to control the wait
    // than letting D3D controlling it, as it waits for very long
    TimeManagerDisabled disableTM;
    // waiting until pushbuffer size is reasonable
    float estimate = GEngine->WaitBeforeFrameDrawing();
//    SectionTimeHandle section = StartSectionTime();
    if (estimate>0)
    {
      PROFILE_SCOPE(drwWt);
      {
        // useful waiting
        PROFILE_SCOPE(drwWU);
        PreloadDataTimeLimit(estimate);
      }
      {
        // waiting but doing nothing useful
        PROFILE_SCOPE(drwWI);
        for(;;)
        {
          // estimate how much time is left
          float wait = GEngine->WaitBeforeFrameDrawing();
          if (wait<=0)
            break;
        }
      }
    }
  }

  // draw reflections and landscape
  if( _scene.GetLandscape() )
  {
    if (!frameRepeated)
    {
      // remove all vehicles that should be removed
      MoveOutAndDelete(_vehicles);
      MoveOutAndDelete(_animals);
      MoveOutAndDelete(_fastVehicles);
      MoveOutAndDelete(_slowVehicles);
      MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);
    }
    // buildings may never be moved out or deleted

    if (enableSceneDraw)
    {
      Draw(drawContext, groundPrimed, camInsideVehicle,frameRepeated,wantNV,wantFlir,deltaT,useTime);
    }
    else
    {
      _scene.GetLandscape()->DrawNull(_scene);
      _scene.CleanUp();
      
      useTime();
    }
  }
  else
  {
    if (enableSceneDraw)
    {
      GEngine->SetDepthRange();
      _scene.DrawPreparation(drawContext,deltaT,false);
      _scene.EndObjects(); // finish Object preparation
      _scene.DrawPass1O(drawContext,false);
      _scene.DrawPass1A(drawContext,false);
      _scene.DrawPass2(drawContext);
      _scene.ObjectsDrawn(drawContext
#if _HELISIM_RENDER_TO_TEXTURE
        , _scene.GetDrawObjectIndex()
#endif
        );
      PostFxSettings pfx;
      pfx.rotBlur = false;
      GEngine->Postprocess(deltaT,pfx);
      _scene.DrawFlares(deltaT);
      _scene.ObjectsCleanUp(drawContext);
      _scene.LightsCleanUp();
    }
    else
      _scene.CleanUp();
  }
}

// TODO: ensure thorough use of RenderVisualState
void World::DrawUI(bool wantNV, bool wantFlir)
{
  if (_loadingScreen)
  {
    _loadingScreen->DrawProgress(1.0f);
    return;
  }

#ifdef _WIN32
  if (_jUI)
  {
    // texture mapping 1:1
    Rect2DPixel rect(0, 0, 1024, 1024);
    _jUI->Draw(rect);
  }
#endif

  OLinkObject cameraVehicle=_cameraOn;
  OLink(Entity) camVehicle=dyn_cast<Entity,Object>(cameraVehicle);
  OLink(EntityAI) camAI=dyn_cast<EntityAI,Entity>(camVehicle);
  OLink(Person) person = FocusOn() ? FocusOn()->GetPerson() : NULL;

  GEngine->EnableNightEye(0);
  GEngine->SetNightVision(false,false,3.0f,true);

  PROFILE_SCOPE_GRF_EX(hudDr,*,SCOPE_COLOR_YELLOW);
  if( _map && (_showMap || _forceMap))
  {
    GEngine->SetNightVision(false,false,3.0f,true);
    _scene.RecalculateLighting(1,NULL);
    _map->DrawHUD(camAI,1);
    if (_showMapProgress) _showMapProgress->DrawProgress(1.0f);
  }
  else
  {
    if (_showMapProgress)
    {
      // we want to draw a progress, but there is no map
      // this is used for async preloads in MP
      GEngine->Draw2DWholeScreen(PackedBlack);
      _showMapProgress->DrawProgress(1.0f);
    }
  }
  
  // no more map objects will be loaded in this frame - distribute the time between other activities
  GTimeManager.Finished(TCLoadMapObjects);
  bool drawHints = false;
  if (_ui)
  {
    if (_cameraEffect)
    {
      if (_cameraEffect->IsHUDEnabled() && camAI && IsUIEnabled())
        _ui->DrawHUDCameraEffect(*_scene.GetCamera(), GetCameraType());
    }
    else if (camAI)
    {
      GEngine->SetNightVision(wantNV,wantFlir,_eyeAccom,_customEyeAccom>0 || EyeGainFixed(wantNV));
      _scene.RecalculateLighting(1,_scene.GetLandscape());
      CameraType camTypeAct = GetCameraType();
      CameraType camTypeInt = GetCameraTypePreferInternal();
      if (camTypeInt == CamInternal || camTypeInt==CamGunner)
      {
        if (!HasMap())
          camAI->DrawCameraCockpit();
      }
      if (!HasMap())
        camAI->DrawPeripheralVision(person, camTypeAct);
      // avoid NV optics in cutscenes
      if (wantNV && person && !_cameraEffect && dyn_cast<EntityAI>(_cameraOn.GetLink()))
        person->DrawNVOptics();
      camAI->DrawActivities();
      GEngine->SetNightVision(false,false,3.0f,true);
      _scene.RecalculateLighting(1,NULL);

      if (IsUIEnabled()
#if _VBS3 && _VBS_TRACKING
        && !camAI->IsTrackerConencted()
#endif
        )
      {
        _ui->DrawHUD(*_scene.GetCamera(),camTypeAct);
        drawHints = true;
      }
    }
    else if (camVehicle)
    {
      GEngine->SetNightVision(wantNV,wantFlir,_eyeAccom,_customEyeAccom>0 || EyeGainFixed(wantNV));
      _scene.RecalculateLighting(1,_scene.GetLandscape());
      camVehicle->DrawCameraCockpit();
      camVehicle->DrawActivities();
      GEngine->SetNightVision(false,false,3.0f,true);
      _scene.RecalculateLighting(1,NULL);
      // seagull HUD (mouse cursor drawing)
      if (IsUIEnabled()
#if _VBS3 && _VBS_TRACKING
         && (!camAI || !camAI->IsTrackerConencted())
#endif
        )
        _ui->DrawHUDNonAI(*_scene.GetCamera(),camVehicle,GetCameraType());
    }
  }

  if (HasMiniMap() && !_miniMap) CreateMiniMaps(); //moved here from but from SwitchLandscape, where it caused GPS to be shown in main menu
  bool showMiniMap = _miniMap && HasMiniMap();
  
  if (IsUIEnabled() &&  showMiniMap)
  {
    Display *miniMapDisplay = NULL;
    if (_showMiniMap && !_showMiniMapToggle) //large
      miniMapDisplay = static_cast<Display*>(_miniMap.GetRef());
    else if (_showMiniMap)//small map
      miniMapDisplay = static_cast<Display*>(_miniMapSmall.GetRef());
    if (miniMapDisplay)
    {
      CStaticMapMain *map = static_cast<CStaticMapMain*>(miniMapDisplay->GetCtrl(IDC_MINIMAP));
      if (map)
      {
        map->CenterMiniMap();
        miniMapDisplay->DrawHUD(NULL,1.0f);
      }
    }
  }

  // titles are on top of cuts
  //if (IsDisplayEnabled())
  if (IsUIEnabled() || IsDisplayEnabled())
  {
    for (int i=0; i<_cutEffects.Size(); i++)
    {
      TitleEffect *effect = _cutEffects[i];
      if (!effect)
        continue;
      if(!effect->DrawInMap() && HasMap())
        continue;
      if (effect && effect->Preload())
        effect->Draw();
    }
  }
#if !_VBS3 //draw titletext over userdialogs
  if (IsUIEnabled())
  {
    if (_titleEffect && _titleEffect->Preload())
      _titleEffect->Draw();
  }
#endif
  NetworkClientState state = GetNetworkManager().GetClientState();

  if( _userDlg )
    _userDlg->DrawHUD(NULL, 1);
  if( _options )
    _options->DrawHUD(NULL, 1);
  for (int i=_displays.Size()-1; i>=0; i--)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display)
      display->DrawHUD(NULL, 1);
  }
#if _VBS3 //draw titletext over userdialogs
  if (IsUIEnabled())
  {
    if (_titleEffect && _titleEffect->Preload()) _titleEffect->Draw();
  }
#endif

  if(drawHints) _ui->DrawHints();

  if (_channel)
    _channel->DrawHUD(NULL, 1);
  if (_chat)
    _chat->DrawHUD(NULL, 1);
  if (_voiceChat)
    _voiceChat->DrawHUD(NULL, 1);
  GChatList.OnDraw();
  // ADDED in Patch 1.01 - reimplementation of WarningMessage
  if (_warningMessage)
  {
    _warningMessage->OnDraw(NULL, 1);
    _warningMessage->DrawCursor();
  }

#if _USE_BATTL_EYE_SERVER
  if (_beLicenceMsg)
  {
    _beLicenceMsg->OnDraw(NULL, 1);
    _beLicenceMsg->DrawCursor();
  }
#endif

#if _ENABLE_CHEATS
  void DrawNetworkStatistics();
  DrawNetworkStatistics();
#endif

  GetNetworkManager().OnDraw();

  if (IsDedicatedServer()) return; // do not draw title effects etc.
  //@{ _DEDICATED_CLIENT
  if ( IsDedicatedClient() )
  return;
  //@}

  if (state == NCSPlaying)
  {
    bool drawMpTable = false;
    if (GInput.GetAction(UANetworkStats, false))
      drawMpTable = true;
    if (drawMpTable && Glob.config.IsEnabled(DTNetStats))
    {
      // no MP stats currently available in Dayz
    }
  }
  GEngine->FlushQueues();
  GEngine->SetNightVision(false,false,1.0f,true);
}

const float PreloadTimeLimit = 0.010; // max. time dedicated to preloading

void World::PreloadData(float reserveLeft)
{
  const float floatMinReserve = 0.040f;
  const float floatMaxReserve = 0.080f;

  // when time reserve is low, reduce preloading time
  float maxPreloadTime = Interpolativ(reserveLeft,floatMinReserve,floatMaxReserve,0,PreloadTimeLimit);
  PreloadDataTimeLimit(maxPreloadTime);
}

void World::PreloadDataTimeLimit(float maxPreloadTime)
{
  // when time reserve is high, we can preload high quality
  float preloadDegrade = Interpolativ(maxPreloadTime,0,PreloadTimeLimit,1.0f,0.25f);
  
  Vector3Val pos = _scene.GetCamera()->Position();
  float maxObjDist = floatMin(_scene.GetFogMaxRange(),Glob.config.objectsZ);

  FreeOnDemandGarbageCollect(mainHeapFreeRequired,systemFreeRequired);
  float maxPreloadDist = floatMax(150,maxObjDist*0.3);
  GLandscape->PreloadData(pos,maxPreloadDist,maxPreloadTime,preloadDegrade,CameraOn());
  // we can precreate shadows and vertex buffer if there is still time left
  //Preload1stPerson();
}

void IntentionalCrash()
{
  // cause access violation
  volatile int a = *(int *)NULL;
  (void)a;
}

void IntentionalError()
{
  ErrorMessage("Shift-Minus-ERROR");
}

void World::ProcessCheats(float noAccDeltaT)
{
  PROFILE_SCOPE_EX(wChts,*);
#if _ENABLE_CHEATS
  if (GInput.GetCheatXToDo(CXTCrash))
    IntentionalCrash();
  if (GInput.GetCheatXToDo(CXTFreeze))
  {
    // used for freeze report testing
    // freeze - infinite loop
    for(;;){}
  }
#endif

  if (GInput.CheatActivated() == CheatCrash)
  {
    //LPTOP_LEVEL_EXCEPTION_FILTER prevExc = SetUnhandledExceptionFilter(TestExceptionCallback);
    GInput.CheatServed();
    IntentionalCrash();
  }
  if (GInput.CheatActivated() == CheatError)
  {
    //LPTOP_LEVEL_EXCEPTION_FILTER prevExc = SetUnhandledExceptionFilter(TestExceptionCallback);
    GInput.CheatServed();
    IntentionalError();
  }

#if _ENABLE_CHEATS
  if (GInput.GetCheat1ToDo(DIK_C))
  {
    void CreateDebugConsole(GameState *state);
    CreateDebugConsole(GetGameState());
  }

#if USER_jana1
  if (GInput.GetCheat2ToDo(DIK_L))
  {
    Soldier *body = (Soldier*)GWorld->GetRealPlayer();
		body->_ragdollCtrl = body->CreateRagdoll();
		/*
    Vector3 ppos = body->GetPosition();
    
    Soldier *soldier = new Soldier(const_cast<VehicleType *>(body->GetType()));
    //soldier->DisableSimulation(true);
    
    Vector3 position = ppos + (2.f * body->FutureVisualState().Direction());
    
    //obslehly z DoRespawn jen jeho objeveni se bez mozku a beze vseho
    soldier->SetID(GLandscape->NewObjectID());

    // add to world
    position[1] = position[1] + .5f;//GLandscape->RoadSurfaceYAboveWater(position + VUp * 0.5f);

    Matrix4 pos;
    pos.SetOrientation(body->FutureVisualState().Orientation());
    pos.SetDirection(-body->FutureVisualState().Direction());
    pos.SetPosition(position);
    soldier->SetTransform(pos);
    soldier->Init(pos, true);
    //soldier->_swimming = true;
    GLOB_WORLD->AddVehicle(soldier);
    
    //soldier->CreatePhysics(EPL_CHARACTER);
		*/
  }
#endif
	#endif

	if (!_editor)
  {
#if _ENABLE_CHEATS
    if( GInput.GetCheat1ToDo(DIK_R))
    {
      Glob.config.difficulty++;
      if (Glob.config.difficulty >= Glob.config.diffSettings.Size())
        Glob.config.difficulty = 0;
      const DifficultySettings *settings = Glob.config.GetDifficultySettings();
      GlobalShowMessage(500,"Difficulty: (%d) %s", Glob.config.difficulty, settings ? settings->displayName : "");
    }
    if (GInput.GetCheat2ToDo(DIK_Z) || GInput.GetCheatXToDo(CXTImmortality))
    {
      Glob.config.super = !Glob.config.super;
      GlobalShowMessage(500, "Immortality %s", Glob.config.super ? "On" : "Off");
    }
    if (GInput.GetCheat1ToDo(DIK_M))
      showCinemaBorder = !showCinemaBorder;
#endif
#if _ENABLE_LIFE_CHEAT
    if (GInput.CheatActivated() == CheatImmortality)
    {
      Glob.config.super = !Glob.config.super;
      GlobalShowMessage(500, "Immortality %s", Glob.config.super ? "On" : "Off");
      GInput.CheatServed();
    }
#endif
    if (_mode == GModeNetware)
      _acceleratedTime = 1;
    else
#if !_ENABLE_CHEATS && !_VBS3
      if (!_cameraEffect)
#endif
      {
        int time = GInput.GetCheatXToDo(CXTTime);
        if(GInput.GetActionToDo(UATimeInc) || time>0)
        {
          _acceleratedTime*=2;
          saturate(_acceleratedTime,_TIME_ACC_MIN,_TIME_ACC_MAX);
          GlobalShowMessage(1000,LocalizeString(IDS_TIME_ACC_FORMAT),_acceleratedTime);
          //SetActiveChannels();
        }
        if(GInput.GetActionToDo(UATimeDec) || time<0)
        {
          _acceleratedTime*=0.5;
          GInput.keysToDo[DIK_PGDN] = false;
          saturate(_acceleratedTime,_TIME_ACC_MIN,_TIME_ACC_MAX);
          GlobalShowMessage(1000,LocalizeString(IDS_TIME_ACC_FORMAT),_acceleratedTime);
          //SetActiveChannels();
        }
      }
  }

#if _ENABLE_CHEATS
  bool CanSaveGame();
  if ((GInput.CheatActivated() == CheatSaveGame || GInput.GetCheatXToDo(CXTSaveGame)) && CanSaveGame())
  {
    SaveGame(SGTUsersave);
    GInput.CheatServed();
  }
  if (GInput.GetCheat1ToDo(DIK_P))
  {
    _enableSimulation=!_enableSimulation;
    // when simulation is disabled, stop loading textures and models as well
    // this should help debugging streaming/LOD management issues
    StopLoadingTextures = !_enableSimulation;
    StopLoadingModels = !_enableSimulation;
    _singleStepSimulation = false;
  }
  if (GInput.GetCheat2ToDo(DIK_Q))
  {
    Landscape::ShowSatelliteNormal = !Landscape::ShowSatelliteNormal;
    DIAG_MESSAGE(500, Format("Sat. normal texture %s", Landscape::ShowSatelliteNormal ? "enabled": "disabled"));
  }

  if (GInput.GetCheat1ToDo(DIK_S))
  {
    RString dir = GetTmpSaveDirectory();
    RString filename;
    for (int i=1; i<=99999; i++)
    {
      filename = Format("%sTMP%05d.%s", cc_cast(dir), i, cc_cast(GetExtensionSave()));
      if (!QIFileFunctions::FileExists(filename))
      {
        SaveBin(filename, IDS_SAVE_GAME);
        break;
      }
    }
  }
  if (GInput.GetCheat1ToDo(DIK_L))
  {
    RString dir = GetTmpSaveDirectory();
    RString filename;
    for (int i=1; i<=99999; i++)
    {
      filename = Format("%sTMP%05d.%s", cc_cast(dir), i, cc_cast(GetExtensionSave()));
      if (!QIFileFunctions::FileExists(filename))
      {
        if (i > 1)
        {
          filename = Format("%sTMP%05d.%s", cc_cast(dir), i - 1, cc_cast(GetExtensionSave()));
          LoadBin(filename, IDS_LOAD_GAME);
        }
        break;
      }
    }
  }
  if (GInput.GetCheat2ToDo(DIK_B))
    DebugOperMapTrouble();
  if (GInput.GetCheat2ToDo(DIK_M))
    DebugOperMap();
//   if (GInput.GetCheat3ToDo(DIK_xxx))
//   {
//     DebugOperMapCover();
//   }
  if (GInput.GetCheat3ToDo(DIK_V))
  {
    EndLoadingScreen();
    GlobalShowMessage(500, "Loading screen closed");
  }

  if (GInput.GetCheat2ToDo(DIK_X))
  {
    EntityAI *veh = dyn_cast<EntityAI>(CameraOn());
    AIBrain *unit = veh ? veh->PilotUnit() : NULL;
    DebugOperMap(unit);
  }
//   if (GInput.GetCheat2ToDo(DIK_PERIOD))
//     Pars.SaveBin("bin\\config.bin");
  if (GInput.GetCheat2ToDo(DIK_BACKSLASH))
  {
    forceControlsPaused = !forceControlsPaused;
    if (forceControlsPaused)
      GlobalShowMessage(500, "controls paused on");
    else
      GlobalShowMessage(500, "controls paused off");
  }
  // do not allow config reload while game is running - would crash the game
  if (GInput.GetCheat2ToDo(DIK_C) && !IsSimulationEnabled())
    Fail("Config reload no longer supported");

  const static int cheatVar[]=
  {
    DIK_0,DIK_1,DIK_2,DIK_3,DIK_4,
    DIK_5,DIK_6,DIK_7,DIK_8,DIK_9
  };
  for( int i=0; i<sizeof(cheatVar)/sizeof(*cheatVar); i++ )
  {
    if (GInput.GetCheat1ToDo(cheatVar[i]))
    {
      BString<64> varName;
      sprintf(varName,"cheat%d",i);
      GetGameState()->VarSet(varName, true, false, false, GetMissionNamespace()); // mission namespace
      break;
    }
  }

  if (GInput.GetCheatXToDo(CXTSoundDiag))
  {
    DiagMode ^= 1<<DESound;
    GlobalShowMessage(1000,(DiagMode&(1<<DESound)) ? "Sound diags on" : "Sound diags off");
  }
  if (GInput.GetCheatXToDo(CXTNetworkDiag))
  {
    DiagMode ^= 1<<DENetwork;
    GlobalShowMessage(1000,(DiagMode&(1<<DENetwork)) ? "Network diags on" : "Network diags off");
  }
  if (GInput.GetCheatXToDo(CXTMapShowCost))
  {
    DiagMode ^= 1<<DECostMap;
    GlobalShowMessage(1000,(DiagMode&(1<<DECostMap)) ? "Cost/lock diags on" : "Cost/lock diags off");
  }

  int frameChange = GInput.GetCheatXToDo(CXTFrameRate);
  if (frameChange)
    GEngine->ToggleFps(GEngine->ShowFpsMode() + frameChange);
  // pages are switched using LT+RB+X/Y
  if (GInput.GetCheatXDirectToDo(XBOX_Y))
    GEngine->StatsPage(+1);
  if (GInput.GetCheatXDirectToDo(XBOX_X))
    GEngine->StatsPage(-1);

  {
    if (GInput.GetCheat2ToDo(DIK_J))
    {
      //size_t free = MemoryFreeOnDemand(INT_MAX);
      // release some memory, but not all
      size_t free = FreeOnDemandMemory(2*1024*1024,true);
      GlobalShowMessage(30000, "Memory released: %d KB", free/1024);
      // browse shape memory information
    }
    if (GInput.GetCheat1(DIK_U))
    {
      _actualOvercast+=noAccDeltaT*0.1*GInput.GetCheat1(DIK_U);
      saturate(_actualOvercast,0,1);
      _wantedOvercast=_actualOvercast;
      GLOB_LAND->SetOvercast(_actualOvercast);
    }
    if (GInput.GetCheat1(DIK_I))
    {
      _actualOvercast-=noAccDeltaT*0.1*GInput.GetCheat1(DIK_I);
      saturate(_actualOvercast,0,1);
      _wantedOvercast=_actualOvercast;
      GLOB_LAND->SetOvercast(_actualOvercast);
    }
    if (GInput.GetCheat1(DIK_COMMA))
    {
      _actualFog+=noAccDeltaT*0.1*GInput.GetCheat1(DIK_COMMA);
      saturate(_actualFog,0,1);
      _wantedFog=_actualFog;
      GLOB_LAND->SetFog(_actualFog);
    }
    if (GInput.GetCheat1(DIK_PERIOD))
    {
      _actualFog-=noAccDeltaT*0.1*GInput.GetCheat1(DIK_PERIOD);
      saturate(_actualFog,0,1);
      _wantedFog=_actualFog;
      GLOB_LAND->SetFog(_actualFog);
    }

    if (!_showMap && GInput.GetCheat1ToDo(DIK_V))
    {
      // cycle through diagnostic modes
      if (DiagMode!=0 || DiagMode2!=0)
      {
        DiagMode = 0;
        DiagMode2 = 0;
        GlobalShowMessage(100,"Diag off");
      }
      else
      {
        DiagMode = (1<<DECombat)|(1<<DEPath);
        GlobalShowMessage(100,"Diag: Combat + Path");
      }
    }

    if (GInput.GetCheat1ToDo(DIK_X) || GInput.GetCheatXToDo(CXTNoTex))
    {
      DisableTextures=!DisableTextures;
      GlobalShowMessage(500,"Textures %s",DisableTextures ? "Off":"On");
    }
    if (GInput.GetCheatXToDo(CXTNoTexLoad) || GInput.GetCheat2ToDo(DIK_COMMA))
    {
      StopLoadingTextures=!StopLoadingTextures;
      GlobalShowMessage(500,"Loading textures %s",StopLoadingTextures ? "Off":"On");
    }
//     if (GInput.GetCheatXToDo(CXTNoShapeLoad) || GInput.GetCheat2ToDo(DIK_PERIOD))
//     {
//       LoadTexturesInFrame = FreeOnDemandFrameID();
//     }
    if (GInput.GetCheatXToDo(CXTNoShapeLoad) || GInput.GetCheat2ToDo(DIK_PERIOD))
    {
      StopLoadingModels=!StopLoadingModels;
      GlobalShowMessage(500,"Loading models %s",StopLoadingModels ? "Off":"On");
    }
  }

  if (GInput.GetCheat1ToDo(DIK_J))
    BrowseCamera(-1);
  if (GInput.GetCheat1ToDo(DIK_K))
    BrowseCamera(+1);
  if (GInput.GetCheat2ToDo(DIK_K))
  {
    QOStrStream out;
    Object *camOn = _cameraOn;
    if (camOn)
    {
      char buf[1024];
      Vector3Val pos = camOn->RenderVisualState().Position();
      Vector3Val dir = camOn->RenderVisualState().Direction();
      float posSY = GLandscape->SurfaceYAboveWater(pos.X(),pos.Z());
      sprintf(buf,"'%s' setPos [%.3f,%.3f,%.3f]\r\n",(const char *)camOn->GetDebugName(),pos.X(),pos.Z(),pos.Y()-posSY);
      out.write(buf,strlen(buf));
      float head = atan2(dir.X(),dir.Z());
      sprintf(buf,"'%s' setDir %.0f\r\n",(const char *)camOn->GetDebugName(),head*(180/H_PI));
      out.write(buf,strlen(buf));

      char logFilename[MAX_PATH];
      if (GetLocalSettingsDir(logFilename))
      {
        CreateDirectory(logFilename, NULL);
        TerminateBy(logFilename, '\\');
      }
      else
        logFilename[0] = 0;
      strcat(logFilename, "clipboard.txt");
      ExportToClipboardAndFile(out.str(), out.pcount(), logFilename);
      //CONTEXT_FULL
    }
  }

  if( GInput.GetCheat1ToDo(DIK_F))
    // display frame rate
    GEngine->ToggleFps(GEngine->ShowFpsMode() + 1);
  if (GInput.GetCheat1ToDo(DIK_PGDN))
    GEngine->StatsPage(+1);
  if (GInput.GetCheat1ToDo(DIK_PGUP))
    GEngine->StatsPage(-1);
#endif

#if _ENABLE_CHEATS
  if (GInput.GetCheat2ToDo(DIK_R))
    _noDisplay = !_noDisplay;
#endif

#if _ENABLE_CHEATS

#if !_RELEASE && !_DEBUG
#define REGULAR_FOOTPRINT 0
#endif

#if REGULAR_FOOTPRINT
  void MemoryFootprint();
  static DWORD lastSample = 0;
  if (GlobalTickCount()>lastSample+60000)
  {
    MemoryFootprint();
    lastSample=GlobalTickCount();
  }
  if (GInput.GetCheat1ToDo(DIK_O))
  {
    MemoryFootprint();
    // avoid any more samples
    lastSample = UINT_MAX;
  }
#else
  if(GInput.GetCheat1ToDo(DIK_O) || GInput.GetCheatXToDo(CXTMemSample))
  {
    void MemoryFootprint();
    void ReportMemoryStatus();
    MemoryFootprint();
    ReportMemoryStatus();
  }
  if(GInput.GetCheat2ToDo(DIK_O))
  {
    void PrintVMMap(int extended, const char *title=NULL);
    PrintVMMap(-1);
  }
  int memLimit = GInput.GetCheatXToDo(CXTMemLimit);
  if (memLimit)
  {
    void ChangeMemoryUsageLimit(int memLimit);
    ChangeMemoryUsageLimit(memLimit);
  }
  if (GInput.GetCheatXToDo(CXTMemFlush))
  {
    static const char *slotOrder[]={
      "Xbox HW","Terrain","Sounds",
        "Map lists","Object lists",
        "Geometries","ShapeLevels",
        "Shadows","LODShapes","Materials","TexCache",
        "Anim","config.bin","File cache",""
    };
    // detailed flushmem stats
    for (int i=0; i<lenof(slotOrder); i++)
    {
      const char *id = slotOrder[i];
      FreeOnDemandReleaseSlot(id,INT_MAX);
    }

    //FreeOnDemandMemory(INT_MAX);
    //FreeOnDemandSystemMemory(INT_MAX);
    //FreeOnDemandLowLevelMemory(INT_MAX);
  }
#endif

#endif
}

Object *World::GetCameraFocusObject() const
{
  // TODO: check camera effect first
  if (_cameraEffect)
  {
    if (CameraHolder *cam = dyn_cast<CameraHolder,Object>(_cameraEffect->GetObject()))
    {
      Object *obj = cam->GetFocus();
      if (obj)
        return obj;
    }
  }

  if (UI())
  {
    Object *obj = UI()->GetCursorObject();
    if (obj)
      return obj;
  }
  return NULL;
}

static inline bool IsAttachedOn(Object *camOn, const Object *obj)
{
  if (camOn==obj) return true;
  Entity * veh = dyn_cast<Entity,Object>(camOn);
  if (veh) for(; Entity *next = veh->GetAttachedTo(); veh=next)
  {
    if (next==obj) return true;
  }
  return false;
}

void World::SimulateRestTrackedByCamera()
{
  if (CHECK_DIAG(DEVisualStates))
  {
    return;
  }
  if (_cameraEffect && _cameraEffect->GetObject())
  {
    Entity *veh = dyn_cast<Entity>(_cameraEffect->GetObject());
    if (veh)
    {
      veh = veh->GetAttachedToRoot();
      veh->SimulateRest(0,veh->GetLastImportance());
    }
  }
  {
    Entity *veh = dyn_cast<Entity,Object>(_cameraOn);
    if (veh)
    {
      veh = veh->GetAttachedToRoot();
      veh->SimulateRest(0,veh->GetLastImportance());
    }
  }
}

bool World::IsTrackedByCamera(const Object *obj) const
{
  /* sometimes it is useful to debug visual states on entity controlled by a player
  if (CHECK_DIAG(DEVisualStates))
  {
    return false;
  }
  */
  if (_cameraEffect && _cameraEffect->GetObject())
  {
    // TODO: traverse the attachment chain
    if (IsAttachedOn(_cameraEffect->GetObject(),obj))
      return true;
  }
  return IsAttachedOn(_cameraOn,obj);
}


Entity *World::CameraOnVehicle() const
{
  if (CHECK_DIAG(DEAnimation))
  {
    if (GetCameraEffect()) return NULL;
  }
  if (CHECK_DIAG(DEVisualStates))
  {
    return NULL;
  }
  Object *camOn = NULL;
  if (_cameraEffect && _cameraEffect->GetObject()) 
  {
    camOn = _cameraEffect->GetObject();
  }
  if (!camOn) camOn = _cameraOn;
  Entity * veh = dyn_cast<Entity,Object>(camOn);
  if (veh) veh = veh->GetAttachedToRoot();
  return veh;
}

/**
@param typicalDist control how much are we focused around the center point. 1/2 of objects falls in this distance
*/
Vector3 RandomOffset(float typicalDist)
{
  // Cauchy distribution - http://en.wikipedia.org/wiki/Cauchy_distribution
  float angle = GRandGen.RandomValue()*H_PI;
  float distance = typicalDist*tan(H_PI*(GRandGen.RandomValue()-0.5f));
  float sa = sin(angle), ca = cos(angle);
  return Vector3(sa,0,ca)*distance;
}

static inline RString DiscardProcedural(RString name)
{
  if (name[0]=='#')
    return RString();
  return name;
}

static RString SelectRandomFile(const TexMaterial *mat)
{
  if (!mat)
    return RString();
  int randomStage = GRandGen.RandomInt(0,mat->_stageCount+2);
  if (randomStage>=mat->_stageCount+1)
    return DiscardProcedural(mat->GetName());
  const Texture *tex = mat->_stage[randomStage]._tex;
  if (!tex)
    return RString();
  return DiscardProcedural(tex->GetName());
}

static RString SelectRandomFile(const Object *obj)
{
  if (!obj || !obj->GetShape())
    return RString();
  for(int tries=3; --tries>=0; )
  {
    int lod = GRandGen.RandomInt(0,toIntCeil(obj->GetShape()->NLevels()*1.3f+1));
    if (lod>=obj->GetShape()->NLevels())
      return DiscardProcedural(obj->GetShape()->GetName());
    const Shape *shape = obj->GetShape()->GetLevelLocked(lod);
    if (!shape || shape->NSections()<=0)
      return RString();
    int section = GRandGen.RandomInt(0,shape->NSections());
    const ShapeSection &sec = shape->GetSection(section);
    if (GRandGen.RandomValue()<0.2f && sec.GetTexture())
      return DiscardProcedural(sec.GetTexture()->GetName());
    if (sec.GetMaterialExt())
      return SelectRandomFile(sec.GetMaterialExt());
  }
  return DiscardProcedural(obj->GetShape()->GetName());
}


RString World::RandomUsedFile(Vector3Par pos) const
{
  const Landscape *land = _scene.GetLandscape();
  if (!land) return RString();
  float contentType = GRandGen.RandomValue();
  if (contentType<0.05f)
  {
    // check terrain texture / material
    // random coordinates around player
    Vector3 sample = pos+RandomOffset(Glob.config.horizontZ*0.5f);
    int x = toIntFloor(sample.X()*land->GetInvLandGrid());
    int z = toIntFloor(sample.Z()*land->GetInvLandGrid());
    const Scenery::TextureInfo &info = land->ClippedTextureInfo(x,z);
    // select either a material file, or a random stage
    return SelectRandomFile(info._mat);
  }
  else if (contentType<0.10f)
  {
    // select a random vehicle
    // most often select nearest vehicles
    for(int tries=10; --tries>=0; )
    {
      SimulationImportance sim = (SimulationImportance)GRandGen.RandomInt(0,SimulateDefault);
      const EntityList &list = _vehicles.Select(sim);
      if (list.VehicleCount()<=0)
        continue;
      const Entity *obj = list.SelectRandom(GRandGen.RandomInt(0,list.VehicleCount()));
      RString ret = SelectRandomFile(obj);
      if (!ret.IsEmpty())
        return ret;
    }
  }
  else if (contentType<0.95f)
  {
    // as many fields contain no objects, we may want to repeat this several times
    for(int tries=10; --tries>=0; )
    {
      // check objects around a player
      Vector3 sample = pos+RandomOffset(Glob.config.horizontZ*0.5f);
      int x = toIntFloor(sample.X()*land->GetInvLandGrid());
      int z = toIntFloor(sample.Z()*land->GetInvLandGrid());
      const ObjectList &list = land->GetObjects(z,x);
      int size = list.Size();
      if (size<=0)
        continue;
      const Object *obj = list[GRandGen.RandomInt(0,size)];
      RString ret = SelectRandomFile(obj);
      if (!ret.IsEmpty())
        return ret;
    }
    return RString();
  }
  else {} // fall through
  // check world file
  return land->GetName();
}

Object *World::GetCamInsideVehicle() const
{
  Object *camInsideVehicle = NULL;

  if (_cameraEffect)
  {
    if (_cameraEffect->IsInside())
      camInsideVehicle = _cameraEffect->GetObject();
  }
  else if (_cameraOn)
  {
    // lod selection - prefer external camera for transitions
    CameraType camType = _camTypeOld;
    static float oldThold = 0.2f;
    static float newThold = 0.8f;
    // check also transition in Man::Draw
    if (_camTypeNewFactor<=oldThold)
      camType = _camTypeOld;
    else if (_camTypeNewFactor>=newThold)
      camType = _camTypeNew;
    else
      // if in doubt, prefer external camera
      camType = _camTypeNew!=CamGunner ? _camTypeNew : _camTypeOld;
    switch (camType)
    {
    //case CamGunner:
    //case CamInternal:
    default:
      camInsideVehicle = _cameraOn;
      break;
    case CamExternal:
    case CamGroup:
      camInsideVehicle=NULL;
      break;
    }
  }
  return camInsideVehicle;
}

#if _ENABLE_CHEATS
bool disableAI=false;
bool disableUnitAI=false;
bool disableSimpleSim=false;
bool forceSimpleSim=false;
bool forceFarSim=false;
#endif

#if _ENABLE_REPORT && _ENABLE_CHEATS
float LastDeltaT = 0;
#endif

#define LOG_FAST_UPDATES  0

#if _MOTIONCONTROLLER
  #include "hla/MotionController.hpp"
#endif

void ClearClutterDrawCache();


void World::AdjustEyeAccom(bool wantNV
#if _HELISIM_RENDER_TO_TEXTURE
                           , bool isR2T, RenderTargetID rtid
#endif
                           )
{
  {
    if (_customEyeAccom > 0)
    {
      _eyeAccomCut = true;
      _eyeAccom = _customEyeAccom;
    }
    else if (EyeGainFixed(wantNV))
    {
      // with night vision assume no eye accommodation - it is an electronic device
      _eyeAccomCut = true;
      // value which is really used is written in Engine::RecalcAccomodation
      _eyeAccom = 1;
    }
    else
    {
#if _HELISIM_RENDER_TO_TEXTURE
      float eyeAccomNew = isR2T ? GEngine->GetLastKnownR2TAvgIntensity(rtid) : GEngine->GetLastKnownAvgIntensity();
#else
      float eyeAccomNew = GEngine->GetLastKnownAvgIntensity();
#endif
      float eyeAccom;
      if (eyeAccomNew<0)
      {
        eyeAccom = _scene.MainLight()->EyeAdaptation(_scene.GetCamera()->GetAge(),_scene.GetCamera()->Direction(),
          _scene.GetCamera()->Left(),_scene.GetCamera()->Top());
      }
      else
      {
        // if dynamic data are available, prefer them
        eyeAccom = eyeAccomNew;
#if _HELISIM_RENDER_TO_TEXTURE
        float minAccom = _scene.MainLight()->GetEyeAdaptMin(isR2T);
#else
        float minAccom = _scene.MainLight()->GetEyeAdaptMin();
#endif
        float maxAccom = _scene.MainLight()->GetEyeAdaptMax();
        saturate(eyeAccom,minAccom,maxAccom);
      }
      if (_eyeAccomCut)
      {
        _eyeAccom = eyeAccom;
        _eyeAccomCut = false;
#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEHDR))
          DIAG_MESSAGE(2000,Format("Eye acc estim %.3f, blind %.3f",_eyeAccom,_blindIntensity));
#endif
      }
      else
      {
        // smooth change
        // if ratio is small, ignore it to avoid oscillation
        _eyeAccom = eyeAccom;
#if 0
        // linear adjustment
        const float getUsedToDark = 5; // time to make change +1
        const float getUsedToLight = 2; // time to make change -1
        float delta = eyeAccom - _eyeAccom;
        saturate(delta,deltaT*(-1/getUsedToLight),deltaT*(1/getUsedToDark));
        _eyeAccom += delta;
#endif
#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEHDR))
        {
          float invDiameter = InvSqrt(_eyeAccom);
          DIAG_MESSAGE(100,Format("Aperture %.2f, (Eye adapt %.5g, est %.5g), blind %.3f",
            invDiameter,_eyeAccom,_scene.MainLight()->EyeAdaptation(_scene.GetCamera()->GetAge(),_scene.GetCamera()->Direction(),
            _scene.GetCamera()->Left(),_scene.GetCamera()->Top()),_blindIntensity));
        }
#endif
      }
    }
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEHDR))
      DIAG_MESSAGE(100,Format("Brightness %.3f, gamma %.2f, pre HDR-brightness %.2f",
        GEngine->GetBrightness(),GEngine->GetGamma(),GEngine->GetPreHDRBrightness()));
#endif
  }
}

struct ObjectIdFunctor
{
  mutable AutoArray<int> ids;

  bool operator() (Entity *obj) const
  {
    int id = obj->ID();
    for (int i=0; i<ids.Size(); i++)
      if (ids[i]==id) 
      {
        LogF("Duplicate ObjectId: %d (%s)", id, cc_cast(obj->GetDebugName()));
        return true;
      }
    ids.Add(id);
    return false;
  }
};

bool World::CheckObjectIds() const
{
  ObjectIdFunctor func;
  return !ForEachVehicle(func);
}

void World::SimulatePostEffect(bool drawingEnable)
{
  if (!drawingEnable) { _ppeManager.Clear(); }

  AIBrain *unit = FocusOn();
  Transport *trans = unit ? unit->GetVehicleIn() : NULL;

  if (trans)
  {
    Person *person = unit->GetPerson();

    TurretContextV context;
    if (person && trans->FindTurret(unconst_cast(trans->RenderVisualState()), person, context))
    {
      AutoArray<PPEffectType> *postETypes = NULL;

      if (trans->GetPostProcessEffects(context, &postETypes) && postETypes)
      {
        _ppeManager.Update(*postETypes);
      }
      else
      {
        // disable effects - usually when camera change - no need to destroy effects
        _ppeManager.EnableEffects(false);
      }
    }
  }
  else
  {
    _ppeManager.Clear();
  }
}

static int CL(LODShape *shape, int level)
{
  return level>=0 && level!=LOD_INVISIBLE ? shape->CheckLevelLoadedDegree(level) : 0;
}

void World::SimulateRTD(bool doSim, float deltaT)
{
#if _HELISIM_ROTORLIB && !_DUMMY_ROTORLIB
  // Update the RotorLib world
  if (doSim)
  {
    _control87( _PC_53, MCW_PC );
    PROFILE_SCOPE_EX(rtSim, *);
    _rtdWorld.update(deltaT, true);
    _control87( _PC_24, MCW_PC );
  }
#endif
}

bool GUAGearActive = false;
void World::Simulate(float deltaT, bool &enableSceneDraw, bool &enableHUDDraw)
{
#if !defined _CONSOLE && _ENABLE_CHEATS
  static bool doIt = false;
  if (doIt)
  {
    doIt = false;
    //AddInternetRadio("http://amp.cesnet.cz:8000/cro2.ogg");

    //const char *music = "C:\\Users\\Bebul\\Documents\\DayZ\\missions\\test.SampleMap\\cro2.ogg";
    const char *music = "C:\\Users\\Bebul\\Documents\\DayZ\\missions\\test.SampleMap\\galaxy.ogg";
    AbstractWave *wave = GSoundScene->OpenAndPlayOnce(music, NULL, false, GetRealPlayer()->GetPosition(), GetRealPlayer()->WorldSpeed());
    if (wave)
    {
      wave->debugMe = true;
      GSoundScene->AddSound(wave);
    }
  }
#endif
  PROFILE_SCOPE_EX_HIER(wSimu,*);
  GMatrixCache.Clear();
  ClearClutterDrawCache();

  GInput.GetActionToDo(UAAltTab); //dummy action mapped to ALT+TAB which effectively disables the actions mapped to TAB key, when ALT-TABbing
  GUAGearActive = GInput.GetActionToDo(UAGear,false);

  FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
#if 0
  // Duplicate ObjectId Detection
  static int objIdCheckCount=0;
  if ( !(objIdCheckCount%100) && !CheckObjectIds() )
    Fail("Duplicate ObjectIds Detected!");
  objIdCheckCount++;
#endif

#if 0
  // diagnose the _remotePlayers. All clients should contain the same values!
  StaticArrayAuto< OLink(Person) > debugPl;
  ScanPlayers(debugPl);
  RString plinfo = "Players: ";
  for (int i=0; i<debugPl.Size(); i++)
  {
    NetworkId nid = debugPl[i]->GetNetworkId();
    plinfo = Format("%s (%d:%"OID_FORMAT") ", cc_cast(plinfo), nid.creator.CreatorInt(), nid.id);
  }
  DIAG_MESSAGE(1000, plinfo);
#endif

#if _ENABLE_CHEATS
  static bool doCheckNullBrains = false;
  if (doCheckNullBrains)
  {
    doCheckNullBrains = false;
    CheckNullBrains func;
    LogF("Checking Null Brains in Vehicles");
    ForEachVehicle(func);
    LogF("Checking Null Brains in OutVehicles");
    ForEachOutVehicle(func);
    LogF("SUMMARY: Checking Null Brains, %d culprits found", CheckNullBrains::_count);
  }

#if _VBS3_LARGE_OBJECTS && _ENABLE_CHEATS
  // Debugging opportunity
  static bool doAddLargeStaticObject = false;
  if (doAddLargeStaticObject)
  {
    doAddLargeStaticObject = false;
    static int intId = 2187;
    VisitorObjId id(intId);
    Object *obj = GLandscape->GetObject(id, GWorld->CameraOn()->FutureVisualState().Position()).GetLink();
    if (obj)
    {
      GLandscape->AddLargeStaticObjectToRTree(obj);
    }
  }
#endif 
#endif 
  
#if _ENABLE_CHEATS
  if (_cpuStress > 0)
  {
    PROFILE_SCOPE_EX(stres,*);
    SectionTimeHandle sectionTime = StartSectionTime();
    while (!CompareSectionTimeGE(sectionTime, _cpuStress)) {}
  }
#endif

	// Frame duration prediction
  int wantedVSyncDuration = 0;
  float maxVSyncEndTime = 0;
  float vsyncTime = 0;
  if (enableSceneDraw || enableHUDDraw)
  {
    if (GEngine->CanVSyncTime())
    {
      vsyncTime = 1.0f / GEngine->RefreshRate();
      deltaT = CalculateFrameDuration(wantedVSyncDuration, maxVSyncEndTime, vsyncTime);

      // assume the frame duration will be about 45 ms
      const float restFrame = 0.045f;
      // check if there is any more time available and distribute it between various activities
      float estimateReserve = (maxVSyncEndTime-GEngine->GetTimeVSync())*vsyncTime-restFrame;
      const float minReserve = 0.003f;
      if (estimateReserve<minReserve) estimateReserve = minReserve;
      GTimeManager.Frame(estimateReserve);
    }
    else
    {
      float smoothFrame = GEngine->GetSmoothFrameDuration();
      if (smoothFrame>0)
        // if time returned was zero, engine is not tracking averages
        deltaT = smoothFrame;
      
      // we can assume the frame will take approximately the same time as the previous one
      // we can see how much of it was time-managed

      float totalDuration = GTimeManager.GetFrameDuration();
      float timeManaged = GTimeManager.GetTimeSpentTotal();
      // we assume amount of unmanaged activities does not change a lot
      float unmanagedDuration = totalDuration-timeManaged;
      // the time spent in them should be approximately the same
      // avoid frames slower than 20 fps
      const float desiredDuration = 0.050f;
      const float minReserve = 0.003f;
      float estimateReserve = desiredDuration-unmanagedDuration;
      if (estimateReserve<minReserve)
        estimateReserve = minReserve;
      
#if _DEBUG
        // debugging - pretend almost no time left for preloading
        //static float estMax = 0.001f;
        static float estMax = 1.000f;
        if (estimateReserve>estMax)
          estimateReserve=estMax;
#endif

//       static float fixedDelta = 0.05f;
//       static float fixedTimeReserve = 1.0f;
//       deltaT = fixedDelta;
//       estimateReserve = fixedTimeReserve;
      
#if _ENABLE_CHEATS
      // If in video capturing mode, fix deltaT and time reserve
      if (_videoCaptureFrameID >= 0)
      {
        deltaT = _videoCaptureFrameDuration;
        estimateReserve = _videoCaptureTimeToReadResources;
      }
#endif

      // Specify the time reserve
      GTimeManager.Frame(estimateReserve);
    }
  }
  else
    GTimeManager.Frame(0.050f);
  
  float noAccDeltaT = deltaT;
  deltaT *= _acceleratedTime;
  // avoid too large time delta
#if _ENABLE_CHEATS
  static int tgtFPSMode=0;
  static const int tgtFPS[]={10,15,20,30};
  float maxDeltaT = (1.0f/tgtFPS[tgtFPSMode]) * floatMax(1.0f, _acceleratedTime);
  if (int changeTgt = GInput.GetCheatXToDo(CXTTargetFpsEmulate))
  {
    tgtFPSMode += changeTgt;
    if (tgtFPSMode<0) tgtFPSMode = lenof(tgtFPS)-1;
    if (tgtFPSMode>lenof(tgtFPS)-1) tgtFPSMode = 0;
    DIAG_MESSAGE(1000,Format("Emulate %d FPS CPU load",tgtFPS[tgtFPSMode]));
  }
  if (GInput.GetCheat1ToDo(DIK_SPACE))
  {
    _singleStepSimulation = true;
  }
  if (_singleStepSimulation)
  {
    if (!_enableSimulation)
    {
      _enableSimulation = true;
    }
    else
    {
      _enableSimulation = false;
      _singleStepSimulation = false;
    }
  }
#else
  float maxDeltaT = 0.100f;
#endif  
  // see also label MAX_DELTA_T
  // prevent "the worse, the worse" effect of simulation saturating itself
  // as we assume framerate should be stable 20Hz or 30 Hz, going below 10 Hz has no sense
  saturateMin(deltaT,maxDeltaT);

  
  bool wasSimEnabled = IsSimulationEnabled();
#if !_DISABLE_GUI
  int timeDelta = toInt(deltaT*1000); //should be set before possible setting deltaT=0
#endif

#if _VBS3
  float renderDeltaT = deltaT;
#endif
  if (!wasSimEnabled)
    deltaT = 0;
  //bool quiet = IsAppPaused();
  bool quiet = !IsAppFocused();

#if _ENABLE_PERFLOG && _ENABLE_CHEATS
  LastDeltaT = deltaT;
#endif

#if !_DISABLE_GUI
  if ( !IsDedicatedServer() && !IsDedicatedClient() ) // ProcessMouse should not be called on dedicated server console window dialog
  {
    // all vehicles are still left in their old positions
    extern bool CanProcessMouse(); //TODO: it is ugly
    extern bool GetMouseEx();
    extern void SynchronizeCursors();
    GInput.cursorMovedZ = 0;
    if (CanProcessMouse())
    {
      // ProcessMouse is called here because deltaT is needed
      GInput.ProcessMouse(timeDelta);
      if (!GetMouseEx() && GInput.mouseButtons[0] || GInput.mouseButtons[1] || GInput.mouseButtons[2] || GInput.mouseButtons[3])
        SynchronizeCursors();
    }
    else
    {
      // clear old mouse input
      GInput.MouseClearWhenCannotProcess();
    }
  }
#endif

  //_closedInput should be cleared before getting any input (SimulateDisplays read input in dialogs)
  GInput._closedInput.Clear();
  //if some action, which read rmbHold exclusively was read in last frame, rmbClick should be closed
  if (GInput.rmbClickClosed)
  {
    ClosedInputItem item(INPUT_DEVICE_MOUSE+MOUSE_CLICK_OFFSET+1, 1);
    GInput._closedInput.Add(item);
    GInput.rmbClickClosed = false;
  }
  GInput.frameID++;
  // give displays an opportunity to indicate game was paused
  _gameWasNotPaused = wasSimEnabled;
  SimulateDisplays(deltaT);

  // Free some memory
  FreeOnDemandGarbageCollect(mainHeapFreeRequired,systemFreeRequired);
  FreeOnDemandFrame();

#if _VBS2 // key bindings
	for (int i=0; i<_boundkeys.Size(); i++)
	{
    if(_boundkeys[i].isDik) continue;
    if (GInput.GetActionToDo((UserAction)_boundkeys[i].userKeyCode,false,false))
		{
			if (_boundkeys[i].command.GetLength() > 0)
			{
				bool alt = GInput.keys[DIK_LMENU]>0;
				bool shift = GInput.keys[DIK_LSHIFT]>0;

				GameState *state = GetGameState();
				GameVarSpace vars(false);
				int gshandle = state->BeginContext(&vars);
				state->VarSetLocal("_alt",alt,true);
				state->VarSetLocal("_shift",shift,true);
        if(state->EvaluateMultiple(_boundkeys[i].command, GameState::EvalContext::_default, GetMissionNamespace()))
        {
          GInput.actionDone[(UserAction)_boundkeys[i].userKeyCode] = true;
        }
				state->EndContext(gshandle);
			}
		}
	}
#endif
  ProcessCheats(noAccDeltaT);
  
  void CreateDSInterface();
  if (GetNetworkManager().GetClientState() >= NCSConnected && GInput.GetActionToDo(UADSInterface,true,false))  
    CreateDSInterface();
  
  bool clear=true; // until sky reflection problem is solved, do clear
  
  OLink(Entity) camVehicle=dyn_cast<Entity,Object>(_cameraOn);
  OLink(EntityAI) camAI=dyn_cast<EntityAI,Entity>(camVehicle);
  OLink(Person) person = FocusOn() ? FocusOn()->GetPerson() : NULL;

  bool doSim=IsSimulationEnabled();

/* combat mode diagnostics
if (_playerOn)
{
  AIUnit *unit = _playerOn->Brain();
  AIGroup *grp = unit ? unit->GetGroup() : NULL;
  if (grp)
  {
    CombatMode mode = unit->GetCombatMode();
    CombatMode modeMajor = unit->GetCombatModeMajor();
    CombatMode modeMinor = grp->GetCombatModeMinor();

    RString msg = Format
    (
      "Mode %s; Major %s; Minor %s",
      (const char *)FindEnumName(mode),
      (const char *)FindEnumName(modeMajor),
      (const char *)FindEnumName(modeMinor)
    );
    DIAG_MESSAGE(100, msg);
  }
}
*/

#if _HELISIM_ROTORLIB && ! _HELISIM_ROTORLIB_SIM_THREAD
  SimulateRTD(doSim, deltaT);
#endif

#if _VBS3 && _EXT_CTRL
  GMachineSessions.Tick();
#endif

#if LOG_FAST_UPDATES
  DWORD timeFU1 = GlobalTickCount();
#endif
  // need to be called several times during the frame
  GetNetworkManager().FastUpdate();

  // AI and vehicles simulation
  if (_ui)
    _ui->CamGroupActions(deltaT);

  // Executes network first, then simulate vehicles, this ensures proper updates of attached units / cameras
#if 1 // _VBS2  
  if (!_editor)
  {
    // Process network communication
    PROFILE_SCOPE_EX(netwT,*);
    ProcessNetwork();
  }
#endif

  EvalCameraPosition();

#if _HELISIM_ROTORLIB_SIM_THREAD
  // wait until RTDSim thread complete simul
  _rtdThread.IsSimulationDone();
#endif

  if (doSim)
    SimulateAllVehicles(deltaT);
  else
    EvalPostponedEvents();

  if (!_editor)
  {
    // give opportunity to scan additional inputs as needed
    GetNetworkManager().OnSimulateDone();
  }
#if _HELISIM_ROTORLIB_SIM_THREAD
  // run RTDSim thread simulation
  _rtdThread.SimulateOnce(doSim, deltaT);
#endif

#if _MOTIONCONTROLLER
  GMotionController.SetInMotion(doSim && !quiet);
  GMotionController.Update();
#endif

#if _VBS3 && _VBS_TRACKING
  GTracking.Update();
#endif

#if LOG_FAST_UPDATES
  DWORD timeFU2 = GlobalTickCount();
#endif
  // need to be called several times during the frame
  GetNetworkManager().FastUpdate();

  // Move simulation time
  Glob.uiTime += noAccDeltaT; 
  Glob.frameID++;
    
  if (IsSimulationEnabled())
    Glob.time += deltaT; // move simulation time

#if 0 // !_VBS2  
  if (!_editor)
  {
    // Process network communication
    PROFILE_SCOPE_EX(netwT,*);
    ProcessNetwork();
  }
#endif

  // Virtual camera
  bool wantNV = false;
  bool enableNV = false;
  bool wantFlir = false;
#if _VBS3
  // Tunnel vision coefficient to be passed to the engine
  float tunnelVisionCoef = 0.0f;
#endif

  //DrawAmbientTraffic();

  //if( !_showMap && !_noDisplay )
  {
    float timeLimit;
    if (_loadingScreen)
      timeLimit = 0.05f; // 20 fps
    else
    {
      float GetTimeForScripts();
      timeLimit = GetTimeForScripts();
    }
    SimulateScripts(deltaT, timeLimit);
   

    //TODO: find a some standard place for this
    ZombieAIScheduler(deltaT);
	//TODO: find a some standard place for this
	WorldResScheduler(deltaT);

    if( _cameraEffect && _cameraEffect->IsTerminated() )
    {
      _cameraEffect.Free();
      _playerSuspended = false;
    }

    if (_autoSaveRequest)
    {
      bool CanSaveGame();
      if (CanSaveGame())
      {
        // before saving handle any outstanding delete/move out requests to make sure the state is clean
        MoveOutAndDelete(_vehicles);
        MoveOutAndDelete(_animals);
        MoveOutAndDelete(_fastVehicles);
        MoveOutAndDelete(_slowVehicles);
        MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);

        if (_disableSavingRequest)
          // further saving will be disabled
          _savingEnabled = false;

        FreeOnDemandGarbageCollect(1024*1024,512*1024);
        GEngine->PrepareScreenshot();
        SaveGame(SGTAutosave);
        GEngine->CleanUpScreenshot();
      }
      _autoSaveRequest = false;
      _disableSavingRequest = false;
    }

    if (_loadAutoSaveRequest)
    {
      // we need to do more than just load the game, see DisplayMission::RetryMission()
      // LoadGame(SGTAutosave);

      // search for the DisplayMission
      ControlsContainer *ptr = dynamic_cast<ControlsContainer *>(Options());
      while (ptr)
      {
        if (ptr->IDD() == IDD_MISSION)
        {
          // found
          DisplayMission *disp = dynamic_cast<DisplayMission *>(ptr);
          disp->RetryMission();
          break;
        }
        ptr = ptr->Child();
      }

      _loadAutoSaveRequest = false;
    }

    SelectCameraType(deltaT);
    
    // Night vision
    if (_cameraEffect || !dyn_cast<EntityAI>(_cameraOn.GetLink()))
    {
      // in cutscenes
      wantNV = enableNV = _camUseNVG;
      wantFlir = _camUseTi;
      if (wantFlir && GEngine) GEngine->SetTIMode(_camTiIndex);
    }
    else
    {
      if (person)
      {
        wantNV = person->IsNVWanted();
        enableNV = person->IsNVEnabled();
        if (wantNV && enableNV)
        {
          // IsNVReady does preloading as well
          // do not call it unless NV is wanted
          enableNV = person->IsNVReady();
        }

        wantFlir = person->IsFlirWanted();
        person->ApplyVisionMode();
      }      
    }

    if (!IsSimulationEnabled() || !IsDisplayEnabled() || _showMap)
    {
      wantNV = false;
      wantFlir = false;
    }
  }

#if 1
  if ( !IsDedicatedClient() && !IsDedicatedServer() )
  {
  SimulatePostEffect(IsDisplayEnabled() && !wantFlir);
  }
#endif

#if _VBS3
  // Inform engine about the current tunnel vision
  GEngine->SetTunnelVisionCoef(tunnelVisionCoef);
#endif

  //show TI in buldozer
#if _ENABLE_BULDOZER & (_ENABLE_CHEATS | _BULDOZER)
  if(LandEditor)
    wantFlir = thermalCheat;
#endif

  wasSimEnabled = _gameWasNotPaused;

  Entity *camInsideVehicle = dyn_cast<Entity>(GetCamInsideVehicle());

  // Simulation
  {
    PROFILE_SCOPE_GRF_EX(wsSet,*,SCOPE_COLOR_BLUE);

#if _ENABLE_CHEATS
    if (GInput.GetCheat3ToDo(DIK_H))
    {
      extern int HeadBobCheatMode;
      static const char* headBobCheatModeNames[] = {
        /* 0 */ "Off",
        /* 1 */ "On - raw",
        /* 2 */ "On - begin/end alignment",
        /* 3 */ "On - average-frame correction",
        /* 4 */ "On - straight horizon",
        /* 5 */ "On - cut-scene mode",
        /* 6 */ "On - mode by animations with max strength",
        /* 7 */ "On - completely driven by animations (default)",
      };
      HeadBobCheatMode = (HeadBobCheatMode + 1) % lenof(headBobCheatModeNames);
      GlobalShowMessage(2000, "Head bob cheat mode %d: %s", HeadBobCheatMode, headBobCheatModeNames[HeadBobCheatMode]);
    }
#endif

    //Assert (GLandscape->VerifyStructure());
    Assert (CheckVehicleStructure());
    // this function does all simulation and drawing for single frame

  //  if( LandEditor ) _enableSimulation=true;
    
    
    UpdateAttachedPositions();  // moved here from SimulateAllVehicles (RenderVisualState must refer to the drawn frame)

    // time and weather changes
    SimulateLandscape(deltaT);
    
    // Update the temperature table
    SimulateTemperature(deltaT, wantFlir || thermalCheat);
    
    if (IsSimulationEnabled())
    {
       PROFILE_SCOPE_EX(simAm,*);
      _ambientLife->Simulate(*_scene.GetCamera(),deltaT);
    }
    //if( !_cameraOn ) BrowseCamera(+1);
    //bool insideVehicle=false;

#ifdef _WIN32
    /**/
    if (GJoystickDevices)
    {
      bool vibrations = GInput.scheme.vibrations;

      // perform FF effects on sticks
      if (vibrations && camVehicle /* may have been destroyed in SimulateAllVehicles */ && IsUIEnabled() && !_cameraEffect)
      {
        FFEffects eff;
        camVehicle->PerformFF(eff);
        GJoystickDevices->SetEngine(eff.engineMag,eff.engineFreq);
        GJoystickDevices->SetStiffness(eff.stiffnessX,eff.stiffnessY);
        GJoystickDevices->SetSurface(eff.surfaceMag);
        GJoystickDevices->FFOn();
      }
      else
        GJoystickDevices->FFOff();
      //if( eff.gunCount>0 )
      //{
      //  GJoystickDevices->PlayGun(eff.gunMag*0.2,-eff.gunMag,eff.gunFreq,eff.gunCount);
      //}
    }
    if (GInput.IsXInputPresent())
    {
      bool vibrations = GInput.scheme.vibrations;

      // perform FF effects on sticks
      if (vibrations && camVehicle && IsUIEnabled() && !_cameraEffect)
      {
        FFEffects eff;
        camVehicle->PerformFF(eff);
        XInputDev->SetEngine(eff.engineMag,eff.engineFreq);
        XInputDev->SetStiffness(eff.stiffnessX,eff.stiffnessY);
        XInputDev->SetSurface(eff.surfaceMag);
        XInputDev->FFOn();
      }
      else
        XInputDev->FFOff();
    }
    else if (XInputDev) XInputDev->FFOff();
#endif

    //PROFILE_SCOPE_EX(simUI,*);
    //SimulateUI(deltaT);
  }
  

  if (CHECK_DIAG(DEModel)) // show model diags
  {
    Object *obj = GetCameraFocusObject();
    if (obj)
    {
      LODShape *shape = obj->GetShape();
      if (shape)
      {
        RString modelName = Format(
          "%d: %s",
          safe_cast<int>(obj->ID()), cc_cast(shape->GetName())
        );
        const EntityType *type = obj->GetEntityType();
        if (type)
        {
          modelName = modelName + Format(" (type=%s)",cc_cast(type->GetName()));
        }
        if (shape->GetPropertyClass().GetLength()>0)
        {
          modelName = modelName + Format(" (class=%s)",cc_cast(shape->GetPropertyClass()));
        }
        DIAG_MESSAGE(100,modelName);
        const SortObject *so = obj->GetInList(
#if _HELISIM_RENDER_TO_TEXTURE
          _scene.GetDrawObjectIndex()
#endif
          );
        // because of instancing another object may be owning the sortobject for us
        if (!so)
        {
          so = _scene.FindSortObject(obj
#if _HELISIM_RENDER_TO_TEXTURE
            , _scene.GetDrawObjectIndex()
#endif
            );
          
        }
        if (so)
        {
          DIAG_MESSAGE(100,
            "   dist %.2f, cArea %g, lod %d:%d (%d:%d,%d:%d, %.2f), shadow %d:%d, root %s",
            sqrt(so->distance2),
            so->drawLOD,CL(so->shape,so->drawLOD),
            so->srcLOD,CL(so->shape,so->srcLOD),so->dstLOD,CL(so->shape,so->dstLOD),so->blendFactor,
            so->shadowLOD,CL(so->shape,so->shadowLOD),
            cc_cast(so->GetObject()->GetDebugName())
          );
          //LogF(
          //  "   dist %.2f, cArea %g, lod %d (%d,%d, %.2f), shadow %d, root %s",
          //  sqrt(so->distance2),so->coveredArea,so->drawLOD,so->srcLOD,so->dstLOD,so->blendFactor,
          //  so->shadowLOD,cc_cast(so->GetObject()->GetDebugName())
          //);
        }
      }
    }
  }
  
	//recalculate cloud occlusion just once per frame, this sets the flag in Landscape
	//that it's necessary to recalculate it
	if (_scene.GetLandscape())
		_scene.GetLandscape()->RecalculateCloudOcclusion();

	// we need the lighting to be setup during eyeAccom estimations
  _scene.RecalculateLighting(1,_scene.GetLandscape());

#if LOG_FAST_UPDATES
  DWORD timeFU3 = timeFU2;
  DWORD timeFU4 = timeFU2;
  DWORD timeFU5 = timeFU2;
#endif

  // callback which will be used before rendering is waiting for its background thread to complete
  UseWaitingTime doNotUseTime;
  // by default there is no functionality
  UseWaitingTime *useTime = &doNotUseTime;
  
  /// AI and sound processing
  class UseWaitingTimeForAIAndSound: public UseWaitingTime
  {
    World *_world;
    float _deltaT;
    float _noAccDeltaT;
    Entity *_camInsideVehicle;
    bool _quiet;
    bool _doSim;

    public:      
    UseWaitingTimeForAIAndSound(World *world, float deltaT, float noAccDeltaT, Entity *camInsideVehicle, bool quiet, bool doSim)
    :_world(world),_deltaT(deltaT),_noAccDeltaT(noAccDeltaT),_camInsideVehicle(camInsideVehicle),_quiet(quiet),_doSim(doSim)
    {}
    
    void AI() const
    {
      if (_doSim)
      {
        PROFILE_SCOPE_EX(perAI,*);
        _world->PerformAI(_deltaT);
      }
    }
    void Sound() const
    {
      {
        PROFILE_SCOPE_GRF_EX(sound,*,SCOPE_COLOR_GRAY);
        // while it may seem doSim and simEnabled are the same, they may differ
        // the result of IsSimulationEnabled may have changed meanwhile
        bool simEnabled = _world->IsSimulationEnabled();
        if( simEnabled )
          _world->PerformSound(_camInsideVehicle,_deltaT);

        GSoundScene->AdvanceAll(_noAccDeltaT,!simEnabled,_quiet); // sort and activate sounds
        GetNetworkManager().AdvanceAllVoNSounds(_deltaT,!simEnabled,_quiet); //sort and activate voices
        GSoundsys->UpdateRadioSubmixes(!IsAppPaused() && simEnabled);
        
        if ( GSoundsys )
          GSoundsys->Commit(); // commit deferred settings
      }
    }
    void Visibility() const
    {
      if(_doSim)
        _world->GetSensorList()->SmartUpdateAll();
    }
    virtual void operator()() const
    {
      AI();
      Sound();
      Visibility();
    }
  } useTimeForAIAndSound(this,deltaT,noAccDeltaT,camInsideVehicle,quiet,doSim);

  // when using PIX on old Xbox it was sometimes necessary to draw the same frame twice
  const bool frameRepeated = false;
  
  GEngine->FinishPresent();

  if (GEngine->PresentIsAsync())
  {
    useTimeForAIAndSound.AI();
    useTimeForAIAndSound.Sound();
  }
  
  if (IsDisplayEnabled())
    AdjustEyeAccom(wantNV
#if _HELISIM_RENDER_TO_TEXTURE
    , false
#endif
    );

#if ANIMATION_RT_SLIST
  // prior drawing, load all needed animations
  PerformAnimationRTLoading();
#endif

  AbilityToDraw isAbleToDraw = ATDAble;
  bool wantReset = false;
  if (enableSceneDraw || enableHUDDraw)
  {
    wantReset = IsDisplayEnabled();
    isAbleToDraw = GEngine->IsAbleToDraw(wantReset);
    #if _PROFILE
    GEngine->SetDebugMarker(isAbleToDraw,"IsAbleToDraw");
    #endif
  }
  if (GInput.CheatActivated() == CheatFlush)
  {
    GInput.CheatServed();
    // we want to force flushing all video memory and all vertex buffers
    GEngine->FlushMemory(true);
    isAbleToDraw = ATDAbleReset;
  }
  switch (isAbleToDraw)
  {
  case ATDUnable:
    enableHUDDraw = enableSceneDraw = false;
    break;
  case ATDAbleReset:
    if (!LandEditor)
    {
      PreloadAroundCameraSync(15);
      // check if device is still OK
      isAbleToDraw = GEngine->IsAbleToDraw(wantReset);
      // if another reset was done, ignore it and do not preload again
      if (isAbleToDraw==ATDUnable)
        enableHUDDraw = enableSceneDraw = false;
    }
    break;
  }

	if (!enableSceneDraw)
	{
		GScene->ClearEnfShapeCollection(true);
	}

  PauseVideoBuffers(!doSim/* && !isAbleToDraw*/);

  // during simulation, camera vehicle can change (even be destroyed)
  camInsideVehicle = dyn_cast<Entity>(GetCamInsideVehicle());
  RString camInsideVehicleName = camInsideVehicle ? camInsideVehicle->GetDebugName() : RString();

  // sometimes we want the adaptation to be fixed at certain level  
  GEngine->SetNightVision(wantNV,wantFlir,_eyeAccom,_customEyeAccom>0 || EyeGainFixed(wantNV));
  _scene.RecalculateLighting(1,_scene.GetLandscape());

  // must be done after simulation, because camera position needs to match target position
  SimulateCamera(deltaT);

  DoAssert(camInsideVehicle==dyn_cast<Entity>(GetCamInsideVehicle()));

  // float inWater = SelectCameraPosition(GEngine, deltaT);
  //// must be done after simulation, because camera position needs to match target position
  //SimulateCamera(deltaT);
  //float inWater = SelectCameraPosition(GEngine, deltaT);
#ifdef TRAP_FOR_0x8000
  //GEngine->TrapFor0x8000("World::Simulate C");
#endif
  {
    PROFILE_SCOPE_EX(rendr,*);

    // If scene is to be drawn, do the preparation and render DB and SB
    bool rendering = false;

    // Context used for scene drawing and preparation
    Scene::DrawContext drawContext;
    GridRectangle groundPrimed;

    if (enableSceneDraw && _scene.GetLandscape() && IsDisplayEnabled() && GWorld->IsSceneReady())
    {
      rendering = true;

      if(camInsideVehicle!=dyn_cast<Entity>(GetCamInsideVehicle()))
      {
        camInsideVehicle=dyn_cast<Entity>(GetCamInsideVehicle());
        LogF("Camera vehicle '%s' disappeared unexpectedly, now '%s'",cc_cast(camInsideVehicleName), camInsideVehicle ? cc_cast(camInsideVehicle->GetDebugName()) : "");
      }
      GEngine->FinishFinishPresent(); // texture loading is finished here, might have cause a device reset when unlucky

#if _HELISIM_RENDER_TO_TEXTURE

      {
        Transport *trans = FocusOn() ? FocusOn()->GetVehicleIn() : NULL;

        if (trans)
        {
          if (_r2tLastTransport != trans)
          {
            // release
            _r2tTransportTgts.Resize(0);

            if (trans->HasR2TTarget())
            {
              const RefArray<R2TTgtPars> &targets = trans->AccessR2TTargets();

              for (int i = 0; i < targets.Size(); i++)
              {
                const Ref<R2TTgtPars> &target = targets[i];
                target->Reset();

                // check if render target is free
                if (!IsR2TSrcCamUsed(target->Name()))
                {
                  R2TCamera &r2tCam = _r2tSourceCamera.Append();
                  r2tCam.SetDefault(CreateCameraEffect(trans, "internal", CamEffectBack), i);
                  //r2tCam._camEffect = CreateCameraEffect(trans, "internal", CamEffectBack);
                  // connection camera effect - target
                  //r2tCam._camEffect->SetAssignedR2TTgtIndex(i);
                  r2tCam._tgtPars = target;

                  // remember targets used for transport
                  R2TTransportTgt &tTgt = _r2tTransportTgts.Append();
                  tTgt._targetName = target->Name();

                  _r2tLastTransport = trans;
                }
              }
            }
          }
        }
        else
        {
          _r2tLastTransport = NULL;

          // release all current r2t cameras
          _r2tTransportTgts.Resize(0);
        }
      }

      DrawSceneR2T(camInsideVehicle, frameRepeated, deltaT);

#endif

      // sometimes we want the adaptation to be fixed at certain level  
      GEngine->SetNightVision(wantNV,wantFlir,_eyeAccom,_customEyeAccom>0 || EyeGainFixed(wantNV));

      // must be done after simulation, because camera position needs to match target position
      SimulateCamera(deltaT);

      if(camInsideVehicle!=dyn_cast<Entity>(GetCamInsideVehicle()))
      {
        camInsideVehicle=dyn_cast<Entity>(GetCamInsideVehicle());
        LogF("Camera vehicle '%s' disappeared unexpectedly, now '%s'",cc_cast(camInsideVehicleName), camInsideVehicle ? cc_cast(camInsideVehicle->GetDebugName()) : "");
      }

      float inWater = SelectCameraPosition(GEngine, deltaT);
      DrawAmbientTraffic();

      if(camInsideVehicle!=dyn_cast<Entity>(GetCamInsideVehicle()))
      {
        camInsideVehicle=dyn_cast<Entity>(GetCamInsideVehicle());
        LogF("Camera vehicle '%s' disappeared unexpectedly, now '%s'",cc_cast(camInsideVehicleName), camInsideVehicle ? cc_cast(camInsideVehicle->GetDebugName()) : "");
      }

      // Prepare list of scene objects for drawing
      PrepareDraw(camInsideVehicle,false,FLT_MAX);

      _scene.RecalculateLighting(1,_scene.GetLandscape());

      if (inWater == 0)
        // Set default DOF
        GEngine->EnableDepthOfField(8.0f, 1.0f, true);
      else
        GEngine->EnableDepthOfField(2.0f, 1.0f, false);

      // Selects reasonable number of lights for the scene
      if( camInsideVehicle ) _scene.SelectActiveLights(camInsideVehicle);
      else _scene.SelectActiveLights(NULL);

      // Fill out the light grid with lights
      _scene.SelectLightsIntoGrid();

      GWorld->BeginWindEmitterScope(_scene.GetCamera(),Glob.config.horizontZ);

      // Preparation to be done prior landscape drawing
      _scene.GetLandscape()->DrawRectPrepare(drawContext, _scene, deltaT, frameRepeated);

      // render the depth buffer (may perform priming as well, unless separate priming is required)
      _scene.GetLandscape()->PrimeDepthBuffer(groundPrimed, _scene, drawContext, false, frameRepeated 
#if _HELISIM_RENDER_TO_TEXTURE 
        , 0
#endif
        , true /*debugMe*/);

      // Render the shadow buffer
      _scene.GetLandscape()->DrawRectShadowBuffer(drawContext, _scene);
      if (GEngine->GetFSAA()>0)
      {
        // PC with FSAA required separate depth priming
        _scene.GetLandscape()->PrimeDepthBuffer(groundPrimed, _scene, drawContext, true, frameRepeated
#if _HELISIM_RENDER_TO_TEXTURE
          , 0
#endif
          );
      }
    }
    else
    {
      // even when not rendering keep camera position ready (used for ambient life, sounds, and also reported to the server in MP)
      SelectCameraPosition(GEngine, deltaT);
    }

    {
      //SelectCameraPosition must be called before UI simulation (precise actions needs precise camera position)
      //not very nice place to simulate UI, but SelectCameraPosition takes 1ms
      PROFILE_SCOPE_EX(simUI,*);
      SimulateUI(deltaT);
    }

    // during handling of _drawObjects, camera vehicle can change (even be destroyed)
    camInsideVehicle = dyn_cast<Entity>(GetCamInsideVehicle());

#ifdef TRAP_FOR_0x8000
    //GEngine->TrapFor0x8000("World::Simulate D");
#endif

    if (enableSceneDraw || enableHUDDraw)
    {
      PROFILE_SCOPE(drwIn);
      // clear with fog color - in case we are able to see through
      GEngine->InitDraw(clear, GEngine->FogColor());
    }
    else
      GEngine->InitNoDraw();

#if LOG_FAST_UPDATES
    timeFU3 = GlobalTickCount();
#endif
    // need to be called several times during the frame
    GetNetworkManager().FastUpdate();

#if _VBS3
    // in case deltaT is zero, we still want to 
    // perform post processing on the scene. This requires a deltaT 
    // stored in renderDeltaT
    float previousDT = deltaT;
    deltaT = renderDeltaT;
#endif

#if _VBS3_CRATERS_LATEINIT
    // Process some items from the late initialization manager
    _lateInitManager.LateInit();
#endif

    if (!GEngine->PresentIsAsync())
      // when Present and 2D rendering requires flush, we want to use the time before flushing
      useTime = &useTimeForAIAndSound;

    DrawScene(drawContext, groundPrimed, enableSceneDraw, camInsideVehicle, frameRepeated, wantNV, wantFlir, frameRepeated ? 0 : deltaT, *useTime);

    if (rendering)
      GWorld->EndWindEmitterScope();

#if _VBS3
    // Restore the old value of deltaT
    deltaT = previousDT;
#endif

#if LOG_FAST_UPDATES
    timeFU4 = GlobalTickCount();
#endif
    // need to be called several times during the frame
    GetNetworkManager().FastUpdate();

    // draw HUD
    if (enableHUDDraw)
      DrawUI(wantNV, wantFlir);


#if LOG_FAST_UPDATES
    timeFU5 = GlobalTickCount();
#endif
    // need to be called several times during the frame
    GetNetworkManager().FastUpdate();

    if (enableSceneDraw || enableHUDDraw)
    {
      PROFILE_SCOPE_EX(drwFn,*);

      // If we are in video capturing mode, fill out the output frame file name
      RString frameFileName;
#if _ENABLE_CHEATS
      if (_videoCaptureFrameID >= 0)
      {
        BString<256> frameNum;
        sprintf(frameNum, "%04d", _videoCaptureFrameID);
        frameFileName = _videoCaptureFolderName + RStringB("\\") + _videoCaptureFilePrefix + RString(frameNum);
        _videoCaptureFrameID++;
      }
      else
#endif
        frameFileName = RString();

      bool firstFrame = doSim && !wasSimEnabled;
      bool vsyncTiming = GEngine->CanVSyncTime();
      if (vsyncTiming)
      {
        const float restFrame = 0.002f;
        float displayReserve = (maxVSyncEndTime-GEngine->GetTimeVSync())*vsyncTime-restFrame;

        DrawFrameDuration(displayReserve);

        GEngine->FinishDraw(wantedVSyncDuration, firstFrame, frameFileName);
      }
      else
      {
        DrawFrameDuration(0);
        GEngine->FinishDraw(1, firstFrame, frameFileName);
      }
      if (firstFrame)
      {
        PROFILE_SCOPE(drwFF);
        // the purpose of the following frame rendering is to make CPU wait for GPU
        // if not performing v-sync based timing this has no sense
        if (vsyncTiming)
        {
          // using previous back buffer content copy might be tempting
          // however HDR would need to be used here
          for (int i=0; i<3; i++)
          {
            int wantedDuration;
            float maxEndTime,skippedT;
            skippedT = CalculateFrameDuration(wantedDuration,maxEndTime,vsyncTime);
            (void)skippedT;
            //LogF("Skipped t %g",skippedT);
            GEngine->InitDraw(true);
            GEngine->FinishDraw(wantedDuration, false, frameFileName);
          }
        }
#if _ENABLE_PERFLOG
        void PerfLogReset();
        PerfLogReset();
#endif
        GEngine->ResetTimingHistory(50);
        GInput.InitMouseVariables(); //there was problem, after ESC and click to continue button, soldier fired
        LogF("* Starting simulation");
      }
    }
    else
      // must be called to make sure frame rate average is maintained correctly
      // otherwise bandwidth estimation function go crazy
      GEngine->FinishNoDraw(1);
    
    GEngine->NextFrame(enableSceneDraw || enableHUDDraw);

    DoAssert(drawContext._oPasO.finalCopy);
    DoAssert(drawContext._oPasA.finalCopy);
    DoAssert(drawContext._oPas3.finalCopy);
    DoAssert(drawContext._oPas4.finalCopy);
    DoAssert(drawContext._lDGnd.finalCopy);
    DoAssert(drawContext._lDGCl.finalCopy || drawContext._lDGCl.IsEmpty());
  }
  
#if LOG_FAST_UPDATES
  DWORD timeFU6 = GlobalTickCount();
#endif
  // need to be called several times during the frame
  GetNetworkManager().FastUpdate();

  if (GEngine->PresentIsAsync())
    // doing visibility last is good for load balancing
    useTimeForAIAndSound.Visibility();
  

  #if 0 // _PROFILE && USER_Ondra
    GLandscape->Visible(0,Vector3(12022.1768,164.057617,12659.9990),Vector3(11970.3613,163.864929,12659.8691),0.371325195,nullptr,nullptr);
  #endif

  if (GLandscape && enableSceneDraw && IsPreloadEnabled())
  {
    float reserveLeft = 0.050f;
    if (_showMapProgress || _preloadAsyncCtx) reserveLeft = 0.150f;
    if (GEngine->CanVSyncTime())
      reserveLeft = (maxVSyncEndTime-GEngine->GetTimeVSync())*vsyncTime;
    PreloadData(reserveLeft);
  }

  // bank hash checking
  BankList::ReadAccess banks(GFileBanks);
  if (!_checkingHash && _checkHashQueue.Size() > 0)
  {
    int index = _checkHashQueue[0]._bankIndex;
    int m = banks.Size();
    // TODO: update when Addon Management changes 
    if (index < m)
      _checkingHash = new BankHashCalculatorAsync(index);
    else
    {
      LogF("Bank index out of range");
      _checkHashQueue.DeleteAt(0);
    }
  }
  if (_checkingHash)
  {
    if (_checkingHash->Process())
    {
      // TODO: protect _checkingHash and _checkHashQueue against manipulation by hacker
      DoAssert(_checkHashQueue.Size() > 0);
      if (!_checkingHash->IsValid())
      {
#if SIGNATURES_TEST_ONLY
        LogF("Warning: Hash of %s is wrong.", cc_cast(banks[_checkHashQueue[0]._bankIndex].GetOpenName()));
#else
        switch (_checkHashQueue[0]._reason)
        {
        case CHRMultiplayer:
          GetNetworkManager().HackedDataDetected(banks[_checkHashQueue[0]._bankIndex].GetOpenName());
          break;
        case CHRDemo:
          ErrorMessage(LocalizeString(IDS_HASH_WRONG));
          break;
        }
#endif
      }
      else
        LogF("Hash of %s checked.", cc_cast(banks[_checkHashQueue[0]._bankIndex].GetOpenName()));
      _checkingHash = NULL;
      _checkHashQueue.DeleteAt(0);
    }
  }
  
  UpdatePerfLog();

#if _ENABLE_CHEATS && COUNT_CLASS_INSTANCES
    if (GInput.GetCheat3ToDo(DIK_7))
    {
      int nEntityAI = _vehicles.VehicleCount()+_slowVehicles.VehicleCount();
      int nTargets = TargetNormal::GetInstanceCount();
      int nTargetLists = TargetList::GetInstanceCount();
      LogF("TargetNormal: %d, TargetList: %d",nTargets,nTargetLists);
      LogF("EntityAI: %d",nEntityAI);
      LogF("Total: %d",nEntityAI*nTargetLists);
      LogF("Complement: %d",nEntityAI*nTargetLists-nTargets);
    }
#endif

#if LOG_FAST_UPDATES
    static DWORD timeFUEnd = 0;
    DWORD timeFULast = GlobalTickCount();
    LogF("Fast update times: %d + %d + %d + %d + %d + %d = %d",
      (timeFU1 - timeFUEnd) + (timeFULast - timeFU6), // out of FastUpdate calls
      timeFU2 - timeFU1, timeFU3 - timeFU2, timeFU4 - timeFU3,
      timeFU5 - timeFU4, timeFU6 - timeFU5, // between FastUpdate calls
      timeFULast - timeFUEnd // total
      );
    timeFUEnd = timeFULast;
#endif
}

void World::EvalCameraPosition()
{
  // check if player is inside vehicle
  _isCameraInsideVeh = false;

  if (GetCameraEffect()) { return; }

  Transport *transport = FocusOn() ? FocusOn()->GetVehicleIn() : NULL;
  if (transport && transport->EnableOutsideSoundFilter()) 
  {   
    _isCameraInsideVeh = transport->GetCamPos() < 0.49f;
  }
}

void World::DrawFrameDuration(float displayReserve)
{
  //float vCurr = GEngine->GetTimeVSync();
  //LogF("Vsync time %.4f",vCurr);
  // estimate which vsync we would like to target

#if _ENABLE_CHEATS && _ENABLE_REPORT
  if( GEngine->ShowFpsMode()>=1)
  {
    const float timeScale = 1/0.030f; // inv. max time in seconds
    if (displayReserve>0)
    {
      // draw file queue status
      float status = displayReserve*timeScale;
      if (status>1) status = 1;

      Color emptyColor(1,0,0,0.2);
      Color fullColor(0,1,0,0.2);
      Color color = fullColor*status+emptyColor*(1-status);
      Rect2DFloat rect;
      rect.x = 0.5;
      rect.w = (0.95-rect.x)*status;
      rect.h = 0.04;
      rect.y = 0.00;
      Rect2DAbs rect2D;
      GEngine->Convert(rect2D,rect);
      MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      GEngine->Draw2D(mip,PackedColor(color),rect2D);
      Point2DAbs pos(rect2D.x,rect2D.y);

      Font *font=GEngine->GetDebugFont();

      float textSize = 0.02;
  
      PackedColor colorText(Color(0.2,0.2,0.2,0.8));
      //PackedColor colorBold(Color(1,0.3,0.2,0.9));
      //TextDrawAttr attrShadow(size,font,PackedBlack,true);
      TextDrawAttr attrText(textSize,font,colorText,true);
      //TextDrawAttr attrBold(size,font,colorBold,true);

      GEngine->DrawTextF(pos,attrText,"%3d ms",toInt(displayReserve*1000));
    }
    // show time distribution
    PackedColor colorRed(255,0,0,64);
    PackedColor colorGreen(0,255,0,64);
    PackedColor colorBlue(0,0,255,64);
    PackedColor colorYellow(255,255,0,64);
    PackedColor colorPink(200,0,255,64);
    PackedColor colorCyan(0,255,255,64);
    PackedColor colorGray(64,64,64,64);
    #define TC_COLOR(name,prior1,prior2,colorName) color##colorName,
    static const PackedColor colors[NTimeClass]= { TIME_CLASS(TC_COLOR) };

    MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    Rect2DFloat rect;
    Rect2DAbs rect2D;
    float xxRes = 0;
    float xxUse = 0;
    const float xRect = 0.5f;
    const float wRect = 0.45f;
    const float yRes = 0.05f;
    const float yUse = 0.08f;
    const float hRect = 0.02f;
    for (int i=0; i<NTimeClass; i++)
    {
      float timeRes = GTimeManager.GetTimeReservedLastFrame(TimeClass(i))*timeScale;
      float timeUse = GTimeManager.GetTimeSpentLastFrame(TimeClass(i))*timeScale;
      float wRes = floatMin(1-xxRes,timeRes);
      float wUse = floatMin(1-xxUse,timeUse);
      
      rect.h = hRect;
      rect.y = yRes;
      rect.x = xRect+xxRes*wRect;
      rect.w = wRes*wRect;
      
      GEngine->Convert(rect2D,rect);
      GEngine->Draw2D(mip,colors[i],rect2D);
      
      rect.h = hRect;
      rect.y = yUse;
      rect.x = xRect+xxUse*wRect;
      rect.w = wUse*wRect;
      
      GEngine->Convert(rect2D,rect);
      GEngine->Draw2D(mip,colors[i],rect2D);
      
      xxRes += wRes;
      xxUse += wUse;
    }
    {
      // draw additional time available
      float timeRes = GTimeManager.GetAddTimeLastFrame()*timeScale;
      float wRes = floatMin(1-xxRes,timeRes);
      
      rect.h = hRect;
      rect.y = yRes;
      rect.x = xRect+xxRes*wRect;
      rect.w = wRes*wRect;
      
      GEngine->Convert(rect2D,rect);
      GEngine->Draw2D(mip,colorGray,rect2D);
    }

  }
#endif // 0
}

void World::UpdatePerfLog()
{
  #if _ENABLE_PERFLOG
  bool enable= true; /* ( IsSimulationEnabled()*/  /*&& IsDisplayEnabled()*/;
  GPerfCounters.Enable(enable);
  GPerfProfilers.Enable(enable);
  #endif
}


/*
void World::EnableDisplay( bool val )
{
  _noDisplay=!val;
  UpdatePerfLog();
}
void World::EnableSimulation( bool val )
{
  _enableSimulation=val;
  if (!val)
  {
    UnloadSounds();
  }
  UpdatePerfLog();
}
*/

bool World::IsPreloadEnabled() const
{
  // similar to IsDisplayEnabled, but 
  if (LandEditor)
    return true;
  if (_noDisplay)
    return false;
  if (_showMap && _map && !_map->IsDisplayEnabled())
    return false;
  if (ProgressIsActive())
    return false;
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && !display->IsDisplayEnabled())
      return false;
  }
  if (!_options)
    return false;
  return _options->IsDisplayEnabled();
}

bool World::IsDisplayEnabled() const
{
  if (LandEditor)
    return true;

  if (_loadingScreen)
    return false;
  
  bool ret = IsPreloadEnabled();
  if (!ret)
    return ret;
  if (_showMap)
  {
    if (_map && !_map->IsDisplayEnabled())
      return false;
    return true;
  }
  // avoid rendering while async preloading
  if (_showMapProgress)
    return false;
  return true;
}

bool World::IsSimulationEnabled() const
{
#if _VBS3
  extern bool ForceSimulation;
  if(_pauseSimulation)
    return false;
#endif

  if (LandEditor)
    return true;
  if (_mode == GModeNetware)
  {
    return GetNetworkManager().GetServerState() == NSSPlaying && GetNetworkManager().GetClientState() == NCSPlaying;
  }
  else
  {
    if (_loadingScreen)
      return false;
    if (_warningMessage)
      return false;
#ifdef _WIN32
    if (_jUI && _jUI->IsVisible())
      return false;
#endif
    if (!_enableSimulation)
      return false;
    if (IsAppPaused()
#if _VBS3
        && !ForceSimulation
#endif
        ) return false;
    // TODO: remove _simulationFocus
    //if (!_simulationFocus) return false;
    if (_userDlg && !_userDlg->IsSimulationEnabled())
      return false;
    for (int i=0; i<_displays.Size(); i++)
    {
      AbstractOptionsUI *display = _displays[i]._display;
      if (display && !display->IsSimulationEnabled())
        return false;
    }
    if (!_options)
      return false;
    return _options->IsSimulationEnabled();
  }
}

bool World::IsExitByKeyEnabled() const
{
#ifdef BULDOZER 
  return true;
#endif
  // allow Alt+F4 in buldozer
  if (LandEditor) return true;
  // whenever the game is paused, we allow Alt-F4
  if (!IsSimulationEnabled()) return true;
  // whenever we have a system cursor, we allow Alt-F4
  if (!GEngine || GEngine->IsCursorShown()) return true;
  // when user input is disabled, user cannot pause the game, and he is unlikely to press Alt-F4 by mistake - allow it
  if (_userInputDisabled) return true;
  return false;
}



void World::ShowMapForced(bool state, bool forced)
{
  if (_map) _map->ResetHUD();

  _showMapforced = forced;
  ShowMap(state);
}

void World::ShowMap(bool state)
{
#if _VBS2 && !_VBS2_LITE
#if _VBS3 // ShowArmAMap difficulty setting
  if (!Glob.config.IsEnabled(DTShowArmAMap))
#endif
  {
  if (state && !UserDialog())
  {
#if _VBS3 // don't show You Are Dead if admin
      if (!Glob.vbsAdminMode)
      {
        if (_playerOn && _playerOn->IsDamageDestroyed())
          return;
      }
#endif
    Display *editor = CreateMissionEditorRealTime(NULL);
    SetUserDialog(editor);
    DisplayMap *map = static_cast<DisplayMap *>(editor);    
    map->ResetHUD();
  }
  return;
  }
#endif
  if (!state) _mapItem = nullptr;

  if (IsShowMapWanted() == state)
    return;

  if (IsShowMapForced() == true && state == false)
    return;

  if (state)
  {
    GChatList.Load(Pars >> "RscChatListMap");
    GetNetworkManager().SetPendingInvitationPos("RscPendingInvitation");
/*
    // map preload did not lead to time savings
    if (_map)
    {
      if (_mode != GModeNetware)
      {
        // preload map
        Ref<ProgressHandle> p = ProgressStartExt(false, LocalizeString(IDS_LOAD_WORLD));
        while (!_map->IsDisplayReady())
        {
          float maxIterTime = 0.02f;

          FreeOnDemandGarbageCollect(mainHeapFreeRequired,systemFreeRequired);
          GTimeManager.Frame(maxIterTime);
          GFileServerUpdate();
          ProgressRefresh();
        }
        ProgressFinish(p);
      }
    }
*/
    _showMapUntil = 0;
    _showMap = true;
    SetShowMapProgress(NULL);
#if _ENABLE_PERFLOG
    LogF("<map>");
#endif
  }
  else
  {
    if (_map)
      _map->OnHide();
    GChatList.Load(Pars >> "RscChatListMission");
    GetNetworkManager().SetPendingInvitationPos("RscPendingInvitationInGame");
    if (_mode == GModeNetware)
    {
      int timeMs = 1000;
      _showMapUntil = GlobalTickCount() + timeMs;
      IProgressDisplay *CreateDisplayProgress(RString text);
      SetShowMapProgress(CreateDisplayProgress(LocalizeString(IDS_LOAD_WORLD)));
      _preloadAsyncCtx = new PreloadAsyncContext(timeMs*0.001f, false);
    }
    else
    {
      // on PC we prefer quick response, even when some visual artifacts might appear
      // we are doing some basic preload while in the map anyway
      if (_map && GLandscape)
      {
        PreloadAroundCameraSync(1.0f);
        // disruptive event,  give opportunity to flush
        GEngine->RequestFlushMemory();
      }
      _showMapUntil = 0;
      _showMap = false;
      SetShowMapProgress(NULL);
      _gameWasNotPaused = false;
    }
#if _ENABLE_PERFLOG
    LogF("</map>");
#endif
  }
}

void World::ShowWatch(InventoryItem *item)
{
  if (_watch == item) _watch = NULL; // hide
  else _watch = item; // show
}

void World::ShowCompass(InventoryItem *item)
{
  if (_compass == item) _compass = NULL; // hide
  else _compass = item; // show
}

void World::ShowMapItem(ItemMap *item)
{
  if (_mapItem == item)
  {
    if (IsShowMapWanted())
    {
      // hide the map
      ShowMap(false);
      return;
    }
  }

  // show the map
  _mapItem = item;
  ShowMap(true);
}

const ItemMap *World::GetMapItem() const
{
  return _mapItem;
}

void ShowInventoryMenu(AbstractOptionsUI *dlg, const InventoryMenuMessage &msg);

void World::ShowInventoryMenu(const InventoryMenuMessage &msg)
{
  ::ShowInventoryMenu(_userDlg, msg);
}

bool World::HasOptions() const
{
  if (!_options)
    return false;
  return !_options->IsSimulationEnabled();
}

bool World::IsUIEnabled() const
{
  if (!IsSimulationEnabled())
    return false;
  if (!_options)
    return false;
  if (_showMap)
  {
    // map is shown and some child display (diary) don't want the UI
    if (_map && !_map->IsUIEnabled())
      return false;
  }
  else
  {
    // while async. preload progress is active, do not render UI
    if (_showMapProgress || _preloadAsyncCtx)
      return false;
  }
  return _options->IsUIEnabled();
}

bool World::HasMiniMap() const
{
  return (_showMiniMap || _showMiniMapToggle) && !_showMap && !_cameraEffect &&
    FocusOn() && FocusOn()->LSIsAlive();
}

void World::OnChannelChanged()
{
  _channel = NULL; // force update of indicator
  _channelChanged = Glob.uiTime;
}

void World::CreateDSOptions()
{
  if (!_options)
  {
    _options = CreateDSOptionsUI();
    if (GEngine)
      GEngine->ReinitCounters();
  }
}

void World::CreateMainOptions()
{
  if (!_options)
  {
    _options = CreateMainOptionsUI();
    if (GEngine)
      GEngine->ReinitCounters();
  }
}

void World::SetOptions(AbstractOptionsUI *options)
{
  Assert(!_options);
  // replace old options
  _options = options;
}

void World::CreateEndOptions(int mode)
{
  if( !_options )
    _options = CreateEndOptionsUI(mode);
}

void World::CreateChat()
{
  if (!_chat)
    _chat = CreateChatUI();
}

void World::CreateVoiceChat() 
{
  if (!_voiceChat) 
    _voiceChat = CreateVoiceChatUI();
}

void World::CreateMainMap()
{
  if (!_map)
    _map = CreateMainMapUI();
  else
  {
    DisplayMap *map = dynamic_cast<DisplayMap *>(Map());
    map->Init();
  }
#if _VBS3 // support for dynamic briefing
  if (_alternateBriefing.GetLength() > 0)
  {
    DisplayMap *map = dynamic_cast<DisplayMap *>(Map());
    map->Init(_alternateBriefing);
  }
#endif
}

void World::AddUserDialog(Display *dlg)
{
  ControlsContainer *udlg = dynamic_cast<ControlsContainer *>(UserDialog());
  if (udlg)
  {
    while (udlg->Child()) udlg = udlg->Child();
    udlg->CreateChild(dlg);
  }
  else SetUserDialog(dlg);
}

void World::AddDisplay(AbstractOptionsUI *display, float priority)
{
  // keep sorted by the priority
  int index = _displays.Size();
  for (int i=0; i<_displays.Size(); i++)
  {
    if (priority <= _displays[i]._priority)
    {
      index = i;
      break;
    }
  }
  _displays.Insert(index);
  _displays[index]._display = display;
  _displays[index]._priority = priority;
}

void World::RemoveDisplay(AbstractOptionsUI *display)
{
  for (int i=0; i<_displays.Size(); i++)
  {
    if (_displays[i]._display == display)
    {
      _displays.Delete(i);
      break;
    }
  }
}

AbstractOptionsUI *World::FindDisplay(int idd) const
{
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && display->GetIDD() == idd)
      return display;
  }
  return NULL;
}

void World::CreateWarningMessage(RString text)
{
  ControlsContainer *CreateWarningMessageBox(RString text);
  if (!_warningMessage)
    _warningMessage = CreateWarningMessageBox(text);
}

void World::OnControllerRemoved()
{
  ControlsContainer *CreateControllerRemovedMessageBox();
  if (!_warningMessage)
    _warningMessage = CreateControllerRemovedMessageBox();
}

void World::ReportCreateSaveError()
{
  ControlsContainer *CreateCreateSaveError();
  if (!_warningMessage)
    _warningMessage = CreateCreateSaveError();
}

void World::OnNetworkDisconnected()
{
  ControlsContainer *CreateNetworkDisconnectedMessageBox();
  if (!_warningMessage)
    _warningMessage = CreateNetworkDisconnectedMessageBox();
}

#if _USE_BATTL_EYE_SERVER
void World::OnBELicenceNotAgreed(RString reconnectGUID, RString reconnectPassword)
{
  BattlEyeLicenceMsgBox *CreateOnBELicenceNotAgreed(ControlsContainer *parent, RString reconnectGUID, RString reconnectPassword);
  if (!_beLicenceMsg)
    _beLicenceMsg = CreateOnBELicenceNotAgreed(nullptr, reconnectGUID, reconnectPassword);
}
#endif

void World::OnAskForShutDown()
{
  ControlsContainer *CreateAskForShutDownMessageBox();
  if (!_warningMessage)
    _warningMessage = CreateAskForShutDownMessageBox();
}

void ReportUserFileError(const RString &dir, const RString &file, const RString &name, bool once, bool canDelete)
{
  GWorld->ReportUserFileError(dir, file, name, once, canDelete);
}

void ReportCreateSaveError()
{
  GWorld->ReportCreateSaveError();
}

RString FindSaveGameName(RString dir, RString file)
{
  return dir + file;
}

void World::ReportUserFileError(const RString &dir, const RString &file, const RString &name, bool once, bool canDelete)
{
  RString errorName = name;
  // check if not ignored
  for (int i=0; i<_fileErrorsIgnored.Size(); i++)
  {
    UserFileErrorInfo &ignored = _fileErrorsIgnored[i];
    if (stricmp(dir, ignored._dir) != 0)
      continue;
    if (stricmp(file, ignored._file) != 0)
      continue;
    if (once)
      return; // ignore
    errorName = ignored._name;
    break;
  }

  // find name
  if (errorName.GetLength() == 0)
  {
    // TODO: Progress
    errorName = FindSaveGameName(dir, file);
  }

  // check that error is not in the queue
  for(int i=0;i<_fileErrorsReported.Size();++i)
  {
    UserFileErrorInfo &error = _fileErrorsReported[i];
    if (stricmp(dir, error._dir) != 0)
      continue;
    if (stricmp(file, error._file) != 0)
      continue;
    return;
  }

  // add to queue
  int index = _fileErrorsReported.Add();
  UserFileErrorInfo &info = _fileErrorsReported[index];
  info._dir = dir;
  info._file = file;
  info._name = errorName;
  info._canDelete = canDelete;
}

void World::DoneUserFileError(bool ignored)
{
  Assert(_fileErrorsReported.Size() > 0);
  if (ignored)
    _fileErrorsIgnored.Add(_fileErrorsReported[0]);
  _fileErrorsReported.Delete(0);
}

void World::ProcessUserFileErrors()
{
  if (!_warningMessage && _fileErrorsReported.Size() > 0)
  {
    ControlsContainer *CreateUserFileError(const UserFileErrorInfo &error, bool canDelete);
    _warningMessage = CreateUserFileError(_fileErrorsReported[0], _fileErrorsReported[0]._canDelete);
  }
}

void World::DestroyOptions(int exitCode)
{
  if( !_options ) return;
  _options.Free();
/*
  switch( exitCode )
  {
    case IDC_CANCEL:
    case IDC_MAIN_GAME:
    break;
    case IDC_MAIN_QUIT:
      Glob.exit=true;
    break;
    case IDC_OK:
      CreateMainOptions();
    break;
  }
*/
  if (exitCode == IDC_MAIN_QUIT) Glob.exit=true;

// ???
  GEngine->ReinitCounters();
}

void World::DestroyMap(int exitCode)
{
  _map.Free();
  _miniMap.Free();
  _miniMapSmall.Free();
  _showMiniMapToggle = _showMiniMap = false;
}

void World::DestroyChat(int exitCode)
{
  if (_chat)
    _chat.Free();
}

void World::DestroyVoiceChat(int exitCode)
{
  _voiceChat.Free();
}

void World::DrawCinemaBorder(float alpha)
{
  if (_cinemaBorder)
    _cinemaBorder->OnDraw(NULL, alpha);
}

Transport *World::FindFreeVehicle(Person *driver) const
{
  // find nearest vehicle I can get in
  const float maxDist=20;
  float minDist2=Square(maxDist);
  Transport *free=NULL;
  int xMin,xMax,zMin,zMax;
  Vector3Val pos=driver->RenderVisualState().Position();
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,pos,pos,maxDist);
  for( int x=xMin; x<=xMax; x++ ) for( int z=zMin; z<=zMax; z++ )
  {
    const ObjectListUsed &list=GLandscape->UseObjects(x,z);
    for( int i=0; i<list.Size(); i++ )
    {
      Object *obj=list[i];
      //if( obj->GetType()!=TypeVehicle ) continue;
      Transport *vehicle=dyn_cast<Transport>(obj);
      if( !vehicle ) continue;
      float dist2=vehicle->RenderVisualState().Position().Distance2(driver->RenderVisualState().Position());
      if( minDist2>dist2 )
      {
        if( vehicle->QCanIGetInAny(driver) )
        {
          Vector3Val relPos=driver->RenderVisualState().PositionWorldToModel(vehicle->FutureVisualState().Position());
//          if( relPos.Z()>0 && fabs(relPos.X())<relPos.Z() )
          if( relPos.Z()>0 )
          {
            minDist2=dist2;
            free=vehicle;
          }
        }
      }
    }
  }
  return free;
}

void World::PerformAI(float deltaT)
{
  PROFILE_SCOPE_GRF_EX(aiAll,*,SCOPE_COLOR_RED);
#if _ENABLE_CHEATS
  if (GInput.GetCheat2ToDo(DIK_T))
  {
    disableAI=!disableAI;
    GlobalShowMessage(500,"Group AI %s",disableAI ? "Off":"On");
  }
  if (GInput.GetCheat2ToDo(DIK_U))
  {
    disableUnitAI=!disableUnitAI;
    GlobalShowMessage(500,"Unit AI %s",disableUnitAI ? "Off":"On");
  }
  if (GInput.GetCheat2ToDo(DIK_I))
  {
    disableSimpleSim=!disableSimpleSim;
    GlobalShowMessage(500,"Simple sim %s",disableSimpleSim ? "Off":"On");
  }
#endif
  if (deltaT>0
#if _ENABLE_CHEATS
    && !disableAI
#endif
  )
  {
    // when simulated time is too big, we need to split AI simulation
    int count = toIntCeil(deltaT / 0.2f);
    saturateMax(count, 1);
    deltaT /= count;

    // simulate all AI more times in accelerated time
    for (int c=0; c<count; c++)
    {
#if _ENABLE_INDEPENDENT_AGENTS
      // TODO: limit AI simulation by some time
      for (int i=0; i<_agents.Size();)
      {
        if (_agents[i])
        {
          _agents[i]->SimulateAI();
          i++;
        }
        else
          _agents.DeleteAt(i);
      }
      for (int i=0; i<_teams.Size();)
      {
        if (_teams[i])
        {
          _teams[i]->SimulateAI();
          i++;
        }
        else
          _teams.DeleteAt(i);
      }
#endif

      GetRadio().Simulate(deltaT,NULL);
      // custom radios
      for (int i=0; i<CCCustomN; i++)
      {
        CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(CCCustom1+i);
        if (info.active)
        {
          GetCustomRadio(i).Simulate(deltaT,NULL);
        }
      }
    }
    
    if ( GetEEndMode()==EMContinue )
      CheckMissionEnd();
  }
}

/**
template argument checkOptimize is used for compile time optimization
*/
template <bool checkOptimize=false>
class EntitySimulateAIOptimized
{
  float _deltaT;
  SimulationImportance _prec;
  Entity *_cameraVehicle;
  float _minOptimizeDistance;
  
public:
  EntitySimulateAIOptimized(float deltaT, SimulationImportance prec,
    Entity *cameraVehicle, float minOptimizeDistance)
  :_deltaT(deltaT),_prec(prec),
  _cameraVehicle(cameraVehicle),_minOptimizeDistance(minOptimizeDistance)
  {}
  __forceinline bool operator()(Entity *vehicle) const
  {
    //don't even get visual state when disabled
    if(vehicle->IsSimulationDisabled())
      return false;
    Object::ProtectedVisualState<const ObjectVisualState> vs = vehicle->FutureVisualStateScope();

    if (checkOptimize && (vehicle==_cameraVehicle || vs->Position().Distance2(GScene->GetCamera()->Position())<Square(_minOptimizeDistance)))
      vehicle->SimulateAIRest(_deltaT,_prec);
    else
      vehicle->SimulateAIOptimized(_deltaT,_prec);
    return false;
  }
};

/**
@param cameraVehicle vehicle camera is attached to - needs to be simulated each frame
@param minOptimizeDistance threshold, saying which vehicles need to be simulated each frame
*/
void World::SimulateVehiclesAI(float deltaT, Entity *cameraVehicle, float minOptimizeDistance)
{
  if (minOptimizeDistance<FLT_MAX || cameraVehicle)
    _vehicles._visibleNear.ForEach(EntitySimulateAIOptimized<true>(deltaT, SimulateVisibleNear, cameraVehicle, minOptimizeDistance));
  else
    _vehicles._visibleNear.ForEach(EntitySimulateAIOptimized<>(deltaT, SimulateVisibleNear, cameraVehicle, minOptimizeDistance));
  _vehicles._visibleFar.ForEach(EntitySimulateAIOptimized<>(deltaT, SimulateVisibleFar, cameraVehicle, minOptimizeDistance));
  _vehicles._invisibleNear.ForEach(EntitySimulateAIOptimized<>(deltaT, SimulateInvisibleNear, cameraVehicle, minOptimizeDistance));
  _vehicles._invisibleFar.ForEach(EntitySimulateAIOptimized<>(deltaT, SimulateInvisibleFar, cameraVehicle, minOptimizeDistance));
}

void World::EvalPostponedEvents()
{
  if (_postponedEvents.Size() > 0)
  {
    // in case of recursion (script calling processInitCommands) prevent the event being processed again
    for (int i=0; i<_postponedEvents.Size(); i++) 
    { 
      Ref<IPostponedEvent> event = _postponedEvents[i];
      if (event)
      {
        _postponedEvents[i].Free();
        event->DoEvent();
      }
    }
    _postponedEvents.Resize(0);
  }
}

void World::SimulateVehicles(float deltaT, VehicleSimulation simul, VehicleSimulation simulOver, SimulationImportance minPrec)
{
  // first delete/move out any entities as requested by things happening out of simulation (like from script)
  MoveOutAndDelete(_vehicles);
  MoveOutAndDelete(_animals);

  SimulateOnly(_vehicles, deltaT, simul, simulOver, minPrec);
  SimulateOnly(_animals, deltaT, simul, simulOver, minPrec);
  // delete/move out entities which were requested during the simulation
  MoveOutAndDelete(_vehicles);
  MoveOutAndDelete(_animals);

  EvalPostponedEvents();
}

void World::SimulateSlowVehicles(float deltaT, VehicleSimulation simul)
{
  {
    PROFILE_SCOPE_GRF(wSimS,0);
    MoveOutAndDelete(_slowVehicles);
    SimulateOnly(_slowVehicles,deltaT,simul,simul,SimulateVisibleNear);
    MoveOutAndDelete(_slowVehicles);
  }
  {
    PROFILE_SCOPE_GRF(wSimV,0);
    MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);
#if 1
    int nVerySlowVehicles = _verySlowVehicles.VehicleCount();
    if (_verySlowToSimulate<0)
      _verySlowToSimulate = 0;
    if (_verySlowToSimulate<nVerySlowVehicles)
    {
      const float timeToWalkWholeList = 5.0f;
      // we want to loop through the whole list once per timeToWalkWholeList
      // compute how much items do we need to visit on each iteration
      int toWalk = toIntCeil(nVerySlowVehicles*deltaT*(1.0f/timeToWalkWholeList));
      // now simulate the corresponding part
      int left = nVerySlowVehicles-_verySlowToSimulate;
      if (toWalk>left) toWalk = left;
      // caution: what is the simT we need to submit here?
      // all entities need to track their "skipped time"
      // for start we assume it does not matter that much, as StreetLamp::Simulate does not use the deltaT at all
      const float simT = timeToWalkWholeList;
      _verySlowVehicles.ForEach(VehSimulate(simul,simT,SimulateVisibleNear),_verySlowToSimulate,_verySlowToSimulate+toWalk);
      _verySlowToSimulate += toWalk;
    }
    if (_verySlowToSimulate>=nVerySlowVehicles)
      _verySlowToSimulate = 0;
#else
    _verySlowVehicles.ForEach(VehSimulate(simul,deltaT,SimulateVisibleNear));
#endif
    MoveOutAndDelete(_verySlowVehicles,&_verySlowToSimulate);
  }
}

void World::SimulateFastVehicles(float deltaT, VehicleSimulation simul)
{
  MoveOutAndDelete(_fastVehicles);
  SimulateOnly(_fastVehicles, deltaT, simul, SimulateVisibleNear);
#if PERF_SIM
  ADD_COUNTER(simF,_fastVehicles.Size());
#endif
  MoveOutAndDelete(_fastVehicles);
}

#if _ENABLE_REPORT
  static int MaxCloudlets = 0;
#endif

void World::SimulateCloudlets(float deltaT)
{
  // simulate all cloudlets ... once per frame
  SimulationImportance prec = SimulateVisibleFar;

  #if _ENABLE_REPORT
    if (_cloudlets.Size()>MaxCloudlets) MaxCloudlets = _cloudlets.Size();
  #endif
  
  // note: if we are deleting many cloudlets at once simple deleting would be O(n^2), use O(N) copying instead
  int dst, src;
  int count = _cloudlets.Size();
  for( dst = src = 0; src<count; src++)
  {
    Entity *vehicle=_cloudlets[src];
    vehicle->Simulate(deltaT,prec);
    if( !vehicle->ToDelete() )
      _cloudlets[dst++] = vehicle;
  }
  _cloudlets.Resize(dst);
}

/**
JumpySim is used in: World::SimulateAllVehicles(), AnimationSourceMemberFuncVisualState::GetPhase(), Object::GetPositionRender(), Object::IsVisualStateHistoryUsed()
0 = no jumps     +0 = render age = _timeOffset (full interpolation)
1 = automatic    +3 = render age = 0 (no interpolation)
2 = all jumpy    
*/

#if _BULDOZER
int JumpySim = 7;  // turn off interpolation and visual states for Buldozer
#else
int JumpySim = 2;
#endif

#if _ENABLE_CHEATS
static void HandleJumpyCheat()
{
  if (GInput.GetCheat3ToDo(DIK_J))
  {
    JumpySim++;
    if (JumpySim >= 6) JumpySim = 0;
    const char *text[] = 
    {
      "Precise Interpolated",
      "Automatic Interpolated",
      "Minimal Interpolated",
      
      "Precise Non-interpolated",
      "Automatic Non-interpolated",
      "Minimal Non-interpolated"
    };
    GlobalShowMessage(1000, "Simulation: %s", text[JumpySim]);
  }
}
#endif


//static int s_physalloc = 0;
//static int s_physobj = 0;
//static int s_physshape = 0;
void World::SimulateAllVehicles(float deltaT)
{
#if _ENABLE_PHYSICS
  //if(GetNetworkManager().IsPhysicsMaster())
  {
    //if(GetPhysicsManager()->GetAllocation() < s_physalloc || Object::ObjectCnt() < s_physobj || LODShape::GetPhysShapeCnt() < s_physshape)
      //LogF("copak to tu mame");
    //s_physalloc = GetPhysicsManager()->GetAllocation();
    //s_physobj = Object::ObjectCnt();
    //s_physshape = s_physshape;
  }
#endif
  PROFILE_SCOPE_GRF_EX(wSimA,*sim,SCOPE_COLOR_GREEN);
  TrapFor0x8000Scope lock("World::SimulateAllVehicles");

#if _ENABLE_CHEATS
  if (GInput.GetCheat1ToDo(DIK_MINUS))
  {
    EnableAmbientLife = !EnableAmbientLife;
    GlobalShowMessage(500, "Ambient life %s", EnableAmbientLife ? "On" : "Off");
    if (EnableAmbientLife)
      // ambient life is incremental - re-init need when toggled on
      _ambientLife->Init(*_scene.GetCamera());
  }
#endif

  {
    PROFILE_SCOPE(wSimD);
    float farValidFor=1.5;
    // far vehicles can be redistributed much less often
    if( Glob.time>_farImportanceDistributionTime+farValidFor )
      DistributeFarImportances();
    if( Glob.time>_nearImportanceDistributionTime+1.0 )
      DistributeNearImportances();
    SetActiveChannels();
  }  
  // some vehicles may sometimes need 40 fps to reach numerical stability
  // we don't use fast nor complex vehicles ATM
  const float maxSimStepVehicles = 1.0f/20;

  // set motion blur start
  ForEachFastVehicle(CallVoidObjMember(&Entity::StartFrame));
  
  // implement real time slice scheme that would allow
  // any fast / slow vehicle combination easily

  // cloudlets are simulated max. once per frame
  {
    PROFILE_SCOPE_EX(wSimC,sim);
    SimulateCloudlets(deltaT);
  }
  // normal and fast vehicles can be simulated multiple times
  float toSimVehicles = deltaT;
  
  // start by simulating everything, but only as given by simulation class requirements
  {    
    PROFILE_SCOPE_GRF_EX(wSimO,sidt,0);
    while( toSimVehicles>maxSimStepVehicles )
    {
      /// perform necessary simulation for all entities
      SimulateVehiclesAI(maxSimStepVehicles,NULL,FLT_MAX);
      SimulateVehicles(maxSimStepVehicles,&Entity::SimulateOptimized,&Entity::SimulateOptimized, SimulateInvisibleFar);
      SimulateFastVehicles(maxSimStepVehicles,&Entity::SimulateFastOptimized);
      toSimVehicles-=maxSimStepVehicles;
    }
  }

  
  // perform the rest of simulation for near entities and fast vehicles only
  {
    PROFILE_SCOPE_GRF_EX(wSimR,sidt,SCOPE_COLOR_YELLOW);
#if _ENABLE_CHEATS
    HandleJumpyCheat();
    static bool OptimizeSimulationStep = true;
    SimulationImportance minPrec = OptimizeSimulationStep ? SimulateVisibleNear : SimulateInvisibleFar;
#else
    const SimulationImportance minPrec = SimulateVisibleNear;
#endif
    if (JumpySim%3 <= 1)
    {
      // AI for close vehicles is performed in each frame (<20 m)
      SimulateVehiclesAI(toSimVehicles, CameraOnVehicle(), 20);
      // simulation for close visible vehicles is performed in each frame
      SimulateVehicles(toSimVehicles, &Entity::SimulateRest, JumpySim%3==1 ? &Entity::SimulateRestWhenVisible : &Entity::SimulateRest, minPrec);
    }
    else
    {
      // accumulate the rest, simulate only when needed (optimized)
      SimulateVehiclesAI(toSimVehicles, CameraOnVehicle(), FLT_MAX);
      SimulateVehicles(toSimVehicles,&Entity::SimulateOptimizedOrRest, &Entity::SimulateOptimizedOrRest, minPrec);
      // we want the camera vehicle movement (not AI) to be simulated once per frame
      if (JumpySim!=2+3) // 2+3 -> minimal non-interpolated
      {
        SimulateRestTrackedByCamera();
      } 
      if (CHECK_DIAG(DEVisualStates))
      {
        // KeyboardPilot needs to be called in each frame, otherwise keyboard "levels" are failing
        Entity *cameraVehicle = dyn_cast<Entity,Object>(_cameraOn);
        if (cameraVehicle)
        {
          SimulationImportance prec = cameraVehicle->GetLastImportance();
          cameraVehicle->SimulateAIRest(0,prec);
        }
      }
    }
  
    // if camera vehicle was not simulated yet, simulate it now
    // this can help MP, where precision may be confused by remote owner
    Entity *cameraVehicle = CameraOnVehicle(); // note: might have been destroyed in SimulateVehicles
    if (cameraVehicle)
    {
      // if precision is not enough to make sure AI is called,
      // players in remote vehicles may see strange things
      SimulationImportance prec = cameraVehicle->GetLastImportance();
      if (prec>SimulateVisibleNear && JumpySim!=2+3)
      {
        // we pass zero, because the time was already accumulated
        cameraVehicle->SimulateAIRest(0,prec);
        cameraVehicle->SimulateRest(0,prec);
      }
    }

    // On server, we want KeyboardPilot executed each frame to make sure ClientOutput set each frame
    if (GetNetworkManager().IsServer())
    {
      AutoArray<NetPlayerInfo> players;
      GetNetworkManager().GetPlayers(players);
      for (int i=0; i<players.Size(); i++)
      {
        if (players[i].person)
        {
          Person *person = dyn_cast<Person, NetworkObject>(players[i].person);
          if (person)
          {
            AIBrain *unit = person->Brain();
            if (unit)
            {
              EntityAI *veh = unit->GetVehicle();
              if (veh && veh != cameraVehicle)
              {
                SimulationImportance prec = SimulateVisibleNear;
                // we pass zero, because the time was already accumulated
                veh->SimulateAIRest(0, prec);
                veh->SimulateRest(0, prec);
              }
            }
          }
        }
      }
    }

    // make sure all handlers are processed before simulating bullets - handlers might have been created in SimulateAIRest
    EvalPostponedEvents();
    // now simulate what is left for each fast vehicle (shots)  
    SimulateFastVehicles(toSimVehicles,&Entity::SimulateFastRest);
  }

  ForEachFastVehicle(CallVoidObjMember(&Entity::EndFrame));
  
  {
    PROFILE_SCOPE_GRF_EX(wSimB,sidt,SCOPE_COLOR_BLUE);
    // slow vehicles are simulated max. once per frame
    SimulateBuildings(deltaT,&Entity::SimulateRestWhenVisible);
    SimulateSlowVehicles(deltaT,&Entity::SimulateOptimized);
  }

#if _ENABLE_PHYSICS
  //physical simulation step
  //if(GetNetworkManager().IsPhysicsMaster())
    GLandscape->SimulatePhysics(deltaT);
#endif
}


void World::UpdateAttachedPositions()
{
  // far simulation only with accumulated precision
  // not necessary in every frame
  for( int i=0; i<_attached.Size(); i++ )
    _attached[i]->UpdatePosition(); // move to follow vehicle
}

/**
@param source source vehicle.
  May be NULL - in such case we only calculate obstructions for environmental sounds.
@inSource indicate the sound originates inside of the source vehicle (Used for occlusions)
  when the source vehicle is a human, it has no sense using this
*/
void World::CheckSoundObstruction(EntityAI *source, bool inSource, float &obstruction, float &occlusion)
{
  obstruction = 1;
  occlusion = 1;
  AIBrain *focusBrain = FocusOn();
  EntityAI *listenerAI = dyn_cast<EntityAI,Object>(CameraOn());

  EntityAI *focusAI = listenerAI;
  // depending on camera we want to simulate inside the vehicle or not
  if (focusBrain)
  {
    switch (GetCameraType())
    {
      case CamGunner: case CamInternal:
        focusAI = focusBrain->GetPerson();
        break;
    }
  }
  if(GetCameraEffect())
  {
    listenerAI = NULL;
    focusAI = NULL;
  }
  
  // if source is a soldier and is boarded in a vehicle, we will check vehicle instead
  // we can also provide an occlusion
  
  EntityAI *sourceVehicle = NULL;
  Transport *listenerVehicle = NULL;
  
  if (source)
  {
    Object *sourceParent = source->GetHierarchyParent();
    if (sourceParent)
    {
      Transport *trans = dyn_cast<Transport>(sourceParent);
      Person *crew = dyn_cast<Person>(source);
      if (crew && trans && trans->ListenerIsInside(crew))
        sourceVehicle = trans;
    }
    else if (inSource)
      sourceVehicle = source;
  }
  if (focusAI)
  {
    Object *listenerParent = focusAI->GetHierarchyParent();
    if (listenerParent)
    {
      Transport *trans = dyn_cast<Transport>(listenerParent);
      Person *crew = dyn_cast<Person>(focusAI);
      if (crew && trans && trans->ListenerIsInside(crew))
        listenerVehicle = trans;
    }
  }

  if (sourceVehicle!=listenerVehicle)
  {
    // sound not inside the same vehicle as the listener
    if (sourceVehicle)
    {
      if (source && source!=sourceVehicle || inSource)
      {
        // source is in the vehicle and it is not the same as source
        // the vehicle is causing occlusion
        // we know for sure we are not handing inside/outside of the same vehicle now?
        sourceVehicle->OccludeAndObstructSound(source,occlusion,obstruction,EntityAI::Different);
      }
    }
    if (listenerVehicle)
    {
      // listener is in the vehicle and it is not the same as source
      // the vehicle is causing occlusion, it may be causing obstruction as well
      EntityAI::ObstructType obsType = EntityAI::Different;
      if (listenerVehicle==source)
        obsType = inSource ? EntityAI::SameInside : EntityAI::SameOutside;
      listenerVehicle->OccludeAndObstructSound(focusAI,occlusion,obstruction,obsType);
    }
  }

  // change target for further obstruction/occlusion testing

  // camera object always sees itself perfectly
  if (source && source!=listenerAI)
  {
    // if the camera is far from the primary observer, we have no way to determine obstructions
    if (listenerAI && focusAI && listenerAI->FutureVisualState().Position().Distance2(_scene.GetCamera()->Position())<Square(10))
    {
      AIBrain *commander = listenerAI->CommanderUnit();
      // if there is no AI unit or it is dead, there is no visibility info
      if (commander && commander->GetLifeState()!=LifeStateDead)
      {
        EntityAI *sourceTest = sourceVehicle ? sourceVehicle : source;
        // TODO: consider providing more information about character of the obstacle
        float visible = Visibility(commander, sourceTest);
        float obstacle = 1-visible;
        // assume most occlusions are because of a big things
        // obstruction applies to direct sound, but not to reverbed/reflected sound
        obstruction *= floatMax(visible,0.02f);
        // occlusion applies to both direct and reflected sound
        // we are not sure if it is occlusion or obstruction, we apply both, occlusion less
        occlusion *= floatMax(1-obstacle*0.5,0.02f);
      }
    }
  }
}

float World::Visibility(const AIBrain *from, Object *to, float maxAge) const
{
  EntityAI *ai = dyn_cast<EntityAI>(to);
  if (!ai)
    return 1.0f; // non-ai objects are always visible
  Person *me = from->GetPerson();
  AIBrain *aiUnit = ai->EffectiveObserverUnit();
  if (aiUnit || ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeAllVehicles)) || ai->GetType()->GetLaserTarget() || ai->GetType()->GetNvTarget())
  {
    if (!ai->GetType()->ScannedAsTarget())
      // if object is not tested as a target, assume it is always visible
      return 1;
    // if vehicle is in assume full visibility
    if (!ai->IsInLandscape())
    {
      LogF("Patch: vanished vehicle visibility queried (%s to %s)", (const char *)me->GetDebugName(), (const char *)ai->GetDebugName());
      return 1.0f;
    }
    // each unit always sees its own body/vehicle 100 %
    if (from->GetVehicle()==to)
      return 1.0f;

    // check actual visibility (from sensor matrix)
    return _sensorList->GetVisibility(me,ai,maxAge);
  }
  // static objects are always visible
  return 1.0f;
}

Time World::VisibilityTime(const AIBrain *from, Object *to) const
{
  EntityAI *ai = dyn_cast<EntityAI>(to);
  if (!ai)
    return Glob.time; // non-ai objects are always visible
  Person *me = from->GetPerson();
  AIBrain *aiUnit = ai->CommanderUnit();
  if (aiUnit /*|| ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeStrategic))*/ || ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeAllVehicles)))
  {
    // if vehicle is in assume full visibility
    if (!ai->IsInLandscape())
      return Glob.time;
    // check actual visibility (from sensor matrix)
    return _sensorList->GetVisibilityTime(me,ai);
  }
  // static objects are always visible
  return Glob.time;
}

/// check if the camera is in position to here the crack
/**
Used when the source begins its travel.

*/
static bool CameraCanHearForCrack(Entity *veh)
{
  // we assume veh is moving almost only in positive z direction
  Vector3 relPos = veh->PositionWorldToModel(GScene->GetCamera()->Position());
  return relPos.Z()>0.5f; // when switch to gun optics then camera position is slightly ahead shot
}
/// check if camera is in front of the supersonic wave
/**
Used to detect the moment shot wave passed by the camera.
*/
static bool CameraWaitingForCrack(Entity *veh)
{
  // following line does not work with stretched shots:
  //Vector3 relPos = veh->PositionWorldToModel(GScene->GetCamera()->Position());
  // create a coordinate space aligned with the veh velocity
  Vector3 velDir = veh->FutureVisualState().Speed().Normalized();
  Matrix4 movingSpace;
  // we do not care about up/aside, we only need dir to be known
  if (fabs(velDir.Y())/*fabs(velDir*VUp)*/<0.7f) movingSpace.SetDirectionAndUp(veh->FutureVisualState().Speed(),VUp);
  else movingSpace.SetDirectionAndUp(veh->FutureVisualState().Speed(),VForward);
  movingSpace.SetPosition(veh->FutureVisualState().Position());
  
  Matrix4 invMovingSpace = movingSpace.InverseRotation();
  
  Vector3 relPos = invMovingSpace.FastTransform(GScene->GetCamera()->Position());
  float sideDist2 = Square(relPos.X())+Square(relPos.Y());
  // TODO: consider cone, not a half-space
  // negative z means camera is behind the bullet
  if (relPos.Z()>0) return true;
  //static bool testCone = false;
  //if (!testCone) return true;
  // even if camera is behind, it may be still waiting
  /*
  float timeNeeded = sqrt(sideDist2)*InvSpeedOfSound;
  float timeElapsed = -relPos.Z()/veh->ModelSpeed().Z();
  // check if time elapsed is long enough
  return timeElapsed<timeNeeded;
  return relPos.Z()*SpeedOfSound/sqrt(sideDist2)>veh->ModelSpeed().Z();
  */
  // check if time elapsed is long enough
  return veh->FutureVisualState().ModelSpeed().Z()*InvSpeedOfSound>-relPos.Z()*InvSqrt(sideDist2);
}

void World::AddFakeBulletsSource(Entity *veh)
{
  if (veh != NULL)
  {
    Vector3Val camPos = GScene->GetCamera()->Position();

    Vector3Val listEmiterDist = camPos - veh->FutureVisualState().Position();

    // no flying bullet sound from near sources
    static const float MinDist2 = 900.0f;

    float vehDist2 = listEmiterDist.SquareSize();

    if (vehDist2 > MinDist2)
    {
      // bullet trajectory cut through listener circle
      float dot = listEmiterDist.DotProduct(veh->FutureVisualState().Direction());

      Vector3Val tmp = veh->FutureVisualState().Position() + dot * veh->FutureVisualState().Direction();

      // check distance from the projectile trajectory
      float dist2 = (tmp - camPos).SquareSize();

      // maximal surroundings where is possible listen flying bullet
      static const float MaxRadius2 = 10;

      if (dist2 < MaxRadius2)
      {
        FakeBulletFlySource &src = _fakeBulletsSource.Append();
        // time when bullet is near camera
        src.time = Glob.time + (vehDist2 / veh->FutureVisualState().Speed().SquareSize());
        src._source = veh;
      }
    }
  }
}

/* Simulate flying bullets sounds around camera, causes zip effect */
void World::SimulateFakeBullets()
{
  PROFILE_SCOPE_EX(simFB, sound);

  int d = 0;
  int currSounds = 0;
  for (int i = 0; i < _fakeBulletsSource.Size(); i++)
  {
    FakeBulletFlySource &fb = _fakeBulletsSource[i];

    // the bullet model no longer exists
    if (fb._source.IsNull())
    {
      if (fb._soundSource)
      {
        // no bullet exists and sound sample finished yet
        if (fb._soundSource->IsTerminated())
          continue;
      }
      else
        // no sample assigned
        continue;
    }
    else if (!fb._soundSource)
    {
      static int MaxContinousSounds = 2;
      // no more than two samples per iteration
      if (currSounds < MaxContinousSounds && fb.time - Glob.time <= 0)
      {
        Vector3Val camPos = GScene->GetCamera()->Position();

        // check listener and projectile distance
        Vector3Val distVec = camPos - fb._source->FutureVisualState().Position();

        float dist2 = distVec.SquareSize();

        static const float MaxRadius2 = 10;

        // if bullet is enough near to listener
        if (dist2 < MaxRadius2)
        {
          float vol = InterpolativC(dist2, 0.1, MaxRadius2, 1, 0.01);

          if (fb._source)
          {
            const SoundPars &pars = fb._source->Type()->FakeBulletSound(GRandGen.RandomValue());

            // select sample
            fb._soundSource = GSoundScene->OpenAndPlayOnce(pars.name, NULL, false, 
              fb._source->FutureVisualState().Position(), fb._source->FutureVisualState().Speed(), vol, pars.freq, pars.distance);
          }
          currSounds++;
        }
      }
    }
    if (d != i)
      _fakeBulletsSource[d] = fb;
    d++;
  }
  _fakeBulletsSource.Resize(d);
}

/* Release current flying bullets when camera change */
void World::ResetFakeBullets()
{
  _fakeBulletsSource.Clear();
}

/**
This function is most often called together with AddFastVehicle
*/
void World::AddSupersonicSource(Entity *veh)
{
  // if too slow, assume there is no need to track it
  // note: some missiles or bombs may accelerate
  // if they should create a supersonic crack, they should register themselves as a source here
  if (veh->FutureVisualState().Speed().SquareSize()<Square(SpeedOfSound))
    return;

  // check if camera is in front of the supersonic wave
  // if it is, this shot is a candidate for a future crack
  if (!CameraCanHearForCrack(veh))
    return;
  SupersonicCrackSource &add = _supersonicSource.Append();
  add._source = veh;
}

void World::SimulateSupersonicCracks()
{
  // TODO: path in the ammo type
  static float nearDistance = 5;
  static float farDistance = 50;
  // scan all potential supersonic crack sources
  // delete those which are already done
  int d=0;
  for (int s=0; s<_supersonicSource.Size(); s++)
  {
    SupersonicCrackSource &ss = _supersonicSource[s];
    if (ss._soundNear || ss._soundFar)
    {
      // once the sound has started, always finish it
      if ((!ss._soundNear || ss._soundNear->IsTerminated()) && (!ss._soundFar || ss._soundFar->IsTerminated()))
        continue;
    }
    else if (ss._source.IsNull() || ss._source->FutureVisualState().Speed().SquareSize()<Square(SpeedOfSound))
      // if the shot no longer exists or has slowed down, it cannot crack
      continue;
    else if (!CameraWaitingForCrack(ss._source))
    {
      // pressure wave has moved over the camera - start the sound
      // check distance from the projectile trajectory
      Vector3Val camPos = GScene->GetCamera()->Position();
      float dist = NearestPointInfiniteDistance(ss._source->FutureVisualState().Position(),ss._source->FutureVisualState().Direction(),camPos);
      // we assume distance from the line makes the sound linearly weaker
      static float maxVolDistance = 5.0f;
      static float maxVol = 8.0f;
      float vol = dist>maxVolDistance ? maxVol*maxVolDistance/dist : maxVol;
      float nearF = Interpolativ(dist,nearDistance,farDistance,1,0);
      const SoundPars &nearPars = ss._source->Type()->_supersonicCrackNear;
      const SoundPars &farPars = ss._source->Type()->_supersonicCrackFar;
      float nearVol = vol*nearF*nearPars.vol;
      float farVol = vol*(1-nearF)*farPars.vol;
      if (nearVol>0 && nearPars.name.GetLength()>0)
        ss._soundNear = GSoundScene->OpenAndPlayOnce2D(nearPars.name,nearVol,nearPars.freq);
      if (farVol>0 && farPars.name.GetLength()>0)
        ss._soundFar = GSoundScene->OpenAndPlayOnce2D(farPars.name,farVol,farPars.freq);
      if (!ss._soundNear && !ss._soundFar)
        // sound not found - no need to track any further
        continue;
    }
    if (d!=s)
      _supersonicSource[d] = ss;
    d++;
  }
  _supersonicSource.Resize(d);
}

struct EntityAddSupersonicSource
{
  World *_world;
  
  explicit EntityAddSupersonicSource(World *world):_world(world){}
  
  bool operator()(Entity *ent) const
  {
    _world->AddSupersonicSource(ent);
    return false;
  }
};

/**
Note: as CameraCanHearForCrack is used here, some source which should be in fact waiting may be skipped.

As this is hard to notice, we do not mind. 
*/

void World::ResetSupersonicCracks()
{
  _supersonicSource.Clear();

  EntityAddSupersonicSource sound(this);
  ForEachFastVehicle(sound);
}

int numSoundEnts = 0;
class EntityPerformSound
{
  Entity *_inside;
  float _deltaT;
  
public:
  EntityPerformSound(Entity *inside, float deltaT):_inside(inside),_deltaT(deltaT){}
  
  __forceinline bool operator()(Entity *vehicle) const
  {
    if(!vehicle->IsSimulationDisabled())
    {
      //it's bad idea to force create render visual state, as most of entities won't use that
      //Object::ProtectedVisualState<const ObjectVisualState> vs = vehicle->RenderVisualStateScope(); 
      vehicle->Sound(_inside==vehicle,_deltaT);
      numSoundEnts++;
    }
    return false;
  }
};



void World::PerformSound(Entity *inside, float deltaT)
{
  PROFILE_SCOPE_EX(wPSnd,sound);
  if( !GSoundsys ) return;
  const Camera &cam=*_scene.GetCamera();
  // check environmental properties around listener
  // check forest:

  // detect forest
#define SHOW_ENV 0

#if SHOW_ENV
  Vector3Val pos = cam.Position();
  int x = toIntFloor(pos.X() * InvLandGrid);
  int z = toIntFloor(pos.Z() * InvLandGrid);
  GeographyInfo geogr = GLandscape->GetGeography(x, z);
  float sy = GLandscape->SurfaceYAboveWater(pos.X(),pos.Z());

  if (geogr.u.forest)
  {
    SoundEnvironment env;
    env.type = SEForest;
    env.size = 38;
    env.density = 0.5;
    GSoundsys->SetEnvironment(env);
    #if _ENABLE_CHEATS
      if (CHECK_DIAG(DEEAX))
      {
        GlobalShowMessage(100,"Forest %.1f %.1f",env.size,env.density);
      }
    #endif
  }
  else if (geogr.u.howManyHardObjects>0)
  {
    // city
    SoundEnvironment env;
    env.type = SECity;
    // check how many object are around us    
    env.size = (4-geogr.u.howManyHardObjects)*15;
    env.density = geogr.u.howManyHardObjects*(1.0f/3);
    GSoundsys->SetEnvironment(env);
    #if _ENABLE_CHEATS
      if (CHECK_DIAG(DEEAX))
      {
        GlobalShowMessage(100,"City %.1f %.1f",env.size,env.density);
      }
    #endif
  }
  else if (sy>170)
  {
    // mountains
    SoundEnvironment env;
    env.type = SEMountains;
    // check height ASL
    env.size = sy-120;
    saturate(env.size,50,100);
    env.density = 0.5;
    GSoundsys->SetEnvironment(env);
    #if _ENABLE_CHEATS
      if (CHECK_DIAG(DEEAX))
      {
        GlobalShowMessage(100,"Mountains %.1f %.1f",env.size,env.density);
      }
    #endif
  }
  else
  {
    // plain
    SoundEnvironment env;
    env.type = SEPlain;
    // check how many object are around us    
    env.size = 75-geogr.u.howManyObjects*15;
    env.density = 0.5;
    GSoundsys->SetEnvironment(env);
    #if _ENABLE_CHEATS
      if (CHECK_DIAG(DEEAX))
      {
        GlobalShowMessage(100,"Plain %.1f %.1f",env.size,env.density);
      }
    #endif
  }
#endif

  // check surface height (to d
  GSoundsys->SetListener(
    cam.Position(),cam.Speed(),
    cam.Direction(),cam.DirectionUp()
  );
  EntityPerformSound sound(inside,deltaT);
  ForEachVehicle(sound);
  ForEachAnimal(sound);
  //ForEachSlowVehicle(sound);
  ForEachFastVehicle(sound);

  if (_envSound)
  {
    PROFILE_SCOPE_EX(snEnv,sound);
    {
      // add environmental sounds (no 3D positioning)
      Vector3Val camPos = cam.Position();
      float camHeight=camPos[1]-_scene.GetLandscape()->SurfaceY(camPos[0],camPos[2]);
      float canHear=1-camHeight*1.0/300.0;

      Transport *vehicleIn = FocusOn() ? FocusOn()->GetVehicleIn() : NULL;
      if (vehicleIn) 
      {
        canHear *= (vehicleIn->GetCamPos() > 0.5f) ? 0.1f : 0.05f;
      }

      if (DisableEnvironment) canHear = 0;
      float obstruction=1,occlusion=1;
      CheckSoundObstruction(NULL,false,obstruction,occlusion);

      int soundCount = _envSound->Count(cam);
      for (int i=0; i<soundCount; i++)
      {
        float volume;
        const SoundPars &pars = _envSound->GetSound(i,volume);

        saturate(canHear,0,1);

        // check occlusion/obstruction based on current focus
        GSoundScene->SetEnvSound(i,pars,volume*canHear,obstruction,occlusion);

        // additional sounds only for environment
        if (volume > 0.05f)
        {
          SoundPars extPars;
          if (_envSound->GetRandSound(i, extPars))
          {
            // random point on circle around camera position
            float const static r = 20;
            float const static dy = 5;
            float angle = GRandGen.RandomValue()*(H_PI*2);          
            float rx = sin(angle), rz = cos(angle);
            float posX = camPos.X()+rx*r;
            float posZ = camPos.Z()+rz*r;

            // place sample above surface
            float posY = GLandscape->RoadSurfaceYAboveWater(posX,posZ) + dy;
            Vector3 pos(posX,posY,posZ);

            GSoundScene->SetEnvSoundExt(i, extPars, pos, canHear, obstruction, occlusion);
          }
        }
      }

      if (ExplicitEnvSound.name.GetLength()!=0)
      {
        // some explicit sound provided - play it
        GSoundScene->SetEnvSound(soundCount, ExplicitEnvSound, canHear, obstruction, occlusion);
        soundCount++;
      }

      GSoundScene->AdvanceEnvSounds(soundCount); // remove old sounds 
    }
  }
  
  // supersonic crack simulation
  SimulateSupersonicCracks(); 
  // fake bullets simulation
  SimulateFakeBullets();
  numSoundEnts = 0;
}

void World::UnloadSounds()
{
  TerminateVideoBuffers();

  ForEachVehicle(CallVoidObjMember<Entity>(&Entity::UnloadSound));
  ForEachSlowVehicle(CallVoidObjMember<Entity>(&Entity::UnloadSound));
  ForEachAnimal(CallVoidObjMember<Entity>(&Entity::UnloadSound));
  ForEachFastVehicle(CallVoidObjMember<Entity>(&Entity::UnloadSound));

  GSoundScene->Reset(); // unload global sounds
}

void World::OnCameraChanged()
{
  _eyeAccomCut = true;
  _blindIntensity = 0;
  GEngine->ForgetAvgIntensity();
  ResetSupersonicCracks();
  ResetFakeBullets();
}

/**
@param time skipped in seconds (may be even negative)
*/
void World::OnTimeSkipped(float time)
{
  if (fabs(time)>1800)
  {
    _eyeAccomCut = true;
    _blindIntensity = 0;
    GEngine->ForgetAvgIntensity();
  }
  if (fabs(time)>120)
    _scene.ResetLandShadows();
}

static void CalculateShadowZDistance(float objDist, float fogMaxRange)
{
#if _HELISIM_SHADOW_ZDISTANCE
  float shadowDist = GScene->GetShadowZDistance();

  // Coefficient that says that shadows end in shadowFogCoef of the maximum fog distance
  const float shadowFogCoef = 0.95f;
  Glob.config.SetShadowZ(floatMin(shadowDist, shadowFogCoef * fogMaxRange));
#else

  float shadowCoef = 0.33f;
  float maxShadow = Glob.config.GetMaxShadowSize();

  float shadowDist = objDist * shadowCoef;
  if (shadowDist>maxShadow) shadowDist = maxShadow;

  // Coefficient that says that shadows end in shadowFogCoef of the maximum fog distance
  const float shadowFogCoef = 1.0f / 6.0f;
  Glob.config.SetShadowZ(floatMin(shadowDist, fogMaxRange * shadowFogCoef));
#endif
}

void World::AdjustDerivedVisibility()
{
  _scene.ResetFog();

  float viewDist = Glob.config.horizontZ;
	// for viewDist begViewRamp we want objDist to be begObjRamp
	// we never want objDist higher than endObjRamp, which we want for viewDist endViewRamp
	const float begViewRamp = 700.0f;
	const float begObjRamp = 700.0f;
#if _VBS3
	const float endViewRamp = 5000.0f;
  const float endObjRamp = Glob.config.maxObjDrawDistance;
#else
	const float endViewRamp = 28000.0f;
	const float endObjRamp = 13000.0f;
#endif
	
	const float begRoadRamp = 500.0f;
#if _VBS3
  const float endRoadRamp = 3000.0f;
#else
	const float endRoadRamp = 1350.0f;
#endif
	
	float objDist = viewDist*(begObjRamp/begViewRamp);
	float roadDist = viewDist*(begRoadRamp/begViewRamp);
	if (viewDist>begViewRamp)
	{
	  if (viewDist>endViewRamp)
	  {
	    objDist = endObjRamp;
	    roadDist = endRoadRamp;
	  }
	  else
	  {
	    float rampF = (viewDist-begViewRamp)/(endViewRamp-begViewRamp);
	    objDist =  begObjRamp *(1-rampF)+endObjRamp*rampF;
	    roadDist = begRoadRamp*(1-rampF)+endRoadRamp*rampF;
	  }
	}
	
	// make sure obj/road never exceeds terrain view distance no matter how ramps are set up
	if (objDist>viewDist) objDist = viewDist;
	if (roadDist>viewDist) roadDist = viewDist;
	
	Glob.config.objectsZ = objDist;
#if _HELISIM_VIEW_DIST
  float preferredObjDist = GScene->GetPreferredObjectViewDistance();
  if(preferredObjDist < 0)
  {
    float preferredViewDistance =  GScene->GetPreferredViewDistance();
    preferredObjDist = preferredViewDistance *(begObjRamp/begViewRamp);
    if (preferredViewDistance>begViewRamp)
    {
      if (preferredViewDistance>endViewRamp)preferredObjDist = endObjRamp;
      else
      {
        float rampF = (preferredViewDistance-begViewRamp)/(endViewRamp-begViewRamp);
        preferredObjDist =  begObjRamp *(1-rampF)+endObjRamp*rampF;
      }
    }
    // make sure obj/road never exceeds terrain view distance no matter how ramps are set up
    if (preferredObjDist >preferredViewDistance) preferredObjDist = preferredViewDistance;
    GScene->SetPreferredObjectViewDistance(preferredObjDist, false);
  }

  Glob.config.objectsZ = preferredObjDist;

  Glob.config.vehiclesZ = preferredObjDist;
  Glob.config.treesZ = preferredObjDist;
  Glob.config.staticZ = preferredObjDist;
#endif
	Glob.config.roadsZ = roadDist;
	CalculateShadowZDistance(objDist, _scene.GetFogMaxRange());
}

bool World::GetGrassEnabled() const
{
  float gridSize = _scene.GetPreferredTerrainGrid();
  // same grid size for all computers required in MP
  if (GetMode()==GModeNetware)
  {
    // for network we have to use default values
    gridSize = _scene.GetDefaultTerrainGrid();
  }
  if (_terrainGridHard>0) gridSize = _terrainGridHard;
  return gridSize<49;  
}

void World::GetCurrentTerrainSettings(float &viewDist, float &gridSize, GameMode mode, bool isR2T) const
{
  gridSize = _scene.GetPreferredTerrainGrid();
  viewDist = _scene.GetPreferredViewDistance();

  if (isR2T)
    viewDist = _scene.GetDefaultViewDistance() * 0.25f;

  // same grid size for all computers required in MP
#if !_VBS3 // allow different viewdistances in MP
  if (mode==GModeNetware)
  {
    // for network we have to use default values
    gridSize = _scene.GetDefaultTerrainGrid();
    viewDist = _scene.GetDefaultViewDistance();
  }
#endif
  if (viewDist<_viewDistanceMin) viewDist = _viewDistanceMin;
  // if there is any hard set value, we need to obey it
  if (_viewDistanceHard>0) viewDist = _viewDistanceHard;
  if (_terrainGridHard>0) gridSize = _terrainGridHard;
  
#if _VBS3
	saturate(viewDist, 200, Glob.config.maxViewDistance);
#else
  saturate(viewDist, 200, 40000);
#endif
}

void World::AdjustSubdivision(GameMode mode)
{
  float gridSize, viewDist;
  GetCurrentTerrainSettings(viewDist,gridSize,mode);
	
	Glob.config.tacticalZ = viewDist;
	Glob.config.horizontZ = viewDist;

  AdjustDerivedVisibility();	
	
  LogF("Visibility set to %f",viewDist);
  GLandscape->InitCache(false);
	GLandscape->Simulate(0);
}

void World::SetTerrainGridHard(float grid, bool initCache)
{
  _terrainGridHard = grid;
  AdjustSubdivision(GetMode());
  _scene.GetLandscape()->OnTerrainGridChanged();
  if (initCache)
    _scene.GetLandscape()->FillCache(*_scene.GetCamera());
}

void World::ActivateAddons(const FindArrayRStringCI &addons, RString worldName)
{
  _activeAddons.Clear();
  // create merged list of banks that should be activated
  // active all default addons
  ParamEntryVal def = Pars>>"CfgAddons">>"PreloadAddons";
  for (int c=0; c<def.GetEntryCount(); c++)
  {
    ParamEntryVal cc = def.GetEntry(c);
    if (!cc.IsClass()) continue;
    if (!cc.FindEntry("list")) continue;
    ParamEntryVal cl = cc>>"list";
    for (int i=0; i<cl.GetSize(); i++)
    {
      RString addon = cl[i];
      //LogF("Activating default addon %s",(const char *)addon);
      _activeAddons.Add(addon);
    }
  }
  // activate all addons from list
  for (int i=0; i<addons.Size(); i++)
  {
    RString addon = addons[i];
    //LogF("Activating addon %s",(const char *)addon);
    _activeAddons.Add(addon);
  }
  
  if (worldName.GetLength()>0)
  {
    // check based on worldName
    // island/world - check owner of the island config
    ConstParamEntryPtr entry = (Pars>>"CfgWorlds").FindEntry(worldName);
    if (entry)
    {
      const RStringB &owner = entry->GetOwnerName();
      if (owner.GetLength()>0)
        _activeAddons.Add(owner);
    }
  }
  
  ConstParamEntryPtr cfg = Pars.FindEntry("CfgPatches");
  if (cfg)
  {
    //! enumerate all addons in this config
    for (int i=0; i<_activeAddons.GetSize(); i++)
    {
      RStringB addonName = _activeAddons.Get(i);
      ConstParamEntryPtr entry = cfg->FindEntry(addonName);
      if (entry && entry->IsClass())
      {
        // check if we can find this addon in "preloaded" addons list
        ConstParamEntryPtr required = entry->FindEntry("requiredAddons");
        if (required)
        {
          // make sure all required addons are listed as well
          for (int r=0; r<required->GetSize(); r++)
          {
            RStringB req = (*required)[r];
            // add if needed
            _activeAddons.Add(req);
          }
        }
      }
    }
  }
}

bool World::CheckAddon(ParamEntryPar entry)
{
  bool visible = entry.CheckVisible(_activeAddons);
  if (visible)
    return true;
  // check if current display is able to perform last addon registration
  if (_options)
  {
    RString addon = entry.GetOwnerName();
    bool registered = _options->DoUnregisteredAddonUsed(addon);
    if (registered)
      _activeAddons.Add(addon);
    else
    {
      RptF("Addon %s (entry %s) not found in the list of active addons.", (const char *)addon, (const char *)entry.GetName());
      // we do not want any "missing addon" messages for singleplayer
      if (GetMode()!=GModeNetware)
        return true;
    }
    //return registered;
  }
  else
  {
    //return false;
  }
  return true; // for Dayz we do not want any missing addons errors
}

void MemoryCleanUp();

/*
@return true when different landscape was loaded
*/
void World::SwitchLandscape(const char *name)
{
  SECUROM_MARKER_SECURITY_ON(15)
  GSoundScene->Reset();
  // stop any music that might be playing, reset sound settings
  // InitVehicles should always be called after this call
  // Serialize (Loading) may also use this function - it should unlock too
  GDebugger.NextAliveExpected(15*60*1000);
  // lock all types and textures in memory
  // locked until InitVehicles
  CleanUp();
  _scene.CleanUpDrawObjects();

  Landscape *land=_scene.GetLandscape();
  // load corresponding landscape file
  if (!land)
  {
    Fail("No landscape");
    return;
  }

  bool deepFlush = false;
  if (_leakRecoveryCounter++>=10)
  {
    _leakRecoveryCounter = 0;
    deepFlush = true;
  }

  const char *lName=land->GetName();
  RString filename = GetWorldName(name);
  if (!lName || strcmpi(filename, lName))
  {
#if _VBS3_LARGE_OBJECTS
    // clear the Large Static Objects (dynamic ones are cleard in Landscape::ClearState)
    land->ClearLargeStaticObjects();
#endif

    // first of all reset landscape state, remove all vehicles ...
    land->ResetState();
    
    land->FreeCaches();
    
    land->FreeIds();

    // make sure all clutter references are unbinded

    // release everything, so that we can cleanup the memory
    
    _engine->FlushMemory(deepFlush);
    GetNetworkManager().CleanUpMemory();
    MemoryCleanUp();
    
#if _ENABLE_REPORT && _ENABLE_PERFLOG
    DWORD startTime = GlobalTickCount();
    size_t startMemory = MemoryUsed();
    void ClearReadWaitTime();
    ClearReadWaitTime();
#endif

    strcpy(Glob.header.worldname, name);

    ParamEntryVal worlds = Pars >> "CfgWorlds";
    ConstParamEntryPtr clsE =  worlds.FindEntry(name);
    ParamEntryVal cls = clsE ? *clsE : worlds >> "DefaultWorld";

    ParseCfgWorld(cls);
    
    // different relief/objects - load the world
    land->LoadData(filename, cls);

    InitLandscape(land);
    //land->RebuildIDCache();
    {
      // prepare some neutral camera position
      Vector3 center;
      center.Init();
      center[0] = (cls >> "centerPosition")[0];
      center[2] = (cls >> "centerPosition")[1];
      center[1] = land->SurfaceYAboveWater(center.X(),center.Z()) + 20;

      Camera &camera = *GScene->GetCamera();

      camera.SetUpAndDirection(VUp,Vector3(1,0,0));
      camera.SetPosition(center);
      camera.SetSpeed(VZero);
    }

    GFileServer->SaveDVDLog();

#if _ENABLE_REPORT && _ENABLE_PERFLOG
    DWORD time = GlobalTickCount() - startTime;
    size_t memUsed = MemoryUsed()-startMemory;
    int GetReadWaitTime();
    LogF("*** Loading %s ... %d ms (%d ms waiting for i/o), %d B used", name, time, GetReadWaitTime(),memUsed);
#endif
  }
  else
  {
    // only reset object state if landscape file is same
    land->ResetState();
    Reset();
    land->FlushAICache(); // no need to reset terrain / landscape cache
    Assert(CheckVehicleStructure());
    if (!_map)
    {
      _showMap = false;
      _showMapUntil = 0;
      SetShowMapProgress(NULL);
//      CreateMainMap();
    }
    //if (!_miniMap) CreateMiniMaps(); //moved to SimulateUI, as this line caused showing GPS in main menu

    _engine->FlushMemory(deepFlush);
    GetNetworkManager().CleanUpMemory();
    MemoryCleanUp();
  }

  // reset visibility distance to default

  Glob.config.tacticalZ=900;
  Glob.config.horizontZ=900;
  Glob.config.objectsZ=600;
  Glob.config.SetShadowZ(Glob.config.DefaultShadowZDistance);

  GLandscape->Simulate(0);
  UpdateAttachedPositions();
  
  AdjustDerivedVisibility();
  //GLandscape->FillCache(*GScene->GetCamera());

  SECUROM_MARKER_SECURITY_OFF(15)
}

OLinkObject World::FindObject(const ObjectId &id) const
{
  if (id.IsNull())
    return NULL;
  if (id.IsObject())
    return GLandscape->GetObject(id);
  // find object in all vehicle lists
  Entity *veh;
  veh = _vehicles.Find(id);
  if (veh)
    return veh;
  veh = _animals.Find(id);
  if (veh)
    return veh;
  // only objects are in buildings list - already handled
  veh = _slowVehicles.Find(id);
  if (veh)
    return veh;
  veh = _verySlowVehicles.Find(id);
  if (veh)
    return veh;
  veh = _fastVehicles.Find(id);
  if (veh)
    return veh;
  // find in cloudlet list?
  // find in outVehicles list?
  for (int i=0; i<_outVehicles.Size(); i++)
  {
    Entity *veh = _outVehicles[i];
    if (veh->GetObjectId()==id)
      return veh;
  }
  ErrF("Id %s not found",(const char *)id.GetDebugName());
  return NULL;
}

void World::AddVehicle(Entity *vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
  LogF("World::AddVehicle %s",(const char *)vehicle->GetDebugName());
#endif
  DoAssert(vehicle->RefCounter()==0 || !_vehicles.IsPresent(vehicle));
  _vehicles.Add(vehicle);
#if _VBS3 // GVehicleOnCreated
  void ProcessCreateVehicleEH(Entity *veh, bool isSlowVehicle);
  ProcessCreateVehicleEH(vehicle,false);
#endif
  EntityAI *veh = dyn_cast<EntityAI>(vehicle);
  if (veh)
  {
    // some vehicles need to be tracked regularly
    if (veh->GetType()->GetLaserTarget() || veh->GetType()->GetNvTarget() || veh->GetType()->IsKindOf(Preloaded(VTypeAllVehicles)))
      AddTarget(veh);
  }
}

void World::RemoveVehicle(Entity *vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
  LogF("World::RemoveVehicle %s",(const char *)vehicle->GetDebugName());
#endif
  DoAssert(_vehicles.IsPresent(vehicle));
  _vehicles.Remove(vehicle);
}

void World::InsertVehicle(Entity *vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
  LogF("World::InsertVehicle %s",(const char *)vehicle->GetDebugName());
#endif
  DoAssert(vehicle->RefCounter()==0 || !_vehicles.IsPresent(vehicle));
  _vehicles.Insert(vehicle);
}

void World::DeleteVehicle(Entity *vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
  LogF("World::DeleteVehicle %s",(const char *)vehicle->GetDebugName());
#endif
  DoAssert(_vehicles.IsPresent(vehicle));
  _vehicles.Delete(vehicle);
}

void World::AddOutVehicle(Entity *vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
  LogF("World::AddOutVehicle %s",(const char *)vehicle->GetDebugName());
#endif
  DoAssert(vehicle->RefCounter()==0 || _outVehicles.Find(vehicle)<0);
  _outVehicles.Add(vehicle);
//RptF("Moving %s from landscape",(const char *)vehicle->GetDebugName());
}

void World::RemoveOutVehicle(Entity *vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
  LogF("World::RemoveOutVehicle %s",(const char *)vehicle->GetDebugName());
#endif
  DoAssert(_outVehicles.Find(vehicle)>=0);
  _outVehicles.Delete(vehicle);
//RptF("Moving %s into landscape",(const char *)vehicle->GetDebugName());
}

bool World::IsOutVehicle(Entity *vehicle)
{
  return (_outVehicles.Find(vehicle)>=0);
}

VehicleListType World::GetListType(EntityList *list)
{
  if (list==nullptr) return VLTOut;
  if (_slowVehicles.IsList(list)) return VLTSlowVehicle;
  if (_animals.IsList(list)) return VLTAnimal;
  if (list==&_verySlowVehicles) return VLTSlowVehicle;
  if (list==&_fastVehicles) return VLTFast;
  DoAssert(_vehicles.IsList(list));
  return VLTVehicle;
}

class WorldValidateOutVehicle
{
  const World *_world;
  Entity *_outVehicle;
public:
  WorldValidateOutVehicle(const World *world, Entity *outVehicle) :_world(world),_outVehicle(outVehicle) {}
  __forceinline bool operator()(Entity *veh) const {return _outVehicle == veh;}
};

bool World::ValidateOutVehicle(Entity *veh) const
{
  // check if vehicle is correctly maintained in all lists
  // it should not be present in vehicle list
  bool ok = !ForEachVehicle(WorldValidateOutVehicle(this,veh));
  // it should not be present in the landscape
  for (int zz=0; zz<LandRange; zz++)
  for (int xx=0; xx<LandRange; xx++)
  {
    const ObjectList &list=GLandscape->GetObjects(zz,xx);
    for (int i=0; i < list.Size(); i++)
    {
      if (list[i] == veh)
        RptF("Out Vehicle %s in landscape (%d,%d)", (const char *)veh->GetDebugName(),xx,zz);
    }
  }
  return ok;
}

bool World::ValidateOutVehicles() const
{
  bool failure = ForEachOutVehicle(CallMember<const World>(this,&World::ValidateOutVehicle));
  return !failure;
}

class WorldVehicleVerifyStructure
{
  const World *_world;
public:
  explicit WorldVehicleVerifyStructure(const World *world):_world(world) {}
  bool operator()(Entity *vehicle) const
  {
    bool ret = vehicle->VerifyStructure();
    Assert(ret);
    if (vehicle->RefCounter()<2)
    {
      RptF("Vehicle %s not present in the landscape (RefCount)",(const char *)vehicle->GetDebugName());
      ret = false;
    }
    // check if it is really there
    ObjectId id = vehicle->GetObjectId();
    if (id.IsObject())
    {
      const ObjectList &list = GLandscape->GetObjects(id.GetObjZ(),id.GetObjX());
      bool found = false;
      for (int v=0;v<list.Size(); v++)
      {
        Object *o = list[v];
        if (o == vehicle)
          found = true;
      }
      if (!found)
      {
        RptF("Vehicle %s not present in the landscape, list %sused", (const char *)vehicle->GetDebugName(), !list.GetList() || list.GetList()->IsUsed() ? "" : "not ");
        ret = false;
      }
    }
    else
    {
      if (vehicle->GetType() == Primary)
      {
#if _HELISIM_LARGE_OBJECTID
        RptF("Primary vehicle %s has bad ID %I64x",(const char *)vehicle->GetDebugName(),id.Encode());
#else
        RptF("Primary vehicle %s has bad ID %x",(const char *)vehicle->GetDebugName(),id.Encode());
#endif
        ret = false;
      }
    }
    return !ret;
  }
};

class WorldVehicleVerifyUnitStructure
{
  const World *_world;
public:
  explicit WorldVehicleVerifyUnitStructure(const World *world):_world(world) {}
  bool operator () (Entity *vehicle) const
  {
    bool ret = true;
    EntityAI *veh = dyn_cast<EntityAI>(vehicle);
    if (veh)
    {
      AIBrain *unit = veh->CommanderUnit();
      if (unit)
      {
        if (!unit->AssertValid())
          ret = false;
      }
      unit = veh->PilotUnit();
      if (unit)
      {
        if (!unit->AssertValid())
          ret = false;
      }
      unit = veh->GunnerUnit();
      if (unit)
      {
        if (!unit->AssertValid())
          ret = false;
      }
    }
    if (!ret)
      LogF("Unit structure problem in %s",cc_cast(vehicle->GetDebugName()));
    return !ret;
  }
};

bool World::CheckVehicleStructure() const
{
  if(ForEachVehicle(WorldVehicleVerifyStructure(this)))
    return false;
  if (ForEachSlowVehicle(WorldVehicleVerifyStructure(this)))
    return false;
  if (ForEachVehicle(WorldVehicleVerifyUnitStructure(this)))
    return false;
  if (ForEachOutVehicle(WorldVehicleVerifyUnitStructure(this)))
    return false;
  return true;
}

inline bool IsPrimary(Object *vehicle)
{
  return (vehicle->GetType() == Primary) || (vehicle->GetType() == Network)
#if _ARMA3_TERRAIN_DECALS
    || (vehicle->GetType() == LandDecal)
#endif     
    ;
}

/// return true when there is a problem
static bool VerifyObjectId(Object *obj)
{
  if (obj->ID()==VISITOR_UNINIT_ID)
  {
    LogF("%s: Id not initialized",(const char *)obj->GetDebugName());
    return true;
  }
  switch (obj->GetType())
  {
    case Primary:
    case Network:
#if _ARMA3_TERRAIN_DECALS
    case LandDecal:
#endif  
      if (obj->ID()<0)
      {
        LogF("%s: No id",(const char *)obj->GetDebugName());
        return true;
      }
      return !obj->GetObjectId().IsObject();
    case TypeVehicle:
      {
        ObjectId oid = obj->GetObjectId();
        VisitorObjId vid = obj->ID();
        if (oid.IsObject())
        {
          LogF("%s: Object Id for vehicle",(const char *)obj->GetDebugName());
          return true;
        }
        if (vid<0)
        {
          LogF("%s: Negative visitor Id for vehicle",(const char *)obj->GetDebugName());
          return true;
        }
        if (oid.IsNull())
        {
          LogF("%s: Null object Id for vehicle",(const char *)obj->GetDebugName());
          return true;
        }
        return oid.GetVehId()!=vid;
      }
      break;
    case Temporary: // temporaty objects need no ID
    case TypeTempVehicle:
      return false;
    default:
      LogF("%s: Unknown vehicle type",(const char *)obj->GetDebugName());
      return true;
  }
}

void World::ResetIDs() const
{
  // verify all objects have correct ID assigned
  // the only objects which need ID are those that are referenced
  // this is verified during Object::SaveRef
//  DoAssert(!ForEachVehicle(VerifyObjectId));
//  DoAssert(!ForEachSlowVehicle(VerifyObjectId));
//  DoAssert(!ForEachFastVehicle(VerifyObjectId));
//  DoAssert(!ForEachOutVehicle(VerifyObjectId));
}

// top level

DEFINE_ENUM(SaveGameType, SGT, SAVE_GAME_TYPE_ENUM);

#include "saveVersion.hpp"

extern RString GetSaveName(SaveGameType type);
bool World::IsSaveGame(SaveGameType type)
{
  RString dir = GetSaveDirectory();
  if (dir.GetLength() == 0) return false;

  RString name = GetSaveName(type);
  return QIFileFunctions::FileExists(dir + name);
}

void World::EnableSaving(bool enable, bool save)
{
  if (!enable && save)
  {
    // we cannot save directly from the script, simulation is requested to do so
    _autoSaveRequest = true;
    _disableSavingRequest = true;
  }
  else
  {
    _savingEnabled = enable;
    _disableSavingRequest = false;
  }
}

LSError World::LoadGame(SaveGameType type, bool disableWarning)
{
  int ids = -1;
  switch (type)
  {
  case SGTAutosave:
  case SGTUsersave:
  case SGTContinue:
  case SGTUsersave2:
  case SGTUsersave3:
  case SGTUsersave4:
  case SGTUsersave5:
  case SGTUsersave6:
    ids = IDS_LOAD_GAME;
    break;
  default:
    Fail("Save game type");
    return LSFileNotFound;
  }

#if _ENABLE_PERFLOG
  LogF("<ignore>");
#endif

  RString name = GetSaveName(type);

  RString dir = GetSaveDirectory();
  LSError ok = LSFileNotFound;
  if (dir.GetLength() > 0)
  {
    if (QIFileFunctions::FileExists(dir + name))
    {
      // for Load in MP we need to destroy all possible earlier created objects
      GetNetworkManager().DestroyAllObjects();
      Ref<ProgressHandle> p = BegLoadProgress(ids);
      ok = LoadBin(dir + name, ids, disableWarning);
      EndLoadSaveProgress(p);
    }
  }

#if _ENABLE_PERFLOG
  LogF("</ignore>");
#endif
#ifndef _SUPER_RELEASE
  if (ok==LSOK) LogF("Game LOADED from Save.");
#endif
  return ok;
}

bool World::SaveGame(SaveGameType type)
{
  // indicate game was paused during the save
  _gameWasNotPaused = false;
  int ids = -1;
  switch (type)
  {
  case SGTAutosave:
  case SGTContinue:
    // ids = IDS_AUTOSAVE_GAME;
    ids = IDS_DISP_XBOX_MESSAGE_SAVING_DONT_TURN_OFF;
    break;
  case SGTUsersave:
  case SGTUsersave2:
  case SGTUsersave3:
  case SGTUsersave4:
  case SGTUsersave5:
  case SGTUsersave6:
    // ids = IDS_SAVE_GAME;
    ids = IDS_DISP_XBOX_MESSAGE_SAVING_USER_DONT_TURN_OFF;
    break;
  default:
    Fail("Save game type");
    return false;
  }
  
#if _ENABLE_PERFLOG
  LogF("<ignore>");
#endif

  Ref<ProgressHandle> p = BegSaveProgress(ids);

  bool ok = false;
  
  RString name = GetSaveName(type);

  RString dir = GetSaveDirectory();
  if (dir.GetLength() > 0)
  {
    ok = SaveBin(dir + name, ids);
  }

  EndLoadSaveProgress(p);

#if _ENABLE_PERFLOG
  LogF("</ignore>");
#endif

  // save is disrupting anyway - give opportunity to reset when needed
  _engine->RequestFlushMemory();
    
  return ok;
}

bool World::DeleteSaveGame(SaveGameType type)
{
  RString name = GetSaveName(type);
  RString dir = GetSaveDirectory();
  if (dir.GetLength() == 0) return false;
  // delete saved file
  return QIFileFunctions::CleanUpFile(dir + name);
}

Ref<ProgressHandle> World::BegLoadProgress(int message) const
{
/*
  Ref<ProgressHandle> progress =  ProgressStart(LocalizeString(message));
  return progress;
*/
  IProgressDisplay *CreateDisplayLoadIsland(RString text, RString date);
  // create progress
  Ref<ProgressHandle> p = ProgressStart(CreateDisplayLoadIsland(LocalizeString(message), RString()));
  return p;
}

Ref<ProgressHandle> World::BegSaveProgress(int message) const
{
  IProgressDisplay *CreateDisplayProgress(RString text, RString resource);

  Ref<ProgressHandle> progress = ProgressStartExt(true, 
    CreateDisplayProgress(LocalizeString(message), "RscDisplayNotFreezeBig"),
    false, 0, 3000);
  return progress;
}

extern const float CoefAdvanceUnit;

Ref<ProgressHandle> World::BegLoadMissionProgress(RString world, IMission &t, GameMode gameMode)
{
  // create display, set values
  RString name;
  RString date;
  bool showTime;
  if (gameMode == GModeIntro)
  {
    ConstParamEntryPtr entry = ExtParsMission.FindEntry("onLoadIntro");
    if (entry)
      name = *entry;
    else
      name = LocalizeString(IDS_LOAD_INTRO);
    entry = ExtParsMission.FindEntry("onLoadIntroTime");
    if (entry)
      showTime = *entry;
    else
      showTime = false;
  }
  else
  {
    ConstParamEntryPtr entry = ExtParsMission.FindEntry("onLoadMission");
    if (entry)
      name = *entry;
    else
      name = LocalizeString(IDS_LOAD_MISSION);
    entry = ExtParsMission.FindEntry("onLoadMissionTime");
    if (entry)
      showTime = *entry;
    else
      showTime = true;
  }

  if (showTime)
  {
    // set current time
    InitGeneral(t.Intel());

    char buffer[256];
    Glob.clock.FormatDate(LocalizeString(IDS_DATE_FORMAT), buffer); 
    float time = Glob.clock.GetTimeOfDay();
    if (time > 1.0) time--;
    time *= 24;
    int hour = toIntFloor(time);
    time -= hour;
    time *= 60;
    int min = toIntFloor(time);
    if (min >= 60)
    {
      hour++;
      min -= 60;
    }
    sprintf(buffer + strlen(buffer), ", % 2d:%02d", hour, min);
    date = buffer;
  }

  Ref<ProgressHandle> p;

  if (gameMode != GModeIntro)
  {
    RString GetBriefingFile();
    RString filename = GetBriefingFile();
    if (filename.GetLength() > 0)
    {
      ProgressDisplay *CreateDisplayLoadMission(RString filename, RString text, RString date);
      ProgressDisplay *display = CreateDisplayLoadMission(filename, name, date);
      if (display) p = ProgressStart(display);
    }
  }
  
  if (!p)
  {
    IProgressDisplay *CreateDisplayLoadIsland(RString text, RString date);
    // create progress
    p = ProgressStart(CreateDisplayLoadIsland(name, date));
  }

  // calculate total time

  // landscape size
  Landscape *land = _scene.GetLandscape();
  if (land)
  {
    const char *landName = land->GetName();
    RString filename = GetWorldName(world);
    if (!landName || strcmpi(filename, landName))
      ProgressAdd(QFBankQueryFunctions::GetFileSize(filename));
  }


  return p;
}

LSError World::Load(const char *name, int message)
{
  Ref<ProgressHandle> p = BegLoadProgress(message);
  Fail("Text load obsolete");
  GDebugger.NextAliveExpected(15*60*1000);

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamArchiveLoad ar(name, NULL, NULL, &globals);

  // during the load, some expressions are precompiled (in scripts and FSMs)
  // pass the correct namespace to enable compile-time check of global variables
  GameDataNamespace *oldGlobals = NULL;
  if (GetGameState()) oldGlobals = GetGameState()->SetGlobalVariables(GetMissionNamespace());

  ar.FirstPass();
  LSError err = Serialize(ar);
  if (err == LSOK)
  {
    ar.SecondPass();

    // note: instead of ambient life init we could created empty manager
    // and serialize ambient entities

    // create the manager prior entities will refer to ambient areas
    if ((Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
    {
      ParamEntryVal world=Pars>>"CfgWorlds">> Glob.header.worldname;
      InitAmbientLife(world>>"Ambient");
    }

    err = Serialize(ar);
  }

  if (GetGameState()) GetGameState()->SetGlobalVariables(oldGlobals);

  EndLoadSaveProgress(p);
  if (err == LSOK)
  {
    // TODO: displays hierarchy
  }
  else
  {
    RptF("Error '%s' at '%s'.", GetErrorName(err), (const char *)ar.GetErrorContext());
    ErrorMessage("Cannot load '%s'.", name);
  }
  return err;
}

LSError World::Save(const char *name, int message) const
{
  Ref<ProgressHandle> p = BegSaveProgress(message);
  Fail("Text save obsolete");
  GDebugger.NextAliveExpected(15*60*1000);
  LSError ret;
  {
    ParamArchiveSave ar(WorldSerializeVersion);
    World *w = const_cast<World *>(this);
    CHECK(w->Serialize(ar))
    ret = ar.Save(name) ? LSOK : LSUnknownError;
  }
  EndLoadSaveProgress(p);
  return ret;
}

static int NVehicles(ParamArchive &ar, RString name, const StaticEntityList &list)
{
  // nothing to serialize - no state maintained
  return 0;
}

static int NVehicles(ParamArchive &ar, RString name, const RefArray<Entity> &list)
{
  int n = 0;

  ParamArchive ar2;
  if (ar.OpenSubclass(name, ar2))
  {
    ar2.Serialize("items", n, 1, 0);
    ar.CloseSubclass(ar2);
  }
  return n;
}

static int NVehicles(ParamArchive &ar, RString name, const EntityList &list)
{
  int n = 0;

  ParamArchive ar2;
  if (ar.OpenSubclass(name, ar2))
  {
    ParamArchive ar3;
    if (ar2.OpenSubclass("Vehicles", ar3))
    {
      ar3.Serialize("items", n, 1, 0);
      ar2.CloseSubclass(ar3);
    }
    ar.CloseSubclass(ar2);
  }
  return n;
}

static int NVehicles(ParamArchive &ar, RString name, const EntitiesDistributed &list)
{
  int n = 0;

  ParamArchive ar2;
  if (ar.OpenSubclass(name, ar2))
  {
    n += NVehicles(ar2, "VisibleNear", list._visibleNear);
    n += NVehicles(ar2, "VisibleFar", list._visibleFar);
    n += NVehicles(ar2, "InvisibleNear", list._invisibleNear);
    n += NVehicles(ar2, "InvisibleFar", list._invisibleFar);
    ar.CloseSubclass(ar2);
  }
  return n;
}

static int NObjectsInLandscape(ParamArchive &ar)
{
  int n = 0;

  ParamArchive ar2;
  if (ar.OpenSubclass("Landscape", ar2))
  {
    ParamArchive ar3;
    if (ar2.OpenSubclass("Objects", ar3))
    {
      ar3.Serialize("items", n, 1, 0);
      ar2.CloseSubclass(ar3);
    }
    ar.CloseSubclass(ar2);
  }

  return n;
}

LSError World::LoadBin(const char *name, int message, bool disableWarning)
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  // clear the ChatList (old chats were still shown after the save). Maybe it will be serialized later.
  GChatList.Clear();
  GChatList.ScriptEnable(true);
  GCustomChannels.ClearCustomChannels();
  ClearMissionEventHandlers();

  FreeOnDemandGarbageCollectMain(4*1024*1024,1024*1024);
  LogF("LoadBin: Start - Total allocated: %d MB",MemoryUsed()/(1024*1024));
  LSError err;
  {
    GDebugger.NextAliveExpected(15*60*1000);
    ParamArchiveLoad ar;
    bool result = ar.LoadSigned(name, NULL, &globals);
    ProgressRefresh();
    if (!result)
    {
      if (!disableWarning)
      {
        const char *ptr = strrchr(name, '\\');
        ptr++;
        RString dir(name, ptr - name);

        ReportUserFileError(dir, ptr, RString(), false);
      }
      return LSBadFile;
    }

    // at this point we know the current game state is no longer valid and we may reset anything
    // (done after Xbox file integrity check has passed)
    if (_ui) _ui->ResetHUD();
    OnInitMission(false);
  
    // during the load, some expressions are precompiled (in scripts and FSMs)
    // pass the correct namespace to enable compile-time check of global variables
    GameDataNamespace *oldGlobals = NULL;
    if (GetGameState()) oldGlobals = GetGameState()->SetGlobalVariables(GetMissionNamespace());

    FreeOnDemandGarbageCollect(4*1024*1024,1024*1024);
    LogF("Load: Total allocated after ar.LoadBin: %d MB",MemoryUsed()/(1024*1024));
    ar.FirstPass();

    // calculate size of save
    // - landscape size
    Landscape *land = _scene.GetLandscape();
    if (land)
    {
      RString filename;
      ar.Serialize("worldName", filename, 1);

      const char *landName = land->GetName();
      if (!landName || strcmpi(filename, landName))
        ProgressAdd(QFBankQueryFunctions::GetFileSize(filename));
    }

    // - vehicles
    int nVehicles = 0;
    nVehicles += ::NVehicles(ar, "FastVehicles", _fastVehicles);
    nVehicles += ::NVehicles(ar, "Vehicles", _vehicles);
    nVehicles += ::NVehicles(ar, "Animals", _animals);
    nVehicles += ::NVehicles(ar, "SlowVehicles", _slowVehicles);
    nVehicles += ::NVehicles(ar, "VerySlowVehicles", _verySlowVehicles);
    nVehicles += ::NVehicles(ar, "OutVehicles", _outVehicles);
    nVehicles += ::NObjectsInLandscape(ar);
    ProgressAdd(CoefAdvanceUnit * nVehicles);

    err = Serialize(ar);
    if (err == LSOK)
    {
      ar.SecondPass();

      // note: instead of ambient life init we could created empty manager
      // and serialize ambient entities

      // create the manager prior entities will refer to ambient areas
      if ((Pars>>"CfgWorlds").FindEntry(Glob.header.worldname))
      {
        ParamEntryVal world=Pars>>"CfgWorlds">> Glob.header.worldname;
        InitAmbientLife(world>>"Ambient");
      }

      err = Serialize(ar);
    }
    if (err==LSOK || err==LSVersionTooNew || err==LSVersionTooOld)
    {
      // TODO: displays hierarchy
    }
    else if (err != LSNoAddOn)
    {
      RptF("Error '%s' at '%s'.", GetErrorName(err), (const char *)ar.GetErrorContext());
      ErrorMessage("Cannot load '%s'.", name);
    }
    LogF("Load: Total allocated after World::Serialize: %d MB",MemoryUsed()/(1024*1024));

    if (GetGameState()) GetGameState()->SetGlobalVariables(oldGlobals);
  }

  LogF("Total allocated after ~ParamArchive: %d MB",MemoryUsed()/(1024*1024));
  MemoryCleanUp();
  LogF("Total allocated after MemoryCleanUp: %d MB",MemoryUsed()/(1024*1024));
  if (!CheckObjectIds())
    RptF("Duplicate ObjectIds Detected!");
  return err;
}


bool World::SaveBin(const char *name, int message) const
{
  // flush some data as needed
  FreeOnDemandGarbageCollectMain(4*1024*1024,1024*1024);
  bool ret;
  {
    GDebugger.NextAliveExpected(15*60*1000);
    ParamArchiveSave ar(WorldSerializeVersion);
    World *w = const_cast<World *>(this);
    LogF("SaveBin: Start - Total allocated: %d MB",MemoryUsed()/(1024*1024));
    if (w->Serialize(ar) != LSOK) return false;
    

    LogF("Total allocated after World::Serialize: %d MB",MemoryUsed()/(1024*1024));

    FreeOnDemandGarbageCollect(4*1024*1024,1024*1024);
    
    ret = ar.SaveSigned(name);
    ProgressRefresh();
    LogF("Total allocated after ar.SaveBin: %d MB",MemoryUsed()/(1024*1024));
  }

  // during save quite a lot of memory can be often allocated
  // cleaning-up after save may help
  LogF("Total allocated after ~ParamArchive: %d MB",MemoryUsed()/(1024*1024));
  MemoryCleanUp();
  LogF("Total allocated after MemoryCleanUp: %d MB",MemoryUsed()/(1024*1024));
  if (!CheckObjectIds())
    RptF("Duplicate ObjectIds Detected!");
  return ret;
}

Entity *NewNonAIVehicle(const EntityType *type, bool fullCreate);
Entity *NewNonAIVehicle(RStringVal typeName, RStringVal shapeName, bool fullCreate);
EntityAI *NewVehicle(RStringVal typeName, RStringVal shapeName, bool fullCreate);
EntityAI *NewVehicle(const EntityAIType *type, bool fullCreate);

EntityAI *World::NewVehicleWithID(RStringVal typeName, RStringVal shapeName, bool fullCreate)
{
  EntityAI *veh = ::NewVehicle(typeName,shapeName,fullCreate);
  if (veh) veh->SetID(GLandscape->NewObjectID());
  return veh;
}

EntityAI *World::NewVehicleWithID(const EntityAIType *type, bool fullCreate)
{
  EntityAI *veh = ::NewVehicle(type,fullCreate);
  if (veh)
    veh->SetID(GLandscape->NewObjectID());
  return veh;
}

Entity *World::NewNonAIVehicleWithID( const EntityType *type, bool fullCreate)
{
  Entity *veh = ::NewNonAIVehicle(type,fullCreate);
  if (veh)
    veh->SetID(GLandscape->NewObjectID());
  return veh;
}

Entity *World::NewNonAIVehicleWithID(RStringVal typeName, RStringVal shapeName, bool fullCreate)
{
  Entity *veh = ::NewNonAIVehicle(typeName,shapeName,fullCreate);
  if (veh)
    veh->SetID(GLandscape->NewObjectID());
  return veh;
}

Entity *World::NewNonAIVehicle(RStringVal typeName, RStringVal shapeName, bool fullCreate)
{
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  Entity *veh = ::NewNonAIVehicle(typeName,shapeName,fullCreate);
  return veh;
}

void World::CheckHash(int bankIndex, CheckHashReason reason)
{
  if (bankIndex < 0)
  {
    if (_checkHashQueue.Size() > 0 || _checkingHash)
    {
      LogF("Cannot add all banks when check is in progress");
      return;
    }
    // indices of banks with configs (only these need to be checked)
    AUTO_STATIC_ARRAY(int, indices, 32);
    BankList::ReadAccess banks(GFileBanks);
    for (int i=0; i<banks.Size(); i++)
    {
      if (banks[i].VerifySignature())
        indices.Add(i);
    }
    for (int j=0; j<indices.Size(); j++)
    {
      int i = toIntFloor((j + 1) * GRandGen.RandomValue());
      CheckHashItem &item = _checkHashQueue.Insert(i);
      item._bankIndex = indices[j];
      item._reason = reason;
    }
  }
  else
  {
    int i = _checkHashQueue.FindKey(bankIndex);
    if (i != 0) // if currently checking, do not change order
    {
      if (i > 0)
        // remove current occurrence
        _checkHashQueue.DeleteAt(i);
      CheckHashItem &item = _checkHashQueue.Insert(std::min(1, _checkHashQueue.Size()));
      item._bankIndex = bankIndex;
      item._reason = reason;
    }
  }
}

int BankIndex(RString filename)
{
  BankList::ReadAccess banks(GFileBanks);
  QFBank *bank = QFBankQueryFunctions::AutoBank(filename);
  if (bank)
  {
    for (int i=0; i<banks.Size(); i++)
    {
      if (&banks[i]==bank)
        return i;
    }
  }
  return -1;
}

void World::CheckHash(RString filename, CheckHashReason reason)
{
  int bankIndex = BankIndex(filename);
  CheckHash(bankIndex, reason);
}

/// functor - call on all vehicles before simulating them
class CallInit
{
public:
  __forceinline bool operator()(Entity *veh) const
  {
    veh->Init(veh->GetFrameBase(), false);
    return false;
  }
};

LSError World::SerializeVehicles(ParamArchive &ar)
{
  // TODO: CHECK(ar.Serialize("Cloudlets", _cloudlets, 1))
  CHECK(ar.Serialize("FastVehicles", _fastVehicles, 1))
  CHECK(ar.Serialize("Vehicles", _vehicles, 1))
  CHECK(ar.Serialize("Animals", _animals, 1))
  CHECK(ar.Serialize("SlowVehicles", _slowVehicles, 1))
  CHECK(ar.Serialize("VerySlowVehicles", _verySlowVehicles, 1))
  CHECK(ar.Serialize("OutVehicles", _outVehicles, 1))

  if (ar.IsLoading() && ar.GetPass()==ParamArchive::PassSecond)
  {
    // call init for all vehicles
    ForEachVehicle(CallInit());
    ForEachAnimal(CallInit());
    ForEachSlowVehicle(CallInit());
    _scene.GetLandscape()->InitObjectVehicles();

    for (int i=0; i<_outVehicles.Size(); i++)
    {
      // set MoveOut flags of all "out" vehicles to true
      Entity *veh = _outVehicles[i];
      veh->SetMoveOutFlag();
      // parent should be serialized in vehicle
    }
    
  }
  CHECK(::Serialize(ar, "NearImportance", _nearImportanceDistributionTime, 1))
  CHECK(::Serialize(ar, "FarImportance", _farImportanceDistributionTime, 1))
  return LSOK;
}

LSError FSMItem::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("FSM", _fsm, 1))
  CHECK(ar.Serialize("age", _age, 1, 0))
  return LSOK;
}

LSError ScriptItem::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("Script", _script, 1))
  CHECK(ar.Serialize("age", _age, 1, 0))
  return LSOK;
}

#if USE_PRECOMPILATION
LSError ScriptVMItem::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("Script", _script, 1))
  CHECK(ar.Serialize("age", _age, 1, 0))
  return LSOK;
}
#endif

static const EnumName GameModeNames[]=
{
  EnumName(GModeNetware, "NETWARE"),
  EnumName(GModeArcade, "ARCADE"),
  EnumName(GModeIntro, "INTRO"),
  EnumName(GModeArcade, "NORMAL"),
  EnumName(GModeArcade, "TRAINING"),
  EnumName()
};

template<>
const EnumName *GetEnumNames( GameMode dummy )
{
  return GameModeNames;
}

bool ProcessTemplateName(RString name);
bool ProcessFullName(RString name);
#if !_HLA 
  bool ParseMission(bool multiplayer, bool avoidCheckIds = false, bool allowEmpty = false);
#endif
void StartMission();
//bool IsHeader();

struct InsideSerializationScope
{
  InsideSerializationScope() {GWorld->SetIsInsideSerialization(true);}
  ~InsideSerializationScope() {GWorld->SetIsInsideSerialization(false);}
};

LSError World::Serialize(ParamArchive &ar)
{
  // Global variable GWorld._isInsideSerialization is used not to reset Glob.time
  // inside World::CleanUpInit, which is called from more places in this function
  InsideSerializationScope enterIsInsideSerialization;
  
  // serialization and test of APP_VERSION_MAJOR & APP_VERSION_MINOR
  if (ar.IsLoading())
  {
    if (ar.GetPass() == ParamArchive::PassFirst)
    {
      int verMinor, verMajor;
      ar.Serialize("appVersionMajor", verMajor, 1, 0);
      ar.Serialize("appVersionMinor", verMinor, 1, 0);
      if (verMajor != APP_VERSION_MAJOR || verMinor != APP_VERSION_MINOR)
      {
        CreateWarningMessage(LocalizeString(IDS_INCOMPATIBLE_LOAD_GAME_ATTEMPT)); //"Cannot load the saved game. Incompatible versions."
        return (verMajor < APP_VERSION_MAJOR || (verMajor==APP_VERSION_MAJOR && verMinor<APP_VERSION_MINOR)) ? LSVersionTooOld : LSVersionTooNew;
      }
    }
  }
  else
  {
    int verMinor = APP_VERSION_MINOR;
    int verMajor = APP_VERSION_MAJOR;
    CHECK(ar.Serialize("appVersionMajor", verMajor, 1, 0));
    CHECK(ar.Serialize("appVersionMinor", verMinor, 1, 0));
  }
  
  // Glob.time serialization is done using its inner integer value serialization
  // note: all other Time variables are serialized as a relative value to Glob.time
  //       Now, when Glob.time is serialized too, it is not necessary to serialize all Time variables 
  //       as relative values, but it is so in order to be able to load old saves.
  if (ar.IsLoading())
  {
    int gtime;
    if (ar.GetPass() == ParamArchive::PassFirst)
    {
      CHECK(ar.Serialize("GlobTime", gtime, 1, 0));
      Glob.time.setInt(gtime);
    }
  }
  else
  {
    int gtime = Glob.time.toInt();
    CHECK(ar.Serialize("GlobTime", gtime, 1, 0));
  }

  CHECK(ar.Serialize("CurrentMission", CurrentMission, 1, ""))
  
  CHECK(ar.SerializeEnum("mode", _mode, 1))
  _modeBackup = _mode;
  CHECK(ar.Serialize("endMission", _endMission, 1, FindEnumName(EMContinue)))
  CHECK(ar.Serialize("endMissionCheated", _endMissionCheated, 1, false))

  CHECK(ar.Serialize("savingEnabled", _savingEnabled, 1, true))

  if (ar.IsSaving())
  {
    RString difficulty = Glob.config.diffNames.GetName(Glob.config.difficulty);
    CHECK(ar.Serialize("difficulty", difficulty, 1))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    RString difficulty;
    CHECK(ar.Serialize("difficulty", difficulty, 1, RString()))
    Glob.config.difficulty = Glob.config.diffDefault;
    int value = Glob.config.diffNames.GetValue(difficulty);
    if (value >= 0) Glob.config.difficulty = value;
  }

  if (ar.IsSaving())
  {
    bool subtitles = GChatList.ScriptEnabled();
    CHECK(ar.Serialize("showSubtitles", subtitles, 1, true))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    bool subtitles;
    CHECK(ar.Serialize("showSubtitles", subtitles, 1, true))
    GChatList.ScriptEnable(subtitles);
  }

  CHECK(ar.Serialize("enableRadio", _enableRadio, 1, true))
  CHECK(ar.Serialize("enableSentences", _enableSentences, 1, true))
  CHECK(ar.Serialize("Radio", *_radio, 1))

  Landscape *land = _scene.GetLandscape();
  if (ar.IsSaving())
  {
    ResetIDs();
    //land->RebuildIDCache();

    RString worldName = land->GetName();
    CHECK(ar.Serialize("worldName", worldName, 1))

    
    AutoArray<RString> addons;
    for (int i=0; i<_activeAddons.GetSize(); i++)
      addons.Add(_activeAddons.Get(i));
    CHECK(ar.SerializeArray("addons",addons,1))

    // Set IDs of locations
    for (int i=0; i<_locations.Size(); i++)
      _locations[i]->_id = -i - 1;

#if _ENABLE_INDEPENDENT_AGENTS
    // Set IDs of teams
    for (int i=0; i<_teams.Size(); i++)
    {
      AITeam *team = _teams[i]->GetTeam();
      if (team)
        team->SetId(i);
    }
#endif
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    Clear();
    CurrentTemplate = new DummyMission();

    RString worldName;
    CHECK(ar.Serialize("worldName", worldName, 1))

    // extract world name from filename
    char islandName[256];
    GetFilename(islandName,worldName);
    ProgressRefresh();

    SwitchLandscape(islandName);
    ProgressRefresh();

    ProgressRefresh();

    if (!_map) CreateMainMap();
    ProgressRefresh();

    FindArrayRStringCI addons;
    CHECK(ar.SerializeArray("addons",addons,1))

    // check presence of addons
    bool CheckMissingAddons(FindArrayRStringCI &addOns);
    CheckMissingAddons(addons);
    /*
    if (!CheckMissingAddons(addons))
      return LSNoAddOn;
    */

    // add addons from mission template
    ActivateAddons(addons,worldName);
    /// 
  }
  ProgressRefresh();
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  CHECK(ar.Serialize("Landscape", *land, 1))
  FreeOnDemandGarbageCollect(1024*1024,256*1024);

  // FIX: need to be prior serialization of GameState - during SetMission, some variables are defined
  if (ar.IsSaving())
  {
    bool userMission = IsUserMission();
    CHECK(ar.Serialize("userMission", userMission, 1, false))
    RString dir = GetBaseDirectoryRaw();
    CHECK(ar.Serialize("directory", dir, 1, ""))
    dir = GetBaseSubdirectory();
    CHECK(ar.Serialize("subdirectory", dir, 1, ""))
    RString mission = Glob.header.filename;
    CHECK(ar.Serialize("mission", mission, 1, ""))
    RString params = GetMissionParameters();
    CHECK(ar.Serialize("missionParameters", params, 1, ""))
    RString displayName = GetMissionParametersDisplayName();
    CHECK(ar.Serialize("missionDisplayName", displayName, 1, ""));
    CHECK(ar.Serialize("filenameReal", Glob.header.filenameReal, 1, ""))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    // TODO: remove Glob.header
    char wname[256];
    strcpy(wname, land->GetName());
    char *ext = strrchr(wname, '.');
    if (ext)
      *ext = 0;
    char *world = strrchr(wname, '\\');
    if (world)
      world++;
    else world = wname;

    bool userMission;

    RString dir;
    CHECK(ar.Serialize("userMission", userMission, 1, false))
    CHECK(ar.Serialize("directory", dir, 1, ""))
    SetBaseDirectory(userMission, dir, false); // do not cleanup during load
    RString mission;
    RString params, displayName;
    CHECK(ar.Serialize("mission", mission, 1, ""));
    CHECK(ar.Serialize("missionParameters", params, 1, ""));
    CHECK(ar.Serialize("missionDisplayName", displayName, 1, ""));
    CHECK(ar.Serialize("subdirectory", dir, 1, ""));
    SetMission(world, mission, dir, params, displayName, false); // do not cleanup during load
    CHECK(ar.Serialize("filenameReal", Glob.header.filenameReal, 1, ""))

    // TODO: verify: can CurrentTemplate = new IMission be done here? (moving into ParseMission)
    ParseMission(_modeBackup==GModeNetware); //modeBackup is used because _mode is changed in Clear, SwitchLandscape, ...
  }

  // serialize MissionEventHandlers
  for (int i=0; i<NMissionEvent; i++)
  {
    const char *eventName = FindEnumName((MissionEvent)i);
    BString<80> eHandlerName;
    sprintf(eHandlerName,"MEHandlers%s",eventName);
    CHECK(ar.SerializeArray(eHandlerName.cstr(), _missionEventHandlers[i], 1))
  }

  // serialize CustomChannels after the SetMission call (as SetActiveChannels can skip the ActualMsg transmit)
  for (int i=0; i<CCCustomN; i++)
  {
    RString customName = Format("CustomRadio%d", i);
    CHECK(ar.Serialize(customName, *_customRadios[i], 1))
  }

  // FIX: Crash
  if (_map)
  {
    CHECK(_map->Serialize(ar))
  }
  //_map
  //LSError SerializeMapInfo(ParamArchive &ar, RString name, int minVersion);
  //CHECK(SerializeMapInfo(ar, "Map", 11))

  // Glob.config view distance was saved here in old versions
  // this is now done by serializing _viewDistanceXXX members instead
  CHECK(ar.Serialize("viewDistMin",_viewDistanceMin,0,0))
  CHECK(ar.Serialize("viewDistHard",_viewDistanceHard,0,0))
  CHECK(ar.Serialize("terrainGridHard",_terrainGridHard,0,0))
  // when value is not saved, it is from old version and no new features should be enabled
  CHECK(ar.Serialize("aiFeatures",Glob.header.aiFeatures,0,0))
  
  CHECK( GEngine->SerializeGameState(ar) );

  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
  {
    // adjust terrain subdivision as necessary
    // this needs to be done before loading vehicles
    AdjustSubdivision(GModeArcade);
  }
   
  CHECK(SerializeVehicles(ar))

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  CHECK(ar.Serialize("Locations", _locations, 18))

#if _ENABLE_INDEPENDENT_AGENTS
  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  CHECK(ar.SerializeRefs("Agents", _agents, 19))

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  CHECK(ar.Serialize("Teams", _teams, 19))
#endif

  CHECK(ar.Serialize("enableItemsDropping",_enableItemsDropping, 1, true))

  CHECK(ar.Serialize("artilleryEnabled",_artilleryEnabled, 1, true))

  CHECK(::Serialize(ar,"moveMinBorders",_moveMinBordersHUD,1,VZero))
  CHECK(::Serialize(ar,"moveMaxBorders",_moveMaxBordersHUD,1,VZero))
  CHECK(::Serialize(ar,"movePositionHUD",_movePositionHUD,1,VZero))
  CHECK(ar.SerializeRef("targetHUD", _targetHUD, 1))

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  CHECK(ar.Serialize("SensorList", _sensorList, 1))
  CHECK(ar.Serialize("Clock", Glob.clock, 1))

  {
    void *oldParams = ar.GetParams();
    ar.SetParams(&GGameState); // needed to create variables of registered types
    DoAssert(_missionNamespace->IsSerializationEnabled());
    CHECK(ar.Serialize("GameState", _missionNamespace->GetVariables(), 1)) // serialize only variables stored in mission namespace
    ar.SetParams(oldParams);
  }

  CHECK(ar.Serialize("actualOvercast", _actualOvercast, 1))
  CHECK(ar.Serialize("wantedOvercast", _wantedOvercast, 1))
  CHECK(ar.Serialize("actualFog", _actualFog, 1))
  CHECK(ar.Serialize("wantedFog", _wantedFog, 1))
  CHECK(ar.Serialize("actualWind", _actualWind, 1))
  CHECK(ar.Serialize("wantedWind", _wantedWind, 1))
  CHECK(ar.Serialize("speedOvercast", _speedOvercast, 1))
  CHECK(ar.Serialize("weatherTime", _weatherTime, 1))
  CHECK(ar.Serialize("nextWeatherChange", _nextWeatherChange, 1))

  if (_mode!=GModeNetware || ar.IsSaving())
  { // do not load it during MPLoad
    CHECK(ar.SerializeRef("playerOn", _playerOn, 1))
    CHECK(ar.SerializeRef("cameraOn", _cameraOn, 1))
    CHECK(ar.SerializeRef("realPlayer", _realPlayer, 1))
    CHECK(ar.Serialize("playerManual", _playerManual, 1, true))
    CHECK(ar.Serialize("playerSuspended", _playerSuspended, 1, false))
  }

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  CHECK(AIGlobalSerialize(ar))

  CHECK(ar.Serialize("Scripts", _scripts, 3))

  /// prepare to serialize variables
  if (!GetGameState())
  {
    ErrF("Error: cannot serialize script - World::GetGameState() == NULL");
    return LSStructure;
  }
  void *oldParams = ar.GetParams();
  ar.SetParams(GetGameState());

#if USE_PRECOMPILATION
  if (ar.IsSaving())
  {
    // store only scripts which have serialization enabled
    AutoArray<ScriptVMItem> scripts;
    for (int i=0; i<_scriptVMs.Size(); i++)
    {
      const ScriptVMItem &item = _scriptVMs[i];
      if (item._script && item._script->IsSerializationEnabled())
        scripts.Add(item);
    }
    CHECK(ar.Serialize("VMs", scripts, 16))
  }
  else
  {
    CHECK(ar.Serialize("VMs", _scriptVMs, 16))
  }
#endif

  CHECK(ar.Serialize("FSMs", _FSMs, 22))
  CHECK(ar.Serialize("nextFSMId", _nextFSMId, 1, 1))

  CHECK(ar.Serialize("jvmFSMs", _jvmFSMs, 24))

  CHECK(ar.Serialize("nextJID", _nextJID, 1, 1))
  
  if (ar.IsSaving())
  {
    int size = _jEntities.card();
    CHECK(ar.Serialize("jEntitiesCount", size, 1, 0))
    int index = 0;
    for (ExplicitMap<int, OLinkO(Entity)>::Iterator i=_jEntities.begin(); i; _jEntities.next(i))
    {
      RString name = Format("jEntity%d", index++);
      int key = i.GetKey();
      OLinkO(Entity) &value = i.GetValue();
      CHECK(ar.Serialize(name + "Key", key, 1, 0))
      CHECK(ar.SerializeRef(name + "Value", value, 1))
    }
  }
  else if (ar.GetPass() == ParamArchive::PassSecond) // referenced entities needs to be loaded
  {
    int size;
    ar.FirstPass();
    CHECK(ar.Serialize("jEntitiesCount", size, 1, 0))
    ar.SecondPass();
    _jEntities.reset();
    for (int i=0; i<size; i++)
    {
      RString name = Format("jEntity%d", i);
      int key;
      OLinkO(Entity) value;
      ar.FirstPass();
      CHECK(ar.Serialize(name + "Key", key, 1, 0))
      ar.SecondPass();
      CHECK(ar.SerializeRef(name + "Value", value, 1))
      _jEntities.put(key, value);
    }
  }

  CHECK(::Serialize(ar, "OnMapSingleClick", GMapOnSingleClick, 1, GameValue()))
  CHECK(::Serialize(ar, "OnMapSingleClickParams", GMapOnSingleClickParams, 1, GameValue()))

  CHECK(::Serialize(ar, "OnCommandModeChanged", GOnCommandModeChanged, 1, GameValue()))

  CHECK(::Serialize(ar, "OnFrameEventHandler", GOnFrameEventHandler, 1, GameValue()))

  /// serialization of variables finished
  ar.SetParams(oldParams);
  
  CHECK(ar.Serialize("CameraEffect", _cameraEffect, 1))

  CHECK(ar.SerializeEnum("camType", _camTypeMain, 1, CamInternal));
  CHECK(ar.Serialize("cameraExternal", _cameraExternal, 1, false));

  // ui serialization
  if (UI())
  {
    if (ar.IsSaving())
    {
#if _ENABLE_CONVERSATION
      // conversation context
      ConversationContext *cc = UI()->GetConversationContext();
      if (cc && cc->_askingUnit)
      {
        CHECK(ar.SerializeRef("askingUnit", cc->_askingUnit, 1));
        CHECK(ar.SerializeRef("askedUnit", cc->_askedUnit, 1));
        KBSaveContext saveContext(cc->_askingUnit, &GGameState);
        void *oldParams = ar.SetParams(&saveContext);
        Ref<KBMessageInfo> question = unconst_cast(cc->_question.GetRef());
        CHECK(ar.Serialize("question", question, 1));
        ar.SetParams(oldParams);
      }
#endif
    }
    else if (ar.GetPass() == ParamArchive::PassSecond)
    {
#if _ENABLE_CONVERSATION
      // conversation context
      OLinkPermNO(AIBrain) askingUnit;
      OLinkPermNO(AIBrain) askedUnit;
      Ref<KBMessageInfo> question;
      CHECK(ar.SerializeRef("askingUnit", askingUnit, 1));
      CHECK(ar.SerializeRef("askedUnit", askedUnit, 1));
      if (askingUnit)
      {
        KBSaveContext saveContext(askingUnit, &GGameState);
        void *oldParams = ar.SetParams(&saveContext);
        // both passes are needed to create a message
        ar.FirstPass();
        CHECK(ar.Serialize("question", question, 1));
        ar.SecondPass();
        CHECK(ar.Serialize("question", question, 1));
        ar.SetParams(oldParams);
        UI()->ProcessConversation(askingUnit, askedUnit, question);
      }
#endif
    }
    CHECK(ar.Serialize("showHUD", UI()->_showHUD, 1, true));
  }
  ProgressRefresh();

  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
  {
    SetCameraType(_camTypeMain);
    InitCameraPos();

#if _ARMA3_SIMUL_WEATHER
    if (GLandscape && GLandscape->GetSimulWeather())
    {
      GLandscape->GetSimulWeather()->InitAfterLoad();
    }
#endif

    _scene.MainLight()->Recalculate(this);
    _scene.RecalculateLighting(1,_scene.GetLandscape());
    _scene.MainLight()->Recalculate(this);

    AIBrain *player = _playerOn ? _playerOn->Brain() : NULL;
    Glob.header.playerSide = player ? player->GetSide() : TSideUnknown;

    VehicleTypes.CleanUp();
    GEngine->TextBank()->Preload();

    GLandscape->SetOvercast(_actualOvercast,true);
    
    Shapes.OptimizeAll();

    // fixed - objectives not updated after load
    DisplayMap *map = dynamic_cast<DisplayMap *>((AbstractOptionsUI *)_map);
    if (map)
      map->UpdatePlan();
  }

  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    _mode = _modeBackup; //it was reset to Arcade mode (at least in Clear and SwitchLandcape)

  // Serialize Custom Radio Channels
  CHECK(GCustomChannels.Serialize(ar));

  if (GSoundScene) CHECK( GSoundScene->SerializeState(ar) );

#if _HELISIM_ROTORLIB
#if !_DUMMY_ROTORLIB
  _rtdWorld.Serialize(ar);
#endif

  CHECK(ar.Serialize("enableUserActions", _enableUserActions, 1, true))
  CHECK(ar.Serialize("enableStressDamage", _enableStressDamage, 1, true))
#endif

#if _HELISIM_RENDER_TO_TEXTURE

  CHECK(ar.Serialize("r2tSourceCamera", _r2tSourceCamera, 1));

#endif

  ProgressRefresh();
  return LSOK;
}

bool World::DoKeyDown(int dikCode)
{
  if (!IsUserInputEnabled()
#if _VBS3
    && dikCode != DIK_ESCAPE
#endif
    )
    return false;
  
#if _ENABLE_CHEATS
  if (GInput.cheat1 || GInput.cheat2)
    return false;
#endif

#if _VBS3 // key bindings
  for (int i=0; i<_boundkeys.Size(); i++)
  {
    if(!_boundkeys[i].isDik) continue;
    if (dikCode == _boundkeys[i].userKeyCode)
    {
      if (_boundkeys[i].command.GetLength() > 0)
      {
        bool alt = GInput.keys[DIK_LMENU]>0;
        bool shift = GInput.keys[DIK_LSHIFT]>0;

        GameState *state = GetGameState();
        if(state)
        {
          GameVarSpace vars(false);
          int gshandle = state->BeginContext(&vars);
          state->VarSetLocal("_alt",alt,true);
          state->VarSetLocal("_shift",shift,true);
          bool handled = state->EvaluateMultiple(_boundkeys[i].command, GameState::EvalContext::_default, GetMissionNamespace());
          state->EndContext(gshandle);
          if(handled)
          {
            GInput.DisableKey(dikCode);
            return false;
          }
        }
      }
    }
  }
#endif

  if (_warningMessage)
  {
    bool ok = _warningMessage->OnKeyDown(dikCode);
    if (_warningMessage->GetExitCode() >= 0) _warningMessage = NULL;
    return ok;
  }
  if (_voiceChat)
  {
    if (_voiceChat->DoKeyDown(dikCode))
      return true;
  }
  if (_chat)
  {
    if (_chat->DoKeyDown(dikCode))
      return true;
  }
#ifdef _WIN32
  if (_jUI)
  {
    if (_jUI->DoKeyDown(dikCode))
      return true;
  }
#endif
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && display->DoKeyDown(dikCode))
      return true;
  }
  if (!HasOptions())
  {
    if (_userDlg)
    {
      if (_userDlg->DoKeyDown(dikCode))
        return true;
    }
    else if (_map && _showMap && IsShowMapWanted())
    {
      if (_map->DoKeyDown(dikCode))
        return true;
    }
  }
  if (_options)
  {
    if (_options->DoKeyDown(dikCode))
      return true;
  }
  return false;
}

bool World::DoKeyUp(int dikCode)
{
  if (!IsUserInputEnabled()
#if _VBS3
    && dikCode != DIK_ESCAPE
#endif
    )
    return false;

#if _ENABLE_CHEATS
  if (GInput.cheat1 || GInput.cheat2)
    return false;
#endif

  if (_warningMessage)
  {
    bool ok = _warningMessage->OnKeyUp(dikCode);
    if (_warningMessage->GetExitCode() >= 0)
      _warningMessage = NULL;
    return ok;
  }
  if (_voiceChat)
  {
    if (_voiceChat->DoKeyUp(dikCode))
      return true;
  }
  if (_chat)
  {
    if (_chat->DoKeyUp(dikCode))
      return true;
  }
#ifdef _WIN32
  if (_jUI)
  {
    if (_jUI->DoKeyUp(dikCode))
      return true;
  }
#endif
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && display->DoKeyUp(dikCode))
      return true;
  }
  if (!HasOptions())
  {
    if (_userDlg)
    {
      if (_userDlg->DoKeyUp(dikCode))
        return true;
    }
    else if (_map && _showMap && IsShowMapWanted())
    {
      if (_map->DoKeyUp(dikCode))
        return true;
    }
  }
  if (_options)
  {
    if (_options->DoKeyUp(dikCode))
      return true;
  }
  return false;
}

void World::DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  if (!IsUserInputEnabled())
    return;

#if _ENABLE_CHEATS
  if (GInput.cheat1 || GInput.cheat2)
    return;
#endif

  if (_warningMessage)
  {
    _warningMessage->OnChar(nChar, nRepCnt, nFlags);
    if (_warningMessage->GetExitCode() >= 0)
      _warningMessage = NULL;
    return;
  }
  if (_voiceChat)
  {
    if (_voiceChat->DoChar(nChar,nRepCnt,nFlags))
      return;
  }
  if (_chat)
  {
    if (_chat->DoChar(nChar,nRepCnt,nFlags))
      return;
  }
#ifdef _WIN32
  if (_jUI)
  {
    if (_jUI->DoChar(nChar, nRepCnt, nFlags))
      return;
  }
#endif
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && display->DoChar(nChar,nRepCnt,nFlags))
      return;
  }
  if (!HasOptions())
  {
    if (_userDlg)
    {
      if (_userDlg->DoChar(nChar,nRepCnt,nFlags))
        return;
    }
    else if (_map && _showMap && IsShowMapWanted())
    {
      if (_map->DoChar(nChar,nRepCnt,nFlags))
        return;
    }
  }
  if (_options)
  {
    if (_options->DoChar(nChar,nRepCnt,nFlags))
      return;
  }
}

void World::DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
  if (!IsUserInputEnabled())
    return;

#if _ENABLE_CHEATS
  if (GInput.cheat1 || GInput.cheat2)
    return;
#endif

  if (_warningMessage)
  {
    _warningMessage->OnIMEChar(nChar, nRepCnt, nFlags);
    if (_warningMessage->GetExitCode() >= 0)
      _warningMessage = NULL;
    return;
  }
  if (_voiceChat)
  {
    if (_voiceChat->DoIMEChar(nChar,nRepCnt,nFlags))
      return;
  }
  if (_chat)
  {
    if (_chat->DoIMEChar(nChar,nRepCnt,nFlags))
      return;
  }
#ifdef _WIN32
  if (_jUI)
  {
    if (_jUI->DoIMEChar(nChar, nRepCnt, nFlags))
      return;
  }
#endif
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && display->DoIMEChar(nChar,nRepCnt,nFlags))
      return;
  }
  if (!HasOptions())
  {
    if (_userDlg)
    {
      if (_userDlg->DoIMEChar(nChar,nRepCnt,nFlags))
        return;
    }
    else if (_map && _showMap && IsShowMapWanted())
    {
      if (_map->DoIMEChar(nChar,nRepCnt,nFlags))
        return;
    }
  }
  if (_options)
  {
    if (_options->DoIMEChar(nChar,nRepCnt,nFlags))
      return;
  }
}

void World::DoIMEComposition(unsigned nChar, unsigned nFlags)
{
  if (!IsUserInputEnabled())
    return;

#if _ENABLE_CHEATS
  if (GInput.cheat1 || GInput.cheat2)
    return;
#endif

  if (_warningMessage)
  {
    _warningMessage->OnIMEComposition(nChar, nFlags);
    if (_warningMessage->GetExitCode() >= 0)
      _warningMessage = NULL;
    return;
  }
  if (_voiceChat)
  {
    if (_voiceChat->DoIMEComposition(nChar,nFlags))
      return;
  }
  if (_chat)
  {
    if (_chat->DoIMEComposition(nChar,nFlags))
      return;
  }
#ifdef _WIN32
  if (_jUI)
  {
    if (_jUI->DoIMEComposition(nChar, nFlags))
      return;
  }
#endif
  for (int i=0; i<_displays.Size(); i++)
  {
    AbstractOptionsUI *display = _displays[i]._display;
    if (display && display->DoIMEComposition(nChar,nFlags))
      return;
  }
  if (!HasOptions())
  {
    if (_userDlg)
    {
      if (_userDlg->DoIMEComposition(nChar,nFlags))
        return;
    }
    else if (_map && _showMap && IsShowMapWanted())
    {
      if (_map->DoIMEComposition(nChar,nFlags))
        return;
    }
  }
  if (_options)
  {
    if (_options->DoIMEComposition(nChar,nFlags))
      return;
  }
}


void World::SaveCrash() const
{
  RString filename = RString("$_crash_$.") + GetExtensionSave();
  SaveBin(filename, IDS_SAVE_GAME);
}

void SaveCrash()
{
  GWorld->SaveCrash();
}

CameraEffect::CameraEffect( Object *object )
: _object(object), _hudEnabled(false)
{
  // each camera effect is assigned to something
}

CameraEffect::~CameraEffect()
{
}

void CameraEffect::Draw() const
{
  // draw cinema borders
  if (showCinemaBorder)
  {
/*
    Object cinema(GScene->Preloaded(CinemaBorder),VISITOR_NO_ID);
    cinema.Draw2D(0); 
*/
    GWorld->DrawCinemaBorder(1.0f);
  }
}

#if _ENABLE_ADDONS
RString GetAddonArgumentPrefix();
bool IsAddonArgumentInstalled();
bool IsAddonArgumentMission();

static void CollectDirectories(const FileInfoO &fi, const FileBankType *files, void *context)
{
  FindArrayRStringCI *dirs = (FindArrayRStringCI *)context;

  const char *ptr = fi.name;
  const char *ext = strrchr(ptr, '\\');
  if (ext) ext++;
  else ext = ptr;

  if (stricmp(ext, "config.cpp") == 0 || stricmp(ext, "config.bin") == 0)
    dirs->AddUnique(fi.name.Substring(0, ext - ptr));
}

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
bool CheckKeys(ParamFile &f, const AutoArray<RString> &activeKeys);

static void AddAllMissions(FindArrayRStringCI &missions, ConstParamEntryPtr list, const AutoArray<RString> &activeKeys)
{
  for (int i=0; i<list->GetEntryCount(); i++)
  {
    ParamEntryVal entry = list->GetEntry(i);
    if (!entry.IsClass()) continue;
    // collect also hidden missions?
    ConstParamEntryPtr ptr = entry.FindEntry("hidden");
    if (ptr)
    {
      bool hidden = *ptr;
      if (hidden) continue;
    }
    ConstParamEntryPtr dir = entry.FindEntry("directory");
    if (dir)
    {
      // check if the mission is not locked
      ParamFile f;
      GameDataNamespace globals(NULL, RString(), false);  // TODO: parsing namespace if access to globals needed
      f.Parse(dir + RString("\\description.ext"), NULL, NULL, &globals);
      if (CheckKeys(f, activeKeys)) missions.AddUnique(*dir);
    }
    else
    {
      // subclass, recursive call
      AddAllMissions(missions, &entry, activeKeys);
    }
  }
}

struct AddonActions
{
  bool install;
  FindArrayRStringCI missions;
  FindArrayRStringCI entities;
  FindArrayRStringCI weapons;
};

struct CheckModDirCtx
{
  CListBoxContainer *container;
  int curRootDirLength;
  CheckModDirCtx(CListBoxContainer *cntr, RString curModDir) { container = cntr; curRootDirLength=curModDir.GetLength(); }
};

static char *modSubdirs[] = {"dta", "addons", "common", "bin"};
static bool CheckModDirectory(const FileItem &file, CheckModDirCtx &ctx)
{
  // Mod is a directory with at least one of the following directories
  //     addons
  if (file.directory)
  {
    for (int i=0; i<sizeof(modSubdirs)/sizeof(*modSubdirs); i++)
    {
      if ( stricmp(file.filename,modSubdirs[i])==0 )
      {
        // parent dir is mod
        RString modDir = file.path.Substring(ctx.curRootDirLength, file.path.GetLength()-1); //without trailing backslash

        if ( !modDir.IsEmpty() && strcmp(modDir,".")!=0 ) //not root dir
        {
          // try to get mod name from possible config file (mod.cpp)
          extern bool ParseModConfig(RString modFilePath, ModInfo &mod);
          RString modFilePath = file.path + "mod.cpp";
          ModInfo info; info.modDir = modDir;
          ParseModConfig(modFilePath, info);
          RString modName = info.GetName();
          int index = ctx.container->AddString(Format(LocalizeString(IDS_ADDON_ACTIONS_INSTALL_MOD), cc_cast(modName)));
          ctx.container->SetValue(index, AATInstall);
          ctx.container->SetData(index, modDir);
          return true; //do not recurse other subdirs
        }
      }
    }
  }
  return false; //continue
}

static bool AddModCallback(const FileItem &file, CheckModDirCtx &ctx)
{
  if (file.directory)
  {
    ForEachFile(file.path+file.filename+RString("\\"), CheckModDirectory, ctx);
  }
  return false;
}

static RString GetMissionName(RString dir)
{
  RString FastSearch(QIStream &file, const char *searchStr, int searchLen);
  const char *searchStr = "briefingName";
  int searchLen = strlen(searchStr);

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  // TODO: handling of locked missions
  RString displayName = dir;

  ParamFile f;
  if (f.ParseBin(dir + RString("\\mission.sqm"), NULL, NULL, &globals))
  {
    ParamEntryVal entry = f >> "Mission" >> "Intel";
    if (entry.FindEntry("briefingName")) displayName = entry >> "briefingName";
  }
  else
  {
    QIFStreamB file;
    file.AutoOpen(dir + RString("\\mission.sqm"));
    if (file.fail())
    {
      RptF("Invalid mission directory %s", cc_cast(dir));
      return RString(); // wrong directory reference
    }

    RString found = FastSearch(file, searchStr, searchLen);
    if (found.GetLength() > 0) displayName = found;
  }

  // load stringtable
  LoadStringtable("Temporary", dir + RString("\\stringtable.csv"), 10, true);
  displayName = Localize(displayName);
  UnloadStringtable("Temporary");

  return displayName;
}

/// functor will replace %1 by the string
class SimpleReplaceFunc
{
protected:
  RString _arg;

public:
  SimpleReplaceFunc(RString arg) : _arg(arg) {}

  RString operator ()(const char *&ptr)
  {
    // only %1 are recognized
    if (*ptr != '1') return RString();
    ptr++;
    return _arg;
  }
};

static void FillAddonActions(CListBoxContainer *container, const void *context)
{
  const AddonActions *actions = reinterpret_cast<const AddonActions *>(context);

  if (actions->install)
  {
    int index = container->AddString(LocalizeString(IDS_ADDON_ACTIONS_INSTALL_CORE));
    container->SetValue(index, AATInstall);
    container->SetData(index, RString());
    // add modes found in the my documents user profile directory
    RString curRootDir = GetDefaultUserRootDir()+RString("\\");
    CheckModDirCtx ctx(container, curRootDir);
    ForEachFile(curRootDir, AddModCallback, ctx);
    //EnumModDirectories(AddModCallback, container);
  }

  for (int i=0; i<actions->missions.Size(); i++)
  {
    // find the user friendly name of the mission
    RString displayName = GetMissionName(actions->missions[i]);
    if (displayName.GetLength() == 0) displayName = actions->missions[i]; // use the path when no friendly name found

    int index = container->AddString(Format(LocalizeString(IDS_ADDON_ACTIONS_PLAY_MISSION), cc_cast(displayName)));
    container->SetValue(index, AATPlayMission);
    container->SetData(index, actions->missions[i]);
  }

  for (int i=0; i<actions->entities.Size(); i++)
  {
    RString action = actions->entities[i];
    RString displayName = Pars >> "CfgVehicles" >> action >> "displayName";
    int index = container->AddString(Format(LocalizeString(IDS_ADDON_ACTIONS_TRY_ENTITY), cc_cast(displayName)));
    container->SetValue(index, AATTryEntity);
    container->SetData(index, action);
  }

  for (int i=0; i<actions->weapons.Size(); i++)
  {
    RString action = actions->weapons[i];
    RString displayName = Pars >> "CfgWeapons" >> action >> "displayName";
    int index = container->AddString(Format(LocalizeString(IDS_ADDON_ACTIONS_TRY_WEAPON), cc_cast(displayName)));
    container->SetValue(index, AATTryWeapon);
    container->SetData(index, action);
  }

  container->SetCurSel(0, false);
}
#endif

/// copy a single signature file
static bool CopySignature(const FileItem &file, RString &dstPath)
{
  RString src = file.path + file.filename;
  RString dst = dstPath + file.filename;
  QIFileFunctions::Copy(src, dst);
  return false;
}

/// check if given file is a signature
static bool TestSignature(const FileItem &arg, const char *name)
{
  if (arg.directory) return false;

  const char *filename = arg.filename;
  
  // first check if the file has a correct prefix
  int nameSize = strlen(name);
  if (strnicmp(filename, name, nameSize) != 0) return false;

  // ".<signature name>.bisign" need to follow
  if (filename[nameSize] != '.') return false;
  const char *ext = strchr(filename + nameSize + 1, '.');
  return ext && stricmp(ext, ".bisign") == 0;
}



void World::PerformAddonAction(int actionType, RString action, RString *modName)
{
#if _ENABLE_ADDONS
  switch (actionType)
  {
  case AATInstall:
    {
      // destination path
      RString dstPath = GetDefaultUserRootDir();
      int dstSize = dstPath.GetLength();
      if (dstPath[dstSize - 1] != '\\') dstPath = dstPath + "\\";
      if (action.GetLength() > 0)
      {
        dstPath = dstPath + action + "\\";
      }
      // if modName is present serialize its value into mod.cpp (create it if it does not exist yet)
      if (modName!=NULL)
      {
        RString modFileName = dstPath+"mod.cpp";
        CreatePath(modFileName);
        ParamFile modCpp;
        if (QIFileFunctions::FileExists(modFileName))
          modCpp.Parse(modFileName);
        modCpp.Add("name", *modName);
        modCpp.Save(modFileName);
      }
      dstPath = dstPath + "addons\\";

      // source path, file name
      RString srcPath;
      const char *name = strrchr(LoadFile, '\\');
      if (name)
      {
        name++;
        srcPath = RString(LoadFile, name - LoadFile);
      }
      else name = LoadFile;

      // copy the addon
      RString dst = dstPath + name;
      CreatePath(dst);
      QIFileFunctions::Copy(LoadFile, dst);

      // copy the signatures
      ForSomeFile(srcPath, PNothing(AFix2Func2(TestSignature, name)), CopySignature, dstPath);
    }
    break;
  case AATPlayMission:
    void StartMission(RString campaign, RString mission, bool skipBriefing, bool ignoreChild);
    StartMission(RString(), action, false, true);
    break;
  case AATTryEntity:
    {
      RString commandFormat = Pars >> "pboTryEntity";
      GameState *state = GetGameState();
      GameVarSpace local(state->GetContext(), false);
      int gshandle = state->BeginContext(&local);
      SimpleReplaceFunc func(action);
      GGameState.Execute(commandFormat.ParseFormat(func), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
      state->EndContext(gshandle);
    }
    break;
  case AATTryWeapon:
    {
      RString commandFormat = Pars >> "pboTryWeapon";
      GameState *state = GetGameState();
      GameVarSpace local(state->GetContext(), false);
      int gshandle = state->BeginContext(&local);
      SimpleReplaceFunc func(action);
      GGameState.Execute(commandFormat.ParseFormat(func), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
      state->EndContext(gshandle);
      break;
    }
  }
#endif
}

void World::StartIntro(RString worldName)
{
  // load mission or save game if passed as argument
  CurrentMission = "";
  
#if _ENABLE_PARAMS

  const char *ext = GetFileExt(LoadFile);
  if (QFBankQueryFunctions::FileExists(LoadFile))
  {
    if (!strcmpi(ext + 1, GetExtensionSave()))
    {
      // load saved game
      LoadBin(LoadFile, IDS_LOAD_GAME);
      ::StartMission();
      return;
    }
#if _GAMES_FOR_WINDOWS
    // profiles are handled by Guide
#else
    else if (!strcmpi(ext + 1, GetExtensionProfile()))
    {
      const char *from = strrchr(LoadFile,'\\');
      if (from && ext > from)
      {
        RString name = DecodeFileName(RString(from + 1, ext - from - 1));
        Glob.header.SetPlayerName(name);
        void UpdateUserProfile();
        UpdateUserProfile();
      }
    }
#endif
    if (!strcmpi(ext + 1, GetExtensionWizardMission()))
    {
      // launch the wizard mission
      void StartWizardMission(AbstractOptionsUI *display, RString filename);
      StartWizardMission(_options, LoadFile);
      return;
    }
#if _ENABLE_EDITOR
    else if( !strcmpi(ext,".sqm") )
    {
      if (ProcessFullName(LoadFile))
      {
        Fail("TODO: run mission");
        return;
    }
    }
#endif
#if _ENABLE_EDITOR2
    else if( !strcmpi(ext,".biedi") )
    {
      if (ProcessFullName(LoadFile))
      {
        void OpenEditor2();
        OpenEditor2();
        return;
      }
    }
#endif
#if _ENABLE_ADDONS
    else if (GetAddonArgumentPrefix().GetLength() > 0)
    {
      if (IsAddonArgumentMission())
      {
        // mission bank, play the contained mission
        PerformAddonAction(AATPlayMission, GetAddonArgumentPrefix());
        return;
      }
      else
      {
        QFBank *bank = QFBankQueryFunctions::AutoBank(GetAddonArgumentPrefix());
        if (bank)
        {
          // read all config files presented in the bank, enumerate all missions and entities
          FindArrayRStringCI dirs;
          bank->ForEach(CollectDirectories, &dirs);

          // find all we can do with the addon
          AddonActions options;
          options.install = !IsAddonArgumentInstalled(); // we can install the addon

          // do not include the locked missions - create list of active keys
          AutoArray<RString> activeKeys;
          ParamFile cfg;
          GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
          if (ParseUserParams(cfg, &globals))
          {
            ParamEntryPtr array = cfg.FindEntry("activeKeys");
            if (array)
            {
              for (int i=0; i<array->GetSize(); i++)
                activeKeys.Add((*array)[i]);
            }
          }

          for (int i=0; i<dirs.Size(); i++)
          {
            ParamFile config;
            if (bank->FileExists(dirs[i] + "config.cpp"))
              config.Parse(*bank, dirs[i] + "config.cpp", NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
            else
              config.ParseBin(*bank, dirs[i] + "config.bin", NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)

            // scan for missions
            ConstParamEntryPtr cls = config.FindEntry("CfgMissions");
            if (cls)
            {
              ConstParamEntryPtr list = cls->FindEntry("Missions");
              if (list) AddAllMissions(options.missions, list, activeKeys); // subclasses could be there, call recursively
            }

            // scan for entities
            cls = config.FindEntry("CfgVehicles");
            if (cls)
            {
              RString filterFormat = Pars >> "pboIsEntityLocked";

              GameState *state = GetGameState();
              GameVarSpace local(state->GetContext(), false);

              for (int i=0; i<cls->GetEntryCount(); i++)
              {
                RString name = cls->GetEntry(i).GetName();
                // to fully access the entry, we need to check the main config
                ParamEntryVal entry = Pars >> "CfgVehicles" >> name;
                if (!entry.IsClass()) continue;
                int scope = entry >> "scope";
                if (scope != 2) continue; // public

                // check if not locked
                int gshandle = state->BeginContext(&local);
                SimpleReplaceFunc func(name);
                bool locked = GGameState.EvaluateMultipleBool(filterFormat.ParseFormat(func), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
                state->EndContext(gshandle);
                if (locked) continue;

                options.entities.AddUnique(name);
              }
            }

            // scan for weapons
            cls = config.FindEntry("CfgWeapons");
            if (cls)
            {
              RString filterFormat = Pars >> "pboIsWeaponLocked";

              GameState *state = GetGameState();
              GameVarSpace local(state->GetContext(), false);

              for (int i=0; i<cls->GetEntryCount(); i++)
              {
                RString name = cls->GetEntry(i).GetName();
                // to fully access the entry, we need to check the main config
                ParamEntryVal entry = Pars >> "CfgWeapons" >> name;
                if (!entry.IsClass()) continue;
                int scope = entry >> "scope";
                if (scope != 2) continue; // public
                RString simulation = entry >> "simulation";
                if (stricmp(simulation, "weapon") != 0) continue;
                int type = entry >> "type";
                if ((type & (MaskSlotPrimary | MaskSlotSecondary | MaskSlotHandGun)) == 0) continue;

                // check if not locked
                int gshandle = state->BeginContext(&local);
                SimpleReplaceFunc func(name);
                bool locked = GGameState.EvaluateMultipleBool(filterFormat.ParseFormat(func), GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
                state->EndContext(gshandle);
                if (locked) continue;

                options.weapons.AddUnique(name);
              }
            }
          }

          // try to select automatic action
          if (!options.install)
          {
            // single mission
            if (options.missions.Size() == 1)
            {
              PerformAddonAction(AATPlayMission, options.missions[0]);
              return;
            }
            // no mission, single entity
            if (options.missions.Size() == 0 && options.entities.Size() == 1)
            {
              PerformAddonAction(AATTryEntity, options.entities[0]);
              return;
            }
            // no mission, no entity, single weapon
            if (options.missions.Size() == 0 && options.entities.Size() == 0 && options.weapons.Size() == 1)
            {
              PerformAddonAction(AATTryWeapon, options.weapons[0]);
              return;
            }
          }
          // let the user decide what to do if there are more options
          if (options.install || options.missions.Size() > 0 || options.entities.Size() > 0 || options.weapons.Size() > 0)
          {
            if (_options)
            {
              typedef void (*FillAddonActionsCallback)(CListBoxContainer *control, const void *context);
              void CreateAddonActionsOptions(AbstractOptionsUI *display, FillAddonActionsCallback callback, const void *context);
              CreateAddonActionsOptions(_options, &FillAddonActions, &options);
              // return;
            }
          }
        }
      }
    }
#endif
  }
  else if (strlen(LoadFile) > 0)
  {
    // first check if it might be a mission.sqm file
    RString filename = RString(LoadFile) + "\\mission.sqm";
    
    if (QIFileFunctions::FileExists(filename))
    {
#if _ENABLE_EDITOR
      if (ProcessFullName(filename))
      {
        Fail("TODO: run the mission");
        return;
      }
#endif
    }
    else
    {
      // nothing found, try as a user (wizard) mission    
      void StartUserMission(AbstractOptionsUI *display, RString name);
      StartUserMission(_options, LoadFile);
      return;
    }

  }

#endif // _ENABLE_PARAMS

  // No argument given
  {
      // start intro
      void StartRandomCutscene(RString world, bool runInitSqs=true);
      StartRandomCutscene(worldName,false);
    }
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
  static bool once = true;
  if (once)
  {
    once = false;
    int GetReadWaitTime();
    LogF("**** StartIntro done %d ms (%d ms waiting for i/o)", GlobalTickCount(), GetReadWaitTime());
  }
  #endif
}

World *GWorld;
AutoArray<BankHashes> GBankHashes;
AutoArray<CanonicalHash> GCanonicalBankHashes;
RString GLoadedContentHash;

#if 0
  #define HshLogF RptF
#else
  #define HshLogF NoLog
#endif
void GetCanonicalBanksHash()
{
  HshLogF("Canonical bank hashes list:");
  QSort(GCanonicalBankHashes, CanonicalHash::CmpByPrefix);
  HashCalculator calculator;
  for (int i=0; i<GCanonicalBankHashes.Size(); i++)
  {
    HshLogF("  %s ... %s", cc_cast(GCanonicalBankHashes[i].prefix), cc_cast(GCanonicalBankHashes[i].hash));
    calculator.Add(GCanonicalBankHashes[i].hash.Data(), GCanonicalBankHashes[i].hash.GetLength());
  }
  AutoArray<char> finalHash; 
  if (calculator.GetResult(finalHash))
  {
    GLoadedContentHash = ConvertToHex(finalHash.Data(), finalHash.Size(), false); //userUpperCase
    HshLogF("*** Final hash is: %s", cc_cast(GLoadedContentHash));
  }
}

#include <Es/Memory/normalNew.hpp>

World::World(Engine *engine, bool editor, bool skipPhysicsManager)
: _engine(engine),_editor(editor),
_camTypeMain(CamInternal), _camTypeOld(CamInternal),_camTypeNew(CamInternal),
_camTypeNewFactor(1),_camTypeTransition(1),
_gridOffsetX(0), _gridOffsetY(0),
_ambientLife(new AmbLifeManager),
_camUseNVG(false),
_camUseTi(false),
_camTiIndex(0),
_preloadAsked(false),
_endDialogEnabled(true),
_savingEnabled(true),
_leakRecoveryCounter(0),
_nextFSMId(1),
_nextJID(1),
_enableSentences(true),
_isInsideSerialization(false),
_insideSimulateScripts(false),
_elevationOffset(0),
_isCameraInsideVeh(false),
_actualOvercast(0.0f),
_wantedOvercast(0.0f),
_speedOvercast(0.0f),
_actualFog(0.0f),
_wantedFog(0.0f),
_actualWind(0.1f),
_wantedWind(0.1f),
_speedFog(0.0f),
_weatherTime(0.0f),
_nextWeatherChange(0),
_nextWeatherChangeOvercast(0),
_nextSchedulerUpdateZombieAI(0),
_nextSchedulerUpdateReSpawn(0),
_resetOptions(false),
_nearbyItemTransmittersChanged(false)
#if _HELISIM_ROTORLIB_SIM_THREAD
,_rtdThread()
#endif
#if _ENABLE_PHYSICS
,_pPhysicsManager(0)
#endif
{
#if _ENABLE_PHYSICS
  if(GetNetworkManager().IsPhysicsMaster())
  {
    _pPhysicsManager = new enf::BulletManager();

    if (!skipPhysicsManager)
    {
      if(!_pPhysicsManager->LoadMaterials(ENFORCE_PHYSICS_MATERIALS_FILE))
	    {
		    char ErrMsg[256];
		    enf::strcpy(ErrMsg, "Cannot load physics materials definition file ");
		    enf::strcat(ErrMsg, ENFORCE_PHYSICS_MATERIALS_FILE);
	    }
  

	    if(!_pPhysicsManager->LoadRagdolls(ENFORCE_PHYSICS_RAGDOLLS_FILE))
	    {
		    char ErrMsg[256];
		    enf::strcpy(ErrMsg, "Cannot load physics ragdolls definition file ");
		    enf::strcat(ErrMsg, ENFORCE_PHYSICS_RAGDOLLS_FILE);
	    }
    }
  }
#endif

  ResetNativeEvents();
  _missionNamespace = new GameDataNamespace(&GGameState, "mission", true, false); // the mission namespace will be serialized, do not AddRef
  _profileNamespace = new GameDataNamespace(NULL, "profile", true);
  #define PRINT_SIZE(type) Log(#type " size %d",sizeof(type))
  PRINT_SIZE(Object);
  PRINT_SIZE(Frame);
  PRINT_SIZE(RemoveLinks);
  PRINT_SIZE(OLinkObject);
  PRINT_SIZE(Entity);
  PRINT_SIZE(LODShape);
  PRINT_SIZE(Shape);
  if( editor )
  {
    _camTypeOld=_camTypeNew=_camTypeMain=CamInternal;
    _camTypeNewFactor = 1;
  }
  #if _PIII
    Assert( ((int)this&0xf)==0 );
  #endif

  _radio = new RadioChannel(CCGlobal, Pars >> "RadioChannels" >> "GlobalChannel");
  for (int i=0; i<CCCustomN; i++)
  {
    _customRadios[i]=new RadioChannel((::ChatChannel)(CCCustom1+i), Pars >> "RadioChannels" >> "GlobalChannel");
  }
  SetSpeaker(RString(), PITCH_DEFAULT);

  // position of special items 
  _watchInfo.Load(Pars >> "RscWatch");
  _compassInfo.Load(Pars >> "RscCompass");

  GCustomChannels.ClearCustomChannels();

  RString skyTexture = Pars>>"CfgWorlds">>"DefaultWorld">>"skyTexture";  
  RString skyTextureR = Pars>>"CfgWorlds">>"DefaultWorld">>"skyTextureR";  
  skyTexture.Lower();
  skyTextureR.Lower();
  _scene.Init(_engine, NULL, skyTexture, skyTextureR);

  GLandscape=_scene.GetLandscape();
  GScene=GetScene();
  _engine->SetSceneProperties(_scene.GetEngineProperties());
  
  _viewDistanceHard = 0;
  _viewDistanceMin = 0;
  _terrainGridHard = 0;
  
  _vsyncErrorApplied = 0;

  _latitude = -40*(H_PI/180); // -40 - Croatia, -90 - north pole;
  _longitude = +15*(H_PI/180);

#if _VBS3
  _waterLevel = 0.0f;
#endif

  Vector3 lightDirection(+1,-0.5,+1);
  LightSun *mainLight=new LightSun();

#ifdef _WIN32
  SYSTEMTIME time;
  GetLocalTime(&time);
  GRandGen.SetSeed(GlobalTickCount()+time.wSecond);
#else
  GRandGen.SetSeed(GlobalTickCount()+(unsigned)time(NULL));
#endif
  Glob.clock.SetTime(8 * OneHour, 130 * OneDay, 1985);
	Glob.clockWeather.SetTime(8 * OneHour, 130 * OneDay, 1985);

  mainLight->Recalculate(this);
  _timeToSkip=0;

  _scene.SetMainLight(mainLight);
  _scene.MainLightChanged();
  _noDisplay = false;
  _enableSimulation = true;
  _singleStepSimulation = false;
  _simulationFocus = true;
  #if !_DISABLE_GUI
  if (!editor)
  {
    _cinemaBorder = new ControlsContainer(NULL);
    _cinemaBorder->Load("RscCinemaBorder");
  }
  #endif

  _userInputDisabled = 0;

  _endMissionCheated = false;
  _endFailed = false;

/*  _jUI = new JVMUI(1024, 1024);
  if (!_jUI->IsValid()) _jUI = NULL*/;

  _moveMinBordersHUD = VZero;
  _moveMaxBordersHUD = VZero;
  _movePositionHUD = VZero;
  _targetHUD = NULL;
#if _ENABLE_CHEATS
  _cpuStress = 0;
#endif
  
#if _VBS2 // allow seagull difficulty option
  _lockTitleEffect = false;
#endif

#if _VBS2 // key bindings
  _boundKeyCount = 0;
  _boundkeys.Clear();
#endif

#if _VBS2 // interact with vehs
  _IWVActive = false;
  ConstParamEntryPtr patches = Pars.FindEntry("CfgPatches");
  if (patches)
  {
    for (int j=0; j<patches->GetEntryCount(); j++)
      if (stricmp("vbs2_plugins_iwv", patches->GetEntry(j).GetName()) == 0)
      {
        _IWVActive = true;
        break;
      }
  }
#endif

  Clear();

  /*
  // all centers are already created in Clear
  _eastCenter=new AICenter(TEast,AICMDisabled);
  _westCenter=new AICenter(TWest,AICMDisabled);
  _guerrilaCenter=new AICenter(TGuerrila,AICMDisabled);
  _civilianCenter=new AICenter(TCivilian,AICMDisabled);
  _logicCenter=new AICenter(TLogic,AICMDisabled);
  */
  _mode = GModeArcade;

  _cameraExternal = false;

  _showMiniMap = _showMiniMapToggle = false;

  _showMap = false;
  _showMapUntil = 0;
  
  _forceMap = false;
  _autoSaveRequest = false;
  _loadAutoSaveRequest = false;
  _disableSavingRequest = false;
  _showMapforced = false;

#if _HELISIM_ROTORLIB
  _enableUserActions = true;
  _enableStressDamage = true;
#endif

  if (GSoundScene)
    GSoundScene->Reset();

#if _ENABLE_CHEATS
  // Video capturing parameters
  _videoCaptureFrameDuration = -1.0f;
  _videoCaptureTimeToReadResources = -1.0f;
  _videoCaptureFrameID = -1;
#endif

#if _VBS3
  _pauseSimulation = false;
  _allowMovementCtrlsInDlg = false;
#endif

#if _VBS2 // death message difficulty option
  _friendlyFireMsgSideOnly = false;
#endif

#if _VBS3_UDEFCHART
  _drawObjectsOnUserDefinedChart = false;
#endif

  _lastTemperatureTableResetTimeInYear = -1.0f;
  for (int k = 0; k < TT_K; k++)
  {
    for (int j = 0; j < TT_Dot; j++)
    {
      for (int i = 0; i < TT_G; i++)
      {
        _temperatureTable[i][j][k] = 0.0f;
      }
    }
  }

#if _HELISIM_ROTORLIB && !_DUMMY_ROTORLIB
  // Initialize RTD simulation library
  RTD::unlock( 10,85,0,133,185,244); 
  RTD::init() ;
  // register new components
  ConstantRPMEngineArma::registerClass("ConstantRPMEngineArma",ConstantRPMEngineArma::getCreatorFunc("ConstantRPMEngineArma"), ConstantRPMEngineArma::getParametersCreatorFunc("ConstantRPMEngineArma"));
  FCSAutoHoverTOH::registerClass("FCSAutoHoverTOH",FCSAutoHoverTOH::getCreatorFunc("FCSAutoHoverTOH"), FCSAutoHoverTOH::getParametersCreatorFunc("FCSAutoHoverTOH"));
  FGGroundContact200Arma::registerClass("FGGroundContact200Arma",FGGroundContact200Arma::getCreatorFunc("FGGroundContact200Arma"), FGGroundContact200Arma::getParametersCreatorFunc("FGGroundContact200Arma"));

  // Set the airport as the local reference for our simulation
  _rtdWorld.setLocalCoordinateSystemCenter( 0.0f, 0.0f, 0.0f);

#if _HELISIM_ROTORLIB_SIM_THREAD
  _rtdThread.Init(this, 32*1024*1024, 2, THREAD_PRIORITY_NORMAL, "RTDSimulThread");
#endif
#endif
}

CustomChannelInfos GCustomChannels;

LSError CustomChannelInfo::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("active", active, 1));
  CHECK(ar.Serialize("callSign", callSign, 1));
  CHECK(ar.Serialize("label", label, 1));
  if (ar.IsLoading())
  {
    // note: second pass does not serialize ints, feed it with color value serialized in FirstPass
    int colVal = (int)color;
    CHECK(ar.Serialize("color", colVal, 1));
    color = PackedColor(colVal);
  }
  else
  {
    int colVal = (int)color;
    CHECK(ar.Serialize("color", colVal, 1));
  }
  // serialize units as RefArray<Person>
  RefArray<Person> perUnits;
  if (ar.IsSaving())
  {
    NORefArray2Persons(units, perUnits);
    CHECK(ar.SerializeRefs("units", perUnits, 1));
  }
  else
  {
    CHECK(ar.SerializeRefs("units", perUnits, 1));
    PersonsRefArray2NO(perUnits, units);
  }
  return LSOK;
}

int World::GetMartaType(const EntityType *type) const
{
  int ix = -1; //there is default 
  for (int i=0; i<_martaType.Size(); i++)
  {
    if (type->IsKindOf(_martaType[i]._type))
      ix = i;
  }
  return ix;
}

float World::GetMartaImportance(const EntityType *type) const
{
  int ix = GetMartaType(type);
  if (ix<0)
    return 1; //default cost
  return _martaType[ix]._importance;
}

float World::GetMartaEffectiveBattleDistance(const EntityType *type) const
{
  int ix = GetMartaType(type);
  if (ix<0)
    return 1; //default cost
  return _martaType[ix]._distanceLevels[0];
}

struct CompareMartaType
{
  int operator()(const World::MartaType *mt1, const World::MartaType *mt2)
  {
    return ( fSign(mt1->_importance - mt2->_importance) );
  }
};

void World::LoadMartaTypes()
{
  ParamEntryVal cfg = Pars >> "CfgMarkedTargets";
  for (int i=0; i<cfg.GetEntryCount(); i++)
  {
    ParamEntryVal entry = cfg.GetEntry(i);
    //LogF(" %s", cc_cast(entry.GetName()));
    _martaType.Add(MartaType(entry));
  }
  // sort by importance
  ::QSort(_martaType, CompareMartaType());
}

World::MartaType::MartaType(ParamEntryPar entry)
{
  _className = entry.GetName();
  _typeName = entry >> "type";
  _type = NewAIType(_typeName);
  _importance = entry >> "importance";
  _distanceLevels[0] = entry >> "battleRadius";
  _distanceLevels[1] = entry >> "farRadius";
  ParamEntryPtr trSld = entry.FindEntry("transportSoldiers");
  _transportSoldiers = trSld ? trSld->GetInt()!=0 : false;
}

static float DefaultRTChangeFactors[] = {3.0f, 2.5f, 2.0f, 1.6f, 1.3f};
static const float DefaultRTEmptyGroupCost = 0.4f;
AutoArray<float> TargetList::AggregatedTargets::_reportTargetsChangeFactors;
float TargetList::AggregatedTargets::_reportTargetsEmptyGroupCost;

void TargetList::LoadTargetAggregationStuff()
{
  ParamEntryPtr cls = Pars.FindEntry("ReportTargetsChangeFactors");
  if (cls && cls->IsArray() && cls->GetSize()>0)
  {
    int size = cls->GetSize();
    TargetList::AggregatedTargets::_reportTargetsChangeFactors.Realloc(size);
    TargetList::AggregatedTargets::_reportTargetsChangeFactors.Resize(size);
    for (int i=0; i<size; i++)
      TargetList::AggregatedTargets::_reportTargetsChangeFactors[i] = (*cls)[i];
  }
  else
  {
    int size = sizeof(DefaultRTChangeFactors)/sizeof(float);
    TargetList::AggregatedTargets::_reportTargetsChangeFactors.Realloc(size);
    TargetList::AggregatedTargets::_reportTargetsChangeFactors.Resize(size);
    for (int i=0; i<size; i++)
      TargetList::AggregatedTargets::_reportTargetsChangeFactors[i] = DefaultRTChangeFactors[i];
  }
  cls = Pars.FindEntry("ReportTargetsEmptyGroupCost");
  if (cls) TargetList::AggregatedTargets::_reportTargetsEmptyGroupCost = *cls;
  else TargetList::AggregatedTargets::_reportTargetsEmptyGroupCost = DefaultRTEmptyGroupCost;
}

void World::Init(bool editor)
{
  PreloadVehicles();
  ProgressRefresh();
  LoadMartaTypes();
  TargetList::LoadTargetAggregationStuff();

#if !_DISABLE_GUI || _DEDICATED_CLIENT
  if (!editor && !IsDedicatedServer())
    CreateMainOptions();
#endif
}

VideoBufferInfo::VideoBufferInfo(const char *name, OggDecoder *decoder)
: _decoder(decoder)//, _videoBufferSize(0), _videoBufferPos(0), _bufferPosForReading(0)
{
  DoAssert(CheckMainThread());

  _inVideoFile = new QIFStreamB();
  _inVideoFile->AutoOpen(name);
  if (_inVideoFile->fail())
  {
    delete _inVideoFile;
    _inVideoFile = NULL;
    return;
  };
}

VideoBufferInfo::~VideoBufferInfo()
{
  DoAssert(CheckMainThread());

  if (_inVideoFile)
  {
    delete  _inVideoFile;
    _inVideoFile = NULL;
  };
  
  _decoder.Free();
}

void VideoBufferInfo::Reset()
{
  if (_inVideoFile)
  {
    _inVideoFile->seekg(0, QIOS::beg);
  }
}

void World::PauseVideoBuffers(bool pause)
{
  for (int i = 0; i < _videoBufferInfo.Size(); i++)
  {
    Ref<VideoBufferInfo> vbi = _videoBufferInfo[i];
    if (vbi.NotNull())
    {
      if (vbi->_decoder)
      {
         vbi->_decoder->pause(pause);
      }
    }
  }
}

void World::TerminateVideoBuffers()
{
  if (_videoBufferInfo.Size())
  {
    // terminate all running ogg decoder threads
    for (int i = 0; i < _videoBufferInfo.Size(); i++)
    {
      Ref<VideoBufferInfo> vbi = _videoBufferInfo[i];
      if (vbi.NotNull())
      {
        DoAssert(vbi->_decoder.NotNull());
        if (vbi->_decoder)
        {
          vbi->_decoder->WaitUntilStopped();
        }
      }
    }

    _videoBufferInfo.Resize(0);
  }

  if (_toDeleteVideoBuffInfo.Size())
  {
    ReleaseVideoBuffers();
    _toDeleteVideoBuffInfo.Resize(0);
  }
}

int World::ResetVideoBuffer(int ident)
{
  if (ident >= 0 && ident < _videoBufferInfo.Size())
  {
    Ref<VideoBufferInfo> vbi = _videoBufferInfo[ident];
    if (vbi)
    {
      vbi->Reset();
      return ident;
    }
  }

  return -1;
}

int World::OpenVideoBuffer(const char *name, OggDecoder *decoder)
{
  for( int i=0; i<_videoBufferInfo.Size(); i++)
  {
    VideoBufferInfo* videoBufferInfo = _videoBufferInfo[i];
    if (videoBufferInfo == NULL)
    {
      _videoBufferInfo[i] = new VideoBufferInfo(name, decoder);
      return (_videoBufferInfo[i] && (_videoBufferInfo[i]->_inVideoFile != NULL) ? i : -1);
    }
  };

  Ref<VideoBufferInfo> vbi = new VideoBufferInfo(name, decoder);
  int addIndex = _videoBufferInfo.Add(vbi);
  return  ((vbi && vbi->_inVideoFile != NULL) ? addIndex : -1);
  
}

bool World::VideoBufferEof(int ident)
{
  DoAssert(ident >= 0 && ident < _videoBufferInfo.Size());

  VideoBufferInfo* videoBufferInfo = _videoBufferInfo[ident];  
  if (videoBufferInfo->_inVideoFile)
  {
      return videoBufferInfo->_inVideoFile->eof() && (videoBufferInfo->_cBuffer.CountPending(0) == 0);
  };
  return true;
}

void World::CloseVideoBuffer(int ident)
{
  DoAssert(ident >= 0 && ident < _videoBufferInfo.Size());

  VideoBufferInfo* videoBufferInfo = _videoBufferInfo[ident];
  if (videoBufferInfo != NULL)
  {
    _toDeleteVideoBuffInfo.Append(videoBufferInfo);
    _videoBufferInfo[ident] = NULL;
  }  
}

int World::CopyVideoBuffer(char* buffer, int ident)
{
  PROFILE_SCOPE_EX(cbOgg, ogg);

  DoAssert(ident >= 0 && ident < _videoBufferInfo.Size());

  VideoBufferInfo *vbi = _videoBufferInfo[ident];

  if (vbi->_cBuffer.CountPending(0) > 0)
  {
    VideoChunkDesc &chDesc = vbi->_cBuffer.Get(0);

    memcpy(buffer, chDesc._data->Data(), chDesc._usedBytes);
    int usedBytes = chDesc._usedBytes;
    vbi->_cBuffer.Advance(0);

    return usedBytes;
    }

  return  0;
}

bool World::ReadVideoBuffer(int ident)
{
  VideoBufferInfo* videoBufferInfo = _videoBufferInfo[ident];
  if (  videoBufferInfo != NULL )
  {
    ReadVideoBuffer( videoBufferInfo );
  };
  return  true;
}

bool World::ReadVideoBuffer(VideoBufferInfo* vbi)
{
  PROFILE_SCOPE_EX(rvBuf, *);

  DoAssert(CheckMainThread());

  while (vbi->_cBuffer.EmptySpace(0) > 0 && (!vbi->_inVideoFile->eof()))
  {
    VideoChunkDesc chDesc;
    chDesc._data = new VideoDataChunk();
    chDesc._usedBytes = vbi->_inVideoFile->read(chDesc._data->Data(), chDesc._data->Size());
    vbi->_cBuffer.Put(chDesc);
    }
    
  return  true;
}

void World::ReleaseVideoBuffers()
{
  if (!_toDeleteVideoBuffInfo.Size()) return;

  for (int i = 0; i < _toDeleteVideoBuffInfo.Size(); i++)
  {
    Ref<VideoBufferInfo> &vbi = _toDeleteVideoBuffInfo[i];
    vbi.Free();
  }

  _toDeleteVideoBuffInfo.Resize(0);
}

void World::ReadAllVideoBuffers()
{
  ReleaseVideoBuffers();

  for( int i=0; i<_videoBufferInfo.Size(); i++)
  {
    VideoBufferInfo* videoBufferInfo = _videoBufferInfo[i];
    if (  videoBufferInfo != NULL )
    {
      ReadVideoBuffer( videoBufferInfo );
    }    
  }
}

bool World::IsCopyFromDVDEnabled() const
{
  #if _PROFILE
    return false;
  #else
    return _options && _options->IsGameInitialized() && !IsSimulationEnabled();
  #endif
}

/**
Check if mission has ended for any reason
*/
void World::CheckMissionEnd()
{
  if (GInput.CheatActivated() == CheatWinMission)
  {
    SetEndMode(CAST_TO_ENUM(EEndMode, EMCheat));
    _endMissionCheated = true;
    GInput.CheatServed();
  }
  else if (GInput.GetCheatXToDo(CXTEndOfMission1))
  {
    SetEndMode(CAST_TO_ENUM(EEndMode, EMCheat));
    _endMissionCheated = true;
  }
  else
  {
#if _ENABLE_CHEATS
    if (GInput.GetCheat2ToDo(DIK_1))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMEnd1));
      _endMissionCheated = true;
    }
    else if (GInput.GetCheat2ToDo(DIK_2))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMEnd2));
      _endMissionCheated = true;
    }
    else if (GInput.GetCheat2ToDo(DIK_3))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMEnd3));
      _endMissionCheated = true;
    }
    else if (GInput.GetCheat2ToDo(DIK_4))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMEnd4));
      _endMissionCheated = true;
    }
    else if (GInput.GetCheat2ToDo(DIK_5))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMEnd5));
      _endMissionCheated = true;
    }
    else if (GInput.GetCheat2ToDo(DIK_6))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMEnd6));
      _endMissionCheated = true;
    }
    else if (GInput.GetCheat2ToDo(DIK_0))
    {
      SetEndMode(CAST_TO_ENUM(EEndMode, EMLoser));
      _endMissionCheated = true;
    }
    else
#endif
    {
      switch (_mode)
      {
      case GModeArcade:
        {
          Person *veh = GetRealPlayer();
          AIBrain *unit = veh ? veh->Brain() : NULL;
          if (!unit || unit->GetLifeState() == LifeStateDead)
          {
            SetEndMode(CAST_TO_ENUM(EEndMode, EMKilled));
            return;
          }
        }
        break;
      case GModeNetware:
        {
          if ( GetNetworkManager().GetClientState()>=NCSPlaying && !IsDedicatedServer() ) // Roleless missions are persistent
          {
            {
              // check if any player is still alive
              // if not, it has no sense to continue the mission
  #if _ENABLE_CHEATS
              // for debugging purposes we may want to continue even with dead players
              static bool neverTerminate = false;
  #else
              const bool neverTerminate = false;
  #endif
              bool player = neverTerminate;

  #if _ENABLE_INDEPENDENT_AGENTS
              // independent agents
              for (int i=0; i<_agents.Size(); i++)
              {
                AITeamMember *member = _agents[i];
                if (!member) continue;
                AIAgent *agent = member->GetAgent();
                if (!agent) continue;
                if (!agent->IsAnyPlayer()) continue;
                LifeState state = agent->GetLifeState();
                if (state != LifeStateDead)
                {
                  player = true;
                  break;
                }
              }
  #endif
            }
          }
        }
        // continue
      case GModeIntro:
        break;
      default:
        Fail("Unknown mode");
        SetEndMode(CAST_TO_ENUM(EEndMode, EMContinue));
        _endMissionCheated = false;
        break;
      }
    }
  }
}
bool IsCopyFromDVDEnabled()
{
  if (GWorld) return GWorld->IsCopyFromDVDEnabled();
  return false;
}

void World::OnMissionEvent(MissionEvent mEvent, RString par)
{
  const AutoArray<RString> &handlers = GWorld->GetMissionEventHandlers(mEvent);
  for (int i=0; i<handlers.Size(); i++)
  {
    GameVarSpace local(false);
    GameState *gstate = GWorld->GetGameState();
    int gshandle = gstate->BeginContext(&local);
    gstate->VarSetLocal("_this",par,true);
    RString handler = handlers[i];
    gstate->Execute(handler, GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
    gstate->EndContext(gshandle);
  }
}

void World::OnMissionEvent(MissionEvent mEvent, EntityAI *veh, EntityAI *entity, float par1, float par2)
{
  if (!IsMissionEventHandler(mEvent)) return;
  GameValue value = GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(veh));
  arguments.Add(GameValueExt(entity));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  OnMissionEvent(mEvent,value);
}

void World::OnMissionEvent(MissionEvent mEvent, EntityAI *entity, float par1, float par2)
{
  if (!IsMissionEventHandler(mEvent)) return;
  GameValue value = GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(entity));
  arguments.Add(GameValue(par1));
  arguments.Add(GameValue(par2));
  OnMissionEvent(mEvent,value);
}

void World::OnMissionEvent(MissionEvent mEvent, EntityAI *entity1, EntityAI *entity2)
{
  if (!IsMissionEventHandler(mEvent)) return;
  GameValue value = GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(entity1));
  arguments.Add(GameValueExt(entity2));
  OnMissionEvent(mEvent,value);
}

void World::OnMissionEvent(MissionEvent mEvent, const GameValue &pars)
{
  const AutoArray<RString> &handlers = GWorld->GetMissionEventHandlers(mEvent);
  for (int i=0; i<handlers.Size(); i++)
  {
    GameVarSpace local(false);
    GameState *gstate = GWorld->GetGameState();
    int gshandle = gstate->BeginContext(&local);
    gstate->VarSetLocal("_this",pars,true);
    RString handler = handlers[i];
    gstate->Execute(handler, GameState::EvalContext::_default, GetMissionNamespace()); // mission namespace
    gstate->EndContext(gshandle);
  }
}

// NetworkObject to keep the camera position of all clients synchronized

#define CLIENT_CAMERA_POSITION_MSG_LIST(XX) \
  XX(Create, CreateClientCameraPosition) \
  XX(UpdateGeneric, UpdateClientCameraPosition)

DEFINE_NETWORK_OBJECT(ClientCameraPositionObject, NetworkObject, CLIENT_CAMERA_POSITION_MSG_LIST)

DEFINE_NET_INDICES_EX(CreateClientCameraPosition, NetworkObject, CLIENT_CAMERA_POSITION_MSG_CREATE)

DEFINE_NET_INDICES_EX_ERR(UpdateClientCameraPosition, NetworkObject, CLIENT_CAMERA_POSITION_MSG_UPDATE, NoErrorInitialFunc)

NetworkMessageFormat &ClientCameraPositionObject::CreateFormat
(
 NetworkMessageClass cls,
 NetworkMessageFormat &format
 )
{
  switch (cls)
  {
  case NMCCreate:
    NetworkObject::CreateFormat(cls, format);
    CLIENT_CAMERA_POSITION_MSG_CREATE(CreateClientCameraPosition, MSG_FORMAT)
      break;
  case NMCUpdateGeneric:
    NetworkObject::CreateFormat(cls, format);
    CLIENT_CAMERA_POSITION_MSG_UPDATE(UpdateClientCameraPosition, MSG_FORMAT_ERR)
      break;
  default:
    NetworkObject::CreateFormat(cls, format);
    break;
  }
  return format;
}

TMError ClientCameraPositionObject::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCCreate:
    TMCHECK(NetworkObject::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(CreateClientCameraPosition)
      if (ctx.IsSending())
      {
        const Camera *camera = GScene->GetCamera();
        if ( camera==NULL || ( GWorld->CameraOn()==GWorld->PlayerOn()  && GWorld->GetCameraEffect()==NULL) )
          _position = InvalidCamPos; // no camera or not on player
        else
          _position = camera->Position();
      }
      TRANSF(dpnid)
    }
    break;
  case NMCUpdateGeneric:
    TMCHECK(NetworkObject::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateClientCameraPosition)
      CLIENT_CAMERA_POSITION_MSG_UPDATE(UpdateClientCameraPosition, MSG_TRANSFER_ERR)
      if (!ctx.IsSending())
      {
        if ( _position!=InvalidCamPos )
          GetNetworkManager().SetVoNPosition(_dpnid, _position, VZero);
      }
    }
    break;
  default:
    TMCHECK(NetworkObject::TransferMsg(ctx))
      break;
  }
  return TMOK;
}

float ClientCameraPositionObject::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = NetworkObject::CalculateError(ctx);

  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    {
      PREPARE_TRANSFER(UpdateClientCameraPosition)

      Vector3 pos;
      if (TRANSF_BASE(position, pos) == TMOK)
      {
        Vector3 camPos;
        const Camera *camera = GScene->GetCamera();
        if ( camera==NULL || ( GWorld->CameraOn()==GWorld->PlayerOn()  && GWorld->GetCameraEffect()==NULL) )
          camPos = InvalidCamPos;
        else
          camPos = camera->Position();
        float err = 1.0 * pos.Distance2(camPos);
        saturateMin(err, 1e6); // avoid infinite result
        error += err;
      }
    }
    break;
  default:
    break;
  }
  return error;
}

RString ClientCameraPositionObject::GetDebugName() const
{
  return Format("ClientCameraPositionObject: dpnid = %x", _dpnid);
}

ClientCameraPositionObject *ClientCameraPositionObject::CreateObject(NetworkMessageContext &ctx)
{
  Ref<ClientCameraPositionObject> camPosObj = new ClientCameraPositionObject();

  PREPARE_TRANSFER(CreateClientCameraPosition)

  NetworkId objectId;
  if (TRANSF_BASE(objectCreator, objectId.creator) != TMOK) return NULL;
  if (TRANSF_BASE(objectId, objectId.id) != TMOK) return NULL;
  camPosObj->SetNetworkId(objectId);
  camPosObj->SetLocal(false);

  camPosObj->TransferMsg(ctx);

  GWorld->AddClientCameraPosition(camPosObj);

  camPosObj->TransferMsg(ctx);
  return camPosObj;
}

bool ClientCameraPositionObject::IsObsolete() const 
{
  return _dpnid!=GetNetworkManager().GetPlayer(); 
}

void World::RemoveClientCameraPosition(ClientCameraPositionObject *obj)
{
  _clientCameraPositions.Delete(obj);
}

void ClientCameraPositionObject::DestroyObject()
{
  GWorld->RemoveClientCameraPosition(this);
}

void World::UpdateCameraPosition(Vector3 &pos, Vector3 &speed, int dpnid)
{
  for (int i=0; i<_clientCameraPositions.Size(); i++)
  {
    if (_clientCameraPositions[i]->_dpnid == dpnid && _clientCameraPositions[i]->_position != InvalidCamPos)
    {
      pos = _clientCameraPositions[i]->_position;
      speed = VZero;
      return;
    }
  }
}

void World::UpdateClientCameraPlayer(int dpnid, Person *person)
{
  for (int i=0; i<_clientCameraPositions.Size(); i++)
  {
    if (_clientCameraPositions[i]->_dpnid == dpnid)
    {
      _clientCameraPositions[i]->_person = person;
      return;
    }
  }
  // not found, add it to store mapping of dpnid to person
  ClientCameraPositionObject *camPosObj = new ClientCameraPositionObject;
  camPosObj->_dpnid = dpnid;
  camPosObj->_person = person;
  _clientCameraPositions.Add(camPosObj);
}

void World::AddClientCameraPosition(ClientCameraPositionObject *obj)
{
  for (int i=0; i<_clientCameraPositions.Size(); i++)
  {
    if (_clientCameraPositions[i]->_dpnid == obj->_dpnid)
    {
      obj->_person = _clientCameraPositions[i]->_person;
      _clientCameraPositions[i] = obj; //rewrite with add, but preserve person
      return;
    }
  }
  _clientCameraPositions.Add(obj);
}

#if _PROFILE_IN_STEAM_CLOUD
class SteamProfileNSLoader
{
public:
  SteamProfileNSLoader()
  {
#if _ENABLE_DEDICATED_SERVER
    bool IsDedicatedServer();
    if (IsDedicatedServer()) return;
#endif
    if (!SteamRemoteStorage()) 
    {
      bool InitSteam();
      if (!InitSteam() || !SteamRemoteStorage())
      {
        LogF("SteamProfileNamespaceLoader has failed!");
        return;
      }
    }
    RString name = Glob.header.GetPlayerName();
    RString filename = GetUserDirectory(name) + EncodeFileName(GetLoginName()) + RString(".vars.") + GetExtensionProfile();    
    RString cloudFileName = GetFilenameExt(filename);
    if (SteamRemoteStorage()->FileExists(cloudFileName))
    {
      if (QIFileFunctions::FileExists(filename))
      {
        int64 cloudTimeStamp = SteamRemoteStorage()->GetFileTimestamp(cloudFileName);
        int64 profileTimeStamp = QIFileFunctions::TimeStamp(filename);
        if (profileTimeStamp < cloudTimeStamp)
        { // profile inside cloud is more current, we should overwrite the profile
          bool LoadFileFromSteamCloud(RString steamFileName, RString targetPath);
          LoadFileFromSteamCloud(cloudFileName, filename);
        }
        // else ... maybe manualy edited profile vars file
      }
    }
  }
};
#endif

enum EUploadResult;
void World::SaveProfileNamespace()
{
  DoAssert(_profileNamespace->IsSerializationEnabled());

  // Save into *.pfv ... PRofile Variables file
  RString name = Glob.header.GetPlayerName();
  RString pfvFile = GetUserDirectory(name) + EncodeFileName(GetLoginName()) + RString(".vars.") + GetExtensionProfile();
  ParamArchiveSave pfvAr(UserInfoVersion);
  pfvAr.SetParams(&GGameState); // needed to create variables of registered types
  pfvAr.Serialize("ProfileVariables", _profileNamespace->GetVariables(), 1);
  pfvAr.SaveSigned(pfvFile);

#if _PROFILE_IN_STEAM_CLOUD
#if _ENABLE_DEDICATED_SERVER
  bool IsDedicatedServer();
  if (!IsDedicatedServer())
#endif
  {
    RString name = Glob.header.GetPlayerName();
    RString filename = GetUserDirectory(name) + EncodeFileName(GetLoginName()) + RString(".vars.") + GetExtensionProfile();    
    RString cloudFileName = GetFilenameExt(filename);
    EUploadResult UploadFileToSteamCloud(RString sourcePath, RString steamFileName);
    /*EUploadResult result = */UploadFileToSteamCloud(filename, cloudFileName);
    //LogF("UploadFileToSteamCloud: %s result=%d", cc_cast(cloudFileName), result);
  }
#endif
}

#include "El/ParamFile/classDbParamFile.hpp"
void World::LoadProfileNamespace()
{
#if _PROFILE_IN_STEAM_CLOUD
  static SteamProfileNSLoader SPL; //singleton to get profile from cloud on init
#endif
  // serialize variables stored in profile namespace
  // Load from *.pfv ... PRofile Variables file
  _profileNamespace->Reset();
  RString name = Glob.header.GetPlayerName();
  RString pfvFile = GetUserDirectory(name) + EncodeFileName(GetLoginName()) + RString(".vars.") + GetExtensionProfile();
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamArchiveLoad pfvAr;
  pfvAr.LoadBin(pfvFile, NULL, &globals);
  pfvAr.SetParams(&GGameState); // needed to create variables of registered types
  pfvAr.Serialize("ProfileVariables", _profileNamespace->GetVariables(), 1);
}


#if _HELISIM_ROTORLIB && !_DUMMY_ROTORLIB

DWORD WINAPI RTDSimulateThread::ThreadProcedureCallback(void *context)
{
  // redirect to the member function
  RTDSimulateThread *instance = reinterpret_cast<RTDSimulateThread *>(context);
  return instance->ThreadProcedure();
}

void RTDSimulateThread::SimulateOnce(bool doSim, float deltaT)
{
  PROFILE_SCOPE_EX(rtdSO, *);
  _doSim = doSim;
  _deltaT = deltaT;

   _simulate.Set();
}

void RTDSimulateThread::IsSimulationDone()
{
  PROFILE_SCOPE_EX(rtdWS, *);
  _simDone.Wait();
}

DWORD RTDSimulateThread::ThreadProcedure()
{
  _simDone.Set();

  while (true)
  {
    SignaledObject *handles[] = {&_terminate, &_simulate};
    int result = SignaledObject::WaitForMultiple(handles, lenof(handles));
    if (result == 0) break; // done

    if (_doSim && _world)
      _world->SimulateRTD(_doSim, _deltaT);

    _simDone.Set();
  }

  return 0;
}

LSError WorldRTD::Serialize(ParamArchive &ar)
{
  if(ar.IsLoading())
  {
    //Vector3 value;
    //CHECK(::Serialize(ar, "LocalCoors", value, 1))
    //base::setLocalCoordinateSystemCenter(smVec3d(value[0],value[1],value[2]));

    _timeToSimulate = _timeToSimulateRest = 0.0f;
    _accumulatedSimulationTime = 0;
  }
  else
  {
   // smVec3d coords = base::getLocalCoordinateSystemCenter();
   // Vector3 value = Vector3(coords[0],coords[1],coords[2]);
   // CHECK(::Serialize(ar, "LocalCoors", value, 1))
  }

  CHECK(ar.Serialize("externalForceGeneratorsRTD",_externalForceGeneratorsRTD,1))
  return LSOK;
}; 

float WorldRTD::FixedSimulationStep = 0.033f;

float WorldRTD::update(float timestep, bool useIterativeUpdate)
{  
  _timeToSimulate += timestep;
  _timeToSimulateRest = _timeToSimulate;

  while (_timeToSimulate > -FixedSimulationStep)
  {
    static double FixedStepAsDouble = (double) WorldRTD::FixedSimulationStep;
    float simulated = (float) base::update(FixedStepAsDouble, useIterativeUpdate);
    _timeToSimulate -= simulated;
    //LogF("RTD has simulated %f s.", simulated);
    _accumulatedSimulationTime += simulated;
    if(simulated < 0.00001) break; //happens if RTD is not unlocked
  }

  _timeToSimulateRest -= _timeToSimulate;
  return _timeToSimulateRest;
};

void WorldRTD::reset() 
{
  _windVelocity = smVec3r(0,0,0);
  base::setLocalCoordinateSystemCenter(smVec3d(0,0,0));
  _externalForceGeneratorsRTD.Clear();
  base::reset();
  _timeToSimulate = _timeToSimulateRest = _accumulatedSimulationTime = 0.0f;
  SetFixedDeltaTime(FixedSimulationStep);
};


#endif

/// functor will format the diary record to the text
class FormatCallSignFunc
{
protected:
  const CustomChannelInfo *_ccInfo;
  const Person *_person;

public:
  FormatCallSignFunc(const CustomChannelInfo *info, const Person *person) : _ccInfo(info), _person(person) {}
  RString operator ()(const char *&ptr);
};

RString FormatCallSignFunc::operator ()(const char *&ptr)
{
  bool localize = false;
  if (*ptr == '$')
  {
    // reference to stringtable
    localize = true;
    ptr++;
  }

  // read the parameter name
  const char *begin = ptr;
  while (*ptr && __iscsym(*ptr)) ptr++;
  RString name(begin, ptr - begin);

  // localize
  if (localize)
  {
    return LocalizeString(name);
  }

  if (stricmp(name, "CHANNEL_LABEL") == 0)
  {
    return _ccInfo->label;
  }
  if (stricmp(name, "UNIT_SIDE") == 0)
  {
    AIBrain *brain = _person->Brain();
    if (brain)
    {
      switch (brain->GetSide())
      {
      case TWest:
        return LocalizeString(IDS_WEST);
      case TEast:
        return LocalizeString(IDS_EAST);
      case TGuerrila:
        return LocalizeString(IDS_GUERRILA);
      case TCivilian:
        return LocalizeString(IDS_CIVILIAN);
      }
    }
    return RString("?");
  }
  if (stricmp(name, "UNIT_NAME") == 0)
  {
    return _person->GetPersonName();
  }
  if (stricmp(name, "UNIT_RANK") == 0)
  {
    return LocalizeString(IDS_SHORT_PRIVATE + _person->GetRank());
  }
  if (stricmp(name, "UNIT_REF") == 0) // useful for the log reference
  {
#if _HELISIM_LARGE_OBJECTID
    return Format("Unit%I64d", _person->GetObjectId().Encode());
#else
    return Format("Unit%d", _person->GetObjectId().Encode());
#endif
  }
  if (stricmp(name, "UNIT_VEH_NAME") == 0)
  {
    const AIBrain *brain = _person->Brain();
    Transport *veh = brain ? brain->GetVehicleIn() : NULL;
    if (!veh) return RString();
    return veh->GetDisplayName();
  }
  if (stricmp(name, "UNIT_VEH_POSITION") == 0)
  {
    const AIBrain *brain = _person->Brain();
    Transport *veh = brain ? brain->GetVehicleIn() : NULL;
    if (!veh) return RString();

    const TransportType *type = veh->Type();
    if (veh->PilotUnit() == brain)
    {
      // driver / pilot
      if (type->HasTurret() || type->GetMaxManCargo() > 0) // some other unit than driver possible
      {
        if (type->IsKindOf(GWorld->Preloaded(VTypeAir)))
          return LocalizeString(IDS_POSITION_PILOT);
        else
          return LocalizeString(IDS_POSITION_DRIVER);
      }
      else
      {
        // only driver is possible
        return RString();
      }
    }
    else
    {
      // gunner / observer
      TurretContext context;
      if (veh->FindTurret(brain->GetPerson(), context) && context._turretType)
        return context._turretType->_gunnerName;
      // cargo
      return RString();
    }
  }

  return RString("");
}

void CustomChannelInfo::GetCallSign(Person *sender, /*OUT*/char *callSignRet) const
{
  RString callSignUse = (callSign.IsEmpty() ? "%UNIT_SIDE" : callSign);
  FormatCallSignFunc func(this, sender);
  RString cs = callSignUse.ParseFormat(func);
  strcpy(callSignRet, cs);
}


void CustomChannelInfo::Clear(int ix)
{
  active = false;
  label = Format("CUSTOM%d", ix+1);
  units.Clear();
  callSign = "";
}

// true when custom channel contains the unit specified
bool CustomChannelInfo::CheckUnit(Person *unit) const
{
  for (int i=0; i<units.Size(); i++) if (unit==units[i]) return true;
  return false;
}

bool CustomChannelInfos::ChannelActive(int channel)
{
  if (channel>=CCCustom1 && channel<=CCCustomLast)
  {
    CustomChannelInfo &customChannel = operator[](channel-CCCustom1);
    if (customChannel.active)
    {
      // player must participate on channel
      Person *player = GWorld->PlayerOn();
      for (int i=0; i<customChannel.units.Size(); i++)
      {
        if (customChannel.units[i] == player) return true;
      }
    }
  }
  return false;
}

LSError CustomChannelInfos::Serialize(ParamArchive &ar)
{
  CustomChannelInfos::base *array = (CustomChannelInfos::base *)this;
  CHECK(ar.Serialize("CustomChannels", *array, 1));
  return LSOK;
}


// JNI scripting support
Entity *World::GetEntity(int jID)
{
  OLinkO(Entity) entry;
  if (!_jEntities.get(jID, entry)) return NULL;
  return entry;
}
int World::AddEntity(Entity *entity)
{
  int jID = _nextJID++;
  _jEntities.put(jID, entity);
  return jID;
}

bool ForEachNearbyTransmitterFreqv(const std::function<bool(TransmitterVoice *voice, float freq)> &func)
{
  if (!GWorld) return true; //stop
  for (int i=0; i<GWorld->_nearbyItemTransmitters.Size(); i++)
  {
    ItemTransmitter *tr = static_cast<ItemTransmitter *>(GWorld->_nearbyItemTransmitters[i].GetRef());
    if (!tr->_voice3D || !tr->isAudible) continue;
    if (func(tr->_voice3D, tr->tunedFrequency)) return true;
  }
  return false;
}

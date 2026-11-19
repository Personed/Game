#include "wpch.hpp"
#include "vehSupply.hpp"
#include "UI/uiActions.hpp"
#include "paramArchiveExt.hpp"
#include "Network/network.hpp"
#include "AI/ai.hpp"
#include "move_actions.hpp"
#include "landscape.hpp"

// config identifiers
#define TitleTxt 0 // down fades text
#define TitleTxtDown 1 // down fades text
#define TitleRsc 2 // resource text
#define TitleObj 3

/*!
\patch 1.47 Date 3/7/2002 by Jirka
- Fixed: Taking weapon from cargo space could crash dedicated server.
*/

void CutScene( const char *name )
{
  if (!GWorld->CameraOn()) return;

  static const char *lastName=NULL;
  static Link<TitleEffect> lastEffect;
  static Link<AbstractWave> lastSound;

  TitleEffect *effect=GWorld->GetTitleEffect();
  if( effect && effect==lastEffect )
  {
    if( !name )
    {
      effect->Terminate();
      return;
    }
    else if( name==lastName )
    {
      effect->Prolong(1);
      // TODO: prolong sound effect?
    }
  }
  if( effect || !name ) return;

  if( GWorld->GetCameraEffect() ) return;
  
  ParamEntryVal entry=Pars>>"CfgCutScenes">>name;
  // set-up title
  int titName=entry>>"titleType";
  RString titText=entry>>"title";

  TitleEffect *titEff=NULL;
  switch( titName )
  {
    case TitleTxt: titEff=CreateTitleEffect(TitPlain,titText);break;
    case TitleTxtDown: titEff=CreateTitleEffect(TitPlainDown,titText);break;
    //case TitleRsc: titEff=CreateTitleEffectRsc(TitPlainDown,titText);break;
  }
  if( titEff )
  {
    GWorld->SetTitleEffect(titEff);
    lastEffect=titEff;
    lastName=name;
  }

  // set-up sound effect
  SoundPars pars;
  GetValue(pars, entry>>"sound");
  if( pars.name.GetLength()>0 )
  {
    AbstractWave *wave=GSoundScene->OpenAndPlayOnce
    (
      pars.name,NULL, false, 
      GWorld->CameraOn()->FutureVisualState().Position(),GWorld->CameraOn()->ObjectSpeed(),
      pars.vol,pars.freq, pars.distance
    );
    if( wave )
    {
      GSoundScene->AddSound(wave);
      lastSound=wave;
    }
  }
}



ResourceSupply::ResourceSupply( EntityAI *vehicle, bool fullCreate)
//:_parent(vehicle)
{
  const VehicleType *type=vehicle->GetType();
  _fuelCargo = type->GetMaxFuelCargo();
  _repairCargo = type->GetMaxRepairCargo();
  _ammoCargo = type->GetMaxAmmoCargo();
  _wasRefuelingPlayer = false;

  _resourceLock.Clear(); // not locked

  for (int i=0; i<type->_weaponCargo.Size(); i++)
  {
    const WeaponCargoItem &item = type->_weaponCargo[i];
    AddWeaponCargoCount(vehicle, item.weapon, item.count);
  }
  for (int i=0; i<type->_magazineCargo.Size(); i++)
  {
    const MagazineCargoItem &item = type->_magazineCargo[i];
    AddMagazineCargoCount(vehicle, item.magazine, item.count);
  }
  
  _itemsCargo.SetWidth(type->GetItemsCargoWidth());
  _itemsCargo.SetHeight(type->GetItemsCargoHeight());
}

// used for serialization only 
ResourceSupply::ResourceSupply()
{
  _fuelCargo = 0;
  _repairCargo = 0;
  _ammoCargo = 0;
  _wasRefuelingPlayer = false;
}

ResourceSupply::~ResourceSupply()
{
  DoAssert(_itemsCargo.IsEmpty());
}

void ResourceSupply::Destroy(EntityAI *parent)
{
  // The items will be deleted by World
  // _itemsCargo.DeleteItems([](const InventoryItem *item) -> bool {return true;});
  _itemsCargo.GetItems().Clear();
}

#define ACTIONS_ENUM_NAME(name, type) EnumName(AT##name, #name),

static const EnumName UIActionNames[]=
{
  ACTIONS_DEFAULT(ACTIONS_ENUM_NAME)
#if _ENABLE_CONVERSATION
  ACTIONS_CONVERSATION(ACTIONS_ENUM_NAME)
#endif 
#if _HELISIM_ROTORLIB
  ACTIONS_HELISIM(ACTIONS_ENUM_NAME)
#endif
#if _VBS3 && _ENABLE_CARRY_BODY //BattlefieldClearance
  ACTIONS_CARRY_BODY(ACTIONS_ENUM_NAME)
#endif
#if _VBS2
  ACTIONS_VBS2(ACTIONS_ENUM_NAME)
#endif   
  EnumName()
};
template<>
const EnumName *GetEnumNames(UIActionType dummy)
{
  return UIActionNames;
}

/*!
\patch 5130 Date 2/21/2007 by Ondra
- Fixed: After save/load weapons might become invisible.
*/
LSError ResourceSupply::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("fuelCargo", _fuelCargo, 1, 0))
  CHECK(ar.Serialize("repairCargo", _repairCargo, 1, 0))
  CHECK(ar.Serialize("ammoCargo", _ammoCargo, 1, 0))
  CHECK(ar.Serialize("wasSupplyingPlayer", _wasRefuelingPlayer, 1, false))
  
  // no need to serialize parent - sets in EntityAI::Serialize
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    CHECK(ar.SerializeRef("Supplying", _supplying, 1))
    CHECK(ar.SerializeRef("Alloc", _alloc, 1))
    CHECK(ar.Serialize("action", _action, 1))
  }

  CHECK(ar.SerializeRefs("ItemsCargo", _itemsCargo.GetItems(), 1));

//  CHECK(ar.Serialize("lastSupportTime", _lastSupportTime, 1))

  return LSOK;
}

DEFINE_NETWORK_OBJECT_SIMPLE(ResourceSupply, UpdateSupply)

DEFINE_NET_INDICES_ERR(UpdateSupply, UPDATE_SUPPLY_MSG, NoErrorInitialFunc)

NetworkMessageFormat &ResourceSupply::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  UPDATE_SUPPLY_MSG(UpdateSupply, MSG_FORMAT_ERR)
  return format;
}

TMError ResourceSupply::TransferMsg(EntityAI *parent, NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(UpdateSupply)

  TRANSF(fuelCargo)
  TRANSF(repairCargo)
  TRANSF(ammoCargo)
//  ITRANSF(infantryAmmoCargo)
 // TRANSF_REF(supplying)
  TRANSF_REF(alloc)
//  ITRANSF(lastSupportTime)

  if (ctx.IsSending())
  {
    TRANSF_REFS_EX(itemsCargo, _itemsCargo.GetItems())
  }
  else
  {
    // unassign old items (if assigned)
    _itemsCargo.ForEachItem([parent](InventoryItem *item) -> bool
    {
      item->CancelHierarchyParent(parent);
      return false; // continue
    });
    // transfer items
    TRANSF_REFS_EX(itemsCargo, _itemsCargo.GetItems())
    // assign new items
    _itemsCargo.ForEachItem([parent](InventoryItem *item) -> bool
    {
      item->ItemMoveOut(parent);
      return false; // continue
    });
  }

// TODO:
/*
  TRANSF_ENUM(action)
  TRANSF(actionParam)
  TRANSF(actionParam2)
  TRANSF(actionParam3)
*/
  return TMOK;
}

float ResourceSupply::CalculateError(NetworkMessageContextWithError &ctx)
{
  PREPARE_TRANSFER(UpdateSupply)

  float error = 0;
  ICALCERR_NEQREF_SOFT(EntityAI, alloc, ERR_COEF_MODE)
 // ICALCERR_NEQREF_PERM(EntityAI, supplying, ERR_COEF_MODE)
// TODO:
/*
  ICALCERR_NEQ(int, action, ERR_COEF_MODE)
  ICALCERR_NEQ(int, actionParam, ERR_COEF_MODE)
  ICALCERR_NEQ(int, actionParam2, ERR_COEF_MODE)
  ICALCERR_NEQSTR(actionParam3, ERR_COEF_MODE)
*/
  ICALCERR_ABSDIF(float, fuelCargo, 0.01 * ERR_COEF_VALUE_MINOR)
  ICALCERR_ABSDIF(float, repairCargo, 0.01 * ERR_COEF_VALUE_MINOR)
  ICALCERR_ABSDIF(float, ammoCargo, 0.01 * ERR_COEF_VALUE_MINOR)
  //ICALCERR_ABSDIF(float, infantryAmmoCargo, 0.01 * ERR_COEF_VALUE_MINOR)

  // items
  int count = intMin(message->_itemsCargo.Size(), _itemsCargo.GetItems().Size());
  int dif = (message->_itemsCargo.Size() - count) + (_itemsCargo.GetItems().Size() - count);
  for (int i=0; i<count; i++)
  {
    if (_itemsCargo.GetItems()[i])
    {
      if (message->_itemsCargo[i] != _itemsCargo.GetItems()[i]->GetNetworkId()) dif++;
    }
    else
    {
      if (!message->_itemsCargo[i].IsNull()) dif++;
    }
  }
  error += ERR_COEF_MODE * dif;

  return error;
}

bool ResourceSupply::Supply(EntityAIFull *vehicle, const Action *action)
{
  if (vehicle)
  {
    if (!vehicle->IsLocal())
    {
      RptF("Remote entity want to supply: %s", cc_cast(vehicle->GetDebugName()));
    }
    AIBrain *brain = vehicle->CommanderUnit();
    if (_alloc && _alloc != vehicle && brain && !brain->IsPlayer()) return false;
    // do not allocate - you would unallocate immediatelly
    /*
    {
      _alloc = vehicle;
      LogF
      (
        "%s: Late alloc to %s",
        (const char *)_parent->GetDebugName(),
        (const char *)vehicle->GetDebugName()
      );
    }
    else if (_alloc != vehicle) return false;
    */
  }
  _supplying = vehicle;
  _action = unconst_cast(action);
  return true;
}

void ResourceSupply::CancelSupply()
{
  if (_alloc && _alloc == _supplying) _alloc = NULL;
  _supplying = NULL;
  _action = NULL;
}

/*!
\patch 1.82 Date 8/28/2002 by Jirka
- Fixed: Unit in cargo space of vehicle can take / drop items even when vehicle moves
\patch 5163 Date 6/4/2007 by Jirka
- Fixed: In some situations, Rearm / Repair / Refuel did not fill all
\patch 5255 Date 5/13/2008 by Bebul
- Fixed: Player could not use grenades added by Rearm
*/

void ResourceSupply::Simulate( EntityAI *parent, float deltaT, SimulationImportance prec )
{
  DoAssert(parent);

  if (_alloc)
  {
    // we are allocated to some unit
    float limit2 = _alloc->QIsManual() ? Square(30) : Square(100);
    if
    (
      _alloc->FutureVisualState().Position().Distance2(parent->FutureVisualState().Position()) > limit2
      // TODO: check if units needs us
    )
    {
      // allocated vehicle is very far - unallocate it
      if (_supplying == _alloc)
      {
        _supplying = NULL;
        _action = NULL;
      }
      _alloc = NULL;
    }
  }
  if (_supplying)
  {
    if (_supplying->FutureVisualState().Speed().Distance2(parent->FutureVisualState().Speed())>Square(2))
    {
      // FIX: Check if unit is not in cargo
      AIBrain *unit = _supplying->CommanderUnit();
      if (unit && unit->GetPerson() == _supplying && unit->GetVehicleIn() == parent)
      {
        // unit in cargo space of vehicle can take / drop items even when vehicle moves
      }
      else
      {
        _supplying = NULL;
        _action = NULL;
      }
    }
  }
  //Assert(_alloc || !_supplying);
  
  const float thold = 0;
  const float fuelThold = 0;

  // check last frame state and reset it
  bool wasRefuelingPlayer = _wasRefuelingPlayer;
  _wasRefuelingPlayer = false;
  
  // support unit - perform support if necessary
  //if (_supplying && _supplying->IsLocal())
  if (_supplying)
  {
    EntityAIFull *target = _supplying;
    if (!_action)
    {
      RptF("Supply action on %s - missing action", cc_cast(_supplying->GetDebugName()));
      _supplying = NULL;
      return;
    }
    switch (_action->GetType())
    {
    case ATHeal:
      if (parent->GetType()->IsAttendant() && target->CommanderUnit() && target->NeedsAmbulance() > 0)
      {
        // TODO: scan also driver, gunner and cargo soldiers
        EntityAI *person = target->CommanderUnit()->GetPerson();
        person->Repair(1);
        _supplying = NULL; // done
        _action = NULL;
        _alloc = NULL;
        return;
      }
      break;
    case ATHealSoldier:
      if (parent->GetType()->IsAttendant() && target->CommanderUnit() && target->NeedsAmbulance() > 0)
      {
        // TODO: scan also driver, gunner and cargo soldiers
        EntityAI *person = target->CommanderUnit()->GetPerson();
        AIBrain *aibrain = person->CommanderUnit();
        person->SetDamageNetAware(0); 
        if(aibrain)
          aibrain->SetLifeState(LifeStateAlive);
        _supplying = NULL; // done
        _action = NULL;
        _alloc = NULL;
        return;
      }
      break;
    case ATRepairVehicle:
      if (parent->GetType()->IsEngineer() && target->GetMaxHitCont() > 0.6f)
      {
        // TODO: scan also driver, gunner and cargo soldiers
        target->SetMaxHitZoneDamageNetAware(0.6f); 

        _supplying = NULL; // done
        _action = NULL;
        _alloc = NULL;

        return;
      }
      break;
    case ATRepair:
      if (GetRepairCargo() > 0 && target->NeedsRepair() > 0)
      {
        float dammage = target->GetTotalDamage();
        float total = target->GetType()->GetCost();
        float howMuch = total * dammage;
        if (howMuch <= total * thold)
        {
          _supplying = NULL; // done
          _action = NULL;
          break;
        }
        saturateMin(howMuch, _repairCargo);
        const float maxFlow = total * 0.1f;
        saturateMin(howMuch, maxFlow * deltaT);     
        target->Repair(howMuch / total);
        _repairCargo -= howMuch;
        if (target == GWorld->CameraOn()) CutScene("Repair");
        return;
      }
      break;
    case ATRefuel:
      if (GetFuelCargo() > 0 && target->NeedsRefuel() > 0)
      {
        float total = target->GetType()->GetFuelCapacity();
        float howMuch = total - target->GetFuel();
        if (howMuch <= total * fuelThold)
        {
          _supplying = NULL; // done
          _action = NULL;
          break;
        }
        saturateMin(howMuch, _fuelCargo);
        const float maxFlow = total * 0.1;
        saturateMin(howMuch, maxFlow * deltaT);
        target->Refuel(howMuch);
        _fuelCargo -= howMuch;
        if (target == GWorld->CameraOn()) CutScene("Refuel");
        return;
      }
      break;
    default:
      Fail("Unsupported action");
      break;
    }
    _supplying = NULL;
    _action = NULL;
  }
  else
  {
    // handle player vehicle automatically
    AIBrain *unit = GWorld->FocusOn();
    if (unit)
    {
      Transport *vehicle = unit->GetVehicleIn();
      if (vehicle && vehicle->FutureVisualState().Speed().SquareSize() < Square(0.1f))
      {
        // repair
        if (GetRepairCargo() > 0 && vehicle->NeedsRepair() > 0)
        {
          float dammage = vehicle->GetTotalDamage();
          float total = vehicle->GetType()->GetCost();
          float howMuch = total * dammage;
          saturateMin(howMuch, _repairCargo);
          if (howMuch > 0)
          {
            const float maxFlow = total * 0.1;
            saturateMin(howMuch, maxFlow * deltaT);
            vehicle->Repair(howMuch / total);
            _repairCargo -= howMuch;
            CutScene("Repair");
            return;
          }
        }
        // refuel
        if (
          GetFuelCargo() > 0 &&
          // when engine is running, we refuel only when there is a substantial amount missing
          // or if we have already started before
          vehicle->NeedsRefuel() > 0 && (!vehicle->EngineIsOn() || vehicle->NeedsRefuel()>0.02f || wasRefuelingPlayer)
        )
        {
          float total = vehicle->GetType()->GetFuelCapacity();
          float howMuch = total - vehicle->GetFuel();
          saturateMin(howMuch, _fuelCargo);
          if (howMuch > 0)
          {
            const float maxFlow = total * 0.1;
            saturateMin(howMuch, maxFlow * deltaT);
            vehicle->Refuel(howMuch);
            _fuelCargo -= howMuch;
            _wasRefuelingPlayer = true;
            CutScene("Refuel");
            return;
          }
        }
        // rearm
        if (GetAmmoCargo() > 0 && vehicle->NeedsRearm() > 0)
        {
          float total = vehicle->GetMaxAmmoCost();
          float howMuch = total - vehicle->GetAmmoCost();
          saturateMin(howMuch, _ammoCargo);
          if (howMuch > 0)
          {
            const float maxFlow1 = total * 0.05;
            const float maxFlow2 = 3000;
            saturateMin(howMuch, maxFlow1 * deltaT);
            saturateMin(howMuch, maxFlow2 * deltaT);
            vehicle->Rearm(howMuch);
            _ammoCargo -= howMuch;
            CutScene("Rearm");
            return;
          }
        }
      }
    }
  }
  CutScene(NULL);

  //check if any MP player have locked this ammocrate 
  //and if that player is still playing
  Vector3 supplyPosition =  parent ? parent->FutureVisualState().Position() : VZero;
  for(int i = _resourceLock.Size() -1 ; i>= 0; i--)
  {
    //remove Lock if unit was deleted or moved away from supply (MP connection interrupted ??)
    if(!_resourceLock[i] || !_resourceLock[i]->GetVehicle() 
      || _resourceLock[i]->GetVehicle()->RenderVisualState().Position().Distance2(supplyPosition) > 100) _resourceLock.DeleteAt(i);
}
  if(_resourceLock.Size() == 0) DeleteIfNotNeeded(parent);

}

#if _VBS3
float ResourceSupply::GetMagazineWeightKg()
{
  float weightKg = 0.0f;
  for( int i = 0; i < _magazineCargo.Size(); ++i )
  {
    const Magazine *m = _magazineCargo[i];
    if (!m) continue;
    const MagazineType *mType = m->_type;
    if (!mType || mType->_maxAmmo == 0) continue;      
    float ammoRatio = (float)m->GetAmmo() / (float)mType->_maxAmmo;

    if(mType->_mass == 0.5)

      weightKg += ammoRatio * mType->_mass;
  }
  return weightKg;
}

float ResourceSupply::GetWeaponWeightKg()
{
  float weightKg = 0.0f;
  for( int i = 0; i < _weaponCargo.Size(); ++i)
  {
    const WeaponType *w = _weaponCargo[i];
    if (!w) continue;
    weightKg += w->GetMass();
  }
  return weightKg;
}
#endif

/*!
\patch 1.21 Date 8/24/2001 by Jirka
- Fixed: Allow Repair, Refuel, Rearm, Heal, TakeMagazine and TakeWeapon actions in MP only to vehicle owner (driver)
\patch 1.75 Date 2/5/2002 by Jirka
- Added: Player can drop actual weapon and magazine
\patch 1.75 Date 2/13/2002 by Jirka
- Added: Player can take weapons and magazines from vehicle if sitting in cargo 
\patch 5097 Date 12/7/2006 by Jirka
- Fixed: Stealing gear from medics was possible
*/

void ResourceSupply::GetActions(EntityAI *parent, const std::function<void(const UIAction &action)> &add, AIBrain *unit, bool now)
{
  if (!unit) return;
  
  EntityAIFull *veh = unit->GetVehicle();
  if (now && !veh->IsLocal()) return;

/*
if (unit == GWorld->FocusOn() && veh->Position().Distance2(parent->Position()) < Square(3))
{
  bool check = Check(parent, veh, NULL, 0, now); // check distance
  GlobalShowMessage
  (
    500,
    "%d weapons, %d magazines, allocated to %s, check %s",
    _weaponCargo.Size(), _magazineCargo.Size(),
    _alloc ? (const char *)_alloc->GetDebugName() : "nobody",
    check ? "true" : "false"
  );
}
*/
  if (_alloc && _alloc != veh && !unit->IsPlayer()) return;  // wait


  if(!parent->GetSupply()) return;

/*
  if (_supplying)
  {
    DoAssert(_supplying == veh);
    actions.Add(ATCancelAction, parent, 20, 0, true, true);
    return;
  }
*/

#if _ENABLE_DATADISC
  bool inCargo = unit->GetVehicleIn() == parent && unit->IsInCargo();
#else
  bool inCargo = false;
#endif

  bool soldier = unit->IsFreeSoldier();
  bool iAmSoldier = dyn_cast<Person>(parent) != NULL;

  AIBrain *parentBrain =  parent->CommanderUnit();
  bool isParentSoldier = false;
  if(parentBrain) isParentSoldier = parentBrain->IsFreeSoldier();
  
  const EntityAIType *type = parent->GetType();

  if (!iAmSoldier)
  {
    if (GetRepairCargo() > 0)
    {
      if (veh->NeedsRepair() > 0)
      {
        UIAction action(new ActionBasic(ATRepair, parent));
        int index = type->GetSupplyPointIndex();
        if (index >= 0) action._memoryPoints.Add(index);
        add(action);
      }
    }
    if (GetFuelCargo() > 0)
    {
      if (veh->NeedsRefuel() > 0)
      {
        UIAction action(new ActionBasic(ATRefuel, parent));
        int index = type->GetSupplyPointIndex();
        if (index >= 0) action._memoryPoints.Add(index);
        add(action);
      }
    }
  }
  if (type->IsAttendant() && soldier && !isParentSoldier)
  {
    if (veh->NeedsAmbulance() > 0)
    {
      UIAction action(new ActionBasic(ATHeal, parent));
      int index = type->GetSupplyPointIndex();
      if (index >= 0) action._memoryPoints.Add(index);
      add(action);
    }
  }

  if (iAmSoldier) return;

  if (type->CanStoreWeapons() && (inCargo || (soldier && !unit->GetPerson()->IsActionInProgress(MFReload))))
  {
    if (!dyn_cast<InventoryItem>(parent))
    {
      UIAction action(new ActionBasic(ATGear, parent));
      int index = type->GetSupplyPointIndex();
      if (index >= 0) action._memoryPoints.Add(index);
      if (inCargo) action.showWindow = false;
      add(action);
    }
  }
}

bool ResourceSupply::ForEachItemCargo(const std::function<bool(InventoryItem *item)> &func, bool recursive)
{
  return _itemsCargo.ForEachItem(func, recursive);
}

bool ResourceSupply::ForEachItemCargo(const std::function<bool(const InventoryItem *item)> &func, bool recursive) const
{
  return _itemsCargo.ForEachItem(func, recursive);
}

bool ResourceSupply::FindItemCargo(const InventoryItem *item) const
{
  return _itemsCargo.FindItem(item);
}

bool ResourceSupply::ForEachWeaponCargo(const std::function<bool(const Weapon *weapon)> &func) const
{
  return _itemsCargo.ForEachItem([&func](const InventoryItem *item) -> bool
  {
    const Weapon *weapon = item->GetWeapon();
    return weapon && func(weapon);
  });
}

bool ResourceSupply::ForEachMagazineCargo(const std::function<bool(const Magazine *magazine)> &func) const
{
  return _itemsCargo.ForEachItem([&func](const InventoryItem *item) -> bool
  {
    const Magazine *magazine = item->GetMagazine();
    return magazine && func(magazine);
  });
}

static bool WeaponCanBeDropped(Vector3Par p)
{
  //LogF("WeaponCanBeDropped test at %.1f,%.1f",p.X(),p.Z());
  // check if we are inside some object
  int lx = (int)(p.X()*InvLandGrid), lz = (int)(p.Z()*InvLandGrid);
  for (int lxx=lx-1; lxx<=lx+1; lxx++) for (int lzz=lz-1; lzz<=lz+1; lzz++)
  {
    if (!InRange(lxx,lzz)) continue;
    const ObjectListUsed &list = GLandscape->UseObjects(lxx,lzz);
    for (int o=0; o<list.Size(); o++)
    {
      const Object *oo = list[o];
      if (oo->GetType()!=Primary && oo->GetType()!=TypeVehicle) continue;
      if (!oo->GetShape()) continue;
      // check if we are inside
      float colSize = oo->CollisionSize();
      saturateMax(colSize,1);
      float distXZ2 = p.DistanceXZ2(oo->FutureVisualState().Position());
      if (distXZ2>=Square(colSize)) continue;
      return false;
    }
  }
  //LogF("  WeaponCanBeDropped passed");
  return true;
}


const int WeaponCountInOperGrid = 3;

static bool OperGridWeaponCanBeDropped(Vector3Par pos, void *context)
{
  //LogF("OperGridWeaponCanBeDropped test at %.1f,%.1f",pos.X(),pos.Z());
  // one square is big enough to contain several weapons (3x3 m)
  const int weaponMax = (WeaponCountInOperGrid-1)/2;
  const float weaponDropGrid = OperItemGrid*(1.0f/WeaponCountInOperGrid);
  for (int x=-weaponMax; x<=weaponMax; x++)
  for (int z=-weaponMax; z<=weaponMax; z++)
  {
    Vector3 p = pos+Vector3(x*weaponDropGrid,0,z*weaponDropGrid);
    if (WeaponCanBeDropped(p))
    {
      //LogF("  OperGridWeaponCanBeDropped passed");
      return true;
    }
  }
  return false;
}

bool FindDropPos(Matrix4 &transform)
{
  Vector3 pos = transform.Position(), normal = VUp;

  //LogF("FindFreePosition drop pos %.1f,%.1f",pos.X(),pos.Z());
  if
  (
    !AIBrain::FindFreePosition
    (
      pos, normal, true, GWorld->Preloaded(VTypePaperCar), OperGridWeaponCanBeDropped
    )
  )
  {
    // no free position - we will rather destroy the container
    // that place it somewhere where it cannot be reached
    return false;
  }
  //LogF("Find drop pos %.1f,%.1f",pos.X(),pos.Z());
  // we have to find some position where is no weapon lying yet
  // search max. 50 m from this position

  if (WeaponCanBeDropped(pos))
  {
    transform.SetPosition(pos);
    Matrix3 dir(MRotationY,H_PI*2*GRandGen.RandomValue());
    transform.SetUpAndDirection(normal,dir.Direction());
    return true;
  }
  // one square is big enough to contain several weapons (3x3 m)
  const int weaponMax = (WeaponCountInOperGrid-1)/2;
  const float weaponDropGrid = OperItemGrid*(1.0f/WeaponCountInOperGrid);
  for (int x=-weaponMax; x<=weaponMax; x++)
  for (int z=-weaponMax; z<=weaponMax; z++)
  {
    Vector3 p = pos+Vector3(x*weaponDropGrid,0,z*weaponDropGrid);
    if (WeaponCanBeDropped(p))
    {
      transform.SetPosition(p);
      Matrix3 dir(MRotationY,H_PI*2*GRandGen.RandomValue());
      transform.SetUpAndDirection(normal,dir.Direction());
      return true;
    }
    
  }
  return false;
}

void ResourceSupply::DeleteIfNotNeeded(EntityAI *parent)
{
  if (parent->GetType()->_forceSupply && !parent->AreResourcesLocked() && parent->IsLocal() && _itemsCargo.IsEmpty())
    parent->SetDelete();
}

bool ResourceSupply::AddWeaponCargoCount(EntityAI *parent, const WeaponType *weaponType, int count)
{
  bool ok = false;
  for (int j=0; j<count; j++)
  {
    Ref<Weapon> weapon = dyn_cast<Weapon>(GWorld->NewVehicleWithID(weaponType));

    if (weapon)
    {
      int index = _itemsCargo.CanAddItem(weapon);

      if (index >= 0)
      {
        // add to the world
        GWorld->AddOutVehicle(weapon);
        weapon->SetMoveOutDone(parent);

        ok = _itemsCargo.AddItem(parent, weapon, index);
        if (!ok) break;

        // create network object
        if (GWorld->GetMode() == GModeNetware)
        {
          GetNetworkManager().CreateVehicle(weapon, VLTOut, "", -1);
        }
      }
    }
  }

#if _VBS3
  parent->OnEvent(EECargoChanged);
#endif

  return ok;
}

void ResourceSupply::ClearWeaponCargo(EntityAI *parent, bool localOnly)
{
  _itemsCargo.DeleteItems([](const InventoryItem *item) -> bool
  {
    const Weapon *weapon = item->GetWeapon();
    return weapon != NULL; // delete if weapon
  });

#if _VBS3
  parent->OnEvent(EECargoChanged);
#endif

  if (localOnly || parent->IsLocal()) DeleteIfNotNeeded(parent);
}

/*!
\patch 5153 Date 4/3/2007 by Jirka
- Fixed: MP - multiplication of magazines when putting them into a full crate
*/

bool ResourceSupply::AddMagazineCargoCount(EntityAI *parent, const MagazineType *type, int count)
{
  bool ok = false;
  for (int j=0; j<count; j++)
  {
    Ref<Magazine> magazine = dyn_cast<Magazine>(GWorld->NewVehicleWithID(type));
    if (magazine)
    {
      int index = _itemsCargo.CanAddItem(magazine);

      if (index >= 0)
      {
        // add to the world
        GWorld->AddOutVehicle(magazine);
        magazine->SetMoveOutDone(parent);

        ok = _itemsCargo.AddItem(parent, magazine, index);

        // create network object
        if (GWorld->GetMode() == GModeNetware)
        {
          GetNetworkManager().CreateVehicle(magazine, VLTOut, "", -1);
        }
      }
    }
  }
#if _VBS3
  parent->OnEvent(EECargoChanged);
#endif

  return ok;
}

void ResourceSupply::ClearMagazineCargo(EntityAI *parent, bool localOnly)
{
  _itemsCargo.DeleteItems([](const InventoryItem *item) -> bool
  {
    const Magazine *magazine = item->GetMagazine();
    return magazine != NULL; // delete all magazines
  });

  if (localOnly || parent->IsLocal()) DeleteIfNotNeeded(parent);
}

void EntityAI::SupplyStarted( AIBrain *unit )
{
  LogF
  (
    "%s SupplyStarted for %s",
    (const char *)GetDebugName(),(const char *)unit->GetDebugName()
  );

  if (!_supply) return;
  SupplyUnitList &list = GetSupplyUnits();

  Assert( list.Find(unit)<0 );
  list.AddUnique(unit);
}

void EntityAI::SupplyFinished( AIBrain *unit )
{
  if (!_supply) return;

  // done/canceled
  LogF
  (
    "%s SupplyFinished for %s",
    (const char *)GetDebugName(),(const char *)unit->GetDebugName()
  );
  if (GetAllocSupply()==unit->GetVehicle())
  {
    LogF("  patch unalloc");
    SetAllocSupply(NULL);
  }
  
  SupplyUnitList &list = GetSupplyUnits();
  for (int i=0; i<list.Size(); i++)
  {
    if( unit == list[i] )
    {
      list.Delete(i);
      break;
    }
  }

  if (list.Size()==0)
  {
    if (GetAllocSupply())
    {
      LogF("  %s: hard patch unalloc",(const char *)GetDebugName());
    }
  }
  UpdateStop();
}

void EntityAI::WaitForSupply(AIBrain *unit)
{
  // add to queue only when commander is alive
  AIBrain *commander = CommanderUnit();
  if (!commander || !commander->LSIsAlive()) return;
  SupplyStarted(unit);  
}

bool EntityAI::CanCancelStop() const
{
/*
  if (_supplyUnits.Count()<=0)
  {
    _supplyUnits.Clear();
    return true;
  }
*/
  return true;
}

void EntityAI::SetAllocSupply( EntityAIFull *vehicle )
{
  if( _supply )
  {
    if (_supply->GetAlloc()!=vehicle)
    {
      LogF
      (
        "%s Allocated for %s",
        (const char *)GetDebugName(),
        vehicle ? (const char *)vehicle->GetDebugName() : "NULL"
      );
      _supply->SetAlloc(vehicle);
    }
  }
}

EntityAIFull *EntityAI::GetAllocSupply() const
{
  return _supply ? _supply->GetAlloc() : NULL;
}

bool EntityAI::Supply(EntityAIFull *vehicle, const Action *action)
{
  //Operator ? shouldn't be used in .NET2003 due to compiler error
  //return _supply? _supply->Supply(vehicle, action) : false;
  if (_supply)
    return _supply->Supply(vehicle, action);
  else 
    return false;
}

bool EntityAI::SupplyInverse(EntityAIFull *vehicle, const Action *action)
{
  //Operator ? shouldn't be used in .NET2003 due to compiler error
  //return _supply? _supply->Supply(vehicle, action) : false;
  if (vehicle->GetSupply())
  {
    EntityAIFull *entity  = dyn_cast<EntityAIFull>(this);
    if (entity) // FIX: CommanderUnit returns NULL sometimes
      return vehicle->GetSupply()->Supply(entity, action);
  }
  return false;
}

void EntityAI::CancelSupply()
{
  if (_supply) _supply->CancelSupply();
}

EntityAIFull *EntityAI::GetSupplying() const
{
  return _supply ? _supply->GetSupplying() : NULL;
}

void EntityAI::UpdateStop()
{
  // if vehicle can be moving, stop waiting
  if( CanCancelStop() )
  {
    AIBrain *brain = PilotUnit();
    if( brain )
    {
      if( brain && (brain->GetState()==AIBrain::Stopping || brain->GetState()==AIBrain::Stopped) )
      {
        brain->SetState(AIBrain::Wait);  // valid pass Stopped -> Wait
      }
    }
  }
}

const SupplyUnitList &EntityAI::GetSupplyUnits() const
{
  return _supply->GetSupplyUnits();
}
SupplyUnitList &EntityAI::GetSupplyUnits()
{
  return _supply->GetSupplyUnits();
}

bool EntityAI::RemoveOwnedItem(InventoryItem *item)
{
  if (!_supply) return false;
  return _supply->GetItemsCargo().RemoveItem(this, item);
}

bool EntityAI::AddOwnedItem(InventoryItem *item)
{
  if (!_supply) return false;
  return _supply->GetItemsCargo().AddItem(this, item, -1);
}

bool EntityAI::CanAddOwnedItem(const InventoryItem *item) const
{
  if (!_supply) return false;
  return _supply->GetItemsCargo().CanAddItem(item, -1) >= 0;
}

int EntityAI::CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const
{
  if (!_supply) return -1;
  return _supply->GetItemsCargo().CanReplaceItem(oldItem, newItem);
}

bool EntityAI::ForEachOwnedItem(const std::function<bool(InventoryItem *)> &func)
{
  if (!_supply) return false;
  return _supply->GetItemsCargo().ForEachItem(func);
}

bool EntityAI::CanMoveItem(int index, const InventoryItem *item) const
{
  if (index >= 0) return false; // no slots
  return CanAddOwnedItem(item);
}

void EntityAI::MoveItem(int index, InventoryItem *item)
{
  Assert(index < 0);
  AddOwnedItem(item);
}


/*!
\file
Implemetation for Person class
*/

#include "wpch.hpp"

#include "transport.hpp"
#include "AI/ai.hpp"
#include "person.hpp"
#include "world.hpp"
#include "Network/network.hpp"
#include "UI/uiActions.hpp"
#include "global.hpp"
#include <Es/Algorithms/qsort.hpp>
#include <El/Enum/enumNames.hpp>
#include <El/Common/randomGen.hpp>
#include "WorldRes/WorldRes.h"
#include <lib/Inventory/itemoptics.hpp>

#if _PROFILE
#pragma optimize( "", off )
#endif

DEFINE_CASTING(Person)

Person::Person(const EntityAIType *name, bool fullCreate)
:base(name,fullCreate),
#pragma warning(push)
#pragma warning(disable:4355)
_weaponsState(*this),
#pragma warning(pop)
_sensorRowID(-1),_remotePlayer(1),_getOutReady(false),
_disableDamageFromObjUntil(Glob.time),
_handsTransferInProgress(false)
{
	_info._rank = RankPrivate;
	_info._experience = 0;
	_info._initExperience = 0;
  _info._pitch = 0;

#if _ENABLE_CHEATS
  _vonDpnid = 0; //means undef
#endif

#if _VBS2 // suppression
  _isSuppressed = false;
  _suppressedWince = false;
#endif

  const EntityAIType *type = GetType();

#if _ENABLE_INDEPENDENT_AGENTS
  // create agent
  AIAgent *agent = new AIAgent(this);
  _brain = agent;
  GWorld->AddAgent(agent);
#if _ENABLE_IDENTITIES  
  // register tasks
  const AutoArray<RString> &tasks = type->_agentTasks;
  for (int i=0; i<tasks.Size(); i++)
  {
    Ref<AITaskType> taskType = AITaskTypes.New(tasks[i]);
    if (taskType)
      agent->RegisterTaskType(taskType);
  }
#endif // _ENABLE_IDENTITIES
#endif // _ENABLE_INDEPENDENT_AGENTS

  // for !fullCreate, do not create magazines and weapons, but still initialize the parameters of effects
  _weaponsState.Init(type->_weapons, *type->_par, this, fullCreate);
}

void Person::SetBrain(AIBrain *brain)
{
  if (_brain == brain) return;
#if _ENABLE_INDEPENDENT_AGENTS
  if (_brain)
  {
    AITeamMember *teamMember = _brain->GetTeamMember();
    if (teamMember)
      GWorld->RemoveAgent(teamMember);
  }
  if (brain)
  {
    AITeamMember *teamMember = brain->GetTeamMember();
    if (teamMember)
      GWorld->AddAgent(teamMember);
  }
#endif
  _brain = brain;
}

void Person::CreateSubobjects()
{
  // inventory slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item) GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);
  }
}

void Person::UpdateSubobjects()
{
  // inventory slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item)
    {
      item->UpdateSubobjects();
      GetNetworkManager().UpdateObject(item);
    }
  }
}

RString Person::SelectSpeaker(RString speakerType)
{
  RString speaker = speakerType;
  ParamEntryVal cls = Pars >> "CfgVoice";
  while (!speaker.IsEmpty())
  {
    ConstParamEntryPtr entry = (Pars >> "CfgVoiceTypes").FindEntry(speaker);
    if (!entry)
    {
      RptF("Speaker %s not found in CfgVoiceTypes",cc_cast(speaker));
      return RString();
    }
    for (int i=0; i<cls.GetEntryCount(); i++)
    {
      ParamEntryVal voice = cls.GetEntry(i);
      if (! voice.IsClass()) continue;

      ConstParamEntryPtr scope = voice.FindEntry("scope");
      if (!scope || !(bool)*scope) continue;

      RString voiceType = voice >> "voiceType";
      if (stricmp(voiceType,speaker)!=0) continue; 
      ParamEntryVal identityTypes = voice >> "identityTypes";
      for (int j=0; j<identityTypes.GetSize(); j++)
      {
        if (IsIdentityEnabled(identityTypes[j]))
          // found, this speaker is good enough
          return voice.GetName();
      }
    }
    speaker = (*entry) >> "alternative";
  }
  
  return RString();
}

void Person::ApplyIdentity()
{
  int dpnid = _remotePlayer;
  if (dpnid==AI_PLAYER)
  { // use Person._info
    AIUnitInfo &info = GetInfo();

    // set no squad for AI
    info._squadPicturePath = RString("");
    info._squadPicture = NULL;

    SetFace(info._face, info._fullName);
    SetGlasses(info._glasses);
    Assert(Brain());
    if (Brain())
      Brain()->SetSpeaker(info._speaker, info._pitch);
    LogF("*** AI identity %s applied on person %d:%" OID_FORMAT" (%s)", (const char *)info._fullName, GetNetworkId().creator, GetNetworkId().id, cc_cast(GetDebugName()));
  }
  else
  { // use PlayerIdentity
    const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpnid);
    if (identity)
    {
      SetFace(identity->face, identity->name);
      SetGlasses(identity->glasses);
      Assert(Brain());
      if (Brain())
        Brain()->SetSpeaker(SelectSpeaker(identity->speakerType), identity->pitch);
      LogF("*** Player identity %s applied on person %d:%" OID_FORMAT" (%s)", (const char *)identity->name, GetNetworkId().creator, GetNetworkId().id,cc_cast(GetDebugName()));
    }
    else
      RptF("*** Player identity cannot be applied on person %d:%" OID_FORMAT" (%s) - player %d not found", GetNetworkId().creator.CreatorInt(), GetNetworkId().id, cc_cast(GetDebugName()), dpnid);
  }
}

#if _ENABLE_IDENTITIES
void Person::CopyIdentity(const Person *source)
{
  const Identity &identity = source->GetIdentity();
  _identity = identity;

  // fix the ownership of tasks
  for (int i=0; i<_identity.GetSimpleTasks().Size(); i++)
  {
    Task *task = _identity.GetSimpleTasks()[i];
    task->ChangeOwner(source, this);
  }

  // re-create the diary UI
  _identity.SetDiaryChanged();
}
#endif

RString Person::GetPersonName() const
{
  int dpid = GetRemotePlayer();
  if (dpid!=1)
  {
    const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpid);
    if (identity)
      return identity->name;
  }
  return _info._fullName;
}

LODShape *Person::GetOpticsModel(const Person *person) const
{
	Assert(person == this);
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
	if (!ptr.IsValid()) return NULL;
  
  const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
  if (!opticsInfo || !opticsInfo->_useModelOptics) return NULL;

  if (ptr._muzzle >=0 && ptr._weapon)
  {
    ItemOptics *optics = ptr._weapon->GetAttachedOptics();
    if (optics) return optics->Type()->_opticsModel;
  }

  const MuzzleType *muzzle = ptr.GetMuzzleType();
	return muzzle->_opticsModel;
}

bool Person::GetEnableOptics(ObjectVisualState const& vs, const Person *person) const
{
	Assert(person == this);
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
	if (!ptr.IsValid()) return false;
  const MuzzleType *muzzle = ptr.GetMuzzleType();
	if (!muzzle) return false;
	return muzzle->_opticsEnable;
}

bool Person::GetForceOptics(const Person *person, CameraType camType) const
{
	Assert(person == this);
  WeaponModePtr ptr = _weaponsState.ValidatedCurrentWeapon();
	if (!ptr.IsValid()) return false;
  const MuzzleType *muzzle = ptr.GetMuzzleType();
	if (!muzzle) return false;
  // with external view the optics may sometimes be not forced
  if (camType==CamExternal)
    return muzzle->_forceOptics==1;
	return muzzle->_forceOptics>0;
}

void Person::CleanUp()
{
  _brain = NULL;
  base::CleanUp();
}

void Person::Simulate(float deltaT, SimulationImportance prec)
{
#ifdef ARROWS
    Vector3 cCenterBody1(VFastTransform,ModelToWorld(),GetCenterOfMass());
    AddForce(cCenterBody1, Vector3(0,2,0), Color(1,0,0));
#endif

#if _VBS2 // suppression
  if (_isSuppressed && Glob.time >= _suppressedUntil)
  {
    _isSuppressed = false;    
    if (this == GWorld->CameraOn())
    {
      _suppressedWince = 0;
      GWorld->SetEyeAccom(-1);     
    }
  }
#endif

	base::Simulate(deltaT,prec);

  // simulate all inventory items (note: weapons and attachments simulated recursively inside PoweredItem::Simulate)
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item) item->Simulate(deltaT, prec);
  }
}

#if _VBS2 // suppression
//TODO: rework! use Arma2 suppression map
void Person::Suppress(ShotShell *shot, float closestDistance, Vector3 closestPosition)
{  
  if (Glob.config.IsEnabled(DTSuppressPlayer) && this == GWorld->CameraOn())
  {
    _suppressedWince = 1;  
    _suppressedUntil = Glob.time + 0.1;
    GWorld->SetEyeAccom(0.01);    
  }
  else
  {
    // AI suppression
    AIBrain *brain = Brain();
    if (brain)
    {
      float skill = brain->GetRawAbility(); // 0 - 1
      if (!_isSuppressed) _suppressedUntil = Glob.time;
      // [1.1-skill] so even 100% skilled AI get some suppression.
      _suppressedUntil += GRandGen.RandomValue() * MAX_AI_SUPPRESSION_TIME * (1.1f - skill);
      if (_suppressedUntil - Glob.time > MAX_AI_SUPPRESSION_TIME_CUMULATIVE) _suppressedUntil = Glob.time + MAX_AI_SUPPRESSION_TIME_CUMULATIVE;
    }
  }
  _isSuppressed = true;  
  SuppressSecondaryEffects();
}
#endif

bool Person::IsRenegade() const
{
#if _VBS3_DISABLE_RENEGADE
  return false;
#endif
  // works even when no brain is present
  return GetExperience() < ExperienceRenegadeLimit;
}

bool Person::QIsManual() const
{
	if (!GLOB_WORLD->PlayerManual())
    return false;
	if (!GLOB_WORLD->PlayerOn())
    return false;
	return GLOB_WORLD->PlayerOn() == this;
}

void Person::SetFace()
{
  int dpid = GetRemotePlayer();
  if (dpid != 1)
  {
    const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpid);
    if (identity)
    {
      SetFace(identity->face, identity->name);
      return;
    }
  }
  SetFace(_info._face, _info._fullName);
}

void Person::SetGlasses()
{
  int dpid = GetRemotePlayer();
  if (dpid!=1)
  {
    const PlayerIdentity *identity = GetNetworkManager().FindIdentity(dpid);
    if (identity)
    {
      SetGlasses(identity->glasses);
      return;
    }
  }
  SetGlasses(_info._glasses);
}

void Person::ResetStatus()
{
	_sensorRowID=SensorRowID(-1);
	base::ResetStatus();
}

UnitPosition Person::GetUnitPosition() const {return UPAuto;}

#define SERIALIZE_SLOT(type, typeprefix, name) \
  CHECK(ar.SerializeRef("slot" #name, _inventorySlots[typeprefix##name], 1))

LSError Person::Serialize(ParamArchive &ar)
{
	CHECK(base::Serialize(ar))
	if (IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
	{
		CHECK(SerializeIdentity(ar))
	}
	else
	{
    // now, unit is stored here
    CHECK(ar.Serialize("Brain", _brain, 1))
		CHECK(ar.Serialize("sensorRowID", _sensorRowID, 1))
	}
#if _ENABLE_IDENTITIES
  LSError result = ar.Serialize("Identity", _identity, 1);
  // class Identity can be missing in older saves
  if (result != LSOK && result != LSNoEntry) return result;
#endif

  INVENTORY_SLOT_ENUM(InventorySlotId, IS, SERIALIZE_SLOT)
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
  {
    for (int i=0; i<NInventorySlotId; i++)
    {
      // ensure the correct clothing model will be selected
      if (_inventorySlots[i]) _inventorySlots[i]->UpdateShape(this);
    }
  }

  if (ar.IsSaving())
  {
    for (int i=0; i<NItemShortcuts; i++)
    {
      const InventoryItemType *type = GetItemShortcut(i);
      RString typeName = type ? type->GetName() : RString();
      CHECK(ar.Serialize(Format("shortcut%d", i), typeName, 1));
    }
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    for (int i=0; i<NItemShortcuts; i++)
    {
      RString typeName;
      CHECK(ar.Serialize(Format("shortcut%d", i), typeName, 1));
      Ref<const InventoryItemType> type = typeName.IsEmpty() ? NULL : dynamic_cast<const InventoryItemType *>(VehicleTypes.New(typeName));
      SetItemShortcut(i, type);
    }
  }

  CHECK(ar.SerializeRef("ItemInHands", _itemInHands, 1));

  CHECK(_weaponsState.Serialize(this, ar))
	return LSOK;
}

LSError Person::SerializeIdentity(ParamArchive &ar)
{
	CHECK(ar.Serialize("Info", _info, 1))
	if (ar.IsSaving())
	{
		float skill = _brain ? _brain->GetRawAbility() : 1.0;
		CHECK(ar.Serialize("skill", skill, 1, 1.0))
	}
	else if (ar.GetPass() == ParamArchive::PassSecond)
	{
		ar.FirstPass();
		if (_brain)
		{
			float skill;
			CHECK(ar.Serialize("skill", skill, 1, 1.0))
			_brain->SetRawAbility(skill);
		}
		ar.SecondPass();

		if (_info._face.GetLength() > 0)
			SetFace(_info._face);
		
		if (_info._glasses.GetLength() > 0)
			SetGlasses(_info._glasses);
		
		if (_info._speaker.GetLength() > 0 && _brain)
			_brain->SetSpeaker(_info._speaker, _info._pitch);
	}
	
	return LSOK;
}

#define PERSON_MSG_LIST(XX) \
  XX(Create, CreateVehicleBrain) \
  XX(UpdateGeneric, UpdateVehicleBrain)

DEFINE_NETWORK_OBJECT(Person, base, PERSON_MSG_LIST)

DEFINE_NET_INDICES_EX(CreateVehicleBrain, CreateVehicleAI, CREATE_VEHICLE_BRAIN_MSG)

DEFINE_NET_INDICES_EX_ERR(UpdateVehicleBrain, UpdateVehicleAIFull, UPDATE_VEHICLE_BRAIN_MSG, NoErrorInitialFunc)

NetworkMessageFormat &Person::CreateFormat
(
	NetworkMessageClass cls,
	NetworkMessageFormat &format
)
{
	switch (cls)
	{
  case NMCCreate:
    base::CreateFormat(cls, format);
    CREATE_VEHICLE_BRAIN_MSG(CreateVehicleBrain, MSG_FORMAT)
    break;
	case NMCUpdateGeneric:
		base::CreateFormat(cls, format);
    UPDATE_VEHICLE_BRAIN_MSG(UpdateVehicleBrain, MSG_FORMAT_ERR)
		break;
	default:
		base::CreateFormat(cls, format);
		break;
	}
	return format;
}

#define TRANSFER_SLOT(type, typeprefix, name) \
  TRANSF_REF_EX(slot##name, _inventorySlots[typeprefix##name])

TMError Person::TransferMsg(NetworkMessageContext &ctx)
{
	switch (ctx.GetClass())
	{
	case NMCUpdateGeneric:
		TMCHECK(base::TransferMsg(ctx))
		{
			PREPARE_TRANSFER(UpdateVehicleBrain)
			if (ctx.IsSending())
			{
        TRANSF(remotePlayer)
			}
      else
      {
        int remotePlayer;
        TRANSF_EX(remotePlayer, remotePlayer)
        SetRemotePlayer(remotePlayer);
      }

      // TODO: ?? _sensorRowID

      if (ctx.IsSending())
      {
        INVENTORY_SLOT_ENUM(InventorySlotId, IS, TRANSFER_SLOT)
      }
      else
      {
        // same for inventory slots
        for (int i=0; i<NInventorySlotId; i++)
        {
          InventoryItem *item = _inventorySlots[i];
          if (item)
          {
            item->UpdateShape(NULL);
            item->CancelHierarchyParent(this);
          }
        }
        INVENTORY_SLOT_ENUM(InventorySlotId, IS, TRANSFER_SLOT)
        #define LOG_SLOT(type, typeprefix, name) \
            do { \
            NetworkId id = message->_slot##name; \
            if (!id.IsNull() && _inventorySlots[typeprefix##name].IsNull()) \
            {\
              LogF("%s: inventory not found: %d:%" OID_FORMAT"",cc_cast(GetDebugName()),id.creator.CreatorInt(),id.id); \
              /*DoAssert (!_inventorySlots[typeprefix##name].IsNull());*/ \
            }} while (false);
        INVENTORY_SLOT_ENUM(InventorySlotId, IS, LOG_SLOT)
        for (int i=0; i<NInventorySlotId; i++)
        {
          InventoryItem *item = _inventorySlots[i];
          if (item)
          {
            item->UpdateShape(this);
            item->ItemMoveOut(this);
          }
        }
      }

      TRANSF_REF(itemInHandsWanted)

      if (_brain)
      {
        #define MSG_TRANSFER_BRAIN(MessageName,type,name,datatype,rawType, compres, defval, description, xfer, errType, errCoef ) \
          TRANSF_EX(name,_brain->Transf_##name())
        UPDATE_AI_BRAIN_MSG(UpdateVehicleBrain,MSG_TRANSFER_BRAIN)
        #undef MSG_TRANSFER_BRAIN
      }
      else if (ctx.IsSending())
      {
        #define MSG_TRANSFER_BRAIN_NULL(MessageName,type,name,datatype,rawType, compres, defval, description, xfer, errType, errCoef ) \
          do {defval;TRANSF_EX(name,defValue);} while(false);
        UPDATE_AI_BRAIN_MSG(UpdateVehicleBrain,MSG_TRANSFER_BRAIN_NULL)
        #undef MSG_TRANSFER_BRAIN_NULL
      }
		}
		break;
  case NMCCreate:
    {
      TMCHECK(base::TransferMsg(ctx))
      {
        PREPARE_TRANSFER(CreateVehicleBrain)

        #define MSG_TRANSFER_CREATE_INFO(MessageName,type,name,datatype,rawType, compres, defval, description, xfer ) \
          TRANSF_EX(name,_info._##name)
        CREATE_AI_BRAIN_MSG(CreateVehicleBrain,MSG_TRANSFER_CREATE_INFO)
      }
    }
    break;

	default:
		return base::TransferMsg(ctx);
	}
	return TMOK;
}

#define CALCERR_SLOT(type, typeprefix, name) \
  ICALCERRE_NEQREF_PERM(InventoryItem, slot##name, _inventorySlots[typeprefix##name], ERR_COEF_MODE)

float Person::CalculateError(NetworkMessageContextWithError &ctx)
{
	float error = 0;
	switch (ctx.GetClass())
	{
	case NMCUpdateGeneric:
		error += base::CalculateError(ctx);
		{
			PREPARE_TRANSFER(UpdateVehicleBrain)
			ICALCERR_NEQ(int, remotePlayer, ERR_COEF_IMMEDIATE)

      // inventory
      INVENTORY_SLOT_ENUM(InventorySlotId, IS, CALCERR_SLOT)
      ICALCERR_NEQREF_SOFT(InventoryItem, itemInHandsWanted, ERR_COEF_MODE)

      #define MSG_CALC_ERR_INFO(MessageName,type,name,datatype,rawType, compres, defval, description, xfer, errType, errCoef ) \
        if (errType > ET_NONE && _brain) error += errCoef * ::CalculateError< errType, rawType >::ErrorStatic(message->_##name, _brain->Transf_##name());
      UPDATE_AI_BRAIN_MSG(UpdateVehicleBrain,MSG_CALC_ERR_INFO)

    }
		break;
	default:
		error += base::CalculateError(ctx);
		break;
	}

  // The player's own position drives remote AI (including zombie attacks).
  // The generic time-only position error can otherwise leave the server with
  // a stale player position for several seconds while the player is moving.
  if (ctx.GetClass() == NMCUpdatePosition && GWorld && this == GWorld->GetRealPlayer())
    error *= 20.0f;

	return error;
}

void Person::JSONSave(JSONValue &container, JSONAllocator &allocator) const
{
  const Matrix4 &trans = FutureVisualState().Transform();
  JSONAddVector3(container, "pos", trans.Position(), allocator);
  JSONAddVector3(container, "dir", trans.Direction(), allocator);
  JSONAddVector3(container, "up", trans.DirectionUp(), allocator);

  JSONValue items;
  items.SetArray();
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item)
    {
      JSONValue value;
      value.SetObject();
      value.AddMember("slot", InventorySlotTypes.GetName(i), allocator);
      value.AddMember("type", item->GetName(), allocator);
      item->JSONSave(value, allocator);
      items.PushBack(value, allocator);
    }
  }
  container.AddMember("items", items, allocator);

  JSONSaveVariables(container, allocator);

  // Save Item Shortcuts
  bool hasShortcuts = false;
  for (int i=0; i<NItemShortcuts; i++)
  {
    if (GetItemShortcut(i))
    {
      hasShortcuts = true;
      break;
    }
  }
  if (hasShortcuts)
  {
    JSONValue newState;
    if (!container.HasMember("state"))
    {
      newState.SetObject();
      container.AddMember("state", newState, allocator);
    }

    JSONValue &state = container["state"];
    if (state.IsObject())
    {
      JSONValue items;
      items.SetArray();
      for (int i=0; i<NItemShortcuts; i++)
      {
        const InventoryItemType *type = GetItemShortcut(i);
        RString typeName = type ? type->GetName() : RString();
        items.PushBack(cc_cast(typeName), allocator);
      }
      state.AddMember("shortcuts", items, allocator);
    }
  }
}

bool Person::JSONLoad(JSONValue &container, bool loadPos)
{
  if (loadPos)
  {
    Vector3 pos, dir, up;
    if (!JSONReadVector3(container, "pos", pos)) return false;
    if (!JSONReadVector3(container, "dir", dir)) return false;
    if (!JSONReadVector3(container, "up", up)) return false;

    Matrix4 transform;
    transform.SetUpAndDirection(up, dir);
    transform.SetPosition(pos);

    PlaceOnSurface(transform);
    SetTransform(transform);
  }

  JSONValue &items = container["items"];
  if (items.IsArray())
  {
    for (int i=0; i<items.Size(); i++)
    {
      JSONValue &value = items[i];
      if (value.IsObject())
      {
        RString slot = value["slot"].GetString();
        int index = InventorySlotTypes.GetValue(slot);
        if (index < 0 || index >= NInventorySlotId) continue; // wrong slot

        RString type = value["type"].GetString();
        Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewNonAIVehicleWithID(type, RString()));
        if (item && item->JSONLoad(value, this))
        {
          _inventorySlots[index] = item;
          item->UpdateShape(this);

          // add to the world
          GWorld->AddOutVehicle(item);
          item->SetMoveOutDone(this);

          // DO NOT GetNetworkManager().CreateVehicle(...) as it will be created later in CreateSubobjects during CreateVehicle of its hierParent
        }
      }
    }
  }

  JSONLoadVariables(container);

  JSONValue &state = container["state"];
  if (state.IsObject())
  {
    JSONValue &items = state["shortcuts"];
    if (items.IsArray())
    {
      for (int i=0; i<NItemShortcuts; i++)
      {
        RString typeName = items[i].GetString();
        Ref<const InventoryItemType> type = typeName.IsEmpty() ? NULL : dynamic_cast<const InventoryItemType *>(VehicleTypes.New(typeName));
        SetItemShortcut(i, type);
      }
    }
  }

  return true;
}

void Person::SetRemotePlayer(int player)
{
  int old = _remotePlayer;
  _remotePlayer = player;
  if ( player!=old ) ApplyIdentity();
  if ( player!=1 ) GWorld->UpdateClientCameraPlayer(player, this);
}

bool Person::IsNetworkPlayer() const
{
	return _remotePlayer != 1 || this == GWorld->GetRealPlayer();
}

bool Person::IsRemotePlayer() const
{
  return _remotePlayer != 1;
}

bool Person::IsControlled() const
{
  if (GetNetworkManager().IsClient())
  {
    if (GetNetworkManager().IsServer())
    {
      if (QIsManual()) return true;
      if (_remotePlayer!=1) return true;
    }
    else if (_remotePlayer==GetNetworkManager().GetPlayer())
    {
      return true;
    }
  }
  else
  {
    if( QIsManual()) return true;
  }
  return false;
}


bool Person::KeyboardUsedInFrame() const
{
  if (_inputFrameID == GInput.frameID) return true; // avoid processing a keyboard twice in a frame
  _inputFrameID = GInput.frameID;
  return false;
}

void Person::SimulateImpulseDamage(DoDamageResult &result, EntityAI *owner, float impulse, Vector3Par modelPos)
{
  float contact=impulse*GetInvMass();
  if (contact < 5.0f)
    contact = contact * (0.01f/5);
  else
    contact = contact * (1.0f/5)-0.99f;

  if( contact>0 )
  {
    float armorCoef = GetInvArmor()*3;
    contact *= armorCoef;

    //LogF("Contact %.2f",contact);
    saturateMin(contact,5);

    if (contact > 0.001f)
      LocalDamage(result, ShotInfo(NULL, owner,this, VZero),contact,1.0f); 
  }   
}

void Person::DisableDamageFromObj(float seconds)
{ 
    _disableDamageFromObjUntil = Glob.time + seconds;
}

bool CheckAccessCreate(ParamEntryPar entry);

bool Person::ForEachTurret(ITurretFunc &func) const
{
  TurretContext context;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  return func(unconst_cast(this), context);
}

bool Person::ForEachTurret(EntityVisualState& vs, ITurretFuncV &func) const
{
  TurretContextV context;
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
//  context._parentTrans = vs.Orientation();
  return func(unconst_cast(this), context);
}

  /// enumerate all turrets
bool Person::ForEachTurretEx(EntityVisualState& vs, ITurretFuncEx &func) const
{
  TurretContextEx context;
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  context._parentTrans = vs.Orientation();
  return func(unconst_cast(this), context);
}


bool Person::ForEachCrew(ICrewFunc &func) const
{
  return func(unconst_cast(this));
}

bool Person::FindTurret(const Person *gunner, TurretContext &context) const
{
  if (gunner != this)
    return false;
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

bool Person::FindTurret(EntityVisualState& vs, const Person *gunner, TurretContextV &context) const
{
  if (gunner != this)
    return false;
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
//  context._parentTrans = vs.Orientation();
  return true;
}

bool Person::GetPrimaryGunnerTurret(TurretContext &context) const
{
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

bool Person::GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextV &context) const
{
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

bool Person::GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextEx &context) const
{
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  context._parentTrans = vs.Orientation();
  return true;
}

bool Person::GetPrimaryObserverTurret(TurretContext &context) const
{
  context._turret = NULL;
  context._turretType = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

bool Person::GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextV &context) const
{
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  return true;
}

bool Person::GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextEx &context) const
{
  context._turret = NULL;
  context._turretType = NULL;
  context._turretVisualState = NULL;
  context._gunner = unconst_cast(this);
  context._weapons = unconst_cast(&_weaponsState);
  context._parentTrans = vs.Orientation();
  return true;
}

bool Person::FireWeaponEffects(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo *remoteInfo)
{
  if (!base::FireWeaponEffects(context, ptr, magazine, target, remoteInfo)) return false; // not fired

  // Shot occurred, add some heat    
  if (ptr._weapon) ptr._weapon->AddHeat();
  return true;
}

bool Person::RemoveOwnedItem(InventoryItem *item)
{
  bool removedFromHands = false;

  // remove from hands
  if (item == _itemInHands)
  {
    // Keep the normal disarm animation, but do not let HandleItemInHands
    // put the same item straight back into the inventory before the
    // drop/transfer completes.
    _handsTransferInProgress = true;
    RemoveItemInHands();
    _handsTransferInProgress = false;
    removedFromHands = true;
  }

  // check slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    if (_inventorySlots[i] == item)
    {
      _inventorySlots[i] = NULL;
      item->UpdateShape(NULL);
      // unassign the item, but leave it in the _outVehicles
      // do not handle it if already handled (for example new owner set)
      item->CancelHierarchyParent(this);
      return true;
    }
  }

  return removedFromHands;
}

bool Person::AddOwnedItem(InventoryItem *item)
{
  // check the correct slot
  const InventoryItemType *type = item->Type();
  if (type)
  {
    int slot = type->GetInventorySlot();
    if (slot == ISShoulder && _inventorySlots[ISShoulder] && !_inventorySlots[ISShoulder2])
      slot = ISShoulder2;
    if (slot >= 0 && slot < NInventorySlotId && _inventorySlots[slot] == NULL)
    {
      // move in world
      item->ItemMoveOut(this);
      _inventorySlots[slot] = item;
      item->UpdateShape(this);
      return true;
    }
  }

  return false;
}

bool Person::CanAddOwnedItem(const InventoryItem *item) const
{
  // check the correct slot
  const InventoryItemType *type = item->Type();
  if (type)
  {
    int slot = type->GetInventorySlot();
    if (slot == ISShoulder && _inventorySlots[ISShoulder] && !_inventorySlots[ISShoulder2])
      slot = ISShoulder2;
    return slot >= 0 && slot < NInventorySlotId && _inventorySlots[slot] == NULL;
  }

  return false;
}

int Person::CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const
{
  if (!newItem->CheckInventoryCondition(this)) return -1;

  const InventoryItemType *type = newItem->Type();
  if (type)
  {
    int slot = type->GetInventorySlot();
    if (slot == ISShoulder && _inventorySlots[ISShoulder2] == oldItem)
      return ISShoulder2;
    if (slot >= 0 && slot < NInventorySlotId && _inventorySlots[slot] == oldItem) return slot;
  }

  return -1;
}

bool Person::ForEachOwnedItem(const std::function<bool(InventoryItem *)> &func)
{
  // first traverse inventory slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item && func(item)) return true;
  }

  return false;
}

bool Person::CanMoveItem(int index, const InventoryItem *item) const
{
  if (index >= 0)
  {
    // slot empty
    if (index < 0 || index >= NInventorySlotId) return false;
    if (GetInSlot((InventorySlotId)index)) return false;

    // item fits into this slot
    const InventoryItemType *type = item->Type();
    return type && (type->GetInventorySlot() == index ||
      (index == ISShoulder2 && type->GetInventorySlot() == ISShoulder));
  }
  else
  {
    return CanAddItem(item);  
  }
}

void Person::MoveItem(int index, InventoryItem *item)
{
  if (index >= 0)
    PutInSlot((InventorySlotId)index, item); // slot and content check inside
  else
    AddItem(item);
}

void Person::ForEachChildren(const std::function<bool(const NetworkObject *)> &func) const
{
  ForEachItem([&func](const InventoryItem *item)->bool {func(item);return false;});
}

bool Person::ForEachItem(const std::function<bool(InventoryItem *)> &func)
{
  // weapons in slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item && (func(item) || item->ForEachItem(func))) return true;
  }

  return false;
}

bool Person::ForEachItem(const std::function<bool(const InventoryItem *)> &func) const
{
  // weapons in slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    const InventoryItem *item = _inventorySlots[i];
    if (item && (func(item) || item->ForEachItem(func))) return true;
  }

  return false;
}

/** caution: ForEachItemReverse not recursive, used only for weapon dropping */
bool Person::ForEachItemReverse(const std::function<bool(InventoryItem *)> &func)
{
  // weapons in containers
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    InventoryItem *item = _inventorySlots[i];
    if (!item) continue;
    ResourceSupply *supply = item->GetSupply();
    if (!supply) continue;
    // caution: recursion not handled in ForEachItemReverse
    if (supply->GetItemsCargo().ForEachItemReverse(func)) return true;
  }

  // weapons in slots
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item && func(item)) return true;
  }

  return false;
}

/** caution: ForEachItemReverse not recursive, used only for weapon dropping */
bool Person::ForEachItemReverse(const std::function<bool(const InventoryItem *)> &func) const
{
  // weapons in containers
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    const InventoryItem *item = _inventorySlots[i];
    if (!item) continue;
    const ResourceSupply *supply = item->GetSupply();
    if (!supply) continue;
    if (supply->GetItemsCargo().ForEachItemReverse(func)) return true;
  }

  // weapons in slots
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    const InventoryItem *item = _inventorySlots[i];
    if (item && func(item)) return true;
  }

  return false;
}

int Person::CountItems(const InventoryItemType *type)
{
  int count = 0;
  ForEachItem([&count, type](const InventoryItem *item) -> bool
  {
    if( item->Type() == type ) count++;
    return false; // continue
  });

  return count;
}

void Person::DeleteItems(const std::function<bool(const InventoryItem *item)> &func)
{
  // weapons in containers
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    InventoryItem *item = _inventorySlots[i];
    if (!item) continue;
    ResourceSupply *supply = item->GetSupply();
    if (!supply) continue;
    supply->GetItemsCargo().DeleteItems(func);
  }

  // weapons in slots
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item && func(item))
    {
      // remove from the world
      item->ItemDestroy();
      // and the slot
      _inventorySlots[i] = NULL;
    }
  }
}

bool Person::DeleteFirstItem(const std::function<bool(const InventoryItem *item)> &func)
{
  // weapons in containers
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    InventoryItem *item = _inventorySlots[i];
    if (!item) continue;
    ResourceSupply *supply = item->GetSupply();
    if (!supply) continue;
    if (supply->GetItemsCargo().DeleteFirstItem(func)) return true;
  }

  // weapons in slots
  for (int i=NInventorySlotId-1; i>=0; i--)
  {
    InventoryItem *item = _inventorySlots[i];
    if (item && func(item))
    {
      // remove from the world
      item->ItemDestroy();
      // and the slot
      _inventorySlots[i] = NULL;
      return true; // deleted
    }
  }

  return false;
}

bool Person::CanAddItem(const InventoryItem *item) const
{
  if (!item->CheckInventoryCondition(this)) return false;
  // try to use slot first
  const InventoryItemType *type = item->Type();
  if (type)
  {
    int slot = type->GetInventorySlot();
    if (slot >= 0 && slot < NInventorySlotId && _inventorySlots[slot] == NULL) return true;
    if (slot == ISShoulder && _inventorySlots[ISShoulder2] == NULL) return true;
  }

  // then check owned containers
  for (int i=0; i<NInventorySlotId; i++)
  {
    const InventoryItem *owner = _inventorySlots[i];
    if (!owner) continue;
    const ResourceSupply *supply = owner->GetSupply();
    if (!supply) continue;
    if (supply->GetItemsCargo().CanAddItem(item) >= 0) return true;
  }

  return false;
}

bool Person::AddItem(InventoryItem *item)
{
  if (!item->CheckInventoryCondition(this)) return false;
  // try to use slot first
  const InventoryItemType *type = item->Type();
  if (type)
  {
    int slot = type->GetInventorySlot();
    if (slot == ISShoulder && _inventorySlots[ISShoulder] && !_inventorySlots[ISShoulder2])
      slot = ISShoulder2;
    if (slot >= 0 && slot < NInventorySlotId && _inventorySlots[slot] == NULL)
    {
      // move in world
      item->ItemMoveOut(this);
      _inventorySlots[slot] = item;
      item->UpdateShape(this);
      OnItemAdded(item);
      return true;
    }
  }

  // otherwise put in some container
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *owner = _inventorySlots[i];
    if (!owner) continue;
    ResourceSupply *supply = owner->GetSupply();
    if (!supply) continue;
    if (supply->GetItemsCargo().AddItem(owner, item, -1))
    {
      OnItemAdded(item);
      return true;
    }
  }

  return false;
}

bool Person::RemoveItem(InventoryItem *item)
{
  if (_itemInHands == item) RemoveItemInHands();

  // weapons in slots
  for (int i=0; i<NInventorySlotId; i++)
  {
    if (item == _inventorySlots[i])
    {
      _inventorySlots[i] = NULL;
      item->UpdateShape(NULL);
      item->CancelHierarchyParent(this);
      return true;
    }
  }

  // weapons in containers
  for (int i=0; i<NInventorySlotId; i++)
  {
    InventoryItem *owner = _inventorySlots[i];
    if (!owner) 
      continue;
    ResourceSupply *supply = owner->GetSupply();
    if (!supply) 
      continue;
    if( supply->GetItemsCargo().RemoveItem(owner, item) )
      return true;
  }

  return false;
}

InventoryItem *Person::CreateItem(RString typeName)
{
  Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewVehicleWithID(typeName));
  if (item)
  {
    item->SetMoveOutDone(NULL); // do not try to move out
    item->Init(FutureVisualState().Transform(), true); // invoke Init prior AddAItem (_reflectors initialization)
    if (AddItem(item))
    {
      // invoke "init" event handler
      item->OnEvent(EEInit);

      // add to the world
      GWorld->AddOutVehicle(item);

      // create network object
      if (GWorld->GetMode() == GModeNetware)
        GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);

      // for magazine, try to reload
      Magazine *magazine = item->GetMagazine();
      if (magazine)
      {
        _weaponsState.ForEachMuzzle([this, magazine](const WeaponModePtr &ptr) -> bool
        {
          const MuzzleType *muzzleType = ptr.GetMuzzleType();
          if (!muzzleType) return false; // continue

          Muzzle &muzzle = ptr.GetMuzzle();
          // if not empty, there is no need to reload
          if (muzzle._magazine && muzzle._magazine->GetAmmo() > 0) return false; // continue
          // check if other reload already in progress
          if (muzzle._magazineWanted && muzzle._magazineWanted != muzzle._magazine) return false; // continue

          for (int k=0; k<muzzleType->_magazines.Size(); k++)
            if (muzzleType->_magazines[k] == magazine->Type())
            {
              ReloadMagazine(_weaponsState, ptr, magazine);
              return true; // done
            }
          return false; // continue
        });
      }

      return item;
    }
  }

  return NULL;
}

bool Person::MoveItem(InventoryItem *item)
{
  // check if fits
  if (!CanAddItem(item)) return false;

  // remove from the old owner
  Entity *parent = item->GetHierarchyParent();
  if (parent) DoVerify(parent->RemoveOwnedItem(item));
  
  // now should be safe to add it
  AddItem(item);
  return true;
}

bool Person::FindItem(const InventoryItem *item) const
{
  return ForEachItem([item](const InventoryItem *check) -> bool
  {
    return check == item; // done or continue
  });
}

// IWeaponsHolder implementation
void Person::Compact()
{
}

bool Person::ForEachWeapon(const std::function<bool(Weapon *weapon)> &func)
{
  return ForEachItem([&func](InventoryItem *item) -> bool
  {
    Weapon *weapon = item->GetWeapon();
    return weapon && func(weapon); // done or continue
  });
}

bool Person::ForEachWeapon(const std::function<bool(const Weapon *weapon)> &func) const
{
  return ForEachItem([&func](const InventoryItem *item) -> bool
  {
    const Weapon *weapon = item->GetWeapon();
    return weapon && func(weapon); // done or continue
  });
}

bool Person::AddWeapon(EntityAIFull *entity, Weapon *weapon)
{
  DoAssert(entity == this);
  return AddItem(weapon);
}

bool Person::RemoveWeapon(Weapon *weapon)
{
  return RemoveItem(weapon);
}

bool Person::DeleteWeapon(RStringB name)
{
  return DeleteFirstItem([name](const InventoryItem *item) -> bool
  {
    const Weapon *weapon = item->GetWeapon();
    if (!weapon) return false; // continue
    const WeaponType *type = weapon->Type();
    return type && stricmp(type->GetName(), name) == 0; // delete or continue
  });
}

void Person::DeleteWeapons(const std::function<bool(const Weapon *weapon)> &func)
{
  return DeleteItems([&func](const InventoryItem *item) -> bool
  {
    const Weapon *weapon = item->GetWeapon();
    return weapon && func(weapon);
  });
}

void Person::DeleteAllWeapons()
{
  return DeleteItems([](const InventoryItem *item) -> bool
  {
    const Weapon *weapon = item->GetWeapon();
    return weapon != NULL;
  });
}

bool Person::ForEachMagazine(const std::function<bool(Magazine *)> &func)
{
  return ForEachItem([&func](InventoryItem *item) -> bool
  {
    Magazine *magazine = item->GetMagazine();
    return magazine && func(magazine); // done or continue
  });
}

bool Person::DropItem(InventoryItem *item)
{
  if (!RemoveItem(item)) return false;

  Vector3Val pos = FutureVisualState().Position();
  Vector3Val dir = FutureVisualState().Direction();
  item->ItemDrop(this, pos, dir);
  return true;
}

bool Person::PutInSlot(InventorySlotId slot, InventoryItem *item)
{
  // check if possible
  if (slot < 0 || slot >= NInventorySlotId) return false;
  if (_inventorySlots[slot]) return false;

  // put it in
  item->ItemMoveOut(this);
  _inventorySlots[slot] = item;
  item->UpdateShape(this);
  return true;
}

InventoryItem *Person::RemoveFromSlot(InventorySlotId slot)
{
  if (slot < 0 || slot >= NInventorySlotId) return NULL;

  InventoryItem *item = _inventorySlots[slot];
  if (item)
  {
    _inventorySlots[slot] = NULL;
    item->UpdateShape(NULL);
    if (_itemInHands == item) RemoveItemInHands();
    // unassign the item, but leave it in the _outVehicles
    // do not handle it if already handled (for example new owner set)
    item->CancelHierarchyParent(this);
  }

  return item;
}

InventoryItem *Person::CreateInSlot(InventorySlotId slot, RString name)
{
  Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewVehicleWithID(name));
  if (!item) return NULL;

  item->Init(FutureVisualState().Transform(), true);

  // invoke "init" event handler
  item->OnEvent(EEInit);

  // add to the world
  GWorld->AddOutVehicle(item);
  item->SetMoveOutFlag();

  bool ok = PutInSlot(slot, item);

  // create network object
  if (GWorld->GetMode() == GModeNetware)
    GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);

  return ok ? item : NULL;
}

InventoryItem *Person::DropFromSlot(InventorySlotId slot)
{
  if (slot < 0 || slot >= NInventorySlotId) return NULL;

  InventoryItem *item = _inventorySlots[slot];
  if (item)
  {
    _inventorySlots[slot] = NULL;
    item->UpdateShape(NULL);
    if (item == _itemInHands) RemoveItemInHands();

    Vector3Val pos = FutureVisualState().Position();
    Vector3Val dir = FutureVisualState().Direction();
    item->ItemDrop(this, pos, dir);
  }

  return item;
}

const InventoryItem *Person::GetInSlot(InventorySlotId slot) const 
{
  return (slot >= 0 && slot< NInventorySlotId) ? _inventorySlots[slot] : NULL;
};

InventoryItem *Person::GetInSlot(InventorySlotId slot)  
{
  return (slot >= 0 && slot< NInventorySlotId) ? _inventorySlots[slot] : NULL;
};

bool Person::AddMagazine(EntityAIFull *entity, Magazine *magazine)
{
  DoAssert(entity == this);
  return AddItem(magazine);
}

bool Person::RemoveMagazine(Magazine *magazine)
{
  return RemoveItem(magazine);
}

bool Person::DeleteMagazine(RStringB name)
{
  return DeleteFirstItem([name](const InventoryItem *item) -> bool
  {
    const Magazine *magazine = item->GetMagazine();
    if (!magazine) return false; // continue
    const MagazineType *type = magazine->Type();
    return type && stricmp(type->GetName(), name) == 0; // delete or continue
  });
}

void Person::DeleteMagazines(const std::function<bool(const Magazine *magazine)> &func)
{
  return DeleteItems([&func](const InventoryItem *item) -> bool
  {
    const Magazine *magazine = item->GetMagazine();
    return magazine && func(magazine);
  });
}

void Person::DeleteAllMagazines()
{
  return DeleteItems([](const InventoryItem *item) -> bool
  {
    const Magazine *magazine = item->GetMagazine();
    return magazine != NULL;
  });
}

WeaponModePtr Person::GetCurrentWeapon() const
{
  WeaponModePtr ptr;
  if (!_itemInHands) return ptr;
  Weapon *currentWeapon = _itemInHands->GetWeapon();

  if (currentWeapon)
  {
    ptr._weapon = currentWeapon;
    ptr._muzzle = currentWeapon->GetCurrentMuzzle();
    if (ptr._muzzle >= 0 && ptr._muzzle < ptr._weapon->NMuzzles())
    {
      const Muzzle &muzzle = ptr._weapon->GetMuzzle(ptr._muzzle);
      ptr._mode = muzzle._currentMode;
    }
  }

  return ptr;
}

void Person::SetCurrentWeapon(const WeaponModePtr &ptr)
{
  TakeItemToHands(ptr._weapon.GetLink());
  if (ptr.IsValid())
  {
    Muzzle &muzzle = ptr._weapon->SetCurrentMuzzle(ptr._muzzle);
    muzzle._currentMode = ptr._mode;
  }
}

void Person::HandleItemInHands(InventoryItem *item)
{
	if (_itemInHands && _itemInHands != item)
	{
		_itemInHands->SwitchOn(false);
		_itemInHands->SwitchOff();

		if (_inventorySlots[ISHands])
		{
			InventoryItem *oldItem = _inventorySlots[ISHands];
			DoAssert(oldItem == _itemInHands);

			// low lever removal of the item
			_inventorySlots[ISHands] = nullptr;
			oldItem->UpdateShape(nullptr);
			// unassign the item, but leave it in the _outVehicles
			// do not handle it if already handled (for example new owner set)
			oldItem->CancelHierarchyParent(this);

			if (!_handsTransferInProgress && !AddItem(oldItem))
			{
				Vector3Val pos = FutureVisualState().Position();
				Vector3Val dir = FutureVisualState().Direction();
				oldItem->ItemDrop(this, pos, dir);
			}
		}
  }
}

void Person::TakeItemToHands(InventoryItem *item)
{
  HandleItemInHands(item);
  // simplified implementation without handling of animations
  _itemInHands = item;
}

#if _ENABLE_PHYSICS
/*
void Person::RemoveItemInHandsThrow()
{
  //HandleItemInHands(nullptr);
  // simplified implementation without handling of animations
  _itemInHands = NULL;
}
*/
#endif

void Person::RemoveItemInHands()
{
  HandleItemInHands(nullptr);
  // simplified implementation without handling of animations
  _itemInHands = NULL;
}

void Person::RemoveAllWeapons(bool removeItems)
{
  _weaponsState.DeleteAllWeapons(this, removeItems);
}

void Person::RemoveAllItems()
{
  DeleteWeapons([](const Weapon *weapon) -> bool
  {
    const WeaponType *type = weapon->Type();
    return !type || (type->_weaponType & MaskSlotInventory) != 0; // delete if have no type or if in the inventory slot
  });
}

void Person::MinimalWeapons()
{
  RemoveAllMagazines();
}

void Person::RemoveAllMagazines()
{
  _weaponsState.DeleteAllMagazines(this);
}

/*!
\patch_internal 1.63 Date 6/3/2002 by Jirka
- Fixed: Better handgun manipulation
*/
void Person::OnWeaponChanged()
{
  WeaponModePtr ptr = _weaponsState.FirstWeapon(this);
  if (ptr.IsValid())
    _weaponsState.SelectWeapon(this, ptr, true);
  else
    _weaponsState.SelectWeapon(this, WeaponModePtr(), true);
  if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
    GWorld->UI()->ResetVehicle(this);
}

void Person::SimulateWeaponInputs(InputInterface *input, AIBrain *unit, const TurretContext &context)
{
  for (int i=UAItemFirst; i<=UAItemLast; i++)
  {
    if (input->GetActionToDo((UserAction)i))
    {
      void ShowShortcuts();
      ShowShortcuts();

      int index = i - UAItemFirst;
      const InventoryItemType *type = GetItemShortcut(index);
      if (type)
      {
        InventoryItem *bound = _shortcutItems[index];
        if (bound && FindItem(bound) && bound->Type() == type)
        {
          bound->UseFromInventory(this);
          continue;
        }
        // find item of this type
        ForEachItem([this, type](InventoryItem *item) -> bool
        {
          if (item->Type() == type)
          {
            item->UseFromInventory(this);
            return true; // done
          }
          return false; // continue
        });
      }
    }
  }
  base::SimulateWeaponInputs(input,unit,context);
}

const InventoryItemType *Person::GetItemShortcut(int index) const
{
  if (index >= 0 && index < NItemShortcuts)
  {
    InventoryItem *item = _itemShortcuts[index];
    if (item) return item->Type();
  }
  return NULL;
}

void Person::SetItemShortcut(int index, const InventoryItemType *itemType)
{
  void ShowShortcuts();
  if (index >= 0 && index < NItemShortcuts) _shortcutItems[index] = NULL;

  if (index<0)
  {
    // find first free slot to add
    int slot = -1;
    for (int i=0; i<lenof(_itemShortcuts); i++)
    {
      if (!_itemShortcuts[i])
      {
        if (slot<0) slot = i;
      }
      else if (_itemShortcuts[i]->GetType()==itemType)
      { // do not add a shortcut if the type already has it
        return;
      }
    }
    if (slot>=0)
    {
      if (!_itemShortcuts[slot] || _itemShortcuts[slot]->Type() != itemType) ShowShortcuts();
      _itemShortcuts[slot] = itemType->CreateUIObject();
    }
  }
  else if (index < NItemShortcuts)
  {
    if (itemType)
    {
      if (!_itemShortcuts[index] || _itemShortcuts[index]->Type() != itemType) ShowShortcuts();
      _itemShortcuts[index] = itemType->CreateUIObject();
    }
    else
    {
      if (_itemShortcuts[index]) ShowShortcuts();
      _itemShortcuts[index] = NULL;
    }
  }
}

void Person::SetItemShortcutInstance(int index, InventoryItem *item)
{
  if (index < 0 || index >= NItemShortcuts) return;
  if (item && !FindItem(item)) return;
  SetItemShortcut(index, item ? item->Type() : NULL);
  _shortcutItems[index] = item;
}

int Person::FindItemShortcutIndex(const InventoryItemType *itemType)
{
  for( int i = 0; i < NItemShortcuts; ++i )
  {
    if( _itemShortcuts[i] && (_itemShortcuts[i]->GetType() == itemType) )
    {
      return i;
    }
  }

  return -1;
}

InventoryItem *Person::GetItemShortcutObject(int index) const
{
  if (index >= 0 && index < NItemShortcuts) return _itemShortcuts[index];
  return NULL;
}

void Person::RemoveItemShortcut(InventoryItem *item)
{
  const InventoryItemType* type = item->Type();
  if( !type )
    return;

  // remove shortcut if it was the last item of this type
  if( CountItems(type) == 0 )
  {
    int index = FindItemShortcutIndex(type);
    if( index != -1 )
      SetItemShortcut(index, NULL);
  }

  if( !item->GetSupply() )
    return;

  item->ForEachItemCargo([this](InventoryItem *itemChild) -> bool
  {
    RemoveItemShortcut(itemChild);
    return false;
  }, false);
}

void Person::OnItemAdded(InventoryItem *item)
{
#ifdef USER_raist
	if( GetWResource() )
		GetWResource()->OnRemoveObject(item);
#endif

  if (item->CanAutoShortcut())
  {
    const InventoryItemType *type = item->Type();
    if (type)
    {
      SetItemShortcut(-1, type);
    }
  }
}

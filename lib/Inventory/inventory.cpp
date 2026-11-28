#include "../wpch.hpp"
#include "inventory.hpp"
#include "recipe.hpp"
#include <lib/proxyWeapon.hpp>
#include <lib/world.hpp>
#include <lib/gameStateExt.hpp>
#include <lib/drawText.hpp>
#include <lib/Network/network.hpp>
#include <lib/WorldRes/WorldRes.h>
#include <lib/Shape/specLods.hpp>
#include <lib/fileLocator.hpp>

#if _ENABLE_HAND_IK
# include <lib/motion.hpp>
#endif

#if _PROFILE
# pragma optimize( "", off )
#endif

void InventoryItemType::ComputeBoundingBox(const LODShape *shape, const Shape *memory, Vector3* boundingBox)
{
  bool set = false;
  if (memory)
  {
    int bb1 = shape->PointIndex(memory, "boundingbox_min");
    int bb2 = shape->PointIndex(memory, "boundingbox_max");
    if (bb1 >= 0 && bb2 >= 0)
    {
      const Vector3 pos1 = memory->Pos(bb1);
      const Vector3 pos2 = memory->Pos(bb2);
      for (int i = 0; i<3; i++)
      {
        if (pos1[i] < pos2[i])
        {
          boundingBox[0][i] = pos1[i];
          boundingBox[1][i] = pos2[i];
        }
        else
        {
          boundingBox[0][i] = pos2[i];
          boundingBox[1][i] = pos1[i];
        }
      }
      set = true;
    }
  }
  if (!set)
  {
    const Vector3 *minMax = shape->MinMax();
    boundingBox[0] = minMax[0];
    boundingBox[1] = minMax[1];
  }
}

InventoryItemType::InventoryItemType(ParamEntryPar param)
  : base(param)
{
  _inventorySlot = (InventorySlotId)-1;
  _handheld = false;
  _autoQuickbar = false;
  _openable = false;
  _melee = false;
  _rotateDropped = false;
  _rotateInvert = false;
  _rotateZ = false;
  _itemWidth = 1;
  _itemHeight = 1;
  _dispersionModifier = 0;
  _dexterityModifier = 0;
  _stackedMax = 0;
  _spawnDamageMax = 0;
  _containerSlot = "";

  _boundingBox[0] = VZero;
  _boundingBox[1] = VZero;

  _inventoryOrientation = M3Identity;
}

void InventoryItemType::Load(ParamEntryPar cls, const char *shape)
{
  base::Load(cls, shape);

   _inventorySlot = (InventorySlotId)-1;

  RStringB inventorySlot = cls >> "inventorySlot";
  if (!inventorySlot.IsEmpty())
  {
    _inventorySlot = (InventorySlotId)InventorySlotTypes.AddValue(inventorySlot, false);
    if (_inventorySlot == -1)
    {
      RptF("Unknown inventory slot '%s' in %s", cc_cast(inventorySlot), cc_cast(cls.GetContext()));
    }
  }

  ParamEntryPtr entry = cls.FindEntry("attachmentCondition");
  if (entry)
  {
    _attachmentCondition = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_attachmentCondition, _attachmentConditionExp, &globals);
    GGameState.EndContext(gshandle);
#endif
  }

  entry = cls.FindEntry("conditionDetach");
  if (entry)
  {
    _conditionDetach = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_conditionDetach, _conditionDetachExp, &globals);
    GGameState.EndContext(gshandle);
#endif
  }

  entry = cls.FindEntry("dispersionCondition");
  if (entry)
  {
    _conditionDispersion = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_conditionDispersion, _conditionDispersionExp, &globals);
    GGameState.EndContext(gshandle);
#endif
    _dispersionModifier = cls >> "dispersionModifier";
  }

  entry = cls.FindEntry("dexterityCondition");
  if (entry)
  {
    _conditionDexterity = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_conditionDexterity, _conditionDexterityExp, &globals);
    GGameState.EndContext(gshandle);
#endif
    _dexterityModifier = cls >> "dexterityModifier";
  }

  entry = cls.FindEntry("inventoryCondition");
  if (entry)
  {
    _conditionInventory = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_conditionInventory, _conditionInventoryExp, &globals);
    GGameState.EndContext(gshandle);
#endif
  }

  entry = cls.FindEntry("tooltip");
  if (entry)
  {
    _tooltip = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_tooltip, _tooltipExp, &globals);
    GGameState.EndContext(gshandle);
#endif
  }

  entry = cls.FindEntry("quantity");
  if (entry)
  {
    _quantity = *entry;
#if USE_PRECOMPILATION
    GameDataNamespace globals(NULL, RString(), false); // temporary globals
    GameVarSpace local(GGameState.GetContext(), false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(_quantity, _quantityExp, &globals);
    GGameState.EndContext(gshandle);
#endif
  }

  ParamEntryVal attachments = cls >> "attachments";
  if (attachments.IsArray())
  {
    int n = attachments.GetSize();
    _attachments.Realloc(n); _attachments.Resize(n);
    for (int i=0; i<n; i++)
    {
      RStringB slot = attachments[i];
      _attachments[i]._id = (InventorySlotId)InventorySlotTypes.AddValue(slot, false);
      ParamEntryPtr entry = (Pars >> "CfgInventorySlots").FindEntry(slot);
      if (entry)
      {
        _attachments[i]._displayName = (*entry) >> "displayName";
        RString FindPicture(RString name);
        _attachments[i]._icon = GlobLoadTexture(FindPicture((*entry) >> "icon"));
      }
      else _attachments[i]._displayName = slot;
    }
  }

  entry = cls.FindEntry("baseAttachments");
  if (entry && entry->IsArray())
  {
    int n = entry->GetSize();
    _baseAttachments.Realloc(n); _baseAttachments.Resize(n);
    for (int i=0; i<n; i++) _baseAttachments[i] = (*entry)[i];
  }

  _handheld = cls >> "handheld";
  _autoQuickbar = cls >> "autoQuickbar";
  _rotateDropped = cls >> "rotateDropped";
  _rotateZ = cls >> "rotateZ";
  _cargoParent = cls >> "cargoClass";

  entry = cls.FindEntry("rotateInvert");
  if (entry)
  {
    _rotateInvert = *entry;
  }

  entry = cls.FindEntry("openable");
  if (entry)
  {
    _openable = cls >> "openable";
  }

  entry = cls.FindEntry("stackedMax");
  if (entry)
  {
    _stackedMax = *entry;
  }
  entry = cls.FindEntry("spawnDamageMax");
  if (entry)
  {
    _spawnDamageMax = *entry;
  }

  ParamEntryVal itemSize = cls >> "itemSize";
  if (itemSize.IsArray() && itemSize.GetSize() == 2)
  {
    _itemWidth = itemSize[0];
    _itemHeight = itemSize[1];
  }

  entry = cls.FindEntry("containerSlot");
  if (entry && entry->IsTextValue())
  {
    _containerSlot = *entry;
  }

  _armAction = cls >> "armAction";

  entry = cls.FindEntry("animClass");
  if (entry)
  {
    _animClass = *entry;
  }

  entry = cls.FindEntry("Melee");
  if( entry )
  {
    _melee = true;
  }
}

void InventoryItemType::InitShape()
{
  base::InitShape();

  if (!_shape) return;

  const Shape *memory = _shape->MemoryLevel();
  ComputeBoundingBox(_shape, memory, _boundingBox);
  if (memory)
  {
    if (_shape->MemoryPointExists("invView"))
    {
      Vector3 dir = _shape->MemoryPoint("invView") + _shape->BoundingCenter(); // vector from the O2 origin to the memory point
      // dir.Normalize();
      if (dir.SquareSize() > 0)
      {
        // rotate to the given direction
        _inventoryOrientation.SetDirectionAndUp(-dir, VUp);
        _inventoryOrientation = _inventoryOrientation.InverseRotation();
      }
      else
      {
        _inventoryOrientation = M3Identity;
      }
    }
  }

#if _ENABLE_HAND_IK
  // read animation of weapon holding
  ParamEntryPtr entryTmp = _par->FindEntry("handAnim");
  if (entryTmp)
  {
    DoAssert(entryTmp->IsArray());
    int n = entryTmp->GetSize() / 2;
    for(int i = 0; i < n; i++)
    {
      RString skeletonName = (*entryTmp)[2*i];
      AnimationRTName name;
      name.name = GetAnimationName((*entryTmp)[2*i+1]);
      name.skeleton = Skeletons.New(skeletonName);
      name.reversed = false;
      name.streamingEnabled = false;

      Ref<AnimationRT> anim = AnimationRTBank.New(name);
      // we want all hand animations to be loaded all the time
      // streaming them in and out would be too complicated, and they are short anyway
      anim->AddPreloadCount();
      _handAnim.Add(anim);
    }
  } 
#endif 
}

void InventoryItemType::DeinitShape()
{
#if _ENABLE_HAND_IK
  for (int i=0; i<_handAnim.Size(); i++)
  {
    _handAnim[i]->ReleasePreloadCount();
  }
  _handAnim.Clear();
#endif

  base::DeinitShape();
}

#if _ENABLE_HAND_IK
/// find animation with such skeleton...
Ref<AnimationRT> InventoryItemType::GetHandAnim(Skeleton * sk) const
{
  for (int i=0; i<_handAnim.Size(); i++)
  {
    if (_handAnim[i]->GetSkeleton() == sk)
      return _handAnim[i];
  }
  return NULL;
}
#endif

Object* InventoryItemType::CreateObject(bool fullCreate) const
{
  return new InventoryItem(this, fullCreate);
}

InventoryItem *InventoryItemType::CreateUIObject() const
{
  InventoryItem *item = dyn_cast<InventoryItem>(CreateObject(true));
  for (int i=0; i<_baseAttachments.Size(); i++)
  {
    Ref<InventoryItem> attachment = dyn_cast<InventoryItem>(GWorld->NewVehicleWithID(_baseAttachments[i]));
    if (attachment)
    {
      attachment->SetMoveOutDone(NULL); // do not try to move out
      item->AddAttachment(attachment);
    }
  }
  return item;
}

Texture *InventoryItemType::GetAttachmentIcon(int i) const
{
  return _attachments[i]._icon;
}

InventoryItem::InventoryItem(const EntityAIType *type, bool fullCreate)
  : base(type, fullCreate), _ui(false), _inventoryRotated(false), _opened(true), _animationsEnabled(true), _isWorldAttached(false)
{
  int n = Type()->_attachments.Size();
  _attachments.Realloc(n); _attachments.Resize(n);
}

InventoryItem::~InventoryItem()
{
  if (_isWorldAttached)
    GWorld->RemoveAttachment(this);
}

AnimationStyle InventoryItem::IsAnimated(int level) const
{
  // do not enforce animation (in EntityAI)
  return Entity::IsAnimated(level);
}

int InventoryItem::GetObjSpecial(int shapeSpecial) const
{
  return _ui ? shapeSpecial | DisableSun : shapeSpecial;
}

int InventoryItem::GetInventoryWidth() const
{
  const InventoryItemType *type = Type();
  if (!type) return 0;
  return _inventoryRotated ? type->GetItemHeight() : type->GetItemWidth();
}

int InventoryItem::GetInventoryHeight() const
{
  const InventoryItemType *type = Type();
  if (!type) return 0;
  return _inventoryRotated ? type->GetItemWidth() : type->GetItemHeight();
}

bool InventoryItem::CanRotateInInventory() const
{
  if (!Type() || GetInventoryWidth() == GetInventoryHeight()) return false;
  EntityAI *parent = dyn_cast<EntityAI>(GetHierarchyParent());
  if (!parent || !parent->GetSupply()) return false;
  return parent->GetSupply()->GetItemsCargo().CanRotateItem(this);
}

bool InventoryItem::RotateInInventory()
{
  if (!CanRotateInInventory()) return false;
  _inventoryRotated = !_inventoryRotated;
  return true;
}

bool InventoryItem::CheckAttachmentCondition(const InventoryItem *attachment) const
{
  const InventoryItemType *type = attachment->Type();
  if (!type) return false;

  const RStringB &cond = type->_attachmentCondition;
  if (cond.IsEmpty()) return true;

  // prepare variables for condition expression if available
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(unconst_cast(attachment)), true);
  local.VarSet("_parent", GameValueExt(unconst_cast(this)), true);

  // check condition
  GameDataNamespace *globals = GWorld->GetMissionNamespace();
  int gshandle = GGameState.BeginContext(&local);
#if USE_PRECOMPILATION
  bool ok = GGameState.EvaluateBool(cond, type->_attachmentConditionExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".attachmentCondition"));
#else
  bool ok = GGameState.EvaluateBool(cond, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gshandle);

  return ok;
}

bool InventoryItem::CheckConditionDetach(const InventoryItem *attachment) const
{
  const InventoryItemType *type = attachment->Type();
  if (!type) return false;

  const RStringB &cond = type->_conditionDetach;
  if (cond.IsEmpty()) return true;

  // prepare variables for condition expression if available
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(unconst_cast(attachment)), true);
  local.VarSet("_parent", GameValueExt(unconst_cast(this)), true);

  // check condition
  GameDataNamespace *globals = GWorld->GetMissionNamespace();
  int gshandle = GGameState.BeginContext(&local);
#if USE_PRECOMPILATION
  bool ok = GGameState.EvaluateBool(cond, type->_conditionDetachExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".conditionDetach"));
#else
  bool ok = GGameState.EvaluateBool(cond, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gshandle);

  return ok;
}

bool InventoryItem::CheckCargoClass(const InventoryItem *parent, const InventoryItem *item) const
{
  /*
  Function checks whether parent item has a defined cargoClass, if so, only lets items that
  are children of this class be placed inside it
  */
  const InventoryItemType *parenttype = parent->Type();
  if (!parenttype) return false;

  const InventoryItemType *type = item->Type();
  if (!type) return false;

  RString cargoclass = parenttype->GetCargoParent();
  if (!cargoclass.IsEmpty())
  {
    if (!type->IsKindOf(cargoclass)) return false;
  }
  return true;
}

bool InventoryItem::CheckInventoryCondition(const EntityAI *item) const
{
  const InventoryItemType *type = Type();
  if (!type) return false;

  const RStringB &cond = type->_conditionInventory;
  if (cond.IsEmpty()) return true;

  // prepare variables for condition expression if available
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(unconst_cast(this)), true);
  local.VarSet("_parent", GameValueExt(unconst_cast(item)), true); //item==nullptr for GROUND

  // check condition
  GameDataNamespace *globals = GWorld->GetMissionNamespace();
  int gshandle = GGameState.BeginContext(&local);
#if USE_PRECOMPILATION
  bool ok = GGameState.EvaluateBool(cond, type->_conditionInventoryExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".inventoryCondition"));
#else
  bool ok = GGameState.EvaluateBool(cond, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gshandle);

  return ok;
}

const bool InventoryItem::CheckConditionDispersion(const InventoryItem *attachment, const EntityAIFull *person) const
{
  const InventoryItemType *type = attachment->Type();
  if (!type) return false;

  const RStringB &cond = type->_conditionDispersion;
  if (cond.IsEmpty()) return false;

  // prepare variables for condition expression if available
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(unconst_cast(attachment)), true);
  local.VarSet("_agent", GameValueExt(unconst_cast(person)), true);

  // check condition
  GameDataNamespace *globals = GWorld->GetMissionNamespace();
  int gshandle = GGameState.BeginContext(&local);
#if USE_PRECOMPILATION
  bool ok = GGameState.EvaluateBool(cond, type->_conditionDispersionExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".conditionDispersion"));
#else
  bool ok = GGameState.EvaluateBool(cond, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gshandle);

  return ok;
}

const bool InventoryItem::CheckConditionDexterity(const InventoryItem *attachment, const EntityAIFull *person) const
{
  const InventoryItemType *type = attachment->Type();
  if (!type) return false;

  const RStringB &cond = type->_conditionDexterity;
  if (cond.IsEmpty()) return false;

  // prepare variables for condition expression if available
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(unconst_cast(attachment)), true);
  local.VarSet("_agent", GameValueExt(unconst_cast(person)), true);

  // check condition
  GameDataNamespace *globals = GWorld->GetMissionNamespace();
  int gshandle = GGameState.BeginContext(&local);
#if USE_PRECOMPILATION
  bool ok = GGameState.EvaluateBool(cond, type->_conditionDexterityExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".conditionDexterity"));
#else
  bool ok = GGameState.EvaluateBool(cond, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gshandle);

  return ok;
}

void InventoryItem::SwitchOff()
{
  // switch off myself (namely lights)
  const InventoryItemType *type = Type();
  _lights.Switch(false, this, type->_lights);
  // Update the lights after changes
  _lights.UpdateAggLights(this);
  //SwitchOn(false); // do not switchOn(false), as lights lights are switched off which is sufficient; for radios and walkie talkie we would preffer not to switch it off inside cargo
  // switch off all my descendant
  ForEachAttachment([](InventoryItem *item) -> bool { item->SwitchOff(); return false; });
}

RefR<INode> InventoryItem::GetTooltip()
{
  const RStringB &tooltip = Type()->_tooltip;
  if (tooltip.IsEmpty()) return nullptr;

  // make member variables and _this accessible in the expression
  int gsmembers = GGameState.BeginContext(GetVars());
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(this), true);
  int gslocal = GGameState.BeginContext(&local);
  GameDataNamespace *globals = GWorld->GetMissionNamespace();

#if USE_PRECOMPILATION
  GameValue value = GGameState.Evaluate(tooltip, Type()->_tooltipExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".tooltip"));
#else
  GameValue value = GGameState.Evaluate(tooltip, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gslocal);
  GGameState.EndContext(gsmembers);

  if (value.GetNil()) return nullptr;
  if (value.GetType() == GameText)
  {
    return static_cast<GameDataText *>(value.GetData())->GetValue();
  }
  if (value.GetType() == GameString) return CreateTextASCII(NULL, value);
  return nullptr;
}

RefR<INode> InventoryItem::GetQuantity()
{
  const RStringB &quantity = Type()->_quantity;
  if (quantity.IsEmpty()) return nullptr;

  // make member variables and _this accessible in the expression
  int gsmembers = GGameState.BeginContext(GetVars());
  GameVarSpace local(GGameState.GetContext(), false);
  local.VarSet("_this", GameValueExt(this), true);
  int gslocal = GGameState.BeginContext(&local);
  GameDataNamespace *globals = GWorld->GetMissionNamespace();

#if USE_PRECOMPILATION
  GameValue value = GGameState.Evaluate(quantity, Type()->_quantityExp, GameState::EvalContext::_reportUndefined, globals, GetName() + RString(".quantity"));
#else
  GameValue value = GGameState.Evaluate(quantity, GameState::EvalContext::_reportUndefined, globals);
#endif
  GGameState.EndContext(gslocal);
  GGameState.EndContext(gsmembers);

  if (value.GetNil()) return nullptr;
  if (value.GetType() == GameText)
  {
    return static_cast<GameDataText *>(value.GetData())->GetValue();
  }
  if (value.GetType() == GameString) return CreateTextASCII(NULL, value);
  return nullptr;
}

bool InventoryItem::GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const
{
  switch (action->GetType())
  {
  case ATCrafting:
    if (unit && unit->IsFreeSoldier())
    {
      const ActionCrafting *actionTyped = static_cast<const ActionCrafting *>(action);
      const Recipe *recipe = Recipes.Find(actionTyped->GetRecipe());
      if (recipe)
      {
        RefR<INode> node = recipe->GetDescription(unit->GetPerson(), unconst_cast(this), actionTyped->GetItem());
        params.Add(node);
        params.Add(node);
        return true;
      }
    }
    return false;
  case ATDetachAttachment:
    if (unit && unit->IsFreeSoldier())
    {
      const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
      int index = actionTyped->GetIndex();
      const InventoryItem *item = GetAttachment(index);
      if (item)
      {
        params.Add(CreateTextASCII(NULL, item->GetDisplayName()));
        return true;
      }
    }
    return false;
  }

  return base::GetActionParams(params, action, unit);
}

void InventoryItem::PerformAction(const Action *action, AIBrain *unit)
{
  switch (action->GetType())
  {
  case ATTakeItem:
    if (unit && unit->IsFreeSoldier())
    {
      if (IsInLandscape())
      {
        Person *person = unit->GetPerson();
        if (person) person->AddItem(this);
      }
    }
    return;
  case ATTakeItemIntoHands:
    if (unit && unit->IsFreeSoldier())
    {
      if (IsInLandscape())
      {
        Person *person = unit->GetPerson();
        if (person)
        {
          DoAssert(!person->GetInSlot(ISHands));
          person->PutInSlot(ISHands, this);
          person->TakeItemToHands(this);
        }
      }
    }
    return;
  case ATCrafting:
    if (unit && unit->IsFreeSoldier())
    {
        const ActionCrafting *actionTyped = static_cast<const ActionCrafting *>(action);
        const Recipe *recipe = Recipes.Find(actionTyped->GetRecipe());
        if (recipe)
        {
          if (!GetNetworkManager().IsServerOwner())
            GetNetworkManager().ProcessRecipe(actionTyped->GetRecipe(), unit->GetPerson(), this, actionTyped->GetItem()); // ask server to execute the action
          else
            recipe->Process(unit->GetPerson(), this, actionTyped->GetItem());
        }
    }
    return;
  case ATDropItem:
    break;
  case ATDetachAttachment:
    if (unit && unit->IsFreeSoldier())
    {
      const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
      int index = actionTyped->GetIndex();
      InventoryItem *item = GetAttachment(index);
      if (item && unit->GetPerson() && unit->GetPerson()->CanAddItem(item))
      {
        RemoveOwnedItem(item);
        unit->GetPerson()->AddItem(item);
      }
    }
    return;
  }

  base::PerformAction(action, unit);
}

void InventoryItem::GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit)
{
  if (unit && unit->IsFreeSoldier())
  {
    Person *person = unit->GetPerson();
    if (person)
    {
      if (IsInLandscape())
      {
        //add item to inventory
        if (person->CanAddItem(this))
        {
          //has room in inventory
          UIAction action(new ActionBasic(ATTakeItem, this));
          add(action);
        }
        if (Type() && !person->GetItemInHands())
        {
          //has no room, take into hands (prototype)
          DoAssert(!person->GetInSlot(ISHands));
          UIAction action(new ActionBasic(ATTakeItemIntoHands, this));
          add(action);
        }
      }

      //openable containers
      if (CanOpenItem())
      {
        //open item
        UIAction action(new ActionBasic(ATGear, this));
        //int index = GetType()->GetSupplyPointIndex();
        //if (index >= 0) action._memoryPoints.Add(index);
        add(action);
      }

      for (int i=0; i<NAttachments(); i++)
      {
        InventoryItem *item = GetAttachment(i);
        if (item && person->CanAddItem(item))
        {
          Entity *parent = item->GetHierarchyParent();
          if (parent && !parent->CanRemoveOwnedItem(item)) continue;

          UIAction action(new ActionIndex(ATDetachAttachment, this, i));
          add(action);
        }
      }

      // crafting recipes
      Recipes.Collect(person, this, NULL,
        [person, this, &add](const Recipe *recipe) -> bool
        { 
          UIAction action(new ActionCrafting(ATCrafting, this, nullptr, recipe->_name));
          add(action);
          return false; // continue
        });

      // combination with the item in hands
      InventoryItem *inHands = person->GetItemInHands();
      if (inHands && this != inHands)
      {
        Recipes.Collect(person, this, inHands,
          [person, this, inHands, &add](const Recipe *recipe) -> bool
          {
            UIAction action(new ActionCrafting(ATCrafting, this, inHands, recipe->_name));
            add(action);
            return false; // continue
          });
      }
    }
  }

  base::GetActions(add, unit);
}

bool InventoryItem::UseFromInventory(Person *owner)
{
  if (IsEventHandler(EEUsed))
  {
    // can be replaced by OnEventRetBool if needed
    OnEvent(EEUsed, owner);
    return true;
  }

  if (Type() && Type()->IsHandheld())
  {
    const InventoryItem *inHands = owner->GetItemInHands();
    if (inHands == this)
    {
      // remove the item
      owner->TakeItemToHands(NULL);
    }
    else
    {
      // take the item
      owner->TakeItemToHands(this);
    }
    return true;
  }

  return false;
}

bool InventoryItem::CanAutoShortcut() const
{
  if (Type() && Type()->IsAutoQuickbar())
  {
    return true;
  }
  return false;
  //return IsEventHandler(EEUsed);
}

float InventoryItem::GetTemperature() const
{
  GameValue val;
  const GameVarSpace * const vars = GetVars();
  if (vars && vars->VarGet("temperature",val))
  {
    const float temp = static_cast<float>(val);
    return temp;
  }
  return 0.0f;
}

void InventoryItem::SetTemperature(float temp)
{
  if (GameVarSpace * const vars = GetVars())
    vars->VarSet("temperature", temp);
}

bool InventoryItem::CanOpenItem() const
{
  //used to check if can be opened, such as fridge or container
  if (Type() && Type()->IsOpenable())
  {
    return true;
  }
  return false;
}

void InventoryItem::ItemMoveIn()
{
  switch (_moveOutState)
  {
  case MOIn:
    // already there
    break;
  case MOMovingOut:
    // cancel moving in
    CancelMoveOutInProgress();
    break;
  case MOMovedOut:
    // move it now
    GWorld->AddSlowVehicle(this);
    GWorld->RemoveOutVehicle(this);
    ResetMoveOut();
    break;
  }
}

void InventoryItem::ItemMoveOut(Entity *parent)
{
  switch (_moveOutState)
  {
  case MOIn:
    MoveOut(parent);
    break;
  case MOMovingOut:
  case MOMovedOut:
    // already in _outVehicles or moving there
    SetHierarchyParent(parent);
    break;
  }

  // TODO: Network object handling
}

void InventoryItem::ItemDestroy()
{
  // when on the bot-client let others know the item is to delete
  // this must not be done on normal clients, as they can be destroying the items when going out of the bubble
  if (GetNetworkManager().IsServerOwner())
  {
    GetNetworkManager().DeleteObject(GetNetworkId());
  }

  switch (_moveOutState)
  {
  case MOIn:
    SetDelete();
    break;
  case MOMovingOut:
    CancelMoveOutInProgress();
    SetDelete();
    break;
  case MOMovedOut:
    GWorld->RemoveOutVehicle(this);
    break;
  }
}

Vector3 InventoryItem::GetPosForItemDrop(Person* owner, Vector3Par refPos, Vector3Par dir, float itemBoundRadius, bool inCollision)
{
  float maxOffset = 0.8f;
  float radius = 0.5f;
  Vector3 res = refPos;

  if( !inCollision )
  {
    Vector3 forward = dir;
    Vector3 aside(-dir.z, 0, dir.x);

    Vector3 corners[9] =
    {
      refPos + forward * radius,
      refPos + aside * radius,
      refPos - forward * radius,
      refPos - aside * radius,
      refPos + (forward + aside) * radius,
      refPos + (forward - aside) * radius,
      refPos - (forward + aside) * radius,
      refPos - (forward - aside) * radius,
      refPos
    };

    bool found = false;
    for( int i = 0; i < 9; ++i )
    {
      res = corners[i];

      Vector3 s = res + VUp * 0.1f;
      Vector3 e = res - VUp * 3.0f;

      CollisionBuffer col;
      GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(), col, Landscape::FilterIgnoreOne(owner), s, e, 0.8f * itemBoundRadius, ObjIntersectFire, ObjIntersectView, CF_NEAREST_CONTACT);
      if( col.Size() == 0 )
      {
        found = true;
        break;
      }
      InventoryItem* item = dyn_cast<InventoryItem>(col[0].object.GetRef());
      if( item == nullptr )
      {
        found = true;
        break;
      }
    }

    if( found )
      maxOffset = 0.2f;
  }

  float offsetX = maxOffset * GRandGen.RandomValue() - maxOffset / 2;
  float offsetZ = maxOffset * GRandGen.RandomValue() - maxOffset / 2;
  res[0] += offsetX;
  res[2] += offsetZ;

  return res;
}

void InventoryItem::ItemDrop(Person *owner, Vector3Par pos, Vector3Par dir, float maxDist)
{
  Vector3 upOffset = VUp * 1;
  bool isWeapon = dyn_cast<Weapon>(this) != nullptr;

  // get bound radius of item
  const InventoryItemType *type = Type();
  const Vector3 *bb = type ? type->BoundingBox() : GetShape()->MinMax();
  float maxBoundRadius = floatMax(bb[0].SizeXZ(), bb[1].SizeXZ());

  Vector3 sourcePos = pos + (owner ? upOffset : VZero);
  Vector3 targetPos = sourcePos + maxDist * dir;

  CollisionBuffer col;
  GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(), col, Landscape::FilterIgnoreTwo(this, owner), sourcePos, targetPos, 0.15f, ObjIntersectFire, ObjIntersectView, CF_NEAREST_CONTACT);
  targetPos = GetPosForItemDrop(owner, targetPos, dir, maxBoundRadius, col.Size() > 0);

  if( !isWeapon )
  {
    GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(), col, Landscape::FilterIgnoreTwo(this, owner), sourcePos, targetPos, 0.15f, ObjIntersectFire, ObjIntersectView, CF_NEAREST_CONTACT);
    if( col.Size() > 0 )
    {
      Vector3 newDir = col[0].pos - sourcePos;
      float distToCol = newDir.Normalize();
      if( distToCol <= 0 )
        targetPos = pos;
      else
        targetPos = sourcePos + newDir * (distToCol - maxBoundRadius);
    }
    else
    {
      targetPos -= dir * maxBoundRadius;
    }
  }
  else
  {
    targetPos = pos;
  }

  Matrix4 transform;
  transform.SetUpAndDirection(VUp, dir);
  transform.SetPosition(targetPos);

  PlaceOnSurfaceOwnerTest(owner, transform, true);

  SetTransform(transform);

  // check the ownership
  DoAssert(GetNetworkManager().IsServerOwner());

  // move weapon to the world
  ItemMoveIn();

  // add object to world content
  if( GetWResource() )
	  GetWResource()->OnAddObject(this,NULL);

}

void InventoryItem::ResetParentPosition(Entity *parent)
{
  /*
  Resets the position and orientation
  */
  if (parent)
  {
    InventoryItem *parentItem = dyn_cast<InventoryItem>(parent);
    if (parentItem)
    {
      RString slot = parentItem->Type()->GetContainerSlot();
      if (!slot.IsEmpty())
      {
        //container that can adjust height
        if (!parentItem->GetHierarchyParent())
        {
          Vector3Val pos = parentItem->FutureVisualState().Position();

          Matrix4 transform;
          transform.SetUpAndDirection(VUp, VForward);
          transform.SetPosition(pos);

          parentItem->PlaceOnSurfaceUnderWater(transform);
          parentItem->SetTransform(transform);
        }
      }
    }
  }
}

bool InventoryItem::MoveToGround()
{
  Entity *parent = GetHierarchyParent();
  if (!parent) return false;
  if (!CheckInventoryCondition(nullptr)) return false;
  Person *player = dyn_cast<Person>(parent);
  if (player)
  {
    return player->DropItem(this);
  }

  if (!parent->RemoveOwnedItem(this)) return false;

  const Entity *root = this;
  while (root->GetHierarchyParent()) root = root->GetHierarchyParent();

  Vector3Val pos = root->FutureVisualState().Position();;
  Vector3Val dir = root->FutureVisualState().Direction();
  ItemDrop(player, pos, dir);
  return true;
}

void InventoryItem::CancelHierarchyParent(Entity *oldParent)
{
  if (GetHierarchyParent() == oldParent)
    SetHierarchyParent(NULL);
}

Person* InventoryItem::GetPersonOwner()
{
  Entity* parent = GetHierarchyParent();
  while( parent )
  {
    Person* p = dyn_cast<Person>(parent);
    if( p )
      return p;

    parent = parent->GetHierarchyParent();
  }

  return NULL;
}

bool InventoryItem::RemoveOwnedItem(InventoryItem *item)
{
  for (int i=0; i<_attachments.Size(); i++)
  {
    if (_attachments[i] == item)
    {
      Ref<Entity> parent = item->GetHierarchyParent();

      _attachments[i] = NULL;
      OnAttachedDetachedEvent(EEItemDetached, item, InventorySlotTypes.GetName(Type()->_attachments[i]._id));

      //reset parent height if container attached
      if(parent)
        ResetParentPosition(parent);

      return true;
    }
  }

  return base::RemoveOwnedItem(item);
}

bool InventoryItem::CanRemoveOwnedItem(const InventoryItem *item) const
{
  for (int i=0; i<NAttachments(); i++)
  {
    if (GetAttachment(i) == item) return CheckConditionDetach(item);
  }

  return base::CanRemoveOwnedItem(item);
}

bool InventoryItem::CanAddOwnedItem(const InventoryItem *item) const
{
  if (GetHierarchyParent() && GetHierarchyParent() == item) return false;

  if (!item->CheckInventoryCondition(this)) return false;
  if (!CheckCargoClass(this,item)) return false;
  if (CanAddAttachment(item)) return true;

  return base::CanAddOwnedItem(item);
}

bool InventoryItem::AddOwnedItem(InventoryItem *item)
{
  if (GetHierarchyParent() && GetHierarchyParent() == item) return false;

  if (!item->CheckInventoryCondition(this)) return false;
  if (!CheckCargoClass(this,item)) return false;
  if (AddAttachment(item)) return true;

  return base::AddOwnedItem(item);
}

int InventoryItem::CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const
{
  if (!newItem->CheckInventoryCondition(this)) return -1;
  if (!CheckCargoClass(this,newItem)) return -1;

  const InventoryItemType *type = newItem->Type();
  if (!type) return -1;

  InventorySlotId slot = type->GetInventorySlot();
  int index = Type()->_attachments.FindKey(slot);
  if (index >= 0)
  {
    if (_attachments[index] == oldItem && CheckConditionDetach(oldItem)) return index;
    return -1;
  }

  return base::CanReplaceOwnedItem(oldItem, newItem);
}

bool InventoryItem::CanMoveItem(int index, const InventoryItem *item) const
{
  if (!item->CheckInventoryCondition(this)) return false;
  if (!CheckCargoClass(this,item)) return false;
  if (index >= 0)
  {
    int n = NAttachments();
    if (index < n)
      return CanAddAttachment(item, index);
    index -= n;

    if (!_supply) return false;
    const InventoryStorage &cargo = _supply->GetItemsCargo();
    return cargo.CanAddItem(item, index) >= 0;
  }
  else
  {
    return base::CanMoveItem(index, item);
  }
}

void InventoryItem::MoveItem(int index, InventoryItem *item)
{
  if (!item->CheckInventoryCondition(this)) return;
  if (!CheckCargoClass(this,item)) return;
  if (index >= 0)
  {
    int n = NAttachments();
    if (index < n)
    {
      AddAttachment(item, index);
      return;
    }
    index -= n;

    Assert(_supply);
    InventoryStorage &cargo = _supply->GetItemsCargo();
    cargo.AddItem(this, item, index);
  }
  else
  {
    base::MoveItem(index, item);
  }
}

InventoryItem *InventoryItem::GetAttachment(RString slotName)
{
  InventorySlotId slotId = (InventorySlotId)InventorySlotTypes.GetValue(slotName);
  if (slotId < 0) return nullptr;
  int index = Type()->_attachments.FindKey(slotId);
  if (index < 0) return nullptr;
  return _attachments[index];
}

InventoryItem *InventoryItem::FindAttachment(InventorySlotId slotId) const
{
  int index = Type()->_attachments.FindKey(slotId);
  if (index < 0) return nullptr;
  return _attachments[index];
}

bool InventoryItem::ChildUpdateByParent(const Entity *obj) const
{
  for (int i=0; i<_attachments.Size(); i++)
  {
    if (_attachments[i]==obj) return true;
  }
  return base::ChildUpdateByParent(obj);
}

void InventoryItem::OnAttachedDetachedEvent(EntityEvent event, InventoryItem *item, const RStringB& slot)
{
  if (!IsEventHandler(event)) return;
  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(GameValueExt(this));
  arguments.Add(GameValueExt(item));
  arguments.Add(GameValue(slot));
  OnEvent(event,value);
}

bool InventoryItem::CanAddAttachment(const InventoryItem *item, int index) const
{
  if (_attachments[index]) return false;

  // check the slot type
  const InventoryItemType *itemType = item->Type();
  if (!itemType) return false;
  InventorySlotId slotId = itemType->_inventorySlot;
  if (slotId < 0) return false;
  if (Type()->_attachments[index]._id != slotId) return false;
  return CheckAttachmentCondition(item);
}

bool InventoryItem::AddAttachment(InventoryItem *item, int index)
{
  if (_attachments[index]) return false;

  // check the slot type
  const InventoryItemType *itemType = item->Type();
  if (!itemType) return false;
  InventorySlotId slotId = itemType->_inventorySlot;
  if (slotId < 0) return false;
  if (Type()->_attachments[index]._id != slotId) return false;
  if (!CheckAttachmentCondition(item)) return false;

  // move in world
  item->ItemMoveOut(this);

  _attachments[index] = item;

  //reset height if container attached
  RString slotname = InventorySlotTypes.GetName(Type()->_attachments[index]._id);
  ResetParentPosition(this);

  OnAttachedDetachedEvent(EEItemAttached, item, slotname);
  return true;
}

bool InventoryItem::CanAddAttachment(const InventoryItem *item) const
{
  const InventoryItemType *itemType = item->Type();
  if (!itemType) return false;
  InventorySlotId slotId = itemType->_inventorySlot;
  if (slotId < 0) return false;
  int index = Type()->_attachments.FindKey(slotId);
  if (index < 0) return false;
  if (_attachments[index]) return false;
  return CheckAttachmentCondition(item);
}

bool InventoryItem::AddAttachment(InventoryItem *item)
{
  const InventoryItemType *itemType = item->Type();
  if (!itemType) return false;
  InventorySlotId slotId = itemType->_inventorySlot;
  if (slotId < 0) return false;
  int index = Type()->_attachments.FindKey(slotId);
  if (index < 0) return false;
  if (_attachments[index] != nullptr) return false;
  if (!CheckAttachmentCondition(item)) return false;

  // move in world
  item->ItemMoveOut(this);

  _attachments[index] = item;

  //reset height if container attached
  RString slotname = InventorySlotTypes.GetName(Type()->_attachments[index]._id);
  ResetParentPosition(this);

  OnAttachedDetachedEvent(EEItemAttached, item, slotname);
  return true;
}

InventoryItem *InventoryItem::CreateAttachment(RString typeName)
{
  Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewVehicleWithID(typeName));
  if (item)
  {
    item->SetMoveOutDone(NULL); // do not try to move out
    item->Init(FutureVisualState().Transform(), true); // invoke Init prior AddAttachment (_lights initialization)
    if (AddAttachment(item))
    {
      // invoke "init" event handler
      item->OnEvent(EEInit);

      // add to the world
      GWorld->AddOutVehicle(item);

      // create network object
      if (GWorld->GetMode() == GModeNetware)
        GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);

      return item;
    }
  }

  return NULL;
}

bool InventoryItem::MoveAttachment(InventoryItem *item)
{
  // check if fits
  if (!CanAddAttachment(item)) return false;

  // remove from the old owner
  Entity *parent = item->GetHierarchyParent();
  if (parent) DoVerify(parent->RemoveOwnedItem(item));

  // now should be safe to add it
  AddAttachment(item);
  return true;
}

Object *InventoryItem::GetProxyInfo(int level, int index) const
{
  const ProxyObjectTyped &proxy = GetProxyForLevel(level, index);

  // Retrieve the proxy object
  Object *proxyObj = proxy.obj;
  if (!proxyObj) return nullptr;

  // Retrieve the proxy type
  const EntityType *proxyType = proxyObj->GetEntityType();
  if (!proxyType) return nullptr;

  // Find what slot this proxy shows 
  InventorySlotId slotId = proxyType->GetInventorySlot();
  if (slotId < 0) return nullptr;

  // Check item in this slot
  return FindAttachment(slotId);
}

Object *InventoryItem::GetProxy(
  VisualStateAge age, LODShape *&shape, Object *&parentObject,
  int level, Matrix4 &transform, const Matrix4 &parentPos, int i) const
{
  Object *object = GetProxyInfo(level, i);
  if (!object) return nullptr;

  const ProxyObjectTyped &proxy = GetProxyForLevel(level, i);
  SkeletonIndex matIndex = GetProxyBoneForLevel(level,i);
  if (matIndex != SkeletonIndexNone)
  {
    Matrix4 animTransform = AnimateProxyMatrix(GetVisualStateByAge(age), level, proxy.GetTransform(), matIndex);

    animTransform.Orthogonalize();
    // TODO: smart clipping
    transform = parentPos * animTransform;
  }
  else
  {
    transform = parentPos * proxy.GetTransform();
  }

  shape = object->GetShape();
  parentObject = object;

  // if shape is reversed, reverse orientation
  if (shape->Remarks() & ShapeReversed)
  {
    Matrix4 swapM(MScale, -1, +1, -1);
    transform = transform * swapM;
  }
  DoAssert(transform.IsFinite());
  return object;
}

bool InventoryItem::PreloadProxies(int level, float dist2, bool preloadTextures) const
{
  bool ret = base::PreloadProxies(level, dist2, preloadTextures);

  // preload all proxy objects
  for (int i=0; i<GetProxyCountForLevel(level); i++)
  {
    Object *object = GetProxyInfo(level, i);
    if (!object) continue;

    LODShape *pshape = object->GetShape();
    const ProxyObjectTyped &proxy = GetProxyForLevel(level, i);

    int pLevel = GScene->PreloadLevelFromDistance2(pshape, dist2, FutureVisualState().Scale() * proxy.obj->FutureVisualState().Scale());
    if (pLevel == LOD_INVISIBLE) continue;
    if (pLevel < 0)
    {
      ret = false;
      continue;
    }
    // preload textures
    ShapeUsed shape = pshape->Level(pLevel);
    if (!shape->PreloadTextures(dist2, NULL)) ret = false;
  }
  return ret;
}

bool InventoryItem::NeedsPrepareProxiesForDrawing() const
{
  return NAttachments() > 0;
}

void InventoryItem::PrepareProxiesForDrawing(
  Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so)
{
  float dist2 = so->distance2;

  ShapeUsed lock = _shape->Level(level);
  for (int i=0; i<GetProxyCountForLevel(level); i++)
  {
    const ProxyObjectTyped &proxy = GetProxyForLevel(level, i);
    SkeletonIndex boneIndex = GetProxyBoneForLevel(level, i);

    EntityAI *parentEntity = so ? dyn_cast<EntityAI>(so->GetParentObject()) : nullptr;
    Matrix4 animTransform = parentEntity ?
      parentEntity->AnimateProxyMatrix(parentEntity->RenderVisualState(), rootLevel, proxy.GetTransform(), boneIndex) :
      AnimateProxyMatrix(RenderVisualState(), level, proxy.GetTransform(), boneIndex);

    Object *object = GetProxyInfo(level, i);
    if (!object) continue;

    // do not show item in hands
    Person *person = dyn_cast<Person>(rootObj);
    if (person && person->GetItemInHands() == object) continue;

    LODShape *pshape = object->GetShape();
    if (!pshape) continue;

    // LOD detection
    // if cockpit is used, use also cockpit for attachments
    int pLevel = -1;
    if (_shape->IsSpecLevel(level, VIEW_PILOT)) pLevel = pshape->FindSpecLevel(VIEW_PILOT);
    if (pLevel < 0) pLevel = GScene->LevelFromDistance2(pshape, dist2, transform.position.Scale());

    // disappering head hotfix (news:i7v7sa$329$1@new-server.localdomain)
    if (pLevel == LOD_INVISIBLE && object->GetFeatureSize() > 0)
    {
      pLevel = pshape->FindSimplestLevel();
    }

    if (pLevel == LOD_INVISIBLE) continue;

    PositionRender pTransform = transform * animTransform;

    // if shape is reversed, reverse orientation
    if (pshape->Remarks() & ShapeReversed)
    {
      Matrix4 swapM(MScale, -1, +1, -1);
      pTransform = pTransform * swapM;
    }

    // note: the itself may be hidden, if it is, skip it

    // check if the corresponding proxy face is hidden
    int section = GetProxySectionForLevel(level, i);
    if (section >= 0)
    {
      int spec = animContext.GetSection(lock, section).Special();
      if (spec & IsHidden) continue;
    }

    // registration for drawing
    GScene->ProxyForDrawing(object, rootObj, pLevel, rootLevel, clipFlags, dist2, pTransform);

    // scan if there are some proxies inside of this proxy
    if (object->NeedsPrepareProxiesForDrawing())
    {
      const EntityType *proxyType = object->GetEntityType();
      if (proxyType && pshape->NLevels() > 0)
      {
        // LOD selection for the proxy of the proxy
        int ppLevel = GScene->LevelFromDistance2(pshape, dist2, pTransform.position.Scale());

        // most likely the Animate is empty, but we call it for consistency
        if (ppLevel != LOD_INVISIBLE)
        {
          ShapeUsed pLock = object->GetShape()->Level(ppLevel);
          AnimationContextStorageGeom storage;
          AnimationContext animContext(object->RenderVisualState(),storage);
          pLock->InitAnimationContext(animContext, pshape->GetConvexComponents(ppLevel), false);
          object->Animate(animContext, ppLevel, true, object, dist2);
          SortObject inner;
          inner.SetObject(object);
          inner.SetParentObject(nullptr);
          inner.SetRootObject(rootObj);
          object->PrepareProxiesForDrawing(rootObj, rootLevel, animContext, pTransform, ppLevel, ClipAll, &inner);
          object->Deanimate(ppLevel,true);
        }
      }
    }
  }

  base::PrepareProxiesForDrawing(rootObj, rootLevel, animContext, transform, level, clipFlags, so);
}

void InventoryItem::DrawProxies(int level, float dist2, const PositionRender &transform, const std::function<bool(Matrix4 &matrix, SkeletonIndex boneIndex)> &func)
{
  ShapeUsed lock = _shape->Level(level);

  // animate the object to check proxies visibility
  AnimationContextStorageGeom storage;
  AnimationContext animContext(RenderVisualState(), storage);
  lock->InitAnimationContext(animContext, _shape->GetConvexComponents(level), false);
  Animate(animContext, level, true, this, dist2);

  for (int i=0; i<GetProxyCountForLevel(level); i++)
  {
    // check if the corresponding proxy face is hidden
    bool hidden = false;
    int section = GetProxySectionForLevel(level, i);
    if (section >= 0)
    {
      int spec = animContext.GetSection(lock, section).Special();
      hidden = (spec & IsHidden) != 0;
    }
    if (hidden) continue;

    const ProxyObjectTyped &proxy = GetProxyForLevel(level, i);

    SkeletonIndex boneIndex = GetProxyBoneForLevel(level, i);
    Matrix4 animTransform = proxy.GetTransform();
    if (!func(animTransform, boneIndex)) animTransform = AnimateProxyMatrix(RenderVisualState(), level, animTransform, boneIndex);

    Object *object = nullptr;

    const EntityType *type = proxy.obj->GetEntityType();
    if (type && strcmp(type->_simName, "alwaysshow") != 0)
    {
      // typed proxy
      object = GetProxyInfo(level, i);
    }
    else
    {
      // show simple proxy always
      object = proxy.obj;
    }
    if (!object) continue;

    LODShape *pshape = object->GetShape();
    if (!pshape) continue;

    // LOD detection
    int pLevel = GScene->LevelFromDistance2(pshape, dist2, transform.position.Scale());

    // disappering head hotfix (news:i7v7sa$329$1@new-server.localdomain)
    if (pLevel == LOD_INVISIBLE && object->GetFeatureSize() > 0)
    {
      pLevel = pshape->FindSimplestLevel();
    }

    if (pLevel == LOD_INVISIBLE) continue;

    PositionRender pTransform = transform * animTransform;

    // if shape is reversed, reverse orientation
    if (pshape->Remarks() & ShapeReversed)
    {
      Matrix4 swapM(MScale, -1, +1, -1);
      pTransform = pTransform * swapM;
    }

    InventoryItem *item = dyn_cast<InventoryItem>(object);

    // we want animate object using this
    Ref<SortObject> oi = new SortObject();
    oi->SetObject(object);
    oi->SetRootObject(this);
    if (GetShape() && pshape->GetSkeletonSource() == GetShape()->GetSkeletonSource()) oi->SetParentObject(this);

    // Set UI specific flags
    if (item) item->InsideUIDraw(true);
    else object->GetShape()->OrSpecial(DisableSun);

    object->Draw(-1, pLevel, ClipAll, DrawParameters::_noShadow, InstanceParameters::_default, dist2, pTransform, oi
#if _HELISIM_RENDER_TO_TEXTURE
      , GScene->GetDrawObjectIndex()
#endif    
      );

    // Restore flags
    if (item) item->InsideUIDraw(false);
    else object->GetShape()->AndSpecial(~DisableSun);

    // for an inventory item, draw attachments recursively
    if (item) item->DrawProxies(pLevel, dist2, pTransform, [](Matrix4 &matrix, SkeletonIndex boneIndex) -> bool
    {
      return false;
    });
  }

  Deanimate(level, true);
}

int InventoryItem::GetProxyCountForLevel(int level) const
{
  return Type()->GetProxyCountForLevel(level);
}

const ProxyObjectTyped &InventoryItem::GetProxyForLevel(int level, int i) const
{
  return Type()->GetProxyForLevel(level, i);
}

SkeletonIndex InventoryItem::GetProxyBoneForLevel(int level, int i) const
{
  return Type()->GetProxyBoneForLevel(level, i);
}

int InventoryItem::GetProxySectionForLevel(int level, int i) const
{
  return Type()->GetProxySectionForLevel(level, i);
}

void InventoryItem::JSONSave(JSONValue &container, JSONAllocator &allocator) const
{
  JSONValue items;
  items.SetArray();
  if (_supply && !_supply->GetItemsCargo().IsEmpty())
  {
    _supply->ForEachItemCargo([&items, &allocator](const InventoryItem *item) -> bool
    {
      JSONValue value;
      value.SetObject();
      value.AddMember("slot", "supply", allocator);
      value.AddMember("type", item->GetName(), allocator);
      item->JSONSave(value, allocator);
      items.PushBack(value, allocator);
      return false; // continue
    }, false /*avoid recursion*/);
  }
  for (int i=0; i<_attachments.Size(); i++)
  {
    const InventoryItem *item = _attachments[i];
    if (item)
    {
      InventorySlotId slot = Type()->_attachments[i]._id;

      JSONValue value;
      value.SetObject();
      value.AddMember("slot", InventorySlotTypes.GetName(slot), allocator);
      value.AddMember("type", item->GetName(), allocator);
      item->JSONSave(value, allocator);
      items.PushBack(value, allocator);
    }
  }
  if (items.Size() > 0) container.AddMember("items", items, allocator);

  JSONSaveVariables(container, allocator);

  float damage = GetTotalDamage();
  if (damage > 0)
  {
    JSONValue newState;
    if (!container.HasMember("state"))
    {
      newState.SetObject();
      container.AddMember("state", newState, allocator);
    }
    JSONValue &state = container["state"];
    if (state.IsObject()) state.AddMember("damage", damage, allocator);
  }
}

bool InventoryItem::JSONLoad(JSONValue &container, Person *person)
{
  JSONValue &items = container["items"];
  if (items.IsArray())
  {
    for (int i=0; i<items.Size(); i++)
    {
      JSONValue &value = items[i];
      RString slot = value["slot"].GetString();
      if (stricmp(slot, "supply") == 0)
      {
        if (_supply)
        {
          RString type = value["type"].GetString();
          Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewNonAIVehicleWithID(type, RString()));
          if (item && item->JSONLoad(value, person))
          {
            int index = _supply->GetItemsCargo().CanAddItem(item);
            if (index >= 0)
            {
              // add to the world
              GWorld->AddOutVehicle(item);
              item->SetMoveOutDone(this);

              _supply->GetItemsCargo().AddItem(this, item, index);

              // DO NOT create network object as it will be created later in CreateSubobjects during CreateVehicle of its hierParent
            }
          }
        }
      }
      else
      {
        InventorySlotId slotId = (InventorySlotId)InventorySlotTypes.GetValue(slot);
        if (slotId >= 0)
        {
          int index = Type()->_attachments.FindKey(slotId);
          if (index >= 0)
          {
            RString type = value["type"].GetString();
            Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewNonAIVehicleWithID(type, RString()));
            if (item && item->JSONLoad(value, person))
            {
              // add to the world
              GWorld->AddOutVehicle(item);
              item->SetMoveOutDone(this);

              _attachments[index] = item;
            }
          }
        }
      }
    }
  }
  JSONLoadVariables(container);

  JSONValue &state = container["state"];
  if (state.IsObject())
  {
    JSONValue &value = state["damage"];
    if (value.IsNumber())
      SetDamage((float)value.GetDouble(), true);
  }

  return true;
}

void InventoryItem::CreateSubobjects()
{
  for (int i=0; i<_attachments.Size(); i++)
  {
    InventoryItem *item = _attachments[i];
    if (item) GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);
  }
  base::CreateSubobjects();
}

void InventoryItem::UpdateSubobjects()
{
  base::UpdateSubobjects();
  for (int i=0; i<_attachments.Size(); i++)
  {
    InventoryItem *item = _attachments[i];
    if (item)
    {
      item->UpdateSubobjects();
      GetNetworkManager().UpdateObject(item);
    }
  }
}

float InventoryItem::FindMaxElevation(Person *owner, Vector3Par pos, bool underWater, float *dx, float *dz)
{
  // find steady position on surface
  // use ground collision test to validate position
  const InventoryItemType *type = Type();
  const Vector3 *bb = type ? type->BoundingBox() : GetShape()->MinMax();

  Vector3 corners[5] =
  {
    pos + Vector3(bb[0].x, 0, bb[0].z),
    pos + Vector3(bb[1].x, 0, bb[0].z),
    pos + Vector3(bb[1].x, 0, bb[1].z),
    pos + Vector3(bb[0].x, 0, bb[1].z),
    pos
  };
  float maxElevation = -FLT_MAX;

  for(int n = 0; n < 5; n++)
  {
    Vector3 localPos = corners[n];
    float elevation = underWater ? GLandscape->RoadSurfaceY(localPos, dx, dz) : GLandscape->RoadSurfaceYAboveWater(localPos, dx, dz);
    Vector3 s = localPos + VUp * 0.1f;
    Vector3 e = localPos - VUp * 3.0f;

    CollisionBuffer col;
    GLandscape->ObjectCollisionLine(GetFutureVisualStateAge(),col,Landscape::FilterIgnoreTwo(this, owner),s,e,0.0f,ObjIntersectFire,ObjIntersectView,CF_NEAREST_CONTACT);

    if( col.Size() > 0)
    {
      elevation = floatMax(elevation, col[0].pos.Y());
    }

    maxElevation = floatMax(elevation, maxElevation);
  }

  return maxElevation;
}

void InventoryItem::PlaceOnSurface(Matrix4 &trans, bool underWater)
{
  PlaceOnSurfaceOwnerTest(nullptr, trans, underWater);
}

void InventoryItem::PlaceOnSurfaceOwnerTest(Person *owner, Matrix4 &trans, bool underWater)
{
  if (!GetShape()) return;

  Vector3 pos = trans.Position();

  float dx, dz;
  pos[1] = FindMaxElevation(owner, pos, underWater, &dx, &dz);

  const InventoryItemType *type = Type();
  int rotationFlags = 0;

  if( type )
  {
    rotationFlags = (type->RotateDropped() ? RF_MASTER : 0) |
      (type->RotateInvert() ? RF_INVERT : 0) |
      (type->RotateZ() ? RF_ZAXIS : 0);
  }

  const Vector3 *bb = type ? type->BoundingBox() : GetShape()->MinMax();
  ItemPlaceOnSurfacePoint(rotationFlags, trans, pos, dx, dz, bb);
}

DEF_RSB(slope)
DEF_RSB(placement)
DEF_RSB(vertical)

void InventoryItem::ItemPlaceOnSurfacePoint(int rotationFlags, Matrix4 &trans, Vector3Val surfPos, float dx, float dz, const Vector3* bb)
{
  const LODShape *shape = GetShape();
  Assert(shape); // checked by caller

  //container that can adjust height
  InventoryItem *container = NULL;
  const InventoryItemType *type = Type();
  if( type )
    container = GetAttachment(type->GetContainerSlot());

  bool rotateMaster = (rotationFlags & RF_MASTER) == RF_MASTER;
  bool rotateInvert = (rotationFlags & RF_INVERT) == RF_INVERT;
  bool rotateZ = (rotationFlags & RF_ZAXIS) == RF_ZAXIS;
  bool rotateRandom = (rotationFlags & RF_RANDOMIZE) == RF_RANDOMIZE;

  // rotate object lo lay on ground
  // not rotated if has a container
  // need to refactor this based on vector in config (cleaner)
  if( (rotateMaster) && !container)
  {
    Matrix3 rot;
    if( rotateInvert )
    {
      rot = Matrix3(MUpAndDirection, VForward, VUp);
    }
    else
    {
      rot = Matrix3(MUpAndDirection, VForward, -VUp);
    }

    Matrix3 orient = trans.Orientation() * rot;
    trans.SetOrientation(orient);
  }

  if( rotateZ )
  {
    Matrix3 rot(MRotationY, H_PI*90/180.0f);
    Matrix3 orient = trans.Orientation() * rot;
    trans.SetOrientation(orient);
  };

  //randomize direction
  if( rotateRandom )
  {
    // TODO: refactor by someone who knows what they are doing
    float aPlus = (float)GRandGen.RandomInt(-180, 180);
    if( rotateMaster && rotateZ )
    {
      Matrix3 rot(MRotationX, H_PI*aPlus / 180.0f);
      Matrix3 orient = trans.Orientation() * rot;
      trans.SetOrientation(orient);
    }
    else if( rotateMaster && !rotateZ )	//items such as axes
    {
      Matrix3 rot(MRotationZ, H_PI*aPlus / 180.0f);
      Matrix3 orient = trans.Orientation() * rot;
      trans.SetOrientation(orient);
    }
    else
    {
      Matrix3 rot(MRotationY, H_PI*aPlus / 180.0f);
      Matrix3 orient = trans.Orientation() * rot;
      trans.SetOrientation(orient);
    }
  }


  // calculate position of lowest point of bounding box
  // planes of object are horizontal (so only two points need to be checked)
  //const Vector3 *bb = boundingBox;
  Vector3 min = trans * bb[0];
  Vector3 max = trans * bb[1];

  //if it has container, use in calculation
  if (container)
  {
    const Vector3 *bb2 = container->Type() ? container->Type()->BoundingBox() : container->GetShape()->MinMax();
    min = trans * (bb[0] + bb2[0]);
    max = trans * (bb[1] + bb2[1]);
  };
  float minY = floatMin(min.Y(), max.Y());

  // how much above surface is this position
  float above = minY - surfPos.Y();
  // move the object to have this position on ground
  Vector3 newPos = trans.Position();
  newPos[1] -= above;
  trans.SetPosition(newPos);

  // if placement slope or vertical is used, matrix should be vertical
  if (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)
    || shape->PropertyValue(RSB(placement))==RSB(slope)
    || shape->PropertyValue(RSB(placement))==RSB(vertical)
    || Type()->_placement==PlacementVertical)
  {
    // ClipLandKeep objects up must point up in world space
    return;
  }

  // change orientation to copy surface
  {
    Matrix3 rot(MUpAndDirection, Vector3(-dx, 1, -dz), VForward);
    Matrix3 orient = rot * trans.Orientation();
    trans.SetOrientation(orient);
  }
}

void InventoryItem::Simulate(float deltaT, SimulationImportance prec)
{
  bool removeAttachment = false;
  if (!GetHierarchyParent()) // handled by the root of the hierarchy otherwise
  { // do the same as UpdateJoinedObjects for non InventoryItems
    if (ForEachAttachment([](const InventoryItem *item) -> bool
        {
          const InventoryItemType *type = item->Type();
          return type && type->_lights.SomeLights(); // break when some light found
        })
    )
    { // so it HasReflectorAttached
      if (!_isWorldAttached)
      {
        GWorld->AddAttachment(this);
        _isWorldAttached = true;
      }
    }
    else removeAttachment = _isWorldAttached;
  }
  else removeAttachment = _isWorldAttached;
  if (removeAttachment)
  {
    GWorld->RemoveAttachment(this);
    _isWorldAttached = false;
  }

  // temperature
  if (GetNetworkManager().IsServerOwner())
  {
    const float T0 = GetTemperature(); // initial temperature
    const float T1 = 0.0f;             // ambient temperature
    const float diffT = T0 - T1;

    if (fabs(diffT) > FLT_EPSILON)
    {
      const float dt = deltaT;         // infinitezimal time
      const float k = 5e-3f;           // cst dependent on material, surface, etc.. @TODO: to some config?
      const float dT = -k * diffT * dt;
      const float new_temp = T0 + dT;
      const float temperature = new_temp;
      //TRACE_PLOT_XY(trace::e_Info, trace::CTX_Default, Glob.time.toFloat(), temperature, "temp/it_%s", GetDebugName().Data());
      SetTemperature(temperature);
    }
  };

  base::Simulate(deltaT, prec);
}

Vector3 InventoryItem::AnimatePoint( const ObjectVisualState &vs, int level, int index ) const
{
  Entity *parent = GetHierarchyParent();
  if (!parent) return base::AnimatePoint(vs, level, index);

  ShapeUsed shape = _shape->Level(level);
  Vector3Val modelPos = shape->Pos(index);

  if (parent->GetShape() && parent->GetShape()->GetSkeletonSource() == _shape->GetSkeletonSource())
  {
    if(index < shape->GetVertexBoneRef().Size())
    {
      // the point can be animated by the parent's skeleton
      const AnimationRTWeight &wg = shape->GetVertexBoneRef()[index];
      if (wg.Size() > 0)
      {
        // TODO: bone blending
        Assert(wg.Size()==1);
        SubSkeletonIndex boneIndex = wg.Get(0).GetSel();
        SkeletonIndex bone = shape->GetSkeletonIndexFromSubSkeletonIndex(boneIndex);
        Matrix4 mat = MIdentity;
        const ObjectVisualState &parentVS = parent->RenderVisualState();
        parent->AnimateBoneMatrix(mat, parentVS, parent->GetShape()->FindMemoryLevel(), bone);
        return /*parentVS.Transform() * */ mat.FastTransform(modelPos);
      }
    }
  }

  // animate proxy in the parent
  return parent->AnimateProxyPoint(this, modelPos);
}

void InventoryItem::UpdatePosition()
{
  if (GetHierarchyParent()) return; // handled by the root of the hierarchy

  UpdateLights(RenderVisualState().Transform());
  //base::UpdatePosition();
}


void InventoryItem::UpdateLights(const PositionRender &parentTransform)
{
  // called from the parent in the hierarchy
  const InventoryItemType *type = Type();
  UpdateLights(parentTransform, _lights, type->_lights);
}

void InventoryItem::UpdateLights(const PositionRender &parentTransform, ObjectLights &lights, const ObjectTypeLights &lightsType)
{
  // animate position
  lights.AnimatePosition(this, lightsType);

  // switch on / off
  bool const on = IsLightOn(FutureVisualState());
  lights.Switch(on, this, lightsType);

  // Update the lights after changes
  lights.UpdateAggLights(this);

  // transform position
  lights.TransformPosition(parentTransform);

  // recursively browse though attachments
  int level = _shape->FindMemoryLevel();
  if (level >= 0)
  {
    for (int i=0; i<GetProxyCountForLevel(level); i++)
    {
      const ProxyObjectTyped &proxy = GetProxyForLevel(level, i);

      // check if proxy is attached to some inventory slot
      Object *proxyObj = proxy.obj;
      if (!proxyObj) continue;
      const EntityType *proxyType = proxyObj->GetEntityType();
      if (!proxyType) continue;
      InventorySlotId slotId = proxyType->GetInventorySlot();
      if (slotId < 0) continue;

      InventoryItem *item = FindAttachment(slotId);

      if (item)
      {
        SkeletonIndex boneIndex = GetProxyBoneForLevel(level, i);
        Matrix4 animTransform = AnimateProxyMatrix(RenderVisualState(), level, proxy.GetTransform(), boneIndex);
        // if shape is reversed, reverse orientation
        if (_shape->Remarks() & ShapeReversed)
        {
          Matrix4 swapM(MScale, -1, +1, -1);
          animTransform = animTransform * swapM;
        }
        PositionRender transform = parentTransform * animTransform;
        item->UpdateLights(transform);
      }
    }
  }
}

void InventoryItem::SetTexture(const RStringB &name)
{
  if (_forcedTexture && _forcedTexture->GetName() == name) return; // already set to this color
  _forcedTexture = GlobLoadTexture(name);
}

void InventoryItem::ResetTexture()
{
  _forcedTexture = nullptr;
}

void InventoryItem::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  base::Animate(animContext, level, setEngineStuff, parentObject, dist2);

  if (!_shape) return;
  ShapeUsed shape = _shape->Level(level);
  if (shape.IsNull()) return;

  if (_forcedTexture)
  {
    for (int i=0; i<shape->NSections(); i++)
    {
      PolyProperties &ss = animContext.SetSection(shape, i);
      ss.SetTexture(_forcedTexture);
    }
  }
}

LSError InventoryItem::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar));
  CHECK(ar.SerializeRefs("Attachments", _attachments, 1));
  CHECK(ar.Serialize("inventoryRotated", _inventoryRotated, 1, false));
  return LSOK;
}

#define INVENTORY_ITEM_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateInventoryItem)

DEFINE_NETWORK_OBJECT(InventoryItem, base, INVENTORY_ITEM_MSG_LIST)

DEFINE_NET_INDICES_EX_ERR(UpdateInventoryItem, UpdateVehicleAI, UPDATE_INVENTORY_ITEM_MSG, NoErrorInitialFunc)

NetworkMessageFormat &InventoryItem::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
  case NMCUpdateDamage:
    base::CreateFormat(cls, format);
    break;
  case NMCUpdateGeneric:
    base::CreateFormat(cls, format);
    UPDATE_INVENTORY_ITEM_MSG(UpdateInventoryItem, MSG_FORMAT_ERR)
    break;
  default:
    base::CreateFormat(cls, format);
    break;
  }
  return format;
}

TMError InventoryItem::TransferMsg(NetworkMessageContext &ctx)
{
  // patch the presence of item in the landscape
  if (!ctx.IsSending() && ctx.GetClass() == NMCUpdateGeneric)
  {
    PREPARE_TRANSFER(UpdateInventoryItem)
    bool inLandscape = false;
    TRANSF_EX(inLandscape, inLandscape);
    if (inLandscape) ItemMoveIn();
  }

  TMCHECK(base::TransferMsg(ctx))

  if (ctx.GetClass() == NMCUpdateGeneric)
  {
    PREPARE_TRANSFER(UpdateInventoryItem)

    bool inventoryRotated = _inventoryRotated;
    TRANSF_EX(inventoryRotated, inventoryRotated);
    if (!ctx.IsSending()) _inventoryRotated = inventoryRotated;

    if (ctx.IsSending())
    {
      TRANSF_REFS(attachments)
      bool inLandscape = IsInLandscape();
      TRANSF_EX(inLandscape, inLandscape);
      bool isOn = IsOn();
      TRANSF_EX(isOn, isOn);
    }
    else
    {
      bool isOn = false;
      TRANSF_EX(isOn, isOn);
      SwitchOn(isOn); //possibly takes other action (such as pilotLights)

      // unassign old items (if assigned)
      for (int i=0; i<_attachments.Size(); i++)
      {
        InventoryItem *item = _attachments[i];
        if (item) item->CancelHierarchyParent(this);
      }
      // transfer items
      TRANSF_REFS(attachments)
      // assign new items
      for (int i=0; i<_attachments.Size(); i++)
      {
        InventoryItem *item = _attachments[i];
        if (item) item->ItemMoveOut(this);
      }
    }
  }

  return TMOK;
}

float InventoryItem::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = base::CalculateError(ctx);
  if (ctx.GetClass() == NMCUpdateGeneric)
  {
    PREPARE_TRANSFER(UpdateInventoryItem)

    if (message->_inLandscape != IsInLandscape()) error += ERR_COEF_IMMEDIATE;
    if (message->_inventoryRotated != _inventoryRotated) error += ERR_COEF_IMMEDIATE;

    // items
    int count = intMin(message->_attachments.Size(), _attachments.Size());
    int dif = (message->_attachments.Size() - count) + (_attachments.Size() - count);
    for (int i=0; i<count; i++)
    {
      if (_attachments[i])
      {
        if (message->_attachments[i] != _attachments[i]->GetNetworkId()) dif++;
      }
      else
      {
        if (!message->_attachments[i].IsNull()) dif++;
      }
    }
    error += ERR_COEF_MODE * dif;

    if ( message->_isOn!=IsOn() ) error += ERR_COEF_STRUCTURE;
  }
  return error;
}

Vector3 InventoryItem::GetCurrentPosition() const
{
  // if item is stored in some container, inherit position of the container
  Entity *parent = GetHierarchyParent();
  if (parent) return parent->GetCurrentPosition();
  return base::GetCurrentPosition();
}

float InventoryItem::UpdateDistance() const
{
  // free object (like loot in the world)
  // item in a container is handled with its parent
  return 200;
}

DEFINE_CASTING(InventoryItem)

DEFINE_ENUM(WeaponSimulation, WS, WEAPON_SIMULATION_ENUM)

bool InventoryStorage::ForEachItem(const std::function<bool(InventoryItem *item)> &func, bool recursive)
{
  for (int i=0; i<_items.Size(); i++)
  {
    InventoryItem *item = _items[i];
    if (!item) continue;
    if (func(item) || recursive && item->ForEachItem(func)) return true;
  }
  return false;
}

bool InventoryStorage::ForEachItem(const std::function<bool(const InventoryItem *item)> &func, bool recursive) const
{
  for (int i=0; i<_items.Size(); i++)
  {
    const InventoryItem *item = _items[i];
    if (!item) continue;
    if (func(item) || recursive && item->ForEachItem(func)) return true;
  }
  return false;
}

bool InventoryStorage::ForEachItemReverse(const std::function<bool(InventoryItem *item)> &func)
{
  for (int i=_items.Size()-1; i>=0; i--)
  {
    InventoryItem *item = _items[i];
    if (!item) continue;
    if (func(item)) return true;
  }
  return false;
}

bool InventoryStorage::ForEachItemReverse(const std::function<bool(const InventoryItem *item)> &func) const
{
  for (int i=_items.Size()-1; i>=0; i--)
  {
    const InventoryItem *item = _items[i];
    if (!item) continue;
    if (func(item)) return true;
  }
  return false;
}

void InventoryStorage::Add(InventoryItem *item, int index)
{
  if (index >= _items.Size()) _items.Resize(index + 1);
  Assert(_items[index] == NULL);
  _items[index] = item;
}

InventoryItem *InventoryStorage::CreateItem(EntityAI *owner, RString typeName)
{
  Ref<InventoryItem> item = dyn_cast<InventoryItem>(GWorld->NewVehicleWithID(typeName));
  if (item) // check if fits
  {
    int index = CanAddItem(item, -1);
    if (index >= 0)
    {
      item->Init(owner ? owner->FutureVisualState().Transform() : M4Identity, true); // invoke Init prior AddAttachment (_lights initialization)

      // invoke "init" event handler
      item->OnEvent(EEInit);

      Add(item, index);

      // add to the world
      GWorld->AddOutVehicle(item);
      item->SetMoveOutDone(owner);

      // create network object
      if (GWorld->GetMode() == GModeNetware)
        GetNetworkManager().CreateVehicle(item, VLTOut, "", -1);

      return item;
    }
  }

  return NULL;
}

bool InventoryStorage::MoveItem(EntityAI *owner, InventoryItem *item)
{
  Assert(item);
  // check if fits
  int index = CanAddItem(item, -1);
  if (index < 0) return false;

  // remove from the old owner
  Entity *parent = item->GetHierarchyParent();
  if (parent) DoVerify(parent->RemoveOwnedItem(item));

  // move in world
  item->ItemMoveOut(owner);

  // add to the new owner
  Add(item, index);
  return true;
}

int InventoryStorage::ItemOn(int row, int col) const
{
  if (row < 0 || row >= _height) return -1;
  if (col < 0 || col >= _width) return -1;

  // only need to check items in rectangle [0, 0] .. [row, col]
  int i = 0;
  for (int r=0; r<=row; r++)
  {
    for (int c=0; c<=col; c++)
    {
      if (i >= _items.Size()) return -1; // no more items

      const InventoryItem *item = _items[i++];
      if (!item) continue;
      const InventoryItemType *type = item->Type();
      if (!type) continue;

      int w = item->GetInventoryWidth();
      int h = item->GetInventoryHeight();
      DoAssert(c + w <= _width);
      DoAssert(r + h <= _height);
      if (c + w > col && r + h > row) return i - 1; // i was already incremented
    }
    i += _width - (col + 1);
  }

  return -1;
}

void InventoryStorage::Render(StaticArrayAuto<bool> &image, const InventoryItem *exclude) const
{
  image.ResizeWithInit(_height * _width, false);

  int i = 0;
  for (int row=0; row<_height; row++)
  {
    for (int col=0; col<_width; col++)
    {
      if (i >= _items.Size()) return; // no more items

      const InventoryItem *item = _items[i++];
      if (!item || item == exclude) continue;
      const InventoryItemType *type = item->Type();
      if (!type) continue;

      int w = item->GetInventoryWidth();
      int h = item->GetInventoryHeight();
      DoAssert(col + w <= _width);
      DoAssert(row + h <= _height);
      // render the item
      int j = i - 1; // i was already incremented
      for (int r=row; r<row+h; r++)
      {
        for (int c=col; c<col+w; c++)
        {
          DoAssert(!image[j]);
          image[j] = true;
          j++;
        }
        j += _width - w; // skip the rest of row
      }
    }
  }
}

int InventoryStorage::FitsInto(const Array<bool> &image, const InventoryItemType *type, int index, bool rotated) const
{
  int h = rotated ? type->GetItemWidth() : type->GetItemHeight();
  int w = rotated ? type->GetItemHeight() : type->GetItemWidth();
  if (index >= 0)
  {
    // check the given space
    int row = index / _width;
    int col = index - row * _width;
    if (row + h > _height) return -1;
    if (col + w > _width) return -1;
    int j = index;
    for (int r=row; r<row+h; r++)
    {
      for (int c=col; c<col+w; c++)
      {
        if (image[j]) return -1;
        j++;
      }
      j += _width - w; // skip the rest of row
    }
    return index;
  }
  else
  {
    // find the free space
    int i = 0;
    for (int row=0; row<=_height-h; row++)
    {
      for (int col=0; col<=_width-w; col++)
      {
        // check the space for [row, col]
        bool found = false;
        int j = i;
        for (int r=row; r<row+h; r++)
        {
          for (int c=col; c<col+w; c++)
          {
            if (image[j])
            {
              found = true;
              break;
            }
            j++;
          }
          if (found) break;
          j += _width - w; // skip the rest of row
        }
        if (!found)
        {
          // this one is free
          return i;
        }
        i++;
      }
      i += w - 1; // skip the rest of row
    }
    return -1; // not found
  }
}

int InventoryStorage::CanAddItem(const InventoryItem *item, int index) const
{
  if (_width <= 0 || _height <= 0) return -1;
  const InventoryItemType *type = item->Type();
  if (!type) return -1;

  // "render" the space occupied by the current items
  AUTO_STATIC_ARRAY(bool, image, 256);
  Render(image, item);

  return FitsInto(image, type, index, item->IsInventoryRotated());
}

bool InventoryStorage::CanRotateItem(const InventoryItem *item) const
{
  if (!item || !item->Type() || _width <= 0 || _height <= 0) return false;
  if (item->GetInventoryWidth() == item->GetInventoryHeight()) return false;
  int index = _items.Find(item);
  if (index < 0) return false;

  AUTO_STATIC_ARRAY(bool, image, 256);
  Render(image, item);
  return FitsInto(image, item->Type(), index, !item->IsInventoryRotated()) == index;
}

int InventoryStorage::CanReplaceItem(const InventoryItem *oldItem, const InventoryItem *newItem) const
{
  const InventoryItemType *type = newItem->Type();
  if (!type) return -1;

  // find the item to replace
  int index = _items.Find(oldItem);
  if (index < 0) return -1; // not found

  AUTO_STATIC_ARRAY(bool, image, 256);
  Render(image, oldItem);

  // check if the newItem fits on the same place
  if (FitsInto(image, type, index, newItem->IsInventoryRotated()) == index) return index;

  // try to find different place in the container
  return FitsInto(image, type, -1, newItem->IsInventoryRotated());
}

bool InventoryStorage::AddItem(EntityAI *owner, InventoryItem *item, int index)
{
  // check if fits
  index = CanAddItem(item, index);
  if (index < 0) return false;

  // move in world
  item->ItemMoveOut(owner);
  item->SwitchOn(false);
  item->SwitchOff();
  Add(item, index);
  return true;
}

bool InventoryStorage::RemoveItem(EntityAI *owner, InventoryItem *item)
{
  // find and remove item
  int index = _items.Find(item);
  if (index < 0) return false; // not found
  // keep tha layout
  _items[index] = NULL;
  // shorten the array if possible
  if (index == _items.Size() - 1)
  {
    while (index >= 0 && _items[index] == NULL) index--;
    _items.Resize(index + 1);
  }

  // unassign the item, but leave it in the _outVehicles
  // do not handle it if already handled (for example new owner set)
  item->CancelHierarchyParent(owner);
  return true;
}

void InventoryStorage::DeleteItems(const std::function<bool(const InventoryItem *item)> &func)
{
  for (int i=_items.Size()-1; i>=0; i--) // go reverse to avoid problems with deletion
  {
    InventoryItem *item = _items[i];
    if (!item) continue;

    if (func(item))
    {
      // remove from the world
      item->ItemDestroy();
      // and the container
      _items.DeleteAt(i);
    }
  }
}

bool InventoryStorage::DeleteFirstItem(const std::function<bool(const InventoryItem *item)> &func)
{
  for (int i=_items.Size()-1; i>=0; i--)
  {
    InventoryItem *item = _items[i];
    if (!item) continue;

    if (func(item))
    {
      // remove from the world
      item->ItemDestroy();
      // and the container
      _items.DeleteAt(i);
      return true; // deleted
    }
  }
  return false; // nothing deleted
}


void ItemWatchType::Load(ParamEntryPar par, const char *shape)
{
  base::Load(par, shape);
}

Object* ItemWatchType::CreateObject(bool fullCreate) const
{
  return new ItemWatch(this, fullCreate);
}

void ItemWatchType::LODShapeLoaded(LODShape *shape)
{
  base::LODShapeLoaded(shape);

  const ParamEntry &par = *_par;
  _date1.Init(_shape, (par >> "selectionDate1").operator RString());
  _date2.Init(_shape, (par >> "selectionDate2").operator RString());
  _day.Init(_shape, (par >> "selectionDay").operator RString());
}

void ItemWatchType::LODShapeUnloaded(LODShape *shape)
{
  base::LODShapeUnloaded(shape);
}

void ItemWatchType::ShapeLoaded(LODShape *shape, int level)
{
  base::ShapeLoaded(shape, level);

  _date1.InitLevel(_shape,level);
  _date2.InitLevel(_shape,level);
  _day.InitLevel(_shape,level);
}

void ItemWatchType::ShapeUnloaded(LODShape *shape, int level)
{
  base::ShapeUnloaded(shape, level);

  _date1.DeinitLevel(_shape,level);
  _date2.DeinitLevel(_shape,level);
  _day.DeinitLevel(_shape,level);
}

bool ItemWatch::UseFromInventory(Person *owner)
{
  // UI based action have to be executed on client
  if (GetNetworkManager().IsServer() && !owner->IsLocal())
    GetNetworkManager().UseItem(owner, this);
  else
  {
    if (GWorld) GWorld->ShowWatch(this);
  }
  return true;
}

int GetDaysInMonth(int year, int month);

bool ItemWatch::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
  if (!_animationsEnabled) return false;

  return base::PrepareShapeDrawMatrices(matrices, vs, shape, level);
}

void ItemWatch::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  base::Animate(animContext, level, setEngineStuff, parentObject, dist2);

  if (!_animationsEnabled) return;

  const ItemWatchType *type = Type();
  if (!type) return;

  if (setEngineStuff)
  {
    int year = Glob.clockWeather.GetYear();
    int dayOfYear = toIntFloor(Glob.clockWeather.GetTimeInYear() * 365);
    float timeOfDay = Glob.clockWeather.GetTimeOfDay();
    if (timeOfDay >= 1.0)
    {
      timeOfDay--;
      dayOfYear++;
    }
    Assert(timeOfDay >= 0 && timeOfDay < 1.0);
    Assert(dayOfYear >= 0 && dayOfYear < 365);
    struct tm tmDate = { 0, 0, 0, 0, 0, 0 };
    tmDate.tm_year = year - 1900;
    tmDate.tm_yday = dayOfYear;
    int m = 0;
    while (tmDate.tm_yday >= GetDaysInMonth(year, m))
    {
      tmDate.tm_yday -= GetDaysInMonth(year, m);
      m++;
    }
    tmDate.tm_mday = tmDate.tm_yday + 1;
    tmDate.tm_mon = m;
    mktime(&tmDate);
    Assert(tmDate.tm_yday == dayOfYear);


    int day = tmDate.tm_wday - 1;
    if (day < 0) day = 6;
    type->_day.UVOffset(animContext, _shape, 0, 0.1 * day, level);

    day = tmDate.tm_mday / 10 - 1;
    if (day < 0) day = 9;
    type->_date1.UVOffset(animContext, _shape, 0, 0.1 * day, level);

    day = tmDate.tm_mday % 10 - 1;
    if (day < 0) day = 9;
    type->_date2.UVOffset(animContext, _shape, 0, 0.1 * day, level);
  }
}

void ItemWatch::Deanimate(int level, bool setEngineStuff)
{
}

void ItemCompassType::Load(ParamEntryPar par, const char *shape)
{
  base::Load(par, shape);
}

AnimationSource *ItemCompassType::CreateAnimationSource(const AnimationType *type, RStringB source)
{
  if (!strcmpi(source, "compassPointer")) return _animSources.CreateAnimationSource(&ItemCompass::GetPointerPos);
  if (!strcmpi(source, "compassArrow")) return _animSources.CreateAnimationSource(&ItemCompass::GetArrowPos);
  if (!strcmpi(source, "compassCover")) return _animSources.CreateAnimationSource(&ItemCompass::GetCoverPos);

  return base::CreateAnimationSource(type, source);
}

Object* ItemCompassType::CreateObject(bool fullCreate) const
{
  return new ItemCompass(this, fullCreate);
}

bool ItemCompass::UseFromInventory(Person *owner)
{
  // UI based action have to be executed on client
  /*
  if (GetNetworkManager().IsServer() && !owner->IsLocal())
    GetNetworkManager().UseItem(owner, this);
  else
  {
    if (GWorld) GWorld->ShowCompass(this);
  }
  */
  if (IsEventHandler(EEUsed))
  {
    // can be replaced by OnEventRetBool if needed
    OnEvent(EEUsed, owner);
    return true;
  }

  if (Type() && Type()->IsHandheld())
  {
    const InventoryItem *inHands = owner->GetItemInHands();
    if (inHands && inHands->Type() == Type())
    {
      // remove the item
      owner->TakeItemToHands(NULL);
    }
    else
    {
      // take the item
      owner->TakeItemToHands(this);
    }
    return true;
  }

  return true;
}


bool ItemCompass::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
  if (!_animationsEnabled) return false;

  return base::PrepareShapeDrawMatrices(matrices, vs, shape, level);
}

float ItemCompass::GetPointerPos(const ObjectVisualState &vs) const
{
  Vector3 dir;
  if (!GetHierarchyParent())
  {
    // laying on ground
    dir = RenderVisualState().DirectionUp();
  }
  else
  {
    // find the root of the hierarchy
    const Entity *root = this;

    while (root->GetHierarchyParent()) root = root->GetHierarchyParent();

    const EntityAI *veh = dyn_cast<const EntityAI>(root);
    if (veh)
    {
      Person *player = GWorld->PlayerOn();

      TurretContext context;
      context._turret = NULL;
      context._turretType = NULL;
      context._gunner = player;
      context._weapons = NULL;

      dir = veh->GetEyeDirection(veh->RenderVisualState(), context);
    }
    else dir = veh->RenderVisualState().Direction();
  }

  float angle = atan2(dir.X(), dir.Z());

#if _VBS2
  if (_trackNorth)
  {
    Vector3 ndir = Vector3(VRotate,GetInvTransform(),VUp);
    angle = atan2(-ndir.X(), ndir.Z());
  }
  //smoothing compass to look more like a magnetic device
  float diff = AngleDifference(_pointerAngle, angle);
  float maxDiff = 2.0 * (Glob.uiTime - _lastPointerUpdateTime);
  saturate(diff, -maxDiff, maxDiff);
  _lastPointerUpdateTime = Glob.uiTime;
  _pointerAngle = AngleDifference(_pointerAngle, diff);
  angle = _pointerAngle;
#endif

  return angle;
}


bool ItemCompass::CanAutoShortcut() const
{
  if (Type() && Type()->IsAutoQuickbar())
  {
    return true;
  }
  return false;
  //return IsEventHandler(EEUsed);
}

void ItemGPSType::Load(ParamEntryPar par, const char *shape)
{
  base::Load(par, shape);
}

Object* ItemGPSType::CreateObject(bool fullCreate) const
{
  return new ItemGPS(this, fullCreate);
}

void ItemBookType::Load(ParamEntryPar par, const char *shape)
{
  base::Load(par, shape);
  _title = par >> "title";
  _author = par >> "author";
  RString file = par >> "file";
  _file = file.IsEmpty() ? RString() : GetDefaultName(file, ".html");
}

Object* ItemBookType::CreateObject(bool fullCreate) const
{
  return new ItemBook(this, fullCreate);
}

bool ItemBook::UseFromInventory(Person *owner)
{
  // UI based action have to be executed on client
  if (GetNetworkManager().IsServer() && !owner->IsLocal())
    GetNetworkManager().UseItem(owner, this);
  else
  {
    const ItemBookType *type = Type();
    if (!type) return false;
    if (GWorld)
    {
      AbstractOptionsUI *CreateBookViewer(const ItemBookType *type);
      AbstractOptionsUI *dlg = CreateBookViewer(type);
      if (dlg) GWorld->SetUserDialog(dlg);
    }
  }
  return true;
}



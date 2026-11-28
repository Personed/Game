#pragma once
#include <lib/vehicleAI.hpp>
//#include "../json.hpp"
//#include "../speaker.hpp"
//#include "../UI/chat.hpp"
#include "powereditem.hpp"

#if _ENABLE_HAND_IK
//# include "../rtAnimation.hpp"
#endif

struct InventorySlotInfo
{
  InventorySlotId _id;
  RStringB _displayName;
  Ref<Texture> _icon;
};
TypeIsMovableZeroed(InventorySlotInfo)

template <>
struct FindArrayKeyTraits<InventorySlotInfo> 
{
  typedef InventorySlotInfo Type;
  typedef InventorySlotId KeyType;
  static bool IsEqual(KeyType a, KeyType b)
  {
    return a == b;
  }
  static KeyType GetKey(const Type &a) {return a._id;}
};

// Base classes for inventory items

class InventoryItemType: public PoweredItemType
{
public:
  static void ComputeBoundingBox(const LODShape *shape, const Shape *memory, Vector3* boundingBox);

private:
  typedef PoweredItemType base;
  friend class InventoryItem;

  // the inventory slot item fits in (-1 if none)
  InventorySlotId _inventorySlot;

  FindArrayKey<InventorySlotInfo> _attachments;

  RStringB _attachmentCondition;
  RStringB _conditionDetach;
  RStringB _conditionDispersion;
  float _dispersionModifier;
  RStringB _conditionDexterity;
  float _dexterityModifier;
  RStringB _conditionInventory;
  RStringB _tooltip;
  RStringB _quantity;
#if USE_PRECOMPILATION
  CompiledExpression _attachmentConditionExp;
  CompiledExpression _conditionDetachExp;
  CompiledExpression _conditionDispersionExp;
  CompiledExpression _conditionDexterityExp;
  CompiledExpression _conditionInventoryExp;
  CompiledExpression _tooltipExp;
  CompiledExpression _quantityExp;
#endif

  // default attachments shown in the UI
  AutoArray<RStringB> _baseAttachments;

  // item can be hold in hands
  bool _handheld;
  bool _autoQuickbar;
  bool _openable;

  bool _rotateDropped;
  bool _rotateInvert;
  bool _rotateZ;
  int _stackedMax;
  float _spawnDamageMax;

  bool _melee;

  RString _containerSlot;
  RStringB _animClass;

  // size of item in the inventory UI
  int _itemWidth;
  int _itemHeight;

  // bounding box given by hints in memory lod or from LODShape::MinMax
  Vector3 _boundingBox[2];

  // orientation of the object in the inventory screen
  Matrix3 _inventoryOrientation;

  // animation action to process when item is taken to hands
  RString _armAction;

  // restrict cargo items to items of this class
  RString _cargoParent;	

public:
  InventoryItemType(ParamEntryPar param);
  virtual void Load(ParamEntryPar par, const char *shape) override;

  virtual void InitShape() override;
  virtual void DeinitShape() override;

  virtual bool AbstractOnly() const override {return false;}

  // Store inventory items in GWorld::_slowVehicles always
  virtual bool IsSlow() const override {return true;}
  virtual bool IsSimulationRequired() const override {return false;}
  virtual bool IsSoundRequired() const override {return false;}

  inline InventorySlotId GetInventorySlot() const {return _inventorySlot;}
  inline bool IsHandheld() const {return _handheld;}
  inline bool IsAutoQuickbar() const {return _autoQuickbar;}
  inline bool IsOpenable() const {return _openable;}
  inline bool IsMelee() const { return _melee; }
  inline int GetItemWidth() const {return _itemWidth;}
  inline int GetItemHeight() const {return _itemHeight;}
  inline float GetDispersionModifier() const {return _dispersionModifier;}
  inline float GetDexterityModifier() const {return _dexterityModifier;}

  inline RString GetArmAction() const {return _armAction;}
  inline RString GetContainerSlot() const {return _containerSlot;}
  inline RString GetCargoParent() const {return _cargoParent;}
  inline int GetStackedMax() const {return _stackedMax;}
  inline int GetSpawnDamageMax() const {return _spawnDamageMax;}
  inline RStringB AnimClass() const { return _animClass; }

  inline const FindArrayKey<InventorySlotInfo> &GetAttachments() const {return _attachments;}
  virtual Texture *GetAttachmentIcon(int i) const;

  const Vector3 *BoundingBox() const {return _boundingBox;}
  bool RotateDropped() const {return _rotateDropped;}
  bool RotateInvert() const {return _rotateInvert;}
  bool RotateZ() const {return _rotateZ;}

  Matrix3Val GetInventoryOrientation() const {return _inventoryOrientation;}

  virtual Object* CreateObject(bool fullCreate) const override;

  // create a temporary object used in UI
  InventoryItem *CreateUIObject() const;

#if _ENABLE_HAND_IK
protected:
  /// soldier anim with weapon
  RefArray<AnimationRT> _handAnim;
public:
  Ref<AnimationRT> GetHandAnim(Skeleton * sk) const;  
#endif
};

TypeIsSimple(InventorySlotId)

#define UPDATE_INVENTORY_ITEM_MSG(MessageName, XX) \
  XX(MessageName, bool, isOn, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Device switched on"),  TRANSF, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
  XX(MessageName, RefArray<InventoryItem>, attachments, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Attachments"), TRANSF_REFS, ET_NOT_EQUAL_COUNT, ERR_COEF_MODE) \
  XX(MessageName, bool, inventoryRotated, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Item rotated in inventory grid"), TRANSF, ET_NOT_EQUAL, ERR_COEF_IMMEDIATE) \
  XX(MessageName, bool, inLandscape, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Item is in landscape"), TRANSF, ET_NOT_EQUAL, ERR_COEF_IMMEDIATE) \
  
DECLARE_NET_INDICES_EX_ERR(UpdateInventoryItem, UpdateVehicleAI, UPDATE_INVENTORY_ITEM_MSG)

class ItemOptics;

// base class for all objects storable in inventory
class InventoryItem : public PoweredItem
{
private:
  typedef PoweredItem base;

  // during drawing in UI, different special flags are used
  bool _ui;

  // Item footprint is rotated 90 degrees in a cargo grid.
  bool _inventoryRotated;

  // content of the container is accessible from the Inventory UI
  bool _opened;

  Ref<Texture> _forcedTexture;

protected:
  bool _animationsEnabled;

  RefArray<InventoryItem> _attachments;

  bool _isWorldAttached;  //whether item is inside World::_attached (needed to UpdateLights)

  void OnAttachedDetachedEvent(EntityEvent event, InventoryItem *item, const RStringB& slot);
    
public:
  enum RotationFlags 
  { 
    RF_MASTER = 0x01,
    RF_INVERT = 0x02,
    RF_ZAXIS = 0x04,
    RF_RANDOMIZE = 0x08
  };

  InventoryItem(const EntityAIType *type, bool fullCreate);
  ~InventoryItem();

  const InventoryItemType *Type() const
  {
    return static_cast<const InventoryItemType *>(GetType());
  }

  virtual float UpdateDistance() const override;

  //! Items can be dropped from an inventory, so keep a valid position update
  //! even when the item is not in the landscape on this client yet.
  virtual bool AcceptPositionOutsideLandscape() const override {return true;}

  virtual bool QIsManual() const override {return false;}
  virtual AnimationStyle IsAnimated(int level) const override;


  void InsideUIDraw(bool ui) {_ui = ui;}
  virtual int GetObjSpecial(int shapeSpecial) const override;

  bool IsOpened() const {return _opened;}
  void Open(bool open) {_opened = open;}

  //! Get parameters of action as structured texts
  virtual bool GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const override;
  //! Perform action
  virtual void PerformAction(const Action *action, AIBrain *unit) override;
  //! Get list of available actions
  virtual void GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit) override;

  /// Tells if shortcut should be automaticaly created when item is added
  virtual bool CanAutoShortcut() const;
  virtual bool CanOpenItem() const;

  //temp
  virtual float GetTemperature() const;
  virtual void SetTemperature(float temp);

  //! Perform default action for item in the inventory
  virtual bool UseFromInventory(Person *owner);

  bool IsInventoryRotated() const {return _inventoryRotated;}
  void SetInventoryRotated(bool rotated) {_inventoryRotated = rotated;}
  int GetInventoryWidth() const;
  int GetInventoryHeight() const;
  bool CanRotateInInventory() const;
  bool RotateInInventory();

  // cast optimization
  virtual Weapon *GetWeapon() {return NULL;}
  virtual const Weapon *GetWeapon() const {return NULL;}
  virtual Magazine *GetMagazine() {return NULL;}
  virtual const Magazine *GetMagazine() const {return NULL;}
  virtual ItemOptics *GetOptics() {return NULL;}
  virtual const ItemOptics *GetOptics() const {return NULL;}

  // oportunity for clothing to change shape when worn
  virtual void UpdateShape(Person *owner) {}
  // set different shape based on clothing type, used in UI drawing only
  virtual int SetClothingType(int value) {return -1;}

  // hi-level handling of existence in the World (_moveOutState, _list) and on Network
  // ownership in containers and position is not handled by these functions
  // make item present in _slowVehicles
  void ItemMoveIn();
  // make item present in _outVehicles
  void ItemMoveOut(Entity *parent);
  // delete item from the world
  void ItemDestroy();

  // lay item on ground, the item need to be removed from container already
  void ItemDrop(Person *owner, Vector3Par pos, Vector3Par dir, float maxDist = 1.0f);
  Vector3 GetPosForItemDrop(Person* owner, Vector3Par refPos, Vector3Par dir, float itemBoundRadius, bool inCollision);
  void ItemPlaceOnSurfacePoint(int rotationFlags, Matrix4 &trans, Vector3Val surfPos, float dx, float dz, const Vector3* boundingBox);
  bool MoveToGround();

  //reset position of world parent
  void ResetParentPosition(Entity *parent);

  // switch off the item and all its attachments, lights etc.
  void SwitchOff();

  // offer ownership
  void CancelHierarchyParent(Entity *oldParent);
  Person* GetPersonOwner();

  virtual bool RemoveOwnedItem(InventoryItem *item) override;
  virtual bool AddOwnedItem(InventoryItem *item) override;
  virtual bool CanRemoveOwnedItem(const InventoryItem *item) const override;
  virtual bool CanAddOwnedItem(const InventoryItem *item) const override;
  virtual int CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const override;

  virtual bool ChildUpdateByParent(const Entity *obj) const override;

  // Inventory UI support
  virtual bool CanMoveItem(int index, const InventoryItem *item) const override;
  virtual void MoveItem(int index, InventoryItem *item) override;

  virtual int NAttachments() const override {return _attachments.Size();}
  virtual const InventoryItem *GetAttachment(int i) const override {return _attachments[i];}
  virtual InventoryItem *GetAttachment(int i) override {return _attachments[i];}
  virtual InventoryItem *GetAttachment(RString slotName);
  virtual InventoryItem *FindAttachment(InventorySlotId slotId) const;
  // check if attachment can be added to the slot
  virtual bool CanAddAttachment(const InventoryItem *item, int index) const;
  // add attachment to the slot
  virtual bool AddAttachment(InventoryItem *item, int index);
  // check if attachment can be added to the slot
  virtual bool CanAddAttachment(const InventoryItem *item) const;
  // add attachment to the slot
  virtual bool AddAttachment(InventoryItem *item);
  // create attachment in the weapon's slot if slot is empty
  InventoryItem *CreateAttachment(RString typeName);
  // move the item here as an attachment
  bool MoveAttachment(InventoryItem *item);

  bool CheckInventoryCondition(const EntityAI *item) const;
  bool CheckCargoClass(const InventoryItem *parent, const InventoryItem *item) const;

  // proxies handling
  virtual Object *GetProxy(
    VisualStateAge age, LODShape *&shape, Object *&parentObject,
    int level, Matrix4 &transform, const Matrix4 &parentPos, int i
  ) const override;
  virtual bool PreloadProxies(int level, float dist2, bool preloadTextures = false) const override;
  virtual bool NeedsPrepareProxiesForDrawing() const override;
  virtual void PrepareProxiesForDrawing(
    Object *rootObj, int rootLevel, const AnimationContext &animContext,
    const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so
  ) override;
  void DrawProxies(int level, float dist2, const PositionRender &transform, const std::function<bool(Matrix4 &matrix, SkeletonIndex boneIndex)> &func);

  virtual int GetProxyCountForLevel(int level) const;
  virtual const ProxyObjectTyped &GetProxyForLevel(int level, int i) const;
  virtual SkeletonIndex GetProxyBoneForLevel(int level, int i) const;
  virtual int GetProxySectionForLevel(int level, int i) const;

  // JSON serialization
  virtual void JSONSave(JSONValue &container, JSONAllocator &allocator) const;
  virtual bool JSONLoad(JSONValue &container, Person *person);
  virtual void CreateSubobjects() override;
  virtual void UpdateSubobjects() override;

  virtual void PlaceOnSurface(Matrix4 &trans, bool underWater) override; // place in steady position on surface including geometries
  void PlaceOnSurfaceOwnerTest(Person *owner, Matrix4 &trans, bool underWater);
  float FindMaxElevation(Person *owner, Vector3Par pos, bool underWater, float *dx, float *dz);

  void EnableAnimations(bool set) {_animationsEnabled = set;}

  void SetTexture(const RStringB &name);
  void ResetTexture();

  virtual void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2) override;

  //! empty simulation
  virtual void Simulate(float deltaT, SimulationImportance prec) override;

  virtual Vector3 AnimatePoint( const ObjectVisualState &vs, int level, int index ) const override;

  virtual void UpdatePosition() override;
  virtual void UpdateLights(const PositionRender &transform);

  RefR<INode> GetTooltip();
  RefR<INode> GetQuantity();

  virtual LSError Serialize(ParamArchive &ar) override;

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
  virtual TMError TransferMsg(NetworkMessageContext &ctx) override;
  virtual float CalculateError(NetworkMessageContextWithError &ctx) override;
  virtual Vector3 GetCurrentPosition() const override;

  INHERIT_SOFTLINK(InventoryItem, base);
  USE_CASTING(base)

private:
  Object *GetProxyInfo(int level, int index) const;

  bool CheckAttachmentCondition(const InventoryItem *attachment) const;
  bool CheckConditionDetach(const InventoryItem *attachment) const;
protected:
  const bool CheckConditionDispersion(const InventoryItem *attachment, const EntityAIFull *person) const;
  const bool CheckConditionDexterity(const InventoryItem *attachment, const EntityAIFull *person) const;

  void UpdateLights(const PositionRender &transform, ObjectLights &lights, const ObjectTypeLights &lightsType);
};

// basic storage space for inventory items
class InventoryStorage
{
private:
  // list of contained items
  RefArray<InventoryItem> _items;
  // available space
  int _width;
  int _height;

  // low level functions for basic operations
  void Add(InventoryItem *item, int index);
  void Render(StaticArrayAuto<bool> &image, const InventoryItem *exclude) const;

public:
  InventoryStorage() {_width = 0; _height = 0;}

  // enumerators
  bool ForEachItem(const std::function<bool(InventoryItem *item)> &func, bool recursive = true);
  bool ForEachItem(const std::function<bool(const InventoryItem *item)> &func, bool recursive = true) const;
  bool ForEachItemReverse(const std::function<bool(InventoryItem *item)> &func);
  bool ForEachItemReverse(const std::function<bool(const InventoryItem *item)> &func) const;

  // create and add an item
  InventoryItem *CreateItem(EntityAI *owner, RString typeName);
  // move item into container (remove it from the current location)
  bool MoveItem(EntityAI *owner, InventoryItem *item);

  // check if item fits into container, find the place if not given
  int CanAddItem(const InventoryItem *item, int index = -1) const;
  int CanReplaceItem(const InventoryItem *oldItem, const InventoryItem *newItem) const;
  bool CanRotateItem(const InventoryItem *item) const;

  bool AddItem(EntityAI *owner, InventoryItem *item, int index);
  bool RemoveItem(EntityAI *owner, InventoryItem *item);
  void DeleteItems(const std::function<bool(const InventoryItem *item)> &func);
  bool DeleteFirstItem(const std::function<bool(const InventoryItem *item)> &func);

  bool IsEmpty() const {return _items.Size() == 0;}
  bool FindItem(const InventoryItem *item) const {return _items.FindKey(item) >= 0;}

  void Compact() {_items.Compact();}

  int GetWidth() const {return _width;}
  int GetHeight() const {return _height;}
  void SetWidth(int width) {_width = width;}
  void SetHeight(int height) {_height = height;}

  const RefArray<InventoryItem> &GetItems() const {return _items;}
  int ItemOn(int row, int col) const;


  // !!! used only for serialization and network transport
  // for enumeration, use ForEachItem functions
  RefArray<InventoryItem> &GetItems() {return _items;}

private:
  // check the space for the item
  int FitsInto(const Array<bool> &image, const InventoryItemType *type, int index, bool rotated = false) const;
};

// Watch classes

class ItemWatchType : public InventoryItemType
{
private:
  typedef InventoryItemType base;
  friend class ItemWatch;

  //@{
  //! date selection
  AnimationUV _date1;
  AnimationUV _date2;
  AnimationUV _day;
  //@}

public:
  ItemWatchType(ParamEntryPar param) : base(param) {}
  virtual void Load(ParamEntryPar par, const char *shape) override;

  virtual Object* CreateObject(bool fullCreate) const override;

  //@{ LODShapeLoadHandler implementation
  virtual void LODShapeLoaded(LODShape *shape) override;
  virtual void LODShapeUnloaded(LODShape *shape) override;
  virtual void ShapeLoaded(LODShape *shape, int level) override;
  virtual void ShapeUnloaded(LODShape *shape, int level) override;
  //@}
};

class ItemWatch : public InventoryItem
{
private:
  typedef InventoryItem base;

public:
  ItemWatch(const EntityAIType *type, bool fullCreate) : base(type, fullCreate)
  {
  }

  const ItemWatchType *Type() const
  {
    return static_cast<const ItemWatchType *>(GetType());
  }

  virtual bool UseFromInventory(Person *owner) override;

  virtual bool PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level) override;
  virtual void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2) override;
  virtual void Deanimate(int level, bool setEngineStuff) override;
};

// Compass classes

class ItemCompassType : public InventoryItemType
{
private:
  typedef InventoryItemType base;
  friend class ItemCompass;

public:
  ItemCompassType(ParamEntryPar param) : base(param) {}
  virtual void Load(ParamEntryPar par, const char *shape) override;

  virtual AnimationSource *CreateAnimationSource(const AnimationType *type, RStringB source) override;
  virtual Object* CreateObject(bool fullCreate) const override;
};

class ItemCompass : public InventoryItem
{
private:
  typedef InventoryItem base;

public:
  ItemCompass(const EntityAIType *type, bool fullCreate) : base(type, fullCreate)
  {
  }

  const ItemCompassType *Type() const
  {
    return static_cast<const ItemCompassType *>(GetType());
  }
  virtual bool CanAutoShortcut() const override;
  virtual bool UseFromInventory(Person *owner) override;

  virtual bool PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level) override;

  float GetPointerPos(const ObjectVisualState &vs) const;
  float GetArrowPos(const ObjectVisualState &vs) const {return 0;}
  float GetCoverPos(const ObjectVisualState &vs) const {return 1;}
};

// GPS classes

class ItemGPSType : public InventoryItemType
{
private:
  typedef InventoryItemType base;
  friend class ItemGPS;

public:
  ItemGPSType(ParamEntryPar param) : base(param) {}
  virtual void Load(ParamEntryPar par, const char *shape) override;

  virtual Object* CreateObject(bool fullCreate) const override;
};

class ItemGPS : public InventoryItem
{
private:
  typedef InventoryItem base;

public:
  ItemGPS(const EntityAIType *type, bool fullCreate) : base(type, fullCreate)
  {
  }

  const ItemGPSType *Type() const
  {
    return static_cast<const ItemGPSType *>(GetType());
  }
};

// Book classes

class ItemBookType : public InventoryItemType
{
private:
  typedef InventoryItemType base;

  RString _title;
  RString _author;
  RString _file;

public:
  ItemBookType(ParamEntryPar param) : base(param) {}
  virtual void Load(ParamEntryPar par, const char *shape) override;

  virtual Object* CreateObject(bool fullCreate) const override;

  RString GetTitle() const {return _title;}
  RString GetAuthor() const {return _author;}
  RString GetFilename() const {return _file;}
};

class ItemBook : public InventoryItem
{
private:
  typedef InventoryItem base;

public:
  ItemBook(const EntityAIType *type, bool fullCreate) : base(type, fullCreate)
  {
  }

  const ItemBookType *Type() const
  {
    return static_cast<const ItemBookType *>(GetType());
  }

  virtual bool UseFromInventory(Person *owner) override;
};


#ifdef _MSC_VER
#pragma once
#endif

#ifndef _PERSON_HPP
#define _PERSON_HPP

/*!
\file
Interface for Person class
*/

#include "vehicleAI.hpp"
#include "Inventory/inventory.hpp"
#include "vehSupply.hpp"
#include "identity.hpp"
#include "proxyWeapon.hpp"
#include "turret.hpp"

#include "json.hpp"

#if _VBS2 // suppression
#define SUPPRESSION_RADIUS_INTEREST      3.0    // rounds that might go within this radius need watching
#define SUPPRESSION_VERTICAL_TOLERANCE   2.0    // scale factor for vertical dimension - be more tolerant
#define ADDITIONAL_SUPPRESS_MILLISECONDS 2      // time (milliseconds) to add to expected suppression time - better to let round
                                                // fly a little bit longer before doing the check rather than too short a time
                                                // as then won't be detected as suppressing.
#define ESTIMATED_FRAMES_PER_SEC         25.0   // an estimate of the number of frames (simulations) per second. This is used
                                                // for the exact flight time calculations of suppression because friction is
                                                // applied every time a round gets to move/simulate. Hence there can be non-trivial
                                                // differences in position.
#define ESTIMATED_FRICTION               0.0005 // the air friction constant being used for ShotShells and bullets.
#define SUPPRESSION_TOLERANCE_CHECK1     1.2    // a scaling factor to apply to the 1st round of suppression checking (where
                                                // we consider if a round could potentially [in the future] suppress so we
                                                // don't over-hastily eliminate rounds that fly close.
#define FRIENDLY_NO_SUPPRESSION_RADIUS2  100.0  // friendlies don't get suppressed if within 10 metres of firer.
#define SUPPRESSION_Y_TWITCH             1.25    // maximum twitch in the horizontal plane if round passes the player
#define SUPPRESSION_X_TWITCH             0.25    // maximum twitch in the vertical plane if round passes the player

#define MAX_AI_SUPPRESSION_TIME            3.0  // maximum amount of time that an AI unit with no skill will be suppressed
#define MAX_AI_SUPPRESSION_TIME_CUMULATIVE 8.0  // maximum cumulative amount of time that an AI unit will be suppressed
#endif

class Building;
class AIBrain;
class InputInterface;

class HeadObject;

DECL_ENUM(UnitPosition)

struct AIUnitInfo : public SerializeClass
{
  RString _fullName;
  RString _face;
  RString _glasses;
  RString _speaker;
  float _pitch;
  float _experience;
  float _initExperience;
  Rank _rank;
  RString _squadPicturePath;
  Ref<Texture> _squadPicture;
  RString _squadTitle;

  LSError Serialize(ParamArchive &ar);
};

#define MSG_SLOTS(XX, MessageName, name) \
 XX(MessageName, Ref<InventoryItem>, slot##name, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Inventory slot"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \

/// creating parts of AIBrain in CREATE_VEHICLE_BRAIN_MSG
#define CREATE_AI_BRAIN_MSG(MessageName, XX) \
  XX(MessageName, RString, fullName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name"), TRANSF) \
  XX(MessageName, RString, face, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Face"), TRANSF) \
  XX(MessageName, RString, speaker, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Speaker"), TRANSF) \
  XX(MessageName, float, pitch, NDTFloat, float, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Voice pitch"), TRANSF) \

const float AIUpdateErrorCoef = 0.1f;

/// updating parts of AIBrain in UPDATE_VEHICLE_BRAIN_MSG
#define UPDATE_AI_BRAIN_MSG(MessageName, XX) \
  XX(MessageName, int, captive, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Captive unit"), TRANSF, ET_NONE, 0) \
  XX(MessageName, int, lifeState, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, LifeStateAlive), DOC_MSG("Life state of unit (Alive, Death, Asleep etc.)"), TRANSF, ET_NOT_EQUAL, AIUpdateErrorCoef * ERR_COEF_MODE) \



/// updating parts of AIBrain as well
#define CREATE_VEHICLE_BRAIN_MSG(MessageName, XX) \
  CREATE_AI_BRAIN_MSG(MessageName, XX)

DECLARE_NET_INDICES_EX(CreateVehicleBrain, CreateVehicleAI, CREATE_VEHICLE_BRAIN_MSG)

#define UPDATE_VEHICLE_BRAIN_MSG(MessageName, XX) \
  XX(MessageName, int, remotePlayer, NDTInteger, int, NCTNone, DEFVALUE(int, 1), DOC_MSG("Person is controled by player on some client"), TRANSF, ET_NOT_EQUAL, ERR_COEF_IMMEDIATE) \
  INVENTORY_SLOT_ENUM(XX, MessageName, MSG_SLOTS) \
  XX(MessageName, OLinkPermO(InventoryItem), itemInHandsWanted, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Item in hands"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  UPDATE_AI_BRAIN_MSG(MessageName,XX)

DECLARE_NET_INDICES_EX_ERR(UpdateVehicleBrain, UpdateVehicleAIFull, UPDATE_VEHICLE_BRAIN_MSG)



struct ActionContextBase;

//! Abstract class Person - interface for any person related activites
/*!
	This class defines interface used to control any person activity.
	It also provides some minimal default implementation for some functions.
*/
class Person: public EntityAIFull, public IWeaponsHolder, public CountInstances<Person>
{
	typedef EntityAIFull base;

public:
  static const int NItemShortcuts = 10; // mapped to keys 1 .. 10 by default  

protected:
	Ref<AIBrain> _brain;

  AIUnitInfo _info; // TODO: move content to _identity
#if _ENABLE_IDENTITIES
  Identity _identity;
#endif


  ///frame identifier (used to call KeyboardPilots only once per frame) the counterpart is member of struct Input
  mutable int _inputFrameID;

	SensorRowID _sensorRowID;
	/*!
	_remotePlayer contain multiplayer ID of player
	_remotePlayer == 1 - AI Player
	*/
	int _remotePlayer;
  Time _disableDamageFromObjUntil;

  // get out ready, for vehicle with getoutinproxy , // todo find better way how to do it 
  bool _getOutReady;

  // state of weapons
  WeaponsState _weaponsState;

  // items currently used by person
  Ref<InventoryItem> _inventorySlots[NInventorySlotId];


  // shortcuts to inventory items
  Ref<InventoryItem> _itemShortcuts[NItemShortcuts];
  OLinkPermO(InventoryItem) _shortcutItems[NItemShortcuts];

  /// currently using item
  OLinkPermO(InventoryItem) _itemInHands;
  /// helper to Animation Subclass simulation
  struct ItemInHandsAnimClassInfo
  {
    RStringB animClass;
    int animSubIx;
    ItemInHandsAnimClassInfo() : animSubIx(-1) {}
    void Reset() { animClass = RStringB(); animSubIx=-1; }
  } _itemInHandsACInfo;

  /// the item wanted by the user/AI
  OLinkPermO(InventoryItem) _itemInHandsWanted;

  /// true while an item is being transferred out of the hands (drop/put away)
  bool _handsTransferInProgress;

  #if _VBS2 // fatigue model
protected:
  bool _isSuppressed;
  bool _suppressedWince;
  Time _suppressedUntil;
#endif

public:
	Person(const EntityAIType *name, bool fullCreate=true);
  ~Person() {}

  AIBrain *Brain() const {return _brain;}
  void SetBrain( AIBrain *brain );

  virtual void CreateSubobjects() override;
  virtual void UpdateSubobjects() override;

  virtual WeaponsState *GetWeaponsState() override {return &_weaponsState;}

  virtual bool RemoveOwnedItem(InventoryItem *item) override;
  virtual bool AddOwnedItem(InventoryItem *item) override;
  virtual bool CanAddOwnedItem(const InventoryItem *item) const override;
  virtual int CanReplaceOwnedItem(const InventoryItem *oldItem, const InventoryItem *newItem) const override;
  virtual bool ForEachOwnedItem(const std::function<bool(InventoryItem *)> &func) override;

  // Inventory UI support
  virtual bool CanMoveItem(int index, const InventoryItem *item) const override;
  virtual void MoveItem(int index, InventoryItem *item) override;

  // IWeaponsHolder implementation
  virtual void Compact() override;
  virtual bool ForEachWeapon(const std::function<bool(Weapon *weapon)> &func) override;
  virtual bool ForEachWeapon(const std::function<bool(const Weapon *weapon)> &func) const override;
  virtual bool AddWeapon(EntityAIFull *entity, Weapon *weapon) override;
  virtual bool RemoveWeapon(Weapon *weapon) override;
  virtual bool DeleteWeapon(RStringB name) override;
  virtual void DeleteWeapons(const std::function<bool(const Weapon *weapon)> &func) override;
  virtual void DeleteAllWeapons() override;
  virtual bool ForEachMagazine(const std::function<bool(Magazine *)> &func) override;
  virtual bool AddMagazine(EntityAIFull *entity, Magazine *magazine) override;
  virtual bool RemoveMagazine(Magazine *magazine) override;
  virtual bool DeleteMagazine(RStringB name) override;
  virtual void DeleteMagazines(const std::function<bool(const Magazine *magazine)> &func) override;
  virtual void DeleteAllMagazines() override;
  virtual WeaponModePtr GetCurrentWeapon() const override;
  virtual void SetCurrentWeapon(const WeaponModePtr &ptr) override;

  virtual void ForEachChildren(const std::function<bool(const NetworkObject *)> &func) const override;
  // Traversal through all owned items
  bool ForEachItem(const std::function<bool(InventoryItem *)> &func);
  bool ForEachItem(const std::function<bool(const InventoryItem *)> &func) const;
  bool ForEachItemReverse(const std::function<bool(InventoryItem *)> &func);
  bool ForEachItemReverse(const std::function<bool(const InventoryItem *)> &func) const;

  int CountItems(const InventoryItemType *type);
  void DeleteItems(const std::function<bool(const InventoryItem *item)> &func);
  bool DeleteFirstItem(const std::function<bool(const InventoryItem *item)> &func);
  bool CanAddItem(const InventoryItem *item) const;
  bool AddItem(InventoryItem *item);
  bool RemoveItem(InventoryItem *item);
  InventoryItem *CreateItem(RString typeName);
  bool MoveItem(InventoryItem *item);
  bool FindItem(const InventoryItem *item) const;
  bool DropItem(InventoryItem *item);

  // Inventory slots handling
  bool PutInSlot(InventorySlotId slot, InventoryItem *item);
  InventoryItem *RemoveFromSlot(InventorySlotId slot);
  InventoryItem *CreateInSlot(InventorySlotId slot, RString name);
  InventoryItem *DropFromSlot(InventorySlotId slot);

  const InventoryItem *GetInSlot(InventorySlotId slot) const;
  InventoryItem *GetInSlot(InventorySlotId slot);

  // helpers for inventory UI
  virtual InventoryItem *ToDrawInSlot(InventorySlotId slot) {return GetInSlot(slot);}
  virtual HeadObject *CreateHead() const {return NULL;}

  // Item in hands handling
  const InventoryItem *GetItemInHands() const {return _itemInHands;}
  InventoryItem *GetItemInHands() {return _itemInHands;}
  virtual void TakeItemToHands(InventoryItem *item);
  virtual void RemoveItemInHands();

  virtual bool Throw(Magazine *item) {return false;}

  virtual void RemovePipeBomb(Entity *pipeBomb) {}
  virtual bool HasPipeBomb(const Entity *pipeBomb) const {return false;}
  virtual bool CanDeactivatePipeBombs() const {return false;}
  virtual bool CanDeactivateMines() const {return false;}

  /// Create the correct type of brain, based on EntityAIType::_agentTasks
  void ApplyIdentity();

  /// select the first speaker matching to given speaker type and identity type of given person
  RString SelectSpeaker(RString speakerType);

  virtual AIBrain *PilotUnit() const {return _brain;}
  virtual AIBrain *GunnerUnit() const {return _brain;}
  virtual AIBrain *ObserverUnit() const {return _brain;}
  virtual AIBrain *CommanderUnit() const {return _brain;}

#if _ENABLE_IDENTITIES
  Identity &GetIdentity() {return _identity;}
  const Identity &GetIdentity() const {return _identity;}

  void CopyIdentity(const Person *source);
#endif

  /// Check whether given identity type is in the list of enabled types
  virtual bool IsIdentityEnabled(RString identityType) const {return true;}

	AIUnitInfo& GetInfo() {return _info;}
	void SetInfo(AIUnitInfo& info) {_info = info;}
  /// Get name from PlayerIdentity for player or _info._name otherwise
  RString GetPersonName() const;

	float GetExperience() const {return _info._experience;}
  void  SetExperience(float exp) { _info._experience = exp; }
	Rank GetRank() const {return _info._rank;}
	void SetRank(Rank rank) {_info._rank = rank;}

  bool IsRenegade() const;

	bool QIsManual() const;
  /// SetRemotePlayer also applies the identity (Face, Glasses, ...)
  void SetRemotePlayer(int player);
	int GetRemotePlayer() const {return _remotePlayer;}
	bool IsRemotePlayer() const;
  bool IsNetworkPlayer() const;
  bool IsControlled() const;

#if _ENABLE_CHEATS
  int  _vonDpnid;   //fake one. It servers only to VoNSay script purposes
  int  GetVoNSayDpnid() {return _vonDpnid;}
  void SetVoNSayDpnid(int dpnid) {_vonDpnid = dpnid;}
#endif

  //! called when animation state with entry forceUnarmed==true has finished (from ActionContextForceUnarmed::Do(Man *)
  void ForceUnArmed() {   HandleItemInHands(nullptr); _itemInHandsWanted = _itemInHands = nullptr; }

  void SetSensorRowID(SensorRowID sensorRowID) {_sensorRowID = sensorRowID;}
	SensorRowID GetSensorRowID() const {return _sensorRowID;}

  bool KeyboardUsedInFrame() const;

	virtual LODShape *GetOpticsModel(const Person *person) const;
	virtual bool GetEnableOptics(ObjectVisualState const& vs, const Person *person) const; 
  virtual PackedColor GetOpticsColor(const Person *person) {return PackedWhite;} // TODO: get color from muzzle
	virtual bool GetForceOptics(const Person *person, CameraType camType) const; 

  virtual void ToggleZoomIn() {}; //UAZoomInToggle helper method
  virtual bool IsZoomInToggled() {return false;} //UAZoomInToggle status
  virtual void ResetZoomInToggle() {}

  virtual void ToggleZoomOut() {} //UAZoomOutToggle helper method
  virtual bool IsZoomOutToggled() {return false;} //UAZoomOutToggle status
  virtual void ResetZoomOutToggle() {};

  /// search for special item of given type in the inventory
  virtual const WeaponType *GetSpecialItem(WeaponSimulation simulation) const {return NULL;}

  // check if person has active gun light
  virtual bool IsGunLightEnabled() const {return false;}
  /// check if person has some night vision equipment
  virtual bool IsNVEnabled() const {return false;}
	/// check if person wants to use the night vision equipment
	virtual bool IsNVWanted() const {return false;}
	/// check if night vision equipment is ready for rendering
	virtual bool IsNVReady() const {return true;}
  virtual void SetNVWanted(bool set=true) {}
	/// render night vision camera effects
  virtual void DrawNVOptics() {}
  virtual bool IsFlirWanted() const {return false;}

#if _VBS3
  //! Return coefficient describing the current tunnel vision (0 - no tunnel vision, 1 - full tunnel vision)
  virtual float GetTunnelVisionCoef() const {return 0.0f;}
  virtual VisionMode GetVisionMode() const {return VMDv;}
  virtual int GetTiModus() const {return 0;}
  virtual const ViewPars* GetViewPars(bool includeTurrets = true) const {return NULL;}
#endif
  virtual const VehPosRelatives *GetVehPosRelatives() const {return NULL;}

  virtual void ApplyVisionMode() {};
  virtual void NextVisionMode() {};

  virtual void KilledBy(EntityAI *owner) {}
  
  /// get aiming position - used for proxies
  virtual Vector3 GetLocalAimingPosition() const {return VZero;}

  virtual bool CanFloat() const {return true;}

	void Simulate(float deltaT, SimulationImportance prec);
  void CleanUp();
  void CleanUpMoveOut() {base::CleanUpMoveOut();}
  
#if _VBS2 // suppression
  void Suppress(ShotShell *shot, float closestDistance, Vector3 closestPosition);
  virtual void SuppressSecondaryEffects() {};
#endif

#if _VBS3
  virtual void ShowTrajectory() {};
#endif

  virtual void ResetMovement(float speed, int action=-1) = 0;
	virtual void ResetMovement(float speed, RString action) = 0;



	//! Action based interface to control person - smooth transition
  virtual bool PlayAction(ManAction action, ActionContextBase *context=NULL, bool callFuncNow=true){return false;}

  virtual bool PlayAction(RString action, ActionContextBase *context, bool callFuncNow=true){return false;}
  virtual bool PlayActionNow(RString action, ActionContextBase *context, bool callFuncNow=true) {return false;}
  virtual bool PlayMove(RStringB move, ActionContextBase *context) {return false;}
  virtual bool PlayMoveNow(RStringB move, ActionContextBase *context) {return false;}
  virtual void SwitchMove(RStringB move, ActionContextBase *context=nullptr) {}
	virtual void SwitchAction(RString action, ActionContextBase *context=nullptr, bool callFuncNow=true){}

  virtual void SwitchAction(ManAction action, ActionContextBase *context=nullptr){}

  virtual bool PlayActionGlobal(RString action) override {return PlayAction(action,nullptr);}
  virtual bool PlayActionNowGlobal(RString action) override {return PlayActionNow(action,nullptr);}
  virtual bool PlayMoveGlobal(RStringB move) override {return PlayMove(move,nullptr);}
  virtual bool PlayMoveNowGlobal(RStringB move) override {return PlayMoveNow(move,nullptr);}
  virtual void SwitchMoveGlobal(RStringB move) override {return SwitchMove(move,nullptr);}
  virtual void SwitchActionGlobal(RString action) override {return SwitchAction(action,nullptr);}

  virtual void PlayerAction(RString action) {}

  virtual void PlayerAction(RString move, const GameDataCode &handler) {}

  /// start get in and add it to activity list
  virtual void CreateActivityGetIn(Transport *veh, UIActionType pos, Turret *turret=NULL, int cargoIndex=-1) = 0;

  virtual void PreciseGetInAction(const RString action, const RString actionAfter, int doorIndex) = 0;
  virtual void PreciseGetOutAction(const RString action) = 0;

  /// process actions needed when person gets in vehicle
  virtual void OnGetInFinished() {};
  /// process actions needed when person gets out from vehicle
  virtual void OnGetOutFinished() {};

  /// access to direct speaking radio
  virtual RadioChannel *GetRadio() {return NULL;}
  /// access to direct speaking radio
  virtual const RadioChannel *GetRadio() const {return NULL;}
  /// attach 3D sound to person to update position and perform lip sync
  virtual void SetDirectSpeaking(AbstractWave *wave) {}

  /// attach 3D sound to person to update position and perform lip sync
  virtual void SetSpeaking3D(AbstractWave *wave) {}
  
  /// sometimes sound needs to be called even in a vehicle, like when speaking
  virtual bool SoundInTransportNeeded() const {return false;}

  /// perform sounds while in a vehicle
  virtual void SoundInTransport(Transport *parent, Matrix4Par worldPos, float deltaT){}

  /// helper for UI action  
  virtual ActionContextBase *CreateActionContextUIAction(const Action *action, ManAction moveAction) = 0;
  
  //! Shift of the action move. 
  virtual Vector3 GetActionMoveShift(RString action) const {return VZero;};
  
  virtual bool IsActionHighlighted(UIActionType type, EntityAI *target) const {return false;}
  virtual bool CanCancelAction() const {return false;}

	virtual void ResetStatus();

  virtual void PutDownEnd() {}

	virtual UnitPosition GetUnitPosition() const;
  virtual void SetUnitPositionCommanded(UnitPosition status) {}
  virtual void SetUnitPositionScripted(UnitPosition status) {}
  virtual void SetUnitPositionFSM(UnitPosition status) {}

  virtual void SetFace(RString name, RString player="") {}
  virtual void SetGlasses(RString name) {}
  /// Set the Face using _info for AI but PlayerIdentity for Players
  virtual void SetFace();
  /// Set the Glasses using _info for AI but PlayerIdentity for Players
  virtual void SetGlasses();

	virtual void BasicSimulation(float deltaT, SimulationImportance prec, float speedFactor) {} // also inside vehicle
  virtual float GetLegPhase() const {return 0.0f;}

  /// make sure person is watching given direction (in model space)
  virtual void AimHeadCrew(Vector3Par dir) {}
  /// return _lookXRotWanted and _lookYRotWanted
  virtual void GetLookWanted(float &x, float &y){x=0.0f,y=0.0f;};
  /// model space - current direction we are looking at
  virtual Vector3 GetCrewHeadDirection() const {return VForward;}
  /// model space - wanted direction to look at
  virtual Vector3 GetCrewHeadDirectionWanted() const {return VForward;}
  /// Looking around
  virtual void ProcessLookAround(float headingWanted, float diveWanted, const ViewPars *viewPars, float deltaT, bool forceLookAround=false, bool forceDiveAround=false) { Assert(false); }
  virtual void UpdateHeadLookAround(InputInterface *input, float deltaT, bool forceLookAround=false) { Assert(false); }
  /// used for instinctive reactions (e.g. looking into corners)
  virtual void ProcessLookAroundAuto(Vector3Par dir) {}
  virtual Zoom *GetZoom() {return NULL;}
  virtual void UpdateHeadLookWanted(float heading, float dive) {Assert(false);}
  /// model space - current direction we are looking at
  virtual Vector3 GetCrewLookDirection(ObjectVisualState const &vs) const {return VForward;}
  /// reset dive camera view direction
  virtual void ResetXRotWanted() {}
  /// it currently stops swimming animation (used when person enters the vehicle while swimming)
  virtual void OnMovedFromLandscape() {}
  /// return true when soldier is swimming
  virtual bool IsSwimming() const {return false;}
  
	virtual float GetAnimSpeed(RStringB move) {return 1.0f;}
	virtual Vector3 GetPilotPosition(CameraType camType) const {return VZero;}
  virtual void RecalcShakeTransform(Vector3Par v,float deltaT){};

  virtual Matrix3 HeadBobOrientation(const VisualStateAge vsa, CameraType camType, bool setEyes) const {return M3Identity;};
	
  virtual bool UseInternalLODInVehicles()	const {return false;}

	virtual void ShowHead(bool show=true) {}
	virtual void ShowWeapons(bool showPrimary=true, bool showSecondary=true) {}

	virtual void HideBody() {}

  virtual Vector3 GetCommonDamagePoint() {return VZero;}

	virtual LSError Serialize(ParamArchive &ar);
	virtual LSError SerializeIdentity(ParamArchive &ar);

  virtual void CatchLadder(Building *obj, int ladder, bool up) {}
  virtual void DropLadder(Building *obj, int ladder) {}
  virtual bool IsOnLadder(Building *obj, int ladder) const {return false;}

	virtual bool IsWoman() const {return false;}
  virtual RString GetGenericNames() const {return "Default";} // default set of names for non-Man Persons (required in the core config)
  virtual RString GetFaceType() const {return RString();}

  virtual Vector3 GetHUDOffset() const {return VZero;};
	
	DECLARE_NETWORK_OBJECT
	static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
	TMError TransferMsg(NetworkMessageContext &ctx);
	float CalculateError(NetworkMessageContextWithError &ctx);
  
  // JSON serialization
  void JSONSave(JSONValue &container, JSONAllocator &allocator) const;
  bool JSONLoad(JSONValue &container, bool loadPos=true);

  void SimulateImpulseDamage(DoDamageResult &result, EntityAI *owner, float impulse, Vector3Par modelPos);

  void DisableDamageFromObj(float seconds);
  virtual void FreeFall(float seconds=1.0f) {}

  virtual bool ForEachTurret(ITurretFunc &func) const;
  virtual bool ForEachTurret(EntityVisualState& vs, ITurretFuncV &func) const;
  virtual bool ForEachTurretEx(EntityVisualState& vs, ITurretFuncEx &func) const;
  /// enumerate all crew
  virtual bool ForEachCrew(ICrewFunc &func) const;

#if _VBS3 //Commander override
  bool FindOpticsTurret(const Person *gunner, TurretContext &context) const { return FindTurret(gunner, context);}
  bool FindControlledTurret(const Person *gunner, TurretContext &context) const { return FindTurret(gunner, context);}
#endif

  virtual bool FindTurret(const Person *gunner, TurretContext &context) const;
  virtual bool FindTurret(EntityVisualState& vs, const Person *gunner, TurretContextV& context) const;
  virtual bool GetPrimaryGunnerTurret(TurretContext& context) const;
  virtual bool GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextV& context) const;
  virtual bool GetPrimaryGunnerTurret(EntityVisualState& vs, TurretContextEx& context) const;
  virtual bool GetPrimaryObserverTurret(TurretContext& context) const;
  virtual bool GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextV& context) const;
  virtual bool GetPrimaryObserverTurret(EntityVisualState& vs, TurretContextEx& context) const;

  WeaponsState &GetWeapons() {return _weaponsState;}

  /// perform effects after weapon is actually fired
  virtual bool FireWeaponEffects(const TurretContext &context, const WeaponModePtr &ptr, const Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo *remoteInfo) override;

  //! adds only minimal equipment for given class
  virtual void MinimalWeapons();
  //! remove all weapons
  void RemoveAllWeapons(bool removeItems);
  /// remove all special items
  void RemoveAllItems();
  //! remove all magazines
  void RemoveAllMagazines();

  //! event handler: some weapon changed
  virtual void OnWeaponChanged();

  virtual void SimulateWeaponInputs(InputInterface *input, AIBrain *unit, const TurretContext &context) override;

  /// Shortcuts to inventory items
  const InventoryItemType *GetItemShortcut(int index) const;
  void SetItemShortcut(int index, const InventoryItemType *itemType);
  void SetItemShortcutInstance(int index, InventoryItem *item);
  int FindItemShortcutIndex(const InventoryItemType *itemType);
  InventoryItem *GetItemShortcutObject(int index) const;
  void RemoveItemShortcut(InventoryItem *item);

  void OnItemAdded(InventoryItem *item);

  bool GetOutReady() const {return _getOutReady;};
  void SetOutReady(bool val) {_getOutReady = val;};

  INHERIT_SOFTLINK(Person,base);
	USE_CASTING(base)

protected:
  void HandleItemInHands(InventoryItem *item); // free temporary hands slot

private: // no default copy
	Person( const Person &src );
	void operator=(const Person &src);
};

typedef Person VehicleWithBrain;
//template Ref<Person>;

void PersonsRefArray2NO(RefArray<Person> &persons, RefArray<NetworkObject> &netObj);
void NORefArray2Persons(RefArray<NetworkObject> &netObj, RefArray<Person> &persons);

#endif

/*!
\file
Interface for resource supplies
*/

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _VEHICLE_SUPPLY_HPP
#define _VEHICLE_SUPPLY_HPP

#include "Network/networkObject.hpp"
#include "weapons.hpp"
#include "memberCalls.hpp"
#include "vehicleAI.hpp"
#include "UI/uiActions.hpp"

#define LOG_WEAPONS_CHANGES 0

class Magazine;
class WeaponType;

class ResourceSupply
{
	private:
	float _fuelCargo,_repairCargo,_ammoCargo;
  InventoryStorage _itemsCargo;
	OLinkO(EntityAIFull) _supplying;
	OLinkO(EntityAIFull) _alloc;
	Ref<Action> _action;
	
	/// make sure we do not start automatic refuel unless needed, but we finish it once we have started
	bool _wasRefuelingPlayer;
	
  /// even empty resource with _forceSupply set cannot be deleted when locked
  /// safe REF for the case when MP client disconnects and cannot unlock supply 
  RefArray<AIBrain> _resourceLock;

  //! units that are services by this resource
	mutable SupplyUnitList _supplyUnits;

	public:
	ResourceSupply( EntityAI *vehicle, bool fullCreate = true);
  ~ResourceSupply();

  void Destroy(EntityAI *parent);

  // direct access to items cargo
  InventoryStorage &GetItemsCargo() {return _itemsCargo;}
  const InventoryStorage &GetItemsCargo() const {return _itemsCargo;}

	void Simulate( EntityAI *parent, float deltaT, SimulationImportance prec );

	void SetAlloc(EntityAIFull *vehicle) {_alloc=vehicle;}
	EntityAIFull *GetAlloc() const {return _alloc;}

  const Action *GetAction() const {return _action;}

  bool Supply(EntityAIFull *vehicle, const Action *action);
  void CancelSupply();
	EntityAIFull *GetSupplying() const {return _supplying;}

  SupplyUnitList &GetSupplyUnits() const {return _supplyUnits;}

	float GetFuelCargo() const {return _fuelCargo;}
	float GetRepairCargo() const {return _repairCargo;}
	float GetAmmoCargo() const {return _ammoCargo;}

  bool ForEachItemCargo(const std::function<bool(InventoryItem *item)> &func, bool recursive = true);
  bool ForEachItemCargo(const std::function<bool(const InventoryItem *item)> &func, bool recursive = true) const;
  bool FindItemCargo(const InventoryItem *item) const;
  bool ForEachWeaponCargo(const std::function<bool(const Weapon *weapon)> &func) const;
  bool ForEachMagazineCargo(const std::function<bool(const Magazine *magazine)> &func) const;

	void LoadFuelCargo( float cargo ) {_fuelCargo+=cargo;}
	void LoadRepairCargo( float cargo ) {_repairCargo+=cargo;}
	void LoadAmmoCargo( float cargo ) {_ammoCargo+=cargo;}

  void DeleteIfNotNeeded(EntityAI *parent);
  void LockResources(AIBrain *user) {if(user) _resourceLock.AddUnique(user);}
  void UnlockResources(AIBrain *user) {if(user)  _resourceLock.Delete(user);}
  bool AreResourcesLocked() const {return _resourceLock.Size() > 0;}

	void ClearWeaponCargo(EntityAI *parent, bool localOnly = true);
	bool AddWeaponCargoCount(EntityAI *parent, const WeaponType *weaponType, int count);

	void ClearMagazineCargo(EntityAI *parent, bool localOnly = true);
	bool AddMagazineCargoCount(EntityAI *parent, const MagazineType *type, int count);

	void GetActions(EntityAI *parent, const std::function<void(const UIAction &action)> &add, AIBrain *unit, bool now);
#if _VBS3
  float GetMagazineWeightKg();
  float GetWeaponWeightKg();
#endif

	LSError Serialize(ParamArchive &ar);
	static ResourceSupply *CreateObject(ParamArchive &ar) {return new ResourceSupply();}

  DECLARE_NETWORK_OBJECT
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(EntityAI *parent, NetworkMessageContext &ctx);
	float CalculateError(NetworkMessageContextWithError &ctx);
	static ResourceSupply *CreateObject(const NetworkMessageContext &ctx) {return new ResourceSupply();}
	
private:
	ResourceSupply(); // used for serialization only

};

#define UPDATE_SUPPLY_MSG(MessageName, XX) \
  XX(MessageName, float, fuelCargo, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported fuel"), TRANSF, ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, repairCargo, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported repair material"), TRANSF, ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR) \
  XX(MessageName, float, ammoCargo, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported ammunition (for vehicles)"), TRANSF, ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR) \
  XX(MessageName, OLinkO(EntityAIFull), alloc, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Unit allocated for supplying"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, RefArray<InventoryItem>, itemsCargo, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Inventory items in cargo"), TRANSF_REFS, ET_NOT_EQUAL_COUNT, ERR_COEF_MODE) \

//  XX(MessageName, OLinkO(EntityAIFull), supplying, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Currently supplying unit"), TRANSF_REF, ET_NOT_EQUAL, ERR_COEF_MODE) \
// TODO:
/*
  XX(MessageName, int, action, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, ATNone), DOC_MSG("Currently processing action"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, int, actionParam, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, int, actionParam2, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE) \
  XX(MessageName, RString, actionParam3, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Action parameter"), TRANSF, ET_NOT_EQUAL, ERR_COEF_MODE)
*/

DECLARE_NET_INDICES_ERR(UpdateSupply, UPDATE_SUPPLY_MSG)

RETVALUES_NO_SPEC(MagazineType *)
RETVALUES_NO_SPEC(WeaponType *)
RETVALUES_NO_SPEC(Magazine *)
RETVALUES_NO_SPEC(Weapon *)

RETVALUES_NO_SPEC(const MagazineType *)
RETVALUES_NO_SPEC(const WeaponType *)
RETVALUES_NO_SPEC(const Magazine *)
RETVALUES_NO_SPEC(const Weapon *)

CALL_ARGS(ResourceSupply,EntityAI,GetSupply())

#define SUPPLY(veh,x,arg) CALLARGS(veh,ResourceSupply,x,arg)
#define SUPPLY_DEF(veh,x,arg,def) CALLARGS_DEF(veh,ResourceSupply,x,arg,def)


inline float EntityAI::GetFuelCargo() const {return SUPPLY(this,GetFuelCargo, ());}
inline float EntityAI::GetRepairCargo() const {return SUPPLY(this,GetRepairCargo, ());}
inline float EntityAI::GetAmmoCargo() const {return SUPPLY(this,GetAmmoCargo, ());}

inline bool EntityAI::ForEachItemCargo(const std::function<bool(InventoryItem *item)> &func, bool recursive) {return GetSupply()->ForEachItemCargo(func, recursive);}
inline bool EntityAI::ForEachItemCargo(const std::function<bool(const InventoryItem *item)> &func, bool recursive) const {return GetSupply()->ForEachItemCargo(func, recursive);}
inline bool EntityAI::FindItemCargo(const InventoryItem *item) const {return GetSupply() ? GetSupply()->FindItemCargo(item) : false;}
inline bool EntityAI::ForEachWeaponCargo(const std::function<bool(const Weapon *weapon)> &func) const {return GetSupply()->ForEachWeaponCargo(func);}
inline bool EntityAI::ForEachMagazineCargo(const std::function<bool(const Magazine *magazine)> &func) const {return GetSupply()->ForEachMagazineCargo(func);}

inline void EntityAI::LoadFuelCargo( float cargo ) {SUPPLY(this,LoadFuelCargo, (cargo));}
inline void EntityAI::LoadRepairCargo( float cargo ) {SUPPLY(this,LoadRepairCargo, (cargo));}
inline void EntityAI::LoadAmmoCargo( float cargo ) {SUPPLY(this,LoadAmmoCargo, (cargo));}

inline void EntityAI::DeleteIfNotNeeded() {SUPPLY(this, DeleteIfNotNeeded, ());}
inline void EntityAI::LockResources(AIBrain *user) {SUPPLY(this, LockResources, (user));}
inline void EntityAI::UnlockResources(AIBrain *user) {SUPPLY(this, UnlockResources, (user));}
inline bool EntityAI::AreResourcesLocked() const {return SUPPLY(this, AreResourcesLocked, ());}

inline void EntityAI::ClearWeaponCargo(bool localOnly) {SUPPLY(this,ClearWeaponCargo, (localOnly));}
inline bool EntityAI::AddWeaponCargo(const WeaponType *weapon, int count)
{
	return SUPPLY_DEF(this,AddWeaponCargoCount, (weapon, count), -1);
}

inline void EntityAI::ClearMagazineCargo(bool localOnly) {SUPPLY(this,ClearMagazineCargo, (localOnly));}
inline bool EntityAI::AddMagazineCargoCount(const MagazineType *type, int count)
{
	return SUPPLY_DEF(this,AddMagazineCargoCount, (type, count), -1);
}

#endif

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETWORK_IMPL_HPP
#define _NETWORK_IMPL_HPP


#include "netTransport.hpp"
#include "network.hpp"
#include "../versionNo.h"
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
#include <Steamworks/steam_gameserver.h>
#endif

#include <Es/Common/win.h>
#include "../memHeap.hpp"

#include "../speaker.hpp"
#include "../arcadeWaypoint.hpp"

#include "../paramFileExt.hpp"

#include <El/Interfaces/iAppInfo.hpp>
#include <El/DebugWindow/debugWin.hpp>
#include <El/DataSignatures/dataSignatures.hpp>

#include <El/FileServer/multithread.hpp>
#include <El/Multicore/jobs.hpp>
#include "Es/Containers/quadtreeEx.hpp"

#include <El/Common/randomGen.hpp>
#include <El/XML/inet.hpp>


#include "../AI/ai.hpp"
#include "../house.hpp"



/*!
\file
Internal interface file for multiplayer game (included only from inside of multiplayer files)
*/

//! Magic for distinguish applications
#define MAGIC_APP ((int)'D'+((int)'a'<<8)+((int)'y'<<16)+((int)'Z'<<24))

/* ArmA 1 values
#elif _DEMO // demo and ArmA do not share LAN sessions
  #define MAGIC_APP   0x45454545
#else // ArmA
  #define MAGIC_APP   0x23232323
#else // ArmA 2
  #define MAGIC_APP   0x25252525
#endif
*/

// old OFP had #define MAGIC_APP   0x00000000

#define NO_CLIENT               0
#define TO_SERVER               1
// TODO: Avoid usage of this value as regular client ID
#define BOT_CLIENT              2

extern const int DiagLevel;
bool DiagFilter(NetworkMessageType type);
RString GetMsgTypeName(NetworkMessageType type);
// extern const char *NetworkMessageTypeNames[];

struct NetworkPlayerInfo;
class NetworkServer;

#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
class SteamValidation
{
  uint64 _steamId;
  NetworkServer *_server;
  int _dpid;
  bool _started;
  bool _authPending;
  unsigned long _ticksStarted;
  RString _name;
  AutoArray<char> _steamSessionTicket;
public:
  SteamValidation();
  ~SteamValidation();
  void Start(PlayerIdentity &identity, NetworkServer *server);
  void HandleTimeout();
  STEAM_GAMESERVER_CALLBACK(SteamValidation, OnValidateAuthTicketResponse, ValidateAuthTicketResponse_t, _callbackGSAuthTicketResponse);
};
#endif

//////////////////////////////////////////////////////////////////////////////
// Network Messages

//! Encapsulation for block of data transferred over network.
/*!
Implement methods nedded for transfer of NetworkMessage into raw data.
\todo Implement compression methods.
\todo Work with bits, no bytes.
*/
class NetworkMessageRaw
{
protected:
  //! raw data itself
  AutoArray<char> _buffer;
  //! read / write position
  int _pos;

  //! external raw data
  char *_externalBuffer;
  //! size of external raw data
  int _externalBufferSize;
public:
  //! Constructor
  NetworkMessageRaw()
  {_pos = 0; _buffer.Realloc(1024); _externalBuffer = NULL; _externalBufferSize = 0;}
  //! Constructor
  /*!
    \param buffer external data
    \param size size of external data
  */
  NetworkMessageRaw(char *buffer, int size)
  {_pos = 0; _externalBuffer = buffer; _externalBufferSize = size;}

  //! Return total size of valid data
  int GetSize() const {return _externalBuffer ? _externalBufferSize : _buffer.Size();}
  //! Set new size of internal buffer, sets position to 0
  void SetSize(int size) {_buffer.Resize(size); _pos = 0;}
  //! Get (read only) pointer to valid data
  const char *GetData() const {return _externalBuffer ? _externalBuffer : _buffer.Data();}
  //! Get (read and write) pointer to internal buffer
  char *SetData() {return _buffer.Data();}
  //! Return current read / write position in message
  int GetPos() const {return _pos;}

  template <class IntType>
  void PutSmallUnsigned(IntType value);

  //@{
  //! Write single value to buffer
  /*!
  \param value value to write
  \param compression compression algorithm
  */
  void Put(bool value, NetworkCompressionType compression);
  void Put(char value, NetworkCompressionType compression);
  void Put(int value, NetworkCompressionType compression);
  void Put(NetInt64 &value, NetworkCompressionType compression);
  void Put(float value, NetworkCompressionType compression);
  void Put(RString value, NetworkCompressionType compression);
  void Put(RStringB value, NetworkCompressionType compression);
  void Put(const LocalizedString &value, NetworkCompressionType compression);
  void Put(Time value, NetworkCompressionType compression);
  void Put(Vector3Par value, NetworkCompressionType compression);
  void Put(Matrix3Par value, NetworkCompressionType compression);
  void Put(NetworkId &value, NetworkCompressionType compression);
  void Put(CreatorId &value, NetworkCompressionType compression);
  void Put(XNADDR &value, NetworkCompressionType compression);
  void Put(XONLINE_STAT &value, NetworkCompressionType compression);
	void* PutRaw(int size);

  //@}

  //@{
  //! Read single value from buffer
  /*!
  \param value reference to variable read into
  \param compression compression algorithm
  \return true if value was read, otherwise false
  */
  bool GetNoCompression(RString &value);
  bool GetNoCompression(RStringB &value);

  template <class IntType>
  bool GetSmallUnsigned(IntType &value);

  bool Get(bool &value, NetworkCompressionType compression);
  bool Get(char &value, NetworkCompressionType compression);
  bool Get(int &value, NetworkCompressionType compression);
  bool Get(NetInt64 &value, NetworkCompressionType compression);
  bool Get(float &value, NetworkCompressionType compression);
  bool Get(RString &value, NetworkCompressionType compression);
  bool Get(RStringB &value, NetworkCompressionType compression);
  bool Get(LocalizedString &value, NetworkCompressionType compression);
  bool Get(Time &value, NetworkCompressionType compression);
  bool Get(Vector3 &value, NetworkCompressionType compression);
  bool Get(Matrix3 &value, NetworkCompressionType compression);
  bool Get(NetworkId &value, NetworkCompressionType compression);
  bool Get(CreatorId &value, NetworkCompressionType compression);
  bool Get(XNADDR &value, NetworkCompressionType compression);
  bool Get(XONLINE_STAT &value, NetworkCompressionType compression);
	void* GetRaw(int size);
  //@}

protected:
  //! Write raw data to buffer
  /*!
  \param buffer raw data
  \param size raw data size
  */
  void Write(const void *buffer, int size)
  {
    int minSize = _pos + size;
    if (_buffer.Size() < minSize) _buffer.Resize(minSize);
    memcpy(_buffer.Data() + _pos, buffer, size);
    _pos += size;
  }
  //! Read raw data from buffer
  /*!
  \param buffer where read raw data
  \param size size of data to read
  \return true if data available in buffer, otherwise false
  */
  bool Read(void *buffer, int size)
  {
    int minSize = _pos + size;
    if (_externalBuffer)
    {
      if (_externalBufferSize < minSize) return false;
      memcpy(buffer, _externalBuffer + _pos, size);
    }
    else
    {
      if (_buffer.Size() < minSize) return false;
      memcpy(buffer, _buffer.Data() + _pos, size);
    }
    _pos += size;
    return true;
  }
};
TypeIsMovable(NetworkMessageRaw)

#define PLAYER_MSG(MessageName, XX) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Unique ID of client (player)"), TRANSF)\
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Player's name"), TRANSF)\
  XX(MessageName, bool, server, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Bot client (running in the same process as server)"), TRANSF)\
  XX(MessageName, bool, vonDisabled, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Voice over Net disabled"), TRANSF)\
  XX(MessageName, int, vonCodecQuality, NDTInteger, int, NCTNone, DEFVALUE(int, 3), DOC_MSG("Voice over Net codec quality"), TRANSF)\
  XX(MessageName, bool, battlEye, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("BattlEye is required by the server"), TRANSF)\
  XX(MessageName, NetInt64, steamServerId, NDTInt64, NetInt64, NCTNone, DEFVALUE(NetInt64, 0), DOC_MSG("Steam server id"), TRANSF)\
  XX(MessageName, bool, steamSecure, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Steam VAC protection is active"), TRANSF)\
  XX(MessageName, int, sigVerRequired, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Required signatures version on the server"), TRANSF)\
  /*XX(MessageName, RString, regURL, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ID Registration URL"), TRANSF)*/\
  XX(MessageName, bool, updatesGuaranteed, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Guaranteed (diffed) updates sent by the server"), TRANSF)\

DECLARE_NET_MESSAGE(Player, PLAYER_MSG)

#define LOGOUT_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of client (player)"), TRANSF)

DECLARE_NET_MESSAGE(Logout, LOGOUT_MSG)

#define PUB_VAR_TO_MSG(MessageName, XX) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Variable name"), TRANSF) \
  XX(MessageName, AutoArray<char>, value, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Variable value"), TRANSF) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Unique ID of client (player)"), TRANSF)

DECLARE_NET_MESSAGE_INIT_MSG(PublicVariableTo, PUB_VAR_TO_MSG)

#define SPAWN_FOR_PLAYER_MSG(MessageName, XX) \
  XX(MessageName, RString, code, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Code to execute"), TRANSF) \
  XX(MessageName, AutoArray<char>, signature, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Code signature to verify it was issued by the server"), TRANSF) \
  XX(MessageName, AutoArray<char>, value, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Parameters"), TRANSF)

DECLARE_NET_MESSAGE_INIT_MSG(SpawnForPlayer,SPAWN_FOR_PLAYER_MSG)

TypeIsMovable(SpawnForPlayerMessage);

#define SYNCHRONIZE_VARIABLE_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAI), veh, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Variable owner"), TRANSF_REF) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Variable name"), TRANSF) \
  XX(MessageName, float, coef, NDTFloat, float, NCTFloatMostly0To1, DEFVALUE(float, 0), DOC_MSG("Importance"), TRANSF) \
  XX(MessageName, RString, handler, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Change handler"), TRANSF) \

DECLARE_NET_MESSAGE(SynchronizeVariable,SYNCHRONIZE_VARIABLE_MSG)

#define APPLYWEATHER_MSG(MessageName,XX) \
  XX(MessageName, float,seaLevelOffset, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, float,overcastSetSky, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  /*XX(MessageName, float,overcastSetClouds, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  XX(MessageName, float,fogSet, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  /*XX(MessageName, float,cloudsPos, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  /*XX(MessageName, float,cloudsAlpha, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  /*XX(MessageName, float,cloudsBrightness, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  /*XX(MessageName, float,cloudsSpeed, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  /*XX(MessageName, float,skyThrough, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  /*XX(MessageName, float,rainDensity, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF)*/ \
  XX(MessageName, float,wind, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, float,rainDensityWanted, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, float,rainDensitySpeed, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,rainNextChange, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,nextWeatherChange, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,lastWindSpeedChange, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,gustUntil, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, Vector3,windSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG(""), TRANSF) \
  XX(MessageName, Vector3,currentWindSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG(""), TRANSF) \
  XX(MessageName, Vector3,currentWindSpeedSlow, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG(""), TRANSF) \
  XX(MessageName, Vector3,gust, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,year, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,month, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,day, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,hour, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, int,minute, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG(""), TRANSF)

DECLARE_NET_MESSAGE(ApplyWeather, APPLYWEATHER_MSG)

#if _VBS3

#define AARDOUPDATE_MSG(MessageName,XX) \
  XX(MessageName, int,  type, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Type of AAR message"), TRANSF) \
  XX(MessageName, int, intType, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("int type can be anything"), TRANSF) \
  XX(MessageName, RString, stringOne , NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("string can be anything"), TRANSF) \
  XX(MessageName, RString, stringTwo , NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("string can be anything"), TRANSF) \
  XX(MessageName, float  , floatType , NDTFloat, float, NCTNone, DEFVALUE(float, 0.5), DOC_MSG("float type can be anything"), TRANSF) \

DECLARE_NET_MESSAGE(AARDoUpdate, AARDOUPDATE_MSG)

#define AARASKUPDATE_MSG(MessageName,XX) \
  XX(MessageName, int,  type, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Type of AAR message"), TRANSF) \
  XX(MessageName, int, intType, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("int type can be anything"), TRANSF) \
  XX(MessageName, RString, stringOne , NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("string can be anything"), TRANSF) \
  XX(MessageName, RString, stringTwo , NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("string can be anything"), TRANSF) \
  XX(MessageName, float  , floatType , NDTFloat, float, NCTNone, DEFVALUE(float, 0.5), DOC_MSG("float type can be anything"), TRANSF) \

DECLARE_NET_MESSAGE(AARAskUpdate, AARASKUPDATE_MSG)


#define ADDMPREPORT_MSG(MessageName,XX) \
  XX(MessageName, int, type , NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Type of string carried to add to all clients"), TRANSF) \
  XX(MessageName, RString, myString , NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("string can be anything"), TRANSF) \

DECLARE_NET_MESSAGE(AddMPReport, ADDMPREPORT_MSG)

#define LOADISLAND_MSG(MessageName,XX) \
  XX(MessageName, RString, islandName , NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Island to load on the client"), TRANSF) \

DECLARE_NET_MESSAGE(LoadIsland, LOADISLAND_MSG)


#define ASKCOMMANDEROVERRIDE_MSG(MessageName,XX) \
  XX(MessageName, OLinkPermNO(Turret), turret, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Specification of turret to override"), TRANSF_REF) \
  XX(MessageName, OLinkNO(Turret), overriddenByTurret, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Which turret takes control"), TRANSF_REF)

DECLARE_NET_MESSAGE(AskCommanderOverride, ASKCOMMANDEROVERRIDE_MSG)

#endif //_VBS3

#define ASK_FOR_SET_ITEM_SHORTCUT_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Player person"), TRANSF_REF) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Shortcut index"), TRANSF)\
  XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Bound inventory item"), TRANSF_REF) \
  XX(MessageName, RString, itemType, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of the item type"), TRANSF) \

DECLARE_NET_MESSAGE(AskForSetItemShortcut, ASK_FOR_SET_ITEM_SHORTCUT_MSG)

#define ASK_INVENTORY_MENU_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), unit, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Asked unit"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item1, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Involved item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item2, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Involved item"), TRANSF_REF) \

DECLARE_NET_MESSAGE(AskInventoryMenu, ASK_INVENTORY_MENU_MSG)

#define ASK_FOR_THROW_ITEM_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Man), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Person throwing the item"), TRANSF_REF) \
	XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Thrown item"), TRANSF_REF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, Vector3(0, 0, 0)), DOC_MSG("item start position in world"), TRANSF) \
  XX(MessageName, Vector3, dir, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, Vector3(0, 0, 0)), DOC_MSG("throw direction"), TRANSF) \

DECLARE_NET_MESSAGE(AskForThrowItem, ASK_FOR_THROW_ITEM_MSG)

#define ASK_FOR_DROP_ITEM_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Person dropping the item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Dropped item"), TRANSF_REF) \

DECLARE_NET_MESSAGE(AskForDropItem, ASK_FOR_DROP_ITEM_MSG)

#define ASK_FOR_MOVE_ITEM_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAI), target, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("New owner of the item"), TRANSF_REF) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Slot index"), TRANSF)\
  XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Moved item"), TRANSF_REF) \
  XX(MessageName, bool, rotated, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Requested inventory orientation"), TRANSF) \

DECLARE_NET_MESSAGE(AskForMoveItem, ASK_FOR_MOVE_ITEM_MSG)

#define ASK_FOR_ROTATE_INVENTORY_ITEM_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Player requesting item rotation"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Item to rotate in cargo"), TRANSF_REF) \

DECLARE_NET_MESSAGE(AskForRotateInventoryItem, ASK_FOR_ROTATE_INVENTORY_ITEM_MSG)

#define ASK_FOR_TAKE_ITEM_TO_HANDS_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Person holding the item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Held item"), TRANSF_REF) \

DECLARE_NET_MESSAGE(AskForTakeItemToHands, ASK_FOR_TAKE_ITEM_TO_HANDS_MSG)

#define ASK_FOR_SWAP_ITEMS_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(InventoryItem), item1, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Swapping item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item2, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Swapping item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(Person), owner, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Person owning the items"), TRANSF_REF) \

DECLARE_NET_MESSAGE(AskForSwapItems, ASK_FOR_SWAP_ITEMS_MSG)

#define ASK_FOR_PLAYER_ACTION_MSG(MessageName, XX) \
  XX(MessageName, RString, action, NDTString, RString, NCTStringMove, DEFVALUE(RString, ""), DOC_MSG("Action identification"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Person doing the action (must be owned by the client)"), TRANSF_REF) \

DECLARE_NET_MESSAGE(AskForPlayerAction, ASK_FOR_PLAYER_ACTION_MSG)

#if _VBS3 // respawn command
#define PAUSESIMULATION_MSG(MessageName, XX) \
  XX(MessageName, bool, pause, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Pause local simulation"), TRANSF)

DECLARE_NET_MESSAGE(PauseSimulation,PAUSESIMULATION_MSG)

#endif

#if _ENABLE_ATTACHED_OBJECTS
#define ATTACHOBJECT_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Object), obj, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Object to attach"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(Object), attachTo, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Object to attach to"), TRANSF_REF) \
  XX(MessageName, int, memIndex, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("memory point index"), TRANSF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, Vector3(0, 0, 0)), DOC_MSG("offset"), TRANSF) \
  XX(MessageName, int, flags, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Additional flags"), TRANSF)

DECLARE_NET_MESSAGE(AttachObject,ATTACHOBJECT_MSG)

#define DETACHOBJECT_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Object), obj, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Object to detach"), TRANSF_REF)

DECLARE_NET_MESSAGE(DetachObject,DETACHOBJECT_MSG)
#endif

#define MISSION_PARAMS_MSG(MessageName, XX) \
  XX(MessageName, float, param1, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter"), TRANSF) \
  XX(MessageName, float, param2, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter"), TRANSF) \
  XX(MessageName, AutoArray<float>, paramsArray, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Array of mission parameters"), TRANSF) \
  XX(MessageName, bool, updateOnly, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Change of params from dialog"), TRANSF)

DECLARE_NET_MESSAGE(MissionParams, MISSION_PARAMS_MSG)

#define ANIMATION_PHASE_MSG(MessageName, XX) \
  XX(MessageName, OLink(Entity), vehicle, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Animated vehicle"), TRANSF_REF) \
  XX(MessageName, RString, animation, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Animation ID"), TRANSF) \
  XX(MessageName, float, phase, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Animation state (phase)"), TRANSF)

DECLARE_NET_MESSAGE_INIT_MSG(AskForAnimationPhase,ANIMATION_PHASE_MSG)

#define VEHICLE_DAMAGED_MSG(MessageName, XX) \
  XX(MessageName, OLink(EntityAI), damaged, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Damaged entity"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(EntityAI), killer, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Entity, causing damage"), TRANSF_REF) \
  XX(MessageName, float, damage, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Amount of damage"), TRANSF) \
  XX(MessageName, RString, ammo, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Used ammunition type"), TRANSF)

DECLARE_NET_MESSAGE(VehicleDamaged, VEHICLE_DAMAGED_MSG)

#define SERVER_STATE_MSG(MessageName, XX) \
  XX(MessageName, int, serverState, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Server state"), TRANSF)

DECLARE_NET_MESSAGE(ServerState, SERVER_STATE_MSG)

#define CLIENT_STATE_MSG(MessageName, XX) \
  XX(MessageName, int, clientState, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Client state"), TRANSF)

DECLARE_NET_MESSAGE(ClientState, CLIENT_STATE_MSG)

#define PLAYER_CLIENT_STATE_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) id"), TRANSF) \
  XX(MessageName, int, clientState, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Client state"), TRANSF)

DECLARE_NET_MESSAGE(PlayerClientState, PLAYER_CLIENT_STATE_MSG)

#define APPLY_IDENTITY_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) id"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Person, which identity will be changed"), TRANSF_REF)

#define FORCE_DELETE_OBJECT_MSG(MessageName, XX) \
  XX(MessageName, OLinkNO(NetworkObject), object, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Object to destroy"), TRANSF_REF)

DECLARE_NET_MESSAGE(ForceDeleteObject, FORCE_DELETE_OBJECT_MSG)

#define MUTE_LIST_MSG(MessageName, XX) \
  XX(MessageName, XUID_ARRAY_TYPE, list, XUID_ARRAY_NET_TYPE, XUID_ARRAY_TYPE, NCTNone, XUID_ARRAY_DEF_VALUE, DOC_MSG("User ids"), TRANSF)

DECLARE_NET_MESSAGE(MuteList, MUTE_LIST_MSG)

#define REMOTE_MUTE_LIST_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<int>, list, NDTIntArray, AutoArray<int>, NCTNone, DEFVALUEINTARRAY, DOC_MSG("List of muting players"), TRANSF)

DECLARE_NET_MESSAGE(RemoteMuteList, REMOTE_MUTE_LIST_MSG)

#define VOICE_ON_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) id"), TRANSF) \
  XX(MessageName, bool, voiceOn, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Voice communicator is connected"), TRANSF)

DECLARE_NET_MESSAGE(VoiceOn, VOICE_ON_MSG)

#define CLEANUP_PLAYER_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) id"), TRANSF) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Id of player person"), TRANSF) \
  XX(MessageName, int, id, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Id of player person"), TRANSF)

DECLARE_NET_MESSAGE(CleanupPlayer, CLEANUP_PLAYER_MSG)

#define PROCESS_ACTION_MSG(MessageName, XX) \
  XX(MessageName, int, actionType, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Action type"), TRANSF) \
  XX(MessageName, OLink(Object), actionTarget, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Action target"), TRANSF_REF) \
  XX(MessageName, AutoArray<char>, actionParams, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Action parameters based on type"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), unit, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Person processing the action"), TRANSF_REF)

DECLARE_NET_MESSAGE(ProcessAction, PROCESS_ACTION_MSG)

#define PROCESS_RECIPE_MSG(MessageName, XX) \
  XX(MessageName, RString, recipe, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Recipe name"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Person executing the recipe"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item1, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Item involved in recipe"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item2, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Item involved in recipe"), TRANSF_REF) \

DECLARE_NET_MESSAGE(ProcessRecipe, PROCESS_RECIPE_MSG)

#define USE_ITEM_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), person, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Owner of the item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Item to use"), TRANSF_REF) \

DECLARE_NET_MESSAGE(UseItem, USE_ITEM_MSG)

#define CHAT_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), sender, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), TRANSF_REF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), TRANSF_REFS) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"), TRANSF) \
  XX(MessageName, RString, text, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Message content"), TRANSF)

DECLARE_NET_MESSAGE(Chat, CHAT_MSG)

#define RADIO_CHAT_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), sender, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), TRANSF_REF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), TRANSF_REFS) \
  XX(MessageName, RString, text, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Message content"), TRANSF) \
  XX(MessageName, RadioSentence, sentence, NDTSentence, RadioSentence, NCTNone, DEFVALUE(RadioSentence, RadioSentence()), DOC_MSG("Content of message (list of words to say)"), TRANSF) \
  XX(MessageName, RString, wordsClass, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Possible subclass in class words to define alternative set of words such as whisper"), TRANSF)

DECLARE_NET_MESSAGE(RadioChat, RADIO_CHAT_MSG)

#define RADIO_CHAT_WAVE_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), sender, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), TRANSF_REF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), TRANSF_REFS) \
  XX(MessageName, RString, senderName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"), TRANSF) \
  XX(MessageName, RString, wave, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound identifier"), TRANSF)

DECLARE_NET_MESSAGE(RadioChatWave, RADIO_CHAT_WAVE_MSG)

/** adapted from CHAT_MSG */
#define LOCALIZED_CHAT_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), sender, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), TRANSF_REF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), TRANSF_REFS) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"), TRANSF) \
  XX(MessageName, AutoArray<LocalizedString>, args, NDTLocalizedStringArray, AutoArray<LocalizedString>, NCTNone, DEFVALUELOCALIZEDSTRINGARRAY, DOC_MSG("Client-side localized text with arguments"), TRANSF) \

DECLARE_NET_MESSAGE(LocalizedChat,LOCALIZED_CHAT_MSG)

#define SET_SPEAKER_MSG(MessageName, XX) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID of speaking player"), TRANSF) \
  XX(MessageName, int, on, NDTBool, int, NCTNone, DEFVALUE(bool, false), DOC_MSG("Turn on / off direct speaking"), TRANSF) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of speaking unit"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("ID of speaking unit"), TRANSF)

DECLARE_NET_MESSAGE(SetSpeaker, SET_SPEAKER_MSG)

#define SELECT_PLAYER_MSG(MessageName, XX) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID of player"), TRANSF) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of player's unit"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("ID of player's unit"), TRANSF) \
  XX(MessageName, Vector3, position, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX)), DOC_MSG("Player's unit position"), TRANSF) \
  XX(MessageName, bool, respawn, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Selection of player's unit after respawn"), TRANSF)

DECLARE_NET_MESSAGE(SelectPlayer, SELECT_PLAYER_MSG)

#define CHANGE_OWNER_MSG(MessageName, XX) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of object, which owner is changing"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("ID of object, whic player is changing"), TRANSF) \
  XX(MessageName, int, owner, NDTInteger, int, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client ID of new owner"), TRANSF) \
  XX(MessageName, bool, disconnected, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Disconnected player becomes AI"), TRANSF)

DECLARE_NET_MESSAGE(ChangeOwner, CHANGE_OWNER_MSG)

#define OWNER_CHANGED_MSG(MessageName, XX) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of object, which owner was changed"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("ID of object, whic player is changing"), TRANSF)

DECLARE_NET_MESSAGE(OwnerChanged, OWNER_CHANGED_MSG)

#define PLAY_SOUND_MSG(MessageName, XX) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound identifier"), TRANSF) \
  XX(MessageName, Vector3, position, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Sound source position"), TRANSF) \
  XX(MessageName, Vector3, speed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Sound source speed"), TRANSF) \
  XX(MessageName, float, volume, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Sound volume"), TRANSF) \
  XX(MessageName, float, frequency, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Sound pitch"), TRANSF) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique network ID of sound"), TRANSF) \
  XX(MessageName, int, soundId, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique network ID of sound"), TRANSF)

DECLARE_NET_MESSAGE(PlaySound, PLAY_SOUND_MSG)

#define SOUND_STATE_MSG(MessageName, XX) \
  XX(MessageName, int, state, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("New state of sound"), TRANSF) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Network ID of sound"), TRANSF) \
  XX(MessageName, int, soundId, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Network ID of sound"), TRANSF)

DECLARE_NET_MESSAGE(SoundState, SOUND_STATE_MSG)

#define DELETE_OBJECT_MSG(MessageName, XX) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Id of object to destroy"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("Id of object to destroy"), TRANSF)

DECLARE_NET_MESSAGE(DeleteObject, DELETE_OBJECT_MSG)

#define DELETED_OBJECT_MSG(MessageName, XX) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Id of object destroyed"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("Id of object destroyed"), TRANSF)

DECLARE_NET_MESSAGE(DeletedObject, DELETED_OBJECT_MSG)

#define ASK_FOR_PILOT_LIGHT_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAI), vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Force switch on/off pilot light"), TRANSF_REF) \
  XX(MessageName, bool, enable, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG(""), TRANSF)

DECLARE_NET_MESSAGE(AskForPilotLight, ASK_FOR_PILOT_LIGHT_MSG)

#define ASK_FOR_VEHICLE_DOOR_STATE_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Transport), vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Open or close vehicle door"), TRANSF_REF) \
  XX(MessageName, int, doorIndex, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, -1), DOC_MSG("Index of door"), TRANSF) \
  XX(MessageName, bool, opened, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG(""), TRANSF)

DECLARE_NET_MESSAGE(AskForVehicleDoorState, ASK_FOR_VEHICLE_DOOR_STATE_MSG)

#define ASK_FOR_AMMO_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAIFull), vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which ammo is changing"), TRANSF_REF) \
  XX(MessageName, OLinkPermNO(Turret), turret, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Turret which ammo is changing"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(Weapon), weapon, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Weapon"), TRANSF_REF) \
  XX(MessageName, int, muzzle, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Muzzle index"), TRANSF) \
  XX(MessageName, int, mode, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Mode index"), TRANSF) \
  XX(MessageName, int, burst, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Amount of ammo to decrease"), TRANSF)

DECLARE_NET_MESSAGE(AskForAmmo, ASK_FOR_AMMO_MSG)

#define ASK_FOR_MOVE_VECTOR_MSG(MessageName, XX) \
  XX(MessageName, OLinkPerm<Object>, vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Moving object"), TRANSF_REF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("New position"), TRANSF)

DECLARE_NET_MESSAGE(AskForMoveVector, ASK_FOR_MOVE_VECTOR_MSG)

#define ASK_FOR_MOVE_MATRIX_MSG(MessageName, XX) \
  XX(MessageName, OLinkPerm<Object>, vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Moving object"), TRANSF_REF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("New position"), TRANSF) \
  XX(MessageName, Matrix3, orient, NDTMatrix, Matrix3, NCTNone, DEFVALUE(Matrix3, M3Identity), DOC_MSG("New orientation"), TRANSF)

DECLARE_NET_MESSAGE(AskForMoveMatrix, ASK_FOR_MOVE_MATRIX_MSG)

#define ASK_FOR_SET_MAGAZINE_AMMO_MSG(MessageName, XX) \
  XX(MessageName, OLinkPerm<Object>, magazine, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Magazine object"), TRANSF_REF) \
  XX(MessageName, int, ammo, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("New amount of ammo"), TRANSF)

DECLARE_NET_MESSAGE(AskForSetMagazineAmmo, ASK_FOR_SET_MAGAZINE_AMMO_MSG)

/*
void ExplosionDamage(EntityAI *owner, Shot *shot, Object *directHit, Vector3Par directHitModelPos, HitInfoPar hitInfo, Object *componentOwner=NULL, int componentIndex=-1, float energyFactor=1.0f, float explosionFactor=0.3f);
void ExplosionDamageEffects(EntityAI *owner, Object *directHit, HitInfoPar hitInfo, int componentIndex, bool enemyDamage, float energyFactor, float explosionFactor);
*/

#define EXPLOSION_DAMAGE_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAI), owner, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Shot owner (who is responsible for explosion)"), TRANSF_REF) \
  XX(MessageName, OLink(Object), directHit, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Hit object"), TRANSF_REF) \
  XX(MessageName, Vector3, originDir, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("World space origin direction relative to the target"), TRANSF) /*TODO: replace with originPos*/ \
  XX(MessageName, Vector3, directHitModelPos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Explosion position in directHit model coordinates"), TRANSF) \
  XX(MessageName, OLink(Object), componentOwner, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Hit object - may be proxy, owner of componentIndex"), TRANSF_REF) \
  XX(MessageName, int, componentIndex, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Index of hit convex component"), TRANSF) \
  XX(MessageName, RString, type, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Ammunition type"), TRANSF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Explosion position"), TRANSF) \
  XX(MessageName, Vector3, surfNormal, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VUp), DOC_MSG("Surface normal at the explosion"), TRANSF) \
  XX(MessageName, Vector3, inSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Speed of the corresponding bullet/shell/missile"), TRANSF) \
  XX(MessageName, Vector3, outSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Result speed (if any)"), TRANSF) \
  XX(MessageName, float, energyFactor, NDTFloat, float, NCTFloatMostly0To1, DEFVALUE(float, 1), DOC_MSG("Energy coefficient for distant hits"), TRANSF) \
  XX(MessageName, float, explosionFactor, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("How large explosion is"), TRANSF)\
  XX(MessageName, float, hitDepth, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("The penetration depth of the hit"), TRANSF)

DECLARE_NET_MESSAGE(ExplosionDamage, EXPLOSION_DAMAGE_MSG)

#define EXPLOSION_DAMAGE_EFFECTS_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAI), owner, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Shot owner (who is responsible for explosion)"), TRANSF_REF) \
  XX(MessageName, OLink(Object), directHit, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Hit object"), TRANSF_REF) \
  XX(MessageName, int, componentIndex, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Index of hit convex component"), TRANSF) \
  XX(MessageName, RString, type, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Ammunition type"), TRANSF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Explosion position"), TRANSF) \
  XX(MessageName, Vector3, surfNormal, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VUp), DOC_MSG("Surface normal at the explosion"), TRANSF) \
  XX(MessageName, Vector3, inSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Speed of the corresponding bullet/shell/missile"), TRANSF) \
  XX(MessageName, Vector3, outSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Result speed (if any)"), TRANSF) \
  XX(MessageName, bool, enemyDamage, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Some enemy was damaged"), TRANSF) \
  XX(MessageName, float, energyFactor, NDTFloat, float, NCTFloatMostly0To1, DEFVALUE(float, 1), DOC_MSG("Energy coefficient for distant hits"), TRANSF) \
  XX(MessageName, float, explosionFactor, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("How large explosion is"), TRANSF)

DECLARE_NET_MESSAGE(ExplosionDamageEffects, EXPLOSION_DAMAGE_EFFECTS_MSG)

#define PROJECTILE_DECAL_MSG(MessageName, XX) \
  XX(MessageName, OLink(Object), directHit, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Hit object"), TRANSF_REF) \
  XX(MessageName, int, componentIndex, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Index of hit convex component"), TRANSF) \
  XX(MessageName, RString, ammoType, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Ammunition type"), TRANSF) \
  XX(MessageName, Vector3, hitPosWorld, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Explosion position in world space"), TRANSF) \
  XX(MessageName, Vector3, hitPosLcl, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Hit position on the body (model space)"), TRANSF) \
  XX(MessageName, Vector3, surfNormal, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VUp), DOC_MSG("Surface normal at the hit spot"), TRANSF) \
  XX(MessageName, Vector3, inSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Speed of the corresponding bullet/shell/missile"), TRANSF) \
  XX(MessageName, Vector3, outSpeed, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Result speed (if any)"), TRANSF) \
  XX(MessageName, Vector3, hitDirLcl, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Forward direction of the projectile (model space)"), TRANSF) \
  XX(MessageName, Vector3, hitDirUpLcl, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VUp), DOC_MSG("Up direction of the projectile (model space)"), TRANSF) \
  XX(MessageName, float, hitDepth, NDTFloat, float, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("The penetration depth of the projectile"), TRANSF)
  
DECLARE_NET_MESSAGE(ProjectileDecal, PROJECTILE_DECAL_MSG)

#define REM_PROJECTILE_DECALS_MSG(MessageName, XX) \
  XX(MessageName, OLink(Object), person, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Person to remove projectiles from"), TRANSF_REF) \
  
DECLARE_NET_MESSAGE(RemoveProjectileDecals, REM_PROJECTILE_DECALS_MSG)

#define BOW_STATE_MSG(MessageName, XX) \
  XX(MessageName, OLink(Object), person, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Person using the bow"), TRANSF_REF) \
  XX(MessageName, int, state, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Updated bow state"), TRANSF) \

DECLARE_NET_MESSAGE(BowState, BOW_STATE_MSG)

#if _VBS3

// Transmit the projectile leaving the weapon
#define FIRE_WEAPON_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAIFull), vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Firing vehicle"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(Person), gunner, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Firing gunner"), TRANSF_REF) \
  XX(MessageName, OLink(EntityAI), target, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Aimed target"), TRANSF_REF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Position of the fire"), TRANSF) \
  XX(MessageName, Vector3, dir, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Direction of the fire"), TRANSF) \
  XX(MessageName, int, weapon, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Firing weapon index"), TRANSF) \
  XX(MessageName, CreatorId, magazineCreator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Fired magazine id"), TRANSF) \
  XX(MessageName, int, magazineId, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Fired magazine id"), TRANSF) \
  XX(MessageName, float, visible, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("How much is the fire visible"), TRANSF) \
  XX(MessageName, OLink(Entity), projectile, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Projectile leaving weapon"), TRANSF_REF)

#else

#define FIRE_WEAPON_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(EntityAIFull), vehicle, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Firing vehicle"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(Person), gunner, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Firing gunner"), TRANSF_REF) \
  XX(MessageName, OLink(EntityAI), target, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Aimed target"), TRANSF_REF) \
  XX(MessageName, Vector3, pos, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Position of the fire"), TRANSF) \
  XX(MessageName, Vector3, dir, NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Direction of the fire"), TRANSF) \
  XX(MessageName, OLinkPermO(Weapon), weapon, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Weapon"), TRANSF_REF) \
  XX(MessageName, int, muzzle, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Muzzle index"), TRANSF) \
  XX(MessageName, int, mode, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Mode index"), TRANSF) \
  XX(MessageName, OLinkPermO(Magazine), magazine, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Fired magazine."), TRANSF_REF) \
  XX(MessageName, float, visible, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("How much is the fire visible"), TRANSF)

#endif

DECLARE_NET_MESSAGE(FireWeapon, FIRE_WEAPON_MSG)

#define VOTE_MISSION_MSG(MessageName, XX) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE_DEF(RString), DOC_MSG("Name of the mission"), TRANSF) \
  XX(MessageName, RString, diff, NDTString, RString, NCTNone, DEFVALUE_DEF(RString), DOC_MSG("Wanted difficulty"), TRANSF) \

DECLARE_NET_MESSAGE(VoteMission,VOTE_MISSION_MSG)

#define SERVER_TIMEOUT_MSG(MessageName, XX) \
  XX(MessageName, int, timeout, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Timeout in seconds"), TRANSF) \

DECLARE_NET_MESSAGE(ServerTimeout,SERVER_TIMEOUT_MSG)

#define SERVER_INFO_MSG(MessageName, XX) \
  XX(MessageName, RString, hostname, NDTString, RString, NCTNone, DEFVALUE_DEF(RString), DOC_MSG("Server Hostname"), TRANSF) \

DECLARE_NET_MESSAGE(ServerInfo,SERVER_INFO_MSG)

#define VEHICLE_DESTROYED_MSG(MessageName, XX) \
  XX(MessageName, OLinkO(EntityAI), killed, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Destroyed vehicle"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(EntityAI), killer, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Who is responsible for destroying"), TRANSF_REF)

DECLARE_NET_MESSAGE(VehicleDestroyed, VEHICLE_DESTROYED_MSG)

#define MARKER_DELETE_MSG(MessageName, XX) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of marker"), TRANSF)

DECLARE_NET_MESSAGE(MarkerDelete, MARKER_DELETE_MSG)

struct NetworkMessageMarker;
struct NetworkMessageEffects;

#define MARKER_CREATE_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Chat channel (who will see the marker)"), TRANSF) \
  XX(MessageName, OLinkPermO(Person), sender, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), TRANSF_REF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), TRANSF_REFS) \
  XX(MessageName, ArcadeMarkerInfo, marker, NDTObject, REF_MSG(NetworkMessageMarker), NCTNone, DEFVALUE_MSG(NMTMarker), DOC_MSG("Marker object to create"), TRANSF_OBJECT)

DECLARE_NET_MESSAGE(MarkerCreate, MARKER_CREATE_MSG)

#if _ENABLE_VBS

#define SET_OBJECT_TEXTURE_MSG(MessageName, XX) \
  XX(MessageName, OLink(EntityAI), veh, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Vehicle that will have a texture changed"), TRANSF_REF) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, -1), DOC_MSG("Index of selection to set texture for"), TRANSF) \
  XX(MessageName, RString, name, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Name of texture"), TRANSF) \
  XX(MessageName, RString, eval, NDTString, RString, NCTDefault, DEFVALUE(RString, "true"), DOC_MSG("Evaluation - must evaluate to true if texture is to be set on client"), TRANSF)

DECLARE_NET_MESSAGE(SetObjectTexture, SET_OBJECT_TEXTURE_MSG)

#define PUBLICEXEC_MSG(MessageName, XX) \
  XX(MessageName, RString, condition, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Condition to evaluate, if true then command will be executed"), TRANSF) \
  XX(MessageName, RString, command, NDTString, RString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Command to execute if evaluation is true"), TRANSF) \
  XX(MessageName, OLink(Object), obj, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Object passed into condition and command as _this"), TRANSF_REF) \
  XX(MessageName, OLinkArray(Object), objs, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Joining units"), TRANSF_REFS)

DECLARE_NET_MESSAGE(PublicExec, PUBLICEXEC_MSG)

#endif // _ENABLE_VBS

#define TRANSFER_FILE_MSG(MessageName, XX) \
  XX(MessageName, RString, path, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Path of transferred file"), TRANSF) \
  XX(MessageName, AutoArray<char>, data, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of file (single segment)"), TRANSF) \
  XX(MessageName, int, totSize, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Total size of file"), TRANSF) \
  XX(MessageName, int, offset, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Offset of this segment"), TRANSF) \
  XX(MessageName, int, totSegments, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Total number of segments"), TRANSF) \
  XX(MessageName, int, curSegment, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Index of this segment"), TRANSF)

DECLARE_NET_MESSAGE(TransferFile, TRANSFER_FILE_MSG)

#define SUBSCRIBE_RADIOS_MSG(MessageName, XX) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID of player"), TRANSF) \
  XX(MessageName, AutoArray<float>, frequences, NDTFloatArray, FindArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("List of subscribed frequences"), TRANSF)

DECLARE_NET_MESSAGE(SubscribeRadios, SUBSCRIBE_RADIOS_MSG)

#define STATUS_CHAT_MSG(MessageName, XX) \
  XX(MessageName, RString, text,  NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Status text to be shown on client"), TRANSF) \
  XX(MessageName, RString, color, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Color class to use when drawing the text"), TRANSF) \

DECLARE_NET_MESSAGE(StatusChat, STATUS_CHAT_MSG)

//! Message for transfer mission pbo file to other clients
struct TransferMissionFileMessage : public TransferFileMessage
{
  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(TransferMissionFile)
};

#define TRANSFER_MISSION_FILE_MSG(MessageName, XX)

DECLARE_NET_INDICES_EX(TransferMissionFile, TransferFile, TRANSFER_MISSION_FILE_MSG)

//! Message for transfer file to server
struct TransferFileToServerMessage : public TransferFileMessage
{
  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(TransferFileToServer)
};

#define TRANSFER_FILE_TO_SERVER_MSG(MessageName, XX)

DECLARE_NET_INDICES_EX(TransferFileToServer, TransferFile, TRANSFER_FILE_TO_SERVER_MSG)

#define ASK_MISSION_FILE_MSG(MessageName, XX) \
  XX(MessageName, bool, valid, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Mission file is valid (present on client computer)"), TRANSF)

DECLARE_NET_MESSAGE(AskMissionFile, ASK_MISSION_FILE_MSG)

//! Single message in agregated message
struct NetworkMessageQueueItem : public RefCount
{
  //! message type
  NetworkMessageType type;
  //! message itself
  Ref<NetworkMessage> msg;
  //! guaranteed msg this message depends on
  RefR<NetMessage, RefCountSafe> dependOn;

  virtual void SetFence(const RefR<NetMessage, RefCountSafe> &resultMsg) = 0;

  //! MP Save serialization
  LSError Serialize(ParamArchive &ar)
  {
    CHECK(ar.Serialize("Message", msg, 1))
    if (ar.IsLoading()) type = msg->GetNMType();
    return LSOK;      
  }
};

struct ClientQueueItem : public NetworkMessageQueueItem
{
  Ref<NetworkObject> object;
  
  virtual void SetFence(const RefR<NetMessage, RefCountSafe> &resultMsg)
  {
    if (object) object->_fence = resultMsg;
  }
};

struct ServerQueueItem : public NetworkMessageQueueItem
{
  RefR<NetworkPlayerObjectInfo> poInfo;

  virtual void SetFence(const RefR<NetMessage, RefCountSafe> &resultMsg);
};
TypeIsMovable(ServerQueueItem)

//! Agregated message
/*!
We want NMTMessaes to be created. This class is not used, it only allows the message factory to compile.
*/
class NetworkMessageQueue : public NetworkSimpleObject, public RefArray<NetworkMessageQueueItem>
{
public:
  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(Messages)
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);
};

//! Encapsulates info needed for messages sent directly (inside process) instead through DirectPlay
struct NetworkLocalMessageInfo
{
  //! DirectPlay ID of sender
  int from;
  //! message type
  NetworkMessageType type;
  //! message itself
  Ref<NetworkMessage> msg;
};
TypeIsMovableZeroed(NetworkLocalMessageInfo)

///////////////////////////////////////////////////////////////////////////////
// Network Components

class NetworkManager;

//! Info about file currently receiving
struct ReceivingFile
{
  //! name of receiving file
  RString fileName;
  //! info about which segments already received
  AutoArray<bool> fileSegments;
  //! content of receiving file
  AutoArray<char> fileData;
  //! total amount of bytes already received (used for statistics)
  int received;
};
TypeIsMovable(ReceivingFile)

#if _ENABLE_CHEATS
//! Statistic info about messages of given type
/*!
\patch_internal 1.08 Date 07/26/2001 by Jirka
- Improved: network diagnostics
*/
struct NetworkStatisticsItem
{
  //! message type
  int message;
  //! count of sent messages (by priority)
  int msgSentHighPriority;
  int msgSentGuaranteed;
  int msgSentOthers;
  //! size of sent messages
  int sizeSent;
  //! count of received messages
  int msgReceived;
  //! size of received messages
  int sizeReceived;

  //! Constructor
  NetworkStatisticsItem()
  {
    message = 0;
    msgSentHighPriority = 0;
    msgSentGuaranteed = 0;
    msgSentOthers = 0;
    sizeSent = 0;
    msgReceived = 0;
    sizeReceived = 0;
  };
};
TypeIsMovableZeroed(NetworkStatisticsItem)
#endif

#if _ENABLE_CHEATS || _ENABLE_DEDICATED_SERVER
//! Statistic info about messages physically send / received by players
struct NetworkRawStatisticsItem
{
  //! player ID
  int player;
  //! count of sent messages
  int msgSent;
  //! size of sent messages
  int sizeSent;
  //! count of received messages
  int msgReceived;
  //! size of received messages
  int sizeReceived;

  //! Constructor
  NetworkRawStatisticsItem()
  {
    player = 0;
    msgSent = 0;
    sizeSent = 0;
    msgReceived = 0;
    sizeReceived = 0;
  };
};
TypeIsMovableZeroed(NetworkRawStatisticsItem)


/// draw fps history
template <int sets>
class PerformanceGraph: public IDiagGraph
{
  public:
  struct Values
  {
    float values[sets];
  };

  private:
  static const int maxSamples = 200;
  Values value[maxSamples];
  float deltaT[maxSamples];
  
  public:

  RString GetName() const {return "Bandwidth";}
  PackedColor GetDataColor(int set) const
  {
    static const PackedColor colors[]=
    {
      PackedColor(0xffffffff),
      PackedColor(0xffffff00),
      PackedColor(0xff00ff00),
      PackedColor(0xff0000ff),
      PackedColor(0xffff0000),
      PackedColor(0xffffff00),
    };
    if (set>=lenof(colors)) return PackedWhite;
    return colors[set];
  }

  PerformanceGraph()
  {
    for (int i=0; i<maxSamples; i++)
    {
      for (int s=0; s<sets; s++) value[i].values[s]=0;
    }
    for (int i=0; i<maxSamples; i++) deltaT[i]=0;
  }
  int GetDataSetCount() const {return sets;}
  int GetDataSetSize(int set) const {return maxSamples;}
  void GetDataSetItem(int set, int i, float &dx, float &y) const
  {
    dx = deltaT[i];
    y = value[i].values[set];
  }
  float GetMaxTime() const
  {
    float time = 0;
    for (int i=0; i<maxSamples; i++) time += deltaT[i];
    return time;
  }
  void AddSample(Values v, float dT)
  {
    memmove(value+1,value,sizeof(value)-sizeof(value[0]));
    memmove(deltaT+1,deltaT,sizeof(deltaT)-sizeof(deltaT[0]));
    value[0] = v;
    deltaT[0] = dT;
  }
  bool ValidForFPSMode( int fpsMode ) const
  {
    return true;
  }

};

class NetStatsGraph: public PerformanceGraph<2>
{
  DWORD _lastTime;
  int _lastSize;
  int _lastMsg;

  public:
  NetStatsGraph();

  void OnSimulate(int size, int msg);
};

#endif

class NetworkClient;

class RadioSubscriber
{
public:
  AutoArray<float> frequencies;
  int dpnid;

  RadioSubscriber() : dpnid(0) {}
  RadioSubscriber(int dp) : dpnid(dp) {}
  bool FrequenciesMatch(float f1, float f2) const;
};

TypeIsMovable(RadioSubscriber)

class RadioSubscribers
{
public:
  AutoArray<RadioSubscriber> _subscribers;

  RadioSubscribers() {}
  
  void OnPlayerDisconnected(int dpnid); // clear given subscriber
  void Clear() { _subscribers.Clear(); }
  void AddUnique(int dpnid, AutoArray<float> &frequences);
};

//! Basic network component (client or server)
class NetworkComponent : public INetworkComponent
{
protected:
  //! creating network manager object
  NetworkManager *_parent;  // _parent must be valid
  //! id of next created network object in this component
  int _nextId;

  //! info about logged players
  AutoArray<PlayerIdentity> _identities;

  //! info about current mission
  MissionHeader _missionHeader;
  //! parameter of current mission
  float _param1;
  //! parameter of current mission
  float _param2;
  //! array of parameters of current mission
  AutoArray<float> _paramsArray;
  
  //! messages received local (inside process)
  AutoArray<NetworkLocalMessageInfo> _receivedLocalMessages;

  //! files currently receiving
  AutoArray<ReceivingFile> _files;
  
#if _ENABLE_CHEATS
  //! Statistics about messages by types
  AutoArray<NetworkStatisticsItem> _statistics;
#endif

#if _ENABLE_DEDICATED_SERVER || _ENABLE_CHEATS
  //! Statistics about messages by players
  AutoArray<NetworkRawStatisticsItem> _rawStatistics;
  RefArray<NetStatsGraph> _rawStatisticsGraph;
#endif

  //! list of accepted keys for data signatures
  AutoArray<DSKey> _acceptedKeys;
  //! the least signature version the server accepts
  int              _sigVerRequired;

  /// choose implementation: with guaranteed updates we can make interesting optimizations (delta transfer)
  bool _updatesGuaranteed;

  static const int MaxVerifySignatures = 2;

  //! list of files (except banks), where signature will be checked
  AutoArray<RString> _additionalSignedFiles;

  //! map players to their subscribed radio frequencies
  RadioSubscribers _radioSubscribers;

public:
  //! Constructor
  /*!
  \param parent creating network manager object
  */
  NetworkComponent(NetworkManager *parent);
  //! Destructor
  ~NetworkComponent();

  //! Add message to local received messages
  /*!
  \param from DirectPlay ID of sender
  \param type message type
  \param msg message itself
  */
  void AddLocalMessage(int from, NetworkMessageType type, NetworkMessage *msg);

  //! Implementation of direct message send through DirectPlay
  /*!
  \param to DirectPlay ID of message receiver
  \param rawMsg message content
  \param msgID out parameter to store DirectPlay ID of message
  \param dwFlags DirectPlay flags
  */
  virtual Ref<NetMessage> DXSendMsg(int to, NetworkMessageRaw &rawMsg, DWORD &msgID, NetMsgFlags dwFlags, const Ref<NetMessage> &dependOn) = 0;

  //! Draw any UI indications you need
  virtual void OnDraw() = 0;
  //! Process (user) message
  /*!
  \param from DirectPlay ID of sender
  \param msg message itself
  \param type message type
  */
  virtual void OnMessage(int from, NetworkMessage *msg, NetworkMessageType type) = 0;

  //! perform regular memory clean-up
  virtual unsigned CleanUpMemory() = 0;

  //! Return if given client is local on server's computer
  virtual bool IsLocalClient(int client) const {return false;}
  
  bool IsUpdatesGuaranteed() const {return _updatesGuaranteed;}

  //{ DEDICATED SERVER SUPPORT
  #if _ENABLE_DEDICATED_SERVER
  //! Called when message is received to calculate transfer rate on dedicated server
  virtual void OnMonitorIn(int size) {}
  #endif
  //}

  //! Hi-level send of message
  /*!
  \param to DirectPlay ID of message receiver
  \param object message object
  \param dwFlags DirectPlay flags
  */
  virtual DWORD SendMsg
  (
    int to,
    NetworkSimpleObject *object, /*int creator, int id,*/
    NetMsgFlags dwFlags
  );

  //! Send single network message
  /*!
  This function determine, whic type of send will be used - local send, agregation or send using DirectPlay
  \param to DirectPlay ID of message receiver
  \param msg message itself
  \param type type of message
  \param dwFlags DirectPlay flags
  */
  virtual DWORD SendMsg
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, NetMsgFlags dwFlags
  ) = 0;

  //! Return mission header
  const MissionHeader *GetMissionHeader() const {return &_missionHeader;}
  //! Return number of role slots for current mission

  //! Search for identity with given player id
  const PlayerIdentity *FindIdentity(int dpnid) const;
  //! Search for identity with given player id
  PlayerIdentity *FindIdentity(int dpnid);
  //! Search for identity with given user friendly player id
  const PlayerIdentity *FindIdentityByPlayerId(int playerId) const;
  //! Return array of identities
  const AutoArray<PlayerIdentity> *GetIdentities() const {return &_identities;}
  //! Retrieves parameters for current mission
  void GetParams(float &param1, float &param2) const {param1 = _param1; param2 = _param2;}
  //! Retrieves parameters for current mission
  float GetParams(int index) const {
    if(index<_paramsArray.Size()) return _paramsArray[index];
    return 0.0;
  };

  //! Retrieves parameters for current mission
  AutoArray<float> GetParamsArray() const{return _paramsArray;};

  //! Return component type ("server" / "client") for debugging purposes
  virtual const char *GetDebugName() const = 0;

  //! Decode (and process) message from raw data
  /*!
  \param from DirectPlay ID of sender
  \param src source raw data
  */
  void DecodeMessage(int from, NetworkMessageRaw &src);
  //! Calculate size of message after serialization and compression
  /*!
  \param msg calculated message
  \param type type of calculated message
  */
  int CalculateMessageSize(NetworkMessage *msg, NetworkMessageType type);

  //! File transfer
  /*!
  Split file into segments and send segments using standard messages.
  \param to DirectPlay ID of file receiver
  \param dest filename on destination computer
  \param source local filename
  */
  void TransferFile(int to, RString dest, RString source);
  /*!
  Split file into segments and store them into msgList array
  \param to DirectPlay ID of file receiver
  \param dest filename on destination computer
  \param source local filename
  */
  void PrepareTransferFileMessages(AutoArray<TransferFileMessage> &msgList, RString dest, RString source);
  //! Transfer player's face from server to client
  /*!
  \param to DirectPlay ID of face receiver
  \param player player which face is transfered
  */
  void TransferFace(int to, RString player);
  //! Transfer player's custom radio messages from server to client
  /*!
  \param to DirectPlay ID of messages receiver
  \param player player which messages are transfered
  */
  void TransferCustomRadio(int to, RString player);

#if _ENABLE_CHEATS
  //! Write into statistics info about sent message
  /*!
  \param msg message type
  \param size size of sent message
  */
  void StatMsgSent(int msg, int size, NetMsgFlags flags);
  //! Write into statistics info about received message
  /*!
  \param msg message type
  \param size size of received message
  */
  void StatMsgReceived(int msg, int size);
  //! Return statistics about messages by types
  AutoArray<NetworkStatisticsItem> &GetStatistics() {return _statistics;}
#endif

#if _ENABLE_DEDICATED_SERVER || _ENABLE_CHEATS
  //! Write raw statistics info about sent message
  /*!
  \param msg message type
  \param to player
  */
  void StatRawMsgSent(int to, int size);
  //! Write raw statistics info about received message
  /*!
  \param msg message type
  \param from player
  */
  void StatRawMsgReceived(int from, int size);
#endif

  int GetMaxMissionPlayers() const {return _missionHeader._maxPlayers; }

protected:
  //! Encode message into raw data
  /*!
  \param dst destination data
  \param msg source message
  \param format format of source message
  \param type type of destination message
  */
  void EncodeMsg(NetworkMessageRaw &dst, NetworkMessage *msg, NetworkMessageFormatBase *format, NetworkMessageType type);
  //! Encode single message item into raw data
  /*!
  \param dst destination data
  \param msg source message
  \param ptr pointer to item to encode
  \param item format of source message item
  */
  void EncodeMsgItem(NetworkMessageRaw &dst, NetworkMessage *msg, void *ptr, NetworkMessageFormatItem &item);
  //! Initialize message before decoding
  /*!
  \param msg destination message
  \param type type of destination message
  */
  virtual void InitMsg(NetworkMessage *msg, NetworkMessageType type) {}
  //! Decode message from raw data
  /*!
  \param msg destination message
  \param src source data
  \param format format of destination message
  \param type type of destination message
  */
  bool DecodeMsg(NetworkMessage *msg, NetworkMessageRaw &src, NetworkMessageFormatBase *format, NetworkMessageType type);
  //! Decode single message item from raw data
  /*!
  \param src source data
  \param msg destination message
  \param ptr pointer to item to decode
  \param item format of source message item
  */
  bool DecodeMsgItem(NetworkMessageRaw &src, NetworkMessage *msg, void *ptr, NetworkMessageFormatItem &item);
  //! Create copy of message
  /*!
  \param src source message
  \param format format of source message
  */
  void CopyMsg(NetworkMessage *dst, const NetworkMessage *src, const NetworkMessageFormatBase *format) const;
  //! Calculate size of message after serialization and compression
  /*!
  \param msg calculated message
  \param format format of calculated message
  \param type type of calculated message
  */
  int CalculateMsgSize(NetworkMessage *msg, NetworkMessageFormatBase *format, NetworkMessageType type);
  //! Calculate size of single message item after serialization and compression
  /*!
  \param msg calculated message
  \param ptr calculated message item
  \param item format of calculated message item
  */
  int CalculateMsgItemSize(void *ptr, const NetworkMessageFormatItem &item);

  //! Process all received messages (local, system and user)
  void ReceiveLocalMessages();


  //! Send single network message using DirectPlay
  /*!
  \param to DirectPlay ID of message receiver
  \param msg message itself
  \param type type of message
  \param dwFlags DirectPlay flags
  */
  int SendMsgRemote
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, NetMsgFlags dwFlags,
    RefR<NetMessage, RefCountSafe> *resultMsg,
    const RefR<NetMessage, RefCountSafe> &dependOn
  );
  //! Send (part of) message queue as single DirectPlay message
  /*!
  \param to DirectPlay ID of message receiver
  \param queue messages to agregate
  \param begin index of first message to send
  \param end index of first message not to send
  \param dwFlags DirectPlay flags
  */
  int SendMsgQueue
  (
    int to, NetworkMessageQueue &queue,
    int begin, int end, NetMsgFlags dwFlags
  );
  //! Lo-level data send using DirectPlay
  /*!
  \param to DirectPlay ID of message receiver
  \param rawMsg data to send
  \param dwFlags DirectPlay flags
  \param diagLevel level of diagnostics about message
  */
  DWORD SendMsgRaw(int to, NetworkMessageRaw &rawMsg, NetMsgFlags dwFlags, int diagLevel, RefR<NetMessage, RefCountSafe> *resultMsg, const Ref<NetMessage> &dependOn);


  //! Called when file segment is received
  /*!
  \param msg segment of file
  \return 1 if whole file is received (and saved)
  \return -1 if whole file is received, but with errors (save failed)
  */
  int ReceiveFileSegment(TransferFileMessage &msg);

  //! verify the real file format matches the extension
  bool ValidDataFile(const char *fileName);

};

//! Info about single type of update of network object
struct NetworkUpdateInfo
{
  //! last sent update message
  Ref<NetworkMessage> lastSentMsg;
  //! cached value of error
  float errorStatic;
  //! cached value is valid
  bool errorValid;

  //! bot-client rejected state received from owner
  bool rejected;

  NetworkUpdateInfo()
  {
    // avoid floating point exceptions
    errorStatic = 0;
    errorValid = false;
    rejected = false;
  }

  bool CheckReject(NetworkClient *client, NetworkMessageClass cls, NetworkObject *obj);

};

//! Info about local object on client
struct NetworkLocalObjectInfo //: public RefCount
{
  //! ID of object
  NetworkId id;
  //! object itself
  OLinkNO(NetworkObject) object;
  //! state of outgoing updates
  NetworkUpdateInfo updates[NMCUpdateN];
};
TypeContainsOLink(NetworkLocalObjectInfo)
//TypeIsGeneric(NetworkLocalObjectInfo)

//! Info about remote object on client
struct NetworkRemoteObjectInfo
{
  //! ID of object
  NetworkId id;
  //! object itself
  OLinkNO(NetworkObject) object;
  //! state of incoming updates
  Ref<NetworkMessage> updates[NMCUpdateN];
  //! all objects are tracked here, but only some are really remote
  bool remote;
};
TypeContainsOLink(NetworkRemoteObjectInfo)

/// info about an object which was already deleted on the server
struct NetworkDeletedObjectInfo
{
  NetworkId id;
  /// time of deletion
  Time time;
  /// owner of the object at the time of deletion
  int owner;
};

TypeIsMovableZeroed(NetworkDeletedObjectInfo);


//! map sounds transferred over network to their unique ids
struct PlaySoundInfo
{
  //! unique id of sound
  CreatorId creator;
  int id;
  //! sound itself
  Link<AbstractWave> wave;
};
TypeIsGeneric(PlaySoundInfo)

//! Class used for transfer of client info (for example camera position) to server
/*!
\patch_internal 1.27 Date 10/11/2001 by Jirka
- Improved: camera position transfer in multiplayer game
*/
class ClientInfoObject : public NetworkObject
{
protected:
  NetworkId _id;

public:
  Vector3 _cameraPosition;
  int     _dpnid;   // Direct play id (necessary for hearing of VoN Direct Speech for dead players)

#if _VBS3
  Vector3 _cameraDirection;
  float   _cameraFov;
#endif

public:
  // Constructor
  ClientInfoObject()
  {
    _cameraPosition = InvalidCamPos;
#if _VBS3
    _cameraDirection = VForwardP;
    _cameraFov = 0.75;
#endif
  }

  void SetNetworkId(NetworkId &id) {_id = id;}
  NetworkId GetNetworkId() const {return _id;}
  Vector3 GetCurrentPosition() const {return VZero;}
  bool IsLocal() const {return true;}
  void SetLocal(bool local = true) {}
  void DestroyObject() {}
  
  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);
  float CalculateError(NetworkMessageContextWithError &ctx);
  virtual float CalculateErrorCoef(Vector3Par position, Vector3Par cameraPosition, float updateDistance, bool updating) const override {return 1.0;}
  RString GetDebugName() const;
};

#if !_VBS3
#define UPDATE_CLIENT_INFO_MSG(MessageName, XX) \
  XX(MessageName, Vector3, cameraPosition, NDTVector, Vector3, NCTVectorPositionCamera, DEFVALUE(Vector3, InvalidCamPos), DOC_MSG("Position of camera on client"), TRANSF, ET_ABS_DIF, 1)

DECLARE_NET_INDICES_EX_ERR(UpdateClientInfo, NetworkObject, UPDATE_CLIENT_INFO_MSG)
#endif

#define UPDATE_CLIENT_INFO_DPID_MSG(MessageName, XX) \
  XX(MessageName, Vector3, cameraPosition, NDTVector, Vector3, NCTVectorPositionCamera, DEFVALUE(Vector3, InvalidCamPos), DOC_MSG("Position of camera on client"), TRANSF, ET_ABS_DIF, 1) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Direct play id of dead player."), TRANSF, ET_NOT_EQUAL, 0)

DECLARE_NET_INDICES_EX_ERR(UpdateClientInfoDpid, NetworkObject, UPDATE_CLIENT_INFO_DPID_MSG)

#define UPDATE_ACTION_SUBSCRIPTION_MSG(MessageName, XX) \
  XX(MessageName, UserAction, action, NDTInteger, UserAction, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("action"), TRANSF) \
  XX(MessageName, bool, checkFocus, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("checkFocus"), TRANSF) \
  XX(MessageName, float, digitalValue, NDTFloat, float, NCTFloatMostly0To1, DEFVALUE(float, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, unsigned char, level, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("level"), TRANSF) \

DECLARE_SIMPLE_NETWORK_STRUCTURE(ActionSubscription,UPDATE_ACTION_SUBSCRIPTION_MSG)

TypeIsSimple(ActionSubscription)

#define UPDATE_ACTIONTODO_SUBSCRIPTION_MSG(MessageName, XX) \
  XX(MessageName, UserAction, action, NDTInteger, UserAction, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("action"), TRANSF) \
  XX(MessageName, bool, reset, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("reset"), TRANSF) \
  XX(MessageName, bool, checkFocus, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("checkFocus"), TRANSF) \
  XX(MessageName, bool, exclusive, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("exclusive"), TRANSF) \
  XX(MessageName, unsigned char, level, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("level"), TRANSF) \
  XX(MessageName, bool, blockButtons, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("blockButtons"), TRANSF) \

DECLARE_SIMPLE_NETWORK_STRUCTURE(ActionToDoSubscription,UPDATE_ACTIONTODO_SUBSCRIPTION_MSG)

TypeIsSimple(ActionToDoSubscription)

#define UPDATE_ACTIONDO_SUBSCRIPTION_MSG(MessageName, XX) \
  XX(MessageName, UserAction, action, NDTInteger, UserAction, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("action"), TRANSF) \
  XX(MessageName, bool, checkFocus, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("checkFocus"), TRANSF) \

DECLARE_SIMPLE_NETWORK_STRUCTURE(ActionDoSubscription,UPDATE_ACTIONDO_SUBSCRIPTION_MSG)

TypeIsSimple(ActionDoSubscription)

#define UPDATE_ITEM_SHORTCUT_MSG(MessageName, XX) \
  XX(MessageName, RString, itemType, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG(""), TRANSF)\

DECLARE_SIMPLE_NETWORK_STRUCTURE(ItemShortcut,UPDATE_ITEM_SHORTCUT_MSG)

TypeIsMovableZeroed(ItemShortcut)

#define UPDATE_ACTIONWITHCURVE_SUBSCRIPTION_MSG(MessageName, XX) \
  XX(MessageName, InputInterface::CurveFactor, factorKind, NDTInteger, InputInterface::CurveFactor, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, UserAction, posAction, NDTInteger, UserAction, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, UserAction, negAction, NDTInteger, UserAction, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG(""), TRANSF) \
  XX(MessageName, bool, checkFocus, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG(""), TRANSF) \
  XX(MessageName, bool, exclusive, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG(""), TRANSF) \

DECLARE_SIMPLE_NETWORK_STRUCTURE(ActionWithCurveSubscription,UPDATE_ACTIONWITHCURVE_SUBSCRIPTION_MSG)

TypeIsSimple(ActionWithCurveSubscription)

#define UPDATE_CLIENT_INPUT_ACTIONS(MessageName, XX) \
  XX(MessageName, AutoArray<float>, actionState, NDTFloatArray, AutoArray<float>, NCTFloat0To1, DEFVALUEFLOATARRAY, DOC_MSG("Input state"), TRANSF) \
  XX(MessageName, AutoArray<bool>, actionToDoState, NDTBoolArray, AutoArray<bool>, NCTNone, DEFVALUEBOOLARRAY, DOC_MSG("Input state"), TRANSF) \
  XX(MessageName, AutoArray<bool>, actionDoState, NDTBoolArray, AutoArray<bool>, NCTNone, DEFVALUEBOOLARRAY, DOC_MSG("Input state"), TRANSF) \
  XX(MessageName, AutoArray<float>, actionWithCurveState, NDTFloatArray, AutoArray<float>, NCTFloatMostly0To1, DEFVALUEFLOATARRAY, DOC_MSG("Input state"), TRANSF) \

#define UPDATE_CLIENT_INPUT_MSG(MessageName, XX) \
  UPDATE_CLIENT_INPUT_ACTIONS(MessageName, XX) \
  XX(MessageName, NetworkId, cursorObject, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Object under cursor"), TRANSF) \
  XX(MessageName, RString, cursorSurface, NDTString, RString, NCTNone, DEFVALUE(RString, ""),DOC_MSG("Surface under cursor"), TRANSF) \

DECLARE_NET_INDICES(UpdateClientInput, UPDATE_CLIENT_INPUT_MSG)


#define UPDATE_CLIENT_OUTPUT_MSG(MessageName, XX) \
  XX(MessageName, FindArrayKey<ActionSubscription>, subscribed, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateActionSubscription), NCTNone, DEFVALUE_MSG(NMTUpdateActionSubscription), DOC_MSG("Output state"), TRANSF_ARRAY) \
  XX(MessageName, FindArrayKey<ActionToDoSubscription>, subscribedToDo, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateActionToDoSubscription), NCTNone, DEFVALUE_MSG(NMTUpdateActionToDoSubscription), DOC_MSG("Output state"), TRANSF_ARRAY) \
  XX(MessageName, FindArrayKey<ActionDoSubscription>, subscribedDo, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateActionDoSubscription), NCTNone, DEFVALUE_MSG(NMTUpdateActionDoSubscription), DOC_MSG("Output state"), TRANSF_ARRAY) \
  XX(MessageName, FindArrayKey<ActionWithCurveSubscription>, subscribedWithCurve, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateActionDoSubscription), NCTNone, DEFVALUE_MSG(NMTUpdateActionWithCurveSubscription), DOC_MSG("Output state"), TRANSF_ARRAY) \
  XX(MessageName, AutoArray<ItemShortcut>, shortcuts, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateItemShortcut), NCTNone, DEFVALUE_MSG(NMTUpdateItemShortcut), DOC_MSG("Output state"), TRANSF_ARRAY) \
  XX(MessageName, AutoArray<UIAction>, myActions, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateUIAction), NCTNone, DEFVALUE_MSG(NMTUpdateUIAction), DOC_MSG("Output state"), TRANSF_ARRAY) \
  XX(MessageName, AutoArray<UIAction>, targetActions, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageUpdateUIAction), NCTNone, DEFVALUE_MSG(NMTUpdateUIAction), DOC_MSG("Output state"), TRANSF_ARRAY) \

DECLARE_NET_INDICES(UpdateClientOutput, UPDATE_CLIENT_OUTPUT_MSG)


// def. message
#define MEMBER_MUTABLE_DEF(MessageName,type,name,datatype,rawType, compres, defval, description, xfer) \
  mutable type _##name;

// def. message
#define MEMBER_DEF(MessageName,type,name,datatype,rawType, compres, defval, description, xfer) \
  type _##name;

/// transfer output state from server to client
/** included input settings as well */
class ClientOutput: public NetworkSimpleObject
{
public:
  UPDATE_CLIENT_OUTPUT_MSG(dummy, MEMBER_DEF)

public:
  ClientOutput();

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(UpdateClientOutput)

  static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
  TMError TransferMsg(NetworkMessageContext &ctx) override;
  void OnSent();
  void OnBeforeSend(const NetworkPlayerInfo &info);

  bool Compare(const ClientOutput *with) const;
};


#define MEMBER_USED_MUTABLE_DEF(MessageName,type,name,datatype,rawType, compres, defval, description, xfer) \
  mutable AutoArray<bool> _##name##Used;

/// transfer input state from client to server
class ClientInputState
{
  protected:
  UPDATE_CLIENT_INPUT_MSG(dummy, MEMBER_MUTABLE_DEF)

  public:
  bool Compare(const ClientInputState *with) const;
  const NetworkId &GetCursorObject() const {return _cursorObject;}
  const RString &GetCursorSurface() const {return _cursorSurface;}
};

class ClientInput: public NetworkSimpleObject, public InputInterface, public ClientInputState, private NoCopy
{
protected:
  bool _local;

  UPDATE_CLIENT_INPUT_ACTIONS(dummy, MEMBER_USED_MUTABLE_DEF)

public:
  // TODO: merge objects instead
  SRef<ClientOutput> _output;
  /// last state of the output sent out
  SRef<ClientOutput> _outputLast;

public:
  ClientInput(bool local);

  // Only ClientInputState is transferred, but it is not NetworkSimpleObject (we want to keep it small)
  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(UpdateClientInput)
  static NetworkMessageFormat &CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format);
  TMError TransferMsg(NetworkMessageContext &ctx) override;

  virtual float GetAction(UserAction action, bool checkFocus = true, float digitalValue=1.0f, unsigned char level = 1) const override;
  virtual bool GetActionToDo(UserAction action, bool reset = true, bool checkFocus = true, bool exclusive = false, unsigned char level = 1, bool blockButtons = false) override;
  virtual bool GetActionDo(UserAction action, bool checkFocus=true) override;
  virtual float GetActionWithCurve(CurveFactor factorKind, UserAction posAction, UserAction negAction, bool checkFocus = true, bool exclusive = false) const override;
};

class ClientInputLocal: public ClientInput
{

  public:

  ClientInputLocal():ClientInput(true){}

  void ScanInput();
  void OnOutputReceived(const ClientOutput &output);
};

struct UpdateLocalObjectInfo;

struct BodyInfo
{
  OLinkPermO(Person) body;
  Time hideTime;
  float value;
};
TypeContainsOLink(BodyInfo)

struct WreckInfo
{
  OLinkPermO(Transport) wreck;
  Time hideTime;
};
TypeContainsOLink(WreckInfo)

class NetworkServer;
void DiagLogF(const char *format, ...);

/// store event handler executed in reaction to PublicVariableMessage
struct PublicVariableEventHandler
{
  RString _name;
#if USE_PRECOMPILATION
  Ref<GameDataCode> _code;
#else
  RString _code;
#endif

  const char *GetKey() const {return _name;}
  LSError Serialize(ParamArchive &ar);
};
TypeIsMovableZeroed(PublicVariableEventHandler);

template <>
struct MapClassTraits<PublicVariableEventHandler> : public MapClassTraitsNoCase<PublicVariableEventHandler>
{
};

/// set of all registered event handlers executed in reaction to PublicVariableMessage
typedef MapStringToClass< PublicVariableEventHandler, AutoArray<PublicVariableEventHandler> > PublicVariableEventHandlers;

//! Network Command Types
/*!
\patch_internal 1.11 Date 07/31/2001 by Jirka
- Added: processing of commands entered in chat line
*/
enum NetworkCommandType
{
  CMDNone = -1,
  CMDLogin,
  CMDLogout,
  CMDKick,
  CMDRestart,
  CMDMission,
  CMDMissions,
  CMDShutdown,
  CMDReassign,
  CMDMonitor,
  CMDUserlist,
  CMDVote,
  CMDAdmin,
  CMDInit,
  CMDDebug,
  CMDExec,
  CMDLock,
  CMDUnlock,
  CMDBEServer,
  CMDBEClient,
  CMDProfile,
#if _ENABLE_CHEATS
  CMDGameExec,
#endif
#if _VBS3
  ,CMDExitAll
#endif
};

struct CheckSignatureItem
{
  int _bankIndex;
  int _sigVer;
  int _level;
  bool _initialTest;
  //! network message type and the prepared message itself (NMTFileSignatureAnswer or NMTDataSignatureAnswer)
  int _type;
  NetworkSimpleObject *_msg;
  CheckSignatureItem() : _msg(NULL) {}
  ~CheckSignatureItem() 
  { 
    if(_msg!=NULL) 
    { 
       delete _msg; 
      _msg=NULL; 
    } 
  }
};
TypeIsMovable(CheckSignatureItem)

template <>
struct FindArrayKeyTraits<CheckSignatureItem>
{
  typedef int KeyType;
  static bool IsEqual(int a, int b) {return a == b;}
  static int GetKey(const CheckSignatureItem &a) {return a._bankIndex;}
};

template <>
struct ImplicitMapTraits<NetworkLocalObjectInfo>
{
  static NetworkLocalObjectInfo null;                 ///< Empty value (can be passed as return value).
  static NetworkLocalObjectInfo zombie;               ///< Removed value (internal usage only).

};

static inline bool operator != (const NetworkLocalObjectInfo &a, const NetworkLocalObjectInfo &b) {return a.id!=b.id;}
static inline bool operator == (const NetworkLocalObjectInfo &a, const NetworkLocalObjectInfo &b) {return a.id==b.id;}

template <>
struct ImplicitMapTraits<NetworkRemoteObjectInfo>
{
  static NetworkRemoteObjectInfo null;                 ///< Empty value (can be passed as return value).
  static NetworkRemoteObjectInfo zombie;               ///< Removed value (internal usage only).

};

static inline bool operator != (const NetworkRemoteObjectInfo &a, const NetworkRemoteObjectInfo &b) {return a.id!=b.id;}
static inline bool operator == (const NetworkRemoteObjectInfo &a, const NetworkRemoteObjectInfo &b) {return a.id==b.id;}






template <>
struct ImplicitMapTraits<NetworkDeletedObjectInfo>
{
  static NetworkDeletedObjectInfo null;                 ///< Empty value (can be passed as return value).
  static NetworkDeletedObjectInfo zombie;               ///< Removed value (internal usage only).

};

static inline bool operator != (const NetworkDeletedObjectInfo &a, const NetworkDeletedObjectInfo &b) {return a.id!=b.id;}
static inline bool operator == (const NetworkDeletedObjectInfo &a, const NetworkDeletedObjectInfo &b) {return a.id==b.id;}




struct DelayedPublicVariable
{
  RString _name;
  int _player;
};
TypeIsMovableZeroed(DelayedPublicVariable)

struct DelayedSpawnForPlayer
{
  RString _code;
  GameValue _args;
};
TypeIsGeneric(DelayedSpawnForPlayer)

//! Network Client
class NetworkClient : public NetworkComponent
{
protected:
  //! DirectPlay ID of attached player
  int _playerDpid;

  //! low-level client implementation
  SRef<NetTranspClient> _client;

#if _USE_BATTL_EYE_CLIENT
  /// BE Integration
  class BattlEye
  {
  protected:
    /// dll handle
    HMODULE _library;
    bool _initialized;
    /// remember the scripts which were already checked and do not send them again

    static intptr_t KeyFromString(const RString &src) {return intptr_t(cc_cast(src));}

    ImplicitMap<intptr_t,RString,KeyFromString> _checked;
    AutoArray<intptr_t> _checkedFIFO;
    //@{
    /// callback functions
    typedef void __cdecl PrintMessageF(const char *message);
    static void __cdecl PrintMessage(const char *message);
    //@}

    //@{
    /// dll functions
    typedef bool __cdecl ExitF();
    typedef bool __cdecl RunF();
    typedef void __cdecl CommandF(const char *command);

    struct GameParams
    {
      unsigned long _address;
      unsigned short _port;
      PrintMessageF *_printMessage;
    };
    GameParams _gameParams;

    struct BEPtrStruct
    {
      ExitF *_exit;
      RunF *_run;
      CommandF * _command;
      CommandF * _onScriptExec;
    };    
    BEPtrStruct _callbacks;

    typedef bool __cdecl InitF(int iIntegrationVersion, GameParams *ppvGamePtrTable, BEPtrStruct *ppvBEPtrTable);
    typedef int __cdecl GetVerF();
    //@}

  public:
    BattlEye();
    ~BattlEye();

    bool Init(NetworkClient *client);
    void Done();
    bool Run(NetworkClient *client);
    void Command(const char *command);
    bool OnScriptExec(RString script);

  protected:
    void CleanUp()
    {
      _library = NULL;
      _callbacks._exit = NULL;
      _callbacks._run = NULL;
      _callbacks._command = NULL;
      _callbacks._onScriptExec = NULL;
    }
    int GetBEVersion(HMODULE library) const;
  };

  BattlEye _beIntegration;
  bool _beWanted;

public:
  bool BattlEyeOnScriptExec(RString script)
  {
    return _beIntegration.OnScriptExec(script);
  }
protected:
#endif

  static NetworkId GetIdFromLocalNetworkObjectInfo(const NetworkLocalObjectInfo &info) {return info.id;}
  static NetworkId GetIdFromRemoteNetworkObjectInfo(const NetworkRemoteObjectInfo &info) {return info.id;}

  typedef ImplicitMap<NetworkId, NetworkLocalObjectInfo, GetIdFromLocalNetworkObjectInfo > LocalObjectsContainer;
  typedef ImplicitMap<NetworkId, NetworkRemoteObjectInfo, GetIdFromRemoteNetworkObjectInfo > RemoteObjectsContainer;


  //! info about objects local on client
  LocalObjectsContainer _localObjects;
  //! info about objects non local (remote) on client
  RemoteObjectsContainer _remoteObjects;
  //! dead bodies (max MAX_LOCAL_BODIES can be simulated)
  AutoArray<BodyInfo> _bodies;
  //! wrecks (max MAX_LOCAL_WRECKS can be simulated)
  AutoArray<WreckInfo> _wrecks;

  /// mark if team switch request is in progress
  DWORD _teamSwitchDisabledUntil;

  //! message formats
  AutoArray<NetworkMessageFormatBase> _formats;
  //! message queues (candidates for aggregation)
  NetworkMessageQueue _messageQueue, _messageQueueNonGuaranteed;
  
  //! connection quality to server
  ConnectionQuality _connectionQuality;
  /// de-sync detected by server
  ConnectionDesync _connectionDesync;
  //! result of _dp->Connect
  ConnectResult _connectResult;

  //! current state
  NetworkClientState _clientState;
  //! current state on server
  NetworkServerState _serverState;

  /// information about last know mission voting results
  VotingMissionProgressMessage _missionVoting;

  /// last custom files progress state sent by NCMTCustomFilesProgress
  int _customFilesProgressMax;
  int _customFilesProgressPos;

  /// last timeout value received from the server, INT_MAX if none
  int _serverTimeout;

  /// server hostname
  RString _serverName;

  //! mission pbo file is valid (actual)
  bool _missionFileValid;

  //! player is game master of dedicated server
  bool _gameMaster;
  //! gamemaster is only voted admin (cannot do shutdown etc.)
  bool _admin;
  //! show "select mission" dialog for dedicated server
  bool _selectMission;
  //! show "vote mission" dialog for dedicated server
  bool _voteMission;

  //! Game is paused due to disconnection state of game
  bool _controlsPaused;

  //! missions on dedicated server
  AutoArray<MPMissionInfo> _serverMissions;

  //! name of local player
  RString _localPlayerName;

  int       _playerID;   //player identity number
  //! short id of player, used as creator in NetworkId
  CreatorId _creatorId;

  //! id of next sound - incremented for each PlaySound
  int _soundId;
  //! list of sent sounds
  AutoArray<PlaySoundInfo> _sentSounds;
  //! list of received sounds
  AutoArray<PlaySoundInfo> _receivedSounds;

  //! sound introducing chat message
  SoundPars _chatSound;

  //! selected channel for voice chat
  int _voiceChannel;

  //! ip address of dedicated server
  sockaddr_in _serverVoNAddr;

  //! is NAT negotiation is needed to access server?
  bool _natNegotiationNeeded;

  //! password used to connect to server
  RString _serverPassword;

  //! used for transfer of client info (for example camera position) to server
  Ref<ClientInfoObject> _clientInfo;

  SRef<ClientInputLocal> _clientInput;
  SRef<ClientInputState> _clientInputLast;
  
  //! used for make the client camera position synchronized on all clients
  Ref<ClientCameraPositionObject> _clientCameraPosition;

  //! number of bodies we want to hide
  int _hideBodies;

  //! number of wrecks we want to hide
  int _hideWrecks;

#if _VBS3 // player color
  bool _sentPlayerColour;
#endif

#if _ENABLE_CHEATS
  //! debugging window
  Link<DebugListWindow> _debugWindow;
#endif

#if _ENABLE_MP
  /// who muted me
  RemoteMuteListMessage _remoteMuteList;
#endif

#if _ENABLE_GAMESPY
  /// IP Address of the client (how is seen from matchmaking server)
  int _publicAddr;
#endif

  /// world transfer progress info
  int _totalToCreate;
  int _remainingToCreate;
  int _totalToUpdate;
  int _remainingToUpdate;

  /// event handlers executed in reaction to PublicVariableMessage
  PublicVariableEventHandlers _publicVariableEventHandlers;

  /// public variables which have Null networkId during PublicVariable are stored to be proccessed later again
  AutoArray<DelayedPublicVariable> _delayedPublicVariables;

  /// spawnForClient code received before the mission init script ran (player scripts are not compiled yet)
  AutoArray<DelayedSpawnForPlayer> _delayedSpawns;
  bool _initScriptRun = false;

  //! time synchronized from server to clients
  DWORD _serverTime;
  DWORD _serverTimeInitTickCount;
  DWORD _serverTimeOfEstimatedEnd;

  //! server admin and gameMaster variables got by ServerAdminMessage
  //  used to know whether some commands like #vote is available
  bool _serverAdmin;
  int _serverGameMaster;


public:

  //! Constructor
  /*!
  \param parent creating network manager object
  \param address URL address of server
  \param password password of session
  \param botClient client is bot client
  */
  NetworkClient(NetworkManager *parent, RString address, RString password);
  //! Connect to server
  /*!
  \param addr host Xbox Title Address
  \param address URL address of server
  \param password password of session
  \return true if client is connected to server
  */
  bool Init(RString address, RString password);

  //! Constructor for bot client
  /*!
  \param parent creating network manager object
  \param playerRating rating of player in statistics
  */
  NetworkClient(NetworkManager *parent, NetworkServer *server, int playerRating);
  //! Connect bot client to server
  bool Init(NetworkServer *server);

  //! Return dpid of attached player
  int GetPlayer() const {return _playerDpid;}

#if _ENABLE_GAMESPY
  /// Set on which IP Address the client is visible from the matchmaking server
  void SetPublicAddress(int addr) {_publicAddr = addr;}
#endif

  /// retrieve the local address client communicate on
  bool GetLocalAddress(in_addr &addr) const {return _client ? _client->GetLocalAddress(addr) : false;}
  bool GetDistantAddress(in_addr &addr, int &port) const {return _client ? _client->GetDistantAddress(addr,port) : false;}
  /// retrieve the port client is listening for voice peer-to-peer communication
  int GetVoicePort() const {return _client ? _client->GetVoicePort() : 0;}

  //! Voice over net initialization
  void InitVoice(bool isBotClient=false);
  //! Connect voice over net to the given player
  void ConnectVoice(int player, sockaddr_in &addr);

  /// Suspend voice playback temporarily
  void DisableVoice(bool disable);
  /// Switches on/off the voice transmition
  void SetVoiceTransmition(bool val) 
  {
    #if VOICE_OVER_NET
    if (_client) _client->SetVoiceTransmition(val);
    #endif
  }
  /// Sets if voice is toggled on (user is NOT using push to talk button)
  virtual void SetVoiceToggleOn(bool val)
  {
#if VOICE_OVER_NET
    if (_client) _client->SetVoiceToggleOn(val);
#endif
  }
  /// sets threshold for silence analyzer (0.0f - 1.0f)
  virtual void SetVoNRecThreshold(float val)
  {
#if VOICE_OVER_NET
    if (_client) _client->SetVoNRecThreshold(val);
#endif
  }
  virtual void SetVoNTransmitterFreq(float f)
  {
#if VOICE_OVER_NET
    if (_client) _client->SetVoNTransmitterFreq(f);
#endif
  }
  virtual float GetVoNTransmitterFreq() const
  {
#if VOICE_OVER_NET
    if (_client) return _client->GetVoNTransmitterFreq();
#endif
    return 0;
  }
  virtual void SetVoNTransmitterRange(float range)
  {
#if VOICE_OVER_NET
    if (_client) _client->SetVoNTransmitterRange(range);
#endif
  }
  virtual float GetVoNTransmitterRange2() const
  {
#if VOICE_OVER_NET
    if (_client) return _client->GetVoNTransmitterRange2();
#endif
    return 1000000.f;
  }
  virtual void WhatUnitsOnTransmitterChannel(RefArray<NetworkObject> &units);

  /// Switches on/off the voice capturing
  virtual void SetVoiceCapture(bool val, int quality) 
  {
#if VOICE_OVER_NET
    if (_client) _client->VoiceCapture(val,quality);
#endif
  }

  void CustomFilesProgressGet(int &max, int &pos)
  {
    max = _customFilesProgressMax; pos = _customFilesProgressPos;
  }
  void CustomFilesProgressClear() 
  {
    _customFilesProgressMax = 1; _customFilesProgressPos = 0;
  }

  //! Destructor
  ~NetworkClient();

  //! Disconnect from server
  void Done();

  bool IsValid() const {return _client != NULL;}

  //! Given player is currently speaking
  bool IsVoicePlaying(int dpnid) const {return _client ? _client->IsVoicePlaying(dpnid) : false;}

  //! Local player is currently speaking
  bool IsVoiceRecording() const {return _client ? _client->IsVoiceRecording() : false;}

  int CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const
  {
    if (_client)
    {
      return _client->CheckVoiceChannel(dpnid,audible2D,audible3D);
    }
    audible2D = audible3D = false;
    return 0;
  }

  void IsVoice2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D) const
  {
    if (_client)
    {
      _client->IsVoice2D3DPlaying(dpnid,audible2D,audible3D);
    }
    else
    {
      audible2D = audible3D = false;
    }
  }

  void DebugServerExec(RString str);

#if _ENABLE_CHEATS
  void VoNSay(int dpnid, int channel, int frequency, int seconds)
  {
    if (_client) _client->VoNSay(dpnid, channel, frequency, seconds);
  }
#endif

  //! Return connection type info for each player along with the GDebugVoNBank output
  RString GetDebugVoNString();

  //! Server time synchronized to clients
  DWORD GetServerTime() 
  {
    _serverTime = ::GetTickCount() - _serverTimeInitTickCount; //make it fresh
    return _serverTime;
  }

  DWORD GetEstimatedEndServerTime() 
  {
    return _serverTimeOfEstimatedEnd;
  }

  RString ServerName() const { return _serverName; }

  int ConsoleF(const char *format, ...);

  //! Create debugging window
  void CreateDebugWindow();

  //! Return name of local player
  RString GetLocalPlayerName() const {return _localPlayerName;}
  //! Return result of _dp->Connect
  ConnectResult GetConnectResult() const {return _connectResult;}

  //! Return if player is game master of dedicated server
  bool IsGameMaster() const {return _gameMaster;}

  //! Return if gamemaster is only voted admin (cannot do shutdown etc.)
  bool IsAdmin() const {return _admin;}

  //! Return if client is bot client
  inline bool IsBotClient() const;

  inline NetworkServer *GetServer() const;

  //! Return connection quality to server
  ConnectionQuality GetConnectionQuality() const {return _connectionQuality;}

  //! Return connection quality to server
  ConnectionDesync GetConnectionDesync() const {return _connectionDesync;}
  
  //! Return current state
  NetworkClientState GetClientState() const {return _clientState;}
  //! Set state and send notification to server
  void SetClientState(NetworkClientState state);

  //! Return how much part of world state was already transferred to client
  bool GetTransferProgress(float &progress) const;

  //! Handler called when client is joining game in progress
  void OnJoiningInProgress();

  //! Clear possible structures keeping something related to dpnid of disconnecting player (such as subscribed radios)
  void OnPlayerLogout(int dpnid);

  //! Time elapsed from start of mission on server
  int TimeElapsed() const;

  //! Return current state on server
  NetworkServerState GetServerState() const {return _serverState;}
  
  int GetServerTimeout() const {return _serverTimeout;}

  //! Return recommended chat priorty based on current game state
  NetMsgFlags GetChatPriority() const;

  //! Set parameters for current mission
  void SetParams(float param1, float param2, bool updateOnly);
  //! Set parameters for current mission
  void SetParams(float param1, float param2, AutoArray<float> paramsArray, bool updateOnly);

  InputInterface *GetInputInterface();

  void OnSimulate();
  void OnSimulateDone();

  /// time critical network update - need to be called several times in frame
  void FastUpdate();
  void OnDraw();
  void OnMessage(int from, NetworkMessage *msg, NetworkMessageType type);

  //! perform regular memory clean-up
  unsigned CleanUpMemory();

  /// called before network shutdown is initiated - can be called multiple times
  void OnClose();
  //! Process received system messages
  void ReceiveSystemMessages();
  //! Process received user messages
  void ReceiveUserMessages();
  //! Destroy all received system messages
  void RemoveSystemMessages();
  //! Destroy all received user messages
  void RemoveUserMessages();

  
  //! Hi-level send of message to server
  /*!
  \param object message object
  \param dwFlags DirectPlay flags
  */
  DWORD SendMsg
  (
    NetworkSimpleObject *object, NetMsgFlags dwFlags
  );

  //! Send single network message
  /*!
  This function determine, whic type of send will be used - local send, agregation or send using DirectPlay
  \param to DirectPlay ID of message receiver
  \param msg message itself
  \param type type of message
  \param dwFlags DirectPlay flags
  */
  DWORD SendMsg
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, NetMsgFlags dwFlags,
    NetworkObject *object
  );
  virtual DWORD SendMsg
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, NetMsgFlags dwFlags
  ) 
  {
    return SendMsg(to, msg, type, dwFlags, NULL);
  }

  Ref<NetMessage> DXSendMsg(int to, NetworkMessageRaw &rawMsg, DWORD &msgID, NetMsgFlags dwFlags, const Ref<NetMessage> &dependOn);
  NetworkMessageFormatBase *GetFormat(/*int client, */int type);
  const NetworkMessageFormatBase *GetFormat(/*int client, */int type) const;

  virtual void InitMsg(NetworkMessage *msg, NetworkMessageType type);

  //! Return short id of player, used as creator in NetworkId
#define DEBUG_CREATOR_ID 0
#if DEBUG_CREATOR_ID
  mutable bool _debugCreatorIdSet;
#endif
  CreatorId GetCreatorId() const 
  {
#if DEBUG_CREATOR_ID
    if (!_debugCreatorIdSet && _playerDpid!=BOT_CLIENT)
    {
      RptF("Warning: GetCreatorId called before its change!");
      static bool doIt = false;
      if (doIt)
      {
        _debugCreatorIdSet = true;
      }
    }
#endif
    return _creatorId;
  }
  //! Set player id directly (used on dedicated server - BOT client has no identity)
  void SetCreatorId(CreatorId id) {_creatorId = id;}
  void SetPlayerId(int id) {_playerID = id;}

  //! Select person as player
  /*!
  \param player DirectX player id
  \param person player's person
  \param respawn play "ressurect" cutscene
  */
  void SelectPlayer(int player, Person *person, bool respawn = false);
  // Send NMTSelectPlayer message to server
  void SendSelectPlayer(int dpid, Person *person);
  //! Play sound on all clients (except itself)
  /*!
  \param name name of sound file
  \param position source position
  \param speed source speed
  \param volume sound volume
  \param freq sound frequency
  \param wave local sound object (for mapping local sounds to sent sounds)
  */
  void PlaySound
  (
    RString name, Vector3Par position, Vector3Par speed, 
    float volume, float freq, AbstractWave *wave
  );
  //! Change state of played sound on all clients (except itself)
  /*!
  \param wave local sound object
  \param state state to set
  */
  void SoundState(AbstractWave *wave, SoundStateType state);
  //! Register vehicle on client and send message to server (and other clients)
  /*!
  \param veh registered vehicle
  \param type id of vehicle list to add into
  \param name name of variable vehicle is stored in
  \param idVeh id of vehicle (assigned in mission editor)
  */
  bool CreateVehicle(Vehicle *veh, VehicleListType type, RString name, int idVeh);
  //! Register subobject of vehicle on client and send message to server (and other clients)
  /*!
  \param veh owner vehicle
  \param index unique id of object in vehicle
  \param object created object
  */
  bool CreateSubobject(Vehicle *veh, TurretPath &path, NetworkObject *object, bool setNetworkId=true);
  //! Register generic network object and send message to server (and other clients)
  bool CreateObject(NetworkObject *object, bool setNetworkId=true);

  bool SendCreateMessage( NetworkObject * object );

  //! Send first update of all created objects over network (as guaranteed message)
  void CreateAllObjects();
  //! Unregister all objects, both local and remote
  void DestroyAllObjects();
  //! Force switch on pilot light
  void AskForPilotLight(EntityAI *vehicle, bool enable);
  //! Vehicle door open/close
  void AskForVehicleDoorState(Transport *vehicle, int doorIndex, bool opened);

  void CreateObjectStatic(Object *who);

  void ExplosionDamage( EntityAI * owner, Vector3Par originDir, Object * directHit, Vector3Par directHitModelPos, HitInfoPar hitInfo, Object * componentOwner, int componentIndex, float energyFactor, float explosionFactor, float hitDepth );

  //! Ask vehicle owner for change ammo state
  /*!
  \param vehicle vehicle which ammo is changing
  \param turret turret which ammo is changing
  \param weapon weapon index
  \param burst amount of ammo to decrease
  */
  void AskForAmmo(
    EntityAIFull *vehicle, Turret *turret, const WeaponModePtr &ptr, int burst
  );

  //! Ask object owner for move object
  /*!
  \param vehicle moving object
  \param pos new position
  */
  void AskForMove
  (
    Object *vehicle, Vector3Par pos
  );
  //! Ask object owner for move object
  /*!
  \param vehicle moving object
  \param trans new transformation matrix
  */
  void AskForMove
  (
    Object *vehicle, Matrix4Par trans
  );

	//! Ask magazine owner for change its ammo count
  /*!
  \param magazine
  \param ammo
  */
  void AskForSetMagazineAmmo
  (
    Object *magazine, int ammo
  );
  //! Transfer explosion effects (explosion, smoke, etc.) to other clients
  /*!
  \param owner shot owner (who is responsible for explosion)
  \param shot shot
  \param directHit hitted object
  \param pos explosion position
  \param dir explosion direction
  \param dir index of the hit component
  \param type ammunition
  \param enemyDamage some enemy was damaged
  \param explosionFactor how large explosion is
  */
  void ExplosionDamageEffects
  (
    EntityAI *owner, Object *directHit, HitInfoPar hitInfo, int componentIndex, 
    bool enemyDamage, float energyFactor, float explosionFactor
  );
  //! Transfer projectile decals (arrows stuck on body, etc.) to other clients
  /*!
  \param directHit Object that got hit
  \param componentIndex Index of the component that got hit
  \param hitInfo  Info about the  hit
  \param hitDepth The depth of penetration
  \param hitPosLcl Hit position on the body (local space)
  \param hitDirLcl Hit direction relative to the body part (local space)
  \param hitDirUpLcl Up direction of hit relative to the body part (local space)
  */
  void ApplyProjectileDecal
  (
    Object *directHit, int componentIndex, HitInfoPar hitInfo, float hitDepth, Vector3Par hitPosLcl, Vector3Par hitDirLcl, Vector3Par hitDirUpLcl
  );
  //! Remove on other clients the projectile decals stuck on victim's body
  /*!
  \param person The person from which projectiles are removed
  */
  void RemoveProjectileDecals(Object *person);
  //! Transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients
  /*!
  \param vehicle firing vehicle
  \param weapon firing weapon index
  \param magazine fired magazine
  \param target aimed target
  */
  void FireWeapon(
    EntityAIFull *vehicle, Person *gunner, const WeaponModePtr &ptr, Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo &remoteInfo
  );
  //! Transfer bow action state to other clients
  /*!
  \param entity Person using the bow
  \param state The state of bow
  */
  void UpdateBowState(EntityAIFull *entity, BowState state );
  //! Set a texture for a vehicle on other clients
  /*!
  \param vehicle vehicle that will have a texture changed
  \param index selection index to change
  \param name texture name to use
  */
  void SetObjectTexture(EntityAI *veh, int index, RString name, RString eval = "true");
  //! Execute game code on other clients
  /*!
  \param condition game code to evaluate to true or false
  \param command game code to execute
  \param obj object to pass as _this
  \param objs array of objects to pass as _this
  */
  void PublicExec(RString condition, RString command, Object* obj, OLinkArray(Object) &objs);
  //! Transfer message who is responsible to destroy vehicle to other clients
  /*!
  \param killed destroyed vehicle
  \param killer who is responsible for destroying
  */
  void OnVehicleDestroyed(EntityAI *killed, EntityAI *killer);
  //! Transfer message about damage of vehicle to other clients
  /*!
  \param damaged damaged vehicle
  \param damage amount of damage hit
  \param ammo ammunition type
  */
  void OnVehicleDamaged(EntityAI *damaged, EntityAI *killer, float damage, RString ammo);
  //! Transfer info about (user made) marker creation to other clients
  /*!
  \param channel chat channel (who will see the marker)
  \param sender sender unit
  \param units receiving units
  \param info marker itself
  */
  void MarkerCreate(int channel, AIBrain *sender, RefArray<NetworkObject> &units, ArcadeMarkerInfo &info);
  //! Transfer info about (user made) marker was deleted to other clients
  /*!
  \param name name of marker
  */
  void MarkerDelete(RString name);

  //! Public variable to other clients
  /*!
  \param name variable name
  */
  void PublicVariable(RString name, int dpnid);

  void SendAUMessage(GameValuePar oper1);
  
  /**
  Do ErrF locally and send the message to the server as well.
  */
  void ServerErrF(const char *format, ...);

  //! Public variables which contains objects with NULL networkId are processed after CreateAllObjects again
  void ProcessDelayedPublicVariables();
  void ClearDelayedPublicVariables();
  void AddDelayedPublicVariable(RString name, int dpnid);

  //! Run spawnForClient code queued until the mission init script ran
  void ProcessDelayedSpawns();
  void RunSpawnedCode(RString code, GameValuePar args);

  //! Send chat message
  /*!
  \param channel chat channel
  \param text chat text
  */
  void Chat(int channel, RString text);

  //! Send chat message
  /*!
  \param channel chat channel
  \param sender sender unit
  \param units receiving units
  \param text chat text
  */
  void Chat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text);
  //! Send chat message
  /*!
  \param channel chat channel
  \param sender sender name
  \param units receiving units
  \param text chat text
  */
  void Chat(int channel, RString sender, RefArray<NetworkObject> &units, RString text);

  /// Send client-side localized chat message
  void LocalizedChat(int channel, const LocalizedFormatedString &text);

  /// Send client-side localized chat message
  /*!
  \param channel chat channel
  \param sender sender unit
  \param units receiving units
  \param text chat text
  */
  void LocalizedChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text);
  /// Send client-side localized chat message
  /*!
  \param channel chat channel
  \param sender sender name
  \param units receiving units
  \param text chat text
  */
  void LocalizedChat(int channel, RString sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text);
  
  //! Send radio message as chat (text and sentence)
  /*!
  \param channel chat channel
  \param sender sender name
  \param units receiving units
  \param text chat text
  \param sentence - list of words to be spoken
  */
  void RadioChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text, RadioSentence &sentence);
  //! Send text radio message as chat (sound and title)
  /*!
  \param channel chat channel
  \param units receiving units
  \param wave name of class from CfgRadio containing sound and title
  \param sender sender unit
  \param senderName sender name
  */
  void RadioChatWave(int channel, RefArray<NetworkObject> &units, RString wave, AIBrain *sender, RString senderName);

  /// Return actual channel for voice chat
  int GetVoiceChannel() const {return _voiceChannel;}
  //! Set channel for Voice Over Net
  /*!
  \param channel chat channel
  */
  void SetVoiceChannel(int channel);
  //! Set position and speed of some Voice Over Net source (it affects sources)
  /*!
  \param dpnid player id got from GetRemotePlayer()
  \param pos position of player
  \param speed speed of player
  */
  void SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed);
  //! Set position and speed of player in Voice Over Net (it affects listener)
  /*!
  \param pos position of player
  \param speed speed of player
  */
  void SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed);
  //! Set obstruction of player in Voice Over Net (it affects source)
  /*!
  \param dpnid player id got from GetRemotePlayer()
  */
  void SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT);
  //! Updates volume and ear accomodation of player in Voice Over Net (it affects source)
  /*!
  \param dpnid player id got from GetRemotePlayer()
  */
  void UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation);
  //! Advance all voices in VoN (even muted ones) 
  /*!
  \param deltaT time to advance
  \param paused whether game is paused
  \param quiet whether voices should be muted
  */
  void AdvanceAllVoNSounds( float deltaT, bool paused, bool quiet );

  //! Send update of the content of the Custom Radio Channel
  /*!
  \param channel The channel in range CCCustom1..CCCustomLast
  \param ucr_type What to update (all, label, callSign, units, ...)
  */
  void UpdateCustomChannel(int channel, int ucr_type);

  //! Transfer file to server
  /*!
  \param dest destination filename
  \param source source filename
  */
  void TransferFileToServer(RString dest, RString source);
  //! Retrieve state of file transfer
  /*!
  \param curBytes amount of transfered bytes
  \param totBytes total amount bytes to transfer
  */
  void GetTransferStats(int &curBytes, int &totBytes);
  void ApplyWeather();
#if _VBS2 //Convoy trainer
  void AARDoUpdate(int type ,int intType,RString stringOne = RString(), RString stringTwo = RString(), float floatType = 0.0);
  void AARAskUpdate(int type ,int intType,RString stringOne = RString(), RString stringTwo = RString(), float floatType = 0.0);
  void AddMPReport(int type, RString myString);
  void LoadIsland(RString islandName);
  void AskWeather();
  void AskCommanderOverride(Turret *turret, Turret *overriddenByTurret);
#endif
  //! Ask vehicle owner to destroy vehicle
  /*!
  \param vehicle vehicle to destroy
  */
  void AskForDeleteVehicle(Entity *veh);

  void AskForSetItemShortcut(Person *person, int index, const InventoryItemType *type, InventoryItem *item);
  void AskForThrowItem(Man *person, InventoryItem *item, Vector3 pos, Vector3 dir);
	void AskForDropItem(Person *person, InventoryItem *item);
  void AskForMoveItem(EntityAI *target, int index, InventoryItem *item, bool rotated);
  void AskForRotateInventoryItem(Person *person, InventoryItem *item);
  void AskForTakeItemToHands(Person *person, InventoryItem *item);
  void AskForSwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner);

  void AskForPlayerAction( Person * person, RString action );

  void AskInventoryMenu(AIBrain *unit, InventoryItem *item1, InventoryItem *item2);

#if _VBS3 // respawn command
  //! Tell local simulation to pause itself
  /*!
  \param pause simulation true or false
  */
  void PauseSimulation(bool pause);
#endif

#if _ENABLE_ATTACHED_OBJECTS
  //! Attach Object
  /*!
  \param obj, object to attach
  \param attachTo, where does the object get attached to?
  \param memindex, memory point index were it get's attach to parent, (-1) for center
  \param pos, position offset from attachPoint
  \param flags, additional flags
  */
  void AttachObject(Object *obj, Object *attachTo, int memIndex, Vector3 pos, int flags);
  //! detach object
  /*!
  \param obj, object to detach
  */
  void DetachObject(Object *obj);
#endif

  //! Ask vehicle for user defined animation
  /*!
  \param vehicle animated vehicle
  \param animation animation name
  \param phase wanted animation phase
  */
  void AskForAnimationPhase(Entity *vehicle, RString animation, float phase);

  //! Checks if gamemaster is to select mission on dedicated server
  bool CanSelectMission() const {return _selectMission;}
  //! Checks if client is to vote mission on dedicated server
  bool CanVoteMission() const {return _voteMission;}
  //! Returns array of missions available on dedicated server
  const AutoArray<MPMissionInfo> &GetServerMissions() const 
  {
    return _serverMissions;
  }

  /// check voting status
  const VotingMissionProgressMessage *GetMissionVotingState() const {return &_missionVoting;}
  //! Select mission on dedicated server
  /*!
  \param mission name of selected mission
  \param difficulty game difficulty level
  */
  void SelectMission(RString mission, int difficulty);
  //! Vote mission on dedicated server
  /*!
  \param mission name of selected mission
  \param difficulty game difficulty level
  */
  void VoteMission(RString mission, int difficulty);

  /// Find the object (local or remote) by its network id
  NetworkObject *GetObject(const NetworkId &id);
  //! Unregister given network object, send message to others
  void DeleteObject(const NetworkId &id, bool sendMsg);

  //! Process chat command for remote control of dedicated server
  /*!
  \param command chat command
  */
  bool ProcessCommand(RString command);
  bool IsCommandAvailable(NetworkCommandType type);
  bool IsCommandAvailable(RString command);

  //! Ask server to kick off player
  /*!
  \param player DirectPlay ID of player to kick off
  */
  void SendKick(int player);

  //! Ask server to disable/enable connection of further clients
  /*!
  \param lock true to disable connection
  */
  void SendLockSession(bool lock = true);

  //! player must disconnect due to some mission loading error
  void Disconnect(RString message);

  //! Force update of network object
  /*!
  \param object object to update
  \param dwFlags DirectPlay flags
  */
  void UpdateObject(NetworkObject *object, NetMsgFlags dwFlags);

  //! Force update of network object (single type of update)
  /*!
  \param object object to update
  \param cls type of update
  \param dwFlags DirectPlay flags
  */
  int UpdateObject(NetworkObject *object, NetworkMessageClass cls, NetMsgFlags dwFlags);

  const char *GetDebugName() const {return "Client";}

  RString GetDiagText(const NetworkObject *obj) const;

  //! Return estimated end of mission time
  Time GetEstimatedEndTime() const;

  //! Body can be hidden (for better performance)
  void DisposeBody(Person *body);

  //! Wreck can be hidden (for better performance)
  void DisposeWreck(Transport *wreck);

  //! Game is paused due to disconnection state of game
  bool IsControlsPaused() {return _controlsPaused;}

  //! Last received message's age in seconds (used to eliminate "disconnect cheat")
  float GetLastMsgAgeReliable();

  //! Cleanup unit after player disconnection
  void CleanupPlayer(int dpnid, NetworkId id);

  //! Set voice mask to new value (Xbox)
  void ChangeVoiceMask(RString mask);

  /// Reaction to detection of hacked data
  void HackedDataDetected(RString filename);

  /// add event handler executed in reaction to PublicVariableMessage
  void AddPublicVariableEventHandler(RString name, GameValuePar code);

  void SynchronizeVariable( EntityAI * veh, RString name, float coef, const GameDataCode & code );

  void ProcessAction(Action *action, AIBrain *unit);

  void ProcessRecipe(RString recipe, Person *person, InventoryItem *item1, InventoryItem *item2);

  // implementation
protected:
  //! Find info about local object with given id (NULL if not found)
  NetworkLocalObjectInfo *GetLocalObjectInfo(const NetworkId &id);
  //! Find info about remote object with given id (NULL if not found)
  NetworkRemoteObjectInfo *GetRemoteObjectInfo(const NetworkId &id);

  //! Register network object as local object, assign id to them
  NetworkId CreateLocalObject(NetworkObject *object, bool setNetworkId=true);

  void InsertLocalObject(NetworkObject *object);

  void InsertRemoteObject(NetworkObject *obj, bool remote);

  //! Prepare multiplayer game
  /*!
  Switch landscape, parse mission sqm file, initialize GWorld.
  */
  bool PrepareGame();

  //! Perform actions needed when client reach state NCSPlaying
  void OnMissionStarted();

  //! Select player on local machine
  void ProcessSelectPlayer(Person *person, bool respawn, Vector3Par pos);
  
  ///! Create and Update messages can be received only when client is ready to play
  inline bool CanReceiveUpdateMessages()
  {
    return (_clientState >= NCSMissionReceived && _clientState <= NCSPlaying);
  }

  //! Create network object and register as remote object
  /*!
  \param ctx network message context of create message
  \param dummy dummy argument for specification type of created network object
  */
  template <class VehType>
  bool CreateRemoteObject(NetworkMessageContext &ctx)
  {
    VehType *dummy; // VehType not seen inside of lambda (Visual Studio 2010), this is a trick to pass it there
    auto create = [dummy](NetworkMessageContext &ctx)->NetworkObject * {return dummy->CreateObject(ctx);};
    return CreateRemoteObject(ctx,create);
  }

  bool CreateRemoteObject(NetworkMessageContext &ctx, std::function<NetworkObject *(NetworkMessageContext &ctx)> create);

  //! Unregister and destroy remote network object
  void DestroyRemoteObject(NetworkId id);

  //! Enforce destruction of (local or remote) object
  void ForceDeleteObject(NetworkObject *object);

  //! Send all agregated and update messages
  void SendMessages();
  //! Calculate limits for message sending
  void EstimateBandwidth(int &nMsgMax, int &nBytesMax);
  //! Calculate errors and place object in list sorted by error
  /*!
  \param objects output sorted list of objects
  */
  void CreateObjectsList(AutoArray<UpdateLocalObjectInfo, MemAllocSA> &objects);
  //! Prepare next object update message to message queue
  bool PrepareNextUpdate(AutoArray<UpdateLocalObjectInfo, MemAllocSA> &objects, int &next);

  //! Return number of auto destroyed local objects - used for debugging purposes
  int NLocalObjectsNULL() const;
  //! Check database of local objects - used for debugging purposes
  bool CheckLocalObjects() const;

  /// clear voting once we know it is no longer valid
  void ClearMissionVoting();
  //! Server state changed
  void OnServerStateChanged(NetworkServerState state);

  //! State change enforced by server
  void OnClientStateChanged(NetworkClientState state);

  void EnqueueMsg
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type,
    NetworkObject *object
  );
  void EnqueueMsgNonGuaranteed
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, const RefR<NetMessage, RefCountSafe> &dependOn
  );

  //! Returns which players hear me
  void GetPlayersOnChannel(AutoArray<int, MemAllocSA> &players, int channel) const;
  //! Returns which players can hear me in direct speak (are within the radius range depending on channel)
  void GetTargetsInDirectSpeakRange(AutoArray<TransmitTarget, MemAllocSA> &players, int channel) const;

  //! Check if player is in remote mute list (local player muted by given player)
  bool IsPlayerInRemoteMuteList(int player) const;

protected:
  //! start validation of bank hash
  //! \param askedIndex index matching NMTDataSignatureAsk
  //! \param qBankIx index of bank inside global GFileBanks
  //! \return true when check was succesful
  bool CheckDataSignature(int askedIndex, int level, QFBank &bank, int gBankIx);
  //! start validation of other additional file hash
  bool CheckDataSignature(int askedIndex, int level, RString filename);
  //! start validation of bank hash (using filename to find inside some bank, sig.ver > 1)
  bool CheckFileSignature( int bankIndex, int level, RString filename );

  /// queue of banks waiting for signature check
  FindArrayKey<CheckSignatureItem> _checkSignatureQueue;
  /// currently checked signature
  SRef<BankSignatureCheckAsync> _checkingSignature;  

#if defined _XBOX && _XBOX_VER >= 200
  /// updates all needed based on new local player info list
  void UpdateLocalPlayerInfoList(const LocalPlayerInfoList& localPlayerInfoList);


#endif
};

//! Info about single type of update of network object
struct NetworkCurrentInfo
{
  //! type of message
  NetworkMessageType type;
  //! sender of message
  int from;
  //! last received state of object
  Ref<NetworkMessage> message;
};

#include <Es/Memory/normalNew.hpp>

//! info about state of sent updates of network object for single client
struct NetworkPlayerObjectInfo: public RefCount, public TLinkBidirD
{
  //! ID of player
  int player;
  /// backlink to NetworkObjectInfo, needed for OnStopUpdating
  NetworkObjectInfo *oInfo;
  //! state of updates
  NetworkUpdateInfo updates[NMCUpdateN];
  //! All updates depends on this message (to be handled after)
  RefR<NetMessage, RefCountSafe> _fence;
  //! is within the updating range
  bool updating;

  NetworkPlayerObjectInfo();
  ~NetworkPlayerObjectInfo();

  //! allocated quite often
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

//! Info about network object on server
/*!
\patch_internal 1.23 Date 9/18/2001 by Jirka
- Improved: storage of PlayerObjectInfo changes
*/
#include <Es/Memory/normalNew.hpp>

/// messages waiting to be sent until acknowledgement OwnerChanged arrives from previous owner in ChangeOwner process
struct WaitingMessageInfo: public RefCount
{
  Ref<NetworkMessage> msg;
  NetworkMessageType  type;

  WaitingMessageInfo(NetworkMessageType t, NetworkMessage *m) : msg(m), type(t) {};

  USE_FAST_ALLOCATOR
};

UNIQUE_TYPE(PlayerIndex,int);

struct NetworkObjectInfo: public RefCount
{
  //! ID of object
  NetworkId id;
  /// the object itself
  OLinkNO(NetworkObject) object;
  //! DirectPlay ID of owner client
  int owner;
  //! DirectPlay ID of new owner client pending (ChangeOwner message sent but waiting to acknowledgment OwnerChanged message)
  //! when set to NO_CLIENT no change owner is pending
  int pendingOwner;
  //! create message (used for JOIN IN PROGRESS)
  NetworkCurrentInfo create;
  //! state of last received updates
  NetworkCurrentInfo currentUpdate[NMCUpdateN];
  //! state of last received updates sent from owner when bot-client sent overrides
  NetworkCurrentInfo currentRejected[NMCUpdateN];
  //! current rejection status reported by a bot-client
  InitVal<bool,false> rejected[NMCUpdateN];
  //! state of last sent updates (for each player)
  RefArray<NetworkPlayerObjectInfo> playerToObjects;
  //! messages waiting to be sent to clients until acknowledgment OwnerChanged arrives from previous owner in ChangeOwner process
  RefArray<WaitingMessageInfo> waitingMessages;

  struct UpdateInfo
  {
    Vector3 _pos;
    float _updateDistance;

    UpdateInfo():_pos(VZero),_updateDistance(FLT_MAX){}
  };
  UpdateInfo _updInfo;

  NetworkObjectInfo();
  ~NetworkObjectInfo();

  //! Find info about network object for given player
  NetworkPlayerObjectInfo *GetPlayerObjectInfo(PlayerIndex player, int pid);
  //! Create info (register) of new network object for given player
  NetworkPlayerObjectInfo *CreatePlayerObjectInfo(PlayerIndex player, int pid);
  //! Delete info (unregister) network object for given player
  void DeletePlayerObjectInfo(PlayerIndex player, int pid);
  //! Delete info (unregister) network object for given player (used when player becomes an owner)
  void NullPlayerObjectInfo(PlayerIndex player, int pid);
  //! Forget the updates sent to given player
  void ResetPlayerObjectInfo(PlayerIndex player, int pid);

  UpdateInfo BestPosition(const NetworkServer *server) const;
  UpdateInfo BestPosition() const;
  bool CheckBestPositionIntegrity(const NetworkServer *server) const;

  void ScanBestPosition(const NetworkServer *server);

  UpdateInfo CheckBestPosition(const NetworkServer *server) const;
  RString GetDebugName() const;

  //! Objects of this type will be allocated quite often
  USE_FAST_ALLOCATOR
};
//TypeIsMovable(NetworkObjectInfo)

#include <Es/Memory/debugNew.hpp>

//! calculate min, max and average from set of values
class CalculateMinMaxAvg
{
  float _min,_max,_sum;
  float _weight;
  /// last value sampled
  float _lastValue;

  public:
  CalculateMinMaxAvg();
  //! add sample
  void Sample(float value, float weight=1);
  //! reset 
  void Reset();
  //! get current results (min)
  float GetMin() const {return _weight>0 ? _min : 0;}
  //! get current results (max)
  float GetMax() const {return _weight>0 ? _max : 0;}
  //! get current results (average)
  float GetAvg() const {return _weight>0 ? _sum/_weight :0;}
  /// get last value
  float GetLastValue() const {return _lastValue;}
};

struct PendingDataSignatureCheck
{
  //! the index of checked file
  DWORD answerIndex;
  //! timeout for question
  DWORD checkTimeout;
};

TypeIsSimple(PendingDataSignatureCheck)

struct PendingFileSignatureCheck
{
  //! the checked file
  RString filename;
  //! the pbo prefix
  RString prefix;
  //! timeout for question
  DWORD checkTimeout;
};

TypeIsMovableZeroed(PendingFileSignatureCheck)

// Helper functor
class CheckSignatureTimeoutFunc
{
public:
  virtual bool operator ()() = 0;
};

//! PendingSignatureChecks encapsulates both PendingDataSignatureCheck and PendingFileSignatureCheck
class PendingSignatureChecks
{
  AutoArray<PendingFileSignatureCheck> pendingFileChecks;
  AutoArray<PendingDataSignatureCheck> pendingDataChecks;
public:
  PendingSignatureChecks() {}

  // both  data and file signature pendings are checked for timeout and given action is processed when timeouted
  void CheckTimeouts(CheckSignatureTimeoutFunc &func
#define DEBUG_SIGNATURES 1
#if DEBUG_SIGNATURES
    , RString &errorInfo
#endif
    );

  // add some time to timeout of initial data signature check
  void UpdateInitialCheckTimeout();

  // add the time specified to all signature timeouts
  void DelayCheckTimeouts(DWORD time, const char *playerName);

  //@{ interface to work with PendingDataSignatureChecks
  bool Pending(int bankIndex)
  {
    for (int i=0; i<pendingDataChecks.Size(); i++)
    {
      if (pendingDataChecks[i].answerIndex==bankIndex) return true;
    }
    return false;
  }
  bool Remove(int bankIndex)
  {
    for (int i=0; i<pendingDataChecks.Size(); i++)
    {
      if (pendingDataChecks[i].answerIndex==bankIndex)
      {
#if DIAG_INITIAL_SIGNATURE_CHECK_TIME
        if (bankIndex == 0x50be)
        {
          extern DWORD GStartTime;
          float lastTime = (GlobalTickCount()-GStartTime)/1000.0f;
          RptF("Initial Signature Test time: %.2fs", lastTime);
        }
#endif
        pendingDataChecks.Delete(i);
        return true;
      }
    }
    return false;
  }
  void Add(int bankIndex)
  {
    PendingDataSignatureCheck &check = pendingDataChecks.Append();
    check.answerIndex = bankIndex;
    check.checkTimeout = GlobalTickCount() + 90000; // 1 1/2 min
#if DIAG_INITIAL_SIGNATURE_CHECK_TIME
    if (bankIndex == 0x50be)
    {
      extern DWORD GStartTime;
      GStartTime = check.checkTimeout-90000;
    }
#endif
  }
  //@}

  //@{ interface to work with PendingFileSignatureChecks
  bool Pending(RString filename)
  {
    for (int i=0; i<pendingFileChecks.Size(); i++)
    {
      if ( stricmp(pendingFileChecks[i].filename,filename)==0 ) return true;
    }
    return false;
  }
  bool Remove(RString filename, RString *prefix)
  {
    for (int i=0; i<pendingFileChecks.Size(); i++)
    {
      if ( stricmp(pendingFileChecks[i].filename,filename)==0 ) 
      {
        // verify the prefix matches as well
        if (prefix) *prefix = pendingFileChecks[i].prefix;
        pendingFileChecks.Delete(i);
        return true;
      }
    }
    return false; // unexpected answer
  }
  void Add(RString filename, RString prefix)
  {
    PendingFileSignatureCheck &check = pendingFileChecks.Append();
    check.filename = filename;
    check.prefix = prefix;
    check.checkTimeout = GlobalTickCount() + 90000;  // 1 1/2 min
  }
  int CountPendingFiles() const {return pendingFileChecks.Size();}
  int CountPendingData() const {return pendingDataChecks.Size();}
  //@}
};

#include <Es/Memory/normalNew.hpp>

/// messages waiting to be sent
struct DelayedMessageInfo: public RefCount
{
  Ref<NetworkMessage> msg;
  NetworkMessageType type;
  float error;
  float invMaxAge;
  Vector3 pos;
  /// when the message was received
  Time timeReceived;

  DelayedMessageInfo(NetworkServer *server, NetworkMessageType t, NetworkMessage *m);
  
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

struct CDKeyCheck
{
  DWORD timeout;
  CDKeyCheck() : timeout(0) {}
};

//! Hashes collected from DataSignatureAnswer messages
class CollectedHashes
{
  struct CollectedHash
  {
    RString    fileName;
    RString    prefix;
    Temp<char> hash;          // DataContentHash
    Temp<char> ver2lev0hash;  // hash of (   DataContentHash + fileListHash + pbo prefix)
    Temp<char> ver2lev2hash;  // hash of (nonDataContentHash + fileListHash + pbo prefix)

    //! CollectedHash is initialized during initial SignatureCheck
    CollectedHash(RString filNam, Temp<char> &hs) : fileName(filNam), hash(hs) {}
    CollectedHash() {};
    ClassIsMovable(CollectedHash)
  };

  static const int SignatureCheckLevels = 3;
  struct RandomUsedFileCandidate
  {
    RString filename;
    RString prefix;
    int levelAsksCount[CollectedHashes::SignatureCheckLevels];

    RandomUsedFileCandidate() {}
    RandomUsedFileCandidate(RString fn, RString prf) : filename(fn), prefix(prf) 
    {
      for (int i=0; i<CollectedHashes::SignatureCheckLevels; i++) levelAsksCount[i]=0; //zero init
    }
    const char *GetKey() const { return prefix; }
    ClassIsMovableZeroed(RandomUsedFileCandidate)
  };

  //! collected hashes
  AutoArray<CollectedHash> hashes;
  //! collecting hashes from client is in progress (through NetworkServer::InitialSignaturesTest(...))
  bool  _collecting;
  //! maps prefix to the RandomUsedFileCandidate to track how often which asks are sent
  typedef MapStringToClass<RandomUsedFileCandidate, AutoArray<RandomUsedFileCandidate> >  PrefixToCandidate;
  PrefixToCandidate _candidatesMap;

public:
  static const int CollectEndIndex = 0x50be; //mnemo Sobe
  //! number of pbo files loaded
  int banksCount;  // -1 is default value meaning the client has not sent this information
  //! next time for full check (level 1)
  DWORD fullCheckNext;
  //! timeout time to get banksCount and iKnowLevel2 info
  DWORD bankInfoTimeout;

private:
#if DEBUG_SIGNATURES
  RString lastError;
#endif

  void Collecting(bool collect) { _collecting = collect; }
  void Clear() { hashes.Resize(0); }

  // return true when HACK detected
  bool TestLevelHash(CollectedHash &colHash, int index, int colIndex, Temp<char> &hash, int sigVer, int level)
  {
    if (!_collecting && index>=0)
    {
      if ( colIndex != (index % SignaturesCount()) ) 
      {
#if DEBUG_SIGNATURES
        lastError = Format("ColIx(%d)-Ix(%d)", colIndex, index);
#endif
        return true; //hack detected
      }
    }
    switch (sigVer)
    {
    case 1:
      { // hash param is the DataContentHash
        if (hash.Size() != colHash.hash.Size() ||
          memcmp(hash.Data(), colHash.hash.Data(), hash.Size()) != 0) return true; //hack detected
      }
      break;
    case 2:
      {
        switch (level)
        {
        case 0:
        case 1: // hash param is the sigVer2, level=0 hash
          if (colHash.ver2lev0hash.Size()>0)
          {
            if (hash.Size() != colHash.ver2lev0hash.Size() ||
              memcmp(hash.Data(), colHash.ver2lev0hash.Data(), hash.Size()) != 0) 
            {
#if DEBUG_SIGNATURES
              lastError = "DiffLev0Hash";
#endif
              return true; //hack detected
            }
          }
          else
          { // collect ver2lev0hash
            colHash.ver2lev0hash = hash;
          }
          break;
        case 2: // hash param is the sigVer2, level=2 hash
          if (colHash.ver2lev2hash.Size()>0)
          {
            if (hash.Size() != colHash.ver2lev2hash.Size() ||
              memcmp(hash.Data(), colHash.ver2lev2hash.Data(), hash.Size()) != 0) 
            {
#if DEBUG_SIGNATURES
              lastError = "DiffLev2Hash";
#endif
              return true; //hack detected
            }
          }
          else
          { // collect ver2lev2hash
            colHash.ver2lev2hash = hash;
          }
          break;
        default:
#if DEBUG_SIGNATURES
          lastError = "UnknownLevel";
#endif
          return true; // unrecognized level
        }
      }
      break;
    default: 
#if DEBUG_SIGNATURES
      lastError = "UnknownVer";
#endif
      return true; //unknown version
    }
        return false; //ok
      }
  // Test the hash against the collectedHash with the given fileName
  // return true when HACK detected
  bool Test(RString fileName, int index, Temp<char> &hash, int sigVer, int level)
  {
    if (index>=0)
    {
      if (!SignaturesCount())
        return true; //HACK, unexpected message with index set during collecting phase
      index = index % SignaturesCount();
      if (hashes[index].fileName != fileName)
      {
#if DEBUG_SIGNATURES
        lastError = "Ix&File";
#endif
        return true; //HACK, index and fileName does not match
      }
      return TestLevelHash(hashes[index], index, index, hash, sigVer, level);
    }
    else 
    {
      for (int i=0; i<hashes.Size(); i++)
      {
        if (hashes[i].fileName==fileName)
        {
          return TestLevelHash(hashes[i], index, i, hash, sigVer, level);
        }
      }
    }
#if DEBUG_SIGNATURES
    lastError = Format("SigNotFound(%d)",index);
#endif
    return true; //hack detected (sent signature not found)
  }

  // Test the hash against the collectedHash with the given bank Prefix
  // return true when HACK detected
  bool Test(int index, RString prefix, Temp<char> &dataContentHash, Temp<char> &levelHash, int sigVer, int level)
  {
    // use index when set
    if (index>=0)
    {
      if (!SignaturesCount())
      {
#if DEBUG_SIGNATURES
        lastError = "NoStoredSig";
#endif
        return true; //HACK, unexpected answer with index set during collecting phase
      }
      // cut index to fit within range using modulo
      index = index % SignaturesCount();
      // Test prefix
      if (hashes[index].prefix.IsEmpty())
        hashes[index].prefix = prefix; // set it at last
      else if (hashes[index].prefix!=prefix)
      {
#if DEBUG_SIGNATURES
        lastError = Format("DiffPrefix(%s,%s)", cc_cast(hashes[index].prefix), cc_cast(prefix));
#endif
        return true; //HACK, prefixes differ
      }
      // Test dataContentHash
      Temp<char> &colDataContentHash = hashes[index].hash;
      if ( !colDataContentHash.Size() )
        colDataContentHash = dataContentHash; //possible collect it (but maybe empty again)
      else if ( 
                dataContentHash.Size() && 
               (dataContentHash.Size()!=colDataContentHash.Size() || (memcmp(dataContentHash.Data(), colDataContentHash.Data(), dataContentHash.Size()) != 0) )
              )
      {
#if DEBUG_SIGNATURES
              lastError = "DiffDCH";
#endif
              return true; //dataContentHashes differ
      }
      // Test levelHash
      if (levelHash.Size())
        return TestLevelHash(hashes[index], index, index, levelHash, sigVer, level);
      else
        return false; //OK, nothing else to check
    }
    else
    { // this happen only for clients before rev.78326
      if (levelHash.Size()>0)
      {
        for (int i=0; i<hashes.Size(); i++)
        {
          if (hashes[i].prefix==prefix)
          {
            return TestLevelHash(hashes[i], i, i, levelHash, sigVer, level);
          }
        }
      }
    }
#if DEBUG_SIGNATURES
    lastError = "NoIxSet";
#endif
    return true; //HACK, index should be set in clients FileSignatureAnswers
  }

  // it search the hash
  int ArrayIx(RString fileName, Temp<char> &hash, int sigVer, int level)
  {
    for (int i=0; i<hashes.Size(); i++)
    {
      Temp<char> *colHash = NULL;
      if (sigVer==1) colHash = &hashes[i].hash;
      else switch (level)
      {
        case 0:
        case 1: colHash = &hashes[i].ver2lev0hash; break;
        case 2: colHash = &hashes[i].ver2lev2hash; break;
      }
      if ( hashes[i].fileName==fileName || 
        (colHash!=NULL && hash.Size() == colHash->Size() &&
        memcmp(hash.Data(), colHash->Data(), hash.Size()) == 0)
        )
      {
#if DEBUG_SIGNATURES
        if (hashes[i].fileName==fileName)
          lastError = "SameFile";
        else
          lastError = "SameSig";
#endif
        return i;
      }
    }
    return -1; //not found
  }

public:
  CollectedHashes() : _collecting(false), banksCount(-1)
  {
    fullCheckNext = GlobalTickCount() + 40000 + toInt(40000 * GRandGen.RandomValue()); // initial delay 40 s - 80 s
    bankInfoTimeout = 0; // no timeout yet
  }

  void StartCollecting()
  {
    Clear();
    Collecting(true);
  }

  //! In spite of we are collecting hashes, the SignaturesCount suits better
  int SignaturesCount() const
  {
    if (_collecting) return 0;
    else 
    {
      return hashes.Size();
    }
  }

  //! AddHash based on fileName [DataSignatureAnswer]
  //! return true when HACK detected
  bool AddHash(int index, RString fileName, Temp<char> &hash, int sigVer, int level, PendingSignatureChecks &pendingChecks)
  {
#if DEBUG_SIGNATURES
    lastError = "";
#endif
    //RptF("AddHash: %s, ix=%d, ver=%d, lev=%d", cc_cast(fileName), index, sigVer, level);
    if (index<0)
    {
      if (!_collecting) return Test(fileName, index, hash, sigVer, level); //fileName based Test
      else  // we are collecting
      {
        int ix = ArrayIx(fileName, hash, sigVer, level);
        if (ix>=0) 
        {
#if DEBUG_SIGNATURES
          lastError = RString("Duplo-") + lastError;;
#endif
          return true; //hack detected (the same answer sent second time)
        }
        // new hash
        switch (sigVer)
        {
        case 1: hashes.Add(CollectedHash(fileName, hash)); break;
        case 2:
          {
            switch (level)
            {
            case 0:
            case 1:
              {
                int ix = hashes.Add(CollectedHash());
                CollectedHash &colHash = hashes[ix];
                colHash.fileName = fileName;
                colHash.ver2lev0hash = hash;
      }
              break;
            case 2:
              {
                int ix = hashes.Add(CollectedHash());
                CollectedHash &colHash = hashes[ix];
                colHash.fileName = fileName;
                colHash.ver2lev2hash = hash;
                // we are collecting and asynchronous level=2 check is used. Add more time to pending signature check
                pendingChecks.UpdateInitialCheckTimeout();
              }
              break;
            default: return true; //unrecognized level
            }
          }
          break;
        default: return true; //unrecognized version
        }
      }
      return false; //ok
    }
    else
    {
      if (_collecting)
      {
        if (index==CollectEndIndex)
        {
          _collecting = false;
          // when there are too few initial DataSignatureAnswers during "collecting" phase,
          // the client is supposed to be cheating. The limit is chosen conservative enough (30 banks).
          const int MinimalBankCount = 30;
          if ( SignaturesCount() < MinimalBankCount )
          {
#if DEBUG_SIGNATURES
            lastError = "MinSigCount";
#endif
            return true;
          }
          if (UpdatePlayerBankCount(SignaturesCount()))
            return true; //hacked
        }
        else 
        {
#if DEBUG_SIGNATURES
          lastError = "50BE-Expected";
#endif
          return true; // CollectEndIndex was expected! => HACK?
        }
      }
      return Test(fileName, index, hash, sigVer, level); //fileName based Test
    }
  }
  //! AddHash based on bank prefix
  //! return true when HACK detected
  bool AddHash(int index, RString prefix, Temp<char> &dataContentHash, Temp<char> &levelHash, int sigVer, int level)
  {
#if DEBUG_SIGNATURES
    lastError = "";
#endif
    //RptF("AddHash: index=%d prefix=%s, ver=%d, lev=%d", index, cc_cast(prefix), sigVer, level);
    if (_collecting) 
    {
#if DEBUG_SIGNATURES
      lastError = "Collecting";
#endif
      return true; //this type of answers is not expected during initial collecting phase
    }
    return Test(index, prefix, dataContentHash, levelHash, sigVer, level); //bank prefix based Test
  }

  //! Get the level to use for the next signature check
  int ActualCheckLevel()
  {
    DWORD time = GlobalTickCount();
    if (time > fullCheckNext)
    {
      fullCheckNext = time + 80000 + toInt(80000 * GRandGen.RandomValue()); // 80 s - 160 s
      // note: full check (level=1) is broken, World::CheckHash is very slow and never completes, use 2 instead
      return 2; // deep check (level 1)
    }
#if 1
    DoAssert(banksCount>0); // the bank count should already be known
    // FIX: level=2 checks are now processed async
    if ( rand()%4==0 && banksCount>0)
      return 2;
    else
    return 0; // shallow check (hash is taken from stored bank position, not recomputed from its content)
#else
    return 0;  //HOTFIX: Level 2 checks are too long to compute for both clients and server (async on clients and precomputation on servers should be implemented!)
#endif
  }

  RString GetLastError() const
  {
#if DEBUG_SIGNATURES
    return lastError;
#else
    return RString("");
#endif
  }

  //! Check timeout guarding whether client send her banksCount
  void CheckTimeouts(CheckSignatureTimeoutFunc &func)
  {
    if ( bankInfoTimeout && (bankInfoTimeout < GlobalTickCount()) )
    {
      (void)func();
    }
  }
 
  //! It calls the GWorld->RandomUsedFile more times to get the file which is not asked too often
  RString RandomUsedFile(Vector3Par pos, int level);

  bool IsCollecting() const { return _collecting; }

  // returns true when hack was detected
  bool UpdatePlayerBankCount(int bankCount);

#if DEBUG_SIGNATURES
  const AutoArray<CollectedHash> &Hashes() { return hashes; }
#endif
};

struct AsyncDBStorage
{
  // asynchronous save to the central database
  struct SaveThreadContext : public RefCount
  {
    SIGNALED_OBJECT _thread;
    RString _url;
    RString _request;
	RString _uid;
    Ref<SaveThreadContext> _prevContext;

    SaveThreadContext() : _thread(NoInitHandle) {}
  };
  Ref<SaveThreadContext> _lastContext;

  bool DBSave(RString baseURL, RString playerUID, RString request);
  bool DBQueue(RString baseURL, RString playerUID, RString request);
  bool DBKill(RString baseURL, RString playerUID);
  bool DBItemDrop(RString baseURL, RString request);

  static DWORD WINAPI DBSaveThreadFunction(void *ctx);
  static DWORD WINAPI DBQueueThreadFunction(void *ctx);
  static DWORD WINAPI DBKillThreadFunction(void *ctx);
  static DWORD WINAPI DBItemDropThreadFunction(void *ctx);

  // asynchronous load to the central database
  struct LoadThreadContext : public RefCount
  {
    SIGNALED_OBJECT _thread;
    RString _url;
    // load possible when all saves completes
    Ref<SaveThreadContext> _prevContext;
    // the result of the request
    RString _result;
    int _err;
    // the script executed when request completes
    Ref<GameDataCode> _code;
    int _playerId;
    RString _playerUID;
    RString _name;

    LoadThreadContext() : _thread(NoInitHandle) {}
  };
  AutoArray< Ref<LoadThreadContext> > _loadContexts;
  void DBLoad(RString baseURL, int playerId, RString playerUID, RString name, GameDataCode *code);
  static DWORD WINAPI DBLoadThreadFunction(void *ctx);
  void DBCheckLoaded();
};



//! Server info about player
struct NetworkPlayerInfo : public AsyncDBStorage, public RefCount, private NoCopy
{
  //! DirectPlay ID of player (client)
  int dpid;
  //! Voice over net ID of player (client)
  int dvid;

  //! chat channel player transmits
  int channel;  

  //! some clients are "local" (no bandwidth control is needed)
  bool _isLocal;
  //! name of player
  RString name;
  //! client state for given player
  NetworkClientState clientState;
  //! message queue (candidates for aggregation)
  NetworkMessageQueue _messageQueue, _messageQueueNonGuaranteed;

  /// messages waiting to be sent to clients
  /** messages describing audio or visual effects, no delivery is guaranteed */
  RefArray<DelayedMessageInfo> _delayedMessages;

  /// list of objects which are marked as updating for this player
  typedef TListBidir<NetworkPlayerObjectInfo,TLinkBidirD,SimpleCounter<NetworkPlayerObjectInfo>> UpdatingList;
  UpdatingList _updating;

  void markAsUpdating(NetworkPlayerObjectInfo *info, UpdatingList &updatingNew)
  {
    //DoAssert(updatingNew.CheckIntegrity());
    //DoAssert(_updating.CheckIntegrity());
    if (info->IsInList())
    {
      //DoAssert(_updating.Contains(info));
      _updating.Delete(info);
    }
    updatingNew.Insert(info);
    //DoAssert(_updating.CheckIntegrity());
    //DoAssert(updatingNew.CheckIntegrity());
  }

  void markAsUpdating(NetworkPlayerObjectInfo *info)
  {
    //DoAssert(_updating.CheckIntegrity());
    if (!info->IsInList()) _updating.Insert(info);
    //DoAssert(_updating.CheckIntegrity());
  }

  //! ping statistics
  CalculateMinMaxAvg _ping;
  //! bandwidth statistics
  CalculateMinMaxAvg _bandwidth;
  //! desync statistics
  CalculateMinMaxAvg _desync; 

  //! player's person
  NetworkId person;

  SRef<ClientInput> _clientInput;

  //! player occupy private slot
  bool privateSlot;

  //! client has actual (valid) mission pbo file
  bool missionFileValid;

  //! client has actual (valid) mission pbo file
  bool paramFileValid;

  //! kick-off already requested
  bool kickedOff;

  //! Connection loosing (no message for 10 seconds) was already reported to other users
  bool connectionProblemsReported;

  //! Player was joined using JIP (used to distinguish between normal JIP and JIP forced after MP Load)
  bool _jipProcessed;

  //@{ private and public socket address + port
  int _publicAddr;
  int _publicPort;

  int _privateAddr;
  int _privatePort;
  //@}

  /// how many times wrong login password was given
  int _loginFailedCount;
  /// how long is login disabled because of two many wrong attempts
  DWORD _loginDisabledUntil;

  /// Signature checks
  PendingSignatureChecks signatureChecks;

  /// Hashes collected during initial signature test
  CollectedHashes hashes;
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
  SteamValidation _steamValidate;
#endif
  

  /// CDKey check
  CDKeyCheck cdKeyCheck;

  //! time when next regular check will take place
  DWORD regularCheckNext;
  //! order of the regular check
  DWORD regularCheckOrder;

  //! position of camera on given client
  Vector3 cameraPosition;
  //! time of last update of cameraPosition
  Time cameraPositionTime;

#if _VBS3
  //! Direction of camera on given client
  Vector3 cameraDirection;
  //! FOV on given client
  float cameraFov;
#endif

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  //! next message of the day index (-1 if done)
  int motdIndex;
  //! next message of the day time
  UITime motdTime;
#endif

  int nextQuestionId;

#if _ENABLE_MP
  MuteListMessage muteList;
#endif

  Time joined;

  /// key for encoding of CD Key validation communication
  RString challenge;

  /// challenge to be signed by the PlayerId certiticate
  long long certChallenge;

  // Serialize for MP Save/Load
  LSError Serialize(ParamArchive &ar);
//}

  /* @return value: true when client is not able to send through network 
                   (because it loads the landscape, mission, do some time exhaustive work,...)
     It detects the start and end of the busy state 
     At start: it remembers the start time
     At end:   it adds the time spent to all pending signature timeouts
  */
  bool ClientStateBusy();
  // helper variable for ClientStateBusy function
  DWORD _busyStartTime; // when zero, the client is NOT busy. The start of busy state otherwise.

#if DEBUG_SIGNATURES
  RString errorInfo;
#endif
};

//! map person to unit in server database
struct PersonUnitPair
{
  //! id of person
  NetworkId person;
  //! id of unit
  NetworkId unit;
};
TypeIsMovable(PersonUnitPair)

//! Single vote in votings
struct Vote
{
  //! DirectPlay ID of player
  int player;
  //! Voted value
  AutoArray<char> value;

  //! Equality operator
  /*!
  \param val voted value
  \param valueSize size of voted value
  */
  bool HasValue(const char *val, int valueSize) const;
};
TypeIsMovable(Vote)

//! Single voting
class Voting : public AutoArray<Vote>
{
  friend class Votings;
  typedef AutoArray<Vote> base;

protected:
  /// Unique id of voting
  AutoArray<char> _id;
  /// Threshold when voting is valid
  float _threshold;
  /// Check if more than half players has the same value
  bool _selection;

  /// when there are to little players, assume other can still connect
  int _minVotePlayers;
public:
  //! Add new vote
  /*!
  \param player DirectPlay ID of voting player
  \param value voted value (NULL if none)
  \param valueSize size of voted value
  */
  int Add(int player, const char *value = NULL, int valueSize = 0);

  //! Return ID of Voting
  const AutoArray<char> &GetID() const {return _id;}

  //! Check if voting is valid
  bool Check(const AutoArray<PlayerIdentity> &identities) const;
  
  /// remote voting of any player which is no longer here
  void OnPlayerDeleted(int dpid);
  //! Get voted value
  /*!
  \param n number of votes
  */
  const AutoArray<char> *GetValue(int *n = NULL, int *n2=NULL) const;

  struct VoteInfo
  {
    const Vote *vote;
    int count;
    
    ClassIsSimple(VoteInfo)
  };
  
  static int CompareVoteInfo(const VoteInfo *v1, const VoteInfo *v2)
  {
    return v2->count-v1->count;
  }
  
  /// get ordered info about voting
  void GetOrder(AutoArray<VoteInfo> &result) const;
  //! Equality operator
  /*!
  \param id unique identification of voting
  \param idSize size of unique identification
  */
  bool HasID(const char *id, int idSize) const;
};
TypeIsMovable(Voting)

//! Voting system
class Votings : public AutoArray<Voting>
{
  typedef AutoArray<Voting> base;

public:
  //! Add new vote
  /*!
  \param server hosting server
  \param id unique identification of voting
  \param idSize size of unique identification
  \param threshold threshold when voting is valid
  \param player DirectPlay ID of voting player
  \param value voted value (NULL if none)
  \param valueSize size of voted value
  */
  void Add(
    NetworkServer *server, const char *id, int idSize, float threshold, int player, const char *value = NULL, int valueSize = 0,
    bool selection = false, int minVotePlayers=-1
  );

  /// find given voting
  const Voting *Find(int id) {return Find((const char *)&id,sizeof(id));}

  /// find given voting
  const Voting *Find(const char *id, int idSize) const;

  /// find index of a corresponding voting
  int FindIndex(const char *id, int idSize) const;

  /// delete voting
  void DeleteVoting(int id) {return DeleteVoting((const char *)&id,sizeof(id));}

  /// delete given voting
  void DeleteVoting(const char *id, int idSize);
  
  /// remote voting of any player which is no longer here
  void OnPlayerDeleted(int dpid);
  
  //! Check all votings (apply if satisfied)
  /*!
  \param server hosting server
  */
  void Check(NetworkServer *server);

  /// apply voting, if there are no votes return false
  bool Apply(NetworkServer *server, int id) {return Apply(server,(const char *)&id,sizeof(id));}
  /// apply voting, if there are no votes return false
  bool Apply(NetworkServer *server, const char *id, int idSize);
};

/// Custom file slot
struct CustomFilePaths
{
  RString _src;
  RString _dst;
  unsigned int _crc;
  int     _len;
  CustomFilePaths() {}
  CustomFilePaths(RString src, RString dst, unsigned int crc, int len) : _src(src), _dst(dst), _crc(crc), _len(len) {}

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat
    (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
    );
  TMError TransferMsg(NetworkMessageContext &ctx);
};
TypeIsMovable(CustomFilePaths)

TypeIsMovable(TransferFileMessage)


//! pending download of squad information
class CheckSquadObject
{
friend class NetworkServer;

protected:

  long long _challenge;
  //! identify player 
  PlayerIdentity _identity;

  //! custom files subsets of which is can be sent to some clients
  AutoArray<CustomFilePaths> _customFileList;
  //! TransferFile messages for each custom file (created only when needed)
  AutoArray<AutoArray<TransferFileMessage> > _customFileMessages;
  //! custom files which given clients wants to sent
  struct CustomFilesForClient
  {
    int  _dpnid;   // client identification
    enum CFTState {CFSWaitingToList, CFSSendingFiles, CFSDone} _state;
    AutoArray<int> _filesWanted;
    //{ Files Transfer members 
    int _fileNo;          // index into _filesWanted, which file is currently being transfered
    int _fragmentNo;      // number of transfered fragments
    int _allFragmentCount; // used for progress bar
    int _allFragmentPos;   // used for progress bar
    DWORD _nextSendTime;  // time to send next bunch of TransferFile messages
    //}
    CustomFilesForClient() {}
    CustomFilesForClient(int dpnid) : _dpnid(dpnid), _state(CFSWaitingToList), _fileNo(0), _fragmentNo(0) , 
      _allFragmentPos(0), _allFragmentCount(200) //200 - approx 100kB before the valid count is not received
    {
      _nextSendTime=::GetTickCount();
    }
    ClassIsMovableZeroed(CustomFilesForClient)
  };
  AutoArray<CustomFilesForClient> _customFilesProgress;

  enum State {SendingCustomFiles,AllDone};
  //! state of download
  State _state;
  //! address of proxy server
  RString _proxy;

public:
  //! start download
  CheckSquadObject(PlayerIdentity &identity, RString proxy, NetworkServer *server);
  //! destruct - wait until current download step is terminated
  ~CheckSquadObject();
  //! check if download is terminated
  bool IsDone(bool &canSendCustomFiles, NetworkServer *server);
  bool IsPlayer(int dpnid) { return _identity.dpnid==dpnid; };

  //! custom files transfer to all clients. Returns true when no custom files are being sent.
  bool StartSendCustomFilesList(NetworkServer *server);

  //! store content of NMTCustomFilesWanted message
  void SetCustomFilesWanted(int dpnid, const AutoArray<int> &filesWanted);

  State GetState() const { return _state; }

protected:
  void CreateCustomFilesList();
};

struct UpdateObjectInfo;
class LiveStatsUpdate;
static const int SessionAttributesCount = 12;

enum HackType
{
  HTUnsignedData,
  HTHackedData
};

#if _ENABLE_DEDICATED_SERVER

#define SERVER_EVENT_ENUM(type, prefix, XX) \
  XX(type, prefix, DoubleIdDetected) /* two users with the same id detected */ \
  XX(type, prefix, OnUserConnected) /* reaction to connected user */ \
  XX(type, prefix, OnUserDisconnected) /* reaction to disconnected user */ \
  XX(type, prefix, OnHackedData) /* reaction to pbo files manipulation */ \
  XX(type, prefix, OnUnsignedData) /* reaction to unsigned file */ \
  XX(type, prefix, RegularCheck) /* called regullary for each user  */ \

#ifndef DECL_ENUM_SERVER_EVENT
#define DECL_ENUM_SERVER_EVENT
DECL_ENUM(ServerEvent)
#endif
DECLARE_ENUM(ServerEvent, SE, SERVER_EVENT_ENUM)

#endif

typedef struct qr2_buffer_s *qr2_buffer_t;
typedef struct qr2_keybuffer_s *qr2_keybuffer_t;

//((EXE_CRC_CHECK
#if !_SUPER_RELEASE

/// info about exe segment to investigate
struct ExeSegmentInfo
{
  int offset;
  int size;
};
TypeIsSimple(ExeSegmentInfo);

/// the status of the client investigation
struct CompareExeClient
{
  int _dpid;
  int _answer;
  bool _answered;
};

/// Find the exe difference for given clients
struct CompareExeInvestigation
{
  /// list of segments to investigate
  AutoArray<ExeSegmentInfo> _stack;
  /// list of modified segments
  AutoArray<ExeSegmentInfo> _modified;
  /// item on the top of stack is already investigating
  bool _asked;
  /// the first client summary
  CompareExeClient _client1;
  /// the second client summary
  CompareExeClient _client2;

  /// prepare structure to investigation
  void Init(int dpid1, int dpid2);
  /// react to the answer
  void OnAnswered(int dpid, int answer);
};

#endif
//))EXE_CRC_CHECK

template <>
struct ImplicitMapTraits< RefD<NetworkObjectInfo> >
{
  static RefD<NetworkObjectInfo> null;                 ///< Empty value (can be passed as return value).
  static RefD<NetworkObjectInfo> zombie;               ///< Removed value (internal usage only).

};

static inline int operator % (const NetworkId &id, unsigned i)
{
  unsigned idl = (id.creator.CreatorInt()<<20)+id.id;
  return idl%i;
}

//! Delayed CreateMarker messages, init messages
struct JIPMarkerInfo
{
  Ref<NetworkMessage> _marker;
  int _channel;
  int _roleIndex;
};
TypeIsMovableZeroed(JIPMarkerInfo)

struct ObjectsSpatialLayer: public RefCount
{
  typedef std::function<void (NetworkObjectInfo *oInfo)> Function;

  virtual void Add(NetworkObjectInfo *oInfo) = 0;
  virtual bool Remove(Vector3Par oldPos, NetworkObjectInfo *oInfo) = 0;
  virtual void ForEach( Vector3 cameraPos, const Function &function ) = 0;
  ObjectsSpatialLayer(){}
  virtual ~ObjectsSpatialLayer(){}

};

struct ObjectsSpatialLayerWithDistance: public ObjectsSpatialLayer
{
  float _distance;

  ObjectsSpatialLayerWithDistance(float distance):_distance(distance){}
};

//! Network Server
class NetworkServer : public NetworkComponent
{
  friend struct DelayedMessageInfo;
  friend struct NetworkObjectInfo;

protected:
  //! low-level server implementation
  SRef<NetTranspServer> _server;

  //! server for peer to peer mission sharing
#ifdef PEER2PEER
  SRef<PeerToPeerServer> _p2pServer;
#endif

#if _USE_BATTL_EYE_SERVER
  /// BE Integration
  class BattlEye
  {
    //! BEVersion is used in BE init call in both Win32 and Linux sections
    static const int CurrentBEIntegrationVersion = 0;
    static const int MinBEVerReq = 176;
  public:
    enum BEInitFailedReason { BEReasonOther, BEReasonNotInstalled };
    BEInitFailedReason _initFailedReason;
  protected:
    /// dll handle
    HMODULE _library;
    bool _initialized;

    //@{
    /// callback functions
    typedef void __cdecl PrintMessageF(const char *message);
    typedef void __cdecl KickPlayerF(int player, const char *reason);
    typedef void __cdecl ExecuteCommandF(const char *command);
    static void __cdecl PrintMessage(const char *message);
    static void __cdecl KickPlayer(int player, const char *reason);
    static void __cdecl ExecuteCommand(const char *command);
    //@}

    //@{
    /// dll functions
    typedef bool __cdecl ExitF();
    typedef bool __cdecl RunF();
    typedef void __cdecl CommandF(const char *command);
    typedef void __cdecl AddPlayerF(int player, unsigned long address, unsigned short port, const char *name);
    typedef void __cdecl ChangePlayerStatusF(int player, int status);
    typedef void __cdecl ReceivedPlayerGUIDF(int player, void *guid, int guidlen);
    typedef void __cdecl PlayerGUIDIsValidF(int player, void *ownerguid, int guidlen);
    typedef void __cdecl ReceivedPlayerMessageF(int player, int channel, const char *message);
    typedef void __cdecl LoadedMissionF(const char *name);

//      XXX(AddWeapon, const char *netId, const char *type) \
//      XXX(AddMagazine, const char *netId, const char *type) \

    #define BE_CALLBACKS(XXX) \
      XXX(UpdatePosition, const char *netId, float x, float y, float z, float dirX, float dirY, float dirZ, float upX, float upY, float upZ, float asideX, float asideY, float asideZ ) \
      XXX(UpdateDamage, const char *netId, float damage ) \
      XXX(PublicVariable, const char *name, const char *data) \
      XXX(AttachObject, const char *netId, const char *netIdAttachTo, int memoryIndex, float x, float y, float z) \

    #define BE_FUNCTION_TYPE(Name, ...) typedef bool __cdecl Name##CallbackF(int player, __VA_ARGS__);
    BE_CALLBACKS(BE_FUNCTION_TYPE)

    struct GamePtrStruct
    {
      char *pstrGameVersion;
      PrintMessageF *_printMessage;
      KickPlayerF *_kickPlayer;
      ExecuteCommandF *_execCommand;
    };
    GamePtrStruct _gameParams;

    struct BEPtrStruct
    {
      ExitF *_exit;
      RunF *_run;
      CommandF * _command;
      AddPlayerF * _addPlayer;
      ChangePlayerStatusF * _changePlayerStatus;
      ReceivedPlayerGUIDF * _receivedPlayerGUID;
      PlayerGUIDIsValidF * _playerGUIDIsValid;
      ReceivedPlayerMessageF * _receivedPlayerMessage;
      LoadedMissionF * _loadedMission;

      #define BE_FUNCTION_PTR(Name, ...) Name##CallbackF *_callback##Name;
      BE_CALLBACKS(BE_FUNCTION_PTR)
    };
    BEPtrStruct _callbacks;

    typedef bool __cdecl InitF(int iIntegrationVersion, GamePtrStruct *ppvGamePtrTable, BEPtrStruct *ppvBEPtrTable);
    typedef int __cdecl GetVerF();
    //@}

  public:
    BattlEye();
    ~BattlEye();

    bool IsEnabled() const {return _library != NULL;}

    bool Init();
    void Done();

    bool Run(const NetworkServer *server);
    void Command(const char *command);

    //! BEServer.AddPlayer() needs to be called as soon as a client is connected to the server (probably right after the initial join packet - as soon as the client is registered/accepted on the server's internal player list).
    void AddPlayer(int player, unsigned long address, unsigned short port, const char *name);

/*! BEServer.RemovePlayer() will be replaced by BEServer.ChangePlayerStatus().
   - BEServer.ChangePlayerStatus() will be called with status = 1 when the player goes in-game (where AddPlayer() was previously called).
   - BEServer.ChangePlayerStatus() will be called with status = 0 when the player goes back to the lobby (where RemovePlayer() was previously called).
   - BEServer.ChangePlayerStatus() will be called with status = -1 when the player disconnects from the server.
*/
    void PlayerGoesInGame(int player);
    void PlayerGoesInLobby(int player);
    void PlayerDisconnected(int player);

    //! BEServer.ReceivedPlayerGUID() will be called as soon as the GameSpy cd-key hash is received from the client (after connecting).
    void PlayerGUIDReceived(int player);
    void PlayerGUIDIsValid(int player);
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
    void PlayerGUIDReceived(int player, const CSteamID &id);
    void PlayerGUIDIsValid(int player, const CSteamID &ownerId);
#endif

    #define BE_WRAP_CALLBACK(Name, ...) bool __cdecl Name##Callback(int player, __VA_ARGS__);
    BE_CALLBACKS(BE_WRAP_CALLBACK)

    //! BEServer.ReceivedPlayerMessage() will be called whenever a client sends an in-game message, so the BE Server can still 
    //!  log the chat to the console (it extracted it from the network traffic before, but that won't be possible anymore after the network traffic scrambling/encryption is introduced). 
    void PlayerChatReceived(int player, int channel, const char *message);

    void LoadedMission(const char *name);

  protected:
    void CleanUp()
    {
      _library = NULL;
      _callbacks._exit = NULL;
      _callbacks._run = NULL;
      _callbacks._command = NULL;
      _callbacks._addPlayer = NULL;
      _callbacks._changePlayerStatus = NULL;
      _callbacks._receivedPlayerGUID = NULL;
      _callbacks._playerGUIDIsValid = NULL;
      _callbacks._receivedPlayerMessage = NULL;
      _callbacks._loadedMission = NULL;
      #define BE_FUNCTION_INIT(Name, ...) _callbacks._callback##Name = NULL;
      BE_CALLBACKS(BE_FUNCTION_INIT)
      _initialized = false;
    }
    int GetBEVersion(HMODULE library) const;
  };
   BattlEye _beIntegration;
  bool BattlEyeEnabled() const
  {
    // for internal version we want to perform the processing even without the BE to verify it is working fine
    return _ENABLE_CHEATS || _beIntegration.IsEnabled();
  }
#else
  bool BattlEyeEnabled() const {return false;}
#endif
  //! list of players (server specific info)
  RefArray<NetworkPlayerInfo> _players;
  //! list of network objects (actual state)

  static NetworkId GetIdFromNetworkObjectInfo(const RefD<NetworkObjectInfo> &info) {return info->id;}

  typedef ImplicitMap<NetworkId, RefD<NetworkObjectInfo>, GetIdFromNetworkObjectInfo > ObjectsMap;

  struct ObjectsSpatialLayerKeyTraits
  {
    typedef float KeyType;
    static bool IsEqual(KeyType a, KeyType b) {return a==b;}
    static KeyType GetKey(const ObjectsSpatialLayerWithDistance *a) {return a->_distance;}
  };

  struct ObjectsSpatialIndex
  {
    typedef NetworkId KeyType;
    typedef RefD<NetworkObjectInfo> TargetType;

    /// objects in bubbles stored in layers based on bubble size
    FindArrayKey<Ref<ObjectsSpatialLayerWithDistance>,ObjectsSpatialLayerKeyTraits> _layers;
    /// objects with no position or no max distance are not held in layers
    Ref<ObjectsSpatialLayer> _noLayer;
    /// all objects in a hash map for easy bulk access
    ObjectsMap _all;

    ObjectsSpatialLayer &Distance(float dist);

    bool get ( KeyType key, TargetType &result ) const
    {
      return _all.get(key,result);
    }

    TargetType *get ( KeyType key )
    {
      return _all.get(key);
    }
    const TargetType *get ( KeyType key ) const
    {
      return _all.get(key);
    }

    bool put ( TargetType value );

    void addToLayer( TargetType value )
    {
      auto &layer = Distance(value->_updInfo._updateDistance);
      layer.Add(value);
    }
    void removeFromLayer( Vector3Par oldPos, float oldDist, TargetType value )
    {
      auto &layer = Distance(oldDist);
      DoVerify(layer.Remove(oldPos,value));
    }

    void removeKey ( KeyType key );


    void reset ();

    template <class Function>
    void forEach(const Function &function)
    {
      _all.forEach(function);
    }

    template <class Function>
    void forEach(const Function &function) const
    {
      _all.forEach(function);
    }

    void forEachInBubble(Vector3 cameraPos, const ObjectsSpatialLayer::Function &function);
    void onPosChanged(NetworkObjectInfo *oInfo, const NetworkObjectInfo::UpdateInfo &oldPos, const NetworkObjectInfo::UpdateInfo &newPos);

    unsigned card() const {return _all.card();}


    ObjectsSpatialIndex();
  };


  FindArrayKey<int> _indexToPlayerId;

  PlayerIndex GetPlayerIndex(int pid) const {return (PlayerIndex)_indexToPlayerId.FindKey(pid);}

  PlayerIndex CreatePlayerIndex(int pid);

  //typedef ObjectsMap ObjectsContainer;
  typedef ObjectsSpatialIndex ObjectsContainer;

  ObjectsContainer _objects;


  static NetworkId GetIdFromDeletedNetworkObjectInfo(const NetworkDeletedObjectInfo &info) {return info.id;}

  typedef ImplicitMap<NetworkId, NetworkDeletedObjectInfo, GetIdFromDeletedNetworkObjectInfo > DeletedObjectsContainer;

  //! info about objects deleted, until deletion is confirmed by the owner
  DeletedObjectsContainer _deletedObjects;

  //! original name of current mission
  RString _originalName;

  //! no other players are enabled to connect
  bool _sessionLocked;
  
  //! password protected session
  bool _password;

  //! equal mod list required by host
  bool _equalModRequired;

  //! data signatures required
  bool _verifySignatures;

  //! the least buildNo the server accepts
  int _buildNoRequired;

  /// level od CD key security required
  int _secureIdRequired;

  /// validate CD Key using GameSpy SDK
  bool _validateCDKey;

  /// do not report session to GameSpy matchmaking
  bool _doNotReport;

  // InitQR was called (so SimulateQR should not call QR sdk)
  bool _qrInitDone;

  /// allow persistent missions - continue even when no players are connected
  bool _persistent;

  /// destroy body of disconnected players
  bool _destroyDisconnected;

  mutable int _longitude;
  mutable int _latitude;

  bool _longLatAuto;


  struct GeoJobThread
  {
    SIGNALED_OBJECT _thread;
    Future<int> _longitude;
    Future<int> _latitude;
    RString _ip;
    GeoJobThread() : _thread(NoInitHandle) {}
  };
  mutable SRef<GeoJobThread> _geoJob;

  static DWORD WINAPI GeoJobThreadFunction(void *ctx);

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  //! DirectPlay ID of logged gamemaster (AI_PLAYER if no gamemaster)
  int _gameMaster;
  //! selected mission name
  RString _mission;
  //! selected mission cadet / veteran mode
  int _difficulty;

  //! gamemaster is only voted admin (cannot do shutdown etc.)
  bool _admin;

  //! kick off players with duplicate ID
  bool _kickDuplicate;

  //! is server dedicated
  bool _dedicated;
  //! server is asked to restart mission
  bool _restart;
  //! server is asked to reassign players
  bool _reassign;
  //! index of next mission in dedicated server config file
  int _missionIndex;
  //! preloaded dedicated server config file
  ParamFile _serverCfg;

  //@{
  //! Info about network trafic on dedicated server
  float _monitorInterval;
  DWORD _monitorNext;
  int _monitorFrames;
  int _monitorIn;
  int _monitorOut;
  //@}

  DWORD _debugNext;
  float _debugInterval;
  //! switched on debug flags
  FindArrayKey<int> _debugOn;

  //! messages of the day
  AutoArray<RString> _motd;
  //! messages of the day interval
  float _motdInterval;

  //! URL of Query & Reporting master server
  RString _reportingIP;

  /// evaluator for (dedicated) server scripts
  GameState _evaluator;
  /// global variables for _evaluator
  GameDataNamespace _globals;
  AutoArray<RString> _sqfExecQueue;
  AutoArray<int> _sqfExecFrom;
  bool _sqfExecBusy;
  //! event handlers
  RString _eventHandlers[NServerEvent];

#else
  static const bool _dedicated = false;
#endif
//}

  //! time when next ping status should be broadcast
  DWORD _pingUpdateNext;

  //! full path to mission pbo file
  RString _missionBank;

  //! global (static) ban list
  FindArray<RString> _banListGlobal;
  //! local (dynamic) ban list
  FindArray<RString> _banListLocal;

  struct KeyListSecure
  {
    FindArray<RString> keys; // blocked keys list
    AutoArray<char> signature; // signature of a blocked keys list
  } _banListSecure;

  //! actually tested identities (squad competence)
  AutoArray< SRef<CheckSquadObject> > _squadChecks;
  DWORD _squadChecksTimeout;

  //! next available (short) id for player
  int _nextPlayerID;

  //! votings currently in progress
  Votings _votings;

  /// overall game de-sync - aggregated from all clients
  ConnectionDesync _connectionDesync;
  
  //@{ last valid result from _desyncTotal
  float _desyncTotalAvg;
  float _desyncTotalMax;
  //@}

  /// aggregate de-sync stats for all clients
  CalculateMinMaxAvg _desyncTotal;
  
  /// time when _desyncTotal was last averaged 
  DWORD _desyncTotalUpdateNext;
  
  //! threshold for votings
  float _voteThreshold;

  //! address of proxy server
  RString _proxy;

  //! current server state
  NetworkServerState _serverState;
  
  /// how long is the server state stable
  DWORD _serverStateTime;
  
  /// what timeout value was reported to the clients
  int _serverTimeOutSent;

  //! network messages, needed to send to joining player
  AutoArray<ServerQueueItem> _initMessages;

  //! Marker messages, needed to send to joining player
  AutoArray<JIPMarkerInfo> _jipMarkerInfos;

#if _ENABLE_CHEATS
  //! debugging window
  Link<DebugListWindow> _debugWindow;
#endif

  int _totalSlots;

  /// last time when timeleft was reported to matchmaking
  DWORD _timeLeftLastTime;
  /// last value reported to matchmaking
  int _timeLeftLastValue;

  /// last value reported to broadcast / QoS
  int _timeLeftLastValueBcast;

  /// absolute limit of max. players allowed
  int _maxPlayersLimit;

  /// 0 for enable, 1 for disable (configurable in server.cfg by disable3rdPerson entry)
  int _disable3rdPerson;

	/// time multiplier for environment
	float _environmentTimeMul;

  //! time synchronized to clients
  DWORD _serverTime;
  DWORD _serverTimeInitTickCount;
  DWORD _serverTimeOfEstimatedEnd;
  int   _secondsToEstimatedEnd;
  static const int ServerTimeUpdateInterval = 300*1000; // 5 minutes
  DWORD _nextServerTimeUpdate;

//((EXE_CRC_CHECK
#if !_SUPER_RELEASE
  CompareExeInvestigation _exeInvestigation;
#endif
//))EXE_CRC_CHECK

#if _VBS2 && _VBS2_LITE
  bool  _memoryWrite;
  DWORD _nextRanUpdate;
#endif

public:
  //! Constructor
  NetworkServer(NetworkManager *parent, RString name, RString password, int maxPlayers, int port, bool isPrivate);
  //! Destructor
  ~NetworkServer();

  int GetSessionPort() const {return _server ? _server->GetSessionPort() : 0;}

  int NextPlayerId() {return _nextPlayerID++;}

  //! Creation of DirectPlay server and MP session
  /*!
  \param port IP port where server communicate
  \param password session password
  */
  bool Init(RString name, RString password, int maxPlayers, int port);
  //! Destroying of MP session and DirectPlay server
  void Done();

  void InitVoice();

  //! Create debugging window
  void CreateDebugWindow();

	//! Kick off given player from game
  void KickOff(int dpnid, KickOffReason reason, const char *reasonStr=NULL);
  //! Ban given player (kick off + add to dynamic ban list)
  void Ban(int dpnid);

  void UpdateBanListSecure(const FindArray<RString> &keys, const Array<char> &signature);

  //! Return if session was created
  bool IsValid() const {return _server != NULL;}

  bool IsDedicatedBotClient(int dpnid) const;

  bool IsLocalClient(int client) const
  {
    // Assume bot client is always local
    return client == BOT_CLIENT;
  }

  void RptFObjectIdJournal(const NetworkId &id);
  void RptFObjectNotFound(const NetworkId &id, const char *msg, int from);
  void RptFObjectNotFound(const NetworkId &id, NetworkMessageType type, int from, const char *context = nullptr);

  void UpdateMaxPlayers(int maxPlayers, int privateSlots);

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  //! Mark server as dedicated
  /*!
  \param config name (path) of config file
  */
  bool SetDedicated(RString config);

  /// add specific server scripting commands to the _evaluator
  void InitEvaluator();
  void QueueExec(int from, RString expression);
  void ProcessQueuedExec();

  void ProfileAnswer(const RString &output);
#endif
//}
  //! force data signatures checking
  void SetVerifySignatures(int reqVersion);

  //! verify signature of the single file based on bank index (DataSignatureCheck)
  void CheckSignature(int dpnid, int fileIndex, int level);

  //! verify signature of the single file based on filename (FileSignatureCheck)
  void CheckSignature(int dpnid, RString filename, RString prefix, int level);

  //! signature check test with index=-1 was started and NetworkPlayerInfo::signatures are to be collected
  void InitSignatureCollecting(NetworkPlayerInfo *info);

  //! List of acceptedKey names in the form key1;key2;key3...
  //! When nonzero param 'maxLen', the return value has at most maxLen+4 chars. When truncated, the last one is ";..."
  RString GetSignaturesList(int maxLen=0);

  //! Get the hash precalculated during server initial banks load
  bool MyHash(Temp<char> &hash, RString prefix, int sigVer, int level);

  //! disable/enable connecting of further clients 
  void LockSession(bool lock = true)
  {
    if (_sessionLocked != lock)
    {
      _sessionLocked = lock;
      UpdateMatchmakingDescription(false);
      _server->UpdateLockedOrPassworded(_sessionLocked, _password);
    }
  }

  //! Retrieves URL of server
  void OnPublicAddressKnown(int ip);

  bool GetURL(char *address, DWORD addressLen);
  int BuildNoRequired() { return _buildNoRequired; }

  int SecureIdRequired() {return _secureIdRequired;}

  //! Creates pbo file for selected mission
  void CreateMission(RString mission, RString world);
  //! Store name of pbo file for selected mission
  void SetOriginalName(RString name) {_originalName = name;}
  // force the _param1, _param2 and _paramsArray to contain undefined values, so InitMission will rewrite these by mission defaults
  void UndefMissionParams();
  //! Initialize mission
  /*!
  - create and send mission header structure (including difficulty settings)
  */
  void InitMission(int difficulty, RString displayName, bool noCopy, RString owner, bool isCampaignMission);

  void KickAllPlayers(int minBuild=0);

  bool DBSave(RString baseURL, int dpnid, RString request);
  void DBLoad(RString baseURL, int playerId, GameDataCode *code);

  void OnDraw();
  void OnSimulate();
  void OnSimulateDone();

  /// time critical network update - need to be called several times in frame
  void FastUpdate();
  void OnCreatePlayer(int player, bool privateSlot, const char *name, unsigned long inaddr);
  InputInterface *GetInputInterface(int player);
  bool OnCreateVoicePlayer(int player);
  void OnMessage(int from, NetworkMessage *msg, NetworkMessageType type);

  void ChangeOwnerFinal( NetworkObjectInfo * oInfo, int from, NetworkId &id );


  void SpawnForPlayer(int dpnid, GameDataCode *code, const GameValue &args);

  void PlayerGUIDIsValid(int player) {
#if _USE_BATTL_EYE_SERVER
    _beIntegration.PlayerGUIDIsValid(player);
#endif
  };
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
  void PlayerGUIDReceived(int player, const CSteamID &id) {
#if _USE_BATTL_EYE_SERVER
    _beIntegration.PlayerGUIDReceived(player, id);
#endif
  }
  void PlayerGUIDIsValid(int player, const CSteamID &ownerId) {
#if _USE_BATTL_EYE_SERVER
    _beIntegration.PlayerGUIDIsValid(player, ownerId);
#endif
  }
#endif

  //! The output format is "creator:id friendlyName"
  RString NetworkIdToNetIdFriendly(const NetworkId &id);

 #if _ENABLE_DEDICATED_SERVER
  void DoReassign();
  void DoReassignDirect();
  void DoMissions(bool admin);
#endif

  /// remove initMessages overridden by this one
  void AddInitAndRemoveOverridden(NetworkMessageType type, NetworkMessage * msg, int **updateI=NULL);
  /// get diagnostics statistics about initMessages
  RString GetAddInitAndRemoveOverriddenStat();
  /// remove initMessages related to a deleted object
  void RemoveInitObjectDeleted(NetworkId obj);

  void AddJIPMarkerInfo(NetworkMessage *marker, int channel, int from); 
  void RemoveJIPMarkerInfo(RString name);

  //! perform regular memory clean-up
  unsigned CleanUpMemory();

  /// called before network shutdown is initiated - can be called multiple times
  void OnClose();

  //! Process received system messages
  void ReceiveSystemMessages();
  //! Process received user messages
  void ReceiveUserMessages();
  //! Destroy all received system messages
  void RemoveSystemMessages();
  //! Destroy all received user messages
  void RemoveUserMessages();
  //! Get position of randomly chosen player
  Vector3 RandomPlayerPos() const;
  //! Perform all active integrity investigations
  void PerformIntegrityInvestigations();

  DWORD SendMsg
  (
    int to, NetworkSimpleObject *object, NetMsgFlags dwFlags
  );


  //! Send single network message
  /*!
  This function determine, whic type of send will be used - local send, agregation or send using DirectPlay
  \param to DirectPlay ID of message receiver
  \param msg message itself
  \param type type of message
  \param dwFlags DirectPlay flags
  */
  DWORD SendMsg
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, NetMsgFlags dwFlags,
    NetworkPlayerObjectInfo *poInfo
  );

  virtual DWORD SendMsg
  (
    int to, NetworkMessage *msg,
    NetworkMessageType type, NetMsgFlags dwFlags
  ) 
  {
    return SendMsg(to, msg, type, dwFlags, NULL);
  }

  Ref<NetMessage> DXSendMsg(int to, NetworkMessageRaw &rawMsg, DWORD &msgID, NetMsgFlags dwFlags, const Ref<NetMessage> &dependOn);

  //! Retrieve list of players
  void GetPlayers(AutoArray<NetPlayerInfo> &players);

  /// ask for item UI invocation
  void UseItem(Person *person, InventoryItem *item);

  //! Return connection quality to server
  ConnectionDesync GetConnectionDesync() const {return _connectionDesync;}
  
  NetworkMessageFormatBase *GetFormat(/*int client, */int type);
  const NetworkMessageFormatBase *GetFormat(/*int client, */int type) const;

  void OnChangedOnPlayerConnecting(GameValuePar codeValue);

  //! Set current server state and send to clients
  void SetServerState(NetworkServerState state);
  //! Return client state for given player
  NetworkClientState GetClientState(int dpnid);
  //! Return name for given player
  RString GetPlayerName(int dpid);
  //! Return camera position for given player
  Vector3 GetCameraPosition(int dpid);

  NetworkId GetPlayerPerson(int dpid);
  Object *GetPlayerTarget(int dpid);

  void ServerSelectPlayer(int dpid, const NetworkId &id);

  /// Return the IP address of the connected client
  bool GetClientAddress(int client, sockaddr_in &address);

  NetworkObject *GetObject(const NetworkId &id);

  //! Unregister all objects
  void DestroyAllObjects();

  //! Send mission pbo file to clients
  /*!
  \param dpnid client, server will send mission to (0 for all clients)
  */
  void SendMissionFile(int dpnid = 0);

  //! Calculate bandwidth estimation to given client
  int GetBandwidthEstimation(int dpnid, int nPlayers);

  const char *GetDebugName() const {return "Server";}

  RString GetDiagText(const NetworkId &id) const;


  //! Client has sent her bankCount. Detect whether it differs to the previously sent value.
  void UpdatePlayerBankCount(NetworkPlayerInfo *info, int bankCount);
  //! Report Wrong signatures (write also a line inside DS log window)
  // @param report used for additional info
  void ReportWrongSignatures(int from, RString filename, const NetworkPlayerInfo *info, RString report=RString());

  //! Creates player identity
  /*!
  Called asynchronously when identity is confirmed
  \param ident player identity
  \param squad player's squad
  */
  void CreateIdentity(PlayerIdentity &ident);

  //! Process result of voting when voting is valid
  /*!
  \param id unique identification of voting
  \param value voted value
  */
  void ApplyVoting(const AutoArray<char> &id, const AutoArray<char> *value);

  //! some admin state change - scan admin status of all players
  void UpdateAdminState();

  //! Delete player info (unregister player)
  void OnPlayerDestroy(int dpid);

  //! Set estimated end of mission time
  /*!
  \param time estimated time
  */
  void SetEstimatedEndTime(Time time);

  /// read to time left estimation
  int GetSessionTimeLeft() const;

  //! Return absolute limit of max. players allowed
  /** does not include virtual slot on ded. server */
  int GetMaxPlayersLimit() const {return _maxPlayersLimit;}

  /// Return maximal number of players according to bandwidth estimation
  /** does not include virtual slot on ded. server */
  int GetMaxPlayersSafe(int bandwidthPerClient, int round) const;

  //! Return maximal allowed number of players on server based on mission and user settings
  /** does include virtual slot on ded. server */
  int GetMaxPlayers() const;
  
  //! Return name of mission pbo file on server to optimize transfer to bot client
  RString GetMissionFilename() const {return _missionBank;}

  //! When player joined to game
  Time GetPlayerJoined(int dpid) const;

  //! Find info about given player
  NetworkPlayerInfo *GetPlayerInfo(int dpid);

//((EXE_CRC_CHECK
#if !_SUPER_RELEASE
  /// Find the exe difference for given clients
  void CompareExe(int dpid1, int dpid2) {_exeInvestigation.Init(dpid1, dpid2);}
#endif
//))EXE_CRC_CHECK

  /// Find owner of given network object (used in scripted function owner)
  int GetOwner(const NetworkId &id) const;
  /// Change owner of given network object (used in scripted function setOwner)
  bool SetOwner(const NetworkId &id, int clientId);

  bool StatusChat(int clientId, RString text, RString colorClass);

  void OnRejectChanged(const NetworkId &id, NetworkMessageClass cls, bool reject);

  void OnHierParentChange( Entity* veh );

protected:
  //! Create player info (register player)
  NetworkPlayerInfo *OnPlayerCreate(int dpid, bool privateSlot, const char *name, unsigned long inaddr);
  //! Force state change for given client
  void SetClientState(int dpnid, NetworkClientState state);

#if _ENABLE_DEDICATED_SERVER
  // select game master on dedicated server
  void SetGameMaster(int player, bool admin);

  /// set event handler
  void SetEventHandler(RString name, RString value);
  /// process event handler (return if the event was handled)
  bool OnEvent(ServerEvent event, float par);
  /// process event handler (return if the event was handled)
  bool OnEvent(ServerEvent event, float par1, float par2);
  /// process event handler (return if the event was handled)
  bool OnEvent(ServerEvent event, float par1, RString par2);
#endif

  /// Find owner of given network object
  int FindOwner(const NetworkId &id);

  //! Find info about given network object
  NetworkObjectInfo *GetObjectInfo(const NetworkId &id) const;
  //! Create object info (register object)
  NetworkObjectInfo *OnObjectCreate(const NetworkId &id, int owner, NetworkMessage *msg, NetworkMessageType type);
  //! Delete object info (unregister object), return owner id
  int PerformObjectDestroy(const NetworkId &id);
  //! Delete object info (unregister object) and send message about destruction
  void OnObjectDestroy(const NetworkId &id, int from);

  Vector3 GetObjectPosition( NetworkId obj );


  //! Called when update message received
  /*!
  \param id network object ID
  \param from sender DirectPlay ID
  \param msg update message
  \param type update message type
  \param cls update message class
  */
  NetworkObjectInfo *OnObjectUpdate(NetworkId &id, int from, NetworkMessage *msg, NetworkMessageType type, NetworkMessageClass cls);

  void RescanBestPosition( NetworkObjectInfo * oInfo );

  void UpdateChildrenPos(const Entity * veh);

  bool CheckBestPositionIntegrity() const;
  //! Send update message
  /*!
  \param pInfo info about receiving player
  \param oInfo info about object to update
  \param cls update message class
  \param dwFlags DirectPlay flags
  */
  int UpdateObject(NetworkPlayerInfo *pInfo, NetworkObjectInfo *oInfo, NetworkMessageClass cls, NetMsgFlags dwFlags);

  //! Calculate time error (message fading) between two stored states (messages)
  /*!
  \param type update message type
  \param msg1 first message
  \param msg2 second message
  */
  float CalculateErrorTime
  (
    NetworkMessageType type, NetworkMessage *msg1, NetworkMessage *msg2
  );

  //! Owner of some object changes
  /*!
  \param id ID of network object
  \param from DirectPlay ID of old owner
  \param to DirectPlay ID of new owner
  */
  void ChangeOwner(const NetworkId &id, int from, int to);

  //! Send mission info (mission heade, side slots and role slots) to client
  /*!
  \param DirectPlay ID of receiving client
  \param onlyPlayers do not send mission header
  */
  void SendMissionInfo(int to, bool onlyPlayers = false);

  //! Send all agregated and update messages
  void SendMessages();
  //! Check if fade out players are to kicked off
  void CheckFadeOut();
  //! Calculate limits for message sending
  void EstimateBandwidth(NetworkPlayerInfo &pInfo, int nPlayers, int &nMsgMax, int &nBytesMax);
  //! Calculate errors and place object in list sorted by error
  /*!
  \param objects output sorted list of objects
  \param pInfo info about receiving player
  */
  void CreateObjectsList(AutoArray<UpdateObjectInfo, MemAllocSA> &objects, NetworkPlayerInfo &pInfo);

  void OnStopUpdating( NetworkObjectInfo &oInfo, NetworkPlayerInfo &pInfo, NetworkPlayerObjectInfo &poInfo);

  //! Prepare next object update message to message queue
  bool PrepareNextUpdate(NetworkPlayerInfo &pInfo, AutoArray<UpdateObjectInfo, MemAllocSA> &objects, int &next);

  //! Dedicated server simulation
  void SimulateDS();

  void SimulateMonitor( DWORD tickCount );

  /// broadcast current mission voting status to all clients
  void MissionVotingEcho();
  /// check time out for current server state
  bool CheckTimeOut(int readyCount, int totalCount, DWORD ready, DWORD notReady);

  /// check how much players is need to consider the server to be full
  int PlayerServerFull() const;

  //! Show message in both console and chat
  void ServerMessage(const char *text);

  //! Show client-side localized message in both console and chat
  void ServerMessage(const LocalizedFormatedString &text);
  
  //! Find players on chat channel
  /*!
  \param players output array of players
  \param units chat target units 
  \param from DirectPlay ID of sender
  \param voice true for Voice Over Net transfer
  */
  void GetPlayersOnChannel(AutoArray<int, MemAllocSA> &players, AutoArray<NetworkId> units, DWORD from, bool voice);
  //! Find players on chat channel
  /*!
  \param players output array of players
  \param channel chat channel
  \param from DirectPlay ID of sender
  \param voice true for Voice Over Net transfer
  */
  void GetPlayersOnChannel(AutoArray<int, MemAllocSA> &players, int channel, DWORD from, bool voice);

  /// check if given player in out of game (dead, or in the lobby)
  bool CheckPlayerInLobby(const NetworkPlayerInfo &info) const;
  /*!
  \name GameSpy Query & Reporting SDK support
  */
  //@{
//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY
  //! Close Query & Reporting SDK support
  void DoneQR();
  //! Simulation step of Query & Reporting SDK support
  void SimulateQR();
  //! Information about change of inner game state
  void ChangeStateQR();
public:
  //! Initialize Query & Reporting SDK support
  void InitQR(bool isPrivate);

  void ProcessGeoJob() const;
  void CloseGeoJob() const;

  /// Called when a server key needs to be reported
  void OnQueryServer(int keyid, qr2_buffer_t outbuf);
  /// Called when a player key needs to be reported
  void OnQueryPlayer(int keyid, int index, qr2_buffer_t outbuf);
  /// Called when a team key needs to be reported
  void OnQueryTeam(int keyid, int index, qr2_buffer_t outbuf);
  /// Called when we need to report the list of keys we report values for
  void OnQueryKeys(int keytype, qr2_keybuffer_t keybuffer);
  /// Called when we need to report the number of players and teams
  int OnQueryCount(int keytype);

  /// validate CD Key using GameSpy SDK
  bool ValidateCDKey() const { return _validateCDKey; }

#if _VERIFY_CLIENT_KEY
   /// called when autorization of CD Key finished
  void OnCDKeyAuthorize(int player, int authenticated, const char *errmsg);
#endif

#endif
//}
  //@}

  //! Update session description server sending via LAN broadcast
  void UpdateSessionDescription();
  
  //! Update session description server sending via both mathmaking and LAN broadcast
  void UpdateMatchmakingDescription(bool changedParams);

  void SendMuteList(const PlayerIdentity &id);
  
  void EnqueueMsg(int to, NetworkMessage *msg, NetworkMessageType type, NetworkPlayerObjectInfo *poInfo);
  void EnqueueMsgNonGuaranteed(int to, NetworkMessage *msg, NetworkMessageType type, const RefR<NetMessage, RefCountSafe> &dependOn);

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  //! Set interval in which dedicated server sends info about transfer rates to gamemaster
  void Monitor(float interval);
  void OnMonitorIn(int size);
  
public:
  void DebugAsk(RString str);
protected:
  void DebugAnswer(RString str);

  int ConsoleF(const char *format, ...);
#else
  int ConsoleF ( const char *format, ... ){return 0;}
#endif
//}

  void OnMessageClientInput(int from, NetworkMessageType type, NetworkMessageContext &ctx);

  //! Client state changed
  void OnClientStateChanged(int dpnid, NetworkClientState state);

  void ObjectUpdateAll(NetworkObjectInfo * info, int dpnid, NetMsgFlags dwFlags);

  //! Check if dpnid and sender are on the same channel
  bool IsPlayerOnChannel(int dpnid, int channel, int roleIndex2);

  //! Send init messages to JIP player, handle special messages
  void SendInitMessages(int dpnid);

  void OnPlayerConnecting(int dpnid);
  void OnPlayerConnecting(int dpnid, GameValuePar codeValue);

  /// update server database when player leave mission
  void DisconnectPlayer(int dpnid, NetworkId playerPerson);

  /// calculate how important is given object to given player
  float CalculateErrorCoef(const NetworkPlayerInfo &pInfo, const NetworkObjectInfo &oInfo, bool updating);

  void SendCreateMessage( int dpnid, NetworkObjectInfo &info, NetworkPlayerObjectInfo *poInfo );

  /// event handler - wrongly signed data or signature check question timed out
  void OnHackedData(HackType type, int dpid, RString filename);

#if _VERIFY_KEY
  /// event handler - two players with the same CD-KEY
  void OnDoubleIdDetected(const PlayerIdentity &newIdentity, const PlayerIdentity &oldIdentity);
#endif

  /// execute corresponding event handler for all players
  void CallOnPlayerDisconnected();

  /// execute corresponding event handler for a signle players
  void OnPlayerDisconnected(NetworkPlayerInfo *info);
public:
  /// report hacked data of given player
  bool ProcessHackedData(NetworkPlayerInfo &pi);

private:
  // recursive traversal of messages to set correct owner of owned items
  void CheckItemsOwner(const NetworkMessageUpdateVehicleAI *msg, int newOwner);
  void CheckItemOwner(const NetworkId &id, int newOwner);



};

class ReportHackedData : public CheckSignatureTimeoutFunc
{
  NetworkPlayerInfo &_pi;
  NetworkServer     &_server;
public:
  ReportHackedData(NetworkPlayerInfo &pi, NetworkServer &server) : _pi(pi) , _server(server) {}
  bool operator ()();
};

///////////////////////////////////////////////////////////////////////////////
// Network Manager

TypeIsSimple(INetworkUpdateCallback *)

//! Implementation of basic network functions
class NetworkManager : public INetworkManager
{
friend class NetworkComponent;

protected:
  SRef<NetTranspSessionEnum> _sessionEnum;
  //! time of last call of EnumHosts
  UITime _lastEnumHosts;
  //! IP address of computer (empty for local network) where search for sessions
  RString _ip;
  //! port where search for sessions
  int _port;
  //! additional remote hosts
  AutoArray<RemoteHostAddress> _hosts;

	//! enable creation of physical world
	bool _physEnabled;

  //! network server component
  SRef<NetworkServer> _server;
  //! network client component
  SRef<NetworkClient> _client;
  //! voice over net disabled
  bool _disableVoN;
  //! voice over net codec quality for encoding
  int _vonCodecQuality;
  //! log file for dedicated server can be specified in server.cfg by entry logFile
  RString _dedServerLogFileName;
  FILE *_dedServerLogFile;


#if _ENABLE_MP && (!_GAMES_FOR_WINDOWS)
  // TODO: Unify X360 / PC mute list handling
  MuteListMessage _muteList;
#endif

  float _pendingInvitationX;
  float _pendingInvitationY;
  float _pendingInvitationW;
  float _pendingInvitationH;

  Ref<Texture> _cqPoorTexture;
  Ref<Texture> _cqBadTexture;
  Ref<Texture> _cqDesyncLow;
  Ref<Texture> _cqDesyncHigh;

  //! voice communicator connected
  bool _isVoice;

  //! whether Done was already called in Binarize at the end before destructor
  bool _disableDone;

  FindArray<INetworkUpdateCallback *> _updateCallbacks;

#if _ENABLE_GAMESPY
  bool _trackUsageDone;
#endif

  /// request to disable voice
  EDisableVoiceRequest _disableVoiceRequest;
  /// time when the request was set
  UITime _disableVoiceRequestTime;

  /// helper to enable asynchronous save / load in singleplayer
  AsyncDBStorage _spStorage;

public:
  //! Constructor
  NetworkManager();
  //! Destructor
  ~NetworkManager();

  bool IsServer() const override {return _server != nullptr;}
  //! Return if client is created
  bool IsClient() const override {return _client != nullptr;}

	void DisablePhysics() override {_physEnabled = false;}
	bool IsPhysicsMaster() const override {return IsServerOwner() && _physEnabled;}
	virtual bool IsPhysicsEnabled() const override {return _physEnabled;}

  bool IsServerOwner() const override {return _server != nullptr || _client==nullptr;}
  bool IsMultiplayer() const override {DoAssert(_server == nullptr || _client!=nullptr); return _client!=nullptr;}

  NetworkServer *GetServer() const {return _server;}

  int RequiredBuildNo() { return (_server!=NULL) ? _server->BuildNoRequired() : 0; }

  //! Access to server component
  NetworkServer *GetServer() {return _server;}
  //! Access to client component
  NetworkClient *GetClient() {return _client;}

  virtual void TrackUsage();

  bool VoiceReady() const;

  //! return if voice communicator is connected
  bool IsVoice() const {return _isVoice;}
  //! Return if player is voice banned
  bool IsVoiceBanned() const;
  //! return if voice over net is disabled
  bool IsVoNDisabled() const {return _disableVoN;}
  //! set whether voice over net is disabled
  void SetVoNDisabled(bool val) {_disableVoN=val;}
  //! return the log File for Dedicated server (entry logFile in server.cfg)
  RString GetDedicatedServerLogFileName() { return _dedServerLogFileName; }
  FILE *GetDedicatedServerLogFile();
  void CloseDedicatedServerLogFile() { if (_dedServerLogFile) { fclose(_dedServerLogFile); _dedServerLogFile=NULL; } }
  //! VoN codec quality for encoding
  int VoNCodecQuality() const { return _vonCodecQuality; }
  //! VoN codec quality for encoding (should propagate into VoNRecorder too)
  void SetVoNCodecQuality(int quality);

  bool Init(RString ip, int port, bool startEnum = true);
  void Init();
  void DisableDone() { _disableDone=true; }

  void Done();
  void RemoveEnumeration();

  bool IsVoicePlaying(int dpnid) const;
  bool IsVoiceRecording() const;
  int CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const;
  void IsVoice2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D) const;

  void SetVoiceON();
  void SetVoiceOFF();
  void SetVoiceToggleOn(bool value);
  /// sets threshold for silence analyzer (0.0f - 1.0f)
  void SetVoNRecThreshold(float val);
  void SetVoNTransmitterFreq(float f) override;
  float GetVoNTransmitterFreq() const override;
  void SetVoNTransmitterRange(float range) override;
  float GetVoNTransmitterRange2() const override;
  void WhatUnitsOnTransmitterChannel(RefArray<NetworkObject> &units) override;
  void SetVoiceCapture(bool value);
  //! request disabling of voice
  virtual void SetDisableVoiceRequest(EDisableVoiceRequest request);


#if _ENABLE_CHEATS
  void VoNSay(int dpnid, int channel, int frequency, int seconds);
#endif

  void CustomFilesProgressClear();
  void CustomFilesProgressGet(int &max, int &pos);
  DWORD GetServerTime();
  DWORD GetEstimatedEndServerTime();


#if _GAMES_FOR_WINDOWS

  virtual SessionType GetSessionType() const {return _sessionType;}

  /// returns if any session exists
  virtual bool SessionExists() const {return _sessionHandle != NULL;}

  virtual bool IsSignedIn() const {return true;}

  // Appear offline implemented by Xbox Guide on Xbox 360
  void Cloak(bool set = true) {}
  bool IsCloaked() const {return false;}

  void DisableVoice(bool disable);

#else
  bool IsSignedIn() const {return false;}
  void Cloak(bool set = true) {}
  bool IsCloaked() const {return false;}
  
  void DisableVoice(bool disable) {}
#endif

  bool IsPlayerInMuteList(const XUID &xuid) const;
  void UpdateMuteList();
  void TogglePlayerMute(const XUID &xuid);
  
  void SetPendingInvitationPos(RString resource);

public:
  bool IsSystemLink() const {return _sessionEnum != NULL;}

  void GetSessions(AutoArray<SessionInfo> &sessions);
  RString IPToGUID(RString ip, int port);
  bool CreateSession(SessionType sessionType, RString name, RString password, int maxPlayers, bool isPrivate, int port, bool dedicated, RString config, int playerRating);

  void UpdateMaxPlayers(int maxPlayers, int privateSlots) override;
  int GetMaxPlayersLimit() const override;
  int GetMaxPlayersSafe(int requiredPerClient, int round) const;

  ConnectResult JoinSession(SessionType sessionType, RString guid, RString password) override;

  const SessionInfo WaitForSession() override;
  void GetPlayers(AutoArray<NetPlayerInfo> &players) override;
  void CreateMission(RString mission, RString world);
  void SetOriginalName(RString name);
  void InitMission(int difficulty, RString displayName, bool noCopy, RString owner, bool isCampaignMission);
  void KickAllPlayers(int minBuild=0);

  void Close();
  unsigned CleanUpMemory();

  int GetMaxMissionPlayers() const;

  void CreateServerDebugWindow();
  void CreateClientDebugWindow();

  void KickOff(int dpnid, KickOffReason reason);
  void Ban(int dpnid);
  void LockSession(bool lock = true);

  void OnSimulate();
  void OnSimulateDone();

  void FastUpdate();
  void RegisterUpdateCallback(INetworkUpdateCallback *callback);
  void UnregisterUpdateCallback(INetworkUpdateCallback *callback);
  void OnDraw();

  NetworkServerState GetServerState() const;
  int GetServerTimeout() const;
  void SetServerState(NetworkServerState state);

  NetworkClientState GetClientState() const;
  void SetClientState(NetworkClientState state);

  bool GetTransferProgress(float &progress) const;

  void OnJoiningInProgress();

  void OnChangedOnPlayerConnecting(GameValuePar codeValue) override;

  int TimeElapsed() const;

  NetworkClientState GetClientState(int dpnid) const;

  bool IsGameMaster() const;
  bool IsAdmin() const;
  ConnectionQuality GetConnectionQuality() const;
  ConnectionDesync GetConnectionDesync() const;
  void GetParams(float &param1, float &param2) const;
  float GetParams(int index) const;
  AutoArray<float> GetParamsArray() const;
  void SetParams(float param1, float param2, bool updateOnly);
  void SetParams(float param1, float param2, AutoArray<float> paramsArray, bool updateOnly);
  void SetParam(float param, int index, bool updateOnly);
  const MissionHeader *GetMissionHeader() const;
  int GetPlayer() const;
  CreatorId GetCreatorId() const;

  const PlayerIdentity *FindIdentity(int dpnid) const;
  const PlayerIdentity *FindIdentityByPlayerId(int playerId) const;
  const AutoArray<PlayerIdentity> *GetIdentities() const;

  virtual bool DBSave(RString baseURL, int dpnid, RString request) override;

  virtual bool DBSave(RString baseURL, RString uid, RString request) override;
  virtual bool DBQueue(RString baseURL, RString uid, RString request) override;
  virtual bool DBKill(RString baseURL, RString uid) override;

  virtual bool DBItemDrop(RString baseURL, RString request) override;

  virtual void DBLoad(RString baseURL, int playerId, GameDataCode *code) override;

  virtual InputInterface *GetInputInterface(int player) override;
  virtual RString GetDiagText(const NetworkObject *obj) const override;

  void SelectPlayer(int player, Person *person, bool respawn = false);
  void SendSelectPlayer(int dpid, Person *person);
  void PlaySound
  (
    RString name, Vector3Par position, Vector3Par speed, 
    float volume, float freq, AbstractWave *wave
  );
  void SoundState(AbstractWave *wave, SoundStateType state);
  RString GetPlayerName(int dpid) override;
  Vector3 GetCameraPosition(int dpid) override;
  NetworkId GetPlayerPerson(int dpid) override;
  virtual Object *GetPlayerTarget(int dpid) override;
  void ServerSelectPlayer(int dpid, Person *person) override;
  NetworkObject *GetObject(const NetworkId &id) override;
  int GetOwner(const NetworkId &id) const override;
  bool SetOwner(const NetworkId &id, int clientId) override;
  bool StatusChat(int clientId, RString text, RString colorClass=RString());
  bool CreateVehicle(Vehicle *veh, VehicleListType type, RString name, int idVeh);
  bool CreateSubobject(Vehicle *veh, TurretPath &path, NetworkObject *object);
  bool CreateObject(NetworkObject *object, bool setNetworkId=true);
  void CreateAllObjects();
  void DeleteObject(const NetworkId &id);
  void DestroyAllObjects();
  void AskForPilotLight(EntityAI *vehicle, bool enable);
  void AskForVehicleDoorState(Transport *vehicle, int doorIndex, bool opened);
  void AskForSetMaxHitZoneDamage
    (
    Object *who, float dammage
    );
  void CreateObjectStatic(Object *who) override;
  void ExplosionDamage(EntityAI *owner, Vector3Par originDir, Object *directHit, Vector3Par directHitModelPos, HitInfoPar hitInfo,
    Object *componentOwner, int componentIndex, float energyFactor, float explosionFactor, float hitDepth) override;


  void AskForAmmo(
    EntityAIFull *vehicle, Turret *turret, const WeaponModePtr &ptr, int burst
  );
  void AskForMove(
    Object *vehicle, Vector3Par pos
  );

	void AskForMove(
    Object *vehicle, Matrix4Par trans
  );
  void AskForSetMagazineAmmo
  (
    Object *magazine, int ammo
  );

  void ExplosionDamageEffects(
    EntityAI *owner, Object *directHit, HitInfoPar hitInfo, int componentIndex,
    bool enemyDamage, float energyFactor, float explosionFactor
  );
  void ApplyProjectileDecal
  (
    Object *directHit, int componentIndex, HitInfoPar hitInfo, float hitDepth, Vector3Par hitPosLcl, Vector3Par hitDirLcl, Vector3Par hitDirUpLcl
  );
  void RemoveProjectileDecals(Object *person);
  void FireWeapon
  (
    EntityAIFull *vehicle, Person *gunner, const WeaponModePtr &ptr, Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo &remoteInfo
  );
  void UpdateBowState(EntityAIFull *entity, BowState state);
  void SetObjectTexture(EntityAI *veh, int index, RString name, RString eval = "true");
  void PublicExec(RString condition, RString command, Object* obj, OLinkArray(Object) &objs);
  void OnVehicleDestroyed(EntityAI *killed, EntityAI *killer);
  void OnVehicleDamaged(EntityAI *damaged, EntityAI *killer, float damage, RString ammo);
  void MarkerCreate(int channel, AIBrain *sender, RefArray<NetworkObject> &units, ArcadeMarkerInfo &info);
  void MarkerDelete(RString name);
  void SendMsg(NetworkSimpleObject *msg);

  void PublicVariable(RString name, int dpnid);
  void SpawnForPlayer(int dpnid, GameDataCode *code, const GameValue &args) override;

  void SendAUMessage(GameValuePar oper1);

  void ClearDelayedPublicVariables();
  void ProcessDelayedPublicVariables();

  void Chat(int channel, RString text);
  void Chat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text);
  void Chat(int channel, RString sender, RefArray<NetworkObject> &units, RString text);

  void LocalizedChat(int channel, const LocalizedFormatedString &text);
  void LocalizedChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, const LocalizedFormatedString & text);
  void LocalizedChat(int channel, RString sender, RefArray<NetworkObject> &units, const LocalizedFormatedString & text);

  void RadioChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text, RadioSentence &sentence);
  void RadioChatWave(int channel, RefArray<NetworkObject> &units, RString wave, AIBrain *sender, RString senderName);
  int GetVoiceChannel() const;
  void SetVoiceChannel(int channel);
  void SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed);
  void SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed);
  void SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT);
  void UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation);
  virtual void AdvanceAllVoNSounds( float deltaT, bool paused, bool quiet );

  void UpdateCustomChannel(int channel, int ucr_type);

  void TransferFile(RString dest, RString source);
  void SendMissionFile();
  void GetTransferStats(int &curBytes, int &totBytes);

  void ApplyWeather();
#if _VBS3
  void AARDoUpdate(int type ,int intType,RString stringOne = RString(), RString stringTwo = RString(), float floatType = 0.0);
  void AARAskUpdate(int type ,int intType,RString stringOne = RString(), RString stringTwo = RString(), float floatType = 0.0);
  void AddMPReport(int type, RString myString);
  void LoadIsland(RString islandName);
  void AskCommanderOverride(Turret *turret, Turret *overriddenByTurret);
#endif

  void AskForDeleteVehicle(Entity *veh);
#if _VBS3 // respawn command
  void PauseSimulation(bool pause);
#endif

  virtual void AskForSetItemShortcut(Person *person, int index, const InventoryItemType *type, InventoryItem *item) override;
  virtual void AskForThrowItem(Man *person, InventoryItem *item, Vector3 pos, Vector3 dir) override;
	virtual void AskForDropItem(Person *person, InventoryItem *item) override;
  virtual void AskForMoveItem(EntityAI *target, int index, InventoryItem *item, bool rotated) override;
  virtual void AskForRotateInventoryItem(Person *person, InventoryItem *item) override;
  virtual void AskForTakeItemToHands(Person *person, InventoryItem *item) override;
  virtual void AskForSwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner) override;

  virtual void AskForPlayerAction(Person *person, RString action) override;

  virtual void AskInventoryMenu(AIBrain *unit, InventoryItem *item1, InventoryItem *item2) override;

#if _ENABLE_ATTACHED_OBJECTS
  void AttachObject(Object *obj, Object *attachTo, int memIndex, Vector3 pos, int flags);
  void DetachObject(Object *obj);
#endif

  void AskForAnimationPhase(Entity *vehicle, RString animation, float phase);

  bool ProcessCommand(RString command);
  bool IsCommandAvailable(RString command);

  void SendKick(int player);
  void SendLockSession(bool lock = true);

  bool CanSelectMission() const;
  bool CanVoteMission() const;
  const VotingMissionProgressMessage *GetMissionVotingState() const;
  const AutoArray<MPMissionInfo> &GetServerMissions() const;
  void SelectMission(RString mission, int difficulty);
  void VoteMission(RString mission, int difficulty);

  RString GetHostName() const override;
  RString GetServerGUID() const override;

  void UpdateObject(NetworkObject *object);
  virtual void UpdateObject(NetworkObject *object, NetworkMessageClass cls) override;

  //! Forget all enumerated hosts
  void ClearEnumHosts();
  //! Cancel _dp->EnumHost operation
  void StopEnumHosts();
  //! Start _dp->EnumHost operation
  bool StartEnumHosts();

  //! Return original name of local player (without decoration used when several players vith the same name are connected)
  RString GetLocalPlayerName() const;

  Time GetEstimatedEndTime() const;
  void SetEstimatedEndTime(Time time);

  void DisposeBody(Person *body);
  bool IsControlsPaused();
  float GetLastMsgAgeReliable();

  /// react to communicator insertion/removal
  void UpdateVoiceStatus();

  void ChangeVoiceMask(RString mask);

  void DebugAsk(RString str);
  void DebugServerExec(RString str);

  Time GetPlayerJoined(int dpid) const;

  void HackedDataDetected(RString filename);

  void AddPublicVariableEventHandler(RString name, GameValuePar code);
  bool BattlEyeOnScriptExec(RString script);

  virtual void ProcessAction(Action *action, AIBrain *unit) override;
  virtual void ProcessRecipe(RString recipe, Person *person, InventoryItem *item1, InventoryItem *item2) override;
  virtual void UseItem(Person *person, InventoryItem *item) override;

  virtual void SynchronizeVariable( EntityAI * veh, RString name, float coef, const GameDataCode &code ) override;
  virtual void SendCreateMessage( EntityAI *veh ) override;
  
  virtual void OnHierParentChange( Entity* veh ) override;
  virtual void RptFObjectIdJournal( const NetworkId &id ) override;

#if _ENABLE_DEDICATED_SERVER
  virtual void ProfileAnswer(const RString &output) override;
#endif

#if _ENABLE_GAMESPY
  /// Set on which IP Address the client is visible from the matchmaking server
  virtual void SetPublicAddress(int addr);

#endif

#if _GAMES_FOR_WINDOWS
  /// Access to the session info
  const XSESSION_INFO &GetSessionInfo() const {return _sessionInfo;}
  /// Access to the session nonce - the unique key used for arbitration
  ULONGLONG GetSessionNonce() const {return _sessionNonce;}

  /// Update the player slots of the session
  bool UpdateSession(int maxPlayers, int privateSlots);

  /// Register arbitration for clients (final registration on server is processed in StartSession)
  bool ArbitrationRegister(ULONGLONG sessionNonce);

  /// Register player to the session
  bool RegisterPlayer(const PlayerIdentity &identity);
  /// Unregister player from the session
  bool UnregisterPlayer(const PlayerIdentity &identity);

  /// start the match itself, statistics collection begins
  bool StartSession();
  /// finish the match itself, statistics will be reported
  bool EndSession();

protected:
  /// Register session on Xbox Live
  bool RegisterSession(bool host, int maxPlayers, int maxPrivateSlots);
  /// Unregister session from Xbox Live
  void UnregisterSession();

  /// Based on game type, select the leaderboard(s) and algorithm and update the statistics
  bool UpdateStatistics(RString gameType);

#endif
};

extern NetworkManager GNetworkManager;
extern NetworkMessageFormat *GMsgFormats[NMTN];

extern RString ServerTmpDir;
RString GetServerTmpDir();
RString GetClientTmpDir();
RString GetClientCustomFilesDir();

//! total number of diagnostic types
/*!
\patch_internal 1.08 Date 07/26/2001 by Jirka
- Improved: network diagnostics
*/
enum OutputDiagsMode
{
  ODMsgQueue,
  ODMsgSent,
  ODMsgReceived,
  ODMRawStats,
  NOutputDiags
};

extern int outputDiags;
extern bool outputLogs;

// transfer parameters
//! When update error (considering distance) is bellow this threshold, update message is not sent
extern float MinErrorToSend;
//! When update error (not considering distance) is bellow this threshold, update message is not sent
extern float MinErrorToSendNear;
//! Maximal number of messages sent in single simulation step
extern int MaxMsgSend;
//! Maximal number of messages sent in single simulation step on dedicated server
extern int DSMaxMsgSend;
//! Maximal size of agregated guaranteed message
extern int MaxSizeGuaranteed;
//! Maximal size of agregated guaranteed message
extern int MaxSizeNonguaranteed;
//! Limit (minimum) for bandwidth estimation
extern int MinBandwidth;
//! Limit (minimum) for bandwidth estimation on dedicated server
extern int DSMinBandwidth;
//! Limit (maximum) for bandwidth estimation
extern int MaxBandwidth;

//! == default MinErrorToSend / 2, after 2 s update is forced
#define ERR_COEF_TIME_POSITION        0.005
//! == default MinErrorToSend / 5, after 5 s update is forced
#define ERR_COEF_TIME_GENERIC         0.002

#define MSGID_REPLACE 0

void WriteDiagOutput(bool server);
int GetDiagLevel(NetworkMessageType type, bool remote);

//((EXE_CRC_CHECK
/// information for client code verification
struct ExeCRCBlock
{
  int offset, size, crc;
};
TypeIsSimple(ExeCRCBlock);
//))EXE_CRC_CHECK

#ifdef _WIN32
HRESULT WINAPI ReceiveMessage(void *context, DWORD type, void *message);
#endif
void InitMsgFormats();
const char *FormatVal(float val, char *buffer);

#if _ENABLE_UNSIGNED_MISSIONS
RString CreateMPMissionBank(RString filename, RString island);
#endif

int GetNetworkPort();
int GetServerPort();

extern WINDOW_HANDLE hwndApp;

bool DeleteDirectoryStructure(const char *name, bool deleteDir);
void CreatePath(RString path);
bool ParseMission(bool multiplayer, bool avoidCheckIds = false, bool allowEmpty = false);

//! Release COM object
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)             { if(p) {(p)->Release(); (p) = NULL;} }
#endif

// Multiplayer version info

#include "../versionNo.h"

//! Actual multiplayer version
#define MP_VERSION_ACTUAL           APP_VERSION_NUM
//! The earliest supported multiplayer version
#define MP_VERSION_REQUIRED         APP_VERSION_NUM

/*** following code shows how to implement a code specific to certain product version
  if (MP_VERSION_REQUIRED>=160 && !strcmp(APP_NAME_SHORT,"ArmA2OA")) // TODO: can be removed in 160
  {
    // implementation for the new version
  }
  else
  {
    #if MP_VERSION_REQUIRED>=160
      #pragma message WARN_MESSAGE("warning: Remove obsolete code")
    #endif
    // implementation for the old version
  }
*/

//! Types of Network command messages
enum NetworkCommandMessageType
{
  NCMTLogin,
  NCMTLogged,
  NCMTLogout,
  NCMTKick,
  /// used as voting id - restart current mission
  NCMTRestart,
  /// used as voting id for mission voting
  NCMTMission,
  /// used as voting id - return to mission selection
  NCMTMissions,
  NCMTShutdown,
  /// used as voting id - return to role selection
  NCMTReassign,
  NCMTMonitorAsk,
  NCMTMonitorAnswer,
  NCMTVote,
  NCMTVoteMission,
  NCMTMissionTimeElapsed,
  /// used as voting id for admin voting
  NCMTAdmin,
  NCMTInit,
  NCMTLockSession,
  NCMTLoggedOut,
  NCMTDebugAsk,
  NCMTDebugAnswer,
  /// used only as voting id for mission difficulty voting
  NCMTDifficulty,
  /// execute the server scripting command
  NCMTExec,
  NCMTExecResult,
  /// BattlEye server command
  NCMTBattlEye,
  /// Progress of custom files uploading from server to clients
  NCMTCustomFilesProgress,
#if _ENABLE_CHEATS
  /// execute the scripting sommand
  NCMTGameExec,
#endif
#if _VBS3
  NCMTExitAll,
#endif
  // server asks clients for their VoNFeatures diagnostics, clients responds
  NCMTAskVoNDiagnostics,
  NCMTMissionDate,
  NCMTVoteMissionExt,
  NCMTProfileAsk,
  NCMTProfileAnswer
};

#define NETWORK_COMMAND_MSG(MessageName, XX) \
  XX(MessageName, int, type, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of command"), TRANSF) \
  XX(MessageName, SimpleStream, content, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Parameters of command"), TRANSF)

DECLARE_NET_MESSAGE(NetworkCommand, NETWORK_COMMAND_MSG)

#define PLAYER_UPDATE_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"), TRANSF, ET_NONE, 0) \
  XX(MessageName, int, minPing,NDTInteger,int, NCTSmallUnsigned, DEFVALUE(int, 10), DOC_MSG("Ping range estimation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, int, avgPing,NDTInteger,int, NCTSmallUnsigned, DEFVALUE(int, 100), DOC_MSG("Ping range estimation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, int, maxPing,NDTInteger,int, NCTSmallUnsigned, DEFVALUE(int, 1000), DOC_MSG("Ping range estimation"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, int, minBandwidth,NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int,  2), DOC_MSG("Bandwidth estimation (in kbps)"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, int, avgBandwidth,NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 14), DOC_MSG("Bandwidth estimation (in kbps)"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, int, maxBandwidth,NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 28), DOC_MSG("Bandwidth estimation (in kbps)"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
  XX(MessageName, int, desync,NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Current desync level (max. error of unsent messages)"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR) \
  XX(MessageName, int, rights,NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Special rights of given player"), TRANSF, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_ERR(PlayerUpdate, PLAYER_UPDATE_MSG)

inline bool NetworkClient::IsBotClient() const
{
  return _parent->IsServer();
}

inline NetworkServer *NetworkClient::GetServer() const
{
  return _parent->GetServer();
}


#define ACCEPTED_KEY_MSG(MessageName, XX) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Key name"), TRANSF) \
  XX(MessageName, Temp<char>, content, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Key content"), TRANSF) \

DECLARE_NET_INDICES(AcceptedKey, ACCEPTED_KEY_MSG)

//! Accepted public key for data signatures
struct AcceptedKeyMessage : public NetworkSimpleObject
{
  DSKey &_key;

  AcceptedKeyMessage(DSKey &key) : _key(key) {}

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(AcceptedKey)
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);
};

#define ADDITIONAL_SIGNED_FILES_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<RString>, list, NDTStringArray, AutoArray<RString>, NCTNone, DEFVALUESTRINGARRAY, DOC_MSG("List of additional files to check."), TRANSF) \

DECLARE_NET_INDICES(AdditionalSignedFiles, ADDITIONAL_SIGNED_FILES_MSG)

//! Accepted public key for data signatures
struct AdditionalSignedFilesMessage : public NetworkSimpleObject
{
  AutoArray<RString> &_list;

  AdditionalSignedFilesMessage(AutoArray<RString> &list) : _list(list) {}

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(AdditionalSignedFiles)
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);
};

#define DATA_SIGNATURE_ASK_MSG(MessageName, XX) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Index of file to check."), TRANSF) \
  XX(MessageName, int, level, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("The level of the check (higher level means deeper check)."), TRANSF) \

DECLARE_NET_MESSAGE(DataSignatureAsk, DATA_SIGNATURE_ASK_MSG)

#define DATA_SIGNATURE_ANSWER_MSG(MessageName, XX) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Index of file to check."), TRANSF) \
  XX(MessageName, RString, filename, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of checked file."), TRANSF) \
  XX(MessageName, RString, keyName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of public key"), TRANSF) \
  XX(MessageName, Temp<char>, keyContent, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of public key"), TRANSF) \
  XX(MessageName, Temp<char>, signature, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of signature"), TRANSF) \
  XX(MessageName, Temp<char>, hash, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of hash"), TRANSF) \
  XX(MessageName, int, version, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Signature version."), TRANSF) \
  XX(MessageName, int, level, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Ask level."), TRANSF) \

DECLARE_NET_INDICES(DataSignatureAnswer, DATA_SIGNATURE_ANSWER_MSG)

//! Answer to data signature check
struct DataSignatureAnswerMessage : public NetworkSimpleObject
{
  int _index;
  RString _filename;
  DSSignature _signature;
  DSHash _hash;
  int _level;

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(DataSignatureAnswer)
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);
};

#define FILE_SIGNATURE_ASK_MSG(MessageName, XX) \
  XX(MessageName, RString, file, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("File to check the signature."), TRANSF) \
  XX(MessageName, int, level, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("The level of the check (higher level means deeper check)."), TRANSF) \

DECLARE_NET_MESSAGE(FileSignatureAsk, FILE_SIGNATURE_ASK_MSG)

#define FILE_SIGNATURE_ANSWER_MSG(MessageName, XX) \
  XX(MessageName, RString, fileName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Checked filename."), TRANSF) \
  XX(MessageName, RString, keyName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of public key"), TRANSF) \
  XX(MessageName, Temp<char>, keyContent, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of public key"), TRANSF) \
  XX(MessageName, Temp<char>, signature, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of signature"), TRANSF) \
  XX(MessageName, Temp<char>, dataToHash, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content which hash was signed"), TRANSF) \
  XX(MessageName, int, version, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Signature version."), TRANSF) \
  XX(MessageName, int, level, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Ask level."), TRANSF) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Index of pbo file matching the DataSignatureAsk"), TRANSF) \


DECLARE_NET_INDICES(FileSignatureAnswer, FILE_SIGNATURE_ANSWER_MSG)

//! Answer to file signature check
struct FileSignatureAnswerMessage : public NetworkSimpleObject
{
  RString _fileName;
  DSSignature _signature;
  Temp<char> _dataToHash;    // for sig.ver.2: (hash of pbo content) + (hash of pbo file list) + (pbo prefix)
  int _level;
  int _index;

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(FileSignatureAnswer)
  static NetworkMessageFormat &CreateFormat
    (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
    );
  TMError TransferMsg(NetworkMessageContext &ctx);
  RString GetPrefix() const; // parse the dataToHash to find the stored pbo prefix
  void GetDataHash(Temp<char> &hash) const;
};

#define CREATE_RADIO_CHANNEL_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Custom channel in CCCustom1 to CCCustomLast range."), TRANSF) \
  XX(MessageName, int, color, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Custom channel color"), TRANSF) \
  XX(MessageName, RString, label, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Channel label."), TRANSF) \
  XX(MessageName, RString, callSign, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Channel Call Sign."), TRANSF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of participating units"), TRANSF_REFS)

DECLARE_NET_MESSAGE_INIT_MSG(CreateRadioChannel, CREATE_RADIO_CHANNEL_MSG)

#define RADIO_CHANNEL_LABEL_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Custom channel in CCCustom1 to CCCustomLast range."), TRANSF) \
  XX(MessageName, RString, label, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Channel label."), TRANSF)

DECLARE_NET_MESSAGE_INIT_MSG(RadioChannelLabel, RADIO_CHANNEL_LABEL_MSG)

#define RADIO_CHANNEL_CALL_SIGN_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Custom channel in CCCustom1 to CCCustomLast range."), TRANSF) \
  XX(MessageName, RString, callSign, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Channel Call Sign."), TRANSF)

DECLARE_NET_MESSAGE_INIT_MSG(RadioChannelCallSign, RADIO_CHANNEL_CALL_SIGN_MSG)

#define RADIO_CHANNEL_UNITS_MSG(MessageName, XX) \
  XX(MessageName, int, channel, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Custom channel in CCCustom1 to CCCustomLast range."), TRANSF) \
  XX(MessageName, RefArray<NetworkObject>, units, NDTRefArray, AutoArray<NetworkId>, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of participating units"), TRANSF_REFS)

DECLARE_NET_MESSAGE_INIT_MSG(RadioChannelUnits, RADIO_CHANNEL_UNITS_MSG)

#define HACKED_DATA_MSG(MessageName, XX) \
  XX(MessageName, int, type, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, HTUnsignedData), DOC_MSG("Type of violation."), TRANSF) \
  XX(MessageName, RString, filename, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of hacked file."), TRANSF) \

DECLARE_NET_MESSAGE(HackedData, HACKED_DATA_MSG)

#define CDKEY_CHECK_ASK_MSG(MessageName, XX) \
  XX(MessageName, RString, challenge, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Chalenge string (see GameSpy SDK Help)."), TRANSF) \

DECLARE_NET_MESSAGE(CDKeyCheckAsk, CDKEY_CHECK_ASK_MSG)

#define CDKEY_CHECK_ANSWER_MSG(MessageName, XX) \
  XX(MessageName, RString, response, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Response string (see GameSpy SDK Help)."), TRANSF) \

DECLARE_NET_MESSAGE(CDKeyCheckAnswer, CDKEY_CHECK_ANSWER_MSG)

#define CDKEY_RECHECK_ASK_MSG(MessageName, XX) \
  XX(MessageName, int, hint, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Session key (see GameSpy SDK Help)."), TRANSF) \
  XX(MessageName, RString, challenge, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Challenge string (see GameSpy SDK Help)."), TRANSF) \

DECLARE_NET_MESSAGE(CDKeyRecheckAsk, CDKEY_RECHECK_ASK_MSG)

#define CDKEY_RECHECK_ANSWER_MSG(MessageName, XX) \
  XX(MessageName, int, hint, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Session key (see GameSpy SDK Help)."), TRANSF) \
  XX(MessageName, RString, response, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Response string (see GameSpy SDK Help)."), TRANSF) \

DECLARE_NET_MESSAGE(CDKeyRecheckAnswer, CDKEY_RECHECK_ANSWER_MSG)

#define ASK_CONNECT_VOICE_MSG(MessageName, XX) \
  XX(MessageName, int, publicAddr, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Public IP address of the client."), TRANSF) \
  XX(MessageName, int, privateAddr, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Private IP address of the client."), TRANSF) \
  XX(MessageName, int, voicePort, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Port voice channel is bind to."), TRANSF) \
  XX(MessageName, int, verNo, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("The build number of client exe file."), TRANSF) \

DECLARE_NET_MESSAGE(AskConnectVoice, ASK_CONNECT_VOICE_MSG)

#define DIAG_OBJECT_INFO_ASK_MSG(MessageName, XX) \
  XX(MessageName, CreatorId, creator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("creator id of object"), TRANSF) \
  XX(MessageName, OIDInt, id, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("id object"), TRANSF)

DECLARE_NET_MESSAGE(DiagObjectInfoAsk, DIAG_OBJECT_INFO_ASK_MSG)

#define CLIENT_DIAG_TO_SERVER_MSG(MessageName, XX) \
  XX(MessageName, RString, response, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Any info client want server to log in tis report file."), TRANSF)

DECLARE_NET_MESSAGE(ClientDiagToServer, CLIENT_DIAG_TO_SERVER_MSG)

#define CONNECT_VOICE_DIRECT_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of player to connect with"), TRANSF) \
  XX(MessageName, int, addr, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("IP address where connect to."), TRANSF) \
  XX(MessageName, int, port, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Port where connect to."), TRANSF) \
  XX(MessageName, bool, direct, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Is the connection direct or punched?"), TRANSF) \

DECLARE_NET_MESSAGE(ConnectVoiceDirect, CONNECT_VOICE_DIRECT_MSG)

#define CONNECT_VOICE_NAT_NEG_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of player to connect with"), TRANSF) \
  XX(MessageName, int, cookie, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Cookie for Nat negotiation."), TRANSF) \
  XX(MessageName, int, clientIndex, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Client index for Nat negotiation."), TRANSF) \

DECLARE_NET_MESSAGE(ConnectVoiceNatNeg, CONNECT_VOICE_NAT_NEG_MSG) // TODO: remove (make dummy)

#define NAT_NEG_RESULT_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of NAT negotiating player"), TRANSF) \
  XX(MessageName, bool, result, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Result of NAT negotiation (true for success)."), TRANSF) \

DECLARE_NET_MESSAGE(NatNegResult, NAT_NEG_RESULT_MSG)

#define DISCONNECT_MSG(MessageName, XX) \
  XX(MessageName, RString, message, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Cause why the player was disconnected."), TRANSF) \

DECLARE_NET_MESSAGE(Disconnect, DISCONNECT_MSG)

#define MISSION_STATS_MSG(MessageName, XX) \
  XX(MessageName, int, toCreate, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of objects we will create on the client."), TRANSF) \
  XX(MessageName, int, toUpdate, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of objects we will update on the client."), TRANSF) \

DECLARE_NET_MESSAGE(MissionStats, MISSION_STATS_MSG)

#define ASK_FOR_ARBITRATION_MSG(MessageName, XX) \
  XX(MessageName, NetInt64, sessionNonce, NDTInt64, NetInt64, NCTNone, DEFVALUE(NetInt64, 0), DOC_MSG("Key used for arbitration."), TRANSF) \

DECLARE_NET_MESSAGE(AskForArbitration, ASK_FOR_ARBITRATION_MSG)

#define BATTLEYE_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<char>, message, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Message content"), TRANSF) \

DECLARE_NET_MESSAGE(BattlEye_Obsolete, BATTLEYE_MSG)

#define SERVERTIME_MSG(MessageName, XX) \
  XX(MessageName, int, time, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Server time"), TRANSF) \
  XX(MessageName, int, endTime, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Server time of estimated end of MP game."), TRANSF) \

DECLARE_NET_MESSAGE(ServerTime, SERVERTIME_MSG)

#define SERVER_ADMIN_MESSAGE(MessageName, XX) \
  XX(MessageName, bool, admin, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("GameMaster on server is voted admin."), TRANSF) \
  XX(MessageName, int, gameMaster, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Id of gameMaster."), TRANSF) \

DECLARE_NET_MESSAGE(ServerAdmin, SERVER_ADMIN_MESSAGE)

#define SEND_AU_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<char>, value, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Send Auto Update message content"), TRANSF)

DECLARE_NET_MESSAGE(SendAUMsg, SEND_AU_MSG)

/// Custom file message to check whether the transfer to client is neccessary
#define CUSTOM_FILE_PATHS_MSG(MessageName, XX) \
  XX(MessageName, RString, dst, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Custom file path"), TRANSF) \
  XX(MessageName, int, crc, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("CRC of custom file"), TRANSF) \
  XX(MessageName, int, len, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("File length of custom file"), TRANSF)

DECLARE_NET_INDICES(CustomFile, CUSTOM_FILE_PATHS_MSG)

// NetworkMessageCustomFileList to transfer array of CustomFilePaths
#define CUSTOM_FILE_LIST_MSG(MessageName, XX) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Direct play id"), TRANSF) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Player name"), TRANSF) \
  XX(MessageName, AutoArray<CustomFilePaths>, fileList, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageCustomFile), NCTNone, DEFVALUE_MSG(NMTCustomFile), DOC_MSG("List of custom files"), TRANSF_ARRAY)

DECLARE_NET_MESSAGE(CustomFileList, CUSTOM_FILE_LIST_MSG)

#define CUSTOM_FILES_WANTED_MSG(MessageName, XX) \
  XX(MessageName, int, player, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Direct play id"), TRANSF) \
  XX(MessageName, AutoArray<int>, filesWanted, NDTIntArray, AutoArray<int>, NCTSmallUnsigned, DEFVALUEINTARRAY, DOC_MSG("Array of indexes to custom file list which client wants to get"), TRANSF) \

DECLARE_NET_MESSAGE(CustomFilesWanted, CUSTOM_FILES_WANTED_MSG)

#define DELETE_CUSTOM_FILES_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<RString>, list, NDTStringArray, AutoArray<RString>, NCTNone, DEFVALUESTRINGARRAY, DOC_MSG("Array of indexes to custom file list which client wants to get"), TRANSF) \

DECLARE_NET_MESSAGE(DeleteCustomFiles, DELETE_CUSTOM_FILES_MSG)

#if _PROFILE
#define DEBUG_GIVEN_NETWORK_ID 1
#endif

#if DEBUG_GIVEN_NETWORK_ID
bool DebugMeBoy(NetworkId id);
bool DebugMeBoy(NetworkMessage *msg, NetworkMessageType type);
#else
inline bool DebugMeBoy(NetworkId id) {return false;}
inline bool DebugMeBoy(NetworkMessage *msg, NetworkMessageType type) {return false;}
#endif
bool ReadValue(GameValue &value, const void* buffer, int buffSize, INetworkManager *netManager);

#endif

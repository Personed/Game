#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETWORK_HPP
#define _NETWORK_HPP

#include "../wpch.hpp"
#include "../inputInterface.h"

#if _GAMES_FOR_WINDOWS
# include "winSockImpl.hpp"
#endif

#include "winSockDecl.hpp"

#include "networkObject.hpp"
#include <Es/Strings/rString.hpp>
#include <Es/Strings/bString.hpp>
#include <Es/Types/pointers.hpp>
#include <Es/Containers/staticArray.hpp>
#include <Es/Containers/rStringArray.hpp>
#include <El/Time/time.hpp>
#include <El/Stringtable/stringtable.hpp>
#include "../vehicleAI.hpp"
#include "../global.hpp"
class CSteamID;
#if _ENABLE_CONVERSATION
class KBMessageInfo;
#endif

class InventoryItemType;

/*!
\file
Basic interface file for multiplayer game
*/

/// Limit for the chat text (avoid too long messages)
static const int MaxChatLength = 150;

#define AI_PLAYER               1

#define STATIC_OBJECT           1


  #if _VBS3 // max players 256 in VBS3
    #if _VBS2_LITE
      #define _VBS2_MAX_PLAYERS   13
      #define MAX_PLAYERS         _VBS2_MAX_PLAYERS
      #define MAX_PLAYERS_DS      _VBS2_MAX_PLAYERS
      #define DEFAULT_PLAYERS_DS  _VBS2_MAX_PLAYERS
    #else
      #define MAX_PLAYERS         256
      #define MAX_PLAYERS_DS      256
      #define DEFAULT_PLAYERS_DS  256
    #endif
  #else //_VBS3
    #define MAX_PLAYERS         16
    #define MAX_PLAYERS_DS      64
    #define DEFAULT_PLAYERS_DS  64
  #endif

#if _ENABLE_CHEATS
  #define DOCUMENT_MSG_FORMATS    0
#else
  #define DOCUMENT_MSG_FORMATS    0
#endif

#if DOCUMENT_MSG_FORMATS
#define DOC_MSG(text) text
#else
#define DOC_MSG(text) ""
#endif

#if _ENABLE_GAMESPY
// Additional GameSpy QR2 question item keys
#define PARAM1_KEY              100
#define PARAM2_KEY              101
#define CURRENT_VERSION_KEY     102
#define REQUIRED_VERSION_KEY    103
#define MOD_KEY                 104
#define EQUAL_MOD_REQUIRED_KEY  105
#define GAME_STATE_KEY          106
#define DEDICATED_KEY           107
#define PLATFORM_KEY            108
#define LANGUAGE_KEY            109
#define DIFFICULTY_KEY          110
#define MISSION_KEY             111
#define BATTLEYE_KEY            112
#define VERIFY_SIGNATURES_KEY   113
#define SIGNATURES_KEY          114
#define MOD_HASH_KEY            115
#define HASH_KEY                116
#define REQ_BUILD_KEY           117
#define REQ_SECURE_ID_KEY       118
#define LONGITUDE_KEY           119
#define LATITUDE_KEY            120
#define VER_KEY                 121 // short key names follow so that we can obey MAX_FIELD_LIST_LEN (256) limit
#define REQ_VER_KEY             122
#define EQ_MOD_KEY              123
#define DS_KEY                  124
#define DIFF_KEY                125
#define BE_KEY                  126
#define VER_SIG_KEY             127
#define SIG_KEY                 128

//! Game name registered at GameSpy
char *GetGameName();

//! Return secret key (password) for GameSpy
char *GetSecretKey();
#endif

#ifndef DECL_ENUM_NETWORK_MESSAGE_ERROR_TYPE
#define DECL_ENUM_NETWORK_MESSAGE_ERROR_TYPE
DECL_ENUM(NetworkMessageErrorType)
#endif

struct MissionHeader;
struct PlayerIdentity;
struct LocalizedString;
struct LocalizedFormatedString;
DECL_ENUM(VehicleListType)
class Transport;
class EntityAI;
typedef EntityAI EntityAI;
typedef EntityAI TargetType;
class Mine;
class Fireplace;
class RadioSentence;
class Command;
struct WaypointInfo;
class Building;
typedef Building Building;

//struct Object::DoDamageResult;

typedef AutoArray<int, MemAllocLocal<int, 16> > TurretPath;

#define CHECK_CAST(dst, src, dsttype) \
  dsttype *dst = static_cast<dsttype *>(src); \
  Assert(dynamic_cast<dsttype *>(src) != NULL);

#define CHECK_ASSIGN(dst, src, dsttype) \
  dsttype &dst = static_cast<dsttype &>(src);

///////////////////////////////////////////////////////////////////////////////
// Interface for network manager object

//! Basic info about multiplayer player
struct NetPlayerInfo
{
  //! Unique DirectPlay ID of player
  int dpid;
  //! name of player
  RString name;
  /// character controlled by the player
  OLinkNO(NetworkObject) person;
};
TypeContainsOLink(NetPlayerInfo)

//! States of network server
enum NetworkServerState
{
  //! no server
  NSSNone,
  //! server created, no mission selected
  NSSSelectingMission,
  //! mission is editing
  NSSEditingMission,
  //! mission is selected, assigning roles
  NSSAssigningPlayers,
  //! mision is sending
  NSSSendingMission,
  //! game (island, vehicles etc.) is loading
  NSSLoadingGame,
  //! game is launched
  NSSPlaying,
  //! game is aborted
  NSSMissionAborted
};

//! States of network client
enum NetworkClientState
{
  //! no client
  NCSNone,
  //! client is created
  NCSCreated,
  //! client is connected to server, message formats are registered
  NCSConnected,
  //! identity is created
  NCSLoggedIn,
  //! mission is selected
  NCSMissionSelected,
  //! server was asked to send / not send mission
  NCSMissionAsked,
  //! role was assigned (and confirmed)
  NCSPlayerAssigned,
  //! mission received
  NCSMissionReceived,
  //! island loaded, vehicles received
  NCSGameLoaded,
  //! client in game
  NCSPlaying,
  //! game was finished
  NCSGameFinished,
};

//! Positions in vehicle to get in
enum GetInPosition
{
  //! anywhere
  GIPAny,
  //! to commander position
  GIPCommander,
  //! to driver / pilot position
  GIPDriver,
  //! to gunner position
  GIPGunner,
  //! back
  GIPCargo,
  //! to the turret
  GIPTurret
};

//! State of sound
enum SoundStateType
{
  SSRestart,
  SSStop,
  SSRepeat,
  SSLastLoop,
};

//! Quality of connection
enum ConnectionQuality
{
  CQGood,
  CQPoor,
  CQBad
};

//! Desync indication
enum ConnectionDesync
{
  CDGood,
  CDLow,
  CDHigh
};

class Magazine;

//enum UIActionType;

enum KickOffReason
{
  KORKick,
  KORBan,
  KORFade,
  KORAddon,
  KORBadCDKey,
  KORCDKeyInUse,
  KORBattlEye,
  KORAuthTimeOut,
  KOROther
};

struct SessionInfo;
#ifndef DECL_ENUM_CONNECT_RESULT
#define DECL_ENUM_CONNECT_RESULT
DECL_ENUM(ConnectResult)
#endif

/// Type of session
enum SessionType
{
  STStandard,
  STRanked,
  STSystemLink
};

enum MissionPlacement
{
  MPEditingNew=-3,
  MPEditingOld,
  MPPbo, 
  MPDirectory, 
  MPOldEditor,
  MPInAddon,
  MPNewEditor,
  MPDownloaded
};

#if _VBS3
enum AddMPReportEnum
{
  AddHeader,
  AddEvent,
  AddFooter 
};
#endif

/// multiplayer mission info needed during mission selection
struct MPMissionInfo
{
  MissionPlacement placement;
  RString gameType;
  RString mission;
  RString world;
  RString overview;
  LocalizedString description;
  LocalizedString displayName;
  int minPlayers;
  int maxPlayers;
  /// estimated per-client bandwidth requirements
  int perClient;
  /// rounding requirements
  int round;
};
TypeIsMovableZeroed(MPMissionInfo)

struct VotingMissionProgressMessage;

typedef __int64 NetInt64;

#if !_GAMES_FOR_WINDOWS

struct XUID
{
  RString id;

  operator RString() const {return id;}
};
TypeIsMovableZeroed(XUID)

#define XUID_ARRAY_TYPE AutoArray<RString>
#define XUID_ARRAY_NET_TYPE NDTStringArray
#define XUID_ARRAY_DEF_VALUE DEFVALUESTRINGARRAY

bool IsUserIdMatch(const XUID &u1, const XUID &u2);
bool IsUserIdMatch(const RString &u1, const XUID &u2);

#define XOnlineAreUsersIdentical IsUserIdMatch

inline RString XUIDDebugName(const XUID &xuid) {return xuid.id;}
inline RString &XUIDToTransfer(XUID &xuid) {return xuid.id;}
inline const RString &XUIDToTransfer(const XUID &xuid) {return xuid.id;}

typedef struct {
  BYTE content[36];
} XNADDR;

typedef struct {
  BYTE content[16];
} XONLINE_STAT;

#else

inline RString XUIDDebugName(const XUID &xuid) {return Format("%lld",xuid);}
inline __int64 &XUIDToTransfer(XUID &xuid) {return *(__int64 *)&xuid;}
inline const __int64 &XUIDToTransfer(XUID &xuid) const {return *(const __int64 *)&xuid;}

#define XUID_ARRAY_TYPE AutoArray<XUID>
#define XUID_ARRAY_NET_TYPE NDTXUIDArray
#define XUID_ARRAY_DEF_VALUE DEFVALUEXUIDARRAY

#endif

#ifndef XONLINE_STAT_IS_SIMPLE
#define XONLINE_STAT_IS_SIMPLE
  TypeIsSimple(XONLINE_STAT)
#endif


struct INetworkUpdateCallback
{
  virtual ~INetworkUpdateCallback() {}
  virtual void operator ()() = 0;
};

//! Interface to basic network functions
class INetworkManager
{
public:
  /// request for voice
  enum EDisableVoiceRequest
  {
    DVRNone,
    DVRDisableVoice, //< disable voice recording and transmission
    DVRDisableVoiceToggle, //< disable voice recording and transmission and disabble voice toggle on
  };

#if _VBS3
  // Added for AAR, to start and stop enumerating hosts
  virtual bool StartEnumHosts(){ return false; };
  virtual void StopEnumHosts(){};
#endif

  //! Initialize multiplayer, listen on given ip address and port
  virtual bool Init(RString ip, int port, bool startEnum = true) = 0;
  //! Initialize multiplayer without creating of enumerator object
  virtual void Init() = 0;
  //! Finish multiplayer
  virtual void Done() = 0;
  virtual void DisableDone() {};
  //! Finish multiplayer without SignOff from Xbox Live
  virtual void RemoveEnumeration() = 0;

  //! GameSpy usage statistics
  virtual void TrackUsage() = 0;

  //! To test whether there is some session
  virtual bool IsClient() const = 0;

  virtual bool VoiceReady() const = 0;

  //! Return if voice communicator is connected
  virtual bool IsVoice() const = 0;
  //! Return if player is voice banned
  virtual bool IsVoiceBanned() const = 0;
  //! Given player is currently speaking
  virtual bool IsVoicePlaying(int dpnid) const = 0;
  /// check if we can currently hear given player, and on which channel
  virtual int CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const = 0;
  /// check whether specific sound buffers are playing
  virtual void IsVoice2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D) const = 0;
  //! Local player is currently speaking
  virtual bool IsVoiceRecording() const = 0;
  //! VoN switching ON and OFF
  virtual void SetVoiceON() = 0;
  virtual void SetVoiceOFF() = 0;
  //! if voice is toggled on (user is not using push-to-talk)
  virtual void SetVoiceToggleOn(bool value) = 0;
  /// sets threshold for silence analyzer (0.0f - 1.0f)
  virtual void SetVoNRecThreshold(float val) = 0;
  /// sets freqv the CCItemTransmitter operates on
  virtual void SetVoNTransmitterFreq(float f) = 0;
  /// sets maximal range of the transmitteritem in meters
  virtual void SetVoNTransmitterRange(float range) = 0;
  virtual float GetVoNTransmitterRange2() const = 0;
  /// freqv the CCItemTransmitter operates on
  virtual float GetVoNTransmitterFreq() const = 0;
  /// get list of units subscribing actual CCItemTransmitter frequency (for chat purposes)
  virtual void WhatUnitsOnTransmitterChannel(RefArray<NetworkObject> &units) = 0;
  //! VoN recording on/off
  virtual void SetVoiceCapture(bool value) = 0;
  //! request disabling of voice
  virtual void SetDisableVoiceRequest(EDisableVoiceRequest request) = 0;
  //! Custom files upload progress sent from server
  virtual void CustomFilesProgressClear() = 0;
  virtual void CustomFilesProgressGet(int &max, int &pos) = 0;
  //! Server time synchronized to clients
  virtual DWORD GetServerTime() = 0;
  //! Server time of estimated End of MP game
  virtual DWORD GetEstimatedEndServerTime() = 0;
  //! get battleye to know about Steam GUID validation

#if _ENABLE_CHEATS
  virtual void VoNSay(int dpnid, int channel, int frequency, int seconds) = 0;
#endif

  //! Check if player is signed in Xbox Live
  virtual bool IsSignedIn() const = 0;
  //! Check if player play System Link game
  virtual bool IsSystemLink() const = 0;


#if _GAMES_FOR_WINDOWS
  /// Return the type of the session (System Link, Standard, Ranked)
  virtual SessionType GetSessionType() const = 0;
  /// returns if any session exists
  virtual bool SessionExists() const = 0;
#endif

  //! Set if player is appeared offline
  virtual void Cloak(bool set = true) = 0;

  //! Check if player is appeared offline
  virtual bool IsCloaked() const = 0;

  //! Allocate voice resources to XHV
  virtual void DisableVoice(bool disable) = 0;

  virtual bool IsPlayerInMuteList(const XUID &xuid) const = 0;
  virtual void UpdateMuteList() = 0;
  virtual void TogglePlayerMute(const XUID &xuid) = 0;
  
  //! Set position of Pending Invitation icon
  virtual void SetPendingInvitationPos(RString resource) = 0;

  //! Retrieve list of sessions
  virtual void GetSessions(AutoArray<SessionInfo> &sessions) = 0;
  //! Transfer IP address and port into DirectPlay URL address
  virtual RString IPToGUID(RString ip, int port) = 0;
  //! Create multiplayer game (server + client)
  /*!
  \param sessionType session type (Standard Match, Ranked Match, System Link)
  \param name session name
  \param password password of session
  \param maxPlayers max. number of players
  \param isPrivate disable reporting server to GameSpy
  \param port UDP port for server
  \return true if game created
  */
  virtual bool CreateSession(SessionType sessionType, RString name, RString password, int maxPlayers, bool isPrivate, int port, bool dedicated, RString config, int playerRating) = 0;

  //! Join to multiplayer game (client only)
  /*!
  \param sessionType session type (Standard Match, Ranked Match, System Link)
  \param guid unique identification of session
  \param password password of session
  \return result of connecting client to server
  */
  virtual ConnectResult JoinSession(SessionType sessionType, RString guid, RString password) = 0;
  //! Wait until session is created on given ip and port
  virtual const SessionInfo WaitForSession() = 0;
  //! Retrieve list of players
  virtual void GetPlayers(AutoArray<NetPlayerInfo> &players) = 0;
  //! update maximal number of players
  virtual void UpdateMaxPlayers(int maxPlayers, int privateSlots) = 0;

  /// check number players allowed
  virtual int GetMaxPlayersLimit() const = 0;
  
  /// check number players supported by the bandwidth
  /** @param requiredPerClient per-client bandwidth requirements */
  virtual int GetMaxPlayersSafe(int requiredPerClient, int round) const = 0;
  
  //! Creates pbo file for selected mission (on server)
  virtual void CreateMission(RString mission, RString world) = 0;
  //! Store name of pbo file for selected mission (on server)
  virtual void SetOriginalName(RString name) = 0;

  //! Initialize mission
  /*!
  - create and send mission header structure (including difficulty settings)
  - create and send slots for sides and roles
  */
  virtual void InitMission(int difficulty, RString displayName, bool noCopy, RString owner, bool isCampaignMission) = 0;
  virtual void KickAllPlayers(int minBuild=0) = 0;

  //! max player count from mission description.ext
  virtual int GetMaxMissionPlayers() const = 0;

  /// Central database handling
  virtual bool DBSave(RString baseURL, int dpnid, RString request) = 0;
  virtual bool DBSave(RString baseURL, RString uid, RString request) = 0;
  virtual bool DBQueue(RString baseURL, RString uid, RString request) = 0;

  virtual bool DBItemDrop(RString baseURL, RString request) = 0;

  virtual void DBLoad(RString baseURL, int playerId, GameDataCode *code) = 0;
  virtual bool DBKill(RString baseURL, RString uid) = 0;

  //! Close multiplayer session, listen to sessions again
  virtual void Close() = 0;

  //! Create server debugging window
  virtual void CreateServerDebugWindow() = 0;
  //! Create client debugging window
  virtual void CreateClientDebugWindow() = 0;

  //! Network simulation - called once during every frame
  virtual void OnSimulate() = 0;
  virtual void OnSimulateDone() = 0;

  /// time critical network update - need to be called several times in frame
  virtual void FastUpdate() = 0;
  /// register callback to be called during FastUpdate (allocation is handled on caller side)
  virtual void RegisterUpdateCallback(INetworkUpdateCallback *callback) = 0;
  /// unregister callback to be called during FastUpdate (deallocation is handled on caller side)
  virtual void UnregisterUpdateCallback(INetworkUpdateCallback *callback) = 0;

  //! Network specific drawing
  virtual void OnDraw() = 0;

	virtual void DisablePhysics() = 0;
	virtual bool IsPhysicsMaster() const = 0;
	virtual bool IsPhysicsEnabled() const = 0;
  
	/// return if this is the computer owning world state (server, or single player)
  virtual bool IsServerOwner() const = 0;
  
  virtual bool IsMultiplayer() const = 0;
  //! Return if server is created
  virtual bool IsServer() const = 0;
#if _VBS3
  //! Return if client is created
  virtual bool IsClient() const = 0;
#endif
  //! Kick off given player from game
  virtual void KickOff(int dpnid, KickOffReason reason) = 0;
  //! Ban given player (kick off + add to dynamic ban list)
  virtual void Ban(int dpnid) = 0;
  //! disable/enable connecting of further clients 
  virtual void LockSession(bool lock = true) = 0;

  //! perform regular memory clean-up
  virtual unsigned CleanUpMemory() {return 0;}

  //! Return current state of server
  virtual NetworkServerState GetServerState() const = 0;
  
  //! Return timeout for current step
  virtual int GetServerTimeout() const = 0;
  
  //! Set current state of server (on server only)
  virtual void SetServerState(NetworkServerState state) = 0;

  //! Return current state of client
  virtual NetworkClientState GetClientState() const = 0;
  //! Set current state of client
  virtual void SetClientState(NetworkClientState state) = 0;


  //! Return how much part of world state was already transferred to client
  virtual bool GetTransferProgress(float &progress) const = 0;

  //! Handler called when client is joining game in progress
  virtual void OnJoiningInProgress() = 0;

  virtual void OnChangedOnPlayerConnecting(GameValuePar codeValue) = 0;

  //! Time elapsed from start of mission on server
  virtual int TimeElapsed() const = 0;

  //! Return client state for given client
  virtual NetworkClientState GetClientState(int dpnid) const = 0;

  //! Check if client is logged into dedicated server as game master
  virtual bool IsGameMaster() const = 0;
  //! Return if gamemaster is only voted admin (cannot do shutdown etc.)
  virtual bool IsAdmin() const = 0;
  //! Return connection quality for client
  virtual ConnectionQuality GetConnectionQuality() const = 0;
  //! Retrieves parameters for current mission
  virtual void GetParams(float &param1, float &param2) const = 0;
  //! Retrieves parameters for current mission
  virtual float GetParams(int index) const = 0;
  //! Retrieves parameters for current mission
  virtual AutoArray<float> GetParamsArray() const = 0;
  //! Set parameters for current mission
  virtual void SetParams(float param1, float param2, bool updateOnly) = 0;
  //! Set parameters for current mission
  virtual void SetParams(float param1, float param2, AutoArray<float> paramsArray, bool updateOnly) = 0;
  //! Return mission header
  virtual const MissionHeader *GetMissionHeader() const = 0;
  //! Return id of player on client
  virtual int GetPlayer() const = 0;
  //! Return short id of player, used as creator in NetworkId
  virtual CreatorId GetCreatorId() const = 0;
  //! Search for identity with given dpnid
  virtual const PlayerIdentity *FindIdentity(int dpnid) const = 0;
  //! Search for identity with given player id
  virtual const PlayerIdentity *FindIdentityByPlayerId(int playerId) const = 0;
  //! Return array of identities
  virtual const AutoArray<PlayerIdentity> *GetIdentities() const = 0;

  virtual RString GetDiagText(const NetworkObject *obj) const = 0;

  virtual InputInterface *GetInputInterface(int player) = 0;
  //! Select person as player
  /*!
  \param player DirectX player id
  \param person player's person
  \param respawn play "ressurect" cutscene
  */
  virtual void SelectPlayer(int player, Person *person, bool respawn = false) = 0;
  // Send NMTSelectPlayer message to server
  virtual void SendSelectPlayer(int player, Person *person) = 0;

  //! Play sound on all clients (except itself)
  /*!
  \param name name of sound file
  \param position source position
  \param speed source speed
  \param volume sound volume
  \param freq sound frequency
  \param wave local sound object (for mapping local sounds to sent sounds)
  */
  virtual void PlaySound
  (
    RString name, Vector3Par position, Vector3Par speed, 
    float volume, float freq, AbstractWave *wave
  ) = 0;
  //! Change state of played sound on all clients (except itself)
  /*!
  \param wave local sound object
  \param state state to set
  */
  virtual void SoundState(AbstractWave *wave, SoundStateType state) = 0;

  //! Return name for given player
  virtual RString GetPlayerName(int dpid) = 0;
  //! Return camera position for given player
  virtual Vector3 GetCameraPosition(int dpid) = 0;

  virtual NetworkId GetPlayerPerson(int dpid) = 0;
  virtual Object *GetPlayerTarget(int dpid) = 0;

  virtual void ServerSelectPlayer(int dpid, Person *person) = 0;
  
  /// Find the object (local or remote) by its network id
  virtual NetworkObject *GetObject(const NetworkId &id) = 0;
  /// Find owner of given network object (works on the server only)
  virtual int GetOwner(const NetworkId &id) const = 0;
  /// Change owner of given network object (used in scripted function setOwner)
  virtual bool SetOwner(const NetworkId &id, int clientId) = 0;

  virtual bool StatusChat(int clientId, RString text, RString colorClass=RString()) = 0;
  //! Register vehicle on client and send message to server (and other clients)
  /*!
  \param veh registered vehicle
  \param type id of vehicle list to add into
  \param name name of variable vehicle is stored in
  \param idVeh id of vehicle (assigned in mission editor)
  */
  virtual bool CreateVehicle
  (
    Vehicle *veh, VehicleListType type, RString name, int idVeh
  ) = 0;
  //! Register subobject of vehicle on client and send message to server (and other clients)
  /*!
  \param veh owner vehicle
  \param index unique id of object in vehicle
  \param object created object
  */
  virtual bool CreateSubobject(Vehicle *veh, TurretPath &path, NetworkObject *object) = 0;

  //! Register generic network object and send message to server (and other clients)
  virtual bool CreateObject(NetworkObject *object, bool setNetworkId=true) = 0;
  //! Send first update of all created objects over network (as guaranteed message)
  virtual void CreateAllObjects() = 0;
  //! Unregister given network object, send message to others
  virtual void DeleteObject(const NetworkId &id) = 0;
  //! Unregister all objects, both local and remote
  virtual void DestroyAllObjects() = 0;
  /// Ask for pilot light
  virtual void AskForPilotLight(EntityAI *vehicle, bool enable) = 0;
  /// Ask for vehicle door open/close
  virtual void AskForVehicleDoorState(Transport *vehicle, int doorIndex, bool opened) = 0;

  /// Ask object owner to inflict some damage
  /**
  @param who damaged object
  @param dammage how much damage should be caused
  */
  virtual void CreateObjectStatic(Object *who) = 0;

  virtual void ExplosionDamage( EntityAI * owner, Vector3Par originDir, Object * directHit, Vector3Par directHitModelPos, HitInfoPar hitInfo, Object * componentOwner, int componentIndex, float energyFactor, float explosionFactor, float hitDepth ) = 0;


  //! Ask vehicle owner for change ammo state
  /*!
  \param vehicle vehicle which ammo is changing
  \param turret turret which ammo is changing
  \param weapon weapon index
  \param burst amount of ammo to decrease
  */
  virtual void AskForAmmo
  (
    EntityAIFull *vehicle, Turret *turret, const WeaponModePtr &ptr, int burst
  ) = 0;
  //! Ask object owner for move object
  /*!
  \param vehicle moving object
  \param pos new position
  */
  virtual void AskForMove
  (
    Object *vehicle, Vector3Par pos
  ) = 0;
  //! Ask magazine owner for change its ammo count
  /*!
  \param magazine
  \param ammo
  */
  virtual void AskForSetMagazineAmmo
  (
    Object *magazine, int ammo
  ) = 0;
  //! Ask object owner for move object
  /*!
  \param vehicle moving object
  \param trans new transformation matrix
  */
	virtual void AskForMove
  (
    Object *vehicle, Matrix4Par trans
  ) = 0;
  //! Transfer explosion effects (explosion, smoke, etc.) to other clients
  /*!
  \param owner shot owner (who is responsible for explosion)
  \param directHit hit object
  \param pos explosion position
  \param dir explosion direction
  \param componentIndex index of convex component that was hit
  \param type ammunition
  \param enemyDamage some enemy was damaged
  */
  virtual void ExplosionDamageEffects
  (
    EntityAI *owner, Object *directHit, HitInfoPar hitInfo, int componentIndex,
    bool enemyDamage, float energyFactor, float explosionFactor
  ) = 0;
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
  virtual void ApplyProjectileDecal
  (
    Object *directHit, int componentIndex, HitInfoPar hitInfo, float hitDepth, Vector3Par hitPosLcl, Vector3Par hitDirLcl, Vector3Par hitDirUpLcl
  ) = 0;
  //! Remove on other clients the projectile decals stuck on victim's body
  /*!
  \param person The person from which projectiles are removed
  */
  virtual void RemoveProjectileDecals
  (
    Object *person
  ) = 0;
  //! Transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients
  /*!
  \param vehicle firing vehicle
  \param weapon firing weapon index
  \param magazine fired magazine
  \param target aimed target
  */
  virtual void FireWeapon(
    EntityAIFull *vehicle, Person *gunner, const WeaponModePtr &ptr, Magazine *magazine, EntityAI *target,
    const RemoteFireWeaponInfo &remoteInfo
  ) = 0;
    //! Transfer bow action state to other clients
  /*!
  \param entity Person using the bow
  \param state The state of bow
  */
  virtual void UpdateBowState(EntityAIFull *entity, BowState state ) = 0;
  //! Set a texture for a vehicle on other clients
  /*!
	\param vehicle vehicle that will have a texture changed
	\param index selection index to change
	\param name texture name to use
  */
  virtual void SetObjectTexture(
    EntityAI *veh, int index, RString name, RString eval = "true"
  ) = 0;
  //! Execute game code on other clients
  /*!
	\param condition game code to evaluate to true or false
	\param command game code to execute
  \param obj object to pass as _this
  \param objs array of objects to pass as _this
  */
  virtual void PublicExec(
    RString condition, RString command, Object* obj, OLinkArray(Object) &objs
  ) = 0;
  //! Transfer message who is responsible to destroy vehicle to other clients
  /*!
  \param killed destroyed vehicle
  \param killer who is responsible for destroying
  */
  virtual void OnVehicleDestroyed(EntityAI *killed, EntityAI *killer) = 0;
  //! Transfer message about damage of vehicle to other clients
  /*!
  \param damaged damaged vehicle
  \param killer who is responsible for destroying
  \param damage amount of damage hit
  \param ammo ammunition type
  */
  virtual void OnVehicleDamaged(EntityAI *damaged, EntityAI *killer, float damage, RString ammo) = 0;
  //! Transfer info about (user made) marker creation to other clients
  /*!
  \param channel chat channel (who will see the marker)
  \param sender sender unit
  \param units receiving units
  \param info marker itself
  */
  virtual void MarkerCreate(int channel, AIBrain *sender, RefArray<NetworkObject> &units, ArcadeMarkerInfo &info) = 0;
  //! Transfer info about (user made) marker was deleted to other clients
  /*!
  \param name name of marker
  */
  virtual void MarkerDelete(RString name) = 0;
  //! Send radio message to other clients
  /*!
  Sending whole radio message is now implemented only for vehicle messages.
  \param msg radio message
  */
  virtual void SendMsg(NetworkSimpleObject *msg) = 0;

  //! Public variable to other clients
  /*!
  \param name variable name
  */
  virtual void PublicVariable(RString name, int dpnid) = 0;

  /// spawn a code on a client
  virtual void SpawnForPlayer(int dpnid, GameDataCode *code, const GameValue &args) = 0;

  virtual void SendAUMessage(GameValuePar oper1) = 0;

  //! Public variables containing Objects with NULL networkId are stored to be proccessed again after the CreateAllObjects call
  virtual void ClearDelayedPublicVariables() = 0;
  virtual void ProcessDelayedPublicVariables() = 0;
  /// Send client-side localized chat message
  virtual void LocalizedChat(int channel, const LocalizedFormatedString &text) = 0;
  /// Send client-side localized chat message
  virtual void LocalizedChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text) = 0;
  /// Send client-side localized chat message
  virtual void LocalizedChat(int channel, RString sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text) = 0;

  //! Send chat message
  /*!
  \param channel chat channel
  \param text chat text
  */
  virtual void Chat(int channel, RString text) = 0;
  //! Send chat message
  /*!
  \param channel chat channel
  \param sender sender unit
  \param units receiving units
  \param text chat text
  */
  virtual void Chat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text) = 0;
  //! Send chat message
  /*!
  \param channel chat channel
  \param sender sender name
  \param units receiving units
  \param text chat text
  */
  virtual void Chat(int channel, RString sender, RefArray<NetworkObject> &units, RString text) = 0;
  //! Send radio message as chat (text and sentence)
  /*!
  \param channel chat channel
  \param sender sender name
  \param units receiving units
  \param text chat text
  \param sentence - list of words to be spoken
  */
  virtual void RadioChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text, RadioSentence &sentence) = 0;
  //! Send text radio message as chat (sound and title)
  /*!
  \param channel chat channel
  \param units receiving units
  \param wave name of class from CfgRadio containing sound and title
  \param sender sender unit
  \param senderName sender name
  */
  virtual void RadioChatWave(int channel, RefArray<NetworkObject> &units, RString wave, AIBrain *sender, RString senderName) = 0;
  /// Return actual channel for voice chat
  virtual int GetVoiceChannel() const = 0;
  //! Set channel for Voice Over Net
  /*!
  \param channel chat channel
  */
  virtual void SetVoiceChannel(int channel) = 0;
  //! Set position and speed of some Voice Over Net source (it affects sources)
  /*!
  \param dpnid player id got from GetRemotePlayer()
  \param pos position of player
  \param speed speed of player
  */
  virtual void SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed) =0;
  //! Set position and speed of player in Voice Over Net (it affects listener)
  /*!
  \param pos position of player
  \param speed speed of player
  */
  virtual void SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed) =0;
  //! Set obstruction of player in Voice Over Net (it affects source)
  /*!
  \param dpnid player id got from GetRemotePlayer()
  */
  virtual void SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT) = 0;
  //! Updates volume and ear accomodation of player in Voice Over Net (it affects source)
  /*!
  \param dpnid player id got from GetRemotePlayer()
  */
  virtual void UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation) = 0;
  //! Advance all voices in VoN (even muted ones) 
  /*!
  \param deltaT time to advance
  \param paused whether game is paused
  \param quiet whether voices should be muted
  */
  virtual void AdvanceAllVoNSounds( float deltaT, bool paused, bool quiet ) = 0;
  //! Send update of the content of the Custom Radio Channel
  /*!
  \param channel The channel in range CCCustom1..CCCustomLast
  \param ucr_type What to update (all, label, callSign, units, ...)
  */
  virtual void UpdateCustomChannel(int channel, int ucr_type) = 0;
  //! Transfer file to other clients
  /*!
  \param dest destination filename
  \param source source filename
  */
  virtual void TransferFile(RString dest, RString source) = 0;
  //! Server send mission pbo file to clients
  virtual void SendMissionFile() = 0;
  //! Retrieve state of file transfer
  /*!
  \param curBytes amount of transfered bytes
  \param totBytes total amount bytes to transfer
  */
  virtual void GetTransferStats(int &curBytes, int &totBytes) = 0;
  //! Process chat command for remote control of dedicated server
  /*!
  \param command chat command
  */
  virtual bool ProcessCommand(RString command) = 0;
  //! Determine whether specified network command is available (based on whether current user is admin or not etc.)
  /*!
  \param command type
  */
  virtual bool IsCommandAvailable(RString command) = 0;
  //! Ask server to kick off player
  /*!
  \param player DirectPlay ID of player to kick off
  */
  virtual void SendKick(int player) = 0;
  //! Ask server to disable/enable connection of further clients
  /*!
  \param lock true to disable connection
  */
  virtual void SendLockSession(bool lock = true) = 0;

  //! Checks if gamemaster is to select mission on dedicated server
  virtual bool CanSelectMission() const = 0;
  //! Checks if client is to vote mission on dedicated server
  virtual bool CanVoteMission() const = 0;
  //! Returns array of missions available on dedicated server
  virtual const AutoArray<MPMissionInfo> &GetServerMissions() const = 0;
  virtual const VotingMissionProgressMessage *GetMissionVotingState() const = 0;

  //! Server hostname 
  virtual RString GetHostName() const = 0;
  virtual RString GetServerGUID() const = 0;

  //! Select mission on dedicated server
  /*!
  \param mission name of selected mission
  \param difficulty game difficulty level
  */
  virtual void SelectMission(RString mission, int difficulty) = 0;
  //! Vote mission on dedicated server
  /*!
  \param mission name of selected mission
  \param difficulty game difficulty level
  */
  virtual void VoteMission(RString mission, int difficulty) = 0;
  //! Force update of network object
  /*!
  \param object object to update
  */
  virtual void UpdateObject(NetworkObject *object) = 0;
  virtual void UpdateObject(NetworkObject *object, NetworkMessageClass cls) = 0;

  virtual void ApplyWeather() = 0;
#if _VBS3
  //! Turn on personalised items for person
  /*
  \param object object to have its personalised items enabled
  \param active turn on or off
  */
  virtual void AARDoUpdate(int type ,int intType,RString stringOne = RString(), RString stringTwo = RString(), float floatType = 0.0) = 0;
  virtual void AARAskUpdate(int type ,int intType,RString stringOne = RString(), RString stringTwo = RString(), float floatType = 0.0)= 0;
  virtual void AskCommanderOverride(Turret *turret, Turret *overriddenByTurret) = 0;
  virtual void AddMPReport(int type, RString myString) = 0;
  virtual void LoadIsland(RString islandName) = 0;
#endif

  virtual void AskForSetItemShortcut(Person *person, int index, const InventoryItemType *type, InventoryItem *item) = 0;
  virtual void AskForThrowItem(Man *person, InventoryItem *item, Vector3 pos, Vector3 dir) = 0;
	virtual void AskForDropItem(Person *person, InventoryItem *item) = 0;
  virtual void AskForMoveItem(EntityAI *target, int index, InventoryItem *item, bool rotated) = 0;
  virtual void AskForRotateInventoryItem(Person *person, InventoryItem *item) = 0;
  virtual void AskForTakeItemToHands(Person *person, InventoryItem *item) = 0;
  virtual void AskForSwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner) = 0;

  virtual void AskForPlayerAction(Person *person, RString action) = 0;

  virtual void AskInventoryMenu(AIBrain *unit, InventoryItem *item1, InventoryItem *item2) = 0;

#if _VBS3 // respawn command
  //! Pause the remote simulation
  /*!
  \param pause, either true or false
  */
  virtual void PauseSimulation(bool pause) = NULL;
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
  virtual void AttachObject(Object *obj, Object *attachTo, int memIndex, Vector3 pos, int flags) = 0;
  //! detach object
  /*!
  \param obj, object to detach
  */
  virtual void DetachObject(Object *obj) = 0;
#endif

  //! Ask vehicle for user defined animation
  /*!
  \param vehicle animated vehicle
  \param animation animation name
  \param phase wanted animation phase
  */
  virtual void AskForAnimationPhase(Entity *vehicle, RString animation, float phase) = 0;

  //! Return estimated end of mission time
  virtual Time GetEstimatedEndTime() const = 0;
  //! Set estimated end of mission time
  /*!
  \param time estimated time
  */
  virtual void SetEstimatedEndTime(Time time) = 0;
  
  //! Body can be hidden (for better performance)
  virtual void DisposeBody(Person *body) = 0;

  //! Game is paused due to disconnection state of game
  virtual bool IsControlsPaused() = 0;

  //! Last received message's age in seconds (used to eliminate "disconnect cheat")
  virtual float GetLastMsgAgeReliable() = 0;

  //! Set voice mask to new value (Xbox)
  virtual void ChangeVoiceMask(RString mask) = 0;

  //! Perform server diagnostics command
  virtual void DebugAsk(RString str) = 0;
  virtual void DebugServerExec(RString str) = 0;

  //! When player joined to game
  virtual Time GetPlayerJoined(int dpid) const = 0;

  /// Reaction to detection of hacked data
  virtual void HackedDataDetected(RString filename) = 0;

  /// dedicated server log file can be specified in server.cfg by entry logFile
  virtual RString GetDedicatedServerLogFileName() = 0;
  virtual FILE *GetDedicatedServerLogFile() = 0;
  virtual void CloseDedicatedServerLogFile() = 0;

#if _ENABLE_GAMESPY
  /// Set on which IP Address the client is visible from the matchmaking server
  virtual void SetPublicAddress(int addr) = 0;
#endif

  /// add event handler executed in reaction to PublicVariableMessage
  virtual void AddPublicVariableEventHandler(RString name, GameValuePar code) = 0;

  /// call BattlEye callback for added scripts
  virtual bool BattlEyeOnScriptExec(RString script) = 0;

  /// ask for action processing
  virtual void ProcessAction(Action *action, AIBrain *unit) = 0;

  /// ask for recipe processing
  virtual void ProcessRecipe(RString recipe, Person *person, InventoryItem *item1, InventoryItem *item2) = 0;

  /// ask for item UI invocation
  virtual void UseItem(Person *person, InventoryItem *item) = 0;

  virtual void SendCreateMessage( EntityAI *veh ) = 0;
  virtual void SynchronizeVariable( EntityAI * veh, RString name, float coef, const GameDataCode &code ) = 0;

  virtual void OnHierParentChange( Entity* veh ) = 0;

  /// print diagnostics journal for an item
  virtual void RptFObjectIdJournal( const NetworkId &id ) = 0;

#if _ENABLE_DEDICATED_SERVER
  virtual void ProfileAnswer(const RString &output) = 0;
#endif
};

//! Return networn manager class
INetworkManager &GetNetworkManager();

//! Interface to basic network component (client or server)
class INetworkComponent
{
public:
  //! Virtual destructor
  virtual ~INetworkComponent() {}
  //! Return message format of given message type
  /*!
  \param type message type
  */
  virtual NetworkMessageFormatBase *GetFormat(int type) = 0;
  //! Return message format of given message type
  /*!
  \param type message type
  */
  virtual const NetworkMessageFormatBase *GetFormat(int type) const = 0;
  //! Return network object with given id
  /*!
  \param id id of object to return
  */
  virtual NetworkObject *GetObject(const NetworkId &id) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// Messages

#define TMCHECK(command) \
{TMError err = command; if (err != TMOK && err != TMNotFound) return err;}

#define MSG_RECEIVE   false
#define MSG_SEND      true

//! Transfer message errors
DEFINE_ENUM_BEG(TMError)      // Transfer message errors
  //! transfer OK
  TMOK,         // OK
  //! transferred item not found in message
  TMNotFound,   // OK - different versions
  //! generic error
  TMGeneric,
  TMBadType,
  //! message format not found
  TMBadFormat,
DEFINE_ENUM_END(TMError)

//! Message types
#define NETWORK_MESSAGE_TYPES(XX) \
  XX(FORMAT_CREATE, NetworkMessageFormatItem, MsgFormatItem, "<notused/> <embedded/> Define single item of message format.", Generic) \
  XX(FORMAT_CREATE, NetworkMessageFormatBase, MsgFormat, "<notused/> Used internally by OFP for dynamic definition of message formats.", Generic) \
  XX(FORMAT_CREATE, PlayerMessage, Player, "This message is send by server to confirm that client was connected successfully.", Control) \
  XX(FORMAT_CREATE, NetworkMessageQueue, Messages, "<notused/> Used internally for aggregation of messages, only NMTMessages is needed.", Generic) \
  XX(FORMAT_SIMPLE, Dummy, ServerState, "Message is send by server if its state is changed.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ClientState, "Message is send by client if its state is changed.", Control) \
  XX(FORMAT_SIMPLE, Dummy, PlayerClientState, "Message is send by server if state of some clients changed.", Control) \
  XX(FORMAT_CREATE, PlayerIdentity, Login, "Detail description of player.", Control) \
  XX(FORMAT_CREATE, LogoutMessage, Logout, "Server broadcast this message if some player disconnect.", Control) \
  XX(FORMAT_CREATE, ChatMessage, Chat, "Chat message.", Chat) \
  XX(FORMAT_CREATE, RadioChatMessage, RadioChat, "Radio message (game radio protocol).", Chat) \
  XX(FORMAT_CREATE, RadioChatWaveMessage, RadioChatWave, "Radio message (message defined by mission designer).", Chat) \
  XX(FORMAT_CREATE, SetSpeakerMessage, SetSpeaker, "Set which unit is speaking using Voice Over Net.", Chat) \
  XX(FORMAT_CREATE, MissionHeader, MissionHeader, "Description of selected mission.", Control) \
  XX(FORMAT_CREATE, SelectPlayerMessage, SelectPlayer, "Assign player to unit.", Control) \
  XX(FORMAT_CREATE, TransferFileMessage, TransferFile, "Used to transfer generic file over network.", Generic) \
  XX(FORMAT_CREATE, AskMissionFileMessage, AskMissionFile, "Used by client to tell server if its mission file is actual.", Control) \
  XX(FORMAT_CREATE, TransferMissionFileMessage, TransferMissionFile, "Used to transfer mission file over network.", Generic) \
  XX(FORMAT_CREATE, TransferFileToServerMessage, TransferFileToServer, "Used to transfer generic file over network.", Generic) \
  XX(FORMAT_CREATE, AskForAmmoMessage, AskForAmmo, "Message for ask vehicle owner for change ammo state.", Ask) \
  XX(FORMAT_CREATE, AskForMoveVectorMessage, AskForMoveVector, "Message for ask object owner for move object.", Ask) \
  XX(FORMAT_CREATE, AskForMoveMatrixMessage, AskForMoveMatrix, "Message for ask object owner for move object.", Ask) \
  XX(FORMAT_CREATE, ExplosionDamageEffectsMessage, ExplosionDamageEffects, "Message for transfer explosion effects (explosion, smoke, etc.) to other clients.", Broadcast) \
  XX(FORMAT_CREATE, ProjectileDecalMessage, ProjectileDecal, "Message for transfer projectile decals (arrows stuck on body, etc.) to other clients.", Broadcast) \
  XX(FORMAT_CREATE, RemoveProjectileDecalsMessage, RemoveProjectileDecals, "Message for transferring to other clients the removal of all projectile decals from person.", Broadcast) \
  XX(FORMAT_CREATE, FireWeaponMessage, FireWeapon, "Message for transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients.", Broadcast) \
  XX(FORMAT_CREATE, BowStateMessage, BowState, "Message for notifying key steps of shooting a bow.", Broadcast) \
  XX(FORMAT_CREATE, VehicleDestroyedMessage, VehicleDestroyed, "Message for transfer message who is responsible to destroy vehicle to other clients.", Statistics) \
  XX(FORMAT_CREATE, MarkerCreateMessage, MarkerCreate, "Message for transfer info about (user made) marker creation to other clients.", Chat) \
  XX(FORMAT_CREATE, MarkerDeleteMessage, MarkerDelete, "Message for transfer info about (user made) marker was deleted to other clients", Chat) \
  XX(FORMAT_CREATE, ChangeOwnerMessage, ChangeOwner, "Message is sent when owner of some object changes.", Control) \
  XX(FORMAT_CREATE, PlaySoundMessage, PlaySound, "Message sent to clients to play sound.", Broadcast) \
  XX(FORMAT_CREATE, SoundStateMessage, SoundState, "Message sent to clients to change state of played sound.", Broadcast) \
  XX(FORMAT_CREATE, DeleteObjectMessage, DeleteObject, "Message announcing destroying of network object.", Destroy) \
  XX(FORMAT_CREATE, DeletedObjectMessage, DeletedObject, "Message confirming destroying of network object.", Destroy) \
  XX(FORMAT_CREATE, Object, CreateObject, "Create Object.", Create) \
  XX(FORMAT_UPDATE, Object, UpdateObject, "Generic update of Object.", Update) \
  XX(FORMAT_CREATE, Vehicle, CreateVehicle, "Create Entity (: Object).", Create) \
  XX(FORMAT_UPDATE, Vehicle, UpdateVehicle, "Generic update of Entity (: Object).", Update) \
  XX(FORMAT_UPDATE_POSITION, Vehicle, UpdatePositionVehicle, "Update position of Entity (: Object)", UpdPos) \
  XX(FORMAT_UPDATE, FlagCarrier, UpdateFlag, "Generic update of Flag (: EntityWithSupply)", Update) \
  XX(FORMAT_CREATE, Shot, CreateShot, "Create Shot (: Entity).", Create) \
  XX(FORMAT_UPDATE, Shot, UpdateShot, "Generic update of Shot (: Entity)", Update) \
  XX(FORMAT_CREATE, Crater, CreateCrater, "Create Crater (: Entity).", Create) \
  XX(FORMAT_CREATE, CraterOnVehicle, CreateCraterOnVehicle, "Create CraterOnVehicle (: Crater).", Create) \
  XX(FORMAT_CREATE, SoundOnVehicle, CreateSoundOnVehicle, "Create SoundrOnVehicle", Create) \
  XX(FORMAT_CREATE, ObjectDestructed, CreateObjectDestructed, "Create ObjectDestruction (: Entity).", Create) \
  XX(FORMAT_CREATE, EntityAI, CreateVehicleAI, "Create EntityAI.", Create) \
  XX(FORMAT_UPDATE, EntityAI, UpdateVehicleAI, "Generic update of EntityAI (: Entity).", Update) \
  XX(FORMAT_UPDATE, EntityAIFull, UpdateVehicleAIFull, "Generic update of EntityAIFull (: EntityAI).", Update) \
  XX(FORMAT_CREATE, Person, CreateVehicleBrain, "Create Person.", Create) \
  XX(FORMAT_UPDATE, Person, UpdateVehicleBrain, "Generic update of Person (: EntityAIFull).", Update) \
  XX(FORMAT_UPDATE, Transport, UpdateTransport, "Generic update of Transport (: EntityAIFull).", Update) \
  XX(FORMAT_UPDATE, Man, UpdateMan, "Generic update of Man (: Person).", Update) \
  XX(FORMAT_UPDATE_POSITION, Man, UpdatePositionMan, "Update position of Man (: Person).", UpdPos) \
  XX(FORMAT_UPDATE, TankOrCar, UpdateTankOrCar, "Generic update of TankOrCar (: Transport).", Update) \
  XX(FORMAT_UPDATE, TankWithAI, UpdateTank, "Generic update of Tank (: TankOrCar).", Update) \
  XX(FORMAT_UPDATE_POSITION, TankWithAI, UpdatePositionTank, "Update position of Tank (: TankOrCar).", UpdPos) \
  XX(FORMAT_UPDATE, Turret, UpdateTurret, "Update of Turret.", Update) \
  XX(FORMAT_UPDATE, Car, UpdateCar, "Generic update of Car (: TankOrCar).", Update) \
  XX(FORMAT_UPDATE_POSITION, Car, UpdatePositionCar, "Update position of Car (: TankOrCar).", UpdPos) \
  XX(FORMAT_UPDATE, AirplaneAuto, UpdateAirplane, "Generic update of Airplane (: Vehicle).", Update) \
  XX(FORMAT_UPDATE_POSITION, AirplaneAuto, UpdatePositionAirplane, "Update position of Airplane (: Vehicle).", UpdPos) \
  XX(FORMAT_UPDATE, HelicopterAuto, UpdateHelicopter, "Generic update of Helicopter (: Vehicle).", Update) \
  XX(FORMAT_UPDATE_POSITION, HelicopterAuto, UpdatePositionHelicopter, "Update position of Helicopter (: Vehicle).", UpdPos) \
  XX(FORMAT_UPDATE, ParachuteAuto, UpdateParachute, "Generic update of Parachute (: Vehicle).", Update) \
  XX(FORMAT_UPDATE, ShipWithAI, UpdateShip, "Generic update of Ship (: Vehicle).", Update) \
  XX(FORMAT_UPDATE_POSITION, ShipWithAI, UpdatePositionShip, "Update position of Ship (: Vehicle).", UpdPos) \
  XX(FORMAT_CREATE, OperInfoResult, PathPoint, "<embedded/> PathPoint.", Create) \
  XX(FORMAT_UPDATE, Motorcycle, UpdateMotorcycle, "Generic update of Motorcycle (: TankOrCar).", Update) \
  XX(FORMAT_UPDATE_POSITION, Motorcycle, UpdatePositionMotorcycle, "Update position of Motorcycle (: TankOrCar).", UpdPos) \
  XX(FORMAT_CREATE, NetworkCommandMessage, NetworkCommand, "Message for transfer of Network Command", Control) \
  XX(FORMAT_UPDATE, SeaGullAuto, UpdateSeagull, "Generic update of SeaGull (: Entity).", Update) \
  XX(FORMAT_UPDATE_POSITION, SeaGullAuto, UpdatePositionSeagull, "Update position of SeaGull (: Entity).", UpdPos) \
  XX(FORMAT_UPDATE_POSITION, PlayerIdentity, PlayerUpdate, "Update network statistics of clients.", Statistics) \
  XX(FORMAT_UPDATE_DAMMAGE, EntityAI, UpdateDamageVehicleAI, "Update damage state of EntityWithAI (: Entity).", UpdDmg) \
  XX(FORMAT_UPDATE_DAMMAGE, Object, UpdateDamageObject, "Update damage state of Object.", UpdDmg) \
  XX(FORMAT_CREATE, HelicopterAuto, CreateHelicopter, "Create Helicopter (: Vehicle).", Create) \
  XX(FORMAT_UPDATE, ClientInfoObject, UpdateClientInfo, "Update position of players.", Control) \
  XX(FORMAT_SIMPLE, Dummy, MissionParams, "Broadcast parameters of mission.", Control) \
  XX(FORMAT_UPDATE, Mine, UpdateMine, "Generic update of Mine (: Shot).", Update) \
  XX(FORMAT_SIMPLE, Dummy, VehicleDamaged, "Transfer message about damage of vehicle to other clients.", Statistics) \
  XX(FORMAT_UPDATE, Fireplace, UpdateFireplace, "Generic update of Fireplace (: EntityWithAI).", Update) \
  XX(FORMAT_SIMPLE, Dummy, AskForAnimationPhase, "Ask vehicle for user defined animation.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, ForceDeleteObject, "Ask object owner to destroy object.", Destroy) \
  XX(FORMAT_UPDATE, ResourceSupply, UpdateSupply, "<embedded/> Update of Supply Resources.", Update) \
  XX(FORMAT_SIMPLE, Dummy, MuteList, "Xbox muted list.", Control) \
  XX(FORMAT_SIMPLE, Dummy, VoiceOn, "Voice communicator connected.", Control) \
  XX(FORMAT_SIMPLE, Dummy, CleanupPlayer, "Cleanup unit player was attached to.", Control) \
  XX(FORMAT_CREATE, ArcadeMarkerInfo, Marker, "<embedded/> Marker in map.", Chat) \
  XX(FORMAT_CREATE, ArcadeEffects, Effects, "<embedded/> Camera and title effects.", Create) \
  XX(FORMAT_CREATE, SeaGullAuto, CreateSeagull, "Create Seagull (: Vehicle).", Create) \
  XX(FORMAT_CREATE, DynSoundSource, CreateDynSoundSource, "Create DynSoundSource (: Entity).", Create) \
  XX(FORMAT_CREATE, DestructionEffects, CreateDestructionEffects, "Create DestructionEffects (: Entity).", Create) \
  XX(FORMAT_SIMPLE, Dummy, RemoteMuteList, "Xbox muting list.", Control) \
  XX(FORMAT_SIMPLE, Dummy, LocalizedChat, "Localized chat message.", Generic) \
  XX(FORMAT_CREATE, Turret, CreateTurret, "Create turret as a network object.", Create) \
  XX(FORMAT_SIMPLE, Dummy, VoteMission, "Client votes about a mission", Control) \
  XX(FORMAT_SIMPLE, Dummy, VotingMissionProgress, "Mission voting progress displayed", Control) \
  XX(FORMAT_SIMPLE, Dummy, ServerTimeout, "Tell clients how many time is left before current screen will timeout.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AcceptedKey, "Public key accepted for data signatures.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AdditionalSignedFiles, "List of additional files where to check signature.", Control) \
  XX(FORMAT_SIMPLE, Dummy, DataSignatureAsk, "Data signature check.", Control) \
  XX(FORMAT_SIMPLE, Dummy, DataSignatureAnswer, "Data signature check answer.", Control) \
  XX(FORMAT_SIMPLE, Dummy, HackedData, "Hacked data detected.", Control) \
  XX(FORMAT_UPDATE_POSITION, Turret, UpdatePositionTurret, "Update position of Turret.", UpdPos) \
  XX(FORMAT_SIMPLE, Dummy, CDKeyCheckAsk, "CD Key initial check question.", Control) \
  XX(FORMAT_SIMPLE, Dummy, CDKeyCheckAnswer, "CD Key initial check answer.", Control) \
  XX(FORMAT_SIMPLE, Dummy, CDKeyRecheckAsk, "CD Key additional check question.", Control) \
  XX(FORMAT_SIMPLE, Dummy, CDKeyRecheckAnswer, "CD Key additional check answer.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AskConnectVoice, "Info about client voice connection.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ConnectVoiceDirect, "Direct voice connection is possible.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ConnectVoiceNatNeg, "Try to establish voice connection using NAT Negotiation.", Control) \
  XX(FORMAT_SIMPLE, Dummy, Disconnect, "Disconnect the player from the server.", Control) \
  XX(FORMAT_SIMPLE, Dummy, MissionStats, "Info what will be transferred during mission loading.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AskForArbitration, "Ask client to arbitrate statistics.", Control) \
  XX(FORMAT_UPDATE_DAMMAGE, Entity, UpdateDamageVehicle, "Update damage state of Entity (: Object).", UpdDmg) \
  XX(FORMAT_SIMPLE, Dummy, BattlEye_Obsolete, "BattlEye library message.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AttachObject, "Attach an object to another object", Control) \
  XX(FORMAT_SIMPLE, Dummy, DetachObject, "Detach an object", Control) \
  XX(FORMAT_CREATE, CustomFilePaths, CustomFile, "<embedded/> Custom file to be tranfered in CustomFileList.", Control) \
  XX(FORMAT_SIMPLE, Dummy, CustomFileList, "List of custom files to be checked on client.", Control) \
  XX(FORMAT_SIMPLE, Dummy, CustomFilesWanted, "List of indexis to custom files list which client wants to get.", Control) \
  XX(FORMAT_SIMPLE, Dummy, DeleteCustomFiles, "List of player names which custom file directories should be cleared", Control) \
  XX(FORMAT_CREATE, LocalPlayerInfo, LocPlayerInfo, "Info about local user", Control) \
  XX(FORMAT_CREATE, LocalPlayerInfoList, LocPlayerInfoList, "List of infos about local users", Control) \
  XX(FORMAT_SIMPLE, Dummy, NatNegResult, "Result of VoN NAT negotiation from client.", Control) \
  XX(FORMAT_UPDATE_POSITION, ClientInfoObject, UpdateClientInfoDpid, "Update position of players.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ServerTime, "Server time.", Control) \
  XX(FORMAT_UPDATE, Door, UpdateDoor, "Generic update of Door (: Transport).", Update) \
  XX(FORMAT_UPDATE_POSITION, Door, UpdatePositionDoor, "Update position of Door (: Transport).", UpdPos) \
  XX(FORMAT_SIMPLE, Dummy, ServerAdmin, "Server admin info.", Control) \
  XX(FORMAT_SIMPLE, AskForVehicleDoorStateMessage, AskForVehicleDoorState, "Open or close vehicle door.", Ask) \
  XX(FORMAT_CREATE, ClientCameraPositionObject, CreateClientCameraPosition, "Position of camera of the client.", Create) \
  XX(FORMAT_UPDATE, ClientCameraPositionObject, UpdateClientCameraPosition, "Update position of camera of client.", Update) \
  XX(FORMAT_CREATE, OwnerChangedMessage, OwnerChanged, "Message is sent as an acknowledgement when owner of some object was changed.", Control) \
  XX(FORMAT_SIMPLE, Dummy, FileSignatureAsk, "File signature check.", Control) \
  XX(FORMAT_SIMPLE, Dummy, FileSignatureAnswer, "File signature check answer.", Control) \
  XX(FORMAT_SIMPLE, AskForPilotLightMessage, AskForPilotLight, "Force pilot light", Ask) \
  XX(FORMAT_CREATE, CreateRadioChannelMessage, CreateRadioChannel , "Custom radio channel properties.", Control) \
  XX(FORMAT_CREATE, RadioChannelLabelMessage, RadioChannelLabel , "Custom radio channel label.", Control) \
  XX(FORMAT_CREATE, RadioChannelCallSignMessage, RadioChannelCallSign , "Custom radio channel callSign.", Control) \
  XX(FORMAT_CREATE, RadioChannelUnitsMessage, RadioChannelUnits , "Custom radio channel participates list.", Control) \
  XX(FORMAT_SIMPLE, Dummy, SendAUMsg, "Send AutoUpdate message", Broadcast) \
  XX(FORMAT_SIMPLE, Dummy, ServerInfo, "Tell clients the server hostname.", Control) \
  XX(FORMAT_CREATE, PublicVariableToMessage, PublicVariableTo, "Update game variable on specified client.", Control) \
  XX(FORMAT_CREATE, SpawnForPlayerMessage, SpawnForPlayer, "Spawn a code on a specified client.", Control) \
  XX(FORMAT_UPDATE, Weapon, UpdateWeapon, "Generic update of Weapon (: EntityAI).", Update) \
  XX(FORMAT_UPDATE, Muzzle, UpdateMuzzle, "<embedded/> Weapon's muzzle.", Update) \
  XX(FORMAT_UPDATE_POSITION, Magazine, UpdatePositionMagazine, "Update position of Magazine", UpdPos) \
  XX(FORMAT_UPDATE, ClientInput, UpdateClientInput, "Update player input state.", Control) \
  XX(FORMAT_UPDATE, ClientOutput, UpdateClientOutput, "Update player output state.", Control) \
  XX(FORMAT_UPDATE, ActionSubscription, UpdateActionSubscription, "<embedded/> ActionSubscription in ClientOutput", Update) \
  XX(FORMAT_UPDATE, ActionToDoSubscription, UpdateActionToDoSubscription, "<embedded/> ActionSubscription in ClientOutput", Update) \
  XX(FORMAT_UPDATE, ActionDoSubscription, UpdateActionDoSubscription, "<embedded/> ActionSubscription in ClientOutput", Update) \
  XX(FORMAT_UPDATE, ActionWithCurveSubscription, UpdateActionWithCurveSubscription, "<embedded/> ActionSubscription in ClientOutput", Update) \
  XX(FORMAT_UPDATE, ItemShortcut, UpdateItemShortcut, "<embedded/> Item shortcut in ClientOutput", Update) \
  XX(FORMAT_UPDATE, InventoryItem, UpdateInventoryItem, "Generic update of InventoryItem (: EntityAI).", Update) \
  XX(FORMAT_SIMPLE, Dummy, AskForMoveItem, "Ask owner for Inventory UI action.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForRotateInventoryItem, "Ask server to rotate inventory item.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForThrowItem, "Ask owner for throwing UI action.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForDropItem, "Ask owner for Inventory UI action.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForSetItemShortcut, "Ask owner for Inventory UI action.", Ask) \
  XX(FORMAT_UPDATE, UIAction, UpdateUIAction, "<embedded/> UI action in ClientOutput", Update) \
  XX(FORMAT_SIMPLE, Dummy, ProcessAction, "Ask owner to process UI action", Ask) \
  XX(FORMAT_SIMPLE, Dummy, SubscribeRadios, "Set list of subscribed radio frequencies.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AskInventoryMenu, "Ask server for Inventory context menu.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, InventoryMenu, "Server reply containing Inventory context menu.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, InventoryMenuAction, "<embedded/> Action in InventoryMenu", Ask) \
  XX(FORMAT_SIMPLE, Dummy, ProcessRecipe, "Ask server to process recipe", Ask) \
  XX(FORMAT_UPDATE, ItemRadio, UpdateItemRadio, "Generic update of ItemRadio (: InventoryItem).", Update) \
  XX(FORMAT_UPDATE, ItemTransmitter, UpdateItemTransmitter, "Generic update of ItemTransmitter (: InventoryItem).", Update) \
  XX(FORMAT_SIMPLE, Dummy, UseItem, "Ask client to display UI attached to item", Ask) \
  XX(FORMAT_SIMPLE, Dummy, StatusChat, "Send the status text to the given client.", Control) \
  XX(FORMAT_CREATE, ExplosionDamageMessage, ExplosionDamage, "Message to request explosion effects to be simulated by the object owner (server).", Ask) \
  XX(FORMAT_SIMPLE, Dummy, InventoryMenuAttachment, "<embedded/> Attachment or Owned item in InventoryMenu", Ask) \
  XX(FORMAT_CREATE, AskForSetMagazineAmmoMessage, AskForSetMagazineAmmo, "Message for ask magazine owner to change its ammo count.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForSwapItems, "Ask owner for Inventory UI action.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForPlayerAction, "Ask server to play an action.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, SynchronizeVariable, "Server announces object variable synchronization.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, AskForTakeItemToHands, "Ask owner to take item to hands.", Ask) \
  XX(FORMAT_SIMPLE, Dummy, DiagObjectInfoAsk, "Ask for info about given object on client.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ClientDiagToServer, "Info about object on client as an answer to DiagObjectInfoAsk.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ApplyWeather,"All apply the server weather.", Control) \

#if _HELISIM_ROTORLIB

  //! Message types specific for rOTORlIB
#define NETWORK_MESSAGE_TYPES_RTD(XX) \
  XX(FORMAT_CREATE, HelicopterRTD, CreateHelicopterRTD, "Create Helicopter (: Vehicle).", Create) \
  XX(FORMAT_UPDATE, HelicopterRTD, UpdateHelicopterRTD, "Generic update of rotorlib Helicopter (: Vehicle).", Update) \
  XX(FORMAT_UPDATE_POSITION, HelicopterRTD, UpdatePositionHelicopterRTD, "Update position of rotorlib Helicopter (: Vehicle).", UpdPos)

#endif 



#if _ENABLE_VBS
//! Message types specific for VBS
#define NETWORK_MESSAGE_TYPES_VBS(XX) \
  XX(FORMAT_SIMPLE, Dummy, SetObjectTexture, "Set an object's texture on all clients.", Control) \
  XX(FORMAT_SIMPLE, Dummy, PublicExec, "Execute game code on clients.", Control) \

#endif

#if _VBS2
//! Message types specific for VBS
# define NETWORK_MESSAGE_TYPES_VBS2(XX) \
  XX(FORMAT_SIMPLE, Dummy, AARAskUpdate, "Ask Server to start doing stuff.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AARDoUpdate, "Get clients to update their states.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AddMPReport, "Add string to mp report on all clients.", Control) \
  XX(FORMAT_SIMPLE, Dummy, LoadIsland, "load the current island.", Control)
#endif

#if _VBS3 // respawn command
# define NETWORK_MESSAGE_TYPES_VBS3(XX) \
  XX(FORMAT_SIMPLE, Dummy, PauseSimulation,"Pause the client local simulation.", Control) \
  XX(FORMAT_SIMPLE, Dummy, ApplyWeather,"All apply the server weather.", Control) \
  XX(FORMAT_SIMPLE, Dummy, AskCommanderOverride, "Ask for Turret override", Ask)
#endif

#define NMT_DEFINE_ENUM(macro, class, name, description, group) NMT##name,

DEFINE_ENUM_BEG(NetworkMessageType)
  NMTNone = -1, // used as return value of GetNMType - no message
  NETWORK_MESSAGE_TYPES(NMT_DEFINE_ENUM)
#if _HELISIM_ROTORLIB
  NETWORK_MESSAGE_TYPES_RTD(NMT_DEFINE_ENUM)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_DEFINE_ENUM)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(NMT_DEFINE_ENUM)
# endif
#if _VBS3
  NETWORK_MESSAGE_TYPES_VBS3(NMT_DEFINE_ENUM)
#endif
#endif
  NMTN,
  NMTFirstVariant = NMTPlayer // NMTMessages is in fact not variant, but it is not an actual format anyway
DEFINE_ENUM_END(NetworkMessageType)

//! get identifier or corresponding network data types
template <class Type>
struct GetNDType
{
  //enum {value=-1};
};

/*
template <int typeId>
struct GetNMTType
{
  //typedef void *Type;
};
*/

//! register particular datatype
#define REGISTER_NDT(type,name) \
  template <> struct GetNDType< type > {enum {value=name};};
/*
  template <> struct GetNMTType< name > {typedef type Type;};
*/

template <int id>
struct NoType {};

/*
struct Test
{
  GetNMTType<NMTFloat>::Type _float;
  GetNMTType<NMTInt>::Type _int;
};
*/

#define NETWORK_DATA_TYPES(XX) \
  XX(bool,Bool,"Boolean value. Transferred as single byte.") \
  XX(int,Integer,"4 bytes long integer value.") \
  XX(NetInt64,Int64,"8 bytes long integer value.") \
  XX(float,Float,"4 bytes long real value.") \
  XX(RString,String,"Null terminated string of bytes, including the trailing zero.") \
  XX(RStringB,StringB,"Null terminated string of bytes, including the trailing zero.") \
  XX(AutoArray<char>,RawData,"Array of bytes. Integer for size, then size bytes of content.") \
  XX(Time,Time,"4 bytes long integer value (time in ms).") \
  XX(Vector3,Vector,"Vector of 3 floats.") \
  XX(Matrix3,Matrix,"Matrix of 3 x 3 floats") \
  XX(AutoArray<bool>,BoolArray,"Array of booleans. Integer for size, then size booleans of content.") \
  XX(AutoArray<int>,IntArray,"Array of integers. Integer for size, then size integers of content.") \
  XX(AutoArray<float>,FloatArray,"Array of floats. Integer for size, then size floats of content.") \
  XX(AutoArray<RString>,StringArray,"Array of strings. Integer for size, then size strings of content.") \
  XX(AutoArray<RStringB>,StringBArray,"Array of strings. Integer for size, then size strings of content.") \
  XX(RadioSentence,Sentence,"Radio message. Integer for number of words, then pairs: string id of word, float pause after word.") \
  XX(NetworkMessage,Object,"Nested message (encoded as whole new message)") \
  XX(AutoArray<NetworkMessage>,ObjectArray,"Array of nested messages. Integer for size, then size of messages.") \
  XX(NoType<3>,ObjectSRef,"Nested message reference (encoded as whole new message)") \
  XX(NetworkId,Ref,"Reference to object. Pair of integer values: id of client where object was created, id of object unique on this client.") \
  XX(AutoArray<NetworkId>,RefArray,"Array of references. Integer for size, then size of references.") \
  XX(NoType<2>,Data,"<notused/> Generic data used for dynamic definition of formats.") \
  XX(AutoArray<XUID>,XUIDArray,"Array of Xbox user ids. Integer for size, then size of ids.") \
  XX(XNADDR,XNADDR,"Xbox network address.") \
  XX(AutoArray<XONLINE_STAT>,XOnlineStatArray,"List of Xbox Live statistics entries.") \
  XX(LocalizedString,LocalizedString,"Client-side localized string.") \
  XX(AutoArray<LocalizedString>,LocalizedStringArray,"Array of client-side localized strings.") \
  XX(Variables,Variables,"List of variables in the object's space.") \
  XX(AutoArray<SynchronizedVariableInfo>,SynchronizedVariables,"List of variable definitions including handlers.") \

#define NDT_DEFINE_ENUM(type,name,description) NDT##name,
#define NDT_REGISTER_TYPE(type,name,description) REGISTER_NDT(type,NDT##name)

//! Types of message items
enum NetworkDataType
{
  NETWORK_DATA_TYPES(NDT_DEFINE_ENUM)
};

NETWORK_DATA_TYPES(NDT_REGISTER_TYPE)

// register various types that have identical handling as some basic type
REGISTER_NDT(AutoArray<RStringI>,NDTStringArray)
REGISTER_NDT(AutoArray<RStringS>,NDTStringArray)
REGISTER_NDT(AutoArray<RStringIB>,NDTStringArray)
REGISTER_NDT(StaticArrayAuto<float>,NDTFloatArray)
REGISTER_NDT(StaticArrayAuto<int>,NDTIntArray)
REGISTER_NDT(StaticArrayAuto<RString>,NDTStringArray)


//! Types of items compression
enum NetworkCompressionType
{
  NCTNone,
  NCTSmallUnsigned, //!< special compression for unsigned int
  NCTSmallSigned, //!< special compression for signed int
  NCTDefault=NCTSmallSigned, //!< default compression for each data type
  NCTStringGeneric=NCTDefault, //!< generic string table
  NCTStringMove, //!< string table for moves
  NCTFloat0To1, //!< float 0 to 1 (8b), vector with size 1 or less (32b)
  NCTFloat0To2, //!< float 0 to 2 (8b)
  NCTFloatM1ToP1, //!< float -1 to 1 (8b)
  NCTFloatMPIToPPI, //!< float -pi to pi (8b)
  NCTFloatMostly0To1, //!< float, mostly 0 to 1 (8b-40b)
  NCTFloatAngle, //!< float, mostly -pi to pi (8b-40b)
  /** camera position - used to control error calculation (1m..10m precision)
  10 m precision required across range:
  x,z from 0..12800, y from 0..1500
  */
  NCTVectorPositionCamera,
  /** generic position - 1 cm precision required across range:
  x,z from 0..12800, y from 0..1500
  */
  NCTVectorPosition,
  //! matrix orientation - matrix is assumed orthogonal
  NCTMatrixOrientation,
};

//! Algorithms for error calculation
#ifndef DECL_ENUM_NETWORK_MESSAGE_ERROR_TYPE
#define DECL_ENUM_NETWORK_MESSAGE_ERROR_TYPE
DECL_ENUM(NetworkMessageErrorType)
#endif
DEFINE_ENUM_BEG(NetworkMessageErrorType)
  //! header - no error, not included in error and diff computation
  ET_HEADER,
  //! no error
  ET_NONE,
  //! 1 if not equal, otherwise 0
  ET_NOT_EQUAL,
  //! absolute value of difference
  ET_ABS_DIF,
  //! square of difference
  ET_SQUARE_DIF,
  //! number of different items in array
  ET_NOT_EQUAL_COUNT,
  //! number of items in second array, not contained in first array
  ET_NOT_CONTAIN_COUNT,
  //! specialized muzzles difference
  ET_MUZZLES,
  //! difference of derivations (role of time is incorporated)
  ET_DER_DIF,
DEFINE_ENUM_END(NetworkMessageErrorType)

#if _HELISIM_LARGE_OBJECTID
  #define NDTOID NDTInt64
  #define NOIDIntComp NCTSmallUnsigned
#else
  #define NDTOID NDTInteger
  #define NOIDIntComp NCTSmallUnsigned
#endif


#define DEFVALUE(type, val) type defValue = val
#define DEFVALUE_MSG(val) NetworkMessageType defValue = val
#define DEFVALUE_DEF(type) type defValue = type()
#define DEFVALUENULL NetworkId defValue
#define DEFVALUEREFARRAY AutoArray<NetworkId> defValue
#define DEFVALUEBOOLARRAY AutoArray<bool> defValue
#define DEFVALUEINTARRAY AutoArray<int> defValue
#define DEFVALUEFLOATARRAY AutoArray<float> defValue
#define DEFVALUESTRINGARRAY AutoArray<RString> defValue
#define DEFVALUESTRINGBARRAY AutoArray<RStringB> defValue
#define DEFVALUERAWDATA AutoArray<char> defValue
#define DEFVALUEXUIDARRAY AutoArray<XUID> defValue
#define DEFVALUEFORMATSPECS NetworkFormatSpecs defValue
#define DEFVALUEXNADDR XNADDR defValue; ZeroMemory(&defValue, sizeof(defValue))
#define DEFVALUEXONLINESTATARRAY AutoArray<XONLINE_STAT> defValue
#define DEFVALUELOCALIZEDSTRINGARRAY AutoArray<LocalizedString> defValue

struct NetworkFormatSpecs
{
  friend class NetworkComponent;

  //! data type
  NetworkDataType _type;
  //! compression type
  NetworkCompressionType _compression;
  //! difference computed or not
  bool _diffed;
  NetworkFormatSpecs()
  {
    _type = (NetworkDataType)-1; _defValue = NULL; _diffed = false;
  }
  NetworkFormatSpecs(const NetworkFormatSpecs &src)
  {
    _defValue = NULL;
    Copy(src);
  }
  ~NetworkFormatSpecs()
  {
    Destroy();
  }
  void operator =(const NetworkFormatSpecs &src)
  {
    if (_defValue) Destroy();
    Copy(src);
  }
  template <class Type>
  const Type *GetDefValue(const Type *dummy) const;
  template <class Type>
  Type *GetDefValue(const Type *dummy);

  // direct access to default value (encode / decode message)
  void *GetDefValuePtr() const {return _defValue;}

  void Create();
  void Destroy();
  void Copy(const NetworkFormatSpecs &src);

private:
  //! default value
  void *_defValue;
};

const size_t InvalidOffset = 0xffffffff;
//! Description of format of single message item
struct NetworkMessageFormatItem
{
  //! name of item
  RString name;
  NetworkFormatSpecs specs;
  size_t offset;

  NetworkMessageFormatItem() {offset = InvalidOffset;}

  NetworkDataType GetType() const {return specs._type;}
  NetworkCompressionType GetCompression() const {return specs._compression;}

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(MsgFormatItem)
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  //! Message update from / to this object
  /*!
    \param ctx message context
  */
  TMError TransferMsg(NetworkMessageContext &ctx);
};
TypeIsMovable(NetworkMessageFormatItem)

//! Basic network message format class (dynamic - sent from server to clients)
class NetworkMessageFormatBase : public NetworkSimpleObject
{
protected:
  //! array of items
  AutoArray<NetworkMessageFormatItem> _items;
  /// count items with specs._diffed set (size of diff transferred)
  int _diffCount;
public:
  //! Constructor
  NetworkMessageFormatBase();

  //! Returns number of format items
  int NItems() const {return _items.Size();}

  int NItemsDiffed() const {return _diffCount;}

  //! Return format item with given index
  const NetworkMessageFormatItem &GetItem(int i) const {return _items[i];}
  //! Return format item with given index
  NetworkMessageFormatItem &GetItem(int i) {return _items[i];}

  void SetOffset(RString name, size_t offset);
/*
  void SetOffset(int index, size_t offset)
  {
    Assert(index >= 0 && index < _items.Size());
    _items[index].offset = offset;
  }
*/

  DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(MsgFormat)
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);

  bool IsDiff(int i, MessageDiffApply &diff) const;
  //! Destroy array of items and map
  void Clear();
};
TypeIsMovable(NetworkMessageFormatBase)

/// simple format has nothing to update
static inline void UpdateMessageFormatSimple(NetworkMessageFormatBase *format, int &index)
{
}

//! orthonormal matrix can be encoded in four float numbers
// plus sign information about two float numbers

struct EncodedMatrix3
{
  // single values encoded as -1 to +1 in 16b representation
  // encoded col 1 (direction up)
  short _01c,_11c;
  // encoded col 2 (direction)
  short _02c,_12c;
  char _21sign;
  char _22sign;

  //! encode 3x3 matrix
  void Encode(const Matrix3 &m);
  //! decode 3x3 matrix
  void Decode(Matrix3 &m) const;
  //! decode 3x3 part of 3x4 matrix
  void Decode(Matrix4 &m) const;
  //@{ decode columns of 3x3 matrix - may be slightly inaccurate
  Vector3 DirectionUp() const;
  Vector3 Direction() const;
  //@}
};

//! Error coeficient for nonimportant values
#define ERR_COEF_VALUE_MINOR  0.1f
//! Error coeficient for normal values
#define ERR_COEF_VALUE_NORMAL 1
//! Error coeficient for important values
#define ERR_COEF_VALUE_MAJOR  10
//! Error coeficient for mode - like values
#define ERR_COEF_MODE         100
//! Error coeficient for structural values
#define ERR_COEF_STRUCTURE    10000
//! Error coeficient for game critical values
#define ERR_COEF_IMMEDIATE    1e10f

//! Item error description
struct NetworkMessageErrorInfo
{
  //! algorithm type
  NetworkMessageErrorType type;
  //! multiplier
  float coef;
};
TypeIsMovableZeroed(NetworkMessageErrorInfo)

#if ! _VBS3_SHAPEFILES //otherwise we get a double type declaration
TypeIsSimple(const char *)
#endif
//! Extended network message format class (static - used also for error calculations)
class NetworkMessageFormat : public NetworkMessageFormatBase
{
  typedef NetworkMessageFormatBase base;

protected:
  //! Error descriptions
  AutoArray<NetworkMessageErrorInfo> _errors;

#if DOCUMENT_MSG_FORMATS
public:
  //! Items documentation
  AutoArray<const char *> _descriptions;
#endif

public:
  //! Return error description for given item
  const NetworkMessageErrorInfo &GetErrorInfo(int i) const {return _errors[i];}
  //! Return error description for given item
  NetworkMessageErrorInfo &GetErrorInfo(int i) {return _errors[i];}
  //! Add new item
  /*!
  \param name name of item
  \param type data type
  \param compression compression type
  \param defValue default value
  \param description item documentation
  \param errType error algorithm
  \param errCoef error multiplier
  */
  template <class Type>
  int Add
  (
    const char *name,
    NetworkDataType type,
    NetworkCompressionType compression,
    const Type &defValue,
    const char *description,
    NetworkMessageErrorType errType = ET_HEADER,
    float errCoef = 0
  )
  {
    int index = Add(name, type, compression, description, errType, errCoef);
    NetworkFormatSpecs &specs = _items[index].specs;
    specs.Create();
    Type *ptr = specs.GetDefValue(&defValue);
    *ptr = defValue;
    return index;
  }

  //! Add new item
  /*!
  \param name name of item
  \param type data type
  \param compression compression type
  \param description item documentation
  \param errType error algorithm
  \param errCoef error multiplier
  */
  int Add
  (
    const char *name,
    NetworkDataType type,
    NetworkCompressionType compression,
    const char *description,
    NetworkMessageErrorType errType = ET_HEADER,
    float errCoef = 0
  );

  int Find(const char *name) const;

  void Clear();
};

NetworkMessage *CreateNetworkMessage(NetworkMessageType type);

#define TRANSFER \
  { \
    if (_sending) msgValue = value; \
    else value = msgValue; \
    return TMOK; \
  }

#define TRANSFER_REF(type) \
  { \
    if (_sending) \
    { \
      if (value) \
      { \
        msgValue = value->GetNetworkId(); \
        if (msgValue.IsNull())  \
          ErrF("Ref to nonnetwork object %s", (const char *)value->GetDebugName()); \
      } \
      else msgValue = NetworkId::Null(); \
    } \
    else \
    { \
      if (msgValue.IsNull()) \
        value = NULL; \
      else \
        value = dynamic_cast<type *>(_component->GetObject(msgValue)); \
    } \
    return TMOK; \
  }

//! Network message context
/*!
Passed into TransferMsg functions of network objects.
Contains all info needed for network transfer (and message itself).
*/
class NetworkMessageContext
{
protected:
  //! message itself
  NetworkMessage *_msg;
  //! server / client class which called TransferMsg
  INetworkComponent *_component;  
  //! message update class
  NetworkMessageClass _cls;
  //! sending / receiving message
  bool _sending;
  //! initial (guaranteed) update of network object
  bool _initialUpdate;

public:
  //! Constructor
  /*!
  \param msg message itself
  \param format message format
  \param component server / client class which called TransferMsg
  \param client not used
  \param sending sending / receiving message
  */
  NetworkMessageContext(NetworkMessage *msg, INetworkComponent *component, DWORD client, bool sending)
  {
    _msg = msg;
    _component = component; //_client = client;
    _sending = sending; _cls = NMCUpdateGeneric;
    _initialUpdate = false;
  }
  //! Constructor
  /*!
  \param msg message itself
  \param format message format
  \param ctx parent message context
  */
  NetworkMessageContext(NetworkMessage *msg, NetworkMessageContext &ctx)
  {
    _msg = msg;
    _component = ctx._component; //_client = ctx._client;
    _sending = ctx._sending; _cls = ctx._cls;
    _initialUpdate = ctx._initialUpdate;
  }

  //! Return if message is to send
  bool IsSending() const {return _sending;}
  //! Return message update class
  NetworkMessageClass GetClass() const {return _cls;}
  //! Set message update class
  void SetClass(NetworkMessageClass cls) {_cls = cls;}
  //! Return if initial (guaranteed) update is performed
  bool GetInitialUpdate() const {return _initialUpdate;}
  //! Set if initial (guaranteed) update is performed
  void SetInitialUpdate() {_initialUpdate = true;}
  //! Return message itself
  NetworkMessage *GetNetworkMessage() const {return _msg;}
  //! Return server / client class which called TransferMsg
  INetworkComponent *GetComponent() const {return _component;}

  //! Return time when message was sent
  Time GetMsgTime() const {return _msg->time;}
  // DWORD GetClient() const {return _client;}

  // Simple types transfer

  //@{
  //! Transfer single item of message (raw data)
  TMError GetRaw(AutoArray<char> &msgValue, void *&value, int &size)
  {
    Assert(!_sending);
    value = msgValue.Data();
    size = msgValue.Size();
    return TMOK;
  }
  TMError SendRaw(AutoArray<char> &msgValue, void *value, int size)
  {
    Assert(!_sending);
    msgValue.Copy((const char *)value, size);
    return TMOK;
  }
  //@}
  //@{
  //! Transfer single item of message
  /*!
  \param index index of item in message
  \param value transferred value
  \name Transfer functions (index based)
  */
  TMError Transfer(bool &msgValue, bool &value) TRANSFER
  TMError Transfer(int &msgValue, int &value) TRANSFER
  TMError Transfer(NetInt64 &msgValue, NetInt64 &value) TRANSFER
  TMError Transfer(LocalizedString &msgValue, LocalizedString &value) TRANSFER
  TMError Transfer(float &msgValue, float &value) TRANSFER
  TMError Transfer(RString &msgValue, RString &value) TRANSFER
  TMError Transfer(Time &msgValue, Time &value) TRANSFER
  TMError Transfer(Vector3 &msgValue, Vector3 &value) TRANSFER
  TMError Transfer(Matrix3 &msgValue, Matrix3 &value) TRANSFER
  TMError Transfer(XNADDR &msgValue, XNADDR &value) TRANSFER
  TMError Transfer(CreatorId &msgValue, CreatorId &value) TRANSFER
  TMError Transfer(NetworkId &msgValue, NetworkId &value) TRANSFER
  TMError Transfer(int &msgValue, unsigned char &value) TRANSFER
  TMError Transfer(UserAction &msgValue, UserAction &value) TRANSFER // TODO: not portable, we need int as a raw format on the left side
  TMError Transfer(InputInterface::CurveFactor &msgValue, InputInterface::CurveFactor &value) TRANSFER
  TMError Transfer(Variables &msgValue, EntityAI *value);
  TMError Transfer(AutoArray<RString> &msgValue, RefArray<Texture> &value);
  TMError Transfer(AutoArray<RString> &msgValue, RefArray<TexMaterial> &value);
  TMError Transfer(AutoArray<RStringB> &msgValue, RefArray<Texture> &value);
  TMError Transfer(AutoArray<RStringB> &msgValue, RefArray<TexMaterial> &value);

  template <class Type, class Allocator>
  TMError Transfer(AutoArray<Type> &msgValue, AutoArray<Type, Allocator> &value) TRANSFER
  template <class Type>
  TMError Transfer(AutoArray<Type> &msgValue, StaticArrayAuto<Type> &value)
  {
    if (_sending) msgValue.Copy(value.Data(),value.Size());
    else value.Copy(msgValue.Data(),msgValue.Size());
    return TMOK;
  }
  template <class Type>
  TMError Transfer(AutoArray<Type> &msgValue, Temp<Type> &value)
  {
    if (_sending) msgValue.Copy(value.Data(),value.Size());
    else value.Realloc(msgValue.Data(),msgValue.Size());
    return TMOK;
  }

  TMError Transfer(RadioSentence &msgValue, RadioSentence &value);

  // References transfer

  template <class Type>
  TMError TransferPtrRef(NetworkId &msgValue, Type *&value) TRANSFER_REF(Type)
  template <class RefType>
  TMError TransferRef(NetworkId &msgValue, RefType &value) TRANSFER_REF(typename RefType::ItemType)

  template <class MsgContainer, class Container>
  TMError TransferRefs(MsgContainer &msgValue, Container &value)
  {
    if (_sending)
    {
      int n = value.Size();
      msgValue.Resize(n);
      for (int i=0; i<n; i++)
      {
        typename Container::DataType::ItemType *item = value[i];
        if (item)
        {
          msgValue[i] = item->GetNetworkId();
          if (msgValue[i].IsNull())
            ErrF("Ref to nonnetwork object %s",(const char *)item->GetDebugName());
        }
        else msgValue[i] = NetworkId::Null();
      }
    }
    else
    {
      int n = msgValue.Size();
      value.Resize(n);
      for (int i=0; i<n; i++)
      {
        NetworkId id = msgValue[i];
        if (id.IsNull())
          value[i] = NULL;
        else
          value[i] = dynamic_cast<typename Container::DataType::ItemType *>(_component->GetObject(id));
      }
    }
    return TMOK;
  }

  // Subobjects transfer

  template <class Type>
  TMError Transfer(Ref<NetworkMessage> &msgValue, Ref<Type> &value)
  {
    if (_sending)
    {
      if (value)
      {
        msgValue = ::CreateNetworkMessage(value->GetNMType(_cls));
        NetworkMessageContext ctx(msgValue, *this);
        TMCHECK(value->TransferMsg(ctx))
      }
      else msgValue = NULL;
    }
    else
    {
      if (msgValue)
      {
        NetworkMessageContext ctx(msgValue, *this);
        value = Type::CreateObject(ctx);
        if (value)
        {
          TMCHECK(value->TransferMsg(ctx))
        }
      }
      else value = NULL;
    }
    return TMOK;
  }

  template <class Type>
  TMError TransferObject(Ref<NetworkMessage> &msgValue, Type &value)
  {
    if (_sending)
    {
      msgValue = ::CreateNetworkMessage(value.GetNMType(_cls));
      NetworkMessageContext ctx(msgValue, *this);
      TMCHECK(value.TransferMsg(ctx))
    }
    else if (msgValue) // subobject may be removed in newer version, in such case do not attempt to transfer it
    {
      Assert(msgValue)
      NetworkMessageContext ctx(msgValue, *this);
      TMCHECK(value.TransferMsg(ctx))
    }
    return TMOK;
  }
  template <class Type>
  TMError TransferContent(Ref<NetworkMessage> &msgValue, Type &value)
  {
    if (_sending)
    {
      if (value)
      {
        msgValue = ::CreateNetworkMessage(value->GetNMType(_cls));
        NetworkMessageContext ctx(msgValue, *this);
        TMCHECK(value->TransferMsg(ctx))
      }
      else msgValue = NULL;
    }
    else
    {
      if (msgValue)
      {
        NetworkMessageContext ctx(msgValue, *this);
        if (!value) value = value->CreateObject(ctx); // avoid creating of new object for EntityAI::_supply
        if (value)
        {
          TMCHECK(value->TransferMsg(ctx))
        }
      }
      else value = NULL;
    }
    return TMOK;
  }
  template <class Type, class Parameter>
  TMError TransferContentPar(Ref<NetworkMessage> &msgValue, Type &value, Parameter par)
  {
    if (_sending)
    {
      if (value)
      {
        msgValue = ::CreateNetworkMessage(value->GetNMType(_cls));
        NetworkMessageContext ctx(msgValue, *this);
        TMCHECK(value->TransferMsg(par, ctx))
      }
      else msgValue = NULL;
    }
    else
    {
      if (msgValue)
      {
        NetworkMessageContext ctx(msgValue, *this);
        if (!value) value = value->CreateObject(ctx); // avoid creating of new object for EntityAI::_supply
        if (value)
        {
          TMCHECK(value->TransferMsg(par, ctx))
        }
      }
      else value = NULL;
    }
    return TMOK;
  }

  template <class Type>
  TMError TransferArray(RefArray<NetworkMessage> &msgValue, AutoArray<Type> &value)
  {
    if (_sending)
    {
      int n = value.Size();
      msgValue.Resize(n);
      for (int i=0; i<n; i++)
      {
        msgValue[i] = ::CreateNetworkMessage(value[i].GetNMType(_cls));
        NetworkMessageContext ctx(msgValue[i], *this);
        TMCHECK(value[i].TransferMsg(ctx))
      }
    }
    else
    {
      int n = msgValue.Size();
      value.Resize(n);
      for (int i=0; i<n; i++)
      {
        Assert(msgValue[i])
        NetworkMessageContext ctx(msgValue[i], *this);
        TMCHECK(value[i].TransferMsg(ctx))
      }
    }
    return TMOK;
  }
  template <class Type>
  TMError TransferArray(RefArray<NetworkMessage> &msgValue, StaticArrayAuto<Type> &value)
  {
    if (_sending)
    {
      int n = value.Size();
      msgValue.Resize(n);
      for (int i=0; i<n; i++)
      {
        msgValue[i] = ::CreateNetworkMessage(value[i].GetNMType(_cls));
        NetworkMessageContext ctx(msgValue[i], *this);
        TMCHECK(value[i].TransferMsg(ctx))
      }
    }
    else
    {
      int n = msgValue.Size();
      value.Resize(n);
      for (int i=0; i<n; i++)
      {
        Assert(msgValue[i])
        NetworkMessageContext ctx(msgValue[i], *this);
        TMCHECK(value[i].TransferMsg(ctx))
      }
    }
    return TMOK;
  }
  template <class Type>
  TMError TransferArraySimple(RefArray<NetworkMessage> &msgValue, StaticArrayAuto<Type> &value)
  {
    if (_sending)
    {
      int n = value.Size();
      msgValue.Resize(n);
      for (int i=0; i<n; i++)
      {
        msgValue[i] = ::CreateNetworkMessage(value[i].GetNMType(_cls));
        NetworkMessageContext ctx(msgValue[i], *this);
        TMCHECK(value[i].TransferMsgSimple(ctx))
      }
    }
    else
    {
      int n = msgValue.Size();
      value.Resize(n);
      for (int i=0; i<n; i++)
      {
        Assert(msgValue[i])
        NetworkMessageContext ctx(msgValue[i], *this);
        TMCHECK(value[i].TransferMsgSimple(ctx))
      }
    }
    return TMOK;
  }
  template <class Type>
  TMError TransferArray(RefArray<NetworkMessage> &msgValue, RefArray<Type> &value)
  {
    if (_sending)
    {
      int n = value.Size();
      msgValue.Resize(n);
      for (int i=0; i<n; i++)
      {
        if (value[i])
        {
          msgValue[i] = ::CreateNetworkMessage(value[i]->GetNMType(_cls));
          NetworkMessageContext ctx(msgValue[i], *this);
          TMCHECK(value[i]->TransferMsg(ctx))
        }
        else msgValue[i] = NULL;
      }
    }
    else
    {
      int n = msgValue.Size();
      value.Resize(n);
      for (int i=0; i<n; i++)
      {
        if (msgValue[i])
        {
          NetworkMessageContext ctx(msgValue[i], *this);
          value[i] = Type::CreateObject(ctx);
          if (value[i])
          {
            TMCHECK(value[i]->TransferMsg(ctx))
          }
        }
        else value[i] = NULL;
      }
    }
    return TMOK;
  }
  template <class Type>
  TMError TransferObjArray(RefArray<NetworkMessage> &msgValue, RefArray<Type> &value)
  {
    if (_sending)
    {
      int n = value.Size();
      msgValue.Resize(n);
      for (int i=0; i<n; i++)
      {
        if (value[i])
        {
          msgValue[i] = ::CreateNetworkMessage(value[i]->GetNMType(_cls));
          NetworkMessageContext ctx(msgValue[i], *this);
          TMCHECK(value[i]->TransferMsg(ctx))
        }
        else msgValue[i] = NULL;
      }
    }
    else
    {
      int n = msgValue.Size();
      value.Resize(n);
      for (int i=0; i<n; i++)
      {
        if (msgValue[i])
        {
          NetworkMessageContext ctx(msgValue[i], *this);
          value[i] = Type::CreateObject(ctx);
          if (value[i])
          {
            TMCHECK(value[i]->TransferMsg(ctx))
          }
        }
        else value[i] = NULL;
      }
    }
    return TMOK;
  }
  //@}

  //! Transfer single item of message (type NDTData - generic data with format)
  /*!
  \param index index of item in message
  \param type data type
  \param compression compression type
  \param defVal transferred data itself
  */
  TMError Transfer
  (
    NetworkFormatSpecs &msgValue,
    NetworkFormatSpecs &value
  ) TRANSFER

  //! Retrieves network id of object from given item of message
  /*!
  \param index index of item in message
  \param id output id
  */
  TMError GetId(NetworkId &msgValue, NetworkId &value) TRANSFER
  //! Replace network id of object in given item of message
  /*!
  \param index index of item in message
  \param id new id
  */
  TMError SetId(NetworkId &msgValue, NetworkId &value) TRANSFER
  //! Retrieves array of network ids of objects from given item of message
  /*!
  \param index index of item in message
  \param array output array of ids
  */
  TMError GetIds(AutoArray<NetworkId> &msgValue, AutoArray<NetworkId> &value) TRANSFER

  //! Log message into debug output
  /*!
  \param level debug level
  \param indent indentation string
  */
  void LogMessage(NetworkMessageFormatBase *format, int level, RString indent) const;
};

class NetworkMessageError
{
  #if _PROFILE
    #define COLLECT_ERROR_STATS 1
  #endif
  #if COLLECT_ERROR_STATS
  // store error information
  struct ErrorItem
  {
    const char *name;
    float error;

    ErrorItem(){}
    ErrorItem(const char *name, float error):name(name),error(error){}
    ClassIsSimple(ErrorItem);
  };
  AutoArray<ErrorItem> _errors;
  #endif

  public:
  void AddError(const char *name, float error)
  {
    #if COLLECT_ERROR_STATS
    _errors.Add(ErrorItem(name,error));
    #endif
  }
};

class NetworkMessageContextWithError: public NetworkMessageContext
{
  NetworkMessageError &_errors;

  public:

  NetworkMessageContextWithError(NetworkMessage *msg, INetworkComponent *component, DWORD client, bool sending, NetworkMessageError &error):NetworkMessageContext(msg,component,client,sending),_errors(error){}
  NetworkMessageContextWithError(NetworkMessage *msg, NetworkMessageContextWithError &ctx):NetworkMessageContext(msg,ctx),_errors(ctx._errors){}

};

// macros
#define PREPARE_TRANSFER_MSG(name,msg) \
  Assert(dynamic_cast<NetworkMessage##name *>(msg)); \
  NetworkMessage##name *message = static_cast<NetworkMessage##name *>(msg); (void)message;

#define PREPARE_TRANSFER(name) PREPARE_TRANSFER_MSG(name,ctx.GetNetworkMessage())

#define TRANSF_BASE(name, member) \
  ctx.Transfer(message->_##name, member)
#define TRANSF_EX(name, member)\
  TMCHECK(TRANSF_BASE(name, member))
#define TRANSF(name)\
  TRANSF_EX(name, _##name)
#define TRANSF_ENUM_EX(name, member)\
  TRANSF_EX(name, (int &)member)
#define TRANSF_ENUM(name)\
  TRANSF_EX(name, (int &)_##name)

#define TRANSF_REF_BASE(name, member) \
  ctx.TransferRef(message->_##name, member)
#define TRANSF_REF_EX(name, member) \
  TMCHECK(TRANSF_REF_BASE(name, member))
#define TRANSF_REF(name) \
  TRANSF_REF_EX(name, _##name)

#define TRANSF_PTR_REF_BASE(name, member) \
  ctx.TransferPtrRef(message->_##name, member)
#define TRANSF_PTR_REF_EX(name, member) \
  TMCHECK(TRANSF_PTR_REF_BASE(name, member))
#define TRANSF_PTR_REF(name) \
  TRANSF_PTR_REF_EX(name, _##name)

#define TRANSF_REFS_BASE(name, member) \
  ctx.TransferRefs(message->_##name, member)
#define TRANSF_REFS_EX(name, member) \
  TMCHECK(TRANSF_REFS_BASE(name, member))
#define TRANSF_REFS(name) \
  TRANSF_REFS_EX(name, _##name)

#define TRANSF_OBJECT_BASE(name, member) \
  ctx.TransferObject(message->_##name, member)
#define TRANSF_OBJECT_EX(name, member) \
  TMCHECK(TRANSF_OBJECT_BASE(name, member))
#define TRANSF_OBJECT(name) \
  TRANSF_OBJECT_EX(name, _##name)

#define TRANSF_OBJ_ARRAY_BASE(name, member) \
  ctx.TransferObjArray(message->_##name, member)
#define TRANSF_OBJ_ARRAY_EX(name, member) \
  TMCHECK(TRANSF_OBJ_ARRAY_BASE(name, member))
#define TRANSF_OBJ_ARRAY(name) \
  TRANSF_OBJ_ARRAY_EX(name, _##name)

#define TRANSF_ARRAY_BASE(name, member) \
  ctx.TransferArray(message->_##name, member)
#define TRANSF_ARRAY_EX(name, member) \
  TMCHECK(TRANSF_ARRAY_BASE(name, member))
#define TRANSF_ARRAY(name) \
  TRANSF_ARRAY_EX(name, _##name)

#define TRANSF_ARRAY_SIMPLE_BASE(name, member) \
  ctx.TransferArraySimple(message->_##name, member)
#define TRANSF_ARRAY_SIMPLE_EX(name, member) \
  TMCHECK(TRANSF_ARRAY_SIMPLE_BASE(name, member))
#define TRANSF_ARRAY_SIMPLE(name) \
  TRANSF_ARRAY_SIMPLE_EX(name, _##name)

#define TRANSF_CONTENT_BASE(name, member) \
  ctx.TransferContent(message->_##name, member)
#define TRANSF_CONTENT_EX(name, member) \
  TMCHECK(TRANSF_CONTENT_BASE(name, member))
#define TRANSF_CONTENT(name) \
  TRANSF_CONTENT_EX(name, _##name)

#define TRANSF_CONTENT_PAR_BASE(name, member, par) \
  ctx.TransferContentPar(message->_##name, member, par)
#define TRANSF_CONTENT_PAR_EX(name, member, par) \
  TMCHECK(TRANSF_CONTENT_PAR_BASE(name, member, par))
#define TRANSF_CONTENT_PAR(name, par) \
  TRANSF_CONTENT_PAR_EX(name, _##name, par)

#define TRANSF_GET_ID_BASE(name, member) \
  ctx.GetId(message->_##name, member)

#define TRANSF_GET_IDS_BASE(name, member) \
  ctx.GetIds(message->_##name, member)

#define TRANSF_GET_RAW_BASE(name, data, size) \
  ctx.GetRaw(message->_##name, data, size)
#define TRANSF_GET_RAW(name, data, size) \
  TMCHECK(TRANSF_GET_RAW_BASE(name, data, size))

#define TRANSF_SEND_RAW_BASE(name, data, size) \
  ctx.SendRaw(message->_##name, data, size)
#define TRANSF_SEND_RAW(name, data, size) \
  TMCHECK(TRANSF_SEND_RAW_BASE(name, data, size))

#define TRANSF_BITBOOL_EX(name, member) \
  { \
  bool t = member; \
  TRANSF_EX(name, t) \
  member = t; \
  }
#define TRANSF_BITBOOL(name) \
  TRANSF_BITBOOL_EX(name, _##name)

#define DIFF_FORMAT(MessageName,name,ids,XX) XX(MessageName, bool, diff##name, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Difficulty settings"), TRANSF)

#define MISSION_HEADER_MSG(MessageName, XX) \
  XX(MessageName, RString, gameType, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Game type of mission"), TRANSF) \
  XX(MessageName, RString, island, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Island (map), where mission is placed"), TRANSF) \
  XX(MessageName, LocalizedString, name, NDTLocalizedString, LocalizedString, NCTNone, DEFVALUE_DEF(LocalizedString), DOC_MSG("Name of mission"), TRANSF) \
  XX(MessageName, LocalizedString, description, NDTLocalizedString, LocalizedString, NCTNone, DEFVALUE_DEF(LocalizedString), DOC_MSG("Description of mission"), TRANSF) \
  XX(MessageName, RString, fileName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of mission file"), TRANSF) \
  XX(MessageName, RString, fileDir, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Directory, where mission file is placed"), TRANSF) \
  XX(MessageName, int, fileSizeL, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Size of mission file (low DWORD)"), TRANSF) \
  XX(MessageName, int, fileSizeH, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Size of mission file (high DWORD)"), TRANSF) \
  XX(MessageName, int, fileCRC, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("CRC of mission file"), TRANSF) \
  XX(MessageName, bool, createPlayers, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Use Lobby screen to assign players to roles"), TRANSF) \
  XX(MessageName, int, maxPlayers, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Max player count"), TRANSF) \
  XX(MessageName, RString, diffName, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Game difficulty"), TRANSF) \
  XX(MessageName, bool, disabledAI, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("AI is disabled"), TRANSF) \
  XX(MessageName, bool, enableAIDisable, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("AI can be disabled"), TRANSF) \
  XX(MessageName, bool, aiKills, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Write AI kills into statistics"), TRANSF) \
  XX(MessageName, bool, updateOnly, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("This message is only update of (recently sent) mission info"), TRANSF) \
  XX(MessageName, bool, hasMissionParams, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("User mission parametrized via .par file"), TRANSF) \
  XX(MessageName, bool, noCopy, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Mission is inside addon - we need not to copy it."), TRANSF) \
  DIFFICULTY_TYPE_ENUM(DIFF_FORMAT, MessageName, XX) \
  XX(MessageName, FindArrayRStringCI, addOns, NDTStringArray, AutoArray<RString>, NCTNone, DEFVALUESTRINGARRAY, DOC_MSG("List of used addons"), TRANSF) \
  XX(MessageName, Time, estimatedEndTime, NDTTime, Time, NCTNone, DEFVALUE(Time, TIME_MIN), DOC_MSG("Time of estimated end of mission"), TRANSF) \
  XX(MessageName, LocalizedString, titleParam1, NDTLocalizedString, LocalizedString, NCTNone, DEFVALUE_DEF(LocalizedString), DOC_MSG("Mission parameter - title"), TRANSF) \
  XX(MessageName, AutoArray<float>, valuesParam1, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Mission parameter - list of values"), TRANSF) \
  XX(MessageName, AutoArray<LocalizedString>, textsParam1, NDTLocalizedStringArray, AutoArray<LocalizedString>, NCTNone, DEFVALUELOCALIZEDSTRINGARRAY, DOC_MSG("Mission parameter - list of value names"), TRANSF) \
  XX(MessageName, float, defValueParam1, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter - default value"), TRANSF) \
  XX(MessageName, LocalizedString, titleParam2, NDTLocalizedString, LocalizedString, NCTNone, DEFVALUE_DEF(LocalizedString), DOC_MSG("Mission parameter - title"), TRANSF) \
  XX(MessageName, AutoArray<float>, valuesParam2, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Mission parameter - list of values"), TRANSF) \
  XX(MessageName, AutoArray<LocalizedString>, textsParam2, NDTLocalizedStringArray, AutoArray<LocalizedString>, NCTNone, DEFVALUELOCALIZEDSTRINGARRAY, DOC_MSG("Mission parameter - list of value names"), TRANSF) \
  XX(MessageName, float, defValueParam2, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter - default value"), TRANSF) \
  XX(MessageName, AutoArray<float>, sizesParamArray, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Mission parameter - list of values"), TRANSF) \
  XX(MessageName, AutoArray<LocalizedString>, textsParamArray, NDTLocalizedStringArray, AutoArray<LocalizedString>, NCTNone, DEFVALUELOCALIZEDSTRINGARRAY, DOC_MSG("Mission parameter - list of value names"), TRANSF) \
  XX(MessageName, AutoArray<float>, valuesParamArray, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Mission parameter - list of values"), TRANSF) \
  XX(MessageName, AutoArray<LocalizedString>, titleParamArray, NDTLocalizedStringArray, AutoArray<LocalizedString>, NCTNone, DEFVALUELOCALIZEDSTRINGARRAY, DOC_MSG("Mission parameter - list of value names"), TRANSF) \
  XX(MessageName, AutoArray<float>, defaultParamArray, NDTFloatArray, AutoArray<float>, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Mission parameter - list of values"), TRANSF) \
  XX(MessageName, int, forceCameraType, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Force 1st/3rd person"), TRANSF) \
	XX(MessageName, float, environmentTimeMul, NDTFloat, float, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter - multiplier of environment update speed"), TRANSF) \


DECLARE_NET_MESSAGE(MissionHeader, MISSION_HEADER_MSG)

#define VOTE_MISSION_PROGRESS_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<LocalizedString>, missions, NDTLocalizedStringArray, AutoArray<LocalizedString>, NCTNone, DEFVALUELOCALIZEDSTRINGARRAY, DOC_MSG("List of most popular missions"), TRANSF) \
  XX(MessageName, AutoArray<int>, missionVotes, NDTIntArray, AutoArray<int>, NCTSmallUnsigned, DEFVALUEINTARRAY, DOC_MSG("Numbers of votes for missions"), TRANSF) \
  XX(MessageName, RString, diff, NDTString, RString, NCTNone, DEFVALUE_DEF(RString), DOC_MSG("Most popular difficulty"), TRANSF) \

DECLARE_NET_MESSAGE(VotingMissionProgress,VOTE_MISSION_PROGRESS_MSG)


struct MissionHeader : public MissionHeaderMessage
{
  //! Starting time of mission
  int _start;
  
  /// client side localized mission name
  RString GetLocalizedMissionName() const;
  /// client side localized mission description
  RString GetLocalizedMissionDesc() const;
};

#ifndef DECL_ENUM_TARGET_SIDE
#define DECL_ENUM_TARGET_SIDE
DECL_ENUM(TargetSide)
#endif

//((EXE_CRC_CHECK
//! State of delayed kick-off
enum KickOffState
{
  /// waiting for kick off because of modified exe
  KOExe,
  /// waiting for kicking
  KOWait,
  /// waiting for kicking
  KOWarning,
  KODone
};
//))EXE_CRC_CHECK

//! what king of priviledges has given player
enum PlayerRights
{
  PRNone=0,
  PRSideCommander=1,
  PRVotedAdmin=2,
  PRAdmin=4,
  PRServer=8
};

struct LocalPlayerInfo
{
  LocalPlayerInfo(): 
  signedToLive(false) {}
  
  /// XUID of player signed to this position (INVALID_XUID if no player is signed in)
  XUID xuid;
  /// if the player signed in to this position signed to Live
  bool signedToLive;

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat
    (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
    );
  TMError TransferMsg(NetworkMessageContext &ctx);
};

TypeIsMovable(LocalPlayerInfo);


/// list of all information about local players
struct LocalPlayerInfoList: public NetworkSimpleObject
{
  /// initializes message from information from SaveSystem
  void Init();

  /// list of local player information
  AutoArray<LocalPlayerInfo> localPlayerInfoList;

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat
    (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
    );
  TMError TransferMsg(NetworkMessageContext &ctx);
};

bool IsUserIdMatch(const char *id, const char *match);


//! Player description
struct PlayerIdentity : public NetworkSimpleObject
{
  //! DirectPlay ID of player
  int dpnid;
  //! ID unique in session (shorter than dpnid)
  int playerid;
  //! Xbox Live user ID, or Player ID on PC, or dpnid on PC demo (where there is no player id)
  XUID xuid;

  LocalPlayerInfoList localPlayerInfoList;

#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
  /// Steam identity and authentication ticket, verified by the dedicated server.
  NetInt64 _steamID;
  AutoArray<char> _steamSessionTicket;
#endif

  //! address for peer-to-peer voice transition
  sockaddr_in _voiceAddress;

  //! unique id of player (derived from CD key, may include verified userId after a double colon)
  RString id;
  //! user certificate
  AutoArray<char> certificate;
  //! challenge signature signed by a private key corresponding to the certificate
  AutoArray<char> challengeSigned;

  /// list of blocked player IDs
  FindArray<RString> blockedKeys;
  /// signed by a registration server
  AutoArray<char> blockedKeysSignature;

  //! Gamespy challenge send by server, which the player needs to sign in the NMTPlayer message
  NetInt64 _challenge;
  //! nick (short) name of player
  RString name;
  //! selected face
  RString face;
  //! selected glasses
  RString glasses;
  //! selected speaker class
  RString speakerType;
  //! selected voice pitch
  float pitch;
  //! full name of player
  RString fullname;
  //! e-mail of player
  RString email;
  //! ICQ of player
  RString icq;
  //! remark about player
  RString remark;
  /// number of files checked by the client (number of banks + additional checked files)
  int filesCount;
  UITime kickOffTime; //!< id is "0", kickoff will follow
  KickOffState kickOffState; //!< chanded during kickoff process
  //! user was kicked
  bool destroy;
  //! timeout for player destroy after kick
  UITime destroyTime;

  //! state of player's network client
  NetworkClientState clientState;

  //! version player is using
  /** if zero, it may be anything up to 1.42, it was introduced in OFP 1.43 */
  int version;
  /// build player is using. Introduced after 85590
  /** 0 = developer version, -1 = before 85590 */
  int build;
  /*!
  \name Client-Server network conditions
  This information is updated more often (by UpdatePosition messages)
  */
  //@{
  //! ping range estimation
  int _minPing,_avgPing,_maxPing;
  //! bandwidth estimation (in kbps)
  int _minBandwidth, _avgBandwidth, _maxBandwidth;
  //! current desync level (max. error of unsent messages)
  int _desync;
  /// some typical values
  enum {DesyncLow=2000,DesyncHigh=20000};
  //! special rights of given player
  int _rights;
  //@}

  //! returns name displayed in game - nick + squad nick
  RString GetName() const;

  bool ValidateID() const;
  /// id representation used fro debugging
  RString GetDebugId() const;

  PlayerIdentity();
  ~PlayerIdentity();

  DECLARE_NETWORK_OBJECT
  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  TMError TransferMsg(NetworkMessageContext &ctx);
};
TypeIsMovable(PlayerIdentity)

//! return name of player on local client
RString GetLocalPlayerName();

/// session info passed in QoS probe
struct QosInfo
{
  //! Game type
  BString<8> gameType;
  /// Mission name localization type
  char missionLocalizedType; 
  //! Mission name localization id
  BString<39> missionId;
  //! Island
  BString<16> island;
  //! State of game on host
  int serverState;
  //! Current number of players (in public slots)
  int playersPublic;
  //! Maximal number of players (public slots)
  int slotsPublic;
  //! Current number of players (in private slots)
  int playersPrivate;
  //! Maximal number of players (private slots)
  int slotsPrivate;
  //! Estimated time to mission end (in minutes)
  int timeleft;
  //! Difficulty set on the server
  int difficulty;
  
  /// decode missionLocalizedType and missionId
  RString GetLocalizedMissionName() const;
  /// encode missionLocalizedType and missionId
  void SetMissionName(const LocalizedString &str);
};

#define MSG_FORMAT_ITEM_MSG(MessageName, XX) \
  XX(MessageName, RString, name, NDTString, RString, NCTDefault, ignored, DOC_MSG("Item name"), TRANSF) \
  XX(MessageName, NetworkFormatSpecs, specs, NDTData, NetworkFormatSpecs, NCTNone, ignored, DOC_MSG("Default value of item"), TRANSF)

DECLARE_NET_INDICES(MsgFormatItem, MSG_FORMAT_ITEM_MSG)

#define MSG_FORMAT_MSG(MessageName, XX) \
  XX(MessageName, int, index, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, -1), DOC_MSG("Index of message type"), TRANSF) \
  XX(MessageName, AutoArray<NetworkMessageFormatItem>, items, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageMsgFormatItem), NCTNone, DEFVALUE_MSG(NMTMsgFormatItem), DOC_MSG("List of items"), TRANSF_ARRAY) \

DECLARE_NET_INDICES(MsgFormat, MSG_FORMAT_MSG)

#define INVENTORY_MENU_ACTION_MSG(MessageName, XX) \
  XX(MessageName, int, actionType, NDTInteger, int, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Action type"), TRANSF) \
  XX(MessageName, OLink(Object), actionTarget, NDTRef, NetworkId,  NCTNone, DEFVALUENULL, DOC_MSG("Action target"), TRANSF_REF) \
  XX(MessageName, AutoArray<char>, actionParams, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Action parameters based on type"), TRANSF) \

DECLARE_NET_MESSAGE(InventoryMenuAction, INVENTORY_MENU_ACTION_MSG)

TypeContainsOLink(InventoryMenuActionMessage);

#define INVENTORY_MENU_ATTACHMENT_MSG(MessageName, XX) \
  XX(MessageName, bool, attachment, NDTBool, bool, NCTNone, DEFVALUE(bool, true), DOC_MSG("True for Attachment, false for owned item."), TRANSF) \
  XX(MessageName, OLinkPermO(InventoryItem), item1, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Involved item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item2, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Involved item"), TRANSF_REF) \

DECLARE_NET_MESSAGE(InventoryMenuAttachment, INVENTORY_MENU_ATTACHMENT_MSG)

TypeContainsOLink(InventoryMenuAttachmentMessage);

#define INVENTORY_MENU_MSG(MessageName, XX) \
  XX(MessageName, OLinkPermO(Person), unit, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Asked person"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item1, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Involved item"), TRANSF_REF) \
  XX(MessageName, OLinkPermO(InventoryItem), item2, NDTRef, NetworkId, NCTNone, DEFVALUENULL, DOC_MSG("Involved item"), TRANSF_REF) \
  XX(MessageName, AutoArray<InventoryMenuActionMessage>, actions, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageInventoryMenuAction), NCTNone, DEFVALUE_MSG(NMTInventoryMenuAction), DOC_MSG("List of actions"), TRANSF_ARRAY) \
  XX(MessageName, AutoArray<InventoryMenuAttachmentMessage>, attachments, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageInventoryMenuAttachment), NCTNone, DEFVALUE_MSG(NMTInventoryMenuAttachment), DOC_MSG("List of attachments"), TRANSF_ARRAY) \
  XX(MessageName, AutoArray<RString>, recipes, NDTStringArray, AutoArray<RString>, NCTNone, DEFVALUESTRINGARRAY, DOC_MSG("List of recipes."), TRANSF) \
  XX(MessageName, bool, swap, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("True if items can be swapped."), TRANSF) \

DECLARE_NET_MESSAGE(InventoryMenu, INVENTORY_MENU_MSG)

#if NO_UNDEF_ENUM_REFERENCE && NO_ENUM_FORWARD_DECLARATION 
template <typename NetworkMessageErrorType::Helper::EnumType ErrType, class Type>
struct CalculateError
{
  static bool IsDiff(MessageDiffApply &diff){DoAssert(false);return true;}
};
#else
template <NetworkMessageErrorType ErrType, class Type>
struct CalculateError
{
  static bool IsDiff(){DoAssert(false);return true;}
};
#endif

template <class Type>
struct DiffType
{
  static Type Empty() {return Type();}
};

template <>
struct DiffType<Vector3>
{
  static Vector3 Empty() {return VZero;}
};

template <>
struct DiffType<Matrix3>
{
  static Matrix3 Empty() {return M3Zero;}
};

template <>
struct DiffType<Matrix4>
{
  static Matrix4 Empty() {return MZero;}
};

template <class Type>
struct CreateDiffYes
{
  static void CreateDiff(Type &value1, const Type &value2, MessageDiffCreate &diff);
  static void ApplyDiff(Type &value1, const Type &value2, MessageDiffApply &diff);
  static bool IsDiff();
};


template <class Type>
void CreateDiffYes<Type>::CreateDiff(Type &value1, const Type &value2, MessageDiffCreate &diff)
{
  if (value1!=value2)
  {
    diff.Append(true);
  }
  else
  {
    diff.Append(false);
    value1 = DiffType<Type>::Empty();
  }
}

template <class Type>
void CreateDiffYes<Type>::ApplyDiff(Type &value1, const Type &value2, MessageDiffApply &diff)
{
  if (!diff.Get())
  {
    //DoAssert(value1==DiffType<Type>::Empty());
    value1 = value2; // value was skipped, copy the old value
  }
}

template <class Type>
bool CreateDiffYes<Type>::IsDiff()
{
  return true;
}

template <class Type>
struct CreateDiffNo
{
  static void CreateDiff(Type &value1, const Type &value2, MessageDiffCreate &diff);
  static void ApplyDiff(Type &value1, const Type &value2, MessageDiffApply &diff);
  static bool IsDiff();

};

template <class Type>
struct CreateDiffDefault: public CreateDiffYes<Type>
{
};

template <class Type>
void CreateDiffNo<Type>::CreateDiff(Type &value1, const Type &value2, MessageDiffCreate &diff)
{
  // TODO: consider not appending the bit here at all once CreateDiffNo is a bit stabilized, CreateDiffNo on the sending and receiving end should always match
  diff.Append(true);
}
template <class Type>
void CreateDiffNo<Type>::ApplyDiff(Type &value1, const Type &value2, MessageDiffApply &diff)
{
  if (!diff.Get()) 
  { 
    DoAssert(false); // this should never happen
    value1 = value2; // value was skipped, copy the old value
  }
}
template <class Type>
bool CreateDiffNo<Type>::IsDiff()
{
  return true;
}

template <class Type>
struct CalculateError<ET_HEADER, Type>: public CreateDiffNo<Type>
{
  static __forceinline float ErrorStatic(const Type &value1, const Type &value2)
  {
    return 0;
  }
  static __forceinline float ErrorDynamic(const Type &value1, const Type &value2, float dt)
  {
    return 0;
  }
};


template <class Type>
struct CalculateError<ET_NONE, Type>: public CreateDiffDefault<Type>
{
  static __forceinline float ErrorStatic(const Type &value1, const Type &value2)
  {
    return 0;
  }
  static __forceinline float ErrorDynamic(const Type &value1, const Type &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, bool>: public CreateDiffDefault<bool>
{
  static __forceinline float ErrorStatic(const bool &value1, const bool &value2)
  {
    return value1 != value2;
  }
  static __forceinline float ErrorDynamic(const bool &value1, const bool &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, int>: public CreateDiffYes<int>
{
  static __forceinline float ErrorStatic(const int &value1, const int &value2)
  {
    return value1 != value2;
  }
  static __forceinline float ErrorDynamic(const int &value1, const int &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, float>: public CreateDiffDefault<float>
{
  static __forceinline float ErrorStatic(const float &value1, const float &value2)
  {
    if (!memcmp(&value1,&value2,sizeof(value1))) return 0;
    return value1 != value2;
  }
  static __forceinline float ErrorDynamic(const float &value1, const float &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, RString>: public CreateDiffDefault<RString>
{
  static __forceinline float ErrorStatic(const RString &value1, const RString &value2)
  {
    return stricmp(value1, value2) != 0;
  }
  static __forceinline float ErrorDynamic(const RString &value1, const RString &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, RStringB>: public CreateDiffDefault<RStringB>
{
  static __forceinline float ErrorStatic(const RStringB &value1, const RStringB &value2)
  {
    return (value1 != value2);
  }
  static __forceinline float ErrorDynamic(const RStringB &value1, const RStringB &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, Vector3>: public CreateDiffDefault<Vector3>
{
  static __forceinline float ErrorStatic(const Vector3 &value1, const Vector3 &value2)
  {
    return value1 != value2;
  }
  static __forceinline float ErrorDynamic(const Vector3 &value1, const Vector3 &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, Time>: public CreateDiffDefault<Time>
{
  static __forceinline float ErrorStatic(const Time &value1, const Time &value2)
  {
    return value1 != value2;
  }
  static __forceinline float ErrorDynamic(const Time &value1, const Time &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, NetworkId>: public CreateDiffDefault<NetworkId>
{
  static __forceinline float ErrorStatic(const NetworkId &value1, const NetworkId &value2)
  {
    return value1 != value2;
  }
  static __forceinline float ErrorDynamic(const NetworkId &value1, const NetworkId &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, AutoArray<bool> >: public CreateDiffDefault<AutoArray<bool>>
{
  static float ErrorStatic(const AutoArray<bool> &value1, const AutoArray<bool> &value2)
  {
    if (value1.Size() != value2.Size()) return 1;
    for (int i=0; i<value1.Size(); i++)
    {
      if (value1[i] != value2[i]) return 1;
    }
    return 0;
  }
  static __forceinline float ErrorDynamic(const AutoArray<bool> &value1, const AutoArray<bool> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, AutoArray<int> >: public CreateDiffDefault<AutoArray<int>>
{
  static float ErrorStatic(const AutoArray<int> &value1, const AutoArray<int> &value2)
  {
    if (value1.Size() != value2.Size()) return 1;
    for (int i=0; i<value1.Size(); i++)
    {
      if (value1[i] != value2[i]) return 1;
    }
    return 0;
  }
  static __forceinline float ErrorDynamic(const AutoArray<int> &value1, const AutoArray<int> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, AutoArray<RString> >: public CreateDiffDefault<AutoArray<RString>>
{
  static float ErrorStatic(const AutoArray<RString> &value1, const AutoArray<RString> &value2)
  {
    if (value1.Size() != value2.Size()) return 1;
    for (int i=0; i<value1.Size(); i++)
    {
      if (stricmp(value1[i], value2[i]) != 0) return 1;
    }
    return 0;
  }
  static __forceinline float ErrorDynamic(const AutoArray<RString> &value1, const AutoArray<RString> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL, AutoArray<RStringB> >: public CreateDiffDefault<AutoArray<RStringB>>
{
  static float ErrorStatic(const AutoArray<RStringB> &value1, const AutoArray<RStringB> &value2)
  {
    if (value1.Size() != value2.Size()) return 1;
    for (int i=0; i<value1.Size(); i++)
    {
      if (value1[i] != value2[i]) return 1;
    }
    return 0;
  }
  static __forceinline float ErrorDynamic(const AutoArray<RStringB> &value1, const AutoArray<RStringB> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_ABS_DIF, int>: public CreateDiffDefault<int>
{
  static __forceinline float ErrorStatic(const int &value1, const int &value2)
  {
    return abs(value1 - value2);
  }
  static __forceinline float ErrorDynamic(const int &value1, const int &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_ABS_DIF, float>: public CreateDiffDefault<float>
{
  static __forceinline float ErrorStatic(const float &value1, const float &value2)
  {
    if (!memcmp(&value1,&value2,sizeof(value1))) return 0;
    return fabs(value1 - value2);
  }
  static __forceinline float ErrorDynamic(const float &value1, const float &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_ABS_DIF, Vector3>: public CreateDiffDefault<Vector3>
{
  static __forceinline float ErrorStatic(const Vector3 &value1, const Vector3 &value2)
  {
    return value1.Distance(value2);
  }
  static __forceinline float ErrorDynamic(const Vector3 &value1, const Vector3 &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_ABS_DIF, Matrix3>: public CreateDiffDefault<Matrix3>
{
  static __forceinline float ErrorStatic(const Matrix3 &value1, const Matrix3 &value2)
  {
    return value1.DirectionUp().Distance(value2.DirectionUp()) + value1.Direction().Distance(value2.Direction());
  }
  static __forceinline float ErrorDynamic(const Matrix3 &value1, const Matrix3 &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_ABS_DIF, AutoArray<float> >: public CreateDiffDefault<AutoArray<float>>
{
  static float ErrorStatic(const AutoArray<float> &value1, const AutoArray<float> &value2)
  {
    int minSize = intMin(value1.Size(), value2.Size());
    float error = value1.Size() - minSize + value2.Size() - minSize;
    for (int i=0; i<minSize; i++)
      error += fabs(value1[i] - value2[i]);
    return error;
  }
  static __forceinline float ErrorDynamic(const AutoArray<float> &value1, const AutoArray<float> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_ABS_DIF, Ref<NetworkMessage> >
{
  static void CreateDiff(Ref<NetworkMessage> &value1, const Ref<NetworkMessage> &value2, MessageDiffCreate &diff);
  static void ApplyDiff(Ref<NetworkMessage> &value1, const Ref<NetworkMessage> &value2, MessageDiffApply &diff);
  static bool IsDiff();



  
  static float ErrorStatic(const Ref<NetworkMessage> &value1, const Ref<NetworkMessage> &value2)
  {
    if (!value1 && !value2) return 0;
    if (value1 && value2) return value1->CalculateErrorStatic(value2);
    Fail("Reference only at one message");
    return 100;
  }
  static float ErrorDynamic(const Ref<NetworkMessage> &value1, const Ref<NetworkMessage> &value2, float dt)
  {
    if (!value1 && !value2) return 0;
    if (value1 && value2) return value1->CalculateErrorDynamic(value2, dt);
    Fail("Reference only at one message");
    return 100;
  }
};



template <>
struct CalculateError<ET_ABS_DIF, Variables>: public CreateDiffNo<Variables> // TODO: compute diff for variables
{
  static float ErrorStatic(const Variables &value1, const Variables &value2)
  {
    return value1.CalculateError(value2);
  }
  static float ErrorDynamic(const Variables &value1, const Variables &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_DER_DIF, Vector3>: public CreateDiffDefault<Vector3>
{
  static __forceinline float ErrorStatic(const Vector3 &value1, const Vector3 &value2)
  {
    return 0;
  }
  static __forceinline float ErrorDynamic(const Vector3 &value1, const Vector3 &value2, float dt)
  {
    return dt * value1.Distance(value2);
  }
};

template <>
struct CalculateError<ET_NOT_EQUAL_COUNT, AutoArray<NetworkId> >: public CreateDiffDefault<AutoArray<NetworkId>>
{
  static float ErrorStatic(const AutoArray<NetworkId> &value1, const AutoArray<NetworkId> &value2)
  {
    int count = intMin(value1.Size(), value2.Size());
    int dif = value1.Size() - count + value2.Size() - count;
    for (int i=0; i<count; i++)
    {
      if (value1[i] != value2[i]) dif++;
    }
    return dif;
  }
  static __forceinline float ErrorDynamic(const AutoArray<NetworkId> &value1, const AutoArray<NetworkId> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_NOT_CONTAIN_COUNT, AutoArray<NetworkId> >: public CreateDiffDefault<AutoArray<NetworkId>>
{
  static float ErrorStatic(const AutoArray<NetworkId> &value1, const AutoArray<NetworkId> &value2)
  {
    int dif = 0;
    for (int i=0; i<value2.Size(); i++)
    {
      NetworkId ref2 = value2[i];
      if (ref2.IsNull()) continue;
      bool found = false;
      for (int j=0; j<value1.Size(); j++)
        if (value1[j] == ref2)
        {
          found = true;
          break;
        }
        if (!found) dif++;
    }
    return dif;
  }
  static __forceinline float ErrorDynamic(const AutoArray<NetworkId> &value1, const AutoArray<NetworkId> &value2, float dt)
  {
    return 0;
  }
};

template <>
struct CalculateError<ET_MUZZLES, RefArray<NetworkMessage> >: public CreateDiffDefault<RefArray<NetworkMessage>>
{
  static float ErrorStatic(const RefArray<NetworkMessage> &value1, const RefArray<NetworkMessage> &value2)
  {
    float CalculateMuzzleError(const RefArray<NetworkMessage> &value1, const RefArray<NetworkMessage> &value2);
    return CalculateMuzzleError(value1, value2);
  }
  static __forceinline float ErrorDynamic(const RefArray<NetworkMessage> &value1, const RefArray<NetworkMessage> &value2, float dt)
  {
    return 0;
  }
};

#define ICALCERR_NEQ(type, name, value) \
  { \
    if (message->_##name != _##name) error += value; \
  }
#define ICALCERRE_NEQ(type, name, member, value) \
  { \
    if (message->_##name != member) error += value; \
  }
#define ICALCERR_NEQREF_PERM(type, name, value) \
  { \
    if (_##name) \
    { \
      if (message->_##name != _##name->GetNetworkId()) error += value; \
    } \
    else \
    { \
      if (!message->_##name.IsNull()) error += value; \
    } \
  }
#define ICALCERR_NEQREF_SOFT(type, name, value) \
  { \
    if (_##name) \
    { \
      if (message->_##name != _##name->GetNetworkId()) error += value; \
    } \
    else \
    { \
      if (!message->_##name.IsNull()) error += value; \
    } \
  }
#define ICALCERRE_NEQREF_PERM(type, name, member, value) \
  { \
    if (member) \
    { \
      if (message->_##name != member->GetNetworkId()) error += value; \
    } \
    else \
    { \
      if (!message->_##name.IsNull()) error += value; \
    } \
  }
#define ICALCERRE_NEQREF_SOFT(type, name, member, value) \
  { \
    if (member) \
    { \
      if (message->_##name != member->GetNetworkId()) error += value; \
    } \
    else \
    { \
      if (!message->_##name.IsNull()) error += value; \
    } \
  }
#define ICALCERR_NEQSTR(name, value) \
  { \
    if (stricmp(message->_##name, _##name) != 0) error += value; \
  }
#define ICALCERRE_NEQSTR(name, member, value) \
  { \
    if (stricmp(message->_##name, member) != 0) error += value; \
  }
#define ICALCERR_ABSDIF(type, name, value) \
  { \
    error += value * fabs(message->_##name - _##name); \
  }
#define ICALCERRE_ABSDIF(type, name, member, value) \
  { \
    error += value * fabs(message->_##name - member); \
  }

#define ICALCERR_DIST2(name, value) \
  { \
    error += value * message->_##name.Distance2(_##name); \
  }
#define ICALCERRE_DIST2(name, member, value) \
  { \
    error += value * message->_##name.Distance2(member); \
  }
#define ICALCERR_MATRIX(name, value) \
  { \
    error += value * \
    ( \
      message->_##name.DirectionUp().Distance(_##name.DirectionUp()) + \
      message->_##name.Direction().Distance(_##name.Direction() \
    ) \
  }
#define ICALCERRE_MATRIX(name, member, value) \
  { \
    error += value * \
    ( \
      message->_##name.DirectionUp().Distance(member.DirectionUp()) + \
      message->_##name.Direction().Distance(member.Direction() \
    ) \
  }

#define ICALCERR_DIST(name, value) \
  { \
    error += value * message->_##name.Distance(_##name); \
  }
#define ICALCERRE_DIST(name, member, value) \
  { \
    error += value * message->_##name.Distance(member); \
  }

#endif

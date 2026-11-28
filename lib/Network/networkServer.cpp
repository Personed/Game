#include "../wpch.hpp"
#include "networkImpl.hpp"
#include "../global.hpp"
//#include "strIncl.hpp"
#include "../stringtableExt.hpp"

#include "../arcadeTemplate.hpp"
#include "../AI/ai.hpp"
#include "../world.hpp"
#include "../landscape.hpp"
#include "../UI/chat.hpp"
#include "../UI/uiMap.hpp"
#include "../UI/missionDirs.hpp"
#include <El/XML/xml.hpp>

#include <Es/Algorithms/qsort.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/Files/fileContainer.hpp>
#include "Es/Containers/RTree.hpp"

#include "../allAIVehicles.hpp"
#include "../seaGull.hpp"
#include "../detector.hpp"
#include "../shots.hpp"
#include "../fileLocator.hpp"
#include "../gameDirs.hpp"
#include "../registration.hpp"

#include <El/Debugging/debugTrap.hpp>
#include <El/Common/perfProf.hpp>

#include <El/QStream/qbStream.hpp>
#include <El/QStream/packFiles.hpp>
#include <El/FileServer/fileServer.hpp>

#if _ENABLE_STEAM && defined(_WIN64)
bool SteamOnlineServerStart(int gamePort, int steamPort, int queryPort, const char *serverName, int maxPlayers, bool passwordProtected);
#endif

#include <El/Evaluator/express.hpp>

#include <El/Common/randomGen.hpp>
#include "../gameStateExt.hpp"

#include "../progress.hpp"
#include <El/Common/perfLog.hpp>

#include "geoLoc.hpp"
#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "../UI/uiActions.hpp"

#include <El/CRC/crc.hpp>
#include <Es/Strings/bString.hpp>
#include <string>
#include <vector>

#include "../saveGame.hpp"
#include "../saveVersion.hpp"

#include "../AI/aiRadio.hpp"
#include <lib/Inventory/recipe.hpp>

#include <El/ParamArchive/paramArchiveDb.hpp>
#include <El/Credits/credits.hpp>

#include "httpRequest.h"

#ifdef _WIN32
  #include <io.h>
#endif

#if defined(_WIN32) 
  #if _ENABLE_MP
    #include <winsock2.h>
  #endif
  #if _ENABLE_DEDICATED_SERVER
    #include <process.h>
  #endif
#endif

#include "El/Speech/debugVoN.hpp"

#if _PROFILE
#pragma optimize( "", off )
#endif

/*!
\file
Basic implementation file for network server
*/

//((EXE_CRC_CHECK
// random integrity check
#if _SUPER_RELEASE
  #define _ENABLE_RANDOM_INTEGRITY_CHECK 1
#else
  #define _ENABLE_RANDOM_INTEGRITY_CHECK 0
#endif
//))EXE_CRC_CHECK

#define LOG_SEND_PROCESS 0

#define LOG_PLAYERS 1

extern int MaxCustomFileSize;

#define QOS_SERVER_BANDWIDTH  4096

//! enable diagnostic logs of message errors
#define LOG_ERRORS        0
//! limit for diagnostic logs of message errors
static float LogErrorLimit = 1.0f;

extern GameValue GNetworkPlayerConnected;
extern GameValue GNetworkPlayerDisconnected;


RStringB GetExtensionWizardMission();

/// generate a 32-bit random number (Implementation of rand() in MSVC returns only 15 bits)
int rand32()
{
  return (rand() & 0x7fff) | (rand() & 0x7fff) << 15 | (rand() & 0x0003) << 30;

}

#define STRINGIZE_X(x) #x
#define STRINGIZE(x) STRINGIZE_X(x)

/*!
\patch 5257 Date 7/15/2008 by Bebul
- Fixed: Players connecting JIP with custom files do not cause server lagging.
\patch 5257 Date 7/15/2008 by Bebul
- Fixed: Players with custom files get no longer kicked when server does not run on default port.
*/
static RString GetServerPath(RString src)
{
  if (src.GetLength() == 0) return src;
  if (src[0] == '$')
  { // translate char '$' to Local Settings directory
    char dir[MAX_PATH];
    if (!GetLocalSettingsDir(dir)) return src.Substring(2, INT_MAX); // place in current directory
    TerminateBy(dir, '\\');
    return RString(dir) + src.Substring(2, INT_MAX);
  }
  else if (src[0] == '#') 
  { // translate char '#' to Local Settings directory + ServerTmpDir
    return GetServerTmpDir() + src.Substring(1, INT_MAX); // place in current directory
  }
  else return src;
}

/*!
  \patch 5088 Date 11/06/2006 by Ondra
  - Fixed: Possible crash when exiting multiplayer game.
*/

void ClearNetServer()
{
}

// Moved, declarations needed in BE

//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY

#define REPORT_ASE  0

#include "../GameSpy/qr2/qr2.h"
#include "../GameSpy/natneg/natneg.h"
#if _VERIFY_CLIENT_KEY
# include "../GameSpy/gcdkey/gcdkeys.h"
# ifdef NOMINMAX
#  undef min
#  undef max
# endif
#include "../GameSpy/gcdkey/gcdkeyc.h"
#endif

# ifdef NOMINMAX
#  undef min
#  undef max
# endif

REGISTER_CREDITS("GameSpy", "2009 GameSpy Industries, Inc.","")

#if REPORT_ASE
extern "C"
{
#include "../ASE/ASEMasterSDK.h"
}
#endif

#ifdef NOMINMAX
# undef min
# undef max
#endif

char *GetGameName()
{
  // GameSpy game name
#if _LIBERATION
  return "IFpc";
#elif _DEMO
  return "takoncopterpcd";
#else
  return "takoncopterpc";
#endif
}

char *GetSecretKey()
{
  // GameSpy secret key
  static char secret_key[9];

  //set the secret key, in a semi-obfuscated manner
  secret_key[0] = '6';
  secret_key[1] = 'P';
  secret_key[2] = 'V';
  secret_key[3] = 'I';
  secret_key[4] = 'h';
  secret_key[5] = 'e';
  secret_key[6] = '\0';

  return secret_key;
}

int GetGameID()
{
  // GameSpy game id
#if _LIBERATION
  return 4077;
#elif _DEMO
  return 5132;
#else
  return 4093;
#endif
}

int GetProductID()
{
  // GameSpy product id
#if _LIBERATION
  return 13905;
#elif _DEMO
  return 13916;
#else
  return 13916;
#endif
}

char *GetGcdHostname()
{
  //host DNS - default
  return "";
}

#endif
//}

DEFINE_FAST_ALLOCATOR(NetworkPlayerObjectInfo)

NetworkObjectInfo::NetworkObjectInfo()
{}


NetworkObjectInfo::~NetworkObjectInfo()
{

}

#if _PROFILE && USER_Ondra
typedef Journal ProfileJournal;
#else
typedef DummyJournal ProfileJournal;
#endif

ProfileJournal NetworkPlayerObjectInfoJournal;

NetworkPlayerObjectInfo::NetworkPlayerObjectInfo()
:updating(false)
{
  NetworkPlayerObjectInfoJournal.Add(Format("Construct %x",this));
}

NetworkPlayerObjectInfo::~NetworkPlayerObjectInfo()
{
  NetworkPlayerObjectInfoJournal.Add(Format("Destruct  %x, %d",this,IsInList()));
}

NetworkPlayerObjectInfo *NetworkObjectInfo::GetPlayerObjectInfo(PlayerIndex player, int pid)
{
  DoAssert(playerToObjects[player]->player==pid);
  return playerToObjects[player];
}

NetworkPlayerObjectInfo *NetworkObjectInfo::CreatePlayerObjectInfo(PlayerIndex player, int pid)
{
  if (player < playerToObjects.Size())
  {
    return playerToObjects[player];
  }
  int index = playerToObjects.Add();
  DoAssert(index==player);
  playerToObjects[index] = new NetworkPlayerObjectInfo;
  NetworkPlayerObjectInfo *poInfo = playerToObjects[index];
  poInfo->player = pid;
  poInfo->oInfo = this;
  for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
  {
    NetworkUpdateInfo &info = poInfo->updates[j];
    info.errorValid = false;
  }
  return poInfo;
}

void NetworkObjectInfo::ResetPlayerObjectInfo(PlayerIndex player, int pid)
{
  if (player<0) return;
  DoAssert(playerToObjects[player]->player==pid);
  NetworkPlayerObjectInfo *poInfo = playerToObjects[player];
  for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
  {
    NetworkUpdateInfo &info = poInfo->updates[j];
    info.errorValid = false;
  }
}

void NetworkObjectInfo::DeletePlayerObjectInfo(PlayerIndex player, int pid)
{
  if (player<0) return;
  DoAssert(playerToObjects[player]->player==pid);
  playerToObjects.Delete(player);
}

void NetworkObjectInfo::NullPlayerObjectInfo(PlayerIndex player, int pid)
{
  if (player<0) return;
  DoAssert(playerToObjects[player]->player==pid);
  playerToObjects[player].Free();
}

RString NetworkObjectInfo::GetDebugName() const
{
  if (object.IsNull()) return Format("id=%d:%" OID_FORMAT,id.creator,id.id);
  return object->GetDebugName();
}

NetworkObjectInfo::UpdateInfo NetworkObjectInfo::CheckBestPosition(const NetworkServer *server) const
{
  Vector3 bestPos = VZero;
  NetworkMessageClass bestJ = NMCUpdateN;
  if (object.NotNull())
  {

    // find the last update of the object
    Time time = TIME_MIN;
    for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
    {
      const NetworkCurrentInfo &info = currentUpdate[j];
      const NetworkMessage *msg = info.message;
      if (msg)
      {
        if (msg->time > time)
        {
          time = msg->time; 
          bestJ = NetworkMessageClass(j);
        }
      }
    }
    if (bestJ!=NMCUpdateN)
    {
      const auto &best = currentUpdate[bestJ];

      // retrieve position of the object
      NetworkMessageContext ctx(best.message, unconst_cast(server), best.from, MSG_RECEIVE);

      PREPARE_TRANSFER(NetworkObject)
      if (TRANSF_BASE(objectPosition, bestPos) != TMOK)
      {
        bestPos = VZero;
        bestJ = NMCUpdateN;
      }
    
    }
  }
  DoAssert(bestJ==NMCUpdateN || bestPos!=VZero);
  NetworkObjectInfo::UpdateInfo ret;
  if (bestJ!=NMCUpdateN)
  {
    ret._pos = bestPos;
    ret._updateDistance = object ? object->UpdateDistance() : FLT_MAX;
    DoAssert(object);
  }

  return ret;
}


void NetworkObjectInfo::ScanBestPosition(const NetworkServer *server)
{
  _updInfo = CheckBestPosition(server);
}

NetworkObjectInfo::UpdateInfo NetworkObjectInfo::BestPosition(const NetworkServer *server) const
{
  #if 0 // _ENABLE_REPORT
    UpdateInfo updInfo = CheckBestPosition(server);
    DoAssert(updInfo._pos==_updInfo._pos);
    DoAssert(updInfo._root==_updInfo._root);
    DoAssert(updInfo._updateDistance==_updInfo._updateDistance);
  #endif
  return _updInfo;
}

bool NetworkObjectInfo::CheckBestPositionIntegrity(const NetworkServer *server) const
{
  UpdateInfo updInfo = CheckBestPosition(server);
  // TODO: enforce strict equality
  if (updInfo._pos.Distance2(_updInfo._pos)>Square(0.01f))
  {
    LogF("%s: pos not matching",cc_cast(GetDebugName()));
    return false;
  }
  if (updInfo._updateDistance!=_updInfo._updateDistance)
  {
    LogF("%s: updateDistance not matching",cc_cast(GetDebugName()));
    return false;
  }
  return true;
}


NetworkObjectInfo::UpdateInfo NetworkObjectInfo::BestPosition() const
{
  return _updInfo;
}


#if _ENABLE_DEDICATED_SERVER
int ConsoleF(const char *format, ...);
int VConsoleF(const char *format, va_list argptr);
int ConsoleTitle(const char *format, ...);
#endif
RString GetUserDirectory();

#if _USE_BATTL_EYE_SERVER
/*!
\patch 5254 Date 5/2/2008 by Bebul
- Fixed: BattlEye issues while using server.cfg on Linux dedicated server
\patch 5276 Date 4/16/2009 by Bebul
- Fixed: Changed BattlEye integration (fixing "random" player kicks)
*/

#include <sys/stat.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

NetworkServer::BattlEye::BattlEye()
{
  CleanUp();
}

NetworkServer::BattlEye::~BattlEye()
{
  Done();
}

bool GetBELaunchPath(char *launchPath)
{
#ifndef _XBOX
  RString GetBEPath();
  RString forcedPath = GetBEPath();
#else
  RString forcedPath;
#endif

  // decide the path to the "cache" from where dll is used
  if (IsRelativePath(forcedPath))
  {
    // relative path
#ifdef _WIN32
    if (!GetLocalSettingsDir(launchPath)) return false;
    TerminateBy(launchPath, '\\');
    strcat(launchPath, "BattlEye\\");
    strcat(launchPath, forcedPath);
    TerminateBy(launchPath, '\\');
#else
    strcpy(launchPath, "./battleye/");
    strcat(launchPath, forcedPath);
    CreateDirectory(launchPath, NULL);
    TerminateBy(launchPath, '/');
#endif
  }
  else
  {
    // absolute path
    strcpy(launchPath, forcedPath);
#ifdef _WIN32
    TerminateBy(launchPath, '\\');
#else
    TerminateBy(launchPath, '/');
#endif
  }

  return true;
}

#define STRINGIZE_X(x) #x
#define STRINGIZE(x) STRINGIZE_X(x)

#ifdef _WIN32

int NetworkServer::BattlEye::GetBEVersion(HMODULE library) const
{
  if (library)
  {
    GetVerF *getVer = (GetVerF *)GetProcAddress(library, "GetVer");
    if (getVer) return (*getVer)();
  }
  return 0;
}

// Win32 implementation
bool NetworkServer::BattlEye::Init()
{
  _initFailedReason = BEReasonOther;
  _initialized = false;
  const char *installPath = "BattlEye\\BEServer.dll";
  char launchPath[MAX_PATH];
  if (!GetBELaunchPath(launchPath)) return false;

  int curBEVersion = 0;
  if (QIFileFunctions::FileExists(installPath))
  {
    GFileServerFunctions->FlushReadHandle(installPath);
    // Get the BEClient.dll version from installPath first
    HMODULE installLibrary = LoadLibrary(installPath);
    int instVersion = GetBEVersion(installLibrary);
    if (installLibrary)
      FreeLibrary(installLibrary);

    CreatePath(launchPath);
    strcat(launchPath, "BEServer.dll");
    // try to load the dll from the cache
    _library = LoadLibrary(launchPath);
    if (_library)
    { // check whether the BE is updated to the last version
      curBEVersion = GetBEVersion(_library);
      if (instVersion>curBEVersion)
      {
        FreeLibrary(_library);
        _library = NULL;  // so it will be repaired from the install path
      }
    }
    if (!_library)
    {
      // install / repair the live copy from the install path
      chmod(launchPath, S_IREAD | S_IWRITE);
      unlink(launchPath);
      QIFileFunctions::Copy(installPath, launchPath);
      _library = LoadLibrary(launchPath);
      if (!_library) return false;
#if _ENABLE_DEDICATED_SERVER
      ::ConsoleF(Format("BattlEye server updated to version: %d", instVersion));
#endif
      curBEVersion = instVersion;
    }
  }
  else
  {
    // BattlEye removed, remove also from the cache
    strcat(launchPath, "BEServer.dll");
    chmod(launchPath, S_IREAD | S_IWRITE);
    unlink(launchPath);
    _initFailedReason = BEReasonNotInstalled;
    return false;
  }

  InitF *init = (InitF *)GetProcAddress(_library, "Init");
  
  _gameParams.pstrGameVersion = APP_NAME " " APP_VERSION_TEXT "." STRINGIZE(BUILD_NO);
  _gameParams._execCommand = &ExecuteCommand;
  _gameParams._kickPlayer = &KickPlayer;
  _gameParams._printMessage = &PrintMessage; 
  if ((curBEVersion<MinBEVerReq) || !init || !(*init)(CurrentBEIntegrationVersion, &_gameParams, &_callbacks))
  {
    FreeLibrary(_library);
    CleanUp();
    if (curBEVersion<MinBEVerReq) RptF("Obsolete/unsupported BattlEye version, please reinstall BE!");
    return false;
  }

  _initialized = true;
  return true;
}

#else 
// Linux implementation using dynamic linking loader (dlopen)

int NetworkServer::BattlEye::GetBEVersion(HMODULE library) const
{
  if (library)
  {
    GetVerF *getVer = (GetVerF *)dlsym(library, "GetVer");
    if (getVer) return (*getVer)();
  }
  return 0;
}

bool NetworkServer::BattlEye::Init()
{
  _initFailedReason = BEReasonOther;
  _initialized = false;
  const char *installPath = "battleye/beserver.so"; //use lower case - as ./install converts everything!
  char launchPath[MAX_PATH];
  if (!GetBELaunchPath(launchPath)) return false;

  if (QIFileFunctions::FileExists(installPath))
  {
    GFileServerFunctions->FlushReadHandle(installPath);
    CreatePath(launchPath);
    strcat(launchPath, "beserver.so");
    // use the install copy instead
    _library = dlopen(launchPath, RTLD_LAZY);
    if (!_library)
    {
      // install / repair the live copy from the install path
      chmod(launchPath, S_IREAD | S_IWRITE);
      unlink(launchPath);
      QIFileFunctions::Copy(installPath, launchPath);
      _library = dlopen(launchPath, RTLD_LAZY);
      if (!_library) return false;
    }
  }
  else
  {
    // BattlEye removed, remove also from the cache
    strcat(launchPath, "beserver.so");
    chmod(launchPath, S_IREAD | S_IWRITE);
    unlink(launchPath);
    _initFailedReason = BEReasonNotInstalled;
    return false;
  }
  int curBEVersion = GetBEVersion(_library);

  InitF *init = (InitF *)dlsym(_library, "Init");
  _gameParams.pstrGameVersion = APP_NAME " " APP_VERSION_TEXT "." STRINGIZE(BUILD_NO);
  _gameParams._execCommand = &ExecuteCommand;
  _gameParams._kickPlayer = &KickPlayer;
  _gameParams._printMessage = &PrintMessage; 
  if ((curBEVersion<MinBEVerReq) || !init || !(*init)(CurrentBEIntegrationVersion, &_gameParams, &_callbacks))
  {
    dlclose(_library);
    _library = NULL;
    CleanUp();
    if (curBEVersion<MinBEVerReq) RptF("Obsolete/unsupported BattlEye version, please reinstall BE!");
    return false;
  }
  _initialized = true;
  return true;
}
#endif 

void NetworkServer::BattlEye::Done()
{
  if (_initialized && _callbacks._exit)
  {
    (*_callbacks._exit)();
    _initialized = false;
  }
  if (_library) 
  {
#ifdef _WIN32
    FreeLibrary(_library);
#else
    dlclose(_library);
#endif
    _library = NULL;
  }
}

bool NetworkServer::BattlEye::Run(const NetworkServer *server)
{
  if (!_callbacks._run) return true;
  if (!(*_callbacks._run)())
  {
    // auto-update
    if (_initialized && _callbacks._exit) (*_callbacks._exit)(); //call the Exit function before whenever you unload the dll, but only when the previous Init() call succeeded
#ifdef _WIN32
    FreeLibrary(_library);
    CleanUp();

    char path[MAX_PATH];
    if (!GetBELaunchPath(path)) return false;
    strcat(path, "BEServer.dll");

    chmod(path, S_IREAD | S_IWRITE);
    unlink(path);
    rename(RString(path) + RString(".new"), path);
#else
    dlclose(_library);
    CleanUp();

    char path[MAX_PATH];
    if (!GetBELaunchPath(path)) return false;
    strcat(path, "beserver.so");

    chmod(path, S_IREAD | S_IWRITE);
    unlink(path);
    rename(RString(path) + RString(".new"), path);
#endif

    if (!Init())
    {
#if _ENABLE_DEDICATED_SERVER
      ::ConsoleF(LocalizeString(IDS_BATTLEYE_INIT_FAILED));
#endif
      return false;
    }

    // add all players
#if _VERIFY_CLIENT_KEY || (_ENABLE_STEAM_IDENTITY && defined(_WIN64))
    const AutoArray<PlayerIdentity> *identities = server->GetIdentities();
    if (identities)
    {
      for (int i=0; i<identities->Size(); i++)
      {
        const PlayerIdentity &identity = identities->Get(i);
          sockaddr_in address;
          server->_server->GetClientAddress(identity.dpnid, address);
        AddPlayer(identity.dpnid, address.sin_addr.s_addr, address.sin_port, identity.name);
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
        if (identity._steamID != 0)
          PlayerGUIDReceived(identity.dpnid, CSteamID((uint64)identity._steamID));
        else
          PlayerGUIDReceived(identity.dpnid);
#else
        PlayerGUIDReceived(identity.dpnid);
#endif
        if (identity.clientState == NCSPlaying)
          PlayerGoesInGame(identity.dpnid);
        //else PlayerGoesInLobby(identity.dpnid); //<- assumed as default after AddPlayer
      }
    }
#endif
  }
  return true;
}

void NetworkServer::BattlEye::Command(const char *command)
{
  if (_callbacks._command) (*_callbacks._command)(command);
}

void NetworkServer::BattlEye::AddPlayer(int player, unsigned long address, unsigned short port, const char *name)
{
  if (_callbacks._addPlayer) (*_callbacks._addPlayer)(player, address, port, name);
}

void NetworkServer::BattlEye::PlayerGoesInGame(int player)
{
  if (_callbacks._changePlayerStatus) (*_callbacks._changePlayerStatus)(player, 1);
}

void NetworkServer::BattlEye::PlayerGoesInLobby(int player)
{
  if (_callbacks._changePlayerStatus) (*_callbacks._changePlayerStatus)(player, 0);
}

void NetworkServer::BattlEye::PlayerDisconnected(int player)
{
  if (_callbacks._changePlayerStatus) (*_callbacks._changePlayerStatus)(player, -1);
}

void NetworkServer::BattlEye::PlayerGUIDReceived(int player)
{
  if (_callbacks._receivedPlayerGUID) 
  {
#if _ENABLE_GAMESPY && _VERIFY_CLIENT_KEY
    const char *hash = gcd_getkeyhash(GetGameID(), player);
    (*_callbacks._receivedPlayerGUID)(player, (void *)hash, strlen(hash));
#endif
  }
}

#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
void NetworkServer::BattlEye::PlayerGUIDReceived(int player, const CSteamID &id)
{
  if (_callbacks._receivedPlayerGUID)
  {
    uint64 steamId = id.ConvertToUint64();
    (*_callbacks._receivedPlayerGUID)(player, &steamId, sizeof(steamId));
  }
}
#endif

void NetworkServer::BattlEye::PlayerGUIDIsValid(int player)
{
}

#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
void NetworkServer::BattlEye::PlayerGUIDIsValid(int player, const CSteamID &ownerId)
{
  if (_callbacks._playerGUIDIsValid)
  {
    uint64 steamId = ownerId.ConvertToUint64();
    (*_callbacks._playerGUIDIsValid)(player, &steamId, sizeof(steamId));
  }
}
#endif

bool NetworkServer::BattlEye::UpdatePositionCallback(int player, const char *netId, float x, float y, float z,
  float dirX, float dirY, float dirZ, float upX, float upY, float upZ, float asideX, float asideY, float asideZ)
{
 if (_callbacks._callbackUpdatePosition) 
  {
    return (*_callbacks._callbackUpdatePosition)(player,netId,x,y,z,dirX,dirY,dirZ, upX, upY, upZ, asideX, asideY, asideZ);
  }
  return false;
}

bool NetworkServer::BattlEye::UpdateDamageCallback(int player, const char *netId, float dam)
{
 if (_callbacks._callbackUpdateDamage) 
  {
    return (*_callbacks._callbackUpdateDamage)(player,netId,dam);
  }
  return false;
}

bool NetworkServer::BattlEye::PublicVariableCallback(int player, const char *name, const char *data)
{
 if (_callbacks._callbackPublicVariable) 
  {
    return (*_callbacks._callbackPublicVariable)(player,name,data);
  }
  return false;
}

bool NetworkServer::BattlEye::AttachObjectCallback(int player, const char *netId, const char *netIdAttachTo, int memoryIndex, float x, float y, float z)
{
 if (_callbacks._callbackAttachObject) 
  {
    return (*_callbacks._callbackAttachObject)(player,netId,netIdAttachTo,memoryIndex,x,y,z);
  }
  return false;
}

void NetworkServer::BattlEye::PlayerChatReceived(int player, int channel, const char *message)
{
  if (_callbacks._receivedPlayerMessage)
    (*_callbacks._receivedPlayerMessage)(player, channel, message);
}

void NetworkServer::BattlEye::LoadedMission(const char *missionName)
{
  if (_callbacks._loadedMission)
    (*_callbacks._loadedMission)(missionName);
}

void NetworkServer::BattlEye::PrintMessage(const char *message)
{
#if _ENABLE_DEDICATED_SERVER
  bool firstLine = true;
  while (const char *newline = strchr(message, '\n'))
  {
    RString line(message, newline - message);
    ::ConsoleF(firstLine ? "BattlEye Server: %s" : "  %s", cc_cast(line));
      firstLine = false;
    message = newline + 1;
    }
  ::ConsoleF(firstLine ? "BattlEye Server: %s" : "  %s", message);
#endif
}

void NetworkServer::BattlEye::KickPlayer(int player, const char *reason)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (server)
  {
#if _ENABLE_DEDICATED_SERVER
    PlayerIdentity *identity = server->FindIdentity(player);
    ::ConsoleF("Player %s kicked off by BattlEye: %s", identity ? cc_cast(identity->name) : "", reason);

/*
// using DisconnectMessage is not sufficient as we need the kick being immediate
// BattleEye "support" attitude: 
//   "Note that this is of course when the ArmA client does not disconnect itself, 
//    which it does when you click OK in the BE kick message box - but not before;
//    naturally the server itself has to do this to have control and not to allow
//    the player to stay on the server with some exploit"
  DisconnectMessage msg;
  msg._message = Format("Kicked off by BattlEye: %s", reason);
  server->SendMsg(player, &msg, NMFGuaranteed);
*/

  server->KickOff(player,KORBattlEye, reason);
#endif
  }
}

#if _ENABLE_DEDICATED_SERVER
// Large SQF files travel as several ordinary BattlEye commands. Reassemble and
// validate the complete script before putting it on the game-thread queue.
struct SDZBeSqfTransfer
{
  std::string id;
  unsigned int expectedParts;
  unsigned int expectedCrc;
  unsigned int receivedParts;
  unsigned int receivedBytes;
  DWORD startedAt;
  std::vector<std::string> parts;
  std::vector<unsigned char> received;

  SDZBeSqfTransfer() : expectedParts(0), expectedCrc(0), receivedParts(0), receivedBytes(0), startedAt(0) {}
  void Reset()
  {
    id.clear(); expectedParts = expectedCrc = receivedParts = receivedBytes = 0; startedAt = 0;
    parts.clear(); received.clear();
  }
};

static SDZBeSqfTransfer SDZBeTransfer;

static unsigned int SDZBeCrc32(const char *data, unsigned int length)
{
  unsigned int crc = 0xFFFFFFFF;
  for (unsigned int i = 0; i < length; i++)
  {
    crc ^= (unsigned char)data[i];
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
  }
  return ~crc;
}

static const char *SDZSkipSpaces(const char *text)
{
  while (*text == ' ' || *text == '\t') text++;
  return text;
}

static bool SDZReadToken(const char **cursor, std::string &token)
{
  const char *start = SDZSkipSpaces(*cursor);
  if (!*start) return false;
  const char *end = start;
  while (*end && *end != ' ' && *end != '\t') end++;
  token.assign(start, end - start);
  *cursor = end;
  return true;
}

static bool SDZValidTransferId(const std::string &id)
{
  if (id.length() != 32) return false;
  for (size_t i = 0; i < id.length(); i++)
    if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f') || (id[i] >= 'A' && id[i] <= 'F'))) return false;
  return true;
}

static bool SDZHandleRconSqfTransfer(NetworkServer *server, const char *command)
{
  static const char beginPrefix[] = "#exec __SDZ_BEGIN__ ";
  static const char partPrefix[] = "#exec __SDZ_PART__ ";
  static const char endPrefix[] = "#exec __SDZ_END__ ";
  if (!strnicmp(command, beginPrefix, sizeof(beginPrefix) - 1))
  {
    SDZBeTransfer.Reset();
    const char *cursor = command + sizeof(beginPrefix) - 1;
    std::string id, countText, crcText, extra;
    if (!SDZReadToken(&cursor, id) || !SDZReadToken(&cursor, countText) || !SDZReadToken(&cursor, crcText) ||
        SDZReadToken(&cursor, extra) || !SDZValidTransferId(id))
    {
      RptF("SDZ_BE_RCON: invalid large-SQF begin frame");
      return true;
    }
    char *parseEnd = NULL;
    unsigned long count = strtoul(countText.c_str(), &parseEnd, 10);
    if (!parseEnd || *parseEnd || count == 0 || count > 2048)
    {
      RptF("SDZ_BE_RCON: rejected large-SQF transfer with invalid part count");
      return true;
    }
    parseEnd = NULL;
    unsigned long crc = strtoul(crcText.c_str(), &parseEnd, 16);
    if (!parseEnd || *parseEnd || crc > 0xFFFFFFFFUL)
    {
      RptF("SDZ_BE_RCON: rejected large-SQF transfer with invalid checksum");
      return true;
    }
    SDZBeTransfer.id = id;
    SDZBeTransfer.expectedParts = (unsigned int)count;
    SDZBeTransfer.expectedCrc = (unsigned int)crc;
    SDZBeTransfer.startedAt = GetTickCount();
    SDZBeTransfer.parts.resize(count);
    SDZBeTransfer.received.resize(count, 0);
    RptF("SDZ_BE_RCON: large-SQF transfer started (%u parts)", (unsigned int)count);
    return true;
  }

  if (!strnicmp(command, partPrefix, sizeof(partPrefix) - 1))
  {
    if (SDZBeTransfer.expectedParts == 0 || GetTickCount() - SDZBeTransfer.startedAt > 120000)
    {
      SDZBeTransfer.Reset();
      RptF("SDZ_BE_RCON: rejected large-SQF part without an active transfer");
      return true;
    }
    const char *cursor = command + sizeof(partPrefix) - 1;
    std::string id, indexText;
    if (!SDZReadToken(&cursor, id) || !SDZReadToken(&cursor, indexText) || id != SDZBeTransfer.id)
    {
      RptF("SDZ_BE_RCON: rejected large-SQF part with invalid transfer id");
      return true;
    }
    char *parseEnd = NULL;
    unsigned long index = strtoul(indexText.c_str(), &parseEnd, 10);
    if (!parseEnd || *parseEnd || index >= SDZBeTransfer.expectedParts || *cursor != ' ')
    {
      RptF("SDZ_BE_RCON: rejected malformed large-SQF part");
      return true;
    }
    const char *chunk = cursor + 1;
    size_t chunkLength = strlen(chunk);
    if (chunkLength == 0 || chunkLength > 900)
    {
      RptF("SDZ_BE_RCON: rejected malformed large-SQF part");
      return true;
    }
    if (SDZBeTransfer.received[index])
    {
      const std::string &receivedChunk = SDZBeTransfer.parts[index];
      if (receivedChunk.size() != chunkLength || receivedChunk.compare(0, receivedChunk.size(), chunk, chunkLength) != 0)
        RptF("SDZ_BE_RCON: conflicting duplicate large-SQF part");
      return true;
    }
    if (SDZBeTransfer.receivedBytes + chunkLength > 1048576)
    {
      SDZBeTransfer.Reset();
      RptF("SDZ_BE_RCON: rejected SQF script larger than 1 MiB");
      return true;
    }
    SDZBeTransfer.parts[index].assign(chunk, chunkLength);
    SDZBeTransfer.received[index] = 1;
    SDZBeTransfer.receivedParts++;
    SDZBeTransfer.receivedBytes += (unsigned int)chunkLength;
    return true;
  }

  if (!strnicmp(command, endPrefix, sizeof(endPrefix) - 1))
  {
    const char *cursor = command + sizeof(endPrefix) - 1;
    std::string id, extra;
    if (!SDZReadToken(&cursor, id) || SDZReadToken(&cursor, extra) || id != SDZBeTransfer.id ||
        SDZBeTransfer.expectedParts == 0 || GetTickCount() - SDZBeTransfer.startedAt > 120000)
    {
      SDZBeTransfer.Reset();
      RptF("SDZ_BE_RCON: rejected invalid or expired large-SQF end frame");
      return true;
    }
    if (SDZBeTransfer.receivedParts != SDZBeTransfer.expectedParts)
    {
      RptF("SDZ_BE_RCON: incomplete large-SQF transfer (%u/%u parts)", SDZBeTransfer.receivedParts, SDZBeTransfer.expectedParts);
      SDZBeTransfer.Reset();
      return true;
    }
    std::string script;
    script.reserve(SDZBeTransfer.receivedBytes);
    for (unsigned int i = 0; i < SDZBeTransfer.expectedParts; i++) script += SDZBeTransfer.parts[i];
    unsigned int expectedCrc = SDZBeTransfer.expectedCrc;
    unsigned int receivedBytes = (unsigned int)script.length();
    SDZBeTransfer.Reset();
    if (SDZBeCrc32(script.c_str(), receivedBytes) != expectedCrc)
    {
      RptF("SDZ_BE_RCON: rejected large-SQF transfer with checksum mismatch");
      return true;
    }
    if (!server || script.empty())
    {
      RptF("SDZ_BE_RCON: rejected empty large-SQF script or unavailable server");
      return true;
    }
    server->QueueExec(BOT_CLIENT, RString(script.c_str()));
    RptF("SDZ_BE_RCON: queued reassembled SQF (%u bytes)", receivedBytes);
    return true;
  }
  return false;
}
#endif

void NetworkServer::BattlEye::ExecuteCommand(const char *command)
{
#if _ENABLE_DEDICATED_SERVER
  // BattlEye invokes this callback on the dedicated server. There is normally
  // no local NetworkClient there, so forwarding #exec through the client drops
  // the command. Queue it for the regular server simulation thread instead.
  if (!command || !*command) return;

  if (strlen(command) > 1000)
  {
    RptF("SDZ_BE_RCON: rejected BattlEye command longer than 1000 bytes");
    return;
  }

  NetworkServer *server = GNetworkManager.GetServer();
  if (SDZHandleRconSqfTransfer(server, command)) return;

  if (strnicmp(command, "#exec", 5) == 0 && (command[5] == 0 || command[5] == ' ' || command[5] == '\t'))
  {
    const char *expression = command + 5;
    while (*expression == ' ' || *expression == '\t') expression++;
    if (!*expression)
    {
      RptF("SDZ_BE_RCON: rejected empty #exec command");
      return;
    }
    if (!server)
    {
      RptF("SDZ_BE_RCON: rejected #exec because the server is unavailable");
      return;
    }
    server->QueueExec(BOT_CLIENT, RString(expression));
    RptF("SDZ_BE_RCON: queued #exec (%d bytes)", (int)strlen(expression));
    return;
  }

  NetworkClient *client = GNetworkManager.GetClient();
  if (client)
  {
    client->ProcessCommand(RString(command));
  }
#endif
}

#endif

// open the mpStatistics.log file and write the required statistics string
// it open and close the mpStatistics.log file each time
static void LogMPStatistics(RString str)
{
  FILE *file = NULL;
  char logFilename[MAX_PATH];
  if ( GetLocalSettingsDir(logFilename) )
  {
    CreateDirectory(logFilename, NULL);
    TerminateBy(logFilename, '\\');
    strcat(logFilename, "mpStatistics.log");
    file = fopen(logFilename, "a+");

    if (file)
    {
      fputs(cc_cast(str), file);
      fclose(file);
    }
  }
}

//! Load ban list from file
/*!
\param filename name (path) of file
\param list output ban list
*/
static void LoadBanList(RString filename, FindArray<RString> &list)
{
  list.Clear();
  QIFStream f;
  f.open(filename);
  while (!f.eof() && !f.fail())
  {
    int c = f.get();
    if (f.eof() || f.fail()) return;
    while (isspace(c) || c==',' || c==';')
    {
      c = f.get();
      if (f.eof() || f.fail()) return;
    }
    const int MaxBanIdLength = 255;
    char value[MaxBanIdLength+1];
    int ix=0;
    while (!isspace(c) && c!=',' && c!=';' && !f.eof() && ix<MaxBanIdLength)
    {
      value[ix++] = c;
      c = f.get();
    }
    value[ix] = 0;
    list.AddUnique(value);
  }
}

static void SaveBanList(RString filename, FindArray<RString> &list)
{
  QOFStream f(filename);
  for (int i=0; i<list.Size(); i++)
  {
    f.write(list[i], list[i].GetLength());
    f.put('\r');
    f.put('\n');
  }
  f.close();
}

//! Return if given message type is kind of transport update
/*!
When driver of transport changes, ownership of whole transport is changed.
*/
bool IsUpdateTransport(NetworkMessageType type)
{
  switch (type)
  {
  case NMTUpdateTransport:
  case NMTUpdateTankOrCar:
  case NMTUpdateTank:
  case NMTUpdateCar:
  case NMTUpdateMotorcycle:
  case NMTUpdateAirplane:
  case NMTUpdateHelicopter:
#if _HELISIM_ROTORLIB
  case NMTUpdateHelicopterRTD:
#endif
  case NMTUpdateParachute:
  case NMTUpdateShip:
  case NMTUpdateDoor:
    return true;
  default:
    return false;
  }
}


//! Find file bank with given prefix
QFBank *FindBank(const char *prefix)
{
  BankList::ReadAccess banks(GFileBanks);
  int prefixLen = strlen(prefix);
  for (int i=0; i<banks.Size(); i++)
  {
    QFBank &bank = banks[i];
    if (strnicmp(bank.GetPrefix(), prefix, prefixLen) == 0)
      return &bank;
  }
  return NULL;
}

#if _ENABLE_GAMESPY
#include "../GameSpy/common/gsPlatformUtil.h"
#include "../GameSpy/common/gsAvailable.h"
#endif

#if 0 // MP_VERSION_REQUIRED>=162
  const int DefaultSecureIdRequired = 1;
  #pragma message WARN_MESSAGE("warning: Remove obsolete code")
#else
  const int DefaultSecureIdRequired = 0;
#endif



NetworkServer::NetworkServer(NetworkManager *parent, RString name, RString password, int maxPlayers, int port, bool isPrivate)
: NetworkComponent(parent)
#if _ENABLE_DEDICATED_SERVER
, _globals(&_evaluator, "server", false)
#endif
{
  _serverState = NSSSelectingMission;
  _serverStateTime = GlobalTickCount();
  _serverTimeOutSent = INT_MAX;
#if _ENABLE_DEDICATED_SERVER
  _sqfExecBusy = false;
#endif
  _connectionDesync = CDGood;
  _desyncTotalUpdateNext = GlobalTickCount();
  _desyncTotalAvg = 0;
  _desyncTotalMax = 0;
  _buildNoRequired = 0;
  _secureIdRequired = DefaultSecureIdRequired;
  _longitude = INT_MAX;
  _latitude = INT_MAX;
  _disable3rdPerson = 0;
	_environmentTimeMul = 0.0f;

  #if USER_Ondra
    _updatesGuaranteed = true;
  #else
    _updatesGuaranteed = false;
  #endif
  _verifySignatures = false;
  _validateCDKey = false;
  _doNotReport = false;
  _qrInitDone = false;
  _persistent = false;
  _destroyDisconnected = true;

  _maxPlayersLimit = maxPlayers;
  _totalSlots = INT_MAX;

  _squadChecksTimeout = 0;

  _serverTime = _serverTimeOfEstimatedEnd = _secondsToEstimatedEnd = 0;
  _serverTimeInitTickCount = ::GetTickCount();
  _nextServerTimeUpdate = _serverTime + ServerTimeUpdateInterval;

  /*
  if (IsDedicatedServer() && !isPrivate)
  {
    _geoJob = new GeoLocationJob();
    _geoJob->_longitude = _longitude;
    _geoJob->_latitude = _latitude;
    GJobManager.CreateJob(_geoJob,10000);
  }
  */

#if _ENABLE_DEDICATED_SERVER
  // provide some reasonable default
  _difficulty = Glob.config.diffDefault;
#endif

#if _VBS2 && _VBS2_LITE
  _memoryWrite   = false;
  _nextRanUpdate = 0;
#endif

  _password = password.GetLength() > 0;

  _nextPlayerID = 2; // value 1 is reserved for static objects

  // make sure statistics from previous session will not be present
  GStats.ClearMission();

  Verify(Init(name, password, maxPlayers, port));

  _param1 = 0;
  _param2 = 0;
  _paramsArray.Clear();

  _timeLeftLastValue = 0;
  _timeLeftLastTime = 0;
  _timeLeftLastValueBcast = 0;

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  _kickDuplicate = false;
  _dedicated = false;
  _missionIndex = -1;

  _gameMaster = AI_PLAYER;
  _admin = false;
  _restart = false;
  _reassign = false;
  
  Monitor(0);
  _debugNext = INT_MAX;
  _debugInterval = 0;

  InitEvaluator();
#endif
//}
  _pingUpdateNext = GlobalTickCount() + 5000;
  
  _sessionLocked = false;

  _equalModRequired = false;

  // ensure no content remain
  if (_server) DeleteDirectoryStructure(GetServerTmpDir(), false);

  #if _ENABLE_DEDICATED_SERVER
    if (!IsDedicatedServer())
  #endif
    {
      LoadBanList(GetUserDirectory() + RString("ban.txt"), _banListLocal);
    }
  #if _VERIFY_CLIENT_KEY
    { // load secure ban list on DS as well
      RString banSecureFilename = GetUserDirectory() + RString("banSecure.txt");
      {
      QIFStream f;
      f.open(banSecureFilename);
      if (f.rest()>0)
      {
        AutoArray<char> banSecureInput;
        banSecureInput.Resize(f.rest());
        f.read(banSecureInput.Data(),banSecureInput.Size());
        ParseSignedKeys(_banListSecure.keys,_banListSecure.signature,banSecureInput.Data());
      }
      }
      GFileServerFunctions->FlushReadHandle(banSecureFilename);
    }
  #endif

#if _ENABLE_GAMESPY 
  // check that the game's back-end is available
  // we need it due to NAT Negotiation
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
  GSIStartAvailableCheck(GetGameName());
  GSIACResult result;
  while ((result = GSIAvailableCheckThink()) == GSIACWaiting)
  {
    msleep(5);
    ProgressRefresh();
  }
  ProgressFinish(p);
#endif


}

void NetworkServer::CreateDebugWindow()
{
#if _ENABLE_CHEATS
  _debugWindow = new DebugListWindow("Server debug");
#endif
}

#include "../Protect/selectProtection.h"

extern const int DefaultNetworkPort;

DWORD GetGameLanguage()
{
  // use internal language ID instead (and mark it)
  int GetLangID();
  return 0x10000 | GetLangID();
}


void NetworkServer::SendMuteList(const PlayerIdentity &id)
{
#if _ENABLE_MP
  int to = id.dpnid;
  
  RemoteMuteListMessage msg;

  for (int i=0; i<_players.Size(); i++)
  {
    const NetworkPlayerInfo &info = *_players[i];
    if (info.dpid == to) continue;

    for (int j=0; j<info.muteList._list.Size(); j++)
    {
      if (XOnlineAreUsersIdentical(info.muteList._list[j], id.xuid))
      {
        msg._list.Add(info.dpid);
        break;
      }
    }
  }
  SendMsg(to, &msg, NMFGuaranteed);
#endif
}

int NetworkServer::GetSessionTimeLeft() const
{
  if (_serverState == NSSPlaying)
  {
    int timeleft = 15;

    float et = _missionHeader._estimatedEndTime.toFloat();
    if (et > 0)
    {
      et -= Glob.time.toFloat();
      timeleft = toInt(et / 60.0);
      saturateMax(timeleft, 1);
    }
    return timeleft;
  }
  return 0;
}

RString GetDefaultSessionName(RString playerName)
{
  char hostName[128];
  bool getLocalName(char *name, unsigned len);
  if (getLocalName(hostName, 128))
  {
    if (playerName.GetLength() > 0)
      return Format(LocalizeString(IDS_SESSION_NAME_FORMAT), cc_cast(playerName), cc_cast(hostName));
    else
      return hostName;
  }
  return LocalizeString(IDS_SESSION_NAME_INIT);
}

// Macrovision CD Protection
#if _ENABLE_MP

//! Protected version of NetworkServer::Init
/*!
\param port IP port where server communicate
\param password session password
\patch 1.11 Date 07/31/2001 by Jirka
- Improved: Multiplayer session description
\patch 1.11 Date 08/02/2001 by Jirka
- Changed: Temporary directory on server in now Tmp<port> instead of Tmp
\patch 1.24 Date 09/19/2001 by Jirka
- Added: parameter "hostname" added to dedicated server config
- Added: port displayed in session description if not default
- Added: parameter "maxPlayers" added to dedicated server config
\patch 1.30 Date 11/01/2001 by Jirka
- Added: parameter "voteMissionPlayers" added to dedicated server config
  when no Missions section in server config and at least "voteMissionPlayers" (default = 1)
  players are connected, mission voting is started automatically
\patch 1.42 Date 1/9/2002 by Jirka
- Added: parameter "voiceOverNet" added to dedicated server config (default = true)
*/

CDP_DECL NetTranspServer * __cdecl CDPInitServer(RString name, RString password, int maxPlayers, int port, ParamEntryPar cfg)
{
  NetTranspServer *server = NULL;
  SECUROM_MARKER_HIGH_SECURITY_ON(10)
  //Direct play has been eliminated from here
  server = CreateNetServer(cfg);
  ProgressRefresh();
  if (!server) return NULL;

  // load some settings from Flashpoint.cfg
  server->SetNetworkParams(cfg);

  MPVersionInfo versionInfo;
  versionInfo.versionActual = MP_VERSION_ACTUAL;
  versionInfo.versionRequired = MP_VERSION_REQUIRED;
  versionInfo.mission[0] = 0;
  versionInfo.serverState = NSSSelectingMission;
#if USE_MOD_LIST
  RString GetModListNames();
  strncpy(versionInfo.mod, GetModListNames(), MOD_LENGTH);
#else
  strncpy(versionInfo.mod, GLoadedContentHash, MOD_LENGTH);
#endif
  versionInfo.mod[MOD_LENGTH - 1] = 0;

  bool voiceOverNet = true;

  bool equalModRequired = false;

#if _ENABLE_DEDICATED_SERVER
  if (IsDedicatedServer())
  {
    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile cfg;
    RString GetServerConfig();
    cfg.Parse(GetServerConfig(), NULL, NULL, &globals);
    
    ConstParamEntryPtr entry = cfg.FindEntry("hostName");
    if (entry) name = *entry;
    if (name.GetLength() == 0) name = GetDefaultSessionName(RString());

    ConstParamEntryPtr instId = cfg.FindEntry("instanceId");
    if (instId)
	{
		RString id = *instId;
		SetInstanceId(id.Data());
	}

    ConstParamEntryPtr instKey = cfg.FindEntry("instanceKey");
    if (instKey)
	{
		RString key = *instKey;
		SetInstanceKey(key.Data());
	}

    entry = cfg.FindEntry("voiceOverNet");
    if (entry)
    {
      voiceOverNet = *entry;
    }

    entry = cfg.FindEntry("equalModRequired");
    if (entry) equalModRequired = *entry;

    // on dedicated server, use only server name as session name
    // playerName = LocalizeString(IDS_DEDICATED_SERVER);
  }
  else
#endif
  {
    if (name.GetLength() == 0)
    {
#if _VBS2 // VBS2 nickname
      RString playerName = Glob.header.playerHandle;
#else
      RString playerName = Glob.header.GetPlayerName();
#endif
      name = GetDefaultSessionName(playerName);
    }
  }

  bool ok = server->Init(
    name, password, maxPlayers, port,  
    GetGameLanguage(), Glob.config.difficulty,
    versionInfo, equalModRequired, MAGIC_APP
  );

  if (!ok)
  {
    delete server;
    return NULL;
  }

#if _VBS3
  // Force the VON off if requested by the program argument
  extern bool DisableVON;
  if (DisableVON)
  {
    voiceOverNet = false;
  }
#endif

  if (voiceOverNet)
    server->InitVoice();

  char buffer[256];
  sprintf(buffer, "Tmp%d", server->GetSessionPort());
  ServerTmpDir = buffer;

  SECUROM_MARKER_HIGH_SECURITY_OFF(10)
  
  return server;
}
#endif //  _ENABLE_MP

void ParseFlashpointCfg(ParamFile &file);
void SaveFlashpointCfg(ParamFile &file);

/*!
\patch 1.89 Date 10/25/2002 by Jirka
- Added: Item "proxy" added to Flashpoint.cfg to enforce proxy server used to download xml squad page etc.
*/

bool NetworkServer::Init(RString name, RString password, int maxPlayers, int port)
{
#if _ENABLE_MP
  // load some settings from Flashpoint.cfg
  ParamFile cfg;
  ParseFlashpointCfg(cfg);

  ConstParamEntryPtr entry = cfg.FindEntry("proxy");
  if (entry) _proxy = *entry;

  _longitude = cfg.ReadValue("serverLongitude",INT_MAX);
  _latitude = cfg.ReadValue("serverLatitude",INT_MAX);
  _longLatAuto = cfg.ReadValue("serverGeoLocAuto",true);

  _indexToPlayerId.Add(BOT_CLIENT);
  // Macrovision CD Protection
  //_serverUpBandwidth = 1000000000; // assume very fast bandwidth unless proven otherwise - 1Gbps
  _server = CDPInitServer(name, password, maxPlayers, port, cfg);
  return _server != NULL;
  #else
  return false;
  #endif
}

void NetworkServer::InitVoice()
{
  if (!_parent->IsVoNDisabled())
    if (_server) _server->InitVoice();
}

NetworkServer::~NetworkServer()
{
#if _ENABLE_GAMESPY
  CloseGeoJob();
#endif
  RemoveSystemMessages();

  SetServerState(NSSNone);

//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY
  DoneQR();
#endif
//}


  Done();

#if _ENABLE_CHEATS
  if (_debugWindow) _debugWindow->Close();
#endif
}

void NetworkServer::Done()
{
#if PEER2PEER
  if (_p2pServer) _p2pServer = NULL;
#endif

  // _dp->CancelAsyncOperation(NULL, DPNCANCEL_ALL_OPERATIONS);
  RemoveUserMessages();
  if (_server) DeleteDirectoryStructure(GetServerTmpDir(), true);
  _server = NULL;
}

/*!
\patch 5095 Date 12/5/2006 by Jirka
- Fixed: Dedicated server crash
*/

int NetworkServer::GetMaxPlayers() const
{
  int maxPlayers = GetMaxPlayersLimit();
  if (_serverState >= NSSAssigningPlayers)
  { // maxPlayers from mission description.ext
    saturateMin(maxPlayers, GetMaxMissionPlayers());
  }
  saturateMin(maxPlayers, _totalSlots);

  return maxPlayers; 
}

int NetworkServer::GetMaxPlayersSafe(int bandwidthPerClient, int round) const
{
  int maxPlayers = GetMaxPlayersLimit();
  // always return even number
  maxPlayers = maxPlayers/round*round;
  if (maxPlayers<round) maxPlayers = round;
  return maxPlayers;
  
}

bool NetworkServer::GetURL(char *address, DWORD addressLen)
{
  if (!_server) return false;
  return _server->GetURL(address, addressLen);
}

/*!
\patch 1.21 Date 08/23/2001 by Jirka
- Improved: session description
*/
void NetworkServer::UpdateSessionDescription()
{
  if (_server)
  {
    RString gameType, island;
    LocalizedString mission;
    int timeleft = 0;
    int difficulty = Glob.config.difficulty;
    if (_serverState >= NSSAssigningPlayers)
    {
      gameType = _missionHeader._gameType;
      mission = _missionHeader._name;
      island = _missionHeader._island;
      timeleft = GetSessionTimeLeft();
    }
    _server->UpdateSessionDescription(
      _serverState, gameType, mission, island, timeleft, difficulty
    );
  }
}

RString QosInfo::GetLocalizedMissionName() const
{
  int type = missionLocalizedType-' ';
  saturate(type,LocalizedString::PlainText,LocalizedString::Stringtable);
  LocalizedString str(LocalizedString::Type(type),missionId.cstr());
  return str.GetLocalizedValue();
}

void QosInfo::SetMissionName(const LocalizedString &str)
{
  missionLocalizedType = ' '+str._type;
  strcpy(missionId,str._id);
}

DWORD WINAPI NetworkServer::GeoJobThreadFunction(void *ctx)
{
  GeoJobThread *geoJob = (GeoJobThread *)ctx;

  float latitude,longitude;
  GeoLocationFromIP(latitude,longitude,geoJob->_ip); // location of my own IP address

  if (longitude<FLT_MAX && latitude<FLT_MAX)
  {
   geoJob->_latitude.SetResult(toInt(latitude));
   geoJob->_longitude.SetResult(toInt(longitude));
  }
  else
  {
    geoJob->_latitude.SetResult(INT_MAX);
    geoJob->_longitude.SetResult(INT_MAX);
  }
  return 0;
}


/*!
\patch 1.21 Date 08/22/2001 by Jirka
- Added: GameSpy Query & Reporting SDK support for dedicated server
\patch_internal 1.86 Date 10/16/2002 by Jirka
- Added: ASE SDK support for dedicated server
\patch_internal 1.90 Date 11/12/2002 by Jirka
- Fixed: dedicated server was reporting to ASE even for reportingIP="" 
*/


void NetworkServer::OnPublicAddressKnown(int ip)
{
  BString<256> ipText;
  sprintf(ipText,"%d.%d.%d.%d",ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff);

  if (!_geoJob && IsDedicatedServer() && !_doNotReport)
  {
    _geoJob = new GeoJobThread();
    _geoJob->_ip = ipText.cstr();
#if USE_MT_LIB_FOR_JOBS
    _geoJob->_thread.Init( CreateMTThreadOnCPU(64 * 1024, GeoJobThreadFunction, _geoJob, 2, MultiThread::ThreadBase::PriorityNormal, "GeoLocation"));
#else
    _geoJob->_thread.Init( CreateThreadOnCPU(64 * 1024, GeoJobThreadFunction, _geoJob, 2, THREAD_PRIORITY_NORMAL, "GeoLocation"));
#endif
  }
}

//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY

// GameSpy callbacks

/*!
\patch 5090 Date 11/27/2006 by Jirka
- Fixed: Possible crash when hosting MP session
*/

// Called when a server key needs to be reported
void serverkey_callback(int keyid, qr2_buffer_t outbuf, void *userdata)
{
  if (GNetworkManager.GetServer() != userdata) return;
  NetworkServer *server = ((NetworkServer *)userdata);
  if (server->IsValid()) server->OnQueryServer(keyid, outbuf);
}

// Called when a player key needs to be reported
void playerkey_callback(int keyid, int index, qr2_buffer_t outbuf, void *userdata)
{
  if (GNetworkManager.GetServer() != userdata) return;
  NetworkServer *server = ((NetworkServer *)userdata);
  if (server->IsValid()) server->OnQueryPlayer(keyid, index, outbuf);
}

// Called when a team key needs to be reported
void teamkey_callback(int keyid, int index, qr2_buffer_t outbuf, void *userdata)
{
  if (GNetworkManager.GetServer() != userdata) return;
  NetworkServer *server = ((NetworkServer *)userdata);
  if (server->IsValid()) server->OnQueryTeam(keyid, index, outbuf);
}	

// Called when we need to report the list of keys we report values for
void keylist_callback(qr2_key_type keytype, qr2_keybuffer_t keybuffer, void *userdata)
{
  if (GNetworkManager.GetServer() != userdata) return;
  NetworkServer *server = ((NetworkServer *)userdata);
  if (server->IsValid()) server->OnQueryKeys(keytype, keybuffer);
}

// Called when we need to report the number of players and teams
int count_callback(qr2_key_type keytype, void *userdata)
{
  if (GNetworkManager.GetServer() != userdata) return 0;
  NetworkServer *server = ((NetworkServer *)userdata);
  if (server->IsValid()) return server->OnQueryCount(keytype);
  return 0;
}

// Called if our registration with the GameSpy master server failed
void adderror_callback(qr2_error_t error, gsi_char *errmsg, void *userdata)
{
  // Display an error message to the user indicating that he is unable to host a game and why (e.g., UDP-blocking firewall). 
  // Note: Applies to Validate Method (GameSpy Source code inspection)
  GWorld->CreateWarningMessage(errmsg);
  RptF("GameSpy QR2 error: %d, %s", error, errmsg);
  (void *)userdata;
}

// NAT Negotiation callbacks
static void NNProgressCallback(NegotiateState state, void *userdata)
{
}

static void NNCompletedCallback(NegotiateResult result, SOCKET gamesocket, struct sockaddr_in *remoteaddr, void *userdata)
{
  switch (result)
  {
  case nr_success:
    RptF("NAT Negotiation completed");
    break;
  case nr_deadbeatpartner:
    RptF("NAT Negotiation failed - partner not found");
    break;
  case nr_inittimeout:
    RptF("NAT Negotiation failed - unable to communicate with the server");
    break;
  case nr_unknownerror:
    RptF("NAT Negotiation failed - unknown error");
    break;
  }
}

void NNDetectionResultsCallback(gsi_bool success, NAT nat)
{
  QOStrStream natType;
  if (success) 
  {
    natType << "Begin (NAT type):\n";
    static bool doIt = false;
    if (doIt)
    {
      if (*nat.brand) natType << "   Brand: " << nat.brand << "\n";
      if (*nat.model) natType << "   Model: " << nat.model << "\n";
      if (*nat.firmware) natType << "   Firmware: " << nat.firmware << "\n";
    }
    RString natTypeStr;
    switch (nat.natType)
    {
    case no_nat: natTypeStr = "no nat"; break;
    case firewall_only: natTypeStr = "firewall only"; break;
    case full_cone: natTypeStr = "full cone"; break;
    case restricted_cone: natTypeStr = "restricted cone"; break;
    case port_restricted_cone: natTypeStr = "port restricted cone"; break;
    case symmetric: natTypeStr = "symmetric"; break;
    case unknown: 
    default: natTypeStr = "unknown";
    }
    natType << "   NAT type: " << cc_cast(natTypeStr) << "\n"; 
    RString natPromiscuity;
    switch (nat.promiscuity)
    {
    case promiscuous: natPromiscuity = "promiscuous"; break;
    case not_promiscuous: natPromiscuity = "not promiscuous"; break;
    case port_promiscuous: natPromiscuity = "port promiscuous"; break;
    case ip_promiscuous: natPromiscuity = "ip promiscuous"; break;
    case promiscuity_not_applicable:
    default: natPromiscuity = "promiscuity not applicable"; break;
    }
    natType << "   NAT promiscuity: " << cc_cast(natPromiscuity) << "\n";
    RString natMappingScheme;
    switch (nat.mappingScheme)
    {
    case private_as_public: natMappingScheme = "private as public"; break;
    case consistent_port: natMappingScheme = "consistent port"; break;
    case incremental: natMappingScheme = "incremental"; break;
    case mixed: natMappingScheme = "mixed"; break;
    case unrecognized:
    default: natMappingScheme = "unrecognized"; break;
    }
    natType << "   NAT mapping scheme: " << cc_cast(natMappingScheme) << "\n";
    if (nat.ipRestricted) natType << "   NAT is IP restricted\n";
    if (nat.portRestricted) natType << "   NAT is PORT restricted\n";
    if (nat.qr2Compatible) natType << "   NAT is qr2 compatible\n";
    natType << "End (NAT type)";
  }
  else natType << "NAT detection was not successful.";
  RString detectedNatType(natType.str(), natType.pcount());
  GDebugVoNBank._natType = detectedNatType;
#if !_SUPER_RELEASE && ENABLE_REPORT
  if (GInput.cheatVoNDetectNatType)
  {
    RptF(cc_cast(detectedNatType));
  }
#endif
}

// Called when a client wants to connect using nat negotiation
//   (Nat Negotiation must be enabled in qr2_init)
void nn_callback(int cookie, void *userdata)
{
  LogF("NAT Negotiation started for cookie : %d", cookie);
  SOCKET GetServerSocket();
  enterNN();
  NegotiateError error = NNBeginNegotiationWithSocket(GetServerSocket(), cookie, 1, NNProgressCallback, NNCompletedCallback, NULL);
#if !_SUPER_RELEASE && _ENABLE_CHEATS
  if (!GDebugVoNBank.NatTypeDetectionTriggered())
  {
    GDebugVoNBank.TriggerNatTypeDetection();
    NegotiateError errorDet = NNStartNatDetection(NNDetectionResultsCallback);
    (void) errorDet;
  }
#endif
  leaveNN();
  (void)error;
  (void)userdata;
}

void SetQRInitialized(bool set);

/*!
\patch 5164 Date 10/3/2007 by Jirka
- Fixed: Handling of invalid CD keys on dedicated servers improved
*/

#if _ENABLE_GAMESPY && _VERIFY_CLIENT_KEY
void NetworkServer::OnCDKeyAuthorize(int player, int authenticated, const char *errmsg)
{
  if (!authenticated)
  {
    // wrong CD Key - kick off client, shut down server
    if (player == BOT_CLIENT)
    {
      GChatList.Add(CCGlobal, NULL, errmsg, false, true);
      WarningMessage(errmsg);
/*
      WarningMessage(LocalizeString(keyInUse ? IDS_MP_CDKEY_IN_USE : IDS_MP_BAD_CDKEY));
*/
      Done();
    }
    else
    {
/*
      // using DisconnectMessage is not sufficient as we need the kick being immediate
      //   "Note that this is of course when the ArmA client does not disconnect itself, 
      //    naturally the server itself has to do this to have control and not to allow
      //    the player to stay on the server with some exploit"
      DisconnectMessage msg;
      msg._message = errmsg;
      SendMsg(player, &msg, NMFGuaranteed);
*/
      bool keyInUse = strcmp(errmsg, "CD Key in use") == 0;
      KickOff(player, keyInUse ? KORCDKeyInUse : KORBadCDKey, errmsg);
    }
/*
    NetworkPlayerInfo *info = GetPlayerInfo(player);
    LogF("%f: Player %s (%d) was not authenticated (%s)", 1e-3 * GlobalTickCount(), info ? cc_cast(info->name) : "<unknown>", player, errmsg);
*/
  }
  else
  {
/*
    NetworkPlayerInfo *info = GetPlayerInfo(player);
    LogF("%f: Player %s (%d) was successfully authenticated (%s)", 1e-3 * GlobalTickCount(), info ? cc_cast(info->name) : "<unknown>", player, errmsg);
*/
  }
}

/// Callback function to indicate whether a client has been authorized or not.
static void OnAuthorize(int productid, int localid, int authenticated, char *errmsg, void *instance)
{
  NetworkServer *server = reinterpret_cast<NetworkServer *>(instance);
  if (server && server == GNetworkManager.GetServer()) server->OnCDKeyAuthorize(localid, authenticated, errmsg);
}

/// Callback function to reauthorize players.
static void OnRefreshAuthorize(int gameid, int localid, int hint, char *challenge, void *instance)
{
  NetworkServer *server = reinterpret_cast<NetworkServer *>(instance);

  if (server && server == GNetworkManager.GetServer())
  {
    CDKeyRecheckAskMessage ask;
    ask._hint = hint;
    ask._challenge = challenge;

    server->SendMsg(localid, &ask, NMFGuaranteed);
  }
}

// Called when the local clients public address is received
void pa_callback(unsigned int ip, unsigned short port, void *userdata)
{
  NetworkServer *server = reinterpret_cast<NetworkServer *>(userdata);
  if (server && server == GNetworkManager.GetServer() /*&& server->IsDedicatedBotClient(BOT_CLIENT)*/)
  {
    NetworkPlayerInfo *player = server->GetPlayerInfo(BOT_CLIENT);
    DoAssert(player);
    if (player)
    {
      player->_publicAddr = ip;
      player->_publicPort = port;

      // once we know a public address, we can start geolocating
      server->OnPublicAddressKnown(ip);
    }
  }
}

#endif

void RegisterQR2Keys()
{
  qr2_register_key(PARAM1_KEY, "param1");
  qr2_register_key(PARAM2_KEY, "param2");
  qr2_register_key(VER_KEY, "ver");
  qr2_register_key(MOD_KEY, "mod");
  qr2_register_key(GAME_STATE_KEY, "gameState");
  qr2_register_key(PLATFORM_KEY, "platform");
  qr2_register_key(LANGUAGE_KEY, "language");
  qr2_register_key(MISSION_KEY, "mission");
  qr2_register_key(MOD_HASH_KEY, "modhash");
  qr2_register_key(HASH_KEY, "hash");
  qr2_register_key(REQ_BUILD_KEY, "reqBuild");
  qr2_register_key(REQ_SECURE_ID_KEY, "reqSecureId");
  qr2_register_key(LONGITUDE_KEY, "lng");
  qr2_register_key(LATITUDE_KEY, "lat");
  qr2_register_key(DS_KEY, "ds");
  qr2_register_key(EQ_MOD_KEY, "eqMod");
  qr2_register_key(REQ_VER_KEY, "reqVer");
  qr2_register_key(DIFF_KEY, "diff");
  qr2_register_key(BE_KEY, "be");
  qr2_register_key(SIG_KEY, "sig");
  qr2_register_key(VER_SIG_KEY, "verSig");
}
/*!
\patch 1.34 Date 12/04/2001 by Jirka
- Changed: GameSpy base local port
*/
void NetworkServer::InitQR(bool isPrivate)
{
  _qrInitDone = true;
#if _ENABLE_DEDICATED_SERVER
  if (stricmp(_reportingIP, "noreport") == 0) isPrivate = true;
#endif
  _doNotReport = isPrivate;

  RegisterQR2Keys();
  
  
#if _ENABLE_DEDICATED_SERVER
  if ( !_doNotReport && !_reportingIP.IsEmpty() )
  {
    int len = sizeof(qr2_hostname) / sizeof(char);
    strncpy(qr2_hostname, _reportingIP, len);
    qr2_hostname[len - 1] = 0;
  }
#endif

  static bool useNatNegotiation = true;

  SOCKET GetServerSocket();
  if (qr2_init_socket(NULL, GetServerSocket(), GetSessionPort(),
    GetGameName(), GetSecretKey(), _doNotReport ? 0 : 1, useNatNegotiation,
    serverkey_callback, playerkey_callback, teamkey_callback,
    keylist_callback, count_callback, adderror_callback, this) == e_qrnoerror)
  {
    SetQRInitialized(true);

    // Set a function to be called when we receive a nat negotiation request
    qr2_register_natneg_callback(NULL, nn_callback);
#if _VERIFY_CLIENT_KEY
    //set host name DNS
    gsiSafeStrcpyA(gcd_hostname, GetGcdHostname(), sizeof(gcd_hostname)); 
    if (gcd_init(GetGameID()) == 0)
      _validateCDKey = true; //this enable validation on GameSpy
    qr2_register_publicaddress_callback(NULL, pa_callback);
#endif
  }

#if REPORT_ASE
  ASEMaster_init(1);
#endif
}

void NetworkServer::DoneQR()
{
#if _VERIFY_CLIENT_KEY
  if (_validateCDKey)
  {
    gcd_disconnect_all(GetGameID());
    gcd_shutdown();
    _validateCDKey = false;
  }
#endif

  //let game master know we are shutting down (removes dead server entry from the list)
  SetQRInitialized(false);
  qr2_shutdown(NULL);

#ifdef GSI_UNICODE
  // In Unicode mode we must perform additional cleanup
  qr2_internal_key_list_free();
#endif
}

void NetworkServer::SimulateQR()
{
  if (!_qrInitDone) 
    return; //InitQR was not called (possibly playing LAN game)
  //check for / process incoming queries
  qr2_think(NULL);
#if _VERIFY_CLIENT_KEY
  if (_validateCDKey) gcd_think();
#endif

#if REPORT_ASE
  ASEMaster_heartbeat(0, ::GetServerPort() + 1, GetGameName());
#endif
}

void NetworkServer::ChangeStateQR()
{
  qr2_send_statechanged(NULL);
}

const int MaxGSMaxSize = 1300;

//max 200B for signatures list GS key
const int MaxGSKey = 200;

static int GetMaxKeySize(qr2_buffer_t outbuf)
{
  struct qr2_buffer_header
  {
	  char buffer[32]; // in fact much longer, but 32 is something we can be sure of
  } * header = reinterpret_cast<qr2_buffer_header *>(outbuf);
  // packet type 0 (PACKET_QUERY), followed by some 4 bytes, followed with "splitnum"
  if (header->buffer[0]==0 && !strncmp(header->buffer+5,"splitnum",sizeof("splitnum")))
  {
    return MaxGSMaxSize; // max. packet size is 1400, but something is needed for splitNum header, keyname ...
  }
  else
  {
    return MaxGSKey;
  }
}
/// overload to handle cutting very long values transparently
void qr2_buffer_add(qr2_buffer_t outbuf, RString longString)
{
  int len = longString.GetLength();
  int maxSize = GetMaxKeySize(outbuf);
  if (len>=maxSize) 
  {
    BString<MaxGSMaxSize> shortString;
    shortString = longString;
    Assert(maxSize>4);
    STATIC_ASSERT(MaxGSMaxSize>4 && MaxGSKey>4);
    strcpy(shortString+(maxSize-4),"...");
    qr2_buffer_add(outbuf,cc_cast(shortString));
  }
  else
  {
    qr2_buffer_add(outbuf,cc_cast(longString));
  }
}

void NetworkServer::CloseGeoJob() const
{
  if (_geoJob)
  {
    // make sure the thread has done its work and will no longer access any variables
    _geoJob->_longitude.GetResult();
    _geoJob->_latitude.GetResult();
    _geoJob->_thread.Wait();
    //RptF("Closing geoJob");
    _geoJob->_thread.Done();
    //RptF("GeoJob closed!"); //NOTE: there was "Critical: Destroying running thread" between these two RptF!
    _geoJob.Free();
  }
}

void NetworkServer::ProcessGeoJob() const
{
  if (_geoJob && _geoJob->_longitude.IsReady())
  {
    int longitude = _geoJob->_longitude.GetResult();
    int latitude = _geoJob->_latitude.GetResult();
    CloseGeoJob();
    // save the values so that we can find them next time
    ParamFile fCfg;
    ParseFlashpointCfg(fCfg);
    if (_longLatAuto)
    {
      _longitude = longitude;
      _latitude = latitude;
      fCfg.Add("serverLongitude",_longitude);
      fCfg.Add("serverLatitude",_latitude);
    }
    fCfg.Add("serverLongitudeAuto",longitude);
    fCfg.Add("serverLatitudeAuto",latitude);
    SaveFlashpointCfg(fCfg);

  }
}

static int IntParam(float param)
{
  if (param>INT_MAX) return INT_MAX;
  return toLargeInt(param);
}

void NetworkServer::OnQueryServer(int keyid, qr2_buffer_t outbuf)
{
  switch (keyid)
  {
  case GAMEVER_KEY:
    qr2_buffer_add(outbuf, Format("%s.%d", APP_VERSION_TEXT, BUILD_NO));
    break;
  case HOSTNAME_KEY:
    qr2_buffer_add(outbuf, _server->GetSessionName());
    break;
  case GAMENAME_KEY:
    qr2_buffer_add(outbuf, GetGameName());
    break;
/*
  case HOSTPORT_KEY:
    qr2_buffer_add_int(outbuf, _server->GetSessionPort());
    break;
*/
  case MAPNAME_KEY:
    {
      RString island;
      if (_serverState >= NSSAssigningPlayers) island = _missionHeader._island;
      qr2_buffer_add(outbuf, island);
    }
    break;
  case GAMETYPE_KEY:
    {
      if (Glob.isDayZ==Globals::IsDayZ)
      {
        qr2_buffer_add(outbuf, "DAYZ");
        break;
      }
      else if (Glob.isDayZ==Globals::IsDayZOther)
      {
        qr2_buffer_add(outbuf, "DAYZ_M");
        break;
      }

      RString gameType;
      if (_serverState >= NSSAssigningPlayers) gameType = _missionHeader._gameType;
      qr2_buffer_add(outbuf, gameType);
    }
    break;
  case MISSION_KEY:
    {
      RString mission;
      if (_serverState >= NSSAssigningPlayers) mission = _missionHeader.GetLocalizedMissionName();
      qr2_buffer_add(outbuf, mission);
    }
    break;
  case NUMPLAYERS_KEY:
    qr2_buffer_add_int(outbuf, _identities.Size());
    break;
  case NUMTEAMS_KEY:
    qr2_buffer_add_int(outbuf, 0);
    break;
  case MAXPLAYERS_KEY:
    qr2_buffer_add_int(outbuf, GetMaxPlayers());
    break;
  case GAMEMODE_KEY:
    {
      const char *mode = "";
      switch (_serverState)
      {
      case NSSNone:
        mode = "exiting"; 
        break;
      case NSSSelectingMission:
      case NSSEditingMission:
      case NSSAssigningPlayers:
        mode = "openwaiting";
        break;
      case NSSSendingMission:
      case NSSLoadingGame:
      case NSSPlaying:
        mode = "openplaying"; 
        break;
      case NSSMissionAborted:
        mode = "openwaiting";
        break;
      default:
        Fail("Game state");
        break;
      }
      qr2_buffer_add(outbuf, mode);
    }
    break;
  case TIMELIMIT_KEY:
    {
      int timeleft;
      if (_serverState == NSSPlaying)
      {
        timeleft = 15;

        float et = _missionHeader._estimatedEndTime.toFloat();
        if (et > 0)
        {
          et -= Glob.time.toFloat();
          timeleft = toInt(et / 60.0);
          saturateMax(timeleft, 1);
        }
      }
      else timeleft = 0;
      qr2_buffer_add_int(outbuf, timeleft);
    }
    break;
  case PASSWORD_KEY:
    qr2_buffer_add_int(outbuf, (_password ? 2 : 0) + (_sessionLocked ? 1 : 0));
    break;
  case PARAM1_KEY:
    qr2_buffer_add_int(outbuf, IntParam(_param1));
    break;
  case PARAM2_KEY:
    qr2_buffer_add_int(outbuf, IntParam(_param2));
    break;
  case CURRENT_VERSION_KEY:
  case VER_KEY:
    qr2_buffer_add_int(outbuf, MP_VERSION_ACTUAL);
    break;
  case REQ_VER_KEY:
  case REQUIRED_VERSION_KEY:
    qr2_buffer_add_int(outbuf, MP_VERSION_REQUIRED);
    break;
  case MOD_KEY:
    {
      RString GetModListNames();
      qr2_buffer_add(outbuf, GetModListNames());
    }
    break;
  case MOD_HASH_KEY:
    {
      RString hashes;
      for (int i=0; i<GModInfos.Size(); i++)
      {
        if (i) hashes = hashes + RString(";") + GModInfos[i].hash;
        else hashes = GModInfos[i].hash;
      }
      qr2_buffer_add(outbuf, hashes);
    }
    break;
  case EQ_MOD_KEY:
  case EQUAL_MOD_REQUIRED_KEY:
    qr2_buffer_add_int(outbuf, _equalModRequired ? 1 : 0);
    break;
  case HASH_KEY:
    qr2_buffer_add(outbuf, GLoadedContentHash);
    break;
  case GAME_STATE_KEY:
    qr2_buffer_add_int(outbuf, _serverState);
    break;
  case DS_KEY:
  case DEDICATED_KEY:
#if _ENABLE_DEDICATED_SERVER
    qr2_buffer_add_int(outbuf, _dedicated ? 1 : 0);
#else
    qr2_buffer_add_int(outbuf, 0);
#endif
    break;
  case PLATFORM_KEY:
#if defined(_WIN32)
    qr2_buffer_add(outbuf, "win");
#elif defined(_LINUX)
    qr2_buffer_add(outbuf, "linux");
#else
    qr2_buffer_add(outbuf, "?");
#endif
    break;
  case LANGUAGE_KEY:
    qr2_buffer_add_int(outbuf, GetGameLanguage());
    break;
  case DIFFICULTY_KEY:
  case DIFF_KEY:
    qr2_buffer_add_int(outbuf, Glob.config.difficulty);
    break;
  case BATTLEYE_KEY:
  case BE_KEY:
    {
#if _USE_BATTL_EYE_SERVER
      int beEnabled = _beIntegration.IsEnabled() ? 1 : 0;
      qr2_buffer_add_int(outbuf, beEnabled);
#else
      qr2_buffer_add_int(outbuf, 0); //FIX: we must add some value!
#endif
    }
    break;
  case SIGNATURES_KEY:
  case SIG_KEY:
    qr2_buffer_add(outbuf, GetSignaturesList(GetMaxKeySize(outbuf))); 
    break;
  case VERIFY_SIGNATURES_KEY:
  case VER_SIG_KEY:
    qr2_buffer_add_int(outbuf, _verifySignatures ? _sigVerRequired : 0);
    break;
  case REQ_BUILD_KEY:
    qr2_buffer_add_int(outbuf, _buildNoRequired);
    break;
  case REQ_SECURE_ID_KEY:
    qr2_buffer_add_int(outbuf, _secureIdRequired);
    break;
  case LONGITUDE_KEY:
    ProcessGeoJob();
    qr2_buffer_add_int(outbuf, _longitude);
    break;
  case LATITUDE_KEY:
    ProcessGeoJob();
    qr2_buffer_add_int(outbuf, _latitude);
    break;
  default:
    qr2_buffer_add(outbuf, _T(""));
    break;
  }
}

//Note: it can contain little bit more chars than maxLen as there can be terminating ";..." when space was not sufficient!
RString NetworkServer::GetSignaturesList(int maxLen)
{
  if (!maxLen)  maxLen=INT_MAX;
  RString list;
  for (int i=0, len=0; i < _acceptedKeys.Size(); i++)
  {
    if (i) 
    {
      list = list + RString(";"); //no ";" at the end of list
      len++;
    }
    int sigLen = _acceptedKeys[i]._name.GetLength();
    if ( len + sigLen > maxLen )
    { // truncate and finish signatures list collecting
      list = list + "..."; // mark signatures list as truncated
      break;
    }
    len += sigLen;
    list = list + _acceptedKeys[i]._name;
  }
  return list;
}

void NetworkServer::OnQueryPlayer(int keyid, int index, qr2_buffer_t outbuf)
{
  //check for valid index
  if (index >= _identities.Size())
  {
    qr2_buffer_add(outbuf, _T(""));
    return;
  }

  const PlayerIdentity &identity = _identities[index];
  switch (keyid)
  {
  case PLAYER__KEY:
    qr2_buffer_add(outbuf, identity.name);
    break;
  case TEAM__KEY:
    {
      RString squad;
      if (identity.squad) squad = identity.squad->_nick;
      qr2_buffer_add(outbuf, squad);
    }
    break;
  case SCORE__KEY:
    {
      //qr2_buffer_add_int(outbuf, score);
    }
    break;
  case DEATHS__KEY:
    {
      //qr2_buffer_add_int(outbuf, deaths);
    }
    break;
  default:
    qr2_buffer_add(outbuf, _T(""));
    break;		
  }
}

void NetworkServer::OnQueryTeam(int keyid, int index, qr2_buffer_t outbuf)
{
  qr2_buffer_add(outbuf, _T(""));
}

void NetworkServer::OnQueryKeys(int keytype, qr2_keybuffer_t keybuffer)
{
  //need to add all the keys we support
  switch (keytype)
  {
  case key_server:
    qr2_keybuffer_add(keybuffer, GAMEVER_KEY);
    qr2_keybuffer_add(keybuffer, HOSTNAME_KEY);
//    qr2_keybuffer_add(keybuffer, HOSTPORT_KEY);
    qr2_keybuffer_add(keybuffer, MAPNAME_KEY);
    qr2_keybuffer_add(keybuffer, GAMETYPE_KEY);
    qr2_keybuffer_add(keybuffer, NUMPLAYERS_KEY);
    qr2_keybuffer_add(keybuffer, NUMTEAMS_KEY);
    qr2_keybuffer_add(keybuffer, MAXPLAYERS_KEY);
    qr2_keybuffer_add(keybuffer, GAMEMODE_KEY);
    qr2_keybuffer_add(keybuffer, TIMELIMIT_KEY);
    qr2_keybuffer_add(keybuffer, PASSWORD_KEY);
    qr2_keybuffer_add(keybuffer, PARAM1_KEY);
    qr2_keybuffer_add(keybuffer, PARAM2_KEY);
    qr2_keybuffer_add(keybuffer, CURRENT_VERSION_KEY);
    qr2_keybuffer_add(keybuffer, REQUIRED_VERSION_KEY);
    qr2_keybuffer_add(keybuffer, MOD_KEY);
    qr2_keybuffer_add(keybuffer, EQUAL_MOD_REQUIRED_KEY);
    qr2_keybuffer_add(keybuffer, GAME_STATE_KEY);
    qr2_keybuffer_add(keybuffer, DEDICATED_KEY);
    qr2_keybuffer_add(keybuffer, PLATFORM_KEY);
    qr2_keybuffer_add(keybuffer, LANGUAGE_KEY);
    qr2_keybuffer_add(keybuffer, DIFFICULTY_KEY);
    qr2_keybuffer_add(keybuffer, MISSION_KEY);
    qr2_keybuffer_add(keybuffer, GAMENAME_KEY);
    qr2_keybuffer_add(keybuffer, BATTLEYE_KEY);
    qr2_keybuffer_add(keybuffer, VERIFY_SIGNATURES_KEY);
    qr2_keybuffer_add(keybuffer, SIGNATURES_KEY);
    qr2_keybuffer_add(keybuffer, MOD_HASH_KEY);
    qr2_keybuffer_add(keybuffer, HASH_KEY);
    qr2_keybuffer_add(keybuffer, REQ_BUILD_KEY);
    qr2_keybuffer_add(keybuffer, REQ_SECURE_ID_KEY);
    qr2_keybuffer_add(keybuffer, LATITUDE_KEY);
    qr2_keybuffer_add(keybuffer, LONGITUDE_KEY);
    break;
  case key_player:
    qr2_keybuffer_add(keybuffer, PLAYER__KEY);
    qr2_keybuffer_add(keybuffer, TEAM__KEY);
    qr2_keybuffer_add(keybuffer, SCORE__KEY);
    qr2_keybuffer_add(keybuffer, DEATHS__KEY);
    break;
  case key_team:
    break;
  default:
    break;
  }
}

int NetworkServer::OnQueryCount(int keytype)
{
  switch (keytype)
  {
  case key_player:
    return _identities.Size();
  case key_team:
    return 0;
  default:
    return 0;
  }
}

/*
void NetworkServer::OnQueryRules(char *outbuf, int maxlen)
{
  if (!SafePrint(ptr, maxlen, "\\mod\\%s", (const char *)GetModListShort())) return;
  if (!SafePrint(ptr, maxlen, "\\equalModRequired\\%d", _equalModRequired)) return;
  if (!SafePrint(ptr, maxlen, "\\gstate\\%d", _serverState)) return;
  + dedicated
  
  }
*/

#endif
//}


bool NetworkServer::DBSave(RString baseURL, int dpnid, RString request)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpnid);
  if (!info)
	  return false;
  PlayerIdentity *identity = FindIdentity(dpnid);
  if (!identity)
	  return false;

  return info->DBSave(baseURL, identity->id, request);
}

void NetworkServer::DBLoad(RString baseURL, int playerId, GameDataCode *code)
{
  const PlayerIdentity *identity = FindIdentityByPlayerId(playerId);
  if (!identity)
	  return;
  NetworkPlayerInfo *info = GetPlayerInfo(identity->dpnid);
  if (!info)
	  return;
  if (info)
	  info->DBLoad(baseURL, playerId, identity->id, info->name, code);
}

void LoadMissions(bool userMissions, AutoArray<MPMissionInfo> &missionList, bool campaignCheat=false);
static void AddMissionList(NetworkCommandMessage &answer, NetworkCommandMessage &answer2)
{
  PROFILE_SCOPE_EX(addML, network)
  AutoArray<MPMissionInfo> missionList;
  LoadMissions(false, missionList);
  for (int i=0; i<missionList.Size(); i++)
  {
    answer._content.WriteString(missionList[i].mission);
    answer._content.WriteString(missionList[i].world);
    answer._content.WriteString(missionList[i].gameType);
    answer._content.WriteLocalizedString(missionList[i].displayName);
    answer._content.Write(&missionList[i].placement, sizeof(int));
    answer._content.Write(&missionList[i].minPlayers, sizeof(int));
    answer._content.Write(&missionList[i].maxPlayers, sizeof(int));
    answer._content.Write(&missionList[i].perClient, sizeof(int));
  }
  answer._content.WriteString("");

  // add additional information into the answer2 (not added into the answer1 becaus of backward MP compatibility)
  int count = missionList.Size();
  answer2._content.Write(&count, sizeof(int));
  for (int i=0; i<count; i++)
  {
    answer2._content.WriteLocalizedString(missionList[i].description);
  }
}

#if _ENABLE_DEDICATED_SERVER
void NetworkServer::SetGameMaster(int player, bool admin)
{
  Monitor(0); // new game master is not willing to get monitor lines in the previous master rate
  _gameMaster = player;
  _admin = admin;

  RString name;
  for (int i=0; i<_players.Size(); i++)
  {
    if (_players[i]->dpid == player)
    {
      name = _players[i]->name;
      break;
    }
  }
  ConsoleF(LocalizeString(IDS_DS_ADMIN_LOGGED_IN), (const char *)name);

  _votings.Clear();

  NetworkCommandMessage answer;
  answer._type = NCMTLogged;
  answer._content.Write(&_admin, sizeof(_admin));
  // AddMissionList(answer);
  SendMsg(player, &answer, NMFGuaranteed);

  if (_mission[0] == '?' && _mission[1] == 0)
  {
    NetworkCommandMessage answer;
    answer._type = NCMTVoteMission;
    NetworkCommandMessage answer2;
    answer2._type = NCMTVoteMissionExt;
    AddMissionList(answer, answer2);
    SendMsg(player, &answer, NMFGuaranteed);
    SendMsg(player, &answer2, NMFGuaranteed);
  }

  UpdateAdminState();
}
#endif

void NetworkServer::ApplyVoting(const AutoArray<char> &id, const AutoArray<char> *value)
{
#if _ENABLE_DEDICATED_SERVER
  int *ptr = (int *)id.Data();
  int type = *(ptr++);
  switch (type)
  {
    case NCMTMission:
      if (value)
      {
        // difficulty voting is separate - ask it for a result
        // the voting is performed at the same time, therefore the result should be ready
        _difficulty = Glob.config.diffDefault;
        const Voting *diff = _votings.Find(NCMTDifficulty);
        if (diff)
        {
          const AutoArray<char> *diffValue = diff->GetValue();
          if (diffValue)
          {
            RString diffName(diffValue->Data(),diffValue->Size());
            int difficulty = Glob.config.diffNames.GetValue(diffName);
            if (difficulty >= 0) _difficulty = difficulty;
          }
          _votings.DeleteVoting(NCMTDifficulty);
        }
        // check mission voting result
        _mission = RString(value->Data(),value->Size());
        if (_mission.GetLength() > 0) _restart = true;
        if (_serverState != NSSSelectingMission && _serverState != NSSPlaying)
        {
          GNetworkManager.DestroyAllObjects();
          SetServerState(NSSSelectingMission);
        }
      }
      break;
    case NCMTMissions:
      DoMissions(false);
      break;
    case NCMTRestart:
      if (_serverState == NSSPlaying)
      {
        _restart = true;
        _reassign = false;
      }
      break;
    case NCMTReassign:
      DoReassign();
      break;
    case NCMTKick:
      {
        int player = *(ptr++);
        if (player != BOT_CLIENT) KickOff(player,KORKick);
      }
      break;
    case NCMTAdmin:
      if (value)
      {
        const char *p = value->Data();
        int player = *((int *)p);
        if (player != AI_PLAYER && _dedicated && _gameMaster == AI_PLAYER)
          SetGameMaster(player, true);
      }
      break;
  }
#endif
}

void OnServerUserMessage(int from, char *buffer, int bufferSize, void *context)
{
  NetworkServer *server = (NetworkServer *)context;
  NetworkMessageRaw rawMsg(buffer, bufferSize);
  server->DecodeMessage(from, rawMsg);
}

void OnServerSendComplete(DWORD msgID, bool ok, void *context)
{
}

void OnServerPlayerCreate(int player, bool botClient, bool privateSlot, const char *name, const char *mod, unsigned long inaddr, void *context)
{
  if (!botClient && stricmp(mod, GLoadedContentHash) != 0)
  {
    RString format = LocalizeString(IDS_MP_VALIDERROR_2);
    RString message = Format(format, cc_cast(name));

#if ENABLE_CLIENT_USE_MODIFIED_DATA_CHAT
    if (!ChannelDisabled(CCSystem))
    {
      RefArray<NetworkObject> dummy;
      GNetworkManager.Chat(CCSystem, "", dummy, message);
    }
    GChatList.Add(CCSystem, NULL, message, false, true);
#endif
#if _ENABLE_DEDICATED_SERVER
    ConsoleF("%s", cc_cast(message));
#endif
  }
  
  NetworkServer *server = (NetworkServer *)context;
  server->OnCreatePlayer(player, privateSlot, name, inaddr);
}

void OnServerPlayerDelete(int player, void *context)
{
  NetworkServer *server = (NetworkServer *)context;
  server->OnPlayerDestroy(player);
}

bool OnServerVoicePlayerCreate(int player, void *context)
{
  NetworkServer *server = (NetworkServer *)context;
  return server->OnCreateVoicePlayer(player);
}

unsigned NetworkServer::CleanUpMemory()
{
  if (_server) return _server->FreeMemory();
  return 0;
}

void NetworkServer::OnClose()
{
#if _USE_BATTL_EYE_SERVER
  _beIntegration.Done();
#endif
}
void NetworkServer::ReceiveSystemMessages()
{
  if (_server)
  {
    _server->ProcessSendComplete(OnServerSendComplete, this);
    _server->ProcessPlayers(OnServerPlayerCreate, OnServerPlayerDelete, this);
    _server->ProcessVoicePlayers(OnServerVoicePlayerCreate, this);
  }
}

void NetworkServer::ReceiveUserMessages()
{
  if (_server) _server->ProcessUserMessages(OnServerUserMessage, this);
}

void NetworkServer::RemoveSystemMessages()
{
  if (_server)
  {
    _server->RemoveSendComplete();
    _server->RemovePlayers();
    _server->RemoveVoicePlayers();
  }
}

void NetworkServer::RemoveUserMessages()
{
  if (_server) _server->RemoveUserMessages();
}

void CalculateMinMaxAvg::Sample(float value, float weight)
{
  saturateMin(_min,value);
  saturateMax(_max,value);
  _sum += value*weight;
  _lastValue = value;
  _weight += weight;
}

void CalculateMinMaxAvg::Reset()
{
  _min = FLT_MAX;
  _max = FLT_MIN;
  _sum = 0;
  _weight = 0;
  _lastValue = 0;
}

CalculateMinMaxAvg::CalculateMinMaxAvg()
{
  Reset();
}


inline int toLargeIntSat(float x)
{
  if (x>=INT_MAX) return INT_MAX;
  if (x<INT_MIN) return INT_MIN;
  return toLargeInt(x);
}

void NetworkServer::ServerMessage(const char *text)
{
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  ConsoleF("%s", cc_cast(text));
#endif
//}
  GChatList.Add(CCSystem, NULL, text, false, true);
  RefArray<NetworkObject> dummy;
  GNetworkManager.Chat(CCSystem, "", dummy, text);
}

void NetworkServer::ServerMessage(const LocalizedFormatedString &text)
{
  RString localized = text.GetLocalizedValue();
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  ConsoleF("%s", cc_cast(localized));
#endif
//}
  GChatList.Add(CCSystem, NULL, localized, false, true);
  RefArray<NetworkObject> dummy;
  GNetworkManager.LocalizedChat(CCSystem, "", dummy, text);
}


//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER

#define DEBUG_TYPE_ENUM(type,prefix,XX) \
  XX(type, prefix, TotalSent) \
  XX(type, prefix, UserSent) \
  XX(type, prefix, Console) \
  XX(type, prefix, UserInfo) \
  XX(type, prefix, UserQueue) \
  XX(type, prefix, JIPQueue)

#ifndef DECL_ENUM_DEBUG_TYPE
#define DECL_ENUM_DEBUG_TYPE
DECL_ENUM(DebugType)
#endif
DECLARE_DEFINE_ENUM(DebugType,DT,DEBUG_TYPE_ENUM)

/*!
\patch 1.88 Date 10/21/2002 by Ondra
- Added: Dedicated server command #debug (userInfo)
(Requires 1.87 client)
\patch 1.90 Date 10/31/2002 by Ondra
- New: MP: Added #debug checkfile command.
Server admins can use this command to check if any file used by clients matches server version.
\patch 1.90 Date 10/31/2002 by Ondra
- New: MP: Added server.cfg array checkfiles[] -
list of files that should be checked for each player connecting.
Example: checkfiles[]={"HWTL\dta\data3d.pbo","dta\data3d.pbo"}
\patch 1.90 Date 10/31/2002 by Ondra
- New: Servers checks if world file (.wrp) used by client matches server version.
*/

static RString ExtractWord(RString &line)
{
  const char *endWord = strchr(line,' ');
  if (!endWord)
  {
    RString word = line;
    line = RString();
    return word;
  }
  RString word = line.Substring(0,endWord-line.Data());
  line = endWord+1;
  return word;
}

void NetworkServer::DebugAsk(RString str)
{
  // check for some special debug commands
  RString line = str;
  RString word = ExtractWord(line);
  if (!strcmpi(word,"limitOut"))
  {
    RString limit = ExtractWord(line);
    int limitBW = atoi(limit);
    if (limit>0 && _server)
    {
      int limitBWB = (limitBW+7)/8;
      _server->SetConnectionLimits(limitBWB);
      RString message = Format("Outgoing bandwidth limited to %d B/s (%d bps) per client",limitBWB,limitBWB*8);
      DebugAnswer(message);
    }
    return;
  }
  if (!strcmpi(word,"von"))
  {
    // check whether #debug von is not currently running
    if (_server && !GDebugVoNBank.IsRunning()) 
    {
      //P2P Topology
      QOStrStream diag;
      AutoArray<ConvPair, MemAllocLocal<ConvPair, 64> > convTable;
      convTable.Realloc(_identities.Size());
      convTable.Resize(_identities.Size());
      for (int i=0; i<_identities.Size(); i++)
      {
        convTable[i]._dpnid = _identities[i].dpnid;
        convTable[i]._id = _identities[i].GetDebugId();
        convTable[i]._name = _identities[i].name;
      }
      _server->GetVoNTopologyDiag(diag, convTable);
      GDebugVoNBank.SetFeature(DebugVoNBank::VoNFeature_NetTopology, RString(diag.str(), diag.pcount()) );
      // Ask each player for her collected VoNFeatures
      for (int i=0; i<_players.Size(); i++)
      {
        const NetworkPlayerInfo &player = *_players[i];
        NetworkCommandMessage ask;
        ask._type = NCMTAskVoNDiagnostics;
        SendMsg(player.dpid, &ask, NMFGuaranteed);
      }
      // Set VoNDiagnozer to wait on clients answers and report it after while
      GDebugVoNBank.StartCollecting(GlobalTickCount(), _players.Size());
    }
    return;
  }
  if (!strcmpi(word,"jipPerf"))
  {
    if (_server) 
    {
      //P2PManager diagnostics first
      RString initDiag = GetAddInitAndRemoveOverriddenStat();
      DebugAnswer(initDiag);
    }
    return;
  }
  #if _ENABLE_CHEATS
    if (!strcmpi(word,"exec"))
    {
      if (_server)
      {
        GameVarSpace vars(false);
        int gshandle = GGameState.BeginContext(&vars);
        GGameState.Execute(line, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        GGameState.EndContext(gshandle);
      }
    }
  #endif
  // check if string is only numbers
  char *endNum;
  float interval = strtod(str,&endNum);
  if (*endNum==0)
  {
    _debugInterval = interval;
    saturate(_debugInterval,0.1f,3600);
    BString<256> msg;
    sprintf(msg,"Debug: Interval %g",_debugInterval);
    DebugAnswer(RString(msg));
    return;
  }
  if (!stricmp(str,"off"))
  {
    // clear all debug info
    _debugOn.Clear();
    DebugAnswer(RString("Debug: All off"));
    //_debugInterval = 0;
    _debugNext = INT_MAX;
    return;
  }

  const char *beg = str;
  const char *end = strchr(beg, ' ');
  int len = end ? end - beg : strlen(beg);

  RString strCommand(beg, len);
  DebugType command = GetEnumValue<DebugType>((const char *)strCommand);
  if (command < 0) return;

  beg += len;
  while (*beg == ' ') beg++;

  if (stricmp(beg, "off") == 0)
  {
    _debugOn.DeleteKey(command);
    DebugAnswer(RString("Debug: ") + strCommand + RString(" off"));
  }
  else
  {
    _debugOn.AddUnique(command);
    DebugAnswer(RString("Debug: ") + strCommand);

    if (_debugInterval<=0)
    {
      _debugInterval = 10;
      _debugNext = _debugNext + toInt(_debugInterval*1000);
    }
    DWORD next = GlobalTickCount()+toInt(_debugInterval*1000);
    if (_debugNext>next) _debugNext = next;
  }
}

void NetworkServer::DebugAnswer(RString str)
{
  if (_gameMaster != AI_PLAYER || !_dedicated)
  {
    if (_dedicated)
    {
      NetworkCommandMessage msg;
      msg._type = NCMTDebugAnswer;
      msg._content.WriteString(str);
      SendMsg(_gameMaster, &msg, NMFGuaranteed);
    }
    LogDebugger("%s",cc_cast(str));
  }
}

/*!
\patch 5126 Date 1/31/2007 by Jirka
- Fixed: DS crash occurred when mission with some special name (containing '%') was selected
*/

int NetworkServer::ConsoleF(const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);

  if (_debugOn.FindKey(DTConsole) >= 0)
  {
    BString<512> buf;
    vsprintf(buf, format, arglist);
    DebugAnswer((const char *)buf);
  }
  int result = ::VConsoleF(format, arglist);
  
  va_end( arglist );
  return result;
}

#endif
//}

/*
const DWORD VotingTimeOutReady = 30000; // timeout in ms when all players are ready
const DWORD VotingTimeOutNotReady = 120000; // timeout in ms when no players are ready

const DWORD BriefingTimeOutReady = 15000; // timeout in ms when all players are ready
const DWORD BriefingTimeOutNotReady = 60000; // timeout in ms when no players are ready

const DWORD DebriefingTimeOutReady = 30000; // timeout in ms when all players are ready
const DWORD DebriefingTimeOutNotReady = 60000; // timeout in ms when no players are ready
*/

#include "Es/Common/langExt.hpp"
#define ENABLED_SHORTTIMEOUT_Bebul 1
#define ENABLED_SHORTTIMEOUT_Jirka 1
#if ENABLED_FEATURE(SHORTTIMEOUT)

const DWORD VotingTimeOutReady = 5000; // timeout in ms when all players are ready
const DWORD VotingTimeOutNotReady = 5000; // timeout in ms when no players are ready

#else

const DWORD VotingTimeOutReady = 60000; // timeout in ms when all players are ready
const DWORD VotingTimeOutNotReady = 60000; // timeout in ms when no players are ready

#endif

const DWORD BriefingTimeOutReady = 60000; // timeout in ms when all players are ready
const DWORD BriefingTimeOutNotReady = 60000; // timeout in ms when no players are ready

const DWORD DebriefingTimeOutReady = 45000; // timeout in ms when all players are ready
const DWORD DebriefingTimeOutNotReady = 45000; // timeout in ms when no players are ready

bool NetworkServer::CheckTimeOut(int readyCount, int totalCount, DWORD ready, DWORD notReady)
{
  // with low numbers assume there still will be more players connecting
  int minPlayersToVote = 8;
  float readyRatio;
  if (totalCount<minPlayersToVote)
  {
    readyRatio = float(readyCount)/float(minPlayersToVote);
  }
  else
  {
    readyRatio = float(readyCount)/float(totalCount);
  }
  DWORD timeToEnd = _serverStateTime+ready+toLargeInt((1-readyRatio)*(notReady-ready));
  if (GlobalTickCount()>=timeToEnd) return true;
  DWORD timeLeft = timeToEnd-GlobalTickCount();
  int secondsLeft = timeLeft/1000+1;
  
  if (_serverTimeOutSent!=secondsLeft)
  {
    // send to all players
    ServerTimeoutMessage msg;
    msg._timeout = secondsLeft;
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.clientState < NCSConnected) continue;
      SendMsg(info.dpid, &msg, NMFGuaranteed);
    }
    _serverTimeOutSent = secondsLeft;
  }

  #if _ENABLE_REPORT
    GlobalShowMessage(1000,"Time left: %d",secondsLeft);
  #endif
  return false;
}

RString NetworkServer::GetDiagText(const NetworkId &id) const
{
  NetworkObjectInfo *oInfo = GetObjectInfo(id);
  if (!oInfo) return RString();

  RString ret = Format(" Pos from %d",oInfo->currentUpdate[NMCUpdatePosition].from);
  if (oInfo->currentRejected[NMCUpdatePosition].message) ret = ret + Format(" Pos rejected from %d",oInfo->currentRejected[NMCUpdatePosition].from);
  if (oInfo->rejected[NMCUpdatePosition]) ret = ret + " Rejected";
  return ret;
}


/*!
\patch 1.52 Date 4/19/2002 by Jirka
- Fixed: When gamemaster disconnect from server during mission selection, server was in bad state.
\patch 1.79 Date 7/31/2002 by Jirka
- Improved: Mission on dedicated server launched if all roles are assigned and confirmed even if some players remain
\patch 1.82 Date 8/23/2002 by Ondra
- Fixed: MP: If server had predefined mission cycle,
mission sometimes started while first user was connecting.
\patch 5110 Date 12/21/2006 by Jirka
- Fixed: Dedicated server can use missions in addons now
\patch 5132 Date 2/23/2007 by Jirka
- Fixed: server config parameter persistent is fully working now
\patch 5133 Date 2/26/2007 by Jirka
- Fixed: MP - admin is deciding when briefing is finished now
*/

struct MissionParameter
{
  RString name;
  float   val;
  MissionParameter() {};
  MissionParameter(RString name1, float val1) : name(name1), val(val1) {};
  ClassIsMovableZeroed(MissionParameter);
};

void NetworkServer::SimulateDS()
{
  PROFILE_SCOPE_EX(nwDS, network)

  DWORD tickCount = GlobalTickCount();
  SimulateMonitor(tickCount);

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (!_dedicated) return;


  // persistent server with only one mission defined in server.cfg (class Missions) run the mission immediately without any players connected
  bool startPersistentMission = false;
  if (_persistent && _serverState!=NSSPlaying)
  {
    ConstParamEntryPtr entry = _serverCfg.FindEntry("Missions");
    int nEntries = entry ? entry->GetEntryCount() : 0;
    startPersistentMission = (nEntries >= 1);
  }

  if (_players.Size() <= 1 && !(_persistent && _serverState == NSSPlaying) && !startPersistentMission)
  {
    if (_serverState != NSSSelectingMission)
    {
      ConsoleF(LocalizeString(IDS_DS_NO_USERS));
      SetServerState(NSSSelectingMission);
      CleanUpMemory();
    }
    _missionIndex = -1;
    _mission = "";
    _restart = false;
    _reassign = false;
    // restart timeout
    _serverStateTime = GlobalTickCount();
  }
  else // Main ServerState traverse section [SelectingMission, AssigningPlayers, Playing, Aborting...]
  {
    static bool loadIsland = false;
    switch (_serverState)
    {
    case NSSSelectingMission:
      {
        RString t;
        // if _param1,2 are not defined in server config, use default
        _param1 = FLT_MAX;
        _param2 = FLT_MAX;
        AutoArray<MissionParameter> parameters; // secondary parameters
        if (_mission.GetLength() == 1 && _mission[0] == '?')
        {
          // voting in progress
          // check timeout, and react if necessary
          // check how many players already voted
          const Voting *miss = _votings.Find(NCMTMission);
          if (!miss) break;
          int total = _parent->GetIdentities()->Size();
          int ready = miss->Size();

          // if server has reached half of its capacity, consider it full
          if (CheckTimeOut(ready,total,VotingTimeOutReady,VotingTimeOutNotReady))
          {
            // try applying vote first
            if (!_votings.Apply(this,NCMTMission))
            {
              // no voting available - select a random mission
              AutoArray<MPMissionInfo> missionList;
              LoadMissions(false, missionList);
              int random = toIntFloor(GRandGen.RandomValue()*missionList.Size());
              saturate(random,0,missionList.Size()-1);
              const MPMissionInfo &info = missionList[random];
              _mission = info.mission+"."+info.world;
              // leave difficulty as it is
            }
          }
          
          break;
        }
        else if (_mission.GetLength() > 0)
        {
          // mission known - start it
          t = _mission;
          Glob.config.difficulty = _difficulty;

          _mission = "";
          _restart = false;
          _reassign = false;
        }
        else
        {
          // check if there is some mission list defined
          ConstParamEntryPtr entry = _serverCfg.FindEntry("Missions");
          int nEntries = entry ? entry->GetEntryCount() : 0;
          if (nEntries == 0)
          {
            // no mission list - check if there are enough players to start voting
            int voteMissionPlayers = 1;
            entry = _serverCfg.FindEntry("voteMissionPlayers");
            if (entry) voteMissionPlayers = *entry;
            if (_identities.Size() >= voteMissionPlayers)
            {
              // start voting
              _mission = "?";
              NetworkCommandMessage answer;
              answer._type = NCMTVoteMission;
              NetworkCommandMessage answer2;
              answer2._type = NCMTVoteMissionExt;
              AddMissionList(answer,answer2);
              for (int i=0; i<_identities.Size(); i++)
              {
                SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
                SendMsg(_identities[i].dpnid, &answer2, NMFGuaranteed);
            }
            }
            break;
          }

          // cycle through the mission list
          _missionIndex++;
          if (_missionIndex >= nEntries) _missionIndex = 0;
          ParamEntryVal item = entry->GetEntry(_missionIndex);

          t = item >> "template";

          Glob.config.difficulty = Glob.config.diffDefault;
          entry = item.FindEntry("difficulty");
          if (entry)
          {
            RString diffName = *entry;
            int difficulty = Glob.config.diffNames.GetValue(diffName);
            if (difficulty >= 0) Glob.config.difficulty = difficulty;
          }
          else
          {
            bool cadetMode = item >> "cadetMode";
            if (cadetMode)
            {
              int difficulty = Glob.config.diffNames.GetValue("Cadet");
              if (difficulty >= 0) Glob.config.difficulty = difficulty;
            }
            else
            {
              int difficulty = Glob.config.diffNames.GetValue("Veteran");
              if (difficulty >= 0) Glob.config.difficulty = difficulty;
            }
          }

          if (item.FindEntry("param1")) _param1 = item >> "param1";
          if (item.FindEntry("param2")) _param2 = item >> "param2";

          //read secondary parameters
          ConstParamEntryPtr paramsDesc = item.FindEntry("Params");
          if(paramsDesc && paramsDesc->IsClass()) for(int i=0; i< paramsDesc->GetEntryCount();i++ )
          {
            parameters.Add( MissionParameter( paramsDesc->GetEntry(i).GetName(), paramsDesc->GetEntry(i) ) );
          }
        }
        
        // mission selected, start it now

        CurrentMission = "";

        SetBaseDirectory(false, "");

        bool noCopy = false;
        RString owner;

        char mission[256]; strcpy(mission, t);
        char *world = strrchr(mission, '.');
        if (!world) return;
        *world = 0; world++;

#if _ENABLE_MISSION_CONFIG
        ConstParamEntryPtr entry = (Pars >> "CfgMissions" >> "MPMissions").FindEntry(mission);
        if (entry)
        {
          ::SelectMission(*entry);
          noCopy = true;
          owner = entry->GetOwnerName();
        }
        else
#endif
        {
          RString FindBriefingName(RString path);
          RString displayName = FindBriefingName(MPMissionsDir + t);

          if (QIFileFunctions::FileExists(MPMissionsDir + t + RString(".pbo")))
          {
            // mission in public bank
            CreateMission(mission, world);
            SetMission(world, "__cur_mp", MPMissionsDir);
            Glob.header.filenameReal = mission;

            ConsoleF(LocalizeString(IDS_DS_MISSION_READ_FROM_BANK), (const char *)displayName);
          }
          else if (QIFileFunctions::FileExists(MPMissionsDir + t + RString("\\mission.sqm")) ||
            QIFileFunctions::FileExists(MPMissionsDir + t + RString("\\mission.sqf")))
          {
            // mission in public directory
            CreateMission("", "");
            SetMission(world, mission, MPMissionsDir);

            ConsoleF(LocalizeString(IDS_DS_MISSION_READ_FROM_DIR), (const char *)displayName);
          }
          else break;
        }

        // load template
        if (!ParseMission(true, true)) break;

        // if parameters are not set in server config, use defaults
        if (_param1 == FLT_MAX)
        {
          _param1 = 0;
          ConstParamEntryPtr cfg = ExtParsMission.FindEntry("defValueParam1");
          if (cfg) _param1 = *cfg;
        }
        if (_param2 == FLT_MAX)
        {
          _param2 = 0;
          ConstParamEntryPtr cfg = ExtParsMission.FindEntry("defValueParam2");
          if (cfg) _param2 = *cfg;
        }
        _paramsArray.Clear();
        _paramsArray.Add(_param1);
        _paramsArray.Add(_param2);
        // secondary parameters
        ConstParamEntryPtr paramsDesc = ExtParsMission.FindEntry("Params");
        if(paramsDesc) for(int i=0; i< paramsDesc->GetEntryCount();i++ )
        {
          ParamEntryVal parameterDesc = paramsDesc->GetEntry(i);
          ConstParamEntryPtr entry = parameterDesc.FindEntry("default");
          _paramsArray.Add(entry ? *entry : 0);
          RString name = parameterDesc.GetName();
          for (int j=0; j<parameters.Size(); j++)
          {
            if (stricmp(name, parameters[j].name)==0)
            {
              _paramsArray[i+2]=parameters[j].val;
              break;
            }
          }
        }

        ConstParamEntryPtr cls = ExtParsMission.FindEntry("disableChannels");
        ResetDisabledChannels();
        if (cls && cls->IsArray())
        {
          for (int i=0; i<cls->GetSize(); i++)
          {
            int chan = toInt((float)(*cls)[i]);
            DisableChannel(chan);
          }
        }

        InitMission(Glob.config.difficulty, RString(), noCopy, owner, false);
        SetServerState(NSSAssigningPlayers);
      }
      break;
    case NSSAssigningPlayers:
      {
        _restart = false;

        // create game
        SetServerState(NSSSendingMission);
        SendMissionFile();

        ConsoleF(LocalizeString(IDS_DS_ROLES_ASSIGNED));

        // clear statistics now - ensure they will not be cleared after stats info from clients arrived
        GStats.ClearMission();
        SetServerState(NSSLoadingGame);
        
        loadIsland = true;
      }
      break;
    case NSSSendingMission:
      Fail("Temporary state");
      break;
    case NSSLoadingGame:
      if (loadIsland && GNetworkManager.GetClient()->GetClientState() == NCSMissionReceived)
      {
        loadIsland = false;

        ConsoleF(LocalizeString(IDS_DS_READING_MISSION));

        Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeNetware);

        GWorld->SwitchLandscape(Glob.header.worldname);

        //set parameters - after SwitchLandscape
        float param1 = 0, param2 = 0;
        GetNetworkManager().GetParams(param1, param2);
        GameState *gstate = GWorld->GetGameState();
        gstate->VarSet("param1", GameValue(param1), false, false, GWorld->GetMissionNamespace()); // mission namespace
        gstate->VarSet("param2", GameValue(param2), false, false, GWorld->GetMissionNamespace()); // mission namespace
        GNetworkManager.PublicVariable("param1", 0);
        GNetworkManager.PublicVariable("param2", 0);
        
        const MissionHeader *header = GetNetworkManager().GetMissionHeader();
        AutoArray<float> paramsArray = GetNetworkManager().GetParamsArray();
        GameValue valueAR = gstate->CreateGameValue(GameArray);
        GameArrayType &arrayAR = valueAR;
        arrayAR.Resize(0);
        for(int i=0; i< paramsArray.Size(); i++)
        {        
          if(header->_sizesParamArray[i]<=0) continue;
          arrayAR.Add(paramsArray[i]);
        }
        gstate->VarSet("paramsArray", arrayAR, false, false, GWorld->GetMissionNamespace()); // mission namespace
        GNetworkManager.PublicVariable("paramsArray", 0);

        CurrentTemplate->ActivateAddons(Glob.header.worldname);
        CurrentTemplate->InitGeneral(GModeNetware);
        bool error = !GWorld->InitVehicles(*CurrentTemplate);

        ProgressFinish(p);

        RString message = GetMaxErrorMessage();
        if (message.GetLength()>0)
        {
          ServerMessage(message);
          if (GetMaxError()>=EMError) // on error select another mission, let warning through
          {
            SetServerState(NSSSelectingMission);
            break;
          }
        }
        if (error)
        {
          ServerMessage(LocalizeString(IDS_DS_LOAD_MISSION_ERROR));
          SetServerState(NSSSelectingMission);
          break;
        }
        
        GNetworkManager.CreateAllObjects();
        // objects are created on client, so we can proceed after transfer from client to server completes

        void RunInitScript();
        RunInitScript();

        message = GetMaxErrorMessage();
        if (message.GetLength()>0)
        {
          ServerMessage(message);
          if (GetMaxError()>=EMError) // on error select another mission, let warning through
          {
            SetServerState(NSSSelectingMission);
            break;
          }
        }
        ConsoleF(LocalizeString(IDS_DS_MISSION_READ));
      }
      break;
    case NSSPlaying:
      if (_restart)
      {
        GNetworkManager.DestroyAllObjects();
        ConsoleF("Game restarted");
        SetServerState(NSSMissionAborted);
      }
      else if 
      (
        GWorld->GetEEndMode() != EMContinue &&
        (
          (!GWorld->GetCameraEffect() && !GWorld->GetTitleEffect()) ||
          GWorld->IsEndForced()
        )
      )
      {
        GNetworkManager.DestroyAllObjects();
        ConsoleF(LocalizeString(IDS_DS_GAME_FINISHED));
        SetServerState(NSSMissionAborted); // we need to wait on the end of mission on other clients
      }
      else
      {
        if (!_persistent)
        {
          // if the server is not persistent, check if some player is still playing the mission
          // if not, we want the mission to be terminated
          // see also World::CheckMissionEnd to termination on all players dead

          bool all = true;
          for (int i=0; i<_players.Size(); i++)
          {
            int player = _players[i]->dpid;

            NetworkClientState state = GetClientState(player);
            if (state >= NCSGameLoaded && state <= NCSGameFinished)
            {
              all = false;
              break;
            }
          }
          if (all)
          {
            GNetworkManager.DestroyAllObjects();
            ConsoleF(LocalizeString(IDS_DS_GAME_FINISHED));
            SetServerState(NSSMissionAborted);
            //debriefing = Glob.uiTime;
          }
        }
      }
      break;
    case NSSMissionAborted:
      {
        bool all = true;
        if (_gameMaster != AI_PLAYER)
        {
          NetworkClientState state = GetClientState(_gameMaster);
          all = state != NCSPlaying && state != NCSGameFinished;
        }
        else
        {
          int total = 0, ready = 0;
          for (int i=0; i<_players.Size(); i++)
          {
            NetworkClientState state = _players[i]->clientState;
            
            if (state>=NCSPlaying || state<=NCSMissionSelected)
            {
              total++;
              if (state>=NCSGameFinished)
              {
                ready++;
              }
            }
          }
          
          all = (
            ready>=total ||
            CheckTimeOut(ready,total,DebriefingTimeOutReady,DebriefingTimeOutNotReady)
          );
        }
        if (all)
        {
          // update statistics
          if (_restart && _reassign)
          {
            DoReassignDirect();
          }
          else if (_restart && _mission.GetLength() == 0)
          {
            SetServerState(NSSAssigningPlayers);
            loadIsland = true;
          }
          else
          {
            SetServerState(NSSSelectingMission);
            ConsoleF(LocalizeString(IDS_DS_WAITING_FOR_NEXT_GAME));
          }
        }
      }
      break;
    }
  }


  if (GDebugVoNBank.IsReportReady(tickCount))
  { // Report the #debug von collected info at last
    FILE *LogFile = GetNetworkManager().GetDedicatedServerLogFile();
    if ( LogFile )
    {
      AutoArray<ConvPair, MemAllocLocal<ConvPair, 64> > convTable;
      convTable.Realloc(_identities.Size());
      convTable.Resize(_identities.Size());
      for (int i=0; i<_identities.Size(); i++)
      {
        convTable[i]._dpnid = _identities[i].dpnid;
        convTable[i]._id = _identities[i].GetDebugId();
        convTable[i]._name = _identities[i].name;
      }
      fprintf(LogFile, "*** VoN Topology Diagnostics ((***\n");
      RString response = GDebugVoNBank.GetDebugString(convTable);
      GDebugVoNBank.StopCollecting(); // other client responses are too late
      GDebugVoNBank.Reset(); // next #debug von should not contain old data
      fwrite(cc_cast(response), 1, response.GetLength(), LogFile);
      fprintf(LogFile, "*** VoN Topology Diagnostics ))***\n");
      fflush(LogFile);
    }
  }

  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.clientState < NCSConnected) continue;
    if (info.motdIndex >= 0 && Glob.uiTime >= info.motdTime)
    {
      ChatMessage chat;
      chat._channel = CCGlobal;
      chat._text = _motd[info.motdIndex];
      chat._name = "";
      SendMsg(info.dpid, &chat, NMFGuaranteed);

      info.motdIndex++;
      if (info.motdIndex >= _motd.Size()) info.motdIndex = -1;
      else info.motdTime = Glob.uiTime + _motdInterval;
    }
  }

#endif // _ENABLE_DEDICATED_SERVER
//}
}


void NetworkServer::SimulateMonitor( DWORD tickCount )
{
#if _ENABLE_DEDICATED_SERVER
  _monitorFrames++;
  if (tickCount >= _monitorNext)
  {
    if (_gameMaster == AI_PLAYER && _dedicated)
    {
      _monitorInterval = 0;
    }
    else //if (_state == NGSPlay)
    {
      NetworkCommandMessage msg;
      msg._type = NCMTMonitorAnswer;
      float invInterval = 1.0 / (_monitorInterval + 0.001f * (tickCount - _monitorNext));
      float fps = _monitorFrames * invInterval;
      int memory = MemoryUsed();
      float in = _monitorIn * invInterval;
      float out = _monitorOut * invInterval;
      msg._content.Write(&fps, sizeof(fps));
      msg._content.Write(&memory, sizeof(memory));
      msg._content.Write(&in, sizeof(in));
      msg._content.Write(&out, sizeof(out));

      int sizeG = 0, sizeNG = 0;
      for (int i=0; i<_players.Size(); i++)
      {
        NetworkPlayerInfo &player = *_players[i];
        for (int j=0; j<player._messageQueue.Size(); j++)
          sizeG += player._messageQueue[j]->msg->size;
        for (int j=0; j<player._messageQueueNonGuaranteed.Size(); j++)
          sizeNG += player._messageQueueNonGuaranteed[j]->msg->size;
      }
      msg._content.Write(&sizeG, sizeof(sizeG));
      msg._content.Write(&sizeNG, sizeof(sizeNG));

      SendMsg(_gameMaster!=AI_PLAYER ? _gameMaster : BOT_CLIENT, &msg, NMFGuaranteed);
    }
    Monitor(_monitorInterval);
  }
#endif
}



#if PEER2PEER

void P2PServerProcess(int channel, P2PMsgType type, BYTE *data, int dataSize, void *context)
{
  if (type == P2PRequest)
  {
    if (dataSize < 1) return;
    PeerToPeerServer *p2p = (PeerToPeerServer*)context;
    Assert(p2p);
    switch (data[0]) // message type
    {
    case 0:
      // list of missions
      {
        QOStrStream out;
        out.put(0); // response type

        // TODO: caching for faster response
        AutoArray<SaveHeader> list;
        SaveHeaderAttributes filter;
        filter.Add("type", FindEnumName(STMission));
        filter.Add("player", Glob.header.GetPlayerName());
        GDebugger.PauseCheckingAlive();
        FindSaves(list, filter);
        GDebugger.ResumeCheckingAlive();
        for (int i=0; i<list.Size(); i++)
        {
          SaveHeaderAttributes &attributes = list[i].attributes;
          RString mission;
          if (attributes.Find("mission", mission))
          {
            out.write(mission, mission.GetLength() + 1);
          }
        }
        out.put(0); // end

        p2p->SendAnswer(channel, (BYTE *)out.str(), out.pcount());
      }
      break;
    case 1:
      // mission
      break;
    }
  }
}

#endif    // PEER2PEER

#if _ENABLE_CHEATS || _ENABLE_DEDICATED_SERVER

NetStatsGraph::NetStatsGraph()
{
  _lastTime = ::GlobalTickCount();
  _lastSize = 0;
  _lastMsg = 0;
}

void NetStatsGraph::OnSimulate(int size, int msg)
{
  DWORD now = ::GlobalTickCount();
  float deltaT = (now-_lastTime)*0.001f;
  if (deltaT>0)
  {
    // 8*000.1 convert from B/s to kbps
    PerformanceGraph<2>::Values val;
    val.values[0] = (size-_lastSize)*8*0.001f/deltaT;
    val.values[1] = (msg-_lastMsg)/deltaT;
    AddSample(val,deltaT);
  }
  _lastSize = size;
  _lastMsg = msg;
  _lastTime = now;

}

#endif

RefD<NetworkObjectInfo> ImplicitMapTraits< RefD<NetworkObjectInfo> >::zombie = (NetworkObjectInfo*)1;
RefD<NetworkObjectInfo> ImplicitMapTraits< RefD<NetworkObjectInfo> >::null;

/*!
\patch 1.79 Date 7/26/2002 by Jirka
- Added: Elimination of "disconnect cheat"
\patch 1.90 Date 10/30/2002 by Jirka
- Fixed: Message "Player ... is loosing connection" isn't write for server now
*/

RString NetworkServer::NetworkIdToNetIdFriendly(const NetworkId &id)
{
  RefD<NetworkObjectInfo> dInfo;
  if (_objects.get(id,dInfo))
  {
    NetworkMessageCreateVehicle *msg = dynamic_cast<NetworkMessageCreateVehicle *>(dInfo->create.message.GetRef());
    if (msg)
    {
      return Format("%s %s", cc_cast(NetworkIdToNetId(id)), cc_cast(msg->_type));
    }
    else
    {
      const char *name = "UNKNOWN";
      switch (dInfo->create.type)
      {
        case NMTCreateClientCameraPosition: name = "cameraPos"; break;
        case NMTCreateTurret: name = "turret"; break;
        case NMTCreateShot: name = "shot"; break;
      }
      return Format("%s %s", name, cc_cast(NetworkIdToNetId(id)));
    }
  }
  //! object not found?
  return NetworkIdToNetId(id);
}

void NetworkServer::OnSimulate()
{
  PROFILE_SCOPE_EX(nwSer, network)

#define DIAGNOSE_NETWORK_OBJECTS 0
#if DIAGNOSE_NETWORK_OBJECTS
  static volatile bool diagnoseObjects = false;
  if (diagnoseObjects)
  {
    RptF("*** NetworkServer _objects list");
    IteratorState it;
    RefD<NetworkObjectInfo> obj;
    if ( _objects.getFirst(it,obj) )
    {
      do
      {
        RptF("   %s",NetworkIdToNetIdFriendly(obj->id));
      }
      while ( _objects.getNext(it,obj) );
    }
    RptF("*** NetworkServer _objects list");
    diagnoseObjects = false; //only once
  }
#endif

  // raw statistics
#if _ENABLE_CHEATS
  // remove statistics info
  static AutoArray<int> texts;
  for (int i=0; i<texts.Size(); i++)
  {
    if (texts[i] >= 0) GEngine->RemoveText(texts[i]);
  }
  texts.Resize(0);

  if (outputDiags == ODMRawStats+NOutputDiags)
  {
    _rawStatisticsGraph.Resize(_rawStatistics.Size());
    // int y = 40;
    for (int i=0; i<_rawStatistics.Size(); i++)
    {
      char output[1024];
      NetworkPlayerInfo *info = GetPlayerInfo(_rawStatistics[i].player);
      if (!info) continue;
      sprintf
      (
        output, "To %s: sent %d (%d), received %d (%d)",
        (const char *)info->name,
        _rawStatistics[i].sizeSent, _rawStatistics[i].msgSent,
        _rawStatistics[i].sizeReceived, _rawStatistics[i].msgReceived
      );
      texts.Add(GEngine->ShowTextF(1000, output));
      //y += 25;

      if (!_rawStatisticsGraph[i]) _rawStatisticsGraph[i] = new NetStatsGraph;
      _rawStatisticsGraph[i]->OnSimulate(_rawStatistics[i].sizeSent,_rawStatistics[i].msgSent);
      GEngine->ShowDiagGraph(_rawStatisticsGraph[i]);

    }
  }
#endif

#define DIAG_NETWORK_OBJECTS 0
#if DIAG_NETWORK_OBJECTS
  static int diagnozeObjectId = 0;
  if (diagnozeObjectId)
  {
    for (int i=0; i<_objects.Size(); i++)
    {
      Object *obj = dynamic_cast<Object *>(GNetworkManager.GetObject(_objects[i]->id));
      if (obj && obj->GetObjectId().Encode()==diagnozeObjectId)
      {
        LogF( " DIAGNOZED ENTITY: %s, index in _objects=%d", obj->GetDebugName(), i );
        break;
      }
    }
    diagnozeObjectId = 0;
  }
  // find object diagnoze using her NetworkId
  static bool diagnozeNetworkId = false;
  static int diagnozeCreator = 0;
  static int diagnozeId = 0;
  if (diagnozeNetworkId)
  {
    NetworkId diagNetworkId(diagnozeCreator, diagnozeId);
    int index = -1;
    for (int i=0; i<_objects.Size(); i++)
    {
      if (_objects[i]->id==diagNetworkId) { index=i; break; }
    }
    NetworkObject *nObj = GNetworkManager.GetObject(diagNetworkId);
    if (nObj)
    {
      LogF( " DIAGNOZED ENTITY: %s, index in _objects=%d", nObj->GetDebugName(), index );
    }
    else
    {
      LogF( " DIAGNOZED ENTITY: *NOT FOUND*, index in _objects=%d", index );
    }
    diagnozeNetworkId = false;
  }
#endif

#define DIAG_SLOPE_SELECTION_SERVER_LAGGING 0
#if DIAG_SLOPE_SELECTION_SERVER_LAGGING
  // RptF all players networking information from time to time
  int currentTickCnt = ::GetTickCount();
  static DWORD lastDiagTickCount = currentTickCnt;
  const DWORD nextDiagDelay = 60000; //1 minute
  if ( lastDiagTickCount + nextDiagDelay < currentTickCnt )
  {
    lastDiagTickCount = currentTickCnt;
    RptF("*** Start: Players Networking Diagnostics ***");
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &pnet = _players[i];
      //if (pnet.dpid==BOT_CLIENT) continue;
      PlayerIdentity *id = FindIdentity(pnet.dpid);
      if (!id) continue;
      RptF("  %s bandwidth=<%d,%d,%d> ping=<%d,%d,%d> desync=%d", 
        cc_cast(id->name), 
        toLargeIntSat(pnet._bandwidth.GetMin()/(1024/8)), toLargeIntSat(pnet._bandwidth.GetMax()/(1024/8)), toLargeIntSat(pnet._bandwidth.GetAvg()/(1024/8)),
        toInt(pnet._ping.GetMin()), toInt(pnet._ping.GetMax()), toInt(pnet._ping.GetAvg()),
        toLargeIntSat(pnet._desync.GetAvg()*100)
      );
    }
    RptF("*** End: Players Networking Diagnostics ***");
  }
#endif

#if _ENABLE_DEDICATED_SERVER

  {
    DWORD now = GlobalTickCount();
    if (now >= _debugNext)
    {
      if (_gameMaster == AI_PLAYER && _dedicated || _debugInterval<0.001)
      {
        _debugNext = INT_MAX;
      }
      else
      {
        _debugNext = _debugNext + toInt(_debugInterval*1000);
      }

      float invAge = 1/_debugInterval;

      if (_debugOn.FindKey(DTUserSent) >= 0)
      {
        for (int i=0; i<_rawStatistics.Size(); i++)
        {
          BString<512> output;
          NetworkPlayerInfo *info = GetPlayerInfo(_rawStatistics[i].player);
          if (!info) continue;
          sprintf
          (
            output, "%s: sent %.0f bps (%.2f Msgps), received %.0f bps (%.2f Msgps)",
            (const char *)info->name,
            invAge * _rawStatistics[i].sizeSent *8, invAge * _rawStatistics[i].msgSent,
            invAge * _rawStatistics[i].sizeReceived *8, invAge * _rawStatistics[i].msgReceived
          );
          DebugAnswer((const char *)output);
        }
      }

      if (_debugOn.FindKey(DTTotalSent) >= 0)
      {
        int totalMsgSent = 0;
        int totalMsgReceived = 0;
        int totalSizeSent = 0;
        int totalSizeReceived = 0;
        for (int i=0; i<_rawStatistics.Size(); i++)
        {
          totalMsgSent += _rawStatistics[i].msgSent;
          totalMsgReceived += _rawStatistics[i].msgReceived;
          totalSizeSent += _rawStatistics[i].sizeSent;
          totalSizeReceived += _rawStatistics[i].sizeReceived;
        }
        BString<512> output;
        sprintf
        (
          output, "** Total: sent %.0f bps (%.2f Msgps), received %.0f bps (%.2f Msgps)",
          invAge * totalSizeSent * 8, invAge * totalMsgSent,
          invAge * totalSizeReceived * 8, invAge * totalMsgReceived
        );
        DebugAnswer((const char *)output);
      }

      _rawStatistics.Clear();

      if (_debugOn.FindKey(DTUserQueue) >= 0)
      {
        // send info about all players
        for (int i=0; i<_players.Size(); i++)
        {
          const NetworkPlayerInfo &player = *_players[i];
          int sizeG = 0, sizeNG = 0;
          for (int j=0; j<player._messageQueue.Size(); j++)
            sizeG += player._messageQueue[j]->msg->size;
          for (int j=0; j<player._messageQueueNonGuaranteed.Size(); j++)
            sizeNG += player._messageQueueNonGuaranteed[j]->msg->size;
          // print actual desync as well

          BString<512> output;
          sprintf
          (
            output, "%s: Queue %d B (%d) %d B Guaranteed (%d), Desync %.0f",
            (const char *)player.name,
            sizeNG, player._messageQueueNonGuaranteed.Size(), sizeG, player._messageQueue.Size(),
            player._desync.GetAvg()*100
          );
          DebugAnswer((const char *)output);
        }
      }
      if (_debugOn.FindKey(DTUserInfo) >= 0)
      {
        // send info about all players
        for (int i=0; i<_players.Size(); i++)
        {
          const NetworkPlayerInfo &player = *_players[i];

          RString stats = _server->GetStatistics(player.dpid);
          
          BString<512> output;
          sprintf
          (
            output, "%s: Info %s",
            (const char *)player.name, (const char *)stats
          );
          DebugAnswer((const char *)output);
        }
      }
      if (_debugOn.FindKey(DTJIPQueue) >= 0)
      {
        BString<512> output;
        sprintf(output, "Init %d",_initMessages.Size());
        DebugAnswer((const char *)output);

      }
    } // if (time for #debug)

  }
#endif

  // receive all system and user messages
  {
    PROFILE_SCOPE_EX(nwSRv, network)
    ReceiveSystemMessages();
    ReceiveLocalMessages();
    ReceiveUserMessages();
  }

#if _ENABLE_DEDICATED_SERVER
  if (_dedicated) ProcessQueuedExec();
#endif

  PerformIntegrityInvestigations();

  #if _ENABLE_MP
  // cancel all update messages
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &pInfo = *_players[i];
    // statistics
    if (DiagLevel >= 3)
    {
      int nMsg, nBytes, nMsgG, nBytesG;
      _server->GetSendQueueInfo(pInfo.dpid, nMsg, nBytes, nMsgG, nBytesG);
      if (nMsg > 0 || nMsgG > 0)
            {
                if ( nMsgG < 0 )        // old style
            DiagLogF("Server to %s: pending in SendQueue: %d messages, %d bytes", (const char *)pInfo.name, nMsg, nBytes);
                else                    // new style
            DiagLogF("Server to %s: pending in SendQueue: common - %d messages, %d bytes, guaranteed - %d messages, %d bytes",
                             (const char *)pInfo.name, nMsg, nBytes, nMsgG, nBytesG);
            }
    }
  }
  #endif

  DWORD tickCount = GlobalTickCount();
  if (GDebugVoNBank.IsReportReady(tickCount))
  { // Report the #debug von collected info at last
    AutoArray<ConvPair, MemAllocLocal<ConvPair, 64> > convTable;
    convTable.Realloc(_identities.Size());
    convTable.Resize(_identities.Size());
    for (int i=0; i<_identities.Size(); i++)
    {
      convTable[i]._dpnid = _identities[i].dpnid;
      convTable[i]._id = _identities[i].GetDebugId();
      convTable[i]._name = _identities[i].name;
    }
    FILE *LogFile = GetNetworkManager().GetDedicatedServerLogFile();
    if ( LogFile )
    {
      fprintf(LogFile, "*** VoN Topology Diagnostics ((***\n");
      RString response = GDebugVoNBank.GetDebugString(convTable);
      GDebugVoNBank.StopCollecting(); // other client responses are too late
      GDebugVoNBank.Reset(); // next #debug von should not contain old data
      fwrite(cc_cast(response), 1, response.GetLength(), LogFile);
      fprintf(LogFile, "*** VoN Topology Diagnostics ))***\n");
      fflush(LogFile);
    }
    else
    {
      // Set TimeStampFormat of LogF to none in order to make VoN Topology Diagnostics parser VoNDiag2FSM.exe working 
      TimeStampFormat backup = GTimeStampFormat;
      GTimeStampFormat = TSFNone;
      LogF("*** VoN Topology Diagnostics ((***");
      RString response = GDebugVoNBank.GetDebugString(convTable);
      GDebugVoNBank.StopCollecting(); // other client responses are too late
      GDebugVoNBank.Reset(); // next #debug von should not contain old data
      response = response + "*** VoN Topology Diagnostics ))***";
      LogF(response);
      GTimeStampFormat = backup;
    }
  }

  SimulateDS();

  // kick off players faded out
  CheckFadeOut();

  // destroy players kicked out
  int size = _identities.Size();
  for (int i=size-1; i>=0; i--)
  {
    PlayerIdentity &identity = _identities[i];
    if (identity.destroy && Glob.uiTime >= identity.destroyTime)
    {
      OnPlayerDestroy(identity.dpnid);
    }
  }

  // send update messages
  {
    PROFILE_SCOPE_EX(nwSSd, network)
    SendMessages();
  }

  // update _serverTime
  _serverTime = ::GetTickCount()-_serverTimeInitTickCount;
  if ( _serverTime > _nextServerTimeUpdate )
  {
    _nextServerTimeUpdate = _serverTime + ServerTimeUpdateInterval;
    // send fresh serverTime update to all clients
    ServerTimeMessage stmsg;
    _serverTime = ::GetTickCount()-_serverTimeInitTickCount; // update _serverTime
    stmsg._time = _serverTime;
    stmsg._endTime = _serverTimeOfEstimatedEnd;
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.clientState < NCSConnected) continue;
      SendMsg(info.dpid, &stmsg, NMFGuaranteed | NMFHighPriority);
    }
  }

  // calculate aggregate de-sync average
  {  
    float desyncTotal = 0;

    float avgDesync = 0;
    float maxDesync = 0;
    int nDesync = 0;
    for (int i=0; i<_players.Size(); i++)
    {
      const NetworkPlayerInfo &pnet = *_players[i];
      // bot client has no de-sync by definition
      if (pnet.dpid==BOT_CLIENT) continue;
      float value = pnet._desync.GetLastValue();
      avgDesync += value, nDesync ++;
      saturateMax(maxDesync,value);
    }
    if (nDesync>0)
    {
      avgDesync /= nDesync;
      // aggregate de-sync - consider both avg and max
      desyncTotal = avgDesync*0.7f+maxDesync*0.3f;
    }
    _desyncTotal.Sample(desyncTotal,1e-3);
  }

  
  const DWORD desyncTotalPeriod = 5000;
  if (GlobalTickCount()>_desyncTotalUpdateNext)
  {
    // maintain aggregate de-sync average
    _desyncTotalAvg = _desyncTotal.GetAvg();
    _desyncTotalMax = _desyncTotal.GetMax();
    _desyncTotal.Reset();
    _desyncTotalUpdateNext = GlobalTickCount()+desyncTotalPeriod;
  }

  // avoid flickering the icon
  // check de-sync information for all players
  _connectionDesync = CDGood;
  // show worst de-sync found for any player
  float aggDesync = _desyncTotalAvg;
  if (aggDesync>PlayerIdentity::DesyncLow/100)
  {
    if (aggDesync>PlayerIdentity::DesyncHigh/100)
    {
      _connectionDesync = CDHigh;
    }
    else
    {
      _connectionDesync = CDLow;
    }
  }
  
  tickCount = GlobalTickCount();
  if (tickCount >= _pingUpdateNext)
  {
    _pingUpdateNext = tickCount + 30000;
    
    // update all players information
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &pnet = *_players[i];
      //if (pnet.dpid==BOT_CLIENT) continue;
      PlayerIdentity *id = FindIdentity(pnet.dpid);
      if (!id) continue;
      id->_minBandwidth = toLargeIntSat(pnet._bandwidth.GetMin()/(1024/8));
      id->_maxBandwidth = toLargeIntSat(pnet._bandwidth.GetMax()/(1024/8));
      id->_avgBandwidth = toLargeIntSat(pnet._bandwidth.GetAvg()/(1024/8));
      id->_minPing = toInt(pnet._ping.GetMin());
      id->_maxPing = toInt(pnet._ping.GetMax());
      id->_avgPing = toInt(pnet._ping.GetAvg());
      id->_desync = toLargeIntSat(pnet._desync.GetAvg()*100);
      saturate(id->_desync,0,100000);
      pnet._ping.Reset();
      pnet._bandwidth.Reset();
      pnet._desync.Reset();
    }

    // broadcast messages about all players to all players
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.clientState < NCSConnected) continue;
      for (int i=0; i<_identities.Size(); i++)
      {
        PlayerIdentity &pi = _identities[i];

        NetworkMessageType type = pi.GetNMType(NMCUpdatePosition);

        Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
        msg->time = Glob.time;
        NetworkMessageContext ctx(msg, this, info.dpid, MSG_SEND);
        ctx.SetClass(NMCUpdatePosition);

        PREPARE_TRANSFER(PlayerUpdate)

        TMError err;
        int dpnid = pi.dpnid;
        err = TRANSF_BASE(dpnid, dpnid);
        if (err != TMOK) continue;
        err = pi.TransferMsg(ctx);
        if (err != TMOK) continue;

        // send message
        //NetworkComponent::
        SendMsg(info.dpid,msg,type,NMFNone, NULL);
      }
    }
  }

  // integrity and CDKey checks
  DWORD time = GlobalTickCount();
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    // CDKey check
    if (info.cdKeyCheck.timeout)
    {
      if (time > info.cdKeyCheck.timeout)
      {
        if (info.dpid == BOT_CLIENT)
        {
          RString errmsg = LocalizeString(IDS_MP_BAD_CDKEY);
          GChatList.Add(CCSystem, NULL, errmsg, false, true);
          WarningMessage(errmsg);
          Done();
        }
        else
        {
          KickOff(info.dpid, KORAuthTimeOut);
        }
        info.cdKeyCheck.timeout = 0; //clear
      }
    }
  }

  // squad checks
  bool canSendCustomFiles = true;
  bool timeout = false;
  for (int i=0; i<_squadChecks.Size();)
  {
    CheckSquadObject *obj = _squadChecks[i];
    if (i==0)
    { //first object can be timeouted in order others can connect too
      if (_squadChecksTimeout)
      {
        if (_squadChecksTimeout < ::GetTickCount()) //timeout
          timeout = true;
      }
      else if (obj->GetState()==CheckSquadObject::SendingCustomFiles)
      {
        _squadChecksTimeout = ::GetTickCount() + 30*1000; //30 seconds
      }
    }
    if (obj->IsDone(canSendCustomFiles, this))
    {
      PlayerIdentity &identity = obj->_identity;
      CreateIdentity(identity);
      _squadChecks.Delete(i);
      if (i==0) //reset timeout
      {
        _squadChecksTimeout = 0;
        timeout = false;
      }
    }
    else i++;
  }
  if (timeout)
  { 
    _squadChecksTimeout = 0;
    // shift the squad objects and place the first at the end
    int schSize = _squadChecks.Size();
    if (schSize>1)
    {
      SRef<CheckSquadObject> first = _squadChecks[0];
      for (int i=1; i<schSize; i++)   
      { //shift
        _squadChecks[i-1] = _squadChecks[i];
      }
      _squadChecks[schSize-1] = first;
    }
  }
  
  int timeleft = GetSessionTimeLeft();
  const DWORD minBetweenUpdates = 120000;
  DWORD now = GlobalTickCount();
  if (_timeLeftLastValue!=timeleft && now>_timeLeftLastTime+minBetweenUpdates)
  {
/*
    // do not update the session description regularly, server will ask us

    // time from time update matchmaking stats
    UpdateMatchmakingDescription(false);
*/

#if _GAMES_FOR_WINDOWS
    // set the properties and contexts for matchmaking
    // - TimeLeft
    GSaveSystem.SetProperty(PROPERTY_GAME_TIMELEFT, timeleft);
#endif
  }
  else if (_timeLeftLastValueBcast!=timeleft)
  {
    // if there is any timeleft estimation, update it
    // updating session description for broadcast session is very quick
    // no network traffic required
    UpdateSessionDescription();
    _timeLeftLastValueBcast = timeleft;
  }

//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY
  SimulateQR();
#endif
//}


  //DoAssert(CheckIntegrityOfPendingMessages());

  //DWORD tickCount = ::GlobalTickCount();
  

  // log all diagnostics
  WriteDiagOutput(true);

  // check for disconnected players
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.clientState != NCSPlaying) continue;
    if (info.dpid == BOT_CLIENT) continue;
    float age = _server->GetLastMsgAgeReliable(info.dpid);
    if (info.connectionProblemsReported)
    {
      info.connectionProblemsReported = age > 5.0f;
    }
    else if (age >= 10.0f)
    {
      LocalizedFormatedString message;
      message.SetFormat(LocalizedString::Stringtable,"STR_MP_CONNECTION_LOOSING");
      message.AddArg(LocalizedString::PlainText,info.name);
      ServerMessage(message);
      info.connectionProblemsReported = true;
    }
  }

#if _ENABLE_CHEATS
  if (_debugWindow)
  {
    _debugWindow->ResetContent();
    _objects.forEach([this](NetworkObjectInfo *oInfo)
    {
      if (!oInfo) return;
      NetworkId id = oInfo->id;
      int owner = oInfo->owner;
      RString name = GetPlayerName(owner);
      RString typeName;
      if (oInfo->currentUpdate[0].message.NotNull())
      {
        NetworkMessageType type = oInfo->currentUpdate[0].type;
        DoAssert(type >=0 && type < NMTN);
        typeName = GetMsgTypeName(type);
      }
      else typeName = "Unknown";
      BString<1024> text;
      sprintf
      (
        text, "%s %d:%" OID_FORMAT" - owner %s (%x)",
        (const char *)typeName, id.creator.CreatorInt(), id.id,
        (const char *)name, owner
      );
      _debugWindow->AddString(text);
    }
    );
  }
#endif

#if _VBS2 && _VBS2_LITE
// Check if there has been a hack attempt on player size limit
// if, there has been, we randomly select offsets and apply them to random players
if(_server)
{
  if(((_players.Size() << 3) > (_VBS2_MAX_PLAYERS << 3))&&(_memoryWrite == false)) 
  {
    _memoryWrite = true;
    _nextRanUpdate = GlobalTickCount() + (DWORD) GRandGen.Gauss((1000*30),(1000*30*2),(1000*30*3));
  }
  
  if(_memoryWrite && (GlobalTickCount()>_nextRanUpdate))
  {
    DWORD nextUpdate = (DWORD) GRandGen.Gauss((1000*30),(1000*30*2),(1000*30*3));
    _nextRanUpdate = GlobalTickCount() + nextUpdate;

    int playerIndex = GRandGen.Gauss(0,0.5,_players.Size());
    if(playerIndex >= 0 && playerIndex < _players.Size())
    {
      NetworkPlayerInfo &info = _players[playerIndex];
      NetworkId &id = info.person;
      if (!id.IsNull())
      {
        NetworkObjectInfo *oInfo = GetObjectInfo(id);
        if(GNetworkManager.GetClient())
        {
          NetworkObject *netObj = GNetworkManager.GetClient()->GetObject(oInfo->id);
          Entity *obj = dyn_cast<Entity>(netObj);
          if(obj)
          {
            // raise player 100 feet above the ground. 
            Matrix4 trans = obj->Transform();
            Vector3 pos = trans.Position();
            pos += Vector3(
              GRandGen.Gauss(-500.0f,0.0f,500.0f),
              GRandGen.Gauss(-1.0f,0.0f,1.0f),
              GRandGen.Gauss(-500.0f,0.0f,500.0f));
           
            trans.SetPosition(pos);
            obj->MoveNetAware(trans);
            obj->OnPositionChanged();
          }
        }
      }
    }
  }
}
#endif

  // TODO: remove (temporary set all to all)
  // Set voice transmission tables on Xbox
  if (_server)
  {
    for (int i=0; i<_players.Size(); i++)
    {
      // if player is not fully created, he might be unable to receive VoN messages
      // this is because VoNClient is created after MAGIC_CREATE_PLAYER message is sent
      // perhaps better solution could be to create VoN client before sending the message
      // Current solution with player state gives players also some time
      // to establish and report peer to peer communication
      if (_players[i]->clientState<NCSLoggedIn && _players[i]->dpid!=BOT_CLIENT) continue;
      // set all other players as recipients
      int fromID = _players[i]->dpid;
      AUTO_STATIC_ARRAY(int, players, 32);
      for (int j=0; j<_players.Size(); j++)
      {
        if (_players[j]->clientState<NCSLoggedIn && _players[j]->dpid!=BOT_CLIENT) continue;
        int toID = _players[j]->dpid;
        if (toID != fromID) players.Add(toID);
#if HEAR_YOURSELF
        else if (hearYourself) players.Add(toID);
#endif
      }
      _server->SetTransmitTargets(fromID, players);
    }
    _server->UpdateTransmitTargets();
  }

#if _USE_BATTL_EYE_SERVER
  if (!_beIntegration.Run(this))
  {
    // reinitialization after auto-update failed, need to shut-down the server
    Done();
  }
#if 0
  // simple test of ExecuteCommand functioinality
  static volatile int doIt = -1; //set it to 1000 or something and wait
  if (doIt>0) doIt--;
  else if (!doIt)
  {
    doIt--;
    BattlEye::ExecuteCommand("#restart");
  }
#endif
#endif

  // check if characters loading is not finished
  for (int i=0; i<_players.Size(); i++)
  {
    _players[i]->DBCheckLoaded();
  }

}

void NetworkServer::OnSimulateDone()
{
}

void NetworkServer::FastUpdate()
{
//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY
  SimulateQR();
#endif
//}
}

void NetworkServer::OnDraw()
{
  static bool lagDiags = false;
    
  if (GInput.GetCheatXToDo(CXTLag))
  {
    lagDiags = !lagDiags;
    GlobalShowMessage(500,"Lag diags %s",lagDiags ? "On":"Off");
  }
  if (lagDiags)
  {
    static float minLagToShow = 1.0f;
    static float badLag = 100.0;
    float x = 0.05f;
    float y = 0.08f;
    float width = 0.80f;
    Font *font=GEngine->GetDebugFont();
    float size = 0.02;
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &pnet = *_players[i];
      float maxLag = pnet._desync.GetMax();
      if (maxLag<minLagToShow) continue;
      float lag = pnet._desync.GetLastValue();
      // lag 100 is usually considered very bad
      float lagRelative = floatMin(lag/badLag,1);
      PackedColor color(0x40,0x40,0,0x60);
      PackedColor nameColor(0xff,0xff,0xff,0xc0);
      // draw player name and lag indication
      Rect2DFloat rect;
      rect.x = x;
      rect.y = y;
      rect.w = lagRelative*width;
      rect.h = size;
      Rect2DAbs rect2D;
      GEngine->Convert(rect2D,rect);
      Point2DAbs pos(rect2D.x,rect2D.y);
      MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      GEngine->Draw2D(mip,color,rect2D);
      
      y+= size; // proceed to a next line
      GEngine->DrawTextF(
        pos,size,font,nameColor,0,"%s - %g, max %g, avg %g",
        cc_cast(pnet.name),lag,pnet._desync.GetMax(),pnet._desync.GetAvg()
      );
    }
  }
}

DEFINE_FAST_ALLOCATOR(NetworkObjectInfo)

/*!
\patch 1.47 Date 3/7/2002 by Ondra
- New: Player ID now displayed in server console when player is connected.
\patch 1.56 Date 5/13/2002 by Ondra
- New: When player is kicked off or banned, specific message is shown.
\patch 5153 Date 4/17/2007 by Jirka
- Fixed: When failed to join to a locked server, the correct cause is shown now.
*/

void NetworkServer::OnCreatePlayer(int player, bool privateSlot, const char *name, unsigned long inaddr)
{
  if (_sessionLocked)
  {
    // session is locked
    _server->KickOff(player, NTRSessionLocked);
    return;
  }

  OnPlayerCreate(player, privateSlot, name, inaddr);
}

DummyInput GDummyInput;

InputInterface *NetworkServer::GetInputInterface(int player)
{
  NetworkPlayerInfo *info = GetPlayerInfo(player);
  if (!info) return &GDummyInput;
  return info->_clientInput;
}

bool NetworkServer::OnCreateVoicePlayer(int player)
{
  NetworkPlayerInfo *info = GetPlayerInfo(player);
  if (!info) return false;
  info->dvid = player;
  return true;
}

void NetworkServer::GetPlayers(AutoArray<NetPlayerInfo> &players)
{
  players.Resize(0);
  for (int i=0; i<_players.Size(); i++)
  {
    int index = players.Add();
    players[index].dpid = _players[i]->dpid;
    players[index].name = _players[i]->name;
    players[index].person = _parent->GetClient()->GetObject(_players[i]->person);
  }
}

void NetworkServer::UseItem(Person *person, InventoryItem *item)
{
  const NetworkObjectInfo *info = GetObjectInfo(person->GetNetworkId());
  if (!info) return;

  UseItemMessage msg;
  msg._person = person;
  msg._item = item;
  SendMsg(info->owner, &msg, NMFGuaranteed);
}

bool NetworkServer::CheckPlayerInLobby(const NetworkPlayerInfo &info) const
{
  if (info.clientState==NCSPlaying)
  {
    // in-game - check if alive
    // this may be difficult, as we are server, and should not have any access to a game world?
    // find a network object describing the players unit
    const NetworkObjectInfo *obj = GetObjectInfo(info.person);
    // if the unit is NULL, session was most likely already terminated on the server
    if (!obj) return true;
    const NetworkCurrentInfo &curInfo = obj->currentUpdate[NMCUpdateGeneric];
    // HOTFIX: probably possible in some rare conditions (crash occurred)
    if (!curInfo.message) return false;
    // unless proven otherwise, assume alive
  }
  else
  {
    // not in-game - force lobby
    return true;
  }
  return false;
}

void NetworkServer::GetPlayersOnChannel(AutoArray<int, MemAllocSA> &players, AutoArray<NetworkId> units, DWORD from, bool voice)
{
  players.Resize(0);
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.dpid == from) continue;
    if (info.clientState < NCSConnected) continue;
    for (int j=0; j<units.Size(); j++)
    {
      if (units[j] == info.person)
      {
        if (voice)
        {
          if (info.dvid != 0) players.Add(info.dvid);
        }
        else
          players.Add(info.dpid);
        break;
      }
    }
  }
}

/*!
\patch 5198 Date 12/12/2007 by Ondra
- Fixed: Dead players can no longer talk or write to in-game players unless they are admins.
*/
void NetworkServer::GetPlayersOnChannel(AutoArray<int, MemAllocSA> &players, int channel, DWORD from, bool voice)
{
  players.Resize(0);
  switch (channel)
  {
  case CCNone:
  case CCDirect:
    return;
  case CCSystem:
  case CCGlobal:
    for (int i=0; i<_players.Size(); i++)
    {
      const NetworkPlayerInfo &info = *_players[i];
      if (info.dpid == from) continue;
      if (info.clientState < NCSConnected) continue;
      if (voice)
      {
        if (info.dvid != 0) players.Add(info.dvid);
      }
      else
        players.Add(info.dpid);
    }
    break;
  case CCVehicle:
    // TODO: remove in DayZ?
    break;
  }
  if (channel>=CCCustom1 && channel<=CCCustomLast)
  {
    for (int i=0; i<_players.Size(); i++)
    {
      const NetworkPlayerInfo &info = *_players[i];
      if (info.dpid == from) continue;
      if (info.clientState < NCSConnected) continue;
      CustomChannelInfo &ccinfo = GCustomChannels.GetCustomChannelInfo(channel);
      for (int j=0; j<ccinfo.units.Size(); j++)
      {
        if (info.person==ccinfo.units[j]->GetNetworkId())
        {
          if (voice)
          {
            if (info.dvid != 0) players.Add(info.dvid);
          }
          else
            players.Add(info.dpid);
        }
      }
    }
  }
}

void NetworkServer::InitSignatureCollecting(NetworkPlayerInfo *info)
{
  // last DataSignatureAnswer with index CollectEndIndex finishes it
  info->hashes.StartCollecting();
#if 0
  // do not send CollectEndIndex packet, as the collecting is run asynchronously on client
  // the client itself should send the answer after the collecting is over
  DataSignatureAskMessage collectEndMsg;
  collectEndMsg._index = CollectedHashes::CollectEndIndex;
  collectEndMsg._level = 0;
  SendMsg(info->dpid, &collectEndMsg, NMFGuaranteed);
#endif
  // add the question to the list of pending questions
  info->signatureChecks.Add(CollectedHashes::CollectEndIndex);
}

RString CollectedHashes::RandomUsedFile(Vector3Par pos, int level)
{
  const int CandidatesMaxCount = 5;
  AutoArray<RandomUsedFileCandidate, MemAllocLocal<RandomUsedFileCandidate, CandidatesMaxCount> > candidates;
  for (int i=0; i<10; i++) //max 10 tries to get five non-empty candidates
  {
    RString file = GWorld->RandomUsedFile(pos);
    if (file.IsEmpty()) continue;
    QFBank *bank = QFBankQueryFunctions::AutoBank(file);
    if (bank)
    {
      RString prefix = bank->GetPrefix();
      // avoid duplicate banks in candidates
      bool duplicate = false;
      for (int j=0; j<candidates.Size(); j++)
      {
        if (candidates[j].prefix==prefix) { duplicate=true; break; }
      }
      if (duplicate) continue;
      int ix=candidates.Add(RandomUsedFileCandidate(file, prefix));
      if ( ix==(CandidatesMaxCount-1) ) break; // all candidates collected
    }
  }
  if (candidates.Size()>0)
  {
    int bestIx = 0; int bestAsksCount = INT_MAX;
    for (int i=0; i<candidates.Size(); i++)
    {
      RandomUsedFileCandidate &newCandidate = candidates[i];
      RandomUsedFileCandidate &storedCandidate = _candidatesMap[newCandidate.prefix];
      if (_candidatesMap.IsNull(storedCandidate))
      { // add it
        _candidatesMap.Add(newCandidate);
        bestIx = i;
        bestAsksCount = 0;
        break; //further search is pointless
      }
      else
      { // already exists
        if ( storedCandidate.levelAsksCount[level] < bestAsksCount )
        { // update best
          bestIx = i;
          bestAsksCount = storedCandidate.levelAsksCount[level];
        }
      }
    }
    // increase the level used count in stored hash table
    RandomUsedFileCandidate &bestStoredCandidate = _candidatesMap[candidates[bestIx].prefix];
    if (!_candidatesMap.IsNull(bestStoredCandidate))
    {
      bestStoredCandidate.levelAsksCount[level]++;
    }
    return candidates[bestIx].filename;
  }
  return RString(); //no random file found (hopefully rare situation)
}

/*!
\patch 1.12 Date 08/08/2001 by Jirka
- Changed: squad validation on server is now processed asynchronously
\patch 1.22 Date 09/04/2001 by Jirka
- Changed: Messages "Player connected", "Player disconnected" are broadcasts over network
\patch 1.27 Date 10/17/2001 by Jirka
- Fixed: Player identity is not registered if player disconnect meanwhile.
 This caused un-kickable ghost players appearing in player list.
\patch 1.34 Date 12/6/2001 by Jirka
- Fixed: squad logos on dedicated server game
\patch_internal 1.46 Date 3/4/2002 by Jirka
- Changed: random integrity checks disabled (does not work fine) 
*/

void NetworkServer::CreateIdentity(PlayerIdentity &ident)
{
  NetworkPlayerInfo *info = GetPlayerInfo(ident.dpnid);
  // check if player is still connected
  if (!info)
  {
    // player already disconnected
    return;
  }

  #if _VERIFY_KEY && !INTERNAL_KEYS
    // verify the response to _steamServerId  is correct 
    if (SecureIdRequired()>0)
    {
      if (ident.blockedKeys.Size()>0 && VerifySignedKeys(ident.blockedKeys,ident.blockedKeysSignature))
      {
        // if there is any problem with the blocked key list, just ignore it
        // synchronize server version of the list
        // use always the longer one, as that is the more complete
        UpdateBanListSecure(ident.blockedKeys,ident.blockedKeysSignature);
      }

      RString message = "Player ID not registered";
      SignValidationResult signOK = SignUnknownError;
      if (ident.certificate.Size()>0)
      {
        signOK = VerifySignedData(ident.id,info->certChallenge,ident.challengeSigned,ident.certificate);
        switch (signOK)
        {
          case SignOK: break;
          case SignBlockedUser: message = "CD Key is blocked";break;
          case SignBadUserName: message = "Player ID not verified: Certificate belongs to a different user";break;
          case SignBadSignature: message = "Player ID not verified: Bad signature";break;
          case SignBadCertificate: message = "Player ID not verified: Certificate not accepted";break;
          default: message = "Player ID not verified: Unknown error";break;
        }
      }
      if (signOK!=SignOK)
      {
        if (SecureIdRequired()>=2)
        {
          // cheat - do not let him connect
          KickOff(info->dpid,KORBadCDKey,message);
          return;
        }
        else
        {
          // forget everything related to playerID verification, so that it is obvious the player was not verified
          ident.certificate.Clear();
          ident.challengeSigned.Clear();
          info->certChallenge = 0;
          RefArray<NetworkObject> dummy;
          GNetworkManager.Chat(CCSystem, ident.name, dummy, message);
          GChatList.Add(CCSystem, ident.name, message, false, true);
          #if _ENABLE_DEDICATED_SERVER
            if (_dedicated)
            {
              ConsoleF("%s:%s %s", cc_cast(ident.name), cc_cast(ident.id), cc_cast(message));
            }
          #endif
        }
      }
    }
  #endif

  int index = _identities.Add(ident);
  PlayerIdentity &identity = _identities[index];
  identity.playerid = NextPlayerId();


#if _VERIFY_KEY
  // check for double id
  if (identity.ValidateID())
  {
    // check only when id!=0
    // for id==0 we have better handling
    // id==999 is used in Multiplayer Demo
    // DEV1,DEV2,...,DEV999 are used in internal versions with INTERNAL_KEYS
    for (int i=0; i<_identities.Size(); i++)
    {
      if (i == index) continue;

      if (IsUserIdMatch(_identities[i].id,identity.id))
      {
        OnDoubleIdDetected(identity, _identities[i]);
        // check if player was kicked out
        if (identity.destroy) return;
        break;
      }
    }
  }
#endif

#if _ENABLE_GAMESPY && _VERIFY_CLIENT_KEY
  if ( _validateCDKey && identity.dpnid!=BOT_CLIENT )
  {
    #if _ENABLE_CHEATS // internal clients connected to internal exe perform no GS key validation
      static const char devPrefix[] = "DEV";
      if (strncmp(identity.id,devPrefix,strlen(devPrefix)))
    #endif
    {
    char buffer[9];
    for (int i=0; i<lenof(buffer)-1; i++) buffer[i] = 'a' + rand() % 26;
    buffer[lenof(buffer) - 1] = 0;
    info->challenge = buffer;
    // initial CD Key test
    CDKeyCheckAskMessage ask;
    ask._challenge = info->challenge;
    SendMsg(identity.dpnid, &ask, NMFGuaranteed);
    //increased to 90 seconds after problems with CD-key authentication 15.6.2012
    info->cdKeyCheck.timeout = GlobalTickCount()+90000; //90 seconds
  }
  }
#endif

  if (identity.dpnid == BOT_CLIENT)
    identity._rights |= PRServer;
  else
  {
    if (_verifySignatures)
    {
      if (_sigVerRequired<2)
      {
        // initial test of signatures
        DataSignatureAskMessage ask;
        ask._index = -2;
        ask._level = 0; // shallow check
        SendMsg(identity.dpnid, &ask, NMFGuaranteed);
        InitSignatureCollecting(info);
      }
      else
      {
        // initial test using level 2 check
        DataSignatureAskMessage askLev2;
        askLev2._index = -2;
        askLev2._level = 2; //HashOf(nonDataHash+fileListHash+pboPrefix) is used
        SendMsg(identity.dpnid, &askLev2, NMFGuaranteed);
        InitSignatureCollecting(info);
      }
    }


    sockaddr_in address;
    if (GetClientAddress(identity.dpnid, address))
    {
      identity._voiceAddress = address;
      //ConnectVoice(identity.dpnid,address);
    }
  }

#if LOG_PLAYERS
LogF
(
  "Server: Identity added - name %s, id %d (total %d identities, %d players)",
  (const char *)ident.name, ident.dpnid, _identities.Size(), _players.Size()
);
#endif

  identity.name = info->name;
  identity.clientState = NCSConnected;
#if _GAMES_FOR_WINDOWS
  identity.privateSlot = info->privateSlot;
#endif

  // Send list of current players which custom files should be removed (as new one will be sent)
  DeleteCustomFilesMessage dcfMsg;
  for (int i=0; i<_identities.Size(); i++)
  {
    if (i != index) dcfMsg._list.Add(_identities[i].name);
  }
  if (dcfMsg._list.Size()) SendMsg(identity.dpnid, &dcfMsg, NMFGuaranteed);

  for (int i=0; i<_identities.Size(); i++)
  {
    PlayerIdentity &player = _identities[i];
    if (i != index)
    {
      // send all faces and sounds to new player
      {
        if (stricmp(player.face, "custom") == 0)
          TransferFace(identity.dpnid, player.name);
        TransferCustomRadio(identity.dpnid, player.name);
      }

		// send all identities to new player
      SendMsg(identity.dpnid, &player, NMFGuaranteed);

    }
    // send new identity to all players
    SendMsg(player.dpnid, &identity, NMFGuaranteed);
  }

#if _ENABLE_MP
  // send mute list to a new player
  SendMuteList(identity);
#endif

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (_dedicated)
  {
    NetworkClient *client = _parent->GetClient();
    DoAssert(client && client->GetPlayer() != identity.dpnid);
    // send new identity to bot client
    SendMsg(client->GetPlayer(), &identity, NMFGuaranteed);
  }
#endif
//}

/*
  info->state = NGSLogin;

  // all message formats registered, we may send messages
  // send state dependent info 
  if (_state >= NGSPrepareSide)
  {
    // send mission info
    SendMissionInfo(info->dpid);
    info->state = NGSPrepareSide;
    info->missionFileValid = false;
  }
  // send current state
  ChangeGameState gs(_state);
  SendMsg(info->dpid, &gs, NMFGuaranteed);

  // if game is already in progress, send how long is played already
  if (_state == NGSPlay)
  {
    NetworkCommandMessage msg;
    msg.type = NCMTMissionTimeElapsed;
    int timeElapsed = GlobalTickCount() - _missionHeader.start;
    msg.content.Write(&timeElapsed, sizeof(timeElapsed));
    SendMsg(info->dpid, &msg, NMFGuaranteed);
  }

  identity.state = info->state;

  // send player state to all clients
  SetPlayerState(info->dpid, info->state);
*/

  // Send server state to client
  ServerStateMessage msg;
  msg._serverState = _serverState;
  SendMsg(identity.dpnid, &msg, NMFGuaranteed);

  identity._minPing = 10;
  identity._avgPing = 100;
  identity._maxPing = 1000;

  identity._minBandwidth = 14;
  identity._avgBandwidth = 28;
  identity._maxBandwidth = 33;

  // Send chat message about player is connected
  if (identity.dpnid != BOT_CLIENT)
  {
/*
    // commented out by rocket, not desired for dayz. possibly use for admins only
    LocalizedFormatedString message;
    message.SetFormat(LocalizedString::Stringtable,"STR_MP_CONNECT");
    message.AddArg(LocalizedString::PlainText,identity.name);
    RefArray<NetworkObject> dummy;
    if (!ChannelDisabled(CCSystem))
      GNetworkManager.LocalizedChat(CCSystem, "", dummy, message);
    
    GChatList.Add(CCSystem, NULL, message.GetLocalizedValue(), false, true);

    // make sure ping information will be refreshed soon
    DWORD updatePingTime = GlobalTickCount() + 5000;
    if (_pingUpdateNext>updatePingTime) _pingUpdateNext = updatePingTime;
*/
    #if _ENABLE_DEDICATED_SERVER
      if (_dedicated)
      {
        ConsoleF(LocalizeString(IDS_DS_PLAYER_CONNECTED),cc_cast(identity.name), cc_cast(identity.id));
      }
    #endif
  }
  UpdateMatchmakingDescription(false);

#if _ENABLE_DEDICATED_SERVER
  if (_serverState == NSSSelectingMission && _mission.GetLength() == 1 && _mission[0] == '?')
  {
    NetworkCommandMessage answer;
    answer._type = NCMTVoteMission;
    NetworkCommandMessage answer2;
    answer2._type = NCMTVoteMissionExt;
    AddMissionList(answer,answer2);
    for (int i=0; i<_identities.Size(); i++)
    {
      SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
      SendMsg(_identities[i].dpnid, &answer2, NMFGuaranteed);
  }
  }
#endif

#if _ENABLE_DEDICATED_SERVER
  // event handler
  OnEvent(SEOnUserConnected, identity.playerid);
#endif
}

#ifndef _WIN32
extern bool interrupted;
#endif


static bool CheckValidUpload(RString path, RString name)
{
  RString prefixShouldBe = GetServerTmpDir() + RString("\\players\\") + EncodeFileName(name) + RString("\\");
  return !strnicmp(path,prefixShouldBe,prefixShouldBe.GetLength());
}

static RString GetRelUploadPath(RString path, RString name)
{
  RString prefixShouldBe = GetServerTmpDir() + RString("\\players\\") + EncodeFileName(name) + RString("\\");
  if (strnicmp(path,prefixShouldBe,prefixShouldBe.GetLength())) return path;
  return path.Substring(prefixShouldBe.GetLength(),INT_MAX);
}

DWORD WINAPI AsyncDBStorage::DBSaveThreadFunction(void *ctx)
{
  Ref<SaveThreadContext> context = (SaveThreadContext *)ctx;
  context->DecRef();
  
  // wait until previous requests end
  Ref<SaveThreadContext> prev = context->_prevContext;
  if (prev)
  {
    prev->_thread.Wait();
    context->_prevContext = NULL; // no longer needed
  }

  // send save request
  RString result;
  int err = GetResponse(EHTTPREQ_POST,result,context->_url,context->_uid,context->_request);
  if (err >= 400)
  {
    LogF("HTTP Error: %d", err);
    LogF(result);
  }

  return 0;
}

DWORD WINAPI AsyncDBStorage::DBQueueThreadFunction(void *ctx)
{
	Ref<SaveThreadContext> context = (SaveThreadContext *)ctx;
	context->DecRef();

	// wait until previous requests end
	Ref<SaveThreadContext> prev = context->_prevContext;
	if (prev)
	{
		prev->_thread.Wait();
		context->_prevContext = NULL; // no longer needed
	}

	// send save request
	RString result;
	int err = GetResponse(EHTTPREQ_POST,result,context->_url,context->_uid,context->_request);
	if (err >= 400)
	{
		LogF("HTTP Error: %d", err);
		LogF(result);
	}

	return 0;
}

DWORD WINAPI AsyncDBStorage::DBKillThreadFunction(void *ctx)
{
	Ref<SaveThreadContext> context = (SaveThreadContext *)ctx;
	context->DecRef();

	// wait until previous requests end
	Ref<SaveThreadContext> prev = context->_prevContext;
	if (prev)
	{
		prev->_thread.Wait();
		context->_prevContext = NULL; // no longer needed
	}

	// send save request
	RString result;

	int err = GetResponse(EHTTPREQ_POST,result, context->_url,context->_uid, context->_request);
	if (err >= 400)
	{
		LogF("HTTP Error: %d", err);
		LogF(result);
	}

	return 0;
}

DWORD WINAPI AsyncDBStorage::DBItemDropThreadFunction(void *ctx)
{
	Ref<SaveThreadContext> context = (SaveThreadContext *)ctx;
	context->DecRef();

	// wait until previous requests end
	Ref<SaveThreadContext> prev = context->_prevContext;
	if (prev)
	{
		prev->_thread.Wait();
		context->_prevContext = NULL; // no longer needed
	}

	// send save request
	RString result;

	int err = GetResponse(EHTTPREQ_POST,result, context->_url, NULL, context->_request);
	if (err >= 400)
	{
		LogF("HTTP Error: %d", err);
		LogF(result);
	}

	return 0;
}

bool AsyncDBStorage::DBSave(RString baseURL, RString playerUID, RString request)
{
  SaveThreadContext *ctx = new SaveThreadContext();
  ctx->AddRef(); // player can disconnect while Thread still need access to its context

  ctx->_url = baseURL;
  ctx->_uid = Format("?uid=%s", cc_cast(playerUID));
  ctx->_request = request;
  ctx->_prevContext = _lastContext;
  _lastContext = ctx;
#if USE_MT_LIB_FOR_JOBS
    ctx->_thread.Init(CreateMTThreadOnCPU(64 * 1024, DBSaveThreadFunction, ctx, 2, MultiThread::ThreadBase::PriorityNormal, "DBSaveCharacter"));
#else
    ctx->_thread.Init(CreateThreadOnCPU(64 * 1024, DBSaveThreadFunction, ctx, 2, THREAD_PRIORITY_NORMAL, "DBSaveCharacter"));
#endif
  return true;
}

bool AsyncDBStorage::DBQueue(RString baseURL, RString playerUID, RString request)
{
	SaveThreadContext *ctx = new SaveThreadContext();
	ctx->AddRef(); // player can disconnect while Thread still need access to its context

	ctx->_url = baseURL;
	ctx->_uid = playerUID;
	ctx->_request = request;
	ctx->_prevContext = _lastContext;
	_lastContext = ctx;
#if USE_MT_LIB_FOR_JOBS
	ctx->_thread.Init(CreateMTThreadOnCPU(64 * 1024, DBQueueThreadFunction, ctx, 2, MultiThread::ThreadBase::PriorityNormal, "DBQueueCharacter"));
#else
	ctx->_thread.Init(CreateThreadOnCPU(64 * 1024, DBQueueThreadFunction, ctx, 2, THREAD_PRIORITY_NORMAL, "DBQueueCharacter"));
#endif
	return true;
}

bool AsyncDBStorage::DBKill(RString baseURL, RString playerUID)
{
	SaveThreadContext *ctx = new SaveThreadContext();
	ctx->AddRef(); // player can disconnect while Thread still need access to its context

	ctx->_url = baseURL;
	ctx->_uid = playerUID;
	ctx->_request = "";
	ctx->_prevContext = _lastContext;
	_lastContext = ctx;
#if USE_MT_LIB_FOR_JOBS
	ctx->_thread.Init(CreateMTThreadOnCPU(64 * 1024, DBKillThreadFunction, ctx, 2, MultiThread::ThreadBase::PriorityNormal, "DBKillCharacter"));
#else
	ctx->_thread.Init(CreateThreadOnCPU(64 * 1024, DBKillThreadFunction, ctx, 2, THREAD_PRIORITY_NORMAL, "DBKillCharacter"));
#endif
	return true;
}

bool AsyncDBStorage::DBItemDrop(RString baseURL, RString request)
{
	SaveThreadContext *ctx = new SaveThreadContext();
	ctx->AddRef(); // player can disconnect while Thread still need access to its context

	ctx->_url = baseURL;
	ctx->_request = request;
	ctx->_prevContext = _lastContext;
	_lastContext = ctx;
#if USE_MT_LIB_FOR_JOBS
	ctx->_thread.Init(CreateMTThreadOnCPU(64 * 1024, DBItemDropThreadFunction, ctx, 2, MultiThread::ThreadBase::PriorityNormal, "DBItemDrop"));
#else
	ctx->_thread.Init(CreateThreadOnCPU(64 * 1024, DBItemDropThreadFunction, ctx, 2, THREAD_PRIORITY_NORMAL, "DBItemDrop"));
#endif
	return true;
}


DWORD WINAPI AsyncDBStorage::DBLoadThreadFunction(void *ctx)
{
  Ref<LoadThreadContext> context = (LoadThreadContext *)ctx;
  context->DecRef();

  // wait until previous save requests end
  Ref<SaveThreadContext> prev = context->_prevContext;
  if (prev)
  {
    prev->_thread.Wait();
    context->_prevContext = NULL; // no longer needed
  }

  context->_err = GetResponse(EHTTPREQ_GET,context->_result,context->_url,context->_playerUID,NULL);

  return 0;
}

void AsyncDBStorage::DBCheckLoaded()
{
  for (int i=0; i<_loadContexts.Size();)
  {
    LoadThreadContext *ctx = _loadContexts[i];
    if (!ctx->_thread.TryWait())
    {
      // thread is still running
      i++; continue;
    }
    // thread finished, handle results
    Person *person = NULL;
    if (ctx->_err >= 400)
    {
      LogF("dbServerLoadCharacter: HTTP Error %d", ctx->_err);
      LogF(ctx->_result);
    }
    else
    {
      JSONDocument document;
      document.Parse<rapidjson::kParseDefaultFlags>(ctx->_result);

      if (document.Empty())
      {
        LogF("dbServerLoadCharacter: document not parsed");
        LogF(ctx->_result);
      }
      else
      {
        JSONValue &value = document["model"];
        if (!value.IsString())
        {
          LogF("dbServerLoadCharacter: model not found");
          LogF(ctx->_result);
        }
        else
        {
          RString model = value.GetString();

          // simplified vehicle creation
          Ref<Entity> veh = GWorld->NewNonAIVehicleWithID(model, RString());
          person = dyn_cast<Person>(veh.GetRef());
          if (!person)
          {
            LogF("dbServerLoadCharacter: cannot create character of type %s", cc_cast(model));
            LogF(ctx->_result);
          }
          else
          {
            if (!person->JSONLoad(document))
            {
              LogF("dbServerLoadCharacter: cannot load state of character", cc_cast(model));
              LogF(ctx->_result);
              person = NULL;
            }
            else
            {
              GWorld->AddVehicle(person);
              GetNetworkManager().CreateVehicle(person, VLTVehicle, "", -1);
              person->Init(person->FutureVisualState().Transform(), true);

              person->UpdateSubobjects();
              GetNetworkManager().UpdateObject(person);
            }
          }
        }
      }
    }

    // execute the "handler"
    GameDataCode *code = ctx->_code;
    if (code && code->IsCompiled() && code->GetCode().Size() > 0)
    {
      GameState *state = GWorld->GetGameState();
      if (state)
      {
        GameVarSpace vars(false);
        int gshandle = state->BeginContext(&vars);

        state->VarSetLocal("_id", (float)ctx->_playerId, true);
        state->VarSetLocal("_uid", ctx->_playerUID, true);
        state->VarSetLocal("_name", ctx->_name, true);
        state->VarSetLocal("_agent", GameValueExt(person), true);
        state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

        state->EndContext(gshandle);
      }
    }

    _loadContexts.Delete(i);
  }
}

void AsyncDBStorage::DBLoad(RString baseURL, int playerId, RString playerUID, RString name, GameDataCode *code)
{
  Ref<LoadThreadContext> ctx = new LoadThreadContext();
  ctx->AddRef(); // player can disconnect while Thread still need access to its context

  ctx->_url = baseURL;

  ctx->_prevContext = _lastContext;
  ctx->_code = code;
  ctx->_playerId = playerId;
  ctx->_playerUID = Format("?uid=%s", cc_cast(playerUID));
  ctx->_name = name;

#if USE_MT_LIB_FOR_JOBS
    ctx->_thread.Init(CreateMTThreadOnCPU(64 * 1024, DBLoadThreadFunction, ctx.GetRef(), 2, MultiThread::ThreadBase::PriorityNormal, "DBLoadCharacter"));
#else
  ctx->_thread.Init(CreateThreadOnCPU(64 * 1024, DBLoadThreadFunction, ctx.GetRef(), 2, THREAD_PRIORITY_NORMAL, "DBLoadCharacter"));
#endif
  _loadContexts.Add(ctx);
}

bool NetworkPlayerInfo::ClientStateBusy()
{
  if ( (clientState>=NCSPlayerAssigned) && (clientState<NCSPlaying) )
  { // I am busy
    if ( !_busyStartTime )
    { // I've just started to be busy
      _busyStartTime = ::GetTickCount();
      NetLog("Client %s busy - not checking signatures",cc_cast(name));
    }
    return true; // busy
  }
  else
  { // Not busy
    if ( _busyStartTime )
    { // I've just stopped to be busy
      // Add the time spent to all signatures timeouts
      signatureChecks.DelayCheckTimeouts(::GetTickCount() - _busyStartTime, name);
      NetLog("Client %s no longer busy - resume checking signatures",cc_cast(name));
    }
    _busyStartTime = 0; // always set. Serves as reinitialization after mision restart etc.
    return false; // not busy
  }
}

#if _PROFILE
  #define DIAG_MP_OBJECT_LIFE 1
#endif


#if DIAG_MP_OBJECT_LIFE
struct NetworkIdJournal
{
  AutoArray<RString> logs;
  NetworkId key;
};
TypeIsMovable(NetworkIdJournal)

template <>
struct MapClassTraits<NetworkIdJournal>: public DefMapClassTraits<NetworkIdJournal>
{
  /// key type
  typedef const NetworkId &KeyType;
  typedef NetworkIdJournal Type;
  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    return 32768*key.creator.CreatorInt()+key.id;
  }
  
  /// compare keys, implementation needs only ZERO when equals
  static int CmpKey(KeyType k1, KeyType k2)
  {
    return k1!=k2;
  }
  
  /// get a key for given a item
  static KeyType GetKey(const Type &item) {return item.key;}
};

class ObjectsLifeJournal
{
  static const int MaxObjectsLifeJournalCount = 10000;  //max count of stored entries
  AutoArray<NetworkId> _networkIdList;
  int lastIndex;  //when journal is full, we overwrite old values in circular manner
  MapStringToClass<NetworkIdJournal, AutoArray<NetworkIdJournal> > _objEventMap;

  public:
    ObjectsLifeJournal():lastIndex(0) {};
    void Add(NetworkId id, RString eventStory);
    const AutoArray<RString>* Get(NetworkId id) const;
    void Clear()
    {
      _networkIdList.Clear();
      _objEventMap.Clear();
      lastIndex = 0;
    }
};

void ObjectsLifeJournal::Add(NetworkId id, RString eventStory)
{
  NetworkIdJournal &objectJournal = _objEventMap.Set(id);
  if (_objEventMap.IsNull(objectJournal))
  {
    // new item
    NetworkIdJournal newJournal; newJournal.key=id; newJournal.logs.Add(eventStory);
    _objEventMap.Add(newJournal);
    if (_networkIdList.Size() < MaxObjectsLifeJournalCount) 
      lastIndex = _networkIdList.Add();
    else
    {
      lastIndex = (lastIndex+1) % MaxObjectsLifeJournalCount;
      _objEventMap.Remove(_networkIdList[lastIndex]);
    }
    _networkIdList[lastIndex] = id;
  }
  else objectJournal.logs.Add(eventStory);
}

const AutoArray<RString>* ObjectsLifeJournal::Get(NetworkId id) const
{
  const NetworkIdJournal &log = _objEventMap.Get(id);
  if (_objEventMap.NotNull(log))
  {
    return &log.logs;
  }
  return NULL;
}

ObjectsLifeJournal GObjLifeJournal;
#endif

#define EXTRACT_ID(name) objectId = message->_##name;
#define EXTRACT_UNITS units = message->_units;
#define EXTRACT_CHANNEL channel = message->_channel;

DEFINE_FAST_ALLOCATOR(DelayedMessageInfo)

DelayedMessageInfo::DelayedMessageInfo(NetworkServer *server, NetworkMessageType t, NetworkMessage *m)
:msg(m),type(t)
{
  error=1;
  invMaxAge = 1.0f/10;
  timeReceived = Glob.time;
  switch (type)
  {
    case NMTFireWeapon:
    {
      NetworkMessageFireWeapon *msg = static_cast<NetworkMessageFireWeapon *>(m);
      static float visibleErrorCoef = 2.0f;
      error = msg->_visible*visibleErrorCoef;
      pos = msg->_pos; // TODO: remove _pos from the message, use server->GetObjectPosition
      invMaxAge = 1.0f/5;
      break;
    }
    case NMTExplosionDamageEffects:
    {
      NetworkMessageExplosionDamageEffects *msg = static_cast<NetworkMessageExplosionDamageEffects *>(m);
      invMaxAge = 1.0f/20;

	    // undefined camera position
	    pos = msg->_pos;
	    // if ammo type is not defined, assume some default importance
	    error = 1;
      if (msg->_type.GetLength() > 0)
      {
        Ref<EntityType> type = VehicleTypes.New(msg->_type);
        AmmoType *ammo = dynamic_cast<AmmoType *>(type.GetRef());
        if (ammo)
        {
          // cf. Landscape::ExplosionDamageEffects
          static float smallExplosionError = 3.0f;
          static float bigExplosionError = 10.0f;
          float smallError = smallExplosionError;
          float bigError = ammo->explosive*ammo->indirectHitRange*bigExplosionError;
          error = Lerp(smallError,bigError,ammo->explosive);
          }
        }
      break; 
    }
    case NMTRemoveProjectileDecals:
    case NMTProjectileDecal:
    case NMTBowState:
    {
      //what should happen here?
      //NetworkMessageProjectileDecal *msg = static_cast<NetworkMessageProjectileDecal *>(m);
      break; 
    }
    case NMTAskForPlayerAction:
    {
      auto *msg = static_cast<NetworkMessageAskForPlayerAction *>(m);
      static float visibleErrorCoefAction = 0.7f;
      error = visibleErrorCoefAction;
      // check player position
      pos = server->GetObjectPosition(msg->_person);
      invMaxAge = 1.0f/5;
      break;
    }
    default:
      Fail("Unexpected message type in DelayedMessageInfo");
  }
}

DEFINE_FAST_ALLOCATOR(WaitingMessageInfo)

/// check if new message is overriding the old one
template <int msgType>
inline bool InitMessageIsOverride(const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  // by default assume it is not
  return false;
}

template <>
inline bool InitMessageIsOverride<NMTAskForAnimationPhase>(const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  // we need to process the messages as they are stored on the server
  const NetworkMessageAskForAnimationPhase *mOld = static_cast<const NetworkMessageAskForAnimationPhase *>(msgOld);
  const NetworkMessageAskForAnimationPhase *mNew = static_cast<const NetworkMessageAskForAnimationPhase *>(msgNew);
  return mOld->_vehicle==mNew->_vehicle && mOld->_animation==mNew->_animation;
};

template <>
inline bool InitMessageIsOverride<NMTCreateRadioChannel>(const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  // we need to process the messages as they are stored on the server
  const NetworkMessageCreateRadioChannel *mOld = static_cast<const NetworkMessageCreateRadioChannel *>(msgOld);
  const NetworkMessageCreateRadioChannel *mNew = static_cast<const NetworkMessageCreateRadioChannel *>(msgNew);
  return (mOld->_channel==mNew->_channel);
};

template <>
inline bool InitMessageIsOverride<NMTRadioChannelLabel>(const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  // we need to process the messages as they are stored on the server
  const NetworkMessageCreateRadioChannel *mOld = static_cast<const NetworkMessageCreateRadioChannel *>(msgOld);
  const NetworkMessageCreateRadioChannel *mNew = static_cast<const NetworkMessageCreateRadioChannel *>(msgNew);
  return (mOld->_channel==mNew->_channel);
};

template <>
inline bool InitMessageIsOverride<NMTRadioChannelCallSign>(const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  // we need to process the messages as they are stored on the server
  const NetworkMessageCreateRadioChannel *mOld = static_cast<const NetworkMessageCreateRadioChannel *>(msgOld);
  const NetworkMessageCreateRadioChannel *mNew = static_cast<const NetworkMessageCreateRadioChannel *>(msgNew);
  return (mOld->_channel==mNew->_channel);
};

template <>
inline bool InitMessageIsOverride<NMTRadioChannelUnits>(const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  // we need to process the messages as they are stored on the server
  const NetworkMessageCreateRadioChannel *mOld = static_cast<const NetworkMessageCreateRadioChannel *>(msgOld);
  const NetworkMessageCreateRadioChannel *mNew = static_cast<const NetworkMessageCreateRadioChannel *>(msgNew);
  return (mOld->_channel==mNew->_channel);
};

template <int newType>
inline bool InitMessageCanOverride(NetworkMessageType oldType, const NetworkMessage *msgOld, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg)
{
  return false;
};

static bool InitMessageIsOverride(
  NetworkMessageType oldType, const NetworkMessage *msgOld, NetworkMessageType newType, NetworkMessage *msgNew, Ref<NetworkMessage> &newInitMsg
)
{
  if (oldType==newType)
  {
    switch (oldType)
    {
      #define NMT_TYPE_CASE_IS_OVERRIDE(macro, class, name, description, group) \
        case NMT##name: return InitMessageIsOverride<NMT##name>(msgOld,msgNew, newInitMsg);
      // generate cases for all message types
      NETWORK_MESSAGE_TYPES(NMT_TYPE_CASE_IS_OVERRIDE);
    }
    return false;
  }
  else
  {
    switch (newType)
    {
      #define NMT_TYPE_CASE_CAN_OVERRIDE(macro, class, name, description, group) \
        case NMT##name: return InitMessageCanOverride<NMT##name>(oldType,msgOld,msgNew,newInitMsg);
      // generate cases for all message types
      NETWORK_MESSAGE_TYPES(NMT_TYPE_CASE_CAN_OVERRIDE);
    }
  }
  return false;
}


/*!
\patch 5161 Date 5/31/2007 by Ondra
- Optimized: Reduced amount of data transferred when JIP into a complex and long running mission.
*/

void NetworkServer::AddInitAndRemoveOverridden(
  NetworkMessageType type, NetworkMessage * msg, int **updateI
)
{
  int dst,src;
  Ref<NetworkMessage> newInitMsg = msg;
  for (src=dst=0; src<_initMessages.Size(); )
  {
    if (InitMessageIsOverride(_initMessages[src].type,_initMessages[src].msg,type,msg,newInitMsg))
    {
      if (updateI)
      {
        // if deleting before any of updateI, we need to update the value
        for (int **ii=updateI; *ii; ii++)
        {
          if (src<=**ii) --**ii;
        }
      }
      // skip the message, it is obsolete now
      src++;
    }
    else
    {
      if (src!=dst) _initMessages[dst] = _initMessages[src];
      src++, dst++;
    }
  }
  // if anything was deleted, remove it now
  _initMessages.Resize(dst);
  if (newInitMsg)
  {
    // add the new message
    //CheckInitMessages(_initMessages, "before _initMessages.Add()");
    int index = _initMessages.Add();
    _initMessages[index].msg = newInitMsg;
    _initMessages[index].type = type;
  }
  //CheckInitMessages(_initMessages, "after _initMessages.Add()");
}

/// check if new message is overriding the old one
template <int msgType>
inline bool InitMessageIsDeleted(const NetworkMessage *msg, NetworkId obj)
{
  // by default assume it is not
  return false;
}

static bool InitMessageIsDeleted(NetworkMessageType type, const NetworkMessage *msg, NetworkId obj)
{
  switch (type)
  {
    #define NMT_TYPE_CASE_IS_DELETED(macro, class, name, description, group) \
      case NMT##name: return InitMessageIsDeleted<NMT##name>(msg,obj);break;
    // generate cases for all message types
    NETWORK_MESSAGE_TYPES(NMT_TYPE_CASE_IS_DELETED);
  }
  return false;
}

void NetworkServer::RemoveInitObjectDeleted(NetworkId obj)
{
  int dst,src;
  for (src=dst=0; src<_initMessages.Size(); )
  {
    if (InitMessageIsDeleted(_initMessages[src].type,_initMessages[src].msg,obj))
    {
      // skip the message, it is obsolete now
      src++;
    }
    else
    {
      if (src!=dst) _initMessages[dst] = _initMessages[src];
      src++, dst++;
    }
  }
  // if anything was deleted, remove it now
  _initMessages.Resize(dst);
}

void NetworkServer::RptFObjectIdJournal(const NetworkId &id)
{
  if (NetworkDeletedObjectInfo *delInfo = _deletedObjects.get(id))
  {
    RptF("   - object was deleted, owner %d, time %.3f", delInfo->owner, delInfo->time.toFloat());
  }
#if DIAG_MP_OBJECT_LIFE
  if (const AutoArray<RString> *logLines = GObjLifeJournal.Get(id))
  {
    for (int i=0; i<logLines->Size(); i++)
    {
      RptF("   -> %s", cc_cast((*logLines)[i]));
    }
  }
#endif
}

void NetworkServer::RptFObjectNotFound(const NetworkId &id, const char *msg, int from)
{
  if (_serverState==NSSLoadingGame || _serverState==NSSPlaying)
  {
//    RString fromTxt = from>=0 ? Format(" from=%d",from) : RString();
//    RptF("Server: Object %d:%" OID_FORMAT" not found (message %s)%s", id.creator.CreatorInt(), id.id, msg, cc_cast(fromTxt));
    RptFObjectIdJournal(id);
#if DIAG_MP_OBJECT_LIFE
    DiagObjectInfoAskMessage ask;
    ask._creator=id.creator;
    ask._id=id.id;
    if (from!=-1)
      SendMsg(from, &ask, NMFGuaranteed);
    if (from!=BOT_CLIENT)
      SendMsg(BOT_CLIENT, &ask, NMFGuaranteed);
#endif
  }
}

void NetworkServer::RptFObjectNotFound(const NetworkId &id, NetworkMessageType type, int from, const char *context)
{
#if _ENABLE_CHEATS
  extern const char *NetworkMessageTypeNames[];
  const char *typeName = NetworkMessageTypeNames[type];
  const char *msg = typeName;
#else
  BString<128> typeName;
  sprintf(typeName,"Type_%d",type);
  const char *msg = typeName;
#endif
  BString<256> temp;
  if (context)
  {
    strcpy(temp,typeName);
    strcat(temp,context);
    msg = temp;
  }
  RptFObjectNotFound(id,msg,from);
}

bool CollectedHashes::UpdatePlayerBankCount(int bankCount)
{
  if (banksCount==-1)
  { // not set yet
    banksCount = bankCount;
    bankInfoTimeout = 0; //reset
  }
  else if (banksCount != bankCount)
  {
    // player sent different bankCount to her prior info => cheating?
    return true;
  }
  int sigCount = SignaturesCount();
  if (sigCount && sigCount!=bankCount)
  {
    // player sent different bankCount than was checked during initial signature test => cheating?
    return true;
  }
  return false;
}

void NetworkServer::ReportWrongSignatures(int from, RString filename, const NetworkPlayerInfo *info, RString report)
{
#if _ENABLE_DEDICATED_SERVER
  LogF("Hacked Signatures?");
  RString reportBase = Format(LocalizeString(IDS_SIGNATURE_WRONG), cc_cast(info->name), cc_cast(filename));
#if DEBUG_SIGNATURES
  if (report.IsEmpty())
  {
    NetLog(reportBase);
  }
  else
  {
    RString reportMsg = Format("%s (%s)", cc_cast(reportBase), cc_cast(report)); //add additional report info
    NetLog(reportMsg);
  }
#endif
  ServerMessage(reportBase); // report only basic info, diag details are in NetLog
#endif
  OnHackedData(HTHackedData, from, filename);
};

#if _ENABLE_DEDICATED_SERVER
void NetworkServer::DoReassignDirect()
{
  // send to clients
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.clientState < NCSConnected) continue;
    SendMissionInfo(info.dpid, true);
  }

  SetServerState(NSSAssigningPlayers);

  _restart = false;
  _reassign = false;
}

void NetworkServer::DoReassign()
{
  _restart = true;
  _reassign = true;
  if (_serverState != NSSPlaying)
  {
    GNetworkManager.DestroyAllObjects();
    DoReassignDirect();
  }
}

void NetworkServer::DoMissions(bool admin)
{
  _mission = "?";
  _restart = true;
  if (_serverState != NSSSelectingMission && _serverState != NSSPlaying)
  {
    GNetworkManager.DestroyAllObjects();
    SetServerState(NSSSelectingMission);
  }
  NetworkCommandMessage answer;
  answer._type = NCMTVoteMission;
  NetworkCommandMessage answer2;
  answer2._type = NCMTVoteMissionExt;
  AddMissionList(answer,answer2);
  if (admin)
  {
    SendMsg(_gameMaster, &answer, NMFGuaranteed);
    SendMsg(_gameMaster, &answer2, NMFGuaranteed);
  }
  else
  {
    for (int i=0; i<_identities.Size(); i++)
    {
      SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
      SendMsg(_identities[i].dpnid, &answer2, NMFGuaranteed);
    }
  }
}
#endif

void NetworkServer::SpawnForPlayer(int dpnid, GameDataCode *code, const GameValue &args)
{
  if (dpnid == BOT_CLIENT)
  {
    ScriptVM *script = new ScriptVM(ScriptVMDirect, *code, args, GWorld->GetGameState()->GetGlobalVariables());
    GWorld->AddScriptVM(script, false);
  }
  else
  {
    SpawnForPlayerMessage msg;
    msg._code = code->GetString();

    if (!args.GetNil())
    {
      QOStrStream out;
      if (WriteValue(args, out))
      {
        int size = out.pcount();
        msg._value.Resize(size);
        memcpy(msg._value.Data(), out.str(), size);
      }
    }

    SendMsg(dpnid, &msg, NMFGuaranteed);
  }
}

static int BestAddress(int &portRet, const NetworkPlayerInfo *info, int NetworkPlayerInfo::*addr, int NetworkPlayerInfo::*port)
{
  if (info->*addr!=0 && info->*port!=0)
  {
    portRet = info->*port;
    return info->*addr;
  }
  else if (info->_publicAddr && info->_publicPort)
  {
    portRet = info->_publicPort;
    return info->_publicAddr;
  }
  else
  {
    portRet = info->_privatePort;
    return info->_privateAddr;
  }
}

/*!
\patch_internal 1.01 Date 6/22/2001 by Ondra.
- Added: ID==0 refusal
\patch 1.02 Date 7/12/2001 by Jirka:
  - Fixed: error in assignment
  - allow player assignment only in appropriate state
  - ignore messages arrived after state has changed
\patch_internal 1.03 Date 7/13/2001 by Jirka.
- Fixed: Allow \ in url of squad (for debugging purposes)
\patch_internal 1.14 Date 8/9/2001 by Ondra.
- Added: ID>110000000 || ID>10000000 && ID<100000000 refusal
\patch 1.21 Date 8/24/2001 by Jirka:
- Fixed: error in assignment
\patch 1.25 Date 9/29/2001 by Ondra:
- New: Ping to all clients displayed on server.
\patch 1.27 Date 10/15/2001 by Jirka
- Fixed: When CAPS LOCK was pressed, Voice Over Net was not disabled (bug since 1.26)
\patch 1.30 Date 11/01/2001 by Jirka
- Fixed: Voting is echoed to clients
\patch 1.33 Date 11/29/2001 by Jirka
- Improved: Global ban list reloads before player is checked
\patch_internal 1.43 Date 1/31/2002 by Jirka
- Fixed: Server does not send create and update messages to nonplaying clients
\patch 1.82 Date 8/19/2002 by Ondra
- Fixed: Voting is echoed even to the player voting.
\patch 1.86 Date 10/16/2002 by Jirka
- Fixed: On dedicated server with mission rotating, after #login and #missions no missions appears
\patch 5136 Date 2/28/2007 by Jirka
- Fixed: MP - command #exec allowed only for logged (not voted) admin
\patch 5137 Date 3/5/2007 by Jirka
- Fixed: Dedicated server - after #reassign two players could control a single avatar
\patch 5154 Date 5/3/2007 by Jirka
- Fixed: Dedicated server - ban.txt was locked for edit during the game
\patch 5164 Date 10/3/2007 by Jirka
- Fixed: Handling of duplicate CD key improved
\patch 5175 Date 10/16/2007 by Jirka
- Added: After 3 wrong attempts to log in as an admin further attempts are ignored for some time
*/

void NetworkServer::OnMessage(int from, NetworkMessage *msg, NetworkMessageType type)
{
#if _ENABLE_PERFLOG
  PROFILE_SCOPE_EX(OnMsg,*);
  if (PROFILE_SCOPE_NAME(OnMsg).IsActive())
  {
    PROFILE_SCOPE_NAME(OnMsg).AddMoreInfo(Format("type=%d", type));
  }
#endif

  NetworkMessageFormatBase *format = GetFormat(/*from, */type);
  if (!format)
  {
    RptF("Server: Bad message %d(%s) format", (int)type, cc_cast(GetMsgTypeName(type)));
    return;
  }
  NetworkMessageContext ctx(msg, this, from, MSG_RECEIVE);

  #if _ENABLE_CHEATS
  int level = GetDiagLevel(type,from!=_parent->GetClient()->GetPlayer());
  if (level >= 2)
  {
    DiagLogF("Server: received message %s from %d", cc_cast(GetMsgTypeName(type)), from);   
    ctx.LogMessage(format, level, "\t");
  }
  #endif

  NetworkId objectId;
  AutoArray<NetworkId> units;
  int channel = -1;

  bool initMessageAlreadyAdded = false;
  switch (type)
  {
/*
    case NMTMsgFormat:
      {
        NetworkPlayerInfo *info = GetPlayerInfo(from);
        if (!info) info = OnPlayerCreate(from, "");
        int index = msg->id.id - NMTFirstVariant;
        if (index >= info->formats.Size())
          info->formats.Resize(index + 1);
        info->formats[index].TransferMsg(ctx);
      }
      break;
*/
    case NMTSelectPlayer:
      {
LogF("Server: received SelectPlayer");
        SelectPlayerMessage pl;
        pl.TransferMsg(ctx);
        Assert(pl._player != AI_PLAYER);
        NetworkId person(pl._creator, pl._id);

        NetworkPlayerInfo *info = GetPlayerInfo(pl._player);
        if (info)
        {
          info->person = person;
          info->cameraPosition = pl._position;
          // HOTFIX: msg->time cannot be used as NMTSelectPlayer is sent by BOT_CLIENT which time can be different
          //         using Time(0) forces first update from client to set it properly according to clients time
          info->cameraPositionTime = Time(0);
#if _VBS3
          info->cameraFov = 0.75;
          info->cameraDirection = VForward;
#endif
        }

        // first select player, then change owner
        // (when changeOwner arrive to client, player must be already known
        // - for respawn parameters initialization)
        if (from != pl._player)
        {
LogF("  sending to %x", pl._player);
          SendMsg(pl._player, msg, type, NMFGuaranteed, NULL);
        }

        NetworkObjectInfo *oInfo = GetObjectInfo(person);
        if (oInfo)
        {
          ChangeOwner(person, oInfo->owner, pl._player);
        }
      }
      break;
    case NMTAskMissionFile:
      {
        AskMissionFileMessage ask;
        ask.TransferMsg(ctx);
        if (ask._valid)
        {
          NetworkPlayerInfo *info = GetPlayerInfo(from);
          if (info) info->missionFileValid = true;
        }
      }
      break;
    case NMTClientState:
      {
        ClientStateMessage cs;
        cs.TransferMsg(ctx);
        OnClientStateChanged(from, (NetworkClientState)cs._clientState);
      }
      break;
    case NMTPlayerClientState:
      {
        PlayerClientStateMessage pcs;
        pcs.TransferMsg(ctx);
        SetClientState(pcs._dpnid, (NetworkClientState)pcs._clientState);
      }
      break;
    case NMTLogin:
      {
        //{ CHANGED
        PlayerIdentity identity;
        identity.TransferMsg(ctx);

/*
        int index = _identities.Add();
        PlayerIdentity &identity = _identities[index];
        identity.TransferMsg(ctx);
*/
        //}

        // check if not in ban lists
        if (from != BOT_CLIENT)
        {
          // reload ban list
          LoadBanList("ban.txt", _banListGlobal);
          GFileServerFunctions->FlushReadHandle("ban.txt");
          
          bool ban = false;
          for (int i=0; i<_banListGlobal.Size(); i++)
            if (IsUserIdMatch(identity.id,_banListGlobal[i])) {ban = true; break;}
          if (!ban) for (int i=0; i<_banListLocal.Size(); i++)
            if (IsUserIdMatch(identity.id,_banListLocal[i])) {ban = true; break;}
          if (ban)
          {
            //{ CHANGED
            // _identities.Delete(index);
            //}
            KickOff(from,KORBan);
            break;
          }
        }

        _squadChecks.Add(new CheckSquadObject(identity, _proxy, this));

#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
        for (int i=0; i<_players.Size(); i++)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid != from) continue;
          info._steamValidate.Start(identity, this);
          break;
        }
#endif

        //}

        // send server time to player just connected
        ServerTimeMessage stmsg;
        _serverTime = ::GetTickCount()-_serverTimeInitTickCount; // update _serverTime
        stmsg._time = _serverTime;
        stmsg._endTime = _serverTimeOfEstimatedEnd;
        SendMsg(from, &stmsg, NMFGuaranteed | NMFHighPriority);

#if _ENABLE_DEDICATED_SERVER
        // client need to know whether there is admin on server
        ServerAdminMessage samsg;
        samsg._admin = _admin;
        samsg._gameMaster = _gameMaster;
        SendMsg(from, &samsg, NMFGuaranteed);
#endif
      }
      break;
    case NMTCustomFilesWanted:
      {
        CustomFilesWantedMessage filesWantedMsg;
        filesWantedMsg.TransferMsg(ctx);
        for (int i=0; i<_squadChecks.Size(); i++)
        {
          if (_squadChecks[i]->IsPlayer(filesWantedMsg._player))
          {
            _squadChecks[i]->SetCustomFilesWanted(from, filesWantedMsg._filesWanted);
            break;
          }
        }
      }
      break;
    case NMTUpdateClientInput:
      OnMessageClientInput(from, type, ctx);
      break;
    case NMTAskForAnimationPhase:
      {
        PREPARE_TRANSFER(AskForAnimationPhase)
        EXTRACT_ID(vehicle)
        if (!objectId.IsNull())
        {
          if (objectId.creator == CreatorId(STATIC_OBJECT))
          {
            // save message for lately connecting players
            AddInitAndRemoveOverridden(type,msg);

            for (int i=0; i<_players.Size(); i++)
            {
              NetworkPlayerInfo &info = *_players[i];
              if (info.dpid == from) continue;
              if (info.clientState != NCSPlaying) continue;
              SendMsg(info.dpid, msg, type, NMFNone, NULL);
            }
          }
          else
          {
            SendMsg(BOT_CLIENT, msg, type, NMFGuaranteed, NULL);
            }
          }
        }
      break;
    case NMTAskForPilotLight:
      {
        PREPARE_TRANSFER(AskForPilotLight)
        EXTRACT_ID(vehicle)
      }
      goto LabelAskWithVehicle;
    case NMTAskForVehicleDoorState:
      {
        PREPARE_TRANSFER(AskForVehicleDoorState)
        EXTRACT_ID(vehicle)
      }
      goto LabelAskWithVehicle;
    case NMTAskForSetMagazineAmmo:
      {
        PREPARE_TRANSFER(AskForSetMagazineAmmo)
        EXTRACT_ID(magazine)
      }
      goto LabelAskWithVehicle;
    case NMTAskForAmmo:
      {
        PREPARE_TRANSFER(AskForAmmo)
        EXTRACT_ID(turret)
        if (objectId.IsNull())
        {
          EXTRACT_ID(vehicle)
        }
      }
      goto LabelAskWithVehicle;
    case NMTAskForMoveVector:
      {
        if (from!=BOT_CLIENT) break; // AskForMove is honored from the bot-client only
        PREPARE_TRANSFER(AskForMoveVector)
        EXTRACT_ID(vehicle)
      }
      goto LabelAskWithVehicle;
    case NMTAskForMoveMatrix:
      {
        if (from!=BOT_CLIENT) break; // AskForMove is honored from the bot-client only
        PREPARE_TRANSFER(AskForMoveMatrix)
        EXTRACT_ID(vehicle)
#if _ENABLE_PHYSICS
				//ConsoleF("askformove vehicle %x", _parent->GetObject(message->_vehicle));
#endif
      }
      goto LabelAskWithVehicle;
#if _VBS3
    case NMTAskCommanderOverride:
      {
        PREPARE_TRANSFER(AskCommanderOverride)
          EXTRACT_ID(turret)
      }

			goto LabelAskWithVehicle;
#endif
    LabelAskWithVehicle:
      {
        NetworkObjectInfo *info = GetObjectInfo(objectId);
        if (info)
        {
          if (info->pendingOwner != NO_CLIENT)
          {
            WaitingMessageInfo *waitingMsg = new WaitingMessageInfo(type,msg);
            info->waitingMessages.Add(waitingMsg);
          }
          else
          {
            SendMsg(info->owner, msg, type, NMFGuaranteed, NULL);
          }
        }
        else
        {
          RptFObjectNotFound(objectId, type,from);
        }
      }
      break;
    case NMTForceDeleteObject:
      {
        // remove from object lists
        PREPARE_TRANSFER(ForceDeleteObject)
        NetworkId id;
        if (TRANSF_GET_ID_BASE(object, id) != TMOK) break;
        PerformObjectDestroy(id);

        // send to other clients
        for (int i=0; i<_players.Size(); i++)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid == from) continue;
          if (info.clientState < NCSMissionReceived) continue;
          SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
        }
      }
      break;
    case NMTAskForSetItemShortcut:
    case NMTAskForMoveItem:
    case NMTAskForTakeItemToHands:
    case NMTAskForSwapItems:
      SendMsg(BOT_CLIENT, msg, type, NMFGuaranteed);
      break;
    case NMTAskForRotateInventoryItem:
      {
        AskForRotateInventoryItemMessage ask;
        TMError err = ask.TransferMsg(ctx);
        if ((err == TMOK || err == TMNotFound) && ask._person && ask._item &&
            ask._person->GetRemotePlayer() == from && ask._person->FindItem(ask._item))
          SendMsg(BOT_CLIENT, msg, type, NMFGuaranteed);
      }
      break;
    case NMTAskInventoryMenu:
      {
        PREPARE_TRANSFER(AskInventoryMenu)
        Person *person = dyn_cast<Person>(_parent->GetObject(message->_unit));
        InventoryItem *item1 = dyn_cast<InventoryItem>(_parent->GetObject(message->_item1));
        if (person && person->Brain() && item1)
        {
          Ref<NetworkMessageInventoryMenu> reply = new NetworkMessageInventoryMenu();
          reply->_unit = message->_unit;
          reply->_item1 = message->_item1;
          reply->_item2 = message->_item2;
          reply->_swap = false;

          if (message->_item2.IsNull())
          {
            // single item

            // actions
            item1->GetActions(
              [&reply](const UIAction &action) -> void 
              { 
                if (action.action.NotNull())
                {
                  NetworkMessageInventoryMenuAction *submsg = new NetworkMessageInventoryMenuAction();
                  submsg->_actionType = action.action->GetType();
                  submsg->_actionTarget = action.action->GetTarget() ? 
                    action.action->GetTarget()->GetNetworkId() : NetworkId::Null();

                  QOStrStream out;
                  action.action->SaveParams(out);
                  int size = out.pcount();
                  submsg->_actionParams.Resize(size);
                  memcpy(submsg->_actionParams.Data(), out.str(), size);

                  reply->_actions.Add(submsg);
                }
              },
              person->Brain());
          }
          else
          {
            // two items
            InventoryItem *item2 = dyn_cast<InventoryItem>(_parent->GetObject(message->_item2));
            if (!item2) break; // recipes on two items are expected

            // recipes
            Recipes.Collect(person, item1, item2,
              [&reply](const Recipe *recipe) -> bool
              { 
                reply->_recipes.Add(recipe->_name);
                return false; // continue
              });

            if (item2->CanAddAttachment(item1))
            {
               NetworkMessageInventoryMenuAttachment *submsg = new NetworkMessageInventoryMenuAttachment();
               submsg->_attachment = true;
               submsg->_item1 = item1->GetNetworkId();
               submsg->_item2 = item2->GetNetworkId();
               reply->_attachments.Add(submsg);
            }
            else if (item2->CanAddOwnedItem(item1))
            {
               NetworkMessageInventoryMenuAttachment *submsg = new NetworkMessageInventoryMenuAttachment();
               submsg->_attachment = false;
               submsg->_item1 = item1->GetNetworkId();
               submsg->_item2 = item2->GetNetworkId();
               reply->_attachments.Add(submsg);
            }

            bool CanSwapItems(InventoryItem *item1, InventoryItem *item2);
            reply->_swap = CanSwapItems(item1, item2);
          }

          SendMsg(from, reply, NMTInventoryMenu, NMFGuaranteed);
        }
      }
      break;
    case NMTAskForThrowItem:
#if _ENABLE_PHYSICS
      {
        // change ownership of item to gain control over position
        PREPARE_TRANSFER(AskForThrowItem)
				
        if (!message->_item.IsNull() && !message->_person.IsNull())
        {
					NetworkObjectInfo *info = GetObjectInfo(message->_item);
					
					//nastavi se sice primo v klientovi, ale tam nevidim na info. takhle zabezpecim, ze mi to nikdo neprepise
					if (info && info->owner != BOT_CLIENT)
					  ChangeOwner(message->_item, info->owner, BOT_CLIENT);
					//SendMsg(BOT_CLIENT, message, type, NMFGuaranteed);
					
					if (info)
					{
						if (info->pendingOwner != NO_CLIENT)
						{
							WaitingMessageInfo *waitingMsg = new WaitingMessageInfo(type,msg);
							info->waitingMessages.Add(waitingMsg);
						}
						else
						{
							SendMsg(info->owner, msg, type, NMFGuaranteed, NULL);
						}
					}
				}
      }
#endif
      break;
     case NMTAskForDropItem:
      {
        // change ownership of item to gain control over position
        PREPARE_TRANSFER(AskForDropItem)
        if (!message->_item.IsNull())
        {
					const NetworkObjectInfo *info = GetObjectInfo(message->_item);
          if (info && info->owner != BOT_CLIENT)
            ChangeOwner(message->_item, info->owner, BOT_CLIENT);
          SendMsg(BOT_CLIENT, message, type, NMFGuaranteed);
        }
      }
      break;
    case NMTProcessAction:
      {
        PREPARE_TRANSFER(ProcessAction)
        if (!message->_unit.IsNull())
        {
          Person *person = dyn_cast<Person>(_parent->GetObject(message->_unit));
          AIBrain *unit = person ? person->Brain() : nullptr;
          if (unit && unit->GetVehicle())
          {
            Ref<Action> action = CreateAction((UIActionType)message->_actionType);
            if (action)
            {
              Object *target = nullptr;
              if (!message->_actionTarget.IsNull()) 
                target = dyn_cast<Object>(_parent->GetObject(message->_actionTarget));
              action->SetTarget(target);

              QIStrStream in(message->_actionParams.Data(), message->_actionParams.Size());
              action->LoadParams(in);

              unit->GetVehicle()->StartActionProcessing(action, unit);
            }
          }
        }
      }
      break;
    case NMTProcessRecipe:
      {
        PREPARE_TRANSFER(ProcessRecipe)
        if (!message->_person.IsNull())
        {
          Person *person = dyn_cast<Person>(_parent->GetObject(message->_person));
          if (person)
          {
            const Recipe *recipe = Recipes.Find(message->_recipe);
            if (recipe)
            {
              InventoryItem *item1 = dyn_cast<InventoryItem>(_parent->GetObject(message->_item1));
              InventoryItem *item2 = dyn_cast<InventoryItem>(_parent->GetObject(message->_item2));
              recipe->Process(person, item1, item2);
            }
          }
        }
      }
      break;
    case NMTMarkerDelete:
      // send to all except from (state Briefing expected)
      for (int i=0; i<_players.Size(); i++)
      {
        NetworkPlayerInfo &info = *_players[i];
        if (info.dpid == from) continue;
        if (info.clientState < NCSGameLoaded) continue;
        SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
      }
      if(type == NMTMarkerDelete)
      {
        PREPARE_TRANSFER(MarkerDelete)
        RemoveJIPMarkerInfo(message->_name);
      }
      break;
#if _VBS2 //Convoy trainer
  #if _EXT_CTRL
    case NMTAARAskUpdate:// Here we service the ask request for AAR
      {
        AARAskUpdateMessage act;
        act.TransferMsg(ctx);

        switch(act._type)
        {
        case StartRecord:
          GAAR.Record();
          break;
        case StopRecord:
          GAAR.RecordStop();
          break;
        case SaveFile:
          GAAR.RecordSave(act._stringOne);
          break;
        case AddBookMK:
          GAAR.AddBookMark(act._floatType,act._stringOne,act._stringTwo);
          break;
        case RemBookMK:
          GAAR.RemoveBookMark(act._stringOne);
          break;
        case SaveState:
          // Client should never ask to set save state
          Assert(false); 
          break;
        default:
          Assert(false);// message type not handeled, development bug
        }
      }
      break;
  #endif
    case NMTAddMPReport:
    case NMTAARDoUpdate: // Here we update all clients with their new state.
    case NMTPauseSimulation:
#endif
    case NMTApplyWeather:// request will be sent to the bot client too
      goto ProcessInitMessage;
#if _ENABLE_ATTACHED_OBJECTS
    case NMTAttachObject:
    case NMTDetachObject:
      if (from!=BOT_CLIENT) break;
#endif
    ProcessInitMessage:
#if _VBS2 || _ENABLE_ATTACHED_OBJECTS
      AddInitAndRemoveOverridden(type, msg);
      initMessageAlreadyAdded = true;
#endif
      // save message for lately connecting players
      if (!initMessageAlreadyAdded)
      {
        //CheckInitMessages(_initMessages, "before _initMessages.Add()");
        int index = _initMessages.Add();
        _initMessages[index].msg = msg;
        _initMessages[index].type = type;
        //CheckInitMessages(_initMessages, "after _initMessages.Add()");
      }
    // no break here - continue
    case NMTPlaySound:
    case NMTSoundState:
    case NMTVehicleDestroyed:
    case NMTVehicleDamaged:
sendToAllPlayers:
      // send to all except from (state Play expected)
      for (int i=0; i<_players.Size(); i++)
      {
        NetworkPlayerInfo &info = *_players[i];
        if (info.dpid == from) continue;
        if (info.clientState != NCSPlaying) continue;
        SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
      }
      break;
    case NMTSubscribeRadios:
      {
       NetworkMessageSubscribeRadios *rm = dynamic_cast<NetworkMessageSubscribeRadios *>(msg);
       if (rm)
       {
         for (int i=0; i<_players.Size(); i++)
         {
           if (_players[i]->dpid==rm->_player)
           {
             _radioSubscribers.AddUnique(rm->_player, rm->_frequences);
             goto sendToAllPlayers;
           }
         }
       }
      }
      break;
    case NMTAskForPlayerAction:
      // check if the person in question is the player
      {
        auto *msgAction = static_cast<NetworkMessageAskForPlayerAction *>(msg);
        NetworkId playerPerson = GetPlayerPerson(from);
        if (playerPerson!=msgAction->_person)
        {
          RptF("AskForPlayerAction ignored for player %d, person different %d:%" OID_FORMAT "!=%d:%" OID_FORMAT,from, playerPerson.creator.CreatorInt(),playerPerson.id,msgAction->_person.creator.CreatorInt(),msgAction->_person.id);
        }

      }
      goto DelayedMessageProcessing;
    case NMTFireWeapon:
    case NMTExplosionDamageEffects:
    case NMTBowState:
    DelayedMessageProcessing:
      for (int i=0; i<_players.Size(); i++)
      {
        NetworkPlayerInfo &info = *_players[i];
        if (info.dpid == from) continue;
        if (info.dpid==BOT_CLIENT)
        {
          SendMsg(BOT_CLIENT, msg, type, NMFGuaranteed, nullptr);
          continue;
        }
        if (info.clientState != NCSPlaying) continue;
        {
          DelayedMessageInfo *delayed = new DelayedMessageInfo(this,type,msg);
          info._delayedMessages.Add(delayed);
        }
      }
      break;
    case NMTProjectileDecal:
    case NMTRemoveProjectileDecals:
      if (from != BOT_CLIENT)
      {
        SendMsg(BOT_CLIENT, msg, type, NMFGuaranteed, nullptr);
      }
      else
      {
        for (int i=0; i<_players.Size(); i++)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid == BOT_CLIENT || info.clientState != NCSPlaying)
          {
            continue;
          }

          DelayedMessageInfo *delayed = new DelayedMessageInfo(this,type,msg);
          info._delayedMessages.Add(delayed);
        }
      }
      break;
    case NMTExplosionDamage:
      SendMsg(BOT_CLIENT, msg, type, NMFGuaranteed, nullptr);
      break;
    case NMTSynchronizeVariable:
      if (from!=BOT_CLIENT) break;
      goto SendToAll;
      // continue
    case NMTTransferFile:
#if _ENABLE_UNSIGNED_MISSIONS
    case NMTTransferMissionFile:
#endif
    case NMTMissionStats:
    SendToAll:
      // send to all except from (state Mission Received expected)
      for (int i=0; i<_players.Size(); i++)
      {
        NetworkPlayerInfo &info = *_players[i];
        if (info.dpid == from) continue;
        if (info.clientState < NCSMissionReceived) continue;
        SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
      }
//CheckInitMessages(_initMessages, "after NetworkComponent::SendMsg");
      break;
    case NMTSendAUMsg:
      {
        SendAUMsgMessage auMsg;
        auMsg.TransferMsg(ctx);
        GameValue value;
        if (!ReadValue(value, auMsg._value.Data(), auMsg._value.Size(), _parent)) break;
        const GameArrayType &array=value;
        if ( array.Size() != 2 ) break;
        if (array[0].GetType()==GameScalar)
        {
          int count = array[0];
          // send to some count of players except from (state Mission Received expected)
          for (int i=0; (i<_players.Size()) && (count>0); i++)
          {
            NetworkPlayerInfo &info = *_players[i];
            if (info.dpid == from) continue;
            count--;
            SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
          }
        }
        else if (array[0].GetType()==GameArray)
        {
          const GameArrayType &sendTo = array[0];
          // send to all players listed in array
          for (int i=0; i<_players.Size(); i++)
          {
            NetworkPlayerInfo &info = *_players[i];
            if (info.dpid == from) continue;
            for (int j=0; j<sendTo.Size(); j++)
            {
              if (sendTo[j].GetType()!=GameString) continue;
              RString name = sendTo[j];
              if ( stricmp(info.name, name)==0 )
              {
                SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
                break;
              }
            }
          }
        }
      }
      break;
    case NMTPublicVariableTo:
      {
        PublicVariableToMessage transfer;
        transfer.TransferMsg(ctx);
        if (from!=BOT_CLIENT && transfer._player!=BOT_CLIENT) break;
        SendMsg(transfer._player, msg, type, NMFGuaranteed, NULL);
      }
      break;
    case NMTTransferFileToServer:
      {
        TransferFileMessage transfer;
        transfer.TransferMsg(ctx);
        transfer._path = GetServerPath(transfer._path);
        // check if path is valid upload path
        // if not, kick sender
        PlayerIdentity *id = FindIdentity(from);
        bool alreadyKicked = false;
        if (id && id->destroy)
        {
          alreadyKicked = true;
        }

        RString name;
        for (int i=0; i<_players.Size(); i++)
        {
          if (_players[i]->dpid == from)
          {
            name = _players[i]->name;
            if (_players[i]->kickedOff) alreadyKicked = true;
            break;
          }
        }

        if (alreadyKicked)
        {
          LogF
          (
            "Transfer file %s request ignored, sender is being kicked off",
            (const char *)transfer._path
          );
          break;
        }
        // check if file transferred is within allowed range
        // if not, kick off sender
        if (transfer._totSize>MaxCustomFileSize)
        {
          // check name of player (from file name)
          RString reason = Format
          (
              LocalizeString(IDS_DS_PLAYER_KICKED_BIG_CUSTOM_FILE),
              (const char *)name,(const char *)GetRelUploadPath(transfer._path,name),
              transfer._totSize,MaxCustomFileSize
          );
          ServerMessage(reason);
          KickOff(from,KORKick,reason);
          break;
        }

        if (!CheckValidUpload(transfer._path,name))
        {
          RString reason = Format
          (
              LocalizeString(IDS_DS_PLAYER_KICKED_INVALID_CUSTOM_FILE),
              (const char *)name,(const char *)transfer._path
          );
          ServerMessage(reason);
          KickOff(from,KORKick, reason);
          break;
        }

        ReceiveFileSegment(transfer);
      }
      break;
    case NMTDeleteObject:
      if (_serverState < NSSLoadingGame) break; // updates from the last session
      {
        PREPARE_TRANSFER(DeleteObject)

        NetworkId id;
        if (TRANSF_BASE(creator, id.creator) != TMOK) break;
        if (TRANSF_BASE(id, id.id) != TMOK) break;

        // object deletion should be requested by the bot-client only
        if (from!=BOT_CLIENT)
        {
#if _ENABLE_PHYSICS1
					{
						InventoryItem *item = dyn_cast<InventoryItem>(_parent->GetObject(id));
						if(item && item->GetPhysics())
							ConsoleF("delete dyn phys for %x user pointer %x", item, ((enf::BulletController*)item->GetPhysics())->GetCollisionObject()->getUserPointer());
					}
#endif
          RptF("Object %d:%",OID_FORMAT," delete requested from client %d",id.creator.CreatorInt(),id.id,from);
          break;
        }

#if DIAG_MP_OBJECT_LIFE
        GObjLifeJournal.Add(id, Format("(%.3f) Delete: from=%d",Glob.time.toFloat(), from));
#endif
        OnObjectDestroy(id, from);
        // RptF("Server: Object %d:%" OID_FORMAT" deleted", id.creator.CreatorInt(), id.id);
      }
      break;
    case NMTDeletedObject:
      {
        if (_serverState < NSSLoadingGame) break; // updates from the last session
        {
          PREPARE_TRANSFER(DeletedObject)

          NetworkId id;
          if (TRANSF_BASE(creator, id.creator) != TMOK) break;
          if (TRANSF_BASE(id, id.id) != TMOK) break;

          _deletedObjects.removeKey(id);
#if DIAG_MP_OBJECT_LIFE
          GObjLifeJournal.Add(id, Format("(%.3f) Deleted: from=%d",Glob.time.toFloat(), from));
#endif
        }
      }
      break;
    case NMTCreateObject:
    case NMTCreateVehicle:
    case NMTCreateVehicleAI:
    case NMTCreateVehicleBrain:
    case NMTCreateShot:
    case NMTCreateCrater:
    case NMTCreateCraterOnVehicle:
    case NMTCreateSoundOnVehicle:
    case NMTCreateObjectDestructed:
    case NMTCreateDynSoundSource:
    case NMTCreateHelicopter:
#if _HELISIM_ROTORLIB
    case NMTCreateHelicopterRTD: 
#endif   
    case NMTCreateSeagull:
    case NMTCreateTurret:
    case NMTCreateDestructionEffects:
    case NMTCreateClientCameraPosition:
      if (_serverState < NSSLoadingGame) break; // updates from the last session

      {
        PREPARE_TRANSFER(NetworkObject)

        NetworkId id;
        if (TRANSF_BASE(objectCreator, id.creator) != TMOK) break;
        if (TRANSF_BASE(objectId, id.id) != TMOK) break;

        if (from!=BOT_CLIENT && type!=NMTCreateClientCameraPosition)
        {
          RptF("Object %d:%",OID_FORMAT," create requested from client %d",id.creator.CreatorInt(),id.id,from);
          break; // create message may come only from a bot-client, with the exception of the CameraPosition object
        }

        // copy message (avoid changes on message in message queue)
        Ref<NetworkMessage> copy = ::CreateNetworkMessage(type);
        CopyMsg(copy, msg, format);
        NetworkObjectInfo *oInfo = OnObjectCreate(id, from, copy, type);
        if (!oInfo) break;
// RptF("Server: Object %d:%" OID_FORMAT" created, type %s", id.creator.CreatorInt(), id.id, cc_cast(GetMsgTypeName(type)));

        auto sendToClient = [this,id,oInfo,from] (int i)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid == from) return;
          if (info.clientState < NCSMissionReceived) return;
          PlayerIndex index = GetPlayerIndex(info.dpid);
          DoAssert(index>=0);
          if (index<0) return;
          
          if (info.person!=id)
          {
            NetworkPlayerObjectInfo *poInfo = oInfo->GetPlayerObjectInfo(index,info.dpid);
            if (!poInfo->updating)
            {
              SendCreateMessage(info.dpid, *oInfo, poInfo);
              poInfo->updating = true;
              info.markAsUpdating(poInfo);
            }
          }
        };

        // send to bot-client first
        for (int i=0; i<_players.Size(); i++) // TODO: avoid for loop, bot client is always first
        {
          if (_players[i]->dpid==BOT_CLIENT) {sendToClient(i);break;}
        }
        oInfo->object = _parent->GetClient()->GetObject(id);
        DoAssert(oInfo->object);

        Entity *ent = dyn_cast<Entity,NetworkObject>(oInfo->object);
        if (ent)
        {
          OnHierParentChange(ent);
        }

        float maxDist = oInfo->BestPosition()._updateDistance;
        for (int i=0; i<_players.Size(); i++)
        {
          int dpid = _players[i]->dpid;
          if (dpid==BOT_CLIENT) continue;
          if (DebugMeBoy(oInfo->id))
          {
            LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} Create message for %d dist %g", oInfo->id.creator.CreatorInt(), oInfo->id.id, dpid, maxDist);
          }
          if (maxDist>=FLT_MAX) sendToClient(i);
        }
        // SendMsg created the object on the bot-client if necessary, we can get it now

      }
      break;
    case NMTUpdateDamageVehicleAI:
    case NMTUpdateDamageVehicle:
    case NMTUpdateDamageObject:
      ctx.SetClass(NMCUpdateDamage);
      if (BattlEyeEnabled())
      {
        PREPARE_TRANSFER(UpdateDamageObject)

        NetworkId id;
        float damage;
        if (TRANSF_BASE(objectCreator, id.creator) != TMOK) break;
        if (TRANSF_BASE(objectId, id.id) != TMOK) break;
        if (TRANSF_BASE(dammage, damage) != TMOK) break;
        #if _USE_BATTL_EYE_SERVER
        if (_beIntegration.UpdateDamageCallback(from,NetworkIdToNetIdFriendly(id),damage)) break;
        #endif
      }
      goto Update;
    case NMTUpdatePositionVehicle:
    case NMTUpdatePositionMan:
    case NMTUpdatePositionTank:
    case NMTUpdatePositionCar:
    case NMTUpdatePositionAirplane:
    case NMTUpdatePositionHelicopter:
#if _HELISIM_ROTORLIB    
    case NMTUpdatePositionHelicopterRTD:
#endif
    case NMTUpdatePositionShip:
    case NMTUpdatePositionDoor:
    case NMTUpdatePositionSeagull:
    case NMTUpdatePositionMotorcycle:
    case NMTUpdatePositionMagazine:
#if _USE_BATTL_EYE_SERVER
      if (BattlEyeEnabled())
      {
        PREPARE_TRANSFER(UpdatePositionVehicle)

        NetworkId id;
        Vector3 position;
        Matrix3 o;
        if (TRANSF_BASE(objectCreator, id.creator) != TMOK) break;
        if (TRANSF_BASE(objectId, id.id) != TMOK) break;
        if (TRANSF_BASE(position, position) != TMOK) break;
        if (TRANSF_BASE(orientation, o) != TMOK) break;
        if (_beIntegration.UpdatePositionCallback(from,NetworkIdToNetIdFriendly(id),position.X(),position.Y(),position.Z(),
                o.Direction().X(),o.Direction().Y(),o.Direction().Z(),
                o.DirectionUp().X(),o.DirectionUp().Y(),o.DirectionUp().Z(),
                o.DirectionAside().X(),o.DirectionAside().Y(),o.DirectionAside().Z())
           ) break;
      }
#endif
      ctx.SetClass(NMCUpdatePosition);
      goto Update;
    case NMTUpdatePositionTurret:
      ctx.SetClass(NMCUpdatePosition);
      goto Update;
    case NMTUpdateTransport:
    case NMTUpdateTankOrCar:
    case NMTUpdateTank:
    case NMTUpdateCar:
    case NMTUpdateAirplane:
    case NMTUpdateHelicopter:
#if _HELISIM_ROTORLIB
    case NMTUpdateHelicopterRTD:
#endif
    case NMTUpdateParachute:
    case NMTUpdateShip:
    case NMTUpdateDoor:
    case NMTUpdateSeagull:
    case NMTUpdateObject:
    case NMTUpdateVehicle:
    case NMTUpdateFlag:
    case NMTUpdateShot:
    case NMTUpdateMine:
    case NMTUpdateWeapon:
    case NMTUpdateVehicleAI:
    case NMTUpdateVehicleAIFull:
    case NMTUpdateVehicleBrain:
    case NMTUpdateMan:
    case NMTUpdateMotorcycle:
    case NMTUpdateFireplace:
    case NMTUpdateTurret:
    case NMTUpdateClientCameraPosition:
    case NMTUpdateInventoryItem:
    case NMTUpdateItemRadio:
    case NMTUpdateItemTransmitter:
    Update:
      if (_serverState < NSSLoadingGame) break; // updates from the last session
      {
        PREPARE_TRANSFER(NetworkObject)

        NetworkId id;
        if (TRANSF_BASE(objectCreator, id.creator) != TMOK) break;
        if (TRANSF_BASE(objectId, id.id) != TMOK) break;

        if (_updatesGuaranteed && from!=BOT_CLIENT)
        {
          MessageDiffApply diff;
          message->LoadDiff(diff);
          
          DoAssert(diff.IsIdentity());

          // TODO: diff messages to the server as well (we need to store or access incoming reference message for that)
          //message->ApplyDiff(refMsg,diff);

          message->ResetDiff();

        }

      #if USER_Bebul
/*
        static int doItCounter = 0;
        if (!(++doItCounter % 50))
        {
          DiagObjectInfoAskMessage ask; ask._creator=id.creator; ask._id=id.id;
          SendMsg(from, &ask, NMFGuaranteed);
          if (from!=BOT_CLIENT)
           SendMsg(BOT_CLIENT, &ask, NMFGuaranteed);
        }
*/
        //RptFObjectNotFound(id, type, from);
      #endif

        NetworkObjectInfo *oInfo = OnObjectUpdate(id, from, msg, type, ctx.GetClass());
        if (!oInfo) break;
        bool guaranteed;
        if (TRANSF_BASE(guaranteed, guaranteed) != TMOK) guaranteed = false;
        // if (_state < NGSPlay)
        if (guaranteed)
        {
          // init state transfer, send immediately
          for (int i=0; i<_players.Size(); i++)
          {
            NetworkPlayerInfo &pInfo = *_players[i];
            if (pInfo.dpid == from) continue;
            if (pInfo.clientState < NCSMissionReceived) continue;

            if (oInfo->GetPlayerObjectInfo(GetPlayerIndex(pInfo.dpid),pInfo.dpid)->updating)
            {
              if (DebugMeBoy(id))
              {
                LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} init update sent to %d", id.creator.CreatorInt(), id.id, pInfo.dpid);
              }
              UpdateObject(&pInfo, oInfo, ctx.GetClass(), NMFGuaranteed);
            }
          }
        }
        else if (from!=BOT_CLIENT)
        {
          // always send immediately to the bot-client
          for (int i=0; i<_players.Size(); i++)
          {
            NetworkPlayerInfo &pInfo = *_players[i];
            if (pInfo.dpid != BOT_CLIENT) continue;
            if (pInfo.clientState < NCSMissionReceived) continue;

            UpdateObject(&pInfo, oInfo, ctx.GetClass(), NMFGuaranteed);
            break;
          }

        }
        if (ctx.GetClass() != NMCUpdateGeneric) break;

        // check ownership of items in cargo
        {
          NetworkMessageUpdateVehicleAI *msg = dynamic_cast<NetworkMessageUpdateVehicleAI *>(ctx.GetNetworkMessage());
          if (msg) CheckItemsOwner(msg, oInfo->owner);
        }
        if (IsUpdateTransport(type))
        {
          PREPARE_TRANSFER(UpdateTransport)

          // vehicle is always simulated where driver is local
          // if there is no driver, it can be simulated on any computer
          NetworkId driver;
          NetworkId copilot;
          if (TRANSF_GET_ID_BASE(copilot, copilot) == TMOK)
          {
            // check for NULL id
            if (!copilot.IsNull())
            {
              int newOwner = FindOwner(copilot);
              if (newOwner != 0) ChangeOwner(id, oInfo->owner, newOwner);
            }
            else if (TRANSF_GET_ID_BASE(driver, driver) == TMOK)
            {
              // check for NULL id
              if (!driver.IsNull())
              {
                int newOwner = FindOwner(driver);
                if (newOwner != 0) ChangeOwner(id, oInfo->owner, newOwner);
              }
            }
          }          
        }
        else if (type == NMTUpdateMan)
        {
          // Check owner of items in slots
          PREPARE_TRANSFER(UpdateVehicleBrain)

#define CHECK_SLOT_OWNER(type, typeprefix, name) \
          { \
            const NetworkId &id = message->_slot##name; \
            if (!id.IsNull()) CheckItemOwner(id, oInfo->owner); \
          }

          INVENTORY_SLOT_ENUM(InventorySlotId, IS, CHECK_SLOT_OWNER)
        }
        else if (type == NMTUpdateTurret)
        {
          PREPARE_TRANSFER(UpdateTurret)

          // turret is always simulated where gunner is local
          // if there is no gunner, it can be simulated on any computer
          NetworkId gunner;
#if _VBS3 //commander override, change locality!
          NetworkId overriddenByTurret;
          bool override = false;
          if (TRANSF_GET_ID_BASE(overriddenByTurret, overriddenByTurret) == TMOK)
          {
            if(!overriddenByTurret.IsNull())
            {
              int newOwner = FindOwner(overriddenByTurret);
              if (newOwner != 0) 
              {
                ChangeOwner(id, oInfo->owner, newOwner);
                override = true;
              }
            }
          }
          if(!override)
#endif
          if (TRANSF_GET_ID_BASE(gunner, gunner) == TMOK)
          {
            // check for NULL id
            if (!gunner.IsNull())
            {
              int newOwner = FindOwner(gunner);
              if (newOwner != 0) ChangeOwner(id, oInfo->owner, newOwner);
            }
          }
        }
        else if (type == NMTUpdateInventoryItem || type == NMTUpdateItemRadio || type == NMTUpdateItemRadio || type == NMTUpdateItemTransmitter || type == NMTUpdateWeapon) // update of any inventory item
        {
          PREPARE_TRANSFER(UpdateInventoryItem)
          if (message->_inLandscape) CheckItemOwner(id, BOT_CLIENT);
        }
      }
      break;
    case NMTUpdateClientInfo:
      {
        ClientInfoObject clientInfo;
        clientInfo.TransferMsg(ctx);
        NetworkPlayerInfo *pInfo = GetPlayerInfo(from);
        if (pInfo && msg->time > pInfo->cameraPositionTime)
        {
          pInfo->cameraPosition = clientInfo._cameraPosition;
          pInfo->cameraPositionTime = msg->time;
#if _VBS3
          pInfo->cameraFov = clientInfo._cameraFov;
          pInfo->cameraDirection = clientInfo._cameraDirection;
#endif

          if (_serverState < NSSLoadingGame || _serverState>NSSPlaying) break; // updates from the last session
          {
            PREPARE_TRANSFER(UpdateClientInfo);

            NetworkId id;
            if (TRANSF_BASE(objectCreator, id.creator) != TMOK) break;
            if (TRANSF_BASE(objectId, id.id) != TMOK) break;
            // Client Info need to be updated on Clients only for dead players (VoN Direct Speech needs position)
            if (CheckPlayerInLobby(*pInfo))
            {
              // create if not created yet
              OnObjectCreate(id, from, NULL, NMTNone);

              clientInfo._dpnid = from;

              Ref<NetworkMessage> msg2 = ::CreateNetworkMessage(NMTUpdateClientInfoDpid);
              msg2->time = Glob.time;
              NetworkMessageContext ctx2(msg2, this, from, MSG_SEND);
              ctx2.SetClass(NMCUpdatePosition);
              
              NetworkMessageUpdateClientInfoDpid *message2 = static_cast<NetworkMessageUpdateClientInfoDpid *>(msg2.GetRef());
              message2->_guaranteed = false;

              TMError err = clientInfo.TransferMsg(ctx2);
              if (err == TMOK)
              {
                OnObjectUpdate(id, from, msg2, NMTUpdateClientInfoDpid, NMCUpdatePosition);
              }
            }
          }
        }
      }
      break;
    case NMTUpdateClientInfoDpid:
      Fail("Unexpected on server");
      break;
    case NMTChat:
      {
        PREPARE_TRANSFER(Chat)

        AutoArray<NetworkId> units;
        if (TRANSF_GET_IDS_BASE(units, units) != TMOK) break;
        AUTO_STATIC_ARRAY(int, players, 32);
        int channel;
        if (TRANSF_BASE(channel, channel) != TMOK) break;
        if (channel & 0x80000000)
        {
          //@{ Ant1HaX0R section
          NetworkPlayerInfo *info = GetPlayerInfo(from);
          if (info)
          {
            // does the client know the level 2 signatures asks?
            RString text;
            if (TRANSF_BASE(text, text) != TMOK) break;
            // loaded bank count hidden inside the NMTChat ... channel = ( #loaded banks xor (dpnid & 0x7fffffff) ) | 0x80000000
            int bankCount = (channel & 0x7fffffff) ^ (from  & 0x7fffffff);
            if (info->hashes.UpdatePlayerBankCount(bankCount))
              KickOff(info->dpid,KORKick);
          }
          break;
          //@}
        }
        if ((channel != CCGlobal && channel != CCSystem) && units.Size() > 0)
          GetPlayersOnChannel(players, units, from, false);
        else
          GetPlayersOnChannel(players, channel, from, false);
        for (int i=0; i<players.Size(); i++)
        {
          NetMsgFlags priority =
          (
            _serverState == NSSPlaying ? NMFNone : NMFHighPriority
          ); 
          SendMsg(players[i], msg, type, NMFGuaranteed|priority, NULL);
        }
#if _USE_BATTL_EYE_SERVER
        {
          RString text;
          if (TRANSF_BASE(text, text) != TMOK) break;
          _beIntegration.PlayerChatReceived(from, channel, text);
      }
#endif
      }
      break;
    case NMTLocalizedChat:
      {
        PREPARE_TRANSFER(LocalizedChat)

        AutoArray<NetworkId> units;
        if (TRANSF_GET_IDS_BASE(units, units) != TMOK) break;
        AUTO_STATIC_ARRAY(int, players, 32);
        int channel;
        if (TRANSF_BASE(channel, channel) != TMOK) break;
        if ((channel != CCGlobal && channel != CCSystem) && units.Size() > 0)
          GetPlayersOnChannel(players, units, from, false);
        else
          GetPlayersOnChannel(players, channel, from, false);
        for (int i=0; i<players.Size(); i++)
        {
          NetMsgFlags priority =
          (
            _serverState == NSSPlaying ? NMFNone : NMFHighPriority
          ); 
          SendMsg(players[i], msg, type, NMFGuaranteed|priority, NULL);
        }
      }
      break;
    case NMTRadioChat:
      {
        PREPARE_TRANSFER(RadioChat)
        EXTRACT_UNITS
        EXTRACT_CHANNEL
      }
      goto LabelChat;
    case NMTRadioChatWave:
      {
        PREPARE_TRANSFER(RadioChatWave)
        EXTRACT_UNITS
        EXTRACT_CHANNEL
      }
      goto LabelChat;
    case NMTMarkerCreate:
      {
        PREPARE_TRANSFER(MarkerCreate)
        EXTRACT_UNITS
        EXTRACT_CHANNEL

        AddJIPMarkerInfo(message->_marker, channel, from); // saved for JIP players
      }
    LabelChat:
      {
        AUTO_STATIC_ARRAY(int, players, 32);
        if (channel != CCGlobal && channel != CCSystem) //units are already extracted, so we simply send it to all units specified
          GetPlayersOnChannel(players, units, from, false);
        else
          GetPlayersOnChannel(players, channel, from, false);
        for (int i=0; i<players.Size(); i++)
        {
          SendMsg(players[i], msg, type, NMFGuaranteed, NULL);
        }
      }
      break;
    case NMTCreateRadioChannel:
    case NMTRadioChannelLabel:
    case NMTRadioChannelCallSign:
    case NMTRadioChannelUnits:
      AddInitAndRemoveOverridden(type, msg);
      {
        for (int i=0; i<_players.Size(); i++)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid == from) continue;
          if (info.clientState < NCSConnected) continue;
          SendMsg(info.dpid, msg, type, NMFGuaranteed);
        }
      }
      break;
    case NMTNetworkCommand:
      {
        NetworkCommandMessage cmd;
        cmd.TransferMsg(ctx);
        switch (cmd._type)
        {
#if _ENABLE_DEDICATED_SERVER
        case NCMTLogin:
          {
            if (_dedicated && (_gameMaster == AI_PLAYER || _admin))
            {
              static const int MaxLoginAttempts = 3;
              static const DWORD DelayAfterMaxAttempts = 10 * 1000; // delay in ms

              NetworkPlayerInfo *info = GetPlayerInfo(from);
              if (!info) break;
              PlayerIdentity *ident = FindIdentity(from);
              if (!ident) break;
              bool adminById = false;
              if (ident->certificate.Size()>0)
              {
                AutoArray<RString> admins;
                ConstParamEntryPtr entry = _serverCfg.FindEntry("admins");
                if (entry)
                {
                  for (int i=0; i<entry->GetSize(); i++)
                  {
                    RString adminId = (*entry)[i];
                    if (!strcmpi(adminId,ident->id))
                    {
                      adminById = true;
                      break;
                    }
                  }
                }
              }

              if (!adminById)
              {
                if (GlobalTickCount() < info->_loginDisabledUntil) break;
              RString password = cmd._content.ReadString();
              ConstParamEntryPtr entry = _serverCfg.FindEntry("passwordAdmin");
              if (entry)
              {
                RString realPassword = *entry;
                if (strcmp(password, realPassword) != 0)
                {
                  info->_loginFailedCount++;
                  if (info->_loginFailedCount >= MaxLoginAttempts)
                  {
                    info->_loginFailedCount = 0;
                    info->_loginDisabledUntil = GlobalTickCount() + DelayAfterMaxAttempts;
                  }
                  break;
                }
              }
              }
              info->_loginFailedCount = 0;

              if (_gameMaster != AI_PLAYER)
              {
                NetworkCommandMessage answer;
                answer._type = NCMTLoggedOut;
                SendMsg(_gameMaster, &answer, NMFGuaranteed);
              }
              
              SetGameMaster(from, false);
            }
          }
          break;
        case NCMTLogout:
          if (_dedicated && from == _gameMaster)
          {
            NetworkCommandMessage answer;
            answer._type = NCMTLoggedOut;
            SendMsg(_gameMaster, &answer, NMFGuaranteed);

            RString name;
            for (int i=0; i<_players.Size(); i++)
              if (_players[i]->dpid == _gameMaster)
              {
                name = _players[i]->name;
                break;
              }
            ConsoleF(LocalizeString(IDS_DS_ADMIN_LOGGED_OUT), cc_cast(name));

            _gameMaster = AI_PLAYER;
            _sessionLocked = false;
            _debugOn.Clear();

            UpdateAdminState();

            if (_mission[0] == '?' && _mission[1] == 0)
            {
              // when admin logged out, perform mission voting instead
              NetworkCommandMessage answer;
              answer._type = NCMTVoteMission;
              NetworkCommandMessage answer2;
              answer2._type = NCMTVoteMissionExt;
              AddMissionList(answer,answer2);
              for (int i=0; i<_identities.Size(); i++)
              {
                SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
                SendMsg(_identities[i].dpnid, &answer2, NMFGuaranteed);
              }
            }
          }
          break;
        case NCMTShutdown:
          if (_dedicated && ((from == _gameMaster && !_admin) || from==BOT_CLIENT))
          {
            #if defined _WIN32
            ::PostMessage((HWND)hwndApp, WM_CLOSE, 0, 0);
            #else
            interrupted = true;
            #endif
          }
          break;
#if _VBS3
        case NCMTExitAll:
          {
            if (_dedicated && from == _gameMaster)
            {
              // send command to all clients to shut down!
              for (int i=0; i<_players.Size(); i++)
              {
                NetworkPlayerInfo &info = _players[i];
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
              }
            }
          }
          break;
#endif
        case NCMTMonitorAsk:
          if (from == _gameMaster || from==BOT_CLIENT)
          {
            float interval;
            cmd._content.Read(&interval, sizeof(interval));
            Monitor(interval);
          }
          break;
        case NCMTDebugAsk:
          if
          (
            (_dedicated && from == _gameMaster && !_admin) ||
             from==BOT_CLIENT
          )
          {
            DebugAsk(cmd._content.ReadString());
          }
          break;
        case NCMTMission:
          if (_dedicated && (from == _gameMaster || from==BOT_CLIENT))
          {
            _mission = cmd._content.ReadString();

            RString diffName = cmd._content.ReadString();
            _difficulty = Glob.config.diffDefault;
            int difficulty = Glob.config.diffNames.GetValue(diffName);
            if (difficulty >= 0) _difficulty = difficulty;
            if (_mission.GetLength() > 0) _restart = true;
            if (_serverState != NSSSelectingMission && _serverState != NSSPlaying)
            {
              GNetworkManager.DestroyAllObjects();
              SetServerState(NSSSelectingMission);
            }
          }
          break;
        case NCMTMissions:
          if (_dedicated && (from == _gameMaster || from==BOT_CLIENT))
          {
            DoMissions(true);
          }
          break;
        case NCMTRestart:
          if (_dedicated && _serverState == NSSPlaying && 
              (from == _gameMaster || from==BOT_CLIENT) ) _restart = true;
          break;
        case NCMTReassign:
          if (_dedicated && (from == _gameMaster || from==BOT_CLIENT))
          {
            DoReassign();
          }
          break;
        case NCMTProfileAsk:
#if _ENABLE_PERFLOG
          if (_dedicated && from == _gameMaster)
          {
            RString params = cmd._content.ReadString();
            params.Trim();
            if (params.IsEmpty()) GPerfProfilers.CaptureSingleFrame(1);
            else
            {
              const char *separator = strchr(params, ',');
              if (separator)
              {
                int len = separator - cc_cast(params);
                RString scope = params.Substring(0, len);
                scope.Trim();
                GPerfProfilers.CaptureSlowFrame(scope, atof(separator + 1));
              }
              else
              {
                GPerfProfilers.CaptureSingleFrame(atoi(params));
              }
            }
          }
#endif
          break;
        case NCMTInit:
          if (_dedicated && (from == _gameMaster || from==BOT_CLIENT))
          {
            RString GetServerConfig();
            _serverCfg.Parse(GetServerConfig(), NULL, NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
          }
          break;
        case NCMTLockSession:
          if (_dedicated && ( (from == _gameMaster && !_admin) || from==BOT_CLIENT ) )
          {
            bool lock;
            if ( cmd._content.Read(&lock, sizeof(bool)) )
              LockSession(lock);
          }
          break;
#endif
        case NCMTKick:
          if
          (
            from == BOT_CLIENT
#if _ENABLE_DEDICATED_SERVER
            || from == _gameMaster
#endif
          )
          {
            int player;
            if
            (
              cmd._content.Read(&player, sizeof(int)) &&
              player != BOT_CLIENT
            ) KickOff(player,KORKick);
          }
          break;
#if _ENABLE_DEDICATED_SERVER
        case NCMTVote:
          if (_dedicated && _gameMaster == AI_PLAYER)
          {
            const NetworkPlayerInfo *info = GetPlayerInfo(from);
            if (!info) break;
            bool doEcho = false;
            LocalizedFormatedString echo;
            int subtype;
            cmd._content.Read(&subtype, sizeof(int));
            switch (subtype)
            {
              case NCMTMission:
                Fail("Command-style mission voting no longer supported");
                break;
              case NCMTMissions:
                _votings.Add(this, (char *)&subtype, sizeof(int), _voteThreshold, from);
                echo.SetFormat(LocalizedString::Stringtable,"STR_DS_VOTES_MISSIONS");
                echo.AddArg(LocalizedString::PlainText,info->name);
                doEcho = true;
                break;
              case NCMTRestart:
                _votings.Add(this, (char *)&subtype, sizeof(int), _voteThreshold, from);
                echo.SetFormat(LocalizedString::Stringtable,"STR_DS_VOTES_RESTART");
                echo.AddArg(LocalizedString::PlainText,info->name);
                doEcho = true;
                break;
              case NCMTReassign:
                _votings.Add(this, (char *)&subtype, sizeof(int), _voteThreshold, from);
                echo.SetFormat(LocalizedString::Stringtable,"STR_DS_VOTES_REASSIGN");
                echo.AddArg(LocalizedString::PlainText,info->name);
                doEcho = true;
                break;
              case NCMTKick:
                {
                  int id[2];
                  id[0] = subtype;
                  cmd._content.Read(&id[1], sizeof(int));
                  _votings.Add(this, (char *)id, 2 * sizeof(int), _voteThreshold, from);
                  const NetworkPlayerInfo *kickInfo = GetPlayerInfo(id[1]);
                  if (kickInfo)
                  {
                    echo.SetFormat(LocalizedString::Stringtable,"STR_DS_VOTES_KICK");
                    echo.AddArg(LocalizedString::PlainText,info->name);
                    echo.AddArg(LocalizedString::PlainText,kickInfo->name);
                    doEcho = true;
                  }
                }
                break;
              case NCMTAdmin:
                {
                  char *ptr = cmd._content.Data() + cmd._content.GetPos();
                  int size = cmd._content.Size() - cmd._content.GetPos();
                  _votings.Add(
                    this, (char *)&subtype, sizeof(int), _voteThreshold, from,
                    ptr, size
                  );
                  int player = *(int *)ptr;
                  RString name;
                  for (int i=0; i<_players.Size(); i++)
                    if (_players[i]->dpid == player)
                    {
                      name = _players[i]->name;
                      break;
                    }
                  echo.SetFormat(LocalizedString::Stringtable,"STR_DS_VOTES_ADMIN");
                  echo.AddArg(LocalizedString::PlainText,info->name);
                  echo.AddArg(LocalizedString::PlainText,name);
                  doEcho = true;
                }
                break;
            }
            // Send voting echo to all other players
            if (doEcho)
            {
              LocalizedChatMessage chat;
              chat._channel = CCSystem;
              chat._args = echo._args;
              for (int i=0; i<_players.Size(); i++)
              {
                if (_players[i]->clientState >= NCSConnected)
                  SendMsg(_players[i]->dpid, &chat, NMFGuaranteed);
              }
            }
          }
          break;
        case NCMTExec:
          {
            if (_dedicated && ( (from == _gameMaster && !_admin) || from==BOT_CLIENT ) )
            {
              RString expression = cmd._content.ReadString();
              if (expression.GetLength() > 0) QueueExec(from, expression);
            }
          }
          break;
#if _ENABLE_CHEATS
        case NCMTGameExec:
          {
            GameState *state = GWorld->GetGameState();
            if (state)
            {
              GameVarSpace vars(false);
              GameValue result;
              int gshandle = state->BeginContext(&vars);
              {
                RString code = cmd._content.ReadString();
                if (code.GetLength() > 0)
                {
                  NetworkId playerId = GetNetworkManager().GetPlayerPerson(from);
                  NetworkObject *playerNO = GetNetworkManager().GetObject(playerId);
                  Person *playerObj = dyn_cast<Person>(playerNO);
                  if (playerObj)
                  {
                    GameValue fromPlayer = GameValueExt(playerObj,GameValExtObject);
                    state->VarSetLocal("_this", fromPlayer, true);
                  }
                  result = state->EvaluateMultiple(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
                }
              }
              state->EndContext(gshandle);
              if (!result.GetNil())
              {
                NetworkCommandMessage msg;
                msg._type = NCMTExecResult;
                msg._content.WriteString(result.GetText());
                SendMsg(from, &msg, NMFGuaranteed);
              }
            }
          }
          break;
#endif
        case NCMTAskVoNDiagnostics:
          {
            // convert dpnid of sender to CD based ID
            PlayerIdentity *id = FindIdentity(from);
            RString cdid,name;
            if (id) cdid = id->GetDebugId(), name = id->name;
            if (cdid.IsEmpty()) cdid = Format("#%d", from);
            RString response = cmd._content.ReadString();
            GDebugVoNBank.AddClientResponse(cdid, name, response);
          }
          break;
#if _USE_BATTL_EYE_SERVER
          /// BattlEye server command
        case NCMTBattlEye:
          if (_dedicated && ( (from == _gameMaster && !_admin) || from==BOT_CLIENT ) )
          {
            RString command = cmd._content.ReadString();
            _beIntegration.Command(command);
          }
          break;
#endif
#endif
        default:
          Fail("Network command type");
          break;
        }
      }
      break;
    case NMTVoteMission:
#if _ENABLE_DEDICATED_SERVER
      if (_gameMaster == AI_PLAYER) // voting enabled only when no admin
      {
        VoteMissionMessage vote;
        vote.TransferMsg(ctx);
        int diffId = NCMTDifficulty;
        // difficulty voting is never finished
        int minVotePlayers = (GetMaxPlayers() + 1) / 2;
        _votings.Add(
          this, (char *)&diffId, sizeof(diffId), 1e6, from,
          vote._diff, vote._diff.GetLength(), false, minVotePlayers
        );
        int missId = NCMTMission;
        _votings.Add(
          this, (char *)&missId, sizeof(missId), 0.9999, from,
          vote._name, vote._name.GetLength(), true, minVotePlayers
        );
        // note: voting may have been completed by the vote
        MissionVotingEcho();
      }
#endif
      break;
    case NMTMissionParams:
      {
        MissionParamsMessage params;
        params.TransferMsg(ctx);

        _param1 = params._param1;
        _param2 = params._param2;

        for (int i=0; i<_players.Size(); i++)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid == from) continue;
          if (info.clientState < NCSConnected) continue;
          SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
        }
      }
      break;
#if _ENABLE_MP
    case NMTMuteList:
      {
        NetworkPlayerInfo *info = GetPlayerInfo(from);
        if (info)
        {
          info->muteList.TransferMsg(ctx);
          // send remote mute lists to other players
          for (int i=0; i<_identities.Size(); i++)
          {
            SendMuteList(_identities[i]);
          }
        }
      }
      break;
#endif
    case NMTVoiceOn:
      {
        VoiceOnMessage ask;
        ask.TransferMsg(ctx);
#if _GAMES_FOR_WINDOWS
        PlayerIdentity *identity = FindIdentity(ask._dpnid);
        if (identity) identity->voiceOn = ask._voiceOn;
#endif
        for (int i=0; i<_players.Size(); i++)
        {
          NetworkPlayerInfo &info = *_players[i];
          if (info.dpid == from) continue;
          if (info.clientState < NCSConnected) continue;
          SendMsg(info.dpid, msg, type, NMFGuaranteed, NULL);
        }
      }
      break;
    case NMTDataSignatureAnswer:
      {
        DataSignatureAnswerMessage answer;
        answer.TransferMsg(ctx);
#define PROFILE_SIGNATURES 0
#if _ENABLE_PERFLOG && PROFILE_SIGNATURES
        LogF("NMTDataSignatureAnswer START: %s", cc_cast(answer._filename));
        PROFILE_SCOPE_EX(NMTDSA,*);
        int time1;
        int time2;
        int time3;
        int time4;
#endif
        NetworkPlayerInfo *info = GetPlayerInfo(from);
        if (!info) break;
        
        //NetLog("Client %s: Data signature answer %d:'%s' level %d received, %d pending",cc_cast(info->name),answer._index,cc_cast(answer._filename),answer._level,info->signatureChecks.CountPendingData());

        if (answer._index >= 0)
        {
          // remove from the list of pending checks
          if (!info->signatureChecks.Remove(answer._index))
          { // I have not asked this question!
            if (answer._index != CollectedHashes::CollectEndIndex)
            { // backward compatibility (CollectEndIndex was not added into the pending check before)
              ReportWrongSignatures(from, answer._filename, info, "DSA Not Pending");
              break;
            }
          }  
        }

#if _ENABLE_PERFLOG && PROFILE_SIGNATURES
        time1 = PROFILE_SCOPE_NAME(NMTDSA).TimeSpentNorm();
#endif

        if (answer._signature.Version()<_sigVerRequired || !DataSignatures::VerifySignature(answer._hash, answer._signature))
        {
          RString wsaReport;
          if (answer._signature.Version()<_sigVerRequired) wsaReport = Format("DSA insufficient ver=%d", answer._signature.Version());
          else wsaReport = "DSA failed";
          wsaReport = wsaReport + Format(" ix=%d, level=%d", answer._index, answer._level);
          ReportWrongSignatures(from, answer._filename, info, wsaReport);
          break;
        }
        else 
        {
#if _ENABLE_PERFLOG && PROFILE_SIGNATURES
          time2 = PROFILE_SCOPE_NAME(NMTDSA).TimeSpentNorm();
#endif
          if (info->hashes.AddHash(answer._index, answer._filename, answer._hash._content, answer._signature.Version(), answer._level, info->signatureChecks))
          {
            RString wsaReport = Format("DSA AddHash, level=%d (%s)", answer._level, cc_cast(info->hashes.GetLastError()));
            ReportWrongSignatures(from, answer._filename, info, wsaReport);
            break;
          }
              }
#if _ENABLE_PERFLOG && PROFILE_SIGNATURES
        time4 = PROFILE_SCOPE_NAME(NMTDSA).TimeSpentNorm();
        extern unsigned getClockFrequency ();
        LogF("    %.2f ms spent, A NMT=%d", time1/100.0f, NMTDataSignatureAnswer);
        LogF("    %.2f ms spent, B NMT=%d", (time2-time1)/100.0f, NMTDataSignatureAnswer);
        LogF("    %.2f ms spent, C NMT=%d", (time3-time2)/100.0f, NMTDataSignatureAnswer);
        LogF("    %.2f ms spent, D NMT=%d", (time4-time3)/100.0f, NMTDataSignatureAnswer);
        LogF("    TOTAL %.2f ms spent, NMT=%d", time4/100.0f, NMTDataSignatureAnswer);
#endif
      }
      break;
    case NMTFileSignatureAnswer:
      {
        FileSignatureAnswerMessage answer;
        answer.TransferMsg(ctx);
        NetworkPlayerInfo *info = GetPlayerInfo(from);
        if (!info) break;

        //NetLog("Client %s: File signature answer %x:'%s' level %d received, %d pending",cc_cast(info->name),answer._index,cc_cast(answer._fileName),answer._level,info->signatureChecks.CountPendingFiles());
/*
        if (answer._index >=0)
          RptF("FILE SIG INDEX INTEGRITY: %s -> %s", cc_cast(info->hashes.hashes[answer._index].fileName), cc_cast(answer._fileName));
*/
        // remove from the list of pending checks
        RString prefix;
        if ( !info->signatureChecks.Remove(answer._fileName,&prefix) )
        { // I have not asked this question!
          ReportWrongSignatures(from, answer._fileName, info, "FSA Not Pending");
          break;
        }

        // compute the answer hash
        DSHash answerHash;
        switch (answer._signature.Version())
        {
        case 1:
          {
            Temp<char> dummy;
            // only data content hash is signed (but pbo prefix is present in answer._dataToHash => extract it)
            answer.GetDataHash(answerHash._content);
            // new clients (start from rev.78326) contain DataSignatureAnswer matching index
            if (info->hashes.AddHash(answer._index, answer.GetPrefix(), answerHash._content, dummy, answer._signature.Version(), answer._level))
            {
              RString wsaReport = Format("FSA AddHash, ver=1, level=%d (%s)", answer._level, cc_cast(info->hashes.GetLastError()));
              ReportWrongSignatures(from, answer._fileName, info, wsaReport);
            }
          }
          break;
        case 2:
          {
            Temp<char> dummy;
          HashCalculator calculator;
          calculator.Add(answer._dataToHash.Data(), answer._dataToHash.Size());
          AutoArray<char> finalHash;
          calculator.GetResult(finalHash);
          answerHash._content.Realloc(finalHash.Data(), finalHash.Size()); //Ver2 hash
            if (answer._level!=2)
            {
              Temp<char> dataContentHash;
              answer.GetDataHash(dataContentHash);
              //AddLevel2Hash based on bank prefix
              if (info->hashes.AddHash(answer._index, answer.GetPrefix(), dataContentHash, answerHash._content, answer._signature.Version(), answer._level)) //AddHash based on bank prefix
              {
                RString wsaReport = Format("FSA AddHash, ver=2, level=%d (%s)", answer._level, cc_cast(info->hashes.GetLastError()));
                ReportWrongSignatures(from, answer._fileName, info, wsaReport);
              }
            }
            else if (info->hashes.AddHash(answer._index, answer.GetPrefix(), dummy, answerHash._content, answer._signature.Version(), answer._level) ) //AddHash based on bank prefix
            {
              RString wsaReport = Format("FSA AddHash, ver=2, level=%d (%s)", answer._level, cc_cast(info->hashes.GetLastError()));
              ReportWrongSignatures(from, answer._fileName, info, wsaReport);
            }
          }
          break;
        }

        if (answer._signature.Version()<_sigVerRequired || !DataSignatures::VerifySignature(answerHash, answer._signature))
        {
          RString wsaReport;
          if (answer._signature.Version()<_sigVerRequired) wsaReport = Format("FSA insufficient ver=%d", answer._signature.Version());
          else wsaReport = "FSA failed";
          ReportWrongSignatures(from, answer._fileName, info, wsaReport);
          break;
        }
        else
        {
            // check the answer has the same prefix
          if ( stricmp(answer.GetPrefix(),prefix)!=0 )
            {
            RString wsaReport = Format("FSA %s!=%s", cc_cast(answer.GetPrefix()), cc_cast(prefix));
              ReportWrongSignatures(from, answer._fileName, info, wsaReport);
              break;
            }
            
            }
          }
            break;
    case NMTHackedData:
      {
        HackedDataMessage answer;
        answer.TransferMsg(ctx);
        OnHackedData((HackType)answer._type, from, answer._filename);
      }
      break;
    case NMTCDKeyCheckAnswer:
#if _ENABLE_GAMESPY && _VERIFY_CLIENT_KEY
      {
        CDKeyCheckAnswerMessage answer;
        answer.TransferMsg(ctx);

        NetworkPlayerInfo *info = GetPlayerInfo(from);
        if (!info) break;

        info->cdKeyCheck.timeout = 0; //answer arrived before timeout elapsed

        sockaddr_in address;
        if (!GetClientAddress(from, address)) break;

        gcd_authenticate_user(
          GetGameID(), from, address.sin_addr.s_addr, info->challenge, answer._response,
          OnAuthorize, OnRefreshAuthorize, this);

        // Bastian: BEServer.ReceivedPlayerGUID() will be called as soon as the GameSpy cd-key hash is received from the client (after connecting).
        _beIntegration.PlayerGUIDReceived(info->dpid);
      }
#endif
      break;
    case NMTCDKeyRecheckAnswer:
#if _ENABLE_GAMESPY && _VERIFY_CLIENT_KEY
      {
        CDKeyRecheckAnswerMessage answer;
        answer.TransferMsg(ctx);

        gcd_process_reauth(GetGameID(), from, answer._hint, answer._response);
      }
#endif
      break;
    case NMTAskConnectVoice:
      {
        AskConnectVoiceMessage ask;
        ask.TransferMsg(ctx);

        NetworkPlayerInfo *info = GetPlayerInfo(from);
        if (!info) break;

//        ask._privateAddr;
        #define AB(v,i) ((v)>>((i)*8)&0xff)
        LogF("VoNLog: Server received AskConnectVoiceMessage with %d.%d.%d.%d:%d from %d",AB(ask._privateAddr,0),AB(ask._privateAddr,1),AB(ask._privateAddr,2),AB(ask._privateAddr,3),ask._voicePort,from);

        // do some tests vor GDebugVoNBank collecting system
        sockaddr_in clAdr;
        bool noClientAddr = !_server->GetClientAddress(from, clAdr) && (from!=BOT_CLIENT);
        if (noClientAddr) GDebugVoNBank.SetFeature(DebugVoNBank::VoNFeature_GetClientAddressFailed);

        int distAddr = ADDR(clAdr);

        info->_privateAddr = ask._privateAddr;
        info->_privatePort = ask._voicePort;
        info->_publicAddr = ask._publicAddr;
        info->_publicPort = ask._publicAddr==distAddr ? ntohs(clAdr.sin_port) : ask._voicePort; // port unknown, try the same port as private

        LogF("VoNLog:    .. public  %d.%d.%d.%d:%d from %d",AB(info->_publicAddr,0),AB(info->_publicAddr,1),AB(info->_publicAddr,2),AB(info->_publicAddr,3),info->_publicPort,from);
        LogF("VoNLog:    .. private %d.%d.%d.%d:%d from %d",AB(info->_privateAddr,0),AB(info->_privateAddr,1),AB(info->_privateAddr,2),AB(info->_privateAddr,3),info->_privatePort,from);

        int serverPublicAddr = 0; // TODO: handle clients on the server LAN differently
        for (int i=0; i<_players.Size(); i++)
        {
          const NetworkPlayerInfo &player = *_players[i];
          if (player.dpid==BOT_CLIENT) serverPublicAddr = player._publicAddr;
        }
        for (int i=0; i<_players.Size(); i++)
        {
          const NetworkPlayerInfo &player = *_players[i];
          if (player.dpid==from) continue;

          // same public ip => same LAN. Send private address
          int NetworkPlayerInfo::*addr = info->_publicAddr == player._publicAddr ? &NetworkPlayerInfo::_privateAddr : &NetworkPlayerInfo::_publicAddr;
          int NetworkPlayerInfo::*port = info->_publicAddr == player._publicAddr ? &NetworkPlayerInfo::_privatePort : &NetworkPlayerInfo::_publicPort;

          LogF("VoNLog: public %d.%d.%d.%d:%d of %d",AB(player._publicAddr,0),AB(player._publicAddr,1),AB(player._publicAddr,2),AB(player._publicAddr,3),player._publicPort,player.dpid);
          LogF("VoNLog: private %d.%d.%d.%d:%d of %d",AB(player._privateAddr,0),AB(player._privateAddr,1),AB(player._privateAddr,2),AB(player._privateAddr,3),player._privatePort,player.dpid);

          LogF("VoNLog: public %d.%d.%d.%d:%d of %d",AB(info->_publicAddr,0),AB(info->_publicAddr,1),AB(info->_publicAddr,2),AB(info->_publicAddr,3),info->_publicPort,info->dpid);
          LogF("VoNLog: private %d.%d.%d.%d:%d of %d",AB(info->_privateAddr,0),AB(info->_privateAddr,1),AB(info->_privateAddr,2),AB(info->_privateAddr,3),info->_privatePort,info->dpid);

          ConnectVoiceDirectMessage connect;
          connect._dpnid = from;
          connect._addr = BestAddress(connect._port,info,addr,port);
          connect._direct = true;
          SendMsg(player.dpid, &connect, NMFGuaranteed);
          LogF("VoNLog: Server sent %d.%d.%d.%d:%d about %d to %d",AB(connect._addr,0),AB(connect._addr,1),AB(connect._addr,2),AB(connect._addr,3),connect._port,from,player.dpid);

          connect._dpnid = player.dpid;
          connect._addr = BestAddress(connect._port,&player,addr,port);
          connect._direct = true;
          SendMsg(from, &connect, NMFGuaranteed);
          LogF("VoNLog: Server sent %d.%d.%d.%d:%d about %d to %d",AB(connect._addr,0),AB(connect._addr,1),AB(connect._addr,2),AB(connect._addr,3),connect._port,player.dpid,from);

        }
      }
      break;
    case NMTClientDiagToServer:
      {
        ClientDiagToServerMessage answer;
        answer.TransferMsg(ctx);
        RString logline = Format("DiagClient(%d): %s", from, cc_cast(answer._response));
        RptF(logline);
      }
      break;
    case NMTConnectVoiceDirect:
      {
      }
      break;
      case NMTOwnerChanged:
        {
          OwnerChangedMessage ackMsg;
          ackMsg.TransferMsg(ctx);
          LogF("Owner of %d:%" OID_FORMAT" changed", ackMsg._creator, ackMsg._id);
          NetworkId id(ackMsg._creator, ackMsg._id);
          NetworkObjectInfo *oInfo = GetObjectInfo(id);
          if (!oInfo)
          {
            RptFObjectNotFound(id, type, from);
            return;
          }

          ChangeOwnerFinal(oInfo, from, id);
        }
        break;
    default:
      //RptF("Server: Unhandled user message %s", cc_cast(GetMsgTypeName(type)));
      ConsoleF("Server: Unhandled user message %s", cc_cast(GetMsgTypeName(type)));            
      break;
  }

#if _ENABLE_PERFLOG
  int profileTime = PROFILE_SCOPE_NAME(OnMsg).TimeSpentNorm();
  static float profileTimeLimit = 10.0f; //ms
  if (profileTime/100.0f > profileTimeLimit)
  {
    #if _SUPER_RELEASE
      RptF("Warn: %.2f ms spent, NMT=%d", profileTime/100.0f, type);
      if (type==NMTNetworkCommand)
      {
        NetworkCommandMessage cmd;
        cmd.TransferMsg(ctx);
        RptF("      NetworkCommand: %d", cmd._type);
      }
    #else
      LogF("Warn: %.2f ms spent, NMT=%d", profileTime/100.0f, type);
    #endif
  }
#endif
}

void NetworkServer::ChangeOwnerFinal( NetworkObjectInfo * oInfo, int from, NetworkId &id )
{
  NetworkPlayerInfo *pInfo = GetPlayerInfo(oInfo->pendingOwner);
  if (pInfo)
  {
    PlayerIndex index = GetPlayerIndex(pInfo->dpid);
    NetworkPlayerObjectInfo *poInfo = oInfo->GetPlayerObjectInfo(index,pInfo->dpid);
    if (!poInfo->updating)
    {
      SendCreateMessage(pInfo->dpid, *oInfo, poInfo);
      poInfo->updating = true;
      pInfo->markAsUpdating(poInfo);
    }
    for (int cls=NMCUpdateFirst; cls<NMCUpdateN; cls++)
    {
      UpdateObject(pInfo, oInfo, (NetworkMessageClass)cls, NMFGuaranteed);
    }
  }
  else
  { // new owner has probably disconnected meanwhile
    oInfo->pendingOwner = BOT_CLIENT;
  }
  // reset times to ensure update from new owner will take place
  for (int cls=NMCUpdateFirst; cls<NMCUpdateN; cls++)
  {
    NetworkCurrentInfo &info = oInfo->currentUpdate[cls];
    if (info.message) info.message->time = Time(0); // do not use TIME_MIN - difference is undefined
  }

  RescanBestPosition(oInfo);

  if (oInfo->owner==from)
  {
    // Send message to new owner at last
    ChangeOwnerMessage msg;
    msg._creator = id.creator;
    msg._id = id.id;
    msg._owner = oInfo->pendingOwner;
    msg._disconnected = false;
    SendMsg(oInfo->pendingOwner, &msg, NMFGuaranteed);

    // Send messages waiting to be sent after owner is changed
    for (int i=0; i<oInfo->waitingMessages.Size(); i++)
    {
      WaitingMessageInfo *wMsgInfo = oInfo->waitingMessages[i];
      SendMsg(oInfo->pendingOwner, wMsgInfo->msg, wMsgInfo->type, NMFGuaranteed, NULL);
    }
    oInfo->waitingMessages.Clear();

    // forget anything sent to the old owner before (normally nothing should be sent to the owner?)
    oInfo->ResetPlayerObjectInfo(GetPlayerIndex(oInfo->owner),oInfo->owner);
    // if we send anything to the new owner before, forget it now
    oInfo->ResetPlayerObjectInfo(GetPlayerIndex(oInfo->pendingOwner),oInfo->pendingOwner);

#if DIAG_MP_OBJECT_LIFE
    GObjLifeJournal.Add(id, Format("(%.3f) Owner changed: from=%d to=%d",Glob.time.toFloat(), oInfo->owner, oInfo->pendingOwner));
#endif
    if (DebugMeBoy(id))
    {
      LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} Owner changed from %d to %d", oInfo->id.creator.CreatorInt(), oInfo->id.id, oInfo->owner, oInfo->pendingOwner);
    }

    oInfo->owner = oInfo->pendingOwner;
    oInfo->pendingOwner = NO_CLIENT;
  }
  else
  {
    RptF("Server: OwnerChanged of %d:%" OID_FORMAT" arrived from non owner %d", id.creator.CreatorInt(), id.id, from);
  }
}

void NetworkServer::CheckItemsOwner(const NetworkMessageUpdateVehicleAI *msg, int newOwner)
{
  const NetworkMessageUpdateInventoryItem *invItemUpdate = dynamic_cast<const NetworkMessageUpdateInventoryItem *>(msg);
  if (invItemUpdate)
  {
    // attachments
    for (int i=0; i<invItemUpdate->_attachments.Size(); i++)
    {
      const NetworkId &id = invItemUpdate->_attachments[i];
      if (!id.IsNull()) CheckItemOwner(id, newOwner);
    }
  }

  // items cargo
  const NetworkMessageUpdateSupply *supply = static_cast<const NetworkMessageUpdateSupply *>(msg->_supply.GetRef());
  if (supply)
  {
    for (int i=0; i<supply->_itemsCargo.Size(); i++)
    {
      const NetworkId &id = supply->_itemsCargo[i];
      if (!id.IsNull()) CheckItemOwner(id, newOwner);
    }
  }

  // magazines loaded in weapon
  const NetworkMessageUpdateWeapon *weapon = dynamic_cast<const NetworkMessageUpdateWeapon *>(msg);
  if (weapon)
  {
    for (int i=0; i<weapon->_muzzles.Size(); i++)
    {
      const NetworkMessageUpdateMuzzle *muzzle = static_cast<const NetworkMessageUpdateMuzzle *>(weapon->_muzzles[i].GetRef());
      if (muzzle)
      {
        const NetworkId &id = muzzle->_magazineWanted;
        if (!id.IsNull()) CheckItemOwner(id, newOwner);
      }
    }
  }
}

void NetworkServer::CheckItemOwner(const NetworkId &id, int newOwner)
{
  const NetworkObjectInfo *info = GetObjectInfo(id);
  if (!info || info->owner == newOwner) return; // no change

  // change my owner
  ChangeOwner(id, info->owner, newOwner);
  // check subitems recursively
  const NetworkCurrentInfo &cInfo = info->currentUpdate[NMCUpdateGeneric];
  const NetworkMessageUpdateVehicleAI *msg = dynamic_cast<const NetworkMessageUpdateVehicleAI *>(cInfo.message.GetRef());
  if (msg) CheckItemsOwner(msg, newOwner);
}

/*!
\patch 5178 Date 11/1/2007 by Bebul
- Fixed: Default handling of different data detected was stricter then intended, resulting in a kick of players.
*/
void NetworkServer::OnHackedData(HackType type, int dpid, RString filename)
{
  RptF("NetworkHackedData type=%d player=%d file=%s", (int)type, dpid, cc_cast(filename));
  const PlayerIdentity *identity = FindIdentity(dpid);
  if (!identity) return;

#if _ENABLE_DEDICATED_SERVER
  switch (type)
  {
  case HTUnsignedData:
    if (OnEvent(SEOnUnsignedData, (float)identity->playerid, filename)) return;
    break;
  case HTHackedData:
    if (OnEvent(SEOnHackedData, (float)identity->playerid, filename)) return;
    break;
  }
#endif

#if !SIGNATURES_TEST_ONLY
  KickOff(dpid, KORFade);
#endif
}

#if _VERIFY_KEY
void NetworkServer::OnDoubleIdDetected(const PlayerIdentity &newIdentity, const PlayerIdentity &oldIdentity)
{
  // report problems on global channel
  RString message = LocalizeString(IDS_FADE_AWAY);
  RString senderName = LocalizeString(IDS_FADE_REMMEMBER);
  RefArray<NetworkObject> dummy;
  GNetworkManager.Chat(CCSystem, senderName, dummy, message);
  GChatList.Add(CCSystem, senderName, message, false, true);

#if _ENABLE_DEDICATED_SERVER
  if (OnEvent(SEDoubleIdDetected, (float)newIdentity.playerid, (float)oldIdentity.playerid)) return; // handled
  if (_kickDuplicate) KickOff(newIdentity.dpnid, KORKick);
#endif
}
#endif

void NetworkServer::OnPlayerDisconnected(NetworkPlayerInfo *info)
{
  if (GNetworkPlayerDisconnected.GetNil()) return;
  GameState *state = GWorld->GetGameState();
  if (state)
  {
    GameVarSpace vars(false);
    int gshandle = state->BeginContext(&vars);
    PlayerIdentity *identity = FindIdentity(info->dpid);
    RString uid = identity ? identity->id : RString();
    state->VarSetLocal("_id", (float)identity->playerid, true);
    state->VarSetLocal("_uid", uid, true);
    state->VarSetLocal("_name", info->name, true);
    Object *agent = dyn_cast<Object>(_parent->GetObject(info->person));
    state->VarSetLocal("_agent", GameValueExt(agent), true);
    
#if USE_PRECOMPILATION
    if (GNetworkPlayerDisconnected.GetType() == GameCode)
    {
      GameDataCode *code = static_cast<GameDataCode *>(GNetworkPlayerDisconnected.GetData());
      if (code->IsCompiled() && code->GetCode().Size() > 0)
        state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    }
    else
#endif
    if (GNetworkPlayerDisconnected.GetType() == GameString)
    {
      // make sure string is not destructed while being evaluated
      RString code = GNetworkPlayerDisconnected;
      if (code.GetLength() > 0)
        state->EvaluateMultiple(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    }
    state->EndContext(gshandle);
  }

}

void NetworkServer::CallOnPlayerDisconnected()
{
  for (int i=0; i<_players.Size(); i++)
  {
    // call only for players which are still playing the mission
    if (_players[i]->clientState == NCSPlaying)
    {
      OnPlayerDisconnected(_players[i]);
    }
  }
}
int NetworkServer::FindOwner(const NetworkId &id)
{
  RefD<NetworkObjectInfo> dInfo;
  if (_objects.get(id,dInfo))
  {
    return dInfo->owner;
  }

  LogF("Owner of %d:%" OID_FORMAT" not known - searching player",id.creator,id.id);
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &pInfo = *_players[i];
    if (pInfo.person == id)
    {
      // change owner
      LogF("Owner not known - player found");
      return pInfo.dpid;
    }
  }
  // not found
  return 0;
}

void NetworkServer::OnMessageClientInput(int from, NetworkMessageType type, NetworkMessageContext &ctx)
{
  // client input has very special handling, no need to check for any network id, it is send only from clients to the server
  NetworkPlayerInfo *info = GetPlayerInfo(from);
  if (info && info->_clientInput)
  {
    info->_clientInput->TransferMsg(ctx);
  }
}

DWORD NetworkServer::SendMsg
(
  int to, NetworkSimpleObject *object, NetMsgFlags dwFlags
)
{
  // check if message can be sent
  NetworkPlayerInfo *info = GetPlayerInfo(to);
  if (!info)
  {
    RptF("Server: cannot send message - player %d is not known.", to);
    return 0xFFFFFFFF;
  }
  if (info->clientState < NCSConnected && object->GetNMType() >= NMTFirstVariant)
  {
    RptF("Server: cannot send message - player's %s messages are not registred yet.", (const char *)info->name);
    return 0xFFFFFFFF;
  }
  return NetworkComponent::SendMsg(to, object, dwFlags);
}

void NetworkServer::EnqueueMsg
(
  int to, NetworkMessage *msg,
  NetworkMessageType type,
  NetworkPlayerObjectInfo *poInfo
)
{
  NetworkPlayerInfo *info = GetPlayerInfo(to);
  if (!info)
  {
    Fail("PlayerInfo");
    return;
  }
  // if 
  ServerQueueItem *item = new ServerQueueItem();
  item->type = type;
  item->msg = msg;
  item->poInfo = poInfo;
  info->_messageQueue.Add(item);
}

void NetworkServer::EnqueueMsgNonGuaranteed
(
  int to, NetworkMessage *msg,
  NetworkMessageType type,
  const RefR<NetMessage, RefCountSafe> &dependOn
)
{
  NetworkPlayerInfo *info = GetPlayerInfo(to);
  if (!info)
  {
    Fail("PlayerInfo");
    return;
  }
  // if 
  ServerQueueItem *item = new ServerQueueItem();
  item->type = type;
  item->msg = msg;
  item->dependOn = dependOn;
  info->_messageQueueNonGuaranteed.Add(item);
}


Vector3 NetworkServer::RandomPlayerPos() const
{
  if (_players.Size())
  {
    return _players[rand() % _players.Size()]->cameraPosition;
  }
  return VZero;
}

void NetworkServer::PerformIntegrityInvestigations()
{
  // scan all players
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &pi = *_players[i];

    if (pi.dpid != BOT_CLIENT)
    {
      // regular checking
      DWORD time = GlobalTickCount();
      if (  time > pi.regularCheckNext && pi.clientState>=NCSGameLoaded && pi.clientState<=NCSPlaying && !pi.hashes.IsCollecting() )
      {
        bool handled = false;
#if _ENABLE_DEDICATED_SERVER
        const PlayerIdentity *identity = FindIdentity(pi.dpid);
        if (identity)
        {
          handled = OnEvent(SERegularCheck, identity->playerid, pi.regularCheckOrder);
          pi.regularCheckOrder++;
        }
#endif
        if (!handled && _verifySignatures)
        {
          // next random question
            if (rand()%2)
            { // use DataSignatureAsk message
          int index = toIntFloor(GRandGen.RandomValue() * 0x8000);
#if _ENABLE_CHEATS
              static int forceValue = -1;
              if (forceValue>=0)
              {
                index = forceValue;
              }
#endif
              int level = pi.hashes.ActualCheckLevel();
          CheckSignature(pi.dpid, index, level);
        }
            else
            { // use FileSignatureAsk message
              int level = pi.hashes.ActualCheckLevel();
              RString filename = pi.hashes.RandomUsedFile(RandomPlayerPos(), level);
#if _ENABLE_CHEATS
              static bool doIt = false;
              if (doIt)
              {
                filename = "ca\\air_d_baf\\aw159_mainrotorblurred_baf.p3d";
                LogF("RandomUsedFile: %s", cc_cast(filename));
              }
#endif
              QFBank *bank = QFBankQueryFunctions::AutoBank(filename);
              if (bank)
              {
                CheckSignature(pi.dpid, filename, bank->GetPrefix(),level);
      }
#if _ENABLE_CHEATS
              else if (!filename.IsEmpty()) 
                LogF("RandomUsedFile test SKIPPED, bank not found! file: %s", cc_cast(filename));
#endif
            }
          }
// The following is only for debugging purposes, define used only not to forget to set more test back to normal in some commit
#define _DEBUG_MORE_SIGNATURE_TESTS (_ENABLE_CHEATS && 0)
#if _DEBUG_MORE_SIGNATURE_TESTS
        // more test when debugging
        pi.regularCheckNext = time + 500 + toInt(1000 * GRandGen.RandomValue()); // 1 s - 3 s
#else
        // Note: timing 80s - 160s is no longer used for regular signature checks, but deep (level=1) checks
        //       regular signature checks are now more frequent, but only shallow (level=0)
        pi.regularCheckNext = time + 5000 + toInt(5000 * GRandGen.RandomValue()); // 5 s - 10 s
#endif
      }

      if (_verifySignatures && !pi.ClientStateBusy())
      {
        ReportHackedData reportFunc(pi, *this);
#if DEBUG_SIGNATURES
        pi.errorInfo = Format("SCh Timeout (cs=%d,ss=%d,#=%d,%d)", pi.clientState, _serverState, pi.hashes.Hashes().Size(),pi.hashes.IsCollecting());
        pi.signatureChecks.CheckTimeouts(reportFunc, pi.errorInfo);
#else
        pi.signatureChecks.CheckTimeouts(reportFunc);
#endif
#if DEBUG_SIGNATURES
        pi.errorInfo = "BI Timeout";
#endif
        pi.hashes.CheckTimeouts(reportFunc);
      }
#if DEBUG_SIGNATURES
      pi.errorInfo = "";
#endif
    }
  }
}

void PendingSignatureChecks::CheckTimeouts(CheckSignatureTimeoutFunc &func
#if DEBUG_SIGNATURES
         , RString &errorInfo
#endif
)
{
  DWORD time = GlobalTickCount();
  for (int i=0; i<pendingDataChecks.Size(); i++)
  {
    if (time > pendingDataChecks[i].checkTimeout)
    {
#if DEBUG_SIGNATURES
      errorInfo = errorInfo + Format(" - ix=%d", pendingDataChecks[i].answerIndex);
#endif
      if (func()) // do the action
        return; 
      pendingDataChecks.Delete(i);
      i--;
    }
  }
  for (int i=0; i<pendingFileChecks.Size(); i++)
  {
    if (time > pendingFileChecks[i].checkTimeout)
    {
#if DEBUG_SIGNATURES
      errorInfo = errorInfo + Format(" - file=%s", cc_cast(pendingFileChecks[i].filename));
#endif
      if (func()) // do something the action
        return; 
      pendingFileChecks.Delete(i);
      i--;
    }
  }
}

void PendingSignatureChecks::DelayCheckTimeouts(DWORD time, const char *playerName)
{
  NetLog("Signature Timeout delayed %.1f seconds for %s", time/1000.0f,playerName);
  for (int i=0; i<pendingDataChecks.Size(); i++) pendingDataChecks[i].checkTimeout += time;
  for (int i=0; i<pendingFileChecks.Size(); i++) pendingFileChecks[i].checkTimeout += time;
}

#if DIAG_INITIAL_SIGNATURE_CHECK_TIME
  DWORD GStartTime = 0;
#endif

void PendingSignatureChecks::UpdateInitialCheckTimeout()
{
  DWORD time = GlobalTickCount()+90000; //maximal timeout limit
  for (int i=0; i<pendingDataChecks.Size(); i++)
  {
    if (pendingDataChecks[i].answerIndex==CollectedHashes::CollectEndIndex)
    {
      DWORD timeout = pendingDataChecks[i].checkTimeout + 600; //minFPS=5, maxFramesForAnswer=3, time=(3/5)*1000=600
      pendingDataChecks[i].checkTimeout = (timeout<time) ? timeout : time;
      break;
    }
  }
}

bool NetworkServer::ProcessHackedData(NetworkPlayerInfo &pi)
{
#if _ENABLE_DEDICATED_SERVER
  RString reportBase = Format(LocalizeString(IDS_SIGNATURE_CHECK_TIMED_OUT), cc_cast(pi.name));
#if DEBUG_SIGNATURES
  if (!pi.errorInfo.IsEmpty())
    NetLog( reportBase + Format(" (%s)", cc_cast(pi.errorInfo)) );
#endif
  ServerMessage(reportBase);
#endif
  OnHackedData(HTHackedData, pi.dpid, RString()); // filename unknown
  return false; //continue
}

bool ReportHackedData::operator ()()
{
  // question timed out
  return _server.ProcessHackedData(_pi);
}

NetworkPlayerInfo *NetworkServer::GetPlayerInfo(int dpid)
{
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.dpid == dpid)
      return &info;
  }
  return NULL;
}

Time NetworkServer::GetPlayerJoined(int dpid) const
{
  for (int i=0; i<_players.Size(); i++)
  {
    const NetworkPlayerInfo &info = *_players[i];
    if (info.dpid == dpid) return info.joined;
  }
  return TIME_MAX;
}

static bool MatchIP(const char *ipString, unsigned int ip)
{
  if (!strcmp(ipString,"localhost")) return ip==((127U<<24)|1);
  // parse string to ip address
  unsigned a,b,c,d;
  if (sscanf(ipString,"%u.%u.%u.%u",&a,&b,&c,&d)!=4) return false;
  unsigned ipVal = (a<<24)|(b<<16)|(c<<8)|(d<<0);
  return ipVal==ip;

}
/*!
\patch 1.25 Date 10/01/2001 by Jirka
- Added: Send chat message "Player XXX connecting" when login process is initiated.
*/

NetworkPlayerInfo *NetworkServer::OnPlayerCreate(int dpid, bool privateSlot, const char *name, unsigned long inaddr)
{
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  bool server = _dedicated && dpid == BOT_CLIENT;
  if (server) name = "__SERVER__";
#else
  bool server = false;
#endif
//}

  char playerName[256];
  strcpy(playerName, name);

  bool ok = false;
  int suffix = 1;
  while (!ok)
  {
    ok = true;
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.dpid == dpid) continue;
      if (stricmp(info.name, playerName) == 0)
      {
        sprintf(playerName, "%s (%d)", name, ++suffix);
        ok = false;
        break;
      }
    }
  }

  {
    NetworkPlayerInfo *info = GetPlayerInfo(dpid);
    if (info)
    {
      DoAssert(strcmp(info->name, playerName) == 0);
      return info;
    }
  }

  int index = _players.Add(new NetworkPlayerInfo());
  NetworkPlayerInfo &info = *_players[index];
  info.dpid = dpid;
  info.dvid = dpid;
  info.clientState = NCSCreated;
  info.channel = CCNone;  
  info.name = playerName;
  info.cameraPosition = InvalidCamPos;
  info.cameraPositionTime = TIME_MIN;
  info._busyStartTime = 0;
#if _VBS3
  info.cameraDirection = VForward;
  info.cameraFov = 0.75;
#endif
  info.privateSlot = privateSlot;
  info.connectionProblemsReported = false;
  info.kickedOff = false;
  info.nextQuestionId = 1;
  info._jipProcessed = false;
  //LogF("_jip=false dpnid=%d setting in %s on line %d\n", dpid, __FILE__, __LINE__);
  info.joined = TIME_MAX;
  info._publicAddr = 0;
  info._publicPort = 0;
  info._privateAddr = 0;
  info._privatePort = 0;
  info._clientInput = new ClientInput(false);

  info.regularCheckNext = GlobalTickCount() + 40000 + toInt(40000 * GRandGen.RandomValue()); // initial delay 40 s - 80 s
  info.regularCheckOrder = 1;

  info._loginFailedCount = 0;
  info._loginDisabledUntil = 0;
  info._isLocal = false;


#if _ENABLE_DEDICATED_SERVER
  ConstParamEntryPtr entry = _serverCfg.FindEntry("localClient");
  if (entry)
  {
    if (entry->IsArray())
    {
      for (int i=0; i<entry->GetSize(); i++)
      {
        RString ip = (*entry)[i];
        if (MatchIP(ip,ntohl(inaddr))) {info._isLocal = true;break;}
      }
    }
    else
    {
      RString ip = entry->GetValue();
      if (MatchIP(ip,ntohl(inaddr))) info._isLocal = true;
    }
  }
#endif

#if LOG_PLAYERS
LogF
(
  "Server: Player info added - name %s, id %d (total %d identities, %d players)",
  (const char *)playerName, dpid, _identities.Size(), _players.Size()
);
#endif

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (_dedicated)
  {
    info.motdIndex = -1;
    if (dpid != BOT_CLIENT && _motd.Size() > 0) info.motdIndex = 0;
  }
#endif
//}

  // register formats
  for (int mt=NMTFirstVariant; mt<NMTN; mt++)
  {
    NetworkMessageFormatBase *item = GetFormat(mt);

    // create message
    NetworkMessageType type = item->GetNMType(NMCCreate);
    Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, this, dpid, MSG_SEND);

    TMError err;

    NetworkMessageMsgFormat *message = (NetworkMessageMsgFormat *)msg.GetRef();
    message->_index = mt;

    err = item->TransferMsg(ctx);
    if (err != TMOK) continue;

    // send message
    SendMsg(dpid, msg, type, NMFGuaranteed, NULL);
  }

  // enable sending messages
  SetClientState(dpid, NCSConnected);

  if (_verifySignatures && dpid != BOT_CLIENT)
  {
    // send accepted keys
    for (int i=0; i<_acceptedKeys.Size(); i++)
    {
      AcceptedKeyMessage key(_acceptedKeys[i]);
      SendMsg(dpid, &key, NMFGuaranteed);
    }
    // send additional signed files
    AdditionalSignedFilesMessage files(_additionalSignedFiles);
    SendMsg(dpid, &files, NMFGuaranteed);
    // TODO: mark required signatures
  }

  #if _USE_BATTL_EYE_SERVER
  if (dpid != BOT_CLIENT)
  {
    sockaddr_in address;
    _server->GetClientAddress(dpid, address);
    _beIntegration.AddPlayer(dpid, address.sin_addr.s_addr, address.sin_port, info.name);
  }
  #endif

  PlayerMessage msg;
  msg._player = dpid;
  msg._name = info.name;
  msg._server = server;
  msg._vonDisabled = _parent->IsVoNDisabled();
  msg._vonCodecQuality = _parent->VoNCodecQuality();  
#if !_USE_BATTL_EYE_SERVER
  msg._battlEye = false;
#else
  msg._battlEye = _beIntegration.IsEnabled();
#endif
  msg._sigVerRequired = _sigVerRequired;
  msg._updatesGuaranteed = _updatesGuaranteed;
  msg._steamServerId = 0;
  msg._steamSecure = false;
#if _VERIFY_CLIENT_KEY
  if (SecureIdRequired()>0)
  {
    msg._steamServerId = GetSecureRandom();
    msg._regURL = "https://is.bistudio.com:8181/BICDKeys/resources/bicdkeys.cdkeys/";
    if (msg._steamServerId==0) msg._steamServerId = 1; // avoid zero, used as a special case: signature not required
    info.certChallenge = msg._steamServerId;
  }
#endif
  //  msg._inaddr = inaddr;
  SendMsg(dpid, &msg, NMFGuaranteed);

  // Send ServerInfo message
  ServerInfoMessage siMsg;
  siMsg._hostname = _server->GetSessionName();
  SendMsg(dpid, &siMsg, NMFGuaranteed);

  // Send chat message about player is connecting
  if (dpid != BOT_CLIENT)
  {
	/*
    if (!ChannelDisabled(CCSystem))
    {
    //char message[256];
    RefArray<NetworkObject> dummy;
    LocalizedChatMessage chat;
      chat._channel = CCSystem;
    // format
    chat._args.Add(LocalizedString(LocalizedString::Stringtable,"STR_MP_CONNECTING"));
    // arguments
    chat._args.Add(LocalizedString(LocalizedString::PlainText,info.name));
    for (int i=0; i<_players.Size(); i++)
    {
      if (_players[i]->clientState >= NCSConnected)
        SendMsg(_players[i]->dpid, &chat, NMFGuaranteed);
    }
	}
	*/
  }

  // send player int and name to himself
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (_dedicated)
  {
    if (server)
    {
      ConsoleF(LocalizeString(IDS_DS_SERVER_IDENTITY_CREATED));
    }
    else
    {
      ConsoleF(LocalizeString(IDS_DS_PLAYER_CONNECTING), cc_cast(info.name));
    }
  }
#endif
//}

  // set the starting number of private and public slots on DS

  //{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (IsDedicatedServer())
  {
#if _GAMES_FOR_WINDOWS
    // update of max. number of players
    int perClient = 32000;
    int round = 1;
    int maxPlayers = GetMaxPlayersSafe(perClient, round);
    int GetDSPrivateSlots();
    int maxPrivate = GetDSPrivateSlots();
    saturateMin(maxPrivate, maxPlayers);
    UpdateMaxPlayers(maxPlayers, maxPrivate);
#else
    UpdateMaxPlayers(GetMaxPlayers(), 0);
#endif
  }
#endif
  //}

  return &info;
}

void NetworkServer::UpdateAdminState()
{
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  // send also info about new admin and gameMaster state to all clients
  // clients need to know whether there is admin on server
  // prepare message
  ServerAdminMessage samsg;
  samsg._admin = _admin;
  samsg._gameMaster = _gameMaster;
  // update all players and send them fresh admin message
  for (int i=0; i<_identities.Size(); i++)
  {
    PlayerIdentity &pi = _identities[i];
    pi._rights &= ~(PRAdmin|PRVotedAdmin);
    if (pi.dpnid==_gameMaster)
    {
      pi._rights |= _admin ? PRVotedAdmin : PRAdmin;
    }
    SendMsg(pi.dpnid, &samsg, NMFGuaranteed);
  }
  DWORD updatePingTime = GlobalTickCount() + 1000;
  if (_pingUpdateNext>updatePingTime) _pingUpdateNext = updatePingTime;
#endif
//}
}

void NetworkServer::OnPlayerDestroy(int dpid)
{
  if (dpid == BOT_CLIENT)
  {
    Fail("Not implemented.");
    // new bot client must be selected
    return;
  }

#if _ENABLE_GAMESPY && _VERIFY_CLIENT_KEY
  gcd_disconnect_user(GetGameID(), dpid);
#endif

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  RString name;
  if (dpid == _gameMaster)
  {
    for (int i=0; i<_players.Size(); i++)
      if (_players[i]->dpid == _gameMaster)
      {
        name = _players[i]->name;
        break;
      }
  }
#endif
//}

  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  NetworkId playerPerson;
  if (info)
  {
	/*
    if (!ChannelDisabled(CCSystem))
    {
    LocalizedFormatedString message;
    message.SetFormat(LocalizedString::Stringtable,"STR_MP_DISCONNECT");
    message.AddArg(LocalizedString::PlainText,info->name);
    RefArray<NetworkObject> dummy;
      GNetworkManager.LocalizedChat(CCSystem, "", dummy, message);
      GChatList.Add(CCSystem, NULL, message.GetLocalizedValue(), false, true);
    }
	*/
    if (info->clientState == NCSPlaying)
    {
      OnPlayerDisconnected(info);
    }

    playerPerson = info->person;

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    if (_dedicated) ConsoleF(LocalizeString(IDS_DS_PLAYER_DISCONNECTED), cc_cast(info->name));
#endif
//}

    _squadChecks.ForEachWithDelete([dpid](CheckSquadObject *check){return check->IsPlayer(dpid);});

    // delete player info
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.dpid == dpid)
      {
#if LOG_PLAYERS
LogF
(
  "Server: Player info removed - name %s, id %d (total %d identities, %d players)",
  (const char *)info.name, dpid, _identities.Size(), _players.Size() - 1
);
#endif
        _players.Delete(i);
        break;
      }
    }
    if (_players.Size() == 0)
    {
      _nextPlayerID = 2; // value 1 is reserved for static objects
      _votings.Clear();
    }
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    else if (_dedicated && _players.Size() == 1)
    {
      _votings.Clear();
    }
#endif
//}
  }

#if _USE_BATTL_EYE_SERVER
  _beIntegration.PlayerDisconnected(dpid);
#endif

  DisconnectPlayer(dpid, playerPerson);

  #if _ENABLE_DEDICATED_SERVER
    if (_dedicated)
    {
      _votings.OnPlayerDeleted(dpid);
    }
  #endif

  LogoutMessage logout;
  logout._dpnid = dpid;
  for (int i=0; i<_identities.Size(); i++)
  {
    if (_identities[i].dpnid == dpid)
    {
#if _ENABLE_DEDICATED_SERVER
      OnEvent(SEOnUserDisconnected, _identities[i].playerid);
#endif

#if LOG_PLAYERS
LogF
(
  "Server: Identity removed - name %s, id %d (total %d identities, %d players)",
  (const char *)_identities[i].name, dpid, _identities.Size() - 1, _players.Size()
);
#endif
      _identities.Delete(i);
      // check if player info was deleted properly
      NetworkPlayerInfo *check = GetPlayerInfo(dpid);
      if (check)
      {
        RptF
        (
          "Server error: identity deleted, player info still exist (%s, id %d)",
          (const char *)check->name, dpid
        );
      }

      _votings.Check(this);
      break;
    }
  }
  for (int i=0; i<_identities.Size(); i++)
  {
    PlayerIdentity &player = _identities[i];
    SendMsg(player.dpnid, &logout, NMFGuaranteed);
  }

//{ QUERY & REPORTING SDK
#if _ENABLE_DEDICATED_SERVER
  if (IsDedicatedServer())
  {
    NetworkClient *client = _parent->GetClient();
    DoAssert(client && client->GetPlayer() != logout._dpnid);
    // send new identity to bot client
    SendMsg(client->GetPlayer(), &logout, NMFGuaranteed);
  }
#endif
//}

  UpdateMatchmakingDescription(false);

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (_dedicated)
  {
    if (dpid == _gameMaster)
    {
      ConsoleF(LocalizeString(IDS_DS_ADMIN_LOGGED_OUT), (const char *)name);

      _gameMaster = AI_PLAYER;
      _sessionLocked = false;
      _debugOn.Clear();

      // on Xbox admin disconnect means nothing
        if (_serverState == NSSSelectingMission && _mission.GetLength() == 1 && _mission[0] == '?')
        {
          // enable other players vote mission or continue with mission defined in config
          _mission = "";
          _restart = false;
          _reassign = false;
        }
      UpdateAdminState();

    }
  }
#endif
//}

  if (_serverState == NSSSelectingMission)
  {
    // new clients should get mission voting echo as necessary, so that they can see voting
    MissionVotingEcho();
  }
#if _ENABLE_MP
  // update remote mute lists
  for (int i=0; i<_identities.Size(); i++)
  {
    SendMuteList(_identities[i]);
  }
#endif

}

void NetworkServer::SetEstimatedEndTime(Time time)
{
  _missionHeader._estimatedEndTime = time;
  _missionHeader._updateOnly = true;
  _serverTime = ::GetTickCount() - _serverTimeInitTickCount;
  _secondsToEstimatedEnd = time.toInt() - Glob.time.toInt();
  _serverTimeOfEstimatedEnd = _serverTime + _secondsToEstimatedEnd;
  _nextServerTimeUpdate = _serverTime; //update endTime (and serverTime) to clients as soon as possible

  // send mission description and roles
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.clientState < NCSConnected) continue;
    SendMsg(info.dpid, &_missionHeader, NMFGuaranteed);
  }
}

NetworkObjectInfo *NetworkServer::GetObjectInfo(const NetworkId &id) const
{
  // _objects.get variant does bot cause the refcount to be changed
  if (const RefD<NetworkObjectInfo> *dInfo=_objects.get(id))
  {
    return *dInfo;
  }
  return NULL;
}

Vector3 NetworkServer::GetObjectPosition( NetworkId obj )
{
  const NetworkObjectInfo *info = GetObjectInfo(obj);
  if (!info) return VZero;
  return info->object->GetCurrentPosition();
}

int NetworkServer::GetOwner(const NetworkId &id) const
{
  const NetworkObjectInfo *info = GetObjectInfo(id);
  if (!info) return 0;

  const PlayerIdentity *identity = FindIdentity(info->owner);
  if (!identity) return 1;

  return identity->playerid;
}

bool NetworkServer::SetOwner(const NetworkId &id, int clientId)
{
  const NetworkObjectInfo *info = GetObjectInfo(id);
  if (!info) return false;

  const PlayerIdentity *identity = FindIdentityByPlayerId(clientId);
  if (!identity) return false;

  if (info->owner == identity->dpnid) return false;

  ChangeOwner(id, info->owner, identity->dpnid);
  return true;
}

bool NetworkServer::StatusChat(int clientId, RString text, RString colorClass)
{
  const PlayerIdentity *identity = FindIdentityByPlayerId(clientId);
  if (!identity) return false;

  StatusChatMessage msg;
  msg._text = text; msg._color = colorClass;
  SendMsg(identity->dpnid, &msg, NMFNone);
  return true;
}

NetworkObjectInfo *NetworkServer::OnObjectCreate(const NetworkId &id, int owner, NetworkMessage *msg, NetworkMessageType type)
{
  if (NetworkObjectInfo *info = GetObjectInfo(id))
  {
    /*
    if (info->owner!=owner)
    {  // ownership change not possible in client-server architecture
      return nullptr;
    }
    */
    // create message redefined
    info->create.from = owner;
    info->create.message = msg;
    info->create.type = type;
#if DIAG_MP_OBJECT_LIFE
    GObjLifeJournal.Add(id, Format("(%.3f) Redefine create message: from=%d type=%d",Glob.time.toFloat(), owner,type));
#endif
    return info;
  }

  NetworkObjectInfo *info = new NetworkObjectInfo;
  info->id = id;
  info->owner = owner;
  info->create.from = owner;
  info->create.message = msg;
  info->create.type = type;
  info->pendingOwner = NO_CLIENT; //no pending
  for (int i=0; i<_indexToPlayerId.Size(); i++)
  {
    info->CreatePlayerObjectInfo(PlayerIndex(i),_indexToPlayerId[i]);
  }
  DoAssert(_indexToPlayerId.Size()==info->playerToObjects.Size());
  _objects.put(info);

#if DIAG_MP_OBJECT_LIFE
  GObjLifeJournal.Add(id, Format("(%.3f) Create: owner=%d type=%d",Glob.time.toFloat(), owner,type));
#endif
  return info;
}

/*!
\patch_internal 1.28 Date 10/29/2001 by Jirka
- Fixed: update objects only by messages by object's owner
*/
NetworkObjectInfo *NetworkServer::OnObjectUpdate(NetworkId &id, int from, NetworkMessage *msg, NetworkMessageType type, NetworkMessageClass cls)
{
  DoAssert(!msg->HasDiff())
  NetworkObjectInfo *oInfo = GetObjectInfo(id);
  if (!oInfo)
  {
    // if the update is from the owner and the object is listed as deleted, ignore the update
    NetworkDeletedObjectInfo *delInfo = _deletedObjects.get(id);
    if (delInfo && delInfo->owner!=from)
    {
      RptFObjectNotFound(id, type, from);
    }
    return NULL;
  }

  if (oInfo->owner != from)
  {
    bool allowNonOwner = false;
    if (from==BOT_CLIENT)
    {
      allowNonOwner = true;
    }
    if (!allowNonOwner)
    {
      RptF("Server: Update of object %d:%" OID_FORMAT" arrived from nonowner", id.creator.CreatorInt(), id.id);
      return NULL;
    }
  }

  bool rejectedFromClient = from!=BOT_CLIENT && oInfo->rejected[cls];
  if (rejectedFromClient)
  {
    LogF("Received rejected %d for %" OID_FORMAT"",cls,oInfo->id.id);
  }

  NetworkCurrentInfo &info = rejectedFromClient ? oInfo->currentRejected[cls] : oInfo->currentUpdate[cls];

  NetworkMessageFormatBase *format = GetFormat(/*from, */type);
  if (!format)
  {
    RptF("Server: Bad message %d(%s) format", (int)type, cc_cast(GetMsgTypeName(type)));
    return NULL;
  }
  
  if (info.from==from && info.message && info.message->time > msg->time)
  {
    if (info.from!=from)
    {
      LogF("Discarded different owner message for %" OID_FORMAT"(%d) because of time. %d!=%d",oInfo->id.id,rejectedFromClient,info.from,from);
    }

    // do not update
    if (DiagLevel >= 1)
      DiagLogF("Server: update message is old (%.3f s)", info.message->time - msg->time);
    return NULL;
  }

  if (info.message && info.message->time > msg->time)
  {
    LogF("Passed old time for %" OID_FORMAT"(%d). %d!=%d",oInfo->id.id,rejectedFromClient,info.from,from);
  }
  
  if (info.from!=from && info.message.NotNull())
  {
    LogF("Changing from %d(%d) for %" OID_FORMAT" %d to %d",cls,rejectedFromClient,oInfo->id.id,info.from,from);
  }


  info.from = from;
  info.type = type;
  info.message = msg;

  RescanBestPosition(oInfo);

  // invalidate cached error
  for (int i=0; i<oInfo->playerToObjects.Size(); i++)
  {
    NetworkPlayerObjectInfo *poInfo = oInfo->playerToObjects[i];
    if (poInfo) poInfo->updates[cls].errorValid = false;

  }

  if (DiagLevel >= 4)
    DiagLogF("Server: object %d:%" OID_FORMAT" updated", id.creator.CreatorInt(), id.id);

  return oInfo;
}

void NetworkServer::RescanBestPosition( NetworkObjectInfo * oInfo )
{
  const auto bestPosOld = oInfo->BestPosition();
  const Entity *veh = dyn_cast<Entity,NetworkObject>(oInfo->object);
  if (veh && veh->GetHierarchyParent())
  {
    return;
  }

  oInfo->ScanBestPosition(this);

  const auto bestPosNew = oInfo->BestPosition();

  _objects.onPosChanged(oInfo,bestPosOld,bestPosNew);

  if (veh && bestPosOld._pos.Distance2(bestPosNew._pos)>Square(0.001f))
  {
    UpdateChildrenPos(veh);
    return;
  }
}

void NetworkServer::UpdateChildrenPos(const Entity * veh)
{
  NetworkObjectInfo *oInfo = GetObjectInfo(veh->GetNetworkId());
  if (!oInfo) return;
  const auto bestPosNew = oInfo->BestPosition();
    
  veh->ForEachChildren([this,&bestPosNew](const NetworkObject *obj)->bool{
    NetworkObjectInfo *oInfo = GetObjectInfo(obj->GetNetworkId());
    if (oInfo)
    {
      const auto bestPosOld = oInfo->BestPosition();
      oInfo->_updInfo = bestPosNew;

      _objects.onPosChanged(oInfo,bestPosOld,bestPosNew);
    }
    return false;
  });
}

bool NetworkServer::CheckBestPositionIntegrity() const
{
  // traverse all objects, for all of them check if BestPosition is up to date
  bool ok = true;
  _objects.forEach([&ok,this](const NetworkObjectInfo *oInfo){
    if (ok) ok = oInfo->CheckBestPositionIntegrity(this);
  });
  return ok;
}


void NetworkServer::OnRejectChanged(const NetworkId &id, NetworkMessageClass cls, bool reject)
{
  // reset the updates we are sending to the object owner
  NetworkObjectInfo *oInfo = GetObjectInfo(id);
  if (oInfo)
  {
    LogF("Reject update %d %d for %" OID_FORMAT" to BOT_CLIENT",reject,cls,id.id);
    oInfo->rejected[cls] = reject;
    // the update from client ("currentRejected") was accepted by a bot-client, use it as a main object state
    if (!reject)
    {
      oInfo->currentUpdate[cls] = oInfo->currentRejected[cls];
      // forget the bot-client provided update
      oInfo->currentRejected[cls].message = nullptr;
      oInfo->currentRejected[cls].from = 0;
      RescanBestPosition(oInfo);
    }
  }
}

NetworkDeletedObjectInfo ImplicitMapTraits<NetworkDeletedObjectInfo>::zombie = {NetworkId(CreatorId(-1),-1)};
NetworkDeletedObjectInfo ImplicitMapTraits<NetworkDeletedObjectInfo>::null = {NetworkId(CreatorId(0),0)};

int NetworkServer::PerformObjectDestroy(const NetworkId &id)
{
  //DoAssert(CheckIntegrityOfPendingMessages());
  int owner = 0;
  if (NetworkObjectInfo *info = GetObjectInfo(id))
  {
    owner = info->owner;

    RemoveInitObjectDeleted(id);
    //LogF("Performing Object Destroy for (%d:%" OID_FORMAT")", _objects[i]->id.creator.CreatorInt(), _objects[i]->id.id);
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &pInfo = *_players[i];
      int dpid = pInfo.dpid;
      PlayerIndex playerIndex = GetPlayerIndex(dpid);
      if (playerIndex<0) continue; // player index not created yet, player cannot be aware of any objects, cannot delete them
      NetworkPlayerObjectInfo *poInfo = info->GetPlayerObjectInfo(playerIndex,dpid);
      if (poInfo->updating)
      {
        DoAssert(pInfo._updating.CheckIntegrity());
        DoAssert(poInfo->IsInList());
        if (poInfo->IsInList()) pInfo._updating.Delete(poInfo);
        DoAssert(pInfo._updating.CheckIntegrity());
      }
    }

    NetworkDeletedObjectInfo delInfo;
    delInfo.id = id;
    delInfo.owner = info->owner;
    delInfo.time = Glob.time;
    _deletedObjects.put(delInfo);

    _objects.removeKey(id);
  }
  return owner;
}

void NetworkServer::OnObjectDestroy(const NetworkId &id, int from)
{
  PerformObjectDestroy(id);
  DeleteObjectMessage msg;
  msg._creator = id.creator;
  msg._id = id.id;
  
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &pInfo = *_players[i];
    if (pInfo.clientState < NCSMissionReceived) continue;
    if (pInfo.dpid == from) continue;
    SendMsg(pInfo.dpid, &msg, NMFGuaranteed);
  }
}

int NetworkServer::UpdateObject(NetworkPlayerInfo *pInfo, NetworkObjectInfo *oInfo, NetworkMessageClass cls, NetMsgFlags dwFlags)
{
  if (!oInfo)
  {
    // TODO: BUG
    DiagLogF("Server: Object not found");
    return -1;
  }


  // when sending to the bot-client use a rejected updated when available
  NetworkCurrentInfo &info = pInfo->dpid==BOT_CLIENT && oInfo->rejected[cls] && oInfo->currentRejected[cls].message.NotNull() ? oInfo->currentRejected[cls] : oInfo->currentUpdate[cls];
  if (!info.message) return -1;


  // SendMsg may change currentRejected, therefore info.message may be no longer valid after it
  Ref<NetworkMessage> msg = info.message;

  NetworkMessageType type = info.type;
  int to = pInfo->dpid;

  PlayerIndex index = GetPlayerIndex(to);
  if (index<0) return -1;

  if (DebugMeBoy(oInfo->id))
  {
    LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} UpdateObject to %d", oInfo->id.creator.CreatorInt(), oInfo->id.id, to);
  }


  // make sure the  children are updating, as the message can contain references to them
  if (oInfo->object && pInfo->dpid!=BOT_CLIENT)
  {
    oInfo->object->ForEachChildren([this,index,pInfo,oInfo,type](const NetworkObject *obj)->bool{
      NetworkObjectInfo *objInfo = GetObjectInfo(obj->GetNetworkId());
      if (!objInfo) 
      {
        BString<256> context;
        sprintf(context, " ForEachChildren of %s", cc_cast(oInfo->object->GetDebugName()));
        RptFObjectNotFound(obj->GetNetworkId(), type, -1, context);
        return false;
      }
      NetworkPlayerObjectInfo *pObjInfo = objInfo->GetPlayerObjectInfo(index,pInfo->dpid);
      if (!pObjInfo->updating)
      {
        DoAssert(objInfo->create.message.NotNull());
        if (objInfo->create.message.NotNull())
        {
          LogF("%s to %d: children %s starting updating",cc_cast(oInfo->GetDebugName()),pInfo->dpid,cc_cast(obj->GetDebugName()));
          SendCreateMessage(pInfo->dpid, *objInfo, pObjInfo);
          pObjInfo->updating = true;
          /*
          Can be called safely from NetworkServer::CreateObjectsList in spite of existence of local updatingNew list, which is not updated here.
          If the children was already visited in the forEachInBubble loop, it was already marked as updating and we are not processing it here.
          If it was not visited yet, pInfo.markAsUpdating(poInfo,updatingNew) will be called on it once it will be visited.
          */
          pInfo->markAsUpdating(pObjInfo);
        }
      }
      else
      {
        //LogF("%s to %d: children %s already updating",cc_cast(oInfo->GetDebugName()),pInfo->dpid,cc_cast(obj->GetDebugName()));
      }
      return false;
    });
  }

  // update object info
  NetworkPlayerObjectInfo *poInfo = oInfo->GetPlayerObjectInfo(index,to);

  DoAssert(!msg->HasDiff());

  Ref<NetworkMessage> toSend = msg;
  if (_updatesGuaranteed && to!=BOT_CLIENT)
  {
    NetworkMessage *refMsg = poInfo->updates[cls].lastSentMsg;
    if (refMsg)
    {
      MessageDiffCreate diff;
      toSend = toSend->Clone();
      toSend->CreateDiff(refMsg,diff);
      toSend->StoreDiff(diff);
    }
  }

  DWORD dwMsgId = SendMsg(to, toSend, type, dwFlags, poInfo);
  if (dwMsgId == 0xFFFFFFFF) return -1;

  // message will be stored and used again, it must not be diffed
  DoAssert(!msg->HasDiff());
  msg->ProtectDiff();

  poInfo->updates[cls].lastSentMsg = msg;
  poInfo->updates[cls].errorValid = false;

  return toSend->size;
}

//! Simplified enumeration of array items
#define FOR_EACH(type,var,array) \
  for (type *var = array.Data(), *end = array.Data() + array.Size(); var < end; var++)

void NetworkServer::DestroyAllObjects()
{
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo *player = _players[i];
    player->person = NetworkId::Null();
    player->_delayedMessages.Clear();
    player->_updating.Clear();
  }
  _objects.reset();
  _deletedObjects.reset();
#if DIAG_MP_OBJECT_LIFE
  GObjLifeJournal.Clear();
#endif
//CheckInitMessages(_initMessages, "before _initMessages.Clear()");
  if ( _initMessages.Size() )
  {
    // log GetAddInitAndRemoveOverridden statistics into the mpStatistics.log file
    LogMPStatistics( GetAddInitAndRemoveOverriddenStat() );
  }
  _initMessages.Clear();
  _jipMarkerInfos.Clear();

  if (_indexToPlayerId.Size()>=1)
  {
    DoAssert(_indexToPlayerId[0]==BOT_CLIENT);
    _indexToPlayerId.Resize(1);
    LogF("**** Player index reset");
  }
//CheckInitMessages(_initMessages, "after _initMessages.Clear()");

  // avoid id duplicity at next missions
  // _nextId = 0;
}


NetworkMessageFormatBase *NetworkServer::GetFormat(/*int client, */int type)
{
  Assert(type >= 0);
  if (type >= NMTN) return NULL; // unknown message
  return GMsgFormats[type];
}

const NetworkMessageFormatBase *NetworkServer::GetFormat(/*int client, */int type) const
{
  Assert(type >= 0);
  if (type >= NMTN) return NULL; // unknown message
  return GMsgFormats[type];
}

void NetworkServer::CreateMission(RString mission, RString world)
{
  _originalName = mission;
#if _ENABLE_UNSIGNED_MISSIONS
  if (mission.GetLength() > 0)
  {
    RString filename = RString("mpmissions\\") + mission + RString(".") + world;
    CreateMPMissionBank(filename, world);
  }
  else
  {
    GFileBanks.Remove("mpmissions\\__cur_mp.");
  }
#endif // _ENABLE_UNSIGNED_MISSIONS
}

/// detect client-side localization, if possible use it
LocalizedString ClientSideLocalizedString(RString value, RString mission)
{
  // if possible, localize client-side
  RString table;
  RString name = TryLocalize(value,table);
  if (!strcmpi(table,"global"))
  {
    // if localized using global table, client can localize it
    return LocalizedString(LocalizedString::Stringtable,(const char *)value+1);
  }
  else
  {
    if (value[0]=='@')
    {
      RptF(
        "%s: string %s cannot be localized client-side - move to global stringtable",
        cc_cast(mission),cc_cast(value)
      );
    }
    return LocalizedString(LocalizedString::PlainText,name);
  }
}


#define DIFF_SET(MessageName,name,ids,XX) _missionHeader._diff##name = item.flags[DT##name];

void NetworkServer::UndefMissionParams()
{
  _param1 = _param2 = FLT_MAX;
  _paramsArray.Clear();
}

void NetworkServer::InitMission(int difficulty, RString displayName, bool noCopy, RString owner, bool isCampaignMission)
{
  PROFILE_SCOPE_EX(nsIM1, network)
  // unlock session
  _sessionLocked = false;

  _missionHeader._estimatedEndTime = TIME_MIN;
  _missionHeader._enableAIDisable = !isCampaignMission;

  // mission description
  _missionHeader._gameType = RString();
  ConstParamEntryPtr header = ExtParsMission.FindEntry("Header");
  if (header)
  {
    ConstParamEntryPtr gameType = header->FindEntry("gameType");
    if (gameType) _missionHeader._gameType = *gameType;
  }

  _missionHeader._island = Glob.header.worldname;
  const ArcadeIntel &intel = CurrentTemplate->Intel();
  if (displayName.GetLength() > 0)
  {
    _missionHeader._name = LocalizedString(LocalizedString::PlainText,displayName);
  }
  else
  {
    _missionHeader._name = ClientSideLocalizedString(intel.briefingName,intel.briefingName);
    if (_missionHeader._name._id.GetLength() == 0)
    {
      _missionHeader._name = LocalizedString(
        LocalizedString::PlainText,
        _originalName.GetLength() > 0 ? _originalName : Glob.header.filename
      );
    }
  }
  
  RString missionName = _missionHeader._name.GetLocalizedValue();
  
  _missionHeader._description = ClientSideLocalizedString(intel.briefingDescription,missionName);

  // ADDED in patch 1.01 - checks of addons
  _missionHeader._addOns = CurrentTemplate->AddOns();
  // make sure addon owning the mission is present on clients
  if (owner.GetLength() > 0) _missionHeader._addOns.AddUnique(owner);

  // ADDED in patch 1.01 - disable AI
#if _ENABLE_AI
  _missionHeader._disabledAI = false;
  ConstParamEntryPtr disabledAI = ExtParsMission.FindEntry("disabledAI");
  if (disabledAI) _missionHeader._disabledAI = *disabledAI;
#endif

  // difficulty
  _missionHeader._diffName = Glob.config.diffNames.GetName(difficulty);
  const DifficultySettings &item = Glob.config.diffSettings[difficulty];
  DIFFICULTY_TYPE_ENUM(DIFF_SET, ignored, ignored)

  _missionHeader._aiKills = false;
  ConstParamEntryPtr aiKills = ExtParsMission.FindEntry("aiKills");
  if (aiKills) _missionHeader._aiKills = *aiKills;

  _missionHeader._createPlayers = false; //TODO

  // handling players after disconnect
  ConstParamEntryPtr entry = ExtParsMission.FindEntry("destroyDisconnected");
  if (entry) _destroyDisconnected = *entry;
  else _destroyDisconnected = true;

  // mission parameters
  entry = ExtParsMission.FindEntry("titleParam1");
  _missionHeader._titleParam1 = ClientSideLocalizedString(entry ? entry->operator RString() : RString(""),missionName);
  _missionHeader._valuesParam1.Resize(0);
  _missionHeader._textsParam1.Resize(0);
  entry = ExtParsMission.FindEntry("valuesParam1");
  if (entry)
  {
    int n = entry->GetSize();
    _missionHeader._valuesParam1.Resize(n);
    for (int i=0; i<n; i++) _missionHeader._valuesParam1[i] = (*entry)[i];
  
    ConstParamEntryPtr entryTexts = ExtParsMission.FindEntry("textsParam1");
    if (!entryTexts || entryTexts->GetSize() != n) entryTexts = entry;

    if (entryTexts)
    {
      _missionHeader._textsParam1.Resize(n);
      for (int i=0; i<n; i++)
      {
        _missionHeader._textsParam1[i] = ClientSideLocalizedString((*entryTexts)[i],missionName);
      }
    }
  }
  entry = ExtParsMission.FindEntry("defValueParam1");
  _missionHeader._defValueParam1 = entry ? *entry : 0;
  
  entry = ExtParsMission.FindEntry("titleParam2");
  _missionHeader._titleParam2 = ClientSideLocalizedString(entry ? entry->operator RString() : RString(""),missionName);
  _missionHeader._valuesParam2.Resize(0);
  _missionHeader._textsParam2.Resize(0);
  entry = ExtParsMission.FindEntry("valuesParam2");
  if (entry)
  {
    int n = entry->GetSize();
    _missionHeader._valuesParam2.Resize(n);
    for (int i=0; i<n; i++) _missionHeader._valuesParam2[i] = (*entry)[i];
  
    ConstParamEntryPtr entryTexts = ExtParsMission.FindEntry("textsParam2");
    if (!entryTexts || entryTexts->GetSize() != n) entryTexts = entry;

    if (entryTexts)
    {
      _missionHeader._textsParam2.Resize(n);
      for (int i=0; i<n; i++)
      {
        _missionHeader._textsParam2[i] = ClientSideLocalizedString((*entryTexts)[i],missionName);
      }
    }
  }
  entry = ExtParsMission.FindEntry("defValueParam2");
  _missionHeader._defValueParam2 = entry ? *entry : 0;

  bool setParams = (_paramsArray.Size()==0);
  if (setParams && _param1==FLT_MAX)
  {
    _param1 = _missionHeader._defValueParam1;
    _param2 = _missionHeader._defValueParam2;
  }

  _missionHeader._hasMissionParams = GetMissionParameters().GetLength() > 0;

  _missionHeader._noCopy = noCopy;

  //----------------------------------------------------------------//
  //----------------------------------------------------------------//
  //array of user mission params
  // mission parameters

  _missionHeader._valuesParamArray.Resize(0);
  _missionHeader._textsParamArray.Resize(0);
  _missionHeader._defaultParamArray.Resize(0);
  _missionHeader._titleParamArray.Resize(0);
  _missionHeader._sizesParamArray.Resize(0);
  if (setParams) _paramsArray.Clear();


  //copy param1/2 to new array
  _missionHeader._titleParamArray.Add(_missionHeader._titleParam1);
  _missionHeader._sizesParamArray.Add(0);
  for(int i=0; i<_missionHeader._valuesParam1.Size();i++)
  {
    _missionHeader._valuesParamArray.Add(_missionHeader._valuesParam1[i]);
    _missionHeader._textsParamArray.Add(_missionHeader._textsParam1[i]);
    _missionHeader._sizesParamArray[_missionHeader._sizesParamArray.Size()-1]++;
  } 
  _missionHeader._defaultParamArray.Add(_missionHeader._defValueParam1);
    if (setParams) _paramsArray.Add(_param1);

  _missionHeader._titleParamArray.Add(_missionHeader._titleParam2);
  _missionHeader._sizesParamArray.Add(0);
  for(int i=0; i<_missionHeader._valuesParam2.Size();i++)
  {
    _missionHeader._valuesParamArray.Add(_missionHeader._valuesParam2[i]);
    _missionHeader._textsParamArray.Add(_missionHeader._textsParam2[i]);
    _missionHeader._sizesParamArray[_missionHeader._sizesParamArray.Size()-1]++;
  }
  _missionHeader._defaultParamArray.Add(_missionHeader._defValueParam2);
  if (setParams) _paramsArray.Add(_param2);

  //read secondary parameters
  ConstParamEntryPtr paramsDesc = ExtParsMission.FindEntry("Params"); 
  //for all parameters add CStatic and CListBox to container
  if(paramsDesc) for(int i=0; i< paramsDesc->GetEntryCount();i++ )
  {
    //create listbox content text (parameters names)
    ParamEntryVal parameterDesc = paramsDesc->GetEntry(i);

    ConstParamEntryPtr entry = parameterDesc.FindEntry("title");
    _missionHeader._titleParamArray.Add(ClientSideLocalizedString(entry ? entry->operator RString() : RString(""),missionName));
    _missionHeader._sizesParamArray.Add(0);

    entry = parameterDesc.FindEntry("values");
    if (entry)
    {
      int n = entry->GetSize();

      for (int i=0; i<n; i++) 
      {
        _missionHeader._valuesParamArray.Add((*entry)[i]);
        _missionHeader._sizesParamArray[_missionHeader._sizesParamArray.Size()-1]++;
      }

      ConstParamEntryPtr entryTexts = parameterDesc.FindEntry("texts");
      if (!entryTexts || entryTexts->GetSize() != n) entryTexts = entry;

      if (entryTexts)
      {
        for (int i=0; i<n; i++)
        {
          _missionHeader._textsParamArray.Add(ClientSideLocalizedString((*entryTexts)[i],missionName));
        }
      }
    }

    entry = parameterDesc.FindEntry("default");
    _missionHeader._defaultParamArray.Add(entry ? *entry : 0);
    if (setParams) _paramsArray.Add(entry ? *entry : 0);
  }
  //----------------------------------------------------------------//
  //----------------------------------------------------------------//

#if _USE_BATTL_EYE_SERVER
  RString beName = RString();
#endif

  // mission file
  if (noCopy)
  {
    // mission inside addon is ready on the client side (is in the addons list)
    _missionHeader._fileDir = GetBaseDirectory() + GetBaseSubdirectory();
    _missionHeader._fileName = RString(Glob.header.filename) + RString(".") + RString(Glob.header.worldname);
#if _USE_BATTL_EYE_SERVER
    beName = _missionHeader._fileName;
#endif
  }
#if _ENABLE_UNSIGNED_MISSIONS
  else
  {
    QFBank *bank = FindBank("MPMissions\\__cur_mp.");
    if (bank)
    {
      // mission in bank
      {
        if (_missionHeader._hasMissionParams)
        {
          _missionHeader._fileDir = RString();
          _missionHeader._fileName = _originalName;
        }
        else
        {
          _missionHeader._fileDir = RString("MPMissions\\");
          _missionHeader._fileName = _originalName + RString(".") + _missionHeader._island;
        }
      }
      _missionBank = _missionHeader._fileDir + _missionHeader._fileName + RString(".pbo");
#if _USE_BATTL_EYE_SERVER
      beName = _missionHeader._fileName;
#endif
    }
    else
    {
      // char '$' is used as an indication of Local Settings directory (different on client than on server)
      _missionHeader._fileDir = RString("$\\tmp\\");
      _missionHeader._fileName = RString("__cur_mp");
#if _USE_BATTL_EYE_SERVER
      beName = Glob.header.filename + RString(".") + _missionHeader._island;
#endif

      RString srcDir = GetServerTmpDir() + RString("\\");
      _missionBank = srcDir + _missionHeader._fileName + RString(".pbo"); 

      ::CreateDirectory(srcDir, NULL);

      DoVerify(QIFileFunctions::CleanUpFile(_missionBank));

      FileBankManager mgr;

      RString dir = GetMissionDirectory();
      int i = dir.GetLength() - 1;
      if (i>=0 && dir[i] == '\\') dir = dir.Substring(0, i); // remove trailing '\'

#if _VBS2 && _VBS2_LITE
      void VBS2EncryptMission(RString fileName,RString directory);
      VBS2EncryptMission(_missionBank,dir);
#else
      // TODO: do not create temporary __CUR_MP
      mgr.Create(_missionBank, dir);
#endif
    }
  }
#endif // _ENABLE_UNSIGNED_MISSIONS

#if _USE_BATTL_EYE_SERVER
  //ASSERT_RLS(beName.GetLength() > 0, ("Empty mission name for BattlEye service!!!"));
  _beIntegration.LoadedMission(beName);
#endif

  {
    QIFStream f;
    f.open(_missionBank);

    CRCCalculator crc;
    crc.Reset();
    f.copy(crc);
    _missionHeader._fileCRC = crc.GetResult();

    _missionHeader._fileSizeH = 0;
    _missionHeader._fileSizeL = f.tellg();
  }

#if _GAMES_FOR_WINDOWS
  // set the properties and contexts for matchmaking
  // - MPType
  int gameType = 0;
  if (_missionHeader._gameType.GetLength() > 0)
  {
    ConstParamEntryPtr cls = (Pars >> "CfgMPGameTypes").FindEntry(_missionHeader._gameType);
    if (cls) gameType = *cls >> "id";
  }
  GSaveSystem.SetContext(CONTEXT_GAME_MPTYPE, gameType);
  // - World
  int worldId = 0;
  if (_missionHeader._island.GetLength() > 0)
  {
    ConstParamEntryPtr cls = (Pars >> "CfgWorlds").FindEntry(_missionHeader._island);
    if (cls) worldId = *cls >> "worldId";
  }
  GSaveSystem.SetContext(CONTEXT_GAME_WORLD, worldId);
  // - Difficulty
  GSaveSystem.SetContext(CONTEXT_GAME_DIFFICULTY, difficulty);
  // - MPMission
  int missionId = 0;
  // TODOXNET: mission identification
  GSaveSystem.SetContext(CONTEXT_GAME_MPMISSION, missionId);
#endif
  _missionHeader._forceCameraType = 0;
	_missionHeader._environmentTimeMul = 0;
#if _ENABLE_DEDICATED_SERVER
  {
    ConstParamEntryPtr entry = _serverCfg.FindEntry("disable3rdPerson");
    if(entry)
      _missionHeader._forceCameraType = *entry;

		ConstParamEntryPtr entry2 = _serverCfg.FindEntry("environmentTimeMul");
		if(entry2)
			_missionHeader._environmentTimeMul = *entry2;
  }
#endif

  // send mission description and roles
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &info = *_players[i];
    if (info.clientState < NCSConnected) continue;
    info.missionFileValid = false;
    info.paramFileValid = GetMissionParameters().GetLength() == 0;
    // set missionFileValid first, as SendMissionInfo on the bot-client will already use it on the receiving end
    SendMissionInfo(info.dpid);
  }

  if (header)
  {
/*
    int minPlayers = -1;
    ConstParamEntryPtr entry = header->FindEntry("minPlayers");
    if (entry) minPlayers = *entry;
*/
    int maxPlayers = -1;
    entry = header->FindEntry("maxPlayers");
    if (entry) maxPlayers = *entry;

    if (maxPlayers > 0)
    {
      _missionHeader._maxPlayers = maxPlayers;
    }
  }
}

void NetworkServer::UpdateMaxPlayers(int maxPlayers, int privateSlots)
{
  _totalSlots = maxPlayers;

  maxPlayers = GetMaxPlayers();
#if _ENABLE_DEDICATED_SERVER
  if (IsDedicatedServer()) maxPlayers++; // one more slot for the dedicated server bot client
#endif

  if(privateSlots > maxPlayers)
    privateSlots = maxPlayers;
  if (_server) _server->UpdateMaxPlayers(maxPlayers);
# if _ENABLE_GAMESPY
    ChangeStateQR();
# endif

#if _GAMES_FOR_WINDOWS
  // update the session description
  if (_parent) _parent->UpdateSession(maxPlayers, privateSlots);
#endif
}

void NetworkServer::ChangeOwner(const NetworkId &id, int from, int to)
{
  if (from == to) return;
  if (id.IsNull()) return;

  NetworkObjectInfo *oInfo = GetObjectInfo(id);
  if (!oInfo)
  {
    RptFObjectNotFound(id,"NetworkServer::ChangeOwner",from);
    return;
  }

  if (oInfo->pendingOwner!=NO_CLIENT)
  { // change of owner of this object is already pending
    oInfo->pendingOwner = to; // BUT change pending owner to new one
    return;
  }
  oInfo->pendingOwner = to; // mark this change of owner as pending

  ChangeOwnerMessage msg;
  msg._creator = id.creator;
  msg._id = id.id;
  msg._owner = to;
  msg._disconnected = false;
#if DIAG_MP_OBJECT_LIFE
  GObjLifeJournal.Add(id, Format("(%.3f) ChangeOwner: from=%d to=%d",Glob.time.toFloat(), from, to));
#endif
  SendMsg(from, &msg, NMFGuaranteed);
  //note: new owner will be notified after the ownerChanged returns from previous owner
  //      so do not SendMsg(to, &msg, NMFGuaranteed); now

  if (DebugMeBoy(id))
  {
    LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} ChangeOwner from %d to %d", oInfo->id.creator.CreatorInt(), oInfo->id.id, from, to);

  }
}

void NetworkServer::SendMissionInfo(int to, bool onlyPlayers)
{
  _missionHeader._updateOnly = false;

  if (!onlyPlayers)
  {
    // send at last, client will update its state as reaction
    SendMsg(to, &_missionHeader, NMFGuaranteed); // LOGGED IN -> MISSION SELECTED 

    MissionParamsMessage msg;
    msg._param1 = _param1;
    msg._param2 = _param2;
    msg._paramsArray = _paramsArray;
    msg._updateOnly = false;
    SendMsg(to, &msg, NMFGuaranteed); // MISSION SELECTED -> MISSION ASKED
  }
}

void Encrypt(QOStrStream &out,const unsigned char *publicKey, int keySize);

//{ DEDICATED SERVER SUPPORT
bool NetworkServer::IsDedicatedBotClient(int dpnid) const
{
#if _ENABLE_DEDICATED_SERVER
  return _dedicated && dpnid == BOT_CLIENT;
#else
  return false;
#endif
}
//}

//! Text representation of NetworkServerState enum values
const EnumName NetworkServerStateNames[]=
{
  EnumName(NSSNone, "NONE"),
  EnumName(NSSSelectingMission, "SELECTING MISSION"),
  EnumName(NSSEditingMission, "EDITING MISSION"),
  EnumName(NSSAssigningPlayers, "ASSIGNING PLAYERS"),
  EnumName(NSSSendingMission, "SENDING MISSION"),
  EnumName(NSSLoadingGame, "LOADING GAME"),
  EnumName(NSSPlaying, "PLAYING"),
  EnumName(NSSMissionAborted, "MISSION ABORTED"),
  EnumName()
};

RString NetworkServer::GetAddInitAndRemoveOverriddenStat()
{
  int stats[NMTN];
  for (int i=0; i<NMTN; i++) stats[i] = 0; //zero statistics

  // get the statistics
  for (int i=0; i<_initMessages.Size(); i++)
  {
    int type = (int)_initMessages[i].type;
    if ( type >= 0 && type < NMTN ) stats[type]++;
  }

  RString retValStr;
  retValStr.CreateBuffer(NMTN * 256); // 256 chars for each line of statistics should be enough
  char *buf = retValStr.MutableData();
  sprintf(buf, "AddInitAndRemoveOverridden statistics ... total messages = %d\n", _initMessages.Size());
  for (int i=0; i<NMTN; i++)
  {
    if ( stats[i]>0 )
    {
      RString name = GetMsgTypeName( (NetworkMessageType)i );
      RString newLine = Format("%d ... %s\n", stats[i], cc_cast(name));
      strcat(buf, newLine);
    }
  }
  strcat(buf, "\n\n");
  return retValStr;
}


void NetworkServer::OnChangedOnPlayerConnecting(GameValuePar codeValue)
{
  for (int i=0; i<_players.Size(); i++)
  {
    if (_players[i]->dpid==BOT_CLIENT)
    {
#if _ENABLE_DEDICATED_SERVER
      bool localDedicated = _dedicated;
#else
      bool localDedicated = false;
#endif
      if (!localDedicated) OnPlayerConnecting(_players[i]->dpid,codeValue);
    }
    else if (_players[i]->clientState>=NCSGameLoaded)
    {
      // can any player be in such state?
      OnPlayerConnecting(_players[i]->dpid,codeValue);
    }
  }
}



void ResetGlobalEventHandlers();

//! Map NetworkServerState enum values to its text representation
template<>
const EnumName *GetEnumNames(NetworkServerState dummy) {return NetworkServerStateNames;}

/*!
\patch 1.32 Date 11/26/2001 by Jirka
- Fixed: players' states are properly send to all clients
\patch 5142 Date 3/16/2007 by Jirka
- Fixed: MP - handling / reporting the current and maximum number of players was wrong sometimes
\patch 5154 Date 5/3/2007 by Jirka
- Fixed: MP mission statistics can be written to the file given by command line argument -ranking=...
\patch 5170 Date 10/8/2007 by Bebul
- New: Command line argument -ranking=... was removed. MP mission statistics can be written to file specified by logFile entry in the server config.
*/

void NetworkServer::SetServerState(NetworkServerState state)
{
//((TO_BE_REMOVED_IN_FINAL
#if _ENABLE_PERFLOG
  RptF("### Server: SetServerState to %s", (const char *)FindEnumName(state));
#endif
//))TO_BE_REMOVED_IN_FINAL

  // Transition graph:
  switch (state)
  {
  case NSSNone:
    // nothing to do
    break;
  case NSSSelectingMission:
    GNetworkManager.DestroyAllObjects();
    break;
  case NSSEditingMission:
    DoAssert(_serverState == NSSSelectingMission);
    // nothing to do
    break;
  case NSSAssigningPlayers:
    break;
  case NSSSendingMission:
    DoAssert(_serverState == NSSAssigningPlayers);
    // only bot client need to advance to PlayerAssigned state, normal clients are driven through DisplayClient logic
    if (GetClientState(BOT_CLIENT) == NCSMissionAsked)
    {
      //RptF("### Server: SetClientState NCSPlayerAssigned to BOT_CLIENT");
      SetClientState(BOT_CLIENT, NCSPlayerAssigned);
    }

    // reset _jipProcessed flag for all players
    for (int i=0; i<_players.Size(); i++)
    {
      _players[i]->_jipProcessed=false;
    }
    break;
  case NSSLoadingGame:
    _serverTimeOfEstimatedEnd = _secondsToEstimatedEnd = 0;
    for (int i=0; i<_players.Size(); i++)
    {
      ServerTimeMessage stmsg;
      _serverTime = ::GetTickCount()-_serverTimeInitTickCount; // update _serverTime
      stmsg._time = _serverTime;
      stmsg._endTime = _serverTimeOfEstimatedEnd;
      SendMsg(_players[i]->dpid, &stmsg, NMFGuaranteed | NMFHighPriority);
    }

    for (int i=0; i<_players.Size(); i++)
    {
      CreatePlayerIndex(_players[i]->dpid);
    }
    break;
  case NSSPlaying:
    // recompute estimatedTimeOfEnd to be without time spent in briefing
    _serverTime = ::GetTickCount() - _serverTimeInitTickCount; //make it fresh
    if (_serverTimeOfEstimatedEnd) //recompute
    {
      _serverTimeOfEstimatedEnd = _serverTime + _secondsToEstimatedEnd;
      for (int i=0; i<_players.Size(); i++)
      {
        // send _serverTime on each client
        ServerTimeMessage stmsg;
        _serverTime = ::GetTickCount()-_serverTimeInitTickCount; // update _serverTime
        stmsg._time = _serverTime;
        stmsg._endTime = _serverTimeOfEstimatedEnd;
        SendMsg(_players[i]->dpid, &stmsg, NMFGuaranteed | NMFHighPriority);
      }
    }
    break;
  case NSSMissionAborted:
    CallOnPlayerDisconnected();
    ResetGlobalEventHandlers();
    break;
  }

  #if _ENABLE_MP
  if (state == NSSAssigningPlayers && _serverState > NSSAssigningPlayers)
  {
    // cancel all messages - updates from last sessions, file transfers etc.
    // !!! cannot do it in this way - some messages are important
    // _server->CancelAllMessages();

    // reset all camera information from previous session
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &p = *_players[i];
      p.cameraPosition = InvalidCamPos;
      p.cameraPositionTime = TIME_MIN;
    }
  }
  #endif

  if (state == NSSPlaying) _missionHeader._start = GlobalTickCount();

  #if _ENABLE_MP
  if (state == NSSMissionAborted && _serverState == NSSPlaying)
  {
    // end of mission - update log file
    FILE *LogFile = GetNetworkManager().GetDedicatedServerLogFile();
    if ( LogFile )
    {
      ParamFile f;
      ParamClassPtr root = f.AddClass("Session");
      if (root)
      {
        root->Add("mission", _missionHeader.GetLocalizedMissionName());
        root->Add("island", _missionHeader._island);
        root->Add("gameType", _missionHeader._gameType);
        root->Add("duration", 0.001f * (GlobalTickCount() - _missionHeader._start));
      }
      // append ranking statistics to logFile
      QOStrStream out;
      f.Save(out, 0, "\r\n");
      fwrite(out.str(), 1, out.pcount(), LogFile);
      // flush is only at the end of mission (here) or some rare occasions
      fflush(LogFile);
    }
  }
  #endif

#if _GAMES_FOR_WINDOWS
  // Xbox Live - start / end the session
  if (state == NSSPlaying)
  {
    if (_serverState != NSSPlaying)
    {
      _parent->StartSession();
    }
  }
  else if (_serverState == NSSPlaying)
  {
    _parent->EndSession();
  }

  if (_parent->GetSessionType() == STRanked)
  {
    // Ranked match - finish the session when the first game is finished
    if (_serverState >= NSSPlaying && state < NSSPlaying)
    {
      Done();
      return;
    }
    // Ranked match - close the possibility to join and ask for arbitration
    if (state >= NSSSendingMission && _serverState < NSSSendingMission)
    {
      LockSession();
      // ask all clients for arbitration
      AskForArbitrationMessage msg;
      msg._sessionNonce = _parent->GetSessionNonce();
      for (int i=0; i<_identities.Size(); i++)
      {
        int player = _identities[i].dpnid;
        if (player != BOT_CLIENT) SendMsg(player, &msg, NMFGuaranteed);
      }
    }
  }
#endif

  _serverState = state;
  _serverStateTime = GlobalTickCount();
  _serverTimeOutSent = INT_MAX; // assume no value reported yet
  if (state == NSSNone) return; // cannot send messages

  // DS selecting the mission - set the correct maxPlayers
  if (state == NSSAssigningPlayers)
  {
    //{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    if (IsDedicatedServer())
    {
#if _GAMES_FOR_WINDOWS
      // update of max. number of players
      int perClient = 32000;
      int round = 2;
      ConstParamEntryPtr header = ExtParsMission.FindEntry("Header");
      if (header)
      {
        ConstParamEntryPtr entry = header->FindEntry("bandwidthPerClient");
        if (entry) perClient = *entry;
        entry = header->FindEntry("playerCountMultipleOf");
        if (entry) round = *entry;
      }
      int maxPlayers = GetMaxPlayersSafe(perClient, round);
      int GetDSPrivateSlots();
      int maxPrivate = GetDSPrivateSlots();
      saturateMin(maxPrivate, maxPlayers);
      UpdateMaxPlayers(maxPlayers, maxPrivate);
#else
      UpdateMaxPlayers(INT_MAX, 0);
#endif
    }
#endif
    //}
  }

  // DS leaving the mission - reset the original maxPlayers
  if (state == NSSSelectingMission)
  {
    //{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    if (IsDedicatedServer())
    {
#if _GAMES_FOR_WINDOWS
      // update of max. number of players
      int perClient = 32000;
      int round = 1;
      int maxPlayers = GetMaxPlayersSafe(perClient, round);
      int GetDSPrivateSlots();
      int maxPrivate = GetDSPrivateSlots();
      saturateMin(maxPrivate, maxPlayers);
      UpdateMaxPlayers(maxPlayers, maxPrivate);
#else
      UpdateMaxPlayers(INT_MAX, 0);
#endif
    }
#endif
    //}
  }

  {
    // send to all players
    ServerStateMessage msg;
    msg._serverState = state;
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.clientState < NCSConnected) continue;
      if (!IsDedicatedServer() || info.dpid!=BOT_CLIENT) // bot-client on a dedicated server has no identity
      {
        if (!FindIdentity(info.dpid)) continue; // prevent clients with unknown identity to proceed with the connection
      }
      SendMsg(info.dpid, &msg, NMFGuaranteed);
    }
  }

  UpdateMatchmakingDescription(true);

#if _GAMES_FOR_WINDOWS
  // set the properties and contexts for matchmaking
  // - ServerState
  GSaveSystem.SetContext(CONTEXT_GAME_SERVERSTATE, state);
  // reset the values which have not yet sense
  if (state < NSSAssigningPlayers)
  {
    // - MPType
    GSaveSystem.SetContext(CONTEXT_GAME_MPTYPE, 0);
    // - World
    GSaveSystem.SetContext(CONTEXT_GAME_WORLD, 0);
    // - Difficulty
    GSaveSystem.SetContext(CONTEXT_GAME_DIFFICULTY, 0);
    // - MPMission
    GSaveSystem.SetContext(CONTEXT_GAME_MPMISSION, 0);
  }
  if (state < NSSPlaying)
  {
    // - TimeLeft
    GSaveSystem.SetProperty(PROPERTY_GAME_TIMELEFT, 0);
  }
#endif

  if (state == NSSEditingMission)
  {
    // No Load should be after mission was edited
    bool DeleteSaveGames();
    DeleteSaveGames();
    bool DeleteWeaponsFile();
    DeleteWeaponsFile();
  }
}

/*!
\patch 5111 Date 12/22/2006 by Jirka
- Fixed: DS - Mission voting results show correctly localized mission name for missions in addons
*/

void NetworkServer::MissionVotingEcho()
{
  const Voting *miss = _votings.Find(NCMTMission);
  const Voting *diff = _votings.Find(NCMTDifficulty);

  // if the voting is no longer here, it has probably completed
  if (diff && miss)
  {
    VotingMissionProgressMessage progress;
    const AutoArray<char> *diffValue = diff->GetValue();
    if (diffValue)
    {
      progress._diff = RString(diffValue->Data(),diffValue->Size());
    }
    AutoArray<Voting::VoteInfo> order;
    miss->GetOrder(order);
    for (int i=0; i<order.Size(); i++)
    {
      const AutoArray<char> &missValue = order[i].vote->value;
      int missVotes = order[i].count;
      LocalizedString FindLocalizedBriefingName(RString path);
      RString name = RString(missValue.Data(),missValue.Size());
      
      RString mission = name;
      RString dir;

      const char *ext = strrchr(name, '.');
      if (ext) mission = RString(name, ext - cc_cast(name));

#if _ENABLE_MISSION_CONFIG
      ConstParamEntryPtr entry = (Pars >> "CfgMissions" >> "MPMissions").FindEntry(mission);
      if (entry)
        dir = (*entry) >> "directory";
      else
#endif
        dir = RString("MPMissions\\") + name;

      LocalizedString displayName = FindLocalizedBriefingName(dir);
      if (displayName._id.GetLength() == 0)
      {
        displayName = LocalizedString(LocalizedString::PlainText,name);
      }

      progress._missions.Add(displayName);
      progress._missionVotes.Add(missVotes);
    }
    for (int i=0; i<_players.Size(); i++)
    {
      const NetworkPlayerInfo &player = *_players[i];
      // notify all clients who might be interested
      if (player.clientState >= NCSConnected && player.clientState<=NCSMissionSelected)
      {
        SendMsg(player.dpid, &progress, NMFGuaranteed);
      }
    }
  }
}

void NetworkServer::UpdateMatchmakingDescription(bool changedParams)
{
  
//{ QUERY & REPORTING SDK
#if _ENABLE_GAMESPY
  ChangeStateQR();
#endif
//}

  UpdateSessionDescription();
  
  // update both broadcast and matchmaking
  _timeLeftLastValueBcast = _timeLeftLastValue = GetSessionTimeLeft();
  _timeLeftLastTime = GlobalTickCount();
}

void NetworkServer::DisconnectPlayer(int dpnid, NetworkId playerPerson)
{
  PlayerIndex playerIndex = GetPlayerIndex(dpnid);

  // player is disconnecting from game, return ownership of objects to bot client

  AutoArray<NetworkId> owned; // we cannot let SendMsg to manipulate _objects while iterating it
  _objects.forEach([this,playerIndex,dpnid,&owned](NetworkObjectInfo *oInfo)
  {
    if (playerIndex>=0) oInfo->DeletePlayerObjectInfo(playerIndex,dpnid);

    for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
    {
      NetworkCurrentInfo &info = oInfo->currentUpdate[j];
      if (info.from == dpnid)
      {
        info.from = BOT_CLIENT;
        info.message = NULL;
      }
    }
    
    RescanBestPosition(oInfo);

    // owner of this object has disconnected - possible pendingOwner can take the full ownership
    // or BOT_CLIENT should take it otherwise
    if (oInfo->owner == dpnid)
    {
      owned.Add(oInfo->id);
    }
  });

  
  if (playerIndex>=0)
  {
    _indexToPlayerId.DeleteAt(playerIndex);
    LogF("**** Player %d index %d removed, %d total",dpnid,int(playerIndex),_indexToPlayerId.Size());
  }
  else
  {
    LogF("**** Player %d index not removed, %d total",dpnid,_indexToPlayerId.Size());
  }

  for (int i=0; i<owned.Size(); i++)
  {
    RefD<NetworkObjectInfo> oInfo;
    _objects.get(owned[i],oInfo);
    if (!oInfo) continue;
    if (oInfo->pendingOwner!=NO_CLIENT && oInfo->pendingOwner!=dpnid)
    {
      NetworkPlayerInfo *pInfo = GetPlayerInfo(oInfo->pendingOwner);
      if (!pInfo)
      { // new owner has probably disconnected meanwhile
        oInfo->pendingOwner = BOT_CLIENT;
      }
      oInfo->owner = oInfo->pendingOwner;
    }
    else
    {
      oInfo->owner = BOT_CLIENT;
    }
    // not pending now
    oInfo->pendingOwner = NO_CLIENT;

    _objects.get(owned[i],oInfo);
    // change the owner
    if (!oInfo->id.IsNull())
    {
      ChangeOwnerMessage msg;
      msg._creator = oInfo->id.creator;
      msg._id = oInfo->id.id;
      msg._owner = oInfo->owner;
      msg._disconnected = oInfo->id == playerPerson; // mark player person to enable clean up on it
      SendMsg(oInfo->owner, &msg, NMFGuaranteed);

      // destroy the person
      if (msg._disconnected && _destroyDisconnected)
      {
        // playable person with AI disabled - destroy unit
        Person *person = dyn_cast<Person>(_parent->GetObject(playerPerson));
        if (person)
        {
          Ref<AIBrain> agent = person->Brain();

          if (agent)
          {
            // Get out from vehicle
            Transport *vehicle = agent->GetVehicleIn();
            if (vehicle)
            {
              agent->DoGetOut(vehicle, true, false);
            }
          }

          // detach brain (to avoid respawn during SetDamage)
          person->SetBrain(NULL);

          // destroy person
          person->SetDamage(1,true);
          person->HideBody();
          // DeleteObject(person->GetNetworkId(), true);
        }
      }

      // owner is chosen so send waiting messages
      for ( int i=0; i>oInfo->waitingMessages.Size(); i++ )
      {
        WaitingMessageInfo *wMsgInfo = oInfo->waitingMessages[i];
        SendMsg(oInfo->owner, wMsgInfo->msg, wMsgInfo->type, NMFGuaranteed, NULL);
      }
      oInfo->waitingMessages.Clear();
    }
  }

  _radioSubscribers.OnPlayerDisconnected(dpnid);

  CleanupPlayerMessage msg;
  msg._dpnid = dpnid;
  msg._creator = playerPerson.creator;
  msg._id = playerPerson.id;
  SendMsg(BOT_CLIENT, &msg, NMFGuaranteed);
}

bool NetworkServer::IsPlayerOnChannel(int dpnid, int channel, int roleIndex2)
{
  if (channel==CCGlobal || channel==CCSystem) return true;
  if (channel==CCDirect || channel==CCNone) return false;

  return false;
}

bool IsEqualMarker(NetworkMessage *marker1, NetworkMessage *marker2);
bool IsEqualMarker(RString name, NetworkMessage *marker2);

void NetworkServer::AddJIPMarkerInfo(NetworkMessage *marker, int channel, int from)
{
  for (int i=0; i<_jipMarkerInfos.Size(); i++)
  {
    if ( IsEqualMarker(marker, _jipMarkerInfos[i]._marker) )
    {
      _jipMarkerInfos[i]._marker = marker;
      return;
    }
  }

  JIPMarkerInfo &info = _jipMarkerInfos.Append();
  info._marker = marker;
  info._channel = channel;
  info._roleIndex = -1; //TODO
}

void NetworkServer::RemoveJIPMarkerInfo(RString name)
{
  for (int i=0; i<_jipMarkerInfos.Size(); i++)
  {
    if ( IsEqualMarker(name, _jipMarkerInfos[i]._marker) )
    {
      _jipMarkerInfos.Delete(i);
      return;
    }
  }
}

void NetworkServer::SendInitMessages(int dpnid)
{
  for (int i=0; i<_initMessages.Size(); i++)
  {
    if (_initMessages[i].msg)
    {
      SendMsg(dpnid, _initMessages[i].msg, _initMessages[i].type, NMFGuaranteed, NULL);
    }
  }
  for (int i=0; i<_jipMarkerInfos.Size(); i++)
  {
    JIPMarkerInfo &info = _jipMarkerInfos[i];
    if ( IsPlayerOnChannel(dpnid, info._channel, info._roleIndex) )
    {
      NetworkMessageMarkerCreate *msg = new NetworkMessageMarkerCreate;
      msg->_channel = info._channel;
      msg->_marker = info._marker;
      SendMsg(dpnid, msg, NMTMarkerCreate, NMFGuaranteed, NULL);
    }
  }
  AutoArray<RadioSubscriber> &_subscribers = _radioSubscribers._subscribers;
  for (int i=0; i<_subscribers.Size(); i++)
  {
    if (_subscribers[i].dpnid == dpnid) continue;
    SubscribeRadiosMessage msg;
    msg._player = _subscribers[i].dpnid;
    msg._frequences = _subscribers[i].frequencies;
    SendMsg(dpnid, &msg, NMFGuaranteed);
  }
}

void NetworkServer::OnPlayerConnecting(int dpnid)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpnid);
  PlayerIdentity *identity = FindIdentity(info->dpid);
  DoAssert(identity);
  if (identity)
  {
    ::OnPlayerConnecting(dpnid,identity->id,identity->playerid,info->name);
  }
}

void NetworkServer::OnPlayerConnecting(int dpnid, GameValuePar codeValue)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpnid);
  PlayerIdentity *identity = FindIdentity(info->dpid);
  DoAssert(identity);
  if (identity)
  {
    ::OnPlayerConnecting(dpnid,identity->id,identity->playerid,info->name,codeValue);
  }
}

void NetworkServer::ObjectUpdateAll(NetworkObjectInfo * info, int dpnid, NetMsgFlags flags)
{
  for (int cls=NMCUpdateFirst; cls<NMCUpdateN; cls++)
  {
    if (info->currentUpdate[cls].message)
    {
      if (info->currentUpdate[cls].type==NMTUpdateClientInfoDpid) continue;
      NetworkPlayerInfo *pInfo = GetPlayerInfo(dpnid);
      if (pInfo) UpdateObject(pInfo, info, (NetworkMessageClass)cls, flags);
    }
  }
}

void NetworkServer::OnClientStateChanged(int dpnid, NetworkClientState state)
{
  PROFILE_SCOPE_EX(onClS, network)
  NetworkClientState oldState = NCSNone;

  NetworkPlayerInfo *pInfo = GetPlayerInfo(dpnid);
  Assert(pInfo);
  if (pInfo)
  {
    oldState = pInfo->clientState;
    if (oldState == state) return;
#if _ENABLE_PERFLOG
    RptF("*** Server: OnClientStateChanged %s %s -> %s", (const char *)pInfo->name, (const char *)FindEnumName(pInfo->clientState), (const char *)FindEnumName(state));
#endif
    if (state == NCSPlayerAssigned && oldState > NCSPlayerAssigned) 
      return; // FIX: problems with mission launching 
    pInfo->clientState = state;
  }

  //RptF("Server: OnClientStateChanged %s to %s", pInfo ? (const char *)pInfo->name : "NULL", (const char *)FindEnumName(state));

  PlayerIdentity *identity = FindIdentity(dpnid);
  if (identity)
  {    
#if _USE_BATTL_EYE_SERVER
#endif

    identity->clientState = state;
  }
  
  PlayerClientStateMessage msg;
  msg._dpnid = dpnid;
  msg._clientState = state;
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &player = *_players[i];
    if (player.dpid != dpnid && player.clientState >= NCSConnected)
      SendMsg(player.dpid, &msg, NMFGuaranteed);
  }

  switch (state)
  {
  case NCSLoggedIn:
    if (pInfo && _serverState >= NSSAssigningPlayers)
    {
      // send mission info
      SendMissionInfo(pInfo->dpid);
      pInfo->missionFileValid = false;
      pInfo->paramFileValid = GetMissionParameters().GetLength() == 0;
    }
    if (_serverState == NSSPlaying)
    {
      // if game is already in progress, send how long is played already
      NetworkCommandMessage msg;
      msg._type = NCMTMissionTimeElapsed;
      int timeElapsed = GlobalTickCount() - _missionHeader._start;
      msg._content.Write(&timeElapsed, sizeof(timeElapsed));
      SendMsg(dpnid, &msg, NMFGuaranteed);
    }
    if (_serverState == NSSSelectingMission)
    {
      // new clients should get mission voting echo as necessary, so that they can see voting
      MissionVotingEcho();
    }
    break;
  case NCSPlayerAssigned:
    if (_serverState >= NSSSendingMission)
    {
      // send mission
      SendMissionFile(dpnid);
    }
    {
      PlayerIndex pIndex = CreatePlayerIndex(dpnid);
      if (pInfo && _serverState >= NSSLoadingGame)
      {
        OnPlayerConnecting(pInfo->dpid);

        pInfo->_jipProcessed = true;
        // load game
        DoAssert(pInfo->clientState == NCSMissionReceived);

        _objects.forEach([pIndex,dpnid](NetworkObjectInfo *oInfo)
        {
          oInfo->CreatePlayerObjectInfo(pIndex,dpnid);
        });

        // send the info about network objects need to be created
        MissionStatsMessage stats;
        stats._toCreate = stats._toUpdate = _objects.card();
        SendMsg(dpnid, &stats, NMFGuaranteed);

        // create all game objects
        //RptF("*** Sending world state ***");
        //RptF("1. all except AI objects");
        // 1. all except AI objects

        Vector3 camPos = pInfo->cameraPosition;

        _objects.forEach([pIndex,pInfo,dpnid,camPos,this](NetworkObjectInfo *info)
        {
          // update object info
          NetworkPlayerObjectInfo *poInfo = info->GetPlayerObjectInfo(pIndex,dpnid);

          NetworkMessageType type = info->create.type;
          // ClientInfo is not the full featured network object, so it should be handled different way
          if ( type == NMTNone && info->currentUpdate[NMCUpdatePosition].type == NMTUpdateClientInfoDpid )
            return;
          if (poInfo->updating) return; // some objects are already synced, e.g. selectPlayer done from onPlayerConnecting
          DoAssert(info->create.message);
#if 0
          if (!info->create.message)
          {
            LogF(" i=%d, info->create.type=%d message=%x", i, info->create.type, info->create.message.GetRef());
          }
          static NetworkId debugNetworkId;
          if (debugNetworkId==info->id)
          {
            _asm nop;
          }
#endif
          
          if (info->object.NotNull())
          {
            auto bestPosition = info->BestPosition(this);
            if (bestPosition._updateDistance<FLT_MAX)
            {
              float dist2 = camPos.Distance2(bestPosition._pos);
              if (dist2>Square(bestPosition._updateDistance))
              {
                return;
              }
            }
          }

          SendCreateMessage(dpnid,*info, poInfo);
          poInfo->updating = true;
          pInfo->markAsUpdating(poInfo);
          //RptF("  %s: %d:%" OID_FORMAT"", cc_cast(GetMsgTypeName(type)), info->id.creator.CreatorInt(), info->id.id);
        });

        //RptF("*** World state sent ***");
        // update all game objects
        _objects.forEach([pIndex,dpnid,this](NetworkObjectInfo *info)
        {
          if (info->owner==dpnid)
          {
            return;
          }
          NetworkPlayerObjectInfo *poInfo = info->GetPlayerObjectInfo(pIndex,dpnid);
          if (poInfo->updating) ObjectUpdateAll(info, dpnid, NMFGuaranteed);
        });
        // send delayed messages
        SendInitMessages(dpnid);
        //      CheckInitMessages(_initMessages, "after NetworkComponent::SendMsg");
      }
      else
      {
        DoAssert(_objects.card()==0);
        _objects.reset();
        _deletedObjects.reset();
#if DIAG_MP_OBJECT_LIFE
        GObjLifeJournal.Clear();
#endif
      }
      if (_serverState >= NSSLoadingGame)
      {
        SetClientState(dpnid, NCSGameLoaded);
      }
    }
    break;
  case NCSGameLoaded:
    if (dpnid == BOT_CLIENT && _serverState < NSSPlaying)
    {
      // game is loaded, can start the game
      SetServerState(NSSPlaying);
    }
    break;
  case NCSPlaying:
    if (pInfo)
    { // clients should both send the bankscount info
      if ( pInfo->hashes.bankInfoTimeout==0 && pInfo->hashes.banksCount<0 )
      {
        pInfo->hashes.bankInfoTimeout = GlobalTickCount() + 10000; //10 seconds
      }
    }
    if (pInfo && oldState != NCSPlaying && !GNetworkPlayerConnected.GetNil())
    {
      GameState *state = GWorld->GetGameState();
      if (state)
      {
        GameVarSpace vars(false);
        int gshandle = state->BeginContext(&vars);
        PlayerIdentity *identity = FindIdentity(pInfo->dpid);
        RString uid = identity ? identity->id : RString();
        int id = identity ? identity->playerid : -1;
        DoAssert(identity);
        state->VarSetLocal("_id", safe_cast<float>(id), true);
        state->VarSetLocal("_uid", uid, true);
        state->VarSetLocal("_name", pInfo->name, true);

#if USE_PRECOMPILATION
        if (GNetworkPlayerConnected.GetType() == GameCode)
        {
          GameDataCode *code = static_cast<GameDataCode *>(GNetworkPlayerConnected.GetData());
          if (code->IsCompiled() && code->GetCode().Size() > 0)
            state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        }
        else
#endif
        if (GNetworkPlayerConnected.GetType() == GameString)
        {
          // make sure string is not destructed while being evaluated
          RString code = GNetworkPlayerConnected;
          if (code.GetLength() > 0)
            state->EvaluateMultiple(code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        }
        state->EndContext(gshandle);
      }
    }
    if (pInfo) pInfo->joined = Glob.time;

    { // send also current clock
      NetworkCommandMessage msg;
      Glob.clock.Write(msg._content);
			Glob.clockWeather.Write(msg._content);
      msg._type = NCMTMissionDate;
      SendMsg(dpnid, &msg, NMFGuaranteed);
    }
    break;
  case NCSGameFinished:
    // disconnected from game
    if (_serverState == NSSPlaying)
    {
      NetworkId playerPerson;
      if (pInfo) playerPerson = pInfo->person;
      DisconnectPlayer(dpnid, playerPerson);
    }
    break;
  }

  if (pInfo && oldState == NCSPlaying && state != NCSPlaying && _serverState==NSSPlaying)
  {
    OnPlayerDisconnected(pInfo);
  }
}

void NetworkServer::KickAllPlayers(int minBuild)
{
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &player = *_players[i];
    if (player.dpid!=BOT_CLIENT)
    {
      if (minBuild>0)
      {
        const PlayerIdentity *ident = FindIdentity(player.dpid);
        if (!ident || ident->build<0 || ident->build>=minBuild) continue;
      }
      KickOff(player.dpid,KOROther, "Game Loaded from MP Save, please reconnect!");
    }
  }
}

NetworkClientState NetworkServer::GetClientState(int dpid)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  if (info) return info->clientState;
  else return NCSNone;
}

void NetworkServer::SetClientState(int dpnid, NetworkClientState state)
{
// RptF("### Server: Set client state");

  NetworkPlayerInfo *info = GetPlayerInfo(dpnid);
  Assert(info);
  if (info)
  {
    //RptF("    player %s, state %s -> %s", (const char *)info->name, (const char *)FindEnumName(info->clientState), (const char *)FindEnumName(state));
    info->clientState = state;
  }

  PlayerIdentity *identity = FindIdentity(dpnid);
  if (identity)
  {    
#if _USE_BATTL_EYE_SERVER
#endif
    NetLog("Client %s - changed state from %s to %s",cc_cast(identity->name),cc_cast(FindEnumName(identity->clientState)),cc_cast(FindEnumName(state)));
    identity->clientState = state;
  }
  
  ClientStateMessage msg1;
  msg1._clientState = state;
  SendMsg(dpnid, &msg1, NMFGuaranteed);

  PlayerClientStateMessage msg;
  msg._dpnid = dpnid;
  msg._clientState = state;
  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &player = *_players[i];
    if (player.dpid != dpnid && player.clientState >= NCSConnected)
      SendMsg(player.dpid, &msg, NMFGuaranteed);
  }
}

RString NetworkServer::GetPlayerName(int dpid)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  Assert(info);
  if (info) return info->name;
  else return "Unknown player";
}

NetworkId NetworkServer::GetPlayerPerson(int dpid)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  Assert(info);
  if (info) return info->person;
  else return NetworkId();
}

Object *NetworkServer::GetPlayerTarget(int dpid)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  if (!info) return nullptr;

  NetworkObject *obj = GetNetworkManager().GetObject(info->_clientInput->GetCursorObject());
  return dyn_cast<Object>(obj);
}

void NetworkServer::ServerSelectPlayer(int dpid, const NetworkId &id)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  if (info)
  {
    info->person = id;
    // make sure new person object is known to the client
    NetworkObjectInfo *oInfo = GetObjectInfo(id);
    DoAssert(oInfo);
    if (oInfo)
    {
      PlayerIndex playerIndex = GetPlayerIndex(dpid);
      NetworkPlayerObjectInfo *pInfo = oInfo->GetPlayerObjectInfo(playerIndex,dpid);
      if (!pInfo->updating && oInfo->owner!=dpid)
      {
        if (oInfo->create.message)
        {
          SendCreateMessage(dpid,*oInfo,pInfo);
          ObjectUpdateAll(oInfo, dpid, NMFGuaranteed);
          pInfo->updating = true;
          info->markAsUpdating(pInfo);
        }
      }
    }
  }
}

Vector3 NetworkServer::GetCameraPosition(int dpid)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpid);
  Assert(info);
  if (info) return info->cameraPosition;
  else return VZero;
}

NetworkObject *NetworkServer::GetObject(const NetworkId &id)
{
  Fail("Server: GetObject is not implemented");
  return NULL;
}

//! Subsidiary structure for sorting messages by error
struct UpdateObjectInfo
{
  Ref<DelayedMessageInfo> msg;
  //! Object info
  NetworkObjectInfo *oInfo;
  //! Player object info
  NetworkPlayerObjectInfo *poInfo;
  //! Message class type
  NetworkMessageClass cls;
  //! Error (difference), distance adjusted
  float error;
  //! Error (difference)
  float errorNoCoef;
  //! Critical update
  bool criticalUpdate;
  
  UpdateObjectInfo()
  {
    oInfo = NULL;
    poInfo = NULL;
  }
};
TypeIsMovableZeroed(UpdateObjectInfo)

//! Compare player object infos by error
static int CmpPlayerObjects(const UpdateObjectInfo *info1, const UpdateObjectInfo *info2)
{
  if (info2->error > info1->error) return +1;
  if (info2->error < info1->error) return -1;
  return 0;
  // following is not the same when both errors are INF, as INF-INF is NaN
  //return sign(info2->error - info1->error);
}

void NetworkServer::KickOff(int dpnid, KickOffReason reason, const char *reasonStr)
{
  RString format;
  switch (reason)
  {
    case KORBattlEye: format = Format("Player %%s kicked off by BattlEye: %s", reasonStr); break;
    case KORKick: format = LocalizeString(IDS_MP_KICKED); break;
    case KORBan: format = LocalizeString(IDS_MP_BANNED); break;
  }
  PlayerIdentity *id = FindIdentity(dpnid);
  if (id)
  {
    if (id->destroy)
    {
      LogF("%s: KickOff in progress, new request ignored",(const char *)id->name);
      return;
    }
    if (format.GetLength() > 0)
    {
      RString message = Format(format,(const char *)id->name);
      RefArray<NetworkObject> dummy;
      if (!ChannelDisabled(CCSystem))
        GNetworkManager.Chat(CCSystem,NULL,dummy,message);
      GChatList.Add(CCSystem, NULL, message, false, true);
    }
    id->destroy = true;
    id->destroyTime = Glob.uiTime + 15; // destroy player after 15 seconds if no system message arrive
  }
  // mark player kickoff is initiated
  for (int i=0; i<_players.Size(); i++)
  {
    if (_players[i]->dpid==dpnid) _players[i]->kickedOff = true;
  }
  NetTerminationReason netReason = NTROther;
  switch (reason)
  {
    case KORKick: netReason = NTRKicked; break;
    case KORBan: netReason = NTRBanned; break;
    case KORFade: netReason = NTROther; break;
    case KORAddon: netReason = NTRMissingAddon; break;
    case KORBadCDKey: netReason = NTRBadCDKey; break;
    case KORCDKeyInUse: netReason = NTRCDKeyInUse; break;
    case KORBattlEye: netReason = NTRBattlEye; break;
    case KORAuthTimeOut: netReason = NTRAuthTimeOut; break;
  }
  _server->KickOff(dpnid,netReason,reasonStr);
}

void NetworkServer::Ban(int dpnid)
{
  // add to ban list
  const PlayerIdentity *identity = FindIdentity(dpnid);
  if (!identity) return;
  _banListLocal.AddUnique(identity->id);

  // save ban list
  RString banList = GetUserDirectory() + RString("ban.txt");
  SaveBanList(banList, _banListLocal);

  // kick off
  _server->KickOff(dpnid,NTRBanned);
}

void NetworkServer::UpdateBanListSecure(const FindArray<RString> &keys, const Array<char> &signature)
{
  #if _VERIFY_CLIENT_KEY
  Assert(VerifySignedKeys(keys,signature)); // signature already verified
  if (keys.Size()>_banListSecure.keys.Size())
  {
    _banListSecure.keys = keys;
    _banListSecure.signature = signature;
    // output the secure key list
    RString banSecureFilename = GetUserDirectory() + RString("banSecure.txt");
    RString banListContent = PrintSignedKeys(_banListSecure.keys,_banListSecure.signature);
    QOFStream f(banSecureFilename);
    f << cc_cast(banListContent);
    f.close();
  }
  #endif
}


#if LOG_ERRORS
//! diagnostic logs of error (difference) between messages
/*
static void LogError
(
  NetworkMessageType type,
  int dpid1, NetworkMessage *msg1,
  int dpid2, NetworkMessage *msg2
)
{
  if (type < 0 || type >= NMTN) return;
  NetworkMessageFormat *format = GMsgFormats[type];

  float dt = msg1->time - msg2->time;
  float error = (
    msg1->CalculateErrorStatic(msg2) + 
    msg1->CalculateErrorDynamic(msg2) + CalculateErrorTime(type, msg1, msg2)
  );
  
  if (error > 0)
    DiagLogF
    (
      "    - %s: %g * %g = %g", (const char *)format->GetItem(i).name,
      coef, value, error
    );
  }
}
*/
#endif

#if _ENABLE_REPORT
  #define LOG_QUEUE_PROGRESS 0
#endif

/*!
\patch_internal 1.01 Date 6/26/2001 by Ondra. Added hacked version player kickoff.
*/
void NetworkServer::CheckFadeOut()
{
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
  for (int i=0; i<_players.Size(); i++)
    _players[i]->_steamValidate.HandleTimeout();
#endif

  if (_serverState == NSSPlaying)
  {
    for (int i=0; i<_identities.Size(); i++)
    {
      PlayerIdentity &identity = _identities[i];
/*
//testing only
      static volatile bool doIt=false;
      if (doIt)
      {
        if (identity.dpnid!=2)
          KickOff(identity.dpnid,KORBattlEye, "Hello boy!");
      }
*/
      if (Glob.uiTime>=identity.kickOffTime)
      {
        switch (identity.kickOffState)
        {
          case KOWait:
            {
              // transmit explanation
              identity.kickOffState = KOWarning;
              identity.kickOffTime = Glob.uiTime;
              RString string = LocalizeString(IDS_FADE_AWAY);
              RString senderName = identity.GetName();
              RefArray<NetworkObject> dummy;
              if (!ChannelDisabled(CCSystem))
                GNetworkManager.Chat(CCSystem,senderName,dummy,string);
              GChatList.Add(CCSystem, senderName, string, false, true);
            }
            break;
//((EXE_CRC_CHECK
          case KOExe:
//))EXE_CRC_CHECK
          case KOWarning:
            if (Glob.uiTime>identity.kickOffTime+15)
            {
//((EXE_CRC_CHECK
              bool wasExe = identity.kickOffState==KOExe;
//))EXE_CRC_CHECK
              identity.kickOffState = KODone;
              KickOff(identity.dpnid,KORFade);
//((EXE_CRC_CHECK
              if (wasExe)
              {
                RString format = LocalizeString(IDS_MP_VALIDERROR_1);

                RString message = Format(format, cc_cast(identity.GetName()));
                RefArray<NetworkObject> dummy;
                if (!ChannelDisabled(CCSystem))
                  GNetworkManager.Chat(CCSystem,"",dummy,message);
                GChatList.Add(CCSystem, NULL, message, false, true);
#if _ENABLE_DEDICATED_SERVER
                ConsoleF("%s", cc_cast(message));
#endif
              }
//))EXE_CRC_CHECK
              UpdateMatchmakingDescription(false);
            }
            break;
          default:
            Fail("KickOffState!");
          case KODone:
            break;
        }
      }
    }
  }
}

/*!
\patch_internal 1.50 Date 4/8/2002 by Jirka
- Fixed: No limits for server - bot client communication.
\patch 1.53 Date 4/23/2002 by Jirka
- Fixed: Bandwidth estimation on dedicated server
*/
void NetworkServer::EstimateBandwidth(NetworkPlayerInfo &pInfo, int nPlayers, int &nMsgMax, int &nBytesMax)
{
  if (pInfo.dpid == BOT_CLIENT || pInfo._isLocal)
  {
    nBytesMax = INT_MAX;
    nMsgMax = INT_MAX;

    pInfo._ping.Sample(0);
    pInfo._bandwidth.Sample(INT_MAX);
  }
  else
  {
    int nMsg = 0, nBytes = 0, nMsgG = 0, nBytesG = 0;

    // calculate quotas
    _server->GetSendQueueInfo(pInfo.dpid, nMsg, nBytes, nMsgG, nBytesG);
    if (nMsgG > 0)
    {
      nMsg += nMsgG;
      nBytes += nBytesG;
    }

    int minBandwidth;
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    if (IsDedicatedServer())
    {
      nMsgMax = DSMaxMsgSend;
      minBandwidth = DSMinBandwidth;
    }
    else
#endif
//}
    {
      nMsgMax = MaxMsgSend;
      minBandwidth = MinBandwidth;
    }
    int bandwidth = 100; // add 800 bps as a basic bandwidth
  #if _ENABLE_MP
    int latencyMS = 0, throughputBPS = 0;
    if (_server->GetConnectionInfo(pInfo.dpid, latencyMS, throughputBPS))
    {
      // nBytesMax += 1.25 * throughputBPS;
      bandwidth += throughputBPS;
      bandwidth += throughputBPS >> 2; // throughputBPS / 4
    }
  #endif
    // assume server upload bandwidth is at least MinBandwidth
    if (nPlayers > 0)
    {
      int lowLevelMaxBandwidth = INT_MAX;
      _server->GetConnectionLimits(lowLevelMaxBandwidth);
      // 8 is conversion from bps to Bps
      int minBandwidthPerPlayer = minBandwidth/(8*nPlayers);
      
      // min limit must never be higher than low-level max limit
      saturateMin(minBandwidthPerPlayer, lowLevelMaxBandwidth);
      saturate(bandwidth, minBandwidthPerPlayer, MaxBandwidth/8);
      if (bandwidth>lowLevelMaxBandwidth) bandwidth = lowLevelMaxBandwidth;
    }

    // float frmDur = GEngine->GetAvgFrameDuration();
    // LogF("  avgFrame: %.3f", frmDur);
    nBytesMax = toInt(0.001 * GEngine->GetAvgFrameDuration() * bandwidth);
  
    nBytesMax -= nBytes;
    nMsgMax -= nMsg;

    {
      // statistics
      int latencyMSRaw = 0, throughputBPSRaw = 0;
    #if _ENABLE_MP
      _server->GetConnectionInfoRaw(pInfo.dpid, latencyMSRaw, throughputBPSRaw);
    #endif
      pInfo._ping.Sample(latencyMSRaw);
      pInfo._bandwidth.Sample(throughputBPSRaw);
    }

    // write statistics
#if _ENABLE_CHEATS
    if (outputLogs) LogF("Server to %s: Limit (%d, %d)", (const char *)pInfo.name, nBytesMax, nMsgMax);
#endif
  }
}

int NetworkServer::GetBandwidthEstimation(int dpnid, int nPlayers)
{
  int bandwidth;
  if (dpnid == BOT_CLIENT)
  {
    bandwidth = INT_MAX;
  }
  else
  {
    int minBandwidth;
#if _ENABLE_DEDICATED_SERVER
    if (IsDedicatedServer()) minBandwidth = DSMinBandwidth;
    else
#endif
    {
      minBandwidth = MinBandwidth;
    }
    bandwidth = 100; // add 800 bps as a basic bandwidth
#if _ENABLE_MP
    int latencyMS = 0, throughputBPS = 0;
    if (_server->GetConnectionInfo(dpnid, latencyMS, throughputBPS))
    {
      // nBytesMax += 1.25 * throughputBPS;
      bandwidth += throughputBPS;
      bandwidth += throughputBPS >> 2; // throughputBPS / 4
    }
#endif
    // assume server upload bandwidth is at least MinBandwidth
    if (nPlayers > 0)
    {
      // 8 is conversion from bps to Bps
      int minBandwidthPerPlayer = minBandwidth/(8*nPlayers);

      int lowLevelMaxBandwidth = INT_MAX;
      _server->GetConnectionLimits(lowLevelMaxBandwidth);
      // min limit must never be higher than low-level max limit
      saturateMin(minBandwidthPerPlayer, lowLevelMaxBandwidth);
      saturate(bandwidth, minBandwidthPerPlayer, MaxBandwidth/8);
    }
  }
  return bandwidth;
}


void NetworkServer::SendCreateMessage( int dpnid, NetworkObjectInfo &info, NetworkPlayerObjectInfo *poInfo )
{
  if (DebugMeBoy(info.id))
  {
    LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} Create for %d", info.id.creator.CreatorInt(), info.id.id, dpnid);
  }
  SendMsg(dpnid, info.create.message, info.create.type, NMFGuaranteed, poInfo);
}

void NetworkServer::OnHierParentChange( Entity* veh )
{
  const Entity *root = veh;
  while (root->GetHierarchyParent()) root = root->GetHierarchyParent();
  NetworkId id = root->GetNetworkId();
  NetworkObjectInfo *rootInfo = GetObjectInfo(id);
  if (!rootInfo) return;
  NetworkObjectInfo *oInfo = GetObjectInfo(veh->GetNetworkId());
  if (!oInfo) return;

  auto bestPosNew = rootInfo->BestPosition();
  auto bestPosOld = oInfo->BestPosition();

  oInfo->_updInfo = bestPosNew;
  _objects.onPosChanged(oInfo,bestPosOld,bestPosNew);

  UpdateChildrenPos(veh);
}


float NetworkServer::CalculateErrorCoef(const NetworkPlayerInfo &pInfo, const NetworkObjectInfo &oInfo, bool updating)
{
  const float errorCoefMyVehicle = 2.0f;
  const float errorCoefMyBody = 10.0f;

  const NetworkCurrentInfo &update = oInfo.currentUpdate[NMCUpdateGeneric];
  if (update.message && !pInfo.person.IsNull())
  {
    if (pInfo.person==oInfo.id)
    {
      return errorCoefMyBody;
    }

    if (IsUpdateTransport(update.type))
    {
      // check if player is not inside given vehicle
      NetworkMessageUpdateTransport *msg = static_cast<NetworkMessageUpdateTransport *>(update.message.GetRef());
      if (msg)
      {
#if _VBS2
        if(msg->_UpdateCoef > 0.0f )
          return msg->_UpdateCoef;
#endif
        if (msg->_driver == pInfo.person) return errorCoefMyVehicle;
        for (int i=0; i<msg->_manCargo.Size(); i++)
        {
          if (msg->_manCargo[i] == pInfo.person) return errorCoefMyVehicle;
        }
      }
    }
    else if (update.type == NMTUpdateTurret)
    {
      // check if player is not inside given turret
      NetworkMessageUpdateTurret *msg = static_cast<NetworkMessageUpdateTurret *>(update.message.GetRef());
      if (msg)
      {
        if (msg->_gunner == pInfo.person) return errorCoefMyVehicle;
      }
    }
  }

  if (!oInfo.object)
  {
    // no object: this must be UpdateClientInfoDpid
    DoAssert(oInfo.currentUpdate[NMCUpdatePosition].type==NMTUpdateClientInfoDpid);
    return 1;
  }

  //auto pos = oInfo.BestPosition();
  NetworkObject *rootObj = oInfo.object;
  Entity *root = dyn_cast<Entity>(rootObj);
  if (root)
  {
    while (root->GetHierarchyParent()) root = root->GetHierarchyParent();
    rootObj = root;
  }
  Vector3 pos = rootObj->GetCurrentPosition();
  float updateDistance = rootObj->UpdateDistance();

  // calculate the error coef by object's position
  return oInfo.object->CalculateErrorCoef(pos, pInfo.cameraPosition, updateDistance, updating);
}


/*!
\patch_internal 1.12 Date 08/07/2001 by Jirka
- Added: error calculation diagnostics
\patch 5115 Date 1/8/2007 by Jirka
- Fixed: MP - fire effects sometimes did not transferred (especially for JIP players)
*/

#if DEBUG_GIVEN_NETWORK_ID
bool DebugMeBoy(NetworkId id)
{
  static NetworkId myId(CreatorId(2),6);
  return (id == myId);
}

bool DebugMeBoy(NetworkMessage *msg, NetworkMessageType type)
{
  static NetworkMessageType debugType = NMTUpdatePositionMan;
  if (type==debugType)
  {
    auto *msg1 = static_cast<NetworkMessageUpdatePositionVehicle *>(msg);
    return DebugMeBoy(NetworkId(msg1->_objectCreator, msg1->_objectId));
  }
  return false;
}

#endif

PlayerIndex NetworkServer::CreatePlayerIndex(int pid)
{
  PlayerIndex pIndex = GetPlayerIndex(pid);
  if (pIndex<0)
  {
    pIndex = (PlayerIndex)_indexToPlayerId.Add(pid);
    LogF("**** Player %d index %d added",pid,int(pIndex));
  }
  else
  {
    LogF("**** Player %d index %d used, %d total",pid,int(pIndex),_indexToPlayerId.Size());
  }
  return pIndex;
}

void NetworkServer::OnStopUpdating(NetworkObjectInfo &oInfo, NetworkPlayerInfo &pInfo, NetworkPlayerObjectInfo &poInfo)
{
  // send a destroy message
  DeleteObjectMessage msg;
  msg._creator = oInfo.id.creator;
  msg._id = oInfo.id.id;

  SendMsg(pInfo.dpid, &msg, NMFGuaranteed);

  for (int i=0; i<NMCUpdateN; i++) poInfo.updates[i].lastSentMsg = nullptr;
}


void NetworkServer::CreateObjectsList(AutoArray<UpdateObjectInfo, MemAllocSA> &objects, NetworkPlayerInfo &pInfo)
{
  PROFILE_SCOPE_EX(crOLS, network)
  // calculate errors and place object in list sorted by error

  PlayerIndex playerIndex = GetPlayerIndex(pInfo.dpid);
  DoAssert(playerIndex>=0);
  DoAssert(pInfo.dpid!=BOT_CLIENT); // this function cannot be used for bot client, as it implements player bubble

  NetworkPlayerInfo::UpdatingList updatingNew;

  auto handleUpdatingObject = [playerIndex,&pInfo,this,&objects,&updatingNew](NetworkObjectInfo *info)
  {
    NetworkObjectInfo &oInfo = *info;

    NetworkPlayerObjectInfo *poInfo = oInfo.GetPlayerObjectInfo(playerIndex,pInfo.dpid);

    float errCoef = CalculateErrorCoef(pInfo, oInfo, poInfo->updating);
    if (errCoef < 0) return;

    bool needsUpdate = errCoef>0 || pInfo.dpid==oInfo.owner;

    if (needsUpdate!=poInfo->updating)
    {
      if (DebugMeBoy(oInfo.id))
      {
        LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} Updating for %d state %d", oInfo.id.creator.CreatorInt(), oInfo.id.id, pInfo.dpid, needsUpdate);
      }
      if (needsUpdate)
      {
        // ClientInfoObject has no create message only NMTUpdateClientInfoDpid (position update)
        if (oInfo.create.message.NotNull())
        {
          SendCreateMessage(pInfo.dpid,oInfo,poInfo);
          ObjectUpdateAll(info, pInfo.dpid, NMFGuaranteed);
        }
      }
      else
      {
        // we could also skip this else branch and leave the cleanup to the pInfo._updating loop below
        DoAssert(pInfo._updating.CheckIntegrity());
        OnStopUpdating(oInfo, pInfo, *poInfo);
        pInfo._updating.Delete(poInfo);
        DoAssert(pInfo._updating.CheckIntegrity());
      }

      poInfo->updating = needsUpdate;
    }


    if (!needsUpdate) return;

    pInfo.markAsUpdating(poInfo,updatingNew);

    for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
    {
      // send update received from an owner to the bot client, so that is can cancel its rejection status
      NetworkCurrentInfo &info = pInfo.dpid==BOT_CLIENT && oInfo.rejected[j] && oInfo.currentRejected[j].message.NotNull() ? oInfo.currentRejected[j] : oInfo.currentUpdate[j];
      if (!info.message) continue;  // no current state

      if (&info==&oInfo.currentRejected[j])
      {
        LogF("Sending rejected %d for %d to BOT_CLIENT",j,oInfo.id.id);
      }
      NetworkUpdateInfo &update = poInfo->updates[j];
      
      if (oInfo.owner == pInfo.dpid)
      {
        if (pInfo.dpid!=BOT_CLIENT && info.from==BOT_CLIENT)
        {
          // bot-client may override owner information
        }
        else
        {
          continue;
        }
      }

      float error;
      // error without distance coef
      float errorNoCoef;
      if (!update.lastSentMsg)
      {
        errorNoCoef = info.message->CalculateErrorInitial();
        error = errCoef * errorNoCoef;
#if LOG_ERRORS
        DiagLogF("Initial error for %s %d:%" OID_FORMAT" to %d", NetworkMessageTypeNames[info.type], oInfo.id.creator.CreatorInt(), oInfo.id.id, pInfo.dpid);
        DiagLogF("  - error %.3f * %.3f = %.3f", errCoef, errorNoCoef, error);
#endif
      }
      else
      {
        NetworkMessage *msg1 = info.message;
        NetworkMessage *msg2 = update.lastSentMsg;

        DoAssert(!msg1->HasDiff());
        DoAssert(!msg2->HasDiff());

        if (!update.errorValid) 
        {
          /*
          if (info.type==NMTUpdateMan)
        {
          __asm nop;
        }
        */

          update.errorStatic = msg1->CalculateErrorStatic(msg2);
          update.errorValid = true;
        }
        float dt = msg1->time - msg2->time;
        float errorDynamic = msg1->CalculateErrorDynamic(msg2, dt);
        errorNoCoef = error = update.errorStatic + errorDynamic + CalculateErrorTime(info.type, msg1, msg2);

#if LOG_ERRORS
        if (!(error * errCoef < LogErrorLimit)) // handle NaN well
        {
          DiagLogF
          (
            "Object %s %d:%" OID_FORMAT" - update to %d",
            NetworkMessageTypeNames[info.type], oInfo.id.creator.CreatorInt(), oInfo.id.id,
            pInfo.dpid
          );
          DiagLogF("  - error %.3f = stat %.3f + dyn %.3f", error, update.errorStatic, errorDynamic);
          DiagLogF("  - error %.3f * %.3f = %.3f", errCoef, error, errCoef * error);
          if (pInfo.cameraPosition[1]!=-FLT_MAX)
          {
            DiagLogF
            (
              "  - camera [%.0f, %.0f, %.0f]",
              pInfo.cameraPosition[0], pInfo.cameraPosition[1], pInfo.cameraPosition[2]
            );
          }

          msg1->CalculateErrorStatic(msg2);
          msg1->CalculateErrorDynamic(msg2, dt);
          CalculateErrorTime(info.type, msg1, msg2);
        }
#endif
        error *= errCoef;
      }

#if DEBUG_GIVEN_NETWORK_ID
      if (DebugMeBoy(oInfo.id) && error>0)
      {
        float msgTime = update.lastSentMsg ? update.lastSentMsg->time.toFloat() : 0; 
        LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"}, Error: %.2f, lastMsgTime: %.3f", oInfo.id.creator.CreatorInt(), oInfo.id.id, error, msgTime);
      }
#endif

      // note: following condition seems to be identical to error>MinErrorToSend
      // but it is different when error is NaN
      if (!(error <= MinErrorToSend) && !(errorNoCoef <= MinErrorToSendNear))
      {
        int index = objects.Add();
        // objects[index].pInfo = &pInfo;
        objects[index].oInfo = &oInfo;
        objects[index].poInfo = poInfo;
        objects[index].cls = (NetworkMessageClass)j;
        objects[index].error = error;
        objects[index].errorNoCoef = errorNoCoef;
        objects[index].criticalUpdate = false;

        #if _DEBUG
          #define NOP __asm {nop}
          switch (info.type)
          {
            case NMTUpdateMan: NOP; break;
            case NMTUpdatePositionMan: NOP; break;
            case NMTUpdateVehicleAI: NOP; break;
            case NMTUpdateDamageVehicleAI: NOP; break;
            case NMTUpdateDamageVehicle: NOP; break;
            case NMTUpdateObject: NOP; break;
            case NMTUpdateDamageObject: NOP; break;
          }
          if (update.lastSentMsg)
          {
            // tracing opportunity
            // place breakpoint depending on message type
            switch (info.type)
            {
              case NMTUpdateMan: NOP; break;
              case NMTUpdatePositionMan: NOP; break;
              case NMTUpdateVehicleAI: NOP; break;
              case NMTUpdateDamageVehicleAI: NOP; break;
              case NMTUpdateDamageVehicle: NOP; break;
              case NMTUpdateObject: NOP; break;
              case NMTUpdateDamageObject: NOP; break;
            }
            NetworkMessage *msg1 = info.message;
            NetworkMessage *msg2 = update.lastSentMsg;
            float errorStatic = msg1->CalculateErrorStatic(msg2);
            float dt = msg1->time - msg2->time;
            float errorDynamic = msg1->CalculateErrorDynamic(msg2, dt);
            (void)errorStatic;
            (void)errorDynamic;
            NOP;
          }
          
        #endif
      }
    }
  };

  _objects.forEachInBubble(pInfo.cameraPosition, handleUpdatingObject);

  // only items in updatingNew should be kept as _updating, the rest needs to be marked as no longer updating
  DoAssert(pInfo._updating.CheckIntegrity());
  AUTO_STATIC_ARRAY(NetworkObjectInfo *, owned, 32);
  for (NetworkPlayerObjectInfo *poInfo=pInfo._updating.Start(); pInfo._updating.NotEnd(poInfo); poInfo=pInfo._updating.Advance(poInfo))
  {
    DoAssert(poInfo->updating);
    // oInfo from poInfo
    NetworkObjectInfo *oInfo = poInfo->oInfo;
    if (pInfo.dpid == oInfo->owner)
    {
      // delayed handling - handleUpdatingObject is changing pInfo._updating we are traversing
      owned.Add(oInfo);
    }
    else
    {
      int oCount = pInfo._updating.Size();
      OnStopUpdating(*oInfo, pInfo, *poInfo);
      int nCount = pInfo._updating.Size();
      DoAssert(oCount==nCount);
      poInfo->updating = false;
    }
  }
  for (int i=0; i<owned.Size(); i++) handleUpdatingObject(owned[i]);

  pInfo._updating.Clear();
  DoAssert(pInfo._updating.CheckIntegrity());

  pInfo._updating.Move(updatingNew);
  DoAssert(pInfo._updating.CheckIntegrity());
  
  for (int i=0; i<pInfo._delayedMessages.Size(); i++)
  {
    DelayedMessageInfo *item = pInfo._delayedMessages[i];

    float ageCoef = 1-(Glob.time-item->timeReceived)*item->invMaxAge;
    float errorCoef = 1;

	  if (pInfo.cameraPosition[0] != -FLT_MAX && item->pos[0]!=-FLT_MAX)
	  {
	    // both positions defined
	    // coef = limit / distance (maximum is 1)
	    const float limit = 20.0f;
	    float dist2 = item->pos.Distance2(pInfo.cameraPosition);
	    if (dist2 > Square(limit))
	    {
		    errorCoef = Square(limit) / dist2;
	    }
	  }
	
    float error = item->error*ageCoef;
    // if age is negative or zero, condition will certainly fail
    if (errorCoef*error<MinErrorToSend || error<MinErrorToSendNear)
    {
      pInfo._delayedMessages.Delete(i--);
      continue;
    }
    
    UpdateObjectInfo &oInfo = objects.Append();
    oInfo.errorNoCoef = error;
    oInfo.error = errorCoef*error;
    oInfo.msg = item;
    oInfo.cls = NMCUpdateN;
    oInfo.criticalUpdate = false;
  }

  // sort objects by errors
  QSort(objects.Data(), objects.Size(), CmpPlayerObjects);
}

template <class Type, class Coord>
class SpatialIndexBruteForce
{
  FindArrayKey<Type> _data;
public:
  void Add(Type item)
  {
    _data.Add(item);
  }
  bool Remove(Vector3Par oldPos, Type item)
  {
    return _data.DeleteKey(item);
  }

  template <class Function>
  void ForEach(Vector3Par pos, float maxDistance, const Function &f)
  {
    auto fInBubble = [pos,maxDistance,&f](Type oInfo)->bool
    {
      DoAssert(oInfo->object->UpdateDistance()==maxDistance);
      auto oPos = oInfo->BestPosition();
      DoAssert(oPos.second);
      float dist2 = pos.Distance2(oPos.first);
      if (dist2<Square(maxDistance))
      {
        f(oInfo);
      }
      return false; // false means continue searching in FindArrayKey
    };
    _data.ForEachF(fInBubble);
  }
};

template <class Type, class Coord>
class SpatialIndexNaive
{
  static const float Grid() {return 100.0f;}

  static int SlotNum(float posX) {return toInt(posX * (1.0f/Grid()));}

  typedef FindArrayKey<Type> Slot;

  AutoArray<Slot> _slot;
  int _slot0;

public:
  SpatialIndexNaive()
  :_slot0(0)
  {
  }
  void Add(Type item)
  {
    Vector3 pos = item->BestPosition()._pos;
    DoAssert(pos.SquareSize()>0);
    int slot = SlotNum(pos.X());

    if (_slot.MaxSize()==0) _slot0 = slot; // empty list, set arbitrary beginning
    if (slot<_slot0)
    {
      _slot.InsertMultiple(0,_slot0-slot);
      _slot0 = slot;
    }
    else if (slot-_slot0>=_slot.Size())
    {
      _slot.Resize(slot-_slot0+1);
    }
    _slot[slot-_slot0].Add(item);
  }
  bool Remove(Vector3Par oldPos, Type item)
  {
    int slot = SlotNum(oldPos.X());
    DoAssert(slot>=_slot0 && slot<_slot0+_slot.Size());
    return _slot[slot-_slot0].DeleteKey(item);
  }

  template <class Function>
  void ForEach(Vector3Par pos, float maxDistance, const Function &f)
  {
    if (_slot.Size()<=0) return;
    int minSlot = SlotNum(pos.X()-maxDistance);
    int maxSlot = SlotNum(pos.X()+maxDistance);
    int minSlotI = minSlot-_slot0;
    int maxSlotI = maxSlot-_slot0;
    saturate(minSlotI,0,_slot.Size()-1);
    saturate(maxSlotI,0,_slot.Size()-1);
    for (int i=minSlotI; i<=maxSlotI; i++)
    {
      auto fInBubble = [pos,maxDistance,&f](Type oInfo)->bool
      {
        // TODO: encapsulate oInfo access in Type traits
        auto oPos = oInfo->BestPosition();
        DoAssert(oPos._updateDistance==maxDistance);
        float dist2 = pos.Distance2(oPos._pos);
        if (dist2<Square(maxDistance))
        {
          f(oInfo);
        }
        return false; // false means continue searching in FindArrayKey
      };
      _slot[i].ForEachF(fInBubble);
    }
  }
};

template <class Type, class Coord>
class SpatialIndexRTree
{
  RTree<Type,float,2> _data;
public:
  void Add(Type item)
  {
    Vector3 pos = item->BestPosition()._pos;
    float vec[2] = {pos[0],pos[2]};
    _data.Insert(vec,vec,item);
  }
  bool Remove(Vector3Par oldPos, Type item)
  {
    float vec[2] = {oldPos[0],oldPos[2]};
    return _data.Remove(vec,vec,item);
  }

  template <class Function>
  void ForEach(Vector3Par pos, float maxDistance, const Function &f)
  {
    auto fInBubble = [pos,maxDistance,&f](Type oInfo)->bool
    {
      auto oPos = oInfo->BestPosition();
      DoAssert(oPos._updateDistance==maxDistance);
      float dist2 = pos.Distance2(oPos._pos);
      if (dist2<Square(maxDistance))
      {
        f(oInfo);
      }
      return true; // true means continue searching in RTree
    };
    Vector3 minPos = pos-Vector3(maxDistance);
    Vector3 maxPos = pos+Vector3(maxDistance);
    float minPosF[2] = {minPos[0],minPos[2]};
    float maxPosF[2] = {maxPos[0],maxPos[2]};
    _data.Search(minPosF,maxPosF,fInBubble);
  }
};

/**
Encapsulation of SpatialIndexRTree, used for testing: compares results against SpatialIndexNaive
*/
template <class Type, class Coord>
class SpatialIndexRTreeChecked
{
  SpatialIndexNaive<Type,Coord> _ref;
  SpatialIndexRTree<Type,Coord> _tst;

  public:
  void Add(Type item)
  {
    _ref.Add(item);
    _tst.Add(item);
  }
  bool Remove(Vector3Par oldPos, Type item)
  {
    bool ok1 = _ref.Remove(oldPos,item);
    bool ok2 = _tst.Remove(oldPos,item);
    DoAssert(ok1==ok2);
    return ok1;
  }

  template <class Function>
  void ForEach(Vector3Par pos, float maxDistance, const Function &f)
  {
    _ref.ForEach(pos,maxDistance,f);

    FindArrayKey<Type> visitedByRef;
    FindArrayKey<Type> visitedByTst;

    _ref.ForEach(pos,maxDistance,[&visitedByRef](Type oInfo){visitedByRef.Add(oInfo);});
    _tst.ForEach(pos,maxDistance,[&visitedByTst](Type oInfo){visitedByTst.Add(oInfo);});

    DoAssert(visitedByTst.Size()==visitedByRef.Size());

    // verify identity
    for (int i=0; i<visitedByTst.Size(); i++)
    {
      DoAssert(visitedByRef.FindKey(visitedByTst[i])>=0);
    }
    for (int i=0; i<visitedByRef.Size(); i++)
    {
      DoAssert(visitedByTst.FindKey(visitedByTst[i])>=0);
    }


  }
};

struct ObjectsSpatialLayerImpl: public ObjectsSpatialLayerWithDistance
{
  SpatialIndexRTree<NetworkObjectInfo *,Vector3> _index;

  ObjectsSpatialLayerImpl(float distance):ObjectsSpatialLayerWithDistance(distance){}
  virtual void Add(NetworkObjectInfo *oInfo) override;
  virtual bool Remove(Vector3Par oldPos, NetworkObjectInfo *oInfo) override;
  virtual void ForEach( Vector3 cameraPos, const Function &function );

};

void ObjectsSpatialLayerImpl::Add(NetworkObjectInfo *oInfo)
{
  _index.Add(oInfo);
}

bool ObjectsSpatialLayerImpl::Remove(Vector3Par oldPos, NetworkObjectInfo *oInfo)
{
  DoAssert(oldPos.SquareSize()>0);
  return _index.Remove(oldPos,oInfo);
}

void ObjectsSpatialLayerImpl::ForEach( Vector3 cameraPos, const Function &function )
{
  _index.ForEach(cameraPos,_distance,function);
}

struct ObjectsSpatialLayerNoBubble: public ObjectsSpatialLayer
{
  FindArrayKey<NetworkObjectInfo *> _index;

  virtual void Add(NetworkObjectInfo *oInfo) override {_index.Add(oInfo);}
  virtual bool Remove(Vector3Par oldPos, NetworkObjectInfo *oInfo) override {return _index.DeleteKey(oInfo);}
  virtual void ForEach(Vector3 cameraPos, const Function &function)
  {
    _index.ForEachF([function](NetworkObjectInfo *oInfo)->bool {function(oInfo);return false;});
  }

};


NetworkServer::ObjectsSpatialIndex::ObjectsSpatialIndex()
{

}
ObjectsSpatialLayer &NetworkServer::ObjectsSpatialIndex::Distance(float dist)
{
  if (dist<FLT_MAX && dist>0)
  {
    int index = _layers.FindKey(dist);
    if (index>=0) return *_layers[index];
    return *_layers.Append(new ObjectsSpatialLayerImpl(dist));
  }
  else
  {
    if (_noLayer.IsNull()) _noLayer = new ObjectsSpatialLayerNoBubble();
    return *_noLayer;
  }
}

bool NetworkServer::ObjectsSpatialIndex::put( TargetType value )
{
  addToLayer(value);
  return _all.put(value);
}

void NetworkServer::ObjectsSpatialIndex::removeKey ( KeyType key )
{
  TargetType value;
  if (_all.removeKey(key,&value))
  {
    auto pos = value->BestPosition();
    removeFromLayer(pos._pos,pos._updateDistance,value);
  }
}


void NetworkServer::ObjectsSpatialIndex::forEachInBubble(Vector3 cameraPos, const ObjectsSpatialLayer::Function &function)
{
  for (int i=0; i<_layers.Size(); i++)
  {
    _layers[i]->ForEach(cameraPos,function);
  }
  _noLayer->ForEach(cameraPos,function);
}


void NetworkServer::ObjectsSpatialIndex::onPosChanged(NetworkObjectInfo *oInfo, const NetworkObjectInfo::UpdateInfo &oldPos, const NetworkObjectInfo::UpdateInfo &newPos)
{
  if (oldPos._pos!=newPos._pos || oldPos._updateDistance!=newPos._updateDistance)
  {
    removeFromLayer(oldPos._pos,oldPos._updateDistance,oInfo);
    addToLayer(oInfo);
  }
}

void NetworkServer::ObjectsSpatialIndex::reset ()
{
  _noLayer.Free();
  _layers.Clear();
  _all.reset();
}




/*!
\patch 1.27 Date 10/09/2001 by Ondra
- Fixed: Ping times on clients were not updated when mission was not running.
\patch_internal 1.47 Date 3/11/2002 by Ondra
- Fixed: Improved latency on kill.
\patch_internal 1.48 Date 3/12/2002 by Ondra
- Fixed: Latency on kill further improved.
Bug introduced in 1.47 beta causing increased lag of movement fixed.
\patch_internal 1.49 Date 3/14/2002 by Ondra
- Fixed: Latence handling code reverted to version 1.45.
\patch 1.53 Date 4/23/2002 by Ondex
- Fixed: Kill/dammage message have now higher priority - this should improve latency on kill.
*/
void NetworkServer::SendMessages()
{
  //DoAssert(CheckBestPositionIntegrity());
#if _ENABLE_CHEATS
  // remove statistics info
  static AutoArray<int> texts;
  for (int i=0; i<texts.Size(); i++)
  {
    if (texts[i] >= 0) GEngine->RemoveText(texts[i]);
  }
  texts.Resize(0);
#endif

#if _ENABLE_CHEATS
  #define LOG_DESYNC 1
#endif

#if LOG_DESYNC
  static DWORD time = 0;
  DWORD now = GlobalTickCount();
  bool logDesync = false;
  if (now >= time + 1000)
  {
    logDesync = true;
    time = now;
  }
#endif

  int nPlayers = _identities.Size() - 1;
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (IsDedicatedServer()) nPlayers++;
#endif
//}
#define DIAGNOZE_BANDWIDTH 0
#if DIAGNOZE_BANDWIDTH
  bool doDiagnozeBandwidth = false;
  static DWORD nextDiagTime = GetTickCount()+5000;
  DWORD curTime = GetTickCount();
  if (curTime>=nextDiagTime)
  {
    nextDiagTime=curTime+5000;
    doDiagnozeBandwidth = true;
  }
#endif

  for (int i=0; i<_players.Size(); i++)
  {
    NetworkPlayerInfo &pInfo = *_players[i];

    int nMsg = 0;
    int nBytes = 0;
    int nMsgMax, nBytesMax;

    //LogF("  NsEstB %d, %d Bytes", nMsgMax, nBytesMax);
    EstimateBandwidth(pInfo, nPlayers, nMsgMax, nBytesMax);
#if DIAGNOZE_BANDWIDTH
    int diagGMsg=0, diagGBytes=0;  //state of nMsgMax and nBytesMax after the guaranteed messages are sent
    int diagCMsg=0, diagCBytes=0;  //state ... after critical updates are sent
#endif    
    //LogF("  NsEstB %d, %d Bytes", nMsgMax, nBytesMax);

  #if LOG_QUEUE_PROGRESS
    if (pInfo._messageQueue.Size() > 0)
    {
      LogF
      (
        "%s: nBytesMax %d, nBytes %d, queue size %d",
        (const char *)pInfo.name,
        nBytesMax, nBytes, pInfo._messageQueue.Size()
      );
    }
  #endif

    // send enqueued guaranteed messages
    int maxSize = MaxSizeGuaranteed;

    const int nMsgMaxGuaranteed = nMsgMax;
    const int nBytesMaxGuaranteed = nBytesMax;

    while (pInfo._messageQueue.Size() > 0)
    {
      if (nMsg >= nMsgMaxGuaranteed || nBytes >= nBytesMaxGuaranteed)
      {
      #if LOG_QUEUE_PROGRESS
        int qBytes = 0;
        for (int i=0; i<pInfo._messageQueue.Size(); i++)
        {
          qBytes += pInfo._messageQueue[i]->msg->size;
        }
        LogF
        (
          "  -- queue left %d (%d B)",pInfo._messageQueue.Size(),qBytes
        );
      #endif
        break;
      }

      NetworkMessage *msg = pInfo._messageQueue[0]->msg;
      int size = msg->size;
      if (size >= maxSize)
      {
        //DWORD dwMsgId =
        RefR<NetMessage, RefCountSafe> resultMsg;
        /*int rawSize = */SendMsgRemote(pInfo.dpid, msg, pInfo._messageQueue[0]->type, NMFGuaranteed|NMFStatsAlreadyDone, &resultMsg, NULL);
        pInfo._messageQueue[0]->SetFence(resultMsg);
        pInfo._messageQueue.Delete(0);
        nMsg++;
        nBytes += size;
      }
      else
      {
        int totalSize = 0;
        int i;
        for (i=0; i<pInfo._messageQueue.Size(); i++)
        {
          NetworkMessage *msg = pInfo._messageQueue[i]->msg;
          int size = msg->size;
          if (totalSize + size > maxSize) break;
          totalSize += size;
        }
        /*int rawSize = */SendMsgQueue(pInfo.dpid, pInfo._messageQueue, 0, i, NMFGuaranteed);
        pInfo._messageQueue.Delete(0, i);
        nMsg++;
        nBytes += totalSize;
      }
    }

    #if LOG_QUEUE_PROGRESS
    if (nBytes>0)
    {
      LogF
      (
        "  -- nMsg %d (nBytes %d)",nMsg,nBytes
      );
      if (pInfo._messageQueue.Size()==0)
      {
        LogF("  ** Queue empty");
      }
    }
    #endif

    AUTO_STATIC_ARRAY(UpdateObjectInfo, objects, 256);

  #define ENABLE_CRITICAL_UPDATES 1

#if DIAGNOZE_BANDWIDTH
    diagGBytes = nBytes;
    diagGMsg = nMsg;
#endif

  #if !ENABLE_CRITICAL_UPDATES
    if (nMsg >= nMsgMax || nBytes >= nBytesMax)
    {
      #if _ENABLE_CHEATS
        if (outputLogs) LogF("  Server to %s: limit reached (guaranteed)", (const char *)pInfo.name);
      #endif
      goto DoNotSendUpdates;
    }
  #endif

    // update objects
    if (_serverState == NSSPlaying && pInfo.clientState == NCSPlaying)
    {
      // update errors of objects
      if (pInfo.dpid!=BOT_CLIENT) // bot client is sent all updates directly when receiving them
      {
        CreateObjectsList(objects, pInfo);

        ClientOutput *obj = pInfo._clientInput->_output;
        obj->OnBeforeSend(pInfo);
        if (!pInfo._clientInput->_outputLast || obj->Compare(pInfo._clientInput->_outputLast))
        {
          // TODO: bandwith control
          NetworkMessageType type = obj->GetNMType(NMCUpdateGeneric);
          Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
          msg->time = Glob.time;
          NetworkMessageContext ctx(msg, this, pInfo.dpid, MSG_SEND);
          ctx.SetClass(NMCUpdateGeneric);
          obj->TransferMsg(ctx);

          SendMsg(pInfo.dpid,msg,type,NMFGuaranteed, NULL);

          // TransferMsg may modify the state
          if (!pInfo._clientInput->_outputLast) pInfo._clientInput->_outputLast = new ClientOutput(*obj);
          else *pInfo._clientInput->_outputLast = *obj;
          obj->OnSent();
        }
      }

      // create and send critical updates
    #if ENABLE_CRITICAL_UPDATES
      PlayerIndex playerIndex = GetPlayerIndex(pInfo.dpid);
      for (int i=0; i<objects.Size(); i++)
      {
        UpdateObjectInfo &info = objects[i];
        NetworkObjectInfo *oInfo = info.oInfo;
        if (!oInfo) continue; //! NULL oInfo is legit, see NetworkServer::PrepareNextUpdate. Different type of info containing msg (There can be a) oInfo etc. XOR b) msg (delayed messages)
        NetworkMessageClass cls = info.cls;

        float error = info.error;
        float errorNoCoef = info.errorNoCoef;

#if DEBUG_GIVEN_NETWORK_ID
        if (DebugMeBoy(oInfo->id))
        {
          float times[NMCUpdateN];
          NetworkPlayerObjectInfo *poInfo = oInfo->GetPlayerObjectInfo(playerIndex,pInfo.dpid);
          for (int j=0; j<NMCUpdateN; j++) times[j] = poInfo->updates[j].lastSentMsg ? poInfo->updates[j].lastSentMsg->time.toFloat() : 0;
          LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} has error %.2f, position in _objects = %d, lastMsgSent: %.02f, %.02f, %.02f", 
            oInfo->id.creator.CreatorInt(), oInfo->id.id, error, i, times[NMCUpdateGeneric], times[NMCUpdatePosition], times[NMCUpdateDamage]);
        }
#endif

        // any message about dammage that transmit change
        // that could mean dead should be considered high priority
        // errorNoCoef - error with no regard to distance
        // error - distance adjusted error
        if
        (
          errorNoCoef < ERR_COEF_IMMEDIATE &&
          ( cls != NMCUpdateDamage || error < ERR_COEF_STRUCTURE * 0.01 || errorNoCoef < ERR_COEF_STRUCTURE )
        ) continue;

        info.criticalUpdate = true;
        //TODO: do not use fence for high priority updates
        NetworkPlayerObjectInfo *poInfo = oInfo->GetPlayerObjectInfo(playerIndex,pInfo.dpid);
        int bytes;
        if (!poInfo->_fence || poInfo->_fence->WasReceived())
        {
          bytes = UpdateObject(&pInfo, oInfo, cls, NMFGuaranteed | NMFHighPriority);
        }
        else
        {
          bytes = UpdateObject(&pInfo, oInfo, cls, NMFGuaranteed);
        }        

      #if _ENABLE_CHEATS
        if (outputLogs)
        {
          LogF
          (
            "  Server to %s: Object %d:%" OID_FORMAT" updated - size %d bytes, critical",
            (const char *)pInfo.name, info.oInfo->id.creator.CreatorInt(), info.oInfo->id.id, bytes
          );
        }
      #endif

        if (bytes >= 0)
        {
          // no error
          nMsg++;
          nBytes += bytes;
        }
      }

#if DIAGNOZE_BANDWIDTH
      diagCBytes = nBytes;
      diagCMsg = nMsg;
#endif

      if (nMsg >= nMsgMax || nBytes >= nBytesMax)
      {
      #if _ENABLE_CHEATS
        if (outputLogs) LogF("  Server to %s: limit reached (guaranteed)", (const char *)pInfo.name);
      #endif
        goto DoNotSendUpdates;
      }
    #endif // ENABLE_CRITICAL_UPDATES
    }

DoNotSendUpdates:
    // send nonguaranteed enqueued messages
    maxSize = MaxSizeNonguaranteed;
    // note: even "nonguaranteed" may be actually sent as guaranteed, but they still have their own queue
    NetMsgFlags flags = _updatesGuaranteed ? NMFGuaranteed : NMFNone;
    int next = 0; // index of next object to update
    bool empty = false;
    while (true)
    {
      // enforce send last message
      if (!empty || pInfo._messageQueueNonGuaranteed.Size() <= 0)
      {
        if (nMsg >= nMsgMax || nBytes >= nBytesMax) break;
      }

      if (pInfo._messageQueueNonGuaranteed.Size() <= 0)
      {
        empty = true;
        // starving, try to create further message
        if (!PrepareNextUpdate(pInfo, objects, next)) break;
        // local communication - message was already sent
        if (pInfo.dpid == BOT_CLIENT)
        {
          DoAssert(pInfo._messageQueueNonGuaranteed.Size() == 0);
          nMsg++;
          continue;
        }
        DoAssert(pInfo._messageQueueNonGuaranteed.Size() > 0);
      }

      NetworkMessageQueueItem *item = pInfo._messageQueueNonGuaranteed[0];
      int size = item->msg->size;
      // enforce send last message
      if (size >= maxSize || nMsg >= nMsgMax || nBytes >= nBytesMax)
      {
        RefR<NetMessage, RefCountSafe> resultMsg;
        int rawSize = SendMsgRemote(pInfo.dpid, item->msg, item->type, NMFStatsAlreadyDone|flags, &resultMsg, item->dependOn);
        //item->SetFence(resultMsg); //nonquaranteed msg cannot raise a fence
        

#if LOG_SEND_PROCESS
        LogF("Server: Message %x sent", dwMsgId);
#endif
        pInfo._messageQueueNonGuaranteed.Delete(0);
        nMsg++;
        nBytes += rawSize;
      }
      else
      {
        int totalSize = 0;
        int i;
        for (i=0; true; i++)
        {
          if (i >= pInfo._messageQueueNonGuaranteed.Size())
          {
            empty = true;
            // starving, try to create further message
            if (!PrepareNextUpdate(pInfo, objects, next)) break;
            DoAssert(pInfo.dpid != BOT_CLIENT);
            DoAssert(i < pInfo._messageQueueNonGuaranteed.Size());
          }

          NetworkMessageQueueItem *item = pInfo._messageQueueNonGuaranteed[i];
          int size = item->msg->size;
          if (totalSize + size > maxSize) break;
          totalSize += size;
        }

        for (int j=0; j<i; j++)
        {
          NetworkMessageQueueItem &item = *pInfo._messageQueueNonGuaranteed[j];

          if (item.dependOn)
          { //it depends on previous guaranteed update and cannot be aggregated. We sent it immediately by setting begin
            RefR<NetMessage, RefCountSafe> resultMsg;
            int rawSize = SendMsgRemote(pInfo.dpid, item.msg, item.type, NMFStatsAlreadyDone|flags, &resultMsg, item.dependOn);
            //item.SetFence(resultMsg); //nonguaranteed messages could not raise a fence
            pInfo._messageQueueNonGuaranteed.Delete(j);
            i--; //one less items to process
            j--; //the same index contains new data
            nMsg++;
            nBytes += rawSize;
          }
        }

        if (i>0)
        {
          int rawSize = SendMsgQueue(pInfo.dpid, pInfo._messageQueueNonGuaranteed, 0, i, flags);
#if LOG_SEND_PROCESS
          LogF("Server: Message %x sent", dwMsgId);
#endif
          pInfo._messageQueueNonGuaranteed.Delete(0, i);
          nMsg++;
          nBytes += rawSize;
        }
      }
    }
    DoAssert(!empty || pInfo._messageQueueNonGuaranteed.Size() == 0);

    // search for maximum error not sent
    //float sumError = 0; // messages not sent total error
    float sumError = 0;
    for (int i=next; i<objects.Size(); i++)
    {
      // FLT_MAX means message is new. We have no idea how serious visual impact such message has
      // we ignore it for purpose of error calculation
      if (objects[i].error < FLT_MAX)
      {
        sumError = objects[i].error;
        break;
      }
     }

    // avoid flt_max error
    saturateMin(sumError,1e9);
    pInfo._desync.Sample(sumError,1e-3);

#if 0 //LOG_DESYNC
    if (logDesync) ConsoleF("%s: desync %8.2f", (const char *)pInfo.name, sumError); // debug log - need not to be localized
#endif
    

    // show server diagnostics
#if _ENABLE_CHEATS

    if (outputDiags == ODMsgQueue+NOutputDiags)
    {
      int count = pInfo._messageQueue.Size(), size = 0;
      for (int i=0; i<count; i++) size += pInfo._messageQueue[i]->msg->size;

      RString output = Format("%s: HLWait%3d(%5dB)", (const char *)pInfo.name, count, size);
      texts.Add(GEngine->ShowTextF(1000, output));
      texts.Add(GEngine->ShowTextF(1000, _server->GetStatistics(pInfo.dpid)));
      
      StatisticsById stats;
      for (int i=0; i<count; i++)
      {
        const NetworkMessageQueueItem *item = pInfo._messageQueue[i];
        stats.Count(safe_cast<int>(item->type),item->msg->size);
      }
      stats.SortData(false);
      int maxLines = 10;
      for (int i=0; i<stats.Size() && i<maxLines; i++)
      {
        const StatisticsById::Item &item = stats.Get(i);
        RString name = GetMsgTypeName((NetworkMessageType)item.id._id1);
        texts.Add(GEngine->ShowTextF(1000, "  %s: %d", cc_cast(name), item.count));
      }
    }
#endif
  }
}

bool NetworkServer::PrepareNextUpdate(NetworkPlayerInfo &pInfo, AutoArray<UpdateObjectInfo, MemAllocSA> &objects, int &next)
{
  while (next < objects.Size())
  {
    UpdateObjectInfo &info = objects[next++];
    if (info.criticalUpdate) continue;

/*
    #if _ENABLE_REPORT
    int oldCount = pInfo._messageQueueNonGuaranteed.Size();
    #endif
*/
    NetworkObjectInfo *oInfo = info.oInfo;
    if (oInfo)
    {
      NetworkMessageClass cls = info.cls;
      int bytes = UpdateObject(&pInfo, oInfo, cls, NMFNone);
      if (bytes < 0) continue;
  #if _ENABLE_CHEATS
      if (outputLogs) LogF("  Server to %s: Object updated - size %d bytes", (const char *)pInfo.name, bytes);
  #endif
    }
    else
    {
      //NetworkComponent::SendMsg(pInfo.dpid, info.msg, info.type, NMFGuaranteed);
      // non-guaranteed enqueues into the same queue as UpdateObject
      SendMsg(pInfo.dpid, info.msg->msg, info.msg->type, NMFNone, NULL);
      // set error to zero so that message is deleted
      info.msg->error = 0;
  #if _ENABLE_CHEATS
      if (outputLogs) LogF("  Server to %s: Delayed message sent", (const char *)pInfo.name);
  #endif
    }
/*
    #if _ENABLE_REPORT
    if (oldCount == pInfo._messageQueueNonGuaranteed.Size())
    {
      Fail("No message added during NetworkServer::PrepareNextUpdate");
      continue;
    }
    #endif
*/

    return true;
  }
  return false;
}

#if _HELISIM_ROTORLIB
  #define ROTOR_LIB_MAP_UPDATE_TO_TYPE(XXXX) \
    XXXX(NMTUpdatePositionHelicopterRTD,HelicopterRTD) \
    XXXX(NMTUpdateHelicopterRTD,HelicopterRTD)
  #define ROTOR_LIB_MAP_CREATE_TO_TYPE(XXXX) \
    XXXX(NMTCreateHelicopterRTD,HelicopterRTD)
#else
  #define ROTOR_LIB_MAP_UPDATE_TO_TYPE(XXXX)
  #define ROTOR_LIB_MAP_CREATE_TO_TYPE(XXXX)
#endif

#define MAP_UPDATE_TO_TYPE(XXXX) \
  XXXX(NMTUpdateDamageObject,Object) \
  XXXX(NMTUpdateObject,Object) \
  XXXX(NMTUpdatePositionVehicle,Vehicle) \
  XXXX(NMTUpdateDamageVehicle,Vehicle) \
  XXXX(NMTUpdateVehicle,Vehicle) \
  XXXX(NMTUpdateDetector,Detector) \
  XXXX(NMTUpdateFlag,FlagCarrier) \
  XXXX(NMTUpdateShot,Shot) \
  XXXX(NMTUpdateMine,Mine) \
  XXXX(NMTUpdateWeapon,Weapon) \
  XXXX(NMTUpdateDamageVehicleAI,EntityAI) \
  XXXX(NMTUpdateVehicleAI,EntityAI) \
  XXXX(NMTUpdateVehicleBrain,Person) \
  XXXX(NMTUpdateTransport,Transport) \
  XXXX(NMTUpdatePositionMan,Man) \
  XXXX(NMTUpdateMan,Man) \
  XXXX(NMTUpdateTankOrCar,TankOrCar) \
  XXXX(NMTUpdatePositionTank,TankWithAI) \
  XXXX(NMTUpdateTank,TankWithAI) \
  XXXX(NMTUpdatePositionMotorcycle,Motorcycle) \
  XXXX(NMTUpdateMotorcycle,Motorcycle) \
  XXXX(NMTUpdatePositionCar,Car) \
  XXXX(NMTUpdateCar,Car) \
  XXXX(NMTUpdatePositionAirplane,AirplaneAuto) \
  XXXX(NMTUpdateAirplane,AirplaneAuto) \
  XXXX(NMTUpdatePositionHelicopter,HelicopterAuto) \
  XXXX(NMTUpdateHelicopter,HelicopterAuto) \
  XXXX(NMTUpdateParachute,ParachuteAuto) \
  XXXX(NMTUpdatePositionShip,ShipWithAI) \
  XXXX(NMTUpdateShip,ShipWithAI) \
  XXXX(NMTUpdatePositionDoor,Door) \
  XXXX(NMTUpdateDoor,Door) \
  XXXX(NMTUpdatePositionSeagull,SeaGullAuto) \
  XXXX(NMTUpdateSeagull,SeaGullAuto) \
  XXXX(NMTUpdateClientInfo,ClientInfoObject) \
  XXXX(NMTUpdateClientInfoDpid,ClientInfoObject) \
  XXXX(NMTUpdateFireplace,Fireplace) \
  XXXX(NMTUpdatePositionTurret,Turret) \
  XXXX(NMTUpdateTurret,Turret) \
  XXXX(NMTAIStatsMPRowUpdate,AIStatsMPRow) \
  XXXX(NMTUpdateClientCameraPosition,ClientCameraPositionObject) \
  XXXX(NMTUpdateInventoryItem,InventoryItem) \
  XXXX(NMTUpdateItemRadio,ItemRadio) \
  XXXX(NMTUpdateItemTransmitter,ItemTransmitter) \
  ROTOR_LIB_MAP_UPDATE_TO_TYPE(XXXX)

#define MAP_CREATE_TO_TYPE(XXXX) \
  XXXX(NMTCreateObject,Object) \
  XXXX(NMTCreateVehicle,Vehicle) \
  XXXX(NMTCreateCrater,Crater) \
  XXXX(NMTCreateCraterOnVehicle,CraterOnVehicle) \
  XXXX(NMTCreateSoundOnVehicle,SoundOnVehicle) \
  XXXX(NMTCreateObjectDestructed,ObjectDestructed) \
  XXXX(NMTCreateDynSoundSource,DynSoundSource) \
  XXXX(NMTCreateShot,Shot) \
  XXXX(NMTCreateHelicopter,HelicopterAuto) \
  XXXX(NMTCreateSeagull,SeaGullAuto) \
  XXXX(NMTCreateTurret,Turret) \
  XXXX(NMTAIStatsMPRowCreate,AIStatsMPRow) \
  XXXX(NMTCreateClientCameraPosition,ClientCameraPositionObject) \
  XXXX(NMTCreateDestructionEffects,ItemTransmitter) \
  ROTOR_LIB_MAP_CREATE_TO_TYPE(XXXX)

#define UPDATE_TO_TYPE_DISTANCE(typeNMT,Type) case typeNMT: return Type::UpdateDistance();

/**
@param msg1 last message received from an owner
@param msg2 last message sent to a remote client
*/
float NetworkServer::CalculateErrorTime
(
 NetworkMessageType type, NetworkMessage *msg1, NetworkMessage *msg2
)
{
  if (type < 0 || type >= NMTN)
  {
    RptF("Server: Bad message %d format", (int)type);
    return 0.0;
  }

  float errCoefTime;
  switch (type)
  {
  case NMTUpdatePositionVehicle:
  case NMTUpdatePositionMan:
  case NMTUpdatePositionTank:
  case NMTUpdatePositionCar:
  case NMTUpdatePositionMagazine:
  case NMTUpdatePositionAirplane:
  case NMTUpdatePositionHelicopter:
#if _HELISIM_ROTORLIB    
  case NMTUpdatePositionHelicopterRTD:
#endif
  case NMTUpdatePositionShip:
  case NMTUpdatePositionDoor:
  case NMTUpdatePositionSeagull:
  case NMTUpdatePositionMotorcycle:
    errCoefTime = ERR_COEF_TIME_POSITION;
    break;
  default:
    errCoefTime = ERR_COEF_TIME_GENERIC;
    break;
  }

  // update to clients on receiving on an update from an owner
  float dt = msg1->time - msg2->time;
  return errCoefTime * dt;
}

/*!
\patch_internal 5102 Date 12/14/2006 by Jirka
- Fixed: Starting of MP missions in Retail Demo was broken
*/

void NetworkServer::SendMissionFile(int dpnid)
{
#if _ENABLE_UNSIGNED_MISSIONS

// RptF("*** Server: Send mission file to %d", dpnid);

  bool found = false;
  bool foundParams = false;
  NetworkPlayerInfo *player = NULL;
  if (dpnid == 0)
  {
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = *_players[i];
      if (info.clientState != NCSPlayerAssigned) continue;
      if (info.dpid == BOT_CLIENT)
      {
        DoAssert(info.missionFileValid);
      }
      if (!info.missionFileValid) found = true;
      if (!info.paramFileValid) foundParams = true;
    }
  }
  else
  {
    DoAssert(dpnid != BOT_CLIENT);
    player = GetPlayerInfo(dpnid);
    if (!player) return;
    if (player->clientState != NCSPlayerAssigned) return;
    if (!player->missionFileValid) found = true;
    if (!player->paramFileValid) foundParams = true;
  }

  int maxSegmentSize = 512 - 50;

  if (foundParams)
  {
    QIFStreamB f;
    RString src = GetMissionParameters();
    // char '$' is used as an indication of Local Settings directory (different on client than on server)
    RString dst = RString("$\\MPMissionsCache\\__cur_mp.") + GetExtensionWizardMission();
    f.AutoOpen(src);

    TransferFileMessage msg;
    msg._path = dst;
    msg._totSize = f.rest();
    msg._totSegments = (msg._totSize + maxSegmentSize - 1) / maxSegmentSize;
    msg._offset = 0;
    for (int i=0; i<msg._totSegments; i++)
    {
      msg._curSegment = i;
      int size = std::min(maxSegmentSize, msg._totSize - msg._offset);
      msg._data.Resize(size);
      f.read(msg._data.Data(),size);
      if (dpnid == 0)
      {
        for (int j=0; j<_players.Size(); j++)
        {
          NetworkPlayerInfo &info = *_players[j];
          if (info.clientState == NCSPlayerAssigned && !info.paramFileValid)
            SendMsg(info.dpid, &msg, NMFGuaranteed);
        }
      }
      else if (!player->paramFileValid)
        SendMsg(dpnid, &msg, NMFGuaranteed);

      msg._offset += size;
    }
  }

  if (found)
  {
    QIFStreamB f;
    RString src = _missionBank;
    // FIX: transfer mission file always into tmp directory (do not rewrite original file)
    // char '$' is used as an indication of Local Settings directory (different on client than on server)
    RString dst = RString("$\\MPMissionsCache\\") + _missionHeader._fileName + RString(".pbo");
    f.AutoOpen(src);

    TransferMissionFileMessage msg;
    msg._path = dst;
    msg._totSize = f.rest();
    msg._totSegments = (msg._totSize + maxSegmentSize - 1) / maxSegmentSize;
    msg._offset = 0;
    for (int i=0; i<msg._totSegments; i++)
    {
      msg._curSegment = i;
      int size = std::min(maxSegmentSize, msg._totSize - msg._offset);
      msg._data.Resize(size);
      f.read(msg._data.Data(),size);
      if (dpnid == 0)
      {
        for (int j=0; j<_players.Size(); j++)
        {
          NetworkPlayerInfo &info = *_players[j];
          if (info.clientState == NCSPlayerAssigned && !info.missionFileValid)
            SendMsg(info.dpid, &msg, NMFGuaranteed);
        }
      }
      else if (!player->missionFileValid)
        SendMsg(dpnid, &msg, NMFGuaranteed);

      msg._offset += size;
    }
  }

  // do not send twice
  if (dpnid == 0)
  {
    for (int j=0; j<_players.Size(); j++)
    {
      NetworkPlayerInfo &info = *_players[j];
// RptF("    player %s, has state %s", (const char *)info.name, (const char *)FindEnumName(info.clientState));
      if (info.clientState == NCSPlayerAssigned)
      {
        info.missionFileValid = true;
        info.paramFileValid = true;
        SetClientState(info.dpid, NCSMissionReceived);
      }
    }
  }
  else
  {
    player->missionFileValid = true;
    player->paramFileValid = true;
    SetClientState(dpnid, NCSMissionReceived);
  }

#else

  bool foundParams = false;
  NetworkPlayerInfo *player = NULL;
  if (dpnid == 0)
  {
    for (int i=0; i<_players.Size(); i++)
    {
      NetworkPlayerInfo &info = _players[i];
      if (info.clientState != NCSPlayerAssigned) continue;
      if (!info.paramFileValid) foundParams = true;
    }
  }
  else
  {
    DoAssert(dpnid != BOT_CLIENT);
    player = GetPlayerInfo(dpnid);
    if (!player) return;
    if (player->clientState != NCSPlayerAssigned) return;
    if (!player->paramFileValid) foundParams = true;
  }

  int maxSegmentSize = 512 - 50;

  if (foundParams)
  {
    QIFStreamB f;
    RString src = GetMissionParameters();
    // char '$' is used as an indication of Local Settings directory (different on client than on server)
    RString dst = RString("$\\MPMissionsCache\\__cur_mp.") + GetExtensionWizardMission();
    f.AutoOpen(src);

    TransferFileMessage msg;
    msg._path = dst;
    msg._totSize = f.rest();
    msg._totSegments = (msg._totSize + maxSegmentSize - 1) / maxSegmentSize;
    msg._offset = 0;
    for (int i=0; i<msg._totSegments; i++)
    {
      msg._curSegment = i;
      int size = min(maxSegmentSize, msg._totSize - msg._offset);
      msg._data.Resize(size);
      f.read(msg._data.Data(),size);
      if (dpnid == 0)
      {
        for (int j=0; j<_players.Size(); j++)
        {
          NetworkPlayerInfo &info = _players[j];
          if (info.clientState == NCSPlayerAssigned && !info.paramFileValid)
            SendMsg(info.dpid, &msg, NMFGuaranteed);
        }
      }
      else if (!player->paramFileValid)
        SendMsg(dpnid, &msg, NMFGuaranteed);

      msg._offset += size;
    }
  }

  // do not send twice
  if (dpnid == 0)
  {
    for (int j=0; j<_players.Size(); j++)
    {
      NetworkPlayerInfo &info = _players[j];
      // RptF("    player %s, has state %s", (const char *)info.name, (const char *)FindEnumName(info.clientState));
      if (info.clientState == NCSPlayerAssigned)
      {
        info.missionFileValid = true;
        info.paramFileValid = true;
        SetClientState(info.dpid, NCSMissionReceived);
      }
    }
  }
  else
  {
    player->missionFileValid = true;
    player->paramFileValid = true;
    SetClientState(dpnid, NCSMissionReceived);
  }

#endif // _ENABLE_UNSIGNED_MISSIONS
}

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER

RString GetPidFileName ();
extern bool MyPidFile;

//! Create dedicated server
/*!
\param config name of server configuaration file
*/
bool CreateDedicatedServer(SessionType sessionType, RString config)
{
  if (GWorld->Options())
  {
    GWorld->Options()->DestroyHUD(-1);
  }

  RString GetLiveGamertag();
  RString gamertag = GetLiveGamertag();
  if (gamertag.GetLength()>0)
  {
    GNetworkManager.Init();
  }
  else
  {
    // create a system link dedicated server
    GNetworkManager.Init("", GetServerPort(), false);
  }

#if _GAMES_FOR_WINDOWS
  GWorld->CreateDSOptions();
#endif

  RString password("");
  int maxPlayers = MAX_PLAYERS_DS;

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile serverCfg;
  serverCfg.Parse(config, NULL, NULL, &globals);

  ConstParamEntryPtr entry = serverCfg.FindEntry("password");
  if (entry) password = *entry;

  entry = serverCfg.FindEntry("maxPlayers");
  if (entry) maxPlayers = *entry;
  maxPlayers += 1; // dedicated server occupy single slot

  RString steamServerName("SourceDayZ Community Server");
  entry = serverCfg.FindEntry("hostname");
  if (entry) steamServerName = *entry;

  int steamPort = 8766;
  entry = serverCfg.FindEntry("steamPort");
  if (entry) steamPort = *entry;

  int steamQueryPort = 27016;
  entry = serverCfg.FindEntry("steamQueryPort");
  if (entry) steamQueryPort = *entry;

  int port = GetServerPort();

  int rating = 0;

  GNetworkManager.CreateSession(sessionType, RString(), password, maxPlayers, false, port, true, config, rating);
  if (GNetworkManager.IsServer())
  {
    RString title = APP_NAME " Console version " APP_VERSION_TEXT "." STRINGIZE(BUILD_NO)" : port %d"; 
    ConsoleTitle(title, GNetworkManager.GetServer()->GetSessionPort());
      // writting the pid of the server instance:
    if ( GetPidFileName().GetLength() )
    {
      FILE *pidFp;
      if ( (pidFp = fopen((const char*)GetPidFileName(),"wt")) != NULL )
      {
        fprintf(pidFp,"%d\n",getpid());
        fclose(pidFp);
#ifdef _WIN32
#else
        fchmod(fileno(pidFp),(S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH));
#endif
        MyPidFile = true;
      }
    }
#if _ENABLE_STEAM && defined(_WIN64)
    if (steamPort > 0 && steamPort <= 65535 && steamQueryPort > 0 && steamQueryPort <= 65535)
      SteamOnlineServerStart(port, steamPort, steamQueryPort, cc_cast(steamServerName), maxPlayers - 1, !password.IsEmpty());
    else
      LogF("Steamworks server-list ports are invalid; use steamPort and steamQueryPort in the dedicated server config");
#endif
    return true;
  }

  ConsoleTitle("Server creation failed : %d", port);
  return false;
}

#define ENUM_LOAD(type, prefix, name) \
  entry = _serverCfg.FindEntry(#name); \
  if (entry) _eventHandlers[prefix##name] = *entry;

/*!
\patch 1.24 Date 09/19/2001 by Jirka
- Added: parameters "motdInterval" and "motd" (for message of the day) added to dedicated server config
\patch 1.24 Date 09/21/2001 by Jirka
- Added: parameter "voteThreshold" added to dedicated server config
\patch 1.27 Date 10/16/2001 by Jirka
- Added: parameter "reportingIP" added to dedicated server config
to change / disable IP address of Query & Reporting master server.
 When empty, reporting is disabled.
 If entry missing, master.gamespy.com is used and server is advertised to GameSpy.
\patch 1.82 Date 8/27/2002 by Jirka
- Added: parameter "kickDuplicate" added to dedicated server config
\patch 5120 Date 1/18/2007 by Ondra
- New: parameter persistent in the server config to allow the mission to continue
  even when no players are connected
\patch 5180 Date 11/12/2007 by Jirka
- New: BattlEye protection integration
*/

bool NetworkServer::SetDedicated(RString config)
{
  _dedicated = true;

  _serverCfg.Parse(config, NULL, NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
  _missionIndex = -1;

  _motdInterval = 5;
  ConstParamEntryPtr entry = _serverCfg.FindEntry("motdInterval");
  if (entry) _motdInterval = *entry;

  entry = _serverCfg.FindEntry("motd");
  if (entry)
  {
    for (int i=0; i<(*entry).GetSize(); i++) _motd.Add((*entry)[i]);
  }

#if _USE_BATTL_EYE_SERVER
  entry = _serverCfg.FindEntry("battlEye");
  if (!entry || (bool)*entry) //battlEye is on by default
  {
    if (!_beIntegration.Init())
    {
#if _ENABLE_DEDICATED_SERVER
      if (entry || (_beIntegration._initFailedReason!=BattlEye::BEReasonNotInstalled) )
      {
        ConsoleF("BattlEye initialization failed");
        return false;
      }
#endif
    }
  }
#endif

  _voteThreshold = 0.5;
  entry = _serverCfg.FindEntry("voteThreshold");
  if (entry) _voteThreshold = *entry;

  // empty string is default now
  // to disable reporting, use reportingIP = "noreport";
  entry = _serverCfg.FindEntry("reportingIP");
  if (entry) _reportingIP = *entry;

  entry = _serverCfg.FindEntry("kickDuplicate");
  if (entry) _kickDuplicate = *entry;

  entry = _serverCfg.FindEntry("equalModRequired");
  if (entry) _equalModRequired = *entry;

  entry = _serverCfg.FindEntry("verifySignatures");
  if (entry)
  {
    int verify = *entry;
    if (verify>0) SetVerifySignatures(verify);
  }

  // TODO: once stabilized, make _updatesGuaranteed the only option
  entry = _serverCfg.FindEntry("guaranteedUpdates");
  if (entry)
  {
    _updatesGuaranteed = *entry;
  }


  _buildNoRequired = _serverCfg.ReadValue<int>("requiredBuild",0);
  // server can never require version higher than it has, as the bot client should meet the requirements as well
  if (BUILD_NO>0)
  {
    if (_buildNoRequired>BUILD_NO) _buildNoRequired = BUILD_NO;
  }

  _secureIdRequired = _serverCfg.ReadValue<int>("requiredSecureId",DefaultSecureIdRequired);

  entry = _serverCfg.FindEntry("persistent");
  if (entry)
  {
    _persistent = *entry;
  }

  entry = _serverCfg.FindEntry("additionalSignedFiles");
  if (entry)
  {
    int n = entry->GetSize();
    _additionalSignedFiles.Realloc(n);
    _additionalSignedFiles.Resize(n);
    for (int i=0; i<n; i++)
    {
      _additionalSignedFiles[i] = (*entry)[i];
    }
  }

  _maxPlayersLimit = MAX_PLAYERS_DS;
  entry = _serverCfg.FindEntry("maxPlayers");
  if (entry) _maxPlayersLimit = *entry;


  entry = _serverCfg.FindEntry("timeStampFormat");
  if (entry && entry->IsTextValue())
  {
    RString timeStampFormat = *entry;
    if (!timeStampFormat.IsEmpty())
    {
      if (stricmp(timeStampFormat, "none")==0) GTimeStampFormat = TSFNone;
      else if (stricmp(timeStampFormat, "short")==0) GTimeStampFormat = TSFShort;
      else if (stricmp(timeStampFormat, "full")==0) GTimeStampFormat = TSFFull;
    }
  }

  entry = _serverCfg.FindEntry("disable3rdPerson");
  if(entry)
  {
    _disable3rdPerson = *entry;
  }

	entry = _serverCfg.FindEntry("environmentTimeMul");
	if(entry)
	{
		_environmentTimeMul = *entry;
	}
	
  // event handlers
  SERVER_EVENT_ENUM(ServerEvent, SE, ENUM_LOAD)

  return true;
}

void NetworkServer::Monitor(float interval)
{
  _monitorInterval = interval;
  _monitorFrames = 0;
  _monitorIn = 0;
  _monitorOut = 0;
  if (_monitorInterval > 0) _monitorNext = GlobalTickCount() + toInt(1000.0 * _monitorInterval);
  else _monitorInterval = UINT_MAX,_monitorNext = UINT_MAX;
}

void NetworkServer::OnMonitorIn(int size)
{
  if (_dedicated) _monitorIn += size;
}

#endif
//}

static bool LoadKey(const FileItem &file, AutoArray<DSKey> &keys)
{
  int index = keys.Add();
  keys[index].Load(file.path + file.filename);
  // test duplicity
  for (int i=0; i<index; i++)
  {
    if (keys[index]==keys[i])
    { // remove duplicity
      keys.Delete(index);
      break;
    }
  }
  return false;
}

void NetworkServer::SetVerifySignatures(int reqVersion)
{
  // Do not allow OLD sigVer=1 signatures in the NEW products
  saturateMax(reqVersion, 2); //at least 2
  _verifySignatures = true;
  _sigVerRequired = reqVersion;
  if (_sigVerRequired>MaxVerifySignatures) _sigVerRequired = MaxVerifySignatures; // 2 is the max value currently supported

  // load all accepted keys
  // core keys
  ForMaskedFile("Keys\\", "*.bikey", LoadKey, _acceptedKeys);
#if _ENABLE_DEDICATED_SERVER
  if (!_dedicated)
#endif
  {
    // user keys
    RString GetUserDirectory();
    ForMaskedFile(GetUserDirectory() + RString("Keys\\"), "*.bikey", LoadKey, _acceptedKeys);
  }
  ForMaskedFile(RString("Expansion\\Keys\\"), "*.bikey", LoadKey, _acceptedKeys);    
  _acceptedKeys.Compact();
}

/*!
\patch 5164 Date 10/3/2007 by Jirka
- Fixed: Signature verification timeout significantly relaxed.
*/

//! verify signature of the single file
void NetworkServer::CheckSignature(int dpnid, int fileIndex, int level)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpnid);
  if (!info) return;

  // check if the same question is not pending
  if (info->signatureChecks.Pending(fileIndex))
    return;

  // send the question
  DataSignatureAskMessage ask;
  ask._index = fileIndex;
  ask._level = level;
  SendMsg(dpnid, &ask, NMFGuaranteed);

  // add the question to the list of pending questions
  info->signatureChecks.Add(fileIndex);
}

//! verify signature of the single file
void NetworkServer::CheckSignature(int dpnid, RString filename, RString prefix, int level)
{
  NetworkPlayerInfo *info = GetPlayerInfo(dpnid);
  if (!info) return;

  // check if the same question is not pending
  if ( info->signatureChecks.Pending(filename) )
    return;

  // send the question
  FileSignatureAskMessage ask;
  ask._file = filename;
  ask._level = level;
  SendMsg(dpnid, &ask, NMFGuaranteed);

  // add the question to the list of pending questions
  info->signatureChecks.Add(filename, prefix);
}

bool NetworkServer::MyHash(Temp<char> &hash, RString prefix, int sigVer, int level)
{
  // find the stored hash using prefix as key in bankHashes array
  int i=0;
  for (; i<GBankHashes.Size(); i++)
  {
    if (GBankHashes[i].prefix==prefix) break;
  }
  // when hash found, return it
  if (i<GBankHashes.Size())
  {
    BankHashes &hashes = GBankHashes[i];
    switch (sigVer)
    {
    case 1: 
      hash = hashes.ver1Hash;
      return true; 
      //break;
    case 2:
      {
        switch (level)
        {
        case 0:
        case 1:
          {
            hash = hashes.ver2lev0Hash;
            return true;
          }
          //break; //unreachable code
        case 2:
          {
            hash = hashes.ver2lev2Hash;
            return true;
          }
          //break; //unreachable code
        }
      }
    }
  }
  return false; //fail
}


//((EXE_CRC_CHECK
#if !_SUPER_RELEASE

void CompareExeInvestigation::Init(int dpid1, int dpid2)
{
  _stack.Resize(0);
  for (int addr=0x00401000; addr<0x00c01000; addr+=0x1000)
  {
    ExeSegmentInfo &info = _stack.Append();
    info.offset = addr;
    info.size = 0x1000;
  }
  _modified.Resize(0);
  _asked = false;
  _client1._dpid = dpid1;
  _client1._answered = false;
  _client2._dpid = dpid2;
  _client2._answered = false;
}

void CompareExeInvestigation::OnAnswered(int dpid, int answer)
{
  static const int minBlockSize = 1;

  // store the answer
  bool done = false;
  if (dpid == _client1._dpid)
  {
    _client1._answer = answer;
    _client1._answered = true;
    done = _client2._answered;
  }
  else if (dpid == _client2._dpid)
  {
    _client2._answer = answer;
    _client2._answered = true;
    done = _client1._answered;
  }
  if (!done) return;

  // answer from both clients arrived
  if (_client1._answer != _client2._answer)
  {
    ExeSegmentInfo &info = _stack[0];
    if (info.size <= minBlockSize)
    {
      // add to the list of modifications
      int index = -1;
      for (int i=0; i<_modified.Size(); i++)
      {
        if (_modified[i].offset >= info.offset)
        {
          index = i;
          if (info.offset + info.size == _modified[i].offset)
          {
            // join with the next block
            _modified[index].offset = info.offset;
            _modified[index].size += info.size;
          }
          else
          {
            // insert the new block
            _modified.Insert(index, info);
          }
        }
      }
      if (index < 0)
      {
        // append it
        index = _modified.Add(info);
      }
      // check if the new block can be merged with the previous one
      if (index > 0)
      {
        ExeSegmentInfo &block1 = _modified[index - 1];
        ExeSegmentInfo &block2 = _modified[index];
        if (block1.offset + block1.size == block2.offset)
        {
          block1.size += block2.size;
          _modified.Delete(index);
        }
      }
    }
    else
    {
      // store further investigations
      ExeSegmentInfo &add1 = _stack.Append();
      ExeSegmentInfo &add2 = _stack.Append();
      add1.offset = info.offset;
      add1.size = info.size / 2;
      add2.offset = info.offset + add1.size;
      add2.size = info.size - add1.size;
    }
  }

  // remove the handled question
  _stack.Delete(0);
  _asked = false;
  _client1._answered = false;
  _client2._answered = false;

  if (_stack.Size() == 0)
  {
    // investigation finished, output the results
    QOFStream out;
    out.openForAppend("cheats.txt");
    for (int i=0; i<_modified.Size(); i++)
    {
      RString line = Format("  0x%08x:0x%08x\r\n", _modified[i].offset, _modified[i].size);
      out << line;
    }
    out.close();
  }
}

#endif
//))EXE_CRC_CHECK

bool NetworkServer::GetClientAddress(int client, sockaddr_in &address)
{
  if (client == BOT_CLIENT)
    return _server->GetServerAddress(address);
  else
    return _server->GetClientAddress(client, address);
}

//////////////////////////////////////////////////////////////////////////
//
// Server scripting functions and event handlers processing
//

/*!
\patch 5126 Date 2/1/2007 by Jirka
- Added: MP - new server scripting function lock 
- Added: MP - new commands #lock, #unlock
*/

#if _ENABLE_DEDICATED_SERVER

DEFINE_ENUM(ServerEvent, SE, SERVER_EVENT_ENUM)

void NetworkServer::SetEventHandler(RString name, RString value)
{
  ServerEvent event = GetEnumValue<ServerEvent>(cc_cast(name));
  if (event != INT_MIN) _eventHandlers[event] = value;
}

bool NetworkServer::OnEvent(ServerEvent event, float par)
{
  RString handler = _eventHandlers[event];
  if (handler.GetLength() == 0) return false;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(par);

  GameVarSpace local(false);
  int gshandle = _evaluator.BeginContext(&local);
  _evaluator.VarSetLocal("_this", arguments, true);
  _evaluator.Execute(handler, GameState::EvalContext::_default, &_globals); // server namespace
  _evaluator.EndContext(gshandle);

  return true;
}

bool NetworkServer::OnEvent(ServerEvent event, float par1, float par2)
{
  RString handler = _eventHandlers[event];
  if (handler.GetLength() == 0) return false;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(par1);
  arguments.Add(par2);

  GameVarSpace local(false);
  int gshandle = _evaluator.BeginContext(&local);
  _evaluator.VarSetLocal("_this", arguments, true);
  _evaluator.Execute(handler, GameState::EvalContext::_default, &_globals); // server namespace
  _evaluator.EndContext(gshandle);

  return true;
}

bool NetworkServer::OnEvent(ServerEvent event, float par1, RString par2)
{
  RString handler = _eventHandlers[event];
  if (handler.GetLength() == 0) return false;

  GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
  GameArrayType &arguments = value;
  arguments.Add(par1);
  arguments.Add(par2);

  GameVarSpace local(false);
  int gshandle = _evaluator.BeginContext(&local);
  _evaluator.VarSetLocal("_this", arguments, true);
  _evaluator.Execute(handler, GameState::EvalContext::_default, &_globals); // server namespace
  _evaluator.EndContext(gshandle);

  return true;
}

#if _VBS3
// forward declaration of checksize and checktype in gamestate.cpp
bool CheckSize(const GameState *state, GameArrayType array, int size);
#else
inline bool CheckSize(const GameState *state, GameArrayType array, int size)
{
  if (array.Size() == size) return true;
  state->SetError(EvalDim, array.Size(), size);
  return false;
}
#endif

#if _VBS3
// forward declaration of checksize and checktype in gamestate.cpp
bool CheckType(const GameState *state, GameValuePar oper, const GameType &type);
#else
inline bool CheckType(const GameState *state, GameValuePar oper, const GameType &type)
{
  if (oper.GetType() == type) return true;
  state->TypeError(type, oper.GetType());
  return false;
}
#endif

GameValue ServerUsers(const GameState *state)
{
  GameValue value = state->CreateGameValue(GameArray);

  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return value;
  const AutoArray<PlayerIdentity> *identities = server->GetIdentities();
  if (!identities) return value;

  int n = identities->Size();
  GameArrayType &array = value;
  array.Realloc(n);
  array.Resize(n);
  for (int i=0; i<n; i++)
  {
    const PlayerIdentity &identity = (*identities)[i];
    array[i] = state->CreateGameValue(GameArray);
    GameArrayType &subarray = array[i];
    subarray.Realloc(2);
    subarray.Resize(2);
    subarray[0] = (float)identity.playerid;
    subarray[1] = identity.GetName();
  }

  return value;
}

GameValue ServerBan(const GameState *state, GameValuePar oper1)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return NOTHING;

  int playerId = toInt((float)oper1);
  const PlayerIdentity *identity = server->FindIdentityByPlayerId(playerId);
  if (!identity) return NOTHING;

  // add global ban list
  FindArray<RString> banList;
  LoadBanList("ban.txt", banList);
  GFileServerFunctions->FlushReadHandle("ban.txt");
  banList.AddUnique(identity->id);
  SaveBanList("ban.txt", banList);

  // kick off
  server->KickOff(identity->dpnid, KORBan);

  return NOTHING;
}

GameValue ServerKick(const GameState *state, GameValuePar oper1)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return NOTHING;

  int playerId = toInt((float)oper1);
  const PlayerIdentity *identity = server->FindIdentityByPlayerId(playerId);
  if (!identity) return NOTHING;

  server->KickOff(identity->dpnid, KORKick);

  return NOTHING;
}

GameValue ServerKickUID(const GameState *state, GameValuePar oper1)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return NOTHING;

  if (oper1.GetType()==GameString)
  {
    RString id = oper1;
    const AutoArray<PlayerIdentity> *identities = server->GetIdentities();
    const PlayerIdentity *identity = NULL;
    for (int i=0; i<(*identities).Size(); i++)
    {
      if ( IsUserIdMatch((*identities)[i].id, id) )
      {
        identity = &(*identities)[i];
        break;
      }
    }
    if (!identity) return NOTHING;

    server->KickOff(identity->dpnid, KORKick);
  }
  return NOTHING;
}

GameValue ServerBanUID(const GameState *state, GameValuePar oper1)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return NOTHING;

  if (oper1.GetType()==GameString)
  {
    RString id = oper1;

    // add global ban list
    FindArray<RString> banList;
    LoadBanList("ban.txt", banList);
    GFileServerFunctions->FlushReadHandle("ban.txt");
    banList.AddUnique(id);
    SaveBanList("ban.txt", banList);

    // kick off
    return ServerKickUID(state, oper1);
  }
  return NOTHING;
}

GameValue ServerNFiles(const GameState *state, GameValuePar oper1)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return 0.0f;

  int playerId = toInt((float)oper1);
  const PlayerIdentity *identity = server->FindIdentityByPlayerId(playerId);
  if (!identity) return 0.0f;

  return (float)identity->filesCount;
}

GameValue ServerLock(const GameState *state, GameValuePar oper1)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (server) server->LockSession(oper1);
  return NOTHING;
}

//((EXE_CRC_CHECK
#if !_SUPER_RELEASE
GameValue ServerCmpExe(const GameState *state)
{
  NetworkServer *server = GNetworkManager.GetServer();
  if (!server) return false;

  // check if exactly two identities are present
  int dpid1 = 0, dpid2 = 0, dpidCount = 0;
  const AutoArray<PlayerIdentity> *identities = server->GetIdentities();
  if (!identities) return false;
  for (int i=0; i<identities->Size(); i++)
  {
    const PlayerIdentity &identity = identities->Get(i);
    if (identity.dpnid == BOT_CLIENT) continue;
    dpidCount++;
    if (dpidCount == 1)
      dpid1 = identity.dpnid;
    else if (dpidCount == 2)
      dpid2 = identity.dpnid;
    else
      break;
  }
  if (dpidCount != 2) return false;

  // start the exe investigation
  server->CompareExe(dpid1, dpid2);
  return true;
}
#endif
//))EXE_CRC_CHECK

#define NULARS_SERVER(XX, Category) \
  XX(GameArray, "users", ServerUsers, "The list of the users connected to the server.", "", "", "2.92", "", Category) \

//((EXE_CRC_CHECK
#define NULARS_SERVER_DEBUG(XX, Category) \
  XX(GameBool, "cmpExe", ServerCmpExe, "Compare executable of two connected clients.", "", "", "5170", "", Category) \
//))EXE_CRC_CHECK

#define FUNCTIONS_SERVER(XX, Category) \
  XX(GameNothing, "ban", ServerBan, GameScalar, "user id", "Add the user to the server ban list.", "", "", "2.92", "", Category) \
  XX(GameNothing, "kick", ServerKick, GameScalar, "user id", "Kick the user from the server.", "", "", "2.92", "", Category) \
  XX(GameScalar, "numberOfFiles", ServerNFiles, GameScalar, "user id", "Return the number of files used by the user.", "", "", "2.92", "", Category) \
  XX(GameNothing, "lock", ServerLock, GameBool, "lock", "Lock / unlock the session.", "", "", "5126", "", Category) \

#define OPERATORS_SERVER(XX, Category) \

NULARS_SERVER(JNI_NULAR, "Server")
#if !_SUPER_RELEASE
  NULARS_SERVER_DEBUG(JNI_NULAR, "Server Debug")
#endif

static const GameNular ServerNular[]=
{
  NULARS_SERVER(REGISTER_NULAR, "Server")
//((EXE_CRC_CHECK
#if !_SUPER_RELEASE
  NULARS_SERVER_DEBUG(REGISTER_NULAR, "Server Debug")
#endif
//))EXE_CRC_CHECK
};

FUNCTIONS_SERVER(JNI_FUNCTION, "Server")

static const GameFunction ServerUnary[]=
{
  FUNCTIONS_SERVER(REGISTER_FUNCTION, "Server")
};

OPERATORS_SERVER(JNI_OPERATOR, "Server")

#if 0 //
static const GameOperator ServerBinary[]=
{
  OPERATORS_SERVER(REGISTER_OPERATOR, "Server")
};
#endif

void NetworkServer::InitEvaluator()
{
  // global namespace need to be passed explicitly
  _evaluator.DisableDefaultGlobals();

  // no special types

  // functions
  for(int i=0; i<lenof(ServerNular); i++)
    _evaluator.NewNularOp(ServerNular[i]);
  for(int i=0; i<lenof(ServerUnary); i++)
    _evaluator.NewFunction(ServerUnary[i]);
  //for(int i=0; i<lenof(ServerBinary); i++)
  //  _evaluator.NewOperator(ServerBinary[i]);
}

void NetworkServer::ProfileAnswer(const RString &output)
{
  NetworkCommandMessage msg;
  msg._type = NCMTProfileAnswer;
  msg._content.WriteString(output);
  SendMsg(_gameMaster, &msg, NMFGuaranteed);
}

void NetworkServer::QueueExec(int from, RString expression)
{
  if (expression.GetLength() <= 0) return;
  if (_sqfExecQueue.Size() >= 32 || _sqfExecQueue.Size() != _sqfExecFrom.Size())
  {
    RptF("SDZ_SQF_EXEC: rejected command because the execution queue is full or inconsistent");
    return;
  }
  _sqfExecQueue.Add(expression);
  _sqfExecFrom.Add(from);
}

#if defined(_MSC_VER)
// Keep SEH in a helper without C++ objects that have destructors.
static bool SDZ_TryEvaluateSqf(GameState *state, const char *expression, GameDataNamespace *globals, GameValue *result)
{
  __try
  {
    *result = state->EvaluateMultiple(expression, GameState::EvalContext::_default, globals);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    RptF("SDZ_SQF_EXEC: exception 0x%08X while evaluating script", (unsigned)GetExceptionCode());
    return false;
  }
}
#endif

void NetworkServer::ProcessQueuedExec()
{
  if (_sqfExecBusy || _sqfExecQueue.Size() <= 0) return;
  _sqfExecBusy = true;

  GameState *execState = GWorld ? GWorld->GetGameState() : NULL;
  GameDataNamespace *mission = GWorld ? GWorld->GetMissionNamespace() : NULL;
  if (!execState || !mission)
  {
    _sqfExecQueue.Resize(0);
    _sqfExecFrom.Resize(0);
    _sqfExecBusy = false;
    return;
  }

  while (_sqfExecQueue.Size() > 0)
  {
    RString expression = _sqfExecQueue[0];
    int from = _sqfExecFrom[0];
    _sqfExecQueue.Delete(0);
    _sqfExecFrom.Delete(0);

    GameVarSpace local(false);
    int gshandle = execState->BeginContext(&local);
    GameValue result = NOTHING;
    bool evaluated = false;
#if defined(_MSC_VER)
    evaluated = SDZ_TryEvaluateSqf(execState, expression, mission, &result);
#else
    result = execState->EvaluateMultiple(expression, GameState::EvalContext::_default, mission);
    evaluated = true;
#endif
    RString answer;
    if (evaluated && !result.GetNil()) answer = result.GetText();
    if (from == BOT_CLIENT)
      RptF("SDZ_BE_RCON: SQF evaluation %s", evaluated ? "completed" : "failed (see preceding exception)");
    result = NOTHING;
    execState->EndContext(gshandle);

    if (from != BOT_CLIENT && answer.GetLength() > 0)
    {
      NetworkCommandMessage msg;
      msg._type = NCMTExecResult;
      msg._content.WriteString(answer);
      SendMsg(from, &msg, NMFGuaranteed);
    }
  }
  _sqfExecBusy = false;
}

#endif // _ENABLE_DEDICATED_SERVER

void ServerQueueItem::SetFence(const RefR<NetMessage, RefCountSafe> &resultMsg)
{
  if (poInfo) poInfo->_fence = resultMsg;
}

DWORD NetworkServer::SendMsg
(
  int to, NetworkMessage *msg,
  NetworkMessageType type, NetMsgFlags dwFlags,
  NetworkPlayerObjectInfo *poInfo
)
{
#if _ENABLE_MP
  NetworkMessageFormatBase *format = GetFormat(type);
  if (!format) return 0xffffffff;

  if (IsLocalClient(to))
  {
    msg->size = 0;
    NetworkClient *client = _parent->GetClient();
    client->AddLocalMessage(TO_SERVER, type, msg);
    return 0;
  }
  else if (dwFlags & NMFGuaranteed)
  {
    msg->size = CalculateMessageSize(msg, type);
#if _ENABLE_CHEATS
StatMsgSent(type, msg->size, dwFlags);
#endif
    if (dwFlags&NMFHighPriority)
    {
      SendMsgRemote(to,msg,type,dwFlags|NMFStatsAlreadyDone, poInfo ? &poInfo->_fence : NULL, NULL);
      return 0;
    }
    else
    {
      EnqueueMsg(to, msg, type, poInfo);
      return 0;
    }
  }
  else
  {
    msg->size = CalculateMessageSize(msg, type);
#if _ENABLE_CHEATS
StatMsgSent(type, msg->size, dwFlags);
#endif
    //invalidate fence when message is acknowledged
    RefR<NetMessage, RefCountSafe> fence;
    if (poInfo && poInfo->_fence)
    {
      if ( poInfo->_fence->WasReceived() ) poInfo->_fence = NULL;
      else fence = poInfo->_fence;
    }    
    EnqueueMsgNonGuaranteed(to, msg, type, fence);
    return 1;
  }
#else
    return E_FAIL;
#endif
}

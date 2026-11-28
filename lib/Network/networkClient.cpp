#include "../wpch.hpp"
#include "networkImpl.hpp"
#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
#include <Steamworks/steam_api.h>
#endif
#include "../global.hpp"
//#include "strIncl.hpp"
#include "../stringtableExt.hpp"

#include "../arcadeTemplate.hpp"
#include "../AI/ai.hpp"
#include "../world.hpp"
#include "../unitsOnLand.hpp"
#include "../landscape.hpp"
#include "../UI/chat.hpp"
#include <El/XML/xml.hpp>

#include <Es/Algorithms/qsort.hpp>
#include <Es/Files/fileContainer.hpp>

#include "../allAIVehicles.hpp"
#include <lib/Inventory/inventory.hpp>
#include <lib/Inventory/transmitter.hpp>
#include <lib/Network/voiceXA2.hpp>
#include "../seaGull.hpp"
#include "../detector.hpp"
#include "../shots.hpp"
#include "../Protect/selectProtection.h"
#include "../scene.hpp"

#include <El/Debugging/debugTrap.hpp>

#include <El/QStream/qbStream.hpp>
#include <El/QStream/packFiles.hpp>
#include <El/FileServer/fileServer.hpp>
#include <El/Evaluator/express.hpp>

#include <El/ParamArchive/paramArchiveDb.hpp>
#include "../saveVersion.hpp"
#include "../registration.hpp"

#include <El/Common/randomGen.hpp>
#include "../gameStateExt.hpp"
#include "../integrity.hpp"

#include "../progress.hpp"
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>

#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "../UI/uiActions.hpp"
#include "../UI/missionDirs.hpp"
#include "../fileLocator.hpp"
#include "../gameDirs.hpp"

#include "../AI/aiRadio.hpp"
#include "../entityEventHandler.hpp"

#include <El/CRC/crc.hpp>

//#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
  #include <io.h>
  #include <objbase.h>
#endif

#include <Es/Strings/bString.hpp>
#ifdef _CPPRTTI
  #include <typeinfo>
#endif

#include "Es/Common/langExt.hpp"
#define ENABLED_NOOPT_Bebul 0
#if ENABLED_FEATURE(NOOPT)
#if _ENABLE_CHEATS
#pragma optimize ("", off)
#endif
#endif

#include "El/Speech/debugVoN.hpp"

#if defined(DAYZ_PRIVATE_SERVER) && defined(_WIN32)
RString GetUserDirectory();

// Stable private-server identity; authentication and authorization remain server responsibilities.
static RString GetPrivateServerUID()
{
  RString path = GetUserDirectory() + RString("private-server-id.txt");
  FILE *file = fopen(cc_cast(path), "r");
  if (file)
  {
    char saved[64] = {0};
    size_t count = fread(saved, 1, sizeof(saved)-1, file);
    fclose(file);
    if (count == 36 && saved[8]=='-' && saved[13]=='-' && saved[18]=='-' && saved[23]=='-')
      return RString(saved);
  }

  GUID guid;
  if (CoCreateGuid(&guid) != S_OK) return RString();
  char id[40];
  sprintf(id, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
    (unsigned long)guid.Data1, (unsigned int)guid.Data2, (unsigned int)guid.Data3,
    (unsigned int)guid.Data4[0], (unsigned int)guid.Data4[1],
    (unsigned int)guid.Data4[2], (unsigned int)guid.Data4[3],
    (unsigned int)guid.Data4[4], (unsigned int)guid.Data4[5],
    (unsigned int)guid.Data4[6], (unsigned int)guid.Data4[7]);
  file = fopen(cc_cast(path), "w");
  if (file)
  {
    fwrite(id, 1, 36, file);
    fclose(file);
  }
  return RString(id);
}
#endif

#if _PROFILE
#pragma optimize( "", off )
#endif

#if _ENABLE_GAMESPY && _VERIFY_KEY
#include "../GameSpy/gcdkey/gcdkeyc.h"
# ifdef NOMINMAX
#  undef min
#  undef max
# endif
#endif

#if _GAMES_FOR_WINDOWS
# include "../saveGame.hpp"
# include "../X360/ArmA2.spa.h"
#endif

extern int MaxGroups;

/*!
\patch 1.87 Date 10/17/2002 by Jirka
- Fixed: Too many dead bodies were kept on server in 1.85.
*/

//! Time after that body can disappear (in seconds)
#define OLD_BODY                    10

//! Number of bodies on bot client
#define BODIES_PER_CLIENT           10

//! Time after that wreck can disappear (in seconds)
#define OLD_WRECK                   10

//! Number of wrecks on bot client
#define WRECKS_PER_CLIENT           5

//! enable diagnostic logs of message errors on client
#define LOG_CLIENT_ERRORS       0
//! limit for diagnostic logs of message errors on client
static float LogClientErrorLimit = 50.0f;

#define LOG_LIVE_STATS 1

/*!
\file
Basic implementation file for network client
*/

#define LOG_SEND_PROCESS 0

#if _SUPER_RELEASE
#define LOG_TEAM_SWITCH 0
#else
#define LOG_TEAM_SWITCH 1
#endif

RStringB GetExtensionWizardMission();

void ParseFlashpointCfg(ParamFile &file);

#if _USE_BATTL_EYE_CLIENT

NetworkClient::BattlEye::BattlEye()
{
  CleanUp();
}

NetworkClient::BattlEye::~BattlEye()
{
  Done();
}

bool GetBELaunchPath(char *launchPath);

int NetworkClient::BattlEye::GetBEVersion(HMODULE library) const
{
  if (library)
  {
    GetVerF *getVer = (GetVerF *)GetProcAddress(library, "GetVer");
    if (getVer) return (*getVer)();
  }
  return 0;
}
 
bool NetworkClient::BattlEye::Init(NetworkClient *client)
{
  _initialized = false;
  const char *installPath = "BattlEye\\BEClient.dll";

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
    strcat(launchPath, "BEClient.dll");
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
      RptF(Format("BattlEye client updated to version: %d", instVersion));
      curBEVersion = instVersion;
    }
  }
  else
  {
    // BattlEye removed, remove also from the cache
    strcat(launchPath, "BEClient.dll");
    chmod(launchPath, S_IREAD | S_IWRITE);
    unlink(launchPath);
    return false;
  }

  InitF *init = (InitF *)GetProcAddress(_library, "Init");
  sockaddr_in address;
  client->_client->GetServerAddress(address);

  _gameParams._address = address.sin_addr.s_addr;
  _gameParams._port = address.sin_port;
  _gameParams._printMessage = &PrintMessage;

  const int minBEVerReq = 164;
  if ((curBEVersion<minBEVerReq) || !init || !(*init)(0, &_gameParams, &_callbacks))
  {
    FreeLibrary(_library);
    CleanUp();
    if (curBEVersion<minBEVerReq) RptF("Obsolete/unsupported BattlEye version, please reinstall BE!");
    return false;
  }

  _initialized = true;
  return true;
}

void NetworkClient::BattlEye::Done()
{
  if (_initialized && _callbacks._exit)
  {
    (*_callbacks._exit)(); //call the Exit function before whenever you unload the dll, but only when the previous Init() call succeeded
    _initialized = false;
  }
  if (_library)
  {
    FreeLibrary(_library);
    _library = NULL;
  }
  CleanUp();
}

bool NetworkClient::BattlEye::Run(NetworkClient *client)
{
  if (!_callbacks._run) return true;
  if (!(*_callbacks._run)())
  {
    // auto-update
    if (_initialized && _callbacks._exit) (*_callbacks._exit)();
    FreeLibrary(_library);
    CleanUp();

    char path[MAX_PATH];
    if (!GetBELaunchPath(path)) return false;
    strcat(path, "BEClient.dll");

    chmod(path, S_IREAD | S_IWRITE);
    unlink(path);
    rename(RString(path) + RString(".new"), path);

    if (!Init(client)) return false;
  }
  return true;
}

void NetworkClient::BattlEye::Command(const char *command)
{
  if (_callbacks._command) (*_callbacks._command)(command);
}

template<> RString ImplicitMapTraits<RString>::null;
template<> RString ImplicitMapTraits<RString>::zombie = RString("zombie"); // any string will do, we need a unique instance of RString

bool NetworkClient::BattlEye::OnScriptExec(RString script)
{
  #if _ENABLE_CHEATS
  if (script && *script) 
  #else
  if (_callbacks._onScriptExec && script && *script) 
  #endif
  {
    RString exists;
    intptr_t key = KeyFromString(script);
    if (_checked.get(key,exists))
    {
      return true;
    }
    // TODO: maintain LRU instead of FIFO?

    const int maxFIFO = 200;
    _checkedFIFO.Add(key);
    if (_checkedFIFO.Size()>maxFIFO)
    {
      intptr_t keyToRemove = _checkedFIFO[0];
      _checkedFIFO.Delete(0,1);
      _checked.removeKey(keyToRemove);
    }
    _checked.put(script);
    if (_callbacks._onScriptExec)
    {
      (*_callbacks._onScriptExec)(script);
    }
#if 0
    static int count = 0;
    RptF("BE Script: no.%d", ++count);
    RptF(script);
#endif
    return true;
  }
#if 0
  if (script && *script)
  {
    static int count = 0;
    RptF("BE Script: no.%d", ++count);
    RptF(script);
  }
#endif
  return (_callbacks._onScriptExec==NULL); //when no callback is present, consider it as done
}

void NetworkClient::BattlEye::PrintMessage(const char *message)
{
  bool firstLine = true;
  const char *nl = NULL;
  while (nl = strchr(message, '\n'))
  {
    RString line(message, nl - message);
    if (firstLine)
    {
      GChatList.Add(CCSystem, NULL, RString("BattlEye Client: ") + line, false, true);
      firstLine = false;
    }
    else
    {
      GChatList.Add(CCSystem, NULL, RString("  ") + line, false, true);
    }
    message = nl + 1;
  }
  if (firstLine)
  {
    GChatList.Add(CCSystem, NULL, RString("BattlEye Client: ") + message, false, true);
    firstLine = false;
  }
  else
  {
    GChatList.Add(CCSystem, NULL, RString("  ") + message, false, true);
  }
}

#endif

// Network client itself

/*!
\patch 1.45 Date 3/1/2002 by Jirka
- Fixed: Sometimes client has problems connect to server
\patch 1.89 Date 10/22/2002 by Ondra
- Fixed: Chat sound more quiet.
\patch 5140 Date 3/15/2007 by Bebul
- Fixed: VoN issues with server behind firewall fixed
*/

NetworkClient::NetworkClient(NetworkManager *parent, RString address, RString password)
: NetworkComponent(parent)
{

#if DEBUG_CREATOR_ID
  _debugCreatorIdSet = false;
#endif
  _playerID = 0;
  _creatorId = CreatorId(0);
#if _VBS2 // VBS2 nickname
  _localPlayerName = Glob.header.playerHandle;
#else
  _localPlayerName = Glob.header.GetPlayerName();
#endif

#if _VBS3 // player color
  _sentPlayerColour = false;
#endif

#if _USE_BATTL_EYE_CLIENT
  _beWanted = false;
#endif

#if _ENABLE_GAMESPY
  _publicAddr = 0;
  const char *ptr = address;
  const char *p = strrchr(ptr, '#');
  if (p)
  {
    // NAT Negotiation is needed
    _natNegotiationNeeded = true;
  }
  else _natNegotiationNeeded = false;
#else
  _natNegotiationNeeded = false;
#endif

  GStats.ClearMission();

  Verify(Init(address, password));
  _clientState = NCSCreated;
  _serverState = NSSSelectingMission;
  _serverTimeout = INT_MAX;

  _connectionQuality = CQGood;
  _connectionDesync = CDGood;

  _missionFileValid = false;

  _teamSwitchDisabledUntil = 0;

  _gameMaster = false;
  _admin = false;
  _selectMission = false;
  _voteMission = false;

  _controlsPaused = false;

  //  _rvReceived = 0;
  _soundId = 0; // incremented for each PlaySound

  GetValue(_chatSound, Pars >> "CfgInGameUI" >> "Chat" >> "sound");

  // ensure no content remain
  DeleteDirectoryStructure(GetClientTmpDir(), false);

  _clientInfo = new ClientInfoObject(); // regular client
  _clientInput = new ClientInputLocal();
  _clientCameraPosition = new ClientCameraPositionObject();
  
  _hideBodies = 0;
  _hideWrecks = 0;

  memset(&_serverVoNAddr,  0, sizeof(_serverVoNAddr));

  _totalToCreate = -1;
  _remainingToCreate = -1;
  _totalToUpdate = -1;
  _remainingToUpdate = -1;

  _voiceChannel = CCGlobal;

  _serverTime = _serverTimeOfEstimatedEnd = 0;
}

bool NetworkClient::Init(RString address, RString password)
{
  // load some settings from Flashpoint.cfg
  ParamFile cfg;
  ParseFlashpointCfg(cfg);

  //Direct play has been eliminated from here
  _client = CreateNetClient(cfg);

  if (!_client)
  {
    _connectResult = CRError;
    return false;
  }
  
  _serverPassword = password; 

  _client->SetNetworkParams(cfg);
  
  int port = GetNetworkPort();

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
#if _VBS2 // VBS2 nickname
  RString playerName = Glob.header.playerHandle;
#else
  RString playerName = Glob.header.GetPlayerName();
#endif
  extern bool KeepAppActive();
  _connectResult = _client->Init
  (
    address, password, false, port, playerName, versionInfo, MAGIC_APP, KeepAppActive
  );
  if (_connectResult != CROK)
  {
    _client = NULL;
    return false;
  }

  InitVoice();
  
  char buffer[256];
  sprintf(buffer, "Tmp%d", port);
  ServerTmpDir = buffer;

  return true;
}

NetworkLocalObjectInfo ImplicitMapTraits<NetworkLocalObjectInfo>::zombie = {NetworkId(CreatorId(-1),-1)};
NetworkLocalObjectInfo ImplicitMapTraits<NetworkLocalObjectInfo>::null= {NetworkId(CreatorId(0),0)};

NetworkRemoteObjectInfo ImplicitMapTraits<NetworkRemoteObjectInfo>::zombie = {NetworkId(CreatorId(-1),-1)};
NetworkRemoteObjectInfo ImplicitMapTraits<NetworkRemoteObjectInfo>::null = {NetworkId(CreatorId(0),0)};

NetworkClient::NetworkClient(NetworkManager *parent, NetworkServer *server, int playerRating)
: NetworkComponent(parent)
{

#if DEBUG_CREATOR_ID
  _debugCreatorIdSet = false;
#endif
  _playerID = 0;
  _creatorId = CreatorId(0);
#if _USE_BATTL_EYE_CLIENT
  _beWanted = false;
#endif

#if _ENABLE_GAMESPY
  _publicAddr = 0;
  _natNegotiationNeeded = false;
#endif

  _updatesGuaranteed = server->IsUpdatesGuaranteed();

#if _VBS2 // VBS2 nickname
  _localPlayerName = Glob.header.playerHandle;
#else
  _localPlayerName = Glob.header.GetPlayerName();
#endif

  GStats.ClearMission();

  Verify(Init(server));
  _clientState = NCSCreated;
  _serverState = NSSSelectingMission;
  _serverTimeout = INT_MAX;

  _connectionQuality = CQGood;
  _connectionDesync = CDGood;

  _missionFileValid = false;

  _teamSwitchDisabledUntil = 0;

  _gameMaster = false;
  _admin = false;
  _selectMission = false;
  _voteMission = false;

  _controlsPaused = false;

  //  _rvReceived = 0;
  _soundId = 0; // incremented for each PlaySound

  GetValue(_chatSound, Pars >> "CfgInGameUI" >> "Chat" >> "sound");

  // ensure no content remain
  DeleteDirectoryStructure(GetClientTmpDir(), false);

  _clientInfo = new ClientInfoObject();
  _clientInput = new ClientInputLocal();
  _clientCameraPosition = new ClientCameraPositionObject();
  
  _hideBodies = 0;
  _hideWrecks = 0;

  memset(&_serverVoNAddr,  0, sizeof(_serverVoNAddr));

  _totalToCreate = -1;
  _remainingToCreate = -1;
  _totalToUpdate = -1;
  _remainingToUpdate = -1;

  _voiceChannel = CCGlobal;

  _serverTime = _serverTimeOfEstimatedEnd = 0;
}

// Init with NetworkServer parameter is called to create BOT_CLIENT
bool NetworkClient::Init(NetworkServer *server)
{
  // load some settings from Flashpoint.cfg
  ParamFile cfg;
  ParseFlashpointCfg(cfg);

  _client = CreateNetClient(cfg);

  if (!_client)
  {
    _connectResult = CRError;
    return false;
  }
  
  _client->SetNetworkParams(cfg);
  
  int port = GetNetworkPort();

  // connect bot client directly
  _playerDpid = BOT_CLIENT;
  _connectResult = CROK;
  
#if _ENABLE_DEDICATED_SERVER
  //if (!IsDedicatedServer())  
#endif
    InitVoice(true);
  
  char buffer[256];
  sprintf(buffer, "Tmp%d", port);
  ServerTmpDir = buffer;

  return true;
}

void NetworkClient::DisableVoice(bool disable)
{
  #if _GAMES_FOR_WINDOWS
  _client->VoiceCapture(!disable);
  #endif
}

void NetworkClient::DebugServerExec(RString str)
{
#if _ENABLE_CHEATS
  NetworkCommandMessage msg;
  msg._type = NCMTGameExec;
  msg._content.WriteString(str);
  SendMsg(&msg, NMFGuaranteed);
#endif
}

#if _ENABLE_GAMESPY

#include "../GameSpy/natneg/natneg.h"
# ifdef NOMINMAX
#  undef min
#  undef max
# endif

static bool CheckRawVoiceMessage(char *data, int len, struct sockaddr_in *from)
{
  if (len >= NATNEG_MAGIC_LEN && memcmp(data, NNMagicData, NATNEG_MAGIC_LEN) == 0)
  {
#if !_SUPER_RELEASE
    int port = from->sin_port;
    LogF("*** NAT Message arrived from %s:%d, length %d bytes", inet_ntoa(from->sin_addr), ntohs(port), len);
#endif
    // NAT Negotiation message - pass to GameSpy SDK
    enterNN();
    NNProcessData(data, len, from);
    leaveNN();
    return true; // processed
  }
  return false;
}
#else
static bool CheckRawVoiceMessage(char *data, int len, struct sockaddr_in *from)
{
  return false;
}
#endif

void NetworkClient::InitVoice(bool isBotClient)
{
  ConstParamEntryPtr entry = (Pars >> "CfgVoiceMask").FindEntry(Glob.header.voiceMask);
  if (entry)
  {
    VoiceMask mask;
    mask.specEnergyWeight = (*entry) >> "specEnergyWeight";
    mask.pitchScale = (*entry) >> "pitchScale";
    mask.whisperValue = (*entry) >> "whisperValue";
    mask.roboticValue = (*entry) >> "roboticValue";
    _client->InitVoice(&mask, &CheckRawVoiceMessage, isBotClient);
  }
  else _client->InitVoice(NULL, &CheckRawVoiceMessage, isBotClient);
#if _GAMES_FOR_WINDOWS
  _client->VoiceCapture(true);
#endif
}

static void TargetFromIdentity(TransmitTarget &tt, const PlayerIdentity &id, int extInfo)
{
  // add to transmit targets
  tt.addr = id._voiceAddress;
  tt.dpid = id.dpnid;

#if _GAMES_FOR_WINDOWS
  tt.xuid = identity->xuid;
#endif
  tt.extInfo = extInfo;
}


/*!
\patch 5108 Date 12/20/2006 by Bebul
- Fixed: Player hosting game was not able to hear voice over net
*/
void NetworkClient::ConnectVoice(int player, sockaddr_in &addr)
{
  // no else here - BOT_CLIENT may be both server and client
  PlayerIdentity *identity = FindIdentity(player);
  if (identity) identity->_voiceAddress = addr;
  if (player==BOT_CLIENT)
  {
    _serverVoNAddr = addr;
  }
#if _ENABLE_MP //binarize was not linkable
  VoiceStartKeepAlive(true, player); //keepAlive is constructed as inactive by default
#endif
}

void NetworkClient::ChangeVoiceMask(RString mask)
{
  if (!_client) return;
  ConstParamEntryPtr entry = (Pars >> "CfgVoiceMask").FindEntry(mask);
  if (entry)
  {
    VoiceMask mask;
    mask.specEnergyWeight = (*entry) >> "specEnergyWeight";
    mask.pitchScale = (*entry) >> "pitchScale";
    mask.whisperValue = (*entry) >> "whisperValue";
    mask.roboticValue = (*entry) >> "roboticValue";
    _client->ChangeVoiceMask(&mask);
  }
}

NetworkClient::~NetworkClient()
{
  RemoveSystemMessages();

  Done();
  DeleteDirectoryStructure(GetClientTmpDir(), true);

#if _ENABLE_CHEATS
  if (_debugWindow) _debugWindow->Close();
#endif
}

void NetworkClient::Done()
{
  // _dp->CancelAsyncOperation(NULL, DPNCANCEL_ALL_OPERATIONS);
  RemoveUserMessages();

  _radioSubscribers.Clear();


  _client = NULL;

  GDebugVoNBank.Reset(true); // all VoN/Net diagnostics should be reset
}

void NetworkClient::CreateDebugWindow()
{
#if _ENABLE_CHEATS
  _debugWindow = new DebugListWindow("Client debug");
#endif
}

int CmpBodies
(
  const BodyInfo *info1,
  const BodyInfo *info2
)
{
  float diff = info1->value - info2->value;
  return sign(diff);
}

/*!
\patch 1.56 Date 5/10/2002 by Jirka
- Improved: New algorithm for removing bodies in MP game
\patch 1.85 Date 9/23/2002 by Jirka
- Fixed: Algorithm for removing bodies in MP game improved
*/

void NetworkClient::DisposeBody(Person *body)
{
  DoAssert(body->IsLocal());

  // Add body to queue
  int index = _bodies.Add();
  BodyInfo &bodyInfo = _bodies[index];
  bodyInfo.body = body;
  bodyInfo.hideTime = Glob.time + OLD_BODY;
  bodyInfo.value = 0;

  // Recalculate body values
  for (int i=0; i<_bodies.Size(); i++)
  {
    BodyInfo &bodyInfo = _bodies[i];
    if (!bodyInfo.body)
    {
      _bodies.Delete(i);
      i--;
      continue;
    }
    bodyInfo.value = 0;
    TurretContext context;
    DoVerify(bodyInfo.body->GetPrimaryGunnerTurret(context));
    context._weapons->ForEachWeapon([&bodyInfo](Weapon *weapon) -> bool
    {
      const WeaponType *type = weapon->Type();
      if (type) bodyInfo.value += type->_value;
      return false; // continue
    });
    context._weapons->ForEachMagazine([&bodyInfo](Magazine *magazine) -> bool
    {
      const MagazineType *type = magazine->Type();
      if (type) bodyInfo.value += type->_value;
      return false; // continue
    });
  }

  // Sort values
  QSort(_bodies.Data(), _bodies.Size(), CmpBodies);

  // Remove old and unvaluable bodies
  for (int i=0; i<_bodies.Size(); i++)
  {
    if (_hideBodies <= 0) break;
    
    BodyInfo &bodyInfo = _bodies[i];
    if (bodyInfo.value >= 1000) break;
    if (bodyInfo.hideTime > Glob.time) continue;

    DoAssert(bodyInfo.body->IsLocal());
    bodyInfo.body->HideBody();
    _bodies.Delete(i);
    _hideBodies--;
    i--;
  }
}

void NetworkClient::DisposeWreck(Transport *wreck)
{
  DoAssert(wreck->IsLocal());

  // Add wreck to queue
  int index = _wrecks.Add();
  WreckInfo &wreckInfo = _wrecks[index];
  wreckInfo.wreck = wreck;
  wreckInfo.hideTime = Glob.time + OLD_WRECK;

  // Remove old wrecks
  for (int i=0; i<_wrecks.Size(); i++)
  {
    if (_hideWrecks <= 0) break;

    WreckInfo &wreckInfo = _wrecks[i];
    if (wreckInfo.hideTime > Glob.time) continue;

    DoAssert(wreckInfo.wreck->IsLocal());
    wreckInfo.wreck->SetDelete();
    _wrecks.Delete(i);
    _hideWrecks--;
    i--;
  }
}

/*!
\patch 1.47 Date 3/8/2002 by Ondra
- Improved: Chat message priority increased when mission is not running.
This should help when chatting during mission transfers.
*/

NetMsgFlags NetworkClient::GetChatPriority() const
{
  if (GetServerState() == NSSPlaying) return NMFNone;
  return NMFHighPriority;
}

void NetworkClient::SetParams(float param1, float param2, bool updateOnly)
{
  _param1 = param1;
  _param2 = param2;
  
  MissionParamsMessage msg;
  msg._param1 = param1;
  msg._param2 = param2;
  msg._paramsArray = _paramsArray;
  msg._updateOnly = updateOnly;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SetParams(float param1, float param2, AutoArray<float> paramsArray, bool updateOnly)
{
  _paramsArray = paramsArray;
  _param1 = param1;
  _param2 = param2;

  MissionParamsMessage msg;
  msg._param1 = param1;
  msg._param2 = param2;
  msg._paramsArray = paramsArray;
  msg._updateOnly = updateOnly;
  SendMsg(&msg, NMFGuaranteed);
}

static void MergeTargets(AutoArray<TransmitTarget, MemAllocSA> &targets, AutoArray<TransmitTarget, MemAllocSA> &addTargets)
{
  // when merging all targets with direct speech targets, target should be always found
  // which means: if 'all' is sorted at the input, it remains sorted also after merging
  int tgSize = targets.Size();
  for (int i=0, size=addTargets.Size(); i<size; i++)
  {
    int dpid = addTargets[i].dpid;
    int j=0;
    for (; j<tgSize; j++)
      if (targets[j].dpid==dpid) break;
    if (j==tgSize) targets.Add(addTargets[i]);
    else targets[j].extInfo |= addTargets[i].extInfo;
  }
}

void NetworkClient::GetTargetsInDirectSpeakRange(AutoArray<TransmitTarget, MemAllocSA> &targets, int channel) const
{
  Person *player = GWorld->GetRealPlayer();
  if (!player) return;
  AutoArray<int> units;
#if _ENABLE_MP //making binarize project linkable
  float maxDist = ((channel==CCDirect) ? 80.0f : 50.0f);
  Vector3Val pos = player->WorldPosition(player->FutureVisualState());
  WhatUnitsVoNDirect(units, pos, maxDist);
#endif

  for (int i=0; i<units.Size(); i++)
  {
    int dpnid = units[i];
    const PlayerIdentity *identity = FindIdentity(dpnid);
    if (identity)
    {
      // check remote mute list
      if (IsPlayerInRemoteMuteList(identity->dpnid)) continue;
      TransmitTarget &tt = targets.Append();
      TargetFromIdentity(tt,*identity,TTChan3D);
    }
  }
}

class PlayersInVehicleFunc : public ICrewFunc
{
protected:
  Person *_player;
  AutoArray<int, MemAllocSA> &_players;

public:
  PlayersInVehicleFunc(AutoArray<int, MemAllocSA> &players, Person *player) : _players(players), _player(player) {}

  virtual bool operator () (Person *person)
  {
    if (person && person != _player && person->Brain() && person->Brain()->GetLifeState() != LifeStateDead)
    {
      int id = person->GetRemotePlayer();
      if (id != AI_PLAYER) _players.Add(id);
    }
    return false; // continue
  }
};

void NetworkClient::WhatUnitsOnTransmitterChannel(RefArray<NetworkObject> &units)
{
  // get the tuned frequency from active ItemTransmitter
  float freq = GetVoNTransmitterFreq();
  const AutoArray<RadioSubscriber> &_rs = _radioSubscribers._subscribers;
  AutoArray<int> unitsDpnid;
  for (int i=0; i<_rs.Size(); i++)
  {
    const RadioSubscriber &sb = _rs[i];
    for (int j=0; j<sb.frequencies.Size(); j++)
    {
      if ( sb.FrequenciesMatch(sb.frequencies[j],freq) )
      {
        unitsDpnid.Add(sb.dpnid);
        break;
      }
    }
  }
  // convert unitsDpnid to units
  // Get All Players
  typedef OLink(Person) helperType;
  AUTO_STATIC_ARRAY(helperType, players, 128);
  GWorld->ScanPlayers(players);

  for (int i=0; i<unitsDpnid.Size(); i++)
  {
    for (int j=0; j<players.Size(); j++)
    {
      if (players[j]->GetRemotePlayer()==unitsDpnid[i])
      {
        Person *person = players[j];
        units.Add(person);
        break;
      }
    }
  }
}

void NetworkClient::GetPlayersOnChannel(AutoArray<int, MemAllocSA> &players, int channel) const
{
  players.Resize(0);
  switch (channel)
  {
  case CCGlobal:
    // optimization
    Fail("Do not call for global channel");
    break;
  case CCNone:
  case CCDirect:
    break;
  case CCVehicle:
    {
      Person *player = GWorld->GetRealPlayer();
      if (!player) break;
      AIBrain *agent = player->Brain();
      if (!agent) break;
      Transport *vehicle = agent->GetVehicleIn();
      if (!vehicle) break;

      PlayersInVehicleFunc func(players, player);
      vehicle->ForEachCrew(func);
    }
    break;
  case CCItemTransmitter:
    // get the tuned frequency from active ItemTransmitter
    float freq = GetVoNTransmitterFreq();
    // clients must subscribe its nearby active radios frequencies
    const AutoArray<RadioSubscriber> &_rs = _radioSubscribers._subscribers;
    for (int i=0; i<_rs.Size(); i++)
    {
      const RadioSubscriber &sb = _rs[i];
      for (int j=0; j<sb.frequencies.Size(); j++)
      {
        if ( sb.FrequenciesMatch(sb.frequencies[j],freq) )
        {
          players.Add(sb.dpnid);
          break;
        }
      }
    }
    break;
  }

  if (channel>=CCCustom1 && channel<=CCCustom10)
  {
    CustomChannelInfo &ccinfo = GCustomChannels.GetCustomChannelInfo(channel);
    for (int j=0; j<ccinfo.units.Size(); j++)
    {
      Person *person = dynamic_cast<Person *>(ccinfo.units[j].GetRef());
      if (!person) continue;
      int id = person->GetRemotePlayer();
      if (id != AI_PLAYER) players.Add(id);
    }
  }
}

DEFINE_NET_MESSAGE(LocalizedChat,LOCALIZED_CHAT_MSG)

bool NetworkClient::IsPlayerInRemoteMuteList(int player) const
{
#if _ENABLE_MP
  for (int i=0; i<_remoteMuteList._list.Size(); i++)
    if (_remoteMuteList._list[i] == player) return true;
#endif
  return false;
}

static int CompareTransmitTargetId(TransmitTarget *t1, TransmitTarget *t2)
{
  return t1->dpid-t2->dpid;
}

void NetworkClient::OnDraw()
{
}

#if DEBUG_VON_DIAGS
  DWORD replayerCurPosition;
  DWORD VoNReplStatus;
#endif

#if HEAR_YOURSELF
bool hearYourself = true; //only for debugging purposes, no need to run two instances of OFP game
#endif

// when port address was changed in VoN layer, change it also in PlayerIdentity
void PeerChangedProc(int dpid, int port, void *context)
{
  AutoArray<PlayerIdentity> &identities = *((AutoArray<PlayerIdentity> *)(context));
  for (int i=0; i<identities.Size(); i++)
  {
    if (identities[i].dpnid == dpid)
    {
      identities[i]._voiceAddress.sin_port = port;
      break;
    }
  }
}

InputInterface *NetworkClient::GetInputInterface()
{
  return _clientInput;
}

/*!
\patch 5164 Date 6/5/2007 by Ondra
- Fixed: Vehicle channel no longer transforms to a side channel when not in the vehicle.
*/
void NetworkClient::OnSimulate()
{
  PROFILE_SCOPE_EX(nwCli, network)

  // raw statistics
#if _ENABLE_CHEATS
  // remove statistics info
  static AutoArray<int> texts;
  for (int i=0; i<texts.Size(); i++)
  {
    if (texts[i] >= 0) GEngine->RemoveText(texts[i]);
  }
  texts.Resize(0);

  if (outputDiags == ODMRawStats)
  {
    DoAssert(_rawStatistics.Size() <= 1);
    if (_rawStatistics.Size() > 0)
    {
      char output[1024];
      sprintf
      (
        output, "To server: sent %d (%d), received %d (%d)",
        _rawStatistics[0].sizeSent, _rawStatistics[0].msgSent,
        _rawStatistics[0].sizeReceived, _rawStatistics[0].msgReceived
      );  
      texts.Add(GEngine->ShowTextF(1000, output));

      _rawStatisticsGraph.Resize(1);
      if (!_rawStatisticsGraph[0]) _rawStatisticsGraph[0] = new NetStatsGraph;
      _rawStatisticsGraph[0]->OnSimulate(_rawStatistics[0].sizeReceived,_rawStatistics[0].msgReceived);
      GEngine->ShowDiagGraph(_rawStatisticsGraph[0]);


    }
    else
    {
      texts.Add(GEngine->ShowTextF(1000, "To server: No raw messages"));
    }


  }
#endif

#if DIAGNOSE_SPEEX_ESTFRAMES
  static volatile bool doIt = false;
  if (doIt)
  { // step into this scope in debugger to increase the codec quality
    static volatile int quality = 1;
    GNetworkManager.SetVoNCodecQuality(quality);
    RptF("VoN quality set to %d", quality);
    quality++;
  }
#endif

  // update ClientCameraPosition
  const Camera *camera = GScene->GetCamera();
  if ( camera==NULL || ( GWorld->CameraOn()==GWorld->PlayerOn()  && GWorld->GetCameraEffect()==NULL) )
    _clientCameraPosition->_position = InvalidCamPos;
  else
    _clientCameraPosition->_position = camera->Position();

  Person *player = GWorld->GetRealPlayer();
  AIBrain *agent = player ? player->Brain() : NULL;

  // outside briefing and game speak on direct channel
  ChatChannel ccLobby = CCDirect; //<- hotfix, see VOIP: Players able to talk globally despite in Direct chat - https://jira.bistudio.com/browse/DAYZ-2339
  
  bool forceLobbyChannel = (
    _clientState != NCSPlaying ||
    !agent || agent->GetLifeState() == LifeStateDead
  );
  
  // if we are server owner or admin, we may always use the global channel
  if (_voiceChannel==CCGlobal && (GetNetworkManager().IsGameMaster() || GetNetworkManager().IsServer()))
  {
    forceLobbyChannel = false;
  }
  
  if (_voiceChannel != ccLobby && _voiceChannel!=CCNone && forceLobbyChannel && GetNetworkManager().IsClient() && GetNetworkManager().VoiceReady())
  {
    SetVoiceChannel(ccLobby);
    GWorld->OnChannelChanged();
  }

  bool canSetTransmitTargets = true;
#if _GAMES_FOR_WINDOWS
  // on Xbox 360, set transmit targets when xuid is correctly set
  canSetTransmitTargets = GetClientState() >= NCSLoggedIn;
#endif


  BankList::ReadAccess banks(GFileBanks);
  //@{ Async bank signatures checking (as sigVer2 test with level=2 are slow)
  if (!_checkingSignature && _checkSignatureQueue.Size() > 0)
  {
    int index = _checkSignatureQueue[0]._bankIndex;
    int m = banks.Size();
    if (index < m)
    {
      int maxFrames = _checkSignatureQueue[0]._initialTest ? 3 : 20; // test the whole file in max three frames when initial check is processin
      _checkingSignature = new BankSignatureCheckAsync(index, _checkSignatureQueue[0]._sigVer, _checkSignatureQueue[0]._level, _checkSignatureQueue[0]._type, maxFrames);
      /*
      if (IsDedicatedClient())
      {
        ConsoleF(
          "Checking signature %d:'%s' level %d, %d pending", index, cc_cast(banks[_checkSignatureQueue[0]._bankIndex].GetOpenName()),
          _checkSignatureQueue[0]._level, _checkSignatureQueue.Size()
        );
    }
      */
    }
    else
    {
      LogF("Bank index out of range");
      _checkSignatureQueue.DeleteAt(0);
    }
  }
  if (_checkingSignature)
  {
    if (_checkingSignature->Process())
    {
      // TODO: protect _checkingSignature and _checkSignatureQueue against manipulation by hacker
      DoAssert(_checkSignatureQueue.Size() > 0);
      LogF("Hash level=%d of %s checked.", _checkSignatureQueue[0]._level, cc_cast(banks[_checkSignatureQueue[0]._bankIndex].GetOpenName()));
      // send the proper message
      switch (_checkSignatureQueue[0]._type)
      {
      case NMTDataSignatureAnswer:
        {
          DataSignatureAnswerMessage *answer = static_cast<DataSignatureAnswerMessage *>(_checkSignatureQueue[0]._msg);
          /*
          if (IsDedicatedClient())
          {
            ConsoleF("Data signature answer %d:'%s'. %d pending", answer->_index, cc_cast(answer->_filename), answer->_level);
          }
          */
          if ( _checkingSignature->GetHash(answer->_hash._content) )
          {
            GetNetworkManager().SendMsg(answer);
          }
          else
            GetNetworkManager().HackedDataDetected(banks[_checkSignatureQueue[0]._bankIndex].GetOpenName());
        }
        break;
      case NMTFileSignatureAnswer:
        {
          FileSignatureAnswerMessage *answer = static_cast<FileSignatureAnswerMessage *>(_checkSignatureQueue[0]._msg);
          /*
          if (IsDedicatedClient())
          {
            ConsoleF("File signature answer %d:'%s'. %d pending", answer->_index, cc_cast(answer->_fileName), answer->_level);
          }
          */
          if ( _checkingSignature->GetHash(answer->_dataToHash) )
            GetNetworkManager().SendMsg(answer);
          else
            GetNetworkManager().HackedDataDetected(banks[_checkSignatureQueue[0]._bankIndex].GetOpenName());
        }
        break;
      }
      _checkingSignature = NULL;
      _checkSignatureQueue.DeleteAt(0);
    }
  }
  //@} Async bank signatures checking 


  if (canSetTransmitTargets)
  {
    // list of all addresses, included muted players
    // used for re-translation
    AUTO_STATIC_ARRAY(TransmitTarget, targets, 32);
    AUTO_STATIC_ARRAY(TransmitTarget, all, 64);
    // TODO: this list needs to be create only when _identities is changed
    bool serverFound = false;
#if DEBUG_IDENTITIES
    static InitVal<int,-1> handles[30];
    int handle = 0;
#endif


#if _ENABLE_MP //making binarize project linkable
    // when port address was changed in VoN layer, change it also in PlayerIdentity
    ProcessPeerChanged(&PeerChangedProc, (void *)&_identities /*context*/);
#endif

    for (int i=0; i<_identities.Size(); i++)
    {
      const PlayerIdentity &identity = _identities[i];
      // nobody can hear himself
#if HEAR_YOURSELF
      if (!hearYourself)
#endif
        if (identity.dpnid == _playerDpid) continue;
      
      // add to transmit targets
      TransmitTarget &tt = all.Append();
      TargetFromIdentity(tt,identity,0);

      if (identity.dpnid==BOT_CLIENT)
      {
        serverFound = true;
      }
#if DEBUG_IDENTITIES
      if (netLogValid) //type -netlog as command line parameter
      {
        char *botstr = (identity.dpnid == BOT_CLIENT) ? "(bot client)" : "";
        char diagTxt[1024]; 
        sprintf(diagTxt, "Player %s IP: %d.%d.%d.%d:%d %s",
          cc_cast(identity.name),
          identity._voiceAddress.sin_addr.S_un.S_un_b.s_b1, identity._voiceAddress.sin_addr.S_un.S_un_b.s_b2, 
          identity._voiceAddress.sin_addr.S_un.S_un_b.s_b3, identity._voiceAddress.sin_addr.S_un.S_un_b.s_b4,
          ntohs(identity._voiceAddress.sin_port), botstr);
        GlobalDiagMessage(handles[handle++],0,500,diagTxt);
        //LogF(diagTxt.Data());
      }
#endif
    }

    // make sure bot-client is always there
    // this is important for dedicated server
    if (!serverFound && _serverVoNAddr.sin_port)
    {
      TransmitTarget &tt = all.Append();
      tt.addr = _serverVoNAddr;
      tt.dpid = BOT_CLIENT;
#if _GAMES_FOR_WINDOWS
      tt.xuid = INVALID_XUID;
#endif
      tt.extInfo = 0;
    }

    QSort(all,CompareTransmitTargetId);

    if (forceLobbyChannel)
    {
      typedef OLink(Person) helperType;
      AUTO_STATIC_ARRAY( helperType, players, 128);
      GWorld->ScanPlayers(players);
      // select dead players
      for (int i=0; i<players.Size(); i++)
      {
        Person *person = players[i];
        if (!person->Brain() || person->Brain()->GetLifeState()==LifeStateDead)
        {
          // he is dead - we want to add him into the list
          // we need to get his identity
          int dpnid = person->GetRemotePlayer();
          const PlayerIdentity *id = FindIdentity(dpnid);
          if (id)
          {
            const PlayerIdentity &identity = *id;
            // add only those which do not pass the handling in forceLobbyChannel below
            if (identity.clientState==NCSPlaying)
            {
              TransmitTarget &tt = targets.Append();
              TargetFromIdentity(tt,identity,TTChan2D);
            }
          }
        }
      }
    }
    if (_voiceChannel == CCGlobal || forceLobbyChannel)
    {
      // need not to search - send to all players
      for (int i=0; i<_identities.Size(); i++)
      {
        const PlayerIdentity &identity = _identities[i];
        // nobody can hear himself
  #if HEAR_YOURSELF
        if (!hearYourself)
  #endif
          if (identity.dpnid == _playerDpid) continue;
        // check remote mute list
        if (IsPlayerInRemoteMuteList(identity.dpnid)) continue;
        
        if (forceLobbyChannel)
        {
          // check for players which are not in the game
          if (identity.clientState==NCSPlaying)
          {
            // when in game and we are not, he cannot hear us
            continue;
          }
        }
        // add to transmit targets
        TransmitTarget &tt = targets.Append();
        TargetFromIdentity(tt,identity,TTChan2D);
      }
    }
    else
    {
      AUTO_STATIC_ARRAY(int, players, 32);
      GetPlayersOnChannel(players, _voiceChannel);
      for (int i=0; i<players.Size(); i++)
      {
        const PlayerIdentity *identity = FindIdentity(players[i]);
        if (identity)
        {
          // check remote mute list
          if (IsPlayerInRemoteMuteList(identity->dpnid)) continue;
          // add to transmit targets
          TransmitTarget &tt = targets.Append();
          TargetFromIdentity(tt,*identity,TTChan2D);
        }
      }
    }
#if _ENABLE_MP
    // only alive players can be speaking 3D
    if (agent && agent->GetLifeState() != LifeStateDead)
    {
      // get transmit targets of players in the "direct speak range"
      AUTO_STATIC_ARRAY(TransmitTarget, dsTargets, 32);
      GetTargetsInDirectSpeakRange(dsTargets, _voiceChannel);
      MergeTargets(targets, dsTargets); //targets = targets + dsTargets
    }
    MergeTargets(all, targets); //this will set extInfo to all targets

  // 'all' targets MUST be sorted (it is assumed in VoNClient::setAllTargets)
  // 'targets' need not be sorted, as these all will be used to send VoN message
  QSort(all,CompareTransmitTargetId);
  
  //final set of transmit targets
  VoiceTransmitTargets(targets.Data(), targets.Size());
  VoiceAllTargets(all.Data(), all.Size());
#endif
  }

#if _ENABLE_REPORT
  int VoiceClientGetReceivedDataCount();
  int VoiceClientGetReceivedDataSize();
  int VoiceClientGetReceivedCtrlCount();
  int VoiceClientGetReceivedCtrlSize();
  int VoiceClientGetSentDataCount();
  int VoiceClientGetSentDataSize();
  int VoiceClientGetSentCtrlCount();
  int VoiceClientGetSentCtrlSize();
  void VoiceClientResetStats();

#if DEBUG_VON_DIAGS
  if (netLogValid) //type -netlog as command line parameter
  {
    char dgmessage[1024]; 
    extern PoCriticalSection lockRecvDiagMessage;
    extern char RecvDiagMessage[1024];
    lockRecvDiagMessage.enter();
    if (RecvDiagMessage[0]!=0) DIAG_MESSAGE(1000,RecvDiagMessage);
    RecvDiagMessage[0]=0;
    lockRecvDiagMessage.leave();
    extern PoCriticalSection lockSendDiagMessage;
    extern char SendDiagMessage[1024];
    lockSendDiagMessage.enter();
    if (SendDiagMessage[0]!=0) DIAG_MESSAGE(1000,SendDiagMessage);
    SendDiagMessage[0]=0;
    extern int SendDiagBps;
    sprintf(dgmessage, "sending rate: %.2f kbps", (SendDiagBps*8)/1000.0f);
    if (SendDiagBps!=0) DIAG_MESSAGE(1000,dgmessage);
    lockSendDiagMessage.leave();
    char dgChatChannel[1024];
    extern int diagChatChan;
    sprintf(dgChatChannel, " chan=%x", diagChatChan);
    if (diagChatChan&TTChan2D) strcat(dgChatChannel, " 2D");
    if (diagChatChan&TTChan3D) strcat(dgChatChannel, " 3D");
    if (diagChatChan&TTDirectChan) strcat(dgChatChannel, " Direct");
    extern float diagLastAmpl;
    extern DWORD diagAmplTimeStamp;
    DWORD sysTime = ::GetTickCount();
    if (sysTime - diagAmplTimeStamp > 1500) diagLastAmpl = 0;
    sprintf(dgmessage, "ampl=%.2f %% %s", diagLastAmpl*100.0f, dgChatChannel);
    if (diagLastAmpl!=0) DIAG_MESSAGE(1000, dgmessage);
#if _ENABLE_CHEATS
    extern char GVoNSoundDiag[];
    if (*GVoNSoundDiag) DIAG_MESSAGE(500, GVoNSoundDiag); 
#endif
#if 0 //this diagnostic section was handy only for DirectSound, not for OpenAl
    extern DWORD replayerCurPosition;
    sprintf(dgmessage, "replayerPos: %u", replayerCurPosition);
    DIAG_MESSAGE(1000, dgmessage);
#define DSBSTATUS_PLAYING           0x00000001
#define DSBSTATUS_BUFFERLOST        0x00000002
#define DSBSTATUS_LOOPING           0x00000004
#define DSBSTATUS_LOCHARDWARE       0x00000008
#define DSBSTATUS_LOCSOFTWARE       0x00000010
#define DSBSTATUS_TERMINATED        0x00000020
    if (VoNReplStatus==-1) sprintf(dgmessage, "VoNStatus: Error");
    else sprintf(dgmessage, "VoNStatus: %s %s %s %s %s %s", 
      ((VoNReplStatus & DSBSTATUS_BUFFERLOST) ? "LOST" : ""),
      ((VoNReplStatus & DSBSTATUS_LOOPING) ? "LOOPING" : ""),
      ((VoNReplStatus & DSBSTATUS_PLAYING) ? "PLAYING" : ""),
      ((VoNReplStatus & DSBSTATUS_LOCSOFTWARE) ? "LOCSOFTWARE" : ""),
      ((VoNReplStatus & DSBSTATUS_LOCHARDWARE) ? "LOCHARDWARE" : ""),
      ((VoNReplStatus & DSBSTATUS_TERMINATED) ? "TERMINATED" : "")
      );
    DIAG_MESSAGE(1000, dgmessage);
#endif
  }
#endif

#endif // _ENABLE_REPORT


  _connectionQuality = CQGood;
  _connectionDesync = CDGood;

#if _ENABLE_CHEATS
  if (GInput.GetCheat2ToDo(DIK_RETURN))
  {
    outputLogs = !outputLogs;
    if (outputLogs)
      GlobalShowMessage(500, "message logs on");
    else
      GlobalShowMessage(500, "message logs off");
  }
  if (GInput.GetCheat2ToDo(DIK_SEMICOLON))
  {
    outputDiags++;
    if (GNetworkManager.GetServer())
    {
      if (outputDiags >= NOutputDiags*2) outputDiags = -1;
    }
    else
    {
      if (outputDiags >= NOutputDiags) outputDiags = -1;
    }
  }
  if (GInput.GetCheat2ToDo(DIK_APOSTROPHE))
  {
    _statistics.Clear();
    if (_parent->GetServer()) _parent->GetServer()->GetStatistics().Clear();
  }
#endif

  if (_remoteObjects.card() > 0 && !_parent->IsServer())
  {
    float age = _client->GetLastMsgAge();

    if (age > 10) 
    {
      if (_connectionQuality < CQBad) _connectionQuality = CQBad;
    }
    else if (age > 5)
    {
      if (_connectionQuality < CQPoor) _connectionQuality = CQPoor;
    }
    
    const PlayerIdentity *identity = FindIdentity(_playerDpid);
    if (identity)
    {
      if (identity->_desync>PlayerIdentity::DesyncLow)
      {
        if (identity->_desync>PlayerIdentity::DesyncHigh)
        {
          _connectionDesync = CDHigh;
        }
        else
        {
          _connectionDesync = CDLow;
        }
      }
    }
  }

  _controlsPaused = _client->GetLastMsgAgeReliable() > 10.0f;

  // receive all system and user messages
  {
    PROFILE_SCOPE_EX(nwCRv, network)
    ReceiveSystemMessages();
    ReceiveLocalMessages();
    ReceiveUserMessages();
  }
  if (!_client) 
    return; // session lost

  // statistics
  #if _ENABLE_MP
  if (DiagLevel >= 3)
  {
    int nMsg, nBytes, nMsgG, nBytesG;
    _client->GetSendQueueInfo(nMsg, nBytes, nMsgG, nBytesG);
        if (nMsg > 0 || nMsgG > 0)
        {
            if ( nMsgG < 0 )    // old style
          DiagLogF("Client: pending in SendQueue: %d messages, %d bytes", nMsg, nBytes);
            else                // new style
          DiagLogF("Client: pending in SendQueue: common - %d messages, %d bytes, guaranteed - %d messages, %d bytes",
                         nMsg, nBytes, nMsgG, nBytesG);
        }
  }
  #endif

#if _GAMES_FOR_WINDOWS
  PlayerIdentity *identity = FindIdentity(GetPlayer());
  if (identity)
  {
    bool voiceOn = _parent->IsVoice();
    if (voiceOn != identity->voiceOn)
    {
      identity->voiceOn = voiceOn;
      VoiceOnMessage msg;
      msg._dpnid = GetPlayer();
      msg._voiceOn = voiceOn;
      SendMsg(&msg, NMFGuaranteed);
    }
  }
#endif

  // send update messages
  {
    PROFILE_SCOPE_EX(nwCSd, network)
    SendMessages();
  }

  // update synchronized _serverTime
  _serverTime = ::GetTickCount() - _serverTimeInitTickCount;

  // log all diagnostics
  WriteDiagOutput(false);

#if _ENABLE_CHEATS
  if (_debugWindow)
  {
    _debugWindow->ResetContent();
    for (LocalObjectsContainer::Iterator it=_localObjects.begin(); it; _localObjects.next(it))
    {
      NetworkLocalObjectInfo &oInfo = *it;
      NetworkId id = oInfo.id;
      NetworkObject *object = oInfo.object;
      RString typeName;
      if (object)
#ifdef _CPPRTTI
        typeName = typeid(*object).name();
#else
        typeName = "Unknown";
#endif
      else typeName = "<null>";

      BString<1024> text;
      AIBrain *unit = dynamic_cast<AIBrain *>(object);
      sprintf
      (
        text, "Local %s (%x) %d:%d %s",
        (const char *)typeName, object, id.creator.CreatorInt(), id.id,
        unit ? (const char *)FindEnumName(unit->GetLifeState()) : ""
      );
      _debugWindow->AddString(text);
    }
    for (RemoteObjectsContainer::Iterator it=_remoteObjects.begin(); it; _remoteObjects.next(it))
    {
      NetworkRemoteObjectInfo &oInfo = *it;
      NetworkId id = oInfo.id;
      NetworkObject *object = oInfo.object;
      RString typeName;
      if (object)
#ifdef _CPPRTTI
        typeName = typeid(*object).name();
#else
        typeName = "Unknown";
#endif
      else typeName = "<null>";

      BString<1024> text;
      AIBrain *unit = dynamic_cast<AIBrain *>(object);
      sprintf
      (
        text, "Remote %s (%x) %d:%d %s",
        (const char *)typeName, object, id.creator.CreatorInt(), id.id,
        unit ? (const char *)FindEnumName(unit->GetLifeState()) : ""
      );
      _debugWindow->AddString(text);
    }
  }
#endif

  //@{ Update possible _nearbyItemTransmitters
  if ( !IsDedicatedServer() && GWorld->_nearbyItemTransmittersChanged )
  {
    GWorld->_nearbyItemTransmittersChanged = false;
    SubscribeRadiosMessage msg;
    msg._player = _playerDpid;
    for (int i=0; i<GWorld->_nearbyItemTransmitters.Size(); i++)
    {
      ItemTransmitter *radio = static_cast<ItemTransmitter *>(GWorld->_nearbyItemTransmitters[i].GetRef());
      int j=0;
      for (; j<msg._frequences.Size(); j++)
      {
        if (msg._frequences[j]==radio->tunedFrequency) break;
      }
      if (j == msg._frequences.Size())
      {
        msg._frequences.Add(radio->tunedFrequency); // add unique frequencies
      }
    }
    GWorld->_transmittersFreqSent = msg._frequences; // backup
    SendMsg(&msg, NMFGuaranteed);
  }
  //@}


#if _USE_BATTL_EYE_CLIENT
  if (!_beIntegration.Run(this))
  {
    // reinitialization after auto-update failed, need to disconnect
    Disconnect(LocalizeString(IDS_BATTLEYE_INIT_FAILED));
  }
#endif
}

void NetworkClient::OnSimulateDone()
{
  if (!_parent->IsServer())
  {
    _clientInput->ScanInput();
  }
}

BankSignatureCheckAsync::BankSignatureCheckAsync(int bankIndex, int sigVer, int level, int type, int maxFrames)
: _bankIndex(bankIndex), _sigVer(sigVer), _level(level), _type(type), _maxFrames(maxFrames)
{
  DoAssert(level==2 && sigVer==2); //async used only for level=2 checks
  BankList::ReadAccess banks(GFileBanks);
  _dummyPaddingFilePresent = banks[bankIndex].GetNonDataFileList(_nonDataFiles);
  _curFile = 0;
}

bool BankSignatureCheckAsync::Process()
{
  if (_curFile < _nonDataFiles.Size())
  {
    BankList::ReadAccess banks(GFileBanks);
    QIStreamDataSource in;
    QFBank &bank = banks[_bankIndex];
    bank.AttachSource(in);
    int range = (_nonDataFiles.Size()+_maxFrames-1)/_maxFrames;  //at least one
    int curStopIx = _curFile+range;
    saturateMin(curStopIx, _nonDataFiles.Size());
    int fileOld = in.tellg(); // store original position
    for (; _curFile < curStopIx; _curFile++)
    {
#if _ENABLE_PBO_PROTECTION 
      if (_dummyPaddingFilePresent)
      {
        _dummyPaddingFilePresent = false; // only once
        // get the original content of ___dummypadding___ file by encrypting pbo header using SETUP key
        QFBank::BankHeaderProtectionInfo info;
        bank.GetBankHeaderProtectionInfo(info);
        QOStrStream decryptedHeader;
        if (bank.DecryptHeaders(decryptedHeader, in, info.protHeaderStart, info.protHeaderStart+info.protHeaderSize))
        {
          QOStrStream setupEncryptedHeader;
          QFileSize encrSize = bank.EncryptHeadersSetup(setupEncryptedHeader, decryptedHeader);
          if (encrSize)
          {
            QIStrStream setupHeader(setupEncryptedHeader.str(), setupEncryptedHeader.pcount());
            bank.AddFileHash(_calculator, setupHeader,  _nonDataFiles[_curFile].startOffset-info.protHeaderStart, _nonDataFiles[_curFile].length);
          }
        }
        else bank.AddFileHash(_calculator, in, _nonDataFiles[_curFile].startOffset, _nonDataFiles[_curFile].length);
      }
      else
#endif
      { //after else!
        bank.AddFileHash(_calculator, in, _nonDataFiles[_curFile].startOffset, _nonDataFiles[_curFile].length);
      }
}
    in.seekg(fileOld); // move read pointer to original position
    return false; // continue next simulation step; note: if we are over, the condition fails next simulation step
  }
  else if ( !_nonDataFiles.Size() )
  {
    static const char *dummyDataFiles = "nothing";
    _calculator.Add(dummyDataFiles, 7); //hash dummy content
  }
  return true;
}

bool BankSignatureCheckAsync::GetHash(Temp<char> &hash)
{
  BankList::ReadAccess banks(GFileBanks);
  QFBank &bank = banks[_bankIndex];
  // the final hash will be: HashOf(nonDataHash+fileListHash+pboPrefix)
  AutoArray<char> nonDataHash;
  if (!_calculator.GetResult(nonDataHash))
    return false;
  AutoArray<char> fileListHash;
  if (!bank.GetFileListHash<HashCalculator>(fileListHash))
    return false;
  switch (this->_type)
  {
  case NMTDataSignatureAnswer:
    {
      // and hash it finally
      HashCalculator calculator;
      calculator.Add(nonDataHash.Data(), nonDataHash.Size());
      calculator.Add(fileListHash.Data(), fileListHash.Size());
      calculator.Add(bank.GetPrefix().Data(), bank.GetPrefix().GetLength());
      AutoArray<char> finalHash;
      calculator.GetResult(finalHash);
      hash.Realloc(finalHash.Data(), finalHash.Size()); //Ver2 hash
      return true;
    }
    break;
  case NMTFileSignatureAnswer:
    {
      // concatenate it to create _dataToHash
      hash.Realloc( nonDataHash.Size() + fileListHash.Size() + bank.GetPrefix().GetLength() );
      char *data = hash.Data();
      memcpy(data, nonDataHash.Data(), nonDataHash.Size()); data += nonDataHash.Size();
      memcpy(data, fileListHash.Data(), fileListHash.Size()); data += fileListHash.Size();
      memcpy(data, bank.GetPrefix().Data(), bank.GetPrefix().GetLength());
      return true;
    }
    break;
  }
  return false;
}

void NetworkClient::FastUpdate()
{
}

void OnClientUserMessage(char *buffer, int bufferSize, void *context)
{
  PROFILE_SCOPE_EX(nwCUM, network)

  NetworkClient *client = (NetworkClient *)context;
  NetworkMessageRaw rawMsg(buffer, bufferSize);
  client->DecodeMessage(TO_SERVER, rawMsg);
}

void OnClientSendComplete(DWORD msgID, bool ok, void *context)
{
}

#if _VBS3
extern RString ClientIP;
#endif

/*!
\patch 1.50 Date 4/4/2002 by Jirka
- Fixed: When session was lost, client doesn't fully escape from game to basic multiplayer screen
*/
void NetworkClient::ReceiveSystemMessages()
{
  PROFILE_SCOPE_EX(nwCSM, network)

  if (!_client) return;
  if (_client->IsSessionTerminated() && _playerDpid != BOT_CLIENT && _serverState != NSSNone)
  {
    _clientState = NCSNone;
    _serverState = NSSNone;
    _serverTimeout = INT_MAX;

    NetTerminationReason reason = _client->GetWhySessionTerminated();
    RString format = LocalizeString(IDS_MP_SESSION_LOST);
    RString message = format;
    switch (reason)
    {
      case NTRTimeout: format = LocalizeString(IDS_MP_TIMEOUT_CLIENT); break;
      case NTRKicked:
        {
          RString reasonStr = _client->GetWhySessionTerminatedStr();
          if (reasonStr.IsEmpty())
            format = LocalizeString(IDS_MP_KICKED_CLIENT); 
          else
            format = LocalizeString(IDS_MP_KICKED_CLIENT) + RString(" (") + reasonStr + RString(")");
        }
        break;
      case NTRBanned: format = LocalizeString(IDS_MP_BANNED_CLIENT); break;
      case NTRBadCDKey: 
      case NTRCDKeyInUse: 
      case NTRAuthTimeOut:
        {
          if (reason == NTRBadCDKey) format = LocalizeString(IDS_MP_BAD_CDKEY);
          else if (reason == NTRAuthTimeOut) format = "Authentication Timeout"; 
          else format = LocalizeString(IDS_MP_CDKEY_IN_USE); 
          RString reasonStr = _client->GetWhySessionTerminatedStr();
          if ( !reasonStr.IsEmpty() && stricmp(reasonStr, format)!=0 )
          { // reason and formated strings differ, we should show both
            format = format + RString(" - ") + reasonStr;
          }
          break;
        }
      case NTRSessionLocked: format = LocalizeString(IDS_MP_SESSION_LOCKED); break;
      case NTRBattlEye: 
        {
          RString reasonStr = _client->GetWhySessionTerminatedStr();
          format = LocalizeString(IDS_MP_KICKED_CLIENT) + RString(" (BattlEye: ") + reasonStr + RString(")");
          break;
        }
    }

#if _VBS3
    // if we dont have a client IP address, then
    // prompt like normal.
    if(ClientIP.GetLength() == 0)
#endif
    {
      GChatList.Add(CCSystem, NULL, format, false, true);
      GWorld->CreateWarningMessage(format);
    }
    Done();
    return;
  }
  _client->ProcessSendComplete(OnClientSendComplete, this);
}

void NetworkClient::ReceiveUserMessages()
{
  if (_client) _client->ProcessUserMessages(OnClientUserMessage, this);
}

void NetworkClient::RemoveSystemMessages()
{
  if (_client) _client->RemoveSendComplete();
}

void NetworkClient::RemoveUserMessages()
{
  if (_client) _client->RemoveUserMessages();
}

//! Text representation of NetworkClientState enum values
const EnumName NetworkClientStateNames[]=
{
  EnumName(NCSNone, "NONE"),
  EnumName(NCSCreated, "CREATED"),
  EnumName(NCSConnected, "CONNECTED"),
  EnumName(NCSLoggedIn, "LOGGED IN"),
  EnumName(NCSMissionSelected, "MISSION SELECTED"),
  EnumName(NCSMissionAsked, "MISSION ASKED"),
  EnumName(NCSPlayerAssigned, "PLAYER ASSIGNED"),
  EnumName(NCSMissionReceived, "MISSION RECEIVED"),
  EnumName(NCSGameLoaded, "GAME LOADED"),
  EnumName(NCSPlaying, "PLAYING"),
  EnumName(NCSGameFinished, "GAME FINISHED"),
  EnumName()
};

//! Map NetworkClientState enum values to its text representation
template<>
const EnumName *GetEnumNames(NetworkClientState dummy) {return NetworkClientStateNames;}

void NetworkClient::SetClientState(NetworkClientState state)
{
  if (state == NCSPlayerAssigned)
  {
    // Clear old storage of other client camera positions
    GWorld->ClearClientCameraPositions();
  }
  
  if (state == NCSPlaying && _clientState != NCSPlaying)
    OnMissionStarted();

  if (state <= NCSMissionAsked || state == NCSGameFinished)
    _initScriptRun = false;
  if (state <= NCSLoggedIn || state == NCSGameFinished)
    _delayedSpawns.Clear();

#if _GAMES_FOR_WINDOWS
  // Xbox Live - start / end the session
  if (state == NCSPlaying)
  {
    if (_clientState != NCSPlaying)
    {
      _parent->StartSession();
    }
  }
  else if (_clientState == NCSPlaying)
  {
    _parent->EndSession();
  }
#endif

#if _ENABLE_PERFLOG
  RptF("*** Client: SetClientState %s -> %s", (const char *)FindEnumName(_clientState), (const char *)FindEnumName(state));
#endif
  _clientState = state;
  PlayerIdentity *identity = FindIdentity(_playerDpid);
  if (identity) identity->clientState = state;

  if (state >= NCSConnected)
  {
    ClientStateMessage msg;
    msg._clientState = state;
    SendMsg(&msg, NMFGuaranteed);
  }

  // reset post processes
  if (state == NCSMissionAsked)
  {
    GEngine->ResetGameState();
  }

#if _GAMES_FOR_WINDOWS
  // reset the values which have not yet sense
  if (state < NCSMissionSelected)
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
#endif
}

bool NetworkClient::GetTransferProgress(float &progress) const
{
  if (_totalToCreate < 0 || _totalToUpdate < 0) return false;
  if (_totalToCreate + _totalToUpdate == 0) progress = 1.0f;
  else progress = 1.0f - (float)(_remainingToCreate + _remainingToUpdate) / (float)(_totalToCreate + _totalToUpdate);

// DIAG_MESSAGE(500, Format("Created %d / %d, updated %d / %d", _remainingToCreate, _totalToCreate, _remainingToUpdate, _totalToUpdate));
  return true;
}

int NetworkClient::TimeElapsed() const
{
  return GlobalTickCount() - _missionHeader._start;
}

void NetworkClient::OnJoiningInProgress()
{
  int timeElapsed = TimeElapsed();
  // FIX: AdvanceTime should not be used here as NCMTMissionDate command is sent instead
  // Glob.clock.AdvanceTime(OneSecond * 0.001 * timeElapsed);
  Glob.time = Time(timeElapsed);
}

void NetworkClient::OnClientStateChanged(NetworkClientState state)
{
#if _ENABLE_PERFLOG
  RptF("*** Client: Client state forced %s -> %s", (const char *)FindEnumName(_clientState), (const char *)FindEnumName(state));
#endif

  NetworkClientState origState = _clientState; 

#if _GAMES_FOR_WINDOWS
  // Xbox Live - start / end the session
  if (state == NCSPlaying)
  {
    if (_clientState != NCSPlaying)
    {
      _parent->StartSession();
    }
  }
  else if (_clientState == NCSPlaying)
  {
    _parent->EndSession();
  }
#endif

  _clientState = state;
  PlayerIdentity *identity = FindIdentity(_playerDpid);
  if (identity) identity->clientState = state;

  switch (state)
  {
  case NCSMissionReceived:
    // after _clientState assigned - network object is created in PrepareGame
    if (origState != NCSPlayerAssigned)
      RptF
      (
        "NetworkClient::OnClientStateChanged state %s, expected %s",
        (const char *)FindEnumName(origState),
        (const char *)FindEnumName(NCSPlayerAssigned)
      );
    DoAssert(_missionFileValid || _missionHeader._hasMissionParams);
    _missionFileValid = true;
    if (_parent->IsServer())
    {
      // clear statistics now - ensure they will not be cleared after stats info from clients arrived
      GStats.ClearMission();
    }
    else
    {
      // do even more on non-BOT 
      DoVerify(PrepareGame());
    }
    // empty event handlers set in the previous mission
    _publicVariableEventHandlers.Clear();
    break;
  case NCSPlaying:
    if (origState != NCSPlaying)
      OnMissionStarted();
    break;
  }

#if _GAMES_FOR_WINDOWS
  // reset the values which have not yet sense
  if (state < NCSMissionSelected)
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
#endif
}

void NetworkClient::OnMissionStarted()
{
#if _ENABLE_CHEATS
  extern int GNextPersonDpnid;
  GNextPersonDpnid = 100; //reinit
#endif
  // Register client info as network object
  LogF("Client: Game started, creating client info");
  CreateLocalObject(_clientInfo);
  UpdateObject(_clientInfo, NMFGuaranteed);

  // Register client camera position object
  _clientCameraPosition->_dpnid = GetPlayer();
  CreateObject(_clientCameraPosition);
  UpdateObject(_clientCameraPosition, NMFGuaranteed);

  // number of bodies we want to hide in mission
  _hideBodies = -BODIES_PER_CLIENT;
  ConstParamEntryPtr entry = Pars.FindEntry("maxBodiesAdded");
  if (entry)
  {
    int maxBodiesAdded = *entry;
    _hideBodies = -maxBodiesAdded;
  }
  _bodies.Clear();

  // number of wrecks we want to hide in mission
  _hideWrecks = -WRECKS_PER_CLIENT;
  entry = Pars.FindEntry("maxWrecksAdded");
  if (entry)
  {
    int maxWrecksAdded = *entry;
    _hideWrecks = -maxWrecksAdded;
  }
  _wrecks.Clear();

  //Check the selected chat channel (possibly disabled channel was selected in briefing)
  void SetChatChannel(ChatChannel channel);
  SetChatChannel(ActualChatChannel());
  GWorld->OnChannelChanged();

  BankList::ReadAccess banks(GFileBanks);
  //@{ Ant1HaX0R section
  // Send the Loaded Bank Count
  // hidden inside the NMTChat ... channel = ( #signed files xor (dpnid & 0x7fffffff) ) | 0x80000000
  int nSignedBanks = 0;
  for (int i=0; i<banks.Size(); i++) 
  {
    if (banks[i].VerifySignature()) nSignedBanks++;
}
  int nFiles = _additionalSignedFiles.Size() + nSignedBanks;
  int fakeChannel = ((_playerDpid & 0x7fffffff) ^ nFiles) | 0x80000000;
  GetNetworkManager().Chat((ChatChannel)fakeChannel,"2"); // I know level 2
  //@}
}

void NetworkClient::ClearMissionVoting()
{
  _missionVoting._diff = RString();
}

void NetworkClient::OnServerStateChanged(NetworkServerState state)
{
  _serverState = state;
  _serverTimeout = INT_MAX;
  
  switch (state)
  {
  case NSSNone:
    SetClientState(NCSNone);
    break;
  case NSSSelectingMission:
  case NSSEditingMission:
    if (_clientState > NCSLoggedIn)
      SetClientState(NCSLoggedIn);
    break;
  case NSSAssigningPlayers:
    if (_clientState > NCSMissionAsked)
      SetClientState(NCSMissionAsked);
    break;
  case NSSSendingMission:
    break;
  case NSSPlaying:
    {
      // Do not set clientState to NCSPlaying directly, as the RunInitScript should create the player agent/camera first
      // and player identity should be checked on Steam too
      if (IsDedicatedServer()) SetClientState(NCSPlaying);
      _missionHeader._start = GlobalTickCount();
    }
    break;
  case NSSMissionAborted:
    if (_clientState == NCSPlaying) SetClientState(NCSGameFinished);
    break;
  }
  
#if _VBS3 // player color
  _sentPlayerColour = state == NSSPlaying;
#endif

  if (state!=NSSSelectingMission)
  {
    // if server is not selecting a mission, voting results habe no sense
    ClearMissionVoting();
    _voteMission = false;
  }
}

unsigned NetworkClient::CleanUpMemory()
{
  if (_client) return _client->FreeMemory();
  return 0;
}

void NetworkClient::OnClose()
{
#if _USE_BATTL_EYE_CLIENT
  if (_playerDpid != BOT_CLIENT || !IsDedicatedServer()) // DS bot client does not represent any player
  {
    _beIntegration.Done();
  }
#endif
}

bool NetworkClient::CheckLocalObjects() const
{
  for (LocalObjectsContainer::Iterator it=_localObjects.begin(); it; _localObjects.next(it))
  {
    NetworkLocalObjectInfo &localObject = *it;
    NetworkObject *object = localObject.object;
    if (object)
    {
      for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
      {
        const NetworkUpdateInfo &info = localObject.updates[j];
        if (info.lastSentMsg)
        {
          NetworkMessageNetworkObject *message = (NetworkMessageNetworkObject *)info.lastSentMsg.GetRef();
          if (message->_objectCreator != object->GetNetworkId().creator || message->_objectId != object->GetNetworkId().id)
          {
            Fail("lastSentMsg is bad");
            return false;
          }
        }
      }
    }
  }
  return true;
}

//! Check if given unit is in list of units
/*!
\param soldier unit to find
\param units checked array of units
*/
static bool FindUnit(NetworkObject *soldier, RefArray<NetworkObject> &units)
{
  if (!soldier) return false;
  for (int i=0; i<units.Size(); i++)
  {
    if (units[i] == soldier) return true;
  }
  return false;
}

//! Find output radio channel for given unit and chat channel
/*!
\param unit unit
\param channel chat channel
*/
RadioChannel *FindChannel(AIBrain *unit, int channel)
{
  switch (channel)
  {
  case CCVehicle:
    {
      Transport *veh = unit->GetVehicleIn();
      return veh ? &veh->GetRadio() : NULL;
    }
  case CCGlobal:
    return &GWorld->GetRadio();
  case CCDirect: 
    { // direct speaking
      Person *person = unit->GetPerson();
      return person ? person->GetRadio() : NULL;
    }
  default:
    // possibly one of the CUSTOM channel
    if (channel>=CCCustom1 && channel<=CCCustomLast)
    {
      return &GWorld->GetCustomRadio(channel-CCCustom1);
    }
    // BAD channel otherwise
    RptF("Client: Bad radio channel %d", channel);
    return NULL;
  }

}

RString MissionHeader::GetLocalizedMissionName() const
{
  return _name.GetLocalizedValue();
}
RString MissionHeader::GetLocalizedMissionDesc() const
{
  return _description.GetLocalizedValue();
}

#if _ENABLE_UNSIGNED_MISSIONS
/*!
\patch 1.78 Date 7/22/2002 by Jirka
- Fixed: Better usage of already transferred multiplayer missions (cache added)
*/
bool CheckMissionFile(RString fileName, MissionHeader &header)
{
  RString fileNameExt = fileName + RString(".pbo");

#ifdef _WIN32

  QFileSize size = QIFileFunctions::GetFileSize(fileNameExt);

  if
  (
    header._fileSizeL != size ||
    header._fileSizeH != 0
  ) return false; 

#else   // !defined _WIN32

  LocalPath(fn,fileNameExt);
  struct stat st;
  if ( stat(fn,&st) ) return false;
#if 1       // (sizeof(off_t) <= 4)
  if ( header._fileSizeH ||
       st.st_size != header._fileSizeL )
    return false;
#else
  if ( st.st_size != header._fileSizeL + (((off_t)header._fileSizeH) << 32) )
    return false;
#endif

#endif

  QIFStream f;
  f.open(fileNameExt);
  CRCCalculator crc;
  crc.Reset();
  // use copy function - it does exactly what we need
  f.copy(crc);
  if (crc.GetResult() != header._fileCRC) return false;
  //if (crc.CRC(f.act(), f.rest()) != header.fileCRC) return false;

  CreateMPMissionBank(fileName, header._island);
  return true;
}
#endif

//! client-side settings:
//! custom face file bigger than given limit is ignored
int MaxCustomFaceSize = 100*1024;
//! server-side setting:
//! client attemping to transfer more than given limit will be kicked
/*!
\patch 1.87 Date 10/17/2002 by Ondra
- New: Server can determine max. size of any single custom file acceptable on the server.
To do this add line MaxCustomFileSize=size_in_bytes into Flashpoint.cfg file.
Users with custom face or custom sound larger than this size are kicked when trying to connect.
*/

int MaxCustomFileSize = 128*1024;

static int FileSize(const char *name)
{
  #ifdef _WIN32
  struct stat st;
  if ( stat(name,&st) ) return INT_MAX;
  #else
  LocalPath(fn,name);
  struct stat st;
  if ( stat(fn,&st) ) return INT_MAX;
  #endif
  if (st.st_size>INT_MAX) return INT_MAX;
  return st.st_size;
}

#define NMT_UPDATE_FORMAT(macro, class, name, description, group) \
  case NMT##name: \
  { \
    int index = 0; \
    void UpdateMessageFormat##name(NetworkMessageFormatBase *format, int &index); \
    UpdateMessageFormat##name(format, index); \
    break; \
  }

void UpdateMessageFormat(NetworkMessageFormatBase *format, int type)
{
  switch (type)
  {
  NETWORK_MESSAGE_TYPES(NMT_UPDATE_FORMAT)
#if _HELISIM_ROTORLIB
    NETWORK_MESSAGE_TYPES_RTD(NMT_UPDATE_FORMAT)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_UPDATE_FORMAT)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(NMT_UPDATE_FORMAT)
# endif
#if _VBS3 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(NMT_UPDATE_FORMAT)
#endif
#endif
  default:
    Fail("Unknown message type");
    break;
  }
}

static RString GetMissionCacheDir()
{
  char dir[MAX_PATH];
  if (GetLocalSettingsDir(dir))
  {
    CreateDirectory(dir, NULL);
    TerminateBy(dir, '\\');
    strcat(dir, "MPMissionsCache\\");
    return dir;
  }

  return "MPMissionsCache\\";
}

RString GetClientPath(RString src)
{
  if (src.GetLength() == 0) return src;
  if (src[0] != '$') return src;

  // translate char 's' to Local Settings directory
  char dir[MAX_PATH];
  if (!GetLocalSettingsDir(dir)) return src.Substring(2, INT_MAX); // place in current directory

  TerminateBy(dir, '\\');
  return RString(dir) + src.Substring(2, INT_MAX);
}

RString NetworkClient::GetDebugVoNString()
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
  QOStrStream out;
  out << GDebugVoNBank.GetDebugString(convTable); //basic info (features collected by client)
  //add version info
  RString ver = Format("Version: %s.%d\n", APP_VERSION_TEXT, BUILD_NO);
  out << ver;
  //add IP:PORT info
  in_addr local;
  if (_client && _client->GetLocalAddress(local))
  {
    RString privateIP = Format("Private: %d.%d.%d.%d\n",
      (unsigned)(local.s_addr & 0xff), (unsigned)((local.s_addr>>8) & 0xff),
      (unsigned)((local.s_addr>>16) & 0xff), (unsigned)((local.s_addr>>24) & 0xff)
      );
    out << privateIP;
  }
#if _ENABLE_GAMESPY
  if (_publicAddr!=0)
  {
    RString publicIP = Format("Public: %d.%d.%d.%d\n",
      _publicAddr&0xff, (_publicAddr>>8)&0xff, (_publicAddr>>16)&0xff, (_publicAddr>>24)&0xff
      );
    out << publicIP;
  }
#endif
  if (GetNetworkManager().IsVoiceRecording())
  {
    out << "Note: I am speaking!\n";
  }
#if _ENABLE_MP //making binarize project linkable
  //add Connection type information for each player
  out << "Connections:\n";
  bool serverFound = false;
  for (int i=0; i<_identities.Size(); i++)
  {
    if (_identities[i].dpnid==_playerDpid) continue; //me
    if (_identities[i].dpnid==BOT_CLIENT) serverFound=true; //server
    // connection type (direct, NAT)
    RString cdid = _identities[i].GetDebugId();
    if (cdid.IsEmpty()) cdid = Format("#%d", _identities[i].dpnid);
    // find corresponding TransmitTarget
    RString diag = GetVoiceTargetDiagnostics(_identities[i].dpnid);
    // everything is ready for output
    out << Format("  %s: %s\n", cc_cast(cdid), cc_cast(diag));
  }
  //serve server at last (on standalone servers, BOT_CLIENT has no identity)
  if (!serverFound && _playerDpid!=BOT_CLIENT) //exclude connection server to server 
  {
    RString cdid = Format("#%d", BOT_CLIENT);
    // find corresponding TransmitTarget
    RString diag = GetVoiceTargetDiagnostics(BOT_CLIENT);
    out << Format("  %s: %s\n", cc_cast(cdid), cc_cast(diag));
  }
  // cliques diagnostics
  GetVoiceCliquesDiagnostics(out, convTable);
#endif

  return RString(out.str(), out.pcount());
}

inline void ReadBuffer(void* target, int size, const void*& buffer, int& buffSize)
{
  memcpy(target, buffer, size);
  buffSize -= size;
  buffer = ((const char*)buffer + size);
}

/*!
\patch 5191 Date 11/27/2007 by Jirka
- New: publicVariable now works for all types of variables
*/

bool ReadValue(GameValue &value, const void* buffer, int buffSize, INetworkManager *netManager)
{    
  GameState *gstate = GWorld->GetGameState();
  GameDataNamespace *globalsExt = GWorld->GetMissionNamespace(); // global variables for compiling of GameDataCode

  // read the type name
  const GameTypeType *typeType;
  int typeSize;
  ReadBuffer(&typeSize, sizeof(typeSize), buffer, buffSize);

  if (typeSize==0) return false;
  if(typeSize < 0)
  {
    //internal types
    typeType = gstate->GetType(-typeSize - 1);
    if (!typeType)
    {
      RptF("Unknown variable type index %d", (-typeSize - 1));
      return false;
    }
  }
  else
  {
    char* typeName = (char*)alloca(typeSize + 1);
    ReadBuffer(typeName, typeSize, buffer, buffSize);
    typeName[typeSize] = 0;
    // find the type
    typeType = gstate->FindType(typeName);
    if (!typeType)
    {
      RptF("Unknown variable type %s", cc_cast(typeName));
      return false;
    }
  }
  GameType type(typeType); // TODO: optimize - avoid GameType temporary

  if (type == GameScalar)
  {
    GameScalarType data;
    ReadBuffer(&data, sizeof(data), buffer, buffSize);
    value = data;
  }
  else if (type == GameBool)
  {
    GameBoolType data;
    ReadBuffer(&data, sizeof(data), buffer, buffSize);
    value = data;
  }
  else if (type == GameString)
  {
    RString data((const char*)buffer, buffSize);
    value = data;
  }
  else if (type == GameObject)
  {
    NetworkId id;
    ReadBuffer(&id, sizeof(id), buffer, buffSize);
    Object *obj = netManager ? dynamic_cast<Object *>(netManager->GetObject(id)) : NULL;
    value = GameValueExt(obj);
  }
#if _ENABLE_INDEPENDENT_AGENTS
  else if (type == GameTeamMember)
  {
    NetworkId id;
    ReadBuffer(&id, sizeof(id), buffer, buffSize);
    AITeamMember *member = netManager ? dynamic_cast<AITeamMember *>(netManager->GetObject(id)) : NULL;
    value = GameValueExt(member);
  }
#endif
  else
  {
    Ref<GameData> data = gstate->CreateGameData(type, NULL);
    if (!data) return false;

    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace needed

    QIStrStream in(buffer, buffSize);

    // load content of variable from the stream (using its serialization)
    ParamArchiveLoad ar;
    ar.SetParams(gstate);
    ar.SetParams(netManager,1);
    ar.LoadBin(in, NULL, &globals);

    ar.FirstPass();

    GameDataNamespace *oldNamespace = gstate->SetGlobalVariables(globalsExt);
    LSError err = data->Serialize(ar);
    gstate->SetGlobalVariables(oldNamespace);
    if (err != LSOK) return false;
    
    ar.SecondPass();
    gstate->SetGlobalVariables(globalsExt);
    err = data->Serialize(ar);
    gstate->SetGlobalVariables(oldNamespace);
    if (err != LSOK) return false;

    value = GameValue(data.GetRef());
  }
  return true;
}

#if _VBS3 // handle clipLandKeep objects
	DEF_RSB(slope)
	DEF_RSB(placement)
	DEF_RSB(vertical)
#endif

/// delete all files in $/Player/name/*.*, which are not contained in the fileList
void DeleteObsoleteFiles(const char *name, const AutoArray<CustomFilePaths> &fileList, bool init=false)
{
  if (!name || *name == 0) return;
  RString path;
  if (init)
  {
    path = GetClientPath(RString("$\\players\\") + EncodeFileName(name));
    name = cc_cast(path);
  }

#ifdef _WIN32
  BString<1024> buffer;
  sprintf(buffer, "%s\\*.*", name);

  _finddata_t info;
  intptr_t h = X_findfirst(buffer, &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        // FIX - do not delete only current and parent directory
        if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
        {
          sprintf(buffer, "%s\\%s", name, info.name);
          DeleteObsoleteFiles(buffer, fileList);
        }
      }
      else
      {
        sprintf(buffer, "%s\\%s", name, info.name);
        bool found = false;
        for (int i=0; i<fileList.Size(); i++)
        {
          RString path = GetClientPath(fileList[i]._dst);
          if (stricmp(path, buffer)==0) 
          {
            found = true;
            break;
          }
        }
        if (!found) 
        {
          //RptF("Unlinking: %s", cc_cast(buffer));
          QIFileFunctions::Unlink(buffer);
        }
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
#endif
}

static void GetCustomFilesWanted(/*OUT*/CustomFilesWantedMessage &customFilesWantedMsg, /*IN*/const AutoArray<CustomFilePaths> &fileList)
{
  AutoArray<int> &wanted = customFilesWantedMsg._filesWanted;
  for (int i=0; i<fileList.Size(); i++)
  {
    RString path = GetClientPath(fileList[i]._dst);
    if (QIFileFunctions::FileExists(path) && FileSize(path)==fileList[i]._len)
    {
      unsigned int GetFileCRC(const char *fullname);
      unsigned int crc = GetFileCRC(path);
      if (crc == fileList[i]._crc) continue; //file is correct, no send is necessary
    }
    wanted.Add(i);
  }
}

void NetworkClient::OnPlayerLogout(int dpnid)
{
  _radioSubscribers.OnPlayerDisconnected(dpnid);
}

bool RadioSubscriber::FrequenciesMatch(float f1, float f2) const
{
  const float limit = 0.1f;
  return ( fabs(f1-f2)<limit );
}

void RadioSubscribers::OnPlayerDisconnected(int dpnid)
{
  for (int i=0; i<_subscribers.Size(); i++)
  {
    if (_subscribers[i].dpnid == dpnid)
    {
      _subscribers.Delete(i);
      break;
    }
  }
}

void RadioSubscribers::AddUnique(int dpnid, AutoArray<float> &frequences)
{
  int ix = -1;
  for (int i=0; i<_subscribers.Size(); i++)
  {
    if (_subscribers[i].dpnid==dpnid)
    {
      ix = i;
      break;
    }
  }
  if (ix<0)
  {
    ix = _subscribers.Add();
    _subscribers[ix].dpnid = dpnid;
  }

  _subscribers[ix].frequencies = frequences;
}

bool NetworkClient::CreateRemoteObject(NetworkMessageContext &ctx, std::function<NetworkObject *(NetworkMessageContext &ctx)> create)
{
  // check if object already exist
  PREPARE_TRANSFER(NetworkObject)

    NetworkId id;
  TRANSF_BASE(objectCreator, id.creator);
  TRANSF_BASE(objectId, id.id);

  if ( !CanReceiveUpdateMessages() ) // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
  {
    RptF("Creation of %d:%" OID_FORMAT" ignored, state %s", id.creator.CreatorInt(), id.id, (const char *)FindEnumName(_clientState));
    return false; // updates from the last session
  }

  // update the transfer progress
  if (_clientState == NCSMissionReceived && _remainingToCreate > 0) _remainingToCreate--;

  NetworkObject *obj;
  if (id.creator!=CreatorId(STATIC_OBJECT))
  {
    if (NetworkObject *test = GetObject(id))
    {
      RptF("Object %d:%" OID_FORMAT" (%s) already exists", id.creator.CreatorInt(), id.id, cc_cast(test->GetDebugName()));
      GetNetworkManager().RptFObjectIdJournal(id);
      return false;
    }

    // create object
    ctx.SetClass(NMCCreate);
    obj = create(ctx);
    if (!obj)
    {
      RptF("Cannot create object %d:%" OID_FORMAT"", id.creator.CreatorInt(), id.id);
      return false;
    }
  }
  else
  {
    obj = GetObject(id);
    if (!obj) 
    {
      RptF("Cannot create object %d:%" OID_FORMAT" (static)", id.creator.CreatorInt(), id.id);
      return false;
    }
    ctx.SetClass(NMCCreate);
  }

  InsertRemoteObject(obj, true);

  if (DiagLevel >= 1)
    DiagLogF("Client: remote object created %d:%" OID_FORMAT"", id.creator.CreatorInt(), id.id);

  // on a client-server all remote objects are potentially local as well
  if (_playerDpid==BOT_CLIENT)
  {
    InsertLocalObject(obj);
  }
  obj->TransferMsg(ctx);
  return true;
}

/*!
@param from is always the server (TO_SERVER), no messages can be sent directly.
\patch 1.01 Date 06/18/2001 by Jirka
- Fixed: bad experience increase in MP
- when killed unit on other machine no experience to player was added
\patch_internal 1.01 Date 06/18/2001 by Jirka
- fixed - added experience change to NMTVehicleDestroyed message
\patch_internal 1.26 Date 10/02/2001 by Ondra
- New: message class for updating dammage.
\patch 1.26 Date 10/02/2001 by Ondra
- Improved: decreased lag when hitting other units.
\patch_internal 1.28 Date 10/29/2001 by Jirka
- Fixed: do not update local objects from network
\patch 1.34 Date 12/03/2001 by Jirka
- Fixed: Join in multiplayer
\patch 1.34 Date 12/04/2001 by Jirka
- Fixed: Waypoint synchronization in multiplayer
\patch 1.34 Date 12/04/2001 by Jirka
- Fixed: Trigger activation through radio in multiplayer
\patch 1.43 Date 1/30/2002 by Jirka
- Fixed: Crashes in MP if player waits in lobby and game is in progress
\patch 1.43 Date 2/4/2002 by Jirka
- Fixed: Sometimes camera was on bad person if two players gets in vehicle simultaneously in MP
\patch 1.47 Date 3/8/2002 by Ondra
- Fixed: Config verification checks have increased priority
to avoid timeouts during mission transfer.
\patch 1.56 Date 5/13/2002 by Ondra
- Fixed: More robust handling of addons missing on client.
\patch 1.80 Date 8/5/2002 by Jirka
- Fixed: MP - when admin logged in during select mission screen, no missions was listed
\patch 5114 Date 1/7/2007 by Pete
- Fixed: setWPPos and setWaypointPosition now modifies waypoint position on all computers in MP
\patch_internal 5138 Date 3/9/2007 by Jirka
- Fixed: MP - signatures are not checked for files without config
*/

// note, cannot use TMCHECK inside NetworkClient::OnMessage, as it returns a value
#define TMSGCHECK(command) \
{TMError err = command; if (err != TMOK && err != TMNotFound) break;}

void NetworkClient::OnMessage(int from, NetworkMessage *msg, NetworkMessageType type)
{
  if (_serverState == NSSNone) return;
  
  DoAssert(from==TO_SERVER);

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  
  NetworkMessageFormatBase *format = GetFormat(/*from, */type);
  if (!format)
  {
    RptF("Client: Bad message %d(%s) format", (int)type, cc_cast(GetMsgTypeName(type)));
    return;
  }
  NetworkMessageContext ctx(msg, this, from, MSG_RECEIVE);

  #if _ENABLE_CHEATS
  int level = GetDiagLevel(type,!_parent->GetServer());
  if (level >= 2)
  {
    DiagLogF("Client: received message %s from %d", cc_cast(GetMsgTypeName(type)), from);   
    ctx.LogMessage(format, level, "\t");
  }
  #endif

  switch (type)
  {
    case NMTMsgFormat:
      {
        NetworkMessageMsgFormat *message = (NetworkMessageMsgFormat *)msg;
        int index = message->_index;
        if (index >= NMTN) break; // unknown message
        if (index < NMTFirstVariant) break; 

        int pos = index - NMTFirstVariant;
        _formats.Access(pos);
        TMSGCHECK(_formats[pos].TransferMsg(ctx))
        ::UpdateMessageFormat(&_formats[pos], index);
      }
      break;
    case NMTPlayerClientState:
      {
        PlayerClientStateMessage state;
        TMSGCHECK(state.TransferMsg(ctx))
        PlayerIdentity *identity = FindIdentity(state._dpnid);
        if (identity) identity->clientState = (NetworkClientState)state._clientState;
      }
      break;
    case NMTClientState:
      {
        ClientStateMessage state;
        TMSGCHECK(state.TransferMsg(ctx))

        OnClientStateChanged((NetworkClientState)state._clientState);
      }
      break;
    case NMTUpdateClientOutput:
      {
        ClientOutput output;
        output.TransferMsg(ctx);
        // we need to adjust input to the output received
        _clientInput->OnOutputReceived(output);

      }
      break;
    case NMTNetworkCommand:
      {
        NetworkCommandMessage cmd;
        TMSGCHECK(cmd.TransferMsg(ctx))
        switch (cmd._type)
        {
        case NCMTLogged:
          {
            _gameMaster = true;
            cmd._content.Read(&_admin, sizeof(_admin));
            GChatList.Add(CCSystem, NULL, LocalizeString(IDS_MP_LOGGED), false, true);
          }
          break;
        case NCMTLoggedOut:
          {
            _gameMaster = false;
            _selectMission = false;
            GChatList.Add(CCSystem, NULL, LocalizeString(IDS_MP_LOGGED_OUT), false, true);
          }
          break;
        case NCMTExecResult:
          {
            RString result = cmd._content.ReadString();
            GChatList.Add(CCSystem, NULL, result, false, true);
            LogF("###ServerExecResult: %s", cc_cast(result));
          }
          break;
        case NCMTVoteMission:
          {
            _serverMissions.Clear();
            while (true)
            {
              RString mission = cmd._content.ReadString();
              if (mission.GetLength() == 0) break;
              int index = _serverMissions.Add();
              MPMissionInfo &info = _serverMissions[index];
              info.mission = mission;
              info.world = cmd._content.ReadString();
              info.gameType = cmd._content.ReadString();
              info.displayName = cmd._content.ReadLocalizedString();
              cmd._content.Read(&info.placement, sizeof(int));
              cmd._content.Read(&info.minPlayers, sizeof(int));
              cmd._content.Read(&info.maxPlayers, sizeof(int));
              cmd._content.Read(&info.perClient, sizeof(int));
            }
            _voteMission = true;
          }
          break;
        case NCMTVoteMissionExt:
          {
            int count;
            cmd._content.Read(&count, sizeof(int));
            for (int i=0; i<count; i++)
            {
              MPMissionInfo &info = _serverMissions[i];
              info.description = cmd._content.ReadLocalizedString();
            }
          }
          break;
        case NCMTMonitorAnswer:
          {
            float fps = 0;
            int memory = 0;
            float in = 0;
            float out = 0;
            cmd._content.Read(&fps, sizeof(fps));
            cmd._content.Read(&memory, sizeof(memory));
            cmd._content.Read(&in, sizeof(in));
            cmd._content.Read(&out, sizeof(out));
          
            int sizeNG = 0, sizeG = 0;
            cmd._content.Read(&sizeG, sizeof(sizeG));
            cmd._content.Read(&sizeNG, sizeof(sizeNG));
            
            char buffer[256];
            sprintf
            (
              buffer, LocalizeString(IDS_SERVER_MONITOR),
              fps, 1e-6 * memory, 8e-3 * out, 8e-3 * in,
              sizeNG, sizeG
            );
            GChatList.Add(CCSystem, NULL, buffer, false, true);
          }
          break;
        case NCMTDebugAnswer:
          LogDebugger(cmd._content.ReadString());
          break;
        case NCMTProfileAnswer:
#if _ENABLE_PERFLOG
          void CreateCaptureDialog(const RString &input);
          CreateCaptureDialog(cmd._content.ReadString());
#endif
          break;
#if _VBS3
        case NCMTExitAll:
            // shut down the client
            #if defined _WIN32
            ::PostMessage((HWND)hwndApp, WM_CLOSE, 0, 0);
            #endif
          break;
#endif
        case NCMTMissionTimeElapsed:
          {
            int timeElapsed = 0;
            cmd._content.Read(&timeElapsed, sizeof(timeElapsed));
            _missionHeader._start = GlobalTickCount() - timeElapsed;
          }
          break;
        case NCMTMissionDate:
          {
            // set current clock
            Glob.clock.Read(cmd._content);
						Glob.clockWeather.Read(cmd._content);
            LightSun *sun = GWorld->GetScene()->MainLight();
            if (sun)
            {
              sun->Recalculate(GWorld);
              GWorld->GetScene()->MainLightChanged();
              GWorld->GetScene()->ResetLandShadows();
            }
          }
          break;
        case NCMTCustomFilesProgress:
          {
            int max = 0;
            int pos = 0;
            cmd._content.Read(&max, sizeof(max));
            cmd._content.Read(&pos, sizeof(pos));
            _customFilesProgressMax = max;
            _customFilesProgressPos = pos;
          }
          break;
        case NCMTAskVoNDiagnostics:
          {
            NetworkCommandMessage answer;
            answer._type = NCMTAskVoNDiagnostics;
            answer._content.WriteString(GetDebugVoNString());
            GDebugVoNBank.Reset(); // next #debug von should not contain old data
            SendMsg(&answer, NMFGuaranteed);
          }
          break;
        }
      }
      break;
    case NMTVotingMissionProgress:
      // update voting mission results
      TMSGCHECK(_missionVoting.TransferMsg(ctx))
      break;
    case NMTPlayer:
      {
        PlayerMessage playerMsg;
        TMSGCHECK(playerMsg.TransferMsg(ctx))
        _playerDpid = playerMsg._player;
//{ DEDICATED SERVER SUPPORT
        if (playerMsg._server)
        {
          break; // do not create identity for server
        }
//}

#if _USE_BATTL_EYE_CLIENT
        _beWanted = playerMsg._battlEye;
        if (_beWanted) // BattlEye initialization needed
        {
          if (_playerDpid != BOT_CLIENT || !IsDedicatedServer()) // DS bot client does not represent any player
          {
            if (!Glob.config.beLicenceAgreed)
            {              
              RString guid = GetNetworkManager().GetServerGUID();
              // ask for licence and try to reconnect
              GWorld->OnBELicenceNotAgreed(guid, _serverPassword);
              Disconnect("");
              return;
            }

            if ( !_beIntegration.Init(this) )
            {
              // initialization failed, disconnect
              Disconnect(LocalizeString(IDS_BATTLEYE_INIT_FAILED));
              return;
            }
          }
        }
#endif

        _sigVerRequired = playerMsg._sigVerRequired;
        _updatesGuaranteed = playerMsg._updatesGuaranteed;

        _parent->SetVoNCodecQuality(playerMsg._vonCodecQuality);
        _parent->SetVoNDisabled(playerMsg._vonDisabled);
        if (playerMsg._vonDisabled)
        {
          void destroyVoice();
          destroyVoice();
        }

        {// send a few messages (random count) to break deterministic serials (and CRC) in packets
          int randMsg = GRandGen.RandomInt(1,20);
          for (int i=0; i<randMsg; i++)
          {
            // use CCVehicle to limit possible receivers of the message
            // in fact, at this stage there is no vehicle and no group, and server will not send the message
            // messages have NMFHighPriority at this stage, preventing aggregation (coalescing)
            Chat(CCVehicle,RString());
          }
        }

        // send identity
        PlayerIdentity identity;
        identity.dpnid = _playerDpid;
//        identity.inaddr.S_un.S_addr = playerMsg._inaddr;
        identity.version = MP_VERSION_ACTUAL;
        identity.build = BUILD_NO;
        #if !INTERNAL_KEYS && !_SERVER && _VERIFY_CLIENT_KEY
        {
          if (playerMsg._steamServerId!=0)
          {
            RString playerId = GetPublicKey();
            RString productId = GetProductIdString();
            const Array<unsigned char> &cdKey = GetCDKeyBinary();
            UserCertificate cert = GetUserCertificate(playerMsg._regURL,productId,playerId,cdKey);
            identity.certificate = cert.certificate;
            if (identity.certificate.Size()>0)
            {
              AutoArray<unsigned char> signature = SignData(playerMsg._steamServerId,Array<unsigned char>(reinterpret_cast<unsigned char *>(cert.privateKey.Data()),cert.privateKey.Size()));
              identity.challengeSigned = AutoArray<char>(reinterpret_cast<char *>(signature.Data()),signature.Size());
              identity.blockedKeys = cert.blockedKeys.keys;
              identity.blockedKeysSignature = cert.blockedKeys.signature;
            }
            else
            {
              RString message = Format("Registration failed: %s",cc_cast(cert.errorMessage));
              GChatList.Add(CCSystem, NULL, message, false, true);
            }
          }
        }
        #endif
#if defined(DAYZ_PRIVATE_SERVER)
        identity.id = GetPrivateServerUID();
#elif BATTLEYE_INTERNAL_ID
// set BATTLEYE_INTERNAL_ID when you need to test BattlEye using internal DEV666 public keys
        RString fakeID = GetPublicKey();
        identity.id = fakeID.Substring(3,fakeID.GetLength());
#else
        identity.id = GetPublicKey();
#endif
        // include userId from the certificate here
        identity.name = playerMsg._name; // name is changed by server
        identity.face = Glob.header.playerFace;
        identity.glasses = Glob.header.playerGlasses;
        identity.speakerType = Glob.header.playerSpeakerType;
        identity.pitch = Glob.header.playerPitch;
        identity._minPing = 10;
        identity._avgPing = 100;
        identity._maxPing = 1000;
        identity._minBandwidth = 14;
        identity._avgBandwidth = 28;
        identity._maxBandwidth = 33;

        BankList::ReadAccess banks(GFileBanks);
        identity.filesCount = banks.Size() + _additionalSignedFiles.Size();

#if _ENABLE_STEAM_IDENTITY && defined(_WIN64)
        if (SteamUser())
        {
          identity._steamID = SteamUser()->GetSteamID().ConvertToUint64();
          char steamTicket[1024];
          uint32 steamTicketSize = 0;
          memset(steamTicket, 0, sizeof(steamTicket));
          SteamUser()->GetAuthSessionTicket(steamTicket, sizeof(steamTicket), &steamTicketSize, NULL);
          if (identity._steamID != 0 && steamTicketSize > 0 && steamTicketSize <= sizeof(steamTicket))
          {
            identity._steamSessionTicket.Copy(steamTicket, steamTicketSize);
            identity.id = Format("%I64u", (unsigned __int64)identity._steamID);
          }
          else
          {
            identity._steamID = 0;
          }
        }
#endif


        GameDataNamespace globals(NULL, RString(), false); // save - no global namespace

		  // send face to server
        bool localServer = _parent->IsServer();
        RString GetUserDirectory();
        if (stricmp(identity.face, "custom") == 0)
        {
          RString srcDir = GetUserDirectory();
          RString dstDir = GetClientCustomFilesDir() + RString("\\players\\") + EncodeFileName(identity.name) + RString("\\");
          DeleteDirectoryStructure(dstDir, true); // clear first
          CreatePath(dstDir);
          RString serverDir;
          if (localServer)
          {
            serverDir = GetServerTmpDir() + RString("\\players\\") + EncodeFileName(identity.name) + RString("\\");
            CreatePath(serverDir);
          }
          else
          {
            // char '#' is used as an indication of Local Settings directory + ServerTmpDir (different on server than on client)
            serverDir = RString("#") + RString("\\players\\") + EncodeFileName(identity.name) + RString("\\");
          }
          RString src = srcDir + RString("face.paa");
          if (QIFileFunctions::FileExists(src) && FileSize(src)<=MaxCustomFaceSize && ValidDataFile(src))
          {
            RString dst = dstDir + RString("face.paa");
            QIFileFunctions::Copy(src, dst); // "send" the file to yourself
            RString server = serverDir + RString("face.paa");
            if (localServer) QIFileFunctions::Copy(src, server);
            else TransferFileToServer(server, src);
          }
          else
          {
            src = srcDir + RString("face.jpg");
            if (QIFileFunctions::FileExists(src) && FileSize(src)<=MaxCustomFaceSize && ValidDataFile(src))
            {
              RString dst = dstDir + RString("face.jpg");
              QIFileFunctions::Copy(src, dst); // "send" the file to yourself
              RString server = serverDir + RString("face.jpg");
              if (localServer) QIFileFunctions::Copy(src, server);
              else TransferFileToServer(server, src);
            }
          }
        }

        SendMsg(&identity, NMFGuaranteed);
      }
      break;
    case NMTRemoteMuteList:
#if _ENABLE_MP
      TMSGCHECK(_remoteMuteList.TransferMsg(ctx))
#endif
      break;
    case NMTLogin:
      {
        int index = _identities.Add();
        PlayerIdentity &identity = _identities[index];
        TMError err = identity.TransferMsg(ctx); 
        if (err != TMOK && err != TMNotFound) 
        {
          _identities.Delete(index,1);
          break;
        }
//        if (identity.dpnid == BOT_CLIENT && _client) _client->GetDistantAddress(identity.inaddr);
        if (identity.dpnid == _playerDpid)
        {
          // store playerId
          _playerID =  identity.playerid;
          SetCreatorId(CreatorId(identity.playerid));
          // login confirmed
          if (_clientState<NCSLoggedIn) SetClientState(NCSLoggedIn);  
#if _GAMES_FOR_WINDOWS
          // voiceAddress was deduced from the xnaddr
          
          // if needed, update the mute list
          if (_identities.Size() > 1) UpdateMuteList();
#else
          // send info needed to establish peer to peer connection
          in_addr local;
          int port;
          if (_client && _client->GetLocalAddress(local,port))
          {
            sockaddr_in vaddr;
            vaddr.sin_port = htons(port);
            vaddr.sin_family = AF_INET;
            vaddr.sin_addr.s_addr = local.s_addr;
            memset(&(vaddr.sin_zero), '\0', 8); // zero the rest of the struct

            // fill my own address so that HEAR_YOURSELF works. If necessary, ConnectVoice can overwrite it later
            identity._voiceAddress = vaddr;

            // ask the server to connect us with other players
            AskConnectVoiceMessage ask;
            ask._privateAddr = local.s_addr; //S_un.S_addr;
            ask._verNo = BUILD_NO;
#if _ENABLE_GAMESPY
            if (_publicAddr != 0)
            {
              ask._publicAddr = _publicAddr;
            }
            else
#endif
            {
              ask._publicAddr = ask._privateAddr;
#             if BETA_TESTERS_DEBUG_VON && _ENABLE_GAMESPY
              RptF("VoNLog,Client: Warning! publicAddr was zero while Login. It was set to privateAddr.");
#             endif
            }
            ask._voicePort = port;
#ifdef _WIN32
            LogF("VoNLog: Client sent AskConnectVoiceMessage %d.%d.%d.%d:%d to server",local.S_un.S_un_b.s_b1,local.S_un.S_un_b.s_b2,local.S_un.S_un_b.s_b3,local.S_un.S_un_b.s_b4,port);
#endif
            SendMsg(&ask, NMFGuaranteed);
          }
#endif
        }
        else
        {
          DecFadeSemaphore(FadePlayerConnected);
#if _GAMES_FOR_WINDOWS
          // check if a new player is not muted, update mute list if so
          int userIndex = GSaveSystem.GetUserIndex();
          if (userIndex >= 0)
          {
            BOOL muted = false;
            if (SUCCEEDED(XUserMuteListQuery(userIndex, identity.xuid, &muted)) && muted)
            {
              // add to the list
              _muteList._list.Add(identity.xuid);
              // send the updated list to the server
              if (_clientState >= NCSConnected) SendMsg(&_muteList, NMFGuaranteed);
            }
          }
#endif // _GAMES_FOR_WINDOWS
        }
        // identity.state = NGSLogin;
      }
      break;
    case NMTLogout:
      {
        LogoutMessage logout;
        TMSGCHECK(logout.TransferMsg(ctx))
        for (int i=0; i<_identities.Size(); i++)
        {
          PlayerIdentity &identity = _identities[i];
          if (identity.dpnid == logout._dpnid)
          {
            OnPlayerLogout(identity.dpnid);
            _identities.Delete(i);
            break;
          }
        }
      }
      break;
    case NMTCustomFileList:
      {
        CustomFileListMessage fileListMsg;
        TMSGCHECK(fileListMsg.TransferMsg(ctx))
        CustomFilesWantedMessage customFilesWantedMsg;
        customFilesWantedMsg._player = fileListMsg._player;
        GetCustomFilesWanted(customFilesWantedMsg, fileListMsg._fileList);
        DeleteObsoleteFiles(fileListMsg._name, fileListMsg._fileList, true);
        SendMsg(&customFilesWantedMsg, NMFGuaranteed);
      }
      break;
    case NMTDeleteCustomFiles:
      {
        DeleteCustomFilesMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        for (int i=0; i<msg._list.Size(); i++)
        {
          if (!msg._list[i].IsEmpty())
          {
            RString dir = GetClientPath(RString("$\\players\\") + EncodeFileName(msg._list[i]));
            RString path = GetClientPath(dir);
            if (path.GetLength()>8) DeleteDirectoryStructure(path, true); // only to be more safe
          }
        }
      }
      break;
    case NMTDiagObjectInfoAsk:
      {
        DiagObjectInfoAskMessage ask;
        ask.TransferMsg(ctx);
        NetworkId id(ask._creator, ask._id);
        NetworkObject *obj = GetObject(id);
        if (obj)
        {
          RString name = obj->GetDebugName();
          Vector3 pos = obj->GetCurrentPosition();

          RString answerStr = Format("(%d:%" OID_FORMAT") %s [%.1f,%.1f,%.1f]", ask._creator.CreatorInt(), ask._id, cc_cast(name), pos.X(),pos.Y(),pos.Z());
          if (Entity *entity = dyn_cast<Entity>(obj))
          {
            Entity *parent = entity->GetHierarchyParent();
            if (parent)
            {
              name = parent->GetDebugName();
              id = parent->GetNetworkId();
              answerStr = answerStr + Format(" parent: (%d:%" OID_FORMAT") %s",id.creator.CreatorInt(), id.id, cc_cast(name));
            }
          }
          ClientDiagToServerMessage answer;
          answer._response = answerStr;
          SendMsg(&answer, NMFGuaranteed);
        }
        else
        {
          RString answerStr = Format("(%d:%" OID_FORMAT") not found", ask._creator.CreatorInt(), ask._id);
          ClientDiagToServerMessage answer;
          answer._response = answerStr;
          SendMsg(&answer, NMFGuaranteed);
        }
      }
      break;
    case NMTMissionHeader:
      {
        TMSGCHECK(_missionHeader.TransferMsg(ctx))
        //RptF("*** Client: NMTMissionHeader");

#if _GAMES_FOR_WINDOWS
        // set the properties and contexts for the rich presence
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
        int difficulty = Glob.config.diffNames.GetValue(_missionHeader._diffName);
        if (difficulty < 0) difficulty = Glob.config.diffDefault;
        GSaveSystem.SetContext(CONTEXT_GAME_DIFFICULTY, difficulty);
        // - MPMission
        int missionId = 0;
        // TODOXNET: mission identification
        GSaveSystem.SetContext(CONTEXT_GAME_MPMISSION, missionId);
#endif

        _missionHeader._fileDir = GetClientPath(_missionHeader._fileDir);

		  if (_missionHeader._updateOnly) break;
        {
          // ADDED - AddOns check
          void CheckPatch(FindArrayRStringCI &addOns, FindArrayRStringCI &missing);
          FindArrayRStringCI missing;
          CheckPatch(_missionHeader._addOns, missing);
#if !_VBS2  // disable MP missing addon warning messages (temp)
          if (missing.Size() > 0)
          {
            RString ReportMissingAddons(FindArrayRStringCI missing);
            RString message = ReportMissingAddons(missing);
            Disconnect(message);
            return;
          }
#endif
          GWorld->ActivateAddons(_missionHeader._addOns,_missionHeader._island);
        }

        Glob.config.difficulty = Glob.config.diffDefault;
        {
          int difficulty = Glob.config.diffNames.GetValue(_missionHeader._diffName);
          if (difficulty >= 0) Glob.config.difficulty = difficulty;
          else
          {
            RptF("Unknown difficulty: %s", (const char *)_missionHeader._diffName);
          }
        }
        
#if _ENABLE_UNSIGNED_MISSIONS
        // check if mission file is valid
        if (_missionHeader._noCopy)
        {
          // if list of addons is OK, we already have the correct mission
          _missionFileValid = true;
        }
        else
        {
          NetworkServer *server = _parent->GetServer();
          if (server)
          {
            // copy file
            RString src = server->GetMissionFilename();
            RString dst = _missionHeader._fileDir + _missionHeader._fileName + RString(".pbo");
            CreatePath(dst);
            QIFileFunctions::Copy(src, dst);
            _missionFileValid = true;
          }
          else
          {
            RString fullname = _missionHeader._fileDir + _missionHeader._fileName;
            _missionFileValid = CheckMissionFile(fullname, _missionHeader);

            if (!_missionFileValid)
            {
              // check cache
              RString fullname = GetMissionCacheDir() + _missionHeader._fileName;
              _missionFileValid = CheckMissionFile(fullname, _missionHeader);
            }
          }
        }
#else
        _missionFileValid = true;
#endif // _ENABLE_UNSIGNED_MISSIONS

        if (_clientState < NCSMissionSelected) 
          SetClientState(NCSMissionSelected);
      }
      break;
    case NMTSelectPlayer:
      {
        SelectPlayerMessage pl;
        TMSGCHECK(pl.TransferMsg(ctx))
        NetworkId nid(pl._creator, pl._id);
        NetworkObject *object = GetObject(nid);
        Person *person = dynamic_cast<Person *>(object);
        ProcessSelectPlayer(person, pl._respawn, pl._position);
      }
      break;
    case NMTTransferFile:
      {
        TransferFileMessage transfer;
        TMSGCHECK(transfer.TransferMsg(ctx))
        transfer._path = GetClientPath(transfer._path);
        ReceiveFileSegment(transfer);
      }
      break;
#if _ENABLE_UNSIGNED_MISSIONS
    case NMTTransferMissionFile:
      {
        //RptF("*** Client: NMTTransferMissionFile");

        TransferMissionFileMessage transfer;
        TMSGCHECK(transfer.TransferMsg(ctx))
        transfer._path = GetClientPath(transfer._path);

        const char *prefix = "mpmissions\\__cur_mp.";
        GFileBanks.Remove(prefix);

        int ret = ReceiveFileSegment(transfer);
        if (ret > 0)
        {
// FIX: transfer mission file always into tmp directory (do not rewrite original file)
//          CreateMPMissionBank(_missionHeader.fileDir + _missionHeader.fileName, _missionHeader.island);
          const char *ptr = transfer._path;
          const char *ext = strrchr(ptr, '.');
          DoAssert(ext);
          DoAssert(stricmp(ext, ".pbo") == 0);
          RString path = transfer._path.Substring(0, ext - ptr);
          CreateMPMissionBank(path, _missionHeader._island);

          DoAssert(!_missionFileValid);
          _missionFileValid = true;
          // TODO: this is not bot client
          DoAssert(!_parent->IsServer());
        }
        else if (ret<0)
        {
          RString format = LocalizeString(IDS_MP_VALIDERROR_2);
          RString message;
          const PlayerIdentity *id = FindIdentity(_playerDpid);
          if (id) message = Format(format, cc_cast(id->name)) + Format(" - %s", cc_cast(transfer._path));
          Disconnect(message);
        }
      }
      break;
#endif // _ENABLE_UNSIGNED_MISSIONS
    case NMTAskForPilotLight:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForPilotLightMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._vehicle) ask._vehicle->SetPilotLight(ask._enable);
      }
      break;
    case NMTAskForVehicleDoorState:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForVehicleDoorStateMessage ask;
        ask.TransferMsg(ctx);
        if (ask._vehicle) ask._vehicle->FutureVisualState().SetDoorOpen(ask._doorIndex, ask._opened);
      }
      break;
    case NMTAskForMoveVector:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForMoveVectorMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._vehicle)
        {
          ask._vehicle->ResetRemoteState();

#if _VBS3 // handle clipLandKeep objects
          if (ask._vehicle->Static())
          {
            LODShape *shape = ask._vehicle->GetShape();
            Entity *objT = dyn_cast<Entity>((Object *)ask._vehicle);
            if (
              shape && (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)
              || shape->PropertyValue(RSB(placement))==RSB(slope)
              || shape->PropertyValue(RSB(placement))==RSB(vertical)
              || (objT && objT->Type()->_placement==PlacementVertical))
            )
            {
              Matrix4 trans = ask._vehicle->Transform();
              trans.SetPosition(ask._pos);
              trans.SetUpAndDirection(VUp,trans.Direction());
              ask._vehicle->InitSkew(GLandscape,trans);
              ask._vehicle->Move(trans);
              break;
            }
          }
#endif
          ask._vehicle->Move(ask._pos);
        }
      }
      break;
    case NMTAskForMoveMatrix:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForMoveMatrixMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._vehicle)
        {
          ask._vehicle->ResetRemoteState();
          Matrix4 trans;
          trans.SetPosition(ask._pos);
          trans.SetOrientation(ask._orient);
#if _VBS3 // handle clipLandKeep objects
          if (ask._vehicle->Static())
          {
            LODShape *shape = ask._vehicle->GetShape();
            Entity *objT = dyn_cast<Entity>((Object *)ask._vehicle);
            if (
              shape && (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)
              || shape->PropertyValue(RSB(placement))==RSB(slope)
              || shape->PropertyValue(RSB(placement))==RSB(vertical)
              || (objT && objT->Type()->_placement==PlacementVertical))
            )
            {
              trans.SetUpAndDirection(VUp,trans.Direction());
              ask._vehicle->InitSkew(GLandscape,trans);
            }
          }
#endif
          ask._vehicle->Move(trans);
        }
      }
      break;
    case NMTAskForSetMagazineAmmo:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForSetMagazineAmmoMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        Ref<Magazine> magazine = dyn_cast<Magazine>(ask._magazine.GetLink());
        if (magazine)
        {
          magazine->SetAmmo(ask._ammo);
        }
      }
      break;
    case NMTProjectileDecal:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        ProjectileDecalMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        Ref<AmmoType> ammo;
        if (ask._ammoType.GetLength() > 0)
        {
          Ref<EntityType> type = VehicleTypes.New(ask._ammoType);
          ammo = dynamic_cast<AmmoType *>(type.GetRef());
          if (ammo)
          {
            HitInfo hitInfo(ammo, ask._hitPosWorld, ask._surfNormal, ask._inSpeed, ask._outSpeed);
            if (ask._directHit != nullptr)
            {
              GLandscape->ApplyProjectileDecal(ask._directHit, ask._componentIndex, hitInfo, ask._hitDepth, 
                ask._hitPosLcl, ask._hitDirLcl, ask._hitDirUpLcl);
              if (IsBotClient())
              {
                SendMsg(&ask, NMFGuaranteed);
              }
            }
            else
            {
              GLandscape->ApplyProjectileDecal(hitInfo, ask._hitDepth);
            }
          }
        }
      }
      break;
    case NMTRemoveProjectileDecals:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        RemoveProjectileDecalsMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._person)
        {
          Man *man = dyn_cast<Man>(ask._person.GetLink());
          if (man) man->RemoveProjectileDecals();
          if (IsBotClient())
          {
            SendMsg(&ask, NMFGuaranteed);
          }
        }
      }
      break;
    case NMTBowState:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        BowStateMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._person)
        {
          Man *man = dyn_cast<Man>(ask._person.GetLink());
          BowState bowState = (BowState)ask._state;
          man->OnBowStateChange(bowState);
        }
      }
      break;
    case NMTExplosionDamageEffects:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        ExplosionDamageEffectsMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        Ref<AmmoType> ammo;
        if (ask._type.GetLength() > 0)
        {
          Ref<EntityType> type = VehicleTypes.New(ask._type);
          ammo = dynamic_cast<AmmoType *>(type.GetRef());
          if (ammo)
          {
            HitInfo hitInfo(ammo, ask._pos, ask._surfNormal, ask._inSpeed, ask._outSpeed);
            GLandscape->ExplosionDamageEffects(
              ask._owner, ask._directHit, hitInfo, ask._componentIndex,
              ask._enemyDamage, ask._energyFactor, ask._explosionFactor
            );
          }
        }
      }
      break;
    case NMTExplosionDamage:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        ExplosionDamageMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        Ref<AmmoType> ammo;
        if (ask._type.GetLength() > 0)
        {
          Ref<EntityType> type = VehicleTypes.New(ask._type);
          ammo = dynamic_cast<AmmoType *>(type.GetRef());
          if (ammo)
          {
            HitInfo hitInfo(ammo, ask._pos, ask._surfNormal, ask._inSpeed, ask._outSpeed);
            GLandscape->ExplosionDamage(
              ask._owner, ask._originDir, ask._directHit, ask._directHitModelPos, hitInfo, ask._componentOwner, ask._componentIndex,
              ask._energyFactor, ask._explosionFactor, ask._hitDepth
              );
          }
        }
      }
      break;
    case NMTAskForAmmo:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForAmmoMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (!ask._vehicle) break;
        WeaponModePtr ptr;
        ptr._weapon = ask._weapon;
        ptr._muzzle = ask._muzzle;
        ptr._mode = ask._mode;

        Magazine *magazine = ptr.GetMagazine();
        if (!magazine) break;
        saturateMin(ask._burst, magazine->GetAmmo());
        ADD_ENCRYPTED(magazine->_ammo, magazine->_ammoSupport, -ask._burst);
        if (magazine->GetAmmo() < 0) magazine->SetAmmo(0);
      }
      break;
    case NMTFireWeapon:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        FireWeaponMessage fire;
        TMSGCHECK(fire.TransferMsg(ctx))
        EntityAIFull *veh = fire._vehicle;
        TurretContext context;
        if (veh && veh->FindTurret(fire._gunner, context))
        {
          RemoteFireWeaponInfo remoteInfo;
          remoteInfo._position = fire._pos;
          remoteInfo._direction = fire._dir;
          remoteInfo._visible = fire._visible;
#if _VBS3
          remoteInfo._projectile = fire._projectile;
#endif
          WeaponModePtr ptr;
          ptr._weapon = fire._weapon;
          ptr._muzzle = fire._muzzle;
          ptr._mode = fire._mode;

          if (veh->FireWeaponEffects(context, ptr, fire._magazine, fire._target,&remoteInfo))
          {
            const WeaponModeType *mode = ptr.GetWeaponModeType();
            veh->RemoveAmmoAfterShot(mode, fire._magazine);
          }

#if _VBS3
          // set the local client who is shooting the laserTarget.
          LaserTarget *lstg = dyn_cast<LaserTarget>(fire._projectile.GetLink());
          if(lstg)
            lstg->SetOwner(veh);
#endif

        }
      }
      break;
    case NMTAskForPlayerAction:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        AskForPlayerActionMessage action;
        TMSGCHECK(action.TransferMsg(ctx))
        if (action._person)
        {
          action._person->PlayerAction(action._action);
        }
        // 
      }
      break;
    case NMTVehicleDestroyed:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        VehicleDestroyedMessage info;
        TMSGCHECK(info.TransferMsg(ctx))

        GStats.OnVehicleDestroyed(info._killed, info._killer);

        // FIX - add experience
        if (info._killer && info._killed)
        {
          // use Entity member to get original target side
          // all dead bodies are considered civilian
          TargetSide origSide = info._killed->Entity::GetTargetSide();
          const VehicleType &type = *info._killed->GetType();

          // increase killer's experience
          AIBrain *kBrain = info._killer->CommanderUnit();
          if (kBrain && kBrain->IsLocal())
          {
            kBrain->IncreaseExperience(type, origSide);
            // send radio message
            if (kBrain->IsEnemy(origSide))
            {
              // find corresponding target
              Target *tar = kBrain->FindTargetAll(info._killed);
              if (tar)
              {
                // mark killer
                // when destroyed will be set, it will be marked for reporting
                tar->MarkKiller(info._killer);
              }
            }
          }
          if
          (
            info._killer->GunnerUnit() &&
            info._killer->GunnerUnit()->IsLocal() &&
            info._killer->GunnerUnit() != kBrain
          )
            info._killer->GunnerUnit()->IncreaseExperience(type, origSide);
          if
          (
            info._killer->PilotUnit() &&
            info._killer->PilotUnit()->IsLocal() &&
            info._killer->PilotUnit() != kBrain
          )
            info._killer->PilotUnit()->IncreaseExperience(type, origSide);
        }
      }
      break;
    case NMTVehicleDamaged:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
#if _ENABLE_VBS
        VehicleDamagedMessage info;
        TMSGCHECK(info.TransferMsg(ctx))
        if (info._damaged) GStats.OnVehicleDamaged(info._damaged, info._killer, info._damage, info._ammo);

#endif
      }
      break;
    case NMTMarkerCreate:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        MarkerCreateMessage marker;
        TMSGCHECK(marker.TransferMsg(ctx))
        RString name = marker._marker.name;
        int index = -1;
        for (int i=0; i<markersMap.Size(); i++)
        {
          if (stricmp(markersMap[i].name, name) == 0)
          {
            index = i;
            break;
          }
        }
#if _VBS3 // marker EH
        bool updated = index > -1;
#endif
        if (index < 0) index = markersMap.Add();
        markersMap[index] = marker._marker;
#if _VBS3 // marker EH
        void ProcessCreateMarkerEH(RString name, bool updated = true);
        ProcessCreateMarkerEH(name,updated);
#endif
      }
      break;
    case NMTMarkerDelete:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        MarkerDeleteMessage info;
        TMSGCHECK(info.TransferMsg(ctx))
        for (int i=0; i<markersMap.Size(); i++)
        {
          ArcadeMarkerInfo &marker = markersMap[i];
          if (marker.name == info._name)
          {
#if _VBS3 // marker EH
            void ProcessDeleteMarkerEH(RString name);
            ProcessDeleteMarkerEH(info._name);
#endif
            markersMap.Delete(i);
            break;
          }
        }
      }
      break;
#if _ENABLE_VBS
    case NMTSetObjectTexture:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        SetObjectTextureMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        EntityAI* veh = msg._veh;   
        if (veh)
        {
          bool setTexture = true;
          if (msg._eval.GetLength() > 0)
          {
            GameState *state = GWorld->GetGameState();
            GameVarSpace vars(false);
            int gshandle = state->BeginContext(&vars);
            state->VarSetLocal("_this", GameValueExt(veh), true);
            setTexture = state->EvaluateMultiple(msg._eval, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
            state->EndContext(gshandle);
          }
          if (setTexture)
          {
#if _VBS3
            if(stricmp(msg._name, "#reset") == 0)
              veh->ResetObjectTexture(msg._index);
            else  
#endif
            {
              Ref<Texture> texture = GlobLoadTexture(msg._name);
              veh->SetObjectTexture(msg._index, texture);
            }
          }
        }
      }
      break;
    case NMTPublicExec:
      {
        PublicExecMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        if (msg._condition.GetLength() > 0)
        {
          GameState *state = GWorld->GetGameState();
          GameVarSpace vars(false);
          int gshandle = state->BeginContext(&vars);
          if (msg._obj) 
            state->VarSetLocal("_this", GameValueExt(msg._obj,GameValExtObject), true);
          else
          {
            GameValue value = state->CreateGameValue(GameArray);
            GameArrayType &array = value;
            array.Resize(msg._objs.Size());
            for (int i=0; i<msg._objs.Size(); i++)
              array[i] = GameValueExt(msg._objs[i]);
            state->VarSetLocal("_this", value, true);
          }
          bool doCommand = state->EvaluateMultiple(msg._condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
          if (doCommand && msg._command.GetLength() > 0)
            state->EvaluateMultiple(msg._command, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
          state->EndContext(gshandle);
        }
      }
      break;
#endif // _ENABLE_VBS
    case NMTChangeOwner:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        ChangeOwnerMessage co;
        TMSGCHECK(co.TransferMsg(ctx))
        NetworkId id(co._creator, co._id);
        if (co._owner == _playerDpid)
        {
          // object becomes local
          //DoAssert(_remoteObjects.presentKey(id));
          NetworkRemoteObjectInfo info;
          if (_remoteObjects.get(id,info) && info.object)
          {
            NetworkObject *object = info.object;
            if ( object->IsObsolete() ) // used for ClientCameraPositionObject (when camera is no longer tracking a player)
            {
              ForceDeleteObject(object);
              // object->DestroyObject(); // this is no longer needed, it is done by ForceDeleteObject
              break;
            }
            object->SetLocal(true);
            info.remote = false;
            _remoteObjects.put(info);
            if (_playerDpid==BOT_CLIENT)
            {
              // object never ceased to be local on the server in a client-server architecture
              DoAssert(_localObjects.get(id)!=nullptr);
            }
            else
            {
              DoAssert(_localObjects.get(id)==nullptr);
              InsertLocalObject(object);
            }

            Person *person = dynamic_cast<Person *>(object);
            if (person)
            {
              if (person == GWorld->GetRealPlayer())
              {
#if LOG_TEAM_SWITCH
                LogF("Remote player of %s set to %d (owner change reaction)", cc_cast(person->GetDebugName()), _playerDpid);
#endif
                person->SetRemotePlayer(_playerDpid);
              }
              else if (co._disconnected) // only player person is marked as disconnected now, but it should be enough
              {
#if LOG_TEAM_SWITCH
                LogF("Remote player of %s set to %d (owner change reaction)", cc_cast(person->GetDebugName()), AI_PLAYER);
#endif
                person->SetRemotePlayer(AI_PLAYER);
              }
            } // person
            else
            {
            }
          }
          else
          {
            LogF("Client: Remote object %d:%" OID_FORMAT" not found", co._creator.CreatorInt(), co._id);
          }
        }
        else
        {
          // object becomes remote
#if CHECK_MSG
          CheckLocalObjects();
#endif
          // send an acknowledgment to server
          OwnerChangedMessage ackMsg;
          ackMsg._creator = co._creator;
          ackMsg._id = co._id;
          SendMsg(&ackMsg, NMFGuaranteed);

#if CHECK_MSG
          CheckLocalObjects();
#endif
          NetworkLocalObjectInfo info;

          if (
            _playerDpid==BOT_CLIENT && _localObjects.get(id,info) && info.object
            || _localObjects.removeKey(id,&info) && info.object
          )
          {
            NetworkObject *object = info.object;

            object->SetLocal(false);
            // add to remote objects
            NetworkRemoteObjectInfo info;
            if (_remoteObjects.get(id,info))
            {
              info.remote = true;
              _remoteObjects.put(info);
            }
          }
          else
          {
            RptF("Client: Local object %d:%" OID_FORMAT" not found", co._creator.CreatorInt(), co._id);
          }
        }
      }
      break;
    case NMTPlaySound:
      {
        PlaySoundMessage sound;
        TMSGCHECK(sound.TransferMsg(ctx))

        AbstractWave *wave = GSoundScene->OpenAndPlayOnce(
          sound._name,
          NULL,false,
          sound._position, sound._speed,
          sound._volume, sound._frequency
        );
        if (wave)
        {
          GSoundScene->AddSound(wave);

          for (int i=0; i<_receivedSounds.Size();)
          {
            PlaySoundInfo &info = _receivedSounds[i];
            if (info.wave) i++;
            else _receivedSounds.Delete(i);
          }

          int index = _receivedSounds.Add();
          _receivedSounds[index].creator = sound._creator;
          _receivedSounds[index].id = sound._soundId;
          _receivedSounds[index].wave = wave;
        }
      }
      break;
    case NMTSoundState:
      {
        SoundStateMessage sound;
        TMSGCHECK(sound.TransferMsg(ctx))

        for (int i=0; i<_receivedSounds.Size();)
        {
          PlaySoundInfo &info = _receivedSounds[i];
          if (!info.wave)
          {
            _receivedSounds.Delete(i);
            continue;
          }
          if (info.creator == sound._creator && info.id == sound._soundId)
          {
            switch (sound._state)
            {
            case SSRestart:
              info.wave->Restart();
              break;
            case SSStop:
              GSoundScene->DeleteSound(info.wave);
              // remove wave - it may never be started again
              // info.wave is link and should be NULL now
              Assert( !info.wave);
              break;
            case SSRepeat:
              info.wave->Repeat(1000);
              break;
            case SSLastLoop:
              info.wave->LastLoop();
              // remove wave - it may never be started again
              info.wave = NULL;
              break;
            default:
              Fail("Sound state");
              break;
            }
          }
          i++;
        }
      }
      break;
    case NMTSpawnForPlayer:
      {
        SpawnForPlayerMessage spawnMsg;
        TMSGCHECK(spawnMsg.TransferMsg(ctx))


        GameValue args;
        {
          bool ok = ReadValue(args, spawnMsg._value.Data(), spawnMsg._value.Size(), _parent);
          if (!ok || args.GetNil() || args.GetType()!=GameArray && args.GetType()!=GameObject)
          {
            args = GameValueExt(GWorld->PlayerOn());
          }
        }

        //GameVarSpace local(state->GetContext(), false);
        //state->BeginContext(&local);
        // TODO: check signature
        if (!_initScriptRun && !IsBotClient())
        {
          // the code typically calls functions compiled by the mission init script
          DelayedSpawnForPlayer &item = _delayedSpawns.Append();
          item._code = spawnMsg._code;
          item._args = args;
          RptF("spawnForClient code delayed until mission init script runs");
          break;
        }
        RunSpawnedCode(spawnMsg._code, args);
        //state->EndContext();
      }
      break;
    case NMTSynchronizeVariable:
      {
        SynchronizeVariableMessage syncMsg;
        TMSGCHECK(syncMsg.TransferMsg(ctx))
        if (syncMsg._veh)
        {
          GameState *state = GWorld->GetGameState();
          Ref<GameDataCode> code = new GameDataCode(state,syncMsg._handler, GWorld->GetMissionNamespace());
          syncMsg._veh->OnSynchronizeVariable(syncMsg._name,syncMsg._coef,*code);
        }
      }
      break;
    case NMTPublicVariableTo:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
        PublicVariableToMessage varMsg;
        TMSGCHECK(varMsg.TransferMsg(ctx))

        RString name = varMsg._name;

        GameState *gstate = GWorld->GetGameState();

        GameValue value;
        if (ReadValue(value, varMsg._value.Data(), varMsg._value.Size(), _parent))
        {
          // set the variable
          const PublicVariableEventHandler &handler = _publicVariableEventHandlers[name];

          if (IsBotClient() && _publicVariableEventHandlers.IsNull(handler))
          { // server variable change not allowed when there is no handler
            RptF("publicVariable %s ignored, no handler installed",cc_cast(name));
            break;
          }

          GWorld->GetGameState()->VarSet(name, value, false, false, GWorld->GetMissionNamespace()); // mission namespace

          // invoke the event handler
          if (_publicVariableEventHandlers.NotNull(handler))
          {
            GameValue arguments = gstate->CreateGameValue(GameArray);
            GameArrayType &array = arguments;
            array.Add(GameValue(name));
            array.Add(value);

            GameVarSpace local(gstate->GetContext(), false);
            int gshandle = gstate->BeginContext(&local);
            gstate->VarSetLocal("_this", arguments, true);
#if USE_PRECOMPILATION
            gstate->Evaluate(handler._code->GetString(), handler._code->GetCode(), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
#else
            gstate->EvaluateMultiple(handler._code, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
#endif
            gstate->EndContext(gshandle);
          }
        }
      }
      break;
#if _VBS2 //Convoy trainer
    case NMTLoadIsland:
      {
        LoadIslandMessage act;
        TMSGCHECK(act.TransferMsg(ctx))

        // get the client to load the current island
        GWorld->SwitchLandscape(act._islandName);
      }
      break;
    case NMTAddMPReport:
      {
        AddMPReportMessage act;
        TMSGCHECK(act.TransferMsg(ctx))

        switch(act._type)
        {
        case AddHeader:
          GStats.VBSAddHeader(act._myString);
          break;
        case AddEvent:
          GStats.VBSAddEvent(act._myString);
          break;
        case AddFooter:
          GStats.VBSAddFooter(act._myString);
          break;
        }
      }
      break;
 #if _EXT_CTRL
    case NMTAARDoUpdate:
      {
        AARDoUpdateMessage act;
        TMSGCHECK(act.TransferMsg(ctx))
        
        switch(act._type)
        {
          case StartRecord:
            GAAR.RemoteRecord();
            break;
          case StopRecord:
            GAAR.RemoteRecordStop();
            // so something regarding stoping the record
            break;
          case SaveFile:
            GAAR.RemoteRecordSave(act._stringOne); // not used on remote clients
            break;
          case SaveState:
            GAAR.SetRemoteSaveState(act._intType);
            break;
          case AddBookMK:
            GAAR.RemoteAddBookMark(act._floatType,act._stringOne,act._stringTwo);
            break;
          case RemBookMK:
            GAAR.RemoteRemoveBookMark(act._stringOne);
            break;
          default:
            Assert(false);// message type not handeled, development bug
        }
      }
      break;
  #endif
#endif
    case NMTAskForSetItemShortcut :
      {
        AskForSetItemShortcutMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._person)
        {
          Ref<InventoryItemType> type;
          if (!ask._itemType.IsEmpty()) type = dynamic_cast<InventoryItemType *>(VehicleTypes.New(ask._itemType));
          if (ask._item && ask._person->FindItem(ask._item))
            ask._person->SetItemShortcutInstance(ask._index, ask._item);
          else
            ask._person->SetItemShortcut(ask._index, type);
        }
      }
      break;
		case NMTAskForThrowItem:
#if _ENABLE_PHYSICS
      {
				AskForThrowItemMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))

				Man *owner = ask._person;
        InventoryItem *item = ask._item; 
				
				if (owner && item && GetNetworkManager().IsPhysicsEnabled())
		    {
					Vector3 dir = ask._dir;
					float force = dir.Normalize();

					Matrix4 transform;
					transform.SetUpAndDirection(VUp, dir);
					transform.SetPosition(ask._pos);
					item->SetTransform(transform);

					ask._person->ResetItemInHands();
					Object* obj;
					Entity *parent = item->GetHierarchyParent();
					if (parent) 
						parent->RemoveOwnedItem(item);

					//creating before pushing to the world avoid creation of static phys representation
					item->CreateDynamicPhysics(EPL_BUILDING);	
					item->ItemMoveIn(); 
					
					if(item->GetPhysics())
					{
						item->ItemDropPhysicaly(dir, force);
					}
				}
      }
#endif
      break;
    case NMTAskForDropItem:
      {
        AskForDropItemMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        Person *owner = ask._person;
        InventoryItem *item = ask._item; 
        if (owner && item)
        {
          Entity *parent = item->GetHierarchyParent();
          if (parent) parent->RemoveOwnedItem(item);

          // remove shortcut
          owner->RemoveItemShortcut(item);

          // dropped item nor any contained item cannot stay in hands
          if (owner)
          {
            const InventoryItem *inHands = owner->GetItemInHands();
            if (inHands)
            {
              if (inHands == item) owner->RemoveItemInHands();
              else item->ForEachItem([&owner, &inHands](const InventoryItem *child) -> bool
              {
                if (child == inHands)
                {
                  owner->RemoveItemInHands();
                  return true;
                }
                return false;
              });
            }
          }

          Vector3Val pos = owner->FutureVisualState().Position();
          Vector3Val dir = owner->FutureVisualState().Direction();
          item->ItemDrop(owner, pos, dir);

          // The item is now a world object. Remote clients need both:
          // - a generic update, which carries the inLandscape flag and makes
          //   the client call ItemMoveIn() (add the item to the landscape);
          // - a position update, because the generic item update does NOT
          //   carry the transform. Without it the client keeps the item at
          //   its old inventory position and it looks like it disappeared.
          UpdateObject(item, NMCUpdateGeneric, NMFGuaranteed);
          UpdateObject(item, NMCUpdatePosition, NMFGuaranteed);
          if (owner) UpdateObject(owner, NMCUpdateGeneric, NMFGuaranteed);
        }
      }
      break;
    case NMTAskForMoveItem:
      {
        AskForMoveItemMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._target && ask._item)
        {
          const bool oldRotation = ask._item->IsInventoryRotated();
          ask._item->SetInventoryRotated(ask._rotated);
          if (ask._target->CanMoveItem(ask._index, ask._item))
          {
            Entity *parent = ask._item->GetHierarchyParent();
            Person *handsOwner = dyn_cast<Person>(parent);
            const bool movedFromHands = handsOwner && handsOwner->GetItemInHands() == ask._item;
            if (parent) parent->RemoveOwnedItem(ask._item);

            ask._target->MoveItem(ask._index, ask._item);
            if (movedFromHands)
              UpdateObject(handsOwner, NMCUpdateGeneric, NMFGuaranteed);
            if (oldRotation != ask._rotated)
              UpdateObject(ask._item, NMCUpdateGeneric, NMFGuaranteed);
          }
          else
            ask._item->SetInventoryRotated(oldRotation);
        }
      }
      break;
    case NMTAskForRotateInventoryItem:
      {
        AskForRotateInventoryItemMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (GetNetworkManager().IsServerOwner() && ask._person && ask._item &&
            ask._person->FindItem(ask._item) && ask._item->RotateInInventory())
          UpdateObject(ask._item, NMCUpdateGeneric, NMFGuaranteed);
      }
      break;
    case NMTAskForTakeItemToHands:
      {
        AskForTakeItemToHandsMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._person && ask._item &&
            (ask._person->FindItem(ask._item) || ask._item->IsInLandscape()))
        {
          ask._person->TakeItemToHands(ask._item);
        }
      }
      break;
    case NMTAskForSwapItems:
      {
        AskForSwapItemsMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._item1 && ask._item2 && ask._owner)
        {
          bool SwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner);
          SwapItems(ask._item1, ask._item2, ask._owner);
        }
      }
      break;
    case NMTSendAUMsg:
      {
        SendAUMsgMessage varMsg;
        TMSGCHECK(varMsg.TransferMsg(ctx))

        GameValue value;
        if (ReadValue(value, varMsg._value.Data(), varMsg._value.Size(), _parent))
        {
          const GameArrayType &array=value;
          if ( array.Size() == 2 )
          {
            RString command = array[1];
            extern void ProcessAUCommand(RString command);
            ProcessAUCommand(command);
          }
        }
      }
      break;
    case NMTApplyWeather:
      {
        ApplyWeatherMessage act;
        TMSGCHECK(act.TransferMsg(ctx))

        GLandscape->ApplyWeatherMsg(act);
      }
      break;
#if _VBS3 // respawn command
    case NMTPauseSimulation:
      {
        PauseSimulationMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
      
        // Finaly pause the local simulation.
        GWorld->PauseSimulation(msg._pause);
      }
      break;
    case NMTAskCommanderOverride:
      {
        AskCommanderOverrideMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._turret)
        {
          Assert(ask._turret->IsLocal());
          Turret* overriddenByTurret = ask._overriddenByTurret;
          ask._turret->_overriddenByTurret = overriddenByTurret;
        }
        else
        {
          Fail("Error: ask._turret was NULL");
        }
      }
      break;
#endif
#if _ENABLE_ATTACHED_OBJECTS
    case NMTAttachObject:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect

        AttachObjectMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        if (msg._obj) msg._obj->AttachTo(msg._attachTo, msg._pos, msg._memIndex, msg._flags);
      }
      break;
    case NMTDetachObject:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect

        DetachObjectMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        if (msg._obj) msg._obj->Detach();
      }
      break;
#endif
    case NMTAskForAnimationPhase:
      {
        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect

        AskForAnimationPhaseMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
#if _VBS3
        float preValue = 0.0f;
        if(ask._vehicle)
          preValue = ask._vehicle->GetAnimationPhase(ask._vehicle->FutureVisualState(), ask._animation);
#endif
        if (ask._vehicle) ask._vehicle->SetAnimationPhase(ask._animation, ask._phase);
      }
      break;
    case NMTInventoryMenu:
      {
        PREPARE_TRANSFER(InventoryMenu)
        InventoryMenuMessage reply;
        TMSGCHECK(reply.TransferMsg(ctx))
        GWorld->ShowInventoryMenu(reply);
      }
      break;
    case NMTChat:
      {
        ChatMessage chat;
        TMSGCHECK(chat.TransferMsg(ctx))
        if (chat._channel<0 || chat._channel>=CCN)
        {
          RptF("NMTChat received bad channel %d",chat._channel);
          break;
        }
        chat._text = chat._text.Substring(0, MaxChatLength);

        if (chat._sender)
          GChatList.Add((ChatChannel)chat._channel, chat._sender->Brain(), chat._text, false, true);
        else
          GChatList.Add((ChatChannel)chat._channel, chat._name, chat._text, false, true);
        if (_chatSound.name.GetLength() > 0)
        {
          AbstractWave *wave = GSoundScene->OpenAndPlayOnce2D
          (
            _chatSound.name, _chatSound.vol, _chatSound.freq, false
          );
          if (wave)
          {
            wave->SetKind(WaveMusic); // UI sounds considered music???
            wave->SetSticky(true); // enable chat sounds in the lobby
            GSoundScene->AddSound(wave);
          }
        }
      }
      break;
    case NMTLocalizedChat:
      {
        LocalizedChatMessage lChat;
        TMSGCHECK(lChat.TransferMsg(ctx))
        if (lChat._channel<0 || lChat._channel>=CCN)
        {
          RptF("NMTLocalizedChat received bad channel %d",lChat._channel);
          break;
        }
        RString text = LocalizedFormatedStringTemp(lChat._args).GetLocalizedValue();
        if (lChat._sender)
          GChatList.Add((ChatChannel)lChat._channel, lChat._sender->Brain(), text, false, true);
        else
          GChatList.Add((ChatChannel)lChat._channel, lChat._name, text, false, true);
        if (_chatSound.name.GetLength() > 0)
        {
          AbstractWave *wave = GSoundScene->OpenAndPlayOnce2D
          (
            _chatSound.name, _chatSound.vol, _chatSound.freq, false
          );
          if (wave)
          {
            wave->SetKind(WaveMusic); // UI sounds considered music???
            wave->SetSticky(true); // enable chat sounds in the lobby
            GSoundScene->AddSound(wave);
          }
        }
      }
      break;
    case NMTRadioChat:
      {
        RadioChatMessage rChat;
        TMSGCHECK(rChat.TransferMsg(ctx))
        if (rChat._channel<0 || rChat._channel>=CCN)
        {
          RptF("NMTRadioChat received bad channel %d",rChat._channel);
          break;
        }
        // if (!chat.sender) break;
        Person *soldier = GWorld->GetRealPlayer();
        if (!soldier) break;
        if (rChat._channel != CCGlobal && !FindUnit(GWorld->GetRealPlayer(), rChat._units)) break;
        // TODO: playerMsg
        GChatList.Add((ChatChannel)rChat._channel, rChat._sender->Brain(), rChat._text, false, false);
        if (rChat._sender && rChat._sentence.Size() > 0)
        {
          AIBrain *unit = soldier->Brain();
          if (!unit) break;
          RadioChannel *channel = FindChannel(unit, rChat._channel);
          if (channel)
          {
            Person *who = NULL;
            if (channel->GetParams().sound3D) who = rChat._sender;
            rChat._sentence.wordsClass = rChat._wordsClass;
            rChat._sentence.Say(who, channel, rChat._sender->Brain()->GetSpeaker());
          }
        }
      }
      break;
    case NMTRadioChatWave:
      {
        RadioChatWaveMessage chat;
        TMSGCHECK(chat.TransferMsg(ctx))
        if (chat._channel<0 || chat._channel>=CCN)
        {
          RptF("NMTRadioChatWave received bad channel %d",chat._channel);
          break;
        }
        Person *soldier = GWorld->GetRealPlayer();
        if (!soldier) break;
        if (chat._channel != CCGlobal && !FindUnit(GWorld->GetRealPlayer(), chat._units)) break;
        if (chat._wave.GetLength() > 0)
        {
          AIBrain *unit = soldier->Brain();
          if (!unit) break;
          RadioChannel *channel = FindChannel(unit, chat._channel);
          if (channel)
          {
            RString player;
            if (chat._wave[0] == '#' && chat._sender)
            {
              player = chat._sender->GetPersonName();
            }
            channel->Say(chat._wave, chat._sender->Brain(), chat._senderName, player, 2.0);
          }
        }
      }
      break;
    case NMTCreateRadioChannel:
      {
        CreateRadioChannelMessage crcmsg;
        crcmsg.TransferMsg(ctx);
        if (crcmsg._channel>=CCCustom1 && crcmsg._channel<=CCCustomLast)
        {
          CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(crcmsg._channel);
          info.callSign = crcmsg._callSign;
          info.label = crcmsg._label;
          info.units = crcmsg._units;
          PackedColor col(crcmsg._color);
          GChatList.SetColor(crcmsg._channel, col);
          info.color = col;
          info.active = true;
          // make the radio channel audible
          GWorld->GetCustomRadio(crcmsg._channel-CCCustom1).SetAudible(true);
        }
      }
      break;
    case NMTRadioChannelCallSign:
      {
        RadioChannelCallSignMessage crcmsg;
        crcmsg.TransferMsg(ctx);
        if (crcmsg._channel>=CCCustom1 && crcmsg._channel<=CCCustomLast)
        {
          CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(crcmsg._channel);
          info.callSign = crcmsg._callSign;
        }
      }
      break;
    case NMTRadioChannelLabel:
      {
        RadioChannelLabelMessage crcmsg;
        crcmsg.TransferMsg(ctx);
        if (crcmsg._channel>=CCCustom1 && crcmsg._channel<=CCCustomLast)
        {
          CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(crcmsg._channel);
          info.label = crcmsg._label;
        }
      }
      break;
    case NMTRadioChannelUnits:
      {
        RadioChannelUnitsMessage crcmsg;
        crcmsg.TransferMsg(ctx);
        if (crcmsg._channel>=CCCustom1 && crcmsg._channel<=CCCustomLast)
        {
          CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(crcmsg._channel);
          info.units = crcmsg._units;
          // It can happen the player talks on this custom channel, but now she is not allowed. Check it.
          GWorld->CheckChatChannel();
        }
      }
      break;
    case NMTSetSpeaker:
      {
        Fail("Network message 'SetSpeaker' not implemented!");
      }
      break;
    case NMTServerState:
      {
        ServerStateMessage state;
        TMSGCHECK(state.TransferMsg(ctx))
        OnServerStateChanged((NetworkServerState)state._serverState);
      }
      break;
    case NMTServerTimeout:
      {
        ServerTimeoutMessage timeout;
        TMSGCHECK(timeout.TransferMsg(ctx))
        _serverTimeout = timeout._timeout;
      }
      break;
    case NMTServerInfo:
      {
        ServerInfoMessage serverInfo;
        TMSGCHECK(serverInfo.TransferMsg(ctx))
        _serverName = serverInfo._hostname;
        // store the into user profile
        GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
        ParamFile cfg;
        bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
        void SaveUserParams(ParamFile &cfg);
        if (!_serverName.IsEmpty() && ParseUserParams(cfg, &globals))
        {
          cfg.Add("lastMPServerName", _serverName);
          SaveUserParams(cfg);
        }
      }
      break;
    case NMTDeleteObject:
      if ( !CanReceiveUpdateMessages() ) 
        break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect
      {
        DeleteObjectMessage dom;
        TMSGCHECK(dom.TransferMsg(ctx))
        NetworkId id(dom._creator, dom._id);
        if (DebugMeBoy(id))
        {
          auto oInfo = GetRemoteObjectInfo(id);
          if (oInfo && oInfo->object)
          {
            LogF("Deleted %d:%" OID_FORMAT": %s",id.creator.CreatorInt(),id.id,cc_cast(oInfo->object->GetDebugName()));
          }
        }

        DestroyRemoteObject(id);
        // the object can be listed both as remote and local now
        if (_localObjects.removeKey(id))
        {
          if (DebugMeBoy(id))
          {
            LogF("Deleted %d:%" OID_FORMAT": local object",id.creator.CreatorInt(),id.id);
          }
          // send confirmation the object was deleted
          DeletedObjectMessage reply;
          reply._creator = dom._creator;
          reply._id = dom._id;
          SendMsg(&reply, NMFGuaranteed);
        }
      }
      break;
    case NMTCreateObject:
      CreateRemoteObject<Object>(ctx);
      break;
    case NMTCreateVehicle:
      CreateRemoteObject<Vehicle>(ctx);
      break;
    case NMTCreateVehicleAI:
      CreateRemoteObject<EntityAI>(ctx);
      break;
    case NMTCreateVehicleBrain:
      CreateRemoteObject<Person>(ctx);
      break;
    case NMTCreateShot:
      CreateRemoteObject<Shot>(ctx);
      break;
    case NMTCreateCrater:
      CreateRemoteObject<Crater>(ctx);
      break;
    case NMTCreateCraterOnVehicle:
      CreateRemoteObject<CraterOnVehicle>(ctx);
      break;
    case NMTCreateObjectDestructed:
      CreateRemoteObject<ObjectDestructed>(ctx);
      break;
    case NMTCreateDynSoundSource:
      CreateRemoteObject<DynSoundSource>(ctx);
      break;
    case NMTCreateSoundOnVehicle:
      CreateRemoteObject<SoundOnVehicle>(ctx);
      break;
    case NMTCreateHelicopter:
      CreateRemoteObject<HelicopterAuto>(ctx);
      break;
#if _HELISIM_ROTORLIB
    case NMTCreateHelicopterRTD:
      CreateRemoteObject<HelicopterRTD>(ctx);
      break;
#endif
    case NMTCreateSeagull:
      CreateRemoteObject<SeaGullAuto>(ctx);
      break;
    case NMTCreateTurret:
      CreateRemoteObject<Turret>(ctx);
      break;
    case NMTCreateDestructionEffects:
      CreateRemoteObject<DestructionEffects>(ctx);
      break;
    case NMTCreateClientCameraPosition:
      CreateRemoteObject<ClientCameraPositionObject>(ctx);
      break;
    case NMTUpdateDamageObject:
    case NMTUpdateDamageVehicle:
    case NMTUpdateDamageVehicleAI:
      ctx.SetClass(NMCUpdateDamage);
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
    case NMTUpdatePositionTurret:
    case NMTUpdatePositionMagazine:
      ctx.SetClass(NMCUpdatePosition);
      // continue with update
    case NMTUpdateObject:
    case NMTUpdateVehicle:
    case NMTUpdateFlag:
    case NMTUpdateShot:
    case NMTUpdateMine:
    case NMTUpdateWeapon:
    case NMTUpdateVehicleAI:
    case NMTUpdateVehicleAIFull:
    case NMTUpdateVehicleBrain:
    case NMTUpdateTransport:
    case NMTUpdateMan:
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
    case NMTUpdateMotorcycle:
    case NMTUpdateFireplace:
    case NMTUpdateTurret:
    case NMTUpdateClientCameraPosition:
    case NMTUpdateInventoryItem:
    case NMTUpdateItemRadio:
    case NMTUpdateItemTransmitter:
    Update:
      {
        // find IndicesNetworkObject level
        PREPARE_TRANSFER(NetworkObject)


        NetworkId id;
        TRANSF_BASE(objectCreator, id.creator);
        TRANSF_BASE(objectId, id.id);

        if (_clientState < NCSMissionReceived)
        {
          LogF("Update of %d:%" OID_FORMAT" ignored, state %s", id.creator.CreatorInt(), id.id, (const char *)FindEnumName(_clientState));
          break; // updates from the last session
        }

        if ( !CanReceiveUpdateMessages() ) 
          break;  // client is unable to receive Update messages, mostly due to being in NCSGameFinished state after disconnect

        // update the transfer progress
        if (_clientState == NCSMissionReceived && ctx.GetClass() == NMCUpdateGeneric && _remainingToUpdate > 0) _remainingToUpdate--;

        if (_updatesGuaranteed)
        {
          NetworkRemoteObjectInfo *info = _remoteObjects.get(id);
          if (info)
          {
            // diff against the last update received
            NetworkMessage *refMsg = info->updates[ctx.GetClass()];
            if (refMsg && !IsBotClient())
            {
              MessageDiffApply diff;
              message->LoadDiff(diff);
              message->ApplyDiff(refMsg,diff);
              // once applied, diff has no longer any sense
              message->ResetDiff();
            }
#if _ENABLE_REPORT
            if (refMsg && IsBotClient())
            {
              MessageDiffApply diff;
              message->LoadDiff(diff);
              DoAssert(diff.IsIdentity()); // it must be identity, as we are not processing it
              DoAssert(diff.IsEmpty()); // it should be empty, as it was reset when received
            }
#endif
            info->updates[ctx.GetClass()] = msg;
          }
        }

        NetworkObject *object = GetObject(id);
        if (!object)
        {
          RptF
          (
            "Client: Object %d:%" OID_FORMAT" (type %s) not found.",
            id.creator.CreatorInt(), id.id,
            cc_cast(GetMsgTypeName(type))
          );
          break;
        }
        if (object->IsLocal())
        {
          bool allowNonOwner = false;
          if (!_parent->IsServer())
          {
            // bot-client can send updates about anything
            allowNonOwner = true;
          }
          if (!allowNonOwner)
          {
            RptF
            (
              "Client: Object (id %d:%" OID_FORMAT", type %s) is local - update is ignored.",
              id.creator.CreatorInt(), id.id,
              cc_cast(GetMsgTypeName(type))
            );
            break;
          }
          else
          {
            //LogF("Non-owner incoming %d for %s (%" OID_FORMAT ")",ctx.GetClass(),cc_cast(object->GetDebugName()),id.id);
          }
        }
        bool reject = false;
        if (_parent->IsServer() && !object->IsLocal())
        {
          NetworkLocalObjectInfo *info = GetLocalObjectInfo(id);
          if (info)
          {
            reject = object->RejectMsg(ctx.GetClass(),ctx.GetNetworkMessage());
            if (info->updates[ctx.GetClass()].rejected!=reject)
            {
              LogF("Rejected %d incoming %d for %s (%" OID_FORMAT ")",reject,ctx.GetClass(),cc_cast(object->GetDebugName()),id.id);
              info->updates[ctx.GetClass()].rejected = reject;
              GetServer()->OnRejectChanged(id,ctx.GetClass(),reject);
            }
          }
        }

        if (!reject)
        {
          TMSGCHECK(object->TransferMsg(ctx))
        }
        if (DiagLevel >= 4)
          DiagLogF("Client: object %d:%" OID_FORMAT" updated", id.creator.CreatorInt(), id.id);

      }
      break;
    case NMTUpdateClientInfo:
      Fail("Unexpected on client");
      break;
    case NMTUpdateClientInfoDpid:
    {
      // camera position is necessary for dead players VoN Direct Speech
      ctx.SetClass(NMCUpdatePosition);
      ClientInfoObject clientInfo;
      TMSGCHECK(clientInfo.TransferMsg(ctx))

      GetNetworkManager().SetVoNPosition(clientInfo._dpnid, clientInfo._cameraPosition, VZero);
      break;
    }
    case NMTPlayerUpdate:
    {
      ctx.SetClass(NMCUpdatePosition);
      //Assert(ctx.GetClass()==NMCUpdatePosition);
      PREPARE_TRANSFER(PlayerUpdate)
      // check which identity is this message affecting
      int dpnid;
      if (TRANSF_BASE(dpnid, dpnid)==TMOK)
      {
        // find corresponding identity
        PlayerIdentity *pi = FindIdentity(dpnid);
        if (pi)
        {
          // update identity
          TMSGCHECK(pi->TransferMsg(ctx))
        }
      }
      break;
    }
    case NMTMissionParams:
      {
        //RptF("*** Client: NMTMissionParams");

        MissionParamsMessage params;
        TMSGCHECK(params.TransferMsg(ctx))

        _param1 = params._param1;
        _param2 = params._param2;
        _paramsArray = params._paramsArray;

        if (!params._updateOnly)
        {
          // whole mission info received, ask for mission file
          AskMissionFileMessage ask;
          ask._valid = _missionFileValid;
          SendMsg(&ask, NMFGuaranteed);

          SetClientState(NCSMissionAsked);
        }
        // else - update of mission params in Multiplayer Setup Display
      }
      break;
    case NMTSubscribeRadios:
      {
        SubscribeRadiosMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        bool playerExists = false;
        for (int i=0; i<_identities.Size(); i++)
        {
          if (_identities[i].dpnid == msg._player) { playerExists=true; break;}
        }
        if (playerExists)
        {
          _radioSubscribers.AddUnique(msg._player, msg._frequences);
        }
      }
      break;
    case NMTStatusChat:
      {
        StatusChatMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        GChatList.Add(msg._text, msg._color);
      }
      break;
    case NMTForceDeleteObject:
      {
        ForceDeleteObjectMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        if (ask._object)
        {
          NetworkId nid=ask._object->GetNetworkId();
          DeleteObject(nid, false); // do not resend message
          ask._object->DestroyObject();
        }
      }
      break;
    case NMTVoiceOn:
#if _GAMES_FOR_WINDOWS
      {
        VoiceOnMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        PlayerIdentity *identity = FindIdentity(ask._dpnid);
        if (identity) identity->voiceOn = ask._voiceOn;
      }
#endif
      break;
    case NMTCleanupPlayer:
      {
        CleanupPlayerMessage state;
        TMSGCHECK(state.TransferMsg(ctx))

        NetworkId person(state._creator, state._id);
        CleanupPlayer(state._dpnid, person);
      }
      break;
    case NMTAcceptedKey:
      {
        int index = _acceptedKeys.Add();
        AcceptedKeyMessage ask(_acceptedKeys[index]);
        TMError err = ask.TransferMsg(ctx); 
        if (err != TMOK && err != TMNotFound) 
        {
          _acceptedKeys.Delete(index, 1);
          break;
      }
      }
      break;
    case NMTAdditionalSignedFiles:
      {
        // TODO: protect _additionalSignedFiles against manipulation by hacker
        AdditionalSignedFilesMessage files(_additionalSignedFiles);
        TMSGCHECK(files.TransferMsg(ctx))
      }
      break;
    case NMTDataSignatureAsk:
      {
        DataSignatureAskMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        /*
        if (IsDedicatedClient())
        {
          ConsoleF("Data signature ask %d level %d",ask._index, ask._level);
        }
        */
        // indices of banks with configs (only these need to be checked)
        AUTO_STATIC_ARRAY(int, indices, 32);
        BankList::ReadAccess banks(GFileBanks);
        for (int i=0; i<banks.Size(); i++)
        {
          if (banks[i].VerifySignature()) indices.Add(i);
        }
        int nBanks = indices.Size(); // banks
        int nFiles = _additionalSignedFiles.Size(); // additional files

        if (ask._index < 0)
        {
          // GDebugger.PauseCheckingAlive();
          Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_CREATE_CLIENT));
          // check all files
          AutoArray<RString> wrongFiles;
          // check the banks
          for (int i=0; i<nBanks; i++)
          {
            ProgressRefresh();
            QFBank &bank = banks[indices[i]];
            if (!CheckDataSignature(ask._index, ask._level, bank, indices[i]))
            {
              wrongFiles.Add(bank.GetOpenName());
            }
          }
          // check the additional file
          for (int i=0; i<nFiles; i++)
          {
            ProgressRefresh();
            RString filename = _additionalSignedFiles[i];
            if (!CheckDataSignature(ask._index, ask._level, filename))
            {
              wrongFiles.Add(filename);
            }
          }
          ProgressFinish(p);
          // GDebugger.ResumeCheckingAlive();
          if (wrongFiles.Size() > 0)
          {
            RString format = LocalizeString(IDS_SIGNATURE_MISSING);
            const int maxFiles = 6;
            RString files = RString("\"") + wrongFiles[0];
            int n = std::min(wrongFiles.Size(), maxFiles);
            for (int i=1; i<n; i++)
            {
              files = files + RString("\", \"") + wrongFiles[i];
            }
            files = files + RString("\"");
            if (wrongFiles.Size() > maxFiles) files = files + RString(", ...");
            WarningMessage(format, cc_cast(files));

            // let server decide if need to disconnect
            for (int i=0; i<wrongFiles.Size(); i++)
            {
              HackedDataMessage msg;
              msg._type = HTUnsignedData;
              msg._filename = wrongFiles[i];
              SendMsg(&msg, NMFGuaranteed);
            }

            ResetErrors();
          }
          else if (ask._level == 1)
          {
            GWorld->CheckHash(-1, CHRMultiplayer);
          }
          // if it is data signatures collecting phase, so send the end packet
          if ( ask._index == -2 )
          {
            //@{ Ant1HaX0R section
            // Send the Loaded Bank Count
            // hidden inside the NMTChat ... channel = ( #signed files xor (dpnid & 0x7fffffff) ) | 0x80000000
            int nSignedBanks = 0;
            for (int i=0; i<banks.Size(); i++) 
            {
              if (banks[i].VerifySignature()) nSignedBanks++;
            }
            int nFiles = _additionalSignedFiles.Size() + nSignedBanks;
            int fakeChannel = ((_playerDpid & 0x7fffffff) ^ nFiles) | 0x80000000;
            GetNetworkManager().Chat((ChatChannel)fakeChannel,"2"); // I know level 2
            //@}

            ask._index = CollectedHashes::CollectEndIndex;
            ProgressFinish(p);
            goto CheckSingleSignature;
          }
        }
        else
        {
CheckSingleSignature:
          // random check
          int i = ask._index % (nBanks + nFiles);
          if (i < nBanks)
          {
            // check the bank
            QFBank &bank = banks[indices[i]];
            if (!CheckDataSignature(ask._index, ask._level, bank, indices[i]))
            {
              HackedDataMessage msg;
              msg._type = HTHackedData;
              msg._filename = bank.GetOpenName();
              SendMsg(&msg, NMFGuaranteed);
            }
            else if (ask._level == 1)
            {
              GWorld->CheckHash(indices[i], CHRMultiplayer);
            }
          }
          else
          {
            // check the additional file
            RString filename = _additionalSignedFiles[i - nBanks];
            if (!CheckDataSignature(ask._index, ask._level, filename))
            {
              HackedDataMessage msg;
              msg._type = HTHackedData;
              msg._filename = filename;
              SendMsg(&msg, NMFGuaranteed);
            }
          }
        }
      }
      break;
    case NMTFileSignatureAsk:
      {
        FileSignatureAskMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        /*
        if (IsDedicatedClient())
        {
          ConsoleF("File signature ask '%s' level %d",cc_cast(ask._file), ask._level);
        }
        */
        // check the bank
#if _ENABLE_REPORT
        LogF("FileSignatureCheck: %s", cc_cast(ask._file));
#endif
        int bankIndex = BankIndex(ask._file);
        if ( bankIndex>=0 )
        {
          if (!CheckFileSignature( bankIndex, ask._level, ask._file))
          {
            HackedDataMessage msg;
            msg._type = HTHackedData;
            msg._filename = ask._file;
            SendMsg(&msg, NMFGuaranteed);
          }
          else if (ask._level == 1)
          {
            GWorld->CheckHash(bankIndex, CHRMultiplayer);
          }
        }
        else
        { 
          HackedDataMessage msg;
          msg._type = HTHackedData;
          msg._filename = ask._file;
          SendMsg(&msg, NMFGuaranteed);
        }
      }
      break;
    case NMTCDKeyCheckAsk:
#if _ENABLE_GAMESPY && _VERIFY_KEY
      {
        CDKeyCheckAskMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        
        RString GetCDKey();
        RString cdKey = GetCDKey();
        char response[RESPONSE_SIZE];
        gcd_compute_response((char *)cc_cast(cdKey), (char *)cc_cast(ask._challenge), response, CDResponseMethod_NEWAUTH);

        CDKeyCheckAnswerMessage answer;
        answer._response = response;
        SendMsg(&answer, NMFGuaranteed);
      }
#endif
      break;
    case NMTCDKeyRecheckAsk:
#if _ENABLE_GAMESPY && _VERIFY_KEY
      {
        CDKeyRecheckAskMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))

        RString GetCDKey();
        RString cdKey = GetCDKey();
        char response[RESPONSE_SIZE];
        gcd_compute_response((char *)cc_cast(cdKey), (char *)cc_cast(ask._challenge), response, CDResponseMethod_REAUTH);

        CDKeyRecheckAnswerMessage answer;
        answer._hint = ask._hint;
        answer._response = response;
        SendMsg(&answer, NMFGuaranteed);
      }
#endif
      break;
    case NMTConnectVoiceDirect:
      {
        ConnectVoiceDirectMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        // fill in some diagnostics first
#if _ENABLE_CHEATS
        if (GInput.cheatVoNServer && ask._dpnid != BOT_CLIENT) break; //force retranslation through server
        if (GInput.cheatVoNRetranslate && ask._dpnid == BOT_CLIENT) break; //force retranslation through gameSocket
#endif
        // Connect voice
        sockaddr_in addr;
        addr.sin_port = htons(ask._port);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ask._addr;
        memset(&(addr.sin_zero), '\0', 8); // zero the rest of the struct
#     if BETA_TESTERS_DEBUG_VON
        RptF("VoNLog,Client: directMsg addr=%d.%d.%d.%d port=%d id=%x", 
          (unsigned)IP4(addr),(unsigned)IP3(addr),(unsigned)IP2(addr),(unsigned)IP1(addr),
          ask._port, ask._dpnid
          );
#     endif
        ConnectVoice(ask._dpnid, addr);
      }
      break;
    case NMTConnectVoiceNatNeg:
      break;
    case NMTDisconnect:
      {
        DisconnectMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        GChatList.Add(CCSystem, NULL, ask._message, false, true);
        WarningMessage(ask._message);

        RemoveUserMessages();
        _clientState = NCSNone;
        _serverState = NSSNone;
      }
      break;
    case NMTMissionStats:
      {
        MissionStatsMessage info;
        TMSGCHECK(info.TransferMsg(ctx))
        _totalToCreate = _remainingToCreate = info._toCreate;
        _totalToUpdate = _remainingToUpdate = info._toUpdate;
      }
      break;
#if _GAMES_FOR_WINDOWS
    case NMTAskForArbitration:
      {
        AskForArbitrationMessage ask;
        TMSGCHECK(ask.TransferMsg(ctx))
        _parent->ArbitrationRegister(ask._sessionNonce);
      }
      break;
#endif
    case NMTServerTime:
      {
        ServerTimeMessage stmsg;
        TMSGCHECK(stmsg.TransferMsg(ctx))
        _serverTime = stmsg._time;
        _serverTimeOfEstimatedEnd = stmsg._endTime;
        _serverTimeInitTickCount = ::GetTickCount()-_serverTime; //we want _serverTime to match ::GetTickCount()-_serverTimeInitTickCount
      }
      break;
    case NMTServerAdmin:
      {
        ServerAdminMessage samsg;
        TMSGCHECK(samsg.TransferMsg(ctx))
        _serverAdmin = samsg._admin;
        _serverGameMaster = samsg._gameMaster;
      }
      break;
    case NMTUseItem:
      {
        UseItemMessage msg;
        TMSGCHECK(msg.TransferMsg(ctx))
        if (msg._person && msg._item) msg._item->UseFromInventory(msg._person);
      }
      break;
    default:
      RptF("Client: Unhandled user message %s", cc_cast(GetMsgTypeName(type)));
      break;
  }
}

bool NetworkClient::CheckDataSignature(int askedIndex, int level, QFBank &bank, int gBankIx)
{
#if _ENABLE_CHEATS
  static bool doIt = true;
  if (doIt && bank.IsPatched()) return false;
#else
  if (bank.IsPatched()) return false;
#endif

  DataSignatureAnswerMessage answer;
  answer._index = askedIndex;
  answer._filename = bank.GetOpenName();
  answer._level = level;
  if (DataSignatures::FindSignature(answer._signature, answer._filename, _acceptedKeys.Data(), _acceptedKeys.Size(), _sigVerRequired))
  {
    if (answer._signature.Version()>1 && level==2)
    { // use async hash computation for level=2 asks
      // Add ask to be completed async
      int ix = _checkSignatureQueue.Add();
      CheckSignatureItem &check = _checkSignatureQueue[ix];
      check._sigVer = answer._signature.Version();
      check._level = level;
      check._bankIndex = gBankIx;
      check._initialTest = (askedIndex == -2);
      // prepare answer msg
      DataSignatureAnswerMessage *answer2 = new DataSignatureAnswerMessage();
      answer2->_signature = answer._signature;
      answer2->_index = askedIndex;
      answer2->_filename = bank.GetOpenName();
      answer2->_level = level;
      check._msg = answer2;
      check._type = NMTDataSignatureAnswer;
      /*
      if (IsDedicatedClient())
      {
        ConsoleF(
          "Queueing signature %d:'%s' level %d, %d pending", check._bankIndex, cc_cast(bank.GetOpenName()),
          check._level, _checkSignatureQueue.Size()
        );
    }
      */

    }
    else
    { // for other asks compute the hash directly
    DoVerify(bank.GetHash<HashCalculator>(answer._hash._content, answer._signature.Version(), level));
    SendMsg(&answer, NMFGuaranteed);
    }
    return true;
  }
  return false;
}

bool NetworkClient::CheckDataSignature(int askedIndex, int level, RString filename)
{
  DataSignatureAnswerMessage answer;
  answer._index = askedIndex;
  answer._filename = filename;
  answer._level = level;
  if (DataSignatures::FindSignature(answer._signature, answer._filename, _acceptedKeys.Data(), _acceptedKeys.Size(), _sigVerRequired))
  {
    DoVerify(DataSignatures::GetHash(answer._hash, answer._filename));
    SendMsg(&answer, NMFGuaranteed);
    return true;
  }
  return false;
}

bool NetworkClient::CheckFileSignature(int askedIndex, int level, RString filename)
{
  BankList::ReadAccess banks(GFileBanks);
  FileSignatureAnswerMessage answer;
  QFBank &bank = banks[askedIndex];
  //RptF("CheckFileSignature: %s-->(%s)", cc_cast(filename), cc_cast(bank.GetPrefix()));
  answer._fileName = filename;
  answer._level = level;
  // get the index matching the DataSignatureAnswers
  int sigIndex = -1;
  for (int i=0; i<=askedIndex; i++)
  {
    if (banks[i].VerifySignature()) sigIndex++;
  }
  answer._index = sigIndex;
  if ( DataSignatures::FindSignature(answer._signature, bank.GetOpenName(), _acceptedKeys.Data(), _acceptedKeys.Size(), _sigVerRequired) )
  {
    Temp<char> dataHash;
    if (!bank.GetHash(dataHash)) 
      return false;
    switch (answer._signature.Version())
    {
    case 1:
      {
        answer._dataToHash.Realloc( dataHash.Size() + bank.GetPrefix().GetLength() );
        char *data = answer._dataToHash.Data();
        memcpy(data, dataHash.Data(), dataHash.Size()); data += dataHash.Size();
        memcpy(data, bank.GetPrefix().Data(), bank.GetPrefix().GetLength());
      }
      break;
    case 2:
      {
        switch (level)
        {
        case 0:
        case 1:
          {
        // combine content hash with hash of pbo file list and pbo prefix
        // the final hash will be: HashOf(storedHash+fileListHash+pboPrefix)
        AutoArray<char> fileListHash;
            if (!bank.GetFileListHash<HashCalculator>(fileListHash))
          return false;
        // concatenate it to create _dataToHash
        answer._dataToHash.Realloc( dataHash.Size() + fileListHash.Size() + bank.GetPrefix().GetLength() );
        char *data = answer._dataToHash.Data();
        memcpy(data, dataHash.Data(), dataHash.Size()); data += dataHash.Size();
        memcpy(data, fileListHash.Data(), fileListHash.Size()); data += fileListHash.Size();
        memcpy(data, bank.GetPrefix().Data(), bank.GetPrefix().GetLength());
      }
      break;
        case 2:
          {
            // use async hash computation for level=2 asks
              // Add ask to be completed async
              int ix = _checkSignatureQueue.Add();
              CheckSignatureItem &check = _checkSignatureQueue[ix];
              check._sigVer = answer._signature.Version();
              check._level = level;
              check._bankIndex = askedIndex;
              check._initialTest = false;
              // prepare answer msg
              FileSignatureAnswerMessage *answer2 = new FileSignatureAnswerMessage();
              answer2->_signature = answer._signature;
              answer2->_index = answer._index;
              answer2->_fileName = answer._fileName;
              answer2->_level = answer._level;
              check._msg = answer2;
              check._type = NMTFileSignatureAnswer;
              return true;
            }
    default:
      return false; //fail
    }
      }
      break;
    default:
      return false; //fail
    }
    SendMsg(&answer, NMFGuaranteed);
    return true;
  }
  return false;
}

/*!
\patch_internal 1.80 Date 8/6/2002 by Jirka
- Fixed: Glob.header.filenameReal now always contain original name of mission (even in MP)
*/

bool NetworkClient::PrepareGame()
{
LogF("Mission %s received", (const char *)_missionHeader.GetLocalizedMissionName());

  _totalToCreate = -1;
  _remainingToCreate = -1;
  _totalToUpdate = -1;
  _remainingToUpdate = -1;

  if (_missionHeader._island.GetLength() == 0)
  {
    RptF("NetworkClient::PrepareGame - no island");
    return true;
  }

  RString fullname = GetWorldName(_missionHeader._island);
  if (!QFBankQueryFunctions::FileExists(fullname))
  {
    // TODO: Warning and error processing
    WarningMessage(LocalizeString(IDS_MSG_NO_WORLD), (const char *)fullname);
    return false;
  }

  SetBaseDirectory(false, "");

  if (_missionHeader._noCopy)
  {
    const char *ext = strrchr(_missionHeader._fileName, '.');
    Assert(ext);
    RString mission(_missionHeader._fileName, ext - cc_cast(_missionHeader._fileName));
    if (_missionHeader._hasMissionParams)
    {
      RString filename = GetMissionCacheDir() + RString("__cur_mp.") + GetExtensionWizardMission();
      SetMission(_missionHeader._island, mission, _missionHeader._fileDir, filename, _missionHeader.GetLocalizedMissionName());
    }
    else
    {
      SetMission(_missionHeader._island, mission, _missionHeader._fileDir);
    }
  }
  else
  {
    if (_missionHeader._hasMissionParams)
    {
      RString filename = GetMissionCacheDir() + RString("__cur_mp.") + GetExtensionWizardMission();

      SetMission(_missionHeader._island, "__cur_mp", MPMissionsDir, filename, _missionHeader.GetLocalizedMissionName());
    }
    else
    {
      SetMission(_missionHeader._island, "__CUR_MP", MPMissionsDir);
      const char *ext = strrchr(_missionHeader._fileName, '.');
      if (ext) Glob.header.filenameReal = _missionHeader._fileName.Substring(0, ext - (const char *)_missionHeader._fileName);
      else Glob.header.filenameReal = _missionHeader._fileName;
    }
  }

  // TODO: different handling for a scripted mission
  ParseMission(true, true);
  LogF("  - parsed");

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeNetware);

  GWorld->SwitchLandscape(_missionHeader._island);

  if (GWorld->UI()) GWorld->UI()->Init();

  GStats.ClearMission();
  CurrentTemplate->ActivateAddons(_missionHeader._island);

  // create markers
  markersMap.Clear();
  void ResetMarkerID();
  ResetMarkerID();

  CurrentTemplate->InitGeneral(GModeNetware);
  const ArcadeIntel &intel = CurrentTemplate->Intel();
  GWorld->SetViewDistanceMin(intel.viewDistance);
  GWorld->SetViewDistanceHard(0);
  GWorld->SetTerrainGridHard(0,false);
  GWorld->AdjustSubdivision(GModeNetware);
  GWorld->InitClient();

  ConstParamEntryPtr enableItemsDropping = ExtParsMission.FindEntry("enableItemsDropping");
  if (enableItemsDropping) GWorld->EnableItemsDropping(*enableItemsDropping);

  CurrentTemplate->AddMarkers();

  ProgressFinish(p);

  void RunInitFunctionsScript();
  RunInitFunctionsScript();
  ProcessDelayedSpawns();

  return true;
}

void NetworkClient::InsertLocalObject(NetworkObject *object)
{
#if CHECK_MSG
  CheckLocalObjects();
#endif

  NetworkLocalObjectInfo localObject;
  localObject.id = object->GetNetworkId();
  localObject.object = object;
  _localObjects.put(localObject);

  if (DiagLevel >= 1)
  {
    NetworkMessageType type = object->GetNMType();
    NetworkId id = object->GetNetworkId();
    DiagLogF("Client: local object created %d:%" OID_FORMAT" (type %s)", id.creator.CreatorInt(), id.id, type == NMTNone ? "None" : cc_cast(GetMsgTypeName(type)));
  }

#if CHECK_MSG
  CheckLocalObjects();
#endif
}

void NetworkClient::InsertRemoteObject( NetworkObject * obj, bool remote )
{
  // add to remote objects
  NetworkRemoteObjectInfo info;
  info.id = obj->GetNetworkId();
  info.object = obj;
  info.remote = remote;
  _remoteObjects.put(info);
}


NetworkId NetworkClient::CreateLocalObject(NetworkObject *object, bool setNetworkId)
{
  Assert(object);

  if (setNetworkId)
  {
    // set network id
    NetworkId id(GetCreatorId(), _nextId++);
    object->SetNetworkId(id);
  }
  NetworkId id = object->GetNetworkId();
  object->SetLocal(true);

  InsertLocalObject(object);
  InsertRemoteObject(object, false);

  return id;
}

bool NetworkClient::CreateObject(NetworkObject *object, bool setNetworkId)
{
  if (!object) return false;
  if (_clientState < NCSMissionReceived)
  {
    RptF("Creation of object %s failed, state %s", (const char *)object->GetDebugName(), (const char *)FindEnumName(_clientState));
    return false;
  }

  NetworkId id = CreateLocalObject(object, setNetworkId);

  return SendCreateMessage(object);
}

bool NetworkClient::SendCreateMessage( NetworkObject * object )
{
  // send message to center
  NetworkMessageType msgType = object->GetNMType(NMCCreate);

  // create message
  Ref<NetworkMessage> msg = ::CreateNetworkMessage(msgType);
  msg->time = Glob.time;
  NetworkMessageContext ctx(msg, this, TO_SERVER, MSG_SEND);
  ctx.SetClass(NMCCreate);


  if (!object->OnSendCreateMessage(ctx)) return false;

  if (object->TransferMsg(ctx) != TMOK) return false;

  // avoid uninitialized message member
  PREPARE_TRANSFER(NetworkObject)
    bool guaranteed = true;
  TRANSF_BASE(guaranteed, guaranteed);

  // send message
  DWORD dwMsgId = SendMsg
    (
    TO_SERVER, msg, msgType, NMFGuaranteed, object
    );

  return dwMsgId != 0xFFFFFFFF;
}


/*!
\patch_internal 1.30 Date 11/5/2001 by Jirka
- Fixed: More robust transfer of guaranteed update messages
\patch 1.35 Date 12/11/2001 by Jirka
- Fixed: Error in updates on dedicated server
*/

int NetworkClient::UpdateObject(NetworkObject *object, NetworkMessageClass cls, NetMsgFlags dwFlags)
{
  if (!object) return -1;
  if (_clientState < NCSMissionReceived) return -1;

  // find local objects
  NetworkId id = object->GetNetworkId();
  if (id.IsNull())
  {
    RptF("Client: Nonnetwork object %x.", object);
    return -1;
  }
#if CHECK_MSG
  CheckLocalObjects();
#endif
  NetworkLocalObjectInfo *localObject = GetLocalObjectInfo(id);
  if (!localObject) return -1;

  NetworkUpdateInfo &info = localObject->updates[cls];

  // send message to center
  NetworkMessageType msgType = object->GetNMType(cls);
  if (msgType == NMTNone) return -1;
  
  // FIX
#if _ENABLE_DEDICATED_SERVER
  if (((msgType == NMTUpdateClientInfo) || (msgType == NMTUpdateClientInfoDpid)) && _parent->GetServer() && IsDedicatedServer()) return -1;
#endif

  // create message
  Ref<NetworkMessage> msg = ::CreateNetworkMessage(msgType);
  // must be Ref - msg is stored
//  msg->id = id;
  msg->time = Glob.time;
//  msg->position = object->GetCurrentPosition();
  NetworkMessageContext ctx(msg, this, TO_SERVER, MSG_SEND);
  ctx.SetClass(cls);
  if ((dwFlags & NMFGuaranteed) != 0) ctx.SetInitialUpdate();
  if (object->TransferMsg(ctx) != TMOK) return -1;

  if (DebugMeBoy(id))
  {
    LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} owner client %d updating", id.creator.CreatorInt(), id.id, _playerDpid);
    
  }
  PREPARE_TRANSFER(NetworkObject)
  bool guaranteed = (dwFlags & NMFGuaranteed) != 0;
  if (TRANSF_BASE(guaranteed, guaranteed) != TMOK)
  {
    Fail("Cannot transfer");
    return -1;
  }

#if CHECK_MSG
  DoAssert(localObject->object == object);
  DoAssert(localObject->id == id);
  if (object)
  {
    NetworkDataTyped<int> *creator = static_cast<NetworkDataTyped<int> *>(msg->values[0].GetRef());
    NetworkDataTyped<int> *id = static_cast<NetworkDataTyped<int> *>(msg->values[1].GetRef());
    LogF
    (
      "Message %x (%d:%" OID_FORMAT") assigned to object %x (%d:%" OID_FORMAT")",
      msg.GetRef(), creator->value, id->value,
      object, object->GetNetworkId().creator.CreatorInt(), object->GetNetworkId().id
    );
  }
#endif

  // send message
  DWORD dwMsgId = SendMsg
  (
    TO_SERVER, msg, msgType, dwFlags, object
  );

  if (dwMsgId == 0xFFFFFFFF) return -1;
  // update local objects info
  info.lastSentMsg = msg;

#if CHECK_MSG
  CheckLocalObjects();
#endif

  return msg->size;
}

void NetworkClient::UpdateObject(NetworkObject *object, NetMsgFlags dwFlags)
{
  if (!object) return;

  // find local objects
  NetworkId id = object->GetNetworkId();
  if (id.IsNull())
  {
    RptF("Client: Nonnetwork object %x.", object);
    return;
  }
#if CHECK_MSG
  CheckLocalObjects();

  if (id.id == 0x24)
    LogF("Here");
#endif

  NetworkLocalObjectInfo *localObject = GetLocalObjectInfo(id);
  if (!localObject) return;

  for (int cls=NMCUpdateFirst; cls<NMCUpdateN; cls++)
  {
    NetworkUpdateInfo &info = localObject->updates[cls];

    // send message to center
    NetworkMessageType msgType = object->GetNMType((NetworkMessageClass)cls);
    if (msgType == NMTNone) continue;
    if (msgType == NMTUpdateClientInfoDpid) continue;

    // create message
    Ref<NetworkMessage> msg = ::CreateNetworkMessage(msgType);
    // must be Ref - msg is stored
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, this, TO_SERVER, MSG_SEND);
    ctx.SetClass((NetworkMessageClass)cls);
    if ((dwFlags & NMFGuaranteed) != 0) ctx.SetInitialUpdate();
    if (object->TransferMsg(ctx) != TMOK) return;

    PREPARE_TRANSFER(NetworkObject)
    bool guaranteed = (dwFlags & NMFGuaranteed) != 0;
    if (TRANSF_BASE(guaranteed, guaranteed) != TMOK)
    {
      Fail("Cannot transfer");
      return;
    }

#if CHECK_MSG
  DoAssert(localObject->object == object);
  DoAssert(localObject->id == id);
  if (object)
  {
    NetworkDataTyped<int> *creator = static_cast<NetworkDataTyped<int> *>(msg->values[0].GetRef());
    NetworkDataTyped<int> *id = static_cast<NetworkDataTyped<int> *>(msg->values[1].GetRef());
    LogF
    (
      "Message %x (%d:%" OID_FORMAT") assigned to object %x (%d:%" OID_FORMAT")",
      msg.GetRef(), creator->value, id->value,
      object, object->GetNetworkId().creator.CreatorInt(), object->GetNetworkId().id
    );
  }
#endif

    // send message
    DWORD dwMsgId = SendMsg
    (
      TO_SERVER, msg, msgType, dwFlags, object
    );
    if (dwMsgId == 0xFFFFFFFF) return;
    // update local objects info
    info.lastSentMsg = msg;
  }
#if CHECK_MSG
  CheckLocalObjects();
#endif

}

RString NetworkClient::GetDiagText(const NetworkObject *obj) const
{
  RString ret;
  {
    #if _ENABLE_CHEATS
    if (obj->GetLastUpdateAge(NMCUpdatePosition)>1000)
    {
      ret = ret + Format(" C Pos: no");
    }
    else
    {
      ret = ret + Format(" C Pos: %.1f",obj->GetLastUpdateAge(NMCUpdatePosition));
    }
    #endif
  }
  /*
  {
    NetworkLocalObjectInfo oInfo;
    if (_localObjects.get(obj->GetNetworkId(),oInfo))
    {
      NetworkUpdateInfo &info = oInfo.updates[NMCUpdatePosition];
    }
  }
  */
  return ret;
}
Time NetworkClient::GetEstimatedEndTime() const
{
  return _missionHeader._estimatedEndTime;
}

void NetworkClient::DestroyRemoteObject(NetworkId id)
{
  NetworkRemoteObjectInfo info;
  if (_remoteObjects.removeKey(id,&info))
  {
    if (info.object && info.remote && id.creator!=CreatorId(STATIC_OBJECT))
    {
      if (DiagLevel >= 1) LogF("Client: DestroyRemoteObject %s is_local=%d is_remote=%d %d:%" OID_FORMAT"", info.object->GetDebugName().Data(), info.object->IsLocal(), info.remote, id.creator.CreatorInt(), id.id);
      info.object->DestroyObject();
    }
    else if (info.object && info.object->IsLocal() && info.remote == false)
    {
      if (DiagLevel >= 1) LogF("Client: DestroyRemoteObject (local) %s is_local=%d is_remote=%d %d:%" OID_FORMAT"", info.object->GetDebugName().Data(), info.object->IsLocal(), info.remote, id.creator.CreatorInt(), id.id);
      info.object->DestroyObject();
    }
  }
}

/// helper for SelectPlayer
class SelectPosition
{
  Vector3 &_pos;
  
  public:
  explicit SelectPosition(Vector3 &pos):_pos(pos){}
  
  __forceinline bool operator () (Entity *obj) const
  {
    _pos = obj->WorldPosition(obj->FutureVisualState());
    return true; /// break on first vehicle
  }
};

/*!
\patch_internal 1.31 Date 11/08/2001 by Jirka
- Fixed: player's vehicle and group ownership is updated immediately when player is selected
*/
void NetworkClient::SelectPlayer(int player, Person *person, bool respawn)
{
LogF("Client: SelectPlayer %x, person %s", player, person ? (const char *)person->GetDebugName() : "<null>");

  Vector3 pos = VZero;

  // send message to server
  if (person)
  {
    // synchronize information on server (ChangeOwner will be performed - so send actual information last time)
    if (person->IsLocal())
    {
      // FIX: wrong mission end detection on DS
      if (player == _playerDpid && person->Brain() && person->Brain()->GetLifeState() != LifeStateDead)
      {
        GWorld->SetRealPlayer(person);
      }

      UpdateObject(person, NMFGuaranteed);
      AIBrain *agent = person->Brain();
      if (agent)
      {
        Transport *veh = agent->GetVehicleIn();
        if (veh)
        {
          if (veh->IsLocal())
            UpdateObject(veh, NMFGuaranteed);
          else
            LogF("SelectPlayer - local player in remote vehicle");
        }
      }
    }
    else
    {
      RptF("SelectPlayer - on remote person");
    }

    pos = person->FutureVisualState().Position();

    NetworkId id = person->GetNetworkId();
    SelectPlayerMessage msg;
    msg._player = player;
    msg._creator = id.creator;
    msg._id = id.id;
    msg._position = pos;
    msg._respawn = respawn;
    SendMsg(&msg, NMFGuaranteed);
  }
  else
  {
    GWorld->ForEachVehicle(SelectPosition(pos));

    SelectPlayerMessage msg;
    msg._player = player;
    msg._creator = CreatorId(0);
    msg._id = 0;
    msg._position = pos;
    msg._respawn = respawn;
    SendMsg(&msg, NMFGuaranteed);
  }

  if (player == _playerDpid) ProcessSelectPlayer(person, respawn, pos);
}

void NetworkClient::SendSelectPlayer(int dpid, Person *person)
{
  // send message to server
  if (person)
  {
    Vector3 pos = person->FutureVisualState().Position();
    NetworkId id = person->GetNetworkId();
    SelectPlayerMessage msg;
    msg._player = dpid;
    msg._creator = id.creator;
    msg._id = id.id;
    msg._position = pos;
    msg._respawn = false;
    SendMsg(&msg, NMFGuaranteed);
  }
}

void NetworkClient::ProcessSelectPlayer(Person *person, bool respawn, Vector3Par pos)
{
LogF
(
  "Client: ProcessSelectPlayer %s (%d:%" OID_FORMAT")",
  person ? (const char *)person->GetDebugName() : "<null>",
  person ? person->GetNetworkId().creator.CreatorInt() : 0,
  person ? person->GetNetworkId().id : 0
);

  if (person && person->Brain() && person->Brain()->GetLifeState() != LifeStateDead) // alive or in respawn
  {
    AIBrain *unit = person->Brain();

    GWorld->SwitchCameraTo(unit->GetVehicle(), CamInternal, true);
    GWorld->SetPlayerManual(true);
    // old player is now in _playerOn
    GWorld->SwitchPlayerTo(person);
    GWorld->SetRealPlayer(person);
    GWorld->UI()->ResetVehicle(unit->GetVehicle());

    // ??? respawn animation script
    if (respawn)
    {
      RStringB GetScriptsPath();
      RString name = Pars >> "playerResurrectScript";
      if (QFBankQueryFunctions::FileExists(GetScriptsPath() + name))
      {
        GameArrayType arguments;
        arguments.Add(GameValueExt(person));
        Script *script = new Script(name, GameValue(arguments), GWorld->GetMissionNamespace()); // mission namespace
        GWorld->StartCameraScript(script);
      }
    }
LogF("  selecting %s person", (const char *)FindEnumName(person->Brain()->GetLifeState()));
  }
  else
  {
    if (person)
    {
      if (!person->Brain())
      {
RptF("  error - person has no brain");
      }
      else
      {
RptF("  error - brain in state %s", (const char *)FindEnumName(person->Brain()->GetLifeState()));
      }
    }
    // create seagull
    SeaGullAuto *seagull = dyn_cast<SeaGullAuto>
    (
      GWorld->NewNonAIVehicleWithID("seagull", RString())
    );
    if (seagull)
    {
      Vector3 position = pos + 20.0f * VUp;
      DoAssert(position.IsFinite());
      seagull->SetPosition(position);
      seagull->SetManual(true);
      seagull->MakeAirborne(20, false);
      seagull->SetPlayer(GetPlayer());
      GWorld->AddAnimal(seagull); // insert vehicle to both landscape and world
      GetNetworkManager().CreateVehicle(seagull, VLTAnimal, "", -1);
      GWorld->SwitchCameraTo(seagull, CamExternal, true);
    }
    if (person)
      LogF("  creating seagull (brain %x, life state %s)", person->Brain(), person->Brain() ? (const char *)FindEnumName(person->Brain()->GetLifeState()) : "none");
    else
      RptF("  creating seagull (no person)");
  }

  if (!respawn)
  {
    // center map
    AbstractOptionsUI *map = GWorld->Map();
    if (map) map->InitHUD();
  }
}

void NetworkClient::PlaySound
(
  RString name, Vector3Par position, Vector3Par speed,
  float volume, float freq, AbstractWave *wave
)
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  PlaySoundMessage msg;
  msg._name = name;
  msg._position = position;
  msg._speed = speed;
  msg._volume = volume;
  msg._frequency = freq;
  msg._soundId = _soundId++;
  msg._creator = GetCreatorId();
  SendMsg(&msg, NMFNone);

  // compact _sentSounds
  for (int i=0; i<_sentSounds.Size();)
  {
    PlaySoundInfo &info = _sentSounds[i];
    if (info.wave) i++;
    else _sentSounds.Delete(i);
  }

  int index = _sentSounds.Add();
  _sentSounds[index].creator = msg._creator;
  _sentSounds[index].id = msg._soundId;
  _sentSounds[index].wave = wave;
}

void NetworkClient::SoundState
(
  AbstractWave *wave, SoundStateType state
)
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  CreatorId creator = CreatorId(-1);
  int id = -1;
  for (int i=0; i<_sentSounds.Size();)
  {
    PlaySoundInfo &info = _sentSounds[i];
    if (!info.wave)
    {
      _sentSounds.Delete(i);
      continue;
    }
    if (info.wave == wave)
    {
      creator = info.creator;
      id = info.id;
    }
    i++;
  }
  
  if (id < 0) return;

  SoundStateMessage msg;
  msg._state = state;
  msg._creator = creator;
  msg._soundId = id;
  SendMsg(&msg, NMFNone);
}

/*!
\patch 5234 Date 2/28/2008 by Jirka
- Fixed: For players in driver and gunner position of the vehicle, obsolete network transfer was generated
*/

/// functor for update object traversal
class NetworkClientUpdateTurret : public ITurretFunc
{
protected:
  NetworkClient *_client;

public:
  NetworkClientUpdateTurret(NetworkClient *client) : _client(client) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret) _client->UpdateObject(context._turret, NMFGuaranteed);
    return false; // continue
  }
};

/// functor for update object traversal
class NetworkClientUpdateObject
{
protected:
  NetworkClient *_client;

public:
  NetworkClientUpdateObject(NetworkClient *client) : _client(client) {}
  bool operator () (Entity *veh) const
  {
    if (veh->GetNetworkId().IsNull()) return false; // there can be craters,smokes,etc. after MP Load, skip them
    _client->UpdateObject(veh, NMFGuaranteed);
    EntityAIFull *vehAI = dyn_cast<EntityAIFull>(veh);
    if (vehAI)
    {
      NetworkClientUpdateTurret func(_client);
      vehAI->ForEachTurret(func);
    }
    return false;
  }
};

void NetworkClient::CreateAllObjects()
{
  // send the info about network objects need to be created
  MissionStatsMessage stats;
  stats._toCreate = 0;
  stats._toUpdate = _localObjects.card();
  SendMsg(&stats, NMFGuaranteed);

  // pass 2
  GWorld->ForEachVehicle(NetworkClientUpdateObject(this));
  GWorld->ForEachAnimal(NetworkClientUpdateObject(this));
  GWorld->ForEachSlowVehicle(NetworkClientUpdateObject(this));
  GWorld->ForEachCloudlet(NetworkClientUpdateObject(this));
  GWorld->ForEachFastVehicle(NetworkClientUpdateObject(this));
  GWorld->ForEachOutVehicle(NetworkClientUpdateObject(this));
  
  ClientStateMessage msg;
  msg._clientState = NCSGameLoaded;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::DestroyAllObjects()
{
LogF("Client: Destroying network objects database");

  // declare all objects as remote
  for (LocalObjectsContainer::Iterator it=_localObjects.begin(); it; _localObjects.next(it))
  {
    NetworkLocalObjectInfo &oInfo = *it;
    if (oInfo.object) oInfo.object->SetLocal(false);
  }
  _localObjects.reset();
  _remoteObjects.reset();
  if (DiagLevel >= 1)
    DiagLogF("Client: all objects destroyed");

  // avoid id duplicity at next missions
  // _nextId = 0;
}

bool NetworkClient::CreateVehicle(Vehicle *veh, VehicleListType list, RString name, int idVeh)
{
  if (!veh) return false;
  if (_clientState < NCSMissionReceived) return false;

  if (!IsBotClient()) return false; // only server can create network objects

  NetworkId id = CreateLocalObject(veh);

  // send message to center
  NetworkMessageType msgType = veh->GetNMType(NMCCreate);

  // create message
  Ref<NetworkMessage> msg = ::CreateNetworkMessage(msgType);
  msg->time = Glob.time;
  NetworkMessageContext ctx(msg, this, TO_SERVER, MSG_SEND);
  ctx.SetClass(NMCCreate);

  // difference from CreateObject - vehicle does known its list, name, id:
  PREPARE_TRANSFER(CreateVehicle)

  if (TRANSF_BASE(list, (int &)list) != TMOK) return false;
  if (TRANSF_BASE(varName, name) != TMOK) return false;
  if (TRANSF_BASE(idVehicle, idVeh) != TMOK) return false;

  // avoid uninitialized message member
  bool guaranteed = true;
  TRANSF_BASE(guaranteed, guaranteed);

  if (veh->TransferMsg(ctx) != TMOK) return false;

  // send message
  DWORD dwMsgId = SendMsg
  (
    TO_SERVER, msg, msgType, NMFGuaranteed, veh
  );

  // create subobjects (turrets etc.)
  veh->CreateSubobjects();

  return dwMsgId != 0xFFFFFFFF;
}

bool NetworkClient::CreateSubobject(Vehicle *veh, TurretPath &path, NetworkObject *object, bool setNetworkId)
{
  Assert(veh);
  Assert(object);
  if (_clientState < NCSMissionReceived) return false;

  NetworkId id = CreateLocalObject(object, setNetworkId);

  // send message to center
  NetworkMessageType msgType = object->GetNMType(NMCCreate);

  // create message
  Ref<NetworkMessage> msg = ::CreateNetworkMessage(msgType);
  msg->time = Glob.time;
  NetworkMessageContext ctx(msg, this, TO_SERVER, MSG_SEND);
  ctx.SetClass(NMCCreate);

  // difference from CreateObject - object does not known his owner
  // TODO: generalization - other subobjects
  PREPARE_TRANSFER(CreateTurret)

  Ref<Entity> vehicle = veh;
  if (TRANSF_REF_BASE(vehicle, vehicle) != TMOK) return false;
  if (TRANSF_BASE(path, path) != TMOK) return false;
  if (object->TransferMsg(ctx) != TMOK) return false;

  // avoid uninitialized message member
  bool guaranteed = true;
  TRANSF_BASE(guaranteed, guaranteed);

  // send message
  DWORD dwMsgId = SendMsg
  (
    TO_SERVER, msg, msgType, NMFGuaranteed, object
  );

  return dwMsgId != 0xFFFFFFFF;
}

//! Maps text form of Network Command to Network Command Type
struct NetworkCommand
{
  //! text form
  const char *command;
  //! Network Command Type 
  NetworkCommandType type;
};

//! Maps text form of Network Commands to Network Command Types
/*!
\patch 1.16 Date 08/10/2001 by Jirka
- Added: Dedicated server command #shutdown
- Added: Dedicated server command #reassign
\patch 1.22 Date 8/24/2001 by Jirka
- Added: Dedicated server command #monitor
\patch 1.24 Date 9/19/2001 by Jirka
- Added: Dedicated server command #userlist
- Added: Dedicated server command #kick <number = player id>
\patch 1.24 Date 9/20/2001 by Jirka
- Added: Voting system (command #vote ...)
Possible vote commands: kick, restart, reassign, mission, missions
\patch 1.33 Date 11/29/2001 by Jirka
- Added: Dedicated server command #vote admin
\patch 1.34 Date 12/06/2001 by Jirka
- Added: Dedicated server command #init (reload server config file)
\patch 1.87 Date 10/17/2002 by Jirka
- Added: Dedicated server command #debug (console, totalSent, userSent)
(Requires 1.87 client, client output sent to OS using OuputDebugString)
*/

static NetworkCommand NetworkCommands[] =
{
  {"login", CMDLogin},
  {"logout", CMDLogout},
  {"kick", CMDKick},
  {"restart", CMDRestart},
  {"mission", CMDMission},
  {"missions", CMDMissions},
  {"shutdown", CMDShutdown},
  {"reassign", CMDReassign},
  {"monitor", CMDMonitor},
  {"userlist", CMDUserlist},
  {"vote", CMDVote},
  {"admin", CMDAdmin},
  {"init", CMDInit},
  {"debug", CMDDebug},
  {"exec", CMDExec},
  {"lock", CMDLock},
  {"unlock", CMDUnlock},  
  {"beserver", CMDBEServer},
  {"beclient", CMDBEClient},
  {"profile", CMDProfile},
#if _ENABLE_CHEATS
  {"gameExec", CMDGameExec},
#endif
#if _VBS3
  {"exitall", CMDExitAll},
#endif
};

//! Find Network Command Type for text form of Network Command
/*!
\param beg text form of Command
\param len length of text
*/
static NetworkCommandType FindCommandType(const char *beg, int len)
{
  for (int i=0; i<sizeof(NetworkCommands)/sizeof(NetworkCommand); i++)
  {
    NetworkCommand &item = NetworkCommands[i];
    if (strlen(item.command) == len && strnicmp(beg, item.command, len) == 0)
    {
      return item.type;
    }
  }
  return CMDNone;
}

//! Find DirectPlay ID of player for text form of player
/*!
\param name text form of player - player name or (simple) player id
\param identities player identities to search in
*/
static int FindPlayerId(const char *name, AutoArray<PlayerIdentity> &identities)
{
  for (int i=0; i<identities.Size(); i++)
  {
    if (stricmp(name, identities[i].name) == 0)
      return identities[i].dpnid;
  }
  int playerid = atoi(name);
  for (int i=0; i<identities.Size(); i++)
  {
    if (identities[i].playerid == playerid)
      return identities[i].dpnid;
  }
  return AI_PLAYER; // not found
}
/*!
\patch 1.33 Date 12/03/2001 by Ondra
- New: Admin / Server indication in list of players.
*/
RString GetIdentityText(const PlayerIdentity &identity)
{
  RString spec;
  spec = Format("%d ms",identity._avgPing);
  if (identity._rights&PRServer)
  {
    spec = LocalizeString(IDS_MP_SERVER);
  }
  else if (identity._rights&(PRAdmin|PRVotedAdmin))
  {
    if (identity._rights&PRVotedAdmin)
    {
      spec = spec + RString(", ")+LocalizeString(IDS_MP_MASTER);
    }
    else
    {
      spec = spec + RString(", *")+LocalizeString(IDS_MP_MASTER)+RString("*");
    }
  }
  if (spec.GetLength() > 0)
    return Format("%s (%s)",(const char *)identity.name,(const char *)spec);
  else
    return identity.name;
}

/*!
\patch 1.25 Date 10/01/2001 by Jirka
- Added: Enable kick off player from MPPlayers display also by gamemaster on dedicated server.
*/
void NetworkClient::SendKick(int player)
{
  NetworkCommandMessage msg;
  msg._type = NCMTKick;
  msg._content.Write(&player, sizeof(int));
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SendLockSession(bool lock)
{
  NetworkCommandMessage msg;
  msg._type = NCMTLockSession;
  msg._content.Write(&lock, sizeof(bool));
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::Disconnect(RString message)
{
  RemoveUserMessages();

  _clientState = NCSNone;
  _serverState = NSSNone;

  if (!message.IsEmpty())
  {
    const PlayerIdentity *id = FindIdentity(_playerDpid);
    if (id)
    {
      RString senderName = id->name;
      GChatList.Add(CCSystem, senderName, message, false, true);
      RefArray<NetworkObject> dummy;
      if (!ChannelDisabled(CCSystem))
        GNetworkManager.Chat(CCSystem,senderName,dummy,message);
    }
    else
    {
      GChatList.Add(CCSystem, NULL, message, false, true);
      WarningMessage(message);
    }
  }
  // BattlEye integration
#if _USE_BATTL_EYE_CLIENT
  if (_playerDpid != BOT_CLIENT || !IsDedicatedServer()) // DS bot client does not represent any player
  {
    _beIntegration.Done();
}
#endif
}

bool NetworkClient::IsCommandAvailable(RString command)
{
  const char *beg = command;
  if (*beg != '#') return false;
  beg++;

  const char *end = strchr(beg, ' ');
  int len = end ? end - beg : strlen(beg);
  NetworkCommandType type = FindCommandType(beg, len);

  return IsCommandAvailable(type);
}

bool NetworkClient::IsCommandAvailable(NetworkCommandType type)
{
  switch (type)
  {
  // NOT AVAILABLE
  case CMDNone:  
  case CMDAdmin: 
    return false;
    break;
  // ALWAYS AVAILABLE
  case CMDLogin: 
  case CMDUserlist:
#if _ENABLE_CHEATS
  case CMDGameExec:
#endif
#if _USE_BATTL_EYE_CLIENT
  case CMDBEClient:
#endif
#if _VBS3
  case CMDExitAll:
#endif
    return true;
    break;
  // There is no admin on the server, so voting is possible
  case CMDVote:
    return _serverGameMaster == AI_PLAYER;
    break;
  // ADMIN OR SERVER
  case CMDKick:
  case CMDDebug:
    return (_gameMaster || GetNetworkManager().IsServer());
  // Logged in ADMINs or server
  case CMDLock: 
  case CMDUnlock:
    return ((_gameMaster && !_admin)|| GetNetworkManager().IsServer());
  // ADMINS ONLY
  // Voted
  case CMDLogout:
  case CMDRestart: 
  case CMDMission:
  case CMDMissions:
  case CMDReassign:
  case CMDMonitor: 
  case CMDInit: 
    return _gameMaster;
  // Logged in admins
  case CMDShutdown:
  case CMDExec:
  case CMDBEServer:
  case CMDProfile: 
    return (_gameMaster && !_admin);
  }
  return false;
}

/*!
\patch 5180 Date 12/21/2007 by Ondra
- Fixed: Disabled player ID in non-admin #userlist.
*/

bool NetworkClient::ProcessCommand(RString command)
{
  const char *beg = command;
  if (*beg != '#') return false;
  beg++;

  const char *end = strchr(beg, ' ');
  int len = end ? end - beg : strlen(beg);
  NetworkCommandType type = FindCommandType(beg, len);
  beg += len;
  while (*beg == ' ') beg++;

  if ( IsCommandAvailable(type) || IsBotClient() )
  {
  switch (type)
  {
    case CMDNone:
    case CMDAdmin:
      break;
    case CMDLogin:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTLogin;
        msg._content.WriteString(beg);
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDLogout:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTLogout;
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDKick:
      {
        int id = FindPlayerId(beg, _identities);
        if (id != AI_PLAYER) SendKick(id);
      }
      break;
    case CMDLock:
      SendLockSession(true);
      break;
    case CMDUnlock:
      SendLockSession(false);
      break;
    case CMDRestart:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTRestart;
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDMission:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTMission;
        msg._content.WriteString(beg);
        msg._content.WriteString(Glob.config.diffNames.GetName(Glob.config.diffDefault));
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDMissions:
      {
        // _selectMission = true;
        NetworkCommandMessage msg;
        msg._type = NCMTMissions;
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDShutdown:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTShutdown;
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDReassign:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTReassign;
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDMonitor:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTMonitorAsk;
        float value = 10.0f;
        if (*beg) value = atof(beg);
        msg._content.Write(&value, sizeof(value));
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDProfile: 
      {
        NetworkCommandMessage msg;
        msg._type = NCMTProfileAsk;
        msg._content.WriteString(beg); // send arguments as given, let server to interpret them
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDUserlist:
      for (int i=0; i<_identities.Size(); i++)
      {
        const PlayerIdentity &identity = _identities[i];
        BString<256> buffer;
        if (GetNetworkManager().IsGameMaster() && !GetNetworkManager().IsAdmin() || GetNetworkManager().IsServer())
        {
          sprintf(
            buffer, "%d: %s (id = %s%s), version %d.%d.%d",
            identity.playerid,cc_cast(GetIdentityText(identity)),cc_cast(identity.id),identity.certificate.Size()>0 ? "" : " ?",
            identity.version/100,identity.version%100,identity.build
          );
        }
        else
        {
          sprintf(
            buffer, "%d: %s",
            identity.playerid,(const char *)GetIdentityText(identity)
          );
        }
        GChatList.Add(CCSystem, NULL, buffer.cstr(), false, true);
      }
      break;
    case CMDInit:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTInit;
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDDebug:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTDebugAsk;
        msg._content.WriteString(beg);
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
    case CMDVote:
      {
        end = strchr(beg, ' ');
        len = end ? end - beg : strlen(beg);
        type = FindCommandType(beg, len);
        beg += len;
        while (*beg == ' ') beg++;

        switch (type)
        {
          case CMDKick:
            {
              int id = FindPlayerId(beg, _identities);
              if (id != AI_PLAYER)
              {
                NetworkCommandMessage msg;
                msg._type = NCMTVote;
                int subtype = NCMTKick;
                msg._content.Write(&subtype, sizeof(int));
                msg._content.Write(&id, sizeof(int));
                SendMsg(&msg, NMFGuaranteed);
              }
            }
            break;
          case CMDRestart:
            {
              NetworkCommandMessage msg;
              msg._type = NCMTVote;
              int subtype = NCMTRestart;
              msg._content.Write(&subtype, sizeof(int));
              SendMsg(&msg, NMFGuaranteed);
            }
            break;
          case CMDReassign:
            {
              NetworkCommandMessage msg;
              msg._type = NCMTVote;
              int subtype = NCMTReassign;
              msg._content.Write(&subtype, sizeof(int));
              SendMsg(&msg, NMFGuaranteed);
            }
            break;
          case CMDMission:
            {
              VoteMissionMessage msg;
              RString name = beg;
              name.Lower();
              msg._name = name;
              msg._diff = Glob.config.diffNames.GetName(Glob.config.diffDefault);
              SendMsg(&msg, NMFGuaranteed);
            }
            break;
          case CMDMissions:
            {
              NetworkCommandMessage msg;
              msg._type = NCMTVote;
              int subtype = NCMTMissions;
              msg._content.Write(&subtype, sizeof(int));
              SendMsg(&msg, NMFGuaranteed);
            }
            break;
          case CMDAdmin:
            {
              int id = FindPlayerId(beg, _identities);
              if (id != AI_PLAYER)
              {
                NetworkCommandMessage msg;
                msg._type = NCMTVote;
                int subtype = NCMTAdmin;
                msg._content.Write(&subtype, sizeof(int));
                msg._content.Write(&id, sizeof(int));
                SendMsg(&msg, NMFGuaranteed);
              }
            }
            break;
        }
      }
      break;
    case CMDExec:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTExec;
        msg._content.WriteString(beg);
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
#if _ENABLE_CHEATS
    case CMDGameExec:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTGameExec;
        msg._content.WriteString(beg);
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
#endif
    case CMDBEServer:
      {
        NetworkCommandMessage msg;
        msg._type = NCMTBattlEye;
        msg._content.WriteString(beg);
        SendMsg(&msg, NMFGuaranteed);
      }
      break;
#if _USE_BATTL_EYE_CLIENT
    case CMDBEClient:
      _beIntegration.Command(beg);
      break;
#endif
#if _VBS3
    case CMDExitAll:
      NetworkCommandMessage msg;
      msg._type = NCMTExitAll;
      SendMsg(&msg, NMFGuaranteed);
      break;
#endif
  }
  }
  return true;
}

void NetworkClient::SelectMission(RString mission, int difficulty)
{
  if (_gameMaster)
  {
    NetworkCommandMessage msg;
    msg._type = NCMTMission;
    msg._content.WriteString(mission);
    msg._content.WriteString(Glob.config.diffNames.GetName(difficulty));
    SendMsg(&msg, NMFGuaranteed);
  }
  _selectMission = false;
  _voteMission = false;
}

void NetworkClient::VoteMission(RString mission, int difficulty)
{
  VoteMissionMessage msg;
  msg._name = mission;
  msg._diff = Glob.config.diffNames.GetName(difficulty);
  SendMsg(&msg, NMFGuaranteed);
}

DWORD NetworkClient::SendMsg
(
  NetworkSimpleObject *object, NetMsgFlags dwFlags
)
{
  if (_clientState<NCSConnected) return ~0;
  return NetworkComponent::SendMsg(TO_SERVER, object, dwFlags);
}

void NetworkClient::EnqueueMsg
(
  int to, NetworkMessage *msg,
  NetworkMessageType type,
  NetworkObject *object
)
{
  ClientQueueItem *item = new ClientQueueItem();
  item->type = type;
  item->msg = msg;
  item->object = object;
  _messageQueue.Add(item);
}

/*!
\patch 1.26 Date 10/05/2001 by Jirka
- Optimized: MP: small update messages are sent packed
  to form optimum size UDP frames.
*/

void NetworkClient::EnqueueMsgNonGuaranteed
(
  int to, NetworkMessage *msg,
  NetworkMessageType type,
  const RefR<NetMessage, RefCountSafe> &dependOn
)
{
  ClientQueueItem *item = new ClientQueueItem();
  item->type = type;
  item->msg = msg;
  item->dependOn = dependOn;
  _messageQueueNonGuaranteed.Add(item);
}

NetworkMessageFormatBase *NetworkClient::GetFormat(/*int client, */int type)
{
  Assert(type >= 0);
  if (type < NMTFirstVariant)
  {
    // static format
    return GMsgFormats[type];
  }
  else if (_clientState>=NCSConnected || type==NMTClientState) // client may receive packets from a previous session. It should not attempt processing them if not ready.
  {
    // dynamic format
    int index = type - NMTFirstVariant;
    if (index >= _formats.Size()) return NULL;
    return &_formats[index];
  }
  else return NULL;
}

const NetworkMessageFormatBase *NetworkClient::GetFormat(/*int client, */int type) const
{
  Assert(type >= 0);
  if (type < NMTFirstVariant)
  {
    // static format
    return GMsgFormats[type];
  }
  else if (_clientState>=NCSConnected || type==NMTClientState) // client may receive packets from a previous session. It should not attempt processing them if not ready.
  {
    // dynamic format
    int index = type - NMTFirstVariant;
    if (index >= _formats.Size()) return NULL;
    return &_formats[index];
  }
  else return NULL;
}

void NetworkClient::AskForPilotLight(EntityAI *vehicle, bool enable)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  AskForPilotLightMessage msg;
  msg._vehicle = vehicle;
  msg._enable = enable;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForVehicleDoorState(Transport *vehicle, int doorIndex, bool opened)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  AskForVehicleDoorStateMessage msg;
  msg._vehicle = vehicle;
  msg._doorIndex = doorIndex;
  msg._opened = opened;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::CreateObjectStatic(Object *who)
{
  DoAssert(GetNetworkManager().IsServerOwner());

  NetworkId id = who->GetNetworkId();
  DoAssert(id.creator==CreatorId(STATIC_OBJECT));

  // check if this already exists as an object
  if (!GetLocalObjectInfo(id))
  {
    InsertLocalObject(who);
    InsertRemoteObject(who, false);
    SendCreateMessage(who);
  }
}

void NetworkClient::ExplosionDamage( EntityAI * owner, Vector3Par originDir, Object * directHit, Vector3Par directHitModelPos, 
                                    HitInfoPar hitInfo, Object * componentOwner, int componentIndex, 
                                    float energyFactor, float explosionFactor, float hitDepth )
{
  ExplosionDamageMessage msg;
  msg._owner = owner;
  msg._directHit = directHit;
  msg._originDir = originDir;
  msg._directHitModelPos = directHitModelPos;
  msg._componentOwner = componentOwner;
  msg._componentIndex = componentIndex;
  msg._type = hitInfo._ammoType->GetName();
  msg._pos = hitInfo._pos;
  msg._surfNormal = hitInfo._surfNormal;
  msg._inSpeed = hitInfo._inSpeed;
  msg._outSpeed = hitInfo._outSpeed;
  msg._energyFactor = energyFactor;
  msg._explosionFactor = explosionFactor;
  msg._hitDepth = hitDepth;

  SendMsg(&msg, NMFGuaranteed);
}


void NetworkClient::AskForAmmo
(
  EntityAIFull *vehicle, Turret *turret, const WeaponModePtr &ptr, int burst
)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  AskForAmmoMessage msg;
  msg._vehicle = vehicle;
  msg._turret = turret;
  msg._weapon = ptr._weapon;
  msg._muzzle = ptr._muzzle;
  msg._mode = ptr._mode;
  msg._burst = burst;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForMove
(
  Object *vehicle, Vector3Par pos
)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  AskForMoveVectorMessage msg;
  msg._vehicle = vehicle;
  msg._pos = pos;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForMove
(
  Object *vehicle, Matrix4Par trans
)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  AskForMoveMatrixMessage msg;
  msg._vehicle = vehicle;
  msg._pos = trans.Position();
  msg._orient = trans.Orientation();
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForSetMagazineAmmo
(
  Object *magazine, int ammo
)
{
  if (_clientState != NCSPlaying) 
    return;  // send event messages only when playing

  AskForSetMagazineAmmoMessage msg;
  msg._magazine = magazine;
  msg._ammo = ammo;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::ExplosionDamageEffects(
  EntityAI *owner, Object *directHit, HitInfoPar hitInfo, int componentIndex,
  bool enemyDamage, float energyFactor, float explosionFactor
)
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  if (hitInfo._ammoType == NULL) return;

  ExplosionDamageEffectsMessage msg;
  msg._owner = owner;
  msg._directHit = directHit;
  msg._componentIndex = componentIndex;
  msg._type = hitInfo._ammoType->GetName();
  msg._pos = hitInfo._pos;
  msg._surfNormal = hitInfo._surfNormal;
  msg._inSpeed = hitInfo._inSpeed;
  msg._outSpeed = hitInfo._outSpeed;
  msg._enemyDamage = enemyDamage;
  msg._energyFactor = energyFactor;
  msg._explosionFactor = explosionFactor;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::ApplyProjectileDecal(
  Object *directHit, int componentIndex, HitInfoPar hitInfo, float hitDepth, 
  Vector3Par hitPosLcl, Vector3Par hitDirLcl, Vector3Par hitDirUpLcl
)
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  if (hitInfo._ammoType == NULL) return;

  ProjectileDecalMessage msg;
  msg._directHit = directHit;
  msg._componentIndex = componentIndex;
  msg._ammoType = hitInfo._ammoType->GetName();
  msg._hitPosWorld = hitInfo._pos;
  msg._surfNormal = hitInfo._surfNormal;
  msg._inSpeed = hitInfo._inSpeed;
  msg._outSpeed = hitInfo._outSpeed;
  msg._hitPosLcl = hitPosLcl;
  msg._hitDirLcl = hitDirLcl;
  msg._hitDirUpLcl = hitDirUpLcl;
  msg._hitDepth = hitDepth;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::RemoveProjectileDecals(Object *person)
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  if (person == NULL) return;

  RemoveProjectileDecalsMessage msg;
  msg._person = person;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::FireWeapon(
  EntityAIFull *vehicle, Person *gunner, const WeaponModePtr &ptr, Magazine *magazine, EntityAI *target,
  const RemoteFireWeaponInfo &remoteInfo
)
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  if (!magazine) return;

  FireWeaponMessage msg;
  msg._vehicle = vehicle;
  // TODO: for now, gunner is used as a turret identification
  // maybe some better turret id should be transferred
  msg._gunner = gunner;
  msg._target = target;
  msg._weapon = ptr._weapon;
  msg._muzzle = ptr._muzzle;
  msg._mode = ptr._mode;
  msg._magazine = magazine;
  DoAssert(remoteInfo._position.IsFinite());
  msg._pos = remoteInfo._position;
  msg._dir = remoteInfo._direction;
  msg._visible = remoteInfo._visible;
#if _VBS3
  msg._projectile = remoteInfo._projectile;
#endif
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::UpdateBowState( EntityAIFull *entity, BowState state )
{
  if (_clientState != NCSPlaying) return;  // send event messages only when playing

  BowStateMessage msg;
  msg._person = entity;
  msg._state = (int)state;

  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::OnVehicleDestroyed
(
  EntityAI *killed, EntityAI *killer
)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  if (!killed->GetNetworkId().IsNull())
  {
    VehicleDestroyedMessage msg;
    msg._killed = killed;
    if (killer && !killer->GetNetworkId().IsNull())
      msg._killer = killer;
    SendMsg(&msg, NMFGuaranteed);
  }
}

void NetworkClient::OnVehicleDamaged(EntityAI *damaged, EntityAI *killer, float damage, RString ammo)
{
  if (_clientState != NCSPlaying
#if _VBS3 //enable messages also during briefing screen to enable scripted missions
    && (_clientState < NCSGameLoaded || _clientState > NCSPlaying)
#endif
    ) return;  // send event messages only when playing

  VehicleDamagedMessage msg;
  msg._damaged = damaged;
  msg._killer = killer;
  msg._damage = damage;
  msg._ammo = ammo;
  SendMsg(&msg, NMFGuaranteed);
}

static inline Person *SenderPerson(AIBrain *sender)
{
  return sender ? sender->GetPerson() : nullptr;
}

void NetworkClient::MarkerCreate(int channel, AIBrain *sender, RefArray<NetworkObject> &units, ArcadeMarkerInfo &info)
{
  if (_clientState < NCSMissionReceived) return;

  MarkerCreateMessage msg;
  msg._marker = info;
  msg._channel = channel;
  msg._sender = SenderPerson(sender);
  msg._units = units;
  SendMsg(&msg, NMFGuaranteed);

}

void NetworkClient::MarkerDelete(RString name)
{
  if (_clientState < NCSMissionReceived) return;

  MarkerDeleteMessage msg;
  msg._name = name;

  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SetObjectTexture(EntityAI *veh, int index, RString name, RString eval)
{
#if _ENABLE_VBS
  if (_clientState < NCSMissionReceived) return;

  SetObjectTextureMessage msg;
  msg._veh = veh;
  msg._index = index;
  msg._name = name;
  msg._eval = eval;
  SendMsg(&msg, NMFGuaranteed);
#endif
}

void NetworkClient::PublicExec(RString condition, RString command, Object* obj, OLinkArray(Object) &objs)
{
#if _ENABLE_VBS
  if (_clientState < NCSMissionReceived) return;

  PublicExecMessage msg;
  msg._condition = condition;
  msg._command = command;
  msg._obj = obj;
  msg._objs = objs;
  SendMsg(&msg, NMFGuaranteed);
#endif
}

/*!
Implemented only for scalar variables.
\todo Implement for other variable types.
\patch 1.34 Date 12/7/2001 by Ondra
- New: publicVariable now works with scalar, bool, Object and Group types.
\patch 2.04 Date 6/4/2003 by Jirka
- New: publicVariable now works with string type.
*/
void NetworkClient::PublicVariable(RString name, int dpnid)
{
#define DEBUG_PUBLIC_VARIABLES 0
#if DEBUG_PUBLIC_VARIABLES
  if (stricmp(cc_cast(name),"BIS_missionScope")==0)
    _asm nop;
#endif
  GameState *gstate = GWorld->GetGameState();
  GameValue var = gstate->VarGet(name, false, GWorld->GetMissionNamespace()); // mission namespace
  if (var.GetNil()) return;

  QOStrStream out;
  if (!WriteValue(var, out))
    return;

  // Store publicVariables containing objects without NetworkId to the delayed list
  bool addToDelayedList = false;
  const GameType &type = var.GetType();
  if (type==GameObject)
  {
    Object *value = GetValObject(var);
    if ( value && value->GetNetworkId().IsNull() ) addToDelayedList = true; // it exists but CreateAllObjects was possibly not called yet
  }
#if _ENABLE_INDEPENDENT_AGENTS
  else if (type==GameTeamMember)
  {
    RptF("Transfer of AI structures is not supported");
  }
#endif

  if (addToDelayedList)
  { 
    AddDelayedPublicVariable(name, dpnid); //store for later processing
    if ( _clientState < NCSGameLoaded )
      return; // Hack: we hope the delayed PublicVariableMessage would contain object with NetworkId present
  }

  if (dpnid == 0)
  {
    RptF("publicVariable no longer supported");
  }
  else
  {
    PublicVariableToMessage msg;
    msg._name = name;
    int size = out.pcount();
    msg._value.Resize(size);
    memcpy(msg._value.Data(), out.str(), size);
    msg._player = dpnid;
    SendMsg(&msg, NMFGuaranteed);
  }
}

void NetworkClient::SendAUMessage(GameValuePar oper1)
{
  QOStrStream out;
  if (!WriteValue(oper1, out))
    return;

  SendAUMsgMessage msg;
  int size = out.pcount();
  msg._value.Resize(size);
  memcpy(msg._value.Data(), out.str(), size);
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AddDelayedPublicVariable(RString name, int dpnid)
{
  DelayedPublicVariable item;
  item._name = name;
  item._player = dpnid;
  _delayedPublicVariables.Add(item);
}

void NetworkClient::ClearDelayedPublicVariables()
{
  _delayedPublicVariables.Clear();
}

void NetworkClient::ProcessDelayedPublicVariables()
{
  //note: the for cycle cannot loop for ever even when PublicVariable add something into _delayedPublicVariables array
  for (int i=0, siz=_delayedPublicVariables.Size(); i<siz; i++)
  {
    const DelayedPublicVariable &item = _delayedPublicVariables[i];
    PublicVariable(item._name, item._player);
  }
  _delayedPublicVariables.Clear();
}

void NetworkClient::RunSpawnedCode(RString code, GameValuePar args)
{
  GameState *state = GWorld->GetGameState();
  Ref<GameDataCode> compiled = new GameDataCode(state, code, GWorld->GetMissionNamespace());
  ScriptVM *script = new ScriptVM(ScriptVMDirect, *compiled, args, GWorld->GetMissionNamespace());
  GWorld->AddScriptVM(script, false);
}

void NetworkClient::ProcessDelayedSpawns()
{
  _initScriptRun = true;
  for (int i=0; i<_delayedSpawns.Size(); i++)
    RunSpawnedCode(_delayedSpawns[i]._code, _delayedSpawns[i]._args);
  _delayedSpawns.Clear();
}

void NetworkClient::ApplyWeather()
{
  ApplyWeatherMessage msg;
  GLandscape->TranferMsg(msg);

  SendMsg(&msg,NMFGuaranteed);
}

#if _VBS3
void NetworkClient::AARAskUpdate(int type ,int intType,RString stringOne, RString stringTwo, float floatType)
{
  AARAskUpdateMessage msg;
  msg._type = type;
  msg._intType = intType;
  msg._stringOne = stringOne;
  msg._stringTwo = stringTwo;
  msg._floatType = floatType;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AddMPReport(int type, RString myString)
{
  AddMPReportMessage msg;
  msg._type = type;
  msg._myString = myString;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AARDoUpdate(int type ,int intType,RString stringOne, RString stringTwo, float floatType)
{
  AARDoUpdateMessage msg;
  msg._type = type;
  msg._intType = intType;
  msg._stringOne = stringOne;
  msg._stringTwo = stringTwo;
  msg._floatType = floatType;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::LoadIsland(RString islandName)
{
  LoadIslandMessage msg;
  msg._islandName = islandName;

  SendMsg(&msg,NMFGuaranteed);
}

void NetworkClient::AskCommanderOverride(Turret *turret, Turret *overriddenByTurret)
{
  AskCommanderOverrideMessage msg;
  msg._turret = turret;
  msg._overriddenByTurret = overriddenByTurret;
  SendMsg(&msg, NMFGuaranteed);
}

#endif //_VBS3

void NetworkClient::AskForSetItemShortcut(Person *person, int index, const InventoryItemType *type, InventoryItem *item)
{
  AskForSetItemShortcutMessage msg;
  msg._person = person;
  msg._index = index;
  msg._item = item;
  msg._itemType = type ? type->GetName() : RString();
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskInventoryMenu(AIBrain *unit, InventoryItem *item1, InventoryItem *item2)
{
  AskInventoryMenuMessage msg;
  msg._unit = unit ? unit->GetPerson() : nullptr;
  msg._item1 = item1;
  msg._item2 = item2;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForThrowItem(Man *person, InventoryItem *item, Vector3 pos, Vector3 dir)
{
  AskForThrowItemMessage msg;
  msg._person = person;
  msg._item = item;
  msg._pos = pos;
  msg._dir = dir;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForDropItem(Person *person, InventoryItem *item)
{
  AskForDropItemMessage msg;
  msg._person = person;
  msg._item = item;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForMoveItem(EntityAI *target, int index, InventoryItem *item, bool rotated)
{
  AskForMoveItemMessage msg;
  msg._target = target;
  msg._index = index;
  msg._item = item;
  msg._rotated = rotated;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForRotateInventoryItem(Person *person, InventoryItem *item)
{
  AskForRotateInventoryItemMessage msg;
  msg._person = person;
  msg._item = item;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForTakeItemToHands(Person *person, InventoryItem *item)
{
  AskForTakeItemToHandsMessage msg;
  msg._person = person;
  msg._item = item;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForSwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner)
{
  AskForSwapItemsMessage msg;
  msg._item1 = item1;
  msg._item2 = item2;
  msg._owner = owner;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForPlayerAction( Person * person, RString action )
{
  AskForPlayerActionMessage msg;
  msg._person = person;
  msg._action = action;
  SendMsg(&msg, NMFGuaranteed);
}

#if _VBS3 // respawn command
void NetworkClient::PauseSimulation(bool pause)
{
  PauseSimulationMessage msg;
  msg._pause = pause;
  SendMsg(&msg,NMFGuaranteed);
}

#endif

#if _ENABLE_ATTACHED_OBJECTS
void NetworkClient::AttachObject(Object *obj, Object *attachTo, int memIndex, Vector3 pos, int flags)
{
  AttachObjectMessage msg;
  msg._obj = obj;
  msg._attachTo = attachTo;
  msg._memIndex = memIndex;
  msg._pos = pos;
  msg._flags = flags;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::DetachObject(Object *obj)
{
  DetachObjectMessage msg;
  msg._obj = obj;
  SendMsg(&msg, NMFGuaranteed);
}
#endif

/*!
\patch 1.82 Date 8/26/2002 by Jirka
- Fixed: Transfer of user defined animations through network in MP
*/

void NetworkClient::AskForAnimationPhase(Entity *vehicle, RString animation, float phase)
{
  AskForAnimationPhaseMessage msg;
  msg._vehicle = vehicle;
  msg._animation = animation;
  msg._phase = phase;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::ProcessAction(Action *action, AIBrain *unit)
{
  ProcessActionMessage msg;
  msg._actionType = action->GetType();
  msg._actionTarget = action->GetTarget();

  QOStrStream out;
  action->SaveParams(out);
  int size = out.pcount();
  msg._actionParams.Resize(size);
  memcpy(msg._actionParams.Data(), out.str(), size);

  msg._unit = unit ? unit->GetPerson() : nullptr;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::ProcessRecipe(RString recipe, Person *person, InventoryItem *item1, InventoryItem *item2)
{
  ProcessRecipeMessage msg;
  msg._recipe = recipe;
  msg._person = person;
  msg._item1 = item1;
  msg._item2 = item2;
  SendMsg(&msg, NMFGuaranteed);
}

int NetworkClient::ConsoleF(const char *format, ...)
{
  #if _ENABLE_DEDICATED_SERVER
  if (!IsDedicatedClient()) return 0;
	int VConsoleF(const char *format, va_list argptr);

  va_list arglist;
  va_start(arglist, format);

  int result = VConsoleF(format, arglist);
  
  va_end( arglist );
  return result;
  #else
  return 0;
  #endif
}

/*!
\patch 5175 Date 10/16/2007 by Jirka
- Fixed: Chat message length limited to 150 characters 
*/

void NetworkClient::Chat(int channel, RString text)
{
  ChatMessage msg;
  msg._channel = channel;
  msg._name = GetLocalPlayerName();
  msg._text = text.Substring(0, MaxChatLength);
  SendMsg(&msg, NMFGuaranteed|GetChatPriority());
}

void NetworkClient::Chat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text)
{
  ChatMessage msg;
  msg._channel = channel;
  msg._sender = SenderPerson(sender);
  msg._units = units;
  msg._name = GetLocalPlayerName();
  msg._text = text.Substring(0, MaxChatLength);
  SendMsg(&msg, NMFGuaranteed|GetChatPriority());
}

void NetworkClient::Chat(int channel, RString sender, RefArray<NetworkObject> &units, RString text)
{
  ChatMessage msg;
  msg._channel = channel;
  msg._sender = NULL;
  msg._units = units;
  msg._name = sender;
  msg._text = text.Substring(0, MaxChatLength);
  SendMsg(&msg, NMFGuaranteed|GetChatPriority());
}

void NetworkClient::LocalizedChat(int channel, const LocalizedFormatedString &text)
{
  LocalizedChatMessage msg;
  msg._channel = channel;
  msg._name = GetLocalPlayerName();
  msg._args = text._args;
  SendMsg(&msg, NMFGuaranteed|GetChatPriority());
}


void NetworkClient::LocalizedChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text)
{
  LocalizedChatMessage msg;
  msg._channel = channel;
  msg._sender = SenderPerson(sender);
  msg._units = units;
  msg._name = GetLocalPlayerName();
  msg._args = text._args;
  SendMsg(&msg, NMFGuaranteed|GetChatPriority());
}

void NetworkClient::LocalizedChat(int channel, RString sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text)
{
  LocalizedChatMessage msg;
  msg._channel = channel;
  msg._sender = NULL;
  msg._units = units;
  msg._name = sender;
  msg._args = text._args;
  SendMsg(&msg, NMFGuaranteed|GetChatPriority());
}


void NetworkClient::RadioChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text, RadioSentence &sentence)
{
  RadioChatMessage msg;
  msg._channel = channel;
  msg._sender = SenderPerson(sender);
  msg._units = units;
  msg._text = text;
  msg._sentence = sentence;
  msg._wordsClass = sentence.wordsClass;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::RadioChatWave(int channel, RefArray<NetworkObject> &units, RString wave, AIBrain *sender, RString senderName)
{
  RadioChatWaveMessage msg;
  msg._channel = channel;
  msg._units = units;
  msg._wave = wave;
  msg._sender = SenderPerson(sender);
  msg._senderName = senderName;
  SendMsg(&msg, NMFGuaranteed);
}

/// Notice VoNChatChannel has CCDirect on zero position (zero value is swapped with CCDirect)
/// this is in order to keep vonApp.hpp without knowledge of ChatChannels which are engine specific
VoNChatChannel ChatChannel2VoNChatChannel(int channel)
{
  if (channel == 0) return (VoNChatChannel)CCDirect;
  if (channel == CCDirect) return (VoNChatChannel)0;
  return (VoNChatChannel)channel;
}

/// inverse function to ChatChannel2VoNChatChannel
ChatChannel VoNChatChannel2ChatChannel(int channel)
{
  if (channel == 0) return CCDirect;
  if (channel == CCDirect) return (ChatChannel)0;
  return (ChatChannel)channel;
}

void NetworkClient::SetVoiceChannel(int channel)
{
#if !_GAMES_FOR_WINDOWS
 if (_client)  _client->VoiceCapture(channel!=CCNone,_parent->VoNCodecQuality());
  /// reset disable voice request
  GNetworkManager.SetDisableVoiceRequest(NetworkManager::DVRNone);
#endif
#if VOICE_OVER_NET && _ENABLE_MP
  if (_voiceChannel!=channel)
    VoiceSetChatChannel(ChatChannel2VoNChatChannel(channel));
#endif
  _voiceChannel = channel;
}

void NetworkClient::SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed)
{
  if (_client) _client->SetVoNPosition(dpnid, pos, speed);
}

void NetworkClient::SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed)
{
  if (_client) _client->SetVoNPlayerPosition(pos, speed);
}

void NetworkClient::SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT)
{
  if (_client) _client->SetVoNObstruction(dpnid, obstruction, occlusion, deltaT);
}

void NetworkClient::UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation)
{
  if (_client) _client->UpdateVoNVolumeAndAccomodation(dpnid, volume, accomodation);
}

void NetworkClient::AdvanceAllVoNSounds(float deltaT, bool paused, bool quiet)
{
  if (_client) _client->AdvanceAllVoNSounds(deltaT, paused, quiet);
}

void NetworkClient::UpdateCustomChannel(int channel, int ucr_type)
{
  CustomChannelInfo &info = GCustomChannels.GetCustomChannelInfo(channel);
  switch (ucr_type)
  {
  case UCR_Create:
    {
      CreateRadioChannelMessage msg;
      msg._channel = channel;
      msg._label = info.label;
      msg._callSign = info.callSign;
      msg._units = info.units;
      msg._color = (int)GChatList.GetColor(channel);
      SendMsg(&msg, NMFGuaranteed);
    }
    break;
  case UCR_CallSign:
    {
      RadioChannelCallSignMessage msg;
      msg._channel = channel;
      msg._callSign = info.callSign;
      SendMsg(&msg, NMFGuaranteed);
    }
    break;
  case UCR_Label:
    {
      RadioChannelLabelMessage msg;
      msg._channel = channel;
      msg._label = info.label;
      SendMsg(&msg, NMFGuaranteed);
    }
    break;
  case UCR_Units:
    {
      RadioChannelUnitsMessage msg;
      msg._channel = channel;
      msg._units = info.units;
      SendMsg(&msg, NMFGuaranteed);
    }
    break;
  }
}

/*!
Note: Files can be transferred only to players temporary folder on server -
path dest format must be "tmp\\players\\" + playername + "\\relativepath".
Transferred file size must not be bigger than MaxCustomFileSize.
All of this is enforced server side and clients violating this are kicked.
This is done to provide some reasonable level of security.
Otherwise fake clients would be able to write anywhere on the server disc.
\patch_internal 1.82 Date 8/19/2002 by Ondra
- Improved: Server-side check of tranferred file path and size.
*/

void NetworkClient::TransferFileToServer(RString dest, RString source)
{
  int maxSegmentSize = 512 - 50;

  QIFStreamB f;
  f.AutoOpen(source);

  TransferFileToServerMessage msg;
  msg._path = dest;
  msg._totSize = f.rest();
  msg._totSegments = (msg._totSize + maxSegmentSize - 1) / maxSegmentSize;
  msg._offset = 0;
  for (int i=0; i<msg._totSegments; i++)
  {
    msg._curSegment = i;
    int size = std::min(maxSegmentSize, msg._totSize - msg._offset);
    msg._data.Resize(size);
    f.read(msg._data.Data(), size);
    SendMsg(&msg, NMFGuaranteed);
    msg._offset += size;
  }
}

void NetworkClient::GetTransferStats(int &curBytes, int &totBytes)
{
  curBytes = 0; totBytes = 0;
  for (int i=0; i<_files.Size(); i++)
  {
    curBytes = _files[i].received;
    totBytes = _files[i].fileData.Size();
  }
}

NetworkLocalObjectInfo *NetworkClient::GetLocalObjectInfo(const NetworkId &id)
{
  const NetworkLocalObjectInfo *info = _localObjects.get(id);
  return unconst_cast(info);
}

NetworkRemoteObjectInfo *NetworkClient::GetRemoteObjectInfo(const NetworkId &id)
{
  const NetworkRemoteObjectInfo *info = _remoteObjects.get(id);
  return unconst_cast(info);
}

NetworkObject *NetworkClient::GetObject(const NetworkId &id)
{
  if ( id.creator == CreatorId(0) ) return NULL;
  else if ( id.creator == CreatorId(STATIC_OBJECT) )
  {
    ObjectId oid;
    oid.Decode(id.id);
    return GLandscape->GetObject(oid);
  }
  else
  {
    NetworkRemoteObjectInfo *infoR = GetRemoteObjectInfo(id);
    if (infoR) return infoR->object;
    NetworkLocalObjectInfo *infoL = GetLocalObjectInfo(id);
    if (infoL) return infoL->object;
    return NULL;
  }
}

void NetworkClient::ServerErrF(const char *format, ...)
{
  BString<512> buf;
  va_list va;
  va_start(va,format);
  vsprintf(buf,format,va);
  va_end(va);

  ClientDiagToServerMessage answer;
  answer._response = buf.cstr();
  SendMsg(&answer, NMFGuaranteed);
}

void NetworkClient::DeleteObject(const NetworkId &id, bool sendMsg)
{
  if (id.IsNull()) return;

#if CHECK_MSG
  CheckLocalObjects();
#endif

  if (DebugMeBoy(id))
  {
    LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} NetworkClient::DeleteObject", id.creator.CreatorInt(), id.id, _playerDpid);
  }
  if (_localObjects.removeKey(id))
  {
    if (DiagLevel >= 1)
      DiagLogF("Client: local object destroyed (DeleteObject function) %d:%" OID_FORMAT"", id.creator.CreatorInt(), id.id);
  }
  else
  {
    LogF("Warning: Client: Object info %d:%" OID_FORMAT" not found.", id.creator.CreatorInt(), id.id);
  }

  // the item can be both in local and remote objects (registered for different types of updates)
  _remoteObjects.removeKey(id);

  if (sendMsg)
  {
    if (IsBotClient())
    {
      DeleteObjectMessage msg;
      msg._creator = id.creator;
      msg._id = id.id;
      SendMsg(&msg, NMFGuaranteed);
    }
    else
    {
      ServerErrF("Error: Client removing object %d:%" OID_FORMAT" in NetworkClient::DeleteObject", id.creator.CreatorInt(), id.id );
    }
  }


#if CHECK_MSG
  CheckLocalObjects();
#endif
}

void NetworkClient::ForceDeleteObject(NetworkObject *object)
{
  NetworkId id = object->GetNetworkId();
  if (object->IsLocal())
  {
    DeleteObject(id, true);
  }
  else
  {
    // first send the message, as DestroyRemoteObject may invalidate the object
    ForceDeleteObjectMessage msg;
    msg._object = object;
    SendMsg(&msg, NMFGuaranteed);

    DoAssert(GetNetworkManager().IsServerOwner());
    DestroyRemoteObject(id);
    // the object can be listed both as remote and local now
    _localObjects.removeKey(id);
    
  }
}

//! Subsidiary structure for sorting messages by error
struct UpdateLocalObjectInfo
{
  //! Local object info
  NetworkLocalObjectInfo *oInfo;
  //! Message class type
  NetworkMessageClass cls;
  //! Error (difference)
  float error;
  //! Critical update
  bool criticalUpdate;
};
TypeIsSimple(UpdateLocalObjectInfo)

//! Compare local object infos by error
int CmpLocalObjects
(
  const UpdateLocalObjectInfo *info1,
  const UpdateLocalObjectInfo *info2
)
{
  float diff = info2->error - info1->error;
  return sign(diff);
}

//! Format value of error for output
/*!
\param val value to format
\param buffer output buffer
\return pointer to output buffer
*/
const char *FormatVal(float val, char *buffer)
{
  if (val == FLT_MAX)
    strcpy(buffer, "MAX");
  else
    sprintf(buffer, "%.0f", val);
  return buffer;
}

void NetworkClient::EstimateBandwidth(int &nMsgMax, int &nBytesMax)
{
  if (IsBotClient())
  {
    // unlimited bandwidth
    nMsgMax = INT_MAX;
    nBytesMax = INT_MAX;
  }
  else
  {
  #if _ENABLE_MP
    int nMsg = 0, nBytes = 0, nMsgG = 0, nBytesG = 0;
    _client->GetSendQueueInfo(nMsg, nBytes, nMsgG, nBytesG);
    if ( nMsgG > 0 )    // new style?
    {
        nMsg += nMsgG;
        nBytes += nBytesG;
    }
  #endif
/*
  #if _ENABLE_CHEATS
    if (outputDiags%NOutputDiags == ODMsgQueue && nMsg > 0) sprintf(output, "Waiting (%d, %d); ", nBytes, nMsg);
  #endif
*/
    nMsgMax = MaxMsgSend;
    nBytesMax = 1024;

    #if _ENABLE_MP
    int latencyMS, throughputBPS;
    if (_client->GetConnectionInfo(latencyMS, throughputBPS))
    {
      // nBytesMax += 1.25 * info.dwThroughputBPS;
      nBytesMax += throughputBPS;
      nBytesMax += throughputBPS >> 2; // info.dwThroughputBPS / 4
/*
  #if _ENABLE_CHEATS
      if (outputDiags%NOutputDiags == ODMsgQueue) sprintf(output + strlen(output), "%d ms, %d bps; ", latencyMS, throughputBPS*8);
  #endif
*/
      if (_connectionQuality < CQPoor)
      {
        if (latencyMS >= 500) _connectionQuality = CQPoor;
        // if (throughputBPS < 3000) _connectionQuality = CQPoor;
      }
    }
    int nPlayers = _identities.Size() - 1;
  //  if (nPlayers > 0) saturateMax(nBytesMax, 14400 / nPlayers);
    if (nPlayers > 0) saturate(nBytesMax, MinBandwidth/8, MaxBandwidth/8);

    nBytesMax = toInt(0.001 * GEngine->GetAvgFrameDuration() * nBytesMax);

    nBytesMax -= nBytes;
    nMsgMax -= nMsg;
    #endif

  #if _ENABLE_CHEATS
    if (outputLogs) LogF("Client: Limit (%d, %d)", nBytesMax, nMsgMax);
/*
    if (outputDiags%NOutputDiags == ODMsgQueue) sprintf(output + strlen(output), "Limit (%d, %d); ", nBytesMax, nMsgMax);
*/
  #endif
  }
}

bool NetworkUpdateInfo::CheckReject(NetworkClient *client, NetworkMessageClass cls, NetworkObject *obj)
{
  if (rejected) return rejected;
  // TODO: sometimes we may want to reject outgoing message (i.e. reject straigt on client)
  // see https://projects.bistudio.com/dayz/Re_Dayz-client__server-v�e_garantovan�-postup-divn�_chov�n�_batoh�.txt
  /*
  bool rejectedMsg = lastSentMsg && obj->RejectMsg(cls,lastSentMsg);
  if (rejectedMsg)
  {
    LogF("Rejected outgoing %d for %s (%" OID_FORMAT ")",cls,cc_cast(obj->GetDebugName()),obj->GetNetworkId().id);
    rejected = true;
    client->GetServer()->OnRejectChanged(obj->GetNetworkId(),cls,rejected);
  }
  */
  return rejected;
}

void NetworkClient::CreateObjectsList(AutoArray<UpdateLocalObjectInfo, MemAllocSA> &objects)
{
  PROFILE_SCOPE_EX(crOLC, network)
  float maxError = 0, sumError = 0;
  for (LocalObjectsContainer::Iterator it=_localObjects.begin(); it; _localObjects.next(it))
  {
    NetworkLocalObjectInfo &oInfo = *it;
    
    for (int j=NMCUpdateFirst; j<NMCUpdateN; j++)
    {
      NetworkMessageClass cls = (NetworkMessageClass)j;
      NetworkUpdateInfo &info = oInfo.updates[j];

      if (GetPlayer()==BOT_CLIENT)
      {
        if (!(
          oInfo.object->IsLocal() || oInfo.object->CheckSendUpdateToOwner(cls) || info.CheckReject(this,cls,oInfo.object)
        )) continue;
      }
      else
      {
        DoAssert(oInfo.object->IsLocal());
        if (!oInfo.object->CheckSendUpdateToServer(cls)) continue;
      }

      float error;
      {
        NetworkMessageType type = oInfo.object->GetNMType(cls);
        if (type == NMTNone) continue;
        if (type == NMTUpdateClientInfoDpid) continue;
        
        if (!info.lastSentMsg)
        {
          // error = FLT_MAX;
          error = oInfo.object->CalculateErrorInitial(cls);
        }
        else
        {
        /*
          NetworkMessageFormatBase *format = GetFormat(type);
          if (!format)
          {
            RptF("Client: Bad message %d(%s) format", (int)type, cc_cast(GetMsgTypeName(type)));
            continue;
          }
          */
          NetworkMessageError errorList;
          NetworkMessageContextWithError ctx(info.lastSentMsg, this, TO_SERVER, MSG_RECEIVE, errorList);
          ctx.SetClass(cls);
          error = oInfo.object->CalculateError(ctx);
#if LOG_CLIENT_ERRORS
          if (!(error < LogClientErrorLimit))
          {
            DiagLogF
            (
              "Object %s %d:%" OID_FORMAT"",
              cc_cast(GetMsgTypeName(type)), oInfo.id.creator.CreatorInt(), oInfo.id.id
            );
            DiagLogF
            (
              "  - error %.3f", error
            );
            float dummy = oInfo.object->CalculateError(ctx);
            DoAssert(dummy == error);
          }
#endif
          #if _ENABLE_REPORT
            if (!_finite(error))
            {
              RptF(
                "Client: Infinite error %g detected on %s",
                error,(const char *)oInfo.object->GetDebugName()
              );
              // recalculate: debuggin opportunity
              oInfo.object->CalculateError(ctx);
            }
          #endif
        }
        // note: following condition seems to be identical to error>MinErrorToSendNear
        // but it is different when error is NaN
        if (!(error <= MinErrorToSendNear))
        {
          int index = objects.Add();
          objects[index].oInfo = &oInfo;
          objects[index].cls = cls;
          objects[index].error = error;
          objects[index].criticalUpdate = false;
          #if _ENABLE_REPORT
            if (!_finite(error))
            {
              RptF(
                "Client: Infinite error error %g, caught on %s",
                error,(const char *)oInfo.object->GetDebugName()
              );
            }
          #endif
        }
        saturateMax(maxError, error);
        sumError += error;
      }
    }
  }
/*
#if _ENABLE_CHEATS
  if (outputDiags%NOutputDiags == ODMsgQueue && sumError > 0)
  {
    char buf1[32], buf2[32];
    sprintf
    (
      output + strlen(output),
      "Err %s / %s",
      FormatVal(maxError, buf1),
      FormatVal(sumError, buf2)
    );
    GlobalShowMessage(1000, output);
  }
#endif
*/

  // sort local objects by errors
  QSort(objects.Data(), objects.Size(), CmpLocalObjects);
}

void NetworkClient::SendMessages()
{
  // calculate limits
  int nMsg = 0, nBytes = 0;
  int nMsgMax, nBytesMax;
  
  EstimateBandwidth(nMsgMax, nBytesMax);

  // send enqueued guaranteed messages

  const int nMsgMaxGuaranteed = nMsgMax;
  const int nBytesMaxGuaranteed = nBytesMax;

  int maxSize = MaxSizeGuaranteed;
  while (_messageQueue.Size() > 0)
  {
    if (nMsg >= nMsgMaxGuaranteed || nBytes >= nBytesMaxGuaranteed)
    {
/*
#if _ENABLE_CHEATS
      if (outputDiags%NOutputDiags == ODMsgQueue)
      {
        strcat(output, "Limit reached");
        GlobalShowMessage(1000, output);
      }
#endif
*/
      break;
    }

    NetworkMessage *msg = _messageQueue[0]->msg;
    int size = msg->size;
    if (size >= maxSize)
    {
      RefR<NetMessage, RefCountSafe> resultMsg;
      SendMsgRemote(TO_SERVER, msg, _messageQueue[0]->type, NMFGuaranteed|NMFStatsAlreadyDone, &resultMsg, NULL);
      _messageQueue[0]->SetFence(resultMsg);
      _messageQueue.Delete(0);
      nMsg++;
      nBytes += size;
    }
    else
    {
      int totalSize = 0;
      int i;
      for (i=0; i<_messageQueue.Size(); i++)
      {
        NetworkMessage *msg = _messageQueue[i]->msg;
        int size = msg->size;
        if (totalSize + size > maxSize) break;
        totalSize += size;
      }
      SendMsgQueue(TO_SERVER, _messageQueue, 0, i, NMFGuaranteed);
      _messageQueue.Delete(0, i);
      nMsg++;
      nBytes += totalSize;
    }
  }

  AUTO_STATIC_ARRAY(UpdateLocalObjectInfo, objects, 256);

#define ENABLE_CRITICAL_UPDATES 1

#if !ENABLE_CRITICAL_UPDATES
  if (nMsg >= nMsgMax || nBytes >= nBytesMax)
  {
  #if _ENABLE_CHEATS
    if (outputLogs) LogF("  Client: limit reached (guaranteed)");
  #endif
    goto DoNotSendUpdates;
  }
#endif

  if (_clientState == NCSPlaying)
  {
  #if CHECK_MSG
    CheckLocalObjects();
  #endif

    // delete destroyed objects - keep order
    for (LocalObjectsContainer::Iterator it=_localObjects.begin(); it; _localObjects.next(it))
    {
      NetworkLocalObjectInfo &info = *it;
      if (!info.object)
      {
        if (IsBotClient())
        {
          DeleteObjectMessage msg;
          msg._creator = info.id.creator;
          msg._id = info.id.id;
          SendMsg(&msg, NMFGuaranteed);
          if (DiagLevel >= 1)
            DiagLogF("Client: local object destroyed (object not found) %d:%" OID_FORMAT"", info.id.creator.CreatorInt(), info.id.id);
        }
        else
        {
          ServerErrF("Error: Client removing object %d:%" OID_FORMAT" in NetworkClient::SendMessages", info.id.creator.CreatorInt(), info.id.id );
        }
        _localObjects.removeKey(info.id); // does not invalidate the iterator - value replaced by a zombie
        _remoteObjects.removeKey(info.id);
        continue;
      }
    }

  #if CHECK_MSG
    CheckLocalObjects();
  #endif

    // update errors of local objects
    CreateObjectsList(objects);

    if (!_parent->IsServer())
    {
      ClientInput *obj = _clientInput;
      #if _PROFILE && ENABLED_FEATURE(SIMFORWARD)
      DIAG_MESSAGE(500,"State UAMoveForward %.2f",_actionState[UAMoveForward]);
      #endif
      if (!_clientInputLast || obj->Compare(_clientInputLast))
      {
        // TODO: bandwith control
        NetworkMessageType type = obj->GetNMType(NMCUpdateGeneric);
        Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
        msg->time = Glob.time;
        NetworkMessageContext ctx(msg, this, TO_SERVER, MSG_SEND);
        ctx.SetClass(NMCUpdateGeneric);
        obj->TransferMsg(ctx);

        SendMsg(TO_SERVER,msg,type,NMFGuaranteed, NULL);

        // store after sending (TransferMsg may modify it)
        if (!_clientInputLast) _clientInputLast = new ClientInputState(*obj);
        else *_clientInputLast = *obj;
      }
    }
#if DEBUG_GIVEN_NETWORK_ID>1
  for (int i=0; i<objects.Size(); i++)
  {
    NetworkLocalObjectInfo &info = *objects[i].oInfo;
    float error = objects[i].error;
    if (DebugMeBoy(info.id))
    {
      float times[NMCUpdateN];
      for (int j=0; j<NMCUpdateN; j++) times[j] = info.updates[j].lastSentMsg ? info.updates[j].lastSentMsg->time.toFloat() : 0;
      LogF("*** DebugMeBoy: {%d:%" OID_FORMAT"} has error %.2f, position in _objects = %d, lastMsgSent: %.02f, %.02f, %.02f", 
          info.id.creator.CreatorInt(), info.id.id, error, i, times[NMCUpdateGeneric], times[NMCUpdatePosition], times[NMCUpdateDamage]);
    }
  }
#endif

    // create and send critical updates     
#if ENABLE_CRITICAL_UPDATES
    PROFILE_SCOPE_EX(UOcrC, network)
    for (int i=0; i<objects.Size(); i++)
    {
      NetworkLocalObjectInfo &info = *objects[i].oInfo;
      NetworkMessageClass cls = objects[i].cls;

      float error = objects[i].error;

      // any message about dammage that transmit change
      // that could mean dead should be considered high priority
      if
      (
        (cls != NMCUpdateDamage || error < ERR_COEF_STRUCTURE) && error<ERR_COEF_IMMEDIATE
      ) continue;

      objects[i].criticalUpdate = true;
      int bytes;
      if (!info.object || !info.object->_fence || info.object->_fence->WasReceived())
      {
        bytes = UpdateObject(info.object, cls, NMFGuaranteed | NMFHighPriority);
      }
      else
      {
        bytes = UpdateObject(info.object, cls, NMFGuaranteed);
      }

    #if _ENABLE_CHEATS
      if (outputLogs)
      {
        LogF
        (
          "  Client: Object %d:%" OID_FORMAT" updated - size %d bytes, critical",
          info.id.creator.CreatorInt(), info.id.id, bytes
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

    if (nMsg >= nMsgMax || nBytes >= nBytesMax)
    {
    #if _ENABLE_CHEATS
      if (outputLogs) LogF("  Client: limit reached (guaranteed)");
    #endif
      goto DoNotSendUpdates;
    }

#endif
  }

DoNotSendUpdates:
  {
    PROFILE_SCOPE_EX(UOngC, network)
    // send nonguaranteed enqueued messages
    maxSize = MaxSizeNonguaranteed;
    int next = 0; // index of next object to update
    bool empty = false;
    while (true)
    {
      // enforce send last message
      if (!empty || _messageQueueNonGuaranteed.Size() <= 0)
      {
        if (nMsg >= nMsgMax || nBytes >= nBytesMax) break;
      }

      if (_messageQueueNonGuaranteed.Size() <= 0)
      {
        empty = true;
        // starving, try to create further message
  int oldNext = next;
        if (!PrepareNextUpdate(objects, next)) break;
        // local communication - message was already sent
        if (_parent->IsServer())
        {
          DoAssert(_messageQueueNonGuaranteed.Size() == 0);
          nMsg++;
          continue;
        }

        // DoAssert(_messageQueueNonGuaranteed.Size() > 0);
        // Temporary FIX:
        if (_messageQueueNonGuaranteed.Size() == 0)
        {
  RptF("PrepareNextUpdate - no message was prepared - next %d, object 0x%x", oldNext, &objects[oldNext]);
  PrepareNextUpdate(objects, oldNext);        
          nMsg++;
          continue;
        }
      }
    
      NetworkMessageQueueItem *item = _messageQueueNonGuaranteed[0];
      int size = item->msg->size;
      // enforce send last message
      if (size >= maxSize || nMsg >= nMsgMax || nBytes >= nBytesMax)
      {
        RefR<NetMessage, RefCountSafe> resultMsg;
        SendMsgRemote(TO_SERVER, item->msg, item->type, NMFStatsAlreadyDone, &resultMsg, item->dependOn);
        //item->SetFence(resultMsg); //nonguaranteed messages could not raise a fence
        _messageQueueNonGuaranteed.Delete(0);
        nMsg++;
        nBytes += size;
      }
      else
      {
        int totalSize = 0; //controls the cumulative size of agregated message
        int i;
        for (i=0; true; i++)
        {
          if (i >= _messageQueueNonGuaranteed.Size())
          {
            empty = true;
            // starving, try to create further message
            if (!PrepareNextUpdate(objects, next)) break;
            DoAssert(!_parent->IsServer());
            DoAssert(i < _messageQueueNonGuaranteed.Size());
          }
        
          NetworkMessageQueueItem &item = *_messageQueueNonGuaranteed[i];
          int size = item.msg->size;
          if (totalSize + size > maxSize) break;
          totalSize += size;
        }

        for (int j=0; j<i; j++)
        {
          NetworkMessageQueueItem &item = *_messageQueueNonGuaranteed[j];

          if (item.dependOn)
          { //it depends on previous guaranteed update and cannot be agregated. We sent it immediately by setting begin
            RefR<NetMessage, RefCountSafe> resultMsg;
            SendMsgRemote(TO_SERVER, item.msg, item.type, NMFStatsAlreadyDone, &resultMsg, item.dependOn);
            //item.SetFence(resultMsg);  //nonguaranteed messages could not raise a fence
            _messageQueueNonGuaranteed.Delete(j);
            i--; //one less items to process
            j--; //the same index contains new data
            nMsg++;
          }
        }

        if (i>0) //it may happen there were only messages with dependOn set
        {
          SendMsgQueue(TO_SERVER, _messageQueueNonGuaranteed, 0, i, NMFNone);
          _messageQueueNonGuaranteed.Delete(0, i);
          nMsg++;
        }
        nBytes += totalSize;
      }
    }
    DoAssert(!empty || _messageQueueNonGuaranteed.Size() == 0);

  //  int sumError = 0; // messages not sent total error
    if (next < objects.Size())
    {
    #if _ENABLE_CHEATS
      if (outputLogs) LogF("  Client: limit reached");
    #endif
  /*
      for (int i=next; i<objects.Size(); i++)
      {
        if (objects[i].error < FLT_MAX)
        {
          sumError += objects[i].error;
        }
      }
  */
    }
  }

#if CHECK_MSG
  CheckLocalObjects();
#endif

#if _ENABLE_CHEATS
  // remove statistics info
  static AutoArray<int> texts;
  for (int i=0; i<texts.Size(); i++)
  {
    if (texts[i] >= 0) GEngine->RemoveText(texts[i]);
  }
  texts.Resize(0);

  // create statistics
  if (outputDiags == ODMsgQueue)
  {
    int count = _messageQueue.Size(), size = 0;
    for (int i=0; i<count; i++) size += _messageQueue[i]->msg->size;

    RString output = Format("Server: HLWait%3d(%5dB)", count, size);
    texts.Add(GEngine->ShowTextF(1000, output));
    texts.Add(GEngine->ShowTextF(1000, _client->GetStatistics()));
  }
#endif
}

bool NetworkClient::PrepareNextUpdate(AutoArray<UpdateLocalObjectInfo, MemAllocSA> &objects, int &next)
{
  while (next < objects.Size())
  {
    UpdateLocalObjectInfo &info = objects[next++];
    if (info.criticalUpdate) continue;

/*
    #if _ENABLE_REPORT
    int oldCount = _messageQueueNonGuaranteed.Size();
    #endif
*/

    NetworkLocalObjectInfo &oInfo = *info.oInfo;
    NetworkMessageClass cls = info.cls;

    int bytes = UpdateObject(oInfo.object, cls, NMFNone);
    if (bytes < 0) continue;

/*
    #if _ENABLE_REPORT
    if (oldCount == _messageQueueNonGuaranteed.Size())
    {
      Fail("No message added during NetworkClient::PrepareNextUpdate");
      continue;
    }
    #endif
*/
  #if _ENABLE_CHEATS
    if (outputLogs) LogF("  Client: Object updated - size %d bytes", bytes);
  #endif
    return true;
  }
  return false;
}


int NetworkClient::NLocalObjectsNULL() const
{
  int n = 0;
  for (LocalObjectsContainer::Iterator it=_localObjects.begin(); it; _localObjects.next(it))
  {
    NetworkLocalObjectInfo &info = *it;
    if (!info.object) n++;
  }
  return n;
}

float NetworkClient::GetLastMsgAgeReliable()
{
  if (_client) return _client->GetLastMsgAgeReliable();
  return 0;
}

void NetworkClient::CleanupPlayer(int dpnid, NetworkId id)
{
  NetworkObject *obj = NULL;
  if (!id.IsNull())
  {
    obj = GetObject(id);
    if (!obj)
    {
      RptF("Warning: Cleanup player - person %d:%" OID_FORMAT" not found", id.creator.CreatorInt(), id.id);
    }
  }
}

void NetworkClient::HackedDataDetected(RString filename)
{
  HackedDataMessage msg;
  msg._type = HTHackedData;
  msg._filename = filename;
  SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AddPublicVariableEventHandler(RString name, GameValuePar code)
{
  DoAssert(code.GetType() == GameCode);
  PublicVariableEventHandler item;
  item._name = name;
#if USE_PRECOMPILATION
  item._code = static_cast<GameDataCode *>(code.GetData());
#else
  item._code = code;
#endif
  _publicVariableEventHandlers.Add(item);
}

void NetworkClient::SynchronizeVariable( EntityAI * veh, RString name, float coef, const GameDataCode & handler )
{
  SynchronizeVariableMessage msg;
  msg._veh = veh;
  msg._name = name;
  msg._coef = coef;
  msg._handler = handler.GetString();
  SendMsg(&msg, NMFGuaranteed);
}

void ListPublicVariableEHFunc(const PublicVariableEventHandler &eh, const PublicVariableEventHandlers *dummy1, void *pubVarEH)
{
  AutoArray<PublicVariableEventHandler> &publicVarEH = *static_cast<AutoArray<PublicVariableEventHandler> *>(pubVarEH);
  publicVarEH.AddFast(eh);
}

/// functor for create object traversal
class NetworkClientCreateTurret : public ITurretFunc
{
protected:
  NetworkClient *_client;

public:
  NetworkClientCreateTurret(NetworkClient *client) : _client(client) {}
  bool operator () (EntityAIFull *entity, TurretContext &context)
  {
    if (context._turret && !context._turret->GetNetworkId().IsNull())
    {
      Transport *veh = dyn_cast<Transport>(entity);
      if (veh)
      {
        TurretPath path;
        bool ok = veh->GetTurretPath(path, context._turret);
        DoAssert(ok);
        _client->CreateSubobject(veh, path, context._turret, false); //do not create NetworkId
      }
    }
    return false; // continue
  }
};

DWORD NetworkClient::SendMsg
(
  int to, NetworkMessage *msg,
  NetworkMessageType type, NetMsgFlags dwFlags,
  NetworkObject *object
)
{
#if _ENABLE_MP
  if (!_client) return -1;
//  NetworkMessageFormatBase *format = GetFormat(type);
//  if (!format) return 0xffffffff;

  NetworkServer *server = _parent->GetServer();
  if (server)
  {
    msg->size = 0;
#if _ENABLE_CHEATS
    StatMsgSent(type, CalculateMessageSize(msg, type), dwFlags);
#endif
    server->AddLocalMessage(BOT_CLIENT, type, msg);
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
      SendMsgRemote(to,msg,type,dwFlags|NMFStatsAlreadyDone, object ? &object->_fence : NULL, NULL);
      return 0;
    }
    else
    {
      EnqueueMsg(to, msg, type, object);
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
    if (object && object->_fence)
    {
      if ( object->_fence->WasReceived() ) object->_fence = NULL;
      else fence = object->_fence;
    }
    EnqueueMsgNonGuaranteed(to, msg, type, fence);
    return 1;
  }
#else
    return E_FAIL;
#endif
}


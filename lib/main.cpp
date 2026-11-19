/*!
\file
Basic win32 framework, application init and message handling.
*/

#include "wpch.hpp"

#ifdef _WIN32
  #include <io.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
  #include <process.h>
#else
  #include <sys/time.h>
  #include <signal.h>
#endif
#include <time.h>
#ifdef _WIN32
  #include <El/Debugging/imexhnd.h> 
#endif
#include "resrc1.h"
#include "versionNo.h"
#include "appName.h"
#include <Es/Strings/bString.hpp>
#include <Es/Containers/hashMap.hpp>
#include <Es/Containers/forEach.hpp>
#include <es/Framework/appFrame.hpp>

#include <El/HiResTime/hiResTime.hpp>

#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>

#include <El/Common/randomGen.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/Files/commandLine.hpp>
#include <El/DebugConsole/debugConsole.hpp>
#include <El/Modules/modules.hpp>

#include "global.hpp"
#include "keyInput.hpp"
#include "saveGame.hpp"

#include "diagModes.hpp"

#include <El/QStream/qbStream.hpp>
#include "world.hpp"
#include "progress.hpp"
#include "engine.hpp"
#include "AI/ai.hpp"
#include "AI/fsmScripted.hpp"
#include "fsmEntity.hpp"
#include "arcadeTemplate.hpp"
#include <El/CRC/crc.hpp>
#include "allAIVehicles.hpp"
#include <lib/Inventory/recipe.hpp>

#include <El/Debugging/debugTrap.hpp>

#include <El/Interfaces/iDebugEngine.hpp>

#include <El/Multicore/multicore.hpp>
#include <El/Multicore/jobs.hpp>

#include <El/Evaluator/express.hpp>
#include <El/Multicore/circularQueue.hpp>
#include <El/FileServer/fileServerAsync.hpp>
#include <El/HiResTime/hiResTime.hpp>
#include "registration.hpp"

#ifdef _WIN32
#include <richedit.h>
#endif

#include "UI/uiControls.hpp"
#include "sleepyAPI.hpp"

#include <El/DLC/DLCTools.h>

#include <El/DataSignatures/dataSignatures.hpp>
#if _ENABLE_PBO_PROTECTION
#include <El/Encryption/PBOHeaderProtection.h>
#endif

#include <Es/Files/fileContainer.hpp>

#include <El/Network/netpch.hpp>
#include <El/Network/netpeer.hpp>

#include "jvmInit.hpp"

#include <Steamworks/steam_api.h>

#include "system/enf_timer.h"
#include "d3d9/engdd9.hpp"

#if _ENABLE_STEAM
//# pragma comment(lib, "steam_api")
  bool UseSteam						= true;
	bool SteamOverlayClosed	= false;
#endif

enum SoundProvider { SPXAudio2 };
bool CloseRequest;

// define used sound system
static SoundProvider UsedSoundProvider = SPXAudio2;

#if defined _WIN32
  #include "soundXA2.hpp"
#endif

#include "UI/chat.hpp"
#include "fileLocator.hpp"
#include "gameDirs.hpp"
#include "landscape.hpp"
#include "UI/missionDirs.hpp"

#include <El/ParamFile/paramFile.hpp>
//#include "loadStream.hpp"
#ifdef _WIN32
  #include "joystick.hpp"
  #include "trackIR.hpp"
  #include "freeTrack.hpp"
#if _SPACEMOUSE
  #include "spaceMouse.hpp"
#endif
#endif

//#include "strIncl.hpp"
#include "stringtableExt.hpp"

#ifndef _WIN32
  #include <El/FileServer/fileServerMT.hpp>
#else
  #include <El/FileServer/fileServer.hpp>
#endif

#include <Es/Types/lLinks.hpp>

#include "engineDll.hpp"

#include "Network/network.hpp"

#include "integrity.hpp"
#include "Protect/selectProtection.h"

#include "mbcs.hpp"

#include "gameDirs.hpp"

#include "WorldRes/WorldRes.h"
#include "WorldRes/WorldLoot.h"

#if PROFILE
  void EnableProfiler(){}
  void DisableProfiler(){}

  #define PROFILE_INIT 1
  #define PROFILE_RUN 1
  #define PROFILE_EXIT 1
#endif

#if _ACCEPT_ONLY_SIGNED_DATA
#include <El/DataSignatures/dataSignatures.hpp>
#endif

/*
test of enfusion engine
*/
//#define ENF_ENGINE

#ifdef ENF_ENGINE
	#include "engine/enf_winengine.h"
#endif



extern ParamFile Pars;
extern ParamFile ExtParsMission;

bool StartMinimized=false;

#if USE_WINDOW
  bool UseWindow=true;
  bool UseWindowByCommandLine = true;
#else
  bool UseWindow=false;
  bool UseWindowByCommandLine = false;
#endif

/// should we pause when application is not active?
static bool PauseInBackround=true;

void ProcessKeyboard(DWORD sysTime, DWORD timeDelta);

#if ENABLE_KEYMOUSE
	bool CreateKeyboard(HWND hwnd, bool firstTime);
	bool CreateMouseDevice(HWND hwnd, bool exclusive);
	void DestroyMouseDevice();

	void ProcessMouseBuffer(const RAWMOUSE& data);
	void ProcessKeyboardBuffer(const RAWKEYBOARD& data);
#endif


#ifdef _WIN32
extern INSTANCE_HANDLE  hInstApp;
#else
typedef void * WINDOW_HANDLE;
#endif
extern WINDOW_HANDLE    hwndApp;

WINDOW_HANDLE   hwndSplash;

#ifdef _WIN32
  #define SVN_AUTO_UPDATE 1
#else 
  #define SVN_AUTO_UPDATE 0
#endif

static bool AutoUpdate=false;

#if SVN_AUTO_UPDATE
/// use SvnAutoUpdater to check for new version and restart the game with new version whenever available
#include "SvnUpdate/svnAutoUpdateClient.h"

// implemented in displayUI.cpp
bool CreateDedicatedClient(RString ip, int port, RString password);

class SVNArmaAutoUpdaterClient : public SVNAutoUpdaterClient
{
  typedef SVNAutoUpdaterClient base;
  friend DWORD CALLBACK AutoUpdateThread(LPVOID parameter);

  //! connectTo parameters from config used for connecting to server pool from where these can be taken through sendAUMessage
  RString _connectToPool;
  //! actual connection parameters
  RString _connectTo;
  int     _port;
  RString _password;

  Event _eventReady;
  HANDLE _autoUpdateThread;

  bool Update();

public:
  SVNArmaAutoUpdaterClient() : _port(2302), _shutdowning(false), _askedToShutDown(false), _restartAfterShutDown(false), _eventReady(true) {}
  //! read svn url and working directory from autoUpdater config
  bool Init();
  void UpdateCommands();
  void ProcessAUCommand(RString command);
  bool ShutDowning() const { return _shutdowning; }
  void SetShutDown(bool restart) { _shutdowning = true; _restartAfterShutDown = restart;}
  bool RestartAfterShutDown() const { return _restartAfterShutDown; }
  void HandleAskToShutDown();  
private:
  void ParseConnect(RString connectTo);

  bool _shutdowning;
  bool _askedToShutDown;
  bool _restartAfterShutDown;

#if 0
  // _url and _wd are hardcoded on DayZUpdater server side
  RString _url, _wd;
#endif
};

bool SVNArmaAutoUpdaterClient::Init()
{
  //! read possible AutoUpdate config
  const char *cfgPath = "SvnUpdater\\config\\config.cfg";
  GameDataNamespace globals(NULL, RString(), false); // no global namespace
  ParamFile config;
  config.Parse(cfgPath, NULL, NULL, &globals);
  ParamEntryPtr cfg = config.FindEntry("connectTo");
  if ( cfg )
  {
    _connectToPool = config >> "connectTo";
    ParseConnect(_connectToPool);
  }
#if 0
  // _url and _wd are hardcoded on DayZUpdater server side
  cfg = config.FindEntry("SVN");
  if ( cfg )
  {
    if ( cfg->FindEntry("url") ) _url = *cfg >> "url";
    if ( cfg->FindEntry("updateDir") ) _wd = *cfg >> "updateDir";
  }
#endif
  
  if (!base::Init("SvnUpdater\\DayZUpdater.exe") ) return false;
  // start Update thread
  _eventReady.Reset();

  DWORD threadId;
  DWORD CALLBACK AutoUpdateThread(LPVOID parameter);
  _autoUpdateThread = CreateThread(NULL,64*1024,&AutoUpdateThread,NULL,CREATE_SUSPENDED,&threadId);

  SetThreadPriority(_autoUpdateThread,THREAD_PRIORITY_LOWEST);
  ResumeThread(_autoUpdateThread);

  // wait until allowed to continue
  _eventReady.Wait();  //when AccessViolation, event is set but _shutDowning==true
  _eventReady.Done();

  return !_shutdowning;
}

RString ClientIP;

void SVNArmaAutoUpdaterClient::ParseConnect(RString connectTo)
{
  if (!ClientIP.IsEmpty()) // -connect=where.to.connect has priority over config
  {
    connectTo = ClientIP;
  }
  const char *substr = strchr(cc_cast(connectTo), '@');
  if (substr)
  { //there is password before '@'
    _password = connectTo.Substring(0,(substr-cc_cast(connectTo)));
    connectTo = substr + 1;
  }
  else _password = RString("");
  substr = strchr(cc_cast(connectTo), ':');
  if (substr)
  {
    _port = atoi(substr+1);
    _connectTo = connectTo.Substring(0, (substr-cc_cast(connectTo)));
  }
  else
  {
    _port = 2302;
    _connectTo = connectTo;
  }
}

//@{ PipeManager initialization and commands
// Commands Sending by AutoUpdater (server)
const std::string CmdReady("ready");
const std::string CmdThx("thanks for ");
const std::string CmdShutDown("shutDown");
const std::string CmdUpToDate("up-to-date");
const std::string CmdCheckout("checkouting");
const std::string CmdOutdated("updating");
const std::string CmdAccessViolating("accessViolating");  //server wants the client to stop immediately
//@}
const std::string CmdRestartMe("restartMe");       //app wants to be restarted after update is over

bool SVNArmaAutoUpdaterClient::Update()
{
  if (!base::IsReady()) return true;

  if (!_pipeManager.Update())
  {
    Close();
    _ready = false;
    return false; //end AutoUpdateThread
  }

  // process all commands
  for (std::string command=_pipeManager.Receive(); !command.empty(); command=_pipeManager.Receive())
  {
    if ( !strnicmp(command.c_str(), CmdThx.c_str(), CmdThx.length()) )
    {
      LogF("<%s>\n", command.c_str());
    }
    else if ( !stricmp(command.c_str(), CmdShutDown.c_str()) )
    {
      LogF("<%s>\n", command.c_str());
      _askedToShutDown = true;
    }
    else if ( !stricmp(command.c_str(), PipeManager::CmdClosing) )
    {
      LogF("The SVNAutoUpdater has finished!\n");
    }
    else if ( !stricmp(command.c_str(), CmdUpToDate.c_str()) )
    {
      LogF("The current version is up-to-date!\n");
    }
    else if ( !stricmp(command.c_str(), CmdOutdated.c_str()) )
    {
      LogF("The current version is outdated! Svn update in progress...\n");
    }
    else if ( !stricmp(command.c_str(), CmdCheckout.c_str()) )
    {
      LogF("The aplication is not under svn control yet, checkout in progress...\n");
    }
    else if ( !stricmp(command.c_str(), PipeManager::CmdFailed) )
    {
      LogF("The autoUpdate functionality is not available!\n");
    }
    else if  ( !stricmp(command.c_str(), CmdAccessViolating.c_str()) ) 
    {
      LogF("<%s>\n", command.c_str());

      // restart me after autoUpdate is over
      _pipeManager.Send(CmdRestartMe); 

      _shutdowning = true;
      _eventReady.Set();
      return false; //immediately end application
    }
    else if  ( !stricmp(command.c_str(), CmdReady.c_str()) ) 
    {
      LogF("<%s>\n", command.c_str());
      _eventReady.Set();
    }
    else LogF("ERROR UNKNOWN: %s\n", command.c_str());
  }

  return true;
}

void SVNArmaAutoUpdaterClient::UpdateCommands()
{
  if ( !_connectTo.IsEmpty() )
  { // we are to connect somewhere
    GetNetworkManager().Done();

    //@{ _DEDICATED_CLIENT
    if ( IsDedicatedClient() )
    {
      CreateDedicatedClient(_connectTo, _port, _password);
    }
    else
    { 
      CDP_DECL void __cdecl CDPCreateClient(RString ip, int port, RString password, RString serverName);
      CDPCreateClient(_connectTo, _port, _password, "");
    }
    //@} _DEDICATED_CLIENT
    _connectTo = RString("");
  }
  else if ( !GetNetworkManager().IsClient() )
  { // we are not connected at all, try to connect to the server pool
    if ( !_connectToPool.IsEmpty() ) ParseConnect(_connectToPool);
  }
}

//! UpdateCommands sent through networking layer 
void SVNArmaAutoUpdaterClient::ProcessAUCommand(RString command)
{
  const char *connectToStr = "ConnectTo:";
  if ( !strnicmp(command, connectToStr, strlen(connectToStr)) )
  {
    const char *connectCmd = cc_cast(command) + strlen(connectToStr);
    while (*connectCmd && *connectCmd==' ') connectCmd++;
    ParseConnect(connectCmd);
  }
}

void SVNArmaAutoUpdaterClient::HandleAskToShutDown()
{
  if ( _askedToShutDown )
  {
    if(GWorld) 
    { // ask when _warningMessage is available in GWorld
      GWorld->OnAskForShutDown();
      _askedToShutDown = false;
    }
  }
}

#else
// Dummy implementation of ArmaAutoUpdaterClient
class SVNArmaAutoUpdaterClient
{
public:
  SVNArmaAutoUpdaterClient() {}
  //! read svn url and working directory from autoUpdater config
  bool Init() { return true; }
  bool Update() { return false; }
  void UpdateCommands() {}
  void ProcessAUCommand(RString command) {}
  bool ShutDowning() const { return false; }
  bool HandleAskToShutDown() {}
  void SetShutDown(bool restart) {}
  bool RestartAfterShutDown() const { return false; }
};
#endif

SVNArmaAutoUpdaterClient GSvnAUC;

void AutoUpdateSetShutdown(bool restart)
{
  GSvnAUC.SetShutDown(restart);
}

static DWORD CALLBACK AutoUpdateThread(LPVOID parameter)
{
  while (true)
  {
    int startTime = ::GetTickCount();
    static int nextUpdateTime = startTime;

    if ( !AutoUpdate ) return 0;
    if ( startTime>=nextUpdateTime )
    {
      nextUpdateTime = startTime + 100; // 100ms delay
      if ( !GSvnAUC.Update() )
      {
        return 0;
      }
    }
  }
}

bool ShutDowning()
{
  return GSvnAUC.ShutDowning() || CloseRequest;
}

// GLobal function called to process received NMTSendAUMsg command
void ProcessAUCommand(RString command)
{
  GSvnAUC.ProcessAUCommand(command);
}

#if _ENABLE_REPORT
#  include "El/Debugging/stackMeter.hpp"
#endif

#if _ENABLE_REPORT
  extern bool CheckProgressRefresh;
  #if _M_PPC
  /// X360 PowerPC performance hit for exceptions enabled is huge
  static bool NoFPTraps = true;
  #else
  /// float traps may be disabled for some long-term stability debugging
  static bool NoFPTraps;
  #endif
#endif

static bool CheckAllSignatures = false;
static bool CheckAllSignaturesFull = false;
static bool UnprotectPbos = false;

// do not touch aspect unless explicitly requested
static int WindowAspectW,WindowAspectH;
static int PausedCount = 0;
static int GSizeMoveCount = 0;
static bool PausedByKey = false;

bool HideCursor = false;
static bool MouseEx = true;
static float BuldozerMouseSpeed = 1.0f;
static float VisitorMouseSpeed = 1.0f;

static float TimeForScripts = 0.003f; // by default, at least 3ms for scripts each frame
float GetTimeForScripts() {return TimeForScripts;}
float SetTimeForScripts(float value) {float old = TimeForScripts; TimeForScripts = value; return old;}

// screenSaver activity backup
static BOOL screenSaverEnabledBackup;


static int JavaDebugPort = 0;
static bool JavaSuspend = false;
static bool JavaPerfTest = false;
static bool JavaExportInterface = false;

/*!
\patch 1.32 Date 11/26/2001 by Jirka
- Added: Additional program parameter: -sockets
*/

int D3DAdapter = -1;
bool WinXP = false;

#pragma comment(lib, "steam_api")

#if _ENABLE_REPORT
  static bool ShowScriptErrors = true;
#else
  static bool ShowScriptErrors = false;
#endif

bool AvoidLauncher = false;

RString AddonsList;

RStringB ScriptsPath("scripts\\");
RStringB GetScriptsPath() {return ScriptsPath;}

RStringB ProfilePathDefault("BIS Core Engine");
RStringB ProfilePathCommon("BIS Core Engine Other Profiles");

RStringB GetProfilePathDefault() {return ProfilePathDefault;}
RStringB GetProfilePathCommon() {return ProfilePathCommon;}

RStringB ExtensionSave("fps");
RStringB ExtensionProfile("cfg");
RStringB ExtensionWizardMission("par");
RStringB ExtensionAddon("pbo");

RStringB GetExtensionSave() {return ExtensionSave;}
RStringB GetExtensionProfile() {return ExtensionProfile;}
RStringB GetExtensionWizardMission() {return ExtensionWizardMission;}
RStringB GetExtensionAddon() {return ExtensionAddon;}

//can be changed for VBS to place dta files in a different folder (buldozer) via command line option -dta=
RString dtaFolder = "dta";

#if !defined _WIN32
	bool GAppActive = true;
#else
	bool GAppActive;
#endif

bool GAppPaused;
static bool GAppIconic;
static bool ValidateQuit; // tell that user really wants to terminate

int SkipKeysUntilFrame=INT_MIN;

//! Global variable
bool GShrinkP3D = false;

/*!
\patch_internal 1.26 Date 10/03/2001 by Ondra
- New: different log files for server and client.
*/
#if _ENABLE_REPORT
static const char *LogFilename = "debug.log";
static bool ResetLogFile = false;
//static LogFile GLogFile;
#endif

void FlushLogFile()
{
#if _ENABLE_REPORT
//  GLogFile.Flush();
#endif
}

extern bool NoLandscape;

#if _ENABLE_CHEATS
	bool DebugVS = false;
#endif

bool NoSound=false;


static bool XboxUI=false;

static int customUID = -1;
// enforce the return value of getPlayerUID scripting function
static RString forcePublicKey;

bool IsXboxUI() {return XboxUI;}

static bool ESRBScreen = true;
bool IsESRBScreen() {return ESRBScreen;}

static bool PatchCheck = true;
bool IsPatchCheck() {return PatchCheck;}

/*!
\patch_internal 1.42 Date 1/7/2002 by Jirka
- Changed: _NO_BLOOD option for Korean version was deleted 
*/

bool NoBlood()
{
/*
#if _NO_BLOOD
  return true;
#else
  return GetLangID() == Korean;
#endif
*/
  return false;
}

#if 0
//! Convert version info into float
float VersionToFloat(const char *ptr)
{
  float version = 0;
  while (isdigit(*ptr))
  {
    version *= 10.0f;
    version += *ptr - '0';
    ptr++;
  }
  if (*ptr == '.')
  {
    ptr++;
    float offset = 0.1f;
    while (isdigit(*ptr))
    {
      version += offset * (*ptr - '0');
      offset *= 0.1f;
      ptr++;
    }
  }
  return version;
}
#endif

//! Convert version info into int (fixed-point, 3 fract. digits)
int VersionToInt ( const char *ptr )
{
  int version = 0;
  while ( isdigit(*ptr) ) {
    version *= 10;
    version += *ptr - '0';
    ptr++;
    }
  version *= 1000;
  if ( *ptr++ == '.' ) {
    int fr = 100;
    while ( fr && isdigit(*ptr) ) {
      version += fr * (*ptr - '0');
      fr /= 10;
      ptr++;
      }
    }
  return version;
}

/**************************************************************************
DirectDraw Globals
**************************************************************************/

bool SkipIntro=false;   // skip intro level loading (fast init)

#ifdef _WIN32

// window only variables
RECT clientRect;

// main globals

static bool CanRender; // everything is initialized
bool ForceRender; // draw frame as soon as possible

#if _VBS3
bool ForceSimulation = false;
bool DisableVON=false;
bool LVCWanted = false;
#endif

extern bool LandEditor;

extern bool ObjViewer;

/// should adddon pbos be loaded?
static bool LoadAddons = true;
static bool NoSplash=false;
static bool GenerateShaders=false;
bool DisableShaderGeneration=false;
static bool ForceDX9=false;
bool EnableSM3=true;
static bool DoPause=false;
static bool DoRestore=false;
#if _ENABLE_DEDICATED_SERVER || _ENABLE_HOST
static bool DoCreateServer=false;
#else
static const bool DoCreateServer=false;
#endif
static RString InstanceName;

#if _VBS2 && _ENABLE_CHEATS
  static bool exportCpp=false;
#endif

#else

static bool CanRender; // everything is initialized
extern bool LandEditor;
static bool NoSplash=false;
static bool GenerateShaders=false;
static bool ForceDX9=false;
static bool DoCreateServer=true;
static bool LoadAddons = true;
static bool UseOpenAL = false;

#endif



struct CheckAddonContext: public BankContextBase
{
  const char **productList;
#if _ENABLE_VBS
  const char **productListVBS;
  bool encryptionRequired;
#endif
  FindArrayRStringCI *configPaths;
  /// some config.bin or config.cpp file is present in the addon
  bool hasConfig;
};

#if _ENABLE_ADDONS

///////////////////////////////////////////////////////////////////////////
// {{
// note: This sections is intended to provide context sensitive access
// to various addon files.
// It is far from working yet, currently only config context handling is done

/*
class CfgPatches
{
     class zwa_bench
     {
         units[] = {zwa_bench};
         weapons[] = {};
         requiredVersion = 1.40;
         requiredAddons[] = {"Resistance"};
     };

};
*/

//! contain information necessary to load addon
class AddonInfo
{
  RString _name;
  RString _prefix;

  public:
  AddonInfo(){}
  AddonInfo(RString name, RString prefix)
  :_name(name),_prefix(prefix)
  {
  }
  const char *GetKey() const {return _name;}
  RStringVal GetName() const {return _name;}
  RStringVal GetPrefix() const {return _prefix;}
};

TypeIsMovableZeroed(AddonInfo)

//! map telling which bank corresponds to which addon name
typedef MapStringToClass<AddonInfo,AutoArray<AddonInfo> > AddonInfoMap;


static AddonInfoMap AddonToPrefix;

static void UnlockAddon(AddonInfo &addon, AddonInfoMap *map, void *context)
{
  RString prefix = addon.GetPrefix();
  // find given bank in global bank list
  GFileBanks.Unlock(prefix);
}

static void MarkAddonLockable(AddonInfo &addon, AddonInfoMap *map, void *context)
{
  RString prefix = addon.GetPrefix();
  // find given bank in global bank list
  GFileBanks.SetLockable(prefix,true);
}


//! load addon and make it active
void LoadAddon(const char *addon)
{
  const AddonInfo &info = AddonToPrefix[addon];
  if (AddonToPrefix.IsNull(info))
  {
    LogF("Cannot load addon %s - addon does not exist",(const char *)addon);
    return;
  }
}


//! Prepare addon for unloading
void UnloadAddon(const char *addon)
{
  BankList::ReadAccess banks(GFileBanks);
  const AddonInfo &info = AddonToPrefix[addon];
  if (AddonToPrefix.IsNull(info))
  {
    LogF("Cannot unload addon %s - addon does not exist",(const char *)addon);
    return;
  }
  // unload all vehicles and weapons defined by this addon?
  // flush all files used by this bank
  for (int i=0; i<banks.Size();i++)
  {
    QFBank &bank = banks[i];
    if (stricmp(bank.GetPrefix(), info.GetPrefix()) == 0)
    {
      if (GFileServer) GFileServer->FlushBank(&bank);
    }
  }
}


//! list of enabled file banks
class BankContext: public IQFBankContext
{
  // list of enabled banks
  MapStringToClass<RString,AutoArray<RString> > _banks;

  public:
  //! check if file is from some of the banks listed
  bool IsAccessible(const QFBank *bank) const;

  //! clear bank list
  void Clear();
  //! add single bank
  void Add(RString addon);
  //! compact memory usage
  void Compact();
};


bool BankContext::IsAccessible(const QFBank *bank) const
{
  if (_banks.IsNull(_banks[bank->GetPrefix()])) return false;
  return true;
}
void BankContext::Clear()
{
  _banks.Clear();
}
void BankContext::Add(RString addon)
{
  _banks.Add(addon);
}

void BankContext::Compact()
{
  //_banks.Compact();
}

//! addon interface to file bank list

class AddonContext: public BankContext
{
  public:
  //! activate all default addons and no other
  void Reset();
  //! activate single addon
  void AddAddon(RString addon);
  
};

void AddonContext::Reset()
{
  Clear();
  // active all default addons
  ParamEntryVal def = Pars>>"CfgAddons">>"PreloadBanks";
  for (int c=0; c<def.GetEntryCount(); c++)
  {
    ParamEntryVal cc = def.GetEntry(c);
    if (!cc.IsClass()) continue;
    if (!cc.FindEntry("list")) continue;
    ParamEntryVal cl = cc>>"list";
    for (int i=0; i<cl.GetSize(); i++)
    {
      RString bank = cl[i];
      LogF("Activating bank %s",(const char *)bank);
      Add(bank);
    }
  }
};


void AddonContext::AddAddon(RString addon)
{
  const AddonInfo &info = AddonToPrefix[addon];
  if (AddonToPrefix.IsNull(info)) return;
  Add(addon);
}

//! Checks addon name as given by addon
static IParamClassSource *CheckAddonSource(RStringB filename, bool unload)
{
  if (!unload)
  {
    return new ParamClassOwnerNoUnload("",filename);
  }
  return new ParamClassOwnerFilename("",&Pars,filename);
}

//! Checks addon name as given by addon
static RStringB CheckAddonOwner(ParamFile &addon)
{
  ConstParamEntryPtr patches = addon.FindEntry("CfgPatches");
  if (!patches || patches->GetEntryCount() == 0)
  {
    return "";
  }

  // several addons may be present in this bank
  // in that case use first of them
  // it should not matter, as all of them should be always loaded together
  RStringB ownerName = patches->GetEntry(0).GetName();
  return ownerName;
}

//! Checks required version for addon
static bool CheckVersion(const RString &prefix, ParamEntryPar addon)
{
  ConstParamEntryPtr patches = addon.FindEntry("CfgPatches");
  if (!patches || patches->GetEntryCount() == 0) return false;

  //ConstParamEntryPtr loadedAddons = Pars.FindEntry("CfgPatches");

  RString GetAppVersion();
  RString strVersion = GetAppVersion();

  for (int i=0; i<patches->GetEntryCount(); i++)
  {
    ParamEntryVal patch = patches->GetEntry(i);
    /*
    if (loadedAddons && loadedAddons->FindEntry(patch.GetName()))
    {
      RptF
      (
        "Conflicting addon %s ('%s') (1)",
        (const char *)patch.GetName(),(const char *)prefix
      );
    }
    */

    /*
    ConstParamEntryPtr entry = patch.FindEntry("requiredVersion");
    if (entry)
    {
      int version = VersionToInt(strVersion);
      RString required = *entry;
      if (VersionToInt(required) > version)
      {
        WarningMessage
        (
#ifdef _WIN32
          LocalizeString(IDS_MSG_ADDON_VERSION),
#endif
          (const char *)patch.GetName(), (const char *)required
        );
        return false;
      }
    }
    */
    // name of patch class is name of the addon
    #if 0
    LogF
    (
      "Addon %s in bank %s",
      (const char *)patch.GetName(),(const char *)prefix
    );
    #endif
    const AddonInfo &info = AddonToPrefix[patch.GetName()];
    if (AddonToPrefix.NotNull(info))
    {
      RptF
      (
        "Conflicting addon %s in '%s', previous definition in '%s'",
        (const char *)patch.GetName(),(const char *)prefix,
        (const char *)info.GetPrefix()
      );
    }
    else
    {
      AddonToPrefix.Add(AddonInfo(patch.GetName(),prefix));
    }
  }
  return true;
}

// }}
///////////////////////////////////////////////////////////////////////////

static AutoArray< SRef<ParamFile> > AddonConfigs;

//! callback function to parse addon config
//! assumes bank is already fully loaded

struct AddConfigPathContext
{
  RString prefix;
  FindArrayRStringCI *configPaths;
  bool *hasConfig;
};

static void AddConfigPath(const FileInfoO &fi, const FileBankType *files, void *context)
{
  AddConfigPathContext *ctx = (AddConfigPathContext *)context;
  
  const char *ptr = fi.name;
  const char *ext = strrchr(ptr, '\\');
  if (ext) ext++;
  else ext = ptr;

  bool isConfig = stricmp(ext, "config.cpp") == 0 || stricmp(ext, "config.bin") == 0;
  if (isConfig && ctx->hasConfig) *ctx->hasConfig = true;

  if (stricmp(ext, "stringtable.csv") == 0 || stricmp(ext, "stringtable.xml") == 0 || stricmp(ext, "stringtable.bin") == 0 || isConfig)
  {
    RString path = ctx->prefix + fi.name.Substring(0, ext - ptr);
    ctx->configPaths->AddUnique(path);
  }
}

static bool ParseAddonConfig(QFBank *bank, FindArrayRStringCI *configPaths, bool *hasConfig)
{
  // only find config and stringtable files
  AddConfigPathContext ctx;
  ctx.prefix = bank->GetPrefix();
  ctx.configPaths = configPaths;
  ctx.hasConfig = hasConfig;
  bank->ForEach(AddConfigPath, &ctx);
  #if _ENABLE_CHEATS
    if (bank->NFiles()==0)
    {
      // developer version - empty pbo injects addon
      configPaths->AddUnique(bank->GetPrefix());
    }
  #endif
  return true;
}

static bool ParseAddonConfigCallback(QFBank *bank, BankContextBase *context)
{
  CheckAddonContext *cpc = static_cast<CheckAddonContext *>(context);
  if (!ParseAddonConfig(bank, cpc->configPaths, &cpc->hasConfig))
  {
    return false;
  }
  return true;
}

// parsing itself
static void PrepareAddonsConfigs(const FindArrayRStringCI &configPaths, RString stringtable, AutoArray< SRef<ParamFile> > &configList)
{
  for (int i=0; i<configPaths.Size(); i++)
  {
    RString prefixPath = configPaths[i];
#if _PUBLICBETA
    if (stricmp(prefixPath,"ca\\ui\\")==0) continue;
#endif
    // stringtable
    RString filename = prefixPath + "stringtable.csv";
    LoadStringtable(stringtable, filename, 0, false);

    // config
    filename = prefixPath + "config.cpp";
    if (QFBankQueryFunctions::FileExists(filename))
    {
      SRef<ParamFile> addon = new ParamFile;
      Ref<IParamClassSource> source = CheckAddonSource(filename,false);
      addon->Parse(filename, source, NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
      // check required version for addon
      if (CheckVersion(prefixPath,*addon))
      {
        source->SetOwnerName(CheckAddonOwner(*addon));
        configList.Add(addon);
      }
    }
    else
    {
      filename = prefixPath + "config.bin";
      if (QFBankQueryFunctions::FileExists(filename))
      {
        SRef<ParamFile> addon = new ParamFile;
        Ref<IParamClassSource> source = CheckAddonSource(filename,true);
        addon->ParseBin(filename, source, NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
        // check required version for addon
        if (CheckVersion(prefixPath,*addon))
        {
          source->SetOwnerName(CheckAddonOwner(*addon));
          // addon config is not parsed yet, it is only stored 
          configList.Add(addon);
        }
      }
    }
  }
}

static void PrepareAddonsConfigs(const FindArrayRStringCI &configPaths)
{
  PrepareAddonsConfigs(configPaths, "Global", AddonConfigs);
}

TypeIsSimple(const ParamFile *)

//! information about what addon is dependent on what addons

struct AddonDependency
{
  bool preloaded;
  const ParamFile *addon;
  AutoArray<const ParamFile *> dependsOn;

  //! dependency resolved - may be removed from addon list
  void Resolved(const AddonDependency &dep);
};

TypeIsMovable(AddonDependency)

void AddonDependency::Resolved(const AddonDependency &dep)
{
  for (int i=0; i<dependsOn.Size(); i++)
  {
    if (dependsOn[i]!=dep.addon) continue;
    dependsOn.Delete(i--);
  }
}

//! extract some addon name from addon config
static RStringB GetAddonName(const ParamFile &config)
{
  ConstParamEntryPtr cfg = config.FindEntry("CfgPatches");
  if (cfg)
  {
    //! enumerate all addons in this config
    for (int i=0; i<cfg->GetEntryCount(); i++)
    {
      ParamEntryVal entry = cfg->GetEntry(i);
      if (!entry.IsClass()) continue;
      return entry.GetName();
    }
  }
  return "";
}

//! find addon config (ParamFile) corresponding to given addon name
static const ParamFile *FindAddonConfig(RStringB name)
{
  for (int i=0; i<AddonConfigs.Size(); i++)
  {
    // scan all entries if CfgPatches
    const ParamFile &config = *AddonConfigs.Get(i);
    ConstParamEntryPtr cfg = config.FindEntry("CfgPatches");
    if (cfg)
    {
      //! enumerate all addons in this config
      for (int i=0; i<cfg->GetEntryCount(); i++)
      {
        ParamEntryVal entry = cfg->GetEntry(i);
        if (!entry.IsClass()) continue;
        if (!strcmpi(entry.GetName(),name)) return &config;
      }
    }
  }
  return NULL;
}

//! check if addon is preloaded (default, official) or not
static bool IsPreloadedAddon(RStringB name)
{
  ParamEntryVal def = Pars>>"CfgAddons">>"PreloadAddons";
  for (int c=0; c<def.GetEntryCount(); c++)
  {
    ParamEntryVal cc = def.GetEntry(c);
    if (!cc.IsClass()) continue;
    if (!cc.FindEntry("list")) continue;
    ParamEntryVal cl = cc>>"list";
    for (int i=0; i<cl.GetSize(); i++)
    {
      RStringB addon = cl[i];
      if (!strcmpi(name,addon)) return true;
    }
  }
  return false;
}



/// partial string comparison, case insensitive
/**
Adapted from strcmp Microsoft CRT source
*/
static int StrCmpIPart(const char *src, const char *srcEnd, const char *dst, const char *dstEnd)
{
  int ret = 0;
  for(;;)
  {
    unsigned char c1 = src<srcEnd ? myLower(*src) : 0;
    unsigned char c2 = dst<dstEnd ? myLower(*dst) : 0;
    ret = c1-c2;
    if (ret || !c2) break;
    ++src, ++dst;
  }
  return ret;
}

/// make sure config like ca\ is parsed before ca\plants
  
static int CmpConfig(const SRef<ParamFile> *c1, const SRef<ParamFile> *c2)
{
  // ignore filename, compare only base part (prefix)
  const char *name1 = (*c1)->GetName();
  const char *name2 = (*c2)->GetName();
  const char *end1 = GetFilenameExt(name1);
  const char *end2 = GetFilenameExt(name2);
  
  // compare name1..end1 with name2..end2
  return StrCmpIPart(name1,end1,name2,end2);
}

//! parse addon configs in correct order
/*!
\patch 1.82 Date 8/16/2002 by Ondra
- New: Addon dependencies can be declared using requiredAddons[] section of CfgPatches.
*/

void ParseAddonsConfigs()
{
  AutoArray<AddonDependency> dependencies;
  // recommended order to avoid compatibility problems with addons
  // that have no dependency list
  // 1) official addons with no dependencies
  // 2) unofficial addons with no dependencies
  // 3) official addons with dependencies
  // 4) unofficial addons with dependencies
  // 5) addons with no CfgPatches (malformed addons)
  // official addons list can be found in "CfgAddons/PreloadAddons".
  // read dependency information from ParamFiles
  
  QSort(AddonConfigs,CmpConfig);
  
  for (int i=0; i<AddonConfigs.Size(); i++)
  {
    const ParamFile &config = *AddonConfigs.Get(i);
    AddonDependency &dep = dependencies.Append();
    dep.addon = &config;
    dep.preloaded = false;
    ConstParamEntryPtr cfg = config.FindEntry("CfgPatches");
    if (cfg)
    {
      //! enumerate all addons in this config
      for (int i=0; i<cfg->GetEntryCount(); i++)
      {
        ParamEntryVal entry = cfg->GetEntry(i);
        if (!entry.IsClass()) continue;
        // check if we can find this addon in "preloaded" addons list
        dep.preloaded = IsPreloadedAddon(entry.GetName());
        ConstParamEntryPtr required = entry.FindEntry("requiredAddons");
        if (!required) continue;
        // find corresponding addon
        for (int i=0; i<required->GetSize(); i++)
        {
          RStringB requiredName= (*required)[i];
          const ParamFile *file = FindAddonConfig(requiredName);
          if (!file)
          {
            //RString message = LocalizeString(IDS_MSG_ADDON_MISSING) + requiredName;
            RString message = "Addon '%s' requires addon '%s'";
            WarningMessage
            (
              message,
              (const char *)GetAddonName(config),
              (const char *)requiredName
            );
            continue;
          }
          dep.dependsOn.Add(file);
        }
      }
    }
  }
  
  // build a tree from the dependency

  // sort addons by dependencies
  // we can always load addon that has already resolved all dependencies
  // "loaded" addon is moved to resolved
  AutoArray<AddonDependency> resolved;
  while(dependencies.Size()>0)
  {
    // then find any preloaded addons with resolved dependencies
    // find any other addons with resolved dependencies
    bool someResolved = false;
    for (int preloaded=(int)true; preloaded>=(int)false; preloaded--)
    {
      for (int i=0; i<dependencies.Size(); i++)
      {
        const AddonDependency &dep = dependencies[i];
        if ((int)dep.preloaded!=preloaded) continue;
        if (dep.dependsOn.Size()>0) continue;
        resolved.Add(dep);
        // removed this addon from all dependencies lists
        for (int j=0; j<dependencies.Size(); j++)
        {
          dependencies[j].Resolved(dep);
        }
        dependencies.Delete(i--);
        someResolved = true;
      }
    }
    if (!someResolved)
    {
      // print some addon name
      RStringB addonName = GetAddonName(*dependencies[0].addon);
      ErrorMessage("Circular addon dependency in '%s'",(const char *)addonName);
      break;
    }
  }

  // parse all resolved addon configs
  for (int i=0; i<resolved.Size(); i++)
  {
    Log
    (
      "Parsing addon config '%s' (%s) %s",
      cc_cast(GetAddonName(*resolved[i].addon)),cc_cast(resolved[i].addon->GetName()),
      resolved[i].preloaded ? "(preloaded)" : ""
    );
    Pars.Update(*resolved[i].addon);
  }

  // ignore all unresolved addon configs - circular dependencies
  /*
  for (int i=0; i<dependencies.Size(); i++)
  {
    Pars.Update(*dependencies[i].addon);
  }
  */
  Pars.SetFile(&Pars);
}

#endif

#if _VBS1 || _VBS1_DEMO
static bool VBS = true;
#else
static bool VBS = false;
#endif

#if _ENABLE_VBS
bool IsVBS() {return VBS;}
#else
bool IsVBS() {return false;}
#endif

static const char *ProductList[]=
{
#if _DEMO
  "ArmA 2 OA",
  "TakeOnH",
  "TakeOnH DEMO",
#else
  "ArmA 2: Operation Arrowhead", // HOTFIX: some OA pbos were not fixed yet, see news:jl1dqb$oh6$2@new-server.localdomain
  "ArmA 2 OA",
  "TakeOnH",
#endif
  NULL
};

#if _ENABLE_VBS
static const char *ProductListVBS[]=
{
  "VBS",
  #if _ENABLE_CHEATS
  "OFP: Cold War Crisis",
  "OFP: Resistance",
  #endif
  NULL
};
#endif

static bool StringInList(const char *str, const char **list)
{
  while (*list)
  {
    if (!strcmpi(*list,str)) return true;
    list++;
  }
  return false;
}

static const int KeySize = 0x94;

struct AcceptedKey
{
  const char *name;
  const unsigned char content[KeySize];
};

// Callback to GFBank.GetAllHashes
void ReportHashError(RString errString)
{
  RptF(errString);
  WarningMessage(errString);
}

struct CheckSignatureContext
{
  DSHash &hashVer1Lev0, &hashVer2Lev0, &hashVer2Lev2;

  AutoArray<DSKey> &_keys;
  int &_count;

  CheckSignatureContext(DSHash &v1l0, DSHash &v120, DSHash &v2l2, AutoArray<DSKey> &keys, int &count) : hashVer1Lev0(v120), hashVer2Lev0(v120), hashVer2Lev2(v2l2), _keys(keys), _count(count) {}
};

static bool CheckProductCallback(QFBank *bank, BankContextBase *context)
{
  // first of all check if the same prefix is loaded twice. This cannot be called any sooner, because prefix was not known yet
  {
    QFBank *check = QFBankQueryFunctions::AutoBank(bank->GetPrefix());
    if (check)
      if (!strcmpi(check->GetPrefix(),bank->GetPrefix()))
        return false;
  }
  CheckAddonContext *cpc = static_cast<CheckAddonContext *>(context);
  RString product = bank->GetProperty("product");
  RString encryption = bank->GetProperty("encryption");
  #if _ENABLE_VBS
    #if _VBS2
        if (encryption.GetLength() > 0 && stricmp(encryption, "VBS2") == 0)
          return true;
        if (encryption.GetLength() > 0 && stricmp(encryption, "VBS2M")== 0)
          return true;
    #endif
    if (!IsVBS())
    {
      if (encryption.GetLength()>0)
      {
        return false;
      }
    }
    #if _VBS1 || _VBS1_DEMO
      if (cpc->encryptionRequired)
      {
        if (encryption.GetLength() > 0 && stricmp(encryption, "VBS1") != 0) return false;
      }
    #endif
  #else
    if (encryption.GetLength()>0)
    {
      return false;
    }
  #endif
  RString formatVersion = bank->GetProperty("pboVersion");
  if (formatVersion.GetLength()>0)
  {
    return false;
  }

  if (product.GetLength()>0)
  {
    bool validProduct = true;
#if _ENABLE_VBS
    if (IsVBS())
    {
      if(!StringInList(product,cpc->productListVBS))
      {
        validProduct = false;
      }
    }
    else
#endif
    {
      if(!StringInList(product,cpc->productList))
      {
        validProduct = false;
      }
    }
    if (!validProduct)
    {
      RptF("Unable to open %s ",cc_cast(bank->GetOpenName()));
      return false;
    }
  }
  
#if _LOAD_ONLY_SIGNED_DATA
  static AcceptedKey acceptedKeys[] = ACCEPTED_KEYS;
  const int acceptedKeysCount = lenof(acceptedKeys);

  DSKey keys[acceptedKeysCount];
  for (int i=0; i<acceptedKeysCount; i++)
  {
    keys[i]._name = acceptedKeys[i].name;
    keys[i]._content.Realloc(KeySize);
    memcpy(keys[i]._content.Data(), acceptedKeys[i].content, KeySize);
  }

  DSSignature signature;
  DSHash hash;
  bank->Load();
  if (
    !DataSignatures::FindSignature(signature, bank->GetOpenName(), keys, acceptedKeysCount) ||
    !bank->GetHash<HashCalculator>(hash._content, signature.Version(), 0) ||
    !DataSignatures::VerifySignature(hash, signature))
  {
    WarningMessage("Warning: Signature of %s is wrong.", cc_cast(bank->GetOpenName()));
    return false;
  }
#endif

  return true;
}

struct LoadBanksContext
{
  /// directory suffix where to find the addon - the whole path of addon is: root/<mod directory>/dir
  RString dir;
  bool emptyPrefix;
  /// check if there is any config and if it is, parse it as addon
  bool parseConfig;
  /// header should be loaded immediately
  /** When parseConfig is used, headers are always loaded*/
  bool loadNow;
  /// bank is part of permanent data and there is no need to check for file changes
  bool permanent;
  /// mark banks to check their signatures
  bool checkSignature;
  /// store the current mod being processed by ForEachModDirectory
  ModInfo *mod;
  
  FindArrayRStringCI configPaths;
};

static void UnloadBankCallback(QFBank &bank)
{
  if (GSoundsys) GSoundsys->FlushBank(&bank);
  if (GEngine)
  {
    AbstractTextBank *textures = GEngine->TextBank();
    if (textures) textures->FlushBank(&bank);
  }
  if (GFileServer) GFileServer->FlushBank(&bank);
}

#if _PROFILE
struct ExtHistogram
{
  StatisticsByName _extSize;
  StatisticsByName _extCount;

  static void ProcessItemCallback(const FileInfoO &fi, const FileBankType *files, void *context)
  {
    reinterpret_cast<ExtHistogram *>(context)->ProcessItem(fi,files);
  }
  void ProcessItem(const FileInfoO &fi, const FileBankType *files)
  {
    _extSize.Count(GetFileExt(fi.name),fi.length);
    _extCount.Count(GetFileExt(fi.name),1);
  }
  void ProcessBank(const QFBank &bank)
  {
    bank.ForEach(ProcessItemCallback,this);
  }
};
#endif

//! Find all banks and adds them to global array of banks
/*!
  \patch 1.01 Date 06/12/2001 by Jirka
  - Added: AddOns support
  \patch_internal 1.01 Date 06/12/2001 by Jirka
  - added parameter parseConfig (if true, config.cpp or config.bin is searched on root of bank and merged with global config)
  \patch_internal 1.26 Date 10/03/2001 by Ondra
  - Fixed: banks were opened even when already existed (Data, Data3D in HWTL mode)
*/

#if _PROFILE
static ExtHistogram GExtHistogram;
#endif

static void LoadBanks(LoadBanksContext *ctx, const char *fullPath)
{
  //RptF("LoadBanks: %s", fullPath);
  I_AM_ALIVE();
  if (GProgress) GProgress->Refresh();

  
  FindArrayRStringCI bankNames;
  FindBank find;
  if (find.First(fullPath))
  {
    do
    {
      char prefix[256];
      strcpy(prefix, find.GetName());
      strlwr(prefix); // note: Win32 - case insensitive
      char *ext = strrchr(prefix, '.');
      #if !_VBS2
      // load only pbos, ignore other extensions (pbx)
      if (!strcmp(ext,".pbo"))
      #endif
      {
        *ext = 0;
        bankNames.AddUnique(prefix);
      }
      
    }
    while (find.Next());
    find.Close();
  }
  RString bankPrefix = RString(ctx->dir) + "\\";
  // open each bank
  for (int i=0; i<bankNames.Size(); i++)
  {
    const RString &bName = bankNames[i];
    //RptF("    %s", cc_cast(bName));

    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();
    
    Ref<CheckAddonContext> cpc = new CheckAddonContext;
    cpc->productList = ProductList;
#if _ENABLE_VBS
    cpc->productListVBS = ProductListVBS;
    cpc->encryptionRequired = ctx->parseConfig;
#endif
    OpenCallback addonCallback = (OpenCallback)NULL;
#if _ENABLE_ADDONS
    if (ctx->parseConfig)
    {
      addonCallback = ParseAddonConfigCallback;
    }
#endif
    cpc->configPaths = &ctx->configPaths;
    cpc->hasConfig = false;

    RString bankPath = RString(fullPath) + "\\";
    // prefix may differ from the pbo name
    QFBank *bank = GFileBanks.Load(
      bankPath,bankPrefix,bName,ctx->emptyPrefix,
      CheckProductCallback,addonCallback,cpc
    );
    if (bank)
    {
      if (ctx->mod)
        ctx->mod->loadedBanks.Add(ModBankInfo(bank->GetPrefix(), bank->GetOpenName()));
      if (ctx->loadNow)
        bank->Load();
      if (ctx->permanent)
        bank->MakeHandlePermanent();
      if (ctx->checkSignature)
        bank->SetVerifySignature();
      if (IsDedicatedServer())
      {
        int ix = GBankHashes.Add(BankHashes(bank->GetPrefix()));
        BankHashes &bankHashes = GBankHashes[ix];
        bank->GetHash<HashCalculator>(bankHashes.ver1Hash, 1, 0);
        bank->GetHash<HashCalculator>(bankHashes.ver2lev0Hash, 2, 0);
        bank->GetHash<HashCalculator>(bankHashes.ver2lev2Hash, 2, 2);
      }
      // Get the canonical hash of loaded bank
      RString canonicalHash = bank->GetProperty("hash");
      if ( canonicalHash.IsEmpty() )
      {
        Temp<char> hash;
        bank->GetHash(hash);
        canonicalHash = ConvertToHex(hash.Data(), hash.Size(), false); //useUpperCase
      }
      //RptF("AddingHash ... %s - %s - %s", cc_cast(bank->GetPrefix()), cc_cast(canonicalHash), cc_cast(bank->GetOpenName()));
      GCanonicalBankHashes.Add(CanonicalHash(bank->GetPrefix(), canonicalHash));

      #if _PROFILE
        GExtHistogram.ProcessBank(*bank);
      #endif
    }
  }
}

/*!
\patch_internal 1.53 Date 4/25/2002 by Jirka
- Added: mods can use also different data banks
*/

static bool LoadBanksCallback(ModInfo *mod, LoadBanksContext &ctx)
{
  if (!mod)
  {
    ctx.mod = mod;
    LoadBanks(&ctx, ctx.dir);
  }
  else if (mod->origin!=ModInfo::ModNotFound)
  {
    if ( !mod->fullPath.IsEmpty() )
    {
      ctx.mod = mod;
      LoadBanks(&ctx, mod->fullPath + RString("\\") + ctx.dir);
    }
  }
  return false;
}

// Function ParseStringtable defined in mainInput.cpp
bool ParseStringtable(RString dir, void *context);

static bool ParseStringtableCallback(ModInfo *mod, int &dummy)
{
  if (!mod)
  {
    return ParseStringtable("", &dummy);
  }
  else if (mod->origin!=ModInfo::ModNotFound)
  {
    if ( !mod->fullPath.IsEmpty() )
    {
      return ParseStringtable(mod->fullPath, &dummy);
    }
  }
  return false;
}

static void LoadBanksEx(LoadBanksContext *ctx)
{
  ForEachModDirectory(LoadBanksCallback, *ctx);
}

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);

RString InitWorldName;

/// used to avoid Stop/Start on App activate/deactivate until file server is started
static bool GFileServerStarted = true;
/// request to start/stop the file server ASAP
/**
This is used to make sure files can be written while the game is not active
We do not want to stop it immediately as this could make game loading very slow
*/
static bool GFileServerActive = true;


void AsyncServerAtAlive()
{
#ifdef _WIN32
  GAsyncFileServer.DoWork(0);
#endif
}

/*!
  \patch_internal 1.01 Date 06/12/2001 by Jirka - added AddOns support
  - directory AddOns is searched for banks
*/
void Globals::InitPhase1()
{
  //int fileMemory=Glob.config.fileHeapSize*oneMB;
  const int oneMB = 1024*1024;
  // with -maxmem=512 we want 64 MB
  // with -maxmem=1024 we want 256 MB, perhaps even 512 MB?
  mem_size_t totalLimit = GetMemoryUsageLimit();
  mem_size_t fileMemory = totalLimit-448*oneMB;
  if( fileMemory<Glob.config.fileHeapSize*oneMB ) fileMemory=Glob.config.fileHeapSize*oneMB;

  size_t cacheSize = MemSizeTToSizeT(fileMemory);
  GFileServer->SetMaxSize(cacheSize);
  
  GFileServerStart();
  GFileServerStarted = true;
  GFileServerActive = true;
  //GlobalAtAlive(AsyncServerAtAlive);
}

// we are not a shell, therefore we probably should not do this
// however, we use our own GUID - uniqueness guaranteed {80182195-6CBA-4218-830B-3AF1CDB5F1BD}

#include "encodeString.h"

#define NameBase "Local\\shell.{80182195-6CBA-4218-830B-3AF1CDB5F1"
#define NameEnd "}"

#define DEFINE_ENCODED_BASE(x) static auto NameBaseEncoded##x = EncodedString<EncoderShiftPos<x>>(NameBase);
#define DEFINE_ENCODED_END(x)  static auto NameEndEncoded ##x = EncodedString<EncoderShiftPos<x>>(NameEnd);

#define DEFINE_ENCODED(x) DEFINE_ENCODED_BASE(x);DEFINE_ENCODED_END(x)

template <int rand, int len, class SrcBase, class SrcEnd>
void CreateFadeNameString(BString<len> &name, int id, const SrcBase &srcBase, const SrcEnd &srcEnd)
{
  /* unmangle the string */
  auto decodedBase = DecodeString(srcBase);
  strcpy(name,decodedBase);
  int len = strlen(name);
  LString str = name+len;
  int numBase = 0xbd;
  sprintf(str,"%02X",numBase+id);
  auto decodedEnd = DecodeString(srcEnd);
  strcat(name,decodedEnd);
}

#define CREATE_FADE_NAME_STRING(tgt,id,rand) CreateFadeNameString<rand>(tgt,id,NameBaseEncoded##rand,NameEndEncoded##rand)

/// see also ReinitFadeCount
const static LONG InitFadeCount[CountFadeConditions]= {10,5,3,5,3};

/**
other functions related to semaphores are intentionally moved to different positions in this source
To avoid the functions being close together.
*/

void CreateFadeSemaphores()
{
#if PROTECTION_ENABLED
  DEFINE_ENCODED(0x6a);
  for (int id=0; id<CountFadeConditions; id++)
  {
    BString<64> name;
    CREATE_FADE_NAME_STRING(name,id,0x6a);

    CreateSemaphore(NULL,InitFadeCount[id],INT_MAX,name);
    // this is a kind of leak - the handle is never closed
    // this is intentional - application termination will close it
  }
  {
    DEFINE_ENCODED(0xa5);
    BString<64> name;
    CREATE_FADE_NAME_STRING(name,CountFadeConditions+1,0xa5);
    CreateEvent(NULL,TRUE,FALSE,name);
  }
#endif
}

#ifdef DEBUG_UI_RSC
void DebugRscDisplayNotFreeze()
{
  ConstParamEntryPtr clsEntry = Pars.FindEntry("RscDisplayNotFreeze");
  ConstParamEntryPtr cfg = clsEntry->FindEntry("controls");
  if (cfg)
  {
    if (cfg->IsClass())
    {
      RptF("RscDisplayNotFreeze entries:");
      for (int i=0; i<cfg->GetEntryCount(); i++)
      {
        ParamEntryVal ctrlCls = cfg->GetEntry(i);
        RptF("  %s", cc_cast(ctrlCls.GetName()));
        if (!ctrlCls.IsClass()) continue;
        if (!ctrlCls.FindEntry("type")) 
        { 
          RptF("Warning: no type entry inside class %s/controls/%s ", "RscDisplayNotFreeze", cc_cast(ctrlCls.GetName())); 
          continue; 
        }
        if (!ctrlCls.FindEntry("idc")) { RptF("Warning: no idc entry inside class %s/controls/%s ", "RscDisplayNotFreeze", cc_cast(ctrlCls.GetName())); continue; }
      }
    }
  }
}
#endif

RString GetFirstSpeakerType();

// filename argument
//char LoadFile[512]="Worlds\\test.wrp";
char LoadFile[512];
//extern const char DefLoadFile[]="Worlds\\test.wrp";

#if _ENABLE_ADDONS
struct
{
  RString finalPrefix;
  bool installed;
  bool mission;
} AddonArgument;

RString GetAddonArgumentPrefix() {return AddonArgument.finalPrefix;}
bool IsAddonArgumentInstalled() {return AddonArgument.installed;}
bool IsAddonArgumentMission() {return AddonArgument.mission;}
#endif

#if _PROFILE && USER_Ondra
#define BENCHMARK_GRID_DIV 0
#endif

#if BENCHMARK_GRID_DIV
#include <El/HiResTime/hiResTime.hpp>
void BenchmarkFunc( const std::function<void()> &func, const char *name)
{
  SectionTimeHandle start = StartSectionTime();
  func();
  float duration = GetSectionTime(start);
  LogF("Benchmark %s: %.2f ms",name,duration*1000);
}

const float baseCoord = 12659.9990f;

template <int iters, class Code>
void BenchmarkCode(Code code, const char *name)
{
  auto loopedCode = [code](){
    for (int i=0; i<iters; i++)
    {
      volatile auto preventOptimizeOut = code(baseCoord+i*0.01f);
      (void)preventOptimizeOut;
    }
  };
  BenchmarkFunc(loopedCode,name);
}


template <int iters, class Code1, class Code2>
void VerifyCode(Code1 code1, Code2 code2, const char *name)
{
  int errors = 0;
  for (int i=0; i<iters; i++)
  {
    if (code1(baseCoord+i*0.01f)!=code2(baseCoord+i*0.01f)) errors++;
  }
  LogF("Errors in %s: %d",name,errors);
}

#endif

void Globals::InitPhase2()
{
  #if BENCHMARK_GRID_DIV
  {
    static volatile float gridV = 7.5f;
    static volatile float invGridV = 1/gridV;
    float grid = gridV;
    float invGrid = invGridV;
    std::function<int(float)> useInvGrid = [grid,invGrid](float x)->int{
      return toIntFloor(x*invGrid);
    };
    std::function<int(float)> useDivGrid = [grid,invGrid](float x)->int{
      return toIntFloor(x/grid);
    };
    std::function<int(float)> useInvGridFix = [grid,invGrid](float x)->int{
      int ix = toIntFloor(x*invGrid);
      // fix rounding errors
      float xCheck = ix*grid;
      if (xCheck>x) ix--;
      if (xCheck+xCheck+grid<x) ix++;
      return ix;
    };
    std::function<int(float)> useInvGridFixUp = [grid,invGrid](float x)->int{
      int ix = toIntFloor(x*invGrid);
      // fix rounding errors
      float xCheck = ix*grid;
      if (xCheck+xCheck+grid<x) ix++;
      return ix;
    };
    std::function<int(float)> useInvGridFixDown = [grid,invGrid](float x)->int{
      int ix = toIntFloor(x*invGrid);
      // fix rounding errors
      float xCheck = ix*grid;
      if (xCheck>x) ix--;
      return ix;
    };
    std::function<int(float)> useInvGridFixOpt = [grid,invGrid](float x)->int{
      int ix = toIntFloor(x*invGrid);
      // fix rounding errors
      float xCheck = ix*grid;
      ix -= (xCheck>x);
      ix += (xCheck+grid<x);
      return ix;
    };

    BenchmarkCode<100000>(useInvGrid,"useInvGrid");
    BenchmarkCode<100000>(useDivGrid,"useDivGrid");
    BenchmarkCode<100000>(useInvGridFix,"useInvGridFix");
    BenchmarkCode<100000>(useInvGridFixUp,"useInvGridFixUp");
    BenchmarkCode<100000>(useInvGridFixDown,"useInvGridFixDown");
    BenchmarkCode<100000>(useInvGridFixOpt,"useInvGridFixOpt");
    VerifyCode<100000>(useDivGrid,useInvGrid,"useInvGrid");
    VerifyCode<100000>(useDivGrid,useInvGridFix,"useInvGridFix");
    VerifyCode<100000>(useDivGrid,useInvGridFixOpt,"useInvGridFixOpt");
    VerifyCode<100000>(useDivGrid,useInvGridFixUp,"useInvGridFixUp");
    VerifyCode<100000>(useDivGrid,useInvGridFixDown,"useInvGridFixDown");
    LogF("BenchmarkCode done") ;
  }
  #endif

//   void Test();
//   
//   Test();

  if (LandEditor)
  {
#if _ENABLE_ADDONS
    if (AddonsList.GetLength() > 0)
    {
      GameDataNamespace globals(NULL, RString(), false); // simple list - no global namespace

      FindArrayRStringCI configPaths;
      ParamFile file;
      file.Parse(AddonsList, NULL, NULL, &globals);
      ParamEntryVal array = file >> "addons";
      for (int i=0; i<array.GetSize(); i++)
      {
        RString addon = array[i];
        configPaths.AddUnique(addon);
      }
      PrepareAddonsConfigs(configPaths);
      ParseAddonsConfigs();
      AddonConfigs.Clear();
    }
#endif
  }
  if (GUseFileBanks)
  {
    GFileBanks.RegisterUnloadCallback(UnloadBankCallback);
  }
  if( GUseFileBanks && LoadAddons)
  {
    
    LoadBanksContext context;
    context.mod = NULL;
#if _ENABLE_ADDONS
    context.dir = "addons";
    context.emptyPrefix = true;
    context.parseConfig = true;
    context.loadNow = false;
    context.permanent = true;
    context.checkSignature = true;
    context.mod = NULL;
    LoadBanksEx(&context);

    context.dir = "common";
    context.emptyPrefix = true;
    context.parseConfig = true;
    context.loadNow = false;
    context.permanent = true;
    context.checkSignature = true;
    context.mod = NULL;
    LoadBanksEx(&context);   //<-- all mods can contain *.pbo files inside its common subfolder now (see news:jl13l4$ffl$1@new-server.localdomain)
#endif // _ENABLE_ADDONS
    context.dir = "Campaigns";
    context.emptyPrefix = false;
    context.parseConfig = false;
    context.loadNow = false;
    context.permanent = false;
    context.checkSignature = false;
    context.mod = NULL;
    LoadBanksEx(&context);

    // if the addon is given as an application argument, try to load it now
#if _ENABLE_ADDONS && _ENABLE_PARAMS
    AddonArgument.installed = false;
    AddonArgument.finalPrefix = RString();
    AddonArgument.mission = false;

    const char *ext = GetFileExt(LoadFile);
    if (stricmp(ext, ".pbo") == 0 && QFBankQueryFunctions::FileExists(LoadFile))
    {
      // get current directory to convert relative path to absolute
      RString currentDir;
      int size = GetCurrentDirectory(0, NULL);
      GetCurrentDirectory(size, currentDir.CreateBuffer(size));
      if (currentDir[size - 1] != '\\') currentDir = currentDir + RString("\\");
    
      BankList::ReadAccess banks(GFileBanks);
      // check if the bank is already loaded
      for (int i=0; i<banks.Size(); i++)
      {
        RString name = banks[i].GetOpenName();
        if (IsRelativePath(name)) name = currentDir + name;

        if (stricmp(name, LoadFile) == 0)
        {
          AddonArgument.installed = true;
          AddonArgument.finalPrefix = banks[i].GetPrefix();
          break;
        }
      }

      if (!AddonArgument.installed)
      {
        // not loaded yet, load it now

        Ref<CheckAddonContext> cpc = new CheckAddonContext;
        cpc->productList = ProductList;
#if _ENABLE_VBS
        cpc->productListVBS = ProductListVBS;
        cpc->encryptionRequired = true;
#endif
        cpc->configPaths = &context.configPaths;
        cpc->hasConfig = false;

        RString bankPath = RString(LoadFile, ext - LoadFile);
        bankPath.Lower();

        // split bankPath
        RString bankName;
        const char *ptr = strrchr(bankPath, '\\');
        if (ptr)
        {
          bankName = ptr + 1;
          bankPath = RString(bankPath, ptr - cc_cast(bankPath) + 1);
        }
        else
        {
          bankName = bankPath;
          bankPath = RString();
        }

        int configsBefore = context.configPaths.Size();

        // prefix may differ from the pbo name
        QFBank *bank = GFileBanks.Load(bankPath, RString(), bankName, true, 
          CheckProductCallback, ParseAddonConfigCallback, cpc);

        if (bank)
        {
          AddonArgument.finalPrefix = bank->GetPrefix();
          if (!cpc->hasConfig && bank->FileExists("mission.sqm"))
          {
            // pbo is a mission, not an addon
            // unload the bank, do not load the configs
            GFileBanks.Remove(*bank);
            context.configPaths.Resize(configsBefore);
            // mark the argument as a mission
            AddonArgument.mission = true;
            AddonArgument.finalPrefix = bankPath + bankName;
          }
          else
          {
            if (false /*ctx->loadNow*/)
              bank->Load();
            if (true /*ctx->permanent*/)
              bank->MakeHandlePermanent();
            if (true /*ctx->checkSignature*/)
              bank->SetVerifySignature();
          }
        }
      }
    }
#endif

#if _ENABLE_ADDONS
    PrepareAddonsConfigs(context.configPaths);
    ParseAddonsConfigs();
    // addon config will be no longer required, as they are already parsed in
    AddonConfigs.Clear();
#ifdef DEBUG_UI_RSC
    DebugRscDisplayNotFreeze();
#endif
#endif
  }

#if _ENABLE_ADDONS
  // maintain default addon list
  AddonToPrefix.ForEach(MarkAddonLockable);

  #if _PROFILE
    LogF("Size by extension");
    GExtHistogram._extSize.Report(0,false);
    LogF("Count by extension");
    GExtHistogram._extCount.Report(0,false);
#endif

#endif

  #if 0 //ndef _SUPER_RELEASE
    // measure config streaming savings
    size_t controlledPars = Pars.MemoryControlled();
    size_t controlledRes = Res.MemoryControlled();
    size_t totalMem = MemoryUsed();
    size_t releasedPars = Pars.FreeAll();
    size_t totalMemReleasePars = MemoryUsed();
    size_t releasedRes = Res.FreeAll();
    size_t totalMemReleaseRes = MemoryUsed();
    LogF("Cfg reported controlled %d",controlledPars);
    LogF("Res reported controlled %d",controlledRes);
    LogF("Cfg reported released %d",releasedPars);
    LogF("Res reported released %d",releasedRes);
    LogF("Cfg released %d",totalMem-totalMemReleasePars);
    LogF("Res released %d",totalMemReleasePars-totalMemReleaseRes);
  #endif
  //
  {
    char logFilename[MAX_PATH];
    if (GetLocalSettingsDir(logFilename))
    {
      CreateDirectory(logFilename, NULL);
      TerminateBy(logFilename, '\\');
    }
    else logFilename[0] = 0;
    strcat(logFilename, "clipboard.txt");
    remove(logFilename);
  }

  // config is parsed now, we may start to use it

  time = Time(0); //-1e8;
  uiTime = UITime(0);
  frameID = 0;

  exit=false;

  strcpy(header.filename, "");
#if _FORCE_DEMO_ISLAND
  RString worldInit = Pars>>"CfgWorlds">>"demoWorld";
#else
  RString worldInit = Pars>>"CfgWorlds">>"initWorld";
  if (InitWorldName.GetLength()>0)
  {
    // check if given world exists
    if ((Pars>>"CfgWorlds").FindEntry(InitWorldName))
    {
      worldInit = InitWorldName;
    }
    else
    {
      // referring to non-existing world - load nothing
      worldInit = RString();
    }
  }
  
#endif
#if _ENABLE_BULDOZER
  if (!LandEditor || strlen(header.worldname) == 0) // if not set by command line param... 
#endif
  strcpy(header.worldname, worldInit);

  ScriptsPath = Pars >> "scriptsPath";

  #if _GAMES_FOR_WINDOWS
  // get player name (if not passed as argument)
  if (Glob.header.GetPlayerName().GetLength() == 0)
  {
        ParamFile settings;
        settings.Parse("cache:\\settings.cfg", NULL, NULL, &globals);
        ConstParamEntryPtr entry = settings.FindEntry("player");
        if (entry) Glob.header.SetPlayerName(*entry);
        else Glob.header.SetPlayerName("Player");
      }
    #endif

  header.playerFace = "Default";
  header.playerGlasses = "None";
  header.playerSpeakerType = GetFirstSpeakerType();
  header.playerPitch = PITCH_DEFAULT;
  ParamEntryVal voiceMask = Pars >> "CfgVoiceMask";
  if (voiceMask.GetEntryCount()>0)
  {
    header.voiceMask = voiceMask.GetEntry(0).GetName();
  }
  header.voiceThroughSpeakers = VOICE_THROUGH_SPEAKERS_DEFAULT;

  header.playerSide = TWest;
  config.difficulty = config.diffDefault;
#if _ENABLE_LIFE_CHEAT
  config.super = false;
#endif

  if (NoBlood())
  {
    config.bloodLevel = 0;
  }
  else
  {
    config.bloodLevel = GetLangID() != Korean ? 1 : 0;
  }

  GInput.Init();

  // Load options from profile
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals))
  {
    GInput.InitKeys();
    config.InitDifficulties();
    config.LoadVideoOptionsConfig();
    // load default settings
    ParamEntryVal defSettings = Pars>>"CfgDefaultSettings">>"UserInfo";
    if (defSettings.GetClassInterface())
    {
      cfg.Update(*defSettings.GetClassInterface());
    }
    // create a new profile
    RString GetUserParams(bool update);
    RString filename = GetUserParams(false);
    void CreatePath(RString path);
    CreatePath(filename);
    cfg.Save(filename);
#if _PROFILE_IN_STEAM_CLOUD
    // Note: no need to save the profile into the Steam Cloud, as the new profile can be default created on all possible desktops
    //       only after some changes are applied, the save should be synced to Steam Cloud too
#endif
    return;
  }

  ConstParamEntryPtr identity = cfg.FindEntry("Identity");
  if (identity)
  {
    ConstParamEntryPtr entry = identity->FindEntry("face");
    if (entry) header.playerFace = *entry;
    entry = identity->FindEntry("glasses");
    if (entry) header.playerGlasses = *entry;
    entry = identity->FindEntry("speaker");
    if (entry) header.playerSpeakerType = *entry;
    entry = identity->FindEntry("pitch");
    if (entry) header.playerPitch = *entry;
    entry = identity->FindEntry("mask");
    if (entry) header.voiceMask = *entry;
  }

  if (!NoBlood())
  {
    ConstParamEntryPtr entry = cfg.FindEntry("blood");
    if (entry) config.bloodLevel = *entry;
  }
  ConstParamEntryPtr headBobEntry = cfg.FindEntry("headBob");
  config.headBob = (headBobEntry) ? *headBobEntry : 1.0;

//#if _HELISIM_AMBIENT_TRAFFIC
//  ConstParamEntryPtr ambientTrafficEntry = cfg.FindEntry("ambientTrafic");
//  config.ambientTraffic = (ambientTrafficEntry) ? *ambientTrafficEntry : 1;
//#endif
#if _HELISIM_IMPERIAL_SYSTEM
  ConstParamEntryPtr useImperialSystemeEntry = cfg.FindEntry("useImperialSysteme");
  Glob.config.useImperialSystem = (useImperialSystemeEntry) ? *useImperialSystemeEntry : 0;
#endif
  ConstParamEntryPtr vehicleFreelookSystemeEntry = cfg.FindEntry("vehicleFreelook");
  Glob.config.vehicleFreelook = (vehicleFreelookSystemeEntry) ? *vehicleFreelookSystemeEntry : 0;
#if !_HELISIM_PRECISE_INTERACTION
  saturate(Glob.config.vehicleFreelook, 0, 1);
#endif

  GInput.LoadKeys(cfg);
  GInput.RemoveInaccessableKeys();
  config.LoadDifficulties(cfg);
  config.LoadBESettings(cfg);

  config.LoadVideoOptionsConfig();

#if _VBS3 // load custom group defs
  RString fullName = GetUserDirectory() + "config\\editorGroups.cfg";
  if (QFBankQueryFunctions::FileExists(fullName))
  {
    SRef<ParamFile> addon = new ParamFile;
    addon->Parse(fullName, NULL, NULL, GWorld->GetMissionNamespace());
    Pars.Update(*addon);
    Pars.SetFile(&Pars);
  }
  fullName = GetUserDirectory() + "config\\editorVehicles.cfg";
  if (QFBankQueryFunctions::FileExists(fullName))
  {
    SRef<ParamFile> addon = new ParamFile;
    addon->Parse(fullName, NULL, NULL, GWorld->GetMissionNamespace());
    Pars.Update(*addon);
    Pars.SetFile(&Pars);
  }
#endif

#if 0 // _PROFILE
  // stress test file server
  // read all pbos sequentialy to fill the cache
  for (int i=0; i<GFileBanks.Size(); i++)
  {
    QFBank &bank = GFileBanks[i];
    LogF("Processing %s from %s, store %d B, mapped %d pages",cc_cast(bank.GetPrefix()),cc_cast(bank.GetOpenName()),
      GMemStore.GetCommittedBytes(),GMemStore.GetMappedPages()
    );
    

    struct ProcessCRC
    {
      struct AddCRC: private CRCCalculator
      {
        void operator () (void *buf, int len)
        {
          Add(buf,len);
        }
      } crc;
      
      bool Process(QIStream &in)
      {
        in.Process(crc);
        return true;
      }
    } process;
    bank.CalculateProcess(process);
  }
#endif

#if _ENABLE_ADDONS && _ENABLE_TEXTURE_HEADERS
  void LoadTextureHeadersFromBanks();
  if (GUseTextureHeaders)
  {
    LoadTextureHeadersFromBanks();
  }
#endif

  CameraShakeManager::ReadParams(Pars);
  ParamEntryVal patches = Pars>>"CfgPatches";

  isDayZ = NotDayZ;
  if (patches.FindEntry("dayz"))
  {
    isDayZ = IsDayZ;
  }
  else
  {
    for (ParamClass::Iterator<> entry = &patches; entry; ++entry)
    {
      RString lowCase = (*entry).GetName();
      if (strstr(lowCase,"dayz"))
      {
        LogF("CfgPatches dayz '%s'",cc_cast(lowCase));
        isDayZ = IsDayZOther;
        break;
      }
    }
  }
}

void Config::LoadBESettings(ParamEntryPar userCfg)
{
  beLicenceAgreed = false;

  beLicenceAgreed = userCfg.ReadValue("battleyeLicense", false);
}

void Config::SaveBESettings(ParamFile &userCfg)
{
  userCfg.Add("battleyeLicense", beLicenceAgreed);
}

#if !_BINARIZE
void Config::SetBELicence(bool agreed)
{
  // set the flag
  beLicenceAgreed = agreed;

  // save it to profile
  GameDataNamespace globals(NULL, RString(), false);
  ParamFile cfg;
  ParseUserParams(cfg, &globals);
  SaveBESettings(cfg);
  void SaveUserParams(ParamFile &cfg);
  SaveUserParams(cfg);
}
#endif

void Config::LoadDifficulties(ParamEntryPar userCfg)
{
  InitDifficulties();

  ConstParamEntryPtr cls = userCfg.FindEntry("Difficulties");
  if (cls)
  {
    for (int i=0; i<diffSettings.Size(); i++)
    {
      RString name = diffNames.GetName(i);
      DifficultySettings &item = diffSettings[i];

      ConstParamEntryPtr entry = cls->FindEntry(name);
      if (!entry) continue;

      ConstParamEntryPtr flags = entry->FindEntry("Flags");
      if (flags)
      {
        for (int i=0; i<DTN; i++)
        {
          if (!item.flagsEnabled[i]) continue;
          RString name = diffDesc[i].name;
          ConstParamEntryPtr entry = flags->FindEntry(name);
          if (entry) item.flags[i] = *entry;
        }
      }
      ConstParamEntryPtr skill = entry->FindEntry("skillFriendly");
      if (skill) item.aiSkill[0] = *skill;
      skill = entry->FindEntry("skillEnemy");
      if (skill) item.aiSkill[1] = *skill;
      skill = entry->FindEntry("precisionFriendly");
      if (skill) item.aiPrecision[0] = *skill;
      skill = entry->FindEntry("precisionEnemy");
      if (skill) item.aiPrecision[1] = *skill;
    }
  }

  ConstParamEntryPtr entry = userCfg.FindEntry("showTitles");
  if (entry) showTitles = *entry;

#ifdef _WIN32
  entry = userCfg.FindEntry("showRadio");
  if (entry) GChatList.Enable(*entry);
#endif

}

void Config::SaveDifficulties(ParamFile &userCfg)
{
  ParamClassPtr cls = userCfg.AddClass("Difficulties");
  for (int i=0; i<diffSettings.Size(); i++)
  {
    RString name = diffNames.GetName(i);
    DifficultySettings &item = diffSettings[i];

    ParamClassPtr entry = cls->AddClass(name);
    ParamClassPtr flags = entry->AddClass("Flags");
    if (flags)
    {
      for (int i=0; i<DTN; i++)
      {
        if (!item.flagsEnabled[i]) continue;
        flags->Add(diffDesc[i].name, item.flags[i]);
      }
    }
    entry->Add("skillFriendly", item.aiSkill[0]);
    entry->Add("skillEnemy", item.aiSkill[1]);
    entry->Add("precisionFriendly", item.aiPrecision[0]);
    entry->Add("precisionEnemy", item.aiPrecision[1]);
  }

  userCfg.Add("blood", Glob.config.bloodLevel);
  userCfg.Add("headBob", Glob.config.headBob);
  userCfg.Add("showTitles", showTitles);

#if _HELISIM_IMPERIAL_SYSTEM
  userCfg.Add("useImperialSystem", Glob.config.useImperialSystem);
#endif
  userCfg.Add("vehicleFreelook", Glob.config.vehicleFreelook);

#ifdef _WIN32
  userCfg.Add("showRadio", GChatList.ProfileEnabled());
#endif

//#if _HELISIM_AMBIENT_TRAFFIC
//  userCfg.Add("ambientTrafic", Glob.config.ambientTraffic);
//#endif
}

void Config::LoadVideoOptionsConfig()
{
  videoOptions.LoadFromConfig(Pars >> "CfgVideoOptions");
}

static int CCALL PrintFileF( HANDLE file, const char* format, ...)
{
    char szBuff[1024];
    int retValue;

#ifdef _WIN32

    DWORD cbWritten;

    va_list argptr;      
    va_start( argptr, format );

    retValue = vsnprintf( szBuff, sizeof(szBuff), format, argptr );
    if ( retValue < 0 )
        retValue = 1023;
    va_end( argptr );

    WriteFile( file, szBuff, retValue * sizeof(char), &cbWritten, 0 );
    
#else
    
    va_list argptr;      
    va_start( argptr, format );

    retValue = vsnprintf( szBuff, sizeof(szBuff), format, argptr );
    if ( retValue < 0 )
        retValue = 1023;

    va_end( argptr );

    write( file, szBuff, retValue );

#endif

    return retValue;
}

void SetFadeEvent(bool fade)
{
#if PROTECTION_ENABLED
  DEFINE_ENCODED(0x38);
  BString<64> name;
  CREATE_FADE_NAME_STRING(name,CountFadeConditions+1,0x38);
  HANDLE handle = OpenEvent(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,name);
  if (handle)
  {
    if (fade) SetEvent(handle);
    CloseHandle(handle);
  }
#endif
}

bool PrintConfigInfo( HANDLE file )
{
  if (!GEngine)
  {
    return false;
  }
#ifdef _WIN32
  extern const char *MemoryAllocatorName();
  // print some relevant information about SW/HW configuration
  // ask graphical engine do give information
  PrintFileF(file,"Allocator: %s\r\n",MemoryAllocatorName());
#endif
  PrintFileF(file,"graphics:  %s\r\n",(const char *)GEngine->GetDebugName());
  PrintFileF
  (
    file,"resolution:  %dx%dx%d%s\r\n",
    GEngine->Width(),GEngine->Height(),GEngine->PixelSize(),
    GEngine->IsWBuffer() ? ", w-buffer" : ""
  );


  // print addon list
#if _ENABLE_ADDONS
  PrintFileF(file,"Addons:\r\n");
  int lineSize = 0;
  for (AddonInfoMap::Iterator iterator(AddonToPrefix); iterator; ++iterator)
  {
    const AddonInfo &info = *iterator;
    RString oneItem = info.GetName()+" in "+info.GetPrefix();
    if (lineSize+oneItem.GetLength()>80 && lineSize>0)
    {
      PrintFileF(file,"\r\n");
      lineSize = 0;
    }
    if (lineSize>0)
    {
      PrintFileF(file,", %s",(const char *)oneItem);
    }
    else
    {
      PrintFileF(file,"  %s",(const char *)oneItem);
    }
    lineSize += oneItem.GetLength();
  }
  if (lineSize>0)
  {
    PrintFileF(file,"\r\n");
  }
#endif
  RString modList = GetModListShort();
  if (modList.GetLength()>0)
  {
    PrintFileF(file,"Mods: %s\r\n",(const char *)modList);
  }
  int GetDistributionId();
  PrintFileF(file, "Distribution: %d\r\n", GetDistributionId());

  return true;
}

void PrintMissionInfo( HANDLE file )
{
  if (strcmp(Glob.header.filenameReal,Glob.header.filename))
  {
    PrintFileF(file,"file:     %s (%s)\r\n",(const char *)Glob.header.filenameReal,Glob.header.filename);
  }
  else
  {
    PrintFileF(file,"file:     %s\r\n",(const char *)Glob.header.filenameReal);
  }
  PrintFileF(file,"world:    %s\r\n",Glob.header.worldname);
  if (CurrentMission.GetLength()>0) PrintFileF(file,"mission:  %s\r\n",(const char *)CurrentMission);
}

void DestroyMsgFormats();

void Globals::Clear()
{
  // clear variables that are not part of Global structure
  QFBankQueryFunctions::ClearBanks();

  // various dependencies may exists between VehicleTypes and MagazineTypes
  // start by clearing unused vehicle types
  // this should really be everything besides of weapon cross dependencies
  VehicleTypes.ClearCache();
  // vehicle types are used by magazine type - AmmoType is EntityType
  VehicleTypes.Clear();
#if _ENABLE_NEWHEAD
  // Head types destroying
  HeadTypes.Clear();
#endif

  ClearShapes();
  
  DestroyMsgFormats();
}

Globals Glob;

/*!
\patch 1.36 Date 12/12/2001 by Ondra
- New: -cfg command line argument to enable selecting Flashpoint.cfg.
*/

  RString FlashpointCfg = APP_NAME_SHORT ".cfg";
  static RString FlashpointPar = APP_NAME_SHORT ".par";

void ParseFlashpointCfg(ParamFile &file)
{
  RString filename = FlashpointCfg;
  file.Parse(filename, NULL, NULL, &GParsingNamespace); // parsing namespace
}
void SaveFlashpointCfg(ParamFile &file)
{
  RString filename = FlashpointCfg;
  void CreatePath(RString path);
  CreatePath(filename);
  file.Save(filename);
}

RString GetConfigSection()
{
  return InstanceName;
}

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  RString AutoTest="";    //it can contain the config file name containing list of missions to be tested
  int AutoTestExitCode=0; //exit value of the EXE == number of failed missions
#endif

/// sometimes we want no error messages (automated builds)
bool Silent=false;

RString InitCommand;

// memory management import


void InitMemory( int size );
void MemoryFast();
void MemoryGood();
bool MemoryCheck();

/*!
\patch_internal 1.45 Date 2/20/2002 by Jirka
- Added: argument vbs in designer version
*/


/*!
\patch 1.11 Date 07/30/2001 by Jirka
- Added: Dedicated server support
*/
/*!
\patch 1.22 Date 09/10/2001 by Ondra
- Added: Separate exe for dedicated server.
*/
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  #if _SERVER
    const bool DoCreateDedicatedServer=true;
    bool IsDedicatedServer() {return true;}
    bool IsDedicatedClient() { return false; }
    void SetDedicatedServer(bool set) {}
  #else
    static bool DoCreateDedicatedServer=false;
    static bool DoCreateDedicatedClient=false;
    bool IsDedicatedServer() {return DoCreateDedicatedServer;}
    void SetDedicatedServer(bool set) {DoCreateDedicatedServer = set;}
    bool IsDedicatedClient() {return DoCreateDedicatedClient;}
  #endif
  static RString ServerConfig;
  RString GetServerConfig() {return ServerConfig;}
#elif _DEDICATED_CLIENT
    const bool DoCreateDedicatedServer=false;
    void SetDedicatedServer(bool set) {}
    bool IsDedicatedClient() { return true; }
    bool IsDedicatedServer() {return false;}
#else
    void SetDedicatedServer(bool set) {}
    bool IsDedicatedClient() { return false; }
#endif


//}
/*!
\patch 1.11 Date 07/31/2001 by Jirka
- Added: Additional program parameters
- port=...
- password=...
\patch 1.57 Date 5/16/2002 by Jirka
- Fixed: Default network port changed (2234 -> 2302) due to compatibility with DirectPlay 9
*/

extern const int DefaultNetworkPort = 2302;
static int NetworkPort = DefaultNetworkPort;
int GetNetworkPort() {return NetworkPort;}
static int ServerPort = DefaultNetworkPort;
int GetServerPort() {return ServerPort;}
void SetServerPort(int port) {ServerPort = port;}
static RString NetworkPassword;
RString GetNetworkPassword() {return NetworkPassword;}
static RString LiveGamertag;
RString GetLiveGamertag() {return LiveGamertag;}

static int DSBandwidth = -1;
int GetDSBandwidth() {return DSBandwidth;}
void SetDSBandwidth(int bandwidth) {DSBandwidth = bandwidth;}
static int DSPrivateSlots = 0;
int GetDSPrivateSlots() {return DSPrivateSlots;}
void SetDSPrivateSlots(int privateSlots) {DSPrivateSlots = privateSlots;}

static RString LiveSession;
RString GetLiveSession() {return LiveSession;}
void ResetLiveSession() {LiveSession = RString();}

static bool ContentDownload = false;
bool IsContentDownload() {return ContentDownload;}
void ResetContentDownload() {ContentDownload = false;}

#if _ENABLE_DEDICATED_SERVER

/// PidFilename!="" -> exclusive network port usage
static RString PidFileName;
RString GetPidFileName() {return PidFileName;}

/// This OFP instance has created the <pid_file>.
bool MyPidFile = false;

#endif

// path for BattlEye files cache
static RString BEPath; 
RString GetBEPath() {return BEPath;}

/**************************************************************************
  DirectInput Globals
**************************************************************************/

#if _DISABLE_GUI
  #define ENABLE_DI_JOYSTICK 0
#else
  #define ENABLE_DI_JOYSTICK 1
#endif

#if _DISABLE_GUI
  #define ENABLE_JOYSTICK 0
#else
  #define ENABLE_JOYSTICK 1
#endif

#if ENABLE_KEYMOUSE
  static IDirectInput8 *dInput;

  void DIError( const char *op, HRESULT err );

#endif

/*!
\patch_internal 1.05 Date 7/18/2001 by Jirka
- Added: enable access to flags GAppActive, GAppPaused, GAppIconic from network.cpp
*/
bool IsAppPaused()
{
#if _ENABLE_DEDICATED_SERVER
  // dedicated server can never be paused
  if (IsDedicatedServer()) return false;
#endif 
  //@{ _DEDICATED_CLIENT
  if ( IsDedicatedClient() )
  return false;
  //@}
  // command line may force app to be never paused because of focus
  if (!PauseInBackround) return false;
  return !GAppActive || GAppPaused || GAppIconic;
}

bool IsAppFocused()
{
  return GAppActive && !GAppIconic;
}

#if _VERIFY_KEY

# include <El/CDKey/serial.hpp>

template <typename Type=unsigned char, size_t size=KEY_BYTES>
struct KeyRepresentation
{
  Type cdkey[size];

  ~KeyRepresentation()
  {
    // optimizer could remove the value cleanup as the value is not used any more
    // to prevent this, we need to mark it as volatile
    for (int i=0; i<sizeof(cdkey); i++) const_cast<volatile Type *>(cdkey)[i] = 0;
  }

  Type operator [] (int i) const {return cdkey[i];}
  operator const Type *() const {return cdkey;}
};

#endif

#if (_VERIFY_KEY || _VERIFY_KEY_EXT) && !INTERNAL_KEYS

# include <El/CDKey/serial.hpp>

// interface to registry key
bool GetKeyFromRegistry(const char *path, unsigned char *cdkey)
{
  memset(cdkey, 0, KEY_BYTES);
  STATIC_ASSERT(!CDKeyRegistryUser); // CD keys in the user profile no longer supported
  HKEY root = CDKeyRegistryUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
  HKEY key;
  if
  (
    ::RegOpenKeyEx
    (
      root, path,
      0, KEY_READ, &key
    ) != ERROR_SUCCESS
  ) return false;

  DWORD size = KEY_BYTES;
  DWORD type = REG_BINARY;
  HRESULT result = ::RegQueryValueEx(key, "KEY", 0, &type, cdkey, &size);
  ::RegCloseKey(key);
  return result == ERROR_SUCCESS && size == KEY_BYTES;
}

#endif


#if _VERIFY_KEY && _ENABLE_DISTRIBUTIONS

# include <El/CDKey/serial.hpp>

RString GetConfigAppFromRegistry()
{
  KeyRepresentation<> cdkey;
  #define GET_KEY_NAME(path) if (GetKeyFromRegistry(ConfigAppRoot path, cdkey.cdkey)) return ConfigAppRoot path;
  APP_REGISTRY(GET_KEY_NAME);
  return ConfigApp;
}

static bool GetMainKeyFromRegistry(KeyRepresentation<> &cdkey)
{
  #define GET_KEY(path) if (GetKeyFromRegistry(ConfigAppRoot path, cdkey.cdkey)) return true;
  APP_REGISTRY(GET_KEY);
  return false;
}

#else

RString GetConfigAppFromRegistry()
{
  return ConfigApp;
}

#endif


#if _VERIFY_KEY

//CDKey GCDKey("Global");

#if INTERNAL_KEYS
// Note, each application should set it to match its name. But the name can change during time, so the APP_NAME is rather not a good idea.
#define INT_CDKEY_GEN_ID "Take On Helis"
void InitInternalCDKey(CDKey &key, int id)
{
  RString cdkey = Format("%s-DEV:%03d",INT_CDKEY_GEN_ID, id);
  HashCalculator calculator;
  calculator.Add(cc_cast(cdkey), cdkey.GetLength());
  AutoArray<char> hashkey;
  calculator.GetResult(hashkey);
  unsigned char cdKeyHashed[KEY_BYTES] = {0}; //zeroed
  memcpy( cdKeyHashed, hashkey.Data(), intMin(KEY_BYTES, hashkey.Size()) );
  key.Init(cdKeyHashed, NULL);
}
#endif

static CDKey GetGCDKey()
{
  CDKey key;
  #if INTERNAL_KEYS
    InitInternalCDKey(key, customUID);
    return key;
  #else
    KeyRepresentation<> cdkey;
    if (!GetMainKeyFromRegistry(cdkey))
    {
      return key;
    }

    #if NEW_KEYS

      key.Init(cdkey, NULL);
      return key;
    #else
      #error NEW_KEYS required
      key.Init(cdkey, CDKeyPublicKey);
      return key;
    #endif
  #endif // INTERNAL_KEYS
}
#endif

// Returns the CD key like stored in registry, used for SecureId registration
AutoArray<unsigned char, SecuredAllocator<> > GetCDKeyBinary()
{
  #if _VERIFY_KEY
  return AutoArray<unsigned char, SecuredAllocator<> >(GetGCDKey().GetBinaryMessage(),KEY_BYTES);
  #else
  Fail("CD Key not available for internal version");
  return AutoArray<unsigned char, SecuredAllocator<> >();
  #endif
}


static void SkipKeysInThisFrame()
{
  if (!hwndApp) return; // no process while in the splash screen
  // until frame ID is increased, ignore all input
  SkipKeysUntilFrame = GInput.frameID;
  // we can forget the keys now
  GInput.ForgetKeys();
#if ENABLE_KEYMOUSE
  // re-register the device to reduces the impact of the Steam Overlay issue (WM_INPUT blocked by chat)
  if (IsDedicatedClient() || IsDedicatedServer()) return;
  CreateKeyboard((HWND)hwndApp,false);
#endif
}

#if ENABLE_DI_JOYSTICK
  // DI Joysticks
  extern SRef<JoystickDevices> GJoystickDevices;
#endif
#if ENABLE_JOYSTICK
  // XInput Controller
  extern SRef<IJoystick> XInputDev;
#endif

#if defined(_WIN32) 
extern SRef<TrackIR> TrackIRDev;
  extern SRef<FreeTrack> FreeTrackDev;
#if _SPACEMOUSE
  extern SRef<SpaceMouse> SpaceMouseDev;
#endif
#endif

/*!
\patch 1.23 Date 09/11/2001 by Ondra
- Fixed: iFeel effects did not work.
*/

void SynchronizeCursors()
{
#if 0 // defined _WIN32
if (HideCursor && UseWindow)
{
  RECT rect;
  if (GetClientRect((HWND)hwndApp,&rect))
  {
    //move cursor to the center of window to avoid random touches at window edge.
    float x = 0.5f;//GInput.cursorX*0.5f+0.5f;
    float y = 0.5f;//GInput.cursorY*0.5f+0.5f;
    POINT wp;
    wp.x = toInt((rect.right-rect.left)*x+rect.left);
    wp.y = toInt((rect.bottom-rect.top)*y+rect.top);
    if (ClientToScreen((HWND)hwndApp,&wp))
    {
      // make sure cursor is on screen?
      //LogF("SetCursorPos %d,%d",wp.x,wp.y);
      SetCursorPos(wp.x,wp.y);
    }
  }
}
#endif
}

#if defined(_WIN32) 
RString GetWindowName(HWND hwnd)
{
  if (hwnd)
  {
    char buf[1024];
    GetWindowText(hwnd,buf,sizeof(buf));
    return buf;
  }
  return "";
}
#endif

void EnableDesktopCursor(bool enable)
{
}


static bool MouseCanMove=true;

static void ClipMouse()
{
  //GInput.cursorX = float(xPos)/GEngine->Width()*2-1;
  //GInput.cursorY = float(yPos)/GEngine->Height()*2-1;
  //DoClipMouse();
#if defined(_WIN32) 
  // instruct the engine to hide the mouse
  if (GEngine)
  {
    // without this handling the cursor was visible when running windowed and switching
    // into the game with cursor outside of the window
    GEngine->CursorOwned(true);
    // hide the cursor
    // note: position is not used at all
    GEngine->SetCursor(NULL,0,0,0,0,PackedWhite,0);
    // hide the system cursor
    SetCursor( NULL );
  }
# if ENABLE_KEYMOUSE
  CreateMouseDevice((HWND)hwndApp, true);
  // re-register the device to reduces the impact of the Steam Overlay issue (WM_INPUT blocked by chat)
  CreateKeyboard((HWND)hwndApp,false);
# endif
  // limit the mouse position inside of the window
  if (UseWindow)
  {
    RECT windowRect;
    GetWindowRect((HWND)hwndApp, &windowRect);
    ClipCursor(&windowRect);
  }
  if (MouseCanMove) ShowCursor(FALSE);
#endif
  MouseCanMove = false;
}

static void StopClippingMouse()
{
#if defined(_WIN32) 
  if (UseWindow)
    ClipCursor(NULL);
  // synchronize windows cursor back from the UI one
  //int w = GEngine->Width();
  POINT wp;
  wp.x = toInt((GInput.cursorX*0.5+0.5)*GEngine->WidthBB());
  wp.y = toInt((GInput.cursorY*0.5+0.5)*GEngine->HeightBB());
  if (ClientToScreen((HWND)hwndApp,&wp))
  {
    SetCursorPos(wp.x,wp.y);
  }
  //GInput.cursorX = float(xPos)/GEngine->Width()*2-1;
  //GInput.cursorY = float(yPos)/GEngine->Height()*2-1;
# if ENABLE_KEYMOUSE
  CreateMouseDevice((HWND)hwndApp, false);
  // re-register the device to reduces the impact of the Steam Overlay issue
  CreateKeyboard((HWND)hwndApp,false);
# endif
  if (!MouseCanMove) ShowCursor(TRUE);
#endif
  MouseCanMove = true;
}

static void EnableMouseMovement(bool enableMovement, bool force=false)
{
  if ((MouseCanMove!=enableMovement) || force)
  {
    PROFILE_SCOPE_EX(mmAct,*);
    if (enableMovement) StopClippingMouse();
    else ClipMouse();
  }
}

// TrackIR (re)initialization
void InitTrackIR()
{
#if defined(_WIN32) 
  if (TrackIRDev) TrackIRDev.Free();
  int id = Pars >> "TrackIR_Developer_ID";
  int appKeyHigh = Pars >> "TrackIR_Developer_AppKeyHigh";
  int appKeyLow = Pars >> "TrackIR_Developer_AppKeyLow";
  TrackIRDev=new TrackIR(id, appKeyHigh, appKeyLow);
  TrackIRDev->Init((HWND)hwndApp);
#endif
}

void InitFreeTrack()
{
#if defined(_WIN32) 
  if (FreeTrackDev) FreeTrackDev.Free();
  FreeTrackDev = new FreeTrack();
  FreeTrackDev->Init();
#endif
}

/**************************************************************************
  InitDInput
**************************************************************************/
bool InitDInput( HWND hwnd )
{
#ifdef _X360SHADERGEN
  return true;
#else
  #if ENABLE_KEYMOUSE
  if (IsDedicatedClient() || IsDedicatedServer()) return true;

  HRESULT err = DirectInput8Create
  (
    (HINSTANCE)hInstApp, DIRECTINPUT_VERSION, IID_IDirectInput8 ,(void **)&dInput, NULL
  );

  if(err != DI_OK)
  {
    goto fail;
  }

#if 0
  // enumerate all attached devices
  unsigned int count;
  if (GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST)) >= 0)
  {
    RAWINPUTDEVICELIST *list = new RAWINPUTDEVICELIST[count];
    if (GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST)) >= 0)
    {
      for (unsigned int i=0; i<count; i++)
      {
        HANDLE device = list[i].hDevice;
        unsigned int size = 0;
        if (GetRawInputDeviceInfo(device, RIDI_DEVICENAME, NULL, &size) >= 0)
        {
          AUTO_STATIC_ARRAY(char, name, 256);
          name.Resize(size);
          if (GetRawInputDeviceInfo(device, RIDI_DEVICENAME, name.Data(), &size) >= 0)
          {
            RptF("Device %s:", cc_cast(name.Data()));
          }
        }

        RID_DEVICE_INFO info;
        size = sizeof(info);
        memset(&info, 0, size);
        info.cbSize = size;
        if (GetRawInputDeviceInfo(device, RIDI_DEVICEINFO, &info, &size) >= 0)
        {
          switch (info.dwType)
          {
          case RIM_TYPEMOUSE:
            RptF(" - mouse");
            break;
          case RIM_TYPEKEYBOARD:
            RptF(" - keyboard");
            break;
          case RIM_TYPEHID:
            RptF(" - page 0x%02x, usage 0x%02x", info.hid.usUsagePage, info.hid.usUsage);
            break;
          }
        }
      }
    }
    delete [] list;
  }
#endif

  if (!CreateMouseDevice(hwnd, !MouseCanMove))
  {
    goto fail;
  }

  if (!CreateKeyboard(hwnd,true))
  {
    goto fail;
  }

  if( !LandEditor )
  {
    GJoystickDevices = CreateJoysticks();
    GInput.ReInitDIJoysticks();
    XInputDev = CreateXInput();
    XInputDev->Init(NULL,hwnd,dInput);
    InitTrackIR();
    InitFreeTrack();
  }
#if _SPACEMOUSE
  SpaceMouseDev = new SpaceMouse(hwnd);  
#endif

  // if we get here, all DirectInput objects were created ok
  return true;

  fail:
  ErrorMessage("Cannot initialize DirectInput. ( DirectX %d required.)",DIRECTINPUT_VERSION>>8);
  if (dInput) dInput->Release(), dInput     = NULL;
  return false;
  #else
  return true;
  #endif

#endif
}

static void FinishDInput()
{
  #if ENABLE_KEYMOUSE
  DestroyMouseDevice();
  #endif
}

void InitWorld();
void DoneWorld();

static void WorldDestroy()
{
  if( GEngine ) GEngine->StopAll();
  if (GWorld)
  {
    DoneWorld();
    delete GWorld;
    GWorld = NULL;
    GScene = NULL;
  }
  GlobPreloadedTexturesClear();
}

static bool AppWinCreate( HWND hwnd )
{
#ifdef _WIN32
  if( !InitDInput(hwnd) )
  {
    return false;
  }
#endif
  //StartTimers(hwnd);
  if( GWorld ) GWorld->UnloadSounds();
  if( GSoundScene ) delete GSoundScene,GSoundScene=NULL;
  if( GSoundsys ) delete GSoundsys,GSoundsys=NULL;
  //{ NEW For dedicated server or with nosound argument create dummy engine
#if defined _WIN32

  bool mute = (
    NoSound ||
#if _ENABLE_DEDICATED_SERVER
    DoCreateDedicatedServer ||
#endif
    IsDedicatedClient()
  );
  //if (UsedSoundProvider == SPXAudio2)
  {
    GSoundsys=new SoundSystemXA2((HWND)hwndApp,mute);
  }

#elif defined (__GNUC__)
  // TODO: maybe dummy XAudio2 implementation would server better in order to clear OAL at all
  GSoundsys=new SoundSystemOAL(); // we use dummy OAL implementation on Linux server (oalDummy.cpp)
#endif
  //}

#ifndef _X360SHADERGEN
  GSoundScene=new SoundScene;
#endif

  GSoundsys->SetListener(VZero, VZero, VForward, VUp); //init (there should be some value on dedicated servers)

  return true;
}


#if _ENABLE_BULDOZER

/// Buldozer animation manager
class AnimationManager
{
  /// list of all controllers
  FindArrayRStringCI _controllerName;
  /// values currently set for controllers
  AutoArray<float> _controllerValue;
  /// name of current controller
  RString _actController;

  int FindControllerIndex() const {return _controllerName.Find(_actController);}
  int SetActController(int index);
  int ValidateActController();
  
  public:
  /// select previous controller
  void PrevAnim();
  /// select next controller
  void NextAnim();
  
  /// change current controller value
  void AdvanceAnim(float amount);
  /// refresh list of controllers
  void Refresh();
};

int AnimationManager::SetActController(int index)
{
  // if there are no controllers, we cannot use any
  if (_controllerName.Size()<=0)
  {
    _actController = RString();
    return -1;
  }
  if (index<0)
  {
    index = 0;
  }
  _actController = _controllerName[index];
  return index;
}

int AnimationManager::ValidateActController()
{
  int index = FindControllerIndex();
  return SetActController(index);
}

void AnimationManager::Refresh()
{
  FindArrayRStringCI oldNames = _controllerName;
  AutoArray<float> oldValues = _controllerValue;
  GWorld->GetViewerControllers(_controllerName);
  // keep old controllers as much as possible
  _controllerValue.Realloc(_controllerName.Size());
  _controllerValue.Resize(_controllerName.Size());
  for (int i=0; i<_controllerValue.Size(); i++)
  {
    _controllerValue[i] = 0;
  }
  for (int i=0; i<_controllerName.Size(); i++)
  {
    int oldIndex = oldNames.Find(_controllerName[i]);
    if (oldIndex<0) continue;
    _controllerValue[i] = oldValues[oldIndex];
  }
}


void AnimationManager::PrevAnim()
{
  int index = FindControllerIndex()-1;
  if (index<0) index = _controllerName.Size()-1;
  SetActController(index);
  GlobalShowMessage(1000,"Anim %s",cc_cast(_actController));
}
void AnimationManager::NextAnim()
{
  int index = FindControllerIndex()+1;
  if (index>=_controllerName.Size()) index = 0;
  SetActController(index);
  GlobalShowMessage(1000,"Anim %s",cc_cast(_actController));
}

void AnimationManager::AdvanceAnim(float amount)
{
  int index = ValidateActController();
  if (index<0) return;
  _controllerValue[index] += amount;
  // pass given value to the viewer
  float minVal,maxVal;
  GWorld->SetViewerController(_actController,_controllerValue[index],minVal,maxVal);
  if (minVal>-FLT_MAX && maxVal<+FLT_MAX)
  {
    saturate(_controllerValue[index],minVal,maxVal);
    GlobalShowMessage(
      1000,"%s set to %.3f (%.3f..%.3f)",
      cc_cast(_actController),_controllerValue[index],minVal,maxVal
    );
  }
  else
  {
    GlobalShowMessage(
      1000,"%s set to %.3f (no limit)",
      cc_cast(_actController),_controllerValue[index]
    );
  }
}

static AnimationManager GAnimationManager;
#endif

#if _VERIFY_KEY

#ifdef PRODUCT_ID
/// Update 8-bit CRC value using polynomial X^8 + X^5 + X^4 + 1
__forceinline void UpdateCRC8(unsigned char &crc, unsigned char src)
{
  static const int polyVal = 0x8C;

  for (int i=0; i<8; i++)
  {
    if ((crc ^ src) & 1) crc = (crc >> 1 ) ^ polyVal;
    else crc >>= 1;
    src >>= 1;
  }
}

__forceinline int DecodeProductId(const unsigned char *cdkey)
{
  unsigned char crc = 0;
  for (int i=0; i<KEY_BYTES-1; i++) UpdateCRC8(crc, cdkey[i]);
  return cdkey[KEY_BYTES - 1] ^ crc;
}

#define TEST_PRODUCT_ID(ProductID) \
  if ((crc ^ cdkey[KEY_BYTES - 1]) == ProductID) return true;

__forceinline bool CheckProductID(const unsigned char *cdkey, unsigned char crc)
{
#if _ENABLE_REPORT
  RptF("ProductID = %d, crc = 0x%x, cdkey[KEY_BYTES - 1] = 0x%x", crc ^ cdkey[KEY_BYTES - 1], crc, cdkey[KEY_BYTES - 1]);
#endif
  PRODUCT_ID(TEST_PRODUCT_ID)
  return false;
}
#endif

// NEW CD_KEY VERIFY
/*!
\patch_internal 1.01 6/20/2001 Changed inline to __forceinline
to make cracking a little bit harder.
*/
__forceinline bool VerifyKey()
{
#if INTERNAL_KEYS
  return true;
#else
  CDKey key = GetGCDKey();
#if NEW_KEYS
  const unsigned char *keyData = key.GetBinaryMessage();
#ifdef PRODUCT_ID
  unsigned char crc = 0;
  for (int i=0; i<KEY_BYTES-1; i++) UpdateCRC8(crc, keyData[i]);
  if ( !CheckProductID(keyData, crc) ) return false;
#endif

  unsigned int message = *(const unsigned int *)keyData;
  unsigned int signature = message >> (DISTR_BITS + PLAYER_BITS);
  message &= (1 << (DISTR_BITS + PLAYER_BITS)) - 1;
  return signature == ((((unsigned char *)&message)[0] * 7 +
    ((unsigned char *)&message)[1] * 11 +
    ((unsigned char *)&message)[2] * 13 +
    ((unsigned char *)&message)[3] * 17) & ((1 << SIMPLE_HASH_BITS) - 1));
#else
  return key.Check(CDKeyIdLength, CDKeyFixed);
#endif
#endif // INTERNAL_KEYS
}

#endif

#if _VERIFY_KEY_EXT
__forceinline bool VerifyKeyExt()
{
  // no Win32 function may be called here
  // as it would make protection impossible

  unsigned char cdkey[KEY_BYTES];
  if (!GetKeyFromRegistry(ConfigAppExt, cdkey))
  {
    return false;
  }

  static const unsigned char publicKey[] =
  {
    0x13, 0x00, 0x00, 0x00,
    0xF7, 0xB0, 0x64, 0xD7, 0xDB, 0x87, 0x52, 0x27, 0x57, 0x91, 0xB1, 0x9E, 0x0E, 0x81, 0x88
  };
  CDKey keyExt;
  keyExt.Init(cdkey, publicKey);

# error Needs maintenace if used

  _int64 value = keyExt.GetValue(0, 5);
  return keyExt.Check(5, "Resistance") && value >= _VERIFY_KEY_BEGIN && value < _VERIFY_KEY_END;
}
#endif

//const int DEF_W = 1024, DEF_H = 768;
const int DEF_W = 800, DEF_H = 600;

extern int MaxMsgSend;
extern int DSMaxMsgSend;
extern int MaxSizeGuaranteed;
extern int MaxSizeNonguaranteed;
extern int MinBandwidth;
extern int DSMinBandwidth;
extern int MaxBandwidth;
extern float MinErrorToSend;
extern float MinErrorToSendNear;
extern int MaxCustomFileSize;

#define DECLARE_PRIORITY	\
  HANDLE hProc = GetCurrentProcess();\
  HANDLE hThrd = GetCurrentThread();\
  DWORD dwPrtyCls = GetPriorityClass(hProc);\
  DWORD dwPrty = GetThreadPriority(hThrd)

#define BOOST_PRIORITY	\
  SetPriorityClass(hProc, REALTIME_PRIORITY_CLASS  /*HIGH_PRIORITY_CLASS*/);\
  SetThreadPriority(hThrd, THREAD_PRIORITY_HIGHEST)\

#define RESTORE_PRIORITY	\
  SetThreadPriority(hThrd, dwPrty);\
  SetPriorityClass(hProc, dwPrtyCls)

#pragma optimize("t", on)

static int Benchmark()
{
#if defined _WIN32
//  AfxGetApp()->BeginWaitCursor();

#define NVec 100000

  DECLARE_PRIORITY;
  BOOST_PRIORITY;

  Temp<Vector3> src(NVec), dst(NVec);
  DWORD timeStart = 0; // will be replaced
  for (int b=0; b<20; b++)
  {
    int i;
    if (b == 2) timeStart = ::GetTickCount();
    for (i=0; i<NVec; i++)
    {
      src[i] = Vector3(0.2f, 0.6f, -0.5f);
    }
    Matrix4 translation(MTranslation, Vector3(56.23f * b, 253.2f, -569.3f));
    Matrix4 rotationY(MRotationY, 1.25f * b);
    Matrix4 rotationZ(MRotationZ, -0.66f * b);
    for (i=0; i<NVec; i++)
    {
      Matrix4 transform = rotationZ * rotationY * translation;
      dst[i].SetFastTransform(transform, src[i]);
    }
  }
  DWORD timeEnd = ::GetTickCount();
  int time = timeEnd-timeStart;
  if (time<1) time = 1;

  // AfxGetApp()->EndWaitCursor();
  RESTORE_PRIORITY;
  return toInt(1500000.0/time);
#else
  return 1;
#endif
};

#pragma optimize("", on)

// all supported languages should be listed
static RString GetDefaultLanguage()
{
#if !defined _WIN32  
  // TODOX360: default language
  return "English";
#else
  switch (PRIMARYLANGID(GetUserDefaultLangID()))
  {
#if !_VBS2 //only allow english for now
  case LANG_FRENCH:
    return "French";
  case LANG_SPANISH:
    return "Spanish";
  case LANG_ITALIAN:
    return "Italian";
  case LANG_GERMAN:
    return "German";
  case LANG_CZECH:
    return "Czech";
  case LANG_POLISH:
    return "Polish";
  case LANG_RUSSIAN:
    return "Russian";
  case LANG_HUNGARIAN:
    return "Hungarian";
  case LANG_PORTUGUESE:
    return "Portuguese";
  case LANG_JAPANESE:
    return "Japanese";
#endif
  default:
    return "English";
  }
#endif
}

int GetProductId()
{
#if _VERIFY_KEY && _ENABLE_DISTRIBUTIONS
  // force language based on the distribution id
  KeyRepresentation<> cdkey;
  if (GetMainKeyFromRegistry(cdkey))
  {
    return DecodeProductId(cdkey);
  }
  else
  {
    Fail("Internal key has no product ID");
    return 0;
  }
#else
  // by default (internal versions) return false - ProductId is used to restrict features for some specific products (Reinforcements)
  return false;
#endif
}


bool CheckProductId(int productId)
{
#if _VERIFY_KEY && _ENABLE_DISTRIBUTIONS
  KeyRepresentation<> cdkey;
  if (GetMainKeyFromRegistry(cdkey))
  {
    return DecodeProductId(cdkey)==productId;
  }
  else
  {
    return false;
  }
#else
  // by default (internal versions) return false - ProductId is used to restrict features for some specific products (Reinforcements)
  return false;
#endif
}

RString CheckLanguage(RString language)
{
	if( stricmp(language, "French") != 0 &&
		stricmp(language, "Spanish") != 0 &&
		stricmp(language, "Italian") != 0 &&
		stricmp(language, "German") != 0 &&
		stricmp(language, "Russian") != 0 &&
		stricmp(language, "Polish") != 0 &&
		stricmp(language, "Czech") != 0 &&
		stricmp(language, "Hungarian") != 0 &&
		stricmp(language, "Japanese") != 0 )
  {
    return "English";
  }

   return language;
}


/*!
\patch_internal 1.24 Date 6/26/2001 by Ondra.
- Optimized: dedicated server memory usage.
\patch_internal 5128 Date 2/8/2007 by Jirka
- New: English enabled in Czech and German distribution
*/

#if _MOTIONCONTROLLER
#include "hla/MotionController.hpp"
#endif

static bool ReadRegistry()
{
  Glob.config.tacticalZ=1600;
  Glob.config.horizontZ=1600;
  Glob.config.objectsZ=900;
#if _HELISIM_VIEW_DIST
  Glob.config.vehiclesZ = 900;
  Glob.config.treesZ = 900;
  Glob.config.staticZ = 900;
#endif
  Glob.config.roadsZ=900;
  Glob.config.SetShadowZ(Glob.config.DefaultShadowZDistance);
  Glob.config.radarZ=2500;

  Glob.config.benchmark = 1000;

  Glob.config.wantBpp = 32;
  Glob.config.wantW = DEF_W;
  Glob.config.wantH = DEF_H;

  Glob.demo = false;

  Glob.config.trackTimeToLive=60;
  Glob.config.invTrackTimeToLive=1/Glob.config.trackTimeToLive;

  #define LOAD_DEF_CFG(READ_FROM_CFG_STRING,NAME_OF_VAR) NAME_OF_VAR = cfg.ReadValue(READ_FROM_CFG_STRING,NAME_OF_VAR)

  if (QIFileFunctions::FileExists(FlashpointCfg))
  {
    // FlashpointCfg file found
    ParamFile cfg;
    ParseFlashpointCfg(cfg);

    //Glob.config.benchmark = Benchmark();
    
    // core values
    Glob.config.benchmark = cfg >> "3D_Performance";
    D3DAdapter = cfg >> "adapter";
    if(cfg.FindEntry("Resolution_W") && cfg.FindEntry("Resolution_H"))
    {//if not present, setDefault() will take resolution from windows
      int value = cfg >> "Resolution_W";
      if (value > 0) Glob.config.wantW = value;
      value = cfg >> "Resolution_H";
      if (value > 0) Glob.config.wantH = value;
    }
    int value = cfg >> "Resolution_Bpp";
    if (value > 0) Glob.config.wantBpp = value;

    // optional values
    ConstParamEntryPtr entry = cfg.FindEntry("MaxMsgSend");
    if (entry)
    {
      MaxMsgSend = *entry;
      DSMaxMsgSend = *entry;
    }
    entry = cfg.FindEntry("MaxSizeGuaranteed");
    if (entry) MaxSizeGuaranteed = *entry;
    entry = cfg.FindEntry("MaxSizeNonguaranteed");
    if (entry) MaxSizeNonguaranteed = *entry;
    entry = cfg.FindEntry("MinBandwidth");
    if (entry)
    {
      MinBandwidth = *entry;
      DSMinBandwidth = *entry;
    }
    entry = cfg.FindEntry("MaxBandwidth");
    if (entry) MaxBandwidth = *entry;
    entry = cfg.FindEntry("MinErrorToSend");
    if (entry) MinErrorToSend = *entry;
    entry = cfg.FindEntry("MinErrorToSendNear");
    if (entry) MinErrorToSendNear = *entry;
    saturateMax(MinErrorToSend,0); // cannot be negative, would break everything
    saturateMax(MinErrorToSendNear,0); // cannot be negative, would break everything
    entry = cfg.FindEntry("MaxCustomFileSize");
    if (entry) MaxCustomFileSize = *entry;

    entry = cfg.FindEntry("demo");
    if (entry) Glob.demo = *entry;

#if _VBS3
    Glob.config.maxViewDistance    = cfg.ReadValue("MaxViewDistance",10000.0f);
    Glob.config.maxObjDrawDistance = cfg.ReadValue("MaxObjectDrawDistance",3000.0f);
    if (Glob.config.maxObjDrawDistance < 3000.0f ||
        Glob.config.maxObjDrawDistance > Glob.config.maxViewDistance )
    {
        Glob.config.maxObjDrawDistance = 3000.0f;

        cfg.Add("MaxObjectDrawDistance", Glob.config.maxObjDrawDistance);
        cfg.Add("MaxViewDistance", Glob.config.maxViewDistance);
        SaveFlashpointCfg(cfg);
    }
#endif

#if _MOTIONCONTROLLER
    int port = cfg.ReadValue("MotionController_Port", 32000);
    RString ip = cfg.ReadValue("MotionController_IP", RString("127.0.0.1"));
    bool enableMC = cfg.ReadValue("MotionController_Enabled", true);
    int maxRate = cfg.ReadValue("MotionController_MaxMsgPerSecond", 40);

    if(!cfg.FindEntry("MotionController_MaxMsgPerSecond"))
    {
      cfg.Add("MotionController_Port", port);
      cfg.Add("MotionController_IP", ip);
      cfg.Add("MotionController_Enabled",enableMC);
      cfg.Add("MotionController_MaxMsgPerSecond",maxRate);
      SaveFlashpointCfg(cfg);
    }
    if(enableMC)
      GMotionController.Init(ip, port, maxRate);
#endif

#if _VBS3_CRATERS_LIMITCOUNT
    Glob.config.maxPermanentCraters = cfg.ReadValue("MaxPermanentCraters",200);
#endif
  }
  else
  {
    RString language = GetDefaultLanguage();
    language = CheckLanguage(language);

    Glob.config.benchmark = Benchmark();

    // Fill the core values and save
    ParamFile cfg;
    cfg.Add("language", language);
    cfg.Add("adapter", D3DAdapter);
    cfg.Add("3D_Performance", Glob.config.benchmark);
    cfg.Add("Resolution_Bpp", Glob.config.wantBpp);

#if _VBS3
    Glob.config.maxViewDistance    = 10000.0f;
    Glob.config.maxObjDrawDistance = 3000.0f;

#if _MOTIONCONTROLLER
    int port = 32000;
    RString ip = "127.0.0.1";
    bool enableMC = true;
    int maxRate = 40;

    cfg.Add("MotionController_Port", port);
    cfg.Add("MotionController_IP", ip);
    cfg.Add("MotionController_Enabled",enableMC);
    cfg.Add("MotionController_MaxMsgPerSecond",maxRate);

    GMotionController.Init(ip, port, maxRate);
#endif

#endif

#if _VBS3_CRATERS_LIMITCOUNT
    Glob.config.maxPermanentCraters = 200;
    cfg.Add("MaxPermanentCraters",Glob.config.maxPermanentCraters);
#endif

    SaveFlashpointCfg(cfg);
  }
  // assume some minimal performance - number below this probably means the test has failed somehow
  saturateMax(Glob.config.benchmark,3000);
  return true;
}

#if PROTECTION_ENABLED
static __forceinline void RestartFadeTimer(HANDLE timer)
{
  LARGE_INTEGER time;
  // simple random - based on the time when it was activated
  int timeMs = (GetTickCount()&1023)*(5*60-60)+60000; // delay 1 .. 5 minutes
  #if _ENABLE_CHEATS
  LogF("Restarting fade timer: %d ms",timeMs);
  #endif
  // given in 100 ns, 1m is 10000 ns
  // negative means delay
  time.QuadPart = -timeMs * 10000LL;
  // start the timer
  SetWaitableTimer(timer,&time,0,NULL,NULL,FALSE);
}
#endif

void DecFadeSemaphore(int id)
{
#if PROTECTION_ENABLED
  DEFINE_ENCODED(0x91);
  { // semaphore handling
    BString<64> name;
    CREATE_FADE_NAME_STRING(name,id,0x91);

    HANDLE handle = OpenSemaphore(SYNCHRONIZE|SEMAPHORE_MODIFY_STATE,FALSE,name);
    if (handle)
    {
      // decrement the count
      if (WaitForSingleObject(handle,0)!=WAIT_OBJECT_0)
      {
        LogF("FAILED");
      }
      CloseHandle(handle);
    }
  }
#if _ENABLE_CHEATS
  LogF("Fade %d even received",id);
#endif
  if (id==FadeMissionInit)
  {
    // if the timer has already signaled when the mission starts, we want to introduce an additional delay
    BString<64> name;
    CREATE_FADE_NAME_STRING(name,CountFadeConditions,0x91);
    HANDLE timer = OpenWaitableTimer(SYNCHRONIZE|TIMER_MODIFY_STATE,FALSE,name);
    if (timer)
    {
      if (WaitForSingleObject(timer,0)==WAIT_OBJECT_0)
      {
        RestartFadeTimer(timer);
      }
      CloseHandle(timer);

    }
  }
#endif
}

#  include <Es/Framework/logflags.hpp>

#ifndef _SUPER_RELEASE
extern bool GLogFileServer;
extern bool GLogDVD;
#endif

#ifdef _WIN32

void SetLocalSettingPaths()
{
  // move path for reports to local setting application data (where write access is granted even for limited users)
  char exeName[MAX_PATH];
  GetModuleFileName(NULL, exeName, MAX_PATH);

  char logFilename[MAX_PATH];
  if (GetLocalSettingsDir(logFilename))
  {
    CreateDirectory(logFilename, NULL);
    TerminateBy(logFilename, '\\');
    GetFilename(logFilename + strlen(logFilename), exeName);
    strcat(logFilename, ".RPT");
    GDebugExceptionTrap.SetLogFileName(logFilename);
  }
}
#endif

/// check if an option matches

static bool IsOption(const char *beg, const char *end, const char *opt)
{
  return end-beg==(int)strlen(opt) && !strnicmp(beg,opt,end-beg);
}

/// check if an option with arguments matches
static bool IsOptionWArgs(const char *beg, const char *opt)
{
  // when this is used, the last character of opt should be '=', like in -x=800
	//This is actually not true, as it's not used this way at all
  Assert(opt[0]!=0/* && opt[strlen(opt)-1]=='='*/);
  return !strnicmp(beg,opt,strlen(opt));
}

struct SingleArgumentFinish
{
  enum SAType {SASingle, SAConnect, SAPassword} type;
  SingleArgumentFinish() : type(SASingle) {}
  void SetType(SAType stype) { type=stype; }
  void Reset() { type=SASingle; }
  bool Process(const char *beg, const char *end);
};

//! SingleArgumentFinish provides support for commandline options which are split into several parts by white char (space)
//! it is needed for SteamAPI support, where API overlay calls the game with commandline parameters like
//!     +connect <ip:port>
//!     +password <password>
//! instead of our typical -connect=<ip:port> etc.
bool SingleArgumentFinish::Process(const char *beg, const char *end)
{
  switch (type)
  {
  case SAConnect:
    {
      ClientIP = RString(beg,end-beg);
      #if _ENABLE_DEDICATED_SERVER
        #if !_DISABLE_GUI
          DoCreateDedicatedServer=false;
        #endif
      #endif
      Reset();
      return true;
    }
    // break; //not needed, return
  case SAPassword:
    {
      NetworkPassword = RString(beg,end-beg);
      Reset();
      return true;
    }
    // break; //not needed, return
  }
  return false;
}
SingleArgumentFinish SAFinish;

static void SingleArgument( const char *beg, const char *end )
{
  if (SAFinish.Process(beg, end)) return;
  if( end==beg ) return;
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    // option
    static const char xStr[]="x=";
    static const char yStr[]="y=";
    static const char wStr[]="window";
    static const char noPauseStr[]="nopause";
    static const char autoUpdateStr[]="autoUpdate";
    static const char noSplashStr[]="nosplash";
    static const char generateShadersStr[]="generateshaders";
    static const char disableShaderGenerationStr[]="disableShaderGeneration";
    static const char forceDX9Str[]="forceDX9";
    static const char enableSM3Str[]="sm3";
    static const char forceSM2Str[]="sm2";
    static const char noLimitTreeLODsStr[]="noLimitTreeLODs";
#if _VBS2 && _ENABLE_CHEATS
    static const char exportCppStr[]="exportCpp";
#endif
#if _VBS3 
    //!VBS2 applications continues to render, even when focus is lost
    static const char forceRenderStr[]="forceRender";
    //!VBS2 applications continues to render and simulate, even when focus is lost (useful for plugins with windows)
    static const char forceSimulStr[]="forceSimul";
    static const char disableVONStr[]="disableVON";
#endif
#if _ENABLE_BULDOZER
    static const char worldCfg[]="worldCfg=";// Init Landscape by following world config... 
#endif
#ifndef _ENABLE_HOST
  #error _ENABLE_HOST should be defined 0 or 1
#endif
#if _ENABLE_HOST
    static const char host[]="host";
#endif
    static const char connect[]="connect=";
    static const char connect2[]="connect";
    static const char gamertag[]="gamertag=";
    static const char instance[]="instance=";
    static const char bandwidth[]="bandwidth=";
    static const char privateSlots[]="privateSlots=";
    static const char session[]="session=";
    static const char download[]="download";
    static const char noesrb[]="noesrb";
    static const char nopatchcheck[]="nopatchcheck";
//    static const char flashpointConfig[]="cfg=";
//    static const char modPath[]="mod=";
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    static const char serverConfig[]="config=";
    static const char bandwidthAlg[]="bandwidthAlg=";
    static const char pidFile[]="pid=";
    static const char version[]="-version";
#ifndef _WIN32
    static const char maxmem[]="maxmem=";
#endif
#endif
//}
    static const char client[]="client";
    static const char port[]="port=";
    static const char ip[]="ip=";
    static const char password[]="password=";
    static const char password2[]="password";
    static const char bePath[]="bepath=";
    static const char openAL[]="openal";
    static const char dSound[]="dsound";
    static const char winxp[]="winxp";
    static const char xa2Sound[] = "xa2sound";
    static const char init[]="init=";
    static const char showscripterrors[]="showscripterrors";
#if _ENABLE_DEDICATED_SERVER
    static const char netlog[]="netlog";
#endif
    static const char noland[]="noland";
    static const char world[]="world=";
    static const char cpuCount[]="cpucount=";
    static const char nosound[]="nosound";
#if _ENABLE_CHEATS
    static const char logfiles[]="logfiles";
    static const char debugVS[]="debugVS";
    static const char tl[]="tl";
    static const char notex[]="notex";
    static const char vbs[]="vbs";
    static const char addons[]="addons=";
    static const char xboxUI[]="xboxui";
    static const char useCB[]="useCB";
    static const char noCB[]="noCB";
    static const char uid[]="uid=";
    static const char forceUID[]="forceUID=";
#endif
#if _ENABLE_CHEATS
    static const char timeForScripts[] = "timeforscripts=";
#endif
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
    static const char autotest[]="autotest=";
#endif
#if _ENABLE_STEAM
    static const char steam[]="steam";
#endif

#if _ENABLE_REPORT
    static const char checkProgressRefresh[]="checkProgressRefresh";
#endif
#if _VBS2
    static const char vbsAdmin[]="admin";
    static const char vbsEnableCNR[]="EnableCNR";
    static const char vbsEnableCNR2[]="cnr";
    static const char vbsEnableLVC[]="lvc";
#endif

#if _ENABLE_TEXTURE_HEADERS
    static const char noTexHeaders[] = "noTexHeaders";
#endif
#if _ENABLE_PBO_PROTECTION
    static const char protTestParentSerial[] = "protTestParentSerial=";
    static const char protTestSerial[] = "protTestSerial=";
#endif

    if (IsOptionWArgs(beg,xStr)) Glob.config.wantW=atoi(beg+strlen(xStr));
    else if (IsOptionWArgs(beg,yStr)) Glob.config.wantH=atoi(beg+strlen(yStr));
    else if (IsOption(beg,end,wStr) ) UseWindowByCommandLine = UseWindow = true;
    else if (IsOption(beg,end,noPauseStr)) PauseInBackround=false;
    else if (IsOption(beg,end,autoUpdateStr)) AutoUpdate=true;
#if _VBS3
    //same behavior for forcerender as noPause
    else if (IsOption(beg,end,forceRenderStr)) PauseInBackround=false;
    else if (IsOption(beg,end,vbsAdmin)) Glob.vbsAdminMode=Glob.vbsAdminUser=true;
    else if (IsOption(beg,end,vbsEnableCNR)) Glob.vbsEnableCNR=true;
    else if (IsOption(beg,end,vbsEnableCNR2)) Glob.vbsEnableCNR=true;
    else if (IsOption(beg,end,forceSimulStr)) { PauseInBackround=false; ForceSimulation = true;}
    else if (IsOption(beg,end,disableVONStr)) DisableVON=true;
    else if (IsOption(beg,end,vbsEnableLVC)) LVCWanted = true;
#endif
#if _ENABLE_BULDOZER
    else if (IsOptionWArgs(beg,worldCfg)) 
    {
      int length = end - beg - strlen(worldCfg);
      if (length < sizeof(Glob.header.worldname))
      {      
        strncpy(Glob.header.worldname, beg + strlen(worldCfg), length);
        Glob.header.worldname[length] = '\0';
      }
    }
#endif
    else if(IsOptionWArgs(beg,init))
    {
      const char *src = beg + strlen(init); 
      InitCommand = RString(src, end-src);
    }
  #if _ENABLE_DEDICATED_SERVER
    else if(IsOption(beg,end,netlog)) netLogValid=true;
  #endif
    else if (IsOption(beg,end,noland)) NoLandscape=true;
    else if (IsOption(beg,end,nosound)) NoSound=true;
    else if (IsOptionWArgs(beg,world))
    {
      const char *src = beg + strlen(world); 
      InitWorldName = RString(src, end-src);
    }
#if _ENABLE_CHEATS
    else if (IsOption(beg,end,logfiles)) GLogFileOps=true;
    else if (IsOption(beg,end,xboxUI)) XboxUI=true;
    else if (IsOptionWArgs(beg,uid)) 
    {    
      customUID = atoi(beg+strlen(uid));
      saturate(customUID, 1,999);
    }    
    else if (IsOptionWArgs(beg, forceUID))
    {
      const char *src = beg + strlen(forceUID); 
      forcePublicKey = RString(src, end-src);
    }
    else if (IsOption(beg,end,debugVS)) DebugVS=true;
    else if (IsOption(beg,end,useCB)) {Glob.config.ForceCB();}
    else if (IsOption(beg,end,noCB)) {Glob.config.DisableCB();}
    else if (IsOption(beg,end,vbs) ) VBS=true;
#endif
    else if (IsOptionWArgs(beg,cpuCount)) {ForceCPUCount(atoi(beg + strlen(cpuCount)));Glob.config.CPUCountChanged();}
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
    else if (IsOptionWArgs(beg,autotest))
    {
      const char *src = beg + strlen(autotest); 
      AutoTest = RString(src, end-src);
      PauseInBackround = false;
      Silent = true;
      GDebugExceptionTrap.SetSilentMode();
    }
#endif
#if _ENABLE_CHEATS
    else if (IsOptionWArgs(beg, timeForScripts))
    {
      TimeForScripts = 0.001f * atof(beg + strlen(timeForScripts));
    }
#endif
#if _ENABLE_CHEATS
    else if (IsOptionWArgs(beg,addons))
    {
      const char *src = beg + strlen(addons); 
      AddonsList = RString(src, end-src);
    }
#endif
#if _ENABLE_REPORT
    else if( !strnicmp(beg,checkProgressRefresh,strlen(checkProgressRefresh)) ) CheckProgressRefresh=true;
    #if _VBS2 & _ENABLE_CHEATS
        else if( !strnicmp(beg,exportCppStr,strlen(exportCppStr)) ) exportCpp=true;
    #endif
#endif
    else if (IsOption(beg,end,noSplashStr)) NoSplash=true;
    else if (IsOption(beg,end,generateShadersStr))
    {
      GenerateShaders=true;
      Silent = true;
#ifdef _WIN32
      GDebugExceptionTrap.SetSilentMode();
#endif
    }
#ifdef _WIN32
    else if (IsOption(beg,end,disableShaderGenerationStr)) DisableShaderGeneration=true;
    else if (IsOption(beg,end,forceDX9Str)) ForceDX9=true;
    else if (IsOption(beg,end,enableSM3Str)) EnableSM3=true;
    else if (IsOption(beg,end,forceSM2Str)) EnableSM3=false;
#endif
#if _ENABLE_HOST
    else if (IsOption(beg,end,host))
    {
      if (!IsDedicatedClient())
      {
        DoCreateServer=true;
        #if _ENABLE_REPORT
        ResetLogFile = true;
        LogFilename = "host.log";
        #endif
      }
    }
#endif
    else if (IsOptionWArgs(beg,instance))
    {
      const char *src = beg + strlen(instance); 
      InstanceName = RString(src,end-src);
    }
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
    else if( IsOptionWArgs(beg,serverConfig))
    {
      const char *src = beg + strlen(serverConfig); 
      ServerConfig = RString(src,end-src);
    }
#endif
//}
    else if (IsOption(beg,end,client))
    {
      #if !_SERVER && _ENABLE_DEDICATED_SERVER
        if (!DoCreateServer)
        {
          DoCreateDedicatedClient=true;
        }
      #endif
    }
#if defined(_WIN32) 
    else if( IsOptionWArgs(beg,connect))
    {
      const char *src = beg + strlen(connect); 
      ClientIP = RString(src,end-src);
      #if _ENABLE_DEDICATED_SERVER
        #if !_DISABLE_GUI
          DoCreateDedicatedServer=false;
        #endif
      #endif
    }
    else if( IsOptionWArgs(beg,connect2) )
    {
      SAFinish.SetType(SingleArgumentFinish::SAConnect);
    }
#endif
    else if( IsOptionWArgs(beg,port) )
    {
      NetworkPort = atoi(beg + strlen(port));
      ServerPort = NetworkPort;
    }
    else if ( IsOptionWArgs(beg,ip) )
    {
      bindIPAddress = inet_addr(beg + strlen(ip));
    }
#if _ENABLE_DEDICATED_SERVER
    else if( !strnicmp(beg,pidFile,strlen(pidFile)) )
    {
      const char *src = beg + strlen(pidFile); 
      PidFileName = RString(src,end-src);
    }
    else if( !strnicmp(beg,bandwidthAlg,strlen(bandwidthAlg)) )
    {
      int algNo = atoi(beg + strlen(bandwidthAlg));
      NetChannel::UseAlternativeBandwidthControlAlgorithm = (algNo==2);
    }
#ifndef _WIN32
    else if( !strnicmp(beg,version,strlen(version)) )
    {
      printf("ArmA Linux Server, ver. %s.%d\n", APP_VERSION_TEXT, BUILD_NO);
      void DDTerm();
      DDTerm();
      exit(0);
    }
    else if ( !strnicmp(beg,maxmem,strlen(maxmem)) )
    {
      extern void MemoryInit(int maxmem);
      int maxm = atoi(beg + strlen(maxmem));
      MemoryInit(maxm);
    }
#endif
#endif
    else if (IsOptionWArgs(beg,password))
    {
      const char *src = beg + strlen(password); 
      NetworkPassword = RString(src,end-src);
    }
    else if (IsOptionWArgs(beg,password2))
    {
      SAFinish.SetType(SingleArgumentFinish::SAPassword);
    }
    else if (IsOptionWArgs(beg,gamertag))
    {
      const char *src = beg + strlen(gamertag); 
      LiveGamertag = RString(src,end-src);
    }
    else if( !strnicmp(beg,bandwidth,strlen(bandwidth)) )
    {
      DSBandwidth = atoi(beg + strlen(bandwidth));
    }
    else if( !strnicmp(beg,privateSlots,strlen(privateSlots)) )
    {
      DSPrivateSlots = atoi(beg + strlen(privateSlots));
    }
    else if( !strnicmp(beg,session,strlen(session)) )
    {
      const char *src = beg + strlen(session); 
      LiveSession = RString(src,end-src);
    }
    else if (IsOption(beg, end, download)) ContentDownload = true;
    else if (IsOption(beg, end, noesrb)) ESRBScreen = false;
    else if (IsOption(beg, end, nopatchcheck)) PatchCheck = false;
    else if (IsOption(beg, end, winxp)) WinXP = true;
    else if (IsOption(beg, end, showscripterrors)) ShowScriptErrors = true;
#if _ENABLE_STEAM
    else if (IsOption(beg, end, steam)) UseSteam = true;
#endif
    // path for BattlEye files cache
    else if (!strnicmp(beg, bePath, strlen(bePath)))
    {
      const char *src = beg + strlen(bePath); 
      BEPath = RString(src,end-src);
    }

#if _ENABLE_TEXTURE_HEADERS
    else if(IsOption(beg, end, noTexHeaders) )
    {
      GUseTextureHeaders = false;
  }
#endif
#if _ENABLE_PBO_PROTECTION
    else if(IsOptionWArgs(beg,protTestParentSerial))
    {
      const char *src = beg + strlen(protTestParentSerial); 
      PBOHeaderProtection::TestParentSerial = RString(src, end-src);
    }
    else if(IsOptionWArgs(beg,protTestSerial))
    {
      const char *src = beg + strlen(protTestSerial); 
      PBOHeaderProtection::TestSerial = RString(src, end-src);
    }
#endif

  }
  return;
}

#if defined(_WIN32)  

struct CommandingPipeContextWin
{
  HANDLE handle;

  ~CommandingPipeContextWin();
  void Close();
  void Open(const char *name);
  RString PeekCommand();
  void Respond(const char *text);
};


void CommandingPipeContextWin::Open(const char *name)
{
  Close();
  BString<1024> pipeName;
  sprintf(pipeName,"\\\\.\\pipe\\%s",name);

  handle = CreateNamedPipe(
    pipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
    PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_NOWAIT,
    1, 4*1024, 4*1024, 100, NULL
  );
}

void CommandingPipeContextWin::Close()
{
  if (handle!=NULL)
  {
    CloseHandle(handle);
    handle = NULL;
  }
}

CommandingPipeContextWin::~CommandingPipeContextWin()
{
  Close();
}

RString CommandingPipeContextWin::PeekCommand()
{
  RString ret;
  for (;;)
  {
    char buffer[4*1024];
    DWORD read = 0;
  
    BOOL ok = ReadFile(handle,buffer,sizeof(buffer),&read,NULL); 
    if (ok)
    {
      // append data and stop reading
      ret = ret + RString(buffer,read);
      break;
    }
    else
    {
      HRESULT hr = GetLastError();
      if (hr!=ERROR_MORE_DATA) return RString();
      // append data and continue reading
      ret = ret + RString(buffer,read);
    }
  }
  return ret;
}

void CommandingPipeContextWin::Respond(const char *text)
{
  DWORD written;
  WriteFile(handle,text,strlen(text),&written,NULL);
}


typedef CommandingPipeContextWin CommandingPipeContext;

#else

struct CommandingPipeContextDummy
{
  void Open(const char *name){}
  RString PeekCommand() {return RString();}
  void Respond(const char *text){}
};

typedef CommandingPipeContextDummy CommandingPipeContext;

#endif

static CommandingPipeContext CommandingPipe;

int VRAMLimit = 0;


void RunOtherInstance(RString runOptions);

static void SingleArgumentPar(const char *beg, const char *end)
{
  if( end==beg ) return;
  static const char par[]="par=";
  static const char run[]="runOther=";
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    if (IsOptionWArgs(beg,par))
    {
      const char *src = beg + strlen(par); 
      FlashpointPar = RString(src,end-src);
    }
    if (IsOptionWArgs(beg,run))
    {
      const char *src = beg + strlen(run); 
      RString runOptions = RString(src,end-src);
      RunOtherInstance(runOptions);
    }
  }
}

static void SingleArgumentRun(const char *beg, const char *end)
{
  if( end==beg ) return;
  static const char minimizeStr[]="minimize";
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    if (IsOption(beg,end,minimizeStr) ) StartMinimized = true;
  }
}

static AutoArray<RString> BetaMod;

extern bool DisableLogs; // disable logs (for autotest, enabled only logs from script)

static void SingleArgumentPass1A(const char *beg, const char *end)
{
  if( end==beg ) return;
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    // option
    static const char flashpointConfig[]="cfg=";
    static const char userinfoConfig[]="profiles=";
    static const char modPath[]="mod=";
    static const char betaPath[]="beta=";
    static const char doNothing[]="donothing";
    static const char logPreload[]="logpreload";
    static const char logDVD[]="logdvd";
    static const char maxVRAM[]="maxVRAM=";

#if _VBS2 // autoassign
    static const char autoAssign[]="autoassign=";
    static const char autoAssignGrp[]="autoassigngrp=";
    static const char autoAssignSide[]="autoassignside=";
    Glob.autoAssignSide = TEmpty;
    static const char dta[]="dta="; //used to redirect dta folder for buldozer
#endif
#if _ENABLE_REPORT
    static const char noFpTraps[]="nofptraps";
    static const char fpTraps[]="fptraps";
#endif
    static const char nologs[]="nologs";
    static const char checkAllSignatures[]="checkSignatures";
    static const char checkAllSignaturesFull[]="checkSignaturesFull";

#if _ENABLE_CHEATS
    static const char unprotectPbos[]="unprotectPbos";
#endif

    if( !strnicmp(beg,userinfoConfig,strlen(userinfoConfig)) )
    {
      const char *src = beg + strlen(userinfoConfig); 
      RString path(src, end-src);
      #if _ENABLE_REPORT
      ResetLogFile = true;
      #endif
      SetExplicitRootDir(path);
#ifdef _WIN32
      SetLocalSettingPaths();
#endif
    }
    else if( !strnicmp(beg,betaPath,strlen(betaPath)) )
    {
      const char *src = beg + strlen(betaPath);
      RString add(src, end-src);
      BetaMod.Add(add);
    }
    else if( !strnicmp(beg,modPath,strlen(modPath)) )
    {
      const char *src = beg + strlen(modPath);
      RString add(src, end-src);
      AddMod(add);
      GModInfos.modsReadOnly = true; // even when the mod does not exist
    }
    else if ( !strnicmp(beg,doNothing,strlen(doNothing)) )
    {
      exit(0);
    }
    else if( IsOptionWArgs(beg,maxVRAM) )
    {
      const char *src = beg + strlen(maxVRAM); 
      VRAMLimit = atoi(src);
      // avoid values which would be absolute nonsense
      saturate(VRAMLimit,128,2047);
      VRAMLimit *= 1024*1024;
    }
    
#if _VBS3 //ability to assign a new dta folder for buldozer
    else if( !strnicmp(beg,dta,strlen(dta)) )
    {
      const char *src = beg + strlen(modPath);
      RString str(src, end-src);
      dtaFolder = str;
    }
#endif

#if _ENABLE_REPORT
    else if( !strnicmp(beg,noFpTraps,strlen(noFpTraps)) ) NoFPTraps=true;
    else if( !strnicmp(beg,fpTraps,strlen(fpTraps)) ) NoFPTraps=false;
#endif
    else if( !strnicmp(beg,nologs,strlen(nologs)) ) DisableLogs=true;
#if _ENABLE_CHEATS
    else if( !strnicmp(beg,unprotectPbos,strlen(unprotectPbos)) ) UnprotectPbos=true;
#endif
    else if( !strnicmp(beg,checkAllSignaturesFull,strlen(checkAllSignaturesFull)) ) CheckAllSignatures=CheckAllSignaturesFull=true;
    else if( !strnicmp(beg,checkAllSignatures,strlen(checkAllSignatures)) ) CheckAllSignatures=true;
#if _VBS2 // autoassign
    else if( !strnicmp(beg,autoAssign,strlen(autoAssign)) )
    {
      const char *src = beg + strlen(autoAssign);
      Glob.autoAssign = RString(src,end-src);
    }
    else if( !strnicmp(beg,autoAssignGrp,strlen(autoAssignGrp)) )
    {
      const char *src = beg + strlen(autoAssignGrp);
      Glob.autoAssignGrp = RString(src,end-src);
    }
    else if( !strnicmp(beg,autoAssignSide,strlen(autoAssignSide)) )
    {
      const char *src = beg + strlen(autoAssignSide);

      // side in text form
      RString tmp(src,end-src);

      if (!strnicmp("west",tmp.Data(),strlen(tmp.Data())))
        Glob.autoAssignSide = TWest;
      else if (!strnicmp("east",tmp.Data(),strlen(tmp.Data())))
        Glob.autoAssignSide = TEast;
      else if (!strnicmp("civilian",tmp.Data(),strlen(tmp.Data())))
        Glob.autoAssignSide = TCivilian;
      else if (!strnicmp("resistance",tmp.Data(),strlen(tmp.Data())))
        Glob.autoAssignSide = TGuerrila;
    }
#endif
#ifndef _SUPER_RELEASE
    else if( !strnicmp(beg,logPreload,strlen(logPreload)) ) GLogFileServer=true;
    else if( !strnicmp(beg,logDVD,strlen(logDVD)) ) GLogDVD=true;
#endif
  }
  return;
}

static void SingleArgumentArgs(const char *beg, const char *end)
{
  if( end==beg ) return;
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    static const char exThreads[]="exthreads=";
    if (IsOptionWArgs(beg,exThreads)) {Fail("-exThreads can be specified via command line onle, not in the .par file");}
  }
}


static void SingleArgumentPass1B(const char *beg, const char *end)
{
  if( end==beg ) return;
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    // option
    static const char flashpointConfig[]="cfg=";
    static const char noLauncher[]="noLauncher";
    static const char name[]="name=";
    static const char skipintro[]="skipintro";
    static const char interpolation[]="interpolation=";
    static const char commandPipe[]="command=";
    static const char jDebugPort[]="jDebugPort=";
    static const char jSuspend[]="jSuspend";
    static const char jPerfTest[]="jPerfTest";
    static const char jExportInterface[]="jExportInterface";
#if _ENABLE_BULDOZER
    static const char buldozer[]="buldozer";
    static const char buldozerWithAddons[]="buldozer+addons";
  #endif
#if _ENABLE_PATCHING
    static const char noFilePatching[]="noFilePatching";
#endif

    if (IsOptionWArgs(beg,flashpointConfig))
    {
      const char *src = beg + strlen(flashpointConfig); 
      FlashpointCfg = RString(src,end-src);
    }
    else if (IsOption(beg, end, noLauncher))
    {
      AvoidLauncher = true;
    }
    else if (IsOptionWArgs(beg,name))
    {
      const char *src = beg + strlen(name); 
      Glob.header.SetPlayerName(RString(src,end-src));
    }
    else if( IsOption(beg,end,skipintro) )
    {
      SkipIntro=true;
    }
    else if (IsOptionWArgs(beg,interpolation))
    {
      switch (beg[strlen(interpolation)])
      {
        case '0': JumpySim = 7; break;
        case '1': JumpySim = 2; break;
        case '2': JumpySim = 1; break;
        default: break;  // ignore
      }
    }
    else if (IsOptionWArgs(beg, jDebugPort)) JavaDebugPort = atoi(beg + strlen(jDebugPort));
    else if (IsOption(beg, end, jSuspend)) JavaSuspend = true;
    else if (IsOption(beg, end, jPerfTest)) JavaPerfTest = true;
    else if (IsOption(beg, end, jExportInterface)) JavaExportInterface = true;
  #if _ENABLE_BULDOZER
    else if( IsOption(beg,end,buldozer) )
    {
      LandEditor=true;
      LoadAddons = false;
      JumpySim = 7;  // turn off interpolation and visual states for Buldozer
    }
    else if( IsOption(beg,end,buldozerWithAddons) )
    {
      LandEditor=true;
    }
  #endif
    else if (IsOptionWArgs(beg,commandPipe))
    {
      const char *src = beg + strlen(commandPipe); 
      CommandingPipe.Open(RString(src,end-src));
    }
#if _ENABLE_PATCHING
    else if (IsOption(beg,end,noFilePatching)) GEnablePatching=false;
#endif
  }
  else
  {
    strncpy(LoadFile,beg,end-beg);
    LoadFile[end-beg]=0;
  }
  return;
}

//void StackOverflow(int level)
//{
//  volatile int a[2732];
//  a[0] = level;
//  if (a[0]>=0)
//  {
//    StackOverflow(level+1);
//  }
//  a[1] = level+1;
//}


static void SleepWithUpdate(int ms)
{
  PROFILE_SCOPE_EX(sleep,*)
  while (ms>0)
  {
    int toSleep= intMin(ms,10);
    Sleep(toSleep);
    GetNetworkManager().FastUpdate();
    ms -= toSleep;
  }
}

static int LimitFpsCoef = 0;
static const int FpsDivs[]={
  0,40,20,10,5,
#if _ENABLE_CHEATS
  2
#endif
};
int LimitFps = FpsDivs[LimitFpsCoef];

extern float Height;

/*!
\patch_internal 1.27 Date 10/12/2001 by Ondra
- Fixed: FPS limitation for all non-graphics modes, not only for ded. server.
\patch 1.27 Date 10/12/2001 by Ondra
- Improved: MP: application behaviour when running on background (alt-tab)
is now more consistent.
\patch_internal 1.31 Date 11/23/2001 by Ondra
- Fixed: FPS limitation for no-graphics cheat
to make MP multiple client testing easier.
\patch_internal 1.33 Date 11/29/2001 by Ondra
- New: Cheat Alt-S to limit fps at 10.
This should help testing some bugs that do not occur with high fps.
*/
void RenderFrame( float deltaT, bool enableSceneDraw, bool enableHUDDraw)
{
  //StackOverflow(0);
  
  if( Glob.exit ) CloseRequest=true;
  #if _ENABLE_CHEATS
    if (GInput.GetCheatXToDo(CXTShutdown))
    {
      CloseRequest=true;
    }
  #endif
  
  if (!GWorld) return;

#ifdef _WIN32 
  ForceRender=false;
#endif

  GWorld->ReadAllVideoBuffers();
  
  int startTime = GlobalTickCount();
  // alive should come soon (if nothing extra happens)
#if _ENABLE_REPORT
  if (CheckProgressRefresh)
    GDebugger.NextAliveExpected(500);
  else
#endif
    GDebugger.NextAliveExpected(10000);

  #if _ENABLE_BULDOZER
    if (LandEditor && ObjViewer)
    {
      // use "action menu" to control animations
      if (GInput.GetActionToDo(UAMenuBack))
      {
        GAnimationManager.Refresh();
        GAnimationManager.PrevAnim();
      }
      if (GInput.GetActionToDo(UAAction))
      {
        GAnimationManager.Refresh();
        GAnimationManager.NextAnim();
      }
      float slow = GInput.GetAction(UABuldTurbo);
      float speed = slow*0.1f+(1-slow)*0.5f;
      if (GInput.GetAction(UAPrevAction))
      {
        GAnimationManager.Refresh();
        GAnimationManager.AdvanceAnim(-deltaT*speed);
      }
      if (GInput.GetAction(UANextAction))
      {
        GAnimationManager.Refresh();
        GAnimationManager.AdvanceAnim(+deltaT*speed);
      }
      if (fabs(GInput.cursorMovedZ)>0.01f)
      {
        GAnimationManager.Refresh();
        if (GInput.GetAction(UADefaultAction))
        {
          if (GInput.cursorMovedZ>0)
          {
            GAnimationManager.NextAnim();
          }
          else
          {
            GAnimationManager.PrevAnim();
          }
        }
        else
        {
          GAnimationManager.AdvanceAnim(GInput.cursorMovedZ*(1.0f/6)*speed);
        }
      }
    }
  #endif

#if _ENABLE_CHEATS
  // change time limit for scripts
  if (GInput.GetCheat3ToDo(DIK_COMMA))
  {
    TimeForScripts *= 0.5f;
    saturateMax(TimeForScripts, 0.0001f); // 0.1 ms
    GlobalShowMessage(500, "Time for scripts %.1f ms", 1000.0f * TimeForScripts);
  }
  if (GInput.GetCheat3ToDo(DIK_PERIOD))
  {
    TimeForScripts *= 2.0f;
    saturateMin(TimeForScripts, 0.05f); // 50 ms
    GlobalShowMessage(500, "Time for scripts %.1f ms", 1000.0f * TimeForScripts);
  }
#endif

  GWorld->SetSimulationFocus(enableSceneDraw || enableHUDDraw);
  GWorld->Simulate(deltaT, enableSceneDraw, enableHUDDraw);

//  if (enableSceneDraw)
//  {
//    Log("%d: RenderFrame",GlobalTickCount());
//  }
  //Assert( MemoryCheck() );

  #if 1
  // if not drawing, avoid having fps too high
  if (
    GInput.CheatActivated() == CheatFps && (GInput.CheatServed(),true)
    #if _ENABLE_CHEATS
      || GInput.GetCheat2ToDo(DIK_S)
    #endif
  )
  {
    LimitFpsCoef++;
    if (LimitFpsCoef>=lenof(FpsDivs)) LimitFpsCoef=0;
    LimitFps = FpsDivs[LimitFpsCoef];
    GlobalShowMessage(500,"Limit FPS %d",LimitFps);
  }
  if (!enableSceneDraw || GWorld->GetRenderingDisabled() || LimitFps || UseWindow && LandEditor)
  {
      int maxFps = LimitFpsCoef ? LimitFps : 50;
    int minMsPerFrame = 1000 / maxFps;
    if (UseWindow && LandEditor)
    {
      // enable max. 200 fps in Buldozer windowed mode
      // note: due to Sleep implementation max. fps will be 50
      saturateMax(minMsPerFrame,1000/200);
    }

    int durationMs = GlobalTickCount() - startTime;
    int sleepMillis = minMsPerFrame - durationMs;
    if (sleepMillis>0) SleepWithUpdate(sleepMillis);
  }
  #endif
}

#ifdef _WIN32

static void ProcessMessages( bool noWait );

void ProcessMessagesNoWait() {ProcessMessages(true);}

#else

void ProcessMessagesNoWait()
{}

#endif

// Main Loop

void OpenPerfCounters();
void ClosePerfCounters();

bool GetMouseEx() 
{
	return MouseEx;
}

bool GetEnableDraw()
{
#if defined(_WIN32) 
  bool focused = GAppActive && !GAppIconic;
  // when the simulation is paused, there is no need to render
  bool forceBackgroundRender = !PauseInBackround && GWorld && GWorld->IsSimulationEnabled();
  bool enableDraw = ( focused || (ForceRender || forceBackgroundRender) && ( LandEditor || UseWindow) );
#else
  bool enableDraw = true;
#endif
  return enableDraw;
}

bool CanProcessMouse() 
{
	return ((!UseWindow || GAppActive ) && GetEnableDraw());
}

float GetBuldozerMouseSpeed() 
{
	return BuldozerMouseSpeed;
}

float GetVisitorMouseSpeed() 
{
	return VisitorMouseSpeed;
}

#pragma warning(disable:4351)


/*!
\patch_internal 1.14 Date 8/10/2001 by Ondra
- Added: New cheat to crash the game.
*/

const EnumName CheatCodeNames[]=
{
  EnumName(CheatNone,"NONE"),
  EnumName(CheatExportMap,"TOPOGRAPHY"),
  EnumName(CheatWinMission,"ENDMISSION"),
  EnumName(CheatSaveGame,"SAVEGAME"),
  EnumName(CheatCrash,"CRASH"),
  EnumName(CheatFreeze,"FREEZE"),
  EnumName(CheatError,"ERROR"),
  EnumName(CheatFlush,"FLUSH"),
  EnumName(CheatFps,"FPS"),
  EnumName(CheatUnlockMissions,"MISSIONS"),
  EnumName(CheatUnlockArmory,"GETALLGEAR"),
  EnumName(CheatHost,"HOST"),
/*
//((TO_BE_REMOVED_IN_FINAL
  EnumName(CheatShowVoNBubbles,"VOICE"),
//))TO_BE_REMOVED_IN_FINAL
*/
#if _ENABLE_LIFE_CHEAT
  EnumName(CheatImmortality,"LIFE"),
#endif
  EnumName()
};

const EnumName& GetCheatName(int index) 
{
	return CheatCodeNames[index];
}

template<>
const EnumName *GetEnumNames(CheatCode dummy) 
{
	return CheatCodeNames;
}


RString GetKeyName(int dikCode);

#ifdef _WIN32
void InitWindow( HINSTANCE hInst, HWND hwnd, bool withCommandLine, const char *appendix );


struct MaintainTitle
{
  bool _host,_admin;
  RString _player;

  MaintainTitle():_host(false),_admin(false){}

  void Update(bool host, bool admin, RString player)
  {
    if (host==_host && admin==_admin && player==_player) return;
    _host = host;
    _admin = admin;
    _player = player;
    bool commandLine = UseWindow && !IsDedicatedClient();
    InitWindow((HINSTANCE)hInstApp,(HWND)hwndApp,commandLine,GetAppendix());
  }
  RString GetAppendix() const
  {
    if (!IsDedicatedClient() && !UseWindow) return RString();
    RString appendix;
    RString role;
    if (IsDedicatedClient()) appendix = appendix + " Console Client";
    if (!_player.IsEmpty()) appendix = appendix + " " + _player;
    if (_host) appendix = appendix + " (" + LocalizeString(IDS_MP_SERVER) + ")";
    else if (_admin) appendix = appendix + " (" + LocalizeString(IDS_MP_MASTER) + ")";
    return appendix;
  }
} SMaintainTitle;

void SetUseWindow(bool value)
{
  if (UseWindow==value) return;
  UseWindow = value;
  InitWindow((HINSTANCE)hInstApp,(HWND)hwndApp,UseWindow,SMaintainTitle.GetAppendix());
}

#endif

//Integrity check - force function aligment on 16B boundary
#pragma optimize("t",on)
#if !_RELEASE
  // it seems in VS 2005 optimize("t",on) applies /Oy as well (Omit frame pointers)
  // in Testing we need stack frames for memory call-stack and crash-dumps
  #pragma optimize("y",off)
#endif


#define X_CHEAT_ENUMLEVEL(name,text,level) level,
const int CheatXLevels[]= { X_CHEAT_LIST(X_CHEAT_ENUMLEVEL) 0};

#if _ENABLE_CHEATS
struct RemoteCheatItem
{
  /// which cheat key (ASCII)
  int key;
  /// cheat 1,2,3
  int cheatNum;
};

TypeIsSimple(RemoteCheatItem)

static AutoArray<RemoteCheatItem> RemoteCheatQueue;

#include <El/evaluator/expressImpl.hpp>

static void DebugCheat(RString cheatString, int num)
{
  for (int i=0; i<cheatString.GetLength(); i++)
  {
    RemoteCheatItem &item = RemoteCheatQueue.Append();
    item.key = cheatString[i];
    item.cheatNum = num;
  }
}

static GameValue DebugCheat1(const GameState *state, GameValuePar oper1)
{
  DebugCheat(oper1,1);
  return GameValue();
}
static GameValue DebugCheat2(const GameState *state, GameValuePar oper1)
{
  DebugCheat(oper1,2);
  return GameValue();
}
static GameValue DebugCheat3(const GameState *state, GameValuePar oper1)
{
  DebugCheat(oper1,3);
  return GameValue();
}

#define X_CHEAT_ENUMNAME(name,text,level) EnumName(CXT##name,#name),

static const EnumName CheatXNames[]=
{
  X_CHEAT_LIST(X_CHEAT_ENUMNAME)
  EnumName()
 
};


 
template <>
const EnumName *GetEnumNames(CheatXType dummy) {return CheatXNames;}

static void DebugCheatX(RString cheatName, int direction)
{
  int cheat = GetEnumValue<CheatXType>(cc_cast(cheatName));
  if (cheat<0 || cheat>=CXTN) return;
  GInput.cheatXToDo[cheat] = direction;
}
static GameValue DebugCheatXFwd(const GameState *state, GameValuePar oper1)
{
  DebugCheatX(oper1,+1);
  return GameValue();
}
static GameValue DebugCheatXBck(const GameState *state, GameValuePar oper1)
{
  DebugCheatX(oper1,-1);
  return GameValue();
}
#endif

#ifdef _WIN32
static int AsciiToDik(int asc)
{
  switch (toupper(asc))
  {
    case '1': return DIK_1;
    case '2': return DIK_2;
    case '3': return DIK_3;
    case '4': return DIK_4;
    case '5': return DIK_5;
    case '6': return DIK_6;
    case '7': return DIK_7;
    case '8': return DIK_8;
    case '9': return DIK_9;
    case '0': return DIK_0;
    case '-': return DIK_MINUS;
    case '=': return DIK_EQUALS;
    //case '': return DIK_BACK;
    //case '': return DIK_TAB;
    case 'Q': return DIK_Q;
    case 'W': return DIK_W;
    case 'E': return DIK_E;
    case 'R': return DIK_R;
    case 'T': return DIK_T;
    case 'Y': return DIK_Y;
    case 'U': return DIK_U;
    case 'I': return DIK_I;
    case 'O': return DIK_O;
    case 'P': return DIK_P;
    case '[': return DIK_LBRACKET;
    case ']': return DIK_RBRACKET;
    //case '': return DIK_RETURN;
    //case '': return DIK_LCONTROL;
    case 'A': return DIK_A;
    case 'S': return DIK_S;
    case 'D': return DIK_D;
    case 'F': return DIK_F;
    case 'G': return DIK_G;
    case 'H': return DIK_H;
    case 'J': return DIK_J;
    case 'K': return DIK_K;
    case 'L': return DIK_L;
    case ';': return DIK_SEMICOLON;
    case '\'': return DIK_APOSTROPHE;
    case '`': return DIK_GRAVE;
    case '\\': return DIK_BACKSLASH;
    case 'Z': return DIK_Z;
    case 'X': return DIK_X;
    case 'C': return DIK_C;
    case 'V': return DIK_V;
    case 'B': return DIK_B;
    case 'N': return DIK_N;
    case 'M': return DIK_M;
    case ',': return DIK_COMMA;
    case '.': return DIK_PERIOD;
    case '/': return DIK_SLASH;
    //case '': return DIK_MULTIPLY;
    case ' ': return DIK_SPACE;
    //case '': return DIK_AT;
    //case '': return DIK_COLON;
  }
  LogF("Unknown dik code for '%c' (%d)",asc,asc);
  return 0;
}
#endif

void RemoteCheats()
{
  #if _ENABLE_CHEATS
    for (int i=0; i<RemoteCheatQueue.Size(); i++)
    {
      const RemoteCheatItem &item = RemoteCheatQueue[i];
      // convert ascii to DIK
      int dik = AsciiToDik(item.key);
      if (!dik) continue;
      switch (item.cheatNum)
      {
        case 3: GInput.cheat1 = GInput.cheat2 = true; break;
        case 2: GInput.cheat2 = true; break;
        default: case 1: GInput.cheat1 = true; break;
      }
      GInput.keysToDo[dik] = true;
    }
    RemoteCheatQueue.Clear();
  #endif
}

#pragma optimize("",on)


#if defined(_WIN32) 
void ProcessTrackIR()
{
  if (!GWorld->IsUserInputEnabled()) return;
  if (TrackIRDev && TrackIRDev->IsEnabled()) TrackIRDev->Get(GInput.trackIRAxis);
  //LogF("NP Roll: %d", GInput.trackIRAxis[TrackIR::TRACKIR_ROLL]); //test only
}

void ProcessFreeTrack()
{
  if ( TrackIRDev && TrackIRDev->IsWorking() ) return; 
  if ( !GWorld->IsUserInputEnabled() ) return;
  if ( FreeTrackDev && FreeTrackDev->IsEnabled() ) FreeTrackDev->Get(GInput.trackIRAxis);
}

void ProcessTrackIRLikeDevices()
{
  ProcessTrackIR();
  ProcessFreeTrack();
  // update GInput.trackIRAxisCleared
  for (int i=0; i<N_TRACKIR_AXES; i++)
  {
    if (GInput.trackIRAxis[i]>0)
    {
      if (GInput.trackIRAxis[i]<GInput.FreeTrackAxisThresholdLow) GInput.trackIRAxisCleared[i] = true;
    }
    else if (-GInput.trackIRAxis[i]<GInput.FreeTrackAxisThresholdLow) GInput.trackIRAxisCleared[i+N_TRACKIR_AXES] = true;
  }
#if _ENABLE_REPORT
  static bool logFreeTrack = false;
  if (logFreeTrack)
  {
    LogF("NP Pitch: %f", GInput.trackIRAxis[TrackIR::TRACKIR_PITCH]);
    LogF("NP Yaw: %f", GInput.trackIRAxis[TrackIR::TRACKIR_YAW]);
    LogF("NP Roll: %f", GInput.trackIRAxis[TrackIR::TRACKIR_ROLL]);
    LogF("NP X: %f", GInput.trackIRAxis[TrackIR::TRACKIR_X]);
    LogF("NP Y: %f", GInput.trackIRAxis[TrackIR::TRACKIR_Y]);
    LogF("NP Z: %f", GInput.trackIRAxis[TrackIR::TRACKIR_Z]);
}
  // average TrackIR/FreeTrack input test
  //   It can eliminate FreeTrack jerking (with worse FreeTrack FPS)
  static bool doIt = false;
  if (doIt)
  {
    static int avgcount = 10;
    static float cache[6][50];
    static int ix=0;
    for (int i=0; i<6; i++)
    {
      cache[i][ix]=GInput.trackIRAxis[i];
      float sum = 0;
      for (int j=0; j<avgcount; j++) sum += cache[i][j];
      GInput.trackIRAxis[i] = sum / avgcount;
    }
    ix = (ix+1)%avgcount;
  }
#endif
}
#endif

void ProcessJoystick()
{
#if ENABLE_DI_JOYSTICK
  if (IsDedicatedClient() || IsDedicatedServer()) return;
  for (int joyIx=0; joyIx<GInput._joysticksState.Size(); joyIx++)
  {
    Input::JoystickState &joy = GInput._joysticksState[joyIx];
    for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
    {
      joy.stickButtons[i] = 0;
      joy.stickButtonsToDo[i] = false;
      joy.stickButtonsUp[i] = false;
      // switch level checking to the next frame
      joy.stickMaxLevelLastFrame[i] = joy.stickMaxLevelThisFrame[i];
      joy.stickMaxLevelThisFrame[i] = 0;
    }
    for (int i=0; i<N_JOYSTICK_POV; i++)
    {
      joy.stickPov[i] = false;
      joy.stickPovToDo[i] = false;
    }

    if (!GWorld->IsUserInputEnabled()) return;

    const int jTHold=20;
    int jAxis[JoystickNAxes];
    unsigned short jButtons[IJoystick::NButtons];
    //bool jButtonEdges[IJoystick::NButtons];
    int pov;

    if( GJoystickDevices && GJoystickDevices->Size()>joyIx && 
      GJoystickDevices->Get(joyIx)->Get(jAxis,jButtons,joy.stickButtonsToDo,joy.stickButtonsUp,joy.stickButtonsPressed,pov)
      )
    {
      for( int i=0; i<JoystickNAxes; i++ )
      {
        if( abs(jAxis[i]-joy.jAxisLast[i])>jTHold )
        {
          joy.jAxisLast[i]=jAxis[i];
          joy.jAxisLastActive[i] = Glob.uiTime;
        }
        joy.stickAxis[i] = jAxis[i]*0.01;
      }
      for (int i=0; i<JoystickNAxes; i++)
      {
        if (joy.stickAxis[i]>0)
        {
          if (joy.stickAxis[i]<joy.StickAxisThresholdLow) joy.stickAxisCleared[i] = true;
        }
        else if(-joy.stickAxis[i]<joy.StickAxisThresholdLow) joy.stickAxisCleared[i+JoystickNAxes] = true;
      }

      Assert(N_JOYSTICK_BUTTONS == IJoystick::NButtons);
      for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
      {
        joy.stickButtons[i] = jButtons[i]*(1.0f/0x7fff);
        saturate(joy.stickButtons[i],-1,+1);
      }
      int pov8 = -1;
      if( pov!=-1 )
      {
        // pov is n*DI_DEGREES
        pov8=toInt(pov*(1.0/45));
        if( pov8>7 ) pov8-=8;
        else if( pov8<0 ) pov8+=8;
        /*
        static const int povKeys[8]=
        {
        DIK_NUMPAD8,DIK_NUMPAD9,DIK_NUMPAD6,DIK_NUMPAD3,
        DIK_NUMPAD2,DIK_NUMPAD1,DIK_NUMPAD4,DIK_NUMPAD7,
        };
        if( pov8>=0 && pov8<8 )
        {
        int dik=povKeys[pov8];
        joy.keysToDo[dik]=true;
        joy.keys[dik]=1;
        }
        */
      } // pov hat
      // pov conversion: index to array
      for (int i=0; i<N_JOYSTICK_POV; i++)
      {
        joy.stickPov[i] = ( pov8==i );
        if (!joy.stickPov[i]) joy.stickPOVCleared[i] = true;

        joy.stickPovToDo[i] = joy.stickPov[i] && !joy.stickPovOld[i];
        joy.stickPovOld[i] = joy.stickPov[i];
      }
    } // JoystickDevices
    if (CHECK_DIAG(DEJoystick))
    {
      RString diagStr = Format("Joystick %d Axes: ", joyIx);
      for (int i=0; i<joy.deadZone.Size(); i++)
      {
        diagStr = diagStr + (i==0 ? Format("%.2f", joy.stickAxis[i]) : Format(", %.2f", joy.stickAxis[i]) );
      }
      DIAG_MESSAGE(250, diagStr);
      diagStr = Format("Joystick %d  Sen: ", joyIx);
      for (int i=0; i<joy.deadZone.Size(); i++)
      {
        float val = joy.sensitivity[i];
        diagStr = diagStr + (i==0 ? Format("%.2f", val) : Format(", %.2f", val) );
      }
      DIAG_MESSAGE(250, diagStr);
      diagStr = Format("Joysticks %d  FIN: ", joyIx);
      for (int i=0; i<joy.deadZone.Size(); i++)
      {
        float sVal = joy.sensitivity[i];
        float val = fabs(joy.stickAxis[i]);
        float steepVal = (joy.steepness.Size()>i) ? joy.steepness[i] : 1.0f;
        GInput.ApplySensitivity(val, sVal, steepVal);
        //float val = pow(fabs(joy.stickAxis[i]), joy.sensitivity[i]);
        if (joy.stickAxis[i]<0) val = -val;
        saturate(val, 0, 1.0f);
        diagStr = diagStr + (i==0 ? Format("%.2f", val) : Format(", %.2f", val) );
      }
      DIAG_MESSAGE(250, diagStr);
    }
  }
#endif
}

static const char *CommandArg(const char *beg, const char *opt)
{
  // when this is used, the last character of opt should be '=', like in -x=800
  Assert(opt[0]!=0 && (opt[strlen(opt)-1]=='=' || opt[strlen(opt)-1]==' '));
  if (strnicmp(beg,opt,strlen(opt))) return NULL;
  return beg+strlen(opt);
}

void ProcessPipeCommands()
{
  RString pipeCommand = CommandingPipe.PeekCommand();
  if (pipeCommand.IsEmpty()) return; // most common situation - no command at all
  if (pipeCommand=="shutdown")
  {
    CloseRequest = true;
  }
  else if (const char *chatText=CommandArg(pipeCommand,"message "))
  {
    // send the ingame chat
    GChatList.Add(CCSystem,"",chatText,false,true);
  }
  else if (const char *replyText=CommandArg(pipeCommand,"reply "))
  {
    // send the response back
    CommandingPipe.Respond(replyText);
  }
}

void ProcessXInput()
{
#if ENABLE_JOYSTICK
  if (IsDedicatedClient() || IsDedicatedServer()) return;
  for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
  {
    GInput.xInputButtons[i] = 0;
    GInput.xInputButtonsToDo[i] = false;
    GInput.xInputButtonsUp[i] = false;
    // switch level checking to the next frame
    GInput.xInputMaxLevelLastFrame[i] = GInput.xInputMaxLevelThisFrame[i];
    GInput.xInputMaxLevelThisFrame[i] = 0;
  }

  if (!GWorld->IsUserInputEnabled()) return;
  if (!GInput.IsXInputPresent()) return;

  int jAxis[JoystickNAxes];
  unsigned short jButtons[IJoystick::NButtons];
  int pov;
  if( XInputDev && XInputDev->Get(jAxis,jButtons,GInput.xInputButtonsToDo,GInput.xInputButtonsUp,GInput.xInputButtonsPressed,pov) )
  {
    Assert(N_JOYSTICK_BUTTONS == IJoystick::NButtons);
    for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
    {
      GInput.xInputButtons[i] = jButtons[i]*(1.0f/0x7fff);
      saturate(GInput.xInputButtons[i],-1,+1);
      //GInput.xInputButtonsToDo[i] = jButtonEdges[i];
    }
  } // XInputDev


#endif
}

#define IsKey(name) GInput.GetKey(GInput.name)>0
#define IsKeyToDo(name) GInput.GetKeyToDo(GInput.name)
#define KeyVal(name) GInput.GetKey(GInput.name)

const bool FilesAsync = true;

#ifdef _WIN32
bool CheckExThreads(int mask, bool autodetected);

static bool UseFilesAsync()
{
  if (!FilesAsync) return false;
  return CheckExThreads(1,GetCPUCount()>1);
}
#endif

void GFileServerStart()
{
  PROFILE_SCOPE_EX(fsSta,*);
#ifdef _WIN32
  if (UseFilesAsync())
  {
    GFileServerFunctions = &GAsyncFileServer;
    GAsyncFileServer.StartWorkerThread();
  }
  else
  {
    GFileServer->Start();
}
#else
  GFileServer->Start();
#endif
}
void GFileServerStop()
{
  PROFILE_SCOPE_EX(fsStp,*);
#ifdef _WIN32
  if (GFileServerFunctions!=GFileServerFunctionsSync)
  {
    GAsyncFileServer.StopWorkerThread();
    GFileServerFunctions = GFileServerFunctionsSync;
  }
  else
#endif
  {
    if (GFileServer)
    {
      GFileServer->Stop();
      GFileServer->Clear();
    }
  }
}

void GFileServerFlush()
{
  PROFILE_SCOPE_EX(fsFls,*);
#ifdef _WIN32
  if (GFileServerFunctions!=GFileServerFunctionsSync)
  {
    GAsyncFileServer.FlushWorkerThread();
  }
  else
#endif
  {
    if (GFileServer)
    {
      GFileServer->Stop();
      GFileServer->Start();
    }
  }
}
void GFileServerClear()
{
  GFileServerStop();
}

void GFileServerWaitForOneRequestDone(int timeout)
{
#ifdef _WIN32
  if (GFileServerFunctions!=GFileServerFunctionsSync)
  {
    Future<WaitableVoid> ret = GAsyncFileServer.WaitForOneRequestDone(timeout);
    ret.GetResult();
  }
  else
  {
    if (GFileServer) GFileServer->WaitForOneRequestDone(timeout);
}
#else
  if (GFileServer) GFileServer->WaitForOneRequestDone(timeout);
#endif
}



void GFileServerUpdate()
{
#ifdef _WIN32
  if (GFileServerFunctions!=GFileServerFunctionsSync)
  {
    GFileServer->UpdateByThread(0);
  }
  else
  {
    //GAsyncFileServer.DoWork(0);
    if (GFileServer) GFileServer->Update();
}
#else
  if (GFileServer) GFileServer->Update();
#endif
}
void GFileServerSubmitRequestsMinimizeSeek()
{
#ifdef _WIN32
  if (GFileServerFunctions!=GFileServerFunctionsSync)
  {
    GAsyncFileServer.SubmitRequestsMinimizeSeek();
}
  else
  {
    if (GFileServer) GFileServer->SubmitRequestsMinimizeSeek();
}
#else
  if (GFileServer) GFileServer->SubmitRequestsMinimizeSeek();
#endif
}

#if _LOAD_ONLY_SIGNED_DATA
class SignatureCheckerThread
{
  class ThrRunnable;
  friend class ThrRunnable;
  class ThrRunnable: public MultiThread::ThreadBase
  {
    SignatureCheckerThread *_outer;
  public:
    ThrRunnable(SignatureCheckerThread *outer) 
      : MultiThread::ThreadBase(MultiThread::ThreadBase::PriorityNormal, 16*1024), 
      _outer(outer) 
    {}
    unsigned long Run()
    {
      _outer->DoWork();
      return 0;
    }
  };

protected:
  ThrRunnable _thread;
  MultiThread::SemaphoreBlocker _submitSemaphore;
  MultiThread::EventBlocker _terminateEvent;
  CriticalSection _lock;

  bool _ready;      //set true after Banks are loaded
  bool _testFailed; //true when last signature test has failed
  bool _waiting;    //when thread waits on semaphore
  SRef<BankSignatureCheckAsync> _checkingSignature; //current signature being tested

public:
#pragma warning(disable:4355)
  SignatureCheckerThread() : _thread(this), _submitSemaphore(INT_MAX, 0), _terminateEvent(), 
     _ready(false), _testFailed(false), _waiting(false) {}
#pragma warning(default:4355)
  ~SignatureCheckerThread();

  //! returns false when the (last) signature check has failed
  bool Maintain();
  void DoWork();
  void GetReady() 
  { 
    _ready=true; 
    _thread.Start();
  }
};
SRef<SignatureCheckerThread> signatureCheckerThread;

SignatureCheckerThread::~SignatureCheckerThread()
{
  if ( _thread.IsRunning() )
  {
    _terminateEvent.Unblock();
    _thread.Join();
  }
}

// returns false when the (last) signature check has failed
bool SignatureCheckerThread::Maintain()
{
  { //scope lock section
    ScopeLockSection lock(_lock);
    BankList::ReadAccess banks(GFileBanks);
    if (!_ready || !banks.Size()) return true; //banks not loaded yet
    if (!_checkingSignature)
    { // SignatureCheckerThread does not work on any job now
      // we should: 
      //   1.) quit the game when the test failed (return false, caller do the oppressive dirty job)
      //   2.) submit new check otherwise
      if (_testFailed) return false; //test failed!
      int index = (toIntFloor(GRandGen.RandomValue() * 0x8000) ) % banks.Size();
      if ( !banks[index].VerifySignature() )
      {
        //LogF("Sync SIG check: SKIPPED!");
        return true; //no test for now
      }
      _checkingSignature = new BankSignatureCheckAsync(index, 2, 2, NMTDataSignatureAnswer, 1000);
      //LogF("Async SIG check: %s", cc_cast(GFileBanks[index].GetOpenName()));
    }
  }
  // let the signature checker thread do the work
  if (_waiting) 
    _submitSemaphore.Release();
  return true;
}

void SignatureCheckerThread::DoWork()
{
  static AcceptedKey acceptedKeys[] = ACCEPTED_KEYS;
  const int acceptedKeysCount = lenof(acceptedKeys);

  DSKey keys[acceptedKeysCount];
  for (int i=0; i<acceptedKeysCount; i++)
  {
    keys[i]._name = acceptedKeys[i].name;
    keys[i]._content.Realloc(KeySize);
    memcpy(keys[i]._content.Data(), acceptedKeys[i].content, KeySize);
  }

  SetThreadName(-1, "SignatureChecker"); //TODO: another name not to inform hackers?
  while (true)
  {
    BlockerArItem objs[] = {&_submitSemaphore, &_terminateEvent};
    _waiting = true;
    DWORD ret = WaitForMultiple(objs, 2, false, INFINITE);
    _waiting = false;
    switch (ret)
    {
    case 1: //semaphore released
      if (_checkingSignature)
      {
        if (_checkingSignature->Process())
        {
          BankList::ReadAccess banks(GFileBanks);
          QFBank &bank = banks[_checkingSignature->GetBankIndex()];
          // check the hash signature
          DSHash hash;
          DSSignature signature;
          if (
            !DataSignatures::FindSignature(signature, bank.GetOpenName(), keys, acceptedKeysCount) ||
            !_checkingSignature->GetHash(hash._content) ||
            !DataSignatures::VerifySignature(hash, signature, 2))
          {
            ErrorMessage(LocalizeString(IDS_HASH_WRONG));
            _testFailed = true;
          }
          _checkingSignature = NULL;
        }
      }
      break;
    case 2: //terminateEvent signaled
      GMemFunctions->OnThreadExit();
      return;
    }
  }
}

class SynchronousSignatureChecker
{
  SRef<BankSignatureCheckAsync> _checkingSignature; //current signature being tested
public:
  SynchronousSignatureChecker() {}
  bool Process();
};
SRef<SynchronousSignatureChecker> syncSigChecker;

bool SynchronousSignatureChecker::Process()
{
  static AcceptedKey acceptedKeys[] = ACCEPTED_KEYS;
  const int acceptedKeysCount = lenof(acceptedKeys);

  DSKey keys[acceptedKeysCount];
  for (int i=0; i<acceptedKeysCount; i++)
  {
    keys[i]._name = acceptedKeys[i].name;
    keys[i]._content.Realloc(KeySize);
    memcpy(keys[i]._content.Data(), acceptedKeys[i].content, KeySize);
  }

  BankList::ReadAccess banks(GFileBanks);
  if (!banks.Size()) return true; //banks not loaded yet
  if (!_checkingSignature)
  { // SignatureCheckerThread does not work on any job now
    // we should submit new check
    int index = (toIntFloor(GRandGen.RandomValue() * 0x8000) ) % banks.Size();
    if ( !banks[index].VerifySignature() )
    {
      //LogF("Sync SIG check: SKIPPED!");
      return true; //no test for now
    }
    _checkingSignature = new BankSignatureCheckAsync(index, 2, 2, NMTDataSignatureAnswer, 1000);
    //LogF("Sync SIG check: %s", cc_cast(GFileBanks[index].GetOpenName()));
  }
  if (_checkingSignature->Process())
  {
    QFBank &bank = banks[_checkingSignature->GetBankIndex()];
    // check the hash signature
    DSHash hash;
    DSSignature signature;
    if (
      !DataSignatures::FindSignature(signature, bank.GetOpenName(), keys, acceptedKeysCount) ||
      !_checkingSignature->GetHash(hash._content) ||
      !DataSignatures::VerifySignature(hash, signature, 2))
    {
      ErrorMessage(LocalizeString(IDS_HASH_WRONG));
      _checkingSignature = NULL;
      return false;
    }
    _checkingSignature = NULL;
  }
  return true; //ok
}
#endif

#if !_SERVER
bool AppIdle()
{
  
  // signal frame to external profilers
  // implemented for custom modification of very sleepy
  #if SLEEPY
  {
    PROFILE_SCOPE_EX(slFrm,*);
    Sleepy.Frame();
  }
  #endif

  #if _ENABLE_CHEATS
  void StressTest();
  StressTest();
  #endif

#if 0 // _PROFILE
  {
    static const char *lzoTest[] = {
      "ca\\structures_e\\misc\\misc_interier\\data\\misc_int_03_nohq.paa",
      "ca\\structures_e\\misc\\misc_interier\\data\\misc_int_05_co.paa",
      NULL
    };
    PROFILE_SCOPE_EX(lzoTe,*);
    for (int i=0; i<4; i++) for (const char **testFile=lzoTest; *testFile; testFile++)
    {
      SectionTimeHandle start = StartSectionTime();
      QIFStreamB in;
      in.AutoOpen(*testFile);

      float open = GetSectionTime(start);
      in.PreRead();
      float preRead = GetSectionTime(start);

      ITextureSourceFactory *factory = SelectTextureSourceFactory(*testFile);

      PacLevelMem mips[64];
      SRef<ITextureSource> src = factory->Create(*testFile,mips,lenof(mips));
      mips[0]._dFormat = mips[0]._sFormat;
      size_t size = mips[0].MipmapSize(mips[0]._sFormat,mips[0]._w,mips[0]._h);
      mips[0]._pitch = size/mips[0]._h;
      char *mem = new char[size];

      float texture = GetSectionTime(start);

      src->GetMipmapData(mem,mips[0],0,size);

      float decomp = GetSectionTime(start);
      delete[] mem;
      //LogF("%s: open %g, preRead %g, decompress %g",*testFile,open,preRead-init,decomp-preRead);
      LogF("%s: decompress %g",*testFile,decomp-texture);
    }
  }
#endif

  //static bool timeValid=false;
  //    timeValid=false;
  bool enableDraw=GetEnableDraw();

  if (SteamOverlayClosed)
  {
    EnableMouseMovement(MouseCanMove, SteamOverlayClosed); //reestablish the old value
    SteamOverlayClosed = false;
  }
  else EnableMouseMovement(!GAppActive || !GEngine || GEngine->IsCursorShown());

  static DWORD lastTime;
  DWORD actTime=::GlobalTickCount();
  DWORD deltaTMs = actTime-lastTime;

  if (!enableDraw || !IsAppFocused())
  {
    int targetMs = 50;
#if _PROFILE && USER_Ondra
      if (!IsAppFocused()) targetMs = 100;
#endif
    if (deltaTMs<targetMs)
    {
      SleepWithUpdate(targetMs-deltaTMs);
      actTime=::GlobalTickCount();
      deltaTMs = actTime-lastTime;
    }
  }

  if (GFileServer && GFileServerStarted)
  {
    // calling Stop multiple times has no sense, but it should do little harm
    if (GFileServerActive!=GAppActive)
    {
      PROFILE_SCOPE_EX(fsAct,*);
      if (GAppActive)
      {
        GFileServerActive = true;
        //GFileServerStart();
      }
      else
      {
        bool deactivated = true;
        if (GEngine) deactivated = GEngine->Deactivate();
        if (deactivated)
        {
          GFileServerActive = false;
          GFileServerFlush();
          GDebugExceptionTrap.FlushLog();
          // we must start again immediately, otherwise async server would get stuck
          #if _ENABLE_REPORT
            //GLogFile.Flush();
          #endif
        }

      }
    }
  }

#if _GAMES_FOR_WINDOWS
  GSaveSystem.Simulate();
#endif

  float deltaT=deltaTMs*0.001;
  lastTime=actTime;


  // note: GetDeviceData reports relative to GetTickCount
  static DWORD lastSysTime;
  DWORD sysTime=::GetTickCount();
  DWORD timeDelta=sysTime-lastSysTime;
  lastSysTime=sysTime;
  //if( !timeValid ) timeDelta=0,deltaT=0;
  //timeValid=true;

  saturateMin(deltaT,0.3);

#if _LOAD_ONLY_SIGNED_DATA
  if (UseFilesAsync())
  {
    if ( signatureCheckerThread && !signatureCheckerThread->Maintain() )
    {
      PostMessage((HWND)hwndApp,WM_CLOSE,0,0);
      signatureCheckerThread = NULL; //terminate
    }
  }
  else
  { //synchronous solution
    if (syncSigChecker && !syncSigChecker->Process() )
    {
      PostMessage((HWND)hwndApp,WM_CLOSE,0,0);
      syncSigChecker = NULL; //terminate
    }
  }
#endif

  #if _ENABLE_PERFLOG && _ENABLE_CHEATS
  static bool EnablePerfLog = false;
  if
  (
    GInput.GetCheat2ToDo(DIK_G)
  )
  {
    EnablePerfLog = !EnablePerfLog;
    GlobalShowMessage(500,"PerfLog %s",EnablePerfLog ? "On":"Off");
    if (EnablePerfLog)
    {
      FreeOnDemandGarbageCollect(256*1024,256*1024);
      OpenPerfCounters();
    }
    else
    {
      ClosePerfCounters();
    }
  }
  #endif
  //if (!enableSim) deltaT=0;

  //if( ( focused || ForceRender && LandEditor ) && CanRender )

  SteamAPI_RunCallbacks();

  ProcessPipeCommands();
  if( CanRender )
  {
    if( UseWindow && !GAppActive || !enableDraw )
    {
      RenderFrame(deltaT, enableDraw, enableDraw);
    }
    else
    {
      {
        PROFILE_SCOPE(input);
        // use buffered events, use proportional integration of buttons time
        // game time

        // system time - necessary for keyboard events
        
        // read input devices
        //ProcessMouse(timeDelta);
        ProcessKeyboard(sysTime, timeDelta);
        if (!MouseEx && GInput.mouseButtons[0] || GInput.mouseButtons[1] || GInput.mouseButtons[2] || GInput.mouseButtons[3])
          SynchronizeCursors();
        if (GInput.CheatActivated() == CheatFreeze)
        {
          //ErrorMessage("Test error");

          GInput.CheatServed();
          for(;;){}
          // freeze - infinite loop
        }
#if _ENABLE_CHEATS
        GInput.ProcessVoNCheats();
#endif

        ProcessJoystick();
        ProcessXInput();
        #if defined(_WIN32) 
        ProcessTrackIRLikeDevices();
        #endif

        // process cheats
  #if _ENABLE_CHEATS
        if (GInput.Cheats12Enabled())
        {
          GInput.cheat1 = false;
          GInput.cheat2 = false;
          bool cheat1 = GInput.GetAction(UACheat1, false)>0;
          bool cheat2 = GInput.GetAction(UACheat2, false)>0;
          GInput.cheat1 = cheat1;
          GInput.cheat2 = cheat2;

          // simulate key strokes using cheats
          RemoteCheats();
        }
  #endif // #if _ENABLE_CHEATS

        // map actions
        //GInput.lookAroundEnabled = GInput.lookAroundToggleEnabled;

        for (int i=0; i<UAN; i++) GInput.actionDone[i] = false;

      }      

      RenderFrame(deltaT, true, true);
    }
    return false;
  }
  else
  {
    // Don't do anything when not the active app
    return true;
  }
}
#endif

bool AppServerLoop()
{
  PROFILE_SCOPE(sLoop);

  static DWORD lastTime;
  DWORD actTime=::GlobalTickCount();
  DWORD deltaTMs = actTime-lastTime;

  float deltaT=deltaTMs*0.001;
  lastTime=actTime;

  static DWORD lastSysTime;
  DWORD sysTime=::GetTickCount();
  DWORD timeDelta=sysTime-lastSysTime;
  lastSysTime=sysTime;
  saturateMin(deltaT,0.300f);
  
  bool focused = GAppActive && !GAppIconic;

  if (focused)
  {
    ProcessKeyboard(sysTime, timeDelta);
    ProcessJoystick();
    ProcessXInput();
    #if defined(_WIN32) 
    ProcessTrackIRLikeDevices();
    #endif
  }


  #if _ENABLE_PERFLOG && _ENABLE_CHEATS
  static bool EnablePerfLog = false;
  if
  (
    GInput.GetCheat2ToDo(DIK_G)
  )
  {
    EnablePerfLog = !EnablePerfLog;
    GlobalShowMessage(500,"PerfLog %s",EnablePerfLog ? "On":"Off");
    if (EnablePerfLog)
    {
      OpenPerfCounters();
    }
    else
    {
      ClosePerfCounters();
    }
  }
  #endif

  bool enableSceneDraw = false;
  bool enableHUDDraw = false;

  if( CanRender )
  {
    // process cheats
#if _ENABLE_CHEATS
    if (GInput.Cheats12Enabled())
    {
      GInput.cheat1 = false;
      GInput.cheat2 = false;
      bool cheat1 = GInput.GetAction(UACheat1, false)>0;
      bool cheat2 = GInput.GetAction(UACheat2, false)>0;
      GInput.cheat1 = cheat1;
      GInput.cheat2 = cheat2;
    }
#endif // #if _ENABLE_CHEATS

    // map actions
    for (int i=0; i<UAN; i++) GInput.actionDone[i] = false;
    RenderFrame(deltaT, enableSceneDraw, enableHUDDraw);
    return false;
  }

  return true;
}

#if PROTECTION_ENABLED

#include <El/CRC/crc.hpp>

/*!
\name Fade
Simple easy to find and hack FADE test.
If the test fails, global variable DoFade is set and program can
degrade in any way.
Please insert list of checkpoints where CheckFade should be performed:
(It may be anywhere in the game or UI, if it is hit once per session, it is enough).
(Timing guideline: 50 ms)
  Landscape::LoadData

Please insert list of degradations:
  Bigger rifle dispersion.
  InGameUI message (with very low probability) Original games do not fade.
  TODO: Slowly decrease experience (score).

For second level protection see CheckGenuine
Please insert list of checkpoints where CheckGenuine should be performed:
(This protection should be called several times during typical mission).
(Timing guideline: 10 ms)
  ????

\patch_internal 1.01 Date 6/21/2001 by Ondra
- New: FADE protection
\patch_internal 1.35 Date 12/11/2001 by Ondra added FADE protection
- Improved: 2nd level checksum protection.
*/
//@{

//! integrity check failed - FADE
#if ALWAYS_FAIL_CRC
bool DoFade = true;
#else
bool DoFade = false;
#endif

void *DoFadeEventHandle;

//! What should be result of CRC test

//! perform integrity check (CRC)
void CheckFade()
{
#if ENABLE_PROTECTION_CRC
  CRCCalculator crc;
  unsigned crcAct = crc.CRC((void *)CRC_BASE_MAGIC,CRC_SIZE_MAGIC);
  if (crcAct!=CRC_VALUE_MAGIC)
  {
    // start fading
    DoFade = true;
    #if CHECK_FADE
    FILE *f = fopen("bin\\fade.txt","a");
    if (f)
    {
      fprintf(
        f,"FADE CRC failed (%x!=%x)\n",
        crcAct,CRC_VALUE_MAGIC
      );
      fclose(f);
    }
    #endif
  }
#endif
}

#if CHECK_FADE

void ExtractFadeArea(void *start, int size)
{
  if (DoFade)
  {
    WarningMessage("CRC area protection failed.");
  }
  FILE *f = fopen("bin\\fade.txt","a");
  if (f)
  {
    fprintf
    (
      f,"FADE range from %x to %x\n",
      start,(int)start+size
    );
  }
  FILE *b = fopen("bin\\fade.bin","wb");
  if (b)
  {
    fwrite(start,size,1,b);
    fclose(b);
  }
  FILE *c = fopen("bin\\code.bin","wb");
  if (c)
  {
    // write all code (should be copy of executable)
#define CODE_OFFSET 0x400000
#define CODE_START 0x401000
#define CODE_SIZE  0x4f3000

    MEMORY_BASIC_INFORMATION mbi;

    if ( !VirtualQuery( ExtractFadeArea, &mbi, sizeof(mbi) ) )
        return;

    DWORD hMod = (DWORD)mbi.AllocationBase;

    // Point to the DOS header in memory
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;

    // From the DOS header, find the NT (PE) header
    PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)(hMod + pDosHdr->e_lfanew);

    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION( pNtHdr );

    int codeStart = pSection->VirtualAddress+CODE_OFFSET;
    int codeSize = pSection->SizeOfRawData;
    
    fwrite((void *)CODE_START,CODE_SIZE,1,c);

    fprintf
    (
      f,"Exe code %x, size %x\n",
      codeStart,codeSize
    );
    
    fclose(c);
  }
  if (f)
  {
    fclose(f);
  }
}

void ReportFade()
{
  ExtractFadeArea((void *)CRC_BASE_MAGIC,CRC_SIZE_MAGIC);
}

#else

#define ReportFade()

#endif

//@}

#endif // PROTECTION_ENABLED

static int ActualResolution( int *resol )
{
  int i;
  for( i=0;;i++ )
  {
    if( resol[i*2+0]==GLOB_ENGINE->Width() && resol[i*2+1]==GLOB_ENGINE->Height() )
    {
      return i;
    }
    if( resol[i*2+0]<=0 || resol[i*2+1]<=0 )
    {
      break;
    }
  }
  return 0;
}


#if 0 //!_RELEASE
int _matherr( struct _exception *except )
{
  Fail("Math error");
  return 0;
}
#endif

void InitFPU()
{
#ifdef _WIN32
  // set rounding mode to near
  // disable all exceptions
  // set precision
  // default in Watcom 10.6: RC_NEAR|PC_53|MCW_EM|IC_AFFINE
  //_control87(RC_NEAR|PC_53|MCW_EM,MCW_EM|MCW_RC|MCW_PC);
#if _ENABLE_REPORT
  if (GDebugger.IsDebugger() && !NoFPTraps)
  {
    // following control word is selected to be compatible with Direct3D
    _control87(
      RC_NEAR|PC_24|
      MCW_EM&~
      (
        _EM_INVALID|
        _EM_ZERODIVIDE|
        //_EM_OVERFLOW|_EM_UNDERFLOW|
        0
      )|_DN_SAVE_OPERANDS_FLUSH_RESULTS,
      MCW_EM|MCW_RC|MCW_PC|_MCW_DN
    );
  }
  else
#endif
  {
    _control87(RC_NEAR|PC_24|MCW_EM|_DN_SAVE_OPERANDS_FLUSH_RESULTS,MCW_EM|MCW_RC|MCW_PC|_MCW_DN);
  }
  #if USING_SSE
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#if _ENABLE_REPORT
    if (GDebugger.IsDebugger() && !NoFPTraps)
    {
      //Assert ( _MM_GET_EXCEPTION_MASK()== _MM_MASK_MASK);
      _MM_SET_EXCEPTION_MASK(
        _MM_MASK_MASK&~(
          _MM_MASK_INVALID|
          _MM_MASK_DIV_ZERO|
          //_MM_MASK_DENORM|
          //_MM_MASK_OVERFLOW|_MM_MASK_UNDERFLOW|
          //_MM_MASK_INEXACT|
          0
        )
      );
    }
    else
#endif
    {
      // disable all exceptions - should be default
      _MM_SET_EXCEPTION_MASK(_MM_MASK_MASK);
    }
  #endif
  
  #if 0 //_DEBUG
    unsigned int fp_cw = 0;
    unsigned int fp_mask = 0;
    unsigned int bits;
    
    const static char *status[2] = { "disabled", "enabled" };
    
    fp_cw = _controlfp( fp_cw, fp_mask );
    
    LogF( "Interrupt Exception Masks" );
    bits = fp_cw & MCW_EM;
    LogF( "  Invalid Operation exception %s", status[ (bits & _EM_INVALID) == 0 ] );
    LogF( "  Denormalized exception %s", status[ (bits & _EM_DENORMAL) == 0 ] );
    LogF( "  Divide-By-Zero exception %s", status[ (bits & _EM_ZERODIVIDE) == 0 ] );
    LogF( "  Overflow exception %s", status[ (bits & _EM_OVERFLOW) == 0 ] );
    LogF( "  Underflow exception %s", status[ (bits & _EM_UNDERFLOW) == 0 ] );
    LogF( "  Precision exception %s", status[ (bits & _EM_INEXACT) == 0 ] );
    
    LogF( "Infinity Control" );
    bits = fp_cw & _MCW_IC;
    if( bits == _IC_AFFINE )     LogF( "  affine" );
    if( bits == _IC_PROJECTIVE ) LogF( "  projective" );
    
    LogF( "Rounding Control" );
    bits = fp_cw & _MCW_RC;
    if( bits == _RC_NEAR )       LogF( "  near" );
    if( bits == _RC_DOWN )       LogF( "  down" );
    if( bits == _RC_UP )         LogF( "  up" );
    if( bits == _RC_CHOP )       LogF( "  chop" );
    
    LogF( "Precision Control" );
    bits = fp_cw & _MCW_PC;
    if( bits == _PC_24 )         LogF( "  24 bits\n" );
    if( bits == _PC_53 )         LogF( "  53 bits\n" );
    if( bits == _PC_64 )         LogF( "  64 bits\n" );
  
  #endif
  // Clear the exception flags (to not to handle the old exception in the first floating operation)
  _clearfp();
#endif
}

void DeinitFPU()
{
#ifdef _WIN32
  // disable all exceptions
  if (GDebugger.IsDebugger())
  {
    _controlfp(RC_NEAR|PC_24|MCW_EM,MCW_EM|MCW_RC|MCW_PC);
  }
#if defined _KNI
#if _ENABLE_REPORT
  #if _XBOX_VER>=2
    // TODOX360: check if needed
  #else
  if (GDebugger.IsDebugger() && !NoFPTraps)
  {
    _MM_SET_EXCEPTION_MASK(_MM_MASK_MASK);
  }
  #endif
#endif
#endif
#endif
}

void DestroyEngine()
{
  if( GEngine ) delete GEngine,GEngine = NULL;
}

// ....

extern Ref<Script> ProgressScript;


static void AppWinDestroy( HWND hwnd );

void ReleaseVBuffers();

void ClearNetServer();
void ClearClutterDrawCache();

/// avoid doing DDTerm when the game was not initialized yet

static bool DDInitDone = false;

void DDTerm()
{
  if (!DDInitDone) return;

  GDebugger.PauseCheckingAlive();
  if( GEngine ) GEngine->ReinitCounters();
  
  #if !defined _WIN32
  LogF("AppWinDestroy %x",hwndApp);
  AppWinDestroy((HWND)hwndApp);
  #endif

  CanRender=false;
  if( GEngine ) GEngine->StopAll();
#ifdef _WIN32
  markersMap.Clear();
  CurrentTemplate = new DummyMission;
#endif
  ReleaseVBuffers();

#ifdef ENF_ENGINE
	enf::DestroyWinEngine();
#endif

  // close the Dedicated Server LogFile
  GetNetworkManager().CloseDedicatedServerLogFile();
  GetNetworkManager().Done(); // close Dedicated Server itself
  GJobManager.CancelAllJobs(); // finish or cancel all jobs - esp. the GEngine ones

  CurrentDebugEngineFunctions->Shutdown();

  ObjectActions.Clear();
  SurfaceActions.Clear();
  Recipes.Clear();

	if( GetWResource() )
		DestroyWResource();

  if (GWorld)
  {
    DoneWorld();
    delete GWorld;
    GWorld = NULL;
    GScene = NULL;
  }
  if( GSoundsys ) delete GSoundsys,GSoundsys=NULL;

#if _ENABLE_INDEPENDENT_AGENTS && _ENABLE_IDENTITIES
  AITaskTypes.Clear();
#endif
  FSMScriptedTypes.Clear();
  FSMEntityTypes.Clear();
  GChatList.Free();
  ProgressScript.Free();
  DestroyJavaVM();
  ClearStringtable();
  Man::ClassCleanUp();
  EntityAI::ClassCleanUp();
  ClearClutterDrawCache();
  GStats.ClearAll();
  ClearNetServer();
  Glob.Clear();

  // free variables from all global namespaces
  GParsingNamespace.Reset();
  GUINamespace.Reset();
  GDefaultNamespace.Reset();

  GlobPreloadedTexturesClear();
  DestroyEngine();
#if _GAMES_FOR_WINDOWS
  XOnlineCleanup();
  XLiveUnInitialize();
#endif

  SteamAPI_Shutdown();
  #if ENABLE_KEYMOUSE
  if (dInput) dInput->Release(), dInput = NULL;
  if (UseWindow) ClipCursor(NULL);
  #endif
  #if PROTECTION_ENABLED && CHECK_FADE
  CheckFade();
  ReportFade();
  #endif

  Pars.Clear();

  //AddonConfigs is temporary, but in case of error in addon config
  // we may exit while it is not empty - this would crash
  AddonConfigs.Clear();
  // Pars.DeleteVariables();
  ExtParsMission.Clear();
  // ExtParsMission.DeleteVariables();
  // Res.Clear();
  // Res.DeleteVariables();
  GFileServerClear();
  GFileServer->Finalize();
#ifndef _WIN32
  extern MultiThread::RunningThread<MultiThread::ThreadBase> *GMultiThreadRegistration;
  if (GMultiThreadRegistration) delete GMultiThreadRegistration;
#endif

  // remove my own pid_file:
#if _ENABLE_DEDICATED_SERVER
  if ( MyPidFile )
  {
    DeleteFile((const char*)GetPidFileName());
  }
#endif
  #if _WIN32
    void MemoryMainDone();

    MemoryMainDone();
  #endif

}

void DDTermError()
{
  #if defined(_WIN32) 
  if (!DDInitDone) return;

  GDebugger.PauseCheckingAlive();
  FinishDInput();
  if (hwndApp)
  {
    DestroyWindow((HWND)hwndApp);
  }
  #else
  DDTerm();
  #endif
}

static void AppUnpause()
{
  PausedCount = 0;
  GAppPaused = false;
  SkipKeysInThisFrame(); // ignore any outstanding key events
  //LogF("App un-paused");
  if (GEngine) GEngine->ResetTimingHistory(50);
}


static void AppPause(bool f, const char *reason)
{
  if (f)
  {
    LogF("App paused BEG %d - %s",PausedCount,reason);
    if (PausedCount++>0)
    {
      return;
    }
    // turn off rotation while paused
    GAppPaused = true;
    //LogF("App paused %s",reason);
    if( CanRender ) GLOB_ENGINE->Pause();
  }
  else
  {
    LogF("App paused END %d - %s",PausedCount-1,reason);
    if (--PausedCount>0)
    {
      return;
    }
    DoAssert(PausedCount==0);
    AppUnpause();
  }
}

// Global function callback to:
//   - react to WM_QUIT
//   - check whether Escape key is pressed (then returns false)
bool KeepAppActive()
{
  const DWORD repeatTime = 200;
  static DWORD lastSysTime = 0;
  DWORD sysTime=::GetTickCount();
  if ( sysTime >= lastSysTime+repeatTime ) // check only each 200ms
  {
    DWORD timeDelta=sysTime-lastSysTime;
    lastSysTime=sysTime;

    // Test whether key ESC is pressed
    // Note: KeepAppActive is often regularly called from one frame, so ProcessKeyboard should be called with SkipKeysUntilFrame reset
    int backupSkipKeysUntilFrame = SkipKeysUntilFrame;
    SkipKeysUntilFrame = 0;
    ProcessKeyboard(sysTime, timeDelta);
    SkipKeysUntilFrame = backupSkipKeysUntilFrame;
#ifdef _WIN32
    if ( GInput.GetKeyToDo(DIK_ESCAPE) )
    {
      return false; //caller should stop processing as Escape key was pressed
    }
#endif
    // Check WM_QUIT, WM_CLOSE, WM_DESTROY
    if (CloseRequest) return false;
    // we assume caller is doing win message peeking, for instance by calling ProgressRefresh()
    // so there is no need to make sure aplication is responding here
  }
  return true;
}

// dirty flag - used for save game when Alt-F4 pressed
extern bool ContinueSaved;

static void AppWinDestroy( HWND hwnd )
{
  GDebugger.PauseCheckingAlive();

  FinishDInput();
    
#if defined(_WIN32) 
  SetCursor(LoadCursor(NULL,IDC_WAIT));
#endif
  
  // shutdown the Network before shutting down
  // note: do it before World destruct, as destructors inside Network can work with Landsape/World and crash, see news:jum20p$mjn$1@new-server.localdomain
  GetNetworkManager().Done();
#if _ENABLE_MP
  void doneNetwork();
  doneNetwork();
#endif

  if( GWorld )
  {
    // save variables from Profile Namespace into the user profile
    GWorld->SaveProfileNamespace();

    GWorld->UnloadSounds();
    GWorld->DestroyUserDialog();

    // save state for continue
    bool CanSaveContinue();
    if (!ContinueSaved && CanSaveContinue())
    {
      if (!IsOutOfMemory())
      {
        FreeOnDemandGarbageCollect(1024*1024,512*1024);
        GEngine->PrepareScreenshot();
        GWorld->SaveGame(SGTContinue);
        GEngine->CleanUpScreenshot();
      }
    }
  
    DoneWorld();
    delete GWorld;
    GWorld = NULL;
    GScene = NULL;
    Shapes.ClearCache();
  }

  GlobPreloadedTexturesClear();
#if _VBS3
  GVBSVisuals.ReleaseTextures();
#endif
  if( GSoundScene ) delete GSoundScene,GSoundScene=NULL;
  if( GSoundsys ) delete GSoundsys,GSoundsys=NULL;
  //if( Glob.soundsys ) Glob.soundsys->Delete();
  
  //KillTimers(hwnd);
  #if ENABLE_KEYMOUSE
  if (dInput) dInput->Release(),dInput=NULL;
  #endif

  #if ENABLE_DI_JOYSTICK
  GJoystickDevices.Free();
  #endif
  #if ENABLE_JOYSTICK
  XInputDev.Free();
  #endif

#ifdef _WIN32
  hwndApp = NULL;
  LogF("hwndApp = NULL");
#else
  hwndApp=0;
#endif

  if (!UseWindowByCommandLine)
  {
    ParamFile fCfg;
    ParseFlashpointCfg(fCfg);
    fCfg.Add("Windowed",UseWindow);
    SaveFlashpointCfg(fCfg);
}
  
}

// avoid processing of messages WM_KEYDOWN etc. in game loops (for example in Progress)
enum AvoidUserInput
{
  AUIUnititialized,
  AUIFalse,
  AUITrue
};
AvoidUserInput avoidUserInput = AUIUnititialized;

#ifdef _WIN32


#if _ENABLE_REPORT || _ENABLE_BULDOZER
void CheckForChangedFiles()
{
  // checking for changed files and reloading them may take very long
  // pause any checking meanwhile
  Debugger::PauseCheckingScope scope(GDebugger);
  
  GTexMaterialBank.Reload();
  if (GEngine)
  {
    GEngine->TextBank()->CheckForChangedFiles();
  }
}
#endif

static void AppActivate(bool active)
{
  GAppActive = active;
  if( !active )
  {
    LogF("App inactive");
    // unacquire everything if app is not active
    SkipKeysInThisFrame(); // ignore any outstanding key events
    if( GSoundsys ) GSoundsys->Activate(false);
    // avoid stopping the file-server during progress / loading screens
    // delay it after the progress is done
    // stopping is done in reaction to GAppActive instead
    //if (GFileServer && GFileServerStarted) GFileServer->Stop();
    // restore screenSaver backup
    if (UseWindow && !LandEditor)
    {
      SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, screenSaverEnabledBackup, 0, 0);
    }
  }
  else
  {
    LogF("App active");
    SkipKeysInThisFrame(); // ignore any outstanding key events
    if( GSoundsys ) GSoundsys->Activate(true);    
    // if Start() is called multiple times, no harm is made
    //if (GFileServer && GFileServerStarted) GFileServer->Start();
    if (GEngine && PauseInBackround) GEngine->ResetTimingHistory(50);
    if (UseWindow && !LandEditor)
    {
      //screensaver should be disabled (long cutscenes etc.)
      SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &screenSaverEnabledBackup, 0);
      SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, FALSE, 0, 0);
    }
  }
}

static void ChangeWindowSize(HWND hwnd)
{
  GetClientRect(hwnd, &clientRect);
  ClientToScreen(hwnd, (LPPOINT)&clientRect);
  ClientToScreen(hwnd, (LPPOINT)&clientRect+1);
  if( GLOB_ENGINE && CanRender )
  {
    int w=clientRect.right-clientRect.left;
    int h=clientRect.bottom-clientRect.top;
    if( w>=10 && h>=10 )
    {
      // ignore nonsense
      LogF("Window switch resolution (%dx%d)",w,h);
      GEngine->SwitchRes(clientRect.left,clientRect.top,w,h,GEngine->PixelSize());
    }
  }
}

/*!
\patch 1.31 Date 11/13/2001 by Jirka
- Fixed: Korean version - error in WM_IME_CHAR message processin
\patch 5117 Date 1/11/2007 by Jirka
- Fixed: The game did not process mouse input when running in fullscreen on the secondary monitor
*/

#include <winuser.h>

LONG CALLBACK AppWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
  DoAssert(avoidUserInput != AUIUnititialized);

  #if _GAMES_FOR_WINDOWS
    if (msg == WM_IME_SETCONTEXT)
    {
      lParam = 0;
      if (IsWindowUnicode(hwnd))
        return DefWindowProcW(hwnd,msg,wParam,lParam);
      else
        return DefWindowProc(hwnd,msg,wParam,lParam);
    }

    // allow Games for Windows - LIVE to handle input
    XLIVE_INPUT_INFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.hWnd = hwnd;
    info.uMsg = msg;
    info.wParam = wParam;
    info.lParam = lParam;

    HRESULT result = XLiveInput(&info);
    if (SUCCEEDED(result) && info.fHandled) return info.lRet;
  #endif

  if( GLOB_ENGINE )
  {
    if( hwnd==hwndApp ) switch( msg )
    {
      case WM_ACTIVATE:
      {
        // as WM_ENTERSIZEMOVE/ WM_EXITSIZEMOVE is not always executed in pairs, it is safer to reset it
        // when activating the window. This allows at least some recovery method. See news:e6lrfe$p1h$1@new_server.localdomain
        while (GSizeMoveCount>0)
        {
          GSizeMoveCount--;
          AppPause(false,"WM_ACTIVATE");
        }
        // catch activate/deactivate
        WORD fActive = LOWORD(wParam);
        BOOL fMinimized = (BOOL) HIWORD(wParam);
        if ( ( fActive == WA_INACTIVE ) || fMinimized )
        {
          EnableMouseMovement(true);
          GEngine->Deactivate();
        }
        else
        {
          GEngine->Activate();
#if _ENABLE_BULDOZER || _ENABLE_REPORT
          bool doCheck = true;
#if _ENABLE_BULDOZER
          // we do not want Visitor to check for all file changes
          // it takes very long and it is not that important for it
          extern bool ConnectedToVisitor;
          if (ConnectedToVisitor) doCheck = false;
#endif
#if !_ENABLE_REPORT
          // we do not want the retail game to check for all file changes
          if (!LandEditor)
          {
            doCheck = false;
          }
#endif
          if (doCheck)
          {
            CheckForChangedFiles();
          }
#endif

        }
        GInput.ForgetKeys();
        if (fActive==WA_INACTIVE || fMinimized)
          {
          INPUT input;
          input.type=INPUT_MOUSE;
          input.mi.dx=0;
          input.mi.dy=0;
          input.mi.dwFlags=
            ((GetKeyState(VK_LBUTTON) & 0x80)!=0?MOUSEEVENTF_LEFTUP:0)|
            ((GetKeyState(VK_RBUTTON) & 0x80)!=0?MOUSEEVENTF_RIGHTUP:0)|
            ((GetKeyState(VK_MBUTTON) & 0x80)!=0?MOUSEEVENTF_MIDDLEUP:0);
          input.mi.time=GetTickCount();
          SendInput(1,&input,sizeof(INPUT));
          }
      }
      break;
    }
  }
  if (msg >= WM_USER)
  {
    if (GWorld && GWorld->UI())
      return GWorld->UI()->ProcessUserMessage(msg, wParam, lParam);
  }
  switch (msg)
  {
    case WM_CREATE:
      hwndApp=hwnd;
      LogF("WM_CREATE hwndApp=%x",hwndApp);
      InitWindow((HINSTANCE)hInstApp,hwnd,UseWindow,SMaintainTitle.GetAppendix());
      if( !AppWinCreate(hwnd) ) return -1; // some fatal error
    break;
    case WM_KILLFOCUS:
      LogF("WM_KILLFOCUS %x,%x",wParam,lParam);
      break;
    case WM_SETFOCUS:
      LogF("WM_SETFOCUS %x,%x",wParam,lParam);
      break;
    case WM_ACTIVATE:
    {
      // catch activate/deactivate
      LogF("WM_ACTIVATE %x,%x, %x",LOWORD(wParam),HIWORD(wParam),hwnd);
      if (UseWindow)
      {
        WORD fActive = LOWORD(wParam);
        BOOL fMinimized = (BOOL) HIWORD(wParam);
        AppActivate(( fActive != WA_INACTIVE ) && !fMinimized );
      }
    }
    break;
    #if 1
    case WM_ACTIVATEAPP:
      // Keep track of whether or not the app is in the foreground
      LogF("WM_ACTIVATEAPP %x, %x",wParam,hwnd);
      if (!UseWindow)
      {
        AppActivate(wParam!=0);
      }
    break;
    case WM_WINDOWPOSCHANGED:
    {
      //WINDOWPOS *wp = (WINDOWPOS *)lParam;
      //HWND after = wp->hwndInsertAfter;
      //LogF("WM_WINDOWPOSCHANGED after %x:%s",after,(const char *)GetWindowName(after));
    }
    break;
    case WM_POWERBROADCAST:
      switch(wParam)
      {
        case PBT_APMSUSPEND:
          RptF("Info: Computer going to Standby...");
          GDebugger.PauseCheckingAlive();
          break;
        case PBT_APMRESUMESUSPEND:
          RptF("Info: Computer resuming from Standby...");
          GDebugger.ResumeCheckingAlive();
          break;
        case PBT_APMRESUMECRITICAL:
          // we are resuming, but we never were notified about not resuming
          // still this might prevent from shutdown
          RptF("Info: Computer resuming from Critical Standby...");
          GDebugger.ResumeCheckingAlive();
          break;
      }
      break;
    case WM_SETCURSOR:
       if (GAppActive && !GAppIconic)
       { 
         //The low-order word of lParam specifies the hit-test code. 
         // The high-order word of lParam specifies the identifier of the mouse message. 
         // when cursor is above borders, we want to see a corresponding cursor
         if (!UseWindow || (lParam&0xffff)==HTCLIENT)
         {
           // notify the engine we are owning the cursor and it should change its shape
           // DIAG_MESSAGE(2000,"WM_SETCURSOR client");
           GEngine->CursorOwned(true);
           return TRUE; // TRUE == prevent Windows from setting cursor to window class cursor
         }
       }
       // notify the engine we are not owning the cursor and it should not change its shape
       GEngine->CursorOwned(false);
       //DIAG_MESSAGE(5000,"WM_SETCURSOR non-client");
      break;
    case WM_MOUSEMOVE:
      {
        int xPos = (short)lParam;
        int yPos = (short)(lParam>>16);
        // in full-screen the message parameters are unreliable
        // use GetCursorPos instead
        if (!UseWindow)
        {
          // use GetCursorInfo instead of GetCursorPos - see http://msdn.microsoft.com/en-us/library/ms648381%28VS.85%29.aspx
          CURSORINFO ptCursor;
          ptCursor.cbSize = sizeof(ptCursor);
          GetCursorInfo( &ptCursor );
          // note: perhaps we should call ScreenToClient here
          // but it seems this call is very unreliable in full-screen
          xPos = ptCursor.ptScreenPos.x;
          yPos = ptCursor.ptScreenPos.y;
          
          static bool EnableCursorUpdate = false;

          if (EnableCursorUpdate)
            GEngine->UpdateCursorPos(xPos,yPos);

          // translate from the virtual desktop to the screen coordinates
          int offsetX, offsetY;
          GEngine->MonitorOffset(offsetX, offsetY);
          xPos -= offsetX;
          yPos -= offsetY;
        }
        // move in-game cursor based on the Windows one
        if (GEngine && GEngine->IsCursorShown())
        {
          if (MouseCanMove)
          {
            // xPos, yPos in client coordinates
            GInput.cursorX = float(xPos)/GEngine->WidthBB();
            GInput.cursorY = float(yPos)/GEngine->HeightBB();
            // Fix: cursorX, cursorY represent only UI area
            AspectSettings as;
            GEngine->GetAspectSettings(as);
            GInput.cursorX = (GInput.cursorX - as.uiTopLeftX) / (as.uiBottomRightX - as.uiTopLeftX);
            GInput.cursorY = (GInput.cursorY - as.uiTopLeftY) / (as.uiBottomRightY - as.uiTopLeftY);
            GInput.cursorX = 2 * GInput.cursorX - 1;
            GInput.cursorY = 2 * GInput.cursorY - 1;
          }
          // there is no guarantee DX cursor is the system one
          // for sure we may want to force DX cursor update
          //GEngine->SetCursor(xxx,x,y,xs,ys);
        }
        else
        {
          // when there is no cursor, ignore any mouse input
          return FALSE;
        }
      }
      break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      if( UseWindow && avoidUserInput == AUIFalse)
      {
        #if ENABLE_KEYMOUSE
          if (PausedByKey)
          {
            AppPause(false,"WM_LBUTTONDOWN");
            PausedByKey = false;
          }
        #endif
      }
    //fallthrough 
    //case WM_LBUTTONDOWN:
    //case WM_RBUTTONDOWN:
    //case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      {
        // move in-game cursor based on the Windows one
        int xPos = (short)lParam;
        int yPos = (short)(lParam>>16);

        // see also WM_MOUSEMOVE
        if (!UseWindow)
        {
          CURSORINFO ptCursor;
          ptCursor.cbSize = sizeof(ptCursor);
          GetCursorInfo( &ptCursor );
          // note: perhaps we should call ScreenToClient here
          // but it seems this call is very unreliable in full-screen
          xPos = ptCursor.ptScreenPos.x;
          yPos = ptCursor.ptScreenPos.y;
          GEngine->UpdateCursorPos(xPos,yPos);
          // translate from the virtual desktop to the screen coordinates
          int offsetX, offsetY;
          GEngine->MonitorOffset(offsetX, offsetY);
          xPos -= offsetX;
          yPos -= offsetY;
        }

        // xPos, yPos in client coordinates
        GInput.cursorX = float(xPos)/GEngine->WidthBB();
        GInput.cursorY = float(yPos)/GEngine->HeightBB();
        // Fix: cursorX, cursorY represent only UI area
        AspectSettings as;
        GEngine->GetAspectSettings(as);
        GInput.cursorX = (GInput.cursorX - as.uiTopLeftX) / (as.uiBottomRightX - as.uiTopLeftX);
        GInput.cursorY = (GInput.cursorY - as.uiTopLeftY) / (as.uiBottomRightY - as.uiTopLeftY);
        GInput.cursorX = 2 * GInput.cursorX - 1;
        GInput.cursorY = 2 * GInput.cursorY - 1;
      }
      break;
    case WM_SIZE: // note: sometimes fall-through to WM_MOVE
      LogF("WM_SIZE %x,%x",wParam,lParam);
      if( wParam==SIZE_MAXIMIZED || wParam==SIZE_RESTORED )
      {
        if( GAppIconic )
        {
          GAppIconic=false;
          Log("Window restored");
          //if( HideCursor ) ShowCursor(FALSE);
          DoRestore=true;
        }
      }
      else if( wParam==SIZE_MINIMIZED )
      {
        if( !GAppIconic )
        {
          GAppIconic=true;
          Log("Window minimized");
          DoPause=true;
          //if( HideCursor ) ShowCursor(TRUE);
        }
        break;
      }
    // fall-through from WM_SIZE
    case WM_MOVE:
      if( UseWindow && GSizeMoveCount<=0)
      {
        ChangeWindowSize(hwnd);
      }
    break;
    #else
    case WM_ACTIVATEAPP:
    case WM_SIZE:
    case WM_MOVE:
      return 0;
    #endif
    
    case WM_SYSCOMMAND:
      //if( !UseWindow )
      //{
      switch( wParam )
      {
        // only a few system menu command are enabled
        case SC_CLOSE: case SC_DEFAULT: case SC_RESTORE:
        case SC_TASKLIST:
          break;
        case SC_MOVE: case SC_SIZE:
          if (UseWindow) break;
          return TRUE;
        // other are disabled and should do nothing
        default:
          //return TRUE;
          break;
        case SC_KEYMENU:
          //if (UseWindow) break;
          return TRUE; // ignore
        case SC_MINIMIZE: case SC_MAXIMIZE:
        //case 
          if (!UseWindow) return TRUE;
          break;
      }
    break;

    case WM_MENUSELECT:
      return 0;
      
    //case WM_MOVING:
    case WM_SIZING:
      if (UseWindow)
      {
        RECT &rc = *(LPRECT) lParam;
        RECT clientRect;
        RECT windowRect;

        GetClientRect(hwnd, &clientRect);
        GetWindowRect(hwnd, &windowRect);
        
        if (GEngine && GEngine->IsInOptionsVideo())
        {
          rc = windowRect;
          return 0;
        }


        int ncH = (windowRect.bottom-windowRect.top)-(clientRect.bottom-clientRect.top);
        int ncW = (windowRect.right-windowRect.left)-(clientRect.right-clientRect.left);

        int newW = rc.right-rc.left-ncW;
        int newH = rc.bottom-rc.top-ncH;

        switch (wParam)
        {
          case WMSZ_BOTTOMLEFT:
          case WMSZ_BOTTOMRIGHT:
          case WMSZ_TOPLEFT:
          case WMSZ_TOPRIGHT:
            // client area aspect must not be changed when dragging corners
            if (GEngine)
            {
              GEngine->KeepAspect(newW,newH);

              //if height or width has been changed so aspect ratio is constant...
              // ...we have to move appropriate corner to correct position
              switch (wParam)
              {
              case WMSZ_BOTTOMRIGHT:
                {
                  rc.right = newW + rc.left + ncW;
                  rc.bottom = newH + rc.top + ncH;
                  break;
                }
              case WMSZ_BOTTOMLEFT:
                {
                  rc.left = rc.right - newW - ncW;
                  rc.bottom = newH + rc.top + ncH;
                  break;
                }
              case WMSZ_TOPRIGHT:
                {
                  rc.right = newW + rc.left + ncW;
                  rc.top = rc.bottom - newH  - ncH;
                  break;
                }
              default: 
                {
                  rc.left = rc.right - newW - ncW;
                  rc.top = rc.bottom - newH  - ncH;
                }
              }
            }
            break;
          default:
            GEngine->SetAspect(newW,newH);
            break;
        }
      }
      break;
    case WM_ENTERSIZEMOVE:
      GSizeMoveCount++;
    // fall through
    case WM_ENTERMENULOOP:
      if (UseWindow)
      {
        LogF("Msg %d:%d,%d",msg,wParam,lParam);
        AppPause(true,"WM_ENTER...");
        break;
      }
      return 0;
    case WM_EXITSIZEMOVE:
      GSizeMoveCount--;
      if (UseWindow)
      {
        ChangeWindowSize(hwnd);
      }
    // fall through
    case WM_EXITMENULOOP:
      if (UseWindow)
      {
        LogF("Msg %d:%d,%d",msg,wParam,lParam);
        AppPause(false,"WM_EXIT...");
        break;
      }
      return 0;

    case WM_INITMENU:
      // no menu
    return TRUE;
    
    case WM_INITMENUPOPUP:
    break;
    
    case WM_CLOSE:
      LogF("WM_CLOSE hwnd=%x",hwnd);
      CanRender=false;
      CloseRequest=true;
      GEngine->StopAll();
    return TRUE;
    case WM_DESTROY:
      LogF("WM_DESTROY hwnd=%x",hwnd);
      AppWinDestroy(hwnd);
      if( ValidateQuit ) 
      {
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
        if ( !AutoTest.IsEmpty() )
          PostQuitMessage(AutoTestExitCode);
        else
#endif
          PostQuitMessage(0);
      }
    break;
    case WM_ERASEBKGND:
      if( !GAppIconic || UseWindow ) return TRUE;
    break;
    case WM_NCPAINT:
      if( !GAppIconic && !UseWindow) return FALSE;
    break;
    case WM_NCACTIVATE:
      if( !GAppIconic && !UseWindow) return TRUE;
    break;
    case WM_NCCREATE:
      if( !GAppIconic && !UseWindow) return TRUE;
    break;
    case WM_PAINT:
      if (UseWindow)
      {
        ValidateRect(hwnd,NULL);
        // we may render while resizing, but we should not resize the backbuffer
        //if (GSizeMoveCount<=0)
        {
          ForceRender = true;
          Log("%d: Paint received",GlobalTickCount());
        }
        /* following code somehow does not do what expected - no rendering
        else
        {
          if( CanRender )
          {
            bool enableDraw=GetEnableDraw();
            if( UseWindow && !GAppActive || !enableDraw )
            {
              RenderFrame(0, enableDraw, enableDraw);
            }
          }
        }
        */
        return FALSE;
      }
      if (!GAppIconic)
      {
        ValidateRect(hwnd,NULL);
        return FALSE;
      }
    break;
    
    case WM_DISPLAYCHANGE:
    return 0;
    
    case WM_INPUT:
      {
        // check the needed size
        unsigned int size = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
        // allocate the buffer
        AutoArray<char, MemAllocLocal<char,256> > buffer;
        buffer.Resize(size);
        // read the data
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.Data(), &size, sizeof(RAWINPUTHEADER));

        RAWINPUT* raw = (RAWINPUT*)buffer.Data();
        // DIAG_MESSAGE(500, Format("Input type: %d (wParam = %d)", raw->header.dwType, wParam));
        switch (raw->header.dwType)
        {
        case RIM_TYPEMOUSE:
#if ENABLE_KEYMOUSE
					ProcessMouseBuffer(raw->data.mouse);
#endif
          break;
        case RIM_TYPEKEYBOARD:
#if ENABLE_KEYMOUSE
					ProcessKeyboardBuffer(raw->data.keyboard);
#endif
          break;
        case RIM_TYPEHID:
          break;
        }
      }
      break;
    case WM_SYSKEYDOWN:
      // capture Alt-F4
      if (!IsAppPaused() && GWorld && !GWorld->IsExitByKeyEnabled())
      {
        if (wParam==VK_F4 && (lParam&(1<<29))!=0) // ingore Alt-F4 when in the game
        {
          return 0;
        }
      }
    case WM_KEYDOWN:
      // WM_KEYDOWN
      if (avoidUserInput == AUIFalse)
      {
        // other processing of keyboard events introduced
/*
        if( GWorld  )
        {
          unsigned repCnt=(lParam&0xffff);
          unsigned flags=(lParam>>16)&0xffff;
          GWorld->DoKeyDown(wParam,repCnt,flags);
        }
*/
        if (UseWindow && LandEditor)
        {
          if (wParam==VK_APPS || wParam==VK_PAUSE)
          {
            PausedByKey = !PausedByKey;
            AppPause(PausedByKey,"PausedByKey");
          }
        }
      }
    break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
      // WM_KEYDOWN
      if (avoidUserInput == AUIFalse)
      {
        // other processing of keyboard events introduced
/*
        if (GWorld)
        {
          unsigned repCnt=(lParam&0xffff);
          unsigned flags=(lParam>>16)&0xffff;
          GWorld->DoKeyUp(wParam,repCnt,flags);
        }
*/
      }
      break;
    case WM_CHAR:
      if (GWorld && avoidUserInput == AUIFalse)
      {
        unsigned repCnt=(lParam&0xffff);
        unsigned flags=(lParam>>16)&0xffff;
        GWorld->DoChar(wParam,repCnt,flags);
      }
      break;
    case WM_IME_CHAR:
      if (GWorld && avoidUserInput == AUIFalse)
      {
        unsigned repCnt = (lParam & 0xffff);
        unsigned flags = (lParam >> 16) & 0xffff;
        unsigned char ch1 = (wParam >> 8) & 0xff;
        unsigned char ch2 = wParam & 0xff;
        if (ch1 >= 0x80 && ch2 >= 0x40)
          GWorld->DoIMEChar(wParam,repCnt,flags);
      }
      break;
    case WM_IME_COMPOSITION:
      if (GWorld && avoidUserInput == AUIFalse)
      {
        GWorld->DoIMEComposition(wParam,lParam);
      }
      break;
  }
  
  if (IsWindowUnicode(hwnd))
    return DefWindowProcW(hwnd,msg,wParam,lParam);
  else
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

#endif

// global messages

#define USER_RELOAD ( WM_APP+0 )
#define USER_CLOSE ( WM_APP+1 )
#define USER_FILE_HANDLE ( WM_APP+2 )
#define USER_MUTEX_HANDLE ( WM_APP+3 )
#define USER_WINDOW_HANDLE ( WM_APP+4 )
#define USER_ANIM_PHASE ( WM_APP+5 )
#define USER_RUN_SCRIPT ( WM_APP+6 )
#define USER_PREVIEW_DIALOG ( WM_APP+7 )


/// Retrieves or sets weather
/**
@param wParam 0xFFFFFFFF to retrieve current weather or float value of new overcast (-1 to no change)
@param lParam if wParam is 0xFFFFFFFF then contain HWND to post result message, otherwise contain float value of new fog (-1 to no change)
@return if wParam set to 0xFFFFFFFF, message posted with wParam = current overcast, lParam = current fog
*/
#define USER_WEATHER     ( WM_APP+16 )

/// Advances time, or retrieves current time
/**
@param wParam (int) delta time to advance in seconds
@param lParam (HWND) window handle to post result - if NULL, no result is posted
@return lParam (DWORD) current time in seconds
*/
#define USER_ADVANCE_TIME     ( WM_APP+17 )

/// Retrieves ambient and diffuse color in COLORREF
/**
@param lParam (HWND) window handle to post result - if NULL, message is dropped
@return wParam (COLORREF) color of ambient, lParam (COLORREF) color of diffuse
*/
#define USER_LIGHT_REQUEST_SHORT   ( WM_APP+18 )

/// 2nd version of reload
/** 
@param wParam Handle of Mutex to lock (handle is closed after use)
@param lParam Handle of Shared memory (handle is closed after use)
*/
#define USER_RELOAD_2 (WM_APP+20)

/// Reloads materials (and other files)
/**no params, no result, updates buldozer's window*/
#define USER_RELOAD_CHANGED_FILES (WM_APP+19)

/// Reloads material specified by name
/** @param lParam Handle of shared memory contains UserReloadMaterialStruct (handle is closed after use)
    @param wParam Handle of Mutex to lock (handle is closed after use)
    @return wParam 1 = operation was successful, 0 = error occured
    */
#define USER_RELOAD_MATERIAL (WM_APP+21)
///This message is sent to last request window, when object has been processed and should be displayed soon
#define USER_RELOAD_DONE (WM_APP+22)

#define USER_RTM_NAME        ( WM_APP+23 )

/// New variant of message for environmental values
/** Usage1
    @param wParam - ID of value (see ValueID enum)
    @param lParam - new value

    Usage2 - retrieve all values
    @param wParam - valGetInfo
    @param lParam - handle of shared memory that contians UserEnvInfoStruct

    Usage3 - set all values
    @param wParam - valSetInfo
    @param lParam - handle of shared memory that contains UserEnvInfoStruct
*/
#define USER_ENVIRONMENT_INFO (WM_APP+24)

/// Sets animation control
/**
  Usage1:
  @param wParam - not used;
  @param lParam - handle of shared memory that contains UserAnimationControlStruct
  */
#define USER_ANIMATION_CONTROL (WM_APP+25)

#if defined(_WIN32) 
struct UserReloadSharedMemStruct
{
  HWND  notify; ///<handle to signal Objektiv2, that data has been taken over
  float LodBias;   ///<new LodBias value
  float animPhase; ///<current animation phase to display
  size_t datasize; ///<size of data
  char data[1];    ///<ObjectData
};

struct UserReloadMaterialStruct
{
  HWND notify; ///<handle to signal Objektiv2, that data has been taken over
  unsigned long matNameOffset; ///<offset in data block containing material name
  unsigned long dataOffset; ///<offset in data block containing material data
  size_t dataSize; ///<size of material data;
  char data[1]; ///<data block
};

struct UserEnvInfoStruct
{
  enum ValueID
  {
    valGetInfo=0,
    valSetInfo=1,
    valOvercast=2,
    valFog=3,
    valEyeAccom=4,
    valTime=5
  };
  HANDLE unblock;
  int version;
  float overcast;
  float fog;
  float wind;
  float eyeAccom;
  unsigned long time;
  float ambient[3];
  float diffuse[3];
};

struct UserAnimationControlStruct
{
  HANDLE unblock;
  int version;
  float value;
  float minval;
  float maxval;
  char controlName[1];
};

#endif

//#define SHARED_LEN ( 128*1024 ) // objects are usually max 32 KB

//bool CloseFile; // command: close application
char *ObjDataBuf = NULL; // local copy of data
int ObjDataLen;          // size of local data
float DoSetPhase=-1;

RString RTMName;
bool DoSetRTMName = false;

RString ObjDataName;

#ifdef _WIN32
// shared memory implementation

static HANDLE FileMutex=NULL;
static HWND ObjektivWindow=NULL;
static HANDLE FileMap=NULL;
#endif

static void CloseSharing()
{
#ifdef _WIN32
  if( FileMap ) CloseHandle(FileMap),FileMap=NULL;
  if( FileMutex ) CloseHandle(FileMutex),FileMutex=NULL;
#endif
}

void UnlockSharedData()
{
  if( ObjDataBuf ) delete[] ObjDataBuf,ObjDataBuf=NULL,ObjDataLen=0;
  ObjDataName = RString();
}


void LockSharedData( int size )
{
  #if defined(_WIN32) 
  UnlockSharedData();
  if( !FileMap || !FileMutex )
  {
    WarningMessage("Handles not inherited.");
  }
  else if( WaitForSingleObject(FileMutex,2000)==WAIT_OBJECT_0 )
  {
    void *memMap=::MapViewOfFile(FileMap,FILE_MAP_READ,0,0,0); // map whole file
    if (memMap)
    {
      ObjDataBuf=new char[size];
      memcpy(ObjDataBuf,memMap,size);
      ObjDataLen=size;
      ObjDataName = RString();
      ::UnmapViewOfFile(memMap);
      ForceRender=true;
      Log("%d: Update received",GlobalTickCount());
    }
    ReleaseMutex(FileMutex);
  }
  else
    WarningMessage("Timeout during waiting for data");
  #endif
}

static void LockSharedData2( HANDLE mutex, HANDLE shared)
{
#if defined(_WIN32) 
  int res=WaitForSingleObject(mutex,10000);
  if (res!=WAIT_TIMEOUT) 
  {
    if (res==WAIT_OBJECT_0)
    {
      const UserReloadSharedMemStruct *data=reinterpret_cast<const UserReloadSharedMemStruct *>(::MapViewOfFile(shared,FILE_MAP_READ,0,0,0));
      if (data!=NULL)
      {
        if (data->notify) PostMessage(data->notify,USER_RELOAD_2,0,0);
        ObjDataBuf=new char[data->datasize];
        memcpy(ObjDataBuf,data->data,data->datasize);
        ObjDataLen = data->datasize;
        //P3D full qualified filename
        ObjDataName =data->data+data->datasize;
        DoSetPhase=data->animPhase;
        //Glob.config.lodCoef=data->LodBias;
        ObjektivWindow=data->notify;
        ::UnmapViewOfFile((LPCVOID)data);    
        ForceRender=true;
        Log("%d: Update 2 received (%s)",GlobalTickCount(), ObjDataName.Data());        
      }
      ReleaseMutex(mutex);
    }
    else
    {WarningMessage("Lock object failed");}
  }
  else
  {WarningMessage("Timeout during waiting for data");}
  CloseHandle(mutex);
  CloseHandle(shared);
#endif
}

#if defined(_WIN32) 
static void ReloadMaterialFromObjektiv(HANDLE mutex, HANDLE shared)
{
  bool ReloadMaterialFromMemory(const char *matName, void *data, size_t datasize);

  int res=WaitForSingleObject(mutex,10000); //only 10 seconds will buldozer wait, to release memory
  if (res!=WAIT_TIMEOUT)      //if not timeout
  {
    if (res==WAIT_OBJECT_0)   //if mutex has been acquired
    {
      //acquire material information block
      const UserReloadMaterialStruct *matinfo=reinterpret_cast<const UserReloadMaterialStruct *>(::MapViewOfFile(shared,FILE_MAP_READ,0,0,0));
      bool succ=matinfo!=NULL;   //mark success, if information block has been successfully acquired
      if (succ) 
        succ=ReloadMaterialFromMemory(
        matinfo->data+matinfo->matNameOffset, 
        (void *)(matinfo->data+matinfo->dataOffset),
        matinfo->dataSize);
      ForceRender=true;
      PostMessage(matinfo->notify,USER_RELOAD_MATERIAL,succ,0); //post objektiv result
      UnmapViewOfFile((void *)matinfo);         //release shared memory
      ReleaseMutex(mutex);                      //release mutex
      Log("%d: MAT reload received",GlobalTickCount());
    }
  }
  CloseHandle(mutex); //close handles
  CloseHandle(shared);
}
 #endif

/*static void SyncObjektiv()
{
  #if defined(_WIN32) 
  float time=GWorld->GetViewerPhase();
  if( ObjektivWindow )
  {
    // send message to Objektiv
    int iTime=time*toIntFloor(65536);
    //WarningMessage("Send message %d to %x",iTime,ObjektivWindow);
    PostMessage(ObjektivWindow,USER_ANIM_PHASE,0,iTime);
  }
  #endif
}
*/

/*!
\patch 1.12 Date 08/06/2001 by Ondra
- Added: MP, Anticheat: Exe integrity verification.
*/

#if defined (_WIN32)

unsigned int CalculateExeCRC(int offset, int size)
{
//((EXE_CRC_CHECK
  #if defined(_WIN32) 
  HMODULE hMod = GetModuleHandle(NULL);

  // get exe header from PE
  PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;
  #else
  HMODULE hMod = (HMODULE)0x400000;
  PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)0x400000;
  #endif

  // From the DOS header, find the NT (PE) header
  PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)((char *)hMod + pDosHdr->e_lfanew);

  int checkStart = offset;
  int checkEnd = offset + size;

  PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHdr);
  for (WORD i=0; i<pNtHdr->FileHeader.NumberOfSections; i++)
  {
    if ((pSection->Characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA)) != 0)
    {
      // section containing code or data
      int sectionStart = (int)hMod + pSection->PointerToRawData;
      int sectionSize = pSection->SizeOfRawData;
      int sectionEnd = sectionStart + sectionSize;
      if (checkStart < sectionEnd && checkEnd > sectionStart)
      {
        // non-empty intersection - calculate the CRC from this segment
        saturate(checkStart, sectionStart, sectionEnd);
        saturate(checkEnd, sectionStart, sectionEnd);

        LogF("%x:%x, testing %x:%x", sectionStart, sectionSize, checkStart, checkEnd - checkStart);
        CRCCalculator crc;
        crc.Reset();
        crc.Add((void *)checkStart,checkEnd-checkStart);
        return crc.GetResult();
      }
    }
    pSection++;
  }

  // segment containing the given block not found, calculate the CRC from the whole first segment
  pSection = IMAGE_FIRST_SECTION(pNtHdr);
  int sectionStart = (int)hMod + pSection->PointerToRawData;
  int sectionSize = pSection->SizeOfRawData;

  LogF("%x:%x, testing %x:%x",sectionStart,sectionSize,sectionStart,sectionSize);
  CRCCalculator crc;
  crc.Reset();
  crc.Add((void *)sectionStart, sectionSize);
  return crc.GetResult();
//))EXE_CRC_CHECK
}
#else
// Dummy implementation only to make linux dedicated server  compilable
unsigned int CalculateExeCRC(int offset, int size)
{
  return 0;
}
#endif

unsigned int CalculateDataCRC(const char *path, int offset, int size)
{
  // reject files not inside current directory
  if (!IsRelativePath(path)) return 0;
  QIFStreamB in;
  in.AutoOpen(path);
  if (in.fail()) return 0;
  int endCheck = offset+size;
  saturate(offset,0,in.rest());
  saturate(endCheck,offset,in.rest());

  CRCCalculator crc;
  crc.Reset();
  in.copy(crc);
  //crc.Add(in.act()+offset,endCheck-offset);
  return crc.GetResult();
}

unsigned int CalculateConfigCRC(const char *path);

#if _ENABLE_REPORT
  
  /// function called once the world is fully initalized
  /**
  Can be used to debug or profile almost any code using world.
  */
  void DebugOpportunityAfterInit()
  {
    #if 0
    Ref<LODShape> lShape = Shapes.New("data3d\\ural.p3d",ShapeReversed|ShapeShadow);
    bool done = false;
    while (!done)
    {
      int level = 0;
      // load
      {
        SectionTimeHandle section = StartSectionTime();
        
        ShapeUsed shape = lShape->Level(level);
        float timeSpent = GetSectionTime(section);
        GScene->AddUseLevelTimeSpent(timeSpent);
        if (timeSpent>0.001)
        {
          LogF("Time spent: %.3f,%s:%d",timeSpent,cc_cast(lShape->GetName()),level);
        }
      }
      // unload
      Shapes.FreeOneItemShape(lShape,level);
      #if 0
        FreeOnDemandReleaseSlot("ShapeLevels",INT_MAX);
        FreeOnDemandReleaseSlot("TexCache",INT_MAX);
        FreeOnDemandReleaseSlot("Materials",INT_MAX);
      #endif
    }
    #endif
  }
#endif


// global viewer properties


#if defined(_WIN32) 
static void ProcessUserMessages( MSG &msg )
{
  Log("%d: User message %d",GlobalTickCount(),msg.message-WM_APP);
switch (msg.message)
  {

  case USER_CLOSE :CloseRequest=true;break;
  case USER_RELOAD:
    LockSharedData(msg.lParam);
    //Glob.config.lodCoef=msg.wParam*0.01f;
    PostMessage(ObjektivWindow,USER_RELOAD,0,msg.lParam);
    break;
  case USER_ANIM_PHASE:
    DoSetPhase=msg.lParam*(1.0/65536);
    break;
  case USER_WINDOW_HANDLE:
    ObjektivWindow=(HWND)msg.lParam;
    break;
  case USER_FILE_HANDLE:
    if ((HANDLE)msg.lParam!=FileMap)
      {
      CloseHandle(FileMap);
      FileMap=(HANDLE)msg.lParam;
      }
    break;
  case USER_MUTEX_HANDLE:
    if ((HANDLE)msg.lParam!=FileMutex)
      {
      CloseHandle(FileMutex);
      FileMutex=(HANDLE)msg.lParam;
      }
    break;
  case USER_RUN_SCRIPT:
    {
      char buffer[MAX_PATH];
      GlobalGetAtomName((ATOM)msg.lParam, buffer, MAX_PATH);
      GlobalDeleteAtom((ATOM)msg.lParam);

      Script *script = new Script(buffer, GameValue(), GWorld->GetMissionNamespace()); // mission namespace
      GWorld->AddScript(script, false);
      ReplyMessage(USER_RUN_SCRIPT);
    }
    break;
  case USER_RTM_NAME:
    {
      char buffer[MAX_PATH];
      if (GlobalGetAtomName((ATOM)msg.lParam, buffer, MAX_PATH) != 0)
      {
        GlobalDeleteAtom((ATOM)msg.lParam);
        RTMName = RString(buffer);
      }
      else
      {
        RTMName = RString("");
      }
      DoSetRTMName = true;
    }
    break;
  case USER_PREVIEW_DIALOG:
    {
      static const int bufferSize = 512;
      char buffer[bufferSize];
      GlobalGetAtomName((ATOM)msg.lParam, buffer, bufferSize);
      GlobalDeleteAtom((ATOM)msg.lParam);

      const char *ptr = strchr(buffer, ';');
      if (ptr)
      {
        RString filename(buffer, ptr - buffer);
        RString resource(ptr + 1);
  
        GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

        ParamFile res;
        res.Parse(filename, NULL, NULL, &globals);
        GWorld->DestroyUserDialog();
        AbstractOptionsUI *CreateUserDialog(ParamEntryVal cls);
        GWorld->SetUserDialog(CreateUserDialog(res >> resource));
      }
    }
    break;
  case USER_LIGHT_REQUEST_SHORT:
    {
      HWND toWindow=(HWND)msg.lParam;
      if (toWindow) 
        {
        LightSun *ls= GScene->MainLight();    
        Color c1= ls->GetAmbient();
        Color c2= ls->GetDiffuse();
        PostMessage(toWindow, USER_LIGHT_REQUEST_SHORT,
          RGB(toInt(c1.R()*255.0f),toInt(c1.G()*255.0f),toInt(c1.B()*255.0f)),
          RGB(toInt(c2.R()*255.0f),toInt(c2.G()*255.0f),toInt(c2.B()*255.0f)));
        }
    }
    break;
  case USER_ADVANCE_TIME:
    {  
      float delta=(int)msg.wParam*OneSecond;
      HWND toWindow=(HWND)msg.lParam;    
      if (delta!=0.0f)
      {
        Glob.clock.AdvanceTime(delta);
				Glob.clockWeather.AdvanceTime(delta);

        GScene->MainLight()->Recalculate();
        GScene->MainLightChanged();
        GLandscape->OnTimeSkipped();
        ForceRender=true;
        Log("%d: advance time  received",GlobalTickCount());
      }
      if (toWindow)
      {
        float t= Glob.clock.GetTimeOfDay();
        PostMessage(toWindow, USER_ADVANCE_TIME, 0, toLargeInt(t*24*60*60));    
      }
    }
    break;
  case USER_WEATHER:
    {
      float overcast;
      float fog;
      float wind;
      if (msg.wParam==0xFFFFFFFF)
      {
        GWorld->GetWeather(overcast,fog,wind);
        if (msg.lParam) PostMessage((HWND)msg.lParam,USER_WEATHER,*(WPARAM *)&overcast,*(LPARAM *)&fog);
      }
      else
      {
        overcast=*(float *)&msg.wParam;
        fog=*(float *)&msg.lParam;
        GWorld->SetWeather(overcast,fog,-1,-1);
        ForceRender=true;
        Log("%d: weather received",GlobalTickCount());
      }
    }
  case USER_RELOAD_2:
    {
      HANDLE shared=(HANDLE)msg.lParam;
      HANDLE mutex=(HANDLE)msg.wParam;
      LockSharedData2(mutex,shared);
    }
    break;
  case USER_RELOAD_MATERIAL:
    {
    HANDLE shared=(HANDLE)msg.lParam;
    HANDLE mutex=(HANDLE)msg.wParam;
    ReloadMaterialFromObjektiv(mutex,shared);
    }    break;
  case USER_ENVIRONMENT_INFO:
    {
      UserEnvInfoStruct::ValueID valId=(UserEnvInfoStruct::ValueID)msg.wParam;
      float fValue=*reinterpret_cast<float *>(&msg.lParam);
      unsigned long lValue=msg.lParam;

      switch (valId)
      {
      case UserEnvInfoStruct::valEyeAccom:
        GWorld->SetEyeAccom(fValue);ForceRender=true;break;
      case UserEnvInfoStruct::valFog:
        GWorld->SetWeather(-1,fValue,-1,-1);ForceRender=true;break;
      case UserEnvInfoStruct::valOvercast:
        GWorld->SetWeather(fValue,-1,-1,-1);ForceRender=true;break;
      case UserEnvInfoStruct::valTime:
        {        
          float delta=(long)lValue*OneSecond;
          if (delta!=0.0f)
          {
            Glob.clock.AdvanceTime(delta);
						Glob.clockWeather.AdvanceTime(delta);

            GScene->MainLight()->Recalculate();
            GScene->MainLightChanged();
            GLandscape->OnTimeSkipped();
            ForceRender=true;
          }
       }
        break;
      case UserEnvInfoStruct::valGetInfo:
        {        
          UserEnvInfoStruct *data=(UserEnvInfoStruct *)MapViewOfFile((HANDLE)msg.lParam,FILE_MAP_WRITE,0,0,0);
          if (data)
          {
            LightSun *ls= GScene->MainLight();
            Color c= ls->GetAmbient();
            data->ambient[0]=c.R();data->ambient[1]=c.G();data->ambient[2]=c.B();
            c= ls->GetDiffuse();
            data->diffuse[0]=c.R();data->diffuse[1]=c.G();data->diffuse[2]=c.B();            
            GWorld->GetWeather(data->overcast,data->fog,data->wind);
            data->eyeAccom=GWorld->GetCustomEyeAccom();
            float t= Glob.clock.GetTimeOfDay();
            data->time=toLargeInt(t*24*60*60);
            data->version=1;
            SetEvent(data->unblock);
            CloseHandle(data->unblock);
            UnmapViewOfFile(data);
          }
          CloseHandle((HANDLE)msg.lParam);
        }
        break;
      case UserEnvInfoStruct::valSetInfo:
        {
          UserEnvInfoStruct *data=(UserEnvInfoStruct *)MapViewOfFile((HANDLE)msg.lParam,FILE_MAP_READ,0,0,0);
          if (data)
          {
            GWorld->SetWeather(data->overcast,data->fog,data->wind,-1);
            GWorld->SetEyeAccom(data->eyeAccom);
       
						float t= Glob.clock.GetTimeOfDay()*OneDay;
            Glob.clock.AdvanceTime(data->time*OneSecond-t);

            t = Glob.clockWeather.GetTimeOfDay()*OneDay;
						Glob.clockWeather.AdvanceTime(data->time*OneSecond-t);

            GScene->MainLight()->Recalculate();
            GScene->MainLightChanged();
            GLandscape->OnTimeSkipped();
            ForceRender=true;
            SetEvent(data->unblock);
            CloseHandle(data->unblock);
            UnmapViewOfFile(data);
          }
          CloseHandle((HANDLE)msg.lParam);
        }
        break;
      }
    }
    break;
  case USER_ANIMATION_CONTROL:
#if _ENABLE_BULDOZER
    {
      UserAnimationControlStruct *data=
        (UserAnimationControlStruct *)MapViewOfFile((HANDLE)msg.lParam,FILE_MAP_ALL_ACCESS,0,0,0);
      if (data)
      {
        GWorld->SetViewerController(RString(data->controlName),data->value,data->minval,data->maxval);
        SetEvent(data->unblock);
        CloseHandle(data->unblock);
        UnmapViewOfFile(data);
        ForceRender=true;
      }
      CloseHandle((HANDLE)msg.lParam);
    }
#endif
    break;
  }
}
#endif

#if _ENABLE_PERFLOG

#include "timeManager.hpp"
extern float LastDeltaT;

//@{ logging using diag_logSlowFrame
bool FpsCap;
RString PerfCapSection;
float PerfCapLimit;

void SetPerfCapLimit(RString section, float limit)
{
  PerfCapSection = section;
  PerfCapLimit = limit;
}
//@}


void PerfLogFrame(float frameDuration, float limitSlowFrame, float limitBottleneck)
{
  //_frameTime=GlobalTickCount();

  #if _ENABLE_REPORT && _ENABLE_CHEATS || _ENABLE_PERFLOG
    GPerfProfilers.CaptureToCounters();
    
    DEF_COUNTER_P_EX(total,*,PROF_COUNT_SCALE);
    frameDuration = COUNTER_P_NAME(total).Slot()->value/COUNTER_P_NAME(total).Slot()->scale*0.00001f;
  #endif

  float fps=0;
  if (frameDuration>0.001) fps=1/frameDuration;
  else fps = 1000;

  #if (_PROFILE || _ENABLE_CHEATS) && !_DEBUG // debug is always too slow, no sense to capture anything in it
  static bool PerfCap = true;
  #else
  static bool PerfCap = false;
  #endif
  static bool PerfBotCap = false;

  #if _ENABLE_CHEATS
  if (GInput.GetCheatXToDo(CXTPerfCap))
  {
    PerfCap = !PerfCap;
    GlobalShowMessage(500,"PerfCap %s",PerfCap ? "On":"Off");
  }
  if (GInput.GetCheatXToDo(CXTPerfBotCap))
  {
    PerfBotCap = !PerfBotCap;
    GlobalShowMessage(500,"Perf Bottleneck Cap. %s",PerfBotCap ? "On":"Off");
  }
  if (GInput.GetCheatXToDo(CXTFpsLog))
  {
    FpsCap = !FpsCap;
    GlobalShowMessage(500,"FpsCap %s",FpsCap ? "On":"Off");
  }
  #endif

  if (PerfCap && GEngine)
  {
    // check "total" counter
    // if some frame is much slower than the average, log values
    int avgMs = GEngine->GetAvgFrameDuration();
    if (frameDuration>avgMs*0.0015f && frameDuration>limitSlowFrame*0.7f || frameDuration>limitSlowFrame)
    {
      #if _PROFILE && USER_Ondra
        // Ondra is debugging multiplayer with multiple clients, Slow frame detected: cluttering debug output
      #else
        #define SLOW_FRAME 1
      #endif
      #if SLOW_FRAME
      #if _ENABLE_REPORT
      float addTime = GTimeManager.GetAddTimeThisFrame();
      float addTimeRest = GTimeManager.GetAddTimeRestThisFrame();
      LogF(
        "WARN: Slow frame detected: %.0f ms (add time %.0f ms, spent %.0f ms), deltaT %.0f ms",
        frameDuration * 1000, addTime * 1000, (addTime - addTimeRest) * 1000, LastDeltaT * 1000
      );
      #endif
      #if _RELEASE
      if (frameDuration>0.5f)
      {
        LogF("*** extremely slow frame ***");
      }
      else if (frameDuration>0.2f)
      {
        LogF("*** very slow frame ***");
      }
      #endif
      int start = GlobalTickCount();
      LogF(GPerfProfilers.Diagnose(true));
      LogF(GPerfCounters.Diagnose());
      int duration = GlobalTickCount()-start;
      if (duration>10)
      {
        LogF("INFO: Diagnose was slow - %d ms",duration);
      }
      #endif
    }
    else
    {
    }
  }
  if (PerfCapSection.GetLength() > 0)
  {
    // we are interested in existing counters only
    int index = GPerfProfilers.Find(PerfCapSection);
    if (index >= 0)
    {
      int value = GPerfProfilers.CurrentValue(index);
      if (0.00001f * value >= PerfCapLimit)
      {
        LogF("WARN: Slow %s frame detected: %.01f ms", (const char *)PerfCapSection, 0.01 * value);
        LogF(GPerfProfilers.Diagnose(true));
        LogF(GPerfCounters.Diagnose());
      }
    }
  }
  if (PerfBotCap)
  {
    float timePerFrame;
    if (GEngine->CanVSyncTime())
    {
      timePerFrame = GEngine->GetAvgFrameDurationVsync()/GEngine->RefreshRate();
    }
    else
    {
      timePerFrame = GEngine->GetAvgFrameDuration(Engine::NFrameDurations);
    }

    int ms100 = COUNTER_P_NAME(total).Slot()->smoothValue;
    if (ms100>limitBottleneck*100000 && !GPerfProfilers.AvgAlreadyDiagnosed())
    {
      LogF("WARN: Perf. bottleneck detected: (avg. %.0f ms)",ms100*0.01f);
      GPerfProfilers.DiagnoseAvg(true);
    }
  }
  
  if (FpsCap)
  {
    LogF(
      "fps %.3f (%.0f ms), deltaT %.0f ms, simulation %s",
      fps, frameDuration*1000, LastDeltaT*1000,
      GWorld && GWorld->IsSimulationEnabled() ? "enabled" : "disabled"
    );
  }

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  if (frameDuration >= 0.001) // ignore empty reports (Reset etc.)
  {
    void AutotestFrame(float duration, RString log1, RString log2);
    AutotestFrame(frameDuration, GPerfProfilers.Diagnose(true, 15), /*GPerfCounters.Diagnose()*/ RString());
  }
#endif

  GPerfCounters.Frame(fps, frameDuration, Engine::NFrameDurations);
  GPerfProfilers.Frame(fps, frameDuration, Engine::NFrameDurations);
    }

/// pretend a frame completed, and reset all frame diagnostics
void PerfLogReset()
{
  PerfLogFrame(0,FLT_MAX,FLT_MAX);
  // make
  GPerfProfilers.Reset();
  GPerfCounters.Reset();
}


#endif

#ifdef _WIN32

#if _SPACEMOUSE  //interface for SpaceMouse, used in ProcessMessages()
  #include "spacemouse.hpp"
#endif

static void ProcessMessages( bool noWait )
{
  if ( AutoUpdate )
  {
    GSvnAUC.HandleAskToShutDown();
    if ( GSvnAUC.ShutDowning() )
    {
      // restart me after autoUpdate is over
      if (GSvnAUC.RestartAfterShutDown()) GSvnAUC.Send(CmdRestartMe.c_str());

      AutoUpdate = false;
      DDTerm();
      // note: asserting does not work now
      AppUnpause();
      RptF("Exit: forced by AutoUpdater");
      exit(0);
    }
  }

  avoidUserInput = noWait ? AUITrue : AUIFalse;
  I_AM_ALIVE();
  #if _ENABLE_REPORT
  GDebugConsole->HandleCommands();
  #endif
  // if (GProgress) GProgress->Refresh(); - avoid recursion
  {
    PROFILE_SCOPE_EX(winMs,*);
    MSG msg;
    //GDebugger.ProcessAlive();
    int begRange = 0;
    int endRange = 0;
    if (noWait)
    {
      // noWait means limited message processing
      // avoid processing user messages in such context
      endRange = WM_APP-1;
    }
    while( PeekMessage(&msg, 0, begRange, endRange, PM_REMOVE) )
    {
      //Log("%d: Message %d",GlobalTickCount(),msg.message);

    #if _SPACEMOUSE
      bool handled = false;
      if(SpaceMouseDev)
      {
        handled = SpaceMouseDev->CheckMessage(msg, GInput.spaceMouseAxis, GInput.spaceMouseButtons);    //process further if it's not a spaceMouse message
      }
      if(!handled) //the Msg has not already been processed by Spacemouse device
    #endif
      
      if( msg.message>=WM_APP && msg.message<WM_APP+1024 )
      {
        if (LandEditor)
        {
          ProcessUserMessages(msg);
        }
        else
        {
          LogF("WM_APP+%d (%x) discarded",msg.message-WM_APP,msg.message);
        }
        
      }
      else if( msg.message==WM_QUIT )
      {
        Assert( ValidateQuit );
        #if PROFILE_EXIT
          EnableProfiler();
        #endif
        DDTerm();
        #if PROFILE_EXIT
          DisableProfiler();
        #endif
        // note: asserting does not work now
        AppUnpause();
        //if( HideCursor ) ShowCursor(TRUE);
        LogF("Exit %d",msg.wParam);
        //Sleep(500);
        exit(msg.wParam);
      }
      else
      {
#if _GAMES_FOR_WINDOWS
        if (!XLivePreTranslateMessage(&msg))
#endif
        {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
      }
      if (CanRender)
      {
        if( GEngine && DoRestore )
        {
          GLOB_ENGINE->Restore();
          DoRestore=false;
        }
        if( GEngine && DoPause )
        {
          GLOB_ENGINE->Pause();
          DoPause=false;
        }
        if (GWorld && hwndApp)
        {
          RString playerName;
          int playerId = GetNetworkManager().GetPlayer();
          if (playerId>0)
          {
            const PlayerIdentity *ident = GetNetworkManager().FindIdentity(playerId);
            if (ident) playerName = ident->GetName();
          }
          SMaintainTitle.Update(GetNetworkManager().IsServer(),GetNetworkManager().IsGameMaster(),playerName);
        }
#if _ENABLE_BULDOZER
        if( GLandscape && ObjDataBuf )
        {
          Debugger::PauseCheckingScope scope(GDebugger);
          // some data reload required:
          // recreate the viewed object
          //HWND wnd=CreateWindow("STATIC","Updating...",WS_CHILD|WS_VISIBLE|SS_CENTER |SS_CENTERIMAGE |WS_BORDER,50,50,150,50,msg.hwnd,NULL,GetModuleHandle(NULL),NULL);
          HCURSOR oldCursor = SetCursor(LoadCursor(NULL,IDC_WAIT));
          //UpdateWindow(wnd);
          GWorld->ReloadViewer(ObjDataBuf,ObjDataLen,"house",ObjDataName);
          // destroy temporary data
          UnlockSharedData();
          CheckForChangedFiles();
          GFileServerFlush();
          ForceRender=true;        
          Log("%d: viewer reloaded",GlobalTickCount());
          
          //Notify Objektiv, that object should be displayed
          if (ObjektivWindow) PostMessage(ObjektivWindow,USER_RELOAD_DONE,0,0);
          SetCursor(oldCursor);
          //DestroyWindow(wnd);
        }
        if (GWorld)
        {
          if (DoSetPhase >= 0)
          {
            GWorld->SetViewerPhase(DoSetPhase);
            DoSetPhase=-1;
            ForceRender=true;
            Log("%d: Phase change received",GlobalTickCount());
          }
          if (DoSetRTMName)
          {
            GWorld->SetViewerRTMName(RTMName);
            ForceRender=true;
            DoSetRTMName = false;
            Log("%d: Animation change received",GlobalTickCount());
          }
        }
#endif
      }
    }
  }


  if( !noWait )
  {
    #if !_DISABLE_GUI
    if( AppIdle() )
    #endif
    {
      //Log("%d: wait 2000 ms",GlobalTickCount());
      PROFILE_SCOPE_EX(limFR,*);
      ::MsgWaitForMultipleObjects(0,NULL,FALSE,2000,QS_ALLEVENTS);
      //WaitMessage();
    }
  }
  }
#endif

bool EnableLowFpsDetection()
{
  // if there is any progress bar, do not detect low fps
  if (GProgress) return false;
  // if simulation or display is not enabled, do not detect low fps
  return GWorld && GWorld->IsSimulationEnabled() && GWorld->IsDisplayEnabled();
}

/*!
  \patch 1.01 Date 06/19/2001 by Jirka
  - Fixed: keyboard input disabled during mission loading
  \patch_internal 1.01 Date 06/19/2001 by Jirka
  - function removes user input messages from windows queue
*/
void RemoveInputMessages()
{   
  #if defined(_WIN32) 
  MSG msg;
  while (PeekMessage(&msg, 0, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {}
  #endif
}

RString GetExtCfgFile();

#define CHECK_STRINGTABLE 0

#if CHECK_STRINGTABLE
void ReportStringtable(QOFStream &out, RString filename)
{
  out << filename << "\r\n";

  StringTableDynamic table;
  table.Load(filename);

  for (int i=0; i<table.NTables(); i++)
  {
    for (int j=0; j<table.GetTable(i).Size(); j++)
    {
      StringTableItem2 item = table.GetTable(i)[j];
      RString name = item.name;
      RString value = item.value;

      bool found = false;
      for (int k=0; k<value.GetLength(); k++)
      {
        unsigned char c = (unsigned char)value[k];
        if (c >= 0x80)
        {
          found = true;
          char buffer[16];
          sprintf(buffer, "0x%x%x ", c / 0x10, c % 0x10);
          out << buffer;
        }
      }
      if (found)
      {
        out << item.name;
        for (int k=0; k<50-item.name.GetLength(); k++) out << " ";
        out << item.value << "\r\n";
      }
    }
  }
  out << "\r\n";
}

void FindStringtables(QOFStream &out, const char *dir)
{
  char buffer[512];
  sprintf(buffer, "%s\\*.*", dir);

  WIN32_FIND_DATA info;
  HANDLE h = FindFirstFile(buffer, &info);
  if (h != INVALID_HANDLE_VALUE)
  {
    do
    {
      if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
      {
        if (info.cFileName[0] != '.')
        {
          sprintf(buffer, "%s\\%s", dir, info.cFileName);
          FindStringtables(out, buffer);
        }
      }
      else if (stricmp(info.cFileName, "stringtable.csv") == 0)
      {
        sprintf(buffer, "%s\\%s", dir, info.cFileName);
        ReportStringtable(out, buffer);
      }
    }
    while (FindNextFile(h, &info));
    FindClose(h);
  }
}

void CheckStringtable(RString language)
{
  GLanguage = language;

  QOFStream out;
  out.open(RString("C:\\check.") + language + ".txt");
  
  ReportStringtable(out, "bin\\stringtable.csv");
  FindStringtables(out, "anims");
  FindStringtables(out, "campaigns");
  FindStringtables(out, "missions");
  FindStringtables(out, "mpmissions");

  out.close();
}
#endif


/*!
\patch_internal 1.20 Czech version Date 09/03/2001 by Jirka
- Added: Enable Polish language at _CZECH configuration
\patch_internal 1.35 Date 12/11/2001 by Jirka
- Added: Check if CD is inserted in Czech version
\patch 1.40 Date 12/20/2001 by Jirka
- Fixed: fix due to bug in resource of Polish Preferences
*/

#ifdef _WIN32

void PostDestroy()
{
  PostMessage((HWND)hwndApp,WM_DESTROY,0,0);
}

#endif

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER || _DEDICATED_CLIENT

#ifdef _WIN32
HWND consoleEditHWnd = NULL;
HWND splashProgress[10]; // ten checkboxes;
int progressSet;
#endif

/*!
  \patch 1.92 Date 8/8/2003 by Pepca
  - Improved: Flushing of stdout (for instant console output) on Linux.
  \patch 5088 Date 11/06/2006 by Ondra
  - Fixed: Unicode used for dedicated server console output.
  \patch 5160 Date 5/18/2007 by Bebul
  - New: Console output of dedicated servers can be logged to file specified in the server config entry logFile.
*/

int VConsoleF(const char *format, va_list argptr)
{
#ifdef _WIN32
#if !_DEDICATED_CLIENT
  if (!DoCreateDedicatedServer && !IsDedicatedClient()) return 0;
#endif

  BString<512> buf;

  SYSTEMTIME syst;
  GetLocalTime(&syst);
  sprintf
    (
    buf, "%2d:%02d:%02d ",
    syst.wHour, syst.wMinute, syst.wSecond
    );

  BString<512> buf2;
  buf2.VPrintF(format, argptr);
  buf += buf2;

#ifdef NET_LOG
  NetLog("%s", (const char*)buf);
#endif
#if !_DEDICATED_CLIENT
  FILE *LogFile=GetNetworkManager().GetDedicatedServerLogFile();
  if (LogFile)
  {
    fprintf(LogFile, "%s\n", buf.cstr());
    // fflush(LogFile); // we cannot flush so often. It is flushed on end of mission.
  }
#endif

  strcat(buf,"\r\n");

  if (consoleEditHWnd)
  {
    // output to dialog window
    PostMessageW(consoleEditHWnd,EM_SETSEL,INT_MAX,INT_MAX);
    {
      SETTEXTEX wParamOUT;
      LRESULT lResult;
      wParamOUT.codepage     = CP_UTF8;
      wParamOUT.flags        = ST_SELECTION;
    
      lResult = SendMessageW(consoleEditHWnd,EM_SETTEXTEX,(WPARAM) &wParamOUT,(LPARAM) buf.cstr());
    }

    #if 0 // TODO: fix the following partial and broken implementation
    // now limit the size to avoid excessive memory usage
    #if _DEBUG || _PROFILE
    const int maxChars = 50;
    #else
      const int maxChars = 20*1024;
    #endif
    for(;;)
    {
      GETTEXTLENGTHEX wParamLen;
      LRESULT lResult;
      wParamLen.codepage = CP_UTF8;
      wParamLen.flags     = GTL_CLOSE|GTL_NUMCHARS;
      lResult = SendMessageW(consoleEditHWnd, EM_GETTEXTLENGTHEX, (WPARAM) &wParamLen, NULL);
      if (lResult<maxChars) break;
    }
    #endif

    PostMessageW(consoleEditHWnd,EM_SETSEL,INT_MAX,INT_MAX);
    PostMessageW(consoleEditHWnd,EM_SCROLLCARET,0,0);
  }

#else

  time_t now;
  time(&now);
  struct tm *now_tm = localtime(&now);
  printf("%2d:%02d:%02d ",now_tm->tm_hour,now_tm->tm_min,now_tm->tm_sec);

  vprintf( format, argptr );
  putchar('\n');
  fflush(stdout);

  FILE *LogFile = GetNetworkManager().GetDedicatedServerLogFile();
  if (LogFile)
  {
    fprintf(LogFile, "%2d:%02d:%02d ",now_tm->tm_hour,now_tm->tm_min,now_tm->tm_sec);
    vfprintf(LogFile, format, argptr);
    fprintf(LogFile, "\n");
    // fflush(LogFile); // we cannot flush so often. It is flushed on mission end.
  }

#ifdef NET_LOG
  char buf[512];
  sprintf(buf,"%2d:%02d:%02d ",now_tm->tm_hour,now_tm->tm_min,now_tm->tm_sec);
  vsprintf( buf+strlen(buf), format, argptr );
  NetLog(buf);
#endif

#endif

  return 0;
}

int ConsoleF(const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);
  int ret = VConsoleF(format, arglist);
  va_end(arglist);
  return ret;
}

int ConsoleTitle(const char *format, ...)
{
  BString<512> buf;
    
  va_list arglist;
  va_start( arglist, format );
  vsprintf(buf,format,arglist);
  va_end( arglist );

#if defined _WIN32

  TempWString<> bufW;
  {
    int wSize = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    bufW.Resize(wSize);
    int written = MultiByteToWideChar(CP_UTF8, 0, buf, -1, bufW.Data(), wSize);
    Assert(written==wSize);
    (void)written;
  }
  
  SetWindowTextW((HWND)hwndApp,bufW);
#else
  puts(buf);
#endif
  return 0;
}

/*
\patch_internal 1.22 Date 9/10/2001 by Ondra
- Fixed: Application Active detection was not working in dedicated server loop.
*/

#if defined(_WIN32) 

struct DSConsoleThreadContext
{
  HINSTANCE hInst;
  MultiThread::EventBlocker *dsInitializedEvent;
};
DSConsoleThreadContext GDSThreadContext;

static int CALLBACK ConsoleDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_INITDIALOG:
    {
      hwndApp=hDlg;
      InitWindow((HINSTANCE)hInstApp,hDlg,false,"");

      consoleEditHWnd = GetDlgItem(hDlg,IDC_CONSOLE_EDIT);

      GDSThreadContext.dsInitializedEvent->Unblock();

      return TRUE;
    }

    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
      {
        EndDialog(hDlg, LOWORD(wParam));
        hwndApp = NULL;
        ValidateQuit=true;
        CloseRequest = true;
        return TRUE;
      }
      break;

    case WM_SIZE:
      if (consoleEditHWnd)
      {
        RECT rect;
        GetClientRect(hDlg, &rect);
        MoveWindow
        (
          consoleEditHWnd, rect.left, rect.top,
          rect.right - rect.left, rect.bottom - rect.top, TRUE
        );
      }
      break;

    case WM_ACTIVATEAPP:
      // Keep track of whether or not the app is in the foreground
      GAppActive = ( wParam!=0 );
      break;
  }
  return FALSE;
}

static int CALLBACK SplashDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_INITDIALOG:
    {
      hwndSplash=hDlg;
      InitWindow((HINSTANCE)hInstApp,hDlg,false,"");

      const static int progressID[10]=
      {
        IDC_CHECK1,IDC_CHECK2,IDC_CHECK3,IDC_CHECK4,IDC_CHECK5,
        IDC_CHECK6,IDC_CHECK7,IDC_CHECK8,IDC_CHECK9,IDC_CHECK10
      };
      for (int i=0; i<10; i++)
      {
        splashProgress[i] = GetDlgItem(hDlg,progressID[i]);
      }

      return TRUE;
    }

    case WM_CLOSE:
      LogF("Splash close ignored");
      return TRUE;
    case WM_DESTROY:
      hwndSplash = NULL;
      break;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
      {
        hwndSplash = NULL;
        EndDialog(hDlg, LOWORD(wParam));
        return TRUE;
      }
      break;

    case WM_ACTIVATE:
      LogF("Splash WM_ACTIVATE %d",wParam,lParam);
      break;
    case WM_ACTIVATEAPP:
      // Keep track of whether or not the app is in the foreground
      LogF("Splash WM_ACTIVATEAPP %d",wParam);
      //GAppActive = ( wParam!=0 );
      if (!UseWindow)
      {
        //AppActivate(wParam!=0);
      }
      break;
  }
  return FALSE;
}
#endif        // _WIN32

#else
  int ConsoleF(const char *format, ...){return 0;}
#endif        // _ENABLE_DEDICATED_SERVER
//}

#if defined(_WIN32)  && (_ENABLE_DEDICATED_SERVER || _ENABLE_DEDICATED_CLIENT)

static void CreateSplash(HINSTANCE hInst)
{
  hwndSplash = CreateDialog(hInst, MAKEINTRESOURCE(IDD_SPLASH), NULL, SplashDlgProc);

  progressSet = 0;

  ShowWindow((HWND)hwndSplash,StartMinimized ? SW_MINIMIZE : SW_SHOW);
  UpdateWindow((HWND)hwndSplash);
  }

static void SetSplashProgress(int progress10)
{
  //splashProgress
  //splashProgress
  for (int i=progressSet; i<progress10; i++)
  {
    SendMessage(splashProgress[i],BM_SETCHECK,BST_CHECKED,0);
    UpdateWindow(splashProgress[i]);
    //PostMessage(splashProgress[i],WM_UPDATE,0);
  }
  progressSet = progress10;

  ProcessMessagesNoWait();
  LogF("Progress %d",progress10);
  //SetWindowText(splashProgress)
}

static void DestroySplash()
{
  LogF("Progress done");
  if (hwndSplash)
  {
    avoidUserInput = AUITrue;
    DestroyWindow((HWND)hwndSplash);
    avoidUserInput = AUIUnititialized;
  }
}

#else

static void CreateSplash(HINSTANCE hInst) {}
static void SetSplashProgress(int progress10) {}
static void DestroySplash() {}

#endif

/*!
\patch_internal 1.15 Date 8/10/2001 by Ondra
- Fixed: Interaction between SafeDisc and Fade protections
caused by dedicated server code.
*/

static bool ReportLowFpsFrame;
static float ReportLowFpsFrameMfps,ReportLowFpsFrameAfps;

void ReportLowFps(float mfps, float afps)
{
  ReportLowFpsFrame = true;
  ReportLowFpsFrameMfps = mfps;
  ReportLowFpsFrameAfps = afps;
}


//@{ _DEDICATED_CLIENT
#if defined(_WIN32) && (_ENABLE_DEDICATED_SERVER  || _DEDICATED_CLIENT)
THREAD_PROC_RETURN THREAD_PROC_MODE DedicatedClientMessageLooperThreadProc ( void *param )
{
  InitFPU();

  // create server console window
	LoadLibrary("riched20.dll");

  hwndApp = CreateDialog(GDSThreadContext.hInst, MAKEINTRESOURCE(IDD_CONSOLE), NULL, ConsoleDlgProc);
  ShowWindow((HWND)hwndApp,StartMinimized ? SW_MINIMIZE : SW_SHOW);
  UpdateWindow((HWND)hwndApp);

  SendMessageW(consoleEditHWnd,EM_EXLIMITTEXT,(WPARAM) 0,(LPARAM) INT_MAX);

  // message loop
#ifdef _WIN32
  MSG msg;
  while (true)
  {
    while( PeekMessage(&msg, 0, 0, 0, PM_REMOVE) )
    {
#if _GAMES_FOR_WINDOWS
      if (!XLivePreTranslateMessage(&msg))
#endif
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      if (msg.message==USER_CLOSE)
      {
        PostMessage((HWND)hwndApp,WM_CLOSE,0,0);
      }
    }
    if (ValidateQuit)
      break;
    Sleep(50); // Beware! We must give the CPU time to do better things!
  }
#endif //_WIN32

  return (THREAD_PROC_RETURN)0;
}
#endif

#if _ENABLE_DEDICATED_SERVER || _DEDICATED_CLIENT

static void CreateDedicatedClientConsoleWindow(HINSTANCE hInst, CreateEnginePars &pars)
{
  Engine *CreateEngineDummy();
  void InitWindowAppName();
  GEngine = CreateEngineDummy();
  InitWindowAppName();
  if (GEngine)
  {
    GEngine->Init();
  }
  HideCursor = false;

  // Create Dedicated Client Dialog
#if defined(_WIN32) 
  ThreadId dsMessageLooper;
  MultiThread::EventBlocker dsInitializedEvent;
  GDSThreadContext.dsInitializedEvent = &dsInitializedEvent;
  GDSThreadContext.hInst = hInst;
  if ( !poThreadCreate(&dsMessageLooper,32*1024,&DedicatedClientMessageLooperThreadProc,NULL) )
  {
    ValidateQuit = true;
    return;
  }
  else
  {
    // dialog created, wait until AppWinCreate can be created
    BlockerArItem objs[] = {&dsInitializedEvent};
    WaitForMultiple(objs, 1, false, INFINITE);
    if( !AppWinCreate((HWND)hwndApp) ) 
    {
      ValidateQuit = true;
      return;
    }
  }
#else
    AppWinCreate(0);
#endif

  //AllocConsole();
  ConsoleF("Dedicated client created.");
}
//@} _DEDICATED_CLIENT

#endif

#if _ENABLE_DEDICATED_SERVER

#if defined(_WIN32) 
THREAD_PROC_RETURN THREAD_PROC_MODE DedicatedServerMessageLooperThreadProc ( void *param )
{
  // create server console window
	LoadLibrary("riched20.dll");

  hwndApp = CreateDialog(GDSThreadContext.hInst, MAKEINTRESOURCE(IDD_CONSOLE), NULL, ConsoleDlgProc);
  ShowWindow((HWND)hwndApp,StartMinimized ? SW_MINIMIZE : SW_SHOW);
  UpdateWindow((HWND)hwndApp);

  SendMessageW(consoleEditHWnd,EM_EXLIMITTEXT,(WPARAM) 0,(LPARAM) INT_MAX);
  

  // message loop
#ifdef _WIN32
  MSG msg;
  while (true)
  {
    while( PeekMessage(&msg, 0, 0, 0, PM_REMOVE) )
    {
#if _GAMES_FOR_WINDOWS
      if (!XLivePreTranslateMessage(&msg))
#endif
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      if (msg.message==USER_CLOSE)
      {
        PostMessage((HWND)hwndApp,WM_CLOSE,0,0);
      }
    }
    if (ValidateQuit)
      break;
    Sleep(50); // Beware! We must give the CPU time to do better things!
  }
#endif //_WIN32

  return (THREAD_PROC_RETURN)0;
}
#endif

static void CreateDedicatedServer(HINSTANCE hInst, CreateEnginePars &pars)
{
  Engine *CreateEngineDummy();
  GEngine = CreateEngineDummy();

  if (GEngine)
  {
    GEngine->Init();
  }
  HideCursor = false;

#if defined(_WIN32) 
  ThreadId dsMessageLooper;
  MultiThread::EventBlocker dsInitializedEvent;
  GDSThreadContext.dsInitializedEvent = &dsInitializedEvent;
  GDSThreadContext.hInst = hInst;
  if ( !poThreadCreate(&dsMessageLooper,32*1024,&DedicatedServerMessageLooperThreadProc,NULL) )
  {
    ValidateQuit = true;
    return;
  }
  else
  {
    // dialog created, wait until AppWinCreate can be created
    BlockerArItem objs[] = {&dsInitializedEvent};
    WaitForMultiple(objs, 1, false, INFINITE);
    if( !AppWinCreate((HWND)hwndApp) ) 
    {
      ValidateQuit = true;
      return;
    }
  }
#else
    AppWinCreate(0);
#endif

  //AllocConsole();
  ConsoleF(LocalizeString(IDS_DS_SERVER_CREATED));
}

#if _ENABLE_REPORT
#include "memHeap.hpp"

#if defined _WIN32 && !defined MALLOC_WIN_TEST

extern RefD<MemHeapLocked> safeHeap;

static void PrintServerStats()
{
  if (safeHeap)
  {
    int totalAlloc = safeHeap->TotalAllocated();
    ConsoleF
    (
      "MT Mem: alloc %d",
      totalAlloc
    );  // debug log - need not to be localized
}
}

#else

static void PrintServerStats()
{
}

#endif

#include <El/Statistics/statistics.hpp>

StatisticsByName *messagePoolStatistics ();
StatisticsByName *messagePoolStatisticsUsed ();

static void PrintServerDetailedStats()
{
#if defined _WIN32 && !defined MALLOC_WIN_TEST
//   LogF("*** Detailed MT Heap stats");
//   safeHeap->LogAllocStats();
#endif

  SRef<StatisticsByName> stats = messagePoolStatistics();
  LogF("*** Message Pool recycled stats");
  stats->Report();

  stats = messagePoolStatisticsUsed();
  LogF("*** Message Pool used stats");
  stats->Report();
}
#else
#define PrintServerStats()
#endif

#ifndef _WIN32

volatile bool interrupted = false;

void handleInt ( int sig )
    // handles SIGINT
{
    interrupted = true;
}

char readKey ()
    // non-blocking keyboard peek
{
    fd_set set;                             // list of receiving fd's
    static struct timeval timeout =
        { 0L, 0L };                         // select() timeout: non-blocking
    FD_ZERO ( &set );
    FD_SET ( STDIN_FILENO, &set );
    int error = select(FD_SETSIZE,&set,NULL,NULL,&timeout);
    if ( error != 1 )
        return (char)0;                     // data are not ready
    return (char)getchar();
}

#endif

// VS 2005 is warning about non-reachable code executed after the DedicatedServerLoop function
#pragma warning(disable:4702)

static void DedicatedServerLoop()
{
  avoidUserInput = AUIFalse;

  #if _ENABLE_REPORT
  int stat1Time = GlobalTickCount();
  //int stat2Time = GlobalTickCount();
  #endif
#ifndef _WIN32
  signal(SIGINT,handleInt);
#endif
  while (true)
  {
    #if _ENABLE_PERFLOG && _ENABLE_CHEATS
      PROFILE_SCOPE_EX_HIER(total,*);
      SectionTimeHandle startTime = StartSectionTime();
    #endif

    #if _ENABLE_REPORT
    int now = GlobalTickCount();
    // print once per hour
    if (now>stat1Time+3600000)
    {
      PrintServerStats();
      stat1Time = now;
    }
    /*
    if (now>stat2Time+60000)
    {
      PrintServerDetailedStats();
      stat2Time = now;
    }
    */
    #endif
    AppServerLoop();
    I_AM_ALIVE();

    PROFILE_SCOPE_EX(MSGLP, *)

#if defined _WIN32

    if (ValidateQuit)
    {
      AppWinDestroy(NULL);
#if PROFILE_EXIT
      EnableProfiler();
#endif
      DDTerm();
#if PROFILE_EXIT
      DisableProfiler();
#endif
      //if( HideCursor ) ShowCursor(TRUE);
      int ret = 0;
      LogF("Exit %d",ret);
      Sleep(500);
      exit(ret);
      break; //unreachable
    }
#else
    if ( interrupted )
    {
      DDTerm();
      ConsoleF(LocalizeString(IDS_DS_SERVER_FINISHED));
      LogF("Exit (SIGINT signal)");
      Sleep(500);
      return;
    }
#endif

#if _ENABLE_PERFLOG && _ENABLE_CHEATS
    if (GEngine)
    {
      const float step = 1.0717734625; // pow(2,1/10) - 10 steps change 2x
      static float limitSlowFrame = 0.1;
      static float limitBottleneck = 0.05*(step*step*step);
      float time = GetSectionTime(startTime);
      if (LimitFpsCoef) // when slowness caused by a cheat, no need to diagnose it
      {
        PerfLogFrame(time,FLT_MAX,FLT_MAX);
      }
      else
      {
      PerfLogFrame(time,limitSlowFrame,limitBottleneck);
    }
    }

#endif

  }
}

#endif // _ENABLE_DEDICATED_SERVER

//@{ _DEDICATED_CLIENT

bool AppDedicatedClientLoop()
{
  PROFILE_SCOPE(cLoop);

  static DWORD lastTime;
  DWORD actTime=::GlobalTickCount();
  DWORD deltaTMs = actTime-lastTime;

  float deltaT=deltaTMs*0.001;
  lastTime=actTime;

  saturateMin(deltaT,0.300f);

  bool enableSceneDraw = false;
  bool enableHUDDraw = false;

  if( CanRender )
  {
    // map actions
    for (int i=0; i<UAN; i++) GInput.actionDone[i] = false;
    RenderFrame(deltaT, enableSceneDraw, enableHUDDraw);
    return false;
  }

  return true;
}

static void DedicatedClientLoop()
{
  avoidUserInput = AUIFalse;

  while (true)
  {
    AppDedicatedClientLoop();
    I_AM_ALIVE();

    PROFILE_SCOPE_EX(MSGLP, *)

#if defined _WIN32

    {
      RString playerName;
      int playerId = GetNetworkManager().GetPlayer();
      if (playerId>0)
      {
        const PlayerIdentity *ident = GetNetworkManager().FindIdentity(playerId);
        if (ident) playerName = ident->GetName();
      }
      SMaintainTitle.Update(GetNetworkManager().IsServer(),GetNetworkManager().IsGameMaster(),playerName);
    }
    if (ValidateQuit)
    {
      AppWinDestroy(NULL);
      DDTerm();
      //if( HideCursor ) ShowCursor(TRUE);
      int ret = 0;
      LogF("Exit %d",ret);
      Sleep(500);
      exit(ret);
      break; //unreachable
    }
#endif
  }
}
//@} _DEDICATED_CLIENT

/*!
\patch_internal 1.51 Date 4/17/2002 by Jirka
- Changed: config.bin (resource.bin) in mod has higher priority than config.cpp (resource.cpp) in base directory 
*/

#include "Es/Files/filenames.hpp"
static bool ParseConfig(ModInfo *mod, int &dummy)
{
  RString dir;
  // find bin directory
  if (!mod) dir = "bin";
  else dir = mod->fullPath + "\\bin";
  
  // special case first
  if (!mod)
  {
    // text config
    RString file = dir + "\\config.cpp";
    if (QFBankQueryFunctions::FileExists(file))
    {
      Pars.Parse(file, new ParamClassOwnerNoUnload("",file), NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
      return true;
    }
    // binary config
    file = dir + "\\config.bin";
    if (QFBankQueryFunctions::FileExists(file))
    {
      Pars.ParseBin(file, new ParamClassOwnerFilename("", &Pars, file), NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
      return true;
    }
    return false;
  }


#if _ENABLE_PATCHING
  if (GEnablePatching)
  {
    // text config
    RString file = dir + "\\config.cpp";
    if (QIFileFunctions::FileExists(file))
    {
      Pars.Parse(file, new ParamClassOwnerNoUnload("",file), NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
      //Pars.SetSource();
      return true;
    }

    // binary config
    file = dir + "\\config.bin";
    if (QIFileFunctions::FileExists(file))
    {
      Pars.ParseBin(file, new ParamClassOwnerFilename("", &Pars, file), NULL, &GParsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)
      return true;
    }
  }
#endif
  // use loaded bank only
  for (int i=0; i<mod->loadedBanks.Size(); i++)
  {
    if (stricmp(mod->loadedBanks[i].prefix, "bin\\")==0)
    {
      RString file = "config.cpp";
      QFBank bank;
      char *ext = GetFileExt(mod->loadedBanks[i].path.MutableData());
      if (ext) *ext=0;
      
      // ToDo: we can possibly avoid to call the bank.open, but it is called only once during start
      bank.open(mod->loadedBanks[i].path);
      if (bank.FileExists(file))
      {
        Pars.Parse(bank, file, NULL, &GParsingNamespace);
        return true;
      }
      file = "config.bin";
      if (bank.FileExists(file))
      {
        Pars.ParseBin(bank, file, NULL, &GParsingNamespace);
        return true;
      }
      return false;
    }
  }

  return false;
}

static void PreloadTexturesByModel(RString name)
{
  Ref<LODShape> shape = Shapes.New(name,ShapeShadow);
  if (!shape) return;
  for (int i=0; i<shape->NLevels(); i++)
  {
    ShapeUsed level = shape->Level(i);
    for (int s=0; s<level->NSections(); s++)
    {
      const ShapeSection &sec = level->GetSection(s);
      Texture *tex = sec.GetTexture();
      if (tex)
      {
        GEngine->TextBank()->MakePermanent(tex);
      }
    }
  }
}

static void PreloadTexturesByName(RString name)
{
  name.Lower();
  Ref<Texture> tex = GlobLoadTexture(name);
  if (tex) GEngine->TextBank()->MakePermanent(tex);
}



static void PreloadTexturesByConfig(ParamEntryVal preload, ParamEntryVal cfgClass)
{
  RStringB relAll="*";
  RStringB absAll="\\*";
  RStringB relModelAll="@*";
  RStringB absModelAll="\\@*";
  for (int i=0; i<preload.GetEntryCount(); i++)
  {
    ParamEntryVal entry = preload.GetEntry(i);
    RStringB name = entry.GetName();
    ConstParamEntryPtr cfgEntry = cfgClass.FindEntry(name);
    if (!cfgEntry)
    {
      RptF(
        "Preloaded texture %s not found in %s",
        cc_cast(name),cc_cast(cfgClass.GetContext())
      );
      continue;
    }
    if (entry.IsClass())
    {
      // dive into the class in both source and target
      PreloadTexturesByConfig(entry,*cfgEntry);
    }
    else
    {
      // preloading textures may take some times - make sure we refresh the progress meanwhile
      I_AM_ALIVE();
      if (GProgress) GProgress->Refresh();
  
      RStringB value = (*cfgEntry);
      RStringB flags = entry;
      if (flags==absAll)
      {
        PreloadTexturesByName(value);
      }
      else if (flags==relAll)
      {
        RString file = GetPictureName(value);
        PreloadTexturesByName(file);
      }
      else if (flags==relModelAll)
      {
        RString file = GetShapeName(value);
        PreloadTexturesByModel(file);
      }
      else if (flags==absModelAll)
      {
        PreloadTexturesByModel(value);
      }
      else
      {
        RptF("Unknown preload flag %s in %s",cc_cast(flags),cc_cast(entry.GetName()));
      }
    }
  }
}

static void PreloadTexturesByConfig()
{
  // preload all textures marked for preloading
  ParamEntryVal preload = Pars>>"PreloadTextures";
  PreloadTexturesByConfig(preload,Pars);
}

static void PreloadConfigByConfig(ParamEntryVal preload, ParamEntryVal cfgClass)
{
  static RStringB subdir="*";
  static RStringB single=".";
  for (int i=0; i<preload.GetEntryCount(); i++)
  {
    ParamEntryVal entry = preload.GetEntry(i);
    RStringB name = entry.GetName();
    ConstParamEntryPtr cfgEntry = cfgClass.FindEntry(name);
    if (!cfgEntry)
    {
      RptF(
        "Preloaded entry %s not found in %s",
        cc_cast(name),cc_cast(cfgClass.GetContext())
      );
      continue;
    }
    if (entry.IsClass())
    {
      // dive into the class in both source and target
      //ParamEntryVal &cEntry = GetParamEntryVal(cfgEntry);
      PreloadConfigByConfig(entry,*cfgEntry);
    }
    else
    {
      RStringB flags = entry;
      // target entry should be a class
      if (!strcmpi(flags,"fastFind"))
      {
        cfgEntry->OptimizeFind();
      }
      else if (flags==single)
      {
        unconst_cast(cfgEntry.GetPointer())->DisableStreaming(false);
      }
      else 
      {
        if (flags!=subdir)
        {
          RptF("Unknown preload flag %s in %s",cc_cast(flags),cc_cast(entry.GetName()));
        }
        unconst_cast(cfgEntry.GetPointer())->DisableStreaming(true);
      }
    }
  }
}

static void PreloadConfigByConfig()
{
  ParamEntryVal preload = Pars>>"PreloadConfig";
  PreloadConfigByConfig(preload,Pars);
}

#if _ENABLE_TEXTURE_HEADERS

class TextureHeadersFindContext
{
public:
  RString bankPrefix;
};

// callback function that is applied to all file infos in bank
static void LoadHeadersFromBank(const FileInfoO &fi, const FileBankType *files, void *context)
{
  DoAssert(context);
  if (!context)
    return;

  // get context
  TextureHeadersFindContext* ctx = static_cast<TextureHeadersFindContext*>(context);
  
  // check if this is the texture headers file
  int length1 = strlen(TextureHeaderManager::DefaultFileName);
  int length2 = fi.name.GetLength();
  if (length2 < length1 || strcmpi(fi.name.Data() + (length2-length1), TextureHeaderManager::DefaultFileName) != 0)
  {
    // not the texture header file
    return;
  }

  bool isFromPBO = true;
#if _ENABLE_PATCHING
  // check if the file is patched
  if (GEnablePatching && fi.loadFromFile)
    isFromPBO = false;
#endif
  RString name = ctx->bankPrefix + fi.name;
  RString texturePrefix = ctx->bankPrefix + fi.name.Substring(0, length2-length1);
  texturePrefix.Lower();

  if (GTextureHeaderManager.LoadFromFile(name ,true, isFromPBO, texturePrefix))
  {
    Log("Added texture headers from file \"%s\"", cc_cast(name));
  }
  else
  {    
    RptF("Failed adding texture headers from file \"%s\"", cc_cast(name));
  }
}

void LoadTextureHeadersFromBanks()
{
  if (!GUseTextureHeaders)
    return;

  BankList::ReadAccess banks(GFileBanks);
  for (int i=0; i!=banks.Size(); ++i)
  {
    const QFBank &bank = banks[i];

    // now we have to check, if this is really the bank that will be used for opening files 
    // there can be more banks with the same prefix, but only the first one will be used to open files
    QFBank *testBank = QFBankQueryFunctions::AutoBank(bank.GetPrefix());
    if (testBank != &bank)
      continue; //bank is not used

    TextureHeadersFindContext ctx;
    ctx.bankPrefix = bank.GetPrefix();
    bank.ForEach(LoadHeadersFromBank, &ctx);
  }

  // check correctness of headers
  // TODO - maybe disable this for retail config?
  GTextureHeaderManager.CheckHeadersAreCorrect();
}
#endif //_ENABLE_TEXTURE_HEADERS

#if 0

typedef void (*RegisterPluginFunction)(int &retLevel, ModuleFunction *&retInit, ModuleFunction *&retDone);
AutoArray< SRef<RegistrationItem> > PluginCache;
void RegisterPlugins()
{
  RString dir = "Plugins\\";
  WIN32_FIND_DATA info;
  HANDLE h = FindFirstFile(dir + "*.dll", &info);
  if (h != INVALID_HANDLE_VALUE)
  {
    do
    {
      if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
      RString name = dir + info.cFileName;
      HMODULE library = LoadLibrary(name);
      if (library)
      {
        RegisterPluginFunction f = (RegisterPluginFunction)GetProcAddress(library, "RegisterPlugin");
        if (f)
        {
          int level;
          ModuleFunction *initFunction;
          ModuleFunction *doneFunction;
          (f)(level, initFunction, doneFunction);
          RegistrationItem *item = new RegistrationItem(level, initFunction, doneFunction);
          PluginCache.Add(item);
          RegisterModule(item);
        }
      }
    }
    while (FindNextFile(h, &info));
    FindClose(h);
  }
}
#endif

#if _ENABLE_REPORT
class DebugCommandScript: public IDebugCommandCommandHandler
{
  public:
  void Do(const char *command);
};

void DebugCommandScript::Do(const char *command)
{
  GameVarSpace vars(false);
  int gshandle = GGameState.BeginContext(&vars);
  GGameState.Execute(command, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  RString error = GGameState.GetLastErrorText();
  if (error.GetLength()>0)
  {
    GDebugConsole->Printf("Error: %s",(const char *)error);
  }
  GGameState.EndContext(gshandle);
}

static DebugCommandScript SDebugCommandScript;

#if _ENABLE_CHEATS

// advances debugging - via scripting
#include <El/evaluator/expressImpl.hpp>

static GameValue DebugFlushMem(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  float ammount = oper2;
  RString id = oper1;
  size_t released = FreeOnDemandReleaseSlot(id,toLargeInt(ammount));
  return GameValue(float(released));
}

typedef long long MemStatusType;

struct MemoryStress
{
  private:
  void *freeVMRegion;
  
  AutoArray<size_t *> memAllocated;
  size_t sizeAllocated;
  
  public:
  int opsPerFrame;
  MemStatusType keepVMFree;
  MemStatusType keepVMCont;
  size_t minAllocSize;
  size_t maxAllocSize;
  
  MemoryStress();
  void Frame();
  bool CheckVMCont();
};

static MemoryStress GMemoryStress;

void StressTest()
{
  GMemoryStress.Frame();
}

MemoryStress::MemoryStress()
{
  opsPerFrame = 0;
  keepVMFree = 1024*1024*1024; // keep one GB free by default?
  keepVMCont = 8*1024*1024; // keep 8 MB cont. by default
  minAllocSize = 32;
  maxAllocSize = 256*1024;
  sizeAllocated = 0;
};

static MemStatusType CheckVMFree()
{
    MEMORYSTATUSEX memstat;
    memstat.dwLength = sizeof(memstat);
    GlobalMemoryStatusEx(&memstat);
    return memstat.ullAvailVirtual;
}

bool MemoryStress::CheckVMCont()
{
  if (freeVMRegion)
  {
    MEMORY_BASIC_INFORMATION buffer;
    PROFILE_SCOPE_EX(vmVer,mem);
    SIZE_T retSize = VirtualQuery(freeVMRegion,&buffer,sizeof(buffer));
    if (retSize==sizeof(buffer) && buffer.State==MEM_FREE && buffer.RegionSize>=keepVMCont)
    { // we have verified the block is still free and still large enough - return
      return true;
    }
    
  }
    
  // the block not free or not large enough, try to find a new one    
  PROFILE_SCOPE_EX(vmVAl,mem);
  void *alloc = VirtualAlloc(NULL,keepVMCont,MEM_RESERVE,PAGE_NOACCESS);
  if (alloc)
  {
    VirtualFree(alloc,0,MEM_RELEASE);
    freeVMRegion = alloc;
    return true;
  }
  return false;
}

void MemoryStress::Frame()
{
  if (opsPerFrame<=0) return;

  // first deallocate randomly - deallocate a bit less to make sure we do not fall below keepVMFree in long term
  for (int i=0; i<opsPerFrame/2; i++)
  {
    if (memAllocated.Size()<=0) break;
    int index = GRandGen.RandomInt(0,memAllocated.Size());
    size_t size = memAllocated[index][0];
    delete[] memAllocated[index];
    memAllocated.Delete(index);
    GMemFunctions->CleanUp();
    sizeAllocated -= size;
  }
  for (int i=0; i<opsPerFrame; i++)
  {
    MemStatusType vmFree = CheckVMFree();
    if (vmFree<keepVMFree) break;
    float allocSize = GRandGen.RandomValue();
    
    size_t allocCount = (toLargeInt((maxAllocSize-minAllocSize)*allocSize+minAllocSize)+sizeof(size_t)-1)/sizeof(size_t);
    size_t *mem = new size_t[allocCount];
    if (mem)
    {
      if (!CheckVMCont())
      {
        delete[] mem;
        break;
      }
      else
      {
        memAllocated.Add(mem);
        sizeAllocated += allocCount*sizeof(size_t);
      }
    }
  }
}
static GameValue DebugStressMem(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GMemoryStress.opsPerFrame = safe_cast<GameScalarType>(oper1);
  GMemoryStress.keepVMFree = safe_cast<GameScalarType>(oper2);
  return GameValue();
}

static GameValue DebugSearchMem(const GameState *state, GameValuePar oper1)
{
  // TODO: read search pattern from oper1
  static const int search[]= {
    0x00000000, 0x3f800000, 0x3d4ccccd, 0x00000000,
    0x3f733333, 0x00000000, 0x3f800000, 0x3f800000,
  };
  const int searchLen = sizeof(search);

  // TODO: return search results
  GameValue value = state->CreateGameValue(GameArray);
  GameArrayType &array = value;

  // print map of virtual memory
  // scan whole application space (2 GB) address range
  // do not try to access kernel space - we would get no information anyway
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  size_t pageSize = si.dwPageSize;
  uintptr_t start = uintptr_t(si.lpMinimumApplicationAddress);
  uintptr_t end = uintptr_t(si.lpMaximumApplicationAddress);
  for (uintptr_t addr = start; addr<end; )
  {
    MEMORY_BASIC_INFORMATION buffer;
    SIZE_T retSize = VirtualQuery((void *)addr,&buffer,sizeof(buffer));
    if (retSize==sizeof(buffer) && buffer.RegionSize>0)
    {
      // dump information about this region
      if (buffer.State&MEM_COMMIT)
      if (buffer.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE))
      if ((buffer.Protect&PAGE_GUARD)==0)
      //if (buffer.Type&(MEM_MAPPED|MEM_PRIVATE))
      {
        for(int *mem=(int *)addr; mem<(int *)(addr+buffer.RegionSize-searchLen); mem++)
        {
          if (memcmp(mem,search,searchLen)==0 && mem!=search)
          {
            void ReportMemoryRegion(void *mem);
            //LogF("0x%x",mem);
            ReportMemoryRegion(mem);
            array.Add(GameValue(GameScalarType(uintptr_t(mem))));
          }
        }
      }
      addr += buffer.RegionSize;
    }
    else
    {
      // always proceed
      addr += pageSize;
    }
  }
  
  return value;
}

#include <El/Modules/modules.hpp>

static const GameOperator DbgBinary[]=
{
  GameOperator(GameScalar,"diag_flushMem",function,DebugFlushMem,GameString,GameScalar TODO_OPERATOR_DOCUMENTATION),
  GameOperator(GameVoid,"diag_stressMem",function,DebugStressMem,GameScalar,GameScalar TODO_OPERATOR_DOCUMENTATION),
};

static const GameFunction DbgUnary[]=
{
  GameFunction(GameNothing,"diag_cheat1",DebugCheat1,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_cheat2",DebugCheat2,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_cheat3",DebugCheat3,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_cheatX",DebugCheatXFwd,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_cheatXB",DebugCheatXBck,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameArray,"diag_searchMem",DebugSearchMem,GameArray TODO_FUNCTION_DOCUMENTATION),
};

INIT_MODULE(GameStateDbgObj, 3)
{
  GGameState.NewOperators(DbgBinary,lenof(DbgBinary));
  GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
};

#endif

#endif


class MemCpy
{
  public:
  void operator ()(void *dst, const void *src, size_t size) const
  {
    memcpy(dst,src,size);
  }
};


//#include <Es/Containers/array2D.hpp>

#if 0
class AbstractFoo
{
  public:
	virtual void value() const = 0;
	// Meyers 3rd Item 7:
	virtual ~AbstractFoo() {}
};

class Foo: public AbstractFoo
{
  public:
	virtual void value() const {}
	virtual ~Foo() {}
	
	void CallValue(){value();}
	Foo()
	{
	  value();
	  CallValue();
	}
};

static void TestPureVirtualCall()
{
	SRef<AbstractFoo> p1 = new Foo();
	delete p1.GetRef();
	p1->value();
}

#endif

void Test()
{
  {
    auto encoded = EncodedString<EncoderShiftPos<0x6a>>("Encoded secret");
    LogF("%s",cc_cast(DecodeString(encoded)));
  }

  //Array2DUnitTest executeArray2DUnitTest;
  #if 0 //_PROFILE
  FreeOnDemandGarbageCollect(512*1024,256*1024);
  
  QuadTreeExRoot<int> quad(8,8,0);
  
  quad.Set(1,2,1002);
  quad.Set(4,4,4004);
  quad.Set(6,7,6007);
  
  
  const int bufSize = 32*1024;
  void *buf = XPhysicalAlloc(bufSize,MAXULONG_PTR,0,PAGE_READWRITE|PAGE_WRITECOMBINE);
  
  if (!buf) return;
  
  QIFStream src;
  src.open("dta\\abel.pbo");
  while (!src.PreRead())
  {
    GFileServerUpdate();
  }
  
  
  LogF("*** Test Begin");
  float fps = 10;
  float frameDuration = 100;
  GPerfCounters.Frame(fps, frameDuration, Engine::NFrameDurations);
  GPerfProfilers.Frame(fps, frameDuration, Engine::NFrameDurations);
  
  SectionTimeHandle start = StartSectionTime();
  #if _DEBUG
    const int iters = 4;
  #else
    const int iters = 100;
  #endif
  for (int i=0; i<iters; i++)
  {
    PROFILE_SCOPE(loop);
    QIFStream in;
    in.open("dta\\abel.pbo");
    while(true)
    {
      int rd = in.readCustomCopy(buf,bufSize,MemCpy());
      if (rd!=bufSize) break;
    }
  }

  GPerfCounters.Frame(fps, frameDuration, Engine::NFrameDurations);
  GPerfProfilers.Frame(fps, frameDuration, Engine::NFrameDurations);
  GPerfProfilers.DiagnoseAvg(true,20,true);

  LogF("Duration %g ms",GetSectionTime(start)*1000);
  
  XPhysicalFree(buf);
  LogF("*** Test End");
  #endif
}

static bool ReadProductFile(ParamFile &product, GameDataNamespace *globals, RString dir)
{
  RString filename = dir + RString("\\product.cpp");
  if (QIFileFunctions::FileExists(filename))
  {
    product.Parse(filename, NULL, NULL, globals);
    return true;
  }
  
  filename = dir + RString("\\product.bin");
  if (QIFileFunctions::FileExists(filename))
  {
    product.ParseBin(filename, NULL, NULL, globals);
    return true;
  }

  return false;
}

static RString GetModuleDirectory()
{
  char exeName[MAX_PATH];
#ifdef _WIN32
  GetModuleFileName(NULL, exeName, MAX_PATH);
  char *ext = strrchr(exeName, '\\');
  if (ext) return RString(exeName, ext - exeName);
#else
  if (getcwd(exeName, MAX_PATH))
    return RString(exeName);
#endif
  return RString();
}

static void ReadProductInfo()
{
  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed
  ParamFile product;

#if _PUBLICBETA
  if (!ReadProductFile(product, &globals, "tkoh_preview\\dta"))
  {
#endif
    // prefer product.bin from main OA folder
    if (!ReadProductFile(product, &globals, "expansion\\dta"))
    {
      // product information not found in the current directory
      // try to set current directory based on exe location
      RString dir = GetModuleDirectory();
      if (!ReadProductFile(product, &globals, "dta"))
      {
        // product information not found in the current directory
        // try to set current directory based on exe location
        RString dir = GetModuleDirectory();
        if (dir.GetLength() > 0 && (
#if _PUBLICBETA
          ReadProductFile(product, &globals, dir + RString("\\tkoh_preview\\dta")) || 
#endif
          ReadProductFile(product, &globals, dir + RString("\\expansion\\dta")) || 
          ReadProductFile(product, &globals, dir + RString("\\dta"))))
        {
          // alternate location found
          SetCurrentDirectory(dir);
        }
      }
    }
#if _PUBLICBETA
  }
#endif

  if (product.GetEntryCount() == 0)
  {
    RptF("No product.bin found, using defaults for a core engine.");
    return;
  }

  ProfilePathDefault = product >> "profilePathDefault";
  ProfilePathCommon = product >> "profilePathCommon";

  ExtensionSave = product >> "extensionSave";
  ExtensionProfile = product >> "extensionProfile";
  ExtensionWizardMission = product >> "extensionWizardMission";
  ExtensionAddon = product >> "extensionAddon";

  // product info can replace the default list of mods
  void InitModList(RString list);
  ParamEntryPtr mods = product.FindEntry("defaultMods");
  if (mods) InitModList(*mods);
}

/// wrapper around Win32 call
static void CallDestroyWindow()
{
#if defined(_WIN32) 
  if (hwndApp) DestroyWindow((HWND)hwndApp);
#endif
}

#define STRINGIZE_X(x) #x
#define STRINGIZE(x) STRINGIZE_X(x)


#if _VBS3 && !_VBS2_LITE
  #include "Protect/Trial.hpp"
#endif

void RecursiveDecent( ConstParamEntryPtr val,RString &parse )
{
  parse = parse + RString(val->GetName().Data()); 
  ConstParamEntryPtr ptr = val->FindBase();

  if(ptr)
  {
    parse = parse + RString(" -> ");
    if(ptr->IsClass())
      RecursiveDecent(ptr,parse);
  }
}

void ParseConfigClass( RString name , ConstParamEntryPtr entry )
{
  if(!entry->IsClass())
    return;

  RString path; 
  path = RString("P:\\vbs2\\Doc\\Config\\") + name + RString(".txt");

  QOFStream out;
  out.open((const char*)path);
  
  if(out.error()!=LSOK)
    return;

  for( int i = 0; i < entry->GetEntryCount(); ++i )
  {
    RString parse("");
    ParamEntryVal val = entry->GetEntry(i);
    if(val.IsClass())
    {
      RecursiveDecent(&val,parse);

      out.write(parse.Data(),parse.GetLength());
      out.write(";\r\n",strlen(";\r\n"));
    }
  }

  out.close();
}

#if _GAMES_FOR_WINDOWS

/// Initialization of Games for Windows - LIVE
static bool InitGamesForWindows(RString language)
{
  XLIVE_INITIALIZE_INFO info;
  memset(&info, 0, sizeof(info));
  info.cbSize = sizeof(info);
  info.langID = 0x0400 + FindLangID(language);
  HRESULT result = XLiveInitialize(&info);
  if (FAILED(result))
  {
    RptF("XLiveInitialize failed with 0x%x", result);
    return false;
  }
  XOnlineStartup();
#if _ENABLE_REPORT
  XLiveSetDebugLevel(XLIVE_DEBUG_LEVEL_INFO, NULL);
#endif
  return true;
}

#endif

//-----------------------------------------------------------------------------
// Purpose: callback hook for debug text emitted from the Steam API
//-----------------------------------------------------------------------------

extern "C" void __cdecl SteamAPIDebugTextHook(int severity, const char *text)
{
  // if you're running in the debugger, only warnings (nSeverity >= 1) will be sent
  // if you add -debug_steamapi to the command-line, a lot of extra informational messages will also be sent
  LogF("STEAMWORKS: %s", text);
}

bool InitSteam()
{
#if _ENABLE_STEAM
  if (!UseSteam) return false;
#endif

  // initialize Steam API
  if (!SteamAPI_Init())
  {
#if _SUPER_RELEASE
    SteamAPI_RestartAppIfNecessary(221100); // Steam appid passed
#endif
    return false;
  }
#if 0
  // Steam Profile Name used as PlayerName instead of Windows Profile Name (which is set as default)
  // one can edit her steam profile name through Steam/Friends/ChangeProfileName
  RString steamPersonalName;
  if (SteamFriends()) 
  {
    steamPersonalName = SteamFriends()->GetPersonaName();
    if (!steamPersonalName.IsEmpty()) Glob.header.SetPlayerName(steamPersonalName);
  }
#endif
  if (SteamUtils()) SteamUtils()->SetWarningMessageHook(&SteamAPIDebugTextHook);

#if _ENABLE_STEAM
  // register Steam callbacks
  GInput.RegisterCallbacks();
#endif

  return true;
}

/// scan a subclass of ArmA2.par file for arguments
template <class Function>
void ScanArguments(ParamEntryPar args, Function func)
{
  for (int i=0; i<args.GetEntryCount(); i++)
  {
    ParamEntryVal entry = args.GetEntry(i);
    if (entry.IsClass())
    {
      // set of arguments valid only for Xbox configuration
      if (stricmp(entry.GetName(), "XBOX") == 0) continue;
#ifndef _RELEASE
      // set of arguments valid only for Release configuration
      if (stricmp(entry.GetName(), "RELEASE") == 0) continue;
#endif
#ifndef _DEBUG
      // set of arguments valid only for Debug configuration
      if (stricmp(entry.GetName(), "DEBUG") == 0) continue;
#endif
      // all filters passed, scan a subclass
      ScanArguments(entry,func);
    }
    else
    {
      // single argument processing
      RStringB arg = entry;
      func(arg, cc_cast(arg) + strlen(arg));
    }
  }
}

static void RegisterMainThread()
{
  RegisterThread(GetCurrentThreadId(), 0, "Main Thread");
}

static bool ProcessLauncher(char *cmdLine)
{
#if _ENABLE_ADDONS && _ENABLE_PARAMS && defined _WIN32
  const char *ext = GetFileExt(LoadFile);
  if (stricmp(ext, ".pbo") != 0) return true; // no test needed

  if (!QFBankQueryFunctions::FileExists(LoadFile))
  {
    ErrorMessage(LocalizeString(IDS_MSG_ADDON_NOT_FOUND)/*"File '%s' not found."*/, cc_cast(LoadFile));
    return false;
  }

  QFBank bank;
  if (!bank.open(RString(LoadFile, ext - LoadFile)))
  {
    ErrorMessage(LocalizeString(IDS_MSG_ADDON_CANNOT_OPEN)/*"Cannot open file '%s'."*/, cc_cast(LoadFile));
    return false;
  }

  // check the product property of the bank, decide what application it belongs to
  RString product = bank.GetProperty("product");
  if (product.GetLength() > 0 && StringInList(product, ProductList))
  {
    // the current application is supporting this bank
    return true;
  }

  // default for pbo without product info
  if (product.GetLength() == 0) product = "TakeOnH";

  // find the application in the registry
  HKEY key;
  if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE, RString("SOFTWARE\\Classes\\.pbo\\AppsTable\\") + product, 0, KEY_READ, &key) != ERROR_SUCCESS)
  {
    ErrorMessage(LocalizeString(IDS_MSG_ADDON_UNKNOWN_PRODUCT)/*"No application found to open '%s' (required product '%s')."*/, cc_cast(LoadFile), cc_cast(product));
    return false;
  }

  char path[MAX_PATH];
  DWORD size = MAX_PATH;
  DWORD type = REG_SZ;
  HRESULT result = ::RegQueryValueEx(key, NULL, 0, &type, (BYTE *)path, &size);
  ::RegCloseKey(key);
  
  // tell user the application was not found
  if (result != ERROR_SUCCESS)
  {
    ErrorMessage(LocalizeString(IDS_MSG_ADDON_UNKNOWN_PRODUCT)/*"No application found to open '%s' (required product '%s')."*/, cc_cast(LoadFile), cc_cast(product));
    return false;
  }

  // now we have the exe file to launch
  char exeName[MAX_PATH];
  GetModuleFileName(NULL, exeName, MAX_PATH);
  if (stricmp(exeName, path) == 0)
  {
    // the current application is supporting this bank
    return true;
  }

  // launch the referenced exe
  STARTUPINFO startupInfo;
  ZeroMemory(&startupInfo, sizeof(startupInfo));
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInformation;
  ZeroMemory(&processInformation, sizeof(processInformation));
  // current directory inherited from the exe placement
  RString dir;
  ext = strrchr(path, '\\');
  if (ext) dir = RString(path, ext - path);
  // command line
  RString command = Format("\"%s\" -noLauncher %s", path, cmdLine);

  ::CreateProcess(
    NULL, command.MutableData(),
    NULL, NULL,											// security
    FALSE,													// inheritance
    0,															// creation flags 
    NULL,														// env
    dir,														// pointer to current directory name 
    &startupInfo, &processInformation);

  CloseHandle(processInformation.hProcess);
  CloseHandle(processInformation.hThread);

  // terminate ourself
  DDTerm();
  exit(0);
  return false; // will not continue
#endif
}

void RunOtherInstance(RString runOptions)
{
  // now we have the exe file to launch
  char exeName[MAX_PATH];
  GetModuleFileName(NULL, exeName, MAX_PATH);
  // launch the referenced exe
  STARTUPINFO startupInfo;
  ZeroMemory(&startupInfo, sizeof(startupInfo));
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInformation;
  ZeroMemory(&processInformation, sizeof(processInformation));
  // command line
  RString command = Format("\"%s\" %s", exeName, cc_cast(runOptions));

  ::CreateProcess(
    NULL, command.MutableData(),
    NULL, NULL,											// security
    FALSE,													// inheritance
    0,															// creation flags 
    NULL,														// env
    NULL,														// pointer to current directory name 
    &startupInfo, &processInformation);
  CloseHandle(processInformation.hProcess);
  CloseHandle(processInformation.hThread);
}


#if _USE_BATTL_EYE_CLIENT
class VMContextBattlEyeMonitor : public VMContextCallback
{
public:
  virtual bool operator() (RString script)
  {
    return GetNetworkManager().BattlEyeOnScriptExec(script);
  }
};


// Use BattlEye to monitor scripts execution
VMContextBattlEyeMonitor battlEyeMonitor;
#endif 

//{@ Debug All Signatures 
static bool DebugTestLoadKey(const FileItem &file, AutoArray<DSKey> &keys)
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

typedef void HashErrorCallback(RString reason);


struct AllLevelsCheckContext
{
  DSHash hashVer1Lev0, hashVer2Lev0, hashVer2Lev2;

  AutoArray<DSKey> _keys;
  bool  ok;
  HashErrorCallback *_errorCallback;

  AllLevelsCheckContext(HashErrorCallback callback) : ok(true), _errorCallback(callback) {}
};

static bool CheckSignaturesAllLevels(const FileItem &file, AllLevelsCheckContext &ctx)
{
  DSSignature signature;
  if (signature.Load(file.path + file.filename))
  {
    bool found = false;
    for (int i=0; i<ctx._keys.Size(); i++)
    {
      if (signature._key == ctx._keys[i])
      {
        // key found
        int sigVer = signature.Version();
        if (sigVer>0 && sigVer<=BIS_SIGNATURE_VERSION)
        {
          if (sigVer!=BIS_SIGNATURE_VERSION)
          {
            RptF("Warning: Signature %s is deprecated!", cc_cast(file.path + file.filename));
          }
          if (signature.Version()!=2)
          {
            if (!DataSignatures::VerifySignature(ctx.hashVer1Lev0, signature))
            {
              DataSignatures::VerifySignature(ctx.hashVer1Lev0, signature);
              ctx._errorCallback(Format("Signature %s is wrong. Err 0x10. Test FAILED!", cc_cast(file.path + file.filename)));
              return false;
            }
          }
          else
          {
            if (!DataSignatures::VerifySignature(ctx.hashVer2Lev0, signature,0))
            {
              DataSignatures::VerifySignature(ctx.hashVer2Lev0, signature,0);
              ctx._errorCallback(Format("Signature %s is wrong. Err 0x20. Test FAILED!", cc_cast(file.path + file.filename)));
              return false;
            }
            if (!DataSignatures::VerifySignature(ctx.hashVer2Lev2, signature,2))
            {
              DataSignatures::VerifySignature(ctx.hashVer2Lev2, signature,2);
              ctx._errorCallback(Format("Signature %s is wrong. Err 0x22. Test FAILED!", cc_cast(file.path + file.filename)));
              return false;
            }
          }
        }
        found = true;
      }
    }
    if (!found)
    {
      ctx._errorCallback(Format("Key not found for signature %s Test FAILED!", cc_cast(file.path + file.filename)));
    }
  }
  else ctx._errorCallback(Format("Cannot load signature %s Test FAILED!", cc_cast(file.path + file.filename)));
  LogF(Format("Signature tests for %s PASSED!", cc_cast(file.path + file.filename)));
  return false;
}


static bool GetAllHashes(const QFBank &bank, Temp<char> &hashVer1Lev0, Temp<char> &hashVer2Lev0, Temp<char> &hashVer2Lev2, bool full, HashErrorCallback _errorCallback)
{
#define DATA_CONTENT_HASH_DIRECTLY_FROM_THE_END_OF_FILE 1
#if DATA_CONTENT_HASH_DIRECTLY_FROM_THE_END_OF_FILE
  // Read the bank hash from the end of pbo file
  RString fullPath = bank.GetOpenName();
  QFileSize size = QIFileFunctions::GetFileSize(fullPath);
  QIFStream file;
  const int ModHashSize = 20; // the length of mod
  file.open(fullPath, size-ModHashSize);
  hashVer1Lev0.Realloc(ModHashSize);
  if (!file.fail())
  {
    file.read(hashVer1Lev0, ModHashSize);
    file.close();
  }
  else 
  {
    _errorCallback( Format("Hash of %s not loaded. Error 0x10. Test FAILED!", cc_cast( bank.GetOpenName() )) );
    return false;
  }
#else
  if ( !bank.GetHash<HashCalculator>(hashVer1Lev0, 1, 0) )
  {
    _errorCallback( Format("Hash of %s not loaded. Error 0x10. Test FAILED!", cc_cast( bank.GetOpenName() )) );
    return false;
  }
#endif
#if 0
  if (full)
  {
    HashCalculator hashcalc;
    if ( !bank.CalculateHash(hashcalc) )
    {
      AutoArray<char> hashComputed;
      hashcalc.GetResult(hashComputed);
      if (hashVer1Lev0.Size()!=hashComputed.Size() || memcmp(hashComputed.Data(),hashVer1Lev0,hashComputed.Size()))
      {
        _errorCallback( Format("Hash of %s not matching content. Error 0x30. Test FAILED!", cc_cast( bank.GetOpenName() )) );
      }
    }
  }
#else
  if (full)
  {
    int index = GFileBanks.BankIndex(bank);
    if (index<0)
    {
      _errorCallback( Format("Hash of %s not computed. Error 0x31. Test FAILED!", cc_cast( bank.GetOpenName() )) );
    }
    else
    {
      BankHashCalculatorAsync calc(index,false);
      while (!calc.Process())
      {
        I_AM_ALIVE();
        FreeOnDemandFrame();
      }
      if (!calc.IsValid())
      {
        _errorCallback( Format("Hash of %s invalid. Error 0x32. Test FAILED!", cc_cast( bank.GetOpenName() )) );
        return false;
      }
    }
  }
#endif
  if ( !bank.GetHash<HashCalculator>(hashVer2Lev0, 2, 0) )
  {
    _errorCallback( Format("Hash of %s not loaded. Error 0x20. Test FAILED!", cc_cast( bank.GetOpenName() )) );
    return false;
  }
  if ( !bank.GetHash<HashCalculator>(hashVer2Lev2, 2, 2) )
  {
    _errorCallback( Format("Hash of %s not loaded. Error 0x22. Test FAILED!", cc_cast( bank.GetOpenName() )) );
    return false;
  }
  return true;
}

static bool DoCheckAllSignatures(bool full)
{
  AllLevelsCheckContext ctx(ReportHashError);
  ForMaskedFile("Keys\\", "*.bikey", DebugTestLoadKey, ctx._keys);
  ForMaskedFile(RString("Expansion\\Keys\\"), "*.bikey", DebugTestLoadKey, ctx._keys);    

  DWORD startTime = GlobalTickCount();
  RptF("*** ALL SIGNATURES CHECK START ***");
  BankList::ReadAccess banks(GFileBanks);
  for (int i=0; i<banks.Size(); ++i)
  {
    const QFBank& bank = banks[i];

    I_AM_ALIVE();
    FreeOnDemandFrame();

    if (!GetAllHashes(bank,ctx.hashVer1Lev0._content, ctx.hashVer2Lev0._content, ctx.hashVer2Lev2._content, full, ReportHashError))
      continue; //failed. Problem reported through ReportHashError

    // get all signatures of given bank
    RString filename = bank.GetOpenName();
    RString path, name;
    const char *ptr = strrchr(filename, '\\');
    if (ptr++)
    {
      path = filename.Substring(0, ptr - cc_cast(filename));
      name = ptr;
    }
    else
    {
      name = filename;
    }

    ForMaskedFile(path, name + RString(".*.bisign"), CheckSignaturesAllLevels, ctx);
  }
  RptF("*** ALL SIGNATURES CHECK END (%d ms)***",GlobalTickCount()-startTime);
  return true;
}
//}@


#if _ENABLE_PBO_PROTECTION

#if _ENABLE_CHEATS
static bool GetUnProtectedPBO(RString fileName)
{
  const char *ext = GetFileExt(fileName); 
  if (!ext || stricmp(ext, ".pbo") != 0)
  {
    // not a PBO, return true
    return true;
  }

  /// open PBO
  QFBank bank;
  if (!bank.openFromFile(fileName))
  {
    return false;
  }

  // find path to write the transformed pbo
  RString outFileName, outFileNameDir;
  const char *ptr = strrchr(fileName, '\\');
  if (ptr)
  {
    RString path = fileName.Substring(0, ptr - cc_cast(fileName));
    RString name = ptr;
    outFileNameDir = path + RString("_SETUP");
    outFileName = outFileNameDir + name;
  }
  else return false;

  /// get protection info
  QFBank::BankHeaderProtectionInfo protInfo;
  bank.GetBankHeaderProtectionInfo(protInfo);

  if (!protInfo.isProtected)
  {
    // not protected, return true
    // close bank
    bank.close();
    return true;
  }

  SRef<IFilebankEncryption> encryptClass(PBOHeaderProtection::CreateSetupHeaderProtection());
  if (encryptClass.IsNull())
  {
    // close bank
    bank.close();
    return false;
  }

  SRef<IFilebankEncryption> decryptClass(bank.CreateSerialHeaderProtection());
  if (decryptClass.IsNull())
  {
    // close bank
    bank.close();
    return false;
  }

  // close bank
  bank.close();

  /// transform PBO
  CreateDirectory(outFileNameDir, NULL);
#if UNPROTECT_PBO_TOTAL
  // get unprotected header content (it is needed, when FileBank is to be used for further addon processing)
  PBOHeaderProtection::TransformResult result = PBOHeaderProtection::TransformPBO(fileName, decryptClass, NULL, outFileName);
#else
  PBOHeaderProtection::TransformResult result = PBOHeaderProtection::TransformPBO(fileName, decryptClass, encryptClass, outFileName);
#endif

  switch(result)
  {
  case PBOHeaderProtection::TROk:
    /// everything was ok
    return true;
    break;
  case PBOHeaderProtection::TRNotPBO:
    /// file is not a PBO, that is ok, just return true
    return true;
    break;
  case PBOHeaderProtection::TRUnexpectedNotEncrypted:
    /// PBO is not protected, that is also ok, just return true
    return true;
    break;
  default:
    {
      RptF("Failed to protect PBO - \"%s\"", cc_cast(fileName));
      return false;
    } 
  }
}

static void UnprotectAllBanks()
{
  BankList::ReadAccess banks(GFileBanks);
  for (int i=0; i<banks.Size(); ++i)
  {
    const QFBank& bank = banks[i];
    GetUnProtectedPBO(bank.GetOpenName());
  }
}
#endif

#if !_SUPER_RELEASE
static bool DebugTestSignature(RString fileName)
{
  AutoArray<DSKey> keys;
  ForMaskedFile("Keys\\", "*.bikey", DebugTestLoadKey, keys);
  ForMaskedFile(RString("Expansion\\Keys\\"), "*.bikey", DebugTestLoadKey, keys);    

  BankList::ReadAccess banks(GFileBanks);
  const QFBank* bank = 0;
  for (int i=0; i<banks.Size(); ++i)
  {
    const QFBank& b = banks[i];
    if (stricmp(b.GetOpenName().Data(), fileName.Data()) == 0)
    {
      bank = &b;
      break;
    }
  }
  if (!bank)
    return false;


  DSSignature sig;
  if (!DataSignatures::FindSignature(sig, bank->GetOpenName(), keys.Data(), keys.Size()))
    return false;

  DSHash hash;
  if (!bank->GetHash(hash._content))
    return false;

  if (!DataSignatures::VerifySignature(hash, sig))
    return false;

  return true;
}

bool DebugTestBankHash(const RString& bankName)
{
  BankList::ReadAccess banks(GFileBanks);
  // find bank
  QFBank* bank = 0;
  int bankIndex = -1;
  for (int i=0; i<banks.Size(); ++i)
  {
    if (stricmp(bankName.Data(), banks[i].GetOpenName().Data()) == 0)
    {
      bank = &(banks[i]);
      bankIndex = i;
      break;
    }
  }

  if (!bank)
    return false;

  SRef<BankHashCalculatorAsync> checkingHash(new BankHashCalculatorAsync(bankIndex));

  int count = 0;
  int maxCount = 1000000000;
  while (!checkingHash->Process() && count < maxCount)
  {
    ++count;
  }

  Assert(count < maxCount);
  return checkingHash->IsValid();
}

#endif // #if !_SUPER_RELEASE

#endif //#if _ENABLE_PBO_PROTECTION

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

#ifdef _WIN32
// help function to read the STRING value from registry key
static RString GetRegistryValue(HKEY hKey, RString valueName)
{
  DWORD valLen = MAX_VALUE_NAME;
  BYTE  val[MAX_VALUE_NAME];
  DWORD type;
  HRESULT result = ::RegQueryValueEx(hKey, valueName, 0, &type, val,&valLen);
  if ( result==ERROR_SUCCESS && type==REG_SZ )
  {
    return RString((char *)val);
  }
  return RString();
}

HRESULT FindExpansionsKey(HKEY &hKey)
{
  HRESULT hr;
  #define ExpansionRegistryPath "\\Expansions"
  #define OPEN_EXPANSION_REG(path) if ( (hr=::RegOpenKeyEx(HKEY_LOCAL_MACHINE, ConfigAppRoot path ExpansionRegistryPath, 0, KEY_READ, &hKey)) == ERROR_SUCCESS ) return ERROR_SUCCESS;
  APP_REGISTRY(OPEN_EXPANSION_REG)
  return hr;
}

void ReadExpansionsFromRegistry(ModInfos &regMods)
{
  regMods.Clear();
  HKEY hKey;
  if ( FindExpansionsKey(hKey) == ERROR_SUCCESS )
  {
    DWORD      subKeysCount;
    // Get the value count. 
    RegQueryInfoKey(hKey,0,0,NULL,&subKeysCount,0,0,0,0,0,0,0);

    if (subKeysCount) // there are some mods
    {
      LogF( "Expansion Mods read from registry:");

      for (int i=0, retCode=ERROR_SUCCESS; i<subKeysCount; i++) 
      {
        DWORD  modNameLen = MAX_VALUE_NAME;
        TCHAR  modName[MAX_VALUE_NAME];
        // Get the value and its data
        retCode = RegEnumKey(hKey,i,modName,modNameLen);
        if ( retCode==ERROR_SUCCESS ) 
        {
          //open the extension key
          HKEY modKey;
          if ( ::RegOpenKeyEx(hKey, modName, 0, KEY_READ, &modKey) == ERROR_SUCCESS )
          {
            // Get the PATH value
            RString modPath = GetRegistryValue(modKey, "PATH");
            if ( !modPath.IsEmpty() )
            {
              const char *MainMacroStr = "%MAIN%";
              const char *DataMacroStr = "%DATA%";

              RString pathFormat = GetRegistryValue(modKey, "PATH_FORMAT");
              if ( pathFormat.IsEmpty() ) pathFormat = DataMacroStr;

              HKEY extensionProductKey;
              // open the registry key of the installed extension
              if ( ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, modPath, 0, KEY_READ, &extensionProductKey) == ERROR_SUCCESS )
              {
                // Prepare dataDirPath from registry key of the installed extension
                RString dataDirPath = GetRegistryValue(extensionProductKey,"DATA");
                RString mainVal = GetRegistryValue(extensionProductKey,"MAIN");
                // %MAIN%
                if ( dataDirPath.IsEmpty() ) 
                {
                    dataDirPath = MainMacroStr;
                }
                // replace possible macro inside DATA key value
                const char *data = cc_cast(dataDirPath);
                const char *replaceStr = strstr(data, MainMacroStr);
                if (replaceStr)
                { // replace it
                  dataDirPath = dataDirPath.Substring(0, replaceStr-data) + 
                                mainVal + (replaceStr+strlen(MainMacroStr));
                }

                //@{ Construct modDirPath using pathFormat
                // Expand:
                //    %DATA% to dataDirPath
                //    %MAIN% to mainVal
                RString modDirPath = dataDirPath;
                data = cc_cast(pathFormat);
                replaceStr = strstr(data, MainMacroStr);
                if (replaceStr)
                { // replace it
                  modDirPath = pathFormat.Substring(0, replaceStr-data) + 
                               mainVal + (replaceStr+strlen(MainMacroStr));
                }
                else { // one cannot use both %MAIN% and %DATA%
                  replaceStr = strstr(data, DataMacroStr);
                  if (replaceStr)
                  { // replace it
                    modDirPath = pathFormat.Substring(0, replaceStr-data) + 
                                 dataDirPath + (replaceStr+strlen(MainMacroStr));
                  }
                }
                //@}

                ModInfo mod;

                RString canDisable = GetRegistryValue(modKey,"CANDISABLE");
                if ( canDisable==RString("0") )
                {
                  mod.cannotDisable = true;
                }

                mod.regName = mod.name = modName;
                mod.fullPath = RString(modDirPath);
                mod.modDir = GetRegistryValue(modKey, "MODDIR");
                if ( mod.modDir.IsEmpty() ) mod.modDir = modName;
                if ( QIFileFunctions::DirectoryExists(mod.fullPath) )
                { // add only existing mods (skip possible registry relics)
                  mod.active = true;
                  mod.origin = ModInfo::ModInRegistry;
                  RString modFilePath = mod.fullPath + "\\mod.cpp";
                  ParseModConfig(modFilePath, mod);  //informations inside mod.cpp are to be considered more important (fresh, updated)
                  // load possible loadafter value from registry
                  RString modKeysList = GetRegistryValue(modKey, "LOADAFTER");
                  if ( !modKeysList.IsEmpty() )
                  {
                    // dependency of one modDir to other mods - value is the list of modDirs separated by ;
                    mod.ParseLoadAfter(modKeysList);
                  }
                  // load possible loadbefore value from registry
                  mod.loadBefore = GetRegistryValue(modKey, "LOADBEFORE");

                  modKeysList = GetRegistryValue(modKey, "REQUIRE");
                  if ( !modKeysList.IsEmpty() )
                  {
                    // dependency of one modDir to other mods - value is the list of modDirs separated by ;
                    mod.ParseRequired(modKeysList);
                  }

                  regMods.AddRegMod(mod);
                  LogF("    RegKey=\"%s\"  Name=\"%s\" Path=\"%s\"", cc_cast(mod.regName), cc_cast(mod.name), cc_cast(mod.fullPath));
                }

                ::RegCloseKey(extensionProductKey);
              }
            }
            ::RegCloseKey(modKey);
          }
        } 
      }
    }
    ::RegCloseKey(hKey);
  }
  for (int i=0; i<regMods.Size(); i++)
  {
    if ( !regMods[i].loadBefore.IsEmpty() )
    {
      // dependency of one modDir to other mods - value is the list of modDirs separated by ;
      regMods.ParseLoadBefore(regMods[i].regName, regMods[i].loadBefore);
    }
  }
  // Update required and requiredBy recursively to contain all dependent mods cumulatively
  regMods.UpdateRequired();
  // regMods.Report();
}
#else
void ReadExpansionsFromRegistry(ModInfos &regMods)
{}
#endif

#include "UI/displayUI.hpp"

// 1. Loads the GModInfosRegistry from registry ConfigApp\\Expansions key
//          ie. "Software\\Bohemia Interactive Studio\\" AppName "\\Expansions"
// 2. Loads the GModInfosProfile from flashpoint cfg.
void LoadModList()
{
  if ( !GModInfosRegistry.Size() ) //not loaded yet
  {
    // load GModInfosRegistry
    ReadExpansionsFromRegistry(GModInfosRegistry);
  }
  { // load from flashpoint cfg
    ParamFile fCfg;
    ParseFlashpointCfg(fCfg);
    ParamEntryPtr modsCfg = fCfg.FindEntry("ModLauncherList");
    if ( !GModInfos.safeModsActivated && modsCfg && modsCfg->IsClass() )
    {
      // Determine possible mods which are marked as disabled in user config (profile)
      ParamEntryPtr cfgDisabledMods = modsCfg->FindEntry("disabled");
      if (cfgDisabledMods && cfgDisabledMods->IsArray())
      {
        for (int j=0, count = cfgDisabledMods->GetSize(); j<count; j++)
        {
          RString disabledMod = (*cfgDisabledMods)[j];
          for (int i=0; i<GModInfosRegistry.Size(); i++)
          {
            if (stricmp(GModInfosRegistry[i].modDir,disabledMod)==0)
            {
              GModInfosRegistry[i].active = false;
              GModInfosRegistry[i].disabled = true;
              break;
            }
          }
        }
      }

      for (int i=0; i<modsCfg->GetEntryCount(); i++)
      {
        ParamEntryVal modCfg = modsCfg->GetEntry(i);
        if (!modCfg.IsClass()) continue;
        int ix = GModInfosProfile.base::Add();
        ModInfo &mod = GModInfosProfile[ix];
        mod.active = true; //all serialized mods are considered as active
        mod.disabled = false;
        mod.name = modCfg >> "name";
        mod.modDir = modCfg >> "dir";
        mod.origin = ModInfo::ModGameDir;
        RString origin = modCfg >> "origin";
        for ( const EnumName *names = GetEnumNames(mod.origin); names->IsValid(); names++ )
        {
          if ( stricmp(names->GetName(),origin)==0 )
          {
            mod.origin = (ModInfo::ModOrigin)(names->GetValue());
            break;
          }
        }

        ParamEntryPtr entry = modCfg.FindEntry("fullPath");
        if (entry)
        {
          mod.fullPath = *entry;
          if ( !QIFileFunctions::DirectoryExists(mod.fullPath) && mod.origin!=ModInfo::ModInConfig )
          {
            mod.origin = ModInfo::ModNotFound;
          }
        }
        else
        { // determine fullPath from mods origin (backward compatibility)
          // check the mod DIR is still present on the path following from mod.origin
          switch (mod.origin)
          {
          case ModInfo::ModOtherDir:
            {
              RString pathToCheck;
              pathToCheck = GetDefaultUserRootDir() + RString("\\") + mod.modDir;
              if ( !QIFileFunctions::DirectoryExists(pathToCheck) )
              {
                mod.origin = ModInfo::ModNotFound;
              }
              else mod.fullPath = pathToCheck;
            }
            break;
          case ModInfo::ModGameDir:
            if ( !QIFileFunctions::DirectoryExists(mod.modDir) )
            {
              mod.origin = ModInfo::ModNotFound;
            }
            else mod.fullPath = mod.modDir;
            break;
          case ModInfo::ModDefault:  mod.defaultMod = true;
          case ModInfo::ModInConfig:
            if ( QIFileFunctions::DirectoryExists(mod.modDir) )
            {
              mod.fullPath = mod.modDir;
            }
            break;
          }
        }
        RString modFilePath = mod.fullPath + "\\mod.cpp";
        ParseModConfig(modFilePath, mod);  //informations inside mod.cpp are to be considered more important (fresh, updated)
      }
    }
    // Merge mods stored in GModInfosRegistry and GModInfosProfile into active mod list in GModInfos
    if (!GModInfos.modsReadOnly)
    {
      AutoArray<ModInfo> defaultMods = GModInfos; // add default mods last (when duplicated, the order must be preserved)
      GModInfos.Copy(GModInfosBeta);
      //add mods read from Registry to "active" mod list
      for (int i=GModInfosRegistry.Size(); --i>=0; )
      {
        const ModInfo &regMod = GModInfosRegistry[i];
        if (!regMod.active) continue;
        // if this mod is already loaded from flashpoint cfg, refresh the info inside GModProfile and use the order in GModProfile array
        int modIx = -1;
        for (int j=0; j<GModInfosProfile.Size(); j++)
        {
          ModInfo &profileMod = GModInfosProfile[j];
          if ( profileMod.ModDirDrivenMod() && stricmp(profileMod.modDir,regMod.modDir)==0 )
          { // the same mods
            profileMod = regMod; // load from Registry should be considered as prior
            modIx = j;
            break; //end GModInfosProfile search
          }
        }
        if (modIx < 0) //add only mods not present in GModInfosProfile
        {
          modIx = GModInfosProfile.Add(GModInfosRegistry[i]); //note: this Add checks for duplicities
        }
      }
      GModInfosProfile.SortByLoadAfterDependencies();
      //add mods loaded from flashpoint cfg to "active" mod list
      for (int i=GModInfosProfile.Size(); --i>=0; )
      {
        if (GModInfosProfile[i].modDir.IsEmpty()) continue;
        GModInfos.Add(GModInfosProfile[i]); //note: this Add checks for duplicities
      }
      //add default mods at last
      for (int i=0; i<defaultMods.Size(); i++) //preserve the original order
      {
#define PERSISTENT_DEFAULT_MODS 1
#if PERSISTENT_DEFAULT_MODS
        bool duplicate = false;
        for (int j=0; j<GModInfos.Size(); j++)
        {
          if ( ( !GModInfos[j].fullPath.IsEmpty() && !stricmp(GModInfos[j].fullPath, defaultMods[i].fullPath) ) //fullPaths present and equal
            || ( GModInfos[j].ModDirDrivenMod() && defaultMods[i].ModDirDrivenMod() && !stricmp(defaultMods[i].modDir,GModInfos[j].modDir) ) //config mods with equal modDir
            ) // default mod
          {
            GModInfos[j].defaultMod = true; // even in registry, it is DEFAULT MOD
            duplicate = true;
            break;
          }
        }
        if ( !duplicate && DisplayModLauncher::ModIndex(defaultMods[i], GModInfos)<0 )
        {
          GModInfos.base::Add(defaultMods[i]); //note: this Add was already checked for duplicities - adding at the end of GModInfos array
        }
#else
        GModInfos.Add(defaultMods[i]); //note: this Add checks for duplicities - adding at the end of GModInfos array
#endif
      }
    }
  }
}

// get the mod info (if present) from config file or create some default (using name of last dir)
// Note: this function is used only for default mods and -mod=xxx mods
void LoadModInfo(RString dir, ModInfo &modInfo)
{
  modInfo.parsed = true;
  modInfo.origin = ModInfo::ModNotFound;
  // We should distinguish between mods like "expansion\beta\expansion" and "expansion" - use full path as mod dir
  modInfo.modDir = dir;
  if (modInfo.modDir.GetLength()>0 && modInfo.modDir[0]=='%') // registry mods can be used by '%' prefix, for example -mod=%someMod
  { // mod should be loaded from Registry
    modInfo.modDir = cc_cast(modInfo.modDir)+1; //skip leading '%'
    modInfo.name = modInfo.modDir; 
    for (int i=0; i<GModInfosRegistry.Size(); i++)
    {
      if ( stricmp(modInfo.name,GModInfosRegistry[i].regName)==0 )
      { // mod found
        modInfo = GModInfosRegistry[i];
        return; //done
      }
    }
    return; //not found, but it should be mod registered in Windows Registry, do not try to find it on different location
  }
  //LoadCfgModInfo(modInfo); //<- Pars is not loaded yet, it is called later
  if (modInfo.origin == ModInfo::ModNotFound)
  {
    RString modFilePath;  //empty
    // find where is the mod located: ModGameDir or ModOtherDir
    if (QIFileFunctions::DirectoryExists(dir))
    {
      modFilePath = dir + "\\mod.cpp";
      modInfo.origin = ModInfo::ModGameDir;
    }
    else 
    {
      RString rootDir = GetDefaultUserRootDir() + RString("\\") + dir;
      if (QIFileFunctions::DirectoryExists(rootDir))
      {
        modFilePath = rootDir + "\\mod.cpp";
        modInfo.origin = ModInfo::ModOtherDir;
      }
    }
    // try to find the name by parsing the possible mod.cpp
    ParseModConfig(modFilePath, modInfo);
    if ( modInfo.name.IsEmpty() )
    { //mod config not found, use last dir as mod name
      modInfo.name = modInfo.modDir;
      const char *lastDir = strrchr(modInfo.name, '\\');
      if (lastDir!=NULL) modInfo.name = lastDir+1;
      lastDir = strrchr(modInfo.name, '/');
      if (lastDir!=NULL) modInfo.name = lastDir+1;
    }
  }
  RString fullPath;
  switch (modInfo.origin)
  {
  case ModInfo::ModOtherDir: fullPath = GetDefaultUserRootDir() + RString("\\"); break;
  case ModInfo::ModGameDir: 
    {
      if ( !IsPathAbsolute(dir) )
      {
        // get current directory to convert relative path to absolute
        int size = GetCurrentDirectory(0, NULL);
        GetCurrentDirectory(size, fullPath.CreateBuffer(size));
        if (fullPath[size - 1] != '\\') fullPath = fullPath + RString("\\");
      }
      break;
    }
  }
  fullPath = fullPath + dir;
  RString PlatformFileName( const char *name );
  fullPath = PlatformFileName(fullPath);
  if ( QIFileFunctions::DirectoryExists(fullPath) )
  {
    modInfo.fullPath = fullPath;
  }
}

//! store mods in structured way
static bool GetModListCallback(RString dir, void *dummy)
{
  if (dir.IsEmpty()) return false;
  ModInfo modInfo(dir);
  LoadModInfo(dir, modInfo);

  modInfo.active = true;  //this callback is used ONLY for active mods
  GModInfos.Add(modInfo);

  return false;
}

//! store mods in structured way
static bool GetModListBetaCallback(RString dir, int &dummy)
{
  if (dir.IsEmpty()) return false;
  ModInfo modInfo(dir);
  LoadModInfo(dir, modInfo);

  modInfo.active = true;  //this callback is used ONLY for active mods
  GModInfosBeta.Add(modInfo);

  return false;
}

#ifdef _WIN32
HRESULT FindConfigAppRegistry(HKEY &hKey)
{
  HRESULT hr;
  #define OPEN_APP_REG(path) if ( (hr=::RegOpenKeyEx(HKEY_CURRENT_USER, ConfigAppRoot path, 0, KEY_READ|KEY_WRITE, &hKey)) == ERROR_SUCCESS ) return ERROR_SUCCESS;
  #define CREATE_APP_REG(path) if ( (hr=::RegCreateKey(HKEY_CURRENT_USER, ConfigAppRoot path, &hKey)) == ERROR_SUCCESS ) return ERROR_SUCCESS;
  // first try opening existing key
  APP_REGISTRY(OPEN_APP_REG)
  // if does not exist yet, create a new one
  APP_REGISTRY(CREATE_APP_REG)
  return hr;
}
#endif

static void InitPlayerName()
{
#if ! _GAMES_FOR_WINDOWS
  // get player name (if not passed as argument)
  if (Glob.header.GetPlayerName().GetLength() == 0)
  {
#if defined _WIN32
    if (!LandEditor)
    {
#if 0
      HKEY key;
      BYTE buf[256];
      DWORD bufSize = sizeof(buf);
      HRESULT keyOK = FindConfigAppRegistry(key);
      if(SUCCEEDED(keyOK) && ::RegQueryValueEx(key, "Player Name", NULL, NULL, buf, &bufSize)==ERROR_SUCCESS && bufSize > 1)
      {
        ERROR_FILE_NOT_FOUND;
        Glob.header.SetPlayerName((const char *)buf);
      }
      else
      {
        Glob.header.SetPlayerName(GetLoginName());
      }
      if (SUCCEEDED(keyOK)) ::RegCloseKey(key);
#else
  ParamFile cfg;
  GameDataNamespace globals(NULL, RString(), false); // no global namespace
  Glob.header.SetPlayerName(GetLoginName()); // MUST be set before ParseUserParams is called, or profile would not be found otherwise
  ParseUserParams(cfg, &globals);
  ConstParamEntryPtr entry = cfg.FindEntry("playerName");
  if (entry && !RString(*entry).IsEmpty()) Glob.header.SetPlayerName(*entry);
#endif
    }
#else
    Glob.header.SetPlayerName("Player");
#endif
  }
#endif
}

static void CheckSafeMode()
{
#if defined(_WIN32) 
  // test whether Shift key is pressed. If so, do not make flashpoint cfg mods active
  short shiftStatus = GetAsyncKeyState(VK_LSHIFT);
  if (shiftStatus!=0)
  {
    GModInfos.safeModsActivated=true;
    RptF("SHIFT pressed during game start. No 'GameConfig'.cfg mods will be loaded!");
  }
#endif
}

#ifndef _WIN32
#include <sys/time.h>
#include <sys/resource.h>
#endif

RString GCmdLine;

#ifdef _WIN32

struct ConnectRequest
{
  RString ip;
  int port;
  RString password;
  bool useMPDisplay;
};

static SRef<ConnectRequest> SConnectRequest;

#if _ENABLE_DEDICATED_SERVER && !_DISABLE_GUI
void TestServerArg(const char *beg, const char *end) 
{
  static const char server[]="server";
  if( *beg=='-' || *beg=='/' || *beg=='+' )
  {
    beg++;
    if (IsOption(beg,end,server))
    {
      DoCreateDedicatedServer=true;
      #if _ENABLE_REPORT
      ResetLogFile = true;
      LogFilename = "server.log";
      #endif
    }
  }
}
#endif

void RequestConnect(RString ip, int port, RString password, bool useMPDisplay = false)
{
  SConnectRequest = new ConnectRequest();
  SConnectRequest->ip = ip;
  SConnectRequest->port = port;
  SConnectRequest->password = password;
  SConnectRequest->useMPDisplay = useMPDisplay;
}

#if _ENABLE_SINGLE_BINARIZE_EXECUTABLE
struct BinarizeArgv
{
  AutoArray<RString> argvArray;
  void Convert(AutoArray<char *> &argv)
  {
    for (int i=0; i<argvArray.Size(); i++) argv.Add(argvArray[i].MutableData());
  }
} binarizeArgv;

void AddToBinarizeArgv(const char *beg, const char *end)
{
  if( end==beg ) return;
  binarizeArgv.argvArray.Add(RString(beg, end-beg));
}
#endif

/*!
Note: we want WinMain to be protected by Fade
this is impossible when there are any DLL functions referenced
all references to Win32 calls must be moved outside

Note: this function is not exited via standard means, and local variable destructors
on the function scope will not be called. Be careful when placing local variables.

\patch_internal 1.01 Date 6/20/2001 by Ondra: Added integrity check.
(see Integrity check)
\patch 1.27 Date 10/15/2001 by Ondra:
- Fixed: Standalone server exe not working.
\patch 1.36 Date 12/12/2001 by Ondra:
- Fixed: Application freezing detection turned off for dedicated server,
it sometimes caused false warnings and premature server termination.
\patch_internal 5101 Date 12/13/2006 by Jirka
- Added: CD Keys on blacklist activate the Fade
\patch 5117 Date 1/16/2007 by Ondra
- Fixed: Argument -noland is now ignored when not running as Buldozer.
\patch 5124 Date 1/29/2007 by Jirka
- New: different progress indicator during game startup
\patch_internal 5129 Date 2/19/2007 by Jirka
- Fixed: game with unknown distribution id is fading
\patch 5149 Date 3/26/2007 by Flyman
- Fixed: The ingame progress bar showed up for a little while when the game was loading and was supposed to use the splash screens instead
*/

  int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
#else
  int main ( int argc, char **argv )
#endif
{
#if _ENABLE_REPORT && defined TRACE_ENABLED
  RString cmdLine = CurrentAppFrameFunctions->GetAppCommandLine();
  const bool isServer = strstr(cmdLine, "-server") != NULL;
  if (isServer)
    TRACE_APPNAME("DayZ-Server");
  else
    TRACE_APPNAME("DayZ-Client");
  TRACE_CONNECT();
  TRACE_SETBUFFERED(0);
  TRACE_SETLEVEL(trace::e_Warning);
#endif

#if _ENABLE_SINGLE_BINARIZE_EXECUTABLE
  // Detect BINARIZE application behaviour based on executable filename
  // Note: it is the same executable as DayZInt.exe but with editbin /SUBSYSTEM:CONSOLE applied
  TCHAR exeNameFull[MAX_PATH];
  GetModuleFileName(0, exeNameFull, MAX_PATH);
  char exeName[256]; GetFilename(exeName, exeNameFull);
  if ( !stricmp(exeName, "binarize") )
  {
    // report binarize.exe commandline
    fprintf(stderr,Format("BINARIZE rev.%d\n",BUILD_NO));
    fprintf(stderr,Format("%s %s\n",exeNameFull, szCmdLine).Data());

    CommandLine cLine(szCmdLine);
    ScanCommandLine(cLine, AddToBinarizeArgv);
    AutoArray<char *> argv;
    argv.Add(exeNameFull);
    binarizeArgv.Convert(argv);
    int main(int argc, char **argv);
    return main(argv.Size(), argv.Data());
  }
#endif

#if _ENABLE_REPORT
  const int stackSize = 2560*1024;
  STACK_INIT(stackSize);
#endif
  // indicate basic init is done and we can perform DDTerm as needed
  DDInitDone = true;


#if 0
    const rlim_t kStackSize = 16 * 1024 * 1024;   // min stack size = 16 MB
    struct rlimit rl;
    int result;

    result = getrlimit(RLIMIT_STACK, &rl);
    if (result == 0)
    {
        if (rl.rlim_cur < kStackSize)
        {
            rl.rlim_cur = kStackSize;
            result = setrlimit(RLIMIT_STACK, &rl);
            if (result != 0)
            {
               fprintf(stderr, "setrlimit returned result = %d\n", result);
            }
            else fprintf(stderr, "setrlimit OK\n", result);
        }
    }
#endif


	#ifdef ENF_ENGINE
		if (!enf::CreateWinEngine("Enfusion game engine", CurrentAppFrameFunctions->GetAppCommandLine().Data(), hInst, false, 4*1024*1024 ))
			return -1;
	#else
		enf::Timer::Init();
	#endif

  SetThreadName(-1,"Main");
  RegisterMainThread();
  // Register for MultiThread2 library purposes (set TLS to use GetCurrentThread on Linux)
  MultiThread::RunningThread<MultiThread::ThreadBase> mtRegistration;

#if _USE_BATTL_EYE_CLIENT
  VMContext::defaultCallback = &battlEyeMonitor;
#endif

#if PROTECTION_ENABLED && _VERIFY_KEY && _VERIFY_BLACKLIST
  // we need them out of following scope
#if !NEW_KEYS
  unsigned keyValueTop;
#endif
  unsigned char dist;
  unsigned keyValue;
#endif

#if _ENABLE_PBO_PROTECTION
  PBOHeaderProtection::AppRegistryPath = GetConfigAppFromRegistry();
#endif

  // all initialization - done to reduce cluttering profile scope hierarchy
  Ref<ProgressHandle> p;
  {
    PROFILE_SCOPE_EX(init,init);
    Glob.InitPhase1();
      #ifdef _WIN32
        SetLocalSettingPaths();
      #endif
      // product configuration
      ReadProductInfo();
    
    const char * volatile version = "VersionMapID" APP_VERSION_TEXT;
    const char * volatile build = "BuildMapID" STRINGIZE(BUILD_NO);
    (void)version;
    (void)build;
    #ifndef _WIN32
      char cmdLine[8*1024];
      char *ptr = cmdLine;
      while ( argc-- > 1 ) {
          strcpy(ptr,argv[1]);
          ptr += strlen(argv[1]);
          *ptr++ = ' ';
          argv++;
      }
      *ptr++ = (char)0;
      *ptr   = (char)0;
      char *szCmdLine = cmdLine;
      HINSTANCE hInst = 0;
      HINSTANCE hPrev = 0;
      int sw = 0;
    #endif
	 #if defined _WIN32
      GDebugExceptionTrap.SetStackBottom(&hInst);
      
      void MemoryMainInit();
      MemoryMainInit();
      
      #if 0 //_ENABLE_PERFLOG
        // make sure the thread is always scheduled to the same CPU
        SetThreadAffinityMask(GetCurrentThread(),1);
      #endif
      // this is needed for profiling functions to work
  #endif
    

    GCmdLine = szCmdLine;
    CommandLine cLine(szCmdLine);
  
    ScanCommandLine(cLine,SingleArgumentRun);

    #if defined _WIN32
      CreateSplash(hInst);
    #endif

    // avoid frequent rehashing
    // there are really many strings in a big app like this
    // experimental value for OFP was around 33000
    RStringB::ReserveTotalCount(16*1024);


    SetSplashProgress(1);

    bool keyOK=false;
    Log("********** WinMain ******** start");

    //HRESULT err=CoInitialize(NULL);
    //if( err!=S_OK && err!=S_FALSE ) ErrorMessage("COM Interface failed.");

    // reserve virtual address space
    //InitMemory(64*1024*1024);

    // HOTFIX: avoid initialization of profile scopes from worker threads
    {
      PROFILE_SCOPE_EX(aiMUp, *);
      PROFILE_SCOPE_EX(aiMPC, aiMap);
      PROFILE_SCOPE_EX(opMUp, aiPath);
      PROFILE_SCOPE_EX(shdCU, *);
    }

  #if _VBS2
      // register vbs2 encryption AES 256 bit
      IFilebankEncryption *CreateEncryptVBS2AESMission(const void *context);
      RegisterFilebankEncryption("VBS2M",CreateEncryptVBS2AESMission);

      IFilebankEncryption *CreateEncryptVBS2AESAddon(const void *context);
      RegisterFilebankEncryption("VBS2",CreateEncryptVBS2AESAddon);
  #endif


    ScanCommandLine(cLine,SingleArgumentPar);
    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    // scan flashpoint.par for arguments
    ParamFile parsFile;
    if (!strcmp(GetFileExt(FlashpointPar),".par"))
    {
      parsFile.Parse(FlashpointPar, NULL, NULL, &globals);
    }
    else
    {
      parsFile.ParsePlainText(FlashpointPar,NULL,&globals);
    }
    ConstParamEntryPtr args = parsFile.FindEntry("Arg");
    if (!args) args = &parsFile;
    // scan flashpoint.par for arguments
    if (args) ScanArguments(*args,SingleArgumentPass1A);
    if (args) ScanArguments(*args,SingleArgumentArgs);
    // scan command line for arguments (pass1) - do it later so that they are preferred
    ScanCommandLine(cLine,SingleArgumentPass1A);

#if _ENABLE_DLC_SUPPORT
    // check if there is an updated version of the DLC in install directory
    DLCTools::CheckDLCForUpdate();
#endif

    if (ProfilePathDefault.GetLength() > 0)
    {
      FlashpointCfg = GetDefaultUserRootDir() + RString("\\") + FlashpointCfg;
    }
  
    // scan flashpoint.par for arguments
    if (args) ScanArguments(*args,SingleArgumentPass1B);
    // scan command line for arguments (pass1)
    ScanCommandLine(cLine,SingleArgumentPass1B);

    const char *preloadIniName = "preload.ini";
    const char *dvdIniName = "dvd.ini";
  
    InitFPU();

    ReadRegistry();

  #ifdef _X360SHADERGEN
    GenerateShaders = true;
  #endif

    #if defined(_X360SHADERGEN)
      CreateEnginePars pars;
      {
        // in
        pars.hInst=hInst;
        pars.hPrev=hPrev;
        if (StartMinimized) sw = SW_MINIMIZE;
        pars.sw=sw;
        pars.w=Glob.config.wantW;
        pars.h=Glob.config.wantH;
        pars.bpp = Glob.config.wantBpp;
        pars.UseWindow = false;
        pars.GenerateShaders = GenerateShaders;

        GEngine=CreateEngineD3DXB(pars);
      }
    #endif

    GFileServer->OpenPreloadLog(preloadIniName);
    GFileServer->OpenDVDLog(dvdIniName);

  #if CHECK_STRINGTABLE
  /*
    {
      static RString languages[] = {"English","French","Italian","Spanish","German","Czech"};
      static int n = sizeof(languages) / sizeof(RString);
      for (int i=0; i<n; i++) CheckStringtable(languages[i]);
    }
  */
    CheckStringtable("English");
  #endif


    // note: code address is constant unless /fixed:no
    // is specified on linker command line
    // note: code size is constant unless code is changed
    // code mostly grows
    // ADDED Patch 1.01 - Integrity check.
    // BEGIN ADDED
    
    // note: external program must be run to "sign" exe
    #if PROTECTION_ENABLED && CHECK_FADE
    unlink("bin\\fade.txt");
    CheckFade();
    ReportFade();
    CheckGenuine();
    #endif

    // END ADDED
    #if 0
      void *leak = new int[32];
    #endif

    #if _ENABLE_REPORT
    Test();
    #endif

    {
      // load stringtable
      GLanguage = "English";
      ParamFile cfg;
      ParseFlashpointCfg(cfg);
      ConstParamEntryPtr cfgLang = cfg.FindEntry("language");
      if (cfgLang) GLanguage = *cfgLang;
      GLanguage = CheckLanguage(GLanguage);
      SetLangID(GLanguage);

  #if _GAMES_FOR_WINDOWS
      InitGamesForWindows(GLanguage);
  #endif

      if( LandEditor )
      {
        extern bool GReplaceProxies;
        //Glob.config.background=false;
  #if _VBS2
        GUseFileBanks=true;
  #else
        GUseFileBanks=true;
  #endif
        GReplaceProxies = false;
      }
      else
      {
        GUseFileBanks = true;
      }

#if _ENABLE_DEDICATED_SERVER && !_DISABLE_GUI
      // scan flashpoint.par for arguments
      if (args) ScanArguments(*args,TestServerArg);
      // scan command line for arguments (pass 2)
      CommandLine cLine(szCmdLine);
      ScanCommandLine(cLine,TestServerArg);
#endif
      // select the profile if not given yet
      InitPlayerName(); 

      //@{ Fill in Mod List (GModInfos)
      // check if safe mode is activated (no flashpoint cfg mods)
      CheckSafeMode();
      // load GModInfosRegistry
      ReadExpansionsFromRegistry(GModInfosRegistry);


      GModInfosBeta.Clear();
      for (int i=0; i<BetaMod.Size(); i++)
      {
        int dummyCtx;
        EnumModDirectories(BetaMod[i],GetModListBetaCallback, dummyCtx);
      }
      BetaMod.Clear();

      // Test of CallStack logging
      //GDebugExceptionTrap.LogFFF("DEBUG Extract CallStack");

      GModInfos.Copy(GModInfosBeta);
      EnumModDirectories(GetModListCallback, NULL);
      if (!GModInfos.modsReadOnly)
      { // there was nothing in commandLine like -mod=xxx
        // Load active mods from user config (mods are driven by ModLauncher)
        // ModPath already contains defaultMods (engine default or defaultMods entry from product file)
        // and previous call to EnumModDirectories has added these mods
        for (int i=0; i<GModInfos.Size(); i++) GModInfos[i].defaultMod=true;
        LoadModList();
      }
      //@}

      if (GUseFileBanks)
      {
        {
          BankList::WriteAccess banks(GFileBanks);
          banks.Clear();
        }
        LoadBanksContext context;

        // we are only opening the pbos here, not parsing any configs or stringtables
        context.dir = dtaFolder;
        context.emptyPrefix = true;
        context.parseConfig = false;
        context.loadNow = true;
        context.permanent = true;
        context.checkSignature = true;
        context.mod = NULL;
        LoadBanksEx(&context);
      }
      
      //CHANGE AFTER RELEASE 1.00 - enable different directories for config files here
      SetSplashProgress(3);

      int dummyCtx;
      ForEachModDirectory(ParseStringtableCallback, dummyCtx);

  #ifdef _PLUGINSYSTEM
      // Load the DLL plugins
      RegisterPlugins();
  #endif

      // Initialize modules
      // - must be after stringtable is loaded now
      // - must be before config and resource is used now

      SetSplashProgress(6);
      
      InitModules();

      // application launcher - call it as soon as possible (but core strings need to be registered because of warning messages)
      if (!AvoidLauncher) ProcessLauncher(szCmdLine);

      GGameState.Compact();

      
      // Initialize Java VM
      static RString jre = "jre";
      static RString jniSources = "JNI/src";
      if (JavaExportInterface) ExportJavaInterface(jniSources);
      InitJavaVM(jre, JavaDebugPort, JavaSuspend);
      if (JavaPerfTest) PerfTestJavaVM();

      LogF("Before config parsing: Total allocated: %d KB",MemoryUsed()/1024);

      DoVerify(ForEachModDirectory(ParseConfig, dummyCtx));

  #if !_VBS3
      GStats.Init();
  #endif
      SetSplashProgress(7);

      SetSplashProgress(8);
      LogF("After config parsing: Total allocated: %d KB",MemoryUsed()/1024);
    }

    //LogF("Buldo: loaded config");
    
    //Log("Config files parsed.");

    #if PROFILE_INIT
      EnableProfiler();
    #endif
    
  #if _ENABLE_DEDICATED_SERVER || _DEDICATED_CLIENT
    netLogValid = false;
  #endif

    Glob.config.Autodetect();
      
    // scan flashpoint.par for arguments
    if (args) ScanArguments(*args,SingleArgument);
    // scan command line for arguments (pass 2)
    SAFinish.Reset();
    ScanCommandLine(cLine,SingleArgument);

    // initialize AutoUpdater
    if ( AutoUpdate )
    {
      if ( !GSvnAUC.Init() )
      {
        if (GSvnAUC.ShutDowning())
          ProcessMessages(false); //exit application
        // we would try to connect regularly
      }
    }

    //@{ _DEDICATED_CLIENT
    // use 127.0.0.1:2302 as default for Dedicated Client
#if defined(_WIN32) 
    if ( IsDedicatedClient() && !AutoUpdate && ClientIP.IsEmpty() )
    {
      ClientIP = "127.0.0.1";
    }
#endif
    //@}

#if INTERNAL_KEYS
    if (customUID<0) //take random
    {
      srand( (unsigned)time( NULL ) );
      customUID = rand()%999+1; //1..999
    }
#endif

    // handle -showScriptErrors option
    GGameState.ShowScriptErrors(ShowScriptErrors);

    // steam not needed during shaders generation
    if 
    (
      !GenerateShaders // already set in SingleArgument
#if _ENABLE_DEDICATED_SERVER
      && !IsDedicatedServer() // dedicated server does not have SteamID
#endif
    )
    {
      if (!SteamUtils() && !InitSteam())
      {
        // if there is a debugger, try it once more
        if (!GDebugger.IsDebugger() || (BreakIntoDebugger("Steam not found"),!InitSteam()))
        {
          ErrorMessage("Unable to locate a running instance of Steam");
          exit(1);
        }
      }
    }

    // after processing -cpuCount
    GJobManager.Init();

    // starts XAudio2 radio system with InternetRadioPollThread
    void CreateInternetRadioSystem();
    CreateInternetRadioSystem();

    //Glob.diskAccessFree=CreateMutex(NULL,FALSE,NULL);
    if( LandEditor )
    {
  #if _ENABLE_BULDOZER
      void SetBinMakeFunctions();
      SetBinMakeFunctions();
  #endif
      // old drawing of roads used in buldozer
      Landscape::_roadsIntoSegment = false;
    }
    else
    {
      // when not running as Buldozer, can cannot disable landscape
      NoLandscape = false;
    }

    //LogF("Buldo: before Init");

    #if _ENABLE_VBS && _ENABLE_ADDONS
      if (IsVBS())
      {
        // register known compression schemes
        IFilebankEncryption *CreateEncryptXOR1024(const void *context);
        RegisterFilebankEncryption("XOR1024",CreateEncryptXOR1024);

        IFilebankEncryption *CreateEncryptVBS1(const void *context);
        RegisterFilebankEncryption("VBS1",CreateEncryptVBS1);
      }
    #endif

  #if _GAMES_FOR_WINDOWS
    // handle the initial notifications to make the profile settings available if player is signed in
    GSaveSystem.Simulate();
    GSaveSystem.CheckInHandled();
    GSaveSystem.SignInChangeHandled();
  #endif

    Glob.InitPhase2();

  #if _VBS3 //we want to be able to overwrite group count/ structure
    GStats.Init();
  #endif

#if _ENABLE_PBO_PROTECTION
      // LUKE TEST
      //bool result = DebugTestBankHash("expansion\\addons\\wheeled_e.pbo");
      //bool result = DebugTestSignature("expansion\\addons\\wheeled_e.pbo");

#if _ENABLE_CHEATS
      if ( UnprotectPbos ) UnprotectAllBanks();
#endif
#endif

    if ( CheckAllSignatures || CheckAllSignaturesFull) DoCheckAllSignatures(CheckAllSignaturesFull);

    AI::InitTables();
    GStats.ClearAll();
    
    SetSplashProgress(9);

#define PROFILE_MODS_HASH 0
#if _ENABLE_PERFLOG && PROFILE_MODS_HASH
    RptF("STARTING GetModsContentHash");
    PROFILE_SCOPE_EX(ModsHash,*);
#endif

    for (int i=0; i<GModInfos.Size(); i++)
    {
      // update possible missing info from CfgMods (during first call the Pars was not loaded yet)
      LoadCfgModInfo(GModInfos[i]);
    }

    GetCanonicalBanksHash();  // hash of all (canonical) bank hashes sorted, hash is converted to Hexadecimal string representation
    GetModsContentHash();
    GModInfos.Report();

#if _ENABLE_PERFLOG && PROFILE_MODS_HASH
    int spent = PROFILE_SCOPE_NAME(ModsHash).TimeSpentNorm();
    RptF("TOTAL TIME GetModsContentHash: %.2f ms", spent/100.0f);
#endif

    #if 0
      //SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
      //SetProcessWorkingSetSize(GetCurrentProcess(),8*1024*1024,16*1024*1024);
    #endif

    
    //ShowWSSize();
    
      
      // version logging
  #ifdef NET_LOG
      NetLog("%s version: %s",APP_NAME_SHORT,APP_VERSION_TEXT);
  #endif

    // Save instance handle for DialogBoxes
      #ifdef _WIN32
    hInstApp = hInst;
      #endif
      

    SetSplashProgress(10);


    //LogF("Buldo: before Create Engine");

    #if _ENABLE_REPORT
    GDebugConsole->Register(&SDebugCommandScript);
    #endif
    
   {
     ParamFile cfg;
     ParseFlashpointCfg(cfg);
     MouseEx=!cfg.ReadValue("DisableMouseExclusive",false);
     BuldozerMouseSpeed=cfg.ReadValue("mouseSpeed",1.0f);
     VisitorMouseSpeed=cfg.ReadValue("visitorMouseSpeed",1.0f);
     if (!UseWindowByCommandLine)
     {
       UseWindow = cfg.ReadValue("Windowed",false);
     }
   }

	// we cannot set pars on PC until command line was parsed
  CreateEnginePars pars;
  // in
  pars.hInst=hInst;
  pars.hPrev=hPrev;
  pars.sw=sw;
  pars.w=Glob.config.wantW;
  pars.h=Glob.config.wantH;
  pars.bpp = Glob.config.wantBpp;
  pars.GenerateShaders = GenerateShaders;
  pars.UseWindow=UseWindow;

  //@{ _DEDICATED_CLIENT
  #if _ENABLE_DEDICATED_SERVER || _DEDICATED_CLIENT
  if ( IsDedicatedClient() )
  {
    GDebugger.PauseCheckingAlive();
    CreateDedicatedClientConsoleWindow(hInst,pars);
    DestroySplash();
  } //@}
  else
  #endif
  {
  //{ DEDICATED SERVER SUPPORT
  #if _ENABLE_DEDICATED_SERVER
    if (DoCreateDedicatedServer)
    {
      GDebugger.PauseCheckingAlive();
      CreateDedicatedServer(hInst,pars);
      DestroySplash();
    }
    else
  #endif

  #if !_DISABLE_GUI
    {  

      // destroy splash window before creating a real one
      DestroySplash();

      avoidUserInput = AUITrue;

    #if _ENABLE_DX10
      if (ForceDX9)
      {
        GEngine=CreateEngineD3D9(pars);
      }
      else
      {
      GEngine=CreateEngineD3DT(pars);
      }
    #else
      GEngine=CreateEngineD3D9(pars);
    #endif
        if (GenerateShaders)
        {
    #if defined(_WIN32) 
          CallDestroyWindow();
    #endif
          DDTerm();
          exit(0);
        }

        if (!GEngine)
        {
          ErrorMessage("Error creating Direct3D 9 Graphical engine");
          exit(1);
        }
  
      GEngine->Init();
      avoidUserInput = AUIUnititialized;

			#ifdef ENF_ENGINE
				enf::CStr cfgFile = enf::GetEngine()->GetParam("cfg");
				if(cfgFile == NULL)
				{
					cfgFile = "DayZSetting.xml";
				}
								
				enf::CStr profileName = "";

				if (!enf::GetEngine()->Create(GEngineDD->GetHWND(), cfgFile.cstr(), profileName.cstr()))
					return -1;
			#endif
			
      // Initialize structured texts drawing
      {
        void InitDefaultDrawTextAttributes(ParamEntryVal cls);
        ConstParamEntryPtr entry = Pars.FindEntry("DefaultTextAttributes");
        if (entry) InitDefaultDrawTextAttributes(*entry);
      }
    }
  #else
      ; // because else before #if !_DISABLE_GUI needs some statement
  #endif //_DISABLE_GUI
  } //_DEDICATED_CLIENT

    #if 0
      void TestPAA();
      TestPAA();    
    #endif
    

    // The window handle should be initialized now, thus we can register plugins
  #ifdef _PLUGINSYSTEM
    // Load the DLL plugins
    RegisterPlugins();
  #endif

    // out
    UseWindow=pars.UseWindow;

    //LogF("Buldo: before Create World");

    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

  #if PROTECTION_ENABLED
    CreateFadeSemaphores();
  #endif


  #if _ENABLE_MP
    void initNetwork();
    initNetwork();

  #endif

    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    GWorld=new World(GLOB_ENGINE,LandEditor);
    I_AM_ALIVE();

    // check of file signatures
  #if _ACCEPT_ONLY_SIGNED_DATA
    {
    static AcceptedKey acceptedKeys[] = ACCEPTED_KEYS;
    const int acceptedKeysCount = lenof(acceptedKeys);

    DSKey keys[acceptedKeysCount];
    for (int i=0; i<acceptedKeysCount; i++)
    {
      keys[i]._name = acceptedKeys[i].name;
      keys[i]._content.Realloc(KeySize);
      memcpy(keys[i]._content.Data(), acceptedKeys[i].content, KeySize);
    }

      BankList::ReadAccess banks(GFileBanks);
      for (int i=0; i<banks.Size(); i++)
    {
        QFBank &bank = banks[i];
      DSSignature signature;
      DSHash hash;
      if (
        !DataSignatures::FindSignature(signature, bank.GetOpenName(), keys, acceptedKeysCount) ||
        !bank.GetHash<HashCalculator>(hash._content, signature.Version(), 0) ||
        !DataSignatures::VerifySignature(hash, signature))
      {
#if SIGNATURES_TEST_ONLY
        LogF("Warning: Signature of %s is wrong.", cc_cast(bank.GetOpenName()));
#else
        if (CheckAllSignatures)
        {
           RString details = Format("\r\nSignature of %s is wrong.", cc_cast(bank.GetOpenName()));
           RString rpt = LocalizeString(IDS_HASH_WRONG) + details;
           ErrorMessage(rpt);
        }
        else
          ErrorMessage(LocalizeString(IDS_HASH_WRONG));
#endif
      }
    }
    static const char *additionalSignedFiles[] = ADDITIONAL_SIGNED_FILES;
    for (int i=0; i<lenof(additionalSignedFiles); i++)
    {
      // hotfix - empty array is not supported, use {0} instead
      if (!additionalSignedFiles[i]) continue;
      DSSignature signature;
      DSHash hash;
      if (
        !DataSignatures::FindSignature(signature, additionalSignedFiles[i], keys, acceptedKeysCount) ||
        !DataSignatures::GetHash(hash, additionalSignedFiles[i]) ||
        !DataSignatures::VerifySignature(hash, signature))
      {
#if SIGNATURES_TEST_ONLY
        LogF("Warning: Signature of %s is wrong.", cc_cast(additionalSignedFiles[i]));
#else
        ErrorMessage(LocalizeString(IDS_HASH_WRONG));
#endif
      }
    }

    GWorld->CheckHash(-1, CHRDemo);

    I_AM_ALIVE();
    }
  #endif


    GlobPreloadedTexturesPreload(true);
    GEngine->CreateAllPushBuffers();
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    PreloadTexturesByConfig();
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    PreloadConfigByConfig();
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    GChatList.Load(Pars >> "RscChatListDefault");

    // Attach BI debugger
    avoidUserInput = AUITrue;
    CurrentDebugEngineFunctions->Startup();
    avoidUserInput = AUIUnititialized;
  
  #if _VERIFY_KEY
    keyOK = VerifyKey(); //bistudio key verification
  #else
    keyOK = true;
  #endif

#if _VBS3
    int customer = Pars.ReadValue("HASPCustomerId", -1);
# if  _TIME_EXPIRY // time expiry and HASP feature check
    if(!CheckTrialVersion(customer)) ErrorMessage("VBS2 Expired. Please contact support@vbs2.com");
# endif
    if(LVCWanted)
    {
      if(CheckFeature(customer,2)) Glob.vbsEnableLVC=true;
      else WarningMessage("LVC feature not enabled.\nPlease contact support@vbs2.com");
    }
#endif

    GLandscape=new Landscape(GEngine,GWorld,false);
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    if (!NoSplash && !LandEditor && !DoCreateServer
#if defined(_WIN32) 
      && ClientIP.GetLength() == 0 
#endif
      && !ContentDownload
      && !IsDedicatedClient()
      //{ DEDICATED SERVER SUPPORT
  #if _ENABLE_DEDICATED_SERVER
      && !DoCreateDedicatedServer
  #endif
      //}
    )
    {
      RString startupScript = Pars >> "startupScript";
      if (startupScript.GetLength() > 0)
      {
        // start loading script
        GameValue val;
        ProgressScript = new Script(startupScript, val, GWorld->GetMissionNamespace()); // mission namespace
        ProgressScript->WaitUntilLoaded();
      }
    }

    {
      RString progressText;
  #if _ENABLE_DEDICATED_SERVER
      if (DoCreateDedicatedServer) progressText = LocalizeString(IDS_CREATE_SERVER);
      else
  #endif
        progressText = LocalizeString(IDS_LOAD_INTRO);
      // if there is no script, we want to avoid clearing the surface
      IProgressDisplay *CreateDisplayProgress(RString text, RString resource);
      p = ProgressStartExt(ProgressScript.IsNull(), CreateDisplayProgress(progressText, "RscDisplayStart"), true, 0); // do not rewrite PersistDisplay
    }

    GWorld->Init(LandEditor);
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    if (!InitCommand.IsEmpty())
    {
      #if _ENABLE_CHEATS || _ENABLE_AUTOTEST
      // no init processing in AutoTest mode
      if (AutoTest.IsEmpty())
      #endif
      {
        GameState *gstate = GWorld->GetGameState();
        DoAssert(gstate);
        gstate->Execute(InitCommand, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
      }
    }
      

    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();
    
    // start init progress

    //LogF("Buldo: before InitWorld");

    if (!LandEditor)
    {
      GWorld->InitLandscape(GLandscape);
      I_AM_ALIVE();
      if (GProgress) GProgress->Refresh();
      VehicleTypes.Preload();
      // flush all banks, so that we can unload them if possible
      if (GFileServer) GFileServer->FlushBank(NULL);
      // we have already loaded all non-default types
      // unlock all non-default addons 
      //AddonToPrefix.ForEach(UnlockAddon);
    }
    else
    {
      GWorld->ParseCfgWorld(Pars>>"CfgWorlds">>"DefaultWorld");
    }

  #if PROTECTION_ENABLED && _VERIFY_KEY
    if (!CHECK_PROTECTION())
    {
      DoFade = true;
#if CHECK_FADE
      FILE *f = fopen("bin\\fade.txt","a");
      if (f)
      {
        fprintf(
          f, "FADE SecuROM is missing\n"
        );
        fclose(f);
      }
#endif
    }
  #endif

    InitWorld();
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();

    #if 0
    for (AddonInfoMap::Iterator iterator(AddonToPrefix); iterator; ++iterator)
    {
      const AddonInfo &info = *iterator;
      LogF("%s in %s",(const char *)info.GetName(),(const char *)info.GetPrefix());
    }
    #endif

  #if _VERIFY_KEY_EXT
    if (keyOK) keyOK = VerifyKeyExt();
  #endif
    
    //ShowPic("Data2D\\TitlScr3.pcc",5000);

    //GEngine->TextBank()->Preload();
    //GEngine->TextBank()->ForceRelight();

    #if PROFILE_INIT
      DisableProfiler();
    #endif

    if( !keyOK )
    {
      ErrorMessage("Bad serial number given in Setup");
      exit(1);
    }

    #if PROFILE_RUN
      EnableProfiler();
    #endif

  #if PROTECTION_ENABLED && _VERIFY_KEY && _VERIFY_BLACKLIST
    {
      CDKey key = GetGCDKey();
      #define MANGLE_64B 0x1fd03ee2dc2cae44LL
      #define CDKEY_MAXBITS 28

      #if NEW_KEYS
        unsigned int keyValueReal = *(unsigned int *)key.GetBinaryMessage();
        dist = keyValueReal & ((1 << DISTR_BITS) - 1);
      #else
        unsigned keyValueReal = key.GetValue32b(0, 4);
        keyValueTop = key.GetValue32b(4, CDKeyIdLength-4);
        // only lower byte is needed 
        dist = unsigned char(keyValueReal);
      #endif
      // blacklisted keys are actually only 32b - larger keys do not exist at all - see CDKEY_MAXBITS
      keyValue = keyValueReal ^ unsigned(MANGLE_64B);

  #if CHECK_FADE
      #define CHECK_CDKEY(x) if (((x) ^ unsigned(MANGLE_64B)) == keyValue) \
      { \
        DoFade = true; \
        FILE *f = fopen("bin\\fade.txt","a"); \
        if (f) \
        { \
          fprintf(f, "FADE CD Key blacklisted\n"); \
          fclose(f); \
        } \
      }
  #else
      #define CHECK_CDKEY(x) if (((x) ^ unsigned(MANGLE_64B)) == keyValue) DoFade = true;
  #endif
      #if !NEW_KEYS
        #define CHECK_CDKEY_RANGE() ( keyValueTop==0 && (keyValue>>CDKEY_MAXBITS)==(unsigned(MANGLE_64B)>>CDKEY_MAXBITS) )
      #else
        #define CHECK_CDKEY_RANGE() ( true )
      #endif
      #define CHECK_DISTRIBUTION_VALID(dist) ( dist<NDistributions )

      CDKEY_BLACKLIST(CHECK_CDKEY)
    }

  #else
    #define CHECK_CDKEY_RANGE() ( true )
    #define CHECK_DISTRIBUTION_VALID(dist) (true)
  #endif
  
    CanRender=true;
    GLOB_ENGINE->SetTimeStartGame(GlobalTickCount());
  }

  ObjectActions.Load(Pars >> "CfgObjectActions");
  SurfaceActions.Load(Pars >> "CfgSurfaceActions");
  Recipes.Load(Pars >> "CfgRecipes");

//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (DoCreateDedicatedServer)
  {
    // update possibly missing CfgMods info in GModInfos mods list
    for (int i=GModInfos.Size()-1; i>=0; i--)
    {
      LoadCfgModInfo(GModInfos[i]);
    }
    bool CreateDedicatedServer(SessionType sessionType, RString config);
#ifdef _WIN32
    CreateDedicatedServer(STSystemLink, ServerConfig);
#else
    if ( CreateDedicatedServer(STSystemLink, ServerConfig) )
#endif
    ProgressFinish(p);
      //TODO: allow some quit command from console
    DedicatedServerLoop();
    Fail("Unreachable");
    return 0;
  }
  else
#endif // _ENABLE_DEDICATED_SERVER
//}

  // continue initialization - done to reduce cluttering profile scope hierarchy
  {
#if _LOAD_ONLY_SIGNED_DATA
    // initialize signatureCheckerThread
    if (UseFilesAsync())
    {
      signatureCheckerThread = new SignatureCheckerThread();
      signatureCheckerThread->GetReady();
    }
    else
    {
      syncSigChecker = new SynchronousSignatureChecker();
    }
#endif

    PROFILE_SCOPE_EX(init,init);
#if !_DISABLE_GUI
    if (DoCreateServer)
    {
      CDP_DECL void __cdecl CDPCreateServer();
      CDPCreateServer();
    }
    else if (ClientIP.GetLength() > 0 && !LandEditor)
    {
      CDP_DECL void __cdecl CDPCreateClient(RString ip, int port, RString password, RString serverName);
      CDPCreateClient(ClientIP, NetworkPort, NetworkPassword, "");
    }
    else
    if (!LandEditor)
    {
      GLOB_WORLD->StartIntro(Glob.header.worldname);
    }
#endif

    // once init is done, start watching thread
  //{ DEDICATED SERVER SUPPORT
  #if _ENABLE_DEDICATED_SERVER
    if (!DoCreateDedicatedServer)
  #endif
  //}
    {
      GDebugger.ResumeCheckingAlive();
    }

    // finish init progress
    if (ProgressScript)
    {
      while (ProgressScript)
      {
        I_AM_ALIVE();
        if (GProgress) GProgress->Refresh();
        if (ProgressScript->IsTerminated())
        {
          ProgressFinish(p);
          ProgressScript.Free();
        }
      }
    }
    else
    {
      ProgressFinish(p);
    }

  # if PROTECTION_ENABLED
    // do not allow unknown distributions
  #   if ALWAYS_FAIL_CDKEY
        bool fadeCdKey = true;
  #   else
        bool fadeCdKey = !CHECK_DISTRIBUTION_VALID(dist) || !CHECK_CDKEY_RANGE();
  #   endif
  # endif
    
    GWorld->SetTitleEffect(NULL);
    GWorld->ClearCutEffects();

    // the init script is not started yet. Start it now.
    
    if (GWorld->GetMode()==GModeIntro)
    {
      void RunInitIntroScript();
      RunInitIntroScript();
    }
    
  #if PROTECTION_ENABLED
    SetFadeEvent(fadeCdKey);
  #endif

    #if _ENABLE_REPORT
    DebugOpportunityAfterInit();
    #endif
    #if 0
      CalculateConfigCRC("");
      CalculateConfigCRC("cfg/CfgVehicles");
      CalculateExeCRC(0,INT_MAX);
    #endif

  }
  
//@{ _DEDICATED_CLIENT
  if ( IsDedicatedClient() )
  {
    // update possibly missing CfgMods info in GModInfos mods list
    for (int i=GModInfos.Size()-1; i>=0; i--)
    {
      LoadCfgModInfo(GModInfos[i]);
    }
    // NOTE: CreateDedicatedClient is to be called from SvnAutoUpdater processing
    ProgressFinish(p);
      //TODO: allow some quit command from console
    DedicatedClientLoop();
    Fail("Unreachable");
    return 0;
  }
  //@} _DEDICATED_CLIENT
  else
  {
  //LogF("Buldo: Running");
  // Polling messages from event queue until quit
  //MemoryFast();
  #ifdef _WIN32
  while( !CloseRequest )
  {
    #if _ENABLE_PERFLOG
      SectionTimeHandle startTime = StartSectionTime();
    #endif
    {
      PROFILE_SCOPE_EX_HIER(total,*);
      
      ProcessMessages(false);
    } // total scope must be terminated before processing it

    #if _ENABLE_PERFLOG

      if (GEngine && GWorld /*&& GWorld->IsDisplayEnabled()*/ && GWorld->IsSimulationEnabled() && LimitFpsCoef==0)
      {
        const float step = 1.0717734625; // pow(2,1/10) - 10 steps change 2x
        static float limitSlowFrame = 0.1;
        static float limitBottleneck = 0.05*(step*step*step);
   
        float time = GetSectionTime(startTime);
        PerfLogFrame(time,limitSlowFrame,limitBottleneck);
      }
      else
      {
        // no slow frame detection when there is no simulation
        float time = GetSectionTime(startTime);
        PerfLogFrame(time,FLT_MAX,FLT_MAX);
      }
    #endif

  }
  #if _ENABLE_REPORT
    unsigned sUsage = STACK_METER(stackSize);
    if ( sUsage )
      LogF("StackMeter(WinMain,%dKB): %u KB",stackSize/1024,sUsage/1024);
    else
      LogF("StackMeter(WinMain,%dKB) overflow",stackSize/1024);
  #endif
  #endif
  //MemoryGood();
  
  #if PROFILE_RUN
    DisableProfiler();
  #endif

  ValidateQuit=true;
  LogF("Shutdown");

  #if !defined _WIN32
    DDTerm();
    #if PROFILE_EXIT
      DisableProfiler();
    #endif
    exit(0);
  #else
  PostDestroy();
  for(;;)
  {
    ProcessMessages(false);
  }
  #endif
  #if _MSC_VER && _MSC_VER<1300
    #ifndef _INTEL_CC
      LogF("WinMain return 0");
      return 0;
    #endif
  #endif
}
  //@} _DEDICATED_CLIENT
}

#if _VERIFY_KEY && NEW_KEYS
#include <Wincrypt.h>
static bool AcquireContext(HCRYPTPROV *provider, bool privateKeyAccess)
{
  DWORD flags = privateKeyAccess ? 0 : CRYPT_VERIFYCONTEXT;
  if (CryptAcquireContext(provider, NULL, NULL, PROV_RSA_FULL, flags)) return true;
  // create a new key container
  if (GetLastError() == NTE_BAD_KEYSET)
  {
    flags |= CRYPT_NEWKEYSET;
    if (CryptAcquireContext(provider, NULL, NULL, PROV_RSA_FULL, flags)) return true;
  }
  return false;
}

/// check the hash of CD Key
__forceinline unsigned char GetKeySignature(unsigned int message)
{
  unsigned char hashByte = 0;

  // initialize the MS CryptoAPI
  HCRYPTPROV provider = NULL;
  if (AcquireContext(&provider, false))
  {
    // calculate the SHA hash
    HCRYPTHASH handle = NULL;
    if (CryptCreateHash(provider, CALG_SHA, NULL, 0, &handle))
    {
      if (CryptHashData(handle, (BYTE *)&message, sizeof(message), 0))
      {
        // export the hash
        DWORD size = 0;
        if (CryptGetHashParam(handle, HP_HASHVAL, NULL, &size, 0)) 
        {
          Temp<unsigned char> buffer(size);
          if (CryptGetHashParam(handle, HP_HASHVAL, buffer.Data(), &size, 0))
          {
            hashByte = buffer[0];
          }
        }
      }
    }
    if (handle) CryptDestroyHash(handle);
  }

  if (provider) CryptReleaseContext(provider, 0);
  return hashByte;
}

#endif

#if _VERIFY_KEY

//@{ A2 Family
static const char MappedProductA2[]="A2";
static const char MappedProductA2Free[]="A2F";
//@}

//@{ OA Family
static const char MappedProductOA[]=""; // do not append anything for OA to maintain best backward compatibility during the conversion
static const char MappedProductRFT[]="RFT";
static const char MappedProductOADECADE[]="AX";
//@}

static const char MappedProductLiberation[]="Lib";

static const char MappedProductTKOH[]="TH";

static const char *MapProduct(ProductId productId)
{
  switch (productId)
  {
    // each product needs to provide MappedProduct values above
    #define MAP_PRODUCT(productId) case productId: return Mapped##productId;
    PRODUCT_ID(MAP_PRODUCT)
  }
  return "";
}
  #endif

// Product name from private CD key
RString GetProductIdString()
{
#if _VERIFY_KEY
# if  INTERNAL_KEYS
  return RString();
# elif NEW_KEYS
  CDKey key = GetGCDKey();
  const unsigned char *cdkey = key.GetBinaryMessage();
  int productId = DecodeProductId(cdkey);
  return MapProduct(ProductId(productId));
# else
  return RString();
# endif
#else
  return RString();
#endif
}

// Public key from private CD key
RString GetPublicKey()
{
#if _VERIFY_KEY
# if  INTERNAL_KEYS
  if (!forcePublicKey.IsEmpty()) return forcePublicKey;
  if(customUID > 0) return Format("DEV%d",customUID);
  else return "999";
# elif NEW_KEYS
  CDKey key = GetGCDKey();
  const unsigned char *cdkey = key.GetBinaryMessage();
  unsigned int message = *(unsigned int *)cdkey;
  if (GetKeySignature(message) != cdkey[4])
  {
#if PROTECTION_ENABLED
    DoFade = true;
#if CHECK_FADE
    FILE *f = fopen("bin\\fade.txt","a");
    if (f)
    {
      fprintf(
        f, "FADE CD Key signature invalid (%x!=%x)\n",
        (int)GetKeySignature(message), (int)cdkey[4]
      );
      fclose(f);
    }
#endif
#endif
  }
  int val = message & ((1 << (PLAYER_BITS + DISTR_BITS)) - 1);

  int productId = DecodeProductId(cdkey);

  char buffer[256];
  _i64toa(val, buffer, 10);
  strcat(buffer,MapProduct(ProductId(productId)));
  return buffer;
# else
  __int64 val = GCDKey.GetValue(0, CDKeyIdLength);
  char buffer[256];
  _i64toa(val, buffer, 10);
  return buffer;
# endif
#else
  if (!forcePublicKey.IsEmpty()) return forcePublicKey;
  if(customUID > 0) return Format("%d",customUID);
  // use SteamID as a player identification
  if (!SteamUser()) return "ERROR";
  CSteamID id = SteamUser()->GetSteamID();
  return Format("%llu", id.ConvertToUint64());
#endif
}

// Distribution id from private CD key
int GetDistributionId()
{
#if _VERIFY_KEY && _ENABLE_DISTRIBUTIONS
# if NEW_KEYS
  CDKey key = GetGCDKey();
  const unsigned char *cdkey = key.GetBinaryMessage();
  unsigned int message = *(unsigned int *)cdkey;
  if (GetKeySignature(message) != cdkey[4])
  {
#if PROTECTION_ENABLED
    DoFade = true;
#endif
#if CHECK_FADE
    FILE *f = fopen("bin\\fade.txt","a");
    if (f)
    {
      fprintf(
        f, "FADE CD Key signature invalid (%x!=%x)\n",
        (int)GetKeySignature(message), (int)cdkey[4]
        );
      fclose(f);
    }
#endif
  }
  switch (message & ((1 << DISTR_BITS) - 1))
# else
  // the last byte of id
  switch (GCDKey.GetValue(0, 1))
#endif
  {
  case DistTKOHUS:
    return 1485;
  case DistTKOHEnglish:
    return 1486;
  case DistTKOHGerman:
    return 1487;
  case DistTKOHPolish:
    return 1488;
  case DistTKOHRussian:
    return 1489;
  case DistTKOHCzech:
    return 1490;
  case DistTKOHEuro:
    return 1491;
  case DistTKOHInternational:
    return 1491;
  case DistTKOHJapanese:
    return 1493;
  case DistTKOHInternal:
    return 1494;
  case DistTKOHItalian:
    return 1495;
  case DistTKOHSpanish:
    return 1496;
  default:
    return 0; // Unknown
  }
#elif _DEMO
  return 1459; // Armed Assault 2 (US)
#else
  return 0; // Unknown
#endif
}

// Text form of CD key for GameSpy check
RString GetCDKey()
{
#if _VERIFY_KEY
  KeyRepresentation<char,32> buffer;
  GetGCDKey().GetTextMessage(buffer.cdkey);
  //RptF(" GetCDKey: \"%s\"", buffer);
  return buffer.cdkey;
#else
  return RString();
#endif
}

#include "appFrameExt.hpp"

// MessageBox does not work when in exclusive mode

/*!
\patch 1.23 Date 9/11/2001 by Ondra
- New: context.bin create when game terminates with error message box.
\patch 1.28 Date 10/18/2001 by Ondra
- New: Error messages logged to Flashpoint.rpt.
*/

void OFPFrameFunctions::ErrorMessage(const char *format, va_list argptr)
{
  static int avoidRecursion=0;
  if( avoidRecursion++!=0 ) return;

  GDebugger.NextAliveExpected(15*60*1000);

  BString<256> buf;
  vsprintf( buf, format, argptr );

  // kill direct draw
#ifdef _WIN32
  GDebugExceptionTrap.SaveContext("ErrorMessage");
#endif
#if _ENABLE_REPORT
  // error which will terminate the game should break into the debugger
  ::ErrF("ErrorMessage: %s",(const char *)buf);
#else
#ifdef _WIN32
  GDebugExceptionTrap.LogLine("ErrorMessage: %s",buf.cstr());
#else
  // !!! not yet !!!
#endif
#endif
#if defined(_WIN32) 
  if (hwndApp) DestroyWindow((HWND)hwndApp);
#endif
  DDTermError();
  // kill main application window
  //MessageBox(hwndApp,buf,AppName,MB_OK);
  //WinDestroy();
  // show message
#ifdef _WIN32
  //if( HideCursor ) ShowCursor(TRUE);

  // convert message from UTF-8 to Unicode
  int len = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
  wBuffer.Resize(len);
  MultiByteToWideChar(CP_UTF8, 0, buf, -1, wBuffer.Data(), len);
  wBuffer[len - 1] = 0; // make sure result is always null terminated

  // convert application name from UTF-8 to Unicode
  len = MultiByteToWideChar(CP_UTF8, 0, AppName, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wAppName, 1024);
  wAppName.Resize(len);
  MultiByteToWideChar(CP_UTF8, 0, AppName, -1, wAppName.Data(), len);
  wAppName[len - 1] = 0; // make sure result is always null terminated

  if (!Silent) //when autotest is running, we want no error messages
    MessageBoxW(NULL,wBuffer.Data(),wAppName.Data(),MB_OK|MB_ICONERROR);
  SetCursor(LoadCursor(NULL,IDC_WAIT));
  TerminateProcess(GetCurrentProcess(),1);
#else
  fputs(buf,stderr);
  fputc('\n',stderr);
#endif
}

#define ENABLE_WARNINGS 1
#define SINGLE_WARNING 1

static ErrorMessageLevel WarningLevel = EMNote;
static RString WarningText;

ErrorMessageLevel GetMaxError()
{
  return WarningLevel;
}

RString GetMaxErrorMessage()
{
  return WarningText;
}

/*!
\patch 1.50 Date 4/11/2002 by Ondra
- Change: First warning during each mission is shown.
Only first warning after game was launched was shown before.
*/

void ResetErrors()
{
  WarningLevel = EMNote;
  WarningText = RString();
}

/*!
  \patch_internal 1.01 Date 06/21/2001 by Jirka
  - reimplementation of Warning message
  - if GWorld exist, display in game dialog with warning
  - otherwise, windows Message Box is performed
*/

void WarningMessageLevel( ErrorMessageLevel level, const char *format, va_list argptr)
{
  BString<1024> buf;
  vsprintf( buf, format, argptr );

  RptF("Warning Message: %s",(const char *)buf);

#if 1 // _ENABLE_REPORT

  #if _DEMO
  // do not report missing data in demo
  if (level < EMWarning) return;
  #endif

  #if SINGLE_WARNING
    if( WarningLevel>=level ) return;
    WarningLevel = level;
  #endif

  static int avoidRecursion = 0;
  if (avoidRecursion> 0) return;

  avoidRecursion++;
  GDebugger.NextAliveExpected(15*60*1000);

  #if SINGLE_WARNING
    WarningText = buf.cstr();
  #endif

  // CHANGED in Patch 1.01
  bool result = false;
  // CHANGED in Patch 1.01
  if (GWorld)
  {
    if (!Silent) //when autotest is running, we want no error messages
      GWorld->CreateWarningMessage(buf.cstr());
    result = true;
  }
  
  // kill main application window
  if (!result)
  {
        #ifdef _WIN32
    //if( HideCursor ) ShowCursor(TRUE);
    DWORD icon = level<=EMWarning ? MB_ICONWARNING : MB_ICONERROR;

    // convert message from UTF-8 to Unicode
    int len = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
    wBuffer.Resize(len);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wBuffer.Data(), len);
    wBuffer[len - 1] = 0; // make sure result is always null terminated

    // convert application name from UTF-8 to Unicode
    len = MultiByteToWideChar(CP_UTF8, 0, AppName, -1, NULL, 0);
    AUTO_STATIC_ARRAY(wchar_t, wAppName, 1024);
    wAppName.Resize(len);
    MultiByteToWideChar(CP_UTF8, 0, AppName, -1, wAppName.Data(), len);
    wAppName[len - 1] = 0; // make sure result is always null terminated

    if (!Silent) //when autotest is running, we want no error messages
      MessageBoxW(NULL,wBuffer.Data(),wAppName.Data(),MB_OK|icon);
    //if( HideCursor ) ShowCursor(FALSE);
        #else
        puts(buf);
        #endif
  }
  // terminate
  avoidRecursion--;
#endif
}

void OFPFrameFunctions::WarningMessage( const char *format, va_list argptr)
{
  WarningMessageLevel(EMWarning,format,argptr);
}

/*!
\patch 1.50 Date 4/11/2002 by Ondra
- Changed: Error "No entry in config" no longer exits game.
This makes mission editing easier in case of misspelled class names.
*/

void OFPFrameFunctions::ErrorMessage(ErrorMessageLevel level, const char *format, va_list argptr)
{
  if (level<=EMError)
  {
    WarningMessageLevel(level,format,argptr);
  }
  else
  {
    ErrorMessage(format,argptr);
  }
}


#if _ENABLE_REPORT
  // always write to debug log
  
  #if _ENABLE_PERFLOG
    LogFile *GLogFile;
    
    static void ToDebugLog(const char *buf)
    {
      static bool open = false;
      static LogFile logFile;
      if (DisableLogs) return;
      if (!open || ResetLogFile)
      {
        char logFilename[MAX_PATH];
        if (GetLocalSettingsDir(logFilename))
        {
          CreateDirectory(logFilename, NULL);
          TerminateBy(logFilename, '\\');
        }
        else logFilename[0] = 0;
        strcat(logFilename, LogFilename);
        logFile.Open(logFilename);
        open = logFile.IsOpen();
        if (open) GLogFile = &logFile;
        ResetLogFile = false;
      }
      if (logFile.IsOpen()) 
        logFile.PrintF("%8.3f: %s",GlobalTickCount()*0.001,buf);
    }
  #endif


#if _ENABLE_REPORT
  // dirty hack: provide our own implementation of printf to allow using printf for debugging
  int __cdecl myprintf (const char *format, ...)
  {
    va_list argptr;      
    va_start( argptr, format );
    CurrentAppFrameFunctions->LogF(format,argptr);
    va_end( argptr );
    return 1; // nobody really cares about number of chars
  }
#endif
/*!
\patch 5088 Date 11/15/2006 by Jirka
- Fixed: Unicode used for debugger output (OutputDebugString)
*/

void OFPFrameFunctions::LogF( const char *format, va_list argptr)
{
  if (DisableLogs) return;
  BString<4096> buf;
  LString output = buf;
  #ifdef _WIN32
  if (GDebugger.IsDebugger())
  {
    // in debugger we want to see the timestamps

    int printed = sprintf(buf,"%s:%8.3f: ",cc_cast(InstanceName),GlobalTickCount()*0.001f);
    
    output = buf + printed;
  }
  #endif
  
  vsprintf(output,format,argptr);

  #if 0
    // always add a back slash
    strcat(buf,"\n");
  #else
    // make sure backslash is always present, but if it already ends with one, do not add another one
    const char *eol = strrchr(buf,'\n');
    // if not there or not at the end, append it
    if (!eol || eol[1]!=0)
    {
      strcat(buf,"\n");
    }
  #endif
  // assume someone will catch it even when there is no debugger
  #ifdef _WIN32
  
  // convert from UTF-8 to Unicode
  int len = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
  const int maxLen = 2048;
  wchar_t wBuffer[2048];
  if (len>maxLen-1) len = maxLen-1;
  MultiByteToWideChar(CP_UTF8, 0, buf, -1, wBuffer, len);
  wBuffer[len - 1] = 0; // make sure result is always null terminated
  OutputDebugStringW(wBuffer);
  #endif

  #if _ENABLE_PERFLOG
    ToDebugLog(buf);
  #endif
}

#endif  // of ENABLE_REPORT

/// see also InitFadeCount
//const static LONG ReinitFadeCount[CountFadeConditions]= {7};
const static LONG ReinitFadeCount[CountFadeConditions]= 
{
  (InitFadeCount[0]*15+9)/10,
  (InitFadeCount[1]*15+9)/10,
  (InitFadeCount[2]*15+9)/10,
  (InitFadeCount[3]*15+9)/10,
  (InitFadeCount[4]*15+9)/10,
};

void OFPFrameFunctions::ErrF( const char *format, va_list argptr)
{
  if (DisableLogs) return;
  // writing to report is very slow - if it happens, we want to see it in stats
  PROFILE_SCOPE_EX(rpt,*)
  
  BString<512> buf;
  vsprintf(buf,format,argptr);

#if _ENABLE_REPORT
  if (GDebugger.IsDebugger())
  {
    // avoid using OFPFrameFunctions::LogF to prevent time-stamping asserts (we want IDE to catch them)
    strcat(buf,"\n");
#ifdef _WIN32
    OutputDebugString(buf);
#else
    fputs(buf,stderr);
#endif
    #if _ENABLE_PERFLOG
      ToDebugLog(buf);
    #endif
#if defined(_WIN32) && _RELEASE
    __debugbreak();
#endif
  }
  else
#endif
  {
#if _RELEASE
#ifdef _WIN32
    ::RptF("%s",(const char *)buf);
    //DebugExceptionTrap::LogFFF(buf);
#else
    fputs(buf,stderr);
    fputc('\n',stderr);
#endif
#endif
	}
}

/*!
\patch 5262 Date 8/7/2008 by Bebul
- New: Optional server.cfg entry timeStampFormat to specify time stamp for each line in *.rpt file.
  Possible values: "none", "short", "full".
*/

TimeStampFormat GTimeStampFormat = TSFNone;

void InitLstFBuf(/*OUT*/BString<1024> &buf, const char *format, va_list argptr )
{
#if _ENABLE_DEDICATED_SERVER
  switch (GTimeStampFormat)
  {
  case TSFFull:
    {
#ifdef _WIN32
      SYSTEMTIME syst;
      GetLocalTime(&syst);
      sprintf(buf, "%4d/%02d/%02d, %2d:%02d:%02d ",
        syst.wYear,syst.wMonth,syst.wDay,
        syst.wHour,syst.wMinute,syst.wSecond
        );
#else
      time_t now;
      time(&now);
      tm *t = localtime(&now);
      sprintf(buf, "%4d/%02d/%02d, %2d:%02d:%02d ",
         t->tm_year+1900, t->tm_mon+1, t->tm_mday,
         t->tm_hour,t->tm_min,t->tm_sec);
#endif
      BString<1024> buf2;
      vsprintf(buf2,format,argptr);
      buf += buf2;
    }
    break;
  case TSFShort:
    {
#ifdef _WIN32
      SYSTEMTIME syst;
      GetLocalTime(&syst);
      sprintf(buf, "%2d:%02d:%02d ", syst.wHour, syst.wMinute, syst.wSecond);
#else
      time_t now;
      time(&now);
      tm *t = localtime(&now);
      sprintf(buf, "%2d:%02d:%02d ", t->tm_hour,t->tm_min,t->tm_sec);
#endif
      BString<1024> buf2;
      vsprintf(buf2,format,argptr);
      buf += buf2;
    }
    break;
  case TSFNone:
    vsprintf(buf,format,argptr);
    break;
  }
#else
  vsprintf(buf,format,argptr);
#endif
}

void FinishLstFBuf(BString<1024> &buf)
{
  if (DisableLogs) return;
#if _ENABLE_REPORT
  if (GDebugger.IsDebugger())
  {
    // avoid using OFPFrameFunctions::LogF to prevent time-stamping asserts (we want IDE to catch them)
    strcat(buf,"\n");
    OutputDebugString(buf);
    #if _ENABLE_PERFLOG
    ToDebugLog(buf);
    #endif
  }
  else
#endif
  {
#if _RELEASE
# ifdef _WIN32
#   if _ENABLE_REPORT
    ::LogF("%s",(const char *)buf);
#   endif
# else
    fputs(buf,stderr);
    fputc('\n',stderr);
# endif
#endif
  }
}

void OFPFrameFunctions::LstF( const char *format, va_list argptr)
{
  if (DisableLogs) return;
  // writing to report is very slow - if it happens, we want to see it in stats
  PROFILE_SCOPE_EX(rpt,*)

  BString<1024> buf;
  InitLstFBuf(buf, format, argptr);

#if _RELEASE
  #ifdef _WIN32
  //OutputDebugString(buf);OutputDebugString("\r\n");
  DebugExceptionTrap::LogFFF(buf,false);
  #endif
#endif

  FinishLstFBuf(buf);
} 

void OFPFrameFunctions::LstFDebugOnly( const char *format, va_list argptr)
{
  if (DisableLogs) return;
  // writing to report is very slow - if it happens, we want to see it in stats
  PROFILE_SCOPE_EX(rpt,*)

    BString<1024> buf;
  InitLstFBuf(buf, format, argptr);
  FinishLstFBuf(buf);
} 

#ifdef _WIN32
VOID ServiceStart(DWORD dwArgc, LPTSTR *lpszArgv) {}
VOID ServiceStop() {}
#endif

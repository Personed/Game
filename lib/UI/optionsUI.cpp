// Implementation of display's instances

#include "../wpch.hpp"
#include "uiMap.hpp"

#include "../landscape.hpp"
#include <El/FileServer/fileServer.hpp>

#include "resincl.hpp"

#include "../keyInput.hpp"

#include "../camera.hpp"
#include "../progress.hpp"

#include <El/ParamArchive/paramArchiveDb.hpp>
#include "../saveVersion.hpp"

#include <El/QStream/qbStream.hpp>

#include <El/Common/randomGen.hpp>
#include <El/Evaluator/express.hpp>

#include "../scripts.hpp"
#include "../fileLocator.hpp"

#include "../dikCodes.h"
#include "../vkCodes.h"
#include <Es/Common/win.h>
#include <El/Common/perfProf.hpp>
#ifdef _WIN32
  #include <io.h>
  #include <direct.h>
#endif

#include "../Network/netTransport.hpp"
#include "../Network/network.hpp"
#include "chat.hpp"

#include "../dynSound.hpp"
#include "displayUI.hpp"
#include "dispMissionEditor.hpp"

#include "../Protect/selectProtection.h"
#include <El/Debugging/debugTrap.hpp>

#include <Es/Algorithms/qsort.hpp>

#include <El/PreprocC/preprocC.hpp>

#include "missionDirs.hpp"

#include "../stringtableExt.hpp"
#include "../mbcs.hpp"

#include "../gameStateExt.hpp"

#include <Es/Files/fileContainer.hpp>

#include "../drawText.hpp"

#include <El/ParamFile/classDbParamFile.hpp>

#include "../saveGame.hpp"
#include "../gameDirs.hpp"

#include "../timeManager.hpp"

#pragma warning( disable : 4530 )
#include "../oggtext.h"

#include "../AI/aiRadio.hpp"

#if (defined _XBOX && _XBOX_VER >= 200)
#include "../joystick.hpp"
#endif

#include "Es/Common/langExt.hpp"
#define ENABLED_NOOPT_Bebul 0
#if ENABLED_FEATURE(NOOPT)
#if _ENABLE_CHEATS
#pragma optimize ("", off)
#endif
#endif

bool CheckDiskSpace(ControlsContainer *disp, const char *drive, int wantedBlocks, bool canCancel = true);

RStringB GetExtensionSave();
RStringB GetExtensionProfile();
RStringB GetExtensionWizardMission();

RString GetVehicleIcon(RString name);

bool CanSaveContinue();

bool HasPrimaryGunnerTurret(ParamEntryPar cls);
bool HasPrimaryObserverTurret(ParamEntryPar cls);

/*!
\file
Interface and implementation file for particular displays.
*/

inline PackedColor ModAlpha( PackedColor color, float alpha )
{
  int a=toInt(alpha*color.A8());
  saturate(a,0,255);
  return PackedColorRGB(color,a);
}

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
extern RString AutoTest;
#endif

const int MaxConstructionPoints = 7500;
#define WAYPOINT_CONSTRUCTION_POINTS 1

const float MaxCustomViewDistance = 1800;
const float MaxNormalViewDistance = 900;

/// with max. visibility you can place two planes (each 200 pts)
const float ConstructionPointsForMaxViewDistance = 500;

const float MaxAreaOver = (
  MaxCustomViewDistance*MaxCustomViewDistance-
  MaxNormalViewDistance*MaxNormalViewDistance
);

// view area considered should be something between 0 and MaxAreaOver
const float ViewareaConstructionPoints=(
  (MaxConstructionPoints-ConstructionPointsForMaxViewDistance)/MaxAreaOver
);


bool TypeIsGroup(RString name)
{
  const char *ptr = name;
  const char *ext = strchr(ptr, '.');
  if (!ext) return false;
  RString prefix(ptr, ext - ptr);
  return stricmp(prefix, "Group") == 0;
}

static RString TypeGetClass(RString name)
{
  const char *ptr = name;
  const char *ext = strchr(ptr, '.');
  DoAssert(ext);
  RString prefix(ptr, ext - ptr);
  DoAssert(stricmp(prefix, "Group") == 0);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString side(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString faction(ptr, ext - ptr);
  ptr = ext + 1;


  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString cls(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(!ext);

  return RString("Group.") + side + RString(".") + faction + RString(".") + cls;
}

ParamEntryVal TypeFindGroupEntry(RString name)
{
  const char *ptr = name;
  const char *ext = strchr(ptr, '.');
  DoAssert(ext);
  RString prefix(ptr, ext - ptr);
  DoAssert(stricmp(prefix, "Group") == 0);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString side(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString faction(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString cls(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(!ext);
  RString type(ptr);

  return Pars >> "CfgGroups" >> side >> faction >> cls >> type;
}

static ParamEntryVal ClassFindGroupEntry(RString name)
{
  const char *ptr = name;
  const char *ext = strchr(ptr, '.');
  DoAssert(ext);
  RString prefix(ptr, ext - ptr);
  DoAssert(stricmp(prefix, "Group") == 0);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString side(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString faction(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(!ext);
  RString cls(ptr);

  return Pars >> "CfgGroups" >> side >> faction >> cls;
}

static ParamEntryVal ClassFindGroupFactionEntry(RString name)
{
  const char *ptr = name;
  const char *ext = strchr(ptr, '.');
  DoAssert(ext);
  RString prefix(ptr, ext - ptr);
  DoAssert(stricmp(prefix, "Group") == 0);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString side(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(ext);
  RString faction(ptr, ext - ptr);
  ptr = ext + 1;

  ext = strchr(ptr, '.');
  DoAssert(!ext);
  RString cls(ptr);

  return Pars >> "CfgGroups" >> side >> faction;
}

static int TypeCalculateGroupCost(RString name)
{
  int value = 0;
  ParamEntryVal clsType = TypeFindGroupEntry(name);
  for (int i=0; i<clsType.GetEntryCount(); i++)
  {
    ParamEntryVal clsUnit = clsType.GetEntry(i);
    if (!clsUnit.IsClass()) continue;
    RStringB type = clsUnit >> "vehicle";
    RStringB simulation = Pars >> "CfgVehicles" >> type >> "simulation";
    ConstParamEntryPtr entry = (Pars >> "CfgSimulationCosts").FindEntry(simulation);
    if (entry) value += (int)(*entry);
  }
  return value;
}

//! creates formatted string for given amount of currency
/*!
  \param buffer output buffer
  \param res amount of currency
*/
void FormatCurrency(char *buffer, float res)
{
  if (res <= 0)
    strcpy(buffer, LocalizeString(IDS_CURRENCY_NONE));
  else
  {
    if (res >= 9.9e9)
      strcpy(buffer, LocalizeString(IDS_CURRENCY_LOT));
    else
    {
      char temp[128];
      sprintf(buffer, "%.0f", res);
      int digits = strlen(buffer);
      int i = 0;
      int j = 0;
      temp[i++] = buffer[j++];
      digits--;
      while (digits > 0)
      {
        if ((digits % 3) == 0)
          temp[i++] = LocalizeString(IDS_CURRENCY_BLANK)[0];
        temp[i++] = buffer[j++];
        digits--;
      }
      temp[i++] = buffer[j++];
      digits--;
      temp[i++] = 0;
      sprintf(buffer, (const char *)LocalizeString(IDS_CURRENCY_FORMAT), temp);
    }
  }
}

static RString UserParamsCached;
extern bool LandEditor;

#ifdef _XBOX
static void DetectCheatProfile(RString player)
{
  #if !_ENABLE_CHEATS
    if (!strcmp(player,"Dentist Guba"))
    {
      saturateMax(GInput.cheatXAllowLevel,0);
    }
    if (!strcmp(player,"QA Tester"))
    {
      saturateMax(GInput.cheatXAllowLevel,1);
    }
    if (!strcmp(player,"XaX 1985"))
    {
      saturateMax(GInput.cheatXAllowLevel,2);
    }
  #endif
}
#endif

#if defined _XBOX && _XBOX_VER >= 200

/// parse settings from user profile
bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals)
{
  QIStrStream in;
  if (!GSaveSystem.ReadSettingsFromProfile(in))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  if (!cfg.ParseBin(in, NULL, NULL, globals))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  return true;
}

/// parse settings from user save
bool ParseUserParamsFromSave(ParamFile &cfg, GameDataNamespace *globals)
{
  QIStrStream in;
  if (!GSaveSystem.ReadSettingsFromSave(in))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  if (!cfg.ParseBin(in, NULL, NULL, globals))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  return true;
}

/// save settings to user profile
void SaveUserParams(ParamFile &cfg)
{
  QOStrStream out;
  if (!cfg.SaveBin(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
  if (!GSaveSystem.WriteSettingsToProfile(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
}

/// save settings to user save
void SaveUserParamsToSave(ParamFile &cfg)
{
  QOStrStream out;
  if (!cfg.SaveBin(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
  if (!GSaveSystem.WriteSettingsToSave(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
}

/// parse settings from user profile
bool ParseUserParamsArchive(ParamArchiveLoad &ar, GameDataNamespace *globals)
{
  // read the config file
  QIStrStream in;
  if (!GSaveSystem.ReadSettingsFromProfile(in))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  // create the archive
  if (!ar.LoadBin(in, NULL, globals))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  return true;
}

/// parse settings from user save
bool ParseUserParamsArchiveFromSave(ParamArchiveLoad &ar, GameDataNamespace *globals)
{
  // read the config file
  QIStrStream in;
  if (!GSaveSystem.ReadSettingsFromSave(in))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  // create the archive
  if (!ar.LoadBin(in, NULL, globals))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  return true;
}

/// parse settings from user profile
bool ParseUserParamsArchive(ParamArchiveSave &ar, GameDataNamespace *globals)
{
  // read the config file
  QIStrStream in;
  if (!GSaveSystem.ReadSettingsFromProfile(in))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  // create the archive
  if (!ar.ParseBin(in, NULL, globals))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  return true;
}

/// parse settings from user save
bool ParseUserParamsArchiveFromSave(ParamArchiveSave &ar, GameDataNamespace *globals)
{
  // read the config file
  QIStrStream in;
  if (!GSaveSystem.ReadSettingsFromSave(in))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  // create the archive
  if (!ar.ParseBin(in, NULL, globals))
  {
    // Settings stored in the profile, process error silently
    return false;
  }
  return true;
}

/// save settings to user profile
void SaveUserParamsArchive(ParamArchiveSave &ar)
{
  QOStrStream out;
  if (!ar.SaveBin(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
  if (!GSaveSystem.WriteSettingsToProfile(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
}

/// save settings to user save
void SaveUserParamsArchiveToSave(ParamArchiveSave &ar)
{
  QOStrStream out;
  if (!ar.SaveBin(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
  if (!GSaveSystem.WriteSettingsToSave(out))
  {
    // Settings stored in the profile, process error silently
    return;
  }
}

#else

#if _PROFILE_IN_STEAM_CLOUD
/// The following SteamCloud implementation is taken / modified from ArmA3 (SteamUtils.hpp.cpp)
/* Returns true if cloud is enabled for user and for application
 */
bool IsSteamCloudEnabled();

/* Function for uploading files to the Steam cloud
 * Files are instantly saved to local Steam cloud, files are synchronized by Steam client after application close and before application starts.
 * Synchronous write, use only for small files.
 */
enum EUploadResult
{
  UROk,
  URNotEnoughSpace,
  URSourceFileDesntExists,
  URInvalidFile,
  URSteamError
};

EUploadResult UploadFileToSteamCloud(RString sourcePath, RString steamFileName)
{
  if (!SteamRemoteStorage())
  {
    LogF("SteamRemoteStorage structure is NULL");
    return URSteamError;
  }

  // check file existence
  if (!QIFileFunctions::FileExists(sourcePath))
  {
    LogF("Upload to steam Cloud: Invalid input file!" );
    return URInvalidFile;
  }

  // get available space
  int32 total, availableSpace;
  if (!SteamRemoteStorage()->GetQuota(&total, &availableSpace))
    return URSteamError;

  // check if file already exists
  if (SteamRemoteStorage()->FileExists(steamFileName))
  {
    // file will be overwritten, add it's size to available space
    availableSpace += SteamRemoteStorage()->GetFileSize(steamFileName);
  }

  // get file size
  const size_t fileSize = QIFileFunctions::GetFileSize(sourcePath);
  if (fileSize > availableSpace)
    return URNotEnoughSpace;
   
  // allocate read/write buffer (1MB)
  const int bufferSize = 1024*1024;
  Buffer<char> buff(bufferSize);

  int writenByes = 0;

  // open stream to Steam cloud
  UGCFileWriteStreamHandle_t fileHandle = SteamRemoteStorage()->FileWriteStreamOpen(steamFileName);
  if (fileHandle == k_UGCFileStreamHandleInvalid)
  {
    LogF("Can't open stream to cloud file '%s'.", cc_cast(steamFileName));
    return URSteamError;
  }

  QIFStream fileStream;
  fileStream.open(sourcePath);
  while (!fileStream.fail() && writenByes < fileSize)
  {
    // read from the file to buffer
    size_t rd = fileStream.read(buff.Data(),bufferSize);
    // store content of the buffer to Steam stream
    if (!SteamRemoteStorage()->FileWriteStreamWriteChunk(fileHandle, buff.Data(), rd))
      break;
    writenByes += rd;
  } 

  // close Steam
  SteamRemoteStorage()->FileWriteStreamClose(fileHandle);

  // check if everything went ok
  if (writenByes != fileSize)
  {
    // when there was a problem with source file, delete file from the cloud, it would be corrupted anyway
    SteamRemoteStorage()->FileDelete(steamFileName);

    LogF("Saving '%s' file to Steam cloud failed", cc_cast(steamFileName));
    return URSteamError;
  }
  fileStream.close();

  return UROk;
}

bool LoadFileFromSteamCloud(RString steamFileName, RString targetPath)
{
  // no need for target path to exist. If so, it should be overwritten
  QOFStream out;
  out.open(targetPath);
  if ( out.fail() || !SteamRemoteStorage()->FileExists( steamFileName ) ) return false;
  int size = SteamRemoteStorage()->GetFileSize( steamFileName );
  Buffer<char> buf(size);
  if ( SteamRemoteStorage()->FileRead( steamFileName, buf, size ) != size )
  {
    out.close(); return false;
  }
  QIStrStream in(buf, size);
  in.copy(out);
  out.close();
  return true;
}

class SteamProfileLoader
{
public:
  SteamProfileLoader()
  {
#if _ENABLE_DEDICATED_SERVER
    bool IsDedicatedServer();
    if (IsDedicatedServer()) return;
#endif
    if (!SteamRemoteStorage()) 
    {
      bool InitSteam();
      if (!InitSteam() || !SteamRemoteStorage())
      {
        LogF("SteamProfileLoader has failed!");
        return;
      }
    }
    RString GetUserParams(bool update);
    RString filename = GetUserParams(false);
    RString cloudFileName = GetFilenameExt(filename);
    if (SteamRemoteStorage()->FileExists(cloudFileName))
    {
      if (QIFileFunctions::FileExists(filename))
      {
        int64 cloudTimeStamp = SteamRemoteStorage()->GetFileTimestamp(cloudFileName);
        int64 profileTimeStamp = QIFileFunctions::TimeStamp(filename);
        if (profileTimeStamp < cloudTimeStamp)
        { // profile inside cloud is more current, we should overwrite the profile
          LoadFileFromSteamCloud(cloudFileName, filename);
        }
        // else ... maybe manualy edited profile
      }
    }
  }
};
#endif

//! returns user profile (parameters) file for given user
RString GetUserParams(RString name, bool update)
{
  if (name.GetLength() == 0) return RString();
  // enable user profiles in buldozer
  // if (LandEditor) return RString();

#ifdef _XBOX
  // Check cache
  bool currentPlayer = name == Glob.header.GetPlayerName();
  if (currentPlayer && !Glob.header.playerNameChanged) return UserParamsCached;

  // Check if directory exist
  Ref<ProgressHandle> p;
  if (GEngine)
  {
    p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
    GDebugger.PauseCheckingAlive();
  }

  RString dir;
  if (!update)
  {
    SaveHeaderAttributes filter;
    filter.Add("type", FindEnumName(STSettings));
    
    AutoArray<SaveHeader> index;
    FindSaves(index, filter);

    #if !_ENABLE_CHEATS
      GInput.cheatXAllowLevel = -1;
    #endif
    for (int i=0; i<index.Size(); i++)
    {
      SaveHeaderAttributes &attributes = index[i].attributes;
      RString player;
      if (attributes.Find("player", player))
      {
        if (!strcmpi(player,name))
        {
          dir = index[i].dir;
        }

        DetectCheatProfile(player);
        
        
      }
    }
  }
  if (dir.GetLength() == 0)
  {
    SaveHeaderAttributes filter;
    filter.Add("type", FindEnumName(STSettings));
    filter.Add("player", name);
    
    dir = CreateSave(filter); // Create save directory
  }
  if (GEngine)
  {
    GDebugger.ResumeCheckingAlive();
    ProgressFinish(p);
  }

  RString filename;
  if (dir.GetLength() == 0)
  {
    // TODOX360: X360 save game management
    // once done, change to Fail + RString() again
    #if 1
      filename = RString("cache:\\") + EncodeFileName(name) + RString(".") + GetExtensionProfile();
      RptF("Cannot find nor create user settings file, using scratch %s",cc_cast(filename));
    #else
      Fail("Cannot find nor create user settings file");
      return RString();
    #endif
  }
  else
  {
    filename = dir + EncodeFileName(name) + RString(".") + GetExtensionProfile();
  }
  if (currentPlayer)
  {
    UserParamsCached = filename;
    Glob.header.playerNameChanged = false;
  }
  return filename;
#else
  // name is ignored, LoginName for filename is used
  return GetUserDirectory(name) + EncodeFileName(GetLoginName()) + RString(".") + GetExtensionProfile();
#endif
}

//! returns user profile (parameters) file for current user
RString GetUserParams(bool update)
{
  return GetUserParams(Glob.header.GetPlayerName(), update);
}

bool ParseUserParams(ParamFile &cfg, RString name, GameDataNamespace *globals)
{
#if _PROFILE_IN_STEAM_CLOUD
  static SteamProfileLoader SPL; //singleton to get profile from cloud on init
#endif
  RString filename = GetUserParams(name, false);
  if (filename.GetLength() <= 0 || !QIFileFunctions::FileExists(filename))
  {
    // even when not parsed we would like to set the name, so that it can be saved
    cfg.SetName(filename);
    return false;
  }

#ifdef _XBOX
  if (!cfg.ParseSigned(filename, NULL, NULL, &globals))
  {
    const char *ptr = strrchr(filename, '\\');
    ptr++;
    RString dir = filename.Substring(0, ptr - (const char *)filename);

    ReportUserFileError(dir, ptr, RString());
    return false;
  }
  return true;
#else
  return cfg.Parse(filename, NULL, NULL, globals) == LSOK;
#endif
}

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals)
{
  return ParseUserParams(cfg, Glob.header.GetPlayerName(), globals);
}

bool ParseUserParamsFromSave(ParamFile &cfg, GameDataNamespace *globals)
{
  return ParseUserParams(cfg, Glob.header.GetPlayerName(), globals);
}

void SaveUserParams(ParamFile &cfg)
{
#ifdef _XBOX
  cfg.SaveSigned();
#else
  cfg.Save();
#endif

#if _PROFILE_IN_STEAM_CLOUD
#if _ENABLE_DEDICATED_SERVER
  bool IsDedicatedServer();
  if (!IsDedicatedServer())
#endif
  {
    RString filename = GetUserParams(false);
    RString cloudFileName = GetFilenameExt(filename);
    /*EUploadResult result = */UploadFileToSteamCloud(filename, cloudFileName);
    //LogF("UploadFileToSteamCloud: %s result=%d", cc_cast(cloudFileName), result);
  }
#endif
}

void SaveUserParamsToSave(ParamFile &cfg)
{
  SaveUserParams(cfg);
}

bool ParseUserParamsArchive(ParamArchiveLoad &ar, GameDataNamespace *globals)
{
  RString filename = GetUserParams(false);
  if (filename.GetLength() <= 0 || !QIFileFunctions::FileExists(filename)) return false;

#ifdef _XBOX
  if (!ar.LoadSigned(filename, NULL, globals))
  {
    const char *ptr = strrchr(filename, '\\');
    ptr++;
    RString dir = filename.Substring(0, ptr - (const char *)filename);

    ReportUserFileError(dir, ptr, RString());
    return false;
  }
  return true;
#else
  return ar.Load(filename, NULL, globals);
#endif
}

bool ParseUserParamsArchiveFromSave(ParamArchiveLoad &ar, GameDataNamespace *globals)
{
  return ParseUserParamsArchive(ar, globals);
}

bool ParseUserParamsArchive(ParamArchiveSave &ar, GameDataNamespace *globals)
{
  RString filename = GetUserParams(false);
  if (filename.GetLength() <= 0 || !QIFileFunctions::FileExists(filename)) return false;

#ifdef _XBOX
  if (!ar.ParseSigned(filename, NULL, NULL, globals))
  {
    const char *ptr = strrchr(filename, '\\');
    ptr++;
    RString dir = filename.Substring(0, ptr - (const char *)filename);

    ReportUserFileError(dir, ptr, RString());
    return false;
  }
  return true;
#else
  return ar.Parse(filename, NULL, globals);
#endif
}

bool ParseUserParamsArchiveFromSave(ParamArchiveSave &ar, GameDataNamespace *globals)
{
  return ParseUserParamsArchive(ar, globals);
}

void SaveUserParamsArchive(ParamArchiveSave &ar)
{
  RString filename = GetUserParams(true);
  if (filename.GetLength() <= 0) return;

#ifdef _XBOX
  ar.SaveSigned(filename);
#else
  ar.Save(filename);
#endif

#if _PROFILE_IN_STEAM_CLOUD
#endif
}

void SaveUserParamsArchiveToSave(ParamArchiveSave &ar)
{
  SaveUserParamsArchive(ar);
}


#endif // defined _XBOX && _XBOX_VER >= 200

void ParseFlashpointCfg(ParamFile &file);
void SaveFlashpointCfg(ParamFile &file);
RString GetConfigSection();

//! reads user profile when current player changed
void UpdateUserProfile()
{
  LogF("Updating user profile '%s'", (const char *)Glob.header.GetPlayerName());

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace

  ParamFile cfg;
  ParseUserParams(cfg, &globals);

  int GetLangID();
  bool NoBlood();
  if (NoBlood())
  {
    Glob.config.bloodLevel = 0;
  }
  else
  {
    Glob.config.bloodLevel = GetLangID() != Korean ? 1 : 0;

    ConstParamEntryPtr entry = cfg.FindEntry("blood");
    if (entry) Glob.config.bloodLevel = *entry;
  }

  ConstParamEntryPtr headBobEntry = cfg.FindEntry("headBob");
  Glob.config.headBob = (headBobEntry) ? *headBobEntry : 1.0;

#if _HELISIM_IMPERIAL_SYSTEM
  ConstParamEntryPtr useImperialSystemeEntry = cfg.FindEntry("useImperialSystem");
  Glob.config.useImperialSystem = (useImperialSystemeEntry) ? *useImperialSystemeEntry : 0;
#endif

  GInput.LoadKeys(cfg);
  GInput.RemoveInaccessableKeys();

  Glob.config.difficulty = Glob.config.diffDefault;
  ConstParamEntryPtr entry = cfg.FindEntry("difficulty");
  if (entry)
  {
    RString name = *entry;
    int difficulty = Glob.config.diffNames.GetValue(name);
    if (difficulty >= 0) Glob.config.difficulty = difficulty;
  }
  Glob.config.LoadDifficulties(cfg);

  ParamFile fcfg;
  ParseFlashpointCfg(fcfg);
  #ifndef _XBOX
    // TODOX360: check which parts of the scene / engine config need to be loaded
    GEngine->LoadConfig(cfg,fcfg,GetConfigSection());
    GScene->LoadConfig(cfg,fcfg);
  #endif
  if ( GSoundsys ) GSoundsys->LoadConfig(cfg);

  if ((Pars >> "CfgVoiceMask").GetEntryCount() > 0)
    Glob.header.voiceMask = (Pars >> "CfgVoiceMask").GetEntry(0).GetName();
  ConstParamEntryPtr identity = cfg.FindEntry("Identity");
  if (identity)
  {
    ConstParamEntryPtr entry = identity->FindEntry("mask");
    if (entry) Glob.header.voiceMask = *entry;
  }
  if (Glob.header.voiceMask.GetLength() > 0)
    GetNetworkManager().ChangeVoiceMask(Glob.header.voiceMask);

  GWorld->LoadProfileNamespace();

#if _VBS2 // VBS2 nickname  
  if (Glob.vbsAdminMode)
    Glob.header.playerHandle = Glob.header.GetPlayerName(); 
  else
  {
    ConstParamEntryPtr playerHandle = cfg.FindEntry("playerHandle");
    if (playerHandle)
      Glob.header.playerHandle = *playerHandle; 
  }
#endif

#if _VBS2 // player color
    ConstParamEntryPtr playerColor = cfg.FindEntry("playerColor");
    if (playerColor)
      Glob.playerColor = *playerColor; 
#endif
}

#if defined _XBOX && _XBOX_VER < 200
RString CheckUserParams(RString name)
{
  // Check if directory exist
  SaveHeaderAttributes filter;
  filter.Add("type", FindEnumName(STSettings));
  filter.Add("player", name);
  RString dir = GetSaveDir(filter);
  if (dir.GetLength() > 0) return dir + EncodeFileName(name) + RString(".") + GetExtensionProfile();
  return RString();
}

bool DeleteUserProfile(RString name)
{
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_NETWORK_DELETING));
  GDebugger.PauseCheckingAlive();

  SaveHeaderAttributes filter;
  AutoArray<SaveHeader> list;
  filter.Add("player", name);
  FindSaves(list, filter);

  bool ok = true;
  for (int i=0; i<list.Size(); i++)
  {
    SaveHeaderAttributes &attributes = list[i].attributes;
    if (!DeleteSave(attributes, true)) ok = false;
  }

  if (stricmp(name, Glob.header.GetPlayerName()) == 0)
  {
    // actual profile deleted
    Glob.header.SetPlayerName(RString());

    // initialization
    Glob.header.playerFace = "Default";
    Glob.header.playerGlasses = "None";
    Glob.header.playerSpeaker = (Pars >> "CfgVoice" >> "default");
    Glob.header.playerPitch = PITCH_DEFAULT;
    Glob.header.voiceMask = (Pars >> "CfgVoiceMask").GetEntry(0).GetName();

    UpdateUserProfile();
  }

  GDebugger.ResumeCheckingAlive();
  ProgressFinish(p);

  return ok;
}

void RenameUserProfile(RString oldName, RString newName)
{
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_NETWORK_UPDATING));
  GDebugger.PauseCheckingAlive();

  SaveHeaderAttributes filter;
  AutoArray<SaveHeader> list;
  filter.Add("player", oldName);
  FindSaves(list, filter);

  for (int i=0; i<list.Size(); i++)
  {
    SaveHeaderAttributes &srcAttributes = list[i].attributes;
    RString srcDir = list[i].dir;
    SaveHeaderAttributes dstAttributes;
    for (int j=0; j<srcAttributes.Size(); j++)
    {
      if (stricmp(srcAttributes[j].name, "player") == 0)
        dstAttributes.Add("player", newName);
      else
        dstAttributes.Add(srcAttributes[j].name, srcAttributes[j].value);
    }
    RString dstDir = CreateSave(dstAttributes);
    if (dstDir.GetLength() == 0)
    {
      Fail("Cannot rename save");
      continue;
    }

    // copy all files to new save
    _finddata_t info;
    long h = _findfirst(srcDir + RString("*.*"), &info); // u: drive
    if (h != -1)
    {
      do
      {
        if ((info.attrib & _A_SUBDIR) != 0)
        {
          Fail("Subdirectory not expected");
        }
        else if (stricmp(info.name, "SaveMeta.xbx") != 0 && stricmp(info.name, "header.bin") != 0)
        {
          QIFStream in;
          in.open(srcDir + RString(info.name));
          QOFStream out;
          out.open(dstDir + RString(info.name));
          in.copy(out);
          out.close();
          in.close();
        }
      }
      while (_findnext(h, &info)==0);
      _findclose(h);
    }

    // remove old save
    DeleteSave(srcAttributes, true);
  }

  GDebugger.ResumeCheckingAlive();
  ProgressFinish(p);
}
#endif // defined _XBOX && _XBOX_VER < 200

//! returns path of current mission's briefing
RString GetBriefingFile(RString dir)
{
  RString prefix = dir + RString("briefing.");
  RString name = prefix + GLanguage + RString(".html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  name = prefix + RString("html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  return RString();
}

RString GetBriefingFile()
{
  if (GetMissionDirectory().GetLength() == 0) return RString();
  return GetBriefingFile(GetMissionDirectory());
}

#ifdef _XBOX
# if _XBOX_VER >= 200

/// Save display name of the currently played mission
RString GetSaveDisplayName()
{
  RString name = Localize(CurrentTemplate->intel.briefingName);
  if (name.GetLength() == 0) name = Glob.header.filenameReal + RString(".") + RString(Glob.header.worldname);
  return Format(LocalizeString(IDS_SAVE_MISSION_FORMAT), cc_cast(name));
}

/// Save file name of the currently played mission
RString GetSaveFileName()
{
  int campaignIndex = 0;

  int missionIndex = 0;
  {
    // get the mission index
    ConstParamEntryPtr entry = ExtParsMission.FindEntry("missionId");
    if (entry) missionIndex = *entry;
    else
    {
      RString mission = GetBaseDirectory() + GetBaseSubdirectory() + Glob.header.filenameReal + RString(".") + RString(Glob.header.worldname);
      RptF("Mission %s - no missionId set", cc_cast(mission));
      missionIndex = CalculateStringHashValueCI(mission.GetKey());
    }
    DoAssert(missionIndex != 0);
  }

  // file name
  bool multiplayer = GWorld->GetMode() == GModeNetware;
  __int64 xuid = 0;
  return Format("%cS%016I64X%08X%08X", multiplayer ? 'M' : 'S', xuid, campaignIndex, missionIndex);
}

/// Save type of the currently played mission
static SaveType GetSaveType()
{
  if (GWorld->GetMode() == GModeNetware) return STSaveMPMission;
  return STSaveSingleMission;
}

/// Find the Xbox 360 save container for the currently played mission
XSaveGame GetSaveGame(bool create, bool noErrorMsg)
{
  RString missionParameters = GetMissionParameters();
  if (missionParameters.GetLength() > 0)
  {
    // user mission save
    // saves disabled for user missions - see bool CanSaveGame()
    return NULL;
  }

  if (!GSaveSystem.IsStorageAvailable()) return NULL;

  RString fileName = GetSaveFileName();
  if (create)
  {
    // display name
    RString displayName = GetSaveDisplayName();
    return GSaveSystem.OpenOrCreateSave(fileName, displayName, GetSaveType(), NULL, noErrorMsg);
  }
  else
  {
    return GSaveSystem.OpenSave(fileName, NULL, noErrorMsg);
  }
};

# else

static void FillSaveGamesFilter(SaveHeaderAttributes &filter)
{
  bool userMission = GetMissionParameters().GetLength() > 0;

  filter.Add("type", FindEnumName(STGameSave));
  filter.Add("player", Glob.header.GetPlayerName());
  filter.Add("parameters", GetMissionParameters());
  RString mission = userMission ? RString() :
  Format("%s%s.%s", (const char *)GetBaseSubdirectory(), (const char *)Glob.header.filenameReal, (const char *)Glob.header.worldname);
  filter.Add("mission", mission);
}

void CreatePath(RString path);

static RString LastSaveDirectory;
static SaveHeaderAttributes LastSaveFilter;

RString CreateSaveGameDirectory()
{
  SaveHeaderAttributes filter;
  FillSaveGamesFilter(filter);
  if (GetMissionParameters().GetLength() == 0 && GetBaseDirectory().GetLength() == 0)
  {
    // Create save directory
    LastSaveDirectory = CreateSave(filter);
  }
  else
  {
    // Create user mission save directory
    LastSaveDirectory = "z:\\CurrentMission\\";
    CreatePath(LastSaveDirectory);
  }
  LastSaveFilter = filter;
  return LastSaveDirectory;
}

# endif
#endif

#if defined _XBOX && _XBOX_VER >= 200

#else

//! returns save directory for current user and current mission
RString GetSaveDirectory()
{
#ifdef _XBOX
  SaveHeaderAttributes filter;
  FillSaveGamesFilter(filter);
  if (filter != LastSaveFilter)
  {
    LastSaveFilter = filter;
    if (GetMissionParameters().GetLength() == 0 && GetBaseDirectory().GetLength() == 0)
    {
      LastSaveDirectory = GetSaveDir(filter);
    }
    else
    {
      LastSaveDirectory = "z:\\CurrentMission\\";
      CreatePath(LastSaveDirectory);
    }
  }
  return LastSaveDirectory;
#else
  RString dir;
  if (GetMissionParameters().GetLength() > 0)
  {
    RString params = GetMissionParameters();
    const char *name = strrchr(params, '\\');
    if (name) name++;
    else name = params;
    dir = GetUserDirectory() + RString("Saved\\") + RString(name) + RString("\\");
  }
  else if (IsUserMission())
  {
    if (Glob.header.filenameReal.GetLength() == 0) return RString();
    dir = GetUserDirectory() + RString("UserSaved\\") + GetBaseSubdirectory() +
      RString(Glob.header.filenameReal) + RString(".") + RString(Glob.header.worldname) + RString("\\");
  }
  else
  {
    dir = GetUserDirectory() + RString("Saved\\") + GetBaseDirectory() + GetBaseSubdirectory() +
      RString(Glob.header.filenameReal) + RString(".") + RString(Glob.header.worldname) + RString("\\");
  }
  CreatePath(dir);
  return dir;
#endif
}

//! returns save directory for current user and given single mission
RString GetMissionSaveDirectory(RString mission)
{
#ifdef _XBOX
  Fail("Not supported on Xbox");
  return RString();
#else
  RString dir = GetUserDirectory() + RString("Saved\\") + mission + RString("\\");
  return dir;
#endif
}

//! returns temporary save directory for current user
RString GetTmpSaveDirectory()
{
#ifdef _XBOX
  Fail("Not supported on Xbox");
  return RString();
#else
  RString dir = GetUserDirectory() + RString("Saved\\Tmp\\");
  CreatePath(dir);
  return dir;
#endif
}

#endif // defined _XBOX && _XBOX_VER >= 200

bool DeleteSaveGames()
{
#ifdef _XBOX
# if _XBOX_VER >= 200
  XSaveGame save = GetSaveGame(false, true);
  if (!save) return true; // Errors already handled
  // TODOXSAVES: save game image
  RString dir = SAVE_ROOT;
# else
  Ref<ProgressHandle> p = ProgressStart(LocalizeString(IDS_LOAD_WORLD));
  GDebugger.PauseCheckingAlive();

  SaveHeaderAttributes filter;
  FillSaveGamesFilter(filter);
  AutoArray<SaveHeader> index;
  FindSaves(index, filter);

  GDebugger.ResumeCheckingAlive();
  ProgressFinish(p);

  if (index.Size() == 0) return false;
  DoAssert(index.Size() == 1);
  RString dir = index[0].dir;
  CopyDefaultSaveGameImage(dir);
# endif
#else
  RString dir = GetSaveDirectory();
#endif

  // remove all files
  bool ok = true;
  for (int i=0; i<2; i++)
  {
    RString mpCampaignPrefix = i ? RString("mp") : RString("");
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("autosave.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("save.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("continue.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("save2.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("save3.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("save4.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("save5.") + GetExtensionSave())) ok = false;
    if (!QIFileFunctions::CleanUpFile(dir + mpCampaignPrefix + RString("save6.") + GetExtensionSave())) ok = false;
  }
  if (!QIFileFunctions::CleanUpFile(dir + RString("autosave.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("save.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("continue.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("save2.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("save3.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("save4.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("save5.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("save6.roles") + GetExtensionSave())) ok = false;
  if (!QIFileFunctions::CleanUpFile(dir + RString("weapons.cfg"))) ok = false;

  return ok;
}

bool DeleteWeaponsFile()
{
#if defined _XBOX && _XBOX_VER >= 200
  GSaveSystem.ClearWeaponConfig();
  return true;
#else
  RString dir = GetSaveDirectory();
  if (dir.GetLength() == 0) return true; // no save exist
  return QIFileFunctions::CleanUpFile(dir + RString("weapons.cfg"));
#endif
}

inline ConstParamEntryPtr FindClassEntry(ConstParamEntryPtr cls, const char *name)
{
  return cls ? ConstParamEntryPtr(cls->FindEntry(name)) : ConstParamEntryPtr();
}
//! find description of environmental sound in configuration files
/*!
  Description is subclass of class CfgEnvSounds.
  Order of searching:
  - description.ext of current mission
  - global config
  \param name name of subclass
  \param day output - sound parameters for environmental sound during day
*/
void FindEnvSound(RString name, SoundPars &day)
{
  // ignore @ - not used now
  if (name[0] == '@') name = (const char *)name + 1;

  // find in mission
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("CfgEnvSounds");
  ConstParamEntryPtr entry = FindClassEntry(cls,name);
  if (entry)
  {
    Assert(GetMissionDirectory().GetLength()>0);
    GetValue(day, (*entry) >> "sound");
    if (day.name.GetLength() > 0)
    {
      day.name = GetMissionDirectory() + day.name;
      day.name.Lower();
    }
    return;
  }

  // find in config
  cls = Pars.FindEntry("CfgEnvSounds");
  entry = FindClassEntry(cls,name);
  if (entry)
  {
    GetValue(day, (*entry) >> "sound");
    day.name.Lower();
    return;
  }

  WarningMessage("Environmental sound %s not found", (const char *)name);
}

//! find description of SFX sound scheme in configuration files
/*!
  Description is subclass of class CfgSFX.
  Order of searching:
  - description.ext of current mission
  - global config
  \param name name of subclass
  \param emptySound empty sound
  \param sounds sound scheme
*/
void FindSFX(RString name, SoundEntry &emptySound, AutoArray<SoundEntry> &sounds)
{
  // ignore @ - not used now
  if (name[0] == '@') name = (const char *)name + 1;

  // find in mission
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("CfgSFX");
  ConstParamEntryPtr entry = FindClassEntry(cls,name);
  if (entry)
  {
    // load empty sound
    Assert(GetMissionDirectory().GetLength()>0);
    emptySound = DynSound::LoadEntry((*entry) >> "empty", GetMissionDirectory());
    // load sound scheme
    ParamEntryVal list = (*entry) >> "sounds";
    int n = list.GetSize();
    sounds.Resize(n);
    for (int i=0; i<n; i++)
    {
      RString name = list[i];
      SoundEntry &sound = sounds[i];
      sound = DynSound::LoadEntry((*entry) >> name, GetMissionDirectory());
    }
    // return sound location
    return; // ExtParsMission.GetName();
  }

  // find in config
  cls = Pars.FindEntry("CfgSFX");
  entry = FindClassEntry(cls,name);
  if (entry)
  {
    // load empty sound
    emptySound = DynSound::LoadEntry((*entry) >> "empty","");
    // load sound scheme
    ParamEntryVal list = (*entry) >> "sounds";
    int n = list.GetSize();
    sounds.Resize(n);
    for (int i=0; i<n; i++)
    {
      RString name = list[i];
      SoundEntry &sound = sounds[i];
      sound = DynSound::LoadEntry((*entry) >> name ,"");
    }
    return; // Pars.GetName();
  }

  WarningMessage("SFX %s not found", (const char *)name);
  return; // "";
}

//! find description of sound in configuration files
/*!
  Description is subclass of class CfgSounds.
  Order of searching:
  - description.ext of current mission
  - global config
  \param name name of subclass
  \param pars sound parameters
*/
ConstParamEntryPtr FindSound(RString name, SoundPars &pars)
{
  // ignore @ - not used now
  if (name[0] == '@') name = (const char *)name + 1;

  // find in mission
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("CfgSounds");
  ConstParamEntryPtr entry = FindClassEntry(cls,name);
  if (entry)
  {
    Assert(GetMissionDirectory().GetLength()>0);
    GetValue(pars, (*entry) >> "sound");
    if (pars.name.GetLength() > 0)
      pars.name = GetMissionDirectory() + pars.name;
    return entry;
  }
  
  // find in config
  cls = Pars.FindEntry("CfgSounds");
  entry = FindClassEntry(cls,name);
  if (entry)
  {
    GetValue(pars, (*entry) >> "sound");
    return entry;
  }

  WarningMessage("Sound %s not found", (const char *)name);
  return ConstParamEntryPtr();
}

//! find description of music track in configuration files
/*!
  Description is subclass of class CfgMusic.
  Order of searching:
  - description.ext of current mission
  - global config
  \param name name of subclass
  \param pars sound parameters
*/
ConstParamEntryPtr FindMusic(RString name, SoundPars &pars)
{
  // find in mission
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("CfgMusic");
  ConstParamEntryPtr entry = FindClassEntry(cls,name);
  if (entry)
  {
    Assert(GetMissionDirectory().GetLength()>0);
    GetValue(pars, (*entry) >> "sound");
    if (pars.name.GetLength() > 0)
      pars.name = GetMissionDirectory() + pars.name;
    return entry;
  }
  
  // find in config
  cls = Pars.FindEntry("CfgMusic");
  entry = FindClassEntry(cls,name);
  if (entry)
  {
    GetValue(pars, (*entry) >> "sound");
    return entry;
  }

  RptF("Music %s not found", (const char *)name);
  return ConstParamEntryPtr();
}

//! find description of radio message in configuration files
/*!
  Description is subclass of class CfgRadio.
  Order of searching:
  - description.ext of current mission
  - global config
  \param name name of subclass
  \param pars sound parameters
  \return description subclass
*/
ConstParamEntryPtr FindRadio(RString name, SoundPars &pars, bool silent = false)
{
  // find in mission
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("CfgRadio");
  ConstParamEntryPtr entry = FindClassEntry(cls,name);
  if (entry)
  {
    Assert(GetMissionDirectory().GetLength()>0);
    GetValue(pars, (*entry) >> "sound");
    if (pars.name.GetLength() > 0)
      pars.name = GetMissionDirectory() + pars.name;
    return entry;
  }
  
  // find in config
  cls = Pars.FindEntry("CfgRadio");
  entry = FindClassEntry(cls,name);
  if (entry)
  {
    GetValue(pars, (*entry) >> "sound");
    return entry;
  }

  if (!silent)
  {
    WarningMessage("Radio message %s not found", (const char *)name);
  }

  return ConstParamEntryPtr();
}

static bool IsRelativeScriptPath(const char *path)
{
  if (IsRelativePath(path)) return true;
  #if _ENABLE_CHEATS
  // suspected not relative path
  // we want to enable the paths used in ArmAExt, like in loadfile "01 :dll"
  // For more ArmA Extended Dll see http://voyager.alfamoon.com/index.php?go=News&in=view&id=16
  const char *dcolon = strchr(path,':');
  if (dcolon) // if double colon is found, chance is it is ArmA ext path
  {
    // allowed syntax is: digits and white-spaces followed by double colon
    // check if any character before double colon is something else than a digit or a space
    for (const char *c=path; c<dcolon; c++)
    {
      if (!isdigit(*c) && !isspace(*c)) return false;
    }
    // no offense found, ArmA ext path confirmed
    return true;
  }
  #endif
  return false;
}

#include "../encodeString.h"

static RString VerifyInBank(RString name)
{
  if (name.IsEmpty()) return name;
  // verify the script comes from pbo - when not, reject it
  if( GUseFileBanks )
  {
    QFBank *bank=QFBankQueryFunctions::AutoBank(name);
    if (!bank)
    {
      auto encoded = EncodedString<EncoderXOR<0x55>>("Free scripts no longer supported, \'%s\' ignored");
      RptF(DecodeString(encoded), cc_cast(name));
      return RString();
    }
  }
  return name;
}

//! find script file
/*!
  Order of searching:
  - mission directory of current mission
  - "scripts" subdirectory in base application directory
  \param name simple script name
  \return full script name (path)

*/
RString FindScript(RString name)
{
  // find in mission
  if (name[0]!='\\')
  {
    if (!IsRelativeScriptPath(name))
    {
      RptF("Invalid path (only relative paths supported): '%s'", cc_cast(name));
      return RString();
    }

    RString missionDir = GetMissionDirectory();
    if (missionDir.GetLength()>0)
    {
      RString fullname = missionDir + name;
      if (QFBankQueryFunctions::FileExists(fullname)) return fullname;
    }

    // find in root
    RStringB GetScriptsPath();
    RString fullname = GetScriptsPath() + name;
    if (QFBankQueryFunctions::FileExists(fullname)) return VerifyInBank(fullname);
  }
  else
  {
    // remove leading '\'
    name = name.Substring(1, INT_MAX);

    if (!IsRelativeScriptPath(name))
    {
      RptF("Invalid path (only relative paths supported): '\\%s'", cc_cast(name));
      return RString();
    }
  }

  if (QFBankQueryFunctions::FileExists(name)) return VerifyInBank(name);
#if !_VBS2_LITE
  WarningMessage("Script %s not found", (const char *)name);
#endif
  return RString();
}

//! find model (d3d) file
/*!
  Order of searching:
  - mission directory of current mission
  - "dtaExt" subdirectory in base aplication directory
  \param name simple model file name
  \return full model file name (path)
*/
RString FindShape(RString name)
{
  // cannot ignore, used commonly in mod paths // ignore @ - not used now
  //if (name[0] == '@') name = (const char *)name + 1;

  if (name[0]!='\\')
  {
    // find in mission
    RString missionDir = GetMissionDirectory();
    if (missionDir.GetLength()>0)
    {
      RString fullname = missionDir + name;
      if (QFBankQueryFunctions::FileExists(fullname)) return fullname;
    }
        
    // find in root
    RString fullname = RString("dtaExt\\") + name;
    if (QFBankQueryFunctions::FileExists(fullname)) return fullname;
  }
  
  // find in bank
  RString fullname = GetShapeName(name);
  if (QFBankQueryFunctions::FileExists(fullname)) return fullname;

  WarningMessage("Shape %s not found", (const char *)name);
  // even if the name is invalid, return it
  // avoid using empty name, as it is handled badly by Shapes
  return fullname;
}

//! find image (texture) file
/*!
  Order of searching:
  - mission directory of current mission
  - "dtaExt" subdirectory in base aplication directory
  \param name simple image file name
  \return full image file name (path)
*/
RString FindPicture(RString name)
{
  // name "" used for NULL texture
  if (name.GetLength() == 0) return name;

  // cannot ignore, used commonly in mod paths // ignore @ - not used now
  //if (name[0] == '@') name = (const char *)name + 1;

  name.Lower();

  if (name[0] == '#') return name; // procedural texture

  if (name[0] != '\\')
  {
    // find in mission
    RString missionDir = GetMissionDirectory();
    if (missionDir.GetLength()>0)
    {
      RString fullname = missionDir + name;
      if (QFBankQueryFunctions::FileExists(fullname))
      {
        fullname.Lower();
        return fullname;
      }
    }
    
    // find in root
    RString fullname = RString("dtaExt\\") + name;
    if (QFBankQueryFunctions::FileExists(fullname))
    {
      fullname.Lower();
      return fullname;
    }
  }
  
  // find in bank
  RString fullname = GetPictureName(name);
  if (QFBankQueryFunctions::FileExists(fullname)) return fullname;

  ErrorMessage(EMMissingData, "Picture %s not found", (const char *)name);
  return "";
}

//! find description of resource based title effect in configuration files
/*!
  Description is subclass of class RscTitles.
  Order of searching:
  - description.ext of current mission
  - global resource
  \param name name of subclass
  \return description subclass
  \patch 1.21 Date 08/22/2001 by Jirka
  - Added: "RscTitles" section in description.ext
*/
ConstParamEntryPtr FindRscTitle(RString name)
{
  // find in mission
  ConstParamEntryPtr cls = ExtParsMission.FindEntry("RscTitles");
  ConstParamEntryPtr entry = FindClassEntry(cls,name);
  if (entry) return entry;
  
  // find in resource
  cls = Pars.FindEntry("RscTitles");
  entry = FindClassEntry(cls,name);
  if (entry) return entry;

  WarningMessage("Resource title %s not found", (const char *)name);
  return ConstParamEntryPtr();
}

////////////////////////////////////////////////////////////////////////////////
// single mission bank

#if _ENABLE_UNSIGNED_MISSIONS

static inline void RemoveSingleMissionBank()
{
  GFileBanks.Remove("missions\\__cur_sp.");
}

//! reads packed mission file (pbo) and create bank
RString CreateSingleMissionBank(RString filename)
{
  // suppose filename is without extension (.pbo)

  // remove bank
  RemoveSingleMissionBank();

  // extract island name
  const char *ext = strrchr(filename, '.');
  RString island;
  if (ext) island = ext + 1;
  else island = "noland";

  // create bank
  QFBank *bank = new QFBank;
  bank->open(filename);
  RString prefix = RString("missions\\__cur_sp.") + island + RString("\\");
  prefix.Lower();
  bank->SetPrefix(prefix);
  BankList::WriteAccess banks(GFileBanks);
  banks.Add(bank);
  return prefix;
}

#endif // _ENABLE_UNSIGNED_MISSIONS

//! select template based user mission
void SetMissionParams(RString filename, RString displayName, bool multiplayer)
{
  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile f;
#ifdef _XBOX

# if _XBOX_VER >= 200

  XSaveGame save = GSaveSystem.OpenSave(filename);
  if (!save)
  {
    // Errors already handled
    return;
  }
  if (f.Parse(RString(SAVE_ROOT) + RString("mission.") + GetExtensionWizardMission(), NULL, NULL, &globals) != LSOK)
  {
    ReportUserFileError(filename, RString("mission.") + GetExtensionWizardMission(), displayName, false);
    return;
  }

# else
  if (!f.ParseSigned(filename, NULL, NULL, &globals))
  {
    const char *ptr = GetFilenameExt(filename);
    RString dir = filename.Substring(0, ptr - (const char *)filename);

    ReportUserFileError(dir, ptr, RString(), false);
    return;
  }
# endif

#else
  if (f.Parse(filename, NULL, NULL, &globals) != LSOK)
  {
    return;
  }
#endif

  RString path = f >> "template";
  bool noCopy = false;
  ConstParamEntryPtr entry = f.FindEntry("noCopy");
  if (entry) noCopy = *entry;

  RString world = f >> "island";

  bool dir = true;
#if _ENABLE_UNSIGNED_MISSIONS
  if (!noCopy && QIFileFunctions::FileExists(path + RString(".pbo")))
  {
    // bank
    dir = false;
  }
#endif // _ENABLE_UNSIGNED_MISSIONS

  RString directory;
  RString name;
  const char *ptr = strrchr(path, '\\');
  if (ptr)
  {
    name = ptr + 1;
    directory = path.Substring(0, ptr + 1 - path);
  }
  else name = path;

  if (dir)
  {
    if (multiplayer)
    {
      SetBaseDirectory(false, "");
      GetNetworkManager().CreateMission("", "");
    }
    SetMission(world, name, directory, filename, displayName);
  }
#if _ENABLE_UNSIGNED_MISSIONS
  else
  {
    const char *prefix;
    if (multiplayer)
    {
      SetBaseDirectory(false, "");
      prefix = "mpmissions\\__cur_mp.";
    }
    else
    {
      prefix = "missions\\__cur_sp.";
    }
    GFileBanks.Remove(prefix);

    // create bank
    QFBank *bank = new QFBank;
    bank->open(path);
    RString str = RString(prefix) + world + RString("\\");
    str.Lower();
    bank->SetPrefix(str);
    BankList::WriteAccess banks(GFileBanks);
    banks.Add(bank);

    if (multiplayer)
    {
      SetMission(world, "__cur_mp", MPMissionsDir, filename, displayName);
      GetNetworkManager().SetOriginalName(path);
    }
    else
      SetMission(world, "__cur_sp", MissionsDir, filename, displayName);

    Glob.header.filenameReal = name;
  }
#endif
}

#if defined _XBOX && _XBOX_VER >= 200

__int64 GetUserMissionIndex()
{
  // default
  __int64 index = 1;

  // read the value from the config
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile file;
  if (ParseUserParams(file, &globals))
  {
    ConstParamEntryPtr entry = file.FindEntry("userMissionId");
    if (entry)
    {
      RString strVal = *entry;
      if (sscanf(strVal, "%I64x", &index) != 1) index = 1;
    }
  }
  else
  {
    Fail("User settings not read, user mission index reset!");
  }

  // write the incremented value to the config
  {
    // 32 chars at most
    char buffer[33];
    sprintf(buffer, "%I64x", index + 1);
    file.Add("userMissionId", buffer);
    SaveUserParams(file);
  }

  return index;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// Interface

Object *GetObject(GameValuePar oper);

//! Edit profile display
class DisplayEditProfile : public Display
{
protected:
  RString _name;

  RString _face;
  RString _glasses;

  RString _speakerType;
  float _pitch;
  RString _mask;
  
  ControllerScheme _scheme;

  float _musicVolume;
  float _voiceVolume;
  float _effectsVolume;

  float _bright;
  float _gamma;

  bool _titles;
  bool _radio;
  bool _inGame;

#ifdef _XBOX
  RString _account;
#endif

  //! face preview control
  Ref<CHead> _head;
  
  InitPtr<ITextContainer> _sumName;
  InitPtr<CStructuredText> _sumController;
  InitPtr<CStructuredText> _sumAccount;

  InitPtr<CStructuredText> _signInStatus;

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayEditProfile(ControlsContainer *parent, RString name, bool inGame);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);

  void OnSimulate(EntityAI *vehicle);
  void OnChildDestroyed(int idd, int exit);

  void SaveConfig(RString newName);
  void ApplyProfile();

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // on Xbox 360, the user cannot change the profile in-game
#else
  RString GetName() const {return _name;}
#endif

protected:
  void LoadConfig();
};

class DisplayProfileFace : public Display
{
protected:
  InitPtr<CListBoxContainer> _face;
  InitPtr<CListBoxContainer> _glasses;

  RString _oldFace;
  RString _oldGlasses;

  //! face preview control
  Ref<CHead> _head;

  //! enabled face type
  RString _faceType;

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayProfileFace(ControlsContainer *parent, bool enableSimulation, RString face, RString glasses, RString player);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  void OnSimulate(EntityAI *vehicle);

  RString GetFace() const;
  RString GetGlasses() const;
};

class DisplayProfileVoice : public Display
{
  typedef Display base;
  
protected:
  InitPtr<CListBoxContainer> _speakerType;
  InitPtr<CSliderContainer> _pitch;
  InitPtr<CListBoxContainer> _mask;
  InitPtr<CListBoxContainer> _throughSpeakers;

  RString _oldSpeakerType;
  float _oldPitch;
  RString _oldMask;
  bool _oldThroughSpeakers;
  bool _oldEnableVoice;

  //! if voice preview will be performed
  bool _doPreview;
  //! time when preview will start (if no additional user changes will be performed)
  DWORD _previewTime;
  //! preview sound
  RefAbstractWave _previewSpeech;

  //! face preview control
  Ref<CHead> _head;

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayProfileVoice
  (
    ControlsContainer *parent, bool enableSimulation,
    RString speakerType, float pitch, RString mask, bool throughSpeakers,
    RString resource, RString face, RString glasses
  );
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  ControlObject *OnCreateObject(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  void OnSimulate(EntityAI *vehicle);

  RString GetSpeakerType() const;
  float GetPitch() const;
  RString GetMask() const;
  bool IsThroughSpeakers() const;

protected:
  void Preview(RString speakerType, float pitch);
};

struct ActionDesc
{
  RString action;
  int key;
};
TypeIsMovableZeroed(ActionDesc)

class DisplayProfileAudio : public Display
{
protected:
  InitPtr<CSliderContainer> _music;
  InitPtr<CSliderContainer> _voice;
  InitPtr<CSliderContainer> _effects;

  float _oldMusic;
  float _oldVoice;
  float _oldEffects;

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayProfileAudio(ControlsContainer *parent, bool enableSimulation, float music, float voice, float effects);
  void Destroy();
  
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSliderPosChanged(IControl *ctrl, float pos);

  float GetMusicVolume() const;
  float GetVoiceVolume() const;
  float GetEffectsVolume() const;
};

class DisplayProfileVideo : public Display
{
protected:
  InitPtr<CSliderContainer> _bright;
  InitPtr<CSliderContainer> _gamma;
  InitPtr<CListBoxContainer> _titles;
  InitPtr<CListBoxContainer> _radio;

  float _oldBright;
  float _oldGamma;
  bool _oldTitles;
  bool _oldRadio;

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayProfileVideo(ControlsContainer *parent, bool enableSimulation, float bright, float gamma, bool titles, bool radio);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnLBSelChanged(IControl *ctrl, int curSel);

  float GetBright() const;
  float GetGamma() const;
  bool GetTitles() const;
  bool GetRadio() const;
};

//! Display for selection of single mission
class DisplaySingleMission : public Display
{
protected:
  //! wanted exit code (exit is performed after close animation is finished)
  int _exitWhenClose;

  /// Root directory (empty for true Single Missions directory)
  RString _root;
  /// Currently listed directory
  RString _directory;
  RString _configclass;

#if _ENABLE_MISSION_CONFIG
  /// config path to config of addon mission 
  AutoArray<RString> _folder;
#endif

  CListBoxContainer *_missionList;
  CHTMLContainer *_overview;

  SaveGameType _saveType;

  bool _userMissions;
  bool _package;

  bool _showAll;

#if defined _XBOX && _XBOX_VER < 200
  // list of all mission save files
  AutoArray<SaveHeader> _saves;
#endif

  AutoArray<RString> _activeKeys;
  Ref<Texture> _done;
  Ref<Texture> _none;
  Ref<Texture> _locked;
  Ref<Texture> _packageImage;

  RString _overviewLockedMission;
  RString _overviewMyMissions;
  RString _overviewNewMission;

public:
  //! selected difficulty
  int _difficulty;

  enum ItemType
  {
//    ITParentDirectory,
#if _ENABLE_WIZARD
    ITUserMissions,
#endif
    ITSubdirectory,
    ITPackage,
#if _ENABLE_UNSIGNED_MISSIONS
    ITMissionDirectory,
    ITMissionBank,
#endif
#if _ENABLE_WIZARD
    ITTemplateNew,
    ITTemplateOpen,
#endif
    ITLocked,
    ITDownloaded,
#if _ENABLE_WIZARD
    ITTemplateCorrupted,
#endif
    ITAddonFolder,
    ITAddon,
  };

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplaySingleMission(ControlsContainer *parent, RString root = RString(), RString missionsSpace = RString());
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  void OnLBDblClick(int idc, int curSel);
  void OnButtonClicked(int idc);
  void OnCtrlClosed(int idc);
  void OnSimulate(EntityAI *vehicle);
  void OnChildDestroyed(int idd, int exit);
  RString GetDirectory() {return _directory;}

protected:
  //! perform updates (show / hide buttons etc.) when mission selection is changed
  void OnChangeMission();
  //! perform updates (show / hide buttons etc.) when difficulty is changed
  void OnChangeDifficulty();
  //! process resume button
  void OnLoadMission();
  //! process restart button
  void OnRestartMission();
  //! load options for display from user profile
  void LoadParams();
  //! save options for display to user profile
  void SaveParams();
  //! save last played mission
  void SaveLastMission();
  //! reloads directory content
  void LoadDirectory();
#if _ENABLE_WIZARD
  //! update list of user missions
  void UpdateUserMissions(RString selected);
#endif
  //! search for save files
  void UpdateSaves();
  //! update dialog title
  void UpdateTitle();
  //! search, where are stored save files for given mission
#if defined _XBOX && _XBOX_VER >= 200
  XSaveGame GetMissionSave(RString dir) const;
#else
  RString FindMissionSaveDirectory(RString dir) const;
#endif
  //! Add mission in bank into list
  void AddMissionInBank(RString bankName, ItemType type);
  //! Add mission in addon into list
  void AddMissionInAddon(ParamEntryVal entry);
  /// Report the mission file is damaged
  void ReportCorrupted(RString name);

#if defined _XBOX && _XBOX_VER >= 200
  /// handle storage device change
  virtual void OnStorageDeviceChanged();
  /// handles storage device removal
  virtual void OnStorageDeviceRemoved();
  /// handles saves change
  virtual void OnSavesChanged();
#endif

};


/*!
  \patch 5145 Date 3/22/2007 by Flyman
  - Fixed: Brightness limits were too benevolent - cheating was possible
*/
#ifdef _XBOX
const float MinBrightness = 0.4f;
const float MaxBrightness = 1.8f;
#else
const float MinBrightness = 0.5f;
const float MaxBrightness = 1.5f;
#endif

/// sample parameter for ValueOption class
struct ValueAccess
{
  /// type of the value controlled
  typedef float Type;
  
  /// get default value
  static Type GetDefault() {return 0;}
  /// get actual value
  static Type Get() {return 0;}
  /// set during value changing
  static void Set(Type val) {}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return -1;}
};

/// template to simplify option setting UI
template <class ValueAccess>
struct ValueOption
{
  typedef typename ValueAccess::Type Type;
  /// old value stored for Cancel functionality
  Type _oldValue;

  /// submenu display can share ValueOption, avoid _oldValue overwrite
  bool _initialized;
  
  ValueOption() : _initialized(false) {}

  typedef ValueAccess ValueAccessType;
  // ValueAccess GetValueAccess() {return ValueAccess();}
  
  void Init()
  {
    if (_initialized) return;
    _initialized = true;
    _oldValue = ValueAccess::Get();
  }
  void Cancel()
  {
    ValueAccess::Set(_oldValue);
  }
  void Default()
  {
    ValueAccess::Set(ValueAccess::GetDefault());
  }
  
  int GetIDC() const {return ValueAccess::GetIDC();}
  
  bool IsControl(int idc) const {return idc==ValueAccess::GetIDC();}

  /// check if some change is pending
  bool IsChangePending() const {return _oldValue!=ValueAccess::Get();}


  // by default there is no handling for no events
  void OnCreateControl(int type, int idc, const ParamEntry &cls, Control *ctrl) {}
  void OnLBSelChanged(int idc, int curSel, CListBoxContainer *lbox) {}
  void OnSliderPosChanged(int idc, float pos, CSliderContainer *slider) {}
  void OnButtonClicked(int idc, ITextContainer *text) {}
  void OnUpdate(IControl *ctrl){}
};

/// sample parameter for SliderOption
struct SliderAccess: public ValueAccess
{
  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 0;}
  /// text of preview
  static RString GetText() {return RString();}
};

/// value controlled by a slider
template <class SliderAccess>
struct SliderOption: public ValueOption<SliderAccess>
{
  typedef SliderAccess ValueAccessType;
  typedef typename ValueOption<SliderAccess>::Type Type;

  // TODO: IRef
  InitPtr<ITextContainer> _preview;

  void OnCreateControl(int type, int idc, const ParamEntry &cls, Control *ctrl)
  {
    CSliderContainer *slider = GetSliderContainer(ctrl);
    if (!slider) return;

    Type minValue = SliderAccess::GetMin();
    Type maxValue = SliderAccess::GetMax();
    Type value = SliderAccess::Get();

    slider->SetRange(minValue, maxValue);
    slider->SetSpeed(0.01 * (maxValue - minValue), 0.1 * (maxValue - minValue));
    slider->SetThumbPos(value);
  }
  void OnSliderPosChanged(int idc, float value, CSliderContainer *slider)
  {
    SliderAccess::Set(value);
    if (_preview) _preview->SetText(SliderAccess::GetText());
  }
  void SetPreview(IControl *ctrl)
  {
    _preview = GetTextContainer(ctrl);
    if (_preview) _preview->SetText(SliderAccess::GetText());
  }
  void OnUpdate(IControl *ctrl)
  {
    CSliderContainer *slider = GetSliderContainer(ctrl);
    if (!slider) return;
    
    Type value = SliderAccess::Get();
    slider->SetThumbPos(value);
    if (_preview) _preview->SetText(SliderAccess::GetText());
  }

  void UpdateRange(IControl *ctrl)
  {
    CSliderContainer *slider = GetSliderContainer(ctrl);
    if (!slider) return;

    Type minValue = SliderAccess::GetMin();
    Type maxValue = SliderAccess::GetMax();
    Type value = SliderAccess::Get();

    slider->SetRange(minValue, maxValue);
    slider->SetSpeed(0.01 * (maxValue - minValue), 0.1 * (maxValue - minValue));
    slider->SetThumbPos(value);
  }
};

/// sample parameter for ButtonOption
struct ButtonAccess: public ValueAccess
{
  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}
  /// reaction to switch
  static void Switch() {}
  /// displayed text
  static RString GetText() {return RString();}
};

/// value controlled by a button
template <class ButtonAccess>
struct ButtonOption: public ValueOption<ButtonAccess>
{
  typedef ButtonAccess ValueAccessType;

  void OnCreateControl(int type, int idc, const ParamEntry &cls, Control *ctrl)
  {
    if (ctrl)
    {
      ctrl->ShowCtrl(ButtonAccess::Show());
      ctrl->EnableCtrl(ButtonAccess::Enable());
    }
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(ButtonAccess::GetText());
  }
  void OnButtonClicked(int idc, ITextContainer *text)
  {
    ButtonAccess::Switch();
    if (text) text->SetText(ButtonAccess::GetText());
  }
  void OnUpdate(IControl *ctrl)
  {
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(ButtonAccess::GetText());
  }
};

/// sample parameter for ListBoxOption
struct ListBoxAccess: public ValueAccess
{
  /// get number of possible selections
  static int GetNValues() {return 1;}
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return NULL;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return 0;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
};

/// value controlled by a listbox
template <class ListBoxAccess>
struct ListBoxOption: public ValueOption<ListBoxAccess>
{
  typedef ListBoxAccess ValueAccessType;
  typedef typename ValueOption<ListBoxAccess>::Type Type;
  // ListBoxAccess GetValueAccess() {return ListBoxAccess();}

  void Update(CListBoxContainer *lbox, bool init = false)
  {
    if (!lbox) return;
    lbox->ClearStrings();
    int sel = 0, selI = 0;
    Type value = ListBoxAccess::Get();
    float selDiff = FLT_MAX; // TODO: more flexible max.
    int nValues = ListBoxAccess::GetNValues();
    // const int **ids = ListBoxAccess::GetValueIds();
    for (int i=0; i<nValues; i++)
    {
      const Type valueI = ListBoxAccess::GetValue(i);
      if (!ListBoxAccess::IsValid(valueI)) continue;
      int index = lbox->AddString(ListBoxAccess::GetText(i));
      lbox->SetValue(index,i);
      float diff = ListBoxAccess::Distance(valueI, value);
      if (diff<selDiff) selDiff = diff, sel = selI;
      selI++;
    }
    lbox->SetCurSel(sel, !init);
  }

  void OnCreateControl(
    int type, int idc, const ParamEntry &cls, Control *ctrl
  )
  {
    // fill the listbox with values
    CListBoxContainer *lbox = GetListBoxContainer(ctrl);
    if (lbox) Update(lbox, true);
  }
  
  void OnLBSelChanged(int idc, int curSel, CListBoxContainer *lbox)
  {
    int nValues = ListBoxAccess::GetNValues();
    int index = lbox->GetValue(curSel);
    Assert(index>=0 && index<nValues);
    if (index>=0 && index<nValues)
    {
      const Type value = ListBoxAccess::GetValue(index);
      // set a value
      ListBoxAccess::Set(value);
    }
  }

  void OnUpdate(IControl *ctrl)
  {
    CListBoxContainer *lbox = GetListBoxContainer(ctrl);
    if (lbox) Update(lbox, true);
  }
};

/// functor for operating controls - init the control
struct InitOp
{
  /// any control type will do, as long as it has Init function
  template <class ControlType>
    void operator () (ControlType &ctrl) const {ctrl.Init();}
};

/// functor for operating controls - cancel the control
struct CancelOp
{
  /// any control type will do, as long as it has Init function
  template <class ControlType>
    void operator () (ControlType &ctrl) const {ctrl.Cancel();}
};

/// functor for operating controls - set default value
struct DefaultOp
{
  ControlsContainer *_disp;
  DefaultOp(ControlsContainer *disp)
  :_disp(disp)
  {
  }
  
  /// any control type will do, as long as it has Init function
  template <class ControlType>
  void operator () (ControlType &ctrl) const
  {
    ctrl.Default();
    int idc = ctrl.GetIDC();
    IControl *c = _disp->GetCtrl(idc);
    if (c) ctrl.OnUpdate(c);
  }
};

/// functor for operating controls - refresh value based on the current one
struct RefreshOp
{
  ControlsContainer *_disp;
  RefreshOp(ControlsContainer *disp)
  :_disp(disp)
  {
  }
  
  /// any control type will do, as long as it has Init function
  template <class ControlType>
  void operator () (ControlType &ctrl) const
  {
    int idc = ctrl.GetIDC();
    IControl *c = _disp->GetCtrl(idc);
    if (c) ctrl.OnUpdate(c);
  }
};

/// operating controls - react to create message
struct CheckOnCreateControl
{
  int _type;
  int _idc;
  const ParamEntry &_cls;
  Control *_ctrl;

  CheckOnCreateControl(int type, int idc, const ParamEntry &cls, Control *ctrl)
    :_type(type),_idc(idc),_cls(cls),_ctrl(ctrl)
  {
  }

  template <class ControlType>
  void operator () (ControlType &ctrl) const
  {
    if (ctrl.IsControl(_idc)) ctrl.OnCreateControl(_type,_idc,_cls,_ctrl);
  }
};

struct CheckOnCreateParentControl
{
  ControlsContainer* _parent;
  int _type;
  int _idc;
  const ParamEntry &_cls;
  Control *_ctrl;

  CheckOnCreateParentControl(ControlsContainer *parent, int type, int idc, const ParamEntry &cls, Control *ctrl)
    : _parent(parent), _type(type),_idc(idc),_cls(cls),_ctrl(ctrl)
  {
  }

  template <class ControlType>
  void operator () (ControlType &ctrl) const
  {
    if (ctrl.IsControl(_idc)) ctrl.OnCreateControl(_type,_idc,_cls,_ctrl);
  }
};

/// operating controls - react to listbox change
struct CheckOnLBSelChanged
{
  int _idc;
  int _curSel;
  CListBoxContainer *_lbox;

  CheckOnLBSelChanged(int idc, int curSel, CListBoxContainer *lbox)
    :_idc(idc),_curSel(curSel),_lbox(lbox)
  {}

  template <class ControlType>
    void operator () (ControlType &ctrl) const
  {
    if (ctrl.IsControl(_idc)) ctrl.OnLBSelChanged(_idc,_curSel,_lbox);
  }
};

/// operating controls - react to slider change
struct CheckOnSliderPosChanged
{
  int _idc;
  float _pos;
  CSliderContainer *_slider;

  CheckOnSliderPosChanged(int idc, float pos, CSliderContainer *slider)
    :_idc(idc), _pos(pos), _slider(slider)
  {}

  template <class ControlType>
  void operator () (ControlType &ctrl) const
  {
    if (ctrl.IsControl(_idc)) ctrl.OnSliderPosChanged(_idc, _pos, _slider);
  }
};

/// operating controls - react to button change
struct CheckOnButtonClicked
{
  int _idc;
  ITextContainer *_text;

  CheckOnButtonClicked(int idc, ITextContainer *text)
    :_idc(idc), _text(text)
  {}

  template <class ControlType>
    void operator () (ControlType &ctrl) const
  {
    if (ctrl.IsControl(_idc)) ctrl.OnButtonClicked(_idc, _text);
  }
};


static const int *HighLowSteps5[]={
  &IDS_TERRAIN_50, // very low
  &IDS_TERRAIN_25, // low
  &IDS_TERRAIN_12_5, // normal
  &IDS_TERRAIN_6_25, // high
  &IDS_TERRAIN_3_125, // very high
};

static const int *HighLowStepsVRAM[]={
  //&IDS_TERRAIN_50, // very low
  &IDS_TEX_256, // low 256
  &IDS_TEX_512, // normal 512
  &IDS_TEX_1024, // high 1024
  &IDS_TEX_2048, // very high 2048
  &IDS_TEX_AUTO, // automatic
};

/// access for complexity control 
struct ComplexityAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() {return Glob.config.videoOptions._defaultSettings._objectQuality._value;}
  /// get actual value
  static Type Get() {return GScene->GetComplexityTarget();}
  /// set during value changing
  static void Set(Type val) {GScene->SetComplexityTarget(val);}
  /// control ID
  static int GetIDC() {return IDC_OBJECTS_DETAIL;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._objectQuality.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._objectQuality.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return Glob.config.videoOptions._objectQuality.GetValue(index)._value;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for texture quality control 
struct TextureQualityAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() 
  { 
    if (Glob.config.videoOptions._defaultSettings._textureQualityIndex >= 0)
      return Glob.config.videoOptions._defaultSettings._textureQualityIndex;
    else
      return Glob.config.videoOptions._textureQuality.GetSize() / 2; 
  }
  /// get actual value
  static Type Get() {return GEngine->GetTextureQuality();}
  /// set during value changing
  static void Set(Type val) {GEngine->SetTextureQuality(val);}
  /// control ID
  static int GetIDC() {return IDC_TEXTURE_DETAIL;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._textureQuality.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._textureQuality.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return value <= GEngine->GetMaxTextureQuality();}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for texture quality control 
struct TextureMemoryAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() {return GEngine->GetTextureMemoryDefault();}
  /// get actual value
  static Type Get() {return GEngine->GetTextureMemory();}
  /// set during value changing
  static void Set(Type val) {GEngine->SetTextureMemory(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_VRAM_VALUE;}

  static const int NValues = 5;
  
  /// get number of possible selections
  static int GetNValues() {return NValues;}
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return HighLowStepsVRAM;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return value<=GEngine->GetMaxTextureMemory();}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
};

/// access for HDR quality control 
struct HDRQualityAccess
{
  /// type of the value controlled
  typedef int Type;
 
  /// get default value
  static Type GetDefault() {return Glob.config.videoOptions._defaultSettings._hdr._value;}
  /// get actual value
  static Type Get() {return Engine::HDRPrecToBits(GEngine->HDRPrecision());}
  /// set during value changing
  static void Set(Type val) {GEngine->SetHDRPrecision(Engine::BitsToHDRPrec(val));}
  /// control ID
  static int GetIDC() {return IDC_HDR_DETAIL;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._hdr.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._hdr.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return Glob.config.videoOptions._hdr.GetValue(index)._value;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for Antialiasing control 
struct FSAAQualityAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() {return 0;}
  /// get actual value
  static Type Get() {return GEngine->GetFSAA();}
  /// set during value changing
  static void Set(Type val) 
  { 
    if ( !val && Get() )
    { // antialiasing disabled => DisableAtoC disabled
      GEngine->SetAToC(val);
    }
    GEngine->SetFSAA(val);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_FSAA_DETAIL;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._fsaa.GetSize();}
  /// get text of the given row
  static RString GetText(int index) { return Glob.config.videoOptions._fsaa.GetText(index); }

  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
};

/// access for VSYNC contrl
struct VSyncAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get default value
  static Type GetDefault() {return true;}
  /// get actual value
  static Type Get() {return GEngine->GetVSync();}
  /// set during value changing
  static void Set(Type val) {GEngine->SetVSync(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_VSYNC_VALUE;}

  /// get number of possible selections
  static int GetNValues() {return 2;}
  /// get text of the given row
  static RString GetText(int index)
  {
    switch (index)
    {
    case 0: return LocalizeString(IDS_DISABLED);
    case 1: return LocalizeString(IDS_ENABLED);
    default:
      // if not known, return a number
      return Format("%d",index);
    }
  }

  /// get values for selections
  static Type GetValue(int index) {return index!=0;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
};

/// access for anisotropy filtering control 
struct AFQualityAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() {return Glob.config.videoOptions._defaultSettings._anisotropicFiltering._value;}
  /// get actual value
  static Type Get() {return GEngine->GetAnisotropyQuality();}
  /// set during value changing
  static void Set(Type val) {GEngine->SetAnisotropyQuality(val);}
  /// control ID
  static int GetIDC() {return IDC_ANISO_DETAIL;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._anisotropicFiltering.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._anisotropicFiltering.GetText(index);}
  /// get values for selections
  static Type GetValue(int index)  {return Glob.config.videoOptions._anisotropicFiltering.GetValue(index)._value;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

struct PPAAQualityAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault() 
  {
    int val = Glob.config.videoOptions._defaultSettings._ppaaIndex;
    if (val >= 0)
      return val;
    else
      return 0;
  }
  /// get actual value
  static Type Get() {return GEngine->GetPPAAQuality();}
  /// set during value changing
  static void Set(Type val)  {GEngine->SetPPAAQuality(val);}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_FXAA_VALUE;} 
  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._ppaa.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._ppaa.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for PIP control
struct PIPQualityAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault()
  {
    int val = Glob.config.videoOptions._defaultSettings._pipIndex;
    if (val >= 0)
      return val;
    else
      return 0;
  }

  /// get actual value
  static Type Get() 
  {
#if _HELISIM_RENDER_TO_TEXTURE
    return GEngine->GetPipQuality();
#else
    return 0;
#endif
  }
  /// set during value changing
  static void Set(Type val) 
  {
#if _HELISIM_RENDER_TO_TEXTURE
    GEngine->SetPipQuality(val);
#endif
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_PIP_VALUE;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._pip.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._pip.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for simul weather control, define number of helper geometry layers
struct SimulWeatherQualityAccess
{
  /// type of the value controlled
  typedef int Type;
  /// get default value
  static Type GetDefault() 
  {
    int val = Glob.config.videoOptions._defaultSettings._cloudQualityIndex;
    if (val >= 0)
      return val;
    else
      return 0;
  }
  /// get actual value
  static Type Get() 
  {
#if _ARMA3_SIMUL_WEATHER
    return Glob.config.videoOptions._cloudQuality.FindNearest(GEngine->GetSWGeometryLayersCount());
#else
    return 0;
#endif
  }
  /// set during value changing
  static void Set(Type val) 
  {     
#if _ARMA3_SIMUL_WEATHER
    GLandscape->SetSimulWeatherLayersCount(Glob.config.videoOptions._cloudQuality.GetValue(val)._value);
#endif
  }
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_SW_VALUE;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._cloudQuality.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._cloudQuality.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};


/// access for postprocess effects control 
struct PostprocessEffectsAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault() {return 0;}
  /// get actual value
  static Type Get() {return GEngine->GetPostprocessEffects();}
  /// set during value changing
  static void Set(Type val) {GEngine->SetPostprocessEffects(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_POSTPROCESS_EFFECTS;}

  /// get number of possible selections
  static int GetNValues() {return 6;}
  /// get text of the given row
  static RString GetText(int index)
  {
    switch (index)
    {
    default:
      Fail("Unsupported postprocess effects level");
    case 0: return LocalizeString(IDS_DISABLED);            // disabled
    case 1: return LocalizeString(IDS_TERRAIN_50);          // very low
    case 2: return LocalizeString(IDS_TERRAIN_25);          // low
    case 3: return LocalizeString(IDS_TERRAIN_12_5);        // normal
    case 4: return LocalizeString(IDS_TERRAIN_6_25);        // high
    case 5: return LocalizeString(IDS_TERRAIN_3_125);       // very high
    }
  }

  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
};

/// access for preset (level) control 
/** Note: preset control requires special handling in several cases */
struct PresetAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault(){Fail("Not implemented");return 0;}
  /// get actual value
  static Type Get()
  {
    int index = Glob.config.videoOptions.DetectCurrentOverallSettings(false);
    if (index == -1)
      return Glob.config.videoOptions._overallSettings.Size(); //  custom

    return index;
  }
  /// set during value changing
  static void Set(Type val) {Fail("Not implemented");}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_QUALITY_PREFERENCE;}

  /// get number of possible selections
  static int GetNValues() 
  { 
    int count = Glob.config.videoOptions._overallSettings.Size();
    int index = Glob.config.videoOptions.DetectCurrentOverallSettings(false);
    if (index == -1)
      count++; //add "Custom" value

    return count;
  }
  /// get text of the given row
  static RString GetText(int index)
  {
    if (index >= 0 && index < Glob.config.videoOptions._overallSettings.Size())
      return Glob.config.videoOptions._overallSettings[index]._text;
    else 
      return LocalizeString(IDS_DISP_OPT_ASPECT_RATIO_CUSTOM);
  }

  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

// image-based, post-processing antialiasing technique
struct AToCAccess
{
  /// type of the value controlled
  typedef VideoOptionsConfig::ATOCValue Type;

  /// get default value
  static Type GetDefault() {return Glob.config.videoOptions._defaultSettings._atocQuality;}
  /// get actual value
  static Type Get() 
  {
    return Type::ConvertATOCMaskToValue((Type::AToCMask)GEngine->GetAToC());
  }
  /// set during value changing
  static void Set(Type val) 
  {
    // set new value only when FSAA is enabled
    if (GEngine->GetFSAA())
    {
      GEngine->SetAToC(Type::ConvertATOCValueToMask(val));
    }
  }
  /// control ID
  static int GetIDC() {return IDC_ATOC_DETAIL;} 
  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._atocQuality.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._atocQuality.GetText(index);}

  /// get values for selections
  static Type GetValue(int index) {return  Glob.config.videoOptions._atocQuality.GetValue(index);}
  /// check if value can be used
  static bool IsValid(Type value) 
  {
    if (GEngine->GetFSAA())
    {
      return true;
    }
    else
    {
      // when MSAA is not enables, ATOC have no sense - enable only "Disabled" option
      return !value.IsATOCEnabled();
    }
  }
  /// distance of values
  static float Distance(Type value1, Type value2) 
  {
    return value1.Distance(value2);
  }
  /// is ctrl enabled?
  static bool IsEnabled() {return GEngine && GEngine->GetFSAA();}
};

struct AmbOcclusionAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get default value
  static Type GetDefault() {return true;}
  /// get actual value
  static Type Get() 
  {
    return GEngine->IsEnabledSSAO();
  }
  /// set during value changing
  static void Set(Type val) 
  {
    GEngine->EnableSSAO(val);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_AMBIENT_OCCLUSION;} 
  /// get number of possible selections
  static int GetNValues() {return 2;}
  /// get text of the given row
  static RString GetText(int index)
  {
    switch (index)
    {
    default:
    case 0: return LocalizeString(IDS_DISABLED);
    case 1: return LocalizeString(IDS_ENABLED);
    }
  }

  /// get values for selections
  static Type GetValue(int index) {return index ? true : false;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
};

/// access for Bloom control
struct BloomAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return 1-0.75f;}
  /// get actual value
  static Type Get() 
  {
    return 1-GEngine->GetBloom();
  }
  /// set during value changing
  static void Set(Type val) 
  {
    GEngine->SetBloom(1-val);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_BLOOM;}

  /// get minimal value
  static Type GetMin() {return 0.0f;}
  /// get maximal value
  static Type GetMax() {return 1.0f;}
  /// text of preview
  static RString GetText() {return Format("%.1f", 0.0f); }
};

/// access for RotationBlur control
struct RotationBlurAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get minimal value
  static Type GetMinRaw() {return 0.0f;}
  /// get maximal value
  static Type GetMaxRaw() {return 0.09f;}

  /// get default value
  static Type GetDefault() {return 0.015f;}
  /// get actual value
  static Type Get() {return GEngine->GetRotBlurPower(); }
  /// set during value changing
  static void Set(Type val) { GEngine->SetRotBlurPower(val); } 
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_ROTATION_BLUR;}

  /// get minimal value
  static Type GetMin() {return 0.0f;}
  /// get maximal value
  static Type GetMax() {return 0.09f;}
  /// text of preview
  static RString GetText() {return Format("%.2f", 0.0f); }
};

/// access for shading quality control 
struct ShadingQualityAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() {return 7;}
  /// get actual value
  static Type Get() {return GScene->GetShadingQuality();}
  /// set during value changing
  static void Set(Type val) {GScene->SetShadingQuality(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_SHADING_DETAIL;}

  static const int NValues = 5;
  static Type _values[NValues];
  
  /// get number of possible selections
  static int GetNValues() {return NValues;}
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return HighLowSteps5;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return _values[index];}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
};

int ShadingQualityAccess::_values[NValues]={0, 3, 7, 10, 100};

static const int *ShadowQualityHighLowSteps5[] =
{
  &IDS_DISP_OPT_DISABLED, // very low
  //&IDS_TERRAIN_25, // low
  &IDS_TERRAIN_12_5, // normal
  &IDS_TERRAIN_6_25, // high
  &IDS_TERRAIN_3_125, // very high
};

/// access for shading quality control 
struct ShadowQualityAccess
{
  /// type of the value controlled
  typedef int Type;
  
  /// get default value
  static Type GetDefault() 
  {
    int val = Glob.config.videoOptions._defaultSettings._shadowQualityIndex;
    if (val >= 0)
      return val;
    else
      return 0;
  }
  /// get actual value
  static Type Get() {return GScene->GetShadowQuality();}
  /// set during value changing
  static void Set(Type val) {GScene->SetShadowQuality(val);GEngine->CommitSettings();}
  /// control ID
  static int GetIDC() {return IDC_SHADOW_DETAIL;}
 
  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._shadowQuality.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._shadowQuality.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for terrain grid control 
struct TerrainAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GScene->GetDefaultTerrainGrid();}
  /// get actual value
  static Type Get() {return GScene->GetPreferredTerrainGrid();}
  /// set during value changing
  static void Set(Type val) {GScene->SetPreferredTerrainGrid(val);}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_TERRAIN;}

  /// get number of possible selections
  static int GetNValues() {return Glob.config.videoOptions._terrainQuality.GetSize();}
  /// get text of the given row
  static RString GetText(int index) {return Glob.config.videoOptions._terrainQuality.GetText(index);}
  /// get values for selections
  static Type GetValue(int index) {return Glob.config.videoOptions._terrainQuality.GetValue(index)._value;}
  /// check if value can be used
  static bool IsValid(Type value) {return value >= GScene->GetMinimalTerrainGrid();}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
  /// is ctrl enabled?
  static bool IsEnabled() {return true;}
};

/// access for resolution control 
struct ResolutionAccess
{
  /// type of the value controlled
  typedef ResolutionInfo Type;

  /// get default value
  static Type GetDefault()
  {
    ResolutionInfo info;
    // TODO: default values
    if (GEngine->IsWindowed())
    {
      info.w = 0;
      info.h = 0;
      info.bpp = 0; 
    }
    else
    {
      info.w = GEngine->WidthBB();
      info.h = GEngine->HeightBB();
      info.bpp = GEngine->PixelSize(); 
    }
    return info;
  }
  /// get actual value
  static Type Get()
  {
    ResolutionInfo info;
    if (GEngine->IsWindowed())
    {
      info.w = 0;
      info.h = 0;
      info.bpp = 0; 
    }
    else
    {
      info.w = GEngine->WidthBB();
      info.h = GEngine->HeightBB();
      info.bpp = GEngine->PixelSize(); 
    }
    return info;
  }
  /// set during value changing
  static void Set(Type val)
  {
    if (val.w == 0 && val.h == 0 && val.bpp == 0)
    {
      GEngine->SwitchWindowed(true);
    }
    else
    {
      GEngine->SwitchWindowed(false,val.w, val.h, val.bpp);
    }
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_RESOLUTION;}

  //! available resolutions
  static FindArray<ResolutionInfo> _resolutions;
  static bool _needInit;

  /// get number of possible selections
  static int GetNValues()
  {
    if (_needInit)
    {
      GEngine->ListResolutions(_resolutions);
      _needInit = false;
    }
    return _resolutions.Size();
  }
  /// get text of the given row
  static RString GetText(int index)
  {
    if (_needInit)
    {
      GEngine->ListResolutions(_resolutions);
      _needInit = false;
    }
    const ResolutionInfo &info = _resolutions[index];
    if (info.w==0 && info.h==0)
    {
      return LocalizeString(IDS_WINDOWED);
    }
    return Format(LocalizeString(IDS_RESOLUTION_FORMAT), info.w, info.h, info.bpp);
  }
  /// get values for selections
  static Type GetValue(int index)
  {
    if (_needInit)
    {
      GEngine->ListResolutions(_resolutions);
      _needInit = false;
    }
    return _resolutions[index];
  }
  /// check if value can be used
  static bool IsValid(const Type &value) {return true;}
  /// distance of values
  static float Distance(const Type &value1, const Type &value2)
  {
    return fabs(value1.w - value2.w) + fabs(value1.h - value2.h) + fabs(value1.bpp - value2.bpp);
  }
};

/// access for fillrate control 
struct FillrateControlAccess
{
  /// type of the value controlled
  typedef ResolutionInfo Type;
  //! available fillrate resolutions
  static FindArray<ResolutionInfo> _fillrates;

  /// get default value
  static Type GetDefault() {
    ResolutionInfo info;
    // TODO: default values

    info.w = GEngine->WidthBB();
    info.h = GEngine->HeightBB();
    info.bpp = GEngine->PixelSize(); 

    return info;
//  float minF,maxF;return ForDisplay(GEngine->GetDefaultFillrateControl(minF,maxF));}
  }
  /// get actual value
  static Type Get()
  {
    return GEngine->GetFillrateControl();
  }
  /// set during value changing
  static void Set(Type val)
  {
    GEngine->SetFillrateControl(val);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_SLIDER_FILLRATE;}

  /// get number of possible selections
  static int GetNValues()
  {
    GEngine->ListFillrateResolutions(_fillrates);
    return _fillrates.Size();
  }
  /// get text of the given row
  static RString GetText(int index)
  {
    GEngine->ListFillrateResolutions(_fillrates);
    const ResolutionInfo &info = _fillrates[index];

    float val = sqrt(float(info.w*info.h)/(GEngine->WidthBB()*GEngine->HeightBB()))*100;
    return Format("%dx%d (%.0f%%)", info.w, info.h, val);
  }
  /// get values for selections
  static Type GetValue(int index)
  {
    GEngine->ListFillrateResolutions(_fillrates);
    return _fillrates[index];
  }
  /// check if value can be used
  static bool IsValid(const Type &value) {return true;}
  /// distance of values
  static float Distance(const Type &value1, const Type &value2)
  {
    return fabs(value1.w - value2.w) + fabs(value1.h - value2.h) + fabs(value1.bpp - value2.bpp);
  }
};

FindArray<ResolutionInfo> ResolutionAccess::_resolutions;
bool ResolutionAccess::_needInit = true;
FindArray<ResolutionInfo> FillrateControlAccess::_fillrates;

/// access for refresh rate control 
struct RefreshRateAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault()
  {
    // TODO: default values
    return GEngine->RefreshRate();
  }
  /// get actual value
  static Type Get()
  {
    return GEngine->RefreshRate();
  }
  /// set during value changing
  static void Set(Type val)
  {
    GEngine->SwitchRefreshRate(val);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_REFRESH;}

  //! available refresh rates for selected resolution
  static FindArray<int> _refreshRates;
  static bool _needInit;

  /// get number of possible selections
  static int GetNValues()
  {
    if (_needInit)
    {
      GEngine->ListRefreshRates(_refreshRates);
      _needInit = false;
    }
    return _refreshRates.Size();
  }
  /// get text of the given row
  static RString GetText(int index)
  {
    Type value = _refreshRates[index];
    if (_needInit)
    {
      GEngine->ListRefreshRates(_refreshRates);
      _needInit = false;
    }
    if (value == 0) return LocalizeString(IDS_DISP_DEFAULT);
    return Format(LocalizeString(IDS_REFRESH_RATE_FORMAT), value);
  }
  /// get values for selections
  static Type GetValue(int index)
  {
    if (_needInit)
    {
      GEngine->ListRefreshRates(_refreshRates);
      _needInit = false;
    }
    return _refreshRates[index];
  }
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}
};

FindArray<int> RefreshRateAccess::_refreshRates;
bool RefreshRateAccess::_needInit = true;

/// access for gamma control 
struct GammaAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get minimal value
  static Type GetMinRaw() {return 0.5;}
  /// get maximal value
  static Type GetMaxRaw() {return 2.3;}
  
  static float ForDisplay(float x)
  {
    float minF = GetMinRaw(),maxF = GetMaxRaw();
    Assert(minF<=1 && maxF>=1);
    // we want 1 to be in the middle
    if (x<=1) return Interpolativ(x,minF,1,0,1);
    else return Interpolativ(x,1,maxF,1,2);
  }
  static float InvForDisplay(float x)
  {
    float minF = GetMinRaw(),maxF = GetMaxRaw();
    GEngine->GetDefaultFillrateControl(minF,maxF);
    if (x<=1) return Interpolativ(x,0,1,minF,1);
    else return Interpolativ(x,1,2,1,maxF);
  }

  /// get default value
  static Type GetDefault() {return ForDisplay(GEngine->GetDefaultGamma());}
  /// get actual value
  static Type Get() {return ForDisplay(GEngine->GetGamma()); }
  /// set during value changing
  static void Set(Type val) {GEngine->SetGamma(InvForDisplay(val));}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_GAMMA_SLIDER;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 2;}
  /// text of preview
  static RString GetText() {return Format("%.1f", GEngine->GetGamma());}
};

/// access for brightness control 
struct BrightnessAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GEngine->GetDefaultBrightness();}
  /// get actual value
  static Type Get() {return GEngine->GetBrightness(); }
  /// set during value changing
  static void Set(Type val) {GEngine->SetBrightness(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_BRIGHT_SLIDER;}

  /// get minimal value
  static Type GetMin() {return MinBrightness;}
  /// get maximal value
  static Type GetMax() {return MaxBrightness;}
  /// text of preview
  static RString GetText() {return Format("%.1f", GEngine->GetBrightness());}
};

/// access for visibility control 
struct VisibilityAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return log(GScene->GetDefaultViewDistance());}
  /// get actual value
  static Type Get() {return log(GScene->GetPreferredViewDistance());}
  /// set during value changing
  static void Set(Type val) {GScene->SetPreferredViewDistance(exp(val));}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_VISIBILITY_SLIDER;}

  /// get minimal value
  static Type GetMin()
  {
#if _HELISIM_VIEW_DIST
    return log(2000);
#else
    return log(500);
#endif
  }
  /// get maximal value
  static Type GetMax()
  {
#ifdef _XBOX
    return log(5000);
#else
#if _VBS3
    return log(Glob.config.maxViewDistance);
#else
#if _HELISIM_VIEW_DIST
    return log(20000);
#else
    return log(12000);
#endif
#endif
#endif
  }
  /// text of preview
  static RString GetText() {return Format("%.0f", GScene->GetPreferredViewDistance());}
};

#if _HELISIM_VIEW_DIST
/// access for visibility control 
struct ObjectVisibilityAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return log(GScene->GetDefaultObjectViewDistance());}
  /// get actual value
  static Type Get() {return log(GScene->GetPreferredObjectViewDistance());}
  /// set during value changing
  static void Set(Type val) {GScene->SetPreferredObjectViewDistance(exp(val));}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_OBJECT_VISIBILITY_SLIDER;}

  /// get minimal value
  static Type GetMin()
  {
    return log(500);
  }
  /// get maximal value
  static Type GetMax()
  {
    return log(Glob.config.horizontZ);
  }
  /// text of preview
  static RString GetText() {return Format("%.0f", GScene->GetPreferredObjectViewDistance());}
};
#endif

#if _HELISIM_AMBIENT_TRAFFIC
/// access for ambient traffic control 
struct TrafficVisibilityAccess
{
  /// type of the value controlled
  typedef float Type;
  
  /// get default value
  static Type GetDefault() {return log(GScene->GetDefaultTrafficViewDistance());}
  /// get actual value
  static Type Get() {return log(GScene->GetPreferredTrafficViewDistance());}
  /// set during value changing
  static void Set(Type val) {GScene->SetPreferredTrafficViewDistance(exp(val));}
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_TRAFFIC_VISIBILITY_SLIDER;}

  /// get minimal value
  static Type GetMin()
  {
    return log(100);
  }
  /// get maximal value
  static Type GetMax()
  {
    return log(Glob.config.horizontZ);
  }
  /// text of preview
  static RString GetText() {return Format("%.0f", GScene->GetPreferredTrafficViewDistance());}
};
#endif

/// access for visibility control 
struct ShadowVisibilityAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return 0;}
  /// get actual value
  static Type Get() 
  {
#if _HELISIM_SHADOW_ZDISTANCE
    return GScene ? GScene->GetShadowZDistance() : Glob.config.GetMaxShadowSize();
#else
    return Glob.config.GetMaxShadowSize();
#endif
  }

  /// set during value changing
  static void Set(Type val) 
  { 
#if _HELISIM_SHADOW_ZDISTANCE
    if (GScene) GScene->SetShadowZDistance(val); 
#endif
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_SHADOW_VISIBILITY_SLIDER;}

  /// get minimal value
  static Type GetMin()
  {
    return 100.0f;
  }
  /// get maximal value
  static Type GetMax()
  {
    return Glob.config.MaxShadowZDistance;
  }
  /// text of preview
  static RString GetText() 
  {
#if _HELISIM_SHADOW_ZDISTANCE
    return Format("%.0f", GScene ? GScene->GetShadowZDistance() : Glob.config.GetMaxShadowSize());
#else
    return RString();
#endif
  }
};

#if _VBS3

#define MAXOJBDRAWDIS 3000.0f

struct DrawDistanceAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return log(Glob.config.maxObjDrawDistance);}
  /// get actual value
  static Type Get()
  { 
    return log(Glob.config.maxObjDrawDistance); 
  }
  /// set during value changing
  static void Set(Type val) 
  { 
    Glob.config.maxObjDrawDistance = exp(val);     
    GScene->SetPreferredViewDistance(GScene->GetPreferredViewDistance());

    // object draw distance changed
    ParamFile cfg;
    ParseFlashpointCfg(cfg);
    cfg.Add("MaxObjectDrawDistance", Glob.config.maxObjDrawDistance);
    SaveFlashpointCfg(cfg);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_DRAWDISTANCE_SLIDER;}

  /// get minimal value
  static Type GetMin()
  {
    return log(MAXOJBDRAWDIS);
  }
  /// get maximal value
  static Type GetMax()
  {
#ifdef _XBOX
    return log(5000);
#else
    return log(Glob.config.maxViewDistance);
#endif
  }
  /// text of preview
  static RString GetText() {return Format("%.0f",Glob.config.maxObjDrawDistance);}
};
#endif


/// access for Blood control 
struct BloodAccess: public ValueAccess
{
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}

  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault() {return 1;}
  /// get actual value
  static Type Get() {return Glob.config.bloodLevel;}
  /// set during value changing
  static void Set(Type val) {Glob.config.bloodLevel = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_BLOOD;}
  /// get number of possible selections
  static int GetNValues() {return 3;}

  /// get text of the given row
  static RString GetText(int index)
  {
    switch (index)
    {
      default:
        Fail("Unsupported blood level");
      case 0: return LocalizeString(IDS_DISP_OPT_DISABLED);
      case 1: return LocalizeString(IDS_TERRAIN_25);
      case 2: return LocalizeString(IDS_TERRAIN_6_25);
    }
  }
};

#if _HELISIM_IMPERIAL_SYSTEM
/// access for Blood control 
struct ImperialUnitsAccess: public ValueAccess
{
  /// get values for selections
  static Type GetValue(int index) {return index;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}

  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault() {return 0;}
  /// get actual value
  static Type Get() {return Glob.config.useImperialSystem;}
  /// set during value changing
  static void Set(Type val) {Glob.config.useImperialSystem = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_IMPERIALUNITS;}
  /// get number of possible selections
  static int GetNValues() {return 3;}

  /// get text of the given row
  static RString GetText(int index)
  {
    switch (index)
    {
    case 0: return LocalizeString(IDS_DISP_OPT_UNITS_METRIC);
    case 1: return LocalizeString(IDS_DISP_OPT_UNITS_MIXED);
    case 2: return LocalizeString(IDS_DISP_OPT_UNITS_IMPERIAL);
    default:
      Fail("Unsupported units system");
      return RString();
    }
  }
};
#endif

/// access for Blood control 
struct VehicleFreeLooksAccess: public ValueAccess
{
  /// type of the value controlled
  typedef int Type;

    /// get values for selections
  static Type GetValue(int index) {return index;};
  /// check if value can be used
  static bool IsValid(Type value) {return true;};
  /// distance of values
  static float Distance(Type value1, Type value2) {return abs(value1 - value2);}

  /// get default value
  static Type GetDefault() {return 0;}
  /// get actual value
  static Type Get() {return Glob.config.vehicleFreelook;}
  /// set during value changing
  static void Set(Type val) {Glob.config.vehicleFreelook = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_VEHICLEFREELOOK;}
  /// get number of possible selections
  static int GetNValues()
  {
#if _HELISIM_PRECISE_INTERACTION
    return 3;
#else
    return 2;
#endif
  }

  /// get text of the given row
  static RString GetText(int index)
  {
    switch(index)
    {
    case 1:
      return LocalizeString(IDS_FREELOOK_ENABLED);
#if _HELISIM_PRECISE_INTERACTION
    case 2:
      return LocalizeString(IDS_FREELOOK_ACTIONS_ENABLED);
#endif
    default:
      return LocalizeString(IDS_DISABLED);
    }
  }
};

/// access for WBuffer control 
struct WBufferAccess: public ValueAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get default value
  static Type GetDefault() {return false;}
  /// get actual value
  static Type Get() {return GEngine->IsWBuffer();}
  /// set during value changing
  static void Set(Type val) {GEngine->SetWBuffer(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_WBUFFER;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return GEngine->CanWBuffer();}
  /// reaction to switch
  static void Switch()
  {
    if (Show() && Enable())
    {
      Type value = !Get();
      Set(value);
    }
  }
  /// displayed text
  static RString GetText()
  {
    if (Get()) return LocalizeString(IDS_ENABLED);
    else return LocalizeString(IDS_DISABLED);
  }
};

enum AspectRatioPreset
{
  /// normal
  Aspect4To3,
  /// wide-screen
  Aspect16To9,
  /// wide-screen 16:10
  Aspect16To10,
  /// Wide TFT
  Aspect5To4,
  /// Triple head
  Aspect12To3,
  /// custom - only when loaded from config and not equal to any other
  AspectCustom,
  /// enum value count
  NAspectRatioPreset
};

static bool AspectPresetsReady = false;
static bool AspectPresetsCustom = false;

static AspectSettings AspectPresets[NAspectRatioPreset];

static void SetWideAspectSettings(AspectSettings &as, float ratio, bool tripleHead = false)
{
  as.leftFOV = ratio/(4.0f/3);
  as.topFOV = 0.75f;
  if (as.leftFOV>1)
  {
    // wider than default
    as.uiTopLeftX = (as.leftFOV-1)*0.5f/as.leftFOV;
    as.uiBottomRightX = 1-as.uiTopLeftX;
    as.uiTopLeftY = 0;
    as.uiBottomRightY = 1;
    if(ratio >= (3.5 - 0.0001)) tripleHead = true;
    else tripleHead = false;
  }
  else
  {
    // taller than default
    as.uiTopLeftX = 0;
    as.uiBottomRightX = 1;
    // scale down y axis accordingly
    // as.leftFOV / as.topFOV = ratio
    // we need biggest possible 4:3 area
    // we want vertical size 0.75
    as.uiTopLeftY = (as.topFOV/as.leftFOV-0.75)*0.5/as.topFOV;
    as.uiBottomRightY = 1-as.uiTopLeftY;
  }
  as.tripleHead = tripleHead;
}

static bool operator == (const AspectSettings &as1, const AspectSettings &as2)
{
  return (
    fabs(as1.leftFOV-as2.leftFOV)<1e-6 &&
    fabs(as1.topFOV-as2.topFOV)<1e-6 
  );
  // following can no longer be used, becouse of IGUI scale
  //fabs(as1.uiTopLeftX-as2.uiTopLeftX)<1e-6 &&
  //fabs(as1.uiBottomRightX-as2.uiBottomRightX)<1e-6 &&
  //fabs(as1.uiTopLeftY-as2.uiTopLeftY)<1e-6 &&
  //fabs(as1.uiBottomRightY-as2.uiBottomRightY)<1e-6
}

static void InitAspectPresets()
{
  if (AspectPresetsReady) return;
  AspectPresetsReady = true;
  // no init needed for 4:3 - default constructor works fine
  //AspectPresets[Aspect4To3].leftFOV = 
  SetWideAspectSettings(AspectPresets[Aspect16To9],(16.0f/9));
  SetWideAspectSettings(AspectPresets[Aspect16To10],(16.0f/10));
  SetWideAspectSettings(AspectPresets[Aspect5To4],(5.0f/4));
  SetWideAspectSettings(AspectPresets[Aspect12To3],(12.0f/3),true);
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  AspectPresets[AspectCustom] = as;
  // check if custom are really custom
  AspectPresetsCustom = true;
  for (int i=0; i<NAspectRatioPreset-1; i++)
  {
    if (AspectPresets[i]==as) {AspectPresetsCustom = false;}
  }
  
}

static const int *AspectStrings[]={
  &IDS_DISP_OPT_ASPECT_RATIO_NORMAL,
  &IDS_DISP_OPT_ASPECT_RATIO_WIDE,
  &IDS_DISP_OPT_ASPECT_RATIO_WIDE_16_10,
  &IDS_DISP_OPT_ASPECT_RATIO_TFT,
  &IDS_DISP_OPT_ASPECT_RATIO_TRIPLE,
  &IDS_DISP_OPT_ASPECT_RATIO_CUSTOM
};

/// access for aspect ratio control 
struct AspectRatioAccess
{
  /// type of the value controlled
  typedef AspectRatioPreset Type;
  
  /// get default value
  static Type GetDefault() {return Aspect4To3;}
  /// get actual value
  static Type Get();
  /// set during value changing
  static void Set(Type val)
  {
    InitAspectPresets();
    if (val<NAspectRatioPreset) GEngine->SetAspectSettings(AspectPresets[val]);
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_ASPECT_RATIO;}

  /// get number of possible selections
  static int GetNValues() {InitAspectPresets();return NAspectRatioPreset-1+AspectPresetsCustom;}
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return AspectStrings;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return AspectRatioPreset(index);}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}
};

//stringtable IDs for true/false listBox 
static const int *IGUISizeTable[]={
  &IDS_VERY_SMALL,
  &IDS_SMALL,  
  &IDS_MEDIUM, 
  &IDS_LARGE, 
  &IDS_VERY_LARGE, 
};

/// access for subtitles control 
struct IGUISizeAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return Glob.config.NormalScale*0.01;}
  /// get actual value
  static Type Get() {return Glob.config.IGUIScale;}
  /// set during value changing
  static void Set(Type val) {Glob.config.IGUIScale = val;}

  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_IGUISIZE;}
  /// get number of possible selections
  static int GetNValues() {return 5;}   //enabled and disabled

  /// get stringtable IDs for selections
  static const int **GetValueIds() {return IGUISizeTable;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index)
  {
    if(index == 0) return Glob.config.ExtraSmallScale*0.01;
    if(index == 1) return Glob.config.VerySmallScale*0.01;
    if(index == 2) return Glob.config.SmallScale*0.01;
    if(index == 3) return Glob.config.NormalScale*0.01;
    return Glob.config.LargeScale*0.01;
  }

  /// check if value can be used
  static bool IsValid(Type value) {return  true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}
};

AspectRatioAccess::Type AspectRatioAccess::Get()
{
  InitAspectPresets();
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  for (int i=0; i<NAspectRatioPreset; i++)
  {
    if (AspectPresets[i]==as) return AspectRatioPreset(i);
  }
  // no aspect found - it must be a custom one
  return NAspectRatioPreset;
}

/// access for Reverse mouse control 
struct RevMouseAccess: public ValueAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get actual value
  static Type Get() {return GInput.revMouse;}
  /// set during value changing
  static void Set(Type val) {GInput.revMouse = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_CONFIG_YREVERSED;}

  /// show the control
  static bool Show() {return true;}
  static bool Enable() {return true;}
  /// reaction to switch
  static void Switch()
  {
    if (Show())
    {
      Type value = !Get();
      Set(value);
    }
  }
  /// displayed text
  static RString GetText()
  {
    if (Get()) return LocalizeString(IDS_CONFIG_YREVERSED);
    else return LocalizeString(IDS_CONFIG_YNORMAL);
  }
};

/// access for Perspective control 
struct PerspectiveAccess: public ValueAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get actual value
  static Type Get() {return GInput.perspective;}
  /// set during value changing
  static void Set(Type val) 
  {
    GInput.perspective = val;
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_PERSPECTIVE;}

  /// show the control
  static bool Show() {return true;}
  static bool Enable() {return true;}
  /// reaction to switch
  static void Switch()
  {
    if (Show()) { Set((Get()==1) ? 3 : 1); }
  }
  /// displayed text
  static RString GetText()
  {
    if (Get()==1) return LocalizeString(IDS_OPTIONS_PERSPECTIVE_FIRST);
    else return LocalizeString(IDS_OPTIONS_PERSPECTIVE_3RD);
  }
};

/// access for field of view
struct FovAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get actual value
  static Type Get() {return GInput.fov;}
  /// set during value changing
  static void Set(Type val)
  {
    GInput.fov = val;
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_FIELD_OF_VIEW;}

  /// get minimal value
  static Type GetMin() {return 0.5;}
  /// get maximal value
  static Type GetMax() {return 2.0;}
  /// text of preview
  static RString GetText() {return Format("%.1f", Get());}
};

/// access for music volume control 
struct MusicAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GSoundsys ? GSoundsys->GetDefaultCDVolume() : 0;}
  /// get actual value
  static Type Get() {return GSoundsys ? GSoundsys->GetCDVolume() : 0;}
  /// set during value changing
  static void Set(Type val)
  {
    if (GSoundsys)
    {
      GSoundsys->SetCDVolume(val);
      GSoundsys->StartPreview();
    }
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_MUSIC_SLIDER;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 10;}
  /// text of preview
  static RString GetText() {return Format("%.1f", Get());}
};

/// access for effects volume control 
struct EffectsAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GSoundsys ? GSoundsys->GetDefaultWaveVolume() : 0;}
  /// get actual value
  static Type Get() {return GSoundsys ? GSoundsys->GetWaveVolume() : 0;}
  /// set during value changing
  static void Set(Type val)
  {
    if (GSoundsys)
    {
      GSoundsys->SetWaveVolume(val);
      GSoundsys->StartPreview();
    }
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_EFFECTS_SLIDER;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 10;}
  /// text of preview
  static RString GetText() {return Format("%.1f", Get());}
};

/// access for voices volume control 
struct VONAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GSoundsys ? GSoundsys->GetDefaultVoNVolume() : 0;}
  /// get actual value
  static Type Get() {return GSoundsys ? GSoundsys->GetVonVolume() : 0;}
  /// set during value changing
  static void Set(Type val)
  {
    if (GSoundsys)
    {
      GSoundsys->SetVonVolume(val);
      GSoundsys->StartPreview();
    }
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_VON_SLIDER;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 10;}

  /// text of preview
  static RString GetText() {return Format("%.1f", Get());}
};

/// access for Master volume control 
struct MasterVolumeAccess
{
  /// type of the value controlled
  typedef int Type;

  /// get default value
  static Type GetDefault() {return 65535;}
  /// get actual value
  static Type Get();
  /// set during value changing
  static void Set(Type val);
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_MASTER_VOLUME;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 65535;}

  /// text of preview
  static RString GetText() {return Format("%d%%", toInt(Get()/655.35f));}
};

int MasterVolumeAccess::Get()
{
#ifndef _CONSOLE
  int GetMasterVolume();
  return GetMasterVolume();
#else 
  return 65535;
#endif
}
void MasterVolumeAccess::Set(int value)
{
#ifndef _CONSOLE
  void SetMasterVolume(int value);
  SetMasterVolume(value);
  GSoundsys->StartPreview();
#endif
}

//! Options display
class DisplayOptions : public Display
{
protected:
  //! wanted exit code (exit is performed after close animation is finished)
  int _exitWhenClose;

public:
  ButtonOption<RevMouseAccess> _revMouse;
  SliderOption<FovAccess> _fov;
  ButtonOption<PerspectiveAccess> _perspective;
  SliderOption<MusicAccess> _music;
  SliderOption<EffectsAccess> _effects;
  SliderOption<VONAccess> _von;
  SliderOption<MasterVolumeAccess> _masterVolume;

  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_revMouse);
    op(_fov);
    op(_perspective);
    op(_music);
    op(_effects);
    op(_von);
    op(_masterVolume);
  }
  void Init() {ForEachControl(InitOp());}
  void Cancel() {ForEachControl(CancelOp());}
  void Refresh() {ForEachControl(RefreshOp(this));}

  //! constructor
  /*!
    \param parent parent display
  */
  DisplayOptions(ControlsContainer *parent, bool enableSimulation, bool credits);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void Destroy();

  void OnButtonClicked(int idc);
  void OnChildDestroyed(int idd, int exit);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnCtrlClosed(int idc);

  void DestroyHUD(int exit);
};

//! NOT USED CURRENTLY
class DisplayCredits : public Display
{
public:
  DisplayCredits(ControlsContainer *parent)
    : Display(parent)
  {
    Load("RscDisplayCredits");
  }
};

/**
The namespace seems to be no longer used.
*/
namespace DefaultVideoOptions
{
  int Complexity()
  {
    return ComplexityAccess::GetDefault();
  }
  int ShadingQuality()
  {
    return ShadingQualityAccess::GetDefault();
  }
  int ShadowQuality()
  {
    return ShadowQualityAccess::GetDefault();
  }
  int TextureQuality()
  {
    return TextureQualityAccess::GetDefault();
  }
  int TextureMemory()
  {
    return TextureMemoryAccess::GetDefault();
  }
  int HDRQuality()
  {
    return HDRQualityAccess::GetDefault();
  }
  int FSAAQuality()
  {
    return FSAAQualityAccess::GetDefault();
  }
  int VSync()
  {
    return VSyncAccess::GetDefault();
  }
  int IGUIScale()
  {
    return IGUISizeAccess::GetDefault();
  }
}

//! Video options display
class DisplayOptionsVideo : public Display
{
protected:
  AspectSettings _oldAspect;
  float _oldIGUIScale;

#if _HELISIM_VIEW_DIST
  float _oldViewDistance;
  float _oldViewObjectDistance;
#endif

public:
  //@{ VideoTextures submenu
  ListBoxOption<TextureQualityAccess> _textureQuality;
  ListBoxOption<TextureMemoryAccess> _textureMemory;  
  ListBoxOption<AFQualityAccess> _afQuality;          
  //@}
  //@{ DisplayOptionsVideoRendering submenu
  ListBoxOption<FSAAQualityAccess> _fsAAQuality;   //IDC_FSAA_DETAIL
  ListBoxOption<AToCAccess> _atoc;                 //IDC_ATOC_DETAIL
  ListBoxOption<PPAAQualityAccess> _ppaa;                 //IDC_OPTIONS_FXAA_VALUE
  ListBoxOption<HDRQualityAccess> _hdrQuality;     //IDC_HDR_DETAIL
  ListBoxOption<PostprocessEffectsAccess> _postprocessEffects;  //IDC_POSTPROCESS_EFFECTS
  ListBoxOption<AmbOcclusionAccess> _ambOcclusion; //Ambient Occlusion
  SliderOption<BloomAccess> _bloom;
  SliderOption<RotationBlurAccess> _rotBlur;
  //@}
  //@{ DisplayOptionsVideoUserInterface submenu
  ListBoxOption<AspectRatioAccess> _aspect;
  ListBoxOption<IGUISizeAccess> _iguiScale;
  ListBoxOption<ResolutionAccess> _resolutions;
  //@}
  //@{ DisplayOptionsVideoQuality submenu
  ListBoxOption<ComplexityAccess> _complexity;        //IDC_OBJECTS_DETAIL
  ListBoxOption<TerrainAccess> _terrain;              //IDC_OPTIONS_TERRAIN
  ListBoxOption<SimulWeatherQualityAccess> _simulWeatherQuality;  //IDC_OPTIONS_SW_VALUE
  ListBoxOption<ShadowQualityAccess> _shadowQuality;  //IDC_SHADOW_DETAIL
  //IDC_PARTICLES_EFFECTS
  //@}
  ListBoxOption<ShadingQualityAccess> _shadingQuality;
  ListBoxOption<RefreshRateAccess> _refreshRates;
  ListBoxOption<PresetAccess> _preset;
  ListBoxOption<VSyncAccess> _vsync;

  ListBoxOption<FillrateControlAccess> _fillrateControl;

  SliderOption<GammaAccess> _gamma;
  SliderOption<BrightnessAccess> _brightness;
  SliderOption<VisibilityAccess> _visibility;

  ListBoxOption<PIPQualityAccess> _PIPQuality;
  ListBoxOption<PPAAQualityAccess> _fxaaQuality;
  SliderOption<ShadowVisibilityAccess> _shadowVisibility;

#if _HELISIM_VIEW_DIST
  SliderOption<ObjectVisibilityAccess> _objectVisibility;
#endif
#if _HELISIM_AMBIENT_TRAFFIC
  SliderOption<TrafficVisibilityAccess> _trafficVisibility;
#endif
#if _VBS3
  SliderOption<DrawDistanceAccess> _drawDistance;
#endif

  ButtonOption<WBufferAccess> _wBuffer;

  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_complexity);
    op(_textureQuality);
    op(_textureMemory);
    op(_shadingQuality);
    op(_shadowQuality);
    op(_terrain);
    op(_resolutions);
    op(_refreshRates);
    op(_hdrQuality);
    op(_fsAAQuality);
    op(_afQuality);
    op(_postprocessEffects);
    op(_aspect);
    op(_iguiScale);
    op(_fillrateControl);

    op(_gamma);
    op(_brightness);
    op(_visibility);
    op(_vsync);

    op(_atoc);
    op(_ppaa);

    op(_simulWeatherQuality);
    op(_PIPQuality);
    op(_fxaaQuality);
#if _HELISIM_VIEW_DIST
    op(_objectVisibility);
#endif
#if _HELISIM_AMBIENT_TRAFFIC
    op(_trafficVisibility);
#endif
    op(_shadowVisibility);
#if _VBS3
    op(_drawDistance);
#endif
    op(_wBuffer);
    op(_ambOcclusion);
    op(_rotBlur);
    op(_bloom);
  }
  
  template <class Op>
  void ForPresetControl(const Op &op)
  {
    op(_preset);
  }
  
  void Init() {ForEachControl(InitOp());}
  void Cancel() {ForEachControl(CancelOp());}
  /// we want "performance level" to be updated on almost any change
  void RefreshPreset()
  {
    RefreshOp(this)(_preset);
#if _VBS3
    RefreshOp(this)(_drawDistance);
#endif
  }
  void Refresh()
  {
    ForEachControl(RefreshOp(this));
    // let preset refresh as well
    RefreshPreset();
  }

  void SetDefaultSettingsLevel(int level);
  
  //! constructor
  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayOptionsVideo(ControlsContainer *parent, bool enableSimulation);
  ~DisplayOptionsVideo();
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnLBSelChanged(IControl *ctrl, int curSel);

  static void ResetAspect(AspectSettings asp); //called from submenu display (DisplayOptionsVideoUserInterface)

  void OnChildDestroyed(int idd, int exit);
  void OnDraw(EntityAI *vehicle, float alpha);
  void OnSimulate(EntityAI *vehicle);
};

//! SUBMENU: Video options Textures display
class DisplayOptionsVideoTextures : public Display
{
public:
  ListBoxOption<TextureQualityAccess> &_textureQuality;
  ListBoxOption<TextureMemoryAccess> &_textureMemory;
  ListBoxOption<AFQualityAccess> &_afQuality;

  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_textureQuality);
    op(_textureMemory);
    op(_afQuality);
  }
 
  void Init() {ForEachControl(InitOp());}
  void Refresh()
  {
    ForEachControl(RefreshOp(this));
  }

  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayOptionsVideoTextures(DisplayOptionsVideo *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
};

//! SUBMENU: Video options Rendering display
class DisplayOptionsVideoRendering : public Display
{
public:
  ListBoxOption<FSAAQualityAccess> &_fsAAQuality;   //Antialiasing
  ListBoxOption<AToCAccess> &_atoc;
  ListBoxOption<PPAAQualityAccess> &_ppaa;                 //Edge smoothing
  ListBoxOption<HDRQualityAccess> &_hdrQuality;     //HDR Detail
  ListBoxOption<AmbOcclusionAccess> &_ambOcclusion; //Ambient Occlusion
  ListBoxOption<PostprocessEffectsAccess> &_postprocessEffects;  //Postprocess Quality
  SliderOption<BloomAccess> &_bloom;
  SliderOption<RotationBlurAccess> &_rotBlur;
  
  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_fsAAQuality);
    op(_atoc);
    op(_ppaa);
    op(_hdrQuality);
    op(_ambOcclusion);
    op(_postprocessEffects);
    op(_bloom);
    op(_rotBlur);
  }
 
  void Init() {ForEachControl(InitOp());}
  void Refresh()
  {
    ForEachControl(RefreshOp(this));
  }

  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayOptionsVideoRendering(DisplayOptionsVideo *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  void OnSliderPosChanged(IControl *ctrl, float pos);
};

//! SUBMENU: Video options UserInterface display
class DisplayOptionsVideoUserInterface : public Display
{
public:
  ListBoxOption<AspectRatioAccess> &_aspect;
  ListBoxOption<IGUISizeAccess> &_iguiScale;
  ListBoxOption<ResolutionAccess> &_resolutions;

  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_aspect);
    op(_iguiScale);
    op(_resolutions);
  }
 
  void Init() {ForEachControl(InitOp());}
  void Refresh()
  {
    ForEachControl(RefreshOp(this));
  }

  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayOptionsVideoUserInterface(DisplayOptionsVideo *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
};

//! SUBMENU: Video options Quality display
class DisplayOptionsVideoQuality : public Display
{
public:
  ListBoxOption<ComplexityAccess> &_complexity;
  ListBoxOption<TerrainAccess> &_terrain;
  ListBoxOption<SimulWeatherQualityAccess> &_simulWeatherQuality;
  ListBoxOption<ShadowQualityAccess> &_shadowQuality;

  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_complexity);
    op(_terrain);
    op(_simulWeatherQuality);
    op(_shadowQuality);
  }
 
  void Init() {ForEachControl(InitOp());}
  void Refresh()
  {
    ForEachControl(RefreshOp(this));
  }

  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayOptionsVideoQuality(DisplayOptionsVideo *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
};

/// access for voices volume control 
struct VoicesAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GSoundsys ? GSoundsys->GetDefaultSpeechVolume() : 0;}
  /// get actual value
  static Type Get() {return GSoundsys ? GSoundsys->GetSpeechVolume() : 0;}
  /// set during value changing
  static void Set(Type val)
  {
    if (GSoundsys)
    {
      GSoundsys->SetSpeechVolume(val);
      GSoundsys->StartPreview();
    }
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_VOICES_SLIDER;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 10;}
  /// text of preview
  static RString GetText() {return Format("%.1f", Get());}
};

/// access for VoN microphone sensitivity
struct VONMicSensitivityAccess
{
  /// type of the value controlled
  typedef float Type;

  /// get default value
  static Type GetDefault() {return GSoundsys ? RecThresholdToSliderVal(GSoundsys->GetDefaultVoNRecThreshold()) : 0;}
  /// get actual value
  static Type Get() {return GSoundsys ? RecThresholdToSliderVal(GSoundsys->GetVoNRecThreshold()) : 0;}
  /// set during value changing
  static void Set(Type val)
  {
    if (GSoundsys)
    {
      GSoundsys->SetVoNRecThreshold(SliderValToRecThreshold(val));
    }
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val){}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_MIC_SENS_SLIDER;}

  /// get minimal value
  static Type GetMin() {return 0;}
  /// get maximal value
  static Type GetMax() {return 10;}
  /// text of preview
  static RString GetText() {return Format("%.1f", Get());}

  /// recomputes slider value to recording threshold
  static float SliderValToRecThreshold(Type sliderVal)
  {
    float size = GetMax() - GetMin();
    if (size <= 0)
      return 0.0f;

    float tmp = (sliderVal-GetMin())/size;
    
    // idea behind this: microphone sensitivity is opposite value to recording (silence) threshold
    // and we use quadratic value so that we can use space better on the slider
    return (1.0f - tmp)*(1.0f - tmp);
  }
  /// recomputes recording threshold to slider value
  static Type RecThresholdToSliderVal(float recThreshold)
  {
    saturate(recThreshold, 0.0f, 1.0f);

    // this is just the inverse function of SliderValToRecThreshold
    float size = GetMax() - GetMin();
    return (1.0f - sqrtf(recThreshold))*size + GetMin();
  }
};

static const int IntSamples[]      = { 16, 24, 32, 48, 64, 80, 96, 112, 128 };

/// access for Hardware acceleration control 
struct SamplesAccess: public ValueAccess
{
  /// type of the value controlled
  typedef int Type;


  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}

  static void Switch()
  {
    Type newIndex = (Get()+1)%GetNValues();
    Set(newIndex);
  }

  /// get default value
  static Type GetDefault() {return GetIndex(GSoundsys->GetSamplesCountDefault());}
  /// get actual value
  static Type Get() {return GetIndex(GSoundsys->GetSamplesCount());}
  /// set during value changing
  static void Set(Type val) 
  { 
    if (val >= 0 && val < GetNValues()) 
      GSoundsys->SetSamplesCount(IntSamples[val]); 
    else
      GSoundsys->SetSamplesCount(GSoundsys->GetSamplesCountDefault()); 
  }
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_SAMPLES_SLIDER;}
  /// text of preview

  static RString GetText() 
  {
    int index = Get();
    if (index < 0 && index >= GetNValues())
    {
      Fail("Unsupported max samples count level");
      index = 0;
    }
    return Format(LocalizeString(IDS_SOUND_SOURCES_COUNT), IntSamples[index]);
  }

  /// get number of possible selections
  static int GetNValues() {return lenof(IntSamples);}

  static int GetIndex(Type val)
  {    
    for (int i = 0; i < GetNValues(); i++)
      if (IntSamples[i] == val) return i;

    return 0;
  }
};

//stringtable IDs for true/false listBox 
static const int *EnableDisableTable[]={
    &IDS_ENABLED,  
    &IDS_DISABLED, 
};

/// access for Hardware acceleration control 
struct HWAccAccess: public ListBoxAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get number of possible selections
  static int GetNValues() {return 2;}   //enabled and disabled
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return EnableDisableTable;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return (index==0) ? true : false;}
  /// check if value can be used
  static bool IsValid(Type value) {return  value ? (GSoundsys ? (GSoundsys->EnableHWAccel(value)):(false)):(true);}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}

  /// get default value
  static Type GetDefault() {return GSoundsys ? GSoundsys->GetDefaultHWAccel() : false;}
  /// get actual value
  static Type Get() {return GSoundsys ? GSoundsys->GetHWAccel() : 0;}
  /// set during value changing
  static void Set(Type val) {if (GSoundsys) GSoundsys->EnableHWAccel(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_HWACC;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}
 
};

/// access for EAX control 
struct EAXAccess: public ListBoxAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get number of possible selections
  static int GetNValues() {return 2;}
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return EnableDisableTable;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return (index==0) ? true : false;}
  /// check if value can be used
  static bool IsValid(Type value) {return value ? (GSoundsys ? (GSoundsys->EnableEAX(value)):(false)):(true);}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}

  /// get default value
  static Type GetDefault() {return GSoundsys ? GSoundsys->GetDefaultEAX() : false;}
  /// get actual value
  static Type Get() {return GSoundsys ? GSoundsys->GetEAX() : false;}
  /// set during value changing
  static void Set(Type val) {if (GSoundsys) GSoundsys->EnableEAX(val);}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_EAX;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}
};

//stringtable IDs for true/false listBox 
static const int *SingleALLVoiceTable[]={
    &IDS_SINGLE_VOICE,  
    &IDS_ALL_VOICES, 
};

/// access for Limited number of voices control 
struct SingleVoiceAccess: public ListBoxAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get number of possible selections
  static int GetNValues() {return 2;}
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return SingleALLVoiceTable;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return (index==0) ? true : false;}
  /// check if value can be used
  static bool IsValid(Type value) {return true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}

  /// get default value
  #if _FORCE_SINGLE_VOICE
  static Type GetDefault() {return true;} // TODO: default
  #else
  static Type GetDefault() {return false;} // TODO: default
  #endif
  /// get actual value
  static Type Get() {return Glob.config.singleVoice;}
  /// set during value changing
  static void Set(Type val) {Glob.config.singleVoice = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_SINGLE_VOICE;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable()
  {
#if _FORCE_SINGLE_VOICE
    return false;
#else
    return true;
#endif
  }
  
};

//! Audio options display
class DisplayOptionsAudio : public Display
{
public:
  SliderOption<MusicAccess> _music;
  SliderOption<EffectsAccess> _effects;
  SliderOption<VoicesAccess> _voices;
  SliderOption<VONAccess> _von;
  SliderOption<VONMicSensitivityAccess> _vonRecThr;

  ButtonOption<SamplesAccess> _samples;
  ListBoxOption<HWAccAccess> _hwAcc;
  ListBoxOption<EAXAccess> _eax;
  ListBoxOption<SingleVoiceAccess> _singleVoice;


  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_music);
    op(_effects);
    op(_voices);
    op(_von);
    op(_vonRecThr);
    op(_samples);

    op(_hwAcc);
    op(_eax);
    op(_singleVoice);
  }

  void Init()
  {
    ForEachControl(InitOp());
  }
  void Cancel()
  {
    ForEachControl(CancelOp());
  }
  void SetDefault()
  {
    ForEachControl(DefaultOp(this));
    CheckPendingRestart();
  }

  void CheckPendingRestart();

  //! constructor
  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayOptionsAudio(ControlsContainer *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnChildDestroyed(int idd, int exit);
  void OnLBSelChanged(IControl *ctrl, int curSel);
};

class DisplayOptionsMicAutoAdj : public Display
{
  // capturing progress time
  UITime _time;
  Ref<CProgressBar> _progress;

public:
  DisplayOptionsMicAutoAdj(ControlsContainer *parent, bool enableSimulation);

  template <class Op>
  void ForEachControl(const Op &op)
  {
  }

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void Init() { ForEachControl(InitOp()); }
  void Cancel() 
  { 
    GSoundsys->StopCapture();
    ForEachControl(CancelOp()); 
  }
  void OnButtonClicked(int idc);
  void OnSimulate(EntityAI *vehicle);
};

//! Difficulties selection control
class CDifficulties
{
public:
  //! current settings for current mode
  bool currentDifficulty[DTN];
  //! settings for current mode enabled
  bool enabledInCurrent[DTN];

protected:
  //! selected column
  int _column;

public:
  //! constructor
  CDifficulties();
  //! virtual destructor
  virtual ~CDifficulties() {}

protected:
  //! toggle current value (enabled / disabled)
  /*!
    \param column cadet / veteran settings
    \param index index of settings
  */
  void Toggle(int column, int index)
  {
    if (enabledInCurrent[index])
    {
      currentDifficulty[index] = !currentDifficulty[index];
    }
  }
};

class C2DDifficulties : public CListBox, public CDifficulties
{
public:
  //! constructor
  /*!
  Used when control is created by resource template.
  \param parent control container by which this control is owned
  \param idc id of control
  \param cls resource template
  */
  C2DDifficulties(ControlsContainer *parent, int idc, ParamEntryPar cls);

  bool OnKeyDown(int dikCode);
  void OnLButtonDown(float x, float y);

  virtual void DrawItem
  (
    UIViewport *vp, float alpha, int i, bool selected, float top,
    const Rect2DFloat &rect, float widthDiff
  ) override; 
};

/// access for languages control 
struct LanguagesAccess: public ValueAccess
{
  static AutoArray<RString> _languages;
  static bool _initialized;
  /// type of the value controlled
  typedef RString Type;

  /// get default value
  static Type GetDefault() {return GLanguage;}
  /// get actual value
  static Type Get() { return GLanguage; }
  /// set during value changing
  static void Set(Type val) {GLanguage = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_LANGUAGE;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}

  static void Switch() 
  { 
    if (_languages.Size()) 
    {
      for (int i=0; i<_languages.Size(); i++)
      {
        if (stricmp(_languages[i], GLanguage) == 0) 
        {
          int sel = (i+1)%_languages.Size();
          Set(_languages[sel]); 
          return;
        }
      }
    }
  }

  static RString GetText() 
  {
    if (!_initialized)
    {
#ifndef _XBOX
      _initialized = true;
      ConstParamEntryPtr cfg = Pars.FindEntry("CfgLanguages");
      if (cfg)
      {
        for (int i=0; i<cfg->GetEntryCount(); i++)
        {
          ParamEntryVal cls = cfg->GetEntry(i);
          if (!cls.IsClass()) continue;
          RString language = cls.GetName();
          if (stricmp(language, CheckLanguage(language)) != 0) continue; // language not supported
//          _languages.Add(cls >> "name");
		  _languages.Add(cls.GetName().Data());
        }
      }
    }
#endif
    return Get();
  }
};
AutoArray<RString> LanguagesAccess::_languages;
bool LanguagesAccess::_initialized = false;

/// access for subtitles control 
struct SubtitlesAccess: public ValueAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get default value
  static Type GetDefault() {return Glob.config.showTitles;}
  /// get actual value
  static Type Get() {return Glob.config.showTitles;}
  /// set during value changing
  static void Set(Type val) {Glob.config.showTitles = val;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_SUBTITLES;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}

  static void Switch() { Set(!Get()); }

  static RString GetText() 
  {
    int index = Get() ? 1:0;
    return Format("%s %s", cc_cast(LocalizeString(IDS_OPT_SUBTITLES)), cc_cast(LocalizeString(*EnableDisableTable[index])));
  }
};

/// access for radio subtitles control 
struct RadioAccess: public ListBoxAccess
{
  /// type of the value controlled
  typedef bool Type;

  /// get number of possible selections
  static int GetNValues() {return 2;}   //enabled and disabled
  /// get stringtable IDs for selections
  static const int **GetValueIds() {return EnableDisableTable;}
  /// get text of the given row
  static RString GetText(int index) {return LocalizeString(*GetValueIds()[index]);}
  /// get values for selections
  static Type GetValue(int index) {return (index==0) ? true : false;}
  /// check if value can be used
  static bool IsValid(Type value) {return  true;}
  /// distance of values
  static float Distance(Type value1, Type value2) {return fabs(value1 - value2);}

  /// get default value
  static Type GetDefault() {return GChatList.ProfileEnabled();}
  /// get actual value
  static Type Get() {return GChatList.ProfileEnabled();}
  /// set during value changing
  static void Set(Type val) {GChatList.Enable(val) ;}
  /// set once dialog is confirmed
  static void SetFinal(Type val) {}
  /// control ID
  static int GetIDC() {return IDC_OPTIONS_RADIO;}

  /// show the control
  static bool Show() {return true;}
  /// enable the control
  static bool Enable() {return true;}
};


//! Game options display
class DisplayGameOptions : public Display
{
public:

  //! original value of Show subtitles (enabled / disabled)
  bool _oldTitles;
  //! original value of Show Radio subtitles (enabled / disabled)
  bool _oldRadio;

  float _oldHeadBob;
  int _oldBlood;
  float _oldMouse;

  ButtonOption<LanguagesAccess> _languages;
  ButtonOption<SubtitlesAccess> _subtitles;
  ListBoxOption<RadioAccess> _radio;
  ListBoxOption<BloodAccess> _blood;

#if _HELISIM_IMPERIAL_SYSTEM
  ListBoxOption<ImperialUnitsAccess> _imperialUnits;
#endif
  ListBoxOption<VehicleFreeLooksAccess> _vehicleFreeLook;

  template <class Op>
  void ForEachControl(const Op &op)
  {
    op(_languages);
    op(_subtitles);
    op(_radio);
    op(_blood);
#if _HELISIM_IMPERIAL_SYSTEM
    op(_imperialUnits);
#endif
    op(_vehicleFreeLook);
  }

  void Init()
  {
    ForEachControl(InitOp());
  }
  void Cancel()
  {
    ForEachControl(CancelOp());
  }
  void SetDefault()
  {
    ForEachControl(DefaultOp(this));
  }

  //! constructor
  /*!
  \param parent parent display
  \param enableSimulation enable game simulation
  - true if called form main menu
  - false in case of in game invocation
  */
  DisplayGameOptions(ControlsContainer *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnChildDestroyed(int idd, int exit);
};

//! Difficulty display
class DisplayDifficulty : public Display
{
public:
  //! Difficulties selection control
  CDifficulties *_diff;

  //! original value of Show subtitles (enabled / disabled)
  bool _oldTitles;
  //! original value of Show Radio subtitles (enabled / disabled)
  bool _oldRadio;
  //! original value of AI Skills
  float _oldAISkill[2]; // index - friendly(0)/enemy(1)

  int _indexCurrent;

  //! constructor
  /*!
    \param parent parent display
    \param enableSimulation enable game simulation
    - true if called form main menu
    - false in case of in game invocation
  */
  DisplayDifficulty(ControlsContainer *parent, bool enableSimulation);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSliderPosChanged(IControl *ctrl, float pos);
  void OnLBSelChanged(IControl *ctrl, int curSel);

protected:
  void InitAILevel(float skill, int idcLevel, int idcSkill, int idcEcho);
  void UpdateSlider(IControl *ctrl, float skill);
  void UpdateText(IControl *ctrl, float skill);
  void UpdateAISkillTitles();

  void Refresh();
};

///////////////////////////////////////////////////////////////////////////////
// Implementation

// fast search for mission name without parsing
// suppose <searchStr> is found in first 4KB of file
RString FastSearch(QIStream &file, const char *searchStr, int searchLen)
{
  char searchIn[4*1024];
  int searchInLen = file.read(searchIn,sizeof(searchIn));
  const char *maxEnd = searchIn+searchInLen;

  for (int s=0; s<searchInLen; s++)
  {
    if (!strnicmp(searchIn+s,searchStr,searchLen))
    {
      // candidate for a match
      // scan for '=' sign
      const char *end = searchIn+s+searchLen;
      while (*end==' ' && end<maxEnd) end++;
      if (*end=='=')
      {
        end++;
        while (*end==' ' && end<maxEnd) end++;
        // should be followed with '"'
        if (*end=='"')
        {
          end++;
          // scan name
          char buf[1024];
          int maxC = 0;;
          while (*end!='"' && end<maxEnd && maxC<sizeof(buf)-1)
          {
            buf[maxC++]=*end++;
          }
          buf[maxC]=0;
          return buf;
        }
      }
      return RString();
    }
  }
  return RString();
}

LocalizedString ClientSideLocalizedString(RString value, RString mission);

static LocalizedString FindLocalizedBriefingName(bool directory, RString path)
{
  const char *searchStr = "briefingName";
  int searchLen = strlen(searchStr);

  const char *begin = strrchr(path, '\\');
  LocalizedString result(LocalizedString::PlainText, begin ? RString(begin + 1) : path);

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  if (directory)
  {
    // load stringtable
    LoadStringtable("Temporary", path + RString("\\stringtable.csv"), 10, true);

    // search for display name
    RString mission = path + RString("\\mission.sqm");
    ParamFile f;
    if (f.ParseBin(mission, NULL, NULL, &globals))
    {
      ParamEntryVal entry = f >> "Mission" >> "Intel";
      if (entry.FindEntry("briefingName"))
        result = ClientSideLocalizedString(entry >> "briefingName",path);
    }
    else
    {
      QIFStreamB file;
      file.AutoOpen(mission);
      RString found = FastSearch(file, searchStr, searchLen);
      if (found.GetLength() > 0)
        result = ClientSideLocalizedString(found,path);
    }

    UnloadStringtable("Temporary");
    return result;
  }
  else
  {
    // create bank (temporary)
    QFBank bank;
    bank.open(path);

    // load stringtable
    LoadStringtable("Temporary", bank, "stringtable.csv", 10, true);

    // search for display name
    ParamFile f;
    if (f.ParseBin(bank, "mission.sqm", NULL, &globals))
    {
      ParamEntryVal entry = f >> "Mission" >> "Intel";
      if (entry.FindEntry("briefingName"))
        result = ClientSideLocalizedString(entry >> "briefingName",path);
    }
    else
    {
      QIFStreamB file;
      file.open(bank, "mission.sqm");
      RString found = FastSearch(file, searchStr, searchLen);
      if (found.GetLength() > 0)
        result = ClientSideLocalizedString(found,path);
    }

    UnloadStringtable("Temporary");
  }

  return result;
}

LocalizedString FindLocalizedBriefingName(RString path)
{
  bool dir = true;
  if (QIFileFunctions::FileExists(path + RString(".pbo")))
  {
    // bank
    dir = false;
  }
  else
  {
    // directory
    Assert(QIFileFunctions::DirectoryExists(path));
  }
  return FindLocalizedBriefingName(dir, path);
}

static RString FindBriefingName(bool directory, RString path)
{
  LocalizedString name = FindLocalizedBriefingName(directory,path);
  return name.GetLocalizedValue();
}

RString FindBriefingName(RString path)
{
  LocalizedString name = FindLocalizedBriefingName(path);
  return name.GetLocalizedValue();
}

// return user account name (gamertag)
RString ApplyUserProfile()
{
  // initialization
  Glob.header.playerFace = "Default";
  Glob.header.playerGlasses = "None";
  Glob.header.playerSpeakerType = GetFirstSpeakerType();
  Glob.header.playerPitch = PITCH_DEFAULT;
  Glob.header.voiceMask = (Pars >> "CfgVoiceMask").GetEntry(0).GetName();

  RString account;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    ConstParamEntryPtr identity = cfg.FindEntry("Identity");
    if (identity)
    {
      ConstParamEntryPtr entry = identity->FindEntry("face");
      if (entry) Glob.header.playerFace = *entry;
      entry = identity->FindEntry("glasses");
      if (entry) Glob.header.playerGlasses = *entry;
      entry = identity->FindEntry("speaker");
      if (entry) Glob.header.playerSpeakerType = *entry;
      entry = identity->FindEntry("pitch");
      if (entry) Glob.header.playerPitch = *entry;
      /* in UpdateUserProfile
      entry = identity->FindEntry("mask");
      if (entry) Glob.header.voiceMask = *entry;
      */
    }
    ConstParamEntryPtr entry = cfg.FindEntry("userAccount");
    if (entry) account = *entry;

    UpdateUserProfile();
  }
  else
  {
#if defined _XBOX && _XBOX_VER >= 200
    if (GSaveSystem.IsUserSignedIn())
    {
      RptF("User profile for %s was not parsed!", cc_cast(Glob.header.GetPlayerName()));
    }
#else
    RptF("User profile was not parsed! File name '%s'.", (const char *)GetUserParams(Glob.header.GetPlayerName(), false));
#endif

#if defined _XBOX && _XBOX_VER >= 200
    // create a dummy user config to avoid further errors
    ParamFile empty;
    SaveUserParams(empty);
#endif
    // use default values
    UpdateUserProfile();
  }
  return account;
}


// Signing in is handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200 && _ENABLE_MP
void SetSignInStatus(CStructuredText *ctrl)
{
  RString text;
  HRESULT hr = GetNetworkManager().SilentSignInResult();
  if (hr == S_FALSE || hr == XONLINETASK_S_RUNNING)
    text = LocalizeString(IDS_SILENT_SIGN_IN_NONE);
  else if (SUCCEEDED(hr))
  {
    XONLINE_USER *user = XOnlineGetLogonUsers();
    Assert(user);
    if (user)
      text = Format(LocalizeString(IDS_SILENT_SIGN_IN_OK), user->szGamertag);
  }
  else if (hr == XONLINE_E_SILENT_LOGON_PASSCODE_REQUIRED)
    text = LocalizeString(IDS_SILENT_SIGN_IN_PASSCODE);
  else
    text = LocalizeString(IDS_SILENT_SIGN_IN_FAILED);
  ctrl->SetRawText(text);
}
#endif

/// sort save slots from the oldest
static int CmpSaveSlots(const int *index1, const int *index2, Future<QFileTime> *saveHandles)
{
#if !POSIX_FILES_COMMON
  QFileTime info1 = saveHandles[*index1].GetResult();
  QFileTime info2 = saveHandles[*index2].GetResult();
  if (info2>info1)
    return -1;
  if (info1<info2)
    return 1;

  return *index2 < *index1 ? 1 : -1;
#else
  return 0;
#endif
}

#ifdef _WIN32
RString FormatFileTime(QFileTime time, RString format)
{
  FILETIME info = ConvertToFILETIME(time);

  FILETIME infoLocal;
  ::FileTimeToLocalFileTime(&info, &infoLocal);

  SYSTEMTIME stime;
  ::FileTimeToSystemTime(&infoLocal, &stime);

  int day = stime.wDay;
  int month = stime.wMonth;
  int year = stime.wYear;
  int minute = stime.wMinute;
  int hour = stime.wHour;
  return Format(format, month, day, year, hour, minute);
}
#endif

// Interrupt revert display

DisplayInterruptRevert::DisplayInterruptRevert(ControlsContainer *parent, Future<QFileTime> saveHandles[NSaveGameType])
: Display(parent)
{
#ifdef _WIN32
  _enableSimulation = false;
  _enableUI = false;

  Load("RscDisplayInterruptRevert");

  if (_revertTypes)
  {
    // sort saves by the date
    AutoArray< int, MemAllocLocal<int, 16> > indices;
    for (int i=0; i<NSaveGameType; i++)
    {
      if (saveHandles[i].GetResult()!=0) indices.Add(i);
    }
    QSort(indices.Data(), indices.Size(), saveHandles, CmpSaveSlots);

    // saves - revert the order (the newest at top)
    for (int i=indices.Size()-1; i>=0; i--)
    {
      int handle = indices[i];
      RString format = LocalizeString(handle == SGTAutosave ? IDS_DISP_INT_RETRY_FORMAT : IDS_DISP_INT_LOAD_FORMAT);
      int index = _revertTypes->AddString(FormatFileTime(saveHandles[handle].GetResult(), format));
      _revertTypes->SetValue(index, handle);
    }

    // restart
    const DifficultySettings *difficulty = Glob.config.GetDifficultySettings();
    int index = _revertTypes->AddString(Format(LocalizeString(IDS_DISP_INT_RESTART_FORMAT), difficulty ? cc_cast(difficulty->displayName) : ""));
    _revertTypes->SetValue(index, -1);
    
    _revertTypes->SetCurSel(0);
  }
#endif
}

SaveGameType DisplayInterruptRevert::GetSaveSlot() const
{
  if (!_revertTypes) return (SaveGameType)-1;
  int sel = _revertTypes->GetCurSel();
  if (sel < 0) return (SaveGameType)-1;
  return (SaveGameType)_revertTypes->GetValue(sel);
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
void DisplayInterruptRevert::OnStorageDeviceChanged()
{
  // exit to DisplayInterrupt menu, the storage change will be handled there
  Exit(IDC_CANCEL);
}

void DisplayInterruptRevert::OnStorageDeviceRemoved()
{
  // we dont want to do anything here, warning dialogue will be displayed in parent display
}

#endif

Control *DisplayInterruptRevert::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_INT_REVERT_TYPE:
    _revertTypes = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayInterruptRevert::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (_revertTypes)
    {
      int sel = _revertTypes->GetCurSel();
      if (sel >= 0)
      {
        int saveType = _revertTypes->GetValue(sel);
        if (saveType == -1) Exit(IDC_INT_RESTART);
        else if (saveType == SGTAutosave) 
        {
          Exit(GWorld->GetMode()==GModeNetware ? IDC_INT_LOAD : IDC_INT_RETRY);
        }
        else Exit(IDC_INT_LOAD);
      }
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayInterruptRevert::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_INT_REVERT_TYPE) OnButtonClicked(IDC_OK);
  else Display::OnLBDblClick(idc, curSel);
}

// DisplaySelectUserSave display
DisplaySelectUserSave::DisplaySelectUserSave(ControlsContainer *parent, Future<QFileTime> saveHandles[NSaveGameType])
: Display(parent)
{
#ifdef _WIN32
  _enableSimulation = false;
  _enableUI = false;

  Load("RscDisplaySelectSave");

  if (_saveTypes)
  {
    // sort saves by the date
    AutoArray< int, MemAllocLocal<int, 16> > indices;
    for (int i=0; i<NSaveGameType; i++)
    {
      if (saveHandles[i].GetResult()!=0) indices.Add(i);
    }
    QSort(indices.Data(), indices.Size(), saveHandles, CmpSaveSlots);

    for (int i=0; i<indices.Size(); i++)
    {
      int handle = indices[i];
      int index = _saveTypes->AddString(FormatFileTime(saveHandles[handle].GetResult(), LocalizeString(IDS_DISP_INT_LOAD_FORMAT)));
      _saveTypes->SetValue(index, handle);
    }
    _saveTypes->SetCurSel(0, false);
  }
#endif
}

SaveGameType DisplaySelectUserSave::GetSaveSlot() const
{
  if (!_saveTypes) return (SaveGameType)-1;
  int sel = _saveTypes->GetCurSel();
  if (sel < 0) return (SaveGameType)-1;
  return (SaveGameType)_saveTypes->GetValue(sel);
}

Control *DisplaySelectUserSave::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_SELECT_SAVE_SLOTS:
    _saveTypes = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplaySelectUserSave::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_SELECT_SAVE_SLOTS) OnButtonClicked(IDC_OK);
  else Display::OnLBDblClick(idc, curSel);
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
void DisplaySelectUserSave::OnStorageDeviceChanged()
{
  // exit to DisplayInterrupt menu, the storage change will be handled there
  Exit(IDC_CANCEL);
}

void DisplaySelectUserSave::OnStorageDeviceRemoved()
{
  // we dont want to do anything here, warning dialogue will be displayed in parent display
}
#endif

// Interrupt display

DisplayInterrupt::DisplayInterrupt(ControlsContainer *parent, Future<QFileTime> saveTimeHandles[NSaveGameType])
  : Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  Init();
  UpdateSaveControls(saveTimeHandles);
  UpdateButtons();
}

DisplayInterrupt::DisplayInterrupt(ControlsContainer *parent)
: Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  Init();
  UpdateButtons();
}

void DisplayInterrupt::Init()
{
  _enableSimulation = false;
  _enableUI = false;

  _request = false;
  _invitation = false;
  _skipSimulation = true;

  _selectedSlot = (SaveGameType)-1;

  for (int i=0; i<NSaveGameType; i++) _saveHandles[i] = Future<QFileTime>(0);

  if (GWorld->GetMode() == GModeNetware)
  {
    ConstParamEntryPtr cls = Pars.FindEntry("RscDisplayMPInterrupt");
    if (cls) LoadFromEntry(*cls);
    else Load("RscDisplayInterrupt");

    IControl *ctrl = GetCtrl(IDC_INT_MISSION);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text)
    {
      const MissionHeader *header = GetNetworkManager().GetMissionHeader();
      text->SetText(header ? header->GetLocalizedMissionName() : "");
    }

	//disable repsawn button for players that are alive
    if (GWorld && GWorld->PlayerOn() && GWorld->PlayerOn()->Brain() && 
        (GWorld->PlayerOn()->Brain()->GetLifeState() == LifeStateAlive) && GetCtrl(IDC_INT_RESPAWN))
      GetCtrl(IDC_INT_RESPAWN)->EnableCtrl(false);
	else if (!GWorld->PlayerOn())
	  //no player object therefore cannot respawn
	  GetCtrl(IDC_INT_RESPAWN)->EnableCtrl(false);
  }
  else
  {
    Load("RscDisplayInterrupt");
  }

#if _VBS2 && !_VBS2_LITE // command mode
  {
    IControl *ctrl = ctrl = GetCtrl(IDC_INT_EDIT_REALTIME);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text && !Glob.vbsAdminMode)
      text->SetText(LocalizeString("STR_DISP_INT_COMMAND"));
  }
#endif

  if (!GetNetworkManager().IsSignedIn())
  {
    if (GetCtrl(IDC_INT_FRIENDS)) GetCtrl(IDC_INT_FRIENDS)->EnableCtrl(false);
    // if (GetCtrl(IDC_INT_PLAYERS)) GetCtrl(IDC_INT_PLAYERS)->EnableCtrl(false);
    if (GetCtrl(IDC_INT_APPEAR_OFFLINE)) GetCtrl(IDC_INT_APPEAR_OFFLINE)->EnableCtrl(false);
  }

  if (GetCtrl(IDC_INT_ABORT) && CanSaveContinue())
  {
    CStructuredText *text = GetStructuredText(GetCtrl(IDC_INT_ABORT));
    if (text) text->SetStructuredText(LocalizeString(IDS_SAVE_AND_ABORT));
  }
}

DisplayInterrupt::~DisplayInterrupt()
{
  GInput.ChangeGameFocus(-1);
}

SaveGameType DisplayInterrupt::GetFreeUserSlot() const
{
  for (int i=0; i<NSaveGameType; i++)
  {
    // check only user save slots
    if (i == SGTAutosave || i == SGTContinue) continue;
    if (_saveHandles[i].GetResult()==0) return (SaveGameType)i;
  }
  return (SaveGameType)-1;
}

void DisplayInterrupt::UpdateSaveControls(Future<QFileTime> saveTimeHandles[NSaveGameType])
{
#ifdef _WIN32
  if (GWorld->GetMode() == GModeNetware)
  {
    // restart button
    IControl *ctrl = GetCtrl(IDC_INT_REVERT);
    if (ctrl)
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text)
      {
        text->SetStructuredText(LocalizeString(IDS_SINGLE_RESTART));
      }
    }
    return;
  }

  // check if some user save exist
  bool isUsersave = false;

  for (int i=0; i<NSaveGameType; i++)
  {
    _saveHandles[i] = Future<QFileTime>(0);
    if (saveTimeHandles[i].GetResult()!=0)
    {
      _saveHandles[i] = saveTimeHandles[i];
      if (i != SGTContinue && i != SGTAutosave) isUsersave = true;
    }
  }

  // save button
  IControl *ctrl = GetCtrl(IDC_INT_SAVE);
  if (ctrl)
  {
    bool CanSaveGame();
    bool enable = CanSaveGame() && (!isUsersave || Glob.config.IsEnabled(DTUnlimitedSaves));
    ctrl->EnableCtrl(enable);
    }

  // revert button
  ctrl = GetCtrl(IDC_INT_REVERT);
  if (ctrl)
  {
    CStructuredText *text = GetStructuredText(ctrl);
    if (text)
    {
      if (!isUsersave && _saveHandles[SGTAutosave].GetResult()==0)
      {
        text->SetStructuredText(LocalizeString(IDS_SINGLE_RESTART));
      }
      else
      {
        text->SetStructuredText(LocalizeString(IDS_DISP_REVERT));
      }
    }
  }
#endif
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
void DisplayInterrupt::OnStorageDeviceChanged()
{
  Future<QFileTime> saveTimeHandles[NSaveGameType];
  void UpdateSaveTimeHandles(Future<QFileTime> saveTimeHandles[NSaveGameType]);
  UpdateSaveTimeHandles(saveTimeHandles);
  UpdateSaveControls(saveTimeHandles);
  GSaveSystem.StorageChangeHandled();
}
#endif

Control *DisplayInterrupt::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_INT_TITLE:
    {
      CStatic *text = new CStatic(this, idc, cls);
      if (GWorld->GetMode() == GModeNetware)
        text->SetText(LocalizeString(IDS_DISP_INT_TITLE_MP));
      else
        text->SetText(LocalizeString(IDS_DISP_INT_TITLE));
      return text;
    }
  case IDC_INT_FRIENDS:
    {
      Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
      _friends = GetStructuredText(ctrl);
      return ctrl;
    }
  case IDC_INT_SIGN_IN_STATUS:
    {
      Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
      _signInStatus = GetStructuredText(ctrl);
      // Sign in is handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200 &&_ENABLE_MP
      if (_signInStatus) SetSignInStatus(_signInStatus);
#endif
      return ctrl;
    }

  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

bool AutoCancelInterrupt()
{
  NetworkClientState state = GetNetworkManager().GetClientState();
  return state != NCSPlaying;
}

void DisplayInterrupt::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_INT_OPTIONS:
#ifdef _XBOX
      CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
#else
      bool IsXboxUI();
      if (IsXboxUI())
        CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
      else
        CreateChild(new DisplayOptions(this, false, false));
#endif
      break;
    case IDC_INT_SETTINGS:
      if (CheckDiskSpace(this, "U:\\", CHECK_SPACE_PROFILE))
        CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
      break;
    case IDC_INT_PLAYERS:
#if _GAMES_FOR_WINDOWS
      {
        int userIndex = GSaveSystem.GetUserIndex();
        if (userIndex >= 0) XShowPlayersUI(userIndex);
      }
#else
      CreateChild(new DisplayMPPlayers(this));
#endif
      break;
    case IDC_INT_FRIENDS:
#if _XBOX_SECURE && _ENABLE_MP
# if _XBOX_VER >= 200
      {
        int userIndex = GSaveSystem.GetUserIndex();
        if (userIndex >= 0) XShowFriendsUI(userIndex);
      }
# else
      CreateChild(new DisplayFriends(this, true));
# endif
#endif
      break;
    case IDC_INT_APPEAR_OFFLINE:
#if _XBOX_SECURE
      {
        bool cloak = !GetNetworkManager().IsCloaked();
        GetNetworkManager().Cloak(cloak);
      }
      UpdateButtons();
#endif
      break;
	case IDC_INT_RESPAWN:
		{
			MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
			MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
			CreateMsgBox
			(
			  MB_BUTTON_OK | MB_BUTTON_CANCEL,
			  LocalizeString(IDS_MSG_CONFIRM_RESPAWN_CLIENT),
			  IDD_MSG_RESPAWN, false, &buttonOK, &buttonCancel
			);
		};
      break;
    case IDC_INT_ABORT:
      if (GetNetworkManager().IsServer())
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox
        (
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          LocalizeString(IDS_MSG_CONFIRM_RETURN_LOBBY),
          IDD_MSG_TERMINATE_SESSION, false, &buttonOK, &buttonCancel
        );
      }
      else if (GWorld->GetMode() == GModeNetware)
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox
        (
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          LocalizeString(IDS_MSG_CONFIRM_RETURN_LOBBY_CLIENT),
          IDD_MSG_TERMINATE_SESSION, false, &buttonOK, &buttonCancel
        );
      }
      else if (!CanSaveContinue())
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_ABORT_MISSION), IDD_MSG_TERMINATE_SESSION, true, &buttonOK, &buttonCancel);
      }
      else
        Exit(idc);
      break;
    case IDC_INT_REVERT:
      // ignore for the client
      if (GWorld->GetMode() != GModeNetware || GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
      {
        bool someSave = false;
        for (int i=0; i<NSaveGameType; i++)
        {
          if (_saveHandles[i].GetResult()!=0)
        {
            someSave = true;
      break;
      }
      }

        if (someSave)
      {
          // select how to revert
          CreateChild(new DisplayInterruptRevert(this, _saveHandles));
      }
      else
      {
          // restart the mission
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
          CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RESTART_MISSION, false, &buttonOK, &buttonCancel);
      }
      }
      break;
    case IDC_INT_SAVE:
      _selectedSlot = GetFreeUserSlot();
      if (_selectedSlot == -1)
      {
        // no free slot exist, let user decide which one to use
        CreateChild(new DisplaySelectUserSave(this, _saveHandles));
      }
      else Exit(idc);
      break;
#if _VBS2 && _ENABLE_EDITOR2
    case IDC_INT_EDIT_REALTIME:
      {
#if _VBS2_LITE
        // show map instead of RTE
        GWorld->ShowMap(true);
#else
#if _VBS3 // don't show You Are Dead if admin
        if (!Glob.vbsAdminMode)
        {
          if (GWorld->PlayerOn() && GWorld->PlayerOn()->IsDamageDestroyed())
          {
            Exit(idc);
            break;
          }
        }
#endif
        GWorld->SetUserDialog(CreateMissionEditorRealTime(NULL));
#endif
        Exit(idc);
      }
      break;
#endif
    default:
      Exit(idc);
      break;
  }
}

bool DisplayInterrupt::OnKeyDown(int dikCode)
{
  if (_skipSimulation) return false;
  return Display::OnKeyDown(dikCode);
}

bool DisplayInterrupt::OnKeyUp(int dikCode)
{
  if (_skipSimulation) return false;
  return Display::OnKeyUp(dikCode);
}

void DisplayInterrupt::OnSimulate(EntityAI *vehicle)
{
  // exit when game ended
  if (GWorld->GetMode() == GModeNetware)
  {
    NetworkClientState state = GetNetworkManager().GetClientState();
    if (state != NCSPlaying)
    {
      Exit(IDC_CANCEL);
      return;
    }
  }

  // wait until button entering the dialog released
  // avoid reaction to both input edges
  if (_skipSimulation)
  {
    if (!GInput.GetActionDo(UAIngamePause, false))
    {
      _skipSimulation = false;
    }
    return; // skip the last simulation step as well (up is active)
  }

  Display::OnSimulate(vehicle);

  // Sign in is handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200 &&_ENABLE_MP
  if (_signInStatus)
  {
    SetSignInStatus(_signInStatus);
  }
#endif

  // Pending invitations are handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
  if (_friends)
  {
    bool request = GetNetworkManager().IsPendingRequest();
    bool invitation = GetNetworkManager().IsPendingInvitation();

    if (request != _request || invitation != _invitation)
    {
      RString text = LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_FRIENDS);
      if (invitation)
        text = text + RString("<img image='\\xmisc\\gameinvitereceived.paa'/>");
      if (request)
        text = text + RString("<img image='\\xmisc\\friendinvitereceived.paa'/>");
      _friends->SetStructuredText(text);

      _request = request;
      _invitation = invitation;
    }
  }
#endif
}

RString InsertDiscForJoin();

void DisplayInterrupt::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_INTERRUPT_REVERT:
    if (exit == IDC_INT_LOAD)
    {
      DisplayInterruptRevert *child = static_cast<DisplayInterruptRevert *>(_child.GetRef());
      _selectedSlot = child->GetSaveSlot();
      Display::OnChildDestroyed(idd, exit);

      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_LOAD), IDD_MSG_LOAD_MISSION, true, &buttonOK, &buttonCancel);
    }
    else if (exit == IDC_INT_RETRY)
    {
    Display::OnChildDestroyed(idd, exit);

      if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RETRY_MISSION, false, &buttonOK, &buttonCancel);
      }
      else if (GWorld->GetMode() == GModeNetware && !GetNetworkManager().IsServer())
      {
        // ignore for client
      }
      else if (_saveHandles[SGTAutosave].GetResult()!=0)
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_LOAD), IDD_MSG_RETRY_MISSION, true, &buttonOK, &buttonCancel);
      }
      else
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RETRY_MISSION, false, &buttonOK, &buttonCancel);
      }
    }
    else if (exit == IDC_INT_RESTART)
    {
      Display::OnChildDestroyed(idd, exit);

      // ignore for the client
      if (GWorld->GetMode() != GModeNetware || GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RESTART_MISSION, false, &buttonOK, &buttonCancel);
      }
    }
    else
    {
      Display::OnChildDestroyed(idd, exit);
    }
    break;
  case IDD_SELECT_SAVE:
    if (exit == IDC_OK)
    {
      DisplaySelectUserSave *child = static_cast<DisplaySelectUserSave *>(_child.GetRef());
      _selectedSlot = child->GetSaveSlot();
      Display::OnChildDestroyed(idd, exit);
      if (_selectedSlot != -1)
      {
        // message box - replace old save?
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_SAVE_MISSION), IDD_MSG_SAVE_MISSION, false, &buttonOK, &buttonCancel);
      }
    }
    else
      Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_MSG_RETRY_MISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_INT_RETRY);
    }
    break;
  case IDD_MSG_RESTART_MISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_INT_RESTART);
    }
    break;
  case IDD_MSG_LOAD_MISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_INT_LOAD);
    }
    break;
  case IDD_MSG_SAVE_MISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_INT_SAVE);
    }
    break;
  case IDD_PROFILE:
    UpdateUserProfile();
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_MSG_TERMINATE_SESSION:
    Display::OnChildDestroyed(idd, exit);
#ifdef _XBOX
    // clean up temporary saves
    bool DeleteDirectoryStructure(const char *name, bool deleteDir);
    DeleteDirectoryStructure("z:\\currentmission", false);
#endif
    if (exit == IDC_OK) Exit(IDC_INT_ABORT);
    break;
  case IDD_MSG_RESPAWN:
	GWorld->GetGameState()->VarSet("respawn", GameValueExt(GWorld->PlayerOn()), false, false, GWorld->GetMissionNamespace()); // mission namespace
	GetNetworkManager().PublicVariable("respawn", 2 /*BOT_CLIENT*/);
	Exit(IDD_INTERRUPT);
	break;

#if _XBOX_SECURE && _ENABLE_MP
  case IDD_NETWORK_CONDITIONS:
    Display::OnChildDestroyed(idd, exit);
    switch (exit)
    {
    case IDC_OK:
      // Invitations implemented in GSaveGames
      break;
    case IDC_CANCEL:
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_XBOX_LIVE_HINT_PLAY), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_BACK), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          LocalizeString(IDS_XBOX_LIVE_ERROR_CONDITIONS),
          IDD_MSG_NETWORK_CONDITIONS, false, &buttonOK, &buttonCancel);
      }
      break;
    case IDC_AUTOCANCEL:
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
      break;
    }
    break;
  case IDD_MSG_NETWORK_CONDITIONS:
    Display::OnChildDestroyed(idd, exit);
    // Invitations implemented in GSaveGames
    break;
#endif // _XBOX_SECURE && _ENABLE_MP
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

void DisplayInterrupt::UpdateButtons()
{
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_INT_APPEAR_OFFLINE));
  if (text)
  {
    if (GetNetworkManager().IsCloaked())
      text->SetText(LocalizeString(IDS_DISP_XBOX_MP_INTERRUPT_ONLINE));
    else
      text->SetText(LocalizeString(IDS_DISP_XBOX_MP_INTERRUPT_OFFLINE));
  }

  {
    ITextContainer *text = GetTextContainer(GetCtrl(IDC_INT_MISSIONNAME));

    RString name;
    if (GWorld->GetMode() == GModeNetware)
    {
      const MissionHeader *header = GetNetworkManager().GetMissionHeader();
      if (header) name = header->GetLocalizedMissionName();
    }
    if (name.GetLength() == 0)
    {
      name = Localize(CurrentTemplate->Intel().briefingName);
      if (name.GetLength() == 0)
      {
        name = Glob.header.filenameReal;
        // user mission file name is encoded
        if (IsUserMission()) name = DecodeFileName(name);
      }
    }
    if(text) text->SetText(name);
  }
  {
    ITextContainer *text = GetTextContainer(GetCtrl(IDC_INT_DIFFICULTY));
    if(text)text->SetText(Glob.config.diffSettings[Glob.config.difficulty].displayName);
  }

}

DisplayEditorInterrupt::DisplayEditorInterrupt(ControlsContainer *parent, RString resource)
: Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _enableSimulation = false;
  _enableUI = false;

  Load(resource);
}

DisplayEditorInterrupt::~DisplayEditorInterrupt()
{
  GInput.ChangeGameFocus(-1);
}

void DisplayEditorInterrupt::OnButtonClicked(int idc)
{
  switch (idc)
  {
case IDC_INT_OPTIONS:
#ifdef _XBOX
  CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
#else
  bool IsXboxUI();
  if (IsXboxUI())
    CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
  else
    CreateChild(new DisplayOptions(this, false, false));
#endif
  break;
case IDC_INT_SETTINGS:
  if (CheckDiskSpace(this, "U:\\", CHECK_SPACE_PROFILE))
    CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
  break;
default:
  Exit(idc);
  break;
  }
}

void DisplayEditorInterrupt::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_PROFILE:
    UpdateUserProfile();
    Display::OnChildDestroyed(idd, exit);
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

DisplayCutsceneInterrupt::DisplayCutsceneInterrupt(ControlsContainer *parent)
: Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _enableSimulation = false;
  _enableUI = false;

  Load("RscDisplayMovieInterrupt");
}

DisplayCutsceneInterrupt::~DisplayCutsceneInterrupt()
{
  GInput.ChangeGameFocus(-1);
}

void DisplayCutsceneInterrupt::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_INT_SETTINGS:
    if (CheckDiskSpace(this, "U:\\", CHECK_SPACE_PROFILE))
      CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
    break;
  case IDC_INT_OPTIONS:
#ifdef _XBOX
    CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
#else
    bool IsXboxUI();
    if (IsXboxUI())
      CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), true));
    else
      CreateChild(new DisplayOptions(this, false, false));
#endif
    break;
  default:
    Exit(idc);
    break;
  }
}

void DisplayCutsceneInterrupt::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_PROFILE:
    UpdateUserProfile();
    Display::OnChildDestroyed(idd, exit);
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

// Main display
bool ContinueSaved = true;

#if defined _XBOX && !(_XBOX_VER>=2)
  #define ENABLE_RANDOM_CUTSCENE  0
#else
  #define ENABLE_RANDOM_CUTSCENE  1
#endif

#if _VBS2
  #undef ENABLE_RANDOM_CUTSCENE
  #define ENABLE_RANDOM_CUTSCENE  0
#endif

static void NoCutscene()
{
  // erase current mission
  SetMissionParameters(RString(), RString());
  Glob.header.filename[0] = 0;
  Glob.header.filenameReal = RString();

  SetBaseSubdirectory(RString());
  ExtParsMission.Clear();
  UnloadStringtable("Mission");

  CurrentTemplate = new DummyMission;

  const char *name = GLandscape->GetName();
  if (name && *name)
  {
    // extract world name from filename
    char islandName[256];
    const char *fname = strrchr(name, '\\');
    if (!fname) fname = name;
    else fname++;
    strcpy(islandName, fname);
    char *ext = strchr(islandName,'.');
    if (ext) *ext = 0;

    GWorld->SwitchLandscape(islandName);
    GEngine->ResetGameState();
    
  }
}

//! starts random cutscene (behind main menu)
void StartRandomCutscene(RString world, bool runInitSqs)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // Mission finished, set Rich Presence to the default one
  GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_INMENU);
#endif

#if ENABLE_RANDOM_CUTSCENE
#if _FORCE_DEMO_ISLAND
  world = Pars>>"CfgWorlds">>"demoWorld";
  if (world.GetLength() == 0)
  {
    world = Pars>>"CfgWorlds">>"initWorld";
  }
#endif
  extern bool SkipIntro;
  if (world.GetLength() == 0 || SkipIntro)
  {
    NoCutscene();
    return;
  }

  ParamEntryVal cls = Pars >> "CfgWorlds" >> world >> "cutscenes";
  int n = cls.GetSize();
  if (n == 0)
  {
    NoCutscene();
    return;
  }

  int i = toIntFloor(n * GRandGen.RandomValue());
  RString name = cls[i];

  SetBaseDirectory(false, "");
#if _ENABLE_MISSION_CONFIG
  ConstParamEntryPtr entry = (Pars >> "CfgMissions" >> "Cutscenes").FindEntry(name);
  if (entry)
  {
    if (!SelectMission(*entry))
    {
      NoCutscene();
      return;
    }
  }
  else SetMission(world, name, AnimsDir);
#else
  SetMission(world, name, AnimsDir);
#endif

  ParseIntro(true);

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeIntro);

  GLOB_WORLD->SwitchLandscape(world);
  if (!CurrentTemplate->IsEmpty())
  {
    CurrentTemplate->ActivateAddons(Glob.header.worldname);
    CurrentTemplate->InitGeneral(GModeIntro);
    GLOB_WORLD->InitVehicles(*CurrentTemplate);

    if (runInitSqs)
    {
      void RunInitIntroScript();
      RunInitIntroScript();
    }
  }

  ProgressFinish(p);
#else // ENABLE_RANDOM_CUTSCENE
  NoCutscene();
#endif // ENABLE_RANDOM_CUTSCENE
}

//! retrieves version of application
/*!
  \patch_internal 1.01 Date 06/21/2001 by Jirka - encapsulates retrieving of version
  - application version is now used not only for display in main manu, but also for addons checks
  \patch_internal 1.03 Date 07/12/2001 by Ondra
  - version retrieving method changed.
*/

#include "../versionNo.h"

#define STRINGIZE_X(x) #x
#define STRINGIZE(x) STRINGIZE_X(x)


RString GetAppVersion()
{
  return APP_VERSION_TEXT;
}

RString GetAppVersionAndBuild()
{
#if _VBS3
  RString ret = APP_VERSION_TEXT "." STRINGIZE(BUILD_NO) APP_VERSION_ADDITION;
  if(Glob.vbsEnableLVC) ret = ret + " LVC";
  if(Glob.vbsEnableCNR) ret = ret + " CNR";
  return ret;
#else
  return APP_VERSION_TEXT "." STRINGIZE(BUILD_NO) APP_VERSION_ADDITION;
#endif
}

static bool TextToData(const char *ptr, unsigned char *data, int n)
{
  DoAssert(strlen(ptr) == 2 * n);
  if (strlen(ptr) != 2 * n) return false;
  for (int i=0; i<n; i++)
  {
    int c = *ptr++;
    unsigned char c1 = c >= 'A' ? c - 'A' + 10 : c - '0';
    c = *ptr++;
    unsigned char c2 = c >= 'A' ? c - 'A' + 10 : c - '0';
    data[i] = (c1 << 4) | c2;
  }
  return true;
}

static void SaveLastPlayer()
{
#ifdef _XBOX
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile settings;
  settings.Parse("cache:\\settings.cfg", NULL, NULL, &globals);
  settings.Add("player", Glob.header.GetPlayerName());
  settings.Save("cache:\\settings.cfg");
#elif defined _WIN32
  const char *name = Glob.header.GetPlayerName();
  HKEY hKey;
  if (FindConfigAppRegistry(hKey) == ERROR_SUCCESS)
  {
    ::RegSetValueEx(hKey, "Player Name", NULL, REG_SZ, (BYTE *)name, strlen(name) + 1);
    ::RegCloseKey(hKey);
  }
  // create user directory
  CreatePath(GetUserDirectory());
#endif
}

#if _VBS2
DisplayMainVBS::DisplayMainVBS(ControlsContainer *parent)
  : DisplayMain(parent)
{
  if (Glob.vbsAdminMode)
  {
    // hide user controls
    if (GetCtrl(IDC_MAIN_PLAYER_NAME))
      GetCtrl(IDC_MAIN_PLAYER_NAME)->ShowCtrl(false);
    if (GetCtrl(IDC_MAIN_PLAYER_ID))
      GetCtrl(IDC_MAIN_PLAYER_ID)->ShowCtrl(false);
    if (GetCtrl(IDC_MAIN_SERVICE_ID_LABEL))
      GetCtrl(IDC_MAIN_SERVICE_ID_LABEL)->ShowCtrl(false);    
    if (GetCtrl(IDC_MAIN_NICKNAME_LABEL))
      GetCtrl(IDC_MAIN_NICKNAME_LABEL)->ShowCtrl(false);    
    if (GetCtrl(IDC_MAIN_NICKNAME_BG))
      GetCtrl(IDC_MAIN_NICKNAME_BG)->ShowCtrl(false);    
  }
  else
  {
    // hide admin controls
    if (GetCtrl(IDC_MAIN_PLAYER))
      GetCtrl(IDC_MAIN_PLAYER)->ShowCtrl(false);
    if (GetCtrl(IDC_MAIN_PLAYER_LABEL))
      GetCtrl(IDC_MAIN_PLAYER_LABEL)->ShowCtrl(false);

    CEditContainer *edit = GetEditContainer(GetCtrl(IDC_MAIN_PLAYER_NAME));
    if (edit)
    {
      if (GetLangID() == Korean)
        edit->SetMaxChars(14);
      else
        edit->SetMaxChars(24);
      edit->SetText(Glob.header.GetPlayerName());
    }

    edit = GetEditContainer(GetCtrl(IDC_MAIN_PLAYER_ID));
    if (edit)
    {
      if (!Glob.vbsAdminMode)
      {
        if (GetLangID() == Korean)
          edit->SetMaxChars(14);
        else
          edit->SetMaxChars(24);
        edit->SetText(Glob.header.playerHandle);
      }
      else
        if (GetCtrl(IDC_MAIN_PLAYER_ID))
          GetCtrl(IDC_MAIN_PLAYER_ID)->EnableCtrl(false);
    }
  }

  // color select box
  CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MAIN_PLAYER_COLOR));
  if (lbox)
  {    
    int sel = 0;
    int m = (Pars >> "CfgInGameUI" >> "PlayerColors").GetEntryCount();
    for (int j=0; j<m; j++)
    {
      const ParamEntry &entry = (Pars >> "CfgInGameUI" >> "PlayerColors").GetEntry(j);
      if (!entry.IsClass()) continue;
      int index = lbox->AddString
      (
        entry >> "text"
      );
      
      RString textureName = entry >> "texture";
      RString previewName = entry >> "previewTexture";
      RString fullName = FindPicture(previewName);
      if (fullName.GetLength() > 0)
      fullName.Lower();
      Ref<Texture> texture = GlobLoadTextureUI(fullName);
      lbox->SetTexture(index, texture);
      lbox->SetData(index, textureName);
      if (stricmp(textureName, Glob.playerColor) == 0) sel = j;
    }
    lbox->SetCurSel(sel);
  }
  //-!
}

void DisplayMainVBS::OnChildDestroyed(int idd, int exit)
{  
  DisplayMain::OnChildDestroyed(idd, exit);
}

void DisplayMainVBS::OnButtonClicked(int idc)
{
  // process player color
  CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MAIN_PLAYER_COLOR));
  if (lbox)
  {
    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
    ParamFile cfg;    
    if (ParseUserParams(cfg, &globals))
    {
      cfg.Add("playerColor", lbox->GetData(lbox->GetCurSel()));
      SaveUserParams(cfg);
    }
    Glob.playerColor = lbox->GetData(lbox->GetCurSel());
  }

  // process player ID
  CEditContainer *edit = GetEditContainer(GetCtrl(IDC_MAIN_PLAYER_NAME));
  CEditContainer *editID = GetEditContainer(GetCtrl(IDC_MAIN_PLAYER_ID));
  if (edit && editID && !Glob.vbsAdminMode)
  {
    // check service number
    RString name = edit->GetText();
    if (name.GetLength() == 0)
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERID_EMPTY));
      return;
    }
    if (strcspn(name, "\\/:*?\"<>|") < strlen(name))
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERID_INVALID));
      return;
    }
 
    // check player description
    RString playerHandle = editID->GetText();
    if (playerHandle.GetLength() == 0)
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_EMPTY));
      return;
    }
    if (strcspn(playerHandle, "\\/:*?\"<>|") < strlen(playerHandle))
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_INVALID));
      return;
    }

    if (name != Glob.header.GetPlayerName() || Glob.header.playerHandle != playerHandle)
    {
      _finddata_t info;
      RString dir = GetUserRootDir(name);
      long check = X_findfirst(dir, &info);

      Glob.header.SetPlayerName(name);      
      SaveLastPlayer();
      UpdateUserProfile();

      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;      

      // new user?
      if (check == -1)
      {
        // save default info         
        ParamEntryVal defSettings = Pars>>"CfgDefaultSettings">>"UserInfo";
        if (defSettings.GetClassInterface())
        {
          cfg.Update(*defSettings.GetClassInterface());          
          RString filename = GetUserRootDir(name) + RString("\\") + EncodeFileName(name) + RString(".") + GetExtensionProfile();
          cfg.Save(filename);        
        }        
      }
      // changed nickname?
      else if (Glob.header.playerHandle != playerHandle)
      {
        if (ParseUserParams(cfg, &globals))
        {
          cfg.Add("playerHandle", playerHandle);
          SaveUserParams(cfg);
        }
      }
      Glob.header.playerHandle = playerHandle;
    }
  }
  DisplayMain::OnButtonClicked(idc);
}
#endif

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

bool CheckForSignIn(bool online, bool forceUI = false, bool mustHaveMPPrivilege = false, bool *signInChanged = NULL)
{
#ifdef _XBOX
  int activeControllerIndex = GetActiveController();
  DoAssert(GSaveSystem.IsValidUserIndex(activeControllerIndex));

  if (signInChanged != NULL)
    *signInChanged = false;
#endif //#ifdef _XBOX

  // check if player is signed in
  if (!forceUI)
  {
    if (GSaveSystem.CheckSignIn(online, mustHaveMPPrivilege)) 
      return true;

#ifdef _XBOX
    if (GSaveSystem.GetUserIndex() != activeControllerIndex)
    {
      /// lets set the current user index based on the controller and check if he is signed in
      GSaveSystem.SetUserIndex(activeControllerIndex);
      if (GSaveSystem.CheckSignIn(online, mustHaveMPPrivilege)) 
      {
        if (signInChanged != NULL)
          *signInChanged = true;
        ApplyUserProfile();
        return true;
      }
    }
#endif //#ifdef _XBOX
  }
  
  // create the UI
  GSaveSystem.SignIn(online);

  // wait until the UI is shown
  Ref<ProgressHandle> p = ProgressStartExt(false, LocalizeString(IDS_SIGN_IN), "%s", true);
  while (!GSaveSystem.IsUIShown())
  {
    GSaveSystem.Simulate();
    ProgressRefresh();
    Sleep(20); // keep 50 fps
  };
  // wait until the UI is processed
  while (GSaveSystem.IsUIShown())
  {
    GSaveSystem.Simulate();
    ProgressRefresh();
    Sleep(20); // keep 50 fps
  } 
  ProgressFinish(p);

  if (signInChanged != NULL)
  {
    if (GSaveSystem.IsCheckInChanged())
      *signInChanged = true;
  }
  /// during the sign in process the signin of current user could change - lets handle it
  GSaveSystem.CheckInHandled();

#ifdef _XBOX
  /// lets set the current user index based on the controller
  if (GSaveSystem.GetUserIndex() != activeControllerIndex)
  {
    /// lets set the current user index based on the controller and check if he is signed in
    GSaveSystem.SetUserIndex(activeControllerIndex);
    if (signInChanged != NULL)
    {
      if (GSaveSystem.IsUserSignedIn())
        *signInChanged = true;
    }
  }
#endif //#ifdef _XBOX

  // update the main menu
  DisplayMain *disp = dynamic_cast<DisplayMain *>(GWorld->Options());
  if (disp) disp->UpdatePlayerName();

  /// we will apply profile even if noone is signed in - defaults will be loaded
  ApplyUserProfile();
  return GSaveSystem.CheckSignIn(online, mustHaveMPPrivilege);
}
#endif


DisplayMain::DisplayMain(ControlsContainer *parent)
  : Display(parent)
{
  _version = GetAppVersionAndBuild();
  _modIcons = NULL;
  _gameInitialized = false;
  _wantedNext = -1;
#if _VBS3
  _reConnectDelay = 0;
#endif

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  // When AutoTest is active, we should load the its config containing the list of missions
  if (!AutoTest.IsEmpty())
  {
    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile autoTest;
    autoTest.Parse(AutoTest, NULL, NULL, &globals);
    ParamEntryPtr cls = autoTest.FindEntry("TestMissions");
    if ( cls && cls->IsClass() && cls->GetEntryCount())
    {
      for (int i=0; i<cls->GetEntryCount(); i++)
      {
        ParamEntryVal mission = cls->GetEntry(i);
        if (mission.IsClass())
        {
          int ix = _testMissions.testMissions.Add();
          _testMissions.testMissions[ix].Load(mission);
        }
      }
    }
    else
    { //something was wrong, report it and exit
      if (!QIFileFunctions::FileExists(AutoTest)) RptF("Error: Cannot open AutoTest config file %s", cc_cast(AutoTest));
      else if (!cls) RptF("Error: AutoTest config file %s does not contain TestMissions class", cc_cast(AutoTest));
      else RptF("Error: TestMissions class of AutoTest config file %s has no subclasses.", cc_cast(AutoTest));
      _testMissions.SetFailed();
      extern int AutoTestExitCode;
      AutoTestExitCode = 10000;
    }
  }
#endif

  if (Glob.header.GetPlayerName().GetLength() > 0) UpdateUserProfile(); // player given in command line
  UpdatePlayerName();

  Load("RscDisplayMain");

  if (_modIcons)
  {
    // add mod icon pictures into the modIcons CControlsGroup
    // parameters taken from the IconPicture resource "template"
    ParamEntryVal iconPicture = Pars >> "RscDisplayMain" >> "IconPicture";
    CStatic *lastIcon = NULL;
    float iconWidth = Pars >> "RscDisplayMain" >> "IconPicture" >> "spacing";
    for (int i=GModInfos.Size()-1; i>=0; i--) //reverse order
    {
      ModInfo &mod = GModInfos[i];
      extern RString GetModPicturePath(ModInfo& mod);
      RString modPicture = GetModPicturePath(mod);
      //only active mods with pictures
      if ( !mod.active || mod.hidePicture || modPicture.IsEmpty() ) continue;
      CStatic *icon = new CStatic(this, -1, iconPicture);
      icon->SetText(modPicture);
      if (!lastIcon) 
      {
        iconWidth += icon->W();
      }
      else
      {
        float x = lastIcon->X()+iconWidth;
        if (x+lastIcon->W()>_modIcons->W()) break; //stop when there is no space for any other pictures
        icon->SetPos(lastIcon->X()+iconWidth, lastIcon->Y(), lastIcon->W(), lastIcon->H());
      }
      lastIcon = icon;
      _modIcons->AddControl(icon);
    }
  }

  if (GetCtrl(IDC_MAIN_CONTINUE))
    GetCtrl(IDC_MAIN_CONTINUE)->EnableCtrl(false);
#if !_ENABLE_MP
  if (GetCtrl(IDC_MAIN_MULTIPLAYER))
  {
    GetCtrl(IDC_MAIN_MULTIPLAYER)->EnableCtrl(false);
  }
#endif
#if !_ENABLE_EDITOR
  if (GetCtrl(IDC_MAIN_EDITOR))
  {
    GetCtrl(IDC_MAIN_EDITOR)->EnableCtrl(false);
  }
#endif
  // On Xbox 360, Achievements button is placed instead of Editor
#ifdef _XBOX
  if (GetCtrl(IDC_MAIN_EDITOR)) GetCtrl(IDC_MAIN_EDITOR)->ShowCtrl(false);
  if (GetCtrl(IDC_MAIN_QUIT)) GetCtrl(IDC_MAIN_QUIT)->ShowCtrl(false);
#endif

#if !_GAMES_FOR_WINDOWS && !defined _XBOX
  if (GetCtrl(IDC_MAIN_ACHIEVEMENTS)) GetCtrl(IDC_MAIN_ACHIEVEMENTS)->ShowCtrl(false);
#endif

#if !_ENABLE_ALL_MISSIONS
  if (GetCtrl(IDC_MAIN_CUSTOM))
  {
    GetCtrl(IDC_MAIN_CUSTOM)->ShowCtrl(false);
  }
#endif
#if !_ENABLE_AI || !_ENABLE_SINGLE_MISSION
  if (GetCtrl(IDC_MAIN_SINGLE))
  {
    GetCtrl(IDC_MAIN_SINGLE)->EnableCtrl(false);
  }
  if (GetCtrl(IDC_MAIN_MULTIPLAYER))
  {
    FocusCtrl(IDC_MAIN_MULTIPLAYER);
  }
#endif

#ifdef _XBOX
  _initialInteractionProcessed = false;
#endif

  // Invitations and content download are handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200 &&_ENABLE_MP
  if (XOnlineFriendsGetAcceptedGameInvite(&GGameInvite) == S_OK)
  {
    // confirmation needed to accept game invitation
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    const char *GetGamertag(const XUID &xuid);
    RString msg = Format(LocalizeString(IDS_MSG_PENDING_INVITATION), GetGamertag(GGameInvite.xuidAcceptedFriend), GGameInvite.InvitingFriend.szGamertag);
    CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, msg, IDD_MSG_PENDING_INVITATION, true, &buttonOK, &buttonCancel);
    _initialInteractionProcessed = true;
  }
  else
  {
    RString GetLiveSession();
    void ResetLiveSession();
    RString session = GetLiveSession();
    if (session.GetLength() > 0)
    {
      ResetLiveSession();
      if (Glob.header.GetPlayerName().GetLength() > 0 && GetNetworkManager().IsSignedIn())
      {
        // invitation accepted in DS
        GetNetworkManager().Init();
        XONLINE_FRIEND f;
        session.Upper();
        if (TextToData(session, (unsigned char *)&f.sessionID, sizeof(XNKID)))
        {
          CreateChild(new DisplayMultiplayerType(this, f));
        }
      }
    }
    else
    {
      bool IsContentDownload();
      void ResetContentDownload();
      if (IsContentDownload())
      {
        if (Glob.header.GetPlayerName().GetLength() > 0 && GetNetworkManager().IsSignedIn())
        {
          // restart in content download screen
          GetNetworkManager().Init();
          CreateChild(new DisplayMultiplayerType(this));
        }
        else
        {
          ResetContentDownload();
        }
      }
    }
  }
#endif

#if defined _XBOX
  _decoder = NULL;
  _playing = -1;
  _lastButtonClicked = 0;
  _testSignIn = true;
  _testStorageDevice = true;
#endif

#if ENABLE_RANDOM_CUTSCENE
  _enableSimulation = true;
  _enableDisplay = true;
#elif defined _XBOX
  _enableSimulation = false;
  _enableDisplay = false;
  ParamEntryVal list = Pars >> resource >> "AttractMode";
  int n = list.GetEntryCount();
  _video.Realloc(n);
  _video.Resize(n);
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = list.GetEntry(i);
    _video[i].file = entry >> "file";
    _video[i].showMenu = entry >> "showMenu";
    _video[i].time = entry >> "time";
  }
/*
  _lastChange = GetTickCount();
  if (_child == NULL) EnableVideo(true);
*/

#endif
}

static void SelectDifficulty()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    int difficulty = Glob.config.diffDefault;
    ConstParamEntryPtr entry = cfg.FindEntry("difficulty");
    if (entry)
    {
      RString name = *entry;
      int dif = Glob.config.diffNames.GetValue(name);
      if (dif >= 0) difficulty = dif;
    }
    Glob.config.difficulty = difficulty;
  }
}

// start the mission given by command line argument
void DisplayMain::StartUserMission(RString name)
{
#ifdef _XBOX
# if _XBOX_VER >= 200
  if (!GSaveSystem.IsStorageAvailable()) return;
  if (!GSaveSystem.CheckSaves()) return;

  for (int i=0; i<GSaveSystem.NSaves(); i++)
  {
    const SaveDescription &save = GSaveSystem.GetSave(i);
    if (save.fileName[0] == 'S' && save.fileName[1] == 'M') // Single-player Mission
    {
      if (stricmp(save.displayName, name) == 0)
      {
        // found
        SetMissionParams(save.fileName, save.displayName, false);

        GStats.ClearAll();
        SelectDifficulty();

        if (!LaunchIntro(this)) 
        {
          OnIntroFinished(this, IDC_CANCEL);
        }

        // User mission started
        if (_child) GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_USERMISSION);

        break;
      }
    }
  }

# else
  AutoArray<SaveHeader> list;
  SaveHeaderAttributes filter;
  filter.Add("type", FindEnumName(STMission));
  filter.Add("player", Glob.header.GetPlayerName());
  filter.Add("mission", name);
  GDebugger.PauseCheckingAlive();
  FindSaves(list, filter);
  GDebugger.ResumeCheckingAlive();
  if (list.Size() > 0)
  {
    RString filename = list[0].dir + RString("mission.") + GetExtensionWizardMission();
    SetMissionParams(filename, name, false);

    GStats.ClearAll();
    SelectDifficulty();

    if (!LaunchIntro(this)) 
    {
      OnIntroFinished(this, IDC_CANCEL);
    }
  }
# endif
#else
    RString filename = GetUserDirectory() + RString("missions\\") + name + RString(".") + GetExtensionWizardMission();
    if (QIFileFunctions::FileExists(filename))
    {
      SetMissionParams(filename, name, false);

      GStats.ClearAll();
      SelectDifficulty();

      if (!LaunchIntro(this)) 
      {
        OnIntroFinished(this, IDC_CANCEL);
      }

#if _GAMES_FOR_WINDOWS
      // User mission started
      if (_child) GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_USERMISSION);
#endif
    }
#endif
  if (_child) EnableVideo(false);
}

void DisplayMain::StartWizardMission(RString filename)
{
#ifndef _XBOX
  SetMissionParams(filename, filename, false);

  GStats.ClearAll();
  SelectDifficulty();

  if (!LaunchIntro(this)) 
  {
    OnIntroFinished(this, IDC_CANCEL);
  }

#if _GAMES_FOR_WINDOWS
  // User mission started
  if (_child) GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_USERMISSION);
#endif

  if (_child) EnableVideo(false);
#endif
}

void StartUserMission(AbstractOptionsUI *display, RString name)
{
  DisplayMain *disp = dynamic_cast<DisplayMain *>(display);
  if (disp) disp->StartUserMission(name);
}

void StartWizardMission(AbstractOptionsUI *display, RString filename)
{
  DisplayMain *disp = dynamic_cast<DisplayMain *>(display);
  if (disp) disp->StartWizardMission(filename);
}

/*!
\patch 5129 Date 2/20/2007 by Jirka
- New: possibility to skip briefing screen using "briefing = 0" in the mission description.ext
*/

void DisplayMain::DoStartMission(RString campaign, RString mission, bool skipBriefing)
{
  CurrentTemplate = new DummyMission;
  if (GWorld->UI()) GWorld->UI()->Init();

  CurrentMission = "";

  const char *ext1 = strrchr(mission, '\\');
  const char *ext2 = strrchr(mission, '.');
  if (!ext1 || !ext2 || ext2 < ext1) return;

  RString missionDir = mission.Substring(0, ext1 + 1 - cc_cast(mission));
  RString missionName(ext1 + 1, ext2 - ext1 - 1);
  RString missionWorld(ext2 + 1);

#if _ENABLE_UNSIGNED_MISSIONS
  {
    SetBaseDirectory(false, RString());
    RString filenameReal = missionName;

    RString bank = mission + RString(".pbo");
    if (QIFileFunctions::FileExists(bank))
    {
      RString prefix = CreateSingleMissionBank(mission);
      missionDir = "missions\\";
      missionName = "__cur_sp";
    }
    SetMission(missionWorld, missionName, missionDir);
    Glob.header.filenameReal = filenameReal;
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    // Single mission started
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_SINGLEMISSION);
#endif
  }
#else
  else return;
#endif // _ENABLE_UNSIGNED_MISSIONS

  GStats.ClearAll();
  SelectDifficulty();

  RString filename = GetMissionDirectory() + RString("mission.sqf");
  if (skipBriefing)
  {
    // even mission without units can be launched
    OnIntroFinished(this, IDC_CANCEL, true, skipBriefing);
  }
  else
  {
    if (!LaunchIntro(this)) 
    {
      OnIntroFinished(this, IDC_CANCEL);
    }
  }
  
  if (_child) EnableVideo(false);
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  else if (_testMissions.Active())
  {
    RptF("<Autotest result=\"FAILED\">");
    RptF("  Unable to start this mission (maybe it does not exist).");
    RptF(" Mission = %s", cc_cast(_testMissions.GetMission()));
    RptF("</Autotest>");
    _testMissions.failedCount++;
    _testMissions.Next();
  }
#endif
}


void DisplayMain::StartMission(RString campaign, RString mission, bool skipBriefing)
{
  // only store request, we are inside expression evaluation
  _wantedMission = mission;
  _wantedMissionSkipBriefing = skipBriefing;
}

void StartMission(RString campaign, RString mission, bool skipBriefing, bool ignoreChild)
{
#if defined _XBOX && _XBOX_VER >= 200
  // Can be invoked from the command line, if so, no user interaction processed yet
  if (GetActiveController() < 0)
  {
    // please, do not remove, no other sign-in check is processed in this case
    // select the first profile and activate it if valid
    GSaveSystem.SetUserIndex(0);
    // we want to call this even when no user is signed in -> defaults will be loaded
    ApplyUserProfile();
  }
#elif _GAMES_FOR_WINDOWS
  if (!CheckForSignIn(false)) return;
#else
  if (Glob.header.GetPlayerName().GetLength() == 0) return;
#endif

  DisplayMain *disp = dynamic_cast<DisplayMain *>(GWorld->Options());
	
	#ifdef _SUPER_RELEASE
		if (disp && (disp->Child() == NULL || ignoreChild)) disp->StartMission(campaign, mission, skipBriefing);
	#else
		if (disp) disp->StartMission(campaign, mission, skipBriefing);
	#endif
}

void StartMission(RString campaign, RString mission, bool skipBriefing)
{
  StartMission(campaign, mission, skipBriefing, false); // do not start mission when DisplayMain has a child
}

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
void AutotestStartMission()
{
  DisplayMain *disp = dynamic_cast<DisplayMain *>(GWorld->Options());
  if (disp) disp->AutotestStartMission();
}
void AutotestEndMission()
{
  DisplayMain *disp = dynamic_cast<DisplayMain *>(GWorld->Options());
  if (disp) disp->AutotestEndMission();
}
void DisplayMain::AutotestFrame(float duration, RString log1, RString log2)
{
  TestMissions::TestMission *info = _testMissions.GetInfo();
  if (info)
  {
    int maxCount = lenof(info->slowFrames);
    if (info->slowFramesCount < maxCount || duration > info->slowFrames[info->slowFramesCount - 1].duration)
    {
      // new slow frame found, insert to the list
      int i = 0;
      while (i < info->slowFramesCount && duration <= info->slowFrames[i].duration) i++;
      // insertion
      if (i < maxCount - 1)
      {
        info->slowFramesCount++;
        saturateMin(info->slowFramesCount, maxCount);
        for (int j=info->slowFramesCount-1; j>i; j--) info->slowFrames[j] = info->slowFrames[j - 1];
        info->slowFrames[i].time = 0.001f * GlobalTickCount();
        info->slowFrames[i].duration = duration;
        info->slowFrames[i].log1 = log1;
        info->slowFrames[i].log2 = log2;
      }
    }
  }
}
void AutotestFrame(float duration, RString log1, RString log2)
{
  DisplayMain *disp = dynamic_cast<DisplayMain *>(GWorld->Options());
  if (disp) disp->AutotestFrame(duration, log1, log2);
}
#endif

DisplayMain::~DisplayMain()
{
#ifdef _XBOX
  EnableVideo(false);
#endif
}

Control *DisplayMain::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
    case IDC_MAIN_PLAYER_NAME:
      {
        Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
        CEditContainer *edit = GetEditContainer(ctrl);
        if (edit) edit->SetMaxChars(15);
        return ctrl;
      }
    case IDC_MAIN_VERSION:
      {
        CStatic *text = new CStatic(this, idc, cls);
        char buffer[256];
        sprintf(buffer, LocalizeString(IDS_VERSION_INFO), (const char *)_version);
        text->SetText(buffer);
#if _VBS2 // version number is available for any display
        if (Glob.buildVersion.GetLength() == 0) Glob.buildVersion = buffer;
#endif
        return text;
      }
    case IDC_MAIN_MOD_LIST:
      {
        RString modList;
        for (int i=GModInfos.Size()-1; i>=0; i--)
        {
          ModInfo &modInfo = GModInfos[i];
          if (!modInfo.hideName && modInfo.origin != ModInfo::ModNotFound)
          {
            if (modList.IsEmpty())
              modList = modInfo.GetName();
            else
              modList = modList + RString("<br/>") + modInfo.GetName();
          }
        }
        UpdateModsHashes();
        GModInfosProfile.Copy(GModInfos); //only active modes are to be in GModInfosProfile (for ModLauncher purposes)
        Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
        CStructuredText *text = GetStructuredText(ctrl);
        if (text)
        {
          text->SetStructuredText(modList);
        }
        return ctrl;
      }
    case IDC_MAIN_MOD_ICONS:
      {
        Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
        _modIcons = static_cast<CControlsGroup *>(ctrl);
        return ctrl;
      }
    case IDC_MAIN_DATE:
      {
        CStatic *text = new CStatic(this, idc, cls);
#if !_VBS3
        #if defined _WIN32 && !defined _XBOX
        char filename[MAX_PATH];
        GetModuleFileName(NULL, filename, MAX_PATH);
        QFileTime time = QIFileFunctions::TimeStamp(filename);
        FILETIME info = ConvertToFILETIME(time), infoLocal;
        ::FileTimeToLocalFileTime(&info, &infoLocal);
        SYSTEMTIME stime;
        ::FileTimeToSystemTime(&infoLocal, &stime);
        int day = stime.wDay;
        int month = stime.wMonth;
        int year = stime.wYear;
        
        BString<256> buffer;
        sprintf(buffer, "%d/%d/%d", month, day, year);
        text->SetText(buffer.cstr());
#      if _VBS2 // version number is available for any display
        if (Glob.buildDate.GetLength() == 0) Glob.buildDate = buffer;
#      endif
        #endif

#else //_VBS3, use Expiry date!
        text->SetText(Glob.expiryDate);
#endif
        return text;
      }
    default:
      return Display::OnCreateCtrl(type, idc, cls);
  }
}


CDP_DECL bool __cdecl CDPCreateOutro(Display *disp, RString endMode)
{
  EEndMode end = GetEnumValue<EEndMode>(cc_cast(endMode));
  if (end<0) end = EMEnd1; //ToDo
  else if (end == EMContinue || end == EMKilled) end = EMLoser;

  switch (end)
  {
  case EMLoser:
    ParseCutscene("OutroLoose", false, true);
    break;
  case EMEnd1:
  case EMEnd2:
  case EMEnd3:
  case EMEnd4:
  case EMEnd5:
  case EMEnd6:
  case EMCheat:
    ParseCutscene("OutroWin", false, true);
    break;
  }

  if (CurrentTemplate->IsEmpty())
    return false;

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeIntro);

  GLOB_WORLD->SwitchLandscape(Glob.header.worldname);
  CurrentTemplate->ActivateAddons(Glob.header.worldname);
  CurrentTemplate->InitGeneral(GModeIntro);
  bool ok = GLOB_WORLD->InitVehicles(*CurrentTemplate);

  if (ok)
  {
    GWorld->FadeInMission();
  }

  ProgressFinish(p);

  if (!ok) return false;

  disp->CreateChild(new DisplayOutro(disp, end));
  
  GWorld->OnInitMission(true);
  
  return true;
}

/*!
\patch 2.01 Date 2/4/2003 by Jirka
- Changed: Dialogs logic changed: after end of mission, game returns to campaign or single mission dialog (not to main menu)
*/

bool OnIntroFinished(Display *disp, int exit, bool enableEmptyMission, bool skipBriefing)
{
  GWorld->DestroyMap(IDC_OK);

  if (exit == IDC_ABORT)
  {
    StartRandomCutscene(Glob.header.worldname);
    return false;
  }

  if (GWorld->UI()) GWorld->UI()->Init();
  if
  (
    !ParseMission(false, true) ||
    !enableEmptyMission && CurrentTemplate->IsEmpty()
  )
  {
    if (CDPCreateOutro(disp, FindEnumName(EMLoser))) return true;
    return OnOutroFinished(disp, IDC_CANCEL);
  }

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeArcade);

  GLOB_WORLD->SwitchLandscape(Glob.header.worldname);
  CurrentTemplate->ActivateAddons(Glob.header.worldname);
  CurrentTemplate->InitGeneral(GModeArcade);
  GLOB_WORLD->InitVehicles(*CurrentTemplate);

  DeleteSaveGames();
  DeleteWeaponsFile();

  ProgressFinish(p);

  disp->CreateChild(new DisplayMission(disp));
  return true;
}

bool OnBriefingFinished(Display *disp, int exit)
{
  if (exit == IDC_CANCEL)
  {
    // exit to main menu
    StartRandomCutscene(Glob.header.worldname);
    return false;
  }
  else
  {
    disp->CreateChild(new DisplayMission(disp));
    return true;
  }
}

bool OnMissionFinished(Display *disp, int exit, bool restartEnabled)
{
  if (exit == IDC_MAIN_QUIT)
  {
    // escape directly into menu
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
    if (!AutoTest.IsEmpty()) disp->Exit(IDC_MAIN_QUIT);
    else 
#endif
      StartRandomCutscene(Glob.header.worldname);
    return false;
  }
  else
  {
    // note: debriefing removed here
    return true;
  }
}

bool OnOutroFinished(Display *disp, int exit)
{
  if (exit == IDC_ABORT)
  {
    StartRandomCutscene(Glob.header.worldname);
    return false;
  }

  StartRandomCutscene(Glob.header.worldname);
  return false;
}

#include "../Protect/selectProtection.h"


CDP_DECL void __cdecl CDPCreateDisplaySingleMission(ControlsContainer *parent)
{
  SECUROM_MARKER_HIGH_SECURITY_ON(7)
  parent->CreateChild(new DisplaySingleMission(parent));
  SECUROM_MARKER_HIGH_SECURITY_OFF(7)
}

void DisplayMain::EnableVideo(bool enable)
{
#ifdef _XBOX
  // destroy current video
  if (_decoder)
  {
    GEngine->VideoClose(_decoder);
    if (GWorld) GWorld->ResetTiming();
    _decoder = NULL;
    _playingFile = RString();
  }
  _playing = -1;

  if (!enable || _video.Size() == 0) return;

  // create new video
  NextVideo(true);
#endif
}

void DisplayMain::NextVideo(bool interrupted)
{
#ifdef _XBOX
  if (_video.Size() == 0) return;
  if (++_playing >= _video.Size()) _playing = 0;
  RString file = _video[_playing].file;
  if (strcmpi(file,_playingFile))
  {
    if (_decoder)
    {
      GEngine->VideoClose(_decoder);
      _decoder = NULL;
    }
    else
    {
      if (GWorld) GWorld->ResetTiming();
    }
    _playingFile = _video[_playing].file;
    float volume = GSoundsys ? GSoundsys->GetVolumeAdjustMusic() : 1.0f;
    _decoder = GEngine->VideoOpen(_playingFile, volume);
  }
  DWORD now = GetTickCount();
  _lastChange = interrupted ? 0 : now;
  _nextChange = now + toInt(1000 * _video[_playing].time);
#endif
}

bool DisplayMain::IsMenuEnabled() const
{
#ifdef _XBOX
  if (_playing < 0) return true;
  return _video[_playing].showMenu;
#else
  return true;
#endif
}

static bool IsCableConnected()
{
#ifdef _XBOX
  DWORD status = XNetGetEthernetLinkStatus();
  return status != 0;
#else
  return true;
#endif
}

void DisplayMain::UpdatePlayerName()
{
  CStructuredText *text = GetStructuredText(GetCtrl(IDC_MAIN_SETTINGS_PROFILES));
  if (!text) return;

  RString name = Glob.header.GetPlayerName();
  if (name.GetLength() > 0)
    text->SetRawText(Format(LocalizeString(IDS_SETTINGS_FORMAT), (const char *)name));
  else
  {
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    text->SetRawText(LocalizeString(IDS_SILENT_SIGN_IN_NONE));
#else
    text->SetRawText(LocalizeString(IDS_DISP_MAIN_XBOX_SETTINGS));
#endif
  }
}

static bool SetCurrentMission(CTree *tree)
{
  // Return to previous game is not availiable now
  // TODO: different handling for a scripted mission
  CurrentTemplate = new ArcadeTemplate;
  if (GWorld->UI()) GWorld->UI()->Init();

  CTreeItem *item = tree->GetSelected();
  Assert(item);
  Assert(item->level > 0);

  CurrentMission = "";
  Glob.config.difficulty = Glob.config.diffDefault;

  RString mission;
  RString world;
  RString path;
  if (item->value == 1)
  {
    // new
    world = item->data;
    CTreeItem *parent = item->parent;
    path = parent->data + RString("\\");
  }
  else
  {
    // edit
    Assert(item->value == 2);

    // mission name and world
    const char *start = item->data;
    const char *name = strrchr(start, '\\');
    if (name) name++;
    else name = start;

    const char *ptr = strrchr(name, '.');
    if (!ptr)
    {
      return false; // wrong directory
    }
    mission = RString(name, ptr - name);
    world = RString(ptr + 1);
    path = RString(start, name - start);
  }

  RString dir;
  RString subdir;
  subdir = path;
  /*
  LogF
  (
  "Dir: '%s', Subdir: '%s', World: '%s', Mission: '%s'",
  (const char *)dir, (const char *)subdir, (const char *)world, (const char *)mission
  );
  */

  SetBaseDirectory(false, "");
  SetMission(world, mission, subdir);
  GLOB_WORLD->SwitchLandscape(Glob.header.worldname);

  return true;
}

static bool CloseReadHandles(const FileItem &file, bool &ctx)
{
  RString fullPath = file.path + file.filename;
  GFileServerFunctions->FlushReadHandle(fullPath);
  return false; // continue with enumeration
}

class DisplaySingleplayer : public Display
{
public:
  DisplaySingleplayer(ControlsContainer *parent);
  void OnButtonClicked(int idc);
  void OnChildDestroyed(int idd, int exit);
};

DisplaySingleplayer::DisplaySingleplayer(ControlsContainer *parent)
: Display(parent)
{
  Load("RscDisplaySingleplayer");

#if !_ENABLE_EDITOR
  if (GetCtrl(IDC_SP_EDITOR)) GetCtrl(IDC_SP_EDITOR)->EnableCtrl(false);
#endif
#ifdef _XBOX
  if (GetCtrl(IDC_SP_EDITOR)) GetCtrl(IDC_SP_EDITOR)->ShowCtrl(false);
#endif

#if !_ENABLE_AI || !_ENABLE_SINGLE_MISSION
  if (GetCtrl(IDC_SP_MISSION)) GetCtrl(IDC_SP_MISSION)->EnableCtrl(false);
#endif

#if defined _XBOX && _XBOX_VER >= 200
  if (GSaveSystem.IsUserSignedIn() && !GSaveSystem.CheckSignIn(true, false))
  {
    //user is signed in, but not signed in to Live - just show warning about statistics
    // localization: "You are not signed to Xbox LIVE. Statistics will not be posted on leaderboards."
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LIVE_STATS_WARN));
  }
#endif

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // we don't need to be connected to Live for playing this mode
  GSaveSystem.SetLiveConnectionNeeded(false);
#endif


}

void DisplaySingleplayer::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_SP_EDITOR:
#if _ENABLE_EDITOR
# if _GAMES_FOR_WINDOWS
    if (!CheckForSignIn(false)) break;
# endif
#ifdef _WIN32
    CreateChild(new DisplaySelectIsland(this));
#endif
#endif
    break;
  case IDC_SP_MISSION:
#if _ENABLE_AI && _ENABLE_SINGLE_MISSION
#if defined _XBOX && _XBOX_VER >= 200
    // GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#elif _GAMES_FOR_WINDOWS
    if (!CheckForSignIn(false)) break;
#endif
    // Macrovision CD Protection
    CDPCreateDisplaySingleMission(this);
    //CreateChild(new DisplaySingleMission(this));
#endif
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplaySingleplayer::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_SELECT_ISLAND:
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_ARCADE_MAP:
    Display::OnChildDestroyed(idd, exit);
#if _ENABLE_EDITOR
    StartRandomCutscene(Glob.header.worldname);
#endif
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

/*!
\patch 1.20 Date 08/15/2001 by Jirka
- Fixed: Bad state of main menu (no background cutscene).
- Happens after disconnect from multiplayer game started with command "-connect" or -host.
\patch 1.82 Date 8/23/2002 by Ondra
- Fixed: Weapon pool lost after using Revert or Continue
to start missions First Strike or Fireworks.
\patch 5095 Date 12/1/2006 by Jirka
- Fixed: Parameters set when creating a new profile was not saved
\patch 5097 Date 12/6/2006 by Jirka
- Fixed: Rename of profile work correctly now
*/

void DisplayMain::OnChildDestroyed(int idd, int exit)
{
  void FlushLogFile();
  FlushLogFile();

#ifdef _WIN32
  switch (idd)
  {
#if defined _XBOX && _XBOX_VER >= 200
    case IDD_MSG_XBOX_NO_SIGN_IN:  // player is not signed in to current profile
      {
        Display::OnChildDestroyed(idd, exit);
        if (exit == IDC_OK)
        {
          // player wants to continue without signing in
          // we have to turn sign in testing off and then simulate last button click
          _testSignIn = false;
          OnButtonClicked(_lastButtonClicked);
          _testSignIn = true;
        }
      }
      break;

    case IDD_MSG_XBOX_NO_STORAGE:  // player did not select storage device
      {
        Display::OnChildDestroyed(idd, exit);
        if (exit == IDC_OK)
        {
          // player wants to continue without selecting storage device
          // we have to turn storage device testing off and then simulate last button click
          _testSignIn = false;
          _testStorageDevice = false;
          OnButtonClicked(_lastButtonClicked);
          _testSignIn = true;
          _testStorageDevice = true;
        }
      }
      break;

#endif

    case IDD_MOD_LAUNCHER:
      if (exit==IDC_MAIN_QUIT)
      {
        Display::OnChildDestroyed(idd, exit);
        Exit(IDC_MAIN_QUIT);
      }
      else Display::OnChildDestroyed(idd, exit);
      break;
    case IDD_PASSWORD:
      if (exit == IDC_OK)
      {
        DisplayPassword *display = dynamic_cast<DisplayPassword *>(_child.GetRef());
        if (display)
        {
          RString guid = display->GetGUID();
          RString password = display->GetPassword();
          RString serverName = display->GetServerName();
          if (password.IsEmpty()) password = RString("fail"); //intended fail to show bad password message
          Display::OnChildDestroyed(idd, exit);
          extern const int DefaultNetworkPort;
          void __cdecl CDPCreateClient(RString ip, int port, RString password, RString serverName);
          CDPCreateClient(guid, DefaultNetworkPort, password, serverName);
          return;
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
    case IDD_SINGLE_MISSION:  // Single mission
      Display::OnChildDestroyed(idd, exit);
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
      if (exit == IDC_MAIN_QUIT && !AutoTest.IsEmpty()) Exit(IDC_MAIN_QUIT);
#endif
      break;
    case IDD_CUSTOM_ARCADE: // All missions
      {
#if _ENABLE_EDITOR2
        if (exit == IDC_CUST_EDIT_2)
        {
          CTree *tree = dynamic_cast<CTree *>(_child->GetCtrl(IDC_CUST_GAME));
          if (!tree)
          {
            Display::OnChildDestroyed(idd, exit);
            break;
          }
          bool ok = SetCurrentMission(tree);
          Display::OnChildDestroyed(idd, exit);
          if (ok)
          {
            CreateChild(CreateMissionEditor(this));
          }
        }
        else
#endif
        Display::OnChildDestroyed(idd, exit);
      }
      break;
    case IDD_SELECT_ISLAND:
#if _ENABLE_EDITOR
      // IDC_SELECT_ISLAND_EDITOR and IDC_OK for the newest available editor
      if (exit == IDC_SELECT_ISLAND_EDITOR || exit == IDC_OK)
      {
#if _ENABLE_EDITOR2
        {
          // launch mission editor 2
          CurrentTemplate = new ArcadeTemplate;
          if (GWorld->UI()) GWorld->UI()->Init();

          CListBoxContainer *lbox = GetListBoxContainer(_child->GetCtrl(IDC_SELECT_ISLAND));
          if (!lbox)
          {
            Display::OnChildDestroyed(idd, exit);
            break;
          }
          RString world = lbox->GetData(lbox->GetCurSel());

          CurrentMission = "";
          SetBaseDirectory(true, RString());
          ::CreateDirectory(GetBaseDirectory() + RString("missions"), NULL);
          SetMission(world, "");

          Display::OnChildDestroyed(idd, exit);
          GLOB_WORLD->SwitchLandscape(Glob.header.worldname);

          CreateChild(CreateMissionEditor(this));
        }
#endif
      }
      else
#endif  // #if _ENABLE_EDITOR
        Display::OnChildDestroyed(idd, exit);
      break;
#if _ENABLE_EDITOR
    case IDD_ARCADE_MAP:
      Display::OnChildDestroyed(idd, exit);
#if _ENABLE_MAIN_MENU_TABS
      // from editor back to island select screen
      OnButtonClicked(IDC_MAIN_EDITOR);
#else
      StartRandomCutscene(Glob.header.worldname);
#endif
      break;
#endif // #if _ENABLE_EDITOR
#if _ENABLE_EDITOR2
    case IDD_MISSION_EDITOR:
      Display::OnChildDestroyed(idd, exit);
#if _ENABLE_MAIN_MENU_TABS
      // from editor back to island select screen
      OnButtonClicked(IDC_MAIN_EDITOR);
#else
      StartRandomCutscene(Glob.header.worldname);
#endif
      break;
#endif // #if _ENABLE_EDITOR2
//{ Mission logic 
    case IDD_INTRO:
      Display::OnChildDestroyed(idd, exit);
      OnIntroFinished(this, exit);
      break;
    case IDD_INTEL_GETREADY:
      Display::OnChildDestroyed(idd, exit);
      OnBriefingFinished(this, exit);
      break;
    case IDD_MISSION:
      Display::OnChildDestroyed(idd, exit);
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
      if ( _testMissions.Active() ) 
      {
        if ( GWorld->GetEEndMode()!=_testMissions.GetEnd() )
        {
          _testMissions.failedCount++;
          RString endName = GWorld->GetEndMode();
          RptF("<AutoTest result=\"FAILED\">");
          RptF(" EndMode = %s", cc_cast(endName));
          if ( !_testMissions.GetCampaign().IsEmpty() )
            RptF(" Campaign = %s", cc_cast(_testMissions.GetCampaign()));
          RptF(" Mission = %s", cc_cast(_testMissions.GetMission()));
          RptF("</AutoTest>");
        };

        TestMissions::TestMission *info = _testMissions.GetInfo();
        if (info)
        {
          LogF("<AutoTestInfo mission=\"%s\" duration=\"%.3f s\">", cc_cast(info->mission), info->ended - info->started);
          if (info->slowFramesCount > 0)
          {
            LogF("The %d slowest frames:", info->slowFramesCount);
            for (int i=0; i<info->slowFramesCount; i++)
            {
              const TestMissions::TestMission::SlowFrameInfo &frame = info->slowFrames[i];
              LogF("Time: %.3f s, duration %.0f ms", frame.time - info->started, 1000.0f * frame.duration);
              LogF("\n%s", cc_cast(frame.log1));
              // LogF("\n%s", cc_cast(frame.log2));
            }
          }
          LogF("</AutoTestInfo>");
        }

        _testMissions.Next();
      }
      else 
#endif
        OnMissionFinished(this, exit, false);
      break;
    case IDD_OUTRO:
      Display::OnChildDestroyed(idd, exit);
      OnOutroFinished(this, exit);
      break;
//}
    case IDD_MULTIPLAYER:
      Assert(exit == IDC_CANCEL);
      GetNetworkManager().Done();
      GChatList.Clear();
      GChatList.ScriptEnable(true);
      GCustomChannels.ClearCustomChannels();
      Display::OnChildDestroyed(idd, exit);
      StartRandomCutscene(Glob.header.worldname);
      break;
    case IDD_PROFILE_LIVE:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
        CreateChild(new DisplayMultiplayerType(this));
      break;
    case IDD_CLIENT:
    case IDD_SERVER:
    case IDD_MP_TYPE:
      // FIX
      GetNetworkManager().Done();
      GChatList.Clear();
      GChatList.ScriptEnable(true);
      GCustomChannels.ClearCustomChannels();
      Display::OnChildDestroyed(idd, exit);
      StartRandomCutscene(Glob.header.worldname);
      break;
#ifndef _XBOX
    case IDD_OPTIONS:
    case IDD_OPTIONS_VIDEO:
    case IDD_OPTIONS_AUDIO:
      Display::OnChildDestroyed(idd, exit);
      if (exit==IDC_MAIN_QUIT)
      {
        Exit(IDC_MAIN_QUIT);
      }
      break;
    case IDD_LOGIN:
      {
        CListBoxContainer *list = GetListBoxContainer(_child->GetCtrl(IDC_LOGIN_USER));
        if (!list)
        {
          // cannot determine the selected player
          Display::OnChildDestroyed(idd, exit);
          break;
        }

        // Determine if new player should be forced (the actual player cannot be found in the list (f.i. because it was deleted from the list of players))
        bool forceNewPlayer = false;
        if (exit == IDC_CANCEL)
        {
          forceNewPlayer = true;
          for (int i = 0; i < list->Size(); i++)
          {
            if (list->GetText(i) == Glob.header.GetPlayerName())
            {
              forceNewPlayer = false;
            }
          }
        }

        // If either OK was pressed or old player is not in the list, select a new player
        if (exit == IDC_OK || forceNewPlayer)
        {
          GWorld->SaveProfileNamespace();
          int index = list->GetCurSel();
          Assert(index >= 0);
          Glob.header.SetPlayerName(list->GetText(index));
          //        LoadHeader();
          SaveLastPlayer();
          Display::OnChildDestroyed(idd, exit);

          // load info from identity
          GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
          ParamFile cfg;
          if (ParseUserParams(cfg, &globals))
          {
            ConstParamEntryPtr identity = cfg.FindEntry("Identity");
            if (identity)
            {
              ConstParamEntryPtr entry = identity->FindEntry("face");
              if (entry) Glob.header.playerFace = *entry;
              entry = identity->FindEntry("glasses");
              if (entry) Glob.header.playerGlasses = *entry;
              entry = identity->FindEntry("speaker");
              if (entry) Glob.header.playerSpeakerType = *entry;
              entry = identity->FindEntry("pitch");
              if (entry) Glob.header.playerPitch = *entry;
              /* in UpdateUserProfile
              entry = identity->FindEntry("mask");
              if (entry) Glob.header.voiceMask = *entry;
              */
            }
            UpdateUserProfile();
          }
          GWorld->_resetOptions = true;
        }
        else if (exit == IDC_LOGIN_NEW || exit == IDC_LOGIN_EDIT)
        {
          int index = list->GetCurSel();
          if (index >= 0)
          {
            Glob.header.SetPlayerName(list->GetText(index));
            SaveLastPlayer();
          }
          RString player = Glob.header.GetPlayerName();
          Display::OnChildDestroyed(idd, exit);
          CreateChild(new DisplayNewUser(this, player, exit == IDC_LOGIN_EDIT));
        }
        else
        {
          Display::OnChildDestroyed(idd, exit);
        }
      }
      break;
    case IDD_NEW_USER:
      if (exit == IDC_OK)
      {
        CEditContainer *edit = GetEditContainer(_child->GetCtrl(IDC_NEW_USER_NAME));
        if (!edit)
        {
          Display::OnChildDestroyed(idd, exit);
          break;
        }
        RString name = edit->GetText();

        DisplayNewUser *disp = static_cast<DisplayNewUser *>((ControlsContainer *)_child);
        RString oldName = disp->_name;

        if (disp->_edit && stricmp(name, oldName) != 0)
        {
          // cannot rename to default user name
          RString dstDir = GetUserRootDir(name);
          if (stricmp(dstDir, GetDefaultUserRootDir()) == 0) return;
          // cannot rename default user
          RString srcDir = GetUserRootDir(oldName);
          if (stricmp(srcDir, GetDefaultUserRootDir()) == 0) return;

          bool dummy = false;
          ForEachFileR(srcDir + RString("\\"), CloseReadHandles, dummy);

          // rename the directory
          if (::MoveFile(srcDir, dstDir) == 0)
          {
            LogF("Cannot rename directory, error 0x%x", GetLastError());
            break;
          }
          // rename the user profile
          if (::MoveFile(
            dstDir + RString("\\") + EncodeFileName(oldName) + RString(".") + GetExtensionProfile(),
            dstDir + RString("\\") + EncodeFileName(name) + RString(".") + GetExtensionProfile()) == 0)
          {
            LogF("Cannot rename profile, error 0x%x", GetLastError());
            break;
          }
        }
        
        Glob.header.SetPlayerName(name);
        SaveLastPlayer();

        CListBoxContainer *list = GetListBoxContainer(_child->GetCtrl(IDC_NEW_USER_FACE));
        if (list)
        {
          int sel = list->GetCurSel();
          if (sel >= 0) Glob.header.playerFace = list->GetData(sel);
        }
        list = GetListBoxContainer(_child->GetCtrl(IDC_NEW_USER_GLASSES));
        if (list)
        {
          int sel = list->GetCurSel();
          if (sel >= 0) Glob.header.playerGlasses = list->GetData(sel);
        }
        list = GetListBoxContainer(_child->GetCtrl(IDC_NEW_USER_SPEAKER));
        if (list)
        {
          int sel = list->GetCurSel();
          if (sel >= 0) Glob.header.playerSpeakerType = list->GetData(sel);
        }
        CSliderContainer *slider = GetSliderContainer(_child->GetCtrl(IDC_NEW_USER_PITCH));
        if (slider) Glob.header.playerPitch = slider->GetThumbPos();

        GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
        ParamFile cfg;
        bool ok = ParseUserParams(cfg, &globals);
        if (!ok && !disp->_edit)
        {
          ParamEntryVal defSettings = Pars>>"CfgDefaultSettings">>"UserInfo";
          if (defSettings.GetClassInterface())
            cfg.Update(*defSettings.GetClassInterface());
          ok = true;
        }

        if (ok)
        {
          // save info to identity
          ParamClassPtr identity = cfg.AddClass("Identity");
          identity->Add("face", Glob.header.playerFace);
          identity->Add("glasses", Glob.header.playerGlasses);
          identity->Add("speaker", Glob.header.playerSpeakerType);
          identity->Add("pitch", Glob.header.playerPitch);

          //take screen settings from old profile
          AspectSettings asp;
          GEngine->GetAspectSettings(asp);
          //cfg.Add("fovBottom",_aspectSettings.bottomFOV);
          cfg.Add("fovTop",asp.topFOV);
          //cfg.Add("fovRight",_aspectSettings.rightFOV);
          cfg.Add("fovLeft",asp.leftFOV);

          cfg.Add("uiTopLeftX",asp.uiTopLeftX);
          cfg.Add("uiTopLeftY",asp.uiTopLeftY);
          cfg.Add("uiBottomRightX",asp.uiBottomRightX);
          cfg.Add("uiBottomRightY",asp.uiBottomRightY);
          cfg.Add("tripleHead",asp.tripleHead);

          cfg.Add("IGUIScale",Glob.config.IGUIScale);

          SaveUserParams(cfg);
          UpdateUserProfile();
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
#endif

#if !defined(_XBOX) && defined(_WIN32)
    case IDD_ESRB:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK) CreateChild(new DisplayMultiplayer(this));
      break;
#endif
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (exit > IDC_MAIN_TAB_LOGIN)
  {
    switch (exit)
    {
      case IDC_MAIN_TAB_SINGLE:
        OnButtonClicked(IDC_MAIN_SINGLE);
        break; 
      case IDC_MAIN_TAB_MULTIPLAYER:
        OnButtonClicked(IDC_MAIN_MULTIPLAYER);
        break; 
      case IDC_MAIN_TAB_EDITOR:
        OnButtonClicked(IDC_MAIN_EDITOR);
        break; 
      case IDC_MAIN_TAB_OPTIONS:
        OnButtonClicked(IDC_MAIN_OPTIONS);
        break; 
      case IDC_MAIN_TAB_CONTROLS:
        OnButtonClicked(IDC_MAIN_CONTROLS);
        break; 
      case IDC_MAIN_TAB_QUIT:
        OnButtonClicked(IDC_MAIN_QUIT);
        break;    
      case IDC_MAIN_TAB_AAR:
        OnButtonClicked(IDC_MAIN_AAR);
        break; 
#if _VBS2
      case IDC_MAIN_TAB_LIBRARY:
        {
          RString action;
          CButton *libraryBtn = dynamic_cast<CButton *>(GetCtrl(IDC_MAIN_TAB_LIBRARY));
          if (libraryBtn)
            action = libraryBtn->GetAction();
          CActiveText *libraryText = dynamic_cast<CActiveText *>(GetCtrl(IDC_MAIN_TAB_LIBRARY));
          if (libraryText)
            action = libraryText->GetAction();
          if (action.GetLength() > 0)
          {
            GameState *gstate = GWorld->GetGameState();
            GameVarSpace local(false);
            int gshandle = gstate->BeginContext(&local);
            gstate->Execute(action, GameState::EvalContext::_default, GWorld->GetMissionNamespace());
            gstate->EndContext(gshandle);
            float GetTimeForScripts();
            GWorld->SimulateScripts(0, GetTimeForScripts());
          }
        }
        break; 
#endif
    }
  }
#endif
#endif            // _WIN32
  if (!_child) EnableVideo(true);
  
  IControl *videoControl = GetCtrl(IDC_MAIN_VIDEOCONTROL);
  if (videoControl && videoControl->getDecoder() && !videoControl->getDecoder()->isPlaying())
  {
    videoControl->getDecoder()->pause(true);
}
}

class ConnectCableMsg : public MsgBox
{
public:
  ConnectCableMsg(ControlsContainer *parent)
    : MsgBox(parent, MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_SYSTEM_LINK_CABLE), IDD_MSG_XONLINE_CONNECTION_FAILED)
  {
  }
  void OnSimulate(EntityAI *vehicle)
  {
    MsgBox::OnSimulate(vehicle);
    if (IsCableConnected()) Exit(IDC_OK);
  }
};

#if defined _XBOX && _XBOX_VER >= 200
bool DisplayMain::CheckStorage()
{
  if (!GSaveSystem.IsUserSignedIn())
    return false;
  if (!GSaveSystem.CheckDevice(CHECK_SAVE_BYTES))
  {
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    CreateMsgBox(
      MB_BUTTON_OK | MB_BUTTON_CANCEL,
      LocalizeString(IDS_MSG_NO_STORAGE_DEVICE),
      IDD_MSG_XBOX_NO_STORAGE, false, &buttonOK, &buttonCancel
      );
    return false;
  }
  return true;
}
#endif


/*!
\patch 5124 Date 1/29/2007 by Jirka
- New: Access to option categories directly from the main menu
*/

void DisplayMain::OnButtonClicked(int idc)
{
#ifdef _XBOX
  if (!_initialInteractionProcessed)
  {
    _initialInteractionProcessed = true;
    OnInitialInteraction();
  }
#endif

#if defined _XBOX && _XBOX_VER >= 200
  _lastButtonClicked = idc;

  // lets test if user is signed in
  if (_testSignIn && idc != IDC_MAIN_PLAYER && idc != IDC_CANCEL && idc != IDC_VK_SHIFT)
  {
    // we do not want to test sign in, if Player profile button is selected, 
    // because it could lead to showing sign in UI twice
    if (!GSaveSystem.CheckSignIn(false, false))
    {
      CheckForSignIn(false);
      if (!GSaveSystem.CheckSignIn(false, false))
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_PLAY_WITHOUT_SAVES),
          IDD_MSG_XBOX_NO_SIGN_IN, false, &buttonOK, &buttonCancel
        );
        return;
      }
    }
  }
#endif

  switch (idc)
  {
    case IDC_MAIN_SINGLEPLAYER:
#if defined _XBOX && _XBOX_VER >= 200
      if (GSaveSystem.IsUserSignedIn() && _testStorageDevice && !CheckStorage())
      {
        return;
      }
#endif
      CreateChild(new DisplaySingleplayer(this));
      break;
    case IDC_MAIN_SINGLE:
#if defined _XBOX && _XBOX_VER >= 200
      if (GSaveSystem.IsUserSignedIn() && _testStorageDevice && !CheckStorage())
      {
        return;
      }
      //GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#elif _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
#endif
      // Macrovision CD Protection
      CDPCreateDisplaySingleMission(this);
      //CreateChild(new DisplaySingleMission(this));
      break;

    case IDC_MAIN_OPTIONS:
#if defined _XBOX && _XBOX_VER >= 200
      // TODOXSAVES - default options if no profile logged in?
      CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), false));
#else
# if _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
# endif
      bool IsXboxUI();
      if (IsXboxUI())
        CreateChild(new DisplayEditProfile(this, Glob.header.GetPlayerName(), false));
      else
        CreateChild(new DisplayOptions(this, true, true));
#endif
      break;

    case IDC_OPTIONS_VIDEO:
#if _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
#endif
      CreateChild(new DisplayOptionsVideo(this, true));
      break;

    case IDC_OPTIONS_AUDIO:
#if _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
#endif
      CreateChild(new DisplayOptionsAudio(this, true));
      break;

#if !defined _XBOX && defined(_WIN32)
    case IDC_OPTIONS_CONFIGURE:
# if _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
# endif
      CreateChild(new DisplayConfigure(this, true));
      break;
#endif

    case IDC_OPTIONS_GAMEOPTIONS:
#if _GAMES_FOR_WINDOWS 
      if (!CheckForSignIn(false)) break;
#endif
      CreateChild(new DisplayGameOptions(this, true));//CreateChild(new DisplayDifficulty(this, true));
      break;

#if !defined(_XBOX) && defined(_WIN32)
    case IDC_MAIN_CUSTOM:
# if _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
# endif
      CreateChild(new DisplayCustomArcade(this));
      break;
#endif

#if _ENABLE_EDITOR
    case IDC_MAIN_EDITOR:
# if _GAMES_FOR_WINDOWS
      if (!CheckForSignIn(false)) break;
# endif
#ifdef _WIN32
      CreateChild(new DisplaySelectIsland(this));
#endif
      break;
#endif
#if _GAMES_FOR_WINDOWS || defined _XBOX
    case IDC_MAIN_ACHIEVEMENTS:
      {
        int user = GSaveSystem.GetUserIndex();
#ifdef _XBOX
        if (GSaveSystem.IsUserSignedIn()) XShowAchievementsUI(user);
#else
        if (user >= 0) XShowAchievementsUI(user);
#endif
      }
      break;
#endif
    case IDC_MAIN_MULTIPLAYER:
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
#ifdef _XBOX
#else
      if (!CheckForSignIn(true)) 
        break;
#endif

      GetNetworkManager().Init();
      CreateChild(new DisplayMultiplayerType(this));
#else
      bool IsXboxUI();
      if (IsXboxUI())
      {
        GetNetworkManager().Init();
        CreateChild(new DisplayMultiplayerType(this));
      }
      else
        CreateChild(new DisplayMultiplayer(this));
#endif
      break;
    case IDC_MAIN_PLAY:
      {  
        bool processed = false;
        // Load connect info from user profile
        GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
        ParamFile cfg;
        bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
        if (ParseUserParams(cfg, &globals))
        {
          ConstParamEntryPtr entry = cfg.FindEntry("lastMPServer");
          if (entry) 
          {
            RString serverName;
            ConstParamEntryPtr entryName = cfg.FindEntry("lastMPServerName");
            if (entryName) serverName = *entryName;

            extern const int DefaultNetworkPort;
            void __cdecl CDPCreateClient(RString ip, int port, RString password, RString serverName);
            CDPCreateClient(*entry, DefaultNetworkPort, "", serverName);
  
            // reset lastMPServer (when successful, it should set it otherwise)
            GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
            ParamFile cfg;
            bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
            void SaveUserParams(ParamFile &cfg);
            if (ParseUserParams(cfg, &globals))
            {
              cfg.Delete("lastMPServer");
              cfg.Delete("lastMPServerName");
              SaveUserParams(cfg);
            }
            processed = true;
          }
        }
        if (!processed)
          CreateChild(new DisplayMultiplayer(this));
      }
      break;

    case IDC_MAIN_AAR:
  #if _VBS2_LITE
      GWorld->CreateWarningMessage(LocalizeString("STR_DISP_VBS2_MAIN_AAR_NA"));
  #endif

#ifndef _XBOX
    case IDC_MAIN_QUIT:
      Exit(IDC_MAIN_QUIT);
      break;
#endif

    case IDC_MAIN_PLAYER:
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
      CheckForSignIn(false, true);
#elif defined _XBOX
#else
      // Before switching to a new profile, we need to save the old one
      {
        GameDataNamespace globals(NULL, RString(), false); // save - no global namespace

        ParamFile cfg;
        ParseUserParams(cfg, Glob.header.GetPlayerName(), &globals);
        GEngine->SaveConfig(cfg,GetConfigSection());
        GScene->SaveConfig(cfg);
        GSoundsys->SaveConfig(cfg);
        SaveUserParams(cfg);
      }
      
      CreateChild(new DisplayLogin(this));
#endif
      break;
	case IDC_MAIN_CHARACTER:
	  //CreateChild(new DisplayCharacter(this));
	  CreateChild(new DisplayCharacterDefault(this, true));	//--- added temporarily until character review enabled
	  break;

#ifndef _XBOX
    case IDC_MAIN_MOD_LAUNCHER:
#define ENABLE_MOD_LAUNCHER 1
#if ENABLE_MOD_LAUNCHER
      CreateChild(new DisplayModLauncher(this));
#endif
      break;
#endif

#ifndef _XBOX
    case IDC_MAIN_CREDITS:
      CreateChild(new DisplayCredits(this));
      break;
#endif

    default:
      // no escape - only QUIT
      break;
  }
  if (_child)
  {
    // Sign in handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
    GetNetworkManager().SilentSignInFinish();
#endif
    EnableVideo(false);
  }
  
  IControl *videoControl = GetCtrl(IDC_MAIN_VIDEOCONTROL);
  if (videoControl && videoControl->getDecoder() && videoControl->getDecoder()->isPlaying()  )
  {
    videoControl->getDecoder()->pause(false);
}
}

#ifdef _XBOX
void DisplayMain::OnInitialInteraction()
{
  // Sign in handled by Xbox Guide on Xbox 360
#if _XBOX_VER < 200
  if (!Glob.demo && !GetNetworkManager().IsSignedIn())
    GetNetworkManager().SilentSignInStart();
#endif
}
#endif

void DisplayMain::OnDraw(EntityAI *vehicle, float alpha)
{
#ifdef _XBOX
  #if 0 // we currently have no time left and it is unlikely we will have any
  if (!_gameInitialized)
  {
#if _ENABLE_REPORT && _ENABLE_PERFLOG
    int GetReadWaitTime();
    LogF("*** game initialized %d ms (%d ms waiting for i/o)", GlobalTickCount(), GetReadWaitTime());
#endif

    Ref<ProgressHandle> p = ProgressStart(LocalizeString(IDS_LOAD_INTRO), "%s", true);
    while (GlobalTickCount() < 28000)
    {
      int requests = 10;
      if (!GFileServer->CopyFromDVD(requests)) break;

#if _ENABLE_REPORT && _ENABLE_PERFLOG
      if (requests > 0)
        LogF("*** copy from DVD %d ms (%d ms waiting for i/o) - %d requests added", GlobalTickCount(), GetReadWaitTime(), requests);
#endif

      size_t systemFreeRequired = 256*1024;
      size_t mainHeapFreeRequired = 1024*1024;
      float maxIterTime = 0.02f;

      FreeOnDemandGarbageCollect(mainHeapFreeRequired,systemFreeRequired);
      GTimeManager.Frame(maxIterTime);
      GFileServerUpdate();
      ProgressRefresh();
    }

    _lastChange = GetTickCount();
    if (_child == NULL) EnableVideo(true);

    ProgressFinish(p);
  }
  #endif

  if (_decoder) GEngine->VideoFrame(_decoder);

  if (_playing >= 0)
  {
    if (!_video[_playing].showMenu) return;

    DWORD time = GetTickCount();
    float t1 = 0.001 * (_nextChange - time);
    saturateMax(t1, 0);
    const float fade = 2;
    if (t1 < fade) alpha *= (1.0f / fade) * t1;
    else
    {
      float t2 = 0.001 * (time - _lastChange);
      saturateMax(t2, 0);
      if (t2 < fade) alpha *= (1.0f / fade) * t2;
    }
   
    saturate(alpha, 0, 1);
  }

#endif
  
  IControl *textCtrl = GetCtrl(IDC_MAIN_PLAYER_NAME);
  ITextContainer *text = GetTextContainer(textCtrl);
  #if _ENABLE_CHEATS
    if (textCtrl)
    {
      float offsetX = 0;
      float offsetY = 0;
      if (GInput.GetKeyToDo(DIK_W,true,false)) offsetY -= 1;
      if (GInput.GetKeyToDo(DIK_S,true,false)) offsetY += 1;
      if (GInput.GetKeyToDo(DIK_A,true,false)) offsetX -= 1;
      if (GInput.GetKeyToDo(DIK_D,true,false)) offsetX += 1;
      float scaleX = 1.0/400; // 2 pixel steps in 800x600
      float scaleY = 1.0/300;
      float scale = 1;
      if (GInput.GetKey(DIK_LSHIFT,false)) scale *= 0.5f;
      if (GInput.GetKey(DIK_LCONTROL,false)) scale *= 0.1f;
      textCtrl->Move(offsetX*scaleX*scale,offsetY*scaleY*scale);
    }
  #endif
#if _VBS2
  if (text) text->SetText(Glob.header.GetPlayerName());
#else
  RString name = Glob.header.GetPlayerName();
# if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    if (name.GetLength() == 0) name = LocalizeString(IDS_SILENT_SIGN_IN_NONE);
# endif
  if (text && !textCtrl->IsFocused()) 
  {
    name.RTrim();
    if (!name.GetLength()) name=GetLoginName();
    text->SetText(name);
  }
#endif

  // Sign in status shown in IDS_MAIN_PLAYER for Xbox 360
#if defined _XBOX  && _XBOX_VER < 200 && _ENABLE_MP
  CStructuredText *ctrl = GetStructuredText(GetCtrl(IDC_MAIN_SIGN_IN_STATUS));
  if (ctrl) SetSignInStatus(ctrl);
#endif

  Display::OnDraw(vehicle,alpha);

  _gameInitialized = true;

  #if _ENABLE_REPORT && _ENABLE_PERFLOG
  static bool once = true;
  if (once)
  {
    void FlushLogFile();
    once = false;
    int GetReadWaitTime();
    LogF("**** init done %d ms (%d ms waiting for i/o)", GlobalTickCount(), GetReadWaitTime());
    // we already loaded all files needed to init - save log
    GFileServer->SavePreloadLog();
    GFileServer->SaveDVDLog();
    FlushLogFile();

    // an opportinity to perorm any test (performace ...) after the game is fully initalized
    // or after a mission run using -init has completed
    void Test();
    Test();
#if 0
    GFileServer->WaitForRequestsDone();
    LogF("**** buffers empty after %d ms (%d ms waiting for i/o)", GlobalTickCount(), GetReadWaitTime());
#endif

  }
  #endif
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)


void DisplayMain::OnReturnToMainMenuRequest()
{
  switch (GSaveSystem.GetReturnToMainMenuRequest())
  {
  case SaveSystem::RRSignInChanged:
    // sign in changed

    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SIGNED_OUT));
    // apply profile to laod defaults
    ApplyUserProfile();
    break;
  case SaveSystem::RRLiveDisconnected:
    // localization:"You've been returned to the main menu because the connection to Xbox LIVE has been lost."
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LIVE_DISCONNECTED));
   break;
  }

  // set request as handled
  GSaveSystem.ResetReturnToMainMenuRequest();

}

void DisplayMain::OnInvited()
{
  GetNetworkManager().Init();
  CreateChild(new DisplayMultiplayerType(this));
}


#endif

#if _VBS3
  extern RString ClientIP;
  int GetNetworkPort();
  RString GetNetworkPassword();
#endif

#if _ENABLE_REPORT && _ENABLE_PERFLOG
extern LogFile *GLogFile;
#endif

void DisplayMain::OnSimulate(EntityAI *vehicle)
{
#if _VBS3
  // For some reason we need to timeout, when we've been forced
  // or diconnected from the server.
  if(ClientIP.GetLength() > 0)
  {
    if(!GetNetworkManager().IsServer())
      if(!GetNetworkManager().IsClient())
      {
        if(_reConnectDelay==0 )
          _reConnectDelay = ::GlobalTickCount() + 500;

        if(_reConnectDelay < ::GlobalTickCount())
        {
          _reConnectDelay = 0;
          void __cdecl CDPCreateClient(RString ip, int port, RString password);
          CDPCreateClient(ClientIP, GetNetworkPort(), GetNetworkPassword());
        }
      }
  }
#endif
#if _ENABLE_HOST
  if (GInput.CheatActivated() == CheatHost)
  {
    CDP_DECL void __cdecl CDPCreateServer();
    CDPCreateServer();
    GInput.CheatServed();
  }
#endif
#if _ENABLE_STEAM
  if (!GInput.changeServer.IsEmpty())
  {
    if (!GetNetworkManager().IsServer() && !GetNetworkManager().IsClient())
    {
      void __cdecl CDPCreateClient(RString ip, int port, RString password, RString serverName);
      extern const int DefaultNetworkPort;
      CDPCreateClient(GInput.changeServer, DefaultNetworkPort, GInput.changeServerPassword, "");
    }
    GInput.changeServer=RString();
  }
#endif

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // handle profile change
  if (GSaveSystem.IsCheckInChanged())
  {
    ApplyUserProfile();
    GSaveSystem.CheckInHandled();
  }
#endif

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  if ( _testMissions.Active() || _testMissions.Failed() )
  {
    if (_testMissions.IsMission())
    {
      DoStartMission(_testMissions.GetCampaign(), _testMissions.GetMission(), true);
      return;
    }
    else
    {
      extern int AutoTestExitCode;
      if (_testMissions.testMissions.Size()>0)
        AutoTestExitCode = _testMissions.failedCount;
      else
        AutoTestExitCode = 10000;
#ifdef _XBOX
#if _ENABLE_REPORT && _ENABLE_PERFLOG
      // make sure we have the whole content of debug.log
      if (GLogFile) GLogFile->Flush();
#endif
      char filename[MAX_PATH];
      if (GetLocalSettingsDir(filename))
      {
        TerminateBy(filename, '\\');
        strcat(filename, "autotestDone.txt");

        QOFStream out;
        out.open(filename);
        out << Format("%d missions failed", _testMissions.failedCount);
        out.close();
      }
#else
      Exit(IDC_MAIN_QUIT);
#endif
    }
  }
#endif
  if (_wantedMission.GetLength() > 0)
  {
    DoStartMission("", _wantedMission, _wantedMissionSkipBriefing);
    _wantedMission = RString();
    return;
  }


#ifdef _XBOX
  if (_playing >= 0)
  {
    if (GetTickCount() >= _nextChange) NextVideo(false);
    /// buttons which perform silent sign-in
    static const int buttons[] = {
      XBOX_A, XBOX_B, XBOX_X, XBOX_Y,
      XBOX_Start, XBOX_Back,
      XBOX_LeftBumper, XBOX_RightBumper
    };
    bool pressed = false;
    for (int i=0; i<lenof(buttons); i++)
    {
      if (GInput.GetXInputButtonToDo(buttons[i], false))
      {
        pressed = true;
        break;
      }
    }
    if (pressed)
    {
      if (!_initialInteractionProcessed)
      {
        _initialInteractionProcessed = true;
        OnInitialInteraction();
      }
    }
          
    if (IsMenuEnabled())
    {
      /// button which avoid entering attract mode in addition to buttons above
      static const int buttonsAvoidAttractMode[] = {
        XBOX_Down,XBOX_Up,XBOX_Right,XBOX_Left,
        XBOX_LeftThumbYUp,XBOX_LeftThumbYDown,XBOX_LeftThumbXRight,XBOX_LeftThumbXLeft
      };
      bool avoidAttract = false;
      for (int i=0; i<lenof(buttonsAvoidAttractMode); i++)
      {
        if (GInput.GetXInputButtonToDo(buttonsAvoidAttractMode[i], false))
        {
          avoidAttract = true;
          break;
        }
      }
      if (pressed || avoidAttract)
      {
        // restart timeout
        _nextChange = GetTickCount() + toInt(1000 * _video[_playing].time);
      }
    }
    else
    {
      if
      (
        GInput.GetXInputButtonToDo(XBOX_A) ||
        GInput.GetXInputButtonToDo(XBOX_Start)
      )
      {
        NextVideo(true); // attract mode interrupted
      }
      if (GInput.GetXInputButton(XBOX_NoController, false) > 0) OnControllerRemoved();
      return;
    }
  }
#endif

  // Pending invitations are handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
  CStructuredText *ctrl = GetStructuredText(GetCtrl(IDC_MAIN_MP_PROFILES));
  if (ctrl)
  {
    bool request = GetNetworkManager().IsPendingRequest();
    bool invitation = GetNetworkManager().IsPendingInvitation();

    RString text = LocalizeString(IDS_DISP_MAIN_LIVE);
    if (invitation)
      text = text + RString("<img image='\\xmisc\\gameinvitereceived.paa'/>");
    if (request)
      text = text + RString("<img image='\\xmisc\\friendinvitereceived.paa'/>");
    ctrl->SetStructuredText(text);
  }
#endif

  Display::OnSimulate(vehicle);
}

void DisplayMain::OnControllerRemoved()
{
#ifdef _XBOX
  if (!_initialInteractionProcessed) return;
#endif
  Display::OnControllerRemoved();
}

static void RemoveBadCharacters(RString &name)
{
  char *data = name.MutableData();
  int ixTo = 0;
  for (int ixFrom=0, len=name.GetLength(); ixFrom<len; ixFrom++)
  {
    //use only allowed characters
    if ( isalnum(data[ixFrom]) || 
        (data[ixFrom]==' ' && (ixTo>0) ) ||
        strchr("~!@#$%^&*()+-=<>/?.:[]{}|\\", data[ixFrom])!=NULL // list of possible other chars allowed
       )
    {
      data[ixTo++]=data[ixFrom];
    }
  }
  if (name.GetLength())
  {
    data[ixTo]=0; //null terminate
  }
  // note no RTrim used, as we want player to be able to write "Name Surname", trailing space is removed when control focus is lost
}

void DisplayMain::OnEditChanged(int idc)
{
  switch (idc)
  {
  case IDC_MAIN_PLAYER_NAME:
    {
      CEditContainer *edit = GetEditContainer(GetCtrl(idc));
      if (!edit) return;
      RString name = edit->GetText();
      RemoveBadCharacters(name);
      edit->SetText(name);
      // store the into user profile
      Glob.header.SetPlayerName(name);
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
      void SaveUserParams(ParamFile &cfg);
      if (ParseUserParams(cfg, &globals))
      {
        cfg.Add("playerName", name);
        SaveUserParams(cfg);
      }
    }
    break;
  }
  Display::OnEditChanged(idc);
}

DisplayEditProfile::DisplayEditProfile(ControlsContainer *parent, RString name, bool inGame)
: Display(parent)
{
  _name = name;

  // default values
  _face = "Default";
  _glasses = "None";

  _speakerType = GetFirstSpeakerType();
  _pitch = 1.0;
  _mask = (Pars >> "CfgVoiceMask").GetEntry(0).GetName();

  RString GetDefaultControllerScheme();
  _scheme.basicSettings = GetDefaultControllerScheme();
  ControllerSensitivity GetDefaultControllerSensitivity();
  _scheme.sensitivity = GetDefaultControllerSensitivity();
#ifdef _XBOX
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
#else
  _scheme.vibrations = true;
#endif

  _musicVolume = GSoundsys->GetDefaultCDVolume();
  _voiceVolume = GSoundsys->GetDefaultSpeechVolume();
  _effectsVolume = GSoundsys->GetDefaultWaveVolume();

  _bright = GEngine->GetDefaultBrightness();
  _gamma = GEngine->GetDefaultGamma();

  _titles = true;
  _radio = true;
  _inGame = inGame;

  LoadConfig();
  ApplyProfile();

  if (inGame)
  { 
    Load("RscDisplayEditProfileInGame");
    _enableSimulation = false;
  }
  else
    Load("RscDisplayEditProfile");

  if (_head)
  {
    _head->SetFace(_face);
    _head->SetGlasses(_glasses);
  }
  if (_sumName) _sumName->SetText(name);
  if (_sumController)
  {
    RString controller = Pars >> "ControllerSchemes" >> _scheme.basicSettings >> "name";
    _sumController->SetStructuredText(controller);
  }
  if (_sumAccount)
#ifdef _XBOX
    _sumAccount->SetStructuredText(_account);
#else
    _sumAccount->SetStructuredText(RString());
#endif

#if defined _XBOX && _XBOX_VER >= 200
  if (inGame && (GetMissionParameters().GetLength() > 0))
  {
    // we have to disable storage device button if we are ingame and playing user mission
    IControl *ctrl = GetCtrl(IDC_PROFILE_STORAGE_DEVICE);
    if (ctrl)
    {
      ctrl->EnableCtrl(!inGame || GetMissionParameters().GetLength() == 0);
    }
  }
#endif //#if defined _XBOX && _XBOX_VER >= 200
}

void DisplayEditProfile::ApplyProfile()
{
  // video
  GEngine->SetBrightness(_bright);
  GEngine->SetGamma(_gamma);
  Glob.config.showTitles = _titles;
  GChatList.Enable(_radio);

  // audio
  GSoundsys->SetCDVolume(_musicVolume);
  GSoundsys->SetSpeechVolume(_voiceVolume);
  GSoundsys->SetWaveVolume(_effectsVolume);

}

Control *DisplayEditProfile::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_PROFILE_SUM_NAME:
    _sumName = GetTextContainer(ctrl);
    break;
  case IDC_PROFILE_SUM_CONTROLLER:
    _sumController = GetStructuredText(ctrl);
    break;
  // Sign in handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
  case IDC_PROFILE_SUM_LIVE:
    _sumAccount = GetStructuredText(ctrl);
    break;
  case IDC_PROFILE_SIGN_IN_STATUS:
    _signInStatus = GetStructuredText(ctrl);
    if (_signInStatus) SetSignInStatus(_signInStatus);
    break;
#endif
  }
  return ctrl;
}

ControlObject *DisplayEditProfile::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_PROFILE_HEAD)
  {
    _head = new CHead(this, idc, cls);
    _head->EnableCtrl(false);
    return _head;
  }
  else
    return Display::OnCreateObject(type, idc, cls);
}

void DisplayEditProfile::OnButtonClicked(int idc)
{
  switch (idc)
  {
#if defined _XBOX && _XBOX_VER >= 200
  case IDC_PROFILE_STORAGE_DEVICE:
    {
      if (!GSaveSystem.IsUserSignedIn())
      {
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_NOT_SIGNED_IN));
        return;
      }
      GSaveSystem.SelectDevice(CHECK_SAVE_BYTES , true);
    }
    break;
#endif //#if defined _XBOX && _XBOX_VER >= 200

  case IDC_PROFILE_FACE:
    {
      CreateChild(new DisplayProfileFace(this, _enableSimulation, _face, _glasses, _name));
      break;
    }
    break;
  case IDC_PROFILE_VOICE:
    {
      CreateChild(new DisplayProfileVoice(this, _enableSimulation, _speakerType, _pitch, _mask, Glob.header.voiceThroughSpeakers, "RscDisplayProfileVoice", _face, _glasses));
      break;
    }
    break;
  case IDC_PROFILE_VOICE_MASK:
    {
      CreateChild(new DisplayProfileVoice(this, _enableSimulation, _speakerType, _pitch, _mask, Glob.header.voiceThroughSpeakers, "RscDisplayVoiceMask", _face, _glasses));
      break;
    }
    break;
  case IDC_PROFILE_CONTROLLER:
    {
      CreateChild(new DisplayProfileController(this, _enableSimulation, _scheme));
      break;
    }
    break;
  case IDC_PROFILE_AUDIO:
    {
      CreateChild(new DisplayProfileAudio(this, _enableSimulation, _musicVolume, _voiceVolume, _effectsVolume));
      break;
    }
    break;
  case IDC_PROFILE_VIDEO:
    {
      CreateChild(new DisplayProfileVideo(this, _enableSimulation, _bright, _gamma, _titles, _radio));
      break;
    }
    break;
  case IDC_PROFILE_CREDITS:
#if _ENABLE_AI
    if (!_inGame) // disable in-game
    {
#ifdef _XBOX
      IControl::PlaySound(_soundYProcessed);
#endif

      RString cutscene = Pars >> "CfgCredits" >> "cutscene";
      char buffer[256];
      strcpy(buffer, cutscene);

      // parse mission name

      // mission
      char *world = strchr(buffer, '.');
      Assert(world);
      *world = 0;
      world++;

      SetBaseDirectory(false, "");
      SetMission(world, buffer, AnimsDir);

      LaunchIntro(this, true);
    }
#endif
    break;
  case IDC_OK:
  case IDC_CANCEL:
    SaveConfig(_name);
    Exit(idc);
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayEditProfile::OnSimulate(EntityAI *vehicle)
{
  if (_head) _head->Simulate();
  // Sign in handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
  if (_signInStatus)
  {
    SetSignInStatus(_signInStatus);
  }
#endif
  Display::OnSimulate(vehicle);
}

void DisplayEditProfile::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_PROFILE_FACE:
    if (exit == IDC_OK)
    {
      DisplayProfileFace *disp = dynamic_cast<DisplayProfileFace *>(_child.GetRef());
      Assert(disp);
      _face = disp->GetFace();
      _glasses = disp->GetGlasses();
      SaveConfig(_name);
      if (_head)
      {
        _head->SetFace(_face);
        _head->SetGlasses(_glasses);
      }
    }
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_PROFILE_VOICE:
    if (exit == IDC_OK)
    {
      DisplayProfileVoice *disp = dynamic_cast<DisplayProfileVoice *>(_child.GetRef());
      Assert(disp);
      _speakerType = disp->GetSpeakerType();
      _pitch = disp->GetPitch();
      _mask = disp->GetMask();
      //Glob.header.voiceThroughSpeakers = disp->IsThroughSpeakers();
      // update mask right now
      GetNetworkManager().ChangeVoiceMask(_mask);

      SaveConfig(_name);
    }
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_PROFILE_CONTROLLER:
    if (exit == IDC_OK)
    {
      DisplayProfileController *disp = dynamic_cast<DisplayProfileController *>(_child.GetRef());
      Assert(disp);
      _scheme = disp->GetScheme();
      if (_sumController)
      {
        RString controller = Pars >> "ControllerSchemes" >> _scheme.basicSettings >> "name";
        _sumController->SetStructuredText(controller);
      }
      SaveConfig(_name);
    }
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_PROFILE_AUDIO:
    if (exit == IDC_OK)
    {
      DisplayProfileAudio *disp = dynamic_cast<DisplayProfileAudio *>(_child.GetRef());
      Assert(disp);
      _musicVolume = disp->GetMusicVolume();
      _voiceVolume = disp->GetVoiceVolume();
      _effectsVolume = disp->GetEffectsVolume();
      SaveConfig(_name);
    }
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_PROFILE_VIDEO:
    if (exit == IDC_OK)
    {
      DisplayProfileVideo *disp = dynamic_cast<DisplayProfileVideo *>(_child.GetRef());
      Assert(disp);
      _bright = disp->GetBright();
      _gamma = disp->GetGamma();
      _titles = disp->GetTitles();
      _radio = disp->GetRadio();
      SaveConfig(_name);
    }
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_PROFILE_LIVE:
#if defined _XBOX && _XBOX_VER < 200 && _ENABLE_MP
    if (exit == IDC_OK)
    {
      DisplayProfileLive *disp = dynamic_cast<DisplayProfileLive *>(_child.GetRef());
      Assert(disp);
      _account = disp->GetAccount();
      SaveConfig(_name);
      if (_sumAccount) _sumAccount->SetText(_account);
    }
#endif
    Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_INTRO:
    GWorld->DestroyMap(IDC_OK);
    void ShowCinemaBorder(bool show);
    ShowCinemaBorder(true);
    StartRandomCutscene(Glob.header.worldname);
    Display::OnChildDestroyed(idd, exit);
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

void DisplayEditProfile::LoadConfig()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace

  ParamFile cfg;
#if _GAMES_FOR_WINDOWS  || (defined _XBOX && _XBOX_VER >= 200)
  if (!ParseUserParams(cfg, &globals)) return;
#else
  if (!ParseUserParams(cfg, _name, &globals)) return;
#endif

  // load values
  ConstParamEntryPtr entry;

  ConstParamEntryPtr identity = cfg.FindEntry("Identity");
  if (identity)
  {
    // face
    entry = identity->FindEntry("face");
    if (entry) _face = *entry;
    entry = identity->FindEntry("glasses");
    if (entry) _glasses = *entry;

    // voice
    entry = identity->FindEntry("speaker");
    if (entry) _speakerType = *entry;
    entry = identity->FindEntry("pitch");
    if (entry) _pitch = *entry;
    entry = identity->FindEntry("mask");
    if (entry) _mask = *entry;
  }

  // controller
  entry = cfg.FindEntry("controller");
  if (entry) _scheme.basicSettings = *entry;
  entry = cfg.FindEntry("sensitivity");
  if (entry)
  {
    RString sensitivity = *entry;
    _scheme.sensitivity = GetEnumValue<ControllerSensitivity>(cc_cast(sensitivity));
  }
#ifdef _XBOX
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
#else
  entry = cfg.FindEntry("vibrations");
  if (entry) _scheme.vibrations = *entry;
#endif
  entry = cfg.FindEntry("ControllerSchemes");
  if (entry)
  {
    int n = entry->GetEntryCount();
    _scheme.settings.Realloc(n);
    _scheme.settings.Resize(n);
    for (int i=0; i<n; i++)
    {
      ParamEntryVal cfg = entry->GetEntry(i);
      _scheme.settings[i].name = cfg.GetName();
      _scheme.settings[i].yAxis = cfg[1];
    }
  }
  else _scheme.settings.Clear();
  
  // audio
  entry = cfg.FindEntry("volumeCD");
  if (entry) _musicVolume = *entry;
  entry = cfg.FindEntry("volumeSpeech");
  if (entry) _voiceVolume = *entry;
  entry = cfg.FindEntry("volumeFX");
  if (entry) _effectsVolume = *entry;
  
  // video
  entry = cfg.FindEntry("brightness");
  if (entry) _bright = *entry;
  entry = cfg.FindEntry("gamma");
  if (entry) _gamma = *entry;
  entry = cfg.FindEntry("showTitles");
  if (entry) _titles = *entry;
  entry = cfg.FindEntry("showRadio");
  if (entry) _radio = *entry;
  
#ifdef _XBOX
  // Xbox live account
  entry = cfg.FindEntry("userAccount");
  if (entry) _account = *entry;
#endif
}

void FlushReadHandles(const char *dir)
{
#ifdef _WIN32
  if (!dir || *dir == 0) return;

  char buffer[256];
  sprintf(buffer, "%s\\*.*", dir);

  _finddata_t info;
  long h = X_findfirst(buffer, &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        // FIX - do not delete only current and parent directory
        if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
        {
          sprintf(buffer, "%s\\%s", dir, info.name);
          FlushReadHandles(buffer);
        }
      }
      else
      {
        sprintf(buffer, "%s\\%s", dir, info.name);
        GFileServerFunctions->FlushReadHandle(buffer);
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
#endif
}

void DisplayEditProfile::SaveConfig(RString newName)
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace

  ParamFile cfg;

#ifdef _XBOX
# if _XBOX_VER >= 200
    ParseUserParams(cfg, &globals);
# else
    RString filename = GetUserParams(_name, false);
    if (filename.GetLength() == 0) return;

    if (newName != _name)
    {
      RenameUserProfile(_name, newName);
      if (Glob.header.GetPlayerName() == _name)
      {
        Glob.header.SetPlayerName(newName);
        SaveLastPlayer();
      }
      _name = newName;
    }
    UpdateUserProfile();
    if (QIFileFunctions::FileExists(filename) && !cfg.ParseSigned(filename, NULL, NULL, &globals))
    {
      const char *ptr = strrchr(filename, '\\');
      ptr++;
      RString dir = filename.Substring(0, ptr - (const char *)filename);

      ReportUserFileError(dir, ptr, RString());
    }
    filename = GetUserParams(_name, true);
# endif // _XBOX_VER >= 200
#elif _GAMES_FOR_WINDOWS
  ParseUserParams(cfg);
#else
  RString filename;
  if (newName != _name)
  {
    RString srcDir;
    RString dstDir;
    srcDir = GetUserRootDir(_name);
    dstDir = GetUserRootDir(newName);
    filename = dstDir + RString("\\") + EncodeFileName(newName) + RString(".") + GetExtensionProfile();
    if (QIFileFunctions::DirectoryExists(srcDir))
    {
      FlushReadHandles(srcDir);
      int ret = rename(srcDir, dstDir);
      if (ret != 0)
      {
        RptF("Cannot rename file due to %d", errno);
      }
    }
    else
      CreatePath(filename);
    if (Glob.header.GetPlayerName() == _name)
    {
      Glob.header.SetPlayerName(newName);
      SaveLastPlayer();
    }
    _name = newName;
  }
  else
  {
    filename = GetUserParams(_name, true);
    CreatePath(filename);
  }
  UpdateUserProfile();

  cfg.Parse(filename, NULL, NULL, &globals);
#endif

  // enable reading of profile by ParamArchive
  cfg.Add("version", UserInfoVersion);

  // save values

  ParamClassPtr identity = cfg.AddClass("Identity");

  // face
  identity->Add("face", _face);
  identity->Add("glasses", _glasses);

  // voice
  identity->Add("speaker", _speakerType);
  identity->Add("pitch", _pitch);
  identity->Add("mask", _mask);

  // controller
  cfg.Add("controller", _scheme.basicSettings);
  RString sensitivity = FindEnumName(_scheme.sensitivity);
  cfg.Add("sensitivity", sensitivity);
#ifndef _XBOX
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
  cfg.Add("vibrations", _scheme.vibrations);
#endif
  ParamClassPtr schemes = cfg.AddClass("ControllerSchemes");
  for (int i=0; i<_scheme.settings.Size(); i++)
  {
    ParamEntryPtr array = schemes->AddArray(_scheme.settings[i].name);
    array->Clear();
    array->AddValue(RString());
    array->AddValue(_scheme.settings[i].yAxis);
  }

  // audio
  cfg.Add("volumeCD", _musicVolume);
  cfg.Add("volumeSpeech", _voiceVolume);
  cfg.Add("volumeFX", _effectsVolume);

  // video
  cfg.Add("brightness", _bright);
  cfg.Add("gamma", _gamma);
  cfg.Add("showTitles", _titles);
  cfg.Add("showRadio", _radio);

#ifdef _XBOX
# if _XBOX_VER >= 200
  SaveUserParams(cfg);
# else
  // Xbox live account
  cfg.Add("userAccount", _account);
  cfg.SaveSigned(filename);
# endif
#elif _GAMES_FOR_WINDOWS
  SaveUserParams(cfg);
#else
  cfg.Save(filename);
#endif
}

bool CustomFaceExist(RString name)
{
#ifndef _XBOX
  RString dir = GetUserDirectory(name);
  RString filename = dir + RString("face.paa");
  if (QIFileFunctions::FileExists(filename)) return true;
  filename = dir + RString("face.jpg");
  if (QIFileFunctions::FileExists(filename)) return true;
#endif
  
  return false;
}

DisplayProfileFace::DisplayProfileFace(ControlsContainer *parent, bool enableSimulation, RString face, RString glasses, RString player)
  : Display(parent)
{
  _oldFace = face;
  _oldGlasses = glasses;

  // initialize _faceType for case no head preview is present
  ParamEntryVal cls = Pars >> "CfgFaces";
  for (int i=0; i<cls.GetEntryCount(); i++)
  {
    ParamEntryVal entry = cls.GetEntry(i);
    if (entry.IsClass())
    {
      _faceType = entry.GetName();
      break;
    }
  }

  Load("RscDisplayProfileFace");
  _enableSimulation = _enableSimulation && enableSimulation;

  if (_face)
  {
    ParamEntryVal list = Pars >> "CfgFaces" >> _faceType;
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      if (entry.FindEntry("disabled")) continue;
      RString name = entry.GetName();
#ifndef _XBOX
      if (stricmp(name, "custom") == 0 && !CustomFaceExist(player)) continue;
#else
      if (stricmp(name, "custom") == 0) continue;
#endif
      int index = _face->AddString(entry >> "name");
      _face->SetData(index, name);
    }

    _face->SortItems();

    int sel = 0;
    for (int i=0; i<_face->GetSize(); i++)
    {
      if (_face->GetData(i) == face)
      {
        sel = i;
        break;
      }
    }
    _face->SetCurSel(sel);
  }
  if (_glasses)
  {
    int sel = 0;
    ParamEntryVal list = Pars >> "CfgGlasses";
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      int scope = entry >> "scope";
      if (scope!=2) continue;
      RString name = entry.GetName();
      int index = _glasses->AddString(entry >> "name");
      _glasses->SetData(index, name);
      if (name == glasses) sel = index;
    }
    _glasses->SetCurSel(sel);
  }
  if (_head)
  {
    _head->SetFace(face);
    _head->SetGlasses(glasses);
  }
}

Control *DisplayProfileFace::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_PROF_FACE_FACE:
    _face = GetListBoxContainer(ctrl);
    break;
  case IDC_PROF_FACE_GLASSES:
    _glasses = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

ControlObject *DisplayProfileFace::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_PROF_FACE_HEAD)
  {
    _head = new CHead(this, idc, cls);
    _head->EnableCtrl(false);
    _faceType = _head->GetFaceType();
    return _head;
  }
  else
    return Display::OnCreateObject(type, idc, cls);
}

void DisplayProfileFace::OnButtonClicked(int idc)
{
  Display::OnButtonClicked(idc);
}

void DisplayProfileFace::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_FACE_FACE:
    if (_head) _head->SetFace(GetFace());
    break;
  case IDC_PROF_FACE_GLASSES:
    if (_head) _head->SetGlasses(GetGlasses());
    break;
  }
}

RString DisplayProfileFace::GetFace() const
{
  if (_face)
  {
    int sel = _face->GetCurSel();
    if (sel >= 0) return _face->GetData(sel);
  }
  return _oldFace;
}

RString DisplayProfileFace::GetGlasses() const
{
  if (_glasses)
  {
    int sel = _glasses->GetCurSel();
    if (sel >= 0) return _glasses->GetData(sel);
  }
  return _oldGlasses;
}

void DisplayProfileFace::OnSimulate(EntityAI *vehicle)
{
  if (_head) _head->Simulate();
  Display::OnSimulate(vehicle);
}

DisplayProfileVoice::DisplayProfileVoice
(
  ControlsContainer *parent, bool enableSimulation,
  RString speakerType, float pitch, RString mask, bool throughSpeakers,
  RString resource, RString face, RString glasses
)
  : Display(parent)
{
  _oldSpeakerType = speakerType;
  _oldPitch = pitch;
  _oldMask = mask;
  _oldThroughSpeakers = throughSpeakers;
  _oldEnableVoice = GetNetworkManager().IsVoice() || !GetNetworkManager().IsSignedIn();

  Load(resource);
  _enableSimulation = _enableSimulation && enableSimulation;

  if (_speakerType)
  {
    int sel = 0;
    ParamEntryVal list = Pars >> "CfgVoiceTypes";
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      if (!entry.IsClass()) continue;
      RString name = entry.GetName();

      int index = _speakerType->AddString(entry >> "name");
      _speakerType->SetData(index, name);
      if (stricmp(name, speakerType) == 0) sel = index;
    }
    _speakerType->SetCurSel(sel);
  }
  if (_pitch)
  {
    _pitch->SetRange(0.8, 1.2);
    _pitch->SetSpeed(0.02, 0.1);
    _pitch->SetThumbPos(pitch);
  }
  if (_mask)
  {
    int sel = 0;
    ParamEntryVal list = Pars >> "CfgVoiceMask";
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      RString name = entry.GetName();
      int index = _mask->AddString(entry >> "name");
      _mask->SetData(index, name);
      if (name == mask) sel = index;
    }
    _mask->SetCurSel(sel);
  }
  if (_throughSpeakers)
  {
    int index = _throughSpeakers->AddString(LocalizeString(IDS_XBOX_VOICE_COMMUNICATOR));
    _throughSpeakers->SetValue(index, 0);
    index = _throughSpeakers->AddString(LocalizeString(IDS_XBOX_VOICE_SPEAKERS));
    _throughSpeakers->SetValue(index, 1);
    _throughSpeakers->SetCurSel(throughSpeakers ? 1 : 0);
  }
  if (_head)
  {
    _head->SetFace(face);
    _head->SetGlasses(glasses);
  }
  _doPreview = true;
  _previewTime = GlobalTickCount();
}

Control *DisplayProfileVoice::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_PROF_VOICE_SPEAKER:
    _speakerType = GetListBoxContainer(ctrl);
    break;
  case IDC_PROF_VOICE_PITCH:
    _pitch = GetSliderContainer(ctrl);
    break;
  case IDC_PROF_VOICE_MASK:
    _mask = GetListBoxContainer(ctrl);
    break;
  case IDC_PROF_VOICE_THROUGH_SPEAKERS:
    _throughSpeakers = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

ControlObject *DisplayProfileVoice::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_PROF_VOICE_HEAD)
  {
    _head = new CHead(this, idc, cls);
    _head->EnableCtrl(false);
    return _head;
  }
  else
    return Display::OnCreateObject(type, idc, cls);
}

void DisplayProfileVoice::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_PROF_VOICE_DEFAULT:
    // default settings
    if (_speakerType)
    {
      RString speakerType = GetFirstSpeakerType();
      int sel = 0;
      for (int i=0; i<_speakerType->GetSize(); i++)
      {
        if (stricmp(_speakerType->GetData(i), speakerType) == 0) sel = i;
      }
      _speakerType->SetCurSel(sel);
    }
    if (_pitch) _pitch->SetThumbPos(PITCH_DEFAULT);
    if (_mask)
    {
      RString mask = (Pars >> "CfgVoiceMask").GetEntry(0).GetName();
      int sel = 0;
      for (int i=0; i<_mask->GetSize(); i++)
      {
        if (stricmp(_mask->GetData(i), mask) == 0) sel = i;
      }
      _mask->SetCurSel(sel);
    }
    if (_throughSpeakers)
    {
      bool enableVoice = GetNetworkManager().IsVoice() || !GetNetworkManager().IsSignedIn();
      bool throughSpeakers = VOICE_THROUGH_SPEAKERS_DEFAULT || !enableVoice;
      Glob.header.voiceThroughSpeakers = throughSpeakers;
      _throughSpeakers->SetCurSel(throughSpeakers ? 1 : 0);
    }
    return;
  case IDC_CANCEL:
    // when user did some active change, restore it
    // active change is: communicator state is the same, but the voiceThroughSpeakers differs
    {
      bool enableVoice = GetNetworkManager().IsVoice() || !GetNetworkManager().IsSignedIn();
      if (enableVoice==_oldEnableVoice)
      {
        Glob.header.voiceThroughSpeakers = _oldThroughSpeakers;
      }
    }
    break;
  }
  Display::OnButtonClicked(idc);
}

void DisplayProfileVoice::OnSliderPosChanged(IControl *ctrl, float pos)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_VOICE_PITCH:
    _doPreview = true;
    _previewTime = GlobalTickCount() + 200;
    break;
  }
}

void DisplayProfileVoice::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_VOICE_SPEAKER:
    _doPreview = true;
    _previewTime = GlobalTickCount() + 200;
    break;
  case IDC_PROF_VOICE_THROUGH_SPEAKERS:
    bool enableVoice = GetNetworkManager().IsVoice() || !GetNetworkManager().IsSignedIn();
    bool throughSpeakers = curSel>0 || !enableVoice;
    Glob.header.voiceThroughSpeakers = throughSpeakers;
    break;
  }
}

RString DisplayProfileVoice::GetSpeakerType() const
{
  if (_speakerType)
  {
    int sel = _speakerType->GetCurSel();
    if (sel >= 0) return _speakerType->GetData(sel);
  }
  return _oldSpeakerType;
}

float DisplayProfileVoice::GetPitch() const
{
  if (_pitch) return _pitch->GetThumbPos();
  return _oldPitch;
}

RString DisplayProfileVoice::GetMask() const
{
  if (_mask)
  {
    int sel = _mask->GetCurSel();
    if (sel >= 0) return _mask->GetData(sel);
  }
  return _oldMask;
}

bool DisplayProfileVoice::IsThroughSpeakers() const
{
  #if 0
  if (_throughSpeakers)
  {
    int sel = _throughSpeakers->GetCurSel();
    if (sel >= 0) return _throughSpeakers->GetValue(sel) != 0;
  }
  return _oldThroughSpeakers;
  #endif
  // we want this field to be really dynamic, to react to communicator insertions
  return Glob.header.voiceThroughSpeakers;
}

void DisplayProfileVoice::OnSimulate(EntityAI *vehicle)
{
  base::OnSimulate(vehicle);

  bool enableVoice = GetNetworkManager().IsVoice() || !GetNetworkManager().IsSignedIn();
  
  IControl *ctrl = GetCtrl(IDC_PROF_VOICE_THROUGH_SPEAKERS);
  if (ctrl) ctrl->EnableCtrl(enableVoice);

  if (_throughSpeakers)
  {
    _throughSpeakers->SetCurSel(Glob.header.voiceThroughSpeakers || !enableVoice);
  }
  
  if (_previewSpeech && _previewSpeech->IsTerminated()) _previewSpeech = NULL;
  if (!_previewSpeech && _doPreview && GlobalTickCount() >= _previewTime)
  {
    Preview(GetSpeakerType(), GetPitch());
    _doPreview = false;
  }
  if (_head) _head->Simulate();
}

void DisplayProfileVoice::Preview(RString speakerType, float pitch)
{
  RString word = Pars >> "CfgVoice" >> "preview";
  if (word.GetLength() == 0) return;

  // for a preview select the first speaker from the list
  ParamEntryPar cls = Pars >> "CfgVoiceTypes" >> speakerType;
  RString speaker = (cls >> "preview");
  if (speaker.IsEmpty())return;

  int index = Glob.config.singleVoice ? 1 : 0; 
  RString dir = (Pars >> "CfgVoice" >> speaker >> "directories")[index];
  RString directory;
  if (dir[0] == '\\')
    directory = (const char *)dir + 1;
  else
    directory = RString("voice\\") + dir;

  RString name = directory + word;
  if (*GetFileExt(name)==0) // when there is no extension, add default one
    name = name+RString(".wss");

  _previewSpeech = GSoundScene->OpenAndPlayOnce2D(name, 1, pitch, false);
  if (_previewSpeech)
  {
    _previewSpeech->SetKind(WaveSpeech);
    _previewSpeech->SetSticky(true);
    if (_head) _head->AttachWave(_previewSpeech, pitch);
  }
}

#include "../joystick.hpp"

DisplayProfileController::DisplayProfileController(ControlsContainer *parent, bool enableSimulation, const ControllerScheme &scheme)
: Display(parent)
{
  _current = scheme;
#ifdef _XBOX
  _type = FindEnumName(XInputDev->GetType());
#else
  _type = (Pars >> "ControllerTypes").GetEntry(0).GetName();
#endif
  // Load("RscDisplayProfileController");
  _enableSimulation = _enableSimulation && enableSimulation;

  Init();
  OnTypeChanged();

  _lastTypes = 0;
  UpdateTypes();
  RString name;
  AIBrain *unit = GWorld->FocusOn();
  if (unit)
  {
    EntityAIFull *veh = unit->GetVehicle();
    if (veh) name = veh->GetControllerScheme();
  }
  UpdateVehicles(name);

  OnVehicleChanged();
  OnSchemeChanged();
  UpdateTexts();
  UpdateButtons();
}

void DisplayProfileController::Init()
{
  ParamEntryVal mainCls = Pars >> "ControllerSchemes";
  
  // fix basicSettings
  ConstParamEntryPtr cls = mainCls.FindEntry(_current.basicSettings);
  if (!cls)
  {
    RptF("Controller scheme %s not found", (const char *)_current.basicSettings);
    cls = &(Pars >> "ControllerSchemes").GetEntry(0);
    _current.basicSettings = cls->GetName();
  }

  _schemeList.Clear();
  for (int i=0; i<mainCls.GetEntryCount(); i++)
  {
    ParamEntryVal entry = mainCls.GetEntry(i);
    if (!entry.IsClass()) continue;
    int index = _schemeList.Add();
    _schemeList[index].name = entry.GetName();
    _schemeList[index].displayName = entry >> "name";
  }

  _typeList.Clear();
  ParamEntryVal array = Pars >> "ControllerTypes";
  for (int i=0; i<array.GetEntryCount(); i++)
  {
    ParamEntryVal entry = array.GetEntry(i);
    int index = _typeList.Add();
    _typeList[index].name = entry.GetName();
    _typeList[index].displayName = entry >> "hint";
  }
}

Control *DisplayProfileController::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_PROF_CONTR_VEHICLE:
    _vehicle = GetListBoxContainer(ctrl);
    break;
  case IDC_PROF_CONTR_TYPE:
    _types = GetListBoxContainer(ctrl);
    break;
  case IDC_PROF_CONTR_SENSITIVITY:
    _sensitivity = GetListBoxContainer(ctrl);
    if (_sensitivity)
    {
      // Fill the sensitivity list
      int sel = 0;
      int index = _sensitivity->AddString(LocalizeString(IDS_CONTROLLER_SENSITIVITY_LOW));
      _sensitivity->SetValue(index, CSLow);
      if (_current.sensitivity == CSLow) sel = index;
      index = _sensitivity->AddString(LocalizeString(IDS_CONTROLLER_SENSITIVITY_MEDIUM));
      _sensitivity->SetValue(index, CSMedium);
      if (_current.sensitivity == CSMedium) sel = index;
      index = _sensitivity->AddString(LocalizeString(IDS_CONTROLLER_SENSITIVITY_HIGH));
      _sensitivity->SetValue(index, CSHigh);
      if (_current.sensitivity == CSHigh) sel = index;
      _sensitivity->SetCurSel(sel, false);
    }
    break;
  case IDC_PROF_CONTR_SCHEME:
    _scheme = GetTextContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayProfileController::OnSimulate(EntityAI *vehicle)
{
  bool typeChanged = false;
  if (_types)
  {
    int sel = _types->GetCurSel();
    typeChanged = sel >= 0 && stricmp(_types->GetData(sel), _type) != 0;
  }

  if (typeChanged)
  {
    // type changed in OnLBSelChanged, handle it now
    RString vehicle;
    if (_vehicle)
    {
      int sel = _vehicle->GetCurSel();
      if (sel >= 0) vehicle = _vehicle->GetData(sel);
    }

    OnTypeChanged();

    _lastTypes = 0;
    UpdateTypes();
    UpdateVehicles(vehicle);

    OnVehicleChanged();
    OnSchemeChanged();

    UpdateTexts();
    UpdateButtons();
  }
  else
  {
    // only check if attached controller changed
    UpdateTypes();
  }

  Display::OnSimulate(vehicle);
}

void DisplayProfileController::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_CONTR_VEHICLE:
    OnVehicleChanged();
    UpdateTexts();
    UpdateButtons();
    break;
  case IDC_PROF_CONTR_TYPE:
    if (_types && curSel >= 0)
    {
      _type = _types->GetData(curSel);
      // FIX: cannot call OnTypeChanged() now (inside function of control which will be deleted) - moved to OnSimulate()
    }
    break;
  case IDC_PROF_CONTR_SENSITIVITY:
    if (_sensitivity) _current.sensitivity = (ControllerSensitivity)_sensitivity->GetValue(curSel);
    break;
  }
}

int DisplayProfileController::FindScheme(RString scheme) const
{
  for (int i=0; i<_schemeList.Size(); i++)
  {
    if (stricmp(_schemeList[i].name, scheme) == 0) return i;
  }
  return -1;
}

void DisplayProfileController::UpdateTypes()
{
#ifdef _XBOX
  int mask = XInputDev->GetTypes();
#else
  int mask = 0xffffffff;
#endif
  if (mask == _lastTypes) return;
  _lastTypes = mask;

  if (_types)
  {
    _types->ClearStrings();
    int sel = 0;
    bool changed = true;
    for (int i=0; i<_typeList.Size(); i++)
    {
      if ((mask & (1 << i)) == 0) continue;
      int index = _types->AddString(_typeList[i].displayName);
      _types->SetData(index, _typeList[i].name);
      if (stricmp(_typeList[i].name, _type) == 0)
      {
        sel = index;
        changed = false;
      }
    }
    _types->SetCurSel(sel, changed);
  }
}

void DisplayProfileController::UpdateVehicles(RString name)
{
  if (!_vehicle) return;

  _vehicle->ClearStrings();
  int sel = 0;

  ParamEntryVal vehicles = Pars >> "ControllerSchemes" >> _current.basicSettings >> "Vehicles";
  for (int i=0; i<vehicles.GetEntryCount(); i++)
  {
    ParamEntryVal entry = vehicles.GetEntry(i);
    int index = _vehicle->AddString(entry >> "name");
    _vehicle->SetData(index, entry.GetName());
    if (stricmp(entry.GetName(), name) == 0) sel = index;
  }
  _vehicle->SetCurSel(sel, false);
}

void DisplayProfileController::UpdateButtons()
{
  IControl *ctrl = GetCtrl(IDC_PROF_CONTR_NEXT_SCHEME);
  if (ctrl) ctrl->EnableCtrl(_schemeList.Size() > 1);
  ctrl = GetCtrl(IDC_PROF_CONTR_PREV_SCHEME);
  if (ctrl) ctrl->EnableCtrl(_schemeList.Size() > 1);
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_PROF_CONTR_Y_AXIS));
  if (text) text->SetText(LocalizeString(_yAxis ? IDS_YAXIS_REVERTED : IDS_YAXIS_NORMAL));
  ctrl = GetCtrl(IDC_PROF_CONTR_VIBRATIONS);
#ifdef _XBOX
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
  if (ctrl) ctrl->ShowCtrl(false);
#else
  text = GetTextContainer(ctrl);
  if (text) text->SetText(LocalizeString(_current.vibrations ? IDS_FOREFDB_ENABLED : IDS_FOREFDB_DISABLED));
#endif
}

void DisplayProfileController::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_PROF_CONTR_NEXT_SCHEME:
    // next scheme
    if (_schemeList.Size() > 1)
    {
      int index = FindScheme(_current.basicSettings) + 1;
      if (index >= _schemeList.Size()) index = 0;
      _current.basicSettings = _schemeList[index].name;
      OnSchemeChanged();
      OnVehicleChanged();
      UpdateTexts();
      UpdateButtons();
    }
    break;
  case IDC_PROF_CONTR_PREV_SCHEME:
    // previous scheme
    if (_schemeList.Size() > 1)
    {
      int index = FindScheme(_current.basicSettings) - 1;
      if (index < 0) index = _schemeList.Size() - 1;
      _current.basicSettings = _schemeList[index].name;
      OnSchemeChanged();
      OnVehicleChanged();
      UpdateTexts();
      UpdateButtons();
    }
    break;
  case IDC_PROF_CONTR_Y_AXIS:
    _yAxis = !_yAxis;
    SetYAxis();
    OnVehicleChanged();
    UpdateTexts();
    UpdateButtons();
    break;
#ifndef _XBOX
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
  case IDC_PROF_CONTR_VIBRATIONS:
    _current.vibrations = !_current.vibrations;
    UpdateButtons();
    break;
#endif
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayProfileController::SetYAxis()
{
  if (!_vehicle) return;
  int sel = _vehicle->GetCurSel();
  if (sel < 0) return;
  RString vehicle = _vehicle->GetData(sel);

  for (int i=0; i<_current.settings.Size(); i++)
  {
    if (stricmp(_current.settings[i].name, vehicle) == 0)
    {
      _current.settings[i].yAxis = _yAxis;
      return;
    }
  }
  // not found
  int i = _current.settings.Add();
  _current.settings[i].name = vehicle;
  _current.settings[i].yAxis = _yAxis;
}

#define STRTEXT(idc) GetStructuredText(disp->GetCtrl(idc))

void UpdateDesc(CStructuredText *ctrl, ActionDesc *data, int size, int key, int show, bool showImage, bool right)
{
  if (!ctrl) return;
  if (show == 0)
  {
    ctrl->SetStructuredText(RString());
    return;
  }

  // search for action
  RString action;
  for (int i=0; i<size; i++)
  {
    if (data[i].key == key)
    {
      action = data[i].action;
      break;
    }
  }
  if (action.GetLength() == 0)
  {
    ctrl->SetStructuredText(RString());
    return;
  }
  if (!Glob.config.IsEnabled(DT3rdPersonView))
  {
    // when PersonView action is disabled, don't show description
    if (stricmp(GInput.userActionDesc[UAPersonView].name, action) == 0)
    {
      ctrl->SetStructuredText(RString());
      return;
    }
  }


  // search for action description
  int desc = -1;
  for (int i=0; i<UAN; i++)
  {
    if (stricmp(GInput.userActionDesc[i].name, action) == 0)
    {
      desc = GInput.userActionDesc[i].desc;
      break;
    }
  }
  if (desc < 0)
  {
    ctrl->SetStructuredText(RString());
    return;
  }

  INode *text = CreateTextStructured(NULL);

  if (right) CreateTextASCII(text, LocalizeString(desc));

  if (showImage)
  {
    KeyImage keyImage = GInput.GetKeyImage(key);
    if (keyImage.id >= 0)
    {
      if (right) CreateTextASCII(text, " ");
      INode *img = CreateTextImage(text);
      SetTextAttribute(img, "image", keyImage.image);
      SetTextAttribute(img, "size", keyImage.size);
      SetTextAttribute(img, "color", "#ffffff");
      if (!right) CreateTextASCII(text, " ");
    }
  }

  if (!right) CreateTextASCII(text, LocalizeString(desc));

  ctrl->SetText(text);
}

void UpdateDesc(ControlsContainer *disp, ActionDesc *ptr, int n, RString scheme, RString vehicle, RString type)
{
#ifdef _WIN32
  XINPUT_GAMEPAD avail;
  memset(&avail, 0xff, sizeof(avail));
#ifdef _XBOX
  if (XInputDev) avail = XInputDev->GetAvailable(type);
#endif

  ParamEntryVal cfgType = Pars >> "ControllerTypes" >> type;
  #if !defined _XBOX || _XBOX_VER >= 2
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_A), ptr, n, INPUT_DEVICE_XINPUT + XBOX_A, avail.wButtons & XINPUT_GAMEPAD_A, true, cfgType >> "imageRightA");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_B), ptr, n, INPUT_DEVICE_XINPUT + XBOX_B, avail.wButtons & XINPUT_GAMEPAD_B, true, cfgType >> "imageRightB");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_Y), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Y, avail.wButtons & XINPUT_GAMEPAD_X, true, cfgType >> "imageRightY");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_X), ptr, n, INPUT_DEVICE_XINPUT + XBOX_X, avail.wButtons & XINPUT_GAMEPAD_Y, true, cfgType >> "imageRightX");
    // TODOX360: Verify White/Black mapping
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_WHITE), ptr, n, INPUT_DEVICE_XINPUT + XBOX_RightBumper, avail.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER, true, cfgType >> "imageRightWhite");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_BLACK), ptr, n, INPUT_DEVICE_XINPUT + XBOX_LeftBumper, avail.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, true, cfgType >> "imageRightBlack");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_LEFT), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Left, avail.wButtons & XINPUT_GAMEPAD_DPAD_LEFT, true, cfgType >> "imageRightLeft");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_RIGHT), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Right, avail.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT, true, cfgType >> "imageRightRight");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_UP), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Up, avail.wButtons & XINPUT_GAMEPAD_DPAD_UP, true, cfgType >> "imageRightUp");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_DOWN), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Down, avail.wButtons & XINPUT_GAMEPAD_DPAD_DOWN, true, cfgType >> "imageRightDown");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_BACK), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Back, avail.wButtons & XINPUT_GAMEPAD_BACK, true, cfgType >> "imageRightBack");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_START), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Start, avail.wButtons & XINPUT_GAMEPAD_START, true, cfgType >> "imageRightStart");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_LEFT_TRIGGER), ptr, n, INPUT_DEVICE_XINPUT + XBOX_LeftTrigger, avail.bLeftTrigger, false, false);
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_RIGHT_TRIGGER), ptr, n, INPUT_DEVICE_XINPUT + XBOX_RightTrigger, avail.bRightTrigger, false, false);
  #else
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_A), ptr, n, INPUT_DEVICE_XINPUT + XBOX_A, avail.bAnalogButtons[0], true, cfgType >> "imageRightA");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_B), ptr, n, INPUT_DEVICE_XINPUT + XBOX_B, avail.bAnalogButtons[1], true, cfgType >> "imageRightB");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_Y), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Y, avail.bAnalogButtons[3], true, cfgType >> "imageRightY");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_X), ptr, n, INPUT_DEVICE_XINPUT + XBOX_X, avail.bAnalogButtons[2], true, cfgType >> "imageRightX");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_WHITE), ptr, n, INPUT_DEVICE_XINPUT + XBOX_RightBumper, avail.bAnalogButtons[5], true, cfgType >> "imageRightWhite");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_BLACK), ptr, n, INPUT_DEVICE_XINPUT + XBOX_LeftBumper, avail.bAnalogButtons[4], true, cfgType >> "imageRightBlack");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_LEFT), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Left, avail.wButtons & 0x04, true, cfgType >> "imageRightLeft");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_RIGHT), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Right, avail.wButtons & 0x08, true, cfgType >> "imageRightRight");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_UP), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Up, avail.wButtons & 0x01, true, cfgType >> "imageRightUp");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_DOWN), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Down, avail.wButtons & 0x02, true, cfgType >> "imageRightDown");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_BACK), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Back, avail.wButtons & 0x20, true, cfgType >> "imageRightBack");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_START), ptr, n, INPUT_DEVICE_XINPUT + XBOX_Start, avail.wButtons & 0x10, true, cfgType >> "imageRightStart");
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_LEFT_TRIGGER), ptr, n, INPUT_DEVICE_XINPUT + XBOX_LeftTrigger, avail.bAnalogButtons[6], false, false);
    UpdateDesc(STRTEXT(IDC_PROF_CONTR_RIGHT_TRIGGER), ptr, n, INPUT_DEVICE_XINPUT + XBOX_RightTrigger, avail.bAnalogButtons[7], false, false);
  #endif

  ParamEntryVal cfg = Pars >> "ControllerSchemes" >> scheme >> "Vehicles" >> vehicle >> type;
  CStructuredText *ctrl = STRTEXT(IDC_PROF_CONTR_LEFT_THUMB_MOVE_X);
  if (ctrl)
  {
    if (avail.sThumbLX) ctrl->SetStructuredText(cfg >> "textLThumbLR");
    else ctrl->SetStructuredText(RString());
  }
  ctrl = STRTEXT(IDC_PROF_CONTR_LEFT_THUMB_MOVE_Y);
  if (ctrl)
  {
    if (avail.sThumbLY) ctrl->SetStructuredText(cfg >> "textLThumbUD");
    else ctrl->SetStructuredText(RString());
  }
  ctrl = STRTEXT(IDC_PROF_CONTR_LEFT_THUMB);
  if (ctrl)
  {
    if (avail.wButtons & 0x40) ctrl->SetStructuredText(cfg >> "textLThumbPS");
    else ctrl->SetStructuredText(RString());
  }

  ctrl = STRTEXT(IDC_PROF_CONTR_RIGHT_THUMB_MOVE_X);
  if (ctrl)
  {
    if (avail.sThumbRX) ctrl->SetStructuredText(cfg >> "textRThumbLR");
    else ctrl->SetStructuredText(RString());
  }
  ctrl = STRTEXT(IDC_PROF_CONTR_RIGHT_THUMB_MOVE_Y);
  if (ctrl)
  {
    if (avail.sThumbRY) ctrl->SetStructuredText(cfg >> "textRThumbUD");
    else ctrl->SetStructuredText(RString());
  }
  ctrl = STRTEXT(IDC_PROF_CONTR_RIGHT_THUMB);
  if (ctrl)
  {
    if (avail.wButtons & 0x80) ctrl->SetStructuredText(cfg >> "textRThumbPS");
    else ctrl->SetStructuredText(RString());
  }

  ctrl = STRTEXT(IDC_PROF_CONTR_LEFT_THUMB_MOVE);
  if (ctrl)
  {
    if (avail.sThumbLX != 0 || avail.sThumbLY != 0) ctrl->SetStructuredText(cfg >> "textLThumb");
    else ctrl->SetStructuredText(RString());
  }
  ctrl = STRTEXT(IDC_PROF_CONTR_RIGHT_THUMB_MOVE);
  if (ctrl)
  {
    if (avail.sThumbRX != 0 || avail.sThumbRY != 0) ctrl->SetStructuredText(cfg >> "textRThumb");
    else ctrl->SetStructuredText(RString());
  }
  ctrl = STRTEXT(IDC_PROF_CONTR_DPAD);
  if (ctrl)
  {
    if (avail.wButtons & 0x0f) ctrl->SetStructuredText(cfg >> "textDPad");
    else ctrl->SetStructuredText(RString());
  }
#endif //_WIN32
}

void DisplayProfileController::OnVehicleChanged()
{
  if (!_vehicle) return;
  int sel = _vehicle->GetCurSel();
  if (sel < 0) return;
  RString vehicle = _vehicle->GetData(sel);

  ParamEntryVal list = Pars >> "ControllerSchemes" >> _current.basicSettings >> "Vehicles" >> vehicle;

  // yAxis settings for this vehicle

  // use config entry as default
  _yAxis = list >> "axisY";
#ifdef _XBOX
  // if not forced, use Xbox Guide Settings as default
  if (!_yAxis)
  {
    switch (GSaveSystem.GetGameDefaults(XPROFILE_GAMER_YAXIS_INVERSION))
    {
    case XPROFILE_YAXIS_INVERSION_OFF:
      _yAxis = false;
      break;
    case XPROFILE_YAXIS_INVERSION_ON:
      _yAxis = true;
      break;
    }
  }
#endif
  // replace by user settings when available
  for (int i=0; i<_current.settings.Size(); i++)
  {
    if (stricmp(_current.settings[i].name, vehicle) == 0)
    {
      _yAxis = _current.settings[i].yAxis;
      break;
    }
  }
}

static void AddDescriptions(StaticArrayAuto<ActionDesc> &desc, ParamEntryVal entry)
{
  RString name = entry.GetName();

  if (entry.IsClass())
  {
    ParamEntryVal keys = entry >> "keys";
    for (int k=0; k<keys.GetSize(); k++)
    {
      int index = desc.Add();
      desc[index].action = name;
      desc[index].key = keys[k];
    }
  }
  else if (entry.IsArray() && entry.GetSize() > 0)
  {
    // add mapping
    const IParamArrayValue &keys = entry[0];
    if (keys.IsArrayValue())
    {
      for (int k=0; k<keys.GetItemCount(); k++)
      {
        int index = desc.Add();
        desc[index].action = name;
        desc[index].key = *keys.GetItem(k);
      }
    }
    else
    {
      int index = desc.Add();
      desc[index].action = name;
      desc[index].key = keys;
    }
  }
}

void DisplayProfileController::UpdateTexts()
{
  if (!_vehicle) return;
  int sel = _vehicle->GetCurSel();
  if (sel < 0) return;
  RString vehicle = _vehicle->GetData(sel);

  AUTO_STATIC_ARRAY(ActionDesc, desc, 32);

  {
    // basic settings
    ParamEntryVal actions = Pars >> "ControllerSchemes" >> _current.basicSettings >> _type >> "Actions";
    for (int i=0; i<actions.GetEntryCount(); i++)
    {
      ParamEntryVal entry = actions.GetEntry(i);
      AddDescriptions(desc, entry);
    }
  }
  {
    // vehicle settings
    ParamEntryVal actions = Pars >> "ControllerSchemes" >> _current.basicSettings >> "Vehicles" >> vehicle >> _type >> "Actions";
    for (int i=0; i<actions.GetEntryCount(); i++)
    {
      ParamEntryVal entry = actions.GetEntry(i);
      RString name = entry.GetName();

      // remove basic mapping replaced by this mapping
      for (int i=0; i<desc.Size(); i++)
      {
        if (stricmp(desc[i].action, name) == 0)
        {
          desc.Delete(i);
          i--;
        }
      }

      AddDescriptions(desc, entry);
    }
  }

  ActionDesc *ptr = desc.Data();
  int n = desc.Size();
  UpdateDesc(this, ptr, n, _current.basicSettings, vehicle, _type);
}

void DisplayProfileController::OnTypeChanged()
{
/*
  IControl *ctrl = GetCtrl(IDC_PROF_CONTR_IMAGE);
  CTextContainer *text = GetTextContainer(ctrl);
  if (!text) return;

  RString picture = Pars >> "ControllerTypes" >> _type >> "picture";
  text->SetText(picture);
*/
  int focused = -1;
  if (GetFocused()) focused = GetFocused()->IDC();

  RString resource = Pars >> "ControllerTypes" >> _type >> "resource";
#if defined _XBOX && !(_XBOX_VER>=2)
  // TODOX360: verify if there are any controller picture requirements
  if (XGetGameRegion() == XC_GAME_REGION_JAPAN)
  {
    ConstParamEntryPtr entry = (Pars >> "ControllerTypes" >> _type).FindEntry("resourceJapanese");
    if (entry) resource = *entry;
  }
#endif

  // avoid pointing to destroyed controls
  _vehicle = NULL;
  _types = NULL;
  _sensitivity = NULL;
  _scheme = NULL;
  _keyA = NULL;
  _keyB = NULL;

  ControlsContainer::Init();
  Load(resource);
  
  if (focused >= 0) FocusCtrl(focused);
}

void DisplayProfileController::OnSchemeChanged()
{
  if (_scheme)
  {
    int index = FindScheme(_current.basicSettings);
    RString scheme = index < 0 ? RString() : _schemeList[index].displayName;
    _scheme->SetText(scheme);
  }
}

DisplayProfileAudio::DisplayProfileAudio(ControlsContainer *parent, bool enableSimulation, float music, float voice, float effects)
  : Display(parent)
{
  _oldMusic = music;
  _oldVoice = voice;
  _oldEffects = effects;

  Load("RscDisplayProfileAudio");
  _enableSimulation = _enableSimulation && enableSimulation;

  if (_music)
    _music->SetThumbPos(music);
  if (_voice)
    _voice->SetThumbPos(voice);
  if (_effects)
    _effects->SetThumbPos(effects);

  GSoundsys->SetCDVolume(music);
  GSoundsys->SetSpeechVolume(voice);
  GSoundsys->SetWaveVolume(effects);
  
  GSoundsys->StartPreview();
}

Control *DisplayProfileAudio::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_PROF_AUDIO_MUSIC:
    _music = GetSliderContainer(ctrl);
    break;
  case IDC_PROF_AUDIO_RADIO:
    _voice = GetSliderContainer(ctrl);
    break;
  case IDC_PROF_AUDIO_EFFECT:
    _effects = GetSliderContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayProfileAudio::Destroy()
{
  GSoundsys->TerminatePreview();
  Display::Destroy();
}

void DisplayProfileAudio::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CANCEL:
    // restore original values
    GSoundsys->SetCDVolume(_oldMusic);
    GSoundsys->SetSpeechVolume(_oldVoice);
    GSoundsys->SetWaveVolume(_oldEffects);
    Exit(idc);
    return;
  case IDC_PROF_AUDIO_DEFAULT:
    // default settings
    GSoundsys->SetCDVolume(GSoundsys->GetDefaultCDVolume());
    GSoundsys->SetSpeechVolume(GSoundsys->GetDefaultSpeechVolume());
    GSoundsys->SetWaveVolume(GSoundsys->GetDefaultWaveVolume());

    GSoundsys->StartPreview();

    if (_music) _music->SetThumbPos(GSoundsys->GetCDVolume());
    if (_voice) _voice->SetThumbPos(GSoundsys->GetSpeechVolume());
    if (_effects) _effects->SetThumbPos(GSoundsys->GetWaveVolume());
    return;
  }
  Display::OnButtonClicked(idc);
}

void DisplayProfileAudio::OnSliderPosChanged(IControl *ctrl, float pos)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_AUDIO_MUSIC:
    GSoundsys->SetCDVolume(pos);
    GSoundsys->StartPreview();
    break;
  case IDC_PROF_AUDIO_RADIO:
    GSoundsys->SetSpeechVolume(pos);
    GSoundsys->StartPreview();
    break;
  case IDC_PROF_AUDIO_EFFECT:
    GSoundsys->SetWaveVolume(pos);
    GSoundsys->StartPreview();
    break;
  }
  if (_music) _music->SetThumbPos(GSoundsys->GetCDVolume());
  if (_voice) _voice->SetThumbPos(GSoundsys->GetSpeechVolume());
  if (_effects) _effects->SetThumbPos(GSoundsys->GetWaveVolume());
}

float DisplayProfileAudio::GetMusicVolume() const
{
  if (_music) return _music->GetThumbPos();
  return _oldMusic;
}

float DisplayProfileAudio::GetVoiceVolume() const
{
  if (_voice) return _voice->GetThumbPos();
  return _oldVoice;
}

float DisplayProfileAudio::GetEffectsVolume() const
{
  if (_effects) return _effects->GetThumbPos();
  return _oldEffects;
}

DisplayProfileVideo::DisplayProfileVideo(ControlsContainer *parent, bool enableSimulation, float bright, float gamma, bool titles, bool radio)
  : Display(parent)
{
  _oldBright = bright;
  _oldGamma = gamma;
  _oldTitles = titles;
  _oldRadio = radio;

  Load("RscDisplayProfileVideo");
  _enableSimulation = _enableSimulation && enableSimulation;

  if (_bright)
  {
    _bright->SetRange(MinBrightness, MaxBrightness);
    _bright->SetSpeed(0.05, 0.25);
    _bright->SetThumbPos(bright);
  }
  if (_gamma)
  {
    _gamma->SetRange(-2.0, -0.5);
    _gamma->SetSpeed(0.05, 0.25);
    _gamma->SetThumbPos(-1.0 / gamma);
  }
  if (_titles)
  {
    _titles->AddString(LocalizeString(IDS_OPT_SUBTITLES_DISABLED));
    _titles->AddString(LocalizeString(IDS_OPT_SUBTITLES_ENABLED));
    _titles->SetCurSel(titles ? 1 : 0);
  }
  if (_radio)
  {
    _radio->AddString(LocalizeString(IDS_OPT_RADIO_DISABLED));
    _radio->AddString(LocalizeString(IDS_OPT_RADIO_ENABLED));
    _radio->SetCurSel(radio ? 1 : 0);
  }

  GEngine->SetBrightness(bright);
  GEngine->SetGamma(gamma);
  Glob.config.showTitles = titles;
  GChatList.Enable(radio);
}

Control *DisplayProfileVideo::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_PROF_VIDEO_BRIGHT:
    _bright = GetSliderContainer(ctrl);
    break;
  case IDC_PROF_VIDEO_GAMMA:
    _gamma = GetSliderContainer(ctrl);
    break;
  case IDC_PROF_VIDEO_SUBTITLES:
    _titles = GetListBoxContainer(ctrl);
    break;
  case IDC_PROF_VIDEO_RADIO:
    _radio = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayProfileVideo::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CANCEL:
    // restore original values
    GEngine->SetBrightness(_oldBright);
    GEngine->SetGamma(_oldGamma);
    Glob.config.showTitles = _oldTitles;
    GChatList.Enable(_oldRadio);
    Exit(idc);
    return;
  case IDC_PROF_VIDEO_DEFAULT:
    // default settings
    GEngine->SetBrightness(GEngine->GetDefaultBrightness());
    GEngine->SetGamma(GEngine->GetDefaultGamma());
    Glob.config.showTitles = SHOW_TITLES_DEFAULT;
    GChatList.Enable(ENABLE_CHAT_DEFAULT);

    if (_bright) _bright->SetThumbPos(GEngine->GetBrightness());
    if (_gamma) _gamma->SetThumbPos(-1.0 / GEngine->GetGamma());
    if (_titles) _titles->SetCurSel(Glob.config.showTitles ? 1 : 0);
    if (_radio) _radio->SetCurSel(GChatList.ProfileEnabled() ? 1 : 0);
    return;
  }
  Display::OnButtonClicked(idc);
}

void DisplayProfileVideo::OnSliderPosChanged(IControl *ctrl, float pos)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_VIDEO_BRIGHT:
    GEngine->SetBrightness(pos);
    break;
  case IDC_PROF_VIDEO_GAMMA:
    GEngine->SetGamma(-1.0 / pos);
    break;
  default:
    Display::OnSliderPosChanged(ctrl, pos);
  }
}

void DisplayProfileVideo::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_PROF_VIDEO_SUBTITLES:
    Glob.config.showTitles = curSel != 0;
    break;
  case IDC_PROF_VIDEO_RADIO:
    GChatList.Enable(curSel != 0);
    break;
  default:
    Display::OnLBSelChanged(ctrl, curSel);
    break;
  }
}

float DisplayProfileVideo::GetBright() const
{
  if (_bright) return _bright->GetThumbPos();
  return _oldBright;
}

float DisplayProfileVideo::GetGamma() const
{
  if (_gamma) return -1.0 / _gamma->GetThumbPos();
  return _oldGamma;
}

bool DisplayProfileVideo::GetTitles() const
{
  if (_titles) return _titles->GetCurSel() != 0;
  return _oldTitles;
}

bool DisplayProfileVideo::GetRadio() const
{
  if (_radio) return _radio->GetCurSel() != 0;
  return _oldRadio;
}

#if defined _XBOX && _XBOX_VER < 200

// On Xbox 360, profiles are handled by Xbox Guide

DisplayProfileLive::DisplayProfileLive(ControlsContainer *parent, bool enableSimulation, RString account, bool mainMenu)
: Display(parent)
{
  Load("RscDisplayProfileLive");
  _enableSimulation = _enableSimulation && enableSimulation;
  _mainMenu = mainMenu;
#if defined _XBOX && _ENABLE_MP
  _usersCount = 0;
  UpdateAccounts(account);

  if (GInvited && _accounts)
  {
    for (DWORD i=0; i<_usersCount; i++)
    {
      if (XOnlineAreUsersIdentical(&_users[i].xuid, &GGameInvite.xuidAcceptedFriend))
      {
        _accounts->SetCurSel(i);
        OnButtonClicked(IDC_OK);
        break;
      }
    }
  }
#endif
}

Control *DisplayProfileLive::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  if (idc == IDC_PROFILE_LIVE_ACCOUNTS)
    _accounts = GetListBoxContainer(ctrl);
  return ctrl;
}

#if defined _XBOX && _ENABLE_MP
void DisplayProfileLive::SignIn(const XONLINE_USER &user)
{
  HRESULT hr = GetNetworkManager().SignIn(user);
  if (SUCCEEDED(hr))
  {
    ParamFile cfg;
    if (ParseUserParams(cfg))
    {
      cfg.Add("userAccount", user.szGamertag);
      SaveUserParams(cfg);
    }
    Exit(IDC_OK);
    return;
  }

  // Error Message
  switch (hr)
  {
  case XONLINE_E_LOGON_UPDATE_REQUIRED:
    // update required
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_XBOX_LIVE_HINT_UPDATE), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_BACK), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_XBOX_LIVE_ERROR_UPDATE),
        IDD_MSG_XONLINE_UPDATE_REQUIRED, false, &buttonOK, &buttonCancel);
    }
    break;
  case XONLINE_E_LOGON_INVALID_USER:
  case XONLINE_E_LOGON_SERVICE_NOT_AUTHORIZED:
    // invalid user
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_XBOX_LIVE_HINT_UPDATE_ACCOUNT), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_XBOX_LIVE_ERROR_NOT_CURRENT),
        IDD_MSG_XONLINE_INVALID_USER, false, &buttonOK, &buttonCancel);
    }
    break;
  case XONLINE_E_LOGON_SERVERS_TOO_BUSY:
  case XONLINE_E_LOGON_SERVICE_TEMPORARILY_UNAVAILABLE:
    // server busy
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_XBOX_LIVE_HINT_TRY_AGAIN), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_BACK), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_XBOX_LIVE_ERROR_BUSY),
        IDD_MSG_XONLINE_SERVER_BUSY, false, &buttonOK, &buttonCancel);
    }
    break;
  case XONLINE_E_LOGON_USER_ACCOUNT_REQUIRES_MANAGEMENT:
    // important message
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_XBOX_LIVE_HINT_READ_MSG), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_BACK), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_XBOX_LIVE_ERROR_IMPORTANT_MSG),
        IDD_MSG_XONLINE_REQUIRED_MSG, false, &buttonOK, &buttonCancel);
    }
    break;
  case XONLINE_E_LOGON_NO_NETWORK_CONNECTION:
  case XONLINE_E_LOGON_CONNECTION_LOST:
  case XONLINE_E_LOGON_CANNOT_ACCESS_SERVICE:
  default:
    // troubleshooter
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_XBOX_LIVE_HINT_TROUBLESHOOTER), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_XBOX_LIVE_ERROR_CONNECT),
        IDD_MSG_XONLINE_CONNECTION_FAILED, false, &buttonOK, &buttonCancel);
    }
    break;
  }
}
#endif

void DisplayProfileLive::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    {
#if defined _XBOX && _ENABLE_MP
      if (!_accounts) return;
      int sel = _accounts->GetCurSel();
      if (sel < 0) return;
      const XONLINE_USER &user = _users[sel];
      SignIn(user);
#endif
    }
    break;
  case IDC_PROFILE_LIVE_CREATE:
    // no matter if cable is connected or not, we need to offer Create account option
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_MSG_XONLINE_NEW_ACCOUNT),
        IDD_MSG_NEWACCOUNT, false, &buttonOK, &buttonCancel);
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayProfileLive::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);

#if defined _XBOX && !(_XBOX_VER>=2)
  // TODOX360: verify following is no longer needed
  // If any MUs are inserted, update the user list
  // and go to account selection if there are any accounts
  DWORD insertions;
  DWORD removals;
  if (XGetDeviceChanges(XDEVICE_TYPE_MEMORY_UNIT, &insertions, &removals))
    UpdateAccounts(GetAccount());
#endif
}

void DisplayProfileLive::OnChildDestroyed(int idd, int exit)
{
#if defined _XBOX && !(_XBOX_VER>=2)
  // TODOX360: verify following is no longer needed
  switch (idd)
  {
  case IDD_MSG_NEWACCOUNT:
#ifdef _XBOX
    if (exit == IDC_OK)
    {
      // Reboot to Dashboard.
      LD_LAUNCH_DASHBOARD ld;
      ZeroMemory(&ld, sizeof(ld));
      ld.dwReason = XLD_LAUNCH_DASHBOARD_NEW_ACCOUNT_SIGNUP;
      XLaunchNewImage(NULL, (LAUNCH_DATA *)(&ld));
      Fail("Unaccessible");
    }
#endif
    break;
#ifdef _XBOX
  case IDD_MSG_XONLINE_CONNECTION_FAILED:
    if (exit == IDC_OK)
    {
      LD_LAUNCH_DASHBOARD ld;
      ZeroMemory(&ld, sizeof(ld));
      ld.dwReason = XLD_LAUNCH_DASHBOARD_NETWORK_CONFIGURATION;
      XLaunchNewImage(NULL, PLAUNCH_DATA(&ld));
      Fail("Unaccessible");
    }
    break;
  case IDD_MSG_XONLINE_UPDATE_REQUIRED:
    if (exit == IDC_OK) XOnlineTitleUpdate(0);
    else Exit(IDC_CANCEL);
    break;
  case IDD_MSG_XONLINE_SERVER_BUSY:
    if (exit == IDC_OK)
    {
      Display::OnChildDestroyed(idd, exit);
      if (!_accounts) return;
      int sel = _accounts->GetCurSel();
      if (sel < 0) return;
      const XONLINE_USER &user = _users[sel];
      SignIn(user);
      return;
    }
    else Exit(IDC_CANCEL);
    break;
  case IDD_MSG_XONLINE_REQUIRED_MSG:
  case IDD_MSG_XONLINE_INVALID_USER:
    if (exit == IDC_OK)
    {
      LD_LAUNCH_DASHBOARD ld;
      ZeroMemory(&ld, sizeof(ld));
      ld.dwReason = XLD_LAUNCH_DASHBOARD_ACCOUNT_MANAGEMENT;
      XLaunchNewImage(NULL, PLAUNCH_DATA(&ld));
    }
    else
    {
      GetNetworkManager().SignOff();
      Exit(IDC_CANCEL);
    }
    break;
#endif
  }
  #endif
  Display::OnChildDestroyed(idd, exit);
}

#if defined _XBOX && _ENABLE_MP
void DisplayProfileLive::UpdateAccounts(RString account)
{
  IControl *button = GetCtrl(IDC_OK);

  if (!_accounts)
  {
    if (button) button->ShowCtrl(false);
    return;
  }

  _accounts->ClearStrings();
  int sel = 0;

  // Wait for any inserted MUs to mount
  while (XGetDeviceEnumerationStatus() == XDEVICE_ENUMERATION_BUSY) {Sleep(10);}

  HRESULT hr = XOnlineGetUsers(_users, &_usersCount);
  if (!SUCCEEDED(hr))
  {
    _usersCount = 0;
  }
  for (DWORD i=0; i<_usersCount; i++)
  {
    _accounts->AddString(_users[i].szGamertag);
    if (strcmp(_users[i].szGamertag, account) == 0) sel = i;
  }

  _accounts->SetCurSel(sel);
  if (button) button->ShowCtrl(_accounts->GetCurSel() >= 0);
}

RString DisplayProfileLive::GetAccount() const
{
  if (!_accounts) return RString();
  int sel = _accounts->GetCurSel();
  if (sel < 0) return RString();
  return _users[sel].szGamertag;
}

#endif // defined _XBOX && _ENABLE_MP

#endif // defined _XBOX && _XBOX_VER < 200

DisplaySelectDifficulty::DisplaySelectDifficulty(ControlsContainer *parent, int iddNext, RString rsc = "RscDisplaySelectDifficulty", bool enableSimulation = false)
  : Display(parent)
{
  _iddNext = iddNext;
  Load(rsc);
  _enableSimulation = enableSimulation;
  if (_list)
  {
    for (int i=0; i<Glob.config.diffSettings.Size(); i++)
    {
      _list->AddString(Glob.config.diffSettings[i].displayName);
    }
    _list->SetCurSel(Glob.config.difficulty);
  }
  OnChangedDifficulty();
}

Control *DisplaySelectDifficulty::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_DIFF_LIST:
    _list = GetListBoxContainer(ctrl);
    break;
  case IDC_DIFF_DESC:
    _desc = GetHTMLContainer(ctrl);
    ctrl->EnableCtrl(false);
    break;
  case IDC_OK:
    if (GWorld->GetMode() == GModeNetware && (!GetNetworkManager().IsServer() || GetNetworkManager().GetServerState()>=NSSAssigningPlayers) )
    {
      ctrl->EnableCtrl(false);
    }
    break;
  }

  return ctrl;
}
  
int DisplaySelectDifficulty::GetDifficulty() const
{
  if (_list)
  {
    int sel = _list->GetCurSel();
    if (sel >= 0) return sel;
  }
  return Glob.config.difficulty;
}

void DisplaySelectDifficulty::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_DIFF_LIST:
    OnChangedDifficulty();
    break;
  default:
    Display::OnLBSelChanged(ctrl, curSel);
    break;
  }
}

void DisplaySelectDifficulty::OnLBDblClick(int idc, int curSel)
{
  switch (idc)
  {
  case IDC_DIFF_LIST:
    if (GWorld->GetMode() != GModeNetware)
    Exit(IDC_OK);
    else
      Display::OnLBDblClick(idc, curSel);
    break;
  default:
    Display::OnLBDblClick(idc, curSel);
    break;
  }
}

static RString FindHTML(RString prefix)
{
  RString name = prefix + RString(".") + GLanguage + RString(".html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  name = prefix + RString(".html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  return RString();
}

void DisplaySelectDifficulty::OnChangedDifficulty()
{
  if (_desc)
  {
    int difficulty = GetDifficulty();
    RString filename = FindHTML(Glob.config.diffSettings[difficulty].description);
    if (filename.GetLength() == 0)
      _desc->Init();
    else
      _desc->Load(filename);
  }
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
void DisplaySelectDifficulty::OnStorageDeviceRemoved()
{
  // we dont want to do anything here, it will be handled in parent display
  Exit(IDC_CANCEL);
}
#endif

static inline void TerminateBy(RString &name, char c)
{
  int n = name.GetLength();
  if (n > 0 && name[n - 1] != c) name = name + RString(&c, 1);
}

// Single mission display
DisplaySingleMission::DisplaySingleMission(ControlsContainer *parent, RString root, RString missionsSpace)
  : Display(parent)
{
  _missionList = NULL;
  _overview = NULL;
  _userMissions = false;
  _package = false;
  _showAll = false;
  ParamEntryVal cls = Pars >> "RscDisplaySingleMission";
  _none = GlobLoadTextureUI(cls >> "none");
  _done = GlobLoadTextureUI(cls >> "done");
  _locked = GlobLoadTextureUI(cls >> "locked");
  _packageImage = GlobLoadTextureUI(cls >> "package");

  _overviewLockedMission = Pars >> "overviewLockedMission";
  TerminateBy(_overviewLockedMission, '\\');
  _overviewMyMissions = Pars >> "overviewMyMissions";
  TerminateBy(_overviewMyMissions, '\\');
  _overviewNewMission = Pars >> "overviewNewMission";
  TerminateBy(_overviewNewMission, '\\');

  _configclass = missionsSpace;
  TerminateBy(root, '\\');
  _directory = _root =  root;
  
  Load("RscDisplaySingleMission");
  UpdateSaves();
  LoadDirectory();
  OnChangeMission();
  LoadParams();
  OnChangeDifficulty();
  _exitWhenClose = -1;
}

void DisplaySingleMission::LoadParams()
{
  _difficulty = Glob.config.diffDefault;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return;

  ConstParamEntryPtr entry = cfg.FindEntry("difficulty");
  if (entry)
  {
    RString name = *entry;
    int difficulty = Glob.config.diffNames.GetValue(name);
    if (difficulty >= 0) _difficulty = difficulty;
  }
}

void DisplaySingleMission::SaveParams()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return;
  cfg.Add("difficulty", Glob.config.diffNames.GetName(_difficulty));
  SaveUserParams(cfg);
}

void DisplaySingleMission::SaveLastMission()
{
  if (!_missionList) return;
  int sel = _missionList->GetCurSel();
  if (sel < 0) return;
  
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return;
  RString displayName = _missionList->GetText(sel);
  cfg.Add("lastSPMission", displayName);
  SaveUserParams(cfg);
}

Control *DisplaySingleMission::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_SINGLE_MISSION:
    _missionList = GetListBoxContainer(ctrl);
    break;
  case IDC_SINGLE_OVERVIEW:
    _overview = GetHTMLContainer(ctrl);
    ctrl->EnableCtrl(false);
    break;
  }

  return ctrl;
}

void DisplaySingleMission::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_SINGLE_MISSION)
  {
    OnChangeMission();
  }

  Display::OnLBSelChanged(ctrl, curSel);
}

void DisplaySingleMission::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_SINGLE_MISSION)
    OnButtonClicked(IDC_OK);
  else  
    Display::OnLBDblClick(idc, curSel);
}

static ConstParamEntryPtr GetMissionsList(const AutoArray<RString> &folder,RString missionClass)
{
  ConstParamEntryPtr ptr = (Pars >> "CfgMissions").FindEntry("Missions");
  if(missionClass.GetLength()>0) ptr = (Pars >> "CfgMissions").FindEntry(missionClass);

  if (!ptr) return NULL;
  for (int i=0; i<folder.Size(); i++)
  {
    ptr = ptr->FindEntry(folder[i]);
    if (!ptr) return NULL;
  }
  return ptr;
}

/*!
\patch 5133 Date 2/26/2007 by Jirka
- Fixed: UI - Single missions dialog title is changing by the selected folder
*/

void DisplaySingleMission::UpdateTitle()
{
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_SINGLE_TITLE));
  if (!text) return;

   if (_userMissions) text->SetText(LocalizeString(IDS_XBOX_WIZARD_MY_MISSIONS));
#if _ENABLE_MISSION_CONFIG
  else if (_folder.Size() > 0)
  {
    ConstParamEntryPtr list = GetMissionsList(_folder,_configclass);
    Assert(list);
    text->SetText((*list) >> "displayName");
  }
#endif
  else if (_directory.GetLength() > 0)
  {
    RString dir = _package ? _directory : RString("Missions\\") + _directory;
    LoadStringtable("Temporary", dir + RString("stringtable.csv"), 10, true);
    RString displayName = TryLocalizeString("STR_DIR_NAME");
    UnloadStringtable("Temporary");
    if (displayName.GetLength() == 0)
    {
      // get the directory name
      int last = _directory.GetLength() - 1;
      Assert(_directory[last] == '\\');
      RString name = _directory.Substring(0, last);
      const char *ext = strrchr(name, '\\');
      displayName = ext ? ext + 1 : cc_cast(name);
    }

    text->SetText(displayName);
  }
    else
      text->SetText(LocalizeString(IDS_DISP_SINGLE_TITLE));
}

#if _ENABLE_WIZARD

struct UserFileContext
{
  CListBoxContainer *listbox;
};

static bool AddUserFile(const FileItem &file, UserFileContext &ctx)
{
#if _FORCE_DEMO_ISLAND || !_ENABLE_UNSIGNED_MISSIONS
  ParamFile cfg;
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
# ifdef _XBOX
  cfg.ParseSigned(file.path + file.filename, NULL, NULL, &globals);
# else
  cfg.Parse(file.path + file.filename, NULL, NULL, &globals);
# endif
#endif

#if _FORCE_DEMO_ISLAND
  RString demo = Pars >> "CfgWorlds" >> "demoWorld";
  RString world = cfg >> "island";
  if (stricmp(world, demo) != 0) return false;
#endif // _FORCE_DEMO_ISLAND

#if !_ENABLE_UNSIGNED_MISSIONS
  bool noCopy = false;
  ConstParamEntryPtr entry = cfg.FindEntry("noCopy");
  if (entry) noCopy = *entry;
  // only template in addon is legal here
  if (!noCopy) return false; 
#endif // !_ENABLE_UNSIGNED_MISSIONS

  const char *filename = file.filename;
  const char *ext = strrchr(filename, '.');
  Assert(ext);
  Assert(stricmp(ext + 1, GetExtensionWizardMission()) == 0);
  RString name = DecodeFileName(RString(filename, ext - filename));
  int index = ctx.listbox->AddString(name);
  ctx.listbox->SetData(index, name);
  ctx.listbox->SetValue(index, DisplaySingleMission::ITTemplateOpen);
  return false; // continue with enumeration
}

void DisplaySingleMission::UpdateUserMissions(RString selected)
{
  if (selected.GetLength() == 0)
  {
    int sel = _missionList->GetCurSel();
    if (sel >= 0) selected = _missionList->GetText(sel);
  }

  _missionList->ClearStrings();

  int index = _missionList->AddString(LocalizeString(IDS_SINGLE_NEW_MISSION));
  _missionList->SetValue(index, ITTemplateNew);

#ifdef _XBOX

#if _XBOX_VER >= 200
  if (GSaveSystem.IsStorageAvailable() && GSaveSystem.CheckSaves())
  {
    for (int i=0; i<GSaveSystem.NSaves(); i++)
    {
      const SaveDescription &save = GSaveSystem.GetSave(i);
      if (save.fileName[0] == 'S' && save.fileName[1] == 'M') // Single-player Mission
      {
        // Check if the mission is not corrupted
        DWORD error = ERROR_SUCCESS;
        XSaveGame saveGame = GSaveSystem.OpenSave(save.fileName, &error);
        if (saveGame)
        {
          int index = _missionList->AddString(save.displayName);
          _missionList->SetValue(index, DisplaySingleMission::ITTemplateOpen);
          _missionList->SetData(index, save.fileName);
        }
        else if (error == ERROR_FILE_CORRUPT || error == ERROR_DISK_CORRUPT) // ignore the mission when unexpected error occurs
        {
          RString text = Format(LocalizeString(IDS_CORRUPTED_MISSION), cc_cast(save.displayName));
          index = _missionList->AddString(text);
          _missionList->SetValue(index, DisplaySingleMission::ITTemplateCorrupted);
          _missionList->SetData(index, save.fileName);
        }
      }
    }
  }
#else
  AutoArray<SaveHeader> list;
  SaveHeaderAttributes filter;
  filter.Add("type", FindEnumName(STMission));
  filter.Add("player", Glob.header.GetPlayerName());
  FindSaves(list, filter);
  ProgressRefresh();
  for (int i=0; i<list.Size(); i++)
  {
    SaveHeaderAttributes &attributes = list[i].attributes;
    RString mission;
    if (attributes.Find("mission", mission))
    {
      int index = -1;
      ParamFile file;
      RString filename = list[i].dir + RString("mission.") + GetExtensionWizardMission();
      if (file.ParseSigned(filename, NULL, NULL, &globals))
      {
        index = _missionList->AddString(mission);
        _missionList->SetValue(index, DisplaySingleMission::ITTemplateOpen);
        _missionList->SetData(index, list[i].dir);
      }
      else
      {
        RString text = Format(LocalizeString(IDS_CORRUPTED_MISSION), cc_cast(mission));
        index = _missionList->AddString(text);
        _missionList->SetValue(index, DisplaySingleMission::ITTemplateCorrupted);
        _missionList->SetData(index, mission);
      }
      ProgressRefresh();
    }
  }
#endif

#else
  UserFileContext ctx;
  ctx.listbox = _missionList;

  GDebugger.PauseCheckingAlive();
  ForMaskedFile(GetUserDirectory() + RString("Missions\\"), RString("*.") + GetExtensionWizardMission(), AddUserFile, ctx);
  GDebugger.ResumeCheckingAlive();
#endif

  _missionList->SortItemsByValue();

  int sel = 0;
  if (selected.GetLength() > 0)
  {
    for (int i=0; i<_missionList->GetSize(); i++)
    {
      if (stricmp(_missionList->GetText(i), selected) == 0)
      {
        sel = i;
        break;
      }
    }
  }
  _missionList->SetCurSel(sel);
}

#endif // _ENABLE_WIZARD

bool CheckKeys(ParamFile &f, const AutoArray<RString> &activeKeys)
{
  ParamEntryPtr keys = f.FindEntry("keys");
  if (!keys) return true;
  ParamEntryPtr keysLimit = f.FindEntry("keysLimit");
  if (!keysLimit) return true;

  int limit = *keysLimit;
  for (int i=0; i<keys->GetSize(); i++)
  {
    RString key = (*keys)[i];
    for (int j=0; j<activeKeys.Size(); j++)
    {
      if (stricmp(key, activeKeys[j]) == 0)
      {
        limit--;
        break;
      }
    }
  }

  return limit <= 0;
}

static bool DoneKeys(ParamFile &f, const AutoArray<RString> &activeKeys, const AutoArray<RString> &addDoneKeys)
{
  for (int i=0; i<addDoneKeys.Size(); i++)
  {
    RString key = addDoneKeys[i];
    for (int j=0; j<activeKeys.Size(); j++)
    {
      if (stricmp(key, activeKeys[j]) == 0) return true;
    }
  }

  ParamEntryPtr keys = f.FindEntry("doneKeys");
  if (!keys) return false;

  for (int i=0; i<keys->GetSize(); i++)
  {
    RString key = (*keys)[i];
    for (int j=0; j<activeKeys.Size(); j++)
    {
      if (stricmp(key, activeKeys[j]) == 0) return true;
    }
  }

  return false;
}

#if _ENABLE_UNSIGNED_MISSIONS

void DisplaySingleMission::AddMissionInBank(RString bankName, ItemType type)
{
  Assert(_missionList);

  const char *ext = strrchr(bankName, '.');
  if (!ext) return; // world name is missing
#if _FORCE_DEMO_ISLAND
  RString demo = Pars >> "CfgWorlds" >> "demoWorld";
  if (stricmp(ext + 1, demo) != 0) return;
#endif

  const char *searchStr = "briefingName";
  int searchLen = strlen(searchStr);

  // remove extension (.pbo)
  ext = strrchr(bankName, '\\');
  const char *name = ext ? ext + 1 : safe_cast<const char *>(bankName);

  // create bank (temporary)
  QFBank bank;
  bank.open(bankName);

  ProgressRefresh();

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile f;
  f.Parse(bank, "description.ext", NULL, &globals);
  if (!_showAll && !CheckKeys(f, _activeKeys))
  {
    int index = _missionList->AddString(LocalizeString(IDS_LOCKED_MISSION));
    _missionList->SetValue(index, ITLocked);
    _missionList->SetTexture(index, _locked);
    PackedColor color = _missionList->GetTextColor();
    _missionList->SetFtColor(index, PackedColorRGB(color, color.A8() / 2));
    color = _missionList->GetActiveColor();
    _missionList->SetSelColor(index, PackedColorRGB(color, color.A8() / 2));
    return;
  }

  AutoArray<RString> dummyDoneKeys;
  bool done = DoneKeys(f, _activeKeys, dummyDoneKeys);

  RString displayName = name;

  if (f.ParseBin(bank, "mission.sqm", NULL, &globals))
  {
    ParamEntryVal entry = f >> "Mission" >> "Intel";
    if (entry.FindEntry("briefingName")) displayName = entry >> "briefingName";
  }
  else
  {
    QIFStreamB file;
    file.open(bank, "mission.sqm");

    RString found = FastSearch(file, searchStr, searchLen);
    if (found.GetLength() > 0) displayName = found;
  }

  // load stringtable
  LoadStringtable("Temporary", bank, "stringtable.csv", 10, true);
  int index = _missionList->AddString(Localize(displayName));
  UnloadStringtable("Temporary");

  _missionList->SetData(index, type == ITMissionBank ? RString(name) : bankName);
  _missionList->SetValue(index, type);
  if (done)
    _missionList->SetTexture(index, _done);
  else
    _missionList->SetTexture(index, _none);
}

#endif //_ENABLE_UNSIGNED_MISSIONS

void DisplaySingleMission::AddMissionInAddon(ParamEntryVal entry)
{
  const char *searchStr = "briefingName";
  int searchLen = strlen(searchStr);

  ProgressRefresh();

  RString dir = entry >> "directory";

  #if _VERIFY_KEY
  if (CheckProductId(ProductRFT))
  {
  RString dirLow = dir;
  dirLow.Lower();
  if (strstr(dirLow,"ca\\missions_e\\scenarios\\")!=0) return;
  if (strstr(dirLow,"ca\\missions_e\\campaign\\")!=0) return;
  }
  #endif

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile f;
  f.Parse(dir + RString("\\description.ext"), NULL, NULL, &globals);
  if (!_showAll && !CheckKeys(f, _activeKeys))
  {
    int index = _missionList->AddString(LocalizeString(IDS_LOCKED_MISSION));
    _missionList->SetValue(index, ITLocked);
    _missionList->SetTexture(index, _locked);
    PackedColor color = _missionList->GetTextColor();
    _missionList->SetFtColor(index, PackedColorRGB(color, color.A8() / 2));
    color = _missionList->GetActiveColor();
    _missionList->SetSelColor(index, PackedColorRGB(color, color.A8() / 2));
    return;
  }

  AutoArray<RString> addDoneKeys;
  ParamEntryPtr doneKeys = entry.FindEntry("doneKeys");
  if (doneKeys && doneKeys->IsArray())
  {
    int size = doneKeys->GetSize();
    addDoneKeys.Realloc(size);
    addDoneKeys.Resize(size);
    for (int i=0; i<doneKeys->GetSize(); i++)
    {
      addDoneKeys[i] = (*doneKeys)[i];
    }
  }

  bool done = DoneKeys(f, _activeKeys, addDoneKeys);

  RString name = entry.GetName();
  RString displayName = name;

  if ( entry.FindEntry("briefingName") )
  {
    displayName = entry >> "briefingName";
  }
  else if (f.ParseBin(dir + RString("\\mission.sqm"), NULL, NULL, &globals))
  {
    ParamEntryVal entry = f >> "Mission" >> "Intel";
    if (entry.FindEntry("briefingName")) displayName = entry >> "briefingName";
  }
  else
  {
    QIFStreamB file;
    file.AutoOpen(dir + RString("\\mission.sqm"));
    if (file.fail())
    {
      RptF("Invalid mission directory %s referenced by %s", cc_cast(dir), cc_cast(entry.GetContext()));
      return; // wrong directory reference
    }

    RString found = FastSearch(file, searchStr, searchLen);
    if (found.GetLength() > 0) displayName = found;
  }

  // load stringtable
  LoadStringtable("Temporary", dir + RString("\\stringtable.csv"), 10, true);
  int index = _missionList->AddString(Localize(displayName));
  UnloadStringtable("Temporary");

  _missionList->SetData(index, name);
  _missionList->SetValue(index, ITAddon);
  if (done)
    _missionList->SetTexture(index, _done);
  else
    _missionList->SetTexture(index, _none);
}

/*!
\patch 1.75 Date 2/6/2002 by Jirka
- Added: directory structure at single missions 
*/

void DisplaySingleMission::LoadDirectory()
{
  UpdateTitle();

  if (!_missionList) return;

  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));

#ifdef _WIN32

#if _ENABLE_WIZARD
  if (_userMissions)
  {
    UpdateUserMissions(RString());
    ProgressFinish(p);
    return;
  }
#endif // _ENABLE_WIZARD

  if (_package)
  {
    _missionList->ClearStrings();
#ifdef _XBOX
    _finddata_t info;
    long h = _findfirst(_directory + RString("*.*"), &info);  // t: drive
    if (h != -1)
    {
      do
      {
        if ((info.attrib & _A_SUBDIR) == 0)
        {
          char *ext = strrchr(info.name, '.');
          if (stricmp(ext, ".pbo") != 0) continue;
          RString bankName = _directory + RString(info.name, ext - info.name);
          AddMissionInBank(bankName, ITDownloaded);
        }
      }
      while (_findnext(h, &info) == 0);
      _findclose(h);
    }
    _missionList->SortItemsByValue();
    _missionList->SetCurSel(0);
#endif
    ProgressFinish(p);
    return;
  }

  _activeKeys.Clear();
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    ParamEntryPtr array = cfg.FindEntry("activeKeys");
    if (array)
    {
      for (int i=0; i<array->GetSize(); i++)
        _activeKeys.Add((*array)[i]);
    }
  }

  RString curData;
  int curValue = 0;
  int sel = _missionList->GetCurSel();
  if (sel >= 0)
  {
    curData = _missionList->GetData(sel);
    curValue = _missionList->GetValue(sel);
  }

  _missionList->ClearStrings();

#if _ENABLE_MISSION_CONFIG
  if (_folder.Size() > 0)
  {
    ConstParamEntryPtr list = GetMissionsList(_folder,_configclass);
    if (list)
    {
      for (int i=0; i<list->GetEntryCount(); i++)
      {
        ParamEntryVal entry = list->GetEntry(i);
        if (!entry.IsClass()) continue;
        ConstParamEntryPtr ptr = entry.FindEntry("hidden");
        if (ptr)
        {
          bool hidden = *ptr;
          if (hidden) continue;
        }
        if (entry.FindEntry("directory"))
        {
          // mission
          AddMissionInAddon(entry);
        }
        else
        {
          RString displayName = entry >> "displayName";
          int index = _missionList->AddString(displayName + RString("..."));
          _missionList->SetData(index, entry.GetName());
          _missionList->SetValue(index, ITAddonFolder);
          _missionList->SetTexture(index, _none);
        }
      }
    }
    return;
  }
#endif

#if _ENABLE_WIZARD
#if defined _XBOX && _XBOX_VER >= 200
  if (_directory.GetLength() == 0 && GSaveSystem.IsStorageAvailable())
#else
  if (_directory.GetLength() == 0 && _configclass.GetLength() == 0)
#endif
  {
    // My missions
    int index = _missionList->AddString(LocalizeString(IDS_XBOX_WIZARD_MY_MISSIONS) + RString("..."));
    _missionList->SetValue(index, ITUserMissions);
    _missionList->SetTexture(index, _none);
    if (Glob.demo)
    {
      PackedColor color = _missionList->GetTextColor();
      _missionList->SetFtColor(index, PackedColorRGB(color, color.A8() / 2));
      color = _missionList->GetActiveColor();
      _missionList->SetSelColor(index, PackedColorRGB(color, color.A8() / 2));
    }
  }
#endif

#if _ENABLE_MISSION_CONFIG
  if (_directory.GetLength() == 0)
  {
    // Missions in addons
    ParamEntryVal  list = Pars >> "CfgMissions" >> "Missions";
    if(_configclass.GetLength()>0) list = Pars >> "CfgMissions" >> _configclass;

    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      if (!entry.IsClass()) continue;

      ConstParamEntryPtr ptr = entry.FindEntry("condition");
      if (ptr)
      {
        ParamEntryVal cnd = *ptr;
        if (cnd.IsArray())
        {
          bool exist = true;
          for (int i=0; i< cnd.GetSize();++i)
            if (!(Pars >> "CfgPatches").FindEntry((RString)cnd[i])) exist = false;

          if (!exist) continue;
        }
        else if (cnd.IsTextValue())
        {
          if (!(Pars >> "CfgPatches").FindEntry((RString)cnd)) continue;
        }
      }

      ptr = entry.FindEntry("hidden");
      if (ptr)
      {
        bool hidden = *ptr;
        if (hidden) continue;
      }
      if (entry.FindEntry("directory"))
      {
        // mission
        AddMissionInAddon(entry);
      }
      else
      {
        RString displayName = entry >> "displayName";
        int index = _missionList->AddString(displayName + RString("..."));
        _missionList->SetData(index, entry.GetName());
        _missionList->SetValue(index, ITAddonFolder);
        _missionList->SetTexture(index, _none);
      }
    }
  }
#endif

  if(_configclass.GetLength() == 0)
  {
#if _ENABLE_UNSIGNED_MISSIONS
  RString dir = RString("Missions\\") + _directory;
  _finddata_t info;

  // first search for .pbo files
  #ifdef _XBOX
    long h = X_findfirst(dir + RString("*"), &info);
  #else
    long h = _findfirst(dir + RString("*.pbo"), &info);  // not used on Xbox
  #endif
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) == 0)
      {
        char *ext = strrchr(info.name, '.');
        if (stricmp(ext, ".pbo") != 0) continue;
        RString bankName = dir + RString(info.name, ext - info.name);
        AddMissionInBank(bankName, ITMissionBank);
        ProgressRefresh();
      }
    }
    while (0==_findnext(h, &info));
    _findclose(h);
  }
  // search for missions is subdirectories and for folders of missions
  h = X_findfirst(dir + RString("*.*"), &info);
  if (h != -1)
  {
    const char *searchStr = "briefingName";
    int searchLen = strlen(searchStr);
    do
    {
      if 
      (
        (info.attrib & _A_SUBDIR) != 0 &&
        info.name[0] != '.'
      )
      {
        RString name = info.name;

        const char *ext = strrchr(name, '.');
        if (ext)
        {
          // Mission directory
          ProgressRefresh();

#if _FORCE_DEMO_ISLAND
          RString demo = Pars >> "CfgWorlds" >> "demoWorld";
          if (stricmp(ext + 1, demo) != 0) return;
#endif
          GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

          bool done = false;
          {
            RString filename = dir + name + RString("\\description.ext");
            ParamFile f;
            f.Parse(filename, NULL, NULL, &globals);
            if (!_showAll && !CheckKeys(f, _activeKeys))
            {
              int index = _missionList->AddString(LocalizeString(IDS_LOCKED_MISSION));
              _missionList->SetValue(index, ITLocked);
              _missionList->SetTexture(index, _locked);
              PackedColor color = _missionList->GetTextColor();
              _missionList->SetFtColor(index, PackedColorRGB(color, color.A8() / 2));
              color = _missionList->GetActiveColor();
              _missionList->SetSelColor(index, PackedColorRGB(color, color.A8() / 2));
              continue;
            }
            AutoArray<RString> dummyDoneKeys;
            done = DoneKeys(f, _activeKeys, dummyDoneKeys);
          }

          RString filename = dir + name + RString("\\mission.sqm");
          RString stringtable = dir + name + RString("\\stringtable.csv");

          ParamFile f;
          if (f.ParseBin(filename, NULL, NULL, &globals))
          {
            ParamEntryVal entry = f >> "Mission" >> "Intel";
            if (entry.FindEntry("briefingName")) name = entry >> "briefingName";
          }
          else
          {
            QIFStream file;
            file.open(filename);

            RString found = FastSearch(file, searchStr, searchLen);
            if (found.GetLength() > 0) name = found;
          }
          LoadStringtable("Temporary", stringtable, 10, true);
          int index = _missionList->AddString(Localize(name));
          UnloadStringtable("Temporary");
          _missionList->SetData(index, info.name);
          _missionList->SetValue(index, ITMissionDirectory);
          _missionList->SetTexture(index, done ? _done : _none);
        }
#if _ENABLE_DATADISC
        else
        {
          // Subdirectory
          LoadStringtable("Temporary", dir + name + RString("\\stringtable.csv"), 10, true);
          RString displayName = TryLocalizeString("STR_DIR_NAME");
          if (displayName.GetLength() == 0) displayName = name;
          UnloadStringtable("Temporary");

          int index = _missionList->AddString(displayName + RString("..."));
          _missionList->SetData(index, name);
          _missionList->SetValue(index, ITSubdirectory);
          _missionList->SetTexture(index, _none);
        }
#endif
        ProgressRefresh();
      }
    }
    while (0==_findnext(h, &info));
    _findclose(h);
  }
#endif // _ENABLE_UNSIGNED_MISSIONS
  }

#if defined _XBOX && _ENABLE_MP

#if _XBOX_VER >= 200
  // TODOXNET: Downloaded content (SP missions)
#else
  // downloaded missions
  if (_directory.GetLength() == 0)
  {
    XCONTENT_FIND_DATA data;
    HANDLE handle = XFindFirstContent("T:\\", 0x0001, &data);
    if (handle != INVALID_HANDLE_VALUE)
    {
      do
      {
        bool CheckContent(const char *dir);
        if (CheckContent(data.szContentDirectory))
        {
          RString dir = RString(data.szContentDirectory) + RString("\\Missions\\");
          if (QIFileFunctions::DirectoryExists(dir))
          {
            LoadStringtable("Temporary", dir + RString("stringtable.csv"), 10, true);
            RString displayName = LocalizeString("STR_DIR_NAME");
            UnloadStringtable("Temporary");

            int index = _missionList->AddString(displayName + RString("..."));
            _missionList->SetData(index, dir);
            _missionList->SetValue(index, ITPackage);
            _missionList->SetTexture(index, _packageImage);
          }
        }
      } while (XFindNextContent(handle, &data));
      XFindClose(handle);
    }
  }
#endif // _XBOX_VER >= 200
#endif // defined _XBOX && _ENABLE_MP

#if _VBS3 // sort mission lists alphabetically
  _missionList->SortItems();
#else
  _missionList->SortItemsByValue();
#endif
  sel = 0;
  if (curData.GetLength() > 0)
  {
    for (int i=0; i<_missionList->Size(); i++)
    {
      if (_missionList->GetValue(i) == curValue && stricmp(_missionList->GetData(i), curData) == 0)
      {
        sel = i;
        break;
      }
    }
  }
  _missionList->SetCurSel(sel);
#endif                // _WIN32
  ProgressFinish(p);
}

//! find overview file for current language
RString GetOverviewFile(RString dir)
{
  RString prefix = dir + RString("overview.");
  RString name = prefix + GLanguage + RString(".html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  name = prefix + RString("html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  return RString();
}

//! find template overview file for current language
RString GetTemplateFile(RString dir)
{
  RString prefix = dir + RString("template.");
  RString name = prefix + GLanguage + RString(".html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  name = prefix + RString("html");
  if (QFBankQueryFunctions::FileExists(name)) return name;
  return RString();
}

RString GetSaveFilename(SaveGameType type)
{
  switch (type)
  {
  case SGTAutosave:
    return RString("autosave.") + GetExtensionSave();
  case SGTUsersave:
    return RString("save.") + GetExtensionSave();
  case SGTContinue:
    return RString("continue.") + GetExtensionSave();
  case SGTUsersave2:
    return RString("save2.") + GetExtensionSave();
  case SGTUsersave3:
    return RString("save3.") + GetExtensionSave();
  case SGTUsersave4:
    return RString("save4.") + GetExtensionSave();
  case SGTUsersave5:
    return RString("save5.") + GetExtensionSave();
  case SGTUsersave6:
    return RString("save6.") + GetExtensionSave();
  }
  Fail("Save game type");
  return RString();
}


//! Select which save to use as continue of mission
SaveGameType SelectSaveGame(RString dir)
{
#ifdef _WIN32
  RString mpCampaignPrefix = RString("");
  RString resume = dir + mpCampaignPrefix + RString("continue.") + GetExtensionSave();
  if (QIFileFunctions::FileExists(resume)) return SGTContinue;

  // Find the latest from user saves and autosave
  SaveGameType saves[] = {SGTAutosave, SGTUsersave, SGTUsersave2, SGTUsersave3, SGTUsersave4, SGTUsersave5, SGTUsersave6};

  SaveGameType bestSave = (SaveGameType)-1;
  QFileTime bestTime = 0;
  for (int i=0; i<lenof(saves); i++)
  {
    RString save = dir + mpCampaignPrefix + GetSaveFilename(saves[i]);
    if (!QIFileFunctions::FileExists(save)) continue;
    // find when this save was created
    QFileTime time = QIFileFunctions::TimeStamp(save);
    // check if this is later than the best found
    if (bestSave == -1 || time > bestTime)
    {
      bestSave = saves[i];
      bestTime = time;
    }
  }
  return bestSave;
#else
  return (SaveGameType)-1;
#endif
}

void DisplaySingleMission::UpdateSaves()
{
#ifdef _XBOX
# if _XBOX_VER >= 200
  // nothing to do - check saves directly by filename
# else
  SaveHeaderAttributes filter;
  filter.Add("type", FindEnumName(STGameSave));
  filter.Add("player", Glob.header.GetPlayerName());
  filter.Add("parameters", RString());
  filter.Add("campaign", RString());
  
  _saves.Resize(0);

  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
  GDebugger.PauseCheckingAlive();

  FindSaves(_saves, filter);

  GDebugger.ResumeCheckingAlive();
  ProgressFinish(p);
# endif
#endif
}

#if defined _XBOX && _XBOX_VER >= 200

XSaveGame DisplaySingleMission::GetMissionSave(RString dir) const
{
  if (!GSaveSystem.IsStorageAvailable()) return NULL;

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile file;
  file.Parse(dir + RString("description.ext"), NULL, NULL, &globals);

  // get the mission index
  int missionIndex = 0;
  ConstParamEntryPtr entry = file.FindEntry("missionId");
  if (entry) missionIndex = *entry;
  else
  {
    RptF("Mission %s - no missionId set", cc_cast(dir));
    missionIndex = CalculateStringHashValueCI(dir.GetKey());
  }
  DoAssert(missionIndex != 0);

  // file name
  int campaignIndex = 0;
  __int64 xuid = 0;
  RString fileName = Format("SS%016I64X%08X%08X", xuid, campaignIndex, missionIndex);

  return GSaveSystem.OpenSave(fileName);
}

#else

RString DisplaySingleMission::FindMissionSaveDirectory(RString dir) const
{
#ifdef _XBOX
  RString name = dir;
  for (int i=0; i<_saves.Size(); i++)
  {
    RString mission;
    _saves[i].attributes.Find("mission", mission);
    if (stricmp(mission, name) == 0)
      return _saves[i].dir;
  }
  return RString();
#else
  return GetMissionSaveDirectory(dir);
#endif
}

#endif

void DisplaySingleMission::OnChangeMission()
{
  _saveType = (SaveGameType)-1;

  IControl *ctrlDelete = GetCtrl(IDC_SINGLE_DELETE);
  IControl *ctrlCopy = GetCtrl(IDC_SINGLE_COPY);
  IControl *ctrlEdit = GetCtrl(IDC_SINGLE_EDIT);
  IControl *ctrlOK = GetCtrl(IDC_OK);
  IControl *ctrlLoad = GetCtrl(IDC_SINGLE_LOAD);
  ITextContainer *textOK = GetTextContainer(ctrlOK);
  ITextContainer *textLoad = GetTextContainer(ctrlLoad);

#ifdef _WIN32
  if (_overview) _overview->Init();
  
  if (!_missionList)
  {
    if (ctrlDelete) ctrlDelete->ShowCtrl(false);
    if (ctrlCopy) ctrlCopy->ShowCtrl(false);
    if (ctrlEdit) ctrlEdit->ShowCtrl(false);
    if (ctrlOK) ctrlOK->ShowCtrl(false);
    if (ctrlLoad) ctrlLoad->ShowCtrl(false);
    return;
  }
  int sel = _missionList->GetCurSel();
  if (sel < 0)
  {
    if (ctrlDelete) ctrlDelete->ShowCtrl(false);
    if (ctrlCopy) ctrlCopy->ShowCtrl(false);
    if (ctrlEdit) ctrlEdit->ShowCtrl(false);
    if (ctrlOK) ctrlOK->ShowCtrl(false);
    if (ctrlLoad) ctrlLoad->ShowCtrl(false);
    return;
  }
  int type = _missionList->GetValue(sel);

#if _ENABLE_WIZARD
  if (ctrlDelete) ctrlDelete->ShowCtrl(type == ITTemplateOpen || type == ITTemplateCorrupted);
  if (ctrlCopy) ctrlCopy->ShowCtrl(type == ITTemplateOpen);
  if (ctrlEdit) ctrlEdit->ShowCtrl(type == ITTemplateOpen);
#else
  if (ctrlDelete) ctrlDelete->ShowCtrl(false);
  if (ctrlCopy) ctrlCopy->ShowCtrl(false);
  if (ctrlEdit) ctrlEdit->ShowCtrl(false);
#endif // _ENABLE_WIZARD

#if _ENABLE_WIZARD
  if (_userMissions)
  {
    if (type == ITTemplateNew)
    {
      if (ctrlOK)
      {
        ctrlOK->ShowCtrl(true);
        if (textOK) textOK->SetText(LocalizeString(IDS_SINGLE_CREATE));
      }
      if (ctrlLoad) ctrlLoad->ShowCtrl(false);
      if (_overview)
      {
        RString filename = GetOverviewFile(_overviewNewMission);
        _overview->Load(filename, false);
      }
      return;
    }
    else if (type == ITTemplateCorrupted)
    {
      if (ctrlOK) ctrlOK->ShowCtrl(false);
      if (ctrlLoad) ctrlLoad->ShowCtrl(false);
      return;
    }
    else
    {
      Assert(type == ITTemplateOpen);
      if (ctrlOK)
      {
        ctrlOK->ShowCtrl(true);
        if (textOK) textOK->SetText(LocalizeString(IDS_SINGLE_PLAY));
      }
      if (ctrlLoad) ctrlLoad->ShowCtrl(false);
      if (_overview)
      {
        GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

        ParamFile file;
#ifdef _XBOX

# if _XBOX_VER >= 200
        RString saveName = _missionList->GetData(sel);
        XSaveGame save = GSaveSystem.OpenSave(saveName);
        if (!save)
        {
          // Errors already handled
          return;
        }
        RString filename = RString(SAVE_ROOT) + RString("mission.") + GetExtensionWizardMission();
        if (file.Parse(filename, NULL, NULL, &globals) != LSOK)
        {
          ReportUserFileError(saveName, RString("mission.") + GetExtensionWizardMission(), _missionList->GetText(sel));
          return;
        }
# else
        RString parname = _missionList->GetData(sel) + RString("mission.") + GetExtensionWizardMission();
        if (!file.ParseSigned(parname, NULL, NULL, &globals))
        {
          const char *ptr = strrchr(parname, '\\');
          ptr++;
          RString dir = parname.Substring(0, ptr - (const char *)parname);

          ReportUserFileError(dir, ptr, RString());
          return;
        }
# endif
#else
        RString parname =
          GetUserDirectory() + RString("Missions\\") + EncodeFileName(_missionList->GetData(sel)) + RString(".") + GetExtensionWizardMission();
        file.Parse(parname, NULL, NULL, &globals);
#endif
        RString templ = file >> "template";
#if _ENABLE_UNSIGNED_MISSIONS
        bool noCopy = false;
        ConstParamEntryPtr entry = file.FindEntry("noCopy");
        if (entry) noCopy = *entry;

        if (!noCopy && QIFileFunctions::FileExists(templ + RString(".pbo")))
        {
          // bank
          RString bank = CreateSingleMissionBank(templ);
          if (bank.GetLength() > 0)
          {
            RString filename = GetOverviewFile(bank);
            _overview->Load(filename, false);
          }
        }
        else
#endif // _ENABLE_UNSIGNED_MISSIONS
        {
          // directory
          RString filename = GetOverviewFile(templ + RString("\\"));
          _overview->Load(filename, false);
        }
      }
      return;
    }
  }
#endif // _ENABLE_WIZARD

  RString mission = _missionList->GetData(sel);

  if (type == ITLocked)
  {
    if (ctrlOK) ctrlOK->ShowCtrl(false);
    if (ctrlLoad) ctrlLoad->ShowCtrl(false);

    if (_overview)
    {
      RString filename = GetOverviewFile(_overviewLockedMission);
      _overview->Load(filename, false);
    }
    return;
  }

#if _ENABLE_WIZARD
  if (type == ITSubdirectory || type == ITUserMissions || type == ITPackage || type == ITAddonFolder)
#else
  if (type == ITSubdirectory || type == ITPackage || type == ITAddonFolder)
#endif
  {
    if (_overview)
    {
      if (type == ITSubdirectory)
      {
        RString directory = RString("missions\\") + _directory + mission;
        RString filename = GetOverviewFile(directory + RString("\\"));
        if (filename.GetLength() > 0)
          _overview->Load(filename, false);
      }
#if _ENABLE_MISSION_CONFIG
      else if (type == ITAddonFolder)
      {
        ConstParamEntryPtr ptr = GetMissionsList(_folder,_configclass);
        if (ptr)
        {
          ptr = ptr->FindEntry(mission);
          if (ptr)
          {
            RString filename = (*ptr) >> "overview";
            if (filename.GetLength() > 0)
              _overview->Load(filename, false);
          }
        }
      }
#endif
      else if (type == ITPackage)
      {
        RString filename = GetOverviewFile(mission + RString("\\"));
        if (filename.GetLength() > 0)
          _overview->Load(filename, false);
      }
      else
      {
        RString filename = GetOverviewFile(_overviewMyMissions);
        _overview->Load(filename, false);
      }
    }

    if (ctrlOK)
    {
      ctrlOK->ShowCtrl(true);
      if (textOK) textOK->SetText(LocalizeString(IDS_SINGLE_OPEN));
    }
    if (ctrlLoad) ctrlLoad->ShowCtrl(false);

    return;
  }
  
  RString filename;
#if defined _XBOX && _XBOX_VER >= 200
  XSaveGame save;
#else
  RString dir;
#endif
  if (type == ITDownloaded)
  {
#if _ENABLE_UNSIGNED_MISSIONS
    RString bank = CreateSingleMissionBank(mission);
    if (bank.GetLength() == 0) return;
    filename = GetOverviewFile(bank);
#ifdef _XBOX
# if _XBOX_VER >= 200
    // TODOXNET: Downloaded content (SP missions)
# else
    for (int i=0; i<_saves.Size(); i++)
    {
      RString name;
      _saves[i].attributes.Find("mission", name);
      if (stricmp(name, mission) == 0)
      {
        dir = _saves[i].dir;
        break;
      }
    }
# endif
#endif
#endif // _ENABLE_UNSIGNED_MISSIONS
  }
#if _ENABLE_MISSION_CONFIG
  else if (type == ITAddon)
  {
    ConstParamEntryPtr ptr = GetMissionsList(_folder,_configclass);
    if (ptr)
    {
      ptr = ptr->FindEntry(mission);
      if (ptr)
      {
        RString directory = (*ptr) >> "directory";
        filename = GetOverviewFile(directory + RString("\\"));
#if defined _XBOX && _XBOX_VER >= 200
        save = GetMissionSave(directory);
#else
        dir = FindMissionSaveDirectory(directory);
#endif
      }
    }
  }
#endif
#if _ENABLE_UNSIGNED_MISSIONS
  else
  {
    RString directory = RString("missions\\") + _directory + mission;
    if (type == ITMissionBank)
    {
      RString bank = CreateSingleMissionBank(directory);
      if (bank.GetLength() == 0) return;
      filename = GetOverviewFile(bank);
    }
    else
    {
      filename = GetOverviewFile(directory + RString("\\"));
    }
#if defined _XBOX && _XBOX_VER >= 200
    save = GetMissionSave(directory);
#else
    dir = FindMissionSaveDirectory(directory);
#endif
  }
#endif
  if (_overview && filename.GetLength() > 0)
    _overview->Load(filename, false);

  // update buttons
#if defined _XBOX && _XBOX_VER >= 200
  RString dir = SAVE_ROOT;
  if (save) _saveType = SelectSaveGame(SAVE_ROOT); // Errors already handled
#else
  if (dir.GetLength() > 0) _saveType = SelectSaveGame(dir);
#endif
  if (_saveType >= 0)
  {
    RString save = dir + GetSaveFilename(_saveType);

    QFileTime time = QIFileFunctions::TimeStamp(save);
    FILETIME info = ConvertToFILETIME(time);
    FILETIME infoLocal;
    ::FileTimeToLocalFileTime(&info, &infoLocal);
    SYSTEMTIME stime;
    ::FileTimeToSystemTime(&infoLocal, &stime);

    int day = stime.wDay;
    int month = stime.wMonth;
    int year = stime.wYear;
    int minute = stime.wMinute;
    int hour = stime.wHour;
    RString text = Format(LocalizeString(IDS_SINGLE_RESUME), month, day, year, hour, minute);

    if (ctrlOK)
    {
      ctrlOK->ShowCtrl(true);
      if (textOK) textOK->SetText(text);
    }
    if (ctrlLoad)
    {
      ctrlLoad->ShowCtrl(true);
      if (textLoad) textLoad->SetText(LocalizeString(IDS_SINGLE_RESTART));
    }

  }
  else
  {
    if (ctrlOK)
    {
      ctrlOK->ShowCtrl(true);
      if (textOK) textOK->SetText(LocalizeString(IDS_SINGLE_PLAY));
    }
    if (ctrlLoad) ctrlLoad->ShowCtrl(false);

  }
#endif
}

void DisplaySingleMission::OnChangeDifficulty()
{
#ifdef _WIN32
  ITextContainer *ctrl = GetTextContainer(GetCtrl(IDC_SINGLE_DIFF));
  if (!ctrl) return;
  RString text = Glob.config.diffSettings[_difficulty].displayName;
  ctrl->SetText(text);
#endif
}

void DisplaySingleMission::OnLoadMission()
{
  if (!_missionList) return;

  int sel = _missionList->GetCurSel();
  Assert(sel >= 0);

  int type = _missionList->GetValue(sel);
  RString mission = _missionList->GetData(sel);

  CurrentMission = "";

#if _ENABLE_MISSION_CONFIG
  if (type == ITAddon)
  {
    ConstParamEntryPtr ptr = GetMissionsList(_folder,_configclass);
    if (ptr)
    {
      ptr = ptr->FindEntry(mission);
      if (ptr) SelectMission(*ptr);
    }
  }
  else
#endif
  {
    const char *ptr = mission;
    const char *ext = strrchr(ptr, '.');
    Assert(ext);
    RString name = mission.Substring(0, ext - ptr);
    RString world = ext + 1;
    if (type == ITDownloaded)
    {
      SetMission(world, name, RString(""));
    }
#if _ENABLE_UNSIGNED_MISSIONS
    else
    {
      SetMission(world, name, RString("missions\\") + GetDirectory());
    }
#endif
  }
  SaveLastMission();
  LSError err = GWorld->LoadGame(_saveType, true);
  if (err != LSOK)
  {
    if (err != LSNoAddOn) // this error was handled already
    {
      // process warning
      UserFileErrorInfo info;
      info._file = GetSaveFilename(_saveType);
#if defined _XBOX && _XBOX_VER >= 200
      if (err != LSDiskError)
      {
      RString GetSaveFileName();
      RString GetSaveDisplayName();
      info._dir = GetSaveFileName();
      info._name = GetSaveDisplayName();
        MsgBox *CreateUserFileError(ControlsContainer *parent, const UserFileErrorInfo &error, bool canDelete);
        SetMsgBox(CreateUserFileError(this, info, true));
      }
#else
      RString FindSaveGameName(RString dir, RString file);
      info._dir = GetSaveDirectory();
      info._name = FindSaveGameName(info._dir, info._file);
      MsgBox *CreateUserFileError(ControlsContainer *parent, const UserFileErrorInfo &error, bool canDelete);
      SetMsgBox(CreateUserFileError(this, info, true));
#endif
    }
    return;
  }
#ifndef _XBOX
  GWorld->DeleteSaveGame(SGTContinue);
#endif
  GWorld->FadeInMission();

  CreateChild(new DisplayMission(this, false, false));

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // Single mission started
  GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_SINGLEMISSION);
#endif
}

void DisplaySingleMission::OnRestartMission()
{
  if (!_missionList) return;

  int sel = _missionList->GetCurSel();
  Assert(sel >= 0);

  // Return to previous game is not availiable now
  Glob.config.difficulty = _difficulty;

  // TODO: different handling for a scripted mission
  CurrentTemplate = new ArcadeTemplate;
  if (GWorld->UI()) GWorld->UI()->Init();

  CurrentMission = "";

  int type = _missionList->GetValue(sel);
  RString name = _missionList->GetData(sel);
#if _ENABLE_WIZARD
  if (type == ITTemplateOpen)
  {
#ifdef _XBOX
# if _XBOX_VER >= 200
    RString filename = name;
# else
    RString filename = name + RString("mission.") + GetExtensionWizardMission();
# endif
#else
    RString filename =
      GetUserDirectory() + RString("Missions\\") + EncodeFileName(name) + RString(".") + GetExtensionWizardMission();
#endif
    SetMissionParams(filename, _missionList->GetText(sel), false);

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    // User mission started
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_USERMISSION);
#endif
  }
  else
#endif // _ENABLE_WIZARD
  {
    if (type == ITDownloaded)
    {
#if _ENABLE_UNSIGNED_MISSIONS
      RString filename = name;
      char buffer[1024];
      strcpy(buffer, CreateSingleMissionBank(filename));
      if (*buffer == 0) return;
      char *str = strrchr(buffer, '\\');
      Assert(str);
      *str = 0;
      char *world = strrchr(buffer, '.');
      Assert(world);
      *world = 0;
      world++;
/*
      const char *mission = strrchr(buffer, '\\');
      Assert(mission);
      mission++;
      SetMission(world, mission);
*/
      // FIX: ensure BaseDirectory + BaseSubdirectory + Glob.header.filenameReal is the correct filename
      SetMission(world, buffer, RString());
      // do not include island name to filenameReal
      const char *ext = strrchr(filename, '.');
      Glob.header.filenameReal = RString(filename, ext - (const char *)filename);
#endif
    }
#if _ENABLE_MISSION_CONFIG
    else if (type == ITAddon)
    {
      ConstParamEntryPtr ptr = GetMissionsList(_folder,_configclass);
      if (ptr)
      {
        ptr = ptr->FindEntry(name);
        if (ptr) SelectMission(*ptr);
      }
      SaveLastMission();
    }
#endif
#if _ENABLE_UNSIGNED_MISSIONS
    else
    {
      RString dir = RString("missions\\") + GetDirectory();
      if (type == ITMissionBank)
      {
        RString filename = name;
        RString directory = dir + filename;
        char buffer[1024];
        strcpy(buffer, CreateSingleMissionBank(directory));
        if (*buffer == 0) return;
        char *str = strrchr(buffer, '\\');
        Assert(str);
        *str = 0;
        char *world = strrchr(buffer, '.');
        Assert(world);
        *world = 0;
        world++;
        const char *mission = strrchr(buffer, '\\');
        Assert(mission);
        mission++;
        SetMission(world, mission);
        strcpy(buffer, filename);
        world = strrchr(buffer, '.');
        Assert(world);
        *world = 0;
        Glob.header.filenameReal = GetDirectory() + RString(buffer);
      }
      else
      {
        RString mission = name;
        if (!ProcessTemplateName(mission, dir)) return;
      }
    }
#endif // _ENABLE_UNSIGNED_MISSIONS
    SaveLastMission();

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    // Single mission started
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_SINGLEMISSION);
#endif
  }

  GStats.ClearAll();
  if (!LaunchIntro(this)) 
  {
    if (!OnIntroFinished(this, IDC_CANCEL))
    {
      LoadDirectory();
      UpdateSaves();
      OnChangeMission();
    }
  }
}

void DisplaySingleMission::OnButtonClicked(int idc)
{
#ifdef _WIN32
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_SINGLE)
    Exit(idc);
#endif
  switch (idc)
  {
  case IDC_SINGLE_LOAD:
    {
      if (_userMissions)
      {
        // continue with IDC_OK
      }
      else
      {
        if (!_missionList) return;
        int sel = _missionList->GetCurSel();
        if (sel < 0) return;
        int type = _missionList->GetValue(sel);
        if (type == ITLocked) return;
#if _ENABLE_WIZARD
        if (type == ITSubdirectory || type == ITUserMissions || type == ITPackage || type == ITAddonFolder)
#else
        if (type == ITSubdirectory || type == ITPackage || type == ITAddonFolder)
#endif
        {
          // continue with IDC_OK
        }
        else
        {
          if (!CheckDiskSpace(this, "U:\\", CHECK_SPACE_SAVE)) break;

          if (_saveType >= 0)
          {
            // confirm mission restart
            MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
            MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
            CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RESTART_MISSION, false, &buttonOK, &buttonCancel);
            break; // done;
          }
          // continue with IDC_OK
        }
      }
    }
  // note: no break here
  case IDC_OK:
    {
      if (!_missionList) return;
      int sel = _missionList->GetCurSel();
      if (sel < 0) return;
      int value = _missionList->GetValue(sel);

      if (value == ITLocked) return;

      if (value == ITSubdirectory)
      {
        // open subdirectory
        _directory = _directory + _missionList->GetData(sel) + RString("\\");
        LoadDirectory();
        return;
      }
#if _ENABLE_MISSION_CONFIG
      else if (value == ITAddonFolder)
      {
        // open subfolder
        _folder.Add(_missionList->GetData(sel));
        LoadDirectory();
        return;
      }
#endif
#if _ENABLE_WIZARD
      else if (value == ITUserMissions)
      {
        if (!Glob.demo)
        {
          _userMissions = true;
          LoadDirectory();
        }
        return;
      }
#endif
      else if (value == ITPackage)
      {
        _package = true;
        _directory = _missionList->GetData(sel);
        LoadDirectory();
        return;
      }
#if _ENABLE_WIZARD
      else if (value == ITTemplateOpen)
      {
        if (!CheckDiskSpace(this, "U:\\", CHECK_SPACE_SAVE)) break;

        // play mission
        // RString mission = _missionList->GetData(sel);
        if (GetCtrl(IDC_SINGLE_DIFF))
          OnRestartMission();
        else
          CreateChild(new DisplaySelectDifficulty(this, -1));
      }
#endif
      else
      {
        if (!CheckDiskSpace(this, "U:\\", CHECK_SPACE_SAVE)) break;

        // play mission
        if (_saveType >= 0)
          OnLoadMission();
        else
        {
          if (GetCtrl(IDC_SINGLE_DIFF))
            OnRestartMission();
          else
            CreateChild(new DisplaySelectDifficulty(this, -1));
        }
      }
    }
    break;
  case IDC_CANCEL:
    {
      if (_package)
      {
        _package = false;
        _directory = _root;
        LoadDirectory();
      }
      else if (stricmp(_directory, _root) != 0)
      {
        // open parent directory
        int last = _directory.GetLength() - 1;
        Assert(_directory[last] == '\\');
        RString name = _directory.Substring(0, last);
        const char *ext = strrchr(name, '\\');
        if (ext) _directory = name.Substring(0, ext - (const char *)name + 1);
        else _directory = "";
        LoadDirectory();
      }
#if _ENABLE_MISSION_CONFIG
      else if (_folder.Size() > 0)
      {
        // back to parent folder
        int n = _folder.Size();
        _folder.Resize(n - 1);
        LoadDirectory();
      }
#endif
      else if (_userMissions)
      {
        _userMissions = false;
        LoadDirectory();
      }
      else
      {
        ControlObjectContainerAnim *ctrl =
          dynamic_cast<ControlObjectContainerAnim *>(GetCtrl(IDC_SINGLE_MISSION_PAD));
        if (ctrl)
        {
          _exitWhenClose = idc;
          ctrl->Close();
        }
        else Exit(idc);
      }
    }
    break;
  case IDC_SINGLE_DIFF:
    _difficulty++;
    if (_difficulty >= Glob.config.diffSettings.Size()) _difficulty = 0;
    SaveParams();
    OnChangeDifficulty();
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
#endif
}

static RString FindFilename(RString dir, RString templ)
{
  for (int i=1; i<INT_MAX; i++)
  {
    RString path = dir + Format(templ, i);
    if (!QIFileFunctions::FileExists(path)) return path;
  }
  Fail("Not reached");
  return RString();
}

void DisplaySingleMission::OnSimulate(EntityAI *vehicle)
{
#if defined _XBOX && _XBOX_VER >= 200
  // if wea re in user missions menu and no storage is available, we must return
  if (_userMissions && !GSaveSystem.IsStorageAvailable())
  {
    OnButtonClicked(IDC_CANCEL);
  }
#endif

  if
  (
    GInput.CheatActivated() == CheatUnlockMissions
#ifdef _XBOX
    || GInput.GetCheatXToDo(CXTUnlockMissions)
#endif
  )
  {
    _showAll = true;
    LoadDirectory();
    OnChangeMission();
    GInput.CheatServed();
  }
  Display::OnSimulate(vehicle);
}

void DisplaySingleMission::OnChildDestroyed(int idd, int exit)
{
#ifdef _WIN32
  switch (idd)
  {
    case IDD_MSG_RESTART_MISSION:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        if (GetCtrl(IDC_SINGLE_DIFF))
          OnRestartMission();
        else
          CreateChild(new DisplaySelectDifficulty(this, -1));
      }
      break;
    case IDD_SELECT_DIFFICULTY: // Select difficulty
      if (exit == IDC_OK)
      {
        DisplaySelectDifficulty *disp = dynamic_cast<DisplaySelectDifficulty *>(_child.GetRef());
        Assert(disp);
        _difficulty = disp->GetDifficulty();
        Display::OnChildDestroyed(idd, exit);

        Glob.config.difficulty = _difficulty;
        GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
        ParamFile cfg;
        if (ParseUserParams(cfg, &globals))
        {
          cfg.Add("difficulty", Glob.config.diffNames.GetName(Glob.config.difficulty));
          SaveUserParams(cfg);
        }

        OnRestartMission();
      }
      else
        Display::OnChildDestroyed(idd, exit);
      break;

//{ Mission logic 
/* Not used in single mission
    case IDD_CAMPAIGN:
      Display::OnChildDestroyed(idd, exit);
      OnCampaignIntroFinished(this, exit);
      break;
*/
    case IDD_INTRO:
      Display::OnChildDestroyed(idd, exit);
      if (!OnIntroFinished(this, exit))
      {
        LoadDirectory();
        UpdateSaves();
        OnChangeMission();
      }
      break;
    case IDD_INTEL_GETREADY:
      Display::OnChildDestroyed(idd, exit);
      if (!OnBriefingFinished(this, exit))
      {
        UpdateSaves();
        OnChangeMission();
      }
      break;
    case IDD_MISSION:
      Display::OnChildDestroyed(idd, exit);
      if (!OnMissionFinished(this, exit, true))
      {
        LoadDirectory();
        UpdateSaves();
        OnChangeMission();
      }
      break;
/*
    case IDD_DEBRIEFING:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_DEBRIEFING_RESTART)
      {
        DisplayMission *disp = new DisplayMission(this);
        CreateChild(disp);
        disp->DoRestartMission();
        GWorld->OnInitMission(true);
      }
      else //if (!OnDebriefingFinished(this, exit))
      {
        LoadDirectory();
        UpdateSaves();
        OnChangeMission();
      }
      break;
*/
    case IDD_OUTRO:
      Display::OnChildDestroyed(idd, exit);
      if (!OnOutroFinished(this, exit))
      {
        LoadDirectory();
        UpdateSaves();
        OnChangeMission();
      }
      break;
//}
    case IDD_MSG_LOAD_FAILED:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        OnChangeMission();
      }
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
#endif            // _WIN32
}

void DisplaySingleMission::OnCtrlClosed(int idc)
{
  if (idc == IDC_SINGLE_MISSION_PAD)
    Exit(_exitWhenClose);
  else
    Display::OnCtrlClosed(idc);
}

void DisplaySingleMission::ReportCorrupted(RString name)
{
  // save is damaged
  RString message = Format(LocalizeString(IDS_LOAD_FAILED), cc_cast(name));
  MsgBoxButton buttonOK(LocalizeString(IDS_DISP_CONTINUE), INPUT_DEVICE_XINPUT + XBOX_A);
  CreateMsgBox(MB_BUTTON_OK, message, -1, false, &buttonOK);
}

#if defined _XBOX && _XBOX_VER >= 200
void DisplaySingleMission::OnStorageDeviceChanged()
{
  GSaveSystem.StorageChangeHandled();

  if (_userMissions)
  {
    /// we must leave the user missions menu
    OnButtonClicked(IDC_CANCEL);
  }
  else
  {
    LoadDirectory();
  }
  OnChangeMission();
}

void DisplaySingleMission::OnStorageDeviceRemoved()
{
  if (_userMissions)
  {
    /// we must leave the user missions menu
    OnButtonClicked(IDC_CANCEL);
  }
  else
  {
    if (GSaveSystem.IsStorageRemovalDialogDeclined())
    {
      GSaveSystem.StorageRemovalDialogResetState();
      GSaveSystem.StorageRemovedHandled();
    }
    else
    {
      Display::OnStorageDeviceRemoved();
    }
  }
}

void DisplaySingleMission::OnSavesChanged()
{
  LoadDirectory();
  Display::OnSavesChanged();
}

#endif  //defined _XBOX && _XBOX_VER >= 200

/// skill value needs to be converted to skill and precision
/** this conversion is used for custom skill */

float SkillToPrecision(float skill)
{
  return floatMinMax(skill*1.8f-0.8f,0.1f,1.0f);
}

// Options display
DisplayOptions::DisplayOptions(ControlsContainer *parent, bool enableSimulation, bool credits)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
#if _VBS2 // alternate options display if access from in-game (no background)
#if _VBS3 // in-editor options
  if (parent->IDD() == IDD_MISSION_EDITOR || parent->IDD() == IDD_MISSION_EDITOR_REALTIME)
    Load("RscDisplayOptionsInEditor");
  else 
#endif
  if (!credits)
    Load("RscDisplayOptionsInGame");
  else
#endif
  Load("RscDisplayOptions");
#if _ENABLE_AI && !_FORCE_DEMO_ISLAND
  if (!credits)
#endif
    if (GetCtrl(IDC_OPTIONS_CREDITS)) GetCtrl(IDC_OPTIONS_CREDITS)->ShowCtrl(false);

  //@{ Get revMouse, perspective and fov settings
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  ParseUserParams(cfg, &globals);
  GInput.LoadKeys(cfg);
  GInput.RemoveInaccessableKeys();

  Init();
  //@}

#if _VBS2
  if (!Glob.vbsAdminMode)
  {
    if (GetCtrl(IDC_OPTIONS_VIDEO))
      GetCtrl(IDC_OPTIONS_VIDEO)->EnableCtrl(false);
    if (GetCtrl(IDC_OPTIONS_AUDIO))
      GetCtrl(IDC_OPTIONS_AUDIO)->EnableCtrl(false);
    if (GetCtrl(IDC_OPTIONS_DIFFICULTY))
      GetCtrl(IDC_OPTIONS_DIFFICULTY)->EnableCtrl(false);
#if _VBS3 // warning message that most options are disabled    
    GWorld->CreateWarningMessage(LocalizeString("STR_MSG_USER_OPTIONS"));
#endif
  }
#endif
}

Control *DisplayOptions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateControl(type,idc,cls,ctrl));

  switch (idc)
  {
  case IDC_OPTIONS_FIELD_OF_VIEW:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider) slider->SetSpeed(0.15, 0.02); 
    }
    break;
  }
  return ctrl;
}

#define DISABLE_OPTIONS_CANCEL 1

void DisplayOptions::Destroy()
{
  Display::Destroy();
  
#if !DISABLE_OPTIONS_CANCEL
  if (_exit != IDC_OK)
  {
    Cancel();
    return;
  }
#endif

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    GInput.MergeJoysticksKeys();
    GInput.SaveKeys(cfg);
    if (GSoundsys) GSoundsys->SaveConfig(cfg);
    SaveUserParams(cfg);
    GInput.RemoveInaccessableKeys();
  }
}

void DisplayOptions::OnButtonClicked(int idc)
{
#ifdef _WIN32
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_OPTIONS)
    Exit(idc);
#endif
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
#if !DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
    {
      Cancel();
      ControlObjectContainerAnim *ctrl =
        dynamic_cast<ControlObjectContainerAnim *>(GetCtrl(IDC_OPTIONS_NOTEBOOK));
      if (ctrl)
      {
        _exitWhenClose = idc;
        ctrl->Close();
      }
      else Exit(idc);
    }
    break;
#endif
  case IDC_OPTIONS_VIDEO:
    CreateChild(new DisplayOptionsVideo(this, _enableSimulation));
    break;
  case IDC_OPTIONS_AUDIO:
    CreateChild(new DisplayOptionsAudio(this, _enableSimulation));
    break;
#if !defined(_XBOX) && defined(_WIN32)
  case IDC_OPTIONS_CONFIGURE:
    CreateChild(new DisplayConfigure(this, _enableSimulation));
    break;
#elif defined(_XBOX)
  case IDC_OPTIONS_CONFIGURE:
    CreateChild(new DisplayProfileController(this, _enableSimulation, GInput.scheme));
    break;
#endif

  case IDC_OPTIONS_GAMEOPTIONS:
    CreateChild(new DisplayGameOptions(this, _enableSimulation));//CreateChild(new DisplayDifficulty(this, true));
    break;
#if _ENABLE_AI
  case IDC_OPTIONS_CREDITS:
    {
      RString cutscene = Pars >> "CfgCredits" >> "cutscene";
      if (cutscene.GetLength() == 0) break;

      char buffer[256];
      strcpy(buffer, cutscene);

      // parse mission name

      // mission
      char *world = strchr(buffer, '.');
      Assert(world);
      if (!world)
      {
        RptF("Wrong credits cutscene: %s", buffer);
        break;
      }
      *world = 0;
      world++;

      SetMission(world, buffer, AnimsDir);
      SetBaseDirectory(false, "");

      LaunchIntro(this);
    }
    break;
#endif
  default:
    Display::OnButtonClicked(idc);
  }
#endif
}

void DisplayOptions::OnSliderPosChanged(IControl *ctrl, float pos)
{
#ifdef _WIN32
  CSliderContainer *slider = GetSliderContainer(ctrl);
  Assert(slider);
  if (!slider) return;

  ForEachControl(CheckOnSliderPosChanged(ctrl->IDC(), pos, slider));
#endif
}

void DisplayOptions::OnChildDestroyed(int idd, int exit)
{
#ifdef _WIN32
  Display::OnChildDestroyed(idd, exit);
  if (exit==IDC_MAIN_QUIT)
  {
    _exit = IDC_MAIN_QUIT;
    Exit(exit);
  }
  else
  {
  switch (idd)
  {
  case IDD_INTRO:
    GWorld->DestroyMap(IDC_OK);
    void ShowCinemaBorder(bool show);
    ShowCinemaBorder(true);
    StartRandomCutscene(Glob.header.worldname);
    break;
  case IDD_OPTIONS_AUDIO:
    Refresh();
    break;
#ifdef _XBOX
  case IDD_PROFILE_CONTROLLER:
    if (exit == IDC_OK)
    {
      DisplayProfileController *disp = dynamic_cast<DisplayProfileController *>(_child.GetRef());
      Assert(disp);
      GInput.scheme = disp->GetScheme();
      GInput.ApplyScheme();
    }
    break;
#endif
  }
  }
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (exit >= IDC_MAIN_TAB_LOGIN)
    Exit(exit);
  // allow switch to alternate options screen
  if (exit >= IDC_OPTIONS_VIDEO && exit <= IDC_OPTIONS_CREDITS)
    OnButtonClicked(exit);
#endif
#endif
}

void DisplayOptions::OnCtrlClosed(int idc)
{
  if (idc == IDC_OPTIONS_NOTEBOOK)
    Exit(_exitWhenClose);
  else
    Display::OnCtrlClosed(idc);
}

// Video options display
/*!
\patch 1.27 Date 10/11/2001 by Jirka
- Added: non-blood option (Video options display)
\patch 1.30 Date 11/02/2001 by Jirka
- Added: multitexturing option (Video options display)
\patch 1.30 Date 11/02/2001 by Jirka
- Added: w-buffer option (Video options display)
\patch 5104 Date 12/18/2006 by Jirka
- Added: Support for languages switching
\patch 5130 Date 2/21/2007 by Jirka
- Fixed: Languages switching was disabled for some distributions
*/

DisplayOptionsVideo::DisplayOptionsVideo(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayOptionsVideo");


#ifdef _WIN32
  _gamma.SetPreview(GetCtrl(IDC_OPTIONS_GAMMA_VALUE));
  _brightness.SetPreview(GetCtrl(IDC_OPTIONS_BRIGHT_VALUE));
  _visibility.SetPreview(GetCtrl(IDC_OPTIONS_VISIBILITY_VALUE));
  _shadowVisibility.SetPreview(GetCtrl(IDC_OPTIONS_SHADOW_VISIBILITY_VALUE));

#if _HELISIM_VIEW_DIST
  _oldViewDistance = -1;
  _oldViewObjectDistance = -1;
  _objectVisibility.SetPreview(GetCtrl(IDC_OPTIONS_OBJECT_VISIBILITY_VALUE));
#endif
  _shadowVisibility.SetPreview(GetCtrl(IDC_OPTIONS_SHADOW_VISIBILITY_VALUE));
#if _VBS3
  _drawDistance.SetPreview(GetCtrl(IDC_OPTIONS_DRAWDISTANCE_VALUE));
#endif

#if _HELISIM_AMBIENT_TRAFFIC
  _trafficVisibility.SetPreview(GetCtrl(IDC_OPTIONS_TRAFFIC_VISIBILITY_VALUE));
#endif


  bool NoBlood();
  if (NoBlood())
  {
    IControl *ctrl = GetCtrl(IDC_OPTIONS_BLOOD_TEXT);
    if (ctrl) ctrl->ShowCtrl(false);
  }
#endif
  
  Init();

  GEngine->GetAspectSettings(_oldAspect);
  _oldIGUIScale = Glob.config.IGUIScale;
  if(_oldIGUIScale <= 0.1) _oldIGUIScale = Glob.config.NormalScale * 0.01;
  else  if(_oldIGUIScale > 1.0) _oldIGUIScale = Glob.config.NormalScale * 0.01;
  
  GEngine->BeginOptionsVideo();
}

DisplayOptionsVideo::~DisplayOptionsVideo()
{
  GEngine->EndOptionsVideo();
}

Control *DisplayOptionsVideo::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateControl(type,idc,cls,ctrl));
  
  // preset control needs special handling
  // check for current value
  ForPresetControl(CheckOnCreateControl(type,idc,cls,ctrl));
  
  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayOptionsVideo::SetDefaultSettingsLevel(int level)
{
  //Scene
  // no need to update, will update everything once done
  GScene->SetDefaultSettingsLevel(level);
  
  GEngine->SetDefaultSettingsLevel(level);
  Refresh();
}

void DisplayOptionsVideo::OnButtonClicked(int idc)
{
#ifdef _WIN32
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
#if DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
#endif
  case IDC_OK:
    {

      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;

      if (ParseUserParams(cfg, &globals))
      {
        GEngine->SaveConfig(cfg,GetConfigSection());
        Glob.config.SaveDifficulties(cfg);
        SaveUserParams(cfg);
      }
    }
    {
      AspectSettings as;
      GEngine->GetAspectSettings(as);

      if( abs(_oldIGUIScale - Glob.config.IGUIScale)>0.001 
        || (_oldAspect.leftFOV != as.leftFOV || _oldAspect.topFOV != as.topFOV))
      {
        if (_enableSimulation) //do not show button OK for immediate restart when options opened while playing a mission
          CreateMsgBox(MB_BUTTON_OK|MB_BUTTON_CANCEL|MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
        else
          CreateMsgBox(MB_BUTTON_CANCEL|MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
        break;
      }
    }
    Exit(idc);
    break;
#if !DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
    { 
      Cancel();
      Glob.config.IGUIScale = _oldIGUIScale;
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        AspectSettings asp;
        GEngine->GetAspectSettings(asp);
        float w = asp.leftFOV;
        float h = asp.topFOV;

        float sc = 0.5f * (1 - Glob.config.IGUIScale);

        if(w/h >= (4.0/3.0))
        {
          asp.uiTopLeftY = sc;
          asp.uiBottomRightY = 1 - sc;

          asp.uiTopLeftX =   0.5 * (1.0 -((h * (4.0/3.0) * Glob.config.IGUIScale)/w));
          asp.uiBottomRightX = 1.0 - asp.uiTopLeftX;
        }
        else
        {
          asp.uiTopLeftX = sc;
          asp.uiBottomRightX = 1 - sc;

          asp.uiTopLeftY =  0.5f * (1 -((w * (3.0f/4.0f) * Glob.config.IGUIScale) / h));
          asp.uiBottomRightY = 1 - asp.uiTopLeftY;
        }
        GEngine->SetAspectSettings(asp);
      }
    }
    Exit(idc);
    break;
#endif
  case IDC_OPTIONS_VIDEO_DEFAULT:
    {
      // SetDefaultSettingsLevel will refresh many HW specific settings
      // set even settings which have no performance impact here
      _gamma.Default();
      _brightness.Default();
      _wBuffer.Default();

      _gamma.OnUpdate(GetCtrl(IDC_OPTIONS_GAMMA_SLIDER));
      _brightness.OnUpdate(GetCtrl(IDC_OPTIONS_BRIGHT_SLIDER));
#if !defined(_XBOX)
      //detect igui size
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_iguiScale.GetIDC()));

      int height = GLOB_ENGINE->HeightBB();

      if(lbox)
      {
        if (lbox->Size()>4)  
        { 
          if(height < 768) lbox->SetCurSel(4);
          else if(height < 1024) lbox->SetCurSel(3);
          else if(height < 1050) lbox->SetCurSel(2);
          else lbox->SetCurSel(1);
        }
        else lbox->SetCurSel(0);
      }

      float ratio = 0;
      if(!GEngine->IsWindowed()) 
        ratio = Glob.config.defaultAspect;
      else
        if(GEngine->HeightBB()>0) ratio = (float)GEngine->WidthBB() / GEngine->HeightBB();

      //detect aspect
      AspectSettings asp;
      GEngine->GetAspectSettings(asp);
      if(ratio>0) SetWideAspectSettings(asp, ratio);
      ResetAspect(asp);
      if(GEngine->IsWindowed())
      {//can jump to custom
        AspectPresetsReady = false;     
        _aspect.Init();
      }
      _aspect.Update(GetListBoxContainer(GetCtrl(_aspect.GetIDC())) ,true);
      _textureMemory.Default();
      SetDefaultSettingsLevel(GEngine->GetDeviceLevel());
      //Bloom and RotBlur - negative values implies default settings
      GEngine->SetBloom(-1); 
      GEngine->SetRotBlurPower(-1);
#endif
    }
    break;
  case IDC_OPTIONS_VIDEO_TEXTURES:
    CreateChild(new DisplayOptionsVideoTextures(this, _enableSimulation));
    break;
  case IDC_OPTIONS_VIDEO_RENDERING:
    CreateChild(new DisplayOptionsVideoRendering(this, _enableSimulation));
    break;
  case IDC_OPTIONS_VIDEO_USER_INTERFACE:
    CreateChild(new DisplayOptionsVideoUserInterface(this, _enableSimulation));
    break;
  case IDC_OPTIONS_VIDEO_QUALITY:
    CreateChild(new DisplayOptionsVideoQuality(this, _enableSimulation));
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
  RefreshPreset();
#endif
}

void DisplayOptionsVideo::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_MSG_RESTART_NEEDED:
    if (exit==IDC_CANCEL) break; //cancel
    else if (exit==IDC_USER_BUTTON)
    { // restart later
    Exit(IDC_OK);
    }
    else
    {
      // Restart the game
      TCHAR exeName[MAX_PATH];
      GetModuleFileName(0, exeName, MAX_PATH);
      extern RString GCmdLine;
      RString execute = RString(exeName) + RString(" ") + GCmdLine;
#if defined _WIN32 && !defined _XBOX
      WinExec(execute, SW_SHOW); //restart
#endif
      _exit = IDC_MAIN_QUIT;
      Exit(IDC_MAIN_QUIT);
    }
    break;
  default: 
    RefreshPreset();
    break;
  }
}

void DisplayOptionsVideo::OnDraw(EntityAI *vehicle, float alpha)
{
#if  0 // !_DEBUG && !_PROFILE
  //patch arma2.1.2 - AA not working properly yet
  // need to disable in loop because of scripted UI
  IControl *ctrl = GetCtrl( _fsAAQuality.GetIDC());
  if(ctrl) ctrl->EnableCtrl(false);
#endif

  Display::OnDraw(vehicle,alpha);
}

void DisplayOptionsVideo::OnSimulate(EntityAI *vehicle)
{
#if _HELISIM_VIEW_DIST
  IControl *ctrl =  GetCtrl(IDC_OPTIONS_OBJECT_VISIBILITY_SLIDER);
  if (ctrl)
  {
    if(_oldViewDistance != Glob.config.horizontZ) 
    {
      _objectVisibility.UpdateRange(ctrl);
#if _HELISIM_AMBIENT_TRAFFIC
      if (GetCtrl(IDC_OPTIONS_TRAFFIC_VISIBILITY_SLIDER)) {
         _trafficVisibility.UpdateRange(GetCtrl(IDC_OPTIONS_TRAFFIC_VISIBILITY_SLIDER));
      }
#endif
    }

    if(_oldViewObjectDistance != Glob.config.objectsZ) 
    {
      if(_oldViewObjectDistance > 0)_objectVisibility.OnUpdate(ctrl);
      _oldViewObjectDistance = Glob.config.objectsZ;
    }
  }
  
#endif

 Display::OnSimulate(vehicle);
}

void DisplayOptionsVideo::OnSliderPosChanged(IControl *ctrl, float pos)
{
#ifdef _WIN32
  CSliderContainer *slider = GetSliderContainer(ctrl);
  Assert(slider);
  if (!slider) return;

  ForEachControl(CheckOnSliderPosChanged(ctrl->IDC(), pos, slider));

  switch (ctrl->IDC())
  {
  case IDC_OPTIONS_VISIBILITY_SLIDER:
    {
      // Update list of terrain grids
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_OPTIONS_TERRAIN));
      if (lbox) _terrain.Update(lbox);
    }
    break;
  }
  RefreshPreset();
#endif
}

void DisplayOptionsVideo::ResetAspect(AspectSettings asp)
{
  float w = asp.leftFOV;
  float h = asp.topFOV;

  float sc = 0.5f * (1 - Glob.config.IGUIScale);

  if(w/h >= (4.0/3.0))
  {
    asp.uiTopLeftY = sc;
    asp.uiBottomRightY = 1 - sc;

    asp.uiTopLeftX =   0.5 * (1.0 -((h * (4.0/3.0) * Glob.config.IGUIScale)/w));
    asp.uiBottomRightX = 1.0 - asp.uiTopLeftX;
  }
  else
  {
    asp.uiTopLeftX = sc;
    asp.uiBottomRightX = 1 - sc;

    asp.uiTopLeftY =  0.5f * (1 -((w * (3.0f/4.0f) * Glob.config.IGUIScale) / h));
    asp.uiBottomRightY = 1 - asp.uiTopLeftY;
  }
  GEngine->SetAspectSettings(asp);
};

void DisplayOptionsVideo::OnLBSelChanged(IControl *ctrl, int curSel)
{
#ifdef _WIN32
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));

  switch (ctrl->IDC())
  {
  case IDC_OPTIONS_RESOLUTION:
    {
      // invalidate the list of refresh rates
      RefreshRateAccess::_needInit = true;
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_refreshRates.GetIDC()));
      if (lbox) _refreshRates.Update(lbox,true);

      lbox = GetListBoxContainer(GetCtrl(_iguiScale.GetIDC()));

      int height = GLOB_ENGINE->HeightBB();
      if(lbox && lbox->Size()>3)  
      { 
        if(height < 768) lbox->SetCurSel(4);
        else if(height < 1024) lbox->SetCurSel(3);
        else if(height < 1050) lbox->SetCurSel(2);
        else lbox->SetCurSel(1);
      }

      if (lbox) _iguiScale.Update(lbox,true);
      
      // update the fillrate control slider
      IControl *ctrl = GetCtrl(IDC_OPTIONS_SLIDER_FILLRATE);
      //if resolution is decreased, update fillrate
      _fillrateControl.OnUpdate(ctrl);
      

      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        AspectSettings asp;
        GEngine->GetAspectSettings(asp);
        ResetAspect(asp);
      }
    }
    break;
  case IDC_HDR_DETAIL:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_fsAAQuality.GetIDC()));
      if (lbox) _fsAAQuality.Update(lbox,true);
    }
    break;
  case IDC_FSAA_DETAIL:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_hdrQuality.GetIDC()));
      if (lbox) _hdrQuality.Update(lbox,true);
    }
    { // atoc disabled when antialiasing disabled
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_atoc.GetIDC()));
      if (lbox) _atoc.Update(lbox,true);
    }
    break;
  case IDC_QUALITY_PREFERENCE:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_preset.GetIDC()));
      if (lbox)
      {
        SetDefaultSettingsLevel(curSel<<8);
      }
    }
    break;
  case IDC_OPTIONS_FXAA_VALUE:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_fxaaQuality.GetIDC()));
      if (lbox) _fxaaQuality.Update(lbox,true);
    }
  case IDC_ASPECT_RATIO:
  case IDC_OPTIONS_IGUISIZE:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_iguiScale.GetIDC()));
      if (lbox) _iguiScale.Update(lbox,true);

      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        AspectSettings asp;
        GEngine->GetAspectSettings(asp);
        ResetAspect(asp);
      }
    }
    break;
    case IDC_VRAM_VALUE:
      {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_textureMemory.GetIDC()));
      if (lbox) _textureMemory.Update(lbox,true);
      }
      break;
    case IDC_TEXTURE_DETAIL:
      {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_textureQuality.GetIDC()));
      if (lbox) _textureQuality.Update(lbox,true);
      }
      break;
  }
  RefreshPreset();
#endif
}

DisplayOptionsVideoTextures::DisplayOptionsVideoTextures(DisplayOptionsVideo *parent, bool enableSimulation)
  : Display(parent), _textureMemory(parent->_textureMemory), _textureQuality(parent->_textureQuality), _afQuality(parent->_afQuality)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayOptionsVideoTextures");

  Init();
}

Control *DisplayOptionsVideoTextures::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateParentControl(_parent.GetLink(),type,idc,cls,ctrl));
  
  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayOptionsVideoTextures::OnButtonClicked(int idc)
{
#ifdef _WIN32
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
  case IDC_CANCEL:
  case IDC_OK:
    Exit(idc);
    break;
  }
#endif
}

void DisplayOptionsVideoTextures::OnLBSelChanged(IControl *ctrl, int curSel)
{
#ifdef _WIN32
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));

  switch (ctrl->IDC())
  {
    case IDC_TEXTURE_DETAIL:
      { // Update the texture memory, as its value can depend on Texture detail
        CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_textureMemory.GetIDC()));
        if (lbox) _textureMemory.Update(lbox,true);
      }
      break;
    case IDC_VRAM_VALUE:
      { // Update the texture quality, as its value can depend on Texture memory
        CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_textureQuality.GetIDC()));
        if (lbox) _textureQuality.Update(lbox,true);
      }
      break;
  }
#endif
}

DisplayOptionsVideoRendering::DisplayOptionsVideoRendering(DisplayOptionsVideo *parent, bool enableSimulation)
  : Display(parent), _fsAAQuality(parent->_fsAAQuality), _atoc(parent->_atoc), _ppaa(parent->_ppaa), _hdrQuality(parent->_hdrQuality),
    _postprocessEffects(parent->_postprocessEffects),
    _ambOcclusion(parent->_ambOcclusion), _bloom(parent->_bloom), _rotBlur(parent->_rotBlur)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayOptionsVideoRendering");

  Init();
}

Control *DisplayOptionsVideoRendering::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateParentControl(_parent.GetLink(),type,idc,cls,ctrl));
  
  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayOptionsVideoRendering::OnButtonClicked(int idc)
{
#ifdef _WIN32
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
  case IDC_CANCEL:
  case IDC_OK:
    Exit(idc);
    break;
  }
#endif
}

void DisplayOptionsVideoRendering::OnLBSelChanged(IControl *ctrl, int curSel)
{
#ifdef _WIN32
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));

  switch (ctrl->IDC())
  {
  case IDC_FSAA_DETAIL:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_hdrQuality.GetIDC()));
      if (lbox) _hdrQuality.Update(lbox,true);
    }
    { // atoc disabled when antialiasing disabled
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_atoc.GetIDC()));
      if (lbox) _atoc.Update(lbox,true);
    }
    break;
  case IDC_HDR_DETAIL:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_fsAAQuality.GetIDC()));
      if (lbox) _fsAAQuality.Update(lbox,true);
    }
    break;
  }
#endif
}

void DisplayOptionsVideoRendering::OnSliderPosChanged(IControl *ctrl, float pos)
{
#ifdef _WIN32
  CSliderContainer *slider = GetSliderContainer(ctrl);
  Assert(slider);
  if (!slider) return;

  ForEachControl(CheckOnSliderPosChanged(ctrl->IDC(), pos, slider));
#endif
}

DisplayOptionsVideoUserInterface::DisplayOptionsVideoUserInterface(DisplayOptionsVideo *parent, bool enableSimulation)
  : Display(parent), _aspect(parent->_aspect), _iguiScale(parent->_iguiScale), _resolutions(parent->_resolutions)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayOptionsVideoUserInterface");

  Init();
}

Control *DisplayOptionsVideoUserInterface::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateParentControl(_parent.GetLink(),type,idc,cls,ctrl));
  
  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayOptionsVideoUserInterface::OnButtonClicked(int idc)
{
#ifdef _WIN32
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
  case IDC_CANCEL:
  case IDC_OK:
    Exit(idc);
    break;
  }
#endif
}

void DisplayOptionsVideoUserInterface::OnLBSelChanged(IControl *ctrl, int curSel)
{
#ifdef _WIN32
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));

  switch (ctrl->IDC())
  {
  case IDC_OPTIONS_RESOLUTION:
    {
      // invalidate the list of refresh rates
      RefreshRateAccess::_needInit = true;

      lbox = GetListBoxContainer(GetCtrl(_iguiScale.GetIDC()));
      if (lbox)
      {
        int height = GLOB_ENGINE->HeightBB();
        if(lbox->Size()>3)  
        { 
          if(height < 768) lbox->SetCurSel(4);
          else if(height < 1024) lbox->SetCurSel(3);
          else if(height < 1050) lbox->SetCurSel(2);
          else lbox->SetCurSel(1);
        }

        _iguiScale.Update(lbox,true);
      }
/*    // SKIPPED: fillrate is not inside Quality submenu display
      // update the fillrate control slider
      IControl *ctrl = GetCtrl(IDC_OPTIONS_SLIDER_FILLRATE);
      //if resolution is decreased, update fillrate
      _fillrateControl.OnUpdate(ctrl);
*/
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        AspectSettings asp;
        GEngine->GetAspectSettings(asp);
        DisplayOptionsVideo::ResetAspect(asp);
      }
    }
    break;
  case IDC_ASPECT_RATIO:
  case IDC_OPTIONS_IGUISIZE:
    {
      CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(_iguiScale.GetIDC()));
      if (lbox) _iguiScale.Update(lbox,true);

      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        AspectSettings asp;
        GEngine->GetAspectSettings(asp);
        DisplayOptionsVideo::ResetAspect(asp);
      }
    }
    break;
  }
#endif
}

DisplayOptionsVideoQuality::DisplayOptionsVideoQuality(DisplayOptionsVideo *parent, bool enableSimulation)
  : Display(parent), _complexity(parent->_complexity), _terrain(parent->_terrain), 
   _simulWeatherQuality(parent->_simulWeatherQuality), _shadowQuality(parent->_shadowQuality)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayOptionsVideoQuality");

  Init();
}

Control *DisplayOptionsVideoQuality::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateParentControl(_parent.GetLink(),type,idc,cls,ctrl));
  
  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayOptionsVideoQuality::OnButtonClicked(int idc)
{
#ifdef _WIN32
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
  case IDC_CANCEL:
  case IDC_OK:
    Exit(idc);
    break;
  }
#endif
}

void DisplayOptionsVideoQuality::OnLBSelChanged(IControl *ctrl, int curSel)
{
#ifdef _WIN32
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));
#endif
}

// Audio options display
DisplayOptionsAudio::DisplayOptionsAudio(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayOptionsAudio");

  _music.SetPreview(GetCtrl(IDC_OPTIONS_MUSIC_VALUE));
  _effects.SetPreview(GetCtrl(IDC_OPTIONS_EFFECTS_VALUE));
  _voices.SetPreview(GetCtrl(IDC_OPTIONS_VOICES_VALUE));
  _von.SetPreview(GetCtrl(IDC_OPTIONS_VON_VALUE));

  Init();
}

Control *DisplayOptionsAudio::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateControl(type,idc,cls,ctrl));

  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayOptionsAudio::OnSliderPosChanged(IControl *ctrl, float pos)
{
#ifdef _WIN32
  CSliderContainer *slider = GetSliderContainer(ctrl);
  Assert(slider);
  if (!slider) return;

  ForEachControl(CheckOnSliderPosChanged(ctrl->IDC(), pos, slider));
#endif
}

/*!
\patch 5163 Date 6/4/2007 by Ondra
- Fixed: Changing audio options while playing MP could cause VoN not working or crash the game.
*/
void DisplayOptionsAudio::CheckPendingRestart()
{
  if (!GSoundsys) return;
  bool restartNeeded = false;
  if (_eax.IsChangePending() && !GSoundsys->CanChangeEAX()) restartNeeded = true;
  if (_hwAcc.IsChangePending() && !GSoundsys->CanChangeHW()) restartNeeded = true;
  if (restartNeeded)
  {
    if (_enableSimulation) //do not show button OK for immediate restart when options opened while playing a mission
      CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL | MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
    else
      CreateMsgBox(MB_BUTTON_CANCEL | MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
  }
}

void DisplayOptionsAudio::OnButtonClicked(int idc)
{
#ifdef _WIN32
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_OPTIONS)
    Exit(idc);
  // allow switch to alternate options screen
  if (idc >= IDC_OPTIONS_VIDEO && idc <= IDC_OPTIONS_CREDITS && idc != IDC_OPTIONS_AUDIO)
    Exit(idc);
#endif
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }

  switch (idc)
  {
  case IDC_OPTIONS_MIC_ADJUST:
    {
      CreateChild(new DisplayOptionsMicAutoAdj(this, _enableSimulation));
      break;
    }
#if DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
#endif
  case IDC_OK:
    {
      GSoundsys->TerminatePreview();
      CheckPendingRestart();
      Exit(idc);
    }
    break;
#if !DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
    Cancel();
    GSoundsys->TerminatePreview();
    Exit(idc);
    break;
#endif
  case IDC_OPTIONS_AUDIO_DEFAULT:
    SetDefault();
    break;
  default:
    Display::OnButtonClicked(idc);
  }
#endif
}

void DisplayOptionsAudio::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));
}

void DisplayOptionsAudio::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_OPTIONS_AUDIO_ADJUST_MIC:
    {
      if (exit == IDC_OK)
      {
        GSoundsys->SetVoNRecThreshold(GSoundsys->GetThreasholdWanted());
        _vonRecThr.OnUpdate(GetCtrl(IDC_OPTIONS_MIC_SENS_SLIDER));
      }
      if (exit == IDC_CANCEL) 
      {
        if (GSoundsys) GSoundsys->StopCapture();
      }
      break;
    }

  case IDD_MSG_RESTART_NEEDED:
    if (exit==IDC_CANCEL) break; //cancel
    else if (exit==IDC_USER_BUTTON)
    { // restart later
    Exit(IDC_OK);
    }
    else
    {
      // Restart the game
      TCHAR exeName[MAX_PATH];
      GetModuleFileName(0, exeName, MAX_PATH);
      extern RString GCmdLine;
      RString execute = RString(exeName) + RString(" ") + GCmdLine;
#if defined _WIN32 && !defined _XBOX
      WinExec(execute, SW_SHOW); //restart
#endif
      _exit = IDC_MAIN_QUIT;
      Exit(IDC_MAIN_QUIT);
    }
    break;
  }
}

DisplayOptionsMicAutoAdj::DisplayOptionsMicAutoAdj(ControlsContainer *parent, bool enableSimulation)
: Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
   Load("RscDisplayMicSensitivityOptions");

  Init();
}

Control *DisplayOptionsMicAutoAdj::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = ControlsContainer::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_OPTIONS_MIC_SLIDER_PROGRESS:
    if (type == CT_PROGRESS)
    {
      _progress = static_cast<CProgressBar *>(ctrl);
      _progress->SetRange(0, 100);
      _progress->SetPos(0);
      _progress->ShowCtrl(true);
    }
    break;
  }
  return ctrl;
}

void DisplayOptionsMicAutoAdj::OnButtonClicked(int idc)
{
  switch (idc)
  {
#if DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
#endif
  case IDC_OK:
    {
      Exit(idc);
    }
    break;

#if !DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
    {
      Cancel();
      Exit(idc);
    }
    break;
#endif
  case IDC_OPTIONS_MIC_START_ADJUST:
    {
      IControl *c = GetCtrl(IDC_OPTIONS_MIC_START_ADJUST);
      c->EnableCtrl(false);
      if (GSoundsys) GSoundsys->StartCapture(5.0f);
      _time = Glob.uiTime + 5.0f;
    }
  	break;
  }
}

void DisplayOptionsMicAutoAdj::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);

  if (GSoundsys)
  {
    IControl *c = GetCtrl(IDC_OPTIONS_MIC_START_ADJUST);
    if (c)
    {
      c->EnableCtrl(!GSoundsys->CaptureRunning());
    }
    c = GetCtrl(IDC_OK);
    if (c)
    {
      c->EnableCtrl(!GSoundsys->CaptureRunning());
    }

    IControl *c1 =  GetCtrl(IDC_OPTIONS_MIC_PROGRESS);
    if (c1)
    {
      CTextContainer *text = dynamic_cast<CTextContainer *>(c1);
      if (text)
      {
        if (GSoundsys->CaptureRunning() || _time.toInt()!=0)
        {
          float progressTime = _time - Glob.uiTime;
          progressTime = 1.0f - (progressTime * 0.2f);
          saturate(progressTime, 0.0f, 1.0f);

          int current = toInt(progressTime* 100); //progress in percents
          BString<64> buffer;
          buffer.PrintF("%3d%%", current);
          text->SetText(buffer.cstr());
          _progress->SetPos(current);
        }
        else text->SetText(" 0%");
      }
    }
  }
}

// Difficulty display
enum AILevel
{
  LNovice,
  LNormal,
  LExpert,
  NAILevels
};

DisplayGameOptions::DisplayGameOptions(ControlsContainer *parent, bool enableSimulation)
: Display(parent)
{
  Load("RscDisplayGameOptions");
  _enableSimulation = _enableSimulation && enableSimulation;

  _oldTitles = Glob.config.showTitles;
  _oldRadio = GChatList.ProfileEnabled();
  _oldMouse = GInput.floatingZoneArea;
  _oldHeadBob = Glob.config.headBob;
  _oldBlood = Glob.config.bloodLevel ;
  Init();
}

Control *DisplayGameOptions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch(idc)
  {
  case IDC_CONFIG_FLOATING_ZONE:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider)
      {
        slider->SetRange(0, 0.4);
        slider->SetSpeed(0.04, 0.004);
        slider->SetThumbPos(GInput.floatingZoneArea);
      }
      break;
    }
  case IDC_OPTIONS_HEADBOB:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider)
      {
        slider->SetRange(0, 2.0);
        slider->SetSpeed(0.2, 0.05);
        slider->SetThumbPos(Glob.config.headBob);
      }
      break;
    }
  }
  // give flexible controls an opportunity to handle OnCreateControl
  ForEachControl(CheckOnCreateControl(type,idc,cls,ctrl));

  return ctrl;
#else
  return Display::OnCreateCtrl(type, idc, cls);
#endif
}

void DisplayGameOptions::OnButtonClicked(int idc)
{
#ifdef _WIN32
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_OPTIONS)
    Exit(idc);
  // allow switch to alternate options screen
  if (idc >= IDC_OPTIONS_VIDEO && idc <= IDC_OPTIONS_CREDITS && idc != IDC_OPTIONS_DIFFICULTY)
    Exit(idc);
#endif
  ITextContainer *text = GetTextContainer(GetCtrl(idc));
  Assert(text);
  if (text)
  {
    ForEachControl(CheckOnButtonClicked(idc, text));
  }
  switch (idc)
  {
  case IDC_OPTIONS_DIFFICULTY:
    CreateChild(new DisplaySelectDifficulty(this, -1, "RscDisplayDifficultySelect",_enableSimulation));
    //CreateChild(new DisplayDifficulty(this, _enableSimulation));
    break;
#if DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
#endif
  case IDC_OK:
    {
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
       GInput.SaveKeys(cfg);
       Glob.config.SaveDifficulties(cfg);
       SaveUserParams(cfg);
      }
#ifndef _XBOX
      if (_languages._oldValue != GLanguage)
      {
        // language changed
        ParamFile cfg;
        ParseFlashpointCfg(cfg);
        cfg.Add("language", GLanguage);
        SaveFlashpointCfg(cfg);
        if (_enableSimulation) //do not show button OK for immediate restart when options opened while playing a mission
          CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL | MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
        else
          CreateMsgBox(MB_BUTTON_CANCEL | MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
        break;
      } 
#endif
      {
        Exit(idc);
        break;
      }
    }
#if !DISABLE_OPTIONS_CANCEL
  case IDC_CANCEL:
    {
      Glob.config.showTitles = _oldTitles;
      GChatList.Enable(_oldRadio);
      GInput.floatingZoneArea = _oldMouse;
      Glob.config.headBob = _oldHeadBob;
      Glob.config.bloodLevel = _oldBlood;
      Cancel();
      Exit(idc);
      break;
    }
#endif
  default:
    Display::OnButtonClicked(idc);
    }
#endif
   //  case IDC_DEFAULT:
  //  CSliderContainer *slider = GetSliderContainer(GetCtrl(IDC_CONFIG_FLOATING_ZONE));
  //  if (slider) slider->SetThumbPos(0.25);
}

void DisplayGameOptions::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (curSel < 0) return;

  CListBoxContainer *lbox = GetListBoxContainer(ctrl);
  Assert(lbox);
  if (!lbox) return;

  ForEachControl(CheckOnLBSelChanged(ctrl->IDC(), curSel, lbox));
}

void DisplayGameOptions::OnSliderPosChanged(IControl *ctrl, float pos)
{
  switch (ctrl->IDC())
  {
  case IDC_CONFIG_FLOATING_ZONE:
    GInput.floatingZoneArea = pos;
    break;
  case IDC_OPTIONS_HEADBOB:
    Glob.config.headBob = pos;
    break;
  default:
    Display::OnSliderPosChanged(ctrl, pos);
    break;
  }
}

void DisplayGameOptions::OnChildDestroyed(int idd, int exit)
{
#ifdef _WIN32
  switch (idd)
  {
  case IDD_MSG_RESTART_NEEDED:
    if (exit==IDC_CANCEL) break; //cancel
    else if (exit==IDC_USER_BUTTON)
    { // restart later
      Exit(IDC_OK);
    }
    else
    {
      // Restart the game
      TCHAR exeName[MAX_PATH];
      GetModuleFileName(0, exeName, MAX_PATH);
      extern RString GCmdLine;
      RString execute = RString(exeName) + RString(" ") + GCmdLine;
#ifndef _XBOX
      WinExec(execute, SW_SHOW); //restart
#endif
      _exit = IDC_MAIN_QUIT;
      Exit(IDC_MAIN_QUIT);
    }
    break;
  case IDD_DIFFICULTY_SELECT: // Select difficulty
    if (exit == IDC_OK && GWorld->GetMode() != GModeNetware)
    {
      DisplaySelectDifficulty *disp = dynamic_cast<DisplaySelectDifficulty *>(_child.GetRef());
      Assert(disp);
      Glob.config.difficulty = disp->GetDifficulty();
      Display::OnChildDestroyed(idd, exit);
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        cfg.Add("difficulty", Glob.config.diffNames.GetName(Glob.config.difficulty));
        SaveUserParams(cfg);
      }
      CreateChild(new DisplayDifficulty(this, _enableSimulation));
    }
    else
      Display::OnChildDestroyed(idd, exit);
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
#endif            // _WIN32
}

DisplayDifficulty::DisplayDifficulty(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  _diff = NULL;

  _indexCurrent = Glob.config.difficulty;
  saturateMax(_indexCurrent, 0);

  Load("RscDisplayDifficulty");


  for (int j=0; j<2; j++)
  {
    _oldAISkill[j] = Glob.config.diffSettings[_indexCurrent].aiSkill[j];
  }

  Refresh();
}

void DisplayDifficulty::Refresh()
{
  InitAILevel(Glob.config.diffSettings[_indexCurrent].aiSkill[0], IDC_DIFF_CADET_FRIENDLY_LEVEL, IDC_DIFF_CADET_FRIENDLY_SKILL, IDC_DIFF_CADET_FRIENDLY_SKILL_ECHO);
  InitAILevel(Glob.config.diffSettings[_indexCurrent].aiSkill[1], IDC_DIFF_CADET_ENEMY_LEVEL, IDC_DIFF_CADET_ENEMY_SKILL, IDC_DIFF_CADET_ENEMY_SKILL_ECHO);
 
  UpdateAISkillTitles();

//#ifdef _WIN32
//  {
//    ITextContainer *text = GetTextContainer(GetCtrl(IDC_OPTIONS_SUBTITLES));
//    if (text)
//    {
//      if (Glob.config.showTitles)
//        text->SetText(LocalizeString(IDS_OPT_SUBTITLES_ENABLED));
//      else
//        text->SetText(LocalizeString(IDS_OPT_SUBTITLES_DISABLED));
//    }
//
//    text = GetTextContainer(GetCtrl(IDC_OPTIONS_RADIO));
//    if (text)
//    {
//      if (GChatList.ProfileEnabled())
//        text->SetText(LocalizeString(IDS_OPT_RADIO_ENABLED));
//      else
//        text->SetText(LocalizeString(IDS_OPT_RADIO_DISABLED));
//    }
//  }
//#endif

  if (_diff)
  {
    for (int i=0; i<DTN; i++)
    {
      _diff->currentDifficulty[i] = Glob.config.diffSettings[_indexCurrent].flags[i];
    }
  }
}

void DisplayDifficulty::UpdateAISkillTitles()
{
  IControl *ctrl = GetCtrl(IDC_DIFF_TEXT_FRIENDLY_SKILL);
  if (ctrl)
  {
    bool show = false;
    if (GetCtrl(IDC_DIFF_CADET_FRIENDLY_SKILL) && GetCtrl(IDC_DIFF_CADET_FRIENDLY_SKILL)->IsVisible()) show = true;
    else if (GetCtrl(IDC_DIFF_VETERAN_FRIENDLY_SKILL) && GetCtrl(IDC_DIFF_VETERAN_FRIENDLY_SKILL)->IsVisible()) show = true;
    ctrl->ShowCtrl(show);
  }
  ctrl = GetCtrl(IDC_DIFF_TEXT_ENEMY_SKILL);
  if (ctrl)
  {
    bool show = false;
    if (GetCtrl(IDC_DIFF_CADET_ENEMY_SKILL) && GetCtrl(IDC_DIFF_CADET_ENEMY_SKILL)->IsVisible()) show = true;
    else if (GetCtrl(IDC_DIFF_VETERAN_ENEMY_SKILL) && GetCtrl(IDC_DIFF_VETERAN_ENEMY_SKILL)->IsVisible()) show = true;
    ctrl->ShowCtrl(show);
  }
}

const float NoviceSkill = 0.55f;
const float NormalSkill = 0.70f;
const float ExpertSkill = 0.90f;

static const float LevelSkill[NAILevels]={NoviceSkill,NormalSkill,ExpertSkill};

static AILevel SkillToLevel(float skill)
{
  float minDist = FLT_MAX;
  int index = 0;
  for (int i=0; i<NAILevels; i++)
  {
    float dist = fabs(LevelSkill[i]-skill);
    if (minDist>dist) minDist = dist, index = i;
  }
  return AILevel(index);
}
void DisplayDifficulty::InitAILevel(float skill, int idcLevel, int idcSkill, int idcEcho)
{
  CListBoxContainer *lb = GetListBoxContainer(GetCtrl(idcLevel));

  if (!lb) return;
  lb->ClearStrings();
  float minSkill = 0;
#if !_VBS2 // allow novice enemy skill in VBS2
  if (!Glob.config.diffSettings[_indexCurrent].showCadetHints)
  {
    minSkill = NormalSkill;
  }
#endif
  saturateMax(skill,minSkill);
  AILevel level = SkillToLevel(skill);
  // enemy AI levels have min. values in veteran mode
  int idsLevel[NAILevels]={IDS_AILEVEL_NOVICE,IDS_AILEVEL_NORMAL,IDS_AILEVEL_EXPERT};
  int sel = 0;
  for (int i=0; i<NAILevels; i++)
  {
    if (LevelSkill[i]>=minSkill)
    {
      int index = lb->AddString(LocalizeString(idsLevel[i]));
      lb->SetValue(index, i);
      if (level == i) sel = index;
    }
  }

  lb->SetCurSel(sel,false);

  IControl *ctrl = GetCtrl(idcSkill);
  CSliderContainer *slider = GetSliderContainer(ctrl);
  if (slider)
  {
    slider->SetRange(minSkill, 1);
    slider->SetSpeed(0.02, 0.1);
    UpdateSlider(ctrl, skill);
  }

  ctrl = GetCtrl(idcEcho);
  if (ctrl)
  {
    UpdateText(ctrl, skill);
  }
}

void DisplayDifficulty::UpdateSlider(IControl *ctrl, float skill)
{
  ctrl->ShowCtrl(true);
  CSliderContainer *slider = GetSliderContainer(ctrl);
  if (slider) slider->SetThumbPos(skill);
}

void DisplayDifficulty::UpdateText(IControl *ctrl, float skill)
{
  ctrl->ShowCtrl(true);
  ITextContainer *text = GetTextContainer(ctrl);
  if (text) text->SetText(Format("%.2f", skill));
}

Control *DisplayDifficulty::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
#ifdef _WIN32
  switch (idc)
  {
  case IDC_DIFFICULTIES_DIFFICULTIES:
    {
      CListBoxContainer *list = NULL;
      Control *ctrl = NULL;

      if (type == CT_LISTBOX)
      {
        C2DDifficulties *diff = new C2DDifficulties(this, idc, cls);
        _diff = diff;
        list = diff;
        ctrl = diff;
      }
      else return NULL;

      for (int i=0; i<DTN; i++)
      {
        list->AddString(LocalizeString(Config::diffDesc[i].desc));
        _diff->enabledInCurrent[i] = Glob.config.diffSettings[_indexCurrent].flagsEnabled[i];
      }
      list->SetCurSel(0);
      return ctrl;
    }
    default:
      return Display::OnCreateCtrl(type, idc, cls);
  }
#else
    return Display::OnCreateCtrl(type, idc, cls);
#endif
}


void DisplayDifficulty::OnButtonClicked(int idc)
{
#ifdef _WIN32
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_OPTIONS)
    Exit(idc);
  // allow switch to alternate options screen
  if (idc >= IDC_OPTIONS_VIDEO && idc <= IDC_OPTIONS_CREDITS && idc != IDC_OPTIONS_DIFFICULTY)
    Exit(idc);
#endif
  switch (idc)
  {
  case IDC_DIFFICULTIES_DEFAULT:
    {
      RString nameCadet = Glob.config.diffNames.GetName(_indexCurrent);
      ParamEntryVal cfgCadet = Pars >> "CfgDifficulties" >> nameCadet;
      DifficultySettings &itemCadet = Glob.config.diffSettings[_indexCurrent];
      
      for (int i=0; i<DTN; i++)
      {
        RString name = Glob.config.diffDesc[i].name;
        itemCadet.flags[i] = (cfgCadet >> "Flags" >> name)[0];
      }
      
      itemCadet.aiSkill[0] = cfgCadet >> "skillFriendly";
      itemCadet.aiSkill[1] = cfgCadet >> "skillEnemy";
      itemCadet.aiPrecision[0] = SkillToPrecision(itemCadet.aiSkill[0]);
      itemCadet.aiPrecision[1] = SkillToPrecision(itemCadet.aiSkill[1]);
    }

    Refresh();
    break;
  case IDC_OK:
    {
      if (_diff)
      {
        for (int i=0; i<DTN; i++)
        {
          Glob.config.diffSettings[_indexCurrent].flags[i] = _diff->currentDifficulty[i];
        }
      }
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        Glob.config.SaveDifficulties(cfg);
        SaveUserParams(cfg);
      }
    }
    Exit(idc);
    break;
  case IDC_CANCEL:
    for (int j=0; j<2; j++)
    {
      Glob.config.diffSettings[_indexCurrent].aiSkill[j] = _oldAISkill[j];
      Glob.config.diffSettings[_indexCurrent].aiPrecision[j] = SkillToPrecision(_oldAISkill[j]);
    }
    Exit(idc);
    break;
  /*case IDC_OPTIONS_SUBTITLES:
    {
      Glob.config.showTitles = !Glob.config.showTitles;
      ITextContainer *text = GetTextContainer(GetCtrl(IDC_OPTIONS_SUBTITLES));
      if (text)
      {
        if (Glob.config.showTitles)
          text->SetText(LocalizeString(IDS_OPT_SUBTITLES_ENABLED));
        else
          text->SetText(LocalizeString(IDS_OPT_SUBTITLES_DISABLED));
      }
    }
    break;
  case IDC_OPTIONS_RADIO:
    {
      GChatList.Enable(!GChatList.ProfileEnabled());
      ITextContainer *text = GetTextContainer(GetCtrl(IDC_OPTIONS_RADIO));
      if (text)
      {
        if (GChatList.ProfileEnabled())
          text->SetText(LocalizeString(IDS_OPT_RADIO_ENABLED));
        else
          text->SetText(LocalizeString(IDS_OPT_RADIO_DISABLED));
      }
    }
    break;*/
  default:
    Display::OnButtonClicked(idc);
  }
#endif
}

void DisplayDifficulty::OnSliderPosChanged(IControl *ctrl, float pos)
{
  int idcEcho = -1;
  int idcList = -1;
  switch (ctrl->IDC())
  {
  case IDC_DIFF_CADET_FRIENDLY_SKILL:
    Glob.config.diffSettings[_indexCurrent].aiSkill[0] = pos;
    Glob.config.diffSettings[_indexCurrent].aiPrecision[0] = SkillToPrecision(pos);
    idcEcho = IDC_DIFF_CADET_FRIENDLY_SKILL_ECHO;
    idcList = IDC_DIFF_CADET_FRIENDLY_LEVEL;
    break;
  case IDC_DIFF_CADET_ENEMY_SKILL:
    Glob.config.diffSettings[_indexCurrent].aiSkill[1] = pos;
    Glob.config.diffSettings[_indexCurrent].aiPrecision[1] = SkillToPrecision(pos);
    idcEcho = IDC_DIFF_CADET_ENEMY_SKILL_ECHO;
    idcList = IDC_DIFF_CADET_ENEMY_LEVEL;
    break;
  }

  if (idcEcho >= 0)
  {
    IControl *text = GetCtrl(idcEcho);
    if (text)
    {
      UpdateText(text, pos);
    }
  }
  if (idcList>=0)
  {
    CListBoxContainer *list = GetListBoxContainer(GetCtrl(idcList));
    if (list)
    {
      AILevel level = SkillToLevel(pos);
      for (int i=0; i<list->Size(); i++)
      {
        if (level==list->GetValue(i))
        {
          list->SetCurSel(i,false);
          break;
        }
      }
    }
  }
}

void DisplayDifficulty::OnLBSelChanged(IControl *ctrl, int curSel)
{
  int idcSlider = -1;
  int idcEcho = -1;

  CListBoxContainer *list = GetListBoxContainer(ctrl);
  if (!list) return;
  int level = list->GetValue(curSel);
  if (level<0 || level>=lenof(LevelSkill)) return;
  float skill = LevelSkill[level];
  float prec = SkillToPrecision(skill);

  switch (ctrl->IDC())
  {
  case IDC_DIFF_CADET_FRIENDLY_LEVEL:
    idcSlider = IDC_DIFF_CADET_FRIENDLY_SKILL;
    idcEcho = IDC_DIFF_CADET_FRIENDLY_SKILL_ECHO;
    Glob.config.diffSettings[_indexCurrent].aiSkill[0] = skill;
    Glob.config.diffSettings[_indexCurrent].aiPrecision[0] = prec;
    break;
  case IDC_DIFF_CADET_ENEMY_LEVEL:
    idcSlider = IDC_DIFF_CADET_ENEMY_SKILL;
    idcEcho = IDC_DIFF_CADET_ENEMY_SKILL_ECHO;
    Glob.config.diffSettings[_indexCurrent].aiSkill[1] = skill;
    Glob.config.diffSettings[_indexCurrent].aiPrecision[1] = prec;
    break;
  default:
    return;
  }
  Assert(idcSlider >= 0);
  Assert(idcEcho >= 0);

  IControl *ctrl2 = GetCtrl(idcSlider);
  if (ctrl2) UpdateSlider(ctrl2, skill);
  ctrl2 = GetCtrl(idcEcho);
  if (ctrl2) UpdateText(ctrl2, skill);
  UpdateAISkillTitles();
}

CDifficulties::CDifficulties()
{
  _column = 0;
}

C2DDifficulties::C2DDifficulties(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CListBox(parent, idc, cls)
{
}

bool C2DDifficulties::OnKeyDown(int dikCode)
{
#ifdef _WIN32
  switch (dikCode)
  {
  case DIK_LEFT:
  case DIK_RIGHT:
  case DIK_SPACE:
    Toggle(1, GetCurSel());
    return true;
  default:
    return CListBox::OnKeyDown(dikCode);
  }
#else
  return false;
#endif
}

void C2DDifficulties::OnLButtonDown(float x, float y)
{
#ifdef _WIN32
  if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
    _scrollbar.SetPos(_topString);
    _scrollbar.OnLButtonDown(x, y);
    _topString = _scrollbar.GetPos();
    return;
  }
  float rowHeight = toInt(SCALED(_rowHeight) * GEngine->Height2D()) * (1.0f / GEngine->Height2D());

  float index = (y - _y) / SCALED(rowHeight);
  float row = toIntFloor(_topString + index);

  float w = _w;
  if (_scrollbar.IsEnabled()) w -= _sbWidth;

  _column = 1;
  Toggle(1, row);
  
#endif
}

void C2DDifficulties::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top,
  const Rect2DFloat &rect, float widthDiff
)
{
#ifdef _WIN32
  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = TEXT_BORDER;
  bool textures = (_style & LB_TEXTURES) != 0;

  PackedColor color = ModAlpha(GetFtColor(i), alpha);
  PackedColor selColor = color;
  bool focused = IsFocused();
  if (selected && _showSelected)
  {
    float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;

    PackedColor bgColor = _selBgColor;
    if (focused)
    {
      Color bgcol0(bgColor), bgcol1(_selBgColor2);
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    }
    bgColor = ModAlpha(bgColor, alpha);

    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
    float xx = rect.x * w;
    float ww = rect.w * w;
    if (textures)
    {
      const int borderWidth = 2;
      xx += SCALED(borderWidth);
      ww -= 2 * SCALED(borderWidth);
    }

    GEngine->Draw2D
      (
      mip, bgColor, Rect2DPixel(xx, top * h, ww, SCALED(_rowHeight) * h),
      Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );
   

    Color col0(GetSelColor(i)), col1(_selColor2);
    selColor = PackedColor(col0 * factor + col1 * (1 - factor));
    selColor = ModAlpha(color, alpha);
  }

  float left = rect.x + border;

  Texture *texture = GetTexture(i);
  if (texture)
  {
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    float width = _rowHeight * (texture->AWidth() * h) / (texture->AHeight() * w);

    PackedColor pictureColor;
    if (selected && _showSelected)
      pictureColor = ModAlpha(_pictureSelectColor, alpha);
    else
      pictureColor = ModAlpha(_pictureColor, alpha);

    GLOB_ENGINE->Draw2D
    (
      mip, pictureColor,
      Rect2DPixel(CX(rect.x + border), CY(top), CH(width), CW(_rowHeight)),
      Rect2DPixel(rect.x * w, rect.y * h, 0.6 * rect.w * w, rect.h * h)
    );
    left += width + border;
  }
  float t = top + 0.5 * (SCALED(_rowHeight - _size));
  PackedColor col = _column == 0 ? selColor: color;
  GLOB_ENGINE->DrawText
  (
    Point2DFloat(left, t), SCALED(_size), Rect2DFloat(rect.x, rect.y, 0.6 * rect.w, rect.h),
    _font, color, GetText(i), _shadow
  );

  left = rect.x + 0.6 * rect.w + border;
  RString text = currentDifficulty[i] ? LocalizeString(IDS_ENABLED) : LocalizeString(IDS_DISABLED);
  if (enabledInCurrent[i])
    col = selColor;
  else
    col = PackedColor(Color(1, 0, 0, alpha));
  GLOB_ENGINE->DrawText
  (
    Point2DFloat(left, t), SCALED(_size), Rect2DFloat(rect.x + 0.6 * rect.w, rect.y, 0.2 * rect.w, rect.h),
    _font, col, text, _shadow
  );

#endif
}

AbstractOptionsUI *CreateBuldozerOptionsUI()
{
  return new DisplayOptions(NULL, false, false);
}


void DisplayOptions::DestroyHUD(int exit)
{
  GWorld->DestroyOptions(exit);
}

ControlsContainer *CreateSingleMissionsDisplay(ControlsContainer *parent, RString root, RString missionsSpace)
{
#if defined _XBOX && _XBOX_VER >= 200
  //GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#elif _GAMES_FOR_WINDOWS
  if (!CheckForSignIn(false)) return NULL;
#endif
  return new DisplaySingleMission(parent, root, missionsSpace);
}

//! creates and returns message box for Warning message
ControlsContainer *CreateWarningMessageBox(RString text)
{
  MsgBoxButton buttonOK(LocalizeString(IDS_DISP_CONTINUE), INPUT_DEVICE_XINPUT + XBOX_A);
  return new MsgBox(NULL, MB_BUTTON_OK, text, -1, false, &buttonOK); 
}

#if _ENABLE_MISSION_CONFIG
DisplayServerHostMission::DisplayServerHostMission(ControlsContainer *parent, ConstParamEntryPtr cfg)
: DisplayServer(parent)
{
  OnPlayMission(cfg);
}

void DisplayServerHostMission::OnChildDestroyed(int idd, int exit)
{
  DisplayServer::OnChildDestroyed(idd, exit);
  Exit(IDC_CANCEL); //never show this dialog
}

void DisplayServerHostMission::OnPlayMission(ConstParamEntryPtr cfg)
{
  do 
  {
    SetBaseDirectory(false, RString());
    if (!SelectMission(*cfg)) break;

    // load template
    if (GWorld->UI()) GWorld->UI()->Init();

    if (!ParseMission(true, true)) break;

    Glob.config.difficulty = _difficulty;
    bool noCopy = false;
    RString owner;
    { // owner of the hosted mission should be the owner of MPMissions
      noCopy = true;
      owner = (Pars >> "CfgMissions" >> "MPMissions").GetOwnerName();
    }
    // for mission in addon, avoid __cur_mp bank
    RString displayName;
    GetNetworkManager().InitMission(_difficulty, displayName, noCopy, owner, false);
    GetNetworkManager().SetServerState(NSSAssigningPlayers);
    GetNetworkManager().UpdateMaxPlayers(INT_MAX, 0);

    Fail("DisplayMultiplayerSetup is subject to be removed!");
    return;
  } while (false);
  // FAIL
  Exit(IDC_CANCEL);
}

void HostMission(ControlsContainer *display, ConstParamEntryPtr cfg)
{
  display->CreateChild(new DisplayMultiplayerHostMission(display, cfg));
}
#endif

//! Book viewer display
class DisplayBookViewer : public Display
{
protected:
  InitPtr<CHTMLContainer> _content;
  InitPtr<ITextContainer> _leftHeader;
  InitPtr<ITextContainer> _rightHeader;
  InitPtr<ITextContainer> _leftFooter;
  InitPtr<ITextContainer> _rightFooter;
  InitPtr<IControl> _prevPage;
  InitPtr<IControl> _nextPage;

  RString _footerFormat;

public:
  DisplayBookViewer(ControlsContainer *parent, const ItemBookType *type);

  virtual Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls) override;
  virtual void OnButtonClicked(int idc) override;
  virtual void OnDraw(EntityAI *vehicle, float alpha) override;
  virtual void DestroyHUD(int exit) override;
};

DisplayBookViewer::DisplayBookViewer(ControlsContainer *parent, const ItemBookType *type)
  : Display(parent)
{
  RString clsName = "RscDisplayBookViewer";
  Load(clsName);

  ParamEntryVal cls = Pars >> clsName;
  _footerFormat = cls >> "footerFormat";

  if (_content)
    _content->Load(type->GetFilename());
  if (_leftHeader)
    _leftHeader->SetText(type->GetTitle());
  if (_rightHeader)
    _rightHeader->SetText(type->GetAuthor());
}

Control *DisplayBookViewer::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_BOOK_VIEWER_CONTENT:
    _content = GetHTMLContainer(ctrl);
    break;
  case IDC_BOOK_VIEWER_LEFT_HEADER:
    _leftHeader = GetTextContainer(ctrl);
    break;
  case IDC_BOOK_VIEWER_RIGHT_HEADER:
    _rightHeader = GetTextContainer(ctrl);
    break;
  case IDC_BOOK_VIEWER_LEFT_FOOTER:
    _leftFooter = GetTextContainer(ctrl);
    break;
  case IDC_BOOK_VIEWER_RIGHT_FOOTER:
    _rightFooter = GetTextContainer(ctrl);
    break;
  case IDC_BOOK_VIEWER_PREV:
    _prevPage = ctrl;
    break;
  case IDC_BOOK_VIEWER_NEXT:
    _nextPage = ctrl;
    break;
  }
  return ctrl;
}

void DisplayBookViewer::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_BOOK_VIEWER_PREV:
    if (_content)
    {
      int i = _content->CurrentSection();
      //int n = _content->NSections();
      int col = _content->NColumns();
      if (i >= col) _content->SwitchSectionRaw(i - col);
    }
    break;
  case IDC_BOOK_VIEWER_NEXT:
    if (_content)
    {
      int i = _content->CurrentSection();
      int n = _content->NSections();
      int col = _content->NColumns();
      if (i < n - col) _content->SwitchSectionRaw(i + col);
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayBookViewer::OnDraw(EntityAI *vehicle, float alpha)
{
  if (_content)
  {
    int i = _content->CurrentSection();
    int n = _content->NSections();
    int col = _content->NColumns();
    Assert(col == 2);
    char buffer[256];
    if (_leftFooter)
    {
      sprintf_s(buffer, sizeof(buffer), _footerFormat, i + 1); // page number 1-based
      _leftFooter->SetText(buffer);
    }
    if (_rightFooter && i + 1 < n)
    {
      sprintf_s(buffer, sizeof(buffer), _footerFormat, i + 2); // page number 1-based
      _rightFooter->SetText(buffer);
    }
    if (_prevPage) _prevPage->ShowCtrl(i >= col);
    if (_nextPage) _nextPage->ShowCtrl(i < n - col);
  }

  Display::OnDraw(vehicle, alpha);
}

void DisplayBookViewer::DestroyHUD(int exit)
{
  if (GWorld->UserDialog() == this)
    GWorld->DestroyUserDialog();
}

AbstractOptionsUI *CreateBookViewer(const ItemBookType *type)
{
  return new DisplayBookViewer(NULL, type);
}

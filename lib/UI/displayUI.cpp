// Implementation of display's instances
#include "../wpch.hpp"
#include "../global.hpp"

#include "uiMap.hpp"
#include "missionDirs.hpp"

#include "../landscape.hpp"

#include "resincl.hpp"
#include "../versionNo.h"

#include "../keyInput.hpp"

#include "../camera.hpp"

#include <El/QStream/qbStream.hpp>

#include <El/Common/randomGen.hpp>
#include <El/Common/perfProf.hpp>
#include <El/ParamArchive/paramArchiveDb.hpp>
#include <El/Evaluator/express.hpp>

#include "../scripts.hpp"
#include "../gameStateExt.hpp"

#include "../dikCodes.h"
#include <Es/Common/win.h>
#if defined _WIN32 && !defined _XBOX
#include <shellapi.h>
#endif
#include "../vkCodes.h"

#include <ctype.h>
#ifdef _WIN32
  #include <io.h>
  #include <direct.h>
#if !defined _XBOX
  #include "../TrackIR.hpp"
  #include "../freeTrack.hpp"
#endif
#endif
#include <sys/stat.h>

#include "../Network/netTransport.hpp"
#include "../Network/network.hpp"
#include "../Network/pingLoc.hpp"

#include "../fileLocator.hpp"

#include "chat.hpp"

#include <El/FileServer/multithread.hpp>

#include "../dynSound.hpp"
#include "displayUI.hpp"

#include "../Protect/selectProtection.h"

#include "../mbcs.hpp"

#include "../saveVersion.hpp"

#include <Es/Strings/bString.hpp>

#include "../stringtableExt.hpp"

#include <Es/Algorithms/qsort.hpp>

#include <Es/Files/filenames.hpp>
#include <Es/Files/fileContainer.hpp>

#include "../drawText.hpp"

#include "../progress.hpp"

#include <El/Debugging/debugTrap.hpp>

#include <El/PreprocC/preprocC.hpp>

#include "../saveGame.hpp"
#include "../gameDirs.hpp"

#include <El/Speech/debugVoN.hpp>

#include "../AI/aiRadio.hpp"

#include "Es/Common/langExt.hpp"
#define ENABLED_NOOPT_Bebul 0
#if ENABLED_FEATURE(NOOPT)
#if _ENABLE_CHEATS
#pragma optimize ("", off)
#endif
#endif

#if defined _XBOX
//#include "../soundDX8.hpp"
//#include "../XboxDSP/xboxSfx.h"
# if _ENABLE_REPORT
#   include <XbDm.h>
# endif
#endif

#include "../joystick.hpp"

#if _ENABLE_GAMESPY
#include "../GameSpy/pt/pt.h"
#include "../GameSpy/ghttp/ghttp.h"

int GetProductID();
int GetDistributionId();
#endif

inline PackedColor ModAlpha( PackedColor color, float alpha )
{
  int a=toInt(alpha*color.A8());
  saturate(a,0,255);
  return PackedColorRGB(color,a);
}

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
void SaveUserParams(ParamFile &cfg);

bool DeleteWeaponsFile();

RStringB GetExtensionSave();
RStringB GetExtensionWizardMission();

bool CustomFaceExist(RString name);

#if _ENABLE_CHEATS && _PROFILE
# pragma optimize("",off)
#endif

static void SetIngameVoiceChannel()
{
  ChatChannel channel = CCDirect;
  GetNetworkManager().SetVoiceChannel(channel);
}

#if _ENABLE_PERFLOG && defined _WIN32 && _ENABLE_CHEATS
static void WriteMissionHeader(const char *section)
{
  RString name;
#ifdef _XBOX
  DM_XBE info;
  DmGetXbeInfo("", &info);
  name = RString("D:\\") + RString(GetFilenameExt(info.LaunchPath));
#else
  TCHAR exeName[MAX_PATH];
  GetModuleFileName(0, exeName, MAX_PATH);
  name = exeName;
#endif

  char build[128] = "";
  struct _stat exeStat;
  if (_stat(name, &exeStat) >= 0)
  {
    struct tm *t = localtime(&exeStat.st_mtime);
    if (t)
    {
      strftime(build, 128, "%d.%m.%Y %H:%M", t);
    }
  }

  LogF(
    "<%s campaign=\"%s\" mission=\"%s%s.%s\" build=\"%s\">",
    section,
    (const char *)GetBaseDirectory(),
    (const char *)GetBaseSubdirectory(), (const char *)Glob.header.filenameReal, Glob.header.worldname,
    build);

  void PerfLogReset();
  PerfLogReset();
}
#endif


#ifdef _XBOX
class DiskFullMessage : public MsgBox
{
protected:
  char _driveLetter;
  int _blocks;

public:
  DiskFullMessage(ControlsContainer *parent, int flags, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel, char driveLetter, int blocks);
  void OnButtonClicked(int idc);
};

DiskFullMessage::DiskFullMessage(ControlsContainer *parent, int flags, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel, char driveLetter, int blocks)
: MsgBox(parent, flags, text, idd, structuredText, infoOK, infoCancel)
{
  _driveLetter = driveLetter;
  _blocks = blocks;
}

void DiskFullMessage::OnButtonClicked(int idc)
{
  if (idc == IDC_CANCEL)
  {
    // Launch Xbox Dashboard
    #if _XBOX_VER>=2
      // TODOX360: check how should dashboard be launched to free space
      XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
      // unreachable: XLaunchNewImage defined as DECLSPEC_NORETURN
    #else
      LD_LAUNCH_DASHBOARD ld;
      ZeroMemory(&ld, sizeof(ld));
      ld.dwReason = XLD_LAUNCH_DASHBOARD_MEMORY;
      ld.dwParameter1 = _driveLetter;
      ld.dwParameter2 = _blocks;
      XLaunchNewImage(NULL, PLAUNCH_DATA(&ld));
      Fail("Unaccessible");
    #endif
  }
  else MsgBox::OnButtonClicked(idc);
}
#endif

bool CheckDiskSpace(ControlsContainer *disp, const char *drive, int wantedBlocks, bool canCancel = true)
{
#ifdef _XBOX
  ULARGE_INTEGER bytes;
  if (!GetDiskFreeSpaceEx(drive, &bytes, NULL, NULL))
  {
    // if we cannot check the space, assume it is OK
    return true;
  }
  const DWORD BLOCK_SIZE = 16384;     // TCR Space Display
  long long available = bytes.QuadPart / BLOCK_SIZE;

  if (wantedBlocks <= available) return true;

  MsgBoxButton buttonOK(LocalizeString(canCancel ? IDS_DISP_CANCEL : IDS_DISP_CONTINUE), INPUT_DEVICE_XINPUT + XBOX_A);
  MsgBoxButton buttonCancel(LocalizeString(IDS_XBOX_HINT_FREE_BLOCKS), INPUT_DEVICE_XINPUT + XBOX_B);
  int flags = MB_BUTTON_OK | MB_BUTTON_CANCEL;

  RString format;
  if (*drive == 'T') format = LocalizeString(IDS_MSG_DISK_FULL_T);
  else format = LocalizeString(IDS_MSG_DISK_FULL_U);
  RString str = Format(format, wantedBlocks - available + 5);
  disp->SetMsgBox(new DiskFullMessage(disp, flags, str, IDD_MSG_DISK_FULL, false, &buttonOK, &buttonCancel, *drive, wantedBlocks + 5));
  return false;
#else
  return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// class ControllerRemovedMsgBox

//! Controller Removed message box class
class ControllerRemovedMsgBox : public MsgBox
{
public:
  ControllerRemovedMsgBox(ControlsContainer *parent, RString text, int idd);
  void OnSimulate(EntityAI *vehicle);
};

ControllerRemovedMsgBox::ControllerRemovedMsgBox(ControlsContainer *parent, RString text, int idd)
: MsgBox(parent, 0, text, idd, NULL)
{
}

void ControllerRemovedMsgBox::OnSimulate(EntityAI *vehicle)
{
  if (GInput.GetXInputButton(XBOX_NewController, false) > 0)
  {
    Exit(IDC_OK);
  }
}

//! creates and returns message box for Controller removed message
ControlsContainer *CreateControllerRemovedMessageBox()
{
  RString text;
#ifdef _WIN32
  if (IsControllerLocked())
    text = Format(LocalizeString(IDS_LOCKED_CONTROLLER_REMOVED), GetActiveController() + 1);
  else
#endif
    text = LocalizeString(IDS_CONTROLLER_REMOVED);
  return new ControllerRemovedMsgBox(NULL, text, -1); 
}

///////////////////////////////////////////////////////////////////////////////
// class ActiveControllerRemovedMsgBox

//! Active Controller Removed message box class
class ActiveControllerRemovedMsgBox : public MsgBox
{
public:
  ActiveControllerRemovedMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL);
  void OnSimulate(EntityAI *vehicle);
};

ActiveControllerRemovedMsgBox::ActiveControllerRemovedMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel)
: MsgBox(parent, MB_BUTTON_OK, text, idd, structuredText, infoOK, infoCancel)
{
}

void ActiveControllerRemovedMsgBox::OnSimulate(EntityAI *vehicle)
{
  MsgBox::OnSimulate(vehicle);
#ifdef _XBOX
  // clear all controller events to avoid handling it in MP game
  for (int i=0; i<XBOX_NoController; i++)
  {
    GInput.xInputButtons[i] = 0;
    GInput.xInputButtonsToDo[i] = false;
    GInput.xInputButtonsUp[i] = false;
    GInput.xInputButtonsPressed[i] = false;
  }
#endif
}

//! creates and returns message box for Controller removed message
MsgBox *CreateActiveControllerRemovedMessageBox()
{
  MsgBoxButton buttonOK(LocalizeString(IDS_DISP_CONTINUE), INPUT_DEVICE_XINPUT + XBOX_Start);
  return new ActiveControllerRemovedMsgBox(NULL, LocalizeString(IDS_ACTIVE_CONTROLLER_REMOVED), -1, false, &buttonOK); 
}


///////////////////////////////////////////////////////////////////////////////
// class NetworkDisconnectedMessageBox

//! Network Disconnected message box class
class NetworkDisconnectedMsgBox : public MsgBox
{
public:
  NetworkDisconnectedMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL);
  void OnButtonClicked(int idc);
  void OnSimulate(EntityAI *vehicle);
};

NetworkDisconnectedMsgBox::NetworkDisconnectedMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel)
: MsgBox(parent, MB_BUTTON_CANCEL, text, idd, structuredText, infoOK, infoCancel)
{
}

void NetworkDisconnectedMsgBox::OnButtonClicked(int idc)
{
  if (idc == IDC_CANCEL)
  {
    // Disconnect from network
    // Sign off handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
    GetNetworkManager().SignOff();
#endif

#if defined _XBOX && _XBOX_VER >= 200
    GSaveSystem.SetReturnToMainMenuRequest(SaveSystem::RRCableDisconnected);
#endif

    GetNetworkManager().Done();

    Exit(IDC_CANCEL);
  }
}

void NetworkDisconnectedMsgBox::OnSimulate(EntityAI *vehicle)
{
#ifdef _XBOX
  DWORD status = XNetGetEthernetLinkStatus();
  if (status != 0)
  {
    Exit(IDC_OK);
    return;
  }
#endif
  MsgBox::OnSimulate(vehicle);
}

ControlsContainer *CreateNetworkDisconnectedMessageBox()
{
  RString text = LocalizeString(IDS_XBOX_MSG_CABLE);
  MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_MP_DISCONNECT), INPUT_DEVICE_XINPUT + XBOX_B);
  return new NetworkDisconnectedMsgBox(NULL, text, -1, false, NULL, &buttonCancel); 
}

#if _USE_BATTL_EYE_SERVER

BattlEyeLicenceMsgBox::BattlEyeLicenceMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel, MsgBoxButton *infoLicence, RString reconnectGUID, RString reconnectPassword)
  : MsgBox(parent, MB_BUTTON_CANCEL | MB_BUTTON_USER | MB_BUTTON_OK, text, idd, structuredText, infoOK, infoCancel, infoLicence), reconnectGUID(reconnectGUID), reconnectPassword(reconnectPassword)
{

}

static void SteamOpenWebBrowser(const RString &url, bool relativeToAppPath)
{
  RString filePath = url;

  // build full path to file if path is relative to game dir
  if (relativeToAppPath && SteamApps())
  {
    // get appid of the current app
    AppId_t appId = SteamUtils()->GetAppID();

    // get path
    int sizeNeeded = SteamApps()->GetAppInstallDir(appId, 0, 0);
    char * dst = filePath.CreateBuffer(sizeNeeded);
    SteamApps()->GetAppInstallDir(appId, dst, sizeNeeded);

    // add url
    filePath = filePath + RString("\\") + url;

    // if file can't be found, revert back to classic url, it's probably web address
    if (!QIFileFunctions::FileExists(filePath))
      filePath = url;
  }
  
  if (SteamFriends())
    SteamFriends()->ActivateGameOverlayToWebPage(cc_cast(filePath));
}

void BattlEyeLicenceMsgBox::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
#if !_BINARIZE
    Glob.config.SetBELicence(true);
#endif
    Exit(IDC_OK);
    break;
  case IDC_CANCEL:
    Exit(IDC_CANCEL);
    break;
  case IDC_USER_BUTTON:
#if !_BINARIZE
#if _ENABLE_STEAM
    {
      RString url = Pars.ReadValue("battleyeLicenceUrl", RString(""));
      if (url.GetLength() > 0)
        SteamOpenWebBrowser(url, true);
    }
#else
    ASSERT_RLS(false, "Missing UI for showing BattlEye licence.");
#endif
#endif
    break;
  default:
    break;
  }
}

BattlEyeLicenceMsgBox *CreateOnBELicenceNotAgreed(ControlsContainer *parent, RString reconnectGUID, RString reconnectPassword)
{
  MsgBoxButton buttonOK(LocalizeString(IDS_MSG_AGREE), INPUT_DEVICE_XINPUT + XBOX_A);
  MsgBoxButton buttonCancel(LocalizeString(IDS_MSG_DISAGREE), INPUT_DEVICE_XINPUT + XBOX_B);
  MsgBoxButton buttonLicence(LocalizeString(IDS_MSG_LICENCE), INPUT_DEVICE_XINPUT + XBOX_Y);
  
  return new BattlEyeLicenceMsgBox(parent, LocalizeString(IDS_MSG_AGREE_BATTLEYE_LICENCE), IDD_MSG_BATTLEYE_LICENCE, true, &buttonOK, &buttonCancel, &buttonLicence, reconnectGUID, reconnectPassword);
}
#endif

///////////////////////////////////////////////////////////////////////////////
// class AskForShutDownMsgBox

//! Network Disconnected message box class
class AskForShutDownMsgBox : public MsgBox
{
public:
  AskForShutDownMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText = false, MsgBoxButton *infoOK = NULL, MsgBoxButton *infoCancel = NULL, MsgBoxButton *infoUser = NULL);
  void OnButtonClicked(int idc);
  void OnSimulate(EntityAI *vehicle);
};

AskForShutDownMsgBox::AskForShutDownMsgBox(ControlsContainer *parent, RString text, int idd, bool structuredText, MsgBoxButton *infoOK, MsgBoxButton *infoCancel, MsgBoxButton *infoUser)
: MsgBox(parent, MB_BUTTON_CANCEL|MB_BUTTON_OK|MB_BUTTON_USER, text, idd, structuredText, infoOK, infoCancel, infoUser)
{
}

#ifdef _CONSOLE
  // dummy definition for Binarize
  void AutoUpdateSetShutdown(bool restart) {}
#else
  //declare only
  void AutoUpdateSetShutdown(bool restart);
#endif

void AskForShutDownMsgBox::OnButtonClicked(int idc)
{
  if (idc == IDC_CANCEL)
  {}  // no special action needed, application ignores the ShutDownAsk
  else if (idc == IDC_OK)
  { // shutDown
    AutoUpdateSetShutdown(false); //do not restart
  }
  else if (idc == IDC_USER_BUTTON)
  { // restart
    AutoUpdateSetShutdown(true);  //shutdown and restart
  }
  Exit(idc);
}

void AskForShutDownMsgBox::OnSimulate(EntityAI *vehicle)
{
  MsgBox::OnSimulate(vehicle);
}

ControlsContainer *CreateAskForShutDownMessageBox()
{
  RString text = "New version of application is available."; //LocalizeString(IDS_ASK_FOR_SHUTDOWN);
  MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_CANCEL), INPUT_DEVICE_XINPUT + XBOX_B);
  MsgBoxButton buttonShutDown("ShutDown"/*LocalizeString(IDS_SHUTDOWN)*/, INPUT_DEVICE_XINPUT + XBOX_A);
  MsgBoxButton buttonRestart("Restart"/*LocalizeString(IDS_RESTART)*/, INPUT_DEVICE_XINPUT + XBOX_X);
  return new AskForShutDownMsgBox(NULL, text, -1, false, &buttonShutDown, &buttonCancel, &buttonRestart); 
}

/*!
\file
Implementation file for particular displays.
\patch_internal 1.01 Date 6/29/2001 by Ondra.
Win32 system calls replaced by corresponding C counterparts to avoid DLL linkage.
DLL linkage make function protection impossible.
*/

void ShowCinemaBorder(bool show);

int GetNetworkPort();
int GetServerPort();
RString GetNetworkPassword();

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
extern RString AutoTest;
#endif

RString GetIdentityText(const PlayerIdentity &identity);

//! Delete directory and whole his content
/*!
  \patch 1.01 Date 06/12/2001 by Jirka
  - Fixed: user cannot be deleted sometimes
  \patch_internal 1.01 Date 06/12/2001 by Jirka
  - fixed - do not delete only current and parent directory
  - no directory starting with "." was not deleted
\patch 5152 Date 3/30/2007 by Jirka
- Fixed: Very long MP mission name could cause crashes or freezes
\patch 5253 Date 5/9/2008 by Bebul
- Fixed: Changes of BattlEye filter in server browser were not saved.
\patch 5253 Date 5/9/2008 by Bebul
- New: BattlEye filter to show servers not running BattleEye in server browser
*/

bool DeleteDirectoryStructure(const char *name, bool deleteDir =true)
{
  if (!name || *name == 0) return false;

  bool ok = true;
#ifdef _WIN32
  BString<1024> buffer;
  sprintf(buffer, "%s\\*.*", name);

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
          sprintf(buffer, "%s\\%s", name, info.name);
          if (!DeleteDirectoryStructure(buffer)) ok = false;
        }
      }
      else
      {
        sprintf(buffer, "%s\\%s", name, info.name);
        if (!QIFileFunctions::Unlink(buffer)) ok = false;
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
  if (deleteDir)
  {
    chmod(name,_S_IREAD | _S_IWRITE);
    if (rmdir(name) != 0) ok = false;
  }
#else
    LocalPath(dname,name);
    DIR *dir = OpenDir(dname);
    if ( !dir ) return false;
    struct dirent *entry;
    int len = strlen(dname);
    dname[len++] = '/';
    while ( (entry = readdir(dir)) ) {      // process one directory item..
        strcpy(dname+len,entry->d_name);
  struct stat st;
  if ( !stat(dname,&st) )             // valid item
      if ( S_ISDIR(st.st_mode) ) {    // sub-directory
          if ( entry->d_name[0] != '.' ||
         entry->d_name[1] &&
         (entry->d_name[1] != '.' || entry->d_name[2]) )
        DeleteDirectoryStructure(dname);
                }
            else {                          // file
                chmod(dname,S_IREAD|S_IWRITE);
    if (unlink(dname) != 0) ok = false;
          }
        }
    closedir(dir);
    if ( deleteDir ) {                      // remove the directory itself
        dname[len-1] = (char)0;
        chmod(dname,S_IREAD|S_IWRITE);
        if (rmdir(dname) != 0) ok = false;
        }
#endif
  return ok;
}

void CopyDirectoryStructure(const char *dst, const char *src)
{
#ifdef _WIN32
  BString<1024> buffer;
  sprintf(buffer, "%s\\*.*", src);

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
          BString<1024> srcNew;
          BString<1024> dstNew;
          sprintf(srcNew, "%s\\%s", src, info.name);
          sprintf(dstNew, "%s\\%s", dst, info.name);
          mkdir(dstNew);
          CopyDirectoryStructure(dstNew, srcNew);
        }
      }
      else
      {
        BString<1024> srcNew;
        BString<1024> dstNew;
        sprintf(srcNew, "%s\\%s", src, info.name);
        sprintf(dstNew, "%s\\%s", dst, info.name);
        QIFileFunctions::Copy(srcNew, dstNew);
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
#else
    LocalPath(srcname,src);
    LocalPath(dstname,dst);                 // dstname must exist
    DIR *dir = OpenDir(srcname);
    if ( !dir ) return;
    struct dirent *entry;
    int srclen = strlen(srcname);
    srcname[srclen++] = '/';
    int dstlen = strlen(dstname);
    dstname[dstlen++] = '/';
    while ( (entry = readdir(dir)) ) {      // process one src-directory item..
        strcpy(srcname+srclen,entry->d_name);
  strcpy(dstname+dstlen,entry->d_name);
  struct stat st;
  if ( !stat(srcname,&st) )           // valid item
      if ( S_ISDIR(st.st_mode) ) {    // sub-directory
          if ( entry->d_name[0] != '.' ||
         entry->d_name[1] &&
         (entry->d_name[1] != '.' || entry->d_name[2]) ) {
        mkdir(dstname,NEW_DIRECTORY_MODE);
        CopyDirectoryStructure(dstname,srcname);
        }
                }
            else                            // file
                fileCopy(srcname,dstname);
        }
    closedir(dir);
#endif
}

void CreatePath(RString path);

void RunInitFunctionsScript()
{
  RString initScript;
#if USE_PRECOMPILATION
  initScript = Pars >> "CfgFunctions" >> "init";
  if (QFBankQueryFunctions::FileExists(initScript))
  {
    QOStrStream out;
    if (!ScriptVM::DefaultPreprocessorFunctions()->Preprocess(out, initScript, true))
    {
      // preprocessor error
      RptF("RunInitFunctionsScript file %s preprocessor error!", cc_cast(initScript));
      return;
    }
    RString expression = RString(out.str(), out.pcount());
    CompiledExpression compiled;
    GameDataNamespace *globals = GWorld->GetMissionNamespace();
    GameVarSpace local(false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.CompileMultiple(expression, compiled, globals);
    GGameState.Evaluate(expression, compiled, GameState::EvalContext::_default, globals);
    GGameState.EndContext(gshandle);
/*
    // asynchronous running is not sufficient, mission relies on the settings done by this script
    ScriptVM *script = new ScriptVM(initScript, GWorld->GetMissionNamespace()); // mission namespace
    GWorld->AddScriptVM(script, true); // simulate now
*/
  }
#endif
}

void RunInitScript()
{
  GWorld->FadeInMission();
  if (GetMissionDirectory().GetLength() > 0)
  {
    RString initScript;
#if USE_PRECOMPILATION
    initScript = GetMissionDirectory() + RString("init.sqf");
    if (QFBankQueryFunctions::FileExists(initScript))
    {
      ScriptVM *script = new ScriptVM(initScript, GWorld->GetMissionNamespace()); // mission namespace
      GWorld->AddScriptVM(script, true); // simulate now
    }
#endif
    initScript = GetMissionDirectory() + RString("init.sqs");
    if (QFBankQueryFunctions::FileExists(initScript))
    {
      Script *script = new Script("init.sqs", GameValue(), GWorld->GetMissionNamespace(), INT_MAX); // mission namespace
      GWorld->AddScript(script, true); // simulate now
    }
  }
  GWorld->SetNativeEvent(NEInitScript);
}

void RunInitIntroScript()
{
  GWorld->FadeInMission();

  if (GetMissionDirectory().GetLength() > 0)
  {
    RString initScript;
#if USE_PRECOMPILATION
    initScript = GetMissionDirectory() + RString("initintro.sqf");
    if (QFBankQueryFunctions::FileExists(initScript))
    {
      ScriptVM *script = new ScriptVM(initScript, GWorld->GetMissionNamespace()); // mission namespace
      GWorld->AddScriptVM(script, true); // simulate now
    }
#endif
    initScript = GetMissionDirectory() + RString("initintro.sqs");
    if (QFBankQueryFunctions::FileExists(initScript))
    {
      Script *script = new Script("initintro.sqs", GameValue(), GWorld->GetMissionNamespace(), INT_MAX); // mission namespace
      GWorld->AddScript(script, true);  // simulate now
    }
  }
}

// file error message

class UserFileErrorMsg : public MsgBox
{
protected:
  UserFileErrorInfo _error;
  bool _canDelete;

public:
  UserFileErrorMsg(ControlsContainer *parent, const UserFileErrorInfo &error, bool canDelete);

  RString GetName() const {return _error._name;}

  void OnButtonClicked(int idc);
};

UserFileErrorMsg::UserFileErrorMsg(ControlsContainer *parent, const UserFileErrorInfo &error, bool canDelete)
: MsgBox
(
  parent,
  canDelete ? MB_BUTTON_OK | MB_BUTTON_CANCEL : MB_BUTTON_CANCEL,
  Format(LocalizeString(IDS_LOAD_FAILED),
  (const char *)error._name), IDD_MSG_LOAD_FAILED
)
{
  _error = error;
  _canDelete = canDelete;

  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl)
  {
    ctrl->SetShortcut(INPUT_DEVICE_XINPUT + XBOX_A);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(LocalizeString(IDS_XBOX_HINT_REMOVE));
  }
  ctrl = GetCtrl(IDC_CANCEL);
  if (ctrl)
  {
    ctrl->SetShortcut(INPUT_DEVICE_XINPUT + XBOX_B);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(LocalizeString(_canDelete ? IDS_XBOX_HINT_IGNORE : IDS_DISP_CONTINUE));
  }
}

void UserFileErrorMsg::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    // remove corrupted file
    Assert(_canDelete);
    /*
    */
#ifdef _XBOX
# if _XBOX_VER >= 200
    GSaveSystem.DeleteSave(_error._dir);
# else
    // remove the whole save
    DeleteSave(_error._dir, _error._name);
# endif
#else
    QIFileFunctions::Unlink(_error._dir + _error._file);
#endif
    if (!_parent) GWorld->DoneUserFileError(false);
    Exit(IDC_OK);
    break;
  case IDC_CANCEL:
    if (!_parent) GWorld->DoneUserFileError(true);
    Exit(IDC_CANCEL);
    break;
  }
}

ControlsContainer *CreateUserFileError(const UserFileErrorInfo &error, bool canDelete)
{
  return new UserFileErrorMsg(NULL, error, canDelete);
}

MsgBox *CreateUserFileError(ControlsContainer *parent, const UserFileErrorInfo &error, bool canDelete)
{
  return new UserFileErrorMsg(parent, error, canDelete);
}

class CreateSaveErrorMsg : public MsgBox
{
public:
  CreateSaveErrorMsg(ControlsContainer *parent);

  void OnButtonClicked(int idc);
};

CreateSaveErrorMsg::CreateSaveErrorMsg(ControlsContainer *parent)
: MsgBox(parent, MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_DISP_XBOX_MESSAGE_SAVING_NOT_SPACE), -1)
{
  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl)
  {
    ctrl->SetShortcut(INPUT_DEVICE_XINPUT + XBOX_A);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(LocalizeString(IDS_DISP_CONTINUE));
  }
  ctrl = GetCtrl(IDC_CANCEL);
  if (ctrl)
  {
    ctrl->SetShortcut(INPUT_DEVICE_XINPUT + XBOX_B);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(LocalizeString(IDS_XBOX_HINT_FREE_SLOTS));
  }
}

void CreateSaveErrorMsg::OnButtonClicked(int idc)
{
#ifdef _XBOX
  switch (idc)
  {
  case IDC_CANCEL:
    // Launch Xbox Dashboard
    #if _XBOX_VER>=2
      // TODOX360: check how should dashboard be launched to free space
      XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
      // unreachable: XLaunchNewImage defined as DECLSPEC_NORETURN
    #else
      LD_LAUNCH_DASHBOARD ld;
      ZeroMemory(&ld, sizeof(ld));
      ld.dwReason = XLD_LAUNCH_DASHBOARD_MEMORY;
      ld.dwParameter1 = 'U';
      ld.dwParameter2 = 0;
      XLaunchNewImage(NULL, PLAUNCH_DATA(&ld));
      Fail("Unaccessible");
    #endif
    return;
  }
#endif
  return MsgBox::OnButtonClicked(idc);
}

ControlsContainer *CreateCreateSaveError()
{
  return new CreateSaveErrorMsg(NULL);
}

#if defined _XBOX && _XBOX_VER >= 200

class XboxSaveErrorMsg : public MsgBox
{
public:
  XboxSaveErrorMsg(ControlsContainer *parent, const XBoxSaveErrorInfo &info);
  void OnButtonClicked(int idc);
  bool IsDiskFull() const;
protected:
  // Message box parameters
  XBoxSaveErrorInfo _info;
};

XboxSaveErrorMsg::XboxSaveErrorMsg(ControlsContainer *parent, const XBoxSaveErrorInfo &info)
: MsgBox
(
 parent,
 MB_BUTTON_OK | MB_BUTTON_CANCEL,
 // select a correct label according to the message box type
 info._errorCode == ERROR_DISK_FULL ?
 Format( LocalizeString(IDS_MSG_SAVE_FAILED), (const char *)info._name) : 
 Format( LocalizeString(IDS_XBOX_SAVE_FAILED), (const char *)info._name),
 -1
 )
{
  // save parameters
  _info = info;

  // set correct labels to the message box buttons
  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl)
  {
    ctrl->SetShortcut(INPUT_DEVICE_XINPUT + XBOX_A);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) 
    {
      if (IsDiskFull())
        text->SetText(LocalizeString(IDS_XBOX_NEW_STORAGE));
      else
        text->SetText(LocalizeString(IDS_XBOX_SAVE_OVERWRITE));
    }
  }
  ctrl = GetCtrl(IDC_CANCEL);
  if (ctrl)
  {
    ctrl->SetShortcut(INPUT_DEVICE_XINPUT + XBOX_B);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) 
    {
      if (IsDiskFull())
        text->SetText(LocalizeString(IDS_DISP_CONTINUE));
      else
        text->SetText(LocalizeString(IDS_XBOX_HINT_IGNORE));
    }
  }
}

bool XboxSaveErrorMsg::IsDiskFull() const
{
  return _info._errorCode == ERROR_DISK_FULL;
}

void XboxSaveErrorMsg::OnButtonClicked(int idc)
{
  // this code handles two message boxes, one about not enough space 
  // on storage device and another about corrupted save file.

  bool removeStorage = true;
  bool storageChosen = true;

  // in case of storage full message and user mission, delete the whole package
  // because the file in it is incomplete
  if (IsDiskFull() && SaveSystem::SEUserMission == _info._type)
    GSaveSystem.DeleteSave(_info._dir);

  // if new storage option or save delete option has been selected
  if (IDC_OK == idc)
  {
    // delete save if it's a save corrupt message
    if( !IsDiskFull() )
      GSaveSystem.DeleteSave(_info._dir);
    else
      // or offer a selection of a new storage device 
      storageChosen = GSaveSystem.SelectDevice(CHECK_SAVE_BYTES, true);

    // if it's a corrupt save message or new storage device has been 
    // confirmed, request a new save
    if (!IsDiskFull() || storageChosen)
    {
      // cancel storage removing.
      removeStorage = false;

      // request a new save
      switch (_info._type)
      {
      case SaveSystem::SESaveUser:
        ControlsContainer::SetSaveRequest(RTResaveUserSave);
        break;
      case SaveSystem::SESaveUser2:
        ControlsContainer::SetSaveRequest(RTResaveUserSave2);
        break;
      case SaveSystem::SESaveUser3:
        ControlsContainer::SetSaveRequest(RTResaveUserSave3);
        break;
      case SaveSystem::SESaveUser4:
        ControlsContainer::SetSaveRequest(RTResaveUserSave4);
        break;
      case SaveSystem::SESaveUser5:
        ControlsContainer::SetSaveRequest(RTResaveUserSave5);
        break;
      case SaveSystem::SESaveUser6:
        ControlsContainer::SetSaveRequest(RTResaveUserSave6);
        break;
      case SaveSystem::SESaveAuto:
        ControlsContainer::SetSaveRequest(RTResaveMissionAutosave);
        break;
      case SaveSystem::SEUserMission:
        ControlsContainer::SetSaveRequest(RTResaveUserMission);
        break;
      default:
        Fail("Unexpected error type");
      }
    }
  }

  // if it's a message about storage device and remove of storage device
  // has been requested, remove it.
  if (IsDiskFull() && removeStorage)
  {
    // storage selection canceled
    // lets force storage device removal
    GSaveSystem.RemoveStorageDevice();
    // we don't want to show dialog about storage device removal...
    GSaveSystem.StorageRemovalDialogDeclined();
  }

  // Always set a request to re-save a missionContinue (if type is missionContinue), 
  // because it's necessary to call AbortMission()
  if (SaveSystem::SESaveContinue == _info._type)
    ControlsContainer::SetSaveRequest(RTResaveMissionContinue);

  // Close message box
  Exit(idc);
}

ControlsContainer *CreateXboxSaveError(const XBoxSaveErrorInfo &error)
{
  return new XboxSaveErrorMsg(NULL, error);
}

#endif

// Configure controls display
static int ReservedKeys[] =
{
  // pause
  DIK_ESCAPE,
  // DayZ: Free up the numbers for further use (will be user configured keys from the inventory screen)
  //       Fkeys are also going to be freed up, but we keep those for future use, no current design yet for them
  // menu
  //DIK_1,DIK_2,DIK_3,DIK_4,DIK_5,DIK_6,DIK_7,DIK_8,DIK_9,DIK_0,
  // units
  //DIK_F1,DIK_F2,DIK_F3,DIK_F4,DIK_F5,DIK_F6,DIK_F7,DIK_F8,DIK_F9,DIK_F10,DIK_F11,DIK_F12
};

bool IsReservedKey(int dikCode)
{
  int n = sizeof(ReservedKeys) / sizeof(int);

  for (int i=0; i<n; i++)
    if (dikCode == ReservedKeys[i]) return true;

  return false;
};

static RString CreateShortJoystickName(RString name)
{
  const int MaxJoystickPrefixLen = 16;
  if (name.GetLength()<=MaxJoystickPrefixLen) return name;
  char buf[MaxJoystickPrefixLen+1];
  int bestIx = 0;
  int ix=0;
  for (int i=0; i<name.GetLength(); i++)
  {
    if (ix>=MaxJoystickPrefixLen)
    {
      if (bestIx>0) buf[bestIx]=0; 
      break;
    }
    if (i>0 && name[i]==' ') bestIx=ix;
    if (isalnum(name[i]) || name[i]==' ') buf[ix++]=name[i];
  }
  buf[ix]=0;
  return RString(buf);
}

static bool KeyIsDisabled(int key)
{
  switch (key & INPUT_DEVICE_MASK)
  {
  case INPUT_DEVICE_STICK:
  case INPUT_DEVICE_STICK_AXIS:
  case INPUT_DEVICE_STICK_POV:
    {
      int keyOffset = ((key - (key & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep) * JoystickDevices::OffsetStep;
#ifdef _WIN32
      JoystickDevices::RecognizedJoystick *joyInfo = GJoystickDevices->GetJoystickInfo(keyOffset);
      if (joyInfo)
        return (joyInfo->mode!=JMCustom);
      else
#endif
        return true; // treat unknown as disabled
    }
    break;
#if defined _WIN32 && !defined _XBOX
  case INPUT_DEVICE_TRACKIR:
    {
      return (!TrackIRDev->IsActive() || !TrackIRDev->IsEnabled()) &&
             (!FreeTrackDev->IsActive() || !FreeTrackDev->IsEnabled());
    }
#endif
  }
  return false;
}

#define AXIS_INFO_TEST(IX) ( joyAxisInfos==NULL || !joyAxisInfos[IX].active || joyAxisInfos[IX].name.IsEmpty() )

RString GetKeyName(int dikCode)
{
  if ((dikCode & INPUT_DEVICE_MASK) == INPUT_DEVICE_XINPUT)
  {
    // need to store to the stringtable?
    static const char *XInputButtonNames[] =
    {
      "XBox A",
      "XBox B",
      "XBox X",
      "XBox Y",
      "XBox Up",
      "XBox Down",
      "XBox Left",
      "XBox Right",
      "XBox Start",
      "XBox Back",
      "XBox Black",
      "XBox White",
      "XBox Left Trigger",
      "XBox Right Trigger",
      "XBox Left Thumb",
      "XBox Right Thumb",
      "XBox Left Thumb X Right",
      "XBox Left Thumb Y Up",
      "XBox Right Thumb X Right",
      "XBox Right Thumb Y Up",
      "XBox Left Thumb X Left",
      "XBox Left Thumb Y Down",
      "XBox Right Thumb X Left",
      "XBox Right Thumb Y Down"
    };

    int index = dikCode - INPUT_DEVICE_XINPUT;
    if (index >= 0 && index < lenof(XInputButtonNames))
      return XInputButtonNames[index];
    else
      return RString();
  }

  if (dikCode>=KEYBOARD_COMBO_OFFSET)
  { // get both parts o combinations and combine them to form DIK_X+DIK_Y
    int dikCombo, dikKey;
    GInput.SeparateComboAndKey(dikCode, dikCombo, dikKey);
    RString comboStr = GetKeyName(dikCombo);
    RString keyStr = GetKeyName(dikKey);
    return Format("%s+%s", cc_cast(comboStr), cc_cast(keyStr));
  }
  if (dikCode<INPUT_DEVICE_KEYBOARD+2*KEYBOARD_DOUBLE_TAP_OFFSET)
  {
    RString codeStr;
    int lowDikCode = dikCode;
    if (dikCode>=KEYBOARD_DOUBLE_TAP_OFFSET) lowDikCode = dikCode-KEYBOARD_DOUBLE_TAP_OFFSET;
    switch (lowDikCode)
    {
    case DIK_ESCAPE: codeStr = LocalizeString(IDS_DIK_ESCAPE); break;
    case DIK_1: codeStr = LocalizeString(IDS_DIK_1); break;
    case DIK_2: codeStr = LocalizeString(IDS_DIK_2); break;
    case DIK_3: codeStr = LocalizeString(IDS_DIK_3); break;
    case DIK_4: codeStr = LocalizeString(IDS_DIK_4); break;
    case DIK_5: codeStr = LocalizeString(IDS_DIK_5); break;
    case DIK_6: codeStr = LocalizeString(IDS_DIK_6); break;
    case DIK_7: codeStr = LocalizeString(IDS_DIK_7); break;
    case DIK_8: codeStr = LocalizeString(IDS_DIK_8); break;
    case DIK_9: codeStr = LocalizeString(IDS_DIK_9); break;
    case DIK_0: codeStr = LocalizeString(IDS_DIK_0); break;
    case DIK_MINUS: codeStr = LocalizeString(IDS_DIK_MINUS); break;
    case DIK_EQUALS: codeStr = LocalizeString(IDS_DIK_EQUALS); break;
    case DIK_BACK: codeStr = LocalizeString(IDS_DIK_BACK); break;
    case DIK_TAB: codeStr = LocalizeString(IDS_DIK_TAB); break;
    case DIK_Q: codeStr = LocalizeString(IDS_DIK_Q); break;
    case DIK_W: codeStr = LocalizeString(IDS_DIK_W); break;
    case DIK_E: codeStr = LocalizeString(IDS_DIK_E); break;
    case DIK_R: codeStr = LocalizeString(IDS_DIK_R); break;
    case DIK_T: codeStr = LocalizeString(IDS_DIK_T); break;
    case DIK_Y: codeStr = LocalizeString(IDS_DIK_Y); break;
    case DIK_U: codeStr = LocalizeString(IDS_DIK_U); break;
    case DIK_I: codeStr = LocalizeString(IDS_DIK_I); break;
    case DIK_O: codeStr = LocalizeString(IDS_DIK_O); break;
    case DIK_P: codeStr = LocalizeString(IDS_DIK_P); break;
    case DIK_LBRACKET: codeStr = LocalizeString(IDS_DIK_LBRACKET); break;
    case DIK_RBRACKET: codeStr = LocalizeString(IDS_DIK_RBRACKET); break;
    case DIK_RETURN: codeStr = LocalizeString(IDS_DIK_RETURN); break;
    case DIK_LCONTROL: codeStr = LocalizeString(IDS_DIK_LCONTROL); break;
    case DIK_A: codeStr = LocalizeString(IDS_DIK_A); break;
    case DIK_S: codeStr = LocalizeString(IDS_DIK_S); break;
    case DIK_D: codeStr = LocalizeString(IDS_DIK_D); break;
    case DIK_F: codeStr = LocalizeString(IDS_DIK_F); break;
    case DIK_G: codeStr = LocalizeString(IDS_DIK_G); break;
    case DIK_H: codeStr = LocalizeString(IDS_DIK_H); break;
    case DIK_J: codeStr = LocalizeString(IDS_DIK_J); break;
    case DIK_K: codeStr = LocalizeString(IDS_DIK_K); break;
    case DIK_L: codeStr = LocalizeString(IDS_DIK_L); break;
    case DIK_SEMICOLON: codeStr = LocalizeString(IDS_DIK_SEMICOLON); break;
    case DIK_APOSTROPHE: codeStr = LocalizeString(IDS_DIK_APOSTROPHE); break;
    case DIK_GRAVE: codeStr = LocalizeString(IDS_DIK_GRAVE); break;
    case DIK_LSHIFT: codeStr = LocalizeString(IDS_DIK_LSHIFT); break;
    case DIK_BACKSLASH: codeStr = LocalizeString(IDS_DIK_BACKSLASH); break;
    case DIK_Z: codeStr = LocalizeString(IDS_DIK_Z); break;
    case DIK_X: codeStr = LocalizeString(IDS_DIK_X); break;
    case DIK_C: codeStr = LocalizeString(IDS_DIK_C); break;
    case DIK_V: codeStr = LocalizeString(IDS_DIK_V); break;
    case DIK_B: codeStr = LocalizeString(IDS_DIK_B); break;
    case DIK_N: codeStr = LocalizeString(IDS_DIK_N); break;
    case DIK_M: codeStr = LocalizeString(IDS_DIK_M); break;
    case DIK_COMMA: codeStr = LocalizeString(IDS_DIK_COMMA); break;
    case DIK_PERIOD: codeStr = LocalizeString(IDS_DIK_PERIOD); break;
    case DIK_SLASH: codeStr = LocalizeString(IDS_DIK_SLASH); break;
    case DIK_RSHIFT: codeStr = LocalizeString(IDS_DIK_RSHIFT); break;
    case DIK_MULTIPLY: codeStr = LocalizeString(IDS_DIK_MULTIPLY); break;
    case DIK_LMENU: codeStr = LocalizeString(IDS_DIK_LMENU); break;
    case DIK_SPACE: codeStr = LocalizeString(IDS_DIK_SPACE); break;
    case DIK_CAPITAL: codeStr = LocalizeString(IDS_DIK_CAPITAL); break;
    case DIK_F1: codeStr = LocalizeString(IDS_DIK_F1); break;
    case DIK_F2: codeStr = LocalizeString(IDS_DIK_F2); break;
    case DIK_F3: codeStr = LocalizeString(IDS_DIK_F3); break;
    case DIK_F4: codeStr = LocalizeString(IDS_DIK_F4); break;
    case DIK_F5: codeStr = LocalizeString(IDS_DIK_F5); break;
    case DIK_F6: codeStr = LocalizeString(IDS_DIK_F6); break;
    case DIK_F7: codeStr = LocalizeString(IDS_DIK_F7); break;
    case DIK_F8: codeStr = LocalizeString(IDS_DIK_F8); break;
    case DIK_F9: codeStr = LocalizeString(IDS_DIK_F9); break;
    case DIK_F10: codeStr = LocalizeString(IDS_DIK_F10); break;
    case DIK_NUMLOCK: codeStr = LocalizeString(IDS_DIK_NUMLOCK); break;
    case DIK_SCROLL: codeStr = LocalizeString(IDS_DIK_SCROLL); break;
    case DIK_NUMPAD7: codeStr = LocalizeString(IDS_DIK_NUMPAD7); break;
    case DIK_NUMPAD8: codeStr = LocalizeString(IDS_DIK_NUMPAD8); break;
    case DIK_NUMPAD9: codeStr = LocalizeString(IDS_DIK_NUMPAD9); break;
    case DIK_SUBTRACT: codeStr = LocalizeString(IDS_DIK_SUBTRACT); break;
    case DIK_NUMPAD4: codeStr = LocalizeString(IDS_DIK_NUMPAD4); break;
    case DIK_NUMPAD5: codeStr = LocalizeString(IDS_DIK_NUMPAD5); break;
    case DIK_NUMPAD6: codeStr = LocalizeString(IDS_DIK_NUMPAD6); break;
    case DIK_ADD: codeStr = LocalizeString(IDS_DIK_ADD); break;
    case DIK_NUMPAD1: codeStr = LocalizeString(IDS_DIK_NUMPAD1); break;
    case DIK_NUMPAD2: codeStr = LocalizeString(IDS_DIK_NUMPAD2); break;
    case DIK_NUMPAD3: codeStr = LocalizeString(IDS_DIK_NUMPAD3); break;
    case DIK_NUMPAD0: codeStr = LocalizeString(IDS_DIK_NUMPAD0); break;
    case DIK_DECIMAL: codeStr = LocalizeString(IDS_DIK_DECIMAL); break;
    case DIK_OEM_102: codeStr = LocalizeString(IDS_DIK_OEM_102); break;
    case DIK_F11: codeStr = LocalizeString(IDS_DIK_F11); break;
    case DIK_F12: codeStr = LocalizeString(IDS_DIK_F12); break;
    case DIK_F13: codeStr = LocalizeString(IDS_DIK_F13); break;
    case DIK_F14: codeStr = LocalizeString(IDS_DIK_F14); break;
    case DIK_F15: codeStr = LocalizeString(IDS_DIK_F15); break;
    case DIK_KANA: codeStr = LocalizeString(IDS_DIK_KANA); break;
    case DIK_ABNT_C1: codeStr = LocalizeString(IDS_DIK_ABNT_C1); break;
    case DIK_CONVERT: codeStr = LocalizeString(IDS_DIK_CONVERT); break;
    case DIK_NOCONVERT: codeStr = LocalizeString(IDS_DIK_NOCONVERT); break;
    case DIK_YEN: codeStr = LocalizeString(IDS_DIK_YEN); break;
    case DIK_ABNT_C2: codeStr = LocalizeString(IDS_DIK_ABNT_C2); break;
    case DIK_NUMPADEQUALS: codeStr = LocalizeString(IDS_DIK_NUMPADEQUALS); break;
    case DIK_PREVTRACK: codeStr = LocalizeString(IDS_DIK_PREVTRACK); break;
    case DIK_AT: codeStr = LocalizeString(IDS_DIK_AT); break;
    case DIK_COLON: codeStr = LocalizeString(IDS_DIK_COLON); break;
    case DIK_UNDERLINE: codeStr = LocalizeString(IDS_DIK_UNDERLINE); break;
    case DIK_KANJI: codeStr = LocalizeString(IDS_DIK_KANJI); break;
    case DIK_STOP: codeStr = LocalizeString(IDS_DIK_STOP); break;
    case DIK_AX: codeStr = LocalizeString(IDS_DIK_AX); break;
    case DIK_UNLABELED: codeStr = LocalizeString(IDS_DIK_UNLABELED); break;
    case DIK_NEXTTRACK: codeStr = LocalizeString(IDS_DIK_NEXTTRACK); break;
    case DIK_NUMPADENTER: codeStr = LocalizeString(IDS_DIK_NUMPADENTER); break;
    case DIK_RCONTROL: codeStr = LocalizeString(IDS_DIK_RCONTROL); break;
    case DIK_MUTE: codeStr = LocalizeString(IDS_DIK_MUTE); break;
    case DIK_CALCULATOR: codeStr = LocalizeString(IDS_DIK_CALCULATOR); break;
    case DIK_PLAYPAUSE: codeStr = LocalizeString(IDS_DIK_PLAYPAUSE); break;
    case DIK_MEDIASTOP: codeStr = LocalizeString(IDS_DIK_MEDIASTOP); break;
    case DIK_VOLUMEDOWN: codeStr = LocalizeString(IDS_DIK_VOLUMEDOWN); break;
    case DIK_VOLUMEUP: codeStr = LocalizeString(IDS_DIK_VOLUMEUP); break;
    case DIK_WEBHOME: codeStr = LocalizeString(IDS_DIK_WEBHOME); break;
    case DIK_NUMPADCOMMA: codeStr = LocalizeString(IDS_DIK_NUMPADCOMMA); break;
    case DIK_DIVIDE: codeStr = LocalizeString(IDS_DIK_DIVIDE); break;
    case DIK_SYSRQ: codeStr = LocalizeString(IDS_DIK_SYSRQ); break;
    case DIK_RMENU: codeStr = LocalizeString(IDS_DIK_RMENU); break;
    case DIK_PAUSE: codeStr = LocalizeString(IDS_DIK_PAUSE); break;
    case DIK_HOME: codeStr = LocalizeString(IDS_DIK_HOME); break;
    case DIK_UP: codeStr = LocalizeString(IDS_DIK_UP); break;
    case DIK_PRIOR: codeStr = LocalizeString(IDS_DIK_PRIOR); break;
    case DIK_LEFT: codeStr = LocalizeString(IDS_DIK_LEFT); break;
    case DIK_RIGHT: codeStr = LocalizeString(IDS_DIK_RIGHT); break;
    case DIK_END: codeStr = LocalizeString(IDS_DIK_END); break;
    case DIK_DOWN: codeStr = LocalizeString(IDS_DIK_DOWN); break;
    case DIK_NEXT: codeStr = LocalizeString(IDS_DIK_NEXT); break;
    case DIK_INSERT: codeStr = LocalizeString(IDS_DIK_INSERT); break;
    case DIK_DELETE: codeStr = LocalizeString(IDS_DIK_DELETE); break;
    case DIK_LWIN: codeStr = LocalizeString(IDS_DIK_LWIN); break;
    case DIK_RWIN: codeStr = LocalizeString(IDS_DIK_RWIN); break;
    case DIK_APPS: codeStr = LocalizeString(IDS_DIK_APPS); break;
    case DIK_POWER: codeStr = LocalizeString(IDS_DIK_POWER); break;
    case DIK_SLEEP: codeStr = LocalizeString(IDS_DIK_SLEEP); break;
    case DIK_WAKE: codeStr = LocalizeString(IDS_DIK_WAKE); break;
    case DIK_WEBSEARCH: codeStr = LocalizeString(IDS_DIK_WEBSEARCH); break;
    case DIK_WEBFAVORITES: codeStr = LocalizeString(IDS_DIK_WEBFAVORITES); break;
    case DIK_WEBREFRESH: codeStr = LocalizeString(IDS_DIK_WEBREFRESH); break;
    case DIK_WEBSTOP: codeStr = LocalizeString(IDS_DIK_WEBSTOP); break;
    case DIK_WEBFORWARD: codeStr = LocalizeString(IDS_DIK_WEBFORWARD); break;
    case DIK_WEBBACK: codeStr = LocalizeString(IDS_DIK_WEBBACK); break;
    case DIK_MYCOMPUTER: codeStr = LocalizeString(IDS_DIK_MYCOMPUTER); break;
    case DIK_MAIL: codeStr = LocalizeString(IDS_DIK_MAIL); break;
    case DIK_MEDIASELECT: codeStr = LocalizeString(IDS_DIK_MEDIASELECT); break;
    default: codeStr = ""; 
    }
    if (dikCode>=KEYBOARD_DOUBLE_TAP_OFFSET)
    {
      return Format(LocalizeString(IDS_KEYBOARD_FORMAT_DOUBLE_TAP), cc_cast(codeStr));
    }
    return codeStr;
  }
  // Determine prefix to distinguish between more joysticks
  RString joystickPrefix=RString("");
  int joyOffset = 0;
  switch (dikCode & INPUT_DEVICE_MASK)
  {
  case INPUT_DEVICE_STICK: 
  case INPUT_DEVICE_STICK_AXIS:
  case INPUT_DEVICE_STICK_POV:
    joyOffset = ((dikCode-(dikCode & INPUT_DEVICE_MASK))/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
    break;
  }
  bool preferTrackIR = true;
  const JoyAxisInfo *joyAxisInfos = NULL;
#ifdef _WIN32
  if (joyOffset>0 && GJoystickDevices) 
  {
    // clear the joystick offset from the dikcode
    dikCode -= joyOffset;
    JoystickDevices::RecognizedJoystick *joyInfo = GJoystickDevices->GetJoystickInfo(joyOffset);
    if (joyInfo)
    {
      joystickPrefix = CreateShortJoystickName(joyInfo->name) + RString(" ");
    }
    for (int i=0; i<GInput._joysticksState.Size(); i++)
    {
      if (GInput._joysticksState[i].offset==joyOffset)
      {
        joyAxisInfos = GJoystickDevices->Get(i)->GetJoyAxesInfo();
      }
    }
  }
#endif
#if defined _WIN32 && !defined _XBOX
  preferTrackIR = (FreeTrackDev==NULL || !FreeTrackDev->IsActive() || !FreeTrackDev->IsEnabled()) || (TrackIRDev && TrackIRDev->IsWorking());
#endif
  switch (dikCode)
  {
  case INPUT_DEVICE_MOUSE: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_0);
  case INPUT_DEVICE_MOUSE + 1: return Format(LocalizeString(IDS_MOUSE_FORMAT_HOLD), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_1)));
  case INPUT_DEVICE_MOUSE + 2: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_2);
  case INPUT_DEVICE_MOUSE + 3: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_3);
  case INPUT_DEVICE_MOUSE + 4: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_4);
  case INPUT_DEVICE_MOUSE + 5: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_5);
  case INPUT_DEVICE_MOUSE + 6: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_6);
  case INPUT_DEVICE_MOUSE + 7: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_7);
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_0)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 1: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_1)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 2: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_2)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 3: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_3)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 4: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_4)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 5: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_5)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 6: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_6)));
  case INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET + 7: return Format(LocalizeString(IDS_MOUSE_FORMAT_DOUBLE_CLICK), cc_cast(LocalizeString(IDS_INPUT_DEVICE_MOUSE_7)));
  case INPUT_DEVICE_MOUSE+MOUSE_CLICK_OFFSET + 1: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_1);
  case INPUT_DEVICE_MOUSE_AXIS + 0: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_LEFT);
  case INPUT_DEVICE_MOUSE_AXIS + 1: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_RIGHT);
  case INPUT_DEVICE_MOUSE_AXIS + 2: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_UP);
  case INPUT_DEVICE_MOUSE_AXIS + 3: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_DOWN);
  case INPUT_DEVICE_MOUSE_WHEEL_UP: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_WHEEL_UP);
  case INPUT_DEVICE_MOUSE_WHEEL_DOWN: return LocalizeString(IDS_INPUT_DEVICE_MOUSE_WHEEL_DOWN);

  case INPUT_DEVICE_TRACKIR + 0: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_0) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_0)); //TrackIR Rot Down
  case INPUT_DEVICE_TRACKIR + 1: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_1) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_1)); //TrackIR Rot Left
  case INPUT_DEVICE_TRACKIR + 2: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_2) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_2)); //TrackIR +rZ
  case INPUT_DEVICE_TRACKIR + 3: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_3) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_3)); //TrackIR Left
  case INPUT_DEVICE_TRACKIR + 4: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_4) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_4)); //TrackIR +tY
  case INPUT_DEVICE_TRACKIR + 5: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_5) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_5)); //TrackIR +tZ
  case INPUT_DEVICE_TRACKIR + 6: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_6) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_6)); //TrackIR Rot Up
  case INPUT_DEVICE_TRACKIR + 7: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_7) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_7)); //TrackIR Rot Right
  case INPUT_DEVICE_TRACKIR + 8: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_8) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_8)); //TrackIR -rZ
  case INPUT_DEVICE_TRACKIR + 9: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_9) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_9)); //TrackIR Right
  case INPUT_DEVICE_TRACKIR + 10: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_10) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_10)); //TrackIR -tY
  case INPUT_DEVICE_TRACKIR + 11: return (preferTrackIR ? LocalizeString(IDS_INPUT_DEVICE_TRACKIR_11) : LocalizeString(IDS_INPUT_DEVICE_FREETRACK_11)); //TrackIR -tZ

  case INPUT_DEVICE_STICK: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_0);
  case INPUT_DEVICE_STICK + 1: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_1);
  case INPUT_DEVICE_STICK + 2: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_2);
  case INPUT_DEVICE_STICK + 3: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_3);
  case INPUT_DEVICE_STICK + 4: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_4);
  case INPUT_DEVICE_STICK + 5: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_5);
  case INPUT_DEVICE_STICK + 6: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_6);
  case INPUT_DEVICE_STICK + 7: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_7);
  case INPUT_DEVICE_STICK + 8: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_8);
  case INPUT_DEVICE_STICK + 9: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_9);
  case INPUT_DEVICE_STICK + 10: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_10);
  case INPUT_DEVICE_STICK + 11: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_11);
  case INPUT_DEVICE_STICK + 12: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_12);
  case INPUT_DEVICE_STICK + 13: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_13);
  case INPUT_DEVICE_STICK + 14: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_14);
  case INPUT_DEVICE_STICK + 15: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_15);
  case INPUT_DEVICE_STICK + 16: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_16);
  case INPUT_DEVICE_STICK + 17: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_17);
  case INPUT_DEVICE_STICK + 18: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_18);
  case INPUT_DEVICE_STICK + 19: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_19);
  case INPUT_DEVICE_STICK + 20: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_20);
  case INPUT_DEVICE_STICK + 21: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_21);
  case INPUT_DEVICE_STICK + 22: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_22);
  case INPUT_DEVICE_STICK + 23: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_23);
  case INPUT_DEVICE_STICK + 24: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_24);
  case INPUT_DEVICE_STICK + 25: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_25);
  case INPUT_DEVICE_STICK + 26: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_26);
  case INPUT_DEVICE_STICK + 27: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_27);
  case INPUT_DEVICE_STICK + 28: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_28);
  case INPUT_DEVICE_STICK + 29: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_29);
  case INPUT_DEVICE_STICK + 30: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_30);
  case INPUT_DEVICE_STICK + 31: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_STICK_31);

  case INPUT_DEVICE_STICK_AXIS + 0: return joystickPrefix+( AXIS_INFO_TEST(0) ? LocalizeString(IDS_INPUT_DEVICE_STICK_AXIS_X_POS) : joyAxisInfos[0].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 1: return joystickPrefix+( AXIS_INFO_TEST(1) ? LocalizeString(IDS_INPUT_DEVICE_STICK_AXIS_Y_POS) : joyAxisInfos[1].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 2: return joystickPrefix+( AXIS_INFO_TEST(2) ? LocalizeString(IDS_INPUT_DEVICE_STICK_AXIS_Z_POS) : joyAxisInfos[2].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 3: return joystickPrefix+( AXIS_INFO_TEST(3) ? LocalizeString(IDS_INPUT_DEVICE_STICK_ROT_X_POS) :  joyAxisInfos[3].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 4: return joystickPrefix+( AXIS_INFO_TEST(4) ? LocalizeString(IDS_INPUT_DEVICE_STICK_ROT_Y_POS) :  joyAxisInfos[4].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 5: return joystickPrefix+( AXIS_INFO_TEST(5) ? LocalizeString(IDS_INPUT_DEVICE_STICK_ROT_Z_POS) :  joyAxisInfos[5].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 6: return joystickPrefix+( AXIS_INFO_TEST(6) ? LocalizeString(IDS_INPUT_DEVICE_STICK_SLIDER_1_POS) : joyAxisInfos[6].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 7: return joystickPrefix+( AXIS_INFO_TEST(7) ? LocalizeString(IDS_INPUT_DEVICE_STICK_SLIDER_2_POS) : joyAxisInfos[1].name+RString("+") );
  case INPUT_DEVICE_STICK_AXIS + 8: return joystickPrefix+( AXIS_INFO_TEST(0) ? LocalizeString(IDS_INPUT_DEVICE_STICK_AXIS_X_NEG) : joyAxisInfos[0].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 9: return joystickPrefix+( AXIS_INFO_TEST(1) ? LocalizeString(IDS_INPUT_DEVICE_STICK_AXIS_Y_NEG) : joyAxisInfos[1].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 10: return joystickPrefix+( AXIS_INFO_TEST(2) ? LocalizeString(IDS_INPUT_DEVICE_STICK_AXIS_Z_NEG) : joyAxisInfos[2].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 11: return joystickPrefix+( AXIS_INFO_TEST(3) ? LocalizeString(IDS_INPUT_DEVICE_STICK_ROT_X_NEG) : joyAxisInfos[3].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 12: return joystickPrefix+( AXIS_INFO_TEST(4) ? LocalizeString(IDS_INPUT_DEVICE_STICK_ROT_Y_NEG) : joyAxisInfos[4].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 13: return joystickPrefix+( AXIS_INFO_TEST(5) ? LocalizeString(IDS_INPUT_DEVICE_STICK_ROT_Z_NEG) : joyAxisInfos[5].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 14: return joystickPrefix+( AXIS_INFO_TEST(6) ? LocalizeString(IDS_INPUT_DEVICE_STICK_SLIDER_1_NEG) : joyAxisInfos[6].name+RString("-") );
  case INPUT_DEVICE_STICK_AXIS + 15: return joystickPrefix+( AXIS_INFO_TEST(7) ? LocalizeString(IDS_INPUT_DEVICE_STICK_SLIDER_2_NEG) : joyAxisInfos[7].name+RString("-") );

  case INPUT_DEVICE_STICK_POV + 0: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_N);
  case INPUT_DEVICE_STICK_POV + 1: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_NE);
  case INPUT_DEVICE_STICK_POV + 2: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_E);
  case INPUT_DEVICE_STICK_POV + 3: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_SE);
  case INPUT_DEVICE_STICK_POV + 4: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_S);
  case INPUT_DEVICE_STICK_POV + 5: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_SW);
  case INPUT_DEVICE_STICK_POV + 6: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_W);
  case INPUT_DEVICE_STICK_POV + 7: return joystickPrefix+LocalizeString(IDS_INPUT_DEVICE_POV_NW);

  default: return "";
  }
}

const int SpecialKeys[] = 
{
  //special mouse buttons
  INPUT_DEVICE_MOUSE, INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET, //primary mouse button
  //left control
  DIK_LCONTROL,
  //mouse axes
  INPUT_DEVICE_MOUSE_AXIS + 0, INPUT_DEVICE_MOUSE_AXIS + 1, INPUT_DEVICE_MOUSE_AXIS + 2, INPUT_DEVICE_MOUSE_AXIS + 3, 
  //mouse wheel
  INPUT_DEVICE_MOUSE_WHEEL_UP, INPUT_DEVICE_MOUSE_WHEEL_DOWN,
  //trackIR axes
  INPUT_DEVICE_TRACKIR + 0, INPUT_DEVICE_TRACKIR + 1, INPUT_DEVICE_TRACKIR + 2, 
  INPUT_DEVICE_TRACKIR + 3, INPUT_DEVICE_TRACKIR + 4, INPUT_DEVICE_TRACKIR + 5, 
  INPUT_DEVICE_TRACKIR + 6, INPUT_DEVICE_TRACKIR + 7, INPUT_DEVICE_TRACKIR + 8, 
  INPUT_DEVICE_TRACKIR + 9, INPUT_DEVICE_TRACKIR + 10, INPUT_DEVICE_TRACKIR + 11, 
/* // for Multiple Joysticks it is not a good idea to show default values in this list
  //stick axes
  INPUT_DEVICE_STICK_AXIS + 0, INPUT_DEVICE_STICK_AXIS + 1, INPUT_DEVICE_STICK_AXIS + 2,
  INPUT_DEVICE_STICK_AXIS + 3, INPUT_DEVICE_STICK_AXIS + 4, INPUT_DEVICE_STICK_AXIS + 5,
  INPUT_DEVICE_STICK_AXIS + 6, INPUT_DEVICE_STICK_AXIS + 7, INPUT_DEVICE_STICK_AXIS + 8,
  INPUT_DEVICE_STICK_AXIS + 9, INPUT_DEVICE_STICK_AXIS + 10, INPUT_DEVICE_STICK_AXIS + 11,
  INPUT_DEVICE_STICK_AXIS + 12, INPUT_DEVICE_STICK_AXIS + 13, INPUT_DEVICE_STICK_AXIS + 14,
  INPUT_DEVICE_STICK_AXIS + 15,
  //stick pov
  INPUT_DEVICE_STICK_POV + 0, INPUT_DEVICE_STICK_POV + 1, INPUT_DEVICE_STICK_POV + 2, 
  INPUT_DEVICE_STICK_POV + 3, INPUT_DEVICE_STICK_POV + 4, INPUT_DEVICE_STICK_POV + 5, 
  INPUT_DEVICE_STICK_POV + 6, INPUT_DEVICE_STICK_POV + 7, 
*/
  DIK_LCONTROL*KEYBOARD_COMBO_OFFSET+INPUT_DEVICE_SPECIAL_COMBINATIONS, //LCTRL+LMB
  DIK_LCONTROL*KEYBOARD_COMBO_OFFSET+INPUT_DEVICE_SPECIAL_COMBINATIONS+1+MOUSE_CLICK_OFFSET, //LCTRL+RMB
  DIK_LCONTROL*KEYBOARD_COMBO_OFFSET+INPUT_DEVICE_SPECIAL_COMBINATIONS+2, //LCTRL+MouseWheel
  -1 //last value MUST be always -1 to recognize it
};

#ifdef _XBOX
bool XBoxControllerS()
{
  // TODO: Check for controller type
  return true;
}
#endif

INode *GetKeyImageOrName(INode *parent, int dikKey)
{
  KeyImage keyImage = GInput.GetKeyImage(dikKey);
  if (keyImage.id >= 0)
  {
    INode *text = CreateTextImage(parent);
    SetTextAttribute(text, "image", keyImage.image);
    SetTextAttribute(text, "size", keyImage.size);
    return text;
  }
  return CreateTextASCII(parent, RString("\"") + GetKeyName(dikKey) + RString("\""));
}

#if !defined(_XBOX) && defined(_WIN32)

static void SaveScheme()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  ParseUserParams(cfg, &globals);

  // Save the controller scheme
  cfg.Add("controller", GInput.scheme.basicSettings);
  RString sensitivity = FindEnumName(GInput.scheme.sensitivity);
  cfg.Add("sensitivity", sensitivity);
#ifndef _XBOX
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
  cfg.Add("vibrations", GInput.scheme.vibrations);
#endif
  ParamClassPtr schemes = cfg.AddClass("ControllerSchemes");
  for (int i=0; i<GInput.scheme.settings.Size(); i++)
  {
    ParamEntryPtr array = schemes->AddArray(GInput.scheme.settings[i].name);
    array->Clear();
    array->AddValue(RString());
    array->AddValue(GInput.scheme.settings[i].yAxis);
  }

  SaveUserParams(cfg);
}

CKeys::CKeys()
{
  _mode = -1;
  _ignoredKey = -1;
}

void CKeys::SetKeys(int action, const AutoArray<int> &keys)
{
  while (_keys[action].Size() > 0) RemoveKey(action);
  for (int i=0; i<keys.Size(); i++) AddKey(action, keys[i]);
}

static bool InputKyesCollide(int key1, int key2)
{
  if (key1==key2) return true;
  if ( key2>=KEYBOARD_COMBO_OFFSET ) Swap(key1, key2);
  if ( key1>=KEYBOARD_COMBO_OFFSET && key2<KEYBOARD_COMBO_OFFSET )
  { //combo collide with normal key
    int dikCombo, dikKey;
    GInput.SeparateComboAndKey(key1, dikCombo, dikKey);
    if (dikCombo==key2) 
      return true;
  }
  return false;
}

void CKeys::CheckCollisions(int key)
{
  AutoArray<int> keyActions;
  AutoArray<int> keyIx;
  for (int i=0; i<UAN; i++)
    for (int j=0; j<_keys[i].Size(); j++)
      if (InputKyesCollide(_keys[i][j],key))
      {
        keyActions.Add(i); 
        keyIx.Add(j);
        _collisions[i][j]=false; //we will set it again if necessary
      }
  for (int i=0; i<keyActions.Size()-1; i++)
    for (int j=i+1; j<keyActions.Size(); j++)
    {
      if (
        GInput.inputCollisions.Collide(keyActions[i],keyActions[j]) &&
        InputKyesCollide(_keys[keyActions[i]][keyIx[i]], _keys[keyActions[j]][keyIx[j]])
      )
      { //these actions collide
        _collisions[keyActions[i]][keyIx[i]]=true;
        _collisions[keyActions[j]][keyIx[j]]=true;
      }
    }
}

void CKeys::RemoveKey(int action)
{
  int index = _keys[action].Size() - 1;
  if (index < 0) return;

  int key = _keys[action][index];
  
  // delete
  _keys[action].Resize(index);
  _collisions[action].Resize(index);

  // check collisions
  CheckCollisions(key);
}

void CKeys::RemoveKey(int action, int key)
{
  for (int j=0; j<_keys[action].Size(); j++)
  {
    if (_keys[action][j] == key)
    {
      // remove key instead
      // delete
      _keys[action].Delete(j);
      _collisions[action].Delete(j);

      // check collisions
      CheckCollisions(key);
      return;
    }
  }
}

void CKeys::AddKey(int action, int key)
{
  for (int j=0; j<_keys[action].Size(); j++)
  {
    if (_keys[action][j] == key)
    {
      // remove key instead
      // delete
      _keys[action].Delete(j);
      _collisions[action].Delete(j);

      // check collisions
      CheckCollisions(key);

      return;
    }
  }

  _keys[action].Add(key);
  _collisions[action].Add(false);
  CheckCollisions(key);
  // note: Input::backupJoysticksKeys still contains keys mapping for other joysticks not connected now
  //       but it is supposed we want to set only connected joysticks to its default preset values
  //       so these backup values should not be cleared and should be merged at the end to the user cfg key list
}

void CKeys::CheckIgnoredKey()
{
  if (_ignoredKey != -1)
  {
    GInput.keysToDo[_ignoredKey] = false;
    _ignoredKey = -1;
  }
}

C2DKeys::C2DKeys(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CListBox(parent, idc, cls), _doNotOpenConfigureAction(false), _actions(NULL)
{
}

bool C2DKeys::OnKeyDown(int dikCode)
{
  SetDoNotConfigureAction(true);
  bool retVal = CListBox::OnKeyDown(dikCode);
  SetDoNotConfigureAction(false);
  return retVal;
}

void C2DKeys::OnLButtonDown(float x, float y)
{
  if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
    _scrollbar.SetPos(_topString);
    _scrollbar.OnLButtonDown(x, y);
    _topString = _scrollbar.GetPos();
  }
}

void C2DKeys::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top,
  const Rect2DFloat &rect, float widthDiff
)
{
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
      Color bgcol0=bgColor, bgcol1=_selBgColor2;
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    }
    bgColor = ModAlpha(bgColor, alpha);

    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
    float xx = rect.x * w;
    float ww = rect.w * w;
    if (textures)
    {
      const int borderWidth = 2;
      xx += borderWidth;
      ww -= 2 * borderWidth;
    }
    switch (_mode)
    {
    case -1:
      GEngine->Draw2D
      (
        mip, bgColor, Rect2DPixel(xx, top * h, _mainCollumW * ww, SCALED(_rowHeight) * h),
        Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );
      break;
    case 0:
      GEngine->Draw2D
      (
        mip, bgColor, Rect2DPixel(xx + _mainCollumW * ww, top * h, _secndCollumW * ww, SCALED(_rowHeight) * h ),
        Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );
      break;
    }

    selColor = GetSelColor(i);
    if (focused)
    {
      Color col0=color, col1=_selColor2;
      selColor = PackedColor(col0 * factor + col1 * (1 - factor));
    }
    selColor = ModAlpha(selColor, alpha);
  }

  float left = rect.x + border;

  Texture *texture = GetTexture(i);
  if (texture)
  {
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    float width = SCALED(_rowHeight) * (texture->AWidth() * h) / (texture->AHeight() * w);

    PackedColor pictureColor;
    if (selected && _showSelected)
      pictureColor = ModAlpha(_pictureSelectColor, alpha);
    else
      pictureColor = ModAlpha(_pictureColor, alpha);

    GLOB_ENGINE->Draw2D
    (
      mip, pictureColor,
      Rect2DPixel(CX(rect.x + border), CY(top), CW(width), CH(SCALED(_rowHeight))),
      Rect2DPixel(rect.x * w, rect.y * h, _mainCollumW * rect.w * w, rect.h * h)
    );
    left += width + border;
  }
  float t = top + 0.5 * (SCALED(_rowHeight - _size ));
  PackedColor col = _mode == -1 ? selColor : color;
  GLOB_ENGINE->DrawText
  (
    Point2DFloat(left, t), SCALED(_size), Rect2DFloat(rect.x, rect.y, _mainCollumW * rect.w, rect.h),
    _font, color, GetText(i), _shadow
  );

  left = rect.x + _mainCollumW * rect.w + border;
  PackedColor noCollision = _mode == 0 ? selColor : color;
  for (int j=0; j<_keys[GetAction(i)].Size(); j++)
  {
    // prefix
    if (j > 0)
    {
      const char *prefix = ", ";
      GLOB_ENGINE->DrawText
      (
        Point2DFloat(left, t), SCALED(_size), Rect2DFloat(rect.x + _mainCollumW * rect.w, rect.y, _secndCollumW * rect.w, rect.h),
        _font, noCollision, prefix, _shadow
      );
      left += GEngine->GetTextWidth(SCALED(_size), _font, prefix);
    }

    // value
    RString text = RString("\"") + GetKeyName(_keys[GetAction(i)][j]) + RString("\"");
    PackedColor col;
    if ( KeyIsDisabled(_keys[GetAction(i)][j]) )
      col = _disabledKeyColor;
    else
      col = _collisions[GetAction(i)][j] ? _collisionColor : noCollision;
    GLOB_ENGINE->DrawText
    (
      Point2DFloat(left, t), SCALED(_size), Rect2DFloat(rect.x + _mainCollumW * rect.w, rect.y, _secndCollumW * rect.w, rect.h),
      _font, col, text, _shadow
    );
    left += GEngine->GetTextWidth(SCALED(_size), _font, text);
  }
}

bool C2DKeys::IsCollision(int curSel, int dikCode)
{
  int i=0;
  for (; i<_keys[curSel].Size(); i++) if (InputKyesCollide(_keys[curSel][i],dikCode)) break;
  if (i<_keys[curSel].Size())
    return _collisions[curSel][i];
  return false;
}

int C2DKeys::GetFirstCollisionAction(int curSel, int dikCode)
{
  if (IsCollision(curSel,dikCode))
  {
    AutoArray<int> keyActions;
    for (int i=0; i<UAN; i++)
      for (int j=0; j<_keys[i].Size(); j++)
        if (InputKyesCollide(_keys[i][j],dikCode))
        {
          keyActions.Add(i); 
          break;
        }
    for (int i=0; i<keyActions.Size(); i++)
    {
      if (curSel==keyActions[i]) continue;
      if (GInput.inputCollisions.Collide(curSel, keyActions[i]))
      { //these actions collide
        return keyActions[i];
      }
    }
  }
  return -1;
}

static RString GetActionName(int action)
{
  return LocalizeString(GInput.userActionDesc[action].desc);
}

RString C2DKeys::GetCollisionName(int curSel, int dikCode)
{
  if (IsCollision(curSel,dikCode))
  {
    AutoArray<int> keyActions;
    int count = 0;
    int colIx = 0;
    for (int i=0; i<UAN; i++)
    {
      for (int j=0; j<_keys[i].Size(); j++)
        if (InputKyesCollide(_keys[i][j],dikCode))
        {
          keyActions.Add(i); 
          break;
        }
    }
    for (int i=0; i<keyActions.Size(); i++)
    {
      if (curSel==keyActions[i]) continue;
      if (GInput.inputCollisions.Collide(curSel, keyActions[i]))
      { //these actions collide
        if (count==0) colIx=keyActions[i];
        if (++count > 1)
        {
          return RString(GetActionName(colIx)) + RString(",...");
        }
      }
    }
    if (count>0) return RString(GetActionName(colIx));
  }
  return RString("");
}

int C2DKeys::GetActionIx(int action)
{
  if (_actions)
  {
    for (int i=0; i<_actions->Size(); i++)
    {
      if ((*_actions)[i]==action) return i;
    }
  }
  return -1;
}

DisplayConfigureAction::DisplayConfigureAction(ControlsContainer *parent, C2DKeys *keys, int curSel, bool enableSimulation, bool curSelIsAction)
: Display(parent), _allKeys(keys)
{
#if _ENABLE_CHEATS
  GInput.EnableCheats12(false);
#endif
  if (curSelIsAction) _curSel = curSel;
  else _curSel = keys->GetAction(curSel);
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayConfigureAction");

  //Reset Joystick analog input - in order NOT TO SHOW sliders or axes active without previous CLEAR!
  for (int joyIx=0; joyIx<GInput._joysticksState.Size(); joyIx++)
  {
    Input::JoystickState &joy = GInput._joysticksState[joyIx];
    if (joy.mode!=JMCustom) continue;
    for (int i=0; i<N_JOYSTICK_AXES; i++)
    { 
      //stick axes can contain negative values, so check it
      float val=joy.stickAxis[i];
      int index=i;
      if (val<0)
      {
        index+=N_JOYSTICK_AXES;
        val=-val;
      }
      if (val>joy.StickAxisThresholdHigh)
      {
        joy.stickAxisCleared[index]=false; //do not map it without previous clear!
      }
    }
  }

  if (_keys)
  {
    const AutoArray<int>& curkeys = keys->GetKeys(_curSel);
    _oldkeys = curkeys; //backup for clear or cancel functionality
    for (int i=0; i<curkeys.Size(); i++)
    {
      AddDikCode(curkeys[i]);
    }
  }
  if (_special)
  {
     for (int i=0; SpecialKeys[i]!=-1; i++)
     {
       // do not show TrackIR as special input when TrackIR is not used
       if ( (SpecialKeys[i] & INPUT_DEVICE_MASK)==INPUT_DEVICE_TRACKIR ) 
       {
         if ( (!TrackIRDev || !TrackIRDev->IsEnabled() || !TrackIRDev->IsActive()) &&
              (!FreeTrackDev || !FreeTrackDev->IsEnabled() || !FreeTrackDev->IsActive()) )
           continue;
       }
       int index = _special->AddString(GetKeyName(SpecialKeys[i]));
       _special->SetValue(index, SpecialKeys[i]);
     }
  }
  _nextAction = 0;
}

int DisplayConfigureAction::AddDikCode(int dikCode)
{
  bool isCollision = _allKeys->IsCollision(_curSel,dikCode);
  RString keyWithCollisions = GetKeyName(dikCode);
  if (isCollision) keyWithCollisions = keyWithCollisions + Format(" [%s]", cc_cast(_allKeys->GetCollisionName(_curSel,dikCode)));
  int index = _keys->AddString(keyWithCollisions);
  _keys->SetValue(index, dikCode);
  PackedColor col = isCollision ? _collisionColor : _noCollisionCol;
  if (KeyIsDisabled(dikCode)) col = _disabledKeyColor;
  _keys->SetFtColor(index,col);
  _keys->SetSelColor(index,col);
  return index;
}

#define ListItemGray Color(0.3,0.3,0.3,1.0)
#define ListItemRed Color(1.0,0,0,1.0)
Control *DisplayConfigureAction::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_CONFIGURE_ACTION_TITLE:
    {
      ITextContainer *title = GetTextContainer(ctrl);
      RString actionName = GetActionName(_curSel);
      title->SetText(Format(title->GetText(),cc_cast(actionName)));
    }
    break;
  case IDC_CONFIGURE_ACTION_KEYS:
    {
      _noCollisionCol = GetPackedColor(cls >> "colorText");
      ParamEntryPtr entry = cls.FindEntry("collisionColor");
      if (entry) _collisionColor = GetPackedColor(*entry);
      else _collisionColor = PackedColor(ListItemRed);
      entry = cls.FindEntry("disabledKeyColor");
      if (entry) _disabledKeyColor = GetPackedColor(*entry);
      else _disabledKeyColor = PackedColor(ListItemGray);
      _keys = GetListBoxContainer(ctrl);
    }
    break;
  case IDC_CONFIGURE_ACTION_SPECIAL:
    _special = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayConfigureAction::OnLBDblClick(int idc, int curSel)
{
  switch (idc)
  {
  case IDC_CONFIGURE_ACTION_KEYS:
    DisplayConfigure *parent = (DisplayConfigure*)(_parent.GetLink());
    if (parent) 
    {
      int action = _allKeys->GetFirstCollisionAction(_curSel, _keys->GetValue(curSel));
      if (action!=-1)
      {
        //DisplayConfigureAction child should be created by calling ChangeConfigureActionChild(action) from parent dialog
        _nextAction = action;
        Exit(IDC_CONFIGURE_ACTION_NEXT);
      }
    }
    break;
  }
}

bool DisplayConfigureAction::RemoveKey(int value)
{
  for (int i=0; i<_keys->Size(); i++)
  {
    if (_keys->GetValue(i)==value)
    {
      _keys->Delete(i); return true;
    }
  }
  return false;
}

int DisplayConfigureAction::AddKey(int value)
{
  if (!RemoveKey(value))
  {
    int index = AddDikCode(value);
    _keys->SetCurSel(index);
    return index;
  }
  return -1;
}

bool DisplayConfigureAction::OnLBDragging(IControl *ctrl, float x, float y)
{
  if (_dragCtrl)
  {
    switch (_dragCtrl->IDC())
    {
    case IDC_CONFIGURE_ACTION_SPECIAL:
      return ctrl && ctrl->IDC() == IDC_CONFIGURE_ACTION_KEYS && _dragRows.Size() > 0;
    case IDC_CONFIGURE_ACTION_KEYS:
      return (!ctrl || ctrl->IDC()!=IDC_CONFIGURE_ACTION_KEYS) && _dragRows.Size() > 0;
    }
  }
  return Display::OnLBDragging(ctrl, x, y);
}

bool DisplayConfigureAction::OnLBDrop(IControl *ctrl, float x, float y)
{
  if (_dragCtrl)
  {
    switch (_dragCtrl->IDC())
    {
    case IDC_CONFIGURE_ACTION_SPECIAL:
      if (ctrl && ctrl->IDC() == IDC_CONFIGURE_ACTION_KEYS && _dragRows.Size() > 0)
      {
        CListBoxContainer *lbox = GetListBoxContainer(ctrl);
        for (int i=0; i<lbox->Size(); i++)
        {
          if (lbox->GetValue(i)==_dragRows[0].value)
          {
            lbox->Delete(i); 
            _allKeys->RemoveKey(_curSel, _dragRows[0].value);
            return true;
          }
        }
        _allKeys->AddKey(_curSel, _dragRows[0].value);
        AddKey(_dragRows[0].value);
      }
      return true;
    case IDC_CONFIGURE_ACTION_KEYS:
      if ((!ctrl || ctrl->IDC()!=IDC_CONFIGURE_ACTION_KEYS) && _dragRows.Size() > 0)
      { //remove the key
        int value= _dragRows[0].value;
        int curSel=0;
        for (; curSel<_keys->Size(); curSel++)
          if (_keys->GetValue(curSel)==value) break;
        if (curSel==_keys->Size()) return false;
        _allKeys->RemoveKey(_curSel, value);
        _keys->Delete(curSel);
        _keys->SetCurSel(_keys->GetSize());
      }
      return true;
    }
  }

  return Display::OnLBDrop(ctrl, x, y);
}

void DisplayConfigureAction::RestoreBackupKeys()
{
  if (_allKeys) 
  {
    for (int i=0, siz=_allKeys->GetKeys(_curSel).Size(); i<siz; i++) _allKeys->RemoveKey(_curSel);
    _keys->ClearStrings();
    for (int i=0; i<_oldkeys.Size(); i++) 
    {
      _allKeys->AddKey(_curSel, _oldkeys[i]);
      AddDikCode(_oldkeys[i]);
    }
  }
}

void DisplayConfigureAction::OnButtonClicked(int idc)
{
  bool cancel=false;
  switch (idc)
  {
  case IDC_CONFIGURE_ACTION_DELETE:
    {
      int curSel = _keys->GetCurSel();
      if (curSel>=0 && curSel <= _keys->Size()) 
      {
        int value = _keys->GetValue(curSel);
        _allKeys->RemoveKey(_curSel,value);
        _keys->Delete(curSel);
        _keys->SetCurSel(_keys->GetSize());
      }
    }
    return;
  case IDC_CONFIGURE_ACTION_DEFAULT: //get default values for THIS action only
    {
      if (_allKeys) _allKeys->SetKeys(_curSel, GInput.defaultProfiles.GetCurrentProfileKeyList((UserAction)_curSel));
      const AutoArray<int>& curkeys = _allKeys->GetKeys(_curSel);
      _keys->ClearStrings();
      for (int i=0; i<curkeys.Size(); i++)
      {
        AddDikCode(curkeys[i]);
      }
    }
    return;
  case IDC_CONFIGURE_ACTION_CANCEL:
    cancel=true;
    //cancel will clear as well...
  case IDC_CONFIGURE_ACTION_CLEAR:
    {
      RestoreBackupKeys();
      if (cancel) Exit(IDC_CANCEL);
    }
    return;
  case IDC_CONFIGURE_ACTION_PREV:
    {
      DisplayConfigure *parent = (DisplayConfigure*)(_parent.GetLink());
      if (parent) 
      {
        int ix = _allKeys->GetActionIx(_curSel);
        if (ix>=0)
        {
          if (--ix<0) ix+=_allKeys->GetSize();
          _nextAction = GetAction(ix);
          Exit(IDC_CONFIGURE_ACTION_NEXT);
        }
        else 
        { //action not present in parent action list (possibly after dblClick on conflict line)
          if (--_curSel<0) _curSel+=UAN;
          _nextAction = _curSel;
          Exit(IDC_CONFIGURE_ACTION_NEXT);
        }
      }
    }
    return;
  case IDC_CONFIGURE_ACTION_NEXT:
    {
      DisplayConfigure *parent = (DisplayConfigure*)(_parent.GetLink());
      if (parent) 
      {
        int ix = _allKeys->GetActionIx(_curSel);
        if (ix>=0)
        {
          if (++ix>=_allKeys->GetSize()) ix=0;
          _nextAction = GetAction(ix);
          Exit(IDC_CONFIGURE_ACTION_NEXT);
        }
        else
        { //action not present in parent action list (possibly after dblClick on conflict line)
          if (++_curSel>=UAN) _curSel=0;
          _nextAction = _curSel;
          Exit(IDC_CONFIGURE_ACTION_NEXT);
        }
      }
    }
    return;
  }
  Display::OnButtonClicked(idc);
}

//#include "Ui/resincl.hpp"
void Input::ForgetDisplayConfigureKeys()
{
  int idd = IDD_CONFIGURE_ACTION;
  if (GWorld)
  {
    AbstractOptionsUI *opt = GWorld->Options();
    if (opt)
    {
      ControlsContainer *disp = dynamic_cast<ControlsContainer *>(opt);
      while (disp)
      {
        if (disp->IDD() == idd) 
        {
          DisplayConfigureAction *dispConfigure = dynamic_cast<DisplayConfigureAction *>(disp);
          if (dispConfigure) dispConfigure->ForgetKeys();
        }
        disp = disp->Child();
      }
    }
  }
}

void DisplayConfigureAction::ForgetKeys()
{
  _activeKeyList.Clear();
}

#define DISABLE_OPTIONS_CANCEL 1

/*!
  \patch 5112 Date 1/3/2007 by Bebul
  - Fixed: Keys 0,..,9 and F1,..,F12 are reserved even when combined with Ctrl key
*/
bool DisplayConfigureAction::OnKeyDown(int dikCode)
{
  if ((dikCode & INPUT_DEVICE_MASK) == INPUT_DEVICE_XINPUT)
  { // ignore XInput input, as it cannot be customized when XInput controller is used with scheme (see Input::GetUserKeys(int index), add all but XInput keys)
    return true; //handled
  }
  bool dikIsInList = false;
  bool isKeyCombination = false;
  for(ActiveKeyItem *i=_activeKeyList.First(); i!=NULL; i=_activeKeyList.Next(i)) 
  {
    if (i->dik == dikCode) {dikIsInList=true; break; }
  }
  if (dikIsInList) return true; //key is already pressed
  if (!_activeKeyList.Empty()) //this is key combination - assume first item in list is Combo
    isKeyCombination = true;
  _activeKeyList.Add(new ActiveKeyItem(dikCode));
  if (dikCode==DIK_ESCAPE) 
  {
#if !DISABLE_OPTIONS_CANCEL
    RestoreBackupKeys();
#endif
    return Display::OnKeyDown(dikCode);
  }
  bool isReservedKey = false;
  if (
    IsReservedKey(dikCode) && !isKeyCombination
  ) 
    isReservedKey = true;
  int dikCombo = 0; //init only to avoid warning 
  if (isKeyCombination)
  {
    dikCombo = _activeKeyList.First()->dik;
  }

  if (GInput.keysDoubleTapToDo[dikCode]) 
  {
    if (isReservedKey) return true;
    _allKeys->RemoveKey(_curSel, dikCode);
    if (isKeyCombination)
    {
      _allKeys->RemoveKey(_curSel, dikCombo);
      RemoveKey(dikCombo);
      _allKeys->RemoveKey(_curSel, dikCombo*KEYBOARD_COMBO_OFFSET+dikCode);
      RemoveKey(dikCombo*KEYBOARD_COMBO_OFFSET+dikCode); //remove Combo + single Tap
      _allKeys->AddKey(_curSel, dikCombo*KEYBOARD_COMBO_OFFSET+dikCode+KEYBOARD_DOUBLE_TAP_OFFSET);
      AddKey(dikCombo*KEYBOARD_COMBO_OFFSET+dikCode+KEYBOARD_DOUBLE_TAP_OFFSET);
    }
    else 
    {
      _allKeys->AddKey(_curSel, dikCode+KEYBOARD_DOUBLE_TAP_OFFSET);
      AddKey(dikCode+KEYBOARD_DOUBLE_TAP_OFFSET);
    }
    RemoveKey(dikCode);
  }
  else
  {
    if (isReservedKey) return true;
    if (isKeyCombination)
    {
      _allKeys->RemoveKey(_curSel, dikCombo);
      RemoveKey(dikCombo);
      _allKeys->AddKey(_curSel, dikCombo*KEYBOARD_COMBO_OFFSET+dikCode);
      AddKey(dikCombo*KEYBOARD_COMBO_OFFSET+dikCode);
    }
    else
    {
      _allKeys->AddKey(_curSel, dikCode);
      AddKey(dikCode);
    }
  }

  return true;
}

bool DisplayConfigureAction::OnKeyUp(int dikCode)
{
  for(ActiveKeyItem *i=_activeKeyList.First(); i!=NULL; i=_activeKeyList.Next(i)) 
  {
    if (i->dik == dikCode) 
    {
      _activeKeyList.Delete(i); break; 
    }
  }
  return true;
}

void DisplayConfigureAction::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
  //handle special input keys

  int count = 0;
  int dik = -1;
  //discrete input
  for (int i=0; i<N_MOUSE_BUTTONS; i++)
  {
#define DISABLE_PRIMARY_BUTTON_DETECTION 1
#if DISABLE_PRIMARY_BUTTON_DETECTION
    if (i == 0) continue;
#endif

#if RMB_HOLD_CLICK_DBL
    if (i==1) //RMB
    {
      GInput.mouseButtonDoubleClickWanted[i] = GInput.mouseButtonHoldWanted[i] = 
        GInput.mouseButtonClickWanted[i] = true;
      count++;
      if (GInput.mouseButtonsDoubleToDo[i]) 
      {
        _allKeys->RemoveKey(_curSel,INPUT_DEVICE_MOUSE+i);
        RemoveKey(INPUT_DEVICE_MOUSE+i);
        dik = INPUT_DEVICE_MOUSE + MOUSE_DOUBLE_CLICK_OFFSET + i;
      }
      else if (GInput.mouseButtonsClickToDo[i]) 
      {
        _allKeys->RemoveKey(_curSel,INPUT_DEVICE_MOUSE+i);
        RemoveKey(INPUT_DEVICE_MOUSE+i);
        dik = INPUT_DEVICE_MOUSE + MOUSE_CLICK_OFFSET + i;
      }
      else if (GInput.mouseButtonsToDo[i]) 
        dik = INPUT_DEVICE_MOUSE + i;
      else count--; //no RMB input
    }
    else 
#endif
    if (GInput.mouseButtonsToDo[i])
    {
      //check double click
      if (GInput.mouseButtonsDoubleToDo[i]) 
      {
        _allKeys->RemoveKey(_curSel,INPUT_DEVICE_MOUSE+i);
        RemoveKey(INPUT_DEVICE_MOUSE+i);
        dik = INPUT_DEVICE_MOUSE+MOUSE_DOUBLE_CLICK_OFFSET+i;
      }
      else dik = INPUT_DEVICE_MOUSE + i;
      count++;
    }
  }
  // Joysticks
  for (int joyIx=0; joyIx<GInput._joysticksState.Size(); joyIx++)
  {
    Input::JoystickState &joy = GInput._joysticksState[joyIx];
    if (joy.mode!=JMCustom) continue;
    int offset = joy.offset;
    for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
    {
      if (joy.stickButtonsToDo[i])
      {
        dik = INPUT_DEVICE_STICK + offset + i;
        count++;
      }
    }
    for (int i=0; i<N_JOYSTICK_POV; i++)
    {
      if (joy.stickPovToDo[i] && joy.stickPOVCleared[i])
      {
        dik = INPUT_DEVICE_STICK_POV + offset + i;
        joy.stickPOVCleared[i]=false;
        count++;
      }
    }
    //Joystick - analog input
    float maxVal = 0;
    for (int i=0; i<N_JOYSTICK_AXES; i++)
    { 
      //stick axes can contain negative values, so check it
      float val=joy.stickAxis[i];
      int index=i;
      if (val<0)
      {
        index+=N_JOYSTICK_AXES;
        val=-val;
      }
      if (val>maxVal && val>joy.StickAxisThresholdHigh && joy.stickAxisCleared[index])
      {
        dik = INPUT_DEVICE_STICK_AXIS + offset + index;
        joy.stickAxisCleared[index]=false;
        maxVal = val;
      }
    }
    if (maxVal!=0) count++;
  }
/*
  // ignore XInput input, as it cannot be customized when XInput controller is used with scheme (see Input::GetUserKeys(int index), add all but XInput keys)
  // XInput
  for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
  {
    if (GInput.IsXInputPresent() && GInput.xInputButtonsToDo[i])
    {
      dik = INPUT_DEVICE_XINPUT + i;
      count ++;
    }
  }
*/
/*
  //Mouse
  maxVal = 0;
  for (int i=0; i<N_MOUSE_AXES; i++)
  { 
    if (fabs(GInput.mouseAxis[i])>maxVal && fabs(GInput.mouseAxis[i])>GInput.MouseAxisThresholdHigh && GInput.mouseAxisCleared[i])
    {
      dik = INPUT_DEVICE_MOUSE_AXIS + i;
      GInput.mouseAxisCleared[i]=false;
      maxVal = GInput.mouseAxis[i];
    }
  }
  if (maxVal!=0) count++;
*/
  //TrackIR
  float maxVal = 0;
  for (int i=0; i<N_TRACKIR_AXES; i++)
  { 
    //trackIR axes can contain negative values, so check it
    float val=GInput.trackIRAxis[i];
    int index=i;
    if (val<0)
    {
      index+=N_TRACKIR_AXES;
      val=-val;
    }
    if (val>maxVal && val>GInput.TrackIRAxisThresholdHigh && GInput.trackIRAxisCleared[index])
    {
      dik = INPUT_DEVICE_TRACKIR + index;
      GInput.trackIRAxisCleared[index]=false;
      maxVal = val;
    }
  }
  if (maxVal!=0) count++;
  //more then one inputs - do not prefer any of them
  if (count != 1) return;
  _allKeys->AddKey(_curSel, dik);
  AddKey(dik);
}

DisplayConfigure::DisplayConfigure(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  // ReInit DI joysticks - user could connect new joystick
#ifdef _WIN32
  if (GJoystickDevices) 
  {
    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
    ParamFile cfg;
    ParseUserParams(cfg, &globals);

    GInput.ReInitDIJoysticks();
    GInput.LoadKeys(cfg); //reload all keys, including keys of not connected joysticks
    GInput.RemoveInaccessableKeys();
  }
  // ReInit TrackIR (possibly switch ON during gameplay)
  extern void InitTrackIR();
  InitTrackIR();
#endif

  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayConfigure");

  _oldMouseButtonsReversed = GInput.mouseButtonsReversed;
  _oldMouseSensitivityX = GInput.mouseSensitivityX;
  _oldMouseSensitivityY = GInput.mouseSensitivityY;
  _oldMouseFiltering = GInput._filterMouse;

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_CONFIG_BUTTONS));
  if (text)
  {
    if (GInput.mouseButtonsReversed) text->SetText(LocalizeString(IDS_RIGHT_BUTTON));
    else text->SetText(LocalizeString(IDS_LEFT_BUTTON));
  }

#if _VBS2
  if (!Glob.vbsAdminMode)
  {
    if (GetCtrl(IDC_OPTIONS_VIDEO))
      GetCtrl(IDC_OPTIONS_VIDEO)->EnableCtrl(false);
    if (GetCtrl(IDC_OPTIONS_AUDIO))
      GetCtrl(IDC_OPTIONS_AUDIO)->EnableCtrl(false);
    if (GetCtrl(IDC_OPTIONS_DIFFICULTY))
      GetCtrl(IDC_OPTIONS_DIFFICULTY)->EnableCtrl(false);
  }
#endif
}

void DisplayConfigure::Destroy()
{
  Display::Destroy();
  
#if !DISABLE_OPTIONS_CANCEL
  if (_exit != IDC_OK)
  {
    GInput.mouseSensitivityX = _oldMouseSensitivityX;
    GInput.mouseSensitivityY = _oldMouseSensitivityY;
    GInput.mouseButtonsReversed = _oldMouseButtonsReversed;
    GInput._filterMouse = _oldMouseFiltering;
    return;
  }
#endif

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    GInput.MergeJoysticksKeys();
    GInput.SaveKeys(cfg);
    SaveUserParams(cfg);
    GInput.RemoveInaccessableKeys();
  }
}

bool DisplayConfigure::OnKeyDown(int dikCode)
{
  return Display::OnKeyDown(dikCode);
}

void DisplayConfigure::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_CONFIG_YREVERSED:
    {
      if(curSel == 0) GInput.revMouse = true;
      else  GInput.revMouse = false;
    }
    break;
  }
  Display::OnLBSelChanged(ctrl,curSel);
}

void DisplayConfigure::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
}

Control *DisplayConfigure::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_CONFIG_XAXIS:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider)
      {
        slider->SetRange(0.2, 2.0);
        slider->SetSpeed(0.15, 0.02);
        slider->SetThumbPos(GInput.mouseSensitivityX);
      }
    }
    break;
  case IDC_CONFIG_YAXIS:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider)
      {
        slider->SetRange(0.2, 2.0);
        slider->SetSpeed(0.15, 0.02);
        slider->SetThumbPos(GInput.mouseSensitivityY);
      }
    }
    break;
  case IDC_CONFIG_MOUSE_FILTERING:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider)
      {
        //keep it to 0.0 - 1.0; if you need some changes, change conversion (now value = pos^2*100) 
        slider->SetRange(0.0, 1.0);
        slider->SetSpeed(0.05, 0.02);
        // slider <-> value conversion is not linear!!
        float pos = sqrtf(abs(GInput._filterMouse) * 0.01f);
        slider->SetThumbPos(pos);
      }
    }
    break;
  case IDC_CONFIG_YREVERSED:
    {
      if (type == CT_LISTBOX || type == CT_XLISTBOX)
      {
        CListBoxContainer *_mouseReversed  = GetListBoxContainer(ctrl);
        _mouseReversed->AddString(LocalizeString(IDS_CONFIG_YREVERSED));
        _mouseReversed->AddString(LocalizeString(IDS_CONFIG_YNORMAL));

        if(GInput.revMouse) _mouseReversed->SetCurSel(0);
        else _mouseReversed->SetCurSel(1);
      }
      break;
    }
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
  }
  return ctrl;
}

void DisplayConfigure::OnButtonClicked(int idc)
{
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_CONTROLS && idc != IDC_MAIN_TAB_OPTIONS)
    Exit(idc);
  // allow switch to alternate options screen
  if (idc >= IDC_OPTIONS_VIDEO && idc <= IDC_OPTIONS_CREDITS && idc != IDC_OPTIONS_CONFIGURE)
    Exit(idc);
#endif
  switch (idc)
  {
  case IDC_CONFIG_DEFAULT:
    {
      GInput.revMouse = false;
      CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_CONFIG_YREVERSED));
      if (list && list->Size()>1) list->SetCurSel(1);
    }
    {
      GInput.mouseButtonsReversed = GetSystemMetrics(SM_SWAPBUTTON)!=0;
      ITextContainer *text = GetTextContainer(GetCtrl(IDC_CONFIG_BUTTONS));
      if (text) text->SetText(LocalizeString(IDS_LEFT_BUTTON));
    }
    {
      GInput.mouseSensitivityX = 1.0;
      CSliderContainer *slider = GetSliderContainer(GetCtrl(IDC_CONFIG_XAXIS));
      if (slider) slider->SetThumbPos(1.0);
    }
    {
      GInput.mouseSensitivityY = 1.0;
      CSliderContainer *slider = GetSliderContainer(GetCtrl(IDC_CONFIG_YAXIS));
      if (slider) slider->SetThumbPos(1.0);
    }
    {
      GInput._filterMouse = GInput._defFilterMouse;
      CSliderContainer *slider = GetSliderContainer(GetCtrl(IDC_CONFIG_YAXIS));
      if (slider) slider->SetThumbPos(GInput._defFilterMouse);
    }
    break;
  case IDC_CONFIG_JOYSTICK:
    {
      CreateChild(new DisplayConfigureControllers(this, _enableSimulation));
    }
    break;
  case IDC_CONFIG_BUTTONS:
    {
      GInput.mouseButtonsReversed = !GInput.mouseButtonsReversed;
      ITextContainer *text = GetTextContainer(GetCtrl(IDC_CONFIG_BUTTONS));
      if (text)
      {
        if (GInput.mouseButtonsReversed)
          text->SetText(LocalizeString(IDS_RIGHT_BUTTON));
        else
          text->SetText(LocalizeString(IDS_LEFT_BUTTON));
      }
    }
    break;
  case IDC_CONFIG_ASSIGNMENTS:
    CreateChild(new DisplayConfigureAssignments(this, _enableSimulation));
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayConfigure::OnSliderPosChanged(IControl *ctrl, float pos)
{
  switch (ctrl->IDC())
  {
  case IDC_CONFIG_XAXIS:
    GInput.mouseSensitivityX = pos;
    break;
  case IDC_CONFIG_YAXIS:
    GInput.mouseSensitivityY = pos;
    break;
  case IDC_CONFIG_MOUSE_FILTERING:
    GInput._filterMouse = 100 * (pos*pos);
    break;
  case IDC_CONFIG_FLOATING_ZONE:
    GInput.floatingZoneArea = pos;
    break;
  default:
    Display::OnSliderPosChanged(ctrl, pos);
    break;
  }
}

void DisplayConfigure::OnChildDestroyed(int idd, int exit)
{
#ifdef _WIN32
  switch (idd)
  {
  case IDD_CONFIGURE_JOYSTICKS:
    if (GJoystickDevices) GJoystickDevices->SaveJoysticks();
    break;
  }
#endif
  Display::OnChildDestroyed(idd, exit);
}

DisplayConfigureAssignments::DisplayConfigureAssignments(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  _keys = NULL;
  Load("RscDisplayConfigureAssignments");

  if (_controlsPages) UpdateControlsPages();
  _currentActionGroup = -1;

  if (_keys) UpdateKeys();
}

static void SaveAssignments(const AutoArray<int> *keys)
{
  if (keys)
  {
    for (int i=0; i<UAN; i++)
    {
      GInput.userKeys[i].SetKeys(keys[i]);
      GInput.userKeys[i].Compact();
    }
  }

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    GInput.MergeJoysticksKeys();
    GInput.SaveKeys(cfg);
    SaveUserParams(cfg);
    GInput.RemoveInaccessableKeys();
  }
}

void DisplayConfigureAssignments::Destroy()
{
  Display::Destroy();
  
#if !DISABLE_OPTIONS_CANCEL
  if (_exit != IDC_OK)
    return;
#endif

  SaveAssignments(_keys->GetKeys());
}

void DisplayConfigureAssignments::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_CONFIG_KEYS:
    if (curSel >= 0 && !_keys->GetDoNotConfigureAction())
    {
      CreateChild(new DisplayConfigureAction(this, _keys, curSel, _enableSimulation));
    }
    break;
  case IDC_CONFIG_CONTROLS_PAGE:
    UpdateKeys();
    FocusCtrl(IDC_CONFIG_KEYS);
    break;
  }
  Display::OnLBSelChanged(ctrl,curSel);
}

void DisplayConfigureAssignments::ChangeConfigureActionChild(int action)
{
  CreateChild(new DisplayConfigureAction(this, _keys, action, _enableSimulation, true));
}

void DisplayConfigureAssignments::UpdateKeys()
{
  Assert(_keys);

  if (_currentActionGroup!=_controlsPages->GetCurSel())
  {
    CListBoxContainer *list = _keys;
    _keys->RemoveAll();
    _currentActionGroup = _controlsPages->GetCurSel();
    int n = GInput.actionGroups.GroupSize(_currentActionGroup);
    if (n)
    {
      const AutoArray<int> &actions = GInput.actionGroups.Group(_currentActionGroup);
      _keys->SetActionGroup(&actions);
      for (int i=0; i<n; i++)
      {
        list->AddString(LocalizeString(GInput.userActionDesc[actions[i]].desc));
        //_keys->SetKeys(actions[i], GInput.userKeys[actions[i]]);
      }
      list->SetCurSel(0, false);
    }
  }
}

Control *DisplayConfigureAssignments::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_CONFIG_KEYS:
    {
      CListBoxContainer *list = NULL;
      Control *ctrl = NULL;

      if (type == CT_LISTBOX)
      {
        C2DKeys *keys = new C2DKeys(this, idc, cls);
        _keys = keys;
        list = keys;
        ctrl = keys;
        ParamEntryPtr entry = cls.FindEntry("collisionColor");
        if (entry) keys->_collisionColor = GetPackedColor(*entry);
        else keys->_collisionColor = PackedColor(ListItemRed);
        entry = cls.FindEntry("disabledKeyColor");
        if (entry) keys->_disabledKeyColor = GetPackedColor(*entry);
        else keys->_disabledKeyColor = PackedColor(ListItemGray);

        entry = cls.FindEntry("mainCollumW");
        if (entry) keys->_mainCollumW = cls >>"mainCollumW";
        else keys->_mainCollumW = 0.4f;
        entry = cls.FindEntry("secndCollumW");
        if (entry) keys->_secndCollumW = cls >> "secndCollumW";
        else keys->_secndCollumW = 0.6f;
      }
      else return NULL;

      for (int i=0; i<UAN; i++)
      {
        list->AddString(LocalizeString(GInput.userActionDesc[i].desc));
        _keys->SetKeys(i, GInput.userKeys[i]);
      }
      list->SetCurSel(0, false);
      return ctrl;
    }
  }

  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_CONFIG_CONTROLS_PAGE:
    _controlsPages = GetListBoxContainer(ctrl);
    //load the controls pages definitions
    break;
  }
  return ctrl;
}

void DisplayConfigureAssignments::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CONFIG_KEY_PRESETS:
    CreateChild(new DisplayControlSchemes(this, _keys));
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayConfigureAssignments::UpdateControlsPages()
{
  Assert(_controlsPages);

  //RString current = GetCurrentPage();
  _controlsPages->RemoveAll();
  for (int i=0; i<GInput.actionGroups.Size(); i++)
  {
    int index = _controlsPages->AddString(GInput.actionGroups.GroupName(i));
    _controlsPages->SetData(index, GInput.actionGroups.GroupName(i));
  }
  _controlsPages->SetCurSel(0, false);
}

void DisplayConfigureAssignments::OnChildDestroyed(int idd, int exit)
{
#ifdef _WIN32
  int  nextAction = -1;
  switch (idd)
  {
  case IDD_CONFIGURE_ACTION:
    if (exit==IDC_CONFIGURE_ACTION_NEXT)
    {
      DisplayConfigureAction *caChild = dynamic_cast<DisplayConfigureAction *>(_child.GetRef());
      if (caChild)
      {
        nextAction = caChild->NextAction();
      }
    }
  }
#endif
  Display::OnChildDestroyed(idd, exit);
  if (nextAction>=0)
  {
    ChangeConfigureActionChild(nextAction);
  }
}

DisplayControlSchemes::DisplayControlSchemes(ControlsContainer *parent, C2DKeys *keys)
 : Display(parent), _outKeys(keys)
{
  Load("RscDisplayControlsScheme");
}

Control* DisplayControlSchemes::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  if (!ctrl)
    return NULL;

  switch (idc)
  {
    case IDC_CONFIG_DEFAULTCONTROLS_LIST:
      {
        // fill up the listbox with default profiles
        _profileList = GetListBoxContainer(ctrl);
        if (_profileList)
        {
          int defaultIndex = 0;
          KeyProfileList list;
          GInput.defaultProfiles.ListProfiles(list);
          for (int i=0;i<list.Size();++i)
          {
            KeyProfile *profile = list[i];
            if (!profile) continue;
            // add new item to the list
            int index = _profileList->AddString(profile->name);
            _profileList->SetValue(index, i);
            // remember index of the selected profile
            if (profile == GInput.defaultProfiles.GetCurrentProfile())
              defaultIndex = index;
          }
          // select currently selected profile
          _profileList->SetCurSel(defaultIndex);
        }    
      }
      break;
  }
  return ctrl;
}

void DisplayControlSchemes::Destroy()
{
  Display::Destroy();

#if !DISABLE_OPTIONS_CANCEL
  if (_exit == IDC_OK)
#endif
  {
    // when display is destroyed with OK idc, set selected profile as active
    int sel = _profileList->GetCurSel();
    if (sel>=0)
    {
      // set current profile
      int profileIndex = _profileList->GetValue(sel);
      GInput.defaultProfiles.SetProfile(profileIndex);
      // update key mapping
      for (int i=0; i<UAN; i++)
        _outKeys->SetKeys(i, GInput.defaultProfiles.GetCurrentProfileKeyList((UserAction)i));
    }
  }
}

DisplayConfigureControllers::DisplayConfigureControllers(ControlsContainer *parent, bool enableSimulation)
: Display(parent), _activeList(AListControllers)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayConfigureControllers");
  _joysticks->SetCurSel(0);
}

static RString GetControllerName(int ctrlIx)
{
#ifdef _WIN32
  switch (ctrlIx)
  {
    case DisplayConfigureControllers::CT_TrackIR: return LocalizeString(IDS_TRACK_IR);
    case DisplayConfigureControllers::CT_FreeTrack: return LocalizeString(IDS_FREETRACK);
  }
  int offset = GJoystickDevices->Get(ctrlIx)->GetOffset();
  const JoystickDevices::RecognizedJoystick *joy = GJoystickDevices->GetJoystickInfo(offset);
  if (joy) 
  {
    return joy->name;
  }
#endif
  return RString("");
}

void DisplayConfigureControllers::UpdateJoysticksList()
{
#ifdef _WIN32
  _joysticks->ClearStrings();
  for (int i=0; i<GInput._joysticksState.Size(); i++)
  {
    const JoystickDevices::RecognizedJoystick *joy = GJoystickDevices->GetJoystickInfo(GInput._joysticksState[i].offset);
    if (joy) 
    {
      if (!joy->isXInput || GInput.customScheme)
      {
        int ix = _joysticks->AddString(joy->name);
        _joysticks->SetValue(ix, i);
        if (joy->mode==JMDisabled) 
        {
          _joysticks->SetFtColor(ix, _joysticksDisabledColor);
          _joysticks->SetSelColor(ix, _joysticksDisabledColor);
        }
      }
    }
  }
#endif
#if defined _WIN32 && !defined _XBOX
  // Add TrackIR if active (installed)
  if (TrackIRDev && TrackIRDev->IsActive())
  {
    int ix = _joysticks->AddString(LocalizeString(IDS_TRACK_IR));
    _joysticks->SetValue(ix, CT_TrackIR);
    if (!TrackIRDev->IsEnabled()) 
    {
      _joysticks->SetFtColor(ix, _joysticksDisabledColor);
      _joysticks->SetSelColor(ix, _joysticksDisabledColor);
    }
  }
  if (FreeTrackDev && FreeTrackDev->IsActive())
    {
      int ix = _joysticks->AddString(LocalizeString(IDS_FREETRACK));
      _joysticks->SetValue(ix, CT_FreeTrack);
      if (!FreeTrackDev->IsEnabled()) 
      {
        _joysticks->SetFtColor(ix, _joysticksDisabledColor);
        _joysticks->SetSelColor(ix, _joysticksDisabledColor);
      }
    }
#endif
}

Control *DisplayConfigureControllers::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_CONTROLLER_LIST:
    {
      _joysticks = GetListBoxContainer(ctrl);
      ParamEntryPtr entry = cls.FindEntry("disabledCtrlColor");
      if (entry) _joysticksDisabledColor = GetPackedColor(*entry);
      else _joysticksDisabledColor = PackedColor(ListItemGray);
      UpdateJoysticksList();
    }
    break;
  case IDC_CONTROLLER_XINPUT_LIST:
    {
      ParamEntryPtr entry = cls.FindEntry("disabledCtrlColor");
      if (entry) _xInputDisabledColor = GetPackedColor(*entry);
      else _xInputDisabledColor = PackedColor(ListItemGray);
      _xInputControllers = GetListBoxContainer(ctrl);
      for (int i=0; i<GInput._joysticksState.Size(); i++)
      {
#ifdef _WIN32
        const JoystickDevices::RecognizedJoystick *joy = GJoystickDevices->GetJoystickInfo(GInput._joysticksState[i].offset);
        if (joy) 
        {
          if (joy->isXInput)
          {
            int ix = _xInputControllers->AddString(joy->name);
            _xInputControllers->SetValue(ix, i);
            if (GInput.customScheme)
            {
              _xInputControllers->SetFtColor(ix, _xInputDisabledColor);
              _xInputControllers->SetSelColor(ix, _xInputDisabledColor);
            }
          }
        }
#endif
      }
    }
    break;
  }
  return ctrl;
}

void DisplayConfigureControllers::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_CONTROLLER_LIST:
    if (curSel >= 0)
    {
      _xInputControllers->SetCurSel(-1);
      Display::OnLBSelChanged(ctrl,curSel);
      RefreshButtons();
      return;
    }
    break;
  case IDC_CONTROLLER_XINPUT_LIST:
    if (curSel >= 0)
    {
      _joysticks->SetCurSel(-1);
      Display::OnLBSelChanged(ctrl,curSel);
      RefreshButtons();
      return;
    }
    break;
  }
  // default
  Display::OnLBSelChanged(ctrl,curSel);
}

void DisplayConfigureControllers::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_PROFILE_CONTROLLER:
    if (exit == IDC_OK)
    {
      DisplayProfileController *disp = dynamic_cast<DisplayProfileController *>(_child.GetRef());
      Assert(disp);
      GInput.scheme = disp->GetScheme();
      GInput.ApplyScheme();
      SaveScheme();
    }
    break;
  }
  Display::OnChildDestroyed(idd, exit);
}

void DisplayConfigureControllers::RefreshButtons()
{
  int curSel = _xInputControllers->GetCurSel();
  IControl *ctrl = GetCtrl(IDC_CONTROLLER_CUSTOMIZE);
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_CONTROLLER_ENABLE));
  RString strEnable = LocalizeString(IDS_ENABLE_CONTROLLER);
  RString strDisable = LocalizeString(IDS_DISABLE_CONTROLLER);
  if (curSel>=0)
  { // XInput listBox is selected
    if (GInput.customScheme)
    {
      if (text) text->SetText(strEnable);
      if (ctrl) ctrl->EnableCtrl(false);
    }
    else
    {
      if (text) text->SetText(strDisable);
      if (ctrl) ctrl->EnableCtrl(false);
    }
  }
  else       
  { // DInput listBox is selected
    curSel = _joysticks->GetCurSel();
    if (curSel>=0)
    {
      int joyIx = _joysticks->GetValue(curSel); //index into GInput._joysticksState and GJoystickDevices
      if (joyIx==CT_TrackIR)
      {
        if (text) 
        {
          if ( TrackIRDev->IsActive() )
          {
          if (TrackIRDev->IsEnabled())
            text->SetText(strDisable);
          else
            text->SetText(strEnable);
        }
      }
      }
      else if (joyIx==CT_FreeTrack)
      {
        if (text) 
        {
          if ( FreeTrackDev->IsActive() )
          {
            if (FreeTrackDev->IsEnabled())
              text->SetText(strDisable);
      else
              text->SetText(strEnable);
          }
        }
      }
      else
      {
        int offset = GInput._joysticksState[joyIx].offset;
        const JoystickDevices::RecognizedJoystick* joy = GJoystickDevices->GetJoystickInfo(offset);
        if (joy)
        {
          switch (GInput._joysticksState[joyIx].mode)
          {
          case JMCustom:
            if (text) text->SetText(strDisable);
            break;
          case JMDisabled:
            if (text) text->SetText(strEnable);
            break;
          }
        }
      }
      if (ctrl) ctrl->EnableCtrl(true);
    }
  }
}

void DisplayConfigureControllers::UpdateJoyColor(int ix, bool val)
{
  if (val)
  {
    _joysticks->SetFtColor(ix, _joysticks->GetTextColor());
    _joysticks->SetSelColor(ix, _joysticks->GetActiveColor());
  }
  else
  {
    _joysticks->SetFtColor(ix, _joysticksDisabledColor);
    _joysticks->SetSelColor(ix, _joysticksDisabledColor);
  }
}

void DisplayConfigureControllers::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CONTROLLER_ENABLE:
    {
      int curSel = _joysticks->GetCurSel();
      if (curSel>=0)
      {
        int joyIx = _joysticks->GetValue(curSel); //index into GInput._joysticksState and GJoystickDevices
        if (joyIx==CT_TrackIR)
        {
          if ( TrackIRDev->IsActive() )
          {
          TrackIRDev->Enable(!TrackIRDev->IsEnabled());
          UpdateJoyColor(curSel,TrackIRDev->IsEnabled());
          RefreshButtons();
          } 
          break;
        }
        else if (joyIx==CT_FreeTrack)
        {
          if ( FreeTrackDev->IsActive() )
          {
            FreeTrackDev->Enable(!FreeTrackDev->IsEnabled());
            UpdateJoyColor(curSel,FreeTrackDev->IsEnabled());
            RefreshButtons();
          } 
          break;
        }

        int offset = GInput._joysticksState[joyIx].offset;
        const JoystickDevices::RecognizedJoystick* joy = GJoystickDevices->GetJoystickInfo(offset);
        if (joy)
        {
          switch (GInput._joysticksState[joyIx].mode)
          {
            case JMCustom: // disable
              GJoystickDevices->Get(joyIx)->Enable(false);
              UpdateJoyColor(curSel, false);
              break;
            case JMDisabled: // enable
              GJoystickDevices->Get(joyIx)->Enable(true);
              UpdateJoyColor(curSel, true);
              break;
          }
          RefreshButtons();
        }
      }
      else
      {
        curSel = _xInputControllers->GetCurSel();
        if (curSel>=0)
        {
          if (GInput.customScheme) GInput.SetCustomScheme(false); // enable XInput
          else GInput.SetCustomScheme(true);
          // the list of _joysticks has changed (XInput devices present or not)
          if (GInput.customScheme)
          {
            _xInputControllers->SetFtColor(curSel, _xInputDisabledColor);
            _xInputControllers->SetSelColor(curSel, _xInputDisabledColor);
          }
          else
          {
            _xInputControllers->SetFtColor(curSel, _xInputControllers->GetTextColor());
            _xInputControllers->SetSelColor(curSel, _xInputControllers->GetActiveColor());
          }
          UpdateJoysticksList();
          RefreshButtons();
        }
      }
      break;
    }
  case IDC_CONTROLLER_CUSTOMIZE:
    {
      if (_joysticks->GetCurSel()>=0)
      {
        CreateChild(new DisplayCustomizeController(this, _joysticks->GetValue(_joysticks->GetCurSel()), _enableSimulation));
      }
      else if (_xInputControllers->GetCurSel()>=0)
      {
        CreateChild(new DisplayProfileController(this, _enableSimulation, GInput.scheme));
      }
      break;
    }
  }
  Display::OnButtonClicked(idc);
}

static RString GetAxisName(int ctrlIx, int axisIx)
{
  const int AxisNamesIds[N_JOYSTICK_AXES] = 
  {
    IDS_INPUT_DEVICE_STICK_AXIS_X, 
    IDS_INPUT_DEVICE_STICK_AXIS_Y,
    IDS_INPUT_DEVICE_STICK_AXIS_Z,
    IDS_INPUT_DEVICE_STICK_ROT_X,
    IDS_INPUT_DEVICE_STICK_ROT_Y,
    IDS_INPUT_DEVICE_STICK_ROT_Z,
    IDS_INPUT_DEVICE_STICK_SLIDER_1,
    IDS_INPUT_DEVICE_STICK_SLIDER_2
  };
  if (ctrlIx==DisplayConfigureControllers::CT_TrackIR || ctrlIx==DisplayConfigureControllers::CT_FreeTrack)
    return GetKeyName(INPUT_DEVICE_TRACKIR+axisIx);
  else 
  {
#if 1
    // use axis name from driver axis enumeration
    const JoyAxisInfo *axisInfos = GJoystickDevices->Get(ctrlIx)->GetJoyAxesInfo();
    if ( axisInfos && axisInfos[axisIx].active && !axisInfos[axisIx].name.IsEmpty() )
    {
      return axisInfos[axisIx].name;
    }
    else
    {
      // use localized Axis name
      if (axisIx>=0 && axisIx<N_JOYSTICK_AXES)
        return LocalizeString(AxisNamesIds[axisIx]);
    }
#else
    // use localized Axis name
    if (axisIx>=0 && axisIx<N_JOYSTICK_AXES)
      return LocalizeString(AxisNamesIds[axisIx]);
#endif
  }
  return RString("");
}

static float GetAxisSensitivity(int ctrlIx, int axisIx)
{
  if (ctrlIx>=0 && ctrlIx<GInput._joysticksState.Size() && axisIx<N_JOYSTICK_AXES)
  {
    return GInput._joysticksState[ctrlIx].sensitivity[axisIx];
  }
  return 1.0f;
}

static float GetAxisDeadZone(int ctrlIx, int axisIx)
{
  if ( ctrlIx>=0 && ctrlIx<GInput._joysticksState.Size() && axisIx<GInput._joysticksState[ctrlIx].deadZone.Size() )
  {
    return GInput._joysticksState[ctrlIx].deadZone[axisIx];
  }
  return 0;
}

static int GetAxisSteepness(int ctrlIx, int axisIx, CListBoxContainer &ctrl)
{
  if ( ctrlIx>=0 && ctrlIx<GInput._joysticksState.Size() && axisIx<GInput._joysticksState[ctrlIx].steepness.Size() )
  {
    int bestIx = 0;
    float bestDiff = 1000.0f;
    float val = GInput._joysticksState[ctrlIx].steepness[axisIx];
    // find the closest value
    for (int i=0; i<ctrl.Size(); i++)
    {
      float diff = fabs(val - ctrl.GetValue(i)/100.0f);
      if (diff < bestDiff)
      {
        bestDiff = diff;
        bestIx = i;
      }
    }
    return bestIx;
  }
  return 0; // default, first item, 1.0
}

static int GetNumAxes(int ctrlIx)
{
  if (ctrlIx==DisplayConfigureControllers::CT_TrackIR || ctrlIx==DisplayConfigureControllers::CT_FreeTrack)
    return N_TRACKIR_AXES;
  else 
    return GJoystickDevices->Get(ctrlIx)->GetAxesCount();
}

DisplayCustomizeController::DisplayCustomizeController(ControlsContainer *parent, int controllerIx, bool enableSimulation)
: Display(parent), _controllerIx(controllerIx), _performActionOnOK(CDANone)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  _showAdvanced = false;
  _updateDeadZones = false;
  _dzNextUpdate = GlobalTickCount(); //asap
  Load("RscDisplayCustomizeController");
  _vSpacing = Pars >> "RscDisplayCustomizeController" >> "Slider" >> "vspacing";
  _activeAxisIcon = GlobLoadTexture(GetPictureName(Pars >> "RscDisplayCustomizeController" >> "axisActiveIcon"));
  if (controllerIx<0)
  { // TrackIR or FreeTrack, do hide the Show/Hide advanced button
    CStatic *ctrl = static_cast<CStatic *>(GetCtrl(IDC_CUSTOMIZE_CTRL_ADVANCED));
    if (ctrl) ctrl->ShowCtrl(false);
  }

  // key assignments (needed because of Joystick/Device mapping custom/scheme/map/unmap)
  for (int i=0; i<UAN; i++)
  {
    static_cast<AutoArray<int>&>(_allKeys[i]) = static_cast<AutoArray<int>&>(GInput.userKeys[i]);
  }

  // add controls
  IControl *ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_SENSITIVITIES);
  if (ctrl && ctrl->GetType() == CT_CONTROLS_GROUP)
  {
    CControlsGroup *grp = static_cast<CControlsGroup *>(ctrl);

    float top = 0.02;
 
    if (_controllerIx==DisplayConfigureControllers::CT_FreeTrack || _controllerIx==DisplayConfigureControllers::CT_TrackIR)
    {
      for (int i=0; i<N_TRACKIR_AXES; i++)
      {
        (void) AddControl(grp,i,i,top,GetKeyName(INPUT_DEVICE_TRACKIR+i));
      }
    }
    else
    { // Direct Input Joystick
      const JoyAxisInfo *axisInfos = GJoystickDevices->Get(_controllerIx)->GetJoyAxesInfo();
      if (axisInfos)
      {
        int ctrlIx = 0;
        for (int i=0; i<N_JOYSTICK_AXES; i++)
        {
          if (axisInfos[i].active)
          {
            (void) AddControl(grp,ctrlIx++,i,top,axisInfos[i].name);
          }
        }
      }
    }

    grp->UpdateScrollbars();
  }
  // update title
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_CUSTOMIZE_CTRL_TITLE));
  if (text) 
  {
    RString name = GetControllerName(_controllerIx);
    text->SetText(Format(LocalizeString(IDS_CUSTOMIZE_CONTROLLER_TITLE), cc_cast(name)));
  }
}

void DisplayCustomizeController::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
  //handle special input keys
  if (_controllerIx>=0 && _controllerIx<GInput._joysticksState.Size())
  {
    Input::JoystickState &joy = GInput._joysticksState[_controllerIx];
    if (joy.mode!=JMCustom) return;
    for (int i=0; i<_deadZoneCtrl.Size(); i++)
    {
      //stick axes can contain negative values, so check it
      IControl *ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_SENSITIVITIES);
      if (ctrl && ctrl->GetType() == CT_CONTROLS_GROUP)
      {
        CStatic *icon = static_cast<CStatic*>(ctrl->GetCtrl(IDC_CUSTOMIZE_CTRL_ACTIVE_AXIS_ICON+i));
        if (icon)
        {
          if ( fabs(joy.stickAxis[_axisIx[i]]) > joy.StickAxisThresholdHigh )
          { // highlight the axis (show icon)
            icon->ShowCtrl();
          }
          else icon->ShowCtrl(false);
        }
        if (_showAdvanced)
        {
          CStatic *axValue = static_cast<CStatic*>(ctrl->GetCtrl(IDC_CUSTOMIZE_CTRL_VALUE+i));
          if (axValue)
          {
            float sVal = 0;
            CXSlider *sld = dynamic_cast<CXSlider *>(_sensitivityCtrl[i].GetRef());
            if (sld)
            {
              sVal = GetSensitivity(sld);
            }
            float steepVal = 1.0f;
            CListBoxContainer *lb = dynamic_cast<CListBoxContainer *>(_steepnessCtrl[i].GetRef());
            if (lb)
            {
              steepVal = lb->GetValue(lb->GetCurSel()) / 100.0f;
            }
            float val = fabs(joy.stickAxis[_axisIx[i]]);
            GInput.ApplySensitivity(val, sVal, steepVal);
            int iVal = toInt(val*100);
            saturate(iVal, 0, 100);
            if (joy.stickAxis[_axisIx[i]]<0) iVal = -iVal;
            axValue->SetText(Format("%d", iVal));
          }
        }
      }
    }
    // update DeadZones from time to time only
    DWORD curTime = GlobalTickCount();
    if (curTime > _dzNextUpdate)
    {
      _dzNextUpdate = curTime + 100; //10 times per second
      if (_updateDeadZones)
      {
        _updateDeadZones = false;
        AutoArray<float> deadZone;
        int size = _deadZoneCtrl.Size();
        deadZone.Realloc(N_JOYSTICK_AXES); deadZone.Resize(N_JOYSTICK_AXES);
        for (int i=0; i<N_JOYSTICK_AXES; i++) deadZone[i]=0; //clear
        for (int i=0; i<size; i++)
        {
          CXSlider *sld = dynamic_cast<CXSlider *>(_deadZoneCtrl[i].GetRef());
          if (sld)
          {
            deadZone[_axisIx[i]] = GetDeadZone(sld);
          }
        }
        GJoystickDevices->ReloadDeadZone(_controllerIx, deadZone);
      }
    }
  }
}

Control *DisplayCustomizeController::AddControl(CControlsGroup *grp, int ctrlIx, int axisIx, float &top, RString name)
{
  if (_controllerIx==DisplayConfigureControllers::CT_TrackIR || _controllerIx==DisplayConfigureControllers::CT_FreeTrack) 
    return NULL; // no gamma for TrackIR (it has its own settings)
  // position of all resources relative to 0, top
  _axisIx[ctrlIx] = axisIx;
  CStatic *title = new CStatic(this, -1, Pars >> "RscDisplayCustomizeController" >> "Title");
  title->SetText(name);
  title->SetPos(title->X(), top + title->Y(), title->W(), title->H());
  grp->AddControl(title);

  float bottom = title->Y() + title->H();

  // select control by param description
  int idc = IDC_SENSITIVITY_SLIDER+ctrlIx;
  CXSlider *slider = new CXSlider(this, idc, Pars >> "RscDisplayCustomizeController" >> "Slider");
  float from = -1.0, to = 1.0;
  slider->SetRange(from, to);
  slider->SetSpeed(0.01 * (to - from), 0.1 * (to - from));
  float ctrlSens = GetAxisSensitivity(_controllerIx, axisIx);
  saturate(ctrlSens, from, to);
  slider->SetThumbPos(ctrlSens);
  slider->SetPos(slider->X(), top + slider->Y(), slider->W(), slider->H());
  grp->AddControl(slider);
  _sensitivityCtrl.Add(slider);
  slider->EnableCtrl();
  saturateMax(bottom, slider->Y() + slider->H());

  // DeadZone slider
  idc = IDC_DEADZONE_SLIDER+ctrlIx;
  CXSlider *dzslider = new CXSlider(this, idc, Pars >> "RscDisplayCustomizeController" >> "DeadZoneSlider");
  from = 0; to = 0.2f;  //max 20%
  dzslider->SetRange(from, to);
  dzslider->SetSpeed(0.01 * (to - from), 0.1 * (to - from));
  dzslider->SetThumbPos(GetAxisDeadZone(_controllerIx, axisIx));
  dzslider->SetPos(dzslider->X(), top + dzslider->Y(), dzslider->W(), dzslider->H());
  grp->AddControl(dzslider);
  _deadZoneCtrl.Add(dzslider);
  dzslider->EnableCtrl();

  // Active Axis Icon
  idc = IDC_CUSTOMIZE_CTRL_ACTIVE_AXIS_ICON+ctrlIx;
  CStatic *actAxis = new CStatic(this, idc, Pars >> "RscDisplayCustomizeController" >> "ActiveAxis");
  actAxis->SetTexture(_activeAxisIcon);
  actAxis->SetPos(actAxis->X(), top + actAxis->Y(), actAxis->W(), actAxis->H());
  actAxis->ShowCtrl(false);
  grp->AddControl(actAxis);

  // Steepness listbox
  idc = IDC_CUSTOMIZE_CTRL_STEEPNESS+ctrlIx;
  int type = Pars >> "RscDisplayCustomizeController" >> "Steepness" >> "Type";
  Control *steepness = NULL;
  switch (type)
  {
  case CT_LISTBOX: 
    steepness = new CListBox(this, idc, Pars >> "RscDisplayCustomizeController" >> "Steepness");
    break;
  case CT_COMBO:
    steepness = new CCombo(this, idc, Pars >> "RscDisplayCustomizeController" >> "Steepness");
    break;
  }
  if (steepness)
  {
    CListBoxContainer *steepList = dynamic_cast<CListBoxContainer *>(steepness);
    steepness->SetPos(steepness->X(), top + steepness->Y(), steepness->W(), steepness->H());
    steepness->ShowCtrl(_showAdvanced);
    ParamEntryVal values = Pars >> "RscDisplayCustomizeController" >> "Steepness" >> "values";
    if (values.IsArray())
    {
      for (int i=0; i<values.GetSize(); i++)
      {
        RString strVal = values[i];
        int index = steepList->AddString(strVal);
        float val = atof(strVal);
        saturate(val, 1.0f, 10.0f);
        steepList->SetValue(index, toInt(val*100));
      }
      _steepnessCtrl.Add(steepness);
      steepList->SetCurSel(GetAxisSteepness(_controllerIx, axisIx, *steepList));
      grp->AddControl(steepness);
    }
  }
  // joystick values
  idc = IDC_CUSTOMIZE_CTRL_VALUE+ctrlIx;
  CStatic *axValue = new CStatic(this, idc, Pars >> "RscDisplayCustomizeController" >> "AxisValue");
  axValue->SetText("0");
  axValue->SetPos(axValue->X(), top + axValue->Y(), axValue->W(), axValue->H());
  axValue->ShowCtrl(_showAdvanced);
  grp->AddControl(axValue);

  top = bottom + _vSpacing; // keep space between controls

  return slider;
}

float DisplayCustomizeController::GetSensitivity(CXSlider *slider)
{
  float pos = slider->GetThumbPos();
  saturate(pos, -1, 1);
  return pos;
}

float DisplayCustomizeController::GetDeadZone(CXSlider *slider)
{
  float pos = slider->GetThumbPos();
  saturate(pos, 0, 0.2f);
  return pos;
}

void DisplayCustomizeController::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CUSTOMIZE_CTRL_UNMAP:
    {
      _performActionOnOK = CDAClearKeys;
      // disable/enable buttons
      IControl *ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_UNMAP);
      if (ctrl) ctrl->EnableCtrl(false);
      ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_DEFAULT);
      if (ctrl) ctrl->EnableCtrl();
      FocusCtrl(IDC_OK);
    }
    break;
  case IDC_CUSTOMIZE_CTRL_DEFAULT:
    {
      // disable/enable buttons
      IControl *ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_DEFAULT);
      if (ctrl) ctrl->EnableCtrl(false);
      // set defaults to sliders
      SetDefaultSensitivity();
      SetDefaultDeadZone();
      SetDefaultSteepness();
      FocusCtrl(IDC_OK);
    }
    break;
  case IDC_CUSTOMIZE_CTRL_DEFAULT_BINDING:
    {
      // disable/enable buttons
      IControl *ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_UNMAP);
      if (ctrl) ctrl->EnableCtrl();

      CreateChild(new DisplayJoystickSchemes(this, _allKeys, _enableSimulation, GetControllerName(_controllerIx)));
      FocusCtrl(IDC_OK);
    }
    break;
  case IDC_CUSTOMIZE_CTRL_ADVANCED:
  {
    _showAdvanced = !_showAdvanced;
    CStatic *ctrl = static_cast<CStatic *>(GetCtrl(IDC_CUSTOMIZE_CTRL_ADVANCED));
    if (_showAdvanced)
      ctrl->SetText(LocalizeString(IDS_DISP_HIDE));
    else
      ctrl->SetText(LocalizeString(IDS_DISP_SHOW));
    for (int i=0; i<_steepnessCtrl.Size(); i++)
    {
      _steepnessCtrl[i]->ShowCtrl(_showAdvanced);
      // show/hide joy axis values
      IControl *ctrl = GetCtrl(IDC_CUSTOMIZE_CTRL_SENSITIVITIES);
      if (ctrl && ctrl->GetType() == CT_CONTROLS_GROUP)
      {
        CControlsGroup *grp = static_cast<CControlsGroup *>(ctrl);
        IControl *ctrlValues = grp->GetCtrl(IDC_CUSTOMIZE_CTRL_VALUE+i);
        if (ctrlValues) ctrlValues->ShowCtrl(_showAdvanced);
      }
    }
  }
  break;
  case IDC_OK:
    {
      // perform possible action
      switch (_performActionOnOK)
      {
      case CDAClearKeys:
        ClearControllerKeys();
        break;
      case CDADefaultKeys:
        SetDefaultKeys();
        break;
      }
      if ( _controllerIx!=DisplayConfigureControllers::CT_TrackIR && _controllerIx!=DisplayConfigureControllers::CT_FreeTrack )
      {
        // save the sliders state
        // sensitivity
        AutoArray<float> sensitivity;
        sensitivity.Realloc(N_JOYSTICK_AXES*2); sensitivity.Resize(N_JOYSTICK_AXES*2);
        for (int i=0; i<N_JOYSTICK_AXES*2; i++) sensitivity[i]=0.0f; //clear
        for (int i=0; i<_sensitivityCtrl.Size(); i++)
        {
          CXSlider *sld = dynamic_cast<CXSlider *>(_sensitivityCtrl[i].GetRef());
          if (sld)
          {
            sensitivity[_axisIx[i]] = sensitivity[_axisIx[i]+N_JOYSTICK_AXES] = GetSensitivity(sld);
          }
        }
        GJoystickDevices->SetSensitivity(_controllerIx, sensitivity);
        // dead zone
        AutoArray<float> deadZone;
        int size = _deadZoneCtrl.Size();
        deadZone.Realloc(N_JOYSTICK_AXES); deadZone.Resize(N_JOYSTICK_AXES);
        for (int i=0; i<N_JOYSTICK_AXES; i++) deadZone[i]=0; //clear
        for (int i=0; i<size; i++)
        {
          CXSlider *sld = dynamic_cast<CXSlider *>(_deadZoneCtrl[i].GetRef());
          if (sld)
          {
            deadZone[_axisIx[i]] = GetDeadZone(sld);
          }
        }
        GJoystickDevices->SetDeadZone(_controllerIx, deadZone);
        // Steepness
        AutoArray<float> steepness;
        size = _steepnessCtrl.Size();
        steepness.Realloc(N_JOYSTICK_AXES); steepness.Resize(N_JOYSTICK_AXES);
        for (int i=0; i<N_JOYSTICK_AXES; i++) steepness[i]=1.0f; //clear
        for (int i=0; i<size; i++)
        {
          CListBoxContainer *list = dynamic_cast<CListBoxContainer*>(_steepnessCtrl[i].GetRef());
          if (list)
          {
            steepness[_axisIx[i]] = list->GetValue(list->GetCurSel()) / 100.0f;
          }
        }
        GJoystickDevices->SetSteepness(_controllerIx, steepness);
      }
      SaveAssignments(_allKeys);
    }
    break;
  case IDC_CANCEL:
    {
      if (_controllerIx!=DisplayConfigureControllers::CT_FreeTrack && _controllerIx!=DisplayConfigureControllers::CT_TrackIR)
      {
        AutoArray<float> deadZone;
        const Ref<IJoystick> joy = GJoystickDevices->Get(_controllerIx);
        int offset = joy->GetOffset();
        for (int i=0; GInput._joysticksState.Size(); i++)
        {
          if (GInput._joysticksState[i].offset == offset)
          { // known
            joy->SetDeadZone(GInput._joysticksState[i].deadZone);
            break;
          }
        }
      }
    }
    break;
  }
  Display::OnButtonClicked(idc);
}

void DisplayCustomizeController::OnLBSelChanged(IControl *ctrl, int curSel)
{
  Display::OnLBSelChanged(ctrl, curSel);
  OnChanged(ctrl);
}

void DisplayCustomizeController::OnChanged(IControl *ctrl)
{
  int idc = ctrl->IDC();
  if (idc>=IDC_DEADZONE_SLIDER && idc<IDC_DEADZONE_SLIDER+_deadZoneCtrl.Size())
  {
    _updateDeadZones = true;
  }
  IControl *ctrlDefault = GetCtrl(IDC_CUSTOMIZE_CTRL_DEFAULT);
  if (ctrlDefault) ctrlDefault->EnableCtrl();
}

void DisplayCustomizeController::ClearControllerKeys()
{
  int offset = 0;
  if ( _controllerIx!=DisplayConfigureControllers::CT_TrackIR && _controllerIx!=DisplayConfigureControllers::CT_FreeTrack )
  {
    if (_controllerIx>=0) offset = GInput._joysticksState[_controllerIx].offset;
    else return;
  }
  for (int i=0; i<UAN; i++)
  {
    FindArray<int> &keys = _allKeys[i];
    for (int j=0; j<keys.Size(); j++)
    {
      if ( _controllerIx==DisplayConfigureControllers::CT_TrackIR || _controllerIx==DisplayConfigureControllers::CT_FreeTrack )
      {
        if ((keys[j] & INPUT_DEVICE_MASK) == INPUT_DEVICE_TRACKIR)
        {
          keys.DeleteAt(j);
          j--; //process again
        }
      }
      else switch (keys[j] & INPUT_DEVICE_MASK)
      {
      case INPUT_DEVICE_STICK:
      case INPUT_DEVICE_STICK_AXIS:
      case INPUT_DEVICE_STICK_POV:
        int keyOffset = ((keys[j] - (keys[j] & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep) * JoystickDevices::OffsetStep;
        if ( keyOffset==offset )
        {
          keys.DeleteAt(j);
          j--; //process again
        }
        break;
      }
    }
  }
}

void DisplayCustomizeController::SetDefaultSensitivity()
{
  for (int i=0; i<_sensitivityCtrl.Size(); i++)
  {
    CXSlider *sld = dynamic_cast<CXSlider *>(_sensitivityCtrl[i].GetRef());
    if (sld)
    {
      sld->SetThumbPos(GInput.joystickSensitivity[_axisIx[i]]);
    }
  }
}

void DisplayCustomizeController::SetDefaultDeadZone()
{
  int count = _deadZoneCtrl.Size();
  for (int i=0; i<count; i++)
  {
    CXSlider *sld = dynamic_cast<CXSlider *>(_deadZoneCtrl[i].GetRef());
    if (sld)
    {
      sld->SetThumbPos( JoystickDevices::DeadZoneDefaults[_axisIx[i]]);
    }
  }
}

void DisplayCustomizeController::SetDefaultSteepness()
{
  int count = intMin(_steepnessCtrl.Size(), N_JOYSTICK_AXES);
  for (int i=0; i<count; i++)
  {
    CListBoxContainer *lb = dynamic_cast<CListBoxContainer *>(_steepnessCtrl[i].GetRef());
    if (lb)
    {
      lb->SetCurSel(0, false);
    }
  }
}

void DisplayCustomizeController::SetDefaultKeys()
{
  int offset = 0;
  if ( _controllerIx!=DisplayConfigureControllers::CT_TrackIR && _controllerIx!=DisplayConfigureControllers::CT_FreeTrack )
  {
    if (_controllerIx>=0) offset = GInput._joysticksState[_controllerIx].offset;
    else return;
  }
  ClearControllerKeys();

  for (int i=0; i<UAN; i++)
  {
    FindArray<int> &keys = _allKeys[i];
    const KeyList &dKeys = GInput.defaultProfiles.GetCurrentProfileKeyList((UserAction)i);
    for (int j=0; j<dKeys.Size(); j++)
    {
      if (_controllerIx==DisplayConfigureControllers::CT_TrackIR || _controllerIx==DisplayConfigureControllers::CT_FreeTrack)
      {
        if ((dKeys[j] & INPUT_DEVICE_MASK) == INPUT_DEVICE_TRACKIR)
        {
          keys.AddUnique(dKeys[j]);
        }
      }
      else
      {
        switch (dKeys[j] & INPUT_DEVICE_MASK)
        {
        case INPUT_DEVICE_STICK:
        case INPUT_DEVICE_STICK_AXIS:
        case INPUT_DEVICE_STICK_POV:
          int joyNum = ((dKeys[j] - (dKeys[j] & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep);
          if (!joyNum) { // default joystick key mapping
            keys.AddUnique(dKeys[j] + offset); // convert default key to specific joystick
          }
          break;
        }
      }
    }
  }
}

void DisplayCustomizeController::SetKeysByScheme( const JoystickKeysScheme &scheme )
{
  int offset = 0;
  if ( _controllerIx!=DisplayConfigureControllers::CT_TrackIR && _controllerIx!=DisplayConfigureControllers::CT_FreeTrack )
  {
    if (_controllerIx>=0) offset = GInput._joysticksState[_controllerIx].offset;
    else return;
  }
  ClearControllerKeys();

  for (int i=0; i<UAN; i++)
  {
    FindArray<int> &keys = _allKeys[i];
    const KeyList &dKeys = scheme.userKeys[i];
    for (int j=0; j<dKeys.Size(); j++)
    {
      if (_controllerIx==DisplayConfigureControllers::CT_TrackIR || _controllerIx==DisplayConfigureControllers::CT_FreeTrack)
      {
        if ((dKeys[j] & INPUT_DEVICE_MASK) == INPUT_DEVICE_TRACKIR)
        {
          keys.Add(dKeys[j]);
        }
      }
      else
      {
        switch (dKeys[j] & INPUT_DEVICE_MASK)
        {
        case INPUT_DEVICE_STICK:
        case INPUT_DEVICE_STICK_AXIS:
        case INPUT_DEVICE_STICK_POV:
          int joyNum = ((dKeys[j] - (dKeys[j] & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep);
          if (!joyNum) { // default joystick key mapping
            keys.Add(dKeys[j] + offset); // convert default key to specific joystick
          }
          break;
        }
      }
    }
  }
}

void DisplayCustomizeController::OnChildDestroyed( int idd, int exit )
{
  switch (idd)
  {
  case IDD_JOYSTICK_SCHEMES:
    if (exit==IDC_OK)
    {
      _performActionOnOK = CDANone; // Keys already mapped to scheme
    }
    break;
  }
  Display::OnChildDestroyed(idd, exit);
}

#if _ENABLE_EDITOR

// Select island game display
Control *DisplaySelectIsland::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
    case IDC_SELECT_ISLAND:
    {
      CListBoxContainer *lbox = GetListBoxContainer(ctrl);
      int sel = 0;
      int m = (Pars >> "CfgWorldList").GetEntryCount();
      for (int j=0; j<m; j++)
      {
        ParamEntryVal entry = (Pars >> "CfgWorldList").GetEntry(j);
        if (!entry.IsClass()) continue;
        RString name = entry.GetName();
#if _FORCE_DEMO_ISLAND
        RString demo = Pars >> "CfgWorlds" >> "demoWorld";
        if (stricmp(name, demo) != 0) continue;
#endif
        // ADDED - check if wrp file exists
        RString fullname = GetWorldName(name);
        if (!QFBankQueryFunctions::FileExists(fullname)) continue;
    
        int index = lbox->AddString
        (
          Pars >> "CfgWorlds" >> name >> "description"
        );
        lbox->SetData(index, name);
        if (stricmp(name, Glob.header.worldname) == 0)
          sel = index;
        RString textureName = Pars >> "CfgWorlds" >> name >> "icon";
        RString fullName = FindPicture(textureName);
        fullName.Lower();
        Ref<Texture> texture = GlobLoadTextureUI(fullName);
        lbox->SetTexture(index, texture);
      }
      lbox->SetCurSel(sel);
    }
    break;
  }
  return ctrl;
}

void DisplaySelectIsland::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_SELECT_ISLAND)
  {
    OnButtonClicked(IDC_OK);
  }
  else
    Display::OnLBDblClick(idc, curSel);
}

void DisplaySelectIsland::OnButtonClicked(int idc)
{
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_EDITOR)
    Exit(idc);
#endif
  switch (idc)
  {
//  case IDC_OK:
  case IDC_CANCEL:
    {
      ControlObjectContainerAnim *ctrl =
        dynamic_cast<ControlObjectContainerAnim *>(GetCtrl(IDC_SELECT_ISLAND_NOTEBOOK));
      if (ctrl)
      {
        _exitWhenClose = idc;
        ctrl->Close();
      }
      else Exit(idc);
    }
    break;
#if _ENABLE_EDITOR
  case IDC_SELECT_ISLAND_EDITOR:
    // launch mission editor 2
    Exit(IDC_SELECT_ISLAND_EDITOR);
    break;
#endif //_ENABLE_EDITOR
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplaySelectIsland::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
}

void DisplaySelectIsland::OnCtrlClosed(int idc)
{
  if (idc == IDC_SELECT_ISLAND_NOTEBOOK)
    Exit(_exitWhenClose);
  else
    Display::OnCtrlClosed(idc);
}

#endif // _ENABLE_EDITOR

// Custom arcade game display
Control *DisplayCustomArcade::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayCustomArcade::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
#if _ENABLE_CHEATS && !defined _XBOX
  if (GInput.GetCheat1ToDo(DIK_NUMPAD1))
  {
    Exit(IDC_CUST_EDIT_2);
  }
#endif
}

void DisplayCustomArcade::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_CUST_PLAY:
      {
        // Return to previous game is not available now
        CurrentTemplate = new DummyMission;
        if (GWorld->UI()) GWorld->UI()->Init();

        CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
        if (!tree) break;
        CTreeItem *item = tree->GetSelected();
        Assert(item);
        Assert(item->level > 0);

        CurrentMission = "";
        Glob.config.difficulty = Glob.config.diffDefault;
        
        // mission name and world
        const char *start = item->data;
        const char *name = strrchr(start, '\\');
        if (name) name++;
        else name = start;

        const char *ptr = strrchr(name, '.');
        if (!ptr) break;

        RString mission = RString(name, ptr - name);
        RString world = RString(ptr + 1);
        RString path = RString(start, name - start);

        RString dir;
        RString subdir;
        subdir = path;
        SetBaseDirectory(false, "");

        SetMission(world, mission, subdir);
        GStats.ClearAll();
        if (!LaunchIntro(this)) OnIntroFinished(this, IDC_CANCEL);
      }
      break;
    case IDC_CUST_EDIT:
      {
        CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
        if (!tree) break;
        CTreeItem *item = tree->GetSelected();
        if (!item) break;
        if (item->value == 0)
        {
          // create folder
        }
        else
          Exit(idc);
      }
      break;
    case IDC_CUST_DELETE:
      {
        CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
        if (!tree) break;
        CTreeItem *item = tree->GetSelected();
        if (item && item->value != 1 && item->children.Size() == 0)
        {
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          CreateMsgBox
          (
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            LocalizeString(IDS_SURE),
            IDD_MSG_DELETEGAME, false, &buttonOK, &buttonCancel
          );
        }
      }
      break;
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

void DisplayCustomArcade::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
    case IDD_MSG_DELETEGAME:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
        if (!tree) break;
        CTreeItem *item = tree->GetSelected();
        if (!item) break;
        if (item->children.Size() > 0 || item->value == 1) break;
        DeleteDirectoryStructure(item->data);
        InsertGames();
      }
      break;
    // mission logic
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
      OnMissionFinished(this, exit, false);
      break;
    case IDD_OUTRO:
      Display::OnChildDestroyed(idd, exit);
      OnOutroFinished(this, exit);
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

bool DisplayCustomArcade::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

  if (_exit == IDC_CUST_PLAY)
  {
    CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
    if (!tree) return false;
    CTreeItem *item = tree->GetSelected();
    if (!item) return false;
    return item->value == 2;
  }
  else if (_exit == IDC_CUST_EDIT)
  {
    CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
    if (!tree) return false;
    CTreeItem *item = tree->GetSelected();
    if (!item) return false;
    return item->value == 1 || item->value == 2;
  }
  else
    return true;
}

void DisplayCustomArcade::ShowButtons()
{
  CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
  IControl *play = GetCtrl(IDC_CUST_PLAY);
  CButton *edit = dynamic_cast<CButton *>(GetCtrl(IDC_CUST_EDIT));
  IControl *del = GetCtrl(IDC_CUST_DELETE);

  CTreeItem *item = tree ? tree->GetSelected() : NULL;
  if (!item)
  {
    if (play) play->ShowCtrl(false);
    if (edit) edit->ShowCtrl(false);
    if (del) del->ShowCtrl(false);
    return;
  }
  if (play) play->ShowCtrl(item->value == 2);
  if (del) del->ShowCtrl(item->children.Size() == 0 && item->value != 1);
  if (edit)
  {
#if _ENABLE_EDITOR
    edit->ShowCtrl(true);
    if (item->value == 2)
      edit->SetText(LocalizeString(IDS_CUST_EDIT));
    else if (item->value == 1)
      edit->SetText(LocalizeString(IDS_CUST_NEW));
    else
      edit->SetText(LocalizeString(IDS_CUST_NEW_DIR));
#else
    edit->ShowCtrl(false);
#endif
  }
}

void DisplayCustomArcade::OnTreeSelChanged(IControl *ctrl)
{
  if (ctrl->IDC() == IDC_CUST_GAME)
  {
    ShowButtons();
  }
  Display::OnTreeSelChanged(ctrl);
}

struct AddFileContext
{
  CTreeItem *root;
  CTreeItem *selected;
  RString text;
};

static bool AddFile(const FileItem &file, AddFileContext &ctx)
{
  PROFILE_SCOPE_EX(meAdF,file);
  // remove trailing '\'
  int n = file.path.GetLength();
  DoAssert(n > 0 && file.path[n - 1] == '\\');
  RString path(file.path, n - 1);

  // world and mission name
  const char *ptr = strrchr(path, '\\');
  if (!ptr) return false;  // avoid missions on root
  RString name(ptr + 1);
  RString dir(path, ptr + 1 - path);
  ptr = strrchr(name, '.');
  if (!ptr) return false; // bad directory name
  RString world(ptr + 1);
  RString mission(name, ptr - name);

  ParamEntryVal worldList = Pars >> "CfgWorldList";
  ConstParamEntryPtr entry = worldList.FindEntry(world);
  if (!entry || !entry->IsClass()) return false; // unknown island

#if _FORCE_DEMO_ISLAND
  RString demo = Pars >> "CfgWorlds" >> "demoWorld";
  if (stricmp(world, demo) != 0) return false;
#endif

  // parse path
  CTreeItem *item = ctx.root;
  const char *start = dir;
  ptr = NULL;
  while (ptr = strchr(start, '\\'))
  {
    RString name(start, ptr - start);
    CTreeItem *child = NULL;
    for (int i=0; i<item->children.Size(); i++)
      if (stricmp(item->children[i]->text, name) == 0)
      {
        child = item->children[i];
        break;
      }
    if (!child)
    {
      child = item->AddChild();
      child->data = RString(dir, ptr - dir);
      child->text = name;
      child->value = 0; // directory
    }

    start = ptr + 1;
    item = child;
  }

  // check childs for worlds
  CTreeItem *child = NULL;
  for (int i=0; i<item->children.Size(); i++)
  {
    if (item->children[i]->value == 1 && stricmp(item->children[i]->data, world) == 0)
    {
      child = item->children[i];
      break;
    }
  }

  // add worlds
  if (!child)
  {
    for (int i=0; i<worldList.GetEntryCount(); i++)
    {
      ParamEntryVal entry = worldList.GetEntry(i);
      if (!entry.IsClass()) continue;
      
      RString worldName = entry.GetName();
      CTreeItem *newChild = item->AddChild();
      newChild->data = worldName;
      newChild->text = Pars >> "CfgWorlds" >> worldName >> "description";
      newChild->value = 1; // world

      if (!child && stricmp(worldName, world) == 0) child = newChild;
    }
  }
  Assert(child);
  item = child;
  
  // add mission
  child = item->AddChild();
  child->text = mission;
  child->value = 2; // mission
  child->data = path;

  if (stricmp(path, ctx.text) == 0)
    ctx.selected = child;

  return false; // continue with enumeration
}

void SortRecursive(CTreeItem *item)
{
  item->SortChildren();
  for (int i=0; i<item->children.Size(); i++)
    if (item->children[i]) SortRecursive(item->children[i]);
}

void DisplayCustomArcade::InsertGames()
{
  CTree *tree = dynamic_cast<CTree *>(GetCtrl(IDC_CUST_GAME));
  if (!tree) return;

  CTreeItem *selected = tree->GetSelected();
  RString text;
  if (selected)
    text = selected->data;
  else
  {
    RString dir = GetMissionDirectory();
    int n = dir.GetLength();
    if (n > 0 && dir[n - 1] == '\\')
      text = RString(dir, n - 1);
  }

  tree->RemoveAll();
  
  AddFileContext ctx;
  ctx.root = tree->GetRoot();
  ctx.text = text;
  ctx.selected = NULL;
  ctx.root->text = "Root";

  GDebugger.PauseCheckingAlive();
  ForMaskedFileR("", "mission.sqm", AddFile, ctx);
  SortRecursive(tree->GetRoot());
  GDebugger.ResumeCheckingAlive();

  selected = ctx.selected;
  if (selected)
  {
    tree->SetSelected(selected);
    while (selected)
    {
      tree->Expand(selected, true);
      selected = selected->parent;
    }
  }
}
#endif //!defined(_XBOX) && defined(_WIN32)

#if _ENABLE_MP
static ConnectResult JoinSession(ControlsContainer *disp, SessionType sessionType, const SessionInfo &session, bool privateSlot, int playerRating)
{
  if (!CheckDiskSpace(disp, "U:\\", CHECK_SPACE_MISSION)) return CROK;

#if _XBOX_SECURE
  ConnectResult result = GetNetworkManager().JoinSession(sessionType, session.addr, session.kid, session.key, session.port, privateSlot, playerRating);
#else
  ConnectResult result = GetNetworkManager().JoinSession(sessionType, session.guid, "");
#endif

  switch (result)
  {
  case CROK:
    // init game info
    SetBaseDirectory(false, "");
    CurrentMission = "";
    Glob.config.difficulty = Glob.config.diffDefault;

    CurrentTemplate = new DummyMission;
    if (GWorld && GWorld->UI()) GWorld->UI()->Init();

    disp->CreateChild(new DisplayClient(disp));
#if _ENABLE_STEAM_MATCHMAKING
    // Add to History list
    SteamMatchmaking()->AddFavoriteGame(SteamUtils()->GetAppID(), session.smIP, session.smPort, session.smQueryPort, k_unFavoriteFlagHistory, time(NULL));
#endif
    break;
  case CRPassword:
    disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
    break;
  case CRVersion:
    disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
    break;
  case CRError:
    // disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
    disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    break;
  case CRSessionFull:
    // disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
    disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_OPENINGS));
    break;
  }
  return result;
}
#endif

#if _XBOX_SECURE && _ENABLE_MP
DisplayTestNetworkConditions::DisplayTestNetworkConditions(ControlsContainer *parent, XONLINE_FRIEND &f)
  : Display(parent)
{
  Load("RscDisplayInteruptReceiving");
  _idd = IDD_NETWORK_CONDITIONS;

  if (_text) _text->SetText(LocalizeString(IDS_XBOX_TESTING_NETWORK_CONDITIONS));

  _timeout = GlobalTickCount() + 60000;

#if _XBOX_VER >= 200
  // check for the buffer size
  int userIndex = GSaveSystem.GetUserIndex();
  DWORD size = 0;
  DWORD result = XSessionSearchByID(f.sessionID, userIndex, &size, NULL, NULL);
  if (result != ERROR_SUCCESS && result != ERROR_INSUFFICIENT_BUFFER)
  {
    RptF("Same title join: XSessionSearchByID failed with error 0x%x", result);
    Exit(IDC_AUTOCANCEL);
  }
  else
  {
    // allocate the buffer
    _buffer.Init(size);
    // start the operation
    memset(&_operation, 0, sizeof(_operation));
    result = XSessionSearchByID(f.sessionID, userIndex, &size, NULL, &_operation);
    if (result != ERROR_SUCCESS && result != ERROR_INSUFFICIENT_BUFFER)
    {
      RptF("Same title join: XSessionSearchByID failed with error 0x%x", result);
      Exit(IDC_AUTOCANCEL);
    }
  }
#else
  _task = NULL;
  HRESULT result = XOnlineMatchSessionFindFromID(f.sessionID, NULL, &_task);
  if (FAILED(result))
  {
    RptF("Same title join: XOnlineMatchSessionFindFromID failed with error 0x%x", result);
    _task = NULL;
    Exit(IDC_AUTOCANCEL);
  }
#endif
  _qosResults = NULL;
}

DisplayTestNetworkConditions::~DisplayTestNetworkConditions()
{
#if _XBOX_VER >= 200
  if (!XHasOverlappedIoCompleted(&_operation)) XCancelOverlapped(&_operation);
#else
  if (_task)
  {
    XOnlineTaskClose(_task);
    _task = NULL;
  }
#endif
  if (_qosResults)
  {
    XNetQosRelease(_qosResults);
    _qosResults = NULL;
  }
}

Control *DisplayTestNetworkConditions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_RECEIVING_TEXT:
    _text = GetTextContainer(ctrl);
    break;
  case IDC_RECEIVING_TIME:
    if (type == CT_ANIMATED_TEXTURE)
      _time = static_cast<CAnimatedTexture *>(ctrl);
    break;
  }
  return ctrl;
}

void DisplayTestNetworkConditions::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);

  DWORD time = GlobalTickCount();

  if (time >= _timeout)
  {
    // timed out
    Exit(IDC_AUTOCANCEL);
    return;
  }

  if (_time)
  {
    float factor = fastFmod(time * (1.0f / 3000), 1);
    _time->SetPhase(factor);
  }

  if (_qosResults)
  {
    Assert(_qosResults->cxnqos == 1);
    const XNQOSINFO &info = _qosResults->axnqosinfo[0];
    if ((info.bFlags & XNET_XNQOSINFO_COMPLETE) && info.cProbesRecv > 0)
    {
      // completed
      DWORD upStream = info.dwUpBitsPerSec;
      DWORD downStream = info.dwDnBitsPerSec;
      WORD latency = info.wRttMedInMsecs;
      if (upStream >= 64000 && downStream >= 64000 && latency <= 200)
        Exit(IDC_OK);
      else
        Exit(IDC_CANCEL);
    }
  }
  else
  {
#if _XBOX_VER >= 200
    if (XHasOverlappedIoCompleted(&_operation))
    {
      DWORD result = XGetOverlappedExtendedError(&_operation);
      if (FAILED(result))
      {
        RptF("Same title join: XSessionSearchByID failed with error 0x%x", result);
        Exit(IDC_AUTOCANCEL);
        return;
      }
      // session found
      XSESSION_SEARCHRESULT_HEADER *header = reinterpret_cast<XSESSION_SEARCHRESULT_HEADER *>(_buffer.Data());
      Assert(header->dwSearchResults == 1);
      static const int qosProbes = 8;
      static const int qosBps = 64000; // client is looking for the session, no need to limit
      const XNADDR *xna[] = {&header->pResults[0].info.hostAddress};
      const XNKID *xnkid[] = {&header->pResults[0].info.sessionID};
      const XNKEY *xnkey[] = {&header->pResults[0].info.keyExchangeKey};
      result = XNetQosLookup(
        1, xna, xnkid, xnkey, // Xbox hosts
        0, NULL, NULL, // Security gateways
        qosProbes, qosBps, 0, NULL, &_qosResults);
      if (result != ERROR_SUCCESS)
      {
        RptF("Qos lookup failed - error 0x%x", result);
        Exit(IDC_AUTOCANCEL);
        return;
      }
    }
#else
    if (_task)
    {
      HRESULT result = XOnlineTaskContinue(_task);
      if (result == XONLINETASK_S_RUNNING) return;
      if (FAILED(result))
      {
        RptF("Same title join: XOnlineTaskContinue failed with error 0x%x", result);
        XOnlineTaskClose(_task);
        _task = NULL;
        Exit(IDC_AUTOCANCEL);
        return;
      }

      DWORD nResults;
      PXONLINE_MATCH_SEARCHRESULT *results;
      result = XOnlineMatchSearchGetResults(_task, &results, &nResults);
      XOnlineTaskClose(_task);
      _task = NULL;
      if (FAILED(result))
      {
        RptF("Same title join: XOnlineMatchSearchGetResults failed with error 0x%x", result);
        Exit(IDC_AUTOCANCEL);
        return;
      }
      if (nResults == 0)
      {
        // session does not exist
        Exit(IDC_AUTOCANCEL);
        return;
      }

      const DWORD samples = 8;
      const DWORD bandwidth = 0; // default
      const XNADDR *xna[] = {&results[0]->HostAddress};
      const XNKID *xnkid[] = {&results[0]->SessionID};
      const XNKEY *xnkey[] = {&results[0]->KeyExchangeKey};
      INT err = XNetQosLookup
        (
        1, xna, xnkid, xnkey, // Xbox hosts
        0, NULL, NULL, // Security gateways
        samples, bandwidth, 0, NULL, &_qosResults
        );
      if (err != 0)
      {
        RptF("Qos lookup failed - error 0x%x", err);
        Exit(IDC_AUTOCANCEL);
        return;
      }
    }
#endif // _XBOX_VER >= 200
  }
}

void DisplayTestNetworkConditions::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    break;
  case IDC_CANCEL:
    Exit(IDC_ABORT);
    break;
  }
}
#endif

static const int BandwidthPerClient = 32000;

DisplayDedicatedServerSettings::DisplayDedicatedServerSettings(ControlsContainer *parent)
: Display(parent)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // from now on we need to be connected to Live
  GSaveSystem.SetLiveConnectionNeeded(true);
#endif

  Load("RscDisplayDedicatedServerSettings");

  int bandwidth = 256000; // by default assume 256 kbps for Live

#if _GAMES_FOR_WINDOWS || defined _XBOX

  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));

  DWORD qosStart = GlobalTickCount();

  // estimate bandwidth by probing Live service
  XNQOS *qos = NULL;
  int qosRet = XNetQosServiceLookup(0,NULL,&qos);

  // force pumping in first loop
  do
  {
    ProgressRefresh();
  }
  while (
    qosRet==0 && (((volatile BYTE &)(qos->axnqosinfo[0].bFlags))&XNET_XNQOSINFO_COMPLETE)==0
    );
  DWORD qosDuration = GlobalTickCount() - qosStart;
  LogF("QoS duration %d ms",qosDuration);

  if (qosRet==0 && qos)
  {
    bandwidth = min(qos->axnqosinfo[0].dwUpBitsPerSec, qos->axnqosinfo[0].dwDnBitsPerSec);
    LogF("Bandwidth detected %d kbps up",qos->axnqosinfo[0].dwUpBitsPerSec/1000);
    LogF("Bandwidth detected %d kbps down",qos->axnqosinfo[0].dwDnBitsPerSec/1000);
  }
  if (qos)
  {
    XNetQosRelease(qos);
  }

  ProgressFinish(p);
#endif

  _defaultMaxPlayers = bandwidth / BandwidthPerClient;
  _bandwidth = -1;
  _slots = 0;

  if (_totalSlots)
  {
    _totalSlots->SetRange(1, MAX_PLAYERS_DS);
    _totalSlots->SetThumbPos(_defaultMaxPlayers);
  }
  if (_privateSlots)
  {
    _privateSlots->SetRange(0, MAX_PLAYERS_DS);
    _privateSlots->SetThumbPos(0);
  }
}

Control *DisplayDedicatedServerSettings::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_DED_SERVER_SLOTS_PUBLIC:
    _totalSlots = GetSliderContainer(ctrl);
    break;
  case IDC_DED_SERVER_SLOTS_PRIVATE:
    _privateSlots = GetSliderContainer(ctrl);
    break;
  }

  return ctrl;
}

void DisplayDedicatedServerSettings::OnSimulate(EntityAI *vehicle)
{
  if(_totalSlots && _privateSlots)
    {
      if(_privateSlots->GetThumbPos() > _totalSlots->GetThumbPos())    
        _privateSlots->SetThumbPos(_totalSlots->GetThumbPos());
    }
  Display::OnSimulate(vehicle);
}

void DisplayDedicatedServerSettings::LaunchDS()
{
#if _XBOX_SECURE && _ENABLE_MP
#if _XBOX_VER >= 200
  int userIndex = GSaveSystem.GetUserIndex();
  if (userIndex < 0) return;
#else
  XONLINE_USER *user = XOnlineGetLogonUsers();
  Assert(user);
  if (!user) return;
#endif // _XBOX_VER >= 200
  _bandwidth = _defaultMaxPlayers * BandwidthPerClient;
  _slots = 0;
  if (_totalSlots)
  {
    _bandwidth = _totalSlots->GetThumbPos() * BandwidthPerClient;
  }
  if (_privateSlots)
  {
    _slots = _privateSlots->GetThumbPos();
  }
  Exit(IDC_OK);
#endif // _XBOX_SECURE && _ENABLE_MP
}

void DisplayDedicatedServerSettings::OnButtonClicked(int idc)
{
  if (idc == IDC_OK)
  {
    if (_totalSlots)
    {
      int totalSlots = _totalSlots->GetThumbPos();
      if (totalSlots > _defaultMaxPlayers)
     { 
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox
        (
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          Format(LocalizeString(IDS_DISP_XBOX_MP_PLAYER_COUNT), _defaultMaxPlayers),
          IDD_MSG_MP_PLAYER_COUNT, false, &buttonOK, &buttonCancel
        );
        return;
      }
    }

    // launch dedicated server
    LaunchDS();
  }
  else Display::OnButtonClicked(idc);
}

void DisplayDedicatedServerSettings::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  if (idd == IDD_MSG_MP_PLAYER_COUNT && exit == IDC_OK)
  {
    LaunchDS();
  }
}

#if defined _XBOX || _GAMES_FOR_WINDOWS
RString FromWideChar(const WCHAR *text)
{
  char buffer[1024];
#ifdef _WIN32
  WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, sizeof(buffer), NULL, NULL);
  // make sure result is always null terminated
  buffer[sizeof(buffer)-1] = 0;
#else
  buffer[0]=0;
#endif
  return buffer;
}

void ToWideChar(WCHAR *buffer, int bufferSize, RString text)
{
#ifdef _WIN32
  MultiByteToWideChar(CP_ACP, 0, text, -1, buffer, bufferSize);
  // make sure result is always null terminated
  buffer[bufferSize-1]=0;
#else
  buffer[0]=0;
#endif
}

LocalizedString DecodeLocalizedString(const WCHAR *buffer)
{
  if (buffer[0]==0)
  {
    return LocalizedString();
  }
  else
  {
    int type = buffer[0]-' ';
    if (type>LocalizedString::Stringtable) type = LocalizedString::Stringtable;
    if (type<LocalizedString::PlainText) type = LocalizedString::PlainText;
    return LocalizedString(LocalizedString::Type(type),FromWideChar(buffer+1));
  }
}

void EncodeLocalizedString(WCHAR *buffer, int bufferSize, const LocalizedString &name)
{
  ToWideChar(buffer+1, bufferSize-1, name._id);
  buffer[0] = name._type+' '; // encode localization as ASCII ' ' or '!'
}

#endif

#ifdef _XBOX

static DWORD GetXLanguage()
{
  int lang = GetLangID();
  switch (lang)
  {
  case Chinese:
    return XC_LANGUAGE_TCHINESE;
  case English:
    return XC_LANGUAGE_ENGLISH;
  case French:
    return XC_LANGUAGE_FRENCH;
  case German:
    return XC_LANGUAGE_GERMAN;
  case Italian:
    return XC_LANGUAGE_ITALIAN;
  case Japanese:
    return XC_LANGUAGE_JAPANESE;
  case Korean:
    return XC_LANGUAGE_KOREAN;
  case Portuguese:
    return XC_LANGUAGE_PORTUGUESE;
  case Spanish:
    return XC_LANGUAGE_SPANISH;
  }
  RptF("Unsupported language %d", lang);
  return XC_LANGUAGE_ENGLISH;
}

// Invitations are handled by Xbox Guide on Xbox 360
#if _XBOX_VER < 200
RString InsertDiscForJoin()
{
  WCHAR gameNameW[MAX_TITLENAME_LEN];
  DWORD titleId = GSelectedFriend.dwTitleID;
  HRESULT result = XOnlineFriendsGetTitleName(titleId, GetXLanguage(), MAX_TITLENAME_LEN, gameNameW);
  RString gameName = SUCCEEDED(result) ? FromWideChar(gameNameW) : RString();
  RString insertDisc = Format(LocalizeString(IDS_XBOX_LIVE_INSERT_DISC),cc_cast(gameName));
  return insertDisc;
}
#endif

#endif


#define DS_MAX_LINES  100

class DisplayDedicatedServer : public Display
{
protected:
  InitPtr<CListBoxContainer> _report;

  bool _oldServer;
  int _oldBandwidth;
  int _oldPrivateSlots;

public:
  DisplayDedicatedServer(ControlsContainer *parent, int bandwidth, int privateSlots);
  ~DisplayDedicatedServer();

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);

  void OnButtonClicked(int idc);
  void OnChildDestroyed(int idd, int exit);
  void OnSimulate(EntityAI *vehicle);

  void DSConsole(const char *buf);
  void DSExit(bool invited);

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  /// handle invitation
  virtual void OnInvited();
#endif
};

#if _ENABLE_DEDICATED_SERVER
int GetDSBandwidth();
int GetDSPrivateSlots();
void SetDedicatedServer(bool set);
void SetDSBandwidth(int bandwidth);
void SetDSPrivateSlots(int privateSlots);
#endif

DisplayDedicatedServer::DisplayDedicatedServer(ControlsContainer *parent, int bandwidth, int privateSlots)
: Display(parent)
{
  Load("RscDisplayDedicatedServer");
  UpdateKeyHints();

#if _ENABLE_DEDICATED_SERVER
  _oldServer = IsDedicatedServer();
  _oldBandwidth = GetDSBandwidth();
  _oldPrivateSlots = GetDSPrivateSlots();
  SetDedicatedServer(true);
  SetDSBandwidth(bandwidth);
  SetDSPrivateSlots(privateSlots);
#else
  _oldServer = false;
  _oldBandwidth = -1;
  _oldPrivateSlots = -1;
#endif
}

DisplayDedicatedServer::~DisplayDedicatedServer()
{
#if _ENABLE_DEDICATED_SERVER
  SetDedicatedServer(_oldServer);
  SetDSBandwidth(_oldBandwidth);
  SetDSPrivateSlots(_oldPrivateSlots);
#endif
}

Control *DisplayDedicatedServer::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_DS_REPORT:
    _report = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayDedicatedServer::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_DS_PLAYERS:
    // Players
#if _GAMES_FOR_WINDOWS
    {
      int userIndex = GSaveSystem.GetUserIndex();
      if (userIndex >= 0) XShowPlayersUI(userIndex);
    }
#else
    CreateChild(new DisplayMPPlayers(this));
#endif
    break;
  case IDC_DS_FRIENDS:
    // Friends
#if _XBOX_SECURE && _ENABLE_MP
# if _XBOX_VER >= 200
    {
      int userIndex = GSaveSystem.GetUserIndex();
      if (userIndex >= 0) XShowFriendsUI(userIndex);
    }
# else
    if (GetNetworkManager().IsSignedIn()) 
      CreateChild(new DisplayFriends(this, true));
# endif
#endif
    break;
  case IDC_CANCEL:
    if (GetNetworkManager().GetIdentities() && GetNetworkManager().GetIdentities()->Size() > 0)
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION),
        IDD_MSG_TERMINATE_SESSION, false, &buttonOK, &buttonCancel);
    }
    else
      DSExit(false);
    break;
  }
  // no default handling
}

void DisplayDedicatedServer::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_NETWORK_CONDITIONS:
    switch (exit)
    {
    case IDC_OK:
      DSExit(true);
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
    return;
  case IDD_MSG_NETWORK_CONDITIONS:
    if (exit == IDC_OK)
    {
      DSExit(true);
    }
    break;
  case IDD_MSG_TERMINATE_SESSION:
    if (exit == IDC_OK) DSExit(false);
    break;
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  case IDD_MSG_ACCEPT_INVITATION:
    if (exit == IDC_OK)
    {
      Exit(IDC_CANCEL);
    }
    else
    {
      // Invitation rejected
      GSaveSystem.InvitationHandled();
    }
    break;    
#endif
  }
}

void DisplayDedicatedServer::OnSimulate(EntityAI *vehicle)
{
  UpdateKeyHints();

  Display::OnSimulate(vehicle);
}

void DisplayDedicatedServer::DSConsole(const char *buf)
{
  if (!_report) return;
  bool scroll = _report->GetCurSel() == _report->Size() - 1;
  while (_report->GetSize() >= DS_MAX_LINES) _report->DeleteString(0);
  _report->AddString(buf);
  // automatic scrolling
  if (scroll)
  {
    _report->SetCurSel(_report->Size() - 1);
  }
}

void DSConsole(const char *buf)
{
  AbstractOptionsUI *ui = GWorld->Options();
  Display *disp = static_cast<Display *>(ui);
  for (ControlsContainer *container = disp; container; container = container->Child())
  {
    if (container->IDD() == IDD_DEDICATED_SERVER)
    {
      static_cast<DisplayDedicatedServer *>(container)->DSConsole(buf);
      break;
    }
  }
}

static void DataToText(unsigned char *data, int n, char *ptr)
{
  for (int i=0; i<n; i++)
  {
    unsigned char ch = data[i];
    int c = ch / 16;
    *ptr++ = c >= 10 ? 'A' + c - 10 : '0' + c;
    c = ch % 16;
    *ptr++ = c >= 10 ? 'A' + c - 10 : '0' + c;
  }
  *ptr = 0;
}

void DisplayDedicatedServer::DSExit(bool invited)
{
  // Invitations implemented in GSaveGames
  Exit(IDC_CANCEL);
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

void DisplayDedicatedServer::OnInvited()
{
  if (_msgBox) return; // confirmation in progress

  if (GSaveSystem.IsInvitationConfirmed())
  {
    Exit(IDC_OK);
  }
  else
  {
    GSaveSystem.ConfirmInvitation();
    // let the user confirm the action because of possible progress lost
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    RString message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION);
    // special type of message box which will not close because of invitation
    _msgBox = new MsgBoxIgnoreInvitation(this, MB_BUTTON_OK | MB_BUTTON_CANCEL, message, IDD_MSG_ACCEPT_INVITATION, false, &buttonOK, &buttonCancel);
  }
}

#endif


DisplayLive::DisplayLive(ControlsContainer *parent, SessionType sessionType)
: Display(parent)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // from now on we need to be connected to Live
  GSaveSystem.SetLiveConnectionNeeded(true);
#endif

  _sessionType = sessionType;
  Load("RscDisplayLive");
}

void DisplayLive::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_LIVE_QUICK_MATCH:
    CreateChild(new DisplayQuickMatch(this, _sessionType));
    break;
  case IDC_LIVE_CUSTOM_MATCH:
    CreateChild(new DisplayOptiMatchFilter(this));
    break;
  case IDC_LIVE_CREATE_MATCH:
    {
#if defined _XBOX && _XBOX_VER >= 200
      // GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#endif
      // host session
      int rating = 0;
#if defined _XBOX && _XBOX_VER >= 200
      // Ranking is handled automatically for Ranked Matches
#elif defined _XBOX
      rating = ReadPlayerRating();
#endif
      GetNetworkManager().CreateSession(_sessionType, RString(), RString(), MAX_PLAYERS, false, GetNetworkPort(), false, RString(), rating);
      if (GetNetworkManager().IsServer())
      {
        CreateChild(new DisplayServer(this));
      }
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayLive::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_OPTIMATCH_FILTER:
    if (exit == IDC_OK)
    {
      DisplayOptiMatchFilter *display = dynamic_cast<DisplayOptiMatchFilter *>(_child.GetRef());
      Assert(display);
      RString gameType = display->GetGameType();
      int minPlayers = display->GetMinPlayers();
      int maxPlayers = display->GetMaxPlayers();
      int language = display->GetLanguage();
      int difficulty = display->GetDifficulty();
      int maxDifficulty = display->GetMaxDifficulty();
      Display::OnChildDestroyed(idd, exit);
      CreateChild(new DisplayOptiMatch(this, _sessionType, gameType, minPlayers, maxPlayers, language, difficulty, maxDifficulty));
    }
    else Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_QUICKMATCH:
    if (exit == IDC_MP_NO_SESSION)
    {
      Display::OnChildDestroyed(idd, exit);

      // offer create a host
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_XBOX_MSG_HOST_SESSION),
        IDD_MSG_HOST_SESSION, false, &buttonOK, &buttonCancel);
    }
    else Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_MSG_HOST_SESSION:
    if (exit == IDC_OK)
    {
      Display::OnChildDestroyed(idd, exit);

#if defined _XBOX && _XBOX_VER >= 200
      // GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#endif
      // host session
      int rating = 0;
#if defined _XBOX && _XBOX_VER >= 200
      // Ranking is handled automatically for Ranked Matches
#elif defined _XBOX
      rating = ReadPlayerRating();
#endif
      GetNetworkManager().CreateSession(_sessionType, RString(), RString(), MAX_PLAYERS, false, GetNetworkPort(), false, RString(), rating);
      if (GetNetworkManager().IsServer()) CreateChild(new DisplayServer(this));
    }
    else Display::OnChildDestroyed(idd, exit);
    break;
  case IDD_DEDICATED_SERVER:
  case IDD_SERVER:
  case IDD_MP_SETUP:
    Display::OnChildDestroyed(idd, exit);
    GetNetworkManager().Close();
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
const char *GetGamertag(const XUID &xuid)
{
  XONLINE_USER users[XONLINE_MAX_STORED_ONLINE_USERS];
  DWORD usersCount;

  HRESULT hr = XOnlineGetUsers(users, &usersCount);
  if (!SUCCEEDED(hr)) return "";
  for (DWORD i=0; i<usersCount; i++)
  {
    if (XOnlineAreUsersIdentical(&users[i].xuid, &xuid))
      return users[i].szGamertag;
  }
  return "";
}
#endif

DisplayMultiplayerType::DisplayMultiplayerType(ControlsContainer *parent)
 : Display(parent)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // we don't need to be connected to Live (connection is needed only for specific submenus of this) 
  GSaveSystem.SetLiveConnectionNeeded(false);
#endif

  Init();

  // Invitations and content download are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _ENABLE_MP
# if _XBOX_VER >= 200

# else
  if (GInvited)
  {
    // confirmation needed to accept game invitation
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    RString msg = Format(LocalizeString(IDS_MSG_PENDING_INVITATION), GetGamertag(GGameInvite.xuidAcceptedFriend), GGameInvite.InvitingFriend.szGamertag);
    CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, msg, IDD_MSG_PENDING_INVITATION, true, &buttonOK, &buttonCancel);

    GInvited = false; // invite processed
  }
  else
  {
    bool IsContentDownload();
    void ResetContentDownload();
    if (IsContentDownload())
    {
      if (_task)
      {
        XOnlineTaskClose(_task);
        _task = NULL;
      }
      if (_newContent)
      {
        _newContent = false;
        _download->SetText(LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_DOWNLOAD));
      }
      CreateChild(new DisplayDownloadContent(this));

      ResetContentDownload();
    }
  }
# endif
#endif
}

#if defined _XBOX && _XBOX_VER < 200 && _ENABLE_MP

#if _XBOX_SECURE
ULONGLONG ReadPlayerRating()
{
  XONLINE_USER *user = XOnlineGetLogonUsers();
  DoAssert(user);
  if (!user) return 0;

  XONLINE_STAT stat;
  stat.wID = XONLINE_STAT_RATING;
  stat.type = XONLINE_STAT_LONGLONG;
  stat.llValue = 0;

  XONLINE_STAT_SPEC spec;
  spec.xuidUser = user->xuid;
  int board = Pars >> "CfgLiveStats" >> "MPTotal" >> "board";
  spec.dwLeaderBoardID = board;
  spec.dwNumStats = 1;
  spec.pStats = &stat;

  XONLINETASK_HANDLE task;
  HRESULT result = XOnlineStatRead(1, &spec, NULL, &task);
  if (FAILED(result)) return 0;

  do
  {
    result = XOnlineTaskContinue(task);
  } while (result == XONLINETASK_S_RUNNING);

  ULONGLONG rating = 0;
  if (SUCCEEDED(result))
  {
    result = XOnlineStatReadGetResult(task, 1, &spec, 0, NULL); 
    if (SUCCEEDED(result))
    {
      if (spec.pStats->type == XONLINE_STAT_LONGLONG)
      {
        // valid value
        rating = spec.pStats->llValue;
      }
    }
  }
  XOnlineTaskClose(task);
  return rating;
}

ULONGLONG RoughRating(ULONGLONG rating)
{
  if (rating <= 1) return 0;
  const float invLog10 = 1.0f / log(10.0f);
  return toInt(log((float)rating) * invLog10);
}
#endif

DisplayMultiplayerType::DisplayMultiplayerType(ControlsContainer *parent, const XONLINE_FRIEND &f)
: Display(parent)
{
  Init();
#if _XBOX_SECURE && _XBOX_VER >= 200
  JoinToFriend(f);
#endif
}

#endif // defined _XBOX && _ENABLE_MP

#if defined _XBOX && _XBOX_VER >= 200

#include <Es/Memory/normalNew.hpp>

/// Encapsulation of XContentGetMarketplaceCounts operation
class OverlappedOperationGetMarketplaceCounts : public OverlappedOperation
{
protected:
  XOFFERING_CONTENTAVAILABLE_RESULT *_results;

public:
  HRESULT Start(DWORD userIndex, DWORD categories, XOFFERING_CONTENTAVAILABLE_RESULT *results);

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

HRESULT OverlappedOperationGetMarketplaceCounts::Start(DWORD userIndex, DWORD categories, XOFFERING_CONTENTAVAILABLE_RESULT *results)
{
  _results = results;
  return XContentGetMarketplaceCounts(userIndex, categories, sizeof(*_results), _results, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationGetMarketplaceCounts)

#endif // defined _XBOX && _XBOX_VER >= 200

void DisplayMultiplayerType::Init()
{
  Load("RscDisplayMPType");

#if defined _XBOX && _XBOX_VER >= 200
  _lastButtonClicked = 0;
  _testStorageDevice = true;
  _testStorageOnInvite = true;
#endif

#if defined _XBOX && _XBOX_SECURE && _ENABLE_MP

# if _XBOX_VER >= 200
  if (_download && _download->GetControl())
  {
    _download->GetControl()->EnableCtrl(false);
    // looking for the new / some content
    DWORD userIndex = GSaveSystem.GetUserIndex();
    if (userIndex >= 0)
    {
      Ref<OverlappedOperationGetMarketplaceCounts> operation = new OverlappedOperationGetMarketplaceCounts();
      HRESULT result = operation->Start(userIndex, 0xFFFFFFFF, &_operationResults);
      if (result == ERROR_IO_PENDING)
      {
        _operation = operation.GetRef();
      }
      else if (SUCCEEDED(result))
      {
        // update the control immediately
        _download->GetControl()->EnableCtrl(_operationResults.dwTotalOffers > 0);
        if (_operationResults.dwNewOffers > 0)
        {
          RString text = Format(LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_DOWNLOAD_NEW_OFFER), _operationResults.dwNewOffers);
          _download->SetText(text);
        }
      }
    }
  }
# else
  _request = false;
  _invitation = false;
  _newContent = false;

  // looking for the new content
  _task = NULL;
  HRESULT result = XOnlineOfferingIsNewContentAvailable(0xffffffff, NULL, &_task);
  if (FAILED(result))
  {
    RptF("XOnlineOfferingIsNewContentAvailable failed with error 0x%x", result);
    _task = NULL;
  }

  UpdateAppearOffline();

  // silent sign in
  HRESULT hr = GetNetworkManager().SilentSignInResult();
  if (hr == XONLINE_S_LOGON_USER_HAS_MESSAGE)
  {
    GetNetworkManager().IgnoreMessageAtSignIn();
    // message
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    CreateMsgBox(
      MB_BUTTON_OK | MB_BUTTON_CANCEL,
      LocalizeString(IDS_XBOX_LIVE_ERROR_NEW_MSG),
      IDD_MSG_XONLINE_RECOMMENDED_MSG, false, &buttonOK, &buttonCancel);
  }
# endif // _XBOX_VER < 200
#endif // defined _XBOX && _XBOX_SECURE && _ENABLE_MP
}

DisplayMultiplayerType::~DisplayMultiplayerType()
{
  // Content download handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
  if (_task)
  {
    XOnlineTaskClose(_task);
  }
#endif

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // we don't need to be on Live anymore
  GSaveSystem.SetLiveConnectionNeeded(false);
#endif

}

Control *DisplayMultiplayerType::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
#if _XBOX_SECURE && _XBOX_VER < 200
  case IDC_MP_TYPE_FRIENDS:
    _friends = GetStructuredText(ctrl);
    break;
#endif
  case IDC_MP_TYPE_DOWNLOAD:
    _download = GetTextContainer(ctrl);
    break;
  case IDC_MP_TYPE_INFO:
    _info = GetHTMLContainer(ctrl);
    ctrl->EnableCtrl(false);
    break;
  }
  return ctrl;
}

#if defined _XBOX && _XBOX_SECURE && _ENABLE_MP && !_SUPER_RELEASE
#include <XbDm.h>
#endif

#if defined _XBOX && _XBOX_VER >= 200
extern bool CheckForSignIn(bool online, bool forceUI = false, bool mustHaveMPPrivilege = false, bool *signInChanged = NULL);

/** 
  checks if the player is correctly signed in for multiplayer features
  \param display display used for showing message boxes
  \param liveEnabled if the player profile must be Live enabled
  \param onlinePrivilege if the player profile must have privilege to play online games (TCR 086)
  \param testStorage if we should test storage device
*/
bool CheckForSignInMultiplayer(Display *display, bool liveEnabled, bool onlinePrivilege, bool testStorage)
{
  DoAssert(display != NULL);
  if (!display)
    return false;

  CheckForSignIn(liveEnabled, false, onlinePrivilege);

  if (!GSaveSystem.CheckSignIn(liveEnabled, onlinePrivilege))
  {
    if (!GSaveSystem.IsUserSignedIn())
    {
      display->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_NOT_SIGNED_IN));
    }
    else
    {
      if (!liveEnabled)
      {
        display->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_NOT_SIGNED_IN));
      }
      else if (!GSaveSystem.CheckSignIn(true, false))
      {
        display->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_LIVE_NOT_SIGNED_IN));
      }
      else
      {
        display->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_NO_PRIVILEGES));
      }
    }
    return false;
  }

  if (testStorage)
  {
    // we have to select storage device
    if (GSaveSystem.IsUserSignedIn())
    {
      if (!GSaveSystem.CheckDevice(CHECK_SAVE_BYTES))
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        display->CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL, 
          LocalizeString(IDS_MSG_NO_STORAGE_DEVICE),
          IDD_MSG_XBOX_NO_STORAGE, false, &buttonOK, &buttonCancel
          );
        return false;
      }
    }      
  }

  return true;
}

#endif


void DisplayMultiplayerType::OnButtonClicked(int idc)
{
#if defined _XBOX && _XBOX_VER >= 200
  _lastButtonClicked = idc;
#endif

  switch (idc)
  {
  case IDC_MP_TYPE_RANKED_MATCH:
#if defined _XBOX && _XBOX_VER >= 200
    if (!CheckForSignInMultiplayer(this, true, true, _testStorageDevice)) 
      return;
#endif
     CreateChild(new DisplayLive(this, STRanked));
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_LIVE_WAITING);
#endif
    break;
  case IDC_MP_TYPE_PLAYER_MATCH:
#if defined _XBOX && _XBOX_VER >= 200
    if (!CheckForSignInMultiplayer(this, true, true, _testStorageDevice)) 
      return;
#endif
    CreateChild(new DisplayLive(this, STStandard));
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_LIVE_WAITING);
#endif
    break;
  case IDC_MP_TYPE_SYSTEM_LINK:
#if defined _XBOX && _XBOX_VER >= 200
    if (!CheckForSignInMultiplayer(this, false, false, _testStorageDevice)) 
      return;
#endif
    CreateChild(new DisplaySystemLink(this));
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    // System Link game selection
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_SYSTEMLINK_WAITING);
#endif
    break;

#if _XBOX_SECURE && _ENABLE_MP
  case IDC_MP_TYPE_FRIENDS:
#if _XBOX_VER >= 200
#ifdef _XBOX
    if (CheckForSignInMultiplayer(this, true, false, false)) 
#endif
    {
      XShowCommunitySessionsUI(GSaveSystem.GetUserIndex(), XSHOWCOMMUNITYSESSION_SHOWPARTY);
    }
# else
    CreateChild(new DisplayFriends(this, false));
# endif // _XBOX_VER >= 200
    break;
  case IDC_MP_TYPE_STATS:
#if defined _XBOX && _XBOX_VER >= 200
    if (!CheckForSignInMultiplayer(this, true, false, false)) 
      return;
#endif
    CreateChild(new DisplayLiveStats(this));
    break;
    // Content download handled by Xbox Guide on Xbox 360
  case IDC_MP_TYPE_DOWNLOAD:
# if defined _XBOX
#   if _XBOX_VER >= 200
    if (CheckForSignInMultiplayer(this, true, false, false)) 
    {
       XShowMarketplaceUI(GSaveSystem.GetUserIndex(), XSHOWMARKETPLACEUI_ENTRYPOINT_CONTENTLIST, 0, 0xFFFFFFFF);
    }
#   else
      if (_task)
      {
        XOnlineTaskClose(_task);
        _task = NULL;
      }
      if (_newContent)
      {
        _newContent = false;
        _download->SetText(LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_DOWNLOAD));
      }
      CreateChild(new DisplayDownloadContent(this));
#   endif // _XBOX_VER >= 200
# endif // defined _XBOX
    break;
# if _XBOX_VER < 200
  case IDC_MP_TYPE_APPEAR_OFFLINE:
    {
      bool cloak = !GetNetworkManager().IsCloaked();
      GetNetworkManager().Cloak(cloak);
    }
    UpdateAppearOffline();
    break;
# endif // _XBOX_VER < 200
#endif // _XBOX_SECURE && _ENABLE_MP

  // Sign off handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
  case IDC_MP_TYPE_SIGN_OUT:
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_MSG_SIGN_OUT),
        IDD_MSG_SIGN_OUT, false, &buttonOK, &buttonCancel);
    }
    break;
#endif // defined _XBOX && _XBOX_VER < 200

  case IDC_MP_TYPE_DEDICATED_SERVER:
#if defined _XBOX && _XBOX_VER >= 200
    if (!CheckForSignInMultiplayer(this, true, false, false)) 
      return;
#endif
    CreateChild(new DisplayDedicatedServerSettings(this));
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    // Dedicated server launched
    GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_DEDICATED_SERVER);
#endif
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
void DisplayMultiplayerType::JoinToFriend(const XONLINE_FRIEND &f)
{
  XONLINETASK_HANDLE task;
  HRESULT result = XOnlineMatchSessionFindFromID(f.sessionID, NULL, &task);
  if (FAILED(result))
  {
    RptF("Same title join: XOnlineMatchSessionFindFromID failed with error 0x%x", result);
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return;
  }
  do
  {
    result = XOnlineTaskContinue(task);
  } while(result == XONLINETASK_S_RUNNING);
  if (FAILED(result))
  {
    RptF("Same title join: XOnlineTaskContinue failed with error 0x%x", result);
    XOnlineTaskClose(task);
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return;
  }
  DWORD nResults;
  PXONLINE_MATCH_SEARCHRESULT *results;
  result = XOnlineMatchSearchGetResults(task, &results, &nResults);
  if (FAILED(result))
  {
    RptF("Same title join: XOnlineMatchSearchGetResults failed with error 0x%x", result);
    XOnlineTaskClose(task);
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return;
  }
  if (nResults == 0)
  {
    // session does not exist
    XOnlineTaskClose(task);
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return;
  }
  // TODO: check private slots

  SessionInfo session;
  session.addr = results[0]->HostAddress;
  session.kid = results[0]->SessionID;
  session.key = results[0]->KeyExchangeKey;
  session.port = GetNetworkPort();

  XOnlineTaskClose(task);

  JoinSession(this, sessionType, session, true, ReadPlayerRating());
}

void DisplayMultiplayerType::UpdateAppearOffline()
{
  ITextContainer *text = GetStructuredText(GetCtrl(IDC_MP_TYPE_APPEAR_OFFLINE));
  if (text)
  {
    if (GetNetworkManager().IsCloaked())
      text->SetText(LocalizeString(IDS_DISP_XBOX_MP_INTERRUPT_ONLINE));
    else
      text->SetText(LocalizeString(IDS_DISP_XBOX_MP_INTERRUPT_OFFLINE));
  }
}
#endif

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

void DisplayMultiplayerType::OnInvited()
{
  // Handle the invitation
  const XINVITE_INFO &info = GSaveSystem.GetInviteInfo();
  GSaveSystem.InvitationHandled();

  // first check if the active user is the same as the active user
  if (GSaveSystem.GetUserIndex() != GSaveSystem.GetInvitedUserId() || !GSaveSystem.CheckSignIn(true, true))
  {
    // no active user or active user is different from invited user

    // set active user to invited user
    GSaveSystem.SetUserIndex(GSaveSystem.GetInvitedUserId());

    // if user changed, we want always to test storage..
    _testStorageOnInvite = true;

    // check if we were successfull and XUIDs match
    if (!GSaveSystem.CheckSignIn(true, true) || GSaveSystem.GetUserIndex() != GSaveSystem.GetInvitedUserId())
    {
      // invalid user signed in - lets exit to main menu
      Exit(IDC_OK);
      return;
    }
    else
    {
      // update the main menu
      DisplayMain *disp = dynamic_cast<DisplayMain *>(GWorld->Options());
      if (disp) disp->UpdatePlayerName();

      // apply profile
      RString ApplyUserProfile();
      ApplyUserProfile();
    }
  }

  // check if XUIDs match
  if (NULL == GSaveSystem.GetUserInfo() || GSaveSystem.GetUserInfo()->xuid != info.xuidInvitee)
  {
    return;
  }

  if (_testStorageOnInvite)
  {
    // wait until the UI is shown (because we want to show storage select UI if needed)
    if (GSaveSystem.IsUIShown())
    {
      Ref<ProgressHandle> p = ProgressStartExt(false, LocalizeString(IDS_SIGN_IN), "%s", true);
      while (GSaveSystem.IsUIShown())
      {
        GSaveSystem.Simulate();
        ProgressRefresh();
        Sleep(20); // keep 50 fps
      };
      ProgressFinish(p);
    }

    // check if storage device is available
    if (!GSaveSystem.CheckDevice(CHECK_SAVE_BYTES))
    {
      // user did not select storage device - show confirmation dialog
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL, 
        LocalizeString(IDS_MSG_NO_STORAGE_DEVICE),
        IDD_MSG_XBOX_NO_STORAGE_ON_INVITE, false, &buttonOK, &buttonCancel
        );
      return;
    }
  }


  SessionInfo session;
  session.addr = info.hostInfo.hostAddress;
  session.kid = info.hostInfo.sessionID;
  session.key = info.hostInfo.keyExchangeKey;
  session.port = GetNetworkPort();

  int rating = 0;
  // Ranking is handled automatically for Ranked Matches
  JoinSession(this, STStandard, session, true, rating);
}

#endif

void DisplayMultiplayerType::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
#if defined _XBOX && _XBOX_VER >= 200
    case IDD_MSG_XBOX_NO_STORAGE:  // player did not select storage device
      {
        Display::OnChildDestroyed(idd, exit);
        if (exit == IDC_OK)
        {
          // player wants to continue without selecting storage device
          // we have to turn storage device testing off and then simulate last button click
          _testStorageDevice = false;
          OnButtonClicked(_lastButtonClicked);
          _testStorageDevice = true;
        }
      }
      break;

    case IDD_MSG_XBOX_NO_STORAGE_ON_INVITE:  // player did not select storage device when he was invited to game
      {
        Display::OnChildDestroyed(idd, exit);
        if (exit == IDC_OK)
        {
          // player wants to continue without selecting storage device
          _testStorageOnInvite = false;
          OnInvited();
          return;
        }
        else
        {
          Exit(IDC_OK);
        }
      }
      break;

#endif //#if defined _XBOX && _XBOX_VER >= 200

#if _XBOX_SECURE && _ENABLE_MP
  case IDD_MSG_XONLINE_RECOMMENDED_MSG:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
# if defined _XBOX
#   if _XBOX_VER >= 200
      // TODOX360: check how should dashboard be launched to free space
      XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
      // unreachable: XLaunchNewImage defined as DECLSPEC_NORETURN
#   else
      LD_LAUNCH_DASHBOARD ld;
      ZeroMemory(&ld, sizeof(ld));
      ld.dwReason = XLD_LAUNCH_DASHBOARD_ACCOUNT_MANAGEMENT;
      XLaunchNewImage(NULL, PLAUNCH_DATA(&ld));
#   endif
# endif // defined _XBOX
    }
    return;
#if _XBOX_VER < 200
  // Invitations handled by Xbox Guide on Xbox 360
  case IDD_MSG_PENDING_INVITATION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      GSelectedFriend = GGameInvite.InvitingFriend;
      CreateChild(new DisplayTestNetworkConditions(this, GSelectedFriend));
    }
    return;
  case IDD_NETWORK_CONDITIONS:
    Display::OnChildDestroyed(idd, exit);
    switch (exit)
    {
    case IDC_OK:
      JoinToFriend(GSelectedFriend);
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
    return;
  case IDD_MSG_NETWORK_CONDITIONS:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK) JoinToFriend(GSelectedFriend);
    return;
#endif
  case IDD_DEDICATED_SERVER_SETTINGS:
    if (exit == IDC_OK)
    {
      // read parameters from dialog
      DisplayDedicatedServerSettings *display = dynamic_cast<DisplayDedicatedServerSettings *>(_child.GetRef());
      Assert(display);
      int bandwidth = display->GetBandwidth();
      int privateSlots = display->GetPrivateSlots();
      Display::OnChildDestroyed(idd, exit);

      CreateChild(new DisplayDedicatedServer(this, bandwidth, privateSlots));

      RString GetServerConfig();
      RString config = GetServerConfig();

      RString password;

      GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

      ParamFile serverCfg;
      serverCfg.Parse(config, NULL, NULL, &globals);
      if (serverCfg.FindEntry("password")) password = serverCfg >> "password";

      int port = GetNetworkPort();

      int rating = 0;
#if _XBOX_VER >= 200
      // Ranking is handled automatically for Ranked Matches
#else
      if (GetNetworkManager().IsSignedIn() && !GetNetworkManager().IsSystemLink())
      {
        ULONGLONG ReadPlayerRating();
        rating = ReadPlayerRating();
      }
#endif

      GetNetworkManager().CreateSession(STStandard, RString(), password, MAX_PLAYERS_DS + 1, false, port, true, config, rating);
      if (!GetNetworkManager().IsServer())
      {
        // destroy the created display
        _child = NULL;
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
        // Returned to the menu
        GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_INMENU);
        // we don't need to be on Live anymore
        GSaveSystem.SetLiveConnectionNeeded(false);
#endif
        return; // server creation failed
      }

      return;
    }
    else
    {
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
      Display::OnChildDestroyed(idd, exit);
      // Returned to the menu
      GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_INMENU);
      // we don't need to be on Live anymore
      GSaveSystem.SetLiveConnectionNeeded(false);
#endif
    }
    break;
#endif
    case IDD_DEDICATED_SERVER:
    case IDD_SERVER:
    case IDD_MP_SETUP:
    case IDD_CLIENT:
      GetNetworkManager().Close();
    case IDD_MULTIPLAYER:
    case IDD_LIVE:
      Display::OnChildDestroyed(idd, exit);
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
      // Returned to the menu
      GSaveSystem.SetContext(X_CONTEXT_PRESENCE, CONTEXT_PRESENCE_STR_RP_INMENU);
      // we don't need to be on Live anymore
      GSaveSystem.SetLiveConnectionNeeded(false);
#endif
      break;
      // Sign off handled by Xbox Guide on Xbox 360
#if defined _XBOX && _XBOX_VER < 200
    case IDD_MSG_SIGN_OUT:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        GetNetworkManager().SignOff();
        Exit(IDC_CANCEL);
      }
      break;
#endif // defined _XBOX && _XBOX_VER < 200

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    case IDD_LIVE_STATS:
      Display::OnChildDestroyed(idd, exit);
      // we don't need to be on Live anymore
      GSaveSystem.SetLiveConnectionNeeded(false);
      break;
#endif

  default:
     Display::OnChildDestroyed(idd, exit);
     break;
  }
}

void DisplayMultiplayerType::OnSimulate(EntityAI *vehicle)
{
#if defined _XBOX && _XBOX_SECURE && _ENABLE_MP

# if _XBOX_VER >= 200 
  if (_download && _download->GetControl() && _operation)
  {
    HRESULT result = _operation->Process();
    if (result != ERROR_IO_PENDING)
    {
      // operation finished
      _operation = NULL;
      if (SUCCEEDED(result))
      {
        // update the control
        _download->GetControl()->EnableCtrl(_operationResults.dwTotalOffers > 0);
        if (_operationResults.dwNewOffers > 0)
        {
          RString text = Format(LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_DOWNLOAD_NEW_OFFER), _operationResults.dwNewOffers);
          _download->SetText(text);
        }
      }
    }
  }
# else
  // Invitations handled by Xbox Guide on Xbox 360
  if (!GetNetworkManager().IsSignedIn())
  {
    Exit(IDC_CANCEL);
    return;
  }

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

  if (_task)
  {
    HRESULT result = XOnlineTaskContinue(_task);
    if (result != XONLINETASK_S_RUNNING)
    {
      XOnlineTaskClose(_task);
      _task = NULL;

      if (FAILED(result))
      {
        RptF("Cannot continue with new content query - error 0x%x", result);
      }
      else if (result == XONLINE_S_OFFERING_NEW_CONTENT)
      {
        _newContent = true;
        RString text =
          LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_DOWNLOAD) +
          RString("<img image='\\xmisc\\balicek.paa'/>");
        _download->SetStructuredText(text);
      }
    }
  }
# endif
#endif

  Display::OnSimulate(vehicle);
} 

// Multiplayer display

#if _ENABLE_GAMESPY

struct CheckPatchAvailableContext
{
  bool _done;
  bool _available;
  bool _mandatory;
  RString _versionName;
  int _fileId;
  RString _downloadURL;

  CheckPatchAvailableContext() {_done = false;}
};

static void CheckPatchAvailableDone(PTBool available, PTBool mandatory, 
  const gsi_char *versionName, int fileID, const gsi_char * downloadURL, void *param)
{
  CheckPatchAvailableContext *context = reinterpret_cast<CheckPatchAvailableContext *>(param);
  context->_done = true;
  context->_available = available != 0;
  context->_mandatory = mandatory != 0;
  context->_versionName = versionName;
  context->_fileId = fileID;
  context->_downloadURL = downloadURL;
}

/// check the version
static void CheckPatchAvailable()
{
  CheckPatchAvailableContext context;
  PTBool ok = ptCheckForPatch(GetProductID(), APP_VERSION_TEXT, GetDistributionId(),
    &CheckPatchAvailableDone, PTFalse, &context);
  if (!ok)
  {
    RptF("Check for the patch failed.");
    return;
  }

  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
  while (!context._done)
  {
    ghttpThink();
    ProgressRefresh();
  }
  ProgressFinish(p);

  if (context._available)
  {
    RString msg = LocalizeString(context._mandatory ? IDS_MSG_MANDATORY_PATCH_AVAILABLE : IDS_MSG_PATCH_AVAILABLE);
    WarningMessage(msg, cc_cast(context._versionName), cc_cast(context._downloadURL));
  }
}

#else

static void CheckPatchAvailable()
{
}

#endif

#if _ENABLE_HOST || _ENABLE_DEDICATED_SERVER
/*!
\patch 1.34 Date 12/7/2001 by Jirka
- Fixed: When server creation failed, application was in corrupted state
*/
CDP_DECL void __cdecl CDPCreateServer()
{
  SECUROM_MARKER_HIGH_SECURITY_ON(2)
  Display *options = dynamic_cast<Display *>(GWorld->Options());
  if (!options) return;

  int port = GetServerPort();
  RString password = GetNetworkPassword();
  GetNetworkManager().Init("", port, false);
  GetNetworkManager().CreateSession(STSystemLink, RString(), password, MAX_PLAYERS, false, port, false, RString(), 0);
  if (GetNetworkManager().IsServer())
  {
    options->CreateChild(new DisplayServer(options));
  }

  SECUROM_MARKER_HIGH_SECURITY_OFF(2)
}

#endif

/*!
\patch 5232 Date 2/26/2008 by Bebul
- Fixed: VoN - P2P connection was not established sometimes.
*/
#if _ENABLE_GAMESPY
void SBCallback(ServerBrowser serverBrowser, SBCallbackReason reason, SBServer server, void *instance)
{
  ((DisplayMultiplayer *)instance)->OnSBUpdated(serverBrowser, reason, server);
}

void SBCallbackEmpty(ServerBrowser serverBrowser, SBCallbackReason reason, SBServer server, void *instance)
{
}

//SB - maximum number of concurrent updates
static const int MaxUpdates = 20;
#endif

#if _ENABLE_GAMESPY
unsigned int GetMyPublicIPAddr(ServerBrowser sb=NULL)
{
  unsigned int retVal = 0;
  if (!sb)
  { // create temporary
    Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
    // check that the game's back-end is available
    GSIStartAvailableCheck(GetGameName());
    GSIACResult result;
    while ((result = GSIAvailableCheckThink()) == GSIACWaiting)
    {
      msleep(5);
      ProgressRefresh();
    }
    switch (result)
    {
    case GSIACAvailable:
      // the game's back-end services are available
      sb = ServerBrowserNew(GetGameName(), GetGameName(), GetSecretKey(),
        0, MaxUpdates, QVERSION_QR2, SBFalse, SBCallbackEmpty, NULL);
      break;
    case GSIACUnavailable:
      // the game's back-end services are unavailable
      break;
    case GSIACTemporarilyUnavailable:
      // the game's back-end services are temporarily unavailable
      break;
    }
    if (sb)
    {
      unsigned char fields[] = { HOSTNAME_KEY }; // maybe the empty list would work too
      int fieldsCount = lenof(fields);

      // do an update (we need the public IP to be filled in)
      RString filter = RString("publicip=0"); //no server should match, but we need only IP address
      ServerBrowserUpdate(sb, SBTrue, SBFalse, fields, fieldsCount, (char *)(const char *)filter);

      SBState result;
      while (( result = ServerBrowserState(sb) ) != sb_connected)
      {
        ServerBrowserThink(sb);
        retVal = ServerBrowserGetMyPublicIPAddr(sb);
        // once we have na IP address, there is no need to continue enumerating
        // we are not interested in the results anyway
        if (retVal!=0)
        {
          break;
        }
        msleep(5);
        ProgressRefresh();
        if (result == sb_disconnected) break; //maybe some problems with server (VoN retranslation through server should be sufficient)
      }
      if (!retVal)
      {
        RptF("Unable to get my public IP address: %d", result);
      }
      ServerBrowserFree(sb);
    }
    ProgressFinish(p);
  }
  else
  {
    // Get the IP address
    retVal = ServerBrowserGetMyPublicIPAddr(sb);
    if (!retVal)
      RptF("Unable to get my public IP address");

  }
  return retVal;
}
#endif

#if !defined _XBOX

/*!
\patch 1.34 Date 12/7/2001 by Jirka
- Fixed: When server creation failed, application was in corrupted state
*/

CDP_DECL void __cdecl CDPCreateClient(RString ip, int port, RString password, RString serverName)
{
#if _XBOX_SECURE
  Fail("Not supported");
#else
  SECUROM_MARKER_HIGH_SECURITY_ON(3)
  Display *options = dynamic_cast<Display *>(GWorld->Options());
  if (!options) return;

  GetNetworkManager().Init(ip, port);
  // CHANGED
  auto session = GetNetworkManager().WaitForSession();
  RString guid = session.guid;
  if (guid.IsEmpty())
  {
    GetNetworkManager().Done();
    return;
  }
  if (session.password && password.IsEmpty())
  {
    // open password display to get the password
    options->CreateChild(new DisplayPassword(options, ip, serverName));
    return;
  }
  ConnectResult result = GetNetworkManager().JoinSession(STSystemLink, guid, password);
  GetNetworkManager().CustomFilesProgressClear();
#if _ENABLE_GAMESPY
  GetNetworkManager().SetPublicAddress(GetMyPublicIPAddr());
#endif
  switch (result)
  {
  case CROK:
    options->CreateChild(new DisplayClient(options));
    break;
  case CRPassword:
    options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
    break;
  case CRVersion:
    options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
    break;
  case CRError:
    options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
    break;
  case CRSessionFull:
    options->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
    break;
  }
  SECUROM_MARKER_HIGH_SECURITY_OFF(3)
#endif

}

#endif

//@{ _DEDICATED_CLIENT
#if _ENABLE_GAMESPY
#include "../GameSpy/serverbrowsing/sb_serverbrowsing.h"
# ifdef NOMINMAX
#  undef min
#  undef max
# endif
#endif
#include "../Network/netTransport.hpp"
int ConsoleF(const char *format, ...);

//! similar functionality as CDPCreateClient, but it should not be Display driven
bool CreateDedicatedClient(RString ip, int port, RString password)
{
#if 0
  if (GWorld->Options())
  {
    GWorld->Options()->DestroyHUD(-1);
  }
#endif

  GetNetworkManager().Init(ip, port);
  // CHANGED
  RString guid = GetNetworkManager().WaitForSession().guid;
  if (guid.IsEmpty())
  {
    GetNetworkManager().Done();
    return false;
  }
  ConnectResult result = GetNetworkManager().JoinSession(STSystemLink, guid, password);
  GetNetworkManager().CustomFilesProgressClear();
#if _ENABLE_GAMESPY
  extern unsigned int GetMyPublicIPAddr(ServerBrowser sb=NULL);
  GetNetworkManager().SetPublicAddress(GetMyPublicIPAddr());
#endif
  switch (result)
  {
  case CROK:
    {
      Display *options = dynamic_cast<Display *>(GWorld->Options());
      if (!options) break;
      options->CreateChild(new DisplayClient(options));

    }
    break;
  case CRPassword:
    ConsoleF(LocalizeString(IDS_MSG_MP_PASSWORD));
    break;
  case CRVersion:
    ConsoleF(LocalizeString(IDS_MSG_MP_VERSION));
    break;
  case CRError:
    ConsoleF(LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
    break;
  case CRSessionFull:
    ConsoleF(LocalizeString(IDS_MSG_MP_SESSION_FULL));
    break;
  }

  if (result!=CROK) return false;
  ConsoleF("Client connected: %s", cc_cast(guid));
  return true;
}

//@} _DEDICATED_CLIENT


#ifndef _XBOX

//! Session filter display
class DisplaySessionFilter : public Display
{
  typedef Display base;

protected:
  SessionFilter _filter;

public:
  //! constructor
  /*!
    \param parent parent display
    \param filter current session filter
  */
  DisplaySessionFilter(ControlsContainer *parent, SessionFilter &filter);
  void OnButtonClicked(int idc);
  void OnLBSelChanged(IControl *ctrl, int curSel);

  bool CanDestroy();
  void Destroy();

  //! return session filter
  const SessionFilter &GetFilter() const {return _filter;}

protected:
  //! update switches (show/hide)
  void UpdateButtons();

  //! update edit boxes
  void UpdateValues();
};

DisplaySessionFilter::DisplaySessionFilter(ControlsContainer *parent, SessionFilter &filter)
: Display(parent)
{
  _enableSimulation = false;
  _filter = filter;
  Load("RscDisplayFilter");
  
  // fill the list of mission types
  CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_FILTER_TYPE));
  if (list)
  {
    // no filter
    int index = list->AddString(RString());
    list->SetData(index, RString());
    // game types    
    ParamEntryVal array = Pars >> "CfgMPGameTypes";
    for (int i=0; i<array.GetEntryCount(); i++)
    {
      ParamEntryVal entry = array.GetEntry(i);
      if (!entry.IsClass()) continue;
      int index = list->AddString(entry >> "name");
      list->SetData(index, entry.GetName());
    }
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_FULL));
  if (list)
  {
    list->AddString(
      Format(LocalizeString(IDS_FILTER_FULL),(const char *)( LocalizeString(IDS_DISP_SHOW) ) )
      );
    list->AddString(
      Format(LocalizeString(IDS_FILTER_FULL),(const char *)(  LocalizeString(IDS_DISP_HIDE)) )
      );

    if(list->GetSize()>1)
    {
      if(_filter.fullServers == true) list->SetCurSel(0);
      else  list->SetCurSel(1);
    }
  }


  list = GetListBoxContainer(GetCtrl(IDC_FILTER_PASSWORDED));
  if (list)
  {
    list->AddString(
            Format(LocalizeString(IDS_FILTER_PASSWORDED), (const char *)(LocalizeString(IDS_DISP_SHOW)))
            );
    list->AddString(
            Format(LocalizeString(IDS_FILTER_PASSWORDED), (const char *)(LocalizeString(IDS_DISP_HIDE)))
            );

    if(list->GetSize()>1)
    {
      if(_filter.passwordedServers == true) list->SetCurSel(0);
      else  list->SetCurSel(1);
    }
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_BATTLEYE));
  if (list)
  {
    list->AddString(
      Format(LocalizeString(IDS_DISP_DEFAULT))
      );
    list->AddString(
      Format(LocalizeString(IDS_LIB_INFO_YES))
      );
    list->AddString(
      Format(LocalizeString(IDS_LIB_INFO_NO))
      );
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_EXPANSIONS));
  if (list)
  {
    list->AddString(
      Format(LocalizeString(IDS_DISP_SHOW))
      );
    list->AddString(
      Format(LocalizeString(IDS_DISP_HIDE))
      );
    if(list->GetSize()>1)
    {
      if(_filter.expansionsServers == true) list->SetCurSel(0);
      else  list->SetCurSel(1);
    }
  }

  UpdateValues();
  UpdateButtons();
}

void DisplaySessionFilter::OnButtonClicked(int idc)
{
  switch (idc)
  {

  case IDC_FILTER_BATTLEYE:
    (int &)_filter.battleyeRequired = ((int)_filter.battleyeRequired+1)%SessionFilter::BEFLT_COUNT;
    UpdateButtons();
    break;
  case IDC_FILTER_DEFAULT:
    _filter = SessionFilter();
    UpdateValues();
    UpdateButtons();
    break;
  default:
    base::OnButtonClicked(idc);
    break;
  }
}

void DisplaySessionFilter::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_FILTER_FULL:
    if(curSel==0) _filter.fullServers = true;
    else _filter.fullServers = false;
    break;
  case IDC_FILTER_PASSWORDED:
    if(curSel==0) _filter.passwordedServers = true;
    else  _filter.passwordedServers = false;
    break;
  case IDC_FILTER_BATTLEYE:
    if(curSel>=0 && curSel<SessionFilter::BEFLT_COUNT)
      (int &)_filter.battleyeRequired = curSel;
    break;
  case IDC_FILTER_EXPANSIONS:
    if(curSel==0) _filter.expansionsServers = true;
    else  _filter.expansionsServers = false;
    break;
  }
}

bool DisplaySessionFilter::CanDestroy()
{
  if (!base::CanDestroy()) return false;

  return true;
}

void DisplaySessionFilter::Destroy()
{
  CEditContainer *ctrl = GetEditContainer(GetCtrl(IDC_FILTER_SERVER));
  if (ctrl) _filter.serverName = ctrl->GetText();

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MISSION));
  if (ctrl) _filter.missionName = ctrl->GetText();

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MAXPING));
  if (ctrl) _filter.maxPing = atoi(ctrl->GetText());

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MINPLAYERS));
  if (ctrl) _filter.minPlayers = atoi(ctrl->GetText());

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MAXPLAYERS));
  if (ctrl) _filter.maxPlayers = atoi(ctrl->GetText());

  CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_FILTER_TYPE));
  if (list)
  {
    int sel = list->GetCurSel();
    if (sel >= 0) _filter.missionType = list->GetData(sel);
  }

  base::Destroy();
}

void DisplaySessionFilter::UpdateValues()
{
  CEditContainer *ctrl = GetEditContainer(GetCtrl(IDC_FILTER_SERVER));
  if (ctrl) ctrl->SetText(_filter.serverName);

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MISSION));
  if (ctrl) ctrl->SetText(_filter.missionName);

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MAXPING));
  if (ctrl) ctrl->SetText(Format("%d", _filter.maxPing));

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MINPLAYERS));
  if (ctrl) ctrl->SetText(Format("%d", _filter.minPlayers));

  ctrl = GetEditContainer(GetCtrl(IDC_FILTER_MAXPLAYERS));
  if (ctrl) ctrl->SetText(Format("%d", _filter.maxPlayers));

  CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_FILTER_TYPE));
  if (list)
  {
    int sel = 0;
    for (int i=0; i<list->GetSize(); i++)
    {
      if (stricmp(list->GetData(i), _filter.missionType) == 0)
      {
        sel = i;
        break;
      }
    }
    list->SetCurSel(sel);
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_FULL));
  if (list && list->GetSize()>1)
  {
    if(_filter.fullServers) list->SetCurSel(0);
    else list->SetCurSel(1);
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_PASSWORDED));
  if (list && list->GetSize()>1)
  {
    if(_filter.passwordedServers) list->SetCurSel(0);
    else list->SetCurSel(1);
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_BATTLEYE));
  if (list && list->GetSize()>=3)
  {
    switch (_filter.battleyeRequired)
    {
    case SessionFilter::BEFLT_NONE: list->SetCurSel(0); break;
    case SessionFilter::BEFLT_REQUIRED: list->SetCurSel(1); break;
    case SessionFilter::BEFLT_WITHOUT_BE: list->SetCurSel(2); break;
    }
  }

  list = GetListBoxContainer(GetCtrl(IDC_FILTER_EXPANSIONS));
  if (list && list->GetSize()>1)
  {
    if(_filter.expansionsServers) list->SetCurSel(0);
    else list->SetCurSel(1);
  }
}

void DisplaySessionFilter::UpdateButtons()
{
}
#endif

#ifdef _XBOX
const int PingGood = 50;
const int PingPoor = 150;
#else
const int PingGood = 100;
const int PingPoor = 300;
#endif


CSessionsContainer::CSessionsContainer(ParamEntryPar cls)
{
  _password = GlobLoadTextureUI(cls >> "password");
  _version = GlobLoadTextureUI(cls >> "version");
  _addons = GlobLoadTextureUI(cls >> "addons");
  _mods = GlobLoadTextureUI(cls >> "mods");
  _lockText = GlobLoadTextureUI(cls >> "locked");
  _none = GlobLoadTextureUI(cls >> "none");

  _colorPingUnknown = GetPackedColor(cls >> "colorPingUnknown");
  _colorPingGood = GetPackedColor(cls >> "colorPingGood");
  _colorPingPoor = GetPackedColor(cls >> "colorPingPoor");
  _colorPingBad = GetPackedColor(cls >> "colorPingBad");

  _host.name = LocalizeString(IDS_HOST_SESSION);
  _host.lastTime = 0xffffffff;
  _host.badActualVersion = false;
  _host.badRequiredVersion = false;
  _host.badSignatures = false;
  _host.badAdditionalMods = false;
  _host.badMod = false;
  _host.password = false;
  _host.lock = false;
  _host.serverState = NSSNone;
  _host.ping = 0;
  _host.playersPublic = -1;
  _host.slotsPublic = -1;
#if _XBOX_SECURE
  _host.playersPrivate = -1;
  _host.slotsPrivate = -1;
#endif
  _host.dedicated = false;
  _host.timeleft = 0;
  _host.language = -1;
  _host.difficulty = -1;
}

RString CSessionsContainer::GetSessionID(int i) const
{
  if (i >= _indices.Size()) return RString();
  const SessionInfo &session = GetSession(i);
#if _XBOX_SECURE
  const BYTE *addr = session.kid.ab;
  char buffer[17];
  char *ptr = buffer;
  for (int i=0; i<8; i++)
  {
    int hi = (addr[i] & 0xf0) >> 4;
    int lo = addr[i] & 0x0f;
    *(ptr++) = hi >= 10 ? 'a' + hi - 10 : '0' + hi;
    *(ptr++) = lo >= 10 ? 'a' + lo - 10 : '0' + lo;
  }
  *ptr = 0;
  return buffer;
#else
  return session.guid;
#endif
}

void CSessionsContainer::DeleteSession(int index)
{
  // delete session
  _sessions.Delete(index);
  // update the selection
  if (index >= NSessions()) SelectSession(index - 1);
  // update the indices
  for (int i=0; i<_indices.Size();)
  {
    if (_indices[i] == index)
    {
      _indices.Delete(i);
      continue;
    }
    if (_indices[i] > index) _indices[i]--;
    i++;
  }
}

Texture *CSessionsContainer::GetSessionIcon(int index) const
{
  if (index < 0 || index >= _indices.Size()) return _none;
  
  const SessionInfo &session = _sessions[_indices[index]];
  if (session.badActualVersion || session.badRequiredVersion) return _version;
  else if (session.badSignatures || session.badMod) return _addons;
  else if (session.lock) return _lockText;
  else if (session.password) return _password;
  return _none;
}

struct CmpSessionsContext
{
  SortColumn column;
  bool ascending;
  const AutoArray<SessionInfo> &sessions;

  CmpSessionsContext(const AutoArray<SessionInfo> &s) : sessions(s) {}
};

/*!
\patch 5115 Date 1/10/2007 by Jirka
- Fixed: MP session list - better sorting by the host name or the mission name
*/

static int CompareUTFStrings(RString text1, RString text2)
{
  // convert from UTF-8 to Unicode
  int len = MultiByteToWideChar(CP_UTF8, 0, text1, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wText1, 1024);
  wText1.Resize(len);
  MultiByteToWideChar(CP_UTF8, 0, text1, -1, wText1.Data(), len);
  wText1[len - 1] = 0; // make sure result is always null terminated

  len = MultiByteToWideChar(CP_UTF8, 0, text2, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wText2, 1024);
  wText2.Resize(len);
  MultiByteToWideChar(CP_UTF8, 0, text2, -1, wText2.Data(), len);
  wText2[len - 1] = 0; // make sure result is always null terminated

#ifdef WIN32
  return _wcsicmp(wText1.Data(), wText2.Data());
#else
  return wcscasecmp(wText1.Data(), wText2.Data());
#endif
}

static int CmpSessions(const int *index1, const int *index2, CmpSessionsContext ctx)
{
  const SessionInfo &info1 = ctx.sessions[*index1];
  const SessionInfo &info2 = ctx.sessions[*index2];

#if 1
  // temporary solution: do not penalize badSignatures and badAdditionalMods until new mods implementation is done
  int bad1 = (info1.badActualVersion || info1.badRequiredVersion || info1.badMod) ? 2 : 0;
  int bad2 = (info2.badActualVersion || info2.badRequiredVersion || info2.badMod) ? 2 : 0;
#else
  int bad1 = (info1.badActualVersion || info1.badRequiredVersion || info1.badMod || info1.badSignatures) ? 2 : 0;
  bad1 += info1.badAdditionalMods ? 1 : 0;
  int bad2 = (info2.badActualVersion || info2.badRequiredVersion || info2.badMod || info2.badSignatures) ? 2 : 0;
  bad2 += info2.badAdditionalMods ? 1 : 0;
#endif
  int diff = bad1 - bad2;
  if (diff != 0) return diff;

  int value = 0;
  switch (ctx.column)
  {
    case SCServer:
      value = CompareUTFStrings(info1.name, info2.name);
      break;
    case SCGameType:
      value = stricmp(info1.gameType, info2.gameType);
      break;
    case SCMission: //column contains version now
      //value = CompareUTFStrings(info1.mission, info2.mission);
      value = info1.actualVersion*1000000+info1.actualBuildNo - (info2.actualVersion*1000000+info2.actualBuildNo);
      break;
    case SCState:
      value = info1.serverState - info2.serverState;
      break;
    case SCFriends:
      value = info1.friends - info2.friends;
      break;
    case SCPlayers:
      value = info1.playersPublic - info2.playersPublic;
      break;
    case SCPing:
      value = info1.ping - info2.ping;
      break;
    case SCTimeOfDay:
      value = stricmp(info1.timeOfDay, info2.timeOfDay);
      break;
    case SCHive:
      value = info1.privateHive - info2.privateHive;
      break;
    case SCMode:
      value = info1.disable3rdPerson - info2.disable3rdPerson;
      break;
  }
  if (ctx.ascending) return value;
  else return -value;
}

void CSessionsContainer::Sort(SortColumn column, bool ascending, bool keepSelection)
{
  // remember actually selected row
  RString selGUID;
  int sel = SelectedSession();
  if (sel >= 0 && sel < NSessions()) selGUID = GetSessionID(sel);

  // sort list
  CmpSessionsContext ctx(_sessions);
  ctx.column = column;
  ctx.ascending = ascending;
  QSort(_indices.Data(), _indices.Size(), ctx, CmpSessions);

  // select original selected row
  if (keepSelection)
  {
    sel = 0;
    for (int i=0; i<NSessions(); i++)
      if (GetSessionID(i) == selGUID)
      {
        sel = i;
        break;
      }
      SelectSession(sel);
  }
}

C2DSessions::C2DSessions(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CListBox(parent, idc, cls), CSessionsContainer(cls)
{
  _star = GlobLoadTextureUI(cls >> "star");

  ParamEntryVal array = cls >> "columns";
  int n = array.GetSize();
  saturateMin(n, _columns);
  for (int i=0; i<n; i++) _colWidths[i] = array[i];
  for (int i=n; i<_columns; i++) _colWidths[i] = 0;
}

/*!
\patch 5111 Date 1/2/2007 by Jirka
- Improved: Multiplayer screens layout
\patch 5124 Date 1/26/2007 by Jirka
- Fixed: MP session list - Server state not shown for incompatible sessions
\patch 5137 Date 3/7/2007 by Ondra
- Changed: Less strict color coding for ping values.
*/

void C2DSessions::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top,
  const Rect2DFloat &rect, float widthDiff
)
{
  const SessionInfo &session = GetSession(i);
  bool host = i >= _indices.Size();

  DWORD time = session.lastTime;
  float age = time == 0xffffffff ? 0 : 0.001 * (GetTickCount() - time);
  if (age > 10) alpha *= (15.0 - age) * (1.0 / 5.0);

  PackedColor ftColor = ModAlpha(GetTextColor(), alpha);
  PackedColor selColor = ModAlpha(GetActiveColor(), alpha);
  PackedColor color;

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = SCALED(TEXT_BORDER);
  bool textures = (_style & LB_TEXTURES) != 0;

  if (selected && _showSelected)
  {
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
    float xx = rect.x * w;
    float ww = rect.w * w;
    if (textures)
    {
      const int borderWidth = 2;
      xx += borderWidth;
      ww -= 2 * borderWidth;
    }
    GLOB_ENGINE->Draw2D
    (
      mip, ftColor, Rect2DPixel(xx, top * h, ww, SCALED(_rowHeight) * h),
      Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
    );
    color = selColor;
  }
  else
  {
    color = ftColor;
  }
  float left = _x + border;
  
  // icon
  Texture *texture = NULL;
  PackedColor col = color;
  if (session.badActualVersion || session.badRequiredVersion)
  {
    texture = _version;
    col = PackedColor(Color(1, 0, 0, alpha));
  }
  else if (session.badSignatures || session.badMod) 
  {
    texture = _addons;
  }
  else if (session.lock) texture = _lockText;
  else if (session.password) texture = _password;
  else if (session.badAdditionalMods)
  {
    texture = _mods;
  }
  else texture = _none;
  float textWidth = 0;
  if (texture)
  {
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    float textWidth = SCALED(_rowHeight) *
      (texture->AWidth() * h) / (texture->AHeight() * w);
    GEngine->Draw2D
    (
      mip, PackedWhite,
      Rect2DPixel(CX(rect.x), CY(top), CW(textWidth), CH(SCALED(_rowHeight))),
      Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
    );
    left += textWidth;
  }

  float t = top + 0.5 * (SCALED(_rowHeight - _size));
  float rest = rect.w + widthDiff; //add the width of possible right slidebar
  Rect2DFloat clipRect;
  clipRect.y = rect.y;
  clipRect.h = rect.h;
  RString text;
  
  float column;
  int c = 0;

  // session name
  column = _colWidths[c++] * rest - textWidth - 2*border;
  text = session.name;
  clipRect.x = left;
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column - border;
  if (host) return;

  // hive / public / private
  column = _colWidths[c++] * rest;
  text = session.privateHive ? "Private" : "Public";
  clipRect.x = left;
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column;

  // mode (regular,hardcore)
  column = _colWidths[c++] * rest;
  text = session.disable3rdPerson ? "Hardcore" : "Regular"; //TODO: localize
  clipRect.x = left;
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column;

  // server version and build
  column = _colWidths[c++] * rest;
  text = Format("%d.%d", session.actualVersion, session.actualBuildNo);
  clipRect.x = left;     
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column;

  // time of day
  column = _colWidths[c++] * rest;
  text = (session.environmentTimeMul > 0.0f && session.environmentTimeMul != 1.0f) ? Format("%sx", session.timeOfDay.Data()) : session.timeOfDay;
  clipRect.x = left;
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column;

  // friends
  column = _colWidths[c++] * rest;
  text = Format("%d", session.friends);
  clipRect.x = left;
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column;

  // players
  column = _colWidths[c++] * rest;
  if (session.slotsPublic)
    text = Format("%d/%d", session.playersPublic, session.slotsPublic);
  else
    text = Format("%d", session.playersPublic);
  clipRect.x = left;
  clipRect.w = column - border;
  GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, color, text, _shadow);
  left += column;

  // ping
  column = _colWidths[c++] * rest;
  col = color;

#ifdef _XBOX
  int nStars = 0;
  if (session.ping < 0) text = "?";
  else if (session.ping < PingGood) {text = RString(); nStars = 3;}
  else if (session.ping < PingPoor) {text = RString(); nStars = 2;}
  else text = "!";
#else
  if (session.ping < 0 || session.ping == INT_MAX)
  {
    text = "?";
    col = ModAlpha(_colorPingUnknown, alpha);
  }
  else
  {
    text = Format("%d", session.ping);
    if (session.ping < PingGood) col = ModAlpha(_colorPingGood, alpha);
    else if (session.ping < PingPoor) col = ModAlpha(_colorPingPoor, alpha);
    else col = ModAlpha(_colorPingBad, alpha);
  }
#endif

  clipRect.x = left;
  clipRect.w = column - border;
  if (text.GetLength() > 0)
    GEngine->DrawText(Point2DFloat(clipRect.x + border, t), SCALED(_size), clipRect, _font, col, text, _shadow);

#ifdef _XBOX
  if (nStars > 0)
  {
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(_star, 0, 0);
    Rect2DPixel rect;
    rect.y = CY(top);
    rect.h = CH(SCALED(_rowHeight));
    rect.x = CX(left + border);
    rect.w = CW(0.75f * SCALED(_rowHeight));
    for (int i=0; i<nStars; i++)
    {
      GEngine->Draw2D(mip, color, rect, Rect2DPixel(clipRect.x * w, clipRect.y * h, clipRect.w * w, clipRect.h * h));
      rect.x += rect.w;
    }
  }
#endif
  left += column;
}

#if _ENABLE_GAMESPY 
struct UpdateSBCallback : public INetworkUpdateCallback
{
  ServerBrowser _serverBrowser;

  UpdateSBCallback(ServerBrowser serverBrowser) : _serverBrowser(serverBrowser) {}
  virtual void operator ()()
  {
    if (_serverBrowser)
    {
      SBError error = ServerBrowserThink(_serverBrowser);
      (void)error;
    }
  }
};
#endif

/*!
\patch 5124 Date 1/26/2007 by Jirka
- Fixed: MP session list - country is shown for all sessions now
\patch 5126 Date 2/6/2007 by Bebul
- Fixed: MP Voice over Net on LAN sessions was not working sometimes
*/
DisplayMultiplayer::DisplayMultiplayer(ControlsContainer *parent)
  : Display(parent)
{
  _enableSimulation = false;
  _sessions = NULL;
  _portRemote = _portLocal = GetNetworkPort();

#if _ENABLE_STEAM_MATCHMAKING || _ENABLE_STEAM
  _source = BSInternet;
  _activeTab = MPTabInternet;
#else
  _source = BSLAN;
  _activeTab = MPTabLAN;
#endif

  Load("RscDisplayMultiplayer");
  LoadParams();

  _refresh = true;

#if _ENABLE_STEAM_MATCHMAKING
  _steamServerBrowser = new SteamServerBrowser(_sessions);
#elif _ENABLE_GAMESPY
  _serverBrowser = NULL;
  _listUpdateRunning = Completed;
  _selected = NULL;

  {
#if _ENABLE_MP
    // check that the game's back-end is available
    Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
    GSIStartAvailableCheck(GetGameName());
    GSIACResult result;
    while ((result = GSIAvailableCheckThink()) == GSIACWaiting)
    {
      msleep(5);
      ProgressRefresh();
    }
    switch (result)
    {
    case GSIACAvailable:
      // the game's back-end services are available
      _serverBrowser = ServerBrowserNew(GetGameName(), GetGameName(), GetSecretKey(),
        0, MaxUpdates, QVERSION_QR2, SBFalse, SBCallback, this);
      _serverBrowserUpdate = new UpdateSBCallback(_serverBrowser);
      if (bindIPAddress)
      {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = bindIPAddress;        
        char bindIPstr[64];
        sprintf( bindIPstr, "%u.%u.%u.%u", (unsigned)IP4(addr),(unsigned)IP3(addr),(unsigned)IP2(addr),(unsigned)IP1(addr) );
        ServerBrowserLANSetLocalAddr(_serverBrowser, bindIPstr);
      }
      GetNetworkManager().RegisterUpdateCallback(_serverBrowserUpdate);

      void RegisterQR2Keys();
      RegisterQR2Keys();
      break;
    case GSIACUnavailable:
      // the game's back-end services are unavailable
      CreateMsgBox(MB_BUTTON_OK, Format(LocalizeString(IDS_MSG_MP_GAMESPY_NOT_AVAIL), APP_NAME));
      break;
    case GSIACTemporarilyUnavailable:
      // the game's back-end services are temporarily unavailable
      CreateMsgBox(MB_BUTTON_OK, Format(LocalizeString(IDS_MSG_MP_GAMESPY_TEMP_NOT_AVAIL), APP_NAME));
      break;
    }
    bool IsPatchCheck();
    if (IsPatchCheck()) CheckPatchAvailable();
    ProgressFinish(p);
#endif // _ENABLE_MP

    if (_source == BSInternet && !_serverBrowser) _source = BSLAN;
  }
#endif // _ENABLE_GAMESPY

  _sort = SCPing;
  _ascending = true;
  _sessionSelected = false;
  ParamEntryVal cls = Pars >> "RscDisplayMultiplayer";
  _sortUp = cls >> "sortUp";
  _sortDown = cls >> "sortDown";

  _showBattlEye = GlobLoadTextureUI(cls >> "showBattlEye");
  _showNoBattlEye = GlobLoadTextureUI(cls >> "showNoBattlEye");
  _hideBattlEye = GlobLoadTextureUI(cls >> "hideBattlEye");

  _showPasswored = GlobLoadTextureUI(cls >> "showPassworded");
  _hidePassworded = GlobLoadTextureUI(cls >> "hidePassworded");

  _showFull = GlobLoadTextureUI(cls >> "showFull");
  _hideFull = GlobLoadTextureUI(cls >> "hideFull");

  _showExpansions = GlobLoadTextureUI(cls >> "showExpansions");
  _hideExpansions = GlobLoadTextureUI(cls >> "hideExpansions");

  _colorPingUnknown = GetPackedColor(cls >> "colorPingUnknown");
  _colorPingGood = GetPackedColor(cls >> "colorPingGood");
  _colorPingPoor = GetPackedColor(cls >> "colorPingPoor");
  _colorPingBad = GetPackedColor(cls >> "colorPingBad");
  _colorVersionGood = GetPackedColor(cls >> "colorVersionGood");
  _colorVersionBad = GetPackedColor(cls >> "colorVersionBad");

  BrowsingSource source = _source;
  if (source == BSRemote) source = BSLAN;
  
  UpdateAddress("", GetNetworkPort());

  SetSource(source, false);

#if _ENABLE_STEAM_MATCHMAKING
  _sessions->_sessions.Resize(0);
  _sessions->_indices.Resize(0);
  _sessions->SelectSession(0);
  _sessionSelected = false;

  _steamServerBrowser->SetFilter(_filter);
  if (_source == BSInternet) _steamServerBrowser->RefreshInternetServers(_activeTab);
  else if (_source == BSLAN) _steamServerBrowser->RefreshLANServers();
  else UpdateSessions();

  _refresh = false;
#elif _ENABLE_GAMESPY
  // need to be called to obtain my public IP address
  UpdateServerList();
  if (_source != BSInternet) UpdateSessions();
#else
  UpdateSessions();
#endif
      
#if _ENABLE_STEAM_MATCHMAKING
  _nextRefreshDetailsTime = ::GetTickCount() + FirstRefresh;
#endif
  OnSessionChanged(true);


#if _ENABLE_GAMESPY || _ENABLE_STEAM_MATCHMAKING
  if (_source == BSInternet)
  {
    ShowFilterIcons(true);
    SetTab(MPTabInternet);
  }
#endif
  SetProgress(0);

  UpdateIcons();
  UpdateFilter();
}

DisplayMultiplayer::~DisplayMultiplayer()
{
#if _ENABLE_STEAM_MATCHMAKING
  // no clean-up needed
#elif _ENABLE_GAMESPY
  if (_serverBrowserUpdate)
    GetNetworkManager().UnregisterUpdateCallback(_serverBrowserUpdate);
  if (_serverBrowser)
    ServerBrowserFree(_serverBrowser);
#endif
}

Control *DisplayMultiplayer::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_MULTI_SESSIONS:
    {
      Control *ctrl = NULL;
      if (type == CT_LISTBOX)
      {
        C2DSessions *sessions = new C2DSessions(this, idc, cls);
        _sessions = sessions;
        ctrl = sessions;
      }
      else
      {
        Fail("Unsupported control type");
      }
      return ctrl;
    }
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplayMultiplayer::OnSimulateBasic()
{
#if _ENABLE_STEAM_MATCHMAKING
  if (_refresh)
  {
    _sessions->_sessions.Resize(0);
    _sessions->_indices.Resize(0);
    _sessions->SelectSession(0);
    _sessionSelected = false;

    if (_source == BSInternet) _steamServerBrowser->RefreshInternetServers(_activeTab);
    else if (_source == BSLAN) _steamServerBrowser->RefreshLANServers();
    else UpdateSessions();

    _refresh = false;
  }
  else if (_source == BSRemote)
  {
    UpdateSessions();
  }
#elif _ENABLE_GAMESPY
  UpdateServerList();
  if (_listUpdateRunning==StageDone)
  {
    if (!StageUpdateServerList(++_listUpdateStage))
    {
      SetProgress(100);
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      _listUpdateRunning = Completed;
      PingGeoLocalize pingGeoLoc;
      for (int i=0; i<_sessions->_sessions.Size(); i++)
      {
        const SessionInfo &info = _sessions->_sessions[i];
        if (info.ping<INT_MAX && info.ping>0 && info.longitude<INT_MAX && info.latitude<INT_MAX)
        {
          pingGeoLoc.Add(info.latitude,info.longitude,info.ping,info.guid);
        }
      }
      pingGeoLoc.Best(_latitude,_longitude);
      SaveParams();
    }
  }


  if (_source != BSInternet) UpdateSessions();
#else
  UpdateSessions();
#endif

  OnSessionChanged(false);
}

void DisplayMultiplayer::OnSimulateBackground(EntityAI *vehicle)
{
  OnSimulateBasic();
}

void DisplayMultiplayer::OnSimulate(EntityAI *vehicle)
{
  if (!GInput.changeServer.IsEmpty())
  {
    if (!GetNetworkManager().IsServer() && !GetNetworkManager().IsClient())
    {
      //JoinSession(STStandard, GInput.changeServer, GInput.changeServerPassword, NULL);
      Exit(IDC_CANCEL);
    }
    else GInput.changeServer = RString();
  }

  OnSimulateBasic();
  Display::OnSimulate(vehicle);
}

#if _USE_BATTL_EYE_SERVER
void DisplayMultiplayer::CreateBELicenceMsgBox()
{
  _msgBox = CreateOnBELicenceNotAgreed(this, "", "");
}
#endif //_USE_BATTL_EYE_SERVER

#ifndef _XBOX
CDP_DECL void __cdecl CDPCreateDisplayIPAddress(ControlsContainer *parent)
{
  SECUROM_MARKER_SECURITY_ON(4)
  parent->CreateChild(new DisplayIPAddress(parent));

  SECUROM_MARKER_SECURITY_OFF(4)
}
#endif

void DisplayMultiplayer::SetTab(MPTabs tab)
{
  static int MPTabIDC[MP_TABS_COUNT] = {IDC_MULTI_TAB_INTERNET, IDC_MULTI_TAB_FAVORITE, IDC_MULTI_TAB_HISTORY, IDC_MULTI_TAB_FRIENDS, IDC_MULTI_TAB_LAN, IDC_MULTI_TAB_REMOTE};
  if (tab==MP_TABS_COUNT) tab = _activeTab; //refresh
  else _activeTab = tab;

  for (int i=0; i<MP_TABS_COUNT; i++)
  {
    CTabButton *tabbutton = dynamic_cast<CTabButton *>( GetTextContainer(GetCtrl(MPTabIDC[i])) );
    if (tabbutton)
    {
      tabbutton->SetActive(i==toInt(tab));
    }
  }
}

/*!
\patch 1.79 Date 7/24/2002 by Jirka
- Fixed: Multiplayer session list: bad title appears when open the screen and Internet sessions was selected last
*/
void DisplayMultiplayer::SetSource(BrowsingSource source, bool save)
{
  _source = source;
  if (save) SaveParams();

  bool internet = false;
#if _ENABLE_STEAM_MATCHMAKING
  internet = (source == BSInternet);
#endif

  if (GetCtrl(IDC_MULTI_GAMESPY)) GetCtrl(IDC_MULTI_GAMESPY)->ShowCtrl(internet);
  if (GetCtrl(IDC_MULTI_PROGRESS)) GetCtrl(IDC_MULTI_PROGRESS)->ShowCtrl(internet);
  if (GetCtrl(IDC_MULTI_REFRESH)) GetCtrl(IDC_MULTI_REFRESH)->ShowCtrl(true);
  if (GetCtrl(IDC_MULTI_FILTER)) GetCtrl(IDC_MULTI_FILTER)->ShowCtrl(internet);
  if (GetCtrl(IDC_MULTI_PORT)) GetCtrl(IDC_MULTI_PORT)->ShowCtrl(!internet);

#if _ENABLE_STEAM_MATCHMAKING
  if (internet)
  {
    IControl *ctrl = GetCtrl(IDC_MULTI_TITLE);
    ITextContainer *title = GetTextContainer(ctrl);
    if (title)
    {
      title->SetText(LocalizeString(IDS_MULTI_TITLE_INTERNET));
    }
  }
  _refresh = true;
#endif
}

void DisplayMultiplayer::UpdateIcons()
{
  IControl *ctrl = GetCtrl(IDC_MULTI_SERVER_ICON);
  ITextContainer *icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCServer)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_GAMETYPE_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCGameType)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_MISSION_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCMission)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }
  
  ctrl = GetCtrl(IDC_MULTI_STATE_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCState)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_FRIENDS_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCFriends)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_TIMEOFDAY_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCTimeOfDay)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_HIVE_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCHive)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_MODE_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCMode)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }

  ctrl = GetCtrl(IDC_MULTI_PLAYERS_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCPlayers)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }
  
  ctrl = GetCtrl(IDC_MULTI_PING_ICON);
  icon = GetTextContainer(ctrl);
  if (icon)
  {
    if (_sort == SCPing)
    {
      ctrl->ShowCtrl(true);
      if (_ascending) icon->SetText(_sortUp);
      else icon->SetText(_sortDown);
    }
    else ctrl->ShowCtrl(false);
  }
}

void DisplayMultiplayer::UpdateFilter()
{
  IControl *ctrl = GetCtrl(IDC_MULTI_TEXTS_FILTER);
  if (ctrl && ctrl->GetType() == CT_STATIC)
  {
    CStatic *text = static_cast<CStatic *>(ctrl);
    RString filter = _filter.serverName;
    if (!_filter.missionName.IsEmpty())
    {
      if (!filter.IsEmpty()) filter = filter+RString("holahou");
      filter = filter + _filter.missionName;
    }
    text->SetText(filter);
  }

  ctrl = GetCtrl(IDC_MULTI_PASSWORDED_FILTER);
  if (ctrl && ctrl->GetType() == CT_STATIC)
  {
    CStatic *text = static_cast<CStatic *>(ctrl);
    text->SetTexture(_filter.passwordedServers ? _showPasswored : _hidePassworded);
  }

#if _USE_BATTL_EYE_CLIENT
  ctrl = GetCtrl(IDC_MULTI_BATTLEYE_FILTER);
  if (ctrl && ctrl->GetType() == CT_STATIC)
  {
    CStatic *text = static_cast<CStatic *>(ctrl);
    switch (_filter.battleyeRequired)
    {
      case SessionFilter::BEFLT_NONE:       text->SetTexture(_hideBattlEye); break;
      case SessionFilter::BEFLT_WITHOUT_BE: text->SetTexture(_showNoBattlEye); break;
      case SessionFilter::BEFLT_REQUIRED:   text->SetTexture(_showBattlEye); break;
    }
  }
#endif

  ctrl = GetCtrl(IDC_MULTI_FULL_FILTER);
  if (ctrl && ctrl->GetType() == CT_STATIC)
  {
    CStatic *text = static_cast<CStatic *>(ctrl);
    text->SetTexture(_filter.fullServers ? _showFull : _hideFull);
  }

  ctrl = GetCtrl(IDC_MULTI_EXPANSIONS_FILTER);
  if (ctrl && ctrl->GetType() == CT_STATIC)
  {
    CStatic *text = static_cast<CStatic *>(ctrl);
    text->SetTexture(_filter.expansionsServers ? _showExpansions : _hideExpansions);
  }

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_MULTI_SERVER_FILTER));
  if (text)
    if (_filter.serverName.GetLength() > 0)
      text->SetText(Format("(%s)", (const char *)_filter.serverName));
    else
      text->SetText(RString());

  text = GetTextContainer(GetCtrl(IDC_MULTI_TYPE_FILTER));
  if (text)
  {
    RString typeName;
    if (_filter.missionType.GetLength() > 0)
    {
      ConstParamEntryPtr entry = (Pars >> "CfgMPGameTypes").FindEntry(_filter.missionType);
      if (entry) typeName = (*entry) >> "shortcut";
      else typeName = Pars >> "CfgMPGameTypes" >> "Unknown" >> "shortcut";
    }
    if (typeName.GetLength() > 0)
      text->SetText(Format("(%s)", (const char *)typeName));
    else
      text->SetText(RString());
  }

  text = GetTextContainer(GetCtrl(IDC_MULTI_MISSION_FILTER));
  if (text)
    if (_filter.missionName.GetLength() > 0)
      text->SetText(Format("(%s)", (const char *)_filter.missionName));
    else
      text->SetText(RString());

  text = GetTextContainer(GetCtrl(IDC_MULTI_PLAYERS_FILTER));
  if (text)
    if (_filter.minPlayers > 0 || _filter.maxPlayers > 0)
      text->SetText(Format("%d..%d", _filter.minPlayers, _filter.maxPlayers));
    else
      text->SetText(RString());

  text = GetTextContainer(GetCtrl(IDC_MULTI_PING_FILTER));
  if (text)
    if (_filter.maxPing > 0)
      text->SetText(Format("<%d", _filter.maxPing));
    else
      text->SetText(RString());
  _steamServerBrowser->SetFilter(_filter);
}

void DisplayMultiplayer::SetProgress(float progress)
{
  CProgressBar *ctrl = dynamic_cast<CProgressBar *>(GetCtrl(IDC_MULTI_PROGRESS));
  if (ctrl) ctrl->SetPos(progress);
}

void DisplayMultiplayer::JoinSession(SessionType sessionType, RString guid, RString password, const SessionInfo *session)
{
#if _ENABLE_GAMESPY
  if (_serverBrowser)
  {
    const char *ptr = guid;
    const char *p = strrchr(ptr, ':');
    if (p)
    {
      RString ip(guid, p - ptr);
      int port = atoi(p + 1);
      SBServer server = ServerBrowserGetServerByIP(_serverBrowser, ip, port);
      if (server)
      {
        // guid is an query address only - we need to obtain the game address
        // see NAT Negotiation SDK Help
        if (SBServerHasPrivateAddress(server) &&
          SBServerGetPublicInetAddress(server) == GetMyPublicIPAddr(_serverBrowser))
        {
          // we are behind the same NAT
          ip = SBServerGetPrivateAddress(server);
          port = SBServerGetPrivateQueryPort(server);
          guid = GetNetworkManager().IPToGUID(ip, port);
        }
        else if (!SBServerHasPrivateAddress(server) && SBServerDirectConnect(server))
        {
          // direct connection through the public address is possible
          ip = SBServerGetPublicAddress(server);
          port = SBServerGetPublicQueryPort(server);
          guid = GetNetworkManager().IPToGUID(ip, port);
        }
        else
        {
          // NAT Negotiation
          int rand32();
          unsigned int cookie = rand32();
          ip = SBServerGetPublicAddress(server);
          port = SBServerGetPublicQueryPort(server);
          // store ip, query port and cookie to the guid (to enable negotiation in NetClient::Init)
          guid = GetNetworkManager().IPToGUID(ip, port) + Format("#%ud", cookie);
          // tell cookie to the server
          ServerBrowserSendNatNegotiateCookieToServer(_serverBrowser, ip, port, cookie);
        }
      }
    }
  }
#endif
  ConnectResult result = GetNetworkManager().JoinSession(sessionType, guid, password);
  GetNetworkManager().CustomFilesProgressClear();
  switch (result)
  {
  case CROK:
#if _ENABLE_GAMESPY
    // set the public IP Address
    if (_serverBrowser) GetNetworkManager().SetPublicAddress(GetMyPublicIPAddr(_serverBrowser));
#   if BETA_TESTERS_DEBUG_VON
    {
      unsigned int ip_addr = GetMyPublicIPAddr(_serverBrowser);
      RptF("VoNLog,JoinSession: publicIP = %d.%d.%d.%d",
        ip_addr&0xff, (ip_addr>>8)&0xff, (ip_addr>>16)&0xff, (ip_addr>>24)&0xff
        );
    }
#   endif
#endif
    CreateChild(new DisplayClient(this));
#if _ENABLE_STEAM_MATCHMAKING
    // Add to History list
    if (session) SteamMatchmaking()->AddFavoriteGame(SteamUtils()->GetAppID(), session->smIP, session->smPort, session->smQueryPort, k_unFavoriteFlagHistory, time(NULL));
#endif
    break;
  case CRPassword:
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
    break;
  case CRVersion:
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
    break;
  case CRError:
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
    break;
  case CRSessionFull:
    CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
    break;
  }
}

void DisplayMultiplayer::CreateSession(SessionType sessionType, RString name, RString password, int maxPlayers, bool isPrivate, int port, MPSessionType mpType, ConstParamEntryPtr *cfg)
{
  GetNetworkManager().CreateSession(sessionType, name, password, maxPlayers, isPrivate, port, false, RString(), 0);
  if (GetNetworkManager().IsServer())
  {
#if _ENABLE_GAMESPY
    // set the public IP Address
    if (_serverBrowser) 
    {
      GetNetworkManager().SetPublicAddress(GetMyPublicIPAddr(_serverBrowser));
#     if BETA_TESTERS_DEBUG_VON
      {
        unsigned int ip_addr = GetMyPublicIPAddr(_serverBrowser);
        RptF("VoNLog,CreateSession: publicIP = %d.%d.%d.%d",
          ip_addr&0xff, (ip_addr>>8)&0xff, (ip_addr>>16)&0xff, (ip_addr>>24)&0xff
          );
      }
#     endif
    }
#endif
    switch (mpType)
    {
    case MPTMultiplayer:
      CreateChild(new DisplayServer(this));
      break;
#if _ENABLE_MISSION_CONFIG
    case MPTHostMission:
      if (cfg) CreateChild(new DisplayServerHostMission(this, *cfg));
      break;
#endif
  }
}
}

void DisplayMultiplayer::ShowFilterIcons(bool show)
{
  static int filterIcons[] = {IDC_MULTI_TEXTS_FILTER,
                              IDC_MULTI_PASSWORDED_FILTER,IDC_MULTI_BATTLEYE_FILTER,IDC_MULTI_FULL_FILTER,
                              IDC_MULTI_EXPANSIONS_FILTER,IDC_MULTI_SERVER_FILTER,IDC_MULTI_TYPE_FILTER,
                              IDC_MULTI_MISSION_FILTER,IDC_MULTI_PLAYERS_FILTER,IDC_MULTI_PING_FILTER, IDC_CURRENT_FILTER};
  for (int i=0; i<sizeof(filterIcons)/sizeof(*filterIcons); i++)
  {
    IControl *ctrl = GetCtrl(filterIcons[i]);
    if (ctrl && ctrl->GetType() == CT_STATIC) ctrl->ShowCtrl(show);
  }
}

void DisplayMultiplayer::UpdateSessionFavorite(const SessionInfo *session)
{
#if _ENABLE_STEAM_MATCHMAKING
  IControl *ctrl = GetCtrl(IDC_MULTI_FAVORITE);
  ITextContainer *button = GetTextContainer(ctrl);
  if (!button) return;
  ctrl->ShowCtrl(session!=nullptr);
  if (!session) return;

  for (int i=0; i<SteamMatchmaking()->GetFavoriteGameCount(); i++)
  {
    AppId_t appId;
    uint32 ip, lastTime, flag;
    uint16 port, queryPort;
    SteamMatchmaking()->GetFavoriteGame(i, &appId, &ip, &port, &queryPort, &flag, &lastTime);
    if ((flag & k_unFavoriteFlagFavorite) //&& (appId==221100 || appId==223350) <-- do not test appId as it is 0 after game is restarted for some reason
      && ip==session->smIP && (port==session->smPort || queryPort==session->smQueryPort)) //for some reason smPort is 27016? often
    { // is favorite
      session->favorite = true;
      button->SetText(LocalizeString(IDS_MULTI_REMOVE_FAVORITE));
      return;
    }
  }
  // is NOT favorite
  session->favorite = false;
  button->SetText(LocalizeString(IDS_MULTI_ADD_FAVORITE));
#endif
}

void DisplayMultiplayer::OnButtonClicked(int idc)
{
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_MULTIPLAYER)
    Exit(idc);
#endif
  switch (idc)
  {
#ifndef _XBOX
#if _ENABLE_STEAM_MATCHMAKING
    case IDC_MULTI_TAB_FAVORITE:
       SetTab(MPTabFavorite);
       goto DMUpdateServers;
    case IDC_MULTI_TAB_HISTORY:
       SetTab(MPTabHistory);
       goto DMUpdateServers;
    case IDC_MULTI_TAB_FRIENDS:
       SetTab(MPTabFriends);
       goto DMUpdateServers;
    case IDC_MULTI_TAB_INTERNET:
       SetTab(MPTabInternet);
       goto DMUpdateServers;
DMUpdateServers:
      if (!_steamServerBrowser)
		  {
				UpdateAddress("", _portLocal);
			}
			else 
			{
				ShowFilterIcons(true);
				SetSource(BSInternet, true);
  	  }
      break;
    case IDC_MULTI_FAVORITE:
      if (_sessionSelected)
      {
        const SessionInfo *session = NULL;
        if (_sessions)
        {
          int sel = _sessions->SelectedSession();
          if (sel >= 0 && sel < _sessions->NSessionsNoHost()) session = &_sessions->GetSession(sel);
        }
        if (session)
        {
          if (session->favorite) 
          { // selected server is already in Favorite list => remove from Favorite list
            SteamMatchmaking()->RemoveFavoriteGame(SteamUtils()->GetAppID(), session->smIP, session->smPort, session->smQueryPort, k_unFavoriteFlagFavorite);
            if (_activeTab==MPTabFavorite) _refresh = true;
          }
          else 
          { // add to Favorite list
            SteamMatchmaking()->AddFavoriteGame(SteamUtils()->GetAppID(), session->smIP, session->smPort, session->smQueryPort, k_unFavoriteFlagFavorite, time(NULL));
          }
          UpdateSessionFavorite(session);
        }
      }
      break;
#endif
    case IDC_MULTI_TAB_LAN:
      SetTab(MPTabLAN);
      UpdateAddress("", _portLocal);
      break;
    case IDC_MULTI_TAB_REMOTE:
    case IDC_MULTI_REMOTE:
      SetTab(MPTabRemote);
      CDPCreateDisplayIPAddress(this);
      break;
    case IDC_MULTI_PORT:
      CreateChild(new DisplayPort(this, GetPort()));
      break;
    case IDC_MULTI_FILTER:
      CreateChild(new DisplaySessionFilter(this, _filter));
      break;
#endif // ndef _XBOX
    case IDC_MULTI_NEW:
#if defined _XBOX && _XBOX_VER >= 200
      //GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#endif
      // create session
#if _XBOX_SECURE
      CreateSession(STSystemLink, RString(), RString(), MAX_PLAYERS, false, GetPort());
#else
#if _VBS2 // skip host settings screen
      {
        RString playerName = Glob.header.playerHandle;
        RString GetDefaultSessionName(RString playerName);
        CreateSession(
          STSystemLink, 
          GetDefaultSessionName(playerName), 
          RString(), 
          MAX_PLAYERS, 
          false, 
          GetNetworkPort()
        );
      }
#else
      CreateChild(new DisplayHostSettings(this));
#endif
#endif
      break;
    case IDC_MULTI_JOIN:
    case IDC_OK:
      {
        if (_sessions)
        {
          int sel = _sessions->SelectedSession();
#if DISABLE_HOST_SESSION
          if (sel >= 0 && sel < _sessions->NSessionsNoHost()) // avoid HOST
#endif
          {
            // join session
            const SessionInfo &session = _sessions->GetSession(sel);

#if _USE_BATTL_EYE_SERVER
            // if server is using battleye and user hasn't agreed to license yet, show the dialog and pospone the join
            if (!Glob.config.beLicenceAgreed && session.battleEye != 0)
            {              
              CreateBELicenceMsgBox();
              break;
            }
#endif //_USE_BATTL_EYE_SERVER

            if (session.badRequiredVersion) break; //do not allow to connect to server with badRequiredVersion

#if _XBOX_SECURE
            int playerRating = 0;
            // Ranking is handled automatically for Ranked Matches
            ConnectResult result = GetNetworkManager().JoinSession(STSystemLink, session.addr, session.kid, session.key, session.port, false, playerRating);

            switch (result)
            {
            case CROK:
              CreateChild(new DisplayClient(this));
              break;
            case CRPassword:
              CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_PASSWORD));
              break;
            case CRVersion:
              CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_VERSION));
              break;
            case CRError:
              CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_CONNECT_ERROR));
              break;
            case CRSessionFull:
              CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_SESSION_FULL));
              break;
            }
#else
            RString guid = session.guid;

            if (session.password)
            {
              // session requires password, ask for it
              CreateChild(new DisplayPassword(this, guid, session.name));
              break;
            }

            JoinSession(STSystemLink, guid, RString(), &session);
#endif
          }
        }
      }
      break;
    case IDC_CANCEL:
      {
        ControlObjectContainerAnim *ctrl =
          dynamic_cast<ControlObjectContainerAnim *>(GetCtrl(IDC_MULTI_NOTEBOOK));
        if (ctrl)
        {
          _exitWhenClose = idc;
          ctrl->Close();
        }
        else Exit(idc);
      }
      break;
    case IDC_MULTI_SERVER_COLUMN:
      if (_sort == SCServer)
        _ascending = !_ascending;
      else
      {
        _sort = SCServer;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_GAMETYPE_COLUMN:
      if (_sort == SCGameType)
        _ascending = !_ascending;
      else
      {
        _sort = SCGameType;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_MISSION_COLUMN:
      if (_sort == SCMission)
        _ascending = !_ascending;
      else
      {
        _sort = SCMission;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_STATE_COLUMN:
      if (_sort == SCState)
        _ascending = !_ascending;
      else
      {
        _sort = SCState;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_FRIENDS_COLUMN:
      if (_sort == SCFriends)
        _ascending = !_ascending;
      else
      {
        _sort = SCFriends;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_TIMEOFDAY_COLUMN:
      if (_sort == SCTimeOfDay)
        _ascending = !_ascending;
      else
      {
        _sort = SCTimeOfDay;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_HIVE_COLUMN:
      if (_sort == SCHive)
        _ascending = !_ascending;
      else
      {
        _sort = SCHive;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_MODE_COLUMN:
      if (_sort == SCMode)
        _ascending = !_ascending;
      else
      {
        _sort = SCMode;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_PLAYERS_COLUMN:
      if (_sort == SCPlayers)
        _ascending = !_ascending;
      else
      {
        _sort = SCPlayers;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_PING_COLUMN:
      if (_sort == SCPing)
        _ascending = !_ascending;
      else
      {
        _sort = SCPing;
        _ascending = true;
      }
      _sessions->Sort(_sort, _ascending, _sessionSelected);
      UpdateIcons();
      break;
    case IDC_MULTI_REFRESH:
      _refresh = true;
      break;
    case IDC_MULTI_DETAILS:
      SetAlwaysShow(true);
      CreateChild(new DisplayMultiplayerDetails(this));
      break;
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

void DisplayMultiplayer::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_MULTI_SESSIONS)
  {
    _sessionSelected = true;
    OnSessionChanged(true);
  }
  else
    Display::OnLBSelChanged(ctrl, curSel);
}

void DisplayMultiplayer::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_MULTI_SESSIONS && curSel >= 0)
  {
    if (_sessions && curSel < _sessions->NSessionsNoHost())
      OnButtonClicked(IDC_MULTI_JOIN);
    else
      OnButtonClicked(IDC_MULTI_NEW);
  }
  else
    Display::OnLBDblClick(idc, curSel);
}

void DisplayMultiplayer::OnCtrlClosed(int idc)
{
  if (idc == IDC_MULTI_NOTEBOOK)
    Exit(_exitWhenClose);
  else
    Display::OnCtrlClosed(idc);
}

void DisplayMultiplayer::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
    case IDD_HOST_SETTINGS:
      if (exit == IDC_OK)
      {
        DisplayHostSettings *display = dynamic_cast<DisplayHostSettings *>(_child.GetRef());
        if (display)
        {
          RString name = display->GetName();
          RString password = display->GetPassword();
          int maxPlayers = display->GetMaxPlayers();
          bool isPrivate = display->IsPrivate();
          int port = display->GetPort();
          Display::OnChildDestroyed(idd, exit);
          CreateSession(STSystemLink, name, password, maxPlayers, isPrivate, port);
          return;
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
#ifndef _XBOX
    case IDD_PASSWORD:
      if (exit == IDC_OK)
      {
        DisplayPassword *display = dynamic_cast<DisplayPassword *>(_child.GetRef());
        if (display)
        {
          RString guid = display->GetGUID();
          RString password = display->GetPassword();
          Display::OnChildDestroyed(idd, exit);
          JoinSession(STSystemLink, guid, password, NULL);
          return;
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
    case IDD_MULTIPLAYER_DETAILS:
      Display::OnChildDestroyed(idd, exit);
      SetAlwaysShow(false);
      if (exit == IDC_MULTI_JOIN)
      {
        OnButtonClicked(IDC_MULTI_JOIN);
      }
      break;
    case IDD_IP_ADDRESS:
      if (exit == IDC_OK)
      {
        CEditContainer *editIP = GetEditContainer(_child->GetCtrl(IDC_IP_ADDRESS));
        CEditContainer *editPort = GetEditContainer(_child->GetCtrl(IDC_IP_PORT));
        if (editIP && editPort)
        {
          RString ip = editIP->GetText();
  
          if (ip.GetLength() > 0)
          {
            int port = atoi(editPort->GetText());
            if (port <= 0) port = GetNetworkPort();
            UpdateAddress(ip, port);

            GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
            ParamFile cfg;
            if (ParseUserParams(cfg, &globals))
            {
              cfg.Add("remoteIPAddress", ip);
              cfg.Add("remotePort", port);
              SaveUserParams(cfg);
            }
          }
          else
          {
            UpdateAddress("", _portLocal);
          }
        }
      }
      else 
      {
        SetTab();
        IControl *tab = GetCtrl(IDC_MULTI_TAB_REMOTE);
        if (tab) tab->OnKillFocus();
      }
      Display::OnChildDestroyed(idd, exit);
      break;
    case IDD_PORT:
      if (exit == IDC_OK)
      {
        CEditContainer *edit = GetEditContainer(_child->GetCtrl(IDC_PORT_PORT));
        if (edit)
        {
          int port = atoi(edit->GetText());
          if (port <= 0) port = GetNetworkPort();
          UpdateAddress(_ipAddress, port);
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
    case IDD_FILTER:
      if (exit == IDC_OK)
      {
        DisplaySessionFilter *display = dynamic_cast<DisplaySessionFilter *>(_child.GetRef());
        if (display)
        {
          _filter = display->GetFilter();
          SaveParams();
          UpdateFilter();
          _refresh = true;
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
#endif
    case IDD_SERVER:
    case IDD_CLIENT:
      GetNetworkManager().Close();
      Display::OnChildDestroyed(idd, exit);
#if _ENABLE_MAIN_MENU_TABS
      // support for main menu tabs
      if (exit >= IDC_MAIN_TAB_LOGIN)
        Exit(exit);
#endif
#if _ENABLE_GAMESPY || _ENABLE_STEAM
      if (_source != BSInternet)
      {
        UpdateAddress(_ipAddress, GetPort());
      }
#endif
      _refresh = true;
      break;
    case IDD_MSG_BATTLEYE_LICENCE:
      if (exit == IDC_OK)
        OnButtonClicked(IDC_MULTI_JOIN);
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

void DisplayMultiplayer::UpdateAddress(RString ipAddress, int port)
{
  _ipAddress = ipAddress;
  SetPort(port);
  bool lan = _ipAddress.GetLength() == 0;
  SetSource(lan ? BSLAN : BSRemote, false);
#if _ENABLE_STEAM_MATCHMAKING
  ShowFilterIcons(_source == BSInternet); //so do not show icons
#endif

  IControl *ctrl = GetCtrl(IDC_MULTI_TITLE);
  ITextContainer *title = GetTextContainer(ctrl);
  if (title)
  {
    char buffer[256];
    if (lan)
    {
      sprintf(buffer, LocalizeString(IDS_MULTI_TITLE_LAN), port);
    }
    else
    {
      sprintf(buffer, LocalizeString(IDS_MULTI_TITLE_REMOTE), (const char *)_ipAddress, port);
    }
    title->SetText(buffer);
  }
  
  if (!GetNetworkManager().Init(ipAddress, port))
  {
    Fail("Network");
  }
}

void DisplayMultiplayer::UpdateSessions()
{
  if (!_sessions) return;

  RString selGUID;
  int sel = _sessions->SelectedSession();
  if (sel >= 0 && sel < _sessions->NSessions()) selGUID = _sessions->GetSessionID(sel);

  _sessions->_sessions.Resize(0);
  GetNetworkManager().GetSessions(_sessions->_sessions);
  
  int n = _sessions->_sessions.Size();
  _sessions->_indices.Realloc(n);
  _sessions->_indices.Resize(n);
  for (int i=0; i<n; i++) _sessions->_indices[i] = i;

  sel = 0;
  for (int i=0; i<_sessions->NSessions(); i++)
    if (_sessions->GetSessionID(i) == selGUID)
    {
      sel = i;
      break;
    }
  _sessions->SelectSession(sel);
}

#if _ENABLE_GAMESPY
static SBServer FindServer(ServerBrowser browser, RString guid)
{
  const char *ptr = guid;
  const char *p = strrchr(ptr, ':');
  if (!p) return NULL;

  RString ip(guid, p - ptr);
  int port = atoi(p + 1);
  return ServerBrowserGetServerByIP(browser, ip, port);
}

struct ServerPlayerInfo
{
  RString name;
  int score;
};
TypeIsMovableZeroed(ServerPlayerInfo)

static int CompareServerPlayerInfo(const ServerPlayerInfo *item0, const ServerPlayerInfo *item1)
{
  int diff = item1->score - item0->score;
  if (diff != 0) return diff;
  return stricmp(item0->name, item1->name);
}

#endif

bool GetModListNamesFunc(ModInfo *mod, AutoArray<RString> &modNames)
{
  if (mod)
  {
    modNames.Add(mod->name);
  }
  return false;
}

RString GetModListNames()
{
  AutoArray<RString> modNames;
  ForEachModDirectory(GetModListNamesFunc,modNames);
  RString modListNames;
  for (int i=modNames.Size()-1; i>=0; i--)
  {
    if (modListNames.IsEmpty())
      modListNames = modNames[i];
    else
      modListNames = modListNames + RString(";") + modNames[i];
  }
  return modListNames;
}

AutoArray<RString> GetModListNamesArray()
{
  AutoArray<RString> modNames;
  ForEachModDirectory(GetModListNamesFunc,modNames);
  return modNames;
}

// Spherical Law of Cosines http://www.movable-type.co.uk/scripts/latlong.html

static float DistOnGlobe(float lat1, float lon1, float lat2, float lon2)
{
  lat1 = HDegree(lat1);
  lat2 = HDegree(lat2);
  lon1 = HDegree(lon1);
  lon2 = HDegree(lon2);
  return acos(sin(lat1)*sin(lat2)+cos(lat1)*cos(lat2)*cos(lon2-lon1))*6378;
}


const SessionInfo *DisplayMultiplayer::CurrentSession() const
{
  const SessionInfo *session = NULL;
  if (_sessions)
  {
    int sel = _sessions->SelectedSession();
    if (sel >= 0 && sel < _sessions->NSessionsNoHost()) session = &_sessions->GetSession(sel);
  }
  return session;
}

void DisplayMultiplayer::Friends(AutoArray<RString> &friends)
{
#if _ENABLE_STEAM_MATCHMAKING
  if (_steamServerBrowser && _sessions)
    _steamServerBrowser->Friends(friends);
#endif
}

void DisplayMultiplayer::OnSessionChanged(bool changed)
{
  const SessionInfo *session = NULL;
  if (_sessions)
  {
    int sel = _sessions->SelectedSession();
    if (sel >= 0 && sel < _sessions->NSessionsNoHost()) session = &_sessions->GetSession(sel);
  }

  IControl *ctrl = GetCtrl(IDC_MULTI_JOIN);
  if (ctrl) ctrl->ShowCtrl(session != NULL);

#if _ENABLE_STEAM_MATCHMAKING
  DWORD curtime = ::GetTickCount();
  if (changed)
  {
    _nextRefreshDetailsTime = curtime+FirstRefresh;
  }
  if (session && curtime > _nextRefreshDetailsTime) //only once per second (it is like DoS attack when done every simulation step!)
  {
    _nextRefreshDetailsTime = curtime+RepeatRefresh;
    _steamServerBrowser->RefreshDetails(session->guid, session->smQueryPort);
    // if there is DisplayMultiplayerDetails child, refresh its data
    DisplayMultiplayerDetails *details = dynamic_cast<DisplayMultiplayerDetails *>(_child.GetRef());
    if (details)
      details->RefreshDetails();
  }
  UpdateSessionFavorite(session);
  
  // Details button
  ctrl = GetCtrl(IDC_MULTI_DETAILS);
  if (ctrl) ctrl->ShowCtrl(session!=nullptr);
#endif

  // fill the host info
  // header
  ctrl = GetCtrl(IDC_MP_PASSWORD);
  if (ctrl && ctrl->GetType() == CT_STATIC && _sessions)
  {
    int sel = _sessions->SelectedSession();
    static_cast<CStatic *>(ctrl)->SetTexture(_sessions->GetSessionIcon(sel));
  }

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_MP_HOST));
  if (text) text->SetText(session ? session->name : RString());

  text = GetTextContainer(GetCtrl(IDC_MP_TYPE));
  if (text)
  {
    RString type;
    RString typeName;
    if (session) type = session->gameType;
    if (type.GetLength() > 0)
    {
      ConstParamEntryPtr entry = (Pars >> "CfgMPGameTypes").FindEntry(type);
      if (entry) typeName = (*entry) >> "name";
      else typeName = Pars >> "CfgMPGameTypes" >> "Unknown" >> "name";
    }
    text->SetText(typeName);
  }

  text = GetTextContainer(GetCtrl(IDC_MP_MISSION));
  if (text) text->SetText(session ? session->mission : RString());

  text = GetTextContainer(GetCtrl(IDC_MP_STATE));
  if (text)
  {
    RString state;
    if (session && !session->badActualVersion && !session->badRequiredVersion)
    {
      switch (session->serverState)
      {
      case NSSSelectingMission:
        state = LocalizeString(IDS_SESSION_CREATE);
        break;
      case NSSEditingMission:
        state = LocalizeString(IDS_SESSION_EDIT);
        break;
      case NSSAssigningPlayers:
        state = LocalizeString(IDS_SESSION_WAIT);
        break;
      case NSSSendingMission:
      case NSSLoadingGame:
        state = LocalizeString(IDS_SESSION_SETUP);
        break;
      case NSSPlaying:
        state = LocalizeString(IDS_SESSION_PLAY);
        break;
      case NSSMissionAborted:
        state = LocalizeString(IDS_SESSION_DEBRIEFING);
        break;
      }
    }
    text->SetText(state);
  }

  text = GetTextContainer(GetCtrl(IDC_MP_SLOTS_PUBLIC));
  if (text)
  {
    RString slots;
    if (session)
    {
      if (session->slotsPublic)
        slots = Format("%d/%d", session->playersPublic, session->slotsPublic);
      else
        slots = Format("%d", session->playersPublic);
    }
    text->SetText(slots);
  }

  text = GetTextContainer(GetCtrl(IDC_MP_PING));
  if (text)
  {
    RString ping;
    PackedColor color = _colorPingUnknown;
    if (session)
    {
      if (session->ping < 0 || session->ping == INT_MAX) ping = "?";
      else
      {
        ping = Format("%d", session->ping);

        if (session->ping < PingGood) color = _colorPingGood;
        else if (session->ping < PingPoor) color = _colorPingPoor;
        else color = _colorPingBad;
      }
    }
    text->SetText(ping);
    text->SetColor(color);
  }

  // additional server info
  text = GetTextContainer(GetCtrl(IDC_MP_PLATFORM));
  if (text)
  {
    // TODO: Localization whenever possible
    RString platform;
    if (session)
    {
      if (stricmp(session->platform, "L") == 0) platform = "Linux";
      else if (stricmp(session->platform, "W") == 0) platform = "Windows";
      else if (stricmp(session->platform, "X") == 0) platform = "Xbox";

      if (session->dedicated) platform = platform + RString(" DS");
    }
    text->SetText(platform);
  }

  text = GetTextContainer(GetCtrl(IDC_MP_LANGUAGE));
  if (text)
  {
    if (session && (session->language & 0x10000) != 0)
      text->SetText(GetLangName(session->language & 0xffff));
    else
      text->SetText(RString());
  }

  text = GetTextContainer(GetCtrl(IDC_MP_COUNTRY));
  if (text)
  {
    if (session)
    {
      // if we know longitude / latitude, we may show a distance

      RString countryString = session->country;
#if _ENABLE_GAMESPY
      if (_latitude<INT_MAX && _longitude<INT_MAX && session->latitude<INT_MAX && session->longitude<INT_MAX)
      {
        float dist = DistOnGlobe(session->latitude,session->longitude,_latitude,_longitude);
        int round = 1000;
        int distRound = (toInt(dist)+round/2)/round*round;
        const int nearLimit = 2000;
        // TODO: localize properly
        RString distanceString = distRound<nearLimit ? Format("<%d km",nearLimit) : Format("%d km",distRound);
        countryString = countryString.IsEmpty() ? distanceString : Format("%s (%s)",cc_cast(countryString),cc_cast(distanceString));
      }
#endif
      text->SetText(countryString);
    }
    else
      text->SetText(RString());
  }

  text = GetTextContainer(GetCtrl(IDC_MP_VERSION));
  if (text)
  {
    RString version;
    PackedColor color = _colorVersionGood;
    if (session)
    {
      version = Format(LocalizeString(IDS_SESSION_VERSION), 0.01f * session->actualVersion);
      if (session->badActualVersion) color = _colorVersionBad;
    }
    text->SetText(version);
    text->SetColor(color);
  }

  text = GetTextContainer(GetCtrl(IDC_MP_VERSION_REQUIRED));
  if (text)
  {
    RString version;
    PackedColor color = _colorVersionGood;
    if (session)
    {
      version = Format(LocalizeString(IDS_SESSION_REQUIRED), 0.01f * session->requiredVersion);
      if (session->requiredBuildNo>0) version = version + Format(".%d",session->requiredBuildNo);
      if (session->badRequiredVersion) color = _colorVersionBad;
    }
    text->SetText(version);
    text->SetColor(color);
  }

#if _ENABLE_GAMESPY
  text = GetTextContainer(GetCtrl(IDC_MP_MODS));
  if (text)
  {
    RString mods;
    PackedColor color = _colorVersionGood;

#if _ENABLE_GAMESPY 
  BrowsingSource source = BSInternet;
#else
  BrowsingSource source = BSLAN;
#endif

    if (session && (_source==source)) // session->mod on LAN contains GLoadedContentHash
    {
      mods = session->mod;

      //! store mods in structured text
      if (session->badMod) color = _colorVersionBad;
    }
    text->SetText(mods);
    text->SetColor(color);
  }
#endif

  // additional mission info
  text = GetTextContainer(GetCtrl(IDC_MP_ISLAND));
  if (text)
  {
    RString island;
    if (session && session->island.GetLength() > 0)
    {
      ConstParamEntryPtr islandCfg = (Pars >> "CfgWorlds").FindEntry(session->island);
      if (islandCfg)
      {
        island = (*islandCfg) >> "description";
      }
    }
    text->SetText(island);
  }

  text = GetTextContainer(GetCtrl(IDC_MP_DIFFICULTY));
  if (text)
  {
    if (session && session->difficulty >= 0 && session->difficulty < Glob.config.diffSettings.Size())
      text->SetText(Glob.config.diffSettings[session->difficulty].displayName);
    else
      text->SetText(RString());
  }

  text = GetTextContainer(GetCtrl(IDC_MP_TIMELEFT));
  if (text)
  {
    RString timeLeft;
    if (session && session->mission.GetLength() > 0 && session->timeleft > 0)
      timeLeft = Format(LocalizeString(IDS_TIME_LEFT), session->timeleft);
    text->SetText(timeLeft);
  }

  // players
  text = GetTextContainer(GetCtrl(IDC_MP_PLAYERS_ROW));
  if (text)
  {
    RString players;
#if _ENABLE_STEAM_MATCHMAKING
    if (session)
    {
      // Friends list
      if (_steamServerBrowser)
      {
        const AutoArray<RString> &friends = _steamServerBrowser->Players();
        for (int i=0; i<friends.Size(); i++)
        {
          RString player = friends[i];
          if (i == friends.Size() - 1)
          {
            // the last player
            RString check = players + player;
            if (text->TextFits(check))
              players = check;
            else
              players = players + RString("...");
            // break not needed
          }
          else
          {
            RString check = players + player + RString("; ...");
            if (text->TextFits(check))
              players = players + player + RString("; ");
            else
            {
              players = players + RString("...");
              break;
            }
          }
        }
      }
    }
#endif
    text->SetText(players);
  }

  text = GetTextContainer(GetCtrl(IDC_REQUIRED_BATTLEYE));
  if (text)
  {
    if(session)
    {
    if (session->battleEye>0)
      text->SetText(Format(LocalizeString(IDS_DISP_XBOX_HINT_YES)).Data());
    else
      text->SetText(Format(LocalizeString(IDS_DISP_XBOX_HINT_NO)).Data());
}
    else text->SetText("");
}

}

static RString And(" and ");
inline static void AddCondition(RString &filter, RString condition)
{
  if (filter.GetLength() > 0) filter = filter + And;
  filter = filter + condition;
}

struct ConditionScope
{
  RString &filter;
  RString cond;
  bool add;
  bool addNot;

  ConditionScope(RString &filter, int stage, int limit):filter(filter),add(stage<=limit+1),addNot(stage-1==limit)
  {
  }
  ~ConditionScope()
  {
    if (cond.IsEmpty()) {}
    else if (add)
    {
      if (addNot) AddCondition(filter, "not (" + cond + ")");
      else AddCondition(filter, cond);
    }
  }
};

static RString LatLongFilter(int myLat, int myLng, int maxAngle)
{
  // modulo of difference needed to handle longitude difference around +180/-180 correctly, +540 used to avoid module of negative number
  #define DIFF_ANG(name) "((" #name "-%d+540)%%360-180)" 
  return Format(DIFF_ANG(lng) "*" DIFF_ANG(lng) "+(lat-%d)*(lat-%d)<%d*%d",myLng,myLng,myLat,myLat,maxAngle,maxAngle);
}

  const char nameDS_KEY[] = "ds";
  const char nameEQ_MOD_KEY[] = "eqMod";
  const char nameREQ_VER_KEY[] = "reqVer";
  const char nameDIFF_KEY[] = "diff";
  const char nameBE_KEY[] = "be";
  const char nameSIG_KEY[] = "sig";
  const char nameVER_SIG_KEY[] = "verSig";


static RString CreateServerFilter(const SessionFilter &filter, int stage, int myLat, int myLng)
{
  RString value;
  if (filter.serverName.GetLength() > 0)
    AddCondition(value, Format("hostname like '%%%s%%'", (const char *)filter.serverName));

  if (filter.missionType.GetLength() > 0)
    AddCondition(value, Format("gametype like '%%%s%%'", (const char *)filter.missionType));

  if (Glob.isDayZ==Globals::IsDayZ)
  {
    AddCondition(value, "gametype = 'DAYZ'");
  }
  else if (Glob.isDayZ==Globals::IsDayZOther)
  {
    AddCondition(value, "gametype = 'DAYZ_M'");
  }
  else
  {
    AddCondition(value, "gametype <> 'DAYZ' and gametype <> 'DAYZ_M'");
  }

  if (filter.missionName.GetLength() > 0)
    AddCondition(value, Format("mission like '%%%s%%'", (const char *)filter.missionName));

  if (filter.minPlayers > 0)
    AddCondition(value, Format("numplayers >= %d", filter.minPlayers));
  if (filter.maxPlayers > 0)
    AddCondition(value, Format("numplayers <= %d", filter.maxPlayers));

  if (!filter.fullServers)
    AddCondition(value, "numplayers < maxplayers");

  switch (filter.battleyeRequired)
  {
    case SessionFilter::BEFLT_REQUIRED: AddCondition(value, RString(nameBE_KEY) + ">0" );break;
    case SessionFilter::BEFLT_WITHOUT_BE: AddCondition(value, RString(nameBE_KEY) + "=0" );break;
  }

  if (!filter.passwordedServers) AddCondition(value, "password=0");

  // subconditions 1 / 2 / 3
  // stage conditions:
  // 0:  1 and 2 and 3
  // 1: !1 and 2 and 3
  // 2:       !2 and 3
  // 3:             !3

  if (filter.maxPing>0 && myLat<INT_MAX && myLng<INT_MAX)
  {
    // ping conservative estimation
    // one degree is ~111 km
    const float oneDegInKm = 6378*H_PI/180;
    const float speedOfInternet = 300000*0.5*0.5; // speed of light, there and back, cables not straight
    const float oneDegInS = oneDegInKm/speedOfInternet;
    float maxDeg = filter.maxPing/(oneDegInS*1000);
    int maxDegUsed = toIntCeil(maxDeg)+1;
    static bool validPosOnly = false;
    if (validPosOnly)
    {
      AddCondition(value, "((lng<>0 or lat<>0) and "+LatLongFilter(myLat,myLng,maxDegUsed) + ")");
    }
    else
    {
      AddCondition(value, "(lng=0 and lat=0 or "+LatLongFilter(myLat,myLng,maxDegUsed) + ")");
    }
    // TODO: when maxPing is used, avoid duplicating the condition in an adaptive query
  }

  int counter = 0;
  {
    {
      ConditionScope cond(value, stage, counter++);
      //AddCondition(cond.cond, value, "reqSecureId >1");
      AddCondition(cond.cond, "numplayers >0");
      AddCondition(cond.cond, RString(nameVER_SIG_KEY) + ">1");
      if (filter.battleyeRequired==SessionFilter::BEFLT_NONE) AddCondition(cond.cond, RString(nameBE_KEY) + ">0");
      if (myLat<INT_MAX && myLng<INT_MAX) AddCondition(cond.cond, LatLongFilter(myLat,myLng,10));
    }
    {
      ConditionScope cond(value, stage, counter++);
      AddCondition(cond.cond, "numplayers < maxplayers");
      if (!filter.passwordedServers) AddCondition(cond.cond, "password=0");
      if (myLat<INT_MAX && myLng<INT_MAX) AddCondition(cond.cond, LatLongFilter(myLat,myLng,60));

    }
    {
      ConditionScope cond(value, stage, counter++);
      if (BUILD_NO>0) AddCondition(cond.cond, Format("reqBuild<=%d",BUILD_NO));
      AddCondition(cond.cond, Format("%s=%d",nameREQ_VER_KEY,APP_VERSION_NUM));
    }
  }
  if (stage<=counter++) {} // last stage is the original filter
  else return RString();
  return value;
}

#if _ENABLE_STEAM_MATCHMAKING

DisplayMultiplayer::SteamServerBrowser::~SteamServerBrowser()
{
  if (SteamMatchmakingServers())
  {
    if (_request != NULL)
    {
      // stop the current update
      SteamMatchmakingServers()->ReleaseRequest(_request);
      _request = NULL;
    }

    if (_queryPing != HSERVERQUERY_INVALID) SteamMatchmakingServers()->CancelServerQuery(_queryPing);
    if (_queryPlayers != HSERVERQUERY_INVALID) SteamMatchmakingServers()->CancelServerQuery(_queryPlayers);
    if (_queryRules != HSERVERQUERY_INVALID) SteamMatchmakingServers()->CancelServerQuery(_queryRules);
  }
}

TypeIsMovable(MatchMakingKeyValuePair_t);
struct SteamSBFilter : public AutoArray<MatchMakingKeyValuePair_t>
{
  SteamSBFilter() {}
  
  void AddFilter(const char *key, const char *value);
};

void SteamSBFilter::AddFilter(const char *key, const char *value)
{
  int ix = Add();
  MatchMakingKeyValuePair_t &filter = operator[](ix);
  strncpy(filter.m_szKey, key, sizeof(filter.m_szKey));
  strncpy(filter.m_szValue, value, sizeof(filter.m_szValue));
}

void DisplayMultiplayer::SteamServerBrowser::RefreshInternetServers(MPTabs activeTab)
{
  if (SteamMatchmakingServers())
  {
    if (_request != NULL)
    {
      // stop the current update
      SteamMatchmakingServers()->ReleaseRequest(_request);
      _request = NULL;
    }


    SteamSBFilter filter;

    filter.AddFilter("gamedir", "dayz");
    filter.AddFilter("secure", "1");
    filter.AddFilter("white", "1");

    if (_filter.minPlayers>0) filter.AddFilter("hasplayers","1"); 
    if (!_filter.fullServers) filter.AddFilter("notfull", "1"); 
    switch (_filter.battleyeRequired)
    {
    case SessionFilter::BEFLT_NONE: break;
    case SessionFilter::BEFLT_REQUIRED: filter.AddFilter("gametagsand", "battleye"); break;
    case SessionFilter::BEFLT_WITHOUT_BE: filter.AddFilter("gametagsnor", "battleye"); break;
    }

    MatchMakingKeyValuePair_t *ptr = filter.Data();
    // bugbug jmccaskey - passing just the appid without filters results in getting all servers rather than
    // servers filtered by appid alone.  So, we'll use the filters to filter the results better.
    switch (activeTab)
    {
    case MPTabInternet:
       _request = SteamMatchmakingServers()->RequestInternetServerList(SteamUtils()->GetAppID(), &ptr, filter.Size(), this);
       break;
    case MPTabFriends:
       _request = SteamMatchmakingServers()->RequestFriendsServerList(SteamUtils()->GetAppID(), &ptr, filter.Size(), this);
       break;
    case MPTabFavorite:
       _request = SteamMatchmakingServers()->RequestFavoritesServerList(SteamUtils()->GetAppID(), &ptr, filter.Size(), this);
       break;
    case MPTabHistory:
       _request = SteamMatchmakingServers()->RequestHistoryServerList(SteamUtils()->GetAppID(), &ptr, filter.Size(), this);
       break;
    }
    _requestedLAN  = false; //internet
  }
}

void DisplayMultiplayer::SteamServerBrowser::RefreshLANServers()
{
  if (SteamMatchmakingServers())
  {
    if (_request != NULL)
    {
      // stop the current update
      SteamMatchmakingServers()->ReleaseRequest(_request);
      _request = NULL;
    }

    // LAN refresh doesn't accept filters like internet above does
    _requestedLAN = true;
    _request = SteamMatchmakingServers()->RequestLANServerList(SteamUtils()->GetAppID(), this);
  }
}

void DisplayMultiplayer::SteamServerBrowser::Friends(AutoArray<RString> &friends)
{
  const SessionInfo &session = _sessions->GetSession(_sessions->SelectedSession());
  for (int i=0; i<_friendList.Size(); i++)
  {
    if ( _friendList[i].gameIP==session.smIP && _friendList[i].gamePort==session.smPort )
    {
      friends.Add(SteamFriends()->GetFriendPersonaName(_friendList[i].steamID));
    }
  }
}

void DisplayMultiplayer::SteamServerBrowser::RefreshDetails(RString guid, int serverPort)
{
  // steam server port is no longer a constant and could be adjusted by parameters. Current value is set in NetworkServer::_steamQueryPort on the server.
  // uint16 serverPort = 27016; // masterServerUpdaterPort

  if (SteamMatchmakingServers() &&
    _queryPing == HSERVERQUERY_INVALID && _queryPlayers == HSERVERQUERY_INVALID && _queryRules == HSERVERQUERY_INVALID)
  {
    int octet0 = 0, octet1 = 0, octet2 = 0, octet3 = 0;
    int port = 0;
    int done = sscanf(guid, "%d.%d.%d.%d:%d", &octet0, &octet1, &octet2, &octet3, &port);
    if (done == 5)
    {
      unsigned int ip = (octet3) + (octet2 << 8) + (octet1 << 16) + (octet0 << 24);
      _currentServerGuid = guid;
      _queryPing = SteamMatchmakingServers()->PingServer(ip, serverPort, this);
      /*
      // DayZ does not use players list, only friends list (but functionality was tested, works, just uncomment this line)
      _queryPlayers = SteamMatchmakingServers()->PlayerDetails(ip, serverPort, this);
      */
      // currently DayZ utilizes only requiredBuild in rules
      // there can be more facts like: country, etc.
      _queryRules = SteamMatchmakingServers()->ServerRules(ip, serverPort, this);
    }
  }
}

int DisplayMultiplayer::SteamServerBrowser::FindSession(RString guid) const
{
  // search for session in the list of sessions
  for (int i=0; i<_sessions->_sessions.Size(); i++)
  {
    if (_sessions->_sessions[i].guid == guid) return i;
  }
  return -1;
}

// ISteamMatchmakingServerListResponse implementation

bool DisplayMultiplayer::SteamServerBrowser::CheckFilter(gameserveritem_t *info)
{
  //no filter for LAN
  if (_requestedLAN) return true; 

  // server name
  if (!_filter.serverName.IsEmpty())
  {
    RString name = info->GetName(); name.Lower();
    if ( !strstr(name, _filter.serverName) ) return false;
  }
  // mission name
  if (!_filter.missionName.IsEmpty())
  {
    RString mission = info->m_szMap; mission.Lower();
    if ( !strstr(mission, _filter.missionName) ) return false;
  }
  // min..max Player count
  if ( (_filter.maxPlayers>0) && (info->m_nPlayers>_filter.maxPlayers) ) return false;
  if ( (_filter.minPlayers>0) && (info->m_nPlayers<_filter.minPlayers) ) return false;
  // max ping
  if ( (_filter.maxPing>0) && (info->m_nPing>_filter.maxPing) ) return false;
  // passworded
  if ( !_filter.passwordedServers && info->m_bPassword ) return false;
  // full servers
  if ( !_filter.fullServers && (info->m_nMaxPlayers<=info->m_nPlayers) ) return false;

  return true;
}

void DisplayMultiplayer::SteamServerBrowser::ServerResponded(HServerListRequest request, int server)
{
  gameserveritem_t *info = SteamMatchmakingServers()->GetServerDetails(request, server);
  if (info)
  {
    // Filter out servers that don't match our appid here (might get these in LAN calls since we can't put more filters on it)
    // if (info->m_nAppID != SteamUtils()->GetAppID()) return;

    // Enable servers hosted both by "DayZ" and "DayZ Server" applications
    if (info->m_nAppID != 221100 && info->m_nAppID != 223350) return;

    RString guid = info->m_NetAdr.GetConnectionAddressString();
    int index = FindSession(guid);

    if (index < 0)
    {
      if (!CheckFilter(info)) return;
      index = _sessions->_sessions.Add();
      _sessions->_indices.Add(index);
    }

    // update friends game playing list
    DWORD curTime = ::GetTickCount();
    if (curTime > _lastFriendUpdateTime + 250)
    {
      _lastFriendUpdateTime = curTime;
      _friendList.Resize(0);
      int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagAll);
      for (int i=0; i<friendCount; i++)
      {
        CSteamID friendId = SteamFriends()->GetFriendByIndex( i, k_EFriendFlagAll );
        FriendGameInfo_t fginfo;
        if (SteamFriends()->GetFriendGamePlayed( friendId, &fginfo ) && (fginfo.m_gameID.AppID()==info->m_nAppID) )
        {
          _friendList.Add(FriendOnServer( friendId.ConvertToUint64(), fginfo.m_unGameIP, fginfo.m_usGamePort ));
        }
      }
    }
    int friendCount = 0;
    for (int i=0; i<_friendList.Size(); i++)
    {
      if ( _friendList[i].gameIP==info->m_NetAdr.GetIP() && _friendList[i].gamePort==info->m_NetAdr.GetConnectionPort() )
      {
        friendCount++;
      }
    }
/*
    // DEBUG>
    int friendCount = SteamFriends()->GetFriendCountFromSource(info->m_steamID);
    RptF(" *** Server: \"%s\" (%llu , type=%d) friends: %d", info->GetName(), info->m_steamID.ConvertToUint64(), info->m_steamID.GetEAccountType(), friendCount);
    for (int i=0; i<friendCount; i++)
    {
      CSteamID friendId = SteamFriends()->GetFriendFromSourceByIndex( info->m_steamID, i );
      FriendGameInfo_t fginfo;
      if (SteamFriends()->GetFriendGamePlayed( friendId, &fginfo ))
        RptF("    -> %s [%llu, %llu]", SteamFriends()->GetFriendPersonaName(friendId), fginfo.m_gameID.ToUint64(), fginfo.m_steamIDLobby.ConvertToUint64());
      else
        RptF("    -> %s", SteamFriends()->GetFriendPersonaName(friendId));
    }

    {
      int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagAll);
      RptF(" ### ALL friends: %d", friendCount);
      for (int i=0; i<friendCount; i++)
      {
        CSteamID friendId = SteamFriends()->GetFriendByIndex( i, k_EFriendFlagAll );
        FriendGameInfo_t fginfo;
        if (SteamFriends()->GetFriendGamePlayed( friendId, &fginfo ))
          RptF("    -> %s [%llu, %llu, %x:%d]", SteamFriends()->GetFriendPersonaName(friendId), fginfo.m_gameID.ToUint64(), fginfo.m_steamIDLobby.ConvertToUint64(), fginfo.m_unGameIP, fginfo.m_usGamePort);
        else
          RptF("    -> %s (%d)", SteamFriends()->GetFriendPersonaName(friendId), friendId.GetEAccountType());
      }
    }

    {
      int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagOnGameServer);
      RptF(" ### friends on game servers: %d", friendCount);
      for (int i=0; i<friendCount; i++)
      {
        CSteamID friendId = SteamFriends()->GetFriendByIndex( i, k_EFriendFlagOnGameServer );
	      // returns true if the friend is actually in a game, and fills in pFriendGameInfo with an extra details 
        FriendGameInfo_t fginfo;
        if (SteamFriends()->GetFriendGamePlayed( friendId, &fginfo ))
          RptF("    -> %s [%llu, %llu]", SteamFriends()->GetFriendPersonaName(friendId), fginfo.m_gameID.ToUint64(), fginfo.m_steamIDLobby.ConvertToUint64());
      }
    }
*/
    // fill the fields
    SessionInfo &session = _sessions->_sessions[index];
    session.guid = guid;
    session.smQueryPort = info->m_NetAdr.GetQueryPort();
    session.smIP = info->m_NetAdr.GetIP();
    session.smPort = info->m_NetAdr.GetConnectionPort();
#if USER_Bebul
    session.name = Format("%d:%s", info->m_nAppID, info->GetName());
#else
    session.name = info->GetName();
#endif
    session.steamID = info->m_steamID.ConvertToUint64();
    session.lastTime = 0xffffffff;
    session.actualVersion = info->m_nServerVersion / 1000000; //29113822 -> 29
    session.actualBuildNo = info->m_nServerVersion % 1000000; //29113822 -> 113822
    void CheckMPVersion(SessionInfo &info);
    CheckMPVersion(session); // sets session.badActualVersion
    session.requiredVersion = 0;
    session.password = info->m_bPassword;
    session.lock = false;
    session.badRequiredVersion = false;
    session.badAdditionalMods = false;
    session.badMod = false;
    session.gameType = RString();
    session.mission = info->m_szMap;
    session.island = info->m_szGameDescription;
    session.serverState = 0;
    session.ping = info->m_nPing;
    session.playersPublic = info->m_nPlayers;
    session.friends = friendCount;
    session.slotsPublic = info->m_nMaxPlayers;
    session.timeleft = 0;
    session.platform = RString();
    session.mod = RString();
    session.equalModRequired = false;
    session.dedicated = false;
    session.language = 0;
    session.difficulty = 0;
    session.country = RString();
    session.region = 0;
    session.timeOfDay = RString();
    
    char tags[256]; 
		strcpy(tags, info->m_szGameTags);
    char *p = strtok(tags, ",");

    if (p && stricmp(p, "battleye")==0) 
    {
      session.battleEye = true;
      p = strtok(NULL, ",");
    }
    else session.battleEye = false;

    if (p && stricmp(p, "no3rd")==0)
    {
      session.disable3rdPerson = true;
      p = strtok(NULL, ",");
    }
    else session.disable3rdPerson = false;
    
		if (p && stricmp(p, "privHive")==0)
    {
      session.privateHive = true;
      p = strtok(NULL, ",");
    }
    else session.privateHive = false;

		session.environmentTimeMul = 0.0f;
		if (p && strstr(p, "etm") == p)
		{
			p += 3;
			session.environmentTimeMul = floatMinMax(atof(p), 0.0f, 64.0);
			p = strtok(NULL, ",");
		}

		if (p)
    {
      session.timeOfDay = p;
      //RptF("Here you are: (gameserveritem_t *)0x%x - %s", info, info->m_szGameTags);
    }

    //RptF("%d - %d - %s", info->m_nServerVersion, session.badActualVersion, cc_cast(session.name));
  }
}

void DisplayMultiplayer::SteamServerBrowser::ServerFailedToRespond(HServerListRequest request, int server)
{
  // some reaction possible, but not requested
  gameserveritem_t *info = SteamMatchmakingServers()->GetServerDetails(request, server);
  LogF("Server %s (%d) failed to respond", info ? info->GetName() : "???", server);
}

void DisplayMultiplayer::SteamServerBrowser::RefreshComplete(HServerListRequest request, EMatchMakingServerResponse response)
{
  // Doesn't really matter to us whether the response tells us the refresh succeeded or failed,
  // we just rack whether we are done refreshing or not
  LogF("RefreshComplete");
}

// ISteamMatchmakingPingResponse implementation
void DisplayMultiplayer::SteamServerBrowser::ServerResponded(gameserveritem_t &server)
{
  int index = FindSession(_currentServerGuid);
  if (index >= 0)
  {
    // update the fields
    SessionInfo &session = _sessions->_sessions[index];
    session.ping = server.m_nPing;
  }

  _queryPing = HSERVERQUERY_INVALID;
}

void DisplayMultiplayer::SteamServerBrowser::ServerFailedToRespond()
{
  // some reaction possible, but not requested
  LogF("Server %s failed to respond (ping)", cc_cast(_currentServerGuid));
  _queryPing = HSERVERQUERY_INVALID;
}

// ISteamMatchmakingPlayersResponse implementation
void DisplayMultiplayer::SteamServerBrowser::AddPlayerToList(const char *name, int score, float timePlayed)
{
  //LogF("AddPlayerToList: %s, %d, %f", name, score, timePlayed);
  _playersUpdate.Add(name);
}

void DisplayMultiplayer::SteamServerBrowser::PlayersFailedToRespond()
{
  // some reaction possible, but not requested
  LogF("Server %s failed to respond (players)", cc_cast(_currentServerGuid));
  _queryPlayers = HSERVERQUERY_INVALID;
}

void DisplayMultiplayer::SteamServerBrowser::PlayersRefreshComplete()
{
  LogF("PlayersRefreshComplete");
  QSort(_playersUpdate.Data(), _playersUpdate.Size(), [](RString *str1, RString *str2)->int { return strcmp(cc_cast(*str1), cc_cast(*str2)); });
  _players = _playersUpdate;
  _playersUpdate.Resize(0);
  _queryPlayers = HSERVERQUERY_INVALID;
}

// ISteamMatchmakingRulesResponse implementation
void DisplayMultiplayer::SteamServerBrowser::RulesResponded(const char *rule, const char *value)
{
  LogF("RulesResponded: %s, %s", rule, value);
  if ( !stricmp(rule,"requiredBuild") ) 
  {
    SessionInfo *session = NULL;
    if (_sessions)
    {
      int sel = _sessions->SelectedSession();
      if (sel >= 0 && sel < _sessions->NSessionsNoHost()) session = &_sessions->GetSession(sel);
    }
    if (session)
      session->requiredBuildNo = atoi(value);
  }
}

void DisplayMultiplayer::SteamServerBrowser::RulesFailedToRespond()
{
  // some reaction possible, but not requested
  LogF("Server %s failed to respond (rules)", cc_cast(_currentServerGuid));
  _queryRules = HSERVERQUERY_INVALID;
}

void DisplayMultiplayer::SteamServerBrowser::RulesRefreshComplete()
{
  _queryRules = HSERVERQUERY_INVALID;
  LogF("RulesRefreshComplete");
}

#endif

/*!
\patch 1.59 Date 5/24/2002 by Jirka
- Added: Ingame server browser 
\patch 1.89 Date 10/23/2002 by Jirka
- Fixed: Joining to DirectPlay sessions through ingame browser often failed
*/


#if _ENABLE_GAMESPY

#include "El/DataSignatures/dataSignatures.hpp"
//! check whether there are signature files of given accptedKeys present for all banks
bool PreverifySignatures(StaticArrayAuto<RString> &acceptedKeys)
{
  BankList::ReadAccess banks(GFileBanks);
  for (int i=0; i<banks.Size(); i++)
  {
    if (banks[i].VerifySignature()) 
    {
      QFBank &bank = banks[i];
      if (bank.IsPatched()) 
      { //report problem, but only once 
        static bool reported = false;
        if (!reported)
        {
          RptF( "Signature keys checking always FAILs because file '%s' is patched!", cc_cast(bank.GetOpenName()) );
          reported = true;
        }
        return false;
      }
      if (!DataSignatures::PreverifySignature(bank.GetOpenName(), acceptedKeys))
      {
        return false;
      }
    }
  }
  return true;
}

void DisplayMultiplayer::UpdateServer(SBServer server, bool add)
{
  const char *ip = SBServerGetPublicAddress(server);
  int port = SBServerGetPublicQueryPort(server);
  RString guid = GetNetworkManager().IPToGUID(ip, port);

  int index = -1;
  // search for session in the list of sessions
  for (int i=0; i<_sessions->_sessions.Size(); i++)
  {
    if (_sessions->_sessions[i].guid == guid)
    {
      index = i;
      break;
    }
  }

  // client side filtering
  bool filtered = false;

  int password = SBServerGetIntValue(server, "password", 0);
  int beEnabled = SBServerGetIntValue(server, nameBE_KEY, 0);
  /*
  if (!_filter.passwordedServers && (password != 0)) filtered = true;

  if ((_filter.battleyeRequired==SessionFilter::BEFLT_REQUIRED) && (beEnabled == 0)) filtered = true;
  else if ((_filter.battleyeRequired==SessionFilter::BEFLT_WITHOUT_BE) && (beEnabled == 1)) filtered = true;
  */

  int ping = SBServerGetPing(server);
  static bool filterPing = true;
  if (filterPing)
  {
    if (_filter.maxPing > 0 && ping > _filter.maxPing) filtered = true;
  }

  RString sessMod = SBServerGetStringValue(server, "mod", "");
  RString sessModHash = SBServerGetStringValue(server, "modhash", "");
  //RptF("modhash: \"%s\"", cc_cast(sessModHash));

  bool badAdditionalMods;
  if (_source != BSInternet)
  {
    bool CheckAdditionalModsLAN(RString mods);
    badAdditionalMods = CheckAdditionalModsLAN(sessMod);
  }
  else
  {
    bool CheckAdditionalModsWAN(RString mods);
    badAdditionalMods = CheckAdditionalModsWAN(sessModHash);
  }
  
  if ( !_filter.expansionsServers && badAdditionalMods ) filtered = true;

  // handle unknown ping
  if (add && ping == 0) ping = INT_MAX;

  // update / create a session
  if (index >= 0)
  {
    // update
    if (filtered)
    {
      // remove the session
      _sessions->DeleteSession(index);
      return;
    }
  }
  else
  {
    // create
    if (filtered)
    {
      // do not add the session
      return;
    }
    index = _sessions->_sessions.Add();
    _sessions->_indices.Add(index);
  }

  // fill the fields
  SessionInfo &session = _sessions->_sessions[index];
  session.guid = guid;

  session.name = SBServerGetStringValue(server, "hostname", "");
  session.gameType = SBServerGetStringValue(server, "gametype", "");
  session.island = SBServerGetStringValue(server, "mapname", "");
  session.mission = SBServerGetStringValue(server, "mission", "");
  session.lastTime = 0xFFFFFFFF;

  session.battleEye = beEnabled;

  session.actualVersion = SBServerGetIntValue(server, "ver", 0);
  session.requiredVersion = SBServerGetIntValue(server, nameREQ_VER_KEY, 0);
  if (session.actualVersion==0) session.actualVersion = session.requiredVersion;
  session.requiredBuildNo = SBServerGetIntValue(server, "reqBuild", 0);
  session.password = (password & 2) != 0;
  session.lock = (password & 1) != 0;
  session.serverState = SBServerGetIntValue(server, "gameState", NSSNone);

  void CheckMPVersion(SessionInfo &info);
  CheckMPVersion(session);

  if (ping > 0 || !SBServerHasFullKeys(server)) session.ping = ping;
  session.playersPublic = SBServerGetIntValue(server, "numplayers", 0);
  session.slotsPublic = SBServerGetIntValue(server, "maxplayers", 0);
  session.timeleft = SBServerGetIntValue(server, "timeleft", 15);
  
  RString platform = SBServerGetStringValue(server, "platform", "");
  if (stricmp(platform, "xbox") == 0) session.platform = "X";
  else if (stricmp(platform, "win") == 0) session.platform = "W";
  else if (stricmp(platform, "linux") == 0) session.platform = "L";
  else session.platform = "?";
  
  session.mod = sessMod; 
  session.equalModRequired = SBServerGetIntValue(server, nameEQ_MOD_KEY, 0) != 0;
  if (session.equalModRequired)
  {
    RString sessLoadedContentHash = SBServerGetStringValue(server, "hash", "");
    if (sessLoadedContentHash.IsEmpty())
    { // old: sessModHash is used only for backward compatibility
      session.badMod = stricmp(sessModHash, GModInfos.hash) != 0;
    }
    else
    { // new: checking the overall hash of all loaded banks
      session.badMod = strcmp(sessLoadedContentHash, GLoadedContentHash) != 0;
    }
  }
  else session.badMod = false; //not bad
  session.badAdditionalMods = badAdditionalMods;

  session.dedicated = SBServerGetIntValue(server, nameDS_KEY, 0) != 0;
  session.language = SBServerGetIntValue(server, "language", 0x10009); // English by default
  session.difficulty = SBServerGetIntValue(server, nameDIFF_KEY, Glob.config.diffDefault);
  RString country = SBServerGetStringValue(server, "country", "");
  if (country.GetLength() > 0) session.country = country;
  int region = SBServerGetIntValue(server, "region", 0);
  if (region!=0) session.region = region;
  session.longitude = SBServerGetIntValue(server, "lng", INT_MAX);
  session.latitude = SBServerGetIntValue(server, "lat", INT_MAX);

  // Signature keys checking (only lightweighted test)
  if ( SBServerGetIntValue(server, nameVER_SIG_KEY, -1)!=0 )
  {
    RString signatureList = SBServerGetStringValue(server, nameSIG_KEY, "");
    //LogF("Server signature list: '%s'", cc_cast(signatureList));

    if (!signatureList.IsEmpty())
    {
      // preverify signatures of all banks against server accepted key list
      AUTO_STATIC_ARRAY(RString, signatureArr, 8);
      const char *nextDelim=NULL;
      bool truncatedSignatureList = false; //when signatures GSKey ends with trailing ';...'
      for (const char *nextSig = cc_cast(signatureList); nextSig; nextSig=nextDelim)
      {
        nextDelim = strchr(nextSig, ';');
        if (nextDelim)
        {
          signatureArr.Add(RString(nextSig, nextDelim - nextSig));
          nextDelim++;
}
        else
        {
          if ( stricmp(nextSig,"...")==0 )
          {
            truncatedSignatureList = true;
      }
          else signatureArr.Add(RString(nextSig));
    }
  }
      // Note: PreverifySignatures called only for non truncated signature lists
      if (!truncatedSignatureList && !PreverifySignatures(signatureArr)) 
        session.badSignatures=true;
}
  }
}

void DisplayMultiplayer::RemoveServer(SBServer server)
{
  const char *ip = SBServerGetPublicAddress(server);
  int port = SBServerGetPublicQueryPort(server);
  RString guid = GetNetworkManager().IPToGUID(ip, port);

  // search for session in the list of sessions
  for (int i=0; i<_sessions->_sessions.Size(); i++)
  {
    if (_sessions->_sessions[i].guid == guid)
    {
      // remove the session
      _sessions->DeleteSession(i);
      return;
    }
  }
}

void DisplayMultiplayer::OnSBUpdated(ServerBrowser serverBrowser, SBCallbackReason reason, SBServer server)
{
  if (_source != BSInternet) return;

  // ignore additional updates
  switch (reason)
  {
  case sbc_serveradded:
    UpdateServer(server, true);
    break;
  case sbc_serverupdated:
    UpdateServer(server, false);
    break;
  case sbc_serverupdatefailed:
    return;
  case sbc_serverdeleted:
    RemoveServer(server);
    break;
  case sbc_updatecomplete:
    if (_listUpdateRunning==StageRunning)
    {
      _listUpdateRunning = StageDone;
    }
    return;
  case sbc_queryerror:
    RptF("Query Error: %s", ServerBrowserListQueryError(_serverBrowser));
    return;
  }

  // update the progress bar
  int total = ServerBrowserCount(_serverBrowser);
  if (total > 0)
  {
    float rangeBeg = float(_listUpdateStage*100)/_listUpdateMaxStage;
    float rangeEnd = float((_listUpdateStage+1)*100)/_listUpdateMaxStage;
    int inProgress = ServerBrowserPendingQueryCount(_serverBrowser);
    SetProgress(float(total - inProgress) / total*(rangeEnd-rangeBeg)+rangeBeg);
  }
  else SetProgress(0);
}


/**
Updates are staged to make sure more useful servers are listed first
*/
bool DisplayMultiplayer::StageUpdateServerList(int stage)
{
  // fields we're interested in
  static const unsigned char fields[] =
  {
    HOSTNAME_KEY, HOSTPORT_KEY,
    NUMPLAYERS_KEY, MAXPLAYERS_KEY, COUNTRY_KEY,
    LATITUDE_KEY, LONGITUDE_KEY,
    MAPNAME_KEY, GAMETYPE_KEY, MISSION_KEY, TIMELIMIT_KEY,
    PASSWORD_KEY,
    /*PARAM1_KEY, PARAM2_KEY,*/
    VER_KEY, REQUIRED_VERSION_KEY,
    MOD_KEY, EQUAL_MOD_REQUIRED_KEY,
    GAME_STATE_KEY,
    DEDICATED_KEY, PLATFORM_KEY, //LANGUAGE_KEY, 
    DIFFICULTY_KEY,
    BATTLEYE_KEY,
    SIGNATURES_KEY, VERIFY_SIGNATURES_KEY,
    MOD_HASH_KEY, HASH_KEY, REQ_BUILD_KEY, REQ_SECURE_ID_KEY
  };
  const int fieldsCount = lenof(fields);
  STATIC_ASSERT(fieldsCount<40); // MAX_QUERY_KEYS
  const int maxKeysLen = 256; // MAX_FIELD_LIST_LEN
	int listLen = 0; // based on ServerBrowserBeginUpdate2 GameSpy internal function
	for (int i = 0 ; i < fieldsCount ; i++)
	{
		listLen += (int)strlen(qr2_registered_key_list[fields[i]])+1;
	}
  DoAssert(listLen<maxKeysLen);

  RString filter = CreateServerFilter(_filter,stage, _latitude, _longitude);
  if (filter.IsEmpty()) return false;

  static int maxServers = 500;
  static int minServersPerStage = 20;

  int stagesLeft = _listUpdateMaxStage-1-stage;
  int maxServersLeft = maxServers-stagesLeft*minServersPerStage-_sessions->_sessions.Size();
  if (maxServersLeft<=0) return false;

  LogF("Server update %d, servers left %d: '%s'",stage,maxServersLeft,cc_cast(filter));
  ServerBrowserHalt(_serverBrowser);
  SBError error = ServerBrowserLimitUpdate(_serverBrowser, SBTrue, SBFalse, fields, fieldsCount, filter, maxServersLeft);
  (void)error;
  _listUpdateRunning = StageRunning;
  return true;
}

/*!
\patch 5124 Date 1/29/2007 by Jirka
- Fixed: MP session browser - sometimes session info was hiding and showing repeatedly
*/

void DisplayMultiplayer::UpdateServerList()
{
  if (!_sessions || !_serverBrowser) return;

  SBError error = ServerBrowserThink(_serverBrowser);
  (void)error;

  if (_refresh)
  {
    // if we're doing an update, cancel it
    SBState state = ServerBrowserState(_serverBrowser);
    if (state != sb_connected && state != sb_disconnected)
    {
      ServerBrowserHalt(_serverBrowser);
      return;
    }

    // clear the progress bar
    SetProgress(0);

    // clear the server browser and list
    _sessions->_sessions.Resize(0);
    _sessions->_indices.Resize(0);
    _sessions->SelectSession(0);
    _sessionSelected = false;

    _listUpdateStage = 0;
    _listUpdateMaxStage = 0; // provide some upper bound so that simulated CreateServerFilter runs without division by zero
    while ( !CreateServerFilter(_filter,_listUpdateMaxStage, _latitude, _longitude).IsEmpty()) _listUpdateMaxStage++;

    _refresh = false;
    ServerBrowserClear(_serverBrowser);
    StageUpdateServerList(0);
  }
}
#endif

int DisplayMultiplayer::GetPort()
{
  if (_ipAddress.GetLength() == 0) return _portLocal;
  else return _portRemote;
}

void DisplayMultiplayer::SetPort(int port)
{
  if (_ipAddress.GetLength() == 0) _portLocal = port;
  else _portRemote = port;

  ITextContainer *button = GetTextContainer(GetCtrl(IDC_MULTI_PORT));
  if (button)
  {
    BString<512> buffer;
    sprintf(buffer, LocalizeString(IDS_DISP_PORT_BUTTON), port);
    button->SetText((const char *)buffer);
  }
}

LSError SessionFilter::Serialize(ParamArchive &ar)
{ // Note: do not use Default Values! (It is called after ParseUserParamsArchive(ar), so default value would be not saved often)
  CHECK(ar.Serialize("serverName", serverName, 1))
  CHECK(ar.Serialize("missionType", missionType, 2))
  CHECK(ar.Serialize("missionName", missionName, 1))
  CHECK(ar.Serialize("maxPing", maxPing, 1))
  CHECK(ar.Serialize("minPlayers", minPlayers, 1))
  CHECK(ar.Serialize("maxPlayers", maxPlayers, 1))
  CHECK(ar.Serialize("fullServers", fullServers, 1))
  CHECK(ar.Serialize("passwordedServers", passwordedServers, 1))
  CHECK(ar.Serialize("battleyeRequired", (int &)battleyeRequired, 1))
  CHECK(ar.Serialize("expansions", expansionsServers, 1))
  return LSOK;
}

DEFINE_ENUM(BrowsingSource, BS, BROWSING_SOURCE_ENUM)

bool ParseUserParamsArchive(ParamArchiveLoad &ar, GameDataNamespace *globals);
bool ParseUserParamsArchive(ParamArchiveSave &ar, GameDataNamespace *globals);
void SaveUserParamsArchive(ParamArchiveSave &ar);

void DisplayMultiplayer::LoadParams()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamArchiveLoad ar;
  if (!ParseUserParamsArchive(ar, &globals)) return;

#if _ENABLE_STEAM_MATCHMAKING
  BrowsingSource defaultSource = BSInternet;
#else
  BrowsingSource defaultSource = BSLAN;
#endif

  ar.SerializeEnum("browsingSource", _source, 1, defaultSource);
#if _ENABLE_GAMESPY
  ar.Serialize("browsingLatitude", _latitude, 1, INT_MAX);
  ar.Serialize("browsingLongitude", _longitude, 1, INT_MAX);
#endif

  ParamArchive arSubcls;
  if (ar.IsSubclass("Filter"))
  {
    ar.Serialize("Filter", _filter, 1);
  }
}

void ParseFlashpointCfg(ParamFile &file);
void SaveFlashpointCfg(ParamFile &file);

void DisplayMultiplayer::SaveParams()
{
  // when out of memory, saving could fail severely
  if (IsOutOfMemory()) return;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamArchiveSave ar(UserInfoVersion);
  if (!ParseUserParamsArchive(ar, &globals)) return;

  ar.SerializeEnum("browsingSource", _source, 1);
#if _ENABLE_GAMESPY
  ar.Serialize("browsingLatitude", _latitude, 1, INT_MAX);
  ar.Serialize("browsingLongitude", _longitude, 1, INT_MAX);
#endif
  ar.Serialize("Filter", _filter, 1);

  SaveUserParamsArchive(ar);

  // save longitude / latide so that it can be used for hosting as well
  ParamFile fCfg;
  ParseFlashpointCfg(fCfg);
#if _ENABLE_GAMESPY
  fCfg.Add("serverLongitude",_longitude);
  fCfg.Add("serverLatitude",_latitude);
#endif
  SaveFlashpointCfg(fCfg);

}

DisplayMultiplayerDetails::DisplayMultiplayerDetails(ControlsContainer *parent)
  : Display(parent)
{
  _enableSimulation = false;
  Load("RscDisplayMultiplayerDetails");
  _lastFriendsUpdate = 0; // update immediately
  RefreshDetails();
}

DisplayMultiplayerDetails::~DisplayMultiplayerDetails()
{
}

Control *DisplayMultiplayerDetails::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_MP_BATTLEYE:
    {
      DisplayMultiplayer *multi = dynamic_cast<DisplayMultiplayer *>(_parent.GetLink());
      bool show = false;
      if (multi) 
      {
        const SessionInfo *session = multi->CurrentSession();
        if (session) show = session->battleEye != NULL;
      }
      ctrl->ShowCtrl(show);
    }
    break;
  }
  return ctrl;
}

void DisplayMultiplayerDetails::RefreshVariables()
{
  DisplayMultiplayer *multi = dynamic_cast<DisplayMultiplayer *>(_parent.GetLink());
  if (!multi) return;
  const SessionInfo *session = multi->CurrentSession();
  if (!session) return;

  // friends count
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_MULTI_FRIENDS));
  if (text)
    text->SetText(Format("%d", session->friends));

  // map / island 
  text = GetTextContainer(GetCtrl(IDC_MP_ISLAND));
  if (text)
  {
    if (session->requiredBuildNo) text->SetText(Format("%s %d", cc_cast(session->island), session->requiredBuildNo));
    else text->SetText(Format("%s", cc_cast(session->island)));
  }

  // players count
  text = GetTextContainer(GetCtrl(IDC_MULTI_PLAYERS));
  if (text)
  {
    RString slots;
    if (session->slotsPublic) slots = Format("%d/%d", session->playersPublic, session->slotsPublic);
    else slots = Format("%d", session->playersPublic);
    text->SetText(slots);
  }

  // ping
  text = GetTextContainer(GetCtrl(IDC_MP_PING));
  if (text)
    text->SetText(Format("%d", session->ping));
}

void DisplayMultiplayerDetails::RefreshDetails()
{
  DisplayMultiplayer *multi = dynamic_cast<DisplayMultiplayer *>(_parent.GetLink());
  if (!multi) return;
  const SessionInfo *session = multi->CurrentSession();
  if (!session) return;

  // take values from parent dialog and fill in the info
  // ip address
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_MULTI_IP_ADDRESS));
  if (text)
  {
    text->SetText(session->guid);
  }

  // server icon
  IControl *ctrl = GetCtrl(IDC_MP_PASSWORD);
  if (ctrl && ctrl->GetType() == CT_STATIC)
  {
    static_cast<CStatic *>(ctrl)->SetTexture(multi->GetCurrentSessionIcon());
  }

  // hostname
  text = GetTextContainer(GetCtrl(IDC_MP_HOST));
  if (text)
    text->SetText(session->name);

  // hive
  text = GetTextContainer(GetCtrl(IDC_MULTI_HIVE));
  if (text)
    text->SetText(session->privateHive ? "Private" : "Public");

  // mode
  text = GetTextContainer(GetCtrl(IDC_MULTI_MODE));
  if (text)
    text->SetText(session->disable3rdPerson ? "Hardcore" : "Regular"); //TODO: localize

  // time
  text = GetTextContainer(GetCtrl(IDC_MULTI_TIMEOFDAY));
  if (text)
    text->SetText(session->timeOfDay);

	text = GetTextContainer(GetCtrl(IDC_MULTI_TIMEOFDAYMUL));
	if (text)
	{
		RString tm = Format("%2.1f", session->environmentTimeMul);
		text->SetText(session->environmentTimeMul == 0.0f ? "Real time" : tm);
	}

#if _ENABLE_STEAM_MATCHMAKING
  // friends list
  DWORD curtime = ::GetTickCount();
  if (curtime > (_lastFriendsUpdate + 2000))
  {
    text = GetTextContainer(GetCtrl(IDC_MP_PLAYERS_ROW));
    if (text)
    {
      RString txtFriends;
      if (session)
      {
        // Friends list
        AutoArray<RString> friends; 
        multi->Friends(friends);
        for (int i=0; i<friends.Size(); i++)
        {
          txtFriends = txtFriends + friends[i];
          if (i!=friends.Size()-1) txtFriends = txtFriends + RString("; ");
        }
      }
      text->SetText(txtFriends);
    }
  }
#endif

  RefreshVariables();

}

Texture *DisplayMultiplayer::GetCurrentSessionIcon() const
{
  if (_sessions)
  {
    int sel = _sessions->SelectedSession();
    if (sel >= 0 && sel < _sessions->NSessions())
    {
      return _sessions->GetSessionIcon(sel);
    }
  }
  return NULL;
}

void DisplayMultiplayerDetails::OnSimulate(EntityAI *vehicle)
{
  RefreshVariables();
  _parent->OnSimulateBackground(vehicle);
  Display::OnSimulate(vehicle);
}

void DisplayMultiplayerDetails::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_MULTI_JOIN:
    Exit(IDC_MULTI_JOIN);
    break;
  }
  Display::OnButtonClicked(idc);
}

RString GetDefaultSessionName(RString playerName);

DisplayHostSettings::DisplayHostSettings(ControlsContainer *parent)
: Display(parent)
{
  _enableSimulation = false;
  Load("RscDisplayHostSettings");

  if (_name)
  {
#if _VBS2 // VBS2 nickname
    RString playerName = Glob.header.playerHandle;
#else
    RString playerName = Glob.header.GetPlayerName();
#endif
    _name->SetText(GetDefaultSessionName(playerName));
  }

  if (_maxPlayers) _maxPlayers->SetText(Format("%d", MAX_PLAYERS));

  if (_port) _port->SetText(Format("%d", GetNetworkPort()));
}

int DisplayHostSettings::GetMaxPlayers() const
{
  int players = _maxPlayers ? atoi(_maxPlayers->GetText()) : 0;
  if (players <= 0) players = MAX_PLAYERS;
  return players;
}

int DisplayHostSettings::GetPort() const
{
  int port = _port ? atoi(_port->GetText()) : 0;
  if (port <= 0) port = GetNetworkPort();
  return port;
}

Control *DisplayHostSettings::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_HOST_NAME:
    _name = GetTextContainer(ctrl);
    break;
  case IDC_HOST_PASSWORD:
    _password = GetTextContainer(ctrl);
    break;
  case IDC_HOST_MAX_PLAYERS:
    _maxPlayers = GetTextContainer(ctrl);
    break;
  case IDC_HOST_PRIVATE:
    _private = GetListBoxContainer(ctrl);
    _private->AddString(LocalizeString(IDS_MULTI_LAN));
    _private->AddString(LocalizeString(IDS_SESSIONS_INTERNET));
    _private->SetCurSel(0);
    break;
  case IDC_HOST_PORT:
    _port = GetTextContainer(ctrl);
    break;
  }
  return ctrl;
}


void DisplayHostSettings::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if(ctrl->IDC() == IDC_HOST_PRIVATE)
  {
    if(strcmpi(_private->GetText(curSel),LocalizeString(IDS_MULTI_LAN))==0)
      _isPrivate = true;
    else _isPrivate = false;
  }
}

#ifndef _XBOX

Control *DisplayPassword::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch(idc)
  {
  case IDC_PASSWORD_SERVER_NAME: 
    ITextContainer *ctrlText = GetTextContainer(ctrl);
    if (ctrlText)
    {
      RString text = LocalizeString(IDS_DISP_PASSWORD_TITLE);
      text = Format("%s : %s", cc_cast(text), _serverName.IsEmpty() ? cc_cast(_guid) : cc_cast(_serverName));
      ctrlText->SetText(text);
    }
    break;
  }
  return ctrl;
}

RString DisplayPassword::GetPassword() const
{
  const ITextContainer *ctrl = GetTextContainer(unconst_cast(GetCtrl(IDC_PASSWORD)));
  if (ctrl) return ctrl->GetText();
  return RString();
}

Control *DisplayPort::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
    case IDC_PORT_PORT:
    {
      CEditContainer *edit = GetEditContainer(ctrl);
      char buffer[256];
      #ifdef _WIN32
      itoa(_port, buffer, 10);
      #else
      sprintf(buffer,"%d",_port);
      #endif
      edit->SetText(buffer);
    }
    break;
  }
  return ctrl;
}

Control *DisplayIPAddress::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
    case IDC_IP_ADDRESS:
    {
      CEditContainer *edit = GetEditContainer(ctrl);
      RString address;
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        ConstParamEntryPtr entry = cfg.FindEntry("remoteIPAddress");
        if (entry) address = *entry;
      }     
      edit->SetText(address);
    }
    break;
    case IDC_IP_PORT:
    {
      CEditContainer *edit = GetEditContainer(ctrl);
      int port = GetNetworkPort();
      GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
      ParamFile cfg;
      if (ParseUserParams(cfg, &globals))
      {
        ConstParamEntryPtr entry = cfg.FindEntry("remotePort");
        if (entry) port = *entry;
      }     
      char buffer[256];
      #ifdef _WIN32
      itoa(port, buffer, 10);
      #else
      sprintf(buffer,"%d",port);
      #endif
      edit->SetText(buffer);
    }
    break;
  }
  return ctrl;
}

bool DisplayIPAddress::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

  if (_exit == IDC_OK)
  {
    CEditContainer *edit = GetEditContainer(GetCtrl(IDC_IP_ADDRESS));
    if (edit)
    {
      RString address = edit->GetText();
      // TODO: check IP address
      int n = address.GetLength() - 1;
      for (; n>=0; n--)
      {
        if (!isspace(address[n])) break;
      }
      if (n < 0 || address[n] != '.') return true;
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_MP_BAD_ADDRESS));
      return false;
    }
  }

  return true;
}
#endif // ndef _XBOX

#if _XBOX_SECURE

DWORD GetGameLanguage();

RString GetGameLanguageName(int language)
{
  switch (language)
  {
    // supported languages
  case XC_LANGUAGE_ENGLISH:
    return LocalizeString(IDS_LANGUAGE_ENGLISH);
  case XC_LANGUAGE_GERMAN:
    return LocalizeString(IDS_LANGUAGE_GERMAN);
  case XC_LANGUAGE_FRENCH:
    return LocalizeString(IDS_LANGUAGE_FRENCH);
  case XC_LANGUAGE_ITALIAN:
    return LocalizeString(IDS_LANGUAGE_ITALIAN);

    // unsupported languages
  case XC_LANGUAGE_SPANISH:
    // Spanish is unsupported now (CM PR 307)
  default:
    Fail("Unsupported language");
    return LocalizeString(IDS_LANGUAGE_ENGLISH);
  }
}

#endif

DisplaySystemLink::DisplaySystemLink(ControlsContainer *parent, bool enumeration)
: Display(parent)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // we don't need to be connected to Live for playing this mode
  GSaveSystem.SetLiveConnectionNeeded(false);
#endif

  _enableSimulation = false;
  _sessions = NULL;
  _enumeration = enumeration;

#if _XBOX_SECURE
  _rating = 0;
#endif

  Load("RscDisplayMultiplayer");

  // no "Refresh" for System link
  IControl *ctrl = GetCtrl(IDC_MULTI_REFRESH);
  if (ctrl) ctrl->ShowCtrl(GetSessionType() != STSystemLink);

  if (enumeration) GetNetworkManager().Init("", GetNetworkPort());
  if (_sessions)
  {
    _sessions->SelectSession(0);
    OnSessionChanged();
  }
}

DisplaySystemLink::~DisplaySystemLink()
{
  if (_enumeration) GetNetworkManager().RemoveEnumeration();
}

Control *DisplaySystemLink::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_MULTI_SESSIONS:
    {
      Control *ctrl = NULL;
      if (type == CT_LISTBOX)
      {
        C2DSessions *sessions = new C2DSessions(this, idc, cls);
        _sessions = sessions;
        ctrl = sessions;
      }
      else
      {
        Fail("Unsupported control type");
      }
      return ctrl;
    }
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplaySystemLink::OnSessionChanged()
{
  if (!_sessions) return;
  int sel = _sessions->SelectedSession();
  const SessionInfo *session = sel >= 0 ? &_sessions->GetSession(sel) : NULL;
  bool host = false;
  if (sel >= 0) host = _sessions->GetSessionID(sel).GetLength() == 0;

  // no "Join" for host
  IControl *ctrl = GetCtrl(IDC_MULTI_JOIN);
  if (ctrl) ctrl->ShowCtrl(!host);

  ctrl = GetCtrl(IDC_MP_HOST);
  ITextContainer *text = GetTextContainer(ctrl);
  if (text)
  {
    text->SetText(session ? session->name : RString());
  }

  ctrl = GetCtrl(IDC_MP_TYPE);
  text = GetTextContainer(ctrl);
  if (text)
  {
    RString type;
    RString typeName;
    if (session) type = session->gameType;
    if (type.GetLength() > 0)
    {
      ConstParamEntryPtr entry = (Pars >> "CfgMPGameTypes").FindEntry(type);
      if (entry) typeName = (*entry) >> "name";
      else typeName = Pars >> "CfgMPGameTypes" >> "Unknown" >> "name";
    }
    text->SetText(typeName);
  }
  ctrl = GetCtrl(IDC_MP_MISSION);
  text = GetTextContainer(ctrl);
  if (text)
  {
    text->SetText(session ? session->mission : RString());
  }
  ctrl = GetCtrl(IDC_MP_ISLAND);
  text = GetTextContainer(ctrl);
  if (text)
  {
    RString island;
    if (session && session->island.GetLength() > 0)
      island = Pars >> "CfgWorlds" >> session->island  >> "description";
    text->SetText(island);
  }
  ctrl = GetCtrl(IDC_MP_STATE);
  text = GetTextContainer(ctrl);
  if (text)
  {
    RString state;
    if (session && !session->badActualVersion && !session->badRequiredVersion && !host)
      switch (session->serverState)
      {
        case NSSSelectingMission:
          state = LocalizeString(IDS_SESSION_CREATE);
          break;
        case NSSEditingMission:
          state = LocalizeString(IDS_SESSION_EDIT);
          break;
        case NSSAssigningPlayers:
          state = LocalizeString(IDS_SESSION_WAIT);
          break;
        case NSSSendingMission:
        case NSSLoadingGame:
          state = LocalizeString(IDS_SESSION_SETUP);
          break;
        case NSSPlaying:
          state = LocalizeString(IDS_SESSION_PLAY);
          break;
        case NSSMissionAborted:
          state = LocalizeString(IDS_SESSION_DEBRIEFING);
          break;
      }
    text->SetText(state);
  }
  ctrl = GetCtrl(IDC_MP_TIMELEFT);
  text = GetTextContainer(ctrl);
  if (text)
  {
    RString timeLeft;
    if (session && session->mission.GetLength() > 0 && session->timeleft > 0)
      timeLeft = Format(LocalizeString(IDS_TIME_LEFT), session->timeleft);
    text->SetText(timeLeft);
  }
  ctrl = GetCtrl(IDC_MP_SLOTS_PUBLIC);
  text = GetTextContainer(ctrl);
  if (text)
  {
    RString slots;
    if (session && !host)
    {
      if (session->slotsPublic)
        slots = Format("%d/%d", session->playersPublic, session->slotsPublic);
      else
        slots = Format("%d", session->playersPublic);
    }
    text->SetText(slots);
  }
  text = GetTextContainer(GetCtrl(IDC_REQUIRED_BATTLEYE));
  if (text)
  {
    RString slots;
    if (session && !host)
    {
      if (session->battleEye>0)
        slots = Format(LocalizeString(IDS_DISP_XBOX_HINT_YES));
      else
        slots = Format(LocalizeString(IDS_DISP_XBOX_HINT_NO));
    }
    text->SetText(slots);
  }
  
#if _XBOX_SECURE
  ctrl = GetCtrl(IDC_MP_SLOTS_PRIVATE);
  text = GetTextContainer(ctrl);
  if (text)
  {
    RString slots;
    if (session && !host)
    {
      if (session->slotsPrivate)
        slots = Format("%d/%d", session->playersPrivate, session->slotsPrivate);
      else
        slots = Format("%d", session->playersPrivate);
    }
    text->SetText(slots);
  }
  ctrl = GetCtrl(IDC_MP_LANGUAGE);
  text = GetTextContainer(ctrl);
  if (text)
  {
    if (session->language >= 0)
      text->SetText(GetGameLanguageName(session->language));
    else
      text->SetText(RString());
  }
  ctrl = GetCtrl(IDC_MP_DIFFICULTY);
  text = GetTextContainer(ctrl);
  if (text)
  {
    if (session->difficulty >= 0 && session->difficulty < Glob.config.diffSettings.Size())
      text->SetText(Glob.config.diffSettings[session->difficulty].displayName);
    else
      text->SetText(RString());
  }
#endif

  UpdateKeyHints();
}

void DisplaySystemLink::OnSimulate(EntityAI *vehicle)
{
  UpdateSessions();
  Display::OnSimulate(vehicle);

  // session may changed - update details info
  OnSessionChanged();
}

void DisplaySystemLink::HostSession()
{
#if defined _XBOX && _XBOX_VER >= 200
  // GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#endif

  // create session
  SuspendEnum();
#if _XBOX_SECURE
  GetNetworkManager().CreateSession(GetSessionType(), RString(), RString(), MAX_PLAYERS, false, GetNetworkPort(), false, RString(), _rating);
#else
  GetNetworkManager().CreateSession(GetSessionType(), RString(), RString(), MAX_PLAYERS, false, GetNetworkPort(), false, RString(), 0);
#endif
  if (GetNetworkManager().IsServer()) CreateChild(new DisplayServer(this));
}

void DisplaySystemLink::OnButtonClicked(int idc)
{
  switch (idc)
  {
#if _ENABLE_MP
    case IDC_MULTI_NEW:
#if defined _XBOX && _XBOX_VER >= 200
      // GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#endif
      HostSession();
      break;
    case IDC_MULTI_JOIN:
    case IDC_OK:
      {
        if (_sessions)
        {
#if defined _XBOX && _XBOX_VER >= 200
          // GSaveSystem.CheckDevice(CHECK_SAVE_BYTES);
#endif
          int sel = _sessions->SelectedSession();
          if (sel >= 0)
          {
            if (_sessions->GetSessionID(sel).GetLength() == 0)
            {
              HostSession();
              break;
            }
#if _XBOX_SECURE
            ConnectResult result = JoinSession(this, GetSessionType(), _sessions->GetSession(sel), false, _rating);
#else
            ConnectResult result = JoinSession(this, GetSessionType(), _sessions->GetSession(sel), false, 0);
#endif
            if (result == CROK) SuspendEnum();
          }
        }
      }
      break;
#endif
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

void DisplaySystemLink::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
    case IDD_SERVER:
    case IDD_CLIENT:
    case IDD_MP_SETUP:
      GetNetworkManager().Close();
      ResumeEnum();
      Display::OnChildDestroyed(idd, exit);
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

void DisplaySystemLink::UpdateSessions()
{
  if (!_sessions) return;
  if (!_enumeration) return;

  RString selGUID;
  int sel = _sessions->SelectedSession();
  if (sel >= 0 && sel < _sessions->NSessions()) selGUID = _sessions->GetSessionID(sel);

  _sessions->_sessions.Resize(0);
  GetNetworkManager().GetSessions(_sessions->_sessions);

  int n = _sessions->_sessions.Size();
  _sessions->_indices.Realloc(n);
  _sessions->_indices.Resize(n);
  for (int i=0; i<n; i++) _sessions->_indices[i] = i;

  sel = 0;
  for (int i=0; i<_sessions->NSessions(); i++)
    if (_sessions->GetSessionID(i) == selGUID)
    {
      sel = i;
      break;
    }
  _sessions->SelectSession(sel);
}

DisplayOptiMatchFilter::DisplayOptiMatchFilter(ControlsContainer *parent)
: Display(parent)
{
  Load("RscDisplayOptiMatchFilter");
  
  if (_gameType)
  {
    int index = _gameType->AddString(LocalizeString(IDS_GAME_TYPE_ANY));
    _gameType->SetData(index, RString());
    ParamEntryVal list = Pars >> "CfgMPGameTypes";
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      int index = _gameType->AddString(entry >> "name");
      _gameType->SetData(index, entry.GetName());
    }
    _gameType->SetCurSel(0);
  }

  if (_minPlayers)
  {
    int index = _minPlayers->AddString(LocalizeString(IDS_NUM_PLAYERS_ANY));
    _minPlayers->SetValue(index, -1);
    index = _minPlayers->AddString("2");
    _minPlayers->SetValue(index, 2);
    index = _minPlayers->AddString("5");
    _minPlayers->SetValue(index, 5);
    index = _minPlayers->AddString("10");
    _minPlayers->SetValue(index, 10);
    index = _minPlayers->AddString("20");
    _minPlayers->SetValue(index, 20);
    index = _minPlayers->AddString("50");
    _minPlayers->SetValue(index, 50);

    _minPlayers->SetCurSel(0);
  }

  if (_maxPlayers)
  {
    int index = _maxPlayers->AddString(LocalizeString(IDS_NUM_PLAYERS_ANY));
    _maxPlayers->SetValue(index, -1);
    index = _maxPlayers->AddString("2");
    _maxPlayers->SetValue(index, 2);
    index = _maxPlayers->AddString("5");
    _maxPlayers->SetValue(index, 5);
    index = _maxPlayers->AddString("10");
    _maxPlayers->SetValue(index, 10);
    index = _maxPlayers->AddString("20");
    _maxPlayers->SetValue(index, 20);
    index = _maxPlayers->AddString("50");
    _maxPlayers->SetValue(index, 50);

    _maxPlayers->SetCurSel(0);
  }

  if (_language)
  {
    int index = _language->AddString(LocalizeString(IDS_LANGUAGE_ANY));
    _language->SetValue(index, -1);
    index = _language->AddString(LocalizeString(IDS_LANGUAGE_ENGLISH));
    _language->SetValue(index, XC_LANGUAGE_ENGLISH);
    index = _language->AddString(LocalizeString(IDS_LANGUAGE_GERMAN));
    _language->SetValue(index, XC_LANGUAGE_GERMAN);
    index = _language->AddString(LocalizeString(IDS_LANGUAGE_FRENCH));
    _language->SetValue(index, XC_LANGUAGE_FRENCH);
    index = _language->AddString(LocalizeString(IDS_LANGUAGE_ITALIAN));
    _language->SetValue(index, XC_LANGUAGE_ITALIAN);
    _language->SetCurSel(0);
  }

  if (_difficulty)
  {
    int index = _difficulty->AddString(LocalizeString(IDS_DIFFICULTY_ANY));
    _difficulty->SetValue(index, -1);
    for (int i=0; i<Glob.config.diffSettings.Size(); i++)
    {
      int index = _difficulty->AddString(Glob.config.diffSettings[i].displayName);
      _difficulty->SetValue(index, i);
    }
    _difficulty->SetCurSel(0);
  }
}

Control *DisplayOptiMatchFilter::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_OPTIMATCH_FILTER_TYPE:
    _gameType = GetListBoxContainer(ctrl);
    break;
  case IDC_OPTIMATCH_FILTER_MIN_PLAYERS:
    _minPlayers = GetListBoxContainer(ctrl);
    break;
  case IDC_OPTIMATCH_FILTER_MAX_PLAYERS:
    _maxPlayers = GetListBoxContainer(ctrl);
    break;
  case IDC_OPTIMATCH_FILTER_LANGUAGE:
    _language = GetListBoxContainer(ctrl);
    break;
  case IDC_OPTIMATCH_FILTER_DIFFICULTY:
    _difficulty = GetListBoxContainer(ctrl);
    break;
  }
  return ctrl;
}

RString DisplayOptiMatchFilter::GetGameType() const
{
  if (_gameType)
  {
    int sel = _gameType->GetCurSel();
    if (sel >= 0) return _gameType->GetData(sel);
  }
  return RString();
}

int DisplayOptiMatchFilter::GetMinPlayers() const
{
  if (_minPlayers)
  {
    int sel = _minPlayers->GetCurSel();
    if (sel >= 0) return _minPlayers->GetValue(sel);
  }
  return -1;
}

int DisplayOptiMatchFilter::GetMaxPlayers() const
{
  if (_maxPlayers)
  {
    int sel = _maxPlayers->GetCurSel();
    if (sel >= 0) return _maxPlayers->GetValue(sel);
  }
  return -1;
}

int DisplayOptiMatchFilter::GetLanguage() const
{
  if (_language)
  {
    int sel = _language->GetCurSel();
    if (sel >= 0) return _language->GetValue(sel);
  }
  return -1;
}

int DisplayOptiMatchFilter::GetDifficulty() const
{
  if (_difficulty)
  {
    int sel = _difficulty->GetCurSel();
    if (sel >= 0) return _difficulty->GetValue(sel);
  }
  return -1;
}

int DisplayOptiMatchFilter::GetMaxDifficulty() const
{
  return -1;
}

int GetPrefferedDifficulty()
{
  int difficulty = Glob.config.diffDefault;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return difficulty;

  ConstParamEntryPtr entry = cfg.FindEntry("difficultyMP");
  if (entry)
  {
    RString name = *entry;
    int diff = Glob.config.diffNames.GetValue(name);
    if (diff >= 0) difficulty = diff;
  }
  return difficulty;
}

static RString FindGameType(int gameTypeInt)
{
  if (gameTypeInt < 0) return RString();

  const ParamEntryVal &cls = Pars >> "CfgMPGameTypes";
  for (int i=0; i<cls.GetEntryCount(); i++)
  {
    const ParamEntryVal &entry = cls.GetEntry(i);
    int id = entry >> "id";
    if (id == gameTypeInt) return entry.GetName();
  }
  return RString();
}

static RString FindWorld(int id)
{
  int n = (Pars >> "CfgWorldList").GetEntryCount();
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = (Pars >> "CfgWorldList").GetEntry(i);
    if (!entry.IsClass()) continue;

    RString name = entry.GetName();
    ConstParamEntryPtr cls = (Pars >> "CfgWorlds").FindEntry(name);
    if (!cls) continue;

    int islandId = (*cls) >> "worldId";
    if (islandId == id) return cls->GetName();
  }

  RptF("Island with id %d not found", id);
  return RString();
}

static RString FindMPMission(int id)
{
  // TODOXNET: Decode mission id
  return RString();
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

static RString FindCountry(int id)
{
  // Country codes by ISO 3166-1 alpha-2
  switch (id)
  {
  case XONLINE_COUNTRY_UNITED_ARAB_EMIRATES:
    return "AE";
  case XONLINE_COUNTRY_ALBANIA:
    return "AL";
  case XONLINE_COUNTRY_ARMENIA:
    return "AM";
  case XONLINE_COUNTRY_ARGENTINA:
    return "AR";
  case XONLINE_COUNTRY_AUSTRIA:
    return "AT";
  case XONLINE_COUNTRY_AUSTRALIA:
    return "AU";
  case XONLINE_COUNTRY_AZERBAIJAN:
    return "AZ";
  case XONLINE_COUNTRY_BELGIUM:
    return "BE";
  case XONLINE_COUNTRY_BULGARIA:
    return "BG";
  case XONLINE_COUNTRY_BAHRAIN:
    return "BH";
  case XONLINE_COUNTRY_BRUNEI_DARUSSALAM:
    return "BN";
  case XONLINE_COUNTRY_BOLIVIA:
    return "BO";
  case XONLINE_COUNTRY_BRAZIL:
    return "BR";
  case XONLINE_COUNTRY_BELARUS:
    return "BY";
  case XONLINE_COUNTRY_BELIZE:
    return "BZ";
  case XONLINE_COUNTRY_CANADA:
    return "CA";
  case XONLINE_COUNTRY_SWITZERLAND:
    return "CH";
  case XONLINE_COUNTRY_CHILE:
    return "CL";
  case XONLINE_COUNTRY_CHINA:
    return "CN";
  case XONLINE_COUNTRY_COLOMBIA:
    return "CO";
  case XONLINE_COUNTRY_COSTA_RICA:
    return "CR";
  case XONLINE_COUNTRY_CZECH_REPUBLIC:
    return "CZ";
  case XONLINE_COUNTRY_GERMANY:
    return "DE";
  case XONLINE_COUNTRY_DENMARK:
    return "DK";
  case XONLINE_COUNTRY_DOMINICAN_REPUBLIC:
    return "DO";
  case XONLINE_COUNTRY_ALGERIA:
    return "DZ";
  case XONLINE_COUNTRY_ECUADOR:
    return "EC";
  case XONLINE_COUNTRY_ESTONIA:
    return "EE";
  case XONLINE_COUNTRY_EGYPT:
    return "EG";
  case XONLINE_COUNTRY_SPAIN:
    return "ES";
  case XONLINE_COUNTRY_FINLAND:
    return "FI";
  case XONLINE_COUNTRY_FAROE_ISLANDS:
    return "FO";
  case XONLINE_COUNTRY_FRANCE:
    return "FR";
  case XONLINE_COUNTRY_GREAT_BRITAIN:
    return "GB";
  case XONLINE_COUNTRY_GEORGIA:
    return "GE";
  case XONLINE_COUNTRY_GREECE:
    return "GR";
  case XONLINE_COUNTRY_GUATEMALA:
    return "GT";
  case XONLINE_COUNTRY_HONG_KONG:
    return "HK";
  case XONLINE_COUNTRY_HONDURAS:
    return "HN";
  case XONLINE_COUNTRY_CROATIA:
    return "HR";
  case XONLINE_COUNTRY_HUNGARY:
    return "HU";
  case XONLINE_COUNTRY_INDONESIA:
    return "ID";
  case XONLINE_COUNTRY_IRELAND:
    return "IE";
  case XONLINE_COUNTRY_ISRAEL:
    return "IL";
  case XONLINE_COUNTRY_INDIA:
    return "IN";
  case XONLINE_COUNTRY_IRAQ:
    return "IQ";
  case XONLINE_COUNTRY_IRAN:
    return "IR";
  case XONLINE_COUNTRY_ICELAND:
    return "IS";
  case XONLINE_COUNTRY_ITALY:
    return "IT";
  case XONLINE_COUNTRY_JAMAICA:
    return "JM";
  case XONLINE_COUNTRY_JORDAN:
    return "JO";
  case XONLINE_COUNTRY_JAPAN:
    return "JP";
  case XONLINE_COUNTRY_KENYA:
    return "KE";
  case XONLINE_COUNTRY_KYRGYZSTAN:
    return "KG";
  case XONLINE_COUNTRY_KOREA:
    return "KR";
  case XONLINE_COUNTRY_KUWAIT:
    return "KW";
  case XONLINE_COUNTRY_KAZAKHSTAN:
    return "KZ";
  case XONLINE_COUNTRY_LEBANON:
    return "LB";
  case XONLINE_COUNTRY_LIECHTENSTEIN:
    return "LI";
  case XONLINE_COUNTRY_LITHUANIA:
    return "LT";
  case XONLINE_COUNTRY_LUXEMBOURG:
    return "LU";
  case XONLINE_COUNTRY_LATVIA:
    return "LV";
  case XONLINE_COUNTRY_LIBYA:
    return "LY";
  case XONLINE_COUNTRY_MOROCCO:
    return "MA";
  case XONLINE_COUNTRY_MONACO:
    return "MC";
  case XONLINE_COUNTRY_MACEDONIA:
    return "MK";
  case XONLINE_COUNTRY_MONGOLIA:
    return "MN";
  case XONLINE_COUNTRY_MACAU:
    return "MO";
  case XONLINE_COUNTRY_MALDIVES:
    return "MV";
  case XONLINE_COUNTRY_MEXICO:
    return "MX";
  case XONLINE_COUNTRY_MALAYSIA:
    return "MY";
  case XONLINE_COUNTRY_NICARAGUA:
    return "NI";
  case XONLINE_COUNTRY_NETHERLANDS:
    return "NL";
  case XONLINE_COUNTRY_NORWAY:
    return "NO";
  case XONLINE_COUNTRY_NEW_ZEALAND:
    return "NZ";
  case XONLINE_COUNTRY_OMAN:
    return "OM";
  case XONLINE_COUNTRY_PANAMA:
    return "PA";
  case XONLINE_COUNTRY_PERU:
    return "PE";
  case XONLINE_COUNTRY_PHILIPPINES:
    return "PH";
  case XONLINE_COUNTRY_PAKISTAN:
    return "PK";
  case XONLINE_COUNTRY_POLAND:
    return "PL";
  case XONLINE_COUNTRY_PUERTO_RICO:
    return "PR";
  case XONLINE_COUNTRY_PORTUGAL:
    return "PT";
  case XONLINE_COUNTRY_PARAGUAY:
    return "PY";
  case XONLINE_COUNTRY_QATAR:
    return "QA";
  case XONLINE_COUNTRY_ROMANIA:
    return "RO";
  case XONLINE_COUNTRY_RUSSIAN_FEDERATION:
    return "RU";
  case XONLINE_COUNTRY_SAUDI_ARABIA:
    return "SA";
  case XONLINE_COUNTRY_SWEDEN:
    return "SE";
  case XONLINE_COUNTRY_SINGAPORE:
    return "SG";
  case XONLINE_COUNTRY_SLOVENIA:
    return "SI";
  case XONLINE_COUNTRY_SLOVAK_REPUBLIC:
    return "SK";
  case XONLINE_COUNTRY_EL_SALVADOR:
    return "SV";
  case XONLINE_COUNTRY_SYRIA:
    return "SY";
  case XONLINE_COUNTRY_THAILAND:
    return "TH";
  case XONLINE_COUNTRY_TUNISIA:
    return "TN";
  case XONLINE_COUNTRY_TURKEY:
    return "TR";
  case XONLINE_COUNTRY_TRINIDAD_AND_TOBAGO:
    return "TT";
  case XONLINE_COUNTRY_TAIWAN:
    return "TW";
  case XONLINE_COUNTRY_UKRAINE:
    return "UA";
  case XONLINE_COUNTRY_UNITED_STATES:
    return "US";
  case XONLINE_COUNTRY_URUGUAY:
    return "UY";
  case XONLINE_COUNTRY_UZBEKISTAN:
    return "UZ";
  case XONLINE_COUNTRY_VENEZUELA:
    return "VE";
  case XONLINE_COUNTRY_VIET_NAM:
    return "VN";
  case XONLINE_COUNTRY_YEMEN:
    return "YE";
  case XONLINE_COUNTRY_SOUTH_AFRICA:
    return "ZA";
  case XONLINE_COUNTRY_ZIMBABWE:
    return "ZW";
  }

  Fail("Unknown country");
  return RString();
}

XLiveQuery::XLiveQuery()
{
  _state = QSInit;
  _resultsSize = 0;
  _qos = NULL;

  // memset(_properties, 0, sizeof(_properties));
  memset(_contexts, 0, sizeof(_contexts));
}

XLiveQuery::~XLiveQuery()
{
  Cancel();
}

bool XLiveQuery::Init(SessionType sessionType)
{
  // Prepare the query parameters
  _userIndex = GSaveSystem.GetUserIndex();

  _contexts[0].dwContextId = X_CONTEXT_GAME_TYPE;
  if (sessionType == STRanked)
  {
    _queryIndex = SESSION_MATCH_QUERY_STANDARDMATCH;
    _contexts[0].dwValue = X_CONTEXT_GAME_TYPE_RANKED;
  }
  else
  {
    _queryIndex = SESSION_MATCH_QUERY_STANDARDMATCH;
    Assert(sessionType == STStandard);
    _contexts[0].dwValue = X_CONTEXT_GAME_TYPE_STANDARD;
  }
  _contexts[1].dwContextId = X_CONTEXT_GAME_MODE;
  _contexts[1].dwValue = CONTEXT_GAME_MODE_MP;

  _state = QSInit;

  // data are prepared, start the query
  return Start();
}

bool XLiveQuery::Start()
{
  Assert(_state != QSSize && _state != QSQuery);
  if (_qos)
  {
    XNetQosRelease(_qos);
    _qos = NULL;
  }

  // start to query the output buffer size
  _resultsSize = 0;
  memset(&_overlapped, 0, sizeof(_overlapped));
  HRESULT result = XSessionSearch(_queryIndex, _userIndex, _resultsCount, 0, lenof(_contexts), NULL, _contexts, &_resultsSize, NULL, &_overlapped);
  if (FAILED(result))
  {
    RptF("XSessionSearch failed - error 0x%x", result);
    return false;
  }

  _state = QSSize;
  return true;
}

void XLiveQuery::Cancel()
{
  switch (_state)
  {
  case QSSize:
  case QSQuery:
    XCancelOverlapped(&_overlapped);
    break;
  }
  if (_qos)
  {
    XNetQosRelease(_qos);
    _qos = NULL;
  }
  _state = QSInit;
}

XLiveQueryState XLiveQuery::Process()
{
  switch (_state)
  {
  case QSSize:
    {
      if (!XHasOverlappedIoCompleted(&_overlapped)) return _state;
      DWORD result = XGetOverlappedExtendedError(&_overlapped);
      if (FAILED(result))
      {
        RptF("Overlapped operation %d failed - error 0x%x", _state, result);
        _state = QSFailed;
        break;
      }

      // prepare the output buffer
      _results.Init(_resultsSize);
      memset(_results.Data(), 0, _resultsSize);

      // process the query
      memset(&_overlapped, 0, sizeof(_overlapped));
      XSESSION_SEARCHRESULT_HEADER *header = (XSESSION_SEARCHRESULT_HEADER *)_results.Data();
      result = XSessionSearch(_queryIndex, _userIndex, _resultsCount, 0, lenof(_contexts), NULL, _contexts, &_resultsSize, header, &_overlapped);
      if (FAILED(result))
      {
        RptF("XSessionSearch failed - error 0x%x", result);
        _state = QSFailed;
        break;
      }

      _state = QSQuery;
    }
    break;
  case QSQuery:
    {
      if (!XHasOverlappedIoCompleted(&_overlapped)) return _state;
      DWORD result = XGetOverlappedExtendedError(&_overlapped);
      if (FAILED(result))
      {
        RptF("Overlapped operation %d failed - error 0x%x", _state, result);
        _state = QSFailed;
        break;
      }

      _state = QSSucceeded;
    }
    break;
  case QSProbe:
    {
      if (_qos)
      {
        if (_qos->cxnqosPending == 0) _state = QSProbeSucceeded;
      }
      else _state = QSFailed;
    }
    break;
  }
  
  return _state;
}

bool XLiveQuery::Read(AutoArray<SessionInfo> &sessions)
{
  if (_state != QSSucceeded) return false;
  _state = QSInit;

  // copy the results to sessions
  XSESSION_SEARCHRESULT_HEADER *header = (XSESSION_SEARCHRESULT_HEADER *)_results.Data();
  int n = header->dwSearchResults;
  sessions.Realloc(n);
  sessions.Resize(n);
  for (int i=0; i<n; i++)
  {
    const XSESSION_SEARCHRESULT &src = header->pResults[i];
    SessionInfo &dst = sessions[i];

    dst.addr = src.info.hostAddress;
    dst.kid = src.info.sessionID;
    dst.key = src.info.keyExchangeKey;
    dst.port = 1000;

    dst.playersPublic = src.dwFilledPublicSlots;
    dst.slotsPublic = src.dwOpenPublicSlots + src.dwFilledPublicSlots;
    dst.playersPrivate = src.dwFilledPrivateSlots;
    dst.slotsPrivate = src.dwOpenPrivateSlots + src.dwFilledPrivateSlots;

    dst.password = false;
    dst.badActualVersion = false;
    dst.badRequiredVersion = false;
    dst.badSignatures = false;
    dst.badAdditionalMods = false;
    dst.badMod = false;
    dst.ping = -1;
    dst.lastTime = 0xffffffff;
    dst.platform = "Xbox";
    dst.dedicated = false;

    for (DWORD j=0; j<src.cProperties; j++)
    {
      const XUSER_PROPERTY &property = src.pProperties[j];
      switch (property.dwPropertyId)
      {
      case PROPERTY_GAME_VERSION:
        Assert(property.value.type == XUSER_DATA_TYPE_INT32);
        dst.actualVersion = property.value.nData;
        break;
      case PROPERTY_GAME_REQUIRED_VERSION:
        Assert(property.value.type == XUSER_DATA_TYPE_INT32);
        dst.requiredVersion = property.value.nData;
        break;
      case PROPERTY_GAME_TIMELEFT:
        Assert(property.value.type == XUSER_DATA_TYPE_INT32);
        dst.timeleft = property.value.nData;
        break;
      case X_PROPERTY_GAMER_HOSTNAME:
        Assert(property.value.type == XUSER_DATA_TYPE_UNICODE);
        dst.name = FromWideChar(property.value.string.pwszData);
        break;
      case X_PROPERTY_GAMER_LANGUAGE:
        Assert(property.value.type == XUSER_DATA_TYPE_INT32);
        dst.language = property.value.nData;
        break;
      case X_PROPERTY_GAMER_COUNTRY:
        Assert(property.value.type == XUSER_DATA_TYPE_INT32);
        dst.country = FindCountry(property.value.nData);
        break;
      }
    }
    for (DWORD j=0; j<src.cContexts; j++)
    {
      const XUSER_CONTEXT &context = src.pContexts[j];
      switch (context.dwContextId)
      {
      case CONTEXT_GAME_MPTYPE:
        dst.gameType = FindGameType(context.dwValue);
        break;
      case CONTEXT_GAME_WORLD:
        dst.island = FindWorld(context.dwValue);
        break;
      case CONTEXT_GAME_DIFFICULTY:
        dst.difficulty = context.dwValue;
        break;
      case CONTEXT_GAME_MPMISSION:
        dst.mission = FindMPMission(context.dwValue);
        break;
      case CONTEXT_GAME_SERVERSTATE:
        dst.serverState = context.dwValue;
        break;
      }
    }
  }

  Probe();

  return true;
}

TypeIsSimple(const XNADDR *);
TypeIsSimple(const XNKID *);
TypeIsSimple(const XNKEY *);

bool XLiveQuery::Probe()
{
  XSESSION_SEARCHRESULT_HEADER *header = (XSESSION_SEARCHRESULT_HEADER *)_results.Data();
  if (!header) return false;
  int n = header->dwSearchResults;
  if (n == 0) return true;

  // cancel current probing
  if (_qos)
  {
    XNetQosRelease(_qos);
    _qos = NULL;
  }

  AUTO_STATIC_ARRAY(const XNADDR *, addr, 64);
  AUTO_STATIC_ARRAY(const XNKID *, kid, 64);
  AUTO_STATIC_ARRAY(const XNKEY *, key, 64);

  addr.Resize(n);
  kid.Resize(n);
  key.Resize(n);

  for (int i=0; i<n; i++)
  {
    const XSESSION_SEARCHRESULT &src = header->pResults[i];
    addr[i] = &src.info.hostAddress;
    kid[i] = &src.info.sessionID;
    key[i] = &src.info.keyExchangeKey;
  }

  static const int qosProbes = 8;
  static const int qosBps = 64000; // client is looking for the session, no need to limit

  int result = XNetQosLookup(n, addr.Data(), kid.Data(), key.Data(), 0, NULL, NULL, qosProbes, qosBps, 0, NULL, &_qos);
  if (result != ERROR_SUCCESS) return false;

  _state = QSProbe;
  return true;
}

bool XLiveQuery::Update(AutoArray<SessionInfo> &sessions)
{
  if (_state != QSProbe && _state != QSProbeSucceeded) return false;
  if (_state == QSProbeSucceeded) _state = QSInit; // done, can restart query or probe

  int n = sessions.Size();
  if (!_qos || _qos->cxnqos != n) return false;

  for (int i=0; i<n; i++)
  {
    const XNQOSINFO &info = _qos->axnqosinfo[i];
    if (!(info.bFlags & XNET_XNQOSINFO_TARGET_CONTACTED) || (info.bFlags & XNET_XNQOSINFO_TARGET_DISABLED)) continue;

    SessionInfo &dst = sessions[i];
    if ((info.bFlags & XNET_XNQOSINFO_COMPLETE) || (info.bFlags & XNET_XNQOSINFO_PARTIAL_COMPLETE))
    {
      dst.ping = info.wRttMedInMsecs;
    }
    if ((info.bFlags & XNET_XNQOSINFO_DATA_RECEIVED) && info.cbData == sizeof(QosInfo))
    {
      const QosInfo *src = (const QosInfo *)info.pbData;
      dst.gameType = (const char *)src->gameType;
      dst.mission = src->GetLocalizedMissionName();
      dst.island = (const char *)src->island;
      dst.serverState = src->serverState;
      dst.playersPublic = src->playersPublic;
      dst.slotsPublic = src->slotsPublic;
      dst.playersPrivate = src->playersPrivate;
      dst.slotsPrivate = src->slotsPrivate;
      dst.timeleft = src->timeleft;
      dst.difficulty = src->difficulty;
    }
  }

  return true;
}

#endif

DisplayOptiMatch::DisplayOptiMatch(ControlsContainer *parent, SessionType sessionType, RString gameType, int minPlayers, int maxPlayers, int language, int difficulty, int maxDifficulty)
: base(parent, false)
{
  _sessionType = sessionType;
  _suspended = 0;

  _minPlayers = minPlayers > 0 ? minPlayers : QueryValueNone;
  _maxPlayers = maxPlayers > 0 ? maxPlayers : QueryValueNone;
  _gameType = QueryValueNone;
  _language = language >= 0 ? language : QueryValueNone;
  _difficulty = difficulty >= 0 ? difficulty : QueryValueNone;
  _maxDifficulty = maxDifficulty >= 0 ? maxDifficulty : QueryValueNone;
  _prefferedDifficulty = GetPrefferedDifficulty();

  if (gameType.GetLength() > 0)
  {
    ConstParamEntryPtr cls = (Pars >> "CfgMPGameTypes").FindEntry(gameType);
    if (cls)
    {
      int id = *cls >> "id";
      if (id >= 0) _gameType = id;
    }
  }

#if _XBOX_VER >= 200
  // initialize the Xbox Live query
  _query.Init(sessionType);
#elif _XBOX_SECURE && _ENABLE_MP
  _rating = ReadPlayerRating();

  HRESULT result = _query.Query(
    _rating, // Rating
    RoughRating(_rating), // RoughRating
    _gameType, // WantedTypeInt
    _minPlayers, // MinPlayers
    _maxPlayers, // MaxPlayers
    _language, // Language
    _difficulty, // Difficulty
    _maxDifficulty, // MaxDifficulty
    NSSAssigningPlayers, // ServerState - preferred game state
    GetGameLanguage(), // SortLanguage - preferred language
    _prefferedDifficulty // SortDifficulty - TODO: valid value
   );
  if (FAILED(result))
  {
    RptF("Cannot start OptiMatch query - error 0x%x", result);
  }
#endif
  
  IControl *ctrl = GetCtrl(IDC_MULTI_TITLE);
  ITextContainer *text = GetTextContainer(ctrl);
  if (text) text->SetText(LocalizeString(IDS_DISP_MAIN_XBOX_MULTI_OPTIMATCH));

  UpdateKeyHints();
}

DisplayOptiMatch::~DisplayOptiMatch()
{
}

void DisplayOptiMatch::SuspendEnum()
{
#if _XBOX_VER >= 200
  _query.Cancel();
#elif _XBOX_SECURE && _ENABLE_MP
  _query.Cancel();
#endif
  _suspended++;
}

void DisplayOptiMatch::ResumeEnum()
{
  // resume will be 
  _suspended--;

  if (_suspended == 0)
  {
#if _XBOX_VER >= 200
    _query.Start();
#elif _XBOX_SECURE && _ENABLE_MP
    HRESULT result = _query.Query(
      _rating, // Rating
      RoughRating(_rating), // RoughRating
      _gameType, // WantedTypeInt
      _minPlayers, // MinPlayers
      _maxPlayers, // MaxPlayers
      _language, // Language
      _difficulty, // Difficulty
      _maxDifficulty, // MaxDifficulty
      NSSAssigningPlayers, // ServerState - preferred game state
      GetGameLanguage(), // SortLanguage - preferred language
      _prefferedDifficulty // SortDifficulty - TODO: valid value
      );
    if (FAILED(result))
    {
      RptF("Cannot start OptiMatch query - error 0x%x", result);
    }
#endif
  }
}

void DisplayOptiMatch::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_MULTI_SESSIONS)
    OnSessionChanged();
  else
    base::OnLBSelChanged(ctrl, curSel);
}

void DisplayOptiMatch::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_MULTI_REFRESH:
#if _XBOX_VER >= 200
    if (_query.GetState() != QSSize && _query.GetState() != QSQuery)
      _query.Start();
#elif _XBOX_SECURE && _ENABLE_MP
    if (!_query.IsRunning())
    {
      SuspendEnum();
      ResumeEnum();
    }
#endif
    break;
  default:
    base::OnButtonClicked(idc);
    break;
  }
}

void DisplayOptiMatch::UpdateButtons()
{
#if _XBOX_SECURE && _ENABLE_MP
  IControl *ctrl = GetCtrl(IDC_MULTI_REFRESH);
  if (ctrl) ctrl->EnableCtrl(_query.GetState() != QSSize && _query.GetState() != QSQuery);
#endif
}

void DisplayOptiMatch::OnSimulate(EntityAI *vehicle)
{
#if _XBOX_SECURE && _ENABLE_MP
  if (!GetNetworkManager().IsSignedIn())
  {
    Exit(IDC_CANCEL);
    return;
  }
#endif

  // avoid session update performed in DisplaySystemLink::OnSimulate
  Display::OnSimulate(vehicle);
  
  if (_suspended > 0) return;
  if (!_sessions) return;

#if _XBOX_VER >= 200
  XLiveQueryState state = _query.Process();
  if (state == QSSucceeded)
  {
    _query.Read(_sessions->_sessions);

    int n = _sessions->_sessions.Size();
    _sessions->_indices.Realloc(n);
    _sessions->_indices.Resize(n);
    for (int i=0; i<n; i++) _sessions->_indices[i] = i;

    OnSessionChanged();
  }
  else if (state == QSProbe)
  {
    // partial results ready
    _query.Update(_sessions->_sessions);
  }
  else if (state == QSProbeSucceeded)
  {
    // final results ready
    _query.Update(_sessions->_sessions);
    _nextQosTime = Glob.uiTime + 15; // start next query after 15 s
  }
  else if (state == QSFailed)
  {
    _query.Cancel();
  }
  else if (state == QSInit && Glob.uiTime > _nextQosTime)
  {
    _query.Probe();
  }
#elif _XBOX_SECURE && _ENABLE_MP
  if (_query.IsRunning())
  {
    HRESULT result = _query.Process();
    if (FAILED(result))
    {
      RptF("Cannot continue with OptiMatch query - error 0x%x", result);
    }
    // MS code bug: _query.Process can return 0 (XONLINETASK_S_RUNNING)
    // when query if finished and no sessions found
    else if (result != XONLINETASK_S_RUNNING || !_query.IsRunning())
    {
      RString selected;
      if (_sessions->NSessions() > 0)
      {
        int sel = _sessions->SelectedSession();
        selected = _sessions->GetSessionID(sel);
      }
      // Transfer sessions into list
      int n = _query.Results.Size();
      _sessions->_sessions.Resize(n);
      for (int i=0; i<n; i++)
      {
        const COptiMatchResult &src = _query.Results[i];
        SessionInfo &dst = _sessions->_sessions[i];

        dst.addr = src.HostAddress;
        dst.kid = src.SessionID;
        dst.key = src.KeyExchangeKey;
        dst.port = 1000;
        dst.playersPublic = src.PublicFilled;
        dst.slotsPublic = src.PublicOpen + src.PublicFilled;
        dst.playersPrivate = src.PrivateFilled;
        dst.slotsPrivate = src.PrivateOpen + src.PrivateFilled;

        dst.name = FromWideChar(src.Host);
        dst.gameType = FindGameType(src.GameTypeInt);
        dst.mission = DecodeLocalizedString(src.Mission).GetLocalizedValue();
        dst.island = FromWideChar(src.Island);
        dst.serverState = src.ServerState;
        dst.actualVersion = src.ActualVersion;
        dst.requiredVersion = src.RequiredVersion;
        dst.timeleft = src.TimeLeft;

        dst.password = false;
        dst.badActualVersion = false;
        dst.badRequiredVersion = false;
        dst.badAdditionalMods = false;
        dst.badSignatures = false;
        dst.badMod = false;
        dst.ping = -1;
        dst.platform = "Xbox";
        dst.lastTime = 0xffffffff;

        dst.dedicated = false;
        dst.language = src.Language;
        dst.difficulty = src.Difficulty;
        dst.country = RString();
      }

      n = _sessions->_sessions.Size();
      _sessions->_indices.Realloc(n);
      _sessions->_indices.Resize(n);
      for (int i=0; i<n; i++) _sessions->_indices[i] = i;

      int sel = 0;
      for (int i=0; i<_sessions->NSessions(); i++)
        if (_sessions->GetSessionID(i) == selected)
        {
          sel = i;
          break;
        }
      _sessions->SelectSession(sel);
      OnSessionChanged();

      _query.Probe();
    }
  }
  else if (_query.IsProbing())
  {
    HRESULT result = _query.Process();
    if (FAILED(result))
    {
      RptF("Cannot continue with OptiMatch query - error 0x%x", result);
      _nextQosTime = Glob.uiTime + 15; // start next query after 15 s
    }
    else if (result != XONLINETASK_S_RUNNING)
    {
      int n = _query.Results.Size();
      DoAssert(_sessions->_sessions.Size() == n);

      // save current selection
      RString selected;
      if (_sessions->NSessions() > 0)
      {
        int sel = _sessions->SelectedSession();
        selected = _sessions->GetSessionID(sel);
      }

      // add only valid sessions
      _sessions->_indices.Resize(0);

      for (int i=0; i<n; i++)
      {
        const XNQOSINFO *info = _query.Results[i].pQosInfo;
        if (!info) continue;

        // not exist or disabled
        if (!(info->bFlags & XNET_XNQOSINFO_TARGET_CONTACTED) || (info->bFlags & XNET_XNQOSINFO_TARGET_DISABLED)) continue;
        
        _sessions->_indices.Add(i);

        SessionInfo &dst = _sessions->_sessions[i];
        dst.ping = info->wRttMedInMsecs;
        if (info->cbData == sizeof(QosInfo))
        {
          const QosInfo *src = (const QosInfo *)info->pbData;
          dst.gameType = (const char *)src->gameType;
          dst.mission = src->GetLocalizedMissionName();
          dst.island = (const char *)src->island;
          dst.serverState = src->serverState;
          dst.playersPublic = src->playersPublic;
          dst.slotsPublic = src->slotsPublic;
          dst.playersPrivate = src->playersPrivate;
          dst.slotsPrivate = src->slotsPrivate;
          dst.timeleft = src->timeleft;
          dst.difficulty = src->difficulty;
        }
      }

      // restore selection
      int sel = 0;
      for (int i=0; i<_sessions->NSessions(); i++)
        if (_sessions->GetSessionID(i) == selected)
        {
          sel = i;
          break;
        }
      _sessions->SelectSession(sel);
      OnSessionChanged();

      _nextQosTime = Glob.uiTime + 15; // start next query after 15 s
    }
  }
  else if (Glob.uiTime > _nextQosTime)
  {
    Assert(_query.Done());
    _query.Probe();
  }
#endif
  UpdateButtons();
}

DisplayQuickMatch::DisplayQuickMatch(ControlsContainer *parent, SessionType sessionType)
: Display(parent)
{
  Load("RscDisplayQuickMatch");
#if _XBOX_VER >= 200
  // Ranking is handled automatically for Ranked Matches
#elif _XBOX_SECURE && _ENABLE_MP
  _rating = ReadPlayerRating();
#endif
  _sessionType = sessionType;
  InitSessions();
#if _XBOX_SECURE && _ENABLE_MP
  if (_sessions.Size() == 0)
  {
    Exit(IDC_MP_NO_SESSION);
    return;
  }
#endif
  if (_sessions.Size() == 1)
  {
    IControl *ctrl = GetCtrl(IDC_MP_NEXT_SESSION);
    if (ctrl) ctrl->EnableCtrl(false);
  } 
  _selected = 0;
  UpdateSession();
}

DisplayQuickMatch::~DisplayQuickMatch()
{
}

void DisplayQuickMatch::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
#if _XBOX_SECURE && _ENABLE_MP
    {
      ULONGLONG rating = 0;
# if _XBOX_VER >= 200
      // Ranking is handled automatically for Ranked Matches
# else
      _query.Cancel();
      rating = _rating;
# endif
      Assert(_selected < _sessions.Size());
      // join session
      JoinSession(this, _sessionType, _sessions[_selected], false, rating);
    }
#endif //_XBOX_SECURE && _ENABLE_MP
    break;
  case IDC_MP_NEXT_SESSION:
    if (_sessions.Size() > 1)
    {
      _selected++;
      if (_selected >= _sessions.Size()) _selected = 0;
      UpdateSession();
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayQuickMatch::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
    case IDD_SERVER:
    case IDD_CLIENT:
    case IDD_MP_SETUP:
      GetNetworkManager().Close();
      // return to main Xbox Live menu
      Exit(IDC_CANCEL);
      break;
  }
}

void DisplayQuickMatch::OnSimulate(EntityAI *vehicle)
{
#if _XBOX_SECURE && _ENABLE_MP
  if (!GetNetworkManager().IsSignedIn())
  {
    Exit(IDC_CANCEL);
    return;
  }
#endif

  Display::OnSimulate(vehicle);

#if _XBOX_VER >= 200
  // Update the session using QoS
  XLiveQueryState state = _query.Process();
  if (state == QSProbe)
  {
    // partial results ready
    if (_query.Update(_sessions)) UpdateSession();
  }
  else if (state == QSProbeSucceeded)
  {
    // final results ready
    if (_query.Update(_sessions)) UpdateSession();
    _nextQosTime = Glob.uiTime + 15; // start next query after 15 s
  }
  else if (state == QSInit && Glob.uiTime > _nextQosTime)
  {
    _query.Probe();
  }
#elif _XBOX_SECURE && _ENABLE_MP
  if (_query.IsProbing())
  {
    HRESULT result = _query.Process();
    if (FAILED(result))
    {
      RptF("Cannot continue with Quick Match query - error 0x%x", result);
      _nextQosTime = Glob.uiTime + 15; // start next query after 15 s
    }
    else if (result != XONLINETASK_S_RUNNING)
    {
      int n = _query.Results.Size();
      Assert(n == _sessions.Size());
      for (int i=0; i<n; i++)
      {
        const XNQOSINFO *info = _query.Results[i].pQosInfo;
        if (!info) continue;

        SessionInfo &dst = _sessions[i];
        dst.ping = info->wRttMedInMsecs;
        if (info->cbData == sizeof(QosInfo))
        {
          const QosInfo *src = (const QosInfo *)info->pbData;
          dst.gameType = (const char *)src->gameType;
          dst.mission = src->GetLocalizedMissionName();
          dst.island = (const char *)src->island;
          dst.serverState = src->serverState;
          dst.playersPublic = src->playersPublic;
          dst.slotsPublic = src->slotsPublic;
          dst.playersPrivate = src->playersPrivate;
          dst.slotsPrivate = src->slotsPrivate;
          dst.timeleft = src->timeleft;
          dst.difficulty = src->difficulty;
        }
      }
      UpdateSession();
      _nextQosTime = Glob.uiTime + 15; // start next query after 15 s
    }
  }
  else if (Glob.uiTime > _nextQosTime)
  {
    if (_query.Done()) // avoid probing when session is running
      _query.Probe();
  }
#endif
}

void DisplayQuickMatch::UpdateSession()
{
  if (_sessions.Size() <= 0) return;

  Assert(_selected < _sessions.Size())
  const SessionInfo &session = _sessions[_selected];

  IControl *ctrl = GetCtrl(IDC_MP_HOST);
  CStructuredText *text = GetStructuredText(ctrl);
  if (text)
  {
    text->SetRawText(session.name);
  }
  ctrl = GetCtrl(IDC_MP_TYPE);
  text = GetStructuredText(ctrl);
  if (text)
  {
    RString type = session.gameType;
    RString typeName;
    if (type.GetLength() > 0)
    {
      ConstParamEntryPtr entry = (Pars >> "CfgMPGameTypes").FindEntry(type);
      if (entry) typeName = (*entry) >> "name";
      else typeName = Pars >> "CfgMPGameTypes" >> "Unknown" >> "name";
    }
    text->SetText(typeName);
  }
  ctrl = GetCtrl(IDC_MP_MISSION);
  text = GetStructuredText(ctrl);
  if (text)
  {
    text->SetRawText(session.mission);
  }
  ctrl = GetCtrl(IDC_MP_ISLAND);
  text = GetStructuredText(ctrl);
  if (text)
  {
    RString island;
    if (session.island.GetLength() > 0)
      island = Pars >> "CfgWorlds" >> session.island  >> "description";
    text->SetText(island);
  }
  ctrl = GetCtrl(IDC_MP_STATE);
  text = GetStructuredText(ctrl);
  if (text)
  {
    RString state;
    if (!session.badActualVersion && !session.badRequiredVersion)
      switch (session.serverState)
      {
        case NSSSelectingMission:
          state = LocalizeString(IDS_SESSION_CREATE);
          break;
        case NSSEditingMission:
          state = LocalizeString(IDS_SESSION_EDIT);
          break;
        case NSSAssigningPlayers:
          state = LocalizeString(IDS_SESSION_WAIT);
          break;
        case NSSSendingMission:
        case NSSLoadingGame:
          state = LocalizeString(IDS_SESSION_SETUP);
          break;
        case NSSPlaying:
          state = LocalizeString(IDS_SESSION_PLAY);
          break;
        case NSSMissionAborted:
          state = LocalizeString(IDS_SESSION_DEBRIEFING);
          break;
      }
    text->SetText(state);
  }
  ctrl = GetCtrl(IDC_MP_TIMELEFT);
  text = GetStructuredText(ctrl);
  if (text)
  {
    RString timeLeft;
    if (session.mission.GetLength() > 0 && session.timeleft > 0)
      timeLeft = Format(LocalizeString(IDS_TIME_LEFT), session.timeleft);
    text->SetText(timeLeft);
  }
  ctrl = GetCtrl(IDC_MP_SLOTS_PUBLIC);
  text = GetStructuredText(ctrl);
  if (text)
  {
    RString slots;
    if (session.slotsPublic)
      slots = Format("%d/%d", session.playersPublic, session.slotsPublic);
    else
      slots = Format("%d", session.playersPublic);
    text->SetText(slots);
  }
#if _XBOX_SECURE && _ENABLE_MP
  ctrl = GetCtrl(IDC_MP_SLOTS_PRIVATE);
  text = GetStructuredText(ctrl);
  if (text)
  {
    RString slots;
    if (session.slotsPrivate)
      slots = Format("%d/%d", session.playersPrivate, session.slotsPrivate);
    else
      slots = Format("%d", session.playersPrivate);
    text->SetText(slots);
  }
#endif
  ctrl = GetCtrl(IDC_MP_LANGUAGE);
  text = GetStructuredText(ctrl);
  if (text)
  {
#if _XBOX_SECURE && _ENABLE_MP
    if (session.language >= 0)
      text->SetText(GetGameLanguageName(session.language));
    else
#endif
      text->SetText(RString());
  }
  ctrl = GetCtrl(IDC_MP_DIFFICULTY);
  text = GetStructuredText(ctrl);
  if (text)
  {
    if (session.difficulty >= 0)
      text->SetText(Glob.config.diffSettings[session.difficulty].displayName);
    else
      text->SetText(RString());
  }
  
  UpdateKeyHints();
}

void DisplayQuickMatch::InitSessions()
{
#if _XBOX_VER >= 200

  // enumerate the sessions using the helper class XLiveQuery
  _query.Init(_sessionType);

  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
  XLiveQueryState state;
  do
  {
    state = _query.Process();
    progress.Refresh();
  } while (state != QSSucceeded && state != QSFailed);

  if (state == QSSucceeded) _query.Read(_sessions);

#elif _XBOX_SECURE && _ENABLE_MP
  // Start sessions enumeration
  HRESULT result = _query.Query(
    _rating, // Rating
    RoughRating(_rating), // RoughRating
    X_MATCH_NULL_INTEGER, // WantedTypeInt
    X_MATCH_NULL_INTEGER, // MinPlayers
    X_MATCH_NULL_INTEGER, // MaxPlayers
    X_MATCH_NULL_INTEGER, // Language
    X_MATCH_NULL_INTEGER, // Difficulty
    X_MATCH_NULL_INTEGER, // MaxDifficulty
    NSSAssigningPlayers, // ServerState - preferred game state
    GetGameLanguage(), // SortLanguage - preferred language
    GetPrefferedDifficulty() // SortDifficulty - TODO: valid value
    );
  if (FAILED(result))
  {
    RptF("Cannot start Quick Match query - error 0x%x", result);
    return;
  }

  // Wait until task is done 
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));
  do
  {
    result = _query.Process();
    ProgressRefresh();
  } while (result == XONLINETASK_S_RUNNING);
  ProgressFinish(p);

  // Handle errors
  if (FAILED(result))
  {
    RptF("Cannot continue with Quick Match query - error 0x%x", result);
    return;
  }

  // Transfer sessions into list
  int n = _query.Results.Size();
  _sessions.Resize(n);
  for (int i=0; i<n; i++)
  {
    const COptiMatchResult &src = _query.Results[i];
    SessionInfo &dst = _sessions[i];

    dst.addr = src.HostAddress;
    dst.kid = src.SessionID;
    dst.key = src.KeyExchangeKey;
    dst.port = 1000;
    dst.playersPublic = src.PublicFilled;
    dst.slotsPublic = src.PublicOpen + src.PublicFilled;
    dst.playersPrivate = src.PrivateFilled;
    dst.slotsPrivate = src.PrivateOpen + src.PrivateFilled;

    dst.name = FromWideChar(src.Host);
    dst.gameType = FindGameType(src.GameTypeInt);
    dst.mission = DecodeLocalizedString(src.Mission).GetLocalizedValue();
    dst.island = FromWideChar(src.Island);
    dst.serverState = src.ServerState;
    dst.actualVersion = src.ActualVersion;
    dst.requiredVersion = src.RequiredVersion;
    dst.timeleft = src.TimeLeft;

    dst.password = false;
    dst.badActualVersion = false;
    dst.badRequiredVersion = false;
    dst.badAdditionalMods = false;
    dst.badSignatures = false;
    dst.badMod = false;
    dst.ping = -1;
    dst.platform = "Xbox";
    dst.lastTime = 0xffffffff;

    dst.dedicated = false;
    dst.language = src.Language;
    dst.difficulty = src.Difficulty;
    dst.country = RString();
  }

  _query.Probe();
#endif
}

#if _XBOX_SECURE && _ENABLE_MP

// Must be global - used for Accepted Same Title Game Invitation
XONLINE_FRIEND GSelectedFriend;

// Friends, feedback and invitations are handled by Xbox Guide on Xbox 360
#if _XBOX_VER < 200

// Must be global - used for Accepted Other Title Game Invitation
XONLINE_ACCEPTED_GAMEINVITE GGameInvite;
bool GInvited;

//! Control for displaying list of friends
class CFriends : public CListBox
{
friend class DisplayFriends;

protected:
  int _friendsCount;
  XONLINE_FRIEND _friends[MAX_FRIENDS];

  Ref<Texture> _communicatorMUTED;
  Ref<Texture> _communicatorON;
  Ref<Texture> _communicatorTV;
  Ref<Texture> _friendInviteReceived;
  Ref<Texture> _friendInviteSent;
  Ref<Texture> _friendOnline;
  Ref<Texture> _gameInviteReceived;
  Ref<Texture> _gameInviteSent;

public:
  CFriends(ControlsContainer *parent, int idc, ParamEntryPar cls);

  int GetSize() const {return _friendsCount;}
  // multiple selection is not allowed
  bool IsSelected(int row, int style) const {return row == GetCurSel();}

  void DrawItem
  (
    UIViewport *vp, float alpha, int i, bool selected, float top,
    const Rect2DFloat &rect, float widthDiff
  ) override;
};

CFriends::CFriends(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CListBox(parent, idc, cls)
{
  _friendsCount = 0;

  _communicatorMUTED = GlobLoadTextureUI("xmisc\\communicatormuted.paa");
  _communicatorON = GlobLoadTextureUI("xmisc\\communicatoron.paa");
  _communicatorTV = GlobLoadTextureUI("xmisc\\communicatortv.paa");
  _friendInviteReceived = GlobLoadTextureUI("xmisc\\friendinvitereceived.paa");
  _friendInviteSent = GlobLoadTextureUI("xmisc\\friendinvitesent.paa");
  _friendOnline = GlobLoadTextureUI("xmisc\\friendonline.paa");
  _gameInviteReceived = GlobLoadTextureUI("xmisc\\gameinvitereceived.paa");
  _gameInviteSent = GlobLoadTextureUI("xmisc\\gameinvitesent.paa");
}

void CFriends::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top,
  const Rect2DFloat &rect, float widthDiff
)
{
  const XONLINE_FRIEND &f = _friends[i];

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();
  const float border = TEXT_BORDER;
  bool textures = (_style & LB_TEXTURES) != 0;

  PackedColor color;
  if (selected && _showSelected)
  {
    PackedColor bgColor = ModAlpha(_selBgColor, alpha);
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(NULL, 0, 0);
    float xx = rect.x * w;
    float ww = rect.w * w;
    if (textures)
    {
      const int borderWidth = 2;
      xx += borderWidth;
      ww -= 2 * borderWidth;
    }
    GLOB_ENGINE->Draw2D
    (
      mip, bgColor, Rect2DPixel(xx, top * h, ww, _rowHeight * h),
      Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
    );
    color = ModAlpha(GetActiveColor(), alpha);
  }
  else
  {
    color = ModAlpha(GetTextColor(), alpha);
  }
  float left = _x + border;
  
  // voice icon
  Texture *texture = NULL;
  if (GetNetworkManager().IsPlayerInMuteList(f.xuid))
  {
    texture = _communicatorMUTED;
  }
  else if (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_VOICE)
  {
    texture = _communicatorON;
  }
  else
  {
    // no icon
  }
  MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
  float width = 0.75 * _rowHeight;
  if (texture) GEngine->Draw2D
  (
    mip, PackedWhite,
    Rect2DPixel(CX(left), CY(h), CW(width), CH(_rowHeight)),
    Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
  );
  left += width;

  // state icon
  if (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDINVITE)
  {
    texture = _gameInviteReceived;
  }
  else if
  (
    (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTINVITE) &&
    !(f.dwFriendState & (XONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED | XONLINE_FRIENDSTATE_FLAG_INVITEREJECTED))
  )
  {
    texture = _gameInviteSent;
  }
  else if (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST)
  {
    texture = _friendInviteReceived;
  }
  else if (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST)
  {
    texture = _friendInviteSent;
  }
  else if (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_ONLINE)
  {
    texture = _friendOnline;
  }
  else 
  {
    texture = NULL;
  }
  mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
  width = 0.75 * _rowHeight;
  if (texture) GEngine->Draw2D
  (
    mip, PackedWhite,
    Rect2DPixel(CX(left), CY(top), CW(width), CH(_rowHeight)),
    Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
  );
  left += width;

  float t = top + 0.5 * (_rowHeight - _size);
  float rest = rect.x + rect.w - left;
  Rect2DFloat clipRect;
  clipRect.y = rect.y;
  clipRect.h = rect.h;
  RString text;
  float column;

  // gamertag
  column = 1.0 * rest;
  text = f.szGamertag;
  clipRect.x = left + border;
  clipRect.w = column - 2 * border;
  GEngine->DrawText(Point2DFloat(clipRect.x, t), _size, clipRect, _font, color, text);
  left += column;
}

/* Not supported
#if _XBOX_SECURE
class DisplayFriendMissions : public Display
{
protected:
  SRef<PeerToPeerClient> _request;
  bool _listing;
  bool _downloading;

public:
  DisplayFriendMissions(ControlsContainer *parent);

  void OnSimulate(EntityAI *vehicle);

protected:
  bool CreateRequest();
  void ListMissions();
};

DisplayFriendMissions::DisplayFriendMissions(ControlsContainer *parent)
: Display(parent)
{
  Load("RscDisplayFriendOfferedMission");
  _listing = false;
  _downloading = false;
  if (CreateRequest())
    ListMissions();
}

void P2PClientProcess(int channel, P2PMsgType type, BYTE *data, int dataSize, void *context)
{
  LogF("Here");
}

void DisplayFriendMissions::OnSimulate(EntityAI *vehicle)
{
  if (_request && (_listing || _downloading))
  {
    _request->ProcessIncomingMessages(P2PClientProcess, this);
  }

  Display::OnSimulate(vehicle);
}

bool DisplayFriendMissions::CreateRequest()
{
  XONLINETASK_HANDLE task;
  HRESULT result = XOnlineMatchSessionFindFromID(GSelectedFriend.sessionID, NULL, &task);
  if (FAILED(result))
  {
    RptF("Offered missions: XOnlineMatchSessionFindFromID failed with error 0x%x", result);
    // CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return false;
  }
  do
  {
    result = XOnlineTaskContinue(task);
  } while(result == XONLINETASK_S_RUNNING);
  if (FAILED(result))
  {
    RptF("Offered missions: XOnlineTaskContinue failed with error 0x%x", result);
    XOnlineTaskClose(task);
    // CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return false;
  }
  DWORD nResults;
  PXONLINE_MATCH_SEARCHRESULT *results;
  result = XOnlineMatchSearchGetResults(task, &results, &nResults);
  if (FAILED(result))
  {
    RptF("Offered missions: XOnlineMatchSearchGetResults failed with error 0x%x", result);
    XOnlineTaskClose(task);
    // CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return false;
  }
  if (nResults == 0)
  {
    // session does not exist
    XOnlineTaskClose(task);
    // CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_LIVE_ERROR_NO_SESSION));
    return false;
  }
  
  XNADDR addr = results[0]->HostAddress;
  XNKID kid = results[0]->SessionID;
  XNKEY key = results[0]->KeyExchangeKey;
  XOnlineTaskClose(task);

  _request = CreatePeerToPeerClient(kid, key, addr);
  if (!_request)
  {
    // session does not exist
    return false;
  }
  return true;
}

void DisplayFriendMissions::ListMissions()
{
  Assert(_request);
  BYTE request = 0; // list
  _request->SendRequest(&request, sizeof(request));
  _listing = true;
}
#endif
*/

class DisplayFriendsOptions : public Display
{
protected:
  RString _name;
  bool _muted;

public:
  DisplayFriendsOptions(ControlsContainer *parent, RString resource, RString name, bool muted);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);

  bool IsMuted() const {return _muted;}

  void OnButtonClicked(int idc);
  void OnSimulate(EntityAI *vehicle);

protected:
  void UpdateButtons();
};

bool operator == (const XNKID &k1, const XNKID &k2)
{
  return memcmp(&k1, &k2, sizeof(XNKID)) == 0;
}

static bool IsVoiceMessage(const XUID &xuid)
{
  // retrieve voice mail
  DWORD n = 0;
  XONLINE_MSG_SUMMARY messages[XONLINE_MAX_NUM_MESSAGES];
  HRESULT result = XOnlineMessageEnumerate(0, messages, &n);
  XONLINE_MSG_SUMMARY *message = NULL;
  if (FAILED(result)) return false;
  for (DWORD i=0; i<n; i++)
  {
    if (XOnlineAreUsersIdentical(&messages[i].xuidSender, &xuid))
    {
      message = &messages[i];
      break;
    }
  }
  if (!message) return false;

  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));

  XONLINETASK_HANDLE task;
  result = XOnlineMessageDetails(0, message->dwMessageID, 0, 0, NULL, &task);
  if (FAILED(result))
  {
    ProgressFinish(p);
    return false;
  }
  do
  {
    result = XOnlineTaskContinue(task);
    ProgressRefresh();
  } while (result == XONLINETASK_S_RUNNING);
  if (FAILED(result))
  {
    XOnlineTaskClose(task);
    ProgressFinish(p);
    return false;
  }

  short codec;
  result = XOnlineMessageDetailsGetResultsProperty
    (
    task, XONLINE_MSG_PROP_VOICE_DATA_CODEC, sizeof(codec), &codec, NULL, NULL
    );
  XOnlineTaskClose(task);
  ProgressFinish(p);
  return SUCCEEDED(result) && codec == XONLINE_PROP_VOICE_DATA_CODEC_WMAVOICE_V90;
}

DisplayFriendsOptions::DisplayFriendsOptions(ControlsContainer *parent, RString resource, RString name, bool muted)
: Display(parent)
{
  _name = name;
  _muted = muted;
  Load(resource);
  UpdateButtons();

  IControl *ctrl = GetCtrl(IDC_FRIENDS_PLAY_MESSAGE);
  if (ctrl) ctrl->EnableCtrl(!GetNetworkManager().IsVoiceBanned() && IsVoiceMessage(GSelectedFriend.xuid));
}

void DisplayFriendsOptions::UpdateButtons()
{
  // no invites into the system link session possible
  const XNKID *key = GetNetworkManager().IsSystemLink() ? NULL : GetNetworkManager().GetSessionKey();
  bool canJoin = !(key && *key == GSelectedFriend.sessionID);
  bool canInvite = key && !(*key == GSelectedFriend.sessionID);
  bool isVoice = GetNetworkManager().IsVoice();

  IControl *ctrl = GetCtrl(IDC_FRIENDS_GAME_INVITE);
  if (ctrl) ctrl->EnableCtrl(canInvite);
  ctrl = GetCtrl(IDC_FRIENDS_GAME_INVITE_VOICE);
  if (ctrl) ctrl->EnableCtrl(canInvite && isVoice);
  ctrl = GetCtrl(IDC_FRIENDS_GAME_JOIN);
  if (ctrl) ctrl->EnableCtrl(canJoin);
  ctrl = GetCtrl(IDC_FRIENDS_GAME_ACCEPT);
  if (ctrl) ctrl->EnableCtrl(canJoin);
}

Control *DisplayFriendsOptions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_FRIENDS_PLAYER:
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text) text->SetRawText(_name);
    }
    break;
  }
  return ctrl;
}

void DisplayFriendsOptions::OnButtonClicked(int idc)
{
  // exit on every button
  Exit(idc);
}

void DisplayFriendsOptions::OnSimulate(EntityAI *vehicle)
{
  UpdateButtons();

  Display::OnSimulate(vehicle);
}

#if _ENABLE_XHV

VoiceMailEngine::VoiceMailEngine()
{
  _recording = false;
  _playing = false;
  _recordingEnabled = false;
  _playbackEnabled = false;
  _length = 0;

  _engine = NULL;
  _port = -1;
  CreateVoiceEngine();
}

VoiceMailEngine::~VoiceMailEngine()
{
  DestroyVoiceEngine();
}

void VoiceMailEngine::OnSimulate()
{
  if (_engine)
  {
    _recordingEnabled = GetNetworkManager().IsVoice();
    _playbackEnabled = !GetNetworkManager().IsVoiceBanned();

#ifdef _WIN32
    int GetActiveController();
    int port = GetActiveController();
    if (port != _port) SetPort(port);
#endif

    if (_recording)
    {
      DWORD time = ::GlobalTickCount() - _start;
      if (time >= XHV_MAX_VOICEMAIL_DURATION_MS) RecordingStop();
    }

    HRESULT result = _engine->DoWork();
    if (FAILED(result))
    {
      RptF("XHVEngine::DoWork failed with error 0x%x", result);
    }
  }
}

void VoiceMailEngine::RecordingStart()
{
  if (!CanRecord()) return;

  int size = XHVGetVoiceMailBufferSize(XHV_MAX_VOICEMAIL_DURATION_MS);
  _data.Realloc(size);
  _data.Resize(size);

  HRESULT result = _engine->VoiceMailRecord(_port, XHV_MAX_VOICEMAIL_DURATION_MS, size, (BYTE *)_data.Data());
  if (SUCCEEDED(result))
  {
    _recording = true;
    _length = 0;
    _start = ::GlobalTickCount();
    UpdateDuration(0.001f * XHV_MAX_VOICEMAIL_DURATION_MS);
  }
  else
  {
    RptF("XHVEngine::VoiceMailRecord failed with error 0x%x", result);
  }
}

void VoiceMailEngine::RecordingStop()
{
  HRESULT result = _engine->VoiceMailStop(_port);
  if (FAILED(result))
  {
    RptF("XHVEngine::VoiceMailRecord failed with error 0x%x", result);
  }
}

void VoiceMailEngine::ReplayStart()
{
  if (!CanReplay()) return;
  if (_data.Size() == 0) return;

  BOOL throughSpeakers = !_recordingEnabled || Glob.header.voiceThroughSpeakers;

  HRESULT result = _engine->VoiceMailPlay(_port, _data.Size(), (const BYTE *)_data.Data(),throughSpeakers);
  if (SUCCEEDED(result))
  {
    _playing = true;
    _start = ::GlobalTickCount();
    UpdateDuration(0.001f * _length);
  }
  else
  {
    RptF("XHVEngine::VoiceMailPlay failed with error 0x%x", result);
  }
}

void VoiceMailEngine::ReplayStop()
{
  HRESULT result = _engine->VoiceMailStop(_port);
  if (FAILED(result))
  {
    RptF("XHVEngine::VoiceMailRecord failed with error 0x%x", result);
  }
}

STDMETHODIMP VoiceMailEngine::CommunicatorStatusUpdate(DWORD dwPort, XHV_VOICE_COMMUNICATOR_STATUS status)
{
	// Establish that communicator is ready for voice mail use
	if (
	  (int)dwPort == _waitForCommunicator &&
		 status == XHV_VOICE_COMMUNICATOR_STATUS_INSERTED
	)
	{
		_waitForCommunicator = -1;
  }
  return S_OK;
}

STDMETHODIMP VoiceMailEngine::VoiceMailDataReady(DWORD dwLocalPort, DWORD dwDuration, DWORD dwSize)
{
  _length = dwDuration;
  _data.Resize(dwSize);
  OnRecorded();
  return S_OK;
}

STDMETHODIMP VoiceMailEngine::VoiceMailStopped(DWORD dwLocalPort)
{
  if (_playing)
  {
    _playing = false;
    OnReplayed();
  }
  else if (_recording)
  {
    _recording = false;
    OnRecorded();
  }
  return S_OK;
}

void VoiceMailEngine::CreateVoiceEngine()
{
  GetNetworkManager().DisableVoice(true);

  XHV_RUNTIME_PARAMS params;
  ZeroMemory(&params, sizeof(params));
  params.dwMaxLocalTalkers = 1;
  params.dwMaxRemoteTalkers = 1;
  params.dwMaxCompressedBuffers = 4; // 4 buffers per local talker
  params.dwFlags = 0;
  params.pEffectImageDesc = (dynamic_cast<SoundSystem8*>(GSoundsys))->GetEffectDesc();
  params.dwEffectsStartIndex = GraphVoice_Voice_0;
  params.dwOutOfSyncThreshold = 10;

  XHVEngine *engine = NULL;
  HRESULT result = XHVEngineCreate(&params, &engine);
  if (FAILED(result))
  {
    RptF("XHVEngineCreate failed with error 0x%x", result);
    return;
  }

  _engine = engine;

  // Enable voicemail and loopback modes only
//  result = engine->EnableProcessingMode(XHV_LOOPBACK_MODE);
//  if (FAILED(result))
//  {
//    RptF("XHVEngine::EnableProcessingMode failed with error 0x%x", result);
//  }
  // we need to enable voice chat mode, otherwise playback does not work
  result = engine->EnableProcessingMode(XHV_VOICECHAT_MODE);
  if (FAILED(result))
  {
    RptF("XHVEngine::EnableProcessingMode failed with error 0x%x", result);
  }
  result = engine->EnableProcessingMode(XHV_VOICEMAIL_MODE);
  if (FAILED(result))
  {
    RptF("XHVEngine::EnableProcessingMode failed with error 0x%x", result);
  }

  result = engine->SetCallbackInterface(this);
  if (FAILED(result))
  {
    RptF("XHVEngine::SetCallbackInterface failed with error 0x%x", result);
  }
  _waitForCommunicator = -1;
}

void VoiceMailEngine::DestroyVoiceEngine()
{
  if (_engine)
  {
    if (_port != -1) SetPort(-1);
    _engine->Release();
    _engine = NULL;
  }
  GetNetworkManager().DisableVoice(false);
}

void VoiceMailEngine::SetPort(int port)
{
  if (_port >= 0)
  {
    // Put it in inactive mode
    HRESULT result = _engine->SetProcessingMode(_port, XHV_INACTIVE_MODE);
    if (FAILED(result))
    {
      RptF("XHVEngine::SetProcessingMode failed with error 0x%x", result);
    }

    // Unregister a local talker
    result = _engine->UnregisterLocalTalker(_port);
    if (FAILED(result))
    {
      RptF("XHVEngine::UnregisterLocalTalker failed with error 0x%x", result);
    }
    LogF("Local talker %d unregistered", _port);
  }

  _port = port;

  if (_port >= 0)
  {
    // Register a local talker
    HRESULT result = _engine->RegisterLocalTalker(_port);
    if (FAILED(result))
    {
      RptF("XHVEngine::RegisterLocalTalker failed with error 0x%x", result);
    }
    LogF("Local talker %d registered", _port);

    // Put it in voicemail mode
    result = _engine->SetProcessingMode(_port, XHV_VOICEMAIL_MODE);
    if (FAILED(result))
    {
      RptF("XHVEngine::SetProcessingMode failed with error 0x%x", result);
    }

	  // Give XHV enough time in DoWork to establish communicators registered above
	  // In a game with voice chat, local talkers would have been established long ago
	  // and this would be unnecessary
	  _waitForCommunicator = _port;
	  int cRetries = 100;
	  #if _ENABLE_REPORT
	    DWORD time = GlobalTickCount();
	  #endif
	  while ( _waitForCommunicator>=0 && --cRetries>=0 )
	  {
		  OnSimulate();
		  Sleep( 10 );
	  }
	  Assert( _waitForCommunicator == -1 );
	  #if _ENABLE_REPORT
	    DWORD duration = GlobalTickCount()-time;
	    LogF("XHV establish in %d ms",duration);
	  #endif

    // disable voice mask
    XHV_VOICE_MASK voiceMask;
    voiceMask.fSpecEnergyWeight = XHV_VOICE_MASK_PARAM_DISABLED;
    voiceMask.fPitchScale = XHV_VOICE_MASK_PARAM_DISABLED;
    voiceMask.fWhisperValue = XHV_VOICE_MASK_PARAM_DISABLED;
    voiceMask.fRoboticValue = XHV_VOICE_MASK_PARAM_DISABLED;
    _engine->SetVoiceMask(_port, &voiceMask);
  }
}

#else

VoiceMailEngine::VoiceMailEngine()
{
  _recording = false;
  _playing = false;
  _length = 0;
}

VoiceMailEngine::~VoiceMailEngine()
{
  if (_recording)
  {
    _length = VoiceRecordingStop(_data);
    if (_length == 0) _data.Clear();
    _recording = false;
  }
  if (_playing)
  {
    VoNResult result = VoiceReplayStop();
    (void)result;
    Assert(result == VonOK || result == VonBusy);
    _playing = false;
  }
}

void VoiceMailEngine::OnSimulate()
{
  // auto stop
  if (_recording)
  {
    if (time >= 15.0f)
    {
      _length = VoiceRecordingStop(_data);
      if (_length == 0) _data.Clear();
      _recording = false;
      OnRecorded();
    }
  }
  if (_playing)
  {
    VoNResult result = VoiceReplayStatus();
    if (result != VonBusy)
    {
      VoNResult result = VoiceReplayStop();
      (void)result;
      Assert(result == VonOK || result == VonBusy);
      _playing = false;
      OnReplayed();
    }
  }
}

void VoiceMailEngine::RecordingStart()
{
  VoNResult result = VoiceRecordingStart();
  if (result == VonOK)
  {
    _recording = true;
    _start = ::GlobalTickCount();
    UpdateDuration(15.0f);
  }
}

void VoiceMailEngine::RecordingStop()
{
  _length = VoiceRecordingStop(_data);
  if (_length == 0) _data.Clear();
  _recording = false;
  OnRecorded();
}

void VoiceMailEngine::ReplayStart()
{
  if (_data.Size() == 0) return;

  VoNResult result = VoiceReplayStart(_data,_length);
  if (result == VonOK)
  {
    _playing = true;
    _start = ::GlobalTickCount();
    UpdateDuration(0.001f * _length);
  }
}

void VoiceMailEngine::ReplayStop()
{
  VoNResult result = VoiceReplayStop();
  (void)result;
  Assert(result == VonOK || result == VonBusy);
  _playing = false;
  OnReplayed();
}

#endif

DisplaySendVoiceMail::DisplaySendVoiceMail(ControlsContainer *parent, RString title, RString name)
: Display(parent)
{
  Load("RscDisplaySendVoiceMail");

  IControl *ctrl = GetCtrl(IDC_SVM_TITLE);
  ITextContainer *text1 = GetTextContainer(ctrl);
  if (text1) text1->SetText(title);
  ctrl = GetCtrl(IDC_SVM_PLAYER);
  CStructuredText *text2 = GetStructuredText(ctrl);
  if (text2) text2->SetRawText(name);

  UpdateButtons();
}

Control *DisplaySendVoiceMail::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_SVM_RECORD:
    _record = GetStructuredText(ctrl);
    break;
  case IDC_SVM_PLAY:
    _play = GetStructuredText(ctrl);
    break;
  case IDC_SVM_SEND:
    _send = GetStructuredText(ctrl);
    break;
  case IDC_SVM_PROGRESS:
    if (ctrl->GetType() == CT_PROGRESS)
      _progress = static_cast<CProgressBar *>(ctrl);
    ctrl->EnableCtrl(false);
    break;
  case IDC_SVM_TIME:
    _time = GetStructuredText(ctrl);
    break;
  }

  return ctrl;
}

void DisplaySendVoiceMail::OnSimulate(EntityAI *vehicle)
{
  UpdateButtons();
  float time = 0;
  if (_playing || _recording) time = 0.001 * (::GlobalTickCount() - _start);

  if (_time) _time->SetText(Format("% 2.0f", time));
  if (_progress) _progress->SetPos(time);

  VoiceMailEngine::OnSimulate();
  Display::OnSimulate(vehicle);
}

void DisplaySendVoiceMail::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_SVM_RECORD:
    if (_playing) return;
    if (_recording)
    {
      RecordingStop();
    }
    else RecordingStart();
    break;
  case IDC_SVM_PLAY:
    if (_recording) return;
    if (_playing)
    {
      ReplayStop();
    }
    else ReplayStart();
    break;
  case IDC_SVM_SEND:
    Exit(IDC_OK);
    break;
  case IDC_CANCEL:
    Exit(IDC_CANCEL);
    break;
  default:
    Fail("Unexpected IDC");
    break;
  }
}

void DisplaySendVoiceMail::OnRecorded()
{
  UpdateButtons();
  FocusCtrl(IDC_SVM_PLAY);
}

void DisplaySendVoiceMail::OnReplayed()
{
  UpdateButtons();
  FocusCtrl(IDC_SVM_SEND);
}

void DisplaySendVoiceMail::UpdateButtons()
{
  if (_playing)
  {
    if (_record)
    {
      _record->EnableCtrl(false);
      _record->SetText(LocalizeString(IDS_XBOX_VOICE_RECORD));
    }
    if (_play)
    {
      _play->EnableCtrl(true);
      _play->SetText(LocalizeString(IDS_XBOX_VOICE_STOP));
    }
    if (_send) _send->EnableCtrl(false);
  }
  else if (_recording)
  {
    if (_record)
    {
      _record->EnableCtrl(true);
      _record->SetText(LocalizeString(IDS_XBOX_VOICE_STOP));
    }
    if (_play)
    {
      _play->EnableCtrl(false);
      _play->SetText(LocalizeString(IDS_XBOX_VOICE_PLAY));
    }
    if (_send) _send->EnableCtrl(false);
  }
  else
  {
    if (_record)
    {
      _record->EnableCtrl(CanRecord() && GetNetworkManager().IsVoice());
      _record->SetText(LocalizeString(IDS_XBOX_VOICE_RECORD));
    }
    if (_play)
    {
      _play->EnableCtrl(CanReplay() && _length > 0);
      _play->SetText(LocalizeString(IDS_XBOX_VOICE_PLAY));
    }
    if (_send) _send->EnableCtrl(_length > 0);
  }
}

void DisplaySendVoiceMail::UpdateDuration(float duration)
{
  if (_progress) _progress->SetRange(0, duration);
}

DisplayReceiveVoiceMail::DisplayReceiveVoiceMail(ControlsContainer *parent, const XUID &xuid, RString name)
: Display(parent)
{
  _name = name;
  _xuid = xuid;
  _played = false;
  Load("RscDisplayReceiveVoiceMail");

  // retrieve voice mail
  DWORD n = 0;
  XONLINE_MSG_SUMMARY messages[XONLINE_MAX_NUM_MESSAGES];
  HRESULT result = XOnlineMessageEnumerate(0, messages, &n);
  XONLINE_MSG_SUMMARY *message = NULL;
  if (SUCCEEDED(result))
  {
    for (DWORD i=0; i<n; i++)
    {
      if (XOnlineAreUsersIdentical(&messages[i].xuidSender, &xuid))
      {
        message = &messages[i];
        break;
      }
    }
  }
  if (!message || !ReadVoiceMail(message->dwMessageID))
  {
    Exit(IDC_CANCEL);
    return;
  }

  RString title;
  switch (message->bMsgType)
  {
  case XONLINE_MSG_TYPE_FRIEND_REQUEST:
    title = LocalizeString(IDS_XBOX_VOICE_FRIEND_REQUEST);
    break;
  case XONLINE_MSG_TYPE_GAME_INVITE:
    title = LocalizeString(IDS_XBOX_VOICE_GAME_INVITE);
    break;
  }

  IControl *ctrl = GetCtrl(IDC_RVM_TITLE);
  ITextContainer *text1 = GetTextContainer(ctrl);
  if (text1) text1->SetText(title);
  ctrl = GetCtrl(IDC_RVM_PLAYER);
  CStructuredText *text2 = GetStructuredText(ctrl);
  if (text2) text2->SetRawText(name);

  UpdateButtons();
}

bool DisplayReceiveVoiceMail::ReadVoiceMail(DWORD msgID)
{
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));

  XONLINETASK_HANDLE task;
  HRESULT result = XOnlineMessageDetails(0, msgID, 0, 0, NULL, &task);
  if (FAILED(result))
  {
    ProgressFinish(p);
    return false;
  }
  do
  {
    result = XOnlineTaskContinue(task);
    ProgressRefresh();
  } while (result == XONLINETASK_S_RUNNING);
  if (FAILED(result))
  {
    XOnlineTaskClose(task);
    ProgressFinish(p);
    return false;
  }

  short codec;
  result = XOnlineMessageDetailsGetResultsProperty
  (
    task, XONLINE_MSG_PROP_VOICE_DATA_CODEC, sizeof(codec), &codec, NULL, NULL
  );
  if (FAILED(result) || codec != XONLINE_PROP_VOICE_DATA_CODEC_WMAVOICE_V90)
  {
    XOnlineTaskClose(task);
    ProgressFinish(p);
    return false;
  }
  
  result = XOnlineMessageDetailsGetResultsProperty
  (
    task, XONLINE_MSG_PROP_VOICE_DATA_DURATION, sizeof(_length), &_length, NULL, NULL
  );
  if (FAILED(result))
  {
    XOnlineTaskClose(task);
    ProgressFinish(p);
    return false;
  }
  
  DWORD size = 0;
  result = XOnlineMessageDetailsGetResultsProperty
  (
    task, XONLINE_MSG_PROP_VOICE_DATA, 0, NULL, &size, NULL
  );
  if (result == XONLINE_E_MESSAGE_PROPERTY_DOWNLOAD_REQUIRED)
  {
    _data.Realloc(size);
    _data.Resize(size);
    XONLINETASK_HANDLE downloadTask;
    result = XOnlineMessageDownloadAttachmentToMemory
    (
      task, XONLINE_MSG_PROP_VOICE_DATA, (BYTE *)_data.Data(), size, NULL, &downloadTask
    );
    if (FAILED(result))
    {
      XOnlineTaskClose(task);
      ProgressFinish(p);
      return false;
    }
    do
    {
      result = XOnlineTaskContinue(downloadTask);
      ProgressRefresh();
    } while (result == XONLINETASK_S_RUNNING);
    if (FAILED(result))
    {
      XOnlineTaskClose(downloadTask);
      XOnlineTaskClose(task);
      ProgressFinish(p);
      return false;
    }
    DWORD sizeReceived, sizeTotal;
    BYTE *data;
    result = XOnlineMessageDownloadAttachmentToMemoryGetResults
    (
      downloadTask, &data, &sizeReceived, &sizeTotal
    );
    if (FAILED(result))
    {
      XOnlineTaskClose(downloadTask);
      XOnlineTaskClose(task);
      ProgressFinish(p);
      return false;
    }

    XOnlineTaskClose(downloadTask);
  }
  else if (FAILED(result))
  {
    XOnlineTaskClose(task);
    ProgressFinish(p);
    return false;
  }
  XOnlineTaskClose(task);
  ProgressFinish(p);
  return true;
}

Control *DisplayReceiveVoiceMail::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_RVM_PLAY:
    _play = GetStructuredText(ctrl);
    break;
  case IDC_RVM_PROGRESS:
    if (ctrl->GetType() == CT_PROGRESS)
      _progress = static_cast<CProgressBar *>(ctrl);
    ctrl->EnableCtrl(false);
    break;
  case IDC_RVM_TIME:
    _time = GetStructuredText(ctrl);
    break;
  }

  return ctrl;
}

void DisplayReceiveVoiceMail::OnSimulate(EntityAI *vehicle)
{
  UpdateButtons();
  float time = 0;
  if (_playing) time = 0.001 * (::GlobalTickCount() - _start);

  if (_time) _time->SetText(Format("% 2.0f", time));
  if (_progress) _progress->SetPos(time);

  VoiceMailEngine::OnSimulate();
  Display::OnSimulate(vehicle);
}

void DisplayReceiveVoiceMail::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_RVM_PLAY:
    if (_playing) ReplayStop();
    else
    {
      ReplayStart();
      if (_playing) _played = true;
    }
    break;
  case IDC_RVM_FEEDBACK:
    CreateChild(new DisplayFeedback(this, _name, "RscDisplayMessageFeedback"));
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayReceiveVoiceMail::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_XPLAYERS_FEEDBACK:
#if _XBOX_SECURE && _ENABLE_MP
    {
      XONLINE_FEEDBACK_TYPE type;
      switch (exit)
      {
      case IDC_FEEDBACK_MSG_HARASSING:
        type = XONLINE_FEEDBACK_NEG_HARASSMENT;
        break;
      case IDC_FEEDBACK_MSG_OFFENSIVE:
        type = XONLINE_FEEDBACK_NEG_MESSAGE_INAPPROPRIATE;
        break;
      case IDC_FEEDBACK_MSG_SPAM:
        type = XONLINE_FEEDBACK_NEG_MESSAGE_SPAM;
        break;
      case IDC_FEEDBACK_SCREAM:
        type = XONLINE_FEEDBACK_NEG_SCREAMING;
        break;
      default:
        Fail("Unknown feedback");
      case IDC_CANCEL:
        return;
      }

      WCHAR buffer[16];
      ToWideChar(buffer, 16, _name);
      XONLINE_FEEDBACK_PARAMS params;
      params.lpStringParam = buffer;
      XONLINETASK_HANDLE task = NULL;
      HRESULT result = XOnlineFeedbackSend(0, _xuid, type, &params, NULL, &task);
      if (FAILED(result))
      {
        RptF("XOnlineFeedbackSend failed with error 0x%x", result);
        return;
      }
      
      Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_NETWORK_SENDING));
      do
      {
        result = XOnlineTaskContinue(task);
        ProgressRefresh();
      } while (result == XONLINETASK_S_RUNNING);
      ProgressFinish(p);

      XOnlineTaskClose(task);
      if (FAILED(result))
      {
        RptF("Pumping of XOnlineFeedbackSend task failed with error 0x%x", result);
      }
    }
#endif
    break;
  }
}


void DisplayReceiveVoiceMail::UpdateButtons()
{
  if (_play)
  {
    if (_playing)
    {
      _play->EnableCtrl(true);
      _play->SetText(LocalizeString(IDS_XBOX_VOICE_STOP));
    }
    else
    {
      _play->EnableCtrl(CanReplay() && _data.Size() > 0);
      _play->SetText(LocalizeString(IDS_XBOX_VOICE_PLAY));
    }
  }

  IControl *ctrl = GetCtrl(IDC_RVM_FEEDBACK);
  if (ctrl) ctrl->EnableCtrl(!_playing && _played);
}

void DisplayReceiveVoiceMail::UpdateDuration(float duration)
{
  if (_progress) _progress->SetRange(0, duration);
}

DisplayFriends::DisplayFriends(ControlsContainer *parent, bool warnIfExit)
: Display(parent)
{
  _warnIfExit = warnIfExit;

#ifdef _WIN32
  LockController();
#endif

  Load("RscDisplayFriends");

  _task = NULL;

  if (_friends)
  {
    HRESULT result = XOnlineFriendsEnumerate(0, NULL, &_task);
    if (FAILED(result))
    {
      RptF("XOnlineFriendsEnumerate failed with error 0x%x", result);
      _task = NULL;
    }
  }

  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl) ctrl->EnableCtrl(_friends && _friends->GetCurSel() >= 0);
}

DisplayFriends::~DisplayFriends()
{
  if (_task)
  {
    HRESULT result = XOnlineFriendsEnumerateFinish(_task);
    Assert(SUCCEEDED(result));
    do
    {
      result = XOnlineTaskContinue(_task);
    } while (result == XONLINETASK_S_RUNNING);
    XOnlineTaskClose(_task);
  }

#ifdef _WIN32
  UnlockController();
#endif
}

Control *DisplayFriends::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_FRIENDS_LIST:
    _friends = new CFriends(this, idc, cls);
    return _friends;
  case IDC_FRIENDS_STATUS:
    {
      Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
      _status = GetStructuredText(ctrl);
      return ctrl;
    }
  }
  return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayFriends::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);

  if (!_friends) return;

  if (_task)
  {
    HRESULT result = XOnlineTaskContinue(_task);
    if (FAILED(result))
    {
      RptF("Friends enumeration: XOnlineTaskContinue failed with error 0x%x", result);
      XOnlineTaskClose(_task);
      _task = NULL;

      // Attempt to restart enumeration
      result = XOnlineFriendsEnumerate(0, NULL, &_task);
      if (FAILED(result))
      {
        RptF("XOnlineFriendsEnumerate failed with error 0x%x", result);
        _task = NULL;
      }
    }
    else if (result == XONLINETASK_S_RESULTS_AVAIL)
    {
      // update friends list
      XUID xuid = {0};
      int sel = _friends->GetCurSel();
      if (sel >= 0) xuid = _friends->_friends[sel].xuid;

      _friends->_friendsCount = XOnlineFriendsGetLatest(0, MAX_FRIENDS, _friends->_friends);

      sel = 0;
      if (xuid.qwUserID != 0)
      {
        for (int i=0; i<_friends->_friendsCount; i++)
        {
          if (XOnlineAreUsersIdentical(&xuid, &_friends->_friends[i].xuid))
          {
            sel = i; break;
          }
        }
      }
      _friends->SetCurSel(sel);
    }
  }

  if (_status)
  {
    RString text;
    int sel = _friends->GetCurSel();
    if (sel >= 0)
    {
      XONLINE_FRIEND &f = _friends->_friends[sel];
      DWORD state = f.dwFriendState;
      WCHAR gameNameW[MAX_TITLENAME_LEN];
      HRESULT result = XOnlineFriendsGetTitleName(f.dwTitleID, GetXLanguage(), MAX_TITLENAME_LEN, gameNameW);
      RString gameName = SUCCEEDED(result) ? FromWideChar(gameNameW) : RString();

      if (state & XONLINE_FRIENDSTATE_FLAG_RECEIVEDINVITE)
      {
        text = Format(LocalizeString(IDS_DISP_XBOX_FRIENDS_INVITE_RECEIVED), (const char *)gameName);
      }
      else if
      (
        (state & XONLINE_FRIENDSTATE_FLAG_SENTINVITE) &&
        !(state & (XONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED | XONLINE_FRIENDSTATE_FLAG_INVITEREJECTED))
      )
      {
        text = LocalizeString(IDS_DISP_XBOX_FRIENDS_INVITE_SENT);
      }
      else if (state & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST)
      {
        text = LocalizeString(IDS_DISP_XBOX_FRIENDS_REQUEST_RECEIVED);
      }
      else if (state & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST)
      {
        text = LocalizeString(IDS_DISP_XBOX_FRIENDS_REQUEST_SENT);
      }
      else if (state & XONLINE_FRIENDSTATE_FLAG_ONLINE)
      {
        if (state & XONLINE_FRIENDSTATE_FLAG_PLAYING)
          text = Format(LocalizeString(IDS_DISP_XBOX_FRIENDS_ONLINE_PLAYING), (const char *)gameName);
        else 
          text = Format(LocalizeString(IDS_DISP_XBOX_FRIENDS_ONLINE_NOT_IN_GAME), (const char *)gameName);
      }
      else
        text = LocalizeString(IDS_DISP_XBOX_FRIENDS_OFFLINE);

      if (GetNetworkManager().IsPlayerInMuteList(f.xuid))
      {
        text = text + RString(", ") + LocalizeString(IDS_DISP_XBOX_FRIENDS_VOICE_MUTED);
      }
      else if (state & XONLINE_FRIENDSTATE_FLAG_VOICE)
      {
        text = text + RString(", ") + LocalizeString(IDS_DISP_XBOX_FRIENDS_VOICE_ON);
      }
    }

    _status->SetText(text);
  }
}

void DisplayFriends::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_FRIENDS_LIST)
  {
    IControl *ctrl = GetCtrl(IDC_OK);
    if (ctrl) ctrl->EnableCtrl(_friends && _friends->GetCurSel() >= 0);
    return;
  }
  Display::OnLBSelChanged(ctrl, curSel);
}

void DisplayFriends::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (_friends)
    {
      int sel = _friends->GetCurSel();
      if (sel >= 0)
      {
        XONLINE_FRIEND &f = _friends->_friends[sel];
        DWORD state = f.dwFriendState;

        RString name;
        if (state & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST)
        {
          name = "RscDisplayFriendsRequestReceived";
        }
        else if (state & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST)
        {
          name = "RscDisplayFriendsRequestSent";
        }
        else if (state & XONLINE_FRIENDSTATE_FLAG_RECEIVEDINVITE)
        {
          name = "RscDisplayFriendsInventionReceived";
        }
        //else if ((state & XONLINE_FRIENDSTATE_FLAG_ONLINE) && (state & XONLINE_FRIENDSTATE_FLAG_PLAYING))
        else if ((state & XONLINE_FRIENDSTATE_FLAG_ONLINE) && (state & XONLINE_FRIENDSTATE_FLAG_JOINABLE))
        {
          if
          (
            (state & XONLINE_FRIENDSTATE_FLAG_SENTINVITE) &&
            !(state & (XONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED | XONLINE_FRIENDSTATE_FLAG_INVITEREJECTED))
          )
          {
            name = "RscDisplayFriendsPlayingInvitationSent";
          }
          else
          {
            name = "RscDisplayFriendsPlaying";
          }
        }
        else
        {
          if
          (
            (state & XONLINE_FRIENDSTATE_FLAG_SENTINVITE) &&
            !(state & (XONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED | XONLINE_FRIENDSTATE_FLAG_INVITEREJECTED))
          )
          {
            name = "RscDisplayFriendsNotPlayingInventionSent";
          }
          else
          {
            name = "RscDisplayFriendsNotPlaying";
          }
        }
        GSelectedFriend = f;
        CreateChild(new DisplayFriendsOptions(this, name, f.szGamertag, GetNetworkManager().IsPlayerInMuteList(f.xuid)));
      }
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

static bool AttachVoiceToMessage(XONLINE_MSG_HANDLE msg, const char *data, int size, int length)
{
  short codec = XONLINE_PROP_VOICE_DATA_CODEC_WMAVOICE_V90;

  HRESULT result = XOnlineMessageSetProperty(msg, XONLINE_MSG_PROP_VOICE_DATA_CODEC, sizeof(codec), &codec, 0);
  if (FAILED(result))
  {
    RptF("XOnlineMessageSetProperty failed with error 0x%x", result);
    return false;
  }
  result = XOnlineMessageSetProperty(msg, XONLINE_MSG_PROP_VOICE_DATA_DURATION, sizeof(length), &length, 0);
  if (FAILED(result))
  {
    RptF("XOnlineMessageSetProperty failed with error 0x%x", result);
    return false;
  }
  result = XOnlineMessageSetProperty(msg, XONLINE_MSG_PROP_VOICE_DATA, size, data, 0);
  if (FAILED(result))
  {
    RptF("XOnlineMessageSetProperty failed with error 0x%x", result);
    return false;
  }
  return true;
}

void DisplayFriends::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
    case IDD_MSG_DECLINE_INVITATION:
      Display::OnChildDestroyed(idd, exit);
      // decline game invitation
      if (exit == IDC_OK)
      {
        HRESULT result = XOnlineFriendsAnswerGameInvite(0, &GSelectedFriend, XONLINE_GAMEINVITE_NO);
        if (FAILED(result))
        {
          RptF("XOnlineFriendsAnswerGameInvite of player %s failed with error 0x%x", GSelectedFriend.szGamertag, result);
        }
      }
      break;
    case IDD_MSG_ACCEPT_INVITATION:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK) AcceptGameInvitation();
      break;
    case IDD_MSG_GAME_JOIN:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK) JoinGame();
      break;
    case IDD_MSG_REVOKE_INVITATION:
      Display::OnChildDestroyed(idd, exit);
      // revoke game invitation
      if (exit == IDC_OK)
      {
        const XNKID *key = GetNetworkManager().GetSessionKey();
        if (!key) break;
        HRESULT result = XOnlineFriendsRevokeGameInvite(0, *key, 1, &GSelectedFriend);
        if (FAILED(result))
        {
          RptF("XOnlineFriendsRevokeGameInvite of player %s failed with error 0x%x", GSelectedFriend.szGamertag, result);
        }
      }
      break;
    case IDD_MSG_BLOCK_REQUEST:
      Display::OnChildDestroyed(idd, exit);
      // decline and block friends request
      if (exit == IDC_OK)
      {
        HRESULT result = XOnlineFriendsAnswerRequest(0, &GSelectedFriend, XONLINE_REQUEST_BLOCK);
        if (FAILED(result))
        {
          RptF("XOnlineFriendsAnswerRequest of player %s failed with error 0x%x", GSelectedFriend.szGamertag, result);
        }
      }
      break;
    case IDD_MSG_DECLINE_REQUEST:
      Display::OnChildDestroyed(idd, exit);
      // decline friends request
      if (exit == IDC_OK)
      {
        HRESULT result = XOnlineFriendsAnswerRequest(0, &GSelectedFriend, XONLINE_REQUEST_NO);
        if (FAILED(result))
        {
          RptF("XOnlineFriendsAnswerRequest of player %s failed with error 0x%x", GSelectedFriend.szGamertag, result);
        }
      }
      break;
    case IDD_MSG_CANCEL_REQUEST:
      Display::OnChildDestroyed(idd, exit);
      // remove friend from friends list
      if (exit == IDC_OK)
      {
        HRESULT result = XOnlineFriendsRemove(0, &GSelectedFriend);
        if (FAILED(result))
        {
          RptF("XOnlineFriendsRemove of player %s failed with error 0x%x", GSelectedFriend.szGamertag, result);
        }
      }
      break;
    case IDD_FRIENDS_OPTIONS:
      Display::OnChildDestroyed(idd, exit);
      switch (exit)
      {
      case IDC_CANCEL:
        break;
      case IDC_FRIENDS_GAME_INVITE:
        // send game invitation
        SendInvitation(NULL);
        break;
      case IDC_FRIENDS_GAME_INVITE_VOICE:
        CreateChild(new DisplaySendVoiceMail(this, LocalizeString(IDS_XBOX_VOICE_GAME_INVITE), GSelectedFriend.szGamertag));
        break;
      case IDC_FRIENDS_PLAY_MESSAGE:
        CreateChild(new DisplayReceiveVoiceMail(this, GSelectedFriend.xuid, GSelectedFriend.szGamertag));
        break;
      case IDC_FRIENDS_GAME_CANCEL:
        {
          // revoke game invitation
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          CreateMsgBox(
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            Format(LocalizeString(IDS_MSG_CONFIRM_REVOKE_INVITATION), GSelectedFriend.szGamertag),
            IDD_MSG_REVOKE_INVITATION, false, &buttonOK, &buttonCancel);
        }
        break;
      case IDC_FRIENDS_GAME_ACCEPT:
        if (_warnIfExit)
        {
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          RString message;
          if (GetNetworkManager().IsServer())
            message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION);
          else if (GetNetworkManager().GetClientState() != NCSNone)
            message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION_CLIENT);
          else
            message = LocalizeString(IDS_MSG_CONFIRM_ACCEPT_INVITATION);
          CreateMsgBox(
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            message,
            IDD_MSG_ACCEPT_INVITATION, false, &buttonOK, &buttonCancel);
        }
        else AcceptGameInvitation();
        break;
      case IDC_FRIENDS_GAME_DECLINE:
        {
          // decline game invitation
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          CreateMsgBox(
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            Format(LocalizeString(IDS_MSG_CONFIRM_DECLINE_INVITATION), GSelectedFriend.szGamertag),
            IDD_MSG_DECLINE_INVITATION, false, &buttonOK, &buttonCancel);
        }
        break;
      case IDC_FRIENDS_GAME_JOIN:
        if (_warnIfExit)
        {
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          RString message;
          if (GetNetworkManager().IsServer())
            message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION);
          else if (GetNetworkManager().GetClientState() != NCSNone)
            message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION_CLIENT);
          else
            message = LocalizeString(IDS_MSG_CONFIRM_ACCEPT_INVITATION);
          CreateMsgBox(
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            message,
            IDD_MSG_GAME_JOIN, false, &buttonOK, &buttonCancel);
        }
        else JoinGame();
        break;
      case IDC_FRIENDS_ACCEPT:
        // accept friends request
        {
          HRESULT result = XOnlineFriendsAnswerRequest(0, &GSelectedFriend, XONLINE_REQUEST_YES);
          if (FAILED(result))
          {
            RptF("XOnlineFriendsAnswerRequest of player %s failed with error 0x%x", GSelectedFriend.szGamertag, result);
          }
        }
        break;
      case IDC_FRIENDS_DECLINE:
        {
          // decline friends request
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          CreateMsgBox(
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            Format(LocalizeString(IDS_MSG_CONFIRM_DECLINE_REQUEST), GSelectedFriend.szGamertag),
            IDD_MSG_DECLINE_REQUEST, false, &buttonOK, &buttonCancel);
        }
        break;
      case IDC_FRIENDS_STOP:
      {
        // decline and block friends request
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          Format(LocalizeString(IDS_MSG_CONFIRM_BLOCK_REQUEST), GSelectedFriend.szGamertag),
          IDD_MSG_BLOCK_REQUEST, false, &buttonOK, &buttonCancel);
        break;
      }
      case IDC_FRIENDS_CANCEL:
      case IDC_FRIENDS_REMOVE:
      {
        // remove friend from friends list
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          Format(LocalizeString(IDS_MSG_CONFIRM_CANCEL_REQUEST), GSelectedFriend.szGamertag),
          IDD_MSG_CANCEL_REQUEST, false, &buttonOK, &buttonCancel);
        break;
      }
/*
#if _XBOX_SECURE
      case IDC_FRIENDS_MISSIONS:
        if ((GSelectedFriend.dwFriendState & XONLINE_FRIENDSTATE_FLAG_ONLINE) != 0 && XOnlineTitleIdIsSameTitle(GSelectedFriend.dwTitleID))
        {
          CreateChild(new DisplayFriendMissions(this));
        }
        break;
#endif
*/
      }
      break;
    case IDD_SEND_VOICE_MAIL:
      if (exit == IDC_OK)
      {
        DisplaySendVoiceMail *disp = static_cast<DisplaySendVoiceMail *>(_child.GetRef());
        if (disp)
        {
          const char *data = disp->GetData();
          int size = disp->GetSize();
          int length = disp->GetDuration();
          XONLINE_MSG_HANDLE msg = NULL;
          if (size > 0 && length > 0)
          {
            int totalSize = sizeof(int) + sizeof(short) + sizeof(XNKID);
            HRESULT result = XOnlineMessageCreate
            (
              XONLINE_MSG_TYPE_GAME_INVITE, 4, totalSize,
              NULL, XONLINE_MSG_FLAG_HAS_VOICE, 0, &msg
            );
            if (SUCCEEDED(result))
            {
              const XNKID *key = GetNetworkManager().GetSessionKey();
              if (key)
              {
                HRESULT result = XOnlineMessageSetProperty(msg, XONLINE_MSG_PROP_SESSION_ID, sizeof(*key), key, 0);
                if (FAILED(result))
                {
                  RptF("XOnlineMessageSetProperty failed with error 0x%x", result);
                }
              }
              AttachVoiceToMessage(msg, data, size, length);
            }
            else
            {
              RptF("XOnlineMessageCreate failed with error 0x%x", result);
            }
          }
          SendInvitation(msg);
          if (msg) XOnlineMessageDestroy(msg);
        }
      }
      Display::OnChildDestroyed(idd, exit);
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

void DisplayFriends::SendInvitation(XONLINE_MSG_HANDLE msg)
{
  const XNKID *key = GetNetworkManager().GetSessionKey();
  if (!key) return;
  if (*key == GSelectedFriend.sessionID) return;

  XONLINETASK_HANDLE task = NULL;
  HRESULT result = XOnlineGameInviteSend(0, 1, &GSelectedFriend.xuid, *key, 0, msg, NULL, &task);
  if (FAILED(result))
  {
    RptF("XOnlineGameInviteSend failed with error 0x%x", result);
  }
  Assert(task);
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_NETWORK_SENDING));
  ProgressAdd(100);
  DWORD last = 0;
  do
  {
    result = XOnlineTaskContinue(task);
    {
      DWORD cur;
      HRESULT result = XOnlineMessageSendGetProgress(task, &cur, NULL, NULL);
      if (SUCCEEDED(result))
      {
        ProgressAdvance(cur - last);
        last = cur;
      }
      ProgressRefresh();
    }
  }
  while (result == XONLINETASK_S_RUNNING);
  ProgressFinish(p);
  XOnlineTaskClose(task);
  if (FAILED(result))
  {
    RptF("XOnlineGameInviteSend pumping failed with error 0x%x", result);
  }
}

void DisplayFriends::AcceptGameInvitation()
{
  // Invitations implemented in GSaveGames
}

void DisplayFriends::JoinGame()
{
  // Invitations implemented in GSaveGames
}
#endif // _XBOX_VER < 200

DisplayLiveStats::DisplayLiveStats(ControlsContainer *parent)
: Display(parent)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // from now on we need to be connected to Live
  GSaveSystem.SetLiveConnectionNeeded(true);
#endif

  Load("RscDisplayStatistics");

  if (_boards)
  {
    ParamEntryVal cls = Pars >> "CfgLiveStats";
    for (int i=0; i<cls.GetEntryCount(); i++)
    {
      ParamEntryVal entry = cls.GetEntry(i);
      int index = _boards->AddString(entry >> "name");
      _boards->SetData(index, entry.GetName());
    }
    _boards->SetCurSel(0);
  }

  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl) ctrl->EnableCtrl(_boards && _boards->GetCurSel() >= 0);
}

Control *DisplayLiveStats::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_LIVE_STATS_BOARDS:
    _boards = GetListBoxContainer(ctrl);
    break;
  }

  return ctrl;
}

void DisplayLiveStats::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (_boards)
    {
      int sel = _boards->GetCurSel();
      if (sel < 0) break;
      CreateChild(new DisplayLiveStatsBoard(this, _boards->GetData(sel)));
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayLiveStats::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_LIVE_STATS_BOARDS)
  {
    IControl *ctrl = GetCtrl(IDC_OK);
    if (ctrl) ctrl->EnableCtrl(_boards && _boards->GetCurSel() >= 0);
    return; 
  }
  Display::OnLBSelChanged(ctrl, curSel);
}

class CLiveStatsList : public CListBox
{
friend class DisplayLiveStatsBoard;

protected:
  const LiveStatsBoardInfo &_info;

public:
  CLiveStatsList(ControlsContainer *parent, int idc, ParamEntryPar cls, const LiveStatsBoardInfo &info);

  int GetSize() const;
  // multiple selection is not allowed
  bool IsSelected(int row, int style) const {return row == GetCurSel();}
  const XUSER_STATS_ROW &GetItem(DWORD i) const;

  void OnDraw(UIViewport *vp, float alpha);
  void DrawTitle(float alpha);
  void DrawItem
  (
    UIViewport *vp, float alpha, int i, bool selected, float top,
    const Rect2DFloat &rect, float widthDiff
  ) override;
};

CLiveStatsList::CLiveStatsList(ControlsContainer *parent, int idc, ParamEntryPar cls, const LiveStatsBoardInfo &info)
: CListBox(parent, idc, cls), _info(info)
{
}

int CLiveStatsList::GetSize() const
{
  if (_info._statsUpdating) return 0;

  const XUSER_STATS_READ_RESULTS *results = reinterpret_cast<const XUSER_STATS_READ_RESULTS *>(_info._statsBuffer.Data());
  if (results->dwNumViews != 1) return 0;

  const XUSER_STATS_VIEW &view = results->pViews[0];
  return view.dwNumRows;
}

const XUSER_STATS_ROW &CLiveStatsList::GetItem(DWORD i) const
{
  Assert(!_info._statsUpdating);

  const XUSER_STATS_READ_RESULTS *results = reinterpret_cast<const XUSER_STATS_READ_RESULTS *>(_info._statsBuffer.Data());
  Assert(results->dwNumViews == 1);

  const XUSER_STATS_VIEW &view = results->pViews[0];
  Assert(i >= 0 && i < view.dwNumRows);
  return view.pRows[i];
}

void CLiveStatsList::OnDraw(UIViewport *vp, float alpha)
{
  CListBox::OnDraw(vp, alpha);
  DrawTitle(alpha);
}

void CLiveStatsList::DrawTitle(float alpha)
{
  Rect2DFloat rect(_x, _y - _rowHeight, _w - _sbWidth, _rowHeight);

  const float w = GEngine->Width2D();
  const float h = GEngine->Height2D();

  PackedColor lineColor = ModAlpha(_ftColor, alpha);
  PackedColor color = ModAlpha(_ftColor, alpha);

  float cx1 = CX(rect.x);
  float cx2 = CX(rect.x + rect.w);
  float cy1 = CY(rect.y);
  float cy2 = CY(rect.y + rect.h);
  // top line
  GEngine->DrawLine(Line2DPixel(cx1, cy1, cx2, cy1), lineColor, lineColor);
  // left line
  GEngine->DrawLine(Line2DPixel(cx1, cy1, cx1, cy2), lineColor, lineColor);
  // bottom line
  GEngine->DrawLine(Line2DPixel(cx1, cy2, cx2, cy2), lineColor, lineColor);

  // const float border = TEXT_BORDER;
  
  // titles of the columns
  Rect2DFloat itemRect(rect.x, rect.y, 0, rect.h);
  for (int i=0; i<_info._columns.Size(); i++)
  {
    const LiveStatsBoardColumn &column = _info._columns[i];
    itemRect.w = column._width * rect.w;

    // right line
    float cx = CX(itemRect.x + itemRect.w);
    GEngine->DrawLine(Line2DPixel(cx, cy1, cx, cy2), lineColor, lineColor);

    // text
    float width = GEngine->GetTextWidth(_size, _font, column._name);
    float l = itemRect.x + 0.5 * (itemRect.w - width); // all titles are centered
    float t = itemRect.y + 0.5 * (itemRect.h - _size);
    GEngine->DrawText(Point2DFloat(l, t), _size, itemRect, _font, color, column._name);

    // move to the next position
    itemRect.x += itemRect.w;
  }
}

void CLiveStatsList::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top,
  const Rect2DFloat &rect, float widthDiff
)
{
  Rect2DFloat sRect = rect;
  sRect.w = _w - _sbWidth; // make design independent on scrollbar existence

  const float w = GEngine->Width2D();
  const float h = GEngine->Height2D();

  // highlight selected row
  if (selected && _showSelected)
  {
    float factor = -0.5 * cos(_speed * (Glob.uiTime - _start)) + 0.5;
    PackedColor bgColor = _selBgColor;

    // animate color if focused
    if (IsFocused())
    {
      Color bgcol0=bgColor, bgcol1=_selBgColor2;
      bgColor = PackedColor(bgcol0 * factor + bgcol1 * (1 - factor));
    }
    bgColor = ModAlpha(bgColor, alpha);

    MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    Rect2DAbs rectA;
    GEngine->Convert(rectA, Rect2DFloat(sRect.x, top, sRect.w, _rowHeight));
    GEngine->Draw2D(mip, bgColor, rectA);
  }

  PackedColor lineColor = ModAlpha(_ftColor, alpha);

  float cx1 = CX(sRect.x);
  float cx2 = CX(sRect.x + sRect.w);
  float cy1 = CY(top);
  float cy2 = CY(top + _rowHeight);

  // left line
  Line2DPixel line(cx1, cy1, cx1, cy2);
  saturateMax(line.beg.y, CY(sRect.y));
  saturateMin(line.end.y, CY(sRect.y + sRect.h));
  GEngine->DrawLine(line, lineColor, lineColor);

  // bottom line
  if( (cy2 > CY(sRect.y)) && (cy2 < CY(sRect.y + sRect.h)) )
    GEngine->DrawLine(Line2DPixel(cx1, cy2, cx2, cy2), lineColor, lineColor);

  const XUSER_STATS_ROW &row = GetItem(i);
  PackedColor color = ModAlpha(_ftColor, alpha);

  if (_info.IsPlayer(row.xuid)) color = ModAlpha(_info._colorPlayer, alpha);
  else if (_info.IsFriend(row.xuid)) color = ModAlpha(_info._colorFriend, alpha);


  Rect2DFloat itemRect(sRect.x, top, 0, _rowHeight);

  const float border = TEXT_BORDER;

  // values of the columns
  for (int i=0; i<_info._columns.Size(); i++)
  {
    const LiveStatsBoardColumn &column = _info._columns[i];
    itemRect.w = column._width * sRect.w;

    // right line
    line.beg.x = line.end.x = CX(itemRect.x + itemRect.w);
    GEngine->DrawLine(line, lineColor, lineColor);

    // compose text
    RString text;
    switch (column._id)
    {
    case -1:
      // rank
      if (row.dwRank > 0) text = Format(column._format, row.dwRank);
      break;
    case -2:
      // gamertag
      text = Format(column._format, row.szGamertag);
      break;
    case -3:
      // rating
      text = Format(column._format, row.i64Rating);
      break;
    default:
      // common value
      for (DWORD j=0; j<row.dwNumColumns; j++)
      {
        const XUSER_STATS_COLUMN &col = row.pColumns[j];
        if (col.wColumnId == column._id)
        {
          // the value found
          switch (col.Value.type)
          {
          case XUSER_DATA_TYPE_INT32:
            text = Format(column._format, col.Value.nData);
            break;
          case XUSER_DATA_TYPE_INT64:
            text = Format(column._format, col.Value.i64Data);
            break;
          case XUSER_DATA_TYPE_FLOAT:
            text = Format(column._format, col.Value.fData);
            break;
          case XUSER_DATA_TYPE_DOUBLE:
            text = Format(column._format, col.Value.dblData);
            break;
          case XUSER_DATA_TYPE_UNICODE:
            {
              RString value = UnicodeToUTF(col.Value.string.pwszData);
              text = Format(column._format, cc_cast(value));
            }
            break;
          }
          break;
        }
      }
      break;
    }

    // draw text
    float width = GEngine->GetTextWidth(_size, _font, text);
    float l = itemRect.x;
    switch (column._align)
    {
    case ST_RIGHT:
      l += itemRect.w - border - width;
      break;
    case ST_CENTER:
      l += 0.5 * (itemRect.w - width);
      break;
    default:
      Assert(column._align == ST_LEFT);
      l += border;
      break;
    }
    float t = itemRect.y + 0.5 * (itemRect.h - _size);
    Rect2DFloat clipRect(itemRect);
    saturateMax(clipRect.y, sRect.y);
    saturateMin(clipRect.h, sRect.y + sRect.h - clipRect.y);
    GEngine->DrawText(Point2DFloat(l, t), _size, clipRect, _font, color, text);

    // move to the next position
    itemRect.x += itemRect.w;
  }
}

bool LiveStatsBoardInfo::IsFriend(XUID xuid) const
{
  if (_friendsHandle != INVALID_HANDLE_VALUE)
  {
    // enumeration in progress
    return false;
  }

  const XONLINE_FRIEND *friends = reinterpret_cast<const XONLINE_FRIEND *>(_friendsBuffer.Data());
  for (DWORD i=0; i<_friendsCount; i++)
  {
    const XONLINE_FRIEND &f = friends[i];
    if (XOnlineAreUsersIdentical(xuid, f.xuid))
    {
      return
        (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST) == 0 &&
        (f.dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST) == 0;
    }
  }
  return false;
}

bool LiveStatsBoardInfo::StartEnumFriends()
{
  if (_friendsHandle != INVALID_HANDLE_VALUE) return true; // enumeration already in progress

  // Start the enumeration of friends
  int index = GSaveSystem.GetUserIndex();
  if (index < 0) return false;

  // Create the enumeration
  DWORD bufferSize = 0;
  HRESULT result = XFriendsCreateEnumerator(index, 0, MAX_FRIENDS, &bufferSize, &_friendsHandle);
  if (result != ERROR_SUCCESS) return false;

  // Allocate the buffer
  _friendsBuffer.Init(bufferSize);
  memset(_friendsBuffer.Data(), 0, bufferSize);
  
  // start the overlapped operation
  memset(&_friendsOverlapped, 0, sizeof(_friendsOverlapped));
  result = XEnumerate(_friendsHandle, _friendsBuffer.Data(), bufferSize, NULL, &_friendsOverlapped);
  if (FAILED(result))
  {
    CloseHandle(_friendsHandle);
    _friendsHandle = INVALID_HANDLE_VALUE;
    return false;
  }

  return true;
}

void LiveStatsBoardInfo::StopEnumFriends()
{
  // Cancel the friends enumeration
  if (_friendsHandle != INVALID_HANDLE_VALUE)
  {
    XCancelOverlapped(&_friendsOverlapped);
    CloseHandle(_friendsHandle);
    _friendsHandle = INVALID_HANDLE_VALUE;
  }
}

bool LiveStatsBoardInfo::ProcessEnumFriends()
{
  if (_friendsHandle == INVALID_HANDLE_VALUE) return false;

  // Process the retrieving of the list of friends
  if (!XHasOverlappedIoCompleted(&_friendsOverlapped)) return false;

  // Done
  DWORD result = XGetOverlappedExtendedError(&_friendsOverlapped);
  if (SUCCEEDED(result))
  {
    // Read the count of friends stored to _friendsBuffer
    XGetOverlappedResult(&_friendsOverlapped, &_friendsCount, TRUE);
    // Create the list of players to retrieve statistics for
    _xuids.Realloc(_friendsCount + 1); // friends and me
    _xuids.Resize(_friendsCount + 1);
    const XONLINE_FRIEND *friends = reinterpret_cast<const XONLINE_FRIEND *>(_friendsBuffer.Data());
    for (DWORD i=0; i<_friendsCount; i++) _xuids[i] = friends[i].xuid; // copy friends
    _xuids[_friendsCount] = _player; // copy myself
  }

  // Free the resources
  CloseHandle(_friendsHandle);
  _friendsHandle = INVALID_HANDLE_VALUE;
  return SUCCEEDED(result);
}

void LiveStatsBoardInfo::StatsInit(int board)
{
  _spec.dwViewId = board;

  int n = 0; // number of columns
  for (int i=0; i<_columns.Size(); i++)
  {
    int id = _columns[i]._id;
    if (id >= 0) // not a fixed column
    {
      _spec.rgwColumnIds[n++] = id;
      if (n >= XUSER_STATS_ATTRS_IN_SPEC) break; // too much columns
    }
  }

  _spec.dwNumColumnIds = n;  
}

bool LiveStatsBoardInfo::StartStatsFriends()
{
  if (_friendsHandle != INVALID_HANDLE_VALUE) return false; // friends enumeration in progress
  if (_statsUpdating) return false; // statistics retrieving already in progress

  // Check the output buffer size
  DWORD bufferSize = 0;
  HRESULT result = XUserReadStats(0, _xuids.Size(), _xuids.Data(), 1, &_spec, &bufferSize, NULL, NULL); // can run synchronously, only checking the size
  if (FAILED(result)) return false;

  // Allocate the buffer
  _statsBuffer.Init(bufferSize);
  memset(_statsBuffer.Data(), 0, bufferSize);

  // start the overlapped operation
  memset(&_statsOverlapped, 0, sizeof(_statsOverlapped));
  result = XUserReadStats(0, _xuids.Size(), _xuids.Data(), 1, &_spec, &bufferSize, (XUSER_STATS_READ_RESULTS *)_statsBuffer.Data(), &_statsOverlapped);
  if (FAILED(result)) return false;

  _statsUpdating = true;
  return true;
}

bool LiveStatsBoardInfo::StartStatsByRank(int rank)
{
  if (_statsUpdating) return false; // statistics retrieving already in progress

  // Create the enumeration
  DWORD bufferSize = 0;
  HRESULT result = XUserCreateStatsEnumeratorByRank(0, rank, MAX_STAT_USERS, 1, &_spec, &bufferSize, &_statsHandle);
  if (result != ERROR_SUCCESS) return false;

  // Allocate the buffer
  _statsBuffer.Init(bufferSize);
  memset(_statsBuffer.Data(), 0, bufferSize);

  // start the overlapped operation
  memset(&_statsOverlapped, 0, sizeof(_statsOverlapped));
  result = XEnumerate(_statsHandle, _statsBuffer.Data(), bufferSize, NULL, &_statsOverlapped);
  if (FAILED(result))
  {
    CloseHandle(_statsHandle);
    _statsHandle = INVALID_HANDLE_VALUE;
    return false;
  }

  _statsUpdating = true;
  return true;
}

bool LiveStatsBoardInfo::StartStatsByXuid(XUID xuid)
{
  if (_statsUpdating) return false; // statistics retrieving already in progress

  // Create the enumeration
  DWORD bufferSize = 0;
  HRESULT result = XUserCreateStatsEnumeratorByXuid(0, xuid, MAX_STAT_USERS, 1, &_spec, &bufferSize, &_statsHandle);
  if (result != ERROR_SUCCESS) return false;

  // Allocate the buffer
  _statsBuffer.Init(bufferSize);
  memset(_statsBuffer.Data(), 0, bufferSize);

  // start the overlapped operation
  memset(&_statsOverlapped, 0, sizeof(_statsOverlapped));
  result = XEnumerate(_statsHandle, _statsBuffer.Data(), bufferSize, NULL, &_statsOverlapped);
  if (FAILED(result))
  {
    CloseHandle(_statsHandle);
    _statsHandle = INVALID_HANDLE_VALUE;
    return false;
  }

  _statsUpdating = true;
  return true;
}

void LiveStatsBoardInfo::StopStats()
{
  if (_statsUpdating)
  {
    XCancelOverlapped(&_statsOverlapped);
    _statsUpdating = false;
  }
  if (_statsHandle != INVALID_HANDLE_VALUE)
  {
    CloseHandle(_statsHandle);
    _statsHandle = INVALID_HANDLE_VALUE;
  }
}

bool LiveStatsBoardInfo::ProcessStats()
{
  if (!_statsUpdating) return false;

  // Process the statistics update
  if (!XHasOverlappedIoCompleted(&_statsOverlapped)) return false;

  // Done
  _statsUpdating = false;

  DWORD result = XGetOverlappedExtendedError(&_statsOverlapped);
  if (SUCCEEDED(result))
  {
    const XUSER_STATS_READ_RESULTS *results = reinterpret_cast<const XUSER_STATS_READ_RESULTS *>(_statsBuffer.Data());
    if (results->dwNumViews == 1)
    {
      const XUSER_STATS_VIEW &view = results->pViews[0];
      _total = view.dwTotalViewRows;
      _listRows = view.dwNumRows;
      // sort the list by rank
      Sort();

      // save rank of the first item.
      if(_listRows > 0)
      {
        XUSER_STATS_ROW &firstItem = view.pRows[0];
        _pageStart = firstItem.dwRank;
      }
      else
        _pageStart = 1;
    }   
  }

  // Free the resources
  if (_statsHandle != INVALID_HANDLE_VALUE)
  {
    CloseHandle(_statsHandle);
    _statsHandle = INVALID_HANDLE_VALUE;
  }

  return SUCCEEDED(result);
}

static int CompareLiveStatsItems(const XUSER_STATS_ROW *item0, const XUSER_STATS_ROW *item1)
{
  int rank0 = item0->dwRank;
  int rank1 = item1->dwRank;
  // players without rank on the bottom
  if (rank0 == 0 && rank1 > 0) return 1;
  else if (rank1 == 0 && rank0 > 0) return -1;
  // sort by the rank
  int diff = rank0 - rank1;
  if (diff != 0) return diff;
  // sort by the name
  return stricmp(item0->szGamertag, item1->szGamertag);
}

void LiveStatsBoardInfo::Sort()
{
  if (_statsUpdating) return;

  XUSER_STATS_READ_RESULTS *results = reinterpret_cast<XUSER_STATS_READ_RESULTS *>(_statsBuffer.Data());
  if (results->dwNumViews != 1) return;

  XUSER_STATS_VIEW &view = results->pViews[0];
  QSort(view.pRows, (int)view.dwNumRows, CompareLiveStatsItems);
}

int LiveStatsBoardInfo::GetPlayerPosition()
{
  XUSER_STATS_READ_RESULTS *results = reinterpret_cast<XUSER_STATS_READ_RESULTS *>(_statsBuffer.Data());
  if (results->dwNumViews != 1) return -1;

  XUSER_STATS_VIEW &view = results->pViews[0];
  for (DWORD i=0; i<view.dwNumRows; ++i)
  {
    XUSER_STATS_ROW &singleRow = view.pRows[i];
    if(singleRow.xuid == _player)
      return i;
  }
  return -1;
}

DisplayLiveStatsBoard::DisplayLiveStatsBoard(ControlsContainer *parent, RString name)
: Display(parent)
{
  // read the statistics description
  ParamEntryVal cls = Pars >> "CfgLiveStats" >> name;
  _board = cls >> "board";
  ParamEntryVal array = cls >> "Columns";
  int n = array.GetEntryCount();
  _info._columns.Realloc(n);
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = array.GetEntry(i);
    if (!entry.IsClass()) continue;

    LiveStatsBoardColumn &column = _info._columns.Append();
    column._id = entry >> "id";
    column._name = entry >> "name";
    column._width = entry >> "width";
    column._format = entry >> "format";
    column._align = entry >> "align";
  }

  _info._pageStart = 1;
  _friendsOnly = false;
  _playerEnabled = true;
  _selectPlayer = false;

  _info._friendsHandle = INVALID_HANDLE_VALUE;
  _info._friendsCount = 0;
  _info._statsUpdating = false;
  _info._statsHandle = INVALID_HANDLE_VALUE;
  _info._total = 0;
  _info._listRows = 0;

  _info.StatsInit(_board);

  RString resource("RscDisplayStatisticsCurrent");
  Load(resource);
  _info._colorPlayer = GetPackedColor(Pars >> resource >> "colorPlayer");
  _info._colorFriend = GetPackedColor(Pars >> resource >> "colorFriend");

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_LIVE_STATS_TITLE));
  if (text) text->SetText(cls >> "name");
  
  // read the local user info
  const XUSER_SIGNIN_INFO *info = GSaveSystem.GetUserInfo();
  if (info)
  {
    _info._player = info->xuid;
    _info._playerName = info->szUserName;
  }

  _info.StartEnumFriends();

  StatsGoTo(BPPlayer);
  UpdateButtons();
}

DisplayLiveStatsBoard::~DisplayLiveStatsBoard()
{
  _info.StopEnumFriends();
  _info.StopStats();
}

Control *DisplayLiveStatsBoard::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_LIVE_STATS_LIST)
  {
    _list = new CLiveStatsList(this, idc, cls, _info);
    return _list;
  }

  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_LIVE_STATS_COUNT:
    _count = GetStructuredText(ctrl); 
    break;
  }

  return ctrl;
}

void DisplayLiveStatsBoard::OnSimulate(EntityAI *vehicle)
{
  _info.ProcessEnumFriends();
  if (_info.ProcessStats())
  {
    // check if active player is present in this view
    if(_playerEnabled && _info._total != 0 && _info._listRows == 0)
    {
      _playerEnabled = false;
      StatsGoTo(BPBegin);
    }

    // check selection request
    if(_selectPlayer && _list)
    {
      // find player item in the list
      int playerPosition = _info.GetPlayerPosition();
      if(playerPosition != -1)
        _list->SetCurSel(playerPosition);
      else
        _list->SetCurSel(0);

      // clear flag
      _selectPlayer = false;
    }
    else
      _list->SetCurSel(0);

    UpdateButtons();
  }

  if (_count)
  {
    if (_info._statsUpdating)
      _count->SetText(RString());
    else
      _count->SetText(Format("%d", _info._total));
  }

  Display::OnSimulate(vehicle);
}

void DisplayLiveStatsBoard::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_LIVE_STATS_GAMER_CARD:
    if (!_info._statsUpdating)
    {
      ShowGamerCard();
    }
    break;
  case IDC_LIVE_STATS_PLAYER:
    if (!_info._statsUpdating && !_friendsOnly)
    {
      if(_playerEnabled)
        StatsGoTo(BPPlayer);
      else
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_XBOX_MSG_NO_BOARD_STATS));
    }
    break;
  case IDC_LIVE_STATS_BEGIN:
    if (!_info._statsUpdating && _info._pageStart > 1 && !_friendsOnly)
    {
      StatsGoTo(BPBegin);
    }
    break;
  case IDC_LIVE_STATS_END:
    if (!_info._statsUpdating && _info._pageStart + MAX_STAT_USERS <= _info._total && !_friendsOnly)
    {
      StatsGoTo(BPEnd);
    }
    break;
  case IDC_LIVE_STATS_PREV:
    if (!_info._statsUpdating && _info._pageStart > 1 && !_friendsOnly)
    {
      StatsGoTo(BPPrev);
    }
    break;
  case IDC_LIVE_STATS_NEXT:
    if (!_info._statsUpdating && _info._pageStart + MAX_STAT_USERS <= _info._total && !_friendsOnly)
    {
      StatsGoTo(BPNext);
    }
    break;
  case IDC_LIVE_STATS_FRIENDS:
    if (!_info._statsUpdating && _info._friendsCount > 0)
    {
      _friendsOnly = !_friendsOnly;
      if(_playerEnabled)
        StatsGoTo(BPPlayer);
      else
        StatsGoTo(BPBegin);
      UpdateButtons();
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayLiveStatsBoard::ShowGamerCard()
{
  if(_list)
  {
    int index = _list->GetCurSel();
    int userIndex = GSaveSystem.GetUserIndex();
    if(userIndex >= 0 && index >=0 && (_info._total > 0 || (_friendsOnly && _info._friendsCount > 0) ) )
    {
      const XUSER_STATS_ROW statsRow = _list->GetItem(index);
      XShowGamerCardUI(userIndex, statsRow.xuid);
    }
  }
}

void DisplayLiveStatsBoard::UpdateButtons()
{
  IControl *ctrl = GetCtrl(IDC_LIVE_STATS_PLAYER);
  if (ctrl) ctrl->EnableCtrl(!_info._statsUpdating && !_friendsOnly);
  ctrl = GetCtrl(IDC_LIVE_STATS_BEGIN);
  if (ctrl) ctrl->EnableCtrl(!_info._statsUpdating && _info._pageStart > 1 && !_friendsOnly);
  ctrl = GetCtrl(IDC_LIVE_STATS_END);
  if (ctrl) ctrl->EnableCtrl(!_info._statsUpdating && _info._pageStart + MAX_STAT_USERS <= _info._total && !_friendsOnly);
  ctrl = GetCtrl(IDC_LIVE_STATS_PREV);
  if (ctrl) ctrl->EnableCtrl(!_info._statsUpdating && _info._pageStart > 1 && !_friendsOnly);
  ctrl = GetCtrl(IDC_LIVE_STATS_NEXT);
  if (ctrl) ctrl->EnableCtrl(!_info._statsUpdating && _info._pageStart + MAX_STAT_USERS <= _info._total && !_friendsOnly);
  ctrl = GetCtrl(IDC_LIVE_STATS_FRIENDS);
  if (ctrl)
  {
    ctrl->EnableCtrl(!_info._statsUpdating && _info._friendsCount > 0);
    ITextContainer *text = GetTextContainer(ctrl);
    if (text) text->SetText(LocalizeString(_friendsOnly ? IDS_XBOX_SHOW_PLAYERS : IDS_XBOX_SHOW_FRIEND));
  }
  ctrl = GetCtrl(IDC_LIVE_STATS_LIST);
  if(ctrl)  ctrl->EnableCtrl(!_info._statsUpdating && (_list->GetSize() > 0 || (_friendsOnly && (_info._friendsCount > 0))) );

  ctrl = GetCtrl(IDC_LIVE_STATS_GAMER_CARD);
  if(ctrl)  ctrl->EnableCtrl(!_info._statsUpdating && (_list->GetSize() > 0 || (_friendsOnly && (_info._friendsCount > 0))) );
}

void DisplayLiveStatsBoard::StatsGoTo(BoardPos pos)
{
  if (_friendsOnly)
  {
    _info.StartStatsFriends();
    _selectPlayer = true;
  }
  else switch (pos)
  {
  case BPBegin:
    _info.StartStatsByRank(1);
    break;
  case BPEnd:
    {
      int pageStart = _info._total - MAX_STAT_USERS + 1;
      saturateMax(pageStart, 1);
      _info.StartStatsByRank(pageStart);
    }
    break;
  case BPPrev:
    {
      int pageStart = _info._pageStart - MAX_STAT_USERS;
      saturateMax(pageStart, 1);
      _info.StartStatsByRank(pageStart);
    }
    break;
  case BPNext:
    {
      int pageStart = _info._pageStart + MAX_STAT_USERS;
      saturateMin(pageStart, _info._total - MAX_STAT_USERS + 1);
      saturateMax(pageStart, 1);
      _info.StartStatsByRank(pageStart);
    }
    break;
  case BPPlayer:
    if(_playerEnabled)
    {
      _info.StartStatsByXuid(_info._player);
      _selectPlayer = true;
    }
    break;
  default:
    Fail("Statistics position");
    return;
  }
  UpdateButtons();
}

// Downloadable content is handled by Xbox Guide on Xbox 360
#if _XBOX_VER < 200

DisplayDownloadContent::DisplayDownloadContent(ControlsContainer *parent)
: Display(parent)
{
  Load("RscDisplayDownloadContent");
  StartProgress(LocalizeString(IDS_LOAD_WORLD));

  XONLINEOFFERING_ENUM_PARAMS params;
  params.dwOfferingType = XONLINE_OFFERING_SUBSCRIPTION | XONLINE_OFFERING_CONTENT;
  params.dwBitFilter    = 0xffffffff;
  params.wStartingIndex = 0; 
  params.wMaxResults = 8; 
  params.dwDescriptionIndex = GetXLanguage();
  
  // Determine the buffer size required for enumeration
  DWORD size = XOnlineOfferingEnumerateMaxSize(&params, 0); 
  _buffer.Init(size);

  HRESULT result = XOnlineOfferingEnumerate
  (
      0, &params,
      _buffer.Data(), size,
      NULL, &_task
  );
  if (!SUCCEEDED(result))
  {
    RptF("XOnlineOfferingEnumerate failed with error 0x%x", result);
    _task = NULL;
    _buffer.Delete();
  }

  ParamEntryVal cls = Pars >> "RscDisplayDownloadContent";
  _none = GlobLoadTextureUI(cls >> "none");
  _done = GlobLoadTextureUI(cls >> "done");
  _bad = GlobLoadTextureUI(cls >> "bad");

  UpdateDownloaded();

  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl) ctrl->EnableCtrl(_list && _list->GetCurSel() >= 0);
}

DisplayDownloadContent::~DisplayDownloadContent()
{
  if (_task)
  {
    XOnlineTaskClose(_task);
    _task = NULL;
    _buffer.Delete();
  }
}

Control *DisplayDownloadContent::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  
  switch (idc)
  {
  case IDC_DOWNLOAD_CONTENT_LIST:
    _list = GetListBoxContainer(ctrl);
    break;
  }

  return ctrl;
}

void DisplayDownloadContent::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (_list)
    {
      int sel = _list->GetCurSel();
      if (sel < 0) break;
      XONLINEOFFERING_INFO &item = _info[sel];
      CreateChild(new DisplayDownloadContentDetails(this, item, !IsDownloaded(item.OfferingId) || IsCorrupted(item.OfferingId)));
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayDownloadContent::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
  if (_task)
  {
    HRESULT result = XOnlineTaskContinue(_task);
    if (result == XONLINETASK_S_RUNNING) return;
    FinishProgress();
    if (FAILED(result))
    {
      RptF("XOnlineTaskContinue of content enumeration failed with error 0x%x", result);
      XOnlineTaskClose(_task);
      _task = NULL;
      _buffer.Delete();
      return;
    }

    // Extract the results
    PXONLINEOFFERING_INFO* info;
    DWORD items;
    BOOL partial;

    result = XOnlineOfferingEnumerateGetResults(_task, &info, &items, &partial);
    if (FAILED(result))
    {
      RptF("XOnlineOfferingEnumerateGetResults failed with error 0x%x", result);
      XOnlineTaskClose(_task);
      _task = NULL;
      _buffer.Delete();
      return;
    }

    // Update listbox
    if (_list)
    {
      int sel = _list->GetCurSel();
      saturateMax(sel, 0);
      for (DWORD i=0; i<items; i++)
      {
        XONLINEOFFERING_INFO &item = *info[i];
        RString text;
        if (item.dwTitleSpecificData > 0)
        {
          QFBank bank;
          FileBufferMemory *buffer = new FileBufferMemory(item.dwTitleSpecificData);
          memcpy(buffer->GetWritableData(), item.pbTitleSpecificData, item.dwTitleSpecificData);
          if (bank.open(buffer))
          {
            ParamFile file;
            file.ParseBinOrTxt(bank, "config.cpp", NULL, &globals);
            ParamEntryPtr entry = file.FindEntry("name");
            if (entry) text = *entry;
          }
        }
        if (text.GetLength() == 0) text = Format("ID: 0x%I64X", item.OfferingId);
        int index = _list->AddString(text);
        Texture *texture;
        if (IsDownloaded(item.OfferingId)) texture = _done;
        else if (IsCorrupted(item.OfferingId)) texture = _bad;
        else texture = _none;
        _list->SetTexture(index, texture);
        _info.Add(item);
      }
      _list->SetCurSel(sel, false);
    }

    if (!partial)
    {
      // Enumeration is complete
      XOnlineTaskClose(_task);
      _task = NULL;
      _buffer.Delete();
    }

    IControl *ctrl = GetCtrl(IDC_OK);
    if (ctrl) ctrl->EnableCtrl(_list && _list->GetCurSel() >= 0);
  }
}

void DisplayDownloadContent::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  if (idd == IDD_DOWNLOAD_CONTENT_DETAILS)
  {
    UpdateDownloaded();
    if (_list)
    {
      for (int i=0; i<_list->GetSize(); i++)
      {
        Texture *texture;
        if (IsDownloaded(_info[i].OfferingId)) texture = _done;
        else if (IsCorrupted(_info[i].OfferingId)) texture = _bad;
        else texture = _none;
        _list->SetTexture(i, texture);
      }
    }
  }
}

const int PreloadSize = 256 * 1024;

struct CheckFileFunction
{
  HANDLE _handle;
  QIFStream &_in;

  CheckFileFunction(QIFStream &in) : _in(in)
  {
    _handle = XCalculateSignatureBegin(XCALCSIG_FLAG_CONTENT);
  }
  void operator () (const void *buf, int size)
  {
    if (_handle != INVALID_HANDLE_VALUE)
      XCalculateSignatureUpdate(_handle, (const BYTE *)buf, size);
    
    // preloading
    int offset = _in.tellg();
    int end = offset + PreloadSize;
    end &= ~(PreloadSize - 1);
    _in.PreReadSequential(end - offset);
  }
};

bool CheckFileSignature(RString fullPath, RString localPath, HANDLE handle)
{
  // skip the metadata file
  if (stricmp(localPath, "contentmeta.xbx") == 0) return true;

  // read file
  QIFStream in;
  in.open(fullPath);
  int size = in.rest();

  // read signature
  DWORD signatureSize = XCALCSIG_SIGNATURE_SIZE;
  BYTE *signature = NULL;
  if (!XLocateSignatureByName(handle, localPath, 0, size, &signature, &signatureSize))
  {
    in.close();
    return false;
  }

  // calculate signature
  CheckFileFunction func(in);
  in.PreReadSequential(PreloadSize);
  in.Process(func);
  in.close();
  if (func._handle == INVALID_HANDLE_VALUE) return false;

  BYTE verify[XCALCSIG_SIGNATURE_SIZE];
  if (XCalculateSignatureEnd(func._handle, verify) != ERROR_SUCCESS) return false;

  // compare signatures
  return memcmp(verify, signature, signatureSize) == 0;
}

struct CheckFileContext
{
  bool ok;
  HANDLE handle;
  int prefix;
};

static bool CheckFile(const FileItem &file, CheckFileContext &ctx)
{
  RString fullPath = file.path + file.filename;
  RString localPath = fullPath.Substring(ctx.prefix, INT_MAX);

  if (CheckFileSignature(fullPath, localPath, ctx.handle))
    return false; // continue with enumeration

  ctx.ok = false;
  return true; // interrupt enumeration
}

bool CheckContent(const char *dir)
{
  HANDLE handle = XLoadContentSignatures(dir);
  if (handle == NULL) return false;

  RString path = RString(dir) + RString("\\");

  CheckFileContext ctx;
  ctx.ok = true;
  ctx.handle = handle;
  ctx.prefix = path.GetLength();
  ForEachFileR(path, CheckFile, ctx);
  
  XCloseContentSignatures(handle);
  return ctx.ok;
}

void DisplayDownloadContent::UpdateDownloaded()
{
  Ref<ProgressHandle> p = ProgressStart(LocalizeString(IDS_LOAD_WORLD));

  _downloaded.Resize(0);
  XCONTENT_FIND_DATA data;
  HANDLE handle = XFindFirstContent("T:\\", 0, &data);
  if (handle != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (CheckContent(data.szContentDirectory))
        _downloaded.Add(data.qwOfferingId);
      else
        _corrupted.Add(data.qwOfferingId);
      ProgressRefresh();
    } while (XFindNextContent(handle, &data));
    XFindClose(handle);
  }

  ProgressFinish(p);
}

bool DisplayDownloadContent::IsDownloaded(const XOFFERING_ID &id) const
{
  for (int j=0; j<_downloaded.Size(); j++)
  {
    if (id == _downloaded[j]) return true;
  }
  return false;
}

bool DisplayDownloadContent::IsCorrupted(const XOFFERING_ID &id) const
{
  for (int j=0; j<_corrupted.Size(); j++)
  {
    if (id == _corrupted[j]) return true;
  }
  return false;
}

DisplayDownloadContentDetails::DisplayDownloadContentDetails(ControlsContainer *parent, const XONLINEOFFERING_INFO &info, bool canDownload)
: Display(parent), _info(info)
{
  _alwaysShow = true;
  _ok = false;
  _restart = -1;
  _canDownload = canDownload;

  StartProgress(LocalizeString(IDS_LOAD_WORLD));

  Load("RscDisplayDownloadContentDetails");

  DWORD size = XOnlineOfferingDetailsMaxSize(0); 
  _buffer.Init(size);

  HRESULT result = XOnlineOfferingDetails
  (
    0, info.OfferingId, GetXLanguage(), GetXLanguage(),
    _buffer.Data(), size,
    NULL, &_task
  );
  if (!SUCCEEDED(result))
  {
    RptF("XOnlineOfferingDetails failed with error 0x%x", result);
    _task = NULL;
    _buffer.Delete();
  }
  UpdateButtons();
}

DisplayDownloadContentDetails::~DisplayDownloadContentDetails()
{
  if (_task)
  {
    XOnlineTaskClose(_task);
    _task = NULL;
    _buffer.Delete();
  }
}

Control *DisplayDownloadContentDetails::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  
  switch (idc)
  {
  case IDC_DOWNLOAD_CONTENT_TITLE:
    _title = GetTextContainer(ctrl);
    break;
/*
  case IDC_DOWNLOAD_CONTENT_PRICE:
    _price = GetStructuredText(ctrl);
    break;
*/
  case IDC_DOWNLOAD_CONTENT_TERMS:
    _terms = GetStructuredText(ctrl);
    break;
  case IDC_DOWNLOAD_CONTENT_PICTURE:
    _picture = dynamic_cast<CStatic *>(ctrl);
    break;
  }

  return ctrl;
}

RString GetPriceText(XONLINEOFFERING_INFO &info, XONLINEOFFERING_DETAILS &details)
{
  if (details.Price.fOfferingIsFree) return LocalizeString(IDS_XBOX_LIVE_FREE_CONTENT);

  // price
  DWORD size = 64;
  WCHAR buffer[64];
  HRESULT result = XOnlineOfferingPriceFormat(&details.Price, buffer, &size, 0);
  DoAssert(SUCCEEDED(result));
  RString price = FromWideChar(buffer);
  // tax
  RString tax;
  switch (details.Price.Tax)
  {
  case NO_TAX:
    tax = LocalizeString(IDS_XBOX_LIVE_NO_TAX);
    break;
  case DEFAULT:
    tax = LocalizeString(IDS_XBOX_LIVE_TAX);
    break;
  case GST:
    tax = LocalizeString(IDS_XBOX_LIVE_GST);
    break;
  case VAT:
    tax = LocalizeString(IDS_XBOX_LIVE_VAT);
    break;
  default:
    Fail("Tax");
    break;
  }
  // prefix
  RString prefix = Format
  (
    LocalizeString(IDS_XBOX_LIVE_PRICE),
    (const char *)price, (const char *)tax
  );

  // suffix
  RString suffix = RString(" ") + LocalizeString(IDS_XBOX_LIVE_SUFFIX);

  if (info.dwOfferingType == XONLINE_OFFERING_CONTENT) return prefix + suffix;

  Assert(info.dwOfferingType == XONLINE_OFFERING_SUBSCRIPTION)

  // frequency
  RString frequency;
  switch (details.Frequency)
  {
  case ONE_TIME_CHARGE:
    break;
  case MONTHLY:
    frequency = RString(" ") + LocalizeString(IDS_XBOX_LIVE_FREQUENCY_MONTHLY);
    break;
  case QUARTERLY:
    frequency = RString(" ") + LocalizeString(IDS_XBOX_LIVE_FREQUENCY_QUARTERLY);
    break;
  case BIANNUALLY:
    frequency = RString(" ") + LocalizeString(IDS_XBOX_LIVE_FREQUENCY_BIANNUALLY);
    break;
  case ANNUALLY:
    frequency = RString(" ") + LocalizeString(IDS_XBOX_LIVE_FREQUENCY_ANNUALLY);
    break;
  default:
    Fail("Frequency");
    break;
  }
  // period
  RString period;
  if (details.dwDuration > 0)
    period = RString(" ") + Format(LocalizeString(IDS_XBOX_LIVE_PERIOD), details.dwDuration);
  // free months
  RString freeMonths;
  if (details.dwFreeMonthsBeforeCharge > 0)
    freeMonths = RString(" ") + Format(LocalizeString(IDS_XBOX_LIVE_FREE_MONTHS), details.dwFreeMonthsBeforeCharge);

  return prefix + frequency + period + freeMonths + suffix;
}

void DisplayDownloadContentDetails::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (!_ok) return;
    if (_info.dwOfferingType == XONLINE_OFFERING_SUBSCRIPTION)
    {
      if (!IsBillingEnabled()) return;
      if (_details.dwInstances == 0)
        CreateChild(new DisplayDownloadContentPrice(this, _name, GetPriceText(_info, _details), LocalizeString(IDS_MSG_CONFIRM_PURCHASE_SUBSCRIBTION), IDD_MSG_SUBSCRIBE));
        // CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_CONFIRM_PURCHASE_SUBSCRIBTION), IDD_MSG_SUBSCRIBE);
      else {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_CONFIRM_CANCEL_SUBSCRIBTION), IDD_MSG_CANCEL_SUBSCRIPTION, false, &buttonOK, &buttonCancel);
      }
    }
    else
    {
      Assert(_info.dwOfferingType == XONLINE_OFFERING_CONTENT);
      if (!_canDownload) return;
      if (!_details.Price.fOfferingIsFree && _details.dwInstances == 0)
      {
        if (!IsBillingEnabled()) return;
        CreateChild(new DisplayDownloadContentPrice(this, _name, GetPriceText(_info, _details), LocalizeString(IDS_MSG_CONFIRM_PURCHASE_CONTENT), IDD_MSG_PURCHASE));
        // CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_CONFIRM_PURCHASE_CONTENT), IDD_MSG_PURCHASE);
      }
      else
      {
        Install();
      }
    }
    break;
  case IDC_DOWNLOAD_CONTENT_SHOW_PRICE:
    // price info
    if (!_ok) return true;
    if (_info.dwOfferingType == XONLINE_OFFERING_CONTENT && _details.Price.fOfferingIsFree) return true;
    CreateChild(new DisplayDownloadContentPrice(this, _name, GetPriceText(_info, _details), RString()));
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayDownloadContentDetails::UpdateButtons()
{
  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl)
  {
    if (!_ok)
    {
      ctrl->ShowCtrl(false);
    }
    else if (_info.dwOfferingType == XONLINE_OFFERING_SUBSCRIPTION)
    {
      ctrl->ShowCtrl(true);
      ITextContainer *text = GetTextContainer(ctrl);
      if (text)
      {
        if (!IsBillingEnabled()) text->SetText(LocalizeString(IDS_XBOX_HINT_BILLING_DISABLED));
        else if (_details.dwInstances == 0) text->SetText(LocalizeString(IDS_DISP_XBOX_HINT_SUBSCRIBE));
        else text->SetText(LocalizeString(IDS_DISP_XBOX_HINT_CANCEL_SUBSCRIPTION));
      }
    }
    else
    {
      Assert(_info.dwOfferingType == XONLINE_OFFERING_CONTENT);
      ctrl->EnableCtrl(_canDownload);
      ITextContainer *text = GetTextContainer(ctrl);
      if (text)
      {
        if (!_details.Price.fOfferingIsFree && _details.dwInstances == 0)
        {
          if (!IsBillingEnabled()) text->SetText(LocalizeString(IDS_XBOX_HINT_BILLING_DISABLED));
          else text->SetText(LocalizeString(IDS_DISP_XBOX_HINT_PURCHASE));
        }
        else text->SetText(LocalizeString(IDS_DISP_XBOX_HINT_INSTALL));
      }
    }
  }
  ctrl = GetCtrl(IDC_DOWNLOAD_CONTENT_SHOW_PRICE);
  if (ctrl)
  {
    ctrl->EnableCtrl(_ok && (_info.dwOfferingType != XONLINE_OFFERING_CONTENT || !_details.Price.fOfferingIsFree));
  }
}

void DisplayDownloadContentDetails::OnDraw(EntityAI *vehicle, float alpha)
{
  Display::OnDraw(vehicle, alpha);
  if (_restart >= 0)
  {
    if (--_restart > 0) return;
    // restart is needed
#ifdef _XBOX
    XONLINE_USER *user = XOnlineGetLogonUsers();
    Assert(user);
    if (user)
    {
      LAUNCH_DATA ld;
      ZeroMemory(&ld, sizeof(ld));
      sprintf
        (
        reinterpret_cast<char *>(ld.Data),
        "-download \"-name=%s\" \"-gamertag=%s\"",
        (const char *)Glob.header.GetPlayerName(),
        user->szGamertag
        );
#if _SUPER_RELEASE
      RString name = "D:\\default.xbe";
#else
      DM_XBE info;
      DmGetXbeInfo("", &info);
      RString name = RString("D:\\") + RString(GetFilenameExt(info.LaunchPath));
#endif

      // show progress screen during reboot
      RString progressText = LocalizeString(IDS_LOAD_WORLD);
      Ref<ProgressHandle> p = ProgressStartExt(true, progressText, "%s", true, 0);

      // Persist display may need a lot of memory
      FreeOnDemandGarbageCollectMain(3*1024*1024,2*1024*1024);
      if (GEngine) GEngine->PersistDisplay();

      XLaunchNewImage(name, &ld);
      Fail("Unaccessible");
    }
#endif
  }
}

void DisplayDownloadContentDetails::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
  if (_task)
  {
    HRESULT result = XOnlineTaskContinue(_task);
    if (result == XONLINETASK_S_RUNNING) return;
    FinishProgress();
    if (FAILED(result))
    {
      RptF("XOnlineTaskContinue of content details failed with error 0x%x", result);
      XOnlineTaskClose(_task);
      _task = NULL;
      _buffer.Delete();
      return;
    }

    // Extract the results
    result = XOnlineOfferingDetailsGetResults(_task, &_details);
    if (FAILED(result))
    {
      RptF("XOnlineOfferingDetailsGetResults failed with error 0x%x", result);
      XOnlineTaskClose(_task);
      _task = NULL;
      _buffer.Delete();
      return;
    }

    _ok = true;
    FileBufferMemory *buffer = new FileBufferMemory(_details.dwDetailsBuffer);
    memcpy(buffer->GetWritableData(), _details.pbDetailsBuffer, _details.dwDetailsBuffer);
    bool opened = _bank.open(buffer);

    // Update controls
    RString terms;
    RString picture;

    if (opened)
    {
      ParamFile file;
      file.ParseBinOrTxt(_bank, "config.cpp", NULL, &globals);
      ParamEntryPtr entry = file.FindEntry("name");
      if (entry) _name = *entry;
      entry = file.FindEntry("description");
      if (entry) terms = *entry;
      entry = file.FindEntry("picture");
      if (entry) picture = *entry;
    }

    if (_title)
    {
      if (_name.GetLength() == 0) _name = Format("ID: 0x%I64X", _info.OfferingId);
      _title->SetText(_name);
    }
    if (_terms)
    {
      _terms->SetText(terms);
    }
    if (_picture && picture.GetLength() > 0)
    {
      QIFStreamB *stream = new QIFStreamB();
      stream->open(_bank, picture);
      if (!stream->fail())
      {
        ITextureSource *source = CreatePacMemTextureSource(stream, picture);
        _picture->SetTexture(GlobLoadTexture(source));
      }
    }
/*
    if (_price) _price->SetText(GetPriceText(_info, _details));
*/
    UpdateButtons();

    // Finish
    XOnlineTaskClose(_task);
    _task = NULL;
    _buffer.Delete();
  }
}

void DisplayDownloadContentDetails::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_MSG_CANCEL_SUBSCRIPTION:
    if (exit != IDC_OK) break;
    if (Unsubscribe())
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_CANCEL_SUBSCRIPTION_SUCCEEDED), IDD_MSG_INSTALL_RESULT);
    else
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_CANCEL_SUBSCRIPTION_FAILED), IDD_MSG_INSTALL_RESULT);
    break;
  case IDD_MSG_SUBSCRIBE:
    if (exit != IDC_OK) break;
    if (Purchase())
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_SUBSCRIPTION_SUCCEEDED), IDD_MSG_INSTALL_RESULT);
    else
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PURCHASE_FAILED), IDD_MSG_INSTALL_RESULT);
    break;
  case IDD_MSG_PURCHASE:
    if (exit != IDC_OK) break;
    if (Purchase())
      Install();
    else
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PURCHASE_FAILED), IDD_MSG_INSTALL_RESULT);
    break;
  case IDD_DOWNLOAD_CONTENT_INSTALL:
    if (exit == IDC_ABORT)
      Exit(IDC_CANCEL);
    else if (exit == IDC_OK)
    {
      char dir[MAX_PATH];
      bool restart = false;
      if (XGetContentInstallLocationFromIDs(0, _info.OfferingId, dir))
      {
        restart = QIFileFunctions::DirectoryExists(RString(dir) + RString("\\Addons"));
      }
      if (restart) _restart = 2;
      else Exit(IDC_CANCEL);
      // CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_INSTALL_SUCCEEDED), IDD_MSG_INSTALL_RESULT);
    }
    else // IDC_CANCEL
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_INSTALL_FAILED), IDD_MSG_INSTALL_RESULT);
    break;
  case IDD_MSG_INSTALL_RESULT:
    Exit(IDC_CANCEL);
    break;
  }
}

bool DisplayDownloadContentDetails::IsBillingEnabled() const
{
  XONLINE_USER *user = XOnlineGetLogonUsers();
  Assert(user);
  return XOnlineIsUserPurchaseAllowed(user->xuid.dwUserFlags);
}

void DisplayDownloadContentDetails::Install()
{
  if (CheckDiskSpace(this, "T:\\", _info.dwInstallSize + 20))
  {
    CreateChild(new DisplayDownloadContentInstall(this, _info.OfferingId, _name));
  }
}

IProgressDisplay *CreateDisplayProgress(RString text, RString resource);

bool DisplayDownloadContentDetails::Purchase()
{
  XONLINETASK_HANDLE task;
  HRESULT result = XOnlineOfferingPurchase(0, _info.OfferingId, NULL, &task);

  if (FAILED(result))
  {
    RptF("XOnlineOfferingPurchase failed with error 0x%x", result);
    return false;
  }

  ProgressReset();
  Ref<ProgressHandle> p = ProgressStartExt(true, CreateDisplayProgress(LocalizeString(IDS_XBOX_PURCHASING_CONTENT), "RscDisplayNotFreezeBig"), true, 0);
  do
  {
    result = XOnlineTaskContinue(task); 
    ProgressFrame();
  } while (result == XONLINETASK_S_RUNNING);
  ProgressFinish(p);
  
  XOnlineTaskClose(task);

  if (FAILED(result))
  {
    RptF("XOnlineTaskContinue of purchase content failed with error 0x%x", result);
    return false;
  }

  return true;
}

bool DisplayDownloadContentDetails::Unsubscribe()
{
  XONLINETASK_HANDLE task;
  HRESULT result = XOnlineOfferingCancel(0, _info.OfferingId, NULL, &task);

  if (FAILED(result))
  {
    RptF("XOnlineOfferingCancel failed with error 0x%x", result);
    return false;
  }

  ProgressReset();
  Ref<ProgressHandle> p = ProgressStart(CreateDisplayProgress(LocalizeString(IDS_XBOX_CANCELLING_SUBSCTIPTION), "RscDisplayNotFreezeBig"));
  do
  {
    result = XOnlineTaskContinue(task); 
    ProgressFrame();
  } while (result == XONLINETASK_S_RUNNING);
  ProgressFinish(p);
  
  XOnlineTaskClose(task);

  if (FAILED(result))
  {
    RptF("XOnlineTaskContinue of cancel subscription failed with error 0x%x", result);
    return false;
  }

  return true;
}

DisplayDownloadContentPrice::DisplayDownloadContentPrice(ControlsContainer *parent, RString title, RString price, RString question, int idd)
: Display(parent)
{
  Load("RscDisplayDownloadContentPrice");
  
  _idd = idd;
  if (_title) _title->SetText(title);
  if (_price) _price->SetText(price);
  if (_question) _question->SetText(question);

  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl) ctrl->EnableCtrl(_idd != -1);
}

Control *DisplayDownloadContentPrice::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_DOWNLOAD_CONTENT_PRICE_TITLE:
    _title = GetTextContainer(ctrl);
    break;
  case IDC_DOWNLOAD_CONTENT_PRICE:
    _price = GetStructuredText(ctrl);
    break;
  case IDC_DOWNLOAD_CONTENT_PRICE_QUESTION:
    _question = GetTextContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayDownloadContentPrice::OnButtonClicked(int idc)
{
  if (idc == IDC_OK && _idd == -1) return;
  Display::OnButtonClicked(idc);
}

static bool UnlockFile(const FileItem &file, bool &ok)
{
  if (!QIFileFunctions::Unlink(file.path + file.filename)) ok = false;
  return false; // continue with enumeration
}

DisplayDownloadContentInstall::DisplayDownloadContentInstall(ControlsContainer *parent, const XOFFERING_ID &id, RString name)
: Display(parent)
{
  Load("RscDisplayDownloadContentInstall");

  if (_title) _title->SetText(name);

  // remove old content
  char dir[MAX_PATH];
  if (XGetContentInstallLocationFromIDs(0, id, dir))
  {
    // unlock files in directory
    bool ok = true;
    ForEachFileR(RString(dir) + RString("\\"), UnlockFile, ok);

    // delete package
    XRemoveContent(dir);
  }

  HRESULT result = XOnlineContentInstall(id, NULL, &_task);
  if (FAILED(result))
  {
    RptF("XOnlineContentInstall failed with error 0x%x", result);
    Exit(IDC_CANCEL);
  }
}

DisplayDownloadContentInstall::~DisplayDownloadContentInstall()
{
  if (_task)
  {
    XOnlineTaskClose(_task);
    _task = NULL;
  }
}

Control *DisplayDownloadContentInstall::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_INSTALL_CONTENT_PROGRESS:
    if (type == CT_PROGRESS)
    {
      _progress = static_cast<CProgressBar *>(ctrl);
      _progress->SetRange(0, 100);
      _progress->SetPos(0);
    }
    break;
  case IDC_INSTALL_CONTENT_NAME:
    _title = GetTextContainer(ctrl);
    break;
  case IDC_INSTALL_CONTENT_TIME:
    if (type == CT_ANIMATED_TEXTURE)
      _time = static_cast<CAnimatedTexture *>(ctrl);
    break;
  }
  return ctrl;
}

void DisplayDownloadContentInstall::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    break;
  case IDC_CANCEL:
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_MSG_CONFIRM_INSTALL_ABORT), IDD_MSG_INSTALL_ABORT, false, &buttonOK, &buttonCancel); 
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayDownloadContentInstall::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);

  if (_time)
  {
    float factor = fastFmod(GlobalTickCount() * (1.0f / 3000), 1);
    _time->SetPhase(factor);
  }

  // Update download progress
  DWORD percent;
  ULONGLONG bytesInstalled;
  ULONGLONG bytesTotal;
  
  HRESULT result = XOnlineContentInstallGetProgress(_task,  &percent, &bytesInstalled, &bytesTotal);
  if (SUCCEEDED(result))
  {
    _progress->SetPos(percent);
  }

  result = XOnlineTaskContinue(_task);
  if (result == XONLINETASK_S_RUNNING) return;

  XOnlineTaskClose(_task);
  _task = NULL;

  if (FAILED(result))
  {
    RptF("XOnlineTaskContinue of content installation failed with error 0x%x", result);
    Exit(IDC_CANCEL);
  }
  else Exit(IDC_OK);
}

void DisplayDownloadContentInstall::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_MSG_INSTALL_ABORT:
    if (exit == IDC_OK) Exit(IDC_ABORT);
    break;
  }
}
#endif // _XBOX_VER < 200

#endif // _XBOX_SECURE && _ENABLE_MP

struct CPlayersItem
{
#if _XBOX_SECURE
  XUID xuid;
#endif
  RString name;
  int dpnid;
  int kills;
  int killed;
};
TypeIsMovableZeroed(CPlayersItem)

int CmpCPlayersItem(const CPlayersItem *item1, const CPlayersItem *item2)
{
  int value = item2->kills - item1->kills;
  if (value != 0) return value;
  value = item1->killed - item2->killed;
  if (value != 0) return value;
  return stricmp(item1->name, item2->name);
}

//! Control for displaying list of players
class CPlayers : public CListBox
{
friend class DisplayXPlayers;

protected:
  AutoArray<CPlayersItem> _players;

#if _XBOX_SECURE
  int _friendsCount;
  XONLINE_FRIEND _friends[MAX_FRIENDS];
#endif

  PackedColor _color;
  PackedColor _colorWest;
  PackedColor _colorEast;
  PackedColor _colorCiv;
  PackedColor _colorRes;
  PackedColor _colorSelectWest;
  PackedColor _colorSelectEast;
  PackedColor _colorSelectCiv;
  PackedColor _colorSelectRes;

  Ref<Texture> _communicatorMUTED;
  Ref<Texture> _communicatorON;
  Ref<Texture> _communicatorTV;
  Ref<Texture> _friendInviteReceived;
  Ref<Texture> _friendInviteSent;
  Ref<Texture> _friendOnline;

public:
  CPlayers(ControlsContainer *parent, int idc, ParamEntryPar cls);

  int GetSize() const {return _players.Size();}
  // multiple selection is not allowed
  bool IsSelected(int row) const {return row == GetCurSel();}
  const CPlayersItem &GetPlayer(int i) const {return _players[i];}
#if _XBOX_SECURE
  const CPlayersItem *FindPlayer(XUID xuid) const;
  const XONLINE_FRIEND *GetFriend(XUID xuid) const;
#endif

  void OnDraw(UIViewport *vp, float alpha);
  void DrawItem
  (
    UIViewport *vp, float alpha, int i, bool selected, float top,
    const Rect2DFloat &rect, float widthDiff
  ) override;

protected:
  void DrawRow
  (
    int columns, const float *colWidths, const bool *doubleLines, const RString *values,
    float top, Rect2DFloat &rect, PackedColor color, PackedColor lineColor, CPlayersItem &player
  );
};

CPlayers::CPlayers(ControlsContainer *parent, int idc, ParamEntryPar cls)
: CListBox(parent, idc, cls)
{
#if _XBOX_SECURE
  _friendsCount = 0;
#endif

  _communicatorMUTED = GlobLoadTextureUI("xmisc\\communicatormuted.paa");
  _communicatorON = GlobLoadTextureUI("xmisc\\communicatoron.paa");
  _communicatorTV = GlobLoadTextureUI("xmisc\\communicatortv.paa");
  _friendInviteReceived = GlobLoadTextureUI("xmisc\\friendinvitereceived.paa");
  _friendInviteSent = GlobLoadTextureUI("xmisc\\friendinvitesent.paa");
  _friendOnline = GlobLoadTextureUI("xmisc\\friendonline.paa");

  _color = _ftColor;
  _colorWest = GetPackedColor(cls >> "colorWest");
  _colorEast = GetPackedColor(cls >> "colorEast");
  _colorCiv = GetPackedColor(cls >> "colorCiv");
  _colorRes = GetPackedColor(cls >> "colorRes");
  _colorSelectWest = GetPackedColor(cls >> "colorSelectWest");
  _colorSelectEast = GetPackedColor(cls >> "colorSelectEast");
  _colorSelectCiv = GetPackedColor(cls >> "colorSelectCiv");
  _colorSelectRes = GetPackedColor(cls >> "colorSelectRes");

  const AutoArray<PlayerIdentity> &identities = *GetNetworkManager().GetIdentities();
  int n = identities.Size();
  _players.Resize(n);
  for (int i=0; i<n; i++)
  {
    _players[i].dpnid = identities[i].dpnid;
    _players[i].name = identities[i].GetName();
    _players[i].kills = 0;
    _players[i].killed = 0;
#if _XBOX_SECURE
    _players[i].xuid = identities[i].xuid;
#endif
  }

  // sort players by initial score
  QSort(_players.Data(), _players.Size(), CmpCPlayersItem);

#if _XBOX_SECURE && _ENABLE_MP
  const AutoArray<RecentPlayerInfo> *recentPlayers = GetNetworkManager().GetRecentPlayers();
  if (recentPlayers) for (int i=0; i<recentPlayers->Size(); i++)
  {
    const RecentPlayerInfo &info = recentPlayers->Get(i);

    // check if player is not connected again
    bool found = false;
    for (int j=0; j<n; j++)
    {
      if (XOnlineAreUsersIdentical(identities[j].xuid, info.xuid))
      {
        found = true;
        break;
      }
    }
    if (found) continue;

    int index = _players.Add();
    _players[index].dpnid = 0;
    _players[index].name = info.name;
    _players[index].xuid = info.xuid;
    _players[index].kills = 0;
    _players[index].killed = 0;
  }
#endif

  SetCurSel(0);
}

void CPlayers::OnDraw(UIViewport *vp, float alpha)
{
  // add new players
  const AutoArray<PlayerIdentity> &identities = *GetNetworkManager().GetIdentities();
  if (!&identities) return;
  int n = identities.Size();
  for (int i=0; i<n; i++)
  {
    const PlayerIdentity &identity = identities[i];
#if _XBOX_SECURE && _ENABLE_MP
    if (identity.xuid != INVALID_XUID)
    {
      // find by name
      bool found = false;
      for (int j=0; j<_players.Size(); j++)
      {
        if (strcmp(_players[j].name, identity.name) == 0)
        {
          _players[j].dpnid = identity.dpnid;
          _players[j].xuid = identity.xuid;
          found = true;
          break;
        }
      }
      if (found) continue;
    }
    else
#endif
    {
      // find by dpnid
      bool found = false;
      for (int j=0; j<_players.Size(); j++)
      {
        if (_players[j].dpnid == identity.dpnid)
        {
          found = true;
          break;
        }
      }
      if (found) continue;
    }

    int index = _players.Add();
    _players[index].dpnid = identities[i].dpnid;
    _players[index].name = identities[i].GetName();
    _players[index].kills = 0;
    _players[index].killed = 0;
#if _XBOX_SECURE
    _players[index].xuid = identities[i].xuid;
#endif
  }

  CListBox::OnDraw(vp, alpha);
}

void CPlayers::DrawRow
(
  int columns, const float *colWidths, const bool *doubleLines, const RString *values,
  float top, Rect2DFloat &rect, PackedColor color, PackedColor lineColor, CPlayersItem &player
)
{
  const float w = GEngine->Width2D();
  const float h = GEngine->Height2D();

  const float border = TEXT_BORDER;

  // bottom line
  float cx1 = CX(rect.x);
  float cx2 = CX(rect.x + rect.w);
  float cy = CY(top + _rowHeight);
  GEngine->DrawLine(Line2DPixel(cx1, cy, cx2, cy), lineColor, lineColor);

  Rect2DFloat itemRect(rect.x, top, 0, _rowHeight);
  Rect2DFloat clipRect(itemRect);
  saturateMax(clipRect.y, rect.y);
  saturateMin(clipRect.h, rect.y + rect.h - clipRect.y);
  for (int i=0; i<columns; i++)
  {
    float width = colWidths[i] * rect.w;
    clipRect.w = itemRect.w = width;

    // line
    float cx = CX(itemRect.x + itemRect.w);
    float cy1 = CY(clipRect.y);
    float cy2 = CY(clipRect.y + clipRect.h);
    if (doubleLines[i])
    {
      GEngine->DrawLine(Line2DPixel(cx - 1, cy1, cx - 1, cy2), lineColor, lineColor);
      GEngine->DrawLine(Line2DPixel(cx + 1, cy1, cx + 1, cy2), lineColor, lineColor);
    }
    else
      GEngine->DrawLine(Line2DPixel(cx, cy1, cx, cy2), lineColor, lineColor);

    float l = itemRect.x + border;
    float t = itemRect.y + 0.5 * (_rowHeight - _size);
#if _XBOX_SECURE && _ENABLE_MP
    if (i == 0)
    {
      const XONLINE_FRIEND *f = NULL;
      if (player.xuid != INVALID_XUID && player.dpnid != GetNetworkManager().GetPlayer())
        f = GetFriend(player.xuid);

      const PlayerIdentity *identity = GetNetworkManager().FindIdentity(player.dpnid);

      // voice icon
      Texture *texture = NULL;
      PackedColor color = PackedWhite;
      if (player.dpnid == GetNetworkManager().GetPlayer())
      {
        if (GetNetworkManager().IsVoiceRecording())
        {
          texture = _communicatorON;
          color = PackedColor(Color(1, 0, 0, 1));
        }
        else if (identity && identity->voiceOn)
        {
          texture = _communicatorON;
        }
        else
        {
          // no icon
        }
      }
      else
      {
        if (player.xuid == INVALID_XUID)
        {
        }
        else if (GetNetworkManager().IsPlayerInMuteList(player.xuid))
        {
          texture = _communicatorMUTED;
        }
        else if (GetNetworkManager().IsVoicePlaying(player.dpnid))
        {
          texture = _communicatorON;
          color = PackedColor(Color(1, 0, 0, 1));
        }
        else if (identity && identity->voiceOn)
        {
          texture = _communicatorON;
        }
        else
        {
          // no icon
        }
      }
      MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
      float width = 0.75 * _rowHeight;
      if (texture) GEngine->Draw2D
      (
        mip, color,
        Rect2DPixel(CX(l), CY(top), CW(width), CH(_rowHeight)),
        Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );
      l += width;

      // state icon
      if (!f)
      {
        texture = NULL;
      }
      else if (f->dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST)
      {
        texture = _friendInviteReceived;
      }
      else if (f->dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST)
      {
        texture = _friendInviteSent;
      }
      else
      {
        texture = _friendOnline;
      }
      mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
      width = 0.75 * _rowHeight;
      if (texture) GEngine->Draw2D
      (
        mip, PackedWhite,
        Rect2DPixel(CX(l), CY(top), CW(width), CH(_rowHeight)),
        Rect2DPixel(rect.x * w, rect.y * h, rect.w * w, rect.h * h)
      );
      l += width;
    }
#endif
    // text
    GEngine->DrawText(Point2DFloat(l, t), _size, clipRect, _font, color, values[i], _shadow);

    // next line
    itemRect.x += width;
    clipRect.x += width;
  }
}

void CPlayers::DrawItem
(
  UIViewport *vp, float alpha, int i, bool selected, float top,
  const Rect2DFloat &rect, float widthDiff
)
{
  CPlayersItem &player = _players[i];
  const PlayerIdentity *identity = NULL;
  if (player.dpnid != 0)
  {
    identity = GetNetworkManager().FindIdentity(player.dpnid);

    if (!identity) player.dpnid = 0; // player was disconnected meanwhile
  }

  PackedColor lineColor = _color;
  PackedColor color = _color;

  Rect2DFloat newRect(rect);
  newRect.w = _w - _sbWidth; // make design independent on scrollbar existence

  if (selected)
  {
    PackedColor bgColor = ModAlpha(_selBgColor, alpha);
    MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
    Rect2DAbs rectA;
    GEngine->Convert(rectA, Rect2DFloat(newRect.x, top, newRect.w, _rowHeight));
    GEngine->Draw2D(mip, bgColor, rectA);
  }

  // TODO: sign of local player

  if (player.dpnid == 0)
  {
    // disconnected player
    const int n = 2;
    const float cw[n] =
    {
      0.52,   // player
      0.48    // "disconnected" message
    };
    const bool dl[n] =
    {
      true,   // player
      false   // "disconnected" message
    };
    const RString v[n] =
    {
      player.name,  // player
      LocalizeString(IDS_XBOX_LIVE_PLAYER_DISCONNECTED) // "disconnected" message
    };
    DrawRow(n, cw, dl, v, top, newRect, color, lineColor, player);
  }
  else
  {
    // connected player
    const int n = 1;
    const float cw[n] =
    {
      0.52,   // player
    };
    const bool dl[n] =
    {
      true,   // player
    };
    const RString v[n] =
    {
      player.name,  // player
    };
    DrawRow(n, cw, dl, v, top, newRect, color, lineColor, player);
  }
}

#if _XBOX_SECURE && _ENABLE_MP
const XONLINE_FRIEND *CPlayers::GetFriend(XUID xuid) const
{
  for (int i=0; i<_friendsCount; i++)
    if (XOnlineAreUsersIdentical(_friends[i].xuid, xuid)) return &_friends[i];
  return NULL;
}

const CPlayersItem *CPlayers::FindPlayer(XUID xuid) const
{
  for (int i=0; i<_players.Size(); i++)
  {
    const CPlayersItem &player = _players[i];
    if (XOnlineAreUsersIdentical(player.xuid, xuid)) return &player;
  }
  return NULL;
}

#endif

#if _XBOX_SECURE && _ENABLE_MP
class DisplayPlayersOptions : public Display
{
protected:
  RString _name;
  bool _muted;

public:
  DisplayPlayersOptions(ControlsContainer *parent, RString resource, RString name, bool muted);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnSimulate(EntityAI *vehicle);

  RString GetName() const {return _name;}
  bool IsMuted() const {return _muted;}

protected:
  void UpdateButtons();
};

DisplayPlayersOptions::DisplayPlayersOptions(ControlsContainer *parent, RString resource, RString name, bool muted)
: Display(parent)
{
  _name = name;
  _muted = muted;
  Load(resource);
  UpdateButtons();
}

void DisplayPlayersOptions::OnSimulate(EntityAI *vehicle)
{
  Display::OnSimulate(vehicle);
  UpdateButtons();
}

void DisplayPlayersOptions::UpdateButtons()
{
  bool isVoice = GetNetworkManager().IsVoice();
  bool isSystemLink = GetNetworkManager().IsSystemLink();
  bool isSignedIn = GetNetworkManager().IsSignedIn();
  {
    IControl *ctrl = GetCtrl(IDC_XPLAYERS_ASK_VOICE);
    if (ctrl) ctrl->EnableCtrl(isVoice && isSignedIn && !isSystemLink);
  }
  {
    IControl *ctrl = GetCtrl(IDC_XPLAYERS_MUTE);
    if (ctrl) ctrl->EnableCtrl(isSignedIn && !isSystemLink);
  }
  {
    IControl *ctrl = GetCtrl(IDC_XPLAYERS_ASK);
    if (ctrl) ctrl->EnableCtrl(isSignedIn && !isSystemLink);
  }
  {
    IControl *ctrl = GetCtrl(IDC_XPLAYERS_FEEDBACK);
    if (ctrl) ctrl->EnableCtrl(isSignedIn && !isSystemLink);
  }
}

Control *DisplayPlayersOptions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_XPLAYERS_PLAYER:
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text) text->SetRawText(_name);
    }
    break;
  case IDC_XPLAYERS_MUTE:
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text)
      {
        if (_muted)
          text->SetText(LocalizeString(IDS_DISP_XBOX_PLAYER_ACTIVE));
        else
          text->SetText(LocalizeString(IDS_DISP_XBOX_PLAYER_STOP));
      }
      ctrl->EnableCtrl(!GetNetworkManager().IsSystemLink());
    }
    break;
  case IDC_XPLAYERS_FEEDBACK:
    // feedback not possible for system link sessions
    ctrl->EnableCtrl(!GetNetworkManager().IsSystemLink());
    break;
  case IDC_XPLAYERS_ASK:
  case IDC_XPLAYERS_ASK_VOICE:
    // friendship not possible when not signed-in
    ctrl->EnableCtrl(GetNetworkManager().IsSignedIn());
    break;
  }
  return ctrl;
}

void DisplayPlayersOptions::OnButtonClicked(int idc)
{
  // exit on every button
  Exit(idc);
}

// Friends, feedback and invitations are handled by Xbox Guide on Xbox 360
#if _XBOX_VER < 200

DisplayFeedback::DisplayFeedback(ControlsContainer *parent, RString name, RString resource)
: Display(parent)
{
  _name = name;
  Load(resource);
}

Control *DisplayFeedback::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_FEEDBACK_PLAYER:
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text) text->SetRawText(_name);
    }
    break;
  case IDC_FEEDBACK_DATE:
    {
      CStructuredText *text = GetStructuredText(ctrl);
      if (text)
      {
        time_t now;
        time(&now);
        tm *t = localtime(&now);
        char buffer[256];
        strftime(buffer, 256, LocalizeString(IDS_FEEDBACK_DATE_FORMAT), t);
        text->SetText(buffer);
      }
    }
    break;
  }
  return ctrl;
}

void DisplayFeedback::OnButtonClicked(int idc)
{
  // confirmation is needed for negative feedback
  switch (idc)
  {
  case IDC_FEEDBACK_BAD_NAME:
  case IDC_FEEDBACK_CURSING:
  case IDC_FEEDBACK_SCREAM:
  case IDC_FEEDBACK_CHEAT:
  case IDC_FEEDBACK_THREAT:
  case IDC_FEEDBACK_MSG_HARASSING:
  case IDC_FEEDBACK_MSG_OFFENSIVE:
  case IDC_FEEDBACK_MSG_SPAM:
    _wantedExit = idc;
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(
        MB_BUTTON_OK | MB_BUTTON_CANCEL,
        LocalizeString(IDS_MSG_CONFIRM_NEGATIVE_FEEDBACK),
        IDD_MSG_NEGATIVE_FEEDBACK, false, &buttonOK, &buttonCancel);
    }
    return;
  }

  // exit on every button
  Exit(idc);
}

void DisplayFeedback::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);
  switch (idd)
  {
  case IDD_MSG_NEGATIVE_FEEDBACK:
    if (exit == IDC_OK) Exit(_wantedExit);
    break;
  }
}

#endif // _XBOX_VER < 200

#endif // _XBOX_SECURE && _ENABLE_MP

DisplayXPlayers::DisplayXPlayers(ControlsContainer *parent, AutoCancelFunction *autoCancel)
: Display(parent)
{
#ifdef _WIN32
  LockController();
#endif

  Load("RscDisplayPlayers");

  _autoCancel = autoCancel;

  // Friends are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
  _taskFriendList = NULL;
  _taskMuteList = NULL;
  _taskFeedback = NULL;
  _muteListNeedUpdate = false;

  if (!GetNetworkManager().IsSystemLink() && GetNetworkManager().IsSignedIn())
  {
    // Enumerate friends
    HRESULT result = XOnlineFriendsEnumerate(0, NULL, &_taskFriendList);
    if (FAILED(result))
    {
      RptF("XOnlineFriendsEnumerate failed with error 0x%x", result);
      _taskFriendList = NULL;
    }
    // Create the mute-list startup task
    result = XOnlineMutelistStartup(NULL, &_taskMuteList);
    if (FAILED(result))
    {
      RptF("XOnlineMutelistStartup failed with error 0x%x", result);
      _taskMuteList = NULL;
    }
  }
#endif

  UpdateButtons();
}

DisplayXPlayers::~DisplayXPlayers()
{
  // Friends are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
  if (_taskFriendList)
  {
    HRESULT result = XOnlineFriendsEnumerateFinish(_taskFriendList);
    Assert(SUCCEEDED(result));
    do
    {
      result = XOnlineTaskContinue(_taskFriendList);
    } while (result == XONLINETASK_S_RUNNING);
    XOnlineTaskClose(_taskFriendList);
  }
  if (_taskMuteList)
  {
    HRESULT result;
    do
    {
      result = XOnlineTaskContinue(_taskMuteList);
    } while (result == XONLINETASK_S_RUNNING);
    XOnlineTaskClose(_taskMuteList);
    if (_muteListNeedUpdate) GetNetworkManager().UpdateMuteList();
  }
  if (_taskFeedback)
  {
    HRESULT result;
    do
    {
      result = XOnlineTaskContinue(_taskFeedback);
    } while (result == XONLINETASK_S_RUNNING);
    XOnlineTaskClose(_taskFeedback);
  }
#endif

#ifdef _WIN32
  UnlockController();
#endif
}

Control *DisplayXPlayers::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_XPLAYERS_LIST:
    _players = new CPlayers(this, idc, cls);
    return _players;
  case IDC_XPLAYERS_STATUS:
    {
      Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
      _status = GetStructuredText(ctrl);
      return ctrl;
    }
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplayXPlayers::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    // action
#if _XBOX_SECURE && _ENABLE_MP
    {
      if (!_players) return;
      int sel = _players->GetCurSel();
      if (sel < 0) return;
      const CPlayersItem &player = _players->GetPlayer(sel);
      if (player.xuid == INVALID_XUID) return;
      // don't perform actions on yourself
      if (player.dpnid == GetNetworkManager().GetPlayer()) return;
      // create dialog
      _selected = player.xuid;
      _nameSelected = player.name;
      const XONLINE_FRIEND *f = _players->GetFriend(_selected);
      RString resource;
      if (!f)
        resource = "RscDisplayPlayersOptionsNoFriend";
      else if (f->dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST)
        resource = "RscDisplayPlayersOptionsWaitingForFriend";
      else
        resource = "RscDisplayPlayersOptionsFriend";

      CreateChild(new DisplayPlayersOptions(this, resource, player.name, GetNetworkManager().IsPlayerInMuteList(_selected)));
    }
#endif
    break;

#if _PROFILE || _DEBUG
  case IDC_XPLAYERS_DISABLE_VOICE:
    GetNetworkManager().DisableVoice(true);
    break;
  case IDC_XPLAYERS_ENABLE_VOICE:
    GetNetworkManager().DisableVoice(false);
    break;
#endif

  case IDC_XPLAYERS_KICKOFF:
    // kick off
    {
      if (!_players) break; // avoid sound
      int sel = _players->GetCurSel();
      if (sel < 0) break; // avoid sound
      const CPlayersItem &player = _players->GetPlayer(sel);
      // don't kick off yourself
      if (player.dpnid == GetNetworkManager().GetPlayer()) break; // avoid sound
      // don't kick off disconnected players
      if (player.dpnid == 0) break; // avoid sound

      if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
      {
        MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
        MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
        CreateMsgBox(
          MB_BUTTON_OK | MB_BUTTON_CANCEL,
          Format(LocalizeString(IDS_MSG_CONFIRM_KICK_OFF), (const char *)player.name),
          IDD_MSG_KICK_OFF, false, &buttonOK, &buttonCancel);
      }
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayXPlayers::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_XPLAYERS_LIST)
  {
    UpdateButtons();
    return; 
  }
  Display::OnLBSelChanged(ctrl, curSel);
}

void DisplayXPlayers::OnSimulate(EntityAI *vehicle)
{
  if (_autoCancel && (*_autoCancel)())
  {
    Exit(IDC_CANCEL);
    return;
  }

  Display::OnSimulate(vehicle);

  // Friends are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
  if (_taskMuteList)
  {
    HRESULT result = XOnlineTaskContinue(_taskMuteList);
    if (FAILED(result))
    {
      // If the mutelist startup task failed, then one of the mutelist operations
      // failed.  Since there's not much we can do about it, we'll just ignore it.
      RptF("Mutelist startup task failed with error 0x%x", result);
    }
    else if (result != XONLINETASK_S_RUNNING && _muteListNeedUpdate)
    {
      GetNetworkManager().UpdateMuteList();
      _muteListNeedUpdate = false;
    }
  }
  if (_taskFriendList)
  {
    HRESULT result = XOnlineTaskContinue(_taskFriendList);
    if (FAILED(result))
    {
      RptF("Friends enumeration: XOnlineTaskContinue failed with error 0x%x", result);
      XOnlineTaskClose(_taskFriendList);
      _taskFriendList = NULL;

      // Attempt to restart enumeration
      result = XOnlineFriendsEnumerate(0, NULL, &_taskFriendList);
      if (FAILED(result))
      {
        RptF("XOnlineFriendsEnumerate failed with error 0x%x", result);
        _taskFriendList = NULL;
      }
    }
    else if (result == XONLINETASK_S_RESULTS_AVAIL)
    {
      // update friends list
      if (_players)
        _players->_friendsCount = XOnlineFriendsGetLatest(0, MAX_FRIENDS, _players->_friends);
    }
  }
  if (_taskFeedback)
  {
    HRESULT result = XOnlineTaskContinue(_taskFeedback);
    if (FAILED(result))
    {
      RptF("Feedback: XOnlineTaskContinue failed with error 0x%x", result);
      XOnlineTaskClose(_taskFeedback);
      _taskFeedback = NULL;
    }
    else if (result != XONLINETASK_S_RUNNING)
    {
      // done
      XOnlineTaskClose(_taskFeedback);
      _taskFeedback = NULL;
    }
  }

  if (_status)
  {
    RString text;
    int sel = _players->GetCurSel();
    if (sel >= 0)
    {
      const CPlayersItem &player = _players->GetPlayer(sel);
      if (player.dpnid == GetNetworkManager().GetPlayer())
      {
        text = LocalizeString(IDS_DISP_XBOX_MP_HOST_NAME);
      }
      else
      {
        const XONLINE_FRIEND *f = NULL;
        if (player.xuid.qwUserID != 0) f = _players->GetFriend(player.xuid);
        
        if (!f)
        {
        }
        else if (f->dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST)
        {
          text = LocalizeString(IDS_DISP_XBOX_FRIENDS_REQUEST_RECEIVED);
        }
        else if (f->dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST)
        {
          text = LocalizeString(IDS_DISP_XBOX_FRIENDS_REQUEST_SENT);
        }
        else
        {
          text = LocalizeString(IDS_XBOX_LIVE_FRIEND);
        }

        const PlayerIdentity *identity = GetNetworkManager().FindIdentity(player.dpnid);
        if (player.xuid.qwUserID == 0)
        {
        }
        else if (GetNetworkManager().IsPlayerInMuteList(player.xuid))
        {
          if (text.GetLength() > 0) text = text + RString(", ");
          text = text + LocalizeString(IDS_DISP_XBOX_FRIENDS_VOICE_MUTED);
        }
        else if (identity && identity->voiceOn)
        {
          if (text.GetLength() > 0) text = text + RString(", ");
          text = text + LocalizeString(IDS_DISP_XBOX_FRIENDS_VOICE_ON);
        }
      }
    }

    _status->SetText(text);
  }
#endif
}

void DisplayXPlayers::UpdateButtons()
{
  IControl *ctrl = GetCtrl(IDC_OK);
  if (ctrl)
  {
#if _XBOX_SECURE && _ENABLE_MP
    bool enable = false;
    if (_players)
    {
      int sel = _players->GetCurSel();
      // some player selected
      if (sel >= 0)
      {
        const CPlayersItem &player = _players->GetPlayer(sel);
        // enable for valid players different from myself
        enable = player.xuid != INVALID_XUID && player.dpnid != GetNetworkManager().GetPlayer();
      }
    }
    ctrl->EnableCtrl(enable);
#else
    ctrl->EnableCtrl(false);
#endif
  }
  ctrl = GetCtrl(IDC_XPLAYERS_KICKOFF);
  if (ctrl)
  {
    bool enable = false;
    // check the competence
    if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
    {
      if (_players)
      {
        int sel = _players->GetCurSel();
        // some player selected
        if (sel >= 0)
        {
          const CPlayersItem &player = _players->GetPlayer(sel);
          // enable for valid players different from myself
          enable = player.dpnid != 0 && player.dpnid != GetNetworkManager().GetPlayer();
        }
      }
    }
    ctrl->EnableCtrl(enable);
  }
}

// Friends are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
void DisplayXPlayers::SendFriendRequest(XONLINE_MSG_HANDLE msg)
{
  XONLINETASK_HANDLE task = NULL;
  HRESULT result = XOnlineFriendsRequestEx(0, _selected, msg, NULL, &task);
  if (FAILED(result))
  {
    RptF("XOnlineFriendsRequestEx failed with error 0x%x", result);
    return;
  }
  Assert(task);
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_NETWORK_SENDING));
  ProgressAdd(100);
  DWORD last = 0;
  do
  {
    result = XOnlineTaskContinue(task);
    DWORD cur;
    HRESULT result = XOnlineMessageSendGetProgress(task, &cur, NULL, NULL);
    if (SUCCEEDED(result))
    {
      ProgressAdvance(cur - last);
      last = cur;
    }
    ProgressRefresh();
  }
  while (result == XONLINETASK_S_RUNNING);
  ProgressFinish(p);
  XOnlineTaskClose(task);
  if (FAILED(result))
  {
    RptF("XOnlineFriendsRequestEx pumping failed with error 0x%x", result);
  }
}
#endif

void DisplayXPlayers::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
// Friends are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
  case IDD_MSG_CANCEL_REQUEST:
    if (exit == IDC_OK && _players)
    {
      const XONLINE_FRIEND *f = _players->GetFriend(_selected);
      if (!f) break;
      HRESULT result = XOnlineFriendsRemove(0, f);
      if (FAILED(result))
      {
        RptF("XOnlineFriendsRemove of player %s failed with error 0x%x", f->szGamertag, result);
      }
    }
    break;
#endif
  case IDD_MSG_KICK_OFF:
    if (exit == IDC_OK && _players)
    {
      int sel = _players->GetCurSel();
      if (sel < 0) break;
      const CPlayersItem &player = _players->GetPlayer(sel);
      // don't kick off yourself
      if (player.dpnid == GetNetworkManager().GetPlayer()) break;

      if (GetNetworkManager().IsServer())
        GetNetworkManager().KickOff(player.dpnid, KORKick);
      else if (GetNetworkManager().IsGameMaster())
        GetNetworkManager().SendKick(player.dpnid);
    }
    break;
  case IDD_XPLAYERS_ACTIONS:
#if _XBOX_SECURE && _ENABLE_MP
    {
      switch (exit)
      {
// Friends are handled by Xbox Guide on Xbox 360
# if _XBOX_VER < 200
      case IDC_XPLAYERS_ASK:
        SendFriendRequest(NULL);
        break;
      case IDC_XPLAYERS_ASK_VOICE:
        Display::OnChildDestroyed(idd, exit);
        {
          const CPlayersItem *pl = _players->FindPlayer(_selected);
          if (!pl) break;
          CreateChild(new DisplaySendVoiceMail(this, LocalizeString(IDS_XBOX_VOICE_FRIEND_REQUEST), pl->name));
        }
        return;
      case IDC_XPLAYERS_CANCEL:
        Display::OnChildDestroyed(idd, exit);
        if (_players)
        {
          const XONLINE_FRIEND *f = _players->GetFriend(_selected);
          if (!f) break;
          MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
          MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
          CreateMsgBox(
            MB_BUTTON_OK | MB_BUTTON_CANCEL,
            Format(LocalizeString(IDS_MSG_CONFIRM_CANCEL_REQUEST), f->szGamertag),
            IDD_MSG_CANCEL_REQUEST, false, &buttonOK, &buttonCancel);
        }
        return;
# endif // _XBOX_VER < 200
      case IDC_XPLAYERS_MUTE:
        {
          DisplayPlayersOptions *disp = static_cast<DisplayPlayersOptions *>(_child.GetRef());
          Assert(disp);
          if (disp->IsMuted())
          {
# if _XBOX_VER >= 200
            // Handled by Xbox Guide on Xbox 360
# else
            HRESULT result = XOnlineMutelistRemove(0, _selected);
            if (FAILED(result))
            {
              RptF("XOnlineMutelistRemove failed with error 0x%x", result);
            }
# endif
          }
          else
          {
# if _XBOX_VER >= 200
            // Handled by Xbox Guide on Xbox 360
# else
            HRESULT result = XOnlineMutelistAdd(0, _selected);
            if (FAILED(result))
            {
              RptF("XOnlineMutelistAdd failed with error 0x%x", result);
            }
# endif
          }
          _muteListNeedUpdate = true;
        }
        break;
// Friends are handled by Xbox Guide on Xbox 360
# if _XBOX_VER < 200
      case IDC_XPLAYERS_FEEDBACK:
        Display::OnChildDestroyed(idd, exit);
        CreateChild(new DisplayFeedback(this, _nameSelected, "RscDisplayPlayersFeedBackSelection"));
        return;
# endif // _XBOX_VER < 200
      }
    }
#endif
    break;
  // Friends, feedback and invitations are handled by Xbox Guide on Xbox 360
#if _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
  case IDD_SEND_VOICE_MAIL:
    if (exit == IDC_OK)
    {
      DisplaySendVoiceMail *disp = static_cast<DisplaySendVoiceMail *>(_child.GetRef());
      if (disp)
      {
        const char *data = disp->GetData();
        int size = disp->GetSize();
        int length = disp->GetDuration();
        XONLINE_MSG_HANDLE msg = NULL;
        if (size > 0 && length > 0)
        {
          int totalSize = sizeof(int) + sizeof(short);
          HRESULT result = XOnlineMessageCreate
          (
            XONLINE_MSG_TYPE_GAME_INVITE, 4, totalSize,
            NULL, XONLINE_MSG_FLAG_HAS_VOICE, 0, &msg
          );
          if (SUCCEEDED(result)) AttachVoiceToMessage(msg, data, size, length);
        }
        SendFriendRequest(msg);
        if (msg) XOnlineMessageDestroy(msg);
      }
    }
    break;
  case IDD_XPLAYERS_FEEDBACK:
    {
      XONLINE_FEEDBACK_TYPE type;
      switch (exit)
      {
      case IDC_FEEDBACK_GREAT_SESSION:
        type = XONLINE_FEEDBACK_POS_SESSION;
        break;
      case IDC_FEEDBACK_GOOD_ATTITUDE:
        type = XONLINE_FEEDBACK_POS_ATTITUDE;
        break;
      case IDC_FEEDBACK_BAD_NAME:
        type = XONLINE_FEEDBACK_NEG_NICKNAME;
        break;
      case IDC_FEEDBACK_CURSING:
        type = XONLINE_FEEDBACK_NEG_LEWDNESS;
        break;
      case IDC_FEEDBACK_SCREAM:
        type = XONLINE_FEEDBACK_NEG_SCREAMING;
        break;
      case IDC_FEEDBACK_CHEAT:
        type = XONLINE_FEEDBACK_NEG_GAMEPLAY;
        break;
      case IDC_FEEDBACK_THREAT:
        type = XONLINE_FEEDBACK_NEG_HARASSMENT;
        break;
      default:
        Fail("Unknown feedback");
      case IDC_CANCEL:
        Display::OnChildDestroyed(idd, exit);
        return;
      }
      // finish previous feedback
      if (_taskFeedback)
      {
        HRESULT result;
        do
        {
          result = XOnlineTaskContinue(_taskFeedback);
        } while (result == XONLINETASK_S_RUNNING);
        XOnlineTaskClose(_taskFeedback);
        _taskFeedback = NULL;
      }
      WCHAR buffer[16];
      ToWideChar(buffer, 16, _nameSelected);
      XONLINE_FEEDBACK_PARAMS params;
      params.lpStringParam = buffer;

      HRESULT result = XOnlineFeedbackSend(0, _selected, type, &params, NULL, &_taskFeedback);
      if (FAILED(result))
      {
        RptF("XOnlineFeedbackSend failed with error 0x%x", result);
      }
    }
#endif // _XBOX_SECURE && _XBOX_VER < 200 && _ENABLE_MP
    break;
  }
  Display::OnChildDestroyed(idd, exit);
}

#if _ENABLE_UNSIGNED_MISSIONS
RString CreateMPMissionBank(RString filename, RString island);
#endif

class MultiplayerSetupMessage : public ControlsContainer
{
protected:
  Ref<CStructuredText> _message;
  Ref<CProgressBar> _progress;
  Ref<CAnimatedTexture> _time;

public:
  MultiplayerSetupMessage();
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void SetMessage(RString message);
  void SetProgress(DWORD time, int current, int total);
};

MultiplayerSetupMessage::MultiplayerSetupMessage()
: ControlsContainer(NULL)
{
  Load("RscMPSetupMessage");

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_CANCEL));
  if (text)
  {
    if (GetNetworkManager().IsServer() || GetNetworkManager().IsGameMaster())
      text->SetText(LocalizeString(IDS_DISP_BACK));
    else
      text->SetText(LocalizeString(IDS_DISP_XBOX_HINT_MP_DISCONNECT));
  }
}

Control *MultiplayerSetupMessage::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = ControlsContainer::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_MPSETUP_MSG_MESSAGE:
    _message = GetStructuredText(ctrl);
    break;
  case IDC_MPSETUP_MSG_PROGRESS:
    if (type == CT_PROGRESS)
    {
      _progress = static_cast<CProgressBar *>(ctrl);
      _progress->ShowCtrl(false);
    }
    break;
  case IDC_MPSETUP_MSG_TIME:
    if (type == CT_ANIMATED_TEXTURE)
    {
      _time = static_cast<CAnimatedTexture *>(ctrl);
    }
    break;
  }
  return ctrl;
}

void MultiplayerSetupMessage::SetMessage(RString message)
{
  if (_message) _message->SetStructuredText(message);
}

void MultiplayerSetupMessage::SetProgress(DWORD time, int current, int total)
{
  if (_progress)
  {
    if (total == 0)
    {
      _progress->ShowCtrl(false);
    }
    else
    {
      _progress->ShowCtrl(true);
      _progress->SetRange(0, total);
      _progress->SetPos(current);
    }
  }
  if (_time)
  {
    float factor = fastFmod(time * (1.0f / 3000), 1);
    _time->SetPhase(factor);
  }
}

// Server display

void LoadMissions(bool userMissions, AutoArray<MPMissionInfo> &missionList, bool campaignCheat=false);

DisplayServer::DisplayServer(ControlsContainer *parent)
: Display(parent), MultiplayerLauncher()
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  // if the storage device was changed sooner, we handle the change, so that OnStorageDeviceChanged was not called
  GSaveSystem.StorageChangeHandled();
#endif
  _enableSimulation = false;
  Load("RscDisplayServer");

  ParamEntryVal cls = Pars >> "RscDisplayServer";
  _colorEditor = GetPackedColor(cls >> "colorEditor");
  _colorWizard = GetPackedColor(cls >> "colorWizard");
  _colorMission = GetPackedColor(cls >> "colorMission");
  _colorMissionEditor = GetPackedColor(cls >> "colorMissionEditor");
  _colorMissionWizard = GetPackedColor(cls >> "colorMissionWizard");

  LoadMissions(true, _missionList);
  UpdateMissions();

  _difficulty = Glob.config.diffDefault;
  _difficulties->SetCurSel(Glob.config.diffDefault);
  LoadParams();
#if !_ENABLE_EDITOR
  if (GetCtrl(IDC_SERVER_EDITOR)) GetCtrl(IDC_SERVER_EDITOR)->ShowCtrl(false);
#endif
}

Control *DisplayServer::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
    case IDC_SERVER_ISLAND:
      {
        _islands = GetListBoxContainer(ctrl);
        if (_islands)
        {
          int sel = 0;
          int m = (Pars >> "CfgWorldList").GetEntryCount();
          for (int j=0; j<m; j++)
          {
            ParamEntryVal entry = (Pars >> "CfgWorldList").GetEntry(j);
            if (!entry.IsClass()) continue;
            RString name = entry.GetName();

  #if _FORCE_DEMO_ISLAND
            RString demo = Pars >> "CfgWorlds" >> "demoWorld";
            if (stricmp(name, demo) != 0) continue;
  #endif

            // RString name = (Pars>>"CfgWorlds">>"worlds")[j];

            // ADDED - check if wrp file exists
            RString fullname = GetWorldName(name);
            if (!QFBankQueryFunctions::FileExists(fullname)) continue;
        
            int index = _islands->AddString
            (
              Pars >> "CfgWorlds" >> name >> "description"
            );
            _islands->SetData(index, name);
            if (stricmp(name, Glob.header.worldname) == 0)
              sel = index;
            RString textureName = Pars >> "CfgWorlds" >> name >> "icon";
            RString fullName = FindPicture(textureName);
            if (fullName.GetLength() > 0)
            fullName.Lower();
            Ref<Texture> texture = GlobLoadTextureUI(fullName);
            _islands->SetTexture(index, texture);
          }
          _islands->SetCurSel(sel);
        }
        return ctrl;
      }
    case IDC_SERVER_MISSION:
      {
        _missions = GetListBoxContainer(ctrl);
        return ctrl;
      }
    case IDC_SERVER_DIFF:
      {
        _difficulties = GetListBoxContainer(ctrl);
        for(int i=0; i<Glob.config.diffSettings.Size();i++)
          _difficulties->AddString(Glob.config.diffSettings[i].displayName);
        return ctrl;
      }
    case IDC_SERVER_PRIVATE_SLOTS:
      {
      _privateSlots = GetSliderContainer(ctrl);
      if(_privateSlots)
      {
      #if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
        _privateSlots->SetRange(0 ,MAX_PLAYERS);
        _privateSlots->SetThumbPos(0);
      #else
        if (ctrl)
          ctrl->ShowCtrl(false);
      #endif
      }
        return ctrl;
      }
    case IDC_SERVER_MAXIMUM_SLOTS:
      {
      _maximumSlots = GetSliderContainer(ctrl);
      if(_maximumSlots)
      {
      #if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
        _maximumSlots->SetRange(0 ,MAX_PLAYERS);
        _maximumSlots->SetThumbPos(MAX_PLAYERS);
      #else
        if (ctrl)
          ctrl->ShowCtrl(false);
      #endif  
      }
        return ctrl;
      }
    case IDC_SERVER_MI_OVERVIEW:
      _overview = GetHTMLContainer(ctrl);
      ctrl->EnableCtrl(false);
      break;
  }
  return ctrl;
}


bool DisplayServer::SetMission(bool editor)
{
  if (!_islands) return false;  
  if (!_missions) return false; 

  CurrentMission = "";
  Glob.config.difficulty = Glob.config.diffDefault;

  int index = _islands->GetCurSel();
  if (index < 0) return false;
  RString world = _islands->GetData(index);

  index = _missions->GetCurSel();
  if (index < 0)
  {
    if (editor)
    {
OnEditor:
      SetBaseDirectory(true, RString());
      ::CreateDirectory(GetBaseDirectory() + RString("MPMissions"),NULL);
      ::SetMission(world, "", MPMissionsDir);
      return true;
    }
    else
      return false;
  }
  RString mission = _missions->GetData(index);
  int missionIndex = _missions->GetValue(index);
  int value = missionIndex < 0 ? missionIndex : _missionList[missionIndex].placement;

  switch (value)
  {
  case MPPbo:
    if (editor) goto OnEditor;
    else
    {
      SetBaseDirectory(false, "");
      GetNetworkManager().CreateMission(mission, world);
      ::SetMission(world, "__cur_mp", MPMissionsDir);
      Glob.header.filenameReal = mission;
    }
    break;
  case MPDirectory:
    if (editor) goto OnEditor;
    else
    {
      SetBaseDirectory(false, "");
      GetNetworkManager().CreateMission("", "");
      ::SetMission(world, mission, MPMissionsDir);
    }
    break;
  case MPOldEditor:
  case MPNewEditor: // _EDITOR2
    {
      SetBaseDirectory(true, RString());
      ::CreateDirectory(GetBaseDirectory() + RString("MPMissions"),NULL);
      if (!editor) GetNetworkManager().CreateMission("", "");
      ::SetMission(world, mission, MPMissionsDir);
    }
    break;
#if _ENABLE_MISSION_CONFIG
  case MPInAddon: // mission in addon
    {
      SetBaseDirectory(false, RString());
      SelectMission(Pars >> "CfgMissions" >> "MPMissions" >> mission);
    }
    break;
#endif
  default:
    if (editor) goto OnEditor;
    break;
  }
  return true;
}

void DisplayServer::ReportCorrupted(const MPMissionInfo &info)
{
  // save is damaged
  RString message = Format(LocalizeString(IDS_LOAD_FAILED), cc_cast(info.displayName.GetLocalizedValue()));
  MsgBoxButton buttonOK(LocalizeString(IDS_DISP_CONTINUE), INPUT_DEVICE_XINPUT + XBOX_A);
  CreateMsgBox(MB_BUTTON_OK, message, -1, false, &buttonOK);
}

void DisplayServer::OnButtonClicked(int idc)
{
#if _ENABLE_MAIN_MENU_TABS
  // support for main menu tabs
  if (idc >= IDC_MAIN_TAB_LOGIN && idc != IDC_MAIN_TAB_MULTIPLAYER)
    Exit(idc);
#endif
  switch (idc)
  {
    case IDC_SERVER_EDITOR:
      {
        if (!_missions) break;
        int index = _missions->GetCurSel();
        if (index < 0) break;
#if _ENABLE_EDITOR || _ENABLE_WIZARD || (_ENABLE_EDITOR2 && _ENABLE_EDITOR2_MP)
        int missionIndex = _missions->GetValue(index);
        int type = missionIndex < 0 ? missionIndex : _missionList[missionIndex].placement;
#endif
#if _ENABLE_EDITOR
        if (type == MPOldEditor)
        {
          Fail("SQM mission editing no longer supported");
          //CreateChild(new DisplayArcadeMap(this, true));
          break;
        }
#endif // #if _ENABLE_EDITOR

#if _ENABLE_EDITOR2 && _ENABLE_EDITOR2_MP
        if (type == MPNewEditor)
        {
          if (!SetMission(true)) break;
  
          GetNetworkManager().SetServerState(NSSEditingMission);
          
          GLOB_WORLD->SwitchLandscape(Glob.header.worldname);

          Display *CreateMissionEditor(ControlsContainer *parent, bool multiplayer);
          this->CreateChild(CreateMissionEditor(this,true));
          break;
        }
#endif // #if _ENABLE_EDITOR2
      }
      break;

    case IDC_SERVER_LOAD:
      {
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
      // note: no break here
    case IDC_OK:
      OnPlayMission(idc);
      break;
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

void DisplayServer::OnPlayMission(int idc)
{
  do 
  {
    if (!_missions) break;
    int index = _missions->GetCurSel();
    if (index < 0) break;
    int missionIndex = _missions->GetValue(index);
    int type = missionIndex < 0 ? missionIndex : _missionList[missionIndex].placement;
  #if _ENABLE_EDITOR2 && _ENABLE_EDITOR2_MP
    if (type == MPEditingNew)
    {
      if (!SetMission(true)) break;

      GetNetworkManager().SetServerState(NSSEditingMission);
      
      GLOB_WORLD->SwitchLandscape(Glob.header.worldname);

      Display *CreateMissionEditor(ControlsContainer *parent, bool multiplayer);
      this->CreateChild(CreateMissionEditor(this,true));
      break;
    }
  #endif // #if _ENABLE_EDITOR2 && _ENABLE_EDITOR2_MP
  #if _ENABLE_EDITOR
    if (type == MPEditingOld)
    {
//      Fail("SQM mission editing no longer supported");
  //    break;
    }
  #endif // #if _ENABLE_EDITOR

    if (!SetMission(false)) break;

    // load template
    if (GWorld->UI()) GWorld->UI()->Init();

    // check Ids after SwitchLandscape
    if (!ParseMission(true, true)) break;

    Glob.config.difficulty = _difficulty;
    bool noCopy = false;
    RString owner;
  #if _ENABLE_MISSION_CONFIG
    if (type == MPInAddon)
    {
      // find the owner of the mission
      noCopy = true;
      RString mission = _missions->GetData(index);
      owner = (Pars >> "CfgMissions" >> "MPMissions" >> mission).GetOwnerName();
    }
  #endif
    // for mission in addon, avoid __cur_mp bank
    RString displayName;
    if (type == MPOldEditor) displayName = _missions->GetText(index);
    if (type == MPNewEditor) displayName = _missions->GetText(index);

#define USE_PROGRESS_ML 1
#if USE_PROGRESS_ML
    if (!baseML::_progressML) baseML::_progressML = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeNetware);
#endif
    GetNetworkManager().InitMission(_difficulty, displayName, noCopy, owner, false);
    GetNetworkManager().SetServerState(NSSAssigningPlayers);
  #if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    if(_maximumSlots && _privateSlots)
      GetNetworkManager().UpdateMaxPlayers(_maximumSlots->GetThumbPos(), _privateSlots->GetThumbPos());
    else
      GetNetworkManager().UpdateMaxPlayers(INT_MAX, 1);  //1 private slot because player on host is created in private slot on XBOX
  #else
    GetNetworkManager().UpdateMaxPlayers(INT_MAX, 0);
  #endif
    GetNetworkManager().SetServerState(NSSSendingMission);
  } while (false);
}

void DisplayServer::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_SERVER_ISLAND:
    UpdateMissions();
    break;
  case IDC_SERVER_MISSION:

    OnMissionChanged(curSel);
    break;
  case IDC_SERVER_DIFF:
    _difficulty = _difficulties->GetCurSel();
    SaveParams();
    break;
  }
  Display::OnLBSelChanged(ctrl, curSel);
}

void DisplayServer::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_SERVER_MISSION && curSel >= 0)
    OnButtonClicked(IDC_SERVER_LOAD);
  else
    Display::OnLBDblClick(idc, curSel);
}

void DisplayServer::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_MSG_DELETEMISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      // delete mission
      if (!_missions) return;
      int sel = _missions->GetCurSel();
      if (sel < 0) return;
#if defined _XBOX && _XBOX_VER >= 200
      if (!GSaveSystem.DeleteSave(_missions->GetData(sel)))
      {
        return;
      }
#elif defined _XBOX
      SaveHeaderAttributes filter;
      filter.Add("type", FindEnumName(STMPMission));
      filter.Add("player", Glob.header.GetPlayerName());
      const MPMissionInfo &info = _missionList[_missions->GetValue(sel)];
      filter.Add("mission", info.displayName._id);
      DeleteSave(filter);
#else
      RString filename = _missions->GetData(sel);
      Verify(QIFileFunctions::Unlink(filename));
#endif
      LoadMissions(true, _missionList);
      UpdateMissions();
    }
    break;
  case IDD_ARCADE_MAP:
  case IDD_MISSION_EDITOR:
      Display::OnChildDestroyed(idd, exit);
      GetNetworkManager().SetServerState(NSSSelectingMission);

      LoadMissions(true, _missionList);
      UpdateMissions(Glob.header.filename, 2);
      if (exit == IDC_OK)
      {
        OnButtonClicked(IDC_OK);
      }
      break;
    case IDD_SERVER_SIDE:
      Display::OnChildDestroyed(idd, exit);
      GetNetworkManager().SetServerState(NSSSelectingMission);
      break;
    case IDD_MP_SETUP:
#if !(_GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200))
     // LUKE - why is it here? commented for XBOX version...
      GetNetworkManager().UpdateMaxPlayers(INT_MAX, 0);
#endif
      OnMissionChanged(_missions->GetCurSel());
      Display::OnChildDestroyed(idd, exit);
#if _ENABLE_MAIN_MENU_TABS
      // support for main menu tabs
      if (exit >= IDC_MAIN_TAB_LOGIN)
        Exit(exit);
#endif
      break;
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    case IDD_MSG_ACCEPT_INVITATION:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        Exit(IDC_CANCEL);
      }
      else
      {
        // Invitation rejected
        GSaveSystem.InvitationHandled();
      }
      break;    
#endif
    case IDD_MSG_RESTART_MISSION:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        OnPlayMission(IDC_SERVER_LOAD);
      }
      break;
    case IDD_MISSION:
      {
        Display::OnChildDestroyed(idd, exit);
        GetNetworkManager().DestroyAllObjects();
        GetNetworkManager().SetClientState(NCSGameFinished);
        GStats.OnMPMissionEnd();
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
        // Mission finished
        DWORD presence = GetNetworkManager().GetSessionType() == STSystemLink ?
          CONTEXT_PRESENCE_STR_RP_SYSTEMLINK_SETUP :
          CONTEXT_PRESENCE_STR_RP_LIVE_SETUP;
        GSaveSystem.SetContext(X_CONTEXT_PRESENCE, presence);
#endif
        GetNetworkManager().SetServerState(NSSSelectingMission);
        baseML::ResetInfo();
        Display::OnChildDestroyed(idd, exit);
      }
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

void DisplayServer::OnSimulate(EntityAI *vehicle)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    // update slot sliders based on current session state
    const AutoArray<PlayerIdentity> *identities = GetNetworkManager().GetIdentities();

    if(identities && _maximumSlots && _privateSlots)
    {
      int privateCount = 0;
      for(int i=0;i<identities->Size();++i)
      {
        if( (*identities)[i].privateSlot )
          ++privateCount;
      }

      // can't select less than number of the occupied slots
      if(_maximumSlots->GetThumbPos() < identities->Size())    
        _maximumSlots->SetThumbPos(identities->Size());

      // can't select less than number of occupied private slots
      if(_privateSlots->GetThumbPos() < privateCount)    
        _privateSlots->SetThumbPos(privateCount);

      // there can't be more private slots than maximum slots - occupied public slots
      if( _privateSlots->GetThumbPos() > (_maximumSlots->GetThumbPos() - (identities->Size() - privateCount)) )    
        _privateSlots->SetThumbPos(_maximumSlots->GetThumbPos() - (identities->Size() - privateCount));
    }
#endif

  if (!GetNetworkManager().IsServer())
  {
    Exit(IDC_CANCEL);
    return;
  }

  bool simulateDisplay = baseML::OnSimulate();
  // show message when Bot Client has not identity yet, because is not authenticated on Steam
  if (!GetNetworkManager().FindIdentity(GetNetworkManager().GetPlayer())) 
  {
    _messageText = "Authenticating, please wait..."; //LocalizeString(IDS_...)
  }
  else _messageText = RString();

  if (_messageText.GetLength() == 0 && simulateDisplay)
    Display::OnSimulate(vehicle);
}

void DisplayServer::OnDraw(EntityAI *vehicle, float alpha)
{
  /// MP session interrupted
  if (GetNetworkManager().GetClientState() == NCSNone) return;

  if ( !baseML::_progressML )
  {
    Display::OnDraw(vehicle, alpha);
  }

  if (_messageText.GetLength() > 0)
  {
    _msg->SetMessage(_messageText);
    _msg->SetProgress(::GlobalTickCount(), _messageCurrent, _messageTotal);
    _msg->OnDraw(vehicle, alpha);
  }
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

void DisplayServer::OnInvited()
{
  if (_msgBox) return; // confirmation in progress

  if (GSaveSystem.IsInvitationConfirmed())
  {
    Exit(IDC_OK);
  }
  else
  {
    GSaveSystem.ConfirmInvitation();
    // let the user confirm the action because of possible progress lost
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    RString message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION);
    // special type of message box which will not close because of invitation
    _msgBox = new MsgBoxIgnoreInvitation(this, MB_BUTTON_OK | MB_BUTTON_CANCEL, message, IDD_MSG_ACCEPT_INVITATION, false, &buttonOK, &buttonCancel);
  }
}

#endif

/*!
\patch 1.34 Date 12/05/2001 by Jirka
- Added: difficulty flag in multiplayer is saved in user profile (default Cadet) 
*/
void DisplayServer::LoadParams()
{
  _difficulty = Glob.config.diffDefault;

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return;

  ConstParamEntryPtr entry = cfg.FindEntry("difficultyMP");
  if (entry)
  {
    RString name = *entry;
    int difficulty = Glob.config.diffNames.GetValue(name);
    if (difficulty >= 0) _difficulty = difficulty;
  }
}

void DisplayServer::SaveParams()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return;
  cfg.Add("difficultyMP", Glob.config.diffNames.GetName(_difficulty));
  SaveUserParams(cfg);
}

static int CompareMissions( const CListBoxItem *str0, const CListBoxItem *str1 )
{
  if (str0->value < 0 || str1->value < 0)
  {
    // -1, -2 used for special items
    int diff = str0->value - str1->value;
    if (diff != 0) return diff;
  }
  return stricmp(str0->text, str1->text);
}

/*!
\patch 1.13 Date 08/08/2001 by Jirka
- Improved: Server display design
*/
void DisplayServer::UpdateMissions(RString filename, int value)
{
  if (!_islands) return;  
  if (!_missions) return; 

  int index = _islands->GetCurSel();
  RString island = index < 0 ? "" : _islands->GetData(index);

  if (filename.GetLength() == 0)
  {
    index = _missions->GetCurSel();
    if (index >= 0)
    {
      filename = _missions->GetData(index);
      value = _missions->GetValue(index);
    }
  }

  _missions->ClearStrings();
  if (island.GetLength() == 0) return;

#if defined _XBOX && _XBOX_VER >= 200
if (GSaveSystem.IsStorageAvailable()) 
{
#endif //#if defined _XBOX && _XBOX_VER >= 200

#if _ENABLE_EDITOR && !_DEMO
  index = _missions->AddString(LocalizeString(IDS_MPW_NEW_EDIT));
  _missions->SetValue(index, -2);
  _missions->SetFtColor(index, _colorEditor);
  _missions->SetSelColor(index, _colorEditor);
#endif

#if _ENABLE_EDITOR2 && _ENABLE_EDITOR2_MP
  index = _missions->AddString(LocalizeString(IDS_MPW_NEW_EDIT2));
  _missions->SetValue(index, -3);
  _missions->SetFtColor(index, _colorEditor);
  _missions->SetSelColor(index, _colorEditor);
#endif

#if _ENABLE_WIZARD
  index = _missions->AddString(LocalizeString(IDS_MPW_NEW_WIZ));
  _missions->SetValue(index, -1);
  _missions->SetFtColor(index, _colorWizard);
  _missions->SetSelColor(index, _colorWizard);
#endif

#if defined _XBOX && _XBOX_VER >= 200
} //  if (GSaveSystem.IsStorageAvailable()) 
#endif //#if defined _XBOX && _XBOX_VER >= 200

  for (int i=0; i<_missionList.Size(); i++)
  {
    const MPMissionInfo &info = _missionList[i];
    if (info.world.GetLength() > 0 && stricmp(info.world, island) != 0) continue; // show corrupted missions

    int index = _missions->AddString(info.displayName.GetLocalizedValue());
    _missions->SetValue(index, i);
    _missions->SetData(index, info.mission);
    switch (info.placement)
    {
    case MPOldEditor:
    case MPNewEditor:
      _missions->SetFtColor(index, _colorMissionEditor);
      _missions->SetSelColor(index, _colorMissionEditor);
      break;
    default:
      _missions->SetFtColor(index, _colorMission);
      _missions->SetSelColor(index, _colorMission);
      break;
    }
  }
  QSort(_missions->Data(), _missions->Size(), CompareMissions);

  int sel = 0;
  for (int i=0; i<_missions->GetSize(); i++)
  {
    if (stricmp(_missions->GetData(i), filename) == 0)
    {
      sel = i;
      break;
    }
  }
  _missions->SetCurSel(sel, false);

  OnMissionChanged(_missions->GetCurSel());
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
void DisplayServer::OnStorageDeviceChanged()
{
  GSaveSystem.StorageChangeHandled();
  /// we have to reload all missions because of custom missions
  LoadMissions(true, _missionList);
  UpdateMissions();
}

void DisplayServer::OnSavesChanged()
{
  LoadMissions(true, _missionList);
  UpdateMissions();

  Display::OnSavesChanged();
}

#endif

void DisplayServer::UpdateMissionInfoUI(MPMissionInfo &info)
{
  if (_overview)
  {
    if ( !info.overview.IsEmpty() )
    {
      if (info.placement == MPPbo)
      {
#if _ENABLE_UNSIGNED_MISSIONS
        CreateMPMissionBank(RString("MPMissions\\") + info.mission + RString(".") + info.world, info.world);
#endif
      }
      _overview->Load(info.overview);
    }
    else
    {
      RString description = info.description.GetLocalizedValue();
      if (!description.IsEmpty())
      {
        RString descHTML = Format("<html><body><p>%s</p></body></html>", cc_cast(description));
        // parse the content
        QIStrStream in(cc_cast(descHTML), descHTML.GetLength());
        _overview->Load(in);
      }
    }
  }
}

void DisplayServer::OnMissionChanged(int sel)
{
  _saveType = (SaveGameType)-1;

  if (!_missions) return; 

/*
  RString tooltip;
  if (sel >= 0) tooltip = lbox->GetText(sel);
  lbox->SetTooltip(tooltip);
*/

#ifdef _WIN32
  if (_overview) _overview->Init();
#endif

  if (sel >= 0)
  {
    bool corrupted = false;
    int missionIndex = _missions->GetValue(sel);

    // update buttons
    IControl *ctrlOK = GetCtrl(IDC_OK);
    ITextContainer *textOK = GetTextContainer(ctrlOK);
    IControl *ctrlLoad = GetCtrl(IDC_SERVER_LOAD);
    ITextContainer *textLoad = GetTextContainer(ctrlLoad);

    if (corrupted)
    {
      if (ctrlOK) ctrlOK->ShowCtrl(false);
      if (ctrlLoad) ctrlLoad->ShowCtrl(false);
      return;
    }

    RString dir = RString("Saved\\mpmissions");
    if (missionIndex >= 0)
    {

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

      // update slot sliders based on current session state
      const AutoArray<PlayerIdentity> *identities = GetNetworkManager().GetIdentities();

      // get maximum number of players for this mission
      int maxPlayers = _missionList[missionIndex].maxPlayers;

      // if it's out of range, use default MAX_PLAYERS
      if(maxPlayers < 0 || maxPlayers > MAX_PLAYERS)
        maxPlayers = MAX_PLAYERS;

      // can't kick player just because the mission is for less players 
      // than the number of connected players in the session.
      if (identities && maxPlayers < identities->Size())
        maxPlayers = identities->Size();

      // adjust max. slot slider
      if (_maximumSlots)
      {
        // update slider with number of max. available slots
        _maximumSlots->SetRange(0 ,maxPlayers);
        // call this, to force slider to update text value
        _maximumSlots->SetThumbPos(_maximumSlots->GetThumbPos());
      }

      // adjust private slot slider
      if(_privateSlots)
      {
        // adjust max. number of private slots 
        _privateSlots->SetRange(0, maxPlayers);
        // call this, to force slider to update text value
        _privateSlots->SetThumbPos(_privateSlots->GetThumbPos());
      }      
#endif

      ITextContainer *text;
      text = GetTextContainer(GetCtrl(IDC_SERVER_MI_GAMETYPE));
      if (text) {
        text->SetText(_missionList[missionIndex].gameType);
      }
      text = GetTextContainer(GetCtrl(IDC_SERVER_MI_MAX_PLAYERS));
      if (text) {
        RString maxPl = _missionList[missionIndex].maxPlayers > 0 ? Format("%d",_missionList[missionIndex].maxPlayers) : RString("Unknown");
        text->SetText(maxPl);
      }

      RString mission = _missions->GetData(sel);
      RString world = RString(".") + _missionList[missionIndex].world;
      int placement = _missionList[missionIndex].placement;
      switch (placement)
      {
      case MPPbo:
      case MPDirectory:
      case MPNewEditor:
      case MPDownloaded:
        dir = RString("Saved\\mpmissions\\") + mission + world;
        break;
      case MPInAddon:
        {
          dir = Pars >> "CfgMissions" >> "MPMissions" >> mission >> "directory";
          dir = "Saved\\" + dir;
          break;
        }
      case MPOldEditor: //UserMission
        dir = RString("UserSaved\\mpmissions\\") + mission + world;
        break;
      default:
        dir = RString("Saved\\mpmissions\\") + mission + world;
      }
      // populate the Mission Information UI (overview, max players, game type, respawn type)
      UpdateMissionInfoUI(_missionList[missionIndex]);
    }
    { // TerminateBy '\\'
      int dirSiz = dir.GetLength();
      if (dirSiz && dir[dirSiz-1]!='\\') dir = dir + RString("\\");
    }

    SaveGameType SelectSaveGame(RString dir);
    #ifdef _XBOX
        XSaveGame save = GetMissionSave(dir);
        if (save) _saveType = SelectSaveGame(SAVE_ROOT); // Errors already handled
    #else
        dir = GetUserDirectory() + dir;
        if (dir.GetLength() > 0) _saveType = SelectSaveGame(dir);
    #endif

    if (_saveType >= 0)
    {
#if !POSIX_FILES_COMMON
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
#endif
    }
    else
    {
      if (ctrlOK)
      {
        ctrlOK->ShowCtrl(true);
        if (textOK) textOK->SetText(LocalizeString(IDS_SINGLE_PLAY));
      }
      if (ctrlLoad) 
        ctrlLoad->ShowCtrl(false);
    }
  }
}



void ScriptedMission::InitNoCenters( VehicleInitMessages &inits, GameMode mode ) const
{
  if (!loaded) return;

  // new style missions
  FilePreprocessor preproc;
  QOStrStream processed;
  if (!preproc.Process(&processed, filename)) return;
  RString script(processed.str(),processed.pcount());

  // launch mission.sqf
  GameVarSpace local(false);
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&local);
  gstate->Execute(script, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
  gstate->EndContext(gshandle);
}
void ScriptedMission::ActivateAddons( RString worldname )
{

}
void ScriptedMission::InitGeneral(GameMode mode)
{
  GWorld->SetMode(mode);
  GWorld->InitGeneral(intel);
}

ScriptedMission::ScriptedMission():loaded(false){}

bool ScriptedMission::Load( RString name, RString cutscene, GameDataNamespace &globals, GameVarSpace &local )
{
  if(strcmpi(cutscene,"Mission")!=0) return false; // scripted mission has no intro / outro

  filename = name;
  loaded = true;

  return true;
}


RString FastSearch(QIStream &file, const char *searchStr, int searchLen);

LocalizedString ClientSideLocalizedString(RString value, RString mission);

/*!
\patch 5106 Date 12/20/2006 by Jirka
- Fixed: Missions in addons - wrong mission name was sometimes shown
*/

void AddMissionInDirectory(const char *name, RString dir, MissionPlacement placement, AutoArray<MPMissionInfo> &missionList, ConstParamEntryPtr missionEntry=NULL)
{
  const char *searchStr = "briefingName";
  int searchLen = strlen(searchStr);
  const char *searchDesc = "briefingDescription";
  int searchDescLen = strlen(searchDesc) ;

  // parse directory name
  LocalizedString displayName;
  if (placement == MPOldEditor)
    displayName = LocalizedString(LocalizedString::PlainText,DecodeFileName(name));
  else
    displayName = LocalizedString(LocalizedString::PlainText,name);

  const char *ext = strrchr(name, '.');
  if (!ext) return;
  RString mission = RString(name, ext - name);
  if(mission.GetLength()==0)
    return;
  RString world = RString(ext + 1);

  RString gameType("Unknown");
  int minPlayers = -1;
  int maxPlayers = -1;
  int perClient = -1;
  int round = -1;
  RString overview;
  LocalizedString description;

  LoadStringtable("Mission", dir + RString("\\stringtable.csv"), 2, true);

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile f;
  {
    f.Parse(dir + RString("\\description.ext"), NULL, NULL, &globals);

    ConstParamEntryPtr header = f.FindEntry("Header");
    if (header)
    {
      ConstParamEntryPtr entry = header->FindEntry("gameType");
      if (entry) gameType = *entry;
      entry = header->FindEntry("minPlayers");
      if (entry) minPlayers = *entry;
      entry = header->FindEntry("maxPlayers");
      if (entry) maxPlayers = *entry;
      entry = header->FindEntry("bandwidthPerClient");
      if (entry) perClient = *entry;
      entry = header->FindEntry("playerCountMultipleOf");
      if (entry) round = *entry;
    }
    
    RString GetOverviewFile(RString dir);
    overview = GetOverviewFile(dir+RString("\\"));
  }

  RString filename = dir + RString("\\mission.sqm");
  if (f.ParseBin(filename, NULL, NULL, &globals))
  {
    ParamEntryVal entry = f >> "Mission" >> "Intel";
    if (entry.FindEntry("briefingName"))
    {
      RString found = entry >> "briefingName";
      if (found.GetLength() > 0) displayName = ClientSideLocalizedString(found,dir);
    }
    if (entry.FindEntry("briefingDescription"))
    {
      RString found = entry >> "briefingDescription";
      if (found.GetLength() > 0) description = ClientSideLocalizedString(found,dir);
  }
  }
  else
  {
    QIFStreamB file;
    file.AutoOpen(filename);
    
    if (!file.fail())
    {
      RString found = FastSearch(file, searchStr, searchLen);
      if (found.GetLength() > 0) displayName = ClientSideLocalizedString(found,dir);
      file.seekg(0, QIOS::beg);
      found = FastSearch(file, searchDesc, searchDescLen);
      if (found.GetLength() > 0) 
      {
        description = ClientSideLocalizedString(found,dir);
    }
  }
  }
#if _ENABLE_EDITOR2
  if (QIFileFunctions::FileExists(dir + RString("\\mission.biedi")) && placement==MPOldEditor)
    placement = MPNewEditor;
#endif

  if (missionEntry && missionEntry->FindEntry("briefingName"))
  {
    RString name = (*missionEntry) >> "briefingName";
    displayName = ClientSideLocalizedString(name, dir);
  }

  int index = missionList.Add();
  MPMissionInfo &info = missionList[index];

  info.placement = placement;
  info.mission = mission;
  info.world = world;
  info.displayName = displayName;
  info.gameType = gameType;
  info.minPlayers = minPlayers;
  info.maxPlayers = maxPlayers;
  info.perClient = perClient;
  info.round = round;
  info.overview = overview;
  info.description = description;
}

#if _ENABLE_UNSIGNED_MISSIONS

static void AddMissionInBank(RString bankName, MissionPlacement placement, AutoArray<MPMissionInfo> &missionList)
{
  const char *searchStr = "briefingName";
  int searchLen = strlen(searchStr);
  const char *searchDesc = "briefingDescription";
  int searchDescLen = strlen(searchDesc);
  const char *ext = strrchr(bankName, '\\');
  const char *name = ext ? ext + 1 :  safe_cast<const char *>(bankName);

  LocalizedString displayName = LocalizedString(LocalizedString::PlainText,name);
  ext = strrchr(name, '.');
  if (!ext) return;
  RString mission = RString(name, ext - name);
  RString world = RString(ext + 1);
  LocalizedString description;
  RString overview;

  RString gameType("Unknown");
  int minPlayers = -1;
  int maxPlayers = -1;
  int perClient = -1;
  int round = -1;
  // create bank (temporary)
  QFBank bank;
  bank.open(bankName);

  LoadStringtable("Mission", bank, "stringtable.csv", 2, true);

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  {
    ParamFile f;
    if (f.Parse(bank, "description.ext", NULL, &globals) == LSOK)
    {
      
      ConstParamEntryPtr header = f.FindEntry("Header");
      if (header)
      {
        ConstParamEntryPtr entry = header->FindEntry("gameType");
        if (entry) gameType = *entry;
        entry = header->FindEntry("minPlayers");
        if (entry) minPlayers = *entry;
        entry = header->FindEntry("maxPlayers");
        if (entry) maxPlayers = *entry;
        entry = header->FindEntry("bandwidthPerClient");
        if (entry) perClient = *entry;
        entry = header->FindEntry("playerCountMultipleOf");
        if (entry) round = *entry;
      }
    }

    if (bank.FileExists("overview.html"))
    {
      overview = RString("mpmissions\\__cur_mp.") + world + RString("\\overview.html");
  }
  }

  {
    ParamFile f;
    if (f.ParseBin(bank, "mission.sqm", NULL, &globals))
    {
      ParamEntryVal entry = f >> "Mission" >> "Intel";
      if (entry.FindEntry("briefingName"))
      {
        RString found = entry >> "briefingName";
        if (found.GetLength() > 0) displayName = ClientSideLocalizedString(found,bankName);
      }
      if (entry.FindEntry("briefingDescription"))
      {
        RString found = entry >> "briefingDescription";
        if (found.GetLength() > 0) description = ClientSideLocalizedString(found,bankName);
    }
    }
    else
    {
      QIFStreamB file;
      file.open(bank, "mission.sqm");

      RString found = FastSearch(file, searchStr, searchLen);
      if (found.GetLength() > 0) displayName = ClientSideLocalizedString(found,bankName);
      file.seekg(0, QIOS::beg);
      found = FastSearch(file, searchDesc, searchDescLen);
      if (found.GetLength() > 0) 
      {
        description = ClientSideLocalizedString(found,bankName);
    }
  }
  }

  int index = missionList.Add();
  MPMissionInfo &info = missionList[index];

  info.placement = placement;
  info.mission = placement == MPPbo ? mission : bankName;
  info.world = world;
  info.displayName = displayName;
  info.gameType = gameType;
  info.minPlayers = minPlayers;
  info.maxPlayers = maxPlayers;
  info.perClient = perClient;
  info.round = round;
  info.overview = overview;
  info.description = description;
}

#endif // _ENABLE_UNSIGNED_MISSIONS


static void AddMission(const char *name, MissionPlacement placement, AutoArray<MPMissionInfo> &missionList)
{
  switch (placement)
  {
#if _ENABLE_UNSIGNED_MISSIONS
  case MPPbo:
    {
      // remove extension .pbo
      const char *ext = strrchr(name, '.');
      RString bankName = RString("MPMissions\\") + RString(name, ext - name);
      AddMissionInBank(bankName, placement, missionList);

    }
    break;
  case MPDirectory:
    {
      RString dir = RString("MPMissions\\") + RString(name);
      AddMissionInDirectory(name, dir, placement, missionList);
    }
    return;
#endif
  case MPOldEditor:
    {
      RString dir = GetUserDirectory() + RString("MPMissions\\") + RString(name);
      AddMissionInDirectory(name, dir, placement, missionList);
    }
    return;
#if _ENABLE_UNSIGNED_MISSIONS
  case MPDownloaded:
    {
      // remove extension .pbo
      const char *ext = strrchr(name, '.');
      if (stricmp(ext, ".pbo") != 0) return;
      RString bankName = RString(name, ext - name);
      AddMissionInBank(bankName, placement, missionList);
    }
    break;
#endif // _ENABLE_UNSIGNED_MISSIONS
  default:
    Fail("Unknown placement");
    return;
  }
}


#if _ENABLE_UNSIGNED_MISSIONS
static bool AddMPMissionFile(const FileItem &file, AutoArray<MPMissionInfo> &list)
{
  if (file.directory)
  {
    AddMission(file.filename, MPDirectory, list); // public directory
  }
  else
  {
    const char *ext = strrchr(file.filename, '.');
    if (ext && stricmp(ext, ".pbo") == 0)
      AddMission(file.filename, MPPbo, list); // public bank
  }
  ProgressRefresh();

  return false; // continue with enumeration
}
#endif // _ENABLE_UNSIGNED_MISSIONS

#if _ENABLE_EDITOR
static bool AddMPUserMissionFile(const FileItem &file, AutoArray<MPMissionInfo> &list)
{
  if (file.directory)
  {
    AddMission(file.filename, MPOldEditor, list); // private directory
  }
  ProgressRefresh();

  return false; // continue with enumeration
}
#endif // _ENABLE_EDITOR

#if _ENABLE_UNSIGNED_MISSIONS
static bool AddMPDownloadedMissionFile(const FileItem &file, AutoArray<MPMissionInfo> &list)
{
  if (!file.directory)
  {
    AddMission(file.path + file.filename, MPDownloaded, list); // downloaded bank
  }
  ProgressRefresh();

  return false; // continue with enumeration
}
#endif

//! load list of all multiplayer missions
void LoadMissions(bool userMissions, AutoArray<MPMissionInfo> &missionList, bool campaignCheat)
{
  SECUROM_MARKER_HIGH_SECURITY_ON(16)
  Ref<ProgressHandle> p = ProgressStartExt(true, LocalizeString(IDS_LOAD_WORLD));

  missionList.Clear();


#if _ENABLE_UNSIGNED_MISSIONS
  ForEachFile("MPMissions\\", AddMPMissionFile, missionList);
#endif

#if _ENABLE_EDITOR
  if (userMissions)
  {
    ForEachFile(GetUserDirectory() + RString("MPMissions\\"), AddMPUserMissionFile, missionList);
  }
#endif

#if _ENABLE_UNSIGNED_MISSIONS && defined _XBOX 
  #if _XBOX_VER>=2
  // TODOX360: decide if we want mission files on X360
  #else
  // parse downloaded missions
  XCONTENT_FIND_DATA data;
  HANDLE handle = XFindFirstContent("T:\\", 0x0002, &data);
  if (handle != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (CheckContent(data.szContentDirectory))
      {
        RString dir = RString(data.szContentDirectory) + RString("\\MPMissions\\");
        ForEachFile(dir, AddMPDownloadedMissionFile, missionList);
      }
    } while (XFindNextContent(handle, &data));
    XFindClose(handle);
  }
  #endif
#endif // _ENABLE_UNSIGNED_MISSIONS && defined _XBOX

#if _ENABLE_MISSION_CONFIG
  ParamEntryVal list = Pars >> "CfgMissions" >> "MPMissions";
  for (int i=0; i<list.GetEntryCount(); i++)
  {
    ParamEntryVal entry = list.GetEntry(i);

    ConstParamEntryPtr ptr = entry.FindEntry("condition");
    if (ptr)
    {
      ParamEntryVal cnd = *ptr;
      if (cnd.IsArray())
      {
        bool exist = true;
        for (int i=0; i< cnd.GetSize();++i)
          if (!(Pars >> "CfgPatches").FindEntry((RString)cnd[i].GetValue())) exist = false;

        if (!exist) continue;
      }
      else if (cnd.IsTextValue())
      {
        if (!(Pars >> "CfgPatches").FindEntry((RString)cnd.GetValue())) continue;
      }
    }

    RString dir = entry >> "directory";
    const char *ext = strrchr(dir, '.');
    if (!ext) continue;
    RString name = entry.GetName() + RString(ext); // encode entry name and island
    AddMissionInDirectory(name, dir, MPInAddon, missionList, &entry); // mission in addon
  }
#endif

  ProgressFinish(p);
  SECUROM_MARKER_HIGH_SECURITY_OFF(16)
}

static void UpdatePlayerList(CListBox *lbox, int (*getState)(const PlayerIdentity &identity))
{
  lbox->SetReadOnly();
  int sel = lbox->GetCurSel(); saturateMax(sel, 0);
  lbox->ClearStrings();
  const AutoArray<PlayerIdentity> *ids = GetNetworkManager().GetIdentities();
  if (ids)
  {
    for (int i=0; i<ids->Size(); i++)
    {
      const PlayerIdentity &identity = ids->Get(i);
      int index = lbox->AddString(GetIdentityText(identity));
      int state = getState(identity);
      lbox->SetValue(index, state);
      static const PackedColor itemColor[5]=
      {
        PackedColor(Color(0.75, 0, 0, 1)), // read
        PackedColor(Color(1, 1, 0, 1)), // yellow
        PackedColor(Color(0, 1, 0, 1)), // green
        PackedColor(Color(0, 0, 1, 1)), // blue
        PackedColor(Color(1, 1, 1, 1)), // white
      };
      saturate(state,0,lenof(itemColor)-1);
      lbox->SetFtColor(index, itemColor[state]);
      lbox->SetSelColor(index, itemColor[state]);
    }
    lbox->SortItemsByValue();
    lbox->SetCurSel(sel, false);
  }
}

static int VotingPlayerState(const PlayerIdentity &identity)
{
  // did the user already vote?
  switch (identity.clientState)
  {
  case NCSNone:
  case NCSCreated:
  case NCSConnected:
    // not fully connected yet - show as red
    return 2;
  case NCSLoggedIn:
  default:
    // not voted yet - show yellow
    return 1;
  case NCSMissionSelected:
    // voted - show green
    return 2;
  }
}

/// update timeout for a given server state
static void UpdateTimeoutText(Display *disp, int idc, int ids)
{
  ITextContainer *text = GetTextContainer(disp->GetCtrl(idc));
  if (text)
  {
    int timeout = GetNetworkManager().GetServerTimeout();
    if (timeout<INT_MAX)
    {
      RString format = LocalizeString(ids);
      text->SetText(Format(format,timeout));
    }
    else
    {
      text->SetText("");
    }
  }
}

/*!
\patch 5089 Date 11/21/2006 by Jirka
- New: Support for improved UI in dialogs during joining to dedicated server 
*/

// Gamemaster select mission display
DisplayRemoteMissions::DisplayRemoteMissions(ControlsContainer *parent)
: Display(parent)
{
  _enableSimulation = false;
  Load("RscDisplayRemoteMissions");
  UpdateMissions();

  _difficulty = Glob.config.diffDefault;
  if (GetCtrl(IDC_SERVER_EDITOR)) GetCtrl(IDC_SERVER_EDITOR)->ShowCtrl(false);
  if (GetCtrl(IDC_SERVER_VOTED_MISSIONS)) GetCtrl(IDC_SERVER_VOTED_MISSIONS)->EnableCtrl(false);
}

Control *DisplayRemoteMissions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
    case IDC_SERVER_ISLAND:
      {
        CListBoxContainer *lbox = GetListBoxContainer(ctrl);
        if (!lbox) return ctrl;

        int sel = 0;
        // int m = (Pars>>"CfgWorlds">>"worlds").GetSize();
        int m = (Pars >> "CfgWorldList").GetEntryCount();
        for (int j=0; j<m; j++)
        {
          ParamEntryVal entry = (Pars >> "CfgWorldList").GetEntry(j);
          if (!entry.IsClass()) continue;
          RString name = entry.GetName();

#if _FORCE_DEMO_ISLAND
          RString demo = Pars >> "CfgWorlds" >> "demoWorld";
          if (stricmp(name, demo) != 0) continue;
#endif
          // RString name = (Pars>>"CfgWorlds">>"worlds")[j];

          // ADDED - check if wrp file exists
          RString fullname = GetWorldName(name);
          if (!QFBankQueryFunctions::FileExists(fullname)) continue;
      
          int index = lbox->AddString
          (
            Pars >> "CfgWorlds" >> name >> "description"
          );
          lbox->SetData(index, name);
          if (stricmp(name, Glob.header.worldname) == 0)
            sel = index;
          RString textureName = Pars >> "CfgWorlds" >> name >> "icon";
          RString fullName = FindPicture(textureName);
          fullName.Lower();
          Ref<Texture> texture = GlobLoadTextureUI(fullName);
          lbox->SetTexture(index, texture);
        }
        lbox->SetCurSel(sel);
        break;
      }
    case IDC_SERVER_MISSION:
      break;
    case IDC_SERVER_DIFF:
      _difficulties = GetListBoxContainer(ctrl);
      if(_difficulties){
        for(int i=0; i<Glob.config.diffSettings.Size();i++)
          _difficulties->AddString(Glob.config.diffSettings[i].displayName);
      }
      return ctrl;
    case IDC_SERVER_MI_OVERVIEW:
      _overview = GetHTMLContainer(ctrl);
      ctrl->EnableCtrl(false);
      break;
  }
  return ctrl;
}


void DisplayRemoteMissions::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_AUTOCANCEL:
      Exit(idc);
      break;
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

void DisplayRemoteMissions::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_SERVER_ISLAND:
    UpdateMissions();
    break;
  case IDC_SERVER_MISSION:
    OnMissionChanged(curSel);
    break;
  case IDC_SERVER_DIFF:
    _difficulty = _difficulties->GetCurSel();
    break;
  }
  Display::OnLBSelChanged(ctrl, curSel);
}

void DisplayRemoteMissions::OnLBDblClick(int idc, int curSel)
{
  if (idc == IDC_SERVER_MISSION && curSel >= 0)
    OnButtonClicked(IDC_OK);
  else
    Display::OnLBDblClick(idc, curSel);
}

/*!
\patch 1.30 Date 11/01/2001 by Jirka
- Added: Multiplayer - state of mission voting is displayed
*/
void DisplayRemoteMissions::OnSimulate(EntityAI *vehicle)
{
  // update player list
  CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_SERVER_PLAYERS));
  if (lbox) UpdatePlayerList(lbox, VotingPlayerState);

  // update top mission list
  const VotingMissionProgressMessage *voting = GetNetworkManager().GetMissionVotingState();
  if (voting)
  {
    CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_SERVER_VOTED_MISSIONS));
    if (lbox)
    {
      lbox->SetReadOnly();
      lbox->ClearStrings();
      for (int i=0; i<voting->_missions.Size(); i++)
      {
        int votes = i<voting->_missionVotes.Size() ? voting->_missionVotes[i] : 0;
        RString name = voting->_missions[i].GetLocalizedValue();
        RString formattedVote = Format("%d: %s", votes, cc_cast(name));
        lbox->AddString(formattedVote);
      }
    }

    ITextContainer *text = GetTextContainer(GetCtrl(IDC_SERVER_VOTED_DIFFICULTY));
    if (text)
    {
      int value = Glob.config.diffNames.GetValue(voting->_diff);
      if (value >= 0)
      {
        RString format = LocalizeString(IDS_XBOX_VOTED_TOP_DIFF_TITLE);
        RString diff = Glob.config.diffSettings[value].displayName;
        text->SetText(Format(format,cc_cast(diff)));
      }
      else
      {
        text->SetText("");
      }
    }

  }

  UpdateTimeoutText(this,IDC_SERVER_TIMEOUT,IDS_XBOX_VOTED_TIMEOUT);

  if (!GetNetworkManager().CanSelectMission() && !GetNetworkManager().CanVoteMission())
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }

  NetworkServerState state = GetNetworkManager().GetServerState();
  if (state == NSSNone)
  {
    OnButtonClicked(IDC_CANCEL);
    return;
  }
  if (state >= NSSAssigningPlayers)
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }

  Display::OnSimulate(vehicle);
}

/*!
\patch 5088 Date 11/16/2006 by Jirka
- Fixed: Display for mission selecting / voting on dedicated server - mission list fixed
*/
void DisplayRemoteMissions::UpdateMissions(RString filename)
{
  CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_SERVER_ISLAND));
  if (!lbox) return;  
  int index = lbox->GetCurSel();
  RString island = index < 0 ? "" : lbox->GetData(index);

  lbox = GetListBoxContainer(GetCtrl(IDC_SERVER_MISSION));
  if (!lbox) return;  

  RString mission = filename;
  if (mission.GetLength() == 0)
  {
    index = lbox->GetCurSel();
    if (index >= 0)
    {
      mission = lbox->GetText(index);
    }
  }

  lbox->ClearStrings();
  if (island.GetLength() == 0) return;

  const AutoArray<MPMissionInfo> &names = GetNetworkManager().GetServerMissions();
  for (int i=0; i<names.Size(); i++)
  {
    if (stricmp(names[i].world, island) != 0) continue;
    int index = lbox->AddString(names[i].displayName.GetLocalizedValue());
    lbox->SetData(index, names[i].mission + RString(".") + names[i].world);
    lbox->SetValue(index,i);
  }

  lbox->SortItems();

  int sel = 0;
  for (int i=0; i<lbox->GetSize(); i++)
  {
    if (stricmp(lbox->GetText(i), mission) == 0) sel = i;
  }
  lbox->SetCurSel(sel);

  OnMissionChanged(lbox->GetCurSel());
}

void DisplayRemoteMissions::OnMissionChanged(int sel)
{
#ifdef _WIN32
  if (_overview) _overview->Init();
#endif

  CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_SERVER_MISSION));
  if (lbox && sel>=0) 
  {
    int missionIndex = lbox->GetValue(sel);
    const AutoArray<MPMissionInfo> &names = GetNetworkManager().GetServerMissions();
    if ( missionIndex>=0 && missionIndex<names.Size() )
    {
      const MPMissionInfo &info = names[missionIndex];

      ITextContainer *text;
      text = GetTextContainer(GetCtrl(IDC_SERVER_MI_GAMETYPE));
      if (text) {
        text->SetText(info.gameType);
      }
      text = GetTextContainer(GetCtrl(IDC_SERVER_MI_MAX_PLAYERS));
      if (text) {
        RString maxPl = info.maxPlayers > 0 ? Format("%d",info.maxPlayers) : RString("Unknown");
        text->SetText(maxPl);
      }

      if (_overview)
      {
        RString description = info.description.GetLocalizedValue();
        if (!description.IsEmpty())
        {
          RString descHTML = Format("<html><body><p>%s</p></body></html>", cc_cast(description));
          // parse the content
          QIStrStream in(cc_cast(descHTML), descHTML.GetLength());
          _overview->Load(in);
        }
      }
    }
  }
}

DisplayXRemoteMissions::DisplayXRemoteMissions(ControlsContainer *parent)
: Display(parent)
{
  _enableSimulation = false;

  _difficulty = Glob.config.diffDefault;

  Load("RscDisplayRemoteMissions");

  ParamEntryVal cls = Pars >> "RscDisplayRemoteMissions";
  _sliderColor = GetPackedColor(cls >> "colorSlider");
  _sliderColorActive = GetPackedColor(cls >> "colorSliderActive");

  if (GetCtrl(IDC_SERVER_EDITOR)) GetCtrl(IDC_SERVER_EDITOR)->ShowCtrl(false);

  if (_gameTypes)
  {
    ParamEntryVal list = Pars >> "CfgMPGameTypes";
    for (int i=0; i<list.GetEntryCount(); i++)
    {
      ParamEntryVal entry = list.GetEntry(i);
      int index = _gameTypes->AddString(entry >> "name");
      _gameTypes->SetData(index, entry.GetName());
    }
    _gameTypes->SetCurSel(0, false);
    _gameTypes->Expand(true);
  }
  if (_difficulties)
  {
    for (int i=0; i<Glob.config.diffSettings.Size(); i++)
    {
      _difficulties->AddString(Glob.config.diffSettings[i].displayName);
    }
    _difficulties->SetCurSel(_difficulty, false);
  }
  
  UpdateMissions();
  OnMissionChanged();

  IControl *ctrl = GetCtrl(IDC_SERVER_VOTED_MISSIONS);
  if (ctrl) ctrl->EnableCtrl(false);
  CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_SERVER_PLAYERS));
  if (lbox) lbox->SetStickyFocus(false);
}

Control *DisplayXRemoteMissions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_SERVER_GAMETYPE:
    _gameTypes = GetListBoxContainer(ctrl);
    break;
  case IDC_SERVER_MISSION:
    _missions = GetListBoxContainer(ctrl);
    break;
  case IDC_SERVER_DIFF:
    _difficulties = GetListBoxContainer(ctrl);
    break;
  case IDC_SERVER_ISLAND:
    _island = GetTextContainer(ctrl);
    break;
  case IDC_SERVER_MIN_PLAYERS:
    _minPlayers = GetTextContainer(ctrl);
    break;
  case IDC_SERVER_MAX_PLAYERS:
    _maxPlayers = GetTextContainer(ctrl);
    break;
  }
  return ctrl;
}

void DisplayXRemoteMissions::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_AUTOCANCEL:
    Exit(idc);
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayXRemoteMissions::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_SERVER_GAMETYPE:
    UpdateMissions();
    OnMissionChanged();
    if (_missions)
    {
      FocusCtrl(IDC_SERVER_MISSION);
      _missions->Expand(true);
    }
    break;
  case IDC_SERVER_MISSION:
    OnMissionChanged();
    if (_difficulties)
    {
      FocusCtrl(IDC_SERVER_DIFF);
      _difficulties->Expand(true);
    }
    break;
  case IDC_SERVER_DIFF:
    _difficulty = curSel;
    break;
  default:
    Display::OnLBSelChanged(ctrl, curSel);
    break;
  }
}

void DisplayXRemoteMissions::OnLBSelUnchanged(int idc, int curSel)
{
  switch (idc)
  {
  case IDC_SERVER_GAMETYPE:
    if (_missions)
    {
      FocusCtrl(IDC_SERVER_MISSION);
      _missions->Expand(true);
    }
    break;
  case IDC_SERVER_MISSION:
    if (_difficulties)
    {
      FocusCtrl(IDC_SERVER_DIFF);
      _difficulties->Expand(true);
    }
    break;
  default:
    Display::OnLBSelUnchanged(idc, curSel);
    break;
  }
}

void DisplayXRemoteMissions::OnLBListSelChanged(int idc, int curSel)
{
  switch (idc)
  {
  case IDC_SERVER_MISSION:
    {
      int index = curSel >= 0 ? _missions->GetValue(curSel) : -1;
      UpdateMissionInfo(index);
    }
    break;
  default:
    Display::OnLBListSelChanged(idc, curSel);
    break;
  }
}

void DisplayXRemoteMissions::OnSimulate(EntityAI *vehicle)
{
  if (!GetNetworkManager().CanSelectMission() && !GetNetworkManager().CanVoteMission())
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }

  NetworkServerState state = GetNetworkManager().GetServerState();
  if (state == NSSNone)
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }
  if (state >= NSSAssigningPlayers)
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }

  Display::OnSimulate(vehicle);

  // update player list
  {
    CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_SERVER_PLAYERS));
    if (lbox)
    {
      // show list of players only when briefing is not shown
      UpdatePlayerList(lbox,VotingPlayerState);
    }

    ITextContainer *text = GetTextContainer(GetCtrl(IDC_SERVER_PLAYERS_TITLE));
    if (text)
    {
      text->SetText(Format(LocalizeString(IDS_DISP_SRVSETUP_PLAYERS), lbox->GetSize()));
    }
  }

  const VotingMissionProgressMessage *voting = GetNetworkManager().GetMissionVotingState();
  
  // update top mission list
  if (voting)
  {
    CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_SERVER_VOTED_MISSIONS));
    if (lbox)
    {
      lbox->SetReadOnly();
      lbox->ClearStrings();
      for (int i=0; i<voting->_missions.Size(); i++)
      {
        int votes = i<voting->_missionVotes.Size() ? voting->_missionVotes[i] : 0;
        RString name = voting->_missions[i].GetLocalizedValue();
        RString formattedVote = Format("%d: %s", votes, cc_cast(name));
        lbox->AddString(formattedVote);
      }
    }
    
    ITextContainer *text = GetTextContainer(GetCtrl(IDC_SERVER_VOTED_DIFFICULTY));
    if (text)
    {
      int value = Glob.config.diffNames.GetValue(voting->_diff);
      if (value >= 0)
      {
        RString format = LocalizeString(IDS_XBOX_VOTED_TOP_DIFF_TITLE);
        RString diff = Glob.config.diffSettings[value].displayName;
        text->SetText(Format(format,cc_cast(diff)));
      }
      else
      {
        text->SetText("");
      }
    }
    
  }
  
  UpdateTimeoutText(this,IDC_SERVER_TIMEOUT,IDS_XBOX_VOTED_TIMEOUT);
}

RString DisplayXRemoteMissions::GetMission() const
{
  if (!_missions) return RString();
  int sel = _missions->GetCurSel();
  if (sel < 0) return RString();
  const AutoArray<MPMissionInfo> &missionList = GetNetworkManager().GetServerMissions();
  const MPMissionInfo &info = missionList[_missions->GetValue(sel)];
  return info.mission + RString(".") + info.world;
//  return _missions->GetData(index);
}

void DisplayXRemoteMissions::UpdateMissions(RString filename)
{
  if (!_gameTypes) return;
  int sel = _gameTypes->GetCurSel();
  if (sel < 0) return;
  RString gameType = _gameTypes->GetData(sel);

  if (!_missions) return;
  _missions->ClearStrings();

  const AutoArray<MPMissionInfo> &missionList = GetNetworkManager().GetServerMissions();
  for (int i=0; i<missionList.Size(); i++)
  {
    const MPMissionInfo &info = missionList[i];
    if (stricmp(info.gameType, gameType) != 0) continue;
    // if given world is not installed, ignore the mission
    ParamEntryVal worlds = Pars >> "CfgWorlds";
    ConstParamEntryPtr cfg = worlds.FindEntry(info.world);
    if (!cfg) continue;   
    RString island = (*cfg) >> "description";
    int index = _missions->AddString(info.displayName.GetLocalizedValue());
    _missions->SetValue(index, i);
  }

  _missions->SetCurSel(0, false);

  UpdateKeyHints();
}

# ifdef NOMINMAX
#  undef min
#  undef max
# endif
void DisplayXRemoteMissions::UpdateMissionInfo(int index)
{
  if (index >= 0)
  {
    const AutoArray<MPMissionInfo> &missionList = GetNetworkManager().GetServerMissions();
    const MPMissionInfo &info = missionList[index];

    if (_island)
    {
      ParamEntryVal worlds = Pars >> "CfgWorlds";
      ConstParamEntryPtr cfg = worlds.FindEntry(info.world);
      RString island = info.world;
      if (cfg) island = (*cfg) >> "description";
      _island->SetText(island);
    }
    if (_minPlayers) _minPlayers->SetText(Format("%d", info.minPlayers));
    if (_maxPlayers)
    {
      int maxPlayers = std::min(MAX_PLAYERS_DS, info.maxPlayers);
      _maxPlayers->SetText(Format("%d", maxPlayers));
    }
  }
  else
  {
    if (_island) _island->SetText(RString());
    if (_minPlayers) _minPlayers->SetText(RString());
    if (_maxPlayers) _maxPlayers->SetText(RString());
  }
}

void DisplayXRemoteMissions::OnMissionChanged()
{
  if (!_missions) return;
  int sel = _missions->GetCurSel();
  int index = sel >= 0 ? _missions->GetValue(sel) : -1;
  UpdateMissionInfo(index);
}

DisplayXVotedMissions::DisplayXVotedMissions(ControlsContainer *parent)
: Display(parent)
{
  _enableSimulation = false;

  Load("RscDisplayRemoteMissionVoted");

  IControl *ctrl = GetCtrl(IDC_SERVER_VOTED_MISSIONS);
  if (ctrl) ctrl->EnableCtrl(false);
}

Control *DisplayXVotedMissions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  return base::OnCreateCtrl(type,idc,cls);
}
void DisplayXVotedMissions::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_AUTOCANCEL:
      Exit(idc);
      return;
    case IDC_OK:
      return;
  }
  return base::OnButtonClicked(idc);
}

/*!
\patch 5132 Date 2/23/2007 by Jirka
- Fixed: MP - situation when admin logged in during mission voting is handled correctly now
*/

void DisplayXVotedMissions::OnSimulate(EntityAI *vehicle)
{
  if (!GetNetworkManager().CanVoteMission())
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }
  if (GetNetworkManager().CanSelectMission())
  {
    // admin, selected #missions
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }

  NetworkServerState state = GetNetworkManager().GetServerState();
  if (state == NSSNone)
  {
    OnButtonClicked(IDC_CANCEL);
    return;
  }
  if (state >= NSSAssigningPlayers || state<NSSSelectingMission)
  {
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  }

  base::OnSimulate(vehicle);

  // update player list
  {
    CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_SERVER_PLAYERS));
    if (lbox)
    {
      // show list of players only when briefing is not shown
      UpdatePlayerList(lbox,VotingPlayerState);
    }

    ITextContainer *text = GetTextContainer(GetCtrl(IDC_SERVER_PLAYERS_TITLE));
    if (text && lbox)
    {
      text->SetText(Format(LocalizeString(IDS_DISP_SRVSETUP_PLAYERS), lbox->GetSize()));
    }
  }

  const VotingMissionProgressMessage *voting = GetNetworkManager().GetMissionVotingState();
  
  // update top mission list
  if (voting)
  {
    CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_SERVER_VOTED_MISSIONS));
    if (lbox)
    {
      lbox->SetReadOnly();
      lbox->ClearStrings();
      for (int i=0; i<voting->_missions.Size(); i++)
      {
        int votes = i<voting->_missionVotes.Size() ? voting->_missionVotes[i] : 0;
        RString name = voting->_missions[i].GetLocalizedValue();
        RString formattedVote = Format("%d: %s", votes, cc_cast(name));
        lbox->AddString(formattedVote);
      }
    }
    
    ITextContainer *text = GetTextContainer(GetCtrl(IDC_SERVER_VOTED_DIFFICULTY));
    if (text)
    {
      int value = Glob.config.diffNames.GetValue(voting->_diff);
      if (value >= 0)
      {
        RString format = LocalizeString(IDS_XBOX_VOTED_TOP_DIFF_TITLE);
        RString diff = Glob.config.diffSettings[value].displayName;
        text->SetText(Format(format,cc_cast(diff)));
      }
      else
      {
        text->SetText("");
      }
    }
    
  }
  
  UpdateTimeoutText(this,IDC_SERVER_TIMEOUT,IDS_XBOX_VOTED_TIMEOUT);
}

SignInfo::SignInfo()
{
  _pos = VZero;
}

// Client display
Control *DisplayClient::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_CLIENT_PROGRESS:
    if (type == CT_PROGRESS)
    {
      _progress = static_cast<CProgressBar *>(ctrl);
      _progress->SetRangeAndPos(0, 1, 0);
    }
    break;
  }
  return ctrl;
}

void DisplayClient::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_OK:
      {
        baseML::InitGameInfo();
      }
      break;
    case IDC_AUTOCANCEL:
      Exit(idc);
      break;
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

void DisplayClient::OnChildDestroyed(int idd, int exit)
{
  //RptF("*** DisplayClient::OnChildDestroyed %d", idd);
  switch (idd)
  {
    case IDD_MP_SETUP:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_CANCEL) Exit(IDC_CANCEL);  
      break;
//    case IDD_CLIENT_SETUP:
    case IDD_CLIENT_SIDE:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_CANCEL) Exit(IDC_CANCEL);  
      break;
    case IDD_SERVER:
      // remote missions display
      if (exit == IDC_OK)
      {
        RString mission;
        CListBoxContainer *lbox = GetListBoxContainer(_child->GetCtrl(IDC_SERVER_MISSION));
        if (lbox)
        {
          int sel = lbox->GetCurSel();
          if (sel >= 0) mission = lbox->GetData(sel);
        }

        int difficulty = Glob.config.diffDefault;
        DisplayRemoteMissions *disp = dynamic_cast<DisplayRemoteMissions *>((ControlsContainer *)_child);
        if (disp) difficulty = disp->_difficulty;

        Display::OnChildDestroyed(idd, exit);
        
        if (GetNetworkManager().CanSelectMission())
        {
          GetNetworkManager().SelectMission(mission, difficulty);
          GetNetworkManager().SetClientState(NCSMissionSelected);
        }
        else if (GetNetworkManager().CanVoteMission())
        {
          GetNetworkManager().VoteMission(mission, difficulty);
          GetNetworkManager().SetClientState(NCSMissionSelected);
          // display voting progress instead of Wait for server
          CreateChild(new DisplayXVotedMissions(this));
        }
      }
      else
      {
        Display::OnChildDestroyed(idd, exit);
        if (exit == IDC_CANCEL) Exit(IDC_CANCEL); // do not end session when autocancel
      }
      break;
    case IDD_SERVER_VOTED:
      // if this screen is closed by the user, he wants to vote again
      Display::OnChildDestroyed(idd, exit);
      if (exit==IDC_CANCEL)
      {
        if (GetNetworkManager().CanVoteMission())
        {
          CreateChild(new DisplayRemoteMissions(this));
        }
      }
      break;
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    case IDD_MSG_ACCEPT_INVITATION:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK)
      {
        Exit(IDC_CANCEL);
      }
      else
      {
        // Invitation rejected
        GSaveSystem.InvitationHandled();
      }
      break;    
#endif
    case IDD_MSG_LAUNCHGAME:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_OK && GetNetworkManager().IsGameMaster())
      {
        GetNetworkManager().SetClientState(NCSPlayerAssigned);
        if (GetCtrl(IDC_OK)) GetCtrl(IDC_OK)->ShowCtrl(false);
      }
      break;
    case IDD_MISSION:
      {
        Display::OnChildDestroyed(idd, exit);
        GetNetworkManager().SetClientState(NCSMissionAsked);
        GetNetworkManager().DestroyAllObjects();
        GStats.ClearMission();
        baseML::ResetInfo();
        if (exit==IDC_MAIN_QUIT)
          Exit(exit);
      }
      break;
    case IDD_CLIENT_WAIT:
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_CANCEL) Exit(IDC_CANCEL);  
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

/*!
\patch 1.55 Date 5/7/2002 by Jirka
- Fixed: MP - when mission was voted repeatedly, all players was green (even when didn't vote yet)
*/
void DisplayClient::OnSimulate(EntityAI *vehicle)
{
  if ( !baseML::OnSimulate() ) return;

  NetworkServerState state = GetNetworkManager().GetServerState();

  switch (state)
  {
  case NSSNone:
    OnButtonClicked(IDC_AUTOCANCEL);
    return;
  case NSSSelectingMission:
    {
      CStatic *text = dynamic_cast<CStatic *>(GetCtrl(IDC_CLIENT_TEXT));
      if (text) text->SetText(LocalizeString(IDS_DISP_CLIENT_TEXT));
    }
    if (GetNetworkManager().CanSelectMission() || GetNetworkManager().CanVoteMission())
    {
      CreateChild(new DisplayRemoteMissions(this));
    }
    break;
  case NSSEditingMission:
    {
      CStatic *text = dynamic_cast<CStatic *>(GetCtrl(IDC_CLIENT_TEXT));
      if (text) text->SetText(LocalizeString(IDS_DISP_CLIENT_TEXT));
    }
    break;
  case NSSAssigningPlayers:
  case NSSSendingMission:
    break;
  case NSSLoadingGame:
  case NSSPlaying:
      { //NMTMissionHeader received
        if (GetNetworkManager().GetClientState()>=NCSMissionAsked && GetNetworkManager().GetClientState()<NCSPlayerAssigned)
        { // not initiated yet
          baseML::InitGameInfo();
          //RptF("*** DisplayClient::OnSimulate, setting NCSPlayerAssigned");
          GetNetworkManager().SetClientState(NCSPlayerAssigned);
        }
      }
    break;
  }

  // update player list
  CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_CLIENT_PLAYERS));
  if (lbox)
  {
    lbox->SetReadOnly();
    int sel = lbox->GetCurSel(); saturateMax(sel, 0);
//    lbox->ShowSelected(false);
    lbox->ClearStrings();

    const AutoArray<PlayerIdentity> &identities = *GetNetworkManager().GetIdentities();
    for (int i=0; i<identities.Size(); i++)
    {
      const PlayerIdentity &identity = identities[i];
      int index = lbox->AddString(GetIdentityText(identity));
      switch (identity.clientState)
      {
      case NCSNone:
      case NCSCreated:
      case NCSConnected:
      case NCSLoggedIn:
        lbox->SetFtColor(index, PackedColor(Color(1, 0, 0, 1)));
        lbox->SetSelColor(index, PackedColor(Color(1, 0, 0, 1)));
        lbox->SetValue(index, 0);
        break;
      case NCSMissionSelected:
      case NCSMissionAsked:
      case NCSPlayerAssigned:
      case NCSMissionReceived:
      case NCSGameLoaded:
      case NCSPlaying:
      case NCSGameFinished:
        lbox->SetFtColor(index, PackedColor(Color(0, 1, 0, 1)));
        lbox->SetSelColor(index, PackedColor(Color(0, 1, 0, 1)));
        lbox->SetValue(index, 2);
        break;
      }
    }
    lbox->SortItemsByValue();
    lbox->SetCurSel(sel, false);
  }

  // update custom files progress bar
  if (_progress)
  {
    int max = 1, pos = 0;
    GetNetworkManager().CustomFilesProgressGet(max, pos);
    _progress->SetRangeAndPos(0, max, pos);
  }

  if (_messageText.GetLength() == 0)
    Display::OnSimulate(vehicle);
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

void DisplayClient::OnInvited()
{
  if (_msgBox) return; // confirmation in progress

  if (GSaveSystem.IsInvitationConfirmed())
  {
    Exit(IDC_OK);
  }
  else
  {
    GSaveSystem.ConfirmInvitation();
    // let the user confirm the action because of possible progress lost
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    RString message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION_CLIENT);
    // special type of message box which will not close because of invitation
    _msgBox = new MsgBoxIgnoreInvitation(this, MB_BUTTON_OK | MB_BUTTON_CANCEL, message, IDD_MSG_ACCEPT_INVITATION, false, &buttonOK, &buttonCancel);
  }
}

#endif

MultiplayerLauncher::MultiplayerLauncher()
{
  ResetInfo();
  _delayedStart = false;
  _msg = new MultiplayerSetupMessage();
}

MultiplayerLauncher::~MultiplayerLauncher()
{
  if (_progressML) { ProgressFinish(_progressML); }
}

void MultiplayerLauncher::SaveLastMission()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (!ParseUserParams(cfg, &globals)) return;
  RString displayName = GetNetworkManager().GetMissionHeader()->GetLocalizedMissionName();
  cfg.Add("lastMPMission", displayName);
  SaveUserParams(cfg);
}

void MultiplayerLauncher::InitGameInfo()
{
  // init game info
  SetBaseDirectory(false, "");
  CurrentMission = "";
  Glob.config.difficulty = Glob.config.diffDefault;

  CurrentTemplate = new DummyMission;
  if (GWorld->UI()) GWorld->UI()->Init();
}

bool MultiplayerLauncher::OnSimulate()
{
  NetworkServerState state = GetNetworkManager().GetServerState();
  if (GetNetworkManager().IsServer())
  {
    switch (state)
    {
    case NSSSendingMission:
      GetNetworkManager().SendMissionFile();
      GetNetworkManager().SetServerState(NSSLoadingGame);
      break;

    case NSSLoadingGame:
      if (_loadIsland)
      {
        _loadIsland = false;

#if USE_PROGRESS_ML
        if (!_progressML) _progressML = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeNetware);
#endif

        GLOB_WORLD->SwitchLandscape(Glob.header.worldname);

        // set parameters - after SwitchLandscape
        float param1 = 0, param2 = 0;
        GetNetworkManager().GetParams(param1, param2);
        GameState *gstate = GWorld->GetGameState();
        gstate->VarSet("param1", GameValue(param1), false, false, GWorld->GetMissionNamespace()); // mission namespace
        gstate->VarSet("param2", GameValue(param2), false, false, GWorld->GetMissionNamespace()); // mission namespace
        GetNetworkManager().PublicVariable("param1", 0);
        GetNetworkManager().PublicVariable("param2", 0);

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
        GetNetworkManager().PublicVariable("paramsArray", 0);

        GetNetworkManager().ClearDelayedPublicVariables();
        CurrentTemplate->ActivateAddons(Glob.header.worldname);
        CurrentTemplate->InitGeneral(GModeNetware);
        if (!GLOB_WORLD->InitVehicles(*CurrentTemplate))
        {
          // TODO: initvehicles returned error, we cannot proceed to the next state
        }

        SetMessage(LocalizeString(IDS_NETWORK_SEND),0,0);

        GetNetworkManager().CreateAllObjects();
        // PublicVariable calls containing objects with NULL networkId should be processed after the CreateAllObjects call again
        GetNetworkManager().ProcessDelayedPublicVariables();
        // objects are created on client, so we can proceed after transfer from client to server completes
        SaveLastMission();
        DoVerify(DeleteWeaponsFile());
        SetIngameVoiceChannel();
        RunInitScript(); //and wait until player is selected
        GWorld->OnChannelChanged();
      }
      break;
    case NSSPlaying:
      // do not continue, until player identity is verified
      if (!GetNetworkManager().FindIdentity(GetNetworkManager().GetPlayer()))
        break;
      else
      { // we need to wait until RunInitScript selects the player
        if ( GWorld->CameraOn() ) //player is selected
        {
#if USE_PROGRESS_ML
          if (_progressML) ProgressFinish(_progressML);
#endif
          //RptF("*** MultiplayerLauncher::OnSimulate - setting NCSPlaying");

          GetNetworkManager().SetClientState(NCSPlaying);
          GWorld->OnChannelChanged();
          SetMessage(RString(),0,0);
          GetNetworkManager().TrackUsage();
          CreateChildML(new DisplayMission(GetDisplayML(), false, true));
        }
      }
      break;
    }
  }
  else
  { // not server -> a client
    NetworkClientState clientState = GetNetworkManager().GetClientState();

    switch (clientState)
    {
    case NCSMissionAsked:
      _delayedStart = false;
      break;
    case NCSPlayerAssigned:
      if (state > NSSAssigningPlayers)
      {
        // receiving mission, show progress
        int curBytes, totBytes;
        GetNetworkManager().GetTransferStats(curBytes, totBytes);
        int curKB = toInt(curBytes / 1024);
        int totKB = toInt(totBytes / 1024);
        BString<256> buffer;
        sprintf(buffer, LocalizeString(IDS_NETWORK_RECEIVE_MISSION), curKB, totKB);
        SetMessage(cc_cast(buffer), curBytes, totBytes);
      }
      break;
    case NCSMissionReceived:
      // loading game, show message
      {
        float progress = 0;
        if (GetNetworkManager().GetTransferProgress(progress))
          SetMessage(LocalizeString(IDS_NETWORK_RECEIVE), toInt(1e6 * progress), 1e6);
        else
          SetMessage(LocalizeString(IDS_DISP_CLIENT_TEXT), 0, 0);
      }
      break;
    case NCSGameLoaded:
    case NCSPlaying:
      /* Mission could start even without player
      // delay display until player is selected to avoid game without player
      if (!GWorld->CameraOn() && state == NSSPlaying)
      {
        SetMessage(LocalizeString(IDS_NETWORK_RECEIVE), 0,0);
        break;
      }
      */
      // game is loaded, prepare game and display briefing
      if (!_delayedStart)
      {
        SetMessage(RString(),0,0);
    
        SaveLastMission();

        DoVerify(DeleteWeaponsFile());

        SetIngameVoiceChannel();
        if (state == NSSPlaying)
        {
          //RptF("*** MultiplayerLauncher::OnOnSimulate - NCSPlaying");
          GetNetworkManager().SetClientState(NCSPlaying);
          GWorld->OnChannelChanged();
          _delayedStart = true;
        }
        else
        {
          //RptF("*** MultiplayerLauncher::OnOnSimulate - NCSMissionAsked");
          GetNetworkManager().SetClientState(NCSMissionAsked);
          GetNetworkManager().DestroyAllObjects();
        }
      }
      if (_delayedStart)
      {
        SetMessage(LocalizeString(IDS_NETWORK_RECEIVE),0,0);
      }
      if (_delayedStart /*&& GWorld->CameraOn()*/) //mission could start even without player
      {
        GetNetworkManager().OnJoiningInProgress();
        GStats.OnMPMissionStart();
        //GWorld->AskPreload(); //mission should do the preload on her own using 'preloadCamera'
        GetNetworkManager().TrackUsage();
        CreateChildML(new DisplayMission(GetDisplayML(), false, false, true));

        GWorld->OnChannelChanged();

        _delayedStart = false;
        SetMessage(RString(),0,0);

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
        // Mission started
        DWORD presence = GetNetworkManager().GetSessionType() == STSystemLink ?
          CONTEXT_PRESENCE_STR_RP_SYSTEMLINK_PLAYING :
          CONTEXT_PRESENCE_STR_RP_LIVE_PLAYING;
        GSaveSystem.SetContext(X_CONTEXT_PRESENCE, presence);
#endif
      }
      break;
    }
  }
  return true;
}

void DisplayMPPlayers::OnSimulate(EntityAI *vehicle)
{
//#if _ENABLE_UA_NETWORK_PLAYERS
  if (GInput.GetActionToDo(UANetworkPlayers, true, false))
  {
    OnButtonClicked(IDC_CANCEL);
  }
//#endif

  if (GetNetworkManager().GetClientState() != NCSPlaying)
  {
    OnButtonClicked(IDC_CANCEL);
  }

  UpdatePlayers();
  Display::OnSimulate(vehicle);
}

void DisplayMPPlayers::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_MP_PLAYERS)
    UpdatePlayerInfo();
  else
    Display::OnLBSelChanged(ctrl, curSel);
}

/*!
\patch 1.25 Date 10/01/2001 by Jirka
- Changed: Players in Multiplayer Players Dialog are colored by the same way as in statistics.
\patch 1.34 Date 12/7/2001 by Jirka
- Added: Time left in MP mission displayed if EstimatedEndTime variable defined
*/

void DisplayMPPlayers::UpdatePlayers()
{
  const MissionHeader *header = GetNetworkManager().GetMissionHeader();
  
  RString mission, island;
  int time = 0;
  if (header)
  {
    mission = header->GetLocalizedMissionName();
    island = header->_island;
    if (GetNetworkManager().GetServerState() == NSSPlaying)
      time = GlobalTickCount() - header->_start;
  }

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_MP_PL_MISSION));
  if (text) text->SetText(mission);

  text = GetTextContainer(GetCtrl(IDC_MP_PL_ISLAND));
  if (text) text->SetText(island);

  int t = time / 1000;
  int m = t / 60;
  int s = t - m * 60;

  text = GetTextContainer(GetCtrl(IDC_MP_PL_TIME));
  if (text)
  {
    char buffer[256];
    sprintf(buffer, "%d:%02d", m, s);
    text->SetText(buffer);
  }

  IControl *ctrl = GetCtrl(IDC_MP_PL_REST);
  text = GetTextContainer(ctrl);
  if (text)
  {
    float et = GetNetworkManager().GetEstimatedEndTime().toFloat();
    if (et > 0)
    {
      et -= t;
      int em = toInt(et / 60.0);
      if (time != 0) saturateMax(em, 1);

      char buffer[256];
      sprintf(buffer, LocalizeString(IDS_TIME_LEFT), em);
      text->SetText(buffer);
      ctrl->ShowCtrl(true);
    }
    else
      ctrl->ShowCtrl(false);
  }

  CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MP_PLAYERS));
  if (!lbox) return;

/*
  PackedColor inactive = ModAlpha(lbox->GetFtColor(), 0.5);
  PackedColor inactiveSel = ModAlpha(lbox->GetSelColor(), 0.5);
*/

  int sel = lbox->GetCurSel();
  int cur = sel >= 0 ? lbox->GetValue(sel) : 0;

  sel = 0;
  lbox->ClearStrings();
  const AutoArray<PlayerIdentity> *pid = GetNetworkManager().GetIdentities();
  if (pid)
  {
    for (int i=0; i<pid->Size(); i++)
    {
      const PlayerIdentity &identity = pid->Get(i);
      char buffer[256];
      sprintf(buffer, "%d: %s", identity.playerid, (const char *)identity.GetName());
      int index = lbox->AddString(buffer);
      lbox->SetValue(index, identity.dpnid);
      PackedColor colorSel = _color;

      PackedColor color = ModAlpha(colorSel, 0.5);
#ifndef _XBOX
      bool isSpeaking = false;
      if (identity.dpnid == GetNetworkManager().GetPlayer())
      {
        isSpeaking = GetNetworkManager().IsVoiceRecording();
      }
      else
      {
        isSpeaking = GetNetworkManager().IsVoicePlaying(identity.dpnid);
      }
      if (isSpeaking)
      {
        // TODO: proper implementation (config values, icons...)
        // now: when speaking, flash color between the current one and transparent
        // flash continuously 1..0..1
        float period = 0.5f;
        float flash = fabs(Glob.uiTime.Mod(period)-period*0.5)*(2.0f/period);
        
        int a8 = toInt(flash*color.A8());
        saturate(a8,0,255);
        color.SetA8(a8);

        int a8s = toInt(flash*colorSel.A8());
        saturate(a8s,0,255);
        colorSel.SetA8(a8s);
      }
      
#endif

      lbox->SetFtColor(index, color);
      lbox->SetSelColor(index, colorSel);
      /*
      if (identity.state != NGSPlay)
      {
        lbox->SetFtColor(index, inactive);
        lbox->SetSelColor(index, inactiveSel);
      }
      */
      if (identity.dpnid == cur) sel = index;
    }
  }
  lbox->SetCurSel(sel);
}


static RString FormatNumberMaxDigits(int number, int maxDigits)
{
  int maxNumber = 1;
  while (--maxDigits>=0) maxNumber *= 10;
  maxNumber--;
  if (number>maxNumber) return FormatNumber(maxNumber);
  return FormatNumber(number);
}

inline RString FormatNumberBandwidth(int number)
{
  return FormatNumberMaxDigits(number, 6);
}

inline RString FormatNumberPing(int number)
{
  return FormatNumberMaxDigits(number, 4);
}

/*!
\patch 1.05 Date 7/17/2001 by Jirka
- Added: server can kick off players from "Players" dialog
*/
void DisplayMPPlayers::UpdatePlayerInfo()
{
  const PlayerIdentity *player = NULL;

  ITextContainer *text = GetTextContainer(GetCtrl(IDC_MP_PL));
  if (text) text->SetText(player ? player->name : "");

  text = GetTextContainer(GetCtrl(IDC_MP_PL_MINPING));
  if (text) text->SetText(player ? FormatNumberPing(player->_minPing) : "");
  text = GetTextContainer(GetCtrl(IDC_MP_PL_AVGPING));
  if (text) text->SetText(player ? FormatNumberPing(player->_avgPing) : "");
  text = GetTextContainer(GetCtrl(IDC_MP_PL_MAXPING));
  if (text) text->SetText(player ? FormatNumberPing(player->_maxPing) : "");
  text = GetTextContainer(GetCtrl(IDC_MP_PL_MINBAND));
  if (text) text->SetText(player ? FormatNumberBandwidth(player->_minBandwidth) : "");
  text = GetTextContainer(GetCtrl(IDC_MP_PL_AVGBAND));
  if (text) text->SetText(player ? FormatNumberBandwidth(player->_avgBandwidth) : "");
  text = GetTextContainer(GetCtrl(IDC_MP_PL_MAXBAND));
  if (text) text->SetText(player ? FormatNumberBandwidth(player->_maxBandwidth) : "");
  text = GetTextContainer(GetCtrl(IDC_MP_PL_DESYNC));
  if (text) text->SetText(player ? FormatNumberBandwidth(player->_desync) : "");

  // ADDED
  if (player)
  {
    bool show = GetNetworkManager().IsServer() && player->dpnid != GetNetworkManager().GetPlayer();
    IControl *ctrl = GetCtrl(IDC_MP_KICKOFF);
    if (ctrl) ctrl->ShowCtrl(show || GetNetworkManager().IsGameMaster() && player->dpnid != GetNetworkManager().GetPlayer());
    ctrl = GetCtrl(IDC_MP_BAN);
    if (ctrl) ctrl->ShowCtrl(show);

    ctrl = GetCtrl(IDC_MP_MUTE);
    if (ctrl)
    {
      // mute is always available
      bool show = player->dpnid != GetNetworkManager().GetPlayer();
      ctrl->ShowCtrl(show);
      // we change the text based on if the player is muted or not
      ITextContainer *text = GetTextContainer(ctrl);
      if (text)
      {
        bool muted = GetNetworkManager().IsPlayerInMuteList(player->xuid);
        text->SetText(muted && show ? "(o) Mute": "( ) Mute");
      }      
    }
  }
}

// ADDED
void DisplayMPPlayers::OnButtonClicked(int idc)
{
  switch (idc)
  {
    case IDC_MP_KICKOFF:
      if (GetNetworkManager().IsServer())
      {
        CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MP_PLAYERS));
        if (!lbox) break;
        int sel = lbox->GetCurSel();
        if (sel < 0) break;
        int dpnid = lbox->GetValue(sel);
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().KickOff(dpnid,KORKick);
      }
      else if (GetNetworkManager().IsGameMaster())
      {
        CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MP_PLAYERS));
        if (!lbox) break;
        int sel = lbox->GetCurSel();
        if (sel < 0) break;
        int dpnid = lbox->GetValue(sel);
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().SendKick(dpnid);
      }
      break;
    case IDC_MP_BAN:
      if (GetNetworkManager().IsServer())
      {
        CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MP_PLAYERS));
        if (!lbox) break;
        int sel = lbox->GetCurSel();
        if (sel < 0) break;
        int dpnid = lbox->GetValue(sel);
        if (dpnid != GetNetworkManager().GetPlayer())
          GetNetworkManager().Ban(dpnid);
      }
      break;
    case IDC_MP_MUTE:
      {
        // check if we have the player currently muted
        // based on the result, mute or un-mute him
        CListBoxContainer *lbox = GetListBoxContainer(GetCtrl(IDC_MP_PLAYERS));
        if (!lbox) break;
        int sel = lbox->GetCurSel();
        if (sel < 0) break;
        int dpnid = lbox->GetValue(sel);
        if (dpnid != GetNetworkManager().GetPlayer())
        {
          const PlayerIdentity *ident = GetNetworkManager().FindIdentity(dpnid);
          if (ident)
          {
            GetNetworkManager().TogglePlayerMute(ident->xuid);
          }
        }
      }
      break;
    default:
      Display::OnButtonClicked(idc);
      break;
  }
}

#ifndef _XBOX

#ifdef _WIN32
static bool AddUser(RString userName, void *context)
{
  CListBoxContainer *list = reinterpret_cast<CListBoxContainer *>(context);
  list->AddString(userName);
  return false; // continue
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Login display

Control *DisplayLogin::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

#ifdef _WIN32
  if (idc == IDC_LOGIN_USER)
  {
    CListBoxContainer *list = GetListBoxContainer(ctrl);
    if (list)
    {
      ForEachUser(AddUser, list);
      list->SortItems();
      list->SetCurSel(0);
      for (int i=0; i<list->GetSize(); i++)
      {
        if (Glob.header.GetPlayerName() == list->GetText(i))
          list->SetCurSel(i);
      }
    }
  }
#endif
  return ctrl;
}
void DisplayLogin::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_LOGIN_NEW:
  case IDC_LOGIN_EDIT:
    Exit(idc);
    break;
  case IDC_OK:
  case IDC_CANCEL:
    {
      _exit = idc;
      bool canDestroy = CanDestroy();
      _exit = -1;
      if (!canDestroy) break;

      ControlObjectContainerAnim *ctrl =
        dynamic_cast<ControlObjectContainerAnim *>(GetCtrl(IDC_LOGIN_NOTEBOOK));
      if (ctrl)
      {
        _exitWhenClose = idc;
        ctrl->Close();
      }
      else Exit(idc);
    }
    break;
  case IDC_LOGIN_DELETE:
  {
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    CreateMsgBox(
      MB_BUTTON_OK | MB_BUTTON_CANCEL,
      LocalizeString(IDS_SURE),
      IDD_MSG_DELETEPLAYER, false, &buttonOK, &buttonCancel);
    break;
  }
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayLogin::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_LOGIN_USER:
    {
      IControl *button = GetCtrl(IDC_LOGIN_DELETE);
      if (button)
      {
        if (curSel < 0)
        {
          button->EnableCtrl(false);
          break;
        }
        CListBoxContainer *list = GetListBoxContainer(ctrl);
        RString name = list->GetText(curSel);
        // default user directory cannot be deleted
        bool enable = stricmp(GetUserRootDir(name), GetDefaultUserRootDir()) != 0;
        button->EnableCtrl(enable);
      }
    }
    break;
  default:
    Display::OnLBSelChanged(ctrl, curSel);
    break;
  }
}

void DisplayLogin::OnCtrlClosed(int idc)
{
  if (idc == IDC_LOGIN_NOTEBOOK)
    Exit(_exitWhenClose);
  else
    Display::OnCtrlClosed(idc);
}
/*!
\patch 5097 Date 12/6/2006 by Jirka
- Fixed: Default profile cannot be deleted nor renamed now
*/
void DisplayLogin::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
    case IDD_MSG_DELETEPLAYER:
      Display::OnChildDestroyed(idd, exit);
#ifndef _XBOX
      if (exit == IDC_OK)
      {
        CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_LOGIN_USER));
        if (!list) break;
        int index = list->GetCurSel();
        if (index >= 0)
        {
          const char *name = list->GetText(index);
          RString dir = GetUserRootDir(name);
          // default user directory cannot be deleted
          if (stricmp(dir, GetDefaultUserRootDir()) != 0)
          {
            if (DeleteDirectoryStructure(dir))
            {
              list->DeleteString(index);
              list->SetCurSel(0);
            }
            // TODO: else error message
          }
        }
      }
#endif
      break;
    default:
      Display::OnChildDestroyed(idd, exit);
      break;
  }
}

bool DisplayLogin::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

  if (_exit == IDC_OK)
  {
    CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_LOGIN_USER));
    if (!list) return false;
    if (list->GetSize() == 0 || list->GetCurSel() < 0)
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_EMPTY));
      return false;
    }
  }

  return true;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// character display
Control *DisplayCharacter::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_MAIN_PLAYER_NAME:
    {
      // --- display character name
      ITextContainer *text = GetTextContainer(ctrl);
      char buffer[256];
      RString name = Glob.header.GetPlayerName();
# if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
        if (name.GetLength() == 0) name = LocalizeString(IDS_SILENT_SIGN_IN_NONE);
# endif
      sprintf(buffer, LocalizeString(IDS_MAIN_PLAYER), cc_cast(name));
      if (text) text->SetText(buffer);
      // ---
    }
    break;
  }

  return ctrl;
};
void DisplayCharacter::OnButtonClicked(int idc)
{
  switch(idc)
  {
	case IDC_CHAR_DEFAULT:
		{
			CreateChild(new DisplayCharacterDefault(this, true));
		}
		break;
	case IDC_CANCEL:
		{
			Exit(idc);
		}
		break;
	default:
		break;
  };
}
/*
void DisplayCharacter::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_CHARACTER_DEFAULT:
    {
        _exit = IDC_OK;
        Exit(IDC_OK);
    }
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}
*/
// character display default
DisplayCharacterDefault::DisplayCharacterDefault(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayDefaultCharacter");
}
void DisplayCharacterDefault::OnButtonClicked(int idc)
{
  switch(idc)
  {
/*	case IDC_CHAR_CONFIRM_DEFAULT:
		{
			CreateChild(new DisplayCharacterConfirm(this, true));
		}
		break;
*/
	case IDC_CHAR_CONFIRM_RESTART:
		{
			CreateChild(new DisplayCharacterRestart(this, true));
		}
		break;
	case IDC_OK:
		{
			Exit(idc);
		}
	case IDC_CANCEL:
		{
			Exit(idc);
		}
		break;
	default:
		break;
  };
}

Control *DisplayCharacterDefault::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  // --- display character name
  IControl *textCtrl = GetCtrl(IDC_MAIN_PLAYER_NAME);
  ITextContainer *text = GetTextContainer(textCtrl);
  char buffer[256];
  RString name = Glob.header.GetPlayerName();
# if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
    if (name.GetLength() == 0) name = LocalizeString(IDS_SILENT_SIGN_IN_NONE);
# endif
  sprintf(buffer, LocalizeString(IDS_MAIN_PLAYER), cc_cast(name));
  if (text) text->SetText(buffer);
  // ---

  return ctrl;
};

DisplayCharacterConfirm::DisplayCharacterConfirm(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscConfirmDefaultSurvivor");
}

DisplayCharacterRestart::DisplayCharacterRestart(ControlsContainer *parent, bool enableSimulation)
  : Display(parent)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscConfirmKillSurvivor");
}

#ifndef _XBOX
///////////////////////////////////////////////////////////////////////////////
// ModLauncher display

int DisplayModLauncher::ModIndex(const ModInfo &mod, const AutoArray<ModInfo> &modList)
{
  for (int i=0; i<modList.Size(); i++)
  {
    const ModInfo &modi = modList[i];
    if (modi.origin==ModInfo::ModNotFound) continue; // possibly some default mods can be "active" even it does not exist
    bool equalModDirs = !stricmp(mod.modDir,modi.modDir);
    if (   ( !modi.fullPath.IsEmpty() && !stricmp(modi.fullPath, mod.fullPath) ) //fullPaths present and equal
      || ( modi.origin==ModInfo::ModInConfig && mod.origin==ModInfo::ModInConfig && equalModDirs ) //config mods with equal modDir
      || ( (mod.origin==ModInfo::ModInRegistry || modi.origin==ModInfo::ModInRegistry) && equalModDirs ) //mod in registry should have unique modDir name
      )
    {
      return i; //mod found
    }
  }
  return -1;
}

// Add only mods which are not added yet
void DisplayModLauncher::Add(ModInfo mod)
{
  if ( ModIndex(mod, _modList)>=0 ) return; //duplicate, no add
  _modList.Add(mod);
}

static char *modSubdirs[] = {"dta", "addons", "common", "bin"};
static bool CheckModDirectory(const FileItem &file, DisplayModLauncher &ctx)
{
  // Mod is a directory with at least one of the following directories
  //     dta, addons, campaign, bin 
  if (file.directory)
  {
    for (int i=0; i<sizeof(modSubdirs)/sizeof(*modSubdirs); i++)
    {
      if ( stricmp(file.filename,modSubdirs[i])==0 )
      {
        // parent dir is mod
        RString fullPath = file.path.Substring(0, file.path.GetLength()-1); //without trailing backslash
        if ( !fullPath.IsEmpty() && strcmp(fullPath,".")!=0 ) //not root dir
        {
          ModInfo mod;
          mod.disabled = true; 
          mod.origin = ctx.curCollectMod;
          mod.fullPath = fullPath;

          // use lastdir as modDir
          mod.modDir = fullPath;
          const char *lastDir = strrchr(mod.modDir, '\\');
          if (lastDir!=NULL) mod.modDir = lastDir+1;
          lastDir = strrchr(mod.modDir, '/');
          if (lastDir!=NULL) mod.modDir = lastDir+1;

          { // try to find the name in mod dir by parsing the possible mod.cpp
            RString modFilePath = mod.fullPath + "\\mod.cpp";
            ParseModConfig(modFilePath, mod);
          }
          if (!mod.hide)
            ctx.Add(mod); //add only mods which are not disabled in its mod.cpp by hide=true;
          return true; //do not recurse other subdirs
        }
      }
    }
  }
  return false; //continue
}

// directories to be skipped in recursive collecting
static char * skipDirs[] = {"saved", "missions", "mpmissions", "addons", "common", "campaign", "templates", "mptemplates"};
static bool CollectModDirs(const FileItem &file, DisplayModLauncher &ctx)
{
  if (file.directory)
  {
    for (int i=0; i<sizeof(skipDirs)/sizeof(*skipDirs); i++)
    {
      if ( !stricmp(file.filename, skipDirs[i]) ) return false; //continue (skip this one)
    }
    if ( !ForEachFile(file.path+file.filename+RString("\\"), CheckModDirectory, ctx) )
    { // this directory was not MOD, recurse collecting into subdirectories
      ForEachFile(file.path+file.filename+RString("\\"), CollectModDirs, ctx);
    }
  }
  return false;
}

//! find mod callback function (used not to Add(mod) twice for the same mod)
static bool FindActiveModCallback(ModInfo *mod, RString dir)
{
  if (mod && (mod->origin!=ModInfo::ModNotFound || (mod->active && mod->defaultMod) )
      && stricmp(mod->modDir,dir)==0 ) 
    return true;
  return false;
}

// Fill in the _modList with all mods available
// It is sorted in the following manner:
//  1. mods defined in config (in the order defined in config)
//  2. active modes, sorted in the order of their initialization
//  3. nonactive modes found in the game directory, lexicographic order
//  4. nonactive modes found in the my documents folder, lexicographic order
void DisplayModLauncher::ReloadModList()
{
  _modList.Resize(0);
  if (GModInfos.modsReadOnly)
  {
    // add all active mods
    for (int i=GModInfos.Size()-1; i>=0; i--)
    {
      Add(GModInfos[i]);
    }
    // when -mod=xxx commandline was used, we are over, no other mods are to be listed
  }
  else // if (!GModInfos.modsReadOnly)
  { // list other mods otherwise
    // show mods from GModInfosProfile as enabled 
    // But it is useful when ModLauncher was canceled with "Restart Later" option
    for (int i=GModInfosProfile.Size()-1; i>=0; i--)
    {
      if ( GModInfosProfile[i].origin != ModInfo::ModNotFound || (GModInfosProfile[i].active && GModInfosProfile[i].defaultMod) )
        Add(GModInfosProfile[i]);
    }
    // add mods Read from registry
    for (int i=GModInfosRegistry.Size()-1; i>=0; i--)
    {
      Add(GModInfosRegistry[i]);
    }
    // add mods configured in CfgMods
    ParamEntryVal mods = Pars>>"CfgMods";
    if (mods.IsClass())
    {
      ConstParamEntryPtr entry = mods.FindEntry("defaultAction");
      if (entry) _defaultAction = entry->GetValue();
      for (int i=0; i<mods.GetEntryCount(); i++)
      {
        ParamEntryVal modClass = mods.GetEntry(i);
        if ( modClass.IsClass() )
        {
          if (!modClass.FindEntry("name")) 
            continue; //when no name is present, the mod is not installed, not even LITE 
          ModInfo mod;
          mod.parsed = true;
          mod.name = modClass >> "name";
          mod.action = modClass >> "action";
          entry = modClass.FindEntry("actionName");
          if (entry) mod.actionName = *entry;
          mod.disabled = true;
          mod.origin = ModInfo::ModInConfig;
          mod.modDir = modClass >> "dir";
          mod.picture = modClass >> "picture";
          entry = modClass.FindEntry("default");
          if (entry) 
          {
            mod.defaultMod = *entry;
            if (mod.defaultMod) 
            {
              mod.active = true;
              mod.cannotDisable = true;
              mod.disabled = false;
            }
          }
          if (!ForEachModDirectory(FindActiveModCallback, mod.modDir))
          { //mod is not active (i.e. not listed yet), we can add it as inactive mod
            Add(mod);
          }
        }
      }
    }
    // modes found in the game directory
    curCollectMod = ModInfo::ModGameDir;
    int size = GetCurrentDirectory(0, NULL);
    GetCurrentDirectory(size, curRootDir.CreateBuffer(size));
    if (curRootDir[size - 1] != '\\') curRootDir = curRootDir + RString("\\");
    ForEachFile(curRootDir, CollectModDirs, *this);
    // modes found in the my documents user profile directory
    curCollectMod = ModInfo::ModOtherDir;
    curRootDir = GetDefaultUserRootDir()+RString("\\");
    ForEachFile(curRootDir, CollectModDirs, *this);
    // parse possible 'not parsed yet' mod.cpp files
    for (int i=0; i<_modList.Size(); i++)
    {
      ModInfo &mod = _modList[i];
      if (!mod.parsed)
      { // try to parse it
        mod.parsed = true;
        { // try to find the name in mod dir by parsing the possible mod.cpp
          RString dir = mod.modDir;
          if (mod.origin==ModInfo::ModOtherDir) dir = GetDefaultUserRootDir() + RString("\\") + dir;
          RString modFilePath = dir + "\\mod.cpp";
          ParseModConfig(modFilePath, mod);
        }
      }
      for (int j=i+1; j<_modList.Size(); j++)
      {
        if ( stricmp(mod.modDir, _modList[j].modDir)==0 )
        { // two mods with the same modDir but different origin
          if (mod.active) 
          {
            if ( (mod.origin!=_modList[j].origin) && (_modList[j].origin==ModInfo::ModOtherDir) )
            { // second mod is in my document directory
              _modList[j].cannotChange=true;
              _modList[j].active=true;
              _modList[j].disabled=false; //show it as active
            }
          }
        }
      }
    }
  }
}

static const EnumName ModOriginNames[]=
{
  EnumName(ModInfo::ModInConfig, "CONFIG"),
  EnumName(ModInfo::ModGameDir, "GAME DIR"),
  EnumName(ModInfo::ModOtherDir, "MY DOCUMENTS DIR"),
  EnumName(ModInfo::ModInRegistry, "REGISTRY"),
  EnumName(ModInfo::ModNotFound, "NOT FOUND"),
  EnumName(ModInfo::ModDefault, "DEFAULT"),
  EnumName()
};
template<>
const EnumName *GetEnumNames(ModInfo::ModOrigin dummy)
{
  return ModOriginNames;
}

void DisplayModLauncher::SaveModList()
{
  ParamFile fCfg;
  void ParseFlashpointCfg(ParamFile &file);
  ParseFlashpointCfg(fCfg);
  fCfg.Delete("ModLauncherList");
  ParamClassPtr modsCfg = fCfg.AddClass("ModLauncherList");
  AutoArray<RString> disabledMods; //will be collected from _modListContainer
  for (int i=0, modNum=1; i<_modListContainer->Size(); i++)
  {
    const ModInfo &mod=_modList[_modListContainer->GetValue(i)];
    if (mod.origin==ModInfo::ModInRegistry && mod.disabled)
    {
      disabledMods.Add(mod.modDir);
      continue;
    }
    if (mod.disabled) continue; //save only enabled mods
    if (mod.defaultMod) continue; // do not save default mods (includes beta ones)
    RString modLabel = Format("Mod%d", modNum++); //start from 1
    ParamClassPtr modCfg = modsCfg->AddClass(modLabel);
    modCfg->Add("dir", mod.modDir);
    modCfg->Add("name", mod.name);
    ModInfo::ModOrigin origin =  ( mod.origin==ModInfo::ModNotFound && mod.defaultMod ) ? ModInfo::ModDefault : mod.origin;
    const EnumName *names = GetEnumNames(origin);
    RString modOrigin = GetEnumName(names, origin);
    modCfg->Add("origin", modOrigin);
    if ( !mod.fullPath.IsEmpty() )
    {
      modCfg->Add("fullPath", mod.fullPath);
    }
  }
  // save possible disabled Registry mods
  if (disabledMods.Size())
  {
    ParamEntryPtr disabledAr = modsCfg->AddArray("disabled");
    for (int i=0; i<disabledMods.Size(); i++)
    {
      disabledAr->AddValue(disabledMods[i]);
    }
  }
  void SaveFlashpointCfg(ParamFile &file);
  SaveFlashpointCfg(fCfg);
}

struct CompareModItems
{
  CListBoxContainer *_modListContainer;
  CompareModItems(CListBoxContainer *modListContainer) { _modListContainer=modListContainer; }
  int operator() ( const CListBoxItem *str0, const CListBoxItem *str1 ) const
  {
    return stricmp(str0->text, str1->text);
  }
};

Control *DisplayModLauncher::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch(idc)
  {
    case IDC_MOD_LAUNCHER_MODS:
      {
        _iconActive = GlobLoadTextureUI(cls >> "active");
        _iconEnabled = GlobLoadTextureUI(cls >> "enabled");
        _iconDisabled = GlobLoadTextureUI(cls >> "disabled");
        _modListContainer = GetListBoxContainer(ctrl);
        if (_modListContainer)
        {
          ReloadModList();
          int firstSortableIx = -1;
          for (int i=0; i<_modList.Size(); i++)
          {
            _modListContainer->AddString(_modList[i].GetName());
            if (_modList[i].disabled) _modListContainer->SetTexture(i, _iconDisabled);
            else if (_modList[i].active) _modListContainer->SetTexture(i, _iconActive);
            else _modListContainer->SetTexture(i, _iconEnabled);
            _modListContainer->SetValue(i, i); //it should be always index to the _modList, even after any Sorting
            if (firstSortableIx==-1 && _modList[i].origin!=ModInfo::ModInConfig && _modList[i].disabled) firstSortableIx = i;
          }
          if (firstSortableIx>=0)
          {
            //keep the same item selected after sort
            CompareModItems compareModItemsFunc(_modListContainer);
            QSort(_modListContainer->Data()+firstSortableIx, _modListContainer->Size()-firstSortableIx, compareModItemsFunc);
          }
        }
      }
      break;
    case IDC_MOD_LAUNCHER_PICTURE:
      {
        _picture = dynamic_cast<CStatic *>(ctrl);
        _picture->SetTexture(NULL);
      }
      break;
    case IDC_MOD_LAUNCHER_ACTION:
      {
        ITextContainer *text = GetTextContainer(ctrl);
        if (text)
        {
          _defaultActionText = text->GetText();
        }
      }
      break;
    case IDC_MOD_LAUNCHER_DISABLE:
    case IDC_MOD_LAUNCHER_DOWN:
    case IDC_MOD_LAUNCHER_UP:
      if (GModInfos.modsReadOnly) ctrl->EnableCtrl(false); //disable when mods are forced by command line (Read Only mode)
      break;
  }
  return ctrl;
}

//disable all manipulating buttons
void DisplayModLauncher::DisableButtons()
{
  IControl *ctrl = GetCtrl(IDC_MOD_LAUNCHER_UP);
  if (ctrl) ctrl->EnableCtrl(false);
  ctrl = GetCtrl(IDC_MOD_LAUNCHER_DOWN);
  if (ctrl) ctrl->EnableCtrl(false);
  ctrl = GetCtrl(IDC_MOD_LAUNCHER_DISABLE);
  if (ctrl) ctrl->EnableCtrl(false);
}

void DisplayModLauncher::RefreshButtons()
{
  int curSel = GetListBoxContainer(GetCtrl(IDC_MOD_LAUNCHER_MODS))->GetCurSel();
  RString strEnable = LocalizeString(IDS_ENABLE_CONTROLLER);
  RString strDisable = LocalizeString(IDS_DISABLE_CONTROLLER);
  if ( curSel>=0 && curSel<_modListContainer->Size() )
  { 
    const ModInfo &mod = _modList[_modListContainer->GetValue(curSel)];
    // IDC_MOD_LAUNCHER_DISABLE button text (we want to set it to proper value also when displayModLauncher is just loaded)
    IControl *disableCtrl = GetCtrl(IDC_MOD_LAUNCHER_DISABLE);
    ITextContainer *text = GetTextContainer(disableCtrl);
    if (text)
    {
      if (mod.disabled) text->SetText(strEnable);
      else text->SetText(strDisable);
    }
    if (!GModInfos.modsReadOnly && !mod.cannotChange)
    {
      // IDC_MOD_LAUNCHER_DISABLE button maintenance
      if (disableCtrl) disableCtrl->EnableCtrl(!mod.defaultMod && (!mod.cannotDisable || mod.disabled));
      // IDC_MOD_LAUNCHER_UP, IDC_MOD_LAUNCHER_DOWN buttons maintenance
      bool canBeMovedUP = false;
      if (curSel>0 && !mod.disabled && !mod.defaultMod) //default mods cannot be moved
      {
        const ModInfo &modUp = _modList[_modListContainer->GetValue(curSel-1)];
        canBeMovedUP = !(mod > modUp); // meaning mod must be after the modUp
      }
      IControl *ctrl = GetCtrl(IDC_MOD_LAUNCHER_UP);
      if (ctrl) ctrl->EnableCtrl(canBeMovedUP); //enable when enabled and not on the top of the list

      bool canBeMovedDOWN = false;
      if ( (curSel<_modListContainer->Size()-1) )
      {
        const ModInfo &modDown = _modList[_modListContainer->GetValue(curSel+1)];
        if ( !modDown.disabled && !modDown.defaultMod && !mod.defaultMod)
        {
          canBeMovedDOWN = !(modDown > mod); // meaning modDown must be after the mod
        }
      }
      ctrl = GetCtrl(IDC_MOD_LAUNCHER_DOWN);
      if (ctrl) ctrl->EnableCtrl(canBeMovedDOWN); //enable when not on the bottom and mod bellow is enabled
    }
    else DisableButtons();
    //Actualize Action Button content
    IControl *ctrl = GetCtrl(IDC_MOD_LAUNCHER_ACTION);
    text = GetTextContainer(ctrl);
    if (text)
    {
      if (mod.actionName.IsEmpty())
      {
        text->SetText(_defaultActionText);
      }
      else
      {
        text->SetText( mod.actionName.Substring(0,std::min(8,mod.actionName.GetLength())) );
      }
    }
  }
}

void DisplayModLauncher::SwapItems(int curSel,int curSel2,bool changeCurSel)
{
  CListBoxItem item = _modListContainer->Get(curSel);
  _modListContainer->Set(curSel) = _modListContainer->Get(curSel2);
  _modListContainer->Set(curSel2) = item;
  if (changeCurSel) 
    _modListContainer->SetCurSel(curSel2);
}

#define LAUNCH_THROUGH_URL_FILE 0
static RString CreateWebsiteButtonURLFile(RString url)
{
#ifdef _WIN32
  char dir[MAX_PATH];
  if (GetTempPath(MAX_PATH, dir) == 0) return RString();
  RString path;
  for (int i=0; i<100000; i++)
  {
    path = Format("%s%s %d.url", dir, AppName, i);
    if (!QIFileFunctions::FileExists(path)) break;
  }

  QOFStream out;
  out.open(path);
  out << Format( "[InternetShortcut]\r\nURL=%s\r\n", cc_cast(url) );
  out.close();
  return path;
#else
  return RString();
#endif
}

class OpenURLThread: public MultiThread::ThreadBase
{
  RString _url;
public:
  OpenURLThread(RString url) 
    : MultiThread::ThreadBase(MultiThread::ThreadBase::PriorityNormal, 16*1024), 
    _url(url) 
  {}
  unsigned long Run()
  {
#ifdef _WIN32
  #define USE_TEMPORARY_URI_FILE 0
  #if !USE_TEMPORARY_URI_FILE
    RString cmd = Format("url.dll,FileProtocolHandler %s", cc_cast(_url));
    ShellExecute(NULL, "open", "rundll32.exe", cmd, NULL, SW_SHOWNORMAL);
  #else
    SHELLEXECUTEINFO sei;
    sei.cbSize = sizeof(SHELLEXECUTEINFO);
    sei.fMask = NULL;
    sei.hwnd = NULL;
    sei.lpVerb = "open";
    sei.lpFile = cc_cast(_url);
    sei.lpParameters= NULL;
    sei.nShow = SW_SHOWNORMAL;
    sei.hInstApp = NULL;
    sei.lpIDList = NULL;
    sei.lpClass = NULL;
    sei.hkeyClass = NULL;
    sei.dwHotKey = NULL;
    sei.hIcon = NULL;
    sei.hProcess = NULL;
    sei.lpDirectory = "C:\\";

    //CoInitialize(NULL);
    ::ShellExecuteEx(&sei);
    //CoUnInitialize();a
#endif
#endif
    delete this;
    return 0;
  }
};

void DisplayModLauncher::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_MOD_LAUNCHER_ACTION:
    {
#ifdef _WIN32
      int curSel = _modListContainer->GetCurSel();
      if ( curSel>=0 && curSel<_modListContainer->Size() )
      { 
        ModInfo &mod = _modList[_modListContainer->GetValue(curSel)];
        RString action = mod.action.IsEmpty() ? _defaultAction : mod.action;
        if ( strnicmp("http://", action, 7)==0 || strnicmp("https://", action, 8)==0 )
        {
          // ShellExecute(NULL, NULL, action, NULL, NULL, SW_NORMAL);
          // ShellExecute does not work sometimes: 
          //    First-chance exception at 0x7c812afb in ArmA2ExpInt.exe: 0x80040155: Interface not registered.
          // One possible solution was found on http://www.indiegamer.com/forums/showthread.php?t=5915
          // so let us create temporary url file and open it using ShellExecute
#if USE_TEMPORARY_URI_FILE
          RString path = CreateWebsiteButtonURLFile(action);
          if (!path.IsEmpty())
          {
            MultiThread::Thread *thr=new MultiThread::ThreadDyn(new OpenURLThread(path)); //the thread is destroyed automatically after its end
            thr->Start();     //run
          }
#else
          MultiThread::Thread *thr=new MultiThread::ThreadDyn(new OpenURLThread(action)); //the thread is destroyed automatically after its end
          thr->Start();     //run
#endif
        }
      }
#endif
    }
    break;
  case IDC_MOD_LAUNCHER_UP:
  case IDC_MOD_LAUNCHER_DOWN:
    {
      int curSel2, curSel = _modListContainer->GetCurSel();
      if (idc==IDC_MOD_LAUNCHER_DOWN) curSel2 = curSel + 1;
      else curSel2 = curSel - 1;
      // swap the curSel and curSel2 items content
      SwapItems(curSel, curSel2, true);
    }
    break;
  case IDC_OK:
    {
      // Detect any changes to be saved to flashpoint cfg and possibly restart the game.
      // compare with GModInfosProfile, if it differs in editable mods, restart is needed
      bool modsMatch = !GModInfosProfile.safeModsActivated;
      if (!GModInfosProfile.safeModsActivated && !GModInfos.modsReadOnly)
      {
        int j=GModInfosProfile.Size()-1;
        for (int i=0; i<_modListContainer->Size(); i++)
        {
          ModInfo &mod = _modList[_modListContainer->GetValue(i)];
          if ( mod.disabled || (mod.defaultMod && mod.origin==ModInfo::ModInConfig) ) continue; //checking only editable enabled mods
          modsMatch = false;
          for (; j>=0; j--)
          {
            // search in mods loaded from flashpoint cfg, keep order
            ModInfo &modProfile = GModInfosProfile[j];
            if ( modProfile.origin == ModInfo::ModNotFound  ) continue;
            if ( stricmp(modProfile.modDir, mod.modDir)==0 )
            {
              if (modProfile.disabled==mod.disabled)
              {
                modsMatch = true; j--;
                break;
              }
              else
              {
                if (!modProfile.disabled) break; //enabled mod was disabled, lists differ!
              }
            }
            else
            { //modProfile is different mod, if disabled we do not care
              if (modProfile.disabled) continue;
              else
              {
                // enabled mod not in the proper place in current _modListContainer, lists differ!
                break;
              }
            }
          }
          if ( !modsMatch ) break;
        }
        // there must not be any other enabled mods inside GModInfosProfile
        if (modsMatch) for (; j>=0; j--)
        {
          // search in mods loaded from flashpoint cfg, keep order
          ModInfo &modProfile = GModInfosProfile[j];
          if (modProfile.origin == ModInfo::ModNotFound) continue; //this mod is some relict
          if (!modProfile.disabled && modProfile.origin!=ModInfo::ModInConfig)
          {
            modsMatch = false;
            break;
          }
        }
      }
      if (!modsMatch)
      {
        CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL | MB_BUTTON_USER, LocalizeString(IDS_MSG_RESTART_NEEDED), IDD_MSG_RESTART_NEEDED);
        break;
      }
      // Save the changes (there can be only changes caused by deleted mods directories)
      SaveModList();
      // behave as the IDC_CANCEL was triggered
    }
  case IDC_CANCEL:
    {
      bool canDestroy = CanDestroy();
      if (!canDestroy) break;
      _exit = -1;
      Exit(idc);
    }
    break;
  case IDC_MOD_LAUNCHER_DISABLE:
    {
      ChangeModStatus();
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

// returns the highest index in (0..modIx) range which matches the LOADAFTER conditions
int DisplayModLauncher::SortModIn(int modIx)
{
  ModInfos activeMods;
  for (int i=0; i<=modIx; i++)
  {
    _modList[_modListContainer->GetValue(i)].modLauncherIx = i;
    activeMods.Add(_modList[_modListContainer->GetValue(i)]);
  }
  int count = activeMods.Size(); // use count instead of modIx+1, as possible duplicates would cause crash (there should not be duplicates!)
  Assert(count==modIx+1);

  // sort active mods 
  activeMods.SortByLoadAfterDependencies();

  AutoArray<CListBoxItem, MemAllocLocal<CListBoxItem,20> > helper;
  helper.Realloc(count);helper.Resize(count);
  int newModIx = modIx;
  for (int i=0; i<count; i++)
  {
    int ix = activeMods[i].modLauncherIx;
    helper[i]=_modListContainer->Get(ix);
    if (ix==modIx) newModIx = i;
  }
  for (int i=0; i<count; i++)
  {
    _modListContainer->Set(i) = helper[i];
  }
  return newModIx;
}

int DisplayModLauncher::EnableMod(int curSel, bool enable)
{
  if (_modListContainer)
  {
    if ( curSel>=0 && curSel<_modListContainer->Size() )
    { 
      ModInfo &mod = _modList[_modListContainer->GetValue(curSel)];
      if ( !mod.defaultMod ) 
      {
        if (mod.disabled && enable)
        {
          mod.disabled = false;
          if (mod.active)
            _modListContainer->SetTexture(curSel, _iconActive);
          else
            _modListContainer->SetTexture(curSel, _iconEnabled);
          // move the mod to the last position in the list of enabled mods
          int curSel2 = curSel;
          for (int i=0; i<_modListContainer->Size(); i++) 
          {
            ModInfo &modi = _modList[_modListContainer->GetValue(i)];
            if (modi.disabled || modi.defaultMod) { curSel2 = i; break; }
          }
          if (curSel2<curSel) 
          {
            for (int i=curSel; i>curSel2; i--) SwapItems(i,i-1,false);
            _modListContainer->SetCurSel(curSel2);
            curSel = curSel2;
          }
          curSel = SortModIn(curSel);
          _modListContainer->SetCurSel(curSel);
        }
        else if (!mod.disabled && !enable)
        {
          mod.disabled = true;
          _modListContainer->SetTexture(curSel, _iconDisabled);
          int curSel2 = curSel;
          for (int i=curSel+1; i<_modListContainer->Size(); i++)
          {
            if (_modList[_modListContainer->GetValue(i)].disabled) break;
            curSel2 = i;
            SwapItems(i-1,i,false);
          }
          //keep the same item selected after sort
          int ixToSel = _modListContainer->GetValue(curSel2);
          CompareModItems compareModItemsFunc(_modListContainer);
          QSort(_modListContainer->Data()+curSel2, _modListContainer->Size()-curSel2, compareModItemsFunc);
          for (int i=0; i<_modListContainer->Size(); i++)
          {
            if (_modListContainer->GetValue(i)==ixToSel)
            {
              _modListContainer->SetCurSel(i);
              break;
            }
          }
        }
      }
    }
  }
  return curSel;
}

void DisplayModLauncher::EnableMod(RString modRegName, bool enable)
{
  // find this mod ix
  for (int i=0; i<_modListContainer->Size(); i++)
  {
    ModInfo &mod = _modList[_modListContainer->GetValue(i)];
    if ( stricmp(mod.regName, modRegName)==0 )
    {
      EnableMod(i, enable);
      return;
    }
  }
}

void DisplayModLauncher::ChangeModStatus()
{
  if (_modListContainer)
  {
    int curSel = _modListContainer->GetCurSel();
    if ( curSel>=0 && curSel<_modListContainer->Size() )
    { 
      int myIx = _modListContainer->GetValue(curSel);
      ModInfo &mod = _modList[_modListContainer->GetValue(curSel)];
      if ( !mod.defaultMod ) 
      {
        if (mod.disabled)
        {
          EnableMod(curSel, true);
          // we should enable all dependant, required mods too
          for (int i=0; i<mod.require.Size(); i++)
          {
            EnableMod(mod.require[i], true);
          }
        }
        else if (!mod.cannotDisable)
        {
          curSel = EnableMod(curSel, false);
          // we should disable all dependant, requiredBy mods too
          for (int i=0; i<mod.requiredBy.Size(); i++)
          {
            EnableMod(mod.requiredBy[i], false);
          }
        }
        // set current selection to the mod enabled by click
        for (int i=0; i<_modListContainer->Size(); i++)
        {
          if (_modListContainer->GetValue(i)==myIx)
          {
            _modListContainer->SetCurSel(i);
            break;
          }
        }
        RefreshButtons();
      }
    }
  }
}

RString GetModPicturePath(ModInfo &mod)
{
  if (!mod.picture.IsEmpty() )
  { //show picture
    RString picturePath = mod.fullPath + RString("\\") + mod.picture;
    if (!QIFileFunctions::FileExists(picturePath))
    {
      picturePath = FindPicture(mod.picture);
    }
    picturePath.Lower();
    return picturePath;
  }
  else
  {
    static const char *pictureFiles[] = {"mod.paa", "mod.jpg"};
    for (int i=0; i<sizeof(pictureFiles)/sizeof(*pictureFiles); i++)
    {
      RString picturePath = mod.fullPath + RString("\\") + pictureFiles[i];
      if (QIFileFunctions::FileExists(picturePath))
      {
        mod.picture = pictureFiles[i];
        picturePath.Lower();
        return picturePath;
      }
    }
  }
  return RString();
}

void DisplayModLauncher::OnLBSelChanged(IControl *ctrl, int curSel)
{
  switch (ctrl->IDC())
  {
  case IDC_MOD_LAUNCHER_MODS:
    {
      // Disable, Up and Down buttons are enabled only for some mods
      RefreshButtons();
      // show possible mod picture
      Ref<Texture> texture = NULL;
      if ( curSel>=0 && curSel<_modListContainer->Size())
      {
        ModInfo &mod = _modList[_modListContainer->GetValue(curSel)];
        RString picturePath = GetModPicturePath(mod);
        if ( !picturePath.IsEmpty() ) 
          texture = GlobLoadTextureUI(picturePath);
      }
      _picture->SetTexture(texture);
    }
    break;
  default:
    Display::OnLBSelChanged(ctrl, curSel);
    break;
  }
}

void DisplayModLauncher::OnLBDblClick(int idc, int curSel)
{
  if (idc==IDC_MOD_LAUNCHER_MODS && !GModInfos.modsReadOnly)
  {
    ChangeModStatus();
  }
  else Display::OnLBDblClick(idc, curSel);
}

void DisplayModLauncher::OnCtrlClosed(int idc)
{
//   if (idc == IDC_ModLauncher_NOTEBOOK)
//     Exit(_exitWhenClose);
//   else
    Display::OnCtrlClosed(idc);
}

/*!
\patch 5097 Date 12/6/2006 by Jirka
- Fixed: Default profile cannot be deleted nor renamed now
*/

void DisplayModLauncher::OnChildDestroyed(int idd, int exit)
  {
  switch (idd)
  {
  case IDD_MSG_RESTART_NEEDED:
    {
      if (exit==IDC_CANCEL) break; //cancel
      else if (exit==IDC_USER_BUTTON)
      {
        // restart later
        // Save the changes
        SaveModList();
        // update GModInfosProfile (as when user returns to ModLauncher, she could see changed content)
        GModInfosProfile.Clear();
        for (int i=_modListContainer->Size()-1; i>=0; i--) // reverse order
        {
          ModInfo &mod = _modList[_modListContainer->GetValue(i)];
          if ( mod.disabled ) continue; //saving only editable enabled mods
          GModInfosProfile.Add(mod);
        }
        _exit = IDC_OK;
        Exit(IDC_OK);
      }
      else
      {
        // Save the changes
        SaveModList();
        // Restart the game
        TCHAR exeName[MAX_PATH];
        GetModuleFileName(0, exeName, MAX_PATH);
        extern RString GCmdLine;
        RString execute = RString(exeName) + RString(" ") + GCmdLine;
#ifdef _WIN32
        WinExec(execute, SW_SHOW); //restart
#endif
        _exit = IDC_MAIN_QUIT;
        Exit(IDC_MAIN_QUIT);
      }
    }
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

bool DisplayModLauncher::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

  if (_exit == IDC_OK)
  {
    CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_MOD_LAUNCHER_MODS));
    if (!list) return false;
  }

  return true;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// New user display

CHead::CHead(ControlsContainer *parent, int idc, ParamEntryPar cls)
: ControlObject(parent, idc, cls)
{
  _lastSimulation = Glob.uiTime;
  _headIndex = SkeletonIndexNone;
  _faceType = cls >> "faceType";

#if _ENABLE_NEWHEAD
  // bone indices
  _boneHead = cls >> "boneHead";
  _boneLEye = cls >> "boneLEye";
  _boneREye = cls >> "boneREye";
  _boneLEyelidUp = cls >> "boneLEyelidUp";
  _boneREyelidUp = cls >> "boneREyelidUp";
  _boneLEyelidDown = cls >> "boneLEyelidDown";
  _boneREyelidDown = cls >> "boneREyelidDown";
  _boneLPupil = cls >> "boneLPupil";
  _boneRPupil = cls >> "boneRPupil";
#else
  _headType = new HeadTypeOld();
  _headType->Load(Pars >> "CfgMimics" >> "HeadPreview");
#endif
  if (!_shape->LoadSkeletonFromSource())
  {
    _shape->SetAnimationType(AnimTypeNone);
  }
  else
  {
    _shape->SetAnimationType(AnimTypeHardware);
  }

#if _ENABLE_NEWHEAD
  // _head = new Head();
#else
  _headType->InitShape(cls, _shape);
  _head = new HeadOld(*_headType, _shape, _faceType);
#endif

  _rotate = true;
  _animSpeed = 1.0;

  ConstParamEntryPtr entry = cls.FindEntry("animation");
  if (entry)
  {
    AnimationRTName name;
    name.name = GetAnimationName(*entry);
    name.skeleton = _shape->LoadSkeletonFromSource();
    name.reversed = false;
    name.streamingEnabled = false;
    if (name.skeleton)
    {
      _animation = new AnimationRT(name);
      _animation->SetLooped(true);
    }
   
    _animSpeed = cls >> "animSpeed";
  }
  _animStart = Glob.uiTime;

  _shape->AddLoadHandler(this);
}

CHead::~CHead()
{
  _shape->RemoveLoadHandler(this);

#if _ENABLE_NEWHEAD
  RemoveHead();
  RemoveGlasses();
#else
  _headType.Free();
#endif
  _shape.Free();
}

void CHead::LODShapeLoaded(LODShape *shape)
{
#if _ENABLE_NEWHEAD
  _proxies.Init(shape);

  // Remember bone indices
  if (shape->GetSkeleton())
  {
    _head._headBone = shape->GetSkeleton()->FindBone(_boneHead);
    _head._lEye = shape->GetSkeleton()->FindBone(_boneLEye);
    _head._rEye = shape->GetSkeleton()->FindBone(_boneREye);
    _head._lEyelidUp = shape->GetSkeleton()->FindBone(_boneLEyelidUp);
    _head._rEyelidUp = shape->GetSkeleton()->FindBone(_boneREyelidUp);
    _head._lEyelidDown = shape->GetSkeleton()->FindBone(_boneLEyelidDown);
    _head._rEyelidDown = shape->GetSkeleton()->FindBone(_boneREyelidDown);
    _head._lPupil = shape->GetSkeleton()->FindBone(_boneLPupil);
    _head._rPupil = shape->GetSkeleton()->FindBone(_boneRPupil);
  }
#endif
}

void CHead::LODShapeUnloaded(LODShape *shape)
{
#if _ENABLE_NEWHEAD
  _proxies.Deinit(shape);
#endif
}

void CHead::ShapeLoaded(LODShape *shape, int level)
{
#if _ENABLE_NEWHEAD
  _proxies.InitLevel(shape, level);
#else
  if (shape->GetSkeleton())
  {
    if (_headType)
    {
      _headType->InitShapeLevel(shape,level);
      _headIndex = _shape->FindBone(_headType->_bone);
    }
  }
#endif
}

void CHead::ShapeUnloaded(LODShape *shape, int level)
{
#if _ENABLE_NEWHEAD
  _proxies.DeinitLevel(shape, level);
#else
  if (shape->GetSkeleton())
  {
    if (_headType)
    {
      _headType->DeinitShapeLevel(shape,level);
    }
  }
#endif
}

#if _ENABLE_NEWHEAD
void CHead::DrawProxy(int cb, const DrawParameters &dp, Object *object, float dist2, const Matrix4 &transform, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
                      , int drawListId
#endif
                      )
{
  int pLevel = GScene->LevelFromDistance2(object->GetShape(), dist2, transform.Scale());
  if (pLevel == LOD_INVISIBLE) return;

  FrameBase frame(transform, false);

  // Set UI specific flags
  int oldSpecial = object->GetShape()->Special();
  object->GetShape()->OrSpecial(BestMipmap | DisableSun);
  object->Draw(cb, pLevel, ClipAll, dp, InstanceParameters::_default, dist2, frame, oi
#if _HELISIM_RENDER_TO_TEXTURE
    , drawListId
#endif    
    );
  object->GetShape()->SetSpecial(oldSpecial);
}

void CHead::DrawProxies(
  int cb, const AnimationContext *animContext,
  int level, ClipFlags clipFlags,
  const Matrix4 &transform, float dist2, const DrawParameters &dp, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  int n = _proxies.GetProxyCountForLevel(_shape, level);
  for (int i=0; i<n; i++)
  {
    const ProxyObjectTyped &proxy = _proxies.GetProxyForLevel(_shape, level, i);
    // Retrieve the proxy object
    Object *proxyObj = proxy.obj;
    if (!proxyObj) continue;
    // Retrieve the proxy type
    const EntityType *proxyType = proxyObj->GetEntityType();
    if (!proxyType) continue;

    if (strcmp(proxyType->_simName, "ProxySubpart") == 0)
    {
      // head
      if (_headType)
      {
        HeadObject *object = _headType->_model;
        if (object)
        {
          // set correct face material and texture prior drawing
          object->_faceMaterial = _faceMaterial;
          object->_faceTexture = _faceTexture;

          Matrix4 pTransform = transform * proxyObj->RenderVisualState().Transform();
          // if shape is reversed, reverse orientation
          if (object->GetShape()->Remarks()&ShapeReversed)
          {
            Matrix4 swapM(MScale, -1, +1, -1);
            pTransform = pTransform * swapM;
          }

          DrawProxy(cb, dp, object, dist2, pTransform, oi
#if _HELISIM_RENDER_TO_TEXTURE
            , drawListId
#endif
            );
        }
      }
    }
  }
}
#endif

void CHead::OnDraw(float alpha)
{
  ControlObject::OnDraw(alpha);

#if _ENABLE_NEWHEAD
  const int level = 0;
  const float dist2 = 0.1f;

  ShapeUsed lock = _shape->Level(level);
  AnimationContextStorageRender storage;
  AnimationContext animContext(RenderVisualState(),storage);
  lock->InitAnimationContext(animContext, _shape->GetConvexComponents(level), false);
  Animate(animContext, level,true, this, dist2);

#if _HELISIM_RENDER_TO_TEXTURE
  DoAssert(CheckMainThread());
#endif

  DrawProxies(-1, &animContext, level, ClipAll, GetFrameBase(), dist2, DrawParameters::_noShadow, NULL
#if _HELISIM_RENDER_TO_TEXTURE
    , GScene->GetDrawObjectIndex()
#endif
  );
  Deanimate(level, true);
#endif
}

void CHead::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  ControlObject::Animate(animContext, level, setEngineStuff, parentObject, dist2);

  float phase = _animSpeed * (Glob.uiTime - _animStart);
  if (phase >= 1.0)
  {
    phase = 0;
    _animStart = Glob.uiTime;
  }
  if (_animation) _animation->Apply(animContext, _shape, level, phase);
#if !_ENABLE_NEWHEAD
  _head->Animate(*_headType, animContext, _shape, level, false, M3Identity, false);
#endif
}

void CHead::Deanimate(int level, bool setEngineStuff)
{
  ControlObject::Deanimate(level, setEngineStuff);

  // if (_animation) _animation->Apply(_shape, level, 0);
#if !_ENABLE_NEWHEAD
  _head->Deanimate(*_headType, _shape, level, false, M3Identity, false);
#endif
}

bool CHead::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
#if _ENABLE_NEWHEAD
  ControlObject::PrepareShapeDrawMatrices(matrices, vs, shape, level);

  // Set grimace
  if (_head._dstGrimace >= 0 && _head._srcGrimace >= 0)
  {
    if (_headType)
    {
      // Get the shape and make sure it already exists
      DoAssert(shape->IsLevelLocked(level));
      const Shape *shapeL = shape->GetLevelLocked(level);

      float eyeAccom = GScene->MainLight()->GetEyeAdaptMin();
      _headType->PrepareGrimaceMatrices(matrices, _head, _headType, shapeL, M4Identity, VForward, eyeAccom, _head._dstGrimace, _head._srcGrimace, _head._factorGrimace);
    }
  }
#else
  float phase = _animSpeed * (Glob.uiTime - _animStart);
  if (phase >= 1.0)
  {
    phase = 0;
    _animStart = Glob.uiTime;
  }

  // Get the shape and make sure it already exists
  DoAssert(_shape->IsLevelLocked(level));
  const Shape *shapeL = _shape->GetLevelLocked(level);

  if (_animation)
  {
    _animation->PrepareMatrices(matrices, phase, 1, shapeL,shape->GetSkeleton());
    const SubSkeletonIndexSet &ssis = shapeL->GetSubSkeletonIndexFromSkeletonIndex(_headIndex);
    if (ssis.Size() > 0)
    {
      _head->PrepareShapeDrawMatrices(*_headType, matrices, shape, level, false, matrices[GetSubSkeletonIndex(ssis[0])]);
      // No matrix filling, no bones replication here
    }
  }
  else
  {
    // Fill out matrices with identities in absence of animation
    matrices.Resize(shapeL->GetSubSkeletonSize());
    for (int i = 0; i < matrices.Size(); i++)
    {
      matrices[i] = M4Identity;
    }
  }
#endif
  return true;
}

void CHead::Simulate()
{
  UITime time = Glob.uiTime;
  const float t0 = 4.0;
  float oldT = fastFmod(_lastSimulation.toFloat(), t0);
  float newT = fastFmod(time.toFloat(), t0);

  if (_rotate)
  {
    float angle = (H_PI * 2.0 / t0) * newT;
    Matrix3 orient(MRotationY, angle);
    float scale = FutureVisualState().Scale();
    SetOrientation(orient);
    SetScale(scale);
  }

  const float tChange = 0.5 * t0;
  if (oldT < tChange && newT >= tChange)
  {
#if _ENABLE_NEWHEAD
    if (_headType)
    {
      int size = _headType->_grimaces.Size();
      int i = toIntFloor(size * GRandGen.RandomValue());
      _head.SetGrimace(i);
    }
#else
    int size = (Pars >> "CfgMimics" >> "States").GetEntryCount();
    int i = toIntFloor(size * GRandGen.RandomValue());
    _head->SetMimic((Pars >> "CfgMimics" >> "States").GetEntry(i).GetName());
#endif
  }

#if _ENABLE_NEWHEAD
  _head.Simulate(time - _lastSimulation, SimulateVisibleNear, false);
#else
  _head->Simulate(*_headType, time - _lastSimulation, SimulateVisibleNear, false);
#endif
  _lastSimulation = time;
}

#if _ENABLE_NEWHEAD
void CHead::SetHead(RString name)
{
  if (_headType) RemoveHead();
  _headType = HeadTypes.New(name);
  _headType->ShapeAddRef();
  if (_headType->_model.NotNull())
  {
    if (GetShape()->GetSkeleton() != _headType->_model->GetShape()->GetSkeleton())
    {
      RptF("Error: Skeleton of the head %s doesn't match the skeleton of the corresponding body %s", _headType->_model->GetShape()->Name(), GetShape()->Name());
      Fail("Error: Skeleton of some head doesn't match the skeleton of the corresponding body");
    }
  }
  else
  {
    RptF("Error: Head model named '%s' not present", cc_cast(name));
  }
}

void CHead::RemoveHead()
{
  if (_headType)
  {
    _headType->ShapeRelease();
    _headType.Free();
  }
}

void CHead::RemoveGlasses()
{
  if (_glassesType)
  {
    _glassesType->ShapeRelease();
    _glassesType.Free();
  }
}

#endif

void CHead::SetFace(RString name)
{
#if _ENABLE_NEWHEAD
  if (name.GetLength() == 0)
  {
    Fail("Face");
    name = "Default";
  }

  ParamEntryVal cfg = Pars >> "CfgFaces" >> _faceType;
  ConstParamEntryPtr cls = cfg.FindEntry(name);
  if (!cls)
  {
    RptF("SetFace error: class CfgFaces.%s.%s not found", cc_cast(_faceType), cc_cast(name));
    cls = cfg.FindEntry("Default");
    if (!cls)
    {
      RptF("SetFace error: class CfgFaces.%s.Default not found", cc_cast(_faceType));
      return;
    }
  }
  if (cls->FindEntry("head"))
  {
    RString name = *cls >> "head";
    if (!name.IsEmpty()) SetHead(name);
  }
  else
  {
    RptF("SetFace error: 'head' entry was not found (class CfgFaces.%s.%s)", cc_cast(_faceType), cc_cast(name));
  }

  // Read material of the face
  RString matName = GetPictureName(*cls >> "material");
  if (matName.GetLength() == 0)
  {
    // do not change the material
    _faceMaterial = NULL;
  }
  else
  {
    Ref<TexMaterial> mat = GTexMaterialBank.New(TexMaterialName(matName));
    if (mat)
    {
      _faceMaterial = mat;
    }
    else
    {
      RptF("SetFace error: material %s not found", cc_cast(matName));
    }
  }

  // Read texture of the face
  RString textName = GetPictureName(*cls >> "texture");
  Ref<Texture> text = GlobLoadTextureUI(textName);
  if (text)
  {
    _faceTexture = text;
  }
  else
  {
    RptF("SetFace error: texture %s not found", cc_cast(textName));
  }

  #ifndef _XBOX
  if (stricmp(cls->GetName(), "custom") == 0)
  {
    RString GetUserDirectory();
    RString dir = GetUserDirectory();
    dir.Lower();
    RString filename = dir + RString("face.paa");
    if (QIFileFunctions::FileExists(filename))
    {
      Ref<Texture> text = GlobLoadTextureUI(filename);
      if (text)
        _faceTexture = text;
      else
      {
        RptF("SetFace error: cannot load custom face texture %s", cc_cast(filename));
      }
    }
    else
    {
      filename = dir + RString("face.jpg");
      if (QIFileFunctions::FileExists(filename))
      {
        Ref<Texture> text = GlobLoadTextureUI(filename);
        if (text)
          _faceTexture = text;
        else
        {
          RptF("SetFace error: cannot load custom face texture %s", cc_cast(filename));
        }
      }
      else
      {
        RptF("SetFace error: custom face texture not found in %s", cc_cast(dir));
      }
    }
  }
  #endif

#else
  _head->SetFace(*_headType, _faceType, _shape, name);
#endif
}

void CHead::SetGlasses(RString name)
{
#if _ENABLE_NEWHEAD
  if (_glassesType) RemoveGlasses();
  _glassesType = GlassesTypes.New(name);
  _glassesType->ShapeAddRef();
#else
  _head->SetGlasses(*_headType, _shape, name);
#endif
}

void CHead::AttachWave(AbstractWave *wave, float freq)
{
#if _ENABLE_NEWHEAD
  _head.AttachWave(wave, freq);
#else
  _head->AttachWave(wave, freq);
#endif
}

bool ParseUserParams(ParamFile &cfg, RString name, GameDataNamespace *globals);

RString GetFirstSpeakerType()
{
  // find the first speaker type
  ParamEntryPar list = Pars >> "CfgVoiceTypes";
  for (int i=0; i<list.GetEntryCount(); i++)
  {
    ParamEntryPar entry = list.GetEntry(i);
    if (entry.IsClass()) return entry.GetName();
  }
  return RString();
}

void GameHeader::SetPlayerName(RString name)
{
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  Fail("Unsupported");
#else
  playerName = name;
  playerNameChanged = true;
#endif
}

#ifndef _XBOX

static bool AddUserToArray(RString userName, void *context)
{//used in foreach to add all item to autoArray  
  AutoArray<RString> *list = reinterpret_cast<AutoArray<RString> *>(context);
  list->Add(userName);
  return false; // continue
}

DisplayNewUser::DisplayNewUser(ControlsContainer *parent, RString name, bool edit)
: Display(parent)
{
  _edit = edit;
  if (edit)
  {
    _name = name;
    _face = Glob.header.playerFace;
    _glasses = Glob.header.playerGlasses;
    _speakerType = Glob.header.playerSpeakerType;
    _pitch = Glob.header.playerPitch;

    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
    ParamFile cfg;
    if (ParseUserParams(cfg, name, &globals))
    {
      ConstParamEntryPtr identity = cfg.FindEntry("Identity");
      if (identity)
      {
        ConstParamEntryPtr entry = identity->FindEntry("face");
        if (entry) _face = *entry;
        entry = identity->FindEntry("glasses");
        if (entry) _glasses = *entry;
        entry = identity->FindEntry("speaker");
        if (entry) _speakerType = *entry;
        entry = identity->FindEntry("pitch");
        if (entry) _pitch = *entry;
        entry = identity->FindEntry("squad");
        if (entry) _squad = *entry;
      }
    }
  }
  else
  {
    _name = GetNewName(); //get default Player X name (X is unigue number)
    _face = "Default";
    _glasses = "None";
    _speakerType = GetFirstSpeakerType();
    _pitch = 1.0;
  }

  _doPreview = false;

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

  Load("RscDisplayNewUser");
  if (_head)
  {
    _head->SetFace(_face);
    _head->SetGlasses(_glasses);
  }
  if (_faces)
  {
    int sel = 0;
    ParamEntryVal cls = Pars >> "CfgFaces" >> _faceType;
    for (int i=0; i<cls.GetEntryCount(); i++)
    {
      ParamEntryVal entry = cls.GetEntry(i);
      ConstParamEntryPtr disabled = entry.FindEntry("disabled");
      if (disabled && (bool)*disabled) continue;
      RString name = entry.GetName();
#ifndef _XBOX
      if (stricmp(name, "custom") == 0 && !CustomFaceExist(_name)) continue;
#else
      if (stricmp(name, "custom") == 0) continue;
#endif
      int index = _faces->AddString(entry >> "name");
      _faces->SetData(index, name);
      if (name == _face) sel = index;
    }
    _faces->SetCurSel(sel);
  }
}

Control *DisplayNewUser::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_NEW_USER_TITLE:
    {
      ITextContainer *text = GetTextContainer(ctrl);
      if (text)
      {
        if (_edit)
          text->SetText(LocalizeString(IDS_NEWUSER_TITLE2));
        else
          text->SetText(LocalizeString(IDS_NEWUSER_TITLE1));
      }
    }
    break;
  case IDC_NEW_USER_ID:
    {
      ITextContainer *text = GetTextContainer(ctrl);
      if (text)
      {
        char buffer[256];
        RString GetPublicKey();
        sprintf(buffer, LocalizeString(IDS_PLAYER_ID), (const char *)GetPublicKey());
        text->SetText(buffer);
      }
    }
    break;
  case IDC_NEW_USER_NAME:
    {
      CEditContainer *edit = GetEditContainer(ctrl);
      if (edit)
      {
        if (GetLangID() == Korean)
          edit->SetMaxChars(14);
        else
          edit->SetMaxChars(24);
        edit->SetText(_name);
      }
      bool enable = stricmp(GetUserRootDir(_name), GetDefaultUserRootDir()) != 0;
      ctrl->EnableCtrl(enable);
    }
    break;
  case IDC_NEW_USER_FACE:
    _faces = GetListBoxContainer(ctrl);
    break;
  case IDC_NEW_USER_GLASSES:
    {
      CListBoxContainer *list = GetListBoxContainer(ctrl);
      if (list)
      {
        int sel = 0;
        ParamEntryVal cls = Pars >> "CfgGlasses";
        for (int i=0; i<cls.GetEntryCount(); i++)
        {
          ParamEntryVal entry = cls.GetEntry(i);
          int scope = entry >> "scope";
          if (scope!=2) continue;
          RString name = entry.GetName();
          int index = list->AddString(entry >> "name");
          list->SetData(index, name);
          if (name == _glasses) sel = index;
        }
        list->SetCurSel(sel);
      }
    }
    break;
  case IDC_NEW_USER_SPEAKER:
    {
      CListBoxContainer *list = GetListBoxContainer(ctrl);
      if (list)
      {
        int sel = 0;
        ParamEntryVal cls = Pars >> "CfgVoiceTypes";
        for (int i=0; i<cls.GetEntryCount(); i++)
        {
          ParamEntryVal entry = cls.GetEntry(i);
          if (!entry.IsClass()) continue;
          RString name = entry.GetName();

          int index = list->AddString(entry >> "name");
          list->SetData(index, name);
          if (stricmp(name, _speakerType) == 0) sel = index;
        }
        list->SetCurSel(sel);
      }
    }
    break;
  case IDC_NEW_USER_PITCH:
    {
      CSliderContainer *slider = GetSliderContainer(ctrl);
      if (slider)
      {
        slider->SetRange(0.8, 1.2);
        slider->SetThumbPos(_pitch);
        slider->SetSpeed(0.02, 0.1);
      }
    }
    break;
  }
  return ctrl;
}

ControlObject *DisplayNewUser::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_NEW_USER_HEAD)
  {
    _head = new CHead(this, idc, cls);
    _faceType = _head->GetFaceType();
    return _head;
  }
  else
    return Display::OnCreateObject(type, idc, cls);
}

void DisplayNewUser::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
  case IDC_CANCEL:
    {
      _exit = idc;
      bool canDestroy = CanDestroy();
      _exit = -1;
      if (!canDestroy) break;

      ControlObjectContainerAnim *ctrl =
        dynamic_cast<ControlObjectContainerAnim *>(GetCtrl(IDC_NEW_USER_NOTEBOOK));
      if (ctrl)
      {
        _exitWhenClose = idc;
        ctrl->Close();
        if (_head) _head->ShowCtrl(false);
      }
      else Exit(idc);
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayNewUser::OnSliderPosChanged(IControl *ctrl, float pos)
{
  if (ctrl->IDC() == IDC_NEW_USER_PITCH)
  {
    CListBoxContainer *list = GetListBoxContainer(GetCtrl(IDC_NEW_USER_SPEAKER));
    if (list)
    {
      int sel = list->GetCurSel();
      if (sel >= 0)
      {
        _previewSpeakerType = list->GetData(sel);
        _previewPitch = pos;
        _doPreview = true;
        _previewTime = GlobalTickCount() + 200;
      }
    }
  }
  else
    Display::OnSliderPosChanged(ctrl, pos);
}

void DisplayNewUser::OnLBSelChanged(IControl *ctrl, int curSel)
{
  if (ctrl->IDC() == IDC_NEW_USER_SPEAKER)
  {
    if (curSel >= 0)
    {
      CListBoxContainer *list = GetListBoxContainer(ctrl);
      CSliderContainer *slider = GetSliderContainer(GetCtrl(IDC_NEW_USER_PITCH));
      if (list && slider)
      {
        _previewSpeakerType = list->GetData(curSel);
        _previewPitch = slider->GetThumbPos();
        _doPreview = true;
        _previewTime = GlobalTickCount() + 200;
      }
    }
  }
  else if (ctrl->IDC() == IDC_NEW_USER_FACE)
  {
    if (_faces && _head && curSel >= 0)
    {
      _head->SetFace(_faces->GetData(curSel));
    }
  }
  else if (ctrl->IDC() == IDC_NEW_USER_GLASSES)
  {
    CListBoxContainer *list = GetListBoxContainer(ctrl);
    if (list && _head && curSel >= 0)
    {
      _head->SetGlasses(list->GetData(curSel));
    }
  }
  else
    Display::OnLBSelChanged(ctrl, curSel);
}

void DisplayNewUser::OnObjectMoved(int idc, Vector3Par offset)
{
  if (idc == IDC_NEW_USER_NOTEBOOK)
  {
/*
    Control3D *area = check_cast<Control3D *>(GetCtrl(IDC_NEW_USER_HEAD_AREA));
    if (area && _head)
    {
      Vector3 pos = area->GetCenter();
      float coef = _head->Position().Z() / pos.Z();
      _head->SetPosition(_head->Position() + coef * offset);
    }
*/
  }
  Display::OnObjectMoved(idc, offset);
}

void DisplayNewUser::OnCtrlClosed(int idc)
{
  if (idc == IDC_NEW_USER_NOTEBOOK)
    Exit(_exitWhenClose);
  else
    Display::OnCtrlClosed(idc);
}

void DisplayNewUser::OnSimulate(EntityAI *vehicle)
{
  if (_head) _head->Simulate();

  Display::OnSimulate(vehicle);

  if (_previewSpeech && _previewSpeech->IsTerminated()) _previewSpeech = NULL;
  if (!_previewSpeech && _doPreview && GlobalTickCount() >= _previewTime)
  {
    Preview(_previewSpeakerType, _previewPitch);
    _doPreview = false;
  }
}

void DisplayNewUser::Preview(RString speakerType, float pitch)
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

RString DisplayNewUser::GetNewName()
{ //get all users
#ifdef _WIN32
  AutoArray<RString> userList;
  ForEachUser(AddUserToArray,&userList);
  //create first possible player name
  int index = 1;
  BString<50> buffer; //i suppose max 10^43 players is enough  
  sprintf(buffer,"PLAYER %d",index);

  for (int i=0; i<userList.Size();i++)
  {//if player already exist...
    if(strcmpi(buffer,userList.Get(i)) == 0 )
    {//...increase player index and try again
      index++;  //prepare next index
      sprintf(buffer,"PLAYER %d",index);
      i = 0;  //restart cycle
    }
  }

  return cc_cast(buffer);
#else 
  return RString();
#endif
}

bool DisplayNewUser::CanDestroy()
{
  if (!Display::CanDestroy()) return false;

#ifdef _WIN32
  if (_exit == IDC_OK)
  {
    CEditContainer *edit = GetEditContainer(GetCtrl(IDC_NEW_USER_NAME));
    if (!edit) return false;
    RString name = edit->GetText(); //Hladas fix: const char* didn't work properly in next if()
    if (name.IsEmpty() || IsEmptyPlayerName(name))
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_EMPTY));
      return false;
    }
    if (!IsValidPlayerName(name))
    {
      CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYERNAME_INVALID));
      return false;
    }
    if (!_edit || stricmp(name, _name) != 0)
    {
      _finddata_t info;
      RString dir = GetUserRootDir(name);
      long check = X_findfirst(dir, &info);
      if (check != -1)
      {
        // player already exist
        CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_PLAYER_EXIST));
        _findclose(check);
        return false;
      }
    }
  }
#endif

  return true;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// Mission, intro, outro displays

static bool BreakIntro(bool credits)
{
  if (credits)
  {
    static const int breakButtons[]=
    {
      XBOX_A,XBOX_Start,
      XBOX_B,XBOX_Back
    };
    for (int i=0; i<sizeof(breakButtons)/sizeof(*breakButtons); i++)
    {
      int b = breakButtons[i];
      if (GInput.xInputButtonsUp[b])
      {
        GInput.xInputButtonsUp[b] = false;
        return true;
      }
    }
  }
  else
  {
    static const int breakButtons[]=
    {
      XBOX_A,XBOX_Start
    };
    for (int i=0; i<sizeof(breakButtons)/sizeof(*breakButtons); i++)
    {
      int b = breakButtons[i];
      if (GInput.xInputButtonsUp[b])
      {
        GInput.xInputButtonsUp[b] = false;
        return true;
      }
    }
  }
  if (GInput.keysToDo[DIK_ESCAPE])
  {
    GInput.keysToDo[DIK_ESCAPE] = false;
    return true;
  }
  if (GInput.keysUp[DIK_SPACE])
  {
    GInput.keysUp[DIK_SPACE] = false;
    return true;
  }
  return false;
}

//! HUD hint display
/*!
  This variant stops simulation and waits for user input.
*/
class DisplayHintC : public Display
{
protected:
  //@{
  //! fast access to control
  CStatic *_background;
  CStatic *_hint;
  CActiveText *_button;
  //@}

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayHintC(ControlsContainer *parent, RString hint);
  void OnSimulate(EntityAI *vehicle);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  //! returns displayed hint
  RString GetHint() {return _hint->GetText();}
};

DisplayHintC::DisplayHintC(ControlsContainer *parent, RString hint)
: Display(parent)
{
  _hint = NULL;
  _enableSimulation = false;
  LoadFromEntry(Pars >> "RscDisplayHintC");
  
  if(_hint)
  {
    _hint->SetText(hint);
    float h = _hint->GetTextHeight();
    float offset = _button->Y() - _background->Y() - _background->H();
    float dh = _background->H() - _hint->H();
    _hint->SetPos
      (
      _hint->X(), _hint->Y(), _hint->W(), h
      );
    _background->SetPos
      (
      _background->X(), _background->Y(), _background->W(), h + dh
      );
    _button->SetPos
      (
      _button->X(), _background->Y() + _background->H() + offset, _button->W(), _button->H()
      );
  }
}

Control *DisplayHintC::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_HINTC_BG:
    _background = new CStatic(this, idc, cls);
    return _background;
  case IDC_HINTC_HINT:
    _hint = new CStatic(this, idc, cls);
    _hint->EnableCtrl(false);
    return _hint;
  case IDC_CANCEL:
    _button = new CActiveText(this, idc, cls);
    return _button;
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplayHintC::OnSimulate(EntityAI *vehicle)
{
  if (GInput.GetKeyToDo(DIK_SPACE))
  {
    Exit(IDC_CANCEL);
  }

  Display::OnSimulate(vehicle);
}

/*!
\patch 5126 Date 1/30/2007 by Jirka
- Fixed: Crash when command "hintC" was used
*/

//! HUD extended hint display
/*!
  This variant stops simulation and waits for user input.
*/
class DisplayHintCEx : public Display
{
protected:
  //@{
  //! fast access to control
  Ref<CStatic> _background;
  Ref<CStatic> _title;
  Ref<CStructuredText> _hint;
  Ref<Control> _button;
  Ref<CStatic> _line1;
  Ref<CStatic> _line2;
  //@}

public:
  //! constructor
  /*!
    \param parent parent display
  */
  DisplayHintCEx(ControlsContainer *parent, RString title, StaticArrayAuto<RefR<INode> > &hints);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnDraw (EntityAI *vehicle, float alpha);
  void OnSimulate(EntityAI *vehicle);

  INode *GetHint() const {return _hint ? _hint->GetStructuredText() : NULL;}
  RString GetTitle() const {return _title ? _title->GetText() : RString();}

protected:
  void UpdateLayout();
};

DisplayHintCEx::DisplayHintCEx(ControlsContainer *parent, RString title, StaticArrayAuto<RefR<INode> > &hints)
: Display(parent)
{
  _enableSimulation = true;
  ParamEntryVal cls = Pars >> "RscDisplayHintCEx";
  LoadFromEntry(cls);
  
  if (_title) _title->SetText(title);

  if (_hint)
  {
    Ref<INode> image = CreateTextImage(NULL);
    SetTextAttribute(image, "image", cls >> "indent");
    Ref<INode> space = CreateTextASCII(NULL, " ");
    Ref<INode> br = CreateTextBreak(NULL);
    Ref<INode> root = CreateTextStructured(NULL);

    root->Add(br);
    for (int i=0; i<hints.Size(); i++)
    {
      root->Add(image);
      root->Add(space);
      root->Add(hints[i]);
      root->Add(br);
      root->Add(br);
    }
    _hint->SetText(root);
  }
}

void DisplayHintCEx::UpdateLayout()
{
  float h = _hint ? _hint->GetTextHeight() : 0;
  float bh = _background ? _background->H() : 0;
  float hTotal = bh + h;
  float by = _background ? _background->Y() : 0;
  float offsetTitle = _title ? _title->Y() - by : 0;
  float offsetHint = _hint ? _hint->Y() - by : 0;
  float offsetButton = _button ? _button->Y() - by : 0;
  float offsetLine1 = _line1 ? _line1->Y() - by : 0;
  float offsetLine2 = _line2 ? _line2->Y() - by : 0;

  float top = 0.5 - 0.5 * hTotal;
  if (_background) _background->SetPos
  (
    _background->X(), top, _background->W(), hTotal
  );
  if (_title) _title->SetPos
  (
    _title->X(), top + offsetTitle, _title->W(), _title->H()
  );
  if (_line1) _line1->SetPos
  (
    _line1->X(), top + offsetLine1, _line1->W(), _line1->H()
  );
  if (_hint) _hint->SetPos
  (
    _hint->X(), top + offsetHint, _hint->W(), h
  );
  if (_line2) _line2->SetPos
  (
    _line2->X(), top + offsetLine2 + h, _line2->W(), _line2->H()
  );
  if (_button) _button->SetPos
  (
    _button->X(), top + offsetButton + h, _button->W(), _button->H()
  );
}

Control *DisplayHintCEx::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_HINTC_BG:
    _background = new CStatic(this, idc, cls);
    return _background;
  case IDC_HINTC_TITLE:
    _title = new CStatic(this, idc, cls);
    return _title;
  case IDC_HINTC_HINT:
    _hint = new CStructuredText(this, idc, cls);
    return _hint;
  case IDC_HINTC_LINE1:
    _line1 = new CStatic(this, idc, cls);
    return _line1;
  case IDC_HINTC_LINE2:
    _line2 = new CStatic(this, idc, cls);
    return _line2;
  case IDC_HINTC_CONTINUE:
    _button = Display::OnCreateCtrl(type, idc, cls);
    return _button;
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplayHintCEx::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_HINTC_CONTINUE:
    Exit(idc);
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayHintCEx::OnDraw (EntityAI *vehicle, float alpha)
{
  if (_enableSimulation)
  {
    // draw when controls are preloaded
    if (IsReady())
    {
      UpdateLayout();
      _enableSimulation = false;
    }
    else return;
  }
  Display::OnDraw(vehicle, alpha);
}

void DisplayHintCEx::OnSimulate(EntityAI *vehicle)
{
  if (!_enableSimulation) Display::OnSimulate(vehicle);
}


DisplayMission::DisplayMission(ControlsContainer *parent, bool editor, bool erase, bool initMap)
  : Display(parent)
{
  _editor = editor;
  _wantedDialog = STNone;
  Load("RscDisplayMission");

  for (int i=0; i<NSaveGameType; i++) _saveTimeHandles[i] = Future<QFileTime>(0);
    
  SetCursor(NULL);

  // update mouse state to avoid cursor movement after EnableSimulation
  GInput.ProcessMouse();
  GInput.cursorX = 0;
  GInput.cursorY = 0;
  GInput.cursorMovedZ = 0;

  if (erase || initMap) InitMap(); // do not rewrite map settings after load
  InitUI();
  GWorld->OnInitMission(true);
  if (GWorld->IsPreloadAsked() && !GWorld->CameraOn())
  {
    // black out screen, wait for preload
    GWorld->SetCutEffect(0, CreateTitleEffect(TitBlackIn, "", 1000));
  }

  if (erase)
  {
    GWorld->DeleteSaveGame(SGTAutosave);
    GWorld->DeleteSaveGame(SGTUsersave);
    GWorld->DeleteSaveGame(SGTUsersave2);
    GWorld->DeleteSaveGame(SGTUsersave3);
    GWorld->DeleteSaveGame(SGTUsersave4);
    GWorld->DeleteSaveGame(SGTUsersave5);
    GWorld->DeleteSaveGame(SGTUsersave6);
  }
  
  ContinueSaved = editor;

  GWorld->ForceEnd(false);

  ShowCinemaBorder(true);

#if _XBOX_SECURE && _ENABLE_MP
# if _XBOX_VER >= 200
  // Statistics handled by the NetworkManager
# else
  if (GWorld->GetMode() == GModeNetware && GetNetworkManager().IsSignedIn() && !GetNetworkManager().IsSystemLink())
    _statsBegin = new LiveStatsUpdate(RString(), LSABegin);
# endif
#endif

  // Mission header
#if _ENABLE_PERFLOG && defined _WIN32 && _ENABLE_CHEATS
  WriteMissionHeader("mission");
#endif

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  void AutotestStartMission();
  AutotestStartMission();
#endif
}

ControlObject *DisplayMission::OnCreateObject(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_INVENTORY_SHORTCUT)
  {
    return new CItemShortcut(this, idc, cls, true);
  }
  return Display::OnCreateObject(type, idc, cls);
}

void DisplayMission::ShowHint(RString hint)
{
  CreateChild(new DisplayHintC(this, hint));
}

void DisplayMission::ShowHint(RString title, StaticArrayAuto< RefR<INode> > &hints)
{
  CreateChild(new DisplayHintCEx(this, title, hints));
}

void DisplayMission::InitMap()
{
  // main map
  GWorld->CreateMainMap();
  DisplayMap *map = dynamic_cast<DisplayMap *>(GWorld->Map());
  if (map)
  {
    ConstParamEntryPtr cls = ExtParsMission.FindEntry("showMap");
    map->ShowMap(!cls ? true : (*cls));
    cls = ExtParsMission.FindEntry("showNotepad");
    map->ShowNotepad(!cls ? true : (*cls));
    cls = ExtParsMission.FindEntry("showGPS");
    map->ShowGPS(!cls ? true : (*cls));
  }
}

void DisplayMission::InitUI()
{
  // in game UI
  AbstractUI *ui = GWorld->UI();
  if (ui)
  {
    ConstParamEntryPtr cls = ExtParsMission.FindEntry("showHUD");
    bool show = !cls ? true : (*cls);
    ui->ShowAll(show);
    // independent flag controlled by scripts
    ui->ScriptShowHUD(true);
  }
  
  // FIX
  GWorld->DestroyUserDialog();

  // chat
  GChatList.Clear();
  GChatList.ScriptEnable(true);
  GChatList.Load(Pars >> "RscChatListMission");
  GetNetworkManager().SetPendingInvitationPos("RscPendingInvitationInGame");

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

  //Check the selected chat channel (possibly disabled channel was selected in briefing)
  void SetChatChannel(ChatChannel channel);
  SetChatChannel(ActualChatChannel());
  GWorld->OnChannelChanged();
}

bool CanSaveGame()
{
#if defined _XBOX && _XBOX_VER >= 200
  if (!GSaveSystem.IsStorageAvailable())
    return false;
#endif

  // saving disabled by the script
  if (!GWorld->IsSavingEnabled()) return false;
  // MP saves removed
  if (GWorld->GetMode()==GModeNetware)
  {
    return false;
  }
  // do not save wizard user mission
  if (GetMissionParameters().GetLength() > 0) return false;
  // do not save unnamed mission
  if (Glob.header.filename[0] == 0) return false;
  // do not save when player is dead (holds only for Single player saves)
  if (!GWorld->GetRealPlayer() || GWorld->GetRealPlayer()->IsDamageDestroyed()) return false;
  //mission cannot continue (f.e. special character has died)
  if (GWorld->GetEEndMode() != EMContinue) return false;

  ConstParamEntryPtr entry = ExtParsMission.FindEntry("saving");
  if (entry)
  {
    bool saving = *entry;
    if (!saving) return false;
  }

  return true;
}

bool CanSaveContinue()
{
  if (!CanSaveGame()) return false;

  return true;
}

static bool SaveContinue()
{
  if (!CanSaveContinue()) return true;
  if (!ContinueSaved)
  {
    if (GWorld->SaveGame(SGTContinue))
    {
      ContinueSaved = true;
      return true;
    }
  }
  else
  {
    return true;
  }
  return false;
}

// updates save time handles
void UpdateSaveTimeHandles(Future<QFileTime> saveTimeHandles[NSaveGameType])
{
  bool someNotDone = false;
  for (int i=0; i<NSaveGameType; i++) saveTimeHandles[i] = Future<QFileTime>(0);
#if defined _XBOX && _XBOX_VER >= 200
  XSaveGame GetSaveGame(bool create, bool noErrorMsg = false);
  XSaveGame saveHandle = GetSaveGame(false);
  RString dir = SAVE_ROOT;
  if (saveHandle) // Errors already handled
#else
  RString dir = GetSaveDirectory();
  if (dir.GetLength() > 0)
#endif
  {
    RString mpCampaignPrefix = RString("");
    for (int i=0; i<NSaveGameType; i++)
    {
      if (i != SGTContinue)
      {
        saveTimeHandles[i] = GFileServerFunctions->RequestTimeStamp(dir + mpCampaignPrefix + GetSaveFilename((SaveGameType)i));
        if (!saveTimeHandles[i].IsReady()) someNotDone = true;
      }
    }
  } 

  if (someNotDone)
  {
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    // we have to wait for operations to complete
    while (someNotDone)
    {
      someNotDone = false;
      for (int i=0; i<NSaveGameType; i++)
      {
        if (!saveTimeHandles[i].IsReady()) someNotDone = true;
      }
      progress.Refresh();
    }
  }
}

/*!
\patch 1.78 Date 7/16/2002 by Jirka
- Fixed: User dialog doesn't disappear when mission end
*/

DisplayMission::~DisplayMission()
{
#if _ENABLE_PERFLOG
  LogF("</mission>");
#endif

#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
  void AutotestEndMission();
  AutotestEndMission();
#endif

  if (!_editor) // otherwise the mission editor is responsible for cleanup
  {
    GChatList.Clear();
    GChatList.ScriptEnable(true);
    GChatList.Load(Pars >> "RscChatListDefault");
    GetNetworkManager().SetPendingInvitationPos("RscPendingInvitation");
    GCustomChannels.ClearCustomChannels();
    // SaveContinue();
#ifdef _WIN32
    if (GWorld) GWorld->OnFinishMission();
    if (GJoystickDevices) GJoystickDevices->FFOff();
    if (XInputDev) XInputDev->FFOff();
#endif

    ResetDisabledChannels();
  }
}

/*!
\patch 1.75 Date 2/11/2002 by Jirka
- Added: exit.sqs script launched if mission ends 
*/

void DisplayMission::OnSimulate(EntityAI *vehicle)
{
  PROFILE_SCOPE(dspMs);
  if (_wantedDialog != STNone)
  {
    {
      PROFILE_SCOPE(dmPre);
      if (_wantedResource.GetLength() > 0 && !Pars.Request(_wantedResource)) return;
      for (int i=0; i<NSaveGameType; i++)
      {
        if (!_saveTimeHandles[i].IsReady())
          return;
      }
    }
    switch (_wantedDialog)
    {
    case STMissionEnd:
      {
        PROFILE_SCOPE(crDME);
        _wantedChild = new DisplayMissionEnd(this, _saveTimeHandles);
        _wantedChild->IsReady(); // start preload
      }
      break;
    case  STMissionFail:
      {
        PROFILE_SCOPE(crDME);
        _wantedChild = new DisplayMissionFail(this, _saveTimeHandles);
        _wantedChild->IsReady(); // start preload
      }
      break;
    case STInterrupt:
      _wantedChild = new DisplayInterrupt(this, _saveTimeHandles);
      _wantedChild->IsReady(); // start preload
       break;
    }
    _wantedDialog = STNone;
    _wantedResource = RString();
    for (int i=0; i<NSaveGameType; i++) _saveTimeHandles[i] = Future<QFileTime>(0);
#if defined _XBOX && _XBOX_VER >= 200
    _saveHandle = NULL;
#endif
    return;
  }
  else if (_wantedChild)
  {
    PROFILE_SCOPE(wcIsR);
    if (_wantedChild->IsReady())
    {
#if _ENABLE_PERFLOG
      LogF("<dialog>");
#endif
      CreateChild(_wantedChild);
      _wantedChild = NULL;
      _enableSimulation = true;
      FinishProgress();
    }
    return;
  }

  if (GWorld->IsPreloadAsked() && GWorld->CameraOn())
  {
    GWorld->PreloadAroundCameraSync(5.0f);
    GWorld->FadeInMission();
  }

#ifdef _XBOX
  if (GInput.GetXInputButton(XBOX_ControllerRemoved, false) > 0)
  {
    _msgBox = CreateActiveControllerRemovedMessageBox();
  }
#endif

/*
  if (_compass) _compass->ShowCtrl(GWorld->HasCompass());
  if (_watch) _watch->ShowCtrl(GWorld->HasWatch());
*/

#if _ENABLE_IDENTITIES
  if (GInput.GetActionToDo(UADiary))
  {
    if (!GWorld->GetCameraEffect())   
    CreateChild(new DisplayDiary(this,"Tasks"));
    //void ProcessDiaryLink(RString link);
    //ProcessDiaryLink("Tasks");
  }
#endif
  
  if (GWorld->GetMode() == GModeNetware)
  {
    NetworkClientState state = GetNetworkManager().GetClientState();
    if (state != NCSPlaying)
    {
#if _XBOX_SECURE && _ENABLE_MP
      // server terminates this session, I am OK
# if _XBOX_VER >= 200
      // Statistics handled by the NetworkManager
# else
      if (GetNetworkManager().IsSignedIn() && !GetNetworkManager().IsSystemLink())
      {
        SRef<LiveStatsUpdate> statsEnd = new LiveStatsUpdate(RString(), LSAEnd);
        statsEnd->Close(); // update immediately
      }
# endif
#endif

      // end mission
//      GStats.Update();
      GWorld->DestroyMap(IDC_OK);
//      GWorld->EnableSimulation(false);
      Exit(IDC_CANCEL);
      return;
    }

#if !defined _XBOX// && _ENABLE_UA_NETWORK_PLAYERS
    if (GInput.GetActionToDo(UANetworkPlayers))
    {
#if _ENABLE_IDENTITIES
      if (!GWorld->GetCameraEffect())   
      CreateChild(new DisplayDiary(this,"players",DCInGame));
#endif
      //CreateChild(new DisplayMPPlayers(this));
    }
#if _ENABLE_IDENTITIES
  if (GInput.GetActionToDo(UADiary))
  {
    //CreateChild(new DisplayDiary(this,"Tasks"));
    //void ProcessDiaryLink(RString link);
    //ProcessDiaryLink("Tasks");
  }
#endif

#endif
  }
  else
  {
    EEndMode emode = GWorld->GetEEndMode();
    if ((
      emode == EMKilled || 
      (GWorld->IsEndFailed() && emode!=EMContinue) ) 
      && GWorld->CameraOn())
    {
      if (GWorld->IsEndDialogEnabled())
      {
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
        if (!AutoTest.IsEmpty())
        {
          GWorld->DestroyMap(IDC_OK);
          Exit(IDC_MAIN_QUIT);
        }
        else
#endif
        {
          PROFILE_SCOPE(dmPrR);
          if(GWorld->IsEndFailed() && emode != EMKilled)
          {
            _wantedDialog = STMissionFail;
            _wantedResource = "RscDisplayMissionFail";
            GWorld->SetEndFailed(false);
          }
          else
          {
            _wantedDialog = STMissionEnd;
            _wantedResource = "RscDisplayMissionEnd";
          }
          Pars.Request(_wantedResource);
          if (GWorld->GetMode() != GModeNetware)
          {
#if defined _XBOX && _XBOX_VER >= 200
            XSaveGame GetSaveGame(bool create, bool noErrorMsg = false);
            _saveHandle = GetSaveGame(false);
            RString dir = SAVE_ROOT;
            if (_saveHandle) // Errors already handled
#else
            RString dir = GetSaveDirectory();
            if (dir.GetLength() > 0)
#endif
            {
              RString mpCampaignPrefix = RString("");
              // create requests (not for Continue save)
              for (int i=0; i<NSaveGameType; i++)
              {
                if (i == SGTContinue) _saveTimeHandles[i] = Future<QFileTime>(0);
                else _saveTimeHandles[i] = GFileServerFunctions->RequestTimeStamp(dir + mpCampaignPrefix + GetSaveFilename((SaveGameType)i));
              }
            }
          }
        }
      }
      return;
    }
  }

  EEndMode emode = GWorld->GetEEndMode();
  if
  (
    emode != EMContinue && emode != EMKilled && 
    (!GWorld->IsEndFailed() || GWorld->GetMode() == GModeNetware) &&
    (
      (!GWorld->GetCameraEffect() && !GWorld->GetTitleEffect() && !GWorld->GetCameraScript())
      || GWorld->IsEndForced()
    ) 
  )
  {
    GWorld->OnMissionEvent(MEEnded, GWorld->GetEndMode());
    // Mission ended, clear its EHs
    GWorld->ClearMissionEventHandlers();

#if _ENABLE_DATADISC
		if (GetMissionDirectory().GetLength() > 0)
    {
      RString exitScript;
#if USE_PRECOMPILATION
      exitScript = GetMissionDirectory() + RString("exit.sqf");
      if (QFBankQueryFunctions::FileExists(exitScript))
      {
        ScriptVM *script = new ScriptVM(exitScript, GameValue(GWorld->GetEndMode()), GWorld->GetMissionNamespace()); // mission namespace
        GWorld->AddScriptVM(script, true); // simulate now
      }
#endif
      exitScript = GetMissionDirectory() + RString("exit.sqs");
      if (QFBankQueryFunctions::FileExists(exitScript))
      {
        Script *script = new Script("exit.sqs", GameValue(GWorld->GetEndMode()), GWorld->GetMissionNamespace(), INT_MAX); // mission namespace
        GWorld->AddScript(script, true); // simulate now
      }
    }
#endif

    // end mission
//    GStats.Update();
    GWorld->DestroyMap(IDC_OK);
#if _XBOX_SECURE && _ENABLE_MP
    // regular end of mission, OK
# if _XBOX_VER >= 200
    // Statistics handled by the NetworkManager
# else
    if (GWorld->GetMode() == GModeNetware && GetNetworkManager().IsSignedIn() && !GetNetworkManager().IsSystemLink())
    {
      SRef<LiveStatsUpdate> statsEnd = new LiveStatsUpdate(RString(), LSAEnd);
      statsEnd->Close(); // update immediately
    }
# endif
#endif
#if _ENABLE_CHEATS || _ENABLE_AUTOTEST
    if (!AutoTest.IsEmpty())
      Exit(IDC_MAIN_QUIT);
    else
#endif
      Exit(IDC_CANCEL);
  }
  else if (GInput.GetActionToDo(UAIngamePause))
  {
    if(GWorld->HasMap()) 
    {
      GWorld->ShowMap(false);
    }
    else
    {
      FreeOnDemandGarbageCollect(1024*1024,512*1024);
      GEngine->PrepareScreenshot();
      _wantedDialog = STInterrupt;
      StartProgress(LocalizeString(IDS_LOAD_WORLD), 300);
      if (GWorld->GetMode() == GModeNetware)
      {
        _wantedResource = "RscDisplayMPInterrupt";
      }
      else
      {
        _enableSimulation = false;
        _wantedResource = "RscDisplayInterrupt";
      }
#if defined _XBOX && _XBOX_VER >= 200
      XSaveGame GetSaveGame(bool create, bool noErrorMsg = false);
      _saveHandle = GetSaveGame(false);
      RString dir = SAVE_ROOT;
      if (_saveHandle) // Errors already handled
#else
      RString dir = GetSaveDirectory();
      if (dir.GetLength() > 0)
#endif
      {
        RString mpCampaignPrefix = RString("");
        // create requests (not for Continue save)
        for (int i=0; i<NSaveGameType; i++)
        {
          if (i == SGTContinue) _saveTimeHandles[i] = Future<QFileTime>(0);
          else _saveTimeHandles[i] = GFileServerFunctions->RequestTimeStamp(dir + mpCampaignPrefix + GetSaveFilename((SaveGameType)i));
        }
      }
      Pars.Request(_wantedResource);
    }
  }

#if _XBOX_SECURE && _ENABLE_MP
# if _XBOX_VER >= 200
  // Statistics handled by the NetworkManager
# else
  if (_statsBegin) _statsBegin->Process();
# endif
#endif

  // simplified content of ControlsContainer::OnSimulate to allow users to use event handlers
  if (_exit < 0)
  {
    bool shift = GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT]; (void)shift;
    bool control = GInput.keys[DIK_LCONTROL] || GInput.keys[DIK_RCONTROL]; (void)control;
    bool alt = GInput.keys[DIK_LMENU] || GInput.keys[DIK_RMENU]; (void)alt;

#ifndef _XBOX
# if _ENABLE_CHEATS
    // when cheat mouse is used, ignore it for UI
    if (!GInput.keys[DIK_RWIN])
# endif
    {
      // mouse buttons
      for (int i=0; i<N_MOUSE_BUTTONS; i++)
      {
        // process all edges
        unsigned int downToDo = GInput.mouseButtonsToDo[i];
        unsigned int upToDo = GInput.mouseButtonsReleased[i];
        unsigned int cycles;
        if (GInput.mouseButtons[i])
          cycles = downToDo - 1 < upToDo ? downToDo - 1 : upToDo; // downToDo need to be handled at last
        else
          cycles = downToDo < upToDo ? downToDo : upToDo;
        if (cycles < 0) cycles = 0;

        if (upToDo > cycles)
        {
          OnEvent(DEMouseButtonUp, i, 0.5f, 0.5f, shift, control, alt); // during the mission, UI cursor is clipped
          upToDo--;
        }
        for (unsigned int j=0; j<cycles; j++)
        {
          OnEvent(DEMouseButtonDown, i, 0.5f, 0.5f, shift, control, alt); // during the mission, UI cursor is clipped
          downToDo--;
          OnEvent(DEMouseButtonUp, i, 0.5f, 0.5f, shift, control, alt); // during the mission, UI cursor is clipped
          upToDo--;
        }
        if (downToDo > 0)
        {
          Assert(GInput.mouseButtons[i]);
          OnEvent(DEMouseButtonDown, i, 0.5f, 0.5f, shift, control, alt); // during the mission, UI cursor is clipped
          downToDo--;
        }
        Assert(downToDo == 0);
        Assert(upToDo == 0);
      }

      // mouse movement
      float mouseSpeedX = GInput.mouseAxis[1] - GInput.mouseAxis[0];
      float mouseSpeedY = GInput.mouseAxis[3] - GInput.mouseAxis[2];
      if (mouseSpeedX != 0 || mouseSpeedY != 0)
        OnEvent(DEMouseMoving, mouseSpeedX, mouseSpeedY);
      else
        OnEvent(DEMouseHolding, 0.0f, 0.0f);

      // mouse wheel
      if (GInput.cursorMovedZ != 0)
      {
        OnEvent(DEMouseZChanged, GInput.cursorMovedZ);
        GInput.cursorMovedZ = 0;
      }
    }
#endif
  }
}

void DisplayMission::DoRestartMission()
{
  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeArcade);

    GWorld->SwitchLandscape(Glob.header.worldname);
    GStats.ClearMission();
    CurrentTemplate->ActivateAddons(Glob.header.worldname);
    CurrentTemplate->InitGeneral(GModeArcade);
    GWorld->InitVehicles(*CurrentTemplate);

  GWorld->DeleteSaveGame(SGTAutosave);
  GWorld->DeleteSaveGame(SGTUsersave);
  GWorld->DeleteSaveGame(SGTContinue);
  GWorld->DeleteSaveGame(SGTUsersave2);
  GWorld->DeleteSaveGame(SGTUsersave3);
  GWorld->DeleteSaveGame(SGTUsersave4);
  GWorld->DeleteSaveGame(SGTUsersave5);
  GWorld->DeleteSaveGame(SGTUsersave6);

  ProgressFinish(p);

}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

void DisplayMission::OnInvited()
{
  if (_msgBox) return; // confirmation in progress

  if (GSaveSystem.IsInvitationConfirmed())
  {
    Exit(IDC_OK);
  }
  else
  {
    GSaveSystem.ConfirmInvitation();
    // let the user confirm the action because of possible progress lost
    MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
    MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
    RString message;
    if (GetNetworkManager().IsServer())
      message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION);
    else if (GetNetworkManager().GetClientState() != NCSNone)
      message = LocalizeString(IDS_MSG_CONFIRM_TERMINATE_SESSION_CLIENT);
    else
      message = LocalizeString(IDS_MSG_CONFIRM_ACCEPT_INVITATION);
    // special type of message box which will not close because of invitation
    _msgBox = new MsgBoxIgnoreInvitation(this, MB_BUTTON_OK | MB_BUTTON_CANCEL, message, IDD_MSG_ACCEPT_INVITATION, false, &buttonOK, &buttonCancel);
  }
}

#endif

bool DisplayMission::RestartMission()
{
  DoAssert(GWorld->GetMode() != GModeNetware);

  GWorld->DestroyMap(IDC_OK);

  GWorld->OnInitMission(false);

  DoRestartMission();
  
  GWorld->PreloadMission();

  // create map after changes in DoRestartMission takes place
  InitMap();
  InitUI();

  // GWorld->FadeInMission(); - already called from RunInitScript

  return true;
}

bool DisplayMission::RetryMission()
{
  DoAssert(GWorld->GetMode() != GModeNetware);

  GWorld->DestroyMap(IDC_OK);
  InitMap(); // create a map prior load to do not rewrite map settings

  // retry mission
  if (GWorld->LoadGame(SGTAutosave) == LSOK)
  {
    GWorld->FadeInMission();
  }
  else
  {
    DoRestartMission();
    // create map after changes in DoRestartMission takes place
    InitMap();
  }

  InitUI();

  GWorld->OnInitMission(true);

  return true;
}

void DisplayMission::AbortMission()
{
  if (SaveContinue())
  {
    GWorld->DestroyMap(IDC_OK);
    Exit(IDC_MAIN_QUIT);
  }
}

/*!
  \patch 1.01 Date 06/11/2001 by Jirka
  - Fixed: "End" button in "Player killed" screen was handled bad
  \patch_internal 1.01 Date 06/11/2001 by Jirka
  - when button 'End' was pressed dialog exits with IDC_CANCEL
  - the right exit code is IDC_MAIN_QUIT
  \patch 5115 Date 1/5/2007 by Jirka
  - New: button "Play again" added to the mission interrupt dialog
  \patch 5126 Date 2/5/2007 by Jirka
  - Fixed: Function "hintC" (versions with the title) - when dialog is closed, text is shown in HUD
*/

void DisplayMission::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_MISSION_END:
    if (exit == IDC_ME_RETRY)
    {
      Display::OnChildDestroyed(idd, exit);
      if (!RetryMission())
      {
        // end mission
//        GStats.Update();
        GWorld->DestroyMap(IDC_OK);
        Exit(IDC_CANCEL);
      }
    }
    else if (exit == IDC_ME_RESTART)
    {
      Display::OnChildDestroyed(idd, exit);
      if (!RestartMission())
      {
        // end mission
        //        GStats.Update();
        GWorld->DestroyMap(IDC_OK);
        Exit(IDC_CANCEL);
      }
    }
    else if (exit == IDC_ME_LOAD)
    {
      // decide what slot to use
      SaveGameType type = (SaveGameType)-1;
      // two possible dialogs with the same IDD
      DisplayMissionEnd *child = dynamic_cast<DisplayMissionEnd *>(_child.GetRef());
      if (child) type = child->GetSelectedSlot();
      else
      {
        DisplayMissionFail *child = dynamic_cast<DisplayMissionFail *>(_child.GetRef());
        if (child) type = child->GetSelectedSlot();
      }

      Display::OnChildDestroyed(idd, exit);
      if (type == -1) break;

      InitMap(); // create a map prior load to do not rewrite map settings
      InitUI();
      LSError err = GWorld->LoadGame(type);
      if (err != LSOK)
      {
        if (err != LSNoAddOn) // this error was handled already
        {
          // process warning and end mission
          UserFileErrorInfo info;
          info._file = GetSaveFilename(type);

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

        GWorld->DestroyMap(IDC_OK);
        Exit(IDC_CANCEL);
        return;
      }
      GWorld->OnInitMission(true);
      GWorld->FadeInMission();
    }
    else if (exit == IDC_CANCEL)
    {
      Display::OnChildDestroyed(idd, exit);
      // end mission
      GWorld->DestroyMap(IDC_OK);
      // fixed: was Exit(IDC_CANCEL)
      Exit(IDC_MAIN_QUIT);
    }
    else
    {
      Display::OnChildDestroyed(idd, exit);
    }
    GInput.InitMouseVariables(); //after returning to simulation - LMB sometimes fired!
#if _ENABLE_PERFLOG
    LogF("</dialog>");
#endif
    break;
  case IDD_INTERRUPT:
    if (exit == IDC_INT_SAVE)
    {
      if (CanSaveGame())
      {
        // decide what slot to use
        DisplayInterrupt *child = static_cast<DisplayInterrupt *>(_child.GetRef());
        SaveGameType type = child->GetSelectedSlot();
        DoAssert(type != -1);

        GWorld->SaveGame(type);
      }
      Display::OnChildDestroyed(idd, exit);
    }
    else if (exit == IDC_INT_LOAD)
    {
      // decide what slot to use
      DisplayInterrupt *child = static_cast<DisplayInterrupt *>(_child.GetRef());
      SaveGameType type = child->GetSelectedSlot();
      DoAssert(type != -1);
      Display::OnChildDestroyed(idd, exit);

      // FIX
      GWorld->DestroyUserDialog();

      InitMap(); // create a map prior load to do not rewrite map settings
      InitUI();
      if (GWorld->GetMode()==GModeNetware) 
      {
        Exit(IDC_CANCEL);
      }
      else
      {
        LSError err = GWorld->LoadGame(type, true);
        if (err == LSOK)
        {
          // OnInitMission moved into the LoadGame
          // this is done because we need OnInitMission to be called before World::Serialize,
          // we cannot call it before LoadGame so that Xbox handling of corrupted loads is clean (no OnInitMission called for them)
          GWorld->PreloadMission();
          GWorld->FadeInMission();
        }
        else if (err == LSNoAddOn)  // this error was handled already
        {
          GWorld->DestroyMap(IDC_OK);
          Exit(IDC_CANCEL);
          return;
        }
        else
        {
          _error._file = GetSaveFilename(type);
#if defined _XBOX && _XBOX_VER >= 200
          if (err != LSDiskError)
          {
            RString GetSaveFileName();
            RString GetSaveDisplayName();
            _error._dir = GetSaveFileName();
            _error._name = GetSaveDisplayName();
            _msgBox = new UserFileErrorMsg(this, _error, true);
          }
#else
          RString FindSaveGameName(RString dir, RString file);
          _error._dir = GetSaveDirectory();
          _error._name = FindSaveGameName(_error._dir, _error._file);
          _msgBox = new UserFileErrorMsg(this, _error, true);
#endif
        }
      }
    }
    else
    {
      Display::OnChildDestroyed(idd, exit);
      if (exit == IDC_INT_ABORT)
      {
        // abort mission
        AbortMission();
      }
#if _ENABLE_IDENTITIES
      else if (exit == IDC_INT_DIARY)
      {
        void ProcessDiaryLink(RString link);
        ProcessDiaryLink(RString());
      }
#endif
      else if (exit == IDC_INT_RETRY || exit == IDC_INT_RESTART)
      {
        if (GetNetworkManager().IsGameMaster())
        {
          GetNetworkManager().ProcessCommand("#restart");
        }
        else if (GWorld->GetMode()==GModeNetware && !GetNetworkManager().IsServer())
        {
          // ignore for client
        }
        else if (exit == IDC_INT_RETRY)
        {
          if (!RetryMission())
          {
            // abort mission
            AbortMission();
          }
        }
        else
        {
          if (!RestartMission())
          {
            // abort mission
            AbortMission();
          }
        }
      }
    }
    GEngine->CleanUpScreenshot();
    GInput.InitMouseVariables(); //after returning to simulation - LMB sometimes fired!
#if _ENABLE_PERFLOG
    LogF("</dialog>");
#endif
    break;
  case IDD_HINTC:
    {
      DisplayHintC *child = static_cast<DisplayHintC *>(_child.GetRef());
      GWorld->UI()->ShowHint(child->GetHint(), true);
    }
    Display::OnChildDestroyed(idd, exit);
    break;    
  case IDD_HINTC_EX:
    {
      DisplayHintCEx *child = static_cast<DisplayHintCEx *>(_child.GetRef());
      INode *hint = child->GetHint();
      RString title = child->GetTitle();
      if (title.GetLength() > 0)
      {
        if (hint)
        {
          Ref<INode> root = CreateTextStructured(NULL);
          CreateTextPlain(root, title);
          root->Add(hint);
          GWorld->UI()->ShowHint(root, true);
        }
        else GWorld->UI()->ShowHint(title, true);
      }
      else if (hint) GWorld->UI()->ShowHint(hint, true);
    }
    Display::OnChildDestroyed(idd, exit);
    break;    
  case IDD_MSG_LOAD_FAILED:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_YES), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      RString text = Format(LocalizeString(IDS_XBOX_QUESTION_DELETE_SAVE), (const char *)_error._name);
      CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, text, IDD_MSG_DELETESAVE, false, &buttonOK, &buttonCancel);
    }
    break;
  case IDD_MSG_DELETESAVE:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
#if defined _XBOX && _XBOX_VER >= 200
      XSaveGame GetSaveGame(bool create, bool noErrorMsg = false);
      XSaveGame save = GetSaveGame(false);
      RString dir = SAVE_ROOT;
      if (save) // Errors already handled
#else
      RString dir = GetSaveDirectory();
      if (dir.GetLength() > 0)
#endif
      {
        QIFileFunctions::CleanUpFile(dir + RString("save.") + GetExtensionSave());
      }
    }
    break;
#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
  case IDD_MSG_ACCEPT_INVITATION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_CANCEL);
    }
    else
    {
      // Invitation rejected
      GSaveSystem.InvitationHandled();
    }
    break;    
#endif
  case IDD_DIARY:
    Display::OnChildDestroyed(idd, exit);
    if (exit == 1003) 
    {
      Display *CreateInventoryDialog(ControlsContainer *parent, AIBrain *me, EntityAI *source);
      AIBrain *unit = GWorld->FocusOn();
      if (unit) GWorld->SetUserDialog(CreateInventoryDialog(NULL, unit, NULL));
    }
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;    
  }
}

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

void DisplayMission::HandleRequest(RequestType request)
{
  switch (request)
  {
  case RTResaveMissionContinue:
    AbortMission();
    break;
  case RTResaveUserSave:
    if (CanSaveGame()) GWorld->SaveGame(SGTUsersave);
    break;
  case RTResaveUserSave2:
    if (CanSaveGame()) GWorld->SaveGame(SGTUsersave2);
    break;
  case RTResaveUserSave3:
    if (CanSaveGame()) GWorld->SaveGame(SGTUsersave3);
    break;
  case RTResaveUserSave4:
    if (CanSaveGame()) GWorld->SaveGame(SGTUsersave4);
    break;
  case RTResaveUserSave5:
    if (CanSaveGame()) GWorld->SaveGame(SGTUsersave5);
    break;
  case RTResaveUserSave6:
    if (CanSaveGame()) GWorld->SaveGame(SGTUsersave6);
    break;
  default:
    ControlsContainer::HandleRequest(request);
    break;
  }
}

#endif //#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)

DisplayCutscene::DisplayCutscene(ControlsContainer *parent)
  : Display(parent)
{
  _credits = false;
  SetCursor(NULL);
//  GWorld->EnableSimulation(true);
  ShowCinemaBorder(true);
}

DisplayCutscene::~DisplayCutscene()
{
  if (GWorld)
  {
    GWorld->UnloadSounds();

    GWorld->SwitchCameraTo(NULL, GWorld->GetCameraType(), true);
    GWorld->SwitchPlayerTo(NULL);
    GWorld->SetRealPlayer(NULL);
  }
#ifdef _WIN32
  if (GJoystickDevices) GJoystickDevices->FFOff();
  if (XInputDev) XInputDev->FFOff();
#endif
}

void DisplayCutscene::OnSimulate(EntityAI *vehicle)
{
#ifdef _XBOX
  if (GInput.GetXInputButton(XBOX_NoController, false) > 0)
  {
    if (_credits || !Pars.FindEntry("RscDisplayMovieInterrupt"))
      OnControllerRemoved();
    else
    {
      CreateChild(new DisplayCutsceneInterrupt(this));
      _child->OnControllerRemoved();
    }
    return;
  }
#endif

  if (GWorld->GetEEndMode() != EMContinue/* || !GLOB_WORLD->GetCameraEffect() */)
  {
//    GWorld->EnableSimulation(false);
    Exit(IDC_CANCEL);
    return;
  }

  if (BreakIntro(_credits))
  {
    if (_credits || !Pars.FindEntry("RscDisplayMovieInterrupt"))
      Exit(IDC_CANCEL);
    else
      CreateChild(new DisplayCutsceneInterrupt(this));
  }
}

void DisplayCutscene::OnChildDestroyed(int idd, int exit)
{
  Display::OnChildDestroyed(idd, exit);

  if (idd == IDD_INTERRUPT)
  {
    switch (exit)
    {
    case IDC_CANCEL:
      // resume
      break;
    case IDC_OK:
      // skip movie
      Exit(IDC_CANCEL);
      break;
    case IDC_INT_RETRY:
      // play again
      PlayAgain();
      break;
    case IDC_ABORT:
      // abort
      Exit(IDC_ABORT);
      break;
    }
  }
}

bool DisplayCutscene::PlayAgainInit()
{
  bool ret = true;
  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeIntro);

  GWorld->SwitchLandscape(Glob.header.worldname);
  CurrentTemplate->ActivateAddons(Glob.header.worldname);
  CurrentTemplate->InitGeneral(GModeIntro);
  if (!GWorld->InitVehicles(*CurrentTemplate))
  {
    Exit(IDC_CANCEL);
    ret = false;
  }
  ProgressFinish(p);
  return ret;
}
void DisplayCutscene::PlayAgainEnd()
{
  GWorld->OnInitMission(true);
}

DisplayIntro::DisplayIntro(ControlsContainer *parent, DisplayIntro::NoInit, bool credits)
  : DisplayCutscene(parent)
{
  _credits = credits;

  Load("RscDisplayIntro");

  Init();
}

DisplayIntro::DisplayIntro(ControlsContainer *parent)
  : DisplayCutscene(parent)
{
  Load("RscDisplayIntro");

  ParseIntro(true);

  if (CurrentTemplate->IsEmpty())
  {
//    GWorld->EnableSimulation(false);
    Exit(IDC_CANCEL);
    return;
  }

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeIntro);

  GLOB_WORLD->SwitchLandscape(Glob.header.worldname);
  CurrentTemplate->ActivateAddons(Glob.header.worldname);
  CurrentTemplate->InitGeneral(GModeIntro);
  bool ok = GLOB_WORLD->InitVehicles(*CurrentTemplate);
  if (!ok)
  {
    ProgressFinish(p);
    Exit(IDC_CANCEL);
    return;
  }

  Init();
  GWorld->OnInitMission(true);
  ProgressFinish(p);

  // Header
#if _ENABLE_PERFLOG && defined _WIN32 && _ENABLE_CHEATS
  WriteMissionHeader("intro");
#endif
}

DisplayIntro::DisplayIntro(ControlsContainer *parent, RString cutscene)
  : DisplayCutscene(parent)
{
  Load("RscDisplayIntro");

  if (cutscene.GetLength() == 0)
  {
//    GWorld->EnableSimulation(false);
    Exit(IDC_CANCEL);
    return;
  }

  if (!ProcessTemplateName(cutscene))
  {
    Exit(IDC_CANCEL);
    return;
  }

  ParseIntro(true);

  if (CurrentTemplate->IsEmpty())
  {
//    GWorld->EnableSimulation(false);
    Exit(IDC_CANCEL);
    return;
  }

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeIntro);

  GLOB_WORLD->SwitchLandscape(Glob.header.worldname);
  CurrentTemplate->ActivateAddons(Glob.header.worldname);
  CurrentTemplate->InitGeneral(GModeIntro);
  bool ok = GLOB_WORLD->InitVehicles(*CurrentTemplate);
  if (!ok)
  {
    ProgressFinish(p);
    Exit(IDC_CANCEL);
    return;
  }

  Init();
  GWorld->OnInitMission(true);
  ProgressFinish(p);

  // Header
#if _ENABLE_PERFLOG && defined _WIN32 && _ENABLE_CHEATS
  WriteMissionHeader("intro");
#endif
}

bool LaunchIntro(Display *parent, bool credits)
{
  ParseIntro(true);

  if (CurrentTemplate->IsEmpty()) return false;

  Ref<ProgressHandle> p = GWorld->BegLoadMissionProgress(Glob.header.worldname, *CurrentTemplate, GModeIntro);

  GLOB_WORLD->SwitchLandscape(Glob.header.worldname);
  CurrentTemplate->ActivateAddons(Glob.header.worldname);
  CurrentTemplate->InitGeneral(GModeIntro);
  bool ok = GLOB_WORLD->InitVehicles(*CurrentTemplate);

  ProgressFinish(p);

  if (!ok) return false;

  parent->CreateChild(new DisplayIntro(parent, DisplayIntro::noInit, credits));

  GWorld->OnInitMission(true);

  // Header
#if _ENABLE_PERFLOG && defined _WIN32 && _ENABLE_CHEATS
  WriteMissionHeader("intro");
#endif

  return true;
}

bool LaunchIntro(Display *parent, RString cutscene, bool credits)
{
  if (cutscene.GetLength() == 0) return false;
  if (!ProcessTemplateName(cutscene)) return false;
  return LaunchIntro(parent, credits);
}

/*!
\patch 1.75 Date 2/4/2002 by Jirka
- Added: script initIntro.sqs is launched on the beginning of intro
\patch 1.82 Date 8/8/2002 by Jirka
- Fixed: initIntro.sqs didn't respond to saved variables
*/

void DisplayIntro::Init()
{
  // main map
  GWorld->CreateMainMap();
  DisplayMap *map = dynamic_cast<DisplayMap *>(GWorld->Map());
  if (map)
  {
    map->ShowMap(true);
    map->ShowNotepad(false);
    map->ShowGPS(false);
    map->SetCursor(NULL);
  }

#if _ENABLE_DATADISC
  RunInitIntroScript();
#endif
}

void DisplayIntro::PlayAgain()
{
  if (!DisplayCutscene::PlayAgainInit())
  {
    return;
  }
  Init();
  DisplayCutscene::PlayAgainEnd();
}

DisplayIntro::~DisplayIntro()
{
#if _ENABLE_PERFLOG
  LogF("</intro>");
#endif
}

DisplayOutro::DisplayOutro(ControlsContainer *parent, EEndMode mode)
  : DisplayCutscene(parent)
{
  _mode = mode;
  Load("RscDisplayOutro");

  // Header
#if _ENABLE_PERFLOG && defined _WIN32 && _ENABLE_CHEATS
  WriteMissionHeader("outro");
#endif
}

DisplayOutro::~DisplayOutro()
{
#if _ENABLE_PERFLOG
  LogF("</outro>");
#endif
}

void DisplayOutro::PlayAgain()
{
  if (PlayAgainInit())
  {
    PlayAgainEnd();
  }
}

DisplayMissionEnd::DisplayMissionEnd(ControlsContainer *parent, Future<QFileTime> saveTimeHandles[NSaveGameType])
  : Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _enableUI = false;

  _selectedSlot = (SaveGameType)-1;
  for (int i=0; i<NSaveGameType; i++) _saveHandles[i] = Future<QFileTime>(0);

  int quotations = (IDS_QUOTE_LAST - IDS_QUOTE_1) / 2 + 1;
  _quotation = toIntFloor(quotations * GRandGen.RandomValue());
  Load("RscDisplayMissionEnd");

  UpdateSaveControls(saveTimeHandles);
}

DisplayMissionEnd::~DisplayMissionEnd()
{
  GInput.ChangeGameFocus(-1);
}

void DisplayMissionEnd::UpdateSaveControls(Future<QFileTime> saveTimeHandles[NSaveGameType])
{
#ifdef _WIN32
  // check if some user save exist
  bool isSave = false;

  if (GWorld->GetMode() != GModeNetware)
  {
    for (int i=0; i<NSaveGameType; i++)
    {
      _saveHandles[i] = Future<QFileTime>(0);
      // TODO: check - sync / async?
      if (saveTimeHandles[i].GetResult()!=0)
      {
        _saveHandles[i] = saveTimeHandles[i];
        if (i != SGTContinue) isSave = true;
      }
    }
  }

  IControl *ctrl = GetCtrl(IDC_ME_LOAD);
  if (ctrl) ctrl->ShowCtrl(false);

  ctrl = GetCtrl(IDC_ME_RESTART);
  if (ctrl) ctrl->ShowCtrl(false);

  ctrl = GetCtrl(IDC_ME_RETRY);
  if (ctrl)
  {
    CStructuredText *text = GetStructuredText(ctrl);
    if (text)
    {
      if (!isSave)
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
void DisplayMissionEnd::OnStorageDeviceChanged()
{
  Future<QFileTime> saveTimeHandles[NSaveGameType];
  UpdateSaveTimeHandles(saveTimeHandles);
  UpdateSaveControls(saveTimeHandles);
  GSaveSystem.StorageChangeHandled();
}
#endif

Control *DisplayMissionEnd::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
    case IDC_ME_SUBTITLE:
      {
        CStatic *text = new CStatic(this, idc, cls);
        text->SetText("");
        return text;
      }
    case IDC_ME_QUOTATION:
      {
        CStatic *text = new CStatic(this, idc, cls);
        text->SetText(RString("\"") + LocalizeString(IDS_QUOTE_1 + 2 * _quotation) + RString("\""));
        text->EnableCtrl(false);
        return text;
      }
    case IDC_ME_AUTHOR:
      {
        CStatic *text = new CStatic(this, idc, cls);
        text->SetText(LocalizeString(IDS_AUTHOR_1 + 2 * _quotation));
        return text;
      }
  }
  return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayMissionEnd::OnButtonClicked(int idc)
{
  if (idc == IDC_ME_RETRY)
  {
    bool isSave = false;
    if (GWorld->GetMode() != GModeNetware)
    {
      for (int i=0; i<NSaveGameType; i++)
      {
        if (i != SGTContinue && _saveHandles[i].GetResult()!=0)
        {
          isSave = true;
          break;
        }
      }
    }
    if (isSave)
    {
      // select how to revert
      CreateChild(new DisplayInterruptRevert(this, _saveHandles));
    }
    else Exit(IDC_ME_RESTART);
  }
  else
  {
    // all other buttons will close the dialog and will be handled by the parent
    Exit(idc);
  }
}

void DisplayMissionEnd::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_INTERRUPT_REVERT:
    if (exit == IDC_INT_LOAD)
    {
      DisplayInterruptRevert *child = static_cast<DisplayInterruptRevert *>(_child.GetRef());
      _selectedSlot = child->GetSaveSlot();
      Display::OnChildDestroyed(idd, exit);
      Exit(IDC_ME_LOAD);
    }
    else if (exit == IDC_INT_RETRY)
    {
      Display::OnChildDestroyed(idd, exit);
      Exit(IDC_ME_RETRY);
    }
    else if (exit == IDC_INT_RESTART)
    {
      Display::OnChildDestroyed(idd, exit);

      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RESTART_MISSION, false, &buttonOK, &buttonCancel);
    }
    else
    {
      Display::OnChildDestroyed(idd, exit);
    }
    break;
  case IDD_MSG_RESTART_MISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_ME_RESTART);
    }
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

DisplayMissionFail::DisplayMissionFail(ControlsContainer *parent, Future<QFileTime> saveTimeHandles[NSaveGameType])
: Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _enableUI = false;

  _selectedSlot = (SaveGameType)-1;
  for (int i=0; i<NSaveGameType; i++) _saveHandles[i] = Future<QFileTime>(0);

  int quotations = (IDS_QUOTE_LAST - IDS_QUOTE_1) / 2 + 1;
  _quotation = toIntFloor(quotations * GRandGen.RandomValue());
  Load("RscDisplayMissionFail");

  UpdateSaveControls(saveTimeHandles);
  CreateDebriefing();

  CHTML *htmlText = dynamic_cast<CHTML *>(GetCtrl(IDC_DEBRIEFING_DEBRIEFING));
  if(htmlText && _debriefing)
  {
    float size = 0;
    if(_debriefing->NSections()>0)
    {
      HTMLSection &section = _debriefing->GetSection(_debriefing->CurrentSection());
      //get HTML control height
      for (int r=0; r<section.rows.Size(); r++)
      {
        HTMLRow &row = section.rows[r];
        size += row.height;
      }
    }
    htmlText->SetPos(htmlText->X(),htmlText->Y(),htmlText->W(),size);
  }

  htmlText = dynamic_cast<CHTML *>(GetCtrl(IDC_DEBRIEFING_OBJECTIVES));
  if(htmlText && _overview)
  {
    float size = 0;
    if(_overview->NSections()>0)
    {
      HTMLSection &section = _overview->GetSection(_overview->CurrentSection());
      //get HTML control height
      for (int r=0; r<section.rows.Size(); r++)
      {
        HTMLRow &row = section.rows[r];
        size += row.height;
      }
    }
    htmlText->SetPos(htmlText->X(),htmlText->Y(),htmlText->W(),size);
  }

  htmlText = dynamic_cast<CHTML *>(GetCtrl(IDC_DEBRIEFING_STAT));
  if(htmlText && _stats)
  {
    float size = 0;
    if(_stats->NSections()>0)
    {
      HTMLSection &section = _stats->GetSection(_stats->CurrentSection());
      //get HTML control height
      for (int r=0; r<section.rows.Size(); r++)
      {
        HTMLRow &row = section.rows[r];
        size += row.height;
      }
    }
    htmlText->SetPos(htmlText->X(),htmlText->Y(),htmlText->W(),size);
  }
}

DisplayMissionFail::~DisplayMissionFail()
{
  GInput.ChangeGameFocus(-1);
}

bool DisplayMissionFail::OnHTMLLink(int idc, RString link)
{
  if (idc == IDC_DEBRIEFING_OBJECTIVES)
  {
    if (stricmp(link, "stat:open") == 0)
    {
      if (GetCtrl(IDC_DEBRIEFING_STATS_GROUP)) GetCtrl(IDC_DEBRIEFING_STATS_GROUP)->ShowCtrl(true);
      if (GetCtrl(IDC_DEBRIEFING_OBJECTIVES_GROUP)) GetCtrl(IDC_DEBRIEFING_OBJECTIVES_GROUP)->ShowCtrl(false);
      return true;
    }
  }
  else if (idc == IDC_DEBRIEFING_STAT)
  {
    if (stricmp(link, "stat:close") == 0)
    {
      if (GetCtrl(IDC_DEBRIEFING_STATS_GROUP)) GetCtrl(IDC_DEBRIEFING_STATS_GROUP)->ShowCtrl(false);
      if (GetCtrl(IDC_DEBRIEFING_OBJECTIVES_GROUP)) GetCtrl(IDC_DEBRIEFING_OBJECTIVES_GROUP)->ShowCtrl(true);
      return true;
    }
  }

  return Display::OnHTMLLink(idc, link);
}

void DisplayMissionFail::UpdateSaveControls(Future<QFileTime> saveTimeHandles[NSaveGameType])
{
#ifdef _WIN32
  // check if some user save exist
  bool isSave = false;

  if (GWorld->GetMode() != GModeNetware)
  {
    for (int i=0; i<NSaveGameType; i++)
    {
      _saveHandles[i] = Future<QFileTime>(0);
      if (saveTimeHandles[i].GetResult()!=0)
      {
        _saveHandles[i] = saveTimeHandles[i];
        if (i != SGTContinue) isSave = true;
      }
    }
  }

  IControl *ctrl = GetCtrl(IDC_ME_LOAD);
  if (ctrl) ctrl->ShowCtrl(false);

  ctrl = GetCtrl(IDC_ME_RESTART);
  if (ctrl) ctrl->ShowCtrl(false);

  ctrl = GetCtrl(IDC_ME_RETRY);
  if (ctrl)
  {
    CStructuredText *text = GetStructuredText(ctrl);
    if (text)
    {
      if (!isSave)
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
void DisplayMissionFail::OnStorageDeviceChanged()
{
  Future<QFileTime> saveTimeHandles[NSaveGameType];
  UpdateSaveTimeHandles(saveTimeHandles);
  UpdateSaveControls(saveTimeHandles);
  GSaveSystem.StorageChangeHandled();
}
#endif

Control *DisplayMissionFail::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_DEBRIEFING_RESULT:
    _result = GetTextContainer(ctrl);
    break;
  case IDC_DEBRIEFING_TITLE:
    _title = GetTextContainer(ctrl);
    break;
  case IDC_DEBRIEFING_DEBRIEFING:
    _debriefing = GetHTMLContainer(ctrl);
    break;
  case IDC_DEBRIEFING_OBJECTIVES:
    _overview = GetHTMLContainer(ctrl);
    break;
  case IDC_DEBRIEFING_INFO:
    _info = GetHTMLContainer(ctrl);
    break;
  case IDC_DEBRIEFING_STAT:
    _stats = GetHTMLContainer(ctrl);
  case IDC_DEBRIEFING_STATS_GROUP:
    ctrl->ShowCtrl(false);
    break;
  }

  return ctrl;
}

void DisplayMissionFail::OnButtonClicked(int idc)
{
  switch (idc)
  {
    // by default continue
  case IDC_OK:
  case IDC_CANCEL:
    Exit(idc);
    break;
  case IDC_AUTOCANCEL:
    Exit(IDC_AUTOCANCEL);
    break;
  case IDC_ME_RETRY:
    {
      bool isSave = false;
      if (GWorld->GetMode() != GModeNetware)
      {
        for (int i=0; i<NSaveGameType; i++)
        {
          if (i != SGTContinue && _saveHandles[i].GetResult()!=0)
          {
            isSave = true;
            break;
          }
        }
      }
      if (isSave)
      {
        // select how to revert
        CreateChild(new DisplayInterruptRevert(this, _saveHandles));
      }
      else Exit(IDC_ME_RESTART);
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

static void AddObjective(CHTMLContainer *html, int src, int dst, int value)
{
  if (value == OSHidden) return;

  ParamEntryVal cls = Pars >> "RscObjectives";
  RString picture;
  switch (value)
  {
  case OSDone:
    picture = cls >> "done";
    break;
  case OSFailed:
    picture = cls >> "failed";
    break;
  default:
    Fail("Unknown objective type");
  case OSActive:
    picture = cls >> "active";
    break;
  }
  html->AddBreak(dst, false);
  float imgHeight = 480.0f * 1.0f * html->GetPHeight();
  HTMLField *fld = html->AddImage(dst, picture, HALeft, false, -1, imgHeight, "");
  fld->exclude = true;
  float indent = 1.2 * fld->width;
  html->SetIndent(indent);
  html->CopySection(src, dst);
  html->SetIndent(0);
}

static void AddTasks(CHTMLContainer *html, int dst, Person *player)
{
#if _ENABLE_IDENTITIES

  const RefArray<Task> tasks = player->GetIdentity().GetSimpleTasks();
  ParamEntryVal cls = Pars >> "RscObjectives";
  RString picture;

  for (int i=0; i< tasks.Size(); i++)
  {
    switch (tasks[i]->GetState())
    {
    case TSSucceeded:
      picture = cls >> "done";
      break;
    case TSFailed:
      picture = cls >> "failed";
      break;
    case TSCanceled:
      picture = cls >> "cancled";
      break;
    case TSCreated:
    case TSNone:
    case TSAssigned:
      picture = cls >> "active";
      break;
    default:
      Fail("Unknown objective type");
      break;
    }

    html->AddBreak(dst, false);
    float imgHeight = 480.0f * 1.0f * html->GetPHeight();
    html->SetIndent(0);
    HTMLField *fld = html->AddImage(dst, picture, HALeft, false, -1, imgHeight, "");
    fld->exclude = true;
    float indent = 1.2 * fld->width;

    RString taskdesc;
    tasks[i]->GetDescriptionShort(taskdesc);
    html->SetIndent(indent);
    html->AddText(dst,taskdesc,HFP,HALeft,false,false,"",0,0,false);
    html->AddBreak(dst, false);
  }
  html->SetIndent(0);


#endif
}

struct KillsInfo
{
  Ref<const EntityAIType> type;
  RString playerName;
  int n;
};
TypeIsMovableZeroed(KillsInfo)

struct CasualtiesInfo
{
  bool player;
  RString killedName;
  RString killerName;
  int n;
};
TypeIsMovableZeroed(CasualtiesInfo)

static int CmpKillsInfo(const KillsInfo *info1, const KillsInfo *info2)
{
  if (info2->playerName.GetLength() > 0)
    if (info1->playerName.GetLength() > 0) return info2->n - info1->n;
    else return 1;
  else if (info1->playerName.GetLength() > 0) return -1;
  else return (int)(info2->type->_cost - info1->type->_cost);
}

static int CmpCasualtiesInfo(const CasualtiesInfo *info1, const CasualtiesInfo *info2)
{
  if (info2->player)
    if (info1->player) return info2->n - info1->n;
    else return 1;
  else if (info1->player) return -1;
  else return strcmp(info1->killedName, info2->killedName);
}

void DisplayMissionFail::CreateDebriefing()
{
  // prepare statistics 
  _oldStats = GStats;
  _client = false;
  _server = false;

  AIUnitHeader &newInfo = GStats._playerInfo;
//  AIUnitHeader &oldInfo = _oldStats._playerInfo;
  RString playerName = GetLocalPlayerName();
  if (newInfo.unit) playerName = newInfo.unit->GetPerson()->GetPersonName();

  // load the briefing
  RString briefing = GetBriefingFile();
  if (briefing.GetLength() > 0)
  {
    if (_debriefing) _debriefing->Load(briefing);
    if (_overview) _overview->Load(briefing);
  }

  // FIX: EndMission resets Glob.clock
  float currentTime = Glob.clockWeather.GetTimeInYear();

  // mission name
  if (_title)
  {
    RString text;
    if (GWorld->GetMode() == GModeNetware)
    {
      const MissionHeader *header = GetNetworkManager().GetMissionHeader();
      if (header) text = header->GetLocalizedMissionName();
    }
    if (text.GetLength() == 0)
    {
      text = Localize(CurrentTemplate->Intel().briefingName);
      if (text.GetLength() == 0)
      {
        text = Glob.header.filenameReal;
        // user mission file name is encoded
        if (IsUserMission()) text = DecodeFileName(text);
      }
    }
    _title->SetText(text);
  }


  if (_info)
  {
    int section = _info->AddSection();
    _info->AddName(section, "__INFO");

    // rank, name of soldier
    RString text = "";
    if(newInfo.unit->GetSide() != TCivilian) text = LocalizeString(IDS_PRIVATE + newInfo.rank) + RString(" ") + playerName;
    else text = playerName;
    _info->AddText(section, text, HFP, HALeft, false, false, "");
    _info->AddBreak(section, false);

    int GetDaysInMonth(int year, int month);
    // mission duration
    const ArcadeIntel &intel = CurrentTemplate->Intel();
    int day = intel.day - 1;
    int year = intel.year;
    for (int m=0; m<intel.month-1; m++) day += GetDaysInMonth(year, m);
    float time = intel.hour * OneHour + intel.minute * OneMinute + day * OneDay + 0.5 * OneSecond;
    float dt = floatMax(currentTime - time, 0);
    dt *= 365 * 24; int hours = toIntFloor(dt); dt -= hours;
    dt *= 60; int minutes = toInt(dt); dt -= minutes;
    if (hours > 0)
      text = Format(LocalizeString(IDS_BRIEF_DURATION_LONG), hours, minutes);
    else
      text = Format(LocalizeString(IDS_BRIEF_DURATION_SHORT), minutes);
    _info->AddText(section, text, HFP, HALeft, false, false, "");
    _info->AddBreak(section, false);

    if (!_client && !_server) // single player
    {
      _info->AddBreak(section, false);

    }

    _info->FormatSection(section);
    _info->SwitchSection("__INFO");
  }

  // mission end
  if (_debriefing)
  {
    int section = _debriefing->AddSection();
    _debriefing->AddName(section, "__DEBRIEFING");

    int src = -1;
    switch (GWorld->GetEEndMode())
    {
    case EMLoser:
      src = _debriefing->FindSection("Debriefing:Loser");
      break;
    case EMEnd1:
      src = _debriefing->FindSection("Debriefing:End1");
      break;
    case EMEnd2:
      src = _debriefing->FindSection("Debriefing:End2");
      break;
    case EMEnd3:
      src = _debriefing->FindSection("Debriefing:End3");
      break;
    case EMEnd4:
      src = _debriefing->FindSection("Debriefing:End4");
      break;
    case EMEnd5:
      src = _debriefing->FindSection("Debriefing:End5");
      break;
    case EMEnd6:
      src = _debriefing->FindSection("Debriefing:End6");
      break;
    case EMCheat:
      src = _debriefing->FindSection("Debriefing:End1"); //Cheat is like ending with End1
      break;
    }
    if (src >= 0) _debriefing->CopySection(src, section);

    _debriefing->FormatSection(section);
    _debriefing->SwitchSection("__DEBRIEFING");
  }

  // objectives
  _objectives = false;
  if (_overview)
  {
    int lSection = _overview->AddSection();
    _overview->AddName(lSection, "__OBJECTIVES");

    _overview->AddText(lSection, LocalizeString(IDS_BRIEF_OBJECTIVES), HFH1, HACenter, false, false, "");
    _overview->AddBreak(lSection, false);
    TargetSide side = TargetSide(Glob.header.playerSide);
    for (int s=0; s<_overview->NSections(); s++)
    {
      const HTMLSection &src = _overview->GetSection(s);
      for (int n=0; n<src.names.Size(); n++)
      {
        RString name = src.names[n];
        static const char *prefix = "OBJ_";
        static const char *prefixWest = "OBJ_WEST_";
        static const char *prefixEast = "OBJ_EAST_";
        static const char *prefixGuerrila = "OBJ_GUER_";
        static const char *prefixCivilian = "OBJ_CIVIL_";
        static const char *prefixHidden = "OBJ_HIDDEN_";
        static const char *prefixWestHidden = "OBJ_WEST_HIDDEN_";
        static const char *prefixEastHidden = "OBJ_EAST_HIDDEN_";
        static const char *prefixGuerrilaHidden = "OBJ_GUER_HIDDEN_";
        static const char *prefixCivilianHidden = "OBJ_CIVIL_HIDDEN_";

        if (strnicmp(name, prefix, strlen(prefix)) == 0)
        {
          if (strnicmp(name, prefixWest, strlen(prefixWest)) == 0)
          {
            if (side != TWest) continue;
          }
          else if (strnicmp(name, prefixEast, strlen(prefixEast)) == 0)
          {
            if (side != TEast) continue;
          }
          else if (strnicmp(name, prefixGuerrila, strlen(prefixGuerrila)) == 0)
          {
            if (side != TGuerrila) continue;
          }
          else if (strnicmp(name, prefixCivilian, strlen(prefixCivilian)) == 0)
          {
            if (side != TCivilian) continue;
          }
          else if (strnicmp(name, prefixHidden, strlen(prefixHidden)) == 0)
          {
            name = prefix + name.Substring(strlen(prefixHidden), INT_MAX);
          }
          else if (strnicmp(name, prefixWestHidden, strlen(prefixWestHidden)) == 0)
          {
            if (side != TWest) continue;
            name = prefixWest + name.Substring(strlen(prefixWestHidden), INT_MAX);
          }
          else if (strnicmp(name, prefixEastHidden, strlen(prefixEastHidden)) == 0)
          {
            if (side != TEast) continue;
            name = prefixEast + name.Substring(strlen(prefixEastHidden), INT_MAX);
          }
          else if (strnicmp(name, prefixGuerrilaHidden, strlen(prefixGuerrilaHidden)) == 0)
          {
            if (side != TGuerrila) continue;
            name = prefixGuerrila + name.Substring(strlen(prefixGuerrilaHidden), INT_MAX);
          }
          else if (strnicmp(name, prefixCivilianHidden, strlen(prefixCivilianHidden)) == 0)
          {
            if (side != TCivilian) continue;
            name = prefixCivilian + name.Substring(strlen(prefixCivilianHidden), INT_MAX);
          }
          GameState *state = GWorld->GetGameState();
          int value = toInt((float)state->Evaluate(name, GameState::EvalContext::_default, GWorld->GetMissionNamespace()));
          AddObjective(_overview, s, lSection, value);
          if (value != OSHidden) _objectives = true;
          break; // next section
        }
      }
    }
    if(newInfo.unit && newInfo.unit->GetPerson())
      AddTasks(_overview, lSection, newInfo.unit->GetPerson());
    _overview->AddBreak(lSection, false);

    _overview->FormatSection(lSection);
    _overview->SwitchSection("__OBJECTIVES");
  }

  // statistics
  if (_stats)
  {
    int sSection = _stats->AddSection();
    _stats->AddName(sSection, "__STATISTICS");

    _stats->AddText(sSection, LocalizeString(IDS_BRIEF_STATISTICS), HFH1, HACenter, false, false, "");
    _stats->AddBreak(sSection, false);
    
    _stats->FormatSection(sSection);
    _stats->SwitchSection("__STATISTICS");
  }
}

void DisplayMissionFail::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_INTERRUPT_REVERT:
    if (exit == IDC_INT_LOAD)
    {
      DisplayInterruptRevert *child = static_cast<DisplayInterruptRevert *>(_child.GetRef());
      _selectedSlot = child->GetSaveSlot();
      Display::OnChildDestroyed(idd, exit);
      Exit(IDC_ME_LOAD);
    }
    else if (exit == IDC_INT_RETRY)
    {
      Display::OnChildDestroyed(idd, exit);
      Exit(IDC_ME_RETRY);
    }
    else if (exit == IDC_INT_RESTART)
    {
      Display::OnChildDestroyed(idd, exit);

      MsgBoxButton buttonOK(LocalizeString(IDS_DISP_XBOX_HINT_MISSION_RESTART), INPUT_DEVICE_XINPUT + XBOX_A);
      MsgBoxButton buttonCancel(LocalizeString(IDS_DISP_XBOX_HINT_NO), INPUT_DEVICE_XINPUT + XBOX_B);
      CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_XBOX_QUESTION_RESTART_MISSION), IDD_MSG_RESTART_MISSION, false, &buttonOK, &buttonCancel);
    }
    else
    {
      Display::OnChildDestroyed(idd, exit);
    }
    break;
  case IDD_MSG_RESTART_MISSION:
    Display::OnChildDestroyed(idd, exit);
    if (exit == IDC_OK)
    {
      Exit(IDC_ME_RESTART);
    }
    break;
  default:
    Display::OnChildDestroyed(idd, exit);
    break;
  }
}

void CreateDSInterface()
{
  if (!GWorld->FindDisplay(IDD_DSINTERFACE) &&  !GWorld->FindDisplay(1024) )
  {
    GWorld->AddDisplay(new DisplayDSInterface(NULL), 5.0f);
  }
}


DisplayDSInterface::DisplayDSInterface(ControlsContainer *parent, const char *cls)
: Display(parent)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return

  _lboxMissions = NULL;
  _lboxPlayers = NULL;
  Load(cls);
  _enableSimulation = true;

}

Control *DisplayDSInterface::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_DSI_PLAYERLIST:
  case 1115:
    {
      _lboxPlayers = GetListBoxContainer(ctrl);
      UpdatePlayers();
      if(_lboxPlayers && _lboxPlayers->NRows()>0) _lboxPlayers->SetCurSel(0);
      break;
    }
  case IDC_DSI_MISSONLIST:
  case 1114:
    {
      _lboxMissions = GetListBoxContainer(ctrl);
      UpdateMissionsList();  
      if(_lboxMissions && _lboxMissions->NRows()>0) _lboxMissions->SetCurSel(0);
      break;
    }
  }

  return ctrl;
};

void DisplayDSInterface::OnSimulate(EntityAI *vehicle)
{
  UpdatePlayers();
  Display::OnSimulate(vehicle);
}

void DisplayDSInterface::UpdateMissionsList()
{
  if(!_lboxMissions) return;
  _lboxMissions->Clear();

  if(GetNetworkManager().IsServer() &&  !IsDedicatedServer())
  {
  AutoArray<MPMissionInfo>  missionList;
  LoadMissions(true, missionList);

  for (int i=0; i<missionList.Size(); i++)
  {
    const MPMissionInfo &info = missionList[i];

    int index = _lboxMissions->AddString(info.displayName.GetLocalizedValue());
    _lboxMissions->SetValue(index, i);
    _lboxMissions->SetData(index, info.mission);
  }
}
  else
  {
    const AutoArray<MPMissionInfo> &names = GetNetworkManager().GetServerMissions();
    for (int i=0; i<names.Size(); i++)
    {
      //if (stricmp(names[i].world, name) != 0) continue;
      int index = _lboxMissions->AddString(names[i].displayName.GetLocalizedValue());
      _lboxMissions->SetData(index, names[i].mission + RString(".") + names[i].world);
    }
  }

  _lboxMissions->SortItems();
}

void DisplayDSInterface::UpdatePlayers()
{
  if(!_lboxPlayers) return;
  _lboxPlayers->Clear();
  const AutoArray<PlayerIdentity> &identities = *GetNetworkManager().GetIdentities();
  if (&identities) for (int i=0; i<identities.Size(); i++)
  {
    const PlayerIdentity &identity = identities[i];
    int index = _lboxPlayers->AddString(GetIdentityText(identity));
    _lboxPlayers->SetValue(index, identity.dpnid);
    _lboxPlayers->SetData(index, identity.name);
  }
}

void DisplayDSInterface::DestroyHUD(int exit)
{
  GWorld->RemoveDisplay(this);
}


DisplayDSInterface::~DisplayDSInterface()
{
  GInput.ChangeGameFocus(-1);
}

#if _ENABLE_CHEATS
DisplayDebug::DisplayDebug(ControlsContainer *parent, GameState *gs, const char *cls)
: Display(parent), _vars(false)
{
  GInput.ChangeGameFocus(+1); //call it as early as possible in order not to skip it by some possible return
  _gs = gs;

  LoadHistory();
  _current = _debugInfo.history.Size() - 1;

  Load(cls);

  _enableSimulation = false;
}

DisplayDebug::~DisplayDebug()
{
  SaveHistory();

  GInput.ChangeGameFocus(-1);
}

AutoArray<RString> DebugConsoleLog;
const int DebugHistoryVersion = 1;

//!return true, if text match filtering rule
bool DisplayDebug::FilterLogTest(RString text)
{
  if (_debugInfo.filtrRule.GetLength()<=0 || Matches(text, _debugInfo.filtrRule)) return true;
  return false;
}

void DisplayDebug::UpdateLog(CListBox *lbox)
{//clear current debug log content and insert lines from DebugConsoleLog, if any filter is set, new line must match given pattern 
  lbox->ClearStrings();
  int n = DebugConsoleLog.Size();

  for (int i=0; i<n; i++)
  {
      if(FilterLogTest(DebugConsoleLog[i]))
      {
          lbox->AddString(DebugConsoleLog[i]);
      }
  }
 lbox->SetCurSel(lbox->Size() - 1);
}

LSError DebugConsoleInfo::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeArray("history", history, 1))
  for (int i=0; i<DebugConsoleExpRows; i++)
  { 
    CHECK(ar.Serialize(Format("exp%d", i), exp[i], 1, ""))
  }
  CHECK(ar.Serialize(Format("filter%d", 0), filtrRule, 1, ""))

  return LSOK;
}

void DisplayDebug::LoadHistory()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace

  ParamArchiveLoad file;
  file.Load(GetUserDirectory() + RString("debug.history"), NULL, &globals);
  _debugInfo.Serialize(file);
}

void DisplayDebug::SaveHistory()
{
  // when we are shutting down because we are out of memory, we rather do not save to stay safe
  if (!IsOutOfMemory())
  {
    ParamArchiveSave file(DebugHistoryVersion);
    _debugInfo.Serialize(file);
    file.Save(GetUserDirectory() + RString("debug.history"));
  }
}

Control *DisplayDebug::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_DEBUG_LOG:
    {
      CListBox *lbox = new CListBox(this, idc, cls);
      UpdateLog(lbox);
      return lbox;
    }
  case IDC_DEBUG_EXP:
    {
      CEdit *edit = new CEdit(this, idc, cls);
      _current = _debugInfo.history.Size() - 1;
      edit->SetText(_current >= 0 ? _debugInfo.history[_current] : "");
      return edit;
    }
  case IDC_DEBUG_EXP1:
  case IDC_DEBUG_EXP2:
  case IDC_DEBUG_EXP3:
  case IDC_DEBUG_EXP4:
    {
      CEdit *edit = new CEdit(this, idc, cls);
      edit->SetText(_debugInfo.exp[idc - IDC_DEBUG_EXP1]);
      return edit;
    }
  case IDC_DEBUG_RES1:
  case IDC_DEBUG_RES2:
  case IDC_DEBUG_RES3:
  case IDC_DEBUG_RES4:
    {
      CStatic *text = new CStatic(this, idc, cls);
      // edit->EnableCtrl(false);
      return text;
    }
  case IDC_DEBUG_FILTER_EDIT:
    {//set filtering rule value from displaydebug history (loaded in to _debugInfo) 
      CEdit *edit = new CEdit(this, idc, cls);
      edit->SetText(_debugInfo.filtrRule);
      return edit;
    }
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplayDebug::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_DEBUG_CLEAR_LOG:
      {//clear debug log console and updates displayed List
          DebugConsoleLog.Clear();
          CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_DEBUG_LOG));
          if (lbox)  lbox->ClearStrings();
          break; 
      }
  case IDC_DEBUG_APPLY:
  case IDC_OK:
    // execute expression
    {
      CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_EXP));
      if (edit)
      {
        RString exp = edit->GetText();
        GameState *gstate = GWorld->GetGameState();
        int gshandle = gstate->BeginContext(&_vars);
        if (!gstate->CheckExecute(exp, GWorld->GetMissionNamespace())) // mission namespace
        {
          FocusCtrl(IDC_DEBUG_EXP);
          CreateMsgBox(MB_BUTTON_OK, gstate->GetLastErrorText());
          edit->SetCaretPos(gstate->GetLastErrorPos(exp));
          gstate->EndContext(gshandle);
          return;
        }
        gstate->Execute(exp, GameState::EvalContext::_reportUndefined, GWorld->GetMissionNamespace()); // mission namespace
        gstate->EndContext(gshandle);

        CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_DEBUG_LOG));
        if (lbox) UpdateLog(lbox);
      }
    }
    // continue
  case IDC_CANCEL:
    {
      CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_EXP));
      if (edit)
      {
        RString text = edit->GetText();
        int last = _debugInfo.history.Size() - 1;
        if (last < 0 || _debugInfo.history[last] != text) _debugInfo.history.Add(text);
      }
    }
    
    for (int i=0; i<DebugConsoleExpRows; i++)
    {
      CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_EXP1 + i));
      if (edit)
        _debugInfo.exp[i] = edit->GetText();
    }
    break;
  } 
  
  Display::OnButtonClicked(idc);
}

bool DisplayDebug::OnKeyDown(int dikCode)
{
  if (GetFocused() && GetFocused()->IDC() == IDC_DEBUG_EXP)
  {
    switch (dikCode)
    {
    case DIK_PRIOR:
      if (_current > 0)
      {
        _current--;
        CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_EXP));
        if (edit)
          edit->SetText(_debugInfo.history[_current]);
      }
      return true;
    case DIK_NEXT:
      if (_current < _debugInfo.history.Size() - 1)
      {
        _current++;
        CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_EXP));
        if (edit)
          edit->SetText(_debugInfo.history[_current]);
        
      }
      return true;
    }
  }

  return Display::OnKeyDown(dikCode);
}

void DisplayDebug::OnEditChanged(int idc)
{//when text in FILTER_RULE_EDIT is changed, new filter is assigned lsitBox updated
    if ( idc == IDC_DEBUG_FILTER_EDIT)
    {
        CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_FILTER_EDIT));
        _debugInfo.filtrRule =  edit->GetText();
        CListBox *lbox = dynamic_cast<CListBox *>(GetCtrl(IDC_DEBUG_LOG));
        UpdateLog(lbox);
    }
}

void DisplayDebug::OnDraw(EntityAI *vehicle, float alpha)
{
  GameState *gstate = GWorld->GetGameState();
  int gshandle = gstate->BeginContext(&_vars);
  for (int i=0; i<DebugConsoleExpRows; i++)
  {
    char var[16];
    sprintf(var, "_debug%d", i + 1);
    _vars._vars.Remove(var);
  }
  for (int i=0; i<DebugConsoleExpRows; i++)
  {
    char var[16];
    sprintf(var, "_debug%d", i + 1);
    CEdit *edit = dynamic_cast<CEdit *>(GetCtrl(IDC_DEBUG_EXP1 + i));
    if (!edit) continue;
    RString exp = edit->GetText();
    RString result;
    if (exp.GetLength() > 0)
    {
      if (!gstate->CheckEvaluate(exp, GWorld->GetMissionNamespace())) // mission namespace
      {
        int pos = gstate->GetLastErrorPos(exp);
        Assert(pos >= 0 && pos <= exp.GetLength());
        result =
          RString("Error: ") + gstate->GetLastErrorText() +
          RString(" '") + exp.Substring(0, pos) + RString("|#|") + exp.Substring(pos, INT_MAX) + RString("'");
      }
      else
      {
        GameValue val = gstate->Evaluate(exp, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
        gstate->VarSetLocal(var, val);
        result = val.GetText();
      }
    }
    CStatic *text = dynamic_cast<CStatic *>(GetCtrl(IDC_DEBUG_RES1 + i));
    if (text)
      text->SetText(result);
  }
  gstate->EndContext(gshandle);

  Display::OnDraw(vehicle, alpha);
}

void CreateDebugConsole(GameState *state)
{
  if (!GWorld->FindDisplay(IDD_DEBUG))
  {
    GWorld->AddDisplay(new DisplayDebug(NULL, state), 5.0f);
  }
}


void DisplayDebug::DestroyHUD(int exit)
{
  GWorld->RemoveDisplay(this);
}

#endif

#if _ENABLE_PERFLOG

/// Info about the single scope in the capture
struct ScopeInfo
{
  /// scope name
  RString _name;
  /// additional information
  RString _moreInfo;
  /// start of the scope (from the start of frame) in ms
  float _start;
  /// duration of the scope in ms
  float _duration;
  /// children count
  int _children;
  /// index of the parent scope in the array
  int _parentIndex;
};
TypeIsMovableZeroed(ScopeInfo)

/// Info about all scopes in the thread
struct ScopesInThread
{
  int _threadId;
  int _threadCPU;
  RString _threadName;

  ScopesInThread():_threadId(0),_threadCPU(0){}
  AutoArray<ScopeInfo> _scopes;
};
TypeIsMovable(ScopesInThread)

struct ScopesInThreadTraits
{
  typedef int KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return a == b;}
  /// get a key from an item
  static KeyType GetKey(const ScopesInThread &a) {return a._threadId;}
};

/// Custom control to display time lines
class CTimeLines : public Control
{
  typedef Control base;

protected:
  int _zoom;
  static const int _minZoom = 1;
  /// needs to be a power of 2
  static const int _maxZoom = 8*1024;

  /// analyzed data
  const FindArrayKey<ScopesInThread, ScopesInThreadTraits> &_scopes;
  /// max. time contained in _scopes
  float _maxTime;
  /// index control
  const Ref<CTree> &_index;

  /// scroll bar used by the control
  CScrollBar _scrollbar;
  static const float _sbHeight;

  // colors
  PackedColor _colorLines;
  PackedColor _colorBackground;
  PackedColor _colorBar;
  PackedColor _colorSelection;

  Ref<Font> _font;

  /// name of the highlighted scope
  RString _selectedScope;

public:
  CTimeLines(ControlsContainer *parent, int idc, ParamEntryPar cls,
    const FindArrayKey<ScopesInThread, ScopesInThreadTraits> &scopes,
    const Ref<CTree> &index);

  // handle scrollbar
  virtual void OnMouseMove(float x, float y, bool active = true);
  virtual void OnMouseHold(float x, float y, bool active = true);
  virtual void OnLButtonDown(float x, float y);
  virtual void OnLButtonUp(float x, float y);
  virtual void OnLButtonDblClick(float x, float y);
  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);

  void OnDraw(UIViewport *vp, float alpha);
  virtual void DrawTooltip(UIViewport *vp, float x, float y) override;

  void UpdateMaxTime();
  void SelectScope(RString scope) {_selectedScope = scope;}

  bool CanZoomIn() const {return _zoom < _maxZoom;}
  bool CanZoomOut() const {return _zoom > _minZoom;}
  void ZoomIn();
  void ZoomOut();
  void OnMouseZChanged(float dz)
  {
    if (dz < 0) ZoomOut();
    else if (dz > 0) ZoomIn();
  }

protected:
  /// set scroll bar parameters bases on _zoom
  void UpdateScrollbar();
  /// recursive traversal of index with drawing of leaf scopes
  void DrawScopes(UIViewport *vp, float alpha, const CTreeItem *parent, const AutoArray<ScopeInfo> &scopes,
    const Rect2DPixel &rect, float timeMin, float timeMax);

  /// convert time to the scrollbar position
  float TimeToPos(float time) const;
  /// convert the scrollbar position to time
  float PosToTime(float pos) const;
  /// find scope info on given coordinates
  CTreeItem *FindScope(float x, float y, float *time = NULL, const ScopeInfo **info = NULL);
};

const float CTimeLines::_sbHeight = 0.03f;

struct ColorDesc
{
  Color color;
  const char *texture;
};
static const ColorDesc ScopeColors[] =
{
  #define COLOR_DESC(r,g,b,a) {Color(r, g, b, a),"#(rgb,1,1,1)color(" #r "," #g "," #b "," #a ")"}
  COLOR_DESC(1, 0, 0, 1),
  COLOR_DESC(0, 1, 0, 1),
  COLOR_DESC(0, 0, 1, 1),
  COLOR_DESC(1, 1, 0, 1),
  COLOR_DESC(1, 0, 1, 1),
  COLOR_DESC(0, 1, 1, 1),
  COLOR_DESC(1, 1, 1, 1),
};

CTimeLines::CTimeLines(ControlsContainer *parent, int idc, ParamEntryPar cls,
  const FindArrayKey<ScopesInThread, ScopesInThreadTraits> &scopes,
  const Ref<CTree> &index)
: Control(parent, CT_USER, idc, cls), _scopes(scopes), _index(index), _scrollbar(false, true, cls >> "Scrollbar")
{
  _zoom = 1;

  _colorLines = GetPackedColor(cls >> "colorLines");
  _colorBackground = GetPackedColor(cls >> "colorBackground");
  _colorBar = GetPackedColor(cls >> "colorBar");
  _colorSelection = GetPackedColor(cls >> "colorSelection");

  _font = GEngine->LoadFont(GetFontID(cls >> "font"));

  // set up the scroll bar
  _scrollbar.SetPosition(_x, _y + SCALED(_h - _sbHeight), SCALED(_w), SCALED(_sbHeight));
  _scrollbar.SetColor(PackedBlack);
  UpdateScrollbar();
}

void CTimeLines::UpdateMaxTime()
{
  // find the upper bound for time
  _maxTime = 0;
  for (int i=0; i<_scopes.Size(); i++)
  {
    const AutoArray<ScopeInfo> &scopes = _scopes[i]._scopes;
    for (int j=0; j<scopes.Size(); j++) saturateMax(_maxTime, scopes[j]._start + scopes[j]._duration);
  }
}

float CTimeLines::TimeToPos(float time) const
{
  float timeMin = _maxTime * _scrollbar.GetPos() / _zoom;
  float timeMax = _maxTime * (_scrollbar.GetPos() + 1.0f) / _zoom;
  float left = _x + SCALED(0.005f);
  float wTotal = SCALED(_w - 0.01f);
  float pos = left + wTotal * (time - timeMin) / (timeMax - timeMin);
  saturate(pos, left, left + wTotal);
  return pos;
}

float CTimeLines::PosToTime(float pos) const
{
  float timeMin = _maxTime * _scrollbar.GetPos() / _zoom;
  float timeMax = _maxTime * (_scrollbar.GetPos() + 1.0f) / _zoom;
  float left = _x + SCALED(0.005f);
  float wTotal = SCALED(_w - 0.01f);
  float time = timeMin + (timeMax - timeMin) * (pos - left) / wTotal;
  saturate(time, timeMin, timeMax);
  return time;
}

void CTimeLines::OnMouseMove(float x, float y, bool active)
{
  if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
  {
    _scrollbar.OnMouseHold(x, y);
  }
  else
  {

  }
}

void CTimeLines::OnMouseHold(float x, float y, bool active)
{
  if (_scrollbar.IsEnabled() && _scrollbar.IsLocked())
  {
    _scrollbar.OnMouseHold(x, y);
  }
  else
  {

  }
}

void CTimeLines::OnLButtonDown(float x, float y)
{
  if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y))
  {
    _scrollbar.OnLButtonDown(x, y);
  }
  else
  {
    CTreeItem *item = FindScope(x, y);
    if (item) _index->SetSelected(item);
  }
}

void CTimeLines::OnLButtonUp(float x, float y)
{
  if (_scrollbar.IsLocked())
  {
    _scrollbar.OnLButtonUp(x, y);
  }
  else if (_scrollbar.IsInside(x,y)) 
  {
    // nothing to do
  }
  else
  {
  }
}

void CTimeLines::OnLButtonDblClick(float x, float y)
{
  if (_scrollbar.IsLocked())
  {
    // nothing to do
  }
  else if (_scrollbar.IsInside(x,y)) 
  {
    // nothing to do
  }
  else
  {
    CTreeItem *item = FindScope(x, y);
    if (item && _parent)
    {
      // translate to double-click to the tree
      _parent->OnTreeDblClick(IDC_CAPTURE_INDEX, item);
      // _index->Expand(item, true);
    }
  }
}

bool CTimeLines::OnKeyDown(int dikCode)
{
  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXLeft:
    {
      float pos = _scrollbar.GetPos();
      pos -= 0.2f;
      _scrollbar.SetPos(pos);
    }
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXRight:
    {
      float pos = _scrollbar.GetPos();
      pos += 0.2f;
      _scrollbar.SetPos(pos);
    }
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYUp:
    ZoomIn();
    return true;
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYDown:
    ZoomOut();
    return true;
  default:
    return base::OnKeyDown(dikCode);
  }
}

bool CTimeLines::OnKeyUp(int dikCode)
{
  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXLeft:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXRight:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYUp:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYDown:
    return true;
  default:
    return base::OnKeyUp(dikCode);
  }
}

/// find recursively the scope containing given timestamp
static CTreeItem *FindScope(const CTreeItem *parent, const AutoArray<ScopeInfo> &scopes, float time)
{
  // check the sub-scopes
  for (int i=0; i<parent->children.Size(); i++)
  {
    CTreeItem *item = parent->children[i];
    // check if inside the current scope
    const ScopeInfo &info = scopes[item->value];
    if (time < info._start || time > info._start + info._duration) continue;
    if (item->IsExpanded())
    {
      CTreeItem *child = FindScope(item, scopes, time);
      if (child) return child;
    }
    return item; // the current item is the inner most containing the time
  }
  return NULL;
}

CTreeItem *CTimeLines::FindScope(float x, float y, float *time, const ScopeInfo **info)
{
  const CTreeItem *root = _index->GetRoot();
  if (!(root && root->IsExpanded())) return NULL;
  
  // find the thread
  float hTotal = _h - _sbHeight;
  float hThread = hTotal / root->children.Size();
  if (hThread <= 0.01f) return NULL;
  int row = toIntFloor((y - _y) / hThread);
  if (row < 0 || row >= root->children.Size()) return NULL;
  const CTreeItem *item = root->children[row];
  if (!item->IsExpanded()) return NULL;

  // convert position to time
  float t = PosToTime(x);
  if (time) *time = t;

  // find the scope
  int index = item->value;
  const AutoArray<ScopeInfo> &scopes = _scopes[index]._scopes;
  CTreeItem *ret = ::FindScope(item, scopes, t);
  if (ret && info) *info = &scopes[ret->value];
  return ret;
}

void CTimeLines::DrawTooltip(UIViewport *vp, float x, float y)
{
  if (!IsInside(x, y)) return;
  if (_scrollbar.IsLocked()) return;
  if (_scrollbar.IsEnabled() && _scrollbar.IsInside(x, y)) return;

  const ScopeInfo *info = NULL;
  float time = 0;
  FindScope(x, y, &time, &info);

  RString name;
  if (info) name = info->_name;

  _tooltip = Format("%.3f ms %s", time, cc_cast(name));
  base::DrawTooltip(vp, x, y);
}

void CTimeLines::DrawScopes(UIViewport *vp, float alpha, const CTreeItem *parent, const AutoArray<ScopeInfo> &scopes,
                       const Rect2DPixel &rect, float timeMin, float timeMax)
{
  for (int i=0; i<parent->children.Size(); i++)
  {
    const CTreeItem *item = parent->children[i];

    // Drawing
    {
      const ScopeInfo &info = scopes[item->value];
      float start = info._start;
      float end = info._start + info._duration;
      if (start < timeMax && end > timeMin)
      {
        if (info._duration <= (timeMax-timeMin)*0.0001f) continue; // too short scope
        //if (info._duration <= 0.0005f) continue; // too short scope
        // left line
        float left;
        if (start >= timeMin)
        {
          // convert from timeMin ... timeMax to rect.x ... rect.x + rect.w
          left = rect.x + rect.w * (start - timeMin) / (timeMax - timeMin);
          Line2DPixel line(left, rect.y, left, rect.y + rect.h + 1);
          vp->DrawLine(line, _colorLines, _colorLines);
        }
        else
        {
          start = timeMin;
          left = rect.x;
        }
        // right line
        float right;
        if (end <= timeMax)
        {
          // convert from timeMin ... timeMax to rect.x ... rect.x + rect.w
          right = rect.x + rect.w * (end - timeMin) / (timeMax - timeMin);
          Line2DPixel line(right, rect.y, right, rect.y + rect.h + 1);
          vp->DrawLine(line, _colorLines, _colorLines);
        }
        else
        {
          end = timeMax;
          right = rect.x + rect.w;
        }
        // top and bottom line
        {
          Line2DPixel line(left, rect.y, right, rect.y);
          vp->DrawLine(line, _colorLines, _colorLines);
        }
        {
          Line2DPixel line(left, rect.y + rect.h, right, rect.y + rect.h);
          vp->DrawLine(line, _colorLines, _colorLines);
        }
        // color bar
        unsigned int colorIndex = CalculateStringHashValue(info._name);
        PackedColor color = PackedColor(ScopeColors[colorIndex % lenof(ScopeColors)].color);
        Rect2DPixel bar;
        bar.x = left + 1;
        bar.y = rect.y + 1;
        bar.w = right - left - 1;
        bar.h = rect.h - 1;
        Draw2DParsExt pars;
        pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        pars.SetColor(color);
        pars.Init();
        pars.spec|= (_shadow == 2)?UISHADOW : 0; 
        vp->Draw2D(pars, bar);
        // highlighting
        if (stricmp(_selectedScope, item->data) == 0)
        {
          const int w = GEngine->Width2D();

          pars.SetColor(_colorSelection);
          Rect2DPixel selection(left, rect.y - CW(SCALED(0.005f)) + 1, floatMax(right - left, 1), CW(SCALED(0.005f)) - 1);
          vp->Draw2D(pars, selection);
          selection.y = rect.y + rect.h;
          vp->Draw2D(pars, selection);
        }
        // text (scope name)
        if (!item->IsExpanded() || item->children.Size() == 0) // only in leaves
        {
          TextDrawAttr attr;
          attr._size = floatMin(rect.h / GEngine->Height2D(), 0.03);
          attr._font = _font;
          attr._color = _colorLines;
          attr._shadow = _shadow;

          float w = GEngine->Width2D() * vp->GetTextWidth(attr, info._name);
          if (w < right - left)
          {
            // the text fits to bar, draw it
            Point2DPixel pos;
            pos.x = 0.5 * (right + left - w);
            pos.y = rect.y + 0.5 * (rect.h - GEngine->Height2D() * attr._size);
            vp->DrawText(pos, attr, info._name);
          }
        }
      }
    }
    
    if (item->IsExpanded() && item->children.Size() > 0)
    {
      // traverse children
      DrawScopes(vp, alpha, item, scopes, rect, timeMin, timeMax);
    }
  }
}

void CTimeLines::OnDraw(UIViewport *vp, float alpha)
{
  // update the position of the scrollbar
  _scrollbar.SetPosition(_x, _y + SCALED(_h - _sbHeight), SCALED(_w), SCALED(_sbHeight));

  // make sure _maxTime will be valid during the next frame
  UpdateMaxTime();

  const int w = GLOB_ENGINE->Width2D();
  const int h = GLOB_ENGINE->Height2D();

  // background
  Rect2DPixel rect;
  rect.x = CX(_x);
  rect.y = CY(_y);
  rect.w = CW(SCALED(_w));
  rect.h = CH(SCALED(_h));
  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  pars.SetColor(_colorBackground);
  pars.Init();
  pars.spec|= (_shadow == 2)?UISHADOW : 0; 
  vp->Draw2D(pars, rect);

  // scrollbar
  _scrollbar.OnDraw(vp, alpha);

  // traversing of scopes using the index
  const CTreeItem *root = _index->GetRoot();
  if (root && root->IsExpanded() && root->children.Size()>0)
  {
    // scopes
    float hTotal = _h - _sbHeight;
    float hThread = hTotal / root->children.Size();
    if (hThread > 0.01f)
    {
      float left = _x + SCALED(0.005f);
      float wTotal = SCALED(_w - 0.01f);

      rect.x = CX(left);
      rect.w = CW(wTotal);
      rect.h = CH(SCALED(hThread - 0.01f));

      // minimal and maximal time to show
      float timeMin = _maxTime * _scrollbar.GetPos() / _zoom;
      float timeMax = _maxTime * (_scrollbar.GetPos() + 1.0f) / _zoom;

      // find in which thread selected item is
      const CTreeItem *selected = _index->GetSelected();
      const CTreeItem *selectedThread = NULL;
      if (_selectedScope.GetLength() == 0 && selected && selected->level > 1) // different highlighting with _selectedScope
      {
        // find the level 1 item (identifying the thread)
        selectedThread = selected;
        while (selectedThread->level > 1) selectedThread = selectedThread->parent; 
      }

      float top = _y;
      for (int i=0; i<root->children.Size(); i++)
      {
        // threads separator
        if (i > 0)
        {
          Line2DFloat line(_x, top, _x + SCALED(_w), top);
          vp->DrawLine(line, _colorLines, _colorLines);
        }

        // bar background (reuse rect, pars)
        rect.y = CY(top + SCALED(0.005f));
        pars.SetColor(_colorBar);
        vp->Draw2D(pars, rect);

        // traverse tree, draw visible scopes
        const CTreeItem *item = root->children[i];
        if (item->IsExpanded())
        {
          int index = item->value;
          const AutoArray<ScopeInfo> &scopes = _scopes[index]._scopes;
          // recursive handling of children
          DrawScopes(vp, alpha, item, scopes, rect, timeMin, timeMax);
          // draw selected item
          if (item == selectedThread)
          {
            const ScopeInfo &info = scopes[selected->value];
            float start = info._start;
            float end = info._start + info._duration;
            if (start < timeMax && end > timeMin)
            {
              saturateMax(start, timeMin);
              saturateMin(end, timeMax);
              float left = rect.x + rect.w * (start - timeMin) / (timeMax - timeMin);
              float right = rect.x + rect.w * (end - timeMin) / (timeMax - timeMin);

              pars.SetColor(_colorSelection);
              Rect2DPixel selection(left, CY(top), floatMax(right - left, 1), CW(SCALED(0.005f)) - 1);
              vp->Draw2D(pars, selection);
              selection.y = CY(top + SCALED(hThread - 0.005f)) + 1;
              vp->Draw2D(pars, selection);
            }
          }
        }

        top += SCALED(hThread);
      }
    }
  }
}

void CTimeLines::ZoomIn()
{
  // try to keep the original time under the cursor
  if (CanZoomIn())
  {
    // what time mouse is pointing to
#ifdef _XBOX
    float mouseX = _x + 0.5 * _w;
#else
    float mouseX = 0.5 + GInput.cursorX * 0.5;
#endif
    float time = PosToTime(mouseX);
    
    _zoom = _zoom * 2;
    UpdateScrollbar();
    
    // what time we need to scroll
    float timeOffset = time - PosToTime(mouseX);
    float pos = _zoom * timeOffset / _maxTime;
    _scrollbar.SetPos(pos);
  }
}

void CTimeLines::ZoomOut()
{
  if (CanZoomOut())
  {
    // what time mouse is pointing to
#ifdef _XBOX
    float mouseX = _x + 0.5 * _w;
#else
    float mouseX = 0.5 + GInput.cursorX * 0.5;
#endif
    float time = PosToTime(mouseX);

    _zoom = _zoom / 2;
    UpdateScrollbar();

    // what time we need to scroll
    float timeOffset = time - PosToTime(mouseX);
    float pos = _zoom * timeOffset / _maxTime;
    _scrollbar.SetPos(pos);
  }
}

void CTimeLines::UpdateScrollbar()
{
  _scrollbar.SetRangeAndPos(0, _zoom, 1, 0); 
  _scrollbar.SetSpeed(0.1f, 1.0f); 
}

#include <El/Clipboard/clipboard.hpp>

/// Frame capture diagnostics display
class DisplayCapture : public Display
{
protected:
  Ref<CTree> _index;
  Ref<CTimeLines> _timeLines;
  /// detailed info about the selected scope
  InitPtr<ITextContainer> _info;

  /// analyzed data
  FindArrayKey<ScopesInThread, ScopesInThreadTraits> _scopes;
  /// name of the highlighted scope
  RString _selectedScope;
  /// _counters
  AutoArray<PerfCounterSaved> _counters;

public:
  DisplayCapture(ControlsContainer *parent, const RString &input = RString());
  ~DisplayCapture();

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);

  void OnButtonClicked(int idc);
  void OnTreeSelChanged(IControl *ctrl);
  void OnTreeDblClick(int idc, CTreeItem *sel);

  virtual bool OnKeyDown(int dikCode);
  virtual bool OnKeyUp(int dikCode);

  void DestroyHUD(int exit);

  /// analyze the captured frame, store it to the "scopes" structure
  static void Analyze(FindArrayKey<ScopesInThread, ScopesInThreadTraits> &scopes, AutoArray<PerfCounterSaved> &counters);
  /// export complete info
  static RString ExportAll(FindArrayKey<ScopesInThread, ScopesInThreadTraits> &scopes);
protected:
  /// expand tree to show all occurrences of the given scope
  void SelectScope(RString scope);
  /// export info about the selected scope and all subscope to the clipboard
  void ExportSelected();
  /// export info about one thread / selection scope and all subscope to the clipboard
  static RString ExportSelection(const AutoArray<ScopeInfo> &scopes, int index);
  /// import a result of the export
  void ImportAll(const RString &src);
  /// create the index tree
  void CreateIndex();
  void UpdateButtons();
};

DisplayCapture::DisplayCapture(ControlsContainer *parent, const RString &input)
: Display(parent)
{
  GInput.ChangeGameFocus(+1);

  Load("RscDisplayCapture");
  if (input.IsEmpty())
  {
    Analyze(_scopes, _counters);
    CreateIndex();
  }
  else
  {
    ImportAll(input);
  }
  UpdateButtons();
  _enableSimulation = false;
}

DisplayCapture::~DisplayCapture()
{
  GInput.ChangeGameFocus(-1);
};

Control *DisplayCapture::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  if (idc == IDC_CAPTURE_TIMELINES)
  {
    // special custom control
    _timeLines = new CTimeLines(this, idc, cls, _scopes, _index);
    return _timeLines;
  }

  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_CAPTURE_INDEX:
    _index = dynamic_cast<CTree *>(ctrl);
    break;
  case IDC_CAPTURE_INFO:
    _info = GetTextContainer(ctrl);
    break;
  }

  return ctrl;
}

void DisplayCapture::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_CAPTURE_ZOOMIN:
    if (_timeLines) _timeLines->ZoomIn();
    UpdateButtons();
    break;
  case IDC_CAPTURE_ZOOMOUT:
    if (_timeLines) _timeLines->ZoomOut();
    UpdateButtons();
    break;
  case IDC_CAPTURE_EXPORT:
    ExportSelected();
    break;
  case IDC_CAPTURE_EXPORT_ALL:
    {
      RString output = ExportAll(_scopes);
      ExportToClipboard(output, output.GetLength());
    }
    break;
  case IDC_CAPTURE_IMPORT_ALL:
    {
      // read clipboard and parse it line by line
      RString src = ImportFromClipboard();
      ImportAll(src);
    }
    break;
  default:
    Display::OnButtonClicked(idc);
    break;
  }
}

void DisplayCapture::OnTreeSelChanged(IControl *ctrl)
{
  SelectScope(RString());
}

void DisplayCapture::OnTreeDblClick(int idc, CTreeItem *sel)
{
  if (idc == IDC_CAPTURE_INDEX && sel)
  {
    if (sel->level > 1 && (GInput.keys[DIK_LSHIFT] || GInput.keys[DIK_RSHIFT]))
    {
      SelectScope(sel->data);
    }
    else
    {
      SelectScope(RString());
      sel->Expand(!sel->IsExpanded());
    }
  }
  else
    Display::OnTreeDblClick(idc, sel);
}

bool DisplayCapture::OnKeyDown(int dikCode)
{
  // redirect right thumb to time line
  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXLeft:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXRight:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYUp:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYDown:
    if (_timeLines) return _timeLines->OnKeyDown(dikCode);
    break;
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumb:
    // equal to Shift + double click
    if (_index)
    {
      CTreeItem *sel = _index->GetSelected();
      if (sel && sel->level > 1)
      {
        SelectScope(sel->data);
      }
    }
    return true;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    // different handling of expanding
    if (_index)
    {
      CTreeItem *selected = _index->GetSelected();
      if (selected)
      {
        if (!selected->IsExpanded()) _index->Expand(selected, true);
        if (selected->level >= 1)
        {
          // select the longest scope
          // parent on level 1 (thread)
          const CTreeItem *selectedThread = selected;
          while (selectedThread->level > 1) selectedThread = selectedThread->parent; 
          // find the thread
          int index = selectedThread->value;
          if (index >= 0 && index < _scopes.Size())
          {
            const AutoArray<ScopeInfo> &scopes = _scopes[index]._scopes;
            // find the best scope
            CTreeItem *best = NULL;
            float maxDuration = 0;
            for (int i=0; i<selected->children.Size(); i++)
            {
              CTreeItem *item = selected->children[i];
              if (!item) continue;
              int index = item->value;
              if (index >= scopes.Size()) continue;
              const ScopeInfo &info = scopes[index];
              if (info._duration > maxDuration)
              {
                best = item;
                maxDuration = info._duration;
              }
            }
            if (best) _index->SetSelected(best);
          }
        }
      }
    }
    return true;
  }
  return Display::OnKeyDown(dikCode);
}

bool DisplayCapture::OnKeyUp(int dikCode)
{
  // redirect right thumb to time line
  switch (dikCode)
  {
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXLeft:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbXRight:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYUp:
  case INPUT_DEVICE_XINPUT + XBOX_RightThumbYDown:
    if (_timeLines) return _timeLines->OnKeyUp(dikCode);
    break;
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumb:
    return true;
  case DIK_RIGHT:
  case INPUT_DEVICE_XINPUT + XBOX_Right:
  case INPUT_DEVICE_XINPUT + XBOX_LeftThumbXRight:
    return true;
  }
  return Display::OnKeyUp(dikCode);
}

static bool SelectScope(RString scope, CTreeItem *parent)
{
  bool found = false;
  for (int i=0; i<parent->children.Size(); i++)
  {
    // traverse tree, search for the scope
    CTreeItem *item = parent->children[i];
    if (stricmp(item->data, scope) == 0 || SelectScope(scope, item)) found = true;
  }
  if (found) parent->Expand(true);
  return found;
}

void DisplayCapture::SelectScope(RString scope)
{
  // store the selected scope
  _selectedScope = scope;
  if (_timeLines) _timeLines->SelectScope(scope);

  // expand the tree
  if (_index)
  {
    if (scope.GetLength() > 0)
    {
      // traversing of scopes using the index
      CTreeItem *root = _index->GetRoot();
      bool found = false;
      if (root)
      {
        for (int i=0; i<root->children.Size(); i++)
        {
          // traverse tree, search for the scope
          CTreeItem *item = root->children[i];
          // if anything inside was expanded, expand this level as well
          if (::SelectScope(scope, item)) found = true;
        }
        if (found) root->Expand(true);
      }
      if (_info)
      {
        // info about the total time spent in the scope
        float totalTime = 0;
        float maxTime = 0;
        int count = 0;
        for (int i=0; i<_scopes.Size(); i++)
        {
          const AutoArray<ScopeInfo> &infos = _scopes[i]._scopes;
          for (int j=0; j<infos.Size(); j++)
          {
            const ScopeInfo &info = infos[j];
            if (stricmp(info._name, scope) == 0)
            {
              totalTime += info._duration;
              count++;
              saturateMax(maxTime, info._duration);
            }
          }
        }
        // also check counter information as recorded (this improves handling of skipped scopes)
        RString addText;
        for (int i=0; i<_counters.Size(); i++)
        {
          if (stricmp(_counters[i]._name, scope)==0)
          {
            if (_counters[i].count>count)
            {
              addText = Format(" (%d: %.3f ms, avg %.3f ms)", _counters[i].count, _counters[i].duration, _counters[i].duration/_counters[i].count);
      }
            break;
    }
        }
        if (count>0)
          _info->SetText(Format("%d x %s: %.3f ms (max %.3f ms, avg %.3f ms)", count, cc_cast(scope), totalTime, maxTime, totalTime / count)+addText);
    else
          _info->SetText(addText); // count==0 and _counters[i].count>0 unlikely, but possible
      } 
    }
    else
    {
      // selected item
      const CTreeItem *selected = _index->GetSelected();
      if (selected && selected->level >= 1)
      {
        if (selected->level == 1)
        {
          // info about the thread
          int index = selected->value;
          if (index < 0 || index >= _scopes.Size())
          {
            // wrong thread
            _info->SetText(RString());
          }
          else
          {
            _info->SetText(Format("Thread %s on processor #%d", cc_cast(_scopes[index]._threadName), _scopes[index]._threadCPU));
          }
        }
        else
        {
          // parent on level 1 (thread)
          const CTreeItem *selectedThread = selected;
          while (selectedThread->level > 1) selectedThread = selectedThread->parent; 
          // find the thread
          int index = selectedThread->value;
          if (index < 0 || index >= _scopes.Size())
          {
            // wrong thread
            _info->SetText(RString());
          }
          else
          {
            const AutoArray<ScopeInfo> &scopes = _scopes[index]._scopes;

            // find the scope 
            index = -1;
            if (selected->level > 1) index = selected->value;
            if (index >= scopes.Size())
            {
              // wrong scope
              _info->SetText(RString());
            }
            else
            {
              const ScopeInfo &info = scopes[index];
              _info->SetText(Format(
                "%s (%.3f ms : %.3f ms) %s", cc_cast(info._name), info._start, info._duration, cc_cast(info._moreInfo)
              ));
            }
          }
        }
      }
      else
      {
        // no info available
        _info->SetText(RString());
      }
    }
  }
  else
  {
    // no index, no info
    if (_info) _info->SetText(RString());
  }
}

void DisplayCapture::ExportSelected()
{
  if (!_index) return;
  // selected item
  const CTreeItem *selected = _index->GetSelected();
  if (!selected || selected->level < 1) return;
  // parent on level 1 (thread)
  const CTreeItem *selectedThread = selected;
  while (selectedThread->level > 1) selectedThread = selectedThread->parent; 
  
  // find the thread
  int threadIndex = selectedThread->value;
  if (threadIndex < 0 || threadIndex >= _scopes.Size()) return;
  const AutoArray<ScopeInfo> &scopes = _scopes[threadIndex]._scopes;

  // find the scope 
  int index = -1;
  if (selected->level > 1) index = selected->value;
  if (index >= scopes.Size()) return;
  
  RString output = ExportSelection(_scopes[threadIndex]._scopes, index);
  ExportToClipboard(output, output.GetLength());
}

RString DisplayCapture::ExportSelection(const AutoArray<ScopeInfo> &scopes, int index)
{
  RString output;
  float maxTime = index < 0 ? FLT_MAX : scopes[index]._start + scopes[index]._duration;
  saturateMax(index, 0);
  for (int i=index; i<scopes.Size(); i++)
  {
    const ScopeInfo &info = scopes[i];
    if (i > index)
    {
      float end = info._start + info._duration;
      if (end > maxTime) break;
    }
    output = output + Format(
      "%s; %.5f; %.5f;\"%s\"\r\n", cc_cast(info._name), info._start, info._duration, cc_cast(info._moreInfo)
    );
  }
  return output;
}

RString DisplayCapture::ExportAll(FindArrayKey<ScopesInThread, ScopesInThreadTraits> &scopes)
{
  RString output;
  for (int i=0; i<scopes.Size(); i++)
  {
    output = output + Format("* Thread %s\n",cc_cast(scopes[i]._threadName));
    output = output + ExportSelection(scopes[i]._scopes, -1);
  }
  return output;
}

void DisplayCapture::ImportAll(const RString &src)
{
  // All / partical capture
  _scopes.Clear();
  _selectedScope = RString();
  _counters.Clear();

  // When All is used, * Thread %s is expected before each thread
  bool threadIdUsed = false;
  _scopes.Append()._threadName = "Copy";

  // parse items one by one
  // lines, items on a line are semicolon delimited
  QIStrStream in(src.Data(),src.GetLength());
  for(;;)
  {
    char line[1024];
    bool last = !in.readLine(line,sizeof(line));
    if (line[0]!=0)
    {
      if (line[0]=='*')
      {
        if (threadIdUsed)
        {
          _scopes.Append();
          threadIdUsed = false;
        }
        static const char threadPrefix[] = "* Thread ";
        if (strnicmp(line,threadPrefix,sizeof(threadPrefix)-1)==0)
        {
          _scopes.Last()._threadName = line+sizeof(threadPrefix)-1;
        }
        continue;
      }
    char name[1024];
    char startS[1024];
    char durationS[1024];
    char extInfo[1024];
    // example line:
    // total; 0.017; 33.854;""
      int scanned = sscanf(line,"%[^;];%[^;];%[^;];%s",name,startS,durationS,extInfo);
    // TODO: strip quotes from extInfo
    // caution: extInfo sometimes multiline (oversight in the Export implementation)
      if (scanned!=4) continue;
      // populate slots

      ScopeInfo &info = _scopes.Last()._scopes.Append();
      info._name = name;
      info._parentIndex = -1;
      info._start = atof(startS);
      info._duration = atof(durationS);
      if (extInfo[0]=='"' && strlen(extInfo)>2 && extInfo[strlen(extInfo)-1]=='"')
      {
        info._moreInfo = RString(extInfo+1,strlen(extInfo)-2);
      }
      info._children = 0;
      threadIdUsed = true;
    }
    // TODO: handle nesting
    if (last) break;
  }

  // for each item find all within it - those are its children
  for (int s=0; s<_scopes.Size(); s++)
  {
    ScopesInThread &scopes = _scopes[s];
    for (int i=0; i<scopes._scopes.Size(); i++)
    {
      //float startI = scopes._scopes[i]._start;
      float endI = scopes._scopes[i]._start+scopes._scopes[i]._duration;
      for (int j=i+1; j<scopes._scopes.Size(); j++)
      {
        float startJ = scopes._scopes[j]._start;
        float endJ = scopes._scopes[j]._start+scopes._scopes[j]._duration;
        // detect overlap: more inside or more outside?
        float inside = endI-startJ;
        float outside = endJ-endI;
        if (outside>inside) // out of the scope -- what about floating point precision?
        {
          break;
        }
        scopes._scopes[j]._parentIndex = i;
      }
    }
  }

  // once all parents are resolved, count children
  for (int s=0; s<_scopes.Size(); s++)
  {
    ScopesInThread &scopes = _scopes[s];
    for (int i=0; i<scopes._scopes.Size(); i++)
    {
      int parent = scopes._scopes[i]._parentIndex;
      if (parent>=0) scopes._scopes[parent]._children++;
    }
  }
  if (_index)
  {
    _index->RemoveAll();
    CreateIndex();
  }
}

void DisplayCapture::UpdateButtons()
{
  IControl *ctrl = GetCtrl(IDC_CAPTURE_ZOOMIN);
  if (ctrl) ctrl->EnableCtrl(_timeLines && _timeLines->CanZoomIn());
  ctrl = GetCtrl(IDC_CAPTURE_ZOOMOUT);
  if (ctrl) ctrl->EnableCtrl(_timeLines && _timeLines->CanZoomOut());
}

static int CmpScopeInfo(const ScopeInfo *info1, const ScopeInfo *info2)
{
  float diff = info1->_start - info2->_start;
  return sign(diff);
}

struct ScopesStackItem
{
  int _index;
  float _end;

  ScopesStackItem(int index, int end) : _index(index), _end(end) {}
};
TypeIsSimple(ScopesStackItem)

typedef InitValT<float,int,0> floatInit0;
TypeIsSimpleZeroed(floatInit0)

void DisplayCapture::Analyze(FindArrayKey<ScopesInThread, ScopesInThreadTraits> &scopes, AutoArray<PerfCounterSaved> &counters)
{
  // access the buffer
  __int64 frameDuration;
  
  const AutoArray<CaptureBufferItem> &buffer = GPerfProfilers.GetCapturedFrame(frameDuration, counters);
  if (buffer.Size() == 0) return;

  float frameDurationMs = frameDuration * 0.01f / (float)PROF_COUNT_SCALE / (1 << PROF_ACQ_SCALE_LOG);
  // categorize scopes by threads
  AutoArray<floatInit0> lastStart;
  lastStart.Resize(scopes.Size());
  for (int i=0; i<buffer.Size(); i++)
  {
    const CaptureBufferItem &item = buffer[i];
    if (item._slotIndex<0) continue;
    const PerfCounterSlot *counterSlot = GPerfProfilers.Slot(item._slotIndex);
    if (!counterSlot) continue;

    // find the thread array
    int threadId = item._threadId;
    int slot = scopes.FindKey(threadId);
    if (slot < 0)
    {
      slot = scopes.Add();
      lastStart.Resize(scopes.Size());
      scopes[slot]._threadId = threadId;
      scopes[slot]._threadName = GetThreadName(threadId);
      scopes[slot]._threadCPU = GetThreadCPU(threadId);
    }
    
    // convert times to ms
    float coef = 0.01f / (float)counterSlot->scale/* / (1 << PROF_ACQ_SCALE_LOG)*/;
    float duration = coef * item._duration;
    float start = coef * item._start;
    
    // ignore very short scopes, but do not ignore such which do not follow another scope shortly
    // such scopes are important to see what is happening during long parent scopes
    // also any scope with "more info" is always important
    if (item._moreInfo.IsEmpty())
    {
      // too short scope - absolute
      if (duration <= 0.002f && start-lastStart[slot]<0.01f) continue;
      
      // too short scope - relative
      if (duration <= frameDurationMs * 0.00001f && start-lastStart[slot]<frameDurationMs *0.0001f) continue;
    }
    
    lastStart[slot] = start;
    // add the new scope
    ScopeInfo &info = scopes[slot]._scopes.Append();
    info._name = cc_cast(counterSlot->_name);
    info._parentIndex = -1;
    info._start = start;
    // clip the end - ignore what is after this frame
    info._duration = floatMin(duration,frameDurationMs-info._start);
    // empty string is commonly assigned from other threads as well?
    info._moreInfo = item._moreInfo;
    info._children = 0;
  }
  scopes.Compact();

  // analyze scopes in thread
  for (int i=0; i<scopes.Size(); i++)
  {
    // sort scopes by the begin time
    AutoArray<ScopeInfo> &infos = scopes[i]._scopes;
    infos.Compact();
    QSort(infos.Data(), infos.Size(), CmpScopeInfo);

    // stack will contain stopper
    AutoArray< ScopesStackItem, MemAllocLocal<ScopesStackItem,64> > stack;
    stack.Add(ScopesStackItem(-1, INT_MAX));

    // hierarchy of scopes
    for (int j=0; j<infos.Size(); j++)
    {
      float end = infos[j]._start + infos[j]._duration;
      // find the parent on the stack
      int k = stack.Size() - 1;
      while (k>=0 && end > stack[k]._end) k--;
      // add item to the stack
      stack.Resize(k + 2);
      stack[k + 1]._index = j;
      stack[k + 1]._end = end;
      // update the parent index
      int parent = stack[k]._index;
      infos[j]._parentIndex = parent;
      if (parent>=0)
      {
        // increment the parent's children count
        infos[stack[k]._index]._children++;
      }
    }
  }
}

struct TreeStackItem
{
  int _index;
  CTreeItem *_item;

  TreeStackItem(int index, CTreeItem *item) : _index(index), _item(item) {}
};
TypeIsSimple(TreeStackItem)

void DisplayCapture::CreateIndex()
{
  if (!_index) return;

  CTreeItem *root = _index->GetRoot();
  for (int i=0; i<_scopes.Size(); i++)
  {
    // item for each thread 
    CTreeItem *item = root->AddChild();
    item->text = _scopes[i]._threadName;
    item->value = i;
    item->Expand(true);

    // stack will contain stopper
    AutoArray< TreeStackItem, MemAllocLocal<TreeStackItem,64> > stack;
    stack.Add(TreeStackItem(-1, item));

    // copy hierarchy of scopes to the index
    AutoArray<ScopeInfo> &infos = _scopes[i]._scopes;
    for (int j=0; j<infos.Size(); j++)
    {
      int parent = infos[j]._parentIndex;
      // find the parent on the stack
      int k = stack.Size() - 1;
      while (parent != stack[k]._index) k--;
      // create the new tree item
      CTreeItem *item = stack[k]._item->AddChild();
      item->text = Format("%s (%.3f ms : %.3f ms, %d)", cc_cast(infos[j]._name), infos[j]._start, infos[j]._duration,infos[j]._children);
      item->data = infos[j]._name;
      item->value = j;
      // color coding
      unsigned int colorIndex = CalculateStringHashValue(infos[j]._name);
      const char *texture = ScopeColors[colorIndex % lenof(ScopeColors)].texture;
      item->texture = GlobLoadTextureUI(texture);

      // add item to the stack
      stack.Resize(k + 2);
      stack[k + 1]._index = j;
      stack[k + 1]._item = item;
    }
  }
}

void CreateCaptureDialog()
{
#if _ENABLE_DEDICATED_SERVER
  if (IsDedicatedServer())
  {
    // analyze the frame and send results to the admin
    FindArrayKey<ScopesInThread, ScopesInThreadTraits> scopes;
    AutoArray<PerfCounterSaved> counters;
    DisplayCapture::Analyze(scopes, counters);
    RString output = DisplayCapture::ExportAll(scopes);
    GetNetworkManager().ProfileAnswer(output);
  }
  else
#endif // _ENABLE_DEDICATED_SERVER
  GWorld->AddDisplay(new DisplayCapture(NULL), 6.0f);
}

void CreateCaptureDialog(const RString &input)
{
  GWorld->AddDisplay(new DisplayCapture(NULL, input), 6.0f);
}

void DisplayCapture::DestroyHUD(int exit)
{
  GWorld->RemoveDisplay(this);
}

#endif

#ifndef _XBOX

///////////////////////////////////////////////////////////////////////////////
// display with dirty glasses

DisplayNotebook::DisplayNotebook(ControlsContainer *parent, ParamEntryVal cls)
: Display(parent)
{
  _lastWeather = WSUndefined;

  _textureClear = cls >> "textureClear";
  _textureCloudly = cls >> "textureCloudly";
  _textureOvercast = cls >> "textureOvercast";
  _textureRainy = cls >> "textureRainy";
  _textureStormy = cls >> "textureStormy";
}

extern void PositionToAA11(Vector3Val pos, char *buffer);

Control *DisplayNotebook::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  switch (idc)
  {
  case IDC_TIME:
    return new CStaticTime(this, idc, cls, false);
  default:
    return Display::OnCreateCtrl(type, idc, cls);
  }
}

void DisplayNotebook::OnDraw(EntityAI *vehicle, float alpha)
{
  // set values of info items
  WeatherState cur = GetWeather();
  if (cur != _lastWeather)
  {
    CStatic *picture = check_cast<CStatic *>(GetCtrl(IDC_WEATHER));
    if (picture)
    {
      switch (cur)
      {
      case WSClear:
        picture->SetText(_textureClear);
        break;
      case WSCloudly:
        picture->SetText(_textureCloudly);
        break;
      case WSOvercast:
        picture->SetText(_textureOvercast);
        break;
      case WSRainy:
        picture->SetText(_textureRainy);
        break;
      case WSStormy:
        picture->SetText(_textureStormy);
        break;
      }
    }
    _lastWeather = cur;
  }

  char buffer[256];
  Clock clock;

  CStaticTime *time = check_cast<CStaticTime *>(GetCtrl(IDC_TIME));
  if (time)
  {
    clock = GetTime();
    time->SetTime(clock);
  }

  CStatic *text = check_cast<CStatic *>(GetCtrl(IDC_DATE));
  if (text)
  {
    clock.FormatDate(LocalizeString(IDS_DATE_FORMAT), buffer);  
    text->SetText(buffer);
  }

  text = check_cast<CStatic *>(GetCtrl(IDC_POSITION));
  if (text)
  {
    Point3 pos;
    if (GetPosition(pos))
      PositionToAA11(pos, buffer);
    else
      buffer[0] = 0;
    text->SetText(buffer);
  }

  // draw controls
  Display::OnDraw(vehicle,alpha);

/*
  // draw glasses
  PackedColor color(Color(1,1,1,1));
  const int w = GLOB_ENGINE->Width();
  const int h = GLOB_ENGINE->Height();

  MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassA, 0, 0);
  GLOB_ENGINE->Draw2D(mip, color, 0, 0, 0.4 * w, 0.5 * h);
  mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassB, 0, 0);
  GLOB_ENGINE->Draw2D(mip, color, 0.4 * w, 0, 0.4 * w, 0.5 * h);
  mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassC, 0, 0);
  GLOB_ENGINE->Draw2D(mip, color, 0, 0.5 * h, 0.4 * w, 0.5 * h);
  mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassD, 0, 0);
  GLOB_ENGINE->Draw2D(mip, color, 0.4 * w, 0.5 * h, 0.4 * w, 0.5 * h);
  mip = GLOB_ENGINE->TextBank()->UseMipmap(_glassE, 0, 0);
  GLOB_ENGINE->Draw2D(mip, color, 0.8 * w, 0, 0.2 * w, 0.5 * h);
*/
}

#endif


//! New mod display (name of mod and its mod directory)
class DisplayNewMod: public Display
{
protected:
  InitPtr<ITextContainer> _name;
  InitPtr<ITextContainer> _dir;

public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayNewMod(ControlsContainer *parent);

  RString GetName() const {return _name ? _name->GetText() : RString();}
  RString GetDir() const {return _dir ? _dir->GetText() : RString();}

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
};

DisplayNewMod::DisplayNewMod(ControlsContainer *parent)
: Display(parent)
{
  Load("RscDisplayNewMod");
}

Control *DisplayNewMod::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_MOD_NAME:
    _name = GetTextContainer(ctrl);
    break;
  case IDC_MOD_DIR:
    _dir = GetTextContainer(ctrl);
    break;
  }
  return ctrl;
}

// DisplayAddonActions

typedef void (*FillAddonActionsCallback)(CListBoxContainer *container, const void *context);

class DisplayAddonActions : public Display
{
protected:
  InitPtr<CListBoxContainer> _actions;

public:
  DisplayAddonActions(ControlsContainer *parent, FillAddonActionsCallback callback, const void *context);

  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  void OnButtonClicked(int idc);
  void OnChildDestroyed(int idd, int exit);
};

DisplayAddonActions::DisplayAddonActions(ControlsContainer *parent, FillAddonActionsCallback callback, const void *context)
: Display(parent)
{
  Load("RscDisplayAddonActions");

  if (_actions)
  {
    (*callback)(_actions, context);
  }
}

Control *DisplayAddonActions::OnCreateCtrl(int type, int idc, ParamEntryPar cls)
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);

  switch (idc)
  {
  case IDC_ADDON_ACTIONS:
    _actions = GetListBoxContainer(ctrl);
    break;
  }

  return ctrl;
}

void DisplayAddonActions::OnButtonClicked(int idc)
{
  switch (idc)
  {
  case IDC_OK:
    if (_actions)
    {
      int sel = _actions->GetCurSel();
      if (sel >= 0) GWorld->PerformAddonAction(_actions->GetValue(sel), _actions->GetData(sel));
    }
    break;
  case IDC_ADDON_ACTIONS_NEW_MOD:
    CreateChild(new DisplayNewMod(this));
    break;
  }
  Display::OnButtonClicked(idc);
}

void DisplayAddonActions::OnChildDestroyed(int idd, int exit)
{
  switch (idd)
  {
  case IDD_NEW_MOD:
    if (exit == IDC_OK)
    {
      DisplayNewMod *display = dynamic_cast<DisplayNewMod *>(_child.GetRef());
      if (display)
      {
        RString modName = display->GetName();
        RString modDir = display->GetDir();
        // create modDir inside my documents directory, write the content of mod.cpp and copy the mod files there
        GWorld->PerformAddonAction(AATInstall, modDir, &modName);
        Display::OnChildDestroyed(idd, exit);
        Exit(IDC_CANCEL);
        return;
      }
    }
    break;
  }
  Display::OnChildDestroyed(idd, exit);
}

void CreateAddonActionsOptions(AbstractOptionsUI *display, FillAddonActionsCallback callback, const void *context)
{
  Display *parent = dynamic_cast<Display *>(display);
  if (!parent || parent->Child()) return;

  parent->CreateChild(new DisplayAddonActions(parent, callback, context));
}

///////////////////////////////////////////////////////////////////////////////
// creation of main display

AbstractOptionsUI *CreateDSOptionsUI()
{
#if _ENABLE_DEDICATED_SERVER
  return new DisplayDedicatedServer(NULL, GetDSBandwidth(), GetDSPrivateSlots());
#else
  return new DisplayDedicatedServer(NULL, -1, -1);
#endif
}

AbstractOptionsUI *CreateMainOptionsUI()
{
#if _VBS2
  return new DisplayMainVBS(NULL);
#else
  return new DisplayMain(NULL);
#endif
}

AbstractOptionsUI *CreateEndOptionsUI(int mode)
{
  Fail("Obsolete");
  return NULL;
}

void DisplayMain::DestroyHUD(int exit)
{
  GLOB_WORLD->DestroyOptions(exit);
}

#if _ENABLE_EDITOR2

void OpenEditor2()
{
  Display *options = dynamic_cast<Display *>(GWorld->Options());
  if (!options) return;
  GWorld->SwitchLandscape(Glob.header.worldname);

  Display *CreateMissionEditor(ControlsContainer *parent, bool multiplayer);
  options->CreateChild(CreateMissionEditor(options, false));
}

#endif // #if _ENABLE_EDITOR2

bool DeleteWeaponsFile();

void StartMission()
{
  Display *options = dynamic_cast<Display *>(GWorld->Options());
  if (!options) return;
  options->CreateChild(new DisplayMission(options));
}

RString GetSaveName(SaveGameType type)
{
  RString mpCampaignPrefix = RString("");
  RString name = GetSaveFilename(type);
  if (name.GetLength() > 0) return mpCampaignPrefix + name;
  return RString();
}

DisplayMultiplayerHostMission::DisplayMultiplayerHostMission(ControlsContainer *parent, ConstParamEntryPtr cfg)
: DisplayMultiplayer(parent), _cfg(cfg)
{
  CreateChild(new DisplayHostSettings(this));
}

void DisplayMultiplayerHostMission::OnChildDestroyed(int idd, int exit)
{
  if (idd==IDD_HOST_SETTINGS)
  {
    if (exit == IDC_OK)
    {
      DisplayHostSettings *display = dynamic_cast<DisplayHostSettings *>(_child.GetRef());
      if (display)
      {
        RString name = display->GetName();
        RString password = display->GetPassword();
        int maxPlayers = display->GetMaxPlayers();
        bool isPrivate = display->IsPrivate();
        int port = display->GetPort();
        Display::OnChildDestroyed(idd, exit);
        CreateSession(STSystemLink, name, password, maxPlayers, isPrivate, port, MPTHostMission, &_cfg);
        return;
      }
    }
    Display::OnChildDestroyed(idd, exit);
    Exit(exit); // Exit, never show this dialog!
    return;
  }

  DisplayMultiplayer::OnChildDestroyed(idd, exit);

  StartRandomCutscene(Glob.header.worldname);

  Exit(exit); // Exit, never show this dialog!
}

#ifdef _WIN32
DisplayJoystickSchemes::DisplayJoystickSchemes( DisplayCustomizeController *parent, FindArray<int> *keys, bool enableSimulation, RString joyName)
: Display(parent), _allKeys(keys)
{
  _enableSimulation = _enableSimulation && enableSimulation;
  Load("RscDisplayJoystickSchemes");
  _schemes->SetCurSel(0);

  // update title
  ITextContainer *text = GetTextContainer(GetCtrl(IDC_JOY_SCHEMES_TITLE));
  if (text) 
  {
    text->SetText(Format(LocalizeString(IDS_JOYSTICK_SCHEMES_TITLE), cc_cast(joyName)));
  }
}

Control * DisplayJoystickSchemes::OnCreateCtrl( int type, int idc, ParamEntryPar cls )
{
  Control *ctrl = Display::OnCreateCtrl(type, idc, cls);
  switch (idc)
  {
  case IDC_JOY_SCHEMES_LIST:
    {
      _schemes= GetListBoxContainer(ctrl);
      UpdateSchemesList();
    }
    break;
  }
  return ctrl;
}

void DisplayJoystickSchemes::OnButtonClicked( int idc )
{
  switch (idc)
  {
  case IDC_JOY_SCHEMES_MAP:
    {
      int curSel = _schemes->GetCurSel();
      if (curSel>=0)
      {
        int ix = _schemes->GetValue(curSel);
        DisplayCustomizeController *parent = dynamic_cast<DisplayCustomizeController *>(Parent());
        if (parent)
        {
          parent->SetKeysByScheme(GJoystickKeysSchemes[ix]);
        }
      }
      Exit(IDC_OK);
      break;
    }
  }
  Display::OnButtonClicked(idc);
}

void DisplayJoystickSchemes::UpdateSchemesList()
{
  _schemes->ClearStrings();
  GInput.LoadJoysticks();
  for (int i=0; i<GJoystickKeysSchemes.Size(); i++)
  {
    const JoystickKeysScheme &sch = GJoystickKeysSchemes[i];
    RString name = sch.name;
    if (sch.userDefined) name = RString("(User) ") + name; //TODO: Localize or not? The Joystick names are not localized too...
    int ix = _schemes->AddString(name);
    _schemes->SetValue(ix, i);
/*
    if (joy->mode==JMDisabled) 
    {
      _schemes->SetFtColor(ix, _joysticksDisabledColor);
      _schemes->SetSelColor(ix, _joysticksDisabledColor);
    }
*/
  }
}
#endif


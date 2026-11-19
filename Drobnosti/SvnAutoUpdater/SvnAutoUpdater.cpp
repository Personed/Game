#include "StdAfx.h"
#include <shellapi.h>
#include <string>
#include <map>
#include <io.h>
#include "svnAutoUpdater.h"
#include "SystemTraySDK.h"
#include "pipeManager.h"
#include "stdStringFormat.h"
#include "resource.h"
#include "Es/Files/commandLine.hpp"
#include "Es/Files/filenames.hpp"

HINSTANCE TheInstance = 0;

#include <Psapi.h>
#include <tlhelp32.h>
#pragma comment(lib,"Psapi.lib")
BOOL GetParentPID( PROCESSENTRY32& procentry )
{
  HANDLE         hSnapShot ;
  BOOL           cont ;

  // Get a handle to a Toolhelp snapshot of the systems
  // processes.
  hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0 );
  if( hSnapShot == INVALID_HANDLE_VALUE )
  {
    return FALSE ;
  }

  // Get the first process' information.
  memset((LPVOID)&procentry,0,sizeof(PROCESSENTRY32));
  procentry.dwSize = sizeof(PROCESSENTRY32) ;
  cont = Process32First( hSnapShot, &procentry ) ;
  DWORD pid = 0;
  // While there are processes, keep looping.
  DWORD  crtpid= GetCurrentProcessId();
  while( cont )
  {
    if(crtpid == procentry.th32ProcessID)
      pid =  procentry.th32ParentProcessID;

    procentry.dwSize = sizeof(PROCESSENTRY32) ;
    cont = !pid && Process32Next( hSnapShot, &procentry );

  }//while ends

  return pid?TRUE:FALSE ;
}

HANDLE GetParentHandle()
{
  PROCESSENTRY32 procentry;
  if ( GetParentPID(procentry) && procentry.th32ParentProcessID )
  {
    return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, procentry.th32ParentProcessID ) ;
  }
  return INVALID_HANDLE_VALUE;
}

#ifndef _CONSOLE
template <class T>
inline T GetWinLong (HWND hwnd, int which = GWL_USERDATA)
{
    return reinterpret_cast<T> (::GetWindowLong (hwnd, which));
}

template <class T>
inline void SetWinLong (HWND hwnd, T value, int which = GWL_USERDATA)
{
    ::SetWindowLong (hwnd, which, reinterpret_cast<long> (value));
}

struct LoginDialogData
{
  char *username, *password;
  LoginDialogData(char *user, char *pass) : username(user), password(pass) {}
};

void GetString(HWND hWnd, char* buf, int len)
{
  SendMessage (hWnd, WM_GETTEXT, (WPARAM) len, (LPARAM) buf);
}

INT_PTR CALLBACK LoginDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LoginDialogData* ctrl = GetWinLong<LoginDialogData *> (hwnd);
  switch (message)
  {
  case WM_INITDIALOG:
      {
          ctrl = reinterpret_cast<LoginDialogData *> (lParam);
 		      SetWinLong<LoginDialogData *>(hwnd, ctrl);
      }
      return TRUE;

  case WM_COMMAND:
    {
      int ctrlID = LOWORD(wParam);
      int notifyCode = HIWORD(wParam);
      switch (ctrlID)
      {
      case IDC_LOGIN_USERNAME:
          if (notifyCode==EN_CHANGE)
          {
            GetString (GetDlgItem(hwnd, IDC_LOGIN_USERNAME), ctrl->username, 256);
          }
          return true;
      case IDC_LOGIN_PASSWORD:
          if (notifyCode==EN_CHANGE)
          {
            GetString (GetDlgItem(hwnd, IDC_LOGIN_PASSWORD), ctrl->password, 256);
          }
          return true;
      case IDOK:
          EndDialog(hwnd, TRUE);
          return true;
      case IDCANCEL:
          EndDialog(hwnd, FALSE);
          return true;
      }
    }

  case WM_DESTROY:
  	SetWinLong<LoginDialogData *> (hwnd, 0);
	  break;
  }
  return FALSE;
}
#endif

//@{ Global option values taken from commandLine
struct Options
{
  string userName;
  string password;
  string svnUrl;    //svn to use when checkout is needed (no working directory exists yet)
  string updateDir;
  string DayZUpdaterRegKeyPath;

  Options()
  {
#define TEST_CASE 1
#if TEST_CASE
    userName = "arma2OA";
    password = "Arma2OA-24915472";
    svnUrl = "https://of.bistudio.com/svn/dist/arma2OA"; 
    updateDir = "O:\\tmp\\bebulek";
    DayZUpdaterRegKeyPath = "Software\\Bohemia Interactive Studio\\DayZUpdater";
#else
    // DayZ
    userName = "dayzUpdater";
    password = ",6u-bvDCwotS";
    svnUrl = "https://of.bistudio.com/svn/dist/DayZ"; 
    updateDir = "O:\\DayZ\\SvnUpdater\\DayZ";
    DayZUpdaterRegKeyPath = "Software\\Bohemia Interactive Studio\\DayZUpdater";
#endif
  }
  ~Options() {}
};
Options GOptions;
//@}

string SvnActionFormat(svn_wc_notify_action_t action)
{
  switch (action)
  {
  case svn_wc_notify_update_started: return "Update started for %s\n";
  case svn_wc_notify_update_update: return "  %s - Updated\n";
  case svn_wc_notify_update_delete: return "  %s - Deleted\n";
  case svn_wc_notify_update_add:    return "  %s - Added\n";
  case svn_wc_notify_update_completed: return "Update of %s completed\n";
  case svn_wc_notify_skip_conflicted: return "CONFLICTED: %s\n";
  }
  string retval = Format("(%d) %%s\n", action); //something different
  return retval;
}

void SvnNotifyCallback(void *baton, const svn_wc_notify_t *notify, apr_pool_t *pool)
{
  Controller *ctrl = static_cast<Controller *>(baton);
  ctrl->SetStatus(Format(SvnActionFormat(notify->action).c_str(), notify->path));
}

char auth_username[512];
char auth_password[512];
Controller::Controller (HWND hwnd)
  :	  _phase(PUpToDate),
      _editLog   (hwnd, IDC_LOG),
      _clear		 (hwnd,	IDC_CLEAR, FALSE),
      _status		 (hwnd,	IDC_STATUS),
      _minSysTrayCheck	 (hwnd,	IDC_MINIMIZE_SYSTRAY),
      _svnAcc(GOptions.userName.c_str(), GOptions.password.c_str(), SvnNotifyCallback, this),
      _svnAccOK(true), _svnErrorShown(false),
      _svnAccRestarted(false),
      _firstSvnCheckDone(false),
      _hSvnThread(INVALID_HANDLE_VALUE),
      _hCopyDirThread(INVALID_HANDLE_VALUE),
      _hwnd(hwnd),
      _minimized(false)
{
  InitializeCriticalSection(&cs);

  // Attach icon to main dialog
  HICON hIcon = LoadIcon (TheInstance, MAKEINTRESOURCE (DLG_ICON));
  SendMessage (hwnd, WM_SETICON, WPARAM (TRUE), LPARAM (hIcon));
  hIcon = LoadIcon (TheInstance, MAKEINTRESOURCE (DLG_ICON_S));
  SendMessage (hwnd, WM_SETICON, WPARAM (FALSE), LPARAM (hIcon));

  // Create the tray icon
  TrayIcon.Create(TheInstance,
                  hwnd,                // Parent window
                  WM_ICON_NOTIFY,      // Icon notify message to use
                  _T("DayZUpdater"),  // tooltip
                  hIcon,
                  IDR_SYSTRAY_POPUP_MENU);

  // Set timer to check for new update periodically
  ::SetTimer(hwnd, ID_CHECK_UPDATE_TIMER, 100, NULL);

  // Other initializations...
  _editLog.SetFocus();
  _minSysTrayCheck.Check();

  // DayZ TODO: store in registry and make it possible to change by commandline option
  _updateDir = GOptions.updateDir;
  SetLogLine(Format("At: %s", _updateDir.c_str()));
}

Controller::~Controller()
{
  if ( _hCopyDirThread!=INVALID_HANDLE_VALUE )
    ::TerminateThread(_hCopyDirThread,0);
  if ( _hSvnThread!=INVALID_HANDLE_VALUE )
    ::TerminateThread(_hSvnThread,0);

  for (auto it = _clients.begin(); it!=_clients.end(); it++)
  {
    delete (*it); //destroy SvnUpdaterClientApp instance
  }

  if (_csInitialized)
    DeleteCriticalSection(&cs);
}

// static members initialization
bool Controller::_csInitialized = false;

void Controller::SetStatus(string msg)
{
  Enter();
  _statusWaiting = msg;
  Leave();
}

void Controller::SetLogLine(string logLine)
{
  Enter();
  _logLinesWaiting.push_back(logLine);
  Leave();
}

void Controller::UpdateStatusAndLog()
{
  // BEWARE: can be called only from main thread!
  Enter();
  if (!_statusWaiting.empty())
  {
    _status.SetString(_statusWaiting.c_str());
    _statusWaiting.clear();
  }
  if (!_logLinesWaiting.empty())
  {
    string allLines;
    for (auto it = _logLinesWaiting.begin(); it!=_logLinesWaiting.end(); it++)
    {
      if (allLines.empty()) allLines = (*it).c_str();
      else allLines = allLines + string("\r\n") + (*it).c_str();
    }
    _logLinesWaiting.clear();
    _editLog.AddLine(allLines.c_str());
  }
  Leave();
}

void Controller::Connect (HWND hwnd, HANDLE hIn, HANDLE hOut)
{
  // we allow application to run even during coppying phase, but its directory must not be under processing
  // which will be determined after initial pipeManager change of information
  // DayZ TODO
  Enter();
  _editLog.AddLine(Format("Connecting: %x:%x", hIn, hOut).c_str());
  SvnUpdaterClientApp *client = new SvnUpdaterClientApp(hIn, hOut, this);
  _clients.push_back(client);
  Leave();
}

void Controller::Command (HWND hwnd, int controlID, int command)
{
    switch (controlID)
    {
        case IDC_LOG:
            if (_editLog.IsChanged(command))
            {
                if (_editLog.GetLength())
                    _clear.Enable();
                else
                    _clear.Disable();
            }
            break;
        case IDC_CLEAR:
            _editLog.SetString("");
            _clear.Disable();
            _editLog.SetFocus();
            break;
        case IDC_MINIMIZE_SYSTRAY:
            break;
        case ID_POPUP_RESTORE:
          TrayIcon.MaximiseFromTray(hwnd);
          ::ShowWindow(_hwnd, SW_SHOW);
          _minimized = false;
           break;
        case ID_POPUP_QUIT:
				   DestroyWindow(hwnd);
           break;
	/*
        case IDM_ABOUT:
				   DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				   break;
  */
    }
}

bool Controller::Minimize(HWND hwnd)
{
  if (_minSysTrayCheck.IsChecked())
  {
    TrayIcon.MinimiseToTray(hwnd);
    TrayIcon.ShowBalloon("The SvnAutoUpdater\nwas minimized to SysTray");
    _minimized = true;
    return true;
  }
  return false;
}

DWORD WINAPI ThreadSvnUpdateProc(LPVOID lpParameter)
{
  Controller *control = reinterpret_cast<Controller *>(lpParameter);
  if (control)
  {
    int rev;
    control->_svnAccOK = control->_svnAcc.Update(control->_updateDir.c_str(), rev);
  }
  return 0;
}

DWORD WINAPI ThreadSvnCheckoutProc(LPVOID lpParameter)
{
  Controller *control = reinterpret_cast<Controller *>(lpParameter);
  if (control)
  {
    control->_svnAccOK = control->_svnAcc.Checkout(GOptions.svnUrl.c_str(), control->_updateDir.c_str());
  }
  return 0;
}

void Controller::SetPhase(enum Phase phase)
{
  _phase = phase;
  switch (phase)
  {
  case PUpToDate: SetStatus("Checking for updates..."); break;
  case PUpdating: SetStatus("Updating working copy..."); break;
  case PCopying: SetStatus("Copying working copy to app folders..."); break;
  }
}

//@{ PipeManager initialization and commands
// Commands Sending by Client
const string CmdCommandline("commandline: "); //used only when client application should be restarted after svn update
const string CmdUpdateDir("updateDir: ");     //client application Working Copy folder
const string CmdCopyDir("copyDir: ");         //client application Game Folder
const string CmdCurDir("workingDir: ");       //used only when client application should be restarted after svn update
const string CmdSvnUrl("url: ");              //DayZ - default set to "https://of.bistudio.com/svn/dist/DayZ";
const string CmdPID("pid: ");                 //ProcessID of the client application
const string CmdRestartMe("restartMe");       //app wants to be restarted after update is over

// Commands Sending by AutoUpdater (server)
const string CmdAccessViolating("accessViolating");  //server wants the client to stop immediately
const string CmdReady("ready");                      //client can continue execution (no access violation)
const string CmdShutDown("shutDown");                //ask client to shut down (as her data are obsolete)
const string CmdThx("thanks for ");
const string CmdUpToDate("up-to-date");
const string CmdCheckout("checkouting");
const string CmdOutdated("updating");
//@}

void Controller::CheckForUpdate()
{
  const char *strWCRev = "Working copy at revision: %d";
  
  _svnAccOK = (_svnAcc.GetLastError()==NULL);

  if ( _hSvnThread != INVALID_HANDLE_VALUE )
  { // svnUpdate or svnCheckOut is in progress
    if (WaitForSingleObject(_hSvnThread, 0)==WAIT_OBJECT_0 )
    {
      if (_svnAccOK)
      {
        _hSvnThread = INVALID_HANDLE_VALUE;
        SetPhase(PUpToDate);
      }
      else
      {
        _editLog.AddLine("Connection to SVN has failed. Unable to check for updates!");
        _hSvnThread = INVALID_HANDLE_VALUE;
        _svnAccOK = false;
        SetPhase(PUpToDate); //try again later?
      }
    }
  }
  else if ( !GOptions.svnUrl.empty() && !_updateDir.empty() && _svnAccOK)
  { 
    if ( !_svnAcc.IsVersioned(_updateDir.c_str()) )
    { // checkout it
      if (_svnAccRestarted || GOptions.userName.length()>0)
      { // IsVersioned does not test connection to svn, restart it with auth info
        //_pipeManager.Send(CmdCheckout);
        _editLog.AddLine("Application in not under svn yet, checkout in progress...");
        _status.SetString("Checkout in progress, downloading...");
        SetPhase(PUpdating);
        _hSvnThread = CreateThread(NULL, 0, ThreadSvnCheckoutProc, this, 0, NULL);
      }
      else _svnAccOK = false; 
    }
    else // working dir is under svn control
    {
      DWORD curTime = ::GetTickCount();
      static DWORD nextUpdateTime = curTime;
      if ( curTime >= nextUpdateTime ) //every 5 seconds
      {
        nextUpdateTime = curTime + 5*1000;

        bool updateAvailable = false;
        int oldRev = RepositoryRevision();
        bool firstSvnCheckDone = ( RepositoryRevision()!=SVN_INVALID_REVNUM );
        if ( _svnAcc.UpdateAvailable(_updateDir.c_str(), updateAvailable) )
        {
          if (updateAvailable)
          { // update it
            //_pipeManager.Send(CmdOutdated);
            _editLog.AddLine("Application is outdated, svn update in progress...");
            _status.SetString("Update in progress, downloading...");
            SetPhase(PUpdating);
            _hSvnThread = CreateThread(NULL, 0, ThreadSvnUpdateProc, this, 0, NULL);
          }
          else if ((oldRev==SVN_INVALID_REVNUM) || (oldRev<RepositoryRevision()) )
          {
            //_pipeManager.Send(CmdUpToDate);
            string uptodateLog = Format(strWCRev, RepositoryRevision());
            _editLog.AddLine(uptodateLog.c_str());
            _status.SetString(uptodateLog.c_str());
          }
        }
        else
        { // something went wrong
          _svnAccOK = false;
        }
      }
    }
  }
  else if ( !_svnAccOK )
  {
    if (!_svnAccRestarted)
    {
      _status.SetString("Access to SVN repository without Username/Password has failed! ");
      ::KillTimer(_hwnd, ID_CHECK_UPDATE_TIMER);  //stop timer
      LoginDialogData loginData(auth_username, auth_password);
      ::DialogBoxParam(TheInstance, MAKEINTRESOURCE(IDD_LOGIN), _hwnd, LoginDialogProc, (LPARAM) &loginData);
      ::SetTimer(_hwnd, ID_CHECK_UPDATE_TIMER, 100, NULL);
      _svnAccRestarted = true;
      _svnAcc.Init(auth_username, auth_password);
      _svnAccOK = true; //try again
    }
    else
    {
      _status.SetString("Access to SVN repository has failed!");
      if (!_svnErrorShown)
      {
        // _pipeManager.Send(PipeManager::CmdFailed);
        _svnErrorShown = true;
        _editLog.AddLine(_svnAcc.GetLastError());
        TrayIcon.MaximiseFromTray(_hwnd);
        _minimized = false;
      }
    }
  }
}

void Controller::Process()
{
  // CALLED FROM MAIN THREAD ONLY!
  UpdateStatusAndLog(); //thread safe

  if ( _hCopyDirThread != INVALID_HANDLE_VALUE )
  {
    if (WaitForSingleObject(_hCopyDirThread, 0)==WAIT_OBJECT_0 )
    {
      DWORD retVal;
      if (GetExitCodeThread(_hCopyDirThread, &retVal) && retVal==1) //copy failed
      {
        //::KillTimer(_hwnd, ID_CHECK_UPDATE_TIMER);  //stop timer
        TrayIcon.MaximiseFromTray(_hwnd); //cannot do from other thread
      }
      SetPhase(PUpToDate); //start to wait on next update or folder copy again
      _hCopyDirThread = INVALID_HANDLE_VALUE;
    }
    // Manage Pipe in/out
    UpdatePipeManager();
  }
  else
  {
    CheckForUpdate();
    if (_phase!=PUpdating) SetPhase(_phase); //update status

    // Receive possible new commands from pipe and also process them
    if (_phase!=PUpdating) UpdateClients();
    else UpdatePipeManager();
  }
}

//check the commandline
/// check if an option with arguments matches
static bool IsOptionWArgs(const char *beg, const char *opt)
{
  // when this is used, the last character of opt should be '=', like in -x=800
  //Assert(opt[0]!=0 && opt[strlen(opt)-1]=='=');
  return !strnicmp(beg,opt,strlen(opt));
}

static void SingleArgument( const char *beg, const char *end )
{
  if( end==beg ) return;
  if( *beg=='-' || *beg=='/' )
  {
    beg++;
    // option
    static const char userNameStr[]="login=";
    static const char passwdStr[]="password=";

    if (IsOptionWArgs(beg, userNameStr))
    {
      const char *val = beg + strlen(userNameStr); 
      GOptions.userName = string(val, end-val);
    }
    else if (IsOptionWArgs(beg, passwdStr))
    {
      const char *val = beg + strlen(passwdStr); 
      GOptions.password= string(val, end-val);
    }
  }
}

BOOL CALLBACK DialogProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static Controller* control = 0;
    switch (message)
    {
    case WM_INITDIALOG:
        try
        {
            control = new Controller (hwnd);
        }
        catch (WinException e)
        {
            MessageBox (0, e.GetMessage (), "Exception", MB_ICONEXCLAMATION | MB_OK);
        }
        catch (...)
        {
            MessageBox (0, "Unknown", "Exception", MB_ICONEXCLAMATION | MB_OK);
            return -1;
        }
        // Minimize (never mind when no client is connected)
        control->Minimize(hwnd);
        return TRUE;
    case WM_COMMAND:
        control->Command(hwnd, LOWORD(wParam), HIWORD (wParam));
        return TRUE;
    case USER_WM_CONNECT:
        control->Connect(hwnd, (HANDLE)wParam, (HANDLE)lParam);
        return TRUE;
    case WM_ICON_NOTIFY:
        return control->TrayIcon.OnTrayNotification(wParam, lParam);
    case WM_SYSCOMMAND:
      {
        switch (wParam)
        {
        case SC_MINIMIZE:
          if (!control->Minimize(hwnd))
            return DefWindowProc(hwnd, message, wParam, lParam);
          return TRUE;
        default:
          return DefWindowProc(hwnd, message, wParam, lParam);
        }
      }
      break;
    case WM_TIMER:
      if (wParam==ID_CHECK_UPDATE_TIMER)
      {
#if ANIMATE_CAPTION
        static int ticks = 0;
        ticks++;
        if (!(ticks%5))
        {
          static int counter=0;
          const char* animChars = ".oOo.";
          char anim[16];
          int animPhase = counter%8;
          if (animPhase>4) animPhase = 8-animPhase;
          for (int i=0; i<animPhase; i++) anim[i] = ' ';
          anim[animPhase] = animChars[animPhase];
          anim[++animPhase] = 0;
          string caption = Format("DayZUpdater %s", anim);
          SetWindowText(hwnd, caption.c_str());
          counter++;
        }
#endif

        control->Process();
/* NOTE: when svnAutoUpdater tell parent application to shutDown, the pipe will not be read on the other side 
         and the _write would block at last (FREEZE)
        // test pipe
        static int count = 1;
        string countStr = Format("svnAutoUpdater timer = %d", count++);
        _write(GOptions.gfdOut, countStr.c_str(), countStr.length());
*/
      }
      break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    case WM_CLOSE:
        delete control;
        DestroyWindow (hwnd);
        return TRUE;
    }
    return FALSE;
}

// help function to read the STRING value from registry key
static string GetRegistryValue(HKEY hKey, string valueName)
{
  const int MAX_VALUE_NAME = 512;
  DWORD valLen = MAX_VALUE_NAME;
  BYTE  val[MAX_VALUE_NAME];
  DWORD type;
  HRESULT result = ::RegQueryValueEx(hKey, valueName.c_str(), 0, &type, val,&valLen);
  if ( result==ERROR_SUCCESS && type==REG_SZ )
  {
    return string((char *)val);
  }
  return string();
}

static bool StoreRegistryString(HKEY key, const char *value, const char *content)
{
  HRESULT result = ::RegSetValueEx(key, value, 0, REG_SZ, reinterpret_cast<const BYTE *>(content), strlen(content)+1);
  return result==ERROR_SUCCESS;
}

enum RegStat
{
  RegOK, RegUpgrading, RegError
};
RegStat Registry()
{
  // when DayZUpdater is run for the first time, it should write the lines into registry
  // DayZ then finds the executable by reading it

  char fileName[2048];
  GetModuleFileName(NULL, fileName, 2048);

  HKEY pKey = NULL;
  //open the registry key 
  if (::RegOpenKeyEx(HKEY_CURRENT_USER,
    GOptions.DayZUpdaterRegKeyPath,
    0,
    KEY_ALL_ACCESS,
    &pKey) != ERROR_SUCCESS)
  {
    if ( RegCreateKey(HKEY_CURRENT_USER, GOptions.DayZUpdaterRegKeyPath, &pKey) != ERROR_SUCCESS )
    { // registration failed
      ::RegCloseKey(pKey);
      return RegError;
    }
    char wc[2048];
    char *exeDir = GetFilenameExt( fileName );
    if (exeDir)
    {
      strncpy(wc, fileName, exeDir-fileName); //ends with '\\'
      strcpy(wc+(exeDir-fileName), "DayZ");
      StoreRegistryString(pKey, "wc", wc);
    }
    StoreRegistryString(pKey, "url", GOptions.svnUrl.c_str());
  }
  else
  { // test whether DayZUpdater.exe is uprading atm
    string upgrading = GetRegistryValue(pKey, "upgrading");
    if (!upgrading.empty())
      return RegUpgrading; //STOP execution
  }
  // the key (now) exists, so rewrite the path value (to keep it fresh)
  StoreRegistryString(pKey, "path", fileName);

  // read the wc and svn url at last
  GOptions.updateDir = GetRegistryValue(pKey, "wc");
  GOptions.svnUrl = GetRegistryValue(pKey, "url");

  ::RegCloseKey(pKey);

  return RegOK; //continue
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, char * cmdLine, int cmdShow)
{
#if 0
    // TEST SECTION
    HWND fnd = FindWindow("DAYZ_UPDATER", NULL);
    if (fnd!=NULL)
    {
      OutputDebugString("GOTCHA! Connecting `,-]\n");
      PostMessage(fnd,USER_WM_CONNECT,(WPARAM)0xffff153,(LPARAM)0xffff154);
    }
    return 1;
#else
    TheInstance = hInst;
    HWND hDialog = 0;

    //MUTEX for DayZUpdater server
    HWND fnd = FindWindow("DAYZ_UPDATER", NULL);
    if (fnd!=NULL)
    {
      ::MessageBox(NULL, "Only one instance of DayZUpdater.exe allowed!", "Closing...", MB_SYSTEMMODAL|MB_OK|MB_ICONEXCLAMATION);
      return 1;
    }

    switch (Registry())
    {
    case RegUpgrading:
      ::MessageBox(NULL, "DayZUpdater is upgrading, try again later!", "Closing...", MB_SYSTEMMODAL|MB_OK|MB_ICONEXCLAMATION);
      return 2;
    case RegError:
      ::MessageBox(NULL, "Cannot create Software\\Bohemia Interactive Studio\\DayZUpdater registry key!", "Closing...", MB_SYSTEMMODAL|MB_OK|MB_ICONEXCLAMATION);
      return 3;
    //case RegOK: //continue
    }

    CommandLine cLine(cmdLine);
    ScanCommandLine(cLine,SingleArgument);

    //Sleep(10000);

    WNDCLASS wc = {0};
    wc.style = CS_BYTEALIGNWINDOW|CS_SAVEBITS|CS_DBLCLKS;
    wc.lpfnWndProc = DefDlgProc;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = TheInstance;
    wc.hIcon = LoadIcon (TheInstance, MAKEINTRESOURCE (DLG_ICON));
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    wc.lpszClassName = _T("DAYZ_UPDATER"); //this name is from dialog's template
    ATOM cls = RegisterClass(&wc);

    hDialog = CreateDialog (hInst, MAKEINTRESOURCE (DLG_MAIN), 0, (DLGPROC)DialogProc);
    if (!hDialog)
    {
        char buf [100];
        wsprintf (buf, "Error x%x", GetLastError ());
        MessageBox (0, buf, "CreateDialog", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    MSG  msg;
    int status;
    while ((status = ::GetMessage (&msg, 0, 0, 0)) != 0)
    {
        if (status == -1)
            return -1;
        if (!IsDialogMessage (hDialog, &msg))
        {
            TranslateMessage ( &msg );
            DispatchMessage ( &msg );
        }
    }

    return msg.wParam;
#endif
}

#define CHECK_PARAM(CmdName, CmdVar) \
  { \
    int len = CmdName.length(); \
    if ( !strnicmp(command.c_str(), CmdName.c_str(), len) ) \
    { \
      CmdVar = command.substr(len, command.length()-len); \
      _pipeManager.Send(CmdThx + CmdName); \
      /*_editLog.AddLine((string("Received: ")+string(command)).c_str());*/ \
      continue; \
    } \
  } 

#define CHECK_PARAM_CONTINUE(CmdName, CmdVar) \
  { \
    int len = CmdName.length(); \
    if ( !strnicmp(command.c_str(), CmdName.c_str(), len) ) \
    { \
      CmdVar = command.substr(len, command.length()-len); \
      _pipeManager.Send(CmdThx + CmdName); \
      /*_editLog.AddLine((string("Received: ")+string(command)).c_str());*/ \
    } \
  } 

// sets TestVar true when command is CmdName 
#define IS_PARAM(CmdName, TestVar) \
  { \
    int len = CmdName.length(); \
    if ( !strnicmp(command.c_str(), CmdName.c_str(), len) ) \
    { \
      /*_editLog.AddLine((string("Received: ")+string(command)).c_str());*/ \
      TestVar = true; \
    } \
  } 

void SvnUpdaterClientApp::Update()
{
  if (_pipeManager.Ready())
  {
    // receive new data
    _pipeManager.Update();
    // process all commands
    for (string command=_pipeManager.Receive(); !command.empty(); command=_pipeManager.Receive())
    {
      CHECK_PARAM(CmdCommandline, _commandLine)
      //CHECK_PARAM(CmdUpdateDir, _updateDir)
      CHECK_PARAM(CmdCurDir, _workingDir)
      CHECK_PARAM(CmdCopyDir, _copyDir)
      //CHECK_PARAM(CmdSvnUrl, _svnUrl)
      std::string pidstr;
      CHECK_PARAM_CONTINUE(CmdPID, pidstr);
      if (!pidstr.empty())
      {
        _pid = atoi(pidstr.c_str());
        _hProcess = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION,FALSE,_pid);
        continue;
      }
      IS_PARAM(CmdRestartMe, _restartAfterShutDown);
    }
  }

  if ( !_autoUpdateInfo.loaded && !_copyDir.empty() )
  {
    _autoUpdateInfo.Load(_copyDir);

    // it may happen the connecting client should stop executing, as it uses the same copyDir as autoUpdater
    if (_autoUpdateInfo.lockPID==GetCurrentProcessId() )
    { //stop it
      _pipeManager.Send(CmdAccessViolating);
      _parent->SetLogLine( Format("Access Violation: %s", _copyDir.c_str()) );
      _parent->SetLogLine("... shut down request sent!");
    }
    else
    { //allow client to continue in her execution
      _pipeManager.Send(CmdReady);
      _parent->SetLogLine( Format("Connected: PID=%d: %s", _pid, _copyDir.c_str()).c_str() );
    }
  }

  if (_hProcess!=INVALID_HANDLE_VALUE)
  {
    DWORD waitRV = WaitForSingleObject(_hProcess, 0);
    if (waitRV==WAIT_OBJECT_0) 
    { //client application is down
      CloseHandle(_hProcess);
      _hProcess = INVALID_HANDLE_VALUE;
      _pipeManager.Close();
      _parent->SetLogLine( Format("Client has Shut Down! (PID=%d)", PID()) );
    }
    else if (waitRV!=WAIT_TIMEOUT)
    {
      _parent->SetLogLine("Error while checking the application process!");
      _parent->SetStatus("");
      _pipeManager.Send(PipeManager::CmdFailed);
      CloseHandle(_hProcess);
      _hProcess=INVALID_HANDLE_VALUE;
      _pipeManager.Close();
    }
  }
}

void SvnUpdaterClientApp::Restart()
{
  if ( !RestartAfterShutDown() ) return;

  _parent->SetLogLine(Format("Restarting %s", _commandLine.c_str()));

  STARTUPINFO startupInfo; 
  PROCESS_INFORMATION processInfo; 
  memset(&startupInfo, 0, sizeof(startupInfo)); 
  memset(&processInfo, 0, sizeof(processInfo)); 
  startupInfo.cb = sizeof(startupInfo);
  char *cmdLine = new char[_commandLine.length()+1];
  strcpy(cmdLine, _commandLine.c_str());
  CreateProcess(NULL, cmdLine , NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, _workingDir.c_str(), &startupInfo, &processInfo);
}

bool SvnUpdaterClientApp::VersionUpToDate(int currentRev)
{
  if (_copyDir.empty()) 
    return true; //hopefully not set yet
  if ( !_autoUpdateInfo.loaded )
    _autoUpdateInfo.Load(_copyDir);
  return (_autoUpdateInfo.revision == currentRev);
}

const char fmtLockPID[] = "lockPID: ";
const char fmtRev[] = "rev: ";
void SvnUpdaterClientApp::AutoUpdateConfig::Load(string configDir)
{
  string configPath = configDir + string("\\") + string(ConfigFilename());

  // invalidate values
  lockPID = -1;
  revision = -1;

  // read possible new values from autoUpdate.info
  FILE *file = fopen(configPath.c_str(), "rt");
  if (file)
  {
    char buf[2048];
    while ( fgets(buf, 2048, file) )
    {
      if ( _strnicmp(buf, fmtRev, sizeof(fmtRev)-1) == 0 )
      {
        revision = atoi(buf + sizeof(fmtRev)-1);
      }
      else if ( _strnicmp(buf, fmtLockPID, sizeof(fmtLockPID)-1) == 0 )
      {
        lockPID = atoi(buf + sizeof(fmtRev)-1);
      }
    }
    fclose(file);
  }
  loaded = true;
}

void SvnUpdaterClientApp::AutoUpdateConfig::Save(string configDir)
{
  string configPath = configDir + string("\\") + string(ConfigFilename());
  FILE *file = fopen(configPath.c_str(), "wt");
  if (file)
  {
    string line;
    line = Format("%s%d\n", fmtRev, revision);
    fputs(line.c_str(), file);
    line = Format("%s%d\n", fmtLockPID, lockPID);
    fputs(line.c_str(), file);
    fclose(file);
  }
}

void Controller::UpdatePipeManager()
{
  // CALLED FROM MAIN THREAD ONLY!
  Enter(); //<-- this section should not last long time
  for (auto it = _clients.begin(); it!=_clients.end(); it++)
  {
    // receive new data through pipe
    (*it)->Update();
  }
  Leave();
}

void Controller::UpdateClients()
{
  // Manage Pipe in/out
  UpdatePipeManager();

  //! ask clients with obsolete data to shutdown
  AskClientsToShutDown();

  //! clear clients which are already not running and not waiting to restart
  ClearInactiveClients();

  SvnUpdaterClientApp *clientToUpdate = GetClientToUpdate();
  if ( clientToUpdate )
  {
    SetPhase(PCopying);
    _hCopyDirThread = CreateThread(NULL, 0, ThreadSynchronizeGameFolders, this, 0, NULL);
  }
}

typedef bool (*CopyFilePredicate)(string src, string dst);

#include <sys/types.h>
#include <sys/stat.h>
bool CopyMoreRecentFilesOnly(string src, string dst)
{
	struct stat srcTime, dstTime;

  if ( !stat(src.c_str(), &srcTime) && !stat(dst.c_str(), &dstTime) )
  {
    return (srcTime.st_mtime > dstTime.st_mtime);
  }
  return true;
}

class ICopyNotifyFunc
{
public:
  ICopyNotifyFunc() {}
  virtual void operator() (string notify) {}; //dummy default
};

ICopyNotifyFunc DummyNotificator;

class CopyNotifyFunc : public ICopyNotifyFunc
{
  Controller &_ctrl;
public:
  CopyNotifyFunc(Controller &controller) : _ctrl(controller) {}

  virtual void operator() (string notify);
};

void CopyNotifyFunc::operator() (string notify)
{
  _ctrl.SetStatus(notify);
}

bool CopyDirectoryStructure(const char *dst, const char *src, vector<string> &skipDirList, CopyFilePredicate predicate=[](string src, string dst) { return true; }, ICopyNotifyFunc &notify=DummyNotificator) //Default: copy all files
{
#ifdef _WIN32
  string buffer = Format("%s\\*.*", src);

  _finddata_t info;
  long h = _findfirst(buffer.c_str(), &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        // FIX - do not delete only current and parent directory
        bool skipIt = false;
        for (auto it = skipDirList.begin(); it!=skipDirList.end(); it++)
        {
          if ( !strcmp(info.name, (*it).c_str()) )
          {
            skipIt=true;
            break;
          }
        }
        if (skipIt) continue;
        if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
        {
          string srcNew = Format("%s\\%s", src, info.name);
          string dstNew = Format("%s\\%s", dst, info.name);
          if (!::CreateDirectory(dstNew.c_str(), NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)
          { //some error?
            return false;
          }
          if (!CopyDirectoryStructure(dstNew.c_str(), srcNew.c_str(), skipDirList, predicate, notify))
          { //some error?
            return false;
          }
        }
      }
      else
      {
        string srcNew = Format("%s\\%s", src, info.name);
        string dstNew = Format("%s\\%s", dst, info.name);
        if (predicate(srcNew, dstNew))
        {
          notify(Format("coppying %s",dstNew.c_str())); //possibly show some notify info about actual file being coppied
          if (!::CopyFile(srcNew.c_str(), dstNew.c_str(), false /*overwrite existing*/))
          {
            return false;
          }
          char bufx[512];
          sprintf(bufx, "COPY: %s\n", srcNew.c_str());
          OutputDebugString(bufx);
        }
        else
        {
          char bufx[512];
          sprintf(bufx, "skipped: %s\n", srcNew.c_str());
          OutputDebugString(bufx);
        }
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
  return true;
#else
  not done, yet, this line purpose is to cause compiler error
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

void Controller::RestartClients(string folder)
{
  Enter();
  for (auto it = _clients.begin(); it!=_clients.end();)
  {
    SvnUpdaterClientApp &client = *(*it);
    if ( client.CopyDir() == folder )
    {
      if ( client.RestartAfterShutDown() )
      {
        client.Restart();
      }
      // remove it, client should connect again
      delete (*it); //destroy SvnUpdaterClientApp instance
      it = _clients.erase(it);
      continue;
    }
    it++; //but do not increment when erase occurs!
  }
  Leave();
}

void Controller::AskClientsToShutDown()
{
  Enter();
  for (auto it = _clients.begin(); it!=_clients.end(); it++)
  {
    SvnUpdaterClientApp &client = *(*it);

    if ( client.CopyDir().empty() ) continue; //not initialized yet

    // we need to recheck, whether client is not running and whether her revision is obsolete
    if ( !client._askedToShutdown && client.IsRunning() && !client.VersionUpToDate(_svnAcc.RepositoryRevision()) )
    {
      SetLogLine( Format("Client asked to ShutDown (PID=%d: %s)", client.PID(),client.CopyDir().c_str()) );
      client._askedToShutdown = true;
      client.Send(CmdShutDown);
    }
  }
  Leave();
}

void Controller::ClearInactiveClients()
{
  Enter();
  for (auto it = _clients.begin(); it!=_clients.end();)
  {
    SvnUpdaterClientApp &client = *(*it);

    if ( !client.CopyDir().empty() && !client.IsRunning() && !client.RestartAfterShutDown() && !client._askedToShutdown ) 
    {
      // remove it, client should connect again
      delete (*it); //destroy SvnUpdaterClientApp instance
      it = _clients.erase(it);
      continue;
    }
    it++; //but do not increment when erase occurs!
  }
  Leave();
}

SvnUpdaterClientApp * Controller::GetClientToUpdate()
{
  map<string, SvnUpdaterClientApp *> candidates;
  typedef std::pair<string, SvnUpdaterClientApp *> CandidatePair;

  Enter();
  for (auto it = _clients.begin(); it!=_clients.end(); it++)
  {
    SvnUpdaterClientApp &client = *(*it);

    if ( client.CopyDir().empty() ) continue; //not initialized yet
    auto candIt = candidates.find(client.CopyDir());

    // we need to recheck, whether client is not running and whether her revision is obsolete
    if ( client.IsRunning() || client.VersionUpToDate(_svnAcc.RepositoryRevision()) )
    {
      if (candIt!=candidates.end())
      { // moreover, no other client sharing the same game folder can be running
        candIt->second = NULL;
      }
      else candidates.insert( CandidatePair(client.CopyDir(), NULL) );
      continue;
    }
    
    if ( candIt==candidates.end() )
    { //new candidate
      candidates.insert( CandidatePair(client.CopyDir(), &client) ) ;
    }
  }
  // traverse all candidates and return first available
  for (auto it = candidates.begin(); it != candidates.end(); it++)
  {
    if (it->second!=NULL)
    {
      Leave();
      return it->second;
    }
  }
  Leave();

  return NULL; //no dirty client found
}

DWORD WINAPI ThreadSynchronizeGameFolders(LPVOID lpParameter)
{
  Controller *control = reinterpret_cast<Controller *>(lpParameter);
  if (control)
  {
    // copy new application version taken from svn over the currently shutdowned one
    vector<string> skipDirList;
    skipDirList.push_back(".svn");

    long curRev = control->_svnAcc.RepositoryRevision(); //current working copy revision

    SvnUpdaterClientApp *clientToUpdate = NULL;
    if ( (clientToUpdate = control->GetClientToUpdate()) != NULL )
    { //somethingDirty
      string logLine = Format("Begin Copy to '%s'", clientToUpdate->CopyDir().c_str());
      control->SetLogLine(logLine);
      clientToUpdate->_autoUpdateInfo.lockPID = GetCurrentProcessId();
      clientToUpdate->_autoUpdateInfo.Save(clientToUpdate->CopyDir());
      if ( !CopyDirectoryStructure(clientToUpdate->_copyDir.c_str(), control->_updateDir.c_str(), skipDirList, CopyMoreRecentFilesOnly, CopyNotifyFunc(*control)) )
      { // update failed
        char *errMsg = "Copy process failed!";
        control->SetStatus(errMsg);
        control->SetLogLine(errMsg);
        clientToUpdate->_autoUpdateInfo.revision = -1; //failed
      }
      else
      {
        control->SetLogLine(Format("Copy done (%s)", clientToUpdate->_copyDir.c_str()));
        clientToUpdate->_autoUpdateInfo.revision = control->RepositoryRevision();
      }
      clientToUpdate->_autoUpdateInfo.lockPID = -1; //clear flag
      clientToUpdate->_autoUpdateInfo.Save( clientToUpdate->CopyDir() );
      // restart all clients sharing this CopyDir
      control->RestartClients( clientToUpdate->CopyDir() );
    }
  }
  control->SetPhase(Controller::PUpToDate);
  return 0;
}

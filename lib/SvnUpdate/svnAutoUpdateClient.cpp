#include <Es/essencepch.hpp>
#include <El/elementpch.hpp>

#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <Es/Strings/rString.hpp>

#include "svnAutoUpdateClient.h"

//@{ PipeManager initialization and commands
// Commands Sending by Client
const std::string CmdCommandline("commandline: "); //used only when client application should be restarted after svn update
const std::string CmdUpdateDir("updateDir: ");     //client application Working Copy folder
const std::string CmdCopyDir("copyDir: ");         //client application Game Folder
const std::string CmdCurDir("workingDir: ");       //used only when client application should be restarted after svn update
const std::string CmdSvnUrl("url: ");              //DayZ - default set to "https://of.bistudio.com/svn/dist/DayZ";
const std::string CmdPID("pid: ");                 //ProcessID of the client application
//@}

bool fexists(const char *filename)
{
  std::ifstream ifile(filename);
  return ifile!=0;
}

bool SVNAutoUpdaterClient::Init(const char *svnAUPath, const char *svnURL, const char *updateDir, const char *user, const char *passwd)
{
  if ( fexists(svnAUPath) )
  {
    HWND fnd = FindWindow("DAYZ_UPDATER", NULL);
    if (fnd==NULL)
    {
      hProcess = (HANDLE)spawnl(P_NOWAIT, svnAUPath, svnAUPath, NULL);
      // wait until spawned DAYZ_UPDATER is available
      for (int i=0; i<20; i++) //max 20*200 = 4 seconds
      {
        Sleep(200);
        fnd = FindWindow("DAYZ_UPDATER", NULL);
        if (fnd!=NULL) break;
      }
      if (fnd==NULL)
      { // still no success => initialization has failed
        return false; //_ready=false
      }
    }

    // create in/out pipes 
    _pipe(&_pipes[0], 4096, O_TEXT); // I read/she writes pipe
    _pipe(&_pipes[2], 4096, O_TEXT); // She reads/I write pipe

    // close her pipes and restore stdin/stdout
    HANDLE hInHer = (HANDLE)_get_osfhandle(_pipes[2]);
    HANDLE hOutHer = (HANDLE)_get_osfhandle(_pipes[1]);
    HANDLE hIn, hOut; // these would be hInHer/Out duplicated to to her 
    DWORD processId;
    /*DWORD rtr = */GetWindowThreadProcessId(fnd, &processId);
    hProcess = OpenProcess(PROCESS_DUP_HANDLE,FALSE,processId);
    DuplicateHandle(GetCurrentProcess(),  hInHer, hProcess, &hIn, 0, FALSE, DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE);
    DuplicateHandle(GetCurrentProcess(),  hOutHer, hProcess, &hOut, 0, FALSE, DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE);
    PostMessage(fnd,USER_WM_CONNECT,(WPARAM)hIn,(LPARAM)hOut);

    if(hProcess && (unsigned int)hProcess!=0xffffffff)
    {
      _pipeManager.Init(PipeIn(), PipeOut());
      if ( _pipeManager.Ready() )
      {
        // send command line
        LPSTR cmdLine = GetCommandLine();
        _pipeManager.Send(CmdCommandline + cmdLine);

        // send working directory
        char cwdBuf[1024];
        GetCurrentDirectory(1024, cwdBuf);
        _pipeManager.Send(CmdCurDir + cwdBuf);

        // send copy directory
        char copyBuf[1024];
        GetCurrentDirectory(1024, copyBuf);  // it can differ to working directory in general, but the same for A2OA projects
        _pipeManager.Send(CmdCopyDir + copyBuf);

        // send ProcessID
        char pid[256];
        sprintf(pid, "%d", GetCurrentProcessId());
        _pipeManager.Send(CmdPID + pid);

        if (svnURL)
        {
          // send svn url to check new version for
          _pipeManager.Send(CmdSvnUrl + std::string(svnURL));
        }

        if (updateDir)
        {
          // send update dir (working directory which is or will be checkouted to SvnUrl)
          _pipeManager.Send(CmdUpdateDir + std::string(updateDir));
        }
  
        _ready = true;
      }
    }
  }
  return _ready;
}

bool SVNAutoUpdaterClient::Update()
{
  return true;
}

ISVNAutoUpdaterClient *CreateSVNAutoUpdaterClient()
{
  return new SVNAutoUpdaterClient();
}

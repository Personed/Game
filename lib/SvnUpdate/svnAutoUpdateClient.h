#ifndef _SVN_AUTO_UPDATER_H
#define _SVN_AUTO_UPDATER_H

#include "pipeManager.h"

#pragma warning( push )
#pragma warning(disable:4530)

#define USER_WM_CONNECT 0x50BA

class ISVNAutoUpdaterClient 
{
  public:
  virtual ~ISVNAutoUpdaterClient() {}

  virtual bool Update() = 0;
  virtual bool Init(const char *svnAUPath, const char *svnURL=NULL, const char *updateDir=NULL, const char *user=NULL, const char *passwd=NULL) = 0;
  virtual bool IsReady() const = 0;
};

class SVNAutoUpdaterClient: public ISVNAutoUpdaterClient
{
public:
  SVNAutoUpdaterClient() : _ready(false)
  {
    for (int i=0; i<sizeof(_pipes)/sizeof(_pipes[0]); i++)
    {
      _pipes[i] = -1;
    }
  }
  ~SVNAutoUpdaterClient()
  {
    _pipeManager.Send(PipeManager::CmdClosing);
    Close();
  }

  void Close()
  {
    // close my pipe ends
    if (_pipes[0]!=-1) { close(_pipes[0]); _pipes[0]=-1; }
    if (_pipes[3]!=-1) { close(_pipes[3]); _pipes[3]=-1; }
  }

  bool Update();

  // returns true when initialization was OK
  bool Init(const char *svnAUPath, const char *svnURL=NULL, const char *updateDir=NULL, const char *user=NULL, const char *passwd=NULL);
  bool IsReady() const {return _ready;}

  void Send(const char *cmd) { _pipeManager.Send(cmd); }

private:
  // Get my side in/out file descriptors
  int PipeIn() { return _pipes[0]; }
  int PipeOut() { return _pipes[3]; }

protected:
  bool  _ready;

  // pipe to communicate with SvnAutoUpdater (server)
  PipeManager _pipeManager;
  int _pipes[4];
  HANDLE hProcess;
};

ISVNAutoUpdaterClient *CreateSVNAutoUpdaterClient();

#pragma warning( pop )

#endif

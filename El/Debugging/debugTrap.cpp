#include <El/elementpch.hpp>
#include <Es/Common/win.h>
#include <Es/Files/commandLine.hpp>

#include "debugTrap.hpp"

#ifdef _WIN32

#include <Es/Threads/multisync.hpp>
#include <Es/Framework/appFrame.hpp>
#include "imexhnd.h"

class DebugThreadWatch
{
	// check if given thread is working
	mutable CriticalSection _lock;
	int _timeOut;
	int _enable; // <0 disabled, >=0 enabled

	HANDLE _mainThreadHandle;
	DWORD _mainThreadId;
	HANDLE _myThreadHandle;
	DWORD _myThreadId;
	Event _terminate;
	Event _keepAlive;

	public:
	DebugThreadWatch();
	~DebugThreadWatch();

	
	DWORD Execute();

	static DWORD WINAPI ExecuteHelper( void *watch )
	{
		return ((DebugThreadWatch*)watch)->Execute();
	}

	// external access functions
	void Terminate() {_terminate.Set();}
	void KeepAlive() {_keepAlive.Set();}
	void AliveEnable (int count)
	{
		ScopeLockSection lock(_lock);
		_enable+=count;
	}
	int GetAliveEnabled() const
	{
		ScopeLockSection lock(_lock);
		return _enable;
	}

	void SetAliveTimeout(int timeMs)
	{
		ScopeLockSection lock(_lock);
		_timeOut=timeMs;
	}
	bool CheckAliveExpected()
	{
		ScopeLockSection lock(_lock);
		// less then 60 seconds means we are expecting alives coming
		return _timeOut<60000;
	}

};

/*!
\patch 1.37 Date 12/18/2001 by Ondra
- Fixed: Freeze detection caused application shutdown.
In certain situation application can be considered frozen while is is actually only slow.
Now only context.bin is saved, but application continues running
even when it is supposed to be frozen.
*/

DebugThreadWatch::DebugThreadWatch()
{
	ScopeLockSection lock(_lock);
	_timeOut=-1;
	_enable=0;

	::DuplicateHandle(
		GetCurrentProcess(),GetCurrentThread(),
		GetCurrentProcess(),&_mainThreadHandle,
		0,false,DUPLICATE_SAME_ACCESS
	);
	_mainThreadId = GetCurrentThreadId();

	_myThreadHandle = ::CreateThread(
		NULL,64*1024,ExecuteHelper,this,CREATE_SUSPENDED,&_myThreadId
	);

	if (_myThreadHandle)
	{
		::ResumeThread(_myThreadHandle);
	}
}

/*!
\patch 1.28 Date 10/18/2001 by Ondra.
- New: When application is frozen, it creates context.bin and Flashpoint.rpt log
and after 30 sec it attempts to terminate.
*/

DWORD DebugThreadWatch::Execute()
{
	DWORD lastAlive=::GetTickCount();
	for(;;)
	{
		SignaledObject *wait[]={&_terminate,&_keepAlive};
		int waitTime,waitTime0;
		{
			ScopeLockSection lock(_lock);
			waitTime0 = waitTime = _timeOut;
			if (waitTime<0 || waitTime>10000) waitTime = 10000;
		}
		// waitTime (usually ~10 seconds) is how long we want to wait before freeze is detected
		// however the game needs to be responsive all the time
		// any unresponsiveness above 500 ms is bad based on Xbox TCR
		int aliveWait = 2000;
		DWORD start = ::GetTickCount();
    DWORD waitUntil = start+waitTime;
		for(;;)
    {
      int waitLeft = waitUntil-GetTickCount();
      if (waitLeft<0) waitLeft = 0;
      int eventWait = intMin(aliveWait,waitLeft);
      int which = SignaledObject::WaitForMultiple(wait,sizeof(wait)/sizeof(*wait),eventWait);
      if (which==0)
      {
        return 0; // _terminate event
      }
      else if (which==1)	 // alive event
      {
      	lastAlive=::GetTickCount();
        // when alive event is here, everything is OK
        // break out of a loop, so that we start waiting for the next one
        break;
      }
      else
      {
        // alive event not here, handle alive and freeze
        // report only when reporting is enabled
        if (_enable>0)
        {
          LogF("No alive in %d ms",aliveWait);
          LogF("  check another threads -- add ProgressRefresh()");
        }
    		DWORD now = ::GetTickCount();
        if (now>=waitUntil)
        {
          // long timeout passed
          if(waitTime0>=0 && waitTime0<20000 && _enable>0)
          {
            RptF("No alive in %d",waitTime);
            CONTEXT context;
            context.ContextFlags=CONTEXT_FULL;
            ::SuspendThread(_mainThreadHandle);
            if (GetThreadContext(_mainThreadHandle,&context))
            {
              GDebugExceptionTrap.ReportContext("FROZEN",&context,true);
            }
              ::ResumeThread(_mainThreadHandle);
  #if _ENABLE_REPORT
            if (::GetTickCount()-lastAlive>=30000)
            {
              // fatal situation - terminate
              RptF("Frozen - exiting");
              // do not attempt real termination
              // MT safety does not allow this
              TerminateProcess(GetCurrentProcess(),1);
              //DebugExceptionTrap::DDTerm();
              exit(1);
              /*NOT REACHED*/
            }
  #endif
          }
        }
        break;
      }
		} // for(;;)
	}
	#if defined(_MSC_VER) && (_MSC_VER<=1300)
	// .NET 7.1 - detect for(;;) is never finished
	return 0;
	#endif
}

DebugThreadWatch::~DebugThreadWatch()
{
}

struct ThreadWatchSingleArgumentContext
{
	bool nofreezecheck;
	bool forcefreezecheck;
	bool hidedebugger;
	bool forcedebugger;
	ThreadWatchSingleArgumentContext()
	{
		nofreezecheck = false;
		hidedebugger = false;
	  forcedebugger = false;
		forcefreezecheck = false;
	}
};
static bool ThreadWatchSingleArgument
(
	const CommandLine::SingleArgument &arg, ThreadWatchSingleArgumentContext &ctx
)
{
  // check if arg is recognized
  const char *beg = arg.beg;
  const char *end = arg.end;

	if( end==beg ) return false;
	if( *beg=='-' || *beg=='/' || *beg=='+' )
	{
		beg++;
		// option
		static const char nofreezecheck[]="nofreezecheck";
		static const char freezecheck[]="freezecheck";
		static const char hidedebugger[]="hidedebugger";
		static const char forcedebugger[]="forcedebugger";

		if( end-beg==(int)strlen(nofreezecheck) && !strnicmp(beg,nofreezecheck,end-beg) )
    {
			ctx.nofreezecheck = true;
      return false;
    }
		if( end-beg==(int)strlen(freezecheck) && !strnicmp(beg,freezecheck,end-beg) )
    {
			ctx.forcefreezecheck = true;
      return false;
    }
		if( end-beg==(int)strlen(hidedebugger) && !strnicmp(beg,hidedebugger,end-beg) )
		{
			ctx.hidedebugger = true;
			return false;
		}
		if( end-beg==(int)strlen(forcedebugger) && !strnicmp(beg,forcedebugger,end-beg) )
		{
			ctx.forcedebugger = true;
			return false;
		}
  }
  return false;
}

Debugger::Debugger()
{
	// dynamic function import
	_isDebugger=false;
  _enableThreadWatch = true;

	HMODULE kernel32 = LoadLibrary("kernel32.dll");
	if (kernel32)
	{
		typedef BOOL IsDebuggerPresentProc(VOID);
		IsDebuggerPresentProc *debPresent = (IsDebuggerPresentProc *)
		(
			GetProcAddress(kernel32,"IsDebuggerPresent")
		);
		if (debPresent)
		{
			_isDebugger = debPresent()!=FALSE;
			if (_isDebugger)
			{
				LogF("Starting debugger session");
			}
		}
	}

/*
  CommandLine cmd(GetCommandLine());
*/
  RString cmdLine = CurrentAppFrameFunctions->GetAppCommandLine();
  CommandLine cmd(cmdLine);

  cmd.SkipArgument(); // skip exe name

  ThreadWatchSingleArgumentContext ctx;
  ForEach<CommandLineForEachTraits>(cmd,ThreadWatchSingleArgument,ctx);
  if (ctx.hidedebugger)
  {
		_isDebugger = false;
  }
  else if (ctx.forcedebugger)
  {
		_isDebugger = true;
  }

	if (_isDebugger)
	{
		_enableThreadWatch = false;
	}
	
	if (ctx.nofreezecheck)
  {
    _enableThreadWatch = false;
  }
	else if (ctx.forcefreezecheck)
  {
    _enableThreadWatch = true;
  }
	
	// start light debugger thread
	StartWatchThread();
}

void Debugger::StartWatchThread()
{
  // freeze watch should be always available on PC
  // but not on Xbox retail version
  if (!_watch && _enableThreadWatch) _watch = new DebugThreadWatch();
}

void Debugger::ForceLogging()
{
	_isDebugger = false;
	StartWatchThread();
}

void Debugger::NextAliveExpected( int timeout )
{
	/*
	GlobalShowMessage(timeout,"Next alive in %d ms, enabled %d",timeout, CheckingAlivePaused());
	*/
	if (_watch)
	{
	  bool wasEnabled = _watch->CheckAliveExpected();
		_watch->SetAliveTimeout(timeout);
		if (wasEnabled)
		{
		  // when keep alive system was enabled, we want to pump it
		  ProcessAlive();
		}
		else
		{
		  _watch->KeepAlive();
		}
	}
}

bool Debugger::CheckingAlivePaused()
{
	if (_watch)
	{
		return _watch->GetAliveEnabled()<0;
	}
	return false;
}

void Debugger::PauseCheckingAlive()
{ // decrement counter - cheking enabled
	if (_watch)
	{
		_watch->AliveEnable(-1);
	}
}

void Debugger::ResumeCheckingAlive()
{ // increment counter - cheking enabled
	if (_watch)
	{
		_watch->AliveEnable(+1);
	}
	I_AM_ALIVE();
  // if (GProgress) GProgress->Refresh();
}


void Debugger::ProcessAlive()
{
	static int lastAlive=GetTickCount();

	int time = GetTickCount();
	int delay = time-lastAlive; 
	lastAlive = time;

	if (delay>1000)
	{
		__asm nop;
	}
	if (_watch) _watch->KeepAlive();
}

Debugger::~Debugger()
{
	if (_watch) _watch->Terminate();

	if (_isDebugger)
	{
		LogF("Closing debugger session");
	}
}

#else              // ifdef _WIN32

class DebugThreadWatch
{
};

Debugger::Debugger()
{
	_isDebugger = false;
}

void Debugger::ForceLogging()
{
	_isDebugger = false;
}

void Debugger::NextAliveExpected( int timeout )
{
}

bool Debugger::CheckingAlivePaused()
{
	return false;
}

void Debugger::PauseCheckingAlive()
{
}

/*!
\patch 5253 Date 4/10/2008 by Bebul
- Fixed: File Server was not working properly on Linux dedicated server. (Causing script files not being executed sometimes.)
*/
void Debugger::ResumeCheckingAlive()
{
  // This one is the place GlobalAlive is called on dedicated servers!
  // (It was missing on Linux (outside _WIN32) and caused FileServerST::Update NOT being called at all!)
  I_AM_ALIVE();
}

void Debugger::ProcessAlive()
{
}

Debugger::~Debugger()
{
}

#endif

// initialization
Debugger GDebugger;

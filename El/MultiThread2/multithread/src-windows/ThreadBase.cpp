#include "common_Win.h"
#include "../threadbase.h"
#include "../tls.h"
#include <malloc.h>
#include "../IBlocker.h"
#include "waitFunctions_win.h"
#include "../ThreadHook.h"
#include <typeinfo.h>
#include <stdio.h>

#if !_DEBUG
#include "Es/Framework/debugLog.hpp"
#include "Es/Memory/normalNew.hpp"
#endif


namespace MultiThread
{




#pragma warning(disable:4073)
#pragma init_seg (lib)
static Tls<ThreadBase> SCurrentThread;


ThreadBase::ThreadBase(void):_priority(PriorityNormal),_initStack(0)
{
  new(_threadData) ThreadBase_InfoStruct;
  IS().hThread=0;
  IS().threadId=0;
}

ThreadBase::ThreadBase(Priority priority, size_t initStack):_priority(priority),_initStack(initStack)
{
  new(_threadData) ThreadBase_InfoStruct;
  IS().hThread=0;
  IS().threadId=0;
}

ThreadBase::~ThreadBase(void)
{
  if (IsRunning() && GetCurrentThread()!=this)
  {  
    //MessageBox(NULL,_T("Critical:Destroying running thread!"),0,MB_SYSTEMMODAL|MB_OK|MB_ICONEXCLAMATION);
#if _DEBUG
    extern void LstF(char const *format,...);
    LstF("Critical:Destroying running thread! Application aborted.");
#else
    RptF("Critical:Destroying running thread! Application aborted.");
#endif
    abort();
  }
  if (IS().hThread) CloseHandle(IS().hThread);
}

bool ThreadBase::Start()
{
  return Start(false);
}
void ThreadHook_ThreadStartedNotify();

//
// Usage: SetThreadName (-1, "MainThread");
//
typedef struct tagTHREADNAME_INFO
{
  DWORD dwType; // must be 0x1000
  LPCSTR szName; // pointer to name (in user addr space)
  DWORD dwThreadID; // thread ID (-1=caller thread)
  DWORD dwFlags; // reserved for future use, must be zero
} THREADNAME_INFO;

static void SetThreadName( DWORD dwThreadID, LPCSTR szThreadName)
{
  THREADNAME_INFO info;
  {
    info.dwType = 0x1000;
    info.szName = szThreadName;
    info.dwThreadID = dwThreadID;
    info.dwFlags = 0;
  }
  __try
  {
    RaiseException( 0x406D1388, 0, sizeof(info)/sizeof(DWORD), (DWORD*)&info );
  }
  __except (EXCEPTION_CONTINUE_EXECUTION)
  {
  }
}

class ThreadBase_StartupClass: public ThreadHook
{
public:
  unsigned long ProcessThreadHook(ThreadBase *instance)
  {
    if (SCurrentThread!=0) return -1;
    ThreadHook_ThreadStartedNotify();

#ifdef _CPPRTTI
    const type_info &nfo=typeid(*instance);
    SetThreadName(-1,nfo.name());
#endif

    SCurrentThread=instance;

    bool init=instance->Init();
    unsigned long res=(unsigned long)-1;
    if (init) res=instance->Run();
    instance->Done(!init);
    return res;
  }
  

  ThreadBase_StartupClass()
  {
    ThreadHook::AddHook(this);    
  }
  ~ThreadBase_StartupClass()
  {
    ThreadHook::RemoveHook(this);
  }
};

static ThreadBase_StartupClass ___startup_hook;

static UINT CALLBACK StartThread(LPVOID data)
{
  ThreadBase *thread=reinterpret_cast<ThreadBase *>(data);
  return ThreadHook::StartThreadInstance(thread);
}

static int ToWindowsPriority(ThreadBase::Priority prior)
{
  switch(prior)
  {
  case ThreadBase::PriorityAboveNormal: return THREAD_PRIORITY_ABOVE_NORMAL;
  case ThreadBase::PriorityHigh: return THREAD_PRIORITY_HIGHEST;
  case ThreadBase::PriorityHighest: return THREAD_PRIORITY_TIME_CRITICAL;
  case ThreadBase::PriorityIdle: return THREAD_PRIORITY_IDLE;
  case ThreadBase::PriorityLow: return THREAD_PRIORITY_BELOW_NORMAL;
  case ThreadBase::PriorityNormal: return THREAD_PRIORITY_NORMAL;
  default:return THREAD_PRIORITY_NORMAL;
  }
}

bool ThreadBase::Start(bool suspended, void *securityDescriptor)
{
  bool res=false;

  IS().start.Lock();

  if (!IsRunning())
  {
    if (IS().hThread) CloseHandle(IS().hThread);

    unsigned long hndl;
    unsigned int id;
    
    
    hndl=_beginthreadex(securityDescriptor,(unsigned int)_initStack,StartThread,this,CREATE_SUSPENDED,&id);
    if (hndl!=(unsigned long)-1) 
    {
      IS().hThread=(HANDLE)hndl;
      IS().threadId=id;
      SetThreadPriority(IS().hThread,ToWindowsPriority(_priority));
      if (!suspended) Resume();
      res=true;
    }
  }
  IS().start.Unlock();
  return res;;
}

void ThreadBase::SetPriority(Priority priority)
{
  if (_priority!=priority)
  {  
    _priority=priority;
    SetThreadPriority(IS().hThread,ToWindowsPriority(_priority));
  }
}

bool ThreadBase::Join(unsigned long timeout) const
{
  if (IS().hThread) 
  {
    if (GetCurrentThreadId()!=IS().threadId)
      return ThreadInternal::WaitForSingleObject(IS().hThread,timeout);
  }
  return false;
}

bool ThreadBase::IsRunning() const
{
  return IS().hThread && !Join(0);
}

void ThreadBase::Sleep(unsigned long miliseconds)
{
  ::Sleep(miliseconds);
}

int ThreadBase::Suspend()
{
  if (IS().hThread)
    return SuspendThread(IS().hThread)+1;
  else
    return 0;
}

int ThreadBase::Resume()
{
  if (IS().hThread)
    return ResumeThread(IS().hThread);
  else
    return -1;
}


ThreadBase *ThreadBase::GetCurrentThread()
{
  return SCurrentThread;
}

ThreadBase *ThreadBase::AttachToCurrentThread()
{
  if (IS().threadId!=0) return this;
  ThreadBase *thisThrd=GetCurrentThread();
  if (thisThrd)
  {
    IS().threadId=thisThrd->IS().threadId;
    IS().hThread=thisThrd->IS().hThread;
    _priority=thisThrd->_priority;
    _initStack=thisThrd->_initStack;
  }
  else
  {
    DuplicateHandle(GetCurrentProcess(),::GetCurrentThread(),GetCurrentProcess(),&(IS().hThread),0,FALSE,DUPLICATE_SAME_ACCESS);
    IS().threadId=GetCurrentThreadId();
    _priority=PriorityNormal;
    _initStack=0;
  }
  SCurrentThread=this;
  return thisThrd;
}

void ThreadBase::DetachThread(ThreadBase *previous)
{
  if (previous==this) return;
  if (previous==0 || previous->IS().hThread!=IS().hThread)
  {
    CloseHandle(IS().hThread);
  }
  IS().hThread=0;

  SCurrentThread=previous;
}


unsigned long ThreadBase::GetExitCode() const
{
  if (IS().hThread)
  {
    unsigned long ret;
    if (GetExitCodeThread(IS().hThread,&ret)==FALSE) return -1;
    return ret;
  }
  else
    return -1;

}
int ThreadBase::JoinMulti(const BredyLibs::Array<ThreadBase *> &threads, bool waitAll,unsigned long timeout)
{
  HANDLE *hList=(HANDLE *)alloca(threads.Size()*sizeof(HANDLE));
  for (unsigned int i=0;i<threads.Size();i++) hList[i]=threads[i]->IS().hThread;
  return ThreadInternal::WaitForMultipleObjects(hList,threads.Size(),waitAll,timeout);
}


IRunnable *ThreadBase::GetCurrentRunnable()
{
  ThreadBase *curr=GetCurrentThread();
  if (curr==0) return 0;
  return curr->GetRunnable();
}

bool ThreadInternal::WaitForSingleObject(HANDLE object, DWORD timeout)
{
  ThreadBase *currentThread=ThreadBase::GetCurrentThread();
  if (currentThread) return currentThread->WaitFunction(ThreadWaitInfo(&object),timeout);
  else return WaitFn::DefaultWait(ThreadWaitInfo(&object),timeout);
}

int ThreadInternal::WaitForMultipleObjects(HANDLE *objects, int count, bool waitAll, unsigned long timeout)
{
  ThreadBase *currentThread=ThreadBase::GetCurrentThread();
  ThreadWaitInfo nfo(objects,count,waitAll);
  bool res;
  if (currentThread) res=currentThread->WaitFunction(nfo,timeout);
  else res=WaitFn::DefaultWait(nfo,timeout);
  if (res==false) return -1;
  return nfo.index;
}

IBlocker *ThreadBase::GetBlocker() const
{
  return const_cast<ThreadBase_InfoStruct *>(&(IS()));
}

bool ThreadBase::IsMineThread() const
{
  return GetCurrentThreadId()==IS().threadId;
}

unsigned long ThreadBase::StartCurrentThread()
{
  //lock Start function
  IS().start.Lock();
  //thread object or thread are already in use?
  if (IS().threadId!=0 || GetCurrentThread()!=0) 
  {
    //unlock Start function
    IS().start.Unlock();
    //this is not allowed
    return -1;
  }

#pragma warning(disable:4553)
  //Attach to the thread, function should return 0
  Verify(AttachToCurrentThread()==0);
#pragma warning(default:4553)
  //unlock Start function - it will now return false - thread already running
  IS().start.Unlock();

  SCurrentThread=0;
  //start thread instance, call hooks and Run
  unsigned long res=ThreadHook::StartThreadInstance(this);

  //lock Start function 
  IS().start.Lock();
  //detach the thread
  DetachThread(0);
  //unlock
  IS().start.Unlock();

  return res;
}

};

// CriticalSection.cpp: implementation of the CriticalSection class.
//
//////////////////////////////////////////////////////////////////////


#include "common_Win.h"
#include "../CriticalSection.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

namespace MultiThread
{

  ///Count of threads waiting for mini critical sections in whole application
  long MiniCriticalSection::SWaitCount=0;
  ///Handle of global blocker
  static HANDLE SMiniBlocker=0;
  ///Blocker ticket counter
  static long SMiniTicket=0;
  ///Mutex guards critical part of the WaitForCriticalSection function
  static HANDLE SMiniMutex=0;

  long ThreadInformation::CurrentThreadId()
  {
    return GetCurrentThreadId();
  }

  bool MiniCriticalSection::WaitForCriticalSection(unsigned long timeout)
  {
    //if mutex is not created yet, create it
    if (SMiniMutex==0)
    {
      HANDLE evObj=CreateMutex(0,FALSE,0);
      if (MTCompareExchange((long *)&SMiniMutex,(long)evObj,0)!=0)
      {
        CloseHandle(evObj); //somebody was faster, okay, drop the object
      }
    }

    //serialize access to this section
    WaitForSingleObject(SMiniMutex,INFINITE);  
    //increment number of waiting threads
    SWaitCount++;

    //If blocker is not created yet, create it
    if (SMiniBlocker==0)
    {
      SMiniBlocker=CreateEvent(0,TRUE,FALSE,0);
    }
    
    //calculate end of waiting
    DWORD endWait=GetTickCount()+timeout;
    
    //try to lock critical section
    //interlocked operation is still needed
    while (MTCompareExchange(&_owner_thread,CurrentThreadId(),0)!=0)
    {
      //calculate remaining time
      long remain=timeout==-1?0x7fffffff:endWait-GetTickCount();
      //no remaining time - exit and return false
      if (remain<0) 
      {
        SWaitCount--;
        ReleaseMutex(SMiniMutex);
        return false;
      }
      //release mutex and wait for event    
      SignalObjectAndWait(SMiniMutex,SMiniBlocker,remain,FALSE);
      //optain the mutex back
      WaitForSingleObject(SMiniMutex,INFINITE);  
    }

    //Remove one waiting thread
    SWaitCount--;
    //release mutex
    ReleaseMutex(SMiniMutex);

    return true;
  }

  void MiniCriticalSection::ReleaseCriticalSection()
  {
    //Lock mini mutex to ensure, that no thread will be inside of the blocking loop during event release
    WaitForSingleObject(SMiniMutex,-1);
    //Release all threads that are waiting on blocker, they will recheck status of mini critical section
    PulseEvent(SMiniBlocker);
    //Release all threads that are waiting on mutex, they will see, that they have an invalid ticket
    ReleaseMutex(SMiniMutex);
  }


#if !MULTITHREAD_USE_EXTERN_CRITICAL_SECTION
  bool CriticalSection::WaitForCriticalSection(unsigned long timeout)
  {
    long curth=CurrentThreadId();  //current thread id
    if (_spin)	//if spin defined
    {
      for (unsigned long p=0,cnt=_spin;p<cnt;p++)  //make spin cycle until section is not released
      {
        long owner=MTCompareExchange(&_owner_thread,curth,0); //mark critical section owned!
        if (owner==0)
        {
          _recursion_count=1;					//this is first recursion    
          return true;
        }
      }	
    }
    MTIncrement(&_wait_count);

    long owner=MTCompareExchange(&_owner_thread,curth,0); //mark critical section owned!
    while (owner!=0)
    {

      if (_blocker==0)      //event is not created, try to create it
      {
        HANDLE blocker=(void *)CreateEvent(NULL,FALSE,TRUE,NULL);  //create the blocker
        //we created signaled blocker to prevent deadlock on first request
        long lblk=(long)blocker;
        long *tblk=(long *)&_blocker;
        //try to save new blocker
        if (MTCompareExchange(tblk,lblk,0)!=0)
          CloseHandle(blocker); //save failed, somebody already created the blocker - destroy it
      }   

      if (!ThreadInternal::WaitForSingleObject((HANDLE)_blocker,timeout))
        return false;
      //Event is signaled, there is possible change, that section is ready for as
      owner=MTCompareExchange(&_owner_thread,curth,0); //mark critical section owned!
      //test success again
    }
    MTDecrement(&_wait_count);
    _recursion_count=1;
    return true;
  }

  void CriticalSection::ReleaseNextThread()
  {
    SetEvent((HANDLE)_blocker);
  }

  CriticalSection::~CriticalSection()
  {
    if (_blocker) CloseHandle((HANDLE)_blocker);
  }
#endif

};

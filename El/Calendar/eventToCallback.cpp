/**
@file   eventToCallback.cpp
@brief  Event -> callback converting thread.

Copyright &copy; 2003 by BIStudio (www.bistudio.com)
@author PE
@since  28.4.2003
@date   13.11.2003
*/

#ifdef _WIN32

#include "El/elementpch.hpp"

#include "eventToCallback.hpp"
#include "Es/Framework/logflags.hpp"
#include "Es/Framework/potime.hpp"

#if _ENABLE_REPORT
#  include "El/Debugging/stackMeter.hpp"
#endif

#include "El/Speech/debugVoN.hpp"

//-------------------------------------------------------------------------
//  Support:

int eventToHandle (const EventInfo &id)
{
  return (int)id.object;
}

EventInfo ImplicitMapTraits<EventInfo>::null   = {
  (HANDLE)-1,
  NULL,
  NULL,
  false
};

EventInfo ImplicitMapTraits<EventInfo>::zombie = {
  (HANDLE)-2,
  NULL,
  NULL,
  false
};

//-------------------------------------------------------------------------
//  EventToCallback:

THREAD_PROC_RETURN THREAD_PROC_MODE waitingRoutine ( void *param )
{
  EventToCallback *etc = (EventToCallback*)param;
  Assert( etc );

#if _ENABLE_REPORT
  STACK_INIT(58*1024);
#endif

#ifdef NET_LOG_ETC
  ThreadId myself;
  poThreadId(myself);
  NetLog("EventToCallback::waitingRoutine() hello 0x%x",(unsigned)myself);
#endif

  etc->handleLock.enter();                  // handleArray exclusivity
  do {
    // multiple-wait:
    //const int timeout=80;
    int handleArraySize = etc->handleArray.Size();
    if (handleArraySize>64)
    {
      handleArraySize = 64; //saturate not to cause freeze
      static bool reportFail = true;
      if (reportFail)
      { // report it only once
        reportFail = false;
        Fail("EventToCallback: Too many VoN players involved!");
      }
    }
    DWORD which = WaitForMultipleObjects(handleArraySize,etc->handleArray.Data(),FALSE,INFINITE/*timeout*/);
    if ( which == WAIT_OBJECT_0 ) 
    { // control-semaphore
      etc->handleLock.leave();              // free handleArray lock
      // stay in single-wait until the control-semaphore is signaled for the 2nd time..
      // This is the ONLY state in which "etc->handleArray" and "etc->eventArray" may be altered!
      WaitForSingleObject(etc->control,INFINITE);
      etc->handleLock.enter();              // handleArray exclusivity
    }
    else if ( which > WAIT_OBJECT_0 && which < WAIT_OBJECT_0 + etc->handleArray.Size() ) 
    {
      // regular handled event:
      HANDLE ev = etc->handleArray[ which - WAIT_OBJECT_0 ];
      EventInfo info;
#if defined(NET_LOG_ETC_VERBOSE) || DEBUG_VON
      unsigned64 now = getSystemTime();
#endif
      if ( etc->eventArray.get((int)ev,info) &&
        info.enabled && info.routine ) { // correct & enabled event -> trigger its call-back
          (*info.routine)(ev,info.data);
      }
#if defined(NET_LOG_ETC_VERBOSE) || DEBUG_VON
      NetLog("EventToCallback: catched: 0x%x, callback: %u us",(unsigned)ev,(unsigned)(getSystemTime()-now));
#endif
    }
    else if (which == WAIT_FAILED)
    {
      static bool reportIt = true;
      if (reportIt)
      { // report it only once
        reportIt = false;
        char buf[1024];
        int err = ::GetLastError();
        FormatMessage
          (
          FORMAT_MESSAGE_FROM_SYSTEM,
          NULL, // source
          err, // requested message identifier 
          0, // languague
          buf,sizeof(buf),
          NULL
          );
        Fail("EventToCallback waiting failed!");
        Fail(buf); // it has said "Invalid parameter"
      }
    }
    else if (which == WAIT_TIMEOUT)
    { //notification are extremely unreliable. That leads to combination of notifications and timer (100ms)
#if DEBUG_VON && 1
      NetLog("EventToCallback timeout: %u (START)", (unsigned)(getSystemTime()));      
#endif
      for (int i=1, siz=etc->handleArray.Size(); i<siz;  i++)
      {
        HANDLE ev = etc->handleArray[ i ];
        EventInfo info;
#if  DEBUG_VON && 1
        unsigned64 now = getSystemTime();
#endif
        if ( etc->eventArray.get((int)ev,info) &&
          info.enabled && info.routine ) { // correct & enabled event -> trigger its call-back
            (*info.routine)(ev,info.data);
        }
#if DEBUG_VON && 1
        NetLog("EventToCallback timeout: catched: 0x%x, callback: %u us",(unsigned)ev,(unsigned)(getSystemTime()-now));
#endif
      }
#if DEBUG_VON && 1
      NetLog("EventToCallback timeout: %u (END)", (unsigned)(getSystemTime()));      
#endif
    }
  } while ( etc->running );
  etc->handleLock.leave();                  // free handleArray lock

#ifdef NET_LOG_ETC
  NetLog("EventToCallback::waitingRoutine() goodbye 0x%x",(unsigned)myself);
#endif

#if _ENABLE_REPORT
  unsigned sUsage = STACK_METER(58*1024);
  if ( sUsage )
  {
#ifdef NET_LOG
#  ifdef NET_LOG_BRIEF
    NetLog("SM(wait,64K): %u",sUsage);
#  else
    NetLog("StackMeter(waitingRoutine,64KB): %u bytes",sUsage);
#  endif
#endif
    LogF("StackMeter(waitingRoutine,64KB): %u bytes",sUsage);
  }
  else
  {
#ifdef NET_LOG
#  ifdef NET_LOG_BRIEF
    NetLog("SM(wait,64K) overflow");
#  else
    NetLog("StackMeter(waitingRoutine,64KB) overflow");
#  endif
#endif
    LogF("StackMeter(waitingRoutine,64KB) overflow");
  }
#endif

  return (THREAD_PROC_RETURN)0;
}

EventToCallback::EventToCallback ()
: LockInit(handleLock,"EventCallback::handleLock",true)
{
  running = false;

  // prepare control-semaphore:
  control = CreateSemaphore(NULL,0L,INT_MAX,NULL);
  if ( !control ) {
    return;                                 // failed .. running == false
  }
  handleArray.Append() = control;

  // ... and finally I'm able to start waiting-thread:
  running = true;
  if ( poThreadCreate(&thread,64*1024,&waitingRoutine,this) ) {
    Verify( poSetPriority(thread,2) );      // the higher priority
  }
  else
    running = false;

#ifdef NET_LOG_ETC
  NetLog("EventToCallback: thread 0x%x was created (%d)",(unsigned)thread,(int)running);
#endif
}

EventToCallback::~EventToCallback ()
{
  if ( !control ) return;
  // now there is time for stopping the service (waiting-thread):
#ifdef NET_LOG_ETC
  NetLog("EventToCallback: thread 0x%x is to be stopped (%d)",(unsigned)thread,(int)running);
#endif
  enter();
  if ( running ) {
    running = false;
    ReleaseSemaphore(control,2L,NULL);      // get out from Multiple-wait & recover
    Verify( poThreadJoin(thread,NULL) );
  }
  leave();
  // free control-semaphore handle:
  CloseHandle(control);
  control = NULL;
}

void EventToCallback::addEvent ( HANDLE event, EventCallback *routine, void *data, bool enabled )
{
  if ( !control ) return;
#ifdef NET_LOG_ETC
  NetLog("EventToCallback::addEvent(%u)",(unsigned)event);
#endif
  EventInfo *info;
  enter();
  ReleaseSemaphore(control,1L,NULL);        // get out from Multiple-wait
  handleLock.enter();                       // handleArray exclusivity
  if ( (info = eventArray.get((int)event)) ) { // exists => rewrite
    info->routine = routine;
    info->data    = data;
    info->enabled = enabled;
  }
  else {                                    // add new item
    handleArray.Append() = event;
    EventInfo newInfo = { event, routine, data, enabled };
    eventArray.put(newInfo);
  }
  handleLock.leave();                       // can use handleArray again..
  ReleaseSemaphore(control,1L,NULL);        // recover
  leave();
}

void EventToCallback::removeEvent ( HANDLE event )
{
  if ( !control ) return;
#ifdef NET_LOG_ETC
  NetLog("EventToCallback::removeEvent(%u)",(unsigned)event);
#endif
  enter();
  if ( eventArray.presentKey((int)event) ) {
    // remove it:
    ReleaseSemaphore(control,1L,NULL);      // get out from Multiple-wait
    handleLock.enter();                     // handleArray exclusivity
    handleArray.Delete(event);
    eventArray.removeKey((int)event);
    handleLock.leave();                     // can use handleArray again..
    ReleaseSemaphore(control,1L,NULL);      // recover
  }
  leave();
}

void EventToCallback::enableEvent ( HANDLE event, bool enabled )
{
  if ( !control ) return;
#ifdef NET_LOG_ETC
  NetLog("EventToCallback::enableEvent(%u): %s",(unsigned)event,enabled?"on":"off");
#endif
  EventInfo *info;
  enter();
  // don't need to lock handleArray..
  if ( (info = eventArray.get((int)event)) )
    info->enabled = enabled;
  leave();
}

#endif

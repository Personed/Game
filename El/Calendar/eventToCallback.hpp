#ifdef _MSC_VER
#  pragma once
#endif

/**
@file   eventToCallback.hpp
@brief  Event -> callback converting thread.

Copyright &copy; 2003 by BIStudio (www.bistudio.com)
@author PE
@since  28.4.2003
@date   18.6.2003
*/

#ifndef _EVENTTOCALLBACK_H
#define _EVENTTOCALLBACK_H

#ifdef _WIN32

#include "Es/Common/win.h"
#include "Es/Containers/array.hpp"
#include "Es/Types/scopeLock.hpp"
#include "Es/Common/global.hpp"
#include "Es/Threads/pocritical.hpp"
#include "Es/Containers/maps.hpp"
#include "Es/Threads/pothread.hpp"
#include "Es/Threads/posemaphore.hpp"

//-------------------------------------------------------------------------
//  Support:

/// Callback routine triggered by an event.
typedef void EventCallback ( HANDLE object, void *data );

/// Used internally in EventToCallback.
struct EventInfo {

  /// signaled object (= hash-table key).
  HANDLE object;

  /// Call-back routine.
  EventCallback *routine;

  /// Data passed to the call-back routine (for user convenience, can be NULL).
  void *data;

  /// Enable/disable flag (disabled events need not be removed from event-array).
  bool enabled;

  bool operator == ( const EventInfo &ev ) const
  {
    return( object == ev.object );
  }

  bool operator != ( const EventInfo &ev ) const
  {
    return( object != ev.object );
  }

};

TypeIsMovableZeroed(EventInfo);

//-------------------------------------------------------------------------
//  EventToCallback:

extern int eventToHandle (const EventInfo &id);

class EventToCallback : public RefCountSafe {
protected:
  /// Critical section to lock object. RefCountSafe lock cannot be used due to possible DeadLock. 
  PoCriticalSection lock;

  /// Enter the object's critical section.
  inline void enter() const
  { lock.enter(); }

  /// Leave the object's critical section.
  inline void leave() const
  { lock.leave(); }

  /// Thread dedicated to Event->callback conversion.
  ThreadId thread;

  /// Control-semaphore.
  HANDLE control;

  /// true if the waiting-thread is running.
  bool running;

  /// Handle-array (passed to WaitForMultipleObjects). Control-event must be handleArray[0]!
  FindArray<HANDLE,MemAllocSafe> handleArray;

  /// Event-array.
  ImplicitMap<int,EventInfo,eventToHandle,true,MemAllocSafe> eventArray;

  /// Lock for handleArray (owned by waitingRoutine() in most times).
  PoCriticalSection handleLock;

  friend THREAD_PROC_RETURN THREAD_PROC_MODE waitingRoutine ( void *param );

public:

  /// Starts waiting-thread.
  EventToCallback ();

  virtual ~EventToCallback ();

  /// Adds a new event (rewrites its data if it is already present).
  virtual void addEvent ( HANDLE event, EventCallback *routine, void *data, bool enabled =true );

  /// Removes the existing event.
  virtual void removeEvent ( HANDLE event );

  /// Enables/disables the existing event.
  virtual void enableEvent ( HANDLE event, bool enabled =true );

};

#else

#error "EventToCallback can be used on WIN32 only"

#endif

#endif

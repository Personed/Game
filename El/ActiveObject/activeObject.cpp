#include <El/elementpch.hpp>
#include <El/ActiveObject/activeObject.hpp>
#include "El/Common/perfProf.hpp"

void Journal::Page()
{
  // remove the last page
  Reset(_pages[maxPages-1]._content);
  // manipulating pages can be done MT unsafe, nobody but this is accessing them
  for (int i=maxPages-1; i>0; i--)
  { // make pages older
    _pages[i] = _pages[i-1];
  }
  _pages[0]._content.Flush(); // make empty, but do not care about data which were there, they are already copied

  // paging live data needs to be safe
  SListMT<Item> temp;
  _data.GetAllReversed(temp);
  temp.GetAllReversed(_pages[0]._content);
}

void EventSpinning::Wait() const
{
  PROFILE_SCOPE_DETAIL_EX(evWai,mt)
  while(_value==0)
  {
    YieldProcessorNice();
  }
}

void EventSpinning::Set()
{
  // set event first, value later - testing is done the other way round
  PROFILE_SCOPE_DETAIL_EX(evSet,mt)
//   _event.Set();
//   PROFILE_SCOPE_DETAIL_EX(evSeV,mt)
  _value = AtomicInt(1);
}
bool EventSpinning::TryWait() const
{
  return _value!=0;
}

bool EventSpinningShort::TryWait(int timeout) const
{
  // TODO: implement timeout
  // test only first so that busy wait is not writing
  if (_value!=0 && _value.CompareSwap(1,0)!=0)
  {
    // value is null now
    _event.Reset();
    if (_value)
    {
      // prevent race - value not null, meaning going to signalled again
      _event.Set();
    }
    return true;
  }
  return false;
}


bool EventSpinningShort::Wait(int timeout) const
{
  PROFILE_SCOPE_DETAIL_EX(esWai,mt)
  int spinCount = 5000;
  while (--spinCount>0)
  {
    if (TryWait())
    {
      _event.Reset();
      return true;
    }
    YieldProcessorNice();
  }
  if (timeout!=0)
  {
    // TODO: implement timeout
    PROFILE_SCOPE_DETAIL_EX(esYie,mt)
    _event.Wait();
    _value = AtomicInt(0);
    // TODO: consider effects of possible race conditions between event / value setting / resetting
    _event.Reset();
    if (_value)
    {
      // prevent race - value not null, meaning going to signalled again
      _event.Set();
    }
    return true;
  }
  return false;
}

void EventSpinningShort::Set()
{
  // set event first, value later - testing is done the other way round
  PROFILE_SCOPE_DETAIL_EX(evSet,mt)
  // _value is always indicative of what we want event to be
  _value = AtomicInt(1);
  _event.Set();
}

const int EventRecyclingPool::_maxPoolSize = 256;

EventRecyclingPool::EventRecyclingPool()
:_used(0)
{
  DoAssert(sizeof(HANDLE)==sizeof(AtomicInt));
  _pool = new AtomicInt[_maxPoolSize];
  for (int i=0; i<_maxPoolSize; i++) _pool[i] = AtomicInt(0);
}
EventRecyclingPool::~EventRecyclingPool()
{
  for (int i=0; i<_used; i++)
    if (_pool[i])
      CloseHandle((HANDLE)(int)_pool[i]);
  delete [] _pool;
}


void EventRecyclingPool::Recycle(HANDLE handle)
{
  for (int i=0; i<_maxPoolSize; i++)
  {
    if (_pool[i].CompareSwap(0,(int)handle)==0)
    {
      // make sure _used contains i
      #if 1
      for (;;)
      {
        int usedOriginal = _used;
        int usedMax = intMax(_used,i+1);
        if (_used.CompareSwap(usedOriginal,usedMax)==usedOriginal) break;
      }
      #else
      // using or is not accurate, but it is very fast and conservative
      _used |= i;
      #endif
      
      return;
    }
  
  }
  CloseHandle(handle);
}

#ifdef _WIN32
HANDLE EventRecyclingPool::Use()
{
  // search for a first non-null
  int used = _used;
  for (int i=0; i<used; i++)
  {
    if (int ret = _pool[i].Swap(0))
    {
      // if we have used the last item, remove it from _used
      _used.CompareSwap(i+1,i);
      HANDLE handle = (HANDLE)ret;
      ResetEvent(handle);
      return handle;
    }
  }
  // events use manual reset object
  return CreateEvent(NULL,TRUE,FALSE,NULL);
}
#else
HANDLE EventRecyclingPool::Use()
{
  return 0;
}
#endif

Void *AllArgsBase<Void>::retVal = NULL;

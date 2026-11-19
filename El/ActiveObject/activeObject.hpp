#ifdef _MSC_VER
#pragma once
#endif

#ifndef _EL_COMMAND_QUEUE
#define _EL_COMMAND_QUEUE

#include <El/elementpch.hpp>
#include <El/Common/perfProf.hpp>
#include <El/Multicore/multicore.hpp>
#include <Es/Containers/array.hpp>
#include <Es/Threads/multisync.hpp>
#include <Es/Threads/pothread.hpp>

#if !defined _WIN32
  // no alignment requirements on Xbox
  #define ALIGN_SLIST
  #define AssertSLISTAlign(entry)
#ifndef _WIN32
  struct SLIST_ENTRY {
    struct SLIST_ENTRY *Next;
  };
  typedef union _SLIST_HEADER {
    unsigned long long Alignment;
    struct {
      SLIST_ENTRY *Next;
      WORD   Depth;
      WORD   Sequence;
    };
  } SLIST_HEADER;
#endif
#else
  // on PC SLIST_ENTRY must be aligned to MEMORY_ALLOCATION_ALIGNMENT
  #define ALIGN_SLIST __declspec(align(MEMORY_ALLOCATION_ALIGNMENT))
  #define AssertSLISTAlign(entry) ((intptr_t(entry)&(MEMORY_ALLOCATION_ALIGNMENT-1))==0)
#endif


#ifdef _WIN32
/// wrapper around Win32 SList (InitializeSListHead, InterlockedPushEntrySList, InterlockedPopEntrySList, InterlockedFlushSList)
template <class Type>
class SListMT
{
  SLIST_HEADER _list;
  
  public:
  typedef SLIST_ENTRY Base;
  
  SListMT()
  {
    InitializeSListHead(&_list);
  }
  
  /// get a chain of all items (used for FIFO access)
  Type *Flush() {return static_cast<Type *>(InterlockedFlushSList(&_list));}
  Type *Pop() {return static_cast<Type *>(InterlockedPopEntrySList(&_list));}
  void Push(Type *item) {InterlockedPushEntrySList(&_list,item);}
  
  void GetAllReversed(SListMT &tgt)
  { // avoid casting to and from Type - make the routine code a lot more compact and easier to debug
    SLIST_ENTRY *entry = InterlockedFlushSList(&_list);
    // walk and push
    while (entry)
    {
      SLIST_ENTRY *next = entry->Next;
      InterlockedPushEntrySList(&tgt._list,entry);
      entry = next;
    }
  }

  void GetAllReversed(SListMT &tgt, Type *skip)
  { // avoid casting to and from Type - make the routine code a lot more compact and easier to debug
    SLIST_ENTRY *entry = InterlockedFlushSList(&_list);
    // walk and push
    while (entry)
    {
      SLIST_ENTRY *next = entry->Next;
      if (entry!=skip)
        InterlockedPushEntrySList(&tgt._list,entry);
      entry = next;
    }
  }
  
  /// no pointer to new or freed memory may exist
  bool CheckIntegrity() const
  {
    SLIST_ENTRY *entry = _list.Next.Next;
    while (entry)
    {
      SLIST_ENTRY *next = entry->Next;
      if (intptr_t(next)==0x7f817f81 || intptr_t(next)==0x7f8f7f8f)
      {
        Fail("Invalid entry");
        return false;
      }
      entry = next;
    }
    return true;
  }  
  ~SListMT()
  {
    // verify there is nothing left
    Assert(Flush()==NULL);
  }
};
#else
/// MultiThread unsafe implementation for Linux server
template <class Type>
class SListMT
{
  SLIST_HEADER _list;
public:
  typedef SLIST_ENTRY Base;
  SListMT()
  {
    //Fail("SListMT test version on Linux!");
    _list.Next = NULL;
  }

  /// get a chain of all items (used for FIFO access)
  Type *Flush() 
  {
    Type *itemsChain = static_cast<Type *>(_list.Next);
    _list.Next = NULL;
    //RptF("SListMT::Flush - %x", &_list);
    return itemsChain;
  }
  Type *Pop()
  {
    Type *firstItem = static_cast<Type *>(_list.Next);
    if (firstItem) 
    {
      _list.Next = firstItem->SLIST_ENTRY::Next;
      firstItem->SLIST_ENTRY::Next = NULL;
    }
    //RptF("SListMT::Pop - %x", &_list);
    return firstItem;
  }
  void Push(Type *item)
  {
    // insert before first element
    item->SLIST_ENTRY::Next = _list.Next;
    _list.Next = item;
    //RptF("SListMT::Push - %x", &_list);
  }

  void GetAllReversed(SListMT &tgt, Type *skip=NULL)
  {
    Type *entry = Flush();
    // walk and push
    while (entry)
    {
      Type *next = static_cast<Type *>(entry->Next);
      if (entry!=skip)
        tgt.Push(entry);
      entry = next;
    }
  }
  
  // dummy implementation
  bool CheckIntegrity() const
  {
    return true;
  }
  
  ~SListMT() {}
};
#endif


/// stored refs waiting to be released
template <class Type>
struct ThreadSafeQueue
{
  struct QueuePart: public AutoArray<Type, MemAllocLocal<Type,256> >, public SLIST_ENTRY
  {
  };

  SListMT<QueuePart> _queue;

  void Add(const Type &item)
  {
    QueuePart *part = _queue.Pop();
    if (!part)
    { // not initialized, or another thread is currently using it
      part = new QueuePart;
    }
    part->Add(item);
    _queue.Push(part);
  }

  template <class Functor>
  void ForEach(const Functor &func)
  {
    QueuePart *first = NULL;
    while (QueuePart *part =_queue.Pop())
    {
      part->ForEachF(func);
      // keep the first list, but compact it and destruct items (resize after compacting, so that compacting respects achieved size)
      if (!first) first = part, first->CompactIfNeeded(2,256), first->Resize(0);
      else delete part; // delete the rest
    }
    if (first) _queue.Push(first);
  }

  ~ThreadSafeQueue()
  {
    while (QueuePart *part =_queue.Pop())
    {
      delete part;
    }
  }
};

typedef ThreadSafeQueue<RefCount *> StoredRefContainer;

template <class Type, StoredRefContainer *store>
class StoredRef: public InitPtr<Type>, private NoCopy
{
  typedef InitPtr<Type> base;

public:
  StoredRef(){}
  StoredRef(Type *src):base(src){Store(src);}
  ~StoredRef(){Destroy(base::GetRef());}
  void Free()
  {
    Destroy(base::GetRef());
    base::operator = (nullptr);
  }

  void operator = (const StoredRef &src)
  {
    Assert(!base::GetRef());
    base::operator = (src);
    Store(src);
  }

  static void Store(Type *src) {if (src) src->AddRef();}
  static void Destroy(Type *src) {if (src) store->Add(src);}
};


/// event with spin-lock implemented
class EventSpinning
{
  AtomicInt _value;
  // TODO: event used optionally for a single/few-core implementation
  //Event _event;
  public:
  EventSpinning():_value(0){}
  EventSpinning(bool manualReset):_value(0){Assert(manualReset);}
  void Set();
  void Wait() const;
  bool TryWait() const;
};

/// event with short spin-lock implemented
class EventSpinningShort
{
  mutable Event _event;
  mutable AtomicInt _value;
  
  public:
  EventSpinningShort():_event(true),_value(0){}
  EventSpinningShort(bool manualReset):_event(true),_value(0){Assert(!manualReset);}
  void Set();
  bool Wait(int timeout=-1) const;
  bool TryWait(int timeout=0) const;
};

class EventRecyclingPool: private NoCopy
{
  AtomicInt *_pool;
  AtomicInt _used;

  static const int _maxPoolSize;
    
  public:
  void Recycle(HANDLE handle);
  
  HANDLE Use();
  
  EventRecyclingPool();
  ~EventRecyclingPool();
};

/// thread safe local logging facility
class Journal: private NoCopy
{
  struct Item: SLIST_ENTRY
  {
    RString log;
    
    Item(const RString &log):log(log){}
  };
  
  SListMT<Item> _data;
  
  static const int maxPages = 4;
  struct Page
  {
    SListMT<Item> _content;
  };
  Page _pages[maxPages];

  static void Reset(SListMT<Item> &root)
  {
    Item *item = root.Flush();
    while (item)
    {
      Item *next = static_cast<Item *>(item->Next);
      delete item;
      item = next;
    }
  }

  public:
  void Add(const char *text)
  {
    Item *item = new Item(Format("%5d: %s",GetCurrentThreadId(), text));
    _data.Push(item);
  }
  ~Journal()
  {
    Reset();
  }
  void Page();

  void Reset()
  {
    Reset(_data);
  }
};

/// thread safe local logging facility
class DummyJournal: private NoCopy
{
  public:
  void Add(const char *text) {}
};

/// provide a handle to a future result
template <class Value>
class FutureValue: public RefCountBase, private NoCopy
{
  Value _value;
  //EventSpinning _done;
  Event _done;
  /// how many threads wait for this future
  mutable AtomicInt _waited;
  
  static EventRecyclingPool _pool;
  
  
  #if _DEBUG
    mutable Journal _journal;
    #define JOURNAL(x) _journal.Add(x)
  #else
    #define JOURNAL(x)
  #endif
  
  struct Count
  {
    #if _ENABLE_REPORT
    AtomicInt _count;
    
    void operator ++ () {++_count;}
    void operator -- () {--_count;}
    
    ~Count()
    {
      if (_count!=0)
      {
        ErrF("Leaked %d in %s",(int)_count,typeid(Count).name());
      }
    }
    
    #else
    
    void operator ++ () {}
    void operator -- () {}
    
    #endif
    
  } static _countHandles;
  
  public:
  FutureValue()
  :_done(NoInitHandle),_waited(0) // manual reset - once set, it stays set forever
  {
    _done.InitFromHandle(_pool.Use());
    ++_countHandles;
    JOURNAL("Construct");
  }
  FutureValue(const Value &value)
  :_value(value), _done(NoInitHandle), _waited(0) // uninitialized event - no operations on event will be processed
  {
    //_done.Set(); // calling Set is nonsense here - NULL handle used
  }
  static void Recycle(HANDLE temp)
  {
    Assert(temp!=NULL);
    
    --_countHandles;
    _pool.Recycle(temp);
    //CloseHandle(temp);
  }
  void SetResult(const Value &value)
  {
    _value = value;
    _done.Set();
    JOURNAL(Format("SetResult %d",_done.GetHandle()));
    // give opportunity do destroy / recycle the object
    RecycleEvent();
  }
  
  void RecycleEvent() const
  {
    // atomic detach: we do not care who has detached the handle, but it must be done atomic
    if (_waited.CompareSwap(0,INT_MIN)==0)
    {
      HANDLE detach = _done.DetachHandle();
      Assert(detach);
      JOURNAL("GetResult recycling");
      Recycle(detach);
    }
    else
    {
      JOURNAL("GetResult recycling ignored");
    }
  }
  const Value &GetResult() const
  {
    if (!_done.IsInitialized())
    {
      //JOURNAL("GetResult !init");
      MemorySubscribe();
      return _value;
    
    }
    JOURNAL(Format("GetResult init %d, waited %d",_done.GetHandle(),int(_waited)));
    if (_waited<0) // "double check" to prevent using interlocked operation in most cases where not necessary
    {
      // once there is -1, the event is being recycled
      JOURNAL("GetResult recycled");
      return _value;
    }
    // beware of race: may become negative meanwhile
    if (++_waited<=0)
    {
      --_waited;
      JOURNAL("GetResult recycled");
      return _value;
    }

    Assert(_done.IsInitialized()); // waited>0 will prevent the handle to be recycled by other calls
    JOURNAL(Format("GetResult wait %d",_done.GetHandle()));
    PROFILE_SCOPE_DETAIL_EX(ftGet,async);
    _done.Wait();
    JOURNAL(Format("GetResult wait done %d",_done.GetHandle()));
    if (--_waited==0)
    {
      RecycleEvent();
    }
    return _value;
  }
  
  ~FutureValue()
  {
    // if nobody set the value yet, we need to clean it up
    if (_done.IsInitialized())
    {
      HANDLE temp = _done.DetachHandle();
      Recycle(temp);
    }
  }
  
  operator const Value &() const {return GetResult();}
  
  bool IsReady() const {return _done.IsInitialized() ? _done.TryWait() : true;} // uninitialized event - value is always ready
  
  int Release() const
  {
    int ret=DecRef();
    if( ret==0 ) delete this;
    return ret;
  }
};

template <class Value>
typename FutureValue<Value>::Count FutureValue<Value>::_countHandles;

template <class Value>
EventRecyclingPool FutureValue<Value>::_pool;

/// used when function returns no value, but we need to be able to wait for it
struct WaitableVoid {};

typedef void Void;


template <class Type>
class Future: private Ref< FutureValue<Type> >
{
  typedef Ref< FutureValue<Type> > base;
  
  public:
  Future():base(new FutureValue<Type>){}
  
  /// retrieving result may block
  const Type &GetResult() const {return base::GetRef()->GetResult();}
  void SetResult(const Type &value) const {base::GetRef()->SetResult(value);};
  //operator const Type &() const {return base::GetRef()->GetResult();}
  bool IsReady() const {return base::GetRef()->IsReady();}
  /// future can be constructed from a value anytime, and cheap
  explicit Future(const Type &value):base(new FutureValue<Type>(value)){}

  enum _nullFuture {Null};
  Future(_nullFuture null){}
  bool IsNull() const {return base::IsNull();}
  bool NotNull() const {return base::NotNull();}

  /// check if the same FutureValue is referenced
  bool IsSameFuture(Future with) const {return with.GetRef() == base::GetRef();}

  /// diagnostics support
  RString FormatValue() const
  {
    return Format("%p", base::GetRef());
  }
};

/// specialization - void needs no allocation
template <>
class Future<Void>
{
  public:
  __forceinline void *GetRef() const {return NULL;}
};

/// make sure each command is aligned so that subsequent commands can be aligned too
/**
Note: cleaner alternative would be to introduce before read/before write alignment via a new function of SizeOf
However defining it globally here is a lot simpler (and results in better performance too)
*/
static inline size_t AlignStoreSize(size_t size)
{
  const int alignment = sizeof(int);
  return (size+alignment-1)&~(alignment-1);
}

/// some structures might contain a dynamic information in them
template <class Type>
struct StoredSize
{
  static size_t SizeOf(const Type &val) {return AlignStoreSize(sizeof(val));}
};
/// default implementation for storing into SerialArray - allows external overloading for individual types
/** implemented because of ComRef specific needs */

template <class DstType, class SrcType>
struct SerialStoreTraits
{
  static void Store(DstType &dst, const SrcType &src)
  {
    dst = src;
  }
};

/// easy way to call SerialStoreTraits::Store without having to provide template parameters
template <class DstType, class SrcType>
inline void SerialStore(DstType &dst, const SrcType &src)
{
  SerialStoreTraits<DstType,SrcType>::Store(dst,src);
}

/// command storage used for CommandBuffers - used for recording
template < int bufferSize=1024, class Container=AutoArray<char, MemAllocLocal<char, bufferSize> > >
class SerialArraySimple
{
  Container _data;
  int _writeTo;

  public:
  
  SerialArraySimple()
  {
    _writeTo = 0;
  }
  ~SerialArraySimple()
  {
  }
  
  /**
  avoid using _writeTo member, because this leads to compiler assuming aliasing
  */
  template <class Type>
  void Set(Type *&val, int writeTo)
  {
    // reserve maximum possible space for the item
    _data.Access(writeTo+AlignStoreSize(sizeof(Type))-1);
    val = (Type *)&_data[writeTo];
    // construct the target area, so that it is ready for an initialization (will be done member by member)
    ConstructAt(*val);
  }
  template <class Type>
  int Put(const Type &val, int writeTo)
  {
    // construct the target area, so that it is ready for an initialization
    int writeEnd = writeTo+StoredSize<const Type>::SizeOf(val);
    _data.Access(writeEnd-1);
    Type *dst = (Type *)&_data[writeTo];
    ConstructAt(*dst);
    // assign the value
    // and advance
    *dst = val;
    // no need to wrap here - wrap is done on FinishWrite only
    return writeEnd;
  }
  
  __forceinline int StartWrite() {return _writeTo;}
  
  template <class Type>
  __forceinline void FinishWrite(const Type *val,int writeTo)
  {
    // we know the size only after the value is written to
    int written = StoredSize<const Type>::SizeOf(*val);
    // verify there is no misalignment caused by the data written
    Assert((written&(sizeof(int)-1))==0);
    //LogF("W %x (%d)",writeTo,written);
    Assert(writeTo+written<=_data.Size());
    _writeTo = writeTo+written;
  }

  /// access to allow reading the command buffer
  const void *GetData(int &size) const
  {
    // verify nobody else is reading the data, and the buffer is large enough to contain the data
    Assert(_data.Size()>=_writeTo);
    size = _writeTo;
    return _data.Data();
  }
  
  int GetCurrentOffset() const {return _writeTo;}
  
  /// reset the command buffer - start recording again
  void Reset()
  {
    _data.Resize(0);
    _writeTo = 0;  
  }
};

/// lightweight counterpart of SerialArrayCircular - read only data
class SerialArrayData
{
  const Array<char> &_data;
  
  public:
  
  SerialArrayData(const Array<char> &data):_data(data)
  {
  }
  ~SerialArrayData()
  {
  }
  
  
  template <class Type>
  __forceinline RESTRICT_RETURN const Type *Get(int &pos)
  {
    #if _DEBUG
    int storedSize = StoredSize<const Type>::SizeOf(*(Type *)(_data.Data()+pos));
    Assert(pos+storedSize<=_data.Size());
    #endif
    // it is quite possible the target may not fit inside - it may be a truncated variables sized item
    //Assert(_readFrom+(int)sizeof(Type)<=_data.Size());
    // target is already constructed - we need to destruct the data first, so that we can move on
    // simply return a pointer to a data we already have - no copy needed
    return (Type *)(_data.Data()+pos);
  }
  template <bool destruct, class Type>
  __forceinline void Advance(Type * __restrict val, int &pos)
  {
    // we are done with the data - we may destruct them now
    Assert(val==(Type *)(_data.Data()+pos));
    // before destructing them we need to read their size
    // note: we always pass a "const Type" to the StoredSize to avoid passing sometimes const Type and sometimes Type
    size_t size = StoredSize<const Type>::SizeOf(*val);
    if (destruct) val->~Type();
    // and advance over them - this will also enable writing into them
    //LogF("R %x (%d)",_readFrom,size);
    pos += size;
    
  }
  
  __forceinline bool IsEmpty(int pos) const {return pos>=_data.Size();}
    
};

/// lightweight counterpart of active object - command buffer recording
template <class ServantClass,class Container>
class CommandBufferRec
{
  protected:
  typedef ServantClass Servant;
  
  /// a list of functions to be called, including their arguments
  Container _invocationList;
  
  #if _DEBUG
  // for debugging purposes we record everything in text form as well
  CharArray<> _debug;
  
  #endif
  
  public:
  
  CommandBufferRec()
  {
    #if _DEBUG
    _debug.Reset();
    #endif
  }
  
  const void *GetData(int &size) const
  {
    return _invocationList.GetData(size);
  }

  int GetDataSize() const
  {
    int size;
    _invocationList.GetData(size);
    return size;
  }
  
  int GetCurrentOffset() const {return _invocationList.GetCurrentOffset();}
  
  void Reset()
  {
    _invocationList.Reset();
    #if _DEBUG
    _debug.Reset();
    #endif
  }

  #if _DEBUG
  const char *GetDebugText() const {return _debug;}
  void AppendDebugText(const char *text){_debug+=text;}
  #else
  const char *GetDebugText() const {return "";}
  void AppendDebugText(const char *text){}
  #endif
};

/// lightweight counterpart of active object - command buffer playback
template <class ServantClass>
class CommandBufferPlay: protected ServantClass
{
  protected:
  typedef ServantClass Servant;
  
  public:
  
  CommandBufferPlay(){}
  
  template <bool destruct, class InvocationContainer>
  void Execute(InvocationContainer &invocationList);

  template <class InvocationContainer>
  void Destruct(InvocationContainer &invocationList);
};

template <class ServantClass>
template <bool destruct, class InvocationContainer>
void CommandBufferPlay<ServantClass>::Execute(InvocationContainer &invocationList)
{
  // adapted from ActiveObject::Update
  int pos = 0;
  while (!invocationList.IsEmpty(pos))
  {  
    // some message is there - check it
    // first get message ID
    const int *idPtr = invocationList.template Get<int>(pos);
    int id = *idPtr;
    invocationList.Advance<destruct>(idPtr,pos);
    Servant::ProcessItem<destruct>((typename Servant::ActObjIds)id,invocationList,pos);
  }
}

template <class ServantClass>
template <class InvocationContainer>
void CommandBufferPlay<ServantClass>::Destruct(InvocationContainer &invocationList)
{
  // adapted from ActiveObject::Update

  int pos = 0;
  while (!invocationList.IsEmpty(pos))
  {  
    // some message is there - check it
    // first get message ID
    /*
    According to C++'03 Standard 14.2/4:
    When the name of a member template specialization appears after . or -> in a postfix-expression, 
    or after nested-name-specifier in a qualified-id, and the postfix-expression or qualified-id explicitly 
    depends on a template-parameter (14.6.2), the member template name must be prefixed by the keyword template. 
    Otherwise the name is assumed to name a non-template.
    */
    const int *idPtr = invocationList.template Get<int>(pos);
    int id = *idPtr;
    invocationList.Advance<true>(idPtr,pos);
    Servant::DestructItem((typename Servant::ActObjIds)id,invocationList,pos);
  }
}

/// optional return value
template <class Ret>
struct AllArgsBase: private NoCopy
{
  typedef Future<Ret> RetType;
  
  Future<Ret> retVal;
};

template <>
struct AllArgsBase<Void>
{
  typedef Void * RetType;
  static Void *retVal;
};


template <class Type>
class ActObjTraits
{
  typedef Type ReturnType;
};

template <>
class ActObjTraits<Void>
{
  typedef void ReturnType;
};

/// used to define argument types
#define ACTOBJ_DECLARE_ARGS(ctx,Ret,Name,Args,NumArgs,DefArgs) \
  struct Name##AllArgs: public AllArgsBase<Ret>  { DefArgs; };

/// used to declare functions
#define ACTOBJ_DECLARE_FUNCTION(ctx,Ret,Name,Args,NumArgs,DefArgs) \
  Ret Name(const Name##AllArgs &args);

/// used to generate enum identifying messages
#define ACTOBJ_ID_FUNCTION(Type,Ret,Name,Args,NumArgs,DefArgs) AO##Name,


#define DECLARE_ACTIVE_OBJECT_SERVANT(Type,MESSAGES) \
  MESSAGES(Type,ACTOBJ_DECLARE_FUNCTION) \
  enum ActObjIds { MESSAGES(Type,ACTOBJ_ID_FUNCTION) };  \
  IS_ACTIVE_OBJECT(Type,MESSAGES)

/**
Ret is not used directly, but specializations are created based on it
*/
template <class Type, class Ret, class FuncType, class ArgsType>
struct CallFunction
{
  __forceinline void CallAndSetResult(Type *obj, FuncType func, const ArgsType &args)
  {
    args.retVal.SetResult((obj->*func)(args));
  }
};

/// partial specialization which avoids attempting using void return value
template <class Type, class FuncType, class ArgsType>
struct CallFunction<Type,void,FuncType,ArgsType>
{
  __forceinline void CallAndSetResult(Type *obj, FuncType func, const ArgsType &args)
  {
    (obj->*func)(args);
  }
};


template <class Args> struct ActObjItem;

struct ActObjItemHeader: public SLIST_ENTRY
{
  int id;

  /// using static_cast we do not rely on any particular alignment rules - let compiler handle them
  template <class Args>
  Args *Get(int pos)
  {
    return static_cast<ActObjItem<Args> *>(this);
  }
  
  template <bool destruct, class Args>
  void Advance(Args *args, int pos)
  {
    const ActObjItem<Args> *item = static_cast<const ActObjItem<Args> *>(args);
    Assert(item==this);
    delete item;
  }
};

template <class Args>
struct ActObjItem: public ActObjItemHeader, public Args {};


/// object providing asynchronous access to methods of some class
template <class ServantClass, int kickOffEach=1>
class ActiveObject: protected ServantClass
{
  protected:
  typedef ServantClass Servant;
  static const int _kickOffEach = kickOffEach;
  
  SListMT<ActObjItemHeader> _invocationList;
  /// we want to avoid kicking the invocation list after each call, as this may be expensive
  /** instead, when kickOffEach is false, we accumulate the number of invocations and user kicks them by KickOff */
  int _toInvoke;
  
  /// advisory "kicked" even
  //EventSpinningShort _kicked;
  Event _kicked;
  
  public:
  
  ActiveObject();
  /// pump the messages
  template <bool checkKicked>
  bool Update(DWORD timeout=0);
  
  void KickOff()
  {
    _toInvoke = 0;
    _kicked.Set();
  }

  // by default assume any Id may kick off the processing
  bool MayAutoKick(int id) const {return true;}

};


template <class Servant, int kickOffEach>
ActiveObject<Servant,kickOffEach>::ActiveObject()
{
  _toInvoke = 0;
};

/**
typically called from the worker thread
@param checkKicked in some implementations we already checked kicked - no need to check it again
*/
template <class Servant, int kickOffEach>
template <bool checkKicked>
bool ActiveObject<Servant,kickOffEach>::Update(DWORD timeout)
{
  // check if there are any data in the invocation list
  if (!checkKicked || _kicked.TryWait(timeout))
  {
    // some message is there - check it
    // first get message ID
    bool ret = false;
    for(;;)
    {
      ActObjItemHeader *idPtr = _invocationList.Flush();
      if (!idPtr) break;
      // now traverse all flushed items 
      // and reverse the traversal order - this makes sure we respect the order as pushed by each individual thread
      // TODO: it would be faster to use a simpler list here (not MT safe)
      SListMT<ActObjItemHeader> reverse;
      for(;;)
      {
        ActObjItemHeader *next=reinterpret_cast<ActObjItemHeader *>(idPtr->Next);
        reverse.Push(idPtr);
        if (!next) break;
        idPtr = next;
      }
      
      while (ActObjItemHeader *idPtr = reverse.Pop())
      {
        int pos = 0;
        typename Servant::ActObjIds id = (typename Servant::ActObjIds)idPtr->id;
        Servant::ProcessItem<true>(id,*idPtr,pos);
        ret = true;
      }
    }
    return ret;
  }
  return false;
  
}

#define ACTOBJ_WRAP_FUNC(Type,Ret,Name,Args,NumArgs,DefArgs) \
  __forceinline Name##AllArgs::RetType Name Args \
  { \
    /* command contains: command id + arguments (arguments may contain return value) */ \
    ActObjItem<Name##AllArgs> *args = new ActObjItem<Name##AllArgs>; \
    args->id = Servant::AO##Name; \
    Name##AllArgs *buf = args; \
    READARGS##NumArgs; \
    Name##AllArgs::RetType ret = buf->retVal; \
    _invocationList.Push(args); \
    if (MayAutoKick(Servant::AO##Name)) \
    { \
      if (_kickOffEach==1 || _kickOffEach>0 && _toInvoke>=_kickOffEach) \
      { \
        KickOff(); \
      } \
    } \
    LOG_DEBUG_REC(Name,NumArgs); \
    return ret; \
  }


/// used to implement method invocation
#define ACTOBJ_SWITCH_FUNC_PROCESS(Type,Ret,Name,Args,NumArgs,DefArgs) \
  case AO##Name: \
    {  \
      /* now we need each argument to be read from the invocation list */ \
      const Name##AllArgs *args = invocationList.Get<Name##AllArgs>(pos); \
      typedef Ret (Type::*FuncType)(const Name##AllArgs &args); \
      CallFunction<Type,Ret,FuncType,Name##AllArgs> call; \
      call.CallAndSetResult(this,&Type::Name,*args); \
      /* we must not advance the pointer until we are done with the data */ \
      invocationList.Advance<destruct>(args,pos); \
    } \
    break;

/// used to implement method invocation for destruction
#define ACTOBJ_SWITCH_FUNC_DESTRUCT(Type,Ret,Name,Args,NumArgs,DefArgs) \
  case AO##Name: \
    {  \
      /* now we need each argument to be read from the invocation list */ \
      const Name##AllArgs *args = invocationList.Get<Name##AllArgs>(pos); \
      /* we must not advance the pointer until we are done with the data */ \
      invocationList.Advance<true>(args,pos); \
    } \
    break;

/// define a function processing individual messages in the Servant class
#define IS_ACTIVE_OBJECT(Type,XXX) \
  template <bool destruct,class SerialArrayType> \
  __forceinline void ProcessItem(ActObjIds id, SerialArrayType &invocationList, int &pos) \
  { \
    switch (id) \
    { \
      XXX(Type,ACTOBJ_SWITCH_FUNC_PROCESS) \
    } \
  } \
  template <class SerialArrayType> \
  __forceinline void DestructItem(ActObjIds id, SerialArrayType &invocationList, int &pos) \
  { \
    switch (id) \
    { \
      XXX(Type,ACTOBJ_SWITCH_FUNC_DESTRUCT) \
    } \
  }


#define READARGS0 
#define READARGS1            SerialStore(buf->arg1,arg1)
#define READARGS2  READARGS1;SerialStore(buf->arg2,arg2)
#define READARGS3  READARGS2;SerialStore(buf->arg3,arg3)
#define READARGS4  READARGS3;SerialStore(buf->arg4,arg4)
#define READARGS5  READARGS4;SerialStore(buf->arg5,arg5)
#define READARGS6  READARGS5;SerialStore(buf->arg6,arg6)
#define READARGS7  READARGS6;SerialStore(buf->arg7,arg7)
#define READARGS8  READARGS7;SerialStore(buf->arg8,arg8)
#define READARGS9  READARGS8;SerialStore(buf->arg9,arg9)
#define READARGS10 READARGS9;SerialStore(buf->arg10,arg10)

/// trait determining for each input type a string type to convert to, and performing the conversion
template <class Type>
struct ArgumentToPrint
{
  typedef const char *RetType;
  
  static RetType FormatArg(const Type &value);
};

/// creative function for ArgumentToPrint
template <class Type>
ArgumentToPrint<Type> ArgumentToPrintF(const Type &value)
{
  return ArgumentToPrint<Type>();
}

template <class ArgType>
void DebugArg(CharArray<> &log, const ArgType &arg)
{
  // by default assume some conversion from argument to string exists
  log += " ";
  log += ArgumentToPrintF(arg).FormatArg(arg);
}

/***/

#define DEBUGARGS0 
#define DEBUGARGS1             DebugArg(_debug,arg1)
#define DEBUGARGS2  DEBUGARGS1;DebugArg(_debug,arg2)
#define DEBUGARGS3  DEBUGARGS2;DebugArg(_debug,arg3)
#define DEBUGARGS4  DEBUGARGS3;DebugArg(_debug,arg4)
#define DEBUGARGS5  DEBUGARGS4;DebugArg(_debug,arg5)
#define DEBUGARGS6  DEBUGARGS5;DebugArg(_debug,arg6)
#define DEBUGARGS7  DEBUGARGS6;DebugArg(_debug,arg7)
#define DEBUGARGS8  DEBUGARGS7;DebugArg(_debug,arg8)
#define DEBUGARGS9  DEBUGARGS8;DebugArg(_debug,arg9)
#define DEBUGARGS10 DEBUGARGS9;DebugArg(_debug,arg10)

#define PASSARGS0 
#define PASSARGS1 arg1
#define PASSARGS2 PASSARGS1,arg2
#define PASSARGS3 PASSARGS2,arg3
#define PASSARGS4 PASSARGS3,arg4
#define PASSARGS5 PASSARGS4,arg5
#define PASSARGS6 PASSARGS5,arg6
#define PASSARGS7 PASSARGS6,arg7
#define PASSARGS8 PASSARGS7,arg8
#define PASSARGS9 PASSARGS8,arg9
#define PASSARGS10 PASSARGS9,arg10

#if 0 //_DEBUG
# define LOG_DEBUG_REC(Name,NumArgs) AppendDebugText(#Name);DEBUGARGS##NumArgs;AppendDebugText("\n")
#else
# define LOG_DEBUG_REC(Name,NumArgs)
#endif


/// used inside a wrapper around the active object, which allows us to use normal function calls to invoke methods
/**
we are forcing inline here, as what we do here is not much more complicated than normal function parameter passing
and we want to avoid the parameter copying overhead
*/

#define CMDBUF_WRAP_FUNC(Type,Ret,Name,Args,NumArgs,DefArgs) \
  __forceinline Name##AllArgs::RetType Name Args \
  { \
    int writeTo = _invocationList.StartWrite(); \
    writeTo = _invocationList.Put(Servant::AO##Name,writeTo); \
    Name##AllArgs *buf; \
    _invocationList.Set(buf,writeTo); \
    Name##AllArgs::RetType ret = buf->retVal; \
    READARGS##NumArgs; \
    _invocationList.FinishWrite(buf,writeTo); \
    LOG_DEBUG_REC(Name,NumArgs); \
    return ret; \
  }

#endif

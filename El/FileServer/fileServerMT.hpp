#ifdef _MSC_VER
#pragma once
#endif

#ifndef _FILE_SERVER_MT_HPP
#define _FILE_SERVER_MT_HPP

#include "fileServer.hpp"
#include <Es/Memory/fastAlloc.hpp>
#include <Es/Containers/staticArray.hpp>
#include <Es/Containers/boolArray.hpp>

#include <El/QStream/qbStream.hpp>

#include <El/FreeOnDemand/memFreeReq.hpp>

#include <Es/Threads/pocritical.hpp>
#include <Es/Threads/multisync.hpp>

/**
Checking file server memory leaks is possible, but it slows down a bit.
*/
#define CHECK_FS_LEAKS 0

#if CHECK_FS_LEAKS
# define INNER_BEG_NEW "Es/Memory/normalNew.hpp"
# define INNER_END_NEW "Es/Memory/debugNew.hpp"
# define OUTER_BEG_NEW "Es/Memory/debugNew.hpp"
# define OUTER_END_NEW "Es/Memory/debugNew.hpp"
#else
# define INNER_BEG_NEW "Es/Memory/normalNew.hpp"
# define INNER_END_NEW "Es/Memory/normalNew.hpp"
# define OUTER_BEG_NEW "Es/Memory/normalNew.hpp"
# define OUTER_END_NEW "Es/Memory/debugNew.hpp"
#endif

#include OUTER_BEG_NEW

#include <El/Common/perfProf.hpp>

#include <El/FileServer/multithread.hpp>
#include <El/MemoryStore/memStore.hpp>

#if USE_MEM_STORE
  class QIStreamBufferMappedTgt;
  typedef QIStreamBufferMappedTgt TgtBuffer;
  typedef RefR<QIStreamBufferMappedTgt> RefTgtBuffer;
#else
  typedef QIStreamBufferPage TgtBuffer;
  typedef Ref<QIStreamBufferPage> RefTgtBuffer;
#endif

// In this implementation, FileServerHandle is interpreted as ReadHandleInfo *

class FileServerWorkerThread;
class FileRequest;

/// information about position where I/O completed
struct DiskPositionDesc
{
  /** handle may be invalid, but this does not matter - it may only cause
  seek detection to report false negative, but the handle is not really used
  */
  HANDLE _handle;
  QFileSize _offset;

  bool CheckSeek(HANDLE handle, QFileSize offset) const
  {
    return handle!=_handle || offset!=_offset;
  }
  
  bool operator == (const DiskPositionDesc &src) const
  {
    return src._handle==_handle && src._offset==_offset;
  }
  
  DiskPositionDesc():_handle(0),_offset(0)
  {
  }
};


/// overlapped operation interface
class IOverlapped: public RefCount
{
protected:
  FileServerWorkerThread *_owner;

public:
  IOverlapped() {_owner = NULL;}
  virtual ~IOverlapped(){}
  virtual HANDLE GetHandle() const = 0;
  virtual const RString &GetName() const = 0;
  virtual RString GetDebugName() const = 0;
  FileServerWorkerThread *GetOwner() {return _owner;}
  virtual FileRequest *GetFileRequest() const {return NULL;}
  virtual int GetMemNeeded() const {return 0;}

  void SetOwner(FileServerWorkerThread *owner) {_owner = owner;}
  virtual bool IsDone() const = 0;
  virtual bool DependsOn(IOverlapped *req) const {return req == this;}
  /// return false when restart is needed (after some memory released)
  virtual bool Perform(DiskPositionDesc &pos) = 0;
  virtual void OnDone() {}
  // ask the op if it will cause the seek
  /** by default we suppose no op will cause a seek, as we do not want them to wait*/
  virtual bool DetectSeek(const DiskPositionDesc &pos) const {return false;}
  bool Wait(int timeout);
};

/// list of requests waiting to be destroyed by the main thread
extern StoredRefContainer FileServerRefToDestroyMainThread;


typedef StoredRef<RequestableObject,&FileServerRefToDestroyMainThread> StoredRefRequestableObject;

/// contains information needed to call the request done handler
struct RequestableObjectRequestInfo
{
  StoredRefRequestableObject _object;
  SRef<RequestableObject::RequestContext> _context;
};

TypeIsMovableZeroed(RequestableObjectRequestInfo)

//Forward declarations
class FileServerST;
class ReadHandleCache;
struct ReadHandleInfoNormal;
struct ReadHandleInfoDVD;
class OverlappedReadScatter;
class FileServerWorkerThread;

template <>
struct FindArrayKeyTraits< RefR<FileRequest> >
{
  typedef FileRequest *KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  /// get a key from an item
  static KeyType GetKey(const RefR<FileRequest> &a) {return a;}
};

template <>
struct FindArrayKeyTraits< InitPtr<FileRequest> >
{
  typedef FileRequest *KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  /// get a key from an item
  static KeyType GetKey(const InitPtr<FileRequest> &a) {return a;}
};

// opening file can be very slow on XBox
// it is therefore a good idea to keep cache of recently opened files in case they are used again
struct ReadHandleInfo: public TLinkBidirRef, public RequestableObject
{
  RStringI _name;
  QFileSize _size;
  /// how many times this item was opened
  int _open;
  /// how many times this item was opened by cached items
  int _openByCache;
  /// do not discard file when server is stopped (Alt-Tab)
  bool _permanent;
  
  ReadHandleInfo();
  ~ReadHandleInfo();

  virtual void *GetMappingHandle() const = 0;
  virtual void *GetWinHandle() const = 0;
  virtual void GetFileTime(FILETIME *time) const = 0;
  virtual bool Check() const = 0;
  virtual bool Read(FileRequest *req, bool urgent) = 0;
  virtual void CheckRequest(FileRequest *req) = 0;
  virtual void UpdateRequest(FileRequest *req) = 0;
  virtual bool IsCached(DWORD start, DWORD size) = 0;
  virtual Ref<IOverlapped> FindHandle(FileServerST *server, QFileSize from, QFileSize size) = 0;

  #include INNER_BEG_NEW
  USE_FAST_ALLOCATOR
  #include INNER_END_NEW
};

struct ReadHandleInfoNormal : ReadHandleInfo
{
  Ref<IOverlapped> _handle;

  #if USE_FILE_MAPPING
  mutable HANDLE _mapping;
  #endif

  ReadHandleInfoNormal(IOverlapped *handle)
  {
    _handle = handle;
    #if USE_FILE_MAPPING
      _mapping = NULL;
    #endif
  }
  ~ReadHandleInfoNormal();

  virtual void *GetWinHandle() const;
  virtual void *GetMappingHandle() const;
  virtual void GetFileTime(FILETIME *time) const;
  virtual bool Check() const;
  virtual bool Read(FileRequest *req, bool urgent);
  virtual void CheckRequest(FileRequest *req);
  virtual void UpdateRequest(FileRequest *req);
  virtual bool IsCached(DWORD start, DWORD size) {return true;}
  virtual Ref<IOverlapped> FindHandle(FileServerST *server, QFileSize from, QFileSize size) {return _handle;}
  virtual int ProcessingThreadId() const {return -1;}
  virtual void RequestDone(RequestContext *context, RequestResult result) {}

  #include INNER_BEG_NEW
  USE_FAST_ALLOCATOR
  #include INNER_END_NEW
};

//! Merge several following requests together

#if _ENABLE_REPORT && _ENABLE_PERFLOG
#include <El/Common/perfLog.hpp>

class LogFileServerActivity
{
  bool _closed;
  //@{ information about the last file requested
  RString _file;
  int _start;
  int _size;
  //@}
  LogFile _log;

public:
  LogFileServerActivity();
  ~LogFileServerActivity();
  void Flush();
  void DoLog(RString file, int start, int size);
  void Open(const char *name);
  void Close();
};
#endif

class ReadHandleCache
{
  friend class FileServerST;
  
  /// total count of handles in the cache
  int _count;
  /// count handles which are only "cached", nobody is using them (holding them open)
  int _countCached;
  /// count handles which are held by somebody else than file cache
  int _countHeld;
  TListBidirRef<ReadHandleInfo> _cache;

  bool FlushOldestNotHeld();
  
  public:
  ReadHandleCache();

  FileServerHandle Open(FileServerST *server, const char *name, bool onlyIfOpen=false);

#ifdef USE_FILE_MAPPING
  HANDLE Mapping(FileServerHandle file);
#endif
  
  //! increase opened count of given handle
  void Open(FileServerHandle handle, bool byCache);
  //! decrease opened count of given handle
  bool Close(FileServerHandle handle, bool byCache);
  //! read from opened handle
  bool Read(FileRequest *req, bool urgent);

  static RString GetName(FileServerHandle handle);
  static QFileSize GetSize(FileServerHandle handle);
  static void CheckRequest(FileRequest *req);
  static void UpdateRequest(FileRequest *req);

  bool Flush(ReadHandleInfo *info);
  void FlushAll();
  bool CheckIntegrity() const;
  void Clear();
  void Maintain();

protected:
};

/// specialize traits for HeapArray
template <>
struct HeapTraits< RefR<FileRequest> >
{
  typedef RefR<FileRequest> Type;
  // default traits: Type is pointer to actual value
  static bool IsLess(const Type &a, const Type &b);
};

struct RequestableObjectRequestResultInfo: public SLIST_ENTRY, public RequestableObjectRequestInfo
{
  RequestableObject::RequestResult _result;

  RequestableObjectRequestResultInfo(RequestableObject::RequestContext *ctx, RequestableObject *obj, RequestableObject::RequestResult res)
  {
    _context = ctx;
    _object = obj;
    _result = res;
  }

};


//! file server worker thread
class FileServerWorkerThread
{
  friend class FileServerST;
  bool _cleanedUp;

  class ThrRunnable;
  friend class ThrRunnable;
  class ThrRunnable: public MultiThread::ThreadBase
  {
    FileServerWorkerThread *_outer;
  public:
    ThrRunnable(FileServerWorkerThread *outer) 
      : MultiThread::ThreadBase(MultiThread::ThreadBase::PriorityAboveNormal, 16*1024), 
        _outer(outer) 
    {}
    unsigned long Run()
    {
      _outer->DoWork();
      return 0;
    }
  };

protected:
  ThrRunnable _thread;
  /// item can never be destroyed unless already processed
  RefArray<IOverlapped> _queue;
  /// signal each item processed - used when waiting for item processing completion
  Event _itemProcessedEvent;
  /// sometimes we need to be sure we are not waiting for a seek
  MultiThread::SemaphoreBlocker _submittedUrgentOp;
  /// sometimes we get the _submittedUrgentOp request, but we are unable to react immediately (request not there yet)
  int _toProcessUrgentOp;
  /// items which are already processed
  /** worker thread cannot manipulate the queue - it only increases processed count */
  int _queueProcessed;
  /// items which already started processing or are processed
  int _queueInProcess;
  PoCriticalSection _queueLock;
  /// count submitted request
  MultiThread::SemaphoreBlocker _submitSemaphore;
  MultiThread::EventBlocker _terminateEvent;
  MultiThread::EventBlocker _errorEvent;
  MultiThread::EventBlocker _errorEventHandled;
#if !POSIX_FILES_COMMON
  HANDLE _overlapEvent;
#endif

  /// position on the disc where last I/O completed
  DiskPositionDesc _lastPos;
  
  FileServerST *_server;

  //@{ _queueLock access
  void EnterLock()
  {
    PROFILE_SCOPE_DETAIL_EX(fsLck,file);
    _queueLock.Lock();
  }
  void LeaveLock(){_queueLock.Unlock();}
  //@}
public:
  FileServerWorkerThread(FileServerST *server);
  ~FileServerWorkerThread();
  void Prepare();
  void CleanUp();

  int Size() const {return _queue.Size();}

  QFileSize GetReadScatterPageSize() const;

#if !POSIX_FILES_COMMON
  HANDLE GetOverlapEvent() const {return _overlapEvent;}
#endif

  bool CheckIntegrity() const;
  void DoWork();
  static DWORD WINAPI DoWorkCallback(void *context);

  /// submit to the queue edn
  IOverlapped *Submit(IOverlapped *req, bool urgent=false);
  /// make sure it is executed as soon as possible
  void MakeRequestUrgent(FileRequest *req);

  bool VerifyRequestPosition(FileRequest * req);
  /// make sure it is executed as soon as possible
  void MakeItemUrgent(int index);
  
  bool Wait(IOverlapped *op, int timeout);
  bool IsDone(RString name) const;
  bool IsWaiting(IOverlapped *op) const;
  void Wait(RString name);

  void Maintain();
  void Update();
  
  private:
  //@{ helpers for MakeRequestUrgent
  int FindForward(IOverlapped *item, int from, int to);
  void Forward(IOverlapped *item, int moveTo, int moveFrom);
  //@}
};


/// class which can be used to construct QIStreamBuffer
class QIStreamBufferSource
  #if !USE_FILE_MAPPING
    :private NoCopy
  #else
    :public RefCount
  #endif
{
  #if USE_FILE_MAPPING
  // note: all of this information is actually useless
  QFileSize _start;
  QFileSize _length;
  HANDLE _mapping;

  #if _DEBUG
    InitVal<bool,false> _bufferFilled;
  #endif
  mutable Ref<QIStreamBuffer> _buffer;
  
  public:

  QIStreamBufferSource():_mapping(NULL){}
  ~QIStreamBufferSource(){}
  
  QIStreamBufferSource(HANDLE mapping, QFileSize start, QFileSize length)
  :_mapping(mapping),_start(start),_length(length){}
  
//   QIStreamBufferSource(QIStreamBufferSource *src)
//   :_mapping(src->_mapping),_start(src->_start),_length(src->_length)
//   {
//   }

//   void operator = (const QIStreamBufferSource &src)
//   {
//     Assert(!_mapping);
//     _mapping = src._mapping;
//     _start = src._start;
//     _length = src._length;
//     Assert(src._buffer.IsNull());
//   }
//   
  void operator = (QIStreamBufferSource *src)
  {
    Assert(!_mapping);
    Assert(!_buffer);
    _mapping = src->_mapping;
    _start = src->_start;
    _length = src->_length;
    #if _DEBUG
      _bufferFilled = src->_bufferFilled;
    #endif
    //Assert(src->_buffer.IsNull());
  }
  
  void Free();
  Ref<QIStreamBuffer> GetBuffer() const;
  
  void ReadIntoMemory();
  
  size_t GetSize() const {return _length;}
  bool IsNull() const {return _mapping==NULL;}


  void *DataLock(QFileSize pos, QFileSize size)
  {
    // lock does nothing at all - data reading is done in a different way
    return NULL;
  }
  void DataUnlock(QFileSize pos, QFileSize size) {}
  bool IsLocked() const {return true;}

  operator QIStreamBufferSource *() {return this;}
  operator const QIStreamBufferSource *() const {return this;}
  
  QIStreamBufferSource *operator ->() {return this;}
  const QIStreamBufferSource *operator ->() const {return this;}
  
  ClassIsMovableZeroed(QIStreamBufferSource)

  #if 0 // USE_FILE_MAPPING
  // note: all of this information is actually useless
  Ref<FileMapping> _mapping;
  #endif
  
  #elif USE_MEM_STORE
  // store based implementation - memory is allocated only as needed
  /// index of the memory store location
  int _memIndex;
  
  public:
  QIStreamBufferSource():_memIndex(-1){}
  ~QIStreamBufferSource(){Free();}
  
  explicit QIStreamBufferSource(int memIndex):_memIndex(memIndex){}
  
  QIStreamBufferSource(const TgtBuffer *tgt);
  void operator = (const TgtBuffer *tgt);
  
  void Free();
  Ref<QIStreamBuffer> GetBuffer() const;
  
  size_t GetSize() const {return _memIndex>=0 ? GetPageRecommendedSize() : 0;}
  bool IsNull() const {return _memIndex<0;}
  
  #else

  // simple implementation - memory held directly
  Ref<QIStreamBuffer> _ref;
  
  public:
  QIStreamBufferSource(){}
  QIStreamBufferSource(QIStreamBuffer *buf):_ref(buf){}
  void operator = (QIStreamBuffer *buf){_ref = buf;}
  
  Ref<QIStreamBuffer> GetBuffer() const {return _ref.GetRef();}
  
  size_t GetSize() const {return _ref ? _ref->GetSize() : 0;}
  bool IsNull() const {return _ref.IsNull();}
  
  #endif

};

#if USE_FILE_MAPPING

/// buffer mapped directly into the file being read
class QIStreamBufferMappedFromFile: public QIStreamBuffer, private NoCopy
{
  char *_view;
  
  public:
  QIStreamBufferMappedFromFile(HANDLE mapping, QFileSize start, QFileSize length);
  ~QIStreamBufferMappedFromFile();
  
  #include INNER_BEG_NEW
  USE_FAST_ALLOCATOR
  #include INNER_END_NEW
};

#elif USE_MEM_STORE

/// buffer mapped into the memory store
class QIStreamBufferMapped: public QIStreamBuffer, public CountInstances<QIStreamBufferMapped>
{
  /// index of the page in the store
  int _index;
  
  public:
  QIStreamBufferMapped(int page);
  ~QIStreamBufferMapped();

  void OnUnused() const;
  
  #include INNER_BEG_NEW
  USE_FAST_ALLOCATOR
  #include INNER_END_NEW
};

extern MemoryStore GMemStore;

/// buffer mapped into the memory store, used a target for file reads
class QIStreamBufferMappedTgt: public RefCount, private NoCopy
{
  friend class QIStreamBufferSource;
  friend class QIStreamBufferMappedTgtLocked;
  
  protected:
  int _index; /// index in the memory store
  char *_buffer; //! data buffer

  public:
  QIStreamBufferMappedTgt()
  {
    _index = GMemStore.Alloc();
    _buffer = NULL;
  }

  ~QIStreamBufferMappedTgt()
  {
    if (_index>=0)
    {
      GMemStore.Free(_index);
    }
  }

  void *DataLock(QFileSize pos, QFileSize size)
  {
    // only one lock per buffer possible
    Assert(!_buffer);
    _buffer = (char *)GMemStore.MapView(_index,true);
    // only whole buffer locked possible
    Assert(pos==0);
    Assert(GMemStore.GetPageSize()>=size);
    //_bufLen = size;
    return _buffer+pos;
  }
  void DataUnlock(QFileSize pos, QFileSize size)
  {
    Assert(_buffer);
    Assert(pos==0);
    Assert(GMemStore.GetPageSize()>=size);
    GMemStore.UnmapView(_index,_buffer);
    _buffer = 0;
  }
  
  Ref<QIStreamBuffer> GetBuffer() const;
  
  bool IsLocked() const {return _buffer!=NULL;}
  
  __forceinline QFileSize GetSize() const {return _index>=0 ? GMemStore.GetPageSize() : 0;}

  #include INNER_BEG_NEW
  USE_FAST_ALLOCATOR
  #include INNER_END_NEW
};

inline QIStreamBufferSource::QIStreamBufferSource(const TgtBuffer *tgt)
{
  // multiple buffers owning the same allocation at the same time
  _memIndex = tgt->_index;
  GMemStore.ShareAlloc(tgt->_index);
}

inline void QIStreamBufferSource::operator = (const TgtBuffer *tgt)
{
  Assert(_memIndex<0);
  // multiple buffers owning the same allocation at the same time
  _memIndex = tgt->_index;
  GMemStore.ShareAlloc(tgt->_index);
}

#endif

// for ArmA 1 we do not need any thread safety, but we need it for ArmA 2
#define FILE_SERVER_MT_SAFE 1

//! file server
class FileServerST: public FileServer, public QIFileServerFunctions, public QIFileWriteFunctions, public MemoryFreeOnDemandHelper
{
  friend class FileRequest;
  friend class FileRequestLock;
  friend struct ReadHandleInfoDVD;
  
  //! convert name to handle
  ReadHandleCache _handles;
  
  #if FILE_SERVER_MT_SAFE
  mutable CriticalSection _lock;
  
  /// lock access to file cache (used in external functions)
  #define FS_SCOPE_LOCK_EXT(server) ScopeLock<FileServerST> lock(unconst_cast(server))
  /// lock access to file cache (used in members)
  #define FS_SCOPE_LOCK() FS_SCOPE_LOCK_EXT(*this)
  
  public:
  #if !USE_MEM_STORE
    #define TAKE_MEM_CS 1
  #endif
  
  void Lock() const
  {
    #if TAKE_MEM_CS
    // CS hierarchy: always lock memory first
      // memory allocation may call file server as low-level releasing
    GMemFunctions->Lock();
    #endif
    _lock.Lock();
  }
  void Unlock() const
  {
    _lock.Unlock();
    #if TAKE_MEM_CS
    GMemFunctions->Unlock();
    #endif
  }
  
  private:
  #else
  #define FS_SCOPE_LOCK() 
  #define FS_SCOPE_LOCK_EXT(server) 
  
  #endif

  //@{ request throttling and statistics
  //! number of requests that were already passed to the OS
  int _nRequestsLoading;
  //! total _size of requests that were already passed to the OS
  int _sizeRequestsLoading;
  //@}

  /// worker thread
  FileServerWorkerThread _workerThread;

  /// true when FileServer was manually destructed
  bool _serverFinalized;
  
  struct HandlerList
  {
    /// handlers for request that are already finished
    SListMT<RequestableObjectRequestResultInfo> _handlers;
  };

  static const int MaxThreads = 8;

  HandlerList _threadHandlers[MaxThreads];

  //! make sure server is started before using it and it is started only once
  bool _initDone;

  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    //! log any file operations - good for preloading
    LogFileServerActivity _logFiles;
  #endif
  
  //! cached item
public:
  class FileInCache: public RefCount, public LinkBidirWithFrameStore
  {
    public:
    InFileLocation _loc;
    
    /// associated data
    QIStreamBufferSource _buffer;
    /// if the file is not loaded yet, store only the associated request here
    /** note: there may be several request associated with the same cache item*/
    FindArrayKey< RefR<FileRequest> > _request;
    //@{ item cannot be released while there is an active request to it
    int _locked;

    #if DIAG_LOCKS
    FindArrayKey< InitPtr<FileRequest> > _holdLocks;
    #endif

    void Lock(FileRequest *r)
    {
      Assert(_locked>=0);
      _locked++;
      #if DIAG_LOCKS
      _holdLocks.Add(r);
      #endif
    }
    void Unlock(FileRequest *r)
    {
      _locked--;
      Assert(_locked>=0);
      #if DIAG_LOCKS
      Verify(_holdLocks.DeleteKey(r));
      #endif
    }
    //@}
    
    FileInCache():_locked(0){} // empty name for and empty entry
    ~FileInCache();

    /// access the buffer
    PosQIStreamBuffer GetBufferRef() const;
    
    void AttachRequest(FileRequest *req);
      
    size_t GetMemoryControlled() const;

    #include INNER_BEG_NEW
    USE_FAST_ALLOCATOR
    #include INNER_END_NEW
  };

  struct RefFileInCache : public Ref<FileInCache>
  {
    RefFileInCache() {}
    RefFileInCache(FileInCache *source) : Ref<FileInCache>(source) {}
    const FileInCache *GetKey() const {return GetRef();}
  };

  struct MapFileInCacheTraits: public DefMapClassTraits<RefFileInCache>
  {
    typedef const InFileLocation &KeyType;

    //! calculate hash value
    static unsigned int CalculateHashValue(KeyType key)
    {
      return (key._start>>12) + (unsigned int)key._handle;
    }

    //! compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
    static int CmpKey(KeyType k1, KeyType k2)
    {
      int diff = k1._start - k2._start;
      if (diff != 0) return diff;
      diff = ComparePointerAddresses(k1._handle, k2._handle);
      if (diff != 0) return diff;
      diff = k1._size - k2._size;
      return diff;
    }
    
    static KeyType GetKey(const RefFileInCache &item) {return item->_loc;}
  };

  struct FileCache
  {
    FramedMemoryControlledList<
      FileInCache,
      FramedMemoryControlledListTraitsRef<FileInCache>
    > _list;
    typedef MapStringToClass<
      RefFileInCache,
      AutoArray<RefFileInCache>,
      MapFileInCacheTraits
    > CacheTableType;
    CacheTableType _table;
  };
private:

  /// verify the thread making requests is always the same
  mutable int _requestingThread;
  //! page size required for ReadScatter operation
  int _readScatterPageSize;
  //! max. allowed memory usage
  QFileSize _maxSize;

  //! all data that is read and cached
  FileCache _cache;

  //! heap of waiting requests
  HeapArray< RefR<FileRequest> > _queue;
  //! total size of all requests waiting in the queue
  int _queueByteSize;

  protected:
  FileInCache *Find(FileServerHandle handle, QFileSize start, QFileSize size);
  /// store a buffer into the file cache
  bool Store(
    TgtBuffer *buf, FileServerHandle handle, QFileSize start, QFileSize size,
    bool executeHandlers, bool success, bool discardSoon, FileRequest *completedBy
  );
  /// store an empty slot into the file cache
  FileInCache *StoreEmpty(
    FileServerHandle handle, QFileSize start, QFileSize size, bool discardSoon
  );
  
  void MoveToFront(FileInCache *item);
  void DeleteItem(FileInCache *item);
  void Maintain();

  /// finish requested operation - called when transfer is completed
  void RequestDone(FileRequest *handle, int rd, bool executeHandlers=false);
  
  /// request must be repeated due to memory failure or similiar condition
  void RequestRepeat(FileRequest *req);

  void UpdateRequestStored(FileRequest *req, QFileSize start, QFileSize size);

  public:
  FileServerST();
  ~FileServerST();

  void SetMaxSize(size_t maxCacheSize);
  void Clear();
  void Finalize();

  bool CheckOwningThread() const
  {
    return CheckSameThread(_requestingThread);
  }

  /// submit request for operation
  IOverlapped *Submit(IOverlapped *req) {return _workerThread.Submit(req);}

  /// submit request for operation - urgent (place before all other requests)
  /** this call is typically used for blocking request. */
  IOverlapped *SubmitUrgent(IOverlapped *req) {return _workerThread.Submit(req,true);}

  //! preload using given logfile
  void Preload(RString logFile);
  

  /// preload - inform object when done
  /** part of QIFileServerFunctions interface */
	bool Preload(
	  FileRequestCompletionHandle &request, RequestableObject *obj, RequestableObject::RequestContext *ctx,
	  FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
	);

  /// preload - inform object when done
	bool Preload(
	  RequestableObject *obj, RequestableObject::RequestContext *ctx,
	  FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
	)
	{
	  // TODO: remove this implementation, each caller should store the request to prevent creating it again and again
	  FileRequestCompletionHandle dummy;
	  return Preload(dummy,obj,ctx,priority,handle,start,size);
	}

  //! preload into the cache, request handle is owned by the server
  bool Preload(
    FileRequestPriority priority, const char *name, QFileSize start=0, QFileSize size=QFileSizeMax
  );
  //! preload into the cache, request handle is owned by the server
  bool Preload(
    FileRequestPriority priority, FileServerHandle handle, QFileSize start=0, QFileSize size=QFileSizeMax
  );

  
  //! request, store result in cache, caller is sure no part is currently cached
  RefR<FileRequest> RequestUncached(
    FileRequest *req,
    RequestableObject *obj, RequestableObject::RequestContext *ctx,
    FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
  );
  //! request, store result in cache
  FileRequestHandle Request(
    FileRequestPriority priority, const char *name, QFileSize start=0, QFileSize size=QFileSizeMax
  );
  //! request, store result in cache
  FileRequestHandle Request(
    RequestableObject *obj, RequestableObject::RequestContext *ctx,
    FileRequestPriority priority, FileServerHandle handle, QFileSize start=0, QFileSize size=QFileSizeMax
  );

  void ExecuteHandler(bool executeNow, RequestableObject::RequestContext *ctx, RequestableObject *obj, bool success);
  void StoreHandler(int threadName, RequestableObject::RequestContext *ctx, RequestableObject *obj, RequestableObject::RequestResult res) 
  {
    RequestableObjectRequestResultInfo *info = new RequestableObjectRequestResultInfo(ctx,obj,res);
    Assert(threadName+1>=0 && threadName+1<MaxThreads);
    _threadHandlers[threadName+1]._handlers.Push(info);
  }
  virtual bool WaitUntilDone(FileRequestHandle handle, int timeout);
  virtual bool RequestIsDone(FileRequestHandle handle);
  virtual bool RequestIsDoneAsync(FileRequestHandle handle);
  virtual PosQIStreamBuffer RequestResultAsync(FileRequestHandle req, FileServerHandle fileHandle, QFileSize start, QFileSize size);
  virtual InFileLocation RequestLocationAsync(const FileRequestCompletionHandle &req);


  //@{ QIFileServerFunctions
  virtual bool OtherThreadsCanUse() const;
  virtual PosQIStreamBuffer RequestResultAsync(FileRequestCompletionHandle &request, FileServerHandle handle, QFileSize pos);
  //@}

  //! cancel request
  void CancelRequest(FileRequestHandle handle);
  /// add a handler to be called once the request is finished
  void AddHandler(
    FileRequestHandle handle,
    RequestableObject *object,
    RequestableObject::RequestContext *context
  );

  RefR<FileRequest> AddRequest
  (
    FileRequest *req, 
    FileServerHandle fileLogical, FileServerHandle filePhysical,
    FileRequestPriority priority,
    QFileSize fromLogical, QFileSize fromPhysical,
    QFileSize sizeLogical, QFileSize sizePhysical,
    RequestableObject *obj = NULL, RequestableObject::RequestContext *ctx = NULL,
    const PackedBoolAutoArray &store = PackedBoolAutoArray()
  );

  FileRequest *CreateDummyRequest(
    FileServerHandle fileLogical, FileRequestPriority priority, QFileSize fromLogical, QFileSize sizeLogical
  );
   
  void AttachToCache(FileRequest *req, FileServerHandle fileLogical, QFileSize fromLogical, QFileSize sizeLogical);
  void LockRequest(FileRequest *req);
  /// used in pair with AddRequest to clean-up
  void UnlockRequest(FileRequest *req);
  void UnlockRequest(FileRequestHandle req);

  virtual bool CheckIntegrity() const {return _workerThread.CheckIntegrity();}

  //! open file - this can be used as a hint to pre-read it
  void Open(QIFStream &in, const char *name, QFileSize from=0, QFileSize to=QFileSizeMax);

  // say we need the file NOW - load it
  void FlushBank(QFBank *bank);
  void OpenPreloadLog(const char *name); //!< start preload monitoring
  void SavePreloadLog(); //!< called when preload monitoring is finished
  void OpenDVDLog(const char *name); //!< start preload monitoring
  void SaveDVDLog(); //!< called when preload monitoring is finished
  void PreloadDVD(RString name);
  void WaitForRequestsDone();
  void Init();
  void Start();
  void Stop();

  //! make sure pending part is not staying idle if there are any queued requests
  void SubmitQueuedRequests(
    FileRequestPriority maxPriority = FileRequestPriority(INT_MAX),
    int maxCount=INT_MAX, bool submitAll=false
  );
  //! handle any pending request competition
  void CheckPendingRequests();

  SignaledObject &GetUpdateEvent() {return _workerThread._itemProcessedEvent;}
  void SignalRequestCompleted() {_workerThread._itemProcessedEvent.Set();}

  //! submit a particular request for processing
  void SubmitRequest(FileRequest *req, bool urgent=false);
  /// make sure given request is processes as soon as possible
  void MakeRequestUrgent(FileRequest *req);
  //! process request queue
  void ProcessRequests(int maxCount=INT_MAX, bool submitAll=false);

  /// remove any priorities to make sure the seeks are minimized
  void OptimizeQueueForSeek();
  
  /// submit all requests - used for batch preloading
  void SubmitRequestsMinimizeSeek();
  //! main body of file-server - perform regular maintenance
  void Update();
  //! regular maintenance done on main thread
  void UpdateByThread(int threadName);
  //! process handlers which are ready
  void ProcessHandlers(int threadName);
  //! update worker thread
  void UpdateWorkerThread() {_workerThread.Update();}
	void LogWaitingRequests(FileRequestHandle req);

  virtual void WaitForOneRequestDone(int timeout);
  virtual float PreloadQueueStatus() const;
  virtual int PreloadQueueRequests(int *size) const;
  
  // implementation of MemoryFreeOnDemandHelper abstract interface
  virtual size_t FreeOneItem();
  virtual float Priority() const;
  virtual size_t MemoryControlled() const;
  virtual size_t MemoryControlledRecent() const;
	virtual void MemoryControlledFrame();
  virtual RString GetDebugName() const;
  virtual bool UsesVirtualMemory() const;
  
  //@{ FileServer interface
  
  virtual int GetPageSize() {return GetPageRecommendedSize();}
  //@}
  QFileSize GetReadScatterPageSize() const {return _readScatterPageSize;}

  //@{ QIFileServerFunctions interface
  virtual bool FlushReadHandle(const char *name);
  virtual PosQIStreamBuffer Read(FileServerHandle handle, QFileSize start, QFileSize size);
  virtual FileServerHandle OpenReadHandle(const char *name);
  virtual FileWriteHandle OpenWriteHandle(const char *name);
  virtual FileWriteHandle OpenWriteHandleForAppend(const char *name);
  virtual void MakeReadHandlePermanent(FileServerHandle handle);
  virtual void CloseReadHandle(FileServerHandle handle);
  virtual bool CheckReadHandle(FileServerHandle handle) const;
  virtual RString GetHandleName(FileServerHandle handle) const;
  virtual void *GetWinHandle(FileServerHandle handle) const;
  virtual QFileSize GetFileSize(FileServerHandle handle) const;
  virtual QFileTime TimeStamp(FileServerHandle handle) const;
  //@}

  bool FlushReadHandle(FileServerHandle handle);
  
  #if _ENABLE_REPORT
  /// check if we are interested in debugging given handle
  bool InterestedIn(FileServerHandle handle) const;
  /// check if we are interested in debugging given region
  bool InterestedIn(FileServerHandle handle, QFileSize start, QFileSize size) const;
  /// check if we are interested in debugging given file
  bool InterestedIn(const char *name) const;
  #else
  bool InterestedIn(FileServerHandle handle) const {return false;}
  /// check if we are interested in debugging given region
  bool InterestedIn(FileServerHandle handle, QFileSize start, QFileSize size) const {return false;}
  /// check if we are interested in debugging given file
  bool InterestedIn(const char *name) const {return false;}
  #endif

  virtual Future<QFileTime> RequestTimeStamp(const char *name);

#ifdef _WIN32
  virtual HANDLE CreateFileLongPath(
    const char *lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    _SECURITY_ATTRIBUTES *lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    FileServerHandle hTemplateFile
  );
#endif
#if POSIX_FILES_COMMON
  virtual int CreateFileLongPath(const char *lpFileName, int oflag, int pmode);

#endif
};

extern FileServerST SFileServer;

//! file request - waiting in queue to
class FileRequest: public FileReqRef, public CountInstances<FileRequest>
{
  friend class FileServerST;
  friend class ReadHandleCache;
  friend struct ReadHandleInfoNormal;
  friend struct ReadHandleInfoDVD;
  friend class OverlappedReadScatter;
  friend class FileServerWorkerThread; // debugging
  friend class QIFileServerFunctionsDefault;

  //! real source file name
  FileServerHandle _filePhysical;
  //@{ real source file region we are interested in
  QFileSize _fromPhysical;
  QFileSize _sizePhysical;
  //@}

  //! logical file name
  FileServerHandle _fileLogical;
  //@{ logical source file region we are interested in
  QFileSize _fromLogical;
  QFileSize _sizeLogical;
  //@}
  /// how many times _fromLogical,_sizeLogical region was locked
  int _lockCount;
  /// debugging - how many pages were locked
  /** used to verify lock/unlock pairing */
  int _lockPages;

  /// list of pending requests which we depend upon
  /**
  Requests may be removed from here once they complete.
  */
  FindArrayKey< RefR<FileRequest> > _depends;
  
  //@ which part of the logical file was needed by the caller
  QFileSize _neededBeg;
  QFileSize _neededEnd;

  //@}
  
  //! request priority
  FileRequestPriority _priority;
  /// time when the request was submitted
  DWORD _timeStarted;

  /// handlers to be performed when the request is done
  AutoArray<RequestableObjectRequestInfo> _handlers;
  
  //! which server is this request from
  /*!
  This is usually GFileServer, but it might be unsafe to assume it.
  */
  FileServerST *_server;
  //! result will be stored here - only results of the actual operation
  AutoArray<RefTgtBuffer> _target;
  /// how many target buffers are still not done
  int _targetsNotDone;

  /// all data corresponding to the request can be found here once the request is finished (_targetValid)
  RefArray<FileServerST::FileInCache> _resultData;
  
  int GetResultIndex(int offset) const {return (offset-_neededBeg)/_server->GetPageSize();}
  
  //! which pages store to cache
  PackedBoolAutoArray _store;

  //! set true when request has not started yet and is waiting in the queue
  bool _waiting;

  //! _target is filled with correct (decompressed) data
  bool _targetValid;

  //! processed overlapped operation (where Ref to request is stored)
  InitPtr<IOverlapped> _operation;

  public:
  FileRequest(){_targetValid = false;}
  ~FileRequest();

  void CloseHandles();
  
  bool IsMoreUrgent(const FileRequest &req) const;

  void OnDone(QFileSize read, HRESULT result, OverlappedReadScatter *op);

  void Decode();

  bool IsTargetValid() const {return _targetValid;}
  void SetTargetValid() {_targetValid = true;}
  
  bool InNeeded(QFileSize pos, QFileSize size) const {return pos>=_neededBeg && pos+size<=_neededEnd;}
  
  PosQIStreamBuffer GetResultBuffer( QFileSize start, QFileSize size ) 
  {
    Assert (_neededBeg<=start && _neededEnd>=start+size);
    if (_neededBeg<=start && _neededEnd>=start+size)
    {
      int resIndex = GetResultIndex(start);
      FileServerST::FileInCache *item = _resultData[resIndex];
      Assert(item && !item->_buffer.IsNull());
      return item->GetBufferRef();
    }
    return PosQIStreamBuffer();
  }

  private:
  bool operator == (const FileRequest &with) const;
  bool Contains(const FileRequest &what) const;
  bool Contains(FileServerHandle file, QFileSize from, QFileSize size) const;
  bool Overlaps(FileServerHandle file, QFileSize from, QFileSize size) const;

  void AddHandler(
    RequestableObject *object,
    RequestableObject::RequestContext *context
  );
  /// notification called when any of the buffers belonging to the request is done
  void OnBufferCompleted(bool executeHandlers, bool success, FileServerST::FileInCache *item);

  /// check if given handler exists  
  bool FindHandler(RequestableObject * obj, RequestableObject::RequestContext * ctx) const
  {
    for (int i=0; i<_handlers.Size(); i++)
    {
      if (_handlers[i]._object==obj && _handlers[i]._context==ctx)
        return true;
    }
    return false;
  }
  /// called once when all buffers are known to be loaded
  void ExecuteHandlers(bool executeHandlers, bool success);
  
  //! check if given request has finished
  bool IsDone();

  bool CheckResultItem(FileServerST::FileInCache *inCache)
  {
    int resIndex = GetResultIndex(inCache->_loc._start);
    if (resIndex>=0 && resIndex<_resultData.Size())
    {
      return _resultData[resIndex] == inCache;
    }
    return false;
  }
  
  void SetResultItem(FileServerST::FileInCache * inCache) 
  {
    int resIndex = GetResultIndex(inCache->_loc._start);
    Assert(resIndex>=0 && resIndex<_resultData.Size());
    if (resIndex>=0 && resIndex<_resultData.Size())
    {
      Assert(_resultData[resIndex].IsNull());
      _resultData[resIndex] = inCache;
    }
  }

  RString GetDebugName() const;
  
  bool WaitUntilDone(int timeout);

  /// make sure the request completes ASAP
  void MarkAsUrgent();
  
#if DEBUG_PRELOADING
  bool DebugMe() const
  {
    for (int i=0; i<_handlers.Size(); i++)
    {
      if (_handlers[i]._object && _handlers[i]._object->DebugMe()) return true;
    }
    return false;
  }


#endif
  
  #include INNER_BEG_NEW
  USE_FAST_ALLOCATOR
  #include INNER_END_NEW
};

inline void FileServerST::UnlockRequest(FileRequestHandle req)
{
  UnlockRequest(static_cast<FileRequest *>(req.GetRef()));
}

inline bool HeapTraits< RefR<FileRequest> >::IsLess(const Type &a, const Type &b)
{
  return a->IsMoreUrgent(*b);
}


inline bool IOverlapped::Wait(int timeout) {return _owner ? _owner->Wait(this,timeout) : true;}
inline QFileSize FileServerWorkerThread::GetReadScatterPageSize() const {return _server->GetReadScatterPageSize();}

#include OUTER_END_NEW

#endif

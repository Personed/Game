#include <El/elementpch.hpp>
#include "fileServer.hpp"
#ifdef _WIN32
  #include <Es/Common/win.h>
#endif
#include "fileServerMT.hpp"
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <El/Debugging/debugTrap.hpp>
#include <El/Multicore/multicore.hpp>
#include <Es/Framework/appFrame.hpp>
#include <El/FileServer/fileServerAsync.hpp>

#if POSIX_FILES_COMMON
#ifdef _WIN32
#include <direct.h>
#endif
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#define ERROR_SUCCESS 0L
#endif

#define DISABLE_BUFFERING 1

#define LOG_QUEUE 0 // 0..40
#define OPEN_HANDLE_DIAGS 0
#define SHOW_WRITE_STATS 0

#ifdef _WIN32
 #define ASM_NOP __asm nop
#else
 #define ASM_NOP
#endif

#if (MFC_NEW && !POSIX_FILES_COMMON) || !PAGE_ALIGNMENT_POSSIBLE
const bool UseReadScatter = false;
#else
const bool UseReadScatter = true;
#endif

/**
@param when true, the function terminates the program execution
*/

#ifndef _SUPER_RELEASE
bool GLogFileServer;
bool GLogDVD;

//////////////////////////////////////////////////////////////////////////
// 
// log DVD activity
// 
#if _ENABLE_PERFLOG
struct LogDVDActivityItem
{
  RString _file;
  int _start;
  int _size;
  PackedBoolAutoArray _used;

  LogDVDActivityItem(){_start=0;_size=0;}
  LogDVDActivityItem(RString file, int start, int size);
  void UpdateRequest(int start, int size);
};
TypeIsMovableZeroed(LogDVDActivityItem)

//! log DVD file operations - good for preloading
class LogDVDActivity
{
protected:
  RString _name;
  AutoArray<LogDVDActivityItem> _prepared;
  AutoArray<LogDVDActivityItem> _done;

public:
  void Open(const char *name);
  void Close();

  void AddRequest(RString file, int totalStart, int totalSize, int start, int size);
  void UpdateRequest(RString file, int totalStart, int totalSize, int start, int size);
  void UpdateRequests(RString file, int start, int size);
  void DoneRequest(RString file, int totalStart, int totalSize);

protected:
  LogDVDActivityItem *FindRequest(RString file, int totalStart, int totalSize);
  int FindPreparedRequest(RString file, int totalStart, int totalSize);
  int FindDoneRequest(RString file, int totalStart, int totalSize);
};

static LogDVDActivity LogDVD;

LogDVDActivityItem::LogDVDActivityItem(RString file, int start, int size)
{
  _file = file;
  _start = start;
  _size = size;

  QFileSize bufSize = GetPageRecommendedSize();
  int totalStart = start / bufSize;
  int totalEnd = (start + size + bufSize - 1) / bufSize;
  _used.Init(totalEnd - totalStart);
  memset(_used.RawData(), 0, _used.RawSize() * sizeof(int));
}

void LogDVDActivityItem::UpdateRequest(int start, int size)
{
  QFileSize bufSize = GetPageRecommendedSize();
  int totalStart = _start / bufSize;
  int storeStart = start / bufSize;
  int storeEnd = (start + size + bufSize - 1) / bufSize;
  for (int i=storeStart-totalStart; i<storeEnd-totalStart; i++)
    _used.Set(i, true);
}

void LogDVDActivity::Open(const char *name)
{
  _name = name;
  _prepared.Clear();
  _done.Clear();
}

void LogDVDActivity::Close()
{
  DoAssert(_prepared.Size() == 0);

  LogFile file;
  file.Open(_name);
  for (int i=0; i<_done.Size(); i++)
  {
    const LogDVDActivityItem &item = _done[i];
    file.PrintF("%s:%x:%x:",(const char *)item._file, item._start, item._size);
    for (int j=0; j<item._used.RawSize(); j++)
      file.PrintF("%08x", item._used.RawData()[j]);
    file.PrintF("\n");
  }

  file.Close();

  _name = RString();
}

void LogDVDActivity::AddRequest(RString file, int totalStart, int totalSize, int start, int size)
{
  if (_name.GetLength() == 0) return;

  LogDVDActivityItem *item = FindRequest(file, totalStart, totalSize);
  if (!item)
  {
    int index = _prepared.Add(LogDVDActivityItem(file, totalStart, totalSize));
    item = &_prepared[index];
  }
    
  item->UpdateRequest(start, size);
}

void LogDVDActivity::UpdateRequest(RString file, int totalStart, int totalSize, int start, int size)
{
  if (_name.GetLength() == 0) return;

  LogDVDActivityItem *item = FindRequest(file, totalStart, totalSize);
  if (item) item->UpdateRequest(start, size);
}

void LogDVDActivity::UpdateRequests(RString file, int start, int size)
{
  if (_name.GetLength() == 0) return;

  for (int i=0; i<_prepared.Size(); i++)
  {
    if
    (
      start < _prepared[i]._start + _prepared[i]._size &&
      _prepared[i]._start < start + size &&
      stricmp(_prepared[i]._file, file) == 0
    )
    {
      int s = std::max(start, _prepared[i]._start);
      int e = std::min(start + size, _prepared[i]._start + _prepared[i]._size);
      _prepared[i].UpdateRequest(s, e - s);
    }
  }
  for (int i=0; i<_done.Size(); i++)
  {
    if
    (
      start < _done[i]._start + _done[i]._size &&
      _done[i]._start < start + size &&
      stricmp(_done[i]._file, file) == 0
    )
    {
      int s = std::max(start, _done[i]._start);
      int e = std::min(start + size, _done[i]._start + _done[i]._size);
      _done[i].UpdateRequest(s, e - s);
    }
  }
}

void LogDVDActivity::DoneRequest(RString file, int totalStart, int totalSize)
{
  if (_name.GetLength() == 0) return;

  int index = FindPreparedRequest(file, totalStart, totalSize);
  if (index >= 0)
  {
    _done.Add(_prepared[index]);
    _prepared.Delete(index);
  }
  else
  {
    // duplicity?
    if (FindDoneRequest(file, totalStart, totalSize) < 0)
    {
      Fail("Done request not found");
    }
  }
}

LogDVDActivityItem *LogDVDActivity::FindRequest(RString file, int totalStart, int totalSize)
{
  int index = FindPreparedRequest(file, totalStart, totalSize);
  if (index >= 0) return &_prepared[index];

  index = FindDoneRequest(file, totalStart, totalSize);
  if (index >= 0) return &_done[index];

  return NULL;
}

int LogDVDActivity::FindPreparedRequest(RString file, int totalStart, int totalSize)
{
  for (int i=0; i<_prepared.Size(); i++)
  {
    if
    (
      _prepared[i]._start == totalStart &&
      _prepared[i]._size == totalSize &&
      stricmp(_prepared[i]._file, file) == 0
    ) return i;
  }
  return -1;
}

int LogDVDActivity::FindDoneRequest(RString file, int totalStart, int totalSize)
{
  for (int i=0; i<_done.Size(); i++)
  {
    if
    (
      _done[i]._start == totalStart &&
      _done[i]._size == totalSize &&
      stricmp(_done[i]._file, file) == 0
    ) return i;
  }
  return -1;
}
#endif

#endif

#if !POSIX_FILES_COMMON
inline DWORD CreateFileAttributes()
{
  DWORD attr = FILE_ATTRIBUTE_NORMAL;
  //if (UseReadScatter)
  {
    #if USE_FILE_MAPPING
      // attr |= FILE_FLAG_NO_BUFFERING;
    #else
    #if DISABLE_BUFFERING
      attr |= FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING;
    #else
      attr |= FILE_FLAG_OVERLAPPED;
    #endif
    #endif
  }
  return attr;
}
#endif

//////////////////////////////////////////////////////////////////////////
// 
// Worker thread
// 

#define _LOG_OVERLAPPED_OPS 0
#ifdef _LOG_OVERLAPPED_OPS
static DWORD totalOpsTime = 0;
#endif

class OverlappedOpen: public IOverlapped
{
protected:
  RString _name;

#if POSIX_FILES_COMMON
  int _oflag;
  int _pmode;
  int _handle;
  int _error;
#else
  HANDLE _handle;
  DWORD _access;
  DWORD _share;
  DWORD _creation;
  DWORD _attr;
  HRESULT _error;
#endif
  bool _done;

public:
#if POSIX_FILES_COMMON
  OverlappedOpen(RString name, int oflag, int pmode) //POSIX
    : _name(name), _oflag(oflag), _pmode(pmode)
  {
    _error = 0; //zero is no error
    _handle = 0;
    _done = false;
  }
#else
  OverlappedOpen(RString name, DWORD access, DWORD share, DWORD creation, DWORD attr)
    :_name(name), _access(access), _share(share), _creation(creation), _attr(attr)
  {
    _handle = 0;
    _error = S_OK;
    _done = false;
  }
#endif
  HANDLE GetHandle() const
  {
    Assert(_handle != 0);
    return (HANDLE)(_handle);
  }
  const RString &GetName() const {return _name;}
  RString GetDebugName() const {return RString("open ")+_name;}
  bool IsDone() const {return _done;}
  bool Perform(DiskPositionDesc &pos);
  void OnDone();
};

class OverlappedGetFileTime: public IOverlapped
{
protected:
  RString _name;
  FILETIME _info;
  bool _done;
  /// sometimes external subscriber is interested about the result
  Future<QFileTime> _result;

public:
  OverlappedGetFileTime(RString name, Future<QFileTime> result)
    :_name(name),_result(result)
  {
    _done = false;
  }
  HANDLE GetHandle() const {return 0;}
  const RString &GetName() const {return _name;}
  RString GetDebugName() const {return RString("time ")+_name;}
  bool IsDone() const {return _done;}
  void GetFileTime(FILETIME &info) const {info = _info;}
  bool Perform(DiskPositionDesc &pos);
};

#if POSIX_FILES_COMMON && !defined(_WIN32)
typedef char PO_FILE_SEGMENT_ELEMENT;
#else
typedef FILE_SEGMENT_ELEMENT PO_FILE_SEGMENT_ELEMENT;
#endif

class OverlappedReadScatter: public IOverlapped
{
protected:
  Ref<IOverlapped> _source;
  RefR<FileRequest> _req;

  QFileSize _pageSize;
  QFileSize _size;
  #if USE_FILE_MAPPING
  AutoArray<RefTgtBuffer> _target;
  #else
  AutoArray<PO_FILE_SEGMENT_ELEMENT> _fileSegs;
  #endif
#if !POSIX_FILES_COMMON
  HRESULT _result;
#else
  int _result;
#endif

  QFileSize _read;
  bool _done;

public:
  OverlappedReadScatter(IOverlapped *source, FileRequest *req);
  ~OverlappedReadScatter();
  HANDLE GetHandle() const
  {
    Assert(_done);
    return _source->GetHandle();
  }
  bool DependsOn(IOverlapped *req) const {return req == this || _source && _source->DependsOn(req);}
  const RString &GetName() const {return _source->GetName();}
  RString GetDebugName() const {return _req->GetDebugName();}
  FileRequest *GetFileRequest() const {return _req;}
  int GetMemNeeded() const;
  bool IsDone() const {return _done;}
  bool Perform(DiskPositionDesc &pos);

  void ReportSegMemory();
  
#ifdef _WIN32
  BOOL EmulateReadFileScatter(HANDLE handle, OVERLAPPED &overlap);
#endif
  bool DetectSeek(const DiskPositionDesc &pos) const 
  {
    return pos.CheckSeek(_source->GetHandle(),_req->_fromPhysical);
  }
  void OnDone();
  void Reset(); // create new buffers

protected:
  void Destroy();
#if POSIX_FILES_COMMON
  BOOL ReadFileScatterSync();
#endif
};

#ifdef _WIN32
class OverlappedWriteGather: public IOverlapped
{
protected:
  Ref<IOverlapped> _source;
  QFileSize _offset;
  QFileSize _size;
  AutoArray<RefTgtBuffer> _target;
  bool _done;
  bool _ok;

  QFileSize _pageSize;
  QFileSize _totalSize;
  AutoArray<PO_FILE_SEGMENT_ELEMENT> _fileSegs;

public:
  OverlappedWriteGather(
    IOverlapped *source, QFileSize offset, QFileSize size, AutoArray<RefTgtBuffer> &target, QFileSize pageSize
  );
  ~OverlappedWriteGather();
  HANDLE GetHandle() const
  {
    Assert(_done);
    return _source->GetHandle();
  }
  bool DependsOn(IOverlapped *req) const {return req == this || _source && _source->DependsOn(req);}
  const RString &GetName() const {return _source->GetName();}
  RString GetDebugName() const {return RString("write ")+_source->GetName();}
  int GetMemNeeded() const;
  bool IsDone() const {return _done;}
  bool Perform(DiskPositionDesc &pos);
  bool DetectSeek(const DiskPositionDesc &pos) const 
  {
    return pos.CheckSeek(_source->GetHandle(),_offset);
  }
protected:
  BOOL WriteFileGatherSync();
};

#endif

#if POSIX_FILES_COMMON
#ifdef _WIN32
#include <errno.h>
#else
#include <sys/errno.h>
#endif
bool OverlappedOpen::Perform(DiskPositionDesc &pos)
{
  _handle = GFileServer->CreateFileLongPath(_name, _oflag,_pmode);
  if (_handle==-1)
  {
    // detect what kind of error it is
    switch (errno)
    {
    case ENOENT: 
      _error = ERROR_FILE_NOT_FOUND;
      break;
    default:
      _error = -1; //TODO
    }
  }
  _done = true;
  pos = DiskPositionDesc();
  return true;
}
#else
bool OverlappedOpen::Perform(DiskPositionDesc &pos)
{
#if _LOG_OVERLAPPED_OPS
  DWORD start = ::GlobalTickCount();
#endif
  _handle = GFileServer->CreateFileLongPath
    (
    _name, _access, _share, NULL, _creation, _attr, NULL
    );
  if (_handle==INVALID_HANDLE_VALUE)
  {
    // detect what kind of error it is
    _error = ::GetLastError();
  }
#if _LOG_OVERLAPPED_OPS
  DWORD end = ::GlobalTickCount();
  totalOpsTime += end - start;
  LogF("### Opening %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
#endif
  _done = true;
  pos = DiskPositionDesc();
  return true;
}
#endif

/*!
\patch 5111 Date 12/22/2006 by Ondra
- Fixed: Failure opening custom files (face, squad logo) might be causing server termination.
*/
void OverlappedOpen::OnDone()
{
  // some failures are natural and cannot mean a device failure
  if (
    (HANDLE)(_handle)==INVALID_HANDLE_VALUE &&
    _error!=ERROR_FILE_NOT_FOUND && _error!=ERROR_PATH_NOT_FOUND &&
    _error!=ERROR_INVALID_NAME
  )
  {
    // PC: no error reporting when opening file fails - we expect the caller will handle it somehow
    // we want the error to be reported in the rpt/log file
    // but not to terminate the program
#ifdef _WIN32
    if (FileServer::GFileServerFunctions)
      FileServer::GFileServerFunctions->ReportFileReadError(_error,_name,false);
#endif
  }
}

bool OverlappedGetFileTime::Perform(DiskPositionDesc &pos)
{
#if POSIX_FILES_COMMON
  int handle = GFileServer->CreateFileLongPath(_name, O_RDONLY, S_IREAD);

  if (handle != (int)INVALID_HANDLE_VALUE)
  {
    struct stat st;
    fstat((int)handle,&st);
    ::close((int)handle);
    *(reinterpret_cast<time_t *>(&_info)) = st.st_mtime;
    _result.SetResult(st.st_mtime);
  }
  else
  {
    _result.SetResult(0);
  }
#else
  HANDLE handle = GFileServer->CreateFileLongPath(_name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  
  if (handle != INVALID_HANDLE_VALUE)
  {
    ::GetFileTime(handle, NULL, NULL, &_info);
    _result.SetResult( ConvertToQFileTime(_info) );
    ::CloseHandle(handle);
  }
  else
  {
    _result.SetResult(0);
  }

#endif
  _done = true;
  pos = DiskPositionDesc();
  return true;
}

static QFileSize CreateFileSegments(AutoArray<PO_FILE_SEGMENT_ELEMENT> &fileSegs, AutoArray<RefTgtBuffer> &buffer, QFileSize from, QFileSize reqSize, QFileSize pageSize)
{
#if !POSIX_FILES_COMMON
  QFileSize bufSize = GetPageRecommendedSize();
  int nPagesInBuf = bufSize / pageSize;

  // make sure all data is zeroed
  int countSegs = (reqSize + pageSize - 1) / pageSize;
  fileSegs.Realloc(countSegs + 1);
  fileSegs.Resize(countSegs + 1);

  QFileSize totalSize = 0;
  int batchSize = 0;

  // one request may contain multiple buffers
  int targetCount = 0;
  while (reqSize > 0)
  {
    QFileSize segTgtSize = std::min(reqSize, bufSize);
    int segOffset = 0;
    for (int i=0; i<nPagesInBuf && segTgtSize > 0; i++)
    {
      QFileSize oneSegSize = segTgtSize;
      if (oneSegSize > pageSize) oneSegSize = pageSize;
      fileSegs[batchSize].Alignment = NULL;
      fileSegs[batchSize].Buffer = PtrToPtr64(buffer[targetCount]->DataLock(segOffset, oneSegSize));
      batchSize++;

      segOffset += oneSegSize;
      segTgtSize -= oneSegSize;
      reqSize -= oneSegSize;

      totalSize += pageSize;
      from += oneSegSize;
    }
    targetCount++;
  }
  //Assert((countSegs-1)*pageSize == (targetCount-1)*nPagesInBuf);
  Assert(totalSize == batchSize * pageSize);

  fileSegs[batchSize].Alignment = NULL;
  fileSegs[batchSize].Buffer = NULL;

  return totalSize;
#else //POSIX_FILES_COMMON
  int countPages = (reqSize + pageSize - 1) / pageSize;
  return (pageSize * countPages);
#endif
}

static void DeleteFileSegments(AutoArray<PO_FILE_SEGMENT_ELEMENT> &fileSegs, AutoArray<RefTgtBuffer> &source, QFileSize pageSize)
{
  int countSegs = fileSegs.Size() - 1;
  fileSegs.Clear();

  // unlock data
  int j = 0;
  for (int i=0; i<source.Size(); i++)
  {
    TgtBuffer *buffer = source[i];
    QFileSize bufferSize = buffer->GetSize();
    QFileSize offset = 0;
    while (bufferSize > 0 && j < countSegs)
    {
      // normally buffers are unlocked when storing them
      #if USE_MEM_STORE // with non-store buffers Unlock is no-op - we do not care
      if (buffer->IsLocked())
      #endif
      {
        buffer->DataUnlock(offset, pageSize);
      }
      j++;
      offset += pageSize;
      bufferSize -= pageSize;
    }
  }
  
  // removed - for compressed files countSegs can change
  // DoAssert(j == countSegs);
}

OverlappedReadScatter::OverlappedReadScatter(IOverlapped *source, FileRequest *req)
: _source(source), _req(req), _done(false), _read(0)
{
  _result = ERROR_SUCCESS;
  _pageSize = req->_server->GetReadScatterPageSize();
  #if USE_FILE_MAPPING
  _target = req->_target;
  _size = req->_sizePhysical;
  #else
  _size = CreateFileSegments(_fileSegs, req->_target, req->_fromPhysical, req->_sizePhysical, _pageSize);
  #endif
}

OverlappedReadScatter::~OverlappedReadScatter()
{
  Destroy();
}

void OverlappedReadScatter::Reset()
{
  Assert(_req);
  #if !USE_FILE_MAPPING
  DeleteFileSegments(_fileSegs, _req->_target, _pageSize);
  _size = CreateFileSegments(_fileSegs, _req->_target, _req->_fromPhysical, _req->_sizePhysical, _pageSize);
  #endif
  _done = false;
  _read = 0;
  _result = ERROR_SUCCESS;
  ReadHandleInfo *info = (ReadHandleInfo *)_req->_filePhysical;
  _source = info->FindHandle(_req->_server, _req->_fromPhysical, _req->_sizePhysical);
}

void OverlappedReadScatter::Destroy()
{
  if (_req)
  {
    #if !USE_FILE_MAPPING
    DeleteFileSegments(_fileSegs, _req->_target, _pageSize);
    #endif
    // for sure
    if (_req->_operation == this) _req->_operation = NULL;
  }
}

int OverlappedReadScatter::GetMemNeeded() const
{
  return GetPageRecommendedSize();
}


#if POSIX_FILES_COMMON
BOOL OverlappedReadScatter::ReadFileScatterSync()
{
  HANDLE handle = _source->GetHandle();
  int intHandle = (int)(handle);
  QFileSize fromPhysical = _req->_fromPhysical;
  AutoArray<Ref<QIStreamBufferPage> > &targets = _req->_target;
  QFileSize sizeLeft = _req->_sizePhysical;

  QFileSize pageSize = _req->_server->GetReadScatterPageSize();
  QFileSize bufSize = GetPageRecommendedSize();
  int nPagesInBuf = bufSize / pageSize;
  QFileSize bufAvailableLen = nPagesInBuf * pageSize;
  //if (bufAvailableLen>sizeLeft) //this condition is often false, for POSIX files, there are no _fileSegs used, but req->_target is read directly
  //Is the file possition set properly?
  if ( fromPhysical != ::lseek(intHandle, 0, SEEK_CUR) )  
  {
    ::lseek(intHandle, fromPhysical, SEEK_SET);
#if !LOG_POSIX_FILES_COMMON
  }
#else
    LogF("Warning: seek possition different from fromPhysical, file %s", cc_cast(_source->GetName()) );
  }
  else LogF("Seek possition %u (file: %s)", fromPhysical, cc_cast(_source->GetName()));
#endif
  for ( 
    int i=0; 
    sizeLeft>0; 
    i++, ((sizeLeft>bufAvailableLen) ? (sizeLeft-=bufAvailableLen) : (sizeLeft=0)) 
  )
  {
    ::read(intHandle, targets[i]->DataLock(0, bufAvailableLen), bufAvailableLen);
    //Assert(rlen==bufAvailableLen);
  }
  return TRUE;
}

bool OverlappedReadScatter::Perform(DiskPositionDesc &pos)
{
#if _LOG_OVERLAPPED_OPS
  DWORD start = ::GlobalTickCount();
#endif
#if !DISABLE_BUFFERING
  SetLastError(ERROR_HANDLE_EOF);
  Fail("No ReadFileScatter with buffering");
  return false;
#endif

  Assert(_owner);

  BOOL ok = ReadFileScatterSync();

  _read = _req->_sizePhysical;
  pos._handle = _source->GetHandle();
  pos._offset = _req->_fromPhysical+_read;

#if _LOG_OVERLAPPED_OPS
  DWORD end = ::GlobalTickCount();
  totalOpsTime += end - start;
  LogF("### Async_reading %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
#endif

  if (ok)
  {
    _done = true;
    return true;
  }
  _result = 1;  //TODO
  _done = true;
  return true;
}

void OverlappedReadScatter::ReportSegMemory()
{
}

#else

void OverlappedReadScatter::ReportSegMemory()
{
  #if !USE_FILE_MAPPING
  for (int i=0; i<_fileSegs.Size()-1; i++) // the last item is the stop (zero)
  {
    PO_FILE_SEGMENT_ELEMENT &seg = _fileSegs[i];
    MEMORY_BASIC_INFORMATION info;
    SIZE_T retSize = VirtualQuery(seg.Buffer,&info,sizeof(info));
    if (retSize==sizeof(info))
    {
      BString<128> state;
      BString<128> protect;
      BString<128> type;
      if (info.State&MEM_FREE) state += " FREE";
      if (info.State&MEM_RESERVE) state += " RESERVE";
      if (info.State&MEM_COMMIT)
      {
        state += " COMMIT";
        if (info.Type&MEM_IMAGE) type += " IMAGE";
        if (info.Type&MEM_MAPPED) type += " MAPPED";
        if (info.Type&MEM_PRIVATE) type += " PRIVATE";
        if (info.Protect&PAGE_NOACCESS) protect += " NOACCESS";
        if (info.Protect&PAGE_READONLY) protect += " READONLY";
        if (info.Protect&PAGE_READWRITE) protect += " READWRITE";
        if (info.Protect&PAGE_WRITECOPY) protect += " WRITECOPY";
        if (info.Protect&PAGE_EXECUTE) protect += " EXECUTE";
        if (info.Protect&PAGE_EXECUTE_READ) protect += " EXECUTE_READ";
        if (info.Protect&PAGE_EXECUTE_READWRITE) protect += " EXECUTE_READWRITE";
        if (info.Protect&PAGE_EXECUTE_WRITECOPY) protect += " EXECUTE_WRITECOPY";
        
        if (info.Protect&PAGE_GUARD) protect += " GUARD";
        if (info.Protect&PAGE_NOCACHE) protect += " NOCACHE";
        if (info.Protect&PAGE_WRITECOMBINE) protect += " WRITECOMBINE";
      }
      RptF(
        " at %llp: base %p, size %5x, state%s, protect%s, type%s",
        seg.Buffer,info.BaseAddress,info.RegionSize,cc_cast(state),cc_cast(protect),cc_cast(type)
      );
    }
    else
    {
      RptF(
        " at %p: VirtualQuery failed, error %x",seg.Buffer,GetLastError()
      );
    }
  }
  #endif
}

BOOL OverlappedReadScatter::EmulateReadFileScatter(HANDLE handle, OVERLAPPED &overlap)
{
  #if !USE_FILE_MAPPING
  BOOL ok = FALSE;
  QFileSize offset = _req->_fromPhysical;

  // try normal overlapped read
  for (int i=0; i<_fileSegs.Size()-1; i++) // the last item is the stop (zero)
  {
    ok = ::ReadFile(handle, _fileSegs[i].Buffer, _pageSize, NULL, &overlap);
    if (!ok)
    {
      HRESULT hr = GetLastError();
      if (hr != ERROR_IO_PENDING)
      {
        RptF("  ReadFile error %x, offset %x, address %llp",hr,offset,_fileSegs[i].Buffer);
        break; // read error
      }

      // wait until the operation proceed
      ::WaitForSingleObject(overlap.hEvent, INFINITE);
      DWORD rd = 0;
      ok = GetOverlappedResult(handle, &overlap, &rd, FALSE);
      if (!ok) break; // read error

      // prepare the overlap structure to the next operation
      offset += _pageSize; 
      overlap.Offset = offset;
      overlap.OffsetHigh = 0;
      ::ResetEvent(overlap.hEvent);
      // we have waited for the event, therefore there is little need to reset
      // however, as we always do it before starting read operation, we do it here as well
    }
  }
  return ok;
#else
  return FALSE;
#endif
}

bool OverlappedReadScatter::Perform(DiskPositionDesc &pos)
{
#if USE_FILE_MAPPING
  // reading the file means accessing its memory
  for (int i=0; i<_target.Size(); i++)
  {
    _target[i]->ReadIntoMemory();
  }

  _result = S_OK;
  _read = _size;
  _done = true;
  
  return true;
#else

#if _LOG_OVERLAPPED_OPS
  DWORD start = ::GlobalTickCount();
#endif

#if !DISABLE_BUFFERING
  SetLastError(ERROR_HANDLE_EOF);
  Fail("No ReadFileScatter with buffering");
  return false;
#endif
  
  Assert(_owner);

  HANDLE handle = _source->GetHandle();

  OVERLAPPED overlap;
  overlap.Offset = _req->_fromPhysical;
  overlap.OffsetHigh = 0;
  overlap.hEvent = _owner->GetOverlapEvent();

#if LOG_QUEUE>10
  LogF
  (
    "Queue: ReadFileScatter %s:%x:%x",
    cc_cast(GetName()), _req->_fromPhysical, _size
  );
#elif LOG_QUEUE>0
  // report only requests which will cause seeking (different file or offset)
  static HANDLE lastHandle = 0;
  static QFileSize lastOffset = 0;
  if (lastHandle!=handle)
  {
    LogF(
      "ReadFileScatter file seek %s:%x:%x (p %d)",
      cc_cast(GetName()), _req->_fromPhysical, _size, _req->_priority
    );
  }
  else if (lastOffset!=_req->_fromPhysical)
  {
    LogF(
      "ReadFileScatter seek %s:%x:%x  (<-%x) (p %d)",
      cc_cast(GetName()), _req->_fromPhysical, _size, lastOffset, _req->_priority
    );
  }
  lastHandle = handle;
  lastOffset = _req->_fromPhysical+_size;
#endif

  #if 0 // _PROFILE || _DEBUG
  ReportSegMemory();
  #endif
  
  ::ResetEvent(overlap.hEvent);
  BOOL ok = ::ReadFileScatter(handle, _fileSegs.Data(), _size, NULL, &overlap);
  _read = _req->_sizePhysical;

  pos._handle = handle;
  pos._offset = _req->_fromPhysical+_read;

  if (ok)
  {
    _done = true;
#if _LOG_OVERLAPPED_OPS
    DWORD end = ::GlobalTickCount();
    totalOpsTime += end - start;
    LogF("### Async_reading %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
/*
    RString name = ((ReadHandleInfo *)_req->_filePhysical)->_name;
    LogF("    Physical %s:%x:%x", (const char *)name, _req->_fromPhysical, _size);
    name = ((ReadHandleInfo *)_req->_fileLogical)->_name;
    LogF("    Logical %s:%x:????", (const char *)name, _req->_fromLogical);
*/
#endif
    return true;
  }
  HRESULT hr = GetLastError();

  if (hr == ERROR_IO_PENDING)
  {
    ::WaitForSingleObject(overlap.hEvent, INFINITE);
    DWORD rd = 0;
    ok = GetOverlappedResult(handle, &overlap, &rd, FALSE);
    _read = rd;
    if (_read > _req->_sizePhysical) _read = _req->_sizePhysical;
    if (ok)
    {
      _done = true;
#if _LOG_OVERLAPPED_OPS
      DWORD end = ::GlobalTickCount();
      totalOpsTime += end - start;
      LogF("### Async_reading %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
/*
      RString name = ((ReadHandleInfo *)_req->_filePhysical)->_name;
      LogF("    Physical %s:%x:%x", (const char *)name, _req->_fromPhysical, _size);
      name = ((ReadHandleInfo *)_req->_fileLogical)->_name;
      LogF("    Logical %s:%x:????", (const char *)name, _req->_fromLogical);
*/
#endif
      return true;
    }
    hr = GetLastError();
  }

    
#if _LOG_OVERLAPPED_OPS
  DWORD end = ::GlobalTickCount();
  totalOpsTime += end - start;
  LogF("### Async_reading (err.) %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
#endif

  // error processing
  switch (hr)
  {
  case ERROR_NO_SYSTEM_RESOURCES:
  case ERROR_NONPAGED_SYSTEM_RESOURCES:
  case ERROR_PAGED_SYSTEM_RESOURCES:
  case ERROR_NOT_ENOUGH_MEMORY:
  case ERROR_OUTOFMEMORY:
  case E_OUTOFMEMORY:
    LogF("Out of memory when reading %s - %x",(const char *)GetName(), hr);
    // try to release some memory and restart request
    _done = false;
    return false;
  case ERROR_HANDLE_EOF:
    LogF("EOF during read");
    _done = true;
    return true;
  case ERROR_NOACCESS:
    RptF("File read error: %s,ERROR_NOACCESS",(const char *)GetName());
    ReportSegMemory();
    goto Recovery;
  case ERROR_IO_INCOMPLETE:
    Fail("Not completed, but reported it has completed.");
    break;
  case ERROR_INVALID_PARAMETER:
    RptF("File read error: %s,ERROR_INVALID_PARAMETER",(const char *)GetName());
    goto Recovery;
  default:
    RptF("File read error: %s,%x",(const char *)GetName(), hr);
  Recovery:
#ifdef _WIN32
    ok = EmulateReadFileScatter(handle,overlap);
    if (ok)
    {
      _done = true;
      return true;
    }
    HRESULT err = hr;
    hr = GetLastError();
    RptF("  File read recovery from error %x has failed with error %x",err,hr);
#endif
    break;
  }
  _result = hr;
  _done = true;
  return true;
#endif
}
#endif

void OverlappedReadScatter::OnDone()
{
  Assert(_req);
  Assert(_done);
  _req->OnDone(_read, _result, this);
}

#if defined _WIN32 && !POSIX_FILES_COMMON
OverlappedWriteGather::OverlappedWriteGather
(
  IOverlapped *source, QFileSize offset, QFileSize size, AutoArray<RefTgtBuffer> &target, QFileSize pageSize
)
 : _source(source), _offset(offset), _size(size), _target(target), _pageSize(pageSize), _done(false), _ok(false)
{
  _totalSize = CreateFileSegments(_fileSegs, target, offset, size, pageSize);
}

OverlappedWriteGather::~OverlappedWriteGather()
{
  DeleteFileSegments(_fileSegs, _target, _pageSize);
}

int OverlappedWriteGather::GetMemNeeded() const
{
  return GetPageRecommendedSize();
}

bool OverlappedWriteGather::Perform(DiskPositionDesc &pos)
{
#if _LOG_OVERLAPPED_OPS
  DWORD start = ::GlobalTickCount();
#endif

#if !DISABLE_BUFFERING
  SetLastError(ERROR_HANDLE_EOF);
  Fail("No WriteFileGather with buffering");
  return false;
#endif

  HANDLE handle = _source->GetHandle();

  OVERLAPPED overlap;
  overlap.Offset = _offset;
  overlap.OffsetHigh = 0;
  overlap.hEvent = _owner->GetOverlapEvent();

  // store time
  FILETIME time;
  ::GetFileTime(handle, NULL, NULL, &time);

#if LOG_QUEUE>0
  LogF
  (
    "WriteFileGather %s:%x:%x",
    (const char *)GetName(), _offset, _size
  );
#endif
  while (true)
  {
    ::ResetEvent(overlap.hEvent);
    DWORD written;
    _ok = ::WriteFileGather(handle, _fileSegs.Data(), _totalSize, &written, &overlap) != FALSE;

    pos._handle = handle;
    pos._offset = _offset+written;
  
    if (_ok)
    {
      // save back time
      ::SetFileTime(handle, NULL, NULL, &time);
      // ensure data are written
      _ok = ::FlushFileBuffers(handle) != FALSE;
      _done = true;
#if _LOG_OVERLAPPED_OPS
      DWORD end = ::GlobalTickCount();
      totalOpsTime += end - start;
      LogF("### Async. writing %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
#endif
      return true;
    }

    HRESULT hr = GetLastError();
    if (hr == ERROR_IO_PENDING)
    {
      ::WaitForSingleObject(overlap.hEvent, INFINITE);
      DWORD rd = 0;
      _ok = GetOverlappedResult(handle, &overlap, &rd, FALSE) != FALSE;
      if (_ok)
      {
        // save back time
        ::SetFileTime(handle, NULL, NULL, &time);
        // ensure data are written
        _ok = ::FlushFileBuffers(handle) != FALSE;
        _done = true;
#if _LOG_OVERLAPPED_OPS
        DWORD end = ::GlobalTickCount();
        totalOpsTime += end - start;
        LogF("### Async_writing %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
#endif
        return true;
      }
    }

#if _LOG_OVERLAPPED_OPS
    DWORD end = ::GlobalTickCount();
    totalOpsTime += end - start;
    LogF("### Async_writing (err.) %s %d - %d (total %d)", (const char *)GetName(), start, end, totalOpsTime);
#endif
    // error processing
    switch (hr)
    {
    case ERROR_IO_INCOMPLETE:
      Fail("Not completed, but reported it has completed.");
      // save back time
      ::SetFileTime(handle, NULL, NULL, &time);
      _done = true;
      return true;
    case ERROR_NO_SYSTEM_RESOURCES:
    case ERROR_NONPAGED_SYSTEM_RESOURCES:
    case ERROR_PAGED_SYSTEM_RESOURCES:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case E_OUTOFMEMORY:
      RptF
      (
        "Out of memory when writing %s - %x",
        (const char *)GetName(), hr
      );
      // save back time
      ::SetFileTime(handle, NULL, NULL, &time);
      _done = false;
      return false;
    default:
      RptF
      (
        "File write error: %s,%x",
        (const char *)GetName(), hr
      );
      // save back time
      ::SetFileTime(handle, NULL, NULL, &time);
      _done = true;
      return true;
    }
  }
}
#endif


#pragma warning(disable:4355)
FileServerWorkerThread::FileServerWorkerThread(FileServerST *server)
: _server(server), 
_thread(this),
_cleanedUp(false),
_submitSemaphore(INT_MAX, 0), _terminateEvent(), _errorEvent(), _errorEventHandled(), _submittedUrgentOp(INT_MAX, 0)
{
  _queueProcessed = 0;
  _queueInProcess = 0;
  _toProcessUrgentOp = 0;
#if !POSIX_FILES_COMMON
  _overlapEvent = NULL;
#endif
}

FileServerWorkerThread::~FileServerWorkerThread()
{
  CleanUp();
}

void FileServerWorkerThread::Prepare()
{
#if !POSIX_FILES_COMMON
  _overlapEvent = ::CreateEvent(NULL,FALSE,FALSE,NULL);
#endif
  _queueProcessed = 0;
  _queueInProcess = 0;
  _toProcessUrgentOp = 0;
  _thread.Start();
  // this thread usually works for a very short time, but if it does, it should get a time
  _thread.SetPriority(MultiThread::ThreadBase::PriorityAboveNormal); //but this is already set in _thread constructor
}

void FileServerWorkerThread::CleanUp()
{
  if (!_cleanedUp && _thread.IsRunning())
  {
    _terminateEvent.Unblock();
    _thread.Join();
    Maintain();  //Maintain cannot be called when thread is over?!
#if !POSIX_FILES_COMMON
    ::CloseHandle(_overlapEvent),_overlapEvent = NULL;
#endif
    _cleanedUp = true;
  }
}

bool FileServerWorkerThread::Wait(IOverlapped *op, int timeout)
{
  DWORD startTime = GlobalTickCount();
  bool done = true;
  //PROFILE_SCOPE_EX(fWtWr,file);
  op->AddRef();
  if (!op->IsDone())
  {
    Update();
  while (!op->IsDone())
  {
    // ... we can do anything here
      // wait until some item is processed before checking again
      {
        PROFILE_SCOPE_EX(fsIPW,*)
//         if (PROFILE_SCOPE_NAME(fsIPW).IsActive())
//         {
//           PROFILE_SCOPE_NAME(fsIPW).AddMoreInfo(op->GetName());
//         }
        if (!_itemProcessedEvent.TryWait(timeout))
        {
          done = false;
          break;
      }
        if (timeout>0)
          timeout = intMax(0, startTime + timeout - GlobalTickCount());
      }
    // we can update here - req keeps reference count
    Update();
  }
  }
  op->Release();
  return done;
}

bool FileServerWorkerThread::IsDone(RString name) const
{
  for (int i=0; i<_queue.Size(); i++)
  {
    IOverlapped *op = _queue[i];
    if (op && !op->IsDone() && stricmp(op->GetName(), name) == 0)
      return false;
  }
  return true;
}

void FileServerWorkerThread::Wait(RString name)
{
  //PROFILE_SCOPE_EX(fWtWn,file);
  while (!IsDone(name))
  {
    // ... we can do anything here
    Sleep(0);
    Update();
  }
}

bool FileServerWorkerThread::IsWaiting(IOverlapped *op) const
{
  for (int i=0; i<_queue.Size(); i++)
  {
    if (!_queue[i]->IsDone() && _queue[i]->DependsOn(op)) return true;
  }
  return false;
}

//#define DEBUG_MULTI_THREADS 1
void FileServerWorkerThread::Maintain()
{
  //PROFILE_SCOPE_EX(fsSWT,*);
  // remove all request that are already done
  #ifdef DEBUG_MULTI_THREADS
  printf("_errorEvent.Acquire(0) ");
  #endif
  if ( _errorEvent.Acquire(0) )
  {
    #ifdef DEBUG_MULTI_THREADS
    printf(" SIGNALED\n");
    #endif
    // Out of memory during IOverlapped::Process
    EnterLock();
    int memNeeded = _queue[_queueProcessed]->GetMemNeeded();
    LeaveLock();
    FreeOnDemandGarbageCollectSystemMemoryLowLevel(memNeeded);
    _errorEventHandled.Unblock();
  }
  #ifdef DEBUG_MULTI_THREADS
  else  printf(" NONsignaled\n");
  #endif

  // reading _queueProcessed is atomic and outside of the following scope it can only grow
  if (_queueProcessed>0)
  {
    PROFILE_SCOPE_EX(fsSWD,*);
    EnterLock();
    Assert(_queueProcessed==_queueInProcess || _queueProcessed+1==_queueInProcess);
    AutoArray< Ref<IOverlapped>, MemAllocLocal<Ref<IOverlapped>,16> > toProcess;
    for (int i=0; i<_queueProcessed; i++)
    {
      // avoid recursion - before calling OnDone make sure the item is no longer there and it cannot be called again
      toProcess.Add(_queue[i]);
    }
    _queue.Delete(0,_queueProcessed);
    // we may be inside of Perform and _queueInProcess may be different from _queueProcessed
    // however none of them may change during Lock
    _queueInProcess -= _queueProcessed;
    _queueProcessed = 0;
  LeaveLock();
    // process the items outside of the lock - processing may be calling more file operations
    for (int i=0; i<toProcess.Size(); i++)
    {
      toProcess[i]->OnDone();
}
  }
}

void FileServerWorkerThread::Update()
{
  Maintain();
}

int FileServerWorkerThread::FindForward(IOverlapped *item, int from, int to)
{
  // if there is any dependency, do not move in front of it
  // TODO: we might be able to move the dependency as well
  for (int i=to-1; i>=from; i--)
  {
    if (item->DependsOn(_queue[i]))
    {
      return i+1;
    }
  }
  // no dependecy - we may move as front as possible
  return from;
  
}

void FileServerWorkerThread::Forward(IOverlapped *item, int moveTo, int moveFrom)
{
//  Log(
//    "Moving %x(%s) from %d to %d, done %d",
//    item,cc_cast(item->GetName()),moveFrom,moveTo,_queueInProcess
//  );
  // we cannot move across _queueInProcess
  Assert(moveTo>=_queueInProcess);
  Assert(moveFrom>moveTo);
  Ref<IOverlapped> temp = _queue[moveFrom];
  // move from moveFrom to moveTo
  for (int i=moveFrom; i>moveTo; i--)
  {
    _queue[i] = _queue[i-1];
  }
  _queue[moveTo] = temp;
}

void FileServerWorkerThread::MakeItemUrgent(int index)
{
  // find all overlapped items related to given request
  // and move them as near to the head as possible
  EnterLock();
  // we cannot move from or into the area where the requests are already processed
  // during movement do not change ordering of items connected to the same request
  if (index>=_queueInProcess)
  {
    int sortFrom = _queueInProcess;
    // if any requests are already marked as urgent, do not move in front of them
    for (int i=_queueInProcess; i<index; i++)
    {
      IOverlapped *item = _queue[i];
      if (item->GetFileRequest() && item->GetFileRequest()->_priority==0)
      {
        sortFrom = i+1;
      }
    }

    if (index>sortFrom)
    {
    IOverlapped *item = _queue[index];
    // move as forward as possible
      int tgtPos = FindForward(item,sortFrom,index);
    if (tgtPos!=index)
    {
      Forward(item,tgtPos,index);
    }
  }
  }
  LeaveLock();
}

bool FileServerWorkerThread::VerifyRequestPosition(FileRequest * req) 
{
  bool ret = true;
  #if _DEBUG || _PROFILE
  // request may be already in the processed area
  int minIndex = INT_MAX;
  int maxIndex = 0;
  for (int i=0; i<_queue.Size(); i++)
  {
    IOverlapped *item = _queue[i];
    if (item->GetFileRequest()==req)
    {
      if (i<minIndex) minIndex = i;
      if (i>maxIndex) maxIndex = i;
    }

  }
  // test functionality: make sure there are no non-urgent requests which could be moved before us now
  for (int i=_queueInProcess; i<maxIndex; i++)
  {
    IOverlapped *item = _queue[i];
    if (item->GetFileRequest() && item->GetFileRequest()->_priority!=0)
    {
      LogF("Non-urgent %s (for %s)",cc_cast(item->GetDebugName()),cc_cast(req->GetDebugName()));
      ret = false;
    }
  }
  #endif
  return ret;
}

void FileServerWorkerThread::MakeRequestUrgent(FileRequest *req)
{
  // find all overlapped items related to given request
  // and move them as near to the head as possible
  EnterLock();
  // we cannot move into the area where the requests are already processed
  int sortFrom = _queueInProcess;
  // if any requests are already marked as urgent, do not move in front of them
  for (int i=_queueInProcess; i<_queue.Size(); i++)
  {
    IOverlapped *item = _queue[i];
    if (!item->GetFileRequest()) continue;
    if (item->GetFileRequest()->_priority==0)
    {
      sortFrom = i+1;
    }
    else
    {
      break;
  }
  }
  int placeTo = sortFrom;
  // during movement do not change ordering of items connected to the same request
  for (int i=sortFrom; i<_queue.Size(); i++)
  {
    IOverlapped *item = _queue[i];
    if (item->GetFileRequest()==req)
    {
      // move as forward as possible
      int tgtPos = FindForward(item,placeTo,i);
      if (tgtPos!=i)
      {
        Forward(item,tgtPos,i);
        placeTo = tgtPos+1;
      }
    }
  }
  Assert(placeTo>=sortFrom);
  #if _PROFILE || _DEBUG
  VerifyRequestPosition(req);
  #endif
  
  LeaveLock();
}

/**
@param urgent submit to the queue head (will be processed first)
*/

IOverlapped *FileServerWorkerThread::Submit(IOverlapped *req, bool urgent)
{
  req->SetOwner(this);
  EnterLock();
  // no Maintain() here - avoid recursion
  int index = _queue.Add(req);
  if (urgent)
  {
    // make sure it is moved as front as possible before releasing the lock
    MakeItemUrgent(index);
  }
  LeaveLock(); 
  if (urgent) _submittedUrgentOp.Release();
  _submitSemaphore.Release();
  return req;
}

DWORD WINAPI FileServerWorkerThread::DoWorkCallback(void *context)
{
  FileServerWorkerThread *ctx = (FileServerWorkerThread *)context;
  ctx->DoWork();
  return 0;
}

//#include <El/Statistics/statistics.hpp>

/**
Introduced because of news:gh3jee$oad$1@new-server.localdomain

During rendering something corrupts server worker thread queue with a GPU fence number
*/
bool FileServerWorkerThread::CheckIntegrity() const
{
  unconst_cast(this)->EnterLock();
  for (int i=0; i<_queue.Size(); i++)
  {
    if (_queue[i]->GetOwner()!=this)
    {
      RptF("Corrupt owner for %p",_queue[i].GetRef());
      return false;
    }
  }
  unconst_cast(this)->LeaveLock();
  return true;
}

void FileServerWorkerThread::DoWork()
{
  SetThreadName(-1, "FileServerWorker");
  while (true)
  {
    BlockerArItem objs[] = {&_submitSemaphore, &_terminateEvent};
    #ifdef DEBUG_MULTI_THREADS
    printf("WaitForMultiple(objs, 2, false, INFINITE) ");
    #endif
    DWORD ret = WaitForMultiple(objs, 2, false, INFINITE);
    #ifdef DEBUG_MULTI_THREADS
    printf(" -- returned %d (1=sem,2=term)\n", ret);
    #endif
    switch (ret)
    {
    case 1: //semaphore released
      {
        // we have some item to be processed
        EnterLock();
        Assert(_queueInProcess==_queueProcessed);
        // get the item from the queue
        IOverlapped *item = _queue[_queueProcessed];
        #if 1
        // if the item would cause a seek and it is not urgent, we will rather wait for a while
        // it is quite possible another request will come which will be non seeking
        if (
          _toProcessUrgentOp<=0 && item->DetectSeek(_lastPos) &&
          item->GetFileRequest() && item->GetFileRequest()->_priority>FileRequestPriority(0)
        )
        {
          PROFILE_SCOPE_DETAIL_EX(fsSek,file);
          LeaveLock();

          BlockerArItem objs[] = {&_submittedUrgentOp};
          if (WaitForMultiple(objs, lenof(objs), false, 4)>=0) //<-- timeout raised from 2ms to 4ms, see http://forums.bistudio.com/showthread.php?130647-ARMA-2-Operation-Arrowhead-Linux-Server-beta-1-60-88775&p=2103469&viewfull=1#post2103469
          {
            _toProcessUrgentOp++;
          }
          EnterLock();
          // item might have changed while lock was released          
          #if _PROFILE || _DEBUG
          if (item != _queue[_queueProcessed])
          {
            __asm nop;
          }
          #endif
          item = _queue[_queueProcessed];
          //static StatEventRatio ratio("Avoid seek");
          //ratio.Count(!item->DetectSeek(_lastPos));
        }
        #endif
        if (!item->GetFileRequest() || item->GetFileRequest()->_priority==FileRequestPriority(0))
        {
          _toProcessUrgentOp--;
        }
        // prevent the item which will be doing Perform from being manipulated with
        _queueInProcess++;
        LeaveLock();
        // perform may take very long
        // while we are in perform, we cannot manipulate the item 
#if DEBUG_PRELOADING
        FileRequest *reqst = item->GetFileRequest();
        if (reqst && reqst->DebugMe())
        {
          RptF("   perform request %s", cc_cast(item->GetName()));
        }
#endif
        bool ok;
        {
          PROFILE_SCOPE_EX(fsWTP,file);
          if (PROFILE_SCOPE_NAME(fsWTP).IsActive())
          {
            PROFILE_SCOPE_NAME(fsWTP).AddMoreInfo(item->GetDebugName());
          }
          ok = item->Perform(_lastPos);
        _itemProcessedEvent.Set();
        }
        if (ok)
        {
          EnterLock();
          _queueProcessed++;
          Assert(_queueInProcess==_queueProcessed);
          LeaveLock();
        }
        else
        {
          _errorEvent.Unblock();
          BlockerArItem objs[] = {&_errorEventHandled, &_terminateEvent};
          DWORD ret = WaitForMultiple(objs, 2, false, INFINITE);
          if (ret == 2) return; //terminate event signaled
          // resubmit the request
          _submitSemaphore.Release();
          EnterLock();
          _queueInProcess--;
          Assert(_queueInProcess==_queueProcessed);
          LeaveLock();
        }
      }
      break;
    case 2: //terminateEvent signaled
      GMemFunctions->OnThreadExit();
      return;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// 
// File server
//

#if _ENABLE_REPORT && _ENABLE_PERFLOG
inline bool FileServerST::InterestedIn(FileServerHandle handle) const
{
  return false;
  //const char *name = _handles.GetName(handle);
  //return strstr(name,"dta\\music")!=NULL;
}
inline bool FileServerST::InterestedIn(FileServerHandle handle, QFileSize start, QFileSize size) const
{
  #if 1
  return false;
  #else
  if (!GDebugger.IsDebugger()) return false; // avoid flooding log - debugging aid only
  RString filename = _handles.GetName(handle);
  if (false)
  {
    struct InterestItem
    {
      const char *filename;
      QFileSize start,size;
    };
    // list all file regions we are interested in
    // used when pbo region is known
    static const InterestItem regions[]=
    {
      //{"xbox\\addons\\misc.pbo",0x73b000,0x1000},
      //{"xbox\\addons\\misc.pbo",0x73c000,0xb000},
      {"",0,0},
    };
    for (int i=0; i<lenof(regions); i++)
    {
      const InterestItem &item = regions[i];
      // we are interested in anything which overlaps our region, even partially
      if (
        !strcmp(filename,item.filename) &&
        start<item.start+item.size && start+size>item.start
      )
      {
        return true;
      }
    }
  }
  // check based on a filename as well
  RString name = QFBank::FileOnOffset(handle,start,start+size);
  return InterestedIn(name);
  #endif
}
inline bool FileServerST::InterestedIn(const char *name) const
{
  //return false;
  return strstr(name,".paa")!=NULL;
}
#endif

RString GetReqDebugName(FileServerHandle handle, QFileSize start, QFileSize size, FileRequestPriority prio)
{
  RString handleName = ((FileServerST *)GFileServer)->GetHandleName(handle);
  RString logName = QFBank::FileOnOffset(handle,start,start+size);
  // avoid long paths in diagnostics (commonly caused by PBOs accessed via registry set absolute paths)
  const int maxLen = 40;
  while (handleName.GetLength()>maxLen)
  {
    const char *path = strchr(handleName,'\\');
    if (!path) path = strchr(handleName,'/');
    if (!path) break;
    handleName = RString(path+1);
  }
  return Format("(P%d) %x:%x:%s/%s",prio,start,size,cc_cast(handleName),cc_cast(logName));
}

RString GetDoneDebugName(FileRequestHandle req, bool done)
{
  if (!req) return "0 ";
  return done ? "Y " : "N ";
}
RString FileRequest::GetDebugName() const
{
  return GetReqDebugName(_fileLogical,_fromLogical,_sizeLogical,_priority);
}


void FileServerST::MoveToFront(FileInCache *item)
{
  //Assert(_cache._list.CheckIntegrity());
  //FS_SCOPE_LOCK(); // scope lock is already executed in all locations calling MoveToFront
  _cache._list.Refresh(item);
}

void FileServerST::Maintain()
{
  PROFILE_SCOPE_EX(fsMnn,*);
  FS_SCOPE_LOCK();
  while( _cache._list.MemoryControlled()>_maxSize && _cache._list.ItemCount()>1 )
  {
    // if there is nothing to release, we need to continue
    if (FreeOneItem()==0) break;
  }
  #if _PROFILE || _DEBUG
  static int counter;
  static bool doIt = false;
  if (doIt && counter++%1000==0)
  {
    Sleep(1500);
}
  #endif
}

void FileServerST::DeleteItem(FileInCache *item)
{
  PROFILE_SCOPE_EX(fsFre,file);
  if (PROFILE_SCOPE_NAME(fsFre).IsActive())
  {
    PROFILE_SCOPE_NAME(fsFre).AddMoreInfo(GetReqDebugName(item->_loc._handle,item->_loc._start,item->_loc._size,FileRequestPriority(-1)));
  }
  #if OPEN_HANDLE_DIAGS>300
    Log("Discarded %x:%x:%x",item->_handle,item->_start,item->_cachedSize);
  #endif

  // item can be never deleted while there are any requests waiting for it
  Assert(item->_request.Size()==0);
  _handles.Close(item->_loc._handle,true);
  _cache._list.Delete(item);
  _cache._table.Remove(item->_loc);
}

size_t FileServerST::FreeOneItem()
{
  FS_SCOPE_LOCK();
  // never release the last item - it may be needed as a part of the active request
  FileInCache *item = _cache._list.First();
  while (item)
  {
    if (item->_request.Size()<=0 && item->_locked<=0)
    {
      break;
    }
    item = _cache._list.Next(item);
  }
  if (!item) return 0;
  size_t released = item->GetStoredMemoryControlled();
  DeleteItem(item);
  DoAssert(released!=0);
  return released;
}

float FileServerST::Priority() const
{
  // empirical value
  return 0.92;
}

RString FileServerST::GetDebugName() const
{
  return "File cache";
}

bool FileServerST::UsesVirtualMemory() const
{
  #if !defined _WIN32
    return true;
  #else
    // on PC we use memory store for the file cache, not using any virtual memory
    return false;
  #endif
}

size_t FileServerST::MemoryControlled() const
{
  // estimate how much memory is controlled by this container
  //FS_SCOPE_LOCK();Assert(_cache._list.CheckIntegrity());
  // FS_SCOPE_LOCK not needed - MemoryControlled is atomic
  return _cache._list.MemoryControlled();
}

size_t FileServerST::MemoryControlledRecent() const
{
  // estimate how much memory is controlled by this container
  // FS_SCOPE_LOCK not needed - MemoryControlledRecent is atomic
  //FS_SCOPE_LOCK();Assert(_cache._list.CheckIntegrity());
  return _cache._list.MemoryControlledRecent();
}
void FileServerST::MemoryControlledFrame()
{
  FS_SCOPE_LOCK();
  return _cache._list.Frame();
}


void FileServerST::FlushBank(QFBank *bank)
{
  FS_SCOPE_LOCK();
  // release all files from this bank
  for (FileInCache *file = _cache._list.First(); file; )
  {
    FileInCache *next = _cache._list.Next(file);
    // check if the handle is asoociated with the bank
    if (bank &&  bank->HandleOwned(file->_loc._handle))
    {
      #if OPEN_HANDLE_DIAGS>300
      Log("Handle %x to %s flushed",GetWinHandle(file->_loc._handle),(const char *)_handles.GetName(file->_loc._handle));
      #endif
//LogF("Flushed from cache %s:%x:%x, buffer 0x%x", cc_cast(_handles.GetName(file->_loc._handle)),file->_loc._start,file->_loc._size, file->_buffer);
      _handles.Close(file->_loc._handle,true);
      _cache._list.Delete(file);
      _cache._table.Remove(file->_loc);
    }
    file = next;
  }
}

static void RegularCallback()
{
  GFileServer->Update();
}

void FileServerST::OpenPreloadLog(const char *name)
{
#ifndef _SUPER_RELEASE 
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  if (GLogFileServer)
  {
    _logFiles.Open(name);
  }
#endif 
#endif  
}

void FileServerST::SavePreloadLog()
{
  Assert(_initDone);
#ifndef _SUPER_RELEASE
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  if (GLogFileServer)
  {
    _logFiles.Close();
  }
#endif 
#endif  
}

void FileServerST::OpenDVDLog(const char *name)
{
#ifndef _SUPER_RELEASE
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  if (GLogDVD)
  {
    while (_workerThread.Size() > 0 || _queue.Size() > 0)
    {
      Update();
      FreeOnDemandGarbageCollect(1024*1024,256*1024);
    }
    LogDVD.Open(name);
  }
#endif 
#endif  
}

void FileServerST::SaveDVDLog()
{
  Assert(_initDone);
#ifndef _SUPER_RELEASE
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  if (GLogDVD)
  {
    while (_workerThread.Size() > 0 || _queue.Size() > 0)
    {
      Update();
      FreeOnDemandGarbageCollect(1024*1024,256*1024);
    }
    LogDVD.Close();
  }
#endif 
#endif  
}

void FileServerST::WaitForRequestsDone()
{
  Assert(_initDone);
  while
  (
    _workerThread.Size() > 0 || _queue.Size() > 0
  )
  {
    Update();
    FreeOnDemandGarbageCollect(1024*1024,256*1024);
  }
}

#include <Es/Threads/pothread.hpp>
void FileServerST::Init()
{
  if (_initDone) return;
  _initDone = true;
  
  if (_requestingThread==0)
  {
    _requestingThread = GetCurrentThreadId();
  }
  else
  {
    Assert(_requestingThread==GetCurrentThreadId());
  }
  
  if (CheckMainThread())
  {
    // we can do RegularCallback from here only when initialized on the main thread
    // in other cases we assume the thread handles regular updates on its own
    GlobalAtAlive(RegularCallback);
  }
  
  const char *preloadIniName = "preload.ini";
  // note: Preload as part of server init is not clean
  (void)preloadIniName;
  //Preload(preloadIniName);
}

void FileServerST::Start()
{
  Init();
}

/// helper to check if all locks have been released
class VerifyNoLock
{
  FileServerST *_server;
  
  public:
  VerifyNoLock(FileServerST *server):_server(server){}
  bool operator () (
    const FileServerST::FileInCache *item,
    FileServerST::FileCache::CacheTableType *table
  ) const
  {
    if (item->_locked!=0)
    {
      RString name = QFBank::FileOnOffset(item->_loc._handle,item->_loc._start,item->_loc._start+item->_loc._size);
      RptF("Item locked %s:%s",cc_cast(cc_cast(_server->GetHandleName(item->_loc._handle))),cc_cast(name));
    }
    return false;
  }
};


void FileServerST::Stop()
{
  AssertSameThread(_requestingThread);
  #if 1
  // wait until pending request are done
  // as we are going to drop the cache anyway, we can drop some requests from the queue
  // this has some limitations
  // 1) we cannot drop requests with a handler
  // 2) dropping from worker thread may be too complicated
  // we drop queue only, and only when there is no handler
  while (_workerThread.Size() > 0 || _queue.Size() > 0)
  {
    // as CancelRequest is not implemented yet, we cannot use it here
    #if 0
    // if queue top can be dropped instead of processing it, drop it
    RefR<FileRequest> req;
    if (_queue.HeapGetFirst(req))
    {
      if (req->_handlers.Size()==0)
      {
        CancelRequest(req.GetRef());
        continue;
      }
    }
    // process 1st request from the queue only
    ProcessRequests(1);
    #else
    ProcessRequests();
    #endif
  }
  #if 1 // _RELEASE
    {
      FS_SCOPE_LOCK();

      FileInCache *item = _cache._list.First();
      while (item)
      {
        FileInCache *next = _cache._list.Next(item);
        if (item->_request.Size()<=0 && item->_locked<=0)
        {
          ReadHandleInfo *info = (ReadHandleInfo *)item->_loc._handle;
          // do not flush permanent handles (i.e. data PBOs)
          if (!info->_permanent)
          {
            DeleteItem(item);
          }
        }
        item = next;
      }

      // no cache entry should be locked now
      // no requests may exists now, and no locks should therefore exists as well
      #if _ENABLE_REPORT && _ENABLE_PERFLOG
        //_cache._table.ForEachF(VerifyNoLock(this));
      #endif
    }
  #endif

  // flush any handles that can be flushed
  _handles.FlushAll();
  #endif
  
  if (CheckMainThread(_requestingThread))
    GlobalCancelAtAlive(RegularCallback);
  
  // the worker thread is shutting down and a new one may be soon started - prepare for that
  _initDone = false;
}


DEFINE_FAST_ALLOCATOR(FileServerST::FileInCache)

FileServerST::FileInCache::~FileInCache()
{
  PROFILE_SCOPE_EX(fsCDs,file)
  Assert(_request.Size()==0);
}

PosQIStreamBuffer FileServerST::FileInCache::GetBufferRef() const
{
  return PosQIStreamBuffer(_buffer.GetBuffer(),_loc._start,_loc._size);
}

size_t FileServerST::FileInCache::GetMemoryControlled() const
{
  return sizeof(*this) + _buffer.GetSize();
}

bool FileRequest::IsMoreUrgent(const FileRequest &req) const
{
  if (_priority == req._priority)
  {
    // first of all check handles
    if (_filePhysical!=req._filePhysical)
    {
        // we have no idea about file ordering
        // but we still want to avoid switches between files
        return _filePhysical<req._filePhysical;
    }
    return _fromPhysical<req._fromPhysical;
  }
  return _priority < req._priority;
}

bool FileRequest::operator == (const FileRequest &with) const
{
  return
  (
  _fileLogical==with._fileLogical && _fromLogical==with._fromLogical && _sizeLogical==with._sizeLogical
  );
}
bool FileRequest::Contains(const FileRequest &what) const
{
  return(
    _fileLogical==what._fileLogical && _fromLogical<=what._fromLogical &&
    _fromLogical+_sizeLogical>=what._fromLogical+what._sizeLogical
  );
}



bool FileRequest::Contains(FileServerHandle file, QFileSize from, QFileSize size) const
{
  return
  (
    _fileLogical==file && _fromLogical<=from && _fromLogical+_sizeLogical>=from+size
  );
}

bool FileRequest::Overlaps(FileServerHandle file, QFileSize from, QFileSize size) const
{
  return
  (
    _fileLogical==file && _fromLogical+_sizeLogical>from && _fromLogical<from+size
  );
}

#pragma warning(disable:4355)
FileServerST::FileServerST()
:_maxSize(128*1024*1024),_initDone(false),_workerThread(this),_requestingThread(0)
#pragma warning(default:4355)
{
  #if USE_MEM_STORE
    // on PC with memory mapping it has no sense to free filecache as low-level
    // as it does not release any virtual space anyway
    RegisterFreeOnDemandMemory(this);
  #else
    RegisterFreeOnDemandLowLevelMemory(this);
  #endif

  #ifndef _WIN32
  extern MultiThread::RunningThread<MultiThread::ThreadBase> *GMultiThreadRegistration;
  GMultiThreadRegistration = new MultiThread::RunningThread<MultiThread::ThreadBase>;
  #endif

    #ifdef _WIN32
      SYSTEM_INFO info;
      GetSystemInfo(&info);
      _readScatterPageSize = info.dwPageSize;
      //int mallocPage = GetPageRecommendedSize();
      //if (_readScatterPageSize<mallocPage) _readScatterPageSize = mallocPage;

      DoAssert((GetPageRecommendedSize()&(_readScatterPageSize-1))==0);
    #else
      _readScatterPageSize = 4*1024;
    #endif
  _nRequestsLoading = 0;
  _sizeRequestsLoading = 0;
  _queueByteSize =0;

  _workerThread.Prepare();
}

#if USE_MEM_STORE
const int MemStoreSizeMB = 512;
/// memory store used even on Xbox so that we have a separate storage for the file cache
MemoryStore GMemStore;
#endif

void FileServerST::Open(QIFStream &in, const char *name, QFileSize from, QFileSize to)
{
  QIFStreamB file;
  file.AutoOpen(name,from,to);
  in = file;
}

void FileServerST::SetMaxSize(size_t maxCacheSize)
{
  #if USE_MEM_STORE
    // we needs a few MB reserved to accommodate for pending requests
    const size_t reserveBytes = 64*1024*1024;
    const size_t storeSize = MemSizeTToSizeT((mem_size_t)maxCacheSize+reserveBytes);
    GMemStore.Init(storeSize);
    if (maxCacheSize>storeSize) maxCacheSize = storeSize;
  #endif
  _maxSize = maxCacheSize;
  // call maintain to shrink cache size if needed
  Maintain();
}

void FileServerST::Clear()
{
  AssertSameThread(_requestingThread);
  // if there are any pending requests, wait for them to finish
  // no need to process any more requests
  Assert(_handles.CheckIntegrity());
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    _workerThread.EnterLock();
    int totalQueue = 0;
    for (int i=0; i<_queue.Size(); i++)
    {
      totalQueue += _queue[i]->_sizePhysical;
      DoAssert(_queue[i]->RefCounter()==1);
    }
    DoAssert(totalQueue==_queueByteSize);
    int totalPending = 0;
    for (int i=0; i<_workerThread.Size(); i++)
    {
      FileRequest *req = _workerThread._queue[i]->GetFileRequest();
      if (req)
      {
        totalPending += req->_sizePhysical;
        DoAssert(req->RefCounter()==1);
      }
    }
    DoAssert(totalPending == _sizeRequestsLoading);
    DoAssert(_workerThread.Size() == _nRequestsLoading);
    _workerThread.LeaveLock();
  #endif
  _queue.Clear();
  _queueByteSize = 0;
  Assert(_handles.CheckIntegrity());
  while (_workerThread.Size() > 0)
  {
    _workerThread.Update();
  }
  Assert(_handles.CheckIntegrity());
  // clear all cached items
  {
    FS_SCOPE_LOCK();
    _cache._table.Clear();
    while (_cache._list.First())
    {
      size_t released = FreeOneItem();
      if (released==0)
      {
        break;
      }
    }
    Assert(_handles.CheckIntegrity());
    _cache._list.Clear();
  
  }
  _handles.Clear();
  //LogF("File server cleared");
}


FileServerST::~FileServerST()
{
#ifndef _WIN32
  //dirty hack: FIX of Linux FileServer destruction
  //register to MultiThread2 again, as it can be already unregistered due to "wrong" static variables destruction on server closing
  MultiThread::RunningThread<MultiThread::ThreadBase> *mtRegistration = new MultiThread::RunningThread<MultiThread::ThreadBase>;
  //NOTE: it would never be destructed (but it is called only on server exit, hopefully)
#endif
  Finalize();
}

void FileServerST::Finalize()
{
  if (!_serverFinalized)
  {
    while (_workerThread.Size() > 0)
    {
      _workerThread.Update();
    }
    DoAssert(_workerThread.Size()==0);
    DoAssert(_queue.Size()==0);
    DoAssert(_queueByteSize==0);
    _workerThread.CleanUp();
    _queue.Clear();
    ProcessHandlers(-1);
    #if _ENABLE_REPORT && _ENABLE_PERFLOG
      FS_SCOPE_LOCK();
      _cache._table.ForEachF(VerifyNoLock(this));
    #endif
    _serverFinalized = true;
  }
}

FileRequestHandle FileServerST::Request(
  FileRequestPriority priority, const char *name, QFileSize start, QFileSize size
)
{
  FileServerHandle handle = OpenReadHandle(name);
  if (CheckReadHandle(handle))
  {
    // get file size
    QFileSize maxSize = _handles.GetSize(handle);
    if (start>maxSize) start = maxSize;
    if (size>maxSize-start) size = maxSize-start;
    FileRequestHandle ret = Request(NULL,NULL,priority,handle,start,size);
    CloseReadHandle(handle);
    return ret;
  }

  return NULL;
}

#define COMPLETION_FROM_REQUEST(req) (static_cast<FileRequestCompletionRefCount *>((RefCount *)req.GetRef()))
#define REQUEST_FROM_COMPLETION(req) (static_cast<FileRequest *>((RefCount *)req.GetRef()))

bool FileServerST::Preload(
  FileRequestCompletionHandle &request, 
	RequestableObject *obj, RequestableObject::RequestContext *ctx,
	FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
)
{
  AssertSameThread(_requestingThread);
  PROFILE_SCOPE_EX(fsPre,file);
  
  FileRequest *candidate = REQUEST_FROM_COMPLETION(request);
  // try matching the previous request first
  FileRequestHandle req;
  if (candidate && candidate->Contains(handle, start, size))
  {
    req = candidate;
    // if the older request contains the newer one, we might need to update some information
    if (candidate->_priority > priority)
    {
      FS_SCOPE_LOCK(); // why scope lock? We do not seem to be modifying the cache
      // we need to change the priority key
      candidate->_priority = priority;
      _queue.HeapUpdateUp(candidate);
    }
    if (obj)
    {
      // add handler - but only if not already there
      bool found = candidate->FindHandler(obj,ctx);
      if (!found)
      {
        candidate->AddHandler(obj,ctx);
      }
    }
  }
  else
  {
    req = Request(obj,ctx,priority,handle,start,size);
  }
  
  request = COMPLETION_FROM_REQUEST(req);
  
  #if 0
  bool done = req.IsNull();
  #else
  bool done = RequestIsDone(req);
  #endif
  // we will destroy the handle now, but it is very likely in some frame which will come soon the same request will be created again
  if (PROFILE_SCOPE_NAME(fsPre).IsActive())
  {
    RString logName = GetReqDebugName(handle,start,size,priority);
    // empty names means not inside of PBO - we are not interested then
    if (!logName.IsEmpty())
    {
      PROFILE_SCOPE_NAME(fsPre).AddMoreInfo(GetDoneDebugName(req,done)+logName);
    }
  }
  return done;
}

bool FileServerST::Preload(FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size)
{
  PROFILE_SCOPE_EX(fsPre,file);
  FileRequestHandle req = Request(NULL,NULL,priority,handle,start,size);
  bool done = RequestIsDone(req);
  if (PROFILE_SCOPE_NAME(fsPre).IsActive())
  {
    RString logName = GetReqDebugName(handle,start,size,priority);
    // empty names means not inside of PBO - we are not interested then
    if (!logName.IsEmpty())
    {
      PROFILE_SCOPE_NAME(fsPre).AddMoreInfo(GetDoneDebugName(req,done)+logName);
    }
  }
  // we will destroy the handle now, but it is very likely in some frame which will come soon the same request will be created again
  return done;
}


bool FileServerST::Preload
(
  FileRequestPriority priority, const char *name, QFileSize start, QFileSize size
)
{
  PROFILE_SCOPE_EX(fsPre,file);
  FileRequestHandle req = Request(priority,name,start,size);
  bool done = RequestIsDone(req);
  // we will destroy the handle now, but it is very likely in some frame which will come soon the same request will be created again
  if (PROFILE_SCOPE_NAME(fsPre).IsActive())
  {
    RString logName = GetReqDebugName(OpenReadHandle(name),start,size,priority);
    // empty names means not inside of PBO - we are not interested then
    if (!logName.IsEmpty())
    {
      PROFILE_SCOPE_NAME(fsPre).AddMoreInfo(GetDoneDebugName(req,done)+logName);
    }
  }
  return done;
}

DEFINE_FAST_ALLOCATOR(FileRequest)

void FileRequest::OnBufferCompleted(bool executeHandlers, bool success, FileServerST::FileInCache *item)
{
  // store the result
  // the result slot is already created on result creation
  Assert(CheckResultItem(item));
  // _targetsNotDone = 0 is used to signal completion. Before signaling it we need to be sure all changes are written
  MemoryPublish();
  // not all requests use all buffers from the request
  // but we should be registered only with buffers we are interested in
  if (--_targetsNotDone!=0)
  {
    // buffer completed, but it was not the last one
    Assert(_targetsNotDone>0);
    //_server->SignalRequestCompleted();
    return;
  }
  ExecuteHandlers(executeHandlers,success);
}

void FileRequest::ExecuteHandlers(bool executeHandlers, bool success)
{
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  bool interesting = _server->InterestedIn(_fileLogical,_fromLogical,_sizeLogical);
  if (interesting)
  {
    LogF(
      "Request completed %s:%x:%x (%s)",
      cc_cast(_server->_handles.GetName(_fileLogical)), _fromLogical, _sizeLogical,
      cc_cast(QFBank::FileOnOffset(_fileLogical,_fromLogical,_fromLogical+_sizeLogical))
    );
  }
#endif
  Assert(_targetsNotDone==0);
  // execute all handlers
  for (int i=0; i<_handlers.Size(); i++)
  {
    const RequestableObjectRequestInfo &src = _handlers[i];
    if (!src._object) continue;
    _server->ExecuteHandler(executeHandlers,src._context,src._object,success);
  }
  _handlers.Clear();
}


void FileRequest::OnDone(QFileSize read, HRESULT result, OverlappedReadScatter *op)
{
#if DEBUG_PRELOADING
  if (DebugMe())
  {
    RptF("   FileRequest::OnDone ... %d", result);
  }
#endif
  if (result == ERROR_SUCCESS)
  {
    _server->RequestDone(this, read);
  }
  else
  {
    // Error recovery
    if (_filePhysical != _fileLogical)
    {
      LogF("Read from preload cache failed, reading from original position");
      _server->_handles.Close(_fileLogical,false);

      // try to read data from original position
      _filePhysical = _fileLogical;
      _fromPhysical = _fromLogical;

      // add operation once more
      op->Reset();
      _server->Submit(op);

      // _server->_handles.Read(this);
    }
    else
    {
#ifdef _WIN32
      if (FileServer::GFileServerFunctions)
        FileServer::GFileServerFunctions->ReportFileReadError(result,_server->_handles.GetName(_filePhysical));
#endif
    }
  }
}

#if _ENABLE_REPORT && _ENABLE_PERFLOG
//! cummulate time spend in waiting for I/O
class TotalTime
{
  public:
  int _total;
  
  TotalTime()
  {
    _total = 0;
  }
  ~TotalTime()
  {
    LogF("Total I/O waiting time %d ms",_total);
  }
};

static TotalTime STotalWaitTime;
int GetReadWaitTime() {return STotalWaitTime._total;}
void ClearReadWaitTime() {STotalWaitTime._total = 0;}

#endif

void FileRequest::MarkAsUrgent() 
{
  // if the request is already in the worker thread, it could still be possible to move it forward
  if (_waiting)
  {
    // make sure the request is on top of the queue
    FS_SCOPE_LOCK_EXT(*_server); // why scope lock? We do not seem to be modifying the cache
    _priority = FileRequestPriority(0);
    _server->_queue.HeapUpdateUp(this);
  }
  else
  {
    // already in queue - just update the priority
    _priority = FileRequestPriority(0);
}
}

bool FileRequest::WaitUntilDone(int timeout)
{
  PROFILE_SCOPE_EX(fRdWD,*);

  // to reduce seeks, increase priority 
  MarkAsUrgent();
  
  // make sure the requests we depend upon are finished ASAP to prevent waiting on them
  for (int i=0; i<_depends.Size(); i++)
  {
    _depends[i]->MarkAsUrgent();
  }
  
  _server->MakeRequestUrgent(this);
  _server->_workerThread.VerifyRequestPosition(this);
  
  for (int i=0; i<_depends.Size(); i++)
  {
    _server->MakeRequestUrgent(_depends[i]);
    _server->_workerThread.VerifyRequestPosition(_depends[i]);
  }

  _server->_workerThread.VerifyRequestPosition(this);
  
  // submit any "urgent" requests - this will submit all we need and probably nothing else
  _server->SubmitQueuedRequests(FileRequestPriority(0),INT_MAX,false);

  //xxx
  _server->_workerThread.VerifyRequestPosition(this);
  
  // Request can be destroyed - we need to maintain RefCount
  bool ret = true;
  AddRef();
  if (_operation)
  {
    Ref<IOverlapped> op = _operation.GetRef();
    if (!op->IsDone())
    {
      if (!op->Wait(timeout))
        ret = false;
      // we are waiting, it is quite possible more info will be desired
      // record only when a substantial time was spent
      //if (PROFILE_SCOPE_NAME(fRdWD).TimeSpentNorm()>10)
      {
        if (PROFILE_SCOPE_NAME(fRdWD).IsActive())
        {
          RString logName = GetDebugName();
          // empty names means not inside of PBO - we are not interested then
          if (!logName.IsEmpty())
          {
            PROFILE_SCOPE_NAME(fRdWD).AddMoreInfo(logName);
          }
        }
      }
    }
    op->GetOwner()->Update();
  }

  Release();
  return ret;
}

/*!
If necessary, finishing processing may be performed during this function.
*/


bool FileRequest::IsDone()
{
  #define LOG_SCOPE(string) \
    if (PROFILE_SCOPE_NAME(fsDon).IsActive()) \
    { \
      RString logName = GetDebugName(); \
      /* empty names means not inside of PBO - we are not interested then */ \
      if (!logName.IsEmpty()) \
      { \
        PROFILE_SCOPE_NAME(fsDon).AddMoreInfo(RString(string)+logName); \
      } \
    }
  
  PROFILE_SCOPE_EX(fsDon,file);
  if (_waiting)
  {
    // make sure this request will become served
    _server->ProcessRequests();
    if (_waiting)
    {
      LOG_SCOPE("Waiting ");
      return false;
    }
  }
  if (_operation)
  {
    if (!_operation->IsDone())
    {
      LOG_SCOPE("Not done ");
      return false;
    }
  }
  // we are done only once all targets we depend on are done
  if (_targetsNotDone>0)
  {
    // targets not done - we need to let the server to update it
    _server->ProcessRequests();
    LOG_SCOPE("Not complete ");
    return false;
  }
  LOG_SCOPE("Done ");
  return true;
}

/*!
When destructing request, we need to wait until the request is finished
*/
FileRequest::~FileRequest()
{
  // the buffers are not needed any longer - unlock them
  Assert(!_waiting);
  //if (_fileLogical!=NULL && !_waiting)
  if (_fileLogical!=NULL)
  {
    _server->UnlockRequest(this);
  }
  // when the operation is not finished yet, it does not matter
  // we cannot verify locks now, as cache may be locked by other requests
  CloseHandles();

  {
    PROFILE_SCOPE_EX(fsFRD,*)
    _resultData.Clear();
}
}

void FileRequest::CloseHandles()
{
  if (_filePhysical)
  {
    if (_fileLogical != _filePhysical) _server->_handles.Close(_fileLogical,false);
    _server->_handles.Close(_filePhysical,false);
    _filePhysical = NULL;
    _fileLogical = NULL;
  }
}

void FileRequest::AddHandler(
  RequestableObject *object,
  RequestableObject::RequestContext *context
)
{
  RequestableObjectRequestInfo &item = _handlers.Append();
  item._object = object;
  item._context = context;
}

#if 0
class QIFileBuffersStream : public QIStream
{
protected:
  AutoArray<RefTgtBuffer> &_buffers;
  int _size;
  int _bufferSize;

public:
  QIFileBuffersStream(AutoArray<RefTgtBuffer> &buffers, int size, int bufferSize) : QIStream(NoBuffer), _buffers(buffers)
  {
    _size = size;
    _bufferSize = bufferSize;
    if (_buffers.Size() > 0) _buf = PosQIStreamBuffer(_buffers[0], 0, bufferSize);
  }
  ~QIFileBuffersStream(){_buf=PosQIStreamBuffer(NULL,0,0);}
  PosQIStreamBuffer ReadBuffer(PosQIStreamBuffer &buf, QFileSize pos); 
  bool PreloadBuffer
  (
    RequestableObject *obj, RequestableObject::RequestContext *ctx,
    FileRequestPriority priority, QFileSize pos, QFileSize size
  ) const {return true;}
  QFileSize GetDataSize() const {return _size;}
};

PosQIStreamBuffer QIFileBuffersStream::ReadBuffer(PosQIStreamBuffer &buf, QFileSize pos)
{
  int i = pos / _bufferSize;
  if (i >= _buffers.Size()) return PosQIStreamBuffer(new QIStreamBuffer,pos,0);
  return PosQIStreamBuffer(_buffers[i], i * _bufferSize, _bufferSize);
}

class QOFileBuffersStream : public QOStream
{
protected:
  RefArray<QIStreamBuffer> &_buffers;
  int _current;
  int _bufSize;

public:
  QOFileBuffersStream(RefArray<QIStreamBuffer> &buffers, int bufSize) : QOStream(NoBuffer), _buffers(buffers)
  {
    _current = 0;
    _bufSize = bufSize;
    if (_buffers.Size() > 0) _buf = _buffers[0];
  }
  ~QOFileBuffersStream(){_buf=NULL;}

protected:
  void Flush(int size);
  void SetFilePos(int pos);
};

void QOFileBuffersStream::Flush(int size)
{
  if (_bufferOffset >= (QFileSize)_bufSize)
  {
    DoAssert(_bufferOffset == _bufSize);
    _current++;
    if (_buffers.Size() > _current) _buf = _buffers[_current];
    else _buf = NULL;
    _bufferStart += _bufSize;
    _bufferOffset = 0;
    _bufferSize = 0;
  }
}

void QOFileBuffersStream::SetFilePos(int pos)
{
  int i = pos / _bufSize;
  if (i != _current)
  {
    _current = i;
    if (_buffers.Size() > i) _buf = _buffers[i];
    else _buf = NULL;
    _bufferStart = i * _bufSize;
    _bufferSize = 0;
  }
  _bufferOffset = pos - _bufferStart;
}

void FileRequest::Decode()
{
  Fail("Decode not implemented");
  #if 0
  QFileSize bufSize = GetPageRecommendedSize();

  // input stream
  QIFileBuffersStream in(_target, _sizePhysical, bufSize);

  // create new buffers
  QFileSize decodedSize;
  in.read(&decodedSize, sizeof(int));
  int nBuffers = (decodedSize + bufSize - 1) / bufSize;
  RefArray<QIStreamBuffer> newTarget;
  newTarget.Realloc(nBuffers);
  newTarget.Resize(nBuffers);
  for (int i=0; i<nBuffers; i++) newTarget[i] = new QIStreamBufferPage(bufSize);

  // output stream
  QOFileBuffersStream out(newTarget, bufSize);

  // decompression
  SSCompress compress;
  compress.Decode(out, decodedSize, in);
  DoAssert(out.tellp() == decodedSize);

  // update request
  DoAssert(decodedSize == _sizeLogical);
  _target = newTarget;
  #endif
}

#endif

/**
@param executeHandlers request handlers are executed instead of placing them into a queue
*/
void FileServerST::RequestDone(FileRequest *req, int rd, bool executeHandlers)
{
  PROFILE_SCOPE_EX(fsRDo,file)
  if (PROFILE_SCOPE_NAME(fsRDo).IsActive())
  {
    RString logName = req->GetDebugName();
    if (!logName.IsEmpty()) /* empty names means not inside of PBO - we are not interested then */
    {
      PROFILE_SCOPE_NAME(fsRDo).AddMoreInfo(logName);
    }
  }

#if _ENABLE_REPORT && _ENABLE_PERFLOG
  bool interesting = InterestedIn(req->_fileLogical,req->_fromLogical,req->_sizeLogical);
  if (interesting)
  {
    LogF(
      "Request done %s:%x:%x, rd %d",
      cc_cast(_handles.GetName(req->_fileLogical)), req->_fromLogical, req->_sizeLogical,
      rd
    );
  }
#endif
#if LOG_QUEUE>20
  LogF
  (
    "Queue: Request %p done %s:%x:%x (%s:%x:%x)",
    req,
    (const char *)_handles.GetName(req->_filePhysical), req->_fromPhysical, req->_sizePhysical,
    (const char *)_handles.GetName(req->_fileLogical), req->_fromLogical, req->_sizeLogical
  );
#endif
  // we were not able to handle waiting request, but we are now
  //Assert(!req->_waiting);

  QFileSize pageSize = _readScatterPageSize;
  QFileSize bufSize = GetPageRecommendedSize();
  int nPagesInBuf = bufSize/pageSize;
  
  if (!req->_waiting)
  {
    _nRequestsLoading--;
    _sizeRequestsLoading -=  req->_sizePhysical;

    Assert(_nRequestsLoading>=0);
    Assert(_nRequestsLoading>0 || _sizeRequestsLoading==0);

    if (req->_fileLogical != req->_filePhysical)
    {
      Fail("Decoding not supported");
      //req->Decode();
    }
    req->SetTargetValid();
  }
  else
  {
    // we should not be deleting the last reference here
    DoAssert(req->RefCounter()>1);
    _queueByteSize -= req->_sizePhysical;
    _queue.HeapDelete(req);
  }

  // if the request was waiting, it cannot succeed, it was canceled
  bool success = rd==req->_sizePhysical && !req->_waiting;
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    if (!success)
    {
      LogF(
        "Request failed - %s:%x:%x",
        (const char *)_handles.GetName(req->_fileLogical),req->_fromLogical,req->_sizeLogical
      );
    }
  #endif


  int i=0;
  int requestSize = req->_sizeLogical;

//LogF("*** Request done %s:%x:%x", (const char *)_handles.GetName(req->_filePhysical),req->_fromPhysical,req->_size);

  // while the request is waiting, there are no targets for it yet
  // but we still need to simulate storing them to make sure related cleanup is performed
  if (!req->_waiting)
  {
    QFileSize fromOffset = 0;

#if _ENABLE_REPORT && _ENABLE_PERFLOG
    if (interesting)
    {
      LogF(
        "Request storing %s:%x:%x, targets %d",
        cc_cast(_handles.GetName(req->_fileLogical)), req->_fromLogical, req->_sizeLogical,
        req->_target.Size()
      );
    }
#endif
  
    // we cannot complete more targets than the total number of not completed
    Assert(req->_target.Size()>0);
    while (requestSize>0)
    {
      TgtBuffer *buffer = req->_target[i];  

      int offset = 0;
      int toRead = bufSize;
      if (toRead>requestSize) toRead = requestSize;

      int totalRead = toRead;
      for (int j=0; j<nPagesInBuf && toRead>0; j++)
      {
        int read = pageSize;
        if (read>toRead) read = toRead;
        buffer->DataUnlock(offset,read);
        offset += read;
        toRead -= read;
        requestSize -= read;
      }
      // forget early what we will copy from DVD to HDD
      bool storeLast = req->_store.RawSize()>0 && !req->_store[i];
      // TODO: if not in preferred region, store last
      Store(
        buffer,req->_fileLogical,req->_fromLogical+fromOffset,totalRead,
        executeHandlers,success,storeLast,req
      );
      
      fromOffset += bufSize;

      i++;
    }
    Assert(requestSize==0);
  }
  else
  {
    QFileSize fromOffset = 0;
    // cleaning up canceled request
    while (requestSize>0)
    {
      int toRead = bufSize;
      if (toRead>requestSize) toRead = requestSize;
      int totalRead = toRead;
      
      requestSize -= totalRead;
      // forget early what we will copy from DVD to HDD
      // TODO: if not in preferred region, store last
      Store(
        NULL,req->_fileLogical,req->_fromLogical+fromOffset,totalRead,
        executeHandlers,false,false,req
      );
      
      fromOffset += bufSize;

      i++;
    }
  }


  // request should be notified it is done from the Store function
  // part of the request may be redirected to other requests
  Assert(req->_targetsNotDone>=0);
  if (req->_targetsNotDone==0 && req->_handlers.Size()>0)
  {
    // this should happen only for requests which do not contribute to cache (copy requests)
    // under rare this may happed also for PreloadDVD requests (cached.txt)
    //Assert(req->_store.RawSize()>0 && req->_store.IsEmpty());
    Assert(req->_store.RawSize()>0);
    req->ExecuteHandlers(executeHandlers,success);
  }
}

void FileServerST::RequestRepeat(FileRequest *req)
{
  // request should be currently pending
  // when we remove it from pending, we can remove last reference
  RefR<FileRequest> lock = req;
  req->_waiting = true;
  
  _nRequestsLoading--;
  _sizeRequestsLoading -=  req->_sizePhysical;

  SubmitRequest(req);
}



#if _ENABLE_REPORT && _ENABLE_PERFLOG

LogFileServerActivity::LogFileServerActivity()
{
  _closed = true;
}

LogFileServerActivity::~LogFileServerActivity()
{
  Close();
}

void LogFileServerActivity::Open(const char *name)
{
  if (!_closed) Close();
  _log.Open(name);
  _closed = false;
}

void LogFileServerActivity::Close()
{
  if (_closed) return;
  Flush();
  _log.Close();
  _closed = true;
}

void LogFileServerActivity::Flush()
{
  if (_file.GetLength() == 0) return;
  _log.PrintF("%s:%x:%x\n",(const char *)_file,_start,_size);
  _file = "";
}

void LogFileServerActivity::DoLog(RString file, int start, int size)
{
  if (_closed) return;
  if (strcmpi(_file,file) || start!=_start+_size)
  {
    Flush();
    _file = file;
    _start = start;
    _size = size;
  }
  else
  {
    _size += size;
  }
}

#endif

#if !POSIX_FILES_COMMON || defined(_WIN32)
TypeIsSimple(PO_FILE_SEGMENT_ELEMENT)
#endif

void FileServerST::PreloadDVD(RString name)
{
  // TODO: make PreloadDVD compatible with async file server
}

RString FullXBoxName(RString name);

void FileServerST::Preload(RString logFile)
{
  // preload the input file in one go
  Request(FileRequestPriority(1), logFile);
  // parse given input file and preload what is found there
  QIFStreamB in;
  in.AutoOpen(logFile);
  if (in.fail() || in.eof())
  {
    // if no log exists, create it
    //GLogFileServer = true;
    return;
  }
  int lines = 0;
  int priority = 100;
  for (;;)
  {
    char buf[1024];
    if (!in.readLine(buf,sizeof(buf)))
    {
      break;
    }
    if (buf[0]==0 || buf[0]=='#')
    {
      continue;
    }
    lines++;
    char file[sizeof(buf)];
    QFileSize start,size;
    // parse a request
    int scanLen = sscanf(buf,"%[^:]:%x:%x",file,&start,&size);

    if (scanLen==3)
    {
      // check real file size
      const char *fileName = file;
      QFileSize realSize = QFBankQueryFunctions::GetFileSize(fileName);
      if (size>realSize) size = realSize;
      if (_queueByteSize+_sizeRequestsLoading+size>(QFileSize)_maxSize)
      {
        LogF("Max. preload capacity reached");
        break;
      }
      if (size>0)
      {
        Preload(FileRequestPriority(priority),fileName,start,size);
        // make sure request stay sorted correctly
        priority++;
      }
    }
  }
  if (lines==0)
  {
    // if no log exists, create it
    //GLogFileServer = true;
    return;
  }
  // make sure some processing is started
  ProcessRequests();
}

/*!
caller needs to make sure that both start and size are with file size limits and properly aligned
\return never returns NULL
*/

RefR<FileRequest> FileServerST::RequestUncached(
  FileRequest *req, RequestableObject *obj, RequestableObject::RequestContext *ctx,
  FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
)
{
  PROFILE_SCOPE_EX(fsRUC,file);
  // as the requests are cached, be sure to align them so that the cache is reusable
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    QFileSize bufSize = GetPageRecommendedSize();
    int start0 = start;
    int size0 = size;    
    QFileSize maxSize = _handles.GetSize(handle);
    start &= ~(bufSize-1);
    int align = start0 - start;
    size = (size+align+bufSize-1)&~(bufSize-1);
    if (size>maxSize-start) size = maxSize-start;
    if (start0!=start || size0!=size)
    {
      Fail("Uncached request should already be aligned");
      start = start0;
      size = size0;
    }
  #endif

  RefR<FileRequest> request = AddRequest(req, handle, handle, priority, start, start, size, size, obj, ctx);  // (physical == logical)
  Assert(request->_resultData.Size()>0);
  
  if (PROFILE_SCOPE_NAME(fsRUC).IsActive())
  {
    RString logName = request->GetDebugName();
    // empty names means not inside of PBO - we are not interested then
    if (!logName.IsEmpty())
    {
      PROFILE_SCOPE_NAME(fsRUC).AddMoreInfo(logName);
    }
  }
  
  return request.GetRef();
}

void FileServerST::AttachToCache(FileRequest *req, FileServerHandle fileLogical, QFileSize fromLogical, QFileSize sizeLogical) 
{
  QFileSize bufSize = GetPageRecommendedSize();
  // attach for notification
  for (QFileSize offset=fromLogical; offset<fromLogical+sizeLogical; offset+=bufSize)
  {
    int sizeRest = fromLogical+sizeLogical-offset;
    int size = intMin(bufSize,sizeRest);
    FileInCache *inCache = Find(fileLogical,offset,size);
    if (!inCache) break;
    req->SetResultItem(inCache);
    if (inCache->_buffer.IsNull())
    {
      inCache->AttachRequest(req);
    }
  }
}

void FileServerST::LockRequest(FileRequest *req)
{
  FS_SCOPE_LOCK();
  
  QFileSize bufSize = GetPageRecommendedSize();
  // lock all corresponding buffers found in the cache
  QFileSize lockBeg = std::min(req->_fromLogical,req->_neededBeg);
  QFileSize lockEnd = std::max(req->_fromLogical+req->_sizeLogical,req->_neededEnd);
  
  QFileSize offset;
  int i;
  for (offset=lockBeg,i=0; offset<lockEnd; offset+=bufSize,i++)
  {
    int sizeRest = lockEnd-offset;
    int size = intMin(bufSize,sizeRest);
    // whole request needs to be locked during its existence
    FileInCache *inCache= Find(req->_fileLogical,offset,size);
    // cache item is sure to exist now
    // avoid releasing it until the request is done
    inCache->Lock(req);
    req->_lockPages++;
  }
  req->_lockCount++;
}

/** The request is no longer needed - we can unlock all corresponding buffers found in the cache */

void FileServerST::UnlockRequest(FileRequest *req)
{
  AssertSameThread(_requestingThread);
  if (req->_lockCount>0)
  {
    FS_SCOPE_LOCK();
    
    QFileSize bufSize = GetPageRecommendedSize();
    
    QFileSize lockBeg = std::min(req->_fromLogical,req->_neededBeg);
    QFileSize lockEnd = std::max(req->_fromLogical+req->_sizeLogical,req->_neededEnd);
  
    int i;
    QFileSize offset;
    for (offset=lockBeg,i=0; offset<lockEnd; offset+=bufSize,i++)
    {
      int sizeRest = lockEnd-offset;
      int size = intMin(bufSize,sizeRest);
      FileInCache *inCache= Find(req->_fileLogical,offset,size);
      // handle NULL - if the request failed, the item might not exist
      if (inCache)
      {
        // we avoid releasing the memory until the request is done
        inCache->Unlock(req);
      }
      req->_lockPages--;
    }
    // nothing is locked any more
    req->_lockCount--;
    Assert(req->_lockCount>0 || req->_lockPages==0);
  }
}


/// manage locked sectors
/**
Keeps track of which sectors in given range are locked and should be unlocked
*/
class FileRequestLock
{
	int _lockedStorage[64];
  QFileSize _lockBeg;
  QFileSize _lockEnd;
  int _bufSize;
  /// bits addressed relative to _lockBeg
  PackedBoolAutoArrayT<MemAllocSA> _locked;
  
  public:
  FileRequestLock(QFileSize start, QFileSize size, int pageSize)
  {
    _lockBeg = start;
    _lockEnd = start+size;
    _bufSize = pageSize;
    _locked.SetStorage(MemAllocSA(_lockedStorage,sizeof(_lockedStorage)));
    _locked.Init((size+pageSize-1)/pageSize);
  }
  ~FileRequestLock()
  {
    // when object goes out of scope, all locks should be released
    Assert(IsEmpty());
  }
  /// convert offset to index inside of _locked
  int GetIndex(QFileSize offset) const
  {
    Assert(offset >= _lockBeg);
    Assert(offset < _lockEnd);
    return (offset-_lockBeg)/_bufSize;
  }
  void Init(int nBits) {_locked.Init(nBits);}
  /// addressed by index (use GetIndex)
  bool Lock(int index)
  {
    // if it is already locked, do not lock again
    if (_locked.Get(index))
    {
      return false;
    }
    _locked.Set(index,true);
    return true;
  }
  /// addressed by index (use GetIndex)
  bool Unlock(int index)
  {
    // unlock only once
    bool ret = _locked.Get(index);
    _locked.Set(index,false);
    return ret;
  }
  bool IsEmpty() const {return _locked.IsEmpty();}
  /// unlock all buffers recorded as locked
  void UnlockAll(FileServerST *server, FileServerHandle handle, FileRequest *req);
};

void FileRequestLock::UnlockAll(FileServerST *server, FileServerHandle handle, FileRequest *req)
{
  if (IsEmpty()) return;
  FS_SCOPE_LOCK_EXT(*server);
  int i=0;
  for (QFileSize offset=_lockBeg; offset<_lockEnd; offset+=_bufSize, i++)
  {
    Assert(GetIndex(offset)==i);
    if (Unlock(i))
    {
      QFileSize rest = _lockEnd-offset;
      QFileSize itemSize = _bufSize;
      if (itemSize>rest) itemSize = rest;
      FileServerST::FileInCache *item = server->Find(handle,offset,itemSize);
      item->Unlock(req);
    }
  }
  Assert(IsEmpty());
}

FileRequest *FileServerST::CreateDummyRequest(
  FileServerHandle fileLogical, FileRequestPriority priority, QFileSize fromLogical, QFileSize sizeLogical
)
{
  FileRequest *req = new FileRequest;
  req->_targetsNotDone = 0;
  req->_server = this;
  req->_neededBeg = fromLogical;
  req->_neededEnd = fromLogical+sizeLogical;

  req->_filePhysical = fileLogical;
  req->_fromPhysical = 0;
  req->_sizePhysical = 0;
  req->_fileLogical = fileLogical;
  req->_fromLogical = fromLogical;
  req->_sizeLogical = 0;
  req->_priority = priority;
  req->_timeStarted = GetTickCount();
  req->_waiting = false;
  req->_lockCount = 0;
  req->_lockPages = 0;

  _handles.Open(fileLogical,false);
  
  QFileSize bufSize = GetPageRecommendedSize();
  req->_resultData.Realloc((sizeLogical+bufSize-1)/bufSize);
  req->_resultData.Resize((sizeLogical+bufSize-1)/bufSize);
  return req;
}

RefR<FileRequest> FileServerST::AddRequest(
  FileRequest *req, FileServerHandle fileLogical, FileServerHandle filePhysical,
  FileRequestPriority priority,
  QFileSize fromLogical, QFileSize fromPhysical,
  QFileSize sizeLogical, QFileSize sizePhysical,
  RequestableObject *obj, RequestableObject::RequestContext *ctx,
  const PackedBoolAutoArray &store
)
{
  AssertSameThread(_requestingThread);
  PROFILE_SCOPE_EX(fsAdR,file);
  // If the request is very large, it can make sense to separate it into several requests
  // and cache can be filled gradually.
  // The difficulty is that this would require a "composite request"
  // which would be signalled once all small requests are finished.
  // As an alternative solution QIFStream::PreRead handles this.
  
  // you need to allocate one buffer for each page
  // you can use ReadFileScatter() to read all buffers at once
  QFileSize bufSize = GetPageRecommendedSize();
  
  Assert(sizeLogical>0); // zero sized requests should already be handled on higher levels
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  bool interesting = InterestedIn(fileLogical,fromLogical,sizeLogical);
  if (interesting)
  {
    LogF(
      "Request %s:%x:%x (%s)",
      cc_cast(_handles.GetName(fileLogical)), fromLogical, sizeLogical,
      cc_cast(QFBank::FileOnOffset(fileLogical,fromLogical,fromLogical+sizeLogical))
    );
  }
#endif

#if LOG_QUEUE>30
  LogF
  (
    "Queue: Request %p add %x:%x:%x",
    req, fileLogical, fromLogical, sizeLogical
  );
#endif
  
  QFileSize fromLogical0 = fromLogical;
  QFileSize sizeLogical0 = sizeLogical;
  
  // no optimization for physical request - there done intentionally large, when done at all
  // moreover, physical requests may be unaligned
  // if request has cache store table, we need to keep it intact
  if (filePhysical==fileLogical && store.RawSize()==0)
  {
    FS_SCOPE_LOCK();
    
    // TODO: consider merging this with a code searching for cached data
    
    // we want to skip parts of the request which are already handled by other requests
    // scan matching beginning
  
    for (QFileSize offset=fromLogical; offset<fromLogical+sizeLogical; offset+=bufSize)
    {
      int sizeRest = fromLogical+sizeLogical-offset;
      int size = intMin(bufSize,sizeRest);
      FileInCache *inCache = Find(fileLogical,offset,size);
      if (!inCache) break;
      #if 0 // _ENABLE_REPORT && _ENABLE_PERFLOG
        if (interesting)
        {
          LogF("Request overlapped %s:%x:%x", (const char *)_handles.GetName(fileLogical), fromLogical, sizeLogical);
        }
      #endif
      // does match - skip it
      fromPhysical += size, fromLogical += size;
      sizePhysical -= size, sizeLogical -= size;
        }
    // note: the request may be in fact empty now - this is not effective and should be handled by other means
    if (sizeLogical==0)
    {
      // if the request is fully covered by another request, we may return the existing one
      for (QFileSize offset=fromLogical0; offset<fromLogical0+sizeLogical0; offset+=bufSize)
      {
        int sizeRest = fromLogical0+sizeLogical0-offset;
        int size = intMin(bufSize,sizeRest);
        FileInCache *inCache = Find(fileLogical,offset,size);
        // inCache is sure to be found - it was already found before
        // check if any request fully covers our request
        for (int i=0; i<inCache->_request.Size(); i++)
        {
          FileRequest *reqOld = inCache->_request[i];
          if (reqOld->InNeeded(req->_neededBeg,req->_neededEnd-req->_neededBeg) && reqOld->Contains(fileLogical,fromLogical0,sizeLogical0))
          {
            // we have a substitute - we will return it
            RefR<FileRequest> subst = reqOld;
            if (subst->_priority > priority)
            {
              // we need to change the priority key
              subst->_priority = priority;
              _queue.HeapUpdateUp(subst);
            }
            if (obj) subst->AddHandler(obj,ctx);
            
            // the substitute request is already locked
            // it will be unlocked when it is destructed
            
            #if _ENABLE_REPORT && _ENABLE_PERFLOG
              if (interesting)
              {
                LogF(
                  "Request covered %s:%x:%x",
                  cc_cast(_handles.GetName(reqOld->_fileLogical)), reqOld->_fromLogical, reqOld->_sizeLogical
                );
              }
            #endif
            // req will be destroyed now
            return subst.GetRef();
          }
        }
      }


      // no substitute - we need to create an empty request
      // we have the request - it is empty, and it is not inserted into any queue
      // many members are not initialized, as the request is not fully created
      // it will be notified by other requests once respective shared buffers are filled
      
      Assert(sizeLogical==0);
      // if guarantees both handles are the same
      Assert(filePhysical==fileLogical);

      AttachToCache(req, fileLogical, fromLogical0, sizeLogical0);

      LockRequest(req);
      
      #if _ENABLE_REPORT && _ENABLE_PERFLOG
        if (interesting)
        {
          LogF("Request shared %s:%x:%x", (const char *)_handles.GetName(fileLogical), fromLogical, sizeLogical);
        }
      #endif
      if (obj) req->AddHandler(obj,ctx);

      return req;
      
    }

    AttachToCache(req, fileLogical, fromLogical0, sizeLogical0);

    QFileSize endLogical = fromLogical+sizeLogical;
    // check page aligned end
    //QFileSize endLogicalAligned = (endLogical)&~(bufSize-1);
    // version above is naive and broken
    // we need to: 1) align towards the end, 2) compute offset of the start of the last page
    QFileSize endLogicalAligned = ((endLogical+bufSize-1)&~(bufSize-1))-bufSize;
    
    // scan matching end
    for (QFileSize offset = endLogicalAligned; offset>=fromLogical; offset-=bufSize)
    {
      int sizeRest = endLogical-offset;
      int size = intMin(bufSize,sizeRest);
      FileInCache *inCache = Find(fileLogical,offset,size);
      if (!inCache) break;
      #if _ENABLE_REPORT && _ENABLE_PERFLOG
        if (interesting)
        {
          LogF("Request overlapped %s:%x:%x", (const char *)_handles.GetName(fileLogical), fromLogical, sizeLogical);
        }
      #endif
      // does match - skip it
      sizePhysical -= size, sizeLogical -= size;
      if (inCache->_buffer.IsNull())
      {
        inCache->AttachRequest(req);
      }

      // this part was not processed yet
      req->SetResultItem(inCache);
    }
    // if sizeLogical is zero now, it should already be zero after the first loop - which it was not
    Assert(sizeLogical>0);
    // if there whould be any store mask, it is broken now - however mask is used only for DVD requests
    Assert(store.RawSize()==0);

  }

//  LogF(
//    "Queue: Request added %s:%x:%x",
//    (const char* )_handles.GetName(fileLogical), fromLogical, sizeLogical
//  );

#if LOG_QUEUE>30
  LogF
  (
    "Queue: Request %p added %x:%x:%x",
    req, fileLogical, fromLogical, sizeLogical
  );
#endif
  // sanity check: we do not expect huge requests, such request is probably a result of bug (overflow)
  Assert(sizePhysical<50*1024*1024);
  Assert(sizeLogical<50*1024*1024);
  
  // make sure file handle stays open until request is finished
  if (filePhysical!=fileLogical) _handles.Open(filePhysical,false);

  req->_filePhysical = filePhysical;
  req->_fromPhysical = fromPhysical;
  req->_sizePhysical = sizePhysical;
  Assert(req->_fileLogical == fileLogical);
  req->_fromLogical = fromLogical;
  req->_sizeLogical = sizeLogical;
  req->_priority = priority;
  req->_timeStarted = GetTickCount();
  req->_waiting = true;
  DoAssert(req->_lockCount==0);

  // when reading from DVD, request can change (whole pages are to read)
  _handles.CheckRequest(req); // change _from, _size

  DoAssert(req->_sizePhysical>0);
  DoAssert(req->_sizeLogical>0);
  
  // we need to set number of targets not done even before the request is submitted
  Assert(req->_targetsNotDone>=0);

  req->_store = store;

  int i;
  QFileSize offset;
  // create a "cached item" for each potential buffer in the request
  for (offset=req->_fromLogical,i=0; offset<req->_fromLogical+req->_sizeLogical; offset+=bufSize,i++)
  {
    int sizeRest = req->_fromLogical+req->_sizeLogical-offset;
    int size = intMin(bufSize,sizeRest);
    // ignore pages which will not be stored - they will not notify us
    // each page needs to be stored in the cache
    bool inNeededRange = offset>=fromLogical0 && offset<fromLogical0+sizeLogical0;
    FileInCache *inCache= StoreEmpty(req->_fileLogical,offset,size,!inNeededRange);
    req->SetResultItem(inCache);
    // it may be already stored and nonempty, because of some overlapping requests
    // only try to lock what is a part of the original request region
    if (inCache->_buffer.IsNull())
    {
      // if it is empty, we will be notified once it is done
      req->_targetsNotDone++;
      Assert(inCache->_request.FindKey(req)<0);
      inCache->_request.Add(req);
    }
  }

  LockRequest(req);
  // we are locked, we may unlock the temporary lock
 
#if !_SUPER_RELEASE
#if _ENABLE_REPORT && _ENABLE_PERFLOG
  if (GLogFileServer)
  {
    // log any file operations - good for preloading
    _logFiles.DoLog(_handles.GetName(req->_fileLogical), req->_fromLogical, req->_sizeLogical);
  }
#endif 
#endif

  // create a request and put it into the queue

  if (obj) req->AddHandler(obj,ctx);

  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    if (interesting)
    {
      LogF(
        "Request nonempty %s:%x:%x",
        cc_cast(_handles.GetName(fileLogical)), fromLogical, sizeLogical
      );
    }
  #endif
  #if LOG_QUEUE>20
  LogF(
    "Queue: Add %s:%x:%x (%s:%x:%x)",
    (const char *)_handles.GetName(req->_filePhysical),req->_fromPhysical,req->_size,
    (const char *)_handles.GetName(req->_fileLogical),req->_fromLogical,req->_size
  );
  #endif
    
#if DEBUG_PRELOADING
  if (obj && obj->DebugMe())
  {
    RptF("   calling _queue.HeapInsert");
  }
#endif
  _queue.HeapInsert(req);
  _queueByteSize += req->_sizePhysical;
  // if there are many waiting requests, check if there are some urgent
  if (_queue.Size()>50 || priority<1)
  {
    // submit urgent entries
    SubmitQueuedRequests(FileRequestPriority(500));
  }
  
  // request may be in any state now (waiting, being processed, finished)
  return req;
}

void FileServerST::FileInCache::AttachRequest(FileRequest *req)
{
  Assert(_request.FindKey(req)<0);
  Assert(_request.Size()>0);
  for (int ri=0; ri<_request.Size(); ri++)
  {
    // any request which will fill the buffer will do
    // we can add all of them, so that all of them can notify us
    // in a typical scenario there will be only one anyway
    req->_depends.AddUnique(_request[ri]);
  }
  _request.Add(req);
  req->_targetsNotDone++;
}

FileRequestHandle FileServerST::Request(
  RequestableObject *obj, RequestableObject::RequestContext *ctx,
  FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
)
{
  AssertSameThread(_requestingThread);
  //_requestingThread;
  // TODO: thread id of the requesting thread
  
  PROFILE_SCOPE_EX(fsReq,file);
  if (size==0 /*|| !UseReadScatter*/)
  {
    if (obj) ExecuteHandler(true,ctx,obj,true);
    return NULL;
  }
#if DEBUG_PRELOADING
  if (obj && obj->DebugMe())
  {
    RptF("FileServerST::Request file %s", cc_cast(_handles.GetName(handle)));
  }
#endif
  
  QFileSize bufSize = GetPageRecommendedSize();
  // as the requests are cached, be sure to align them so that the cache is reusable
  QFileSize start0 = start;
  QFileSize size0 = size;(void)size0;
  QFileSize maxSize = _handles.GetSize(handle);
  if (start>=maxSize)
  {
    // reading over EOF - nothing to read
    if (obj) ExecuteHandler(true,ctx,obj,true);
    return NULL;
  }
  start &= ~(bufSize-1);
  int align = start0 - start;
  size = (size+align+bufSize-1)&~(bufSize-1);
  Assert(start<=start0 && start+size>=start0+size0);
  if (size>maxSize-start) size = maxSize-start;
  
  if (PROFILE_SCOPE_NAME(fsReq).IsActive())
  {
    RString logName = GetReqDebugName(handle,start,size,priority);
    // empty names means not inside of PBO - we are not interested then
    if (!logName.IsEmpty())
    {
      PROFILE_SCOPE_NAME(fsReq).AddMoreInfo(logName);
    }
  }
  
//  LogF(
//    "Request %s:%x:%x (unaligned %x:%x)",
//    (const char* )_handles.GetName(handle),start,size,start0,size0
//  );

#if 0 //_ENABLE_REPORT && _ENABLE_PERFLOG
  bool interesting = InterestedIn(handle,start,size);
#endif
#if 0 //_ENABLE_REPORT && _ENABLE_PERFLOG
  if (interesting)
  {
    LogF(
      "Request %s:%x:%x aligned to %x:%x",
      (const char* )_handles.GetName(handle),start0,size0,start,size
    );
  }
#endif
#ifndef _SUPER_RELEASE
#if _ENABLE_REPORT && _ENABLE_PERFLOG

  if (GLogDVD)
  {
    LogDVD.UpdateRequests(_handles.GetName(handle), start, size);
  }
#endif  
#endif  

  RefR<FileRequest> req = CreateDummyRequest(handle,priority,start,size);
  
  RefR<FileRequest> reqLocked = req;
  FileRequestLock locked(start,size,bufSize);
  
  {
    // make sure nothing is evicted from the cache while creating the request
    FS_SCOPE_LOCK();

    // part of the request may already be cached
    // skip cached beginning (if any)
    while(size>0)
    {
      QFileSize itemSize = bufSize;
      if (itemSize>size) itemSize = size;
      FileInCache *item = Find(handle,start,itemSize);
      if (!item || item->_buffer.IsNull()) break;
#if 0 // _ENABLE_REPORT && _ENABLE_PERFLOG
      if (interesting)
      {
        LogF(
          "Request part cached %s:%x:%x",
          (const char *)_handles.GetName(handle),start,itemSize
          );
      }
#endif
      int resIndex = locked.GetIndex(start);
      if (locked.Lock(resIndex))
      {
        item->Lock(req);
        //Log("Lock %x:%d",lockBeg,(start-lockBeg)/bufSize);
      }

      req->_resultData[resIndex] = item;
      
      MoveToFront(item);
      start += itemSize;
      size -= itemSize;
    }
    // skip cached end (if any)
    while(size>0)
    {
      // int itemSize = bufSize&(bufSize-1));
      // if (itemSize==0) itemSize = bufSize;
      // optimized
      QFileSize itemSize = bufSize-((bufSize-size)&(bufSize-1));
      FileInCache *item = Find(handle,start+size-itemSize,itemSize);
      if (!item || item->_buffer.IsNull()) break;
#if 0 // _ENABLE_REPORT && _ENABLE_PERFLOG
      if (interesting)
      {
        LogF(
          "Request part cached %s:%x:%x",
          (const char *)_handles.GetName(handle),start+size-itemSize,itemSize
          );
      }
#endif
      // we need to lock the item, so that it is not discarded during the request processing
      int resIndex = locked.GetIndex(start+size-itemSize);
      if (locked.Lock(resIndex))
      {
        item->Lock(req);
        //Log("Lock %x:%d",lockBeg,(start+size-itemSize-lockBeg)/bufSize);
      }

      req->_resultData[resIndex] = item;

      MoveToFront(item);
      size -= itemSize;
    }
  }
  
  if (size<=0)
  {
#if 1
    
    LockRequest(req); // and lock them
#endif
    
    #if 0 //_ENABLE_REPORT && _ENABLE_PERFLOG
      if (interesting)
      {
        LogF(
          "Request cached %s:%x:%x",
          (const char *)_handles.GetName(handle),start,size
        );
      }
    #endif
    // no request necessary - data already cached
    if (obj)
    {
      ExecuteHandler(true,ctx,obj,true);
    }
    #if 1 // hotfix for performance problem  news:hm3585$th0$1@new-server.localdomain
    if (CheckMainThread(_requestingThread))
    {
    locked.UnlockAll(this,handle,reqLocked);
    return NULL;
    }
    #else
    // make the request empty
    //UnlockRequest(req);
    //req->_neededEnd = req->_neededBeg;
    // make the request data empty
    //req->_resultData.Clear();
    #endif
  }
  else
  {
    req = RequestUncached(req,obj,ctx,priority,handle,start,size);
  }

  
  // unlock what was locked before
  locked.UnlockAll(this,handle,reqLocked);
  
  return req.GetRef();

}

void FileServerST::ExecuteHandler(bool executeNow, RequestableObject::RequestContext *ctx, RequestableObject *obj, bool success) 
{
  RequestableObject::RequestResult res = success ? RequestableObject::RRSuccess : RequestableObject::RRFailed;
  int threadId = obj->ProcessingThreadId();
  if (executeNow && (threadId==-1 || threadId==0 && CheckMainThread(_requestingThread)) || obj->ProcessImmediately())
  {
    obj->RequestDone(ctx,res);
  }
  else
  {
    StoreHandler(threadId,ctx,obj,res);
  }
}


/*!
@param urgent used when submitting a request which we will wait for (blocking)

req must be in _queue before this call
after the call it can be in _pending (_context != NULL)
or it can be finished (_context == NULL)
In both cases _waiting will be false
*/

void FileServerST::SubmitRequest(FileRequest *req, bool urgent)
{
  if (req->_sizePhysical==0)
  {
    // empty request - no processing needed
    req->_waiting = false;

    // handle open with AddRequest - will be closed once req is destroyed

    return;
  }
  PROFILE_SCOPE_EX(fsSuR,file);

  if (PROFILE_SCOPE_NAME(fsSuR).IsActive())
  {
    RString logName = req->GetDebugName();
    if (!logName.IsEmpty()) /* empty names means not inside of PBO - we are not interested then */
    {
      PROFILE_SCOPE_NAME(fsSuR).AddMoreInfo(logName);
    }
  }
    
  Assert(req->_waiting);
  req->_waiting = false;
  
  QFileSize bufSize = GetPageRecommendedSize();
  DoAssert((bufSize&(_readScatterPageSize-1))==0);

  // create buffers

  int targetCount = (req->_sizePhysical+bufSize-1)/bufSize;
  // use bigger of _readScatterPageSize and GetPageRecommendedSize()
  // and split big buffers into several smaller ones if necessary
  {
    //PROFILE_SCOPE_EX(fsSCT,file);
    Assert(targetCount>0);
#if USE_MEM_STORE
    // make sure we have a space for the allocation
    // when the store is full, we need to release something from the store,
    // which means releasing something from the cache
    int toFree = targetCount-GMemStore.GetFreePageCount();
    while (--toFree>=0)
    {
      if (FreeOneItem()==0) break;
    }
#endif
    // note: we assume here each cache item released means one page released from the store
    // However, some pages might perhaps be shared with some existing requests.
    // This should be very rare, but it might be possible

    req->_target.Realloc(targetCount);
    for (int i=0; i<targetCount; i++)
    {
      Retry:
      #if USE_FILE_MAPPING
      // TODO: optimize
      int left = req->_sizePhysical-i*bufSize;
      int size = intMin(left,bufSize);
      RefTgtBuffer buf = new TgtBuffer(_handles.Mapping(req->_filePhysical),req->_fromPhysical+bufSize*i,size);
      #else
      RefTgtBuffer buf = new TgtBuffer();
      #endif
      if (buf->GetSize()==0)
      {
        // we know there should be still a space left in the store,
        // but chance is we are out of physical or virtual memory
        if (FreeOnDemandSystemMemoryLowLevel(2*GetPageRecommendedSize())>0)
        {
          // before giving up, retry as many times as possible
          // there can be nothing worse than a fatal error
          goto Retry;
        }
        else
        {
          // while we are not of of low-level memory (there is no low-level memory on PC), we may be out of "store" memory
          if (FreeOneItem()>0)
          {
            goto Retry;
          }
          // no recovery possible - we have no other option that to fail
          ErrorMessage("Fatal error: Out of memory in the file operation");
        }
      }
      req->_target.Add(buf);
    }
  }
  //Assert(req->_targetsNotDone>=targetCount);
  //Assert(targetI==targetCount);

   // create handler for copying from DVD to cache
  _handles.UpdateRequest(req);

  _nRequestsLoading++;
  _sizeRequestsLoading +=  req->_sizePhysical;

  //PROFILE_SCOPE_EX(fRdSc,file);

  BOOL ok = _handles.Read(req,urgent);
  // ok is usually false. This is expected, but we need to check everything is OK
  if (ok)
  {
    // completed immediately - strange, but possible
    // in this case there is no need to create context
    DWORD rd = req->_sizePhysical;
    // execute all handlers immediately
    
    RequestDone(req, rd);
  }
}

void FileServerST::MakeRequestUrgent(FileRequest *req)
{
  _workerThread.MakeRequestUrgent(req);
}

/*
@param submitAll when true, no request are forced to be delayed. This is used for block preloading, via SubmitRequests
*/
void FileServerST::SubmitQueuedRequests(FileRequestPriority maxPriority, int maxCount, bool submitAll)
{
  AssertSameThread(_requestingThread);
  //PROFILE_SCOPE_EX(fsSQR,file);
  // if there are little request pending now, we may process some of them
  int maxRequests = CheckMainThread(_requestingThread) ? 64 : 2;
  // when we work asynchronously, there is no need to keep the worker thread queue long
  // as we will be submitting into it in a timely manner
  while ((_nRequestsLoading<=maxRequests && _sizeRequestsLoading<=512*1024 || maxPriority==0) && --maxCount>=0)
  {
    RefR<FileRequest> req;
    if (!_queue.HeapGetFirst(req)) break;
    // do not submit request with a priority over given limit
    if (req->_priority > maxPriority) break;
    // make sure request with priority>10000 wait a while until submitted
    if (
      !submitAll &&
      req->_priority > FileRequestPriority(10000) && req->_timeStarted+1000>GetTickCount()
    )
    {
      break;
    }
    _queue.HeapRemoveFirst();
    _queueByteSize -= req->_sizePhysical;
    #if 0
    const char *name = _handles.GetName(req->_filePhysical);
    if (submitAll)
    {
      LogF("Submit req %s:%x:%x (prior %d)",cc_cast(name),req->_fromPhysical,req->_sizePhysical,req->_priority);
    }
    #endif
    SubmitRequest(req,req->_priority==0);
  }
}

void FileServerST::CheckPendingRequests()
{
}

void FileServerST::ProcessRequests(int maxCount, bool submitAll)
{
  AssertSameThread(_requestingThread);
  //PROFILE_SCOPE_EX(fsPRq,*);
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
  static DWORD lastTime = ::GlobalTickCount();
  DWORD time = ::GlobalTickCount();
    
    #if 0 // !_DEBUG
      if (time-lastTime>500)
      {
        LogF("FileServerST::ProcessRequests may be starving: %d (%d requests pending)",time-lastTime,_workerThread.Size());
      }
    #endif
    lastTime = time;
  #endif
  // process waiting queue requests
  #if 0
  if (_sizeRequestsLoading>0)
  {
    DIAG_MESSAGE(
      500,
      Format(
        "Files pending %d %s, queued %d %s",
        _nRequestsLoading,(const char *)FormatByteSize(_sizeRequestsLoading),
        _queue.Size(),(const char *)FormatByteSize(_queueByteSize)
      )
    );
  }
  #endif

  _workerThread.Update();

  CheckPendingRequests();
  
  SubmitQueuedRequests(FileRequestPriority(INT_MAX),maxCount,submitAll);

  bool IsCopyFromDVDEnabled();
}

void FileServerST::OptimizeQueueForSeek()
{
  AssertSameThread(_requestingThread);
  // reorder based on seek only, assume all priorities equal now
  
  HeapArray< RefR<FileRequest> > temp;
  // minimize Realloc during adding - we know in advance what memory will be needed
  temp.Realloc(_queue.Size());

  // there is no need to extract from _queue heap in any particular order
  // the heap creation speed is probably little affected by input order, if at all
  for (int i=0; i<_queue.Size(); i++)
  {
    RefR<FileRequest> req = _queue[i];
    req->_priority = FileRequestPriority(100);
    temp.HeapInsert(req);
  }

  // avoid copy - temp will not be needed any more
  _queue.Move(temp);
}

void FileServerST::SubmitRequestsMinimizeSeek()
{
  OptimizeQueueForSeek();

  if (CheckMainThread(_requestingThread))
  {
  // we want to flush all requests, even low priority ones
  // this is used to minimize seeks during preloading
  ProcessRequests(INT_MAX,true);
}
}

void FileServerST::WaitForOneRequestDone(int timeout)
{
  AssertSameThread(_requestingThread);
  // try submitting some requests to prevent empty worker thread queue
  ProcessRequests();
  // check first item in the _workerThread queue
  _workerThread.EnterLock();
  RefR<FileRequest> req;
  if (_workerThread._queueProcessed<_workerThread._queue.Size())
  {
    req = _workerThread._queue[_workerThread._queueProcessed]->GetFileRequest();
  }
  _workerThread.LeaveLock();
  if (req)
  {
    WaitUntilDone(req.GetRef(),timeout);
  }
}

bool FileServerST::WaitUntilDone(FileRequestHandle handle, int timeout)
{
  // no need for a scope here, time spent mostly in req->WaitUntilDone
  //PROFILE_SCOPE_EX(fSRWD,file);
  if (handle.IsNull())
  {
    //Log("Cannot wait for NULL request");
    return true;
  }
  FileRequest *req = static_cast_checked<FileRequest *>(handle.GetRef());

  
  DWORD startTime = ::GlobalTickCount();
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    bool interesting = InterestedIn(req->_fileLogical,req->_fromLogical,req->_sizeLogical);
    
    bool queued = false;
    bool pending = false;
   
    if (req->_waiting)
    {
      queued = true;
    }
    if (req->_operation && !req->_operation->IsDone())
    {
      pending = true;
    }
    
    if (interesting && (queued && pending || req->_targetsNotDone>0) )
    {
      LogF(
        "Request used %s:%x:%x when not ready",
        cc_cast(_handles.GetName(req->_fileLogical)),req->_fromLogical,req->_sizeLogical
      );
    
    }
    #if 0 // _PROFILE
      if (queued)
      {
        const char *name = _handles.GetName(req->_fileLogical);
        RString logName = QFBank::FileOnOffset(req->_fileLogical,req->_fromLogical,req->_fromLogical+req->_sizeLogical);
        LogF(
          "Blocked: [[[ Queued %s(%s):%x:%x",
          name,cc_cast(logName),req->_fromLogical,req->_sizeLogical
        );
        LogWaitingRequests(req);
      }
    #endif
  #endif

  PROFILE_SCOPE_EX(fsWai,*);
  if (PROFILE_SCOPE_NAME(fsWai).IsActive())
  {
    RString logName = req->GetDebugName();
    if (!logName.IsEmpty()) /* empty names means not inside of PBO - we are not interested then */
    {
      PROFILE_SCOPE_NAME(fsWai).AddMoreInfo(logName);
    }
  }
  
  
  // if there is context,chance is operation did not finish yet
  // if the request is non empty, it might be in progress still
  if (req->_sizePhysical>0)
  {
    if (!req->WaitUntilDone(timeout))
      return false;
    if (timeout>0)
      timeout = intMax(0, startTime + timeout - GlobalTickCount());
  }
  // part of the request may be handled by other requests - we may need to wait for some of them
  for (int ri=0; (ProcessRequests(),req->_targetsNotDone>0) && ri<req->_depends.Size(); ri++)
  {
    // note: some waiting may be done even here
    _workerThread.VerifyRequestPosition(req->_depends[ri]);
    if (!req->_depends[ri]->WaitUntilDone(timeout))
      return false;
    if (timeout>0)
      timeout = intMax(0, startTime + timeout - GlobalTickCount());
  }
  while (req->_targetsNotDone>0)
  {
    ProcessRequests();
  }

  
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
  DWORD endTime = ::GlobalTickCount();
    STotalWaitTime._total += endTime-startTime;
    #if 1
    if (endTime-startTime>10) //less of spamm
    {
      const char *name = _handles.GetName(req->_fileLogical);
      RString logName = QFBank::FileOnOffset(req->_fileLogical,req->_fromLogical,req->_fromLogical+req->_sizeLogical);
      LogF(
        "Waiting %d ms for %s%s%s:%x:%x (%s)",
        endTime-startTime,pending ? "pending " : "",queued ? "queued " : "",
        name,req->_fromLogical,req->_sizeLogical,cc_cast(logName)
      );
      if (strstr(name,"\\anim.pbo"))
      {
        ASM_NOP;
      }
      // note: logName search is not reliable, as for short files there may be multiple files in one request
      // as a result, false positives may occur
      else if (strstr(logName,".rtm"))
      {
        ASM_NOP;
      }
      else if (strstr(logName,".wrp"))
      {
        ASM_NOP;
      }
      else if (strstr(logName,".wss"))
      {
        ASM_NOP;
      }
      else if (strstr(logName,".ogg"))
      {
        ASM_NOP;
      }
      else if (strstr(logName,".p3d"))
      {
        ASM_NOP;
      }
      else if (strstr(logName,".paa"))
      {
        ASM_NOP;
      }
      else
      {
        ASM_NOP;
      }
    }
    #endif
  #endif
  return true;
}


bool FileServerST::RequestIsDoneAsync(FileRequestHandle handle)
{
  FileRequest *req = static_cast_checked<FileRequest *>(handle.GetRef());
  if (!req) return true;
  MemorySubscribe();
  if (req->_targetsNotDone<=0)
  {
    // make sure we see all changes done to the request before we use it
    MemorySubscribe();
    return true;
  }
  return false;
}

PosQIStreamBuffer FileServerST::RequestResultAsync(FileRequestHandle handle, FileServerHandle fileHandle, QFileSize start, QFileSize size)
{
  FileRequest *req = static_cast_checked<FileRequest *>(handle.GetRef());
  if (!req) return PosQIStreamBuffer();
  if (req->_targetsNotDone>0) return PosQIStreamBuffer();
  DoAssert(req->_fileLogical==fileHandle);
  // find the corresponding result
  #if 1
  int resIndex = req->GetResultIndex(start);
  if (resIndex>=0 && resIndex<req->_resultData.Size())
  {
    FileInCache *item = req->_resultData[resIndex];
    Assert(!item->_buffer.IsNull()); // request has completed, it must have all data ready
    return item->GetBufferRef();
  }
  #endif

  return PosQIStreamBuffer();  
}

InFileLocation FileServerST::RequestLocationAsync(const FileRequestCompletionHandle &req)
{
  InFileLocation key;
  FileRequest *request = REQUEST_FROM_COMPLETION(req);
  if (!request)
  {
    key._handle = NULL;
    key._start = 0;
    key._size = 0;
}
  else
  {
    key._handle = request->_fileLogical;
    key._start = request->_neededBeg;
    key._size = request->_neededEnd-request->_neededBeg;
  }
  return key;
}


PosQIStreamBuffer FileServerST::RequestResultAsync(FileRequestCompletionHandle &request, FileServerHandle handle, QFileSize pos)
{
  FileRequest *req = REQUEST_FROM_COMPLETION(request);
  return RequestResultAsync(req,handle,pos,1);
}


bool FileServerST::RequestIsDone(FileRequestHandle handle)
{
  AssertSameThread(_requestingThread);
  if (handle.IsNull())
  {
    //Log("Cannot wait for NULL request");
    return true;
  }
  FileRequest *req = static_cast_checked<FileRequest *>(handle.GetRef());
  
  // if there is context, operation did not finish yet
  bool done = req->IsDone();
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    if (
      //done &&
      req && InterestedIn(req->_fileLogical,req->_fromLogical,req->_sizeLogical)
    )
    {
      const char *state = done ? "Done" : "Test";
      LogF(
        "%s - req %s:%x:%x",state,
        (const char *)_handles.GetName(req->_fileLogical),req->_fromLogical,req->_sizeLogical
      );
    }
  #endif

  return done;
}

void FileServerST::CancelRequest(FileRequestHandle handle)
{
  // request cancelling is only partially implemented
  // there are some problems with request unlocking/removal, including crashes
  #if 0
  FileRequest *req = static_cast_checked<FileRequest *>(handle.GetRef());
  if (!req->_waiting)
  {
    // currently we are unable to cancel request which are already submitted into the working thread
    return;
  }
  // we need to clean up the request - it may have allocated some pages in the memory
  // if there are any handlers, they will be executed as failed
  // TODO: make sure RRCanceled is passed
  RequestDone(req,0);
  #endif
}

void FileServerST::AddHandler(
  FileRequestHandle handle,
  RequestableObject *object,
  RequestableObject::RequestContext *context
)
{
  if (handle.IsNull())
  {
    Fail("No handler can be attached to a NULL request");
    return;
  }
  FileRequest *req = static_cast_checked<FileRequest *>(handle.GetRef());
  req->AddHandler(object,context);
}

bool FileServerST::OtherThreadsCanUse() const
{
  return false;
}

void FileServerST::ProcessHandlers(int threadName)
{
  // reverse the traversal order - this makes sure we respect the order as pushed by each individual thread
  // TODO: select handlers for given thread only
  Assert(threadName+1>=0 && threadName+1<MaxThreads);
  PROFILE_SCOPE_EX(fsPHa,file);
  SListMT<RequestableObjectRequestResultInfo> reverse;
  _threadHandlers[threadName+1]._handlers.GetAllReversed(reverse);
      
  while (RequestableObjectRequestResultInfo *handler = reverse.Pop())
  {
    if (handler->_object)
    {
      Assert(threadName==handler->_object->ProcessingThreadId());
    handler->_object->RequestDone(handler->_context,handler->_result);
    }
    delete handler;
  }
}

void FileServerST::Update()
{
  ProcessRequests();
  ProcessHandlers(-1);
  if (CheckMainThread(_requestingThread))
  {
    ProcessHandlers(0);
  }
  Maintain();
}

StoredRefContainer FileServerRefToDestroyMainThread;

struct DestroyRef
{
  bool operator () (RefCount *ref) const {ref->Release();return false;}
};
 
void FileServerST::UpdateByThread(int threadName)
{
  ProcessHandlers(threadName);
  if (threadName==0)
  { // main thread processing
    // caution: destroying a texture may take the texture-memory critical section
    // if another thread already holds the CS, deadlock might be possible
   FileServerRefToDestroyMainThread.ForEach(DestroyRef());
  }
}



void FileServerST::LogWaitingRequests(FileRequestHandle req)
{
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
    #if 0
      // it makes no sense to log queued request, as we will skip them
      // in favor of any blocking request
      for (int i=0; i<_queue.Size(); i++)
      {
        const FileRequest *req = _queue[i];

        const char *name = _handles.GetName(req->_fileLogical);
        RString logName = QFBank::FileOnOffset(req->_fileLogical,req->_fromLogical,req->_fromLogical+req->_sizeLogical);

        LogF(
          "  Queued %s(%s):%x:%x, priority %d",
          name,cc_cast(logName),req->_fromLogical,req->_sizeLogical,req->_priority
        );
        
      }
    #endif
    #if 1
      // log any worker thread requests, as they are hard to skip
      LogF(
        "Worker thread %d requests, %d bytes",
        _nRequestsLoading,_sizeRequestsLoading
      );
      _workerThread.EnterLock();
      for (int i=0; i<_workerThread.Size(); i++)
      {
        IOverlapped *overlapped = _workerThread._queue[i];
        if (req && overlapped->GetFileRequest()==req) break;
        LogF("  %s",cc_cast(overlapped->GetName()));
      }
      _workerThread.LeaveLock();
    #endif
  #endif
}

int FileServerST::PreloadQueueRequests(int *size) const
{
  if (size)
  {
    *size = _queueByteSize+_sizeRequestsLoading;
  }
  return _workerThread.Size()+_queue.Size();
}

float FileServerST::PreloadQueueStatus() const
{
  // check number of pending and queued requests
  // check total size of pending and queued requests
  int number = _workerThread.Size()+_queue.Size();
  int totalSize = _queueByteSize+_sizeRequestsLoading;
  const float maxTotalSize = 8*1024*1024;
  const float maxTotalNumber = 20;
  if (number>=maxTotalNumber || totalSize>=maxTotalSize) return 1;
  return floatMax(totalSize/maxTotalSize,number/maxTotalNumber);
}


bool FileServerST::FlushReadHandle(const char *name)
{
  AssertSameThread(_requestingThread);
  // if it is bank, we will try unloading it first
  FileBufferBankFunctions::FlushBankHandle(name);
  // we need to release all buffers with this handle first
  // convert name to handle by opening it
  FileServerHandle handle = _handles.Open(this, name, true);
  return FlushReadHandle(handle);
}
  
bool FileServerST::FlushReadHandle(FileServerHandle handle)
{
  if (!handle) return true;
  //LogF("Flushing %s:%x",name,handle);
  // if there are any pending requests on this file, they need to be finished first
  for (;;)
  {
    RefR<FileRequest> req;
    for (int i=0; i<_queue.Size(); i++)
    {
      if (_queue[i]->_fileLogical==handle) {req=_queue[i];break;}
    }
    // it could be better to delete queued item immediately, but someone could be waiting for it
    // deleting is possible only when there are no external references
    if (!req)
    {
      for (int i=0; i<_workerThread.Size(); i++)
      {
        if (!_workerThread._queue[i]) continue;
        FileRequest *r = _workerThread._queue[i]->GetFileRequest();
        if (r && r->_fileLogical == handle)
        {
          req = r;
          break;
        }
      }
    }
    if (!req) break; // no more requests
    //LogF("Waiting for request %s:%x",name,_handle);
    req->WaitUntilDone(INFINITE);
  }
  // find any cached items associated with this _handle
  {
    FS_SCOPE_LOCK();
    for (FileInCache *item = _cache._list.First(); item; )
    {
      FileInCache *next = _cache._list.Next(item);
      // check if the _handle is associated with the bank
      if (item->_loc._handle==handle)
      {
        DeleteItem(item);
      }
      item = next;
    }
  }
  
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  if (info->Check()) // note: invalid handle does not need closing, but it needs flushing
  {
    // close the handle
    _handles.Close(handle,false);
  }
  bool flushed = _handles.Flush(info);
  if (!flushed && info)
  {
    // following happens when leaving MP mission, no idea why
    //ErrF("Flushing file %s not possible - still open",cc_cast(info->_name));
  }
  return flushed;
}

FileServerST::FileInCache *FileServerST::Find(FileServerHandle handle, QFileSize start, QFileSize size)
{
  // cache critical section already needs to be taken - the caller handling the result needs to hold it
  // make sure nothing is evicted from the cache while creating the request
  //PROFILE_SCOPE_EX(fsFnd,file);
  InFileLocation key;
  key._handle = handle;
  key._start = start;
  key._size = size;
  Assert(size<=GetPageRecommendedSize());
  const RefFileInCache &item = _cache._table.Get(key);
  return _cache._table.IsNull(item) ? NULL : item.GetRef();
}

#if USE_FILE_MAPPING

void QIStreamBufferSource::Free()
{
  Fail("Not implemented yet");
}

void QIStreamBufferSource::ReadIntoMemory()
{
  _buffer = new QIStreamBufferMappedFromFile(_mapping,_start,_length);
  void *memory = _buffer->DataLock(0,_buffer->GetSize());
  volatile char read = *reinterpret_cast<char *>(memory);
  (void)read;
  _buffer->DataUnlock(0,_buffer->GetSize());
  #if _DEBUG
  _bufferFilled = true;
  #endif
}

Ref<QIStreamBuffer> QIStreamBufferSource::GetBuffer() const
{
  if (_buffer)  
  { 
    Ref<QIStreamBuffer> ret = _buffer;
    _buffer.Free();
    return ret;
  }
  // this is normal when reading from a cache
  Assert(_bufferFilled);
  return new QIStreamBufferMappedFromFile(_mapping,_start,_length);
}
  
static class SystemInfo
  {
  SYSTEM_INFO _info;
  DWORD _pageSize;
  DWORD _allocationGranularity;
  
  public:
  SystemInfo() {GetSystemInfo(&_info);}
  
  DWORD AllocationGranularity() {return _info.dwAllocationGranularity;} 
} SSystemInfo;

static size_t FileMapped;
static size_t FileReserved;

QIStreamBufferMappedFromFile::QIStreamBufferMappedFromFile(HANDLE mapping, QFileSize start, QFileSize length)
    {
  // round to allocation granularity
  int align = SSystemInfo.AllocationGranularity();
  QFileSize roundStart = start& ~(align-1);
  //QFileSize roundEnd = (start+length+align-1)& ~(align-1);
  QFileSize roundEnd = start+length;
  
  _view = reinterpret_cast<char *>(MapViewOfFile(mapping,FILE_MAP_READ,0,roundStart,roundEnd-roundStart));
  if (!_view)
  {
    _bufferSize = 0;
    RptF("Failed mapping");
    void ReportMemoryStatus();
    ReportMemoryStatus();
    }
  else
  {
    _buffer = _view + (start-roundStart);
    _bufferSize = length;
    
    ++FileMapped;
  }
}
    
QIStreamBufferMappedFromFile::~QIStreamBufferMappedFromFile()
{
  --FileMapped;
    
  UnmapViewOfFile(_view);
  _view = NULL;
  _buffer = NULL; // prevent parent from deallocating
}
    

mem_size_t MemoryStoreUsed()
{
  return 0;
  }

#elif USE_MEM_STORE

DEFINE_FAST_ALLOCATOR(QIStreamBufferMappedTgt)

mem_size_t MemoryStoreUsed()
{
  // return how much of the memory store is currently committed
  return GMemStore.GetCommittedBytes();
}

void QIStreamBufferSource::Free()
{
  if (_memIndex>=0)
  {
    GMemStore.Free(_memIndex);
    _memIndex = -1;
  }
}

/**
There are two situations we need data to be mapping in the virtual memory:
1) writing data - using DataLock / DataUnlock
2) reading data - as long as QIStreamBufferMapped exists
*/

Ref<QIStreamBuffer> QIStreamBufferSource::GetBuffer() const
{
  if (_memIndex<0)
  { 
    Fail("Accessing buffer which is not allocated");
    return NULL;
  }
  // call Lock to access real memory
  return new QIStreamBufferMapped(_memIndex);
}

Ref<QIStreamBuffer> QIStreamBufferMappedTgt::GetBuffer() const
{
  if (_index<0)
  { 
    Fail("Accessing buffer which is not allocated");
    return NULL;
  }
  // request exists, therefore memory is committed
  // however we need to be sure the memory is still mapped even if the target unmaps it via DataUnlock
  return new QIStreamBufferMapped(_index);
}



DEFINE_FAST_ALLOCATOR(QIStreamBufferMapped)

QIStreamBufferMapped::QIStreamBufferMapped(int page)
{
  _index = page;
  // while the buffer is mapped, it must not be released
  // TODO: consider: both ShareAlloc and GMemStore.MapView lock a critical section
  // It would be enough to lock it once here
  GMemStore.ShareAlloc(_index);
  _buffer = (char *)GMemStore.MapView(page);
  _bufferSize = GMemStore.GetPageSize();
  
}
QIStreamBufferMapped::~QIStreamBufferMapped()
{
  GMemStore.UnmapView(_index,_buffer);
  GMemStore.Free(_index);
  // avoid the base destructor freeing the memory
  _buffer = NULL;
}

void QIStreamBufferMapped::OnUnused() const
{
    Destroy();
  }



#else
mem_size_t MemoryStoreUsed() {return 0;}

#endif

/**
@return function returns false when it cannot store an item because it is already stored.
@param executeHandlers sometimes handlers may be executed immediately on finished handlers
@param success in case of failure we will remove the buffer from the cache instead
@param discardSoon item will can be discarded as soon as it is unlocked - place it at the cache end
*/

bool FileServerST::Store(
  TgtBuffer *buf, FileServerHandle handle, QFileSize start, QFileSize size,
  bool executeHandlers, bool success, bool discardSoon, FileRequest *completedBy
)
{
  FS_SCOPE_LOCK();
  //PROFILE_SCOPE_EX(fsSto,file);
  // note: as requests may overlap, single buffer may already be stored
  FileInCache *item = Find(handle,start,size);
  if (item && !item->_buffer.IsNull())
  {
    // if the item is stored, its handlers and requests are already executed
    if (!discardSoon)
    {
      MoveToFront(item);
    }
    return false;
  }
  
  if (item)
  {
    // the item is there, but there are some requests associated with it and there is no buffer

    Assert(item->_loc._start == start);
    Assert(item->_loc._size == size);
    Assert(item->_loc._handle == handle);
   
    if (success)
    {
      // we need to delete the item from the cache and insert it again, as buf size will change
      Ref<FileInCache> temp = item;
      //Assert(_cache._list.CheckIntegrity());
      _cache._list.Delete(item);
      //Assert(_cache._list.CheckIntegrity());
      item->_buffer = buf;
      if (!discardSoon)
      {
        _cache._list.Add(item);
      }
      else
      {
        _cache._list.AddLast(item);
      }
      
      //LogF("Filled in cache %s:%x:%x, buffer 0x%x", cc_cast(_handles.GetName(handle)),start,size,buf);
    }
    // no else here - if not success - we will destroy it later
    
    // notify the requests they might be finished by completing this particular buffer
    for (int i=0; i<item->_request.Size(); i++)
    {
      FileRequest *req = item->_request[i];
      // the request may be empty
      req->OnBufferCompleted(executeHandlers,success,item);
      if (completedBy && req!=completedBy)
      {
        bool deleted = req->_depends.DeleteKey(completedBy);
        deleted = deleted; // breakpoint opportunity
    }
    }
    item->_request.Clear();
    
    if (!success)
    {
      DeleteItem(item);
    }
    return true;
  }

  if (success)
  {
    if (UseReadScatter)
    {
      // when virtual memory is enabled, different code path is used for files  
      // no item yet? this is strange - an item should be created while creating the request
      LogF(
        "Request %s:%x:%x (%s)",
        cc_cast(_handles.GetName(handle)),start,size,
        cc_cast(QFBank::FileOnOffset(handle,start,start+size))
      );
      Fail("Item should be already created");
    }
    

    item = new FileInCache;
    
    item->_buffer = buf;
    item->_loc._start = start;
    item->_loc._size = size;
    item->_loc._handle = handle;
    _handles.Open(handle,true);
    Assert(size<=GetPageRecommendedSize());

    //LogF("Stored in cache %s:%x:%x, buffer 0x%x", cc_cast(_handles.GetName(handle)),start,size,buf);

    if (!discardSoon)
    {
      _cache._list.Add(item);
    }
    else
    {
      _cache._list.AddLast(item);
    }
    _cache._table.Add(item);
  }
  //Assert(CheckIntegrity());
  //LogF(
  //  "Store %s:%d:%d",
  //  (const char *)_handles.GetName(item->handle),item->start,item->size
  //);
  return false;
}

FileServerST::FileInCache *FileServerST::StoreEmpty(
  FileServerHandle handle, QFileSize start, QFileSize size, bool discardSoon
)
{
  //PROFILE_SCOPE_EX(fsStE,file);
  FS_SCOPE_LOCK();
  
  
  //item->_buffer = NULL;
  InFileLocation loc;
  loc._start = start;
  loc._size = size;
  loc._handle = handle;
  Assert(size<=GetPageRecommendedSize());

  const RefFileInCache &inCache = _cache._table.Get(loc);
  if (_cache._table.NotNull(inCache))
  {
    // there is already an item like this - no need to create another one
    return inCache;
  }

  Ref<FileInCache> item = new FileInCache;
  item->_loc = loc;
  _cache._list.Add(item);
  _cache._table.Add(item.GetRef());
  _handles.Open(handle,true);

  return item;
}

/** used as interface for reading done from QIFStream / FileBufferLoading */
PosQIStreamBuffer FileServerST::Read(FileServerHandle handle, QFileSize start, QFileSize size)
{
  PROFILE_SCOPE_EX(fsRd,file);
  FileRequestHandle req;
  // first try finding this buffer in the cache
  FileInCache *item = Find(handle,start,size);
  if (item)
  {
    if (!item->_buffer.IsNull())
    {
    FS_SCOPE_LOCK();
    MoveToFront(item);
#ifndef _SUPER_RELEASE
#if _ENABLE_REPORT && _ENABLE_PERFLOG
    if (GLogDVD)
    {
      LogDVD.UpdateRequests(_handles.GetName(handle), start, size);
    }
#endif  
#endif  
    return item->GetBufferRef();
  }
    else
    {
      // data not there yet, but there are already requests prepared to get them
      // Request will take care of this and find the substitute for us
      req = Request(NULL,NULL,FileRequestPriority(1),handle,start,size);
    }
  }
  else
  {
  // once we read anything, extend the request so that we read enough
  // If the request overlaps, file server will cache it anyway
  
    // TODO: consider not only extending the request size, but also aligning it or at least aligning its end
  const int minReqSize = (intMax(32*1024,_readScatterPageSize)+_readScatterPageSize-1)/_readScatterPageSize*_readScatterPageSize;
  int reqSize = intMax(size,minReqSize);

    req = Request(NULL,NULL,FileRequestPriority(1),handle,start,reqSize);
  // create one additional request - this can be performed while we will be processing the one we just submitted
    Request(NULL,NULL,FileRequestPriority(2),handle,start+minReqSize,minReqSize);
  }
  
  // NULL request means all data are already cached
  // empty request means all data are already covered by other requests - we may need to wait for some of them
  Assert(req.NotNull() || size==0);
  if (req)
  {
    FileRequest *r = static_cast_checked<FileRequest *>(req.GetRef());
    WaitUntilDone(req,INFINITE);

    // now we need to find our data in the request
    PosQIStreamBuffer ret = r->GetResultBuffer(start,size);
    if (ret._len>0)
      return ret;

    ErrF("Result not found in the request %s",cc_cast(r->GetDebugName()));
    
    DoAssert(r->_fileLogical==handle);
    // we cannot use the memory from the buffer, as it does not have a suitable form
    if (r->_fromLogical<=start && r->_fromLogical+r->_sizeLogical>=start+size)
    {
      // we know item buffer is kept in the request
      int pageSize = GetPageRecommendedSize();
      int bufIndex = (start-r->_fromLogical)/pageSize;
      if (bufIndex>=0 && bufIndex<r->_target.Size())
      {
        #if USE_MEM_STORE
        return PosQIStreamBuffer(r->_target[bufIndex]->GetBuffer(),start,size);
        #else
        return PosQIStreamBuffer(r->_target[bufIndex],start,size);
        #endif
      }
    }
    FS_SCOPE_LOCK();
    // if buffer search failed, we will try to find it in the cache
    FileInCache *item = Find(handle,start,size);
    if (item && !item->_buffer.IsNull())
    {
      return item->GetBufferRef();
    }
    // the data are not yet present in the buffer
    Fail("Request not satisfied");
  }
  // try finding this buffer in the cache
  item = Find(handle,start,size);
  
  // when request fails, proceed by normal processing via New

  if (!item || item->_buffer.IsNull())
  {
    Fail("Request failed, attempting New fallback");
    return PosQIStreamBuffer();
  }
  else
  {
    FS_SCOPE_LOCK();
    MoveToFront(item);
# ifndef _SUPER_RELEASE
#   if _ENABLE_REPORT && _ENABLE_PERFLOG
      if (GLogDVD)
      {
        LogDVD.UpdateRequests(_handles.GetName(handle), start, size);
      }
#   endif  
# endif  
    return item->GetBufferRef();
  }
}

const int MaxCachedHandles = 64;

FileServerHandle FileServerST::OpenReadHandle(const char *name)
{
  AssertSameThread(_requestingThread);
#if _USE_FCPHMANAGER
  if (FileServer::GFileServerFunctions) 
    FileServer::GFileServerFunctions->ProcessFCPHManager(name);
#endif

  //PROFILE_SCOPE_EX(fsOpR,file);
  if (!_initDone) Init();
  // keep total number of cached handles within some limits
  const int maxHandles = intMax(512,_handles._countHeld+MaxCachedHandles);
  while (_handles._count>maxHandles)
  {
    // we want to flush data which are kept in cache, but no other handles are held to them
    // this never happens with pbo files, but can happen with files like missions, scripts...
    // find some handle which is not currently "held", and flush it (and close it)
    if (!_handles.FlushOldestNotHeld())
      break;
  }
  return _handles.Open(this, name);
}

FileWriteHandle FileServerST::OpenWriteHandle(const char *name)
{
#if POSIX_FILES_COMMON
#ifdef _WIN32
  return (FileWriteHandle)CreateFileLongPath(name, O_TRUNC | O_CREAT | O_BINARY | O_WRONLY, S_IWRITE);
#else
  return (FileWriteHandle)CreateFileLongPath(name, O_TRUNC | O_CREAT | O_WRONLY, S_IWRITE);
#endif
#else
  return (FileWriteHandle)CreateFileLongPath(
    name, GENERIC_WRITE, 0, NULL, // security 
    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); // template
#endif
}

FileWriteHandle FileServerST::OpenWriteHandleForAppend(const char *name)
{
#if POSIX_FILES_COMMON
#ifdef _WIN32
  return (FileWriteHandle)CreateFileLongPath(name, O_APPEND | O_CREAT | O_BINARY | O_WRONLY, S_IREAD|S_IWRITE);
#else
  return (FileWriteHandle)CreateFileLongPath(name, O_APPEND | O_CREAT | O_WRONLY, S_IREAD|S_IWRITE);
#endif
#else
  HANDLE fileHandle = CreateFileLongPath(
    name, GENERIC_WRITE, 0, NULL, // security 
    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); // template
  SetFilePointer(fileHandle, 0, 0, FILE_END);
  return (FileWriteHandle)fileHandle;
#endif
}

void FileServerST::CloseReadHandle(FileServerHandle handle)
{
  AssertSameThread(_requestingThread);
	if (!CheckReadHandle(handle)) return;
  _handles.Close(handle,false);
}
bool FileServerST::CheckReadHandle(FileServerHandle handle) const
{
  //AssertSameThread(_requestingThread);
  if (!handle) return false;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  return info->Check();
}

void FileServerST::MakeReadHandlePermanent(FileServerHandle handle)
{
  //AssertSameThread(_requestingThread);
  if (!handle) return;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  info->_permanent = true;
}

RString FileServerST::GetHandleName(FileServerHandle handle) const
{
  //AssertSameThread(_requestingThread);
  return _handles.GetName(handle);
}

void *FileServerST::GetWinHandle(FileServerHandle handle) const
{
  if (!CheckReadHandle(handle)) return (void *)INVALID_HANDLE_VALUE;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  return info->GetWinHandle();
}

QFileSize FileServerST::GetFileSize(FileServerHandle handle) const
{
  //AssertSameThread(_requestingThread);
  if (!CheckReadHandle(handle)) return 0;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  return info->_size;
}

QFileTime FileServerST::TimeStamp(FileServerHandle handle) const
{
  AssertSameThread(_requestingThread);
  if (!CheckReadHandle(handle)) return 0;
#if  POSIX_FILES_COMMON
  struct stat st;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  int intHandle = (int)info->GetWinHandle();
  fstat(intHandle,&st);
  return st.st_mtime;
#else
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  FILETIME filetime;
  info->GetFileTime(&filetime);
  return ConvertToQFileTime(filetime);
#endif
}

Future<QFileTime> FileServerST::RequestTimeStamp(const char *name)
{
  Future<QFileTime> result;
  Submit(new OverlappedGetFileTime(name,result));
  return result;
}

#ifdef _WIN32


HANDLE FileServerST::CreateFileLongPath(
  const char *lpFileName,
  DWORD dwDesiredAccess,
  DWORD dwShareMode,
  _SECURITY_ATTRIBUTES *lpSecurityAttributes,
  DWORD dwCreationDisposition,
  DWORD dwFlagsAndAttributes,
  FileServerHandle hTemplateFile)
{
  wchar_t *prefix = L"\\\\?\\";
  int prefixLen = wcslen(prefix);

  bool fullpath = lpFileName[0] == '\\' || lpFileName[0] != '0' && lpFileName[1] == ':';

  AUTO_STATIC_ARRAY(wchar_t, buffer, 1024);
  int len = MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, NULL, 0);

  if (fullpath)
  {
    buffer.Resize(prefixLen + len);
    wcscpy(buffer.Data(), prefix);
    MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, buffer.Data() + prefixLen, len);
  }
  else
  {
    DWORD dirLen = GetCurrentDirectoryW(0, NULL);
    buffer.Resize(prefixLen + dirLen + len);
    wchar_t *ptr = buffer.Data();
    wcscpy(ptr, prefix);
    ptr += prefixLen;
    GetCurrentDirectoryW(dirLen, ptr);
    ptr += dirLen - 1;
    if (*(ptr - 1) != '\\')
    {
      *ptr = '\\';
      ptr++;
    }
    MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, ptr, len);
  }

  return (FileServerHandle)::CreateFileW(buffer.Data(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}
#endif

#if POSIX_FILES_COMMON
int FileServerST::CreateFileLongPath(const char *lpFileName, int oflag, int pmode)
{
#ifdef _WIN32
  const char dirSep = '\\';
  bool fullpath = lpFileName[0] == dirSep || lpFileName[0] != '0' && lpFileName[1] == ':';
#else
  LocalPath(newPath, lpFileName);
  lpFileName = newPath;
  const char dirSep = '/';
  bool fullpath = lpFileName[0] == dirSep || lpFileName[0] == '~';
#endif
  
  AUTO_STATIC_ARRAY(char, buffer, 1024);
  if (!fullpath)
  {
    char *ptr = buffer.Data();
    getcwd(ptr, 1024);
    int dirLen = strlen(ptr);
    ptr += dirLen;
    if (*(ptr - 1) != dirSep)
    {
      *ptr = dirSep;
      ptr++;
    }
    strcpy(ptr, lpFileName);
  }
  else
  {
    strcpy(buffer.Data(), lpFileName);
  }

#if LOG_POSIX_FILES_COMMON
  LogF("CreateFileLongPath opening file: %s", buffer.Data());
#endif
  int retVal = ::open(buffer.Data(), oflag, pmode);
  if (oflag & O_CREAT) chmod(buffer.Data(),S_IREAD|S_IWRITE);
  return retVal;
}
#endif

#include <El/Statistics/statistics.hpp>

ReadHandleCache::ReadHandleCache()
{
  _count = 0;
  _countCached = 0;
  _countHeld = 0;
}

DEFINE_FAST_ALLOCATOR(ReadHandleInfo)

ReadHandleInfo::ReadHandleInfo()
{
  _open = 0;
  _openByCache = 0;
  _permanent = false;
}

ReadHandleInfo::~ReadHandleInfo()
{
}

DEFINE_FAST_ALLOCATOR(ReadHandleInfoNormal)

ReadHandleInfoNormal::~ReadHandleInfoNormal()
{
#if OPEN_HANDLE_DIAGS>200
  Log("Closed handle %x to %s",_handle,(const char *)_name);
#endif
  #if USE_FILE_MAPPING
  if (_mapping)
  {
    CloseHandle(_mapping);
    _mapping = NULL;
  }
  #endif
  if (_handle)
  {
    //PROFILE_SCOPE_EX(fClos,file);
    Assert(!_handle->GetOwner()->IsWaiting(_handle));
    #if POSIX_FILES_COMMON
      int intHandle = (int)(_handle->GetHandle());
      if ( intHandle != -1 ) //it is valid
        ::close(intHandle);
    #else
      if (_handle->GetHandle()!=INVALID_HANDLE_VALUE)
      {
        ::CloseHandle(_handle->GetHandle());
      }
    #endif
    _handle = NULL;
  }
}

void *ReadHandleInfoNormal::GetWinHandle() const
{
  if (!_handle->IsDone()) _handle->Wait(INFINITE);

  return (void *)_handle->GetHandle();
}

void *ReadHandleInfoNormal::GetMappingHandle() const
{
  #if USE_FILE_MAPPING
  if (!_mapping)
  {
    if (!_handle->IsDone()) _handle->Wait(INFINITE);

    HANDLE file = _handle->GetHandle();
    _mapping = CreateFileMapping(file,NULL,PAGE_READONLY,0,0,NULL);
    if (!_mapping)
    {
      ErrorMessage("File mapping has failed, error %x",GetLastError());
    }
  }
  return _mapping;
  #else
  return NULL;
  #endif
}


void ReadHandleInfoNormal::GetFileTime(FILETIME *time) const
{
  if (!_handle->IsDone()) _handle->Wait(INFINITE);

#if POSIX_FILES_COMMON
  struct stat st;
  fstat((int)_handle->GetHandle(),&st);
  *(reinterpret_cast<time_t *>(time)) = st.st_mtime;
#else
  ::GetFileTime(_handle->GetHandle(), NULL, NULL, time);
#endif
}

bool ReadHandleInfoNormal::Check() const
{
  // we wait for the open to complete right when creating the handle
  // there is no need to wait for it any more
  DoAssert(_handle->IsDone());
  //if (!_handle->IsDone()) _handle->Wait(INFINITE);

  return _handle->GetHandle() != INVALID_HANDLE_VALUE;
}

bool ReadHandleInfoNormal::Read(FileRequest *req, bool urgent)
{
  Assert(_handle);
  OverlappedReadScatter *read = new OverlappedReadScatter(_handle, req);
  req->_operation = _handle->GetOwner()->Submit(read,urgent);
#ifdef _WIN32
  SetLastError(ERROR_HANDLE_EOF);
#endif
  return false;
}

void ReadHandleInfoNormal::CheckRequest(FileRequest *req)
{
}

void ReadHandleInfoNormal::UpdateRequest(FileRequest *req)
{
}

//! create path to given directory
void CreatePath(RString path)
{
  // string will be changed temporary
  char *end = (char *)path.Data();
  // skip absolute part of the path, like a:, or cache:
  char *absPart = strchr(end,':');
  if (absPart) end = absPart+1;
  // skip backslash, like in cache:\xx
  while (*end=='\\') end++;
  while (end = strchr(end, '\\'))
  {
    *end = 0;
    ::CreateDirectory(path, NULL);
    *end = '\\';
    end++;
  }
}

#if _SUPER_RELEASE
#define _TEST_UPDATE 0
#else
#define _TEST_UPDATE 1
#endif

/**
@param onlyIfOpen do not open file if no handle currently exists - useful when we need the handle
  only to flush/close it.
*/
FileServerHandle ReadHandleCache::Open(
  FileServerST *server, const char *name, bool onlyIfOpen
)
{
  DoAssert(server->CheckOwningThread());
  Assert(CheckIntegrity());
  // check if given name can be found
  {
    for (ReadHandleInfo *info=_cache.Start(); _cache.NotEnd(info); info = _cache.Advance(info))
    {
      if (!strcmpi(info->_name,name))
      {
        // handle that is used cannot be cached
        if (info->Check())
        {
          if (info->_open==info->_openByCache)
            _countHeld++; // we are opening held, if not held before, became held right now
          if (info->_open++==0)
            _countCached--; // it can no longer be considered cached
        }
        _cache.MakeFirst(info);
        Assert(CheckIntegrity());
        FileJournal.Add(Format("  add %s to %d,%d",cc_cast(name),info->_open,info->_openByCache));
        return (FileServerHandle)info;
      }
    }

    if (onlyIfOpen)
    {
      return NULL;
    }
  }
  #if _ENABLE_REPORT && _ENABLE_PERFLOG
  DWORD startTime = ::GlobalTickCount();
  #endif

  //PROFILE_SCOPE_EX(fOpen,file);
  //DWORD attr = CreateFileAttributes();

  // note: low level free may release some cache items, resulting even in some handle released
  // this should not matter, as we are getting to open the handle anyway
  FreeOnDemandGarbageCollectSystemMemoryLowLevel(GetPageRecommendedSize()*8);
  //Retry:

  ReadHandleInfo *info = NULL;

#if POSIX_FILES_COMMON
#ifdef _WIN32
  Ref<IOverlapped> handle = server->SubmitUrgent( new OverlappedOpen(name, O_RDONLY | O_BINARY, S_IREAD) );
#else
  Ref<IOverlapped> handle = server->SubmitUrgent( new OverlappedOpen(name, O_RDONLY, S_IREAD) );
#endif
#else
  DWORD attr = CreateFileAttributes();
  Ref<IOverlapped> handle = server->SubmitUrgent( new OverlappedOpen(name, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, attr) );
#endif
  info = new ReadHandleInfoNormal(handle);
  info->_name = name;
  if (!handle->IsDone())
  {
    handle->Wait(INFINITE);
  }
  if (handle->GetHandle()!=INVALID_HANDLE_VALUE)
  {
#if POSIX_FILES_COMMON
    info->_size = ::filelength( (int)(handle->GetHandle()) );
#else
    info->_size = ::GetFileSize(handle->GetHandle(), NULL);
#endif
  }
  else
  {
    info->_size = 0;  
  }

  #if _ENABLE_REPORT && _ENABLE_PERFLOG
  DWORD endTime = ::GlobalTickCount();
    STotalWaitTime._total += endTime-startTime;
    if (endTime-startTime>10)
    {
      LogF(
        "Waiting %d ms for open %s",
        endTime-startTime,name
      );
      if (strstr(name,".p3d"))
      {
        ASM_NOP;
      }
      else if (strstr(name,".paa"))
      {
        ASM_NOP;
      }
      else if (strstr(name,".wss"))
      {
        ASM_NOP;
      }
      else if (strstr(name,".ogg"))
      {
        ASM_NOP;
      }
    }
  #endif
  Assert(info);

  #if OPEN_HANDLE_DIAGS>100
  Log("Opened handle %x to %s",handle,(const char *)name);
  #endif

  _cache.Insert(info);
  _count++;
  if (info->Check())
  {
    info->_open++;; // one reference count for each opened file
    _countHeld++;
    Assert(info->_open==1);
    Assert(info->_openByCache==0);
  }
  else
  {
    info->_size = 0;
    #ifdef _WIN32
    HRESULT hr = GetLastError();
    if (hr==ERROR_NETWORK_ACCESS_DENIED)
    {
      LogF("Too many open network handles");
    }
    #endif
    #if _DEBUG && defined _WIN32
      if (!strncmp(name,"data\\",5))
      {
        // debugging opportunity
        Log("Cannot open %s, %x",name,hr);
        // what error
        ASM_NOP;
      }
    #endif
    // invalid value can be placed to cache immediatelly - it is not open
    _countCached++;
    Maintain();
  }
  Assert(CheckIntegrity());

  FileJournal.Add(Format("  create %s to %d",cc_cast(name),info->_open,info->_openByCache));

  return (FileServerHandle)info;
}

#ifdef USE_FILE_MAPPING

HANDLE ReadHandleCache::Mapping(FileServerHandle file)
{
  if (!file) return NULL;
  ReadHandleInfo *info = (ReadHandleInfo *)file;
  return info->GetMappingHandle();
}

#endif

bool ReadHandleCache::Read(FileRequest *req, bool urgent)
{
  if (!req->_filePhysical) return FALSE;
  ReadHandleInfo *info = (ReadHandleInfo *)req->_filePhysical;
  return info->Read(req,urgent);
}

QFileSize ReadHandleCache::GetSize(FileServerHandle handle)
{
  if (!handle) return 0;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  return info->_size;
}

RString ReadHandleCache::GetName(FileServerHandle handle)
{
  if (!handle) return RString();
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  return info->_name;
}

void ReadHandleCache::CheckRequest(FileRequest *req)
{
  ReadHandleInfo *info = (ReadHandleInfo *)req->_fileLogical;
  if (!info) return;
  info->CheckRequest(req);
}

void ReadHandleCache::UpdateRequest(FileRequest *req)
{
  ReadHandleInfo *info = (ReadHandleInfo *)req->_fileLogical;
  if (!info) return;
  info->UpdateRequest(req);
}

void ReadHandleCache::Open(FileServerHandle handle, bool byCache)
{
  if (!handle) return;
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  if (info->_open == 0)
    _countCached--;
  bool wasHeld = info->_open>info->_openByCache;
  info->_open++;
  info->_openByCache += byCache;
  bool isHeld = info->_open>info->_openByCache;
  if (byCache)
  { // opening by cache can make no difference in "held" status
    Assert(wasHeld==isHeld);
  }
  else
  {
    if (isHeld>wasHeld)
      _countHeld++;
  }

  Assert(info->_openByCache>=0);
  Assert(info->_openByCache<=info->_open);
}

/**
@return true when last handle was closed and Win32 handle was therefore closed as well.
*/
bool ReadHandleCache::Close(FileServerHandle handle, bool byCache)
{
  if (!handle ) return true;
  Assert(CheckIntegrity());
  ReadHandleInfo *info = (ReadHandleInfo *)handle;
  if (!info->Check())
  {
    Fail("Invalid _handle closed");
    return true;
  }
  bool wasHeld = info->_open>info->_openByCache;
  info->_openByCache -= byCache;
  --info->_open;
  Assert(info->_openByCache>=0);
  Assert(info->_openByCache<=info->_open);
  bool isHeld = info->_open>info->_openByCache;

  if (byCache)
  { // closing by cache can make no difference in "held" status
    Assert(wasHeld==isHeld);
  }
  else
  { // closing "held" handle can reduce number of total handles
    Assert(wasHeld>=isHeld);
    if (wasHeld>isHeld)
      _countHeld--;
  }
  FileJournal.Add(Format("  del %s to %d,%d",cc_cast(GetName(handle)),info->_open,info->_openByCache));
  if (info->_open == 0)
  {
    // last opened reference closed - leave it cached
    _countCached++;
    _cache.MakeFirst(info);
    Assert(CheckIntegrity());
    Maintain();
    return true;
  }
  Assert(CheckIntegrity());
  return false;
}

void ReadHandleCache::FlushAll()
{
  Assert(CheckIntegrity());
  for (ReadHandleInfo *info=_cache.Start(); _cache.NotEnd(info); )
  {
    ReadHandleInfo *next = _cache.Advance(info);
    if (info->_open==0 && !info->_permanent)
    {
      _count--;
      _countCached--;
      _cache.Delete(info);
    }
    info = next;
  }
  Assert(CheckIntegrity());
}

bool ReadHandleCache::FlushOldestNotHeld()
{
  for (ReadHandleInfo *info=_cache.Last(); info; info=_cache.Prev(info))
  {
    if (info->_open==info->_openByCache)
    {
      return Flush(info);
    }
  }
  return false;
}

/**
@return false when the _handle cannot be flushed because it is still open
*/

bool ReadHandleCache::Flush(ReadHandleInfo *info)
{
  if (!info)
    return false;
  Assert(CheckIntegrity());
  if (info->_open==0)
  {
    Assert(info->_openByCache==0);
    _count--;
    _countCached--;
    _cache.Delete(info);
    Assert(CheckIntegrity());
    return true;
  }
  else
  {
    return false;
  }
}

bool ReadHandleCache::CheckIntegrity() const
{
  int cachedCount = 0;
  int count = 0;
  int heldCount = 0;
  for (ReadHandleInfo *item = _cache.First(); item; item = _cache.Next(item))
  { 
    if (item->_open==0) cachedCount++;
    if (item->_openByCache!=item->_open) heldCount++;
    count++;
  }
  if (cachedCount == _countCached && count == _count && heldCount==_countHeld) return true;
  LogF("Cached count %d %d", cachedCount, _countCached);
  LogF("Held count %d %d", heldCount, _countHeld);
  LogF("Count %d %d", count, _count);
  return false;
}

void ReadHandleCache::Maintain()
{
  Assert(CheckIntegrity());
  while (_countCached>MaxCachedHandles)
  {
    bool someRemoved = false;
    for (ReadHandleInfo *item = _cache.Last(); item; )
    {
      ReadHandleInfo *prev = _cache.Prev(item);
      if (item->_open==0)
      {
        _countCached--;
        _count--;
        _cache.Delete(item);
        someRemoved = true;
        Assert(CheckIntegrity());
        if (_countCached<=MaxCachedHandles) break;
      }
      item = prev;
    }
    if (!someRemoved)
    {
      Fail("Handle cache corrupt");
      break;
    }
  }
  Assert(CheckIntegrity());
}

void ReadHandleCache::Clear()
{
  for (ReadHandleInfo *info=_cache.Start(); _cache.NotEnd(info); info = _cache.Advance(info))
  {
    Assert(info->_open==0);
  }
  _cache.Clear();
  _count = 0;
  _countCached = 0;
  Assert(CheckIntegrity());
}

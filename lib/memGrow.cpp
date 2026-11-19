#include "wpch.hpp"

#if !defined USE_MALLOC && !defined MFC_NEW
#include "memGrow.hpp"
#include <El/FreeOnDemand/memFreeReq.hpp>
#include "global.hpp"
#include <Es/Common/win.h>
#include <El/Common/perfProf.hpp>

void MemoryErrorReported();

  //const int CommitPagesUnit = 16; // 64 KB commits
  /* note: most operations with CommitPagesUnit=1 are optimized out */
  const int CommitPagesUnit = 1; // always one page (4 KB) commits

#if ALLOC_DEBUGGER
void *MemAllocSystem::Alloc( size_t &size, const char *file, int line, const char *postfix )
#else
void *MemAllocSystem::Alloc( size_t &size )
#endif
{
  #if defined(_WIN32)
  void *mem = GlobalAlloc(GMEM_FIXED,size);
  #else
  void *mem = malloc(size);
  #endif
  if (!mem)
  {
    OutputDebugString("Out of memory\n");
    FailHook("Out of memory");
    // issue some low level warning - windows memory is not available
  }
  return mem;
}
void MemAllocSystem::Free( void *mem, size_t size )
{
  #if defined(_WIN32)
  GlobalFree(mem);
  #else
  free(mem);
  #endif
}

void *MemAllocSystem::Realloc( void *mem, size_t oldSize, size_t size )
{
  #if defined(_WIN32)
  return GlobalReAlloc(mem,size,0);
  #else
  // there is no portable way to resize the memory block
  // realloc is allowed to move it, which we cannot
  return NULL;
  #endif
}

void MemCommit::DoConstruct()
{
  _nRes = 0;
  _pagesCommited=0;_pagesMinCommited=0;
  _pagesLocked=0;
  _maxCommitted = 0;
  _error = false;

  #if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    // we do not need to know page size exactly,
    // but if we know it, we can better commit pages
    // we do not care if actual page size is power of two
    // we ony want to be sure that our _pageSize is power of two
    _pageSize=info.dwPageSize;
    _allocationGranularity=info.dwAllocationGranularity;
    int pageSizeLog=0;
    while( (1<<pageSizeLog)<_pageSize ) pageSizeLog++;

    if (_pageSize!=1<<pageSizeLog)
    {
      MemoryErrorReported();
      ErrorMessage(
        "System page size is %d.\nPower of two page size is required, wanted %d.",
        info.dwPageSize,_pageSize
      );
    }
    _pageSize=(1<<pageSizeLog);
    _pageSizeLog = pageSizeLog;
  #else
    _allocationGranularity=64*1024;
    _pageSize=4*1024;
    _pageSizeLog = 12;
  #endif
}

void MemCommit::DoConstruct( int size )
{
  DoConstruct();
  int minSize = size;
  // if we cannot get more, use 256 MB
  if (minSize>256*1024*1024) minSize = 256*1024*1024;
  Reserve(size*4,minSize,size);
}

void MemCommit::DoDestruct()
{
  if (_nRes>0)
  {
    for (int i=0; i<_nRes; i++)
    {
      #if _ENABLE_REPORT
        // note: flags header is always locked, but it is not marked as PageAvailable
        int resPages = _res[i]._reserved>>_pageSizeLog;
        for (int p=0; p<resPages; p++)
        {
          int first = _res[i]._flags.FindFirst(p,resPages,PageAvailable|PageLocked,0);
          if (first<0) break;
          LogF("  Page %d (%p) locked",first,(char *)_res[i]._data+(first<<_pageSizeLog));
          p = first;
        }
      #endif
      _res[i]._flags.Destroy();
      // decommit all, no matter if is was commited or not
      // no need to decommit, RELEASE will do it both
      ::VirtualFree(_res[i]._data,0,MEM_RELEASE);
      LogF("  Virtual memory %p (%d) released.",_res[i]._data,_res[i]._reserved);
      _res[i]._data=NULL;
      _res[i]._reserved=0;
    }
    _nRes = 0;
  }
}

void MemoryErrorReported();

const MemReservedInfo &MemCommit::Reserve(size_t virtualSize, size_t minPhysicialSize, size_t maxPhysicalSize)
{
  DoAssert(minPhysicialSize>0);
  DoAssert(virtualSize>0);
  DoAssert(maxPhysicalSize>=minPhysicialSize);
  if (_nRes>=MaxResBlocks)
  {
    MemoryErrorReported();
    ErrorMessage("Too many virtual memory blocks requested");
    static MemReservedInfo info={0,0};
    return info;
  }
  //saturateMin(size,40*1024*1024);
  
  // with page size 4 KB this gives 2 GB
  // we want to avoid more, to make sure we are not hitting any 32b overflows
  const int maxPages = 512*1024;
  if ((virtualSize>>_pageSizeLog)>maxPages)
  {
    virtualSize = maxPages*_pageSize;
  }
  if (maxPhysicalSize>virtualSize)
  {
    maxPhysicalSize = virtualSize;
  }
  virtualSize = (virtualSize+_allocationGranularity-1)& ~(_allocationGranularity-1);
  maxPhysicalSize = (maxPhysicalSize+_pageSize-1)& ~(_pageSize-1);

  MemReservedInfo &res = _res[_nRes];
  
  // retry until success or memory would be too low
  int step = intMax(intMax(_allocationGranularity,_pageSize),CommitPagesUnit);
  for( ;virtualSize>=minPhysicialSize; virtualSize -= step)
  {
    int flags = MEM_RESERVE;
    res._data=::VirtualAlloc(NULL,virtualSize,flags,PAGE_READWRITE);
    if( res._data )
    {
      res._reserved=virtualSize;
      break;
    }
    
  }
  // we can never have more physical than virtual
  if (maxPhysicalSize>virtualSize) maxPhysicalSize = virtualSize;
  _maxCommitted += maxPhysicalSize;
  if (!res._data)
  {
    MemoryErrorReported();
    ErrorMessage(
      "Cannot reserve virtual memory (%d MB,%d MB).",
      virtualSize/(1024*1024),minPhysicialSize/(1024*1024)
    );
    static MemReservedInfo info={0,0};
    return info;
  }
  
  int nPages = virtualSize >> _pageSizeLog;
  int nGroups = nPages>>MemReservedInfo::GroupSizeLog;
  size_t memSize = nGroups*sizeof(MemReservedInfo::FlagsArray::Group);
  
  int headerPages = (memSize + _pageSize-1)>>_pageSizeLog;
  
  // align as required for the commit
  headerPages = (headerPages+CommitPagesUnit-1)&~(CommitPagesUnit-1);
  
  
  // we need to commit the header memory first
  if (::VirtualAlloc(res._data,headerPages<<_pageSizeLog,MEM_COMMIT,PAGE_READWRITE)!=res._data)
  {
    HRESULT hr = GetLastError();
    MemoryErrorReported();
    ErrorMessage("Cannot commit %d pages, error %x",headerPages,hr);
    void ReportMemoryStatus();
    ReportMemoryStatus();
  }
  
  res._flags.Init(res._data,nGroups);
  // all pages are available
  res._flags.SetAll(PageAvailable);
  // but header part is busy - mark as not available
  res._flags.SetRange(0,headerPages,PageLocked);

  _nRes++;
  
  return res;
}

void MemCommit::SetMinCommit(size_t minUsage)
{
  _pagesMinCommited = minUsage>>_pageSizeLog;
  if (_pagesCommited<_pagesMinCommited)
  {
    // commit some more pages
    while (_pagesCommited<_pagesMinCommited)
    {
      int inR;
      int page = FindNoncommitedPage(inR);
      if (page<0 || !CommitPages(inR,page,page+1,false,false))
      {
        size_t released = FreeOnDemandSystemMemory(64*1024);
        if (!released) break;
      }
    }
  }
  else
  {
    // make sure any over commit is released
    ReleaseOvercommit();
  }
}

bool MemCommit::CheckLockedRange(void *startAddr, void *endAddr) const
{
  Fail("Not tested");
  
  // we assume 32b environment here
  COMPILETIME_COMPARE(sizeof(uintptr_t),4)
  int inR = FindRes(startAddr, endAddr);
  DoAssert(inR>=0);
  const MemReservedInfo &res = _res[inR];
  
  uintptr_t start = uintptr_t(startAddr)-uintptr_t(res._data);
  uintptr_t end = uintptr_t(endAddr)-uintptr_t(res._data);
  int iStart = start>>_pageSizeLog;
  int iEnd = (end+_pageSize-1)>>_pageSizeLog;
  bool ret = true;
  for (int i=iStart; i<iEnd; i++)
  {
    unsigned char flag = res._flags[i];
    if ((flag&PageLocked)==0)
    {
      RptF("Page %d:%p not locked (%p..%p)",i,i*_pageSize,start,end);
      ret = false;
    }
  }
  return ret;
}

void MemCommit::LockRange(void *startAddr, void *endAddr)
{
  Fail("Not tested");
  int inR = FindRes(startAddr, endAddr);
  DoAssert(inR>=0);
  MemReservedInfo &res = _res[inR];
  // convert to relative offsets
  uintptr_t start = uintptr_t(startAddr)-uintptr_t(res._data);
  uintptr_t end = uintptr_t(endAddr)-uintptr_t(res._data);
  
  int iStart = start>>_pageSizeLog;
  int iEnd = (end+_pageSize-1)>>_pageSizeLog;
  int changed = 0;
  for (int i=iStart; i<iEnd; i++)
  {
    unsigned char flag = res._flags[i];
    DoAssert(flag&PageAvailable);
    if ((flag&PageLocked)==0)
    {
      flag |= PageLocked;
      changed++;
      res._flags.Set(i,flag);
    }
  }
  _pagesLocked += changed;
}

int MemCommit::DecommitPages(int pages)
{

  // when we are requested to "decommit", it is because someone else want to commit a memory
  // therefore we need to really decommit some physical pages, not only "our" pages
  // with 4K pages this is the same, but on X360 with large pages (64KB) this can be different
  int decomPhysical = 0;
  // how many pages do we still want to decommit
  int decomPages = pages;
  while (decomPhysical<pages)
  {
    
    // we want to decommit the "uppermost address" page
    // by uppermost we mean in the highest index _res block, with highest address in it
    // we scan from lowermost address and count how many pages we have to skip
    int decom = 0;
    // find topmost page that is committed but not locked
    int left = _pagesCommited-_pagesLocked;
    while (left>0 && decomPages>0)
    {
      for (int r=0; r<_nRes; r++)
      {
        MemReservedInfo &res = _res[r];
        int resPages = res._reserved>>_pageSizeLog;
        for (int i=0; i<resPages && left>0; i++)
        {
          // TODO: using FindLast would be a lot more efficient here
          // esp. for 64 KB case
          int first = res._flags.FindFirst(i,resPages,PageCommited,PageLocked);
          if (first<0) break;

          i = first;
          unsigned char flag = res._flags[i];
          DoAssert(flag&PageAvailable);
          DoAssert(flag&PageCommited);
          if (left<=decomPages)
          {
            // we should free this page, as there will not be enough pages with higher addresses
            // check if all other pages in the same big page are already free
            // if not, we cannot call VirtualFree
            int bigPageStart = i&~(CommitPagesUnit-1);
            bool someOtherCommitted = false;
            for (int p = 0; p<CommitPagesUnit; p++)
            {
              if (bigPageStart+p!=i && (res._flags[bigPageStart+p]&PageCommited)!=0){someOtherCommitted=true;break;}
            }

            void *addr = (char *)res._data+(bigPageStart*_pageSize);
            // if some other is still committed, we cannot decommit, but we pretend we did
            // this simulates successful single page decommit
            if (someOtherCommitted || ::VirtualFree(addr,_pageSize*CommitPagesUnit,MEM_DECOMMIT))
            {
              // only when we were performing VirtualFree -  track how many we really decommitted
              if (!someOtherCommitted)
              {
                decomPhysical += CommitPagesUnit;
                // when we have really decommitted, we can reduce decommit requirements for the next iteration
                decomPages -= CommitPagesUnit;
              }
              flag &= ~PageCommited;
              _pagesCommited--;
              decom++;
              res._flags.Set(i,flag);
            }
            else
            {
              LogF("Decommit failed: %x",GetLastError());
              Fail("Decommit failed");
            }
          }
          left--;
          i = first;
        
        }
        
      }
    }
    DoAssert(left==0);
    if (decom<=0) break;
  }
  // scan up for some next page to be decommitted
  return decomPhysical;
}

char *MemCommit::ExtendEnd(char *end, char *extend)
{
  // extend end to nearest up page boundary, if allowed by extend range
  int iEnd = uintptr_t(end)>>_pageSizeLog;
  if (uintptr_t(extend)>=(iEnd+1)*_pageSize)
  {
    return (char *)((iEnd+1)*_pageSize);
  }
  return end;
}
char *MemCommit::ExtendBeg(char *beg, char *extend)
{
  // extend end to nearest down page boundary, if allowed by extend range
  int iBeg = uintptr_t(beg)>>_pageSizeLog;
  if (uintptr_t(extend)<=iBeg*_pageSize)
  {
    return (char *)(iBeg*_pageSize);
  }
  return beg;
}

int MemCommit::FindNoncommitedPage(int &inR) const
{
  inR = -1;
  for (int r=0; r<_nRes; r++)
  {
    const MemReservedInfo &res = _res[r];
    int resPages = res._reserved>>_pageSizeLog;
    int first = res._flags.FindFirst(0,resPages,PageAvailable,PageCommited);
    if (first>=0)
    {
      inR = r;
      return first;
    }
  }
  return -1;
}

void *MemCommit::FindFreeRange(int size, int align) const
{
  //PROFILE_SCOPE_EX(mgFFR,mem)
  int sizeInPages = (size+_pageSize-1)>>_pageSizeLog;
  int alignInPages = (align+_pageSize-1)>>_pageSizeLog;

  // to prevent fragmentation we always search for a best fit
  int bestLen = INT_MAX;
  void *ret = NULL;
  
  for (int r=0; r<_nRes; r++)
  {
    const MemReservedInfo &res = _res[r];
    // find first continuous free range
    // busy page is always locked (non-available pages are always locked)
    // non-locked pages is always available
    int resPages = res._reserved>>_pageSizeLog;
    for (int from=0; from<resPages; )
    {
      // free page is not locked - first first free page
      int free = res._flags.FindFirst(from,resPages,PageAvailable,PageLocked);
      if (free<0)
      {
        break;
      }
      // find first non-free page
      // non-free page is always locked (non-available pages are always locked)
      int busy = res._flags.FindFirst(free+1,resPages,PageLocked,0);
      // if there is none, it means all the rest is free
      if (busy<0) busy = resPages;
      // check if region free..busy is good enough for us
      int freeBeg = (free+alignInPages-1)&~(alignInPages-1);
      int freeEnd = busy&~(alignInPages-1);
      if (freeEnd-freeBeg>=sizeInPages && busy-free<bestLen)
      {
        bestLen = busy-free;
        ret = (char *)res._data + freeBeg*_pageSize;
        // once we have a perfect fit, there is no need to search for any other
        if (sizeInPages==bestLen) return ret;
      }
      
      // we may skip the busy one, we know we are not interested in it
      from = busy+1;
    }
  }
  return ret;
}

int MemCommit::FindLongestRange() const
{
  // find longest continuous free range

  int maxLenght = 0;
  for (int r=0; r<_nRes; r++)
  {
    const MemReservedInfo &res = _res[r];
    int resPages = res._reserved>>_pageSizeLog;
    for (int from=0; from<resPages; )
    {
      int free = res._flags.FindFirst(from,resPages,PageAvailable,PageLocked);
      if (free<0) break;
      // find first non-free page
      // non-free page is always locked (non-available pages are always locked)
      int busy = res._flags.FindFirst(free+1,resPages,PageLocked,0);
      // if there is none, it means all the rest is free
      if (busy<0) busy = resPages;
      int lenght = busy-free;
      if (maxLenght<lenght) maxLenght = lenght;
      
      // we may skip the busy one, we know we are not interested in it
      from = busy+1;
    }
  }
    
  return maxLenght;
}

bool MemCommit::DecommitOnePage()
{
  return DecommitPages(1)>0;
}

bool MemCommit::CheckUnlockedRange(void *startAddr, void *endAddr) const
{
  Fail("Not tested");
  int inR = FindRes(startAddr, endAddr);
  DoAssert(inR>=0);
  const MemReservedInfo &res = _res[inR];
  // convert to relative offsets
  uintptr_t start = uintptr_t(startAddr)-uintptr_t(res._data);
  uintptr_t end = uintptr_t(endAddr)-uintptr_t(res._data);
  
  int iStart = (start+_pageSize-1)>>_pageSizeLog;
  int iEnd = end>>_pageSizeLog;
  bool ret = true;
  for (int i=iStart; i<iEnd; i++)
  {
    int locked = res._flags.FindFirst(i,iEnd,0,PageLocked);
    if (locked<0) break;
    i = locked;
    Assert(res._flags[i]&PageAvailable);
    Assert(res._flags[i]&PageLocked);
    RptF("Page %d:%p locked (%p..%p)",i,i*_pageSize,start,end);
    ret = false;
  }
  return ret;
}

void MemCommit::UnlockRange(void *startAddr, void *endAddr)
{
  int inR = FindRes(startAddr, endAddr);
  DoAssert(inR>=0);
  MemReservedInfo &res = _res[inR];
  // convert to relative offsets
  uintptr_t start = uintptr_t(startAddr)-uintptr_t(res._data);
  uintptr_t end = uintptr_t(endAddr)-uintptr_t(res._data);
  #if 0 //_DEBUG
    // we can destroy the range before decommitting it
    float fStart = (float)start/_pageSize;
    float fEnd = (float)end/_pageSize;
  #endif
  // we may unlock only pages that are whole contained in the start..end range
  int iStart = (start+_pageSize-1)>>_pageSizeLog;
  int iEnd = end>>_pageSizeLog;
  #if 0 //_DEBUG
    // note: part of the range may be already uncommiteed
    char *rangeStart = (char *)Data()+istart;
    char *rangeEnd = (char *)Data()+iend;

    for (int i=iStart-1; i<=iEnd; i++)
    {
      if ((res._flags[i]&PageCommited)==0) continue;
      char *pageStart = (char *)Data()+i*_pageSize;
      char *pageEnd = pageStart+_pageSize;

      char *fillStart = pageStart;
      if (fillStart<rangeStart) fillStart = rangeStart;
      char *fillEnd = pageEnd;
      if (fillEnd>rangeEnd) fillEnd = rangeEnd;
      if (fillEnd>fillStart)
      {
        memset(fillStart,0x55,fillEnd-fillStart);
      }
    }
  #endif
  #if 0 //_ENABLE_REPORT
    char *addrStart = (char *)Data()+iStart*_pageSize;
    int size = (iEnd-iStart)*_pageSize;
    if (iEnd>iStart)
    {
      LogF("Unlock %p..%p (%p..%p)",addrStart,addrStart+size,(char *)Data()+start,(char *)Data()+end);
    }
  #endif
  
  for (int i=iStart; i<iEnd; i++)
  {
    int locked = res._flags.FindFirst(i,iEnd,PageLocked,0);
    if (locked<0) break;
    // when locked, it needs to be committed as well
    // we should never access pages which are not available, as there are never free
    unsigned char flag = res._flags[i];
    Assert(flag==(PageAvailable|PageCommited|PageLocked));
    flag &= ~PageLocked;
    res._flags.Set(i,flag);
    //_flags.Set(i,flag);
    _pagesLocked--;
  }
  // lazy decommitting:
  // we may want to keep some pages commited, but not too many
  int overMax = _pagesCommited-(_maxCommitted>>_pageSizeLog);
  if (overMax>0)
  {
    if (DecommitPages(overMax)!=overMax)
    {
      Fail("Nothing to decommit");
    }
  }
  ReleaseOvercommit();
}

void MemCommit::ReleaseOvercommit()
{
  int overCommit = _pagesCommited-_pagesLocked;
  if (overCommit>=16)
  {
    // we want to make sure some ammount is always commited
    int overCommitMin = _pagesCommited-_pagesMinCommited;
    if (overCommitMin>0)
    {
      int overCommitWanted = _maxCommitted/80/_pageSize;
      // we want to keep around 1/80 overcommit - for 256 MB heap this is 3.2 MB
      // do not care if overcommit is small
      if (overCommit-overCommitWanted>=16)
      {
        if (overCommit>overCommitMin) overCommit=overCommitMin;
        DecommitPages(overCommit);
      }  
    }
  }
}

bool MemCommit::CommitRange( void *startAddr, void *endAddr, bool lock)
{
  int inR = FindRes(startAddr, endAddr);
  DoAssert(inR>=0);
  MemReservedInfo &res = _res[inR];
  // convert to relative offsets
  uintptr_t start = uintptr_t(startAddr)-uintptr_t(res._data);
  uintptr_t end = uintptr_t(endAddr)-uintptr_t(res._data);
  int iStart = start>>_pageSizeLog;
  int iEnd = (end+_pageSize-1)>>_pageSizeLog;
  #if _DEBUG
    for (int i=iStart; i<iEnd; i++)
    {
      Assert(res._flags[i]&PageAvailable);
    }
    // check if pages that are whole contained in the region are not locked yet
    if (lock)
    {
      int startWholeI = (start+_pageSize-1)>>_pageSizeLog;
      int endWholeI = end>>_pageSizeLog;
      for (int i=startWholeI; i<endWholeI; i++)
      {
        if (res._flags[i]&PageLocked)
        {
          RptF("Page %d:%p already locked (%p..%p)",i,i*_pageSize,start,end);
        }
      }
    }
  #endif
  return CommitPages(inR,iStart,iEnd,lock,true);
}

bool MemCommit::CommitPages(int inR, int iStart, int iEnd, bool lock, bool allowDecommit)
{
  MemReservedInfo &res = _res[inR];
  int restToCommit = 0, restToLock = 0;
  for (int i=iStart; i<iEnd; i++)
  {
    unsigned char flag = res._flags[i];
    DoAssert(flag&PageAvailable);
    if ((flag&PageCommited)==0)
    {
      restToCommit++;
      restToLock++;
      Assert((flag&PageLocked)==0);
    }
    else if ((flag&PageLocked)==0)
    {
      // we need to lock this page to make sure it will not get decommitted
      restToLock++;
    }
  }
  if (_maxCommitted<(restToLock+_pagesLocked)*_pageSize)
  {
    // too many pages are already commited and locked - we cannot commit this range
    return false;
  }
  // we may need to decommit some unlocked pages first
  int overCommit = restToCommit+_pagesCommited-(_maxCommitted>>_pageSizeLog);
  // lock pages that are already committed and are in the target range
  // this will prevent decommitting them in the loop below
  int locked = 0;
  for (int i=iStart; i<iEnd; i++)
  {
    unsigned char flag = res._flags[i];
    DoAssert(flag&PageAvailable);
    if ((flag&(PageCommited|PageLocked))==PageCommited)
    {
      flag |= PageLocked|PageLockedTemp;
      res._flags.Set(i,flag);
      _pagesLocked++;
      locked++;
      // we know can can lock the pages, as we already tested restToLock
      Assert(_pagesLocked*_pageSize<=_maxCommitted);
    }
  }
  if (overCommit>0)
  {
    overCommit -= DecommitPages(overCommit);
    Assert(overCommit==0);
  }

  // check if commit is needed based on other pages in the same big page
  int bigPageStart = iStart&~(CommitPagesUnit-1);
  int bigPageEnd = (iEnd+CommitPagesUnit-1)&~(CommitPagesUnit-1);
  
  // first or last big page overlapping the range may already be committed
  // if this is the case, no need to commit it any more
  for (int i=bigPageStart; i<iStart; i++)
  {
    if (res._flags[i]&PageCommited) {bigPageStart += CommitPagesUnit;break;}
  }
  if (bigPageStart<bigPageEnd) for (int i=iEnd; i<bigPageEnd; i++)
  {
    if (res._flags[i]&PageCommited) {bigPageEnd -= CommitPagesUnit;break;}
  }

  if (bigPageEnd>bigPageStart && restToCommit!=0)
  {
    // try to commit whole range
    void *addrStart = (char *)res._data+(bigPageStart*_pageSize);
    int size = (bigPageEnd-bigPageStart)*_pageSize;
    while(::VirtualAlloc(addrStart,size,MEM_COMMIT,PAGE_READWRITE)!=addrStart)
    {
      LogF("Commit failed: %x",GetLastError());
      if (!allowDecommit || !DecommitOnePage())
      {
        // de-commit failed - revert lock flags
        for (int i=iStart; i<iEnd; i++)
        {
          unsigned char flag = res._flags[i];
          DoAssert(flag&PageAvailable);
          if (flag&PageLockedTemp)
          {
            Assert((flag&(PageLocked|PageLockedTemp|PageCommited))==(PageLocked|PageLockedTemp|PageCommited));
            flag &= ~(PageLocked|PageLockedTemp);
            res._flags.Set(i,flag);
            _pagesLocked--;
          }
        }
        return false;
      }
    }
  }
  int commited = 0;
  for (int i=iStart; i<iEnd; i++)
  {
    unsigned char flag = res._flags[i];
    DoAssert(flag&PageAvailable);
    if ((flag&PageCommited)==0)
    {
      Assert((flag&PageLocked)==0);
      commited++;
      _pagesCommited++;
      Assert(_pagesCommited*_pageSize<=_maxCommitted);
      if (lock)
      {
        _pagesLocked++;
        locked++;
      }
      Assert(_pagesLocked*_pageSize<=_maxCommitted);
    }
    else if ((flag&PageLocked)==0)
    {
      if (lock)
      {
        _pagesLocked++;
        locked++;
      }
      Assert(_pagesLocked*_pageSize<=_maxCommitted);        
    }
    else
    { // page is locked
      Assert ((flag&PageLocked)!=0);
      if (!lock)
      {
        // we cannot unlock anything which was already locked before the allocation
        Assert ((flag&PageLockedTemp)!=0);
        _pagesLocked--;
      }
    }
  }
  if (lock)
  {
    // loop before made sure all pages are counted as locked
    res._flags.SetRange(iStart,iEnd,PageCommited|PageLocked|PageAvailable);
  }
  else
  {
    // loop before made sure no pages are counted as locked
    res._flags.SetRange(iStart,iEnd,PageCommited|PageAvailable);
  }
  Assert(restToCommit==commited);
  Assert(restToLock==locked);
  
  return true;
}

int MemCommit::GetReserved() const
{
  int total = 0;
  // return how much is reserved
  for (int i=0; i<_nRes; i++)
  {
    total += _res[i]._reserved;
  }
  return total;
}

bool MemCommit::OwnsPointer(void *p) const
{
  int inR = FindRes(p,(char *)p+1);
  if (inR<0) return false;

  const MemReservedInfo &res = _res[inR];
  uintptr_t start = uintptr_t(p)-uintptr_t(res._data);
  int index = start>>_pageSizeLog;
  (void)index;
#if 0
  //TODO: better Asserts (these are forbidden as always happen after device is lost, for instance after ctrl+alt+del)
  // we seem to manage this memory
  // if someone else is referencing it, it should be available, committed and locked
  Assert(res._flags[index]&PageAvailable);
  Assert(res._flags[index]&PageCommited);
  Assert(res._flags[index]&PageLocked);
#endif
  return true;
}

void *MemCommit::End() const
{
  // TODO: always pretend reserved space spans whole memory, let busy blocks fill the non-reserved regions?
  return (char *)_res[0]._data+_res[0]._reserved;
}

int MemCommit::FindRes(void * startAddr, void * endAddr) const
{
  // find which res block is this in
  uintptr_t start = uintptr_t(startAddr);
  for (int r=0; r<_nRes; r++)
  {
    uintptr_t rStart = uintptr_t(_res[r]._data);
    uintptr_t rEnd = rStart+_res[r]._reserved;
    if (start>=rStart && start<rEnd)
    {
      // verify the whole block is contained
      Assert(uintptr_t(endAddr)<=rEnd);
      return r;
    }
  }
  return -1;
}
#endif

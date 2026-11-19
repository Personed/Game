#include <Es/essencepch.hpp>
#include <Es/platform.hpp>
#include "fastAlloc.hpp"
#include <Es/Containers/typeOpts.hpp>
#include <Es/Containers/staticArray.hpp>
#include <Es/Algorithms/qsort.hpp>

#if CHECK_HEAP_THREAD
#include <Es/Common/win.h>
#endif
//#include "El/Debugging/imexhnd.h"

template <>
TListBidir<FastAlloc::Chunk>::~TListBidir()
{
  if (First())
  {
    Log("Non-empty link list destruct avoided.");
  }
}

// should be included in source file with #pragma init_seg(lib)
// to guarantee initialization before all user variables

//////////////////////////////////////////////////////////////////////////////

FastAlloc::FastAlloc( size_t n, const char *name, int alignOffset, int pageSize )
:esize
(
  #if USE_CHUNK_POINTERS
    n+sizeof(void *)<sizeof(Link) ? sizeof(Link) : n+sizeof(void *)
  #else
    n<sizeof(Link) ? sizeof(Link) : n
  #endif
),
_name(name),
_alignOffset(alignOffset),
//head(0),
allocated(1)
{
  _manualCleanUp = false;
#if CHECK_HEAP_THREAD
  _unsafeHeapThread = 0; //GetCurrentThreadId();
#endif

  // if structure is smaller than 32 it is very likely
  // it does not have any special alignment requirements
  // to conserve memory we will align it to 4B only
  #if defined _KNI || defined _M_PPC
    int alignMem=esize>=16 ? 16 : 4;
  #else
    int alignMem=esize>32 ? 8 : 4;
  #endif
  // alignment is never needed bigger than the alignment of the structure being allocated
  // this is because when member is aligned, the whole structure size is known to be aligned too
  // this avoid any penalty caused by alignment, as esize is not aligned when not needed
  while (alignMem>4)
  {
    // if structure size is aligned to alingMem, alignMem is reasonable
    if (((alignMem-1)&esize)==0) break;
    alignMem >>= 1;
  }
  
  esize=(esize+alignMem-1)&~(alignMem-1);

  _chunkSize = pageSize;
  if (_chunkSize<=0) _chunkSize = GetPageRecommendedSize();

  // compute _alignOffset so that we meet alignment required by alignMem
  
  int memOffset = offsetof(Chunk,mem);
  int alignedOffset = (memOffset + alignMem - 1) &~(alignMem - 1);
  
  _alignOffset += alignedOffset-memOffset;
  
  // if overhead is too big, increase chunk

  for(;;)
  {
    int chSize = ChunkMemSize()-_alignOffset;
    // was written as:
    //int chSize = _chunkSize-sizeof(ChunkHead)-sizeof(TLinkBidirD)-_alignOffset;
    // however, this may be inaccurate depending on ChunkHead and TLinkBidirD alignment

    Assert(chSize>=(int)esize);
    _nElemPerChunk = chSize/esize;
    int overhead = _chunkSize-_nElemPerChunk*esize;
    // check if overhead is acceptable, and if we are free to select page size
    if (overhead*20<=_chunkSize || pageSize>0)
    {
      /*
      LogF
      (
        "%s: overhead %d (%.0f %%), size %d, aligned size %d, Chunk size %d KB, elements %d",
        name,overhead,overhead*100.0f/_chunkSize,n,esize,_chunkSize/1024,_nElemPerChunk
      );
      */
      break;
    }
    // try larger chunk
    _chunkSize *= 2;
  }
  //Assert(_nElemPerChunk>8);
}

void FastAlloc::ReleaseAll()
{
  if (allocated==0) return;
  // note: automatic destruction of links takes place
  //Log("Destruct %s",_name);

  // check if all chunks may be freed now
  if( --allocated==0 )
  {
    FreeChunks();
    Assert(!chunksBusy.First());
    Assert(!chunksFree.First());
    Assert(!chunksPart.First());
  }
  else
  {
    LogF("FastAlloc %s not free when destructed.",Name());
  }
}

FastAlloc::~FastAlloc()
{
  ReleaseAll();
}

#if CHECK_HEAP_THREAD
void FastAlloc::AssertThread()
{
  AssertSameThread(_unsafeHeapThread);
    }
#endif

#if !_RELEASE
  #define FA_DIAGS 0
#endif

#include "normalNew.hpp"

FastAlloc::Chunk *FastAlloc::NewChunk()
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  Chunk *chunk = (Chunk *)NewPage(_chunkSize,_chunkSize);
  #if !USE_CHUNK_POINTERS
    Assert(((int)chunk&(_chunkSize-1))==0);
  #endif

  ConstructAt(*chunk);
  /*
  #if ALLOC_DEBUGGER && defined _CPPRTTI
    Chunk *chunk=new(FileLine(_name),esize) Chunk;
  #else
    Chunk *chunk=new Chunk;
  #endif
  //LogF("%s: NewChunk %x",_name,chunk);
  */
  return chunk;

}
void FastAlloc::DeleteChunk( Chunk *chunk )
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  #if !USE_CHUNK_POINTERS
    Assert(((int)chunk&(_chunkSize-1))==0);
  #endif
  // explicit destruction
  chunk->~Chunk();
  DeletePage(chunk,_chunkSize);
  //delete chunk;
}

#include "debugNew.hpp"

void FastAlloc::FreeChunks()
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  //Log("FreeChunks %s",_name);
  #if FA_DIAGS
    int size=0;
  #endif
  // free all free chunks
  Chunk *n;
  while ((n=chunksFree.First())!=NULL)
  {
    // all chunks should be empty
    Assert(n->allocated==0);
    #if FA_DIAGS
      size+=sizeof(*n);
    #endif
    chunksFree.Delete(n);
    DeleteChunk(n); // use chunk delete
  }

  // there should be no busy or partially busy chunks
  Assert(chunksPart.First()==NULL);
  while ((n=chunksPart.First())!=NULL)
  {
    #if FA_DIAGS
      size+=sizeof(*n);
    #endif
    chunksPart.Delete(n);
    DeleteChunk(n); // use chunk delete
  }

  // note: there should be no busy chunks now
  Assert(chunksBusy.First()==NULL);
  // free all busy chunks
  while ((n=chunksBusy.First())!=NULL)
  {
    #if FA_DIAGS
      size+=sizeof(*n);
    #endif
    chunksBusy.Delete(n);
    DeleteChunk(n); // use chunk delete
  }

  #if FA_DIAGS
    LogF("FastAlloc of type %s (%d) - %d KB",_name,esize,(size+1023)/1024);
  #endif
}

// such value is big positive as int, and at the same time it is float signalling NaN
#define MEM_NEW_VAL_32 0x7f8f7f8f
#define MEM_FREE_VAL_32 0x7f817f81

/// alternative to memset, filling memory with a 32b value aligned to 32b
static __forceinline void MemSet32(void *dst, int val32, int size)
{
  // assume memory is 32b aligned
  // if not, fill less then required
  int *d = (int *)dst;
  size>>=2;
  while (--size>=0) *d++ = val32;
}

void *FastAlloc::AllocCounted(size_t n)
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  allocated++; // count allocation

  Assert( n<=esize );
  if (chunksPart.First()==NULL && chunksFree.First()==NULL)
  {
    if (!Grow()) return NULL;
  }
  bool wasFree = false;
  Chunk *ch = chunksPart.First();
  if (!ch)
  {
    ch = chunksFree.First();
    wasFree = true;
  }

  if (!ch) return NULL;

  Link *p = ch->head;

  Assert((char *)p>=ch->mem && (char *)p<ch->mem+ChunkMemSize());
  
  ch->head = p->next;
  ch->allocated++;
  
  if (!ch->head)
  {
    // move this chunk to busy chunk list
    Assert (ch->allocated==_nElemPerChunk);
    if (wasFree)
    {
      chunksFree.Delete(ch);
    }
    else
    {
      chunksPart.Delete(ch);
    }
    chunksBusy.Insert(ch);
    //Log("%s: Chunk %x moved to busy",_name,ch);
  }
  else if (wasFree)
  {
    Assert((char *)ch->head>=ch->mem && (char *)ch->head<ch->mem+ChunkMemSize());
  
    Assert(ch->allocated==1);
    chunksFree.Delete(ch);
    chunksPart.Insert(ch);
  }

  #if USE_CHUNK_POINTERS
    *(Chunk **)p=ch;
  #endif
  // mark chunk to link
  #if _DEBUG
    #if USE_CHUNK_POINTERS
      MemSet32(((char *)p)+sizeof(Chunk **),MEM_NEW_VAL_32,esize-sizeof(Chunk **));
    #else
      MemSet32(p,MEM_NEW_VAL_32,esize);
    #endif
  #endif

  return p;
}

void FastAlloc::FreeCounted(void*pAlloc)
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  if (!pAlloc) return;

  #if USE_CHUNK_POINTERS
    Chunk *ch=*(Chunk **)pAlloc;
  #else
    // calculate chunk address using known chunk address requirements
    // caution: may cause portability issues
    int offset = (int)pAlloc&(_chunkSize-1);
    Chunk *ch = (Chunk *)((char *)pAlloc-offset);
  #endif

  if (ch->null1)
  {
    Fail("FastAlloc chunk header corrupted.");
    RptF("Chunk %x, null %x",ch,ch->null1);
    return;
  }

  #if _DEBUG
    #if USE_CHUNK_POINTERS
      MemSet32((char *)pAlloc+sizeof(Chunk **),MEM_FREE_VAL_32,esize-sizeof(Chunk **));
    #else
      MemSet32((char *)pAlloc,MEM_FREE_VAL_32,esize);
    #endif
  #endif

  Link *p=static_cast<Link*>(pAlloc);
  //p--; // link is skipped

  Assert((char *)p>=ch->mem && (char *)p<ch->mem+ChunkMemSize());
  
  // add to head of free links in this chunk
  p->next=ch->head;
  /// wasBusy means no chunk was free or partially free before
  bool wasBusy = ch->head==NULL;
  ch->head=p;
  
  // mark which chunk do we belong to
  p->chunk=ch;

  bool isFree = --ch->allocated==0;
  if (wasBusy)
  {
    // was in busy list
    // move it to free list
    chunksBusy.Delete(ch);
    if (isFree)
    {
      chunksFree.Insert(ch); // this is strange - busy became free (one item per chunk?)
    }
    else
    {
      chunksPart.Insert(ch);
    }
    //Log("%s: Chunk %x moved to free",_name,ch);
  }
  else if (isFree)
  {
    chunksPart.Delete(ch);
    chunksFree.Insert(ch);
  }

  if (!_manualCleanUp)
  {
    // it is not necessary to have two chunks free, one is enough
    int freeCount = chunksFree.Size();
    int partFreeCount = chunksPart.Size();
    if (freeCount>0 && freeCount+partFreeCount>1)
    {
      CleanUp();
    }
  }
  // check if all chunks may be freed now
  if( --allocated==0 ) FreeChunks();
}

bool FastAlloc::Chunk::CheckIntegrity(int chunkSize) const
{
  bool ret = true;
  Link * const * bp = &head;
  for (Link *p = head; p; bp = &p->next, p=p->next)
  {
    if ((char *)p<mem || (char *)p>=mem+chunkSize-offsetof(Chunk,mem))
    {
      LogF("Bad pointer %p on %p",p,bp);
      ret = false;
      break; // if the pointer is bad, there is no way to continue enumeration
    }
  }
  return ret;
}

bool FastAlloc::CheckIntegrity() const
{
  bool ret = true;
  
  for (Chunk *ch = chunksPart.Start(); chunksPart.NotEnd(ch); ch=chunksPart.Advance(ch))
  {
    bool ok = ch->CheckIntegrity(_chunkSize);
    if (!ok)
    {
      LogF("Chunk %p in %s not valid",ch,Name());
      ret = false;
    }
  }
  return ret;
}


FastAlloc *FastAlloc::WhichAllocator( void*pAlloc )
{
  #if USE_CHUNK_POINTERS
    if (!pAlloc) return NULL;
    Chunk *ch=*(Chunk **)pAlloc;
    if( ch->null1!=0 ) return NULL;
    return ch->allocator;
  #else
    Fail("Cannot use WhichAllocator (safe) without chunk pointers");
    return NULL;
  #endif
}

FastAlloc *FastAlloc::WhichAllocatorUnsafe( void*pAlloc, size_t pageSize )
{
  //int pageSize = GetPageRecommendedSize();
  int offset = (int)pAlloc&(pageSize-1);
  Chunk *ch = (Chunk *)((char *)pAlloc-offset);
  DoAssert(ch->null1==0);
  FastAlloc *allocator = ch->allocator;
  DoAssert(allocator->_chunkSize==pageSize);
  return allocator;
}

// free unused chunks
void FastAlloc::CleanUp()
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  // clean-up this one allocator
  // if we have reference count zero, remove the chunk
  // no need to check busyChunks
  for (Chunk *ch = chunksFree.First(); ch; )
  {
    Chunk *next = chunksFree.Next(ch);
    Assert(ch->allocated==0)
    ChunkRemove(ch);
    ch = next;
  }
  // sort chunks by filled
  // most filled should be first
}

/*!
Used in genuineness check when test failed.
Damages internal structures of TrackLLink allocator.
This usually leads to strange error or program crashes, but not very soon.
It should be therefore difficult to track the place causing the dammage.
*/

bool FastAlloc::CheckIfFree(Chunk *chunk, void *data) const
{
  Chunk *chunkVerify = *(Chunk **)data;
  // if item does not contain pointer back to chunk, it must be free
  //if (chunkVerify!=chunk) return true;
  // check if data are from this chunk
  // traverse free list and check if data is in it
  Link *p = chunk->head;
  while (p)
  {
    if (data>=p && data<(char *)p+esize) return true;
    p = p->next;
  }
  // item is not free: chunk must match
  /*
  if (chunkVerify!=chunk)
  {
    __asm nop;
    return true;
  }
  */
  return false;
}

//! free first used chunk in alloc
void Damage_FastAlloc(FastAlloc *alloc)
{
  /*
  FastAlloc::Chunk *chunk = alloc->chunksFree.First(); // check chunk with some free links
  if (!chunk || chunk->allocated==0)
  {
    // there is no free chunk or has no items allocated
    chunk = alloc->chunksBusy.First(); // check busy chunk
  }
  if (!chunk) return;

  // we have to find some non-free block of the chunk
  // use first busy block of the chunk
  // it will become free, but the memory will still be used,
  // which should lead to strange effects soon
  void *p = chunk->mem;
  while (alloc->CheckIfFree(chunk,p))
  {
    // note: freeing free block is not possible, it is not linked back to chunk
    p = (char *)p + alloc->ItemSize();
    if (p>=chunk->mem+FastAlloc::Chunk::size)
    {
      // no dammage can be done
      return;
    }
  }
  alloc->FreeCounted(p);
  */
}

void FastAlloc::ChunkRemove( Chunk *chunk )
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  // chunk may be released
  #if !_RELEASE
  bool found = true;
  for (Chunk *ch=chunksFree.Start(); chunksFree.NotEnd(ch); ch = chunksFree.Advance(ch))
  {
    if (ch==chunk) 
    {
      found = true;
      break;
    }
  }
  Assert(found);
  #endif

  chunksFree.Delete(chunk);
  // remove all items from this chunk from free list
  // no need to remove links - they are not stored anywhere

  DeleteChunk(chunk);
}

bool FastAlloc::Grow()
{
#if CHECK_HEAP_THREAD
  AssertThread();
#endif

  Chunk *chunk=NewChunk(); // use chunk new
  // if we are out of memory, we cannot create a new chunk
  if (!chunk) return false;

  #if !USE_CHUNK_POINTERS
    Assert(((int)chunk&(_chunkSize-1))==0);
  #endif
  chunk->null1=0;

  chunk->allocated=0;
  chunk->allocator=this;

  chunksFree.Insert(chunk);

  char *start=chunk->mem+_alignOffset;
  

  const int nelem= _nElemPerChunk;

  char *last=&start[(nelem-1)*esize];
  for( char*p = start; p<last; p+=esize )
  {
    Link *link = reinterpret_cast<Link*>(p);

    link->next=reinterpret_cast<Link*>(p+esize);
    link->chunk=chunk;
  }
  Link *lastLink = reinterpret_cast<Link*>(last);
  lastLink->next=0;
  lastLink->chunk=chunk;

  chunk->head=reinterpret_cast<Link*>(start);
  return true;
}

// keep track of all FastCAlloc
// it has static (or no) initialization, and therefore it is certainly initialized before objects are added
// (initialization done at compilation time)

const int MaxAllInstancesCount = 256;
static FastCAlloc *AllInstancesData[MaxAllInstancesCount];
static int AllInstancesCount = 0;


static void AllInstancesDelete(int n)
{
  ConstructTraits<FastCAlloc *>::Destruct(AllInstancesData[n]);
  ConstructTraits<FastCAlloc *>::DeleteData(AllInstancesData+n,AllInstancesCount-n,1);
  AllInstancesCount--;
  // make sure deleted item is zeroed - this makes debugging easier
  AllInstancesData[AllInstancesCount] = NULL;
}

static void AllInstancesAdd(FastCAlloc *alloc)
{
  if (AllInstancesCount<MaxAllInstancesCount)
  {
    AllInstancesData[AllInstancesCount++] = alloc;
  }
  else
  {
    Fail("Out of FastCAlloc slots");
  }
}

FastCAlloc::FastCAlloc( size_t n, const char *name )
// note: base class is always constructed before members
#if USE_CHUNK_POINTERS
:base(n,name,12)
#else
:base(n,name,0)
#endif
{
#if CHECK_CHEAP_THREAD
  _cHeapThread = 0;
#endif
  // insert FastCAlloc into FastCAlloc list
  AllInstancesAdd(this);
  //Log("Constructed %s",name);
  #if 0 // _PROFILE
  if ((n&0x1f)==0 && (n&0x3f)!=0)
  {
    LogF("Sizeof %s is %d",name,n);
  }
  #endif
}

FastCAlloc::~FastCAlloc()
{
  
  for( int i=0; i<AllInstancesCount; i++ )
  {
    FastCAlloc *ii=AllInstancesData[i];
    if( ii==this )
    {
      AllInstancesDelete(i);
      return;
    }
  }
  Fail("FastCAlloc not listed?");
}

#if _ENABLE_REPORT && _RELEASE
  #define CALLOC_MEM_FILL 1
#else
  #define CALLOC_MEM_FILL 0
#endif



void *FastCAlloc::CAlloc( size_t n )
{
#if CHECK_CHEAP_THREAD
  AssertSameThread(_cHeapThread);
#endif
  #if USE_CHUNK_POINTERS
    Assert( n+sizeof(void *)<=esize );
    char *counted = (char *)FastAlloc::AllocCounted(n+sizeof(void *));
    if (!counted) return NULL;
    char *ret = counted+sizeof(int); // skip header
    #if CALLOC_MEM_FILL
      MemSet32(ret,MEM_NEW_VAL_32,esize-sizeof(int));
    #endif
    return ret;
  #else
    Assert( n<=esize );
    void *counted = FastAlloc::AllocCounted(n);
    if (!counted) return NULL;
    #if CALLOC_MEM_FILL
      MemSet32(counted,MEM_NEW_VAL_32,esize);
    #endif
    return counted;
  #endif
}

void FastCAlloc::CFree( void*pAlloc )
{
#if CHECK_CHEAP_THREAD
  AssertSameThread(_cHeapThread);
#endif
  if (!pAlloc) return;
  #if USE_CHUNK_POINTERS
    #if CALLOC_MEM_FILL
      MemSet32(pAlloc,MEM_FREE_VAL_32,esize-sizeof(int));
    #endif
    char *counted = (char *)pAlloc-sizeof(int);
    // free but do not clean chunks yet
    FastAlloc::FreeCounted(counted);
  #else
    #if CALLOC_MEM_FILL
      MemSet32(pAlloc,MEM_FREE_VAL_32,esize);
    #endif
    char *counted = (char *)pAlloc;
    // free but do not clean chunks yet
    FastAlloc::FreeCounted(counted);
  #endif
}


size_t FastAlloc::Requested() const
{
  #if USE_CHUNK_POINTERS
  return allocated*(esize-sizeof(void *));
  #else
  return allocated*esize;
  #endif
}
size_t FastAlloc::Allocated() const
{
  // count all busy chunks
  size_t total = 0;
  for (Chunk *ch=chunksBusy.First(); ch; ch=chunksBusy.Next(ch))
  {
    total += _chunkSize;
  }
  // count all free chunks as well
  for (Chunk *ch=chunksFree.First(); ch; ch=chunksFree.Next(ch))
  {
    total += _chunkSize;
  }
  for (Chunk *ch=chunksPart.First(); ch; ch=chunksPart.Next(ch))
  {
    total += _chunkSize;
  }
  return total;
}


void FastCAlloc::CleanUpAll()
{
  #if USE_FAST_ALLOCATORS==1
  for( int i=0; i<AllInstancesCount; i++ )
  {
    AllInstancesData[i]->CleanUp();
  }
  #elif USE_FAST_ALLOCATORS==2
    nedalloc::nedmalloc_trim(0);
  #endif
}

size_t FastCAlloc::TotalRequested()
{
  #if USE_FAST_ALLOCATORS==1
  size_t total = 0;
  for( int i=0; i<AllInstancesCount; i++ )
  {
    total += AllInstancesData[i]->Requested();
  }
  return total;
  #elif USE_FAST_ALLOCATORS==2
    return nedalloc::nedmalloc_footprint();
  #else
    return 0;
  #endif
}
size_t FastCAlloc::TotalAllocated()
{
  #if USE_FAST_ALLOCATORS==1
  size_t total = 0;
  for( int i=0; i<AllInstancesCount; i++ )
  {
    total += AllInstancesData[i]->Allocated();
  }
  return total;
  #elif USE_FAST_ALLOCATORS==2
    return nedalloc::nedmalloc_footprint();
  #else
    return 0;
  #endif
}

#if _ENABLE_REPORT
int CompareFAAllocated(const FastCAlloc * const *a1, const FastCAlloc * const *a2)
{
  return (*a2)->CompareAllocatedSize(**a1);
}



void FastCAlloc::ReportTotals()
{
  AUTO_STATIC_ARRAY(const FastCAlloc *,stats,MaxAllInstancesCount);
  for( int i=0; i<AllInstancesCount; i++ )
  {
    const FastCAlloc *alloc = AllInstancesData[i];
    if (alloc->Allocated()>0)
    {
      stats.Add(alloc);
    }
  }
  QSort(stats.Data(),stats.Size(),CompareFAAllocated);

  for( int i=0; i<stats.Size(); i++ )
  {
    const FastCAlloc *alloc = stats[i];
    LogF(
      "%10s: requested %d, allocated %d",
      (const char *)alloc->Name(),alloc->Requested(),alloc->Allocated()
    );
  }

}

#endif

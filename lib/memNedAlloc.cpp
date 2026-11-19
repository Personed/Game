// global redefinition of new, delete operators
#include "wpch.hpp"

#if defined _WIN32 && !defined MALLOC_WIN_TEST

#pragma optimize("t",on)
#if !_RELEASE
  // it seems in VS 2005 optimize("t",on) applies /Oy as well (Omit frame pointers)
  // in Testing we need stack frames for memory call-stack and crash-dumps
  #pragma optimize("y",off)
#endif

#include "memHeap.hpp"

#ifdef _MEM_NED_ALLOC_HPP // depending on memHeap.hpp content enable or disable whole implementation

#if _DEBUG || _PROFILE
  #define USE_MV 0
  #define USE_MV_USER 0 // TODO: when using Windows allocator, there is no need to use the API
#endif

//#include "global.hpp"
#include <Es/Common/win.h>
#include <Es/Files/commandLine.hpp>
#include <El/QStream/qbStream.hpp>
#include <El/Debugging/debugTrap.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <Es/Containers/staticArray.hpp>

#if _MSC_VER && !defined INIT_SEG_COMPILER
  // we want Memory Heap to deallocate last
  #pragma warning(disable:4074)
  #pragma init_seg(compiler)
  #define INIT_SEG_COMPILER
#endif

AtomicInt TotalAllocatedByActualAlloc;

struct MallocImplWin
{
  static size_t __stdcall MemTotalCommitted() {return TotalAllocatedByActualAlloc;}
  static size_t __stdcall MemTotalReserved() {return TotalAllocatedByActualAlloc;}
  static size_t __stdcall MemFlushCache(size_t size) {return 0;}
  static void __stdcall MemFlushCacheAll() {}
  static size_t __stdcall MemSize(void *mem) {return _aligned_msize(mem,16,0);}
  static void *__stdcall MemAlloc(size_t size) {return _aligned_malloc(size,16);}
  static void __stdcall MemFree(void *mem) {_aligned_free(mem);}
};

static struct MallocImplDll
{
  private:
  bool _init;
  HMODULE _dll;

  public:
  bool Initialized() const {return _init;}

  MallocImplDll();
  bool Load(const char *name);

  const char *Name() const;

  size_t (__stdcall *_TotalCommitted)();
  size_t (__stdcall *_TotalReserved)();
  size_t (__stdcall *_FreeSystemMemory)(size_t size);
  void (__stdcall *_CleanUp)();
  size_t (__stdcall *_MSize)(void *mem);
  void *(__stdcall *_MemAlloc)(size_t size);
  void (__stdcall *_MemFree)(void *mem);

  MemSize TotalCommitted(){return _TotalCommitted();}
  MemSize TotalReserved(){return _TotalReserved();}
  MemSize FreeSystemMemory(size_t size){return _FreeSystemMemory(size);}
  void CleanUp(){_CleanUp();}
  size_t MSize(void *mem){return _MSize(mem);}
  void *MemAlloc(size_t size){return _MemAlloc(size);}
  void MemFree(void *mem){return _MemFree(mem);}
  void ThreadDetach(){} // Dll can implement thread detach easily using DLL_THREAD_DETACH
} GMallocImpl;

template <class Type>
inline void GetFunctionAddress(Type *&res, HMODULE mod, const char *name)
{
  res = (Type *)GetProcAddress(mod,name);
};

MallocImplDll::MallocImplDll():_init(false)
{
  _TotalCommitted = MallocImplWin::MemTotalCommitted;
  _TotalReserved = MallocImplWin::MemTotalReserved;
  _FreeSystemMemory = MallocImplWin::MemFlushCache;
  _CleanUp = MallocImplWin::MemFlushCacheAll;
  _MSize = MallocImplWin::MemSize;
  _MemAlloc = MallocImplWin::MemAlloc;
  _MemFree = MallocImplWin::MemFree;

}


bool MallocImplDll::Load(const char *name)
{
  // the name must not contain any dots (prevent accessing relative paths)
  if (strchr(name,'.') || strchr(name,'\\'))
  {
    #if !_ENABLE_CHEATS
      return false;
    #else
      _dll = LoadLibrary(name);
    #endif
  }
  else
  {
  BString<512> fullName;
  // the name should be a short form only, provide both folder and extension
  strcpy(fullName,"dll\\");
  strcat(fullName,name);
  strcat(fullName,".dll");
  _dll = LoadLibrary(fullName);
  }

  if (!_dll) return false;

  size_t (__stdcall *temp_TotalCommitted)();
  size_t (__stdcall *temp_TotalReserved)();
  size_t (__stdcall *temp_FreeSystemMemory)(size_t size);
  void (__stdcall *temp_CleanUp)();
  size_t (__stdcall *temp_MSize)(void *mem);
  void *(__stdcall *temp_MemAlloc)(size_t size);
  void (__stdcall *temp_MemFree)(void *mem);

  GetFunctionAddress(temp_TotalCommitted,_dll,"_MemTotalCommitted@0");
  GetFunctionAddress(temp_TotalReserved,_dll,"_MemTotalReserved@0");
  GetFunctionAddress(temp_FreeSystemMemory,_dll,"_MemFlushCache@4");
  GetFunctionAddress(temp_CleanUp,_dll,"_MemFlushCacheAll@0");
  GetFunctionAddress(temp_MSize,_dll,"_MemSize@4");
  GetFunctionAddress(temp_MemAlloc,_dll,"_MemAlloc@4");
  GetFunctionAddress(temp_MemFree,_dll,"_MemFree@4");
  if (temp_TotalCommitted && temp_TotalReserved && temp_FreeSystemMemory && temp_CleanUp && temp_MSize && temp_MemAlloc && temp_MemFree)
  {
    _TotalCommitted = temp_TotalCommitted;
    _TotalReserved = temp_TotalReserved;
    _FreeSystemMemory = temp_FreeSystemMemory;
    _CleanUp = temp_CleanUp;
    _MSize = temp_MSize;
    _MemAlloc = temp_MemAlloc;
    _MemFree = temp_MemFree;
    _init = true;
  }
  return _init;
}

const char *MallocImplDll::Name() const
{
  if (_dll)
  {
    static BString<256> name;
    GetModuleFileName(_dll,unconst_cast(name.cstr()),256-1);
    return name;
  }
  else
  {
    return "Windows";
  }
}

static struct SelectMallocImpl
{
  SelectMallocImpl()
  {
    CommandLine cmd(GetCommandLine());
    cmd.SkipArgument(); // skip exe name
    struct MemImplArgumentContext
    {
      BString<512> dllName;
    };
    MemImplArgumentContext ctx;
    struct MemImplArgument
    {
      bool operator () (const CommandLine::SingleArgument &arg, MemImplArgumentContext &ctx)
      {
        // check if arg is recognized
        const char *beg = arg.beg;
        const char *end = arg.end;

        if( end==beg ) return false;
        if( *beg=='-' || *beg=='/' )
        {
          beg++;
          // option
          static const char malloc[]="malloc=";
          if( !strnicmp(beg,malloc,strlen(malloc)) )
          {
            const char *valBeg = beg+strlen(malloc);
            strncpy(ctx.dllName,valBeg,end-valBeg);
            if (end-valBeg<256) ctx.dllName[end-valBeg]=0;
          }
        }
        return false;
      }
    };

    ForEach<CommandLineForEachTraits>(cmd,MemImplArgument(),ctx);
    if (ctx.dllName[0]!=0)
    {
      GMallocImpl.Load(ctx.dllName);
    }

    if (!GMallocImpl.Initialized()) // no explicit preference, or explicit preference did not work
    {
      if (!strcmp(ctx.dllName,"system"))
      {
        OutputDebugString("Using system memory allocator\n");
      }
      else
      {
      static const char *dllNames[]=
      {
        // TBB was selected as the best in forum poll http://forums.bistudio.com/showthread.php?t=121455
          "tbb4malloc_bi","tbb3malloc_bi","jemalloc_bi","tcmalloc_bi","nedmalloc_bi","custommalloc_bi"
      };
      for (int i=0; i<lenof(dllNames); i++)
      {
        if (GMallocImpl.Load(dllNames[i]))
        {
          break;
        }
      }
      if (!GMallocImpl.Initialized())
      {
        // note: we cannot use RptF yet
        OutputDebugString("Unable to load memory allocator dll, falling back to Windows allocator\n");
      }
    }
  }
  }
} SSelectMallocImpl;


#include "memGrow.hpp"

#if 1
#define PROFILE_SCOPE_MEM(name) PROFILE_SCOPE_EX(name,mem)
#else
#define PROFILE_SCOPE_MEM(name)
#endif

//#include "engineDll.hpp"

#include <Es/Memory/normalNew.hpp>

// note: MEM_CHECK makes sure leaks are detected, but if leak source tracking is needed
// you need to use ALLOC_DEBUGGER

#if _TESTING
  #define DO_MEM_FILL 1
  #define MEM_CHECK 1
  #define DO_MEM_STATS 1
#elif _PROFILE
  #define MEM_CHECK 0
  #define DO_MEM_STATS 0
  #define DO_MEM_FILL 0
#elif _DEBUG
  #define DO_MEM_FILL 0
  #define MEM_CHECK 0
  #define DO_MEM_STATS 0
#else // _RELEASE
  #define MEM_CHECK 0
  #define DO_MEM_STATS 0
  #if _ENABLE_REPORT
    #define DO_MEM_FILL 1
  #else
    #define DO_MEM_FILL 0
  #endif
#endif


// when using byte, we have little to choose from
#define MEM_GUARD_VAL 0x6a
#define MEM_FREE_VAL 0x7f
#define MEM_NEW_VAL 0x71

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


/// should the primary memory heap should be multi-thread safe?
#define MAIN_HEAP_MT_SAFE 1

//{ useAppFrameExt.cpp is embedded here:
//  - LogF is used in MemHeap construction, so it must be defined in compiler init_seg

#include "appFrameExt.hpp"
static OFPFrameFunctions GOFPFrameFunctions INIT_PRIORITY_URGENT;
AppFrameFunctions *CurrentAppFrameFunctions = &GOFPFrameFunctions;

//} End of embedded useAppFrameExt.cpp

#ifndef MemAllocDataStack
  /// define a store for MemAllocDataStack
  DataStack GDataStack(1*1024*1024);
#endif


#ifndef MFC_NEW

#if MEM_CHECK || DO_MEM_STATS
#include "memCheck.hpp"
#endif


#include <El/Debugging/imexhnd.h>


#if defined(_WIN32) && _ENABLE_REPORT && !MAIN_HEAP_MT_SAFE

static DWORD UnsafeHeapThread=GetCurrentThreadId();

static void MFSafetyBroken()
{
  Fail("MT safety broken");

#if _ENABLE_REPORT
  CONTEXT context;
  context.ContextFlags = CONTEXT_FULL;
  RtlCaptureContext(&context);
  GDebugExceptionTrap.ReportContext("MT SAFETY BROKEN", &context, true);
  TerminateProcess(GetCurrentProcess(), 1);    
#endif
}
#define AssertThread() \
  if(GetCurrentThreadId()!=UnsafeHeapThread && UnsafeHeapThread) MFSafetyBroken()

#else

#define AssertThread()

#endif



#if DO_MEM_STATS
  // check new call point histogram
  #include <El/Statistics/statistics.hpp>
  #include "keyInput.hpp"
  #include "dikCodes.h"

  //! allocation count statistics
  #if USE_MEM_COUNT
    static StatisticsByName MemCntStats INIT_PRIORITY_URGENT;
  #endif

  #if USE_MEM_FILENAME
    //! file based statistics
    static StatisticsByName MemTotStats INIT_PRIORITY_URGENT;
  #else if USE_MEM_CALLSTACK_CP
    static StatisticsById MemTotStats INIT_PRIORITY_URGENT;
  #endif
  #if USE_MEM_CALLSTACK_CS
    //! call stack based statistics
    static StatisticsByName MemCSStats INIT_PRIORITY_URGENT;
  #endif
  #if USE_MEM_CALLSTACK_CP
    //! call stack based statistics
    static StatisticsById MemCPStats INIT_PRIORITY_URGENT;
  #endif
  #if USE_MEM_CALLSTACK_CL
    //! call stack based statistics
    static StatisticsById MemCLStats INIT_PRIORITY_URGENT;
  #endif

  //! avoid recursion
  static AtomicInt MemStatsDisabled(0);

  void ReportMemory();
  void ReportMemoryTotals();

  void MemoryFootprintSummary()
  {
    //MemTotStats.Clear();
    // scan all allocated blocks
    for
    (
      MemoryInfo *info=PAllocated->First(); info; info=PAllocated->Next(info)
    )
    {
      #if USE_MEM_FILENAME
        char buf[128];
        _snprintf(buf,sizeof(buf),"%s(%d): S",info->File(),info->Line());
        buf[sizeof(buf)-1]=0;
        MemTotStats.Count(buf,info->Size());
      #else if USE_MEM_CALLSTACK_CP
        if (info->CallstackSize()>0)
        {
          void *callplace = info->Callstack()[0];
          void *caller = 0;
          MemTotStats.Count(
            StatisticsById::Id((int)callplace,(int)caller),
            info->Size()
          );
        }
      #endif
      #if USE_MEM_CALLSTACK_CS
        MemCSStats.Count(info->Callstack(),info->CallstackSize()*sizeof (void *),info->Size());
      #endif
      #if USE_MEM_CALLSTACK_CP
        for (int i=0; i<info->CallstackSize(); i++)
        {
          void *callplace = info->Callstack()[i];
          void *caller = i<info->CallstackSize()-1 ? info->Callstack()[i+1] : 0;
          //void *caller = 0;
          MemCPStats.Count(
            StatisticsById::Id((int)callplace,(int)caller),
            info->Size()
          );
        }
      #endif
      #if USE_MEM_CALLSTACK_CL
        for (int i=0; i<info->CallstackSize(); i++)
        {
          void *callplace = info->Callstack()[i];
          void *caller = 0;
          MemCLStats.Count(
            StatisticsById::Id((int)callplace,(int)caller),
            info->Size()
          );
        }
      #endif
    }
    #if USE_MEM_FILENAME || USE_MEM_CALLSTACK_CP
      MemTotStats.Sample();
    #endif
    #if USE_MEM_CALLSTACK_CS
      MemCSStats.Sample();
    #endif
    #if USE_MEM_CALLSTACK_CP
      MemCPStats.Sample();
    #endif
    #if USE_MEM_CALLSTACK_CL
      MemCLStats.Sample();
    #endif
    ReportMemory();
  }
  void MemoryFootprint()
  {
    MemoryFootprintSummary();
    ReportMemoryTotals();
  }

  void CountNew( const char *file, int line, int size )
  {
    #if USE_MEM_COUNT
      if( MemStatsDisabled>0 ) return;
      MemStatsDisabled++;
      char buf[128];
      _snprintf(buf,sizeof(buf),"%s(%d): C",file,line);
      buf[sizeof(buf)-1]=0;
      MemCntStats.Count(buf);
      // scan all allocated blocks and add them to memory footprint
      MemStatsDisabled--;
    #endif
  }

  
  #if USE_MEM_CALLSTACK_CS
  void MemIdCallbackCS(const void *id, int idSize, int count)
  {
    // convert call stack to text 
    // report only big allocations
    void * const *ids = (void * const *)id;
    int idsSize = idSize/sizeof(void *);
    for (int i=0; i<idsSize; i++)
    {
      void *idcs = ids[i];
      // translate id to text representation
      const char *txt = GMapFile.MapNameFromPhysical((int)idcs);
      LogF("%32s: CS %6d",txt,count);
    }
    //sprintf(buf,"%32s: %6d\r\n",_data[i].name,_data[i].count/_factor);
    //f.write(buf,strlen(buf));
  }
  #endif

  #if USE_MEM_CALLSTACK_CP || USE_MEM_CALLSTACK_CL
  void MemIdCallbackCP(StatisticsById::Id id, int count)
  {
    // convert call stack to text 
    // report only big allocations
    BString<1024> callPair;
    int id1Start;
    const char *name1 = GMapFile.MapNameFromPhysical(id._id1,&id1Start);
    sprintf(callPair,"%X:%s",id._id1,name1);
    if (id._id2)
    {
      int id2Start;
      const char *name2 = GMapFile.MapNameFromPhysical(id._id2,&id2Start);
      strcat(callPair," <- ");
      sprintf(callPair+strlen(callPair),"%X:%s",id._id2,name2);
    }
    LogF("%90s: CP %6d",cc_cast(callPair),count);
    //sprintf(buf,"%32s: %6d\r\n",_data[i].name,_data[i].count/_factor);
    //f.write(buf,strlen(buf));
  }
  #endif

  
  void ReportMemory()
  {
    MemStatsDisabled++;

    #if USE_MEM_FILENAME
      LogF("Allocation footprint report (%d samples)",MemTotStats.NSamples());
      LogF("-----------------------");
      MemTotStats.Report(64*1024);
      MemTotStats.ReportTotal();
    #else if USE_MEM_CALLSTACK_CP
      LogF("Allocation footprint report (%d samples)",MemTotStats.NSamples());
      LogF("-----------------------");
      MemTotStats.Report(MemIdCallbackCP,64*1024);
      MemTotStats.ReportTotal();
    #endif
    #if USE_MEM_CALLSTACK_CS
      MemCSStats.Report(MemIdCallbackCS,128*1024);
    #endif
    #if USE_MEM_CALLSTACK_CP
      LogF("Allocation call-pair report (%d samples)",MemCPStats.NSamples());
      LogF("-----------------------");
      MemCPStats.Report(MemIdCallbackCP,128*1024);
    #endif
    #if USE_MEM_CALLSTACK_CL
      LogF("Allocation call-place report (%d samples)",MemCLStats.NSamples());
      LogF("-----------------------");
      MemCLStats.Report(MemIdCallbackCP,128*1024);
    #endif
    #if USE_MEM_COUNT
      LogF("Allocation count report");
      LogF("-----------------------");
      MemCntStats.Report(100);
    #endif
    
    #if USE_MEM_FILENAME
      MemTotStats.Clear();
    #endif
    #if USE_MEM_CALLSTACK_CS
      MemCSStats.Clear();
    #endif
    #if USE_MEM_CALLSTACK_CP
      MemCPStats.Clear();
    #endif
    #if USE_MEM_CALLSTACK_CL
      MemCLStats.Clear();
    #endif
    #if USE_MEM_COUNT
      MemCntStats.Clear();
    #endif

    MemStatsDisabled--;
  }

#else
  void ReportMemoryTotals();
  void MemoryFootprint()
  {
    #if USE_MEM_CALLSTACK_CP
    ReportAllocated();
    #endif
    ReportMemoryTotals();
  }
  #define ReportMemory() GMemFunctions->ReportMemory(){}

  #define CountNew(file,line,size)
#endif

static bool MemoryErrorState = false;

void MemoryErrorReported()
{
  MemoryErrorState = true;
}


// manage memory pool
// memory pool is reserved when application is started
// and committed as necessary

// maybe we could maintain order of free blocks
// by address (this will speed-up Free) (est. 2x)
// by size (this will speed-up Alloc) (est. 2x)

int MemoryFreeBlocks();

class MemHeap;
class MemHeapLocked;

#if MAIN_HEAP_MT_SAFE
  #define MemHeapType MemHeapLocked
#else
  #define MemHeapType MemHeap
#endif


//extern MemHeap *SMemPtr;
extern MemHeapType *BMemPtr;



void MemoryErrorPage(size_t size);

#if 0 // _ENABLE_REPORT 

  #define VerifyStructure() DoAssert( CheckIntegrity() )

#else

  #define VerifyStructure()

#endif

// Chunk::new is now directed to region memory, and following is no longer true:
// memory allocated by smallAlloc would appear in statistics twice
// because Alloc calls Chunk::new to allocate Chunks
#if MEM_CHECK 
// for small alloc leak debugging use SMALL_ALLOC 0
#define SMALL_ALLOC 1
#else
#define SMALL_ALLOC 1
#endif

void MemHeap::DoConstruct(MemHeap **lateDestructPointer)
{
  _freeVMRegion = NULL;
  _name = "No";
  _lateDestruct = lateDestructPointer;
}

#include <Es/Memory/normalNew.hpp>

/*!
\patch 5142 Date 3/20/2007 by Ondra
- Fixed: Reduced virtual address space usage on computers with 1 GB or more of RAM.
*/


void MemHeap::DoConstruct
(
  const char *name, MemSize size, MemSize align,
  MemHeap **lateDestructPointer
)
{
  _freeVMRegion = NULL;
  const MemSize oneMB = 1024*1024;
  LogF("MemHeap %s constructed (%d)",name,size);
  _name = name;

  _destruct=false;
  
  _shared._leftToReserve = size;
  // TODO: dynamic adding blocks as long as they are needed
  //totalLeft -= reserved._reserved;
  int allocSize = intMin(16*oneMB,_shared._leftToReserve);
  const MemReservedInfo &info = _memRegions.ReserveWithMult(allocSize/2,allocSize,1);
  // report failure
  if (info._reserved<allocSize)
  {
    RptF("Heap init failed, %d wanted, %d allocated",allocSize,allocSize-info._reserved);
  }
  _shared._leftToReserve -= info._reserved;
  _shared._reserveGranularity = 16*oneMB;
  
  _allocatedDirect = 0;
  
  _lateDestruct = lateDestructPointer;
}

void MemHeap::DoDestruct()
{
  LogF("Destruct memory heap %s",_name);
  // all memory should be deallocated by now

  if (_lateDestruct)
  {
    *_lateDestruct = NULL; // nothing to diagnose any more - memory is gone
    Log("**** All memory released - memory checker deinitialized *****");
  }
}

bool MemHeap::CheckIntegrity() const
{
  #if 1
    static int seldom = 0;
    if (seldom++<50) return true;
    seldom = 0;
  #endif
  if (!Check())
  {
    return false;
  }
  return true;
}

size_t MemHeap::FreeSystemMemory(size_t size)
{
  int pageSize = _memRegions.GetPageSize();
  size_t decommited = 0;
  {
    int nPages = (size+pageSize-1)/pageSize;
    decommited += _memRegions.DecommitPages(nPages)*pageSize;
  }
  if (decommited<size)
  {
    decommited += GMallocImpl.FreeSystemMemory(size);
  }
  return decommited;
}

size_t MemHeap::TotalLowLevelMemory() const
{
  size_t total = 0;
  for(IMemoryFreeOnDemand *walk=MemHeap::GetFirstFreeOnDemandLowLevel(); walk; walk = MemHeap::GetNextFreeOnDemandLowLevel(walk))
  {
    total += walk->MemoryControlled();
  }
  return total;
}

//#pragma optimize ("a",off)

// Alloc and Free are used only within operator new/delete
// inlining avoids using 1:1 call

#if _RELEASE && _ENABLE_PERFLOG
  #define MEM_PERF_LOG 1
#endif

#if MEM_PERF_LOG
  #include <El/Common/perfLog.hpp>
  #include <El/Common/perfProf.hpp>
#endif

MemHeap::~MemHeap()
{
  DoDestruct();
}

/**
thread safe. When two threads are executing this simultaneously, results may be too pessimistic, but stay correct.
*/
bool MemHeap::CheckVirtualFree(size_t size)
{
  if (_freeVMRegion)
  {
    MEMORY_BASIC_INFORMATION buffer;
    PROFILE_SCOPE_EX(vmVer,mem);
    SIZE_T retSize = VirtualQuery(_freeVMRegion,&buffer,sizeof(buffer));
    if (retSize==sizeof(buffer) && buffer.State==MEM_FREE && buffer.RegionSize>=size)
    { // we have verified the block is still free and still large enough - return
      return true;
    }
  }
    
  // the block not free or not large enough, try to find a new one    
  PROFILE_SCOPE_EX(vmVAl,mem);
  void *alloc = VirtualAlloc(NULL,size,MEM_RESERVE,PAGE_NOACCESS);
  if (alloc)
  {
    VirtualFree(alloc,0,MEM_RELEASE);
    _freeVMRegion = alloc;
    return true;
  }
  return false;
}

void MemHeap::BeginUse(size_t minUsage)
{
  // TODO: what is this for?
}
void MemHeap::EndUse()
{
} 

#if defined USE_MALLOC && USE_MALLOC
#error malloc allocation is used - using MemTable makes no sense.
#endif

MemHeap::MemHeap
(
  const char *name, MemSize size, MemSize align,
  MemHeap **lateDestructPointer
)
:_memRegions(_shared)
{
  _vspaceMultiplier = 3;
  DoConstruct(name,size,align,lateDestructPointer);
}
MemHeap::MemHeap(MemHeap **lateDestructPointer)
:_memRegions(_shared)
{
  _vspaceMultiplier = 3;
  DoConstruct(lateDestructPointer);
}

#if 1

static int AllocCounter = 0;
static int FreeCounter = 0;
static int BusyBlockCounter = 0;

void CountAlloc()
{
  AllocCounter++;
  BusyBlockCounter++;
}
void CountFree()
{
  FreeCounter++;
  BusyBlockCounter--;
}

#else

#define CountAlloc()
#define CountFree()

#endif

extern RefD<MemHeapLocked> safeHeap;

/** Check if given heap owns this memory
*/
int MemHeap::IsFromHeap( void *mem ) const
{
  // we need to check all memory owner by this heap - both normal and regions
  if (_memRegions.OwnsPointer(mem)) return 1;
  //BusyBlock *busy=(HeaderType *)mem-1;
  //if( busy<_memBlocks.Data() || busy->EndBusy()>(char *)_memBlocks.Data()+_memBlocks.Size() ) return false;
  return -1;
}


void *MemHeap::AllocRegion(MemSize size, MemSize align)
{
  #if MEM_PERF_LOG
  PROFILE_SCOPE_EX(memAR,mem);
  #endif
  Assert((size&(_memRegions.GetPageSize()-1))==0);
  // region is always allocated from a separate heap
  void *offset = _memRegions.FindFreeRange(size,align);
  if (!offset)
  {
    // first of all check if we can extend the allocated range
    if (_shared._leftToReserve>(int)size)
    {
      // we can extend significantly, let us do it
      int allocSize = intMin(_shared._reserveGranularity,_shared._leftToReserve);
      const MemReservedInfo &info = _memRegions.Reserve(allocSize,size,allocSize);
      if (info._reserved>=(int)size)
      {
        // we have reserved enough, we will use it
        offset = _memRegions.FindFreeRange(size,align);
      }
    }

    if (!offset)
    {
      LogF("%s: cleaning-up (pages)",_name);
      CleanUp();
      offset = _memRegions.FindFreeRange(size,align);
      if (!offset)
      {
        return NULL;
      }
    }
  }
  do
  {
    if (_memRegions.CommitRange(offset,(char *)offset+size))
    {
      return offset;
    }
    // if the request failed, try to decommit some page
  } while (false /*_memBlocks.DecommitOnePage()*/);
  return NULL;
}
void MemHeap::FreeRegion(void *region, MemSize size)
{
  // release single memory page
  _memRegions.UnlockRange(region,(char *)region+size);
  //ReleaseBlock(region);
}



size_t MemHeap::GetRegionRecommendedSize() const
{
  return _memRegions.GetPageSize();
}

void MemHeap::CleanUp()
{
  GMallocImpl.CleanUp();
}

/* struct nedmallinfo 
  size_t arena;    // non-mmapped space allocated from system
  size_t ordblks;  // number of free chunks
  size_t hblkhd;   // space in mmapped regions
  size_t usmblks;  // maximum total allocated space
  size_t uordblks; // total allocated space
  size_t fordblks; // total free space
  size_t keepcost; // releasable (via malloc_trim) space
  NedMalloc Invariant: info.area + info.hblkhd == info.uordblks + info.fordblks == nedmalloc_footprint()
*/

MemSize MemHeap::TotalAllocated() const
{
  size_t footprint = TotalAllocatedByActualAlloc;
  return footprint  + _memRegions.GetLocked() + _allocatedDirect;
}
MemSize MemHeap::TotalCommitted() const
{
  size_t committed = GMallocImpl.TotalCommitted();
  return committed  + _memRegions.GetCommitted() + _allocatedDirect;
}

MemSize MemHeap::TotalReserved() const
{
  size_t reserved = GMallocImpl.TotalReserved();
  return reserved + _memRegions.GetReserved() + _allocatedDirect;
}

MemSize MemHeap::CommittedByNew() const
{
  return GMallocImpl.TotalCommitted();
}

MemSize MemHeap::ReservedByNew() const
{
  return GMallocImpl.TotalReserved();
}

#if _ENABLE_REPORT

bool MemHeap::Check() const
{
  bool ret = true;
  return ret;
}

void MemHeap::ReportTotals() const
{
  LogF("Total via new %d",TotalAllocatedByActualAlloc);
  LogF("Reserved by malloc %d",TotalReserved());
  LogF("Committed by malloc %d",TotalCommitted());
  LogF("Allocated by malloc %d",TotalAllocated());
  LogF("Overhead by malloc %d",TotalCommitted()-TotalAllocatedByActualAlloc);
  LogF("Total locked in pages: %d",_memRegions.GetLocked());
}

void MemHeap::ReportAnyMemory(const char *reason)
{
  // may be called only from the main thread, because it is using the system allocations as well
  AssertThread();

  // heap is already locked if needed - we are in a member funtion  
  typedef IMemoryFreeOnDemand *(MemHeap::*GetFirstMemoryFreeOnDemandF)() const;
  typedef IMemoryFreeOnDemand *(MemHeap::*GetNextMemoryFreeOnDemandF)(IMemoryFreeOnDemand *cur) const;
  const static struct 
  {
    GetFirstMemoryFreeOnDemandF first;
    GetNextMemoryFreeOnDemandF next;
    const char *name;
  } onDemandFcs[]=
  {
    {&MemHeap::GetFirstFreeOnDemand,&MemHeap::GetNextFreeOnDemand,"Heap"},
    {&MemHeap::GetFirstFreeOnDemandSystem,&MemHeap::GetNextFreeOnDemandSystem,"Sys"},
    {&MemHeap::GetFirstFreeOnDemandLowLevel,&MemHeap::GetNextFreeOnDemandLowLevel,"LL"},
  };

  if (reason) RptF("On demand memory report - %s[[[[ ",reason);

  for (int i=0; i<sizeof(onDemandFcs)/sizeof(*onDemandFcs); i++)
  {
    size_t total = 0;
    for
    (
      IMemoryFreeOnDemand *mem = (this->*onDemandFcs[i].first)();
      mem;
      mem = (this->*onDemandFcs[i].next)(mem)
    )
    {
      size_t memSize = mem->MemoryControlled();
      RString memUsed = FormatByteSize(memSize);
      total += memSize;
      RptF(
        "%s - %s allocated %s",
        onDemandFcs[i].name,
        (const char *)mem->GetDebugName(),
        (const char *)memUsed
      );
    }
    RString memTotal = FormatByteSize(total);
    RptF("** Total %s: %s",onDemandFcs[i].name,cc_cast(memTotal));
  }
  if (reason) RptF("On demand memory report ]]]] ");
  #if 0 // DO_MEM_STATS
    // in Testing build print a memory footprint as well
    MemoryFootprintSummary();
  #endif

}

#endif

#if DO_MEM_STATS
class ReportN
{
  public:
  ~ReportN()
  {
    ReportMemory();
  }
};

static ReportN ReportNOnExit INIT_PRIORITY_URGENT;

#endif

class RefHeap
{
  Ref<MemHeapType> _memory;

  public:
  operator MemHeapType *() {return _memory;}
  MemHeapType *operator ->() {return _memory;}

  RefHeap( int sizeMB=256 );
  ~RefHeap();
};


int DetectHeapSizeMB();

RefHeap BMemory(DetectHeapSizeMB()) INIT_PRIORITY_URGENT; // memory low condition testing

//MemHeapType *SMemPtr=SMemory;
MemHeapType *BMemPtr=BMemory;

//size_t MemoryFreeOnDemand(size_t size);

size_t FreeOnDemandSystemMemoryLowLevel(size_t size);

int HandleOutOfMemory( size_t size )
{
   // Your code
   return FreeOnDemandSystemMemoryLowLevel(size);
}

#ifdef _WIN32
#include <new.h>
#endif

RefHeap::RefHeap( int sizeMB )
{
  #ifdef _WIN32
    // initialize malloc failure handler
    _set_new_handler(HandleOutOfMemory);
    _set_new_mode(1);
  #endif
    // on PC (or X360) we have quite a lot more memory available
    // we prefer to avoid fragmentation
    const int align=DefaultAlign;
#ifdef _WIN32
  _memory=new MemHeapType("Default",sizeMB*1024*1024,align,&BMemPtr);
#endif
}

RefHeap::~RefHeap()
{
  // memory can be allocated - it needs to be deallocated before MemHeap::DoDestruct
//  LogF("Memory static ref deallocated.");
//  if( _memory && _memory->TotalAllocated()>0 )
//  {
//    LogF("  %d KB not deallocated",_memory->TotalAllocated()/1024);
//  }
}

//! initialization done after all static variables are initialized
void MemoryMainInit()
{
  #if MEM_CHECK
    InitMemoryCheck();
  #endif
}

void MemoryMainDone()
{
  //GMapFile.Clear();
}

void MemoryInit()
{
}

void MemoryCleanUp()
{
#ifdef _WIN32
  //GMapFile.Clear();
  if( BMemPtr )
  {
    // force all allocators to clean up
    FastCAlloc::CleanUpAll();
    BMemPtr->CleanUp();
  }
#endif
}

void MemoryDone()
{
  // all FastAlloc should be cleaned up now

  if ( BMemPtr )
  {
    BMemPtr->CleanUp();
  }

  #if MEM_CHECK
    ReportAllocated();
  #endif
  if ( BMemPtr )
  {
    RptF("Memory should be free now.");
    RptF("  %d B not deallocated",BMemPtr->TotalAllocated());

  }
  else
  {
    LogF("Memory is free now.");
  }
  #if 0 //SMALL_ALLOC
  LogF("Allocation size report");
  for( int i=0; i<=nAllocSlots; i++ )
  {
    int size=i<nAllocSlots ? allocSizes[i] : 1*1024*1024;
    LogF("slot %2d (%7d): %9d",i,size,AllocStats[i]);
  }
  #endif

  #ifdef _WIN32
    //GMapFile.Clear();
  #endif
}

class MemTableFunctions: public MemFunctions
{
  public:
  virtual void *New(size_t size);
  virtual void *New(size_t size, const char *file, int line);
  virtual void Delete(void *mem);
  virtual void Delete(void *mem, const char *file, int line);
  virtual void *Realloc(void *mem, size_t size);
  virtual void *Realloc(void *mem, size_t size, const char *file, int line);
  virtual void *Resize(void *mem, size_t size);
  virtual size_t GetSize(void *ptr);

  void *NewPage(size_t size, size_t align);
  void DeletePage(void *page, size_t size);
  size_t GetPageRecommendedSize();

  //! base of memory allocation space, NULL is unknown/undefined
  virtual void *HeapBase()
  {
    return NULL;
  }

  //! approximate total amount of commited memory
  virtual size_t HeapUsed()
  {
    if( !BMemory ) return 0;
    return BMemory->TotalAllocated();
  }

  virtual size_t HeapUsedByNew();
  //! approximate total amount of commited memory
  virtual size_t HeapCommited()
  {
    if( !BMemory ) return 0;
    return BMemory->TotalCommitted();
  }

  //! approximate total count of free memory blocks (used to determine fragmentation)
  virtual int FreeBlocks()
  {
    if( !BMemory ) return 0;
    return 1;
  }

  //! approximate total count of allocated memory blocks (used to determine fragmentation)
  virtual int MemoryAllocatedBlocks()
  {
    if( !BMemory ) return 0;
    return BMemory->RefCounter();
  }

  //! list all allocated blocks to debug log
  virtual void Report()
  {
    #if _ENABLE_REPORT
    if( !BMemory ) return;
    int alloc=BMemory->TotalAllocated();
    int commit=BMemory->TotalCommitted();
    LogF("Total allocated %d, commited %d",alloc,commit);
    #endif
  }

  //! check heap integrity
  virtual bool CheckIntegrity() {return BMemory->Check();}
  //! check if out of memory is signalized
  //! if true, application should limit memory allocation as much as possible
  virtual bool IsOutOfMemory() {return MemoryErrorState;}

  virtual void CleanUp() {BMemory->CleanUp();}

  #if MAIN_HEAP_MT_SAFE
	virtual void Lock() {BMemory->Lock();}
	virtual void Unlock() {BMemory->Unlock();}
	#endif

	virtual void OnThreadEnter();
	virtual void OnThreadExit();
};

MemTableFunctions OMemTableFunctions INIT_PRIORITY_URGENT;

MemFunctions *GMemFunctions = &OMemTableFunctions;

//////////////////////////////////////////////////////////////////////////

namespace Memory
{
  void ReportAnyMemory(const char *reason)
  {
#if _ENABLE_REPORT
    if (BMemory)
    {
      BMemory->ReportAnyMemory(reason);
    }
#endif
  };
  mem_size_t FreeSystemMemory(mem_size_t size)
  {
    if (BMemory)
    {
      return BMemory->FreeSystemMemory(size);
    }
    return 0;
  }

  bool CheckVirtualFree(mem_size_t size) {return BMemory->CheckVirtualFree(size);}

  mem_size_t TotalAllocated() {return BMemory ? BMemory->TotalAllocated() : 0;}
  mem_size_t TotalCommitted() {return BMemory ? BMemory->TotalCommitted() : 0;}
  mem_size_t TotalLowLevelMemory() {return BMemory ? BMemory->TotalLowLevelMemory() : 0;}

  LockedFreeOnDemandEnumerator GetFreeOnDemandEnumerator() {return BMemory->GetFreeOnDemandEnumerator();}
}

void PrintVMMap(int extended, const char *title=NULL);
void ReportMemoryStatus();

IMemoryFreeOnDemand *ProgressGetFreeOnDemandInterface();

/*!
\param freeRequired how much main and low level heap memory should be left
\param freeSysRequired  how much system heap memory should be left
*/

#if _ENABLE_REPORT
void ReportGC(const char *text)
{
  MEMORYSTATUS memstat;
  memstat.dwLength = sizeof(memstat);
  GlobalMemoryStatus(&memstat);
  size_t sysFree = memstat.dwAvailPhys;
  size_t heapFree = MemoryCommited()-MemoryUsed();
  size_t llFree = BMemory->TotalLowLevelMemory();
  LogF("%s: heap %d, system %d, LL %d",text,heapFree+sysFree,sysFree,llFree);
}
#endif

/// get information about memory which is committed outside of our heaps (e.g. file cache)
mem_size_t MemoryStoreUsed();




// check how much memory is allocated now
// this is platform dependent operation
#if _ENABLE_CHEATS

static size_t TotalAllocatedCheck()
{
    size_t normalHeap = BMemory->TotalAllocated();
    return normalHeap;
}

#endif


/**
@param id any slots containing id in the name (ignore case) are processes
@param [out] total ammount of memory which was really released, based on system wide measurements
@return total size of items released
*/
size_t FreeOnDemandReleaseSlot(const char *id, size_t toRelease, size_t *verifyRelease)
{ 
  #if _ENABLE_CHEATS
  if (BMemory)
  {
    return BMemory->FreeOnDemandReleaseSlot(id,toRelease,verifyRelease);
  }
  #endif
  return 0;
}

#if _ENABLE_REPORT
size_t MemHeap::FreeOnDemandReleaseSlot(const char *id, size_t toRelease, size_t *verifyRelease)
{
  size_t released = 0;
  #if _ENABLE_CHEATS
  // check how much memory is free now
  // this is platform dependent operation
  size_t totalBefore = TotalAllocatedCheck();

  // may be called only from the main thread, because it is using the system allocations as well
  AssertThread();
  
  typedef IMemoryFreeOnDemand *(MemHeap::*GetFirstMemoryFreeOnDemandF)() const;
  typedef IMemoryFreeOnDemand *(MemHeap::*GetNextMemoryFreeOnDemandF)(IMemoryFreeOnDemand *cur) const;
  const static struct 
  {
    GetFirstMemoryFreeOnDemandF first;
    GetNextMemoryFreeOnDemandF next;
    const char *name;
  } onDemandFcs[]=
  {
    {&MemHeap::GetFirstFreeOnDemand,&MemHeap::GetNextFreeOnDemand,"Heap"},
    {&MemHeap::GetFirstFreeOnDemandSystem,&MemHeap::GetNextFreeOnDemandSystem,"Sys"},
    {&MemHeap::GetFirstFreeOnDemandLowLevel,&MemHeap::GetNextFreeOnDemandLowLevel,"LL"},
  };

  int slotCount = 0;
  RString lowId = id;
  lowId.Lower();
  int statsDiffTotal = 0;
  for (int i=0; i<sizeof(onDemandFcs)/sizeof(*onDemandFcs); i++)
  {
    for(
      IMemoryFreeOnDemand *mem = (this->*onDemandFcs[i].first)();
      mem;
      mem = (this->*onDemandFcs[i].next)(mem)
    )
    {
      // check if the name matches
      RString lowName = mem->GetDebugName();
      lowName.Lower();
      if (strstr(lowName,lowId))
      {
        size_t totalBeforeItem = TotalAllocatedCheck();
        size_t statBefore = mem->MemoryControlled();
        size_t memReleased = mem->Free(toRelease);
        size_t statAfter = mem->MemoryControlled();
        size_t totalAfterItem = TotalAllocatedCheck();
        // print single slot stats
        if (memReleased>0 || totalAfterItem<totalBeforeItem || statAfter<statBefore)
        {
          LogF(
            "Slot %s: released %d, allocation difference %d, stats difference %d",
            (const char *)mem->GetDebugName(),
            memReleased,totalBeforeItem-totalAfterItem,statBefore-statAfter
          );
          released += memReleased;
          statsDiffTotal += statBefore-statAfter;
          slotCount++;
        }
      }
    }
  }
  size_t totalAfter = TotalAllocatedCheck();
  // if there were mutliple slots affects, print multiple slot stats
  if (slotCount>1)
  {
    LogF(
      "Released %d, allocation difference %d, stats difference %d",
      released,totalBefore-totalAfter,statsDiffTotal
    );
  }
  if (verifyRelease)
  {
    *verifyRelease = totalBefore-totalAfter;
  }
  #endif
  return released;
}
#endif

int FrameId=0;

void FreeOnDemandFrame()
{
  if (BMemory)
  {
    PROFILE_SCOPE_EX(gbFrm,mem); // scope to ensure memory CS locks are inside of a common scope
    BMemory->FreeOnDemandFrame();
  }
}

#include "El/FileServer/fileServerMT.hpp"


void MemHeap::FreeOnDemandFrame()
{
  // called only from main thread - accessing system allocator
  AssertThread();

  // advance global FrameId first, all local ones will follow
  FrameId++;
  
  // note: memory lock not taken (see MemHeapLocked::FreeOnDemandFrame)
  // note: we do not need memory CS lock just to traverse on demand lists, as it is stable
  typedef IMemoryFreeOnDemand *(MemHeap::*GetFirstMemoryFreeOnDemandF)() const;
  typedef IMemoryFreeOnDemand *(MemHeap::*GetNextMemoryFreeOnDemandF)(IMemoryFreeOnDemand *cur) const;
  const static struct 
  {
    GetFirstMemoryFreeOnDemandF first;
    GetNextMemoryFreeOnDemandF next;
    const char *name;
  } onDemandFcs[]=
  {
    {&MemHeap::GetFirstFreeOnDemand,&MemHeap::GetNextFreeOnDemand,"Heap"},
    {&MemHeap::GetFirstFreeOnDemandSystem,&MemHeap::GetNextFreeOnDemandSystem,"Sys"},
    {&MemHeap::GetFirstFreeOnDemandLowLevel,&MemHeap::GetNextFreeOnDemandLowLevel,"LL"},
  };
  for (int i=0; i<sizeof(onDemandFcs)/sizeof(*onDemandFcs); i++)
  {
    for
    (
      IMemoryFreeOnDemand *mem = (this->*onDemandFcs[i].first)();
      mem;
      mem = (this->*onDemandFcs[i].next)(mem)
    )
    {
      mem->MemoryControlledFrame();
    }
  }  
  
}

void RegisterFreeOnDemandMemory(IMemoryFreeOnDemand *object)
{
#ifdef _WIN32
  DoAssert( BMemory );
#endif
  if( BMemory )
  {
    BMemory->RegisterFreeOnDemand(object);
  }
}

mem_size_t FreeOnDemandAnyMemory(
  mem_size_t size, bool physical, mem_size_t &systemReleased,
  IMemoryFreeOnDemand **extras, int nExtras
);

void RegisterFreeOnDemandLowLevelMemory(IMemoryFreeOnDemand *object)
{
#ifdef _WIN32
  DoAssert( BMemory );
#endif
  if( BMemory )
  {
    BMemory->RegisterFreeOnDemandLowLevel(object);
  }
}

size_t FreeOnDemandLowLevelMemory(size_t size)
{
  if (BMemory)
  {
    return BMemory->FreeOnDemandLowLevelMemory(size);
  }
  return 0;
}

/**
Perform any possible low-level memory freeing as necessary 

Note: different from FreeOnDemandLowLevel
*/
size_t MemHeap::FreeOnDemandLowLevelMemory(size_t size)
{
  // balanced releasing of all kinds of memory
  // note: allocation here is extremely dangerous
  AUTO_STATIC_ARRAY(OnDemandStats,stats,128);

  for(IMemoryFreeOnDemand *walk=MemHeap::GetFirstFreeOnDemandLowLevel(); walk; walk = MemHeap::GetNextFreeOnDemandLowLevel(walk))
  {
    OnDemandStats &mstat = stats.Append();
    mstat.mem = walk;
    mstat.system = false;
  }
  mem_size_t systemReleased;
  mem_size_t released = MemoryFreeOnDemandList::BalanceList(size,stats.Data(),stats.Size(),NULL,0,&systemReleased);
  #if 0 // _ENABLE_REPORT
  if (released>0)
  {
    LogF("Low level memory release forced - %d B",released);
  }
  #endif
  return MemSizeTToSizeT(released);
}

void FreeOnDemandLock()
{
  if (BMemory) BMemory->Lock();
}
void FreeOnDemandUnlock()
{
  if (BMemory) BMemory->Unlock();
}

IMemoryFreeOnDemand *GetFirstFreeOnDemandMemory()
{
  if (BMemory) return BMemory->GetFirstFreeOnDemand();
  return NULL;
}
IMemoryFreeOnDemand *GetNextFreeOnDemandMemory(IMemoryFreeOnDemand *cur)
{
  if (BMemory) return BMemory->GetNextFreeOnDemand(cur);
  return NULL;
}

IMemoryFreeOnDemand *GetFirstFreeOnDemandLowLevelMemory()
{
  if (BMemory) return BMemory->GetFirstFreeOnDemandLowLevel();
  return NULL;
}
IMemoryFreeOnDemand *GetNextFreeOnDemandLowLevelMemory(IMemoryFreeOnDemand *cur)
{
  if (BMemory) return BMemory->GetNextFreeOnDemandLowLevel(cur);
  return NULL;
}



//////////////////////////////////////////////////////////////////////////


/// coalesce regions with the same flags
class CoalesceRegions
{
  DWORD _state;
  DWORD _type;
  SIZE_T _addr;
  SIZE_T _regionSize;
public:
  CoalesceRegions()
  {
    _state = 0;
    _type = 0;
    _regionSize = 0;
    _addr = 0;
  }
  
  template <class FlushFunc>
  void Flush(const FlushFunc &func)
  {
    if (_regionSize>0)
    {
      func(_state,_type,_addr,_regionSize);
    }
  }

  template <class FlushFunc>
  void Add(DWORD state, DWORD type, SIZE_T addr, SIZE_T regionSize, const FlushFunc &func)
  {
    if (_state!=state || addr!=_addr+_regionSize || type!=_type)
    {
      if (_regionSize>0)
      {
        func(_state,_type,_addr,_regionSize);
      }
      _state = state;
      _type = type;
      _addr = addr;
      _regionSize = regionSize;
    }
    else
    {
      _regionSize += regionSize;
    }
  }
};

enum
{
  PageOwnFree,
  /// page reserved by some heap under our control
  PageOwnReservedHeap=0x10,
  PageOwnReservedCommittedLast=PageOwnReservedHeap+15,
  /// page committed by some heap under our control
  PageOwnCommittedHeap,
  PageOwnCommittedHeapLast=PageOwnCommittedHeap+15,
  /// page reserved by some library (most likely D3D + driver)
  PageOwnReservedOther,
  PageOwnReservedOtherLast=PageOwnReservedOther+15,
  PageOwnCommittedOther,
  PageOwnCommittedOtherLast=PageOwnCommittedOther+15,
};

struct WriteRegionToRpt
{
  void operator () (char state, DWORD type, SIZE_T addr, SIZE_T regionSize) const
  {
    const char *stateDesc = "???";
    switch (state)
    {
      #define CASE_ENUM(x) case PageOwn##x: stateDesc = #x; break;
      CASE_ENUM(Free)
      CASE_ENUM(ReservedHeap)
      CASE_ENUM(ReservedHeap+1)
      CASE_ENUM(ReservedHeap+2)
      CASE_ENUM(ReservedHeap+3)
      CASE_ENUM(ReservedHeap+4)
      CASE_ENUM(ReservedHeap+5)
      CASE_ENUM(ReservedHeap+6)
      CASE_ENUM(ReservedHeap+7)
      CASE_ENUM(CommittedHeap)
      CASE_ENUM(CommittedHeap+1)
      CASE_ENUM(CommittedHeap+2)
      CASE_ENUM(CommittedHeap+3)
      CASE_ENUM(CommittedHeap+4)
      CASE_ENUM(CommittedHeap+5)
      CASE_ENUM(CommittedHeap+6)
      CASE_ENUM(CommittedHeap+7)
      CASE_ENUM(ReservedOther)
      CASE_ENUM(CommittedOther)
      #undef CASE_ENUM
    }
    const char *typeDesc = "";
    switch (type)
    {
      #define CASE_ENUM(x) case MEM_##x: stateDesc = #x; break;
      CASE_ENUM(IMAGE)
      CASE_ENUM(MAPPED)
      CASE_ENUM(PRIVATE)
      #undef CASE_ENUM
    }
    RptF("Region: %p,%d KB,%s %s",addr,regionSize/1024,stateDesc,typeDesc);
  }
};

static int CheckHeapOwnerShip(size_t addr, size_t size)
{
  // check if given page is owner by some heap we know (and which)
  // walk through all known heaps
  
  if (BMemPtr)
  {
    int own = BMemPtr->IsFromHeap((void *)addr);
    if (own>=0) return own;
  }
  if (safeHeap)
  {
    int own = safeHeap->IsFromHeap((void *)addr);
    if (own>=0) return 4+own;
  }
  
  return -1;
}



#if _ENABLE_REPORT
  #define _VM_OWN_TRACK 0
#endif

#if _VM_OWN_TRACK
/// store last known ownership for each memory page
// possible ownership values:


struct WriteDiffToLog
{
  void operator () (DWORD state, DWORD type, SIZE_T addr, SIZE_T regionSize) const
  {
    const char *stateDesc = "???";
    switch (state)
    {
      #define CASE_ENUM(x) case PageOwn##x: stateDesc = #x; break;
      CASE_ENUM(Free)
      CASE_ENUM(ReservedHeap)
      CASE_ENUM(ReservedHeap+1)
      CASE_ENUM(ReservedHeap+2)
      CASE_ENUM(ReservedHeap+3)
      CASE_ENUM(ReservedHeap+4)
      CASE_ENUM(ReservedHeap+5)
      CASE_ENUM(ReservedHeap+6)
      CASE_ENUM(ReservedHeap+7)
      CASE_ENUM(CommittedHeap)
      CASE_ENUM(CommittedHeap+1)
      CASE_ENUM(CommittedHeap+2)
      CASE_ENUM(CommittedHeap+3)
      CASE_ENUM(CommittedHeap+4)
      CASE_ENUM(CommittedHeap+5)
      CASE_ENUM(CommittedHeap+6)
      CASE_ENUM(CommittedHeap+7)
      CASE_ENUM(ReservedOther)
      CASE_ENUM(CommittedOther)
      #undef CASE_ENUM
    }
    const char *typeDesc = "";
    switch (type)
    {
      #define CASE_ENUM(x) case MEM_##x: stateDesc = #x; break;
      CASE_ENUM(IMAGE)
      CASE_ENUM(MAPPED)
      CASE_ENUM(PRIVATE)
      #undef CASE_ENUM
    }
    LogF("Diff: %p,%6d KB,%s %s",addr,regionSize/1024,stateDesc,typeDesc);
  }
};


char MapOwnership[0x80000000U/0x1000U];

static char CanonicalOwner(char owner)
{
  if (owner==PageOwnFree) return 0;
  // when owner is the same, we do nor care if it is committed or reserved
  if (owner>=PageOwnReservedHeap && owner<=PageOwnCommittedHeapLast) return 1;
  if (owner>=PageOwnReservedOther && owner<=PageOwnCommittedOtherLast) return 2;
  return owner;
}

static bool ChangedOwner(char owner1, char owner2)
{
  char o1Canonical = CanonicalOwner(owner1);
  char o2Canonical = CanonicalOwner(owner2);
  return o1Canonical!=o2Canonical;
}

#endif

static char GetPageState(DWORD bufferState, size_t addr, size_t pageSize)
{
  char state = PageOwnFree;
  if (bufferState&MEM_COMMIT)
  {
    state = PageOwnCommittedOther;
    int own = CheckHeapOwnerShip(addr,pageSize);
    if (own>=0)
    {
      state = PageOwnCommittedHeap+own;
    }
  }
  else if (bufferState&MEM_RESERVE)
  {
    state = PageOwnReservedOther;
    int own = CheckHeapOwnerShip(addr,pageSize);
    if (own>=0)
    {
      state = PageOwnReservedHeap+own;
    }
  }
  return state;
}

// we report any differences against the map
void PrintVMMap(int extended, const char *title)
{
  if (title) LogF("VM map %s",title);
  // print map of virtual memory
  // scan whole application space (2 GB) address range
  // do not try to access kernel space - we would get no information anyway
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  size_t pageSize = si.dwPageSize;
  uintptr_t start = uintptr_t(si.lpMinimumApplicationAddress);
  uintptr_t end = uintptr_t(si.lpMaximumApplicationAddress);
  DoAssert(pageSize>=0x1000); // assumption made by MapOwnership
  size_t longestFreeApp = 0;
  size_t totalFreeApp = 0;
  size_t totalBusyApp = 0;
  size_t totalReservedApp = 0;
  size_t totalCommittedApp = 0;
  size_t totalMappedApp = 0;
  /// compute small mapped regions
  /*
  Detect bug: a lot of small committed mapped pages (8 KB regions) interleaved with 60-64 KB free regions.
  Caused by old ATI drivers.
  */
  int smallMappedRegions = 0;
  size_t smallMappedRegionsSize = 0;
  CoalesceRegions coalesce;
  CoalesceRegions coalesceDiff;
  int index=0;
  for (uintptr_t addr = start; addr<end; )
  {
    MEMORY_BASIC_INFORMATION buffer;
    SIZE_T retSize = VirtualQuery((void *)addr,&buffer,sizeof(buffer));
    if (retSize==sizeof(buffer) && buffer.RegionSize>0)
    {
      // detect suspected "leak" pattern
      if (buffer.Type==MEM_MAPPED)
      {
        if (buffer.RegionSize<=8*1024)
        {
        smallMappedRegions++;
          smallMappedRegionsSize += buffer.RegionSize;
        }
        totalMappedApp += buffer.RegionSize;
      }
      if (extended>0)
      {
        char state = GetPageState(buffer.State, addr, pageSize);
        coalesce.Add(state,buffer.Type,addr,buffer.RegionSize,WriteRegionToRpt());
      }
      #if _VM_OWN_TRACK
      if (extended==0)
      {
        for (size_t page=0,pIndex=index; page<buffer.RegionSize; page+=pageSize,pIndex++)
        {
          // mark each page 
          // check what ownership do we have now
          char state = GetPageState(buffer.State, addr+page, pageSize);
          if (ChangedOwner(MapOwnership[pIndex],state))
          {
            coalesceDiff.Add(state,buffer.Type,addr+page,pageSize,WriteDiffToLog());
          }
          MapOwnership[pIndex] = state;
        }
      }
      #endif
      // dump information about this region
      if (buffer.State&MEM_FREE)
      {
        if (buffer.RegionSize>longestFreeApp) longestFreeApp = buffer.RegionSize;
        totalFreeApp += buffer.RegionSize;
      }
      else
      {
        totalBusyApp += buffer.RegionSize;
        if (buffer.State&MEM_RESERVE) totalReservedApp += buffer.RegionSize;
        if (buffer.State&MEM_COMMIT) totalCommittedApp += buffer.RegionSize;
      }
      addr += buffer.RegionSize;
      index+= buffer.RegionSize/pageSize;
    }
    else
    {
      // always proceed
      addr += pageSize;
      index++;
    }
  }
  if (extended)
  {
    coalesce.Flush(WriteRegionToRpt());
  }
  #if _VM_OWN_TRACK
  if (extended==0)
  {
    coalesceDiff.Flush(WriteDiffToLog());
  }
  #endif
  RptF("Longest free VM region: %u B",longestFreeApp);
  RptF(
    "VM busy %u B (reserved %u B, committed %u B, mapped %u B), free %u B",
    totalBusyApp,totalReservedApp,totalCommittedApp,totalMappedApp,totalFreeApp
  );
  RptF("Small mapped regions: %d, size %d B",smallMappedRegions,smallMappedRegionsSize);
}



#if _ENABLE_REPORT
// create and compare virtual memory snapshots
// for each VM page identify its owner

#endif

void ReportMemoryRegion(void *mem)
{
  #if MEM_CHECK
	  int *block=(int *)mem-PREFIX_SIZE/sizeof(int);
	  int magic=block[1];
	  Assert( magic==15879634 );
	  if( magic!=15879634 )
	  {
	    LogF("%p: Unknown memory, magic not matching",mem);
	    return;
	  }
	  MemoryInfo *info=(MemoryInfo *)block[0];
	  if (!info || !info->Valid())
	  {
	    LogF("%p: Memory block info not found",mem);
	    return;
	  }
	
		info->Report();
  #else
    LogF("%p",mem);
  #endif
}

void ReportMemoryStatus(int level)
{
  const int oneMB = 1024*1024;
    MEMORYSTATUSEX memstat;
    memstat.dwLength = sizeof(memstat);
    GlobalMemoryStatusEx(&memstat);
    RptF("Virtual memory total %lld MB (%llu B)",memstat.ullTotalVirtual/oneMB,memstat.ullTotalVirtual);
    RptF("Virtual memory free %lld MB (%llu B)",memstat.ullAvailVirtual/oneMB,memstat.ullAvailVirtual);
    RptF("Physical memory free %lld MB (%llu B)",memstat.ullAvailPhys/oneMB,memstat.ullAvailPhys);
    RptF("Page file free %lld MB (%llu B)",memstat.ullAvailPageFile/oneMB,memstat.ullAvailPageFile);
	size_t used = GetMemoryUsedSize();
  size_t commited = GetMemoryCommitedSize();
  RptF("Process working set %d MB (%u B)",used/oneMB,used);
  RptF("Process page file used %d MB (%u B)",commited/oneMB,commited);
//   RptF("Runtime reserved %d MB (%u B)",BMemory->TotalReserved()/oneMB,BMemory->TotalReserved());
//   RptF("Runtime committed %d MB (%u B)",BMemory->TotalCommitted()/oneMB,BMemory->TotalCommitted());
  if (level>0) PrintVMMap(level>1 ? 1 : 0);
}

void ReportMemoryStatus()
{
  ReportMemoryStatus(1);
}

void MemoryErrorCheckPageFile()
{
    MEMORYSTATUSEX memstat;
    memstat.dwLength = sizeof(memstat);
    GlobalMemoryStatusEx(&memstat);
    const MemSize oneMB = 1024*1024;
    if (memstat.ullAvailPageFile<128*oneMB)
    {
      ErrorMessage(
        "Out of memory.\n"
        "Your swap file is too small, only %lld MB of free space left.\n"
        "Increase your swap file size or delete some files from your hard disk\n",
        memstat.ullAvailPageFile/oneMB
      );
    }
}
/*!
\patch_internal 1.05 Date 07/10/2001 by Ondra.
- Fix: Bytes were reported instead of KB.
*/
void MemHeap::MemoryError(int size)
{
  MemoryErrorReported();
  #if _ENABLE_REPORT
    ReportAnyMemory("MemoryError");
    // we  might print callstack here, but it is almost always useless anyway (the harm was already done)
  #endif
  size_t footprint = GMallocImpl.TotalCommitted();
  ReportMemoryStatus();
  MemoryErrorCheckPageFile();
  ErrorMessage(
    "Out of memory (requested %d KB).\n"
    "  footprint %u KB.\n"
    "  pages %d KB.\n",
    size/1024,footprint,_memRegions.GetReserved()/1024
  );
}

void MemHeap::MemoryErrorPage(int size)
{
  MemoryErrorReported();
  #if _ENABLE_REPORT
    ReportAnyMemory("MemoryError");
    // force output of call-stack
    GDebugExceptionTrap.LogFFF("Out of Memory", true);

  #endif

  ReportMemoryStatus();
  MemoryErrorCheckPageFile();
  ErrorMessage(
    "Out of memory (pages) (requested %d KB).\n"
    "Reserved: %d KB.\n"
    "Total free %d KB\n",
    size/1024,
    _memRegions.GetReserved()/1024,
    (_memRegions.GetReserved()-_memRegions.GetLocked())/1024,
    _memRegions.FindLongestRange()*_memRegions.GetPageSize()/1024
  );
}

void MemoryErrorPage(size_t size)
{
  if (BMemory) BMemory->MemoryErrorPage(size);
}


void MemoryErrorMalloc( int size )
{
  MemoryErrorReported();
  ReportMemoryStatus();
  MemoryErrorCheckPageFile();
  ErrorMessage("Out of Win32 memory (%d KB).\n",size);
}



static size_t ActualSize(void *mem)
{
  return GMallocImpl.MSize(mem);
}

/** memory validator API - see "C:\Program Files\Software Verification\Memory Validator\stublib\stublib.h"

void mvUserCustomAlloc(void	*address,				// address of allocation
					 int	size,					// size of allocation (bytes)
					 DWORD	userData1,
					 DWORD	userData2,
					 DWORD	dllId = -1);			// extDll id, -1 if none

void mvUserCustomReAlloc(void		*oldAddress,		// address of allocation
					   void		*newAddress,		// address of allocation
					   int		newSize,			// size of allocation (bytes)
					   DWORD	userData1,
					   DWORD	userData2,
					   DWORD	dllId = -1);		// extDll id, -1 if none

void mvUserCustomFree(void	*address,				// address of free
					DWORD	userData1,
					DWORD	userData2,
					DWORD	dllId = -1);			// extDll id, -1 if none

void mvUserCustomRefCountDecrement(DWORD	data,		// address of allocation
								 DWORD	userData,
			 					 DWORD	userData2,
								 DWORD	dllId = -1);// extDll id, -1 if none


void mvUserCustomRefCountIncrement(DWORD	data,		// address of allocation
								 DWORD	userData1,
								 DWORD	userData2,
								 DWORD	dllId = -1);// extDll id, -1 if none

DUMP_RESULT mvUserDumpLeaks(DUMP_METHOD			dumpMethod,	// how to do the dump
						  const char			*fileName,	// NULL if no file,
						  USER_DUMP_CALLBACK	callback,	// NULL if not to callback
						  DWORD					userData);	// userdata to pass to callback

List of API functions and the GetProcAddress name equivalent 
Any API function not listed here, please lookup the name using depends.exe 

API Name            GetProcAddress() Name 
mvUserCustomAlloc         ?informAlloc@@YAXPAXHKKK@Z 
mvUserCustomReAlloc         ?informReAlloc@@YAXPAX0HKKK@Z 
mvUserCustomFree         ?informFree@@YAXPAXKKK@Z 
mvUserCustomRefCountDecrement   ?informRefCountDecrement@@YAXKKKK@Z 
mvUserCustomRefCountIncrement   ?informRefCountIncrement@@YAXKKKK@Z 
mvUserDumpLeaks         ?informDumpLeaks@@YA?AW4_dumpResult@@W4_dumpMethod@@PBDP6AHKK11HH@ZK@Z 
*/


#if USE_MV_USER
typedef void mvUserCustomAlloc_FUNC(void	*address,				// address of allocation
					 size_t size,					// size of allocation (bytes)
					 DWORD	userData1,
					 DWORD	userData2,
					 DWORD	dllId);			// extDll id, -1 if none

typedef void mvUserCustomReAlloc_FUNC(void		*oldAddress,		// address of allocation
					   void		*newAddress,		// address of allocation
					   size_t newSize,			// size of allocation (bytes)
					   DWORD	userData1,
					   DWORD	userData2,
					   DWORD	dllId);		// extDll id, -1 if none

typedef void mvUserCustomFree_FUNC(void	*address,				// address of free
					DWORD	userData1,
					DWORD	userData2,
					DWORD	dllId);			// extDll id, -1 if none


class MemoryValidatorAPI
{
  HMODULE hMod;
  mvUserCustomAlloc_FUNC *customAlloc;
  mvUserCustomReAlloc_FUNC *customRealloc;
  mvUserCustomFree_FUNC *customFree;
  
  void *Lookup(const char *name)
  {
    extern bool MemoryValidator;
    if (!MemoryValidator) return NULL;
    Init();
    if (hMod)
      return GetProcAddress(hMod, name);
    return NULL;
  }
  
  void Init();
  
  public:
  void mvUserCustomAlloc(void	*address, size_t size, DWORD userData1, DWORD userData2, DWORD dllId)
	{
	  if (!customAlloc) customAlloc = (mvUserCustomAlloc_FUNC *)Lookup("?informAlloc@@YAXPAXKKKK@Z");
	  if (!customAlloc) customAlloc = (mvUserCustomAlloc_FUNC *)Lookup("?informAlloc@@YAXPAXHKKK@Z");
	  if (customAlloc)
	  {
	    customAlloc(address,size,userData1,userData2,dllId);
	  }
	}

  void mvUserCustomReAlloc(void *oldAddress, void *newAddress, size_t newSize, DWORD userData1, DWORD userData2, DWORD dllId)
  {
	  if (!customRealloc) customRealloc = (mvUserCustomReAlloc_FUNC *)Lookup("?informReAlloc@@YAXPAX0KKKK@Z");
	  if (!customRealloc) customRealloc = (mvUserCustomReAlloc_FUNC *)Lookup("?informReAlloc@@YAXPAX0HKKK@Z");
	  if (customRealloc)
	  {
	    customRealloc(oldAddress,newAddress,newSize,userData1,userData2,dllId);
	  }
  }

  void mvUserCustomFree(void *address, DWORD	userData1, DWORD userData2, DWORD dllId)
  {
	  if (!customFree) customFree = (mvUserCustomFree_FUNC *)Lookup("?informFree@@YAXPAXKKK@Z");
	  if (customFree)
	  {
	    customFree(address,userData1,userData2,dllId);
	  }
  }


} SMemoryValidatorAPI;

void MemoryValidatorAPI::Init()
{
  if (!hMod)
  {
    // TODO: try this only once per frame or once per a few frames?
    hMod = GetModuleHandleA("svlMemoryValidatorStub.dll");
    if (!hMod)
    {
      static bool once = false;
      if (!once++)
        hMod = LoadLibrary("svlMemoryValidatorStub.dll");
    }
  }
}

#endif

inline void *ActualAlloc( size_t size )
{
#ifdef _WIN32
  PROFILE_SCOPE_MEM(memAl);
  AssertThread();
  #if NO_ALLOCATION_AFTER_ERROR
  if (MemoryErrorState)
  {
    // we did not have memory before - we do not have it again
    return NULL;
  }
  #endif
  for (;;)
  {
    void *mem = GMallocImpl.MemAlloc(size);
    if (mem)
    {
      size_t allocSize = ActualSize(mem);
      TotalAllocatedByActualAlloc += allocSize;
      #if _ENABLE_REPORT
        MemSet32(mem,MEM_NEW_VAL_32,allocSize);
        #if USE_MV_USER
          SMemoryValidatorAPI.mvUserCustomAlloc(mem,allocSize,0,0,-1);
        #endif
        //LogF("** %d: Alloc %d -> %p",GetCurrentThreadId(),size,mem);
      #endif
      return mem;
    }
    size_t freeSize = size>0 ? size : 1;
    size_t freed = FreeOnDemandLowLevelMemory(freeSize);
    if (freed==0)
    {
      // failure: nothing to free - we are unable to satisfy the request
      if (GDebugger.IsDebugger())
      {
        ReportMemoryStatus();
        RptF("Out of memory (requested %d KB).\n",size/1024);
        Fail("Memory allocation failed, debugging opportunity");
      }
      else
      {
      BMemory->MemoryError(size);
      return NULL;
    }
    }
    // some memory was freed, but request was not successful
  }
#else
  return malloc(size);
#endif



}
inline void ActualFree( void *mem )
{
  //LogF("** %d: Free %p",GetCurrentThreadId(),mem);
  PROFILE_SCOPE_MEM(memFr);
  AssertThread();

  if (mem)
  {
    size_t allocSize = ActualSize(mem);
    TotalAllocatedByActualAlloc -= allocSize;
    #if _ENABLE_REPORT
      MemSet32(mem,MEM_FREE_VAL_32,allocSize);
      #if USE_MV_USER
        SMemoryValidatorAPI.mvUserCustomFree(mem,0,0,-1);
      #endif
    #endif

  }
  GMallocImpl.MemFree(mem);
}
inline void *ActualResize( void *mem, size_t size )
{
  AssertThread();
  #if NO_ALLOCATION_AFTER_ERROR
  if (MemoryErrorState)
  {
    // we did not have memory before - we do not have it again
    return NULL;
  }
  #endif
  // the intention may actually be to compact the memory
  // this may be hard to recognize, but it should be safe to assume allocator overhead is under 1/2
  void *ret = NULL;
  size_t memSize = GMallocImpl.MSize(mem);
  if (memSize>=size && size>memSize/2) ret = mem;
  else
  {
    #if 0 // _TESTING
      // try to allocate and verify we will not get the same size back
      // TBB4 with memSize/4*3 sometimes returns the same sized block
      void *temp = GMallocImpl.MemAlloc(size);
      if (temp)
      {
        size_t tempSize = GMallocImpl.MSize(temp);
        if (tempSize==memSize)
        {
          LogF("Sizes %d and %d result in the same size %d",size,memSize,tempSize);
        }
        GMallocImpl.MemFree(temp);
      }
    #endif
  }
    //LogF("** %d: Resize %p:%d -> %p",GetCurrentThreadId(),mem,size,ret);
  #if USE_MV_USER
    if (ret)
    {
      SMemoryValidatorAPI.mvUserCustomReAlloc(ret,mem,size,0,0,-1);
    }
  #endif
  return ret;
}

#if 0 // _DEBUG // replicate TCMalloc error
  #include <Es/Memory/normalNew.hpp>
 
  intptr_t ExplicitMapTraits<intptr_t,intptr_t>::null = 0;
  intptr_t ExplicitMapTraits<intptr_t,intptr_t>::keyNull = -1;
  intptr_t ExplicitMapTraits<intptr_t,intptr_t>::zombie  = -2;

  HANDLE ExplicitMapTraits<int,HANDLE>::null = 0;
  int ExplicitMapTraits<int,HANDLE>::keyNull = -1;
  int ExplicitMapTraits<int,HANDLE>::zombie  = -2;

  struct TCMallocError
  {
    ExplicitMap<intptr_t, intptr_t, false, MemAllocSystem> _mapAddr;
    int _mainThread;
    ExplicitMap<int, HANDLE, false, MemAllocSystem> _threads;

    Event _cmdSend,_cmdDone;
    const char *_cmd;

    TCMallocError();

    void DoCommand(const char *item);

    struct CallbackCtx
    {
      TCMallocError *obj;
      HANDLE cmdEvent;
    };
    static DWORD WINAPI ThreadCallback(void *context)
    {
      CallbackCtx *ctx = (CallbackCtx *)context;
      
      ctx->obj->ThreadProc(ctx->cmdEvent);
      return 0;
    }

    void ThreadProc(HANDLE event);
  } STCMallocError;

void TCMallocError::DoCommand(const char *item)
  {
    size_t size;
    intptr_t mem,ret;
    //LogF("%s",item);
    if (sscanf(item,": Alloc %d -> %x\n",&size,&ret)==2)
    {
      void *r = ActualAlloc(size);
      _mapAddr.put(ret,(intptr_t)r);
      //LogF(" %p->%p", ret, r);
    }
    else if (sscanf(item,": Free %x\n",&mem)==1)
    {
      intptr_t m = *_mapAddr.get(mem);
      ActualFree((void *)m);
      _mapAddr.removeKey(mem);
      //LogF(" %p->nil", mem);
    }
    else if (sscanf(item,": Resize %x:%d -> %x\n",&mem,&size,&ret)==3)
    {
      intptr_t m = *_mapAddr.get(mem);
      ActualResize((void *)m,size); // it returns NULL or the original pointer
    }
  }

  void TCMallocError::ThreadProc(HANDLE event)
  {
    for(;;)
    {
      WaitForSingleObject(event,INFINITE);
      if (!_cmd)
        return;
      DoCommand(_cmd);
      _cmd = NULL;
      _cmdDone.Set();
    }
  }

  
  TCMallocError::TCMallocError()
  {
    _mainThread = 0;

    FILE *log = fopen("TCM-error.log","r");
    if (!log) return;
    char buf[512];
    while (fgets(buf,sizeof(buf),log))
    {
      const char *item = strstr(buf,"** ");
      if (!item) continue;

      char *cmdBeg;
      int threadId = strtoul(item+3,&cmdBeg,10);
      if (item+3==cmdBeg || threadId==0) continue;

      if (_mainThread==0) _mainThread = threadId;
      if (_mainThread==threadId)
      {
        DoCommand(cmdBeg);
      }
      else
      {
        _cmd = cmdBeg;
        HANDLE *t = _threads.get(threadId);
        if (!t)
        {
          DWORD tid;
          HANDLE eHandle = CreateEvent(NULL,FALSE,FALSE,NULL);
          CallbackCtx *ctx = (CallbackCtx *)GlobalAlloc(0,sizeof(CallbackCtx)); // leak: we do not care
          ctx->obj = this;
          ctx->cmdEvent = eHandle;
          HANDLE tHandle = CreateThread(NULL,16*1024,ThreadCallback,ctx,0,&tid);
          _threads.put(threadId,eHandle);
          SetEvent(eHandle);
          CloseHandle(tHandle);
        }
        else
        {
          SetEvent(*t);
        }
        _cmdDone.Wait();
      }

    }
    fclose(log);

    _cmd = NULL;
    IteratorState it;
    HANDLE h;
    if ( _threads.getFirst(it,h) )
    {
      do 
      {
        SetEvent(h);
      }
      while ( _threads.getNext(it,h) );
      _threads.reset();
    }
  }

#endif

size_t MemTableFunctions::HeapUsedByNew()
{
  #if !_ENABLE_REPORT
    Fail("Do not use in retail version");
    return 0;
  #elif 0 // MEM_CHECK
    // scan all MemoryInfos
    size_t total = 0; // total memory wanted
    size_t guard = 0; // total memory used by guard and similar 
    for
    (
      MemoryInfo *info=Allocated.First(); info; info=Allocated.Next(info)
    )
    {
      total += info->Size();
      guard += PREPARE_ALLOC_SLACK;
    }

    // ask C allocators
    size_t fastAllocated = FastCAlloc::TotalAllocated();
    size_t fastRequested = FastCAlloc::TotalRequested();
    
    total += fastRequested;
    size_t totalAlloc = TotalAllocatedByActualAlloc + fastAllocated;
    size_t infoAlloc = BMemory->InfoAllocAllocated();
    size_t smallOverhead = 0;
    //return total + FastCAlloc::TotalRequested();
    // small alloc statistics are already included in "total"
    // small alloc allocations are included in TotalAllocatedByActualAlloc
    return totalAlloc-total-guard+infoAlloc;
  #else
    size_t commit = GMallocImpl.TotalCommitted();
    if (commit<TotalAllocatedByActualAlloc) return 0;
    return commit-TotalAllocatedByActualAlloc;
  #endif
}


void *MemTableFunctions::New(size_t size)
{
  CountNew("nofile",0,size);
  #if MEM_CHECK
    size=PrepareAlloc(size);
  #endif
  void *ret=ActualAlloc(size);

  #if MEM_CHECK
    ret=FinishAlloc(ret,size,"",0);
  #endif
  return ret;
}

void *MemTableFunctions::New(size_t size, const char *file, int line)
{
  #if DO_MEM_STATS
  if (*file==0)
  {
    LogF("Warning");
  }
  #endif
  CountNew(file,line,size);
  #if MEM_CHECK
    size=PrepareAlloc(size);
  #endif
  void *ret=ActualAlloc(size);

  #if MEM_CHECK
    ret=FinishAlloc(ret,size,file,line);
  #endif
  return ret;
}

void MemTableFunctions::Delete(void *mem)
{
  if( !mem ) return;
  #if MEM_CHECK
    mem=PrepareFree(mem);
  #endif
  // determine which heap it is in
  ActualFree(mem);
}
void MemTableFunctions::Delete(void *mem, const char *file, int line)
{
  if( !mem ) return;
  #if MEM_CHECK
    mem=PrepareFree(mem);
  #endif
  // determine which heap it is in
  ActualFree(mem);
}

size_t MemTableFunctions::GetSize(void *ptr)
{
  return ActualSize(ptr);
}

void *MemTableFunctions::Realloc(void *mem, size_t size)
{
  // try resizing
  void *newmem = Resize(mem,size);
  if (newmem)
  {
    Assert(newmem==mem);
    return newmem;
  }
  
  // realloc
  newmem = New(size);

  if (mem && newmem)
  {
    // move memory as necessary
    #if MEM_CHECK
    size_t copy = GetRealSize(mem);
    #else
    size_t copy = ActualSize(mem);
    #endif
    if (copy>size) copy = size;
    memcpy(newmem,mem,copy);

    Delete(mem);
  }
  return newmem;
}

void *MemTableFunctions::Resize(void *mem, size_t size)
{
  #if MEM_CHECK
    const MemoryInfo *info = GetMemoryInfo(mem);
    char file[MemoryInfo::FileBufferSize];
    int line;
    if (info)
    {
      // secure - info->_file is certainly zero terminated (see MemoryInfo::MemoryInfo)
      strcpy(file,info->File());
      line = info->Line();
    }
    else
    {
      Fail("Error: Unexpected NULL");
      line = -1;
    }
    size_t oldSize = PrepareAlloc(GetRealSize(mem));
    // no memory fill - memory is already used
    mem = PrepareFree(mem,false);
    size=PrepareAlloc(size);
  #endif
  void *ret=ActualResize(mem,size);
  #if MEM_CHECK
    if (ret)
    {
      // no memory fill - memory is already used
      // TODO: fill newly extended memory
      ret=FinishAlloc(ret,size,file,line,false);
    }
    else
    {
      // no memory fill - memory is already used
      FinishAlloc(mem,oldSize,file,line,false);
    }
  #endif
  return ret;
}

void *MemTableFunctions::Realloc(void *mem, size_t size, const char *file, int line)
{
  // TODO: pass file,line into resize
  void *newmem = Resize(mem,size);
  if (newmem)
  {
    Assert(newmem==mem);
    return newmem;
  }
  
  // realloc
  newmem = New(size,file,line);

  if (mem && newmem)
  {
    // move memory as necessary
    #if MEM_CHECK
    size_t copy = GetRealSize(mem);
    #else
    size_t copy = ActualSize(mem);
    #endif
    if (copy>size) copy = size;
    memcpy(newmem,mem,copy);

    Delete(mem);
  }
  return newmem;
}

void *MemTableFunctions::NewPage(size_t size, size_t align)
{
  void * mem = BMemory->AllocRegion(size,align);
  while (!mem)
  {
    // some cleanup may help
    size_t free = BMemory->FreeOnDemandLowLevel(size);
    if (free<=0)
    {
      // failure: nothing to free - we are unable to satisfy the request
      if (GDebugger.IsDebugger())
      {
        ReportMemoryStatus();
        RptF("Out of memory (requested %d KB).\n",size/1024);
        Fail("Memory allocation failed, debugging opportunity");
      }
      else
      {
      BMemory->MemoryErrorPage(size);
      return NULL;
    }
    }
    mem = BMemory->AllocRegion(size,align);;
  }
  return mem;
}
void MemTableFunctions::DeletePage(void *page, size_t size)
{
  BMemory->FreeRegion(page,size);
}

size_t MemTableFunctions::GetPageRecommendedSize()
{
  return BMemory->GetRegionRecommendedSize();
}

void MemTableFunctions::OnThreadEnter()
{

}

/// handler to handle thread termination
/** from http://www.codeproject.com/KB/threads/tls.aspx and http://www.rsdn.ru/forum/winapi/2895408.1.aspx */

void NTAPI on_tls_callback(PVOID DllHandle, DWORD dwReason, PVOID Reserved)
{
  if (dwReason==DLL_THREAD_DETACH)
  {
    GMallocImpl.ThreadDetach();
  }
}

#pragma data_seg(push, old_seg)
#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK p_thread_callback = on_tls_callback;
#pragma data_seg(pop, old_seg)

#pragma comment(linker, "/INCLUDE:__tls_used") // 

void MemTableFunctions::OnThreadExit()
{
}

#ifdef _WIN32

//---------------------------------------------------------------------------
//  Net-heap support:

#ifdef _WIN32

/// Static instance of mt-safe heap (to be destructed as late as possible).
RefD<MemHeapLocked> safeHeap INIT_PRIORITY_URGENT;

#ifdef NET_LOG_SAFE_HEAP
unsigned safeHeapCounter = 0;
#endif

#define NET_HEAP_SIZE   32

MemFunctions *GSafeMemFunctions = &OMemTableFunctions;

MemHeapLocked::MemHeapLocked(
  const char *name, MemSize size, MemSize align,
  MemHeapLocked **lateDestructPointer
)
: base(name,size,align,(MemHeap **)lateDestructPointer)
{
}
MemHeapLocked::MemHeapLocked()
{

}

#if _DEBUG || _PROFILE
static DWORD BPThread=GetCurrentThreadId(); // do only for the main thread
static int BPCount;
int AllowBPMemLock;
void StartBPScope()
{
   BPCount = 0;
}
#endif

void MemHeapLocked::Lock() const
{
  #if _DEBUG || _PROFILE
  if (AllowBPMemLock>0)
  {
    // detected interesting scope
    PROFILE_SCOPE_EX(memLS, mem);
    _lock.Lock();
    if (GetCurrentThreadId()==BPThread)
    {
      BPCount++;
      static int BPOver = 1000;
      if (BPCount>BPOver)
      {
        // next breakpoint at higher limit
        BPOver += 1000;
        __asm nop;
      }
    }
  }
  else
  #endif
  {
    // default handling
    PROFILE_SCOPE_EX(memLo, mem);
    _lock.Lock();
  
  }
}

/** AddRef/Release used only from the main thread, to constuct the global object */

int MemHeapLocked::AddRef() const {AssertThread();return base::AddRef();}
int MemHeapLocked::Release() const {AssertThread();return base::Release();}

void SafeHeapBeginUse()
{
}
void SafeHeapEndUse()
{
}

#else

void *safeNew ( size_t size )
{
    return malloc(size);
}

void *safeNew ( size_t size, const char *file, int line )
{
    free(mem);
}

void safeDelete ( void *mem )
{
    free(mem);
}

#endif

#endif

#if _USE_DEAD_LOCK_DETECTOR
//Global DeadLockDetector variable
DeadLockDetector GDeadLockDetector;
#endif

#include "Es/essencepch.hpp"
#include "Es/Framework/netlog.hpp"

	#if defined(NET_LOG) && defined(EXTERN_NET_LOG)
  #  ifdef NET_LOG_PERIOD
  NetLogger netLogger(NET_LOG_PERIOD);
  #  else
  NetLogger netLogger;
  #  endif
  #endif

#else

MemFunctions OMemFunctions INIT_PRIORITY_URGENT;
MemFunctions OSafeMemFunctions INIT_PRIORITY_URGENT;
MemFunctions *GMemFunctions = &OMemFunctions;
MemFunctions *GSafeMemFunctions = &OSafeMemFunctions;

// no garbage collect or any on demand releasing with default new

void RegisterFreeOnDemandMemory(IMemoryFreeOnDemand *object)
{
}

size_t MemoryFreeOnDemand(size_t size)
{
  return 0;
}

void FreeOnDemandGarbageCollectMain(size_t freeRequired, size_t freeSysRequired, bool isMain)
{
}

mem_size_t GetMemoryUsageLimit(mem_size_t *virtualLimit)
{
  if (virtualLimit) *virtualLimit = INT_MAX;
  return INT_MAX; // no limit - 2GB allowed
}

int FrameId=0;

void FreeOnDemandFrame()
{
  FrameId++;
}

void RegisterFreeOnDemandSystemMemory(IMemoryFreeOnDemand *object)
{
}

void RegisterFreeOnDemandLowLevelMemory(IMemoryFreeOnDemand *obj)
{
}

void FreeOnDemandGarbageCollectSystemMemoryLowLevel(size_t freeSysRequired)
{
}

size_t FreeOnDemandSystemMemoryLowLevel(size_t size)
{
  return 0;
}

size_t FreeOnDemandSystemMemory(
  size_t size, IMemoryFreeOnDemand **extras, int nExtras
)
{
  return 0;
}

void MemoryInit(){}
void MemoryDone(){}
void MemoryFootprint(){}
void PrintVMMap(int extended, const char *title=NULL){}

#endif

#if  _ENABLE_REPORT && !defined MFC_NEW

RString GetMemStat(mem_size_t statId, mem_size_t &statVal)
{
  int id = 0; // compiler will eliminate the ++ done on this into constants

  if (statId==id++)
  {
    statVal = 0;
    return " ";
  }
  if (statId==id++)
  {
    statVal = MemoryUsed();
    return "MemAlloc";
  }
  if (statId==id++)
  {
    statVal = MemoryUsedByNew();
    return "MemOvhead";
  }
  if (statId==id++)
  {
    statVal = MemoryCommited();
    return "MemCommit";
  }
  if (statId==id++)
  {
    statVal = BMemory->TotalReserved();
    return "MemReserved";
  }
  if (statId==id++)
  {
    statVal = BMemory->ReservedByPages();
    return "MemReserved (Page)";
  }
  if (statId==id++)
  {
    statVal = BMemory->ReservedByNew();
    return "MemReserved (New)";
  }
  if (statId==id++)
  {
    statVal = BMemory->CommittedByPages();
    return "MemCommit (Page)";
  }
  if (statId==id++)
  {
    statVal = BMemory->CommittedByNew();
    return "MemCommit (New)";
  }
  if (statId==id++)
  {
    statVal = BMemory->CommittedByDirect();
    return "MemCommit (Direct)";
  }
  if (statId==id++)
  {
    statVal = BMemory->LongestFreeForPages();
    return "MemLongFree (Page)";
  }
  if (statId==id++)
  {
    statVal = BMemory->LongestFreeForNew();
    return "MemLongFree (New)";
  }
  if (statId==id++)
  {
    statVal = MemoryStoreUsed();
    return "Memory Store";
  }
  #if !defined USE_FILE_MAPPING && USE_MEM_STORE && _ENABLE_CHEATS
  if (statId==id++)
  {
    statVal = GMemStore.GetMappedPages()*GMemStore.GetPageSize();
    return "Mapped Store";
  }
  #endif

  if (statId==id++)
  {
    statVal = ThrottleMemoryUsageVirtual-BMemory->TotalAllocated();
    return "Virt. to limit";
  }
  if (statId==id++)
  {
    statVal = ThrottleMemoryUsagePhysical-BMemory->TotalAllocated()-MemoryStoreUsed();
    return "Phys. to limit";
  }
  if (statId==id++)
  {
    statVal = 0;
    return " ";
  }
  /* now displayed in the main stats - see Engine::DrawFinishTexts
  if (statId==id++)
  {
    MEMORYSTATUS mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatus(&mem);
    statVal = mem.dwAvailVirtual;
    return "Virtual Free";
  }
  if (statId==id++)
  {
    MEMORYSTATUS mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatus(&mem);
    statVal = mem.dwAvailPhys;
    return "Physical Free";
  }
  */

  
  statVal = 0;
  return RString();
}
#endif

#if _ENABLE_REPORT && !defined MFC_NEW
void ReportMemoryTotals()
{
  FastCAlloc::ReportTotals();
  LogF("Fastalloc requested %d",FastCAlloc::TotalRequested());
  LogF("Fastalloc allocated %d",FastCAlloc::TotalAllocated());
  if (BMemory)
  {
    BMemory->ReportTotals();
    BMemory->ReportAnyMemory("");
    // report all basic memory stats
    for( int c=0; c<100; c++ )
    {
      mem_size_t value;
      RString name = GetMemStat(c,value);
      if (name.GetLength()==0) break;
      if (value==0) continue;
      LogF("%s - %lld",(const char *)name,value);
    }
  }
}
#else
void ReportMemoryTotals()
{
}
#endif

const char *MemoryAllocatorName()
{
  return GMallocImpl.Name();
}


// MT safe heap - equal to GMemFunctions or GSafeMemFunctions depending on configuration
#if MAIN_HEAP_MT_SAFE
  MemFunctions *GMTMemFunctions = GMemFunctions; 
#else
  MemFunctions *GMTMemFunctions = GSafeMemFunctions; 
#endif

size_t TotalAllocatedWin()
{
#ifdef MFC_NEW
  return 0;
#else
  if( !BMemory ) return 0;
  return BMemory->TotalAllocated();
#endif
}

#endif // enable/disable whole implementation

#else

// non-win32 version present in memTable only
// TODO: nedalloc should be usable on Linux as well

#endif //_win32


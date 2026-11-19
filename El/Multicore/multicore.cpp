#include <El/elementpch.hpp>
#include "multicore.hpp"

static int ForcedCPUCount = 0;

void ForceCPUCount(int count)
{
  // avoid signed 32b number overflow - never attempt to use more thena 31 cores
  if (count>31) count = 31;
  ForcedCPUCount = count;

  #if !_SUPER_RELEASE && defined _WIN32
  // when used in non-retail builds, we want to use cpuCount to simulate less-core CPUs
  // therefore we set affinity mask so that no more CPUs are used
  // in Retail builds we use it only to override CPU count detection to determine optimum threading patterns,
  // but we still allow the system to run more threads when possible
  DWORD processAffinityMask = 0;
  DWORD systemAffinityMask = 0;
  if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinityMask, &systemAffinityMask))
  {
    int left = count;
    DWORD wantedAffinityMask = 0;
    for (int i=0; i<32; i++)
    {
      if (processAffinityMask&(1<<i))
      {
        wantedAffinityMask |= 1<<i;
        if (--left<=0) break;
      }
    }
    LogF("Process afinity forced to %x",wantedAffinityMask);
    SetProcessAffinityMask(GetCurrentProcess(),wantedAffinityMask);
  }
  #endif
}

int GetCPUCount()
{
  // when CPU count is given from command line, we want to respect it even on Xbox retail version
  // such command lines are used during the build process
  if (ForcedCPUCount > 0) return ForcedCPUCount;
#if DUMMY_MULTICORE
  return 1;
#elif defined _WIN32
  int count = 0;
  DWORD processAffinityMask = 0;
  DWORD systemAffinityMask = 0;
  if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinityMask, &systemAffinityMask))
  {
    while (processAffinityMask != 0)
    {
      if (processAffinityMask & 1) count++;
      processAffinityMask >>= 1;
    }
  }
  // TODO:: add real hyperthreading detection
  if (count>=8) count=count/2; // heuristics: when 8 cores or more, assume hyperthreading - we do not want to use hyperthreaded cores
  // note: 5 or 7 cores very rary, but 6 cores most likely really means 6 cores (AMD Phenom II X6)
  // only 31 CPUs because of bitmask limitations
  if (count>31) count = 31;
  return count;
#else
  FILE * fp;
  char res[128];
  fp = popen("/bin/cat /proc/cpuinfo |grep -c '^processor'","r");
  fread(res, 1, sizeof(res)-1, fp);
  fclose(fp);
  int cpuCount = atoi(res);
  return cpuCount>0 ? cpuCount : 1;
#endif
}

#if USE_THREAD_NAME

#define MS_VC_EXCEPTION 0x406D1388

struct THREADNAME_INFO
{
  DWORD dwType;     // Must be 0x1000
  LPCSTR szName;    // Pointer to name (in user address space)
  DWORD dwThreadID; // Thread ID (-1 for caller thread)
  DWORD dwFlags;    // Reserved for future use; must be zero
};

void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName)
{
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = szThreadName;
  info.dwThreadID = dwThreadID;
  info.dwFlags = 0;

  __try
  {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(DWORD), (DWORD *)&info);
  }
  __except (EXCEPTION_CONTINUE_EXECUTION)
  {
  }
}

#endif

struct ThreadInfo
{
  DWORD _threadId;
  int _cpu;
  RString _name;

  ThreadInfo(DWORD threadId, int cpu, RString name)
    : _threadId(threadId), _cpu(cpu), _name(name) {}
};
TypeIsMovableZeroed(ThreadInfo)

struct ThreadInfoTraits
{
  typedef DWORD KeyType;
  /// check if two keys are equal
  static bool IsEqual(DWORD a, DWORD b) {return a == b;}
  /// get a key from an item
  static DWORD GetKey(const ThreadInfo &a) {return a._threadId;}
};

static FindArrayKey<ThreadInfo, ThreadInfoTraits> ThreadsMap;

void RegisterThread(DWORD threadId, int cpu, const char *name)
{
  ThreadsMap.AddUnique(ThreadInfo(threadId, cpu, name));
}

RString GetThreadName(DWORD threadId)
{
  int index = ThreadsMap.FindKey(threadId);
  if (index < 0) return Format("Id=%d",threadId);
  return ThreadsMap[index]._name;
}

int GetThreadCPU(DWORD threadId)
{
  int index = ThreadsMap.FindKey(threadId);
  if (index < 0) return -1;
  return ThreadsMap[index]._cpu;
}

#if !DUMMY_MULTICORE
struct InheritFPUSettingsContext
{
  LPTHREAD_START_ROUTINE threadProc;
  void *context;
# if USE_THREAD_NAME
  RString debugName;
#endif
  
  // x87 control word to inherit
  int fpuControl;
  #if USING_SSE
    // SSE control word to inherit
    int mmCSR;
  #endif
};
#endif

#if !DUMMY_MULTICORE
static DWORD WINAPI InheritFPUSettings(void *context)
{
  GMemFunctions->OnThreadEnter();
  
  InheritFPUSettingsContext *fpuContext = (InheritFPUSettingsContext *)context;
# if USE_THREAD_NAME
  // VisualStudio 2005 overrides the thread name by the name of the function.
  // we need to set it again once the thread is running, it is not enough to set it before running
  SetThreadName(-1, fpuContext->debugName);
# endif
#if !USE_MT_LIB_FOR_JOBS //no FPU tweaking on Linux (also InitFPU function in main.cpp is empty outside _WIN32)
  _control87(fpuContext->fpuControl,~0);
  // the main purpose of this is to inherit precision - check if we have been sucessfull
  Assert((_controlfp(0, 0)&_MCW_PC)==(fpuContext->fpuControl&_MCW_PC));
  #if USING_SSE
     _mm_setcsr(fpuContext->mmCSR);
  #endif
#endif
  
  void *threadContext = fpuContext->context;
  LPTHREAD_START_ROUTINE start = fpuContext->threadProc;
  delete fpuContext;
  DWORD ret = start(threadContext);
  
  GMemFunctions->OnThreadExit();
  return ret;
}

#if USE_MT_LIB_FOR_JOBS
MultiThread::ThreadBase *CreateMTThreadOnCPU(
                         DWORD stackSize, LPTHREAD_START_ROUTINE startAddress, LPVOID parameter, int cpu, MultiThread::ThreadBase::Priority priority, const char *name
                         )
{
  // dynamic allocation needed here, we cannot use stack storage
  // this function can terminate (and stack vars are destroyed) before the new thread processed the data
  InheritFPUSettingsContext *fpuContext = new InheritFPUSettingsContext;
  fpuContext->context = parameter;
  fpuContext->threadProc = startAddress;

  ThreadObject *threadObj = new ThreadObject(stackSize, InheritFPUSettings, fpuContext, priority);
  threadObj->Start();
  static DWORD threadId = 0xaa00;
  threadId++;
  RegisterThread(threadId, cpu, name);
  // SetThreadPriority(handle, priority); 
  // ResumeThread(handle); //thread->Start();
  return threadObj;
}
#endif

#ifdef _WIN32
/**
@param threadIdRet [out] [optional] return thread ID value
*/
HANDLE CreateThreadOnCPU(
  DWORD stackSize, LPTHREAD_START_ROUTINE startAddress, LPVOID parameter, int cpu, int priority, const char *name,
  DWORD *threadIdRet
)
{
  DWORD threadId;
  // dynamic allocation needed here, we cannot use stack storage
  // this function can terminate (and stack vars are destroyed) before the new thread processed the data
  InheritFPUSettingsContext *fpuContext = new InheritFPUSettingsContext;
  fpuContext->context = parameter;
  fpuContext->threadProc = startAddress;

  // store the FPU control word
  fpuContext->fpuControl = _control87(0,0);
  # if USE_THREAD_NAME
    fpuContext->debugName = name;
  #endif
  #if USING_SSE
    fpuContext->mmCSR = _mm_getcsr();
  #endif

  HANDLE handle = CreateThread(NULL, stackSize, InheritFPUSettings, fpuContext, CREATE_SUSPENDED, &threadId);
  if (handle)
  {
    // TODO: ??? place thread on given CPU using SetThreadAffinityMask or SetThreadIdealProcessor

# if USE_THREAD_NAME
    if (name) SetThreadName(threadId, name);
# endif
    RegisterThread(threadId, cpu, name);

    SetThreadPriority(handle, priority);

    ResumeThread(handle);
    
    if (threadIdRet) *threadIdRet = threadId;
  }
  return handle;
}
#endif
#endif

#ifndef __IMEXHND_H__
#define __IMEXHND_H__
//-----------------------------------------------------------------------------
//
// FILE: IMEXHND.H
//
//
// (c) Interactive Magic (1997)
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------
// Version tracking information
//-----------------------------------------------------------------------

#include <Es/Common/win.h>
#include <El/Interfaces/iAppInfo.hpp>

class MapFile;

//! information about single call
struct CallInfo
{
  void *retAddr; //!< return address
  void *fStart; //!< address of function containing return address
  void *calledAddr; //!< called address found at given return address
  //! number of bytes allocated in the stack frame belonging to the fStart
  int stackFrameSize;
};

TypeIsSimple(CallInfo);

class DebugExceptionTrap
{
  TCHAR m_szLogFileName[MAX_PATH];
  LPTOP_LEVEL_EXCEPTION_FILTER m_previousFilter;
  HANDLE m_hReportFile;
  bool m_header;
  bool m_configHeader;
  void *m_stackBottom;
  /// how many lines was logged without flushing
  int m_countFlushLines;

  bool m_silentMode;//silent mode - disables window that alerts user about crash
  /// make sure first crash dump is not overwritten by subsequent crashes
  bool m_dumpOnce;

  bool DumpOnce()
  {
    bool ret = m_dumpOnce;
    m_dumpOnce = false;
    return ret;
  }

  //! callstack information
  //enum {MaxCalls = 2048};
  //CallInfo _callstack[MaxCalls];
  //int _calls; // actual number of stored calls

  static void OptimizeCalls(void *startFunction, CallInfo *callstack, int &calls);

  //void OptimizeCalls(void *startFunction);
  void PrintCalls(CallInfo *callstack, int &calls, MapFile &map, void *pc);
  static void DeleteCall(CallInfo *callstack, int &calls, int i);

public:
  DebugExceptionTrap();
  ~DebugExceptionTrap();
  
  void SetSilentMode();
  void SetLogFileName( const char *pszLogFileName );
  static void LogFFF( const char *text, bool stack=true, bool force=false );

  //! extract callstack information from current context
  //! no new/delete guaranteed
  void ExtractCallstack(CallInfo *callstack, int &calls, bool validate=true, MapFile *map=NULL);

  //! extract callstack information from current context
  //! no new/delete guaranteed
  void ExtractCallstack(void **callstack, int &calls, bool validate=true, MapFile *map=NULL);

  void LogLine(const char *text, ...);
  void SaveContext(const char *text);
  void PrintConfig();

  //! Ensures, that object is inicialized
  /** This function doing nothing, but linker includes this object into executable
    when it is used from LIB file.     
  */
  void EnsureInitialized();

  void LogFSP(
    void *eip, DWORD *ebp, DWORD *esp, const char *text, bool stack=true, bool force=false
  );
  void SetStackBottom( void *stackBottom ){m_stackBottom=stackBottom;}
  void *GetStackBottom() const {return m_stackBottom;}

  void ReportContext( const char *text, CONTEXT *context, bool mtSafe=false );

  //! extract callstack information from given stack
  void ExtractThreadCallstack(HANDLE threadHandle, DWORD *stackBot, CallInfo *callstack, int &calls, bool validate=true);
  
  int GenerateMinidump(PEXCEPTION_POINTERS pExceptionInfo);

  static void DDTerm() {CurrentAppInfoFunctions->DDTerm();}

  /// used when app lost focus to make sure handle is not open
  void FlushLog(){CloseLogFile();}

#if _USE_DEAD_LOCK_DETECTOR
  void GenerateBidmp( char *ext );
#endif
 
private:

  void OpenLogFile();
  void CloseLogFile();
  void PrintHeader();

  void FlushLogFile();
  
  // entry point where control comes on an unhandled exception
  static LONG WINAPI ExceptionCallback( PEXCEPTION_POINTERS pExceptionInfo );
  LONG UnhandledExceptionFilter( PEXCEPTION_POINTERS pExceptionInfo );

  // where report info is extracted and generated 
  void PrintBanner();
  void PrintContext( void *ip, CONTEXT *ctx, bool mtSafe=false );
  void GenerateExceptionReport( PEXCEPTION_POINTERS pExceptionInfo );

  // Helper functions
  static LPTSTR GetExceptionString( DWORD dwCode );
  static BOOL GetLogicalAddress(  PVOID addr, PTSTR szModule, DWORD len,
                  DWORD& section, DWORD& offset );
  
  void IntelStackWalk
  (
    DWORD eip, DWORD *ebp, DWORD *pStackTop, DWORD *pStackBot
  );
  void IntelStackWalk( PCONTEXT pContext );

  //! extract callstack information from given stack
  static void ExtractCallstack
  (
    CallInfo *callstack, int &calls, void *eip, DWORD *ebp, DWORD *stackTop, DWORD *stackBot,
    bool validate=true, MapFile *map=NULL, bool firstEntryEIP=true
  );


  void IntelStackSave
  (
    DWORD eip, DWORD *pStackTop, DWORD *pStackBot
  );

  void IntelStackSave( PCONTEXT pContext, char *fileExt=NULL );

  void IntelPCPrint( class MapFile &map, int pc, bool doEol=true );
  int CCALL _tprintf(const TCHAR * format, ...);
  int CCALL _tlprintf(const TCHAR * format, ...);
};

extern DebugExceptionTrap GDebugExceptionTrap;  //  global instance of class

#endif

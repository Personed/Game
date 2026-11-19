//-----------------------------------------------------------------------------
//
// FILE: IMEXHND.CPP
// 
// Matt Pietrek
// Microsoft Systems Journal, April 1997
//
// (c) Interactive Magic (1997)
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------
// Version tracking information
//-----------------------------------------------------------------------

#ifdef _WIN32

#include <El/elementpch.hpp>

#include <tchar.h>
#include <time.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Es/Memory/normalNew.hpp>
#include <xutility>
#include <Es/Memory/debugNew.hpp>

#include "imexhnd.h"
#include "debugTrap.hpp"
#include <El/CRC/crc.hpp>
#include <Es/Files/filenames.hpp>

#include "mapFile.hpp"



#if _ENABLE_REPORT
#define DEBUGGER_DETECTION 1
#else
#define DEBUGGER_DETECTION 1
#endif

#if _RELEASE && !_PROFILE
  #define STACK_FRAME_POINTERS 0
#else
  #define STACK_FRAME_POINTERS 1
#endif

#define GUARDED(x) do {__try {x;} __finally {}} while (false)

//============================== Global Variables =============================

//
// Declare the static variables of the DebugExceptionTrap class
//


DebugExceptionTrap GDebugExceptionTrap;  // Declare global instance of class

//============================== Class Methods =============================

//=============
// Constructor 
//=============
DebugExceptionTrap :: DebugExceptionTrap( )
{
  m_previousFilter = NULL;
  m_dumpOnce = true;
  
  #if DEBUGGER_DETECTION
  if (!GDebugger.IsDebugger())
  #endif
  {
    m_silentMode = false;
    m_header = false; // header printer
    m_countFlushLines = 0;
    m_configHeader = false;
    // Install the unhandled exception filter function
    m_previousFilter = SetUnhandledExceptionFilter(ExceptionCallback);

    // Figure out what the report file will be named, and store it away
    GetModuleFileName( 0, m_szLogFileName, MAX_PATH );
    
    // Look for the '.' before the "EXE" extension.  Replace the extension
    // with "RPT"
    PTSTR pszDot = _tcsrchr( m_szLogFileName, _T('.') );
    if ( pszDot )
    {
        pszDot++;   // Advance past the '.'
        if ( _tcslen(pszDot) >= 3 )
            _tcscpy( pszDot, _T("RPT") );   // "RPT" -> "Report"
    }
  }
}

//============
// Destructor 
//============
DebugExceptionTrap :: ~DebugExceptionTrap( )
{
  if (m_previousFilter)
  {
    SetUnhandledExceptionFilter( m_previousFilter );
    m_previousFilter = NULL;
  }
  // the log file may be still open - close it if needed
  CloseLogFile();
}

void DebugExceptionTrap :: SetSilentMode(void)
{
  m_silentMode = true;

}

//==============================================================
// Lets user change the name of the report file to be generated 
//==============================================================

/*!
  \patch 5095 Date 12/4/2006 by Ondra
  - Fixed: Report file size limited to 2 MB to prevent filling a lot of disc space.
*/
void DebugExceptionTrap :: SetLogFileName( const char *pszLogFileName )
{
  // close the file in case it was open because something was already writing into it
  CloseLogFile();
  
  _tcscpy( m_szLogFileName, pszLogFileName );

  {

    // reduce file size if (as) needed
    // we need the source only for reading here
    HANDLE rFile  = CreateFile(
        m_szLogFileName,
        GENERIC_READ,
        0,0,
        OPEN_EXISTING,
        0,0
    );
    if (rFile!=INVALID_HANDLE_VALUE)
    {
      DWORD size = GetFileSize(rFile,NULL);
      const DWORD maxSize = 2*1024*1024;
      //const DWORD maxSize = 128*1024;
      // open the file
      bool success = false;

      // create a target file (always empty)
      BString<512> tempName;
      strcpy(tempName,m_szLogFileName);
      strcat(tempName,".tmp");

      if (size>maxSize)
      {
        // skip as much as needed
        SetFilePointer(rFile,size-maxSize,NULL,FILE_BEGIN);
        size = maxSize;

        const size_t bufSize = 64*1024;

        char buf[bufSize];
        DWORD read;
        if (ReadFile(rFile,buf,bufSize,&read,NULL)==FALSE || read!=bufSize)
        {
          // the first one should not be partial, if it is, there is no need to cur the file
          goto Done;
        }
        char *startWrite = buf;
        int sizeWrite = bufSize;
        // skip first line int the first buffer, as it is probably only partial
        char *eol = (char *)memchr(buf,'\n',bufSize);
        if (eol)
        {
          startWrite = eol+1;
          size_t skip = startWrite-buf;
          sizeWrite -= skip;
          size -= skip;
        }
        HANDLE tFile = CreateFile(
          tempName,
          GENERIC_WRITE,
          0,0,
          CREATE_ALWAYS,
          0,0
        );
        // if temporary has failed, do nothing
        if (!tFile) goto Done;
         
        // prevent fragmentation by resizing the target file in advance
        SetFilePointer(tFile,size,NULL,FILE_BEGIN);
        SetEndOfFile(tFile);
        // return to the beginning
        SetFilePointer(tFile,0,NULL,FILE_BEGIN);
        
        // now copy
        // first write the partial buffer we have
        DWORD written;
        if (WriteFile(tFile,startWrite,sizeWrite,&written,NULL)==FALSE || written!=sizeWrite)
        {
          goto ErrorWriting;
        }
        size -= written;

        // now copy the rest of the source file
        while(size>0)
        {
          size_t left = size;
          if (left>bufSize) left = bufSize;
          if (ReadFile(rFile,buf,left,&read,NULL)==FALSE || read!=left) goto ErrorWriting;
          if (WriteFile(tFile,buf,left,&written,NULL)==FALSE || written!=left) goto ErrorWriting;
          size -= left;
        }

        //DoneWriting:
        SetEndOfFile(tFile);
        if (!CloseHandle(tFile))
        {
          if (false) // only goto can enter this scope
          {
            ErrorWriting:
            CloseHandle(tFile);
          }
          // unable to shorten - keep it as it is
          DeleteFile(tempName);
        }
        else
        {
          success = true;
          // we cannot delete, source is still open for reading
        }

      }
      Done: 
      CloseHandle(rFile);

      if (success)
      {
        // delete the old (big) one
        DeleteFile(m_szLogFileName);
        // rename the new one
        MoveFile(tempName,m_szLogFileName);

      }
    }
  }
}

// file operations
void DebugExceptionTrap::OpenLogFile()
{
  if (!m_hReportFile)
  {
    #if DEBUGGER_DETECTION
    if (!GDebugger.IsDebugger())
    #endif
    {
      m_hReportFile = CreateFile(
        m_szLogFileName,
        GENERIC_WRITE,
        FILE_SHARE_READ,0,
        OPEN_ALWAYS, //CREATE_ALWAYS,
        0,0
      );
    }
    if (m_hReportFile==INVALID_HANDLE_VALUE) m_hReportFile = NULL;
    

    if ( m_hReportFile )
    {
      SetFilePointer( m_hReportFile, 0, 0, FILE_END );

      PrintHeader();
    }
  }
}

static void HidePassword(LPTSTR text)
{
  // replace all chars after -password= with asterisks to the first space char
  const TCHAR *passphrase = _T("-password=");
  LPTSTR ptr = _tcsstr(text, passphrase);
  if (ptr)
  {
    ptr = ptr + _tcslen(passphrase);
    while (*ptr && !_istspace(*ptr))
    {
      *ptr++ = _T('*');
    }
  }
}

void DebugExceptionTrap :: PrintHeader()
{
  if( !m_header )
  {
    m_header=true;
    // scan session information

    TCHAR exeName[MAX_PATH];
    GetModuleFileName( 0, exeName, MAX_PATH );
    LPTSTR args = GetCommandLine();

    _tprintf( _T("\r\n") );
    _tprintf( _T("\r\n") );
    _tprintf( _T("=====================================================================\r\n") );
    _tprintf( _T("== %s\r\n"), exeName );
    HidePassword(args);
    _tprintf( _T("== %s\r\n"), args );
    _tprintf( _T("=====================================================================\r\n") );

    struct _stat exeStat;
    if( _stat(exeName,&exeStat)>=0 )
    {
      tm *t = localtime(&exeStat.st_mtime);
      _tprintf(
        _T("Exe timestamp: %4d/%02d/%02d %02d:%02d:%02d\r\n"),
        t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec
      );
    }

    { // print current time and date
      time_t tt;
      time(&tt);
      tm *t = localtime(&tt);
      _tprintf(
        _T("Current time:  %4d/%02d/%02d %02d:%02d:%02d\r\n\r\n"), 
        t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec
      );
    }
    _tlprintf( _T("Version %s.%d\r\n"), (const char *)CurrentAppInfoFunctions->GetVersionText(), CurrentAppInfoFunctions->GetBuildNumber());

    FlushLogFile();

  }
}

void DebugExceptionTrap :: CloseLogFile()
{
  if ( m_hReportFile )
  {
    CloseHandle( m_hReportFile );
    m_hReportFile = 0;
  }
}

#if _ENABLE_REPORT
const int MaxTextReported = 32;
static BString<256> TextReported[MaxTextReported];
static int NTextReported;
#endif

void DebugExceptionTrap::ReportContext( const char *text, CONTEXT *context, bool mtSafe )
{
  if (!DumpOnce()) return;

  // Generate MiniDump - to receive a exception pointer, we will force an exception
  __try
  {
    int *badPtr = NULL;
    *badPtr = 0;
  }
  __except(GDebugExceptionTrap.GenerateMinidump(GetExceptionInformation()))
  {
  }
  
  #if !_ENABLE_REPORT
  GDebugExceptionTrap.IntelStackSave(context);
  #endif

  OpenLogFile();
  
  if( text ) _tprintf( _T("===%s====>>>>>>BEG\r\n"), text );

  #if _M_PPC
    PrintContext((void *)context->Iar,context,mtSafe);
  #else
    PrintContext((void *)context->Eip,context,mtSafe);
  #endif
  FlushLogFile();
  
  #if _ENABLE_REPORT
  IntelStackWalk(context);
  #endif

  if( text ) _tprintf( _T("===%s====>>>>>>END\r\n"), text );
  
  FlushLogFile();
    
  CloseLogFile();
}

void DebugExceptionTrap :: ExtractThreadCallstack(HANDLE threadHandle, DWORD *stackBot, CallInfo *callstack, int &calls, bool validate)
{
  CONTEXT context;
  context.ContextFlags=CONTEXT_FULL; // TODO: replace with minimal context needed
  ::SuspendThread(threadHandle);
  if (GetThreadContext(threadHandle,&context))
  {
    // TODO: detect when stackframes are used and we can use EBP
    // main thread stack limits
    #if STACK_FRAME_POINTERS
      DWORD ebp = context.Ebp;
    #else
      DWORD ebp = 0;
    #endif
    GDebugExceptionTrap.ExtractCallstack(
      callstack,calls,(void *)context.Eip,(DWORD *)ebp,(DWORD *)context.Esp,stackBot
    );
    
    //GDebugExceptionTrap.OptimizeCalls(func,callstack,callstackSize);
    // write only the called addresses to the pipe - it will ignore the rest anyway
  }
  else
  {
    calls = 0;
  }
  
  ::ResumeThread(threadHandle);
}

void DebugExceptionTrap :: FlushLogFile()
{
  if (m_hReportFile) FlushFileBuffers(m_hReportFile),m_countFlushLines=0;
}

/*!
\patch 5095 Date 12/4/2006 by Ondra
- Fixed: Large .rpt file could slow the game down when adding into a report freqently.
*/
void DebugExceptionTrap :: LogFSP( void *eip, DWORD *ebp, DWORD *esp, const char *text, bool stack, bool force )
{
  /*
  // normal stack entry
  008FA30A push        ebp
  008FA30B mov         ebp,esp
  008FA30D sub         esp,0Ch
  */
  // assume there are no local variables on esp
  // find first ebp (frame pointer)
  
  OpenLogFile();
  // note, one cannot use "%s\r\n" as text can already have 1024 characters (limit inside LstF)
  // moreover one cannot use _tprintf(text), as text can contain printf escape character %
  _tprintf( "%s", text );
  _tprintf( _T("\r\n") );
  
#if _ENABLE_REPORT
  if( stack )
  {
    #if DEBUGGER_DETECTION
    if (GDebugger.IsDebugger())
    {
      BREAK();
    }
    #endif

    // check if this text was already reported
    int i = NTextReported;
    if (!force)
    {
      for( i=0; i<NTextReported; i++ )
      {
        if( !strcmp(TextReported[i],text) ) break;
      }
    }

    if( i>=NTextReported )
    {
      // if we reached the end, cycle
      if (NTextReported>=MaxTextReported) NTextReported = 0;
      strcpy(TextReported[NTextReported++],text);
      // print mission header
      PrintConfig();
      if (m_hReportFile) /*PrintMissionInfo(m_hReportFile)*/CurrentAppInfoFunctions->VariableHeader(m_hReportFile);

      // generate stack report
      IntelStackWalk((DWORD)eip,ebp,esp,(DWORD *)m_stackBottom);
      FlushLogFile();
    }
  }
#endif

  //CloseLogFile();
}

#pragma warning(disable:4035)


#if STACK_FRAME_POINTERS

inline void NextFrame(DWORD &ebp, DWORD &eip)
{
  if (!ebp) return;
  eip = ((DWORD *)ebp)[1];
  ebp = ((DWORD *)ebp)[0];
}

static void SkipFrames(DWORD &ebp, DWORD &eip, int frames)
{
  while (--frames>=0)
  {
    if (!ebp) return;
    NextFrame(ebp,eip);
  }
}

/*
static DWORD __declspec(naked) GetEIP(DWORD &espRet, DWORD &ebpRet)
{
  __asm
  {
    push        ebp  
    mov         ebp,esp 
    push        ecx  
    
    mov         eax,dword ptr [ebp]
    
    mov         dword ptr [edx],eax 
    mov         edx,dword ptr [ebp+8] 
    mov         dword ptr [edx],eax 
    
    mov         eax,dword ptr [ecx+4] 
    leave            
    ret              
  }
}
*/


// implement GetEIP so that is always skips its own stack frame
static DWORD GetEIP(DWORD &espRet, DWORD &ebpRet)
{
#if _M_IX86
  // entry to this function looks like
  // 008FA442   push        ebp
  // 008FA443   mov         ebp,esp
  // 008FA445   sub         esp,18h
  DWORD *ebpCur;
  __asm
  {
    mov ebpCur,ebp
  };
  // ebpCur pointing to stack frame of this function
  // we want to skip this frame and get frame of our caller
  ebpRet = ebpCur[0];
  espRet = ebpRet;
  DWORD retAddr = ebpCur[1];
  return retAddr;
#else
  // TODOX360: PowerPC - get return address
  return 0;
#endif
}

#else

#pragma optimize("yg",off)

// implement GetEIP so that is always skips its own stack frame
static DWORD GetEIP(DWORD &espRet, DWORD &ebpRet)
{
  #if _M_IX86
    // entry to this function looks like
    // 008FA442   push        ebp
    // 008FA443   mov         ebp,esp
    // 008FA445   sub         esp,18h
    // ebpCur pointing to stack frame of this function
    // we want to skip this frame and get frame of our caller
    DWORD *ebpCur;
    __asm
    {
      mov ebpCur,ebp
    };
    ebpRet = 0;
    espRet = ebpCur[0];
    DWORD retAddr = ebpCur[1];
    return retAddr;
  #else
    // TODOX360: PowerPC - get return address
    return 0;
  #endif
}


inline void NextFrame(DWORD &ebp, DWORD &eip)
{
  // TODO: implement frame skip in release mode
}

#endif

#pragma warning(default:4035)

void DebugExceptionTrap :: LogFFF( const char *text, bool stack, bool force )
{
  // get eip, esp
  DWORD espReg = 0,ebpReg = NULL;
  //
  DWORD eipReg = GetEIP(espReg,ebpReg);

  NextFrame(ebpReg,eipReg);

  //GDebugExceptionTrap.LogFSP(LogF,(DWORD *)&text,text,stack);
  GDebugExceptionTrap.LogFSP((void *)eipReg,(DWORD *)ebpReg,(DWORD *)espReg,text,stack,force);
}

#if _USE_DEAD_LOCK_DETECTOR
void DebugExceptionTrap :: GenerateBidmp( char *ext )
{
  CONTEXT context;
  memset(&context, 0, sizeof(CONTEXT));
  context.Eip = GetEIP(context.Esp, context.Ebp);
  IntelStackSave(&context, ext);
}
#endif

#pragma optimize("",on)

void DebugExceptionTrap :: LogLine(const char *text, ...)
{
  char szBuff[256];
  va_list argptr;
        
  va_start( argptr, text );
  wvsprintf( szBuff, text, argptr );
  va_end( argptr );
  OpenLogFile();
  _tprintf( _T("%s\r\n"), szBuff);
  //CloseLogFile();
}

void DebugExceptionTrap :: SaveContext(const char *name)
{
  CONTEXT context;
  context.ContextFlags = CONTEXT_ALL;
  GetThreadContext(GetCurrentThread(),&context);
  DWORD espReg,ebpReg;

  #if !_M_PPC
    context.Eip = (DWORD)GetEIP(espReg,ebpReg);
  #else
    context.Iar = (DWORD)GetEIP(espReg,ebpReg);
  #endif

  ReportContext(name,&context,true);
}

#pragma optimize( "", off ) //turn off optimization. Safe Mode here

// by default mini-dumps are enabled now

#ifndef MINIDUMPTYPE

#define MINIDUMPTYPE_FULL MiniDumpNormal|MiniDumpWithDataSegs|MiniDumpWithFullMemory
#define MINIDUMPTYPE_MIDDLE MiniDumpNormal|MiniDumpWithDataSegs|MiniDumpWithIndirectlyReferencedMemory
#define MINIDUMPTYPE_SMALL MiniDumpNormal

#if defined GENERATE_MINIDUMP
// GENERATE_MINIDUMP users want to have a complete dump
#define MINIDUMPTYPE MINIDUMPTYPE_FULL
#else
// not specified otherwise, assume middle mini dumps as a reasonable default
#define MINIDUMPTYPE MINIDUMPTYPE_MIDDLE
#endif

#endif

#include <DbgHelp.h>

typedef BOOL (WINAPI  *type_MiniDumpWriteDump)(
                                       HANDLE hProcess,
                                       DWORD ProcessId,
                                       HANDLE hFile,
                                       MINIDUMP_TYPE DumpType,
                                       PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                       PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                       PMINIDUMP_CALLBACK_INFORMATION CallbackParam
                                       );
static int dumpcnt=0;

int DebugExceptionTrap ::GenerateMinidump(PEXCEPTION_POINTERS pExceptionInfo)
{
  HMODULE lib=LoadLibrary("dbghelp.dll");
  if (lib==NULL) 
  {
    GUARDED(_tlprintf( _T("error: Minidump is unavailable, dlghelp.dll load error...\r\n") ));
    return EXCEPTION_EXECUTE_HANDLER;  
  }
  type_MiniDumpWriteDump pMiniDumpWriteDump=(type_MiniDumpWriteDump)GetProcAddress(lib,"MiniDumpWriteDump");
  if (pMiniDumpWriteDump==NULL)
  {
    GUARDED(_tlprintf(_T("error: Minidump is unavailable, cannot link function MiniDumpWriteDump from dlghelp.dll...\r\n")));
    return EXCEPTION_EXECUTE_HANDLER;
  }
  MINIDUMP_EXCEPTION_INFORMATION expinfo;
  expinfo.ClientPointers=TRUE;
  expinfo.ExceptionPointers=pExceptionInfo;
  expinfo.ThreadId=GetCurrentThreadId();


  char crashName[1024];
  strcpy(crashName,m_szLogFileName);
  strcpy(GetFileExt(crashName),".mdmp");

  HANDLE minidumpFile=CreateFile(crashName,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);
  
  if (minidumpFile!=INVALID_HANDLE_VALUE)
  {
    GUARDED(LogF(_T("Generating minidump please wait.\r\n")));
    if (pMiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),minidumpFile,(MINIDUMP_TYPE)(MINIDUMPTYPE),&expinfo,NULL,NULL))
      GUARDED(_tlprintf(_T("note: Minidump has been generated into the file %s\r\n"),cc_cast(crashName)));
    else
      GUARDED(_tlprintf(
        _T("error: Minidump is unavailable. MiniDumpWriteDump into %s failed (error 0x%x).\r\n"),
        cc_cast(crashName),GetLastError()
      ));
    CloseHandle(minidumpFile);
  }
  else
    _tlprintf(
      _T("error: Minidump is unavailable. Cannot create minidump file %s (error 0x%x)\r\n"),
      cc_cast(crashName), GetLastError()
    );
  FreeLibrary(lib);
  dumpcnt++;

  return EXCEPTION_EXECUTE_HANDLER;
}

/// convert a pure call into an ordinary crash, the function name will be enough to identify it
/** see http://www.drdobbs.com/184416623 or http://www.lenholgate.com/archives/000623.html */
extern "C" int _cdecl _purecall(void)
{
  volatile int a = 0;
  return 1/a;
}

/// convert abort into an ordinary crash, the function name will be enough to identify it
/** http://stackoverflow.com/questions/4215842/what-is-c-runtime-library-runtime-error/4243238#4243238 */
extern "C" void __cdecl abort (void)
{
  #if _DEBUG
  __debugbreak();
  #else
  volatile int a = 0;
  a = 1/a;
  #endif
}


//===========================================================
// Entry point where control comes on an unhandled exception 
//===========================================================

LONG WINAPI DebugExceptionTrap::ExceptionCallback( PEXCEPTION_POINTERS pExceptionInfo )
{
  return GDebugExceptionTrap.UnhandledExceptionFilter(pExceptionInfo);
}

LONG DebugExceptionTrap :: UnhandledExceptionFilter( PEXCEPTION_POINTERS pExceptionInfo )
{ 
  if (!DumpOnce()) return EXCEPTION_CONTINUE_SEARCH;

  // no alive will arive any more
  GDebugger.NextAliveExpected(INT_MAX);
  // during exception handling restore previous exception filter
  SetUnhandledExceptionFilter( m_previousFilter );

  GUARDED(LogF(_T("Unhandled exception.\r\n")));  

  OpenLogFile();
  if ( m_hReportFile )
  {
    GenerateExceptionReport( pExceptionInfo );
  }
  
  GenerateMinidump(pExceptionInfo );

  if (m_silentMode)
  {    
    TerminateProcess(GetCurrentProcess(),1);    
  }    
  

  CloseLogFile();

  LONG ret = EXCEPTION_CONTINUE_SEARCH;
  
  if ( m_previousFilter )
  {
    ret = m_previousFilter( pExceptionInfo );
  }
  
  // exception handling finished - use custom handler again  
  SetUnhandledExceptionFilter(ExceptionCallback);
  
  return ret;
}
#pragma optimize( "", on )

void DebugExceptionTrap::PrintBanner()
{
  if (!m_hReportFile) return;

  // Start out with a banner
  _tprintf( _T("=======================================================\r\n") );
  _tprintf( _T("-------------------------------------------------------\r\n") );
}

void DebugExceptionTrap::PrintConfig()
{
  if (m_configHeader) return;
  if (!m_hReportFile) return;

  if (CurrentAppInfoFunctions->ConstantHeader(m_hReportFile)) m_configHeader = true;
}

void DebugExceptionTrap::PrintContext( void *ip, CONTEXT *ctx, bool mtSafe )
{
  if (!mtSafe)
  {
    PrintConfig();
  }

  TCHAR szFaultingModule[MAX_PATH];
  DWORD section, offset;
  GetLogicalAddress(  ip,
                      szFaultingModule,
                      sizeof( szFaultingModule ),
                      section, offset );

  _tlprintf( _T("Version %s.%d\r\n"), (const char *)CurrentAppInfoFunctions->GetVersionText(), CurrentAppInfoFunctions->GetBuildNumber());
  _tlprintf( _T("Fault address:  %08X %02X:%08X %s\r\n"),
              ip,
              section, offset, szFaultingModule );

  if (m_hReportFile && !mtSafe)
  {
    CurrentAppInfoFunctions->VariableHeader(m_hReportFile);
  }

  if (!IsBadReadPtr((char *)ip-16,32))
  {
    char code[1024];

    *code = 0;
    for (int i=0; i<16; i++)
    {
      sprintf(code+strlen(code)," %02X",((unsigned char *)ip-16)[i]);
    }
    _tlprintf( _T("Prev. code bytes:%s\r\n"), code );

    *code = 0;
    for (int i=0; i<16; i++)
    {
      sprintf(code+strlen(code)," %02X",((unsigned char *)ip)[i]);
    }
    _tlprintf( _T("Fault code bytes:%s\r\n"), code );
  }

  // Show the registers
  #ifdef _M_IX86  // Intel Only!
  _tlprintf( _T("\r\nRegisters:\r\n") );

  _tlprintf(_T("EAX:%08X EBX:%08X\r\nECX:%08X EDX:%08X\r\nESI:%08X EDI:%08X\r\n"),
          ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi );

  _tlprintf( _T("CS:EIP:%04X:%08X\r\n"), ctx->SegCs, ctx->Eip );
  _tlprintf( _T("SS:ESP:%04X:%08X  EBP:%08X\r\n"),
              ctx->SegSs, ctx->Esp, ctx->Ebp );
  _tlprintf( _T("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n"),
              ctx->SegDs, ctx->SegEs, ctx->SegFs, ctx->SegGs );
  _tlprintf( _T("Flags:%08X\r\n"), ctx->EFlags );

  #endif
}

//===========================================================================
// Open the report file, and write the desired information to it.  Called by 
// UnhandledExceptionFilter                                               
//===========================================================================

/*!
\patch 1.02 Date 7/6/2001 by Ondra.
- New: Save context.bin on crash for post-mortem analysis.
\patch_internal 1.42 Date 1/8/2002 by Ondra
- Fixed: Removed double Fault address line in Flashpoint.rpt"
*/

void DebugExceptionTrap :: GenerateExceptionReport(
    PEXCEPTION_POINTERS pExceptionInfo )
{

  PrintBanner();

  CurrentAppInfoFunctions->FlushLogs(m_hReportFile);
  // PrintNetworkInfo(m_hReportFile);

  PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;

  // First print information about the type of fault
  _tlprintf(   _T("Exception code: %08X %s at %08X\r\n"),
              pExceptionRecord->ExceptionCode,
              GetExceptionString(pExceptionRecord->ExceptionCode),
              pExceptionRecord->ExceptionAddress
              );

  FlushLogFile();
  // save stack first - in case map file operation will fail
  IntelStackSave( pExceptionInfo->ContextRecord );

  // Now print information about where the fault occured
  /*
  TCHAR szFaultingModule[MAX_PATH];
  DWORD section, offset;
  GetLogicalAddress(  pExceptionRecord->ExceptionAddress,
                      szFaultingModule,
                      sizeof( szFaultingModule ),
                      section, offset );
  */

  PrintContext(pExceptionRecord->ExceptionAddress,pExceptionInfo->ContextRecord);
  _tlprintf( _T("=======================================================\r\n") );

  #if _ENABLE_REPORT
    // Walk the stack using x86 specific code
    IntelStackWalk( pExceptionInfo->ContextRecord );

    _tlprintf( _T("\r\n") );
    FlushLogFile();
  #endif
}

//======================================================================
// Given an exception code, returns a pointer to a static string with a 
// description of the exception                                         
//======================================================================
LPTSTR DebugExceptionTrap :: GetExceptionString( DWORD dwCode )
{
    #define EXCEPTION( x ) case EXCEPTION_##x: return _T(#x);

    switch ( dwCode )
    {
        EXCEPTION( ACCESS_VIOLATION )
        EXCEPTION( DATATYPE_MISALIGNMENT )
        EXCEPTION( BREAKPOINT )
        EXCEPTION( SINGLE_STEP )
        EXCEPTION( ARRAY_BOUNDS_EXCEEDED )
        EXCEPTION( FLT_DENORMAL_OPERAND )
        EXCEPTION( FLT_DIVIDE_BY_ZERO )
        EXCEPTION( FLT_INEXACT_RESULT )
        EXCEPTION( FLT_INVALID_OPERATION )
        EXCEPTION( FLT_OVERFLOW )
        EXCEPTION( FLT_STACK_CHECK )
        EXCEPTION( FLT_UNDERFLOW )
        EXCEPTION( INT_DIVIDE_BY_ZERO )
        EXCEPTION( INT_OVERFLOW )
        EXCEPTION( PRIV_INSTRUCTION )
        EXCEPTION( IN_PAGE_ERROR )
        EXCEPTION( ILLEGAL_INSTRUCTION )
        EXCEPTION( NONCONTINUABLE_EXCEPTION )
        EXCEPTION( STACK_OVERFLOW )
        EXCEPTION( INVALID_DISPOSITION )
        EXCEPTION( GUARD_PAGE )
        EXCEPTION( INVALID_HANDLE )
    }

    // If not one of the "known" exceptions, try to get the string
    // from NTDLL.DLL's message table.

    static TCHAR szBuffer[512] = { 0 };

    FormatMessage(  FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE,
                    GetModuleHandle( _T("NTDLL.DLL") ),
                    dwCode, 0, szBuffer, sizeof( szBuffer ), 0 );

    return szBuffer;
}

//==============================================================================
// Given a linear address, locates the module, section, and offset containing  
// that address.                                                               
//                                                                             
// Note: the szModule paramater buffer is an output buffer of length specified 
// by the len parameter (in characters!)                                       
//==============================================================================
BOOL DebugExceptionTrap :: GetLogicalAddress(
        PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset )
{
    MEMORY_BASIC_INFORMATION mbi;

    strcpy(szModule,"Unknown module");

    if ( !VirtualQuery( addr, &mbi, sizeof(mbi) ) )
        return FALSE;

    DWORD hMod = (DWORD)mbi.AllocationBase;
    // we need to get read access
    // if not, it is not a module and we will not try to get its name
    // Point to the DOS header in memory
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;

    if (IsBadReadPtr(pDosHdr,sizeof(PIMAGE_DOS_HEADER)))
    {
      offset = (DWORD)addr;
      section = 0;
      return FALSE;
    }

    if ( !GetModuleFileName( (HMODULE)hMod, szModule, len ) )
        return FALSE;
    
    // From the DOS header, find the NT (PE) header
    PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)(hMod + pDosHdr->e_lfanew);
    if (IsBadReadPtr(pNtHdr,sizeof(PIMAGE_NT_HEADERS)))
    {
      offset = (DWORD)addr;
      section = 0;
      return FALSE;
    }


    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION( pNtHdr );

    if (IsBadReadPtr(pSection,sizeof(PIMAGE_NT_HEADERS)*pNtHdr->FileHeader.NumberOfSections))
    {
      offset = (DWORD)addr;
      section = 0;
      return FALSE;
    }

    DWORD rva = (DWORD)addr - hMod; // RVA is offset from module load address

    // Iterate through the section table, looking for the one that encompasses
    // the linear address.
    for (   unsigned i = 0;
            i < pNtHdr->FileHeader.NumberOfSections;
            i++, pSection++ )
    {
        DWORD sectionStart = pSection->VirtualAddress;
        DWORD sectionEnd = sectionStart
                    + std::max(pSection->SizeOfRawData, pSection->Misc.VirtualSize);

        // Is the address in this section???
        if ( (rva >= sectionStart) && (rva <= sectionEnd) )
        {
            // Yes, address is in the section.  Calculate section and offset,
            // and store in the "section" & "offset" params, which were
            // passed by reference.
            section = i+1;
            offset = rva - sectionStart;
            return TRUE;
        }
    }

    return FALSE;   // Should never get here!
}

//============================================================
// Walks the stack, and writes the results to the report file 
//============================================================
void DebugExceptionTrap :: IntelPCPrint( MapFile &map, int pc, bool doEol)
{
  TCHAR szModule[MAX_PATH] = _T("");
  DWORD section = 0, offset = 0;

  GetLogicalAddress((PVOID)pc, szModule,sizeof(szModule),section,offset );

  int nameValue=0;
  const char *name=map.MapNameFromLogical(offset,&nameValue);
  int nameOffset=offset-nameValue;

  
  if( section==1 && nameValue!=0 )
  {
    _tlprintf( _T("%8X %8X %8X + %s"),
              pc, nameValue, nameOffset, name );
  }
  else
  {
    _tlprintf( _T("%08X %04X:%08X       %s"),
               pc, section, offset, szModule );
  }
  if (doEol)
  {
    _tlprintf( _T("\r\n") );
  }
}


// Called may contain 0 when unknown indirect call took place

/*!
\patch_internal 1.01 Date 6/28/2001 by Ondra.
- Fixed: disabled call stack walk on exception in SuperRelease build.
- This should make crashes to behave more consistently.
*/

MapFile GMapFile;

static void KnownFunctionAddress(){}

void DebugExceptionTrap :: IntelStackWalk(
  DWORD eipReg, DWORD *ebpReg, DWORD *pStackTop, DWORD *pStackBot
)
{
#if _ENABLE_REPORT

  if( pStackBot<pStackTop ) pStackBot=pStackTop+64*1024; // in DWORDS


  _tlprintf( _T("\r\nCall stack:\r\n") );
  _tlprintf( _T("\r\nStack %08X %08X\r\n"), pStackTop,pStackBot );

  void *pc = (void *)eipReg;

  MapFile &map = GMapFile;
  if (map.Empty())
  {
    // if map file already exists, reuse it
    map.ParseMapFile();
    
	  // fix offset
	  int mapAddress = map.PhysicalAddress("?KnownFunctionAddress@@YAXXZ");
	  int realAddress = (int)KnownFunctionAddress;
  	
	  int offset = realAddress-mapAddress;
	  if (offset)
	  {
	    LogF("Applying offset %x to mapfile",offset);
	    map.OffsetPhysicalAddress(offset);
	  }
  }

  _tlprintf( _T("mapfile: %s (empty %d)\r\n"), map.GetName(), map.Empty() );

  if( map.Empty() ) return;

  // two pass processing


  enum {MaxCalls = 2048};
  CallInfo callstack[MaxCalls];
  int calls; // actual number of stored calls

  calls = MaxCalls;
  if (ebpReg)
  {
    _tlprintf( _T("\r\nStack frames used\r\n") );
  }
  ExtractCallstack(callstack,calls,pc,ebpReg,pStackTop,pStackBot,true,&map,false);


  //_tlprintf( _T("\r\n------- Begin:: info callstack:\r\n") );
  PrintCalls(callstack,calls,map,pc);
  //_tlprintf( _T("\r\n------- End  :: info callstack:\r\n") );


  if (!ebpReg)
  {
    void *functionStart = map.FunctionStartFromPhysical(pc);
    _tlprintf( _T("\r\n------- Begin:: Optimized callstack:\r\n") );
    OptimizeCalls(functionStart,callstack,calls);
    PrintCalls(callstack,calls,map,pc);
    _tlprintf( _T("\r\n------- End  :: Optimized callstack:\r\n") );
  }
#endif
}

void DebugExceptionTrap :: DeleteCall(CallInfo *callstack, int &calls, int i)
{
  ConstructTraits<CallInfo>::DeleteData(callstack+i,calls-i,1);
 
  calls--;
}

void DebugExceptionTrap ::OptimizeCalls(void *startFunction, CallInfo *callstack, int &calls)
{
  // remove functions that are sure to be skipped
  for (int i=0; i<calls; i++)
  {
    // check if we know where was this place called from
    int calledFrom = -1;
    for (int j=i+1; j<calls; j++)
    {
      // in case of recursion we cannot proceed
      if (callstack[j].fStart==callstack[i].fStart) break;
      // check if this is the call-site
      if (callstack[j].calledAddr==callstack[i].fStart) {calledFrom=j;break;}
    }
    if (calledFrom<0) continue; // we do not know the call site
    // remove anything between i dan calledFrom
    for (int j=i+1; j<calledFrom; )
    {
      DeleteCall(callstack,calls,j);
      calledFrom--; // calledFrom index is moved
    }

  }
  // TODO: bottom-up: first check which calls are perfectly possible 
  // remove calls that are impossible
  for (int i=0; i<calls; i++)
  {
    if (callstack[i].calledAddr==0 || callstack[i].calledAddr==(void *)-1) continue; // indirect call - cannot remove
    bool callPossible = startFunction==callstack[i].calledAddr;
    for (int j=0; j<i; j++)
    {
      if (callstack[j].fStart==callstack[i].calledAddr) {callPossible = true;break;}
    }
    if (callPossible) continue;
    DeleteCall(callstack,calls,i);
    i--;
  }

  // TODO: remove impossible indirect calls
}

void DebugExceptionTrap::PrintCalls( CallInfo *callstack, int &calls, MapFile &map, void *pc )
{
  _tlprintf(
    _T("%8s %8s %19s %26s\r\n"),
    "Address","Logical","Function","Stack size"
  );

    IntelPCPrint(map,(int)pc);
  
  //int minPc=map.MinLogicalAddress();
  //int maxPc=map.MaxLogicalAddress();
  
  int offset=map.MinPhysicalAddress()-map.MinLogicalAddress();

  int functionStart=0;
  const char *name = map.MapNameFromLogical((int)pc-offset,&functionStart);

  (void)name;
  
  void *lastFunctionStart = (void *)functionStart;
  for (int i=0; i<calls; i++)
  {
    void *pc = callstack[i].retAddr;
    void *called = callstack[i].calledAddr;

    int nameValue=0;
    const char *name=map.MapNameFromLogical((int)pc-offset,&nameValue);
    int nameOffset = (int)pc-offset-nameValue;
  
    _tlprintf(
      _T("%8X %8X %8X + %-30s"),
       pc, nameValue, nameOffset, name
     );

    int stackSize = callstack[i].stackFrameSize;
    if (!stackSize)
    {
      _tlprintf( _T("%5s"),"");
    }
    else
    {
      _tlprintf( _T("%5d"),stackSize);
    }

    //IntelPCPrint(map,pc, false);

    // print call information
    
    if (!called)
    {
      _tlprintf( _T("     (Indirect)\r\n") );
    }
    else if (lastFunctionStart!=called)
    {
      _tlprintf( _T("     -> %8X\r\n"), called );
    }
    else
    {
      _tlprintf( _T("\r\n") );
    }
    lastFunctionStart = (void *)(nameValue+offset);

    
  }
}

void DebugExceptionTrap :: IntelStackWalk( PCONTEXT pContext )
{
  #if defined _M_PPC
    // TODOX360: PowerPC stack walk
  #else
    #if STACK_FRAME_POINTERS
      IntelStackWalk(pContext->Eip,(PDWORD)pContext->Ebp,(PDWORD)pContext->Esp,(PDWORD)m_stackBottom);
    #else
      IntelStackWalk(pContext->Eip,NULL,(PDWORD)pContext->Esp,(PDWORD)m_stackBottom);
    #endif
  #endif
}

static DWORD GetCallAddress(unsigned char *pcCode, int abspc)
{
  // note: cs is not the same as ds
  // this may result in some conversions necessary?
  // advance PC validation
  if (pcCode[-5]==0xe8)
  {
    // simple relative call instruction
    LONG relativeOffset = *(LONG *)(pcCode-4);
    DWORD addr = abspc+relativeOffset;
    return addr;
  }
  else
  {
    // check 2..7 B call instruction
    for (int b=-2; b>=-7; b--)
    {
      // check R/M byte
      // bits 5,4,3 of R/M should be 010
      if (pcCode[b]!=0xff) continue;
      if ( ((pcCode[b+1]>>3)&7)!=2) continue;
      return 0;
    }
  }
  return -1;
}

/// checking pointers may lead to longer callstacks, however it is quite slow
#define CHECK_POINTERS 0

/*!
save only those callstack entries that might be relevant (can be valid code pointers)
\param stack [out] callstack result 
\param stackTop stack top (minimum address)
\param stackTop stack bottom (maximum address)
\param validate when true, addresses are validated at the call place
and included only when there is some call found there
\param map when map is provided, calller and callees are paired and impossible calls are omitted.
*/

void DebugExceptionTrap::ExtractCallstack
(
  CallInfo *callstack, int &calls, void *eipReg, DWORD *ebpReg, DWORD *stackTop, DWORD *stackBot,
  bool validate, MapFile *map, bool firstEntryEIP
)
{
  // valid code range? - check process information
  int maxCalls = calls;
  if (maxCalls<1) return;


  DWORD minAbsPc;
  DWORD maxAbsPc;
  if (!map || !map->Parsed() || map->Empty()) map = NULL;
  if (map)
  {
    minAbsPc = map->MinPhysicalAddress();
    maxAbsPc = map->MaxPhysicalAddress();
  }
  else
  {
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)GetModuleHandle(NULL);
    // From the DOS header, find the NT (PE) header
    PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)((char *)pDosHdr + pDosHdr->e_lfanew);
    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION( pNtHdr );
    int codeSize = pSection->SizeOfRawData;
    char *codeData = (char *)pDosHdr+pSection->PointerToRawData;
    minAbsPc = (int)codeData;
    maxAbsPc = minAbsPc+codeSize;
  }

  calls = 0;
  if (firstEntryEIP)
  {
    CallInfo &callInfo = callstack[calls++];
    callInfo.retAddr = eipReg;
    callInfo.calledAddr = 0;
    callInfo.fStart = map ? map->FunctionStartFromPhysical(eipReg) : 0;
  }


  if (ebpReg)
  {
    // get first frame pointer from esp
    if( ebpReg>=stackTop && ebpReg<=stackBot || (int)ebpReg>0x10000 && !IsBadReadPtr(ebpReg,4) )
    {
      for (DWORD *stackframe = ebpReg; stackframe && stackframe<stackBot; )
      {

        if (calls>=maxCalls) break;
        // quickcheck for bad stack
        if ((DWORD)stackframe<0x10000) break;
#if !CHECK_POINTERS
        if (stackframe<stackTop || stackframe>stackBot) break;
#else
        if( (stackframe<stackTop || stackframe>stackBot) && IsBadReadPtr(stackframe,8) ) break;
#endif

        DWORD abspc = stackframe[1];
        stackframe = (DWORD *)(stackframe[0]);

        // Can two DWORDs be read from the supposed frame address?          
#if !CHECK_POINTERS
        if (abspc<minAbsPc || abspc>maxAbsPc) continue;
#else
        if ( (abspc<minAbsPc || abspc>maxAbsPc) && IsBadCodePtr((int (__stdcall*)())abspc) ) continue;
#endif



        // try to verify if we could be called from this address
        // check for known call instructions

        DWORD calledAddress = -1; // -1 -> no call

        // read some bytes from the abspc

        BYTE *pcCode = (BYTE *)abspc;

        if( (abspc>=minAbsPc+7 && abspc<=maxAbsPc) || !IsBadReadPtr(pcCode-7,7) )
        {
          calledAddress = GetCallAddress(pcCode,abspc);
        }

        //DWORD logpc=abspc-offset;
        void *functionStart = NULL;
        if( abspc>=minAbsPc && abspc<=maxAbsPc && map)
        {
          // check start of the function
          functionStart = map->FunctionStartFromPhysical((void *)abspc);
        }
        // store call information
        callstack[calls].retAddr = (void *)abspc; // abspc logical address
        callstack[calls].fStart = functionStart; // abspc function start logical address
        callstack[calls].calledAddr = (void *)calledAddress; // called logical address
        callstack[calls].stackFrameSize = 0;

        calls++;

      }
    }
  }
  else
  {

    for( PDWORD stack=stackTop; stack<stackBot; stack++ )
    {
      if (calls>=maxCalls) break;
      if( map && IsBadReadPtr(stack,4) ) break; // map means used from crash analysis, stackBot may be unreliable
      DWORD abspc = *stack;

      //DWORD logpc=abspc-offset;
      if( abspc<minAbsPc || abspc>=maxAbsPc )
      {
        //LogF("%08x ...",abspc);
        continue;
      }

      // Can two DWORDs be read from the supposed frame address?          
      //if ( IsBadCodePtr((int (__stdcall*)())abspc) ) continue;

      // try to verify if we could be called from this address
      // check for known call instructions

      DWORD calledAddress = -1; // -1 -> no call

      // read some bytes from the abspc

      BYTE *pcCode = (BYTE *)abspc;

      if( abspc-7>=minAbsPc)
      {
        calledAddress = GetCallAddress(pcCode,abspc);
      }

      if (calledAddress!=-1)
      {
        // some call detected

        // check start of the function
        void *functionStart = map ? map->FunctionStartFromPhysical((void *)abspc) : NULL;

        // store call information
        callstack[calls].retAddr = (void *)abspc; // abspc logical address
        callstack[calls].fStart = functionStart; // abspc function start logical address
        callstack[calls].calledAddr = (void *)calledAddress; // called logical address


        if (functionStart)
        {
        // check this function start
        // try to determine stack size for given function based on typical prologue used
        unsigned char *eCode = (unsigned char *)functionStart;
        // check enter code type
        int stackSize = 0;
        if (eCode[0]==0x83 && eCode[1]==0xec)
        {
          // 83 EC aa         sub         esp,aa
          stackSize = *(signed char *)(eCode+2);
        }
        else if (eCode[0]==0x81 && eCode[1]==0xec)
        {
           // 81 EC aa bb cc dd sub         esp,ddccbbaa 
          stackSize = *(int *)(eCode+2);
        }
        else if (eCode[0]==0x55 &&
          eCode[1]==0x8b && eCode[2]==0xec &&
          eCode[3]==0x83 && eCode[4]==0xec
        )
        {
          // 55               push        ebp  
          // 8B EC            mov         ebp,esp 
          // 83 EC aa         sub         esp,aa 
          stackSize = *(signed char *)(eCode+5)+4;
        }
        else if (eCode[0]==0x55 &&
          eCode[1]==0x8b && eCode[2]==0xec &&
          eCode[3]==0x81 && eCode[4]==0xec
        )
        {
          // 55               push        ebp  
          // 8B EC            mov         ebp,esp 
            // 81 EC aa bb cc dd sub         esp,ddccbbaa
          stackSize = *(int *)(eCode+5)+4;
        }

        // align stack skip
        if (stackSize&3)
        {
          LogF("Stack not aligned - %x",stackSize);
          stackSize &= ~3;
        }

        //stack += stackSize/4;
        //const char *name = map->MapNameFromPhysical(abspc);
        //LogF("%08x %30s, skip stack %d",abspc,name,stackSize);

        callstack[calls].stackFrameSize = stackSize;
        }
        else
        {
          //const char *name = map->MapNameFromPhysical(abspc);
          //LogF("%08x %30s",abspc,name);

        callstack[calls].stackFrameSize = 0;

        }

        calls++;
      }
      else
      {
        //LogF("%08x ???",abspc);
      }


    }
  }
}
/*
Function prologue may look like:

00408FA0 83 EC 54         sub         esp,54h 
00408FA3 53               push        ebx  
00408FA4 56               push        esi  
00408FA5 57               push        edi  

00408FD0 81 EC D0 00 00 00 sub         esp,0D0h 
00408FD6 53               push        ebx  
00408FD7 56               push        esi  
00408FD8 57               push        edi  

00409000 55               push        ebp  
00409001 8B EC            mov         ebp,esp 
00409003 83 EC 78         sub         esp,78h 

00409000 55               push        ebp  
00409001 8B EC            mov         ebp,esp 
00409003 81 EC D0 00 00 00 sub         esp,0D0h 

it could be possible to start with enter as well, but it is very unlikely
*/

//! extract callstack information from current context
void DebugExceptionTrap::ExtractCallstack(void **callstack, int &calls, bool validate, MapFile *map)
{
  int maxCalls = calls;

  DWORD espReg,ebpReg;
  DWORD eipReg = (DWORD)GetEIP(espReg,ebpReg);

  NextFrame(ebpReg,eipReg);

  static CallInfo callstackFull[256];

  int fullCalls = 256;

  ExtractCallstack(callstackFull,fullCalls,(void *)eipReg, (DWORD *)ebpReg,(DWORD *)espReg,(DWORD *)m_stackBottom,validate,map);

  if (!ebpReg && map && map->Parsed() && !map->Empty())
  {
    int oCalls = fullCalls-1;
    OptimizeCalls(callstackFull[0].retAddr,callstackFull+1,oCalls);
    oCalls++;

    if (oCalls>maxCalls) oCalls = maxCalls;
    for (int i=0; i<oCalls; i++)
    {
      callstack[i] = callstackFull[i].retAddr;
    }
    calls = oCalls;
  }
  else
  {
    if (fullCalls>maxCalls) fullCalls = maxCalls;
    for (int i=0; i<fullCalls; i++)
    {
      callstack[i] = callstackFull[i].retAddr;
    }
    calls = fullCalls;
  }

}

void DebugExceptionTrap::ExtractCallstack(CallInfo *callstack, int &calls, bool validate, MapFile *map)
{
  // get eip, esp
  DWORD esp, ebp;
  DWORD eip = GetEIP(esp,ebp);

  NextFrame(ebp,eip);

  ExtractCallstack(callstack,calls,(void *)eip,(DWORD *)ebp,(DWORD *)esp,(DWORD *)m_stackBottom,validate,map);
}

/*!
\patch 1.04 Date 7/14/2001 by Ondra.
- Improved: Crash info context.bin contains more information.
- CPU registers saved
- version info saved
\patch 1.12 Date 8/8/2001 by Ondra.
- Improved: context.bin contains more info about process memory.
\patch_internal 1.28 Date 10/18/2001 by Ondra.
- Improved: context.bin contains information about configuration (Server, Czech)
*/

extern "C" void FunctionWithKnownAddress();

void FunctionWithKnownAddress()
{
}

void DebugExceptionTrap :: IntelStackSave( PCONTEXT pContext, char *fileExt )
{
  #if defined _M_PPC  
    // TODOX360: PowerPC stack walk
  #else
  //DWORD eip = pContext->Eip;
  DWORD *pStackTop = (DWORD *)pContext->Esp;
  DWORD *pStackBot = (DWORD *)m_stackBottom;

  if( pStackBot<pStackTop )
  {
    pStackBot=pStackTop+4;
    while (pStackBot < pStackTop+4*1024) // at most 4*1024 in DWORDS for stack
    {
      // verify determined stack bottom is readable
      if (IsBadReadPtr(pStackBot,4)) 
      { // and get last readable
        pStackBot -= 4;
        break;
      }
      pStackBot += 4;
    }
  }

  // dump stack to external file
  char crashName[512];
  strcpy(crashName,m_szLogFileName);
  if (fileExt)
  {
    strcpy(GetFileExt(crashName),fileExt);
    strcat(crashName, ".bidmp");
  }
  else 
    strcpy(GetFileExt(crashName),".bidmp");
  int stackF = open(crashName,_O_CREAT|_O_TRUNC|_O_BINARY|_O_WRONLY,_S_IREAD|_S_IWRITE);
  if (stackF>=0)
  {
    // write EIP
    static const char magic[]="STK7";
    static int stkVersion = 103;
    // save context
    write(stackF,magic,sizeof(magic));
    write(stackF,&stkVersion,sizeof(stkVersion));
    int ver = /*APP_VERSION_NUM*/ CurrentAppInfoFunctions->GetVersionNumber();
    write(stackF,&ver,sizeof(ver));
    int build = CurrentAppInfoFunctions->GetBuildNumber();
    write(stackF,&build,sizeof(build));
    int contextSize = sizeof(*pContext);
    write(stackF,&contextSize,sizeof(contextSize));
    write(stackF,pContext,sizeof(*pContext));
    // save some info about exe file
    char exePath[1024];
    char sourceName[1024];
    strcpy(sourceName,"");
    strcpy(exePath,"");
    
      GetModuleFileName(NULL,exePath,sizeof(exePath));
      DWORD size = sizeof(sourceName);
      GetComputerName(sourceName,&size);
    struct stat st;
    int ok = stat(exePath,&st);
    int exeSize = ok>=0 ? st.st_size : 0;
    // get short filename
    const char *shortName = GetFilenameExt(exePath);
    // store the filename
    int len = strlen(shortName)+1;
    write(stackF,&len,sizeof(len));
    write(stackF,shortName,len);

    int srcLen = strlen(sourceName)+1;
    write(stackF,&srcLen,sizeof(srcLen));
    write(stackF,sourceName,srcLen);

    write(stackF,&exeSize,sizeof(exeSize));
    char mapExt[3];
    mapExt[0] = 0;
    #if _FORCE_DS_CONTEXT
      strcpy(mapExt,"DS");
    #endif
    write(stackF,mapExt,2);
    // save CRC info about process memory
    const int pagesize = 4*1024;
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)GetModuleHandle(NULL);
    // From the DOS header, find the NT (PE) header
    PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)((char *)pDosHdr + pDosHdr->e_lfanew);
    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION( pNtHdr );
    // save crc of each 4KB page
    int codeSize = pSection->SizeOfRawData;
    char *codeData = (char *)pDosHdr+pSection->PointerToRawData;
    
    write(stackF,&codeData,sizeof(codeData));
    write(stackF,&codeSize,sizeof(codeSize));
    // save some known symbol value
    void *knownFunction = FunctionWithKnownAddress;
    write(stackF,&knownFunction,sizeof(knownFunction));

    CRCCalculator crc;
    while (codeSize>=pagesize)
    {
      unsigned int crcResult = crc.CRC(codeData,pagesize);
      write(stackF,&crcResult,sizeof(crcResult));
      codeSize -= pagesize;
      codeData += pagesize;
    }
    
    // save stack bottom
    write(stackF,&pStackBot,sizeof(pStackBot));
    int stackSize = (int)pStackBot-(int)pStackTop;
    // save stack size - verification stack bottom is valid
    write(stackF,&stackSize,sizeof(stackSize));
    write(stackF,pStackTop,stackSize);
    close(stackF);
  }
  #endif
}

//============================================================
// Walks the stack, and writes the results to the report file 
//============================================================

//============================================================================
// Helper function that writes to the report file, and allows the user to use 
// printf style formating                                                     
//============================================================================
int __cdecl DebugExceptionTrap :: _tprintf(const TCHAR * format, ...)
{
  int retValue=0;
  if (m_hReportFile)
  {
    TCHAR szBuff[1024]; // wvsprintf is used, so it cannot be higher (MSDN: The maximum size of the buffer is 1024 bytes.)
    DWORD cbWritten;
    va_list argptr;
          
    va_start( argptr, format );
    retValue = vsnprintf( szBuff, sizeof(szBuff)-1, format, argptr ); //wvsprintf( szBuff, format, argptr );
    va_end( argptr );

    if (retValue<0)
    {
      retValue = 1023;
      szBuff[retValue]=0;
    }
    WriteFile( m_hReportFile, szBuff, retValue * sizeof(TCHAR), &cbWritten, 0 );

    if (m_countFlushLines++>=100)
    {  
      FlushLogFile();
    }
  }
  return retValue;
}


int __cdecl DebugExceptionTrap :: _tlprintf(const TCHAR * format, ...)
{
  TCHAR szBuff[1024];
  int retValue=0;
  if (m_hReportFile)
  {
    DWORD cbWritten;
    va_list argptr;
    va_start( argptr, format );
    retValue = wvsprintf( szBuff, format, argptr );
    va_end( argptr );

    WriteFile( m_hReportFile, szBuff, retValue * sizeof(TCHAR), &cbWritten, 0 );

    if (m_countFlushLines++>=100)
    {  
      FlushLogFile();
    }
  }
  else
  {
    va_list argptr;
    va_start( argptr, format );
    retValue = wvsprintf( szBuff, format, argptr );
    va_end( argptr );
  }
  bool eol = false;
  while (*szBuff)
  {
    char *lastchar = szBuff+strlen(szBuff)-1;
    if (*lastchar=='\n' || *lastchar=='\r') *lastchar=0,eol = true;
    else break;
  }
  if (eol) strcat(szBuff,"\n");
  OutputDebugString(szBuff);
  return retValue;
}


void DebugExceptionTrap :: EnsureInitialized()
{ 
  m_header=false;
  m_countFlushLines = 0;
}

#endif

#ifdef _MSC_VER
#  pragma once
#endif

/**
  @file   platform.hpp
  @brief  Platform-specific definitions/declarations.
    
  Copyright &copy; 2002-2003 by BIStudio (www.bistudio.com)
  @author PE
  @since  4.6.2002
  @date   16.10.2003
*/

#ifndef _PLATFORM_HPP
#define _PLATFORM_HPP

// portability settings for different compilers/platforms

#ifdef _MSC_VER

  #define CCALL __cdecl

	#ifdef _XBOX
	  // no need to perform stack probes on X360
	  #pragma check_stack(off)
	#endif
  // make for variable local
  #ifndef NONSTANDARD_FOR_SCOPE
    #if _MSC_VER>=1300
      #pragma conform(forScope,on)
      #pragma warning(disable:4258)
      #if _MSC_VER>=1400
        // disable bogus warning:
        // loop control variable ... is used outside the for-loop scope
        // with conformant for scope rules such thing can never happen
        #pragma warning(disable:4288)
      #endif
    #else
      #define for if( false ) {} else for
    #endif
  #endif

  #define NO_ENUM_FORWARD_DECLARATION 0
  #define NO_UNDEF_ENUM_REFERENCE     0

  #define ENUM_CAST(Type,value) value

  #if _MSC_VER>=1400
    // we want to use standard function even if they are considered insecure by MS
    #define _CRT_SECURE_NO_DEPRECATE 1
    // we want to use POSIX names
    #define _CRT_NONSTDC_NO_DEPRECATE 1
    // we want to mark functions we know return restricted pointers (like malloc) 
    #define RESTRICT_RETURN __declspec(restrict)
  #else
    #define RESTRICT_RETURN
  #endif
#else

  #define CCALL 
  #define __int64 long long

  #define NO_ENUM_FORWARD_DECLARATION 1
  #define NO_UNDEF_ENUM_REFERENCE     1
  
  #define ENUM_CAST(Type,value) Type(value)

  #define StrToInt(x)  (*(int*)x)

  #define __forceinline inline

  #define RESTRICT_RETURN

#endif

#ifdef _WIN32

#define snprintf  _snprintf
#if _MSC_VER < 13
#define vsnprintf _vsnprintf
#endif
#define strDup    _strdup
#define getpid    _getpid

typedef int socklen_t;
#define DllExport   __declspec( dllexport )

// POSIX_FILES_COMMON servers for both Linux and Win32 POSIX API usage
#define POSIX_FILES_COMMON 0
#if POSIX_FILES_COMMON
#define MAX_PATH          260
//#define FILE_BEGIN   SEEK_SET
//#define FILE_CURRENT SEEK_CUR
//#define FILE_END     SEEK_END
//#define GetCurrentDirectory(len,buf)    getcwd(buf,len)
//#define SetCurrentDirectory             chdir
//#define WriteFile(file,buf,len,wr,ovl)  (*(wr)=write((int)file,buf,len),*(wr)!=0xffffffff)
//#define CloseHandle(handle)             ::close((int)handle)

#define ReadFilePosix(file,buf,len,rd,ovl)   (*(rd)=read((int)file,buf,len),*(rd)!=0xffffffff)
#define SetFilePointerPosix(file,pos,x,mode) lseek((int)file,pos,mode)

const int MaxFileName  = 2048;
#ifdef _WIN32
  #define S_IWUSR S_IWRITE
  #define LocalPath(fn,fileName) \
    char fn[MaxFileName]; \
    strncpy(fn,fileName,MaxFileName); \
    fn[MaxFileName-1] = (char)0;
#else
  #define LocalPath(fn,fileName) \
    char fn[MaxFileName]; \
    strncpy(fn,fileName,MaxFileName); \
    fn[MaxFileName-1] = (char)0; \
    unixPath(fn)
#endif

#endif

#else

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#define _REENTRANT

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <typeinfo>
#include <wchar.h>

#define USE_MALLOC 1

typedef int HANDLE;
#define INVALID_HANDLE_VALUE -1
typedef int HRESULT;
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_INVALID_NAME 123L
#define ERROR_SUCCESS 0L
#define INFINITE 0xFFFFFFFF
typedef int HWND;
typedef int HINSTANCE;
typedef char* LPSTR;
typedef size_t SIZE_T;
typedef wchar_t WCHAR;

// POSIX_FILES_COMMON servers for both Linux and Win32 POSIX API usage
#define POSIX_FILES_COMMON 1

#define __cdecl
#define WINAPI
#define DllExport

#ifndef max
  #undef  min
  #define min(a,b) (((a)<(b))?(a):(b))
  #define max(a,b) (((a)>(b))?(a):(b))
#endif

#define _finite(x) finite(x)
#define _isnan(x)  isnan(x)
extern char *strDup ( const char *src );

#define OutputDebugString(s) {char dbg[1024]="Debug: ";strcat(dbg,s);LogF(dbg);}

extern char *CCALL strlwr( char *a );
extern char *CCALL strupr( char *a );
extern int CCALL strcmpi( const char *a, const char *b );
extern int CCALL stricmp( const char *a, const char *b );
extern int CCALL strnicmp( const char *a, const char *b, int n );

#define ReadFilePosix(file,buf,len,rd,ovl)   (*(rd)=read((int)file,buf,len),*(rd)!=0xffffffff)
#define SetFilePointerPosix(file,pos,x,mode) lseek((int)file,pos,mode)

const int MaxFileName  = 2048;
#define LocalPath(fn,fileName) \
    char fn[MaxFileName]; \
    strncpy(fn,fileName,MaxFileName); \
    fn[MaxFileName-1] = (char)0; \
    unixPath(fn)
    
#ifndef CP_UTF8
#define CP_ACP                    0           // default to ANSI code page
#define CP_UTF8                   65001       // UTF-8 translation
#endif

#define MAX_PATH          260
#define FILE_BEGIN   SEEK_SET
#define FILE_CURRENT SEEK_CUR
#define FILE_END     SEEK_END
#define GetCurrentDirectory(len,buf)    getcwd(buf,len)
#define SetCurrentDirectory             chdir
#define ReadFile(file,buf,len,rd,ovl)   (*(rd)=read((int)file,buf,len),*(rd)!=0xffffffff)
#define WriteFile(file,buf,len,wr,ovl)  (*(wr)=write((int)file,buf,len),*(wr)!=0xffffffff)
#define SetFilePointer(file,pos,x,mode) lseek((int)file,pos,mode)
#define CloseHandle(handle)             ::close((int)handle)
#define CreateDirectory(dir,d)          createDirectory(dir)
extern void createDirectory ( const char *dir );
#define NEW_DIRECTORY_MODE              (S_IREAD|S_IWRITE|S_IEXEC|S_IXGRP|S_IXOTH)
#define DeleteFile(fn)                  deleteFile(fn)
extern void deleteFile ( const char *path );
extern void unixPath ( char *path );
extern bool isSuffix ( const char *str, const char *suffix );
#define ioctlsocket ioctl

extern void sleepMs ( unsigned ms );
extern void Sleep ( unsigned ms );

extern unsigned long long getSystemTime();
inline unsigned long long GetTickCount ()
{
    return( getSystemTime()/1000 );
}

#ifndef _FILETIME_
#define _FILETIME_
typedef struct _FILETIME {
    unsigned long dwLowDateTime;
    unsigned long dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
#endif

#define _atoi64(x) atoi64(x)
extern long long atoi64 ( const char *str );
#define _i64toa(i,buf,radix) i64toa(i,buf,radix)
extern char* i64toa ( long long i, char *buf, int radix );
extern bool fileTime ( const char *fileName, long long win32Time );
#define CopyFile(src,dst,d) fileCopy(src,dst)
extern bool fileCopy ( const char *src, const char *dest );
extern long filelength(int fd);

#define __iscsym(_c)    (isalnum(_c) || ((_c) == '_'))
#define __iscsymf(_c)   (isalpha(_c) || ((_c) == '_'))

#endif

#ifdef __GNUC__

#define INIT_PRIORITY_NORMAL __attribute__ ((init_priority(10000)))
#define INIT_PRIORITY_HIGH   __attribute__ ((init_priority(9000)))
#define INIT_PRIORITY_URGENT __attribute__ ((init_priority(8000)))
#define INIT_PRIORITY(prio)  __attribute__ ((init_priority(prio)))

#define PACKED               __attribute__ ((packed))

#else

#define INIT_PRIORITY_NORMAL
#define INIT_PRIORITY_HIGH
#define INIT_PRIORITY_URGENT
#define INIT_PRIORITY(prio)

#define PACKED

#endif

/* __BEGIN_DECLS should be used at the beginning of your declarations,
   so that C++ compilers don't mangle their names.  Use __END_DECLS at
   the end of C declarations.
*/
#undef __BEGIN_DECLS
#undef __END_DECLS
#ifdef __cplusplus
#  define __BEGIN_DECLS extern "C" {
#  define __END_DECLS }
#else
#  define __BEGIN_DECLS /* empty */
#  define __END_DECLS /* empty */
#endif

#if _GAMES_FOR_WINDOWS || (defined _XBOX && _XBOX_VER >= 200)
#define _ENABLE_VON 1
#define VOICE_OVER_NET 1
#elif defined _WIN32
#define _ENABLE_VON 0
#define VOICE_OVER_NET 0
#else
#define _ENABLE_VON 0
#define VOICE_OVER_NET 1
#endif

#endif


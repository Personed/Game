#ifdef _MSC_VER
#pragma once
#endif

#ifndef _SIMUL_WEATHER_PRECOMP_MAIN_H
#define _SIMUL_WEATHER_PRECOMP_MAIN_H

#include <crtdefs.h>
//==================================================
// platform specific
//==================================================


#ifdef _MSC_VER

#define CCALL __cdecl

#if _MSC_VER>=1400
// we want to use standard function even if they are considered insecure by MS
#define _CRT_SECURE_NO_DEPRECATE 1
// we want to use POSIX names
#define _CRT_NONSTDC_NO_DEPRECATE 1
// we want to mark functions we know return restricted pointers (like malloc) 
#define RESTRICT_RETURN
#else
#define RESTRICT_RETURN
#endif
#else

#define CCALL 
#define __int64 long long
#define __forceinline inline
#define RESTRICT_RETURN

#endif

//==================================================
// memory allocation
//==================================================
#define _MFC_OVERRIDES_NEW
#include "memAllocator.h"

inline void * CCALL operator new (size_t size, intptr_t addr, char const *file, int line)
{ // placement new
  size; file; line;
  return (void *)addr;
}
#ifdef _CPPUNWIND
inline void CCALL operator delete (void *mem, intptr_t addr, char const *file, int line)
{ // placement delete
  mem; addr; file; line;
}
#endif

// note: global array versions ([]) are ignored in VC5.0

inline RESTRICT_RETURN void * CCALL operator new ( size_t size ) {return simul::base::BaseMemoryAllocator::GetMemoryAllocator()->New(size);}
inline RESTRICT_RETURN void * CCALL operator new[] ( size_t size ) {return simul::base::BaseMemoryAllocator::GetMemoryAllocator()->New(size);}

inline RESTRICT_RETURN void * CCALL operator new ( size_t size, const char *file, int line )
{
  return simul::base::BaseMemoryAllocator::GetMemoryAllocator()->New(size,file,line);
}
inline RESTRICT_RETURN void * CCALL operator new[] ( size_t size, const char *file, int line )
{
  return simul::base::BaseMemoryAllocator::GetMemoryAllocator()->New(size,file,line);
}
inline void CCALL operator delete ( void *ptr )
{
  simul::base::BaseMemoryAllocator::GetMemoryAllocator()->Delete(ptr);
}
inline void CCALL operator delete[] ( void *ptr )
{
  simul::base::BaseMemoryAllocator::GetMemoryAllocator()->Delete(ptr);
}

#ifdef _CPPUNWIND
inline void CCALL operator delete ( void *ptr, const char *file, int line )
{
  simul::base::BaseMemoryAllocator::GetMemoryAllocator()->Delete(ptr,file,line);
}
inline void CCALL operator delete[] ( void *ptr, const char *file, int line )
{
  simul::base::BaseMemoryAllocator::GetMemoryAllocator()->Delete(ptr,file,line);
}
#endif


#endif //_SIMUL_WEATHER_BASE_PRECOMP_H

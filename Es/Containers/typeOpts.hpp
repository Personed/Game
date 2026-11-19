#ifdef _MSC_VER
#pragma once
#endif

#ifndef _TYPE_OPTS_HPP
#define _TYPE_OPTS_HPP


// default general implemetation in common
// to both define/template approach to overloading
#include <Es/Memory/normalNew.hpp>

#if !__WATCOMC__ || !defined WNew // check for Optima++ declarations

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
#ifdef _WIN32
	static inline void *__cdecl operator new (size_t size, void *addr)
	{
		return addr;
	}
	// delete required for unwinding exceptions
	static inline void __cdecl operator delete (void *mem, void *addr)
	{
	}
#else
/*
        //No special placement new on Linux servers (use std instead)
        inline void * operator new (size_t size, void *addr)
        {
                return addr;
        }
        // delete required for unwinding exceptions
        inline void operator delete (void *mem, void *addr)
        {
        }
*/
#endif
#endif

#endif

#include <Es/platform.hpp>

// constructor variants
template <class Type>
__forceinline void ConstructAt( Type &dst ) {new(&dst) Type;}

template <class Type>
__forceinline void ConstructAt( Type &dst, const Type &src ) {new(&dst) Type(src);}

#include <Es/Memory/debugNew.hpp>

#include "typeDefines.hpp"

#include <string.h>

TypeIsSimple(bool);
TypeIsSimple(char);
TypeIsSimple(signed char);
TypeIsSimple(unsigned char);
TypeIsSimple(short);
TypeIsSimple(unsigned short);
TypeIsSimple(int);
TypeIsSimple(unsigned int);
TypeIsSimple(long);
TypeIsSimple(unsigned long);
TypeIsSimple(float);
TypeIsSimple(double);
TypeIsSimple(__int64);
TypeIsSimple(unsigned __int64);

#if !defined _MSC_VER || defined _NATIVE_WCHAR_T_DEFINED
TypeIsSimple(wchar_t);
#endif

#endif


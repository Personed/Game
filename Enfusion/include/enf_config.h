//-----------------------------------------------------------------------------
// File: enf_config.h
//
// Desc: 
//
// Copyright (c) 2000-2005 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_CONFIG_H
#define ENF_CONFIG_H

namespace enf
{
  /*
#ifdef WINVER
	#undef WINVER
#endif

#ifdef _WIN32_WINNT
	#undef _WIN32_WINNT
#endif

#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
*/
#define _MFC_VER
#define ENF_VERSION_MAJOR	0
#define ENF_VERSION_MINOR	1
#define ENF_VERSION_STRING "0.1"//#ENF_VERSION_MAJOR ## "." ## #ENF_VERSION_MINOR
#define ENF_VERSION_ID	((ENF_VERSION_MAJOR << 16) | (ENF_VERSION_MINOR))

#define ENF_STATIC //until whole engine won't be DLL

/*
until whole engine will be really separated engine ;-)
*/
#define ENF_BUILD


#ifdef ENF_DEVELOPER
	#define ENF_ENABLE_DIAG
#endif

const unsigned int MAX_FILENAME_LENGTH	= 256;

#ifdef _MSC_VER
#pragma warning( disable : 4100 ) //warning C4100: 'world' : unreferenced formal parameter
#pragma warning( disable : 4127 ) //warning C4127: conditional expression is constant
#pragma warning( disable : 4131 ) //warning C4131: '_tr_align' : uses old-style declarator
#pragma warning( disable : 4201 ) //warning C4201: nonstandard extension used : nameless struct/union
#pragma warning (disable : 4251)	// disable no DLL interface
#pragma warning (disable : 4244)	// disable conversion
//#pragma warning (disable : 4624)	// private destructor
#pragma warning (disable : 4312)	// conversion from '' to '' of greater size
#endif

#if (_MSC_VER >= 1400) || __GCC__ || __SNC__
#define ENF_RESTRICT __restrict
#else
#define ENF_RESTRICT
#endif

//Consoles are clear switch configuration
#ifdef ENF_XENON
	#undef ENF_INTEL
	#undef ENF_SSE
	#undef ENF_STATIC
	#define ENF_STATIC
	#define D3DCOMPILE_USEVOIDS
	#define ENF_CONSOLE
	#define ENF_POWERPC
	#define ENF_RENDERQUADS
	#ifndef XM_NO_OPERATOR_OVERLOADS
		#define XM_NO_OPERATOR_OVERLOADS
	#endif
#endif

#ifdef SN_TARGET_PS3
	#undef ENF_INTEL
	#undef ENF_SSE
	#undef ENF_STATIC
	#define ENF_STATIC
	#define ENF_PS3
#ifdef ENF_DEVELOPER
	#define __CELL_ASSERT__
#endif
	#define ENF_CONSOLE
	#define ENF_POWERPC
	#define ENF_RENDERQUADS
	//#define ENF_FORCE_SYNC
#endif

#ifdef __MACOSX__
	#define ENF_OGL
	#define ENF_SSE
	#define __GCC__
	#define ENF_STATIC
#endif

#if !defined(ENF_CONSOLE) || defined(ENF_DEVELOPER)
	#define ENF_SHADER_COMPILER
#endif

#ifdef WIN32
	//#define ENF_OGL
#if !defined(ENF_DLL_EXPORT) && !defined(ENF_STATIC) 
#ifndef ENF_DLL
	#define ENF_DLL
#endif
#endif
#ifndef ENF_OGL
	//#define ENF_STEREO_VISION
#endif
	#define ENF_SSE
#endif

#if defined(ENF_DEVELOPER) && defined(WIN32)
	#define ENF_WB
#endif

//define the endianess of the integers
#if defined(ENF_POWERPC)
	#define ENF_INTEGER_BIG_ENDIAN 1
#else
	#define ENF_INTEGER_BIG_ENDIAN 0
#endif

//define the endianess of floats (not necessarily the same as integers)
#if defined(ENF_POWERPC)
	#define ENF_FLOAT_BIG_ENDIAN 1
#else
	#define ENF_FLOAT_BIG_ENDIAN 0
#endif


#if defined(ENF_PS3)
	#define ENF_HWINSTANCING
#endif


#if defined(ENF_OGL) && !defined(ENF_PS3)
	#define GLEW_STATIC
#endif


#define PUBLIC_API
#define FORCE_PUBLIC_API __declspec(dllexport)

#if !defined(ENF_STATIC)
	#ifdef ENF_DLL
		#undef PUBLIC_API
		#define PUBLIC_API __declspec( dllimport )
	#endif

	#ifdef ENF_DLL_EXPORT
		#undef PUBLIC_API
		#define PUBLIC_API __declspec( dllexport )
	#endif
#endif

#ifndef NULL
	#define NULL 0L
#endif

#ifdef __SNC__
	#define __GCC__
#endif

#ifdef __GCC__
	#ifdef __APPLE__
		#define ENF_STDCALL __attribute__((stdcall)) 
	#else
		#define ENF_STDCALL
	#endif
	#define ENF_FASTCALL __fastcall 
	#define ENF_CDECL 
	#define ENF_INLINE inline
	#define ENF_INLINEFUNC inline
	#define ENF_DECL_THREAD	__thread
#else
	#define ENF_STDCALL __stdcall
	#define ENF_FASTCALL __fastcall 
	#define ENF_CDECL __cdecl
	#define ENF_INLINE __forceinline
	#define ENF_INLINEFUNC __forceinline
	#define ENF_DECL_THREAD _declspec(thread)
#endif


//native method declaration
#define NMETHOD ENF_STDCALL

/*!
dummy render needed in some configuration to be DayZ compatible
*/
#if defined(_DISABLE_GUI) || defined(_SERVER) || defined(_BINARIZE)
	#define ENF_DUMMYRENDER
#endif


enum ErrorResponse
{
	ER_ABORT,
	ER_RETRY,
	ER_IGNORE
};

PUBLIC_API ErrorResponse _ShowError(const char* title, const char* intro, const char* error, const char* file, unsigned line);
PUBLIC_API ErrorResponse _ShowError(const char* title, const char* intro, const char* error, void* ebp, void* eip);

PUBLIC_API void _enf_assert(const char *, const char *, unsigned);

#ifdef  NDEBUG
	#define enf_assert(exp) ((void)0)
	#define enf_assert2(exp, desc) ((void)0)
#else
	#define enf_assert(exp) ( (exp) || (enf::_enf_assert(#exp, __FILE__, __LINE__), 0) )
	#define enf_assert2(exp, desc) ( (exp) || (enf::_enf_assert(desc, __FILE__, __LINE__), 0) )
#endif  /* NDEBUG */

#ifdef ENF_DEVELOPER
	#define enf_rlsassert(exp) (void)( (exp) || (enf::_enf_assert(#exp, __FILE__, __LINE__), 0) )
	#define enf_rlsassert2(exp, desc) (void)( (exp) || (enf::_enf_assert(desc, __FILE__, __LINE__), 0) )
#else
	#define enf_rlsassert(exp) ((void)0)
	#define enf_rlsassert2(exp, desc) ((void)0)
#endif

#define _CRT_SECURE_NO_DEPRECATE 1
const unsigned int ENF_CLEANUP_FLUSHLEVEL = 100;

#if !defined(ENF_XENON) && !defined(ENF_PS3)
	#define ENF_LOD_CROSSFADE
#endif

}


#endif

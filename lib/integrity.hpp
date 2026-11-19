/*!
\file
File integrity check
Calculate checksum and if it does not match, do something clever.
*/

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _INTEGRITY_HPP
#define _INTEGRITY_HPP

#if _SUPER_RELEASE && defined _WIN32

# define PROTECTION_ENABLED 1

# if !_DISABLE_CRC_PROTECTION
#   define ENABLE_PROTECTION_CRC 1   // enable CRC protection
# endif

# define ALWAYS_FAIL_CRC 0   // simulate crack of the area protected by CRC
# define ALWAYS_FAIL_SUM 0   // simulate crack of the area protected by checksum
# define ALWAYS_FAIL_CDKEY 0 // simulate invalid CD key
# define CHECK_FADE 0

#else

# define PROTECTION_ENABLED 0

# define ENABLE_PROTECTION_CRC 0   // enable CRC protection
# define ALWAYS_FAIL_CRC 0   // simulate crack of the area protected by CRC
# define ALWAYS_FAIL_SUM 0   // simulate crack of the area protected by checksum
# define ALWAYS_FAIL_CDKEY 0 // simulate invalid CD key
# define CHECK_FADE 0

#endif

#if PROTECTION_ENABLED

#define SUM_BASE_MAGIC 'SBAS'
#define SUM_SIZE_MAGIC 'SSIZ'

#define CRC_BASE_MAGIC 'CBAS'
#define CRC_SIZE_MAGIC 'CSIZ'

#define SUM_ADJUST_MAGIC 'FSUM'
#define CRC_VALUE_MAGIC  'FCRC'

#define ALLOCATOR_MAGIC  'FALC'


// description:

// two regions are protected, one with checksum (SUM_BASE_MAGIC, SUM_SIZE_MAGIC)
// second with CRC (CRC_BASE_MAGIC, CRC_SIZE_MAGIC)

// region protected with CRC is used to determine if FADE should activate
// region protected with checksum is used to detetmine if Fade is cracked or removed

// from experience we know that some references to DLL make function protection impossible
// extreme care has to be taken when editing function in this region, as they should not contain
// any Win32 or other DLL references
// DLL reference is usually compiled as indirect call
// corresponding intel byte hexcodes are FF 15 followed by DWORD address

/*! Calculate checksum of memory region
If checksum is not zero, dammage random part of memory
*/

__forceinline void CheckGenuine()
{
  // TODO: make Damage_FastAlloc doing something clever
  #if 0
	// import necessary memory information functions
	/*
	extern char *MemoryBase();
	extern int MemoryUsed();

	void *membeg = MemoryBase();
	int memsize = MemoryUsed()
	*/
	//FastCAlloc *a = &TrackLLinks::_allocatorTrackLLinks;

	#define MEASURE_PERFORMANCE 0
	#if CHECK_FADE
	int csum;
	int sumbase,sumsize;
	#endif
	#if MEASURE_PERFORMANCE
	static int time;
	#endif
	__asm
	{
		#if MEASURE_PERFORMANCE
		rdtsc
		mov time,eax
		#endif

		// performance estimations:
		// CPU: 
		// 4 cycles per loop (PII), 4 B per loop, 1 MB ~~ 1 M cycles
		// PII 500 MHz: 2MB ~~ 2 ms
		// memory bandwidth:
		// 32b (4B), 133 MHz -> cca 512 MB/s
		// 2MB ~~ 4 ms
		// estimation 5 ms
		// measured cca 10 ms
		// use additon here to avoid being searched by constants
		// note: signing application should replace 

		mov edx,SUM_BASE_MAGIC
		mov ecx,SUM_SIZE_MAGIC

		add edx,SUM_BASE_MAGIC
		add ecx,SUM_SIZE_MAGIC

		#if CHECK_FADE
		mov sumbase,edx
		mov sumsize,ecx
		#endif

		xor esi,esi
		CheckLoop:
			mov eax,[edx]
			add edx,4
			rol eax,cl // note: only 5 bits are used in rotation
			add esi,eax
			dec ecx
		jnz CheckLoop


		#if MEASURE_PERFORMANCE
		rdtsc
		sub eax,time
		mov time,eax
		#endif

		#if CHECK_FADE
			mov csum,esi
		#endif
		#if !CHECK_FADE || ALWAYS_FAIL_SUM
			#if !ALWAYS_FAIL_SUM
			test esi,esi
			je SkipHit
			#endif
				// load address of the allocator we want to dammage
				// CRC application will replace following code
				// with some kind of address calculation
				mov eax, ALLOCATOR_MAGIC
				add eax, ALLOCATOR_MAGIC
				push eax
				call Damage_FastAlloc

			SkipHit:;
		#endif
	}

	#if CHECK_FADE
		if (csum)
		{
			FILE *f = fopen("bin\\fade.txt","a");
			if (f)
			{
				fprintf
				(
					f,"FADE csum %x (range %x..%x)\n",
					csum,sumbase,sumbase+sumsize*4
				);
				fclose(f);
			}

		}
		FILE *b = fopen("bin\\sum.bin","wb");
		if (b)
		{
			fwrite((void *)sumbase,sumsize*4,1,b);
			fclose(b);
		}

	#endif
	#endif
}

// fade protection test

extern bool DoFade;
/// store handle used to denote an event used to mark fading was detected
/**
Handle instead of a memory location is used to prevent setting memory breakpoint on it.

We do not want to use HANDLE here, as this would force us using windows.hpp
*/
extern void *DoFadeEventHandle;

/**
Note: windows.h needs to be loaded to use GET_FADE_EVENT / SET_FADE_EVENT
*/
#define GET_FADE_EVENT() ( (HANDLE)DoFadeEventHandle )
#define SET_FADE_EVENT(handle) ( DoFadeEventHandle = (handle) )

#define FADE_ADR_MAGIC 'FADR'

// avoid using direct adress of DoFade here
// SetCRC application will replace it

#define IF_FADE() \
	bool doFade; \
	{ \
		__asm lea eax,DoFade \
		__asm sub eax,FADE_ADR_MAGIC \
		__asm add eax,FADE_ADR_MAGIC \
		__asm cmp byte ptr [eax],0 \
		__asm setne doFade \
	} \
	if (doFade)

#else

#define IF_FADE() if (false)

#endif // _SUPER_RELEASE

void CheckFade();

/// various conditions which may trigger the degradation
enum FadeConditions
{
  /// when player goes prone
  FadeProne,
  /// when player reloads a magazine
  FadeReloadMag,
  /// when mission is started/restarted
  FadeMissionInit,
  /// when a new player has connected
  FadePlayerConnected,
  /// when player gets out
  FadeGetOut,
  /// count of total conditions
  CountFadeConditions
};

void DecFadeSemaphore(int id);

#endif

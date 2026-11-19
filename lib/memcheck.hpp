#ifndef _MEMCHECK_HPP
#define _MEMCHECK_HPP

// memory allocation tracking - find un-freed blocks
#include <Es/Containers/listBidir.hpp>
#include "Es/Containers/maps.hpp"
#include "Es/Memory/debugAlloc.hpp"

#include <El/Debugging/imexhnd.h>

#include <Es/Memory/normalNew.hpp>

#if _DEBUG
  /// allocation postfix - memory guarding block boundaries
  #define GUARD_SIZE 16
  #define PREFIX_SIZE 16
#else
  #define GUARD_SIZE 0
  #define PREFIX_SIZE 0
#endif

#define USE_MEM_FILENAME 1

#if DO_MEM_STATS
  #define USE_MEM_COUNT 0 // not MT safe
  #define USE_MEM_CALLSTACK 1
  #define USE_MEM_CALLSTACK_CS 0 // report based on callstack
  #define USE_MEM_CALLSTACK_CP 1 // report based on call place pairs
  #define USE_MEM_CALLSTACK_CL 1 // report based on call place locations
#else
  #define USE_MEM_COUNT 0
  #define USE_MEM_CALLSTACK 0
  #define USE_MEM_CALLSTACK_CS 0 // report based on callstack
  #define USE_MEM_CALLSTACK_CP 0 // report based on call place
  #define USE_MEM_CALLSTACK_CL 0 // report based on call place locations
#endif

#if USE_MEM_CALLSTACK

#include <El/Debugging/mapFile.hpp>
extern MapFile GMapFile;

void InitMemoryCheck()
{
  // if map-file is already parsed, reuse it
  if (!GMapFile.Empty()) return;
	GMapFile.ParseMapFile();
	// fix offset
	int initMemoryCheckMap = GMapFile.PhysicalAddress("?InitMemoryCheck@@YAXXZ");

	int initMemoryCheckReal = (int)InitMemoryCheck;
	
	int offset = initMemoryCheckReal-initMemoryCheckMap;
	if (offset)
	{
	  LogF("Applying offset %x to mapfile",offset);
	  GMapFile.OffsetPhysicalAddress(offset);
	}
}

#else

void InitMemoryCheck()
{
}

#endif

class MemoryInfo: public TLinkBidirD
{
  public:
	enum {FileBufferSize=108};
	
	private:
	// record all memory blocks
	void *_mem;
	int _size;
	//const char *_file;
	#if USE_MEM_FILENAME
		char _file[FileBufferSize];
		int _line;
	#endif

	#if USE_MEM_CALLSTACK
		void *_callstack[26];
		int _calls;
	#endif

	public:
	MemoryInfo( void *m, int s )
	{
		_mem=m,_size=s;

		#if USE_MEM_FILENAME
			_file[0]=0,_line=0;
		#endif
		#if USE_MEM_CALLSTACK
			_calls = sizeof(_callstack)/sizeof(*_callstack);
			GDebugExceptionTrap.ExtractCallstack(_callstack,_calls,true,&GMapFile);
			//GDebugExceptionTrap.ExtractCallstack(_callstack,_calls,true);
		#endif
	}
	MemoryInfo( void *m, int s, const char *file, int line)
	{
		_mem=m,_size=s;
		#if USE_MEM_FILENAME
			strncpy(_file,file,sizeof(_file));
			_file[sizeof(_file)-1]=0;
			_line=line;
		#endif
		#if USE_MEM_CALLSTACK
			_calls = sizeof(_callstack)/sizeof(*_callstack);
			GDebugExceptionTrap.ExtractCallstack(_callstack,_calls,true,&GMapFile);
			//GDebugExceptionTrap.ExtractCallstack(_callstack,_calls,true);
		#endif
	}

	void Report() const;

	bool Valid() const {return _mem!=NULL && _size>=0;}
	void Invalidate() {_mem=NULL,_size=-1;}

	void *Addr() const {return _mem;}
	int Size() const {return _size;}

	#if USE_MEM_FILENAME
		const char *File() const {return _file;}
		int Line() const {return _line;}
	#endif

	#if USE_MEM_CALLSTACK
		void * const *Callstack() const {return _callstack;}
		int CallstackSize() const {return _calls;}
  #endif
	// override memory allocation
	// this array must not use the resources it monitors
	void *operator new ( size_t size ) {return malloc(size);}
	void operator delete ( void *mem ) {free(mem);}
};

class MemList: public TListBidir<MemoryInfo>
{
	public:
	void *operator new ( size_t size ) {return malloc(size);}
	void operator delete ( void *mem ) {free(mem);}
};

// we are in hpp, however this hpp is included only in one cpp

static MemList *PAllocated=new MemList;
static CriticalSection PAllocatedLock;

void MemoryInfo::Report() const
{
	const int dstSize = 64;
	int srcSize = 128;
	if (srcSize>_size) srcSize = _size;
	char text[dstSize + 1];
	int j = 0;
	for (int i=0; i<srcSize; i++)
	{
		char c = ((char *)_mem)[i];
		if (c >= 32)
		{
			text[j++] = c;
			if (j >= dstSize) break;
		}
	}
	text[j] = 0;
	#if USE_MEM_FILENAME
		if( _file && *_file )
		{
			if( strchr(_file,'.') )
			{
				LogF("%s(%d): Memory %8p:%6d '%s'",_file,_line,_mem,_size,text);
			}
			else
			{
				// _file is probably class name
				LogF("Memory %8p:%6d: '%s':%d",_mem,_size,_file,_line);
			}
		}
		else
		{
			LogF("Memory %8p:%6d '%s'",_mem,_size,text);
		}
	#else
		LogF("Memory %8p:%6d '%s'",_mem,_size,text);
	#endif
	#if USE_MEM_CALLSTACK
		// report callstack
		for (int i=0; i<_calls; i++)
		{
			int eip = (int)_callstack[i];
			int lower;
			const char *name = GMapFile.MapNameFromPhysical(eip,&lower);
			LogF("  %8x: %8x + %s",eip,eip-lower,name);
			//LogF("  %8x",eip);
		}
	#endif

}

inline size_t GetRealSize(void *mem)
{
	int *block=(int *)mem-PREFIX_SIZE/sizeof(int);
	int magic=block[1];
	Assert( magic==15879634 );
	if( magic!=15879634 ) return 0;
	MemoryInfo *info=(MemoryInfo *)block[0];
	Assert( info && info->Valid() );
  return info->Size();
}

intptr_t MemoryKey(MemoryInfo * const &info)
{
  return (intptr_t)info->Addr();
}

ImplicitMap<intptr_t,MemoryInfo *,MemoryKey,true,DebugAlloc> MapMemToInfo;

template<> MemoryInfo *ImplicitMapTraits<MemoryInfo *>::null = (MemoryInfo *)0;
template<> MemoryInfo *ImplicitMapTraits<MemoryInfo *>::zombie = (MemoryInfo *)1;

inline MemoryInfo *GetMemoryInfo(void *mem)
{
	MemoryInfo *info;
  if (MapMemToInfo.get(intptr_t(mem),info))
  {
	if (info && info->Valid())
	{
	  return info;
	}
  }
  __debugbreak();
	return NULL;
}

static AtomicInt FinishAllocTotal(0);


inline void *PrepareFree( void *mem, bool fill=true )
{
  char *block = (char *)mem-PREFIX_SIZE;
	MemoryInfo *info=GetMemoryInfo(mem);
	if (!info)
	{
	  Fail("Memory block info not found");
	  return mem;
	
	}
	
			int fullSize=info->Size()+GUARD_SIZE+PREFIX_SIZE;
  const char *guard=block+fullSize-GUARD_SIZE;
			for( int i=0; i<GUARD_SIZE; i++ )
			{
				if( guard[i]!=MEM_GUARD_VAL )
				{
					ErrF("GUARD (after %x) Memory changed.",mem);
					info->Report();
				}
			}
  guard = (const char *)block;
  for( int i=0; i<PREFIX_SIZE; i++ )
			{
				if( guard[i]!=MEM_GUARD_VAL )
				{
					ErrF("GUARD (before %x) Memory changed.",mem);
					info->Report();
				}
			}

      {
        ScopeLockSection lock(PAllocatedLock);
			  // fill invalid memory with MEM_FREE_VAL
  			info->Delete();

      }
			if (fill)
			{
			  MemSet32(block,MEM_FREE_VAL_32,fullSize);
			}

  FinishAllocTotal -= info->Size();

  MapMemToInfo.removeKey(intptr_t(mem));

			info->Invalidate();
			delete info;
	return (char *)block;
}

#define PREPARE_ALLOC_SLACK ( PREFIX_SIZE+GUARD_SIZE )

inline int PrepareAlloc( int size )
{
	return size+PREPARE_ALLOC_SLACK; // record memory info pointer
}



inline void *FinishAlloc( void *ret, int size, const char *file, int line, bool fill=true )
{
	if( ret )
	{

		int noGuardSize=size-GUARD_SIZE;
		int origSize=noGuardSize-PREFIX_SIZE;


		MemoryInfo *info=new MemoryInfo((char *)ret+PREFIX_SIZE,origSize,file,line);
		if (info)
		{
      ScopeLockSection lock(PAllocatedLock);
			PAllocated->Insert(info);
      FinishAllocTotal += info->Size();
      MapMemToInfo.put(info);
		}
    memset(ret,MEM_GUARD_VAL,PREFIX_SIZE);

		void *guard=(char *)ret+noGuardSize;
		ret=(char *)ret+PREFIX_SIZE;
		if (fill)
		{
		  MemSet32(ret,MEM_NEW_VAL_32,origSize);
		}
		memset(guard,MEM_GUARD_VAL,GUARD_SIZE);
	}
	return ret;
}

/// memory leak reporting
inline void ReportAllocated()
{
	// report all allocated blocks
  ScopeLockSection lock(PAllocatedLock);
	for
	(
		MemoryInfo *info=PAllocated->First();
		info;
		info=PAllocated->Next(info)
	)
	{
		// report:
		info->Report();
	}
}

#endif

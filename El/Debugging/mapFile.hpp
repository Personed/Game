#ifdef _MSC_VER
#pragma once
#endif

#ifndef _MAPFILE_HPP
#define _MAPFILE_HPP

#include <Es/Strings/bString.hpp>
#include <Es/Containers/array.hpp>

#ifdef MAPINFO_EXTENDED
#define MapInfo MapIntoExt
#define MapFile MapFileExt
#endif

struct MapInfo
{
  BString<512> name;
	int section;
	int physAddress; // symbol value
	int logAddress; // logical address

#ifdef MAPINFO_EXTENDED
  BString<128> module;
#endif
	
	/// undecorated (human readable) name
	const char *ReadableName() const;
};

typedef int MapInfo::*MapAddressId;

TypeIsSimpleZeroed(MapInfo);

class AllocatorWin
{
	public:
	//AllocatorWin(){}
	public:
	#if ALLOC_DEBUGGER
	static void *Alloc( size_t &size, const char *file, int line, const char *postfix );
	#else
	static void *Alloc( size_t &size );
	#endif
	static void Free( void *mem, size_t size );
	static void *Realloc( void *mem, size_t oldSize, size_t size );
	static void Unlink( void *mem ) {}
  static inline int MinGrow() {return 32;}
};

class MapFile
{
	BString<512> _name;
	AutoArray<MapInfo,AllocatorWin> _map;
	bool _parseDone;
  enum {MaxSections=32};
  //! index of first item not in given section
  int _sectionEnd[MaxSections];
  //! index of first item not in section 1
  //int _firstSection2;

	public:
	MapFile();
	~MapFile();

	void ParseMapFile(const char *name=NULL);
	// add given offset to all physical addresses
	void OffsetPhysicalAddress(int offset);
	void Clear();

	const char *GetName() const {return _name;}
	const char *MapName( int address, MapAddressId id, int *lower=NULL ) const;
  const char *MapRawName( int address, int section, MapAddressId id, int *lower=NULL ) const;
	int FunctionStart( int address, MapAddressId id) const;
	const char *MapNameFromLogical( int lAddress, int *lower=NULL ) const
	{
		return MapName(lAddress,&MapInfo::logAddress,lower);
	}
	int FunctionStartFromLogical(int lAddress) const
	{
		return FunctionStart(lAddress,&MapInfo::logAddress);
	}

	const MapInfo *FindByPhysical( int fAddress ) const;

	const char *MapNameFromPhysical( int fAddress, int *lower=NULL ) const;
	void *FunctionStartFromPhysical(void *pAddress) const;

	int Address( const char *name, MapAddressId id ) const ;
	int AddressBySubstring( const char *name, MapAddressId id ) const;
	int MinAddress( MapAddressId id ) const ;
	int MaxAddress( MapAddressId id, int section=1 ) const ;

	int PhysicalAddress( const char *name ) const {return Address(name,&MapInfo::physAddress);}
	int LogicalAddress( const char *name ) const {return Address(name,&MapInfo::logAddress);}

	int PhysicalAddressBySubstring( const char *name ) const {return AddressBySubstring(name,&MapInfo::physAddress);}
	int LogicalAddressBySubstring( const char *name ) const {return AddressBySubstring(name,&MapInfo::logAddress);}

	int MinPhysicalAddress() const {return MinAddress(&MapInfo::physAddress);}
	int MaxPhysicalAddress() const {return MaxAddress(&MapInfo::physAddress);}
	int MinLogicalAddress() const {return MinAddress(&MapInfo::logAddress);}
	int MaxLogicalAddress() const {return MaxAddress(&MapInfo::logAddress);}

	bool Empty() const {return _map.Size()<=0;}
	bool Parsed() const {return _parseDone;}
	
  protected:
  //@{ allow inherited classes to use access to symbols
	int GetSymbolCount() const {return _map.Size();}
	MapInfo &GetSymbol(int i) {return _map[i];}
	const MapInfo &GetSymbol(int i) const {return _map[i];}
	//@}
};

#endif

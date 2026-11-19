// Map File parsing

#include <El/elementpch.hpp>
#include "mapFile.hpp"

//#include <fstream.h>
#include <Es/Algorithms/qsort.hpp>
//#include <El/QStream/qStream.hpp>
#include <Es/Common/win.h>
#include <Es/Files/filenames.hpp>
#include <Es/Common/fltopts.hpp>

#if REM
// typical map file looks like:
//...some other stuff...
//...followed by:
  Address         Publics by Value              Rva+Base     Lib:Object

 0001:00000000       ??_H@YGXPAXIHP6EX0@Z@Z     00401000 f i engGlide.obj
 0001:00000030       ??_I@YGXPAXIHP6EX0@Z@Z     00401030 f i engGlide.obj
 0001:00000070       ??0EngineGlide@@QAE@PAX0HHH_N@Z 00401070 f   engGlide.obj
#endif

#if ALLOC_DEBUGGER
void *AllocatorWin::Alloc( size_t &size, const char *file, int line, const char *postfix )
{
  void *mem = GlobalAlloc(GMEM_FIXED,size);
  if (!mem)
  {
    OutputDebugString("Out of memory\n");
    FailHook("Out of memory");
    // issue some low level warning - windows memory is not available
  }
  return mem;
}
#else
void *AllocatorWin::Alloc( size_t &size )
{
  // size is not changed
  #if defined(_WIN32)
  void *mem = GlobalAlloc(GMEM_FIXED,size);
  #else
  void *mem = malloc(size);
  #endif
  if (!mem)
  {
    // issue some low level warning - windows memory is not available
    #define ERROR_MESSAGE "Out of memory during map file loading"
    OutputDebugString(ERROR_MESSAGE "\n");
    FailHook(ERROR_MESSAGE);
    abort();
  }
  return mem;
}
#endif
void AllocatorWin::Free( void *mem, size_t size )
{
  #if defined(_WIN32)
  GlobalFree(mem);
  #else
  free(mem);
  #endif
}

void *AllocatorWin::Realloc( void *mem, size_t oldSize, size_t size )
{
  #if defined(_WIN32)
  return GlobalReAlloc(mem,size,0);
  #else
  // there is no portable way to resize the memory block
  // realloc is allowed to move it, which we cannot
  return NULL;
  #endif
}


static int CmpMaps( const MapInfo *f0, const MapInfo *f1 )
{
  int d = f0->section-f1->section;
  if (d) return d;
  return f0->physAddress-f1->physAddress;
}

/*
static void GetLine( char *line, int size, QIStream &in)
{
  char * l = line;
  int c = in.get();
  while (c!=EOF && c!='\n')
  {
    if (size>1)
    {
      if (c!='\r')
      {
        *l++ = c;
        size--;
      }
    }
    c = in.get();
  }
  if (size>0) *l=0;
}
*/

static void GetLine( char *line, int size, FILE *in)
{
  char * l = line;
  int c = fgetc(in);
  while (c!=EOF && c!='\n')
  {
    if (size>1)
    {
      if (c!='\r')
      {
        *l++ = c;
        size--;
      }
    }
    c = fgetc(in);
  }
  if (size>0) *l=0;
}

MapFile::MapFile()
{
  _parseDone = false;
}

MapFile::~MapFile()
{
}

void MapFile::Clear()
{
  _map.Clear();
  _parseDone = false;
}

static const char *ReadableName(const char *name);
static const char *ReadableName(BString<256> &res, const char *name);

/*!
\param name if name is NULL, mapfile based on current exe name is loaded.
*/
void MapFile::ParseMapFile(const char *name)
{
  if (_parseDone) return;
  #if defined _WIN32
  //QIFStream in;
  if (!name)
  {
    char sourceName[256];
    // use program name as given on command line
    const char *cLine=GetCommandLine();
    if( *cLine=='"' )
    {
      const char *eName=strchr(cLine+1,'"');
      if( !eName )
      {
        LogF("Cannot get program name from '%s'.",cLine);
        return;
      }
      strncpy(sourceName,cLine+1,eName-1-cLine),sourceName[eName-1-cLine]=0;
    }
    else
    {
      const char *eName=strchr(cLine,' ');
      if( !eName ) strcpy(sourceName,cLine);
      else strncpy(sourceName,cLine,eName-cLine),sourceName[eName-cLine]=0;
    }
    // replace .exe with .map
    //LogF("Mapfile for '%s'.",sourceName.cstr());
    SYSTEMTIME syst;
    GetLocalTime(&syst);
    /*
    LogF
    (
      "%4d/%02d/%02d, %2d:%02d:%02d",
      syst.wYear,syst.wMonth,syst.wDay,
      syst.wHour,syst.wMinute,syst.wSecond
    );
    */
    char *ext=GetFileExt(GetFilenameExt(sourceName));
    if( ext )
    {
      strcpy(ext,".map");
    }
    else
    {
      strcat(sourceName,".map");
    }
    //LogF("Loading map file '%s'.",sourceName);

    strcpy(_name,sourceName);
  }
  else
  {
    strcpy(_name,name);
  }

  FILE *in = fopen(_name,"r");
  if (in)
  {
    // FIX: note, some Botan library stuff has its mapfile lines very long (3595 characters) see news:k3s7ts$46b$2@new-server.localdomain
    char line[16384];
    bool error = false;
    for(;;)
    {
      GetLine(line,sizeof(line),in);
      //in.getline(line,sizeof(line));
      //if( in.eof() || in.fail() )
      if (ferror(in) || feof(in))
      {
        error = true;
        break;
      }
      if
      (
        !strcmpi(
          line,
          "  Address         Publics by Value              Rva+Base     Lib:Object"
        )
        || !strcmpi(
          line,
          "  Address         Publics by Value              Rva+Base      Lib:Object"
        )
        || !strcmpi(
          line,
          "  Address         Publics by Value              Rva+Base       Lib:Object"
        )
      ) break;
    }
    // 
    while (!error)
    { // search for first nonempty line
      char name[sizeof(line)];
      GetLine(line,sizeof(line),in);
      if (ferror(in) || feof(in)) break;
      //if( in.eof() || in.fail() ) break;
      // remove terminating EOL
      if (*line && line[strlen(line)-1]=='\n') line[strlen(line)-1]=0;
      if( !strchr(line,':') ) continue;
      if( line[0]==0 ) continue;
      // scan line for: <rel addr> <name> <abs addr> <......>
      const char *c=line;
      while( isspace(*c) ) c++;
      if (!isdigit(*c))
      {
        // non-digit lines should be skipped
        continue;
      }
      int logSection=strtoul(c,(char **)&c,16);
      if (logSection<1) continue;
      if (logSection>=MaxSections)
      {
        Assert(logSection<MaxSections);
        continue;
      }
      
      while( *c==':' || isspace(*c) ) c++;
      int logAddress=strtoul(c,(char **)&c,16);
      while( isspace(*c) ) c++;
      char *d=name;
      while( !isspace(*c) && *c ) *d++=*c++; // get name
      *d=0;
      while( isspace(*c) ) c++;
      int physAddress=strtoul(c,(char **)&c,16);
      if( *name==0 ) continue; // no name
      MapInfo info;
      // if extended mapinfo is required, parse it
      #ifdef MAPINFO_EXTENDED
      // object name should be at the end of the line
      int rest = strlen(c);
      if (rest>1)
      {
        const char *symName = c + rest;
        while (rest>1 && !isspace(symName[-1]))
        {
          --symName;
          rest--;
        }
        info.module = symName;
      }
      else
      {
        strcpy(info.module,"");
      }
      #endif

      // we can ignore rest of the line
      // store name/address information
      info.section = logSection;
      info.name = name;
      info.physAddress=physAddress;
      info.logAddress=logAddress;
      _map.Add(info);
    }
    _map.Compact();
    QSort(_map.Data(),_map.Size(),CmpMaps);

    fclose(in);
  }
  #endif
  
  int curSection = 0;

  //_sectionEnd
  for (int i=0; i<_map.Size(); i++)
  {
    const MapInfo &info = _map[i];
    if (info.section>curSection)
    {
      for (int s=curSection; s<info.section; s++)
      {
        _sectionEnd[s] = i;
      }
      curSection = info.section;
    }
  }

  for (int s=curSection; s<MaxSections; s++)
  {
    _sectionEnd[s] = _map.Size()-1;
  }
  _parseDone = true;
#if 0 //_DEBUG // unmangle all names
  for (int i=0; i<_sectionEnd[1]; i++)
  {
    const char *rawName = _map[i].name;
    const char *readName = ReadableName(rawName);
    // check readable name
    if (strstr(readName,"?"))
    {
      const char *check = ReadableName(rawName);
    }
  }
#endif
}

void MapFile::OffsetPhysicalAddress(int offset)
{
  if (!offset) return;
  for (int i=0; i<_map.Size(); i++)
  {
    MapInfo &info = _map[i];
    info.physAddress += offset;
  }
}

int MapFile::FunctionStart( int address, MapAddressId id) const
{
  for( int i=0; i<_map.Size(); i++ )
  {
    //if( _map[i].*id>=address )
    if( _map[i].*id<=address && ( i>=_map.Size()-1 || _map[i+1].*id>address ) )
    {
      return _map[i].*id;
    }
  }
  return 0;
}

const char *MapFile::MapRawName( int address, int section, MapAddressId id, int *lower ) const
{
  for( int i=0; i<_map.Size(); i++ )
  {
    //if( _map[i].*id>=address )
    if (section>=0 && _map[i].section!=section) continue;
    if( _map[i].*id<=address && ( i>=_map.Size()-1 || _map[i+1].*id>address ) )
    {
      if( lower ) *lower=_map[i].*id;
      return _map[i].name;
    }
  }
  return "???";
}

const char *GetClassName(BString<256> &clsName, const char *name);

const char *GetTypeName(BString<256> &clsName, const char *name)
{
  if (name[0]=='?' && name[1]=='$')
  {
    // template
    name = GetClassName(clsName,name);
  }
  else if (
     // V - class, U - struct, T - union
    name[0]=='T' || name[0]=='U' || name[0]=='V'
  )
  {
    name++;
    BString<256> cls;
    name = GetClassName(cls,name);
    // get qualification
    clsName[0]=0;
    while (*name!='@' && *name)
    {
      BString<256> scope;
      name = GetClassName(scope,name);
      strcat(clsName,scope);
      strcat(clsName,"::");
    }
    strcat(clsName,cls);
    if (*name) name++;
    
  }
  else if (name[0]=='P')
  {
    // pointer
    name++;
    if (*name=='A' || *name=='B' || *name=='C')
    {
      // const / volatile qualifier
      name++;
    }
    BString<256> type;
    name = GetTypeName(type,name);
    strcpy(clsName,type);
    strcat(clsName," *");
  }
  else if (name[0]=='Q')
  {
    name++;
    if (*name=='A' || *name=='B' || *name=='C')
    {
      // const / volatile qualifier
      name++;
    }
    BString<256> type;
    name = GetTypeName(type,name);
    strcpy(clsName,type);
    strcat(clsName,"[]");
  }
  else switch (name[0])
  {
    case 'C': strcpy(clsName,"signed char");name++; break;
    case 'D': strcpy(clsName,"char");name++; break;
    case 'E': strcpy(clsName,"unsigned char");name++; break;
    case 'F': strcpy(clsName,"short");name++; break;
    case 'G': strcpy(clsName,"unsigned short");name++; break;
    case 'H': strcpy(clsName,"int");name++; break;
    case 'I': strcpy(clsName,"unsigned");name++; break;
    case 'J': strcpy(clsName,"long");name++; break;
    case 'K': strcpy(clsName,"unsigned long");name++; break;
    case 'L': strcpy(clsName,"?");name++; break;
    case 'M': strcpy(clsName,"float");name++; break;
    case 'N': strcpy(clsName,"double");name++; break;
    case 'O': strcpy(clsName,"long double");name++; break;
    case 'X': strcpy(clsName,"void");name++; break;
    case '_':
      // double char type
      if (name[1]=='N') strcpy(clsName,"bool");
      if (name[1]=='J') strcpy(clsName,"__int64");
      else strcpy(clsName,"?");
      name += 2;
      break;
    default:
      strcpy(clsName,"?");
      name++;
      break;
  }

/*
pointer        P (see below)
array          Q (see below)
struct/class   V (see below)
void           X (terminates argument list)
elipsis        Z (terminates argument list)
*/

  return name;
}

const char *GetClassName(BString<256> &clsName, const char *name)
{
  if (name[0]=='?' && name[1]=='$')
  {
    const char *eName = strchr(name,'@');
    if (eName)
    {
      strcpy(clsName,name+2);
      clsName[eName-name-2] = 0;
      name = eName+1;
      strcat(clsName,"<");
      bool first = true;
      while (*name!='@' && *name)
      {
        BString<256> templateArg;
        //strcpy(templateArg,eName+2);
        if (*name=='$')
        {
          if (name[1]=='1' && name[2]=='?')
          {
            name = ReadableName(templateArg,name+2);
          }
          else if (name[2]=='?')
          {
            name = ReadableName(templateArg,name);
          }
          else
          {
            strcpy(templateArg,"?");
            name += 2;
          }
        }
        else
        {
          name = GetTypeName(templateArg,name);
        }
        //if (*name=='@') name++;
        if (!first) strcat(clsName,",");
        strcat(clsName,templateArg);
        first = false;
      }
      strcat(clsName,">");
      if (*name) name++;
    }
  }
  else
  {
    strcpy(clsName,name);
    const char *eName = strstr(name,"@");
    if (eName)
    {
      clsName[eName-name] = 0;
      name = eName+1;
    }
    else
    {
      strcpy(clsName,"?");
      name++;
    }
  }
  return name;
}

#if _ENABLE_REPORT && defined _WIN32

#define IMAGEHLP_SYMS 1

#endif


#if IMAGEHLP_SYMS

#include <imagehlp.h>
// UnDecorateSymbolName

typedef DWORD __stdcall WINAPI UnDecorateSymbolNameF
(
    LPCSTR   DecoratedName,         // Name to undecorate
    LPSTR    UnDecoratedName,       // If NULL, it will be allocated
    DWORD    UndecoratedLength,     // The maximym length
    DWORD    Flags                  // See above.
);

static HMODULE ImageHlpLib = LoadLibrary("imagehlp.dll");

static UnDecorateSymbolNameF *SUnDecorateSymbolName = (UnDecorateSymbolNameF *)
(
  ImageHlpLib ? GetProcAddress(ImageHlpLib,"UnDecorateSymbolName") : NULL
);

#endif

static char *Skip(char *name, const char **skiplist)
{
  // skip mid strings as necessary
  for(;;)
  {
    bool skipped = false;
    for (const char **skip=skiplist; *skip; skip++)
    {
      if (**skip!=*name) continue;
      int len = strlen(*skip);
      if (strncmp(*skip,name,len)) continue;
      name += len;
      skipped = true;
      break;
    }
    if (!skipped) break;
  }
  return name;
}

static void Skip(char *name, const char **skipStart, const char **skipMid)
{
  char *retName = name;
  char dstBuff[512];
  char *dst = dstBuff;
  if (strlen(name)>sizeof(dstBuff))
  {
    name[sizeof(dstBuff)-1] = 0;
  }
  // skip start strings as necessary
  name = Skip(name,skipStart);
  // copy what is not matching skipMid
  while (*name)
  {
    // skip mid strings as necessary
    name = Skip(name,skipMid);
    if (!__iscsym(*name))
    {
      *dst++ = *name++;
    }
    else
    {
      // skip whole identifiers at once
      *dst++ = *name++;
      while (__iscsymf(*name))
      {
        *dst++ = *name++;
      }
    }
    
  }
  *dst = 0;
  strcpy(retName,dstBuff);
}


static const char *ReadableName(BString<256> &res, const char *name)
{
  res[0] = 0;
  #if _DEBUG && IMAGEHLP_SYMS
    char readableName[512];
    if (SUnDecorateSymbolName)
    {
      SUnDecorateSymbolName(name,readableName,sizeof(readableName),UNDNAME_COMPLETE);
      readableName[sizeof(readableName)-1]=0;
    }
  #elif IMAGEHLP_SYMS
    if (SUnDecorateSymbolName)
    {
      static const char *skipStart[] = 
      {
        "public: ", "private: ", "protected: ",
        "virtual ", "static ",
        NULL
      };
      static const char *skipMid[] = 
      {
        "__thiscall ","__cdecl ",
        "struct ","class ",
        NULL
      };
      static char readableName[512];
      SUnDecorateSymbolName(name,readableName,sizeof(readableName),UNDNAME_COMPLETE);
      readableName[sizeof(readableName)-1]=0;
      Skip(readableName,skipStart,skipMid);
      strcpy(res,readableName);
      return readableName+strlen(readableName);
    }
  #endif
  // extract class and function name
  // starting character is calling convention
  if( *name=='?' ) // C++ decorated name
  {
    //const char *eName;
    BString<256> resName;
    BString<512> resClass;
    name++;
    // operator starts with a ?, template starts with a ?$
    if( name[0]=='?' && name[1]!='$')
    {
      name++;
      switch( *name )
      {
        case '0': strcpy(resName,"constructor");name++;break;
        case '1': strcpy(resName,"destructor");name++;break;
        case '2': strcpy(resName,"operator new");name++;break;
        case '3': strcpy(resName,"operator delete");name++;break;
        case '4': strcpy(resName,"operator =");name++;break;
        case '5': strcpy(resName,"operator >>");name++;break;
        case '6': strcpy(resName,"operator <<");name++;break;
        case '8': strcpy(resName,"operator ==");name++;break;
        //case '4': strcpy(resName,"operator ->");name++;break;
        case 'A': strcpy(resName,"operator []");name++;break;
        case 'B': strcpy(resName,"operator (cast)");name++;break;
        case 'C': strcpy(resName,"operator ->");name++;break;
        case 'E': strcpy(resName,"operator ++");name++;break;
        case 'H': strcpy(resName,"operator +");name++;break;
        case 'R': strcpy(resName,"operator ()");name++;break;
        case '_':
          name++;
          if (*name=='G') {strcpy(resName,"scalar delete");}
          else if (*name=='E') {strcpy(resName,"vector delete");}
          else
          {
            strcpy(resName,"??? destruct ");
            resName[(int)strlen(resName)] = *name;
          }
          name++;
          break;
        default:
          {
            char add[2];
            add[0]=*name++;
            add[1]=0;
            strcpy(resName,"operator??");
            strcat(resName,add);
          }
          break;
      }
    }
    else
    {
      // note: function name may be template
      name = GetClassName(resName,name);
    }
    // all before @@ is class name
    // list of class names, each class name terminated by @,
    // list terminated by another @
    strcpy(resClass,"");
    while (*name!='@' && *name)
    {
      BString<256> className;
      name = GetClassName(className,name);
      strcat(resClass,className);
      strcat(resClass,"::");
    }
    if (*name) name++;
    if (*name=='Y' || *name=='Q')
    {
      name++;
      // scan return value
      if (*name=='A' || *name=='I' || *name=='G')
      {
        name++;
      }
      BString<256> datatype;
      name = GetTypeName(datatype,name);
      
    }
    else if (*name=='2' || *name=='3')
    {
      name++;
      BString<256> datatype;
      //name = GetDataType(datatype,name)
    }
    if (*name=='X') name++;
    if (*name=='Z') name++;
    // scan argument list
    strcpy(res,resClass);
    strcat(res,resName);
    return name;
  }
  else
  {
    strcpy(res,name);
    return name+strlen(name);
  }
}

static const char *ReadableName(const char *name)
{
  static BString<256> resName;
  ReadableName(resName,name);
  return resName;
}

const char *MapInfo::ReadableName() const
{
  static BString<256> resName;
  ::ReadableName(resName,name);
  return resName;
}


const MapInfo *MapFile::FindByPhysical( int fAddress ) const
{
  if (_map.Size()<1) return NULL;
  // check first
  if (_map[0].physAddress>fAddress) return NULL;
  // check last
  int last = _map.Size()-1;
  if( _map[last].physAddress<=fAddress ) return &_map[last];
  // it is somewhere in between
  // we know physical address is monotone
  // we may fast iterate
  // check if 1. section is enough
  int hi = _map.Size()-1;
  if (fAddress<=_map[_sectionEnd[1]].physAddress) hi = _sectionEnd[1];
  int lo = 0;
  if( _map[hi].physAddress<=fAddress ) return &_map[hi];
  // always: _map[lo].physAddress<=fAddress, _map[hi].physAddress>fAddress
  while (hi>lo)
  {
    Assert( _map[lo].physAddress<=fAddress );
    
    Assert( _map[hi].physAddress>fAddress );
    // estimate where could the result be
    int hiAddr = _map[hi].physAddress;
    int loAddr = _map[lo].physAddress;

    if (_map[lo+1].physAddress>fAddress) {hi=lo;break;}
    float approxIndex = (fAddress-loAddr)/float(hiAddr-loAddr)*(hi-lo)+lo;
    int approxI = toInt(approxIndex);
    if (approxI>=hi) approxI = hi-1;
    if (approxI<=lo) approxI = lo+1;
    const MapInfo &info = _map[approxI];
    if (info.physAddress>fAddress) hi = approxI;
    else lo = approxI;
  }
  Assert(hi == lo);
  int fastIndex = lo;
  //Assert(fastIndex == slowIndex);
  if (fastIndex>=0) return &_map[fastIndex];
  return NULL;
}

/*!
commonly used - optimized for best speed
*/

const char *MapFile::MapNameFromPhysical( int fAddress, int *lower) const
{
  const MapInfo *info = FindByPhysical(fAddress);
  if (!info)
  {
    if (lower) *lower = 0;
    return "???";
  }
  if (lower) *lower = info->physAddress;
  return ReadableName(info->name);
}

/*!
commonly used - optimized for best speed
*/

void *MapFile::FunctionStartFromPhysical(void *pAddress) const
{
  const MapInfo *info = FindByPhysical((int)pAddress);
  if (!info)
  {
    return 0;
  }
  return (void *)info->physAddress;
}

const char *MapFile::MapName( int address, MapAddressId id, int *lower ) const
{
  for( int i=0; i<_map.Size(); i++ )
  {
    //if( _map[i].*id>=address )
    if( _map[i].*id<=address && ( i>=_map.Size()-1 || _map[i+1].*id>address ) )
    {
      if( lower ) *lower=_map[i].*id;
      const char *name=_map[i].name;
      return ReadableName(name);

    }
  }
  return "???";
}

int MapFile::MinAddress( MapAddressId id ) const
{
  if( _map.Size()<=0 ) return 0;
  return _map[0].*id;
}

int MapFile::MaxAddress( MapAddressId id, int section ) const 
{
  if( _map.Size()<=0 ) return 0x7fffffff;
  return _map[_sectionEnd[section]-1].*id;
}

int MapFile::Address( const char *name, MapAddressId id ) const 
{
  for( int i=0; i<_map.Size(); i++ )
  {
    if( !strcmp(_map[i].name,name) ) return _map[i].*id;
  }
  return 0;
}

int MapFile::AddressBySubstring(const char *name, MapAddressId id) const 
{
  for( int i=0; i<_map.Size(); i++ )
  {
    if (strstr(_map[i].name,name)) return _map[i].*id;
  }
  return 0;
}


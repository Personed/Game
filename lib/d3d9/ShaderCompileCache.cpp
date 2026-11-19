#include "../wpch.hpp"
#if _ENABLE_COMPILED_SHADER_CACHE && !defined _SERVER && !_DISABLE_GUI

//#include <malloc.h>
#include <El/QStream/qStream.hpp>
#include <El/QStream/QBStream.hpp>
#include <Es/Strings/bString.hpp>
#include "d3d9Defs.hpp"
#include <El/FileServer/fileServer.hpp>
#include "ShaderCompileCache.h"

#include <El/crc/crc.hpp>

///Change this value, if you want to invalidate cache from previous version
#define CACHE_ID "0004"

class ShaderCacheFileDesc
{
  QOFStream _outcache;
  QIFStreamB _incache;
  RString _sectionName;
  //! Custom path to place the file, temporary folder taken if not specified
  RString _customPath;

  bool _cached;

public:
  //! Destructor
  ~ShaderCacheFileDesc()
  {
    _outcache.close();
  }
  void Init(const Array<RString> &sources, const char *sectionName, RStringB customPath, bool forceCreate);

  RString GetCacheFilename();
  static bool PrepareValidTestBlock(const Array<RString> &sources, QOStrStream &testBlock, bool noSources);

  bool ReadFromCache(LPD3DXBUFFER *buffer);
  bool WriteToCache(LPD3DXBUFFER buffer);
  bool IsCacheActive() {return _cached;}
  void DropCache()
  {
    _incache.close();
    _outcache.open(GetCacheFilename());
    _outcache.close();
    LogF("Inconsistent cache detected - cache has been invalidated.");
  }
  bool TestNextSection(const Array<RString> &sectionNames);
  bool CompareWithCache(LazyLoadFile &testBlock);
};

static SRef<ShaderCacheFileDesc> SCurrentShaderFileDesc;
//static CriticalSection SCurrentShaderFileDescLock;

RString ShaderCacheFileDesc::GetCacheFilename()
{
  RStringB path;
  if (_customPath.IsEmpty())
  {
    unsigned long needsz = GetTempPath(0,0);
    char *name = path.CreateBuffer(needsz);
    GetTempPath(needsz, name);
  }
  else
  {
    path = _customPath;
  }
  
  const char *ext = ".shdc";
  #if SHADER_DBG_LEVEL>1
    if (GEnablePatching) ext = "_dbg.shdc";
  #elif SHADER_DBG_LEVEL>0
    if (GEnablePatching) ext = "_prof.shdc";
  #endif
  RString name = path + _sectionName + ext;
  LogF("Path for cache of compiled shaders: %s", cc_cast(name));
  return name;
}

bool ShaderCacheFileDesc::PrepareValidTestBlock(const Array<RString> &sources, QOStrStream &testBlock, bool noSources)
{
  const char *header="BIShaderCache-id: "CACHE_ID;
  testBlock.write(header,strlen(header)+1);
  bool sourceFilesExist = true;
  for (int i=0;i<sources.Size();i++)
  {
    // on Xbox DVD timestamps are for whole disk, which is not practical for development
    // use filesize + CRC instead of timestamp - better suited for source control systems
    // Note both branches must write the same number of chars into the testBlock file
    if (noSources || !QFBankQueryFunctions::FileExists(sources[i]))
    {
      // write file size
      unsigned long size = 0xFFFFFFFF;
      testBlock.write(&size,sizeof(size));
      
      // use the result
      unsigned long res = 0xFFFFFFFF;
      testBlock.write(&res,sizeof(res));
      
      sourceFilesExist = false;
    }
    else
    {
      QIFStreamB in;
      in.AutoOpen(sources[i]);
      // write file size
      unsigned long size = in.rest();
      // we always need to work in little endian (x86) format, even on big endian (X360) platforms
      testBlock.put(size&0xff);
      testBlock.put((size>>8)&0xff);
      testBlock.put((size>>16)&0xff);
      testBlock.put((size>>24)&0xff);
      // write CRC
      CRCCalculator crc;
      crc.Reset();
      // read whole file, processing CRC
      in.copy(crc);
      // use the result
      unsigned long res = crc.GetResult();
      testBlock.put(res&0xff);
      testBlock.put((res>>8)&0xff);
      testBlock.put((res>>16)&0xff);
      testBlock.put((res>>24)&0xff);
    }
  }
  // we need big/little endian to be compatible
  unsigned long sdkx=D3DX_VERSION;
  testBlock.put(sdkx&0xff);
  testBlock.put((sdkx>>8)&0xff);
  testBlock.put((sdkx>>16)&0xff);
  testBlock.put((sdkx>>24)&0xff);
  unsigned long sdk=D3DX_SDK_VERSION;
  testBlock.put(sdk&0xff);
  testBlock.put((sdk>>8)&0xff);
  testBlock.put((sdk>>16)&0xff);
  testBlock.put((sdk>>24)&0xff);
  unsigned long mark=0xAA55AA55;
  testBlock.put(mark&0xff);
  testBlock.put((mark>>8)&0xff);
  testBlock.put((mark>>16)&0xff);
  testBlock.put((mark>>24)&0xff);
  // verify debug level as well
  unsigned long debug=SHADER_DBG_LEVEL; // replicate value to all 4 bytes
  testBlock.put(debug);
  testBlock.put(debug);
  testBlock.put(debug);
  testBlock.put(debug);
  return sourceFilesExist;
}

extern bool DisableShaderGeneration;


static int CheckMatch(const char *m1, const char *m2, size_t size)
{
  const char *m1start = m1;
  while (size-->0)
  {
    if(*m1!=*m2) break;
    ++m1,++m2;
  }
  return m1-m1start;
}

/*!
  \patch 5151 Date 3/29/2007 by Flyman
  - Fixed: Retail version doesn't try to compile shaders now unless being specifically asked to generate them
*/
void ShaderCacheFileDesc::Init(const Array<RString> &sources, const char *sectionName, RStringB customPath, bool forceCreate)
{
  QOStrStream testBlock;

  // Determine whether we use retail version or not
  #if _SUPER_RELEASE
  const bool retail = true;
  #else
  const bool retail = false;
  #endif

  // Enter the no-source code in the following cases:
  // - sources are really not found
  // - retail version is being used and cache creation was not forced (that means we don't want to create the shader cache)
  // - we were forced by a program argument not to create the shader cache
  bool sourceFilesExist = PrepareValidTestBlock(sources,testBlock, retail && !forceCreate);

  _sectionName=sectionName;

  // Remember the cache folder, if on XBOX and sources are present, redirect it to scratch
  _customPath = customPath;

  RString cachename=GetCacheFilename();
  if(!forceCreate)
    _incache.AutoOpen(cachename);

  if (!sourceFilesExist || DisableShaderGeneration)
  {
    DoAssert(!_incache.fail());
    int size=testBlock.pcount();
    char *cmpmem=(char *)alloca(size);
    int rd=_incache.read(cmpmem,size);
    if (rd != size)
    {
      _incache.close();
      ErrorMessage("Shaders not valid (mismatch of exe and data?)");
    }
    if (DisableShaderGeneration)
    {
      if (sourceFilesExist)
      {
        enum Match {OK,Size,Content} match = OK;
        int matchSize = 0;
        if (rd!=size) match = Size;
        else if ((matchSize=CheckMatch(cmpmem,testBlock.str(),size))!=size) match = Content;
        if (match!=OK)
        {
          LogF("Using cache of compiled shaders (Forced by program argument, sources not matching, %s: reason %d, %d of %d).",sectionName,match,matchSize,size);
    }
        else
        {
          DoAssert(memcmp(cmpmem,testBlock.str(),size)==0);
          LogF("Using cache of compiled shaders (Forced to use it by program argument, sources matching).");
        }
      }
      else
      {
        LogF("Using cache of compiled shaders. (Forced to use it by program argument, no sources).");
      }
    }
    else if (retail)
    {
      LogF("Using cache of compiled shaders. Retail version.");      
    }
    else
    {
      LogF("Using cache of compiled shaders. No sources present.");      
    }
    _cached=true;
  }
  else if (_incache.fail() || forceCreate)
  {
    _cached=false;
    if (!_customPath.IsEmpty()) CreatePath(_customPath);
    _outcache.open(cachename);
    _outcache.write(testBlock.str(),testBlock.pcount());
    LogF("Creating cache of compiled shaders");
  }
  else
  {
    int size=testBlock.pcount();
    char *cmpmem=(char *)alloca(size);
    int rd=_incache.read(cmpmem,size);
    enum Match {OK,Size,Content} match = OK;
    int matchSize = 0;
    if (rd!=size) match = Size;
    else if ((matchSize=CheckMatch(cmpmem,testBlock.str(),size))!=size) match = Content;
    if (match!=OK)
    {
      _cached=false;
      _incache.close();
      if (!_customPath.IsEmpty()) CreatePath(_customPath);
      _outcache.open(cachename);
      _outcache.write(testBlock.str(),testBlock.pcount());
      LogF("Note: Compiled shaders cache not matching (%s, reason %d, %d of %d).",sectionName,match,matchSize,size);
    }
    else
    {
      DoAssert(memcmp(cmpmem,testBlock.str(),size)==0);
      LogF("Using cache of compiled shaders. It will speed-up startup.");      
      _cached=true;
    }
  }
}

bool ShaderCacheFileDesc::TestNextSection(const Array<RString> &sectionNames)
{
  QOStrStream testBlock;
  for (int i=0;i<sectionNames.Size();i++)
  {
    testBlock.write(sectionNames[i],strlen(sectionNames[i])+1);
  }
  unsigned long magic=0x11222211; // Symmetric magic for X360 purposes
  testBlock.write(&magic,sizeof(magic));
  int size=testBlock.pcount();
  const char *data=testBlock.str();
  if (IsCacheActive())
  {
    char *cmpmem=(char *)alloca(size);
    int rd=_incache.read(cmpmem,size);
    return rd==size && memcmp(cmpmem,data,size)==0;       
  }
  else
  {
    _outcache.write(data,size);
    return true;
  }
}


void ShaderCompileCache::ShaderCacheInit(const Array<RString> &sources, const char *sectionName, RStringB customPath, bool forceCreate)
{
  Log("ShaderCacheInit, was %p",SCurrentShaderFileDesc.GetRef());
  SCurrentShaderFileDesc=new ShaderCacheFileDesc;
  SCurrentShaderFileDesc->Init(sources, sectionName, customPath, forceCreate);
}


bool ShaderCacheFileDesc::ReadFromCache(LPD3DXBUFFER *buffer)
{
  unsigned long sz;
  sz = (unsigned long) _incache.getil();
  if (D3DXCreateBuffer(sz,buffer)!=0) return false;
  if (_incache.read((*buffer)->GetBufferPointer(),sz)!=sz)
  {
    (*buffer)->Release();
    return false;
  }
  unsigned long magic;
  if (_incache.read(&magic,sizeof(magic))!=sizeof(magic) || magic!=0x00AAAA00)
  {
    (*buffer)->Release();
    return false;
  }

  return true;
}

void SwapEndian(unsigned long &i)
{
  i = ((i&0xff)<<24)|((i&0xff00)<<8)|((i&0xff0000)>>8)|(i>>24);
}

bool ShaderCacheFileDesc::WriteToCache(LPD3DXBUFFER buffer)
{
  unsigned long sz = buffer->GetBufferSize();
  unsigned long szData = sz;
  unsigned long magic=0x00AAAA00; // Symmetric magic for X360 purposes
  _outcache.write(&szData,sizeof(szData));
  _outcache.write(buffer->GetBufferPointer(),sz);
  _outcache.write(&magic,sizeof(magic));
  return true;
}

void ShaderCompileCache::ShaderCacheDone()
{
  Log("ShaderCacheDone, was %p",SCurrentShaderFileDesc.GetRef());
  SCurrentShaderFileDesc=0;
}

HRESULT WINAPI D3DXCompileShader_Cached(
  const LazyLoadFile &src, CONST D3DXMACRO* pDefines,
  LPD3DXINCLUDE pInclude,
  LPCSTR pFunctionName, LPCSTR pProfile, DWORD Flags,
  D3DXCompileShader_Result &result
)

{
  RString sections[3];
  sections[0]="D3DXCompileShader";
  sections[1]=pFunctionName;
  sections[2]=pProfile;

  {
    //ScopeLockSection lock (SCurrentShaderFileDescLock);
  if (SCurrentShaderFileDesc.NotNull() && SCurrentShaderFileDesc->IsCacheActive())
  {
      if (SCurrentShaderFileDesc->TestNextSection(Array<RString>(sections,lenof(sections))) && SCurrentShaderFileDesc->ReadFromCache(result.shader.Init()))
    {    
      Log("D3DXCompileShader: Cached %s",pFunctionName);
        *result.errorMsgs.Init()=0;
      return 0;
    }
    else
    {
      LogF("D3DXCompileShader: Cannot read %s from the cache. Cache dropped.",pFunctionName);
      SCurrentShaderFileDesc->DropCache();
      SCurrentShaderFileDesc=0;
    }
  }
  }

  #if SHADER_DBG_LEVEL>0
    // we want to enable debugging in profile / debug configurations
    #if SHADER_DBG_LEVEL>1
      // for extensive debugging we want to disable optimizations as well
      Flags |= D3DXSHADER_SKIPOPTIMIZATION;
    #endif
    Flags |= D3DXSHADER_DEBUG;
  #endif

  int srcDataLen;
  const char *pSrcData = src.Load(srcDataLen);
  HRESULT res=D3DXCompileShader(pSrcData,srcDataLen,pDefines,pInclude,pFunctionName,pProfile,Flags,result.shader.Init(),result.errorMsgs.Init(),NULL);
  if (res!=0 && (Flags&D3DXSHADER_SKIPOPTIMIZATION)!=0)
  {
    // some shaders may fail compiling when disabling optimizations
    // try again with optimizations
    Flags &= ~D3DXSHADER_SKIPOPTIMIZATION;
    res=D3DXCompileShader(pSrcData,srcDataLen,pDefines,pInclude,pFunctionName,pProfile,Flags,result.shader.Init(),result.errorMsgs.Init(),NULL);
  }
  if (res==0)
  {
    //DWORD size = (*ppShader)->GetBufferSize();
    const DWORD *data = (const DWORD *)result.shader->GetBufferPointer();
    //DWORD ssize = D3DXGetShaderSize(data);
    ComRef<ID3DXBuffer> assembly;
    D3DXDisassembleShader(data, false, pFunctionName, assembly.Init());
    const char *src = (const char *)assembly->GetBufferPointer();
    // disassembly ends with something like:
    // approximately 14 instruction slots used (1 texture, 13 arithmetic)
    static const char instrText[]="// approximately ";
    const char *sizeStr = strstr(src,instrText);
    if (sizeStr)
    {
      sizeStr += sizeof(instrText)-1;
      BString<256> size;
      const char *endSize = strrchr(sizeStr,'\n');
      if (endSize)
      {
        strncpy(size,sizeStr,endSize-sizeStr);
        size[endSize-sizeStr] = 0;
      }
      else
      {
        strcpy(size,sizeStr);
      }
      // print instruction count + debug or not
      LogF("D3DXCompileShader %s - %s %s",pFunctionName,cc_cast(size),Flags&D3DXSHADER_SKIPOPTIMIZATION ? " /Od" : "");
    }
    else
    {
      LogF("D3DXCompileShader %s - unable to parse disassembly",pFunctionName);
    }
    
    {
      //ScopeLockSection lock(SCurrentShaderFileDescLock);
    if (SCurrentShaderFileDesc.NotNull() && !SCurrentShaderFileDesc->IsCacheActive())
    {
      SCurrentShaderFileDesc->TestNextSection(Array<RString>(sections,lenof(sections)));
        SCurrentShaderFileDesc->WriteToCache(result.shader);
    }
  }
  }
  else
  {
    LogF("D3DXCompileShader %s failed (%x)",pFunctionName,res);
    if (pDefines)
    {
      LogF("Defines:");
      const D3DXMACRO *p = pDefines;
      while (p->Name != NULL)
      {
        LogF("  #define %s %s", p->Name, p->Definition);
        p++;
      }
    }
  }
  return res;
}

#endif
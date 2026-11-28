#include "../wpch.hpp"

#if _ENABLE_COMPILED_SHADER_CACHE && !defined _SERVER && _ENABLE_DX11

#include "ShaderCompileCacheDX11.h"
#include <El/QStream/qStream.hpp>
#include <El/QStream/QBStream.hpp>
#include <Es/Strings/bString.hpp>
#include "d3d11Defs.hpp"
#include <El/FileServer/fileServer.hpp>

#include <El/crc/crc.hpp>

///Change this value, if you want to invalidate cache from previous version
#define CACHE_ID "0004"

extern bool DisableShaderGeneration;

namespace DX11
{

#if _ENABLE_CHEATS
  /// set this flag to true before compiling specific shader - this shader will be always compiled, even if it is in cache
  bool ShaderCompileCache::ForceCompileShader = false;
#endif

class ShaderCacheFileDescDX11
{
  QOFStream _outcache;
  QIFStreamB _incache;
  RString _sectionName;
  //! Custom path to place the file, temporary folder taken if not specified
  RString _customPath;

  bool _cached;

public:
  //! Destructor
  ~ShaderCacheFileDescDX11()
  {
    _outcache.close();
  }
  void Init(const Array<RString> &sources, const char *sectionName, RStringB customPath, bool forceCreate);

  RString GetCacheFilename();
  static bool PrepareValidTestBlock(const Array<RString> &sources, QOStrStream &testBlock, bool noSources);

  bool ReadFromCache(ID3D10Blob** buffer);
  bool WriteToCache(ID3D10Blob* buffer);
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

static SRef<ShaderCacheFileDescDX11> SCurrentShaderFileDesc;

RString ShaderCacheFileDescDX11::GetCacheFilename()
{
  RStringB path;
  if (_customPath.IsEmpty())
  {
  #ifdef _XBOX
    Fail("Custom path required on Xbox");
  #else
    unsigned long needsz = GetTempPath(0,0);
    char *name = path.CreateBuffer(needsz);
    GetTempPath(needsz, name);
  #endif
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

bool ShaderCacheFileDescDX11::PrepareValidTestBlock(const Array<RString> &sources, QOStrStream &testBlock, bool noSources)
{
  const char *header="BIShaderCache-id: " CACHE_ID;
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
  unsigned long sdkx=11; /*D3DX_VERSION;*/
  testBlock.put(sdkx&0xff);
  testBlock.put((sdkx>>8)&0xff);
  testBlock.put((sdkx>>16)&0xff);
  testBlock.put((sdkx>>24)&0xff);
  unsigned long sdk=D3D11_SDK_VERSION;
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

/*!
  \patch 5151 Date 3/29/2007 by Flyman
  - Fixed: Retail version doesn't try to compile shaders now unless being specifically asked to generate them
*/
void ShaderCacheFileDescDX11::Init(const Array<RString> &sources, const char *sectionName, RStringB customPath, bool forceCreate)
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
  bool sourceFilesExist = PrepareValidTestBlock(sources,testBlock, (retail && !forceCreate) || DisableShaderGeneration);

  _sectionName=sectionName;

  // Remember the cache folder, if on XBOX and sources are present, redirect it to scratch
  _customPath = customPath;
#ifdef _XBOX
  if (sourceFilesExist)
  {
    // check if cache in bin is valid, if not use cache: partition
    RString cachename=GetCacheFilename();
    QIFStreamB incache;
    incache.AutoOpen(cachename);
    bool ok = false;
    if (!incache.fail())
    {
      int size=testBlock.pcount();
      char *cmpmem=(char *)alloca(size);
      int rd=incache.read(cmpmem,size);
      if (rd!=size || memcmp(cmpmem,testBlock.str(),size)!=0)
      {
      }
      else
      {
        ok = true;
      }
    }
    if (!ok)
    {
      // cache not present or invalid - we need to compile shaders into the cache
      _customPath = RString("cache:\\") + _customPath;
    }
  }
#endif

  RString cachename=GetCacheFilename();
  // A forced generation must start a new cache. Reusing an existing cache here
  // can keep shader bytecode from a different executable even when the source
  // test block still matches.
  // DX11 caches are generated as loose files in bin. AutoOpen can select a PBO
  // bank that does not contain this file and, in retail builds, skip the loose
  // file fallback. Use QIFStream::open to read the physical cache directly.
  if (!forceCreate) static_cast<QIFStream&>(_incache).open(cachename);

  if (!sourceFilesExist)
  {
    DoAssert(!_incache.fail());
    int size=testBlock.pcount();
    char *cmpmem=(char *)alloca(size);
    int available=(int)_incache.rest();
    int rd=_incache.read(cmpmem,size);
    if (rd != size)
    {
      _incache.close();
      ErrorMessage("Shaders not valid (mismatch of exe and data?): DX11 section %s, expected %d bytes, read %d, available %d, source count %d",
        sectionName, size, rd, available, sources.Size());
    }
    if (DisableShaderGeneration)
    {
      LogF("Using cache of compiled shaders. Forced to use it by program argument.");
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
  else if (forceCreate || _incache.fail())
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
    if (rd!=size) match = Size;
    else if (memcmp(cmpmem,testBlock.str(),size)!=0) match = Content;

    // ===================================================
#if _ENABLE_CHEATS
    if (Engine::ForceShaderCache)
      match = OK;
#endif
    // ===================================================

    if (match!=OK)
    {
      _cached=false;
      _incache.close();
      if (!_customPath.IsEmpty()) CreatePath(_customPath);
      _outcache.open(cachename);
      _outcache.write(testBlock.str(),testBlock.pcount());
      LogF("Note: Compiled shaders cache not matching (%s, reason %d).",sectionName,match);
    }
    else
    {
      LogF("Using cache of compiled shaders. It will speed-up startup.");      
      _cached=true;
    }
  }
}

bool ShaderCacheFileDescDX11::TestNextSection(const Array<RString> &sectionNames)
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
  SCurrentShaderFileDesc=new ShaderCacheFileDescDX11;
  SCurrentShaderFileDesc->Init(sources, sectionName, customPath, forceCreate);
}


bool ShaderCacheFileDescDX11::ReadFromCache(ID3D10Blob** buffer)
{
  unsigned long sz;
  sz = (unsigned long) _incache.getil();
  if (D3D10CreateBlob(sz,buffer)!=S_OK) return false;
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

bool ShaderCacheFileDescDX11::WriteToCache(ID3D10Blob* buffer)
{
  unsigned long sz = buffer->GetBufferSize();
  unsigned long szData = sz;
#ifdef _XBOX
  SwapEndian(szData); // Write it in Win32 form even on xbox
#endif
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

HRESULT WINAPI D3DX11CompileShader_Cached(
  const LazyLoadFile &src,
  LPCSTR pFileName,
  CONST D3D10_SHADER_MACRO *pDefines,
  LPD3D10INCLUDE pInclude,
  LPCSTR pFunctionName,
  LPCSTR pProfile,
  UINT Flags1,
  UINT Flags2,
  ID3DX11ThreadPump *pPump,
  HRESULT *pHResult,
  D3DXCompileShader_Result &result
  )
{
  RString sections[3];
  sections[0]="D3DX11CompileShader";
  sections[1]=pFunctionName;
  sections[2]=pProfile;

  if (SCurrentShaderFileDesc.NotNull() && SCurrentShaderFileDesc->IsCacheActive())
  {
    if (SCurrentShaderFileDesc->TestNextSection(Array<RString>(sections,lenof(sections))) && SCurrentShaderFileDesc->ReadFromCache(result.shader.Init()))
    {    
      Log("D3DX11CompileShader: Cached %s",pFunctionName);

#if _ENABLE_CHEATS
      if (!ShaderCompileCache::ForceCompileShader)
#endif
      {
        *result.errorMsgs.Init()=0;
        return 0;
      }
    }
    else
    {
      LogF("D3DX11CompileShader: Cannot read %s from the cache. Cache dropped.",pFunctionName);
      SCurrentShaderFileDesc->DropCache();
      SCurrentShaderFileDesc=0;
    }
  }

  #if SHADER_DBG_LEVEL>0
    // we want to enable debugging in profile / debug configurations
    #if SHADER_DBG_LEVEL>1
      // for extensive debugging we want to disable optimizations as well
      Flags1 |= D3D10_SHADER_SKIP_OPTIMIZATION;
    #endif
    Flags1 |= D3D10_SHADER_DEBUG;
  #else
    Flags1 |= D3D10_SHADER_OPTIMIZATION_LEVEL3;
  #endif

  int srcDataLen;
  const char *pSrcData = src.Load(srcDataLen);

  HRESULT res=D3DX11CompileFromMemory(
    pSrcData,
    srcDataLen,
    pFileName,
    pDefines,
    pInclude,
    pFunctionName,
    pProfile,
    Flags1,
    Flags2,
    pPump,
    result.shader.Init(),
    result.errorMsgs.Init(),
    pHResult
    );

  if (res!=0 && (Flags1&D3D10_SHADER_SKIP_OPTIMIZATION)!=0)
  {
    // some shaders may fail compiling when disabling optimizations
    // try again with optimizations
    Flags1 &= ~D3D10_SHADER_SKIP_OPTIMIZATION;
    res=D3DX11CompileFromMemory(
      pSrcData,
      srcDataLen,
      pFileName,
      pDefines,
      pInclude,
      pFunctionName,
      pProfile,
      Flags1,
      Flags2,
      pPump,
      result.shader.Init(),
      result.errorMsgs.Init(),
      pHResult
      );
  }
  if (res==0)
  {
    #ifndef _XBOX
    const DWORD *data = (const DWORD *)result.shader->GetBufferPointer();
    ComRef<ID3D10Blob> assembly;
    D3D10DisassembleShader(data, result.shader->GetBufferSize(), false, pFunctionName, assembly.Init());
    const char *src = (const char *)assembly->GetBufferPointer();
    // disassembly ends with something like:
    // approximately 14 instruction slots used (1 texture, 13 arithmetic)
    static const char instrText[]="// Approximately ";
    const char *sizeStr = strstr(src,instrText);
    if (sizeStr)
    {
      sizeStr += sizeof(instrText)-1;
      BString<256> size;
      const char *endSize = strrchr(sizeStr,'\n');
      if (endSize)
      {
        strncpy(size,sizeStr,endSize-sizeStr);
        size[static_cast<int>(endSize-sizeStr)] = 0;
      }
      else
      {
        strcpy(size,sizeStr);
      }
      // print instruction count + debug or not
      LogF("D3DX11CompileShader %s - %s %s",pFunctionName,cc_cast(size),Flags1&D3D10_SHADER_SKIP_OPTIMIZATION ? " /Od" : "");
    }
    else
    {
      LogF("D3DX11CompileShader %s - unable to parse disassembly",pFunctionName);
    }
    #else
    // TODOX360: use D3DXDisassembleShaderEx with D3DXDISASSEMBLER_SHOW_TIMING_ESTIMATE
    #endif

    if (SCurrentShaderFileDesc.NotNull() && !SCurrentShaderFileDesc->IsCacheActive())
    {
      SCurrentShaderFileDesc->TestNextSection(Array<RString>(sections,lenof(sections)));
      SCurrentShaderFileDesc->WriteToCache(result.shader);
    }
  }
  else
  {
    LogF("D3DX11CompileShader %s failed (%x)",pFunctionName,res);
    if (pDefines)
    {
      LogF("Defines:");
      CONST D3D10_SHADER_MACRO *p = pDefines;
      while (p->Name != NULL)
      {
        LogF("%s - %s", p->Name, p->Definition);
        p++;
      }
    }
  }
  return res;
}
} //namespace DX11
#endif //_ENABLE_COMPILED_SHADER_CACHE && !defined _SERVER && _ENABLE_DX11


#ifdef _MSC_VER
#pragma once
#endif

#ifndef _TXTD3D11_HPP
#define _TXTD3D11_HPP

#if _ENABLE_DX11
#if !_DISABLE_GUI

#include "../textbank.hpp"
#include "../d3dCommon/d3dCommon.hpp"
#include "d3d11Defs.hpp"
#include <Es/Framework/appFrame.hpp>

class TimeManager;

namespace DX11
{

class EngineDD11;
class TextureD3D11;
class MyD3DDevice;
class MyD3DDeviceCB;

/// item describing what element is held and can be evicted from the cache
class HMipCacheD3D11: public LinkBidirWithFrameStore
{
public:
  /// which texture
  TextureD3D11 *texture;
  /// which mipmap level
  /** Several mipmaps may be stored from the same texture */
  int level;

  size_t GetMemoryControlled() const;

#if _ENABLE_REPORT
  ~HMipCacheD3D11()
  {
    // destructor must never remove from the list - that would break thread safety
    DoAssert(!IsInList());
    DoAssert(GetStoredMemoryControlled()==0);
  }
#endif

  USE_FAST_ALLOCATOR;
};

#include <Es/Memory/debugNew.hpp>

template <class Type>
struct FramedMemoryControlledListTraitsSRef
{
  static void AddRef(Type *item) {}
  static void Release(Type *item){delete item;}
};

} //namespace

// must be outside namespace
template <>
struct FramedMemoryControlledListTraits<DX11::HMipCacheD3D11>: public DX11::FramedMemoryControlledListTraitsSRef<DX11::HMipCacheD3D11>
{
};

namespace DX11 
{

/**
track texture mipmaps suitable for discarding
*/
class D3DMipCacheRoot11: public FramedMemoryControlledList<HMipCacheD3D11>
{
  typedef FramedMemoryControlledList<HMipCacheD3D11> base;

};

/** 
We do not provide any real memory to MemoryFreeOnDemandHelper, but we need to be connect to it
so that it calls our Frame function 
*/
class D3DMipCacheRoot11FreeOnDemand: public MemoryFreeOnDemandHelper
{
  D3DMipCacheRoot11 &_list;

public:
  D3DMipCacheRoot11FreeOnDemand(D3DMipCacheRoot11 &list):_list(list){}

  //@{ MemoryFreeOnDemandHelper implementation
  virtual size_t FreeOneItem();
  virtual float Priority() const;
  virtual size_t MemoryControlled() const;
  virtual size_t MemoryControlledRecent() const;
  virtual RString GetDebugName() const;
  virtual void MemoryControlledFrame();
  //@}
};

//! Structure to hold brief texture information
struct TEXTUREDESC11
{
  TEXTUREDESC11()
    :w(0), h(0), nMipmaps(0), format(DXGI_FORMAT_UNKNOWN), usage(D3D11_USAGE_DEFAULT), isSRGB(false)
#if _HELISIM_RENDER_TO_TEXTURE
    , useAsRT(false) 
#endif
  {}

  //! Width and height of the texture
  int w,h;
  //! Number of mipmaps
  int nMipmaps;
  //! Texture format
  DXGI_FORMAT format;
  //! usage 
  D3D11_USAGE usage;
  //! if tex. is srgb
  bool isSRGB;
#if _HELISIM_RENDER_TO_TEXTURE
  //! if texture can be used as render target
  bool useAsRT;
#endif
};

//! Texture and associated resource view
struct SurfaceDataD3D11
{
  const ComRef<ID3D11ShaderResourceView>& GetResourceView(bool srgb) const
  {
    if (srgb && _resViewSRGB.NotNull())
    {
      return _resViewSRGB;
    }
    else
    {
      return _resView;
    }
  }

  //! Texture data
  ComRef<ID3D11Texture2D> _texture;
  ComRef<ID3D11Texture3D> _texture3D;
  //! Non-srgb Texture view
  ComRef<ID3D11ShaderResourceView> _resView;
  //! SRGB texture view (may not be available)
  ComRef<ID3D11ShaderResourceView> _resViewSRGB;
  //! Render target view (used only for R2T)
  ComRef<ID3D11RenderTargetView> _rtView;

  void Free()
  {
    _resView.Free();
    _resViewSRGB.Free();
    _rtView.Free();
    _texture.Free();
    _texture3D.Free();
  }

  bool IsNull() const
  {
    return _texture.IsNull() && _texture3D.IsNull() && _resView.IsNull() && _resViewSRGB.IsNull() && _rtView.IsNull();
  }
};


/// information about a surface allocated with D3D
class SurfaceInfoD3D11
{
private:
  //! Surface data (texture and view)
  SurfaceDataD3D11 _surface;

  static int _nextId;
  int _id;

public:
  int _totalSize; // expected size for allocation
  int _usedSize; // actually allocated size
  int _w,_h;
  int _nMipmaps;
  DXGI_FORMAT _d3dFormat;
  bool _isSRGB;

#if _HELISIM_RENDER_TO_TEXTURE
  bool _isR2T;
#endif

  int SizeExpected() const {return _totalSize;}
  int SizeUsed() const {return _usedSize;}
  int Slack() const {return _usedSize-_totalSize;}

  int GetCreationID() const {return _id;}

#if TRACK_TEXTURES
  SurfaceInfoD3D11(){}
  SurfaceInfoD3D11(const SurfaceInfoD3D11 &src);
  void operator = (const SurfaceInfoD3D11 &src);
#endif

  static int CalculateSize(
    MyD3DDeviceCB &dDraw, const TEXTUREDESC11 &desc, PacFormat format,
    int totalSize=-1
    );
  HRESULT CreateSurface(
    EngineDD11 *engine, MyD3DDeviceCB &dDraw, const TEXTUREDESC11 &desc, PacFormat format,
    int totalSize=-1
    );
  void Free(const char *name, bool lastRef, int refValue=0 );
  // TODO: use ComRef more consistently
  // add AddRef to ComRef contructor?
  // or use Ref for COM types
  SurfaceDataD3D11 &GetSurface() {return _surface;}
  const SurfaceDataD3D11 &GetSurface() const {return _surface;}
  
  TextureHandle11* GetResourceView(bool srgb) const 
  {
    srgb = srgb && _isSRGB; 
    return _surface.GetResourceView(srgb);
  }

  bool IsNull() const {return _surface.IsNull();}
};


struct D3DSurfaceToDestroy11
{
  int _id;
  SurfaceDataD3D11 _surface;
};


/* on PC there is no way to copy smaller mip-maps of the texture, therefore we keep separate chains for smaller mip-maps */
/* TODO:
But it would be possible to employ a different scheme. Smaller mipmaps could be loaded only in a system memory,
as one chain of separate surfaces. Whenever we would be downgrading, we would copy the sysmem into VIDMEM and
discard the biggest surface.

This would mean somewhat larger SYSMEM consumption, reduced VIDMEM, and reduced overall usage.
*/
# define SMALL_MIPS_READY 1

class D3DSurfaceDestroyer11
{
  // manage order of batch of destroyed surfaces
  // DX memory manager releases surfaces much faster
  // when released in reverse order of creation
  AutoArray<D3DSurfaceToDestroy11> _surfaces;
public:
  D3DSurfaceDestroyer11();
  ~D3DSurfaceDestroyer11();
  void Add( const SurfaceInfoD3D11 &surf );
  void DestroyAll();
};

#include <Es/Memory/normalNew.hpp>

#define ASSERT_INIT() Assert(_initialized);

#define MAX_MIPMAPS 12

/// class to build a list of textures to be discarded
class TextureLRUItem : public TLinkBidir<4>
{
};

struct HeapTraitsLoadingJob
{
  typedef LLink<TextureD3D11> Type;
  static bool IsLess(const Type &a, const Type &b);
};

//! single texture (D3D)
class TextureD3D11: public Texture 
{
  typedef Texture base;

  friend class TextBankD3D11;
  friend class EngineDD11;
  friend class HMipCacheD3D11;
  friend struct HeapTraitsLoadingJob;

private:
  SRef<ITextureSource> _src; //!< texture source provider

  bool _initialized;
  bool _permanent;
  bool _initializeRequested;

  /// performance requirements (priority)
  int _performance;

  //bool _isPaa;
  int _maxSize;
  /// landscape textures have different limit than object...
  UsageType _usageType;

  // this information is same for all mipmaps, but is also copied in mipmap header
  int _nMipmaps;
  //! List of mipmaps (the first one with index 0 is the biggest one)
  PacLevelMem _mipmaps[MAX_MIPMAPS];
  /// video memory copy
  SurfaceInfoD3D11 _surfaces[MAX_MIPMAPS]; 

  class LoadLevelsJob;
  /// only only loading job may be active for a single texture
  SRef<LoadLevelsJob> _loadingJob;

#ifdef TRAP_FOR_0x8000
#if 1
  int _dummyInts[256];
#endif
#endif

  /// is given level present in the cache?
  InitPtr<HMipCacheD3D11> _surfCache[MAX_MIPMAPS];

  /// First level which is small enough to be used
  signed char _largestUsed;
  /// First level which is small enough to be considered atomic (either we have at least this level, or the texture is not loaded at all)
  signed char _smallLevel;
  /// The finest level loaded
  signed char _levelLoaded;
  /// Level which is marked in the cache as active and can be safely used for rendering
  /** note: this is not updated when texture is not used, and still points to the last assigned value */
  signed char _levelActive;
  /// Is currently used for background operation - do not release
  signed char _inUse;
  /// levelNeeded already included mipbias
  float _levelNeededThisFrame;
  float _levelNeededLastFrame;


  TextureHandle11* GetBigSurface(bool srgb) const
  {
#if SMALL_MIPS_READY
    // we should use only a mipmap which is known to be registered as used
    // otherwise we risk stall on release of the fine mipmap
    Assert(_levelActive>=_levelLoaded);
    if (_levelActive >= _nMipmaps) return NULL;
    Assert(_surfaces[_levelActive].GetResourceView(srgb));
    return _surfaces[_levelActive].GetResourceView(srgb);
#else
    // when storing only one mipmap we have no choice and need to use the one we have
    // TODO: solve possible GPU stalls
    Assert(_levelActive>=_levelLoaded);
    if (_levelActive >= _nMipmaps || _levelLoaded>=_nMipmaps) return NULL;
    Assert(_surfaces[_levelActive].GetSurface()._resView);
    return _surfaces[_levelActive].GetSurface()._resView;
#endif
  }

  SurfaceInfoD3D11* GetSurface() 
  {
#if SMALL_MIPS_READY
    // we should use only a mipmap which is known to be registered as used
    // otherwise we risk stall on release of the fine mipmap
    Assert(_levelActive>=_levelLoaded);
    if (_levelActive >= _nMipmaps) return NULL;
    return &_surfaces[_levelActive];
#else
    // when storing only one mipmap we have no choice and need to use the one we have
    // TODO: solve possible GPU stalls
    Assert(_levelActive>=_levelLoaded);
    if (_levelActive >= _nMipmaps || _levelLoaded>=_nMipmaps) return NULL;
    return &_surfaces[_levelActive].GetSurface();
#endif
  }

  float LevelNeeded() const
  {
    return
      (
      _levelNeededThisFrame<_levelNeededLastFrame 
      ? _levelNeededThisFrame : _levelNeededLastFrame
      );
  }

public:
  TextureD3D11();
  ~TextureD3D11();

  bool IgnoreLoadedLevels() const { return (_src!=NULL)  && _src->IgnoreLoadedLevels();  };
  OggDecoder* getDecoder() const {  return ( (_src!=NULL)  ? _src->getDecoder() : NULL ) ;  };

  void OnUnused() const;

  /// Returns description of the texture acquired from the level "levelMin"
  /*!
  \param ddsd Texture description to return
  \param levelMin level to acquire texture from
  \param enableDXT Flag to determine DXT2-5 can be used
  */
  void InitDDSD(TEXTUREDESC11 &ddsd, int levelMin, bool enableDXT);
#if !defined(_XBOX) && !defined(_X360SHADERGEN)
  /// copy texture from SYSMEM to DEFAULT
  static void CopyToVRAM(SurfaceInfoD3D11 &surface, int sysLevel, const SurfaceInfoD3D11 &sys);
#endif
  /// discard levels that are not needed, reuse original surface
  bool ReduceTexture(SurfaceInfoD3D11 &surf, int mipImprovement);

  bool ReduceTextureLevel(int tgtLevel);
  bool ReduceTextureLevel(SurfaceInfoD3D11 &surf, int needed, int mipImprovement);

  int LoadLevels(int levelMin, bool ignoreLoadedLevels = false); // load to VRAM

  /// request texture loading, return true if texture is ready
  bool RequestLevelsSys(int levelMin, int priority);
  /// request texture loading, return true if texture is ready
  bool RequestLevels(int levelMin, int priority);
  /// Returns permanent default texture - will always return non NULL value
  TextureD3D11 *GetDefaultTexture() const;

  __forceinline TextureHandle11* GetHandle(bool srgb) const {return GetBigSurface(srgb);}

private:

  void MemoryReleased(int from, int to); // mark as released

  int TotalSize( int levelMin ) const;

public:

  /// verify internal object state is OK
  bool CheckIntegrity() const;

  int TotalVRAMUsed() const {return TotalSize(_levelLoaded);}

  int ReleaseMemory( SurfaceInfoD3D11 &surf, bool store=false, D3DSurfaceDestroyer11 *batch=NULL ); // move to reusable

  int ReleaseSystem(SurfaceInfoD3D11 &surface, bool store=false, D3DSurfaceDestroyer11 *batch=NULL); // move to reusable
  int ReleaseMemory( bool store=false, D3DSurfaceDestroyer11 *batch=NULL ); // move to reusable
  int ReleaseLevelMemory( int from, int to, bool store=false, D3DSurfaceDestroyer11 *batch=NULL ); // move to reusable
  void ReuseMemory( SurfaceInfoD3D11 &surf ); // reuse immediatelly

  virtual PacFormat GetFormat() const {return _src ? _src->GetFormat() : PacUnknown;}
  virtual bool IsAlpha() const {ASSERT_INIT();return _src && _src->IsAlpha();}
  virtual bool IsAlphaNonOpaque() const {ASSERT_INIT();return _src && _src->IsAlphaNonOpaque();}
  virtual bool IsTransparent() const {ASSERT_INIT();return _src && _src->IsTransparent();}
  QFileTime GetTimestamp() const {ASSERT_INIT();return _src ? _src->GetTimestamp() : 0;}

  virtual int GetClamp() const {return _src ? _src->GetClamp() : 0;}
  virtual void SetClamp(int clampFlags);

  /// adjust max. size based on usage type
  void AdjustMaxSize();
  void SetUsageType( UsageType type );
  void SetNMipmaps( int n);

  bool VerifyChecksum( const MipInfo &mip ) const;

  virtual TextureType Type() const;

  virtual TexFilter GetMaxFilter() const;

  //int Load( int level );

  void SetMipmapRange( int min, int max );

  /// do as little preparation as possible
  int Init(const char *name, bool caseSensitive = false);

  /// initialize based on known source
  int Init(ITextureSource *source);

private:
  /// initialize based on source (_src)
  void DoInitSource();

  //! do real preparation - is called before any real data is used
  void DoLoadHeaders(bool canIgnore=true);

  /// advertise the resource is ready
  void MarkInitialized()
  {
    // before we advertise the texture is initialized, make sure other threads can really see it
    MemoryPublish();
    _initialized = true;
    _initializeRequested = true;
  }


public:
  /// non-virtual implementation of HeadersReady to allow inlining
  bool HeadersReadyFast() const
  {
    bool ret = _initialized;
    // make sure all other writes really finish before we start reading
    MemorySubscribe();
    return ret;
  }
  /// check if headers are ready
  virtual bool HeadersReady() const
  {
    return HeadersReadyFast();
  }
  //! ask file server to preload headers from file
  virtual void PreloadHeaders();

  virtual void RequestDone(RequestContext *context, RequestResult result);
  virtual int ProcessingThreadId() const;

#if DEBUG_PRELOADING
  virtual bool DebugMe() const;
#endif

  //! undo what was done by DoLoadHeaders or any actual loading
  virtual void DoUnloadHeaders();

  //! load header if necessary (non-virtual)
  void LoadHeadersNV() const
  {
    if (HeadersReadyFast()) return;
    const_cast<TextureD3D11 *>(this)->DoLoadHeaders();
  }

  void LoadHeadersServantCallback() const
  {
    const_cast<TextureD3D11 *>(this)->DoLoadHeaders(false);
  }

  void LoadHeaders();

  void CheckForChangedFile();
  void FlushHandles();

  const PacLevelMem *Mipmap( int level ) const
  {
    ASSERT_INIT();
    Assert(level < MAX_MIPMAPS);
    return &_mipmaps[level];
  }

  const AbstractMipmapLevel &AMipmap( int level ) const
  {
    ASSERT_INIT();
    Assert(level < MAX_MIPMAPS);
    return _mipmaps[level];
  }
  AbstractMipmapLevel &AMipmap( int level )
  {
    ASSERT_INIT();
    Assert(level < MAX_MIPMAPS);
    return _mipmaps[level];
  }

  int NMipmaps() const
  {
    ASSERT_INIT();
    return _nMipmaps;
  }
  //const PacPalette &GetPalette() const {return _pal;}

  int ANMipmaps() const {ASSERT_INIT();return _nMipmaps;}
  int BestMipmap() const {ASSERT_INIT();return _largestUsed;}
  void ASetNMipmaps( int n ); // {_nMipmaps=n;}
  int AArea( int level=0)const {LoadHeadersNV();Assert(level < MAX_MIPMAPS);return _mipmaps[level]._w*_mipmaps[level]._h;}
  int AWidth( int level=0 ) const {LoadHeadersNV();Assert(level < MAX_MIPMAPS);return _mipmaps[level]._w;}
  int AHeight( int level=0 ) const {LoadHeadersNV();Assert(level < MAX_MIPMAPS);return _mipmaps[level]._h;}

  virtual Color GetPixel( int level, float u, float v ) const;
  virtual bool IsDynRangeCompressed() const {return _src && _src->GetMaxColor()!=PackedWhite;}
  virtual bool GetRect(
    int level, int x, int y, int w, int h, PacFormat format, void *data
    ) const;
  virtual bool RequestGetRect(int level, int x, int y, int w, int h) const;

  /// update _loadedTexDetail based on _levelLoaded
  void UpdateLoadedTexDetail(int levelLoaded);

  void UpdateMaxTexDetail();

  int LoadingJobFrame() const;


  /// 0 = default performance, 100, 200 = increased performance
  void SetTexturePerformance(int performance);

  Color GetColor() const;
  Color GetMaxColor() const;

  int Width( int level ) const {ASSERT_INIT();Assert(level < MAX_MIPMAPS);return _mipmaps[level]._w;}
  int Height( int level ) const {ASSERT_INIT();Assert(level < MAX_MIPMAPS);return _mipmaps[level]._h;}

  void CacheUse(TextBankD3D11 &bank, int level, int active);

  __forceinline void AddUse() {_inUse++;}
  __forceinline void ReleaseUse() {_inUse--;}

#ifdef TRAP_FOR_0x8000
  virtual bool TrapFor0x8000(const char *str) const;
#endif

  USE_FAST_ALLOCATOR
};

//! object that can be used to scope lock textures
class ScopeUseTexture11
{
  TextureD3D11 *_inUse;

public:
  //! construct - enter lock
  ScopeUseTexture11(TextureD3D11 *texture)
  { 
    if (texture)
    {
      _inUse = texture;
      _inUse->AddUse();
    }
    else
    {
      _inUse = NULL;
    }
  }
  //! destruct - leave lock
  ~ScopeUseTexture11()
  {
    if (_inUse) _inUse->ReleaseUse();
  }
  //! copy - duplicate lock
  ScopeUseTexture11(const ScopeUseTexture11 &src)
    :_inUse(src._inUse)
  {
    if (_inUse) _inUse->AddUse();
  }
  //! no assignement possible
  void operator =(const ScopeUseTexture11 &src)
  {
    if (src._inUse) src._inUse->AddUse();
    if (_inUse) _inUse->ReleaseUse();
    _inUse = src._inUse;
  }
};

//! object that can be used to scope lock textures
class ScopeInUse11
{
  signed char &_inUse;

public:
  //! construct - enter lock
  ScopeInUse11(signed char &inUse)
    :_inUse(inUse)
  {
    _inUse++;
  }
  //! destruct - leave lock
  ~ScopeInUse11()
  {
    _inUse--;
  }
  //! copy - duplicate lock
  ScopeInUse11(const ScopeInUse11 &src)
    :_inUse(src._inUse)
  {
    _inUse++;
  }
private:
  //! no assignement possible
  void operator =(const ScopeInUse11 &src);
};

#include <Es/Memory/debugNew.hpp>

#include <El/FreeOnDemand/memFreeReq.hpp>

} //namespace

// must be outside namespace
template <>
struct MapClassTraits< LLink<DX11::TextureD3D11> >: public DefMapClassTraits< LLink<DX11::TextureD3D11> >
{
  /// key type
  typedef RStringB KeyType;
  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    return CalculateStringHashValue(key);
  }

  /// compare keys, return zero when equal, non-zero otherwise (this is all hashmap needs)
  static int CmpKey(KeyType k1, KeyType k2)
  {
    return k1!=k2;
  }

  static int CoefExpand() {return DefCoefExpand;}

  /// get a key for given a item
  static KeyType GetKey(const LLink<DX11::TextureD3D11> &item)
  {
    if (item.IsNull()) return KeyType();
    return item->GetName();
  }

  struct DeleteNullTexture
  {
    bool operator () (const LLink<DX11::TextureD3D11> &tex) const
    {
      return tex.IsNull();
    }
  };

  /// cleanup zombies - handle NULL links
  static int CleanUp(LLinkArray<DX11::TextureD3D11> &array)
  {
    int oldCount = array.Size();
    // remove any targets with idExact = NULL
    array.ForEachWithDelete(DeleteNullTexture());
    //Assert(oldCount== array.Size());
    // return how many items did we remove
    return oldCount- array.Size();
  }
};

namespace DX11
{

//! texture manager (D3D)
class TextBankD3D11: public RefCount, public AbstractTextBank
{
  typedef AbstractTextBank base;

  friend class TextureD3D11;

  int _maxTextureMemory; // initial memory free for textures
  /// common limit for textures + vertex buffers
  int _limitAlocatedVRAM;
  /// we may know from experience some VRAM must be left
  int _reserveTextureMemory;
  /// updated every time RecalculateTextureMemoryLimits is called
  int _freeTextureMemory; 

  /// what is the minimal reasonable texture size
  int _minTextureSize;

  typedef MapStringToClass< LLink<TextureD3D11>, LLinkArray<TextureD3D11> > TextureList;
  TextureList _texture;
  Ref<TextureD3D11> _white; ///< required for pixel shader - NULL texture is black
  Ref<TextureD3D11> _zero; ///< used as default for mask
  Ref<TextureD3D11> _black;  // required for Ti pixel shader when no Ti texture is defined
  Ref<TextureD3D11> _random; ///< small texture with random numbers in alpha
  Ref<TextureD3D11> _defNormMap; ///< default normal map
  Ref<TextureD3D11> _defDetailMap; ///< default detail map
  Ref<TextureD3D11> _defMacroMap; ///< default macro map
  Ref<TextureD3D11> _defSpecularMap; ///< default specular map
  Ref<TextureD3D11> _defDTSpecularMap; ///< default detail specular map
  RefArray<TextureD3D11> _permanent; ///< texture loaded permanently

  CriticalSection _loadingJobsCS;
  HeapArray<LLink<TextureD3D11>, MemAllocD, HeapTraitsLoadingJob >_loadingJobs;

#if _ENABLE_CHEATS
  struct StressInfo
  {
    ComRef<ID3D10Texture2D> _tex;
    int _size;

    StressInfo(){}
    StressInfo(const ComRef<ID3D10Texture2D> &tex, int size)
      :_tex(tex),_size(size)
    {
    }

    ClassIsMovableZeroed(StressInfo)
  };
  /// memory stress testing - surfaces doing nothing but eating the memory
  AutoArray<StressInfo> _stressTest;
  /// track total size of stress test surfaces
  int _stressTestTotalSize;
  /// stress testing can continuously create and destroy random sized textures
  /**
  Negative value means we prefer destroying textures
  Positive value means we prefer creating textures
  The bigger number, the bigger the change per frame is
  */
  int _texStressMode;

  /// virtual memory stress test
  AutoArray<void *> _stressTestVM;
#endif


  int _limitSystemTextures;
  // system texture bank
  AutoArray<SurfaceInfoD3D11> _freeSysTextures; // free system surfaces

  AutoArray<SurfaceInfoD3D11> _freeTextures; // free VRAM surfaces
  int _freeAllocated; //!< allocated in free vidmem heap (_freeTextures)
  int _totalAllocated; //!< allocated in vidmem heap (_freeTextures and _texture)
  int _totalSlack; //!< slack in vidmem heap (_freeTextures and _texture)
  /// total amout of VRAM freed in this frame
  /** used in debugging to detect suspicious extensive freeing */
  int _freeVRAMTurnOver;

  /// is FinishFrame still expected to be called?
  bool _frameNotFinishedYet;

  EngineDD11 *_engine;
  int _systemAllocated; //!< allocated in system heap (_freeSysTextures and _texture)

  /// LRU list of mipmaps to be discarded  
  D3DMipCacheRoot11 _used;

  D3DMipCacheRoot11FreeOnDemand _freeOnDemand;

  /// keep information about what texture is to be discarded

  /*
  TListBidir<TextureD3D11,TextureLRUItem> _recentlyUsed;
  */

  int _thisFrameAlloc; // VRAM allocations/deallocations (count)

  /// currently used texture quality  
  int _textureQuality;
  /// max. texture quality as currently possible based on video card capabilities
  int _maxTextureQuality;

  /// currently used texture memory  
  int _textureVRAM;
  /// max. texture quality as currently possible based on video card capabilities
  int _maxTextureVRAM;

  /// protect texture memory allocation
  mutable CriticalSection _textureMemoryCS;
  /// ID of thread currently holding _textureMemoryCS
  mutable int _textureMemoryThread;
  /// ID of the thread used for texture management
  int _textureThread;



  TimeManager &GetTexTimeManager();

  /// check for changed files at the next frame was requested
  bool _checkForChangedFiles;
  bool _flushHandles;

  static const int _textureLimit[];
  static const float _textureMipBiasQuality[];
  static const int _textureMaxSize[][Texture::NUsageType];
public:
  //! Constructor
  /*!
  \param engine The engine
  \param cfg User parameters
  \param fcfg Flashpoint cfg
  */
  TextBankD3D11(EngineDD11 *engine, ParamEntryPar cfg, ParamEntryPar fcfg);
  ~TextBankD3D11();

  /// free texture memory, considering user settings
  int FreeTextureMemory();
  /// free texture memory as reported by the device driver
  int FreeTextureMemoryReported();
  /// free memory as reported by DDraw7 interfaces
  void FreeTextureMemoryReportedByDDraw(int &local, int &nonlocal);

  void ResetMaxTextureQuality(bool complete);

  //! get statistics for display / debugging purposed
#if _ENABLE_CHEATS
  virtual RString GetStat(int statId, RString &statVal, RString &statVal2);
#endif

  void RecalculateTextureMemoryLimits();

private:

  void MakePermanent(Texture *tex);

  void PreCreateVRAM( int reserve, bool randomOrder ); // pre-create surfaces
  void PreCreateSys(); // pre-create surfaces

  void PreCreateSurfaces();

public:
  class StoreThreadId: private ::NoCopy
  {
    TextBankD3D11 &_bank;
#if _ENABLE_REPORT
    int _prevVal;
#endif

  public:
    StoreThreadId(TextBankD3D11 &bank):_bank(bank)
    {
      PROFILE_SCOPE_EX(txCSL,tex);
      bank._textureMemoryCS.Lock();
#if _ENABLE_REPORT
      _prevVal = bank._textureMemoryThread;
      bank._textureMemoryThread = GetCurrentThreadId();
#endif
    }
    ~StoreThreadId()
    {
#if _ENABLE_REPORT
      _bank._textureMemoryThread = _prevVal;
#endif
      _bank._textureMemoryCS.Unlock();
    }
  };
  bool CheckTextureMemoryAccess() const {return _textureMemoryThread==GetCurrentThreadId();}

  void InitDetailTextures();
  void DoneDetailTextures();

  void Compact();

  void ReloadAll(); // texture cache was destroyed - reload

  //@{ texture memory stress testing
  void ClearTexStress();
  void AddTexStress(int size, bool renderTarget=false);
  void SetStressMode(int mode);
  void StressTestFrame();
  //@}

  //@{ virtual memory stress testing
  void ClearVMStress();
  bool AddVMStress(int size);
  //@}

  void GetTextureList(LLinkArray<Texture> &list);
  void Preload(); // initialize VRAM
  virtual bool FlushTexture(const char *name);
  virtual Ref<Texture> SuspendTexture(const char *name);
  virtual void ResumeTexture(Texture *tex);

  int MaxTextureSize(Texture::UsageType type);

  RString GetTextureDebugName(ID3D11Resource* handle) const;

  virtual int GetMaxTextureQuality() const;
  virtual int GetTextureQuality() const {return _textureQuality;}
  virtual void SetTextureQuality(int value);

  static int SelectTextureMemory(int vram);

  virtual int GetMaxTextureMemory() const;
  virtual int GetTextureMemory() const;
  virtual void SetTextureMemory(int value);
  virtual void CheckForChangedFiles();

  /// debugging  - explicit control over limits
  void SetTextureMemoryDiag(int value);

  void FlushHandles();

  void DoCheckForChangedFiles();
  void DoFlushHandles();

  void FlushTextures(bool deep, bool allowAutoDeep); // switch data - flush and preload
  bool CheckDeepFlushNeeded() const;
  void FlushBank(QFBank *bank);

protected:
  LLink<TextureD3D11> Find(RStringB name1) const;

  int FindSurface(
    int w, int h, int nMipmaps, DXGI_FORMAT format,
    const AutoArray<SurfaceInfoD3D11> &array
    ) const;

  // find, create if not found
  int FindSystem(int w, int h, int nMipmaps, DXGI_FORMAT format) const;
  void UseSystem(SurfaceInfoD3D11 &surf, const TEXTUREDESC11 &ddsd, PacFormat format, int mipImprovement);
  void AddSystem( SurfaceInfoD3D11 &surf );
  int DeleteLastSystem( int need, D3DSurfaceDestroyer11 *batch=NULL );

  int FindReleased(int w, int h, int nMipmaps, DXGI_FORMAT format) const
  {
    return FindSurface(w,h,nMipmaps,format,_freeTextures);
  }
  void AddReleased(SurfaceInfoD3D11 &surf);
  void UseReleased(SurfaceInfoD3D11 &surf, const TEXTUREDESC11 &ddsd);
  int DeleteLastReleased(D3DSurfaceDestroyer11 *batch=NULL);

  bool Reuse(
    SurfaceInfoD3D11 &surf, const TEXTUREDESC11 &ddsd, PacFormat format,
    bool enableReduction, int mipImprovement
    );

public:
  Ref<Texture> CreateTexture(ITextureSource *source);

  Ref<Texture> Load(RStringB name, bool caseSensitive = false);

  void StopAll(); // stop all background activity (before shutdown)

  void RegisterVisualizeThread(int threadId);
  bool UsingVisualizeThread() const {return _textureThread!=0 && !::CheckMainThread(_textureThread);}

  void StartFrame();

  void PerformAsyncMaintenance();

  void FinishFrame();
  bool FrameNotFinishedYet() const {return _frameNotFinishedYet;};
  void PerformMipmapLoading();

  /// use some of the loaded mipmaps, never load a new one
  MipInfo UseMipmapLoaded(Texture *tex);
  /// make sure given mipmaps are loaded
  MipInfo UseMipmap(Texture *texture, int level, int levelTop, int priority=TexPriorityNormal);
  TextureD3D11 *GetWhiteTexture() const {return _white;}
  TextureD3D11 *GetBlackTexture() const {return _black;}
  TextureD3D11 *GetRandomTexture() const {return _random;}
  TextureD3D11 *GetDefNormalMapTexture() const {return _defNormMap;}
  TextureD3D11 *GetDefDetailTexture() const {return _defDetailMap;}
  TextureD3D11 *GetDefMacroTexture() const {return _defMacroMap;}
  TextureD3D11 *GetDefSpecularTexture() const {return _defSpecularMap;}
  TextureD3D11 *GetDefDTSpecularTexture() const {return _defDTSpecularMap;}
  TextureD3D11 *GetDefMaskTexture() const {return _zero;}

  bool VerifyChecksums();

#if !SMALL_MIPS_READY
  /**
  when we are not really releasing, we can only limit what memory is used in other than free textures
  otherwise we would release all textures to get into the limit
  */
  int TotalMemoryToLimit() const {return _totalAllocated-_freeAllocated;}
#else
  int TotalMemoryToLimit() const {return _totalAllocated;}
#endif

  /// handle freeing of _freeTextures
  void HandleRegularCleanup(int limit);
  int ReserveMemoryLimit(int limit, int minAge);
  float VRAMDiscardMetrics() const;
  int VRAMDiscardCountAge(int minAge, int *totalSize=NULL) const;

  //! Change levelNeeded parameters in all textures of the cache specified
  /*!
  \param root Cache containing textures to change
  \param change Value to change the needed parameters by
  */
  void ChangeMipBias(D3DMipCacheRoot11 &root, float change);
  //! Find texture with the biggest difference between loaded and needed
  /*!
  \param root Cache containing textures to find in
  \param maxBias Returned maxBias value (the biggest difference)
  \param maxTex Returns texture associated with the maxBias value
  \return The same value as maxBias
  */
  float FindWorstBias(D3DMipCacheRoot11 &root, float &maxBias, TextureD3D11 *&maxTex) const;
  //! Limit memory allocated by decreasing mip bias
  /*!
  This function is rather drastic measure.
  Regular maintenance should keep mipbias so that this function is almost never needed.
  */
  bool LimitMemoryUsingMipBias(int maxAllocAllowed, int mipImprovement);

  /// track texture allocation
  void TextureAllocated(int size, int overhead);

  /// track texture allocation
  void TextureFreed(int size, int overhead) {TextureAllocated(-size,-overhead);}

  /// make sure there is enough memory for "size" allocation
  int ReserveMemory(int size, int minAge);

  int ForcedReserveMemory( int size, bool enableSysFree=true);

  /// check current memory allocation, release as necessary, return how much was released
  int CheckMemoryAllocation();

  void ReleaseAllTextures(bool releaseSysMem=false);
  void ReleaseTexturesWithPerformance(int performance);

  void RemoveLoadingJob( TextureD3D11 *tex );
  void TrackLoadingJob( TextureD3D11 *tex );
  void UpdateLoadingJob( TextureD3D11 *tex );

  //! create surface in VRAM - try reusing first
  int CreateVRAMSurfaceSmart(
    SurfaceInfoD3D11 &surface,
    const TEXTUREDESC11 &desc, PacFormat format, int totalSize, int mipImprovement
    );

  //! create surface in VRAM
  int CreateVRAMSurface(
    SurfaceInfoD3D11 &surface,
    const TEXTUREDESC11 &desc, PacFormat format, int totalSize
    );

  int ReserveSystemLimit(int limit);
  int ReserveSystem( int size );
  int ForcedReserveSystem( int size );

  void ReportTextures11( const char *name );

public:
  size_t GetMaxVRAMAllocation() const;
  size_t GetVRamTexAllocation() const {return _totalAllocated;}

  void OnVRAMChanged();
#ifdef TRAP_FOR_0x8000
  void TrapFor0x8000(const char *str);
#endif
};

#if 0
#define PROTECT_TEXTURE_MEMORY_ACCESS_BANK(bank) AssertSameThread(_textureThread);
#define CHECK_TEXTURE_MEMORY_ACCESS_BANK(bank) AssertSameThread((bank)._textureThread);
#else
#define PROTECT_TEXTURE_MEMORY_ACCESS_BANK(bank) PROFILE_SCOPE_DETAIL_EX(txCSS,tex);TextBankD3D11::StoreThreadId idLock((bank));
#define CHECK_TEXTURE_MEMORY_ACCESS_BANK(bank) DoAssert((bank).CheckTextureMemoryAccess());
#endif
#define PROTECT_TEXTURE_MEMORY_ACCESS() PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*this)
#define CHECK_TEXTURE_MEMORY_ACCESS() CHECK_TEXTURE_MEMORY_ACCESS_BANK(*this)

void ReportMemoryStatusWithVRAM();

} //namespace DX11

TypeIsMovableZeroed(DX11::SurfaceInfoD3D11);
TypeIsMovableZeroed(DX11::D3DSurfaceToDestroy11);

#endif //!_DISABLE_GUI
#endif //_ENABLE_DX11

#endif //_TXTD3D11_HPP

#include "../wpch.hpp"

#if _ENABLE_DX11
#if !_DISABLE_GUI

#include <Es/Common/win.h>
#include <Es/Common/mathUtils.hpp>
#include <El/Common/perfLog.hpp>
#include <El/ParamFile/paramFile.hpp>
#include <El/Statistics/statistics.hpp>
#include <El/Debugging/debugTrap.hpp>

#include "txtd3d11.hpp"
#include "engdd11.hpp"

#include <Es/Threads/multiSync.hpp>
#include <El/FileServer/fileServer.hpp>
#include <El/QStream/qbStream.hpp>
#include <El/common/randomGen.hpp>
#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "../progress.hpp"
#include "../timeManager.hpp"
#include <Es/Algorithms/qsort.hpp>
#include "../world.hpp" // used to access object under cursor for diagnostics
#include "../camera.hpp" // used to access object under cursor for diagnostics
#include "../diagModes.hpp"
#include "d3d11utils.hpp"
#include "El/multicore/jobs.hpp"

extern int VRAMLimit;

#if _ENABLE_CHEATS
extern bool DisableTextures;
extern bool StopLoadingTextures;
extern int LoadTexturesInFrame;
#endif

namespace DX11
{

// {5926683A-2CF2-43df-A8A1-688108C9F922}
static const GUID PrivateDataGUID = 
{ 0x5926683a, 0x2cf2, 0x43df, { 0xa8, 0xa1, 0x68, 0x81, 0x8, 0xc9, 0xf9, 0x22 } };

#define MIN_MIP_SIZE 4 // guarantee QWORD alignment (4 16-bit pixels are enough)

#define REPORT_ALLOC 0 // verbosity level 0..30

static PacFormat BasicFormat( const char *name )
{
  const char *ext=strrchr(name,'.');
  if( ext && !strcmpi(ext,".paa") ) return PacARGB4444;
  else return PacARGB1555;
}

/// Performs actual texture loading from the file into system texture

class TextureD3D11::LoadLevelsJob: public Job, private ::NoCopy
{
  SurfaceInfoD3D11 _systemSurf;
  /// we use Ref to delay texture destruction until the job is complete
  /** to prevent circular referencing, Ref is released once job is processed */
  Ref<TextureD3D11> _tex;

public:
  int _levelMin;
  int _frameUsed;

  /*!
  \param levelMin Least level to be loaded
  */
  LoadLevelsJob(TextureD3D11 *tex, int levelMin);
  ~LoadLevelsJob();

  virtual bool Process();

  SurfaceInfoD3D11 GetResult() const {return _systemSurf;}
};

TextureD3D11::LoadLevelsJob::LoadLevelsJob(TextureD3D11 *tex, int levelMin)
:_tex(tex),_levelMin(levelMin)
{
  PROFILE_SCOPE_EX(txLSI,tex);
  if (!_tex->_src)
    return;
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());

  TEXTUREDESC11 sysd;
  memset(&sysd,0,sizeof(sysd)); 

  Assert(levelMin < MAX_MIPMAPS);
  PacFormat format=tex->_mipmaps[levelMin].DstFormat();
  //GEngineDD11->InitVRAMPixelFormat(sysd.ddpfPixelFormat,format);
  bool isSRGB = false;
  switch (tex->_src->GetTextureType())
  {
  case TT_Diffuse: case TT_Macro: isSRGB = true; break;
  }

  GEngineDD11->InitVRAMPixelFormat(sysd.format,format,isSRGB);

  // create sysmem mip-map chain

#ifdef _XBOX
  // we want sysmem texture not to be tiled
  sysd.format = (D3DFORMAT)MAKELINFMT(sysd.format);
#endif

  sysd.usage=D3D11_USAGE_STAGING;
  sysd.isSRGB = isSRGB;
  sysd.nMipmaps = tex->_nMipmaps-levelMin;

  Assert(levelMin < MAX_MIPMAPS);
  PacLevelMem &topMip = tex->_mipmaps[levelMin];

  // not present -> create it and load it
  sysd.w = topMip._w;
  sysd.h = topMip._h;

  int mipImprovement = tex->_levelLoaded - levelMin;
  bank->UseSystem(_systemSurf,sysd,format,mipImprovement);
  _frameUsed = FreeOnDemandFrameID(); // Caution: atomic, but may be out of sync
}

TextureD3D11::LoadLevelsJob::~LoadLevelsJob()
{
  PROFILE_SCOPE_EX(txLSE,tex);
}


bool TextureD3D11::LoadLevelsJob::Process()
{
  PROFILE_SCOPE_EX(texLS,*);

  if (_systemSurf.GetSurface()._texture)
  {

    // load all mipmaps - coarse mipmaps are very small anyway
    // and it would help little not to load them
    int texSize = _tex->_src->GetTextureType()==TT_Diffuse ? intMax(_tex->_mipmaps[_levelMin]._w,_tex->_mipmaps[_levelMin]._h) : 0;
    for( int i=_levelMin; i<_tex->_nMipmaps; i++ )
    { // sysmem load
      Assert(i < MAX_MIPMAPS);
      PacLevelMem &mip = _tex->_mipmaps[i];

      int aLevel = i-_levelMin;
      Assert( aLevel>=0 );

      // lock surface - we will load it
      HRESULT err;
      D3D11_MAPPED_SUBRESOURCE subResource;
      {
        PROFILE_DX_SCOPE(3dLkR);
        err=GEngineDD11->GetD3DDeviceRec()->Map(_systemSurf.GetSurface()._texture, aLevel, D3D11_MAP_WRITE, 0, &subResource);
      }

      // Assign the true value to pitch
      if (PacFormatIsDXT(mip._dFormat))
      {
        // Pitch is 4 lines together
        mip._pitch = subResource.RowPitch >>2;
      }
      else
      {
        mip._pitch = subResource.RowPitch;
      }

      bool ret = true;

      if( err!=D3D_OK ) GEngineDD11->DDError11("Cannot lock mipmap",err);
      // assume normal pitch
      // load level
      ret = _tex->_src->GetMipmapData(subResource.pData,mip,i,texSize);
      ADD_COUNTER(ldTxP,mip._w*mip._h);

      if (!ret)
      {
        // in case of error fill mipmap with average color
        PackedColor color = _tex->_src->GetAverageColor();
        mip.FillMipmap(subResource.pData,color);
      }

      GEngineDD11->GetD3DDeviceRec()->Unmap(_systemSurf.GetSurface()._texture, aLevel);
      //if( err!=D3D_OK ) GEngineDD11->DDError11("Cannot unlock mipmap",err);

      //actMipmap->GetAttachedSurface(&caps,nextMipmap.Init());
      //actMipmap=nextMipmap;
    } // if( load mipmap level)

    // if we quick-filled only, we should not mark the texture as loaded
#if 0 //def _XBOX
    CacheUse(*bank,levelMin);
    //LogF("texture %s: whole used",Name());
    UpdateLoadedTexDetail(levelMin);
#endif
  }
  _tex.Free();
  return true;
}

/*!
Similar to LoadLevelsSys
*/
bool TextureD3D11::RequestLevelsSys(int levelMin, int priority)
{
  // if there is no time left for loading, we pretend data are not ready
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngineDD11->TextBank());
  if (priority<=TexPriorityNormal && bank->GetTexTimeManager().TimeLeft(TCLoadTexture)<=0) return false;

  // FIX: Avoid Crash
  if (!_src) return false;

  PROFILE_SCOPE_EX(texRq,tex);
  // we do not care about swizzling now - we only need to be sure data are here
  FileRequestPriority prior;
  // map texture priority to file request priority
  if (priority>=TexPriorityCritical) prior = FileRequestPriority(1);
  //else if (priority>=TexPriorityUIHigh) prior = FileRequestPriority(4);
  else if (priority>=TexPriorityUILow) prior = FileRequestPriority(5);
  else if (priority>=TexPriorityNormal) prior = FileRequestPriority(10);
  else if (priority>=TexPriorityLow) prior = FileRequestPriority(100);
  else prior = FileRequestPriority(500);
  if (!_src->RequestMipmapData(_mipmaps,levelMin,_nMipmaps,prior))
  {
    return false;
  }

  // a "better" job would do for us as well, not need to create another
  if (!GEngineDD11->IsMipLoadingAsync())
    return true;
  if (_loadingJob)
  {
    _loadingJob->_frameUsed = FreeOnDemandFrameID();
    bank->UpdateLoadingJob(this);
    if (_loadingJob->GetJobState()!=Job::JSDone)
    {
      return false;
    }
    if (_loadingJob->_levelMin<=levelMin)
      return true;
    // the job is done, we can safely remove it
    bank->RemoveLoadingJob(this);
  }
  _loadingJob = new LoadLevelsJob(this,levelMin);
  bank->TrackLoadingJob(this);
  GJobManager.CreateJob(_loadingJob,1500);
  return false;
}

int TextureD3D11::LoadingJobFrame() const
{
  DoAssert(_loadingJob); // if there is no loading job, it should not be tracked by a heap
  return _loadingJob ? _loadingJob->_frameUsed : 0;
}


bool HeapTraitsLoadingJob::IsLess(const Type &a, const Type &b)
{
  int frameA = a->LoadingJobFrame();
  int frameB = b->LoadingJobFrame();
  return frameA<frameB; // oldest (smallest frameID) first
}

void TextBankD3D11::RemoveLoadingJob( TextureD3D11 *tex )
{
  ScopeLockSection lock(_loadingJobsCS);
  _loadingJobs.HeapDelete(tex);
}
void TextBankD3D11::TrackLoadingJob( TextureD3D11 *tex )
{
  ScopeLockSection lock(_loadingJobsCS);
  _loadingJobs.HeapInsert(tex);
}

void TextBankD3D11::UpdateLoadingJob( TextureD3D11 *tex )
{
  ScopeLockSection lock(_loadingJobsCS);
  _loadingJobs.HeapUpdateDown(tex);
}
/**
We use 200 for grass (ground clutter) and 100 for trees
*/
void TextureD3D11::SetTexturePerformance(int performance)
{
  _performance = performance;
}

void TextureD3D11::UpdateMaxTexDetail()
{
  if (_nMipmaps>0)
  {
    Assert(_largestUsed<_nMipmaps);
    _maxTexDetail = _mipmaps[_largestUsed]._w*_mipmaps[_largestUsed]._h;
  }
  else
  {
    _maxTexDetail = 1;
  }
}
void TextureD3D11::UpdateLoadedTexDetail(int levelLoaded)
{
  // mipmaps which are not loaded can never be active
  if (_levelActive<levelLoaded) _levelActive = levelLoaded;
  // If loading better mipmap, it must be because we want it to be active.
  // Mark it as such to prevent one frame delay in "activating" it, would cause using unused texture
  if (_levelLoaded>levelLoaded) _levelActive = levelLoaded;

  // Update _levelLoaded
  _levelLoaded = levelLoaded;
  DoAssert(_levelLoaded<=_smallLevel || _levelLoaded>=_nMipmaps)

    if (_levelLoaded<=_largestUsed)
    {
      // best mipmap sometimes required special handling
      _loadedTexDetail = _src->GetMaxTexDetail(_maxTexDetail);
      return;
    }
    // we know _levelLoaded is integer - acquiring TexDetail from it is very easy
    // it is the mipmap resolution
    const int missingLods = _levelLoaded-_largestUsed;
    Assert(missingLods<=MAX_MIPMAPS);
    const int nom = 1<<(2*MAX_MIPMAPS-2*missingLods);
    const int denom = 1<<(2*MAX_MIPMAPS);
    const float factor = nom*(1.0f/denom);
    _loadedTexDetail = _maxTexDetail*factor;
}


void TextureD3D11::InitDDSD( TEXTUREDESC11 &ddsd, int levelMin, bool enableDXT )
{
  memset(&ddsd,0,sizeof(ddsd)); 

  ddsd.usage=D3D11_USAGE_DEFAULT;

  Assert(levelMin < MAX_MIPMAPS);
  PacFormat format=_mipmaps[levelMin].DstFormat();

  bool isSRGB = false;
  switch (_src->GetTextureType())
  {
  case TT_Diffuse: case TT_Macro: isSRGB = true; break;
  }

  GEngineDD11->InitVRAMPixelFormat(ddsd.format,format,isSRGB);

  ddsd.w = _mipmaps[levelMin]._w; 
  ddsd.h = _mipmaps[levelMin]._h; 
  ddsd.nMipmaps = _nMipmaps-levelMin;
  ddsd.isSRGB = isSRGB;

#if _HELISIM_RENDER_TO_TEXTURE
  if (_src->GetTextureType() == TT_R2T)
  {
    ddsd.useAsRT = true;
  }
  else
  {
    ddsd.useAsRT = false;
  }
#endif
}


void TextureD3D11::CopyToVRAM(SurfaceInfoD3D11 &surface, int sysLevel, const SurfaceInfoD3D11 &sys)
{
  PROFILE_SCOPE_EX(texUT,tex);
  // select best system surface
  DoAssert (sys.GetSurface()._texture);
  DoAssert (surface.GetSurface()._texture);
  DoAssert((sys._w>>sysLevel)==surface._w);
  DoAssert((sys._h>>sysLevel)==surface._h);
  DoAssert(sys._d3dFormat==surface._d3dFormat);
  DoAssert(sys._nMipmaps - sysLevel == surface._nMipmaps);
 
  for (int i = 0; i < surface._nMipmaps; i++)
  {
    GEngineDD11->GetD3DDeviceRec()->CopySubresourceRegion(surface.GetSurface()._texture, i, 0, 0, 0, sys.GetSurface()._texture, i + sysLevel, NULL);
  }

}

bool TextureD3D11::ReduceTexture(SurfaceInfoD3D11 &surf, int mipImprovement)
{
  int needed = toIntFloor(LevelNeeded());
  // if texture is not needed, assume small version is needed
  if (needed>=_nMipmaps) needed = _nMipmaps-1;
  return ReduceTextureLevel(surf, needed, mipImprovement);
}

bool TextureD3D11::ReduceTextureLevel(int tgtLevel)
{
  if (tgtLevel>_nMipmaps) tgtLevel = _nMipmaps;
  return ReleaseLevelMemory(_levelLoaded,tgtLevel,false,NULL)>0;
}

bool TextureD3D11::ReduceTextureLevel(SurfaceInfoD3D11 &surf, int needed, int mipImprovement)
{
#if !SMALL_MIS_READY
  // following implementation does not work with "one mip only"
  // we might fix it or adapt ReleaseLevelMemory so that it can be used here
  // but as this function is optional, we simply disable it
  return false;
#else
  if (needed > _smallLevel) needed = _smallLevel;
  if (_levelLoaded >= needed) return false;
  if (needed >= _nMipmaps) return false;

  ScopeInUse11 use(_inUse);

  // Return value will contain the actual level
  Assert(_surfaces[_levelLoaded].GetSurface());
  surf = _surfaces[_levelLoaded];

  // Zero the returned texture
  _surfaces[_levelLoaded].Free(Name(),false); // False, because the surface is shared with surf variable

  // Free the rest of disposed textures
  for (int i = _levelLoaded + 1; i < needed; i++)
  {
    Assert(_surfaces[i].GetSurface());
    ReleaseMemory(_surfaces[i]);
  }

  MemoryReleased(_levelLoaded,needed);

  // Update the level loaded variable and quit
  UpdateLoadedTexDetail(needed);

  return true;
#endif
}

void ReportGRAM( const char *name)
{
  if (GEngineDD11) GEngineDD11->ReportGRAM(name);
}

void ReportTextures11()
{
  GEngineDD11->TextBankDD()->ReportTextures11(NULL);
}



int TextureD3D11::TotalSize( int levelMin ) const
{
  int totalSize=0;
  for( int i=levelMin; i<_nMipmaps; i++ )
  {
    Assert(i < MAX_MIPMAPS);
    const PacLevelMem &mip = _mipmaps[i];
    int size = PacLevelMem::MipmapSize(mip.DstFormat(),mip._w,mip._h);
    //int size=_mipmaps[i]._pitch*_mipmaps[i]._h;
    totalSize+=size;
  }
  return totalSize;
}

/*!
Similar to LoadLevels
*/

bool TextureD3D11::RequestLevels(int levelMin, int priority)
{
  // if no levels are needed, we can load nothing and still return success
  if (levelMin<0 || _levelLoaded<=levelMin) return true;

  return RequestLevelsSys(levelMin,priority);
}


#if _DEBUG // _ENABLE_REPORT //_DEBUG
/// enable texture tracking using private data
#define TRACK_TEXTURES 0
#endif


#if TRACK_TEXTURES
static RString CreatingSurface;
static char InterestedIn[]="ca\\plants\\data\\palma3_polyplane";

static bool InterestedInTexture(const char *name)
{
  if (!strncmp(InterestedIn,name,sizeof(InterestedIn)-1))
  {
    return true;
  }
  //if (strstr(name,InterestedIn)) return true;
  return false;
}

#endif

TextureD3D11 *TextureD3D11::GetDefaultTexture() const
{
#if 0 // TRACK_TEXTURES
  if (strstr(GetName(),InterestedIn))
  {
    LogF("@@@ Default used for %s",cc_cast(GetName()));
  }
#endif
  TextureD3D11 *result;
  switch (Type())
  {
  case TT_Specular: result = GEngineDD11->TextBankDD()->GetDefSpecularTexture(); break;
  case TT_DTSpecular: result = GEngineDD11->TextBankDD()->GetDefDTSpecularTexture(); break;
  case TT_Macro: result = GEngineDD11->TextBankDD()->GetDefMacroTexture(); break;
  case TT_Detail: result = GEngineDD11->TextBankDD()->GetDefDetailTexture(); break;
  case TT_Normal: result = GEngineDD11->TextBankDD()->GetDefNormalMapTexture(); break;
  case TT_Mask: result = GEngineDD11->TextBankDD()->GetDefMaskTexture(); break;
  default: result = GEngineDD11->TextBankDD()->GetWhiteTexture(); break;
  }
  Assert(result);
  Assert(result->_permanent);
  return result;
}

#if 0 //_ENABLE_REPORT
#define INTERESTED_IN_MIPMAPS 1
#endif

#if INTERESTED_IN_MIPMAPS
static char InterestedInMipmaps[256]="trnka_";
#endif

static TimeManager VisTimeManager;

int TextureD3D11::LoadLevels(int levelMin, bool ignoreLoadedLevels)
{
  // if no levels are needed, we can load nothing and still return success
  if (levelMin<0) return 0;

  int mipImprovement = _levelLoaded - levelMin;

  //ScopeLock<AbstractTextureLoader> lock(*_bank->_loader);
  // if the texture loaded contains min..max range, let it be...

  Assert (levelMin<_nMipmaps);
  Assert (levelMin>=0);

  if (ignoreLoadedLevels)
  {
    if (!_surfaces[0].GetSurface()._texture)
    {
      ignoreLoadedLevels  = false;
    }
  }

  // assume: there is some levelCur so that for every i, i>=levelCur
  // _mipmaps[i]._memorySurf is not NULL
  if( _levelLoaded>levelMin || ignoreLoadedLevels)
  {
    SCOPE_GRF(Name(),0);

    TimeManagerScope scope;

    TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngineDD11->TextBank());

    scope.Start(TCLoadTexture,bank->GetTexTimeManager());

    // no need to release old data - we will be happy to reuse them
    //ReleaseMemory(true); // release all old mipmap data
    //levelCur=_nMipmaps; // all levels released

#if REPORT_ALLOC>=20
    Assert(levelMin < MAX_MIPMAPS);
    LogF
      (
      "Load %s (%d:%d x %d)",
      (const char *)Name(),
      levelMin,_mipmaps[levelMin]._w,_mipmaps[levelMin]._h
      );
#endif

    // Lock the texture
    ScopeInUse11 use(_inUse);
    SurfaceInfoD3D11 systemSurf;
    int ret = -1;
    {
      bool loadSync = true;
      if (_loadingJob)
      {
        // if it is done, canceling it will do no harm
        GJobManager.CancelJob(_loadingJob);
        if (_loadingJob->GetJobState()!=Job::JSCanceled)
        {
          GJobManager.Wait(_loadingJob);
          if (_loadingJob->_levelMin<=levelMin)
          {
            systemSurf = _loadingJob->GetResult();
            // if there was a "better" job completed, we can use it
            bank->RemoveLoadingJob(this);
            _loadingJob.Free();
            loadSync = false;
          }
        }
      }

      if (loadSync)
      {
        // no data yet, but we need them
        LoadLevelsJob job(this,levelMin);
        Verify (job.Process());
        systemSurf = job.GetResult();
      }

      // Load texture in the system memory
      if (systemSurf.GetSurface()._texture)
      {
        ret = 0;
      }
      else
      {
        WarningMessage("Cannot load mipmap %s",Name());
      }
    }


    if (ret>=0)
    {
#if !defined(_XBOX) && !defined(_X360SHADERGEN)
      if (ignoreLoadedLevels && _surfaces[0].GetSurface()._texture)
      { // surface already exists, we only need to update it
        CopyToVRAM(_surfaces[0], 0, systemSurf);
      }
#endif

      { // Create whole texture chain and load the data
#ifndef _XBOX
        PROFILE_SCOPE_EX(texCV,tex);
        TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngineDD11->TextBank());

        Assert(CheckIntegrity());

        Assert(levelMin < MAX_MIPMAPS);
        //Assert(_levelLoaded < _nMipmaps);
        int to = _levelLoaded;
        if (to>_smallLevel+1) to = _smallLevel+1;
        int loaded = to;
        bool canReuseSystem = true;
        for (int i = to; --i>=levelMin; ) // go backwards so that can we abort on a failure and still be in a consistent state
        {

          // Get the texture description
          TEXTUREDESC11 ddsd;
          InitDDSD(ddsd, i, true);

          // Create or reuse VRAM surface
          PacFormat format = _mipmaps[i].DstFormat();
          int totalSize=TotalSize(i);
#if TRACK_TEXTURES
          CreatingSurface = GetName();
#endif
          int created = bank->CreateVRAMSurfaceSmart(_surfaces[i],ddsd,format,totalSize,mipImprovement);
#if TRACK_TEXTURES
          CreatingSurface = RString();
          if (InterestedInTexture(GetName()))
          {
            LogF(
              "@@@ %p: CreateTexture %s (%dx%d,%d), %d",
              _surfaces[i].GetSurface(),
              cc_cast(GetName()),_surfaces[i]._w,_surfaces[i]._h,_nMipmaps,i
              );
          }
#endif
          if ( created< 0)
          {
            RptF("Error: Failed to create surface texture (%s:%d)",cc_cast(GetName()),i);
            ret = -1;
            break;
          }

#if _HELISIM_RENDER_TO_TEXTURE
          // Register R2T texture
          if (ddsd.useAsRT)
          {
            DoAssert(_surfaces[i]._isR2T);
            GEngineDD11->RegisterR2TSourceTexture(_surfaces[i].GetSurface(), _surfaces[i]._w, _surfaces[i]._h, _src->GetR2TName());
          }
#endif

#if !defined(_XBOX) && !defined(_X360SHADERGEN)
          // Copy the texture to VRAM
          CopyToVRAM(_surfaces[i], i-levelMin, systemSurf);
#endif
          loaded = i; // track which level did we load
        }

        //LogF("texture %s: whole used",Name());
        //if (loaded<=_smallLevel) UpdateLoadedTexDetail(ignoreLoadedLevels? _levelLoaded : loaded);
        if (loaded<=_smallLevel) UpdateLoadedTexDetail(loaded);

        //Assert(CheckIntegrity());
        ReleaseSystem(systemSurf,canReuseSystem);
#else
        TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngineDD11->TextBank());
        // fill _surface data using systemSurf


        int to = _levelLoaded;
        if (to>_smallLevel+1) to = _smallLevel+1;
#if !SMALL_MIPS_READY
        // we can release the currently owned surface, as we will load a new (finer) one
#if TRACK_TEXTURES
        if (InterestedInTexture(Name()))
        {
          LogF("Release %s:%d, loading %d",cc_cast(Name()),_levelLoaded,levelMin);
        }
#endif
        ReleaseMemory(true);
        // on X360 we are able to degrade textures gracefully and we can always keep finest mipmap only
        int i = levelMin;
#else
        for (int i = levelMin; i < to; i++)
#endif
        {

          // Get the texture description
          TEXTUREDESC9 ddsd;
          InitDDSD(ddsd, i, true);

          // Create or reuse VRAM surface
          PacFormat format = _mipmaps[i].DstFormat();
          int totalSize=TotalSize(i);
#if TRACK_TEXTURES
          CreatingSurface = GetName();
#endif
          int created = bank->CreateVRAMSurfaceSmart(_surfaces[i],ddsd,format,totalSize,mipImprovement);
#if TRACK_TEXTURES
          CreatingSurface = RString();
          if (InterestedInTexture(GetName()))
          {
            LogF(
              "@@@ %p: CreateTexture %s (%dx%d,%d), %d",
              _surfaces[i].GetSurface(),
              cc_cast(GetName()),_mipmaps[i]._w,_mipmaps[i]._h,_nMipmaps,i
              );
          }
#endif
          if ( created< 0)
          {
            RptF("Error: Failed to create surface texture (%s:%d)",cc_cast(GetName()),i);
            return -1;
          }

          // Copy the texture to VRAM
          DWORD gpuFormat = XGGetGpuFormat(ddsd.format);
          DWORD xgTileFlags = 0;
          int toLevel = _nMipmaps;
          if (!XGIsPackedTexture(_surfaces[i].GetSurface())) xgTileFlags |= XGTILE_NONPACKED;
          else
          {
            // if tgt is packed, src has to be packed as well
            Assert( XGIsPackedTexture(_surfaces[i].GetSurface()) );
#if 0
            // for packed mipmap tails it is enough to copy first level of the packed tail
            // however, when using XGEndianSwapSurface only relevant data are processed
            // and we need to process all mipmaps then
            D3DMIPTAIL_DESC tail;
            _surfaces[i].GetSurface()->GetTailDesc(&tail);
            toLevel = tail.BaseLevel+1;
            //Assert(toLevel<=_nMipmaps);
            if (toLevel>_nMipmaps) toLevel = _nMipmaps;
#endif
          }
          if (XGIsBorderTexture(_surfaces[i].GetSurface())) xgTileFlags |= XGTILE_BORDER;

          XGTEXTURE_DESC destDesc;
          XGGetTextureDesc( _surfaces[i].GetSurface(), 0, &destDesc );

          // _surfaces[i] is the target - complete pyramid
          // systemSurf[i-levelMin] is the basis of the source pyramid
          //AutoArray<char> temp;
          //temp.Resize(_surfaces[i]._usedSize);

          for (int l=i; l<toLevel; l++)
          {
            D3DLOCKED_RECT lockedSrc;
            D3DLOCKED_RECT lockedTgt;
            // with tiling we need write access to change endian
            systemSurf.GetSurface()->LockRect(
              l-levelMin,&lockedSrc,NULL,(ddsd.format&D3DFORMAT_TILED_MASK) ? 0 : D3DLOCK_READONLY
              );
            _surfaces[i].GetSurface()->LockRect(l-i,&lockedTgt,NULL,0);
            // the pitch needs to be the same
            int srcW = systemSurf._w>>(l-levelMin);
            int srcH = systemSurf._h>>(l-levelMin);
            // check if source and target dimensions match
            Assert(srcH==_surfaces[i]._h>>(l-i));
            int blockH = srcH;
            // caution: for DXT blocks pitch is in 4 lines
            if (PacFormatIsDXT(_mipmaps[l]._dFormat))
            {
              blockH>>=2; // block sizes described in "Texture and Surface Block Sizes"
            }

            // some textures (e.g. procedural ones) are already big endian
            if (_src->IsLittleEndian())
            {
              if (ddsd.format&D3DFORMAT_TILED_MASK)
              {
                // first perform in-place endian swap, followed by a tiling
                XGEndianSwapSurface(
                  lockedSrc.pBits,lockedSrc.Pitch,
                  lockedSrc.pBits,lockedSrc.Pitch,srcW,srcH,systemSurf._d3dFormat
                  );

                Assert(destDesc.Width==_mipmaps[i]._w);
                Assert(destDesc.Height==_mipmaps[i]._h);
                XGTileTextureLevel(  
                  destDesc.Width, destDesc.Height, l-i, gpuFormat, xgTileFlags,
                  lockedTgt.pBits, NULL, lockedSrc.pBits, lockedSrc.Pitch, NULL );
              }
              else
              {
                // endian swap for linear texture
                XGEndianSwapSurface(
                  lockedTgt.pBits,lockedTgt.Pitch,
                  lockedSrc.pBits,lockedSrc.Pitch,srcW,srcH,ddsd.format
                  );
              }
            }
            else
            {
              if (ddsd.format&D3DFORMAT_TILED_MASK)
              {
                // we still need to copy/tile the data, even when endian conversion is not needed
                XGTileTextureLevel(
                  _mipmaps[i]._w,_mipmaps[i]._h,l-i,gpuFormat,xgTileFlags,
                  lockedTgt.pBits,NULL,lockedSrc.pBits,lockedSrc.Pitch,NULL
                  );
              }
              else
              {
                // both source and target linear, do a simple copy          
                Assert(lockedTgt.Pitch==lockedSrc.Pitch);
                XMemCpyStreaming_WriteCombined(lockedTgt.pBits,lockedSrc.pBits,lockedTgt.Pitch*blockH);
              }
            }
            systemSurf.GetSurface()->UnlockRect(l-levelMin);
            _surfaces[i].GetSurface()->UnlockRect(l-i);
          }
        }

        UpdateLoadedTexDetail(levelMin);

        DoAssert(_levelLoaded<=levelMin);
        DoAssert(_levelLoaded<=_nMipmaps);
        DoAssert(_surfaces[_levelLoaded].GetSurface()!=NULL);
        // TODOX360: use ReleaseSystem
        //ReleaseSystem(systemSurf,true);
        systemSurf.Free(Name(),true);

        // some errors may have left the texture in corrupt state and we need to free it now
        if (ret < 0)
        {
          ReleaseMemory(true);
          return ret;
        }
#endif
      }
    }
  }


  return 0;
}

bool TextBankD3D11::Reuse(
                         SurfaceInfoD3D11 &surf, const TEXTUREDESC11 &ddsd, PacFormat format,
                         bool enableReduction, int mipImprovement
                         )
{
  CHECK_TEXTURE_MEMORY_ACCESS();
  PROFILE_SCOPE_EX(texRu,tex);
  // first try discarding previously used
  int w=ddsd.w;
  int h=ddsd.h;
  int nMipmaps=ddsd.nMipmaps;
  if (w*h<_minTextureSize*2)
  {
    // no need to try reducing - no reduction is possible for this size
    enableReduction = false;
  }
  HMipCacheD3D11 *last=_used.First();
  if (!last) return false;
  // min. age dependent on max. age - do not reuse if much older textures exist
  int maxFrameId = _used.FrameID()-(_used.FrameID()-last->GetFrameNum())/2;
  for (; last; last=_used.Next(last))
  {
    // if the texture is recent, all after it are more recent
    if (last->GetFrameNum()>maxFrameId) break;
    // release mip-map data
    TextureD3D11 *texture=last->texture;
    //cannot release this one
    if( texture->_inUse ) continue;

    const SurfaceInfoD3D11 &surface=texture->_surfaces[texture->_levelLoaded];
    if( nMipmaps!=surface._nMipmaps ) continue;
    // match size
    if( surface._w!=w ) continue;
    if( surface._h!=h ) continue;
    // match pixel format
    // if equal - this texture is perfect match
    if( surface._d3dFormat!=format ) continue;

#if _ENABLE_REPORT
    if (last->GetFrameNum()>_used.FrameID()-500)
    {
      LogF(
        "Reused %s:%d (age %d)",cc_cast(texture->GetName()),last->level,
        _used.FrameID()-last->GetFrameNum()
        );
    }
#endif
    // surface moved to reusable
    if (enableReduction && texture->ReduceTexture(surf, mipImprovement))
    {
      return true;
    }
    else
    {
      // note: during ReuseMemory last becomes invalid, as texture is removed
      // from the rootPart list and corresponding cache entry is deleted
      texture->ReuseMemory(surf);
      return true;
    }
  }
  return false;
}

int TextBankD3D11::FindSurface(
                              int w, int h, int nMipmaps, DXGI_FORMAT format,
                              const AutoArray<SurfaceInfoD3D11> &array
                              ) const
{
  CHECK_TEXTURE_MEMORY_ACCESS();
  PROFILE_SCOPE_EX(texFn,tex);
  int i;
  for( i=0; i<array.Size(); i++ )
  {
    const SurfaceInfoD3D11 &surface=array[i];
    // match mipmap structure
    if( nMipmaps!=surface._nMipmaps ) continue;
    //const PacLevelMem &pMip=pattern->_mipmaps[0];
    // match size
    if( surface._w!=w ) continue;
    if( surface._h!=h ) continue;
    // match pixel format
    if( surface._d3dFormat!=format ) continue;
#ifdef _XBOX
    // we cannot reuse a surface which is busy
    if (surface.GetSurface()->IsBusy()) continue;
#endif
    /*
    // this texture is perfect match
    if( surface._texture==texture )
    {
    LogF("Texture %dx%d (%s) reused",w,h,(const char *)texture->Name());
    */
    return i;
  }
  return -1;
}

#pragma warning(disable:4702)

int TextBankD3D11::DeleteLastReleased(D3DSurfaceDestroyer11 *batch)
{
  CHECK_TEXTURE_MEMORY_ACCESS();
#ifdef _XBOX
  batch = NULL;
#endif
  // LRU is last
  // delete first 
  for (int i=0; i<_freeTextures.Size(); i++)
  {
    SurfaceInfoD3D11 &free=_freeTextures[i];
#ifdef _XBOX
    if (free.GetSurface()->IsBusy()) continue;
#endif

    int size=free.SizeUsed();

    _totalSlack -= free.Slack();
    _freeAllocated -= size;
    TextureFreed(size,free.Slack());

    ADD_COUNTER(tHeap,size);
    _thisFrameAlloc++;

#if REPORT_ALLOC>=10
    if (!batch || REPORT_ALLOC>=15)
    {
      LogF
        (
        "VID Released %dx%dx%s (%d B) deleted",
        free._w,free._h,FormatName(free._format),size
        );
    }
#endif

    if (batch) batch->Add(free);
    free.Free(NULL,true,batch!=NULL);
    _freeTextures.Delete(i);

    return size;
  }
  // reached only when there is no free texture to be released
  return 0;
}

#pragma warning(default:4702)

int TextBankD3D11::DeleteLastSystem( int need, D3DSurfaceDestroyer11 *batch )
{
  PROFILE_SCOPE_EX(texDS,tex);
  // LRU is last
  // delete first 
  SurfaceInfoD3D11 &free=_freeSysTextures[0];
  int size=free.SizeUsed();
  _systemAllocated-=size;
#if REPORT_ALLOC>=10
  if (!batch || REPORT_ALLOC>=15)
  {
    LogF
      (
      "SYS Released %dx%dx%s (%dB) deleted",
      free._w,free._h,FormatName(free._format),size
      );
  }
#endif
  if (batch) batch->Add(free);
  //ADD_COUNTER(syRel,1);
  free.Free(NULL,true,batch!=NULL);

  _freeSysTextures.Delete(0);
  return size;
}

void TextBankD3D11::AddReleased( SurfaceInfoD3D11 &surf )
{
  CHECK_TEXTURE_MEMORY_ACCESS();
#if REPORT_ALLOC>=40
  LogF
    (
    "Add to released %dx%dx%s (%dB)",
    surf._w,surf._h,FormatName(surf._format),surf.Size()
    );
#endif
  _freeTextures.Add(surf);
  _freeAllocated += surf.SizeUsed();
}

void TextBankD3D11::UseReleased(SurfaceInfoD3D11 &surf, const TEXTUREDESC11 &ddsd)
{
  CHECK_TEXTURE_MEMORY_ACCESS();
  Assert(surf.IsNull());
  int reuse=FindReleased(ddsd.w,ddsd.h,ddsd.nMipmaps,ddsd.format);
  if( reuse<0 ) return;
  SurfaceInfoD3D11 &reused=_freeTextures[reuse];
  surf=reused;
#if REPORT_ALLOC>=20
  LogF
    (
    "Released %dx%dx%s (%dB) reused.",
    reused._w,reused._h,FormatName(reused._format),reused.SizeUsed()
    );
#endif
  Assert(ddsd.nMipmaps==surf._nMipmaps);
  _freeAllocated -= reused.SizeUsed();
  reused.Free(NULL,false);
  _freeTextures.Delete(reuse);
}

int TextBankD3D11::FindSystem(int w, int h, int nMipmaps, DXGI_FORMAT format) const
{
  CHECK_TEXTURE_MEMORY_ACCESS();
  PROFILE_SCOPE_EX(texFS,tex);
  return FindSurface(w,h,nMipmaps,format,_freeSysTextures);
}

/*!
\patch 5142 Date 3/20/2007 by Ondra
- Fixed: On "Cannot create system memory surface: 8007000e" error, some memory is flushed to allow game continuing.
*/
void TextBankD3D11::UseSystem(SurfaceInfoD3D11 &surf, const TEXTUREDESC11 &ddsd, PacFormat format, int mipImprovement)
{
  PROFILE_SCOPE_EX(txUsS,tex);
  int nMipmaps = ddsd.nMipmaps;
  int index=FindSystem(ddsd.w,ddsd.h,nMipmaps,ddsd.format);
  if( index>=0 )
  {
    surf=_freeSysTextures[index];
    _freeSysTextures.Delete(index);
#if REPORT_ALLOC>=30
    LogF
      (
      "Used system %dx%dx%s (%dB)",
      surf._w,surf._h,FormatName(surf._format),surf.SizeUsed()
      );
#endif
  }
  else
  {
    /**/

    TEXTUREDESC11 ssss=ddsd;
    MyD3DDeviceCB &dDraw=_engine->GetD3DDeviceRec();
    int size = surf.CalculateSize(dDraw,ssss,format);
    int sizeNeeded = _engine->AlignVRAMTex(size);
    ReserveSystem(sizeNeeded);
    // create a new surface

    int retry = 3;  // retry only a few times
Retry:
    //ADD_COUNTER(syCre,1);
#if 0 // _ENABLE_CHEATS
    static float randomFailure = 0.01f;
    bool pretendFailure = GRandGen.RandomValue()<randomFailure;
#else
    const bool pretendFailure = false;
#endif
    HRESULT err = pretendFailure ? E_OUTOFMEMORY : surf.CreateSurface(_engine,dDraw,ssss,format);
    if( err!=S_OK )
    {
      if (--retry>=0)
      {
        if (ForcedReserveSystem(sizeNeeded)) goto Retry;
        //ReportMemoryStatus(true);
        if (FreeOnDemandSystemMemoryLowLevel(sizeNeeded)) goto Retry;
        if (ForcedReserveMemory(sizeNeeded)) goto Retry;
      }

      RptF("Forced memory flush (%d system memory surface creation failed)",sizeNeeded);
      RptF(
        "Cannot create system memory surface %s,%dx%d (size %d B), Error %x",
        cc_cast(FormatName(format)),ssss.w,ssss.h,sizeNeeded,err
        );
      ReportMemoryStatusWithVRAM();
      // something is bad. We should definitely consider reset unless it was done recently
      _engine->RequestFlushMemoryCritical();
    }
    else
    {
      _systemAllocated += surf.SizeUsed();
    }
  }
}

void TextBankD3D11::AddSystem( SurfaceInfoD3D11 &surf )
{
  _freeSysTextures.Add(surf);
  surf.Free(NULL,false);
  while( _freeSysTextures.Size()>0 && _systemAllocated>_limitSystemTextures )
  {
    DeleteLastSystem(0);
  }
}

void TextureD3D11::SetNMipmaps( int n )
{
  if (_nMipmaps==n) return;
  LogF("%s: Set %d mipmaps",Name(),n);
  int discard = n;
  if (discard<_levelLoaded) discard = _levelLoaded;
  if (discard<_nMipmaps)
  {
    ReleaseLevelMemory(discard,_nMipmaps);
  }
  _nMipmaps=n;
  if (_levelLoaded>_nMipmaps)
  {
    UpdateLoadedTexDetail(_nMipmaps);
  }
  if (_smallLevel>_nMipmaps-1)
  {
    _smallLevel = _nMipmaps-1;
  }
  Assert(_nMipmaps>=MAX_MIPMAPS || _surfaces[_nMipmaps].IsNull());
  Assert(n <= MAX_MIPMAPS);
}

/*!
\patch 1.03 Date 7/12/2001 by Ondra
- Improved: more robust handling of max. texture size
*/
void TextureD3D11::ASetNMipmaps( int n )
{
  if (HeadersReadyFast())
  {
    TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
    PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*bank);
    // never attempt to set more mipmaps then you can
    // assume max. size was already set
    saturateMin(n,_nMipmaps-_largestUsed);
    SetNMipmaps(_largestUsed+n);
  }
  else
  {
    _nMipmaps = n;
  }
}

void TextureD3D11::MemoryReleased(int from, int to)
{
  Assert(from>=_levelLoaded);
  Assert(to<=_nMipmaps);

  // levels smaller than _smallLevel can never be released individually
  Assert(from<=_smallLevel || from>=_nMipmaps);
  // mark as released
  // delete from statistics
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  for (int level=from; level<to; level++)
  {
    if( _surfCache[level] )
    {
      CHECK_TEXTURE_MEMORY_ACCESS_BANK(*bank);
      Assert(level<=_smallLevel);
#if TRACK_TEXTURES
      if (InterestedInTexture(GetName()))
      {
        LogF(
          "@@@ Surface released %s:%d, age %d",cc_cast(GetName()),level,
          _used.FrameID()-_surfCache[level]->GetFrameNum()
          );
      }
#endif
      bank->_used.Delete(_surfCache[level]);
      _surfCache[level]=NULL;
    }
  }

  // when releasing affects level loaded, set it
  if (from<=_levelLoaded && to>_levelLoaded)
  {
    UpdateLoadedTexDetail((to <= _smallLevel) ? to : _nMipmaps);
  }
}

void TextBankD3D11::TextureAllocated(int size, int overhead)
{
  _totalAllocated += size;
  _engine->AddLocalVRAMAllocated(size+overhead);
  _freeTextureMemory -= size+overhead;
}

int TextureD3D11::ReleaseMemory( SurfaceInfoD3D11 &surf, bool store, D3DSurfaceDestroyer11 *batch )
{
  int ret = 0;
#ifdef _XBOX
  batch = NULL;
#endif
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  if( !surf.IsNull())
  {
    // we need to wait until the surface can be released
    GEngineDD11->D3DTextureDestroyed(surf);
    // first of all: move data to reserve bank
    if( store )
    {
      // make sure surface is restored
      // surface might be lost?
#if REPORT_ALLOC>=20
      LogF
        (
        "VID Large Add To Bank %dx%dx%s (%dB) - '%s'",
        _surface._w,_surface._h,FormatName(_surface._format),_surface.SizeUsed(),
        (const char *)Name()
        );
#endif
      bank->AddReleased(surf);
    }
    else
    {
      int size = surf.SizeUsed();
      ret += size;
      bank->TextureFreed(size,surf.Slack());

      bank->_totalSlack -= surf.Slack();

      bank->_thisFrameAlloc++;
      ADD_COUNTER(tHeap,size);
#if REPORT_ALLOC>=10
      if (!batch || REPORT_ALLOC>=15)
      {
        LogF
          (
          "VID Large Release %dx%dx%s (%dB) - '%s', rest %d, %s",
          _surface._w,_surface._h,FormatName(_surface._format),_surface.SizeUsed(),
          (const char *)Name(),bank->_totalAllocated,
          batch ? "Batch" : ""
          );
      }
#endif
      if (batch) batch->Add(surf);
    }
    surf.Free(Name(),!store,batch!=NULL);
  }
  return ret;
}

bool TextureD3D11::CheckIntegrity() const
{
  bool ret = true;
  for (int i=0; i<_levelLoaded; i++)
  {
    if (!_surfaces[i].IsNull())
    {
      LogF("%s:%d surface present, not tracked by _levelLoaded",cc_cast(GetName()),i);
      ret = false;
    }
  }
#if SMALL_MIPS_READY
  for (int i=_levelLoaded; i<_smallLevel; i++)
  {
    if (_surfaces[i].IsNull())
    {
      LogF("%s:%d surface not present, tracked by _levelLoaded",cc_cast(GetName()),i);
      ret = false;
    }
  }
#else
  // only one surface should be present
  if (_levelLoaded<_nMipmaps && _surfaces[_levelLoaded].IsNull())
  {
    LogF("%s:%d surface not present, tracked by _levelLoaded",cc_cast(GetName()),_levelLoaded);
    ret = false;
  }
  for (int i=_levelLoaded+1; i<_smallLevel; i++)
  {
    if (!_surfaces[i].IsNull())
    {
      LogF("%s:%d surface present, not needed",cc_cast(GetName()),i);
      ret = false;
    }
  }
#endif
  if (!_permanent)
  {
    for (int i=0; i<_nMipmaps; i++)
    {
      if (!_surfaces[i].IsNull())
      {
        if(i<=_smallLevel && !_surfCache[i])
        {
          LogF("%s:%d missing cache entry",cc_cast(GetName()),i);
          ret = false;
        }
      }
    }
  }
  return ret;
}

#ifdef TRAP_FOR_0x8000
bool TextureD3D11::TrapFor0x8000(const char *str) const
{
#if 1
  for (int i=0; i<32; i++)
  {
    if (_dummyInts[i]!=0xabcdef78)
    {
      RptF("TrapFor0x8000 %s, texture: %s, mipmapIx: %d", str, cc_cast(GetName()), i);
      RptF(" _dummyInts[%d]==0x%x addr=0x%x", i, _dummyInts[i], _dummyInts+i); 
      return true;
    }
  }
#endif
  for (int i=0; i<MAX_MIPMAPS; i++)
  {
    if ( ((int)_surfCache[i].GetRef()) & 0x80000000 )
    {
      RptF("TrapFor0x8000 %s, texture: %s, mipmapIx: %d", str, cc_cast(GetName()), i);
      for (int j=0; j<MAX_MIPMAPS; j++)
      {
        if ( ((int)_surfCache[j].GetRef()) & 0x80000000 )
          RptF(" _surface[%d].GetHandle()==%x  _surfCache[%d]==%x addr=%x", j, _surfaces[j].GetHandle(), j, _surfCache[j].GetRef(), _surfCache[j].GetRefPtr()); 
      }
      return true;
    }
  }
  return false;
}
#endif

int TextureD3D11::ReleaseLevelMemory( int from, int to, bool store, D3DSurfaceDestroyer11 *batch)
{
  if (_nMipmaps>MAX_MIPMAPS) return 0; // if the texture not initialized yet, do nothing
  if (_levelLoaded>=_nMipmaps) return 0; // if nothing is loaded, do nothing


  if (_loadingJob && _loadingJob->_levelMin>=from && _loadingJob->_levelMin<to)
  {
    TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
    // when discarding the memory related to the job, we may discard even the job
    GJobManager.CancelJob(_loadingJob);
    // first make sure the job is no longer running
    // TODO: consider using Ref to job instead of direct job, this would allow us to discard asynchronously
    GJobManager.Wait(_loadingJob);
    bank->RemoveLoadingJob(this);
    _loadingJob.Free();
  }

  Assert(CheckIntegrity());
  Assert(from>=_levelLoaded && from<=_nMipmaps);
  Assert(to>=_levelLoaded && to<=_nMipmaps);
  int size = 0;
#if SMALL_MIPS_READY
  for (int i = from; i < to; i++)
  {
#if TRACK_TEXTURES
    if (InterestedInTexture(Name()))
    {
      LogF("@@@ ReleaseMemory %s: %d (%dx%d)",cc_cast(GetName()),i,_surfaces[i]._w,_surfaces[i]._h);
    }
#endif
    size += ReleaseMemory(_surfaces[i], store, batch);
  }
  MemoryReleased(from,to);
#else
  Assert(from!=to);
  Assert(to>=_nMipmaps || to<=_smallLevel); // must not address anything from _smallLevel+1 on
  // first copy into to if needed
  if (to<=_smallLevel)
  {
#if TRACK_TEXTURES
    if (InterestedInTexture(Name()))
    {
      LogF("Release %s:%d..%d",cc_cast(Name()),from,to);
    }
#endif
    TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
    // do the copy - using LockRect should be easy and straightforward
    // there is a catch, though: we need to allocate the surface first
    // TODO: attempt recycling
    int totalSize = TotalSize(to);
    PacFormat format = _mipmaps[to].DstFormat();
    TEXTUREDESC11 ddsd;
    ddsd.format = _surfaces[from]._d3dFormat;
    ddsd.h = _mipmaps[to]._h;
    ddsd.w = _mipmaps[to]._w;
    ddsd.nMipmaps = _nMipmaps-to;
    ddsd.pool = D3DPOOL_DEFAULT;
    bank->CreateVRAMSurface(_surfaces[to],ddsd,format,totalSize);
    // now copy the whole mip-map chain into the new texture
    for (int i=to; i<_nMipmaps; i++)
    {
      D3DLOCKED_RECT lockedSrc;
      D3DLOCKED_RECT lockedTgt;
      // source read only makes sure memory is cached
      // TODO: use LockTail
      // _surfaces[from] has _nMipmaps-from levels
      // _surfaces[to] has _nMipmaps-to levels
      _surfaces[from].GetSurface()->LockRect(i-from,&lockedSrc,NULL,D3DLOCK_READONLY);
      _surfaces[to].GetSurface()->LockRect(i-to,&lockedTgt,NULL,0);
      int srcH = _surfaces[from]._h>>(i-from);
      // check if source and target dimensions match
      Assert(srcH==_surfaces[to]._h>>(i-to));
      int blockH = srcH;
      // caution: for DXT blocks pitch is in 4 lines
      if (PacFormatIsDXT(_mipmaps[to]._dFormat))
      {
        blockH>>=2; // block sizes described in "Texture and Surface Block Sizes"
      }
      XMemCpyStreaming_WriteCombined(lockedTgt.pBits,lockedSrc.pBits,lockedTgt.Pitch*blockH);
      _surfaces[to].GetSurface()->UnlockRect(to-i);
      _surfaces[from].GetSurface()->UnlockRect(from-i);

    }
  }
  if (from<_nMipmaps)
  {
    // now release the old one
    size += ReleaseMemory(_surfaces[from], store, batch);
    MemoryReleased(from,to);
  }
#endif
  Assert(CheckIntegrity());
  return size;
}

int TextureD3D11::ReleaseMemory( bool store, D3DSurfaceDestroyer11 *batch )
{
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*bank);
  return ReleaseLevelMemory(_levelLoaded,_nMipmaps,store,batch);
}

#ifndef _XBOX
int TextureD3D11::ReleaseSystem(SurfaceInfoD3D11 &surface, bool store, D3DSurfaceDestroyer11 *batch)
{
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*bank);
  int ret = 0;
  if( surface.GetSurface()._texture )
  {
    if( store )
    {
      // move data to reserve bank
#if REPORT_ALLOC>=30
      LogF
        (
        "SYS Add To Bank %dx%dx%s (%dB)",
        surface._w,surface._h,
        FormatName(surface._format),surface.SizeUsed()
        );
#endif
      bank->AddSystem(surface);
      surface.Free(Name(),false);
    }
    else
    {
#if REPORT_ALLOC>=15
      if (!batch || REPORT_ALLOC>=15)
      {
        LogF
          (
          "SYS Release %dx%dx%s (%dB), rest %d, %s",
          surface._w,surface._h,
          FormatName(surface._format),surface.SizeUsed(),
          bank->_systemAllocated,batch ? "Batch" : ""
          );
      }
#endif
      bank->_systemAllocated -= surface.SizeUsed();
      ret += surface.SizeUsed();
      //ADD_COUNTER(syRel,1);
      if (batch) batch->Add(surface);
      surface.Free(Name(),true,batch!=NULL);
    }
  }
  return ret;
}
#endif

void TextureD3D11::ReuseMemory( SurfaceInfoD3D11 &surf )
{
  // reuse immediately
  if (_surfaces[_levelLoaded].GetSurface()._texture)
  {
    // first of all: move data to reserve bank

    surf = _surfaces[_levelLoaded];
    // allocated memory amount not changed
#if REPORT_ALLOC>=20
    LogF
      (
      "VID Directly reused %dx%dx%s (%dB) - '%s'",
      surf._w,surf._h,FormatName(surf._format),surf.SizeUsed(),Name()
      );
#endif
    _surfaces[_levelLoaded].Free(Name(),false);
  }
  MemoryReleased(_levelLoaded,_levelLoaded+1);
}

static PacFormat DstFormat( PacFormat srcFormat, int dxt )
{
  // memory representation
  switch (srcFormat)
  {
  case PacARGB1555:
  case PacRGB565:
  case PacARGB4444:
  case PacAI88:
  case PacARGB8888:
  case PacP8:
    return PacARGB8888;
  case PacDXT1:
  case PacDXT2:
  case PacDXT3:
  case PacDXT4:
  case PacDXT5:
    return srcFormat;
  default:
    LogF("Unsupported source format %d",srcFormat);
    return srcFormat;
  }
}

void TextureD3D11::SetMipmapRange( int min, int max )
{
  LoadHeaders();
  // if there is some surface, it is not usable now,
  // because it already contains mipmaps
  ReleaseMemory(true);
  if( min<0 ) min=0;
  if( max>_nMipmaps-1 ) max=_nMipmaps-1;
  if( min>max ) min=max;
  // disable mipmaps before min and after max
  _largestUsed = min;
  SetNMipmaps(max+1);

  if (_smallLevel<_largestUsed) _smallLevel=_largestUsed;
  UpdateMaxTexDetail();
}

/*!
\patch_internal 1.05 Date 7/16/2001 by Ondra
- Fixed: Older alpha textures without alpha taggs were not recognized,
resulting in bad renderstates while drawing. Alpha artifacts were visible.
- bug introduced while implementing JPG support.
*/

int TextureD3D11::Init(const char *name, bool caseSensitive)
{
  SetName(name, caseSensitive);

  _maxSize=INT_MAX; // no texture size limit
  _usageType = TexObject;

  // quick check if source does exist
  ITextureSourceFactory *factory = SelectTextureSourceFactory(name);
  if (!factory || !factory->Check(name))
  {
    ErrorMessage(EMMissingData, "Cannot load texture %s.",(const char *)GetName());
    _nMipmaps=0;
    _levelLoaded = _levelActive = _smallLevel = _largestUsed = _nMipmaps;
    return -1;
  }
  PreloadHeaders();
  //DoInit();
  return 0;
}

/// sometimes we want to set some parameters even before the texture is loaded
class TextureSourceNotLoaded: public ITextureSource
{
  int _clampFlags;

public:
  //@{ implement ITextureSource
  virtual bool Init(PacLevelMem *mips, int maxMips) {Fail("Not loaded yet");return false;}
  virtual int GetMipmapCount() const {Fail("Not loaded yet");return 0;}
  virtual PacFormat GetFormat() const {Fail("Not loaded yet");return PacUnknown;}
  virtual TextureType GetTextureType() const {Fail("Not loaded yet");return TT_Diffuse;}
  virtual float GetMaxTexDetail(float maxTexDetail) const {Fail("Not loaded yet");return FLT_MAX;}
  virtual TexFilter GetMaxFilter() const {Fail("Not loaded yet");return TFAnizotropic16;}
  virtual bool IsAlpha() const {Fail("Not loaded yet");return false;}
  virtual bool IsAlphaNonOpaque() const {Fail("Not loaded yet");return false;}
  virtual bool IsTransparent() const {Fail("Not loaded yet");return false;}
  virtual bool IsLittleEndian() const {return false;}
  virtual bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const {Fail("Not loaded yet");return false;}
  virtual bool RequestMipmapData(
    const PacLevelMem *mips, int levelMin, int levelMax, FileRequestPriority prior
    ) const {Fail("Not loaded yet");return false;}
  virtual PackedColor GetAverageColor() const {Fail("Not loaded yet");return PackedWhite;}
  virtual PackedColor GetMaxColor() const {Fail("Not loaded yet");return PackedWhite;}
  virtual void ForceAlpha() {Fail("Not loaded yet");}
  virtual QFileTime GetTimestamp() const {Fail("Not loaded yet");return 0;}
  virtual void FlushHandles() {}
  virtual int GetClamp() const {return _clampFlags;}
  virtual void SetClamp(int clampFlags) {_clampFlags = clampFlags;}
};

int TextureD3D11::Init(ITextureSource *source)
{
  // no name - directly defined texture
  SetName("");

  // if there already exists a source, load parameters from it
  // this is done to prevent need to load the texture before calling SetClamp
  int clampFlags = clampFlags = _src ? _src->GetClamp() : 0;

  _src = source;

  if (_src->Init(_mipmaps,MAX_MIPMAPS))
  {
    DoInitSource();
    if (clampFlags) _src->SetClamp(clampFlags);
    MarkInitialized();
  }
  else
  {
    LogF("No init");
  }
  return 0;
}

TextureType TextureD3D11::Type() const
{
  if (HeadersReadyFast())
  {
    return _src ? _src->GetTextureType() : TT_Diffuse;
  }
  // if there is no source yet, ask the factory what the type is
  ITextureSourceFactory *factory = SelectTextureSourceFactory(GetName());
  return factory->GetTextureType(GetName());
}

TexFilter TextureD3D11::GetMaxFilter() const
{
  if (HeadersReadyFast())
  {
    return _src ? _src->GetMaxFilter() : TFAnizotropic16;
  }
  // if there is no source yet, ask the factory what the Max Filter is
  ITextureSourceFactory *factory = SelectTextureSourceFactory(GetName());
  return factory->GetMaxFilter(GetName());
}

void TextureD3D11::PreloadHeaders()
{
  // if headers are already loaded, there is no need to load them once again
  if (HeadersReadyFast()) return;
  ITextureSourceFactory *factory = SelectTextureSourceFactory(Name());
  if (factory->PreInit(Name(),this))
  {
    LoadHeaders();
  }
}

#if DEBUG_PRELOADING
bool TextureD3D11::DebugMe() const
{
  if (!strcmp(GetName(),"ca\\air2\\f35b\\data\\f35_nohq.paa"))
    return true;
  return false;
}
#endif

int TextureD3D11::ProcessingThreadId() const
{
  return EngineDD11::IAmVisualizeThread;
}

void TextureD3D11::RequestDone(RequestContext *context, RequestResult result)
{
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  // when an attempt is done to execute the handler on the wrong thread, redirect it

  // caution: is following correct?
  // _textureThread is called from the main thread only.
  // what if thread ownership changes before or during DoLoadHeaders is called?
  if (!CheckSameThread(bank->_textureThread))
  {
#if VIS_JOURNAL
    GEngineDD11->_visJournal.Add(Format("Async DoLoadHeaders %s",cc_cast(GetName())));
#endif
    GEngineDD11->_vis.DoLoadHeaders(this);
    return;
  }

  // note: initialization might be done in between,
  // because it was required by someone else
  //Log("%s: Headers loaded",(const char *)Name());
  // we cannot ignore canceled requests, as nobody would re-submit them
  if (!HeadersReadyFast())
  {
    DoLoadHeaders();
  }
  AssertSameThread(bank->_textureThread);
  base::RequestDone(context,result);
}


/*!
\patch_ 1.50 Date 4/15/2002 by Ondra
- Optimized: Texture loading is now postponed to "Get ready" screen.
*/

void TextureD3D11::DoLoadHeaders(bool canIgnore)
{
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  // when an attempt is done to execute the handler on the wrong thread, redirect it
  if (!CheckSameThread(bank->_textureThread))
  {
    // request loading only once for each texture - requesting it multiple times only floods the queue
    if (!_initializeRequested)
    {
      _initializeRequested = true;
#if VIS_JOURNAL
      GEngineDD11->_visJournal.Add(Format("Redirect DoLoadHeaders %s",cc_cast(GetName())));
#endif
      GEngineDD11->_vis.DoLoadHeaders(this);
    }
    else
    {
#if VIS_JOURNAL
      GEngineDD11->_visJournal.Add(Format("Ignore DoLoadHeaders %s",cc_cast(GetName())));
#endif
      DoAssert(canIgnore);
    }
    return;
  }

  Assert (!HeadersReadyFast());

  // if max. size is not set yet, auto-detect it
  if (_maxSize>=0x10000)
  {
    _maxSize = bank->MaxTextureSize(_usageType);
  }
  saturateMin(_maxSize,GEngineDD11->MaxTextureSize());

  // if there already exists a source, load parameters from it
  // this is done to prevent need to load the texture before calling SetClamp
  int clampFlags = clampFlags = _src ? _src->GetClamp() : 0;

  ITextureSourceFactory *factory = SelectTextureSourceFactory(Name());
  _src = factory ? factory->Create(Name(),_mipmaps,MAX_MIPMAPS) : NULL;
  DoInitSource();

  if (clampFlags && _src) _src->SetClamp(clampFlags);

  Assert (!HeadersReadyFast());
  MarkInitialized();
}

void TextureD3D11::DoInitSource()
{
  if (_src)
  {
    PacFormat format=BasicFormat(GetName());

    bool isPaa = ( format==PacARGB4444 );
    TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());

    //in.seekg(0,QIOS::beg);
    // .paa should start with format marker

#if REPORT_ALLOC>=20
    LogF("Init texture %s",(const char *)Name());
#endif
    format = _src->GetFormat();

    if
      (
      format==PacARGB4444 ||
      format==PacAI88 ||
      format==PacARGB8888
      )
    {
      _src->ForceAlpha();
    }


    int dxt = GEngineDD11->DXTSupport();
    PacFormat dFormat = DstFormat(format,dxt);

    if( !_src->IsTransparent() && _src->GetFormat()==PacARGB1555 )
    {
      if( bank->_engine->Can565() && !isPaa ) dFormat=PacRGB565;
    }

    _largestUsed = MAX_MIPMAPS+1;
    _smallLevel = MAX_MIPMAPS+1;
    int nMipmaps = _src->GetMipmapCount();

    Assert(nMipmaps <= MAX_MIPMAPS);

    int i;
    for( i=0; i<nMipmaps; i++ )
    {
      Assert(i < MAX_MIPMAPS);
      PacLevelMem &mip=_mipmaps[i];

      mip.SetDestFormat(dFormat,8);

      if (!mip.TooLarge(_maxSize))
      {
        if (_largestUsed>i) _largestUsed=i;
      }

      // If there is a DXT compression
      if (PacFormatIsDXT(dFormat))
      {
        // If the resolution is smaller than minimum
        if ((mip._w < MIN_MIP_SIZE) || (mip._h < MIN_MIP_SIZE))
        {
          LogF("Warning: Texture is DXT compressed, but its mipmap resolution is smaller than %d.", MIN_MIP_SIZE);
          break;
        }
      }
    }

    // if _nMipmaps was already reduced from initial MAX_MIPMAPS, respect it
    // this is done to prevent need to load the texture before calling ASetNMipmaps
    if (_nMipmaps>i) _nMipmaps=i;
    Assert(_nMipmaps <= MAX_MIPMAPS);

    int smallLevel;
    // find first mipmap large enough to be useful
    for( smallLevel=_nMipmaps-1; smallLevel>_largestUsed; smallLevel-- )
    {
      const PacLevelMem &mipTop = _mipmaps[smallLevel];
      int size = mipTop.MipmapSize(mipTop._dFormat,mipTop._w,mipTop._h);
      // 4/3 is used to calculate mipmap requirements
      // 2/3 is how much we require to be really used
      //if( size*4/3>=bank->_minTextureSize*2/3 ) break;
      // optimized to:
      if( size*2>=bank->_minTextureSize ) break;
    }
    _smallLevel = smallLevel;

    _levelNeededThisFrame=_levelNeededLastFrame=i; // no level loaded, no needed
    UpdateMaxTexDetail();
    UpdateLoadedTexDetail(_nMipmaps);

    return;
  } 

  ErrorMessage(EMMissingData, "Cannot load texture %s.",(const char *)GetName());
  _nMipmaps=0;
  _levelLoaded = _levelActive = _smallLevel = _largestUsed = _nMipmaps;
  return;
}

void TextureD3D11::DoUnloadHeaders()
{
  if (HeadersReadyFast())
  {
    // caution: not MT safe, should never be used while other threads can be using the texture
    _initialized = false;
    _initializeRequested = false;
    ReleaseMemory();
    _src.Free();
  }
}

void TextureD3D11::FlushHandles()
{
  if (_src) _src->FlushHandles();
}

void TextureD3D11::CheckForChangedFile()
{
  // unload the source
  if (_src) _src->FlushHandles();
  RStringB name = GetName();
  // procedural or unnamed texture cannot (and need not to) be reloaded
  if (name[0]==0 || name[0]=='#') return;
  // if the texture is not ready, there is no need to reload it
  if (!HeadersReadyFast()) return;
  QFileTime fileTime = QFBankQueryFunctions::TimeStamp(name);
  if (fileTime>GetTimestamp())
  {
    // we need to reload as much as possible
    DoUnloadHeaders();
    LoadHeaders();
  }
}

void TextureD3D11::LoadHeaders()
{
  if (HeadersReadyFast()) return;
  DoLoadHeaders();
}
Color TextureD3D11::GetColor() const
{
  LoadHeadersNV();
  return _src ? Color(_src->GetAverageColor())*Color(_src->GetMaxColor()) : HBlack;
}
Color TextureD3D11::GetMaxColor() const
{
  LoadHeadersNV();
  return _src ? _src->GetMaxColor() : HWhite;
}

Color TextureD3D11::GetPixel( int level, float u, float v ) const
{
  PROFILE_SCOPE_EX(txGPx,tex);
  LoadHeadersNV();
  // Reduce the level to the largest used texture
  level = max(level, (int)_largestUsed);

  if (!_src) return HWhite;

  PacLevelMem mip = _mipmaps[level];

  AutoArray<char> mem;
  int dataSize = PacLevelMem::MipmapSize(mip._dFormat,mip._w,mip._h);
  mem.Realloc(dataSize);
  mem.Resize(dataSize);

  // the pitch stored in the texture is determined by HW
  // we want compact layout here
  mip._pitch = dataSize/mip._h;

  // get data from the texture source
  _src->GetMipmapData(mem.Data(),mip,level,0);

  Color col=Color(mip.GetPixel(mem.Data(),u,v))*Color(_src->GetMaxColor());
  return col;
}

bool TextureD3D11::GetRect(
                          int level, int x, int y, int w, int h, PacFormat format, void *data
                          ) const
{
  PROFILE_SCOPE_EX(txGRt,tex);
  Assert(HeadersReadyFast());

  // open texture file
  if (!_src) return false;

  PacLevelMem mip = _mipmaps[level];

  // only whole surface is currently supported
  DoAssert(x==0);
  DoAssert(y==0);
  DoAssert(w==mip._w);
  DoAssert(h==mip._h);
  // override default destination format
  mip._dFormat = format;

  int dataSize = PacLevelMem::MipmapSize(format,mip._w,mip._h);

  mip._pitch = dataSize/mip._h;

  _src->GetMipmapData(data,mip,level,0);

  return true;

}

bool TextureD3D11::RequestGetRect(int level, int x, int y, int w, int h) const
{
  if (!HeadersReadyFast()) return false;
  // open texture file
  if (!_src) return false;

  // only whole surface is currently supported
  FileRequestPriority prior = FileRequestPriority(10);
  return _src->RequestMipmapData(_mipmaps,level,level,prior);
}



DEFINE_FAST_ALLOCATOR(TextureD3D11);

TextureD3D11::TextureD3D11()
:_nMipmaps(MAX_MIPMAPS+1),
_levelLoaded(MAX_MIPMAPS+1),_largestUsed(0),_smallLevel(0),_levelActive(MAX_MIPMAPS+1),
_levelNeededThisFrame(0),_levelNeededLastFrame(0), // no level loaded, all needed
_inUse(0),
_maxSize(256),_usageType(TexObject),
_initialized(false),_initializeRequested(false),_permanent(false),_performance(0)
{
#ifdef TRAP_FOR_0x8000
#if 1
  for (int i=0;i<32; i++)
  {
    _dummyInts[i]=0xabcdef78;
  }
#endif
#endif
}

TextureD3D11::~TextureD3D11()
{
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  if (GEngineDD11)
  {
    if (_loadingJob)
    {
      // we cannot destroy while there is a job waiting to fill the texture
      // waiting should be quick, texture is AddRefed while the job is waiting
      GJobManager.CancelJob(_loadingJob);
      GJobManager.Wait(_loadingJob);
      bank->RemoveLoadingJob(this);
      _loadingJob.Free();
    }
    GEngineDD11->TextureDestroyed(this);
  }
  DoAssert(_inUse<=0);
  bool store = !IsOutOfMemory();
  PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*bank);
  ReleaseMemory(store);
}

void TextureD3D11::OnUnused() const
{
  if (GetName().GetLength()>0 && GEngine && GEngine->TextBank())
  {
    TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
    PROTECT_TEXTURE_MEMORY_ACCESS_BANK(*bank);
    bank->_texture.Remove(GetName());
  }
  base::OnUnused();
}

void TextureD3D11::SetUsageType( UsageType type )
{
  UsageType oldType = _usageType;
  // if the same texture is used multiple times, highest usage type should apply
  if (type>_usageType) _usageType = type;
  // depending on usage type determine the max size
  if (oldType!=_usageType)
  {
    AdjustMaxSize();
  }
}

void TextureD3D11::SetClamp(int clampFlags)
{
  if (_src) _src->SetClamp(clampFlags);
  else
  {
    // if setting to zero, no need to set - zero is default
    if (clampFlags)
    {
      // create a dummy source, only to carry the flags
      // this is done to prevent need to load the texture now
      _src = new TextureSourceNotLoaded;
      _src->SetClamp(clampFlags);
    }
  }
}
void TextureD3D11::AdjustMaxSize()
{
  TextBankD3D11 *bank=static_cast<TextBankD3D11 *>(GEngine->TextBank());
  int size = bank->MaxTextureSize(_usageType);
  if (HeadersReadyFast())
  {
    // limit size by real texture size
    int maxSize = _mipmaps[0]._w;
    saturateMax(maxSize,_mipmaps[0]._h);
    saturateMin(size,maxSize);
    // set max. size
    _maxSize = size;
    saturateMin(_maxSize,GEngineDD11->MaxTextureSize());
    // scan for corresponding mipmap level
    int nMipmaps = _src ? _src->GetMipmapCount() : 0;
    if (nMipmaps<=0) return;
    _largestUsed = MAX_MIPMAPS+1;
    for (int i=0; i<nMipmaps; i++)
    {
      Assert(i < MAX_MIPMAPS);
      const PacLevelMem &mip=_mipmaps[i];

      if (!mip.TooLarge(_maxSize))
      {
        if (_largestUsed>i) _largestUsed=i;
      }

      // do not load too small mip-maps
      if( mip._w<MIN_MIP_SIZE ) break;
      if( mip._h<MIN_MIP_SIZE ) break;
    }
    if (_largestUsed > _nMipmaps-1) _largestUsed = _nMipmaps-1;
    if (_smallLevel < _largestUsed) _smallLevel = _largestUsed;
    UpdateMaxTexDetail();
    // make sure levels up to _largestUsed are not loaded
    if (_levelLoaded<_largestUsed)
    {
      ReduceTextureLevel(_largestUsed);
      DoAssert(_levelLoaded>=_largestUsed);
      // mipmaps which are not loaded can never be active
      if (_levelActive>_levelLoaded) _levelActive = _levelLoaded;
    }
  }
  else
  {
    _maxSize = size;
    saturateMin(_maxSize,GEngineDD11->MaxTextureSize());
  }
}

D3DSurfaceDestroyer11::D3DSurfaceDestroyer11()
{
}

D3DSurfaceDestroyer11::~D3DSurfaceDestroyer11()
{
  DestroyAll();
}


static int CmpSurfById( const D3DSurfaceToDestroy11 *s1, const D3DSurfaceToDestroy11 *s2 )
{
  // move small ID to the end
  return s2->_id-s1->_id;
}

void D3DSurfaceDestroyer11::Add(const SurfaceInfoD3D11 &surf)
{
#ifndef _XBOX
  if (surf.GetSurface()._texture)
  {
    D3DSurfaceToDestroy11 destroy;
    destroy._surface = surf.GetSurface();
    destroy._id = surf.GetCreationID();
    _surfaces.Add(destroy);
  }
#else
  Fail("No batch destroy on Xbox");
#endif
}

void D3DSurfaceDestroyer11::DestroyAll()
{
#ifndef _XBOX
  if (_surfaces.Size()<=0) return;
  {
    PROFILE_SCOPE_EX(txDAS,tex);
    QSort(_surfaces.Data(),_surfaces.Size(),CmpSurfById);
  }
#if REPORT_ALLOC>=10
  for (int i=0; i<_surfaces.Size(); i++)
  {
    D3DSurfaceToDestroy11 &surf = _surfaces[i];
    LogF("  destroy %x, id %d",surf._surface,surf._id);
  }
#endif

  PROFILE_SCOPE_EX(txDAC,tex);
  if (PROFILE_SCOPE_NAME(txDAC).IsActive())
  {
    PROFILE_SCOPE_NAME(txDAC).AddMoreInfo(Format("%d surfaces",_surfaces.Size()));
  }
#endif
}


int SurfaceInfoD3D11::CalculateSize
(
 MyD3DDeviceCB &dDraw, const TEXTUREDESC11 &desc, PacFormat format,
 int totalSize
 )
{
  if (totalSize>0) return totalSize;
  int nMipmaps = desc.nMipmaps;
  int cTotalSize=0;
  int size=PacLevelMem::MipmapSize(format,desc.w,desc.h);
  for( int level=nMipmaps; --level>=0; )
  {
    cTotalSize+=size;
    size>>=2;
  }
  return cTotalSize;
}

#ifndef _XBOX
int SurfaceInfoD3D11::_nextId;
#endif

HRESULT SurfaceInfoD3D11::CreateSurface(
                                       EngineDD11 *engine, MyD3DDeviceCB &dDraw, const TEXTUREDESC11 &desc, PacFormat format, int totalSize
                                       )
{
  if (_surface._texture)
  {
    Fail("Non-free surface created.");
  }

  _w = desc.w;
  _h = desc.h;
#if _HELISIM_RENDER_TO_TEXTURE
  _isR2T = desc.useAsRT;
#endif
  _nMipmaps = desc.nMipmaps;
  _isSRGB = desc.isSRGB;
  {
    int cTotalSize=0;
    int size=PacLevelMem::MipmapSize(format,_w,_h); // assume 16-b textures
    for( int level=_nMipmaps; --level>=0; )
    {
      cTotalSize+=size;
      size>>=2;
    }
    if( totalSize<0 ) totalSize=cTotalSize;
    Assert( totalSize==cTotalSize );
  }
  _totalSize=totalSize;
  _d3dFormat=desc.format;

  bool cpuAccess = desc.usage!=D3D11_USAGE_DEFAULT;
  EngineDD11::CheckMemTypeScope scope(engine,cpuAccess ? "VIDtex" : "SYStex",totalSize);
  
  PROFILE_SCOPE_EX(texCT,tex);

  bool createRTView = false;
#if _HELISIM_RENDER_TO_TEXTURE
  createRTView = desc.useAsRT;
#endif

  // Create the texture
  HRESULT err = GEngineDD11->CreateTexture(_w, _h, _nMipmaps, desc.format, desc.usage, _surface, true, createRTView);

  if (err!=S_OK)
  {
    LogF("Dimensions %d:%d,%d",_w,_h,_nMipmaps);
    GEngineDD11->DDError11("Create Surface Error",err);
  }
  else
  { 

#if TRACK_TEXTURES
    if (InterestedInTexture(CreatingSurface))
    {
      LogF(
        "@@@ %p: CreateTexture %s (%dx%d,%d), %s",
        _surface.GetRef(),
        cc_cast(CreatingSurface),_w,_h,_nMipmaps,desc.pool==D3DPOOL_SYSTEMMEM ? "SYS" : "VRAM"
        );
    }
#endif

    _usedSize = engine->AlignVRAMTex(totalSize);
    _id = _nextId++;

  }
#if REPORT_ALLOC>=10
  if( err==S_OK )
  {
    const char *mem="VDA";
    if( desc.usage==D3D11_USAGE_DEFAULT ) mem="VID";
    else mem="SYS";
    LogF("%s: Created %dx%dx%s (%dB)",mem,_w,_h,FormatName(format),SizeUsed());
  }
#endif
  return err;
}

#if ENCAPSULATE_TEXTURE_REF
void SurfaceInfoD3D11::Copy(const SurfaceInfoD3D11 &src)
{
  // TODO: remove
  // memberwise copy is all we need
  _surface = src._surface;
  _totalSize = src._totalSize;
  _usedSize = src._usedSize;
  _w = src._w;
  _h = src._h;
  _nMipmaps = src._nMipmaps;
  _d3dFormat = src._d3dFormat;

}

TextureHandle9::~TextureHandle9()
{
#ifdef _XBOX
  if (_physicalMemory)
  {
    // wait until GPU no longer uses the resource
    if (_surface->IsBusy())
    {
      LOG_WAIT_GPU("texture",_surface);
      _surface->BlockUntilNotBusy();
    }
    // we do not attempt to match all attributes here
    DWORD protect = XALLOC_MEMPROTECT_READWRITE;
    DWORD attr = MAKE_XALLOC_ATTRIBUTES(
      0,FALSE,FALSE,FALSE,eXALLOCAllocatorId_D3D,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,
      protect,FALSE,XALLOC_MEMTYPE_PHYSICAL
      );
    XMemFree(_physicalMemory,attr);
    _physicalMemory = NULL;
    _surface.Free();
  }
  else
  {
    Fail("Impossible - no surface");
    _surface.Unlink();
  }
#else
  _surface.Free();
#endif
}
#endif

/**
@param name passed for debugging purposes only
*/
void SurfaceInfoD3D11::Free(const char *name, bool lastRef, int refValue)
{
#if TRACK_TEXTURES
  if (lastRef && refValue==0)
  {
    if (InterestedInTexture(name))
    {
      LogF("Free last ref to %s:%p",name,_surface.GetRef());
    }
  }
  else
  {
    if (_surface)
    {
      if (InterestedInTexture(name))
      {
        LogF("Free  ref to %s:%p",name,_surface.GetRef());
      }
    }
  }
#endif
  //GEngineDD11->TextBankDD()->ReportTextures11("* Free [");
#if ENCAPSULATE_TEXTURE_REF
  _surface.Free();
#else
  PROFILE_DX_SCOPE(3dSFr);

  ID3D11Texture2D* surf = _surface._texture;

#if _HELISIM_RENDER_TO_TEXTURE
  if (_isR2T)
  {
    GEngineDD11->UnRegisterR2TSourceTexture(_surface);
  }
#endif

  _surface.Free();

  if (lastRef)
  {
#if REPORT_ALLOC>=10
    if (surf && refValue==0)
    {
      LogF("  destroy %x, id %d, size %d",surf,_id,_usedSize);
    }
#endif
    (void)surf;
  }
#endif
  //GEngineDD11->TextBankDD()->ReportTextures11("* Free ]");
  _w=0;
  _h=0;
  _nMipmaps=0;
  _totalSize=0;
  _usedSize = 0;
}

/*!
\patch 5090 Date 11/28/2006 by Flyman
- Fixed: Fixed bug in texture management. All textures could have been thrown away under certain conditions
*/
int TextBankD3D11::CreateVRAMSurface(
                                    SurfaceInfoD3D11 &surface, const TEXTUREDESC11 &desc, PacFormat format, int totalSize
                                    )
{
  // VRAM creation - mark it
  const TEXTUREDESC11 &ddsd = desc;

  MyD3DDeviceCB &dDraw=_engine->GetD3DDeviceRec();

#if 0 // _ENABLE_CHEATS
  static float randomFailure = 0.05f;
  bool pretendFailure = GRandGen.RandomValue()<randomFailure;
#else
  const bool pretendFailure = false;
#endif
  bool retry = false;
Retry:
  //ReportTextures11("* Create [");
  HRESULT err = pretendFailure ? E_FAIL : surface.CreateSurface(_engine,dDraw,ddsd,format,totalSize);

  //ReportTextures11("* Create ]");
  if (retry)
  {
    if (err!=S_OK)
    {
      _engine->DDError11("Create Surface Error",err);
    }
    else
    {
      LogF("Retry OK");
    }
  }

  if(err==E_OUTOFMEMORY)
  {
#ifdef _XBOX
    // first of all try discarding file cache
    if (FreeOnDemandSystemMemoryLowLevel(totalSize)>0)
    {
      retry = true;
      goto Retry;
    }
#endif
    // first try to deallocate some conventional memory
    int reserved = ForcedReserveMemory(totalSize);

    // Calculate how much memory we need to reserve in this case
    _freeTextureMemory = FreeTextureMemory();
    int reserve = _freeTextureMemory - reserved - totalSize;

    // Update the reserve (to omit the same situation later)
    if (reserve>_reserveTextureMemory)
    {
      // it seems we need more texture memory than expected
      _reserveTextureMemory = reserve;

      // Recalculate the _limitAlocatedVRAM used to invoke mipbias setting
      RecalculateTextureMemoryLimits();
    }

    if (_freeTextures.Size()<=0)
    {
      ReportTextures11("Forced Reserve report");
    }
    if( reserved )
    {
      retry = true;
      goto Retry;
    }
    RptF(
      "Cannot create video memory surface %s,%dx%d (size %d B), Error %x",
      cc_cast(FormatName(format)),ddsd.w,ddsd.h,totalSize,err
      );
    ReportMemoryStatusWithVRAM();
    _engine->RequestFlushMemoryCritical();
    return -1; // out of memory
  }
  // DX11 TODO
  //else if(err==D3DERR_OUTOFVIDEOMEMORY )
  //{
  //  // video memory error reported
  //  // first try to evict some video memory, than system memory
  //  Log("Out of video memory.");

  //  // Now we know we failed to get the memory (can be caused by fragmentation)
  //  // So first check out how much memory can we get
  //  int reserved = ForcedReserveMemory(totalSize);

  //  // Calculate how much memory we need to reserve in this case
  //  _freeTextureMemory = FreeTextureMemory();
  //  _engine->CheckLocalNonlocalTextureMemory();
  //  int reserve = _freeTextureMemory - reserved - totalSize;

  //  // Update the reserve (to avoid handling the same situation later)
  //  if (reserve>_reserveTextureMemory)
  //  {
  //    // it seems we need more texture memory than expected
  //    _reserveTextureMemory = reserve;

  //  }

  //  // Recalculate the _limitAlocatedVRAM used to invoke mipbias setting
  //  RecalculateTextureMemoryLimits();
  //  if (reserved>0)
  //  {
  //    retry = true;
  //    goto Retry;
  //  }
  //  RptF(
  //    "Cannot create video memory surface %s,%dx%d (size %d B), Error %x",
  //    cc_cast(FormatName(format)),ddsd.w,ddsd.h,totalSize,err
  //    );
  //  ReportMemoryStatusWithVRAM();
  //  _engine->RequestFlushMemoryCritical();
  //  return -1; // out of memory
  //}
  else if( err )
  {
    RptF(
      "Cannot create video memory surface %s,%dx%d (size %d B), Error %x",
      cc_cast(FormatName(format)),ddsd.w,ddsd.h,totalSize,err
      );
    ReportMemoryStatusWithVRAM();
    _engine->RequestFlushMemoryCritical();
    return -1;
  }
  Assert( err==S_OK );
  TextureAllocated(surface.SizeUsed(),surface.Slack());
  _totalSlack += surface.Slack();
  return 0;
}

int TextBankD3D11::CreateVRAMSurfaceSmart(
  SurfaceInfoD3D11 &surface,
  const TEXTUREDESC11 &desc, PacFormat format, int totalSize, int mipImprovement
  )
{
  PROFILE_SCOPE_EX(txCVS,tex);
  PROTECT_TEXTURE_MEMORY_ACCESS();

  UseReleased(surface,desc);

  Assert(!surface.GetSurface()._texture || surface._nMipmaps==desc.nMipmaps);

  if(!surface.GetSurface()._texture)
  {
    // do not try reusing until used nearly all video memory
    int totAlloc = _totalAllocated+(int)_engine->UsedVRAM();
    if (totAlloc>_limitAlocatedVRAM/16*15)
    {
      // avoid hasty release of useful data
      // if there are too many free textures, release some of them first
      // if is possible that they are in some format which is not very common
      if (_freeAllocated<totAlloc/16*2)
      {
        Reuse(surface, desc, format, true, mipImprovement);
        Assert(!surface.GetSurface()._texture || surface._nMipmaps==desc.nMipmaps);
      }
    }

    if (!surface.GetSurface()._texture)
    {
      // if nothing can be reused, we have to create something new
      int neededSize = _engine->AlignVRAMTex(totalSize);
      int minAge = 3;
      ReserveMemory(neededSize,minAge);

#ifndef _XBOX
      if (totAlloc+neededSize>_limitAlocatedVRAM)
      {
        int maxAllocAllowed = _limitAlocatedVRAM-neededSize;

        if (
          ReserveMemoryLimit(maxAllocAllowed,minAge)<=0 &&
          !LimitMemoryUsingMipBias(maxAllocAllowed,mipImprovement)
          )
        {
          LogF("CreateVRAMSurface throttled (%d,%d)",neededSize,_totalAllocated);
          return -1;
        }
      }
#endif

      if( CreateVRAMSurface(surface,desc,format,totalSize)<0 )
      {
        return -2;
      }
      Assert(surface._nMipmaps==desc.nMipmaps);

      _thisFrameAlloc++;

      ADD_COUNTER(tGRAM,totalSize);
    }
    // we created or reused a texture
  }

  Assert(surface._nMipmaps==desc.nMipmaps);
  Assert(surface.SizeExpected()==totalSize);

  return 0;
}

bool TextureD3D11::VerifyChecksum( const MipInfo &absMip ) const
{
  return true;
}

// constructor

TextBankD3D11::TextBankD3D11(EngineDD11 *engine, ParamEntryPar cfg, ParamEntryPar fcfg)
:_totalAllocated(0),_totalSlack(0),
_systemAllocated(0),
_freeAllocated(0),
_freeOnDemand(_used),
_textureThread(0),_textureMemoryThread(0),
_checkForChangedFiles(false),_flushHandles(false)
{
  //RegisterFreeOnDemandSystemMemory(this);
  RegisterFreeOnDemandMemory(&_freeOnDemand);
#if _ENABLE_CHEATS
  _texStressMode = 0;
  _stressTestTotalSize = 0;
#endif

  // to avoid frequent system surface creation we maintain a pool of used surfaces
  _limitSystemTextures = 8*1024*1024;
  _reserveTextureMemory = 0;
  _engine=engine;

#ifdef _XBOX
#if 0 // _DEBUG
  _maxTextureMemory = 80*1024*1024;
#else
  _maxTextureMemory = 160*1024*1024;
  //_maxTextureMemory = 140*1024*1024;
  //_maxTextureMemory = 128*1024*1024;
#endif
#else
  _maxTextureMemory = INT_MAX;
#endif
  _maxTextureQuality = 2;
  _maxTextureVRAM = 2;
  _textureQuality = 1;
  _textureVRAM = 1;

  if (VRAMLimit>0) _maxTextureMemory = VRAMLimit;
  // will set _limitAlocatedVRAM and _freeTextureMemory
  _freeTextureMemory = FreeTextureMemory();
  RecalculateTextureMemoryLimits();

#ifndef _XBOX
  _maxTextureMemory = _freeTextureMemory;
#endif

  // avoid 
  _minTextureSize = 16;  
  ResetMaxTextureQuality(false);

  //PreCreateVRAM(512*1024,true);
}

TextBankD3D11::~TextBankD3D11()
{
  DeleteAllAnimated();

  _white.Free();
  _zero.Free();
  _random.Free();
  _defNormMap.Free();
  _defDetailMap.Free();
  _defMacroMap.Free();
  _defSpecularMap.Free();
  _defDTSpecularMap.Free();
  _permanent.Clear();
  _texture.CleanUp();
  struct LogTexture
  {
    bool operator () (TextureD3D11 *tex, const TextureList *list) const
    {
      if (!tex) return false;
      LogF("  not released: %s, %d refs",cc_cast(tex->GetName()),tex->RefCounter());
      return false;
    }
  };
  _texture.ForEachF(LogTexture());
  Assert (_texture.NItems()==0);
  _texture.Clear();
  //_ditherMap.Free();
  ClearTexCache();

  if (!IsOutOfMemory())
  {
    // use batch destroyed to achieve fast texture destruction
    // by destroying in reverse order to creation order
    // this is possible only when we have enough memory to store the batch
    D3DSurfaceDestroyer11 batch;
#ifndef _XBOX
    for( int i=0; i<_freeTextures.Size(); i++ )
    {
      batch.Add(_freeTextures[i]);
    }
    for( int i=0; i<_freeSysTextures.Size(); i++ )
    {
      batch.Add(_freeSysTextures[i]);
    }
    _freeSysTextures.Clear();
#else
    for( int i=0; i<_freeTextures.Size(); i++ )
    {
      _freeTextures[i].Free(NULL,true);
    }
#endif
    _freeAllocated = 0;
    _freeTextures.Clear();
    batch.DestroyAll();
  }
  else
  {
    _freeTextures.Clear();
    _freeAllocated = 0;
#ifndef _XBOX
    _freeSysTextures.Clear();
#endif
  }
}

void TextBankD3D11::Compact()
{
  //_texture.RemoveNulls();
}

void TextBankD3D11::StopAll()
{
  //_loader->DeleteAll();
}

void TextBankD3D11::RegisterVisualizeThread(int threadId)
{
  //Assert(_textureThread==0 || _textureThread==threadId); // make sure no conflicting registration exists yet
  _textureThread = threadId;
}

const char *SurfaceFormatNameDD11(int format);

static void SurfaceName( RString &buf, const SurfaceInfoD3D11 &surface )
{
  const char *name=SurfaceFormatNameDD11(surface._d3dFormat);
  buf = Format("\t%s,%d,%d,",name,surface._w,surface._h);
}

bool TextBankD3D11::VerifyChecksums()
{
  AssertMainThread();

  StatisticsByName stats;
  // print released textures statistics
  for( int i=0; i<_freeTextures.Size(); i++ )
  {
    const SurfaceInfoD3D11 &surface=_freeTextures[i];
    RString name;
    SurfaceName(name,surface);
    stats.Count(name);
  }
  LogF("Unused texture surfaces");
  stats.Report();
  stats.Clear();

  // print used textures statistics
  LogF("Used texture surfaces");
  struct LogSurface
  {
    StatisticsByName &_stats;

    LogSurface(StatisticsByName &stats):_stats(stats){}
    bool operator () (const TextureD3D11 *texture, const TextureList *list) const
    {
      if( !texture ) return false;
      if( texture->_surfaces[texture->_levelLoaded].GetSurface()._texture )
      {
        RString name;
        SurfaceName(name,texture->_surfaces[texture->_levelLoaded]);
        _stats.Count(name);
      }
      return false;
    }
  };
  _texture.ForEachF(LogSurface(stats));
  stats.Report();
  stats.Clear();

  /*
  int i;
  for( i=0; i<_texture.Size(); i++ ) 
  {
  Assert( _texture[i] );
  if( !_texture[i]->VerifyChecksum(NULL) )
  {
  Log("Texture %s checksum failed",_texture[i]->Name());
  return false;
  }
  }
  */
  return true;
}

#ifdef TRAP_FOR_0x8000
void TextBankD3D11::TrapFor0x8000(const char *str)
{
  static DWORD nextTime = 0;
  static bool triggered = false;
  if (!triggered)
  {
    struct TrapFor0x8000Func
    {
      const char *_str;
      bool *_triggered;

      TrapFor0x8000Func(const char *str, bool *trig):_str(str), _triggered(trig) {}
      bool operator () (const TextureD3D11 *texture, const TextureList *list) const
      {
        if( !texture ) return false;
        if ( texture->TrapFor0x8000(_str) )
        {
          *_triggered = true;  //but continue (show all occurrences)
          nextTime = GetTickCount() + 1000; //make a report each second
        }
        return false;
      }
    };
    _texture.ForEachF(TrapFor0x8000Func(str, &triggered));
  }
  else
  {
    if ( GetTickCount() > nextTime )
    {
      triggered = false; //process again
    }
  }
}
#endif

LLink<TextureD3D11> TextBankD3D11::Find(RStringB name1) const
{
  CHECK_TEXTURE_MEMORY_ACCESS();

  // before searching clear any zombies which may be hanging around
  // caution: this is O(N) !
  return _texture.Get(name1);
}

Ref<Texture> TextBankD3D11::CreateTexture(ITextureSource *source)
{
  Ref<TextureD3D11> texture = new TextureD3D11;
  if (!texture) return NULL;
  if (texture->Init(source)) return NULL;
  return texture.GetRef();
}

Ref<Texture> TextBankD3D11::Load(RStringB name, bool caseSensitive)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  LLink<TextureD3D11> tex = Find(name);
  if( tex.NotNull() ) return tex.GetLink();

#if _DEBUG
  if (strstr(name,"xfonts\\helveticanarrowb22-03"))
  {
    __asm nop; // break opportunity
  }
  else if (strstr(name,"xfonts\\helveticanarrowb22-"))
  {
    __asm nop; // break opportunity
  }
  else if (strstr(name,"xfonts\\"))
  {
    __asm nop; // break opportunity
  }
#endif

  //int iFree = _texture.FindKey(NULL);
  //if (iFree<0) iFree=_texture.Size();

  Ref<Texture> texture = LoadCached(name);
  if (!texture)
  {

    Ref<TextureD3D11> tex=new TextureD3D11;
    if (tex->Init(name, caseSensitive))
    {
      // set name to empty to avoid inserting in the cache
      tex->SetName(RStringB());
      return NULL;
    }
    texture = tex;
  }

  TextureD3D11 *txt=static_cast<TextureD3D11 *>(texture.GetRef());
  _texture.Add(LLink<TextureD3D11>(txt));
  Assert(!txt->HeadersReadyFast() || txt->_nMipmaps>0);

#if _DEBUG
  if (strstr(name,"xfonts\\helveticanarrowb22-03"))
  {
    __asm nop; // break opportunity
  }
  else if (strstr(name,"xfonts\\helveticanarrowb22-"))
  {
    __asm nop; // break opportunity
  }
  else if (strstr(name,"xfonts\\"))
  {
    __asm nop; // break opportunity
  }
#endif

#if DEBUG_PRELOADING
  if (txt->DebugMe())
    LogF(" - PreloadHeaders");
#endif
  txt->PreloadHeaders();

  return txt;
}

// maintain texture cache

void TextBankD3D11::ReleaseAllTextures(bool releaseSysMem)
{
  AssertMainThread();
  _engine->FinishPresent();
  _engine->FinishFinishPresent();

  LogF("Allocated before ReleaseAllTextures %d",_totalAllocated);
  if (_defDTSpecularMap) _defDTSpecularMap->ReleaseMemory();
  if (_defSpecularMap) _defSpecularMap->ReleaseMemory();
  if (_defMacroMap) _defMacroMap->ReleaseMemory();
  if (_defDetailMap) _defDetailMap->ReleaseMemory();
  if (_defNormMap) _defNormMap->ReleaseMemory();
  if (_random) _random->ReleaseMemory();
  if (_white) _white->ReleaseMemory();
  if (_zero) _zero->ReleaseMemory();
  if (_black) _black->ReleaseMemory();
  // release memory for all permanent textures
  for (int i=0; i<_permanent.Size(); i++)
  {
    _permanent[i]->ReleaseMemory();
  }
  ReserveMemoryLimit(0,0);
  LogF("Allocated after ReleaseAllTextures %d",_totalAllocated);
#if _ENABLE_REPORT
  if (_totalAllocated>0)
  {
    DoAssert(_used.First()==NULL);
    Fail("Flush did not flush everything");
    // scan what is allocated
    struct ReportSurface
    {
      bool operator () (TextureD3D11 *tex, const TextureList *list) const
      {
        if (!tex) return false;
        if (tex->_levelLoaded>=tex->_nMipmaps) return false;
        RptF("  allocated: %s (%d)",cc_cast(tex->Name()),tex->_levelLoaded);
        return false;
      }
    };
    _texture.ForEachF(ReportSurface());
    // check cached textures as well
    for (Texture *t = _cache.First(); t; t=_cache.Next(t))
    {
      TextureD3D11 *tex = static_cast<TextureD3D11 *>(t);
      if (tex && tex->GetBigSurface(false))
      {
        RptF("  allocated: %s (%d)",cc_cast(tex->Name()),tex->_levelLoaded);
      }
    }
    if( _freeTextures.Size()>0 )
    {
      RptF("FreeTextures: %d",_freeTextures.Size());
    }
    ReportTextures11(NULL);
  }
#endif
#ifndef _XBOX
  if (releaseSysMem)
  {
    ReserveSystem(0);
    if (_freeSysTextures.Size()>0)
    {
      Fail("Some free sys texture");
      _freeSysTextures.Clear();
    }
    if (_systemAllocated>0)
    {
      LogF("_systemAllocated %d",_systemAllocated);
    }
  }
#endif
#if _ENABLE_CHEATS
  _stressTest.Clear();
  _stressTestTotalSize=0;
#endif

}

void TextBankD3D11::ReleaseTexturesWithPerformance(int performance)
{
  _engine->EndHighLevelRecording();
  // scan what is allocated
  D3DSurfaceDestroyer11 batch;
  struct TexReleaseMemory
  {
    int _performance;
    D3DSurfaceDestroyer11 &_batch;

    TexReleaseMemory(int performance, D3DSurfaceDestroyer11 &batch):_performance(performance),_batch(batch){}

    bool operator () (TextureD3D11 *texture, const TextureList *list) const
    {
      if (texture && texture->GetBigSurface(false))
      {
        if (texture->_performance==_performance)
        {
          texture->ReleaseMemory(false,&_batch);
          LogF("Flushed %s",cc_cast(texture->GetName()));
        }
      }
      return false;
    }
  };
  _texture.ForEachF(TexReleaseMemory(performance,batch));
  // check cached textures as well
  for (Texture *tex = _cache.First(); tex; tex=_cache.Next(tex))
  {
    TextureD3D11 *texture = static_cast<TextureD3D11 *>(tex);
    if (texture && texture->GetBigSurface(false))
    {
      if (texture->_performance==performance)
      {
        texture->ReleaseMemory(false,&batch);
        LogF("Flushed %s",cc_cast(texture->GetName()));
      }
    }
  }
  if( _freeTextures.Size()>0 )
  {
    for( int i=0; i<_freeTextures.Size(); i++ )
    {
      batch.Add(_freeTextures[i]);
    }
  }
}

void TextBankD3D11::HandleRegularCleanup(int limit)
{
  D3DSurfaceDestroyer11 batchDestroy;
  bool someReleased=false;
  int totalReleased = 0;

  // we will try to release free textures first if possible
  if( _freeTextures.Size()>0 && _totalAllocated>limit)
  {
    // compute how much memory in free textures we may keep
    int freeLimit = _freeAllocated-(_totalAllocated-limit);
    while (_freeAllocated>freeLimit)
    {
      int released = DeleteLastReleased(&batchDestroy);
      // if there is nothing to release, break
      if (released<=0) break;
      totalReleased += released;
      someReleased=true;
    }
  }
}


/**
@return amount of memory released
*/
int TextBankD3D11::ReserveMemoryLimit(int limit, int minAge)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();

#define LOG_TIME 0
#if LOG_TIME
  DWORD start = GlobalTickCount();
#endif

#if SMALL_MIPS_READY
  HandleRegularCleanup(limit);
#endif

  D3DSurfaceDestroyer11 batchDestroy;
  bool someReleased=false;
  int totalReleased = 0;

  // release as many mip-map data as necessary to get required memory
  // get last mip-map used
  HMipCacheD3D11 *last=_used.First();
  while( TotalMemoryToLimit()>limit )
  {
    if( !last ) break;

    if (minAge>_used.FrameID()-last->GetFrameNum()) break;

    // release texture data
    TextureD3D11 *texture=last->texture;
    // when texture has requested its updating, we will not discard its levels equal or worse than the one in active use,
    // unless we want to discard even recent textures (minAge==0)
    if( texture->_inUse || minAge>0 && texture->IsInSList() && last->level>=texture->_levelActive)
    {
      last=_used.Next(last);
      continue;
    }
    int level = last->level;
    // release texture data from texture heap
    Assert(level>=0);
    Assert(level>=texture->_levelLoaded);
    Assert(level<texture->_nMipmaps);
    Assert( texture->_surfCache[level]==last );

#if _ENABLE_REPORT
    static int maxAgeReport = 50;
    if (last->GetFrameNum()+maxAgeReport>=_used.FrameID() && limit>0)
    {
      LogF(
        "Releasing recent texture %s:%d - age %d",
        cc_cast(texture->GetName()),level,
        _used.FrameID()-last->GetFrameNum()
        );
    }
#endif

    someReleased=true;
    // we need to handle small mipmaps as atomic
    int to = level+1;
    if (to>texture->_smallLevel) to = texture->_nMipmaps;
    DoAssert(texture->_levelLoaded<texture->_nMipmaps);
#if SMALL_MIPS_READY
    bool store = false;
#else
    // when small mips are not ready (Xbox), we need to avoid releasing textures directly
    // as that can cause a GPU stall
    bool store = true;
#endif
    totalReleased += texture->ReleaseLevelMemory(texture->_levelLoaded,to,store,&batchDestroy);
    // make sure this one is no longer here
    Assert(texture->_surfCache[level]==NULL);
    // if there is any surface, it needs to be tracked
#if _DEBUG
    Assert(texture->CheckIntegrity());
    for (int i=0; i<texture->_nMipmaps; i++)
    {
      if (!texture->_surfaces[i].IsNull())
      {
        Assert(i>=to);
      }
    }
#endif
    if (_used.First()==last)
    {
      RptF("Texture %s:%d..%d not released",cc_cast(texture->GetName()),texture->_levelLoaded,to);
      Fail("Infinite loop prevented in ReserveMemoryLimit");
      break;
    }
    last=_used.First();
  }
  if (someReleased)
  {
    batchDestroy.DestroyAll();
#if LOG_TIME
    DWORD end = GlobalTickCount();
    LogF("Time to release: %d",end-start);
#endif
  }
  else
  {
    //LogF("Nothing released: total %d, limit %d",_totalAllocated,limit);
    //LogF("  _freeTextures.Size() %d",_freeTextures.Size());
    // check if all textures are in use
  }
  return totalReleased;
}

static inline int CmpTexMemUsed(const InitPtr<TextureD3D11> *t1,  const InitPtr<TextureD3D11> *t2)
{
  const TextureD3D11 *tt1 = *t1;
  const TextureD3D11 *tt2 = *t2;
  int d= tt2->TotalVRAMUsed()-tt1->TotalVRAMUsed();
  if (d) return d;
  return strcmp(tt1->GetName(),tt2->GetName());
}

void TextBankD3D11::ReportTextures11( const char *name)
{
  // DX11 TODO
  Fail("DX11 TODO");
//
//#if _ENABLE_PERFLOG
//  // report all textures loaded into VRAM and sysRAM
//  LogFile file;
//#ifndef _XBOX
//  if (name && strchr(name,'.') && !strchr(name,'*') )
//  {
//    file.Open(name);
//  }
//  else
//#endif
//  {
//    file.AttachToDebugger();
//  }
//  // report some global information
//  {
//
//#if !defined(_XBOX) && !defined(_X360SHADERGEN)
//    MyD3DDevice11 &dDraw=_engine->GetDirect3DDevice();
//    UINT dwFree = dDraw->GetAvailableTextureMem();
//#else
//    UINT dwFree = 0;
//#endif
//
//    file << " Max " << _maxTextureMemory << " Free " << (int)dwFree << "\n";
//    file << "Allocated " << _totalAllocated << "\n";
//
//    int overhead = _maxTextureMemory - dwFree - _totalAllocated;
//    file << "Overhead " << overhead << "\n";
//  }
//
//
//  if (file.IsOpen())
//  {
//    int usedInBigTextures = 0;
//    int usedInFreeTextures = 0;
//    int slackInTextures = 0;
//    struct TexLogMem
//    {
//      int &_usedInBigTextures;
//      int &_slackInTextures;
//      TexLogMem(int &usedInBigTextures, int &slackInTextures)
//        :_usedInBigTextures(usedInBigTextures),_slackInTextures(slackInTextures)
//      {}
//
//      bool operator () (TextureD3D11 *texture, const TextureList *list) const
//      {
//        if (!texture) return false;
//        for (int i = texture->_levelLoaded; i < texture->_nMipmaps; i++)
//        {
//          if (texture->_surfaces[i].GetSurface())
//          {
//            _usedInBigTextures += texture->_surfaces[i].SizeUsed();
//            _slackInTextures += texture->_surfaces[i].SizeUsed()-texture->_surfaces[i].SizeExpected();
//          }
//        }
//        return false;
//      }
//    };
//    _texture.ForEachF(TexLogMem(usedInBigTextures,slackInTextures));
//    for (int i=0; i<_freeTextures.Size(); i++)
//    {
//      SurfaceInfoD3D11 &free = _freeTextures[i];
//      if (free.GetSurface()) usedInFreeTextures+=free.SizeUsed();
//    }
//    int usedInTextures = usedInBigTextures + usedInFreeTextures;
//    file << "Used in big   textures: " << usedInBigTextures   << "\n";
//    file << "Used in free  textures: " << usedInFreeTextures  << "\n";
//    file << "Used  (total)          : " << usedInTextures     << "\n";
//    file << "Slack (total)          : " << slackInTextures    << "\n";
//
//    AutoArray< InitPtr<TextureD3D11> > list;
//
//    struct TexLevelLoaded
//    {
//      AutoArray< InitPtr<TextureD3D11> > &_list;
//      TexLevelLoaded(AutoArray< InitPtr<TextureD3D11> > &list):_list(list){}
//
//      bool operator () (TextureD3D11 *texture, const TextureList *list) const
//      {
//        if (!texture) return false;
//        if (!texture->HeadersReadyFast()) return false;
//        // report only loaded textures
//        if (texture->_levelLoaded<texture->_nMipmaps )
//        {
//          _list.Add(texture);
//        }
//        return false;
//      }
//    };
//    list.Realloc(_texture.NItems());
//    _texture.ForEachF(TexLevelLoaded(list));
//
//    // scan cache as well
//    for (Texture *tex = _cache.First(); tex; tex=_cache.Next(tex))
//    {
//      TextureD3D11 *texture = static_cast<TextureD3D11 *>(tex);
//      if (!texture) continue;
//      if (!texture->HeadersReadyFast()) continue;
//      // report only loaded textures
//      if (texture->_levelLoaded<texture->_nMipmaps )
//      {
//        list.Add(texture);
//      }
//    }
//
//
//    QSort(list,CmpTexMemUsed);
//
//    for (int i=0; i<list.Size(); i++)
//    {
//      TextureD3D11 *texture = list[i];
//      file.PrintF("%-32s",texture->Name());
//      file.PrintF(" mips: %d",texture->NMipmaps());
//      // detect which is or was needed based on cache entry analysis
//      int activeCache = texture->_nMipmaps;
//      for (int level=texture->_levelLoaded; level<=texture->_smallLevel; level++)
//      {
//        if (texture->_surfCache[level] && texture->_surfCache[level]->GetFrameNum()>=_used.FrameID()-1)
//        {
//          activeCache = level;
//          break;
//        }
//      }
//      if (texture->_permanent) activeCache = texture->_largestUsed;
//      // check which level is active based on "LevelNeeded" information
//      int activeNeeded = toIntFloor(texture->LevelNeeded());
//      saturate(activeNeeded,texture->_levelLoaded,texture->_nMipmaps);
//      file.PrintF(
//        " loaded %d, active %d (%d,%d), needed %.2f",
//        texture->_levelLoaded,texture->_levelActive,activeCache,activeNeeded,texture->LevelNeeded()
//        );
//      const PacLevelMem &mip = texture->_mipmaps[texture->_levelLoaded];
//      int used =0, expected = 0;
//      for (int i=0; i<texture->_nMipmaps; i++)
//      {
//        const SurfaceInfoD3D11 &surf = texture->_surfaces[i];
//        if (surf.GetSurface())
//        {
//          used += surf.SizeUsed();
//          expected += surf.SizeExpected();
//        }
//      }
//      file.PrintF(" %dx%d, memUsed: %d (%d)",mip._w,mip._h,used,expected);
//      file << "\n";
//    }
//
//  }
//#endif
}

void TextBankD3D11::ChangeMipBias(D3DMipCacheRoot11 &root, float change)
{
  for (HMipCacheD3D11 *item = root.Last(); item; item = root.Prev(item))
  {
    TextureD3D11 *texture=item->texture;
    texture->_levelNeededLastFrame += change;
    texture->_levelNeededThisFrame += change;
  }
}

float TextBankD3D11::FindWorstBias(D3DMipCacheRoot11 &root, float &maxBias, TextureD3D11 *&maxTex) const
{
  for (HMipCacheD3D11 *item = root.Last(); item; item = root.Prev(item))
  {
    // release texture data
    TextureD3D11 *texture = item->texture;
    if (texture->_inUse) continue;
    // if nothing is loaded, nothing can be reduced
    if (texture->_levelLoaded >= texture->_nMipmaps) continue;
    // if the very minimum is loaded, nothing can be reduced
    if (texture->_levelLoaded >= texture->_smallLevel) continue;
    float bias = texture->LevelNeeded() - texture->_levelLoaded;
    if (maxBias < bias) maxBias = bias, maxTex = texture;
  }
  return maxBias;
}

bool TextBankD3D11::LimitMemoryUsingMipBias(int maxAllocAllowed, int mipImprovement)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  bool ret = true;
  float origBias = GetTextureMipBias();
#if _ENABLE_REPORT
  bool biasChanged = false;
  int thisUsed0 = _used.MemoryControlled()+_engine->MemoryControlled();
#endif
  while(TotalMemoryToLimit() > maxAllocAllowed)
  {
    // find a texture which has most space between what is loaded and what is needed
    // (or find the texture, which has loaded closest to what is needed and can be discarded first)
    // all textures should 
    float worstBias = -FLT_MAX;
    TextureD3D11 *tex = NULL;
    FindWorstBias(_used,worstBias,tex);
    if (!tex)
    {
      ret = false;
      break;
    }
    // if worstBias is positive and large enough, there is no need to change bias
    // all we need to do is reducing the texture
    if (worstBias > 1)
    {
      // this probably will never happen, as such textures should be caught earlier
      // avoid discarding too much
      int toDiscard = toIntFloor(worstBias);
      if (toDiscard>mipImprovement) toDiscard = mipImprovement;
      if (toDiscard<1) toDiscard = 1;
      int tgtLevel = tex->_levelLoaded + toDiscard;
      if (!tex->ReduceTextureLevel(tgtLevel))
      {
        // if we cannot reduce, we can evict the texture completely
        tex->ReleaseMemory();
      }
      continue;
    }
    // we need to reduce mipbias so that this texture is evicted
    // this means adjusting bias to that worstBias becomes at least 1
    // change it a little bit more than necessary to increase chance
    // it will not be changed again very soon
    float oldBias = GetTextureMipBias();
    float newBias = floatMin(oldBias + (1 - worstBias) + 0.2f, 5);
    if (newBias > oldBias)
    {
      // it has no sense to degrade mipbias so that even the newly loaded texture is degraded
      if (newBias > origBias + mipImprovement)
      {
        break;
      }
      float changeBias = newBias - oldBias;

#if _ENABLE_REPORT
      biasChanged = true;
#endif
      SetTextureMipBias(newBias);
      // change needed values for all textures
      ChangeMipBias(_used,changeBias);
    }

    int tgtLevel = tex->_levelLoaded + 1;
    if (!tex->ReduceTextureLevel(tgtLevel))
    {
      // if we cannot reduce, we can evict the texture completely
      tex->ReleaseMemory();
    }
  }
#if _ENABLE_REPORT
  if (biasChanged)
  {
    int thisUsed = _used.MemoryControlled()+_engine->MemoryControlled();
    // we had to change bias - change it a little bit more
    LogF("Mip bias forced from %.3f to %.3f",origBias,GetTextureMipBias());
    LogF("  VRAM usage reduced %d->%d",thisUsed0,thisUsed);
    if (thisUsed0<_limitAlocatedVRAM/4*3)
    {
      LogF("Strange reduction");
    }
  }
#endif
  return ret;
}

float TextBankD3D11::VRAMDiscardMetrics() const
{
#if SMALL_MIPS_READY
  // if we have a free texture which is not busy, it should be always free to be discarded
  if (_freeTextures.Size()>0)
  {
#ifdef _XBOX
    if (!_freeTextures[0].GetSurface()->IsBusy())
#endif
    {
      // pretend the free texture is really really old
      return INT_MAX;
    }
  }
#endif
  HMipCacheD3D11 *item = _used.First();
  if (!item) return NULL;
  // check topmost entry age
  //TextureD3D11 *tex = item->texture;
  return _used.FrameID()-item->GetFrameNum();
}

int TextBankD3D11::VRAMDiscardCountAge(int minAge, int *totalSize) const
{
  int count = 0;
  int size = 0;

  for ( HMipCacheD3D11 *last=_used.First(); last; last = _used.Next(last))
  {
    if (minAge>_used.FrameID()-last->GetFrameNum()) break;

    // release texture data
    TextureD3D11 *texture=last->texture;

    count++;

#if SMALL_MIPS_READY
    size += texture->_surfaces[last->level].SizeUsed();
#else
    size += texture->_surfaces[texture->_levelLoaded].SizeUsed();
#endif
  }
  if (totalSize) *totalSize = size;
  return count;
}

int TextBankD3D11::ReserveMemory(int size, int minAge)
{
  PROFILE_SCOPE_EX(txRsM,tex);
  PROTECT_TEXTURE_MEMORY_ACCESS();
  // note: while reducing may free some memory, it needs some allocation to be done
  // check size
  // never free this&last frame textures
  // try to keep some memory (1/64 of total space) free to avoid
  // out of memory conditions
  // first divide, multiply later (to avoid 32b overflow)
  int maxVRAMAllowed = _limitAlocatedVRAM/64*63-_engine->AlignVRAMTex(size);
  if (size>=INT_MAX)
  {
    // all memory will be released - reset and start again
    _reserveTextureMemory = 0;
    maxVRAMAllowed = 0;
  }
  else if (minAge==0)
  {
    // minAge ==0 means forced memory reservation
    // this means something has reported D3DERR_OUTOFVIDEOMEMORY
    // in such situation we need to release something, otherwise we risk crash
    maxVRAMAllowed = _totalAllocated+(int)_engine->UsedVRAM()-size;

    // ignore requests to flush when there should be plenty of VRAM free (we see we are well under allowed limits)
    // releasing it would probably not help anyway
    // it is possible drivers contains a bug which makes them to report D3DERR_OUTOFVIDEOMEMORY
    // even when in fact they have a problem with virtual address space
    if (_totalAllocated<_limitAlocatedVRAM/4*3)
    {
      // before giving up, verify with DX9 there is enough VRAM free
      // if it is, do not attempt to flush textures
      if (FreeTextureMemoryReported()>size*4+8*1024*1024)
      {
        // when we will return 0, alternate recovery should take place (including releasing system memory)
        return 0;
      }
    }
  }
  // avoid underflow, as with conversion to unsigned negative would act as a huge positive
  if (maxVRAMAllowed<0) maxVRAMAllowed = 0;

  int free = 0;
  for(;;)
  {
    if (TotalMemoryToLimit()+(int)_engine->UsedVRAM()<=maxVRAMAllowed) break;

    float bMetrics = _engine->VRAMDiscardMetrics();
    float tMetrics = VRAMDiscardMetrics();

    int freeStep = 0;  
    if (bMetrics>=tMetrics)
    {
      freeStep = _engine->ThrottleVRAMAllocation(_engine->UsedVRAM()-1,minAge);
    }
    else
    {
      freeStep = ReserveMemoryLimit(TotalMemoryToLimit()-1,minAge);
    }

    _freeVRAMTurnOver += freeStep;
#if _ENABLE_REPORT
    static int limitTurnOver = 10*1024*1024;
    if (_freeVRAMTurnOver>limitTurnOver)
    {
      LogF("Suspicious extensive VRAM freeing");
      _freeVRAMTurnOver = 0;
    }
#endif

    if (freeStep<=0) break;
    free += freeStep;
  }

  return free + CheckMemoryAllocation();
}

int TextBankD3D11::CheckMemoryAllocation()
{
  // on some HW there is no use for local/nonlocal prediction
  if (!_engine->UseLocalPrediction())
  {
    return 0;
  }
  // note: never executed on Xbox, UseLocalPrediction always false

  // we want to make sure textures are not overfilling the local memory
  // if we see there is no local memory left and at the same time there are some very old textures
  // we should definitely consider evicting some of them
  const int safetyMargin = 4*1024*1024 + (_engine->GetLocalVramAmount()>>5);
  int free = 0;
  if (_engine->GetFreeLocalVramAmount()<safetyMargin)
  {
    int toFree = safetyMargin-_engine->GetFreeLocalVramAmount();
    // we want to evict some, however we do not want to evict anything recent
    const int evictTime = 15; // evict time in seconds
    int minAge = evictTime*50; // assume 50 fps
    if (_engine->GetDynamicVBNonLocal())
    {
      // do not evict vertex buffers - it would not help anyway
      // compute what the new limit should be?
      int loweredLimit = _totalAllocated-toFree;
      // the limit should never go below zero
      if (loweredLimit<0) loweredLimit = 0;
      // if there is no item older than minAge, ReserveMemoryLimit does nothing
      free = ReserveMemoryLimit(_totalAllocated-toFree,minAge);
    }
    else
    {
      while (toFree>free)
      {
        int freeStep = 0;
        float bMetrics = _engine->VRAMDiscardMetrics();
        float tMetrics = VRAMDiscardMetrics();
        if (bMetrics>=tMetrics)
        {
          freeStep = _engine->ThrottleVRAMAllocation(_engine->UsedVRAM()-1,minAge);
        }
        else
        {
          freeStep = ReserveMemoryLimit(_totalAllocated-1,minAge);
        }
        if (freeStep<=0) break;
        free += freeStep;
      }
    }
#if _ENABLE_CHEATS
    //LogF("Local VRAM flush: %d KB, requested %d KB",(free+1023)/1024,(toFree+1023)/1024);
#endif
    if (free<toFree)
    {
      DIAG_MESSAGE(1000,"VRAM overflow detected");
    }
  }
  return free;
}

// assume driver will align RAM to some reasonable number
// 256 is experimental value for GeForce
// use GetAvailableVidMem after allocating very small texture to check this
// consider: this could be done run-time

// ATI seems to be using 4096 alignment

int TextBankD3D11::ForcedReserveMemory( int size, bool enableSysFree )
{
  // TODO: something is bad. We should definitely consider reset unless it was done recently

  const int minRelease=4*1024;
  if( size<minRelease ) size=minRelease; // force some minimal release
  int ret = ReserveMemory(size,0);

  LogF("ForcedReserveMemory total allocated %d B, released %d B",_totalAllocated,ret);

#if REPORT_ALLOC>=10
  int freemem = FreeTextureMemory();

  LogF(
    "VID Forced %d, free %d, allocated %d, limit %d",
    size,freemem,_totalAllocated+_engine->UsedVRAM(),_limitAlocatedVRAM
    );
#endif

  return ret;
}

int TextBankD3D11::ReserveSystemLimit(int limit)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  PROFILE_SCOPE_EX(texRS,tex);
  // release as many mip-map data as neccessary to get required memory
  D3DSurfaceDestroyer11 batchDestroy;
  D3DSurfaceDestroyer11 *batch = IsOutOfMemory() ? NULL : &batchDestroy;

  bool someReleased=false;
  int totalReleased = 0;
#if REPORT_ALLOC>=10
  if (_systemAllocated>limit)
  {
    LogF("Allocated %d, limit %d",_systemAllocated,limit);
  }
#endif
  while( _freeSysTextures.Size()>0 && _systemAllocated>limit )
  {
    // free more than one at once
    int need=_systemAllocated-limit;
    totalReleased += DeleteLastSystem(need,batch);
    someReleased=true;
  }
  return totalReleased;
}

int TextBankD3D11::ReserveSystem( int size )
{
  return ReserveSystemLimit(_limitSystemTextures-size);
}

int TextBankD3D11::ForcedReserveSystem( int size )
{
  const int minRelease=4*1024;
  if( size<minRelease ) size=minRelease; // force some minimal release
  return ReserveSystemLimit(_systemAllocated-size);
}


void TextBankD3D11::PerformAsyncMaintenance()
{
  if (!CheckSameThread(_textureThread)) return;
  if (_checkForChangedFiles)
  {
    DoCheckForChangedFiles();
    _checkForChangedFiles = false;
  }
  if (_flushHandles)
  {
    DoFlushHandles();
    _flushHandles = false;
  }
}
void TextBankD3D11::StartFrame()
{
  if (!_engine->ResetNeeded())
  {
    InitDetailTextures();
    // do not check texture memory each frame - this function can be quite slow
    //_freeTextureMemory = FreeTextureMemory();
    //RecalculateTextureMemoryLimits();

    StressTestFrame();

    PerformAsyncMaintenance();
  }
  _thisFrameAlloc = 0; // VRAM allocations/deallocations (count)
  _freeVRAMTurnOver = 0;
  _frameNotFinishedYet = true;

#if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_X))
  {
    _engine->UnsetAllDiscardableResources();
    ReserveMemory(INT_MAX,0);
    extern int MipmapLoadingDiags;
    MipmapLoadingDiags++;
    if (MipmapLoadingDiags>2) MipmapLoadingDiags = 0;
    static const char *text[]={"None","Used","Loaded"};
    GlobalShowMessage(500,"Mipmap Loading Diags %s",text[MipmapLoadingDiags]);

  }
#endif
}

TimeManager &TextBankD3D11::GetTexTimeManager()
{
  return CheckMainThread(_textureThread) ? GTimeManager : VisTimeManager;
}


void TextBankD3D11::PerformMipmapLoading()
{
  AssertSameThread(_textureThread);

  if (!CheckMainThread(_textureThread))
  {
    // high fps: as we are running in the background, we can spend more time than a main thread would
    float reserve = GTimeManager.GetTotalTime()*1.66f;
    // low fps: slow frames are most often caused by slow main thread - we can dedicate more time to background loading in such case
    float proportional = GEngine->GetSmoothFrameDuration()*0.15f;
    const float maxTime = 0.050f;
    VisTimeManager.Frame(floatMin(maxTime,floatMax(reserve,proportional)));
    VisTimeManager.Finished(TCPrepareShadow);
    VisTimeManager.Finished(TCLoadObjects);
    VisTimeManager.Finished(TCVisUpdate);
    VisTimeManager.Finished(TCAICenterMap);
    VisTimeManager.Finished(TCLoadMapObjects);
  }

  GFileServer->UpdateByThread(EngineDD11::IAmVisualizeThread);
  UseMipmap(_white,0,0);
  UseMipmap(_zero,0,0);
  UseMipmap(_random,0,0);
  UseMipmap(_defNormMap,0,0);
  UseMipmap(_defDetailMap,0,0);
  UseMipmap(_defMacroMap,0,0);
  UseMipmap(_defSpecularMap,0,0);
  UseMipmap(_defDTSpecularMap,0,0);

  for (int i=0; i<_permanent.Size(); i++)
  {
    UseMipmap(_permanent[i],0,0);
  }

  // load mipmaps for all textures which requested it
  // track what textures are still not loaded as needed
  // TODO: load textures with most missing information first
#if _ENABLE_CHEATS
  float maxMissingLevels = 0;
  TextureD3D11 *maxMissing = NULL;
  InitVal<int,0> missingMips[MAX_MIPMAPS];
#endif
  PROFILE_SCOPE_GRF_EX(txMLo,*,0);

  if (PROFILE_SCOPE_NAME(txMLo).IsActive())
  {
    PROFILE_SCOPE_NAME(txMLo).AddMoreInfo(Format("Time %.2f",GetTexTimeManager().TimeLeft(TCLoadTexture)*1000));
  }


  Assert(_engine->CheckAllDiscardableResourcesUnset());

  LockFreeGrowingList<Texture> toLoad;
  DoAssert(_loadRequested.CheckIntegrity());
  _loadRequested.GetAllReversed(toLoad);

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEStreaming) && GWorld)
  {
    // list all textures present on the given model (on its selected LOD)
    Object *obj = GWorld->GetCameraFocusObject();
    if (obj)
    {
#if _HELISIM_RENDER_TO_TEXTURE
      DoAssert(GScene->GetDrawObjectIndex() == 0);
      SortObject *so = obj->GetInList(GScene->GetDrawObjectIndex());
#else
      SortObject *so = obj->GetInList();
#endif
      LODShape *lShape = obj->GetShape();
      if (so && lShape)
      {
        DIAG_MESSAGE(100,
          "%d: %s, lod %d (%d,%d), shadow %d",
          safe_cast<int>(obj->ID()), cc_cast(lShape->GetName()),
          so->drawLOD,so->srcLOD,so->dstLOD,so->shadowLOD
          );
        LogF(
          "%d: %s, lod %d (%d,%d), shadow %d",
          safe_cast<int>(obj->ID()), cc_cast(lShape->GetName()),
          so->drawLOD,so->srcLOD,so->dstLOD,so->shadowLOD
          );
      }
    }
  }
#endif
  while(Ref<Texture> get = toLoad.Get())
  {
    TextureD3D11 *req = static_cast<TextureD3D11 *>(get.GetRef());
    req->RemoveFromSList();
    // remember current values, as UseMipmap will reset them
    float wantedAOTPerZ2 = req->_wantedAOTPerZ2;
    float preloadAOTPerZ2 = req->_preloadAOTPerZ2;
    MipInfo loaded = UseMipmap(req,INT_MAX,INT_MAX);
    // check what was requested
    // compare with what was loaded

#if _ENABLE_CHEATS
    // we are not interested in procedural textures here
    if (CHECK_DIAG(DEModel) && CHECK_DIAG(DEStreaming) && req->GetName()[0]!='#' && GWorld && GWorld->UI())
    {
      // list all textures present on the given model (on its selected LOD)
      Object *obj = GWorld->GetCameraFocusObject();
      if (obj)
      {
#if _HELISIM_RENDER_TO_TEXTURE
        DoAssert(GScene->GetDrawObjectIndex() == 0);
        SortObject *so = obj->GetInList(GScene->GetDrawObjectIndex());
#else
        SortObject *so = obj->GetInList();
#endif
        LODShape *lShape = obj->GetShape();
        if (so && lShape && so->dstLOD>=0 && so->dstLOD!=LOD_INVISIBLE)
        {
          int level = so->dstLOD;
          const Shape *shape = lShape->GetLevelLocked(level);
          if (shape && shape->IsOwningTexture(req))
          {
            // texture present on the model - display diags for it
            int mip = toIntFloor(req->_mipmapWanted);
            saturate(mip,0,req->NMipmaps()-1);
            // compute some interesting properties. We know AoT, therefore we should be able to compute texel size back
            float texels = req->AArea(mip);

            const Camera &camera = *GScene->GetCamera();
            // search AOT index for the model
            // or recompute AOT based on how we know screenAOT was computed
            // as we no longer track z2

            float aotW = wantedAOTPerZ2/camera.GetAOTFactor();
            float aotP = preloadAOTPerZ2/camera.GetAOTFactor();
            //float shapeAOT = shape->GetTextureAOT(req);
            // now convert back to pixels based on known screen size, distance and FOV
            float screenArea = 4*camera.Left()*camera.Top(); // 4x becase top/left is only half of the screen
            float screenPixels = GEngine->WidthBB()*GEngine->HeightBB();

            DIAG_MESSAGE_ID(
              0,(int)req,
              "  %s:%.2f (%.2f) - %dx%d, texel %.1f (%.1f) pixels (aot = %g (%g)/%g)",
              cc_cast(req->GetName()),req->_mipmapWanted,req->_mipmapPreload,req->AWidth(mip),req->AHeight(mip),
              sqrt(screenPixels/screenArea*aotW/texels),sqrt(screenPixels/screenArea*aotP/texels),
              wantedAOTPerZ2,preloadAOTPerZ2,camera.GetAOTFactor()
              );
          }
        }

      }
      // check 

    }
    //req->_levelNeededThisFrame;
    float levelsMissing = loaded._level-floatMax(req->_mipmapWanted,req->_largestUsed);
    if (levelsMissing>0)
    {
      int numMissing = toInt(levelsMissing);
      saturate(numMissing,0,MAX_MIPMAPS-1);
      missingMips[numMissing]++;

      if (levelsMissing>maxMissingLevels)
      {
        maxMissingLevels = levelsMissing;
        maxMissing = req;
      }
    }
#endif

  }

#if !SMALL_MIPS_READY
  // perform regular cleanup (Xbox)
  int limit = _limitAlocatedVRAM-_engine->UsedVRAM();
  HandleRegularCleanup(limit);
#endif

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEStreaming))
  {
    if (maxMissingLevels>0)
    {
      DIAG_MESSAGE(200,Format("Missing %s:%.2f",cc_cast(maxMissing->GetName()),maxMissingLevels));
      BString<256> histogram;
      for (int i=0; i<MAX_MIPMAPS; i++)
      {
        if (missingMips[i]>0)
        {
          sprintf(histogram+strlen(histogram),"%d: %d ",i,missingMips[i]);
        }
      }
      if (histogram[0]!=0)
      {
        DIAG_MESSAGE(200,Format("Missing levels %s",cc_cast(histogram)));
      }
    }
  }
  if (GInput.GetCheat2ToDo(DIK_E))
  {
    ReportTextures11(NULL);
  }
#endif

  { // if there are any very old loading jobs, discard them now, they are unlikely to be used
    // but if the number of jobs is low, just keep them, as they do little harm
    const int maxAge = 100;
    const int minCount = 8;
    ScopeLockSection lock(_loadingJobsCS);
    LLink<TextureD3D11> first;
    if (_loadingJobs.HeapGetFirst(first) && _loadingJobs.Size()>minCount)
    {
      if (!first)
      {
        _loadingJobs.HeapRemoveFirst();
      }
      else if (FreeOnDemandFrameID()-first->LoadingJobFrame()>maxAge)
      {
        first->_loadingJob.Free();
        _loadingJobs.HeapRemoveFirst();
      }
    }
  }

}

void TextBankD3D11::FinishFrame()
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  AssertSameThread(_textureThread);
  Assert(_frameNotFinishedYet);
  _frameNotFinishedYet = false;

  // handle texture lists
  PROFILE_SCOPE_EX(txHLi,*)

    //LogF("******* FinishFrame");
    // perform dynamic mip-bias adjustment now
    // basic criterion is - how much is used by _thisFrameWholeUsed
    // TODO: avoid CalculateMemory, calculate incrementally
    int thisUsed = _used.MemoryControlledRecent()+_engine->MemoryControlledRecent();


  // if no data are old enough, this is an indication we should reduce the VRAM working set
  // as controlling LOD would be difficult, we rather control mip bias only

  // if we have a data sufficiently old, this probably means we can reduce the bias
  int oldestItemAge = floatMin(VRAMDiscardMetrics(),_engine->VRAMDiscardMetrics());
  static int ageToReduceBias = 700;
  static int ageToGrowBias = 500;
  // if we have no old data and we are near the allocation limit, we need to increase the bias
  int oldTexSize = _used.MemoryControlled()-_used.MemoryControlledRecent();
  int oldVBSize = _engine->VRAMDiscardMemoryOld();


  // TODO: consider dynamic DX7 prediction results as well
  int maxAlloc = _limitAlocatedVRAM;
  int totAlloc = _totalAllocated+_engine->UsedVRAM();
  if (
    // no old enough items present
    oldestItemAge<ageToGrowBias
    // almost all memory used
    && totAlloc>=maxAlloc/8*7
    // and almost all is used recently
    && (oldTexSize+oldVBSize)<totAlloc/3
    )
  {
    float bias = GetTextureMipBias();
    // note: we must not grow the bias too fast, as by increasing the bias we are not discarding anything
    // therefore it is likely in following frames we will increase it again
    // it will take some time before we reach some settled state
    bias += 0.02f;
    saturateMin(bias,5.0f);
    SetTextureMipBias(bias);
    // discard something here to prevent following frames increasing further
    // compute how much texture memory we allow - subtract how much is overallocated from total amount of textures
    //int maxTextures = _totalAllocated-(totAlloc-maxAlloc/8*7);
    int maxTextures = maxAlloc/8*7-_engine->UsedVRAM(); // above, after substitution
    // avoid forcing nonsensical values
    if (maxTextures<maxAlloc*1/4) maxTextures = maxAlloc*1/4;
    // allow degrading 1 level
    LimitMemoryUsingMipBias(maxTextures,1.0f);
  }
  else if (oldestItemAge>ageToReduceBias && totAlloc<maxAlloc/2 && thisUsed<maxAlloc/4)
  {
    // plenty of spare space detected - reduce bias fast
    float bias = GetTextureMipBias();
    bias -= 0.005f;
    saturateMax(bias,GetTextureMipBiasMin());
    SetTextureMipBias(bias);
  }
  else if (oldestItemAge>ageToReduceBias && thisUsed<maxAlloc/2) // it does not matter what totAlloc there is
  {
    // some spare space detected - reduce bias slowly
    float bias = GetTextureMipBias();
    bias -= 0.001f;
    saturateMax(bias,GetTextureMipBiasMin());
    SetTextureMipBias(bias);
  }

  // make last and this frame textures history one frame older

  // we might verify previous textures are marked as unused

  int frameId = _used.FrameID();
#if _ENABLE_REPORT
  int maxIters = _used.ItemCount()+1;
#endif
  for( HMipCacheD3D11 *tc=_used.Last(); tc; tc=_used.Prev(tc) )
  {
    // no need to traverse textures which are not recently used
    if (frameId-tc->GetFrameNum()>2) break;
    TextureD3D11 *tex = tc->texture;
    tex->_levelNeededLastFrame = tex->_levelNeededThisFrame;
    tex->_levelNeededThisFrame = tex->_mipmapWanted;
#if _ENABLE_REPORT
    if (--maxIters<0)
    {
      Fail("_used list broken");
      // impossible to recover
      break;
    }
#endif
  }
  // the loop was separated to make sure _mipmapWanted is still valid
  // even if the same texture is traversed several times (several mipmaps)
  for( HMipCacheD3D11 *tc=_used.Last(); tc; tc=_used.Prev(tc) )
  {
    // no need to traverse textures which are not recently used
    if (frameId-tc->GetFrameNum()>2) break;
    TextureD3D11 *tex = tc->texture;
    tex->ResetMipmap();
  }

}

DEFINE_FAST_ALLOCATOR(HMipCacheD3D11);

size_t HMipCacheD3D11::GetMemoryControlled() const
{
  Assert(level>=0);
  Assert(level>=texture->_levelLoaded);
  Assert(level<texture->_nMipmaps);
  size_t size = texture->_surfaces[level].GetSurface()._texture ? texture->_surfaces[level].SizeUsed() : 0;
  Assert(size<INT_MAX && size>=0); // detect int range overflow
  Assert(size<64*1024*1024); // detect nonsense values
  return size;

}


size_t D3DMipCacheRoot11FreeOnDemand::FreeOneItem(){return NULL;}
float D3DMipCacheRoot11FreeOnDemand::Priority() const {return 1.0f;}
size_t D3DMipCacheRoot11FreeOnDemand::MemoryControlled() const {return 0;}
size_t D3DMipCacheRoot11FreeOnDemand::MemoryControlledRecent() const {return 0;}
RString D3DMipCacheRoot11FreeOnDemand::GetDebugName() const {return "";}
void D3DMipCacheRoot11FreeOnDemand::MemoryControlledFrame()
{
  // this is the reason why we implement MemoryFreeOnDemandHelper
  _list.Frame();
}

void TextureD3D11::CacheUse(TextBankD3D11 &bank, int level, int active)
{
  if (_permanent || _nMipmaps<=0)
  {
    return;
  }

  CHECK_TEXTURE_MEMORY_ACCESS_BANK(bank);

  // when we are improving or keeping the active level, we can always do so
  if (active<=_levelActive)
  {
    _levelActive = active;
  }
  else if (_levelActive<_nMipmaps && _surfCache[active]) // strange: _levelActive should be tested instead of active?
  {
    // sometimes we also need to make _levelActive worse
    // check how old the information there is
    if (_surfCache[active]->GetFrameNum()<bank._used.FrameID())
    {
      // the information was not recorded in the recent frame - degrade
      _levelActive = active;
    }
  }

  Assert(level<=active);
  // we insert more detailed mipmaps first - this way they should be also discarded first
  Assert(level<=_smallLevel || level>=_nMipmaps) ;
  Assert(_smallLevel<_nMipmaps || _nMipmaps==0);
  for (int i=level; i<=_smallLevel; i++)
  {
    if (_surfCache[i])
    {
      HMipCacheD3D11 *first = _surfCache[i];
      bank._used.Refresh(first);
    }
    else
    {
      HMipCacheD3D11 *first=new HMipCacheD3D11;
      first->texture = this;
      first->level = i;
      bank._used.Add(first);
      _surfCache[i] = first;
    }
#if TRACK_TEXTURES>1
    if (strstr(GetName(),InterestedIn))
    {
      LogF("@@@ Surface cached %s:%d",cc_cast(GetName()),i);
    }
#endif
  }
}

// assume: max reasonable allocations per frame
const int MaxAllocationsPerFrame=32;

MipInfo TextBankD3D11::UseMipmapLoaded(Texture *tex)
{
  if( !tex )
  {
    // with pixel shaders we need to override default black
    return MipInfo(NULL,0);
  }
  TextureD3D11 *texture=static_cast<TextureD3D11 *>(tex);
  // if headers are not here, return NULL
  if (!texture->HeadersReadyFast()) return MipInfo(NULL,0);
  //texture->LoadHeadersNV();
  return MipInfo(texture,texture->_levelLoaded);
}

/*!
\patch 1.03 Date 7/12/2001 by Ondra
- Fixed: crash when too low heap memory
\patch 5090 Date 11/24/2006 by Flyman
- Fixed: crash when out of videomemory
\patch 5090 Date 11/28/2006 by Flyman
- Changed: normal maps are not used when using "shading quality" "very low"
*/
MipInfo TextBankD3D11::UseMipmap(Texture *absTexture, int level, int top, int priority)
{
  if( !absTexture )
  {
    // with pixel shaders we need to override default black
    return MipInfo(NULL,0);
  }
  //CHECK_TEXTURE_MEMORY_ACCESS();
  PROTECT_TEXTURE_MEMORY_ACCESS();
  if (absTexture->LinkBidirWithFrameStore::IsInList())
  {
    // if the texture is cached, do not load it
    // cached texture can never be used on anything
    Assert(absTexture->RefCounter()<=0);
    return MipInfo(NULL,0);
  }
#if _ENABLE_REPORT
  if(absTexture->RefCounter()<=0)
  {
    static int maxMsg = 30;
    if (maxMsg>0)
    {
      RptF(
        "Texture %s not loaded properly when used (RefCount<=0)",
        cc_cast(absTexture->GetName())
        );
      maxMsg--;
    }
  }
#endif

  // level is the level we need
  // top is the level we would like to have
  // we are sure that texture is of type Texture
  // however this cast is potentially unsafe - take care
  TextureD3D11 *texture=static_cast<TextureD3D11 *>(absTexture);
  texture->LoadHeadersNV();

  Assert(texture->_inUse == 0);

  bool forceUse = level==top && level<INT_MAX || texture->_someDataNeeded;

  // if some level is explicitly required, we may not apply bias
  // level is necessary, top is wanted
  // as we are background loading, we can always load what is wanted
  level = top; // TODO: refactor level argument, it is used only in the comparison above

  int levelA = top;

  // some info may be stored in the wanted slots
  texture->ProcessAoTWanted();

  // no bias here - it was already applied during FindMipmapLevel
  float levelExact = floatMin(level,texture->_mipmapWanted);

  // subtract a small value to make sure 0 is not handled as 1
  const float eps = 0.0001;
  saturateMin(level, toIntFloor(floatMin(texture->_mipmapWanted,texture->_mipmapPreload)-eps));
  saturateMin(levelA, toIntFloor(texture->_mipmapWanted-eps));
  saturateMax(level,texture->_largestUsed);
  saturateMax(levelA,texture->_largestUsed);

#if _ENABLE_CHEATS
  if (!forceUse && DisableTextures)
  {
    texture->ReleaseMemory(true);
  }
#endif

  // If the content is not to be loaded, we don't need the texture

  // if some data are wanted or needed, make sure we load something even when the texture is very coarse
  if (forceUse || texture->_someDataWanted)
  {
    saturateMin(level,texture->_nMipmaps-1);
    saturateMin(levelA,texture->_nMipmaps-1);
  }
  else
  {
    saturateMin(level,texture->_nMipmaps);
    saturateMin(levelA,texture->_nMipmaps);
  }

  // when using something, never use mipmaps smaller than some limit
  if (level<texture->_nMipmaps) saturateMin(level,texture->_smallLevel);
  if (levelA<texture->_nMipmaps) saturateMin(levelA,texture->_smallLevel);

  DoAssert(level >= 0);

  // note: _levelNeededThisFrame is used for diagnostics and reduction only
  if (texture->_levelNeededThisFrame>levelExact)
  {
    texture->_levelNeededThisFrame = levelExact;
    //LogF("texture %s: need %d",texture->Name(),level);
  }

  Assert(texture->CheckIntegrity());

  Assert(level<=levelA);

  // when reset is requested, avoid loading any textures
  if (!_engine->ResetNeeded())
  {
    if (texture->_levelLoaded>level || texture->IgnoreLoadedLevels()  )
    {
      //     #if INTERESTED_IN_MIPMAPS
      //       if (strstr(texture->GetName(),InterestedInMipmaps))
      //       {
      //         LogF("%s: Loading level %d (%.2f)",cc_cast(texture->GetName()),level,levelExact);
      //       }
      //     #endif

      // level is not present - you have to load something-> load top

      // if request is ready to be satisfied, load it
      // avoid overloading the request manager
#if _ENABLE_CHEATS
      bool stop = StopLoadingTextures && LoadTexturesInFrame!=FreeOnDemandFrameID();
      bool loaded = forceUse || (( DisableTextures || stop ) ? false : texture->RequestLevels(level,priority));
#else
      bool loaded = forceUse || texture->RequestLevels(level,priority);
#endif
      // if there are no data yet, load (quick fill) something
      if (loaded || forceUse )
      {
#if INTERESTED_IN_MIPMAPS
        if (strstr(texture->GetName(),InterestedInMipmaps))
        {
          LogF("%s: Loading level %d (%.2f)",cc_cast(texture->GetName()),level,levelExact);
        }
#endif
        PROFILE_SCOPE_EX(texLd,*);
        if (PROFILE_SCOPE_NAME(texLd).IsActive())
        {
          RString logName = Format(
            "%s:%d%s%s%s",cc_cast(texture->GetName()),level,
            forceUse ? " Forced" : "",
            texture->_someDataNeeded ? " Needed" : "",
            texture->_someDataWanted ? " Wanted" : ""
            );
          // empty names means not inside of PBO - we are not interested then
          PROFILE_SCOPE_NAME(texLd).AddMoreInfo(logName);
        }
        for(;;)
        {
          DoAssert(level<texture->_nMipmaps);

          // check if source data are already present in the file cache
          int ret=texture->LoadLevels(level, texture->IgnoreLoadedLevels());
          if( ret>=0 )
          {
            break;
          }
          LogF("Out of VID: Try next level");
          level++;

          // Finish if we exceeded the smallLevel (at least _smallLevel we need to be loaded)
          if (level > texture->_smallLevel || level >= texture->_nMipmaps || level>=texture->_levelLoaded)
          {
            // Mark level it was not created
            if (level > texture->_smallLevel) level = texture->_nMipmaps;
            break;
          }
        }
        // for permanent textures CacheUse is not maintaining the "active" status
        if (texture->_permanent)
        {
          texture->_levelActive = texture->_levelLoaded;
        }
      }
      else
      {
        // return what is loaded
        level = texture->_levelLoaded;
        if (level>texture->_nMipmaps) level = texture->_nMipmaps;
      }
    }
    PROTECT_TEXTURE_MEMORY_ACCESS();
    // if nobody marked the cache as used (that means nobody moved the item to the cache start), do it now
    // never can be active what is not loaded
    saturateMax(levelA,texture->_levelLoaded);
    saturateMax(level,texture->_levelLoaded);
    texture->CacheUse(*this,level,levelA);
  }

  Assert(texture->CheckIntegrity());

  // if no level is loaded, average color will be used
  return MipInfo(texture,levelA); // given mip-map loaded
}

/*
void TextBankD3D11::ReleaseMipmap()
{
//Assert( CheckConsistency() );
//_loader->Unlock();
//Assert( CheckConsistency() );
}
*/

struct SurfDesc
{
  PacFormat format;
  int w,h;
  int n;
};

/*
static const SurfDesc PreloadDesc[]=
{
// data based on real session statistics
PacARGB1555,16,16,309,
};

const int NPreloadDesc=sizeof(PreloadDesc)/sizeof(*PreloadDesc);
*/
extern bool UseWindow;

static PacFormat SupportedFormat( PacFormat format )
{
  //if( format==PacRGB565 && !GEngineDD11->Can565() ) return PacARGB1555;
  if( format==PacRGB565 ) return PacARGB1555;
  return format;
}

void TextBankD3D11::MakePermanent(Texture *tex)
{
  // caution - we need to take care of permanent during Reset
  TextureD3D11 *texNat = static_cast<TextureD3D11 *>(tex);
  _permanent.Add(texNat);
  texNat->LoadHeaders();
  for (int i=0; i<texNat->_nMipmaps; i++)
  {
    DoAssert(texNat->_surfCache[i]==NULL);
  }
  texNat->_permanent = true;

  tex->PrepareMipmap0(true);
  //UseMipmap(tex,0,0);
}

void TextBankD3D11::InitDetailTextures()
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  if( !_white )
  {
    _white=new TextureD3D11;
    ITextureSource *source = CreateColorTextureSource(HWhite);
    _white->Init(source);
    _white->_permanent = true;
  }
  if( !_zero )
  {
    _zero=new TextureD3D11;
    ITextureSource *source = CreateColorTextureSource(Color(0,0,0,0));
    _zero->Init(source);
    _zero->_permanent = true;
  }

  if (!_black)
  {
    _black = new TextureD3D11;
    Color black(0.0, 0.0, 0.0);
    ITextureSource *source = CreateColorTextureSource(black);
    _black->Init(source);
    _black->_permanent = true;
  }

  if( !_random )
  {
    _random=new TextureD3D11;
    ITextureSource *source = CreateRandomTestTextureSource();
    _random->Init(source);
    _random->_permanent = true;
  }

  if( !_defNormMap )
  {
    _defNormMap=new TextureD3D11;
    Color neutral(0.5f,0.5f,1);
    ITextureSource *source = CreateColorTextureSource(neutral);
    _defNormMap->Init(source);
    _defNormMap->_permanent = true;
  }

  if( !_defDetailMap )
  {
    _defDetailMap=new TextureD3D11;
    Color neutral(0.5,0.5,0.5,0.5f);
    ITextureSource *source = CreateColorTextureSource(neutral);
    _defDetailMap->Init(source);
    _defDetailMap->_permanent = true;
  }

  if( !_defMacroMap )
  {
    _defMacroMap=new TextureD3D11;
    Color neutral(0.0,0.0,0.0,0.0f);
    ITextureSource *source = CreateColorTextureSource(neutral);
    _defMacroMap->Init(source);
    _defMacroMap->_permanent = true;
  }

  if( !_defSpecularMap )
  {
    _defSpecularMap=new TextureD3D11;
    Color neutral(1.0,0.0,0.0,1.0f);
    ITextureSource *source = CreateColorTextureSource(neutral);
    _defSpecularMap->Init(source);
    _defSpecularMap->_permanent = true;
  }

  if( !_defDTSpecularMap )
  {
    _defDTSpecularMap=new TextureD3D11;
    Color neutral(0.5,0.0,1.0,1.0f);
    ITextureSource *source = CreateColorTextureSource(neutral);
    _defDTSpecularMap->Init(source);
    _defDTSpecularMap->_permanent = true;
  }

  /*
  UseMipmap(_white,0,0);
  UseMipmap(_zero,0,0);
  UseMipmap(_black,0,0);
  UseMipmap(_random,0,0);
  UseMipmap(_defNormMap,0,0);
  UseMipmap(_defDetailMap,0,0);
  UseMipmap(_defMacroMap,0,0);
  UseMipmap(_defSpecularMap,0,0);
  UseMipmap(_defDTSpecularMap,0,0);

  for (int i=0; i<_permanent.Size(); i++)
  {
  UseMipmap(_permanent[i],0,0);
  }
  */
}

void TextBankD3D11::DoneDetailTextures()
{
  //  _white.Free();
  //  _zero.Free();
  //  _random.Free();
  //  _defNormMap.Free();
  //  _defDetailMap.Free();
  //  _defMacroMap.Free();
  //  _defSpecularMap.Free();
  //  _defDTSpecularMap.Free();
}

void TextBankD3D11::PreCreateVRAM( int reserve, bool randomOrder )
{
}

void TextBankD3D11::PreCreateSys()
{
}

bool TextBankD3D11::FlushTexture(const char *name)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  // check if the texture is cached
  RStringB fastName = name;
  FlushCached(fastName);
  // another texture with the same name but different type may be loaded
  // check if the texture is loaded
  const LLink<TextureD3D11> &tex = _texture.Get(fastName);
  if (_texture.NotNull(tex))
  {
    tex->ReleaseMemory(true);
    return false;
  }
  return true;
}

Ref<Texture> TextBankD3D11::SuspendTexture(const char *name)
{
  CHECK_TEXTURE_MEMORY_ACCESS();

  // check if the texture is cached
  RStringB fastName = name;
  FlushCached(fastName);
  // ???? another texture with the same name but different type may be loaded
  const LLink<TextureD3D11> &tex = _texture.Get(fastName);
  if (_texture.NotNull(tex))
  {
    tex->DoUnloadHeaders();
    return tex.GetLink();
  };
  return NULL;
}
void TextBankD3D11::ResumeTexture(Texture *tex)
{
  if (tex) tex->LoadHeaders();
}

const float TextBankD3D11::_textureMipBiasQuality[]=
{
  5.0f, // very low
  3.7f, // low
  2.0f, // normal
  1.5f, // high
  1.0f, // very high
  1.0f, // automatic (better than very high)
};

/// We expect around 50 MB may be needed for fragmentation overhead

const int vbVRAMNeeded = 50*1024*1024;

/**
Assume there is VRAM + AGP. We want to avoid using all VRAM,
because some memory is needed for framebuffers + render targets.
In case there will be slight overflow, we will use AGP, but this is much slower
*/

const int TextBankD3D11::_textureLimit[]=
{
#if 0 // _DEBUG
  /// 256+0 MB VRAM, 26 MB VRAM needed for FB + RT
  80*1024*1024,
  /// 256+128 MB VRAM, 32 MB VRAM needed for FB + RT
  90*1024*1024,
#else
  /// 256+0 MB VRAM, 26 MB VRAM needed for FB + RT
  230*1024*1024,
  /// 256+128 MB VRAM, 32 MB VRAM needed for FB + RT
  352*1024*1024,
#endif
  /// 512+0 MB VRAM, 42 MB VRAM needed for FB + RT
  470*1024*1024,
  /// 512+128 MB VRAM, 42 MB VRAM needed for FB + RT
  550*1024*1024,
  /// 512+0 MB VRAM, 42 MB VRAM needed for FB + RT
  590*1024*1024,
  /// 550+0 MB VRAM or better - automatic (better than very high)
  550*1024*1024,
};

const int TextBankD3D11::_textureMaxSize[][Texture::NUsageType]=
{
  //Obj,Land,Cock,UI,Custom(Sky)
  {512,512,1024,2048,1024}, // very low
  {512,512,1024,2048,1024}, // low
  {1024,1024,2048,2048,2048}, // normal
  {4096,4096,4096,4096,4096}, // high
  {4096,4096,4096,4096,4096}, // very high
  {4096,4096,4096,4096,4096}, // automatic (better than very high)
};

int TextBankD3D11::GetMaxTextureQuality() const
{
  return _maxTextureQuality;
}

void TextBankD3D11::SetTextureQuality(int value)
{
  saturate(value,0,lenof(_textureLimit)-2);

  _textureQuality = value;
  _textureMipBiasMin = _textureMipBiasQuality[value+1];
  SetTextureMipBias(_textureMipBiasMin);
}

int TextBankD3D11::GetTextureMemory() const
{
  return _textureVRAM;
}

int TextBankD3D11::GetMaxTextureMemory() const
{
  return _maxTextureVRAM;
}

int TextBankD3D11::SelectTextureMemory(int vram)
{
  vram -= vram/40 + 16*1024*1024; // assume 2.5% + 16 MB for back-buffers and render targets
  // no need to test the last one - we will use it as a fallback
  for (int i=0; i<lenof(_textureLimit)-1; i++)
  {
    if (vram<_textureLimit[i+1])
    {
      return i-1;
    }
  }
  return lenof(_textureLimit)-2;
}

void TextBankD3D11::SetTextureMemory(int value)
{
  saturate(value,0,lenof(_textureLimit)-2);

  _textureVRAM = value;

#ifndef _XBOX
  _maxTextureMemory = _textureLimit[_textureVRAM+1];
  // last value has a special meaning
  if (_textureVRAM+1==lenof(_textureLimit)-1)
  {
    // we want to use all local which was detected, if more than the limit we have
    // we need to keep something for render targets
    // on the other hand we mostly do not mind if some vertex buffers will end in AGP
    // still this setting will be interesting for 768 MB or 1 GB card, perhaps even for a 640 MB one
    int totalVRAM = _engine->GetLocalVramAmount();
    // 50 MB for render targets should be quite generous
    totalVRAM -= 50*1024*1024;
    if (_maxTextureMemory<totalVRAM) _maxTextureMemory = totalVRAM;
  }
#endif

  OnVRAMChanged();
}

void TextBankD3D11::SetTextureMemoryDiag(int value)
{
  _maxTextureMemory = value;

  OnVRAMChanged();
}


void TextBankD3D11::OnVRAMChanged()
{
  if (GEngine)
  {
    _engine->FinishPresent();
    _engine->FinishFinishPresent();
  }


  _freeTextureMemory = FreeTextureMemory();
  RecalculateTextureMemoryLimits();
  _engine->CheckLocalNonlocalTextureMemory();

  struct TexAdjSize
  {
    bool operator () (TextureD3D11 *texture, const TextureList *list) const
    {
      if (!texture) return false;
      texture->AdjustMaxSize();
      return false;
    }
  };
  _texture.ForEachF(TexAdjSize());

  // resize all textures as needed
  for (Texture *tex = _cache.First(); tex; tex=_cache.Next(tex))
  {
    tex->AdjustMaxSize();
  }

  // check if vertex buffers should be static based on VRAM/AGP stats

  int vramNeeded = _maxTextureMemory + vbVRAMNeeded;

  _engine->SetVBuffersStatic(_engine->GetLocalVramAmount() > vramNeeded);
}
RString TextBankD3D11::GetTextureDebugName(ID3D11Resource *handle) const
{
  CHECK_TEXTURE_MEMORY_ACCESS();

  struct TexFindHandleRes
  {
    RString name;
  };
  struct TexFindHandle
  {
    TexFindHandleRes &_res;
    ID3D11Resource *_tex;
    TexFindHandle(ID3D11Resource *tex, TexFindHandleRes &res):_tex(tex),_res(res){}

    bool operator () (TextureD3D11 *texture, const TextureList *list) const
    {
      if (!texture) return false;
      for (int i=0; i<texture->_nMipmaps; i++)
      {
        if (texture->_surfaces[i].GetSurface()._texture==_tex)
        {
          _res.name = Format("%s:%d",cc_cast(texture->GetName()),i);
          return true;
        }
      }
      return false;
    }
  };
  TexFindHandleRes res;
  _texture.ForEachF(TexFindHandle(handle,res));
  return res.name;
}

int TextBankD3D11::MaxTextureSize(Texture::UsageType type)
{
  int index = _textureQuality+1;
  saturate(index,0,lenof(_textureMaxSize)-1);
  return _textureMaxSize[index][type];
}


bool TextBankD3D11::CheckDeepFlushNeeded() const
{
  return _reserveTextureMemory>0;
}

void TextBankD3D11::FlushTextures(bool deep, bool allowAutoDeep)
{
  Compact();
  if (allowAutoDeep)
  {
    if (CheckDeepFlushNeeded())
    {
      // texture memory estimations went wrong - reinitialize them by flushing everything
      deep = true;
    }
  }
  if (deep)
  {
    // release all textures and vertex buffers
    // this prevents fragmentation building up
    _engine->UnsetAllDiscardableResources();
    ReserveMemory(INT_MAX,0);
  }
  // check current status of the local/nonlocal memory
  _engine->CheckLocalNonlocalTextureMemory();
}

void TextBankD3D11::FlushHandles()
{
  _flushHandles = true;
}

void TextBankD3D11::DoFlushHandles()
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  // if any source file has changed, reload it
  // TODO: scan cache as well
  struct TexFlushHandle
  {
    bool operator () (TextureD3D11 *texture, const TextureList *list) const
    {
      if (!texture) return false;
      texture->FlushHandles();
      return false;
    }
  };
  _texture.ForEachF(TexFlushHandle());
}
void TextBankD3D11::CheckForChangedFiles()
{
  _checkForChangedFiles = true;
}

void TextBankD3D11::DoCheckForChangedFiles()
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  // if any source file has changed, reload it
  // TODO: scan cache as well
  struct TexCheckFile
  {
    bool operator () (TextureD3D11 *texture, const TextureList *list) const
    {
      if (!texture) return false;
      texture->CheckForChangedFile();
      return false;
    }
  };
  _texture.ForEachF(TexCheckFile());
}

void TextBankD3D11::FlushBank(QFBank *bank)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  struct TexFlushBank
  {
    QFBank *_bank;

    TexFlushBank(QFBank *bank):_bank(bank){}

    bool operator () (const LLink <TextureD3D11> &texture, TextureList *list ) const
    {
      if (!texture) return false;
      // check if texture is from given bank
      if (_bank && !_bank->Contains(texture->GetName())) return false;
      list->Remove(texture->GetName());
      // only one texture of given name can exists
      return true;
    }
  };
  _texture.ForEachF(TexFlushBank(bank));
  // we need to flush the texture from the cache as well
  FlushBankFromCache(bank);
}

void TextBankD3D11::PreCreateSurfaces()
{
  //PreCreateVRAM(512*1024,true);
}

static int CompareTextureFileOrder(TextureD3D11 **tl1, TextureD3D11 **tl2)
{
  TextureD3D11 *t1 = *tl1;
  TextureD3D11 *t2 = *tl2;
  return QFBankQueryFunctions::FileOrder(t1->GetName(),t2->GetName());
}

#if _ENABLE_CHEATS
void DisplayTextureFileOrder(TextureD3D11 *t1)
{
  const char *n1 = t1->GetName();
  QFBank *b1 = QFBankQueryFunctions::AutoBank(t1->GetName());
  if (!b1) return;
  int o1 = b1->GetFileOrder(n1+strlen(b1->GetPrefix()));
  LogF("texture %s, order %d",n1,o1);
}
#endif

/*!
\patch 1.78 Date 7/23/2002 by Ondra
- Fixed: Random crash during program startup.
*/

void TextBankD3D11::Preload()
{

  DWORD start = GlobalTickCount();
  // sort textures bank / by order in bank
  typedef AutoArray<TextureD3D11 *, MemAllocLocal<TextureD3D11 *,4096> > ToLoadArray;
  ToLoadArray toLoad;
  {
    PROTECT_TEXTURE_MEMORY_ACCESS();
    Compact();
    struct TexList
    {
      ToLoadArray &_list;
      TexList(ToLoadArray &list):_list(list){}

      bool operator () (TextureD3D11 *texture, const TextureList *list) const
      {
        if (!texture) return false;
        if (!texture->HeadersReadyFast()) return false;
        // report only loaded textures
        if (texture->_levelLoaded<texture->_nMipmaps )
        {
          _list.Add(texture);
        }
        return false;
      }
    };

    _texture.ForEachF(TexList(toLoad));
  }
  QSort(toLoad.Data(),toLoad.Size(),CompareTextureFileOrder);
  // start preloading all headers
  for (int i=0; i<toLoad.Size(); i++)
  {
    TextureD3D11 *tex = toLoad[i];
    if (!tex) continue;
    tex->PreloadHeaders();
  }
  GFileServerSubmitRequestsMinimizeSeek();

  // load all headers
  for (int i=0; i<toLoad.Size(); i++)
  {
    TextureD3D11 *tex = toLoad[i];
    if (!tex) continue;
    tex->LoadHeadersNV();
    //DisplayTextureFileOrder(tex);
    ProgressRefresh();
  }
  LogF("Preload %d textures - %d ms",toLoad.Size(),GlobalTickCount()-start);
}

void TextBankD3D11::GetTextureList(LLinkArray<Texture> &list)
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  struct TexList
  {
    LLinkArray<Texture> &_list;
    TexList(LLinkArray<Texture> &list):_list(list){}

    bool operator () (TextureD3D11 *texture, const TextureList *list) const
    {
      if (!texture) return false;
      if (!texture->HeadersReadyFast()) return false;
      // report only loaded textures
      if (texture->_levelLoaded<texture->_nMipmaps )
      {
        _list.Add(texture);
      }
      return false;
    }
  };
  _texture.ForEachF(TexList(list));
}


size_t TextBankD3D11::GetMaxVRAMAllocation() const
{
  return _limitAlocatedVRAM;
}


/*!
\patch 5160 Date 5/23/2007 by Ondra
- New: Support for larger texture cache with new Texture Detail level, useful for cards with 640 MB VRAM or more.
*/
void TextBankD3D11::ResetMaxTextureQuality(bool complete)
{
  // check how much texture we have and how much can we allocate
  long long vramReported = (long long)FreeTextureMemoryReported()+_totalAllocated+_engine->UsedVRAM();
  // check if we can fit withing given limits
  // we know we can always fit into current quality
  // we can use additional local/nonlocal information as a hint

  int q;
  for (q=1; q<lenof(_textureLimit); q++)
  {
    int limit = _textureLimit[q];
    if (limit>vramReported && q>_textureQuality+1)
    {
      break;
    }
  }
  _maxTextureQuality = q-2;
  _maxTextureVRAM = q-2;

#ifdef _XBOX
  // force max. texture detail Low on X360
  // TODOX360: consider using better detail
  _maxTextureQuality = intMin(0,_maxTextureQuality);
#endif

  if (complete)
  {
    int oldMinTextureSize = _minTextureSize;
    // set small texture limit based on texture allocator granularity
    _minTextureSize = _engine->GetTexAlignment();
    // on Xbox we have custom allocator capable of allocating 256B aligned blocks
    // we assume PC video memory allocation is about the same
    // if the allocator forces us to use more than 256 KB, it is broken anyway
    // 8800
#if 0 // DEBUG || _PROFILE
    _minTextureSize = 16*16; // make texture bugs caused by bad mipmap handling more obvious for debugging
#endif
    saturate(_minTextureSize,16*16,512*512);
    if (oldMinTextureSize!=_minTextureSize)
    {
      LogF("Min texture size %d B",_minTextureSize);
    }
  }
}


int TextBankD3D11::FreeTextureMemory()
{
  PROTECT_TEXTURE_MEMORY_ACCESS();
  int vramReported = FreeTextureMemoryReported();
  int vramAssumed = _maxTextureMemory - _totalAllocated - _engine->UsedVRAM();
  return intMin(vramReported,vramAssumed);
}

void TextBankD3D11::FreeTextureMemoryReportedByDDraw(int &local, int &nonlocal)
{
  local = nonlocal = 0;
}


int TextBankD3D11::FreeTextureMemoryReported()
{
  // DX11 TODO
  DoAssert(_engine);
  if (_engine)
  {
    if (_engine->_freeLocalVramAmount > 0)
      return _engine->_freeLocalVramAmount;
    else if (_engine->_freeNonlocalVramAmount > 0)
      return _engine->_freeNonlocalVramAmount;
  }

  return 512*1024*1024;

//#if _ENABLE_CHEATS
//  // allow skipping the runtime free memory test
//  // used to simulate large VRAM on a card with little VRAM, or the other way round
//  if (VRAMLimit>0)
//  {
//    if (VRAMLimit!=_maxTextureMemory)
//    {
//      _maxTextureMemory = VRAMLimit;
//      // Recalculate the _limitAlocatedVRAM used to invoke mipbias setting
//      RecalculateTextureMemoryLimits();
//    }
//    return _maxTextureMemory - _totalAllocated - _engine->UsedVRAM();
//  }
//#endif
//
//#if defined(_XBOX) || defined(_X360SHADERGEN)
//  // always assume the same memory
//  return _maxTextureMemory - _totalAllocated - _engine->UsedVRAM();
//#else
//  PROFILE_SCOPE_EX(txRFM,tex);
//  MyD3DDevice11 &dDraw=_engine->GetDirect3DDevice();
//  UINT free = dDraw->GetAvailableTextureMem();
//  if (free>INT_MAX)
//  {
//    return INT_MAX;
//  }
//  return free;
//#endif
}

#if _ENABLE_CHEATS
static RString FormatByteSizeNoZero(size_t size)
{
  if (size) return FormatByteSize(size);
  return RString();
}
RString TextBankD3D11::GetStat(int statId, RString &statVal, RString &statVal2)
{
  int id = 0;
  if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(intMax(0,FreeTextureMemory()));
    statVal2 = FormatByteSizeNoZero(FreeTextureMemoryReported());
    return "Free VRAM Game/DX9" ;
  }
#ifndef _XBOX
  else if (statId==id++)
  {
    int local,nonlocal;
    FreeTextureMemoryReportedByDDraw(local,nonlocal);
    statVal = FormatByteSizeNoZero(local);
    statVal2 = FormatByteSizeNoZero(nonlocal);
    return "DX7 Free VRAM/AGP";
  }
  else if (statId==id++)
  {
    // note: based on experiments:
    //   on nVidia dynamic buffers go into non-local
    //   on ATI dynamic buffers go into local
    // it is always safer to assume everything goes into local
    // and be proven wrong once remeasured
    if (_engine->UseLocalPrediction())
    {
      int local = _engine->GetFreeLocalVramAmount();
      int nonlocal = _engine->GetFreeNonlocalVramAmount();
      if (local<0)
      {
        statVal = RString("-")+FormatByteSizeNoZero(-local);
      }
      else
      {
        statVal = FormatByteSizeNoZero(local);
      }
      statVal2 = FormatByteSizeNoZero(intMax(0,nonlocal));
      return "Est. free VRAM/AGP";
    }
    else
    {
      int totalVRAM = _engine->GetVRAMAllocLimit();
      int freeVRAM = totalVRAM - _totalAllocated-_engine->UsedVRAM();
      if (freeVRAM<0)
      {
        statVal = RString("-")+FormatByteSizeNoZero(-freeVRAM);
      }
      else
      {
        statVal = FormatByteSizeNoZero(freeVRAM);
      }
      statVal2 = FormatByteSizeNoZero(intMax(0,totalVRAM));
      return "Stat. VRAM free/max";

    }
  }
#endif
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_totalAllocated);
    statVal2 = FormatByteSizeNoZero(_totalAllocated+_engine->UsedVRAM());
    return "Alloc Tex/Tex+VBuf";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_used.MemoryControlled());
    statVal2 = FormatByteSizeNoZero(_used.MemoryControlledRecent());
    return "Tex Used/recent";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_engine->VRAMMemoryTotal());
    statVal2 = FormatByteSizeNoZero(_engine->VRAMMemoryRecent());
    return "VB Used/recent";
  }
  if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_freeAllocated);
    statVal2 = RString();
    return "Free";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_totalSlack);
    statVal2 = RString();
    return "Slack";
  }
  else if (statId==id++)
  {
    statVal = FormatByteSizeNoZero(_stressTestTotalSize);
    statVal2 = RString();
    return _stressTestTotalSize>0 ? "Stress" : " ";
  }
  else if (statId==id++)
  {
    statVal = RString();
    statVal2 = RString();
    return " ";
  }
  if (statId==id++)
  {
    statVal = Format("%.3f",GetTextureMipBias());
    statVal2 = Format("%.3f",GetTextureMipBiasMin());
    return "Mip bias cur/min";
  }
  else if (statId==id++)
  {
    PROTECT_TEXTURE_MEMORY_ACCESS();
    statVal = Format("T %g",VRAMDiscardMetrics());
    statVal2 = Format("V %g",_engine->VRAMDiscardMetrics());
    return "Age T/VB";
  }
  else if (statId==id++)
  {
    int size;
    statVal = Format("%d",VRAMDiscardCountAge(10,&size));
    statVal2 = FormatByteSize(size);
    return "T Age>10 #/mem";
  }
  else if (statId==id++)
  {
    int size;
    statVal = Format("%d",VRAMDiscardCountAge(500,&size));
    statVal2 = FormatByteSize(size);
    return "T Age>500 #/mem";
  }
  else if (statId==id++)
  {
    PROTECT_TEXTURE_MEMORY_ACCESS();
    int size;
    statVal = Format("%d",_engine->VRAMDiscardCountAge(10,&size));
    statVal2 = FormatByteSize(size);
    return "V Age>10 #/mem";
  }
  else if (statId==id++)
  {
    PROTECT_TEXTURE_MEMORY_ACCESS();
    int size;
    statVal = Format("%d",_engine->VRAMDiscardCountAge(500,&size));
    statVal2 = FormatByteSize(size);
    return "V Age>500 #/mem";
  }
  statVal = RString();
  statVal2 = RString();
  return RString();
}
#endif

void TextBankD3D11::RecalculateTextureMemoryLimits()
{
  _limitAlocatedVRAM = _freeTextureMemory + _totalAllocated + _engine->UsedVRAM();
  // check limit preventing us to overflow into AGP
  int maxVRAM = _engine->GetVRAMAllocLimit();
  if (_limitAlocatedVRAM>maxVRAM) _limitAlocatedVRAM = maxVRAM;
#if defined _XBOX && _ENABLE_CHEATS // used for low VRAM conditions testing on Xbox
  if (_limitAlocatedVRAM>_maxTextureMemory) _limitAlocatedVRAM = _maxTextureMemory;
#endif
  // if below certain limit, something very wrong has happened
  // do not degrade any more, let other systems handle it
  const int minTexVRAM = 32*1024*1024;
  if (_limitAlocatedVRAM<minTexVRAM) _limitAlocatedVRAM = minTexVRAM;
}

void TextBankD3D11::ClearVMStress()
{
#if _ENABLE_CHEATS
  for (int i=0; i<_stressTestVM.Size(); i++)
  {
    ::VirtualFree(_stressTestVM[i],0,MEM_RELEASE);
  }
  _stressTestVM.Clear();
#endif
}
bool TextBankD3D11::AddVMStress(int size)
{
#if _ENABLE_CHEATS
  void *vm = ::VirtualAlloc(NULL,size,MEM_RESERVE,PAGE_NOACCESS);
  if (vm)
  {
    _stressTestVM.Add(vm);
    return true;
  }
#endif
  return false;
}


void TextBankD3D11::ClearTexStress()
{
#if _ENABLE_CHEATS
  // release them all
  _stressTest.Clear();
  _stressTestTotalSize = 0;
#endif
}

void TextBankD3D11::AddTexStress(int size, bool renderTarget)
{
  Fail("DX10 TODO");
  // DX11 TODO
//
//#if _ENABLE_CHEATS
//  ComRef<IDirect3DTexture9> tex;
//
//
//  HRESULT err;
//  int pixelSize = 1;
//  if (!renderTarget)
//  {
//    err = _engine->GetDirect3DDevice()->CreateTexture(size, size, 1, 0, D3DFMT_DXT5, D3DPOOL_DEFAULT, tex.Init(), NULL);
//  }
//  else
//  {
//    pixelSize = 4;
//    err = _engine->CreateTextureRT(size, size, 1, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, tex, true);
//  }
//  if (SUCCEEDED(err))
//  {
//    _stressTest.Add(StressInfo(tex,size*size*pixelSize));
//    _stressTestTotalSize += size*size*pixelSize;
//    //LogF("Total stress: %s",cc_cast(FormatByteSize(_stressTestTotalSize)));
//  }
//  else
//  {
//    _engine->DDError11("Cannot create stress surface",err);
//  }
//#endif
}

void TextBankD3D11::SetStressMode(int mode)
{
#if _ENABLE_CHEATS
  _texStressMode = mode;
#endif
}

void TextBankD3D11::StressTestFrame()
{
  // DX11 TODO
//#if _ENABLE_CHEATS
//  if (_texStressMode==0) return;
//  // if stress mode positive, create a few textures
//  // if stress mode negative, destroy a few textures
//  int countCreat = _texStressMode>0 ? _texStressMode : -_texStressMode/10;
//  int countDestr = _texStressMode<0 ? -_texStressMode : _texStressMode/10;
//  saturateMax(countCreat,4);
//  saturateMax(countDestr,4);
//  for (int i=0; i<countDestr && _stressTest.Size()>0; i++)
//  {
//    // destroy a random one
//    int index = rand()%_stressTest.Size();
//    _stressTestTotalSize -= _stressTest[index]._size;
//    _stressTest.Delete(index);
//  }
//  for (int i=0; i<countCreat; i++)
//  {
//    // create a few random sized textures
//    int sizeLogX = rand()%9;
//    int sizeLogY = rand()%9;
//    int sizeX = 4<<sizeLogX;
//    int sizeY = 4<<sizeLogY;
//
//    ComRef<IDirect3DTexture9> tex;
//
//    HRESULT err = _engine->GetDirect3DDevice()->CreateTexture(sizeX, sizeY, 1, 0, D3DFMT_DXT5, D3DPOOL_DEFAULT, tex.Init(), NULL);
//    if (SUCCEEDED(err))
//    {
//      _stressTest.Add(StressInfo(tex,sizeX*sizeY));
//      _stressTestTotalSize += sizeX*sizeY;
//    }
//    else
//    {
//      _engine->DDError11("Cannot create stress surface",err);
//    }
//  }
//#endif
}



void TextBankD3D11::ReloadAll()
{
  Log("Reload all textures.");
  // make sure all texture state is tested
}

} //namespace DX11
#endif //!_DISABLE_GUI
#endif // _ENABLE_DX11

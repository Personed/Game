//# pragma optimize( "", off ) 

/*!
\file
Scene implementation
*/
#include "wpch.hpp"
#include "global.hpp"
#include "keyInput.hpp"
#include "dikCodes.h"

#include "scene.hpp"
#include "engine.hpp"
#include "textbank.hpp"
#include "lights.hpp"
#include "landscape.hpp"
#include "txtPreload.hpp"
#include "object.hpp"
#include "camera.hpp"
#include "Shape/poly.hpp"
#include "animation.hpp"
#include "vehicleAI.hpp"
#include "Shape/material.hpp"
#include "shots.hpp"
#include "objectClasses.hpp"
#include "timeManager.hpp"
#include "AI/ai.hpp" // remove dependancy
#include "world.hpp"
#include "Shape/specLods.hpp"
#include "stringtableExt.hpp"
#include <time.h>
#include <stdint.h>
#include "diagModes.hpp"
#include "fileLocator.hpp"
#include "progress.hpp"

#include <El/ParamFile/paramFile.hpp>
#include <El/Common/randomGen.hpp>
#include <El/HiResTime/hiResTime.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <Es/Containers/sortedArray.hpp>
#include <Es/Algorithms/qsort.hpp>
#include <El/Evaluator/express.hpp>

#include "baseWorld/enf_shapes.h"
#include "Common/enf_quicksort.h"
#if !defined(_WIN64)
#include "d3d9/engdd9.hpp"
#else
#include <El/FreeOnDemand/memFreeReq.hpp>
#include "../Enfusion/include/common/enf_pointerhash.h"
#endif

static int CompareAddress(const void *left, const void *right)
{
  const uintptr_t l = reinterpret_cast<uintptr_t>(left);
  const uintptr_t r = reinterpret_cast<uintptr_t>(right);
  return l < r ? -1 : (l > r ? 1 : 0);
}


#ifdef _PROFILE
#pragma optimize("", off)
#endif

DEFINE_FAST_ALLOCATOR(GridItemLightList)

void ParseFlashpointCfg(ParamFile &file);
RString GetConfigSection();

Scene *GScene;

/// fog calculation
/** Cf. FPShaders.hlsl */
static inline float Haze(float dist, float expFogCoef)
{
  // ln(1/512)/maxDist  ... maxDist = 10000
  float haze = exp(dist*expFogCoef);
  return haze;
}

static float FogWithHaze( float dist, float start, float end, float expFogCoef )
{
  float haze = Haze(dist,expFogCoef);
  // shader VFog(): vFog = haze*saturate((end - dist) /(end-start));
  return 1-haze*floatMinMax((end-dist)/(end-start),0,1);
}

enum LODBlendingMode
{
  LODBlendImmediate,
  LODBlendMemory,
  LODBlendPreventFlicker,
  NLODBlend
};

const LODBlendingMode DefaultLODBlendingMode = LODBlendImmediate;

#if _ENABLE_CHEATS
static LODBlendingMode GLODBlendingMode = DefaultLODBlendingMode;

void ChangeLODBlending(int dir)
{
  int newBlend = GLODBlendingMode+dir;
  if (newBlend<0) newBlend = NLODBlend-1;
  if (newBlend>=NLODBlend) newBlend = 0;
  GLODBlendingMode = LODBlendingMode(newBlend);
}

#else
const LODBlendingMode GLODBlendingMode = DefaultLODBlendingMode;
void ChangeLODBlending(int dir){}
#endif



static void AdaptVolumeLight( LODShape *lodShape, bool point )
{
  lodShape->DisableStreaming();
  if (lodShape->NLevels()>0)
  {
    lodShape->OrSpecial(DisableSun|NoShadow|IsAlpha|DstBlendOne|NoZWrite|ClampU|ClampV|IsAlphaFog|IsColored);

    // TODO: check if modification of shape is legal
    ShapeUsed lock = lodShape->Level(0);
    Assert(lock.NotNull());
    Shape *shape0 = lock.InitShape();

    Ref<TexMaterial> material;
    material = new TexMaterial;
    material->SetMainLight(ML_None);
    material->SetFogMode(FM_Alpha);
    material->_ambient = HBlack;
    material->_diffuse = HBlack;
    // point light may need a different intensity than a reflector ones
    float intensity = point ? 1.0f : 0.005f;
    material->_emmisive = Color(intensity,intensity,intensity);
    shape0->SetUniformMaterial(material);
    shape0->VertexDeclarationNeedsUpdate();
    shape0->DisablePushBuffer();
  }
}

#define USE_SHADOWMAP_THREAD 1

#if USE_SHADOWMAP_THREAD
#include <El/Multicore/jobs.hpp>
#endif

#if _VBS3_LAND_SHADOW
struct Rect 
{
  float xs;
  float zs;
  float xe;
  float ze;
};

/// Stores land shadow for one land segment.
///
class LandShadowSegment : public RefCount, public TLinkBidirRef
{
protected:
  GridRectangle _rectSD;
  //GridRectangle _rect;

  /// position in _data where last incremental update ended
  int _xInc, _zInc;

  /// time in ms, when this segment was calculated
  float _age;

  Array2D<float> _data;

public:
  LandShadowSegment(GridRectangle& rectSD) : _rectSD(rectSD) {Invalidate();}

  const GridRectangle& GetRect() const {return _rectSD;}
  //const GridRectangle& GetRect() const {return _rect;}
  size_t GetSize() const {return sizeof(*this) + _data.RawSize();}

  /// Returns cached land shadow info for the segment.
  void GetLandShadow(int lod, VertexCustomInfo* custom);
  float GetLandShadow(int x, int z);
  void GetLandShadowRect(int x, int z, Rect& rect);
  /// Returns cached shadow values for a shape (used for roads)
  void GetShadowValuesShape(const AutoArray<Vector3>& pos, AutoArray<VertexCustomInfo>& custom);

  const float Age() const {return _age;}

  /// Discards cached land shadow values.
  void Invalidate()
  {
    int lssize = GLandscape ? GLandscape->GetLandSegmentSize() : 0;
    _data.Clear();
    _data.Dim(lssize+1, lssize+1);
    _xInc = _zInc = 0;
    _age = 0;
  };

  /// Calculates part of expensive land shadow values.
  void IncrementalUpdate();

  /// Returns true if IncrementalUpdate needs to be called on this segment.
  bool NeedsUpdating() const 
  {
    const int subdiv = TerrainRangeLog - LandRangeLog;
    return _zInc <= (_rectSD.Height()>>subdiv);
  }

private:
  float CheckShadow(int xx, int zz, Vector3Par sunDir, TerrainCache *cache=NULL) const;
};

/// Hashing traits for LandShadowSegment - copied from landscape.cpp
///
template <>
struct MapClassTraits<Ref<LandShadowSegment> > : public DefMapClassTraits<Ref<LandShadowSegment> >
{
  /// key type
  typedef const GridRectangle &KeyType;

  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    return key.xBeg ^ (key.zBeg<<16);
  }

  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2)
  {
    int d = k1.xBeg-k2.xBeg;
    if (d) return d;
    return k1.zBeg-k2.zBeg;
  }

  /// we want this to be very fast, even if memory requirements may be a little bit higher
  static int CoefExpand() {return 4;}

  /// get a key for given a item
  static KeyType GetKey(const Ref<LandShadowSegment> &item) { return item->GetRect(); }
};

class LandscapeShadowMapCache : public MemoryFreeOnDemandHelper
{
  friend class Landscape;

protected:

  /// list of all segments, last in list is LRU
  TListBidirRef<LandShadowSegment> _segments;

  /// hash table of segments
  MapStringToClass<Ref<LandShadowSegment>, RefArray<LandShadowSegment> > _index;

  /// maximum number of stored segments
  int _maxN;

  GridRectangle _bigRect;
  mutable CriticalSection _csShadow;

  /// State of the incremental update mechanism
  enum IUState
  {
    Idle,   ///< inactive (no segments need updating)
    Select, ///< next segment to update needs to be selected
    Working ///< currently working on a segment
  };

  IUState _iuState;
  Ref<LandShadowSegment> _iuSegment; ///< segment currently being incrementally updated
  Ref<LandShadowSegment> _oldestSegment; ///< oldest segment in cache

  ///
  Ref<LandShadowSegment> GetSegment(GridRectangle& rectSD);
  Ref<LandShadowSegment> _emptySegment;

public:

  LandscapeShadowMapCache();
  ~LandscapeShadowMapCache() { Clear(); }

  void Init(float landSegSize);
  void Clear(bool invalidateSegments = false);

  void CalculateBigRect();

  void GetLandShadow(GridRectangle& rectSD, int lod, VertexCustomInfo* custom);
  void GetLandShadowRect(int x, int z, Rect& rect);
  void GetShadowValuesShape(GridRectangle& rectSD, const AutoArray<Vector3>& pos, AutoArray<VertexCustomInfo>& custom);

  /// Discard cached land shadow segment. Rectangle in land grid cells.
  void InvalidateSegment(GridRectangle& rect);

  /// Update land shadow values of one row of one segment.
  void IncrementalUpdate();

  CriticalSection &GetLock() const {return _csShadow;}

  void Delete(LandShadowSegment *seg)
  {
    _segments.Delete(seg);
    _index.Remove(seg->GetRect());
  }

  void Insert(LandShadowSegment *seg)
  {
    _segments.Insert(seg);
    _index.Add(seg);
    _iuState = Select;
  }

  // { memory free on demand implementation
  size_t FreeOneItem();
  float Priority() const;
  size_t MemoryControlled() const;
  RString GetDebugName() const;
  // }
};
#endif

#if !_VBS3_LAND_SHADOW
/// clouds and landscape shadow casting
class LandscapeShadowMap
#if USE_SHADOWMAP_THREAD
  : public Job
#endif
{
  friend class Landscape;
  /// shadow casted by a landscape
  Array2D<float> _landShadow;

  /// rectangle we are currently holding
  GridRectangle _rect;
  
  TerrainCache *_terCache;
  
  /// perform cyclical field-by-field update
  int _updateX;
  int _updateZ;

  /// how many frames to skip between updates  
  /**
  With 1200 m viewdistance and 50 m grid we have 24 lines.
  With framerate 20 fps and UpdateFrames we have one line update in 2 sec,
  resulting in one full update each 48 sec.
  */
  static const int UpdateFrames = 40;
  
  /// no need to update each frame
  int _toNextUpdate;
  
  /// after rectangle change, we need to update a bit faster
  int _fastUpdatesNeeded;
  /// stored parameter for the job
  Vector3 _sunDir;
  /// stored parameter for the job
  Landscape *_landscape;
  
  public:
  ~LandscapeShadowMap();
  void Calculate(Landscape *land, const GridRectangle &bigRect, Scene &scene, bool init);
  void Clear();
  float GetLand(int x, int z) const
  {
    saturate(x,_rect.xBeg,_rect.xEnd);
    saturate(z,_rect.zBeg,_rect.zEnd);
    return _landShadow(x-_rect.xBeg,z-_rect.zBeg);
  }
  bool IsEmpty() const
  {
    return _landShadow.GetXRange()<=0 || _landShadow.GetYRange()<=0;
  }
  
#if USE_SHADOWMAP_THREAD
  /// the main Job procedure
  virtual bool Process();
#endif
  
  private:
  float CheckShadow(Landscape *land, int xx, int zz, Vector3Par sunDir, TerrainCache *cache=NULL) const;
};
#endif

/** implementation of EngineSceneProperties, to avoid .hpp dependencies */
class EngineProperties: public EngineSceneProperties
{
public:
  virtual void GetSeaWaveMatrices(Matrix4 &matrix0, Matrix4 &matrix1, Matrix4 &matrix2, Matrix4 &matrix3, const EngineShapeProperties &prop) const;
  virtual void GetSeaWavePars(float &waveXScale, float &waveZScale, float &waveHeight) const;
  virtual void GetSkyColor(Color &sky, Color &aroundSun) const;
  virtual Vector3Val GetCameraPosition() const;
  virtual Matrix4Val GetCameraProjection() const;
  virtual void GetTerrainLODPars(float pars[4]) const;
  virtual void GetGrassPars(float pars[8]) const;
  virtual float GetSeaLevel() const;
  virtual float GetSatScale() const;
  virtual FogParams ComputeFogParams( float farFog ) const;
};

void EngineProperties::GetSeaWaveMatrices(Matrix4 &matrix0, Matrix4 &matrix1, Matrix4 &matrix2, Matrix4 &matrix3, const EngineShapeProperties &prop) const
{
  GLandscape->GetSeaWaveMatrices(matrix0,matrix1,matrix2,matrix3,prop);
}

void EngineProperties::GetSeaWavePars(float &waveXScale, float &waveZScale, float &waveHeight) const
{
  GLandscape->GetSeaWavePars(waveXScale,waveZScale,waveHeight);
}

void EngineProperties::GetSkyColor(Color &sky, Color &aroundSun) const
{
#if _ARMA3_NEW_LIGHTING_CFG
  if (GLandscape && GLandscape->UseNewLandLighting() && GScene)
  {
    sky = GScene->GetCurrentLighting()._sky;
    aroundSun = GScene->GetCurrentLighting()._skyAroundSun;
  }
  else
#endif
  {
  float cosSun = -GScene->MainLight()->SunDirection().Y();
  GLandscape->GetSkyColors(sky,aroundSun,cosSun);
}
}

Vector3Val EngineProperties::GetCameraPosition() const
{
  return GScene->GetCamera()->Position();
}
Matrix4Val EngineProperties::GetCameraProjection() const
{
  return GScene->GetCamera()->ProjectionNormal();
}

void EngineProperties::GetTerrainLODPars(float pars[4]) const
{
  pars[0] = GLandscape->GetLODBias();
  pars[1] = GLandscape->GetMaxLod(); // NLods-1
  pars[2] = pars[3] = 0;

#if _VBS3_MORETERRAINLODS
  // tweak the values so that we still get the original terrain detail even if we have more than 3 LODs
  float oldMaxLod = floatMin(pars[1], 2);
  pars[0] -= pars[1]; // now the formula in TransformLODEx is "k + logDistance*0.5"
  pars[0] += oldMaxLod; // now the formula is the original "oldMaxLod + k + logDistance*0.5"
#endif
}

static inline void SetRampEndBeg(float &add, float &mul, float start, float end)
{
  add = end;
  mul = 1.0f/(end-start);
  // ramp result is: saturate((end-x)*mul)
  //   end => 0
  //   start => 1
}

void EngineProperties::GetGrassPars(float pars[8]) const
{
  // see also startRoadAlpha in Landscape::DrawGround
  float startGrassAlpha = floatMax(Glob.config.roadsZ*0.2f,400.0f);
  float endGrassAlpha = Glob.config.roadsZ;

  float clutterDist = GLandscape->GetClutterDist();
  static bool noNearAlpha = false; // set true to display if you want the grass layer to be displayed even close to camera
  if (noNearAlpha)
  {
    SetRampEndBeg(pars[0],pars[4],clutterDist*0.02f,clutterDist*0.06f);
  }
  else
  {
    // grass layer fading in close to camera
    SetRampEndBeg(pars[0],pars[4],clutterDist*0.2f,clutterDist*0.6f);
  }
  // grass layer fading out in distance
  SetRampEndBeg(pars[1],pars[5],startGrassAlpha,endGrassAlpha);

  // grass color factor (detail/satellite) - not used any more, because clutter is colored by sat. map
  SetRampEndBeg(pars[2],pars[6],clutterDist*0.3f,clutterDist);
  pars[3] = pars[7] = 0;
}

float EngineProperties::GetSeaLevel() const
{
  return GLandscape->GetSeaLevel();
}

float EngineProperties::GetSatScale() const
{
  int zAlign;
  return GLandscape->GetSatSegSize(zAlign);
}

FogParams EngineProperties::ComputeFogParams( float farFog ) const
{
  // Set the fog parameters
  const float defExpFog = 10000.0f;
  //const float startFactor = InterpolativC(farFog,100,200,0,0.2f);
  const float startFactor = InterpolativC(farFog,2000,defExpFog,0.5f,0.9f);
  //added distance dependenci for haze
  float hazeDefaultDistance = (GWorld && GWorld->GetScene())? GWorld->GetScene()->GetHazeDefaultDistnce() : -1;
  const float distFactor = hazeDefaultDistance > 0 ? (hazeDefaultDistance / floatMax(farFog,hazeDefaultDistance)) : 1;
#if 1 // _VBS3_NPFLT
  float nearFog  = startFactor * farFog;
#else
  float nearFog  = ( startFactor*(farFog-camera->ClipNear()) ) + camera->ClipNear();
#endif
  const float denseFogFar = 300.0f;
  const float denseFogNear = 200.0f;

  float expFar = defExpFog;

  // for thick fog we need much denser base value
  /**
  This is related to the fact thick fog was tuned with SM 2 and SRGB fog blending
  */
  float lnInvFar = -0.8f;
  float lnInvNear = -3.2188758249f;
  float lnInv = lnInvFar;

  // if the fog is very dense, prefer exponential (underwater, real fog)
  // in the middle range prefer linear, not starting very soon (hiding far objects)
  // if the fog is very light, prefer exponential (realistic scattering)
  if (farFog>defExpFog) expFar = farFog;
  else if (farFog<denseFogFar)
  {
    if (farFog>denseFogNear)
    {
      expFar = InterpolativC(farFog,denseFogNear,denseFogFar,denseFogNear,defExpFog);
      lnInv = InterpolativC(farFog,denseFogNear,denseFogFar,lnInvNear,lnInvFar);
    }
    else
    {
      expFar = farFog;
      lnInv = lnInvNear;
    }
  }
  FogParams params;
  params.expCoef = distFactor * (lnInv/expFar);
  params.start = nearFog;
  params.end = farFog;

#if _ARMA3_HAZE_COMPUTATION
  if (GLandscape)
  {
    float hazeCoef = GLandscape->GetHazeFogCoef();
    float hazeDistCoef = GLandscape->GetHazeDistCoef();
    if (hazeCoef > 0 && hazeDistCoef > 0)
    {
      float lnHazeCoef = log(hazeCoef);
      float dist = farFog * hazeDistCoef;
      if (dist > 0)
        params.expCoef = lnHazeCoef / (farFog * hazeDistCoef);
    }
  }
#endif

  return params;
}

EngineSceneProperties *Scene::GetEngineProperties() const
{
  return _engineProps;
}

#if !_VBS3_LAND_SHADOW
LandscapeShadowMap::~LandscapeShadowMap()
{
#if USE_SHADOWMAP_THREAD
  // stop the job
  GJobManager.CancelJob(this);
#endif
}

void LandscapeShadowMap::Clear()
{
#if USE_SHADOWMAP_THREAD
  // stop the job
  GJobManager.CancelJob(this);
#endif

  _rect = GridRectangle(0,0,-1,-1);
  _landShadow.Clear();
  _updateZ = -1;
  _updateX = -1;
  _toNextUpdate = UpdateFrames;
  _fastUpdatesNeeded = 0;
}

float LandscapeShadowMap::CheckShadow(Landscape *land, int xx, int zz, Vector3Par sunDir, TerrainCache *cache) const
{
  const int terrainLog = land->GetSubdivLog();
  float xPos = xx*land->GetLandGrid();
  float zPos = zz*land->GetLandGrid();
  float yPos = land->ClippedDataXZ(xx<<terrainLog,zz<<terrainLog,cache);
  // be sure to check a little bit above the ground to avoid numeric precision errors
  const float above = 0.5f;
  // precision higher than terrain grid has no sense
  const float towardsSun = land->GetTerrainGrid()*1.5f;
  Vector3 cPos = Vector3(xPos,yPos+above,zPos)-sunDir*towardsSun;
  // use ray bending up to make sure shadows are never too long
  // this also kind of "simulates" light scattering
  
  //  quadratic attenuation: make sure shadow of 0.5*NOM m hill is never longer than DENOM m (with horizontal light)
  static float bend = 100.0f/Square(1400.0f);
  
  // for TerSynth terrain make bending even stronger. Tests there are a lot slower while results are not that interesting.
  static float bendOut = 100.0f/Square(400.0f);
  
  float under = land->IntersectUnder(cPos,-sunDir,Glob.config.horizontZ,bend,bendOut,cache);
  return under;
}

#if USE_SHADOWMAP_THREAD
bool LandscapeShadowMap::Process()
{
  // process piece of work
  PROFILE_SCOPE_EX(shdCU, *);

  int cellsToDo = _landShadow.GetXRange();
  if (_fastUpdatesNeeded > 0) cellsToDo = intMin(_fastUpdatesNeeded, 5)*_landShadow.GetXRange(); // faster update needed for a while
  // with large visibility prevent computing too many cells - that would exhaust CPU
  if (cellsToDo>200) cellsToDo = 200;
  // TODO: also break if we detect the computation takes too long
  for (int i=0; i<cellsToDo; i++)
  {
    if (_updateX>_rect.xEnd || _updateX<_rect.xBeg)
    {
      _updateX = _rect.xBeg;
      _updateZ++;
      _fastUpdatesNeeded--;
    }
    if (_updateZ>_rect.zEnd || _updateZ<_rect.zBeg)
  {
      _updateZ = _rect.zBeg;
    }

    // update one line
    int z = _updateZ-_rect.zBeg;
    int x = _updateX-_rect.xBeg;
    
    _landShadow(x, z) = CheckShadow(_landscape, _updateX, _updateZ, _sunDir, _terCache);
    if (WantToBeCanceled()) return false;

    _updateX++;
  }
  return !WantToBeCanceled(); // done or canceled (this job is never suspended)
}
#endif
#endif

//! bilinear interpolation, yXZ, xf and zf = <0,1>

static __forceinline float Bilint
(
  float y00, float y01, float y10, float y11,
  float xf, float zf
)
{
  return y00*(1-zf)*(1-xf) + (y01*zf)*(1-xf) + y10*(1-zf)*xf + y11*zf*xf;
}

/// compute modulo as defined in algebra (always positive, division rounding down)
static inline int Mod(int a, int b)
{
  int mod = a%b;
  if (mod<0) return mod+b;
  return mod;
}

#if _VBS3_LAND_SHADOW
#define LOG_SHADOW_CACHE 0 // debug logs related to LandShadowMapCache

void LandShadowSegment::GetLandShadow(int lod, VertexCustomInfo* custom)
{
  const int lodStep = 1<<lod;
  const int subdivisionLevel = TerrainRangeLog - LandRangeLog;
  const int subdivisionCount = 1<<subdivisionLevel;
  const float subdivisionCountInv = 1.0f/subdivisionCount;
  const int xCountVert=((_rectSD.xEnd-_rectSD.xBeg)>>lod)+1;

  if (_zInc<=_rectSD.Height()>>subdivisionLevel)
  {
    for (int z = _rectSD.zBeg; z <= _rectSD.zEnd; z+=lodStep)
      for (int x = _rectSD.xBeg; x <= _rectSD.xEnd; x+=lodStep)
      {
        const int xz=((z-_rectSD.zBeg)>>lod)*xCountVert+((x-_rectSD.xBeg)>>lod);
        custom[xz]._landShadow = -10.0f;
      }
  }
  else
  {
    for (int z = _rectSD.zBeg; z <= _rectSD.zEnd; z+=lodStep)
    {
      int zl = z>>subdivisionLevel;
      int zz = zl - (_rectSD.zBeg>>subdivisionLevel);

      for (int x = _rectSD.xBeg; x <= _rectSD.xEnd; x+=lodStep)
      {
        const int xz=((z-_rectSD.zBeg)>>lod)*xCountVert+((x-_rectSD.xBeg)>>lod);

        int xl = x>>subdivisionLevel;      
        int xx = xl - (_rectSD.xBeg>>subdivisionLevel);
        
        int xx1 = xx+1 > GLandscape->GetLandSegmentSize() ? xx : xx+1;
        int zz1 = zz+1 > GLandscape->GetLandSegmentSize() ? zz : zz+1;

        float xz00 = _data(xx,zz);
        float xz10 = _data(xx1,zz);
        float xz01 = _data(xx,zz1);
        float xz11 = _data(xx1,zz1);

        float xf = (x-(xl<<subdivisionLevel))* subdivisionCountInv;
        float zf = (z-(zl<<subdivisionLevel))* subdivisionCountInv;

        custom[xz]._landShadow = Bilint(xz00,xz01,xz10,xz11,xf,zf);
      }
    }
  }
}

void LandShadowSegment::GetLandShadowRect(int x, int z, Rect& rect)
{
  if (NeedsUpdating())
  {
    rect.xs = -10.0f;
    rect.xe = -10.0f;
    rect.zs = -10.0f;
    rect.ze = -10.0f;
  }
  else
  {
    const int lssize = GLandscape->GetLandSegmentSize();
    const int subdiv = TerrainRangeLog - LandRangeLog;

    int xBeg = _rectSD.xBeg>>subdiv;
    int zBeg = _rectSD.zBeg>>subdiv;

    int xxBeg1 = x - xBeg + 1;
    int zzBeg1 = z - zBeg + 1;
    saturate(xxBeg1, 0, lssize);
    saturate(zzBeg1, 0, lssize);

    rect.xs = _data(x-xBeg, z-zBeg);
    rect.xe = _data(xxBeg1, z-zBeg);
    rect.zs = _data(x-xBeg, zzBeg1);
    rect.ze = _data(xxBeg1, zzBeg1);
  }
}

void LandShadowSegment::GetShadowValuesShape(const AutoArray<Vector3>& pos, AutoArray<VertexCustomInfo>& custom)
{
  if (NeedsUpdating())
  {
    for (int i = 0; i < pos.Size(); i++)
      custom[i]._landShadow = -10.0f;
  }
  else
  {
    const int subdiv = TerrainRangeLog - LandRangeLog;

    int xSize = _rectSD.Width() >> subdiv;
    int zSize = _rectSD.Height() >> subdiv;
    int xHalf = xSize / 2, zHalf = zSize / 2;

    DoAssert(pos.Size() == custom.Size());
    for (int i = 0; i < pos.Size(); i++)
    {
      float xl = pos[i].X() * InvLandGrid,
        zl = pos[i].Z() * InvLandGrid;

      int xx = toIntFloor(xl), 
        zz = toIntFloor(zl);

      float xf = xl - xx,
        zf = zl - zz;

      xx += xHalf; 
      zz += zHalf; 

      if (xx < 0) { xx = 0; xf = 0; }
      else if (xx >= xSize) { xx = xSize-1; xf = 1.0; }

      if (zz < 0) { zz = 0; zf = 0; }
      else if (zz >= zSize) { zz = zSize-1; zf = 1.0; }

      custom[i]._landShadow = Bilint(_data(xx, zz), _data(xx, zz+1), _data(xx+1, zz), _data(xx+1, zz+1), xf, zf);
    }
  }
}

float LandShadowSegment::GetLandShadow(int x, int z)
{
  const int subdiv = TerrainRangeLog - LandRangeLog;

  if (NeedsUpdating())
    return -10.0f;
  else
    return _data(x-(_rectSD.xBeg>>subdiv), z-(_rectSD.zBeg>>subdiv));
}

void LandShadowSegment::IncrementalUpdate()
{
  PROFILE_SCOPE_EX(shdCl,*);
  const int MaxRays = 25; // number of rays to cast in one incremental update
  const int subdiv = TerrainRangeLog - LandRangeLog;

  const int width = (_rectSD.Width()>>subdiv);
  const int height = (_rectSD.Height()>>subdiv);

  Vector3 sunDir = GScene->MainLight()->ShadowDirection();

  // update several land grids, but don't exceed MaxRays (too much)
  int rays = 0;
  do
  {
    _data(_xInc, _zInc) = CheckShadow((_rectSD.xBeg>>subdiv) + _xInc, (_rectSD.zBeg>>subdiv) + _zInc, sunDir);

    ++rays;

    // move to next land grid
    _xInc++;
    if (_xInc > width)
    {
      _xInc = 0;
      _zInc++;
      if (_zInc > height)
      {
        _age = Glob.clockWeather.GetTimeOfDay();
        break;
      }
    }
  }
  while (rays < MaxRays);
}

float LandShadowSegment::CheckShadow(int xx, int zz, Vector3Par sunDir, TerrainCache *cache) const
{
  const int terrainLog = GLandscape->GetSubdivLog();
  float xPos = xx*GLandscape->GetLandGrid();
  float zPos = zz*GLandscape->GetLandGrid();
  float yPos = GLandscape->ClippedDataXZ(xx<<terrainLog,zz<<terrainLog);
  // be sure to check a little bit above the ground to avoid numeric precision errors
  const float above = 0.5f;
  // precision higher than terrain grid has no sense
  const float towardsSun = GLandscape->GetTerrainGrid()*1.5f;
  Vector3 cPos = Vector3(xPos,yPos+above,zPos)-sunDir*towardsSun;
  Vector3 underPos;
//  float under = GLandscape->IntersectUnder(cPos,-sunDir,Glob.config.horizontZ,underPos);

  //  quadratic attenuation: make sure shadow of 0.5*NOM m hill is never longer than DENOM m (with horizontal light)
  static float bend = 100.0f/Square(1400.0f);

  // for TerSynth terrain make bending even stronger. Tests there are a lot slower while results are not that interesting.
  static float bendOut = 100.0f/Square(400.0f);
  float under = GLandscape->IntersectUnder(cPos,-sunDir,Glob.config.horizontZ,bend,bendOut,cache);
  /*
  // we want sea bed (terrain under the sea) to be darker
  // would could influence the depth map here, but we prefer to do it in the vertex shader
  // because of better precision and increased flexibility
  if (yPos<0)
  {
    // simulate light is reduced under water
    saturateMax(under,-yPos*(1.0f/20));
  }
  */
  return under;
}

LandscapeShadowMapCache::LandscapeShadowMapCache()
{
  RegisterFreeOnDemandMemory(this);
  _maxN = 500; // just initialize to something, actual value is computed in Init()
  _iuState = Idle;
  _iuSegment = NULL;
  _bigRect.xBeg = 0; _bigRect.zBeg = 0;
  _bigRect.xEnd = -1; _bigRect.zEnd = -1;
}

void LandscapeShadowMapCache::Init(float landSegSize)
{
  // calculate number of segments in visible area
  float range = GScene ? GScene->GetFogMaxRange() : Glob.config.GetShadowZ();
  int sideSegments = toIntCeil(range / landSegSize)*2+1;
  const int minReasonable = 200;
  int needSegments = intMax(sideSegments*sideSegments, minReasonable);

  _maxN = needSegments*4;
  
  CalculateBigRect();
  
  const int subdivisionLevel = TerrainRangeLog - LandRangeLog;
  const int subdivisionCount = 1<<subdivisionLevel;

  // position of empty segment is arbitrary
  GridRectangle rectSD(-100,-100,-100+GLandscape->GetLandSegmentSize()*subdivisionCount,-100+GLandscape->GetLandSegmentSize()*subdivisionCount);
  _emptySegment = new LandShadowSegment(rectSD);

  _oldestSegment = NULL;

 #if LOG_SHADOW_CACHW
  LogF("LandscapeShadowMapCache: _maxN set to %d", _maxN);
 #endif
}

void LandscapeShadowMapCache::Clear(bool invalidateSegments)
{
  if (GLandscape) Init(GLandscape->GetLandSegmentSize()*GLandscape->GetLandGrid());
  if (invalidateSegments)
  {
    // invalidate land cache
  //  if (GLandscape) GLandscape->InvalidateAllSegments();

    _segments.Clear();
    _index.Clear();
  }
  else
  {
    Ref<LandShadowSegment> seg = _segments.First();
    while (seg)
    {
      seg->Invalidate();
      seg = _segments.Next(seg);
    }
  } 
}

void LandscapeShadowMapCache::CalculateBigRect()
{
  if (!GScene->GetCamera())
  {
    _bigRect.xBeg = 0;
    _bigRect.xEnd = -1;
    _bigRect.zBeg = 0;
    _bigRect.zEnd = -1;
  }
  else
  {
    Vector3Val pos = GScene->GetCamera()->Position();

    float radius = GScene->GetFogMaxRange();
    float xMinF = pos.X()-radius;
    float xMaxF = pos.X()+radius;
    float zMinF = pos.Z()-radius;
    float zMaxF = pos.Z()+radius;
    _bigRect.xBeg = toIntFloor(xMinF*InvObjGrid);
    _bigRect.xEnd = toIntCeil(xMaxF*InvObjGrid);
    _bigRect.zBeg = toIntFloor(zMinF*InvObjGrid);
    _bigRect.zEnd = toIntCeil(zMaxF*InvObjGrid);
  }
}

void LandscapeShadowMapCache::GetLandShadow(GridRectangle& rectSD, int lod, VertexCustomInfo* custom)
{
  GetSegment(rectSD)->GetLandShadow(lod, custom);
}

void LandscapeShadowMapCache::GetShadowValuesShape(GridRectangle& rectSD, const AutoArray<Vector3>& pos, AutoArray<VertexCustomInfo>& custom)
{
  GetSegment(rectSD)->GetShadowValuesShape(pos, custom);
}


extern int RoundToSegment(int x, int landSegmentSize, int landSegmentAlign);

void LandscapeShadowMapCache::GetLandShadowRect(int x, int z, Rect& rect)
{
  const int subdivisionLevel = TerrainRangeLog - LandRangeLog;
  const int subdivisionCount = 1<<subdivisionLevel;

  int zAlign;
  GLandscape->GetSatSegSize(zAlign);

  int xs = RoundToSegment(x, GLandscape->GetLandSegmentSize(), 0);
  int zs = RoundToSegment(z, GLandscape->GetLandSegmentSize(), zAlign);

  GridRectangle rectSD;
  rectSD.xBeg = xs*subdivisionCount;
  rectSD.zBeg = zs*subdivisionCount;
  rectSD.xEnd = rectSD.xBeg + GLandscape->GetLandSegmentSize()*subdivisionCount;
  rectSD.zEnd = rectSD.zBeg + GLandscape->GetLandSegmentSize()*subdivisionCount;

  GetSegment(rectSD)->GetLandShadowRect(x,z,rect);
}

void LandscapeShadowMapCache::InvalidateSegment(GridRectangle& rectSD)
{
  const Ref<LandShadowSegment> &seg = _index.Get(rectSD);
  if (_index.NotNull(seg))
  {
    Delete(seg);
  }

  // start incremental update
  _iuState = Select;
}

static int CmpShadowSegDistToCam(const Ref<LandShadowSegment> *seg1, const Ref<LandShadowSegment> *seg2)
{
  int subdiv = 1 << (TerrainRangeLog - LandRangeLog);
  GridRectangle rect1 = seg1->GetRef()->GetRect();
  GridRectangle rect2 = seg2->GetRef()->GetRect();
  float segSizeHalf1 = rect1.Width()/2;
  float segSizeHalf2 = rect2.Width()/2;
  Vector3 seg1Pos(rect1.xBeg + segSizeHalf1, 0.0f, rect1.zBeg + segSizeHalf1);
  Vector3 seg2Pos(rect2.xBeg + segSizeHalf2, 0.0f, rect2.zBeg + segSizeHalf2);
  Vector3 camPos = GScene->GetCamera()->Position() * GLandscape->GetInvLandGrid() * subdiv;

  float dist1 = camPos.Distance2(seg1Pos);
  float dist2 = camPos.Distance2(seg2Pos);

  float diff = dist1 - dist2;
  if (diff > 0.0f) return 1;
  else if (diff < 0.0f) return -1;
  else return 0;
}

void LandscapeShadowMapCache::IncrementalUpdate()
{
  CalculateBigRect();
//  const int subdiv = TerrainRangeLog - LandRangeLog;
  if (_iuState == Idle)
  {
    // there are no segments that need updating
    // recalculate oldest segment
    const float timeInYear32OneDay = 24*60;
    const float TTL = 5.0f/timeInYear32OneDay; // time to live in minutes
    const float currTime = Glob.clockWeather.GetTimeOfDay();
    if (_oldestSegment && abs(currTime - _oldestSegment->Age()) > TTL)
    {
     #if LOG_SHADOW_CACHE
      GridRectangle rect = _oldestSegment->GetRect();
      LogF("SC: IncrementalUpdate: segment (%d,%d) is too old (age: %f)!", rect.xBeg>>subdiv, rect.zBeg>>subdiv, currTime - _oldestSegment->Age());
     #endif
      _oldestSegment->Invalidate();
      _iuState = Select;
      _oldestSegment = NULL;
    }
    return;
  }
  else if (_iuState == Working)
  {
    // update one row in the current segment
    LandShadowSegment* seg = _iuSegment;
    seg->IncrementalUpdate();

    // if finished, show changes and select next segment
    if (!seg->NeedsUpdating())
    {
      //int subdiv = TerrainRangeLog - LandRangeLog;
#if _VBS3_CPU_LAND_LIGHT
      const GridRectangle& rect = seg->GetRect();
      GLandscape->InvalidateSegment(rect.xBeg>>subdiv, rect.zBeg>>subdiv, false);
#endif

     #if LOG_SHADOW_CACHE
      LogF("SC: IncrementalUpdate: segment (%d,%d) finished", rect.xBeg>>subdiv, rect.zBeg>>subdiv);
     #endif
      _iuState = Select;
      _iuSegment = NULL;
    }
  }
  else // (_iuState == Select)
  {
    // select a segment that needs racalculation of land shadow values
    // segments closer to the camera are selected first

    LandShadowSegment* seg = _segments.First();
    RefArray<LandShadowSegment> segments;
    while (seg)
    {
      if (seg->NeedsUpdating()) 
      {
        segments.Add(seg);
      }
      seg = _segments.Next(seg);
    }

    if (segments.Size() > 0)
    {
      QSort(segments, CmpShadowSegDistToCam);
      _iuSegment = segments.First();
      _iuState = Working;
      return;
    }
    else
    {
      LandShadowSegment* seg = _segments.First();
      LandShadowSegment* segn = NULL;
      LandShadowSegment* oldestSeg = _oldestSegment = seg;

      const float currTime = Glob.clockWeather.GetTimeOfDay();

      while (seg)
      {
        segn = _segments.Next(seg);
        if (segn && abs(currTime - oldestSeg->Age()) < abs(currTime - segn->Age()))
        {
          oldestSeg = segn;
        }
        seg = segn;
      }
      _oldestSegment = oldestSeg;
    }

    // no such segment found -> Idle
   #if LOG_SHADOW_CACHE
    LogF("SC: IncrementalUpdate(): Idle");
   #endif
    _iuState = Idle;
    _iuSegment = NULL;
  }
}

Ref<LandShadowSegment> LandscapeShadowMapCache::GetSegment(GridRectangle& rectSD)
{
  ScopeLockSection lock(_csShadow);
  // look up segment by rectSD
  Ref<LandShadowSegment> seg;
  const Ref<LandShadowSegment> &segi = _index.Get(rectSD);
  if (_index.NotNull(segi))
  {
    // segment exists: move it to the beginning of the list    
    _segments.Delete(segi); // (no need to keep the ref here, we know another reference is kept in _index)
    _segments.Insert(segi);

    seg = segi;
  }
  else // segment doesn't exist, we need to create it
  {
    // if segment is too far, do not create it
    const int subdivisionLevel = TerrainRangeLog - LandRangeLog;
    GridRectangle rect;
    rect.xBeg = rectSD.xBeg >> subdivisionLevel;
    rect.zBeg = rectSD.zBeg >> subdivisionLevel;
    rect.xEnd = rectSD.xEnd >> subdivisionLevel;
    rect.zEnd = rectSD.zEnd >> subdivisionLevel;
    rect = _bigRect&rect;
    if (rect.IsEmpty())
    {
     #if LOG_SHADOW_CACHE
      LogF("SC: segment (%d,%d) is too far!", rectSD.xBeg >> subdivisionLevel, rectSD.zBeg >> subdivisionLevel);
     #endif
      return _emptySegment;
    }

    // drop LRU segment if we are already storing the maximum number
    int count = _index.NItems();
    if (segi.IsNull() && count >= _maxN)
    {
#if LOG_SHADOW_CACHE
      LandShadowSegment *seg = _segments.Last();
      LogF("SC: dropped segment (count>%d) %d,%d", _maxN, seg->GetRect().xBeg>>subdivisionLevel, seg->GetRect().zBeg>>subdivisionLevel);
#endif
      // to avoid fragmentation we first delete and then generate
      Delete(_segments.Last());
    }

    // create new light segment
    seg = new LandShadowSegment(rectSD);
    //Assert(rectSD.xBeg%48==0);
    Insert(seg);
  }

  return seg;
}

size_t LandscapeShadowMapCache::FreeOneItem()
{
  // take the LRU
  LandShadowSegment* seg = _segments.Last();
  if (!seg) return 0;

  // free it 
  size_t size = seg->GetSize();
  Delete(seg);
  return size;
}

float LandscapeShadowMapCache::Priority() const
{
  return 0.3f;
}

size_t LandscapeShadowMapCache::MemoryControlled() const
{
  // calculate how much memory is controlled by this container
  size_t mem = 0;
  for (LandShadowSegment* seg = _segments.First();  seg;  seg = _segments.Next(seg))
    mem += seg->GetSize();
  return mem;
}

RString LandscapeShadowMapCache::GetDebugName() const
{
  return RString("LandShadowMapCache");
}
#endif

#if !_VBS3_LAND_SHADOW
void LandscapeShadowMap::Calculate(Landscape *land, const GridRectangle &bigRect, Scene &scene, bool init)
{
  // calculate projected cloud shadows for whole area which may be visible on screen
  PROFILE_SCOPE_EX(shdCl,*);
  
  if (_rect.xBeg>_rect.xEnd && _rect.zBeg>_rect.zEnd)
  {
    // detect the first time Calculate is called after Clear
    init = true;
  }
  --_toNextUpdate;

#if USE_SHADOWMAP_THREAD
  // when job is still running, try in the next frame again
  if (GetJobState() == Job::JSRunning ||  GetJobState() == Job::JSWaiting) return;
#endif

#if USE_SHADOWMAP_THREAD
  // check if multicore can be used
  bool runAsJob = GJobManager.IsUsable();
#else
  bool runAsJob = false;
#endif

  // avoid too long shadows by using ShadowDirection instead of LightDirection
  #if 0
  Vector3 sunDir = scene.MainLight()->ShadowDirection();
  #else
  // we want longer shadows than ShadowDirection would allow us
  Vector3 sunDir = scene.MainLight()->LightDirection();
  static float maxShadowDer = -0.1f;
  if (sunDir[1] > maxShadowDer)
  {
    sunDir[1] = maxShadowDer;
    sunDir.Normalize();
  }
  #endif
  
  if (bigRect.IsInside(_rect))
  {
#if USE_SHADOWMAP_THREAD
    if (runAsJob)
    {
      if (!IsEmpty())
      {
        _sunDir = sunDir;
        _landscape = land;
        
        // at this point we can be sure nobody is accessing the cache - job is not running
        // should do nothing - we are not changing _rect here
        _terCache = land->UpdateTerrainCache(_rect);
  
        GJobManager.CreateJob(this, 10000);
      }
    }
    else
#endif
  {
    // we have valid information - nothing to calculate
    // we perform a cyclical update to make sure information is more or less up to date
    if (_toNextUpdate>0) return;
    
    _toNextUpdate = UpdateFrames;
    
    if (IsEmpty()) return;
    
    if (_updateZ>_rect.zEnd || _updateZ<_rect.zBeg)
    {
      _updateZ = _rect.zBeg;
    }
    // update one line

    int z = _updateZ-_rect.zBeg;
    for (int xx=_rect.xBeg; xx<=_rect.xEnd; xx++)
    {
        _landShadow(xx-_rect.xBeg,z) = CheckShadow(land,xx,_updateZ,sunDir,_terCache);
    }
    
    _updateZ++;
    }
    return;
  }


  // if we are calculating something, we will calculate it so that it covers rotation
  // increase bigRect to contain more than needed
  
  Vector3Val pos = scene.GetCamera()->Position();
  
  GridRectangle rotRect;

  // do not use ObjRadiusRectangle, we handle out of range coordinates well here
  float radius = scene.GetFogMaxRange();
  float xMinF = pos.X()-radius;
  float xMaxF = pos.X()+radius;
  float zMinF = pos.Z()-radius;
  float zMaxF = pos.Z()+radius;
  rotRect.xBeg=toIntFloor(xMinF*InvObjGrid);
  rotRect.xEnd=toIntCeil(xMaxF*InvObjGrid);
  rotRect.zBeg=toIntFloor(zMinF*InvObjGrid);
  rotRect.zEnd=toIntCeil(zMaxF*InvObjGrid);

  int landSegmentSize = land->GetLandSegmentSize();
  // use only rough rectangles - no generalization
  rotRect.xBeg &= ~(landSegmentSize-1);
  rotRect.zBeg &= ~(landSegmentSize-1);
  rotRect.xEnd = (rotRect.xEnd+(landSegmentSize-1))&~(landSegmentSize-1);
  rotRect.zEnd = (rotRect.zEnd+(landSegmentSize-1))&~(landSegmentSize-1);
  
  
  //DIAG_MESSAGE(100,"Gen land shadow");
  // we could calculate only what is in bigRect and not in _rect  
  
  // first of all reuse what can be reused
  Array2D<float> oldShadow;
  oldShadow.Move(_landShadow);

  GridRectangle oldRect = _rect;
  // make sure we contain all needed here
  _rect = bigRect + rotRect;
  
  // at this point we can be sure nobody is accessing the cache - job is not running
  _terCache = land->UpdateTerrainCache(_rect);
        
  int xMin = _rect.xBeg;
  int xMax = _rect.xEnd;
  int zMin = _rect.zBeg;
  int zMax = _rect.zEnd;
  _landShadow.Dim(xMax-xMin+1, zMax-zMin+1);
  
  // oldRect and _rect is end inclusive, we need end exclusive representation
  GridRectangle oldRectExcl(oldRect.xBeg,oldRect.zBeg,oldRect.xEnd+1,oldRect.zEnd+1);
  GridRectangle rectExcl(_rect.xBeg,_rect.zBeg,_rect.xEnd+1,_rect.zEnd+1);
  
  // how much items needs to be computed
  GridRectangle intersect = rectExcl&oldRectExcl;
  // count of samples which will be reused
  int intersectSize = intersect.IsEmpty() ? 0 : intersect.Area();
  // count of samples which need to be calculated
  int recalcSize = rectExcl.Area()-intersectSize;
  
  // avoid too many recalculations - 1 mil. samples takes around 7 seconds on P4 3.6 GHz
  // note: if we are using TerSynth, ray casting can be quite slow
  
  // we want. max. 100000 samples (700 ms)

  // if update is processed on the other core, it will be much faster - we can create only a rough estimation
  const int maxSamples = runAsJob && !init ? 200 : 100000;
  
  float ratio = float(recalcSize)/maxSamples;
  float stepRatio = sqrt(ratio);
  int step = toIntCeil(stepRatio);
  
  // to limit number of samples we limit CheckShadow calculations
  // and use bilinear interpolation for the rest
  
  // 1st step - make sure basic grid has data ready
  
  int recalcCount = 0;
  {
  for (int z=zMin; z<=zMax; z++) for (int x=xMin; x<=xMax; x++)
  {
    // if possible, reuse old data
    // TODO: use Rectangle ForEachInComplement
    if (Position(x,z).IsInside(oldRectExcl))
    {
      _landShadow(x-xMin,z-zMin) = oldShadow(x-oldRect.xBeg,z-oldRect.zBeg);
    }
    else
    {
      if (step<=1 || z%step==0 && x%step==0)
      {
          _landShadow(x-xMin,z-zMin) = CheckShadow(land,x,z,sunDir,_terCache);
        recalcCount++;
      }
#if _DEBUG
      else
      {
        _landShadow(x-xMin,z-zMin) = -1;
      }
#endif
      }
    }
  }

  if (step>1)
  {
    // 2nd step - interpolate the rest, replicate the borders where interpolation is not available
    // caution - any value can be negative - modulo of negative value is ill-defined in C++
    int clampXMin = xMin+step-Mod(xMin,step);
    int clampZMin = zMin+step-Mod(zMin,step);
    int clampXMax = xMax-Mod(xMax,step);
    int clampZMax = zMax-Mod(zMax,step);
    float invStep = 1.0f/step;
    for (int z=zMin; z<=zMax; z++) for (int x=xMin; x<=xMax; x++)
    {
      if (Position(x,z).IsInside(oldRectExcl)) continue;
      int xx = Mod(x,step);
      int zz = Mod(z,step);
      if (xx==0 && zz==0) continue;
      // calculate coordinates to data which are ready
      int xBase = x-xx;
      int zBase = z-zz;
      // sample nearest available, clamp coordinates
      int x0 = xBase, x1 = xBase+step;
      int z0 = zBase, z1 = zBase+step;
      saturate(x0,clampXMin,clampXMax);
      saturate(x1,clampXMin,clampXMax);
      saturate(z0,clampZMin,clampZMax);
      saturate(z1,clampZMin,clampZMax);
      float y00 = _landShadow(x0-xMin,z0-zMin);
      float y10 = _landShadow(x1-xMin,z0-zMin);
      float y01 = _landShadow(x0-xMin,z1-zMin);
      float y11 = _landShadow(x1-xMin,z1-zMin);
      //Assert(y00>=0);
      //Assert(y01>=0);
      //Assert(y10>=0);
      //Assert(y11>=0);
      float xf = xx*invStep;
      float zf = zz*invStep;
      
      float y = Bilint(y00,y01,y10,y11,xf,zf);
      _landShadow(x-xMin,z-zMin) = y;
    }
  }
  
#if USE_SHADOWMAP_THREAD
  if (runAsJob && !IsEmpty())
  {
    _fastUpdatesNeeded = _landShadow.GetYRange();
    _sunDir = sunDir;
    _landscape = land;
    GJobManager.CreateJob(this, 10000);
  }
#endif
  
  #if _ENABLE_PERFLOG
    static int maxScopeTime = 500;
    int scopeTime = PROFILE_SCOPE_NAME(shdCl).TimeSpentNorm();
    if (scopeTime>maxScopeTime || scopeTime>10000)
    {
      // we detected interesting (slow) scope - report it
      LogF("Slow shdCl - %.f ms", 0.01f * scopeTime);
      LogF("- recalc %d, calculated %d, step %d",recalcSize,recalcCount,step);
    }
  #endif
}
#endif

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
void SaveUserParams(ParamFile &cfg);

Scene::Scene()
:_skyColor(HWhite),
_mainLight(NULL),
#if _VBS3_LODTRANSITIONCONTROL
_lodTransitionSpeed(0.125f),
#endif
_camera(new Camera),
_smoothLensSunTime(DAYZ_LENSFLARES_TIMEOUT),
_smoothLensFlaresTime(DAYZ_LENSFLARES_TIMEOUT),
_tacticalVisibility(TACTICAL_VISIBILITY),
_rainRange(900),
_constantColor(HWhite),
_drawDensity(0.1),
_preferredTerrainGrid(12.5),
_defaultTerrainGrid(12.5),
_defaultViewDistance(900),
_preferredViewDistance(900),
#if _HELISIM_VIEW_DIST
_defaultObjectViewDistance(600),
_preferredObjectViewDistance(600),
#endif
#if _HELISIM_AMBIENT_TRAFFIC
_defaultTrafficViewDistance(600),
_preferredTrafficViewDistance(600),
#endif
#if _HELISIM_RENDER_TO_TEXTURE
_drawObjectsId(0),
#endif
_enfShapeCollection(NULL),
_engineWidth(GEngine->Width()),
_engineHeight(GEngine->Height()),
_engineArea(GEngine->Width()*GEngine->Height()),
_minPixelSizeMinDist(1),_minPixelSizeMidDist(2),_minPixelSizeMaxDist(4),_complexityTarget(300000),
_prepareId(0), _hazeDefaultDistance(-1.0f)
#if _HELISIM_SHADOW_ZDISTANCE
, _shadowZDistance(Glob.config.DefaultShadowZDistance)
#endif
{
#if _VBS3_LAND_SHADOW
  _shadowMapCache = new LandscapeShadowMapCache;
#else
  _shadowMap = new LandscapeShadowMap;
#endif

  _engineProps = new EngineProperties;
  
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  ParseUserParams(cfg, &globals);
  ParamFile fcfg;
  ParseFlashpointCfg(fcfg);
  LoadConfig(cfg,fcfg);

	if (!_enfShapeCollection)
	{
		_enfShapeCollection = new enf::ShapeCollection;
		_enfShapeCollection->SetCurrent();
	}

  #if _PIII
    Assert( (reinterpret_cast<uintptr_t>(this)&0xf)==0 );
  #endif
  ResetFog();
}

void Scene::ResetFog()
{
  // display fog
  // set back clipping to fog range
  Glob.config.horizontZ=floatMin(_rainRange+20,Glob.config.tacticalZ);

  // avoid fog starting too far
  float maxRange=(Glob.config.horizontZ-20);
  
  FogParams fog = _engineProps->ComputeFogParams(maxRange);
  
  _fogMinRange=fog.start;
  _fogMaxRange=fog.end;
  _fog.Set(fog.start,fog.end,fog.expCoef,FogWithHaze);
  _tacticalFog.Set(fog.start,fog.end,fog.expCoef,FogWithHaze);
}

void Scene::SetTacticalVisibility( float tv, float rainRange )
{
  _tacticalVisibility=tv;
  if( fabs(rainRange-_rainRange)<3 ) return;
  _rainRange=rainRange;
  //LogF("Tac range %.3f",rainRange);
  GWorld->AdjustDerivedVisibility();
}

static void MakeStain(Ref<LODShape> &lodShape)
{
  lodShape->OrSpecial(ClampU|ClampV|NoZWrite|IsAlpha|IsAlphaFog|OnSurface|NoShadow|IsColored);
  lodShape->SetCanBeOccluded(false);
  for (int level = 0; level < lodShape->NLevels(); level++)
  {
    // TODO: check if modification of shape is legal
    ShapeUsed lock = lodShape->Level(level);
    if (lock.IsNull()) continue;
    Shape *shape = lock.InitShape();
    shape->ClipOrAll(ClipLandOn);
  }
  if (lodShape->GetAnimationType()==AnimTypeSoftware)
  {
    lodShape->SetAnimationType(AnimTypeNone);
  }
}

#if _VBS3_CRATERS_DEFORM_TERRAIN
void MakeCrater(Ref<LODShape> &lodShape)
{
  lodShape->OrSpecial(ClampU|ClampV|IsAlpha|/*OnSurface|*/NoShadow|IsColored);
  lodShape->SetCanBeOccluded(false);
  for (int level = 0; level < lodShape->NLevels(); level++)
  {
#if _VBS3_CRATERS_COLLGEOMETRYWORKING
    ShapeUsed shape = lodShape->LevelDirect(level);
#else
    ShapeUsed shape = lodShape->Level(level);
#endif
    if (shape.IsNull()) continue;
    //shape->ClipOrAll(ClipLandOn);
  }
  if (lodShape->GetAnimationType()==AnimTypeSoftware)
  {
    lodShape->SetAnimationType(AnimTypeNone);
  }
}
#endif

#define PRELOADED_SAFE_METHODCALL(id,method) if (_preloaded[id]) _preloaded[id]->method;

/**
@param skyTexture texture to be used for sky when landscape is NULL
*/
void Scene::Init( Engine *engine, Landscape *landscape, const char *skyTexture, const char *skyTextureR)
{
  // forget all cached entries
  // init ...
  //_engine=engine;
	ResetLensFlaresTime();

  if( _landscape!=landscape ) _landscape=landscape;
  AbstractTextBank *bank = engine->TextBank();
  if( _landscape )
  {
    // some shapes have been modified and must be reloaded

    _landscape->SetOvercast(0.0f);
    _landscape->SetFog(0.0f);
    _landscape->SetWind(0.1f);
    ProgressRefresh();

    if (!_preloaded[CraterShell])
    {
      ParamEntryVal cfgCore = Pars >> "CfgCoreData";

      RString name = cfgCore >> "craterShell";
      _preloaded[CraterShell]=Shapes.New(name,ShapeOnSurface);
      name = cfgCore >> "craterBullet";
      _preloaded[CraterBullet]=Shapes.New(name,ShapeOnSurface);
      MakeStain(_preloaded[CraterBullet]);

#if _ENABLE_DATADISC
      name = cfgCore >> "slopBlood";
      _preloaded[SlopBlood]=Shapes.New(name,ShapeOnSurface);
#endif
      name = cfgCore >> "cloudletBasic";
      NewSprite(name, ShapeNormal, _preloaded[CloudletBasic]);
      name = cfgCore >> "cloudletFire";
      NewSprite(name, ShapeNormal, _preloaded[CloudletFire]);
      name = cfgCore >> "cloudletFireD";
      NewSprite(name, ShapeNormal, _preloaded[CloudletFireD]);
      name = cfgCore >> "cloudletWater";
      NewSprite(name, ShapeNormal, _preloaded[CloudletWater]);
      name = cfgCore >> "cloudletMissile";
      NewSprite(name, ShapeNormal, _preloaded[CloudletMissile]);

      const int cloudletSpec=ClampU|ClampV|NoZWrite|IsAlpha|IsAlphaFog|NoShadow|IsColored;
      PRELOADED_SAFE_METHODCALL(CloudletBasic, OrSpecial(cloudletSpec));
      PRELOADED_SAFE_METHODCALL(CloudletFire, OrSpecial(cloudletSpec));
      PRELOADED_SAFE_METHODCALL(CloudletFireD, OrSpecial(cloudletSpec));
      PRELOADED_SAFE_METHODCALL(CloudletWater, OrSpecial(cloudletSpec));
      PRELOADED_SAFE_METHODCALL(CloudletMissile, OrSpecial(cloudletSpec));

#if _VBS3_CRATERS_DEFORM_TERRAIN
      MakeCrater(_preloaded[CraterShell]);
#else
      MakeStain(_preloaded[CraterShell]);
#endif

#if _ENABLE_DATADISC
      MakeStain(_preloaded[SlopBlood]);
#endif

      name = cfgCore >> "textureTIConversion";
      GEngine->LoadTIConversionTexture(name);

#if _VBS3_LODTRANSITIONCONTROL
      // Init the LOD transition speed
      {
        _lodTransitionSpeed = 0.125f;
        ParamEntryPtr lts = cfgCore.FindEntry("lodTransitionSpeed");
        if (lts)
        {
          _lodTransitionSpeed = *lts;
        }
      }
#endif

      ProgressRefresh();

      name = cfgCore >> "cobraLight";
      _preloaded[CobraLight]=Shapes.New(name,false,false);
      name = cfgCore >> "sphereLight";
      _preloaded[SphereLight]=Shapes.New(name,false,false);
      name = cfgCore >> "halfLight";
      _preloaded[HalfLight]=Shapes.New(name,false,false);
      name = cfgCore >> "marker";
      _preloaded[Marker]=Shapes.New(name,false,false);
      
      name = cfgCore >> "footStepL";
      _preloaded[FootStepL]=Shapes.New(name,ShapeOnSurface);
      name = cfgCore >> "footStepR";
      _preloaded[FootStepR]=Shapes.New(name,ShapeOnSurface);
      ProgressRefresh();

      name = cfgCore >> "paperCarModel";
      _preloaded[PaperCarModel]=Shapes.New(name,true,true);
      name = cfgCore >> "halfSpaceModel";
      _preloaded[HalfSpaceModel]=Shapes.New(name,false,false);
      name = cfgCore >> "forceArrowModel";
      _preloaded[ForceArrowModel]=Shapes.New(name,false,false);
      name = cfgCore >> "sphereModel";
      _preloaded[SphereModel]=Shapes.New(name,false,false);
      name = cfgCore >> "rectangleModel";
      _preloaded[RectangleModel]=Shapes.New(name,false,false);
      name = cfgCore >> "rayModel";
      _preloaded[RayModel]=Shapes.New(name,false,false);
      name = cfgCore >> "gunLightModel";
      _preloaded[GunLightModel]=Shapes.New(name,false,false);
      
      
      // gizmo for Buldozer
      bool isGizmoModelValid = cfgCore.CheckIfEntryExists("gizmoModel");
      if (isGizmoModelValid)
        name = cfgCore >> "gizmoModel";
      else
        name = "core\\default\\default.p3d";  
      
      _preloaded[GizmoModel]=Shapes.New(name,false,false);

      _preloaded[GizmoModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      _preloaded[GunLightModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      _preloaded[RayModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      _preloaded[ForceArrowModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      _preloaded[HalfSpaceModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      _preloaded[SphereModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      _preloaded[RectangleModel]->OrSpecial(IsColored|IsAlpha|IsAlphaFog);
      ProgressRefresh();

/*
      name = cfgCore >> "skySphere";
      _preloaded[SkySphere] = Shapes.New(name,false,false);
      _preloaded[SkySphere]->OrSpecial(FilterLinear);
      name = cfgCore >> "horizontObject";
      _preloaded[HorizontObject] = Shapes.New(name,false,false);
      ProgressRefresh();
*/

/*
      ParamEntryVal veh = Pars>>"CfgVehicles";

      #define FX_LOAD(name) \
        _preloaded[name] = Shapes.New \
        ( \
          GetShapeName(veh>>#name>>"model"),veh>>#name>>"reversed",true \
        )

      FX_LOAD(FxExploGround1);
      FX_LOAD(FxExploGround2);
      ProgressRefresh();

      FX_LOAD(FxExploArmor1);
      FX_LOAD(FxExploArmor2);
      FX_LOAD(FxExploArmor3);
      FX_LOAD(FxExploArmor4);
      ProgressRefresh();

      FX_LOAD(FxCartridge);
*/
      
      const int FootStepFlags=IsAlpha|NoShadow|NoZWrite|IsAlphaFog|OnSurface|IsColored;
      _preloaded[FootStepL]->OrSpecial(FootStepFlags);
      _preloaded[FootStepR]->OrSpecial(FootStepFlags);

      //_preloaded[BulletLine] = ObjectLine::CreateShape();
      //_preloaded[BulletLine]->OrSpecial(DstBlendOne|TracerLighting); // Make the tracer shine

      AdaptVolumeLight(_preloaded[CobraLight],false); // used for headlights
      AdaptVolumeLight(_preloaded[SphereLight],true); // used for flares
      AdaptVolumeLight(_preloaded[HalfLight],false); // used for streetlights
      AdaptVolumeLight(_preloaded[Marker],true); // used for vehicle positional lights

      name = cfgCore >> "collisionShape";
      LODShape *collisionShape=Shapes.New(name,false,false);
      collisionShape->OrSpecial(IsAlpha|IsAlphaFog|IsColored);
      _collisionStar=new ObjectColored(collisionShape,VISITOR_NO_ID);
    }

  }
  else
  {
    //_forceScale=NULL;
    _skyTexture=bank->Load(skyTexture);
    _skyTextureR=bank->Load(skyTextureR);
    Color skyColor = _skyTexture ? _skyTexture->GetPixel(0,1,1) : HWhite;
    Color skyTopColor = _skyTexture ? _skyTexture->GetPixel(0,1,0) : HWhite;
    SetSkyColor(skyColor,skyTopColor);
  }

  ProgressRefresh();
}

void Scene::SetSkyColor(ColorVal skyColor, ColorVal skyTopColor)
{
  // guarantee that the mipmap is loaded
  _skyColor = skyColor;
  _skyTopColor = skyTopColor;
}

#if _HELISIM_RENDER_TO_TEXTURE

bool Scene::SetDrawObjectsIndex(int newidx)
{
  Assert(newidx >= 0 && newidx < Object::NumDrawObjects);

  if(newidx > Object::NumDrawObjects || newidx < 0) return false;

  if(newidx == _drawObjectsId) return true;

  _drawObjectsStore[_drawObjectsId] = _drawObjects;
  _drawObjects = _drawObjectsStore[newidx];
  _drawObjectsId = newidx;

  return true;
}

void Scene::ReleaseDrawObject(Object *obj)
{
  SortObject *sortObject;

  for(int d = 0; d < Object::NumDrawObjects; d++)
  {
    if ((sortObject = obj->GetInList(d)) != NULL)
      _drawObjectsStore[d].DeleteKey(sortObject);
  }
}

#endif

void Scene::CleanUpDrawObjects()
{
#if _HELISIM_RENDER_TO_TEXTURE
  for(int d = 0; d < Object::NumDrawObjects; d++)
  {
    for (int i = 0; i < _drawObjectsStore[d].Size(); i++)
      _drawObjectsStore[d][i]->GetObject()->SetInList(NULL, d);

    _drawObjectsStore[d].Resize(0);
  }
  for (int i=0; i<_drawObjects.Size(); i++)
  {
    _drawObjects[i]->GetObject()->SetInList(NULL, _drawObjectsId);
  }
#else
  for( int i=0; i<_drawObjects.Size(); i++ )
  {
    _drawObjects[i]->GetObject()->SetInList(NULL);
  }
#endif
  _drawObjects.Resize(0);
  _proxyObjects.Resize(0);
}

void Scene::CleanUp()
{
  #if !_DEBUG && !_PROFILE
  CleanUpDrawObjects();
  #endif
  _arrows.Clear();
  _diagText.Clear();
  _irLasers.Clear();
}

Scene::~Scene()
{
	if (_enfShapeCollection)
	{
		delete _enfShapeCollection;
		_enfShapeCollection = NULL;
	}

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  if (ParseUserParams(cfg, &globals))
  {
    SaveConfig(cfg);
    SaveUserParams(cfg);
  }

  ResetLights();
  if( _mainLight ) delete _mainLight,_mainLight=NULL;
  if( _camera ) delete _camera,_camera=NULL;
  _skyTexture=NULL;
  _skyTextureR=NULL;
  _landscape.Free();
  GLandscape=NULL;
}

void Scene::SetCamera( const Camera &camera )
{
  *_camera=camera;
}

FogFunction::FogFunction()
{
  _start2=0;
  _end2=1;
  _divisor=(LEN_FOG_TABLE-1)/_end2;
}

void FogFunction::Set(
  float start, float end, float expFogCoef,
  float (*function)(float distRel, float start, float end, float expFogCoef)
)
{
  _start2=start*start;
  _end2=end*end;
  _divisor=(LEN_FOG_TABLE-1)/_end2;
  for( int i=0; i<LEN_FOG_TABLE; i++ )
  {
    // index into the table is i, corresponding relative distance is
    float indexRel=i*(1.0f/(LEN_FOG_TABLE-1));
    // indexRel corresponds to (dist*dist)/(_end*_end)
    // we would like to calculate dist/_end
    float dist=sqrt(indexRel*_end2);
    float fogF = function(dist,start,end,expFogCoef);
    int fog=toInt(fogF*255);
    if( fog<0 ) fog=0;else if( fog>255 ) fog=255;
    _fog[i]=fog;
  }
  _fog[LEN_FOG_TABLE-1]=0xff; // full fog
  //_fog[LEN_FOG_TABLE-1]=1.0; // full fog
}

int FogFunction::operator () ( float distSquare ) const
{
  // calculate index between 0 and 1
  float indexFlt=distSquare*_divisor;
  if (indexFlt>=LEN_FOG_TABLE) return _fog[LEN_FOG_TABLE-1];
  int index=toIntFloor(indexFlt);
  if( index<=0 ) return _fog[0];
  if( index<LEN_FOG_TABLE ) return _fog[index];
  return _fog[LEN_FOG_TABLE-1];
}

//-----------------------------------------------------------------------------
void Scene::AddLightReal( Light *light )
{
  int i;
  // if possible insert the light into some empty slot
  for( i=0; i<_realLights.Size(); i++ ) if( !_realLights[i] )
  {
    _realLights[i]=light;
    return;
  }
  _realLights.Add(light);
}

//-----------------------------------------------------------------------------
void Scene::AddLightAgg( Light *light )
{
  int i;
  // if possible insert the light into some empty slot
  for( i=0; i<_aggLights.Size(); i++ ) if( !_aggLights[i] )
  {
    _aggLights[i]=light;
    return;
  }
  _aggLights.Add(light);
}

//-----------------------------------------------------------------------------
void Scene::ResetLights()
{
  _realLights.Clear();
  _aggLights.Clear();
}

//-----------------------------------------------------------------------------
static int CmpALightCtx
(
  const ActiveLightPointer *l0, const ActiveLightPointer *l1,
  LightContext *context
)
{
  const Light *light0=*l0;
  const Light *light1=*l1;
  Assert( light0 );
  Assert( light1 );
  Assert( light0!=light1 );
  return light1->Compare(*light0,*context);
}

//-----------------------------------------------------------------------------
static int CmpALight( const ActiveLightPointer *l0, const ActiveLightPointer *l1 )
{
  const Light *light0=*l0;
  const Light *light1=*l1;
  Assert( light0 );
  Assert( light1 );
  Assert( light0!=light1 );
  return light1->Compare(*light0);
}

/*!
\patch_internal 1.01 Date 6/18/2001 by Ondra
  This function has been introduces to optimize lights used on landscape.
\patch 1.16 Date 6/18/2001 by Ondra
  Fixed: Night landscape lighting
  (terrain was black sometimes even directly under lamppost).
*/

// Spot lights frustum cut
#if _ENABLE_CHEATS
static bool EnableSpotLightsFC = true;
#else
const bool EnableSpotLightsFC = true;
#endif

//-----------------------------------------------------------------------------
const LightList &Scene::SelectAggLights(Vector3Par pos, float radius, LightList &work)
{
  PROFILE_SCOPE_EX(obLSl,drw);
#if _ENABLE_CHEATS
  if (GInput.GetCheatXToDo(CXTEnableSpotLightsFC))
  {
    EnableSpotLightsFC = !EnableSpotLightsFC;
    GlobalShowMessage(500, "EnableSpotLightsFC %s", EnableSpotLightsFC ? "On":"Off");
  }
#endif

#if _ENABLE_CHEATS
  static float lightThold = 0.02f;
  int dir = GInput.GetCheatXToDo(CXTLightThold);
  if (dir>0)
  {
    lightThold *= 1.1f;
    GlobalShowMessage(500,"Light Thold %.2f",lightThold);
  }
  else if (dir<0)
  {
    lightThold /= 1.1f;
    GlobalShowMessage(500,"Light Thold %.2f",lightThold);
  }
#else
  const float lightThold = 0.02f;
#endif
  // select only the nearest lights to make lighting faster
  int i;
  for( i=0; i<_aAggLights.Size(); i++ )
  {
    // Select only light which can add something to object lighting
    Light *light = _aAggLights[i];

		if (light->Cull(pos, radius))
			continue;

		work.Add(light);
	}

  /**/
  #if 0 //!_RELEASE // light calculation statistics
    static int sumW,sumA;
    static int counter;
  sumW+=work.Size(),sumA+=_aAggLights.Size();
    if( ++counter==100 )
    {
      counter=0;
      Log("Lights: %d of %d",sumW,sumA);
    }
  #endif

  // try to optimize lights
  if( work.Size()>1)
  {
    LightContext context;
    context.position = pos;
    context.radius = radius;
    QSort(work,&context,CmpALightCtx);
    //QSort(work, CmpALight);
    if (work.Size() > GEngine->GetMaxPSLightsCount())
    {
      work.Resize(GEngine->GetMaxPSLightsCount());
    }
    #if 0 //_ENABLE_REPORT
      if (radius>100)
      {
        LogF("Light report");
        for (int i=0; i<work.Size(); i++)
        {
          Light * l = work[i];
          LogF
          (
            "  %d: dist %.2f, bright %.2f, type %s",
            i,
            sqrt(l->SquareDistance(pos)),
            l->Brightness(),
            typeid(*l).name()
          );
        }
      }
      else
      {
      }
    #endif
  }
  return work;
}


//-----------------------------------------------------------------------------
const float LightGridRadius = sqrt(2 * LIGHTGRID_SIZE * LIGHTGRID_SIZE) * 0.5f;

const LightList &Scene::SelectAggLights( Vector3Par objPos, const Object *object, int level, LightList &work)
{
  int spec = object->GetObjSpecial(object->GetSpecial());
  // when sun is disabled, we use all lights in the scene - this is used for UI objects
  if (spec & DisableSun) return _aAggLights;

	#ifndef ENABLE_DAY_LIGHTS
		if( (spec&DisableSun)==0 && MainLight()->NightEffect() < 0.01 )
		{
			// no lights used - full day light
			return _noLights;
		}
	#endif

  if (object->Airborne())
  { // airborne object cannot use precalculated lights, as those are calculated for the surface
    SelectAggLights(objPos,LightGridRadius,work);
    return work;
  }
  // may return work or something else
  LODShape *lShape=object->GetShape();
  ShapeUsed oShape=lShape->Level(level);

  // no light selection for shadows
  if (spec&IsShadow) return _noLights;

  return GetLightGridItem(objPos.X(), objPos.Z());
}

//-----------------------------------------------------------------------------
LightList::LightList()
{
}

//-----------------------------------------------------------------------------
LightList::LightList( const LightList &src )
{
  Realloc(src.Size());
  Resize(src.Size());
  // note: we assume LightList does not need any destruction
  CTraits::CopyData(Data(),src.Data(),src.Size());
}

//-----------------------------------------------------------------------------
int LightList::Compare(const LightList &src) const
{
  // if light count is different, light list cannot be identical
  if (src.Size()!=Size()) return Size()-src.Size();
  for (int i=0; i<Size(); i++)
  {
    const Light *light1 = Get(i);
    const Light *light2 = src.Get(i);
    if (light1>light2) return +1;
    if (light1<light2) return -1;
  }
  return 0;
}

//-----------------------------------------------------------------------------
bool LightList::ContainsIdenticalLights(const LightList &src) const
{
  // if light count is different, light list cannot be identical
  if (src.Size()!=Size()) return false;
  for (int i=0; i<Size(); i++)
  {
    if (Get(i)!=src.Get(i)) return false;
  }
  return true;
}

//-----------------------------------------------------------------------------
void Scene::SetActiveRealLights(const LightList &lights)
{
  _aRealLights.Resize(0); 
  // copy all lights from the list
  for( int i=0; i<lights.Size(); i++ )
  {
    Light *light=lights[i];
    _aRealLights.Add(light);
  }
}

//-----------------------------------------------------------------------------
static inline float TotalLightBrightness(LightInfo &light)
{
  return (light.color.Brightness()+light.ambient.Brightness());
}

//-----------------------------------------------------------------------------
void Scene::SetActiveAggLights(const LightList &lights)
{
  _aAggLights.Resize(0); 
  // copy all lights from the list
  for( int i=0; i<lights.Size(); i++ )
  {
    Light *light=lights[i];
    _aAggLights.Add(light);
  }
  
}

//-----------------------------------------------------------------------------
int Scene::GetLightGridIndex(float x, float z) const
{
  float invLGS = 1.0f / LIGHTGRID_SIZE;
  int ii = toIntFloor(x * invLGS);
  int jj = toIntFloor(z * invLGS);
//  int ii = toInt(x * invLGS);
//  int jj = toInt(z * invLGS);
  if (ii < _gridLightsBoundingRect.xBeg) ii = _gridLightsBoundingRect.xBeg;
  if (jj < _gridLightsBoundingRect.zBeg) jj = _gridLightsBoundingRect.zBeg;
  if (ii > _gridLightsBoundingRect.xEnd) ii = _gridLightsBoundingRect.xEnd;
  if (jj > _gridLightsBoundingRect.zEnd) jj = _gridLightsBoundingRect.zEnd;
  int i = ii - _gridLightsBoundingRect.xBeg;
  int j = jj - _gridLightsBoundingRect.zBeg;
  int dx = _gridLightsBoundingRect.xEnd - _gridLightsBoundingRect.xBeg + 1;
  return j * dx + i;
}

//-----------------------------------------------------------------------------
const LightList &Scene::GetLightGridItem(float x, float z) const
{
  // In case of zero lights or in case there is a day, zero and return original lights
  if (_gridLights.Size() == 0/*(NLights()==0 || MainLight()->NightEffect()<0.01)*/)
  {
    return _noLights;
  }
  else
  {
    return *_gridLights[GetLightGridIndex(x, z)];
  }
}

//-----------------------------------------------------------------------------
void Scene::SelectLightsIntoGrid(bool r2t)
{
  PROFILE_SCOPE_GRF_EX(selLi,*,0);

	// In case of zero lights or in case there is a day, clear grid and return
	#ifdef ENABLE_DAY_LIGHTS
		if (_aAggLights.Size() == 0)
	#else
		if (_aAggLights.Size()==0 || MainLight()->NightEffect() < 0.01f)
	#endif
  {
    _gridLights.Clear();
    return;
  }

  // Get the grid position and dimension
  float maxObjDist = floatMin(GetFogMaxRange(), Glob.config.objectsZ);
  saturateMin(maxObjDist, r2t ? 500.0f : 1500.0f);
  CalculBoundingRect(_gridLightsBoundingRect, maxObjDist, LIGHTGRID_SIZE);

  // Resize the field of the lights grid
  int dx = _gridLightsBoundingRect.xEnd - _gridLightsBoundingRect.xBeg + 1;
  int dz = _gridLightsBoundingRect.zEnd - _gridLightsBoundingRect.zBeg + 1;
  DoAssert(dx > 0);
  DoAssert(dz > 0);
  _gridLights.Resize(dx * dz);

	#ifdef LOG_LIGHT_GRID
		LogF("--------------------------");
		int fullLights = 0;
	#endif

  // Get through all the grid items and select the most important lights for each of them
  for (int z = 0; z < dz; z++)
  {
    float coordZ = _gridLightsBoundingRect.zBeg * LIGHTGRID_SIZE + LIGHTGRID_SIZE * z + LIGHTGRID_SIZE * 0.5f;
    //float coordZ = _gridLightsBoundingRect._begZ * LIGHTGRID_SIZE + LIGHTGRID_SIZE * z;
  	
		#ifdef LOG_LIGHT_GRID
			char row [128];
			char *cchar = &row[0];
		#endif

		for (int x = 0; x < dx; x++)
	  {
      float coordX = _gridLightsBoundingRect.xBeg * LIGHTGRID_SIZE + LIGHTGRID_SIZE * x + LIGHTGRID_SIZE * 0.5f;
      //float coordX = _gridLightsBoundingRect._begX * LIGHTGRID_SIZE + LIGHTGRID_SIZE * x;

      // Add the space for lights into the grid
      Ref<GridItemLightList> &gill = _gridLights[z * dx + x];
      if (gill.IsNull())
      {
        gill = new GridItemLightList;
        gill->Reserve(8, 8);
      }
      gill->Resize(0);

      // Get the lights covering the grid item
      SelectAggLights(Vector3(coordX, _landscape->SurfaceYAboveWaterNoWaves(coordX, coordZ), coordZ), LightGridRadius, *gill);

			#ifdef LOG_LIGHT_GRID
				*cchar++ = '0' + (char) gill->Size();
				fullLights += gill->Size();
			#endif
    }

		#ifdef LOG_LIGHT_GRID
			*cchar++ = 0;
			LogF("%s", row);	
		#endif
  }

	#ifdef LOG_LIGHT_GRID
		LogF("lights %d", fullLights);
	#endif
}


//-----------------------------------------------------------------------------
void Scene::SelectActiveRealLights( Object *dimmed )
{
  _aRealLights.Resize(0);
  
  // enumerate all light objects for drawing
  Vector3Val camPos = GetCamera()->Position();
	float maxLightDist2 = Square(Glob.config.horizontZ);

  for( int i=0; i<_realLights.Size(); i++ )
  {
    Light *light=_realLights[i];
    if( light && light->IsShining() )
    {
      // check if light can be ignored (very far)
      if (light->Position().Distance2(camPos) > maxLightDist2)
      {
        continue;
      }

      bool invisible=false;
      Object *attach=light->AttachedOn();
      if( attach && attach==dimmed ) invisible=true;
      light->ToDraw(ClipAll,invisible);
      //light->SetPosition(Vector3(light->Position().X() - 25, light->Position().Y(), light->Position().Z() - 25));
      _aRealLights.Add(light);
    }
  }

  // we need to know the strongest light source 
  QSort(_aRealLights,CmpALight);
  if (_aRealLights.Size()>Glob.config.maxLights)
  {
    // use only strongest lights
    _aRealLights.Resize(Glob.config.maxLights);
  }
}


//-----------------------------------------------------------------------------
#if _ENABLE_CHEATS
enum DebugTestLightMode
{
	DTLM_LOW_BRIGHTNESS,
	DTLM_SMALL,
	DTLM_ADDED
};

inline void DebugTestAddedLights(DebugTestLightMode mode, Vector3Par pos, float radius)
{
	if (CHECK_DIAG(DELight))//DEDynamicLight))
	{
		int color = mode == DTLM_ADDED ? 0x20708020: mode == DTLM_SMALL ? 0x10400000 : 0x10404040;
		enf::Shape::CreateSphere(pos, radius, color, enf::SS_ONCE|enf::SS_TRANSP|enf::SS_DOUBLESIDE|enf::SS_NOOUTLINE);
	}
}
#endif


//-----------------------------------------------------------------------------
void Scene::SelectActiveAggLights( Object *dimmed )
{
  _aAggLights.Resize(0);

  // enumerate all light objects for drawing
  Vector3Val camPos = GetCamera()->Position();

	// apply perspective
	float cameraX = GetCamera()->Projection()(0,0)/GetCamera()->Left();

	#ifdef ENABLE_DAY_LIGHTS
		static float ambAmplifier = 3.0f;
		float ambBrightness = _mainLight->GetAmbient().Brightness();
	#endif

	float maxLightDist2 = Square(Glob.config.horizontZ);
  for( int i=0; i<_aggLights.Size(); i++ )
  {
    Light *light=_aggLights[i];

		if( light && light->IsShining() )
    {
      // check if light can be ignored (very far)
      float dist2 = light->Position().Distance2(camPos);
      if (dist2 > maxLightDist2)
				continue;
  		
      // the light can be also ignored when its contribution to the scene is extremely small, this is typical for small marker lights
			float bright;

			#ifdef ENABLE_DAY_LIGHTS
				if (light->IsDayLight())
				{
					bright = light->Brightness();

					if (bright < 0.0001f)
						continue;

					//update brightness coef according the day to have always lights in day a little visible
					static_cast<LightPositionedColored*>(light)->SetBrightnessDayCoef(ambBrightness > bright ? 1.0f + ambAmplifier*(ambBrightness/bright - 1.0f): 1.0f);
				}
			#endif

			bright = light->Brightness(GEngine->GetAccomodateEye());
			
			static float minBrightness = 0.05f;
			if (bright < minBrightness)
			{
				#if _ENABLE_CHEATS
					DebugTestAddedLights(DTLM_LOW_BRIGHTNESS, light->Position(), static_cast<LightPositioned*>(light)->GetRadius());
				#endif

				continue;
			}

			float invDist = InvSqrtFast(dist2);
			float projPosX = cameraX*invDist*static_cast<LightPositioned*>(light)->GetRadius();

			//apply brightness a little -> we take bright light with maximum "projection size" of 200 pixels (not exact, for pixel it's ~x4 ) and when the brightness is less, update a little the size
			projPosX *= floatMin(bright, 1.0f);
			static float minContribution = 150.0f;
			if (projPosX > minContribution)
				_aAggLights.Add(light);

			#if _ENABLE_CHEATS
				DebugTestAddedLights(projPosX > minContribution ? DTLM_ADDED : DTLM_SMALL, light->Position(), static_cast<LightPositioned*>(light)->GetRadius());
			#endif
		}
  }

  // we need to know the strongest light source 
  QSort(_aAggLights,CmpALight);
  if (_aAggLights.Size()>Glob.config.maxLights)
  {
    // use only strongest lights
    _aAggLights.Resize(Glob.config.maxLights);
  }
}


//-----------------------------------------------------------------------------
class CompareHeatHazeInstancesFarthestFirst
{
public:
	inline int operator()(const HeatHazeRenderInstance& a, const HeatHazeRenderInstance& b) const
	{
		if (a.LinearZ > b.LinearZ) 
			return -1;

		if (a.LinearZ < b.LinearZ) 
			return 1;

		return 0;
	}
};

//-----------------------------------------------------------------------------
bool Scene::PrepareLightHeatHaze(HeatHazeRenderArray& instances)
{
	instances.Clear();

	//lights are already processed, these really bring some contribution to scene
	for (int i = 0; i< _aAggLights.Size() && !instances.IsFull(); i++ )
	{
		const Light *light= _aAggLights[i];

		if( light && light->IsHeatHazeActive() )
		{
			light->PrepareHeatHaze(instances);
		}
	}

	if (instances.GetCardinality() > 1)
		enf::QuickSortCmp<HeatHazeRenderInstance, CompareHeatHazeInstancesFarthestFirst > (instances.GetCardinality(), instances.GetArray());

	return instances.GetCardinality() > 0;
}



/**
@param land when not NULL, water fog can be calculated using given landscape
*/
void Scene::RecalculateLighting(float allowCloudShadow, Landscape *land)
{
  Color sky,aroundSun;
#if _ARMA3_HDR_IMPROVEMENTS
  float desiredLuminanceCoef = 1.0f;
  float luminanceRectCoef = 1.0f;
#endif 

  if( GetLandscape() )
  {
    // change lighting based on overcast
    float cloudAlpha = GetLandscape()->CloudsAlpha();
    float cloudOcclusion = GetLandscape()->CheckCloudOcclusion(
      GetCamera()->Position(),-MainLight()->LightDirection()
    )*allowCloudShadow;
    //DIAG_MESSAGE(100,Format("Cloud occ %.3f",cloudOcclusion));

    float cloud = cloudOcclusion*cloudAlpha;
    float cosSun = -MainLight()->SunDirection().Y();
  
#if _ARMA3_NEW_LIGHTING_CFG
    if (GetLandscape()->UseNewLandLighting())
    {
      float height = 0.0f;
      if (GetCamera())
        height = GetCamera()->Position()[1] - GetLandscape()->GetSeaLevelNoWave();

      bool ok = GetLandscape()->GetLightingNew(_currentLighting, height, cosSun, cloud);
      ok;
      Assert(ok);


      _mainLight->SetLighting(_currentLighting._ambient, _currentLighting._diffuse, _currentLighting._sun, _currentLighting._sunOrMoon);
      sky = _currentLighting._sky;
      aroundSun = _currentLighting._skyAroundSun;

		#ifdef _ARMA3_HDR_IMPROVEMENTS
			desiredLuminanceCoef = _currentLighting._desiredLuminanceCoef;
			luminanceRectCoef = _currentLighting._luminanceRectCoef;
		#endif
    }
    else
#endif
    {
    Color amb,dif,sun;    
    float sunOrMoon = GetLandscape()->GetLighting(amb,dif,sun,cosSun,cloud);
    
    _mainLight->SetLighting(amb,dif,sun,sunOrMoon);
    GetLandscape()->GetSkyColors(sky,aroundSun,cosSun);

#if _ARMA3_HDR_IMPROVEMENTS
    GetLandscape()->GetHDRCoefs(cosSun, cloud, desiredLuminanceCoef, luminanceRectCoef);
#endif
    }

#if _ARMA3_HDR_IMPROVEMENTS
    if (GEngine)
    {
      if (GetLandscape()->GetDesiredLuminanceCoefOverride() >= 0.0f)
        GEngine->SetDesiredLuminanceCoef(GetLandscape()->GetDesiredLuminanceCoefOverride());
      else
        GEngine->SetDesiredLuminanceCoef(desiredLuminanceCoef);

			GEngine->SetHDRLuminanceRectCoef(0.0f);
			/*
			smaller hdr rect brings problem with point lights - when they shine in the center of screen the rest goes black

      if (GetLandscape()->GetLuminanceRectCoefOverride() >= 0.0f)
        GEngine->SetHDRLuminanceRectCoef(GetLandscape()->GetLuminanceRectCoefOverride());
      else
        GEngine->SetHDRLuminanceRectCoef(luminanceRectCoef);
			*/
    }
#endif //_ARMA3_HDR_IMPROVEMENTS

    #if _ENABLE_CHEATS
      if (CHECK_DIAG(DELight))
      {
        DIAG_MESSAGE(
          100,Format(
            "Angle %.1f, cloud %0.3f (%.3f*%.3f), overcast %.3f",
            asin(cosSun)*(180/H_PI),cloud,cloudOcclusion,cloudAlpha,GetLandscape()->GetOvercast()
          )
        );
        ColorVal la = _mainLight->GetAmbient();
        ColorVal ld = _mainLight->GetDiffuse();
        DIAG_MESSAGE(
          100,Format(
            "Amb=%5.3f,%5.3f,%5.3f, Dif=%5.3f,%5.3f,%5.3f, Sky %5.3f,%5.3f,%5.3f...%5.3f,%5.3f,%5.3f",
            la.R(),la.G(),la.B(),ld.R(),ld.G(),ld.B(),
            sky.R(),sky.G(),sky.B(),aroundSun.R(),aroundSun.G(),aroundSun.B()
          )
        );
      }
    #endif
  }
  else
  {
    sky = HWhite;
    aroundSun = HWhite;
  }
  
  // recalculate background color
  // calculate fog color from sky texture
  // we should apply some lighting (to have dark fog in the night)
  // we might want to do some saturation here
  Color lighting=sky*GEngine->GetAccomodateEye();

  // when under water change fog color to simulate underwater rendering
  
  Color skyColor=_skyColor;
  Color skyTopColor=_skyTopColor;
  if (land)
  {
    float underWater = land->WaterSurfaceY(GetCamera()->Position())-GetCamera()->Position().Y();
    // when around the surface, interpolate between air and water fog color
    const float noWaterFog = -0.3f;
    const float fullWaterFog = +0.1f;
    // when going deeper, change water fog color
    const float deepWaterFog = +10.0f;
    if (underWater>noWaterFog)
    {
      #if 0 //_ENABLE_REPORT
        DIAG_MESSAGE(100,Format("Under water %.3f",underWater));
      #endif
      // seabed color is 0,0.05,0.15
      static const Color waterColor(0.02f,0.08f,0.12f);
      static const Color surfaceColor(0.20f,0.30f,0.25f);
      
      static const Color deepWaterColor(0.01f,0.06f,0.14f);
      static const Color deepSurfaceColor(0.10f,0.18f,0.22f);
      if (underWater>fullWaterFog)
      {
        float waterDeep = InterpolativC(underWater,fullWaterFog,deepWaterFog,0,1);
        skyColor = waterColor*(1-waterDeep)+deepWaterColor*waterDeep;
        skyTopColor = surfaceColor*(1-waterDeep)+deepSurfaceColor*waterDeep;
      }
      else
      {
        float waterFog = InterpolativC(underWater,noWaterFog,fullWaterFog,0,1);
        skyColor = skyColor*(1-waterFog)+waterColor*waterFog;
        skyTopColor = skyTopColor*(1-waterFog)+surfaceColor*waterFog;
      }
    }
  }
  skyColor = skyColor*lighting;
  skyTopColor = skyTopColor*lighting;
  skyColor.SetA(1);
  skyTopColor.SetA(1);
  GEngine->SetFogColor(skyColor,skyTopColor);
}


void Scene::MainLightChanged()
{
  // no reaction to light change is necessary
  // as light will be recalculated several times during rendering
}

void Scene::BeginSunPixelCounting(int cb)
{
  _sunQuery = GEngine->BegPixelCounting(cb, _sunQuery);
}
void Scene::EndSunPixelCounting(int cb)
{
  GEngine->EndPixelCounting(cb, _sunQuery);
}

const Matrix4 &Scene::ScaledInvTransform() const {return _camera->_scaledInvTransform;}
const Matrix3 &Scene::CamNormalTrans() const {return _camera->_camNormalTrans;}
const Matrix4 &Scene::CamInvTrans() const {return _camera->_camInvTrans;}

void Scene::SetComplexityTarget(float value)
{
  _complexityTarget=value;
  PixelSizeFromComplexity(_complexityTarget);
}

float Scene::GetMinimalTerrainGrid() const
{
  // depending on computer speed and visibility, some terrain grid may be unavailable

  // on PIII/733  with HWT&L we will enable max. 2500m/6.25m
  // when compared to 900m/50m, this is about 500 slower

  //float GetPreferredTerrainGrid() const {return _preferredTerrainGrid;}
  //float GetPreferredViewDistance() const {return _preferredViewDistance;}

  // estimate memory requirements
  // estimate computational requirements
  
  // number of terrain squares in the visible area
  
  const float defaultDistance = 900;
  const float defaultGrid = 50;
  float defaultSquares = Square(defaultDistance/defaultGrid);
  
  // terrain should not use more than a given part of total memory
  float maxMemory = GetMemoryUsageLimit()*0.33f;

  // depending on CPU power we scale down as well
  // constant 0.5 is empirical here
  float maxFactor = Glob.config.benchmark*0.5f;

  const float maxTerrainGrid = 30;
  for(
    float minimalTerrainGrid = 3.125;
    minimalTerrainGrid<maxTerrainGrid;
    minimalTerrainGrid *= 2
  )
  {
    // squares without any LOD
    float nSquaresLin = Square(_preferredViewDistance/minimalTerrainGrid);
    // LOD: we can assume each 2x increase increases number of squares 2x as well
    float nSquaresLod = _preferredViewDistance*(defaultSquares/defaultDistance)*(defaultGrid/minimalTerrainGrid);
    // in reality it is something in between
    const float lodCoef = 0.8f;
    float nSquares = nSquaresLin*(1-lodCoef)+nSquaresLod*lodCoef;
    
    float cpuFactor = nSquares/defaultSquares;
    const int perSquare = 200;  // empirical - estimate memory usage per square
    /// 4 is here because we need to be able to hold everything around, not only view-port
    float memoryRequired = nSquares*4*perSquare;
    // if we would be too slow, reduce one step more
    if (cpuFactor<=maxFactor && memoryRequired<=maxMemory)
    {
      LogF(
        "Distance %.1f - min grid %g, Slowdown CPU factor %.1f (max. %.1f), memory required %.1f MB (max %.1f MB)",
        _preferredViewDistance,minimalTerrainGrid,
        cpuFactor,maxFactor,memoryRequired/(1024*1024),maxMemory/(1024*1024)
      );
      return minimalTerrainGrid;
    }
  }
  return maxTerrainGrid;
}

void Scene::SetPreferredTerrainGrid(float x)
{
  /*
  float minGrid = GetMinimalTerrainGrid();
  if (x<minGrid)
  {
    LogF("Grid %.1f would be too slow, clamping to %.1f",x,minGrid);
    x = minGrid;
  }
  */
  saturate(x,0.5,100);
  _preferredTerrainGrid=x;
  if (GWorld)
    GWorld->OnPreferredTerrainGridChanged(GWorld->GetMode());
  // we need to recalculate the cache
  if (_landscape)
    _landscape->OnTerrainGridChanged();
}

void Scene::SetPreferredViewDistance(float x)
{
  // no saturation here - done in the UI
  //saturate(x,500,10000);
  _preferredViewDistance=x;

#if _HELISIM_VIEW_DIST
  _preferredObjectViewDistance = -1;
#endif

  float minGrid = GetMinimalTerrainGrid();
  float grid = GetPreferredTerrainGrid();
  if (grid<minGrid)
  {
    LogF("Warning: Terrain grid %.1f will be too slow, should use %.1f",grid,minGrid);
    //_preferredTerrainGrid=grid;
  }

  if (GWorld)
    GWorld->OnPreferredViewDistanceChanged(GWorld->GetMode());
}

#if _HELISIM_VIEW_DIST
float Scene::GetPreferredObjectViewDistance() const 
{
  return _preferredObjectViewDistance;
}

void Scene::SetPreferredObjectViewDistance(float x, bool update)
{
  x = floatMin(x,_preferredViewDistance);

  _preferredObjectViewDistance = x;

  if (update && GWorld)
    GWorld->OnPreferredViewDistanceChanged(GWorld->GetMode());
}
#endif

#if _HELISIM_AMBIENT_TRAFFIC
float Scene::GetPreferredTrafficViewDistance() const 
{
  return _preferredTrafficViewDistance;
}

void Scene::SetPreferredTrafficViewDistance(float x)
{
  _preferredTrafficViewDistance = x;
}
#endif

void Scene::SetShadowQuality(int value)
{
  _shadowQuality=value;
  if (GWorld) GWorld->AdjustDerivedVisibility();
  if (value>=SB_SQ)
    GEngine->EnableSBShadows(value-SB_SQ+1);
  else
    GEngine->EnableSBShadows(0);
}

#if _HELISIM_SHADOW_ZDISTANCE
void Scene::SetShadowZDistance(float zDistance)
{
  _shadowZDistance = zDistance;
  Glob.config.SetMaxShadowSize(zDistance);
  saturateMin(_shadowZDistance, Glob.config.MaxShadowZDistance);
}
#endif

void Scene::SetShadingQuality(float val)
{
  _shadingQuality=val;
  /*
  // postprocess effects are now separate
  if (val >= DOF_SHQ)
  {
    GEngine->EnableDepthOfField(val-DOF_SHQ+1);
    _landscape->OnShadingQualityChanged();
  }
  else
  {
    _landscape->OnShadingQualityChanged();
    GEngine->EnableDepthOfField(0);
  }
  */
}

void Scene::PixelSizeFromComplexity(float complexity)
{
  _minPixelSizeMaxDist = pow(3000000.0f/complexity,0.77f);
  _minPixelSizeMidDist = pow(1200000.0f/complexity,0.55f);
  _minPixelSizeMinDist = pow(1000000.0f/complexity,0.50f);

  // rendering below 1 pixel has little sense
  // note: it may have some sense with AntiAliasing, SuperSampling ...

  // at extreme close we want to always see almost everything
  saturate(_minPixelSizeMinDist,1,2);
  // at rifle engagement range we want to always see well
  // this is important for combat
  saturate(_minPixelSizeMidDist,1,4);
  saturateMax(_minPixelSizeMaxDist,1);
}

/// LOD manager and pixel size limit can consider object fogging
/**
However measurements did not show any significant performance improvement when doig this
and visual impact seemed to be negative, in spite of some close objects being rendered now.
*/
const bool FogLODsDefault = false;

#if _ENABLE_CHEATS
static bool FogLODs = FogLODsDefault;
#else
const bool FogLODs = FogLODsDefault;
#endif

inline float Scene::MinPixelSize2FromDist2(float dist2) const
{
  static float fogFactor = 0.8f;
  float factor = FogLODs ? fogFactor : 1.0f;
  const float maxDist = 350;
  const float midDist = 200;
  const float minDist = 20;
  if (dist2>Square(midDist))
  {
    // mid..max..infinity
    return factor*InterpolativC(
      dist2,Square(midDist),Square(maxDist),
      Square(_minPixelSizeMidDist),Square(_minPixelSizeMaxDist)
    );
  }
  else
  {
    // zero..min..mid
    return factor*InterpolativC(
      dist2,Square(minDist),Square(midDist),
      Square(_minPixelSizeMinDist),Square(_minPixelSizeMidDist)
    );
  }
}

/*!
\patch 1.82 Date 8/14/2002 by Ondra
- New: New userInfo.cfg entries fovLeft and fovTop to improved wide-screen support.
(Default values are fovLeft = 1, fovTop = 0.75. For three monitor view use fovLeft = 3).
*/
void Scene::LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg)
{
  // first of all attempt auto-detection
  _defaultViewDistance = Pars>>"CfgDefaultSettings">>"defaultVisibility";
  _defaultTerrainGrid = Pars>>"CfgDefaultSettings">>"defaultTerrainGrid";
#if _HELISIM_VIEW_DIST
  _defaultObjectViewDistance = Pars>>"CfgDefaultSettings">>"defaultObjectViewDistance";
#endif
  _hazeDefaultDistance = Pars>>"CfgDefaultSettings">>"hazeDefaultDistance";

  // NOTE - we don't want to set clouds quality here, because it is already set in engine
  SetDefaultSettingsLevel(GEngine->GetDeviceLevel(),false);
  
  // now override settings by those set in the config
  SetPreferredViewDistance(cfg.ReadValue("viewDistance",_preferredViewDistance));
#if _HELISIM_VIEW_DIST
  SetPreferredObjectViewDistance(cfg.ReadValue("preferredObjectViewDistance", _preferredObjectViewDistance));
#endif
#if _HELISIM_AMBIENT_TRAFFIC
  SetPreferredTrafficViewDistance(cfg.ReadValue("preferredTrafficViewDistance", _preferredTrafficViewDistance));
#endif
  SetPreferredTerrainGrid(cfg.ReadValue("terrainGrid",_preferredTerrainGrid));
  _complexityTarget = cfg.ReadValue("sceneComplexity",_complexityTarget);
  PixelSizeFromComplexity(_complexityTarget);
  _shadingQuality = cfg.ReadValue("shadingQuality",_shadingQuality);
  _shadowQuality = cfg.ReadValue("shadowQuality",_shadowQuality);
#if _HELISIM_SHADOW_ZDISTANCE
  float shadowZDistance = cfg.ReadValue("shadowZDistance",Glob.config.DefaultShadowZDistance);
  SetShadowZDistance(shadowZDistance);
#endif
  if (_shadowQuality>=SB_SQ)
  {
    GEngine->EnableSBShadows(_shadowQuality-SB_SQ+1);
  }
  else
  {
    GEngine->EnableSBShadows(0);
  }
  GEngine->CommitSettings();
}

// see also ComplexityAccess::_values
static const int ComplexityValues[]={1000000,500000,300000,200000,150000};

// see also ShadingQualityAccess::_values
static const int ShadingQualityValues[]={100,10,7,3,0};

static const float ViewDistanceFromLevel[]= {1000/1600.0f, 1200/1600.0f, 1600/1600.0f, 2400/1600.0f, 3600/1600.0f};

void Scene::SetDefaultSettings()
{
#if _HELISIM_VIEW_DIST  
  SetPreferredViewDistance(GetDefaultViewDistance());
  SetPreferredObjectViewDistance(GetDefaultObjectViewDistance());
#endif
#if _HELISIM_SHADOW_ZDISTANCE
  SetShadowZDistance(100.0f); 
#endif
  SetComplexityTarget(300000);
  SetPreferredTerrainGrid(GetDefaultTerrainGrid());
}

void Scene::SetDefaultSettingsLevel(int level, bool setClouds)
{
  if (Glob.config.videoOptions._overallSettings.Size() == 0)
  {
    // no overall settings loaded, set default values
    SetDefaultSettings();
    return;
  }

  level = (level>>8);
  saturate(level, 0, Glob.config.videoOptions._overallSettings.Size()-1);
  const VideoOptionsConfig::OverallQualityItem& item = Glob.config.videoOptions._overallSettings[level];

  SetPreferredViewDistance(item._visibility);

#if _HELISIM_VIEW_DIST
  SetPreferredObjectViewDistance(item._objectsVisibility);
#endif
#if _HELISIM_SHADOW_ZDISTANCE
  SetShadowZDistance(item._shadowsVisibility); 
#endif
  SetComplexityTarget(item._objectQuality._value);
  SetPreferredTerrainGrid(item._terrainQuality._value);
  SetShadingQuality(ShadingQualityValues[4-level]);
  SetShadowQuality(item._shadowQualityIndex);
#if _ARMA3_SIMUL_WEATHER
  if (setClouds && GLandscape)
    GLandscape->SetSimulWeatherLayersCount(item._cloudQuality._value);
#endif
}

/*!
\patch 5093 Date 11/30/2006 by Ondra
- Fixed: Lowering terrain grid increased overall settings indication.
\patch 5122 Date 1/22/2007 by Ondra
- Fixed: Setting overall settings to Very low was not properly indicated in video options.
\patch 5161 Date 5/28/2007 by Ondra
- Fixed: Overall quality indication in the options changed for large view distances.
*/
int Scene::GetCurrentSettingsLevel() const
{
  int maxILevel = -1;
  for (int i=0; i<lenof(ViewDistanceFromLevel); i++)
  {
    if (_preferredViewDistance<_defaultViewDistance*ViewDistanceFromLevel[i]*1.1f)
    {
      maxILevel = i-1;
      break;
    }
  }
  if (_preferredViewDistance>_defaultViewDistance*1.05f)
  {
    int over = toInt(_preferredViewDistance/_defaultViewDistance);
    saturateMax(maxILevel,over);
  }
  if (_preferredTerrainGrid<_defaultTerrainGrid)
  {
    int over = toInt(log(_defaultTerrainGrid/_preferredTerrainGrid)/log(2));
    saturateMax(maxILevel,1+over);
  }
  if (_shadowQuality<3)
  {
    saturateMax(maxILevel,_shadowQuality-2);
  }
  else if (_shadowQuality==3)
  {
    // shadow level 3 is actually considered Low impact
    saturateMax(maxILevel,0);
  }
  else
  {
    // shadow level Very High causes high
  saturateMax(maxILevel,_shadowQuality-1);
  }
  
  // complexity is complex - we need to know thresholds here
  
  int complexLevel = 0;
  for (int i=0; i<lenof(ComplexityValues); i++)
  {
    if (ComplexityValues[i]*0.8f<_complexityTarget)
    {
      complexLevel = i;
      break;
    }
  }
  int shadingLevel = 0;
  for (int i=0; i<lenof(ShadingQualityValues); i++)
  {
    if (ShadingQualityValues[i]<=_shadingQuality)
    {
      shadingLevel = i;
      break;
    }
  }
  // High means very high here
  int maxHighLevel = -1;
  saturateMax(maxHighLevel,3-complexLevel);
  saturateMax(maxHighLevel,3-shadingLevel);
  if (maxHighLevel>2) maxHighLevel++;
  saturateMax(maxILevel,maxHighLevel);
  return (maxILevel+1)<<8;
}


void Scene::SaveConfig(ParamFile &cfg) const
{
  if (!IsOutOfMemory())
  {
    cfg.Add("sceneComplexity",_complexityTarget);
    //cfg.Add("materialComplexity",_materialComplexity);
    cfg.Add("shadingQuality",_shadingQuality);
    cfg.Add("shadowQuality",_shadowQuality);
#if _HELISIM_SHADOW_ZDISTANCE
    cfg.Add("shadowZDistance",_shadowZDistance);
#endif
    cfg.Add("viewDistance",GetPreferredViewDistance());
#if _HELISIM_VIEW_DIST
    cfg.Add("preferredObjectViewDistance",GetPreferredObjectViewDistance());
#endif
#if _HELISIM_AMBIENT_TRAFFIC
    cfg.Add("preferredTrafficViewDistance",GetPreferredTrafficViewDistance());
#endif
    cfg.Add("terrainGrid",GetPreferredTerrainGrid());
  }
}

/*
static void RecalcFrameDuration()
{
  float minFPS = TargetFrameRate*(10.0/15);
  float maxFPS = TargetFrameRate*(20.0/15);
  MaxTargetFrameDuration = 1000/minFPS;
  MinTargetFrameDuration = 1000/maxFPS;
}
*/

extern bool ObjViewer;

void Scene::OnCameraCut()
{
  /*
  struct ResetLODMemory
  {
    bool operator () (SortObject *oi) const
    {
      oi->srcLOD = oi->dstLOD = oi->drawLOD;
      oi->blendFactor = -1;
      return false;
    }
  };
  // reset lod blending/memory for all objects
  _drawObjects.ForEachF(ResetLODMemory());
  */
  CleanUpDrawObjects();
}

void Scene::OnCameraChanged()
{
  // camera changed significantly
  // reset visibility testing for all objects
  _sunQuery.Free();
  OnCameraCut();

	ResetLensFlaresTime();
}

#if _VBS3_LAND_SHADOW
void Scene::CalculateShadowBigRect()
{
  _shadowMapCache->CalculateBigRect();
}

void Scene::CalculateLandShadows()
{
  _shadowMapCache->IncrementalUpdate();
}

void Scene::ResetLandShadows(bool invalidateSegments)
{
  _shadowMapCache->Clear(invalidateSegments);
}
#else
void Scene::CalculateLandShadows(const GridRectangle &bigRect, bool init)
{
  _shadowMap->Calculate(GetLandscape(),bigRect,*this,init);
}

void Scene::ResetLandShadows()
{
  _shadowMap->Clear();
}
#endif

void Scene::ResetLensFlaresTime()
{
	_smoothLensSunTime.Init(DAYZ_LENSFLARES_TIMEOUT);
	_smoothLensFlaresTime.Init(DAYZ_LENSFLARES_TIMEOUT);
}

/*
static __forceinline float Bilint(
  float y00, float y01,
  float y10, float y11,
  float xf, float zf
)
{
  float y0z = y00*(1-zf) + y01*zf;
  float y1z = y10*(1-zf) + y11*zf;
  return y0z*(1-xf) + y1z*xf;
}
*/

/*!
\patch 5137 Date 3/3/2007 by Ondra
- Fixed: Reduced shadowing caused by the terrain. 
*/
static inline float ConvertDepthToShadow(float depth, float terrainGrid)
{
  #if _ENABLE_CHEATS
  static bool NoLandShadow = false;
  static bool FullLandShadow = false;
  if (NoLandShadow) depth = 0;
  if (FullLandShadow) depth = 1000;
  #endif
  // as the sun gets deeper under horizon, the shadow gets deeper as well
  // IntersectUnder returns depth
  const float maxShadowUnder = terrainGrid*0.2f;
  const float minShadowUnder = 0;
  const float minShadow = 1.00f;
  const float maxShadow = 0.20f;
  return Interpolativ(depth,minShadowUnder,maxShadowUnder,minShadow,maxShadow);
}

float Scene::GetLandShadowSmooth(Vector3Par pos) const
{
  float depth = GetLandShadowDepthSmooth(
    pos.X()*_landscape->GetInvLandGrid(),
    pos.Z()*_landscape->GetInvLandGrid()
  );
  // depth tells us how high above the surface sun shines
  if (depth>0)
  {
    // shadow detected - we want to pretend shadows are not casted on very high objects
    float aboveSurface = pos.Y()-_landscape->SurfaceY(pos.X(),pos.Z());
    // reduce depth about how much are we above the surface
    depth -= aboveSurface;
  }
  return ConvertDepthToShadow(depth,_landscape->GetTerrainGrid());
}

#if !_VBS3_LAND_SHADOW
float Scene::GetLandShadowDepth(int x, int z) const
{
  if (_shadowMap->IsEmpty()) return 0;
  return _shadowMap->GetLand(x,z);
}

float Scene::GetLandShadow(int x, int z) const
{
  return ConvertDepthToShadow(_shadowMap->GetLand(x,z),_landscape->GetTerrainGrid());
}
#endif

float Scene::GetLandShadowDepthSmooth(float x, float z) const
{
#if _VBS3_LAND_SHADOW
  int x0 = toIntFloor(x);
  int z0 = toIntFloor(z);
  float xf = x-x0;
  float zf = z-z0;
  Rect rect;
  _shadowMapCache->GetLandShadowRect(x0,z0,rect);

  return Bilint(rect.xs, rect.zs, rect.xe, rect.ze, xf, zf);  
#else
  if (_shadowMap->IsEmpty()) return 0;
  int x0 = toIntFloor(x);
  int z0 = toIntFloor(z);
  float xf = x-x0;
  float zf = z-z0;
  float y00 = _shadowMap->GetLand(x0,z0);
  float y01 = _shadowMap->GetLand(x0,z0+1);
  float y10 = _shadowMap->GetLand(x0+1,z0);
  float y11 = _shadowMap->GetLand(x0+1,z0+1);
  return Bilint(y00,y01,y10,y11,xf,zf);  
#endif
}

#if _VBS3_LAND_SHADOW
void Scene::GetSegmentLandShadow(GridRectangle& rectSD, int lod, VertexCustomInfo* custom)
{
  _shadowMapCache->GetLandShadow(rectSD, lod, custom);
}
void Scene::GetLandShadowValuesShape(GridRectangle& rectSD, const AutoArray<Vector3>& pos, AutoArray<VertexCustomInfo>& custom)
{
  _shadowMapCache->GetShadowValuesShape(rectSD, pos, custom);
}
#endif

void Scene::FlushLandCache()
{
  _shadowCache.Clear();

#if _VBS3_LAND_SHADOW
  if (_shadowMapCache) _shadowMapCache->Clear();
#else
  if (_shadowMap) _shadowMap->Clear();
#endif
}



DEFINE_FAST_ALLOCATOR(SortObject)


SortObject::SortObject()
{
}

SortObject::~SortObject()
{
  SetObject(NULL);
  SetParentObject(NULL);
  SetRootObject(NULL);
}

void SortObject::SetObject(Object *obj)
{
  // lock the new object
  if (obj) obj->LockLink();
  if (_object)
  {
    // the grid in question may be destroyed immediately
    // do not keep Ref, as it could prevent destruction
    UnlockLinkContext temp;
    _object->UnlockLinkPrepare(temp);
    _object = NULL;
    Object::UnlockLinkFinish(temp);
  }
  _object = obj;
}

void SortObject::SetParentObject(Object *obj)
{
  // lock the new object
  if (obj) obj->LockLink();
  if (_parentObject)
  {
    // the grid in question may be destroyed immediately
    // do not keep Ref, as it could prevent destruction
    UnlockLinkContext temp;
    _parentObject->UnlockLinkPrepare(temp);
    _parentObject = NULL;
    Object::UnlockLinkFinish(temp);
  }
  _parentObject = obj;
}

void SortObject::SetRootObject(Object *obj)
{
  // lock the new object
  if (obj) obj->LockLink();
  if (_rootObject)
  {
    // the grid in question may be destroyed immediately
    // do not keep Ref, as it could prevent destruction
    UnlockLinkContext temp;
    _rootObject->UnlockLinkPrepare(temp);
    _rootObject = NULL;
    Object::UnlockLinkFinish(temp);
  }
  _rootObject = obj;
}

template <class CompareLods>
inline bool SortObject::CanBeInstancedTogether(const SortObject &so, const CompareLods &compareLods, const DrawParameters &dp) const
{
  // If any of the two objects is about to use craters, don't instance it
  if (GetObject()->CheckCraterDrawing(distance2, dp)) return false;
  if (so.GetObject()->CheckCraterDrawing(so.distance2, dp)) return false;

  // If in thermal mode and any of the two objects has got own heat source, disable instancing
  if (GEngine->GetThermalVision())
  {
    Vector3 dummy;
    if (GetObject()->GetOwnHeatSource(dummy)) return false;
    if (so.GetObject()->GetOwnHeatSource(dummy)) return false;
  }

  // Compare the LODShape and used level
  return shape==so.shape && !compareLods(*this,so);
}

void SortObject::UpdateBlendFactor()
{
  if (srcLOD<0 || dstLOD<0)
  {
  }
  else if (srcLOD != dstLOD)
  {
    DoAssert(blendFactor >= 0.0f);

    // determine current blending direction based on desired LOD
    if (!DirectionReversed())
    {
      if (GLODBlendingMode==LODBlendImmediate && !GetObject()->GetShape()->CanBlend()) blendFactor = 1;
      else blendFactor += GScene->GetLodTransitionDelta();
      if (blendFactor >= 1.0f)
      {
        srcLOD = dstLOD;
        blendFactor = -1.0f;
      }
    }
    else
    {
      if (GLODBlendingMode==LODBlendImmediate && !GetObject()->GetShape()->CanBlend()) blendFactor = 0;
      else blendFactor -= GScene->GetLodTransitionDelta();
      if (blendFactor <=0)
      {
        dstLOD = srcLOD;
        blendFactor = -1.0f;
      }
    }
  }
}

float LODShape::EstimateArea(float scale) const
{
  // check average radius based on min-max box
  Vector3Val min = Min();
  Vector3Val max = Max();
  float xDiameter = max.X()-min.X();
  float yDiameter = max.Y()-min.Y();
  float zDiameter = max.Z()-min.Z();
  float avgDiameter = (xDiameter+yDiameter+zDiameter)*(0.5f/3);
  return Square(avgDiameter*scale);
}


float LODShape::CoveredArea(const Camera &cam, float dist2, float oScale) const
{
  //float radius = shape->BoundingSphere()*oScale;
  //float fog = 1-GScene->Fog8(dist2)*(1.0f/255);
  const float fog = FogLODs ? 1-GScene->Fog8(dist2)*(1.0f/255) : 1.0f;

  // return area in pixels
  // note: InvLeft corresponds to half screen view space, while Width corresponds to full screen
  // therefore radius is converted to area
  // calculation could also be performed with diameter and half-width
  float areaK = cam.InvLeft()*cam.InvTop()*GScene->GetEngineArea();
  float areaEst = EstimateArea(oScale)*fog;

  const float maxArea = 2.0f;
  // if (Square(radius)/dist2>maxArea)
  if (areaEst>=dist2*maxArea)
  {
    return maxArea*areaK;
  }
  return areaEst*areaK/dist2;
}

/**
used for projective shadow visibility testing
//TODO: share code with ObjectForDrawing
*/
bool Scene::CheckShadowInvisibile(LODShape *shape, Vector3Par pos, float radius) const
{
  const float maxShadowSize = Glob.config.GetMaxShadowSize();
  if( _camera->IsClippedSecShadow(pos,radius+maxShadowSize))
  {
    // if the object with its radius is very far, shadow may be skipped
    return true;
  }
  else
  {
    ClipFlags clippedByShadowFrustum = _camera->IsClippedSecShadow(pos,radius);
    if (!clippedByShadowFrustum)
    {
      // if the object itself is in shadow non-clip area, shadow is present
      return false;
    }
    else
    {
      // check shadow direction against 6 shadow frustum direction
      // any time shadows are coming from outside, we can discard all objects
      // which are culled by that frustum face
      ClipFlags shadowInbound = _camera->IsInboundSecShadow(_mainLight->ShadowDirection());
      
      if (shadowInbound&clippedByShadowFrustum)
      {
        return true;
      }
      else
      {
      
        // if object is too far, we ignore its shadow
        // check a few shadow points
        // 1) object bottom projection
        // 2) object top projection
        
        // some invisible objects have visible shadows
        // simple test - shadow possible

        float maxShadowSize = Glob.config.GetMaxShadowSize();
        // assume shadow may be larger than the object itself
        // TODO: more correct estimation based on sun direction
        Vector3 objTop=pos+Vector3(0,radius,0);
        Vector3 shadowTopPos=objTop;
        if(!ShadowPos(objTop,shadowTopPos,_mainLight,maxShadowSize))
        {
          return true;
        }
        else
        {
          float shadowEnlargeFactor = 2.0;
          float shadowRadius = radius*shadowEnlargeFactor;
          Vector3 objBot = pos-Vector3(0,radius,0);
          Vector3 shadowBotPos=objBot;
          if(
            !ShadowPos(objBot,shadowBotPos,_mainLight,maxShadowSize)
            || _camera->IsClippedSecShadow(shadowBotPos,shadowTopPos,shadowRadius)
          )
          {
            return true;
          }
        }
      }
    }
  }
  return false;
}

#if 0 // _PROFILE
#pragma optimize("", off)
#endif

#if _ENABLE_CHEATS
#include <El/Statistics/statistics.hpp>
#endif

#if _ENABLE_CHEATS
static bool RandomizeSize = true;
static bool OptimizeRandCull = true;
static bool DitherGroups = true;
#else
const bool RandomizeSize = true;
const bool OptimizeRandCull = true;
const bool DitherGroups = true;
#endif

/// bigger limit means less objects are rendered
// const float MinObjRandomDraw = 0.9f*0.9f;
// const float MaxObjRandomDraw = 1.4f*1.4f;
const float MinObjRandomDraw = 0.95f*0.95f;
const float MaxObjRandomDraw = 1.05f*1.05f;

bool Scene::CanBeDrawing(float dist2, float estArea) const
{
  // check fogging as well
  const float fog = FogLODs ? 1-Fog8(dist2)*(1.0f/255) : 1.0f;
  float areaK = _camera->InvLeft()*_camera->InvTop()*_engineArea;
  const float minPixelArea = MinPixelSize2FromDist2(dist2);
  // check fogging as well
  //if (Square(radius)*areaK/dist2<minPixelArea)
  //return estArea*areaK>=dist2*minPixelArea*MinObjRandomDraw;
  if (RandomizeSize) return estArea*fog*areaK>=dist2*minPixelArea*MinObjRandomDraw;
  return estArea*fog*areaK>=dist2*minPixelArea;
}

bool Scene::CanDrawOcclusion(const Object* obj, Vector3Par pos) const
{
	float dist2 = pos.Distance2(_camera->Position());
#if _HELISIM_VIEW_DIST
  if(obj->GetType() ==1 && obj->GetEntityType()) 
  { //buiding 
    if (dist2>Square(Glob.config.staticZ)) return false;
  }
  else if(obj->GetType() == 1)
  { //trees
    if (dist2>Square(Glob.config.treesZ)) return false;
  } //vehicles
  else 
  {
    if (dist2>Square(Glob.config.vehiclesZ)) return false;
  }
#else
  if (dist2>Square(Glob.config.objectsZ))
		return false;
#endif

	float areaK = _camera->InvLeft()*_camera->InvTop()*_engineArea;
	float areaEst = obj->EstimateArea();
	float area;

	const float maxArea = 2.0f;
	// if (Square(radius)/dist2>maxArea)
	if (areaEst>=dist2*maxArea)
	{
		area = maxArea*areaK;
	}
	else
		area = areaEst*areaK/dist2;

	if(area > 7000)
		return true;

	return false;
}

bool Scene::IsShadowInvisible(float shadowsZ, Vector3Par pos, float dist2, float radius, int shapeSpecial, bool isProxy) const
{
  bool invisibleShadow = false;

  const float maxShadowSize = Glob.config.GetMaxShadowSize();
  if (dist2>Square(shadowsZ+maxShadowSize+radius))
  {
    invisibleShadow = true;
  }
  // check if shadow is enabled
  else if (shapeSpecial&NoShadow) // proxy_shadow
  {
    invisibleShadow = true;
  }
  // TODO: proxy shadows should inherit its parent shadow visibility decision
  else if( _camera->IsClippedShadowCustomBackPlane(pos, radius + maxShadowSize, NULL, shadowsZ))
  {
    // if the object with its radius is very far, shadow may be skipped
    invisibleShadow = true;
  }
  else
  {
    ClipFlags clippedByShadowFrustum = _camera->IsClippedShadowCustomBackPlane(pos, radius, NULL, shadowsZ);
    if (!clippedByShadowFrustum)
    {
      // if the object itself is in shadow non-clip area, shadow is present
    }
    else
    {
      // check shadow direction against 6 shadow frustum direction
      // any time shadows are coming from outside, we can discard all objects
      // which are culled by that frustum face
      ClipFlags shadowInbound = _camera->IsInboundShadowCustomBackPlane(_mainLight->ShadowDirection(), shadowsZ);

      if (shadowInbound&clippedByShadowFrustum)
      {
        invisibleShadow = true;
      }
      else
      {
        float maxShadowSize = Glob.config.GetMaxShadowSize();
        // if object is too far, we ignore its shadow
        // check a few shadow points
        // 1) object bottom projection
        // 2) object top projection

        // some invisible objects have visible shadows
        // simple test - shadow possible

        // assume shadow may be larger than the object itself
        // TODO: more correct estimation based on sun direction
        Vector3 objTop = pos + Vector3(0, radius, 0);
        Vector3 objBot = pos-Vector3(0,radius,0);
        Vector3 shadowTopPos = objTop;
        Vector3 shadowBotPos=objBot;

        // even if the shadow top is invisible, the shadow bottom may still be visible, shadows may go away from us
        float shadowEnlargeFactor = 2.0;
        float shadowRadius = radius*shadowEnlargeFactor;
        if(
          !ShadowPos(objTop,shadowTopPos,_mainLight,maxShadowSize) &&
          !ShadowPos(objBot,shadowBotPos,_mainLight,maxShadowSize)
          || _camera->IsClippedShadowCustomBackPlane(shadowBotPos,shadowTopPos,shadowRadius,shadowsZ)
        )
        {
          invisibleShadow = true;
        }
      }
    }
  }

  return invisibleShadow;
}

/// metaprogramming to allow binary literals
template< unsigned long long N >
struct binary
{
  enum { value = (N % 8) + 2 * binary< N / 8 > :: value } ;
};

template<>
struct binary< 0 >
{
  enum { value = 0 } ;
};

#define BIN(bbbb) (binary<0##bbbb>::value) // prepend zero to make always octal

const float MinDistDataNeeded = 200;
static float MaxDensity = 0.3f; // max. polygons per pixel wanted


/// accumulate some properties during ObjectForDrawing so that we can make informed decisions when doing LOD management
struct ObjectForDrawingContext
{
  /// area covered by objects we ara managing (can decice on their lods)
  float managedArea;
  /// area covered by worst LOD objects
  float worstLODArea;
  /// complexity used by worst LODs
  int worstLODComplexity;
  /// complexity used by explicit (forced) LODs
  /** note: we are not counting area for this */
  int explicitComplexity;

  ObjectForDrawingContext()
  :managedArea(0),worstLODArea(0),worstLODComplexity(0),explicitComplexity(0)
  {
  }
};

void RenderRoadways(Object* obj, const Shape* shape)
{
  static const Vector3 offset = VUp * 0.1f;
  if (shape)
  {
    Vector3 verts[32];

    Vector3 cameraPos = GScene->GetCamera()->Position();
    for (Offset o = shape->BeginFaces(); o < shape->EndFaces(); shape->NextFace(o))
    {
      const Poly &face = shape->Face(o);

      int numVerts = face.N();
      DoAssert(numVerts >= 3);

      for (int i = 0; i < numVerts; i++)
      {
        verts[numVerts - i - 1] = shape->Pos(face.GetVertex(i)) + offset;
      }

      enf::Shape::CreatePolygon(verts, face.N(), 0x7f007f7f, enf::SS_TRANSP|enf::SS_NOZWRITE|enf::SS_ONCE)->SetMatrix(obj->FutureVisualState().Transform());
    }
  }
}

struct PathVertex
{
  Vector3 p;
  unsigned int color;
};

void RenderPaths(Object* obj, const Shape* roadway)
{
  const IPaths* building = obj->GetIPaths();

  if (building)
  {
    PathVertex* pv = ALLOCA(PathVertex, building->NPoints());

    for(int n = 0; n < building->NPoints(); n++)
    {
      pv[n].p = building->GetPosition(n);
      pv[n].color = 0x3f00ff00;
      PathAction *pa = building->GetPathAction(n);
      if(pa)
      {
        pv[n].color = 0x3fff0000;
      }
    }
    for(int n = 0; n < building->NExits(); n++)
    {
      pv[building->GetExit(n)].color = 0x3f0000ff;
    }

    for(int n = 0; n < building->NPoints(); n++)
    {
      enf::Shape::CreatePyramid(pv[n].p + Vector3(-0.05,0.1,-0.05), pv[n].p + Vector3(0.05,0.2,0.05), pv[n].color, enf::SS_TRANSP|enf::SS_NOZWRITE|enf::SS_ONCE);

      const FindArray<int> &connections = building->GetConnections(n);
      for(int c = 0; c < connections.Size(); c++)
      {
        int to = connections[c];
        enf::Shape::CreateLine(pv[n].p + Vector3(0,0.1,0), pv[to].p + Vector3(0,0.1,0), 0xffafafaf, enf::SS_NOZWRITE|enf::SS_ONCE);
      }
    }
  }
}
/*!
\param parentCsmLayerReached used to allow/disable shadows, and to specify CSM levels for proxies

\patch 1.35 Date 12/10/2001 by Ondra
- Fixed: Disappearing shadows when sun is low.
Note: this function is a common implementation for ProxyForDrawing and ObjectForDrawing
Compiler should be clever enough to optimize out code based on constant isProxy argument
*/


#if !_ENABLE_REPORT
__forceinline
#endif
SortObject *Scene::ObjectForDrawing(
  Object *obj, int forceLOD, ClipFlags clip,
  bool isProxy, float proxyDist2, const PositionRender &position, int parentCsmLayerReached, const ObjectGroup *oGrp
)
{
  //if (obj->ID() != 17928) return;

  Assert((clip&ClipUser0)==0);
  const ObjectVisualState& vs = obj->RenderVisualState();

  LODShape *shape=obj->GetShape();

  #if 0 // _ENABLE_REPORT
    // debugging opportunity
    static const char interestedIn[64]="radnice";
    if (shape && strstr(shape->GetName(),interestedIn))
    {
      __asm nop;
    }
  #endif

	if( !shape ) return NULL; // nothing to draw
  
  Vector3 pos = position.PositionWorld(*GetCamera());

  ADD_COUNTER(obFD,1)

	// some objects may be trivially clipped or distance culled, get rid of them ... 

  float featureSize = obj->GetFeatureSize();

  // rejecting by distance / pixel size is quite common
  // it is best to do it as soon as possible
  float dist2;
  // calculate distance
  if (isProxy)
  {
    dist2 = proxyDist2;
  }
  else
  {
    dist2 = pos.Distance2(_camera->Position());
  }

  // if it is an RTM animated object which is relatively close, we need to check the animation center
  // call Static before AnimationMovesPosition to prevent calling virtual function if possible
  if (dist2<Square(200) && !obj->Static() && obj->AnimationMovesPosition())
  {
    dist2 = obj->AimingPosition(vs).Distance2(_camera->Position());
  }

#if _HELISIM_VIEW_DIST
  float objectsZ = Glob.config.vehiclesZ;
  if(obj->GetType() ==1 && obj->GetEntityType()) objectsZ = Glob.config.staticZ;
  else if(obj->GetType() ==1) objectsZ = Glob.config.treesZ;
  if (dist2>Square(objectsZ))
#else
    if (dist2>Square(Glob.config.objectsZ))
#endif
  {
    // never draw anything behind horizon, even if it is a feature
    if (dist2>Square(Glob.config.horizontZ))
    {
      return NULL;
    }
    // never cull "features" based on distance only
    if (featureSize<=0)
    {
      return NULL;
    }
  }

  float oScale = vs.Scale();
  float radius = shape->BoundingSphere()*oScale;

  float areaK = _camera->InvLeft()*_camera->InvTop()*_engineArea;
  const float fog = FogLODs ? 1-Fog8(dist2)*(1.0f/255) : 1.0f;
  const float minPixelArea = obj->MinPixelArea(MinPixelSize2FromDist2(dist2));
  float estArea = obj->EstimateArea()*fog;

  // if object is smaller than certain area, do not draw it
  // it would cause aliasing artifacts, and it is a waste of power anyway

  float area = estArea*areaK;

  unsigned char disappearDither = ~0;
  if (featureSize<=0)
  {
    // apply screen size (1) and the object distance (2) limit at the same time
    // this way we can be sure there will no hard line on objects disappearing
    // (1) estArea*areaK/dist2<minPixelArea (2) Square(Glob.config.objectsZ)<dist2
    // converted into a similar form:  
    // (1) estArea*areaK<minPixelArea*dist2 (2) minPixelArea*Square(Glob.config.objectsZ)<minPixelArea*dist2
    // as we are never getting objects with dist2>Square(Glob.config.objectsZ) here, we therefore need to skew the
    // distance slightly to use the whole range provided by MinObjRandomDraw..MaxObjRandomDraw
  

#if _HELISIM_VIEW_DIST
    float objectsZ = Glob.config.vehiclesZ;
    if(obj->GetType() ==1 && obj->GetEntityType()) objectsZ = Glob.config.staticZ;
    else if(obj->GetType() ==1) objectsZ = Glob.config.treesZ;
    if (dist2>Square(objectsZ))
    // TODO: debug and verify the limit is implemented well
    saturateMin(area, minPixelArea*Square(objectsZ)*(1.0f/MaxObjRandomDraw));
#else
    // TODO: debug and verify the limit is implemented well
    saturateMin(area, minPixelArea*Square(Glob.config.objectsZ)*(1.0f/MaxObjRandomDraw));
#endif

    // perform randomization to make sure static objects with the same size like forest trees
    // do not appear all at the same time
    if (RandomizeSize)
    {
      if (OptimizeRandCull && obj->GetObjectId().IsObject())
      {
        // sometimes we know for sure the condition will be satisfied
        float minArea = dist2*minPixelArea*MinObjRandomDraw;
        float maxArea = dist2*minPixelArea*MaxObjRandomDraw;
        if (area<minArea)
        {
            return NULL;
        }
        // sometimes we know for sure the condition will be never satisfied
        if (area<maxArea)
        {
          // apply random group cull here using _disappearDither
          if (DitherGroups)
          {
            //float random = GRandGen.RandomValue(obj->ID())*(MaxObjRandomDraw-MinObjRandomDraw)+MinObjRandomDraw;
            // create random "dither"
  
            // we know: area>minArea and area<maxArea
            
            // compute the ratio - TODO: avoid division?
            float ratio = (area-minArea)/(maxArea-minArea); // from <0,1>
            int bits = toIntFloor(ratio*6.999f); // <0,6>
            // 1 to 7 bits (no 0 and no 8 - they are outside the range)
            static const unsigned char ditherPatterns[]=
            {
              BIN(00000001),BIN(00010001),BIN(00010101),BIN(01010101),BIN(01010111),BIN(01110111),BIN(01111111)
            };
            Assert(bits>=0 && bits<lenof(ditherPatterns));
            disappearDither = ditherPatterns[bits];
          }
          else
          {
            float random = GRandGen.RandomValue(obj->ID())*(MaxObjRandomDraw-MinObjRandomDraw)+MinObjRandomDraw;
            if (estArea*areaK<dist2*minPixelArea*random)
            {
              return NULL;
            }
          }
        }
      }
      else if (obj->GetObjectId().IsObject())
      {
        float random = GRandGen.RandomValue(obj->ID())*(MaxObjRandomDraw-MinObjRandomDraw)+MinObjRandomDraw;
        if (area<dist2*minPixelArea*random)
        {
					return NULL;
        }
      }
      else
      {
        if (area<dist2*minPixelArea)
        {
          return NULL;
        }
      }
    }
    else
    {
      if (area<dist2*minPixelArea)
      {
        return NULL;
      }
    }
  }

  
#if _HELISIM_RENDER_TO_TEXTURE
  SortObject *sObj=obj->GetInList(_drawObjectsId);
#else
  SortObject *sObj=obj->GetInList();
#endif
  Assert(!sObj || ((_prepareId-sObj->_prepareId)&0xff)<=2); // prepare Id must never be too old
  // detect object is used for the 2nd time in a frame
  // this may happen for "features", as they are rendered from a separate list as well
  if(sObj && sObj->_prepareId==_prepareId)
  {
    Assert(sObj->position.position==position.position);
    // if submitted twice, it must be a feature
    Assert(featureSize>0);
    if (featureSize>0)
    {
      return NULL;
    }
  }

  // object is invisible  
  //bool invisible = false;
  // object primary shadow is invisible
  bool invisibleShadow = parentCsmLayerReached==0;
  unsigned char csmLayerReached = 0; // by default indicate no CSM level at all (no shadow)

  bool invisible = false;

  // we always want to render our own shadow
  if (obj!=GWorld->CameraOn() )
  {
    float clipRadius;
    if (oGrp)
    {
      const ObjectListFull *list = oGrp->_list;
      pos = list->GetBSphereCenter();
      clipRadius = list->GetBSphereRadius();

			//specialized version with world-space box (no need for transform)
      if ((clip&ClipAll)!=0)
      {
        ClipFlags mayBeClipped;
        invisible = _camera->IsClippedByAnyPlane(pos,clipRadius,list->GetMinMax(),mayBeClipped);
        clip &= mayBeClipped;
      }
    }
    else
    {
			Vector3 minMax[2];
      clipRadius = obj->ClippingInfo(vs,minMax,Object::ClipVisual);

			if ((clip&ClipAll)!=0)
      {
        ClipFlags mayBeClipped;
        invisible = _camera->IsClippedByAnyPlane(pos,clipRadius,position.TransformWorld(*_camera),minMax,mayBeClipped);
        clip &= mayBeClipped;
      }
    }
        
    if (isProxy) // proxies inherit the information from their parents
    {
      csmLayerReached = parentCsmLayerReached;
    }
    else if (!invisibleShadow)
    {
      // Check visibility of the shadow
      invisibleShadow = IsShadowInvisible(Glob.config.GetShadowZ(), pos, dist2, radius, obj->GetObjSpecial(shape->Special()), isProxy);
          
      // if shadow is not present at all, no need to test CSM layers
      if (!invisibleShadow)
      {
        csmLayerReached = 1; // always cast the shadow into the most distant layer
        // check if CSM shadows apply to this object
        // this is optimization - if they do not, avoid computing them
        if (GEngine->IsSBEnabled())
        {
          // Include visibility for the rest of the layers
          float shadowsZ = Glob.config.GetShadowZ() * (1.0f/ShadowBufferCascadeRatio);
          for (int i = 1; i < ShadowBufferCascadeLayers; i++)
          {
            bool noShadowHere = IsShadowInvisible(shadowsZ, pos, dist2, radius, shape->Special(), isProxy);
            csmLayerReached |= (!noShadowHere) << i;
            // if shadow does not reach here, it cannot reach any sub-layer
            if (noShadowHere)
              break;
            shadowsZ *= (1.0f/ShadowBufferCascadeRatio);
          }
        }
      }
    }

    // Set visibility for the roughest layer

    

    if (invisible && invisibleShadow)
    {
      return NULL;
    }
  }
  else
  {
    // 1st person is always casted into all CSM levels
    csmLayerReached = (1<<ShadowBufferCascadeLayers)-1;
  }
  
  ADD_COUNTER(obFDY,1)

#ifdef GEOM_OCCLUDERS
	if(isProxy)
	{
		Vector3 minmax[2];
		obj->ClippingInfo(obj->RenderVisualState(),minmax,Object::ClipVisual);
		enf::AAB wbbox;
		wbbox.InitFromOBB(position, enf::AAB(minmax[0], minmax[1]));
		
		if(!CheckAgainstOccluders(wbbox))
			return NULL;
	}
#endif

  DoAssert(!isProxy || !sObj);
  if( !sObj )
  {
    int index=_drawObjects.Add(new SortObject);
    sObj=_drawObjects[index];

    sObj->SetObject(obj);
    sObj->shape = shape;
    sObj->radius = radius;
    // if the object was already rendered, but with instancing, we need to reuse the settings
    SortObject *instanced = isProxy ? NULL : FindSortObject(obj
#if _HELISIM_RENDER_TO_TEXTURE
      , _drawObjectsId
#endif
      );
    if (instanced)
    {
      sObj->srcLOD = instanced->srcLOD;
      sObj->dstLOD = instanced->dstLOD;
      sObj->blendFactor = instanced->blendFactor;
    }
    else
    {
      sObj->srcLOD = -1;
      sObj->dstLOD = -1;
      sObj->blendFactor = -1.0f;
    }
    sObj->forceDrawLOD = -1; 
    sObj->_nItems = 0;
    sObj->_rootIndex = -1;
    sObj->_prepareId = _prepareId-1; // set to anything not equal to _prepareId
    if (!isProxy)
    {
#if _HELISIM_RENDER_TO_TEXTURE
      obj->SetInList(sObj, _drawObjectsId);
#else
      obj->SetInList(sObj); // this object is in list
#endif
    }
  }
  
  if (oGrp)
  {
    sObj->_list = oGrp->_list;
    sObj->_listBeg = oGrp->_beg;
    sObj->_listEnd = oGrp->_end;
    sObj->_disappearDither = disappearDither;
  }
  else
  {
    sObj->_list = NULL;
    // this is done so that we can always use _listEnd-_listBeg to get the lenght of the sequence
    sObj->_listBeg = 0;
    sObj->_listEnd = 1;
  }
  
  
  if( invisible )
  {
    sObj->forceDrawLOD = LOD_INVISIBLE;
    sObj->_visibleRatio = 1.0f;
  }
  else
  {
#if _ENABLE_CHEATS || _VBS3_CHEAT_DIAG
    if (DiagDrawModeState!=DDMNormal)
    {
      int geom = -1;
      switch (DiagDrawModeState)
      {
        case DDMGeometry:
          geom=shape->FindGeometryLevel();
          break;
        case DDMViewGeometry:
          if (obj->OcclusionView())
          {
            geom=shape->FindViewGeometryLevel();
          }
          break;
        case DDMFireGeometry:
          if (obj->OcclusionFire())
          {
            geom=shape->FindFireGeometryLevel();
          }
          break;
        case DDMRoadway:
          //render roadways
          if(dist2 < Square(200))
            RenderRoadways(obj, shape->RoadwayLevel());
          break;
        case DDMPaths:
          if(dist2 < Square(200))
            RenderPaths(obj, shape->PathsLevel());
          break;
      }
      if( geom>=0 ) forceLOD=geom;
    }
#endif

    sObj->forceDrawLOD = forceLOD;
    sObj->_visibleRatio = 1.0f;
  }

  // for wrecks, force special LOD if viewed from outside
  // prevent normal shadow from being casted - proxy shadow casting will be done independently
  // test FindWreckLevel (fast, mostly fails) before calling virtual function (slow)
  if (sObj->forceDrawLOD < 0 && shape->FindWreckLevel()>=0 && obj->IsWreck())
  {
    invisibleShadow = true;
    sObj->forceDrawLOD = shape->FindWreckLevel();
  }

  Assert(_finite(dist2));
  
  // if object is near we use nearest distance instead of center distance
  // this avoid degenerate LODs when being near
  // if radius is 0.25 of distance, it is considered significant

  if (isProxy)
  {
    // by default no shadows for proxies - proxy shadow always use hierarchy traversal
    // TODO:MC: linear rendering for proxy shadows - when doing this, check proxy_shadow above
    sObj->shadowLOD = -1;
    sObj->shadowVolLOD = -1;
    //sObj->shadowLOD = LOD_INVISIBLE;
    //sObj->shadowVolLOD = LOD_INVISIBLE;
    sObj->limitSpriteSize = false; // Initialize it somehow
    sObj->csmLayerReached = csmLayerReached; // Initialize it somehow
    sObj->position = position;
    sObj->passNum = -1; // Set in ProxyForDrawing()
    sObj->drawLOD = forceLOD; // no auto-detection for proxies
  }
  else
  {
    DoAssert(sObj);
    if (sObj->GetParentObject()) sObj->SetParentObject(NULL);
    if (sObj->GetRootObject()) sObj->SetRootObject(NULL);
    
    sObj->drawLOD = -1; // override with auto-detection
    
    // when object is near, we need to consider its nearest point
    // radius relative to distance
    // we need good nearest point calculation at 4x radius
    const float minRadiusCoef = 4.0f;
    // we are happy with the object center at 10x radius
    const float maxRadiusCoef = 10.0f;
    if (dist2 < Square(radius * maxRadiusCoef))
    {
      // radius is never negative
      // condition above makes sure it is not zero, as dist2 is never negative
      Assert(radius>0);
      // calculate nearest point estimation
      //float dist = dist2>0 ? dist2*InvSqrt(dist2) : 0;
      
      //float distDivRadius = dist2>0 ? dist2*InvSqrt(dist2)/radius : 0;
      float distDivRadius = dist2>0 ? dist2*InvSqrt(dist2*Square(radius)) : 0;

      // nearest point is one radius nearer than the center      
      float distNearDivRadius = floatMax(distDivRadius - 1,0);
      float distNear = distNearDivRadius*radius;

			//float distSave= distNear;
      // If camera is in interpolate area then modify the distNear
      if (dist2 > Square(radius * minRadiusCoef))
      {
        //float factor = (dist - radius * minRadiusCoef) / (radius * maxRadiusCoef - radius * minRadiusCoef);
        float factor = (distDivRadius - minRadiusCoef) * (1.0f / (maxRadiusCoef - minRadiusCoef));
        float dist = distDivRadius*radius;
        distNear = factor * dist + (1.0f - factor) * distNear;

				//LogF("%f -> %f", distSave, distNear);
      }
      dist2 = Square(distNear);
    }
    
    saturateMax(dist2, Square(_camera->Near()));

    // shadow management
    if (invisibleShadow)
    {
      sObj->shadowLOD = LOD_INVISIBLE;
      sObj->shadowVolLOD = LOD_INVISIBLE;
      sObj->limitSpriteSize = false; // Initialize it somehow
      sObj->csmLayerReached = 0;
    }
    else
    {
      const float minPixelAreaShadow = MinPixelSize2FromDist2(dist2)*4;
      if (estArea*areaK<dist2*minPixelAreaShadow)
      {
        sObj->shadowLOD = LOD_INVISIBLE;
        sObj->shadowVolLOD = LOD_INVISIBLE;
        sObj->limitSpriteSize = false; // Initialize it somehow
        sObj->csmLayerReached = 0; // Initialize it somehow
      }
      else
      {
        sObj->limitSpriteSize = false; // Initialize it somehow

        #if 0 //_ENABLE_REPORT
        Matrix4 invCam = _camera->GetInvTransform();
        Vector3 camPos = invCam.FastTransform(obj->Position());
        LogF(
          "Visible shadow for %s, dist %.1f, rel pos %.1f,%.1f,%.1f",
          cc_cast(sObj->shape->GetName()),sqrt(dist2),
          camPos.X(),camPos.Y(),camPos.Z()
        );
        #endif
        sObj->shadowLOD = -1; // override with auto detection
        sObj->shadowVolLOD = -1;
        sObj->csmLayerReached = csmLayerReached; // We are interested in the shadow visibility per layer now
      }
    }
    sObj->position = position;
    // if object shadow is too small, do not draw it
    // it would cause aliasing artifacts  
    sObj->passNum = -1; // non-init

    //sObj->zCoord = (toCamera*pos).Z()-offset;
    // optimized: we need only z-coordinate, obtaining it does not require complete transformation
  }
  
  Assert(_finite(dist2));
  sObj->distance2 = dist2;

  if(!sObj->position.camSpace)
  {
    sObj->zCoord = GetCamera()->Direction()*(pos-GetCamera()->Position());
  }
  else
  {
    sObj->zCoord = pos.Z();
  }

  // disappering head hotfix (news:i7v7sa$329$1@new-server.localdomain)
  sObj->_someDataNeeded = isProxy && featureSize>0 || dist2<Square(MinDistDataNeeded);
  
  if (
    (sObj->forceDrawLOD!=LOD_INVISIBLE || sObj->shadowLOD!=LOD_INVISIBLE)
    && !obj->IgnoreComplexity()
  )
  {
    float factor = obj->DensityRatio();
    sObj->coveredArea = shape->CoveredArea(*GetCamera(),dist2,oScale)*factor;
    // consider fogging in covered area as well
    // foggy objects do not need that much detail
    DoAssert(sObj->coveredArea>=0);

    // often we know even the simplest LOD is fine enough, no need to perform any LOD selection then
    if (sObj->forceDrawLOD<0 && shape->FindSimplestLevel()>=0)
    {
      float oComplexity = sObj->coveredArea*MaxDensity;
      float simplestComplexity = shape->Complexity(shape->FindSimplestLevel());
      if (shape->FindSimplestLevel()>0)
      {
        // the decision level is in the middle between the two last LODs
        simplestComplexity = (simplestComplexity+shape->Complexity(shape->FindSimplestLevel()-1))*0.5f;
      }
      if (simplestComplexity>oComplexity)
      {
        sObj->forceDrawLOD = shape->FindSimplestLevel();
      }
    }
  }
  else
  {
    sObj->coveredArea = 0;
  }
  
  Assert((clip&ClipUser0)==0);

  sObj->orClip=clip;
  Assert(sObj->_prepareId!=_prepareId); // no object may be used twice in the same frame
  sObj->_prepareId = _prepareId;
  sObj->_usedInFrame = FreeOnDemandFrameID();
  
  return sObj;
}


void Scene::ObjectsForDrawing(const ObjectGroup &oGrp, ClipFlags clip, bool shadowPossible)
{
  // when casting shadows, we want to handle individual objects
  DoAssert(!shadowPossible);
  enum Mode {Individual,Group,GroupUnlessClipped,GroupFirst,NModes};
    #if _VBS3
    const int defaultMode = Invidiual;
    #else
    const int defaultMode = GroupUnlessClipped;
    #endif
  #if _ENABLE_CHEATS
    static int mode = defaultMode;
    if (GInput.GetCheat3ToDo(DIK_1))
    {
      mode++;
      if (mode>=NModes) mode = 0;
      DIAG_MESSAGE(1000,Format("Group Instancing mode %d",mode));
    }
  #else
    const int mode = defaultMode;
    #endif
  if (mode==Individual || !Glob.config.enableInstancing || clip && mode==GroupUnlessClipped)
  {
  }
  else if (mode!=GroupFirst)
  {
    Object *objBeg = oGrp._list->Get(oGrp._beg);
    if (!objBeg->NeedsPrepareProxiesForDrawing())
    {
      ObjectForDrawing(objBeg,-1,clip,false,0,PositionRender(objBeg->GetFrameBase(),false),false,&oGrp);
      return;
    }
    // proxies needed - fall through
  }
  else
  {
    // prototype: submit one object from the list only
    ObjectForDrawing(oGrp._list->Get(oGrp._beg),-1,clip,false);
    return;
  }
  for (int i=oGrp._beg; i<oGrp._end; i++)
  {
    // instancing "emulation" - submit each object individually
    ObjectForDrawing(oGrp._list->Get(i),-1,clip,false);
  }
}


void Scene::ObjectForDrawing(Object *obj, int forceLOD, ClipFlags clip, bool shadowPossible)
{
  // TODO: verify what CSM should be used here
  int csmLayerReached = shadowPossible ? (1<<ShadowBufferCascadeLayers)-1 : 0;

#if 1
  ObjectForDrawing(obj,forceLOD,clip,false,0,obj->GetPositionRender(),csmLayerReached);
#else
  ObjectForDrawing(obj,forceLOD,clip,false,0,PositionRender(*obj,false),csmLayerReached);
#endif
}

void Scene::ObjectForDrawing(Object *obj, int forceLOD, ClipFlags clip, const PositionRender &position, bool shadowPossible)
{
  // TODO: verify what CSM should be used here
  int csmLayerReached = shadowPossible ? (1<<ShadowBufferCascadeLayers)-1 : 0;
  ObjectForDrawing(obj,forceLOD,clip,false,0,position,csmLayerReached);
}

void Scene::ProxyForDrawing(Object *obj, Object *rootObj, int forceLOD, int rootLOD, ClipFlags clip, float dist2, const PositionRender &pos, Object *parentObj)
{
  // for proxies inherit shadow enable/disable/CSM influence from the parent (root) object
#if _HELISIM_RENDER_TO_TEXTURE
  int shadowCSM = rootObj && rootObj->GetInList(_drawObjectsId) ? rootObj->GetInList(_drawObjectsId)->csmLayerReached : 0;
#else
  int shadowCSM = rootObj && rootObj->GetInList() ? rootObj->GetInList()->csmLayerReached : 0;
#endif
  
  SortObject *sObj = ObjectForDrawing(obj,forceLOD,clip,true,dist2,pos,shadowCSM,NULL);
  if (sObj)
  {
    sObj->_list = NULL;
    sObj->_listBeg = 0;
    sObj->_listEnd = 1;
    sObj->SetParentObject(parentObj);
    sObj->SetRootObject(rootObj);
    sObj->rootLOD = rootLOD;
    sObj->passNum = obj->PassNum(forceLOD);
    // if not alpha object, try to keep the same pass as the root object
    if (sObj->passNum != 2 && rootObj != obj && rootObj)
      sObj->passNum = rootObj->PassNum(rootLOD);
  }
}

void Scene::CloudletForDrawing( Object *obj )
{
  // some objects may be trivially clipped
  // get rid of them ...
  Vector3Val pos=obj->RenderVisualState().Position();
  float radius=obj->GetRadius();

  //float size=obj->Scale();
  // perform clip test?
  if( _camera->IsClipped(pos,radius) ) return;

  Vector3 cPos=ScaledInvTransform()*pos;

  // camera plane clip test
  // perform more distant clipping than normal
  // in case of real 3d object peform normal clipping
  float nearest=_camera->Near()*obj->CloudletClippingCoef();
  // Consider particle radius to get rid of particles that cannot be seen (they are not needed and also they otherwise steal precious X slots for full size particles)
  float particleRadius = obj->RenderVisualState().Scale() * 0.5f;
  if( (cPos.Z()-particleRadius)<nearest ) return;  
  if( cPos.Z()<nearest ) return;

  // if we want to have alpha objects sorted correctly, we need to consider
  // minimum distance

  float dist2=_camera->Position().Distance2Inline(pos);

  // float estimate area
  const float minCloudletArea = 4;
  float size2=Square(radius*_engineWidth*_camera->InvLeft());
    if( size2<dist2*minCloudletArea ) return;
  
#if _HELISIM_RENDER_TO_TEXTURE
    SortObject *sObj=obj->GetInList(_drawObjectsId);
#else
    SortObject *sObj=obj->GetInList();
#endif
  if( !sObj )
  {
    int index=_drawObjects.Add(new SortObject);
    sObj=_drawObjects[index];

    sObj->SetObject(obj);
    sObj->shape = obj->GetShape();
    sObj->radius=radius;
    sObj->srcLOD = -1;
    sObj->dstLOD = -1;
    sObj->blendFactor=-1.0f;
    sObj->_nItems = 0;
    sObj->_rootIndex = -1;
    sObj->_prepareId = _prepareId-1; // set to anything not equal to _prepareId
#if _HELISIM_RENDER_TO_TEXTURE
    obj->SetInList(sObj, _drawObjectsId); // this object is in list
#else
    obj->SetInList(sObj);
#endif
  }
  if (sObj->shape->CheckLevelLoaded(0,true))
  {
    sObj->drawLOD = -1;
    sObj->forceDrawLOD = 0;
    sObj->_visibleRatio = 1.0f;
  }
  else
  {
    sObj->drawLOD = LOD_INVISIBLE;
    sObj->forceDrawLOD = LOD_INVISIBLE;
    sObj->_visibleRatio = 1.0f;
  }
  sObj->_list = NULL;
  sObj->_listBeg = 0;
  sObj->_listEnd = 1;
  sObj->shadowLOD = LOD_INVISIBLE;
  sObj->shadowVolLOD = LOD_INVISIBLE;
  sObj->limitSpriteSize = false; // Initialize it somehow
  sObj->csmLayerReached = ~0; // Initialize it somehow
  sObj->distance2 = dist2;
  sObj->_someDataNeeded = dist2<Square(MinDistDataNeeded);

  sObj->zCoord = GetCamera()->Direction()*(obj->RenderVisualState().Position()-GetCamera()->Position());

  sObj->zCoord = 0;
  sObj->position = PositionRender(obj->GetRenderFrameBase(),false);
  sObj->passNum=2; // all cloudlets drawn in alpha pass
  Assert(sObj->_prepareId!=_prepareId); // no object may be used twice in the same frame
  sObj->_prepareId = _prepareId;
  sObj->_usedInFrame=FreeOnDemandFrameID();
  sObj->orClip = ClipAll;
  sObj->coveredArea = 0;
}

typedef Ref<SortObject> SortObjectItem;

/// check z-order for alpha objects
static inline int CmpZOrderObj( const SortObjectItem *p1, const SortObjectItem *p2 )
{
  // the first object is the nearest one
  const SortObject *o1=*p1;
  const SortObject *o2=*p2;
  return sign(o2->zCoord-o1->zCoord);
}

/// draw opaque first, alpha later, in both categories respect z-order
static inline int CmpAlphaOrderedObj( const SortObjectItem *p1, const SortObjectItem *p2 )
{
  const SortObject *o1=*p1;
  const SortObject *o2=*p2;
  int alphaLevel1 = o1->GetObject()->GetAlphaLevel(o1->drawLOD);
  int alphaLevel2 = o2->GetObject()->GetAlphaLevel(o2->drawLOD);
  int d = alphaLevel1-alphaLevel2; // more alpha last
  if (d) return d;
  // TODO: when alpha level is the same, draw proxies first, real object later. For this we need to know what is a proxy
  // the first object is the nearest one
  return sign(o2->zCoord-o1->zCoord);
}



static OIDInt CmpRoadObj( const SortObjectItem *p1, const SortObjectItem *p2 )
{
  const SortObject *o1=*p1;
  const SortObject *o2=*p2;

  int order1 = o1->GetObject()->PassOrder(o1->drawLOD);
  int order2 = o2->GetObject()->PassOrder(o2->drawLOD);
  int oDif = order1-order2;
  if (oDif) return oDif;

#if _VBS3_CRATERS_BIAS
  // Consider object biases, sort according to them (this helps to solve problem of z-fighting craters)
  int bias1 = o1->GetObject()->Bias();
  int bias2 = o2->GetObject()->Bias();
  int bDif = bias1 - bias2;
  if (bDif) return bDif;
#endif

  LODShape *s1=o1->shape;
  LODShape *s2=o2->shape;
  
  int sDif=CompareAddress(s2,s1);
  // no invisible LODs here
  if( sDif )
  {
    // sort by shape
    return sDif;
  }

  // ordering should not matter for the same shape
  #if 0
    // to draw front first to use early Z out
    // sort by LOD - fine LODs (small LOD numbers) first
    sDif = o1->drawLOD-o2->drawLOD;
    if( sDif ) return sDif;
    // first draw
    // same shape sort by distance, front first
    return sign(o1->distance2-o2->distance2);
  #else
    // draw back first to help alpha transparency
    // sort by LOD - fine LODs (small LOD numbers) last
    sDif = o2->drawLOD-o1->drawLOD;
    if( sDif ) return sDif;
    
    #if _VBS3_CRATERS_DEFORM_TERRAIN
    // Sort by object ID in the end - usually distance sorting is performed on this place, but in case of on surface models this is not that important.
    // We use the sorting by object ID here to avoid flickering of overlapping craters caused by changing of their drawing order, if sorted by distance.
    sDif = o2->GetObject()->GetObjectId().Encode() - o1->GetObject()->GetObjectId().Encode();
    return sDif;
    #else
    // first draw
    // same shape sort by distance, back first
    return sign(o2->distance2-o1->distance2);
    #endif
  #endif
}

static inline int CmpProjShadowObj( const SortObjectItem *p1, const SortObjectItem *p2 )
{
  const SortObject *o1=*p1;
  const SortObject *o2=*p2;

  LODShape *s1 = o1->shape;
  LODShape *s2 = o2->shape;
  int sDif=CompareAddress(s2,s1);
  // first sort by shape
  if( sDif ) return sDif;
  // second sort by LOD
  sDif = o2->shadowLOD-o1->shadowLOD;
  // nothing else has any sense with shadows
  return sDif;
}

/// ordering of shadow buffer objects
static inline int CmpSBShadowObj( const SortObjectItem *p1, const SortObjectItem *p2 )
{
  const SortObject *o1=*p1;
  const SortObject *o2=*p2;

  int sDif;

  // first sort by shape
  LODShape *s1 = o1->shape;
  LODShape *s2 = o2->shape;
  sDif=CompareAddress(s2,s1);
  if( sDif ) return sDif;

  // second sort by LOD
  sDif = o2->shadowLOD-o1->shadowLOD;
  if( sDif ) return sDif;

  return sDif;
}

static inline int CmpVolShadowObj( const SortObjectItem *p1, const SortObjectItem *p2 )
{
  const SortObject *o1=*p1;
  const SortObject *o2=*p2;

  LODShape *s1 = o1->shape;
  LODShape *s2 = o2->shape;
  int sDif=CompareAddress(s2,s1);
  // first sort by shape
  if( sDif ) return sDif;
  // second sort by LOD
  sDif = o2->shadowVolLOD-o1->shadowVolLOD;
  // nothing else has any sense with shadows
  return sDif;
}


/*!
\patch 1.20 Date 8/16/2001 by Ondra
- Fixed: Lens-flares are no longer colored in night vision.

\patch 5160 Date 5/23/2007 by Ondra
- Fixed: Flare disappearing is now smooth near screen edges.
*/


float Scene::DrawFlare( ColorVal color, Vector3Par lightPos, Vector3Par projLightPos, bool secondary, bool sun, float visibility, float flareVisibility)
{
  float oldA = color.A();
  Color lightColor = color * GEngine->GetAccomodateEye();

  // we can draw flares now
  // flare positions between posLight (0) and screen center (1)

	static const float flarePos[FlareLast+1-Flare0]={
		0.0f,
		-0.3f,-0.15f,
		+0.25f,+0.275f,+0.3f,+0.45f,+0.5f,
		+0.6f,+0.65f, +0.7f,+0.75f,+0.825f,+0.89f,+1.0f,+1.15f
	};

	// flare size when used as lens camera
	static const float flareSizeCam[]={
		0.5,
		0.6,0.3,
		0.3,0.7,0.35,0.2,0.4,
		0.5,0.2, 1.0,1.7,0.4,0.25,0.5,0.5
	};

	static const float flareBright[FlareLast+1-Flare0]={
		1,
		0.4f,0.6f,
		0.25f,0.3f,0.3f,0.6f, 0.4f,
		0.4f,0.5f,0.4f,0.4f,0.5f,0.5f,0.5f,0.3f
	};

  // flare size when used as eye simulation
  static const float flareSizeEye[]={
    0.5
  };

	const float *flareSize = secondary ? flareSizeCam : flareSizeEye;
  int nFlares = secondary ? lenof(flareSizeCam) : lenof(flareSizeEye);
  DoAssert(nFlares<=FlareLast+1-Flare0);

	// note - z is ignored when doing 2D draw
  // but is significant for decal draw
  float w=GLOB_ENGINE->WidthBB();
  float h=GLOB_ENGINE->HeightBB();

	// apply perspective
	Matrix4Val project=GetCamera()->Projection();
	
	static const float overallIntensity = 0.9f;
  float intensity = lightColor.A()*lightColor.Brightness()*overallIntensity;
 
  if (intensity > 0.001f || (secondary && flareVisibility > 0.0f))
  {
    intensity *= visibility;
    lightColor = lightColor*visibility;
    
    float size = 0.1f;
		float firstSize = 1.0f;

		// size depends on intensity (to certain extent)
    {
      static float minIntensity = 0.0f;
	    static float maxIntensity = 1.0f;
		  static float minIntensitySize = 0.5;
			static float maxIntensitySize = 1.0;

			//move glare a little bigger when pp effects is on low setting
			static float PPEffectSizeMul[3] = {1.3f, 1.3f, 1.0f};
			int pp = GEngine->GetPostprocessEffects();
			saturate(pp, 0, 2);

      firstSize *= PPEffectSizeMul[pp]*Interpolativ(intensity,minIntensity,maxIntensity,minIntensitySize,maxIntensitySize);
    }

    // Remember projection size of the screen
    float projSizeX = +project(0,0)*GetCamera()->InvLeft();
    float projSizeY = -project(1,1)*GetCamera()->InvTop();
      
    float sizeX = projSizeX * size;
    float sizeY = projSizeY * size;
      
    // Calculate flare intensity into texture (via post process effect) and set the texture
    // on texture stage 15. The texture will be read only by flares with material FlareMaterial)
    // Flare(s) will be drawn at the end of this scope.
    // Set also aperture texture on stage 14
		/*
			if (sun)
			{
			  static float flareSourceSizeCoef = 0.025f;
			  int flareSourceSizeX = toInt(projSizeX * flareSourceSizeCoef * firstSize);
			  int flareSourceSizeY = toInt(projSizeY * flareSourceSizeCoef * firstSize);
				  GEngine->CalculateFlareIntensity(toInt(projLightPos.X()), toInt(projLightPos.Y()), flareSourceSizeX, flareSourceSizeY);
			}
		*/

    Vector3 center(0.5f*w,0.5f*h,1);
    const int special=NoZBuf|IsAlpha|DstBlendOne|ClampU|ClampV|IsAlphaFog;

    const TexMaterial *mat = sun ? GlobPreloadMaterial(FlareMaterial) : GlobPreloadMaterial(NonTLMaterial);
      
		if (intensity>=0.001f)
    {
			Texture *texture = secondary ? Preloaded(Flare0) : Preloaded(FlareEye);

      if (texture)
      {

        MipInfo mip=GEngine->TextBank()->UseMipmap(texture,1,0);
        if( mip.IsOK() && mip._level==0)
        {
					lightColor.SetA(0.01);
					lightColor.NormalizeColor();

					lightColor.SetA(overallIntensity*oldA*flareBright[0]);
          PackedColor color=PackedColor(lightColor);
          float sizeCoef=texture->AWidth()*(1.0f/64);
          sizeCoef *= flareSize[0];
          GEngine->DrawDecal(-1,projLightPos,1,sizeX*sizeCoef*firstSize,sizeY*sizeCoef*firstSize,color,mip,mat,special);
        }
      }
    }

		// Draw secondary flares of the particular light source (f.i. the camera flares line)
	  if (secondary && flareVisibility > 0.01) 
		{
			float moveCoef = 1.0f;

			Color flareColor = color * GEngine->GetAccomodateEye();
			flareColor.SetA(1);

			flareSize = flareSizeCam;
			nFlares = lenof(flareSizeCam);
			moveCoef = 2.0f;
			DoAssert(nFlares<=FlareLast+1-Flare0);

			for( int s=1; s<nFlares; s++ )
			{
				float coef=flarePos[s];
				Texture *texture=Preloaded(PreloadedTexture(Flare0+s));
				if (texture)
				{
					MipInfo mip=GEngine->TextBank()->UseMipmap(texture,1,0);
					if( mip.IsOK() && mip._level==0)
					{
						float flareIntensity = flareVisibility*overallIntensity*flareBright[s];
						PackedColor color=PackedColor(flareColor*flareIntensity);
            
						Vector3 pos=projLightPos + (moveCoef*coef)*(center-projLightPos);
						float sizeCoef=texture->AWidth()*(1.0f/64);
            
						sizeCoef *= flareSize[s];
						GEngine->DrawDecal(-1,pos,1,sizeX*sizeCoef,sizeY*sizeCoef,color,mip,mat,special);
					}
				}
			}
		}


		 // Force the flare to be drawn (because we have flare light texture prepared for the particular one)
    GEngine->FlushQueues();
  }
  return intensity;
}


/**
@calculate light screen projection position
*/
Vector3 Scene::ProjectLightPos(Vector3Par lightPos)
{
	// calculate screen position of light
	Vector3Val pos=ScaledInvTransform()*lightPos;
	if (pos[2]<0.1f) 
		return Vector3(0,0,-1.0f);

	// apply perspective
	Matrix4Val project = GetCamera()->Projection();

	float invW=1/pos[2];
	return Vector3 
	(
		project(0,2)+project(0,0)*pos[0]*invW,
		project(1,2)+project(1,1)*pos[1]*invW,
		1.0f
	);
}



/**
@calculate light screen projection position
*/
float Scene::GetProjLightVisibility(Vector3Par lightPos, Vector3& projPos)
{
	projPos = ProjectLightPos(lightPos);
	if (projPos.Z() < 0.0f)
		return 0.0f;

	float w=GLOB_ENGINE->WidthBB();
	float h=GLOB_ENGINE->HeightBB();

	float projX = fabs(projPos.X()/w*2-1); // -1..1 -> 0..1
	float projY = fabs(projPos.Y()/h*2-1);

	static float startBorder = 0.85f;
	static float endBorder = 1.1f;

	Vector3 p (
		InterpolativC(projX,startBorder,endBorder,1,0),
		InterpolativC(projY,startBorder,endBorder,1,0),
		1);

	return p.X()*p.Y();
}



/**
@return value describing total intensity of the flares - can be used for eye accommodation
*/
float Scene::DrawFlares(float timeDelta)
{
  if (GEngine->GetThermalVision()) return 0.0f;
  if( !GetLandscape() ) return 0;
  float vis=GetLandscape()->SkyThrough();
  float night=MainLight()->NightEffect();
  
  PROFILE_SCOPE_GRF_EX(drwFl,*,0);
  float totalIntensity = 0;
  
	Vector3Val cPos = GetCamera()->Position();
	
	CameraType camType=GWorld->GetCameraType();
	bool aiming = GWorld->GetCameraTypeWanted() == CamGunner;
	
	//sun flare
	bool testVis = false;
  if( vis>0.05 && night<0.95 )
  {
		// check HasFlares of camera source
    Object *camObj = GWorld->CameraOn();
		bool lensFlares = camObj ? camObj->HasFlares(camType) : camType != CamInternal;

    Vector3 lightDir=MainLight()->SunDirection();
    if (lightDir.Y()<+0.02f)
    {
      Color lightColor=MainLight()->GetDiffuse()*(1-night)*vis;
			Vector3 eyeLightPos = cPos - lightDir;

			Vector3 projLightPos;
			float sunVisibility = GetProjLightVisibility(eyeLightPos, projLightPos);
			float flareVisibility = 0.0f;

			//is on or near screen
			if (sunVisibility > 0.01f || lensFlares)
			{
				if (_sunQuery)
				{
					int sunPixelsRendered = intMax(_sunQuery->GetVisiblePixels(), 0);
					float sunPixelsEstimation = GetLandscape()->TestSunVisibility(*this);

					sunVisibility = floatMinMax(sunPixelsRendered/sunPixelsEstimation, 0.0f, 1.0f);
					LogF("pixels visible %d, estimate %f, ratio %f, frame latency %d", sunPixelsRendered, sunPixelsEstimation, sunVisibility, _sunQuery->GetLastLatency());
				}
				else
				{
					float intersectionTest = 0.0f;
					float t = _landscape->IntersectWithGroundOrSea(NULL,cPos,-lightDir);

					//no intersection with ground
					if (t >= Glob.config.horizontZ)
					{
						float tCockpit = GWorld->IntersectWithCockpit(cPos,-lightDir,0,Glob.config.horizontZ*1.1);

						//no intersection with cockpit
						if (tCockpit >= Glob.config.horizontZ)
						{
							Vector3 sunPos = cPos - lightDir*Glob.config.horizontZ;

							// check line against view geometries
							CollisionBuffer col;
							Object *camOn = GWorld->GetCameraEffect() ? NULL : (camType == CamInternal || aiming) ? GWorld->CameraOn() : NULL;
							_landscape->ObjectCollisionLine(GetCamera()->GetAge(),col,Landscape::FilterIgnoreOne(camOn),cPos,sunPos,0,ObjIntersectView, ObjIntersectFire, CF_FIRST_CONTACT);
							if(!col.Size())
							{
								intersectionTest = 1.0f;

								//looking in the same direction as the sun
								float dot = GetCamera()->Direction().DotProduct(-lightDir);
								flareVisibility = enf::LineEquation(dot, 0.15f, 0.05f);								
							}
						}
					}

					sunVisibility *= intersectionTest;
					//LogF("Average visibility %f", sunVisibility);
				}
			}

			if (!_sunQuery)
			{
				sunVisibility   = _smoothLensSunTime.UpdateDeltaTime(sunVisibility, timeDelta);
				flareVisibility = _smoothLensFlaresTime.UpdateDeltaTime(flareVisibility, timeDelta);
				testVis = true;
			}

			if (sunVisibility > 0.01f || (lensFlares && flareVisibility > 0.0f))
			{
				// check clouds
				float cloudOcclusion = GetLandscape()->CheckCloudOcclusion(cPos,-lightDir);
				float cloudDiffuseOccluded = 1-GetLandscape()->CloudThrough();

				// check against occlusion buffer
				sunVisibility *= (1-cloudOcclusion*cloudDiffuseOccluded);

				if (lensFlares)
				{
					saturateMin(sunVisibility, 2.0f);
				}
				else
				{
					float haloA = MainLight()->SunHaloObjectColor().A();
					saturateMin(sunVisibility, haloA*2.0f);
					saturateMin(sunVisibility, 2.0f);
				}

				if (sunVisibility > 0.01f || (lensFlares && flareVisibility > 0.0f))
				{
					Color sunColor = lightColor;
					sunColor.SetA(sunVisibility);

					// sun is considered very bright
					static float intensity = 30.0f;
					totalIntensity += intensity *DrawFlare(sunColor, eyeLightPos, projLightPos, lensFlares, true, sunVisibility, flareVisibility);
				}
			}
    }
  }

	if (!_sunQuery && !testVis)
	{
		_smoothLensSunTime.UpdateDeltaTime(0.0f, timeDelta);
		_smoothLensFlaresTime.UpdateDeltaTime(0.0f, timeDelta);
	}

  if( night>=0.1)
  {
		float nightVis = floatMin(1.0f, (night - 0.1f)*10.0f);

    // draw flares from active lights
    //Vector3Val camPos = GetCamera()->Position();
    Vector3Val camDir = GetCamera()->Direction();
    for( int i=0; i<_aRealLights.Size(); i++ )
    {
      Light *light=_aRealLights[i];
      Vector3 dir=cPos-light->Position();

			//opposite side of camera
			if( dir*camDir>0 ) 
				continue; 

      float intensity = nightVis*light->FlareIntensity(cPos,camDir);
			if (intensity<0.001f) 
				continue;

      //TODO: check intersection with ground
      //check if light position is visible from camera position
      Vector3 dirNorm = dir.Normalized();
      float dirSize = dir*dirNorm;
      float t = _landscape->IntersectWithGroundOrSea(NULL,cPos,-dirNorm,0,dirSize*1.1f);
      float tCockpit = GWorld->IntersectWithCockpit(cPos,-dirNorm,0,dirSize*1.1f);
      float visLand = t>1 && tCockpit>1;

      if (visLand>0)
      {
        Color lightColor=light->GetObjectColor();
				
        CollisionBuffer col;
				Object *camOn = GWorld->GetCameraEffect() ? NULL : (camType == CamInternal || aiming) ? GWorld->CameraOn() : NULL;
        _landscape->ObjectCollisionLine(GetCamera()->GetAge(),col,Landscape::FilterIgnoreOne(camOn),cPos,light->Position(),0,ObjIntersectView, ObjIntersectFire, CF_FIRST_CONTACT);

				if(!col.Size())      
        {
					//test, compare light to overall light in scene and increase/decrease its intensity
					float mainBrightness  = floatMax(MainLight()->GetDiffuse().Brightness()*0.5f + MainLight()->GetAmbient().Brightness(), 0.01f);
					float lightBrightness = lightColor.Brightness();

					float enhance = intensity;
					if (lightBrightness > mainBrightness)
					{
						static float divEnh = 0.1f;
						static float maxEnh = 4.0f;

						enhance = intensity*floatMin(maxEnh, lightBrightness/mainBrightness);
						
						float colEnh = 1.0f + (enhance - 1.0f)*divEnh;
						
						lightColor.SetR(lightColor.R()*colEnh);
						lightColor.SetG(lightColor.G()*colEnh);
						lightColor.SetB(lightColor.B()*colEnh);
					}
					lightColor.SetA(enhance);

					Vector3 projLightPos = ProjectLightPos(light->Position());

					// point lights are considered unit
          totalIntensity += 100*DrawFlare(lightColor, light->Position(), projLightPos, false, false, intensity, 0.0f);
        }
      }
    }
  }
  GEngine->FlushQueues();
  return totalIntensity;
}

//! Values that determines border between pass3 and UI pass in depth buffer
const float DepthBorderPass4Min = 0.0f;
const float DepthBorderPass4Max = 0.003f;

//! Values that determines border between usual pass and pass3 in depth buffer
const float DepthBorderPass3Min = 0.00301f;
const float DepthBorderPass3Max = 0.01f;



void Scene::ObjectsDrawn(DrawContext &ctx
#if _HELISIM_RENDER_TO_TEXTURE
                         , int drawListId
#endif
                         )
{
  // verify the border rules follow some common sense (create separated regions)
  Assert(DepthBorderPass4Min<DepthBorderPass4Max);
  Assert(DepthBorderPass4Max<DepthBorderPass3Min);
  Assert(DepthBorderPass3Min<DepthBorderPass3Max);
  Assert(DepthBorderPass3Max<DepthBorderCommonMin);
  
  DrawPass3(ctx);
  DrawPass4(ctx);

  // will flush worker thread - needed for text rendering
  DrawDiagTexts();

  #if _ENABLE_CHEATS
    if (GInput.GetCheat3ToDo(DIK_2))
    {
      RandomizeSize = !RandomizeSize;
      GlobalShowMessage(500, "RandomizeSize %s", RandomizeSize ? "On" : "Off");
    }
//     if (GInput.GetCheat3ToDo(DIK_3))
//     {
//       OptimizeRandCull = !OptimizeRandCull;
//       GlobalShowMessage(500, "OptimizeRandCull %s", OptimizeRandCull ? "On" : "Off");
//     }
    if (GInput.GetCheat3ToDo(DIK_3))
    {
      DitherGroups = !DitherGroups;
      GlobalShowMessage(500, "DitherGroups %s", DitherGroups ? "On" : "Off");
    }
    if (GInput.GetCheat3ToDo(DIK_8))
    {
      FogLODs = !FogLODs;
      GlobalShowMessage(500, "FogLODs %s", FogLODs ? "On" : "Off");
    }
  #endif
  
}

void Scene::LightsCleanUp()
{
  _aRealLights.Resize(0);
  _aAggLights.Resize(0);
  // clear all refs from the light grid
  for (int i = 0; i < _gridLights.Size(); i++)
  {
    _gridLights[i]->Resize(0);
  }
}


void Scene::ObjectsCleanUp(DrawContext &ctx, bool cleanUpProxies)
{
  // clear working list
  ctx._drawPass1ON._original.Resize(0);
  ctx._drawPass1ON._blending.Resize(0);
  ctx._drawPass1OF._original.Resize(0);
  ctx._drawPass1OF._blending.Resize(0);
  ctx._drawPass1AN.Resize(0);
  ctx._drawPass1AF.Resize(0);
  ctx._drawPass2.Resize(0);
  ctx._drawPass3.Resize(0);
  ctx._drawPass4.Resize(0);
  ctx._drawOnSurfaces.Resize(0);
  ctx._drawShadowVol.Resize(0);
  ctx._drawShadowProj.Resize(0);
  // keep drawObjects for next frame
	#ifdef TEST_RAIN_MAP
		ctx._drawRainMap.Resize(0);
	#endif

  // clear list of proxies to draw
  if (cleanUpProxies)
    _proxyObjects.Resize(0);
}

#if _ENABLE_CHEATS || _VBS3_CHEAT_DIAG

// advances diagnostics - via scripting
#include <El/evaluator/expressImpl.hpp>

#define NOTHING GameValue()

DEFINE_ENUM(DiagDrawMode,DDM,DIAG_DRAW_MODE_ENUM)

DEFINE_ENUM(DiagEnable,DE,DIAG_ENABLE_ENUM)

DiagDrawMode DiagDrawModeState = DDMNormal;
int DiagMode;
int DiagMode2;

static GameValue SetDiagDrawMode(const GameState *state, GameValuePar oper)
{
#if _VBS3  
  if(!Glob.vbsAdminMode) return NOTHING;
#endif
  const char *modeStr = (RString)oper;
  DiagDrawMode mode = GetEnumValue<DiagDrawMode>(modeStr);
  if ((int)mode==-1) return NOTHING;
  DiagDrawModeState = mode;
  return NOTHING;
}

static GameValue SetDiagEnable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
#if _VBS3  
  if(!Glob.vbsAdminMode) return NOTHING;
#endif
  const char *modeStr = (RString)oper1;
  bool onOff = oper2;
  int modeMask = ~0;
  int modeMask2 = ~0;
  if (strcmpi(modeStr,"all"))
  {
    DiagEnable mode = GetEnumValue<DiagEnable>(modeStr);
    if ((int)mode==-1) return NOTHING;
    if (mode < 32) { modeMask = 1<<mode; modeMask2 = 0; }
    else { modeMask2 = 1<<(mode-32); modeMask = 0; }
  }

  if (onOff) { DiagMode |= modeMask; DiagMode2 |= modeMask2; }
  else { DiagMode &= ~modeMask; DiagMode2 &= ~modeMask2; }
  return NOTHING;
}

static GameValue SetDiagToggle(const GameState *state, GameValuePar oper1)
{
#if _VBS3  
  if(!Glob.vbsAdminMode) return NOTHING;
#endif
  const char *modeStr = (RString)oper1;
  DiagEnable mode = GetEnumValue<DiagEnable>(modeStr);
  if ((int)mode==-1) return NOTHING;
  int modeMask = 0;
  int modeMask2 = 0;
  if (mode < 32) modeMask = 1<<mode;
  else modeMask2 = 1<<(mode-32);

  DiagMode ^= modeMask;
  DiagMode2 ^= modeMask2;
  return NOTHING;
}

#include <El/Modules/modules.hpp>

/*
static const GameNular ObjNular[]={};
*/
static const GameFunction ObjUnary[]=
{
  GameFunction(GameNothing,"diag_drawmode",SetDiagDrawMode,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_toggle",SetDiagToggle,GameString TODO_FUNCTION_DOCUMENTATION),
};
static const GameOperator ObjBinary[]=
{
  GameOperator(GameNothing,"diag_enable",function,SetDiagEnable,GameString,GameBool TODO_OPERATOR_DOCUMENTATION),
};


INIT_MODULE(GameStateObj, 3)
{
  GGameState.NewOperators(ObjBinary,sizeof(ObjBinary)/sizeof(*ObjBinary));
  GGameState.NewFunctions(ObjUnary,sizeof(ObjUnary)/sizeof(*ObjUnary));
  //GGameState.NewFunctions(ObjNular,sizeof(ObjNular)/sizeof(*ObjNular));
};

#endif

/*
\patch 5134 Date 2/26/2007 by Flyman
- New: Shadows by moon in the night are enabled now
*/
float Scene::ShadowFactor() const
{
  // Calculate factor of the shadow (to disappear with evening and showing again in the night)
  float daytimeShadowFactor;
  {
    // Remember the sunOrMoon coefficient
    float somCoef = MainLight()->SunOrMoon();

    // Get coefficient out of SunOrMoon
    daytimeShadowFactor = fabs((somCoef - 0.5f) * 2.0f);

    // This code is here to keep the old functionality when there were day shadows only
    if (somCoef > 0.5f)
    {
      saturateMin(daytimeShadowFactor, (1.0f - MainLight()->NightEffect()));
    }
  }
 
//   // we do not care about sky - this is already included in the diffuse lighting level
//   float skyCoef = GetLandscape()->SkyThrough()+0.1f;
//   // We can have 100% shadows now when we have hemispherical lighting implemented - normal maps will be visible even in shadows now
//   static float shadowFactorCoef = 1.0f;
//   return floatMin(shadowFactorCoef*daytimeShadowFactor,skyCoef);
  return daytimeShadowFactor;
}

bool Scene::CastShadows() const
{
  static float minShadowFactorToCastShadows = 0.047058823529f; // Former 12/255
  return ShadowFactor() >= minShadowFactorToCastShadows;
}

static __forceinline bool RequestAndLoadGeometry(Shape *shape, ShapeRef *shapeRef)
{
  bool ret = shape->RequestGeometry();
  if (ret)
  {
    // if the geometry is ready, make sure it is loaded as well
    ShapeGeometryLock<> lock(shape,shapeRef);
  }
  return ret;
}

extern bool StopLoadingModels;

bool LODShape::CheckLevelLoadedAsShadow(int level, bool request) const
{
  PROFILE_SCOPE(lsCLS);
  TimeManagerScope time;
  // note: , used im the condition, because we need to measure RequestLoadingAndLoad
  if (
    _lods[level].IsLoaded() ||
    #if _ENABLE_CHEATS
      !StopLoadingModels &&
    #endif
    request && GTimeManager.TimeLeft(TCLoadShape)>0 && (time.Start(TCLoadShape),_lods[level].RequestLoadingAndLoad())
  )
  {
    // we know Shape is loaded now
    // locking should never wait for any file, but this does not mean it is free
    Shape *shape = unconst_cast(this)->InitLevelLocked(level);
    // check if the vertex buffer is ready
    // if not, check if there is a time to load it and a data ready
    // if not, request it
    return shape->IsGeometryLoaded() || request && RequestAndLoadGeometry(shape,_lods[level]);
  }
  return false;
}

bool LODShape::CheckLevelLoaded(int level, bool request) const
{
  PROFILE_SCOPE_DETAIL(lsCLL);
  TimeManagerScope time;
    
#if _VBS3
  //! Added - Request Earl: Prevents CTD if no p3d model is avaliable during loading
  if(_nLods == 0)
    return false;
#endif

  // note: , used in the condition, because we need to measure RequestLoadingAndLoad
  if (
    _lods[level].IsLoaded() ||
    #if _ENABLE_CHEATS
      !StopLoadingModels &&
    #endif
    request && GTimeManager.TimeLeft(TCLoadShape)>0 && (time.Start(TCLoadShape),_lods[level].RequestLoadingAndLoad())
  )
  {
    bool ret = true;
    // we know Shape is loaded now
    // we do not need to lock the file - we only want to use it if it is present
    // locking and unlocking may cause cache maintenance in case shape is not used
    // locking should never wait for any file, but this does not mean it is free
    Shape *shape = unconst_cast(this)->InitLevelLocked(level);
    Assert(shape);
    // check if the vertex buffer is ready
    // if not, check if there is a time to load it and a data ready
    // if not, request it
    if (
      shape->IsVertexBufferGeometryLoaded() ||
      (
        // if vbuffer is not used, we need geometry
        !LevelUsesVBuffer(shape,level) ||
        // if there is no time to create vbuffer, we report not ready
        GTimeManager.TimeLeft(TCCreateVertexBuffer)>0 ||
        // but if SW animation is used, creating VB would be very quick
        GetAnimationType()==AnimTypeSoftware
      )
      && ( shape->IsGeometryLoaded() || request && RequestAndLoadGeometry(shape,_lods[level]))
    )
    {
      if (!shape->MakeTexturesReady())
      {
//            LogF("%s: some texture not ready",cc_cast(GetName()));
            ret = false;
          }
      
    }
    else
    {
//      LogF(
//        "%s,%d: Geometry not loaded, %d, %d, %d",
//        cc_cast(GetName()),level,
//        shape->IsGeometryLoaded(),
//        shape->IsVertexBufferGeometryLoaded(),
//        LevelUsesVBuffer(shape,level)
//      );
      ret = false;
    }
    
    return ret;
  }
  else
  {
    //LogF("%s,%d: Shape not loaded",cc_cast(GetName()),level);
  }
  return false;
}

int LODShape::CheckLevelLoadedDegree(int level) const
{
  const Shape *shape = GetLevelLocked(level);
  if (!shape) return 0;
  if (!shape->GetVertexBuffer()) return 1;
  if (shape->IsVertexBufferGeometryLoaded()) return 9;
  if (!LevelUsesVBuffer(level) || GetAnimationType()==AnimTypeSoftware)
  {
    if (shape->IsGeometryLoaded()) return 7;
    else return 6;
  }
  return 8;
}

inline bool LODShape::CheckLevelLoadedQuick(int level) const
{
  const Shape *shape = GetLevelLocked(level);
  if (!shape) return false;
  return (
    shape->IsVertexBufferGeometryLoaded() ||
    (
      // if vbuffer is not used, we need geometry
      !LevelUsesVBuffer(level) ||
      // but if SW animation is used, creating VB would be very quick
      GetAnimationType()==AnimTypeSoftware
    )
    && shape->IsGeometryLoaded()
  );
}

inline int LODShape::UseLevel(int level, bool needSomeData) const
{
  int limitLod = 0;
  saturateMax(level, limitLod);
  return UseLevelInRange(level,needSomeData,limitLod,_nGraphical);
}

inline int LODShape::UseLevel(int level, float dist2, bool needSomeData, int previousLevel) const
{
  if (_nGraphical<=0) return -1;
  int limitLod = 0;
  saturateMax(level, limitLod);
  return UseLevelInRange(level,needSomeData,limitLod,_nGraphical,dist2,previousLevel);
}

#if _PROFILE || _DEBUG // _ENABLE_REPORT
  #define TRACK_SHAPE 0
#endif

#if TRACK_SHAPE
static const char InterestedInShape[256]="ca\\structures\\mil\\mil_controltower.p3d";
#endif

int LODShape::UseLevelQuick(int level) const
{
  { // first check the requested one
    const Shape *shape = GetLevelLocked(level);
    if (shape && shape->GetVertexBuffer()) return level;
  }
  
  // test all worse levels
  for (int i=level+1; i<_nGraphical; i++)
  {
    const Shape *shape = GetLevelLocked(i);
    if (shape && shape->GetVertexBuffer()) return i;
  }
  // test all finer levels
  for (int i=level-1; i>=0; i--)
  {
    const Shape *shape = GetLevelLocked(i);
    if (shape && shape->GetVertexBuffer()) return i;
  }
  return -1;
    

}

int LODShape::UseLevelInRange(int level, bool needSomeData, int beg, int end) const
{
  
  PROFILE_SCOPE(lodUL);
  // if LOD is not in given range, return it
  // and hope that who selected it knew what he is doing
  if (level<beg || level>=end)
  {
    return level;
  }
  // check if the level is loaded, request it as needed
  if(CheckLevelLoaded(level,true))
  {
    return level;
  }
  // if there are other levels, try searching them
  if (end-beg>1)
  {
    // if the level is not ready, return what we already have
    // avoid calling CheckLevelLoaded, as this can be quite slow
    // find nearest level that is already loaded
    int complexity = Complexity(level);

    // avoid using too good LODs, as this could degrade performance significantly
    // rather used worst level if near level is not available
    int bestI = -1;
    int bestDist = INT_MAX;
    if (level>beg)
    { //check previous level
      int iLevel = level - 1;
      int dist = abs(Complexity(iLevel)-complexity);
      // do CheckLevelLoadedQuick as late as possible - it is relatively slow
      if (bestDist>dist && CheckLevelLoadedQuick(iLevel))
      {
        bestDist = dist, bestI = iLevel;
      }
    }

    // check following levels
    for (int iLevel=level+1; iLevel<end; iLevel++)
    {
      int dist = abs(Complexity(iLevel)-complexity);
      if (bestDist>dist && CheckLevelLoadedQuick(iLevel))
      {
        bestDist = dist, bestI = iLevel;
      }
      // once we have found a solution, no other can be better, given LODs are ordered by complexity
      if (bestI>=0)
      {
        // if we have found something, return it
        ShapeUsed shapeBestI = Level(bestI);
        if (!shapeBestI->GetVertexBuffer())
        {
          // the geometry will be needed - make sure it is made recent
          ShapeGeometryLock<> lock(this,bestI);
        }
        return bestI;
      }
    }
  }

  // if nothing is loaded yet, return the worst level possible
  return needSomeData ? end-1 : -1;
}

/*!
\patch 5115 Date 1/8/2007 by Flyman
- Fixed: Reduced object LOD switching.
*/
int LODShape::UseLevelInRange(int level, bool needSomeData, int beg, int end, float dist2, int previousLevel) const
{
  PROFILE_SCOPE(lodUL);
  // if LOD is not in given range, return it
  // and hope that who selected it knew what he is doing
  if (level<beg || level>=end)
  {
    return level;
  }
  #if TRACK_SHAPE
  bool interested = strcmp(GetName(),InterestedInShape)==0;
  #else
  const bool interested = false;
  #endif
  // if we have already used the level before, it is almost certain its data are still ready
  if (level==previousLevel)
  {
    if (interested)
    {
      LogF("reuse LOD %s:%d",cc_cast(GetName()),level);
    }
    return level;
  }
  // we are willing to accept somewhat lower texture quality
  dist2 = (dist2+1)*2;
  // if fine quality is not available,
  // find a shape which is ready and has the best texture quality
  float bestTexQuality = 0;
  int bestNT = -1;
  // check if the level is loaded, request it as needed
  if(CheckLevelLoaded(level,true))
  {
    ShapeUsed shape = Level(level);
    float quality;
    if (shape->PreloadTextures(dist2, NULL, false, &quality))
    {
      if (interested)
    {
        LogF("LOD %s:%d - **quality %g",cc_cast(GetName()),level,quality);
      }
      return level;
    }
    bestNT = level;
    bestTexQuality = quality;
    if (interested)
    {
      LogF("LOD %s:%d - *quality %g",cc_cast(GetName()),level,quality);
    }
  }
  // if there are other levels, try searching them
  if (end-beg>1)
  {
    // If we don't have the desired level prepared and we used some level in the previous frame,
    // then use the previous level instead of trying to find the most appropriate level.
    // This will prevent the wild blinking of LODs.
    if (previousLevel>=beg && previousLevel<end)
    {
      // check if the vertex buffer is still loaded (it should be because of LRU)
      if (CheckLevelLoaded(previousLevel,false))
      {
        // check if some texture was perhaps evicted
        // this should not happen, as textures from objects which are rendered
        // are marked recent in LRU
        /*
        ShapeUsed shape = Level(previousLevel);
        float quality;
        const float minQuality = 0.0f;
        shape->PreloadTextures(dist2, NULL, false, &quality);
        if (quality>minQuality)
        */
        {
          if (interested)
          {
            LogF("prev LOD %s:%d",cc_cast(GetName()),previousLevel);
          }
          return previousLevel;
        }
        //LogF("Warning: Desired level %d, previous level %d, texture quality of previous level is not sufficient - %f", level, previousLevel, quality);
      }
    }

    // avoid calling CheckLevelLoaded, as this can be quite slow
    // if the level is not ready, return what we already have
    // find nearest level that is already loaded
    int complexity = Complexity(level);

    int invEndBeg = 1.0f/(end-beg);
    // avoid using too good LODs, as this could degrade performance significantly
    // rather used worst level if near level is not available
    int bestI = -1;
    int bestDist = INT_MAX;
    if (level>beg)
    { //check previous level
      int iLevel = level - 1;
      int dist = abs(Complexity(iLevel)-complexity);
      // do CheckLevelLoadedQuick as late as possible - it is relatively slow
      if (bestDist>dist && CheckLevelLoadedQuick(iLevel))
      {
        ShapeUsed shape = Level(iLevel);
        float quality;
        shape->PreloadTextures(dist2, NULL, false, &quality);
        if (quality>=1)
        {
          bestDist = dist, bestI = iLevel;
          if (interested)
          {
            LogF("better LOD %s:%d - +quality %g",cc_cast(GetName()),iLevel,quality);
          }
        }
        else
        {
          if (interested)
          {
            LogF("better LOD %s:%d - -quality %g",cc_cast(GetName()),iLevel,quality);
          }
          // slightly prefer closer levels
          quality *= 1-invEndBeg*0.4f;
          if (quality>bestTexQuality)
          {
            bestTexQuality = quality, bestNT = iLevel;
          }
        }
      }
    }

    // check following levels
    
    float invComplex = complexity>0 ? 1.0f/complexity : 1;
    for (int iLevel=level+1; iLevel<end; iLevel++)
    {
      int iComplex = Complexity(iLevel);
      int dist = abs(iComplex-complexity);
      if (bestDist>dist && CheckLevelLoadedQuick(iLevel))
      {
        ShapeUsed shape = Level(iLevel);
        float quality;
        shape->PreloadTextures(dist2, NULL, false, &quality);
        if (quality>=1)
        {
          if (interested)
          {
            LogF("worse LOD %s:%d - +quality %g",cc_cast(GetName()),iLevel,quality);
          }
          bestDist = dist, bestI = iLevel;
        }
        else
        {
          if (interested)
          {
            LogF("worse LOD %s:%d - -quality %g",cc_cast(GetName()),iLevel,quality);
          }
          // slightly prefer closer levels
          // assume worse level quality corresponds to it having less polygons
          quality *= iComplex*invComplex*0.8f+0.2f;
          if (quality>bestTexQuality)
          {
            bestTexQuality = quality, bestNT = iLevel;
          }
        }
      }
      // once we have found a solution, no other can be better, given LODs are ordered by complexity
      if (bestI>=0)
      {
        if (interested)
        {
          LogF("Used other LOD %s:%d",cc_cast(GetName()),bestI);
        }
        // if we have found something, return it
        ShapeUsed shapeBestI = Level(bestI);
        if (!shapeBestI->GetVertexBuffer())
        {
          // the geometry will be needed - make sure it is made recent
          ShapeGeometryLock<> lock(this,bestI);
        }
        return bestI;
      }
    }
  }

  // if textures are not ready, prefer a shape which has buffer ready
  if (bestNT>=0)
  {
    if (interested)
    {
      LogF("Used NT LOD %s:%d - -quality %g",cc_cast(GetName()),bestNT,bestTexQuality);
    }
    // if we have found something, return it
    ShapeUsed shapeBestI = Level(bestNT);
    if (!shapeBestI->GetVertexBuffer())
    {
      // the geometry will be needed - make sure it is made recent
      ShapeGeometryLock<> lock(this,bestNT);
    }
    return bestNT;
  }
  if (interested)
  {
    LogF("Used end LOD %s:%d",cc_cast(GetName()),end-1);
  }
  // if nothing is loaded yet, return the worst level possible
  return needSomeData ? end-1 : -1;
}

int LODShape::UseLevelAsSBShadow(int level, bool needSomeData) const
{
  PROFILE_SCOPE(lodUL);
  // based on _sbSource determine what it the expected shadow lod range
  int beg = 0, end = _nGraphical;
  if (_sbSource!=SBS_Visual)
  {
    beg = _shadowBuffer;
    end = _shadowBuffer+_shadowBufferCount;
  }
  if (level<beg || level>=end)
  {
    if (level >= 0)
    {
      RptF("Error: %s : %f SB shadow lod expected in this level", cc_cast(GetName()), Resolution(level));
    }
    return level;
  }
  return UseLevelInRange(level,needSomeData,beg,end);
}

int LODShape::UseLevelAsShadow(int level, bool needSomeData) const
{
  PROFILE_SCOPE(lodUS);
  // special levels cannot be substituted for anything else
  if (level<0 || level>=_nGraphical) return level;
  // check if the level is loaded
  if(CheckLevelLoadedAsShadow(level,true))
  {
    return level;
  }
  // if the level is not ready, return what we already have
  // find nearest level that is already loaded
  int complexity = Complexity(level);
  int bestDist = INT_MAX;
  int bestI = -1;
  for (int i=0; i<_nGraphical; i++) if (i!=level)
  {
    if (!CheckLevelLoadedAsShadow(i,false)) continue;
    int dist = abs(Complexity(i)-complexity);
    if (bestDist>dist) bestDist = dist, bestI = i;
  }
  if (bestI>=0)
  {
    // if we have found something, return it
    ShapeUsedGeometryLock<> shapeBestI(this,bestI);
    return bestI;
  }
  // if nothing is loaded yet, return the worst level possible
  return needSomeData ? _nGraphical-1 : -1;
}

int LODShape::UseShadowLevel(int level) const
{
  // handle special case - volume shadow
  if (IsShadowVolume(level))
  {
    // if the level is not loaded yet, continue without drawing its shadow
    return UseLevelInRange(level,false,_shadowVolume,_shadowVolume+_shadowVolumeCount);
  }
  // if shadow level is not loaded, ShadowCache::Shadow should handle it
  return level;
}


int Scene::PreloadLevelFromDistance2(LODShape *shape, float dist2, float scale)
{
  float area = shape->CoveredArea(*GetCamera(),dist2,scale);
  float oComplexity = area*_drawDensity;
  int level = shape->FindLevelWithComplexity(area,oComplexity,oComplexity*1.5f+200);
  if (level==LOD_INVISIBLE) return level;
  if (level<0)
  {
    // when the object is empty, do not try to load anything
    if (shape->FindSimplestLevel()<0) return LOD_INVISIBLE;
    return level;
  }

  saturateMax(level, 0);
  // we do not want any other lod than the one we need
  if(!shape->CheckLevelLoaded(level,true))
    return -1;

  //LogF("Preloaded %s:%d, dist %.1f",cc_cast(shape->GetName()),level,sqrt(dist2));
  return level;
}

/**
@param level which LOD are we rendering
@param screenScale approximate size of 1 m2 in the screen space square pixels after projection
*/
float LODShape::ShaderComplexity(int level, float screenScale) const
{
  const RefShapeRef &lod = _lods[level];
  int nVertices = lod.GetShapeRef()->NVertices();
  float area = lod.GetShapeRef()->FaceArea();
  // a sphere with area 4*pi*r^2 is projected to a circle with area pi*r^2, therefore the projected area can be estimated as 4x lower
  // than the original area
  float areaInPixels = area*0.25f*screenScale;
  // TODO: check instructions for VS/PS
  static int vsInstructions = 100;
  static int psInstructions = 25;
  return nVertices*vsInstructions+areaInPixels*psInstructions;
}

int LODShape::FindLevelWithDensity(
  float dist2, float scale, float density
) const
{
  // scale is zero, there is no need to render anything at all
  if (scale==0) return LOD_INVISIBLE;
  float area = CoveredArea(*GScene->GetCamera(),dist2,scale);
  float oComplexity = area*density;
  int level = FindLevelWithComplexity(area,oComplexity,oComplexity*1.5f+200);
  return UseLevel(level,dist2,dist2<Square(MinDistDataNeeded),-1);
}


int LODShape::FindShadowLevelWithDensity(float dist2, float scale, float density) const
{
  // scale is zero, there is no need to render anything at all
  if (scale==0) return LOD_INVISIBLE;
  float area = CoveredArea(*GScene->GetCamera(),dist2,scale);
  float oComplexity = area*density;
  return FindShadowLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
}

int LODShape::FindShadowVolumeLevelWithDensity(float dist2, float scale, float density) const
{
  // scale is zero, there is no need to render anything at all
  if (scale==0) return LOD_INVISIBLE;
  float area = CoveredArea(*GScene->GetCamera(),dist2,scale);
  float oComplexity = area*density;
  return FindShadowVolumeLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
}

int LODShape::FindShadowLevelWithComplexity(float complexity, float maxDif) const
{
	bool useRainMap = false;
	#ifdef TEST_RAIN_MAP
		useRainMap = GLandscape->GetRainDensity() > 0.001f && GEngine->IsRainMapEnabled();
	#endif

  // Consider the sbSource in case of SB shadows
  if (GEngine->IsSBEnabled() || useRainMap)
  {
    switch (_sbSource)
    {
    case SBS_Visual:
      break; // Do nothing here - behave the same way as in non-SB shadows
    case SBS_ShadowVolume:
      return _shadowBuffer; // Return the first SB lod (it was obtained from shadow volume)
    case SBS_Explicit:
      return FindLevelWithComplexity(0,complexity, maxDif, _shadowBuffer, _shadowBuffer + _shadowBufferCount);
    case SBS_None:
      return -1; // No SB shadow
    default:
      Fail("Error: Unknown shadow buffer source");
      return -1;
    }
  }

  // used only for projected shadows
  // SW animation is not compatible with volume shadows
  // when SW animation is detected, use CPU projected shadows
  if (_minShadow>=_nGraphical) return -1; // no shadow
  int i = FindLevelWithComplexity(0,complexity,maxDif);

  if (i<0) return i;
  if (i<_minShadow)
  {
    return _minShadow;
  }
  return i;
}

int LODShape::FindShadowVolumeLevelWithComplexity(float complexity, float maxDif) const
{
  // used only for volume shadows
  if (_shadowVolume<0 || _shadowVolumeCount<=0) return -1; // no shadow
  return FindLevelWithComplexity(0,complexity,maxDif, _shadowVolume, _shadowVolume+_shadowVolumeCount);
}

void LODShape::FindResolutionInterval(int &start, int &end, float minRes, float maxRes) const
{
  start = INT_MAX;
  end = 0;
  for (int i=0; i<_nGraphical; i++)
  {
    float r = _resolutions[i];
    if (r>=minRes && r<=maxRes)
    {
      if (start>i) start = i;
      if (end<i+1) end = i+1;
    }
  }
  if (start>end)
  {
    Assert(end==0);
    // No suitable LODs found - return both 0    
    start = end;
  }
  
}

//static bool PreferLast = false;

int LODShape::FindLevelWithComplexity(float coveredArea, float complexity, float maxDif, int start, int end) const
{
  Assert(complexity>=0);
  if (end<=start) return -1;
  int bestI = -1;
  // avoid passing too large numbers into toIntCeil
  int complexityStart = Complexity(start);
  if (complexity>=complexityStart) return start;
  // we know level complexity is integer
  // we can therefore round the input
  int bestDif = maxDif>complexityStart ? complexityStart : toIntCeil(maxDif);
  int complexityRounded = toInt(complexity);
  for (int i=start; i<end; i++)
  {
    int lComplex = Complexity(i);
    int dif = abs(complexityRounded-lComplex);
    // if in doubt, select the last one (it is probably cheaper in terms of rendering)
    if (bestDif>=dif)
    {
      bestDif = dif;
      bestI = i;
    }
  }
  if (bestI<0) bestI = end-1; // best level is not desired, select the worst level

  // verify using the LOD makes sense in terms of shader complexity
  #if _ENABLE_CHEATS
    static bool ControlShaderComplexity = false;
    if (GInput.GetCheat3ToDo(DIK_9))
  {
      ControlShaderComplexity = !ControlShaderComplexity;
      DIAG_MESSAGE(1000,"ControlShaderComplexity %s",ControlShaderComplexity ? "On" : "Off");
  }
  #else
    const bool ControlShaderComplexity = false;
  #endif
  if (ControlShaderComplexity && coveredArea>0 && bestI>start && _lodsAreaFactor<1.0f)
  {
    // TODO: replace with precomputed screenScale limits
    float screenScale = coveredArea/EstimateArea(1);
    float sci = ShaderComplexity(bestI,screenScale);
    // select the better one with a minimal complexity
    for (int l=start; l<bestI; l++)
    {
      float scl = ShaderComplexity(l,screenScale);
      if (scl<sci)
        sci = scl,bestI = l;
    }
  }

  return bestI;
}

float LODShape::GetBlendingFactor(int level, float complexity) const
{
  // In case we are in last level use it it any case
  if (level >= _nGraphical - 1) return 0.0f;

  // Calculate this and next level complexity
  float thislevelComplexity = Complexity(level);
  float nextLevelComplexity = Complexity(level + 1);

  // if lods are not properly ordered, we cannot compute blend factor
  // this was added because some models seem to have two lods with the same face count
  if (nextLevelComplexity>=thislevelComplexity)
  {
    return 1;
  }
  // Calculate blending factor from approx. range <-0.5, 0.5>
  float origFactor = (complexity - thislevelComplexity) / (nextLevelComplexity - thislevelComplexity);

  // Make the coefficient more steep
  const float steepCoef = 4.0f;
  float steepFactor = origFactor * steepCoef;
  
  // Convert steepFactor from range <-1, 1> into <0, 1>
  float factor01 = (steepFactor + 1.0f) * 0.5f;

  // Saturate the factor and return
  saturate(factor01, 0.0f, 1.0f);
  return factor01;
}

static float SmokeGenFactor = 0.010f;

float Scene::GetSmokeGeneralization() const
{
  float generalization = SmokeGenFactor/_drawDensity;
  return generalization;
}

static bool FarEnoughForOcclusion( const SortObject *oi )
{
  // do not occlude things that are very near
  // the test would be very slow and is very like to fail
  const float occNearest = 20;
  if (oi->distance2>Square(occNearest+50)) return true;
  else
  {
    float distNear = oi->distance2*InvSqrt(oi->distance2) - oi->radius;
    if (distNear>occNearest) return true;
  }
  return false;
}

struct CmpInstanceContext
{
  GridRectangle _gridLightsBoundingRect;
  Scene *_scene;
};

static inline int CmpInstances(
  const ObjectInstanceInfo *info1, const ObjectInstanceInfo *info2, const CmpInstanceContext &ctx
)
{
  int idx1 = ctx._scene->GetLightGridIndex(info1->_origin.Position().X(), info1->_origin.Position().Z());
  int idx2 = ctx._scene->GetLightGridIndex(info2->_origin.Position().X(), info2->_origin.Position().Z());
  if (idx1!=idx2)
  {
    const LightList &lst1 = ctx._scene->GetLightGridItem(idx1);
    const LightList &lst2 = ctx._scene->GetLightGridItem(idx2);
    return lst1.Compare(lst2);
  }
  return 0;
}

static float BlendingCoef = 1.6f;

float SortObject::GetBlendingCoef(BlendAlphaOrigin bao, bool canBlend) const
{
  switch (bao)
  {
  default:
    Fail("Unknown BlendAlphaOrigin");
    return 1;
  case BAO_None:
    return 1;
  case BAO_Src:
  case BAO_Dst:
    // when canBlend is disabled, we return 0 for one side and 1 for the other side for the threshold value
    bool invert = bao==BAO_Src;
    if (!canBlend) return blendFactor<0.5 ? invert : !invert;
    float factor = blendFactor;
    if (invert) factor = 1-factor;
    return floatMin(factor * BlendingCoef, 1.0f);
    //return 1.0f - max(blendFactor * 2.0f - 1.0f, 0.0f);
  }
}

/*
@param instancesBeg start of the instanced chain 
@param instancesBeg end of the instanced chain - inclusive
*/
void Scene::DrawObjectsInstanced(
  int cb, const SortObjectListArg &drawPass, int instancesBeg, int instancesEnd,
  bool allowObjectSort, int level, SortObject::BlendAlphaOrigin bao, bool updateBF, const DrawParameters &dp
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{  
  const int maxInstancingChain = 512;
  // if instance count is very large, it would cause memory allocation
  // we rather to split it into several chains on the top level instead
  
  while (instancesBeg<=instancesEnd)
  {
    // count instances including chains passed to us from the object lists
    int instCount = 0;
    int iiEnd;
    for (iiEnd=instancesBeg; iiEnd<=instancesEnd; iiEnd++)
    {
      SortObject *sObj = drawPass[iiEnd];
      int count = sObj->_listEnd-sObj->_listBeg;
      int newCount = instCount + count;
      // if there would be too much instances, flush the buffer
      if (newCount>maxInstancingChain)
  {
        break;
      }
      instCount = newCount;
    }
    Assert(instCount>0);
    // Prepare array of instances
    AUTO_STATIC_ARRAY_16(ObjectInstanceInfo,instances,maxInstancingChain);
    // create a list of instances
    // we need to 
    instances.Resize(instCount);
    float minDist2 = FLT_MAX/1000; // be prepared minDist2 will be multiplied - avoid inf
    float maxCoveredArea = 0;
    int tgt = 0;
    for (int ii=instancesBeg; ii<iiEnd; ii++)
    {
      SortObject *iObj = drawPass[ii];
      #if DPRIM_STATS
        int startTgt = tgt;
      #endif
      bool canBlend = iObj->GetObject()->GetShape()->CanBlend();
      if (const ObjectListFull *list = iObj->_list)
      {
				int li;
        for (li = iObj->_listBeg; (li+1)<iObj->_listEnd; li++) 
        {
					PrefetchT0Off(list->Get(li+1), 0);
					if (iObj->IsVisible(li))
					{
            Object *obj = list->Get(li);

            float color = iObj->GetBlendingCoef(bao,canBlend);
            if (color>0)
            {
            ObjectInstanceInfo &info = instances[tgt++];
            info._origin = obj->RenderVisualState().Transform();
              info._color = Color(1,1,1,color);
              info._shadowCoef = GetLandShadowSmooth(info._origin.Position());
            info._object = info._parentObject = obj;
            info._limitSpriteSize = iObj->limitSpriteSize;
          }
          }
          if (updateBF) iObj->UpdateBlendFactor();
        }
				//do the last one
				{
					if (iObj->IsVisible(li))
					{
            Object *obj = list->Get(li);
            float color = iObj->GetBlendingCoef(bao,canBlend);
            if (color>0)
            {

            ObjectInstanceInfo &info = instances[tgt++];
            info._origin = obj->RenderVisualState().Transform();
              info._color = Color(1,1,1,color);
              info._shadowCoef = GetLandShadowSmooth(info._origin.Position());
            info._object = info._parentObject = obj;
            info._limitSpriteSize = iObj->limitSpriteSize;
          }
          }
          if (updateBF) iObj->UpdateBlendFactor();
        }
        // assume scale is around 1, therefore the same for all objects here
        if (minDist2>iObj->distance2) // TODO: this should perhaps be done inside in the loop - or can distance2 be assumed to be known for all at once?
        {
          minDist2 = iObj->distance2;
        }
        if (iObj->coveredArea>maxCoveredArea) maxCoveredArea = iObj->coveredArea;
      }
      else
      {
        float color = iObj->GetBlendingCoef(bao,canBlend);
        if (color>0)
        {
        ObjectInstanceInfo &info = instances[tgt++];
        info._origin = iObj->position.position;
          info._color = Color(1,1,1,color);
        Assert(!iObj->position.camSpace);
          info._shadowCoef = GetLandShadowSmooth(info._origin.Position());
        info._object = info._parentObject = iObj->GetObject();
        info._limitSpriteSize = iObj->limitSpriteSize;
        }
        float scale2 = Square(iObj->GetObject()->RenderVisualState().Scale());
        if (minDist2*scale2>iObj->distance2)
        {
          minDist2 = iObj->distance2/scale2;
        }
        if (iObj->coveredArea>maxCoveredArea) maxCoveredArea = iObj->coveredArea;
        if (updateBF) iObj->UpdateBlendFactor();
      }

#if DPRIM_STATS
      if ((cb<0 || Glob.config._mtSerial) && GEngine->ShowFpsMode()==FpsPixs)
	    {
        ShapeUsed shape = iObj->GetObject()->GetShape()->Level(level);
        if (shape->NSections()>0)
        {
          int count = tgt-startTgt;
          float coveredAreaPerSection = iObj->coveredArea / shape->NSections();
          for (int i = 0; i < shape->NSections(); i++)
          {
            const ShapeSection &sec = shape->GetSection(i);
            TexMaterial *mat = sec.GetMaterialExt();
            if (mat)
            {
              BString<64> s;
              sprintf(s, "%d : %s", 0, cc_cast(mat->GetName()));
              PixelStatsCount(s, coveredAreaPerSection*count);
            }
            else
            {
              PixelStatsCount("#NoMaterial", coveredAreaPerSection*count);
            }
          }
        }
      }
#endif
    }
    // array may be used only partially, as we may have skipped some items
    Assert(tgt<=instances.Size());
    instances.Resize(tgt);
    SortObject *oiBeg=drawPass[instancesBeg];
    // advance to the end of what we have already rendered
    instancesBeg = iiEnd;

    // flush instanced chain
    Object *obj = oiBeg->GetObject();

    // The level is instance-able - already verified, and no GC was performed in between
    // TODO:MC: removed ShapeUsed from LevelUsesVBuffer
    DoAssert(obj->CanBeInstanced(level, oiBeg->distance2, dp));
    // airborne objects need special light selection and therefore cannot be instanced
    Assert(!obj->Airborne());

    // In case there is a day here (or zero lights) then draw it without worrying,
    // else draw it split into groups
    if (_gridLights.Size() == 0 /*(NLights()==0 || MainLight()->NightEffect()<0.01)*/)
    {
      // Draw the instances
      LightList empty;
      obj->DrawSimpleInstanced(
        cb, level, ClipAll, dp, instances.Data(), instances.Size(), minDist2, empty, oiBeg, maxCoveredArea
#if _HELISIM_RENDER_TO_TEXTURE
        , drawListId
#endif
      );
    }
    else
    {
      // Sort the array of instances into a light grid
      if (allowObjectSort)
      {
        CmpInstanceContext ctx;
        ctx._gridLightsBoundingRect = _gridLightsBoundingRect;
        ctx._scene = this;
        QSort(instances, ctx, CmpInstances);
      }
      
      // Draw instances grouped by a light grid
      int instanceIndex = 0;
      while (instanceIndex < instances.Size())
      {
        const ObjectInstanceInfo &info = instances[instanceIndex];
        int lastLightGridIndex = GetLightGridIndex(info._origin.Position().X(), info._origin.Position().Z());
        const LightList *lastLights = _gridLights[lastLightGridIndex];
        int newInstanceIndex;
        for (newInstanceIndex = instanceIndex + 1; newInstanceIndex < instances.Size(); newInstanceIndex++)
        {
          const ObjectInstanceInfo &info = instances[newInstanceIndex];
          int newLightGridIndex = GetLightGridIndex(info._origin.Position().X(), info._origin.Position().Z());
          if (newLightGridIndex != lastLightGridIndex)
          {
            const LightList *lights = _gridLights[newLightGridIndex];
            if (!lights->ContainsIdenticalLights(*lastLights))
            {
              break;
            }
          }
        }

        // Now, concatenated instances are in interval <instanceIndex, newInstanceIndex)
        // Draw them
        obj->DrawSimpleInstanced(
          cb, level, ClipAll, dp, &instances[instanceIndex], newInstanceIndex - instanceIndex, minDist2,
          *_gridLights[lastLightGridIndex], oiBeg, maxCoveredArea
#if _HELISIM_RENDER_TO_TEXTURE
          , drawListId
#endif
        );

        // Set the instance index to the new value
        instanceIndex = newInstanceIndex;
      }
    }
    
  }
}

void Scene::DrawObjectsShadowVolumeInstanced(
  int cb, const SortObjectListArg &drawPass, int instancesBeg, int instancesEnd,
  int level
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  PROFILE_SCOPE(scSVI);
  // Prepare array of instances
  AUTO_STATIC_ARRAY(ObjectInstanceInfo, instances, 256);
  SortObject *oiBeg=drawPass[instancesBeg];
  // flush instanced chain
  Object *obj = oiBeg->GetObject();
  // create a list of instances
  instances.Resize(instancesEnd-instancesBeg+1);
  for (int ii=instancesBeg; ii<=instancesEnd; ii++)
  {
    SortObject *iObj = drawPass[ii];
    ObjectInstanceInfo &info = instances[ii-instancesBeg];
    info._origin = iObj->position.TransformWorld(*GetCamera());
    info._object = info._parentObject = iObj->GetObject();
  }

  // Draw the instances
#if _HELISIM_RENDER_TO_TEXTURE
  obj->DrawShadowVolumeInstanced(cb,oiBeg->shape, level, instances.Data(), instances.Size(), oiBeg, drawListId);
#else
  obj->DrawShadowVolumeInstanced(cb,oiBeg->shape, level, instances.Data(), instances.Size(), oiBeg);
#endif
}

/// functor to get LOD level for normal rendering
struct CompareDrawLevel
{
  int operator () (const SortObject &oi1, const SortObject &oi2) const
  {
    int sDif = oi1.drawLOD-oi2.drawLOD;
    if (sDif) return sDif;
    if (oi1.srcLOD==oi2.srcLOD && oi1.dstLOD==oi2.dstLOD) return 0; // same LOD blending
    if (oi1.srcLOD<0 && oi2.srcLOD==oi2.dstLOD) return 0; // unknown LOD and no LOD blending
    if (oi1.srcLOD==oi1.dstLOD && oi2.srcLOD<0) return 0; // no LOD blending and unknown LOD
    sDif = oi1.srcLOD-oi2.srcLOD;
    if (sDif) return sDif;
    sDif = oi1.dstLOD-oi2.dstLOD;
    if (sDif) return sDif;
    return 0;
  }
};

/// functor to get LOD level for shadow buffer rendering
struct CompareLevelDrawShadowBuffer
{
  int operator () (const SortObject &oi1, const SortObject &oi2) const {return oi1.shadowLOD-oi2.shadowLOD;}
};

struct LoadedLevelsInfo
{
  //!{ Loaded level (if no blending then src and dst will have the same value)
  int src,dst;
  //@}
  LoadedLevelsInfo(){}
  LoadedLevelsInfo(int src, int dst):src(src),dst(dst){}
  LoadedLevelsInfo(int level):src(level),dst(level){}
};

/// How much of the blend has already been done (blendFactor is bidirectional)

static inline float BlendElapsed(const SortObject &so) // TODO: make member function of SortObject
{
  return so.DirectionReversed() ? 1-so.blendFactor : so.blendFactor;
  }

static inline int MajorLOD(const SortObject &so)
{
  return so.blendFactor<0.5f ? so.srcLOD : so.dstLOD;
}

static inline float MinorLODBlendingCoef(SortObject &so)
{
  bool canBlend = so.GetObject()->GetShape()->CanBlend();
  return so.blendFactor<0.5f ? so.GetBlendingCoef(SortObject::BAO_Dst,canBlend) : so.GetBlendingCoef(SortObject::BAO_Src,canBlend);
}

static inline void SetMinorLOD(SortObject &so, int lod)
{
  (so.blendFactor<0.5f ? so.dstLOD : so.srcLOD) = lod;
}

struct ShapeLoadedLevels
{

  LoadedLevelsInfo operator () (int level, SortObject &so, bool needed) const
  {
  #if TRACK_SHAPE
    bool interested = !strcmp(so.shape->GetName(),InterestedInShape);
    if (interested)
    {
      __asm nop;
    }
  #endif
    int srcLoaded,dstLoaded,wantedLoaded;
    // Fill out default values
    // If blending is not in progress then either start blending or update the loaded levels, else check
    // the source and the destination
    if (so.srcLOD == so.dstLOD)
    {
      int oldLOD = so.dstLOD;
      // Try to load the level corresponding to the LOD which was used before
      int loaded = so.shape->UseLevel(level, so.distance2, needed, oldLOD);

      if (so.srcLOD<0)
      {
        // no LOD used yet, switch to the new one
        wantedLoaded = srcLoaded = dstLoaded = loaded;
      }
      else if (oldLOD != loaded /*&& so.shape->CanBlend()*/)
      { // If we now want to use a different LOD (determined by oldLOD), then start the blending
        // Make sure the source lod is loaded as well
        srcLoaded = so.shape->UseLevel(oldLOD, so.distance2, needed, oldLOD);

        // In the unlikely case the source LOD is not ready, don't allow the blending
        if (oldLOD>=0 && srcLoaded != oldLOD)
        {
          wantedLoaded = srcLoaded = dstLoaded = loaded;
        }
        else
        {
          // Remember the destination lod
          wantedLoaded = dstLoaded = loaded;
        }
      }
      else
      {
        // No blending
        wantedLoaded = srcLoaded = dstLoaded = loaded;
      }
    }
    else
    {
      DoAssert(so.blendFactor >= 0.0f);

      // Make sure both the source and the destination lod are loaded
      srcLoaded = so.shape->UseLevel(so.srcLOD, so.distance2, needed, so.srcLOD);
      dstLoaded = so.shape->UseLevel(so.dstLOD, so.distance2, needed, so.dstLOD);

      // In the unlikely case one of the LODs is not ready, disable LOD blending
      if (srcLoaded != so.srcLOD || dstLoaded != so.dstLOD)
      {
        srcLoaded = dstLoaded;
      }
      
      if (level==so.srcLOD || level==srcLoaded) wantedLoaded = srcLoaded;
      else if (level==so.dstLOD || level==dstLoaded) wantedLoaded = dstLoaded;
      else wantedLoaded = so.shape->UseLevel(level, so.distance2, needed, so.dstLOD);
      
      // check which one is closer
    }
  #if TRACK_SHAPE
    if (interested)
    {
      DIAG_MESSAGE(0,"LoadedLevels %s: %d,%d,%d",cc_cast(so.shape->GetName()),so.drawLOD,srcLoaded,dstLoaded);
    }
    if (interested && so.dstLOD==3 && dstLoaded<2)
    {
      __asm nop;
    }
  #endif
    LoadedLevelsInfo info(srcLoaded,dstLoaded);
    
    const float stopBlendThold = 0.25f;

    if (so.srcLOD==so.dstLOD || BlendElapsed(so)<stopBlendThold || MinorLODBlendingCoef(so)<=0)
    { // if there is no blend or it has started only recently, we can start a new one
      if (so.srcLOD<0 /*|| !so.shape->CanBlend()*/) // no LOD, or blending from the wanted lod has not started yet
      {
        so.dstLOD = so.srcLOD = info.src;
        so.blendFactor = -1;
      }
      else if (wantedLoaded>=0)
      {
        if (so.srcLOD==so.dstLOD)
        {
          so.dstLOD = wantedLoaded;
          so.blendFactor = 0;
        }
        else
        {
          // we need to keep the lod which is currently the major one, just replace the minor one
          SetMinorLOD(so,wantedLoaded);
        }
      }
      // if both LODs are now actually the same, we can cancel the blend
      if (so.srcLOD==so.dstLOD)
      {
        so.blendFactor = -1;
    }
      else if (so.srcLOD>so.dstLOD)
      { // we reverse the blend direction to keep src<=dst, so that blends in both directions are instancable together
        Swap(so.srcLOD,so.dstLOD);
        so.blendFactor = 1-so.blendFactor;
      }
      DoAssert(so.srcLOD==so.dstLOD || so.blendFactor>=0);
    }
    info.src = so.srcLOD;
    info.dst = so.dstLOD;
    return info;
  }
};

struct ShapeLoadedLevelsShadowOrSplit
{

  LoadedLevelsInfo operator () (int level, SortObject &so, bool needed) const
  {
    Object *obj = so.GetObject();
    RememberSplit *splitEntry = obj->GetShadow(level);
    // select loaded level
    if (splitEntry && splitEntry->IsLoaded())
    {
      return level;
    }
    // shadow vertex buffer not ready - check if data are loaded
    return so.shape->UseLevelAsShadow(level,needed);
  }
};

struct ShapeLoadedLevelsSB
{
  LoadedLevelsInfo operator () (int level, SortObject &so, bool needed) const
  {
    return so.shape->UseLevelAsSBShadow(level,needed);
  }
};


#if _PROFILE || _DEBUG
  #define PIX_NAMED_CB_SCOPES 1
#endif

/// micro-jobs are gathered first, processed by JobManager later
class MicroJobList
{
  /// Tasks preparing draw matrices to the cache
  AutoArray< SRef<IMicroJob>, MemAllocLocal<SRef<IMicroJob>, 1024> > _prepareJobs;
  /// Tasks encapsulating drawing itself (recording to command buffers)
  AutoArray< SRef<IMicroJob>, MemAllocLocal<SRef<IMicroJob>, 1024> > _jobs;
  
  public:
  
  void AddPrepare(const SRef<IMicroJob> &task)
  {
    if (task) _prepareJobs.Add(task);
  }
  void Add(const SRef<IMicroJob> &task)
  {
    _jobs.Add(task);
  }

  void Start()
  {
    // this is the easiest way to make sure objects are captured in the "immediate" CB
    GEngine->StartCBScope(0,0);
    GEngine->EndCBScope(); // EndCBScope is basicaly a nop - used only so that API calls are nicely paired
  }
  /// do all the work stored in micro-jobs (record CBs and play them)
  void Execute(int nResults)
  {
    // prepare draw matrices
    {
      PROFILE_SCOPE_GRF_EX(aniMt,*,0);
      if (Glob.config._mtSerialMat)
      {
        GJobManager.ProcessMicroJobsSerial(NULL, 0, _prepareJobs);
      }
      else
      {
        GJobManager.ProcessMicroJobs(NULL, 0, _prepareJobs);
      }
      _prepareJobs.Clear();
    }

    // create command buffers
    {
      if (Glob.config._mtSerial)
      {
        PROFILE_SCOPE_GRF_EX(recCB,*,0);
        GEngine->StartCBScope(1,nResults);
        GJobManager.ProcessMicroJobsSerial(NULL,0,_jobs);
      }
      else
      {
        PROFILE_SCOPE_GRF_EX(recCB,*,0);
        GEngine->StartCBScope(GJobManager.GetMicroJobCount(),nResults);
        GJobManager.ProcessMicroJobs(NULL,0,_jobs);
      }
      GEngine->EndCBScope();
      _jobs.Clear();
    }

  }
};

/// no real micro-jobs - each task is executed instead of adding into a list

class MicroJobFakeList
{
  public:

  void AddPrepare(const SRef<IMicroJob> &task)
  {
    if (task) (*task)(NULL,-1);
  }
  void Add(const SRef<IMicroJob> &task)
  {
    // we are currently inside of a CB scope
    (*task)(NULL,-1);
  }

  void Start()
  {
    GEngine->StartCBScope(0,0);
  }
  void Execute(int nResults)
  {
    GEngine->EndCBScope();
  }
  
};

// access either of two based on the CB flag
static MicroJobList GMicroJobsCB;
static MicroJobFakeList GMicroJobsFake;

inline void MicroJobsStart()
{
  if (Glob.config._useCB) GMicroJobsCB.Start();
  else GMicroJobsFake.Start();
}
inline void MicroJobsAddPrepare(const SRef<IMicroJob> &task)
{
  if (Glob.config._useCB) GMicroJobsCB.AddPrepare(task);
  else GMicroJobsFake.AddPrepare(task);
}
inline void MicroJobsAdd(const SRef<IMicroJob> &task)
{
  if (Glob.config._useCB) GMicroJobsCB.Add(task);
  else GMicroJobsFake.Add(task);
}

inline void MicroJobsExecute(int nResults)
{
  if (Glob.config._useCB) GMicroJobsCB.Execute(nResults);
  else GMicroJobsFake.Execute(nResults);
}
  

/// common infrastructure for all micro-jobs 
struct ObjDrawBase: public IMicroJob
{
  /// slot where the result should be stored
  int _resultIndex;
  
  /// we always need keep the "shape" loaded
  ShapeUsed _shapeLockSrc;
  ShapeUsed _shapeLockDst;
  
  // ID of the command buffer as returned by StartCBRecording
  int _cb;
  /// name used for PIX scope
  #if PIX_NAMED_CB_SCOPES
  virtual const char *GetName() const = 0;
  virtual int GetID() const = 0;
  #else
  // when not needed, provide efficient inlined dummy implementation
  const char *GetName() const {return NULL;}
  int GetID() const {return -1;}
  #endif
  
#if _HELISIM_RENDER_TO_TEXTURE
  int _drawObjectsId;

  ObjDrawBase(int resultIndex, LODShape *shape, int srcLevel, int dstLevel, int drawObjectsId)
    :_resultIndex(Glob.config._useCB ? resultIndex : -1),
    _shapeLockSrc(shape ? shape->Level(srcLevel) : ShapeUsed()),
    _shapeLockDst(srcLevel==dstLevel ? _shapeLockSrc : shape ? shape->Level(dstLevel) : ShapeUsed()),
    _drawObjectsId(drawObjectsId)
#else
  ObjDrawBase(int resultIndex, LODShape *shape, int srcLevel, int dstLevel)
  :_resultIndex(Glob.config._useCB ? resultIndex : -1),
  _shapeLockSrc(shape ? shape->Level(srcLevel) : ShapeUsed()),
  _shapeLockDst(srcLevel==dstLevel ? _shapeLockSrc : shape ? shape->Level(dstLevel) : ShapeUsed())
#endif
  {
    // if there is no vertex buffer yet, we need to create it
    shape->OptimizeLevelRendering(srcLevel);
    if (dstLevel!=srcLevel) shape->OptimizeLevelRendering(dstLevel);
  }
  /// prepare - start CB recording, ...
  void Pre(bool sync, int thrId);
  /// post - store CB results
  void Post(bool sync);
};

inline void ObjDrawBase::Pre(bool sync, int thrId)
{
  _cb = GEngine->StartCBRecording(_resultIndex,thrId,GetID());
}
inline void ObjDrawBase::Post(bool sync)
{
  GEngine->StopCBRecording(_cb,_resultIndex,GetName());
}

#include <Es/Memory/normalNew.hpp>
/// Micro task used to parallel call of Object::StoreDrawMatrices
struct ObjStoreDrawMatrices : public IMicroJob
{
  const SortObject *_so;
  Object *_obj;
  LODShape *_shape;
  int _level;
  Object *_parentObject;
  int _rootIndex;
  /// keep the shape level locked during the existence of the task
  ShapeUsed _lock;

  ObjStoreDrawMatrices(Object *obj, LODShape *shape, int level, Object *parentObject, const SortObject *so, int rootIndex)
  : _obj(obj), _shape(shape), _level(level), _parentObject(parentObject),_so(so),_rootIndex(rootIndex),
  _lock(shape->Level(level))
  {}

  void operator () (IMTTContext *context, int thrIndex)
  {
    PROFILE_SCOPE_DETAIL_EX(aniMJ,oSort);
    
    _obj->StoreDrawMatrices(_shape, _level, _parentObject, _so, _rootIndex);
  }
  
  friend class Scene; // we need the Scene to access the allocator
  USE_FAST_ALLOCATOR
};

template <bool sync>
struct ObjDraw: public ObjDrawBase
{
  SortObject *_oi;
  Object *_obj;
  int _srcLoaded;
  int _dstLoaded;
  const DrawParameters _dp;
  
  #if PIX_NAMED_CB_SCOPES
  virtual const char *GetName() const {return _obj && _obj->GetShape() ? _obj->GetShape()->Name() : "";}
  virtual int GetID() const {return _obj ? _obj->ID().Encode() : -1;}
  #endif

#if _HELISIM_RENDER_TO_TEXTURE
  ObjDraw(int resultIndex, SortObject *oi, Object *obj, int srcLoaded, int dstLoaded, const DrawParameters &dp, int objectsDrawId)
  :ObjDrawBase(resultIndex,obj->GetShape(),srcLoaded,dstLoaded,objectsDrawId),
  _oi(oi),_obj(obj),_srcLoaded(srcLoaded),_dstLoaded(dstLoaded),_dp(dp)
  {
  
  }
#else
  ObjDraw(int resultIndex, SortObject *oi, Object *obj, int srcLoaded, int dstLoaded, const DrawParameters &dp)
    :ObjDrawBase(resultIndex,obj->GetShape(),srcLoaded,dstLoaded),
    _oi(oi),_obj(obj),_srcLoaded(srcLoaded),_dstLoaded(dstLoaded),_dp(dp)
  {

  }
#endif
  void operator () (IMTTContext *context, int thrIndex)
  {
    Pre(sync,thrIndex);
    {
      SortObject *oi = _oi;
      PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
      PROFILE_SCOPE_DETAIL_EX(rndMN,oSort);
      SCOPE_GRF(GetName(),0);
      InstanceParameters ip(oi->coveredArea);
      float shadow = GScene->GetLandShadowSmooth(oi->position.PositionWorld(*GScene->GetCamera()));
      Object *parentObj = oi->GetParentObject();
      if (!parentObj) parentObj = _obj;

      Object::ProtectedVisualState<const ObjectVisualState> vs = _obj->RenderVisualStateScope();
      if (_srcLoaded == _dstLoaded)
      {
        GEngine->SetInstanceInfo(_cb,HWhite, shadow);
        _obj->Draw(_cb,_dstLoaded, oi->orClip, _dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
          , ObjDrawBase::_drawObjectsId
#endif
          );
      }
      else
      {
        if (oi->blendFactor<1)
        {
          bool canBlend = oi->GetObject()->GetShape()->CanBlend();
          float coef = oi->GetBlendingCoef(SortObject::BAO_Src,canBlend);
          if (coef>0)
          {
            GEngine->SetInstanceInfo(_cb,Color(1,1,1,coef), shadow);
            _obj->Draw(_cb,_srcLoaded, oi->orClip, _dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
              , ObjDrawBase::_drawObjectsId
#endif
              );
          }
        }
        if (oi->blendFactor>0)
        {
          bool canBlend = oi->GetObject()->GetShape()->CanBlend();
          float coef = oi->GetBlendingCoef(SortObject::BAO_Dst,canBlend);
          if (coef>0)
          {
            GEngine->SetInstanceInfo(_cb,Color(1,1,1,coef), shadow);
            _obj->Draw(_cb,_dstLoaded, oi->orClip, _dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
              , ObjDrawBase::_drawObjectsId
#endif              
              );
        }
      }
    }
      oi->UpdateBlendFactor();
    }
    Post(sync);
  }

  /// Prepare matrices to the cache for a single level, should be called prior Add
  SRef<IMicroJob> PrepareLevel(int level)
  {
    LODShape *shape = _obj->GetShape();

    // decide setEngineStuff for the given level
    bool isSkinned = false;
    if (shape->GetAnimationType() == AnimTypeHardware && shape->LevelUsesVBuffer(level))
    {
      // LOD need to be checked
      ShapeUsed sShape = shape->Level(level);
      // avoid crash when data are corrupted
      if (sShape)
      {
        isSkinned = sShape->GetSubSkeletonSize() > 0;
        if (isSkinned)
        {
          int special = _obj->GetObjSpecial(sShape->Special());
          if ((special & OnSurface) && (sShape->GetAndHints() & ClipLandMask) == ClipLandOn) isSkinned = false; // cannot skin in such a case
        }
      }
      Object *parent = _oi->GetParentObject();
      if (!parent) parent = _obj;
      Object *root = _oi->GetRootObject();
      if (!root) root = _obj;
      // we need to use root object, as this is guaranteed to be unique
#if _HELISIM_RENDER_TO_TEXTURE
      SortObject *rootOI = root->GetInList(ObjDrawBase::_drawObjectsId);
#else
      SortObject *rootOI = root->GetInList();
#endif
      if (!rootOI) rootOI = _oi;
      // check and store matrices
      if (_obj->StoreDrawMatricesNeeded(isSkinned, level, _oi, rootOI))
      {
        if (!sync)
        {
          // create a micro job
          return new ObjStoreDrawMatrices(_obj, shape, level, parent, rootOI, _oi->_rootIndex);
        }
        else
        {
          // when working in sync, do not create a task, execute it straight away
          ObjStoreDrawMatrices store(_obj, shape, level, parent, rootOI, _oi->_rootIndex);
          store(NULL,-1);
          return NULL;
        }
      }
    }

    return NULL;
  }
  
  friend class Scene; // we need the Scene to access the allocator
  USE_FAST_ALLOCATOR
};

template <bool sync>
struct ObjDrawInstanced: public ObjDrawBase
{
  const SortObjectListArg &_list;
  int _beg;
  int _end;
  int _srcLoaded;
  int _dstLoaded;
  const DrawParameters _dp;
  
#if _HELISIM_RENDER_TO_TEXTURE
  ObjDrawInstanced(
    int resultIndex, 
    const SortObjectListArg &list, int beg, int end, int srcLoaded, int dstLoaded,
    const DrawParameters &dp,
    int drawObjectsId
    )
    :ObjDrawBase(resultIndex,list[beg]->GetObject()->GetShape(),srcLoaded,dstLoaded,drawObjectsId),
#else
  ObjDrawInstanced(
    int resultIndex, 
    const SortObjectListArg &list, int beg, int end, int srcLoaded, int dstLoaded,
    const DrawParameters &dp
  )
  :ObjDrawBase(resultIndex,list[beg]->GetObject()->GetShape(),srcLoaded,dstLoaded),
#endif
  _list(list),_beg(beg),_end(end),_srcLoaded(srcLoaded),_dstLoaded(dstLoaded),_dp(dp)
  {
    Assert(sync || _list[_beg]->GetObject()->CanObjDrawAsTask(_srcLoaded) && _list[_beg]->GetObject()->CanObjDrawAsTask(_dstLoaded));
  }
  #if PIX_NAMED_CB_SCOPES
  virtual const char *GetName() const
  {
    Object *obj = _list[_beg]->GetObject();
    return obj->GetShape() ? obj->GetShape()->Name() : "";
  }
  virtual int GetID() const
  {
    Object *obj = _list[_beg]->GetObject();
    return obj ? obj->ID().Encode() : -1;
  }
  #endif
  
  void operator () (IMTTContext *context, int thrIndex)
  {
    Pre(sync,thrIndex);
    {
      PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
      SCOPE_GRF(GetName(),0);
      GEngine->SetInstanceInfo(_cb,HWhite, 1);
      if (_srcLoaded == _dstLoaded)
      {
        GScene->DrawObjectsInstanced(_cb,_list, _beg, _end, true, _dstLoaded, SortObject::BAO_None, false, _dp
#if _HELISIM_RENDER_TO_TEXTURE
          , _drawObjectsId
#endif
          );
      }
      else if (_srcLoaded<_dstLoaded) // always draw finer level last
      {
        GScene->DrawObjectsInstanced(_cb,_list, _beg, _end, true, _dstLoaded, SortObject::BAO_Dst, false, _dp
#if _HELISIM_RENDER_TO_TEXTURE
          , _drawObjectsId
#endif
          );
        GScene->DrawObjectsInstanced(_cb,_list, _beg, _end, true, _srcLoaded, SortObject::BAO_Src, true, _dp
#if _HELISIM_RENDER_TO_TEXTURE
          , _drawObjectsId
#endif
          );
      }
      else
      {
        GScene->DrawObjectsInstanced(_cb,_list, _beg, _end, true, _srcLoaded, SortObject::BAO_Src, false, _dp
#if _HELISIM_RENDER_TO_TEXTURE
          , _drawObjectsId
#endif
          );
        GScene->DrawObjectsInstanced(_cb,_list, _beg, _end, true, _dstLoaded, SortObject::BAO_Dst, true, _dp
#if _HELISIM_RENDER_TO_TEXTURE
          , _drawObjectsId
#endif
          );
      }
    }
    Post(sync);
  }

  /// Prepare matrices to the cache for a single level, should be called prior Add
  SRef<IMicroJob> PrepareLevel(int level)
  {
    // instacing is not used with skining, no prepare needed
    return NULL;
  }

  friend class Scene; // we need the Scene to access the allocator
  USE_FAST_ALLOCATOR
};

template <bool sync>
struct ObjDrawShadow: public ObjDrawBase
{
  SortObject *_oi;
  Object *_obj;
  int _level;
  
#if _HELISIM_RENDER_TO_TEXTURE
  ObjDrawShadow(int resultIndex, Object *obj,SortObject *oi, int level, int drawObjectsId)
    :ObjDrawBase(resultIndex,obj->GetShape(),level,level,drawObjectsId),
    _oi(oi),_obj(obj),_level(level)
  {
  }

#else
  ObjDrawShadow(int resultIndex, Object *obj,SortObject *oi, int level)
  :ObjDrawBase(resultIndex,obj->GetShape(),level,level),
  _oi(oi),_obj(obj),_level(level)
  {
  }
#endif
  
  #if PIX_NAMED_CB_SCOPES
  virtual const char *GetName() const {return _obj && _obj->GetShape() ? _obj->GetShape()->GetName() : "";}
  virtual int GetID() const {return _obj ? _obj->ID().Encode() : -1;}
  #endif
  
  void operator () (IMTTContext *context, int thrIndex)
  {
    Pre(sync,thrIndex);
    {
      PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
      PROFILE_SCOPE_DETAIL_EX(rndMN,oSort);
      SCOPE_GRF(GetName(),0);

#if _HELISIM_RENDER_TO_TEXTURE
      _obj->DrawExShadow(_cb,_oi->shape,_oi->position,_level,true,false,_oi,ObjDrawBase::_drawObjectsId);
#else
      _obj->DrawExShadow(_cb,_oi->shape,_oi->position,_level,true,false,_oi);
#endif
    }
    Post(sync);
  }

  /// Prepare matrices to the cache for a single level, should be called prior Add
  SRef<IMicroJob> PrepareLevel()
  {
    LODShape *shape = _obj->GetShape();
    ShapeUsed shadow = shape->Level(_level);

    // decide setEngineStuff for the given level
    bool isSkinned = shape->GetAnimationType() == AnimTypeHardware && shadow->GetSubSkeletonSize() > 0;

    Object *parent = _oi->GetParentObject();
    if (!parent) parent = _obj;
    Object *root = _oi->GetRootObject();
    if (!root) root = _obj;
    // we need to use root object, as this is guaranteed to be unique
#if _HELISIM_RENDER_TO_TEXTURE
    SortObject *rootOI = root->GetInList(ObjDrawBase::_drawObjectsId);
#else
    SortObject *rootOI = root->GetInList();
#endif
    if (!rootOI) rootOI = _oi;
    // check and store matrices
    if (_obj->StoreDrawMatricesNeeded(isSkinned, _level, _oi, rootOI))
    {
      // create a micro job
      if (!sync)
      {
        return new ObjStoreDrawMatrices(_obj, shape, _level, parent, rootOI, _oi->_rootIndex);
      }
      else
      {
        ObjStoreDrawMatrices store(_obj, shape, _level, parent, rootOI, _oi->_rootIndex);
        store(NULL,-1);
        return NULL;
      }
    }
    return NULL;
  }

  friend class Scene; // we need the Scene to access the allocator
  USE_FAST_ALLOCATOR
};

template <bool sync>
struct ObjDrawShadowInstanced: public ObjDrawBase
{
  const SortObjectListArg &_list;
  int _beg;
  int _end;
  int _iLevel;
  
#if _HELISIM_RENDER_TO_TEXTURE
  ObjDrawShadowInstanced(int resultIndex, const SortObjectListArg &list, int beg, int end, int iLevel, int drawObjectsId)
  :ObjDrawBase(resultIndex,list[beg]->GetObject()->GetShape(),iLevel,iLevel, drawObjectsId),
  _list(list),_beg(beg),_end(end),_iLevel(iLevel)
  {
  
  }
#else
  ObjDrawShadowInstanced(int resultIndex, const SortObjectListArg &list, int beg, int end, int iLevel)
    :ObjDrawBase(resultIndex,list[beg]->GetObject()->GetShape(),iLevel,iLevel),
    _list(list),_beg(beg),_end(end),_iLevel(iLevel)
  {

  }
#endif
  #if PIX_NAMED_CB_SCOPES
  virtual const char *GetName() const
  {
    Object *obj = _list[_beg]->GetObject();
    return obj->GetShape() ? obj->GetShape()->GetName() : "";
  }
  virtual int GetID() const
  {
    Object *obj = _list[_beg]->GetObject();
    return obj ? obj->ID().Encode() : -1;
  }
  #endif
  
  void operator () (IMTTContext *context, int thrIndex)
  {
    Pre(sync,thrIndex);
    {
      PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
      SCOPE_GRF(GetName(),0);

#if _HELISIM_RENDER_TO_TEXTURE
      GScene->DrawObjectsShadowVolumeInstanced(_cb, _list, _beg, _end, _iLevel, ObjDrawBase::_drawObjectsId);
#else
      GScene->DrawObjectsShadowVolumeInstanced(_cb, _list, _beg, _end, _iLevel);
#endif
    }
    Post(sync);
  }

  /// Prepare matrices to the cache for a single level, should be called prior Add
  SRef<IMicroJob> PrepareLevel()
  {
    return NULL;
  }

  friend class Scene; // we need the Scene to access the allocator
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

/// collect instanced chains
/**
@param prepareNeeded sometimes we need to execute the "Prepare" tasks as well
*/

template <
  bool basicLODs = true, class LoadedLevelsFunctor=ShapeLoadedLevels, class CmpLevelFunctor=CompareDrawLevel,
  bool prepareNeeded=false
>
struct MergeInstances
{
  /// export prepareNeeded so that is can be queried from outside
  static const bool isPrepareNeeded = prepareNeeded;
  
  /// start of instanced chain
  int _beg;
  /// end of instanced chain
  int _end;
  /// count of the objects participating in the current chain
  /**
  this is different from _end+1-_beg, as one item may represent multiple objects
  Upper bound - real count may be lower because of SortObject::IsVisible
  */
  int _countObjects;
  
  LoadedLevelsInfo _loaded;
  //! Draw parameters
  DrawParameters _dp;
  /// functor for testing if level is already loaded
  LoadedLevelsFunctor _loadedLevels;
  /// functor for testing if level is already loaded
  CmpLevelFunctor _cmpLevel;
  
  /// size of the SortObject list being processed
  int _nSlots;
  
  /// index of the currently processed slot
  int _iSlot;

#if _HELISIM_RENDER_TO_TEXTURE
  int _drawObjectsId;
  MergeInstances(int nSlots, int drawObjectsId, LoadedLevelsFunctor loadedLevels = LoadedLevelsFunctor(), CmpLevelFunctor cmpLevel=CmpLevelFunctor())
    :_beg(-1),_end(-1),_loadedLevels(loadedLevels),_cmpLevel(cmpLevel),_nSlots(nSlots), _drawObjectsId(drawObjectsId), _iSlot(0)
  {
    MicroJobsStart();
  }
#else
  MergeInstances(int nSlots, LoadedLevelsFunctor loadedLevels = LoadedLevelsFunctor(), CmpLevelFunctor cmpLevel=CmpLevelFunctor())
  :_beg(-1),_end(-1),_loadedLevels(loadedLevels),_cmpLevel(cmpLevel),_nSlots(nSlots),_iSlot(0)
  {
    MicroJobsStart();
  }
#endif

  ~MergeInstances()
  {
    // avoid cleaning up after each instance - clean after all are done
    MicroJobsExecute(_iSlot);
    // note: cleaning needs to be done after GMicroJobs.Execute was called
    // we cannot optimize here, as we cannot rely upon the state
    GEngine->SetInstanceInfo(-1, HWhite, 1, false);
  }

  
  void Flush(Scene *scene, SortObjectListArg &list)
  {
    if (_beg>=0)
    {
      Object *obj = list[_beg]->GetObject();
      if (obj->CanObjDrawAsTask(_loaded.src) && (_loaded.src==_loaded.dst || obj->CanObjDrawAsTask(_loaded.dst)))
      {
        ObjDrawInstanced<false> *ptr = new ObjDrawInstanced<false>(_iSlot++,list,_beg,_end,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
          , _drawObjectsId
#endif
          );

        SRef<IMicroJob> draw(ptr);
        
        // store matrices to the cache
        if (prepareNeeded)
        {
          MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.src));
          if (_loaded.dst != _loaded.src) MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.dst));
        }
        MicroJobsAdd(draw);
      }
      else
      {
        ObjDrawInstanced<true> draw(-1,list,_beg,_end,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
          ,_drawObjectsId
#endif
                    );

        // store matrices to the cache
        if (prepareNeeded)
        {
          draw.PrepareLevel(_loaded.src);
          if (_loaded.dst != _loaded.src) draw.PrepareLevel(_loaded.dst);
        }
        draw(NULL,-1);
      
      }
      FreeOnDemandGarbageCollect(512*1024,128*1024);
      _end = _beg = -1;
    }
  }

private:
  /// opening a new chain
  void Start(Scene *scene, SortObjectListArg &list, int i, int level, bool needed)
  {
    SortObject *oi=list[i];
    // check if the level can be instanced
    Object *obj = oi->GetObject();

    // Load level(s) that are to be drawn
    _loaded = _loadedLevels(level, *oi, needed);

    if (_loaded.src < 0 || _loaded.dst < 0)
    {
      // reset instancing chain
      _beg = _end = -1;
    }
    else if (
      Glob.config.enableInstancing && obj->CanBeInstanced(_loaded.src, oi->distance2, _dp)
      && (_loaded.src == _loaded.dst || obj->CanBeInstanced(_loaded.dst, oi->distance2, _dp))
      && !oi->position.camSpace // camSpace not supported for instanced rendering
    )
    {
      _beg = i;
      _end = i;
      _countObjects = oi->_listEnd-oi->_listBeg;
    }
    else
    {
      #if _ENABLE_CHEATS
      // we may need to handle lists when rendering in special rendering modes (geometry)
      // another case are bridges, which use lists and cannot be instanced
      if (oi->_list)
      {
        for (int i=oi->_listBeg; i<oi->_listEnd; i++)
        {
          // consider using oi->IsVisible dithering here
          Object *obj = oi->_list->Get(i);
          if (obj->CanObjDrawAsTask(_loaded.src) && (_loaded.src == _loaded.dst || obj->CanObjDrawAsTask(_loaded.dst)))
          {
            ObjDraw<false> *ptr = new ObjDraw<false>(_iSlot++,oi,obj,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
              , _drawObjectsId
#endif
              );
            SRef<IMicroJob> draw(ptr);
            // store matrices to the cache
            if (prepareNeeded) 
            {
              MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.src));
              if (_loaded.dst != _loaded.src) MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.dst));
            }

            MicroJobsAdd(draw);
          }
          else
          {
            // some object may need to be rendered in sync (e.g. dynamic VBs for seagulls)
            // TODO:MC: done this way rendering order is changed
            // if this is a problem, we need to be able to execute micro-jobs directly while rendering instead
            ObjDraw<true> draw(-1,oi,obj,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
              , _drawObjectsId
#endif
              );

            // store matrices to the cache
            if (prepareNeeded) 
            {
              draw.PrepareLevel(_loaded.src);
              if (_loaded.dst != _loaded.src)
              {
                draw.PrepareLevel(_loaded.dst);
              }
            }

            draw(NULL,-1);
          }
        }
      }
      else
      #endif
      {
        // cannot be instanced - draw it

        if (obj->CanObjDrawAsTask(_loaded.src) && (_loaded.src==_loaded.dst || obj->CanObjDrawAsTask(_loaded.dst)))
        {
          ObjDraw<false> *ptr = new ObjDraw<false>(_iSlot++,oi,obj,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
            , _drawObjectsId
#endif
            );

          SRef<IMicroJob> draw(ptr);
          // store matrices to the cache
          if (prepareNeeded) 
          {
            MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.src));
            if (_loaded.dst != _loaded.src) MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.dst));
          }

          MicroJobsAdd(draw);
        }
        else
        {
          ObjDraw<true> draw(-1,oi,obj,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
            , _drawObjectsId
#endif
            );

          // store matrices to the cache
          if (prepareNeeded)
          {
            SRef<IMicroJob> prepare = draw.PrepareLevel(_loaded.src);
            if (prepare) (*prepare)(NULL, -1);
            if (_loaded.dst != _loaded.src)
            {
              prepare = draw.PrepareLevel(_loaded.dst);
              if (prepare) (*prepare)(NULL, -1);
            }
          }

          draw(NULL,-1);
          // some object may need to be rendered in sync (e.g. dynamic VBs for seagulls)
        }
      }
      
      FreeOnDemandGarbageCollect(512*1024,128*1024);
      // reset instancing chain
      _beg = _end = -1;
    }
  }

public:
  void Add(Scene *scene, SortObjectListArg &list, int i, int level, bool needed)
  {
    if (_beg<0)
    {      
      Start(scene,list,i,level,needed);
    }
    else
    {
      // we want to avoid too long instancing chains, as they would break concurrency
      static int maxInstancingCB = 128;
      
      SortObject *oi=list[i];

      SortObject *oiBeg=list[_beg];
      if (
        (!Glob.config._useCB || _countObjects<maxInstancingCB) &&
        oiBeg->CanBeInstancedTogether(*oi, _cmpLevel, _dp)
      )
      { // we can instance
        Assert(_end==i-1);
        _end = i;
        _countObjects += oi->_listEnd-oi->_listBeg;

        // If LOD blending was not initialized yet, initialize it the same way as header is, to prevent a sudden switch later once chains are reorganized and this item can become a head itself
        if (oi->srcLOD < 0)
        {
          DoAssert(oi->dstLOD);
          oi->srcLOD = oiBeg->srcLOD;
          oi->dstLOD = oiBeg->dstLOD;
          oi->blendFactor = oiBeg->blendFactor;
      }
      }
      else
      {
        Object *obj = oiBeg->GetObject();
        if (obj->CanObjDrawAsTask(_loaded.src) && (_loaded.src==_loaded.dst || obj->CanObjDrawAsTask(_loaded.dst)))
        {
          ObjDrawInstanced<false> *ptr = new ObjDrawInstanced<false>(_iSlot++,list,_beg,_end,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
            , _drawObjectsId
#endif
            );

          SRef<IMicroJob> draw(ptr);

          // store matrices to the cache
          if (prepareNeeded) 
          {
            MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.src));
            if (_loaded.dst != _loaded.src) MicroJobsAddPrepare(ptr->PrepareLevel(_loaded.dst));
          }
          MicroJobsAdd(draw);
        }
        else
        {
          ObjDrawInstanced<true> draw(-1,list,_beg,_end,_loaded.src,_loaded.dst,_dp
#if _HELISIM_RENDER_TO_TEXTURE
            , _drawObjectsId
#endif
            );

          // store matrices to the cache
          if (prepareNeeded) 
          {
            draw.PrepareLevel(_loaded.src);
            if (_loaded.dst != _loaded.src) draw.PrepareLevel(_loaded.dst);
          }
          draw(NULL,-1);
        
        }
        
        FreeOnDemandGarbageCollect(512*1024,128*1024);
        Start(scene,list,i,level,needed);
      }
    }

  }
};


/**
@return alpha type
  0 no alpha - sort, can be rendered before the landscape
  1 very little alpha (average >0xf0) - do not sort, but render after landscape
  2 some alpha (average<0xd0) - sort by z, but still allow shadow casting
*/

TypeIsSimple(SortObject *)

//typedef MemAllocSimpleStorage<int,32*1024> SortObjStorage;
typedef MemAllocSimpleStorage<int,128*1024> SortObjStorage;

typedef MemAllocSimple<SortObjStorage> SortObjAllocator;

/// information about selected LODs
union LODInfo
{
  /// one integer for comparison purposes
  struct Packed
  {
    unsigned packed1;
    unsigned packed2;
  } packed;
  /// individual values
  struct LODs
  {
    signed char draw;
    signed char shadow;
    signed char shadowVol;
    signed char forceDraw;
    signed char srcLOD;
    signed char dstLOD;
    unsigned char visibleRatio;
  } lod;
  
  LODInfo(signed char dr, signed char sh, signed char sv, signed char fd,
          signed char srcLOD, signed char dstLOD, unsigned char visibleRatio)
  {
    lod.draw = dr;
    lod.shadow = sh;
    lod.shadowVol = sv;
    lod.forceDraw = fd;
    lod.srcLOD = srcLOD;
    lod.dstLOD = dstLOD;
    lod.visibleRatio = visibleRatio;
  }
  LODInfo(){}
  
};

/// identify the slot for batch LOD selection
struct SlotName
{
  LODShape *shape;
  LODInfo lods;
  /// root object may affect LOD selection sometimes (pass3 in 1st person view)
  Ref<Object> root;
  
  explicit SlotName(LODShape *s, LODInfo l, Object *root)
  :shape(s),lods(l),root(root){}
  SlotName(){}
  
  bool operator == (const SlotName &w) const
  {
    return (
      shape==w.shape &&
      lods.packed.packed1==w.lods.packed.packed1 && lods.packed.packed2==w.lods.packed.packed2 &&
      root==w.root
    );
  }
  bool operator < (const SlotName &w) const
  {
    if (shape<w.shape) return true;
    if (shape>w.shape) return false;
    if (lods.packed.packed1<w.lods.packed.packed1) return true;
    if (lods.packed.packed1>w.lods.packed.packed1) return false;
    if (lods.packed.packed2<w.lods.packed.packed2) return true;
    if (lods.packed.packed2>w.lods.packed.packed2) return false;
    if (root<w.root) return true;
    if (root>w.root) return false;
    return false;
  }
};
  
#define MULTIPASS_SHAPE_SORT 0

/// LODShape / distance combination
struct SortShapeSlotDist
{
  /// slot LODShape id
  SlotName name;
  //! Square distance of the closest object in the slot
  float dist2;
  /// which discrete slot
  int discSlot;
  //@{ corresponds to SortObject variables
  /// which LOD it is
  signed char drawLOD,shadowLOD,shadowVolLOD;
  signed char forceDrawLOD; // forced draw LOD
  signed char passNum;

  #if MULTIPASS_SHAPE_SORT
  /// 1st pass to reduce memory allocation - count objects in this slot
  InitVal<int,0> nObjs;
  #endif
  
  FindArray<SortObject *, SortObjAllocator> objs;
  
  int ObjCount() const
  {
    // TODO: optimization: precompute this when creating slots, and only read it here
    // TODO: check if we should consider dithering here as well
    int count = 0;
    for (int i=0; i<objs.Size(); i++)
    {
      const SortObject *so = objs[i];
      count += so->_listEnd-so->_listBeg;
    }
    return count;
  }
};

TypeIsMovableZeroed(SortShapeSlotDist)

TypeIsSimple(SortShapeSlotDist *)

/// information for slot based (radix) sorting of SortObject
struct SortLODShapeSlot
{
  /// which LODShape
  SlotName name;

  /// list of objects with this LODShape, separated by distance
  AutoArray<SortShapeSlotDist,SortObjAllocator> objects;

  bool operator == (const SortLODShapeSlot &w) const {return name==w.name;}
  
  explicit SortLODShapeSlot(const SlotName &name):name(name){}
  
  SortLODShapeSlot(){}
  
};

TypeIsMovableZeroed(SortLODShapeSlot)

template <>
struct BinFindArrayKeyTraits<SortLODShapeSlot>
{
  typedef SlotName KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  /// check if one key is lower than the other
  static bool IsLess(KeyType a, KeyType b) {return a<b;}
  /// get a key from an item
  static KeyType GetKey(const SortLODShapeSlot &a) {return a.name;}
};



// static inline int CmpObjInSlot(SortObject **s1, SortObject **s2)
// {
//   return sign((*s1)->distance2-(*s2)->distance2);
// }


static int CmpShapeSlot(SortShapeSlotDist **ss1, SortShapeSlotDist **ss2)
{
  const SortShapeSlotDist *sd1 = *ss1;
  const SortShapeSlotDist *sd2 = *ss2;
  LODShape *s1 = sd1->name.shape;
  LODShape *s2 = sd2->name.shape;

  int sDif=CompareAddress(s2,s1);
  // no invisible LODs here
  if( sDif )
  {
    return sDif;
  }

  // Compare indices of LODs to draw (if differs then finish)
  sDif = sd1->drawLOD - sd2->drawLOD;
  if (sDif)
  {
    return sDif;
  }

  // no alpha - we want to draw front first to use early Z out
  // discSlot is based on covered area
  // we want small covered area last
  return sd2->discSlot-sd1->discSlot;
}

static int CmpBlendingItem(Ref<SortObject> *ss1, Ref<SortObject> *ss2)
{
  const Ref<SortObject> &s1 = *ss1;
  const Ref<SortObject> &s2 = *ss2;
  LODShape *shape1 = s1->shape;
  LODShape *shape2 = s2->shape;

  int sDif;

  // First sort by shape
  sDif = CompareAddress(shape2,shape1);
  if (sDif) return sDif;

  // compare lod and blending
  sDif = s1->drawLOD-s2->drawLOD;
  if (sDif) return sDif;

  // within the same lod sort in following order
  // blending (src!=dst), non-blending (src==dst, src>=0), fresh (src<0)
  // this way fresh entries can be instanced together with existing non-blending ones
  int s1Cat = s1->srcLOD!=s1->dstLOD ? 0 : s1->srcLOD>=0 ? 1 : 2;
  int s2Cat = s2->srcLOD!=s2->dstLOD ? 0 : s2->srcLOD>=0 ? 1 : 2;
  sDif = s1Cat-s2Cat;
  if (sDif) return sDif;

  sDif = s1->srcLOD-s2->srcLOD;
  if (sDif) return sDif;
  sDif = s1->dstLOD-s2->dstLOD;
  if (sDif) return sDif;
  
  // Sort by distance
  float sDiff = s2->coveredArea - s1->coveredArea;
  if (sDiff > 0.0f) return 1;
  else if (sDiff < 0.0f) return -1;
  else return 0;
}

/// container to hold slots used in one pass
template <int count>
class PassSlots: public AutoArray<SortShapeSlotDist *, MemAllocDataStack<SortShapeSlotDist *,count> >
{
 typedef AutoArray<SortShapeSlotDist *, MemAllocDataStack<SortShapeSlotDist *,count> > base;
public:
  //! Go through the list of object and return the minimum square distance
  float GetMinDist2() const
  {
    float minDist2 = FLT_MAX;
    for (int i = 0; i < base::Size(); i++)
    {
      const float slotDist2 = base::Get(i)->dist2;
      if (slotDist2 < minDist2) minDist2 = slotDist2;
    }
    return minDist2;
  }
};

typedef Array<SortShapeSlotDist *> PassSlotsArg;

/// context to keep data while separating and sorting slots


struct Scene::SplitSlotContext
{
  /// objects divided by LODShape and distance
  BinFindArrayKey<
    SortLODShapeSlot,
    BinFindArrayKeyTraits<SortLODShapeSlot>,
    MemAllocDataStack<SortLODShapeSlot,256>
  > slots;
};

/// number of discrete slots
const int NDiscSlots = 40;

struct Scene::SlotObjectsContext
{
  /// objects separated to slots
  SplitSlotContext objs;
  /// proxies separated to slots
  SplitSlotContext proxies;
  
  static const int slotsInScene = 2048;
  //@{ same meaning as in Scene::DrawContext, but slots instead of objects this time
  PassSlots<slotsInScene*1/8> _drawPass1ON;
  PassSlots<slotsInScene*3/8> _drawPass1OF;
  PassSlots<slotsInScene*1/8> _drawPass1AN;
  PassSlots<slotsInScene*1/8> _drawPass1AF;
  PassSlots<slotsInScene*1/16> _drawOnSurfaces;
  PassSlots<slotsInScene*1/8> _drawPass2;
  PassSlots<32> _drawPass3;
  PassSlots<8> _drawPass4;
  PassSlots<slotsInScene*1/8> _drawShadowVol;
  PassSlots<slotsInScene*1/16> _drawShadowProj;

	#ifdef TEST_RAIN_MAP
		PassSlots<slotsInScene*1/16> _drawRainMap;
	#endif
  //@}
  
  /// storage to avoid dynamic memory allocation
  SortObjStorage sortStorage;
  /// discretization helper for LOD determination
  DiscreteSlots<NDiscSlots> discretize;
  
  SlotObjectsContext(float kCoef, float minArea, int nSlots)
  :discretize(kCoef,minArea,nSlots)
  {
  }

  /// add a single slot into a corresponding list  
  void AddSlot(SortShapeSlotDist *slot);
  
  struct SetZCoordEmpty
  {
    void operator () (SortObject *sObj) const {}
  };

  template <bool checkWater>
  struct SetZCoord
  {
    Scene *scene;

    SetZCoord(Scene *scene):scene(scene){}

    void operator () (SortObject *sObj) const
    {
      Vector3 pos;
      if (sObj->shape->Mass()>0)
      {
        pos = sObj->position.position.FastTransform(sObj->shape->CenterOfMass());
      }
      else
      {
        pos = sObj->position.position.Position();
      }
      // hack for small object inside of a large object
      // we slightly offset the z coordinate 
      // this way small objects are more often rendered before the big ones
      // this was done to ensure co-centric diagnostic spheres are sorted well
      float offset = sObj->radius*0.1f;
      if (sObj->_rootObject)
      {
        // when we are a proxy, we need to be rendered before our proxy container
        // hence we need to offset about his diameter (away from camera)
        // no need to do this when we are in a different pass
        // quick hack: do not do this for pass 2 (alpha) objects - otherwise muzzle flashes sort bad against particles
        if (sObj->passNum<2)
        {
          offset -= sObj->_rootObject->GetRadius()*2;
        }
      }
      if(!sObj->position.camSpace)
      {
        //sObj->zCoord = (toCamera*pos).Z()-offset;
        // optimized: we need only z-coordinate, obtaining it does not require complete transformation
        sObj->zCoord = scene->GetCamera()->Direction()*(pos-scene->GetCamera()->Position())-offset;
        Assert(fabs(sObj->zCoord-((scene->GetCamera()->CalcInvTransform()*pos).Z()-offset))<0.01f);
      }
      else
      {
        sObj->zCoord = pos.Z()-offset;
      }
      // hack for a water: we want water to be rendered first
      // but we do not want the "waterness" to be checked in the sorting function
      if (checkWater && sObj->drawLOD!=LOD_INVISIBLE)
      {
        // the first object is the nearest one
        LODShape *lodShape = sObj->shape;
        if (lodShape->Special()&IsAlpha)
        {
          const Shape *shape = lodShape->GetLevelLocked(lodShape->FindSimplestLevel());
          if (shape && shape->NSections()>0)
          {
            const TexMaterial *mat = shape->GetSection(0).GetMaterialExt();
            if (mat)
            {
              VertexShaderID vs = mat->GetVertexShaderID();
              if (vs==VSWater || vs==VSWaterSimple || vs==VSCalmWater)
              {
                // pretend the water is a lot further away
                sObj->zCoord += Glob.config.horizontZ;
              }
            }
          }
        }
      }
    }
  };

  //! Basic creating of list of objects out of objects in slots
  template <class TgtArray, class SetZCoordFunc>
  void CreateList(TgtArray &tgt, const PassSlotsArg &slots, const SetZCoordFunc &setZCoordFunc)
  {
    for (int i=0; i<slots.Size(); i++)
    {
      SortShapeSlotDist *shapeSlot = slots[i];
      for (int j=0; j<shapeSlot->objs.Size(); j++)
      {
        SortObject *sObj = shapeSlot->objs[j];
        sObj->drawLOD = shapeSlot->drawLOD;
        sObj->shadowLOD = shapeSlot->shadowLOD;
        sObj->shadowVolLOD = shapeSlot->shadowVolLOD;
        sObj->limitSpriteSize = false; // Initialize it somehow
        //sObj->csmLayerReached = ~0; // Initialize it somehow
        sObj->passNum = shapeSlot->passNum;
        setZCoordFunc(sObj);
        tgt.Add(sObj);
      }
    }
  }

  template <class TgtArray>
  void CreateList(TgtArray &tgt, const PassSlotsArg &slots)
  {
    CreateList<TgtArray>(tgt,slots,SlotObjectsContext::SetZCoordEmpty());
  }

  //! Creating of list of objects and separating them between original and blending arrays
  template <class TgtArray>
  void CreateList(TgtArray &tgtOriginal, TgtArray &tgtBlending, const PassSlotsArg &slots)
  {
    for (int i=0; i<slots.Size(); i++)
    {
      SortShapeSlotDist *shapeSlot = slots[i];
      for (int j=0; j<shapeSlot->objs.Size(); j++)
      {
        SortObject *sObj = shapeSlot->objs[j];
        sObj->drawLOD = shapeSlot->drawLOD;
        sObj->shadowLOD = shapeSlot->shadowLOD;
        sObj->shadowVolLOD = shapeSlot->shadowVolLOD;
        sObj->limitSpriteSize = false; // Initialize it somehow
        //sObj->csmLayerReached = ~0; // Initialize it somehow
        sObj->passNum = shapeSlot->passNum;
        if (sObj->srcLOD == sObj->dstLOD)
        {
          tgtOriginal.Add(sObj);
        }
        else
        {
          tgtBlending.Add(sObj);
        }
      }
    }
  }

  //! Creating of list of objects with distance limit
  template <class TgtArray>
  void CreateList(TgtArray &tgt, const PassSlotsArg &slots, float limitDistance)
  {
    float limitDistance2 = Square(limitDistance);
    for (int i=0; i<slots.Size(); i++)
    {
      SortShapeSlotDist *shapeSlot = slots[i];

      // Quick omitting of whole slots
      if (shapeSlot->dist2 < limitDistance2)
      {
        for (int j=0; j<shapeSlot->objs.Size(); j++)
        {
          SortObject *sObj = shapeSlot->objs[j];

          // Clean omitting of single object
          if (sObj->distance2 < limitDistance2)
          {
            sObj->drawLOD = shapeSlot->drawLOD;
            sObj->shadowLOD = shapeSlot->shadowLOD;
            sObj->shadowVolLOD = shapeSlot->shadowVolLOD;
            sObj->limitSpriteSize = false; // Initialize it somehow
            //sObj->csmLayerReached = ~0; // Initialize it somehow
            sObj->passNum = shapeSlot->passNum;
            tgt.Add(sObj);
          }
        }
      }
    }
  }

};

void Scene::SlotObjectsContext::AddSlot(SortShapeSlotDist *slot)
{
  // All objects shadows can be drawn behind them must be in the near passes (as SSSM uses for depth pass near objects only)
  float nearLimitDistance2 = Square(floatMax(Glob.config.GetShadowZ(), 100.0f));

	#ifdef TEST_RAIN_MAP
		bool useRainMap = GLandscape->GetRainDensity() > 0.001f && GEngine->IsRainMapEnabled();
		float nearRainMapDistance2 = useRainMap ? Square(GEngine->GetRainRadius()*1.42f) : -1.0f; //circle in square, we prefer square -> sqrt(2) = ~1.42
	#endif

  Assert(slot->name.shape);
  if( slot->drawLOD!=LOD_INVISIBLE)
  {
    DoAssert(slot->passNum>=-3);
    DoAssert(slot->passNum<=4);
    switch (slot->passNum)
    {
      case -3: // No alpha - normal object
        if (slot->dist2<nearLimitDistance2) _drawPass1ON.Add(slot);
        else _drawPass1OF.Add(slot);
        break;
      case -2: // Marginal alpha (less than slight alpha) - rendering after the landscape required, no z-sorting needed
        if (slot->dist2<nearLimitDistance2) _drawPass1AN.Add(slot);
        else _drawPass1OF.Add(slot);
        break;
      default:
      case -1: // Not decided - common unimportant objects
        _drawPass1OF.Add(slot);
        break;
      case 0: // Slight alpha - rough z-order preferred
        // no trees should ever get here
        #if 0 //_ENABLE_REPORT
          if (!strncmp(slot->name.shape->Name(),"ca\\plants2\\tree",sizeof("ca\\plants2\\tree")-1))
          {
            __asm nop;
          }
        #endif
        if (slot->dist2<nearLimitDistance2) _drawPass1AN.Add(slot);
        else _drawPass1AF.Add(slot);
        break;
      case 1: _drawOnSurfaces.Add(slot);break;
      case 2: _drawPass2.Add(slot);break;
      case 3: _drawPass3.Add(slot);break;
      case 4: _drawPass4.Add(slot);break;
    }
  }
  if (slot->shadowVolLOD!=LOD_INVISIBLE)
  {
    if (slot->name.shape->DrawVolumeShadow(slot->passNum))
    {
      _drawShadowVol.Add(slot);
    }
  }
  if (slot->shadowLOD!=LOD_INVISIBLE)
  {
    if (slot->name.shape->DrawProjShadow(slot->passNum))
    {
      _drawShadowProj.Add(slot);
    }

		#ifdef TEST_RAIN_MAP
			if (slot->dist2 < nearRainMapDistance2 && slot->name.shape->DrawRainMap(slot->passNum))
			{
				_drawRainMap.Add(slot);
			}
		#endif
  }
}


void Scene::SplitObjectsBySlots(SlotObjectsContext &slotCtx, SplitSlotContext &ctx, Ref<SortObject> *sortObj, int nSortObj)
{
  PROFILE_SCOPE_EX(oSplt,*);
  // TODO: precise handling in the innermost circle?
  /// what is minimal / maximal value for covered area?

  // we might want to avoid working arrays to be reallocated
  // 1st pass check how much objects will fit into each slot and allocates memory
  // 2nd pass fills the slots
  // measurements have shown multi-pass solution is slower even in complex scenes
#if MULTIPASS_SHAPE_SORT
  for (int pass=0; pass<2; pass++)
#endif
  {
    for (int i=0; i<nSortObj; i++)
    {
      // check pointer sortObj is still valid
      DoAssert(sortObj+i>=_drawObjects.Data() && sortObj+i<=_drawObjects.Data()+_drawObjects.Size());
      SortObject *sObj = sortObj[i];
      SortLODShapeSlot *slot;
      bool found;
      LODInfo lods(sObj->drawLOD, sObj->shadowLOD, sObj->shadowVolLOD, sObj->forceDrawLOD, sObj->srcLOD, sObj->dstLOD,sObj->_visibleRatio);
      SlotName key(sObj->shape,lods,sObj->GetRootObject());
      int index = ctx.slots.FindKeyPos(key,found);
      if (!found)
      {
        SortLODShapeSlot newSlot(key);
        //index = ctx.slots.Add(newSlot);
        ctx.slots.Insert(index,newSlot);
        slot = &ctx.slots[index];
        slot->objects.SetStorage(slotCtx.sortStorage);
        slot->objects.Realloc(NDiscSlots);
      }
      else
        slot = &ctx.slots[index];
      
      int discSlot = slotCtx.discretize.SlotFromValue2(sObj->coveredArea);

      saturate(discSlot,0,NDiscSlots-1);
      slot->objects.Access(discSlot);
      
      SortShapeSlotDist &distSlot = slot->objects[discSlot];

      #if MULTIPASS_SHAPE_SORT
        if (pass==0)
        {
          // init each slot once
          if (distSlot.nObjs==0)
          {
            distSlot.objs.SetStorage(slotCtx.sortStorage);
            distSlot.discSlot = discSlot;
            distSlot.name = key;
            distSlot.dist2 = FLT_MAX;
          }
          distSlot.nObjs++;
        }
        else
        {
          distSlot.objs.Add(sObj);
          Assert(distSlot.objs.Size()<=distSlot.nObjs);
        }
      #else
        // init each slot once
        if (distSlot.objs.Size()==0)
        {
          distSlot.objs.SetStorage(slotCtx.sortStorage);
          distSlot.discSlot = discSlot;
          distSlot.name = key;
          distSlot.dist2 = FLT_MAX;
        }
        distSlot.objs.Add(sObj);
      #endif
      saturateMin(distSlot.dist2,sObj->distance2);
    }
    #if MULTIPASS_SHAPE_SORT
    if (pass==0)
    {
      for (int i=0; i<ctx.slots.Size(); i++)
      {
        SortLODShapeSlot &slot = ctx.slots[i];
        for (int ds=0; ds<slot.objects.Size(); ds++)
        {
          SortShapeSlotDist &distSlot = slot.objects[ds];
          if (distSlot.nObjs>0)
          {
            distSlot.objs.Realloc(distSlot.nObjs);
          }
        }
      }
    }
    #endif
  }
}

// SlotObjectsContext &ctx, Ref<SortObject> *sortObj, int nSortObj)

static int DisplayComplex(LODShape *shape, int level)
{
  if (level==LOD_INVISIBLE) return 0;
  return shape->Complexity(level);
}

int Scene::ComputeTerrainComplexity() const
{
  return toLargeInt( Square(Glob.config.horizontZ*(1.0f/50)) );
}

#if _ENABLE_REPORT
  #define LOG_LODS 100
  static int LogLodsCounter = 0;
#endif

void Scene::AdjustComplexity(DrawContext &ctx, SlotObjectsContext &slotCtx, bool frameRepeated)
{
  // objects are divided by LODShape and slots
  // we select LODs for them now
  
  #if _ENABLE_CHEATS
  static int displayComplexityLimit = INT_MAX;
  #endif

  PROFILE_SCOPE(sceAC);

  SplitObjectsBySlots(slotCtx,slotCtx.objs,_drawObjects.Data(),_drawObjects.Size());
  
  bool castShadows = CastShadows() && GetObjectShadows();
	bool castRainMap = false;

	#ifdef TEST_RAIN_MAP
		castRainMap = GLandscape->GetRainDensity() > 0.001f && GEngine->IsRainMapEnabled();
	#endif

  CameraType camType = GWorld->GetCameraType();
  
  // for projective shadows we want much less polygons
  static float projShadowAreaCoef = 1.0f/32;
  // for volume shadows we want somewhat less polygons
  static float volShadowAreaCoef = 1.0f/16;
  #if _ENABLE_CHEATS
  //static float invShadowAreaCoef = 1/shadowAreaCoef;
  if( GInput.GetCheat1ToDo(DIK_LBRACKET) )
  {
    MaxDensity /= 1.2f;
    GlobalShowMessage(500,"Density %.5f",MaxDensity);
  }
  if( GInput.GetCheat1ToDo(DIK_RBRACKET) )
  {
    MaxDensity *= 1.2f;
    GlobalShowMessage(500,"Density %.5f",MaxDensity);
  }

  if( GInput.GetCheat2ToDo(DIK_NUMPADSLASH) )
  {
    ChangeLODBlending(+1);
    GlobalShowMessage(500,"LOD blending %d",GLODBlendingMode);
  }
  if( GInput.GetCheat2ToDo(DIK_NUMPADSTAR) )
  {
    GlobalShowMessage(500,"LOD fixed/zoomed");
  }
  if( GInput.GetCheat2ToDo(DIK_NUMPADMINUS) )
  {
    GlobalShowMessage(500,"LOD dynamic/static");
  }

  int complex = 0;
  if( GInput.GetCheat3ToDo(DIK_LBRACKET))
  {
    float bias = +0.2f + GEngine->TextBank()->GetTextureMipBiasMin();
    saturate(bias,0,8);
    GlobalShowMessage(500,"Mip bias %.1f",bias);
    GEngine->TextBank()->SetTextureMipBiasMin(bias);
  }
  if( GInput.GetCheat3ToDo(DIK_RBRACKET))
  {
    float bias = -0.2f + GEngine->TextBank()->GetTextureMipBiasMin();
    saturate(bias,0,8);
    GlobalShowMessage(500,"Mip bias %.1f",bias);
    GEngine->TextBank()->SetTextureMipBiasMin(bias);
    if (bias>2.0f)
    {
      GEngine->TextBank()->SetTextureMipBias(bias);
    }
  }
  if( GInput.GetCheat2ToDo(DIK_LBRACKET) || complex<0 )
  {
    _complexityTarget /=1.1f;
    PixelSizeFromComplexity(_complexityTarget);
    GlobalShowMessage(
      500,"Complexity %.0f, pixel size %.1f..%.1f",
      _complexityTarget,sqrt(MinPixelSize2FromDist2(0)),sqrt(MinPixelSize2FromDist2(1e8f))
    );
  }
  if( GInput.GetCheat2ToDo(DIK_RBRACKET)  || complex>0)
  {
    _complexityTarget *=1.1f;
    PixelSizeFromComplexity(_complexityTarget);
    GlobalShowMessage(
      500,"Complexity %.0f, pixel size %.1f..%.1f",
      _complexityTarget,sqrt(MinPixelSize2FromDist2(0)),sqrt(MinPixelSize2FromDist2(1e8f))
    );
  }

  int changeMode = GInput.GetCheatXToDo(CXTGeomDiags);
  if (changeMode)
  {
    int newMode = DiagDrawModeState+changeMode;
    if (newMode>=NDiagDrawMode) newMode = 0;
    if (newMode<0) newMode = NDiagDrawMode-1;
    DiagDrawModeState = DiagDrawMode(newMode);
    GlobalShowMessage(500,"Draw geom %s",(const char *)FindEnumName(DiagDrawModeState));
    
  }
  #endif

  float maxDensity = MaxDensity; // max. polygons per pixel wanted
  float densityByPixelSize = 0.1f/_minPixelSizeMaxDist; // _minPixelSizeMaxDist cca 10..6..2
  float minDensity = floatMax(maxDensity*0.1f,densityByPixelSize); // min. polygons per pixel wanted

  // preparation: calculate total covered area
  float totalArea = 0;
  // sum complexity of objects that are excluded from lod management
  int complexityUsed = 0;
  int complexityShadows = 0;
  
  for( int i=0; i<slotCtx.objs.slots.Size(); i++ )
  {
    SortLODShapeSlot &slot = slotCtx.objs.slots[i];
    for (int j=0; j<slot.objects.Size(); j++)
    {
      SortShapeSlotDist &slotDist = slot.objects[j];
      int nObjsInSlot = slotDist.ObjCount();
      if (nObjsInSlot<=0) continue;
      // initial values - negative indicates not set yet
      slotDist.drawLOD = slotDist.name.lods.lod.draw;
      float coveredArea = slotCtx.discretize.MinValue2(slotDist.discSlot);
      slotDist.shadowLOD = slotDist.name.lods.lod.shadow;
      slotDist.shadowVolLOD = slotDist.name.lods.lod.shadowVol;
      slotDist.forceDrawLOD = slotDist.name.lods.lod.forceDraw;
      slotDist.passNum = SCHAR_MIN; // explicitly "uninitialized" value
      if (slotDist.forceDrawLOD>=0)
      {
        // count complexity as used
        if (slotDist.forceDrawLOD!=LOD_INVISIBLE)
        {
          // TODO: smart GetComplexity handling
          for (int i=0; i<slotDist.objs.Size(); i++)
          {
            SortObject *so = slotDist.objs[i];
            Object *obj = so->GetObject();
            // one slot may represent multiple objects
            int objsInSlot = so->_listEnd-so->_listBeg;
            complexityUsed += obj->GetComplexity(slotDist.forceDrawLOD,obj->GetFrameBase())*objsInSlot;
          }
          // TODO: PassNum independent of the object
          Object *obj0 = slotDist.objs[0]->GetObject();
          slotDist.passNum = obj0->PassNum(slotDist.forceDrawLOD);
        }
        slotDist.drawLOD = slotDist.forceDrawLOD;
      }
      else
      {
        totalArea += coveredArea*nObjsInSlot;
      }

      // shadow distance test was already done before
      LODShape *shape = slotDist.name.shape;
      if(shape && !(shape->Special() & NoShadow) && (castRainMap || castShadows)) // && oi->distance2<Square(_shadowFogMaxRange)
      {
        // some objects cast two shadows
        float factor = 0;
				#ifdef TEST_RAIN_MAP
					if (slotDist.shadowLOD<LOD_INVISIBLE && ((shape->DrawProjShadow(slotDist.passNum) || (castRainMap && shape->DrawRainMap(slotDist.passNum))))) 
						factor += projShadowAreaCoef;

					if (castRainMap && !castShadows)
						slotDist.shadowVolLOD = LOD_INVISIBLE;
				#else
					if (slotDist.shadowLOD<LOD_INVISIBLE && shape->DrawProjShadow(slotDist.passNum)) 
						factor += projShadowAreaCoef;
				#endif

        if (slotDist.shadowVolLOD<LOD_INVISIBLE && shape->DrawVolumeShadow(slotDist.passNum)) 
					factor += volShadowAreaCoef; // volShadowAreaMulFactor

        totalArea += coveredArea*factor*nObjsInSlot;
        /* following functionality is done in Transport::Draw due to separate z-space
        // for some visual lods we need to force shadow LOD as well
        */
      }
      else
      {
        slotDist.shadowVolLOD = LOD_INVISIBLE;
        slotDist.shadowLOD = LOD_INVISIBLE;
      }
    }
  }

  // if total area is zero, there are no controlled visible objects and density does not matter
  // we have total area, we know how much complexity we want - we may calculate density now
  const int wantedComplexity = _complexityTarget;
  complexityUsed += ComputeTerrainComplexity();

  int complexity = complexityUsed;

  float density = (wantedComplexity-complexity)/floatMax(totalArea,1e-20);

  if (CHECK_DIAG(DEModel) || CHECK_DIAG(DEScene))
  {
    DIAG_MESSAGE(
      200,Format(
        "Complex %d->%d, area %.0f, dens %0.4f (min %0.4f)",
        complexity,wantedComplexity,totalArea,density,minDensity
      )
    );
  }
  saturate(density,minDensity,maxDensity);


#if 0 //_ENABLE_REPORT
  float density0 = density;
  float totalArea0 = totalArea;
#endif
  // note: if too many polygons were already used, density may be negative

  float minArea = totalArea*1e-4f;
  // first pass: select object which use lod 0
  // such objects often do not use available complexity
  // when two-sided stencil is supported, reduce shadow poly estimation 2x
  int volShadowDivLog = GEngine->CanTwoSidedStencil() ? 1 : 0;
  // simulate division by shifting
  //float volShadowAreaMulFactor = (1<<volShadowDivLog);
  //float volShadowAreaFactor = 1.0f/volShadowAreaMulFactor;
  for( int i=0; i<slotCtx.objs.slots.Size(); i++ )
  {
    SortLODShapeSlot &slot = slotCtx.objs.slots[i];
    for (int j=0; j<slot.objects.Size(); j++)
    {
      SortShapeSlotDist &slotDist = slot.objects[j];
      int nObjsInSlot = slotDist.ObjCount();
      if (nObjsInSlot<=0) continue;
      float coveredArea = slotCtx.discretize.MinValue2(slotDist.discSlot);
      Object *obj0 = slotDist.objs[0]->GetObject();
      if (slotDist.drawLOD<0)
      {
        LODShape *lShape = slotDist.name.shape;
        float oComplexity = coveredArea*density;
        int level = lShape->FindLevelWithComplexity(coveredArea,oComplexity,oComplexity*1.5f+200);
        if (level==0)
        {
          int levelComplexity = lShape->Complexity(level);
          #if _ENABLE_CHEATS
          if (abs(oComplexity-levelComplexity)>displayComplexityLimit)
          {
            LogF(
              "%s - complexity %d, wanted %.0f",
              (const char *)lShape->GetName(),levelComplexity,oComplexity
            );
          }
          #endif
          slotDist.passNum = obj0->PassNum(level);
          slotDist.drawLOD = level;
          float coveredArea = slotCtx.discretize.MinValue2(slotDist.discSlot);

          complexity += levelComplexity*nObjsInSlot;
          totalArea -= coveredArea*nObjsInSlot;
        }
      }
      else
      {
        Assert(slotDist.drawLOD==LOD_INVISIBLE || slotDist.drawLOD==slotDist.forceDrawLOD);
      }
      // similar for shadows
      if (slotDist.shadowVolLOD<0)
      {
        LODShape *lShape = slotDist.name.shape;
        // TODO: once shadow volume LOD selection is done, handle best LOD here
        if (lShape->DrawVolumeShadow(slotDist.passNum))
        {
          float oComplexity = coveredArea*density*volShadowAreaCoef;

          // select special lod for pass 3
          int level = -1;
          bool isBest = false;

          if (slotDist.passNum == 3)
          {
            level = obj0->GetPass3ShadowVolumeLevel(slotDist.passNum, camType);
            isBest = true;
          }

          if (level == -1)
          {
            // volume shadow level is currently fixed
            level = lShape->FindShadowVolumeLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
            // check if the best shadow volume level was selected (FindShadowVolume is the best one)
            isBest = level==lShape->FindShadowVolume();
          }

          if (isBest)
          {
            int levelComplexity = lShape->Complexity(level)>>volShadowDivLog;
            complexity += levelComplexity*nObjsInSlot;
            complexityShadows += levelComplexity*nObjsInSlot;
            totalArea -= coveredArea*volShadowAreaCoef*nObjsInSlot;
            slotDist.shadowVolLOD = level;
          }
        }
        else
        {
          slotDist.shadowVolLOD = LOD_INVISIBLE;
        }
      }

      if (slotDist.shadowLOD<0)
      {
        LODShape *lShape = slotDist.name.shape;

				#ifdef TEST_RAIN_MAP
					if (lShape->DrawProjShadow(slotDist.passNum) || (castRainMap && lShape->DrawRainMap(slotDist.passNum)))
				#else
					if (lShape->DrawProjShadow(slotDist.passNum))
				#endif
        {
          float oComplexity = coveredArea*density*projShadowAreaCoef;
          int level = lShape->FindShadowLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
          // check if the best shadow level was selected (GetMinShadowLevel is the best one)
          if (level>=0 && level<=lShape->GetMinShadowLevel())
          {
            //Assert(level==lShape->GetMinShadowLevel());
            int levelComplexity = lShape->Complexity(level);
            slotDist.shadowLOD = level;
            complexity += levelComplexity*nObjsInSlot;
            complexityShadows += levelComplexity*nObjsInSlot;
            totalArea -= coveredArea*projShadowAreaCoef*nObjsInSlot;
          }
        }
	      else
        {
          slotDist.shadowLOD = LOD_INVISIBLE;
        }
      }
    }
  }
  
  // when we rendered almost everything, there is no need to update density any more
  if (totalArea>minArea)
  {
    density = (wantedComplexity-complexity)/floatMax(totalArea,1e-20);
    if (CHECK_DIAG(DEModel) || CHECK_DIAG(DEScene))
    {
      DIAG_MESSAGE(
        200,Format(
          "Compl.2 %d->%d, area %.0f, dens %0.4f",
          complexity,wantedComplexity,totalArea,density
        )
      );
    }
    saturate(density,minDensity,maxDensity);
  }

  // Second pass: selects LODs for all other objects
  for( int i=0; i<slotCtx.objs.slots.Size(); i++ )
  {
    SortLODShapeSlot &slot = slotCtx.objs.slots[i];
    for (int j=0; j<slot.objects.Size(); j++)
    {
      SortShapeSlotDist &slotDist = slot.objects[j];
      int nObjsInSlot = slotDist.ObjCount();
      if (nObjsInSlot<=0) continue;
      float coveredArea = slotCtx.discretize.MinValue2(slotDist.discSlot);
      Object *obj0 = slotDist.objs[0]->GetObject();

      if (slotDist.drawLOD<0)
      {
        LODShape *lShape = slotDist.name.shape;
        float oComplexity = coveredArea*density;
        int level = lShape->FindLevelWithComplexity(coveredArea,oComplexity,oComplexity*1.5f+200);
   
        if (level<0)
          slotDist.drawLOD = LOD_INVISIBLE;
        else
        {
          int levelComplexity = lShape->Complexity(level);
#if _ENABLE_CHEATS
          if (abs(oComplexity-levelComplexity)>displayComplexityLimit)
            LogF("%s - complexity %d, wanted %.0f", (const char *)lShape->GetName(), levelComplexity, oComplexity);
#endif
          complexity += levelComplexity*nObjsInSlot;
          slotDist.passNum = obj0->PassNum(level);
          slotDist.drawLOD = level;
        }
      }
      // handle other shadow volume LODs here
      if (slotDist.shadowVolLOD<0)
      {
        LODShape *lShape = slotDist.name.shape;
        float oComplexity = coveredArea*density*volShadowAreaCoef;
        // unless someone will find some LOD, assume invisible
        slotDist.shadowVolLOD = LOD_INVISIBLE;
        if (lShape->DrawVolumeShadow(slotDist.passNum))
        {
          int level = lShape->FindShadowVolumeLevelWithComplexity(oComplexity,oComplexity*1.5f+200);

          if (level>=0)
          {
            // shadow lod volume area / complexity already considered
            int levelComplexity = lShape->Complexity(level);
#if _ENABLE_CHEATS
            if (abs(oComplexity-levelComplexity)>displayComplexityLimit)
              LogF("%s - complexity %d, wanted %.0f", (const char *)lShape->GetName(), levelComplexity, oComplexity);
#endif
            complexityShadows += levelComplexity*nObjsInSlot;
            complexity += levelComplexity*nObjsInSlot;
            slotDist.shadowVolLOD = level;
          }
        }
      }
      if (slotDist.shadowLOD<0)
      {
        LODShape *lShape = slotDist.name.shape;
        float oComplexity = coveredArea*density*projShadowAreaCoef;
        // unless someone will find some LOD, assume invisible
        slotDist.shadowLOD = LOD_INVISIBLE;

				#ifdef TEST_RAIN_MAP
					if (lShape->DrawProjShadow(slotDist.passNum) || (castRainMap || lShape->DrawRainMap(slotDist.passNum)))
				#else
					if (lShape->DrawProjShadow(slotDist.passNum))
				#endif
        {
          int level = lShape->FindShadowLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
          if (level>=0)
          {
            int levelComplexity = lShape->Complexity(level);
#if _ENABLE_CHEATS
            if (abs(oComplexity-levelComplexity)>displayComplexityLimit)
              LogF("%s - complexity %d, wanted %.0f", (const char *)lShape->GetName(), levelComplexity, oComplexity);
#endif
            complexityShadows += levelComplexity*nObjsInSlot;
            complexity += levelComplexity*nObjsInSlot;
            slotDist.shadowLOD = level;
          }
        }
      }
    }
  }

  if (CHECK_DIAG(DEModel) || CHECK_DIAG(DEScene))
  {
    DIAG_MESSAGE(
      200,Format(
        "Compl.T %d->%d, area %.0f, dens %0.4f",
        complexity,wantedComplexity,totalArea,density
      )
    );
  }

  // Remember the density for later use
  _density = density;
    
  // scene polygons are distributed, but there may be some proxies left
  // we can assume proxies will not use more than a few screens
  float proxyArea = _engineArea*4.0f;
  float densityLeft = (wantedComplexity-complexity)/proxyArea;
  if (densityLeft>density)
  {
    // we have the polygons, there is no reason not to use them
    density = densityLeft;
    saturate(density,minDensity,maxDensity);
  }
  
  // Make sure there is some minimal density
  _drawDensity = floatMax(density, minDensity);
  
  ADD_COUNTER(compX,complexity);

  // store complexity to complexity history
  for (int i=1; i<NStoreComplexities; i++)
  {
    _lastComplexity[i-1] = _lastComplexity[i];
  }
  _lastComplexity[NStoreComplexities-1] = complexity;

  #if 0 //_ENABLE_REPORT
    float avgComplexity = 0;
    for (int i=0; i<NStoreComplexities; i++)
    {
      avgComplexity += _lastComplexity[i];
    }
    avgComplexity *= 1.0f/NStoreComplexities;
    GlobalShowMessage(
      500,
      "C %6d (%6d,%6d), d %0.3f, a %0.f",
      complexity,complexityShadows,complexityUsed,density,totalArea
    );
  #endif
  #if LOG_LODS
  if (LogLodsCounter>0)
  {
    LogF("Frame capture %s [[[[[[[[[",frameRepeated ? "Repeat" : "");
    for( int i=0; i<_drawObjects.Size(); i++ )
    {
      SortObject *oi=_drawObjects[i];
      float area = oi->coveredArea;
      int complexityO = DisplayComplex(oi->shape,oi->drawLOD);
      int complexityS = DisplayComplex(oi->shape,oi->shadowLOD);
      // calculate density
      LogF(
        "  %40s: %d,%d - dist %.0f, area %.0f (complex %d,%d -> %.3f, %.3f)",
        (const char *)oi->shape->GetName(),
        oi->drawLOD,oi->shadowLOD,sqrt(oi->distance2),area,
        complexityO,complexityS,complexityO/area,complexityS/area
      );
    }  
    LogF("]]]]]]]]");
    LogLodsCounter--;
  }
  #endif
  


  // we have all object levels selected - we can scan proxies
  // the _drawObjects will grow during the loop
  int nObjects = _drawObjects.Size();

  // each slot must have a corresponding objects
  DoAssert(slotCtx.objs.slots.Size()<=nObjects);

  for( int i=0; i<slotCtx.objs.slots.Size(); i++ )
  {
    SortLODShapeSlot &slot = slotCtx.objs.slots[i];
    for (int j=0; j<slot.objects.Size(); j++)
    {
      SortShapeSlotDist &slotDist = slot.objects[j];
      int nObjs = slotDist.objs.Size();
      if (nObjs<=0) continue;
      
      if (slotDist.drawLOD==LOD_INVISIBLE) continue;

      // TODO: smart handling of NeedsPrepareProxiesForDrawing
      // TODO: once common UseLevel
      for (int o=0; o<nObjs; o++)
      {
        SortObject *so = slotDist.objs[o];
        Object *obj = so->GetObject();
        // walk through proxy hierarchy - which level?
        if (!obj->NeedsPrepareProxiesForDrawing())
        {
          // UseLevel can be quite slow - skip it unless really needed
          continue;
        }
        DoAssert(!so->_list);
        int level = slotDist.name.shape->UseLevel(slotDist.drawLOD,false);
        if (level>=0)
        {
          DoAssert(slotCtx.objs.slots.Size()<=_drawObjects.Size());
          // if in list, we need to traverse all objects?
          // we need to call animate to make sure selections are hidden
          // we use setEngineStuff=true to avoid any bone animation
          Object::ProtectedVisualState<const ObjectVisualState> vs = obj->RenderVisualStateScope();
          ShapeUsed lock = slotDist.name.shape->Level(level);
          AnimationContextStorageRender storage;
          AnimationContext animContext(vs,storage);
          lock->InitAnimationContext(animContext, slotDist.name.shape->GetConvexComponents(level), false);
          obj->Animate(animContext,level,true,obj,so->distance2);
          obj->PrepareProxiesForDrawing(obj, level, animContext, so->position, level, so->orClip, so);
          obj->Deanimate(level,true);
        }
      }
    }

  }
  
  // new from proxy - divide to slots, select LODs
  // we need to divide everything between nObjects and _drawObjects.Size()

  SplitObjectsBySlots(slotCtx,slotCtx.proxies,_drawObjects.Data()+nObjects,_drawObjects.Size()-nObjects);
  
  // TODO: PrepareProxiesForDrawing should not perform LOD selection
  
  // select LODs on new (proxy) slots, but this time very simple
  for( int i=0; i<slotCtx.proxies.slots.Size(); i++ )
  {
    SortLODShapeSlot &slot = slotCtx.proxies.slots[i];
    for (int j=0; j<slot.objects.Size(); j++)
    {
      SortShapeSlotDist &slotDist = slot.objects[j];
      int nObjsInSlot = slotDist.ObjCount();
      if (nObjsInSlot<=0) continue;
      
      float coveredArea = slotCtx.discretize.MinValue2(slotDist.discSlot);
      if (slotDist.name.lods.lod.forceDraw >= 0)
      {
        // HOTFIX: avoid LOD selection when LOD forced
        slotDist.drawLOD = slotDist.name.lods.lod.forceDraw;
      }
      else
      {
      float oComplexity = coveredArea*_drawDensity;
        // convert density to LOD
        slotDist.drawLOD = slotDist.name.shape->FindLevelWithComplexity(coveredArea,oComplexity,oComplexity*1.5f+200);
      }
      
      // we need to select shadow LODs as well unless prohibited
      if (slotDist.name.lods.lod.shadowVol<0)
      {
        // TODO: avoid code replication, see above
        LODShape *lShape = slotDist.name.shape;
        // TODO: once shadow volume LOD selection is done, handle best LOD here
        if (lShape->DrawVolumeShadow(slotDist.passNum) && GetObjectShadows())
        {
          float oComplexity = coveredArea*density*volShadowAreaCoef;
          // volume shadow level is currently fixed
          slotDist.shadowVolLOD = lShape->FindShadowVolumeLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
        }
        else
          slotDist.shadowVolLOD = LOD_INVISIBLE;
      }
      else
        slotDist.shadowVolLOD = slotDist.name.lods.lod.shadowVol;
      
      if (slotDist.name.lods.lod.shadow<0)
      {
        LODShape *lShape = slotDist.name.shape;
				#ifdef TEST_RAIN_MAP
					if ((lShape->DrawProjShadow(slotDist.passNum) && GetObjectShadows()) || (castRainMap && lShape->DrawRainMap(slotDist.passNum)))
				#else
				  if (lShape->DrawProjShadow(slotDist.passNum) && GetObjectShadows())
				#endif
        {
          float oComplexity = coveredArea*density*projShadowAreaCoef;
          slotDist.shadowLOD = lShape->FindShadowLevelWithComplexity(oComplexity,oComplexity*1.5f+200);
          if (slotDist.shadowLOD<0)
            slotDist.shadowLOD = LOD_INVISIBLE;    
        }
        else
				{
					slotDist.shadowLOD = LOD_INVISIBLE;
				}
      }
      else
        slotDist.shadowLOD = slotDist.name.lods.lod.shadow;

      if (slotDist.drawLOD!=LOD_INVISIBLE)
      {
        // TODO: PassNum independent of the object - property of Type / LODShape only
        SortObject *so0 = slotDist.objs[0];
        Object *obj0 = so0->GetObject();
        Object *root0 = so0->GetRootObject();
        slotDist.passNum = obj0->PassNum(slotDist.drawLOD);
        // if not alpha object, try to keep the same pass as the root object
        // if the root is pass 3, all proxies need to be the same pass
        if (slotDist.passNum != 2 && root0 != obj0)
        {
          int rootPass = root0->PassNum(so0->rootLOD);
          if (rootPass==3)
          {
            slotDist.passNum = rootPass;
          }
          else
          {
            // if root is not pass 3, we must not be pass 3
            Assert(slotDist.passNum!=3);
          }
        }
      }
    }
  }
}

float Scene::GetSunClosestDistanceFrom0(DrawContext &ctx, int layerIndex)
{
  // Get the shadow direction
	#ifdef TEST_RAIN_MAP
		bool rainMap = layerIndex == RAIN_MAP_FAKE_LAYER;
		Vector3Val shadowDirection = rainMap ? Vector3(0, -1, 0) : MainLight()->ShadowDirection();

		const SortObjectList<DrawContext::MaxSceneObjects*1/16>& objList = rainMap ? ctx._drawRainMap : ctx._drawShadowProj;
	#else
		bool rainMap = false;
		Vector3Val shadowDirection = MainLight()->ShadowDirection();

		const SortObjectList<DrawContext::MaxSceneObjects*1/16>& objList = ctx._drawShadowProj;
	#endif

	// Calculate distance of the plane closest to the sun from point (0, 0, 0)
  float minSunDistance = FLT_MAX;
  const SortObject *oiClosest = NULL;

  for (int i = 0; i < objList.Size(); i++)
  {
    const SortObject *oi = objList[i];

    // If objects is not to be drawn, we need not to consider it
    if (oi->shadowLOD == LOD_INVISIBLE) continue;

    // Skip objects whose shadows do not lie in the specified CSM layer
    if (!rainMap && !(oi->csmLayerReached & (1 << layerIndex))) continue;

    // Get the distance of the point from plane with normal dir going through the (0, 0, 0)
    Vector3 pos = oi->position.PositionWorld(*GetCamera());
    float d = pos.DotProduct(shadowDirection);

    // Remember the smaller distance
    if (d < minSunDistance)
    {
      minSunDistance = d;
      oiClosest = oi;
    }
  }

  // Include object diameter
  if (oiClosest) minSunDistance -= oiClosest->radius;

  // Return minSunDistance - no matter it can be FLT_MAX
  return minSunDistance;
}

#include "fixed.hpp"
#include "El/DebugWindow/debugWin.hpp"




typedef unsigned char OccZType;

/** adapted from a former "Occlusion" class */

class SceneCoverage 
{
	#if _ENABLE_CHEATS
	static Link<DebugMemWindow> _debugWin;
	#endif

	SRefArray<OccZType> _data;
	int _w,_h;
	float _w2,_h2; // precalculate w/2, h/2

	// note - ordered by column - accessed and rendered by column
	OccZType Get( int x, int y ) const {return _data[x*_h+y];}
	OccZType &Set( int x, int y ) {return _data[x*_h+y];}
	
	__forceinline OccZType &operator () ( int x, int y ) {return Set(x,y);}
	__forceinline OccZType operator () ( int x, int y ) const {return Get(x,y);}

	public:
  struct PixelCoverage
  {
    static const int CountLayers = 6;

    /// pixels rendered (passed depth test)
    int covered[CountLayers];
    /// pixels total
    int total;

    #pragma warning(suppress:4351) // default initialization of covered() is intended
    PixelCoverage():covered(),total(0){}

    void operator += (const PixelCoverage &src)
    {
      for (int i=0; i<CountLayers; i++) covered[i] += src.covered[i];
      total += src.total;
    }
  };

	SceneCoverage( int w, int h );
	~SceneCoverage();

	// project, clip and test bbox
	PixelCoverage RenderBBox(const Matrix4 &trans, const Vector3 *minMax, ClipFlags clip);

	void DebugPoly( const LogicalPoly &poly, DebugPixel color ) const;

	// horizontal fill
	void RenderSpan( OccZType *tgt, int yBeg, int width, OccZType z, PixelCoverage &coverage );

	// predefined callbacks
	PixelCoverage RenderProjectedPoly( const LogicalPoly &poly);
	PixelCoverage RenderPoly( const LogicalPoly &poly, ClipFlags clip);

	void Clear();

	void OutputDebug();
};

#if _ENABLE_CHEATS
Link<DebugMemWindow> SceneCoverage::_debugWin;
#endif

SceneCoverage::SceneCoverage( int w, int h )
:_w(w),_h(h),_w2(w*0.5),_h2(h*0.5)
{
  _data.Realloc(w*h);
  Clear();
}
SceneCoverage::~SceneCoverage()
{
  #if _ENABLE_CHEATS
    if( GInput.GetCheat3ToDo(DIK_N) )
    {
      if( !_debugWin ) _debugWin=new DebugMemWindow("Occlusions",_w,_h);
    }
    if( _debugWin )
    {
      OutputDebug(); // draw z-buffer to background
      _debugWin->Update(); // send old frame data
      // reset output to black
      for( int x=0; x<_w; x++ ) for( int y=0; y<_h; y++ )
      {
        _debugWin->Set(x,y)=0;
      }

    }
  #endif
}

void SceneCoverage::Clear()
{
  for (int i=0; i<_w*_h; i++)
  {
    _data[i] = 0;
  }
}

void SceneCoverage::OutputDebug()
{
  #if _ENABLE_CHEATS
  DebugMemWindow *window = _debugWin;
  static const Color layerColor[]={Color(0,0,0),Color(0.5,0.5,0.5),Color(0.5,0.5,1),Color(0.5,1,0.5),Color(1,1,0.5),Color(1,0.5,0.5),Color(0,1,0)}; // last color is used on overflow
  DebugPixel layerColorD[PixelCoverage::CountLayers+1];
  COMPILETIME_COMPARE(lenof(layerColor),PixelCoverage::CountLayers+1);
  for (int i=0; i<lenof(layerColorD); i++)
  {
    layerColorD[i] = window->DColor(layerColor[i]);
  }
  for( int x=0; x<_w; x++ ) for( int y=0; y<_h; y++ )
  {
    int coverage = Get(x,y);

    // draw only if pixel is black (i.e. no other diagnostics there)
    if( window->Get(x,y)==0 )
    {
      saturate(coverage,0,PixelCoverage::CountLayers);
      window->Set(x,y) = layerColorD[coverage];
    }
  }
  #endif
}




void SceneCoverage::RenderSpan(OccZType *tgt, int yBeg, int height, OccZType z, PixelCoverage &coverage)
{
  coverage.total += height;
  // perform horizontal fill
  // note: this could be well accomplished with SIMD2 instructions
  tgt+=yBeg;
  //int covered[PixelCoverage::CountLayers] = {}; // zero initialize whole array
  while( --height>=0 )
  {
    int layers = *tgt;
    Assert(layers>=0 && layers<PixelCoverage::CountLayers);
    coverage.covered[layers]++;
    int incTgt = *tgt; // note: needs to be atomic if parallelized
    if (incTgt<PixelCoverage::CountLayers-1)
      incTgt++;
    Assert(incTgt<PixelCoverage::CountLayers);
    *tgt = incTgt;
    
    tgt++;
  }
}

class HPoint
{
  public:
  // TODO: use SSE for HPoint representation
  Fixed16p16 x,y;

  HPoint(){} // default constructors - uninitialized polygon data
  HPoint( float xx, float yy )
  {
    x=fixed(xx);
    y=fixed(yy);
  } 
};

inline float Invert( Fixed16p16 a )
{
  if( a!=Fixed16p160 ) return 1.0f/fxToFloat(a);
  return 0;
}

#define CalcZHDelta(beg,end,invDist) fixed( fxToFloat(end-beg)*invDist )

SceneCoverage::PixelCoverage SceneCoverage::RenderProjectedPoly( const LogicalPoly &poly)
{
  PROFILE_SCOPE_DETAIL_EX(occRP,occ);
  PixelCoverage coverage;

  int n = poly.N();
  if (n<3) return coverage;
  // render poly to occlusion z-buffer

  // search for leftmost vertex
  // scan for furthest (max.) z

  #define HP(v) HPoint((v)->X()*_w2+_w2,(v)->Y()*_h2+_h2);

  float farZ = 0;
  int leftI = 0;
  float leftX = 1e10;
  //LogF("Vertices: %d",n);
  for (int i=0; i<n; i++)
  {
    Vector3Val v = poly[i];

    HPoint vp=HP(&poly[i]);
    
    saturateMax(farZ,v.Z());
    if (leftX>v.X()) leftX = v.X(), leftI = i;
    //LogF("  %d: %.2f,%.2f",i,fxToFloat(vp.x),fxToFloat(vp.y));
  }

  //int tCur = leftI, bCur = leftI;
  int tNxt = leftI-1, bNxt = leftI+1; // top and bottom current point
  if (tNxt<0) tNxt = n-1;
  if (bNxt>=n) bNxt = 0;


  // minZ is minimal z
  // 0 is far
  OccZType minZOcc = 0; // z does not matter, we always increment


  HPoint tp=HP(&poly[leftI]);
  HPoint bp=tp;

  //LogF("LFT %d, %.2f,%.2f",leftI,fxToFloat(tp.x),fxToFloat(tp.y));

  // calculate next point (top and bottom)

  HPoint tnp=HP(&poly[tNxt]);
  HPoint bnp=HP(&poly[bNxt]);
  
  // calculate invariant delta Y

  Fixed16p16 yT,dxT(Fixed16p160); // actual positions
  Fixed16p16 yB,dxB(Fixed16p160);
  

  // L R - does not mean left or right in terms of x coord
  // rather in clockwise/counterclockwise sense

  int curC = fxToIntCeil(tp.x);

  //LogF("TNP %d, %.2f,%.2f",tNxt,fxToFloat(tnp.x),fxToFloat(tnp.y));
  //LogF("BNP %d, %.2f,%.2f",bNxt,fxToFloat(bnp.x),fxToFloat(bnp.y));

  OccZType *tgt=&Set(curC,0);
  
  int vertRest = n-2;

  bool recalcT = true, recalcB = true;
  while(vertRest>=0)
  {
    //LogF("vertRest %d",vertRest);
    // process next vertex
    int nxtCT = fxToIntCeil(tnp.x);
    int nxtCB = fxToIntCeil(bnp.x);
    if (nxtCT>curC && recalcT)
    {
      recalcT = false;
      Fixed16p16 skip = fixed(curC)-tp.x;
      // some drawing will be done based on yR,zR - calculate correct deltas
      float invD = Invert(tnp.x-tp.x);
      dxT = CalcZHDelta(tp.y,tnp.y,invD);
      // correct difference between curL and aktX
      yT = tp.y+skip*dxT;
      //LogF("next dxT %.3f",fxToFloat(dxT));
    }
    
    if (nxtCB>curC && recalcB)
    {
      recalcB = false;
      Fixed16p16 skip = fixed(curC)-bp.x;
      // some drawing will be done based on yL,zL - calculate correct deltas
      float invD = Invert(bnp.x-bp.x);
      dxB=CalcZHDelta(bp.y,bnp.y,invD);
      // correct difference between curL and aktX
      yB = bp.y + skip*dxB;
      //LogF("next dxB %.3f",fxToFloat(dxB));
    }
    
    // calculate how much can be processed without recalculating dx
    int restT = nxtCT-curC;
    int restB = nxtCB-curC;
    int restTotal = _w-curC;

    // calculate how many rest can be simply drawn without any tests
    int restSure = restT;
    if( restSure>restB ) restSure = restB;
    if( restSure>restTotal ) restSure = restTotal;

    for( int cnt=restSure; --cnt>=0; )
    {
      if( curC>=0 )
      {
        int beg=fxToIntCeil(yB);
        int end=fxToIntCeil(yT);

        //int beg=fxToIntCeil(yT);
        //int end=fxToIntCeil(yB);

        //LogF("%d: Span %d..%d",curC,beg,end);

        // perform simple clipping (for a few pixels)
        if( beg<0 ) beg=0;
        if( end>_h ) end=_h;
        
        if( end>beg )
        {
          // correct difference between xBeg and yL

          // perform linear interpolated fill
          RenderSpan(tgt,beg,end-beg,minZOcc,coverage);
        }
  
      }
      // advance to next line
      curC++;
      tgt += _h;
      yT += dxT;
      yB += dxB;
    }
    restT -= restSure;
    restB -= restSure;
    restTotal -= restSure;
    
    #if OPTIMIZE_FOR_MMX
    _mm_empty();
    #endif

    if( restTotal<=0 ) break; // bottom end reached - nothing to draw     
    if( restT==0 )
    {
      // top edge done - move to next point
      tp = tnp;

      tNxt--;if (tNxt<0) tNxt = n-1;
      vertRest--;

      tnp=HP(&poly[tNxt]);
      //LogF("TNP next %d, %.2f,%.2f",tNxt,fxToFloat(tnp.x),fxToFloat(tnp.y));
      recalcT = true;
    }
    if( restB==0 )
    {
      // right edge done - move to next point
      bp = bnp;

      bNxt++;if (bNxt>=n) bNxt = 0;
      vertRest--;

      bnp=HP(&poly[bNxt]);
      //LogF("BNP next %d, %.2f,%.2f",bNxt,fxToFloat(bnp.x),fxToFloat(bnp.y));
      recalcB = true;
    }
  }
  return coverage;
}

static inline void Perspective(Vector3 &d)
{
  float invZ=1/d.Z();
  d[0]*=invZ;
  d[1]*=invZ;
}

static void Perspective(LogicalPoly &poly)
{
  for( int i=0; i<poly.N(); i++ )
  {
    Vector3 &d=poly.Set(i);
    Perspective(d);
  }
}

SceneCoverage::PixelCoverage SceneCoverage::RenderPoly( const LogicalPoly &poly, ClipFlags clip)
{
  PROFILE_SCOPE_DETAIL_EX(occRR,occ);
  // clip polygon
  LogicalPoly temp=poly;
  // check clipping
  if( clip )
  {
    float cNear = 0.01f;
    float cFar = GScene->GetCamera()->ClipFar();
    temp.Clip(cNear,cFar,clip);
    if( temp.N()<3 ) return PixelCoverage();
  }


  Perspective(temp);

  return RenderProjectedPoly(temp);  
}


SceneCoverage::PixelCoverage SceneCoverage::RenderBBox(const Matrix4 &trans, const Vector3 *minMax, ClipFlags clip)
{
  PROFILE_SCOPE_DETAIL_EX(occBB,occ);
  PixelCoverage coverage;

  // back face cull - render only faces facing the camera

  // transform all corners into view-space
  Matrix4 modelToView = GScene->ScaledInvTransform()*trans;

  Vector3 corners[2][2][2];
  {
    PROFILE_SCOPE_DETAIL_EX(occBT,occ);
    for (int z=0; z<2; z++) for (int y=0; y<2; y++) for (int x=0; x<2; x++)
    {
      Vector3 modelCorner(minMax[x].X(),minMax[y].Y(),minMax[z].Z());
      corners[z][y][x] = modelToView * modelCorner;
    }
  }

  // for each face: backface cull, clip, project
  static const int faces[6][4][3] = {
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,1,0},{0,1,1},{1,1,1},{1,1,0}},

    {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},

    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}},
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
  };
  static const Vector3 normals[6] = {
    Vector3(0,-1,0), // top
    Vector3(0,+1,0), // bottom

    Vector3(0,0,-1), // front
    Vector3(0,0,+1), // back

    Vector3(-1,0,0), // left
    Vector3(+1,0,0), // right
  };
  // each face goes through minmax[0] or minmax[1] - determine which one
  static const int mCenters[6] = {0,1,0,1,0,1};

  // compute the backface cull, for each face compute dot product of face normal against face position (all in camera space)
  bool visible[6];
  Matrix4 camInvTrans = GScene->GetCamera()->GetInvTransform();
  for (int f=0; f<6; f++)
  {
    float cull = camInvTrans.Rotate(normals[f])*camInvTrans.FastTransform(minMax[mCenters[f]]);
    visible[f] = cull>0;
  }

  if (clip==ClipNone)
  { // optimize a common case of no clipping - common perspective
    for (int z=0; z<2; z++) for (int y=0; y<2; y++) for (int x=0; x<2; x++)
    {
      Perspective(corners[z][y][x]);
    }
    for (int f=0; f<lenof(faces); f++) if (visible[f])
    {
      LogicalPoly face;
      for (int v=0; v<lenof(faces[f]); v++)
      {
        face.Add(corners[faces[f][v][2]][faces[f][v][1]][faces[f][v][0]]);
      }
      coverage += RenderProjectedPoly(face);
    }
  }
  else
  {
    for (int f=0; f<lenof(faces); f++) if (visible[f])
    {
      LogicalPoly face;
      for (int v=0; v<lenof(faces[f]); v++)
      {
        face.Add(corners[faces[f][v][2]][faces[f][v][1]][faces[f][v][0]]);
      }
      coverage += RenderPoly(face,clip);
    }
  }

  return coverage;
}


const int FarLodsDefault = 2;
#if _ENABLE_CHEATS
  static int FarLods = FarLodsDefault;
#else
  const int FarLods = FarLodsDefault;
#endif

/* complexity management should not be done when exact frame needs to be drawn again
Primary reason for this is PIX Xbox analyzer.
*/
struct SortByZ
{
  int operator () (SortObject **s1, SortObject **s2) const
  {
    return sign((*s1)->zCoord-(*s2)->zCoord);
  }
};

void Scene::DrawPreparation(DrawContext &ctx, float deltaT, bool frameRepeated)
{
  PROFILE_SCOPE_GRF_EX(oPrep,*,SCOPE_COLOR_YELLOW);

  _prepareId++;

  // prepare texture memory if necessary
  GEngine->TextBank()->AllocateTextureMemory();
  FreeOnDemandGarbageCollect(1024*1024,256*1024);

#if _ENABLE_CHEATS
  GDiagsShown.Render();
#endif
  // Move debugging arrows to the object list
  for( int i=0; i<_arrows.Size(); i++ )
  {
    ObjectForDrawing(_arrows[i]);
  }
  _arrows.Clear(); // Arrows are cleared now

  // remove all objects that should not be used
  int s=0,t=0;
  //LogF("Before not used %d",_drawObjects.Size());
  int currFrame = FreeOnDemandFrameID();

  #if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_0))
  {
    FarLods++;
    if (FarLods>3) FarLods = 0;
    DIAG_MESSAGE(1000,"FarLods %d",FarLods);
  }
  #endif

  ObjectForDrawingContext areaCtx;
  // TODO: use MemAllocDataStack
  AutoArray<SortObject *, MemAllocDataStack<SortObject*,1024> > farLODObjects;
  if (FarLods)
    farLODObjects.Realloc(_drawObjects.Size());
  {
  PROFILE_SCOPE_EX(obCull,drw);
  for( ; s<_drawObjects.Size(); s++ )
  {
    // TODO: optimize
    SortObject *sObj=_drawObjects[s];
    // if the object was not used in the last frame, we may delete it. Otherwise we initialize it
    if( sObj->_usedInFrame==currFrame )
    {
      if( s!=t ) _drawObjects[t]=sObj;
      t++;
      // reset the root index
      sObj->_rootIndex = -1;
      sObj->_nItems = 0;
      if (FarLods)
      {
        if (sObj->forceDrawLOD<0)
        {
          farLODObjects.Add(sObj);
          areaCtx.managedArea += sObj->coveredArea;
    }
        else if (sObj->forceDrawLOD!=LOD_INVISIBLE)
        {
          // this may by worst LOD or explicit LOD - do we care about the difference?
          areaCtx.worstLODComplexity += sObj->shape->Complexity(sObj->forceDrawLOD);
          areaCtx.worstLODArea += sObj->coveredArea;
        }
      }
    }
    else
    {
      // if object is back-linked, remove it from the list
      Object *obj = sObj->GetObject();
      if (obj)
      {
#if _HELISIM_RENDER_TO_TEXTURE
        if (obj->GetInList(_drawObjectsId)==sObj)
#else
        if (obj->GetInList()==sObj)
#endif
        {
#if _HELISIM_RENDER_TO_TEXTURE
          obj->SetInList(NULL, _drawObjectsId);
#else
          obj->SetInList(NULL);
#endif
    }
        else
        {
          #if _ENABLE_REPORT && defined _WIN32
#if _HELISIM_RENDER_TO_TEXTURE
          if (obj->GetInList(_drawObjectsId)!=NULL)
#else
            if (obj->GetInList()!=NULL)
#endif
            {
              //obj->SetInList(NULL);
              __asm nop
  }
          #endif
        }
      }
    }
  }
  _drawObjects.Resize(t);
  }
  //LogF("After not used %d",_drawObjects.Size());




  if (FarLods)
  {
    PROFILE_SCOPE_EX(oFLod,*);
    // sometimes there is no need for any farlods management, as there is enough polygons for whole scene
    // how can we detect this?
    // sort "farLOD" objects by distance
    QSort(farLODObjects,SortByZ());

    const float minAreaFactor = 0.01f;
    // for all objects rasterize their bounding boxes
    if (FarLods>=2)
    {
      SceneCoverage coverage(64,32); // TODO: adjust to FOV
      int terrainComplexity = ComputeTerrainComplexity();
      int givenComplexity = terrainComplexity+areaCtx.worstLODComplexity;
      int maxComplexity = 0;
      int minComplexity = 0;
      float controlledArea = 0;
      for (int i=0; i<farLODObjects.Size(); i++)
      {
        SortObject *so = farLODObjects[i];
        Object *obj = so->GetObject();
        if (!obj->Static()) // ignore moving objects, so that their movement does not affect the scene
          continue;
        LODShape *shape = so->shape;
        Vector3 minmax[2];
        obj->ClippingInfo(obj->RenderVisualState(),minmax,Object::ClipVisual);
        SceneCoverage::PixelCoverage pixels = coverage.RenderBBox(obj->GetRenderFrameBase(),minmax,so->orClip); // RenderVisualState
        // analyze the coverage, when covered by only a few layers, reduce the assumed covered area only a little bit
        static const float coverageWeight[]={0,0,0.1,0.3,0.6,1.0};
        COMPILETIME_COMPARE(lenof(coverageWeight),SceneCoverage::PixelCoverage::CountLayers);
        float sumCovered = 0;
        if (pixels.total>0) for (int i=0; i<SceneCoverage::PixelCoverage::CountLayers; i++)
        {
          float layerCovered = pixels.covered[i]*coverageWeight[i]/float(pixels.total);
          sumCovered += layerCovered;
        }

        if (FarLods>=3 && sumCovered>=0.99)
        {
          so->forceDrawLOD = LOD_INVISIBLE;
        }
        else
        {
          float lodFactor = InterpolativC(sumCovered,0.7f,0.9f,1.0f,minAreaFactor);
          so->_visibleRatio = lodFactor;
          if (lodFactor<1)
          {
            if (shape->FindSimplestLevel()>=0)
            {
              float wantedComplexity = so->coveredArea*MaxDensity;
              int maxComplexityShape = shape->Complexity(shape->FindLevelWithComplexity(so->coveredArea,wantedComplexity,wantedComplexity*8));
              int minComplexityShape = shape->Complexity(shape->FindLevelWithComplexity(so->coveredArea,wantedComplexity*minAreaFactor,wantedComplexity*8));
              //int minComplexityShape = shape->Complexity(shape->FindSimplestLevel());
              maxComplexity += maxComplexityShape;
              minComplexity += minComplexityShape;
              controlledArea += so->coveredArea;
            }
          }
          else 
          {
            if (shape->FindSimplestLevel()>=0)
            {
              float wantedComplexity = so->coveredArea*MaxDensity;
              givenComplexity += shape->Complexity(shape->FindLevelWithComplexity(so->coveredArea,wantedComplexity,wantedComplexity*8));
            }
          }
        }
      }

      int tgtComplexity = _complexityTarget-givenComplexity;
      float factor = 1.0f;
      if (maxComplexity>tgtComplexity)
      {
        factor = Interpolativ(tgtComplexity,minComplexity,maxComplexity,0.0f,1.0f);
        for (int i=0; i<farLODObjects.Size(); i++)
        {
          SortObject *so = farLODObjects[i];
          so->coveredArea *= Lerp(so->_visibleRatio,1.0,factor);
        }
      }
      if (CHECK_DIAG(DEModel) || CHECK_DIAG(DEScene))
      {
        DIAG_MESSAGE(
          1000,"Complexity range %d..%d, target %.0f, factor %.3f",
          givenComplexity+minComplexity,givenComplexity+maxComplexity,_complexityTarget,factor
        );
      }
    }
    else
    {

      int terrainComplexity = ComputeTerrainComplexity();
      // compute maximum possible complexity (when each object is displayed at full density)
      // compute minimum possible complexity (when each object is displayed at worst LOD)
      // the solution will lie somewhere in between
      int maxComplexity = 0;
      int minComplexity = 0;
      float controlledArea = 0;
      for (int i=0; i<farLODObjects.Size(); i++)
      {
        SortObject *so = farLODObjects[i];
        LODShape *shape = so->shape;
        if (shape->FindSimplestLevel()<0) continue;
        float wantedComplexity = so->coveredArea*MaxDensity;
        int maxComplexityShape = shape->Complexity(shape->FindLevelWithComplexity(so->coveredArea,wantedComplexity,wantedComplexity*8));
        int minComplexityShape = shape->Complexity(shape->FindSimplestLevel());
        Assert(maxComplexityShape>=minComplexityShape);
        maxComplexity += maxComplexityShape;
        minComplexity += minComplexityShape;
        controlledArea += so->coveredArea;
      }

      // average density achieved when all objects are selected in the lod as needed by density (should be close to MaxDensity)
      float maxCDensity = controlledArea>0 ? maxComplexity/controlledArea : MaxDensity;

      float minCDensity = controlledArea>0 ? minComplexity/controlledArea : MaxDensity; // average density achieved when all objects are selected in the worst lod
      float farDensity = MaxDensity;
      float minDist = 0;
      float maxDist = Glob.config.objectsZ;
      int tgtComplexity = _complexityTarget-terrainComplexity-areaCtx.worstLODComplexity;
      if (maxComplexity>tgtComplexity && farLODObjects.Size()>=2)
      {
        minDist = sqrt(farLODObjects[0]->distance2);
        maxDist = sqrt(farLODObjects.Last()->distance2);
        // we want to reach the tgtComplexity

        farDensity = tgtComplexity/controlledArea; // inaccurate, but simple: assume we will use the farDensity everywhere
//         else if (FarLods==4)
//         {
//           // assume polygon distribution is linear with distance
//           float relDensity = Interpolativ(tgtComplexity,minComplexity,maxComplexity,0,1);
//           farDensity = (2*relDensity-1)*(MaxDensity-minCDensity)+minCDensity;
//         }

        // gradual interpolation: full density near, no density far

        for (int i=0; i<farLODObjects.Size(); i++)
        {
          float densityI = InterpolativC(sqrt(farLODObjects[i]->distance2),minDist,maxDist,MaxDensity,farDensity);
          saturate(densityI,minCDensity,MaxDensity);
          float ratio2 = floatMinMax(densityI*(1.0f/MaxDensity),minAreaFactor,1.0f);
          farLODObjects[i]->_visibleRatio = ratio2;
          farLODObjects[i]->coveredArea *= ratio2;
        }
      }
      if (CHECK_DIAG(DEModel) || CHECK_DIAG(DEScene))
      {
        DIAG_MESSAGE(
          1000,"farLODObjects %d, %.1f..%.1f, managed area %.2f, worst area %.2f, worst complexity %d",
          farLODObjects.Size(),minDist,maxDist,
          areaCtx.managedArea,areaCtx.worstLODArea,areaCtx.worstLODComplexity
        );
        DIAG_MESSAGE(
          1000,"Complexity range %d..%d, target %.0f, density range %.3f..%.3f, far density %.3f (%.3f)",
          minComplexity+areaCtx.worstLODComplexity+terrainComplexity,maxComplexity+areaCtx.worstLODComplexity+terrainComplexity,_complexityTarget,maxCDensity,minCDensity,
          farDensity,controlledArea >0 ? tgtComplexity/controlledArea : 0.0f
        );
      }
    }
  }


  SlotObjectsContext sortCtx(1.5f,1,NDiscSlots);

  AdjustComplexity(ctx,sortCtx,frameRepeated);

  // do not copy objects that are not drawn
  {
    if ((ctx._drawPass1ON._original.MaxSize()==0) && (ctx._drawPass1ON._blending.MaxSize()==0))
    {
      // first initialization
      // we can assume 1/4 of the _drawObjects size will be needed for both lists
      // avoid frequent allocation on demand
      int objNeed = _drawObjects.Size();
      ctx._drawPass1ON._original.Reserve(objNeed*1/16);
      ctx._drawPass1ON._blending.Reserve(objNeed*1/16);
      ctx._drawPass1OF._original.Reserve(objNeed*3/16);
      ctx._drawPass1OF._blending.Reserve(objNeed*3/16);
      ctx._drawPass1AN.Reserve(objNeed*1/32);
      ctx._drawPass1AF.Reserve(objNeed*3/32);
      ctx._drawPass2.Reserve(objNeed*2/8);
      ctx._drawOnSurfaces.Reserve(objNeed*1/8);
      ctx._drawShadowProj.Reserve(objNeed*1/16);
      ctx._drawShadowVol.Reserve(objNeed*1/16);

			#ifdef TEST_RAIN_MAP
				ctx._drawRainMap.Reserve(objNeed/128);
			#endif
    }
    // resizing makes sense if frame is repeated
    ctx._drawPass1ON._original.Resize(0);
    ctx._drawPass1ON._blending.Resize(0);
    ctx._drawPass1OF._original.Resize(0);
    ctx._drawPass1OF._blending.Resize(0);
    ctx._drawPass1AN.Resize(0);
    ctx._drawPass1AF.Resize(0);
    ctx._drawPass2.Resize(0);
    ctx._drawPass3.Resize(0);
    ctx._drawPass4.Resize(0);
    ctx._drawOnSurfaces.Resize(0);
    ctx._drawShadowProj.Resize(0);
    ctx._drawShadowVol.Resize(0);

		#ifdef TEST_RAIN_MAP
			ctx._drawRainMap.Resize(0);
		#endif

    sortCtx._drawPass1ON.Resize(0);
    sortCtx._drawPass1OF.Resize(0);
    sortCtx._drawPass1AN.Resize(0);
    sortCtx._drawPass1AF.Resize(0);
    sortCtx._drawPass2.Resize(0);
    sortCtx._drawPass3.Resize(0);
    sortCtx._drawPass4.Resize(0);
    sortCtx._drawOnSurfaces.Resize(0);
    sortCtx._drawShadowProj.Resize(0);
    sortCtx._drawShadowVol.Resize(0);

		#ifdef TEST_RAIN_MAP
			sortCtx._drawRainMap.Resize(0);
		#endif


    // copy objects to working lists
    for (int i=0; i<sortCtx.objs.slots.Size(); i++)
    {
      SortLODShapeSlot &slot = sortCtx.objs.slots[i];
      for (int j=0; j<slot.objects.Size(); j++)
      {
        SortShapeSlotDist &distSlot = slot.objects[j];
        if (distSlot.objs.Size()<=0) continue;
        sortCtx.AddSlot(&distSlot);
      }
    }

    // copy objects to working lists
    for (int i=0; i<sortCtx.proxies.slots.Size(); i++)
    {
      SortLODShapeSlot &slot = sortCtx.proxies.slots[i];
      for (int j=0; j<slot.objects.Size(); j++)
      {
        SortShapeSlotDist &distSlot = slot.objects[j];
        if (distSlot.objs.Size()<=0) continue;
        sortCtx.AddSlot(&distSlot);
      }
    }
  }

  // convert all passes into a continuous lists and sort them as necessary
  // sort _drawPass1O objects
  {
    PROFILE_SCOPE_EX(oSo1O,oSort);
    // sort by shape
    QSort(sortCtx._drawPass1OF,CmpShapeSlot);
    QSort(sortCtx._drawPass1ON,CmpShapeSlot);

    // Create _drawPass1OF, sort the blending array
    sortCtx.CreateList(ctx._drawPass1OF._original, ctx._drawPass1OF._blending, sortCtx._drawPass1OF);
    QSort(ctx._drawPass1OF._blending, CmpBlendingItem);

    // Create _drawPass1ON, sort the blending array
    sortCtx.CreateList(ctx._drawPass1ON._original, ctx._drawPass1ON._blending, sortCtx._drawPass1ON);
    QSort(ctx._drawPass1ON._blending, CmpBlendingItem);
  }

  {
    PROFILE_SCOPE_EX(oSo1A,oSort);

    // Create _drawPass1AF, alpha part needs to be sorted by distance, not by shape
    sortCtx.CreateList(ctx._drawPass1AF,sortCtx._drawPass1AF,SlotObjectsContext::SetZCoord<false>(this));
    QSort(ctx._drawPass1AF,CmpZOrderObj);

    // Create _drawPass1AN, alpha part needs to be sorted by distance, not by shape
    sortCtx.CreateList(ctx._drawPass1AN,sortCtx._drawPass1AN,SlotObjectsContext::SetZCoord<false>(this));
    QSort(ctx._drawPass1AN,CmpZOrderObj);
  }

  {
    PROFILE_SCOPE_EX(oSo2A,oSort);

    sortCtx.CreateList(ctx._drawPass2,sortCtx._drawPass2,SlotObjectsContext::SetZCoord<true>(this));
    //QSort(ctx._drawPass2,CmpZOrderObjPass2) ;
    QSort(ctx._drawPass2,CmpZOrderObj) ;
  }

  {
    PROFILE_SCOPE_EX(oSo3,oSort);
    // in pass3 there is usually one object only, but there can be multiple proxies
    sortCtx.CreateList(ctx._drawPass3,sortCtx._drawPass3,SlotObjectsContext::SetZCoord<false>(this));
    QSort(ctx._drawPass3,CmpAlphaOrderedObj);
  }
  {
    PROFILE_SCOPE_EX(oSo4,oSort);
    // sorting in Pass 4 does not matter - usually only one object here
    sortCtx.CreateList(ctx._drawPass4,sortCtx._drawPass4,SlotObjectsContext::SetZCoord<false>(this));
    QSort(ctx._drawPass4,CmpZOrderObj);
  }

  {
    PROFILE_SCOPE_EX(oSoOS,oSort);

    sortCtx.CreateList(ctx._drawOnSurfaces,sortCtx._drawOnSurfaces);
    QSort(ctx._drawOnSurfaces,CmpRoadObj);
  }

  if (!GEngine->IsSBEnabled())
  { // sorting of projected shadows
    PROFILE_SCOPE_EX(oSoSP,oSort);

    sortCtx.CreateList(ctx._drawShadowProj,sortCtx._drawShadowProj);
    QSort(ctx._drawShadowProj,CmpProjShadowObj);
  }
  else
  { // sorting of shadow buffer shadows
    PROFILE_SCOPE_EX(oSoSB,oSort);

    sortCtx.CreateList(ctx._drawShadowProj,sortCtx._drawShadowProj);
    QSort(ctx._drawShadowProj,CmpSBShadowObj);
  }

	#ifdef TEST_RAIN_MAP
		if (GEngine->IsRainMapEnabled())
		{
			PROFILE_SCOPE_EX(oSoRainMap,oSort);
			sortCtx.CreateList(ctx._drawRainMap,sortCtx._drawRainMap);
			QSort(ctx._drawRainMap,CmpProjShadowObj);
		}
	#endif
				
  {
    PROFILE_SCOPE_EX(oSoSV,oSort);

    sortCtx.CreateList(ctx._drawShadowVol,sortCtx._drawShadowVol);
    QSort(ctx._drawShadowVol,CmpVolShadowObj);
  }

  // _VBS3_NPFLT: Move the camera near plane as far as possible, to reduce shadow-fighting artifacts
  static bool enableFloatingNearPlane = true;
  {
    // Enable/Disable the feature
#if _ENABLE_CHEATS
    if (GInput.GetCheat2ToDo(DIK_SPACE))
    {
      enableFloatingNearPlane = !enableFloatingNearPlane;
      GlobalShowMessage(500, "Floating near plane %s", enableFloatingNearPlane ? "On" : "Off");
    }
#endif

    // If feature enabled, do it
    if (enableFloatingNearPlane)
    {
      // now conservative value, consider real ground
      static float minNear = 0.07f;
      static float maxNear = 0.50f;
      static float safeNear = 0.2f; // 0.2 causes no ground clipping
      // Get the camera reference
      Camera &camera = *GetCamera();

      float cLeft = camera.Left();
      float cTop = camera.Top();
      float cNear = camera.Near();

      // depending of fov, determine min. required near plane distance
      AspectSettings as;
      GEngine->GetAspectSettings(as);
      //float cTop = fov * as.topFOV;
      //float fov = cTop/as.topFOV;
      float cNearDesired = 0.067f *as.topFOV/cTop;

      // Go through the usual drawing lists (except of pass3) and get the closest object distance. Note we can rely on the fact
      // the close objects have in dist2 value of the distance to bounding sphere surface, not only object center (see code with
      // "maxRadiusCoef", where this is done). This is quite important, as the closest object distance is going to be used to
      // shift the near clipping plane
      // see also other nearLimitDistance2
      float nearLimitDistance2 = Square(floatMax(Glob.config.GetShadowZ(), 100.0f));
      float minDistance = nearLimitDistance2;
        saturateMin(minDistance, sortCtx._drawPass1ON.GetMinDist2());
        //saturateMin(minDistance, sortCtx._drawPass1OF.GetMinDist2());
        saturateMin(minDistance, sortCtx._drawPass1AN.GetMinDist2());
        //saturateMin(minDistance, sortCtx._drawPass1AF.GetMinDist2());
        saturateMin(minDistance, sortCtx._drawOnSurfaces.GetMinDist2());
        saturateMin(minDistance, sortCtx._drawPass2.GetMinDist2());
        #if _VBS2
          saturateMin(minDistance, GVBSVisuals.GetMinDist2Line());
        #endif
        minDistance = sqrt(minDistance);

      float nearWanted = floatMin(maxNear,cNearDesired);
      if (minDistance<nearWanted)
      {
        // if the object distance is under the wanted value, we want to confirm it by real intersection
        CollisionBuffer col;
        Object *camOn = GWorld->GetCameraEffect() ? NULL : GWorld->CameraOn();
        Vector3 bottomCenter = camera.PositionModelToWorld(Vector3(0,-cTop,cNear).Normalized()*nearWanted*1.5f);
        _landscape->ObjectCollisionLine(GetCamera()->GetAge(),col,Landscape::FilterIgnoreOne(camOn),camera.Position(),bottomCenter,0,ObjIntersectView, ObjIntersectNone, CF_NEAREST_CONTACT);

        // select the nearest
        if (col.Size() > 0)
        {
          float dist2 = camera.Position().Distance2(col[0].pos);
          // be conservative, assume possible intersection even closer
          if (dist2*0.25f>Square(minDistance))
          {
            minDistance = floatMin(nearWanted,sqrt(dist2*0.25f));
          }
          if (CHECK_DIAG(DEMinObjectDistance))
          {
            DIAG_MESSAGE(200, "Confirmed object intersection %0.4f, result %0.4f", sqrt(dist2), minDistance);
          }
        }
        else
        {
          if (CHECK_DIAG(DEMinObjectDistance))
          {
            DIAG_MESSAGE(200, "No confirmed object intersection, result %0.4f", nearWanted);
          }
          minDistance = nearWanted;
        }
      }


      // Limit somehow the front plane shift
      static float maxFrontPlaneDistance = 10.0f;

      // Limit distance by ground distance
        // Remember top and left aspect of the camera

        // Get directions of the 4 frustum corners
        Vector3 dirLT = camera.Direction() + camera.DirectionUp() * cTop - camera.DirectionAside() * cLeft;
        Vector3 dirRT = camera.Direction() + camera.DirectionUp() * cTop + camera.DirectionAside() * cLeft;
        Vector3 dirLB = camera.Direction() - camera.DirectionUp() * cTop - camera.DirectionAside() * cLeft;
        Vector3 dirRB = camera.Direction() - camera.DirectionUp() * cTop + camera.DirectionAside() * cLeft;

        // Get inverse size of one corner - they all should be the same
      float frustumCornerInvSize = dirLT.InvSize();
        Assert(fabs(dirLT.Size() - dirRT.Size()) < 0.0001f);
        Assert(fabs(dirLT.Size() - dirLB.Size()) < 0.0001f);
        Assert(fabs(dirLT.Size() - dirRB.Size()) < 0.0001f);

        // Calculate the closest collision with a ground (use lines that are going through the corners of the frustum)
        float closestGroundCollision;
        {
          // Coefficient to shift the ground collision towards the eye a bit
          static float groundIntersectCoef = 0.8f;

          // Extend the distance a bit so that even after shifting towards player we can get a range from 0 to maxFrontPlaneDistance
          // Statement with "... / groundIntersectCoef... " sometimes failed from no obvious reason (it didn't report collision even though it was present,
          // providing visual glitches - near plane was behind the closest ground - that's why more generous estimation "... * 2.0f ..." was introduced)
          //float maxFrontPlaneDistanceShifted = maxFrontPlaneDistance / groundIntersectCoef;
          float maxFrontPlaneDistanceShifted = maxFrontPlaneDistance * 2.0f;

          // Get the intersection with the ground. Note we don't need to include object collisions at all, because they are included already
          // Note the calculations like  t / dirLT.Size() - its because we want to convert the actual distance to Z value
          {
            float t;
            closestGroundCollision = maxFrontPlaneDistanceShifted;
            const Vector3 groundOffset(0.0f, 0.8f, 0.0f);  // Offset to make the collision appear closer than the ground itself (estimation of clutter maximum height)
            t = _landscape->IntersectWithGroundOrSea(NULL, camera.Position() - groundOffset, dirLT.Normalized(), 0.0f, maxFrontPlaneDistanceShifted);
            if (t <= maxFrontPlaneDistanceShifted) saturateMin(closestGroundCollision, t  * frustumCornerInvSize);
            t = _landscape->IntersectWithGroundOrSea(NULL, camera.Position() - groundOffset, dirRT.Normalized(), 0.0f, maxFrontPlaneDistanceShifted);
            if (t <= maxFrontPlaneDistanceShifted) saturateMin(closestGroundCollision, t  * frustumCornerInvSize);
            t = _landscape->IntersectWithGroundOrSea(NULL, camera.Position() - groundOffset, dirLB.Normalized(), 0.0f, maxFrontPlaneDistanceShifted);
            if (t <= maxFrontPlaneDistanceShifted) saturateMin(closestGroundCollision, t  * frustumCornerInvSize);
            t = _landscape->IntersectWithGroundOrSea(NULL, camera.Position() - groundOffset, dirRB.Normalized(), 0.0f, maxFrontPlaneDistanceShifted);
            if (t <= maxFrontPlaneDistanceShifted) saturateMin(closestGroundCollision, t  * frustumCornerInvSize);
          }

          // Shift the ground collision towards the eye a bit
          closestGroundCollision *= groundIntersectCoef;


        // a trick: we need to avoid clipping into the ground
        // for this we compute an intersection at the bottom of view frusta, and limit the maximum
        float nearAdapt = maxNear;
        if (cNearDesired>safeNear)
        {
          Vector3 isect;
          // triangle:    cTop|____ cNear
          float cNearDist = sqrt(cTop*cTop+cNear*cNear);
          float isectDist = cNearDist * floatMin(cNearDesired,maxNear)/cNear;
          float isectSafe = cNearDist * safeNear/cNear;

          // transform bottom of frustum into camera space
          Vector3 bottomCenter = camera.DirectionModelToWorld(Vector3(0,-cTop,cNear)).Normalized();
          Vector3 leftCenter = camera.DirectionModelToWorld(Vector3(0,+cLeft,cNear)).Normalized();
          Vector3 rightCenter = camera.DirectionModelToWorld(Vector3(0,-cLeft,cNear)).Normalized();
          if (isectDist>isectSafe) isectDist = floatMin(isectDist,0.95f*_landscape->IntersectWithGround(NULL,camera.Position(),bottomCenter,isectSafe));
          if (isectDist>isectSafe) isectDist = floatMin(isectDist,0.95f*_landscape->IntersectWithGround(NULL,camera.Position(),leftCenter,isectSafe));
          if (isectDist>isectSafe) isectDist = floatMin(isectDist,0.95f*_landscape->IntersectWithGround(NULL,camera.Position(),rightCenter,isectSafe));

          float isectNear = cNear/cNearDist*isectDist;
          if (CHECK_DIAG(DEMinObjectDistance))
          {
            DIAG_MESSAGE(500,"  clipped to: %.3f (dist %.3f, max %.3f)",isectNear,isectDist, cNearDist*maxNear/cNearDesired);
          }
          if (isectNear<safeNear) isectNear = safeNear; // if results indicate clipping closer than safeNear, ignore them
          if (nearAdapt>isectNear) nearAdapt = isectNear;
        }
        else
        {
          nearAdapt = floatMax(cNearDesired,minNear);
        }

        // make sure we are not clipping into the ground at all, or into the clutter too much
        if (closestGroundCollision<nearAdapt) closestGroundCollision = nearAdapt;
#if 0 // _PROFILE || _DEBUG
        DIAG_MESSAGE(500,"Near z: %.3f, wanted %.3f",cNear,0.067f / fov);
#endif
        // at this point we should consider objects as well


        // Display the nearest ground collision distance
        if (CHECK_DIAG(DEMinObjectDistance))
        {
          DIAG_MESSAGE(200, Format("Nearest displayed ground distance: %0.4f, %.04f", closestGroundCollision, nearAdapt));
        }
      }
        // Convert minDistance from actual distance to conservative Z distance using the frustum corner size
        minDistance = minDistance * frustumCornerInvSize;


      // when we suspect an object collision, it might be wise to confirm it

        // Influence the min distance to be used for a clip near plane
        saturateMin(minDistance, closestGroundCollision);

      // Don't use values bigger than maxFrontPlaneDistance (this is just to keep the whole change (floating near plane) more conservative - nothing should not depend on this saturation ATM)
      saturateMin(minDistance, maxFrontPlaneDistance);

      // If the near plane was specified big from some reason, the one who specified that probably knew what he does, so keep it
      saturateMax(minDistance, camera.ClipNear());

      // Display the near plane distance
      if (CHECK_DIAG(DEMinObjectDistance))
      {
        DIAG_MESSAGE(200, Format("Safe Z distance to closest object (closest object is either in this or bigger distance): %0.4f", minDistance));
      }

      // Modify the camera near plane distance, to prevent z-fighting
      if (minDistance > camera.ClipNear())
      {
        camera.SetClipRange(minDistance, camera.ClipFar(), camera.ClipFarShadow(), camera.ClipFarSecShadow(), camera.ClipFarFog());
      }
    }
  }
}

#if 0 // _PROFILE
  #pragma optimize( "", on)
#endif

void Scene::DrawPass1Part(DrawContext &ctx, SortObjectListArg &list, Engine::PredicationMode depthDrawing)
{
  PROFILE_SCOPE_GRF_EX(o1Drw,*,SCOPE_COLOR_GREEN);
  // first of all draw non-alpha objects

  AUTO_STATIC_ARRAY(InitPtr<SortObject>,queryVis,256);
  int nDraw=list.Size();

  {
    PROFILE_SCOPE_DETAIL_EX(mrgIn,oSort);

#if _HELISIM_RENDER_TO_TEXTURE
    DoAssert(CheckMainThread());
    MergeInstances<> instanced(nDraw, _drawObjectsId);
#else
    MergeInstances<> instanced(nDraw);
#endif

    // scope capture inner part of MergeInstances only - can be seen as micro-job preparation in PIX
    {
      PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);

    // Inform rendering process the depth buffer rendering is in progress
      instanced._dp._zprimeIsBeingRendered = depthDrawing!=Engine::RenderingPass;
      instanced._dp._canReceiveShadows = depthDrawing==Engine::RenderingPass;

    for( int i=0; i<nDraw; i++ )
    {
      SortObject *oi=list[i];
      if (oi->drawLOD==LOD_INVISIBLE)
      {
        instanced.Flush(this,list);
        continue;
      }
      // select loaded level
      int level = oi->drawLOD;
        Assert( level>=0 );
        // one sort object may represent multiple objects at once
        if (oi->_list)
      {
          // check if there is already an instancing in progress
          instanced.Add(this,list,i, level, oi->_someDataNeeded);
              continue;
            }
        Object *obj = oi->GetObject();
        if (!Glob.config.enableInstancing)
        {
          instanced.Flush(this,list);

          // Load level(s) that are to be drawn
          LoadedLevelsInfo loaded = ShapeLoadedLevels()(oi->drawLOD, *oi, oi->_someDataNeeded);

          // Draw level(s)
          if (loaded.src >= 0 && loaded.dst >= 0)
          {
            DrawParameters dp(false, depthDrawing==Engine::PrimingPass, depthDrawing==Engine::RenderingPass, false, ZSpaceNormal, 0);

            ObjDraw<false> *ptr = new ObjDraw<false>(instanced._iSlot++,oi,obj,loaded.src,loaded.dst,dp
#if _HELISIM_RENDER_TO_TEXTURE
              , _drawObjectsId
#endif
              );

            SRef<IMicroJob> draw(ptr);
            // store matrices to the cache
            if (instanced.isPrepareNeeded)
          {
              MicroJobsAddPrepare(ptr->PrepareLevel(loaded.src));
              if (loaded.dst != loaded.src) MicroJobsAddPrepare(ptr->PrepareLevel(loaded.dst));
          }

            MicroJobsAdd(draw);

        }

        }
        else
        {
          // check if there is already an instancing in progress
          instanced.Add(this,list,i, level, oi->_someDataNeeded);
      }
    }


    instanced.Flush(this,list);
  }
  }

}


void Scene::DrawPass3PartBody(SortObjectListArg &list, int nDraw, Engine::PredicationMode depthDrawing, bool useSVShadows
#if _HELISIM_RENDER_TO_TEXTURE
                              , int drawListId
#endif
                              )
{
  // Prepare the draw parameters
  DrawParameters dp;
  if (depthDrawing!=Engine::RenderingPass)
  {
    dp._zprimeIsBeingRendered = depthDrawing!=Engine::RenderingPass;
    if (GEngine->IsSBEnabled())
		{
      // this is used to indicate 1st person rendering should perform shadow casting as well
      // this can be done only when using SV shadows, and needs to be done only when priming
      dp._canReceiveShadows = depthDrawing==Engine::DepthPass && useSVShadows;
		}
		else
		{
      dp._canReceiveShadows = false;
    }

	}
  dp._zSpace = ZSpace1stPerson;

	if (useSVShadows)
	{
		GEngine->SetStencilBufferTest(-1, TRUE);
	}

  GEngine->StartCBScope(0,0);
  int cb = GEngine->StartCBRecording(-1,-1,0);

  // pass 3 - draw cockpits after all external alpha effects
  for( int i=0; i<nDraw; i++ )
  {
    SortObject *oi=list[i];
    if( oi->drawLOD!=LOD_INVISIBLE )
    {
      Assert( oi->drawLOD>=0 );
#if 0 //_ENABLE_CHEATS

      if (CHECK_DIAG(DEXxx) && GWorld->GetCameraType()==CamInternal)
      {
        int view = oi->object->InsideViewGeomLOD(GWorld->GetCameraType());
        if (view!=LOD_INVISIBLE && view>=0)
        {
          oi->object->Draw(view,ClipAll,*oi->object);
        }
      }
      else
#endif
      {
        SCOPE_GRF(oi->shape->GetName(),0);
        Object *obj = oi->GetObject();
        Object *parentObj = oi->GetParentObject();
        if (!parentObj) parentObj = obj;

        GEngine->SetInstanceInfo(cb,HWhite, GetLandShadowSmooth(oi->position.PositionWorld(*GetCamera())));
        InstanceParameters ip(oi->coveredArea);
  #if _DEBUG
          Matrix4 trans = oi->position.TransformWorld(*GetCamera());
          if (!oi->_rootObject)
          {
            Assert(trans.Distance2(obj->RenderVisualState().Transform())<0.3f);
          }
  #endif

        obj->Draw(cb, oi->drawLOD, oi->orClip, dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
          , drawListId
#endif
          );
        GEngine->SetInstanceInfo(cb, HWhite, 1);
      }
    }
  }

  GEngine->StopCBRecording(cb,-1,"pass3");
  GEngine->EndCBScope();

	if (useSVShadows)
	{
		GEngine->SetStencilBufferTest(-1, FALSE);
	}
}

void Scene::DrawPass3Part(Engine::PredicatedCBScope &pred, SortObjectListArg &list, Engine::PredicationMode depthDrawing, bool useSVShadows
#if _HELISIM_RENDER_TO_TEXTURE
                          , int drawListId
#endif
                          )
{
  PROFILE_SCOPE_GRF_EX(oPas3,*,SCOPE_COLOR_BLUE);
  int nDraw=list.Size();

  // Finish early in case there is nothing to draw
  if (nDraw <= 0)
  {
    pred.beg=pred.end = 0;
    return;
  }

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  
  // we know we are always using separate z-space in pass3

  // prepare camera and depth range
  Camera::ClipRangeState state;
  Camera *cam = GetCamera();
  
  GEngine->SetDepthRange(DepthBorderPass3Min, DepthBorderPass3Max);
  cam->SaveClipRange(state);
  cam->SetClipRange(0.01,110,Glob.config.GetShadowZ(),Glob.config.GetProjShadowsZ(),cam->ClipFarFog());

  // make sure we are capturing a CB for predication
  bool doBody = true;
  if (depthDrawing!=Engine::RenderingPass)
  {
    doBody = GEngine->StartCBPredicatedScope(pred,depthDrawing);
  }
  else
  {
    if (GEngine->CopyCBPredicatedScope(pred,Engine::RenderingPass)) doBody = false;
  }
  
  if (doBody)
  {
    DrawPass3PartBody(list, nDraw, depthDrawing, useSVShadows
#if _HELISIM_RENDER_TO_TEXTURE
      , drawListId
#endif
      );
    if (depthDrawing!=Engine::RenderingPass)
    {
      GEngine->EndCBPredicatedScope(pred);
    }
  }

  if (useSVShadows)
  {
    // DoAssert(drawListId == 0);
    // draw shadow volumes for all objects which need it (including proxies)
    GEngine->BeginShadowVolumeRendering();
    for( int i=0; i<nDraw; i++ )
    {
      SortObject *oi=list[i];
      if (oi->shadowVolLOD==LOD_INVISIBLE) continue;

      Object *obj = oi->GetObject();
      float castShadowDiff;
      float castShadowAmb;
      bool castVolShadow = obj->CastPass3VolShadow(oi->drawLOD, true, castShadowDiff, castShadowAmb);

      if (castVolShadow)
      {

        int cb = -1;

        PROFILE_SCOPE(o3dsV);

        obj->DrawExShadow(cb,oi->shape,oi->position,oi->shadowVolLOD,true,true,oi
#if _HELISIM_RENDER_TO_TEXTURE
          , drawListId
#endif          
          );
      }
    }
    GEngine->EndShadowVolumeRendering();
        }
  // Restore the original camera and depth range
  cam->RestoreClipRange(state);
  GEngine->SetDepthRange();
}

void Scene::DrawPass4PartBody(SortObjectListArg &list, int nDraw, Engine::PredicationMode depthDrawing, bool useSVShadows
#if _HELISIM_RENDER_TO_TEXTURE
                              , int drawListId
#endif
                              )
{
  // Prepare the draw parameters
  DrawParameters dp;
  if (depthDrawing!=Engine::RenderingPass)
  {
    dp._zprimeIsBeingRendered = depthDrawing!=Engine::RenderingPass;
    if (GEngine->IsSBEnabled())
    {
      // this is used to indicate 1st person rendering should perform shadow casting as well
      // this can be done only when using SV shadows, and needs to be done only when rendering depth
      dp._canReceiveShadows = depthDrawing==Engine::DepthPass && useSVShadows;
      }
    else
    {
      dp._canReceiveShadows = false;
    }

  }
  dp._zSpace = ZSpaceUI;

  GEngine->StartCBScope(0,0);
  int cb = GEngine->StartCBRecording(-1,-1,0);

  // pass 4 - draw UI after cockpits
  for( int i=0; i<nDraw; i++ )
  {
    SortObject *oi=list[i];
    if( oi->drawLOD!=LOD_INVISIBLE )
    {
      Assert( oi->drawLOD>=0 );
      {
        SCOPE_GRF(oi->shape->GetName(),0);
        Object *obj = oi->GetObject();
        Object *parentObj = oi->GetParentObject();
        if (!parentObj) parentObj = obj;

        // avoid self-shadowing
        GEngine->SetInstanceInfo(cb,HWhite, GetLandShadowSmooth(oi->position.PositionWorld(*GetCamera())));
        InstanceParameters ip(oi->coveredArea);

        {
          Object::ProtectedVisualState<const ObjectVisualState> vs = obj->RenderVisualStateScope();
          obj->Draw(cb, oi->drawLOD, oi->orClip, dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
            , drawListId
#endif
          );
        }
        GEngine->SetInstanceInfo(cb, HWhite, 1);
      }
    }
  }

  GEngine->StopCBRecording(cb,-1,"pass4");
  GEngine->EndCBScope();
}

void Scene::DrawPass4Part(Engine::PredicatedCBScope &pred, SortObjectListArg &list, Engine::PredicationMode depthDrawing, bool useSVShadows
#if _HELISIM_RENDER_TO_TEXTURE
                          , int drawListId
#endif
                          )
{
  PROFILE_SCOPE_GRF_EX(oPas4,*,SCOPE_COLOR_BLUE);
  int nDraw=list.Size();

  // Finish early in case there is nothing to draw
  if (nDraw <= 0)
  {
    pred.beg=pred.end = 0;
    return;
  }

  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  
  // we know we are always using separate z-space in pass3

  // prepare camera and depth range
  Camera::ClipRangeState state;
  Camera *cam = GetCamera();
  
  GEngine->SetDepthRange(DepthBorderPass4Min, DepthBorderPass4Max);
  cam->SaveClipRange(state);
  cam->SetClipRange(0.001,10,Glob.config.GetShadowZ(),Glob.config.GetProjShadowsZ(),cam->ClipFarFog());


  extern Camera OriginalCamera; // is used by the compass to perform the animation // TODO: do not change cam direction
  extern const float CameraZoom;
  extern const float InvCameraZoom;
  
  OriginalCamera = *GetCamera();

  // set object drawing parameters
  float fov = 0.5 * InvCameraZoom;
  //*cam = Camera();
  AspectSettings as;
  GEngine->GetAspectSettings(as);
  cam->SetPerspective( 0.1, 100.0f, as.leftFOV * fov, as.topFOV * fov, Glob.config.GetShadowZ(), Glob.config.GetProjShadowsZ(), 100.0f );
  cam->Adjust(GEngine);


  // make sure we are capturing a CB for predication
  bool doBody = true;
  if (depthDrawing!=Engine::RenderingPass)
  {
    doBody = GEngine->StartCBPredicatedScope(pred,depthDrawing);
  }
  else
  {
    if (GEngine->CopyCBPredicatedScope(pred,Engine::RenderingPass)) doBody = false;
  }
  
  if (doBody)
  {
    DrawPass4PartBody(list, nDraw, depthDrawing, useSVShadows
#if _HELISIM_RENDER_TO_TEXTURE
      , drawListId
#endif
      );
    if (depthDrawing!=Engine::RenderingPass)
    {
      GEngine->EndCBPredicatedScope(pred);
    }
  }
  
  // restore camera
  SetCamera(OriginalCamera);
  
  // Restore the original camera and depth range
  cam->RestoreClipRange(state);
  GEngine->SetDepthRange();
}

void Scene::DrawPass1O(DrawContext &ctx, bool frameRepeated)
{
  PROFILE_SCOPE_EX(oPasO,*);

  // this is the only place where z-priming for objects is assumed to be done
  // in other places z-test LESSOREQUAL and alphatesting enabled if needed will do OK
  GEngine->SetZPrimingDone(-1,true);

  if (!GEngine->CopyCBPredicatedScope(ctx._oPasO,Engine::RenderingPass))
  {
    DrawPass1Part(ctx,ctx._drawPass1ON._original,Engine::RenderingPass);
    DrawPass1Part(ctx,ctx._drawPass1ON._blending,Engine::RenderingPass);
  }

  GEngine->SetZPrimingDone(-1,false);
}

void Scene::DrawPass1A(DrawContext &ctx, bool frameRepeated)
{
  PROFILE_SCOPE_EX(oPasA,*);

  DrawPass1Part(ctx,ctx._drawPass1OF._original,Engine::RenderingPass);
  DrawPass1Part(ctx,ctx._drawPass1OF._blending,Engine::RenderingPass);

  DrawPass1Part(ctx,ctx._drawPass1AF,Engine::RenderingPass);

  GEngine->SetZPrimingDone(-1,true);

  if (!GEngine->CopyCBPredicatedScope(ctx._oPasA,Engine::RenderingPass))
  {
    DrawPass1Part(ctx,ctx._drawPass1AN,Engine::RenderingPass);
  }

  GEngine->SetZPrimingDone(-1,false);
}

void Scene::DrawPassOnSurface(DrawContext &ctx, Engine::PredicationMode depthDrawing)
{
  PROFILE_SCOPE_GRF(oSDrw,SCOPE_COLOR_GREEN);
  
  int nDraw=ctx._drawOnSurfaces.Size();

  PROFILE_SCOPE_DETAIL_EX(mrgIn,oSort);

#if _HELISIM_RENDER_TO_TEXTURE
  DoAssert(CheckMainThread());
  MergeInstances<true, ShapeLoadedLevelsShadowOrSplit> instanced(nDraw, _drawObjectsId);
#else
  MergeInstances<true, ShapeLoadedLevelsShadowOrSplit> instanced(nDraw);
#endif

  {
    PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);

#if _VBS3_CRATERS_DEFORM_TERRAIN_SHADOWS
    // Inform rendering process the depth buffer rendering is in progress
    instanced._dp._zprimeIsBeingRendered = depthDrawing==Engine::PrimingPass;
    instanced._dp._canReceiveShadows = depthDrawing==Engine::RenderingPass;
#endif

    for( int i=0; i<nDraw; i++ )
    {
      SortObject *oi=ctx._drawOnSurfaces[i];
      if (oi->drawLOD==LOD_INVISIBLE)
      {
        Fail("Invisible?");
        instanced.Flush(this,ctx._drawOnSurfaces);
        continue;
      }
      Assert( oi->drawLOD>=0 );
      Object *obj = oi->GetObject();
      if (!Glob.config.enableInstancing)
      {
        instanced.Flush(this,ctx._drawOnSurfaces);
        // detect instancing

        // Load level(s) that are to be drawn
        ShapeLoadedLevelsShadowOrSplit useLevel;
        int loaded = useLevel(oi->drawLOD, *oi, oi->_someDataNeeded).dst;

        // Draw level(s)
        if (loaded >= 0)
        {
#if _VBS3_CRATERS_DEFORM_TERRAIN_SHADOWS
          DrawParameters dp(false, false, depthDrawing==Engine::PrimingPass, depthDrawing==Engine::RenderingPass, false, false, false, 0);
#else
          const DrawParameters &dp = DrawParameters::_default;
#endif
          // note: on surface object typically cannot be multijobed, they require splitting
          ObjDraw<true> draw(-1,oi,obj,loaded,loaded,dp
#if _HELISIM_RENDER_TO_TEXTURE
            , _drawObjectsId
#endif
            );

          // store matrices to the cache
          // note: we know PrepareLevel does not return anything when called for a sync task
          if (instanced.isPrepareNeeded)
          {
            draw.PrepareLevel(loaded);
          }
          draw(NULL,-1);
        }

      }
      else
      {
        // check if there is already an instancing in progress
        instanced.Add(this,ctx._drawOnSurfaces,i, oi->drawLOD, oi->_someDataNeeded);
      }
    }

    instanced.Flush(this,ctx._drawOnSurfaces);
  }
}

#ifdef TEST_RAIN_MAP

void Scene::DrawRainMap(DrawContext &ctx)
{
	PROFILE_SCOPE_GRF_EX(rainMapDrw,*,SCOPE_COLOR_GREEN);
	int nDraw=ctx._drawRainMap.Size();

//	LogF("%d", nDraw);
//	return;

	PROFILE_SCOPE_DETAIL_EX(mrgIn,oSort);
	// as the same object participates in multiple cascades, we cache the animations

#if _HELISIM_RENDER_TO_TEXTURE
	MergeInstances<false, ShapeLoadedLevelsSB,CompareLevelDrawShadowBuffer,true/*Prepare: cached animation*/> instanced(nDraw, _drawObjectsId);
#else
	MergeInstances<false, ShapeLoadedLevelsSB,CompareLevelDrawShadowBuffer,true/*Prepare: cached animation*/> instanced(nDraw);
#endif

	{
		PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);
		// Inform rendering process the shadow buffer rendering is in progress
		instanced._dp._sbIsBeingRendered = true;
		instanced._dp._canReceiveShadows = false;

		for( int i=0; i<nDraw; i++ )
		{
			SortObject *oi = ctx._drawRainMap[i];

			// Select a LOD to draw
			int level = oi->shadowLOD;

			// Skip objects which have no shadowLOD or whose shadows do not lie in the specified CSM layer
			if (level==LOD_INVISIBLE)
			{
				instanced.Flush(this, ctx._drawRainMap); // If we skip some object, we have broken the interval of instances and must flush it
				continue;
			}
			else
			{
				#if _ENABLE_PERFLOG
					ADD_COUNTER(rainMap,1);
				#endif
			}

			Assert( level>=0 );
			// select loaded level
			if (!Glob.config.enableInstancing)
			{
				instanced.Flush(this, ctx._drawRainMap);
				// detect instancing
				int levelS = oi->shape->UseLevel(level,false);
				if (levelS>=0)
				{
					Object *obj = oi->GetObject();
					Object *parentObj = oi->GetParentObject();
					if (!parentObj) parentObj = obj;

					PROFILE_SCOPE(o1odr);
					DrawParameters dp(true, false, false, false, ZSpaceNormal, 0);
					InstanceParameters ip(oi->coveredArea);
					obj->Draw(-1, levelS, oi->orClip, dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
						, _drawObjectsId
#endif
						);
				}
			}
			else
			{
				// check if there is already an instancing in progress
				instanced.Add(this, ctx._drawRainMap, i, level, false);
			}
		}
		instanced.Flush(this,ctx._drawRainMap);
	}
}

#endif


void Scene::DrawSBShadows(DrawContext &ctx, int layerIndex)
{
  PROFILE_SCOPE_GRF_EX(sbDrw,*,SCOPE_COLOR_GREEN);
  int nDraw=ctx._drawShadowProj.Size();
  
  PROFILE_SCOPE_DETAIL_EX(mrgIn,oSort);
  // as the same object participates in multiple cascades, we cache the animations

#if _HELISIM_RENDER_TO_TEXTURE
  MergeInstances<false, ShapeLoadedLevelsSB,CompareLevelDrawShadowBuffer,true/*Prepare: cached animation*/> instanced(nDraw, _drawObjectsId);
#else
  MergeInstances<false, ShapeLoadedLevelsSB,CompareLevelDrawShadowBuffer,true/*Prepare: cached animation*/> instanced(nDraw);
#endif

  {
    PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);
    // Inform rendering process the shadow buffer rendering is in progress
    instanced._dp._sbIsBeingRendered = true;
    instanced._dp._canReceiveShadows = false;

    for( int i=0; i<nDraw; i++ )
    {
      SortObject *oi = ctx._drawShadowProj[i];

      // Select a LOD to draw
      int level = oi->shadowLOD;

      // Skip objects which have no shadowLOD or whose shadows do not lie in the specified CSM layer
      if ((level==LOD_INVISIBLE) || (!(oi->csmLayerReached & (1 << layerIndex))))
      {
        instanced.Flush(this, ctx._drawShadowProj); // If we skip some object, we have broken the interval of instances and must flush it
        continue;
      }
      else
      {
#if _ENABLE_PERFLOG
        switch (layerIndex)
        {
        case 0:
          ADD_COUNTER(csmL0,1);
          break;
        case 1:
          ADD_COUNTER(csmL1,1);
          break;
        case 2:
          ADD_COUNTER(csmL2,1);
          break;
        case 3:
          ADD_COUNTER(csmL3,1);
          break;
        }
#endif
      }

      Assert( level>=0 );
      // select loaded level
      if (!Glob.config.enableInstancing)
      {
        instanced.Flush(this, ctx._drawShadowProj);
        // detect instancing
        int levelS = oi->shape->UseLevel(level,false);
        if (levelS>=0)
        {
          Object *obj = oi->GetObject();
          Object *parentObj = oi->GetParentObject();
          if (!parentObj) parentObj = obj;

          // TODO:MC: use micro-job instead
          PROFILE_SCOPE(o1odr);
          DrawParameters dp(true, false, false, false, ZSpaceNormal, 0);
          InstanceParameters ip(oi->coveredArea);
          obj->Draw(-1, levelS, oi->orClip, dp, ip, oi->distance2, oi->position, oi
#if _HELISIM_RENDER_TO_TEXTURE
            , _drawObjectsId
#endif
            );
        }
      }
      else
      {
        // check if there is already an instancing in progress
        instanced.Add(this, ctx._drawShadowProj, i, level, false);
      }
    }
    instanced.Flush(this,ctx._drawShadowProj);
  }
}

const bool PrepareVolShadows = false;

static inline int ShadowInstancingFlush(int &iSlot, const SortObjectListArg & list, int iBeg, int iEnd, int iLevel
#if _HELISIM_RENDER_TO_TEXTURE
                                        , int drawObjectsId
#endif
                                        )
{
  if (iBeg>=0)
  {
#if _HELISIM_RENDER_TO_TEXTURE
    ObjDrawShadowInstanced<false> *ptr = new ObjDrawShadowInstanced<false>(iSlot++, list, iBeg, iEnd, iLevel, drawObjectsId);
#else
    ObjDrawShadowInstanced<false> *ptr = new ObjDrawShadowInstanced<false>(iSlot++, list, iBeg, iEnd, iLevel);
#endif
    SRef<IMicroJob> draw(ptr);
    // store matrices to the cache
    if (PrepareVolShadows)
    {
      MicroJobsAddPrepare(ptr->PrepareLevel());
    }
    
    MicroJobsAdd(draw);

    FreeOnDemandGarbageCollect(512*1024,128*1024);
    // mark as flushed
    iBeg = -1;
  }
  return iBeg;
}

void Scene::DrawVolShadows(DrawContext &ctx
#if _HELISIM_RENDER_TO_TEXTURE
                           , int drawObjectsId
#endif
                           )
{
  // no stencil buffer => no shadows 
  if (GEngine->ZBiasExclusion()) return;
  
  PROFILE_SCOPE_GRF_EX(o1ShadowVolume,*,SCOPE_COLOR_BLUE);
  const SortObjectListArg &list = ctx._drawShadowVol;
  GEngine->BeginShadowVolumeRendering();
  
  PROFILE_SCOPE_DETAIL_EX(mrgIn,oSort);
  

  MicroJobsStart();
  int iSlot = 0;
  {
    PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);

  int instancedBeg = -1; // start of instanced chain
  int instancedEnd = -1; // end of instanced chain
  int instancedLevel = -1;
  // never call between UseShadowLevel and rendering, but make sure it is called before each render
  FreeOnDemandGarbageCollect(512*1024,128*1024);
  for (int i = 0; i < list.Size(); i++)
  {
    SortObject *oi = list[i];
      if( oi->shadowVolLOD==LOD_INVISIBLE )
      {
#if _HELISIM_RENDER_TO_TEXTURE
        instancedBeg = ShadowInstancingFlush(iSlot, list, instancedBeg, instancedEnd, instancedLevel, drawObjectsId);
#else
        instancedBeg = ShadowInstancingFlush(iSlot, list, instancedBeg, instancedEnd, instancedLevel);
#endif
        continue;
      }
    LODShape *shape = oi->shape;
    Assert(shape->IsShadowVolume(oi->shadowVolLOD));
    // check if the shadow lod is ready
    int level = oi->shape->UseShadowLevel(oi->shadowVolLOD);
      if (level<0)
      {
#if _HELISIM_RENDER_TO_TEXTURE
        instancedBeg = ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel, drawObjectsId);
#else
        instancedBeg = ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel);
#endif
        continue;
      }

    Object *obj = oi->GetObject();
      if (!Glob.config.enableInstancing || !obj->CanBeInstancedShadow(level))
      {
        // flush first, to make sure we are recording ordered by i
#if _HELISIM_RENDER_TO_TEXTURE
        instancedBeg = ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel, drawObjectsId);
#else
        instancedBeg = ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel);
#endif

        if (obj->CanObjDrawShadowAsTask(level))
        {
#if _HELISIM_RENDER_TO_TEXTURE
          ObjDrawShadow<false> *ptr = new ObjDrawShadow<false>(iSlot++,obj,oi,level, drawObjectsId);
#else
          ObjDrawShadow<false> *ptr = new ObjDrawShadow<false>(iSlot++,obj,oi,level);          
#endif
          SRef<IMicroJob> draw(ptr);
          // store matrices to the cache
          if (PrepareVolShadows)
          {
            MicroJobsAddPrepare(ptr->PrepareLevel());
          }

          MicroJobsAdd(draw);
        }
        else
        {
#if _HELISIM_RENDER_TO_TEXTURE
          ObjDrawShadow<true> draw(-1,obj,oi,level, drawObjectsId);
#else
          ObjDrawShadow<true> draw(-1,obj,oi,level);
#endif
          // store matrices to the cache
          if (PrepareVolShadows)
    {
            SRef<IMicroJob> prepare = draw.PrepareLevel();
            if (prepare) (*prepare)(NULL, -1);
          }

          draw(NULL,-1);
        }

      FreeOnDemandGarbageCollect(512*1024,128*1024);
    }
    else
    {
      // check if there is already an instancing in progress
      if (instancedBeg<0)
        { // no instancing started yet - start a new one
        instancedBeg = i;
        instancedEnd = i;
        instancedLevel = level;
      }
      else
      {
          // if instancing is open, current item needs to continue it
          Assert(instancedEnd==i-1);
        SortObject *oiBeg = list[instancedBeg];
        // if shadow LODs are supported, they need to be tested here
          // instancedEnd==i-1 should not be necessary
        if (instancedEnd==i-1 && oiBeg->shape==shape && instancedLevel==level)
        {
          instancedEnd = i;
        }
        else
        {
#if _HELISIM_RENDER_TO_TEXTURE
            ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel, drawObjectsId);
#else
            ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel);
#endif

          instancedBeg = i;
          instancedEnd = i;
          instancedLevel = level;
          FreeOnDemandGarbageCollect(512*1024,128*1024);
        }
      }
    }
  }
#if _HELISIM_RENDER_TO_TEXTURE
    ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel, drawObjectsId);
#else
    ShadowInstancingFlush(iSlot,list, instancedBeg, instancedEnd, instancedLevel);
#endif
  }
    
  MicroJobsExecute(iSlot);
  GEngine->EndShadowVolumeRendering();
}

void Scene::DrawPass2(DrawContext &ctx)
{
  GEngine->FlushQueues();
  // ...CaptureRefractionsBackground moved before water rendering
  // make a snapshot of the scene used for water rendering and other refractions
  //GEngine->CaptureRefractionsBackground();   


  FreeOnDemandGarbageCollect(1024*1024,256*1024);
  PROFILE_SCOPE_EX(oPas2,*);

  //GetLandscape()->DrawNearGround(*this);

  // must be sorted by distance
  // then draw shadows
  // note: some polygons can not be ordered
  int nDraw=ctx._drawPass2.Size();
  // draw alpha parts of roads

  // Calculate index of the i-th closest sprite
  int limitSizeIndex = nDraw;
  {
    // Get the number of limited particles according to shading quality
    int limitNum;
    if (GetShadingQuality() >= 100) // Very high
    {
      limitNum = 60;
    }
    else if (GetShadingQuality() >= 10) // High
    {
      limitNum = 40;
    }
    else  // Normal
    {
      limitNum = 20;
    }

    // Calculate index of the i-th closest sprite
    int limitSpriteNum = limitNum;
    for (int i = nDraw - 1; i >= 0; i--)
    {
      SortObject *oi = ctx._drawPass2[i];
      if (oi->GetObject()->IsSprite())
      {
        limitSizeIndex = i;
        if ((--limitSpriteNum) <= 0) break;
      }
    }
  }

  {
    PROFILE_SCOPE_DETAIL_EX(mrgIn,oSort);
    // objects are already sorted by zCoord - see CmpZOrderObj in Scene::DrawPreparation
    PROFILE_SCOPE_GRF(o2Drw,SCOPE_COLOR_GREEN);

#if _HELISIM_RENDER_TO_TEXTURE
    DoAssert(CheckMainThread());
    MergeInstances<> instanced(nDraw, _drawObjectsId);
#else
    MergeInstances<> instanced(nDraw);
#endif

    {
      PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);

      // last draw alpha objects (not roads - they are already drawn)
      for( int i=0; i<nDraw; i++ )
      {
        SortObject *oi=ctx._drawPass2[i];

        // Set the limitSpriteSize flag
        oi->limitSpriteSize = (i < limitSizeIndex);

        if (oi->drawLOD==LOD_INVISIBLE)
        {
          instanced.Flush(this,ctx._drawPass2);
          continue;
        }
        Assert( oi->drawLOD>=0 );
        Object *obj = oi->GetObject();
        if (!Glob.config.enableInstancing)
        {
          instanced.Flush(this,ctx._drawPass2);

          // Load level(s) that are to be drawn
          LoadedLevelsInfo loaded = ShapeLoadedLevels()(oi->drawLOD, *oi, oi->_someDataNeeded);

          // Draw level(s)
          if (loaded.src >= 0 && loaded.dst >= 0)
          {
            ObjDraw<true> draw(-1, oi, obj, loaded.src, loaded.dst, DrawParameters::_default
#if _HELISIM_RENDER_TO_TEXTURE
              , _drawObjectsId
#endif
              );

            // store matrices to the cache
            if (PrepareVolShadows)
            {
              draw.PrepareLevel(loaded.src);

              if (loaded.dst != loaded.src)
              {
                draw.PrepareLevel(loaded.dst);
              }
            }

            draw(NULL,-1);
            FreeOnDemandGarbageCollect(512*1024,128*1024);
          }

        }
        else
        {
          instanced.Add(
            this,ctx._drawPass2,i,oi->drawLOD,oi->_someDataNeeded
            );
        }
      }

      instanced.Flush(this,ctx._drawPass2);
    }
  }
  GEngine->FlushQueues();
}

void Scene::DrawPass3(DrawContext &ctx)
{
  GEngine->SetZPrimingDone(-1,true);

  DrawPass3Part(ctx._oPas3,ctx._drawPass3, Engine::RenderingPass, false
#if _HELISIM_RENDER_TO_TEXTURE
    , _drawObjectsId
#endif
    );
  
  GEngine->SetZPrimingDone(-1,false);
}

void Scene::DrawPass4(DrawContext &ctx)
{
  GEngine->SetZPrimingDone(-1,true);

  DrawPass4Part(ctx._oPas4,ctx._drawPass4, Engine::RenderingPass, false
#if _HELISIM_RENDER_TO_TEXTURE
    , _drawObjectsId
#endif
    );

  GEngine->SetZPrimingDone(-1,false);
}

void Scene::DrawDepthPrime(DrawContext &ctx, bool useSVShadows, Engine::PredicationMode pass
#if _HELISIM_RENDER_TO_TEXTURE
                           , int drawListId
#endif
                           )
{
  if (!GEngine->IsAbleToDrawCheckOnly()) return;
  PROFILE_SCOPE_EX(oPasD,*);

  // Draw the usual depth objects, near first
  
  if (GEngine->StartCBPredicatedScope(ctx._oPasO,pass))
  {
    DrawPass1Part(ctx,ctx._drawPass1ON._original,pass);
    DrawPass1Part(ctx,ctx._drawPass1ON._blending,pass);
    GEngine->EndCBPredicatedScope(ctx._oPasO);
  }

  // We need to perform depth priming even for partially alpha transparent objects (in order the SSSM to work correctly)
  if (GEngine->StartCBPredicatedScope(ctx._oPasA,pass))
  {
    DrawPass1Part(ctx,ctx._drawPass1AN,pass);
    GEngine->EndCBPredicatedScope(ctx._oPasA);
  }
  
  // Draw volume shadows prior the pass3 (as the z-buffer can't be used for depth determination after that)
  if (useSVShadows)
  {
#if _HELISIM_RENDER_TO_TEXTURE
    DrawVolShadows(ctx, drawListId);
#else
    DrawVolShadows(ctx);
#endif
  }

  // Draw the _drawDepth3 objects (like gun)
#if _HELISIM_RENDER_TO_TEXTURE
  if (drawListId == 0)
  {
    DrawPass3Part(ctx._oPas3,ctx._drawPass3, pass, useSVShadows, drawListId);
    DrawPass4Part(ctx._oPas4,ctx._drawPass4, pass, useSVShadows, drawListId);
  }
#else
  DrawPass3Part(ctx._oPas3,ctx._drawPass3, pass, useSVShadows);
  DrawPass4Part(ctx._oPas4,ctx._drawPass4, pass, useSVShadows);
#endif
  
}

/// temporary object - used for diagnostics objects rendering
class TempVehicle: public Vehicle
{
  public:
  TempVehicle( LODShape *shape )
  :Vehicle(shape,VehicleTypes.New("temp"),CreateObjectId())
  {}
  void Simulate( float deltaT, SimulationImportance prec );
};

void TempVehicle::Simulate( float deltaT, SimulationImportance prec )
{
  SetDeleteRaw(); // delete immediatelly
}

/// implementation of TempVehicle with an arrow shape
class SelArrow: public TempVehicle
{
  public:
  SelArrow():TempVehicle(GScene->ForceArrow())
  {}
};

class SelSphere: public TempVehicle
{
public:
  SelSphere():TempVehicle(GScene->ForceSphere())
  {}
};

class SelGizmo: public TempVehicle
{
public:
  SelGizmo():TempVehicle(GScene->ForceGizmo())
  {}
};


Entity *Scene::CreateSelArrow() const
{
  if (_arrows.Size()>100000)
  {
    Fail("Too many diagnostics objects");
    return NULL;
  }
  return new SelArrow();
}

Entity *Scene::CreateSelGizmo() const
{
  if (_arrows.Size()>100000)
  {
    Fail("Too many diagnostics objects");
    return NULL;
  }
  return new SelGizmo();
}

Entity *Scene::CreateSelSphere() const
{
  if (_arrows.Size()>100000)
  {
    Fail("Too many diagnostics objects");
    return NULL;
  }
  return new SelSphere();
}

void Scene::ShowArrow( Vector3Par pos )
{
  if (_arrows.Size()>100000)
  {
    Fail("Too many diagnostics objects");
    return;
  }
  Assert(pos.IsFinite());
  // create a temporary object for drawing
  // use object min-max box to determine arrow position
  SelArrow *sel=new SelArrow();
  LODShape *arrow=sel->GetShape();
  //float topLevelShape=shape->Max().Y();
  // arrow will be rotated, maxZ will become minY
  float bottomLevelArrow=arrow->Max().Z();
  sel->SetOrientation(Matrix3(MRotationX,H_PI/2));
  sel->SetPosition(pos+Vector3(0,bottomLevelArrow,0));
  _arrows.Add(sel);
  Assert(sel->RenderVisualState().IsFinite());
}
  
/**
@param now render now, or accumulate for rendering later
 Render now should be used when we know we are inside scene rendering.
*/
void Scene::ShowObject( Object *obj, bool now )
{
  if (now)
    DrawObject(obj);
  else
    ShowDiagObject(obj);
  }

void Scene::ShowDiagObject(Object *obj)
{
  if (_arrows.Size()>100000)
  {
    Fail("Too many diagnostics objects");
    return;
  }
  // create a temporary object for drawing
  Assert(obj->RenderVisualState().IsFinite());
  _arrows.Add(obj);
}

void Scene::DrawObject(Object *obj)
{
#if _HELISIM_RENDER_TO_TEXTURE
  DoAssert(CheckMainThread());
#endif

  obj->Draw(
    -1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, obj->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
    , _drawObjectsId
#endif
  );
}


void Scene::DiagBBox(const Vector3 *minMax, const Frame &pos, ColorVal color, RenderAs render)
{
  Ref<enf::Shape> sh = enf::Shape::CreateBox(minMax[0], minMax[1], PackedColor(color), enf::SS_ONCE|enf::SS_WIREFRAME);
  sh->SetMatrix(pos);
  sh->SetOutlineColor(PackedColor(color));
}

/**
@param priority max. order of the text in the scene for the text to be visible

Diagnostics text rendering is deferred after the whole 3D scene is rendered
This allows us to prevent z-buffer issues.
*/
void Scene::DrawDiagText(RString text, Vector3Par pos, int priority, float size, ColorVal color, float maxDist)
{
  // we also limit a drawing distance for texts depending on their priority
  float priorDist = priority*2.0f;
  if (maxDist>priorDist) maxDist = priorDist;
  if (maxDist>Glob.config.horizontZ) maxDist = Glob.config.horizontZ;
  if (!GetCamera()) return;
  if (pos.IsFinite())
  {
    if (GetCamera()->Position().Distance2(pos)>Square(maxDist)) return;
    DiagText &txt = _diagText.Append();
    txt._text = text;
    txt._pos = pos;
    txt._color = color;
    txt._size = size;
    txt._priority = priority;
  }
  else
  {
    LogF("Infinite DrawDiagText pos: %s, %g,%g,%g",cc_cast(text),pos.X(),pos.Y(),pos.Z());
  }
}

struct TextToDraw
{
  Point2DAbs pos;
  int textIndex;
  float size;
};

TypeIsSimple(TextToDraw)

void Scene::DrawDiagTexts()
{
#if _WIN32
  if (_diagText.Size()<=0) return;
  
  // we need to flush the background thread, so that we can peform direct rendering
  GEngine->FlushBackgroundScope();
    
  const int maxTexts = 100;
  struct CompareByDistance
  {
    Vector3 _pos;

    CompareByDistance(Vector3Par pos):_pos(pos){}

    int operator () (const DiagText *t1, const DiagText *t2) const
    {
      return sign(t1->_pos.Distance2(_pos)-t2->_pos.Distance2(_pos));
    }
  };

  // we want the text size is pixel to be approximately the same no matter what resolution is used
  // for 800x600 we want 0.02
  float fontSize = floatMin(GEngine->GetDebugFontSize(),(800*0.02f)/GEngine->Height());
  
  QSort(_diagText,CompareByDistance(GetCamera()->Position()));
  // traverse the texts front to back to check which we want to render
  AutoArray< TextToDraw, MemAllocLocal<TextToDraw,maxTexts> > toDraw;
  int fullSizeDraw = 0;
  for (int i=0; i<_diagText.Size(); i++)
  {
    const DiagText &txt = _diagText[i];
    // some texts want to be rendered only if there are not many texts before them
    // caution: we need to prevent overflow by *4
    if (txt._priority<INT_MAX/4 && txt._priority*4<fullSizeDraw) continue;
    // when text is out of priority, we fade it out with a distance for a short while
    // 2D approach:
    // compute the text location
    // render the text as 2D

    const Camera &camera=*GetCamera();
    Vector3 posp=ScaledInvTransform()*txt._pos;

    // camera plane clip test
    // perform more distant clipping than normal
    // but because of floating plane (_VBS3_NPFLT) avoid clipping to far
    float nearest=floatMin(camera.Near()*2.5f,1.0f);
    if( posp.Z()<nearest ) continue;

    // apply perspective on position and size
    Matrix4Val project=camera.Projection();

    float invW=1/posp[2];
    posp[0]=project(0,2)+project(0,0)*posp[0]*invW;
    posp[1]=project(1,2)+project(1,1)*posp[1]*invW;
    //pos[2]=(project.Position()[2]-size*0.1)*invW;
    float invNearPos = 1/posp[2];
    posp[2]=project(2,2)+project.Position()[2]*invNearPos;
    //float rhw = invNearPos;

    // a simple clipping to prevent counting texts outside of the screen as rendered
    if (posp[0]>_engineWidth || posp[0]<-_engineWidth*0.1) continue;
    if (posp[1]>_engineHeight || posp[1]<-_engineHeight*0.1) continue;

    
    float size = 1.0f;
    if (txt._priority<fullSizeDraw && toDraw.Size()<maxTexts)
    {
      // we know text with priority txt._priority is already queued for rendering
      // txt._priority<fullSizeDraw, fullSizeDraw<=toDraw.Size()
      Assert(fullSizeDraw<=toDraw.Size());
      const DiagText &baseTxt = _diagText[toDraw[txt._priority].textIndex];
      float baseZ = (ScaledInvTransform()*baseTxt._pos)[2];
      // texts are ordered by distance, not Z
      // in some cases Z is lower than distance
      // but we want to avoid texts growing larger
      size = floatMin(baseZ*invW,1.0f);
      // rendering text too small has no sense
      if (fontSize*size*GEngine->Height()<2.0f) continue;
    }
    else
    {
      fullSizeDraw++;
      if (fullSizeDraw>=maxTexts) break;
    }
    TextToDraw &td = toDraw.Append();
    td.pos = Point2DAbs(posp[0],posp[1]);
    td.textIndex = i;
    td.size = size;
  }

  // render the texts back to front
  for (int t=toDraw.Size(); --t>=0; )
  {
    const TextToDraw &tt = toDraw[t];
    const DiagText &txt = _diagText[tt.textIndex];

    
    
    // we want the screen size to be independent of the perspective
    // we always use debug font and debug size
    TextDrawAttr attr(
      fontSize*txt._size*tt.size,GEngine->GetDebugFont(),
      PackedColor(0x80000000), 0
    );

    Point2DAbs posS(tt.pos.x+1,tt.pos.y+1);
    GEngine->DrawText(posS,attr,txt._text);

    // draw a shadow
    attr._color = PackedColor(txt._color);
    GEngine->DrawText(tt.pos,attr,txt._text);
  }
  _diagText.Clear();
  GEngine->FlushQueues();
#endif
}

void Scene::DrawCollisionStar( Vector3Par pos, float size, ColorVal color, bool now)
{
  if( _collisionStar.NotNull() && size>=0.01 )
  {
    Ref<Object> obj=new ObjectColored(_collisionStar->GetShape(),VISITOR_NO_ID);
    obj->SetTransform(M4Identity);
    obj->SetPosition(pos);
    obj->SetScale(size*4);
    obj->SetConstantColor(color);
    ShowObject(obj,now);
  }
}

void Scene::DrawDiagModel(Vector3Par pos, LODShape *shape, float size, ColorVal color, bool now)
{
  Ref<Object> obj=new ObjectColored(shape,VISITOR_NO_ID);
  obj->SetTransform(M4Identity);
  obj->SetPosition(pos);
  obj->SetScale(size/shape->BoundingSphere());
  obj->SetConstantColor(color);
  ShowObject(obj,now);
}

void Scene::DrawDiagRectangle(float size, Vector3 pos, ColorVal color, bool now)
{
  LODShape *rectShape = Preloaded(RectangleModel);

  Ref<Object> rect = new ObjectColored(rectShape, VISITOR_NO_ID);

  float origSize = rectShape->BoundingSphere() * H_SQRT2;
  rect->SetScale(size / origSize);
              
  rect->SetPosition(pos);
  rect->SetConstantColor(color);

  ShowObject(rect, now);
}

void Scene::DrawDiagArrow(Vector3Par pos, Vector3Par dir, float size, ColorVal color, bool now)
{
  LODShape *forceArrow=ForceArrow();
  float length = forceArrow->Max()[2] - forceArrow->Min()[2];
  size /= length;
  if (size<1e-4) return; // no need to show zero sized or very small forces

  Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
  Vector3 aside=dir.CrossProduct(VAside);
  Vector3 zAside=dir.CrossProduct(VForward);
  if( zAside.SquareSize()>aside.SquareSize() ) aside=zAside;
  arrow->SetPosition(pos);
  arrow->SetOrient(dir,aside);
  arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
  arrow->SetScale(size);
  arrow->SetConstantColor(color);

  ShowObject(arrow, now);
}

void Scene::DrawDiagArrow(RString text, Vector3Par pos, Vector3Par dir, float size, ColorVal color, bool now)
{
  DrawDiagArrow(pos,dir,size,color,now);
  // make priority dependent on the arrow size
  int prior = toInt(size*10);
  saturate(prior,2,20);
  DrawDiagText(text,pos+dir.Normalized()*size*0.8,prior);
}

void Scene::DrawDiagSphere(Vector3Par pos, float size, ColorVal color, bool now)
{
  LODShape *sphere=Preloaded(SphereModel);
 
  size /= sphere->BoundingSphere();
  if (size<1e-4) return; // no need to show zero sized or very small forces

  Ref<Object> obj=new ObjectColored(sphere,VISITOR_NO_ID);
  obj->SetTransform(M4Identity);
  obj->SetPosition(pos);  
  obj->SetPosition(obj->RenderVisualState().PositionModelToWorld(sphere->BoundingCenter()*size));
  obj->SetScale(size);
  obj->SetConstantColor(color);
  //ObjectForDrawing(arrow);

  ShowObject(obj,now);
}

#include <Es/Memory/normalNew.hpp>

/// object used for light volumetric
class ObjectLight: public ObjectColored
{
  typedef ObjectColored base;

  Color _emissiveTemp;

  public:
  ObjectLight(LODShape *shape, const CreateObjectId &id)
  :base(shape,id)
  {
  }
  virtual float MinPixelArea(float minPixelAuto) const;

  virtual void Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2);
  virtual void Deanimate(int level, bool setEngineStuff);

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(ObjectLight)

/*!
\patch 5129 Date 2/19/2007 by Ondra
- Fixed: Colored positional lights were rendered white.
*/
void ObjectLight::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  if (!setEngineStuff) return;

  ShapeUsed shape = _shape->Level(level);
  if (shape->NSections()>0)
  {
    Assert(shape->NSections()==1);
    TexMaterial *mat = shape->GetSection(0).GetMaterialExt();
    if (mat)
    {
      mat->Load();
      _emissiveTemp = mat->_emmisive;
      mat->_emmisive = mat->_emmisive * GetConstantColor();
    }
  }
}
void ObjectLight::Deanimate(int level, bool setEngineStuff)
{
  if (!setEngineStuff) return;

  ShapeUsed shape = _shape->Level(level);
  if (shape->NSections()>0)
  {
    Assert(shape->NSections()==1);
    TexMaterial *mat = shape->GetSection(0).GetMaterialExt();
    if (mat)
    {
      mat->Load();
      mat->_emmisive = _emissiveTemp;
    }
  }
}

float ObjectLight::MinPixelArea(float minPixelAuto) const
{
  // we want to render lights even when they are one pixel only, perhaps even less
  // (we rather want to see a flickering light than not see it at all)
  return floatMin(1.0,minPixelAuto*0.5f);
}

void Scene::IRLasers::Register(const Person *person, bool reg, Vector3Val pos, Vector3Val dir)
{
  if (!person) return;

  int index = -1;

  // find if inserted yet
  for (int i = 0; i < _irLasers.Size(); i++)
  {
    if (_irLasers[i]._person == person) { index = i; break; }
  }

  if (reg)
  {
    if (index == -1)
    {
      // append
      IRRaySource &info = _irLasers.Append();
      info._person = person;
      info._pos = pos;
      info._dir = dir;
    }
    else
    {
      // update
      IRRaySource &info = _irLasers[index];
      Assert(info._person == person);
      info._pos = pos;
      info._dir = dir;
    }
  }
  else
  {
    if (index != -1) 
    {
      // remove
      _irLasers.Delete(index);
    }
  }
}

void Scene::IRLasers::Draw()
{
  if (_irLasers.Size() == 0) return;

  // visible in NVG & Ti
  if (GEngine->GetNightVision() || GEngine->GetThermalVision())
  {
    Vector3 pos = GScene->GetCamera()->Position();
    Vector3 dir = GScene->GetCamera()->Direction();

    const float MaxDist = 100.0f;
    const float MaxDist2 = MaxDist * MaxDist;

    AUTO_STATIC_ARRAY(IRRaySort, sort, 64);
    
    // perpendicular plane to player look vector
    Plane plane(dir, pos);

    // select probably visible
    for (int i = 0; i < _irLasers.Size(); i++)
    {
      IRRaySource &info = _irLasers[i];

      // source is too far to be visible
      if (info._pos.Distance2(pos) <  5.0f * MaxDist2)
      {
        // inside or outside plane
        info._isBehind = plane.Distance(info._pos) < 0.0f;
        // source is outside plane and is oriented to opposite side
        if (dir.DotProduct(info._dir) < 0 && info._isBehind) { continue; }
        // max reachable position
        info._endPos = info._pos + info._dir * MaxDist;
        // check max position
        if (info._isBehind && plane.Distance(info._endPos) < 0.0f) continue;

        IRRaySort &irSort = sort.Append();
        // ray - player distance
        float dot = (pos - info._pos).DotProduct(info._dir);
        Vector3 projPos = info._pos + dot * info._dir;
        irSort._distToPlayer = (pos - projPos).Size() / MaxDist;
        saturate(irSort._distToPlayer, 0.0f, 1.0f);
        irSort._ir = &info;
      }
    }

    // sort by beam distance to player
    QSort(sort, IRRaySort::Compare);

    // use max 16 beams
    if (sort.Size() > 16) sort.Resize(16);

#define _SHOW_BEAMS_COUNT_ 0

#if _SHOW_BEAMS_COUNT_
    int count = 0;
#endif

    const Person *person = GWorld->FocusOn() ? GWorld->FocusOn()->GetPerson() : NULL;
    float fov = 1.0f;
    if (!GWorld->GetCameraEffect() && person)
    {
      fov = person->GetCameraFOV() + 0.3f;
      saturate(fov, 0.1f, 1.5f);
    }

    // calculate beam end position
    for (int i = 0; i < sort.Size(); i++)
    {
      IRRaySource &info = *sort[i]._ir;

      // collision with ground
      Vector3 tmpEnd;
      float tGround = GLandscape->IntersectWithGroundOrSea(&tmpEnd, info._pos, info._dir, 0, MaxDist);

      // we've hit the ground, check if there is an object collision
      if (tGround < MaxDist) info._endPos = tmpEnd;

      // start & end point behind plane
      if (info._isBehind && plane.Distance(info._endPos) < 0) continue;

      CollisionBuffer result;
      GLandscape->ObjectCollisionLine(GScene->GetCamera()->GetAge(),result, Landscape::FilterIgnoreOne(info._person), info._pos, info._endPos, 0/*0.5f*/, ObjIntersectView, ObjIntersectFire, CF_NEAREST_CONTACT);

      // select the nearest
      if (result.Size() > 0)
        info._endPos = result[0].pos;

      // start & end point behind plane
      if (info._isBehind && plane.Distance(info._endPos) < 0)
        continue;

#if _SHOW_BEAMS_COUNT_
      count++;
#endif

      GScene->DrawIRRay(info._pos, info._dir, (info._endPos - info._pos).Size(), fov * sort[i]._distToPlayer);
    }
#if _SHOW_BEAMS_COUNT_
    DIAG_MESSAGE(100, Format("IR beams drawn: %d", count));
#endif
  }
}

void Scene::AddIRRay(const Person *person, bool add, Vector3Val pos, Vector3Val dir)
{
  _irLasers.Register(person, add, pos, dir);
}

void Scene::DrawIRRays()
{
	PROFILE_SCOPE_EX(drawIR, wPrep);

  if (GEngine->GetNightVision()) _irLasers.Draw();
}

void Scene::DrawIRRay(Vector3Val pos, Vector3Val dir, float size, float dist)
{
  LODShape *rayModel = _preloaded[RayModel];
  Assert(rayModel);

  Ref<Object> object = new ObjectColored(rayModel, VISITOR_NO_ID);
  static Color color(0.0f, 1.0f, 0.0f, 1.0f);
  object->SetConstantColor(color);
  
  Matrix4 matrix(MTranslation, pos);
  matrix.SetDirectionAndUp(dir, VUp);

  static float MinScale = 0.15f;
  static float MaxScale = 3.0f;
  const float XYScale = MinScale + dist * (MaxScale - MinScale);
  float scaleMult = floatMax(0.01f, size); // collision point / model size in Z axis
  Vector3 scaleVec(XYScale, XYScale, scaleMult);
  Vector3 center(0, 0, 0.5 * scaleMult); //scaled bb center

  Matrix4 scale(MScale, scaleVec.X(), scaleVec.Y(), scaleVec.Z());
  Matrix4 transl(MTranslation, center);
  Matrix4 final = matrix * transl * scale;
  
  object->SetTransform(final);
  ObjectForDrawing(object, 0, ClipAll, false);
}

void Scene::DrawFlashLight(LODShape *shape, Vector3Val pos, Vector3Val dir, ColorVal color, Vector3Val scale)
{
  Ref<Object> object = new ObjectLight(shape,VISITOR_NO_ID);

  Color c = color;
  // Draw
  c.SetA(MainLight()->NightEffect());
  object->SetConstantColor(c);
  
  static float scaleDistance = 0.2f;
  Vector3 center = Vector3(shape->BoundingCenter()*scaleDistance);

  Matrix4 scaleM(MScale, scale.X(), scale.Y(), scale.Z());
  Matrix4 transl(MTranslation, pos);
  Matrix4 trnasLoc(MTranslation, center);

  transl.SetDirectionAndUp(dir, VUp);

  // final transformation matrix
  Matrix4 matrix = transl * trnasLoc * scaleM;
  object->SetTransform(matrix);

  ObjectForDrawing(object, 0, ClipAll, false);
}

void Scene::DrawVolumeLight(LODShape *shape, ColorVal color, const Frame &pos, float size, bool orthogonalToPlayer)
{
  // Volume lights not visible when using thermal vision
  if (GEngine->GetThermalVision()) return;

  // Calculate the object color
  Color c = color;
//   float maxValue = floatMax(c.R(), c.G());
//   maxValue = floatMax(maxValue, c.B());
//   float invAccom = 1.0f / GEngine->GetAccomFactor();
//   if (maxValue > invAccom)
//   {
//     c = c*(invAccom/maxValue);
//   }

  // Create the object

  Ref<Object> object = new ObjectLight(shape,VISITOR_NO_ID);

  // Draw
  c.SetA(MainLight()->NightEffect());
  object->SetConstantColor(PackedColor(c));
  object->SetTransform(pos.Transform());
  Vector3 objectPosition = object->RenderVisualState().PositionModelToWorld(shape->BoundingCenter() * size);

  if (orthogonalToPlayer)
  {
    Matrix3 orient;
    orient.SetDirectionAndUp(objectPosition - _camera->Position(), Vector3(0, 1, 0));
    object->SetOrientation(orient);
  }
  
  object->SetPosition(objectPosition);
  object->SetScale(size);

  ObjectForDrawing(object, 0, ClipAll,false);
}

/*!
\patch 1.24 Date 8/16/2001 by Ondra
- Fixed: Random crash during shadow rendering.
*/
#if _VBS3 
  #include "soldierOld.hpp"
#endif


void Scene::ReleaseDrawObject(SortObject *oi)
{  
#if _HELISIM_RENDER_TO_TEXTURE
  for (int d = 0; d < Object::NumDrawObjects; d++)
    _drawObjectsStore[d].DeleteKey(oi);
#else
  _drawObjects.DeleteKey(oi);
#endif
}

struct FrustumEdge
{
	int	_p0, _p1;

	FrustumEdge(int p0, int p1): _p0(p0), _p1(p1){}
	FrustumEdge(){}
};

TypeIsSimpleZeroed(FrustumEdge);

bool Scene::Calcul2DEdges(float dist, float gridSize, int range, bool clampedges, FlexibleLoop2& loop, GridRectangle& brect)
{
	AutoArray< FrustumEdge,MemAllocLocal<FrustumEdge,5> > edges;

	loop._beg = loop._end = 0;
	loop._zMajor = false;
	loop._scanLines.Resize(0);

	float invGridSize = 1.0 / gridSize;

	//first, project corners of frustum
	const int frustumCorners = 5;

	Point3 corner[frustumCorners+1];
	float cFar = dist;

	Matrix4Val invView = _camera->Transform();

	float cLeftFar = _camera->Left() * dist;
	float cTopFar = _camera->Top() * dist;
	corner[0] = _camera->Position();
	corner[1].SetFastTransform(invView,Vector3(-cLeftFar,+cTopFar,cFar));
	corner[2].SetFastTransform(invView,Vector3(+cLeftFar,+cTopFar,cFar));
	corner[3].SetFastTransform(invView,Vector3(-cLeftFar,-cTopFar,cFar));
	corner[4].SetFastTransform(invView,Vector3(+cLeftFar,-cTopFar,cFar));
	corner[5].SetFastTransform(invView,Point3(0,0,cFar));

	{
		Coord xMin,xMax,zMin,zMax;
		// always include camera position
		xMin=xMax=corner[0].X();
		zMin=zMax=corner[0].Z();
		for( int i=1; i<frustumCorners+1; i++ )
		{ 
			Coord x=corner[i].X(),z=corner[i].Z();
			saturateMin(xMin,x);
			saturateMin(zMin,z);
			saturateMax(xMax,x);
			saturateMax(zMax,z);
		}
		brect.xBeg = toIntFloor(xMin*invGridSize)-1; // leave some reserve
		brect.zBeg = toIntFloor(zMin*invGridSize)-1;
		brect.xEnd = toIntCeil(xMax*invGridSize)+1;
		brect.zEnd = toIntCeil(zMax*invGridSize)+1;

		// view frustum may contain something outside of the circle
		// but we never need to display more than a sphere describes
		// what is beyond the range is never visible anyway
		GridRectangle circleRect;

		Vector3Val pos = _camera->Position();
		circleRect.xBeg = toIntFloor((pos.X()-dist)*invGridSize);
		circleRect.zBeg = toIntFloor((pos.Z()-dist)*invGridSize);
		circleRect.xEnd = toIntCeil((pos.X()+dist)*invGridSize);
		circleRect.zEnd = toIntCeil((pos.Z()+dist)*invGridSize);

    brect = brect&circleRect;
	}

	Vector3Val dir = _camera->Direction();

	// orientation of flexible  loop - minor/major can be x or z
	bool zMajor = fabs(dir.Z())>fabs(dir.X());

	//Realize orientation of clipping planes in respect to Y+
	{
		//avoid tiny edges when we are *nearly* perpendicular
		const float epsilon = 0.005f;

		bool cRight = _camera->_rClipPlane.Normal().DotProduct(VUp) > epsilon;
		bool cLeft = _camera->_lClipPlane.Normal().DotProduct(VUp) > epsilon;
		bool cTop = _camera->_tClipPlane.Normal().DotProduct(VUp) > epsilon;
		bool cBottom = _camera->_bClipPlane.Normal().DotProduct(VUp) > epsilon;
		bool cFar = _camera->_fClipPlane.Normal().DotProduct(VUp) > epsilon;

		//second, find edges of opposite oriented planes and add them to list
		//in clockwise order

		//test edge 0-1
		if(cLeft != cBottom) edges.Add(cLeft ? FrustumEdge(0, 1) : FrustumEdge(1, 0));
		//test edge 0-2
		if(cBottom != cRight) edges.Add(cRight ? FrustumEdge(2, 0) : FrustumEdge(0, 2));
		//test edge 0-3
		if(cLeft != cTop) edges.Add(cTop ? FrustumEdge(0, 3) : FrustumEdge(3, 0));
		//test edge 0-4
		if(cRight != cTop) edges.Add(cTop ? FrustumEdge(4, 0) : FrustumEdge(0, 4));
		//test edge 3-4
		if(cFar != cTop) edges.Add(cTop ? FrustumEdge(3, 4) : FrustumEdge(4, 3));
		//test edge 4-2
		if(cRight != cFar) edges.Add(cRight ? FrustumEdge(4, 2) : FrustumEdge(2, 4));
		//test edge 2-1
		if(cBottom != cFar) edges.Add(cBottom ? FrustumEdge(2, 1) : FrustumEdge(1, 2));
		//test edge 1-3
		if(cLeft != cFar) edges.Add(cLeft ? FrustumEdge(1, 3) : FrustumEdge(3, 1));
	}

	int verts[8];
	int numverts = edges.Size();

	//Sanity check
  if (numverts < 3) // HOTFIX: there was crash here caused by insane camera set (containing many -1.#IND000)
  {
    return false; // do not traverse land grid
  }

#if _DEBUG
	memset(verts, 0xff, sizeof(verts));
#endif

	//now, convert edges to vertexes to make closed polygon
	verts[0] = edges[0]._p0;
	int last = edges[0]._p1;

	for(int edge = 1; edge < numverts; edge++)
	{
		for(int c = 0; c < numverts; c++)
		{
			if(edges[c]._p0 == last)
			{
				verts[edge] = edges[c]._p0;
				last = edges[c]._p1;
				break;
			}
		}
	}

#ifdef _DEBUG
	for(int c = 0; c < numverts; c++)
	{
		Assert(verts[c] >= 0 && verts[c] < frustumCorners);
	}
#endif


	//HACK! Enlarge frustum corners in 2D to match epsilons introduced in original GridRectangle
	//enlarge corners in the correct directions - by vertex normal
	Vector3 normals[frustumCorners+1];

/* //Just for sanity check that shift is pointing outside of polygon. Keep it here, please

	Vector3 center(0,0,0);
	
	for(int c = 0; c < numverts; c++)
	{
		center = center + corner[c];
	}
	center = center * (1.0f / numverts);
*/
	float enlarge = gridSize;
	for(int c = 0; c < numverts; c++)
	{
		int next = (c + 1) < numverts ? c + 1 : 0;
		int prev = (c - 1) >= 0 ? c - 1 : numverts - 1;

		Vector3 vprev = corner[verts[c]] - corner[verts[prev]];
		Vector3 vnext = corner[verts[c]] - corner[verts[next]];
		vprev[1] = 0;
		vnext[1] = 0;
		vprev.Normalize();
		vnext.Normalize();

		//let's compute length of vertex offset
		float dot = vprev.DotProduct(vnext);
		//avoid numeric imprecisions
		saturate(dot, -1.0f, 1.0f);
		float alpha = acosf(dot) * 0.5f;
		float sinAlpha = sinf(alpha);
		//avoid singularities
		saturateMax(sinAlpha, 0.05f);
		float x = 1.0f / sinAlpha;
		//keep it always >=2.0
		saturateMax(x, 2.0f);
		//shift is in normal direction
		Vector3 edgeNorm0 = vprev.CrossProduct(VUp);
		Vector3 edgeNorm1 = VUp.CrossProduct(vnext);
		Vector3 vertNorm = (edgeNorm0 + edgeNorm1).Normalized();

		//check that we are pointing in the "out" direction
		//float vnormDot = vertNorm.DotProduct((center - corner[verts[c]]).Normalized());
		//DoAssert(vnormDot < 0);

		normals[c] = vertNorm * x * enlarge;
	}

	//the real shift
	for(int c = 0; c < numverts; c++)
	{
		corner[verts[c]] += normals[c];
	}

	int minv = 0, maxv = 0;

	float mins, maxs;
	mins = maxs = zMajor ? corner[verts[0]].Z() : corner[verts[0]].X();

	//find the top/bottom of polygon
	//Note that only edge verts are involved! That's why the rescale code is separated
	for(int c = 1; c < numverts; c++)
	{
		int i = verts[c];
		float v = zMajor ? corner[i].Z() : corner[i].X();

		if(v < mins)
		{
			mins = v;
			minv = c;
		}
		if(v > maxs)
		{
			maxs = v;
			maxv = c;
		}
	}

	int iMins = toIntFloor(mins * invGridSize);
	int iMaxs = toIntCeil(maxs * invGridSize);

	//remember unclipped min, as we need to rasterize it too
	int iUnclippedMin = iMins;

	//clip by brect
	if(zMajor)
	{
		iMins = intMax(iMins, brect.zBeg);
		iMaxs = intMin(iMaxs, brect.zEnd + 1);
	}
	else
	{
		iMins = intMax(iMins, brect.xBeg);
		iMaxs = intMin(iMaxs, brect.xEnd + 1);
	}

	//store results to FlexibleLoop
	if(clampedges)
	{
		//it's really hell to clamp polygon at major axis.
		//for now, just turn the clipped rectangle to scan-lines.
		if(iMins < 0 || iMaxs > range)
		{
			int xMin=brect.xBeg,xMax=brect.xEnd;
			int zMin=brect.zBeg,zMax=brect.zEnd;
			if( xMin<0 ) xMin=0;if( xMin>range-1 ) xMin=range-1;
			if( xMax<0 ) xMax=0;if( xMax>range-1 ) xMax=range-1;
			if( zMin<0 ) zMin=0;if( zMin>range-1 ) zMin=range-1;
			if( zMax<0 ) zMax=0;if( zMax>range-1 ) zMax=range-1;
			// convert end inclusive to end exclusive
			xMax++,zMax++;
			if(zMajor)
			{
				int zStep = dir.Z()<0 ? -1 : +1;

				loop._majorInc = zStep;
				if(zStep > 0)
  {
					loop._beg = zMin;
					loop._end = zMax;
				}
				else
    {
					loop._beg = zMax - 1;
					loop._end = zMin - 1;
    }
				loop._scanLines.Reserve(zMax - zMin);
				for(int z = zMin; z < zMax; z++)
					loop._scanLines.Add(FrustumScanLine(xMin, xMax));

				loop._majorOffset = zMin;
				loop._zMajor = true;
  }
  else
  {
				int xStep = dir.X()<0 ? -1 : +1;

				loop._majorInc = xStep;
				if(xStep > 0)
				{
					loop._beg = xMin;
					loop._end = xMax;
				}
				else
    {
					loop._beg = xMax - 1;
					loop._end = xMin - 1;
    }
				loop._scanLines.Reserve(zMax - zMin);
				for(int x = xMin; x < xMax; x++)
					loop._scanLines.Add(FrustumScanLine(zMin, zMax));
    
				loop._majorOffset = xMin;
				loop._zMajor = false;
			}
			return true;
		}
	}

	if(zMajor)
	{
		int zStep = dir.Z()<0 ? -1 : +1;

		loop._majorInc = zStep;
		if(zStep > 0)
		{
			loop._beg = iMins;
			loop._end = iMaxs;
		}
		else
		{
			loop._beg = iMaxs - 1;
			loop._end = iMins - 1;
		}
		loop._majorOffset = iMins;
		loop._zMajor = true;
	}
	else
	{
		int xStep = dir.X()<0 ? -1 : +1;
    
		loop._majorInc = xStep;
		if(xStep > 0)
		{
			loop._beg = iMins;
			loop._end = iMaxs;
		}
		else
    {
			loop._beg = iMaxs - 1;
			loop._end = iMins - 1;
		}
		loop._majorOffset = iMins;
		loop._zMajor = false;
    }

	//Rasterize
	if(iMins < iMaxs)
	{
		float epsilon = 1.0f - invGridSize;

		float scorner[frustumCorners][2];

		//rescale to grid-size
		for(int c = 0; c < frustumCorners; c++)
		{
			scorner[c][0] = corner[c].X() * invGridSize;
			scorner[c][1] = corner[c].Z() * invGridSize;
  }

		loop._scanLines.Reserve(iMaxs - iMins);

		if(zMajor)
		{
			//rasterize as horizontal scan-lines
			int leftCount = 0;
			int rightCount = 0;
			int leftVert, rightVert;

			leftVert = rightVert = minv;
			float leftDx = 0, rightDx = 0;
			float leftX = 0, rightX = 0;
			float leftDDx = 0, rightDDx = 0;

			for(int line = iUnclippedMin; line < iMaxs; line++)
			{
				//new left edge?
				while(leftCount <= 0)
  {
					int v0 = verts[leftVert];
					//going counter-clockwise, so next on left is +1
					if(++leftVert >=numverts)
						leftVert = 0;
					int v1 = verts[leftVert];

					float y0 = scorner[v0][1];
					float y1 = scorner[v1][1];

					y0 = fastFloor(y0);
					y1 = fastCeil(y1);

					float dy = y1 - y0;
					leftCount = dy;

					//skip horizontal edge!
					if(leftCount > 0)
					{
						leftX = fastFloor(scorner[v0][0]-epsilon);
						leftDx = (fastFloor(scorner[v1][0]-epsilon) - leftX) / dy;
						leftDDx = 0.0f;
						break;
					}
				}

				//new right edge?
				while(rightCount <= 0)
				{
					int v0 = verts[rightVert];
					//going clockwise, so next on right is -1
					if(--rightVert < 0)
						rightVert = numverts - 1;
					int v1 = verts[rightVert];

					float y0 = scorner[v0][1];
					float y1 = scorner[v1][1];

					y0 = fastFloor(y0);
					y1 = fastCeil(y1);

					float dy = y1 - y0;
					rightCount = dy;

					//skip horizontal edge!
					if(rightCount > 0)
					{
						rightX = fastCeil(scorner[v0][0]+epsilon);
						rightDx = (fastCeil(scorner[v1][0]+epsilon) - rightX) / dy;
						rightDDx = 0.0f;
						break;
					}
				}

				//brect clip
				if(line >= iMins)
    {
					//add scan-line
					int lx = toIntFloor(leftX + leftDDx * leftDx);
					int rx = toIntCeil(rightX + rightDDx * rightDx);
					//Fix rare case, when end of edges get crossed, because of numerical imprecision
					if(lx > rx)
      {
						Swap(lx, rx);
					}
					lx = intMax(lx, brect.xBeg);
					rx = intMin(rx, brect.xEnd);

					if(clampedges)
        {
						saturateMin(lx, range - 1);
						saturateMin(rx, range - 1);
						saturateMax(lx, 0);
						saturateMax(rx, 0);
        }

					loop._scanLines.Add(FrustumScanLine(lx, rx + 1));
      }
				leftDDx += 1.0f;
				rightDDx += 1.0f;
				leftCount--;
				rightCount--;
    }
  }
		else
		{
			//rasterize as vertical scan-lines
			int topCount = 0;
			int bottomCount = 0;
			int topVert, bottomVert;

			topVert = bottomVert = minv;
			float topDy = 0, bottomDy = 0;
			float topY = 0, bottomY = 0;
			float topDDy = 0, bottomDDy = 0;

			for(int line = iUnclippedMin; line < iMaxs; line++)
			{
				//new bottom edge?
				while(bottomCount <= 0)
  {
					int v0 = verts[bottomVert--];
					if(bottomVert < 0)
						bottomVert = numverts - 1;
					int v1 = verts[bottomVert];

					float x0 = scorner[v0][0];
					float x1 = scorner[v1][0];

					x0 = fastFloor(x0);
					x1 = fastCeil(x1);

					float dx = x1 - x0;
					bottomCount = dx;
					//skip vertical edge!
					if(bottomCount > 0)
    {
						bottomY = fastFloor(scorner[v0][1]-epsilon);
						bottomDy = (fastFloor(scorner[v1][1]-epsilon) - bottomY) / dx;
						bottomDDy = 0;
						break;
    }
  }

				//new top edge?
				while(topCount <= 0)
				{
					int v0 = verts[topVert];
					topVert = (topVert + 1) % numverts;
					int v1 = verts[topVert];

					float x0 = scorner[v0][0];
					float x1 = scorner[v1][0];

					x0 = fastFloor(x0);
					x1 = fastCeil(x1);

					float dx = x1 - x0;
					topCount = dx;

					//skip vertical edge!
					if(topCount > 0)
					{
						topY = fastCeil(scorner[v0][1]+epsilon);
						topDy = (fastCeil(scorner[v1][1]+epsilon) - topY) / dx;
						topDDy = 0;
						break;
					}
				}

				//brect clip
				if(line >= iMins)
  {
					//add scan-line
					int by = toIntFloor(bottomY + bottomDDy * bottomDy);
					int ty = toIntCeil(topY + topDDy * topDy);
					//Fix rare case, when end of edges get crossed, because of numerical imprecision
					if(ty < by)
    {
						Swap(by, ty);
    }
					ty = intMax(ty, by);
					by = intMax(by, brect.zBeg);
					ty = intMin(ty, brect.zEnd);
					if(clampedges)
    {
						saturateMin(by, range - 1);
						saturateMin(ty, range - 1);
						saturateMax(by, 0);
						saturateMax(ty, 0);
					}
					loop._scanLines.Add(FrustumScanLine(by, ty + 1));
				}
				bottomDDy += 1.0f;
				topDDy += 1.0f;
				topCount--;
				bottomCount--;
    }
  }

#if 0
		static bool doit = false;

		if(doit)
		{
			doit=false;
#pragma pack(1)

			struct TGARGBHeader
			{
				unsigned char	IDLenght;
				char				CMapType;
				char				ImageType;
				char				CMapSpec[5];
				short				XOffset;
				short				YOffset;
				short				Width;
				short				Height;
				char				BitsPerPixel;
				char				ImageDesc;
			};
#pragma pack()

			TGARGBHeader head={0,0,2,{0,0,0,0,0},0,0,0,0,32,32};

			head.Height = 512;
			head.Width = 512;
			FILE* ff = fopen("test.tga", "wb");
			if(ff)
			{
				fwrite(&head, sizeof(head), 1, ff);
				int data[512][512];
				memset(data, 0, sizeof(data));

				{
					GridRectangle brect;
					CalculBoundingRect(brect, dist, gridSize);

					for(int z = intMax(0,brect.zBeg); z <= intMin(511,brect.zEnd); z++)
						for(int x = intMax(0,brect.xBeg); x <= intMin(511,brect.xEnd); x++)
							data[z][x] = 0xffcf0000;
				}
				for(int line = iMins; line < iMaxs; line++)
				{
					if(line < 0 || line >= 512)
						continue;

					int left = intMax(loop._scanLines[line-iMins]._min, 0);
					int right = intMin(loop._scanLines[line-iMins]._max, 512);
					if((right - left) < 1)
						continue;

					if(zMajor)
  {
						for(int c = left; c < right; c++)
							data[line][c] = 0xff00cfcf;
					}
					else
    {
						for(int c = left; c < right; c++)
							data[c][line] = 0xff0000cf;
					}
    }

				//draw camera pos
				{
					Vector3 cpos = _camera->Position() * invGridSize;
					int cx = toIntFloor(cpos.X());
					int cz = toIntFloor(cpos.Z());
					if(cx >= 0 && cz < 512 &&
						cx >= 0 && cz < 512)
    {
						data[cz][cx] = 0xff40ff40;
    }
  }

				fwrite(data, sizeof(data), 1, ff);
				fclose(ff);
			}
		}
#endif

		return true;
	}

	return false;
}

void Scene::CalculBoundingRect(GridRectangle &brect, float dist, float grid)
{
  Point3 corner[8];
  float cLeftFar = _camera->Left() * dist;
  float cTopFar = _camera->Top() * dist;
  float cFar = dist;
  /* not true
  // no matter what the zoom is, we cannot see more than the max visibility range
  if (cLeftFar>cFar) cLeftFar = cFar;
  if (cTopFar>cFar) cTopFar = cFar;
  */

  Matrix4Val invView = _camera->Transform();

  //float invSize=1.0;
  // middle point
  corner[0] = _camera->Position();
  corner[1].SetFastTransform(invView,Point3(0,0,cFar));
  corner[2].SetFastTransform(invView,Vector3(-cLeftFar,-cTopFar,cFar));
  corner[3].SetFastTransform(invView,Vector3(+cLeftFar,-cTopFar,cFar));
  corner[4].SetFastTransform(invView,Vector3(-cLeftFar,+cTopFar,cFar));
  corner[5].SetFastTransform(invView,Vector3(+cLeftFar,+cTopFar,cFar));

  Coord xMin,xMax,zMin,zMax;
  // always include camera position
  xMin=xMax=corner[0].X();
  zMin=zMax=corner[0].Z();
  for( int i=1; i<6; i++ )
  { 
    Coord x=corner[i].X(),z=corner[i].Z();
    saturateMin(xMin,x);
    saturateMin(zMin,z);
    saturateMax(xMax,x);
    saturateMax(zMax,z);
  }
  float iGrid=1.0f/grid;
  brect.xBeg = toIntFloor(xMin*iGrid)-1; // leave some reserve
  brect.zBeg = toIntFloor(zMin*iGrid)-1;
  brect.xEnd = toIntCeil(xMax*iGrid)+1;
  brect.zEnd = toIntCeil(zMax*iGrid)+1;

  // view frustum may contain something outside of the circle
  // but we never need to display more than a sphere describes
  // what is beyond the range is never visible anyway
  GridRectangle circleRect;

  Vector3Val pos = _camera->Position();
  circleRect.xBeg = toIntFloor((pos.X()-dist)*iGrid);
  circleRect.zBeg = toIntFloor((pos.Z()-dist)*iGrid);
  circleRect.xEnd = toIntCeil((pos.X()+dist)*iGrid);
  circleRect.zEnd = toIntCeil((pos.Z()+dist)*iGrid);

  brect = brect&circleRect;

#if _VBS3_LARGE_OBJECTS
  // to prevent large objects disappearing, we need to search for them in static and dynamic R-tree
  // and adjusting scene bounding rectangle accordingly
  if (grid == ObjGrid) // we need to do this only for the object grid, not for cloud, clutter, etc. grids
  {
    int xMin2, xMax2, zMin2, zMax2;
    ObjRadiusRectangle(xMin2, xMax2, zMin2, zMax2, pos, pos, 0);
    GridRectangle largeObjRect;
    largeObjRect.xBeg = xMin2;
    largeObjRect.zBeg = zMin2;
    largeObjRect.xEnd = xMax2;
    largeObjRect.zEnd = zMax2;

    if (!largeObjRect.IsEmpty())
      brect = brect+largeObjRect;
  }
#endif
}

// clear various global variables

static bool CheckBufferReleased(
  const LLink<LODShape> &shape, const ShapeBank::ContainerType *bank
)
{
  if (shape)
  {
    LogF(
      "Shape %s not released (%d refs).",
      cc_cast(shape->Name()),shape->RefCounter()
    );
  }
  return false;
};


void ClearShapes()
{
  //WeaponInfos.Clear();
  RoadTypes.Clear();
  // check if all shapes have been released
  #if !_RELEASE
	Shapes.ClearCache();
	Shapes.ForEachF(CheckBufferReleased);
  #endif
  Shapes.Clear();
	Shapes.ClearCache();
}


bool Scene::ShadowPos(
  Vector3Par pos, Vector3 &aprox, LightSun *light, float maxShadow
) const
{ // 
  Vector3Val dir=light->ShadowDirection();
  if( dir.Y()>0 )
  {
    return false; // shadow casted up - no real shadow
  }
  float t = GetLandscape()->IntersectWithGround(&aprox,pos,dir,0,maxShadow*1.1);
  if (t>maxShadow)
  {
    // no intersection
    return false;
  }
  if (t<=0.01f)
  {
    // up -> on surface required
    aprox[1] = _landscape->SurfaceY(aprox[0],aprox[2]);
  }
  return true;
}

float Scene::CheckShadowSize(
  const Vector3 *minMax, Matrix4Par frame,
  float knownShadowSize
) const
{
  // Get the shadow direction
  Vector3Val dir = MainLight()->ShadowDirection();

  // Shadow cannot be casted up
  if (dir.Y() > 0) return 0;

  // Get the maximum length of the shadow
  const float maxShadowSize = Glob.config.GetMaxShadowSize();

  // If model stays almost straight, consider only upper 4 points
  int startY = frame.DirectionUp().Y()>0.996194f; // cos 5 deg

  //Vector3 longestPos = Vector3(0, 0, 0);

  // Go through all interesting model points (cube corners) and cast shadows out of them
  for (int y=startY; y<2; y++)
  for (int z=0; z<2; z++)
  for (int x=0; x<2; x++)
  {
    Vector3 modelPoint(minMax[x].X(),minMax[y].Y(),minMax[z].Z());
    Vector3 pos(VFastTransform, frame, modelPoint);

    // do not test the part which is already known to be necessary
    pos += knownShadowSize*dir;
    float testSize = maxShadowSize*1.01f-knownShadowSize;
    
    float t = GetLandscape()->IntersectWithGround(NULL,pos,dir,0,testSize);
    if (t<=0) continue;

    //longestPos = Vector3(VFastTransform, frame, modelPoint);

    saturateMax(knownShadowSize, t+knownShadowSize);
    // if the shadow is long enough, break - it will not be any longer
    if (knownShadowSize>=maxShadowSize*0.99f) return knownShadowSize;
  }

//  //if (knownShadowSize >= 10.5f)
//  {
//    DrawDiagArrow(longestPos + Vector3(0, 1, 0), dir, knownShadowSize);
//  }

  return knownShadowSize;
}


void Scene::PrepareDraw()
{
  if( _camera )
  {
    _camera->Adjust(GEngine);
  }

	//store to float, to avoid LHS
	_engineWidth = GEngine->Width();
	_engineHeight = GEngine->Height();
	//avoid area recomputing
	_engineArea = _engineWidth * _engineHeight;
}

void Scene::BeginObjects()
{
  static bool manualCleanup = true;
  #if USE_FAST_ALLOCATORS
    // each structure which allocates and deallocates a lot in each frame benefits a lot from manual cleanup
    // perhaps we could somehow make such behaviour more general?
    SortObject::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjStoreDrawMatrices::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDraw<true>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDraw<false>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDrawInstanced<true>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDrawInstanced<false>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDrawShadow<true>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDrawShadow<false>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDrawShadowInstanced<true>::_allocatorF.SetManualCleanUp(manualCleanup);
    ObjDrawShadowInstanced<false>::_allocatorF.SetManualCleanUp(manualCleanup);
    void LandscapeSetManualCleanUp(bool manualCleanup);
    LandscapeSetManualCleanUp(manualCleanup);
  #endif

  //_drawObjects.Resize(0);

  _shadowCache.CleanUp(); // remove old shadows

  // calculate shadow intensity based on diffuse/ambient relation
  {
    // Get the sun object
    LightSun *sun = MainLight();

    // Get and modify the ambient color (make the ambient color only reasonable small, the shadows have
    // problems with precision if the Diffuse/Ambient is too big - that's because we have only 16 grades
    // of a shadow)
    Color ambient;
    {
      static float maxDifAmbRatio = 10.0f;
      Color c = sun->GetDiffuse() / sun->GetAmbient();
      float maxDif = floatMax(c.R(), c.G(), c.B());
      if (maxDif > 0.0f) // The diffuse color can be 0 in certain cases (no sun, no moon)
      {
        float multCoef = maxDif / floatMin(maxDif, maxDifAmbRatio);
        ambient = sun->GetAmbient() * multCoef;
        ambient.SetA(sun->GetAmbient().A()); // Keep the original alpha just for all cases
      }
      else
      {
        ambient = sun->GetAmbient();
      }

      // For backward compatibility, keep the ambient as it is during the day
      float somCoef = sun->SunOrMoon();
      ambient = ambient * (1.0f - somCoef) + sun->GetAmbient() * somCoef;
    }

    // Set the ambient and diffuse color to the engine
    float shadowFactor = ShadowFactor();
    GEngine->SetShadowFactor(ambient, sun->GetDiffuse(), shadowFactor);
  }

}

void Scene::EndObjects()
{
  // fast allocator for SortObject requires a manual regular cleanup
  // we do it after all new SortObject-s are created 
#if USE_FAST_ALLOCATORS
    SortObject::_allocatorF.CleanUp();
    ObjStoreDrawMatrices::_allocatorF.CleanUp();
    ObjDraw<true>::_allocatorF.CleanUp();
    ObjDraw<false>::_allocatorF.CleanUp();
    ObjDrawInstanced<true>::_allocatorF.CleanUp();
    ObjDrawInstanced<false>::_allocatorF.CleanUp();
    ObjDrawShadow<true>::_allocatorF.CleanUp();
    ObjDrawShadow<false>::_allocatorF.CleanUp();
    ObjDrawShadowInstanced<true>::_allocatorF.CleanUp();
    ObjDrawShadowInstanced<false>::_allocatorF.CleanUp();
    void LandscapeManualCleanUp();
    LandscapeManualCleanUp();
#endif
}

SortObject *Scene::FindSortObject(Object * obj
#if _HELISIM_RENDER_TO_TEXTURE
                                  , int drawObjId
#endif                                  
                                  )
{
  // check grid, identity our group owner
  const ObjectListFull *list = GetLandscape()->GetList(obj).GetList();
  if (!list) return NULL;
  int index = 0;
  for (int i=0; i<list->NGroups(); i++)
  {
    const ObjectListGroup &grp = list->Group(i);
    int grpStart = index;
		for ( ;index<grp._end; index++)
		{
		  if (list->Get(index)==obj)
		  {
		    return list->Get(grpStart)->GetInList(
#if _HELISIM_RENDER_TO_TEXTURE
          drawObjId
#endif
          );
		  }
		}
  }
  return NULL;
}

#if _VBS3_NO_SEABED
bool Scene::UnderWater()
{
  const Vector3& camPos = _camera->Position();
  return camPos.Y() < _landscape->GetSeaLevel(camPos);
}
#endif


void Scene::RenderDebugShapes()
{
	if (_enfShapeCollection)
			_enfShapeCollection->RenderShapes();
}

int Scene::GetEnfDebugShapesCount() const
{
	return _enfShapeCollection ? _enfShapeCollection->GetCount() : 0;
}

void Scene::ClearEnfShapeCollection(bool all)
{
	if (_enfShapeCollection)
	{
		_enfShapeCollection->FlushShapes(all ? enf::SS_ONCE : enf::SS_PROCESSED);
	}
}

DEFINE_FAST_ALLOCATOR(ObjStoreDrawMatrices)
DEFINE_FAST_ALLOCATOR(ObjDraw<true>)
DEFINE_FAST_ALLOCATOR(ObjDraw<false>)
DEFINE_FAST_ALLOCATOR(ObjDrawInstanced<true>)
DEFINE_FAST_ALLOCATOR(ObjDrawInstanced<false>)
DEFINE_FAST_ALLOCATOR(ObjDrawShadow<true>)
DEFINE_FAST_ALLOCATOR(ObjDrawShadow<false>)
DEFINE_FAST_ALLOCATOR(ObjDrawShadowInstanced<true>)
DEFINE_FAST_ALLOCATOR(ObjDrawShadowInstanced<false>)

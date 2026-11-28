/*!
\file
Ground clutter handling
*/

#include "wpch.hpp"
#include "landImpl.hpp"
#include "landscape.hpp"
#include "global.hpp"
#include "world.hpp"
#include "txtPreload.hpp"
#include "diagModes.hpp"
#include "scene.hpp"
#include "camera.hpp"
#include <El/Common/perfProf.hpp>
#include <El/PerlinNoise/perlinNoise.hpp>
#include <Es/Types/scopeLock.hpp>
#include <Es/Threads/multisync.hpp>
#include <El/Multicore/jobs.hpp>
#include "fileLocator.hpp"
#include "gridEventLogger.hpp"
#include "keyInput.hpp"
#include "dikCodes.h"

//#pragma optimize ("", off)

class GroundClutterInfo;
/// information which is enough to construct an object instance from GroundClutterInfo
struct GroundClutterDesc
{
  /// the info must exist - this is normally in _clutterData
  GroundClutterInfo *info;
  /// precalculated disappearing factor
  float disappear;
  //@{ indices into the lighting grid
  short lx,lz;
  //@}
};

TypeIsSimple(GroundClutterDesc)

/// info about a random instance
/**
Note: we store large amounts of this. Keep the structure small
*/

class GroundClutterInfo
{
  public:
  /// position of ground clutter
  Vector3 _pos;
  /// orientation, including scale
  Matrix3 _orient;
  // XZ radius of the object
  float _radius;
  /// random seed
  int _seedXZ;
  //@{ surface derivatives - used for skew matrix
  float _dX,_dZ;
  //@}
  /// ground normal used for lighting
  Vector3 _norm;
  /// adjust color as needed
  Color _colorRatio;
  /// several precomputed random values to be used as needed
  unsigned char _randomValueB[4];
  /// store scale separately
  float _scale;
  /// how much was the grass flattened  (includes direction)
  float _flattenSkewX,_flattenSkewZ;
  // when was the grass last flattened (to _flatten)
  Time _flattenTime;

  void SetSeedXZ(int seed, Landscape *l)
  {
    _seedXZ = seed;
    for (int i=0; i<lenof(_randomValueB); i++)
    {
      float rand = l->_randGen.RandomValue(_seedXZ+5+i);
      int rb = toInt(rand*255);
      saturate(rb,0,255);
      _randomValueB[i] = rb;
    }
  }
  float GetRandomValue(int i) const {return _randomValueB[i]*(1.0f/255);}
  
  /// create a data needed for instanced rendering  
  void ConvertToInstance(
    ObjectInstanceInfo &info, float disappear,
    const Clutter &lClutter, const WindEmitterList &windList, float groundDiffuseCoef
  );

  void Flatten(const Entity *flattener);
};

TypeIsSimple(GroundClutterInfo)

/// table based sin/cos computation

class SinCosCalculator
{
  // note: we need power of two sized table
  static const int tableSize=256;
  static const int cosOffset = tableSize*3/4;
  
  float _sin[tableSize];
  public:
  SinCosCalculator()
  {
    for (int i=0; i<lenof(_sin); i++)
    {
      float x = float(i)/lenof(_sin)*(H_PI*2);
      _sin[i] = sin(x);
    }
  }
  
  /**
  @param x assumed 0..1
  */
  float Circle(float x, float &cos) const
  {
    float xPart = x-toIntFloor(x);
    int xI = toInt(xPart*lenof(_sin));
    cos = _sin[(xI+cosOffset)&(lenof(_sin)-1)];
    return _sin[xI&(lenof(_sin)-1)];
  }
  /**
  @param x assumed in radians
  */
  float Sin(float x) const
  {
    x *= 1/(H_PI*2);
    float xPart = x-toIntFloor(x);
    int xI = toInt(xPart*lenof(_sin));
    return _sin[xI&(lenof(_sin)-1)];
  }
  float Cos(float x) const
  {
    x *= 1/(H_PI*2);
    float xPart = x-toIntFloor(x);
    int xI = toInt(xPart*lenof(_sin));
    return _sin[(xI+cosOffset)&(lenof(_sin)-1)];
  }
} SinCos;

const float fullFlatten = 3;
const float startRaisingTime = 30.0f;
const float endRaisingTime = 90.0f;

void GroundClutterInfo::Flatten(const Entity *flattener)
{
  float flattenSkewX = 0, flattenSkewZ = 0;
  float flattenFactor = flattener->FlattenGrass(flattenSkewX,flattenSkewZ,_pos,_radius);

  float flattenDegree2 = Square(_flattenSkewX)+Square(_flattenSkewZ);
  float newFlattenDegree2 = Square(flattenSkewX)+Square(flattenSkewZ);
  if (newFlattenDegree2>=flattenDegree2)
  {
    // if we are already fully flattened, do not change flattening orientation
    // as normalization is not precise, we need to consider some epsilon
    if (flattenDegree2<Square(fullFlatten)*0.995f && newFlattenDegree2>flattenDegree2)
    {
      // never avoid flattening more than allowed maximum
      if (newFlattenDegree2>=Square(fullFlatten))
      {
        float invSize = InvSqrtFast(newFlattenDegree2);
        float factor = fullFlatten*invSize;
        flattenSkewX *= factor;
        flattenSkewZ *= factor;
      }
      _flattenSkewX = flattenSkewX;
      _flattenSkewZ = flattenSkewZ;
    }
    _flattenTime = Glob.time;
  }
  else if (flattenFactor>=1)
  {
    // we are fully flattened, but we do not know in which direction - keep current state, but prevent raising up
    _flattenTime = Glob.time;
  }
}

#if 0 // _ENABLE_CHEATS
  static int WatchClutterX = 35;
  static int WatchClutterZ = 28;
#endif


void GroundClutterInfo::ConvertToInstance(
  ObjectInstanceInfo &info, float disappear, const Clutter &lClutter,
  const WindEmitterList &windList, float groundDiffuseCoef
)
{
  Assert(disappear<1);
  
  float skewX = 0;
  float skewZ = 0;
  if (_scale>lClutter._noWindAffectionScale)
  {
    Vector3 wind = GWorld->GetWindSlow(_pos,windList);
    
    // Amplitude
    const float amplitude = 0.02f;

    // Frequency
    const float frequency = 1.0f;

    // Frequency can change +-10%
    float frequencyCoef = 1.0f + GetRandomValue(0) * 0.2f - 0.1f;

    // Input of harmonical function
    float x = (GetRandomValue(1) + Glob.time.toFloat() * frequency * frequencyCoef);

    // Calculate the change in the wind direction
    float cos;
    float windChange = SinCos.Circle(x,cos) * amplitude;

    // Orthogonal amplitude is between 0% and 20% of original amplitude, regardless on direction
    float orthoAmplitudeCoef = GetRandomValue(2) * 0.4f - 0.2f;

    // Calculate the change in the direction orthogonal to the wind direction
    float windChangeOrtho = cos * amplitude * orthoAmplitudeCoef;

    // Calculate the skew matrix
    const float basicSlope = 1.0f/8.0f;
    
    // TODO: optimize division
    float windAffection = Interpolativ(_scale,lClutter._noWindAffectionScale,lClutter._fullWindAffectionScale,0,1);
    
    skewX = (wind.X() * basicSlope + wind.X() * windChange - wind.Z() * windChangeOrtho) * windAffection;
    skewZ = (wind.Z() * basicSlope + wind.Z() * windChange + wind.X() * windChangeOrtho) * windAffection;
  }
  
  if (_scale>lClutter._noFlattenScale)
  {
    float flattenSkewX = 0, flattenSkewZ = 0;
    if (_flattenTime>Glob.time-endRaisingTime)
    {
      float age = Glob.time-_flattenTime;
      if (age<startRaisingTime)
      {
        flattenSkewX = _flattenSkewX;
        flattenSkewZ = _flattenSkewZ;
      }
      else 
      {
        float ageFactor = InterpolativC(age,startRaisingTime,endRaisingTime,1.0f,0.0f);
        flattenSkewX = _flattenSkewX*ageFactor;
        flattenSkewZ = _flattenSkewZ*ageFactor;
      }
    }
    else
    {
      flattenSkewX = 0;
      flattenSkewZ = 0;
    }
    // TODO: optimize division
    float flattened = Interpolativ(_scale,lClutter._noFlattenScale,lClutter._fullFlattenScale,0,1);
    // if flattening, prefer the flattened skew over the wind
    //float windSkew = (1-flattened)+flattenRatio*flattened;
    float windSkew = 1;
    skewX = skewX*windSkew + flattenSkewX*flattened;
    skewZ = skewZ*windSkew + flattenSkewZ*flattened;
    
  }

  if (lClutter._noWindAffectionScale<=_scale || lClutter._noFlattenScale<=_scale)
  {
    // we need to know the flattening direction as well?

		float invSize = InvSqrtFast(1 + skewX * skewX + skewZ * skewZ);
//		float invSize = InvSqrt(1 + skewX * skewX + skewZ * skewZ);
    /*
    // skew multiplication adds skew amount
    Matrix3 windSkew = M3Identity;
    windSkew(0,1) = (skewX + dX)* invSize;
    windSkew(1,1) = invSize;
    windSkew(2,1) = (skewZ + dZ) * invSize;
    info._origin.SetOrientation(windSkew * orient);
    */
    /*
    Matrix3 windSkew = M3Identity;
    windSkew(0,1) = skewX * invSize;
    windSkew(1,1) = invSize * flattenRatio;
    windSkew(2,1) = skewZ * invSize;
    
    Matrix3 skew = M3Identity;
    skew(1,0) = _dX;
    skew(1,2) = _dZ;
    
    info._origin.SetOrientation(skew * windSkew * _orient);
    */

		//faster way, without full matrix multiplication
		float w0 = skewX * invSize;
		float w1 = invSize /** flattenRatio*/;
		float w2 = skewZ * invSize;

		Matrix3 skew;
		skew(0,0) = _orient(0,0);
		skew(1,0) = _orient(0,0)*_dX + _orient(2,0)*_dZ;
		skew(2,0) = _orient(2,0);

		skew(0,1) = _orient(1,1)*w0;
		skew(1,1) = _orient(1,1)*(_dX*w0 + w1 + _dZ*w2);
		skew(2,1) = _orient(1,1)*w2;

		skew(0,2) = _orient(0,2);
		skew(1,2) = _orient(0,2)*_dX + _orient(2,2)*_dZ;
		skew(2,2) = _orient(2,2);

		info._origin.SetOrientation (skew);
  }
  else
  {
    // orient already contains the skew for objects which are not affected by the wind
    info._origin.SetOrientation(_orient);
  }


  info._object = info._parentObject = lClutter._obj;

  LODShape *shape = lClutter._shape;
  Vector3Val bCenterR = info._origin.Rotate(shape->BoundingCenter());
  Vector3Val centerPos = bCenterR+_pos;

  
  saturateMax(disappear,0);
  
  info._origin.SetPosition(centerPos);

  // get sat.map coloring / detail texture
  info._color = _colorRatio;
  info._color.SetA(1 - disappear);
  // TODO: this could be optimized, we already know integral x/z
  info._shadowCoef = GScene->GetLandShadowSmooth(centerPos) * groundDiffuseCoef;

  #if 0 // _ENABLE_CHEATS
    if (CHECK_DIAG(DEModel))
    {
      int watchSeedXZ=GLandscape->_randGen.GetSeed(WatchClutterX,WatchClutterZ);
      if (watchSeedXZ==_seedXZ)
      {
        info._origin.SetPosition(info._origin.Position()+Vector3(0,0.5f,0));
        info._color = Color(1.5,0.5,0.5,info._color.A());
}
    }
  #endif
}

#include <Es/Memory/normalNew.hpp>

/// convert clutter type to index and back
class ClutterTypeIndex
{
  /// list of clutter types indexed
  FindArray<const Clutter *> _types;
  
  public:
  int Find(const Clutter *c) const {return _types.Find(c);}
  int Size() const {return _types.Size();}
  const Clutter *Get(int i) const {return _types.Get(i);}

  void Clear() {_types.Clear();}
  void Compact() {_types.Compact();}
  void Merge(const ClutterTypeIndex &with);
  int Add(const Clutter *c)
  {
    Assert(Find(c)<0);
    return _types.Add(c);
  }
};

void ClutterTypeIndex::Merge(const ClutterTypeIndex &with)
{
  for (int i=0; i<with.Size(); i++)
  {
    const Clutter *c = with.Get(i);
    if (Find(c)<0)
    {
      Add(c);
    }
  }
}

/// item held by GroundClutterCache
class GroundClutterCacheItem: public RefCount, public LinkBidirWithFrameStore
{
  public:
  /// identification - coordinates  
  struct Id
  {
    int _x,_z;
    
    Id(int x, int z):_x(x),_z(z){};
  };
  
  friend class GroundClutterCache;
  
  private:
  /// 
  Id _id;
  /// what types are used in this segment
  ClutterTypeIndex _types;
  /// data - each item in the top-level array corresponds to _types
  AutoArray< AutoArray<GroundClutterInfo> > _data;
  //@{ bounding volume information (world space)
  Vector3 _minMax[2];
  Vector3 _bCenter;
  float _bSphere;
  //@}
  
  
  public:
  int Size() const
  {
    Assert(_data.Size()==_types.Size());
    return _data.Size();
  }
  AutoArray<GroundClutterInfo> &operator [] (int i) {return _data[i];}
  const AutoArray<GroundClutterInfo> &operator [] (int i) const {return _data[i];}
  const Clutter *GetType(int i) const {return _types.Get(i);}

  GroundClutterCacheItem(int x, int z);
  size_t GetMemoryControlled() const
  {
    size_t sum = sizeof(*this)+_data.GetMemoryAllocated();
    for (int i=0; i<_data.Size(); i++)
    {
      sum += _data[i].GetMemoryAllocated();
    }
    return sum;
  }
  /// recalculate min-max box using all clutters participating
  void CalculateMinMax(Landscape *l);
  
  /// compact memory storage
  void Compact();
  
  const Id &GetId() const {return _id;}

  const Vector3 &GetBCenter() const {return _bCenter;}
  float GetBSphere() const {return _bSphere;}
	const Vector3* GetMinMax() const { return _minMax;}
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(GroundClutterCacheItem)

GroundClutterCacheItem::GroundClutterCacheItem(int x, int z)
:_id(x,z)
{
}

/// helper for clutter type ordering
struct TypeOrder
{
  int index;
  const Clutter *key;
  
  static int Compare(const TypeOrder *o1, const TypeOrder *o2)
  {
    return ComparePointerAddresses(o1->key,o2->key);
  }
};
TypeIsSimple(TypeOrder)

void GroundClutterCacheItem::CalculateMinMax(Landscape *l)
{
  if (_data.Size()==0)
  {
    // avoid calculations with FLT_MAX*2
    _minMax[0] = _minMax[1] = VZero;
    return;
  }
  _minMax[0] = Vector3(+FLT_MAX,+FLT_MAX,+FLT_MAX);
  _minMax[1] = Vector3(-FLT_MAX,-FLT_MAX,-FLT_MAX);
  // first cycle - calculate min-max box
  for (int i=0; i<_data.Size(); i++)
  {
    const AutoArray<GroundClutterInfo> &data = _data[i];
    const Clutter &lClutter = *_types.Get(i);
    if (!lClutter._obj) continue;
    
    float bSphere = lClutter._shape->BoundingSphere()*1.3f;
    Vector3 minBox(-bSphere,-bSphere,-bSphere);
    Vector3 maxBox(+bSphere,+bSphere,+bSphere);
    
    for (int c=0; c<data.Size(); c++)
    {
      const GroundClutterInfo &info = data[c];
      
      // position and b-sphere is all we will use
      // object may be animated anyway
      
      Vector3 minPos = info._pos+minBox;
      Vector3 maxPos = info._pos+maxBox;
      //
      SaturateMin(_minMax[0],minPos);
      SaturateMax(_minMax[1],maxPos);
      
    }
  }
  // use min-max center as a sphere radius
  _bCenter = (_minMax[0]+_minMax[1])*0.5f;
  _bSphere = _minMax[1].Distance(_minMax[0])*0.5f;
  // TODO: second cycle to check b-sphere radius
  
  // index was built while creating the list
  Assert(_types.Size()==_data.Size());
  // build ordering index for types

}

void GroundClutterCacheItem::Compact()
{
  _data.Compact();
  for (int i=0; i<_data.Size(); i++)
  {
    _data[i].Compact();
  }
  _types.Compact();
}

template <>
struct MapClassTraits< Ref<GroundClutterCacheItem> >: public DefMapClassTraits< Ref<GroundClutterCacheItem> >
{
  typedef Ref<GroundClutterCacheItem> Type;
  /// key type
  typedef const GroundClutterCacheItem::Id &KeyType;
  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    // we know segments are segment size aligned
    // we might divide both coordinates by segment size
    // this might result in better hash keys
    // unfortunately we have no access to segment size here
    // we assume 16b is enough to contain
    // if not, some overlapping may occur, resulting in suboptimal hashing
    //return key._x|(key._z<<16);
    return key._x*33+key._z;
  }

  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2)
  {
    int d = k1._x-k2._x;
    if (d) return d;
    return k1._z-k2._z;
  }

  /// we want this to be very fast, even if memory requirements may be a little bit higher
  static int CoefExpand() {return 4;}

  /// get a key for given a item
  static KeyType GetKey(const Type &item) {return item->GetId();}
};

struct PosPair
{
  float x,z;
};
TypeIsSimple(PosPair)


/// positions for ground clutters in one texture grid
/**
We use one map per surface type. This allows us to represent clutter for each surface type quite well.

TODO: It would be also possible to use one for the whole terrain.
Alpha could be then used to simulate clutter density. This would be probably faster
(only 1 texture needed in the shader), but it would require more changes to the shader
*/
class GroundClutterPosMap: public RefCount, public LinkBidirWithFrameStore, public CountInstances<GroundClutterPosMap>
{
  // Calculate() and the memory pressure callback can run on different worker
  // threads. Protect the lazily generated arrays from concurrent Unload().
  mutable CriticalSection _dataLock;
  mutable InitVal<bool> _loaded;
  
  mutable Array2D<PosPair> _pos;
  mutable Array2D<signed char> _isHere;

  int _x,_z;
  int _layer;
  SurfaceCharacter _surf;

  void GenerateLocked() const;
  
  public:
  GroundClutterPosMap(int x, int z, int layer, RandomGenerator &randGen, const SurfaceCharacter &surf);
  ~GroundClutterPosMap() {}
  
  void Generate() const;

  void Unload() const
  {
    ScopeLockSection lock(_dataLock);
    _loaded=false;
    _pos.Clear();
    _isHere.Clear();
  }
  
  bool GetCellData(int x, int z, PosPair &pos, int &isHere) const;
  int GetXRange() const {return _x;}
  int GetZRange() const {return _z;}
  
  size_t GetMemoryControlled() const
  {
    return sizeof(this)+_pos.RawSize()+_isHere.RawSize();
  }
};

/// cache clutter positions based on random function and exclusions (roadways)
/**
This cache tracks all existing GroundClutterPosMap objects. It never destroys or creates them.
For some of them it only unloads their contents.
*/
class PosMapCache: public MemoryFreeOnDemandHelper
{
  friend class GroundClutterCache;
  
  FramedMemoryControlledList<GroundClutterPosMap> _posMap;
  
  public:
  PosMapCache();
  ~PosMapCache();
  //@{ MemoryFreeOnDemandHelper implementation
  virtual size_t FreeOneItem()
  {
    GroundClutterPosMap *first = _posMap.First();
    if (!first) return 0;
    first->Unload();
    return _posMap.Delete(first);
  }
  // usually we need very little GroundClutterPosMap entries cached
  virtual float Priority() const {return 0.005;}
  virtual size_t MemoryControlled() const {return _posMap.MemoryControlled();}
  virtual size_t MemoryControlledRecent() const {return _posMap.MemoryControlledRecent();}
  virtual RString GetDebugName() const {return "Clutter Map";}
  virtual void MemoryControlledFrame() {_posMap.Frame();}
  //@}
  
  void AddOrRefresh(GroundClutterPosMap *posMap)
  {
    if (posMap->IsInList())
    {
      _posMap.Refresh(posMap);
    }
    else
    {
      _posMap.Add(posMap);
    }
  }
};

PosMapCache::PosMapCache()
{
  RegisterFreeOnDemandMemory(this);
}
PosMapCache::~PosMapCache()
{
}

/// maintain state of ground clutter around a player
/**
Reason for this cache are:
- performance (clutter creating is not very simple)
- state tracking (clutter items may maintain their state in the cache)
*/

class GroundClutterCache: public RefCount, public MemoryFreeOnDemandHelper
{
  FramedMemoryControlledList<
    GroundClutterCacheItem,
    FramedMemoryControlledListTraitsRef<GroundClutterCacheItem>
  > _cache;
  
  friend class Landscape;
  /// 2d array - one list for each TerrainGrid field
  Array2D< Ref<GroundClutterCacheItem> > _clutterData;
  
  /// fast searching needed
  MapStringToClass< Ref<GroundClutterCacheItem>, RefArray<GroundClutterCacheItem> > _index;
  /// rectangle we are currently holding (terrain grid coordinates)
  GridRectangle _rect;
  
  /// list of all clutter types currently used in the cache
  ClutterTypeIndex _types;
  
  /// we need to keep the rendering order for types
  AutoArray<int> _typeRenderOrder;
  
  //! Flag to determine the prepare method succeeded
  bool _prepareSucceeded;
  //! Prepared clutter distance (without distance limitation)
  float _effClutterDist;
  //! Prepared list of wind emitters
  WindEmitterList _windEmiterList;
  //! Prepared grid rectangle
  GridRectangle _gcBegEnd;
  //! Prepared skipLods
  int _skipLods;

  /// find cached item based on terrain coordinates
  GroundClutterCacheItem *FindCached(int x, int z);
  
  /// cache to make sure posMap is not persistent, but can be reused
  PosMapCache _posMap;
public:
  
  GroundClutterCache();
  
  bool Calculate(
    Landscape *land, const GridRectangle &bigRect, Scene &scene, float effCluterDist
  );

  void Clear();

  void ForceUpdate(Landscape *l, int x, int z);
  void Flatten(Landscape *l, const Entity *entity, Vector3Val flattenPos, float maxFlattenRadius);

  //@{ MemoryFreeOnDemandHelper implementation
  virtual size_t FreeOneItem();
  virtual float Priority() const;
  virtual size_t MemoryControlled() const;
  virtual size_t MemoryControlledRecent() const;
  virtual RString GetDebugName() const;
  virtual void MemoryControlledFrame();
  //@}
};

/// item holding a part of the layer mask
class LayerMaskCacheItem: public RefCount, public LinkBidirWithFrameStore
{
  friend class LayerMaskCache;

  struct TexAccess
  {
    /// mipmap data
    AutoArray<char> _data;
    /// mask texture name - which segment grid is this for
    Ref<Texture> _tex;
    /// access interface to data
    PacLevelMem _access;
    
    bool Request() const;
    
    void Load();
  };
  
  TexAccess _mask;
  
  TexAccess _sat;
  
  public:  
  LayerMaskCacheItem(Texture *layerMask, Texture *sat);
  bool Loaded() const {return _mask._data.Size()>0 && _sat._data.Size()>0;}
  PackedColor GetPixel(float u, float v) const {return _mask._access.GetPixel(_mask._data.Data(),u,v);}
  PackedColor GetPixelSat(float u, float v) const {return _sat._access.GetPixel(_sat._data.Data(),u,v);}
  
  size_t GetMemoryControlled() const
  {
    return sizeof(*this)+_mask._data.GetMemoryAllocated()+_sat._data.GetMemoryAllocated();
  }
  /// async loading request
  bool Request(bool needed=false);

  const SurfaceInfo &GetSurfaceInfo(const TexMaterial *mat, float u, float v) const;
#if _ENABLE_PHYSICS
  enf::PhysicsMaterial* GetPhysMaterial(const TexMaterial *mat, float u, float v) const;
#endif
  Texture *GetTexture(const TexMaterial *mat, float u, float v) const;
  int GetLayer(const TexMaterial *mat, float u, float v) const;
  PackedColor GetSatMap(const TexMaterial *mat, float u, float v) const;
  /// return interpolated result of close detail texture (blended)
  Color GetDetMap(const TexMaterial *mat, float u, float v) const;

  void GetLayerAlphas(float *alphas, PackedColor argb, int layerMask) const;
  /// get stage number from a layer mask (0..3 = ARGB) index
  static int GetStageOrder(const TexMaterial *mat, int pass);
};

static int GetStageOrderFromPass(int passMask, int pass, bool useSatNormal)
{
  // last layer is reserved for satellite normal (when used)
  int maxLayerCount = useSatNormal ? LandMatLayerCount-1 : LandMatLayerCount;

  if (passMask==(1<<maxLayerCount)-1)
  {
    return pass;
  }
  Fail("Old terrain rvmat staging no longer supported");
  int stageOrder = 0;
  // we have mask layer pass index
  // we need to know which material stage this corresponds to
  // check passes in the pass mask
  for (int passIndex=0; passIndex<pass; passIndex++)
  {
    if (passMask&(1<<passIndex))
    { // pass present
      stageOrder++;
    }
  }
  return stageOrder;
}

int GetPassFromStageOrder(int passMask, int stageOrder, bool useSatNormal)
{
  // last layer is reserver for satellite normal (when used)
  int maxLayerCount = useSatNormal?LandMatLayerCount-1 : LandMatLayerCount;

  if (passMask==(1<<maxLayerCount)-1)
  {
    return stageOrder;
  }
  int stageOrderI = 0;
  // we have mask layer pass index
  // we need to know which material stage this corresponds to

  // check passes in the pass mask
  for (int passIndex=0; passIndex<LandMatLayerCount; passIndex++)
  {
    if (passMask&(1<<passIndex))
    { // pass present
      if (stageOrder==stageOrderI) return passIndex;
      stageOrderI++;
    }
  }
  Fail("Stage not found");
  return 0;
}

/**
@param passMask 1..15 - bitmask of present layers, part of PSTerrainXXX identification
*/

int LayerMaskCacheItem::GetStageOrder(const TexMaterial *mat, int pass)
{
  int passMask = GetLayerMask(mat);

  int passOrder = GetStageOrderFromPass(passMask,pass, mat->GetPixelShaderID() == PSTerrainSNX);

  // single-pass implementation
  if (LandMatStageLayers+passOrder*LandMatStagePerLayer>=mat->_stageCount+1)
  {
    LogF("Accessing pass %d (%d) which does not exist in %s",passOrder,pass,cc_cast(mat->GetName()));
    passOrder = (mat->_stageCount+1-LandMatStageLayers)/LandMatStagePerLayer;
  }
  return passOrder;
}

void LayerMaskCacheItem::GetLayerAlphas(float *alphas, PackedColor argb, int layerMask) const
{
#if 0
  COMPILETIME_COMPARE(4,LandMatLayerCount);
  // may be ARGB or lerp
  // convert lerp form into ARGB one
  const float inv255 = 1.0f/255;
  alphas[3] = argb.B8()*inv255;
  alphas[2] = (argb.G8()*inv255)*(1-l3);
  alphas[1] = (argb.R8()*inv255)*(1-l2)*(1-l3);
  alphas[0] = (1-l1)*(1-l2)*(1-l3);
#else
  COMPILETIME_COMPARE(6,LandMatLayerCount);
  // convert lerp form into ARGB one
  const float inv255 = 1.0f/255;
  // based on PFTerrainBodyX pixel shader
  
  // overall weight for the whole XYZ layer
  float cAlpha = argb.B8()*inv255;
  // XYZ style weights
  // when texture alpha is 1, we want layer 0 to be used
  float a = 1-argb.A8()*inv255;
  // weight for layer 1 grows between a = 0 .. 0.5 
  // weight for layer 2 grows between a = 0.5 .. 1
  float weightsXYZ[3] = {1, floatMinMax(a*2,0,1), floatMinMax((a-0.5)*2,0,1) };
  // compute overall weight
  float lerpAlpha[6];
  lerpAlpha[5] = weightsXYZ[5-3]*cAlpha;
  lerpAlpha[4] = weightsXYZ[4-3]*cAlpha;
  lerpAlpha[3] = cAlpha;
  lerpAlpha[2] = argb.G8()*inv255;
  lerpAlpha[1] = argb.R8()*inv255;
  lerpAlpha[0] = 1;
  
  bool first = true;
  for (int i=0; i<6; i++)
  {
    if (layerMask&(1<<i))
    {
      // set first used layer to 1 (makes sure some layer is always active)
      if (first) lerpAlpha[i]=1,first=false;
    }
    else
    {
      // zero layers which are not used at all
      lerpAlpha[i] = 0;
      // zeroing a substantial information is strange
    }
  }

  alphas[5] = lerpAlpha[5];
  alphas[4] = lerpAlpha[4]*(1-lerpAlpha[5]);
  alphas[3] = lerpAlpha[3]*(1-lerpAlpha[4])*(1-lerpAlpha[5]); // common XYZ layer weight
  alphas[2] = lerpAlpha[2]*(1-lerpAlpha[3])*(1-lerpAlpha[4])*(1-lerpAlpha[5]); // C layer
  alphas[1] = lerpAlpha[1]*(1-lerpAlpha[2])*(1-lerpAlpha[3])*(1-lerpAlpha[4])*(1-lerpAlpha[5]); // B layer
  alphas[0] = lerpAlpha[0]*(1-lerpAlpha[1])*(1-lerpAlpha[2])*(1-lerpAlpha[3])*(1-lerpAlpha[4])*(1-lerpAlpha[5]); // A layer
#endif

}
/*!
@param u 0..1 in a satellite grid
@param v 0..1 in a satellite grid
*/

int LayerMaskCacheItem::GetLayer(const TexMaterial *mat, float u, float v) const
{
  // hot-fix - data unavailable?
  if (_mask._data.Size()<=0)
  {
    return -1;
  }
  // material contains mapping from world space into a texture space
  // TODO: prepare simple mapping from world space into a texture space in RVMAT
  PackedColor argbLayers = GetPixel(u,v); // get mask texel
  float l[LandMatLayerCount];
  GetLayerAlphas(l,argbLayers,mat->GetCustomInt());
  float curr = l[0];
  int pass = 0;
  for (int i=1; i<LandMatLayerCount; i++)
  {
    if (l[i]>curr) pass=i,curr=l[i];
  }
  // note: GetStageOrder is identity since ArmA 2
  int passOrder = GetStageOrder(mat, pass);
  return passOrder;
}

PackedColor LayerMaskCacheItem::GetSatMap(const TexMaterial *mat, float u, float v) const
{
  if (_sat._data.Size()<=0)
  {
    return PackedWhite;;
  }
  return GetPixelSat(u,v);
}

Color LayerMaskCacheItem::GetDetMap(const TexMaterial *mat, float u, float v) const
{
  // hot-fix - data unavailable?
  if (_mask._data.Size()<=0)
  {
    return HWhite;
  }
  // material contains mapping from world space into a texture space
  // TODO: prepare simple mapping from world space into a texture space in bimpas
  PackedColor argbLayers = GetPixel(u,v);
  
  float layers[LandMatLayerCount];
  GetLayerAlphas(layers,argbLayers,mat->GetCustomInt());

  int passMask = GetLayerMask(mat);

  // we simulate the pixel shader work here
  // we have mask layer pass index
  int stageOrder = 0;
  // now process all stages which are present
  // we need to know which material stage this corresponds to

  Fail("Not implemented - coloring by sat. mask missing");
  
  Color ret = HBlack;
  // check passes in the pass mask
  for (int passIndex=0; passIndex<LandMatLayerCount; passIndex++)
  {
    if (passMask&(1<<passIndex))
    { // pass present
      Texture *detLayer = mat->_stage[LandMatStageLayers+stageOrder*LandMatStagePerLayer+LandMatDtInStage]._tex;
      // blend together
      ret += detLayer->GetColor()*layers[passIndex];
      // proceed to the next layer
      stageOrder++;
    }
  }
  return ret;
}

Texture *LayerMaskCacheItem::GetTexture(const TexMaterial *mat, float u, float v) const
{
  int layer = GetLayer(mat,u,v);
  if (layer<0)
  {
    return NULL;
  }
  // sample the close detail texture
  return mat->_stage[LandMatStageLayers+layer*LandMatStagePerLayer+LandMatDtInStage]._tex;
}

#if _ENABLE_PHYSICS
enf::PhysicsMaterial* LayerMaskCacheItem::GetPhysMaterial(const TexMaterial *mat, float u, float v) const
{
  Texture *tex = GetTexture(mat, u, v);
  if(!tex)
  {
    return 0;
  }
  return tex->GetPhysMaterial();
}
#endif

const SurfaceInfo &LayerMaskCacheItem::GetSurfaceInfo(const TexMaterial *mat, float u, float v) const
{
  Texture *tex = GetTexture(mat,u,v);
  if (!tex)
  {
    return SurfaceInfo::_default;
  }
  return tex->GetSurfaceInfo();
}

bool LayerMaskCacheItem::TexAccess::Request() const
{
  if (_data.Size()>0) return true;
  if (!_tex) return true;
  if (!_tex->HeadersReady()) return false;
  int w = _tex->AWidth();
  int h = _tex->AHeight();
  return _tex->RequestGetRect(0,0,0,w,h);
}

bool LayerMaskCacheItem::Request(bool needed)
{
  // first request one, then second, to reduce cache requirement
  if (_mask._data.Size()<=0)
  {
    if (!needed && !_mask.Request())
    {
      return false;
    }
    _mask.Load();
  }
  if (_sat._data.Size()<=0)
  {
    if (!needed && !_sat.Request())
    {
      return false;
    }
    _sat.Load();
  }
  return true;
}

void LayerMaskCacheItem::TexAccess::Load()
{
  if (_data.Size()>0 || !_tex) return;
  int w = _tex->AWidth();
  int h = _tex->AHeight();
  PacFormat format = _tex->GetFormat();
  int dataSize = PacLevelMem::MipmapSize(format,w,h);
  _data.Realloc(dataSize);
  _data.Resize(dataSize);
  bool dataOk = _tex->GetRect(0,0,0,w,h,format,_data.Data());
  if (!dataOk)
  {
    _data.Clear();
  }
  else
  {
    // initialize sampling access
    _access._w = w;
    _access._h = h;
    _access._pitch = dataSize/h;
    _access._sFormat = format;
    _access._dFormat = format;
  }
}

LayerMaskCacheItem::LayerMaskCacheItem(Texture *layerMask, Texture *sat)
{
  _mask._tex = layerMask;
  _sat._tex = sat;
  // get mipmap data for the mask (do not change the format)
}

/// layer mask cache to provide efficient access to layer mask information
class LayerMaskCache: public RefCount, public MemoryFreeOnDemandHelper
{
  FramedMemoryControlledList<
    LayerMaskCacheItem,
    FramedMemoryControlledListTraitsRef<LayerMaskCacheItem>
  > _cache;

  public:
  
  LayerMaskCache();
  // { memory free on demand implementation
  virtual size_t FreeOneItem();
  virtual float Priority() const;
  virtual size_t MemoryControlled() const;
  virtual size_t MemoryControlledRecent() const;
  virtual RString GetDebugName() const;
  virtual void MemoryControlledFrame();
  // }
  void Init(Landscape *land);
  void Clear()
  {
    _cache.Clear();
  }
  
  LayerMaskCacheItem *Item(const TexMaterial *mat, bool needed=true, bool *ready=NULL);
};


LayerMaskCache::LayerMaskCache()
{
  RegisterFreeOnDemandMemory(this);
}

void LayerMaskCache::Init(Landscape *land)
{
  // TODO:
  // load what is necessary for the camera
}

LayerMaskCacheItem *LayerMaskCache::Item(const TexMaterial *mat, bool needed, bool *ready)
{
  // first of all search existing items
  // most recent first
  if (ready) *ready = false;
  if (needed)
  {
    mat->Load();
  }
  else
  {
    if (!mat->IsReady()) return NULL;
  }

  VertexShaderID vs = mat->GetVertexShaderID();
  if (vs!=VSTerrain)
  {
    if (ready) *ready = true;
    return NULL;
  }
  
  // single-pass implementation - layer mask in stage 1
  int satStage = 0;
  int maskStage = 1;
  
  Texture *layerMask = mat->_stage[maskStage]._tex;
  // if there is no layer mask, material is wrong and we cannot rely on it
  if (!layerMask)
  {
    if (ready) *ready = true;
    return NULL;
  }
  
  // TODO: use hashed index here to allow larger cached area?
  for (LayerMaskCacheItem *item = _cache.Last(); item; item = _cache.Prev(item))
  {
    if (item->_mask._tex==layerMask)
    {
      _cache.Refresh(item);
      if (item->Request(needed)) 
      {
        if (ready) *ready = true;
        return item;
      }
      return NULL;
    }
  }
  
  Texture *sat = mat->_stage[satStage]._tex;
  LayerMaskCacheItem *newItem = new LayerMaskCacheItem(layerMask,sat);
  _cache.Add(newItem);
  //_index.Add(newItem);
  
  // avoid excessive caching?
  // TODO: check how many items is reasonable to have
  const int maxItems = 10;
  while (_cache.ItemCount()>maxItems)
  {
    FreeOneItem();
  }

  // check if the item data are ready
  if (newItem->Request(needed))
  {
    if (ready) *ready = true;
    return newItem;
  }
  return NULL;
}

size_t LayerMaskCache::FreeOneItem()
{
  // never release the last item - it may be needed as a part of the active request
  LayerMaskCacheItem *item = _cache.First();
  if (!item) return 0;
  size_t released = item->GetStoredMemoryControlled();
  _cache.Delete(item);
  return released;
}

float LayerMaskCache::Priority() const
{
  // layer mask entries are quite large, but fortunately we usually do not need a lot them
  return 0.03f;
}
size_t LayerMaskCache::MemoryControlled() const
{
  return _cache.MemoryControlled();
}
size_t LayerMaskCache::MemoryControlledRecent() const
{
  return _cache.MemoryControlledRecent();
}
RString LayerMaskCache::GetDebugName() const
{
  return "LayerMask";
}

void LayerMaskCache::MemoryControlledFrame()
{
  _cache.Frame();
}

GroundClutterCacheItem *GroundClutterCache::FindCached(int x, int z)
{
  const Ref<GroundClutterCacheItem> &item = _index.Get(GroundClutterCacheItem::Id(x,z));
  if (_index.NotNull(item)) return item;
  return NULL;
}

void Landscape::InitPrimaryTexture(bool compute)
{
  DWORD start = GlobalTickCount();
  _primTexIndex.Dim(_terrain->_data.GetXRange(), _terrain->_data.GetYRange());
  for (int z = 0; z < _primTexIndex.GetYRange(); z++)
  {
    for (int x = 0; x < _primTexIndex.GetXRange(); x++)
    {
      int index = 0;
      if (compute)
        index = FindPrimTexIndex(x, z);
      _primTexIndex(x, z) = index;
    }
  }
  if (compute)
    LogF("Init primary texture mapping (%s): %d ms", cc_cast(_name), GlobalTickCount() - start);
}

Texture* Landscape::PrimaryTexture(int x, int z) const
{
  Assert(_primTexIndex.GetXRange() == GetTerrainRange());
  // FIXME Radim: after resolving "out-of-map" situations, return proper index instead
  if (!land_TerrainInRange(z, x))
  {
    if (_terrain->_outsideTextureInfo._mat)
    {
      int index = LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage;
      return _terrain->_outsideTextureInfo._mat->_stage[index]._tex;
    }
    else
      return NULL;
  }
  int index = _primTexIndex(x, z);
  if (index < 0)
    return NULL;
  const int subdivLog = GetSubdivLog();
  const Scenery::TextureInfo& texInfo = ClippedTextureInfo(z >> subdivLog, x >> subdivLog);
  const TexMaterial* gMaterial = texInfo._mat;
  if (!gMaterial)
    return NULL;
  // map index to the corresponding texture (see LayerMaskCacheItem::GetTexture())
  return gMaterial->_stage[LandMatStageLayers + index * LandMatStagePerLayer + LandMatDtInStage]._tex;
}

bool Landscape::DebugSavePrimaryTexture(const char* filename, bool unify) const
{
  FILE* f = fopen(filename, "w");
  if (!f)
    return false;
  FindArrayKey<Texture*> primTexMap;
  int maxx = _primTexIndex.GetXRange();
  int maxz = _primTexIndex.GetYRange();
  for (int z = 0; z < maxz; z++)
  {
    for (int x = 0; x < maxx; x++)
    {
      // align properly against the map
      int zz = maxz - 1 - z;
      int index;
      if (unify)
        index = primTexMap.FindOrAdd(PrimaryTexture(x, zz));
      else
        index = _primTexIndex(x, zz);
      char c = 'A' + index;
      fputc(c, f);
    }
    fputc('\n', f);
  }
  fclose(f);
  return true;
}

bool Landscape::DebugSaveHeightMap(const char* filename) const
{
  FILE* f = fopen(filename, "w");
  if (!f)
    return false;
  int maxx = _terrain->_data.GetXRange();
  int maxz = _terrain->_data.GetYRange();
  for (int z = 0; z < maxz; z++)
  {
    for (int x = 0; x < maxx; x++)
    {
      // align properly against the map
      int zz = maxz - 1 - z;
      float y = _terrain->_data.Get(x, zz);
      fprintf(f, "% 8.4f ", y);
    }
    fputc('\n', f);
  }
  fclose(f);
  return true;
}

int Landscape::FindPrimTexIndex(int x, int z) const
{
  const int subdivLog = GetSubdivLog();
  const Scenery::TextureInfo& texInfo = ClippedTextureInfo(z >> subdivLog, x >> subdivLog);
  const TexMaterial* gMaterial = texInfo._mat;
  if (!gMaterial)
    return -1;
  // note: this is the slow operation which should be eliminated in "real-time" position -> primary texture mapping
  Ref<LayerMaskCacheItem> layerMaskItem = _layerMaskCache->Item(gMaterial);
  int world = gMaterial->_stage[LandMatStageMask]._texGen;
  if (world < 0)
    return -1;
  Assert(gMaterial->_texGen[world]._uvSource == UVWorldPos);
  // sub-sample the mask texel by texel
  Matrix4 uvtrans = gMaterial->_texGen[world]._uvTransform;
  // start and end positions for the subsampling (y doesn't matter here)
  Vector3 pos00(x * GetTerrainGrid(), 0, z * GetTerrainGrid());
  Vector3 pos11((x + 1) * GetTerrainGrid(), 0, (z + 1) * GetTerrainGrid());
  Vector3 uv00 = uvtrans.FastTransform(pos00);
  Vector3 uv11 = uvtrans.FastTransform(pos11);
  float u0 = uv00.X();
  float v0 = uv00.Y();
  float u1 = uv11.X();
  float v1 = uv11.Y();
  if (u0 > u1)
    Swap(u0, u1);
  if (v0 > v1)
    Swap(v0, v1);
  const Texture* mask = gMaterial->_stage[LandMatStageMask]._tex;
  // Hotfix: crash when mask == NULL
  if (!mask) return -1;
  int w = mask->AWidth();
  int h = mask->AHeight();
  // step deltas
  float du = 1.0 / w;
  float dv = 1.0 / h;
  float ucount = (u1 - u0) * w;
  float vcount = (v1 - v0) * h;
  // number of steps (always do at least one step)
  int usteps = std::max(1, toIntFloor(ucount));
  int vsteps = std::max(1, toIntFloor(vcount));
  // histogram of layer indices
  int layerCount[LandMatLayerCount];
  for (int i = 0; i < LandMatLayerCount; i++)
    layerCount[i] = 0;
  // perform sub-sampling
  float v = v0;
  for (int sv = 0; sv < vsteps; sv++, v += dv)
  {
    float u = u0;
    for (int su = 0; su < usteps; su++, u += du)
    {
      int layer = layerMaskItem->GetLayer(gMaterial, u, v);
      // ignore invalid indices
      if (layer >= 0)
        layerCount[layer]++;
    }
  }
  // find the most frequent index
  int maxIndex = -1;
  int maxCount = 0;
  for (int i = 0; i < LandMatLayerCount; i++)
  {
    int count = layerCount[i];
    if (count > maxCount)
    {
      maxCount = count;
      maxIndex = i;
    }
  }
  return maxIndex;
}


void Landscape::ClearGrassMap()
{
  _grassApprox.Clear();
  _grassApproxOrig.Clear();
}


void Landscape::InitGrassMapEmpty()
{
  _grassApproxOrig.Dim(GetTerrainRange(),GetTerrainRange());
  for (int z = 0; z < GetTerrainRange(); z++) for (int x = 0; x < GetTerrainRange(); x++)
  {
    _grassApproxOrig(x,z) = 0;
  }
  _grassApprox = _grassApproxOrig;
}

/*!
\patch 5143 Date 3/22/2007 by Ondra
- Fixed: Distant grass layer follows roads more closely.
*/
void Landscape::InitGrassMap()
{
#if 0
  for (int i=0; i<_clutter.Size(); i++)
  {
    Clutter &c = _clutter[i];
    float height = c._obj ? c._obj->Max().Y()-c._obj->Min().Y() : 0.0f;
    RptF("Clutter %s, height %.2g", cc_cast(c._name),height);
  }
  // check if character is OK?
#endif
  DWORD start = GlobalTickCount();
  
  _grassApproxOrig.Dim(GetTerrainRange(),GetTerrainRange());
  // we need to sample layer mask for each vertex
  // this requires accessing a lot of data
  // we do not need to sample finest mipmap, though

  //bool once = true;

  /// first loop through the texture grid
  int subdiv = GetSubdiv();
  int subdivLog = GetSubdivLog();

// use only one sample in Helisim 
#ifndef _HELISIM_MAPS
  // we need to access a few samples around as well
  // no need to sample ceil(GetTerrainGrid()/_clutterGrid) - weight would be zero
  const int samples = toIntFloor(GetTerrainGrid()/_clutterGrid);
#else // fast approximation - inaccurate, but quick
  const int samples = 0;
#endif
  for (int z = 0; z < GetLandRange(); z++) 
  {
    if(z%100 == 0)
      LogF("InitGrassMap calcs: %d / %d", z, GetLandRange());
    for (int x = 0; x < GetLandRange(); x++)
    {
      AUTO_STATIC_ARRAY(InitPtr<Object>,roadways,32);
      GetRoadList(x, z, roadways);

      // calculate a weighted average
      float grassSum = 0;
      float grassWeight = 0;
      for (int zz = 0; zz < subdiv; zz++) for (int xx = 0; xx < subdiv; xx++)
      {
        int xxx = (x<<subdivLog)+xx;
        int zzz = (z<<subdivLog)+zz;

        // consider reusing grass height samples, otherwise the creation will be way too slow
        // we might cache last few fields based on xxx/zzz
        // this could yield 4x performance improvement
        for (int zs=-samples; zs<=+samples; zs++)
          for (int xs=-samples; xs<=+samples; xs++)
          {
            float xOffset = xs*_clutterGrid;
            float zOffset = zs*_clutterGrid;
            float xc = xxx*GetTerrainGrid() + xOffset;
            float zc = zzz*GetTerrainGrid() + zOffset;
            float grass = GetGrassHeight(xc,zc,roadways);
            // calculate bilinear weights - distance from the current node
            // each point will have a sum of weights for neighboring vertices 1

            float wx = floatMax(1-fabs(xOffset)*GetInvTerrainGrid(),0);
            float wz = floatMax(1-fabs(zOffset)*GetInvTerrainGrid(),0);
            grassWeight += wx*wz;
            grassSum += grass * wx*wz;
          }
          float grass = grassWeight>0 ? grassSum/grassWeight : 0;

          // when used during LoadData/Binarize, scan for road objects
          if (!IsStreaming() && roadways.Size()>0)
          {
            // there is a list and it contains some object with a roadway
            static float criticalRadius = 4.0f;
            // it might be somewhat faster to use groups here
            // however, the difference is not worth the effort
            for (int i=0; i<roadways.Size(); i++)
            {
              Object *obj = roadways[i];
              // process only primary / network objects now
              if (obj->GetType()!=Primary && obj->GetType()!=Network
#if _ARMA3_TERRAIN_DECALS
                && obj->GetType()!=LandDecal
#endif                 
                ) continue;
              LODShape *shape = obj->GetShape();
              if (!shape) continue;
              if (shape->FindRoadwayLevel()<0) continue;
              float radius = obj->GetRadius();
              if (radius<criticalRadius) continue;
              // cut grass around the object
              float xcMin = obj->FutureVisualState().Position().X()-radius, xcMax = obj->FutureVisualState().Position().X()+radius;
              float zcMin = obj->FutureVisualState().Position().Z()-radius, zcMax = obj->FutureVisualState().Position().Z()+radius;

              int xtMin = toIntFloor(xcMin*GetInvTerrainGrid()), xtMax = toIntCeil(xcMax*GetInvTerrainGrid());
              int ztMin = toIntFloor(zcMin*GetInvTerrainGrid()), ztMax = toIntCeil(zcMax*GetInvTerrainGrid());
              saturate(xtMin,0,GetTerrainRangeMask());saturate(xtMax,0,GetTerrainRangeMask());
              saturate(ztMin,0,GetTerrainRangeMask());saturate(ztMax,0,GetTerrainRangeMask());

              // when any object influence is at any neighbouring cell, cut the grass
              if (xxx>=xtMin && xxx<=xtMax && zzz>=ztMin && zzz<=ztMax)
              {
                // check if there is any intersection between the current terrain grid and the object
                // this can be done by gradually cutting the object polygons to see if anything is left from them
                if (obj->DetectRoadwayOverlap((xxx-1)*GetTerrainGrid(),(xxx+1)*GetTerrainGrid(),(zzz-1)*GetTerrainGrid(),(zzz+1)*GetTerrainGrid()))
                {
                  grass = 0;
                }
              }
            }
          }


          // convert to byte representation
          int grassInt = toInt(grass*(1.0f/GrassApproxScale));
          saturate(grassInt,0,255);
          /*
          if (grass>0 && once)
          {
          RptF("  grass detected at %d,%d",xxx,zzz);
          once = false;
          }
          */
          _grassApproxOrig(xxx,zzz) = grassInt;
      }
    }
  }
  // dynamic state is the same as static
  _grassApprox = _grassApproxOrig;
  LogF("Init grass (%s): %d ms",cc_cast(_name),GlobalTickCount()-start);
}

bool Landscape::CutGrass(int ztBeg, int ztEnd, int xtBeg, int xtEnd)
{
  bool someChanged = false;
  for (int zz=ztBeg; zz<=ztEnd; zz++) for (int xx=xtBeg; xx<=xtEnd; xx++)
  {
    // no grass at any of the neighbouring vertices
    // TODO: use more exact information where available?
    unsigned char &grass = _grassApprox(xx,zz);
    if (grass!=0) someChanged = true;
    grass = 0;
  }
  return someChanged;
}

void Landscape::UpdateGrassMap()
{
  if (_grassApproxOrig.GetXRange()==0) return;
  Assert(_grassApproxOrig.GetXRange()==_terrain->_data.GetXRange());
  Assert(_grassApproxOrig.GetYRange()==_terrain->_data.GetYRange());

  // restore previous state
  // some vertex buffers may need "restoring" (flushing) as well
  // the best would be to implement some lazy flushing, only as needed
  FlushSegCache();

  _grassApprox = _grassApproxOrig;
  for (int z=0; z<_terrain->_data.GetYRange(); z++)
  for (int x=0; x<_terrain->_data.GetXRange(); x++)
  {
    UpdateGrassMap(x,z);
  }
}

/*
@param x coordinate (n LandGrid)
@param z coordinate (n LandGrid)
*/
void Landscape::UpdateGrassMap(int x, int z)
{
  int subdiv = GetSubdivLog();

  GeographyInfo g(_terrain->_geography(x,z));
  // if landscape contains any roadway, we need to cull the grass there
  // consider: we might cull it only for roads/tracks
  //if (g.u.someRoadway) return true;
  if (g.u.road)
  {
    // since version 20 better solution implemented during LoadData (Binarize)
    if (IsStreaming() && _fileVersion<20)
    {
      // some roadway present - we need all vertices to contain grass 0
      int xtMin = x<<subdiv, xtMax = (x+1)<<subdiv;
      int ztMin = z<<subdiv, ztMax = (z+1)<<subdiv;

      saturate(xtMin,0,GetTerrainRangeMask());saturate(xtMax,0,GetTerrainRangeMask());
      saturate(ztMin,0,GetTerrainRangeMask());saturate(ztMax,0,GetTerrainRangeMask());

      if (CutGrass(ztMin, ztMax, xtMin, xtMax))
      {
        FlushSegCache(x,z);
      }
    }
    return;
  }
  const ObjectListFull *list = _objects(x,z).GetList();
  if (!list || list->GetRoadwayCount()==0)
  {
    return;
  }
  // there is a list and it contains some object with a roadway
  static float criticalRadius = 4.0f;
  // it might be somewhat faster to use groups here
  // however, the difference is not worth the effort
  bool someChange = false;
  for (int i=0; i<list->Size(); i++)
  {
    Object *obj = list->Get(i);
    if (!obj) continue;
    if (obj->GetType()==Primary || obj->GetType()==Network
#if _ARMA3_TERRAIN_DECALS
      || obj->GetType()==LandDecal
#endif      
      ) continue;
    LODShape *shape = obj->GetShape();
    if (!shape) continue;
    if (shape->FindRoadwayLevel()<0) continue;
    float radius = obj->GetRadius();
    if (radius<criticalRadius) continue;
    if (obj->GetType()==TypeTempVehicle)
    {
      // experimental: artificially smaller is enough for craters
      static float craterFactor = 0.1f;
      radius *= craterFactor;
    }
    // cut grass around the object
    float xcMin = obj->FutureVisualState().Position().X()-radius, xcMax = obj->FutureVisualState().Position().X()+radius;
    float zcMin = obj->FutureVisualState().Position().Z()-radius, zcMax = obj->FutureVisualState().Position().Z()+radius;

    int xtMin = toIntFloor(xcMin*GetInvTerrainGrid()), xtMax = toIntCeil(xcMax*GetInvTerrainGrid());
    int ztMin = toIntFloor(zcMin*GetInvTerrainGrid()), ztMax = toIntCeil(zcMax*GetInvTerrainGrid());
    saturate(xtMin,0,GetTerrainRangeMask());saturate(xtMax,0,GetTerrainRangeMask());
    saturate(ztMin,0,GetTerrainRangeMask());saturate(ztMax,0,GetTerrainRangeMask());
    if (CutGrass(ztMin, ztMax, xtMin, xtMax))
    {
      someChange = true;
    }
  }
  if (someChange)
  {
    FlushSegCache(x,z);
  }
}

void Landscape::UpdateGrassMap(Object *obj)
{
  int x = toIntFloor(obj->FutureVisualState().Position().X()*GetInvLandGrid());
  int z = toIntFloor(obj->FutureVisualState().Position().Z()*GetInvLandGrid());
  UpdateGrassMap(x,z);
}

/// helper to pass three values in the GroundClutterTextureSource constructor
struct TexSizeInfo
{
  int w,h,mipmaps;
};



GroundClutterPosMap::GroundClutterPosMap(
  int x, int z, int layer, RandomGenerator &randGen, const SurfaceCharacter &surf
)
{
  _x = x;
  _z = z;
  _layer = layer;
  _surf = surf;
  //Generate();
}

void GroundClutterPosMap::Generate() const
{
  ScopeLockSection lock(_dataLock);
  GenerateLocked();
}

void GroundClutterPosMap::GenerateLocked() const
{
  if (_loaded) return;
  
  PROFILE_SCOPE_EX(grGen,*);
  int x = _x;
  int z = _z;
  int layer = _layer;
  RandomGenerator &randGen = GLandscape->GetRandGen();
  const SurfaceCharacter &surf = _surf;

  _pos.Dim(x,z);
  _isHere.Dim(x,z);
  
  // generate a position pair for each grid location
  for (int zz=0; zz<z; zz++) for (int xx=0; xx<x; xx++)
  {
    int seedXZ = randGen.GetSeed(xx,zz,layer);
    // relative position in the grid (0..1 in both directions)
    int isHere = -1;
    float isHereF = randGen.RandomValue(seedXZ);
    for (int i=0; i<surf.Size(); i++)
    {
      float prop = surf.Get(i)._probabilityThold;
      if (prop>=isHereF)
      {
        isHere = surf.Get(i)._landIndex;
        // negative _landIndex is possible, but it should never be used on the current landscape
        // TODO: replace with some clever RptF
        //Assert(isHere>=0);
        break;
      }
    }
    // avoid overflowing char
    DoAssert(isHere<127);
    _isHere(xx,zz) = isHere;

    _pos(xx,zz).x = randGen.RandomValue(seedXZ+1);
    _pos(xx,zz).z = randGen.RandomValue(seedXZ+2);

  }
  _loaded=true;
}

bool GroundClutterPosMap::GetCellData(int x, int z, PosPair &pos,
  int &isHere) const
{
  ScopeLockSection lock(_dataLock);
  GenerateLocked();
  if (x < 0 || x >= _pos.GetXRange() || z < 0 ||
      z >= _pos.GetYRange() || x >= _isHere.GetXRange() ||
      z >= _isHere.GetYRange())
    return false;
  pos = _pos.Get(x, z);
  isHere = _isHere.GetValue(x, z);
  return true;
}

/// texture source 
class GroundClutterTextureSource: public TextureSourceSimple
{
  typedef TextureSourceSimple base;

  Ref<GroundClutterPosMap> _posMap;

  public:
  GroundClutterTextureSource(GroundClutterPosMap *posMap, const TexSizeInfo &size);
  //! implements TextureSourceSimple
  virtual TextureType GetTextureType() const
  {
    // our RGB channels contain SRGB information
    return TT_Diffuse;
  };
  virtual TexFilter GetMaxFilter() const {return TFAnizotropic16;}
  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  PackedColor GetAverageColor() const {return PackedBlack;}
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  int GetClamp() const {return 0;}
  void SetClamp(int clampFlags) {}
};

/// compute suitable texture size given one side size (width, height)
/**
We need the resolution to be fine enough to represent position inside of the texel
We do not need much for that, though. 3-4x oversampling should be fine
*/

static inline int TextureSizeFromPosMap(int size)
{
  return roundTo2PowerNCeil(size*3);
}

/// compute suitable texture size given a GroundClutterPosMap
static TexSizeInfo TextureSizeFromPosMap(const GroundClutterPosMap *posMap)
{
  TexSizeInfo ret;
  ret.w = TextureSizeFromPosMap(posMap->GetXRange());
  ret.h = TextureSizeFromPosMap(posMap->GetZRange());
  ret.mipmaps = log2Floor(intMax(ret.w,ret.h));
  return ret;
}
// we might consider even better resolution
// however generating such texture might take very long
GroundClutterTextureSource::GroundClutterTextureSource(GroundClutterPosMap *posMap, const TexSizeInfo &size)
:base(PacARGB8888,size.w,size.h,size.mipmaps),
_posMap(posMap)
{
}

static float ClutterHeightIgnored = 0.10f;


static bool CheckClutterIgnored(Landscape *land, int clutter)
{
  // TODO: optimize (cache in the clutter)
  LODShape *shape = land->GetClutter(clutter)._shape;
  if (!shape) return true;
  float height = shape->Max().Y()-shape->Min().Y();
  return height<ClutterHeightIgnored;
}

bool GroundClutterTextureSource::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  Fail("GroundClutterTextureSource::GetMipmapData not thread safe - fix before using again");
  _posMap->Generate();
  GLandscape->AddOrRefreshGroundClutterPosMap(_posMap);
  
  float invW = 1.0f/mip._w;
  float invH = 1.0f/mip._h;
  int xRange = _posMap->GetXRange();
  int zRange = _posMap->GetZRange();
  // fill data needed, for each point check if we hit clutter in a near cell
  // TODO: for high resolution cell driven rasterization would be faster
  for (int y=0; y<mip._h; y++)
  {
    void *line = ((char *)mem)+mip._pitch*y;
    for (int x=0; x<mip._w; x++)
    {
      float u = (x+0.5f)*invW;
      float v = (y+0.5f)*invH;
      // check how close we are to close cells
      float xCell = u*xRange;
      float zCell = v*zRange;

      // scan four neighbors, check distance to the nearest clutter item
      int xMin = toInt(xCell)-1;
      int zMin = toInt(zCell)-1;
      int xMax = xMin+2;
      int zMax = zMin+2;
      float minDist2 = FLT_MAX;
      for (int zz=zMin; zz<zMax; zz++) for (int xx=xMin; xx<xMax; xx++)
      {
        // perform looped access as needed, + is needed to avoid mod of negative number
        int zWrap = (zz+zRange)%zRange;
        int xWrap = (xx+xRange)%xRange;
        PosPair cellPos;
        int isHere;
        if (!_posMap->GetCellData(xWrap,zWrap,cellPos,isHere)) continue;
        if (isHere<0) continue;
        if (CheckClutterIgnored(GLandscape,isHere)) continue;
        // check center position
        float xGrid = xWrap + cellPos.x;
        float zGrid = zWrap + cellPos.z;
        

        // compute x and z distance considering wrapping
        float xDist = floatMin(fabs(xCell-xGrid),fabs(xCell-xRange-xGrid),fabs(xCell+xRange-xGrid));
        float zDist = floatMin(fabs(zCell-zGrid),fabs(zCell-zRange-zGrid),fabs(zCell+zRange-zGrid));
        float dist2 = Square(xDist)+Square(zDist);
        saturateMin(minDist2,dist2);
      }
      int a8 = 0;
      if (minDist2<FLT_MAX)
      {
        float circle = floatMax(1-sqrt(minDist2),0);
        a8 = toInt(circle*255*4);
        // experimental - is seems almost opaque layer looks best
        // alpha testing creates too much aliasing
        saturate(a8,0,255);
      }
#if 0 // _DEBUG
      // diagnostics - render grid borders
      if (x==0 || y==0) a8 = 255;
#endif
      switch (mip._dFormat.GetEnumValue())
      {
        case PacAI88:
          // replicate into 2 bytes
          ((unsigned short *)line)[x] = (unsigned(a8)<<8)|unsigned(a8);
          break;
        case PacARGB8888:
          // replicate into 4 bytes
          ((unsigned int *)line)[x] = (unsigned(a8)<<24)|(unsigned(a8)<<16)|(unsigned(a8)<<8)|unsigned(a8);
          break;
      }
    }
  }
  return true;
}


/// GroundClutterPosMap with an added texture
class GroundClutterPosMapWithTex: public GroundClutterPosMap
{
  Ref<Texture> _tex;
  bool _used;

  typedef GroundClutterPosMap base;

  public:
  GroundClutterPosMapWithTex(
    int x, int z, int layer, RandomGenerator &randGen, const SurfaceCharacter &surf, bool used
  );
  ~GroundClutterPosMapWithTex(){}

  void InitTexture();

  Texture *GetTexture() const {return _tex;}
  void Destroy() {_tex.Free();}
};

GroundClutterPosMapWithTex::GroundClutterPosMapWithTex(
  int x, int z, int layer, RandomGenerator &randGen, const SurfaceCharacter &surf, bool used
)
:base(x,z,layer,randGen,surf)
,_used(used)
{
  // Do not create a texture source here. This object has RefCount zero until
  // the caller stores the returned pointer in GroundClutterPosMaps::_maps.
  // The source owns a Ref back to this map, so constructing it here can
  // release and destroy the map before its constructor's caller gets it.
}

void GroundClutterPosMapWithTex::InitTexture()
{
  if (_used && !_tex)
  {
    // The caller must first hold this object in a Ref.
    ITextureSource *src = new GroundClutterTextureSource(this,TextureSizeFromPosMap(this));
    _tex = GlobLoadTexture(src);
  }
}

struct GroundClutterPosMaps: public TexMaterial::CustomData
{
  /// one pos map available for each layer
  Ref<GroundClutterPosMapWithTex> _maps[LandMatLayerCount];
  
  ~GroundClutterPosMaps();
  #if _DEBUG
  void OnUsed() const {TexMaterial::CustomData::OnUsed();}
  void OnUnused() const {TexMaterial::CustomData::OnUnused();}
  #endif
};

GroundClutterPosMaps::~GroundClutterPosMaps()
{
  for (int i=0; i<LandMatLayerCount; i++)
  {
    if (_maps[i])
    {
      // break circular dependency of Refs
      _maps[i]->Destroy();
      // this should leave onle one Ref active, the one we own
      Assert(_maps[i]->RefCounter()==1);
    }
  }
}


Texture *Landscape::GetGrassMask(const TexMaterial *mat, int layer) const
{
  GroundClutterPosMaps *posMap = static_cast<GroundClutterPosMaps *>(mat->GetCustomData());
  if (!posMap) return NULL;
  if (!posMap->_maps[layer]) return NULL;
  return posMap->_maps[layer]->GetTexture();
}

/// prepare grass textures for the grass shader
/**
@return false when resulting material would be empty
*/
bool Landscape::PrepareGrassTextures(TexMaterial *mat, const TexMaterial *src)
{
  // layer mask is needed for this
  Ref<LayerMaskCacheItem> layerMaskItem = _layerMaskCache->Item(mat);
  if (!mat)
  {
    Fail("No layer mask, no grass");
    return false;
  }

  // we need to share the custom data
  mat->SetCustomData(src->GetCustomData());

  {
    // we need to change the mapping so that it maps 1 texture per LandGrid
    // we will use model space position -> UV
    // model space position is +-segment count * LandGrid
    // all we need is multiply by 1/LandGrid
    // find the texgen for the no. stage
    int noTexGen = mat->_stage[LandMatStageLayers+LandMatNoInStage]._texGen;
    TexGenInfo &texGen = mat->_texGen[noTexGen];

    if (texGen._uvSource==UVPos)
    {
      // if there is already UVPos, the transformation was already done
      // check if the converted material is valid (contains some pass)
      return mat->_stageCount>1;
    }

    texGen._uvSource = UVPos;
    texGen._uvTransform = MZero;
    texGen._uvTransform(0,0) = GetInvLandGrid();
    texGen._uvTransform(1,2) = GetInvLandGrid();
    texGen._uvTransform(2,1) = GetInvLandGrid();
  }

  {
    // detail textures are not used, we will use Perlin noise instead for them
    int dtTexGen = mat->_stage[LandMatStageLayers+LandMatDtInStage]._texGen;
    TexGenInfo &texGen = mat->_texGen[dtTexGen];

    // mapping similar to grass map, but denser
    texGen._uvSource = UVPos;
    texGen._uvTransform = MZero;
    const float scale = 8;
    texGen._uvTransform(0,0) = GetInvLandGrid()*scale;
    texGen._uvTransform(1,2) = GetInvLandGrid()*scale;
    texGen._uvTransform(2,1) = GetInvLandGrid()*scale;
  }
  
  
  //Assert(mat->GetPixelShaderID()>=PSTerrainGrass1 && mat->GetPixelShaderID()<=PSTerrainGrass15);
  Assert(mat->GetPixelShaderID()==PSTerrainGrassX);
  // we need to get a surface info for each stage
  // verify the material stages are what we expect them to be
  Assert(mat->_stageCount+1>=LandMatStageLayers+LandMatStagePerLayer);
  Assert((mat->_stageCount+1-LandMatStageLayers)%LandMatStagePerLayer==0);
  int nPasses = (mat->_stageCount+1-LandMatStageLayers)/LandMatStagePerLayer;

  bool someTexture = false;
  const GroundClutterPosMaps *posMaps = static_cast<const GroundClutterPosMaps *>(mat->GetCustomData());
  for (int i=0; i<nPasses; i++)
  {
    // layer main texture is at +0
    // some layers have no clutter, some have only clutter which is ignored for this purpose
    if (!posMaps || !posMaps->_maps[i] || !posMaps->_maps[i]->GetTexture())
    {
      // no posMap may be because there is no clutter for the layer
      // in such case we want to remove the corresponding stages
      mat->_stage[LandMatStageLayers+i*LandMatStagePerLayer+LandMatNoInStage]._tex = GPreloadedTextures.New(TextureZero);
      mat->_stage[LandMatStageLayers+i*LandMatStagePerLayer+LandMatDtInStage]._tex = GPreloadedTextures.New(TextureZero);
    }
    else
    {
      mat->_stage[LandMatStageLayers+i*LandMatStagePerLayer+LandMatNoInStage]._tex = posMaps->_maps[i]->GetTexture();
      mat->_stage[LandMatStageLayers+i*LandMatStagePerLayer+LandMatDtInStage]._tex = _grassNoiseTexture;
      someTexture = true;
    }
  }
  if (!someTexture)
  {
    // indicate the material is empty
    mat->_stageCount = LandMatStageLayers;
  }
  return someTexture;
}

//! bilinear interpolation for any data type, yXZ, xf and zf = <0,1>

template <class Numeric>
static inline Numeric BilintEx
(
  const Numeric & y00, const Numeric & y01, const Numeric & y10, const Numeric &y11,
  float xf, float zf
)
{
//  float y0z = y00*(1-zf) + y01*zf;
//  float y1z = y10*(1-zf) + y11*zf;
//  return y0z*(1-xf) + y1z*xf;
  return y00*((1-zf)*(1-xf)) + y01*(zf*(1-xf)) + y10*((1-zf)*xf) + y11*(zf*xf);
}


static float GrassDistanceFactor(Landscape *land, const Camera &cam, float effClutterDist)
{
  // when we are flying high, we can reasonably assume no clutter is visible
  // to prevent FPS going down when clutter starts being rendered again, we use a continuous solution
  float spd = cam.Speed().SizeXZ();
  float alt = cam.Position().Y()-land->SurfaceY(cam.Position());
  
  float spdFactor = InterpolativC(spd,20,75,1,0);
  float altFactor = InterpolativC(alt,effClutterDist*0.5f,effClutterDist*1.2f,1,0);
  
  float distFactor = floatMin(spdFactor,altFactor);

  #if 0 // _PROFILE
    DIAG_MESSAGE(1000,"Alt %.2f, spd %.2f, Grass factor %.3f,%.3f",alt,spd,altFactor,spdFactor);
  #endif
  
  return distFactor;
}

#if 0 // _PROFILE || _DEBUG

static EventLogger GGrassEventLogger;

#else

static DummyEventLogger GGrassEventLogger;

#endif

bool GroundClutterCache::Calculate(
  Landscape *land, const GridRectangle &rect, Scene &scene, float effClutterDist
)
{
  // rect is given as inclusive - convert to exclusive
  GridRectangle exclusive = rect;
  exclusive.xEnd++;
  exclusive.zEnd++;
  #if _DEBUG
  static bool force = false;
  #else
  const bool force = false;
  #endif
  if (!force && exclusive==_rect) return true;
  
  PROFILE_SCOPE_EX(lDGCG,*);

  int tLog = land->GetTerrainRangeLog()-land->GetLandRangeLog();
  
  //for (int i=0; i<nClutters; i++) _clutterData[i].Clear();
  // Traverse the texture square (land)
  const int t = 1 << tLog;
  const int xxBase = exclusive.xBeg<<tLog;
  const int zzBase = exclusive.zBeg<<tLog;

  {
    PROFILE_SCOPE_EX(clCch,land);
    for (int z=0; z<_clutterData.GetYRange(); z++) for (int x=0; x<_clutterData.GetXRange(); x++)
    {
      GroundClutterCacheItem *item = _clutterData(x,z);
      if (item)
      {
        _cache.Add(item);
        _index.Add(item);
      }
    }
  }
  
  //TODO: consider incremental release / add
  // note: this is not very easy, _clutterData needs to be resize and reallocated
  // lazy caching currently works quite fine and this therefore does not seem to be very important
  
  /*
  // determine shared area
  GridRectangle keep = exclusive&_rect;
  // what is currently used in not cached
  for (int x=0; x<_clutterData.GetXRange(); x++) for (int z=0; z<_clutterData.GetYRange(); z++)
  {
    // release what is not in keep
    int xx = (x+xxBase)>>tLog;
    int zz = (z+zzBase)>>tLog;
    if (Position(xx,zz).IsInside()) continue;
    _cache.Add(_clutterData(x,z));
  }
  */
  // Dim will release everything
  _clutterData.Dim(exclusive.Width()<<tLog,exclusive.Height()<<tLog);

  const int clutterPerGrid = land->GetClutterPerLandGrid();
  const float clutterStep = land->GetLandGrid()/clutterPerGrid;
  const float terrainGridDivClutterStep = land->GetTerrainGrid()/clutterStep;

  GGrassEventLogger.Add(exclusive<<tLog,"lDGCG");
  
  for (int z = exclusive.zBeg; z < exclusive.zEnd; z++)
  for (int x = exclusive.xBeg; x < exclusive.xEnd; x++)
  {

    // Get the texture associated with the land fragment
    const Scenery::TextureInfo &texInfo = land->ClippedTextureInfo(z,x);
    // check layer mask (bimpas, any pass should do, the mask is always the same
    // any material will do - mask is always the same
    const TexMaterial *gMaterial = texInfo._mat;

    if (!gMaterial) continue;
    Ref<LayerMaskCacheItem> layerMaskItem = land->_layerMaskCache->Item(gMaterial);
    if (!layerMaskItem || land->_clutter.Size()<=0) continue;
    DoAssert(layerMaskItem->Loaded());

    // we need to find any world space texgen in the material (actually the stage is always the same - the rvmat is fixed)
    int world = -1;
    for (int i=0; i<gMaterial->_nTexGen; i++)
    {
      if (gMaterial->_texGen[i]._uvSource==UVWorldPos)
      {
        world = i;
        break;
      }
    }
    Assert(world>=0);
    // we need the properties of the surface
    if (world<0) continue;

    AUTO_STATIC_ARRAY(InitPtr<Object>,roadways,32);
    land->GetRoadList(x, z, roadways);

    // Traverse the terrain
    const int xxBeg = x<<tLog, zzBeg = z<<tLog;
    const int xxEnd = xxBeg + t, zzEnd = zzBeg + t;

    for (int zz=zzBeg; zz < zzEnd; zz++) for (int xx=xxBeg; xx < xxEnd; xx++)
    {
      // first search the cache
      GroundClutterCacheItem *cached = FindCached(xx,zz);
      if (cached)
      {
        if (force)
        {
          _index.Remove(cached->_id);
          _cache.Delete(cached);
        }
        else
        {
          _clutterData(xx-xxBase,zz-zzBase) = cached;
          _index.Remove(cached->_id);
          _cache.Delete(cached);
          continue;
        }
      }
      PROFILE_SCOPE_EX(lDGC1,*);
      
      if (PROFILE_SCOPE_NAME(lDGC1).IsActive())
      {
        int xc = toIntFloor(scene.GetCamera()->Position().X()*InvTerrainGrid);
        int zc = toIntFloor(scene.GetCamera()->Position().Z()*InvTerrainGrid);
        
        RString history = GGrassEventLogger.Get(GridRectangle(xx,zz,1,1));
        PROFILE_SCOPE_NAME(lDGC1).AddMoreInfo(Format("%d,%d - cam %d, %d, '%s'",xx,zz,xc,zc,cc_cast(history)));
      }

      GGrassEventLogger.Add(GridRectangle(xx,zz,1,1),"Create");
      
      GroundClutterCacheItem *item = new GroundClutterCacheItem(xx,zz);
      _clutterData(xx-xxBase,zz-zzBase) = item;
      GroundClutterCacheItem &gridCache = *item;
      
      const float terrainGrid = land->GetTerrainGrid();
      const float invTerrainGrid = land->GetInvTerrainGrid();
      //float clutterGrid = land->_clutterGrid;
      
      // we cannot perform any clipping now  - need to create everything

      // consider using GetRectY - caution about yXZ convention
      // (yZX is used here)
      //float y00,y01,y10,y11;
      //land->GetRectY(xx,zz,y00,y01,y10,y11);

      // optimization of SurfaceY:
      
      float y00 = land->ClippedDataXZ(xx,zz);
      float y01 = land->ClippedDataXZ(xx+1,zz);
      float y10 = land->ClippedDataXZ(xx,zz+1);
      float y11 = land->ClippedDataXZ(xx+1,zz+1);

      // we need some neighbour values as well
      // indices 0: min-X, 1: max-X, 2: min-Z, 3: max-Z, 4: diagonal
      float yn0 = land->ClippedDataXZ(xx-1,zz+1);
      float yn1 = land->ClippedDataXZ(xx+2,zz);
      float yn2 = land->ClippedDataXZ(xx+1,zz-1);
      float yn3 = land->ClippedDataXZ(xx,zz+2);
      
      Vector3 c00(xx*terrainGrid,y00,zz*terrainGrid);
      Vector3 c01((xx+1)*terrainGrid,y01,zz*terrainGrid);
      Vector3 c10(xx*terrainGrid,y10,(zz+1)*terrainGrid);
      Vector3 c11((xx+1)*terrainGrid,y11,(zz+1)*terrainGrid);

      // note: if no clutter is using ground lighting, we might skip this part
      // interpolate normal for lighting 
      // consider: we might calculate normals directly using the yXX data we have?
      Vector3Val vl00 = land->ClippedNormal(xx,zz);
      Vector3Val vl10 = land->ClippedNormal(xx+1,zz);
      Vector3Val vl01 = land->ClippedNormal(xx,zz+1);
      Vector3Val vl11 = land->ClippedNormal(xx+1,zz+1);
      
      float dXA = (y01-y00)*invTerrainGrid;
      float dZA = (y10-y00)*invTerrainGrid;

      float dXB = (y11-y10)*invTerrainGrid;
      float dZB = (y11-y01)*invTerrainGrid;

      // partial derivatives for neighbours
      float dXn0 = (y10-yn0)*invTerrainGrid;
      float dZn0 = dZA;
      
      float dXn1 = (yn1-y01)*invTerrainGrid;
      float dZn1 = dZB;
      
      float dXn2 = dXA;
      float dZn2 = (y01-yn2)*invTerrainGrid;
      
      float dXn3 = dXB;
      float dZn3 = (yn3-y10)*invTerrainGrid;
      
      // edge directions - used for convexity test
      Vector3 edge0 = c10-c00;
      Vector3 edge1 = c01-c11;
      Vector3 edge2 = c00-c01;
      Vector3 edge3 = c11-c10;
      Vector3 edgeDA = c01-c10;
      Vector3 edgeDB = c10-c01;
      
      // what we want to check is angle between polygon normals
      Vector3 normA(-dXA,1,-dZA);
      Vector3 normB(-dXB,1,-dZB);
      Vector3 normN0(-dXn0,1,-dZn0);
      Vector3 normN1(-dXn1,1,-dZn1);
      Vector3 normN2(-dXn2,1,-dZn2);
      Vector3 normN3(-dXn3,1,-dZn3);
      
      // check the angle of the edges and diagonal
      float cosAngle[5];
      cosAngle[0] = normN0.CosAngle(normA);
      cosAngle[1] = normN1.CosAngle(normB);
      cosAngle[2] = normN2.CosAngle(normA);
      cosAngle[3] = normN3.CosAngle(normB);
      cosAngle[4] = normA.CosAngle(normB);
      //const float limitCos = 0.96592582629; // cos 15deg
      const static float limitCos = 0.96592582629; // cos 15deg
      
      // check convex or concave
      float edgeSign[5];
      edgeSign[0] = normA.CrossProduct(normN0)*edge0;
      edgeSign[1] = normB.CrossProduct(normN1)*edge1;
      edgeSign[2] = normA.CrossProduct(normN2)*edge2;
      edgeSign[3] = normB.CrossProduct(normN3)*edge3;
      edgeSign[4] = normA.CrossProduct(normB)*edgeDA;
      if (force)
      {
        // diagnostics for edge detection
        static bool diags = false;
        if (diags)
        {
          LODShape *forceArrow = scene.ForceArrow();
          Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

          float size=1;
          Matrix3 orient;
          Vector3 center = (c01+c10)*0.5f;
          #define TO_CENTER_W(v,w) ((v)*(1-(w))+center*w)
          #define TO_CENTER(v) TO_CENTER_W(v,0.02f)
          #define EGDE_COLOR(i) ( \
            cosAngle[i]<limitCos ? \
            PackedColor(Color(1.0,0.2,0.2,0.5)) \
            : PackedColor(Color(0.2,1.0,0.2,0.5)) \
          )
          #define DIAG_COLOR(i) ( \
            cosAngle[i]<limitCos ? \
            PackedColor(Color(1,1,0.2,0.5)) \
            : PackedColor(Color(0.2,0.2,1,0.5)) \
          )
          #define CONV_COLOR(i) ( \
            cosAngle[i]<limitCos && edgeSign[i]<0 ? \
            PackedColor(Color(1,0,1,0.5)) \
            : PackedColor(Color(0,0,1,0.5)) \
          )
          // draw A and B normals - center of the rectangle, blue
          orient.SetDirectionAndUp(normA,VForward);
          arrow->SetPosition(TO_CENTER_W((c00+c01+c10)/3,0.95f));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(DIAG_COLOR(4));
#if _HELISIM_RENDER_TO_TEXTURE
          DoAssert(CheckMainThread());
#endif
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );

          
          orient.SetDirectionAndUp(edgeDA,VUp);
          arrow->SetPosition(TO_CENTER_W((c11+c01+c10)/3,0.95f));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(CONV_COLOR(4));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif            
            );

          orient.SetDirectionAndUp(normB,VForward);
          arrow->SetPosition(TO_CENTER_W((c11+c01+c10)/3,0.95f));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(DIAG_COLOR(4));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
          
          orient.SetDirectionAndUp(edgeDB,VUp);
          arrow->SetPosition(TO_CENTER_W((c11+c01+c10)/3,0.95f));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(CONV_COLOR(4));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
          
          // draw 0..3 normals, edges, green
          orient.SetDirectionAndUp(normN0,VForward);
          arrow->SetPosition(TO_CENTER((c00+c10)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(EGDE_COLOR(0));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );

          orient.SetDirectionAndUp(edge0,VUp);
          arrow->SetPosition(TO_CENTER((c00+c10)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(CONV_COLOR(0));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
          
          orient.SetDirectionAndUp(normN1,VForward);
          arrow->SetPosition(TO_CENTER((c01+c11)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(EGDE_COLOR(1));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );

          orient.SetDirectionAndUp(edge1,VUp);
          arrow->SetPosition(TO_CENTER((c01+c11)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(CONV_COLOR(1));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
          
          orient.SetDirectionAndUp(normN2,VForward);
          arrow->SetPosition(TO_CENTER((c00+c01)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(EGDE_COLOR(2));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );

          orient.SetDirectionAndUp(edge2,VUp);
          arrow->SetPosition(TO_CENTER((c00+c01)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(CONV_COLOR(2));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
          
          orient.SetDirectionAndUp(normN3,VForward);
          arrow->SetPosition(TO_CENTER((c10+c11)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(EGDE_COLOR(3));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
          
          orient.SetDirectionAndUp(edge3,VUp);
          arrow->SetPosition(TO_CENTER((c10+c11)/2));
          arrow->SetOrientSkewed(orient*Matrix3(MScale,size*0.05));
          arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
          arrow->SetConstantColor(CONV_COLOR(3));
          arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
            , scene.GetDrawObjectIndex()
#endif
            );
        }
      }


      // simulate x/z traversal in LandGrid
      // compute starting and ending indices

      int xxxBeg = toIntFloor((xx-xxBeg)*terrainGridDivClutterStep);
      int zzzBeg = toIntFloor((zz-zzBeg)*terrainGridDivClutterStep);
      int xxxEnd = toIntCeil((xx+1-xxBeg)*terrainGridDivClutterStep);
      int zzzEnd = toIntCeil((zz+1-zzBeg)*terrainGridDivClutterStep);
      Assert(xxxBeg>=0);
      Assert(zzzBeg>=0);
      Assert(xxxEnd<=clutterPerGrid+1);
      Assert(zzzEnd<=clutterPerGrid+1);
      saturateMax(xxxBeg,0);
      saturateMax(zzzBeg,0);
      saturateMin(xxxEnd,clutterPerGrid-1);
      saturateMin(zzzEnd,clutterPerGrid-1);

      for (int zzz=zzzBeg; zzz<=zzzEnd; zzz++)
      for (int xxx=xxxBeg; xxx<=xxxEnd; xxx++)
      {
        //if (zzz!=zzzBeg && zzz!=zzzEnd && xxx!=xxxBeg && xxx!=xxxEnd) continue;
        
        int seedXZ=land->_randGen.GetSeed(xxx,zzz);

        // check which layer is prevalent
        // y should not matter here
        Vector3 pos(
          xxBeg*terrainGrid+(xxx+0.5f)*clutterStep,
          0,
          zzBeg*terrainGrid+(zzz+0.5f)*clutterStep
        );
        // material contains mapping from world space into a texture space
        
        Vector3 uv = gMaterial->_texGen[world]._uvTransform.FastTransform(pos);
        // check primary layer
        int layer = layerMaskItem->GetLayer(gMaterial,uv.X(),uv.Y());
        if (layer<0) continue;
        const GroundClutterPosMaps *maps = (const GroundClutterPosMaps *)gMaterial->GetCustomData();
        if (!maps) continue;
        GroundClutterPosMap *posMap = maps->_maps[layer];
        if (!posMap) continue;
        
        posMap->Generate();
        _posMap.AddOrRefresh(posMap);
        
        PosPair cellPos;
        int isHere;
        if (!posMap->GetCellData(xxx,zzz,cellPos,isHere)) continue;
        float xIn = xxx*clutterStep + cellPos.x*clutterStep - (xx-xxBeg)*terrainGrid;
        float zIn = zzz*clutterStep + cellPos.z*clutterStep - (zz-zzBeg)*terrainGrid;
        //float xIn = xxx*clutterStep - (xx-xxBeg)*terrainGrid;
        //float zIn = zzz*clutterStep - (zz-zzBeg)*terrainGrid;
        //Log("pos %.2f,%.2f, xxx=%d,zzz=%d, in %.2f,%.2f",pos.X(),pos.Z(),xxx,zzz,xIn,zIn);
        // avoid generating outside of the grid - neighbour will generate there
        
        if (xIn<0 || zIn<0 || xIn>=terrainGrid || zIn>=terrainGrid)
        {
          continue;
        }

        // clutter presence pattern is defined by the position map
//         int isHere = 0;
        if (isHere<0) continue;

        const Clutter &clutter = land->_clutter[isHere];
        LODShape *shape = clutter._shape;
        if (!shape) continue;

        float scaleMin = clutter._scaleMin;
        float scaleMax = clutter._scaleMax;
        float scale = scaleMin + (scaleMax - scaleMin) * land->_randGen.RandomValue(seedXZ+4);
        
        //float radius = shape->BoundingSphere()*scale;
        // XZ radius may be significantly smaller - we assume object is approximately round in the XZ direction
        float radius = scale*floatMax(-shape->Min().X(),-shape->Min().Z(),shape->Max().X(),shape->Max().Z());
        
        #if 1 // _DEBUG
        //static bool checkEdges = false;
        //if (checkEdges)
        {
          // check if we cross any edge
          
          // we are interested in: rectangle edges
          // we are interested in: rectangle diagonal
          
          // handle rectangle edges
          float xMinIn = xIn-radius, xMaxIn = xIn+radius;
          float zMinIn = zIn-radius, zMaxIn = zIn+radius;
          // determine safe region based on estimated levitation (related to radius)
          const float maxError = 0.10f; // we assume this error acceptable
          // we want error < maxError, where error = sin(alpha)*radius;

          // maxError > sin(alpha)*radius;

          // sin(alpha)=sqrt(1-cos(alpha))
          // maxError > sqrt(1-cos(alpha))*radius
          // (maxError/radius)^2 > 1-cos(alpha)
          // cos(alpha) > 1- (maxError/radius)^2
          const float maxCos = floatMin(1-Square(maxError/radius),limitCos);
          
          // move in the safe region as necessary
          // TODO: move only as necessary, based on error
          if (cosAngle[0]<maxCos && edgeSign[0]>0 && xMinIn<0) xIn -= xMinIn;
          if (cosAngle[1]<maxCos && edgeSign[1]>0 && xMaxIn>terrainGrid) xIn -= xMaxIn-terrainGrid;
          if (cosAngle[2]<maxCos && edgeSign[2]>0 && zMinIn<0) zIn -= zMinIn;
          if (cosAngle[3]<maxCos && edgeSign[3]>0 && zMaxIn>terrainGrid) zIn -= zMaxIn-terrainGrid;

          // handle diagonal - move along rectangle edges to ensure
          // we do not break the alignment we already did
          
          // check distance from diagonal
          // assume diagonal has a form ax+bz+c=0
          // diagonal "normal" is normalize(1,1), i.e. (1/sqrt(2),1/sqrt(2))
          // H_INVSQRT2*xIn+H_INVSQRT2*zIn = -c
          // H_INVSQRT2*terrainGrid = -c
          float diagDist = H_INVSQRT2*xIn+H_INVSQRT2*zIn-H_INVSQRT2*terrainGrid;
          if (cosAngle[4]<limitCos && edgeSign[4]>0 && fabs(diagDist)<radius)
          {
            // we need to increase diagDist to radius
            // result of sqrt(2) movement is 1 m
            float move = (radius-fabs(diagDist))*H_SQRT2;
            // 4 cases, depending on what part of the grid are we in
            if (xIn<zIn)
            {
              if (diagDist<=0) zIn -= move;
              else xIn += move;
            }
            else
            {
              if (diagDist<=0) xIn -= move;
              else zIn += move;
            }
          }
        }
        #endif

        // position fixed - check y and store it
        float dX,dZ,y;
        bool isB;
        if( xIn+zIn<=terrainGrid )
        { // triangle 00,01,10
          dX=dXA;
          dZ=dZA;
          y = y00+dZ*zIn+dX*xIn;
          isB = false;
        }
        else
        {
          // triangle 01,10,11
          dX=dXB;
          dZ=dZB;
          y = y10-y11+y01+dX*xIn+dZ*zIn;
          isB = true;
        }

        // adjust the position after fixing it
        Vector3 itemPos = Vector3(xx*terrainGrid+xIn,y,zz*terrainGrid+zIn);
        //item._radius = radius;

        const SurfaceInfo *surfRoadway = land->CheckRoadway(roadways, itemPos);
        if (surfRoadway && surfRoadway->_terrainClutterFactor<=0)
        {
          continue;
        }
        //float terrainClutterFactor = surfRoadway ? surfRoadway->_terrainClutterFactor : 0;

        // isHere is global index
        // we need local index as well
        int localIndex = gridCache._types.Find(&clutter);
        if (localIndex<0)
        {
          localIndex = gridCache._types.Add(&clutter);
          gridCache._data.Append();
          Assert(gridCache._data.Size()==gridCache._types.Size());
        }
        
        AutoArray<GroundClutterInfo> &array = gridCache[localIndex];
        GroundClutterInfo &info = array.Append();

        info._pos = itemPos;
        info._radius = radius;
        info._flattenSkewX = 0;
        info._flattenSkewZ = 0;
        info._flattenTime = TIME_MIN;

        float azimut = land->_randGen.RandomValue(seedXZ+3)*(2*H_PI);

        info.SetSeedXZ(seedXZ,land);
        info._dX = dX;
        info._dZ = dZ;
        //info.isB = isB;
        
        // check sat. map color and detail texture color at given position
        Color satPixel = layerMaskItem->GetSatMap(gMaterial,uv.X(),uv.Y());
        if (clutter._isColored)
        {
          // coloring based on the original texture
          info._colorRatio = satPixel/clutter._surfColor;
          info._colorRatio.SaturateRGBMinMax(0.5f,2.0f);
        }
        else
        {
          // coloring based on the clutter color
          if (clutter._shape->NLevels()>0)
          {
            info._colorRatio = satPixel/clutter._shape->LevelRef(0).GetColor();
          }
          else
          {
            info._colorRatio = HWhite;
          }
          info._colorRatio.SaturateRGBMinMax(0.33f,3.0f);
        }
        
        // interpolation based on x/z position
        float xf = xIn*land->GetInvTerrainGrid();;
        float zf = zIn*land->GetInvTerrainGrid();
        info._norm = BilintEx(vl00,vl01,vl10,vl11,xf,zf);
        info._scale = scale;
        // we do not normalize - this way we get similar artifacts as terrain rendering
        if (clutter._noWindAffectionScale>=scale && clutter._noFlattenScale>=scale)
        {
          // we can skew now, as nobody will be touching the matrix any more
          Matrix3 skew = M3Identity;
          skew(1,0) = dX;
          skew(1,2) = dZ;
          info._orient = skew*Matrix3(MRotationY,azimut)*scale;
        }
        else
        {
          info._orient = Matrix3(MRotationY,azimut)*scale;
        }

      }
      item->CalculateMinMax(land);
      item->Compact();
    }
  }
  _rect = exclusive;

  // we do the flush after we reused what can be reused
  // one item is for one terrain grid
  // avoid the cache getting too large - we need to be able to walk it

  {
    PROFILE_SCOPE_EX(lDGCC,land);
    
    // note: sometimes the clutter is completely disabled
    // no need to check this, in such case we should not be called at all
    int cacheSizeWanted = toIntCeil(Square(effClutterDist/land->GetTerrainGrid()))*16;
    while (_cache.ItemCount()>cacheSizeWanted)
    {
      GroundClutterCacheItem *item = _cache.First();
      GGrassEventLogger.Add(GridRectangle(item->_id._x,item->_id._z,1,1),"Delete trim");
      // we know it is not linked by _clutterData now
      _index.Remove(item->_id);
      _cache.Delete(item);
    }
  }
  
  {
    PROFILE_SCOPE_EX(lDGCT,land);
    // we need to check what clutter types are present in the cache
    _types.Clear();
    for (int z=0; z<_clutterData.GetYRange(); z++)
    for (int x=0; x<_clutterData.GetXRange(); x++)
    {
      GroundClutterCacheItem *item = _clutterData(x,z);
      if (item)
      {
        _types.Merge(item->_types);
      }
    }
    // use QSort to build the permutation
    AutoArray<TypeOrder, MemAllocLocal<TypeOrder,64> > order;
    order.Resize(_types.Size());
    for (int i=0; i<_types.Size(); i++)
    {
      order[i].index = i;
      order[i].key = _types.Get(i);
    }
    QSort(order,TypeOrder::Compare);
    
    // store the permutation  
    _typeRenderOrder.Realloc(order.Size());
    _typeRenderOrder.Resize(order.Size());
    for (int i=0; i<order.Size(); i++)
    {
      _typeRenderOrder[i] = order[i].index;
    }
  }
  
  return true;  
}

GroundClutterCache::GroundClutterCache()
{
  RegisterFreeOnDemandMemory(this);
  _effClutterDist = 0.0f;
	_prepareSucceeded = false;
}

void GroundClutterCache::Clear()
{
  _clutterData.Clear();
  _index.Clear();
  _cache.Clear();
  _rect = GridRectangle(0,0,0,0);
  _types.Clear();
  _typeRenderOrder.Clear();
  _effClutterDist = 0.0f;
}

void GroundClutterCache::ForceUpdate(Landscape *l, int x, int z)
{
  const int tLog = l->GetSubdivLog();
  if (x>=(_rect.xBeg<<tLog) && x<(_rect.xEnd<<tLog) && z>=(_rect.zBeg<<tLog) && z<(_rect.zEnd<<tLog))
  {
    // it is in the active region as well
    // we might update it there
    // or we can flush the whole active region into the cache

    for (int z=0; z<_clutterData.GetYRange(); z++) for (int x=0; x<_clutterData.GetXRange(); x++)
    {
      GroundClutterCacheItem *item = _clutterData(x,z);
      if (item)
      {
        _cache.Add(item);
        _index.Add(item);
      }
    }
    // Dim will force all Ref to be released
    // it will not reallocate the array memory, though
    _clutterData.Dim(_clutterData.GetXRange(),_clutterData.GetYRange());
    // mark active area as invalid
    _rect = GridRectangle();
  }
  GroundClutterCacheItem *item = FindCached(x,z);
  if (item)
  {
    // we know it is not linked by _clutterData now
    GGrassEventLogger.Add(GridRectangle(item->_id._x,item->_id._z,1,1),"Delete force");
    _index.Remove(item->_id);
    _cache.Delete(item);
  }
}

size_t GroundClutterCache::FreeOneItem()
{
  // never release the last item - it may be needed as a part of the active request
  GroundClutterCacheItem *item = _cache.First();
  if (!item) return 0;
  GGrassEventLogger.Add(GridRectangle(item->_id._x,item->_id._z,1,1),"Delete free1");
  size_t released = item->GetStoredMemoryControlled();
  _index.Remove(item->_id);
  _cache.Delete(item);
  return released;
}

float GroundClutterCache::Priority() const
{
  // when clutter is dense, it can take quite a lot of memory
  return 0.01f;
}
size_t GroundClutterCache::MemoryControlled() const
{
  return _cache.MemoryControlled();
}
size_t GroundClutterCache::MemoryControlledRecent() const
{
  return _cache.MemoryControlledRecent();
}
RString GroundClutterCache::GetDebugName() const
{
  return "Clutter";
}

void GroundClutterCache::MemoryControlledFrame()
{
  _cache.Frame();
}

bool Landscape::PreloadGroundClutter(Vector3Par pos, float radius, bool logging)
{
  float viewDist,gridSize;
  GWorld->GetCurrentTerrainSettings(viewDist,gridSize,GWorld->GetMode());

  if (gridSize>=49)
  {
    // terrain detail very low - no ground clutter
    return true;
  }

  // compute a factor, gridSize == 12.5 should give 1.0, 3.125 should give 2x
  // sqrt(12.5/a) = sqrt(12.5)/sqrt(a)
  float effClutterDist = EffectiveClutterDist(gridSize);
  
  effClutterDist *= GrassDistanceFactor(this,*GScene->GetCamera(),effClutterDist);
  
  if (effClutterDist<=0)
    return true; // when preload is not needed, report it as completed

  // view frustum may contain something outside of the circle
  // but we never need to display more than a sphere describes
  // what is beyond the range is never visible anyway
  GridRectangle landRectBegEnd;

  float iGrid = GetInvLandGrid();
  landRectBegEnd.xBeg = toIntFloor((pos.X()-radius)*iGrid);
  landRectBegEnd.zBeg = toIntFloor((pos.Z()-radius)*iGrid);
  landRectBegEnd.xEnd = toIntCeil((pos.X()+radius)*iGrid);
  landRectBegEnd.zEnd = toIntCeil((pos.Z()+radius)*iGrid);

  if (landRectBegEnd.xBeg<0) landRectBegEnd.xBeg = 0;
  if (landRectBegEnd.xEnd>=GetLandRange()) landRectBegEnd.xEnd = GetLandRange()-1;
  if (landRectBegEnd.zBeg<0) landRectBegEnd.zBeg = 0;
  if (landRectBegEnd.zEnd>=GetLandRange()) landRectBegEnd.zEnd = GetLandRange()-1;

  // TODO: traverse satellite grid instead
  for (int z = landRectBegEnd.zBeg; z <= landRectBegEnd.zEnd; z++)
  for (int x = landRectBegEnd.xBeg; x <= landRectBegEnd.xEnd; x++)
  {

    // Get the texture associated with the land fragment
    const Scenery::TextureInfo &texInfo = _terrain->_texture[GetTex(x,z)];
    
    // check layer mask
    // any material will do - mask is always the same
    const TexMaterial *mat = texInfo._mat;
    if (!mat) continue;
    
    // note: may return NULL when data cannot be valid. This should be considered as ready
    bool ready = false;
    Ref<LayerMaskCacheItem> layerMaskItem = _layerMaskCache->Item(mat,false,&ready);
    if (!ready)
    {
      if (logging)
      {
        LogF("  not done, layer mask %d,%d",x,z);
      }
      return false;
    }
  }

  GroundClutterCache &cache = *_groundClutterCache;
  
  // make sure cache is up to date
  GridRectangle gcBegEnd;
  GScene->CalculBoundingRect(gcBegEnd, effClutterDist, GetLandGrid());
  cache.Calculate(this,gcBegEnd,*GScene,effClutterDist);
  
  // make sure some textures are loaded for clutters used 
  bool ret = true;
  if (_grassNoiseTexture)
  {
    if (!_grassNoiseTexture->PrepareMipmap0(false))
    {
      if (logging)
      {
        LogF("  not done, noise texture");
      }
      ret = false;
    }
  }
  for (int i=0; i<cache._types.Size(); i++)
  {
    const Clutter *ci = cache._types.Get(i);
    LODShape *shape = ci->_shape;
    if (shape && shape->NLevels()>0)
    {
      const ShapeUsed &level0 = shape->Level(0);
      if (!level0->PreloadTextures(Square(5), NULL))
      {
        if (logging)
        {
          LogF("  not done, clutter %s texture",cc_cast(shape->GetName()));
        }
        ret = false;
      }
    }
  }
  return ret;
}

float Landscape::EffDissapearDist() const 
{ 
  // test is clutter enable/disable in video options
  return (_groundClutterCache.NotNull() && _groundClutterCache->_clutterData.Size1D() > 0) ? _groundClutterCache->_effClutterDist : _endDisappearDistGMax; 
}

void Landscape::ForceClutterUpdate(Object *obj)
{
  float radius = obj->GetRadius();
  float xMinF = obj->FutureVisualState().Position().X()-radius;
  float xMaxF = obj->FutureVisualState().Position().X()+radius;
  float zMinF = obj->FutureVisualState().Position().Z()-radius;
  float zMaxF = obj->FutureVisualState().Position().Z()+radius;
  float invTerrainGrid = GetInvTerrainGrid();
  int xMin=toIntFloor(xMinF*invTerrainGrid);
  int xMax=toIntFloor(xMaxF*invTerrainGrid);
  int zMin=toIntFloor(zMinF*invTerrainGrid);
  int zMax=toIntFloor(zMaxF*invTerrainGrid);
  // no need to saturate - items outside may be valid
  for (int z=zMin; z<=zMax; z++) for (int x=xMin; x<=xMax; x++)
  {
    _groundClutterCache->ForceUpdate(this,x,z);
  }
}

void Landscape::FlushGroundClutterCache()
{
  _groundClutterCache->Clear();
}


/// structure for ordering object instances by z
struct ItemInfoZ
{
  int _index;
  float _zCoord;
  
  ItemInfoZ(){}
  ItemInfoZ(int index, float zCoord):_index(index),_zCoord(zCoord){}
};

TypeIsSimple(ItemInfoZ)

static inline int CompareInstByZ(const ItemInfoZ *i1, const ItemInfoZ *i2)
{
  // big z goes first
  return sign(i2->_zCoord-i1->_zCoord);
}

typedef MemAllocSimpleStorage<int,64*1024/sizeof(int)> SlotStorage;
typedef MemAllocSimple<SlotStorage> SlotAllocator;
typedef AutoArray<GroundClutterDesc,SlotAllocator> SlotArray;

/// starting indices in _slots for each clutter type
struct ClutterStartIndices: public AutoArray< int, MemAllocLocal<int,64> >
{
};

/// accumulated instances from all grid fields together
struct ClutterAccumulator
{
  /// accumulated instances from all grid fields together
  AutoArray< SlotArray, MemAllocLocal<SlotArray,256> > _slots;

  ClutterAccumulator(int slots, SlotStorage &slotStorage)
  {
    // allocate as many slots as needed
    _slots.Resize(slots);
    // each slots needs to use the allocator
    for (int i=0; i<_slots.Size(); i++)
    {
      _slots[i].SetStorage(slotStorage);
    }
  }

  void Draw(
    int cb, const ClutterStartIndices &startIndices, const Clutter &lClutter, int skipLods, const Landscape *l,
    int index, const LightList &lights, float clutterDist2, const WindEmitterList &windList
#if _HELISIM_RENDER_TO_TEXTURE
    , int drawListId
#endif
  );

  static void GetLevelRange(int &nLevels, int &startLevelm, const Clutter &lClutter, int skipLods);
};

void ClearClutterDrawCache()
{
}

void ClutterAccumulator::GetLevelRange(int &nLevels, int &startLevel, const Clutter &lClutter, int skipLods )
{
  // convert from descriptions to instances
  nLevels = lClutter._shape->FindSimplestLevel()+1;
  startLevel = 0;
  if (nLevels>0)
  {
    startLevel = skipLods;
    if (startLevel >= nLevels) startLevel = nLevels - 1;
    nLevels -= skipLods;
    if (nLevels<1) nLevels = 1;
  }
}


/*!
  \patch 5088 Date 11/15/2006 by Flyman
  - Fixed: LOD selection for clutters was not working properly
*/
void ClutterAccumulator::Draw(
  int cb, const ClutterStartIndices &startIndices, const Clutter &lClutter, int skipLods, const Landscape *l,
  int index, const LightList &lights, float clutterDist2, const WindEmitterList &windList
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  // LODs are sorted by z
  // we should decide what lod range do we want to use for which ranges
  // then split by LOD
  // convert GroundClutterDesc to ObjectInstanceInfo
  

  // which clutter is present in given slot
  LODShape *lShape = lClutter._shape;
  if (!lShape) return;
  
  int nLevels,startLevel;
  GetLevelRange(nLevels,startLevel, lClutter, skipLods);

  // reversed LOD ordering to make sure alpha sorting is OK
  int beg = nLevels-1;
  int end = -1;
  int step = -1;
  // sometimes we do not need alpha sorting, though
  if (!lClutter._isAlpha)
  {
    beg = 0;
    end = nLevels;
    step = +1;
  }
  for (int level=beg; level!=end; level+=step)
  {
    int slot = startIndices[index]+level;
    SlotArray &array = _slots[slot];
    if (array.Size()<=0) continue;

    int sLevel = startLevel+level;
    
    // VB should be already created from ConvertToVBufferClutterHelper
    // to be safe we can render only LODs using VBs we have
    
    int drawLOD = lShape->UseLevelQuick(sLevel);
    if (drawLOD<0) continue;
    const Shape *shape = lShape->GetLevelLocked(drawLOD);
    if (!shape->CheckTexturesReady() && !shape->MakeTexturesReady()) continue;
    
    //ShapeUsed shape = lShape->Level(sLevel);
    //shape->ConvertToVBuffer(VBStatic, lShape, sLevel);

    Object *object = lClutter._obj;
    DrawParameters dp;
    
    const int maxInstances = 128;
    AutoArray<ObjectInstanceInfo, MemAllocLocal<ObjectInstanceInfo,maxInstances,AllocAlign16> > instances;
    
    {
      PROFILE_SCOPE_DETAIL_EX(clIns,oSort)
      
      const LightSun *sun = GScene->MainLight();
      // for alpha we want back to front ordering
      int iBeg = 0;
      int iEnd = array.Size();
      int iStep = +1;
      // for non-alpha we want roughly front to back
      if (!lClutter._isAlpha)
      {
        iBeg = array.Size()-1;
        iEnd = -1;
        iStep = -1;
      }
      for (int i=iBeg; i!=iEnd; i+=iStep)
      {
        ObjectInstanceInfo &inst = instances.Append();
        GroundClutterDesc &desc = array[i];
        
        // while writing we go through the L1/L2 cache as well
        // prefetching helps writes too
        // note: on Xbox we might be able to use __dcbz128, as we are writing large and contiguous data
        static int offsetD = 256;
        static int offsetS = 256;
        PrefetchT0Off(&inst,offsetD);
        PrefetchT0Off(&desc,offsetS);
        // Setting the instance information
        {
          // Calculate the ground diffuse coefficient
          float groundDiffuseCoef =  sun->LightDirection() * desc.info->_norm;
          saturate(groundDiffuseCoef, 0, 1);

          // Set the instance information
          desc.info->ConvertToInstance(inst,desc.disappear,lClutter,windList,groundDiffuseCoef);
        }
        
        // avoid over-allocating instances buffer
        // instancing is not able to handle that much anyway
        if (instances.Size()>=maxInstances)
        {
          object->DrawSimpleInstanced(
            cb, drawLOD, ClipAll, dp, instances.Data(), instances.Size(),
            clutterDist2, lights, NULL,FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
            , drawListId
#endif
          );
          instances.Clear();
        }      
      }
    }

    // assume grass is always very near
    object->DrawSimpleInstanced(
      cb, drawLOD, ClipAll, dp, instances.Data(), instances.Size(),
      clutterDist2, lights, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
      , drawListId
#endif
    );
    // Zero the drawn clutters array
    array.Clear();
  }
  
}

/// finding LOD based on z-distance
struct LODLimit: public AutoArray<float, MemAllocLocal<float,16> >
{
  int FindLOD(float dist2) const
  {
    // first LOD is always adequate
    int last = 0;
    for (int i=0; i<Size(); i++)
    {
      float lodDist2 = Get(i);
      // if lod is adequate, lodZ<=z
      // we want to get the last adequate lod
      if (lodDist2<=dist2)
      {
        last = i;
      }
      else
      {
        // once one is inadequate, all the rest is inadequate as well
        break;
      }
      
    }
    return last;
  }
};

void Landscape::AddOrRefreshGroundClutterPosMap(GroundClutterPosMap *posMap)
{
  _groundClutterCache->_posMap.AddOrRefresh(posMap);
}

class EntityGrassFlatten
{
  GroundClutterCache &_cache;
  Landscape *_land;
  Vector3 _pos;
  float _radius;

public: 
  EntityGrassFlatten(GroundClutterCache &cache, Landscape *land, Vector3Par pos, float radius)
    :_cache(cache),_land(land),_pos(pos),_radius(radius)
  {
  }
  bool operator () (Entity *entity) const
  {
    Vector3 flattenPos;
    float maxFlattenRadius = entity->IsGrassFlatenner(_pos,_radius,flattenPos);
    if (maxFlattenRadius<=0) return false;
    _cache.Flatten(_land,entity,flattenPos,maxFlattenRadius);
    return false;
  }
};

void Landscape::PrepareGroundClutter(Scene &scene)
{
  PROFILE_SCOPE_GRF_EX(lPGCl,*,SCOPE_COLOR_YELLOW);

  // Get the clutter cache
  GroundClutterCache &cache = *_groundClutterCache;

  cache._prepareSucceeded = false;

  bool someClutter = false;
  for (int i=0; i<_clutter.Size(); i++)
  {
    if (_clutter[i]._obj.NotNull()) someClutter = true;
  }
  if (!someClutter) return;
  // draw ground clutter
  // similar to cloud drawing

  // based on Terrain Detail settings we may want to skip ground clutter, reduce it or extend it

  float viewDist,gridSize;
  GWorld->GetCurrentTerrainSettings(viewDist,gridSize,GWorld->GetMode());

  if (gridSize>=49)
  {
    // terrain detail very low - no ground clutter
    return;
  }

  // compute a factor, gridSize == 12.5 should give 1.0, 3.125 should give 2x
  // sqrt(12.5/a) = sqrt(12.5)/sqrt(a)
  float effClutterDist0 = EffectiveClutterDist(gridSize);

  effClutterDist0 *= GrassDistanceFactor(this,*GScene->GetCamera(),effClutterDist0);
  if (effClutterDist0<=0)
    return;

  cache._effClutterDist = effClutterDist0;

  // If there is either a coarse scene complexity or shading level, select coarse clutter lods as well
  {
    float complexityTarget = scene.GetComplexityTarget();
    if (complexityTarget<175000) cache._skipLods = 3;
    else if (complexityTarget<250000) cache._skipLods = 2;
    else if (complexityTarget<250000) cache._skipLods = 1;
    else cache._skipLods = 0;
  }

  // shading quality line is as follows: 100,10,7,3,0

  // under 175000 we want no ground clutter at all
  //if (complexityTarget<175000) return;

  // TODO: with low complexity we want some slight degradation
  /*
  // this needs to be done where the cache content is computed
  int complexStep = 1;
  if (complexityTarget<250000) complexStep = 2;
  */

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DEWind))
  {
    LODShape *forceArrow=scene.ForceArrow();
    Vector3 dir = GetWindSpeed();

    Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);

    float size=dir.Size();
    Matrix3 orient;
    orient.SetDirectionAndUp(dir,VUp);

    Vector3 pos = scene.GetCamera()->PositionModelToWorld(Vector3(0,0,10));

    arrow->SetPosition(PointOnSurface(pos.X(),0.5,pos.Z()));
    arrow->SetOrientSkewed(orient*Matrix3(MScale,0.1,0.1,size*0.05));
    arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
    arrow->SetConstantColor(PackedColor(Color(0.2,0.2,1,0.5)));
#if _HELISIM_RENDER_TO_TEXTURE
    DoAssert(CheckMainThread());
#endif
    arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif
      );

    arrow->SetPosition(PointOnSurface(pos.X(),1.0,pos.Z()));
    arrow->SetOrient(orient*Matrix3(MScale,0.1,0.1,0.1));
    arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()));
    arrow->SetConstantColor(PackedColor(Color(0.5,0.4,0.4,0.3)));
    arrow->Draw(-1, 0, ClipAll, DrawParameters::_default, InstanceParameters::_default, 0.1f, arrow->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif
      );
  }
#endif

  // Get the camera
  const Camera &camera=*scene.GetCamera();

  cache._windEmiterList.Clear();
  
  // Wind emitters
  GWorld->GetWindEmitterList(cache._windEmiterList,camera.Position(),effClutterDist0);


  scene.CalculBoundingRect(cache._gcBegEnd, effClutterDist0, GetLandGrid());
  cache._prepareSucceeded = cache.Calculate(this,cache._gcBegEnd, scene, effClutterDist0);

  // flatten all cache as needed
  {
    PROFILE_SCOPE_DETAIL_GRF_EX(gcFlt,oSort,0);
    GWorld->ForEachVehicle(EntityGrassFlatten(cache,this,camera.Position(),effClutterDist0));
  }
}

/// can be used to hotfix grass CB problems
static const bool GrassCBs = true;

/// flexible double loop - inner can be x or z
struct FlexibleTerrainLoop
{
  int c0InnerBeg,c0InnerEnd,c0InnerStep;
  int c1InnerBeg,c1InnerEnd,c1InnerStep;

  int c0OuterBeg,c0OuterEnd,c0OuterStep;
  int c1OuterBeg,c1OuterEnd,c1OuterStep;

  /// determine which coordinate is x and which z
  int mapCToX, mapCToZ;
};

struct DrawClutterHelperContext
{
  Scene &scene;
  const Camera &camera;
  float effClutterDist;
  float clutterDist2;
  Matrix4Val toCamera;
  const Array<LODLimit> &lodLimit;
  bool useLights;
  int slots;

  /**
  Non-obvious syntactic rule used: initializers expect member name left, but evaluate based on scope in the ()
  This allows us to use the same names for parameters and members.
  */  
  DrawClutterHelperContext(
    Scene &scene, const Camera &camera, float effClutterDist,
    float clutterDist2, Matrix4Val toCamera, const Array<LODLimit> &lodLimit, bool useLights, int slots
  )
  :scene(scene),camera(camera),effClutterDist(effClutterDist),
  clutterDist2(clutterDist2),toCamera(toCamera),lodLimit(lodLimit),useLights(useLights),slots(slots)
  {
  
  }
};
  


void Landscape::ConvertToVBufferClutterHelper(const DrawClutterHelperContext &ctx, int x, int z, GroundClutterCache &cache)
{
  // use some distance estimation
  // distance of the nearest point to the camera
  Vector3Val camPos = ctx.camera.Position();

  const int tLog = GetTerrainRangeLog()-GetLandRangeLog();
  const int t = 1<<tLog;
  int xg = (x>>tLog)+cache._rect.xBeg, zg = (z>>tLog)+cache._rect.zBeg;
  
  float landGrid = GetLandGrid();
  float xgMin = xg*landGrid, xgMax = (xg+1)*landGrid;
  float zgMin = zg*landGrid, zgMax = (zg+1)*landGrid;

  // we are not too far from camera - verify we have sensible numbers
  Assert(fabs(camPos.X()-xgMin)<500);
  Assert(fabs(camPos.Z()-zgMin)<500);
  Assert(fabs(camPos.X()-xgMax)<500);
  Assert(fabs(camPos.Z()-zgMax)<500);

  // calculate/estimate min. z coord for given grid
  // TODO: calculate 3D distance instead of 2D
  float xDistance = 0, zDistance = 0;
  if (camPos.X()<xgMin) xDistance = xgMin-camPos.X();
  if (camPos.X()>xgMax) xDistance = camPos.X()-xgMax;
  if (camPos.Z()<zgMin) xDistance = zgMin-camPos.Z();
  if (camPos.Z()>zgMax) xDistance = camPos.Z()-zgMax;
  float nearestDist2 = xDistance*xDistance+zDistance*zDistance;

  // scan cache items and create VBs as needed
  for (int zz=0; zz<t; zz++) for (int xx=0; xx<t; xx++)
  {
    // no need to refresh the cache - used items are actually not cached
    const GroundClutterCacheItem *item = cache._clutterData(x+xx,z+zz);
    if (!item || item->Size()==0) continue;
    // for each clutter 

    // empirical tests show clipping here eliminates around 65 % of items
    ClipFlags mayBeClipped;
    if (ctx.camera.IsClipped(item->GetBCenter(),item->GetBSphere(),&mayBeClipped))
    {
      continue;
    }

#ifdef GEOM_OCCLUDERS
		if(!CheckAgainstOccluders(enf::AAB(item->GetMinMax()[0], item->GetMinMax()[1])))
			continue;
#endif
    for (int t=0; t<item->Size(); t++)
    {
      const Clutter &clutter = *item->GetType(t);
      int nLevels,startLevel;
      ClutterAccumulator::GetLevelRange(nLevels,startLevel, clutter, cache._skipLods);
      if (nLevels<=0) continue;
      int endLevel = nLevels+startLevel;

      // if lod is "too good", we can use another one
      int distLevel = ctx.lodLimit[t].FindLOD(nearestDist2);
      if (distLevel>=endLevel) distLevel = endLevel-1;
      if (distLevel<startLevel) distLevel = startLevel;

      for (int level=distLevel; level<endLevel; level++)
      {
        const Shape *shape = clutter._shape->GetLevelLocked(level);
        if (shape) shape->ConvertToVBuffer(VBStatic, clutter._shape, level);
        // we could lock the buffer to avoid discarding it
        // however if recent buffers get discarded, we are in trouble anyway
        // we just let the system handle it by not rendering the model if needed
      }
    }
  }
}

void Landscape::DrawClutterHelper(
  int cb, const ClutterStartIndices &startIndices, ClutterAccumulator &clutters, const FlexibleTerrainLoop &loop,
  GroundClutterCache &cache, int x, int z, const DrawClutterHelperContext &ctx
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  const int tLog = GetTerrainRangeLog()-GetLandRangeLog();
  int xg = (x>>tLog)+cache._rect.xBeg, zg = (z>>tLog)+cache._rect.zBeg;
  

  Assert(abs(loop.c0InnerBeg-loop.c0InnerEnd)==(1<<tLog));
  Assert(abs(loop.c1InnerBeg-loop.c1InnerEnd)==(1<<tLog));

  // use some distance estimation
  // distance of the nearest point to the camera
  Vector3Val camPos = ctx.camera.Position();

  float landGrid = GetLandGrid();
  float xgMin = xg*landGrid, xgMax = (xg+1)*landGrid;
  float zgMin = zg*landGrid, zgMax = (zg+1)*landGrid;

  // we are not too far from camera - verify we have sensible numbers
  Assert(fabs(camPos.X()-xgMin)<500);
  Assert(fabs(camPos.Z()-zgMin)<500);
  Assert(fabs(camPos.X()-xgMax)<500);
  Assert(fabs(camPos.Z()-zgMax)<500);

  // calculate/estimate min. z coord for given grid
  // TODO: calculate 3D distance instead of 2D
  float xDistance = 0, zDistance = 0;
  if (camPos.X()<xgMin) xDistance = xgMin-camPos.X();
  if (camPos.X()>xgMax) xDistance = camPos.X()-xgMax;
  if (camPos.Z()<zgMin) xDistance = zgMin-camPos.Z();
  if (camPos.Z()>zgMax) xDistance = camPos.Z()-zgMax;
  float nearestDist2 = xDistance*xDistance+zDistance*zDistance;
  
  // copying to local variables helps compiler assuming no aliasing
  int c0InnerBeg = loop.c0InnerBeg;
  int c0InnerEnd = loop.c0InnerEnd;
  int c0InnerStep = loop.c0InnerStep;
  int c1InnerBeg = loop.c1InnerBeg;
  int c1InnerEnd = loop.c1InnerEnd;
  int c1InnerStep = loop.c1InnerStep;
  int mapCToX = loop.mapCToX;
  int mapCToZ = loop.mapCToZ;
  float invEffClutterDist = 1.0f/ctx.effClutterDist;


  // traverse terrain grid
  for (int c0Inner=c0InnerBeg; c0Inner!=c0InnerEnd; c0Inner+=c0InnerStep) for (int c1Inner=c1InnerBeg; c1Inner!=c1InnerEnd; c1Inner+=c1InnerStep)
  {
    int cInner[2] = {c0Inner,c1Inner};

    int xx = cInner[mapCToX];
    int zz = cInner[mapCToZ];


    int xxx = (cache._rect.xBeg<<tLog)+x+xx;
    int zzz = (cache._rect.zBeg<<tLog)+z+zz;

    // cull by distance
    Vector3 terrainItemPos(GetTerrainGrid() * (xxx + 0.5f), 0.0f, GetTerrainGrid() * (zzz + 0.5f));

    if (terrainItemPos.DistanceXZ2(ctx.camera.Position()) > Square(ctx.effClutterDist+GetTerrainGrid()*H_SQRT2)) continue;

    // no need to refresh the cache - used items are actually not cached
    GroundClutterCacheItem *item = cache._clutterData(x+xx,z+zz);
    if (!item || item->Size()==0) continue;
    GroundClutterCacheItem &data = *item;
    // apply view-frustum culling here
    // TODO: use min-max box as well
    ClipFlags mayBeClipped;
    if (ctx.camera.IsClipped(data.GetBCenter(),data.GetBSphere(),&mayBeClipped))
    {
      continue;
    }

#ifdef GEOM_OCCLUDERS
		if(!CheckAgainstOccluders(enf::AAB(data.GetMinMax()[0], data.GetMinMax()[1])))
			continue;
#endif
    
    for (int ii=0; ii<data.Size(); ii++)
    {
      // we have one clutter type - convert it to instances
      const Clutter *lClutter = data.GetType(ii);
      if (!lClutter->_obj) continue;

      // map type into the global table
      int globalType = cache._types.Find(lClutter);
      Assert(globalType>=0);
      float bRadius = lClutter->_shape->BoundingSphere()*1.3f;

      const int maxInstancesInGrid = 256;
      AutoArray<GroundClutterDesc, MemAllocLocal<GroundClutterDesc,maxInstancesInGrid,AllocAlign16> > items;
      AutoArray<GroundClutterInfo> &clutter = data[ii];

      float startDisappearDist = lClutter->_startDisappearDist;
      float endDisappearDist = lClutter->_endDisappearDist;
      #if _ENABLE_CHEATS
      static bool handTune = false;
      if (handTune)
      {
        float shapeHeight = lClutter->_shape->BoundingCenter().Y()+lClutter->_shape->Max().Y();
        static float disap20cm = 20.0f;
        static float disap100cm = 50.0f;
        static float disapE20cm = 100.0f;
        static float disapE100cm = 400.0f;
        startDisappearDist = Interpolativ(shapeHeight,0.2f,1.0f,disap20cm,disap100cm);
        endDisappearDist   = Interpolativ(shapeHeight,0.2f,1.0f,disapE20cm,disapE100cm);
      }
      #endif

      const static float minRandDist = 0.5f;
      const static float maxRandDist = 1.0f;
      const float minRandDistClutter = floatMin(minRandDist,startDisappearDist*invEffClutterDist);
      const float maxRandDistClutter = floatMin(maxRandDist,endDisappearDist*invEffClutterDist);
      
      for (int c=0; c<clutter.Size(); c++)
      {
        GroundClutterInfo &item = clutter[c];

        // randomize disappearing

        const int offset = 256;
        // L2 traffic is very significant here
        // TODO: perhaps we should create an array containing positions only?
        PrefetchT0Off(&item,offset);

        float dist2 = ctx.camera.Position().Distance2(item._pos);
        // sometimes we are able to predict the result
        if (dist2>=Square(ctx.effClutterDist)) continue;
        
        // for small clutters we can start disappearing a lot sooner
        // for disappearing we want to always use cache._effClutterDist, so that it does not depend on distance limit
        float maxDist = cache._effClutterDist*(item.GetRandomValue(3)*(maxRandDistClutter-minRandDistClutter)+minRandDistClutter);
        if (dist2>=Square(maxDist)) continue;

        if (mayBeClipped!=ClipNone)
        {
          // something may be clipped: we should check this point clipping
          ClipFlags clip = ctx.camera.IsClipped(item._pos,bRadius);
          if (clip!=ClipNone ) continue;
        }

        float disappear = sqrt(dist2)/maxDist*2-1;
        if (disappear>=0.996f) continue;
        

        GroundClutterDesc &desc = items.Append();
        desc.info = &item;
        // we want to allow slight negative disappear, as this causes LODs more varied
        desc.disappear = floatMax(disappear,-0.25f);

        desc.lx = short(x+xx);
        desc.lz = short(z+zz);
      }

      // sort clutters from current grid
      if (!_someClutterAlpha)
      {
        // while traversing it, select LODs based on z
        for (int i=0; i<items.Size(); i++)
        {
          const GroundClutterDesc &gItem = items[i];

          //float z = (ctx.toCamera * gItem.info->_pos).Z();
          float z2 = ctx.camera.Position().Distance2(gItem.info->_pos);

          // alpha disappearing should affect lod selection
          float disappearArea = z2/Square(1-gItem.disappear);
          // if lod is "too good", we can use another one
          // TODO: incremental level here, by currentLod
          int level = ctx.lodLimit[globalType].FindLOD(disappearArea);

          #if 0 // _ENABLE_CHEATS
          if (CHECK_DIAG(DEModel))
          {
            int watchSeedXZ=_randGen.GetSeed(WatchClutterX,WatchClutterZ);
            if (watchSeedXZ==gItem.info->_seedXZ && z2<Square(20))
            {
              DIAG_MESSAGE(200,"Clutter lod %d, z %.2f, area %.3f",level,sqrt(z2),disappearArea);
            }
          }
          #endif


          int slot = startIndices[globalType]+level;

          clutters._slots[slot].Append() = gItem;
        }
      }
      else
      {
        AutoArray<ItemInfoZ, MemAllocLocal<ItemInfoZ,maxInstancesInGrid> > sortItems;
        // sort newly added items by z
        // create an index for sorting
        sortItems.Resize(items.Size());
        for (int s=0; s<sortItems.Size(); s++)
        {
          const GroundClutterDesc &item = items[s];
          Vector3Val itemPos = item.info->_pos;
          float z = (ctx.toCamera * itemPos).Z();
          sortItems[s]._zCoord = z;
          sortItems[s]._index = s;
        }
        QSort(sortItems,CompareInstByZ);

        // while traversing it, select LODs based on z
        for (int i=0; i<sortItems.Size(); i++)
        {
          const ItemInfoZ &item = sortItems[i];

          int src = item._index;
          float z = item._zCoord;

          const GroundClutterDesc &gItem = items[src];

          // alpha disappearing should affect lod selection
          float disappearArea = Square(z/(1-gItem.disappear));
          // if lod is "too good", we can use another one
          // TODO: incremental level here, by currentLod
          int level = ctx.lodLimit[globalType].FindLOD(disappearArea);

          int slot = startIndices[globalType]+level;

          clutters._slots[slot].Append() = gItem;
        }
      }
    }
  }

  if (cb>=0 || ctx.useLights)
  {
    float clutterDist2 = floatMax(ctx.clutterDist2,nearestDist2);
    int nClutters = cache._types.Size();
    const LightList &lights = ctx.scene.GetLightGridItem(GetLandGrid() * (xg + 0.5f), GetLandGrid() * (zg + 0.5f));
    for (int index=0; index<nClutters; index++)
    {
      int orderedIndex = cache._typeRenderOrder[index];
      const Clutter &cl = *cache._types.Get(orderedIndex);
      clutters.Draw(cb,startIndices,cl,cache._skipLods, this,orderedIndex,lights,clutterDist2,cache._windEmiterList
#if _HELISIM_RENDER_TO_TEXTURE
        , drawListId
#endif
        );
    }
  }
}

/// micro-jobs are gathered first, processed by JobManager later
/** adapted from MicroJobList in scene.cpp */
class MicroJobGrassList
{
  /// Tasks encapsulating drawing itself (recording to command buffers)
  AutoArray< SRef<IMicroJob>, MemAllocLocal<SRef<IMicroJob>, 1024> > _tasks;
  
  public:
  void Add(SRef<IMicroJob> &task)
  {
    _tasks.Add(task);
  }

  /// do all the work stored in micro-jobs (record CBs and play them)
  void Execute(int nResults)
  {
    // create command buffers
    if (Glob.config._mtSerial)
    {
      PROFILE_SCOPE_GRF_EX(recCB,*,0);
      GEngine->StartCBScope(1,nResults);
      GJobManager.ProcessMicroJobsSerial(NULL,0,_tasks);
    }
    else
    {
      PROFILE_SCOPE_GRF_EX(recCB,*,0);
      GEngine->StartCBScope(GJobManager.GetMicroJobCount(),nResults);
      GJobManager.ProcessMicroJobs(NULL,0,_tasks);
    }
    GEngine->EndCBScope();
    _tasks.Clear();
  }
};

static MicroJobGrassList GMicroJobsGrassCB;

#include <Es/Memory/normalNew.hpp>

/// grass rendering microjob
struct DrawClutter: public IMicroJob
{
  int res;
  Landscape *land;
  const ClutterStartIndices &startIndices;
  const FlexibleTerrainLoop &loop;
  GroundClutterCache &cache;
  int x,z;
  const DrawClutterHelperContext &ctx;
#if _HELISIM_RENDER_TO_TEXTURE
  int _drawObjectsId;
#endif
  
  DrawClutter(
    int res,
    Landscape *land, const ClutterStartIndices &startIndices,
    const FlexibleTerrainLoop &loop, GroundClutterCache &cache,
    int x, int z, const DrawClutterHelperContext &ctx
#if _HELISIM_RENDER_TO_TEXTURE
    , int drawObjectsId
#endif
  ):res(res),land(land),startIndices(startIndices),loop(loop),cache(cache),x(x),z(z),ctx(ctx)
#if _HELISIM_RENDER_TO_TEXTURE
  , _drawObjectsId(drawObjectsId)
#endif
  {
    PROFILE_SCOPE_DETAIL_EX(clTVB,oSort)
    land->ConvertToVBufferClutterHelper(ctx,x,z,cache);
  }
  void operator () (IMTTContext *context, int thrIndex)
  {
    PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
    #if PIX_NAMED_CB_SCOPES
      int debugId = 0x20000000+x*0x1000+z;
    #else
      int debugId = -1;
    #endif
    int cb = GEngine->StartCBRecording(res,thrIndex,debugId);

    // local clutter storage is enough for microjobs - each MJ performs separate instancing
    SlotStorage slotStorage;
    ClutterAccumulator clutters(ctx.slots,slotStorage);
    land->DrawClutterHelper(thrIndex, startIndices, clutters, loop, cache, x, z, ctx
#if _HELISIM_RENDER_TO_TEXTURE
      , _drawObjectsId
#endif
      );

    GEngine->StopCBRecording(cb,res,"grass");
  }
  
  
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(DrawClutter)

void GroundClutterCache::Flatten(Landscape *l, const Entity *entity, Vector3Val flattenPos, float maxFlattenRadius)
{
  // compute which cache grid is needed
  int xMin,xMax,zMin,zMax;

  float invTerrainGrid = l->GetInvTerrainGrid();
  float xMinF = flattenPos.X()-maxFlattenRadius;
  float xMaxF = flattenPos.X()+maxFlattenRadius;
  float zMinF = flattenPos.Z()-maxFlattenRadius;
  float zMaxF = flattenPos.Z()+maxFlattenRadius;
  xMin=toIntFloor(xMinF*invTerrainGrid);
  xMax=toIntFloor(xMaxF*invTerrainGrid);
  zMin=toIntFloor(zMinF*invTerrainGrid);
  zMax=toIntFloor(zMaxF*invTerrainGrid);
  xMax++,zMax++; // convert from inclusive to exclusive
  // traverse terrain grid
  int subdiv = l->GetSubdivLog();
  for (int zo=zMin; zo<=zMax; zo++) for (int xo=xMin; xo<=xMax; xo++)
  {
    int x = xo-(_rect.xBeg<<subdiv);
    int z = zo-(_rect.zBeg<<subdiv);
    if (x<0 || x>=_clutterData.GetXRange()) continue;
    if (z<0 || z>=_clutterData.GetYRange()) continue;
    // no need to refresh the cache - used items are actually not cached
    GroundClutterCacheItem *item = _clutterData(x,z);
    if (!item || item->Size()==0) continue;
    GroundClutterCacheItem &data = *item;
    
    for (int ii=0; ii<data.Size(); ii++)
    {
      // we have one clutter type - convert it to instances
      const Clutter *lClutter = data.GetType(ii);
      if (!lClutter->_obj) continue;

      AutoArray<GroundClutterInfo> &clutter = data[ii];

      for (int c=0; c<clutter.Size(); c++)
      {
        GroundClutterInfo &item = clutter[c];
        if (item._scale>lClutter->_noFlattenScale)
        {
          // quick test - distance
          float dist2 = item._pos.Distance2(flattenPos);
          if (dist2>Square(maxFlattenRadius))
          {
            continue;
          }

          // when fully flattened, for certain time do not test flattening that frequently - it would have no effect anyway
          if (Square(item._flattenSkewX)+Square(item._flattenSkewZ)<Square(fullFlatten) || Glob.time-item._flattenTime>startRaisingTime*0.33f)
          {
            item.Flatten(entity);
          }
        }
      } // for (c)
    }
  }
}


void Landscape::DrawGroundClutter(Scene &scene, float limitDistance, bool debugMe)
{
  PROFILE_SCOPE_GRF_EX(lDGCl,*,SCOPE_COLOR_YELLOW);
  //if (debugMe) GEngine->TrapFor0x8000("Landscape::DrawGroundClutter A");
  // during night time it may be necessary to use point and spot lights
  bool useLights = scene.NLightsAgg() > 0;

	#ifndef ENABLE_DAY_LIGHTS
		useLights = useLights && (scene.MainLight()->NightEffect() >= 0.01);
	#endif

  // Get the cache
  GroundClutterCache &cache = *_groundClutterCache;

  // If preparation failed the don't do anything
  if (!cache._prepareSucceeded) return;

  // Limit the distance clutter will be drawn to
  float effClutterDist = cache._effClutterDist;
  saturateMin(effClutterDist, limitDistance);

  // Get the camera
  const Camera &camera=*scene.GetCamera();
  Matrix4 toCamera = camera.CalcInvTransform();

  // estimate dist2 based on camera height above the ground
  float camY = camera.Position().Y()-SurfaceY(camera.Position());
  const float clutterHeight = 0.1f;
  float clutterDist2 = Square(floatMax(0,camY-clutterHeight));

  // make sure cache is up to date
  
  if (cache._clutterData.GetXRange()<=0 || cache._clutterData.GetYRange()<=0)
  {
    // if cache is empty, do nothing
    // this is needed so that we can terminate by equality in the loops below
    return;
  }

  #if 0
  if (CHECK_DIAG(DEModel))
  {
    WatchClutterX += GInput.GetCheat3ToDo(DIK_LEFT)-GInput.GetCheat3ToDo(DIK_RIGHT);
    WatchClutterZ += GInput.GetCheat3ToDo(DIK_UP)-GInput.GetCheat3ToDo(DIK_DOWN);
  }
  #endif
  // now render the data from the cache

  // double loop - traverse per LandGrid / TerrainGrid
  const int tLog = GetTerrainRangeLog()-GetLandRangeLog();
  const int t = 1<<tLog;
  
  // we should respect principal view direction, so that we traverse the grid in a sorted order
  
  bool zMajor = fabs(camera.Direction().Z())>fabs(camera.Direction().X());
  
  int zDir = camera.Direction().Z()>0 ? -1 : +1;
  int xDir = camera.Direction().X()>0 ? -1 : +1;
  

  // Calculate the rectangle limits
  int xBeg, xEnd, zBeg, zEnd;

  GridRectangle limitedBegEnd;
  scene.CalculBoundingRect(limitedBegEnd, effClutterDist, GetLandGrid());

  // limitedBegEnd subset of gcBegEnd
  Assert(limitedBegEnd.IsInside(cache._gcBegEnd));

  int xRectBegG = limitedBegEnd.xBeg-cache._gcBegEnd.xBeg;
  int zRectBegG = limitedBegEnd.zBeg-cache._gcBegEnd.zBeg;

  int xRectEndG = limitedBegEnd.xEnd+1-cache._gcBegEnd.xBeg;
  int zRectEndG = limitedBegEnd.zEnd+1-cache._gcBegEnd.zBeg;

  // calculate range in LandGrid coordinates
  int xRectBeg = xRectBegG<<tLog;
  int zRectBeg = zRectBegG<<tLog;

  int xRectEnd = xRectEndG<<tLog;
  int zRectEnd = zRectEndG<<tLog;

  Assert(cache._clutterData.GetXRange()==(cache._gcBegEnd.xEnd+1-cache._gcBegEnd.xBeg)<<tLog);
  Assert(cache._clutterData.GetYRange()==(cache._gcBegEnd.zEnd+1-cache._gcBegEnd.zBeg)<<tLog);


  // calculate beg/end points for outer loop
  xBeg = xDir>0 ? xRectBeg : xRectEnd-t; 
  xEnd = xDir>0 ? xRectEnd : xRectBeg-t;
  zBeg = zDir>0 ? zRectBeg : zRectEnd-t; 
  zEnd = zDir>0 ? zRectEnd : zRectBeg-t;

  int xStep = t*xDir;
  int zStep = t*zDir;
  
  // calculate beg/end points for inner loop
  int xTBeg = xDir>0 ? 0 : t-1;
  int xTEnd = xDir>0 ? t : -1;
  int zTBeg = zDir>0 ? 0 : t-1;
  int zTEnd = zDir>0 ? t : -1;
  
  FlexibleTerrainLoop loop;
  if (zMajor)
  {
    loop.c0InnerBeg = zTBeg, loop.c0InnerEnd = zTEnd, loop.c0InnerStep = zDir;
    loop.c1InnerBeg = xTBeg, loop.c1InnerEnd = xTEnd, loop.c1InnerStep = xDir;

    loop.c0OuterBeg = zBeg, loop.c0OuterEnd = zEnd, loop.c0OuterStep = zStep;
    loop.c1OuterBeg = xBeg, loop.c1OuterEnd = xEnd, loop.c1OuterStep = xStep;
    
    loop.mapCToX = 1;
    loop.mapCToZ = 0;
  }
  else
  {
    loop.c0InnerBeg = xTBeg, loop.c0InnerEnd = xTEnd, loop.c0InnerStep = xDir;
    loop.c1InnerBeg = zTBeg, loop.c1InnerEnd = zTEnd, loop.c1InnerStep = zDir;

    loop.c0OuterBeg = xBeg, loop.c0OuterEnd = xEnd, loop.c0OuterStep = xStep;
    loop.c1OuterBeg = zBeg, loop.c1OuterEnd = zEnd, loop.c1OuterStep = zStep;
    
    loop.mapCToX = 0;
    loop.mapCToZ = 1;
  }
  
  PROFILE_SCOPE_EX(lDGCD,land);

  LightList empty;

  // gather used clutter types?
  int nClutters = cache._types.Size();
  // TODO: consider some LOD distribution heuristics?

  SlotStorage slotStorage; 
  
  ClutterStartIndices startIndices;
  // table of LOD z limits for each clutter type
  AutoArray< LODLimit,MemAllocDataStack<LODLimit,64> > lodLimit;
  lodLimit.Resize(nClutters);
  startIndices.Resize(nClutters);

  int start =0;
  for (int i=0; i<nClutters; i++)
  {
    const Clutter *lClutter = cache._types.Get(i);
    startIndices[i] = start;
    int nLevels = lClutter->_shape ? lClutter->_shape->FindSimplestLevel()+1 : 0;
    if (nLevels>0)
    {
      nLevels -= cache._skipLods;
      if (nLevels<1) nLevels = 1;
    }
    // reserve z limits array
    lodLimit[i].Resize(nLevels);
    // advance start for the next model
    start += nLevels;
  }
  // if there are no clutter LODs, return to avoid crash
  if (start<=0) return;
  
  
#if 0 // _DEBUG || _PROFILE
  static float maxGrassDensity = 0.01f;
  static float minGrassDensity = 0.01f;
#else
  static float maxGrassDensity = 0.3f;
  static float minGrassDensity = 0.1f;
#endif
  float grassDensity = floatMinMax(GScene->GetDrawDensity(),minGrassDensity,maxGrassDensity);
  
  if (CHECK_DIAG(DEModel))
  {
    DIAG_MESSAGE(100,"Grass density %.04f",grassDensity);
  }
  ClutterAccumulator clutters(start,slotStorage);

  //if (debugMe) GEngine->TrapFor0x8000("Landscape::DrawGroundClutter B");

  for (int i=0; i<nClutters; i++)
  {
    // compute lod boundaries
    const Clutter &lClutter = *cache._types.Get(i);
    if (!lClutter._obj) continue;
    int nLevels = lClutter._shape->FindSimplestLevel()+1;
    int startLevel = 0;
    if (nLevels>0)
    {
      startLevel = cache._skipLods;
      if (startLevel >= nLevels) startLevel = nLevels - 1;
      nLevels -= cache._skipLods;
      if (nLevels < 1) nLevels = 1;
    }
    
    for (int level=0; level<nLevels; level++)
    {
      // calculate z-limit from the resolution, density and face count
      int nFaces = lClutter._shape->LevelRef(level+startLevel).NFaces();
      // the limit is when given LOD becomes inadequate

      /*
      See: LODShape::CoveredArea, LODShape::FindLevelWithDensity
      
      float areaK = camera.InvLeft()*camera.InvTop()*GEngine->Width()*GEngine->Height();
      float areaEst = lClutter._obj->EstimateArea(this,1.0f);
      float coveredArea = areaEst*areaK/dist2;
      float oComplexity = coveredArea*_drawDensity;
      */

      float areaK = camera.InvLeft()*camera.InvTop()*scene.GetEngineArea();
      float areaEst = lClutter._shape->EstimateArea(1.0f);

      // when true, lod is inadequate
      // nFaces < areaEst*areaK/dist2*_drawDensity;
      // dist2 < (_drawDensity*areaEst*areaK)/nFaces
      
      // grass "visual style" is very specific
      // most of the detail is driven by textures
      static float clutterDensityCoef = 0.01f;
      // the lod becomes inadequate once its complexity is too low
      float z2Limit = (grassDensity*areaEst*areaK)/nFaces;
      lodLimit[i][level] = z2Limit*clutterDensityCoef;
      /*
      if (level<=1) lodLimit[i][level] = FLT_MIN;
      if (level>1) lodLimit[i][level] = FLT_MAX;
      */
    }
  }
  
  #ifdef TRAP_FOR_0x8000
    if (debugMe) GEngine->TrapFor0x8000("Landscape::DrawGroundClutter C");
  #endif

  Assert(abs(loop.c0OuterBeg-loop.c0OuterEnd)/t*abs(loop.c1OuterBeg-loop.c1OuterEnd)/t==(xRectEndG-xRectBegG)*(zRectEndG-zRectBegG));
  int nResults = (xRectEndG-xRectBegG)*(zRectEndG-zRectBegG);
  
  bool grassCBs = GrassCBs && !Glob.config._mtSerial;

  if (!grassCBs) GEngine->StartCBScope(0,0),GEngine->StartCBRecording(-1,-1,0);
  
  DrawClutterHelperContext ctx(scene, camera, effClutterDist, clutterDist2, toCamera, lodLimit, useLights, start);

  // prepare microjobs
  {
    PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);
    // traverse land grid
    int res = 0;

#ifdef TRAP_FOR_0x8000
    if (debugMe) GEngine->TrapFor0x8000("Landscape::DrawGroundClutter X");
#endif

    for (int c0Outer=loop.c0OuterBeg; c0Outer!=loop.c0OuterEnd; c0Outer+=loop.c0OuterStep)
    for (int c1Outer=loop.c1OuterBeg; c1Outer!=loop.c1OuterEnd; c1Outer+=loop.c1OuterStep)
    {
      int cOuter[2] = {c0Outer,c1Outer};
      
      int x = cOuter[loop.mapCToX];
      int z = cOuter[loop.mapCToZ];
      
      if (grassCBs)
      {
        SRef<IMicroJob> task = new DrawClutter(res,this,startIndices, loop, cache, x, z, ctx
#if _HELISIM_RENDER_TO_TEXTURE
          , scene.GetDrawObjectIndex()
#endif
          );
        GMicroJobsGrassCB.Add(task);
      }
      else
      {
        { // simulate DrawClutter constructor
          PROFILE_SCOPE_DETAIL_EX(clTVB,oSort)
          ConvertToVBufferClutterHelper(ctx,x,z,cache);
        }
        DrawClutterHelper(-1,startIndices,clutters,loop,cache,x,z,ctx
#if _HELISIM_RENDER_TO_TEXTURE
          , scene.GetDrawObjectIndex()
#endif
          );
      }
      
      
      res++;
    }

#ifdef TRAP_FOR_0x8000
    //if (debugMe) GEngine->TrapFor0x8000("Landscape::DrawGroundClutter Y");
#endif

    Assert(res==nResults);
  }
  if (grassCBs)
  {
    // execute microjobs
    GMicroJobsGrassCB.Execute(nResults);
  }
  else  
  {
    // Draw the rest clutters
    int cb = -1;
    for (int index=0; index<nClutters; index++ )
    {
      int orderedIndex = cache._typeRenderOrder[index];
      const Clutter &cl = *cache._types.Get(orderedIndex);
      clutters.Draw(cb,startIndices,cl,cache._skipLods,this,orderedIndex,empty,clutterDist2,cache._windEmiterList
#if _HELISIM_RENDER_TO_TEXTURE
       , scene.GetDrawObjectIndex()
#endif
        );
    }
    GEngine->StopCBRecording(-1,-1,"grass");
    GEngine->EndCBScope();
  }

#ifdef TRAP_FOR_0x8000
  if (debugMe) GEngine->TrapFor0x8000("Landscape::DrawGroundClutter D");
#endif
    
  #if _ENABLE_CHEATS
  
    //if (CHECK_DIAG(DEForce))
    if (false)
    {
      Vector3 camPos = camera.Position();

      const int maxInstances = 256;
      AutoArray< ObjectInstanceInfo,MemAllocDataStack<ObjectInstanceInfo,maxInstances> > instances;
      // grass approximation diagnostics
      // scan terrain around the camera

      Ref<Object> obj=new ObjectColored(scene.Preloaded(SphereModel),VISITOR_NO_ID);

      { // grass layer diagnostics

        Matrix3 scale(MScale,0.1f);

        DrawParameters dp;

        int x = toInt(camPos.X()*GetInvTerrainGrid());
        int z = toInt(camPos.Z()*GetInvTerrainGrid());
        int range = 20;
        for (int xx=x-range; xx<=x+range; xx++)
        for (int zz=z-range; zz<=z+range; zz++)
        {
          float grass = _grassApprox(xx,zz)*GrassApproxScale;
          float posY = ClippedDataXZ(xx,zz)+grass;

          Vector3 pos(xx*GetTerrainGrid(),posY,zz*GetTerrainGrid());
          
          if (camera.IsClipped(pos,1,NULL))
          {
            continue;
          }

          ObjectInstanceInfo &info = instances.Append();
          info._origin.SetOrientation(scale);
          info._origin.SetPosition(pos);
          info._color = HWhite;
          info._shadowCoef = 1;
          info._parentObject = NULL;
          info._object = NULL;

          if (instances.Size()>=maxInstances)
          {
            obj->DrawSimpleInstanced(
              -1,0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
              , scene.GetDrawObjectIndex()
#endif
            );
            instances.Clear();

          }
        }


        obj->DrawSimpleInstanced(
          -1,0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
          , scene.GetDrawObjectIndex()
#endif
        );
        instances.Clear();
      }

      { // grass diagnostics

        Matrix3 scale(MScale,0.02f);

        DrawParameters dp;
        float invClutterGrid = 1.0f/_clutterGrid;
        int x = toInt(camPos.X()*invClutterGrid);
        int z = toInt(camPos.Z()*invClutterGrid);
        int range = 20;

        AUTO_STATIC_ARRAY(InitPtr<Object>,roadways,32);
        int xr = toInt(camPos.X()*GetInvLandGrid());
        int zr = toInt(camPos.Z()*GetInvLandGrid());
        GetRoadList(xr, zr, roadways);

        for (int zz=z-range; zz<=z+range; zz++) for (int xx=x-range; xx<=x+range; xx++)
        {
          float posX = xx*_clutterGrid;
          float posZ = zz*_clutterGrid;
          float grass = GetGrassHeight(posX,posZ,roadways);
          float posY = SurfaceY(posX,posZ);

          Vector3 pos(posX,posY+grass,posZ);

          if (camera.IsClipped(pos,1,NULL))
          {
            continue;
          }

          ObjectInstanceInfo &info = instances.Append();
          info._origin.SetOrientation(scale);
          info._origin.SetPosition(pos);
          info._color = HWhite;
          info._shadowCoef = 1;
          info._parentObject = NULL;
          info._object = NULL;

          if (instances.Size()>=maxInstances)
          {
            obj->DrawSimpleInstanced(
              -1, 0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
              , scene.GetDrawObjectIndex()
#endif
              );
            instances.Clear();

          }
        }


        obj->DrawSimpleInstanced(
          -1, 0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
          , scene.GetDrawObjectIndex()
#endif
          );
        instances.Clear();
      }


      { // grass diagnostics

        Matrix3 scale(MScale,0.04f);

        DrawParameters dp;
        float invClutterGrid = 1.0f/_clutterGrid;
        int x = toInt(camPos.X()*invClutterGrid);
        int z = toInt(camPos.Z()*invClutterGrid);
        int range = 20;

        AUTO_STATIC_ARRAY(InitPtr<Object>,roadways,32);
        int xr = toInt(camPos.X()*GetInvLandGrid());
        int zr = toInt(camPos.Z()*GetInvLandGrid());
        GetRoadList(xr, zr, roadways);

        for (int zz=z-range; zz<=z+range; zz++) for (int xx=x-range; xx<=x+range; xx++)
        {
          float posX = xx*_clutterGrid;
          float posZ = zz*_clutterGrid;
          float grass = GrassHeight(posX,posZ);
          float posY = SurfaceY(posX,posZ);

          Vector3 pos(posX,posY+grass,posZ);

          if (camera.IsClipped(pos,1,NULL))
          {
            continue;
          }

          ObjectInstanceInfo &info = instances.Append();
          info._origin.SetOrientation(scale);
          info._origin.SetPosition(pos);
          info._color = HWhite;
          info._shadowCoef = 1;
          info._parentObject = NULL;
          info._object = NULL;

          if (instances.Size()>=maxInstances)
          {
            obj->DrawSimpleInstanced(
              -1, 0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
              , scene.GetDrawObjectIndex()
#endif
              );
            instances.Clear();

          }
        }


        obj->DrawSimpleInstanced(
          -1, 0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
          , scene.GetDrawObjectIndex()
#endif
          );
        instances.Clear();
      }

      { // water parametric surface diagnostics
        Matrix3 scale(MScale,0.1f);
        DrawParameters dp;

        for (int z=-30; z<=+30; z+=3) for (int x=-30; x<=+30; x+=3)
        {
          float posX = camPos.X()+x;
          float posZ = camPos.Z()+z;
          float posY = GetSeaLevel(posX,posZ);
          
          Vector3 pos(posX,posY,posZ);
          
          if (camera.IsClipped(pos,1,NULL))
          {
            continue;
          }

          ObjectInstanceInfo &info = instances.Append();
          info._origin.SetOrientation(scale);
          info._origin.SetPosition(pos);
          info._color = HWhite;
          info._shadowCoef = 1;
          info._parentObject = NULL;
          info._object = NULL;
        }

        obj->DrawSimpleInstanced(
          -1, 0, ClipAll, dp, instances.Data(), instances.Size(), 0, empty, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
          , scene.GetDrawObjectIndex()
#endif
        );
        instances.Clear();
      }
    }
  #endif
}

/**
@param x terrainGrid coordinate of the vertex
@param z terrainGrid coordinate of the vertex
@param xOffset offset (distance) from the vertex
@param zOffset offset (distance) from the vertex
@param roadways list of roadway objects nearby (used for grass exclusion)
*/
float Landscape::GetGrassHeight(float x, float z, StaticArrayAuto< InitPtr<Object> > &roadways) const
{
  const float coef = 1.0f / _clutterGrid;
  const int xxx = toInt(x*coef);
  const int zzz = toInt(z*coef);

  // y should not matter here
  Vector3 pos(xxx*_clutterGrid,0,zzz*_clutterGrid);

  //int subdiv = GetSubdiv();
  //int subdivLog = GetSubdivLog();
  int gx = toIntFloor(pos.X()*GetInvLandGrid());
  int gz = toIntFloor(pos.Z()*GetInvLandGrid());
  const Scenery::TextureInfo &texInfo = ClippedTextureInfo(gz,gx);
  // check layer mask (bimpas, any pass should do, the mask is always the same
  // any material will do - mask is always the same
  const TexMaterial *gMaterial = texInfo._mat;

  if (!gMaterial) return 0;
  Ref<LayerMaskCacheItem> layerMaskItem = _layerMaskCache->Item(gMaterial);
  if (!layerMaskItem || _clutter.Size()<=0) return 0;
  // check if some clutter is high enough
  // if not, ignore it

  DoAssert(layerMaskItem->Loaded());

  // TODO: prepare simple mapping from world space into a texture space in bimpas
  // we need to find any world space texgen
  int world = -1;
  for (int i=0; i<gMaterial->_nTexGen; i++)
  {
    if (gMaterial->_texGen[i]._uvSource==UVWorldPos)
    {
      world = i;
      break;
    }
  }
  Assert(world>=0);
  if (world<0) return 0;
  
  // material contains mapping from world space into a texture space
  Vector3 uv = gMaterial->_texGen[world]._uvTransform.FastTransform(pos);
  const SurfaceCharacter &character = layerMaskItem->GetSurfaceInfo(gMaterial,uv.X(),uv.Y())._character;
  // check which layer is prevalent

  int seedXZ=_randGen.GetSeed(xxx,zzz);

  // we should check the position map as well

  float isHereF = _randGen.RandomValue(seedXZ);
  int isHere = -1;
  for (int i=0; i<character.Size(); i++)
  {
    float prop = character.Get(i)._probabilityThold;
    if (prop>=isHereF)
    {
      isHere = character.Get(i)._landIndex;
      Assert(isHere>=0 && isHere<=_clutter.Size());
      break;
    }
  }
  if (isHere<0) return 0;

  if (CheckRoadway(roadways,pos)) return 0;

  const Clutter &clutter = _clutter[isHere];
  LODShape *shape = clutter._shape;
  if (!shape) return 0;

  // check model height

  float scaleMin = clutter._scaleMin;
  float scaleMax = clutter._scaleMax;
  float scale = scaleMin + (scaleMax - scaleMin) * _randGen.RandomValue(seedXZ+4);

  float grass = (shape->BoundingCenter().Y()+shape->Max().Y())*scale;
  float alpha = shape->Color().A8()*(1.0f/255);

  // we multiply by a factor, as we expect not a whole size is really used
  const float grassMul = InterpolativC(alpha,0.1f,0.8f,0.6f,0.9f);
  return floatMax(grass*grassMul,0);
}

static void ClearClutterMapping(const SurfaceInfo * info)
{
  for (int i=0; i<info->_character.Size(); i++)
  {
    info->_character.Get(i)._landIndex = -1;
  }
}

static void BuildClutterMapping(Landscape *land, const SurfaceInfo * info)
{
  if (info->_character.IsEmpty()) return;
  for (int i=0; i<info->_character.Size(); i++)
  {
    RStringB name = info->_character.Get(i)._name;
    if (name.GetLength()<=0) continue;
    int clutter = land->FindClutter(name);
    if (clutter<0) continue;
    info->_character[i]._landIndex = clutter;
  }
}

static GroundClutterPosMapWithTex *BuildClutterPosMap(Landscape *land, const TexMaterial *mat, int pass)
{
  Texture *det = mat->_stage[LandMatStageLayers+pass*LandMatStagePerLayer+LandMatDtInStage]._tex;
  if (!det) return NULL;
  const SurfaceInfo &info = det->GetSurfaceInfo();
  if (info._character.IsEmpty()) return NULL;
  bool someClutter = false;
  int clutterHash = 0;
  // we can ignore clutter layers which are low
  float maxHeight = 0;
  for (int i=0; i<info._character.Size(); i++)
  {
    RStringB name = info._character.Get(i)._name;
    if (name.GetLength()<=0) continue;
    int clutter = land->FindClutter(name);
    if (clutter<0) continue;
    // for purposes of grass rendering we ignore low clutter
    LODShape *shape = land->GetClutter(clutter)._shape;
    if (!shape) continue;
    someClutter = true;
    // 33 should do - we do not mind if there are some conflicts
    clutterHash = clutterHash*33 + clutter;
    float height = shape->Max().Y()-shape->Min().Y();
    if (maxHeight<height) maxHeight = height;
  }
  // one position mapping for each SurfaceInfo
  // no need to create a mapping for clutters which are not used
  if (someClutter)
  {
    // even when maxHeight<ClutterHeightIgnored, we still need the PosMap to be created
    // it is used to display the clutter, not only the clutter layer
    // clutter grid is constant across the landscape
    float nClutters = land->GetClutterPerLandGrid();
    // we need a "layer" seed, which can be any combination of clutter types
    // we need a color not of primary texture, rather of a close detail texture
    return new GroundClutterPosMapWithTex(
      nClutters,nClutters,clutterHash,land->GetRandGen(),
      info._character,maxHeight>ClutterHeightIgnored
    );
  }
  return NULL;
}

static bool TexMaterialClearClutterMapping(
  TexMaterial *mat, const TexMaterialBank::ContainerType *bank
)
{
  const SurfaceInfo *info = mat->GetSurfaceInfo();
  if (info)
  {
    ClearClutterMapping(info);
  }
  mat->SetCustomData(NULL);
  return false;
}


struct TexMaterialBuildClutterMapping
{
  Landscape *_land;
  
  explicit TexMaterialBuildClutterMapping(Landscape *land):_land(land) {}
  bool operator () (
    const TexMaterial *mat, const TexMaterialBank::ContainerType *bank
  ) const
  {
    const SurfaceInfo *info = mat->GetSurfaceInfo();
    if (info)
    {
      BuildClutterMapping(_land,info);
    }
    return false;
  }

};

void Landscape::ClearClutterMapping()
{
  // we need to flush the posMap cache first
  _groundClutterCache->_posMap.FreeAll();
  GTexMaterialBank.ForEach(TexMaterialClearClutterMapping);
  AbstractTextBank *bank = GEngine->TextBank();
  for (int s=0; s<bank->NSurfaceInfos(); s++)
  {
    const SurfaceInfo *info = bank->GetSurfaceInfo(s);
    if (info)
    {
      ::ClearClutterMapping(info);
    }
  }
}
void Landscape::BuildClutterMapping()
{
  // TODO: create a bank of SurfaceInfo-s
  GTexMaterialBank.ForEach(TexMaterialBuildClutterMapping(this));
  AbstractTextBank *bank = GEngine->TextBank();
  for (int s=0; s<bank->NSurfaceInfos(); s++)
  {
    const SurfaceInfo *info = bank->GetSurfaceInfo(s);
    if (info)
    {
      ::BuildClutterMapping(this,info);
    }
  }
//   int startMem = MemoryCommited();
//   int totalPasses = 0;
//   int builtPasses = 0;
  // scan all landscape material, for each build a "PosMap"
  for (int i=0; i<_terrain->_texture.Size(); i++)
  {
    TexMaterial *mat = _terrain->_texture[i]._mat;
    // for each pass-stage check if PosMap is already built
    //if (!mat || mat->GetPixelShaderID()<PSTerrain1 || mat->GetPixelShaderID()>PSTerrain15)
    if (!mat || (mat->GetPixelShaderID()!=PSTerrainX && mat->GetPixelShaderID()!=PSTerrainSNX))
    {
      continue;
    }
    // build each custom data only once
    if (mat->GetCustomData()) continue;
    GroundClutterPosMaps *maps = new GroundClutterPosMaps;
    int nPasses = (mat->_stageCount+1-LandMatStageLayers)/LandMatStagePerLayer;
    for (int p=0; p<nPasses; p++)
    {
      //totalPasses++;
      // color of the detail texture needs to be stored in the custom data of the detail texture
      // detail texture also contains the SurfaceInfo
      maps->_maps[p] = ::BuildClutterPosMap(this,mat,p);
      if (maps->_maps[p]) maps->_maps[p]->InitTexture();
//       if (maps->_maps[p])
//       {
//         builtPasses++;
//       }
    }
    // TODO: build one common texture only, used for all layers at once
    mat->SetCustomData(maps);
  }
//   if (builtPasses>0)
//   {
//     int memUsed = MemoryCommited()-startMem;
//     LogF(
//       "%s: Clutter mem: total %d, built %d, avg mem %d, total mem %d, nClutters %d, clutter grid %g",
//       cc_cast(GetName()),
//       totalPasses,builtPasses,memUsed/builtPasses,memUsed,GetClutterPerLandGrid(),_clutterGrid
//     );
//   
//   }
}


void Landscape::DrawRainClutter(Scene &scene)
{
  if (!_rainClutter || _rainClutter->NLevels()<=0) return;

  // Raindrop default transparency (final transparency is also affected by distance and weather)
  const float densityFactor = 0.8f;

  float rainDensity = GetRainDensity() * densityFactor;
  if (rainDensity<0.05f) return;
  PROFILE_SCOPE_GRF_EX(lDRnC,land,SCOPE_COLOR_BLUE);
  // generate random raindrops around the player
  const int nClutterObj = 512;
  AUTO_STATIC_ARRAY_16(ObjectInstanceInfo,clutter,nClutterObj);

  const Camera &camera=*scene.GetCamera();

  const float clutterGrid = 1;
  const float clutterDist = 10;
  GridRectangle rainBegEnd;
  scene.CalculBoundingRect(rainBegEnd, clutterDist, clutterGrid);
  for (int x=rainBegEnd.xBeg; x<=rainBegEnd.xEnd; x++)
  for (int z=rainBegEnd.zBeg; z<=rainBegEnd.zEnd; z++)
  {
    // Test the terrain grid item distance
    float xPos = clutterGrid * (x + 0.5f);
    float zPos = clutterGrid * (z + 0.5f);
    Vector3 terrainItemPos(xPos, 0.0f, zPos);
    if (terrainItemPos.DistanceXZ2(camera.Position()) > Square(clutterDist+clutterGrid)) continue;

    int seedXZ=_randGen.GetSeed(x,z);
    
    float timeBase = _randGen.RandomValue(seedXZ+3)*10;
    float time = Glob.time.toFloat()-timeBase;
    // inverse of period/duration
    float speed = _randGen.RandomValue(seedXZ+5)*5+3;
    float cycleCont = time*speed;
    int cycle = toLargeInt(cycleCont-0.5f);

    // check where should we place the raindrop
    seedXZ=_randGen.GetSeed(x,z,cycle);

    float xOffset = _randGen.RandomValue(seedXZ+1)*clutterGrid-(clutterGrid*0.5);
    float zOffset = _randGen.RandomValue(seedXZ+2)*clutterGrid-(clutterGrid*0.5);
    
    xPos += xOffset;
    zPos += zOffset;
    
    float dX,dZ;
    const float yEpsilon = 0.001f;
    float yPos = SurfaceYAboveWater(xPos,zPos,&dX,&dZ)+yEpsilon;
    Vector3 pos(xPos,yPos,zPos);

    float dist2 = camera.Position().Distance2(pos);
    if (dist2>=Square(clutterDist)) continue;
    
    float scale = _randGen.RandomValue(seedXZ+4)*0.5f+0.5f;

    float disappear = sqrt(dist2)/clutterDist*2-1;
    saturate(disappear,0,1);
    
    float phase = cycleCont-cycle;
    
    float alpha = 1-fabs(1-2*phase);
    
    alpha *= (1-disappear)*rainDensity;
    if (alpha<0.01f) continue;
    
    ObjectInstanceInfo &info = clutter.Append();
    
    // Set instance parameters  
    Vector3 groundNormal(-dX,1,-dZ);
    Matrix3 alignGround(MUpAndDirection,groundNormal,VForward);
    info._origin.SetPosition(pos);
    info._origin.SetOrientation(alignGround*scale*3);
    info._color = Color(1,1,1,alpha);
    info._shadowCoef = scene.GetLandShadowSmooth(info._origin.Position());
    info._object = info._parentObject = NULL;
  }

  // Draw the rest clutters
  const LightList &lights = scene.GetLightGridItem(camera.Position().X(), camera.Position().Z());

  LODShape *shape = _rainClutter;
  ShapeUsed level0 = shape->Level(0);

  level0->ConvertToVBuffer(VBStatic, shape, 0);

  Object *object = _rainClutterObj;

  DrawParameters dp;

  // assume rain is always very near
  object->DrawSimpleInstanced(
    -1, 0, ClipAll, dp, clutter.Data(), clutter.Size(), 0, lights, NULL,FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
    , scene.GetDrawObjectIndex()
#endif
  );
}

void Landscape::ClearGroundClutterCache()
{
  if (_layerMaskCache) _layerMaskCache->Clear();
  if (_groundClutterCache) _groundClutterCache->Clear();
}

void Landscape::FlushGroundClutterSegCache()
{
  _groundClutterCache->Clear();
}

void Landscape::InitGroundClutterSegCache()
{
  _layerMaskCache->Init(this);
}

void Landscape::CreateGroundClutterCache()
{
  _layerMaskCache = new LayerMaskCache;
  _groundClutterCache = new GroundClutterCache;
}

#include <Es/Memory/normalNew.hpp>

/// similar to ObjectPlain, but faster
class GroundClutter: public Object
{
  typedef Object base;
  Color _constantColor;

  public:
  GroundClutter(LODShape *shape, const CreateObjectId &id);
  void SetConstantColor( ColorVal color ) {_constantColor=color;}
  ColorVal GetConstantColor() const {return _constantColor;}
  int GetObjSpecial(int shapeSpecial) const {return shapeSpecial|IsColored;}
  int GetSpecial() const {return base::GetSpecial()|IsColored;}

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(GroundClutter)

GroundClutter::GroundClutter(LODShape *shape, const CreateObjectId &id)
:base(shape,id),_constantColor(HWhite)
{
}


void Landscape::InitClutter(ParamEntryPar cls)
{
  ParamEntryVal clutter = cls>>"Clutter";
  int nClutter = clutter.GetEntryCount();

  const int maxClutter = 127;
  if (nClutter>maxClutter)
  {
    RptF("Too many clutter types present: max %d allowed, %d present",maxClutter,nClutter);
    nClutter = maxClutter;
  }
  _startDisappearDistGMin = 100;
  _endDisappearDistGMax = 200;
  _clutter.Clear();
  _someClutterAlpha = false;
  for (int i=0; i<nClutter; i++)
  {
    Clutter &ci = _clutter.Append();

    ParamEntryVal item = clutter.GetEntry(i);

    RString itemName = item.GetName();
    itemName.Lower();
    ci._name = itemName;

    RStringB name = item>>"model";

    ci._shape = name.GetLength()>0 ? Shapes.New(name,false,false) : NULL;
    // if there are no LODs, ignore the clutter
    if (ci._shape && ci._shape->NLevels()<=0) ci._shape = NULL;
    if (ci._shape)
    {
      ci._obj = new GroundClutter(ci._shape,VISITOR_NO_ID);
    }
    // TODO: rename config
    bool swLighting = item>>"swLighting";

    // initialize material somehow
    if (ci._shape)
    {
      // Try to guess if the model is a grass or a stone, set the LODShape thermal properties accordingly
      bool isGrass = false;

      ci._shape->DisableStreaming();
      ci._shape->OrSpecial(IsAlphaFog);
      if (ci._shape->NLevels()>0)
      {
        ShapeUsed lock = ci._shape->Level(0);
        Assert(lock.NotNull());
        Shape *shape = lock.InitShape();
        //DoAssert(shape->NSections()==1);
        for (int s=0; s<shape->NSections(); s++)
        {
          ShapeSection &sec = shape->GetSection(s);
          TexMaterial *mat = sec.GetMaterialExt();
          if (mat)
          {
            // Make sure the material uses sun
            if (swLighting)
            {
              // In case of SW lighting modify the clutter material
              // Note: after swLighting flag is removed from data and after after materials are set properly
              // (with forced diffuse), this branch can be removed)
              mat->SetMainLight(ML_Sun);
              if (mat->GetVertexShaderID()==VSBasic && mat->GetPixelShaderID()==PSGrass)
              {
                mat->SetVertexShaderID(VSGrass);
              }

              // All the diffuse is moved to forcedDiffuse
              mat->_forcedDiffuse = mat->_diffuse + mat->_forcedDiffuse;
              mat->_diffuse = HBlack;
            }
            // Update the isGrass flag
            if (mat->GetPixelShaderID()==PSGrass) isGrass = true;
          }
        }
      }
      // Set the shape thermal imaging parameters according to grass/stone decision
      static float HTGrass = 60.0f * 60.0f * 0.1f;
      static float HTStone = 60.0f * 60.0f * 25.0f;

      if (isGrass)
      {
        ci._shape->SetTIProperties(HTGrass, HTGrass, 0, 0, 0, 0);
      }
      else
      {
        ci._shape->SetTIProperties(HTStone, HTStone, 0, 0, 0, 0);
      }
    }

    //if not shape use constant, non zero because of division
    float shapeHeight = ci._shape ? shapeHeight = ci._shape->BoundingCenter().Y()+ci._shape->Max().Y() : 0.01f;
    float invShapeHeight = 1.0f/shapeHeight;
    
    ci._scaleMin = item >> "scaleMin";
    ci._scaleMax = item >> "scaleMax";
    // depending on size the clutter (20-100 cm) should disappearing farther from camera
    ci._startDisappearDist = Interpolativ(shapeHeight,0.20f,1.00f,20.0f,100.0f);
    ci._endDisappearDist   = Interpolativ(shapeHeight,0.20f,1.00f,50.0f,400.0f);
    
    _endDisappearDistGMax = floatMax(ci._endDisappearDist, _endDisappearDistGMax);
    _startDisappearDistGMin = floatMin(ci._startDisappearDist, _startDisappearDistGMin);

    // small clutter even sooner
    float windAffection = item>>"affectedByWind";

    if (windAffection<=0)
    {
      ci._noFlattenScale = ci._fullFlattenScale = FLT_MAX;
      ci._noWindAffectionScale = ci._fullWindAffectionScale = FLT_MAX;
    }
    else
    {

      float noFlattenHeight = 0.20f;
      float fullFlattenHeight = 0.60f;


      ci._noFlattenScale = noFlattenHeight*invShapeHeight;
      ci._fullFlattenScale = fullFlattenHeight*invShapeHeight;

      // by default we want no wind under 20 cm, or the average clutter height, whatever is smaller
      //float noWindHeight = floatMin(0.2f,shapeHeight);
      //float noWindScale = noWindHeight*invShapeHeight;
      const float noWindHeight = 0.35f;
      ci._noWindAffectionScale = floatMin(noWindHeight*invShapeHeight,0.9f);

      float minHeight = ci._scaleMin*shapeHeight;
      float maxHeight = ci._scaleMax*shapeHeight;
      (void)minHeight,(void)maxHeight;
      // on scale 1 we should be at given windAffection
      //(scale-ci._noWindAffectionScale)/(ci._fullWindAffectionScale-ci._noWindAffectionScale) = windAffection
      //(1.0-ci._noWindAffectionScale) = windAffection * (ci._fullWindAffectionScale-ci._noWindAffectionScale)
      //(1.0-ci._noWindAffectionScale + windAffection*ci._noWindAffectionScale)/windAffection= ci._fullWindAffectionScale
      if (ci._noWindAffectionScale<1)
      {
        ci._fullWindAffectionScale = (1.0-ci._noWindAffectionScale)/windAffection + ci._noWindAffectionScale;
        float windAffectionVerify = (1.0-ci._noWindAffectionScale)/(ci._fullWindAffectionScale-ci._noWindAffectionScale);
        (void)windAffectionVerify; Assert(fabs(windAffection-windAffectionVerify)<1e-2f);
      }
      else
      {
        // provide reasonable defaults when _noWindAffectionScale is 1 or greater
        ci._fullWindAffectionScale = ci._noWindAffectionScale*1.5f;
      }
    }
    if (item.FindEntry("colorByGround"))
    {
      float colorByGround = item>>"colorByGround";
      if (colorByGround>0)
      {
        RptF("colorByGround no longer supported (used in %s)",cc_cast(item.GetContext()));
      }
    }
    if (item.FindEntry("surfaceColor"))
    {
      ci._isColored = GetValue(ci._surfColor,item>>"surfaceColor");
    }
    else
    {
      ci._isColored = false;
    }
    // if some AlphaTest RenderFlag is set, it is not an Alpha clutter
    ci._isAlpha = false;
    if (ci._shape && ci._shape->NLevels()>0)
    {
      bool someAlpha = false;
      ShapeUsed shape0 = ci._shape->Level(0);
      for (int i=0; i<shape0->NSections(); i++)
      {
        const ShapeSection &sec = shape0->GetSection(i);
        TexMaterial *mat = sec.GetMaterialExt();
        if (sec.Special()&IsAlpha)
        {
          if (mat)
          {
            TexMaterial::Loaded loaded(mat);
            if (
              !loaded.GetRenderFlag(RFAlphaTest128) &&
              !loaded.GetRenderFlag(RFAlphaTest64) &&
              !loaded.GetRenderFlag(RFAlphaTest32)
              )
            {
              someAlpha = true;
            }
          }
          else
          {
            someAlpha = true;
          }
        }
      }
      if (!someAlpha)
      {
        // hot-fix - clutter missing material is some LODs
        bool hotfixed = false;
        for (int l=1; l<=ci._shape->FindSimplestLevel(); l++)
        {
          // TODO: check if modification of shape is legal
          ShapeUsed lock = ci._shape->Level(l);
          Assert(lock.NotNull());
          Shape *shape = lock.InitShape();

          for (int i=0; i<shape->NSections(); i++)
          {
            const ShapeSection &sec = shape->GetSection(i);
            TexMaterial *mat = sec.GetMaterialExt();
            if (!mat && (sec.Special()&IsAlpha))
            {
              if (shape0->NSections()!=shape->NSections())
              {
                RptF("Cannot hotfix alpha clutter %s",cc_cast(ci._shape->GetName()));
              }
              else
              {
                shape->GetSection(i).SetMaterialExt(shape0->GetSection(i).GetMaterialExt());
                hotfixed = true;
              }
            }
          }
        }
        if (hotfixed)
        {
          RptF("Clutter %s uses alpha blending in some LOD - use renderflag AlphaTest instead",cc_cast(ci._shape->GetName()));
        }
      }
      else
      {
        RptF("Error: Clutter %s uses alpha blending - use renderflag AlphaTest instead",cc_cast(ci._shape->GetName()));
      }
      // clutter rendering no longer supports alpha anyway, no use to flag it as such
      ci._isAlpha = false;
    }
  }

  RStringB raindropName = cls>>"Rain">>"raindrop";
  _rainClutter = Shapes.New(GetShapeName(raindropName),false,false);
  if (_rainClutter)
  {
    _rainClutter->DisableStreaming();
    _rainClutter->OrSpecial(IsAlpha|IsAlphaFog|NoZWrite|IsOnSurface);
    _rainClutterObj = new GroundClutter(_rainClutter,VISITOR_NO_ID);
  }
  
}

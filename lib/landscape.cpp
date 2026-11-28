//# pragma optimize( "", off ) 

/*!
\file
Landscape drawing
*/
#include "wpch.hpp"
#include "landscape.hpp"
#include "landImpl.hpp"
#include "editor.hpp"
#include "lights.hpp"
#include "camera.hpp"
#include "tlVertex.hpp"
#include "world.hpp"
#include "engine.hpp"
#include "fileLocator.hpp"
#include "global.hpp"
#include "AI/operMap.hpp"
#include "Shape/specLods.hpp"
#include "diagModes.hpp"
#include "paramFileExt.hpp"
#include "Shape/material.hpp"
#include "keyInput.hpp"
#include "txtPreload.hpp"
#include "roads.hpp"
#include "dikCodes.h"
#include "Network/network.hpp"
//#include "objectClasses.hpp"
#include <Es/Types/scopeLock.hpp>
#include <El/Common/randomGen.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/Strings/bString.hpp>
#include <Es/Algorithms/qsort.hpp>
#include <El/FreeOnDemand/memFreeReq.hpp>
#include <El/Evaluator/express.hpp>
#include "TerSynth/terrain.h"

#if _VBS3
#include <Es/Common/win.h>
#endif

#include "progress.hpp"
#include "depMake.hpp"
#include "gridEventLogger.hpp"

#include "Physics\enf_worldphysics.h"

#if _ARMA3_SIMUL_WEATHER
#include "SimulWeather\SimulWeatherBase.hpp"
#endif
extern bool NoLandscape;

#if _DEBUG || _PROFILE
  #define LOG_SEG 0
#endif

#if _DEBUG
  //#define INTERESTED_IN_ID 15041
#endif

extern bool LandEditor;
void MemoryCleanUp();


#if 0 // _PROFILE || _DEBUG

static EventLogger GEventLogger;

#else

static DummyEventLogger GEventLogger;

#endif



class LandSegmentBase: public RefCount,public TLinkBidirRef
{
  friend class Landscape;
  friend class LandCache;

  protected:

  GridRectangle _rect;
  bool _valid;
  //! Vertices of the segment are not using world space to keep the model coordinates low. Therefore
  //! we need the segment offset to represent the shift of the segment in the world.
  Vector3 _offset;

  public:
  LandSegmentBase();
  ~LandSegmentBase();
  const GridRectangle &GetRect() const {return _rect;}
  bool ValidFor( const GridRectangle &rect ) const;
  bool VerifyStructure() const;
  void Clear();
  Vector3Val Offset() const {return _offset;}
};

/// information about Area Over Texture area for a single texture
struct TexAOTInfo
{
  Texture *_tex;
  float _aot;
};

template <>
struct FindArrayKeyTraits<TexAOTInfo>
{
  typedef Texture *KeyType;
  typedef TexAOTInfo Type;
  
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  /// get a key from an item
  static KeyType GetKey(const Type &a) {return a._tex;}
};

TypeIsSimple(TexAOTInfo)

/// keep information about ground in given segment
/** maintain cache of smaller rectangles (8x8)
this will reduce DIP numbers, improve texture consistency, vertex data locality
and caching efficiency
*/

class LandSegment: public LandSegmentBase
{
public:
  /// number of LOD levels used for terrain mesh
  /**
  Due to shader limitations we currently cannot support more than 4
  Moreover, we want one slot in the vector to be available for grass layer
  */
#if _VBS3_MORETERRAINLODS
  enum {NLods=VertexCustomItems};  // currently 7, see vertex.hpp
#else
  enum {NLods=3};
#endif
  
  /// there can be multiple passes on one grid -track them all together
  struct TexPassInfo
  {
    signed char _tex;
    bool operator==(const TexPassInfo &i) const { return _tex==i._tex; }

    explicit TexPassInfo(int tex)
    :_tex(tex){Assert(tex<=SCHAR_MAX);}
    
    TexPassInfo(){}
    
    ClassIsSimple(TexPassInfo)
  };
  struct TexMPassInfo
  {
    FindArrayKey<TexPassInfo, FindArrayKeyTraits<TexPassInfo>, MemAllocLocal<TexPassInfo,16> > _pass;
    // ^^^ wasted a lot of memory, _pass is often short of not present at all
    //FindArrayKey<TexPassInfo, FindArrayKeyTraits<TexPassInfo> > _pass;
    
    ClassIsMovableZeroed(TexMPassInfo)
  };
  
  friend class Landscape;
  friend class LandCache;
  
  typedef LandSegmentBase base;

  /// list of textures and materials used in the segment
  struct TexMatIndex
  {
    /// list of textures used in the segment
    FindArrayKey<TexAOTInfo> _textures;
    /// mapping from xz to _textures and _materials
    Array2D<TexMPassInfo> _grid;

    void Initialize(const GridRectangle &rect)
    {
      _textures.Clear();
      _grid.Dim(rect.xEnd-rect.xBeg,rect.zEnd-rect.zBeg);
    }

    int AddInfo(Texture *tex, float aot);
    
    void Clear()
    {
      _grid.Clear();
      _textures.Clear();
    }
  };
  
private:
  //! Structure to hold parameters for single LODs
  struct SegLod
  {
    //! Terrain geometry (including simple version, seams, shore)
    Shape _table;
    //! Structure to hold start section index and sections count for specified geometry
    struct SectionInterval
    {
      //! First item
      int _beg;
      //! Not inclusive end - after last item
      int _end;
      //! Constructor to initialize content with undefined values
      SectionInterval() {_beg = -1; _end = -1;}
    } _geometry[GeometryTypeCount];
    //! Roads geometry
    Shape _roads;
    //! Was _roads already created?
    /*!
      Used to distinguish non-created and empty roads
    */
    bool _roadsDone;
  } _lod[NLods];
  /// index for _table
  TexMatIndex _index;
  /// index for _roads
  TexMatIndex _indexRoads;
  /// check if geometry or vertex buffer of this segment is still loaded
public:
  LandSegment();
  ~LandSegment();
  
  void Initialize(const GridRectangle &rect)
  {
    _index.Initialize(rect);
    //_indexRoads.Initialize(rect);
  }
  void BuildTexMatIndex(TexMatIndex &index, Landscape *l);
  void UpdateTexMatIndex(TexMatIndex &index, int xs,int zs,LODShape *lshape);
  void PrepareTexturesBasedOnIndex(
    Landscape *l, TexMatIndex &index, const GridRectangle &rect, Vector3Par pos, float zMin
  );
  
  void BuildLocalMaterials(Landscape *l, Shape &shape, int lod);
  //return true if there was some merging
  bool MergeAndSnapRoads(Shape &roads, RoadLink *roadLink, OffTreeRoad *roadVertexTree);
  bool MergeAndSnapRoads(Shape &roads, const LODShape *lShape, const Matrix4 &transform, OffTreeRoad *roadVertexTree);
  
  void GenerateRoad(int lod, float dist, Landscape *land);
  
  void Clear();
  
  static size_t GetMemoryControlled(int landSegSize)
  {
    // texture "indices" may be a significant memory footprint
    return sizeof(LandSegment) + sizeof(LandSegment::TexMPassInfo) * landSegSize * landSegSize;  
  }
};

template <class Type>
struct MapClassTraitsLandCache: public DefMapClassTraits< Ref<Type> >
{
  /// key type
  typedef const GridRectangle &KeyType;
  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    // we know segments are segment size aligned
    // we might divide both coordinates by segment size
    // this might result in better hash keys
    // unfortunately we have no access to segment size here
    // we assume 16b is enough to contain
    // if not, some overlapping may occur, resulting in suboptimal hashing
    return key.xBeg+key.zBeg*33;
  }

  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2)
  {
    int d = k1.xBeg-k2.xBeg;
    if (d) return d;
    return k1.zBeg-k2.zBeg;
  }

  /// we want this to be very fast, even if memory requirements may be a little bit higher
  /**
  For a view-distance 3500m and shape 500m we get around 50 shapes.
  */
  static int CoefExpand() {return 4;}

  /// get a key for given a item
  static KeyType GetKey(const Ref<Type> &item) {return item->GetRect();}
};

template <>
struct MapClassTraits< Ref<LandSegment> >: public MapClassTraitsLandCache<LandSegment>
{
};

#if _VBS3_LARGE_WATERSEG
const int MaxWaterSegmentSize = 10; // can now be less than the actual waterSegmentSize - 11 is the maximum here due to the number of vertex shader constants
#else
const int MaxWaterSegmentSize = 8;
#endif

/// cached attributes needed for rendering of given water segment
class WaterSegment: public LandSegmentBase
{
  friend class Landscape;
  
  // currently the only thing we store is vertex shader constants for the grid
  float _waterDepth[(MaxWaterSegmentSize+1)*(MaxWaterSegmentSize+1)][4];

  public:  
  static size_t GetMemoryControlled() {return sizeof(WaterSegment);}
};

template <>
struct MapClassTraits< Ref<WaterSegment> >: public MapClassTraitsLandCache<WaterSegment>
{
};

/// satellite segment information
struct LandSegInfo
{
  /// a common material
  Ref<TexMaterial> _mat;
};

TypeIsMovableZeroed(LandSegInfo)



/// cache of landscape shapes
class LandCache: public MemoryFreeOnDemandHelper
{
  public:
  /// several water geometry LODs may be present at the same time  
  static const int NWaterLods = 4;
  
  private:
  friend class Landscape;
  
  TListBidirRef<LandSegment> _segments;
  TListBidirRef<WaterSegment> _waterSeg;
  
  /// we want fast searching
  MapStringToClass< Ref<LandSegment>, RefArray<LandSegment> > _index;
  MapStringToClass< Ref<WaterSegment>, RefArray<WaterSegment> > _waterIndex;
  
  int _maxN;
  int _maxWaterN;

  /// satellite segment size
  int _satSegmentSize;
  /// satellite segment z alignment
  int _satSegmentAlign;
  
  /// size of shape in landscape grid (Landscape::GetLandGrid())
  int _landSegmentSize;
  /// z alignment of the landscape grid (derived from _satSegmentAlign)
  int _landSegmentAlign;
  
  /// size of shape of water
  int _waterSegmentSize;
  
  /// number of possible terrain LOD levels
  int _nLods;
  /// is land segment mapped with only one sat segment?
  bool _singleSatLandSegment;
  
  float _invLandSegmentSize;

  // maximum memory used for storing landscape
  // we always should be able to contain single frame

  /// water geometry - shared for all water parts
  Shape _wTable[NWaterLods];

  /// information about each land segment
  /**
  Requires processing whole landscape. May be empty, e.g. in Buldozer.
  */
  Array2D<LandSegInfo> _landSeg;
  
  /// where z is starting in _landSeg
  int _landSegZStart;

  void ProcessLandSegInfo(Landscape *l);
    
  
  public:
  LandCache();
  void Clear();
  
  /// number of possible terrain LOD levels
  int NLods() const {return _nLods;}

  /// perform LOD selection based on distance
  float LandLODWanted(Landscape *l, float minDist) const;
  /// perform LOD selection based on position
  float MinDistRect(Landscape *l, Vector3Par pos, float minZ, const GridRectangle &sRect) const;

  int GetSatSegSize(int &zAlign) const
  {
    zAlign = _satSegmentAlign;
    return _satSegmentSize;
  }

  void Init(Landscape *land, bool forceProcessing);

  LandSegment *FirstSegment() const {return _segments.First();}
  LandSegment *NextSegment(LandSegment *cur) const {return _segments.Next(cur);}

  Ref<LandSegment> Segment(Landscape *land, const GridRectangle &rect, int lod);

  Ref<WaterSegment> WSegment(Landscape *land, const GridRectangle &rect);
  
  /// underlying data has changed - force update
  void ForceUpdate(int x, int z);

  void Fill(Landscape *land, const Frame &pos);

  void PreloadSegAt(int xxx, int zzz, Landscape * land, Vector3Par pos);
  
  bool VerifyStructure() const;

  void Delete(LandSegment *seg)
  {
    GEventLogger.Add(seg->_rect,"Delete");
    _segments.Delete(seg);
    _index.Remove(seg->GetRect());
  }
  void Insert(LandSegment *seg)
  {
    _segments.Insert(seg);
    _index.Add(seg);
  }
  void Delete(WaterSegment *seg)
  {
    _waterSeg.Delete(seg);
    _waterIndex.Remove(seg->GetRect());
  }
  void Insert(WaterSegment *seg)
  {
    _waterSeg.Insert(seg);
    _waterIndex.Add(seg);
  }
  // { memory free on demand implementation
  size_t FreeOneItem();
  float Priority() const;
  size_t MemoryControlled() const;
  RString GetDebugName() const;
  // }
  
  private:
  void CalculateLandSegmentSize(Landscape *l);
};


LandSegmentBase::LandSegmentBase()
{
}

LandSegmentBase::~LandSegmentBase()
{
}

void LandSegmentBase::Clear()
{
  _rect.xBeg=INT_MAX,_rect.xEnd=INT_MIN; // invalid rectangle
  _rect.zBeg=INT_MAX,_rect.zEnd=INT_MIN;
  _valid=false;
  _offset = VZero;
}


bool LandSegmentBase::VerifyStructure() const
{
  return true;
}

bool LandSegmentBase::ValidFor( const GridRectangle &rect ) const
{
  if( !_valid ) return false;
  if( _rect.xBeg>rect.xBeg ) return false;
  if( _rect.xEnd<rect.xEnd ) return false;
  if( _rect.zBeg>rect.zBeg ) return false;
  if( _rect.zEnd<rect.zEnd ) return false;
  return true;
}

LandSegment::LandSegment()
{
  Clear();
}

LandSegment::~LandSegment()
{
}


/// default distance, where ground detail normal is starts to blend to satellite normal texture
const float Landscape::DefaultSatelliteNormalBlendStart = 80.0f;
/// default distance, where blending of detail normal ends (satellite normal is fully visible)
const float Landscape::DefaultSatelliteNormalBlendEnd = 100.0f;
/// if satellite normal should be shown
bool Landscape::ShowSatelliteNormal = true;

void Landscape::FreeIds()
{
  // make sure clutter mappings are cleared, to avoid dangling them
  ClearClutterMapping();

  if( GScene ) GScene->FlushLandCache();
  _roadNet.Free();
#if _HELISIM_AMBIENT_TRAFFIC
  _ambientRoadNetwork.Free();
#endif
  _buildingsOffTree.Clear();
  _buildings.Clear();
  _features.Clear();

  // it is very likely most Ids are in link trackers.
  // Make sure there are no links left
  #if _ENABLE_REPORT
  for (int zz=0; zz<GetLandRange(); zz++)
  for (int xx=0; xx<GetLandRange(); xx++)
  {
    const ObjectList &list=_objects(xx,zz);
    for( int i=0; i<list.Size(); i++ )
    {
      Object *obj = list[i];
      if (obj->Track())
      {
#if _HELISIM_LARGE_OBJECTID
        RptF("Object %s: %I64x (%s) is still tracked",
          cc_cast(obj->GetDebugName()), obj->GetObjectId().Encode(), cc_cast(obj->GetObjectId().GetDebugName()));
#else
        RptF(
          "Object %s: %x (%s) is still tracked",
          cc_cast(obj->GetDebugName()),
          obj->GetObjectId().Encode(),
          cc_cast(obj->GetObjectId().GetDebugName())
        );
#endif
      }
    }
  }
  #endif
  
  while (ObjectIdAutoDestroy *id = _idList.First())
  {
#if _HELISIM_LARGE_OBJECTID
    RptF("Link to %I64x (%s) not released",id->Encode(),cc_cast(id->GetDebugName()));
#else
    RptF("Link to %x (%s) not released",id->Encode(),cc_cast(id->GetDebugName()));
#endif //_HELISIM_LARGE_OBJECTID
    *id = ObjectIdAutoDestroy();
  }
}

class LandscapeAddObject
{
  Landscape *_land;
  
  public:
  LandscapeAddObject(Landscape *land):_land(land){}
  bool operator () (Object *obj) const {_land->AddObject(obj);return true;}
};

static void InitLandMat(Texture *midDetail, TexMaterial *mat)
{
  if (mat)
  {
    mat->Load();
    mat->SetRenderFlag(RFLandShadow,true);
    // override middle detail textures - not specified in the material
    mat->_stage[LandMatStageMidDet]._tex = midDetail ? midDetail : GPreloadedTextures.New(TextureHalf);
    // set usage for all textures
    for (int stage=0; stage<mat->_stageCount+1; stage++)
    {
      Texture *tex = mat->_stage[stage]._tex;
      if (tex) tex->SetUsageType(Texture::TexLandscape);
    }
  }
}


#if _ENABLE_CHEATS
  bool CheckDrawTerrain();
  bool CheckDrawWater();
  bool CheckDrawRoads();
#else
  static inline bool CheckDrawTerrain(){return true;}
  static inline bool CheckDrawWater(){return true;}
  static inline bool CheckDrawRoads(){return true;}
#endif

TerrainCache::TerrainCache(Terrain *terrain, const GridRectangle &rect, const TerrainCache *src)
{
  // reuse what we can reuse
  _cache.Dim(rect.Width()>>DimLog,rect.Height()>>DimLog);
  _rect = rect;
  if (src)
  {
    PROFILE_SCOPE_DETAIL_EX(tcCpy,*);
    GridRectangle reused = src->_rect&rect;
      
    for (int z=reused.zBeg; z<reused.zEnd; z+=Dim) for (int x=reused.xBeg; x<reused.xEnd; x+=Dim)
    {
      Set(x,z) = src->Get(x,z);
    }
  }
  // any NULLs needs to be filled now
  // TODO: use GridRectangle::ForEachInComplement instead
  PROFILE_SCOPE_DETAIL_EX(tcCrt,*);
  for (int z=rect.zBeg; z<rect.zEnd; z+=Dim) for (int x=rect.xBeg; x<rect.xEnd; x+=Dim)
  {
    if (!Get(x,z))
    {
      // cache only outside data, not inside
      if (!InRange(x,z) || !InRange(x+Dim,z+Dim))
      {
        // TODO: make sure GetPatch is MT safe and use microjobs for this loop
        PROFILE_SCOPE_DETAIL_EX(tcCCI,*);
        TerrainCacheItem *item = new TerrainCacheItem;
        terrain->GetPatch(item->_data[0],x,z,Dim,Dim);
        Set(x,z) = item;
      }
    }
  }
  #if _ENABLE_REPORT
  _destructed=false;
  #endif
}

#if _HELISIM_AMBIENT_TRAFFIC
void Landscape::GenerateAmbientTraffic(float t, float x0, float x1, float z0, float z1) {
  if (_ambientRoadNetwork) _ambientRoadNetwork->GenerateTraffic(t, x0, x1, z0, z1);
}

void Landscape::SetAmbientTrafficGap(float gap, float x0, float x1, float z0, float z1) {
  if (_ambientRoadNetwork) _ambientRoadNetwork->SetGap(gap, x0, x1, z0, z1);
}

void Landscape::SetAmbientTrafficSpeed(float speed, float x0, float x1, float z0, float z1) {
  if (_ambientRoadNetwork)
  {
    if (speed >= 0) _ambientRoadNetwork->SetSpeed(speed, x0, x1, z0, z1);
  }
}

void Landscape::SetAmbientTrafficEnabled(bool enable) {
  if (_ambientRoadNetwork) _ambientRoadNetwork->SetEnabled(enable);
}

float Landscape::GetAmbientTrafficAvgTrajectoriesPerRoad() {
  return _ambientRoadNetwork ? _ambientRoadNetwork->GetAvgTrajectoriesPerRoad() : 0;
}

#endif


void Landscape::ReleaseAllVBuffers()
{ 
  for( LandSegment *segi=_segCache->FirstSegment(); segi; )
  {
    LandSegment *segn = _segCache->NextSegment(segi);
    // if there is any non-VB geometry, we want to keep the entry -----  WHY ????
    // note: this should not happen, non-VB geometry is only temporary
    bool toDelete = true;
    for (int i=0; i<LandSegment::NLods; i++)
    {
      if (segi->_lod[i]._table.GetVertexBuffer())
      {
        segi->_lod[i]._table.ReleaseVBuffer();
      }
      // Why - comment above says just the opposite ????
//       if (!segi->_lod[i]._table.IsGeometryLoaded())
//       {
//         // do not delete when geometry is not loaded? Why - comment above says just the opposite ????
//         toDelete = false;
//       }
    }
    if (toDelete)
    {
      _segCache->Delete(segi);
    }
    segi = segn;
  }
  // vbuffer needs to be recreated when necessary
  for (int i=0; i<LandCache::NWaterLods; i++)
  {
    _segCache->_wTable[i].ReleaseVBuffer();
  }

  if( GScene )
  {
    GScene->GetShadowCache().Clear();
    GScene->CleanUp();
  }
  for (int i=0; i<_clutter.Size(); i++)
  {
    LODShape *shape = _clutter[i]._shape;
    if (!shape || shape->NLevels()<=0) continue;
    ShapeUsed level0 = shape->Level(0);
    level0->ReleaseVBuffer();
  }
}

//-----------------------------------------------------------------------------
int Landscape::GetLandSegmentSize() const
{
  return _segCache->_landSegmentSize;
}

int Landscape::GetLandSegmentAlign() const
{
  return _segCache->_landSegmentAlign;
}
  

float Landscape::GetInvLandSegmentSize() const
{
  return 1.0f/_segCache->_landSegmentSize;
}

#if _ENABLE_CHEATS
  static int MaxWaterLOD = LandCache::NWaterLods-1;
#else
  const int MaxWaterLOD = LandCache::NWaterLods-1;
#endif

int Landscape::GetWaterSegmentSize() const
{
  return _segCache->_waterSegmentSize;
}

void Landscape::FillCache( const Frame &pos )
{
  extern size_t systemFreeRequired;
  extern size_t mainHeapFreeRequired;
  
  //@{ _DEDICATED_CLIENT
  if (!IsDedicatedServer() && !IsDedicatedClient())
  {
    TimeScope time("Fill Terrain");
    LogF("Recreate caches %.1f,%.1f",pos.Position().X(),pos.Position().Z());
    _segCache->Fill(this,pos);
    for (int lod=0; lod<=MaxWaterLOD; lod++)
    {
      GenerateWaterSegment(lod);
    }
  }
  //@}

  // create normal conditions for objects as well
  FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
  
  // force loading all relevant ObjectListUsed rectangles
  TimeScope time("Fill Objects");

  int xMin,xMax,zMin,zMax;
  ObjRadiusRectangle(
    xMin,xMax,zMin,zMax,pos.Position(),pos.Position(),Glob.config.objectsZ
  );
  int regular = 0;    
  for( int z=zMin; z<=zMax; z++ ) for( int x=xMin; x<=xMax; x++ )
  {
    ProgressRefresh();
    if (regular++>=10)
    {
      regular = 0;
      // no need to call for each grid, but time to time we should do it
      FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
    }
    // TODO: optimization - many slots can be skipped
    // use objects, but ignore results
    UseObjects(x,z);
  }
  // it is very important to call GC here so that the rest of the game can operate under normal conditions
  FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
}

#if _VBS3
// used to change the Height of the terrain during the game
// call instead of HeightChange to make sure that changes get restored after the mission

struct DetectPosition
{
  int _x, _y;

  DetectPosition(int x, int y)
  {
    _x = x; _y = y;
  }

  bool operator () (int x, int y, int size) const
  {
    return _x >= x && _y >= y && _x < x + size && _y < y + size;
  }
};

struct ChangeNode
{
  int _x, _y;
  float _newHeight;

  ChangeNode(int x, int y, float newHeight)
    :_x(x), _y(y), _newHeight(newHeight)
  {
  }

  bool operator () (HeightNode &item)
  {
    if(item._x == _x && item._y == _y)
    {
      item._newHeight = _newHeight;
      return true;
    }
    return false;
  }
};

void Landscape::RT_ChangeHeightPoint( int x, int z, float y )
{
  DetectPosition detect(x,z);
  ChangeNode change(x,z,y);

  //store old value
  if(!_changedHeightField.ForEachInRegion(detect, change))
    _changedHeightField.Set(x,z,HeightNode(x,z, GetData(x,z), y));

  HeightChange(x, z, y);
}

bool HeightIsSharedByPreviousSegment(int xx, int dataShift)
{
  // If xx modulo (1<<dataShift) is zero, then the height is shared with previous segment
  return (xx & ((1 << dataShift) - 1)) ? false : true;
}

void Landscape::RT_HeightChange( float x, float z, float y )
{
  int xx = toInt(x * GetInvTerrainGrid());
  int zz = toInt(z * GetInvTerrainGrid());

  if (!land_TerrainInRange(zz, xx))
  {
    LogF("[m] Requested Terrain not in range (%f/%f)", x,z );
    return;
  }

  RT_ChangeHeightPoint(xx,zz,y);

  // Flush the terrain segments cache - take special care when the changed height is shared by more segments
  int dataShift = GetSubdivLog();
  int xxLgBeg, xxLgEnd, zzLgBeg, zzLgEnd;
  xxLgBeg = xxLgEnd = xx >> dataShift;
  zzLgBeg = zzLgEnd = zz >> dataShift;
  if (HeightIsSharedByPreviousSegment(xx, dataShift)) xxLgBeg--;
  if (HeightIsSharedByPreviousSegment(zz, dataShift)) zzLgBeg--;
  for(int x = xxLgBeg; x <= xxLgEnd; ++x)
    for(int z = zzLgBeg; z <= zzLgEnd; ++z)
      FlushSegCache(x, z);

  FlushGroundClutterSegCache();
  //FlushAI as well?
}

void Landscape::RT_HeightChangeArea( float xs, float xe, float zs, float ze, float y, bool changeLower, bool changeHigher)
{
//  LogF("[m] change area: %f->%f  %f->%f height %f", xs, xe, zs, ze, y);
  if(xs > xe) Swap(xs, xe);
  if(zs > ze) Swap(zs, ze);
  
//  LogF("[m] change area sorted: %f->%f  %f->%f height %f", xs, xe, zs, ze, y);
  int l_xx = toInt(xs * GetInvTerrainGrid());
  int r_xx = toInt(xe * GetInvTerrainGrid());

  int b_zz = toInt(zs * GetInvTerrainGrid());
  int t_zz = toInt(ze * GetInvTerrainGrid());

//  LogF("[m] change area int: %d->%d  %d->%d height %f", l_xx, r_xx, t_zz, b_zz, y);

  DWORD start = GetTickCount();

  for(int x = l_xx; x <= r_xx; ++x)
  {
    for(int z = b_zz; z <= t_zz; ++z)
    {
      if (land_TerrainInRange(z, x))
      {
        float actHeight = GetHeight(z,x);
        if(actHeight < y && !changeLower) continue;
        if(actHeight > y && !changeHigher) continue;
        RT_ChangeHeightPoint(x,z,y);
      }
    }
  }
  
  DWORD t = GetTickCount();
  //LogF("[m] changed HeightNodes: %d ms", t - start);
  start = t;

  // Flush the terrain segments cache - take special care when the changed height is shared by more segments
  int dataShift = GetSubdivLog();
  int l_xxLg = l_xx >> dataShift;
  int r_xxLg = r_xx >> dataShift;
  int b_zzLg = b_zz >> dataShift;
  int t_zzLg = t_zz >> dataShift;
  if (HeightIsSharedByPreviousSegment(l_xx, dataShift)) l_xxLg--;
  if (HeightIsSharedByPreviousSegment(b_zz, dataShift)) b_zzLg--;

  for(int x = l_xxLg; x <= r_xxLg; ++x)
    for(int z = b_zzLg; z <= t_zzLg; ++z)
      FlushSegCache(x, z);

  t = GetTickCount();
  LogF("[m] flushed segments: %d ms", t - start);
  start = t;

  FlushGroundClutterSegCache();
  t = GetTickCount();
  LogF("[m] FlushGroundClutterSegCache: %d ms", t - start);
  start = t;

  //FlushAI as well?
}

struct DetectAll
{
  bool operator () (int x, int y, int size) const
  {
    return true;
  }
};

struct RestoreHeight
{
  Landscape* _land;
  RestoreHeight(Landscape* landscape)
    :_land(landscape)
  {
  }
  bool operator () (HeightNode &item)
  {
    _land->HeightChange(item._x, item._y, item._oldHeight);
    return false;
  }
};

void Landscape::RestoreOldHeightLevels()
{
  DetectAll detect;
  RestoreHeight change(this);
  _changedHeightField.ForEachInRegion(detect, change);
  _changedHeightField.Clear();
  FlushSegCache();
}
#endif


void Landscape::HeightChange(int xx, int zz, float yy)
{
  // Make sure we stay within XZ range
  Assert( land_TerrainInRange(xx,zz) );

#if _VBS3_CRATERS_DEFORM_TERRAIN
  // Remember old height
  float oldHeight = GetData(xx, zz);
#endif

  // Set the new height
  _terrain->SetData(xx, zz, yy);  

#if _VBS3_CRATERS_DEFORM_TERRAIN

  // Refresh the environment (recalculate craters, affect object positions)
  {
    // Calculate world position of the grid shifted point
    Vector3 recalculateXZPosition(xx * GetTerrainGrid(), yy, zz * GetTerrainGrid());

    // Calculate distance for objects to be recalculated - consider we need to cover whole diagonal
    float recalculateXZRadius = sqrt(GetTerrainGrid() * GetTerrainGrid() * 2.0f);

    // Go through the shadow cache and remove all items in the vicinity (we want especially refresh the craters)
    GScene->GetShadowCache().Clear2DVicinity(recalculateXZPosition, recalculateXZRadius);

    // Go through the objects in the vicinity and move them towards the ground to reflect the terrain has been changed.
    {
      int xMin,xMax,zMin,zMax;
      ObjRadiusRectangle(xMin, xMax, zMin, zMax, recalculateXZPosition, recalculateXZPosition, recalculateXZRadius);

      // Go through the objects in the vicinity and change their position
      for( int z=zMin; z<=zMax; z++ )
      {
        for( int x=xMin; x<=xMax; x++ )
        {
          const ObjectListUsed &list = UseObjects(x, z);
          int n=list.Size();
          for( int i=0; i<n; i++ )
          {
            // Get object affected by the ground change
            Object *o = list[i];

            // No object, no fun
            if (!o) continue;

            // If object is high enough (like an aircraft), don't affect it
            // Don't use this call as the reverse set need not to address all objects otherwise
            //if (o->Position().Distance2(recalculateXZPosition) > Square(o->GetRadius() + recalculateXZRadius)) continue;

            // If object is ground aligned (like crater) then align it, else change its position (relatively to previous position)
            if (o->IsGroundAligned())
            {
#if _VBS3_CRATERS_LATEINIT
              // Register object for late initialization
              {
                // Get distance of a crater from camera position
                const Camera &camera=*GScene->GetCamera();
                float dist = camera.Position().Distance(o->Position());

                // Creation priority depends on object size and distance to camera
                float priority = o->DesiredScale() / dist;

                // Register object
                GWorld->RegisterLateInitObject(o, priority);
              }
#else
              o->AlignWithGround();
#endif
            }
            else
            {
              // Set the previous height for a moment
              _terrain->SetData(xx, zz, oldHeight);

              // Get the previous height of the object
              float prevHeight = SurfaceY(o->Position().X(), o->Position().Z());

              // Restore the previous height
              _terrain->SetData(xx, zz, yy);

              // Get the new height of the object
              float newHeight = SurfaceY(o->Position().X(), o->Position().Z());

              // Move the object accordingly
              //o->Move(Vector3(o->Position().X(), o->Position().Y() - (prevHeight - newHeight), o->Position().Z())); // This function must not be called, as it changes item order inside the list
              o->SetPosition(Vector3(o->Position().X(), o->Position().Y() - (prevHeight - newHeight), o->Position().Z()));
              o->OnPositionChanged();
              o->OnMoved();
            }
          }
        }
      }
    }
  }

  // Remove affected craters from shadow cache to make sure they are going to be recalculated next time they are drawn
  //GWorld->ForEachAnimal(RemoveFromGeometryCache());
#endif
}

void Landscape::TextureChange( int x, int z, int id )
{
  Assert( land_InRange(x,z) );

#if 1
  if (id < 0 || id >= _terrain->_texture.Size() || _terrain->_texture[id]._mat.IsNull())
  {
    RptF("Bad Texture on (%d,%d) ID %d", x,z,id);
    return; 
  }
#endif

  SetTex(x,z,id);
  //FlushCache();
}

/*!
\patch_internal 1.45 Date 2/20/2002 by Ondra
- New: Variable landscape dimensions.
*/

void Landscape::Dim(int x,int z,int rx, int rz, float landGrid)
{
  _roadNet.Free();
#if _HELISIM_AMBIENT_TRAFFIC
  _ambientRoadNetwork.Free();
#endif
  _objIndex.Free();;
  _mapObjIndex.Free();;

  // all caches will become invalid now - release them
  FreeCaches();
  
  int xLog = 0;
  int xVal = x;
  while (xVal>1) xLog++,xVal >>= 1;

  int rxLog = 0;
  int rxVal = rx;
  while (rxVal>1) rxLog++,rxVal >>= 1;

  if (x!=z)
  {
    ErrorMessage("Landscape dimensions %dx%d not rectangular",x,z);
  }
  if (x!=1<<xLog)
  {
    ErrorMessage("Landscape dimensions %dx%d not power of 2",x,z);
  }
  if (rx!=rz)
  {
    ErrorMessage("Terrain dimensions %dx%d not rectangular",rx,rz);
  }
  if (rx!=1<<rxLog)
  {
    ErrorMessage("Terrain dimensions %dx%d not power of 2",rx,rz);
  }

  _terrain->Dim(x, z, rx, rz, xLog, rxLog);
  _soundMap.Dim(x,z);
  _terrain->_tex.Dim(x,z);
  _buildingsOffTree.Clear();
  _buildings.Clear();
  _features.Clear();
  _objects.Dim(x,z);
  _mapObjects.Dim(x,z);
  // _terrain, not _land

  // by default do not provide any grass approximation
  _grassApprox.Clear();
  // no clipping information present
  _yRange.Clear();

 // Assert(rx==x);
 // Assert(rz==z);
  // each access should update as necessary
  SetLandGrid(landGrid);
}

void Landscape::SetLandGrid(float grid)
{
  _terrain->SetLandGrid(grid);
  OnTerrainGridChanged();
}

Landscape *GLandscape;

#pragma warning(disable:4355)

typedef QuadTreeExRoot<short, QuadTreeExTraitsWORD<short> >::QuadTreeType QuadTreeExShort;
// DEFINE_FAST_ALLOCATOR(QuadTreeExShort)

typedef QuadTreeExRoot<unsigned short, QuadTreeExTraitsWORD<unsigned short> >::QuadTreeType QuadTreeExUShort;
// DEFINE_FAST_ALLOCATOR(QuadTreeExUShort)

//typedef QuadTreeExRoot<unsigned short, QuadTreeExTraitsWORD<unsigned short>,1,2 >::QuadTreeType QuadTreeExUShort12;
//DEFINE_FAST_ALLOCATOR(QuadTreeExUShort12)

typedef QuadTreeExRoot<ObjectList, QuadTreeExTraitsObjectList>::QuadTreeType QuadTreeExObjectList;
// DEFINE_FAST_ALLOCATOR(QuadTreeExObjectList)

typedef QuadTreeExRoot<MapObjectList, QuadTreeExTraitsMapObjectList>::QuadTreeType QuadTreeExMapObjectList;
// DEFINE_FAST_ALLOCATOR(QuadTreeExMapObjectList)

// DEFINE_FAST_ALLOCATOR(QuadTreeEx<RawType>)
// DEFINE_FAST_ALLOCATOR(QuadTreeExRoot<int>::QuadTreeType)

void LandSegment::Clear()
{
  base::Clear();
  for (int i=0; i<NLods; i++)
  {
    _lod[i]._table.Clear();
    _lod[i]._roads.Clear();
    _lod[i]._roadsDone = false;
  }
  _index.Clear();
  _indexRoads.Clear();
}


int LandSegment::TexMatIndex::AddInfo(Texture *tex, float aot)
{
  Assert(_finite(aot) && aot<FLT_MAX*1e-3f);
  int index = _textures.FindKey(tex);
  if (index>=0)
  {
    saturateMax(_textures[index]._aot,aot);
    return index;
  }
  index = _textures.Add();
  TexAOTInfo &info = _textures[index];
  info._tex = tex;
  info._aot = aot;
  return index;
}

void LandSegment::BuildTexMatIndex(TexMatIndex &index, Landscape *l)
{
  // scan whole source area
  float aotGrid = Square(l->GetLandGrid());
  for (int zz=_rect.zBeg; zz<_rect.zEnd; zz++)
  for (int xx=_rect.xBeg; xx<_rect.xEnd; xx++)
  {
    const Scenery::TextureInfo &info = l->ClippedTextureInfo(zz,xx);
    if (info._mat)
    {
      TexMPassInfo &grid = index._grid(xx-_rect.xBeg,zz-_rect.zBeg);
      // we need to add textures from materials as well
      // adapted from Shape::BuildTexAreaIndex
      const TexMaterial *mat = info._mat;
      // caution: material needs to be loaded for building the index
      Assert(mat->IsReady());
      mat->Load();
      for (int i=0; i<mat->_stageCount+1; i++)
      {
        if (!mat->_stage[i]._tex) continue;
        Texture *tex = mat->_stage[i]._tex;

        // check how does texgen affect the mipmap selection
        int tg = mat->_stage[i]._texGen;
        UVSource uvs = mat->_texGen[tg]._uvSource;
        float aot = 0;
        switch (uvs)
        {
          case UVNone:
            // we need the best mipmap we can get
            aot = FLT_MAX;
            break;
          case UVTex:
            aot = aotGrid/mat->_texGen[tg].GetUVScale2D();
            break;
          case UVTex1:
            aot = aotGrid/mat->_texGen[tg].GetUVScale2D();
            break;
          case UVPos:
          case UVWorldPos:
            // pos-based texgen areaOTex is 1/1 by definition
            aot = 1/mat->_texGen[tg].GetUVScale3D();
            break;
          default:
            Fail("UVSource not implemented");
            break;
        }

        // Hotfix: detail textures not loaded when camera far from terrain
        saturateMax(aot, 10e4);

        // if there is one material per segment (most common), the way the grid is constructed it always maps index 1:1
        grid._pass.Add(TexPassInfo(index.AddInfo(tex, aot)));
      }
      grid._pass.Compact();
    }
    else if (l->OutsideTextureInfo()._mat)
    {
      RptF("Strange - no material used on terrain %d,%d?",xx,zz);
    }
  }
}

void LandSegment::UpdateTexMatIndex(TexMatIndex &index, int xs,int zs,LODShape *lshape)
{
  saturate(xs, 0,index._grid.GetXRange()-1);
  saturate(zs, 0,index._grid.GetYRange()-1);
  TexMPassInfo &grid = index._grid(xs,zs);
  
  //LODShape *lshape=obj->GetShape();
  ShapeUsedGeometryLock<> shape(lshape,0);
  for (int s=0; s<shape->NSections(); s++)
  {
    const ShapeSection &sec = shape->GetSection(s);

    Texture *tex = sec.GetTexture();
    if (tex)
    {
      grid._pass.Add(TexPassInfo(index.AddInfo(tex,sec.GetAreaOverTex(0))));
    }

    TexMaterial *mat = sec.GetMaterialExt();
    if (mat)
    {
      Assert(mat->IsReady());
      mat->Load();
      for (int i=0; i<mat->_stageCount+1; i++)
      {
        if (!mat->_stage[i]._tex) continue;
        Texture *tex = mat->_stage[i]._tex;
        // check how does texgen affect the mipmap selection
        int tg = mat->_stage[i]._texGen;
        UVSource uvs = mat->_texGen[tg]._uvSource;
        float aot = 0;
        switch (uvs)
        {
          case UVNone:
            // we need the best mipmap we can get
            aot = FLT_MAX;
            break;
          case UVTex:
            aot = sec.GetAreaOverTex(0)*mat->_texGen[tg].GetUVScale2D();
            break;
          case UVTex1:
            aot = sec.GetAreaOverTex(1)*mat->_texGen[tg].GetUVScale2D();
            break;
          case UVPos:
          case UVWorldPos:
            // pos-based texgen areaOTex is 1/1 by definition
            aot = mat->_texGen[tg].GetUVScale3D();
            break;
          default:
            Fail("UVSource not implemented");
            break;
        }
        grid._pass.Add(TexPassInfo(index.AddInfo(tex,aot)));
      }
    }
  }
}

void LandSegment::BuildLocalMaterials(Landscape *l, Shape &shape, int lod)
{
  // materials assume engine can use world space texgen
  // we prefer converting world space to model space here
  // world space texgen requires longer shaders, moreover it is not implemented yet
  // we need to traverse all sections
  Ref<TexMaterialType> typeDefault = new TexMaterialTypeModelSpace(TexMaterialName::_defaultType,Offset());
  // replace all materials by a simple ones
  for (int s = _lod[lod]._geometry[GTBasic]._beg; s < _lod[lod]._geometry[GTBasic]._end; s++)
  {
    ShapeSection &sec = shape.GetSection(s);
    TexMaterial *mat = sec.GetMaterialExt();
    // some polygons has no material - they are used as a helper pass only
    if (!mat) continue;
    
    VertexShaderID vs = mat->GetVertexShaderID();
    Ref<TexMaterial> newMat = mat;
    switch (vs)
    {
      case VSTerrain:
        // new style terrain is not singlePassType
        Assert(mat->GetPixelShaderID()==PSTerrainX || mat->GetPixelShaderID()==PSTerrainSNX || mat->GetPixelShaderID()==PSTerrainSimpleX);
        //Assert(
        //  mat->GetPixelShaderID()>=PSTerrain1 && mat->GetPixelShaderID()<=PSTerrain15 ||
        //  mat->GetPixelShaderID()>=PSTerrainSimple1 && mat->GetPixelShaderID()<=PSTerrainSimple15
        //);
        DoAssert( mat->GetName()._type==TexMaterialName::_defaultType );
        newMat = GTexMaterialBank.NewDerived(mat,typeDefault);
        break;
      default:
        Fail("Unknown terrain material");
        break;
    }
    
    sec.SetMaterialExt(newMat);
  } 
    
}

bool LandSegment::MergeAndSnapRoads(Shape &roads, const LODShape *lShape, const Matrix4 &transform, OffTreeRoad *roadVertexTree)
{
  Assert(lShape);
  if(!lShape) return false;

  if (lShape->FindGeometryLevel()<0 || lShape->GeometryLevel()->NFaces()==0)
  {
    ShapeUsedGeometryLock<> source(lShape,0);
    // we need to keep ST if it was in the original model
    // we are not interested in any selections
    const Shape* shape = source.GetShape();
    if (!shape)
    {
      RptF("Road %s has no LODs", cc_cast(lShape->GetName())) ;
      return false; //road has no shape to merge
    }

#if _ENABLE_REPORT
    for (int s=0; s<shape->NSections(); s++)
    {
      const ShapeSection &sec = shape->GetSection(s);
      if (sec.GetMaterialExt()==NULL)
      {
        // if there is no material, it is a problem
        LogF("Road %s with no material, lod %s",cc_cast(lShape->GetName()),cc_cast(lShape->LevelName(shape)));
      }
    }
#endif
    // any selections would be ignored anyway
    //shape->ClearSelections();
    //merge should also round close road vertexes to one vertex
    MergeRoads(&roads, shape, lShape, transform, roadVertexTree, true, false);
    return true;
  }
  return false; //no road
}

bool LandSegment::MergeAndSnapRoads(Shape &roads, RoadLink *roadLink, OffTreeRoad *roadVertexTree)
{
  LODShape *lshape=roadLink->GetLODShape();
  Assert(lshape);
  if(!lshape || lshape->NLevels()<=0) return false;
  // note: roads do not have any components (but bridges have, so do not include them)
  // See also Road::IsMerged()
  if (lshape->FindGeometryLevel()<0 || lshape->GeometryLevel()->NFaces()==0)
  {
    ShapeUsedGeometryLock<> source(lshape,0);
    // we need to keep ST if it was in the original model
    // we are not interested in any selections
    const Shape* shape = source.GetShape();
    if (!shape)
    {
      RptF("Road %s has no LODs", cc_cast(lshape->GetName())) ;
      return false; //road has no shape to merge
    }
    
    #if _ENABLE_REPORT
      for (int s=0; s<shape->NSections(); s++)
      {
        const ShapeSection &sec = shape->GetSection(s);
        if (sec.GetMaterialExt()==NULL)
        {
          // if there is no material, it is a problem
          LogF("Road %s with no material, lod %s",cc_cast(lshape->GetName()),cc_cast(lshape->LevelName(shape)));
        }
      }
    #endif
    // any selections would be ignored anyway
    //shape->ClearSelections();
    //convert road to segment's model space
    Matrix4 fromWorld(MTranslation,-_offset);
    //merge should also round close road vertexes to one vertex
    MergeRoads(&roads, shape, lshape, fromWorld*roadLink->Transform(),roadVertexTree,true, false);
    return true;
  }
  return false; //no road
}

/**
Count length of the sequence of grid squares with the same texture in given stage
Used for CheckSatSegSize
*/
static int CountSameTextureZ(Landscape *l, int x, int z, int stage)
{
  const Scenery::TextureInfo &t0 = l->ClippedTextureInfo(z,x);
  const TexMaterial *tm0 = t0._mat;
  if (!tm0) return 1;
  Texture *tex0 = tm0->_stage[stage]._tex;
  int zz;
  for (zz=z; zz<l->GetLandRange(); zz++)
  {
    const Scenery::TextureInfo &tC = l->ClippedTextureInfo(zz,x);
    const TexMaterial *tmC = tC._mat;
    if (!tmC) break;
    if (tmC->_stage[stage]._tex!=tex0) break;
  }
  return zz-z;
}

/**
use heuristics to determine satellite segment size
*/

static int CheckSatSegSize(Landscape *l, int &align)
{
  // if no sat. segment, we can return
  // 1 as no preference,
  // some high power of 2 as pow2 preferred
  // landscape size as fraction of landscape size preferred
  int noPreferrence = 1;
  align = 0;
  // check landscape material style based on material properties
  const Scenery::TextureInfo &t00 = l->ClippedTextureInfo(0,0);
  if (!t00._mat) return noPreferrence;
  const TexMaterial *tm00 = t00._mat;
  if (!tm00) return noPreferrence;
  if(tm00->GetPixelShaderID()!=PSTerrainX && tm00->GetPixelShaderID()!=PSTerrainSNX && (tm00->GetPixelShaderID()<PSTerrain1 || tm00->GetPixelShaderID()>PSTerrain15))
  {
    return noPreferrence;
  }
  // for new islands the sat. texture is stage 0
  int satStage = 0;
  // Sat. segment shares the same sat. texture in all grid squares
  // we can start anywhere, one segment may be shorted, but 2nd one needs to be correct
  int count0 = CountSameTextureZ(l,0,0,satStage);
  int count1 = CountSameTextureZ(l,0,count0,satStage);
  // align is needed to compensate for the starting origin
  int size = count0>count1 ? count0 : count1;
  align = count0%size;
  return size;
}

/*
/// prime numbers - source http://doc.trolltech.com/3.2/primes.html
const static int Primes[]=
{
  2,  3,  5,  7, 11, 13, 17, 19, 23, 29,
  31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
  73, 79, 83, 89, 97,101,103,107,109,113,
  127,131,137,139,149,151,157,163,167,173,
  179,181,191,193,197,199,211,223,227,229,
  233,239,241,251,257,263,269,271,277,281,
  283,293,307,311,313,317,331,337,347,349,
  353,359,367,373,379,383,389,397,401,409,
  419,421,431,433,439,443,449,457,461,463,
  467,479,487,491,499,503,509,521,523,541,
  547,557,563,569,571,577,587,593,599,601,
  607,613,617,619,631,641,643,647,653,659,
  661,673,677,683,691,701,709,719,727,733,
  739,743,751,757,761,769,773,787,797,809,
  811,821,823,827,829,839,853,857,859,863,
  877,881,883,887,907,911,919,929,937,941,
  947,953,967,971,977,983,991,997,1009,1013,
  1019,1021,1031,1033,1039,1049,1051,1061,1063,1069,
  1087,1091,1093,1097,1103,1109,1117,1123,1129,1151,
  1153,1163,1171,1181,1187,1193,1201,1213,1217,1223,
  1229,1231,1237,1249,1259,1277,1279,1283,1289,1291,
  1297,1301,1303,1307,1319,1321,1327,1361,1367,1373,
  1381,1399,1409,1423,1427,1429,1433,1439,1447,1451,
  1453,1459,1471,1481,1483,1487,1489,1493,1499,1511,
  1523,1531,1543,1549,1553,1559,1567,1571,1579,1583,
  1597,1601,1607,1609,1613,1619,1621,1627,1637,1657,
  1663,1667,1669,1693,1697,1699,1709,1721,1723,1733,
  1741,1747,1753,1759,1777,1783,1787,1789,1801,1811,
  1823,1831,1847,1861,1867,1871,1873,1877,1879,1889,
  1901,1907,1913,1931,1933,1949,1951,1973,1979,1987,
  1993,1997,1999,2003,2011,2017,2027,2029,2039
};
*/

/// calculate suitable segment size
/**
Large segments give better opportunity of creating large batches,
but they have bigger triangle overhead.
By keeping segment size proportional to view distance
we avoid too many DIP calls with increased view distance.
*/

void LandCache::CalculateLandSegmentSize(Landscape *l)
{
#if _VBS3_LARGE_LANDSEG
  float range=10000; // It is a constant, as we need land segment size to be independent on the view distance
#else
  float range=Glob.config.horizontZ;
#endif
  // with default view distance (900 m and below) we want land segment size to be 400 (8*50)
  // calculate segment size in m
  const float defView = 1200.0f; // not tied to game default - rescaling only
  const float defSegSize = 500.0f;
  float segmentSizeM = defSegSize;
  if (range>defView)
  {
    segmentSizeM = range*(defSegSize/defView);
  }
  // calculate segment size in terms on LandGrid
  float segmentSizeWanted = segmentSizeM*l->GetInvLandGrid();
  int segmentSize = toInt(segmentSizeWanted);
  // it is not necessary "per se" for LandSegment size to be power of 2
  // some requirements may be given by the LOD system
  // to have lods possible, we prefer multiplies of 4 (3 LODs possible)

  segmentSize = (segmentSize+2)&~(4-1);
  
  // avoid creating more points in one segment that we can handle
#if _VBS3_LARGE_LANDSEG
  const float maxPoints = 32767;
#else
  const float maxPoints = 8192;
#endif
  int subdiv = l->GetTerrainRange()/l->GetLandRange();
  int maxSegmentSize = toIntFloor(sqrt(maxPoints/(subdiv*subdiv)));
  // 12 is currently the limitation given by land shadow casting shader
  #if 0
    if (maxSegmentSize>8) maxSegmentSize=8;
  #else
#if _VBS3_LARGE_LANDSEG
  if (maxSegmentSize>24) maxSegmentSize=24;
#else
  if (maxSegmentSize>12) maxSegmentSize=12;
#endif
  #endif
  // zero should never happen, but we definitelly want to avoid at least 1x1 segment
  if (maxSegmentSize<1) maxSegmentSize=1;
  _satSegmentSize = CheckSatSegSize(l,_satSegmentAlign);
  
  if (segmentSize>maxSegmentSize) segmentSize = maxSegmentSize;

#if _VBS3_LARGE_LANDSEG
  // there's no point increasing the segment size beyond 12 if the satellite segment is not large too
  if (_satSegmentSize >= 12 && segmentSize > _satSegmentSize)
    segmentSize = _satSegmentSize;
#endif

  // try to be as close to the desired size as possible
  int best = segmentSize;
  if (segmentSize>_satSegmentSize)
  {
    // if bigger than sat. seg size, use a multiply of sat. segment
    float ratio = float(segmentSize)/_satSegmentSize;
    // select nearest lower and bigger
    int tryLo = toIntFloor(ratio)*_satSegmentSize;
    int tryHi = tryLo+_satSegmentSize;
    // check which one is closer, respect maxSegmentSize
    // lower is guaranteed to be within given limits
    Assert(tryLo<=maxSegmentSize);
    best = tryLo;
    if (tryHi<=maxSegmentSize && abs(tryHi-segmentSize)<abs(tryLo-segmentSize))
    {
      best = tryHi;
    }
    //return best;
  }
  else if (segmentSize<_satSegmentSize)
  {
    // if smaller than sat. seg size, use a fraction of sat. segment
    //float ratio = satSeg/float(segmentSize);
    //Assert(ratio>1);
    // currently we use only satSeg/primeNumber
    int bestDif = INT_MAX;
    // there is no use to try high ratios - the results with them are never efficient
    // we will use segmentSize instead
    for (int p=1; p<_satSegmentSize && p<4; p++)
    {
      int pf = _satSegmentSize/p;
      if (pf*p!=_satSegmentSize) continue;
      if (pf>maxSegmentSize) continue;
      int dif = abs(pf-segmentSize);
      if (bestDif>dif) bestDif=dif,best=pf;
    }
  }
  _landSegmentAlign = _satSegmentAlign%best;
  _landSegmentSize = best;
  _invLandSegmentSize = 1.0f/_landSegmentSize;


  // one LOD is always possible
  _nLods = 1;
  for (int i=0; i<LandSegment::NLods; i++)
  {
    int lodSize = 1<<i;
    if ((_landSegmentSize&(lodSize-1))==0) _nLods = i+1;
  }

  // with large view distance we prefer larger water segment size
  // TODO: larger water segment size dynamically for more distant segments
  // max. water segment size is given by the shader
#if _VBS3_LARGE_WATERSEG
  int waterSeg = 12;
#else
  int waterSeg = 8;
#endif
  
  // on change we need to recalc water map
  int oldWaterSeg = _waterSegmentSize;
  
  _waterSegmentSize = waterSeg;
  LogF(
    "Land segment size %d <- %.1f, max. %d, sat %d, lods %d",
    best,segmentSizeWanted,maxSegmentSize,_satSegmentSize,_nLods
  );
  LogF("Water segment size %d",waterSeg);
  if (oldWaterSeg!=waterSeg)
  {
    l->InitWaterInSeg(false);
    // flush all water mesh data, as they became obsolete
    for (int i=0; i<LandCache::NWaterLods; i++)
    {
      _wTable[i].ReleaseVBuffer();
    }
    // flush all cached water items - they are obsolete as well
    _waterIndex.Clear();
    _waterSeg.Clear();
  }

  LogF(
    "Land segment size %d <- %.1f, max. %d, sat %d, lods %d",
    best,segmentSizeWanted,maxSegmentSize,_satSegmentSize,_nLods
  );
  LogF("Water segment size %d",waterSeg);
  
  _singleSatLandSegment = (_satSegmentSize%_landSegmentSize)==0;
}

int Landscape::GetSatSegSize(int &zAlign) const
{
  return _segCache->GetSatSegSize(zAlign);
}

static int CalculateCacheSize(Landscape *l, bool log, float segmentSide)
{
  float range=Glob.config.horizontZ;
  // we need to be able to keep data when turning all way round
  // this means square (2*range) x (2*range)
  // assume worst possible alignment

  // we need to calculate the same range as calculated in LandCache::Fill
  // otherwise the cache would overflow during the Fill
  int sideSegments = toIntCeil(range/segmentSide)*2+1;
  int needSegments = sideSegments*sideSegments;
  const int minReasonable=7*7+1;
  #if _ENABLE_REPORT
    if (log)
    {
      LogF(
        "Wanted %d (%dx%d) segments (side %f, visibility %f)",
        needSegments,sideSegments,sideSegments,segmentSide,range
      );
    }
  #endif
  if( needSegments<minReasonable ) needSegments=minReasonable;
  // overestimate a little bit - having too many will do now harm, memory balancing should do the job anyway
  return needSegments*3;
}



LandCache::LandCache()
{
  // maxN depends on visibility
  // for visibility r there is around (r/(50*8))^2*4 neccessary segments
  // we will allocate cache to hold (r/(50*8))^2*6
  RegisterFreeOnDemandMemory(this);
  // provide reasonable defaults for the terrain
  _landSegmentSize = 8;
  _invLandSegmentSize = 1.0f/_landSegmentSize;
  _waterSegmentSize = 8;
  _singleSatLandSegment = false;
}

void LandCache::Clear()
{
  _segments.Clear();
  _index.Clear();
  _waterIndex.Clear();
  _waterSeg.Clear();
  //_wSegments.Clear();
  for (int i=0; i<NWaterLods; i++)
  {
    _wTable[i].Clear();
  }
}

RString LandCache::GetDebugName() const
{
  return Format("Terrain %g",GLandscape->GetTerrainGrid());
}

size_t LandCache::MemoryControlled() const
{
  /*
  Vertex buffers have their own management.
  The geometry created is only temporary and there is no need to track it.
  */
  // estimate how much memory is controlled by this container
  size_t size = _index.NItems()*LandSegment::GetMemoryControlled(_landSegmentSize);
  size_t water = _waterIndex.NItems()*WaterSegment::GetMemoryControlled();
  return size+water;
}

size_t LandCache::FreeOneItem()
{
  // most recently used segments are first
  // note: sometimes we should also flush water segments
  // we always flush the one in the most populated list
  if (_index.NItems()>_waterIndex.NItems())
  {
    LandSegment *seg=_segments.Last();
    if (!seg) return 0;
    Delete(seg);
    return LandSegment::GetMemoryControlled(_landSegmentSize);
  }
  else
  {
    WaterSegment *seg=_waterSeg.Last();
    if (!seg) return 0;
    Delete(seg);
    return WaterSegment::GetMemoryControlled();
  }
  
}

float LandCache::Priority() const
{
  return 0.25f;
}

bool LandCache::VerifyStructure() const
{
  for( LandSegment *segi=_segments.First(),*segn; segi; segi=segn )
  {
    segn=_segments.Next(segi);
    if (!segi->VerifyStructure()) return false;
  }
  return true;
}

#if _VBS3_LAND_SHADOW
int RoundToSegment(int x, int landSegmentSize, int landSegmentAlign)
#else
static inline int RoundToSegment(int x, int landSegmentSize, int landSegmentAlign)
#endif
{
  //landSegmentAlign = 0;
  int xMod = (x-landSegmentAlign)%landSegmentSize;
  // operator % is not handling negative numbers as modulo
  if (xMod<0) xMod += landSegmentSize;
  return x-xMod;
}


Ref<LandSegment> LandCache::Segment( Landscape *land, const GridRectangle &rect, int lod )
{
  // debugging/performance tuning - force segment creation in each frame
  #if 1
  static bool force = false;
  #else
  const bool force = false;
  #endif
  
  //PROFILE_SCOPE_EX(lSegG,*);
  DoAssert(RoundToSegment(rect.xBeg,_landSegmentSize,0)==rect.xBeg);
  DoAssert(RoundToSegment(rect.zBeg,_landSegmentSize,_landSegmentAlign)==rect.zBeg);
  Assert(RoundToSegment(rect.xEnd,_landSegmentSize,0)==rect.xEnd);
  Assert(RoundToSegment(rect.zEnd,_landSegmentSize,_landSegmentAlign)==rect.zEnd);
  // last in list is LRU
  // search for cached data
  
  // sea
  const Ref<LandSegment> &segi = _index.Get(rect);
  if (_index.NotNull(segi))
  {
    // check if segment is still valid
    // no need to keep the ref here - we know another reference is kept in _index
    _segments.Delete(segi);
    _segments.Insert(segi);
    // check if we can get the lod we need
    if (!force && segi->_lod[lod]._table.NVertex()>0 && segi->_lod[lod]._table.GetVertexBuffer())
    {
      return segi;
    }
    // continue - level needs to be created again
  }
  
  int count=_index.NItems();
  
#if 1
  // to avoid fragmentation we first delete and then generate
  if( segi.IsNull() && count>=_maxN )
  {
    #if LOG_SEG
      LandSegment *segi=_segments.Last();
      LogF("Dropped segment (count>%d) %d,%d",_maxN,segi->_rect.xBeg,segi->_rect.zBeg);
    #endif
    Delete(_segments.Last());
  }
#endif

  // generate cached data for segment rect
  Ref<LandSegment> seg=land->GenerateSegment(rect,segi,lod);
  //LogF("LandSegment count %d of %d",_segments.Size(),_maxN);
  if (_index.IsNull(segi))
  {
    Insert(seg);
  }
  return seg;
}

Ref<WaterSegment> LandCache::WSegment( Landscape *land, const GridRectangle &rect)
{
  // debugging/performance tuning - force segment creation in each frame
  #if 1
  static bool force = false;
  #else
  const bool force = false;
  #endif
  
  PROFILE_SCOPE_EX(lSegW,*);
  int waterSegmentSize = _waterSegmentSize;
  DoAssert(RoundToSegment(rect.xBeg,waterSegmentSize,0)==rect.xBeg);
  DoAssert(RoundToSegment(rect.zBeg,waterSegmentSize,0)==rect.zBeg);
  Assert(RoundToSegment(rect.xEnd,waterSegmentSize,0)==rect.xEnd);
  Assert(RoundToSegment(rect.zEnd,waterSegmentSize,0)==rect.zEnd);
  // last in list is LRU
  // search for cached data
  
  // sea
  const Ref<WaterSegment> &segi = _waterIndex.Get(rect);
  if (_waterIndex.NotNull(segi))
  {
    // check if segment is still valid
    // no need to keep the ref here - we know another reference is kept in _index
    _waterSeg.Delete(segi);
    _waterSeg.Insert(segi);
    // check if we can get the lod we need
    if (!force)
    {
      return segi;
    }
    // delete and continue - level needs to be created again
    Delete(segi);
  }
  
  int count=_waterIndex.NItems();
  
#if 1
  // to avoid fragmentation we first delete and then generate
  if(count>=_maxWaterN )
  {
    #if LOG_SEG
      WaterSegment *segi=_waterSeg.Last();
      LogF("Dropped water segment (count>%d) %d,%d",_maxWaterN,segi->_rect.xBeg,segi->_rect.zBeg);
    #endif
    Delete(_waterSeg.Last());
  }
#endif

  // generate cached data for segment rect
  Ref<WaterSegment> seg=land->GenerateWaterSegment(rect);
  //LogF("LandSegment count %d of %d",_segments.Size(),_maxWaterN);
  Insert(seg);
  return seg;
}

static float TerrainLODBias = -11; // the lower (more negative), the higher detail



float LandCache::LandLODWanted(Landscape *l, float minDist) const
{
  // connected to vertex shader
#if _VBS3_MORETERRAINLODS
  float maxLod = floatMin(NLods(), 3)-1;
  // -- we want to keep old calculations even if we have more than 3 LODs
  // what this means is that at distance 2^(-TerrainLODBias) there will be the maximum LOD,
  // so at 2048m (TerrainLODBias==-11) we want to keep the old LOD number 2.
#else
  float maxLod = NLods()-1;
#endif
  const float invLog2 = 1.4426950409f;
  return maxLod + TerrainLODBias + log(minDist)*invLog2 + l->GetLodBias();
}

inline float DistFromInterval(float c, float cMin, float cMax)
{
  if (c<cMin) return cMin-c;
  if (c>cMax) return c-cMax;
  return 0;
}

float LandCache::MinDistRect(Landscape *l, Vector3Par pos, float minZ, const GridRectangle &sRect) const
{
  // for distance estimation use X/Z distance only
  // if inside of the rectangle, distance needs to be 0
  float landGrid = l->GetLandGrid();
  Vector3 c00(sRect.xBeg*landGrid,0,sRect.zBeg*landGrid);
  Vector3 c11(sRect.xEnd*landGrid,0,sRect.zEnd*landGrid);

  float minDist = 0;
  float xDist = DistFromInterval(pos.X(),c00.X(),c11.X());
  float zDist = DistFromInterval(pos.Z(),c00.Z(),c11.Z());

  if (xDist>0 || zDist>0)
  {
    float dist2 = Square(xDist)+Square(zDist);
    minDist = dist2*InvSqrt(dist2);
  }

  if (minDist<minZ) minDist = minZ;
  return minDist;
}

void LandCache::Init(Landscape *land, bool forceProcessing)
{
  int oldSize = _landSegmentSize;
  int oldAlign = _landSegmentAlign;

  CalculateLandSegmentSize(land);
  _maxN = CalculateCacheSize(land,true,_landSegmentSize*land->GetLandGrid());
  _maxWaterN = CalculateCacheSize(land,false,_waterSegmentSize*WaterGrid);

  
  // we do not want to process again if the information is still valid
  if (
    _satSegmentSize>1 && (
      oldSize!=_landSegmentSize || oldAlign!=_landSegmentAlign || forceProcessing
    )
  )
  {
    if (!_singleSatLandSegment && _satSegmentSize>1)
    {
      LogF(
        "Warning: land segment (%d) not fraction of satellite segment (%d)in %s",
        _landSegmentSize,_satSegmentSize,cc_cast(land->GetName())
      );
      _landSeg.Clear();
      _landSegZStart = 0;
    }
    else
    {
      ProcessLandSegInfo(land);
    }
    land->InitSegmentMinMax();
  }
}

void LandCache::PreloadSegAt( int xxx, int zzz, Landscape * land, Vector3Par pos )
{
  PROFILE_SCOPE_EX(preSA,pre);
  GridRectangle rect;
  rect.xBeg=xxx;
  rect.zBeg=zzz;
  rect.xEnd=xxx+_landSegmentSize;
  rect.zEnd=zzz+_landSegmentSize;
#if LOG_SEG>1
  LogF("Fill land %d,%d",xxx,zzz);
#endif

  // select which LOD is appropriate for that field
  const float minDist = 1.0f;
  float dist = MinDistRect(land,pos,minDist,rect);

  float lod = LandLODWanted(land,dist);

  int lod0 = toIntFloor(lod);
  int lod1 = lod0+1;
  saturate(lod0,0,NLods()-1);
  saturate(lod1,0,NLods()-1);

  // generate the two LODs around
  GEventLogger.Add(rect,"PreloadSegAt");
  Ref<LandSegment> seg0 = Segment(land,rect,lod0);
  Ref<LandSegment> seg1 = Segment(land,rect,lod1);      
  Assert(seg0==seg1); // different lods are still the same LandSegments

  // generate the roads as well (if needed)
  seg0->GenerateRoad(lod0, dist, land);
  if (lod1!=lod0) seg1->GenerateRoad(lod1, dist, land);
}

void Landscape::GetTerrainPreloadGrid(Vector3Par pos, float maxRange, GridRectangle &aroundRect)
{
  float dist = floatMin(maxRange,GScene->GetFogMaxRange());
  float iGrid = GetInvLandGrid();
  aroundRect.xBeg = toIntFloor((pos.X()-dist)*iGrid);
  aroundRect.zBeg = toIntFloor((pos.Z()-dist)*iGrid);
  aroundRect.xEnd = toIntCeil((pos.X()+dist)*iGrid);
  aroundRect.zEnd = toIntCeil((pos.Z()+dist)*iGrid);

  int landSegmentSize = _segCache->_landSegmentSize;
  int landSegmentAlign = _segCache->_landSegmentAlign;
  aroundRect.xBeg = RoundToSegment(aroundRect.xBeg,landSegmentSize,0);
  aroundRect.zBeg = RoundToSegment(aroundRect.zBeg,landSegmentSize,landSegmentAlign);
  aroundRect.xEnd = RoundToSegment(aroundRect.xEnd+landSegmentSize-1,landSegmentSize,0);
  aroundRect.zEnd = RoundToSegment(aroundRect.zEnd+landSegmentSize-1,landSegmentSize,landSegmentAlign);
}

void Landscape::PreloadTerrain(int x, int z, Vector3Par pos)
{
  _segCache->PreloadSegAt(x,z, this, pos);
}

void LandCache::Fill(Landscape *land, const Frame &pos)
{
#if _VBS3_LAND_SHADOW
  GScene->CalculateShadowBigRect();
#endif

  // recalculate necessary space  - view distance might change
  _maxN = CalculateCacheSize(land,true,_landSegmentSize*land->GetLandGrid());
  _maxWaterN = CalculateCacheSize(land,false,_waterSegmentSize*WaterGrid);

  //LogF("LandCache fill: start %d",_segments.Size());
  // pre-build as many cache segments as possible
  int x=toIntFloor(pos.Position().X()*land->GetInvLandGrid());
  int z=toIntFloor(pos.Position().Z()*land->GetInvLandGrid());
  float range = Glob.config.horizontZ;
  int maxRange = toIntCeil(range*InvLandGrid*land->GetInvLandSegmentSize());
  if (maxRange<2) maxRange=2;
  LogF("LandCache fill: range %d, pos %.1f,%.1f",maxRange,pos.Position().X(),pos.Position().Z());
  // align to segment size
  int xseg = RoundToSegment(x,_landSegmentSize,0);
  int zseg = RoundToSegment(z,_landSegmentSize,_landSegmentAlign);
  int count = 0;
  for( int range=0; range<=maxRange; range++ )
  {
    extern size_t systemFreeRequired;
    extern size_t mainHeapFreeRequired;
    

    int regular = 0;    
    // traverse from near to far (growing rectangle)
    for( int xx=-range; xx<=range; xx++ )
    for( int zz=-range; zz<=range; zz++ )
    if( xx==-range || xx==+range || zz==-range || zz==+range )
    {
      int xxx = xseg+xx*_landSegmentSize;
      int zzz = zseg+zz*_landSegmentSize;

#if _VBS3_NO_SEABED
      if (land->IsSeabedSegment(xxx, zzz)) continue;
#endif
      
      //if (regular++>=5)
      {
        regular = 0;
        // loading the terrain may be quite memory demanding and time consuming
        // perform memory management
        FreeOnDemandGarbageCollectMain(mainHeapFreeRequired,systemFreeRequired);
        // and give debuggers and progress bars opportunity to refresh
        I_AM_ALIVE();
        if (GProgress) GProgress->Refresh();
      }
      
      PreloadSegAt(xxx, zzz, land, pos.Position());

      // break if cache is full
      if (++count>=_maxN)
      {
        #if LOG_SEG
          LogF("Cache full, range %d of %d",range,maxRange);
        #endif
        break;
      }
    }
  }
  // TODO: fill water cache as well
  LogF("LandCache fill: end %d (of %d)",_segments.Size(),_maxN);
}

void LandSegment::GenerateRoad(int lod, float dist, Landscape *land)
{
  LandSegment::SegLod &segLod = _lod[lod];
  if (dist<Glob.config.roadsZ && CheckDrawRoads())
  {
    Shape &roads = segLod._roads;
    // the IsGeometryLoaded test is here to eliminate segments which are already done but have no roads in them
    // when roads are generated non-empty, the _nVertex is set to >0 and never reset back
    if (!segLod._roadsDone || !roads.GetVertexBuffer() && !roads.IsGeometryLoaded())
    {
      land->GenerateRoadSegment(_rect,this,lod);
    }
  }
}

/**
@param x coordinate (n LandGrid)
@param z coordinate (n LandGrid)
*/
void LandCache::ForceUpdate(int x, int z)
{
  // find the item
  GridRectangle rect;

  rect.xBeg = RoundToSegment(x,_landSegmentSize,0);
  rect.zBeg = RoundToSegment(z,_landSegmentSize,_landSegmentAlign);
  rect.xEnd = rect.xBeg + _landSegmentSize;
  rect.zEnd = rect.zBeg + _landSegmentSize;

  const Ref<LandSegment> &seg = _index.Get(rect);
  if (_index.NotNull(seg))
  {
    Delete(seg);
  }
}


static int CmpALight
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


static int SortByTextureR( const PolyProperties &p0, const PolyProperties &p1 )
{
  return ComparePointerAddresses(p0.GetTexture(),p1.GetTexture());
}

const int MaxVertexCanditates = 6; // vertex may be shared between six faces
class VertexCandidates: public VerySmallArray<int,sizeof(int)*8>
{
  public:
  void AddUnique(int src)
  {
  #if _DEBUG
    for( int i=0; i<Size(); i++ )
    {
      if( (*this)[i]==src )
      {
        Fail("Index not unique");
        return;
      }
    }
  #endif
    Add(src);
  }
  int FindOrAdd(int src);
  int Find(int src) const;
};

int VertexCandidates::FindOrAdd(int src)
{
  for( int i=0; i<Size(); i++ ) if( (*this)[i]==src ) return i;
  return Add(src);
}

int VertexCandidates::Find(int src) const
{
  for( int i=0; i<Size(); i++ ) if( (*this)[i]==src ) return i;
  return -1;
}

#define LOG_SHARING 0

//! bilinear interpolation, yXZ, xf and zf = <0,1>

static __forceinline float Bilint
(
  float y00, float y01, float y10, float y11,
  float xf, float zf
)
{
//  float y0z = y00*(1-zf) + y01*zf;
//  float y1z = y10*(1-zf) + y11*zf;
//  return y0z*(1-xf) + y1z*xf;
  return y00*(1-zf)*(1-xf) + (y01*zf)*(1-xf) + y10*(1-zf)*xf + y11*zf*xf;
}

static Vector3 WaterNormal(0,-1,0);

/*!
\patch 2.01 Date 12/16/2002 by Ondra
- Fixed: Water textures were not moving with HW T&L.
*/

const int WaterFlags = IsAlpha;

const int GroundFlagsPass1Lerp = IsAlpha|NoAlphaWrite|NoZWrite;

/*!
\patch 1.43 Date 1/30/2002 by Ondra
- Fixed: Great Monolith gradually growing out of the water after mission reload near A1 grid.
(Thanks to WKK Gimbal for discovering when this bug happens.)
\patch 1.45 Date 2/25/2002 by Ondra
- Removed: SW T&L terrain color randomization removed.
This makes terrain memory usage lower.
Note: This feature was never present in HW T&L
\patch_internal 1.45 Date 2/25/2002 by Ondra
- Optimized: Terrain segment creation optimized,
useful for high resolution terrain.
\patch 1.50 Date 4/2/2002 by Ondra
- Fixed: Water surface normals were reversed.
As a result, water was drawn too dark.
*/

void Landscape::GenerateWaterSegment(int lod)
{
      
  // we have to reconstruct cache
  
  // calculate counts for subdivision of very big landscape squares
  // water subdivision is always the same
  
  // if the view distance is high, reduce water tessellation
  // maintain count of water faces approximately constant
  // 
  // nFaces = view_distance^2/waterGrid^2
  // waterGrid = sqrt(view_distance^2/nFaces)
  // nFaces ~ 20K seems reasonable for Xbox
  
  // 10 is empirical constant, to achieve 30000 from 300000
  int nFacesWanted = toInt(GScene->GetComplexityTarget()/20);
  // ???? upper limit ????
  saturate(nFacesWanted,10000,300000);
  
  
  // estimate how much segments will be visible at one time
  // LODs should make large view-distances viable even with fine near mesh
  
  const float viewDistance = MaxWaterLOD>=2 ? 800 : MaxWaterLOD>0 ? 1200 : Glob.config.horizontZ;
  
  const int waterSegmentSize = GetWaterSegmentSize();
  const int rangeSegments = toIntCeil(viewDistance/(WaterGrid*waterSegmentSize));
    
  const int nSegments = rangeSegments*rangeSegments;
  const float facesPerSegment = float(nFacesWanted)/nSegments;
  
  const int gridsPerSegment = waterSegmentSize*waterSegmentSize;
  const float facesPerGridWanted = facesPerSegment/gridsPerSegment;
  
  // square root of the aspect - applied to both, giving aspect as a result
  //const float aspectXToZ2 = 0.71f;
  // as wave direction is arbitrary, we have no a priory knowledge of it
  // subdivision needs to be symmetric
  
  // facesPerGrid = 
  const float subdivisionCountWanted = sqrt(facesPerGridWanted);
  
  int subdivisionCount = toInt(subdivisionCountWanted);
#if _VBS3_LARGE_WATERSEG
  if ((waterSegmentSize & (waterSegmentSize-1)) && (subdivisionCount & 1))
    subdivisionCount--; // we don't want odd subdivision when segment size is not a power of 2
#endif
  //int subdivisionCountX = toInt(subdivisionCountWanted/aspectXToZ2);
  //int subdivisionCountZ = toInt(subdivisionCountWanted*aspectXToZ2);
  // 1 is natural limit, but why upper limit?
  saturate(subdivisionCount,1,16);
  
  // when more LODs wanted then waterSegmentSize can support, we may need to keep the subdivision
  // a multiply of power of 2
  
  int alignNeeded = (1<<MaxWaterLOD)/waterSegmentSize;
  // alignNeeded may be smaller than 1, in which case we need to skip the alignment computation
  if (alignNeeded>1)
  {
    // round down
    subdivisionCount &= ~(alignNeeded-1);
    // avoid result zero, which could be achieved by rounding down
    if (subdivisionCount<alignNeeded) subdivisionCount = alignNeeded;
  }
  
  const int waterSegmentSizeSubd = (waterSegmentSize*subdivisionCount)>>lod;
  const float invSubdivisionCount = 1.0f/subdivisionCount*(1<<lod);

  const int xCountSD = waterSegmentSizeSubd+1;
  const int zCountSD = waterSegmentSizeSubd+1;

  const int nVertices=zCountSD*xCountSD;
  
  Shape &shape = _segCache->_wTable[lod];
  // if the mesh is already prepared, do not generate it once more
  if (shape.NVertex()==nVertices && shape.LockVertexBuffer())
  {
    GEngine->RefreshBuffer(shape);
    return;
  }

  const float waterGridX = WaterGrid*invSubdivisionCount;
  const float waterGridZ = WaterGrid*invSubdivisionCount;
  #if _PROFILE || _DEBUG
  LogF(
    "Water subdiv %dx%x, wanted %.2fx%.2f, nVertices %d",
    subdivisionCount,subdivisionCount,
    subdivisionCountWanted,subdivisionCountWanted,
    nVertices
  );
  #endif
  
  PROFILE_SCOPE_EX(lMshW,land);
  GridRectangle rect;
  rect.xBeg = 0;
  rect.xEnd = waterSegmentSize;
  rect.zBeg = 0;
  rect.zEnd = waterSegmentSize;
  
  GridRectangle rectSD;
  rectSD.xBeg = 0;
  rectSD.xEnd = waterSegmentSizeSubd;
  rectSD.zBeg = 0;
  rectSD.zEnd = waterSegmentSizeSubd;
  
  
  Assert( xCountSD==rectSD.xEnd-rectSD.xBeg+1 );
  Assert( zCountSD==rectSD.zEnd-rectSD.zBeg+1 );

  float offsetX = (rectSD.xBeg+rectSD.xEnd)*0.5f;
  float offsetZ = (rectSD.zBeg+rectSD.zEnd)*0.5f;

  Vector3 originalS(1, 0, 0);
  Vector3 originalT(0, 0, 1);
  //Vector3 originalSAndT(1, 0, 1);

  shape.Clear();
  shape.Reserve(nVertices);
  shape.ReserveST(nVertices);
  //shape.ReserveAlpha(nVertices);

  // UV range - 0 .. waterSegmentSizeSubd * invSubdivisionCount
  // waterSegmentSizeSubd * invSubdivisionCount
  float uvRange = waterSegmentSizeSubd * invSubdivisionCount;
  UVPair minUV(0, 0), maxUV(uvRange, uvRange), invUV;
  shape.InitUV(invUV, minUV, maxUV);

  {
    
    // subdivided water surface
    const int xn = waterSegmentSizeSubd+1;
    const int zn = waterSegmentSizeSubd+1;
    // create position and vertices
    for (int zz=0; zz<zn; zz++) for (int xx=0; xx<xn; xx++)
    {
      Vector3 wPos = Vector3(
        waterGridX*(xx+rectSD.xBeg - offsetX),
        0,
        waterGridZ*(zz+rectSD.zBeg - offsetZ)
      );
      const float stretch = +0.01f;
      if (zz==0) wPos[2] -= stretch;
      if (zz==zn-1) wPos[2] += stretch;
      if (xx==0) wPos[0] -= stretch;
      if (xx==xn-1) wPos[0] += stretch;


      const float u = xx*invSubdivisionCount, v=zz*invSubdivisionCount;

      // See label WATER_UV
      float uu = u;
      float vv = v;
      
      int v0=shape.AddVertex(wPos,WaterNormal,ClipAll,invUV, uu, vv,NULL,STPair(originalS,originalT));
      (void)v0;
      Assert(v0==zz*xCountSD+xx);

    }
    
  }
  
  // note: every vertex in mesh is initialized
  //AUTO_STATIC_ARRAY(VertexCandidates,wCandidates,maxNVertices);
  
  //const float uvWScale = 0.15f;
  
  const int zn = waterSegmentSizeSubd;
  const int xn = waterSegmentSizeSubd;

  int wFaces=zn*xn*2;

  shape.ReserveFaces(wFaces);

  // add water faces
  Poly water;
  water.Init();
  water.SetN(3);

  for( int zz=0; zz<zn; zz++ ) for( int xx=0; xx<xn; xx++ )
  {
    // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
    const int xz = xx + zz*xCountSD;
    // water polygon

    const int vo00=xz+0+0;
    const int vo10=xz+0+1;
    const int vo01=xz+xCountSD+0;
    const int vo11=xz+xCountSD+1;

    // alternate triangulation direction
    if ((xx+zz)&1)
    {
      // Create the first face
      water.Set(0,vo10);
      water.Set(1,vo00);
      water.Set(2,vo01);
      shape.AddFace(water);

      // Create the second face
      water.Set(1,vo01);
      water.Set(2,vo11);
      shape.AddFace(water);
    }
    else
    {
      // Create the first face
      water.Set(0,vo11);
      water.Set(1,vo00);
      water.Set(2,vo01);
      shape.AddFace(water);

      // Create the second face
      water.Set(1,vo10);
      water.Set(2,vo00);
      shape.AddFace(water);
    }
  }
  // we know we are using ClipAll everywhere on the landscape
  shape.SetClipAll(ClipAll);
  // vertices should be shared .. check if it is true
  Assert( shape.NVertex()==nVertices );
  Assert( shape.NFaces()==wFaces );
  //Assert( seg->_wFaces.Size()<=wFaces );
  //_segCache->_wFaces.Compact();
  shape.Compact();
  shape.CalculateMinMax();
  //LogF("CWater %x",seg);

  // Update the sea material - set position of the UV transform to 0, set 3 uv sources
  Matrix4 &m = _seaMaterial->_texGen[1]._uvTransform;
  m.SetPosition(Vector3(0, 0, 0));
  _seaMaterial->_texGen[1]._uvSource = UVTexWaterAnim;
  _seaMaterial->_texGen[2]._uvSource = UVTexWaterAnim;
  _seaMaterial->_texGen[3]._uvSource = UVTexWaterAnim;

  ShapeSectionInfo waterProp;
  waterProp.Init();
  //waterProp.SetSpecial(WaterFlags|ClampV|FilterAnizotrop|NoBackfaceCull);
  waterProp.SetSpecial(WaterFlags|ClampV|FilterTrilinear|NoBackfaceCull);
  waterProp.SetTexture(GetSeaTexture());
  waterProp.SetAreaOverTex(0, WaterGrid*WaterGrid);
  waterProp.SetMaterialExt(_seaMaterial);

  shape.SetAsOneSection(waterProp);
  shape.DisablePushBuffer();


  #if LOG_SHARING
    int maxV = shape.NFaces()*3;
    int actV = shape.NVertex();
    LogF("Water (deep) sharing ratio is %f",actV*1.0f/maxV);
  #endif
  shape.ConvertToVBuffer(VBStaticReused);
}

#if _VBS3_NO_SEABED
void Landscape::ResetSeabedInfo()
{
  int segsize = _segCache->_landSegmentSize;
  int n = (GetLandRange() + segsize-1) / segsize;

  // reset the array of flags
  _segSeabed.Dim(n+2, n+2);
  for (int i = 0; i < n+2; i++)
    for (int j = 0; j < n+2; j++)
      _segSeabed(i, j) = -1;
}

bool Landscape::IsSeabedSegment(int xBeg, int zBeg)
{
  // first try to return cached result
  int segsize = _segCache->_landSegmentSize;
  int zBeg2 = zBeg - _segCache->_landSegmentAlign;
  int x = xBeg / segsize;
  int z = zBeg2 / segsize;
  if (xBeg >= 0) x++;
  if (zBeg2 >= 0) z++;

  if(x < 0 || x > _segSeabed.GetXRange()-1) return false;
  if(z < 0 || z > _segSeabed.GetYRange()-1) return false;

  saturate(x, 0, _segSeabed.GetXRange()-1);
  saturate(z, 0, _segSeabed.GetYRange()-1);
  char flag = _segSeabed(x, z);
  if (flag >= 0) return flag != 0;

  // determine the depth of the segment

  int xEnd = xBeg + segsize;
  int zEnd = zBeg + segsize;

  int subdiv = 1 << (GetTerrainRangeLog() - GetLandRangeLog());
  xBeg *= subdiv;  xEnd *= subdiv;
  zBeg *= subdiv;  zEnd *= subdiv;

  saturate(xBeg, 0, GetTerrainRange()-1);
  saturate(zBeg, 0, GetTerrainRange()-1);
  saturate(xEnd, 1, GetTerrainRange());
  saturate(zEnd, 1, GetTerrainRange());

  const float threshold = _seaLevelOffset - 20.0;
  bool deep = true;
  for (int xx = xBeg; xx < xEnd; xx++)
    for (int zz = zBeg; zz < zEnd; zz++)
      if (_terrain->GetData(xx, zz) > threshold)
      { deep = false; goto done; }

done:
      _segSeabed(x, z) = deep ? 1 : 0;
      return deep;
}
#endif


Vector3 Landscape::ClippedNormal(int x, int z) const
{
  Coord xDelta,zDelta;
  if (land_TerrainInRange(x+1,z+1) && land_TerrainInRange(x-1,z-1))
  {
    // no clipping test needed - we know we are inside
    xDelta = _terrain->GetData(x+1,z)-_terrain->GetData(x-1,z);
    zDelta = _terrain->GetData(x,z+1)-_terrain->GetData(x,z-1);
  }
  else
  {
    xDelta = _terrain->ClippedDataXZ(x+1,z)-_terrain->ClippedDataXZ(x-1,z);
    zDelta = _terrain->ClippedDataXZ(x,z+1)-_terrain->ClippedDataXZ(x,z-1);
  }
  /*
  Vector3 offX(GetTerrainGrid(), xDelta, 0);
  Vector3 offZ(0, zDelta, GetTerrainGrid());
  Vector3Val cp = offX.CrossProduct(offZ);
  */
  /// cross product optimized:
  /*
  float x = xDelta*GetTerrainGrid();
  float y = -GetTerrainGrid()*GetTerrainGrid();
  float z = GetTerrainGrid()*zDelta;
  */
  // normalization will ignore *GetTerrainGrid() done on all members
  Vector3 cp(xDelta,-GetTerrainGrid(),zDelta);
  return cp.Normalized();
}


/**
Function to combine multiple terrain based materials into one
*/
static void TerrainCombine(TexMaterial *tgt, const TexMaterial *src)
{
  Assert(src->_stage[LandMatStageSat]._tex==tgt->_stage[LandMatStageSat]._tex);
  Assert(src->_stage[LandMatStageMask]._tex==tgt->_stage[LandMatStageMask]._tex);

  int maxLayerCount = LandMatLayerCount;
  if (src->GetPixelShaderID()==PSTerrainSNX)
    maxLayerCount--; //last layer is used for satellite texture

  int srcMask = GetLayerMask(src);
  int tgtMask = GetLayerMask(tgt);
  int mask = srcMask|tgtMask;
  // if no new stage is used, there is no need to update anything
  if (mask==tgtMask)
  {
    // we still may need to fill in stages which were null
    if ((src->GetPixelShaderID()==PSTerrainX && tgt->GetPixelShaderID()==PSTerrainX)
      || (src->GetPixelShaderID()==PSTerrainSNX && tgt->GetPixelShaderID()==PSTerrainSNX) )
    {
      for (int i=0; i<maxLayerCount; i++)
      {
        int base = LandMatStageLayers+i*LandMatStagePerLayer;
        if (tgt->_stage[base+LandMatNoInStage]._tex.IsNull())
        {
          Assert(tgt->_stage[base+LandMatDtInStage]._tex.IsNull());
          tgt->_stage[base+LandMatNoInStage] = src->_stage[base+LandMatNoInStage];
          tgt->_stage[base+LandMatDtInStage] = src->_stage[base+LandMatDtInStage];
          // if the stage was missing completely in the material, extend it as needed
          if (tgt->_stageCount<base+LandMatStagePerLayer-1) tgt->_stageCount = base+LandMatStagePerLayer-1;
        }
      }
    }
    // we need to merge the custom int holding layer mask as well
    tgt->SetCustomInt( src->GetCustomInt()|tgt->GetCustomInt() );
    return;
  }
  // merge stages which were not covered before
  // note: stage arrangement is different based on mask
  // while merging we need to handle this
  TexStageInfo oldStage[TexMaterial::NStages];
  for (int i=0; i<tgt->_stageCount+1; i++)
  {
    oldStage[i] = tgt->_stage[i];
  }
  // we always want to keep first two stages (sat+mask)
  int tgtStageI = LandMatStageLayers;
  int oldStageI = LandMatStageLayers;
  int srcStageI = LandMatStageLayers;
  for (int i=0; i<maxLayerCount; i++)
  {
    int stageMask = 1<<i;
    // copy stage from any source which contains it
    if (tgtMask&stageMask)
    {
      for (int j=0; j<LandMatStagePerLayer; j++)
      {
        tgt->_stage[tgtStageI++] = oldStage[oldStageI++]; // prim. texture
      }
    }
    else if (srcMask&stageMask)
    {
      for (int j=0; j<LandMatStagePerLayer; j++)
      {
        tgt->_stage[tgtStageI++] = src->_stage[srcStageI]; // prim. texture
      }
    }
    // skip what is needed to be skipped in the source
    // this cannot be moved into the condition above, as it can happen when first if is true
    if (srcMask&stageMask) srcStageI += LandMatStagePerLayer;
    // texgen is always ready
  }
  // increase stage count if needed (and it should be needed)
  Assert(tgt->_stageCount+1<tgtStageI);
  if (tgt->_stageCount+1<tgtStageI) tgt->_stageCount = tgtStageI-1;
  // change pixel shader ids
  Fail("Stage count changing no longer supported");
  PixelShaderID ps0 = PixelShaderID(PSTerrain1-1+mask);
  tgt->SetPixelShaderID(ps0);
}

/**
@param array of results, dimension LandSegment::NLods
@param lod first lod we are interested in
*/
void Landscape::VertexLodHeights(float *res, int x, int z, int lod) const
{
  if (lod==0)
  {
    // LOD 0 handling can be very simple
    res[0] = _terrain->ClippedDataXZ(x,z);
  }
  // for other LODs
  int start = intMax(lod,1);
  for (int l=start; l<LandSegment::NLods; l++)
  {
    // TODO: consider performing this in one x/z loop for each lod
    // or even better, store it permanently in the Landscape
    // simulate other LODs
    int lodStep = (1<<l);
    int lodMask = (1<<l)-1;
    int x0 = x&~lodMask;
    int z0 = z&~lodMask;
    float y00,y01,y11,y10;
    float invLodMask = 1/float(lodStep);
    float xf = (x&lodMask)*invLodMask;
    float zf = (z&lodMask)*invLodMask;
    _terrain->GetRectY(x0,z0,lodStep,lodStep,y00,y01,y10,y11);
    res[l] = RectBilint(y00,y01,y10,y11,xf,zf);
  }
  // for all lods below lod copy lod
  for (int i=0; i<lod; i++)
  {
    res[i] = res[lod];
  }
}


void Landscape::InitSegmentMinMax()
{
  // compute how many segments do we render
  const int subdivisionLevel = GetTerrainRangeLog()-GetLandRangeLog();
  const int subdivisionCount = 1<<subdivisionLevel;
  int landSegSize = GetLandSegmentSize()*subdivisionCount;
  int landSegAlign = GetLandSegmentAlign()*subdivisionCount;
  
  int xRange = (GetTerrainRange()+landSegSize-1)/landSegSize;
  
  int zStart = RoundToSegment(0,landSegSize,landSegAlign);
  int zEnd = RoundToSegment(GetTerrainRange(),landSegSize,landSegAlign);
  if (zEnd<GetTerrainRange()) zEnd += landSegSize;
  
  int zRange = (zEnd-zStart)/landSegSize;
  
  _yRange.Dim(xRange,zRange);
  for (int zr=0; zr<zRange; zr++)
  for (int xr=0; xr<xRange; xr++)
  {
    GridRectangle rectSD;
    rectSD.xBeg = xr*landSegSize;
    rectSD.zBeg = zr*landSegSize+zStart;
    rectSD.xEnd = rectSD.xBeg+landSegSize;
    rectSD.zEnd = rectSD.zBeg+landSegSize;

    float minY = FLT_MAX, maxY = -FLT_MAX;
    for(int z=rectSD.zBeg; z<=rectSD.zEnd; z++)
    for(int x=rectSD.xBeg; x<=rectSD.xEnd; x++)
    {
      float y = _terrain->ClippedDataXZ(x,z);
      saturateMin(minY,y);
      saturateMax(maxY,y);
    }
    _yRange(xr,zr).minY = minY;
    _yRange(xr,zr).maxY = maxY;
  }
}

void LandCache::ProcessLandSegInfo(Landscape *l)
{
  // scan every land. segment and create a common material for it
  int landRange = l->GetLandRange();
  float landGrid = l->GetLandGrid();
  
  int zStart = _landSegmentAlign-_landSegmentSize;
  if (zStart<=-_landSegmentSize) zStart += _landSegmentSize;
  _landSegZStart = zStart;
  
  int zCount = (landRange+_landSegmentSize-1-zStart)/_landSegmentSize;
  int xCount = (landRange+_landSegmentSize-1)/_landSegmentSize;
  _landSeg.Dim(xCount,zCount);
  for (int z=zStart,zc=0; z<landRange; z+=_landSegmentSize,zc++)
  {
    // note: the rectangle may be partially out of range
    DoAssert(RoundToSegment(z,_landSegmentSize,_landSegmentAlign)==z);
    int zEnd = z+_landSegmentSize;


    for (int x=0,xc=0; x<landRange; x+=_landSegmentSize,xc++)
    {
      
      DoAssert(RoundToSegment(x,_landSegmentSize,0)==x);
      int xEnd = x+_landSegmentSize;

      Vector3 offset = Vector3(
        landGrid*0.5f*(x+xEnd),
        0,
        landGrid*0.5f*(z+zEnd)
      );

      Ref<TexMaterial> combined;
      Ref<TexMaterialType> type = new TexMaterialTypeSimpleTerrain(TexMaterialName::_defaultType,offset);
      if (l->GridInRange(x,z) && l->GridInRange(xEnd-1,zEnd-1))
      {
        // merge sections sharing the same satellite map texture

        // scan TextureInfo in the grid
        // we already know it shares the sat. map
        for (int zz=z; zz<zEnd; zz++)
        for (int xx=x; xx<xEnd; xx++)
        {
          if (zz<0 || zz>=landRange) continue;
          if (xx<0 || xx>=landRange) continue;
          const Scenery::TextureInfo &tex = l->ClippedTextureInfo(zz,xx);
          const TexMaterial *mat = tex._mat;
          if (mat && (mat->GetPixelShaderID()==PSTerrainX || mat->GetPixelShaderID()==PSTerrainSNX))
          {
            // materials in multiple sections can differ by name only
            // like p_031-011_l00_l01_l03_n.rvmat
            // we want all materials with identical sat. map to be considered the same one here
            // the material is never shared between segments, as it contains segment offset
            // therefore we as a segment are free to manipulate it
            // reloading it will not work, though
            if (!combined)
            {
              combined = GTexMaterialBank.NewDerived(mat,type);
            }
            else
            {
              TerrainCombine(combined,mat);
            }
            // new section should contain all faces from the old one
            // by default new sections are hidden
          }
        }
      }
      else
      {
        TexMaterial *mat = l->OutsideTextureInfo()._mat;
        combined = mat ? GTexMaterialBank.NewDerived(mat,type) : NULL;
      }

      
      _landSeg(xc,zc)._mat = combined;
    }
  }
}

#if _ENABLE_CHEATS
  static bool DrawGrassLayer = false;
#else
  const bool DrawGrassLayer = false;
#endif

bool Landscape::PreloadData(Vector3Par pos, float maxPreloadDist, float maxTime, float degrade,
  Object *doNotPreload, bool alwaysRecreate, float autoTimeOut, bool logging,
  const Vector3 *dir, float fov)
{
  return PreloadData(DrawGrassLayer,pos,maxPreloadDist,maxTime,degrade,doNotPreload,alwaysRecreate,autoTimeOut,logging,dir,fov);
}

/// switching between old and new roads maintenance (in segments or as separate shapes)
bool Landscape::_roadsIntoSegment = true;

/// grass by terrain approximation
static float GrassHeightFactor = 1.0f;

static float NoGrassHeight = -0.10f;

 // local data enough for 32 with borders, which is segment on Chernarus

typedef Array2D<float, MemAllocLocal<float, 64*4 * 64*4, AllocAlign16> > YDataType;

static inline Vector3 ClippedNormalFromArray(const YDataType &yData, int x, int z, float terrainGrid)
{
  // based on Landscape::ClippedNormal
  // no clipping test needed - we know we are inside
  Coord xDelta = yData(x+1,z)-yData(x-1,z);
  Coord zDelta = yData(x,z+1)-yData(x,z-1);
  // normalization will ignore *GetTerrainGrid() done on all members
  Vector3 cp(xDelta,-terrainGrid,zDelta);
  return cp.Normalized();
}

/// get four data points from the grid, bigger size allowed
static inline void GetRectY(const YDataType &yData, int x, int z, int xStep, int zStep, float &y00, float &y01, float &y10, float &y11)
{
  // temporary variables so that compiler can assume no aliasing when writing to them
  float t00 = yData(x, z);
  float t10 = yData(x+xStep, z);
  float t01 = yData(x, z+zStep);
  float t11 = yData(x+xStep, z+zStep);
  y00 = t00;
  y01 = t01;
  y10 = t10;
  y11 = t11;
}

/**
@param array of results, dimension LandSegment::NLods
@param lod first lod we are interested in
*/
static void VertexLodHeightsFromArray(float *res, const YDataType &yData, int x, int z, int lod)
{
  if (lod==0)
  {
    // LOD 0 handling can be very simple
    res[0] = yData(x,z);
  }
  // for other LODs
  int start = intMax(lod,1);
  for (int l=start; l<LandSegment::NLods; l++)
  {
    // TODO: consider performing this in one x/z loop for each lod
    // or even better, store it permanently in the Landscape
    // simulate other LODs
    int lodStep = (1<<l);
    int lodMask = (1<<l)-1;
    int x0 = x&~lodMask;
    int z0 = z&~lodMask;
    float y00,y01,y11,y10;
    float invLodMask = 1/float(lodStep);
    float xf = (x&lodMask)*invLodMask;
    float zf = (z&lodMask)*invLodMask;
#if _VBS3_MORETERRAINLODS
    //hot fix - with large lods, ydata is to small
    int lodStepX = x0+lodStep >= yData.GetXRange() ? yData.GetXRange() - x0 -1 : lodStep;
    int lodStepZ = z0+lodStep >= yData.GetYRange() ? yData.GetYRange() - z0 -1 : lodStep;
    GetRectY(yData,x0,z0,lodStepX,lodStepZ,y00,y01,y10,y11);
#else
    GetRectY(yData,x0,z0,lodStep,lodStep,y00,y01,y10,y11);
#endif
    res[l] = Landscape::RectBilint(y00,y01,y10,y11,xf,zf);
  }
  // for all lods below lod copy lod
  for (int i=0; i<lod; i++)
  {
    res[i] = res[lod];
  }
}

const float TerrainYSeam = -2.0f;
/**
@param rect landgrid coordindates
*/
void Landscape::GetSegmentMinMaxBox(const GridRectangle &rect, Vector3 *minmax)
{
  PROFILE_SCOPE_EX(lGSMM,terr);

  const int subdivisionLevel = GetTerrainRangeLog()-GetLandRangeLog();
  const int subdivisionCount = 1<<subdivisionLevel;
  
  GridRectangle rectSD; // terrain grid (subdivided) coordinates
  rectSD.xBeg = rect.xBeg*subdivisionCount;
  rectSD.xEnd = rect.xEnd*subdivisionCount;
  rectSD.zBeg = rect.zBeg*subdivisionCount;
  rectSD.zEnd = rect.zEnd*subdivisionCount;
  
  int landSegSize = GetLandSegmentSize();
  int landSegAlign = GetLandSegmentAlign();
  
  // rectangle is segment aligned
  int zStart = RoundToSegment(0,landSegSize,landSegAlign);

  int xr = rect.xBeg/landSegSize;Assert(xr*landSegSize==rect.xBeg);
  int zr = (rect.zBeg-zStart)/landSegSize;Assert(zr*landSegSize+zStart==rect.zBeg);
  
  float minY = FLT_MAX, maxY = -FLT_MAX;
  if (xr>=0 && xr<_yRange.GetXRange() && zr>=0 && zr<_yRange.GetYRange())
  {
    // calculate normals for all basic vertices
    saturateMin(minY,_yRange(xr,zr).minY);
    saturateMax(maxY,_yRange(xr,zr).maxY);
  }
  // when we are out (even partially), we need to pre-generate sampled data for the mesh when using TerSynth
  if ((!land_InRange(rectSD.zBeg,rectSD.xBeg) || !land_InRange(rectSD.zEnd,rectSD.xEnd)))
  {
    if (_terrain->_outsideData.NotNull())
    {
      // TODO: use one call only
      float minH = _terrain->_outsideData->GetMinHeight(rectSD.xBeg,rectSD.zBeg,rectSD.xEnd,rectSD.zEnd);
      float maxH = _terrain->_outsideData->GetMaxHeight(rectSD.xBeg,rectSD.zBeg,rectSD.xEnd,rectSD.zEnd);
      saturateMin(minY,minH);
      saturateMax(maxY,maxH);
    }
    else
    {
      saturate(xr,0,_yRange.GetXRange()-1);
      saturate(zr,0,_yRange.GetYRange()-1);
      saturateMin(minY,_yRange(xr,zr).minY);
      saturateMax(maxY,_yRange(xr,zr).maxY);
    }
  }
  
  #if 0 // _DEBUG
  // prototype implementation: generate all vertices, scan minmax box from them
  // based on GenerateSegment

  // when lod very coarse, we need to skip top-level grids

  
  float minYExact = FLT_MAX, maxYExact = -FLT_MAX;
  // when we are out (even partially), we need to pre-generate sampled data for the mesh when using TerSynth
  if ((land_InRange(rectSD.zBeg,rectSD.xBeg) && land_InRange(rectSD.zEnd,rectSD.xEnd)) || _terrain->_outsideData.IsNull())
  {
    
    for (int z=rectSD.zBeg; z<=rectSD.zEnd; z++)
    for (int x=rectSD.xBeg; x<=rectSD.xEnd; x++)
    {
      // calculate normals for all basic vertices
      float y = ClippedDataXZ(x,z);
      saturateMin(minYExact,y);
      saturateMax(maxYExact,y);
    }
  }
  else
  {
    PROFILE_SCOPE_EX(lGenO,terr);
    
    // compute 16x16 aligned bounding rectangle and generate it (GetPatch can get both data inside and outside)
    // border not needed here
    const int align = 16;
    int xMinAligned = (rectSD.xBeg)&~(align-1);
    int zMinAligned = (rectSD.zBeg)&~(align-1);
    int xMaxAligned = (rectSD.xEnd+1+align-1)&~(align-1);
    int zMaxAligned = (rectSD.zEnd+1+align-1)&~(align-1);
    
    YDataType yData;
    yData.Dim(xMaxAligned-xMinAligned,zMaxAligned-zMinAligned);

    {
      PROFILE_SCOPE_EX(otPat,land);
      if (PROFILE_SCOPE_NAME(otPat).IsActive())
      {
        PROFILE_SCOPE_NAME(otPat).AddMoreInfo(Format("%d,%d..%d,%d",xMinAligned,zMinAligned,xMaxAligned,zMaxAligned));
      }
      _terrain->_outsideData->GetPatch(yData.Data1D(),xMinAligned,zMinAligned,xMaxAligned-xMinAligned,zMaxAligned-zMinAligned);
    }      

    for (int z=rectSD.zBeg; z<=rectSD.zEnd; z++)
    for (int x=rectSD.xBeg; x<=rectSD.xEnd; x++)
    {
      float y = yData(x-xMinAligned,z-zMinAligned);
      saturateMin(minYExact,y);
      saturateMax(maxYExact,y);
    }
      
  }
  
  // make sure minY/maxY is conservative
#if !_HELISIM_RENDER_TO_TEXTURE
  DoAssert(minYExact>=minY);
  DoAssert(maxYExact<=maxY);
#endif
  #endif

  minmax[0] = Vector3(GetTerrainGrid()*rectSD.xBeg,minY,GetTerrainGrid()*rectSD.zBeg);
  minmax[1] = Vector3(GetTerrainGrid()*rectSD.xEnd,maxY,GetTerrainGrid()*rectSD.zEnd);
  
  // consider seam as well
  // following is a little bit pessimistic, but difference is very small and this implementation is very simple
  minmax[0][1] += TerrainYSeam;
}


/**
@param seg when not NULL, adding another LOD into existing structure
*/
Ref<LandSegment> Landscape::GenerateSegment(const GridRectangle &rect, Ref<LandSegment> seg, int lod)
{
  PROFILE_SCOPE_GRF_EX(lGenS,*,0);

  GEventLogger.Add(rect,"GenerateSegment");
  if (!seg)
  {
    seg=new LandSegment;
    // we have to reconstruct cache
    
    // verify seg _table vertices
    seg->_valid=true;
    seg->_rect=rect;
    seg->_offset = Vector3(
      GetLandGrid()*0.5f*(rect.xBeg+rect.xEnd),
      0,
      GetLandGrid()*0.5f*(rect.zBeg+rect.zEnd)
    );
  }

  // store references to shape before changing LOD
  // note this can sometimes lead to duplicate LODs, if lod will be clamped
  Shape &shape = seg->_lod[lod]._table;
  
  shape.Clear();

  
  // if part of the segment is outside, we will use outside material only
  bool inside = land_InRange(rect.xBeg,rect.zBeg) && land_InRange(rect.xEnd-1,rect.zEnd-1);

  Ref<TexMaterial> singleMaterial = NULL;
  if (_segCache->_singleSatLandSegment)
  {
    // check what material can be used for single-section representation
    int x = rect.xBeg/_segCache->_landSegmentSize;
    int z = (rect.zBeg-_segCache->_landSegZStart)/_segCache->_landSegmentSize;

    if (inside)
    {
      Assert(x>=0 && x<_segCache->_landSeg.GetXRange());
      Assert(z>=0 && z<_segCache->_landSeg.GetYRange());
      singleMaterial = _segCache->_landSeg(x,z)._mat;
    }
    else
    {
      Ref<TexMaterialType> type = new TexMaterialTypeSimpleTerrain(TexMaterialName::_defaultType,seg->_offset);
      singleMaterial = GTexMaterialBank.NewDerived(_terrain->_outsideTextureInfo._mat,type);
    }
  }
  
  const int subdivisionLevel = GetTerrainRangeLog()-GetLandRangeLog();
  // TODO: remove LOD limit  
  // we currently cannot perform more LODding than subdivision allows us to do
  bool simpleOnly = false;
  if (lod>subdivisionLevel)
  {
    // we need to use simple material only
    if (_segCache->_singleSatLandSegment)
    {
      int lodStepAfterSubdiv = 1<<(lod - subdivisionLevel);
      if ((_segCache->_landSegmentSize&(lodStepAfterSubdiv-1))==0)
      {
        simpleOnly = true;
      }
    }
    if (!simpleOnly)
    {
      lod = subdivisionLevel;
    }
  }
  
  // create all vertices
  //const int xCount=rect.xEnd-rect.xBeg+1;
  //int zCount=rect.zEnd-rect.zBeg+1;

  // calculate counts for subdivision of very big landscape squares
  const int maxSubdivisionLevel = 3;
  
  // terrain data are already subdivided
  const int lodStep = 1<<lod;
  const int subdivisionCount = 1<<subdivisionLevel;
  // when lod very coarse, we need to skip top-level grids
  const int subdivisionCountLod = subdivisionLevel-lod>=0 ? 1<<(subdivisionLevel-lod) : 1;
  const int topStep = subdivisionLevel-lod>=0 ? 1 : 1<<(lod-subdivisionLevel);
  const float invSubdivisionCountLod = 1.0f/subdivisionCountLod;

  GridRectangle rectSD;
  rectSD.xBeg = rect.xBeg*subdivisionCount;
  rectSD.xEnd = rect.xEnd*subdivisionCount;
  rectSD.zBeg = rect.zBeg*subdivisionCount;
  rectSD.zEnd = rect.zEnd*subdivisionCount;
  
//  const int xCountSD=(rect.xEnd-rect.xBeg)*subdivisionCount+1;
//  const int zCountSD=(rect.zEnd-rect.zBeg)*subdivisionCount+1;
  const int xCountVert=(((rect.xEnd-rect.xBeg)<<subdivisionLevel)>>lod)+1;
  const int zCountVert=(((rect.zEnd-rect.zBeg)<<subdivisionLevel)>>lod)+1;

  const int nVertices=zCountVert*xCountVert;
  const int maxLandSegmentSize = 12;
  const int maxSubdivisionCount = (1<<maxSubdivisionLevel);
  const int maxNVertices = (
    (maxLandSegmentSize*maxSubdivisionCount+1)*
    (maxLandSegmentSize*maxSubdivisionCount+1)
  );

  //Assert (nVertices<=maxNVertices);
  
  // TODO: use TerSynth for whole segment, not for inidividual vertices

  // generate basic pos and normal map
  AutoArray<Vector3, MemAllocDataStack<Vector3,maxNVertices,AllocAlign16> > pos;
  AutoArray<Vector3, MemAllocDataStack<Vector3,maxNVertices,AllocAlign16> > norm;
#if _VBS3_MORETERRAINLODS
  AUTO_STATIC_ARRAY(VertexCustomInfo,lodHeights,maxNVertices);
#else
  AutoArray<Vector3, MemAllocDataStack<Vector3,maxNVertices,AllocAlign16> > lodHeights;
#endif
  pos.Resize(nVertices);
  lodHeights.Resize(nVertices);
  norm.Resize(nVertices);

#if 0 //_ENABLE_CHEATS
  {
    float xMin = rectSD.xBeg*GetTerrainGrid(), xMax = rectSD.xEnd*GetTerrainGrid();
    float zMin = rectSD.zBeg*GetTerrainGrid(), zMax = rectSD.zEnd*GetTerrainGrid();
    Vector3Val cpos = GScene->GetCamera()->Position();
    if
    (
      cpos.X()>=xMin && cpos.X()<=xMax &&
      cpos.Z()>=zMin && cpos.Z()<=zMax
    )
    {
      __asm nop;
    }
  }
#endif

  // make sure offset can be represented by integer
  DoAssert(((rectSD.xBeg+rectSD.xEnd)&1)==0);
  DoAssert(((rectSD.zBeg+rectSD.zEnd)&1)==0);
  
  int offsetX = (rectSD.xBeg+rectSD.xEnd)>>1;
  int offsetZ = (rectSD.zBeg+rectSD.zEnd)>>1;

  //Vector3 originalS(1, 0, 0);
  //Vector3 originalT(0, 0, 1);
  Vector3 originalSAndT(1, 0, 1);

  // note: every vertex in mesh is initialized
  
  // terrain creation
  {
    // avoid too large absolute value for U/V
    const float uOffset = rect.xBeg+(_segCache->_landSegmentSize>>1);
    const float vOffset = rect.zBeg+(_segCache->_landSegmentSize>>1);
    
#if _VBS3_MORETERRAINLODS
    Assert(LandSegment::NLods == VertexCustomItems); // this should be always fulfilled
#else
    // following loop assumes exactly 3 lods supported
    DoAssert(LandSegment::NLods==3);
#endif

    // when we are out (even partially), we need to pre-generate sampled data for the mesh when using TerSynth
    if ((land_TerrainInRange(rectSD.zBeg,rectSD.xBeg) && land_TerrainInRange(rectSD.zEnd,rectSD.xEnd)) || _terrain->_outsideData.IsNull()/* || lod>1*/) 
    {
      PROFILE_SCOPE_EX(lGenI,terr);
      
      for (int z=rectSD.zBeg; z<=rectSD.zEnd; z+=lodStep)
      for (int x=rectSD.xBeg; x<=rectSD.xEnd; x+=lodStep)
      {
        // calculate normals for all basic vertices
        const int xz=((z-rectSD.zBeg)>>lod)*xCountVert+((x-rectSD.xBeg)>>lod);
        
        Vector3Val normal = ClippedNormal(x,z);
        float grass = ClippedGrass(x,z)*GrassHeightFactor;
        if (grass<=0)
        {
          // avoid z-fight
          grass = NoGrassHeight;
        }

        Assert( normal.IsFinite() );
        Assert( xz<nVertices );
        norm[xz]=normal;

        // set position data
#if _VBS3_MORETERRAINLODS
        VertexCustomInfo custom;
        VertexLodHeights(custom._item, x, z, lod);
        pos[xz] = Vector3(GetTerrainGrid() * (x - offsetX), custom._item[0], GetTerrainGrid() * (z - offsetZ));
        // re-use lod0 (already stored in pos) for grass value
        custom._item[0] = grass;
        lodHeights[xz] = custom;
#else
        //lodHeights[xz] = pos[xz];
        float lods[LandSegment::NLods];
        VertexLodHeights(lods,x,z,lod);
        lodHeights[xz] = Vector3(lods[1],lods[2],grass);
        pos[xz]=Vector3(GetTerrainGrid()*(x-offsetX),lods[0],GetTerrainGrid()*(z-offsetZ));
#endif

        // apply some stretch on border vertices to prevent rounding error tearing
        //const float stretch = -0.100f; // seam debugging - create artificial hole
        const float stretch = +0.0001f;
        if (z==rectSD.zBeg) pos[xz][2] -= stretch;
        if (z==rectSD.zEnd) pos[xz][2] += stretch;
        if (x==rectSD.xBeg) pos[xz][0] -= stretch;
        if (x==rectSD.xEnd) pos[xz][0] += stretch;
      }
    }
    else
    {
      PROFILE_SCOPE_EX(lGenO,terr);
      //const int xCountVertLod0=((rect.xEnd-rect.xBeg)<<subdivisionLevel)+1;
      //const int zCountVertLod0=((rect.zEnd-rect.zBeg)<<subdivisionLevel)+1;
      
      // compute 16x16 aligned bounding rectangle and generate it (GetPatch can get both data inside and outside)
      // we need 1 vertex border so that we can compute normals
      const int align = 16;
      int xMinAligned = (rectSD.xBeg-1)&~(align-1);
      int zMinAligned = (rectSD.zBeg-1)&~(align-1);
      int xMaxAligned = (rectSD.xEnd+2+align-1)&~(align-1);
      int zMaxAligned = (rectSD.zEnd+2+align-1)&~(align-1);
      
      YDataType yData;
      yData.Dim(xMaxAligned-xMinAligned,zMaxAligned-zMinAligned);

      {
        PROFILE_SCOPE_EX(otPat,land);
        if (PROFILE_SCOPE_NAME(otPat).IsActive())
        {
          PROFILE_SCOPE_NAME(otPat).AddMoreInfo(Format("%d,%d..%d,%d",xMinAligned,zMinAligned,xMaxAligned,zMaxAligned));
        }
        _terrain->_outsideData->GetPatch(yData.Data1D(),xMinAligned,zMinAligned,xMaxAligned-xMinAligned,zMaxAligned-zMinAligned);
      }       

      for (int z=rectSD.zBeg; z<=rectSD.zEnd; z+=lodStep)
      for (int x=rectSD.xBeg; x<=rectSD.xEnd; x+=lodStep)
      {
        // calculate normals for all basic vertices
        const int xz=((z-rectSD.zBeg)>>lod)*xCountVert+((x-rectSD.xBeg)>>lod);
        
        Vector3Val normal = ClippedNormalFromArray(yData,x-xMinAligned,z-zMinAligned,GetTerrainGrid());
        float grass = ClippedGrass(x,z)*GrassHeightFactor;
        if (grass<=0)
        {
          // avoid z-fight
          grass = NoGrassHeight;
        }

        Assert( normal.IsFinite() );
        Assert( xz<nVertices );
        norm[xz]=normal;

        // set position data
#if _VBS3_MORETERRAINLODS
        VertexCustomInfo custom;
        VertexLodHeightsFromArray(custom._item,yData,x-xMinAligned,z-zMinAligned,lod);
        pos[xz] = Vector3(GetTerrainGrid() * (x - offsetX), custom._item[0], GetTerrainGrid() * (z - offsetZ));
        // re-use lod0 (already stored in pos) for grass value
        custom._item[0] = grass;
        lodHeights[xz] = custom;
#else
        //lodHeights[xz] = pos[xz];
        float lods[LandSegment::NLods];
        VertexLodHeightsFromArray(lods,yData,x-xMinAligned,z-zMinAligned,lod);
        lodHeights[xz] = Vector3(lods[1],lods[2],grass);
        pos[xz]=Vector3(GetTerrainGrid()*(x-offsetX),lods[0],GetTerrainGrid()*(z-offsetZ));
#endif

        // apply some stretch on border vertices to prevent rounding error tearing
        //const float stretch = -0.100f; // seam debugging - create artificial hole
        const float stretch = +0.0001f;
        if (z==rectSD.zBeg) pos[xz][2] -= stretch;
        if (z==rectSD.zEnd) pos[xz][2] += stretch;
        if (x==rectSD.xBeg) pos[xz][0] -= stretch;
        if (x==rectSD.xEnd) pos[xz][0] += stretch;
      }
    }

#if _VBS3_LAND_SHADOW
    // get land shadow data from LandscapeShadowMap and fill custom vertex info
    GScene->GetSegmentLandShadow(rectSD, lod, lodHeights.Data());
#endif

    // since ArmA we know UV and all attributes are always shared between all neighbouring faces
    // this makes generation a lot easier
    int zCountFace = (rectSD.zEnd-rectSD.zBeg)>>lod;
    int xCountFace = (rectSD.xEnd-rectSD.xBeg)>>lod;
    //int nVertEstimation = nVertices+zCountVert+xCountVert-1;
    int nVertEstimation = nVertices+zCountFace*4+xCountFace*4;
    int nFacesEstimation = zCountFace*xCountFace*2+zCountFace*4+xCountFace*4;
    shape.Reserve(nVertEstimation);
    shape.ReserveST(nVertEstimation);
    shape.ReserveCustom(nVertEstimation);
    shape.ReserveFaces(nFacesEstimation);
    
    // calculate UV range
    UVPair minUV, maxUV, invUV;
    minUV.u = rect.xBeg - uOffset;
    minUV.v = rect.zBeg - vOffset;
    maxUV.u = rect.xEnd - uOffset;
    maxUV.v = rect.zEnd - vOffset;
    shape.InitUV(invUV, minUV, maxUV);

    // first create all vertices
    
    // TODO: handle "border seam" vertices here as well
    {
      PROFILE_SCOPE_EX(lGenV,terr);
      
      Assert(xCountVert-1==(rect.xEnd-rect.xBeg)*subdivisionCountLod/topStep);
      Assert(zCountVert-1==(rect.zEnd-rect.zBeg)*subdivisionCountLod/topStep);
      for( int zz=0; zz<zCountVert; zz++ )
      for( int xx=0; xx<xCountVert; xx++ )
    {
      // prepare to offset interpolation
      // get u,v offset in all four vertices

      // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
      const int xz = zz*xCountVert+xx;
      
      const int vo00 = xz+0+0;

        const float u = rect.xBeg-uOffset+xx*topStep*invSubdivisionCountLod;
        const float v = rect.zBeg-vOffset+zz*topStep*invSubdivisionCountLod;

      // now we have polygon to add
      Assert( vo00<nVertices );

      float uu00 = u;
      float vv00 = v;

      // create polygons for all passes
      // vertex information is almost the same, but the alpha may differ
      // Generate ST vectors for each vertex
      Vector3 s00, t00;
      NormalizeST(originalSAndT, norm[vo00], s00, t00);

#if  _VBS3_MORETERRAINLODS
      const VertexCustomInfo *pc00 = &lodHeights[vo00];
#else
      // custom lod values for all LODs
      const Vector3 *pc00 = &lodHeights[vo00];
#endif
      // store index as candidate for vertices created from same data
        int v00=shape.AddVertex(pos[vo00],norm[vo00],ClipAll,invUV,uu00,vv00,pc00,STPair(s00,t00));
      (void)v00;
      Assert(v00==xz);
    }
    }    
    
    FaceArray shoreFaces;
    shoreFaces.Reserve(nFacesEstimation);

    {
      PROFILE_SCOPE_EX(lGenF,terr);
      // Array to hold faces that belongs to shore (will be copied to shape at the end)

    Poly poly;
    poly.Init();

    for( int zs=rect.zBeg; zs<rect.zEnd; zs+=topStep )
    for( int xs=rect.xBeg; xs<rect.xEnd; xs+=topStep )
    {
      TexMaterial *mat = (
        simpleOnly ?
        singleMaterial.GetRef() :
        inside ? ClippedTextureInfo(zs,xs)._mat.GetRef() : _terrain->_outsideTextureInfo._mat.GetRef()
      );

      ShapeSectionInfo prop;
      prop.Init();
      //prop.SetTexture(texture);

      prop.SetAreaOverTex(0, GetLandGrid()*GetLandGrid());

      // In case of just one pass we know we don't use the alpha.

      // Gather geometry and sections for all the passes

      prop.SetTexture(NULL);
      prop.SetMaterialExt(mat);
      
      for( int zzs=0; zzs<subdivisionCountLod; zzs++ )
      for( int xxs=0; xxs<subdivisionCountLod; xxs++ )
      {
        const int xx = (((xs-rect.xBeg)<<subdivisionLevel)>>lod)+xxs;
        const int zz = (((zs-rect.zBeg)<<subdivisionLevel)>>lod)+zzs;

        // create square face (x,z) (x+1,z), (x+1,z+1), (x,z+1)
        const int xz = zz*xCountVert+xx;
        
        const int vo10 = xz+1+0;
        const int vo00 = xz+0+0;
        const int vo01 = xz+0+xCountVert;
        const int vo11 = xz+1+xCountVert;

        // now we have polygon to add
        Assert( vo10<nVertices );
        Assert( vo00<nVertices );
        Assert( vo01<nVertices );
        Assert( vo11<nVertices );
        
        // Track maximum and minimum of vertex height for shore decisions
        float maxY;
        float minY;

        // store index as candidate for vertices created from same data
        int v10=vo10;
        int v00=vo00;
        int v01=vo01;
        int v11=vo11;
        maxY = pos[vo10].Y();
        minY = pos[vo10].Y();
        saturateMax(maxY, pos[vo00].Y());
        saturateMin(minY, pos[vo00].Y());
        saturateMax(maxY, pos[vo01].Y());
        saturateMin(minY, pos[vo01].Y());
        saturateMax(maxY, pos[vo11].Y());
        saturateMin(minY, pos[vo11].Y());

        static float MaxFaceOnShore = 1.0f;
        static float MinFaceOnShore = 2.0f;

        // Determine whether face can belong to the shore (note it is slightly more conservative,
        // as there are 2 faces considered at once)
        bool faceOnShore = (minY < MaxTide + MaxWave + MaxFaceOnShore) && (maxY > (-MaxWave - MinFaceOnShore));
        
        // Create the first face
        poly.SetN(3);
        poly.Set(0,v10); // shared vertices
        poly.Set(1,v00);
        poly.Set(2,v01);
        shape.AddFace(poly);
        if (faceOnShore) shoreFaces.Add(poly);

        #if 1 //!_ENABLE_CHEATS
        // Create the second face
        //poly.Set(0,v10); // shared vertices
        poly.Set(1,v01);
        poly.Set(2,v11);
        shape.AddFace(poly);
        if (faceOnShore) shoreFaces.Add(poly);
        #endif

        if (xxs==0 && xs==rect.xBeg)
        {
          // border: create a seam 
          // use vertices v00, v01
#if _VBS3_MORETERRAINLODS
          VertexCustomInfo vci00(shape.Custom(v00) + TerrainYSeam);
          int v00Seam=shape.AddVertex(
            pos[vo00]+Vector3(0,TerrainYSeam,0),norm[vo00],ClipAll,invUV,shape.UV(vo00).u,shape.UV(vo00).v,
            &vci00,shape.ST(v00)
            );
          VertexCustomInfo vci01(shape.Custom(v01) + TerrainYSeam);
          int v01Seam=shape.AddVertex(
            pos[vo01]+Vector3(0,TerrainYSeam,0),norm[vo01],ClipAll,invUV,shape.UV(vo01).u,shape.UV(vo01).v,
            &vci01,shape.ST(v01)
            );
#else
          Vector3 vect00(shape.Custom(v00)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
            int v00Seam=shape.AddVertex(
              pos[vo00]+Vector3(0,TerrainYSeam,0),norm[vo00],ClipAll,invUV,shape.UV(vo00).u,shape.UV(vo00).v,
              &vect00,shape.ST(v00)
          );
          Vector3 vect01(shape.Custom(v01)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v01Seam=shape.AddVertex(
              pos[vo01]+Vector3(0,TerrainYSeam,0),norm[vo01],ClipAll,invUV,shape.UV(vo01).u,shape.UV(vo01).v,
              &vect01,shape.ST(v01)
          );
#endif
          poly.Set(0,v01);
          poly.Set(1,v00);
          poly.Set(2,v00Seam);
          shape.AddFace(poly);

          poly.Set(0,v01);
          poly.Set(1,v00Seam);
          poly.Set(2,v01Seam);
          shape.AddFace(poly);
        }
        if (xxs==subdivisionCountLod-1 && xs==rect.xEnd-1)
        {
          // border: create a seam 
          // use vertices v10, v11
#if _VBS3_MORETERRAINLODS
          VertexCustomInfo vci00(shape.Custom(v10) + TerrainYSeam);
          int v10Seam=shape.AddVertex(
            pos[vo10]+Vector3(0,TerrainYSeam,0),norm[vo10],ClipAll,invUV,shape.UV(vo10).u,shape.UV(vo10).v,
            &vci00,shape.ST(v10)
            );
          VertexCustomInfo vci01(shape.Custom(v11) + TerrainYSeam);
          int v11Seam=shape.AddVertex(
            pos[vo11]+Vector3(0,TerrainYSeam,0),norm[vo11],ClipAll,invUV,shape.UV(vo11).u,shape.UV(vo11).v,
            &vci01,shape.ST(v11)
            );
#else
          Vector3 vect10(shape.Custom(v10)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v10Seam=shape.AddVertex(
              pos[vo10]+Vector3(0,TerrainYSeam,0),norm[vo10],ClipAll,invUV,shape.UV(vo10).u,shape.UV(vo10).v,
              &vect10,shape.ST(v10)
          );
          Vector3 vect11(shape.Custom(v11)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v11Seam=shape.AddVertex(
              pos[vo11]+Vector3(0,TerrainYSeam,0),norm[vo11],ClipAll,invUV,shape.UV(vo11).u,shape.UV(vo11).v,
              &vect11,shape.ST(v11)
          );
#endif
          
          poly.Set(0,v10);
          poly.Set(1,v11);
          poly.Set(2,v11Seam);
          shape.AddFace(poly);

          poly.Set(0,v10);
          poly.Set(1,v11Seam);
          poly.Set(2,v10Seam);
          shape.AddFace(poly);
        }

        if (zzs==0 && zs==rect.zBeg)
        {
          // border: create a seam 
          // use vertices v00, v10
#if _VBS3_MORETERRAINLODS
          VertexCustomInfo vci00(shape.Custom(v00) + TerrainYSeam);
          int v00Seam=shape.AddVertex(
            pos[vo00]+Vector3(0,TerrainYSeam,0),norm[vo00],ClipAll,invUV,shape.UV(vo00).u,shape.UV(vo00).v,
            &vci00,shape.ST(v00)
            );
          VertexCustomInfo vci01(shape.Custom(v10) + TerrainYSeam);
          int v10Seam=shape.AddVertex(
            pos[vo10]+Vector3(0,TerrainYSeam,0),norm[vo10],ClipAll,invUV,shape.UV(vo10).u,shape.UV(vo10).v,
            &vci01,shape.ST(v10)
            );
#else
          Vector3 vect00(shape.Custom(v00)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v00Seam=shape.AddVertex(
              pos[vo00]+Vector3(0,TerrainYSeam,0),norm[vo00],ClipAll,invUV,shape.UV(vo00).u,shape.UV(vo00).v,
              &vect00,shape.ST(v00)
          );
          Vector3 vect01(shape.Custom(v10)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v10Seam=shape.AddVertex(
              pos[vo10]+Vector3(0,TerrainYSeam,0),norm[vo10],ClipAll,invUV,shape.UV(vo10).u,shape.UV(vo10).v,
              &vect01,shape.ST(v10)
          );
#endif

          poly.Set(0,v00);
          poly.Set(1,v10);
          poly.Set(2,v10Seam);
          shape.AddFace(poly);

          poly.Set(0,v00);
          poly.Set(1,v10Seam);
          poly.Set(2,v00Seam);
          shape.AddFace(poly);
        }
        if (zzs==subdivisionCountLod-1 && zs==rect.zEnd-1)
        {
          // border: create a seam 
          // use vertices v11,v01
#if _VBS3_MORETERRAINLODS
          VertexCustomInfo vci00(shape.Custom(v01) + TerrainYSeam);
          int v01Seam=shape.AddVertex(
            pos[vo01]+Vector3(0,TerrainYSeam,0),norm[vo01],ClipAll,invUV,shape.UV(vo01).u,shape.UV(vo01).v,
            &vci00,shape.ST(v01)
            );
          VertexCustomInfo vci01(shape.Custom(v11) + TerrainYSeam);
          int v11Seam=shape.AddVertex(
            pos[vo11]+Vector3(0,TerrainYSeam,0),norm[vo11],ClipAll,invUV,shape.UV(vo11).u,shape.UV(vo11).v,
            &vci01,shape.ST(v11)
            );
#else
          Vector3 vect01(shape.Custom(v01)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v01Seam=shape.AddVertex(
              pos[vo01]+Vector3(0,TerrainYSeam,0),norm[vo01],ClipAll,invUV,shape.UV(vo01).u,shape.UV(vo01).v,
              &vect01,shape.ST(v01)
          );
          Vector3 vect11(shape.Custom(v11)+Vector3(TerrainYSeam,TerrainYSeam,TerrainYSeam));
          int v11Seam=shape.AddVertex(
              pos[vo11]+Vector3(0,TerrainYSeam,0),norm[vo11],ClipAll,invUV,shape.UV(vo11).u,shape.UV(vo11).v,
              &vect11,shape.ST(v11)
          );
#endif

          poly.Set(0,v11);
          poly.Set(1,v01);
          poly.Set(2,v01Seam);
          shape.AddFace(poly);
            
          poly.Set(0,v11);
          poly.Set(1,v01Seam);
          poly.Set(2,v11Seam);
          shape.AddFace(poly);
        }
      } // for (xxs, zzs - subdivided rect.)
      // create sections on the fly, during Optimize sort sections only
      shape.ExtendLastSection(prop);
      // if there will be any more passes, make sure they use correct flags
      prop.SetSpecial(GroundFlagsPass1Lerp|NoClamp|FilterAnizotrop);
    } // for (x,z)
    }
 
    //Log("Alloc %d<%d",seg->_mesh.NVertex(),nVertices*3);
    #if LOG_SHARING
      int maxV = shape.NFaces()*3;
      int actV = shape.NVertex();
      LogF("Ground sharing ratio is %f",actV*1.0f/maxV);
    #endif

    // sort faces by texture - it will help to avoid state changes
    shape.Optimize();

    // Set the Basic geometry range to cover the all shape
    seg->_lod[lod]._geometry[GTBasic]._beg = 0;
    seg->_lod[lod]._geometry[GTBasic]._end = shape.NSections();

    // Precalculate index of start of simple section (which is right behind the Basic geometry)
    int simpleSectionStart = seg->_lod[lod]._geometry[GTBasic]._end;

    // if shape is already simple, we can do nothing more
    if (!simpleOnly)
    {
      PROFILE_SCOPE_EX(lGenI,terr);
      if (_segCache->_singleSatLandSegment)
      { // all sections sharing the same satellite map texture - merge them
        // map the simplified section by a coarse material
        for (int i = seg->_lod[lod]._geometry[GTBasic]._beg; i < seg->_lod[lod]._geometry[GTBasic]._end; i++)
        {
          const ShapeSection &src = shape.GetSection(i);
          const TexMaterial *mat = src.GetMaterialExt();
          if (mat && (mat->GetPixelShaderID()==PSTerrainX || mat->GetPixelShaderID()==PSTerrainSNX))
          {
            // note: src may be invalidated during AddSection resizing
            ShapeSection sec = src;
            // materials in multiple sections can differ by name only
            // like p_031-011_l00_l01_l03_n.rvmat
            // we want all materials with identical sat. map to be considered the same one here
            if (inside)
            {
              Assert(mat->_stage[0]._tex==singleMaterial->_stage[0]._tex);
              Assert(mat->_stage[1]._tex==singleMaterial->_stage[1]._tex);
            }
            sec.SetMaterialExt(singleMaterial);
            shape.AddSection(sec);
          }
        }
      }
      else
      { // merge sections sharing the same satellite map texture
        Ref<TexMaterialType> type = new TexMaterialTypeSimpleTerrain(TexMaterialName::_defaultType,seg->Offset());
        // we want to create a simplified version of the sections listed
        // we will currently use the same indices
        // we start with using a derived materials
        // in future, we could build another LOD here
        for (int i = seg->_lod[lod]._geometry[GTBasic]._beg; i < seg->_lod[lod]._geometry[GTBasic]._end; i++)
        {
          const ShapeSection &src = shape.GetSection(i);
          const TexMaterial *mat = src.GetMaterialExt();
          if (mat && mat->GetPixelShaderID()>=PSTerrainX)
          {
            // note: src may be invalidated during AddSection resizing
            ShapeSection sec = src;
            // materials in multiple sections can differ by name only
            // like p_031-011_l00_l01_l03_n.rvmat
            // we want all materials with identical sat. map to be considered the same one here
            // search if material for given sat. map is already created
            TexMaterial *find = NULL;
            for (int s = simpleSectionStart; s < shape.NSections(); s++)
            {
              const ShapeSection &ss = shape.GetSection(s);
              TexMaterial *sat = ss.GetMaterialExt();
              if (
                sat && (sat->Load(),sat->_stage[0]._tex==mat->_stage[0]._tex)
              )
              {
                find = sat;
                break;
              }
            }
            if (!find)
            {
              // the material is never shared between segments, as it contains segment offset
              // therefore we as a segment are free to manipulate it
              // reloading it will not work, though
              Ref<TexMaterial> matDerived = GTexMaterialBank.NewDerived(mat,type);
              sec.SetMaterialExt(matDerived);
            }
            else
            {
              TerrainCombine(find,mat);
              sec.SetMaterialExt(find);
            }
            // new section should contain all faces from the old one
            shape.AddSection(sec);
          }
        }
      }

      // we cannot perform full optimize, as this would need to reorder faces as well
      shape.ConcatSections(simpleSectionStart);

      // Set the Simple geometry range
      seg->_lod[lod]._geometry[GTSimple]._beg = simpleSectionStart;
      seg->_lod[lod]._geometry[GTSimple]._end = shape.NSections();
    }

    {

  PROFILE_SCOPE_EX(lGenW,terr);

    // Add shore faces and sections
    {
      // Copy shore faces
      shape.Faces().Merge(shoreFaces);

      // Create section
      ShapeSectionInfo section;
      section.Init();
      section.SetAreaOverTex(0, GetLandGrid() * GetLandGrid());
      section.SetTexture(GetSeaTexture());
      section.SetMaterialExt(_shoreMaterial);
      //section.SetSpecial(GroundFlagsPass1Lerp|NoClamp|FilterAnizotrop);
      section.SetSpecial(GroundFlagsPass1Lerp|NoClamp|FilterTrilinear);

      // Add section to the shape and include all homeless faces into it (set the GTShore as well)
      seg->_lod[lod]._geometry[GTShore]._beg = shape.NSections();
      shape.ExtendLastSection(section, true);
      seg->_lod[lod]._geometry[GTShore]._end = shape.NSections();
    }

    // Add foam shore section
    {
      // Add sections to the shape
      seg->_lod[lod]._geometry[GTShoreFoam]._beg = shape.NSections();
      for (int i = seg->_lod[lod]._geometry[GTShore]._beg; i < seg->_lod[lod]._geometry[GTShore]._end; i++)
      {
        // Create section copy the GTShore and replace material
        // note: copy needed, avoid reference, resize possible
        ShapeSection section = shape.GetSection(i);
        section.SetMaterialExt(_shoreFoamMaterial);

        // Add new section
        shape.AddSection(section);
      }
      seg->_lod[lod]._geometry[GTShoreFoam]._end = shape.NSections();
    }

    // Add wet shore section
    {
      // Add sections to the shape
      seg->_lod[lod]._geometry[GTShoreWet]._beg = shape.NSections();
      for (int i = seg->_lod[lod]._geometry[GTShore]._beg; i < seg->_lod[lod]._geometry[GTShore]._end; i++)
      {
        // Create section copy the GTShore and replace material
        // note: copy needed, avoid reference, resize possible
        ShapeSection section = shape.GetSection(i);
        section.SetMaterialExt(_shoreWetMaterial);

        // Add new section
        shape.AddSection(section);
      }
      seg->_lod[lod]._geometry[GTShoreWet]._end = shape.NSections();
    }

    // Add grass section
    if (DrawGrassLayer)
    {
      Ref<TexMaterialType> type = new TexMaterialTypeGrassTerrain(TexMaterialName::_defaultType,seg->Offset());
      // replicate all basic sections to get a grass one
      seg->_lod[lod]._geometry[GTGrass]._beg = shape.NSections();
      for (int i = seg->_lod[lod]._geometry[GTBasic]._beg; i < seg->_lod[lod]._geometry[GTBasic]._end; i++)
      {
        // Create section copy the GTBasic and replace material
        // note: copy needed, avoid reference, resize possible
        ShapeSection section = shape.GetSection(i);
        if (section.GetMaterialExt())
        {
          Ref<TexMaterial> mat = GTexMaterialBank.NewDerived(section.GetMaterialExt(),type);
          // it is quite likely each material is used only once
          // transform its textures now
          if (PrepareGrassTextures(mat,section.GetMaterialExt()))
          {
            // if result not empty, add a new section
            section.SetMaterialExt(mat);
            shape.AddSection(section);
          }
        }
      }
      seg->_lod[lod]._geometry[GTGrass]._end = shape.NSections();
    }
    shape.CalculateMinMax();
    }

    shape.Compact();
    
    // we know we are using ClipAll everywhere on the landscape
    shape.SetClipAll(ClipAll);

    // convert materials from world space to model space
    // if using single material, conversion was already done
    if (!simpleOnly)
    {
      PROFILE_SCOPE_EX(lGenB,terr);
      seg->BuildLocalMaterials(this,shape,lod);
    }
    
    // index is the same for all lods
    if (seg->_index._grid.GetXRange()==0)
    {
      PROFILE_SCOPE_EX(lGenT,terr);
      seg->Initialize(rect);
      seg->BuildTexMatIndex(seg->_index,this);
      shape.BuildTexAreaIndex();
    }

    // TODO: sort vertices by face
    //LogF("CWater %x",seg);
    //shape.FindSections(true);
    PROFILE_SCOPE_EX(lGenC,terr);
    shape.SetFacesCanonical(true);
    shape.ConvertToVBuffer(VBBigDiscardable);
    #if _ENABLE_REPORT && !defined MFC_NEW
    if(!shape.GetVertexBuffer())
    {
#ifdef _WIN32
      void ReportMemoryTotals();
      ReportMemoryTotals();
#endif
      Fail("Terrain VB creation failed");
    }
    #endif
  }

  #if LOG_SEG
    static float sumTime[LandSegment::NLods];
    static int sumCount[LandSegment::NLods];
    
    sumTime[lod] += PROFILE_SCOPE_NAME(lGenS).TimeSpentNorm()*0.01f;
    sumCount[lod]++;
    if (sumCount[lod]>=20)
    {
      LogF(
        "GenerateSegment lod %d, time spent: %.1f ms",lod,sumTime[lod]/sumCount[lod]
      );
      sumCount[lod] = 0;
      sumTime[lod] = 0;
    }
//     LogF(
//       "GenerateSegment %d,%d, lod %d, time spent: %.1f ms",
//       rect.xBeg,rect.zBeg,lod,scopeProf__lGenS.TimeSpentNorm()*0.01f
//     );
  #endif

  return seg;
}

void Landscape::GenerateRoadSegment(const GridRectangle &rect, Ref<LandSegment> seg, int lod)
{
  seg->_lod[lod]._roadsDone = true;
  
  if (!_roadsIntoSegment || !_roadNet)
  {
    return;
  }

  PROFILE_SCOPE_GRF_EX(lGenR,*,0);

  Shape &roads = seg->_lod[lod]._roads;
  const Shape &shape = seg->_lod[lod]._table;

  // we should never generate twice. If we already have some data, it is strange  
  DoAssert(!roads.GetVertexBuffer());
  
  const int subdivisionLevel = GetTerrainRangeLog()-GetLandRangeLog();
  // TODO: removed LOD limit  
  // we currently cannot perform more LODding than subdivision allows us to do
  if (lod>subdivisionLevel) lod = subdivisionLevel;
  
  const int subdivisionCount = 1<<subdivisionLevel;

  roads.Clear();
  
  //enlarge the rectangle, as models of roads in the neighboring segment can intersect this one
  int zBeg = (rect.zBeg>0 ? rect.zBeg-1 : 0);
  int xBeg = (rect.xBeg>0 ? rect.xBeg-1 : 0);
  int zEnd = (((rect.zEnd+1)&(~GetLandRangeMask()))==0 ? (rect.zEnd+1) : rect.zEnd);
  int xEnd = (((rect.xEnd+1)&(~GetLandRangeMask()))==0 ? (rect.xEnd+1) : rect.xEnd);
  OffTreeRoad roadVertexTree(
    (rect.xBeg-2)*GetLandGrid()-seg->_offset.X(),(rect.zBeg-2)*GetLandGrid()-seg->_offset.Z(),
    (rect.xEnd-rect.xBeg+4)*GetLandGrid()
  );
  //estimated number of roads and faces computed using heuristic (area)
  float estArea = (zEnd-zBeg)*(xEnd-xBeg)*GetLandGrid()*GetLandGrid();
  const int EstNRoadsInGridSquare = 50;
  const int EstRoadNVertices = 10;
  const int MinNRoadVertices = 350;
  int estNRoadVertices = toInt(estArea/Square(EstNRoadsInGridSquare)*EstRoadNVertices);
  saturateMax(estNRoadVertices,MinNRoadVertices);
  roads.Reserve(estNRoadVertices);
  int estNRoadFaces= estNRoadVertices;
  roads.FaceReserveRaw(estNRoadFaces);
  roads.ReserveST(estNRoadVertices);
  bool initialized = false;
  for( int zs=zBeg; zs<zEnd; zs++ )
  {
    for( int xs=xBeg; xs<xEnd; xs++ )
    {
      //Road Representation -> enumerate all roads and merge them to one shape
      const RoadList &list = _roadNet->GetRoadList(xs,zs);
      for (int i=0;i<list.Size();i++)
      { 
        if (seg->MergeAndSnapRoads(roads, list[i], &roadVertexTree))
        {
          // initialize road index only when there are some roads
          if (!initialized)
          {
            seg->_indexRoads.Initialize(rect);
            initialized = true;
          }
          seg->UpdateTexMatIndex(seg->_indexRoads,xs-rect.xBeg,zs-rect.zBeg,list[i]->GetLODShape());
        }
      }
    }
  }

#if _ARMA3_TERRAIN_DECALS
  Matrix4 fromWorld(MTranslation,-seg->Offset());
  for( int zs=zBeg; zs<zEnd; zs++ )
  {
    for( int xs=xBeg; xs<xEnd; xs++ )
    {
      const ObjectList &list=GetObjects(zs,xs);
      for( int o=0; o<list.Size(); o++ )
      {
        Object *obj=list[o];
        if (!obj)
          continue;
        if( obj->GetType()!=LandDecal ) continue;
        if (obj->GetShape()==NULL) continue;

        //convert road to segment's model space
        Matrix4 transform = fromWorld * obj->FutureVisualState().Transform();
        if (seg->MergeAndSnapRoads(roads, obj->GetShape(), transform, &roadVertexTree))
        {
          // initialize road index only when there are some roads
          if (!initialized)
          {
            seg->_indexRoads.Initialize(rect);
            initialized = true;
          }
          seg->UpdateTexMatIndex(seg->_indexRoads,xs-rect.xBeg,zs-rect.zBeg,obj->GetShape());
        }
      }
    }
  }
#endif //#if _ARMA3_TERRAIN_DECALS

  // if there
  roads.AdjustAutoClamp();
  // adjust all materials
  for (int s=0; s<roads.NSections(); s++)
  {
    ShapeSection &sec = roads.GetSection(s);
    const TexMaterial *mat = sec.GetMaterialExt();
    if (!mat)
    {
      const Texture *tex = sec.GetTexture();
      LogF("Road with no material, texture %s",tex ? cc_cast(tex->GetName()) : "<null>");
    }
    else
    {
      Ref<TexMaterial> roadMat = GTexMaterialBank.NewDerived(mat,TexMaterialName::_roadType);
      sec.SetMaterialExt(roadMat);
    }
  }
  //roads.OptimizeIgnoreAlpha();
  roads.Optimize();
  // alpha is needed for all roads to make sure distance based disappering works
  roads.OrSpecial(ZBiasStep*2|IsAlpha);
  //roadsShape now contains all roads merged
  Matrix4 toWorld(MTranslation,seg->_offset);


  GridRectangle rectSD;
  rectSD.xBeg = rect.xBeg*subdivisionCount;
  rectSD.xEnd = rect.xEnd*subdivisionCount;
  rectSD.zBeg = rect.zBeg*subdivisionCount;
  rectSD.zEnd = rect.zEnd*subdivisionCount;

  // note: split is done based on LOD
  roads.SurfaceSplit(
    this, toWorld,
    GEngine->ZShadowEpsilon(),0.0f, &rectSD /*doCut*/, true /*quickAdd*/, lod
  );

  // we are all split up
  // now we want to provide an information for the LOD blending
  roads.ReserveCustom(roads.NVertex());
  roads.ResizeCustom(roads.NVertex());
  for (int v=0; v<roads.NVertex(); v++)
  {
    // pos is valid for current LOD (=variable lod)
    // better LOD is never rendered using this
    Vector3Val pos = roads.Pos(v);
    // convert to world space
    Vector3Val wPos = pos + seg->_offset;
    float grass = GrassHeight(wPos.X(),wPos.Z())*GrassHeightFactor;
    if (grass<=0)
    {
      // make sure zero grass is not affecting terrain or roads at all
      // (avoid z-fight)
      grass = NoGrassHeight;
    }

#if _VBS3_MORETERRAINLODS
    Assert(LandSegment::NLods == VertexCustomItems); // that should be always fulfilled, see the NLods declaration
    Assert(VertexCustomItems > 1); // and this too
    // we want to provide worse LOD as well - model space
    VertexCustomInfo custom;
    custom._item[0] = grass;
    for (int l = 1; l < VertexCustomItems; l++)
      custom._item[l] = (l <= lod) ? pos.Y() : (SurfaceYLod(wPos.X(), wPos.Z(), l) - seg->_offset.Y());
    roads.SetCustom(v, custom);
#else
    if (LandSegment::NLods>1)
    {
      //pos[1] += grass;

      // we want to provide worse LOD as well - model space
      float lodY[LandSegment::NLods];
      for (int l=0; l<=lod; l++)
      {
        lodY[l] = pos.Y();
      }
      for (int l=lod+1; l<LandSegment::NLods; l++)
      {
        lodY[l] = SurfaceYLod(wPos.X(),wPos.Z(),l)-seg->_offset.Y(); //+grass;
      }
      DoAssert(LandSegment::NLods==3);
      Vector3 custom(lodY[1],lodY[2],grass);
      roads.SetCustom(v,custom);
    }
    else
    {
      Vector3 custom(pos.Y(),pos.Y(),grass);
      roads.SetCustom(v,custom);
    }
#endif
    roads.SetPos(v) = pos;
  }
  // 
#if _VBS3_LAND_SHADOW
  // get land shadow data from LandscapeShadowMap and fill custom vertex info
  if (GScene)
    GScene->GetLandShadowValuesShape(rectSD, roads.GetPosArray(), roads.GetCustomArray());
#endif

  roads.Compact();
  roads.SetClipAll(ClipAll);
  roads.ConvertToVBuffer(VBBigDiscardable);
  roads.CopyMinMax(shape);
  roads.BuildTexAreaIndex();
  DoAssert(roads.GetVertexBuffer() || roads.NVertex()==0);
}

void Landscape::MapSeaUVLogicalLinear(float &u, float &v, float xx, float zz, float coastDirX, float coastDirZ) const
{
  v = coastDirX*xx+coastDirZ*zz;
  u = -coastDirZ*xx+coastDirX*zz;
}

void Landscape::MapSeaUVLogicalCircular(float &u, float &v, float xx, float zz) const
{
  // circular mapping
  float centerX = GetLandRange()*0.5*GetLandGrid()*InvWaterGrid;
  float centerZ = GetLandRange()*0.5*GetLandGrid()*InvWaterGrid;
  // calculate center position
  float x = xx-centerX, z = zz-centerZ;
  float r = sqrt(Square(x)+Square(z));
  float theta = atan2(x,z);
  //const float uCircle = 800;
  const float uCircle = 500;
  // make sure theta makes m
  
  // vertex shader will do decompression from 
  v = theta*(uCircle/(H_PI*2));
  u = r;
}


void Landscape::MapSeaUVLogical(float &u, float &v, float xx, float zz, int waterSegmentSize) const
{
  float mulUV,addUV;
  if (GEngine->CanSeaUVMapping(waterSegmentSize,mulUV,addUV))
  {
    MapSeaUVLogicalCircular(u,v,xx,zz);
  }
  else
  {
    // simple rectangular mapping 
    u = xx;
    v = zz;
  }
}

void Landscape::MapSeaUV(float &u, float &v, float xx, float zz, int waterSegmentSize) const
{
  const bool circularWaves = true;
  //static bool circularWaves = false;
  float mulUV,addUV;
  if (circularWaves && GEngine->CanSeaUVMapping(waterSegmentSize,mulUV,addUV))
  {
    #if 0
    float y00 = GetData(0,0);
    float y10 = GetData(GetLandRangeMask(),0);
    float y01 = GetData(0,GetLandRangeMask());
    float y11 = GetData(GetLandRangeMask(),GetLandRangeMask());
    static bool cheatY = false;
    if (cheatY)
    {
      static float cheatY00 = -100;
      static float cheatY10 = -100;
      static float cheatY01 = -100;
      static float cheatY11 = 100;
      y00 = cheatY00;
      y01 = cheatY01;
      y10 = cheatY10;
      y11 = cheatY11;
    }
    if (y00>0 || y10>0 || y01>0 || y11>0)
    {
      // coast - sea only on some parts
      // compute gradient of "sea-ness". Input "sea-ness" consider as a discrete value
      // TODO: precompute and store in the landscape
      float sea00 = y00<=0;
      float sea10 = y10<=0;
      float sea01 = y01<=0;
      float sea11 = y11<=0;
      float dsdx = ((sea10-sea00)+(sea11-sea01))*0.5f;
      float dsdz = ((sea01-sea00)+(sea11-sea10))*0.5f;
      float invSizeDS = InvSqrt(dsdx*dsdx+dsdz*dsdz);
      MapSeaUVLogicalLinear(u,v,xx,zz,dsdx*invSizeDS,dsdz*invSizeDS);
      // vertex shader will do decompression
      v = v*mulUV+addUV;
      u = u*mulUV+addUV;
    }
    else
    #endif
    {
      // island - wrapped by a sea
      // circular mapping
      MapSeaUVLogicalCircular(u,v,xx,zz);
      // vertex shader will do decompression
      v = v*mulUV+addUV;
      u = u*mulUV+addUV;
    }
  }
  else
  {
    // simple rectangular mapping 
    u = xx;
    v = zz;
  }
}


Ref<WaterSegment> Landscape::GenerateWaterSegment(const GridRectangle &rect)
{
  PROFILE_SCOPE_GRF_EX(lGenW,*,0);
  Ref<WaterSegment> seg = new WaterSegment;
  // we have to reconstruct cache
  
  // verify seg _table vertices
  seg->_valid=true;
  seg->_rect=rect;
  // note: offset is currently not used by water
  seg->_offset = Vector3(
    GetLandGrid()*0.5f*(rect.xBeg+rect.xEnd),
    0,
    GetLandGrid()*0.5f*(rect.zBeg+rect.zEnd)
  );
  
  int waterSegmentSize = GetWaterSegmentSize();
#if _VBS3_LARGE_WATERSEG
  if (WaterGrid==GetLandGrid() && waterSegmentSize == MaxWaterSegmentSize)
#else
  if (WaterGrid==GetLandGrid())
#endif
  {
    // special case optimization - water grid equal to landscape grid
    // this was common in OFP, however it is no longer hit in ArmA
    int terrainLog = GetTerrainRangeLog()-GetLandRangeLog();
    // terrain sampling for water vertices
    int x = rect.xBeg, z = rect.zBeg;
    for (int zz=0,i=0; zz<=waterSegmentSize; zz++) for (int xx=0; xx<=waterSegmentSize; xx++)
    {
      seg->_waterDepth[i][0] = _terrain->ClippedDataXZ((x+xx)<<terrainLog,(z+zz)<<terrainLog);
      MapSeaUV(seg->_waterDepth[i][1],seg->_waterDepth[i][2],xx+rect.xBeg,zz+rect.zBeg,waterSegmentSize);
      seg->_waterDepth[i][3] = 1;
      i++;
    }
  }
  else
  {
    // terrain sampling for water vertices
    int x = rect.xBeg, z = rect.zBeg;
#if _VBS3_LARGE_WATERSEG
    float coef = (float) waterSegmentSize / MaxWaterSegmentSize;
    for (int zz=0,i=0; zz<=MaxWaterSegmentSize; zz++) for (int xx=0; xx<=MaxWaterSegmentSize; xx++)
    {
      seg->_waterDepth[i][0] = SurfaceY((x+xx*coef)*WaterGrid,(z+zz*coef)*WaterGrid);
      MapSeaUV(seg->_waterDepth[i][1],seg->_waterDepth[i][2],xx*coef+rect.xBeg,zz*coef+rect.zBeg,waterSegmentSize);
#else
    for (int zz=0,i=0; zz<=waterSegmentSize; zz++) for (int xx=0; xx<=waterSegmentSize; xx++)
    {
      seg->_waterDepth[i][0] = SurfaceY((x+xx)*WaterGrid,(z+zz)*WaterGrid);
      MapSeaUV(seg->_waterDepth[i][1],seg->_waterDepth[i][2],xx+rect.xBeg,zz+rect.zBeg,waterSegmentSize);
#endif
      seg->_waterDepth[i][3] = 1;
      i++;
    }

  }

  #if LOG_SEG>1
    LogF("GenerateWaterSegment %d,%d, time spent %.2f ms",rect.xBeg,rect.zBeg,scopeProf__lGenW.TimeSpentNorm()*0.01f);
  #endif
  
  return seg;
}

#define LANDDRAW 1


/// micro-jobs are gathered first, processed by JobManager later
/** adapted from MicroJobList in scene.cpp */
class MicroJobLandList
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

static MicroJobLandList GMicroJobsLandCB;

template <class SkyTextures>
struct SkyTexturesContext: private NoCopy
{
  bool _textureAReady;
  bool _textureBReady;
  Texture *_textureA;
  Texture *_textureB;
  Shape *_shape;
  TexMaterial *_skyMaterial;
  
  SkyTexturesContext(Shape *shape, TexMaterial *skyMaterial):_shape(shape),_skyMaterial(skyMaterial)
  {
    _textureA = SkyTextures::GetA(shape,skyMaterial);
    _textureB = SkyTextures::GetB(shape,skyMaterial);

    // Make sure both textures exist
    if (_textureA && _textureB)
    {
      // Determine if textures are ready
      _textureAReady = _textureA->PrepareMipmap0(false);
      _textureBReady = _textureB->PrepareMipmap0(false);

      // If no texture is ready, don't do anything
      if (_textureAReady || _textureBReady)
      {
        // A texture not ready, use B
        if (!_textureAReady)
          SkyTextures::SetA(shape,skyMaterial,_textureB);

        // B texture not ready, use A
        if (!_textureBReady)
          SkyTextures::SetB(shape,skyMaterial,_textureA);
      }
    }
  }
  
  ~SkyTexturesContext()
  {
    // If no texture is ready, don't do anything
    if (_textureAReady || _textureBReady)
    {
      // A texture not ready, restore A
      if (!_textureAReady)
        SkyTextures::SetA(_shape,_skyMaterial,_textureA);

      // B texture not ready, restore B
      if (!_textureBReady)
        SkyTextures::SetB(_shape,_skyMaterial,_textureB);
    }
  }
  
};

#if _PROFILE || _DEBUG
  #define PIX_NAMED_CB_SCOPES 1
#endif

#include <Es/Memory/normalNew.hpp>
/// terrain rendering micro job
class Landscape::LandDrawTask: public IMicroJob
{
  /// slot where the result should be stored
  int _resultIndex;
  // ID of the command buffer as returned by StartCBRecording
  int _cb;
  
  /// each _lights needs its own storage which exists during the task lifetime
  /** must be defined before _lights, so that is it constructed before used */
  LightList _lightsWork;
  /// we always need keep the "shape" loaded
  //@{ parameters needed to call the helper
  Landscape *_land;
  LandSegment *_seg;
  int _x,_z;
  const GridRectangle &_groundPrimed;
  Scene &_scene;
  bool _depthOnly;
  bool _zPrime;
  int _lod;
  Vector3Par _pos;
  float _minZDist;
  float _zd;
  float _gridSize;
  const LightList &_lights;
  //@}
  
  
  VertexBufferLock _shapeLock,_roadsLock;
  
  public:
  LandDrawTask(
    int resultIndex,
    Landscape *land, LandSegment *seg, int x, int z, const GridRectangle &groundPrimed, Scene &scene,
    bool depthOnly, bool zPrime, int lod, Vector3Par pos, float minZDist, float gridSize, float zd,
    Vector3Val bCenter, float bRadius
  ):_resultIndex(resultIndex),_land(land),_seg(seg),_x(x),_z(z),_groundPrimed(groundPrimed),_scene(scene),
  _depthOnly(depthOnly),_zPrime(zPrime),_lod(lod),_pos(pos),_minZDist(minZDist),_gridSize(gridSize),_zd(zd),
  _lights((depthOnly || zPrime) && !Glob.config._predication ? _lightsWork : scene.SelectAggLights(bCenter,bRadius,_lightsWork))
  {
    // TODO: avoid code duplication - always use the task
    LandSegment::SegLod &segLod = seg->_lod[lod];

    // lock the shape VB before we call GenerateRoad, as that could evict us otherwise  
    _shapeLock.Lock(&segLod._table);
    segLod._table.ForceTexturesReady();
    // when needed, generate the road shape + VB as well
    if (!depthOnly && !zPrime)
    {
      seg->GenerateRoad(lod,zd,land);
      segLod._roads.ForceTexturesReady();
      _roadsLock.Lock(&segLod._roads);
    }
  }
  
  virtual void operator () (IMTTContext *context, int thrIndex)
  {
    PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
    #if PIX_NAMED_CB_SCOPES
      // create ID which is likely to be unique (not clashing with object IDs)
      int debugId = 0x10000000+_x*0x1000+_z;
    #else
      int debugId = -1;
    #endif
    _cb = GEngine->StartCBRecording(_resultIndex,thrIndex,debugId);
    _land->LandDrawHelper(_cb,_seg,_x,_z,_groundPrimed,_scene,_depthOnly,_zPrime,_lod,_pos,_minZDist,_gridSize,_zd,_lights);
    GEngine->StopCBRecording(_cb,_resultIndex,"");
  }

  USE_FAST_ALLOCATOR

  friend void LandscapeManualCleanUp();
  friend void LandscapeSetManualCleanUp(bool manualCleanup);
};

//////////////////////////////////////////////////////////////////////////

/// water rendering micro job
class Landscape::WaterDrawTask: public IMicroJob
{
  /// slot where the result should be stored
  int _resultIndex;
  // ID of the command buffer as returned by StartCBRecording
  int _cb;
  
  /// we always need keep the "shape" loaded
  //@{ parameters needed to call the helper
  Landscape *_land;
  WaterSegment *_seg;
  int _x,_z;
  //const GridRectangle &_groundPrimed;
  Scene &_scene;
  //bool _depthOnly;
  //bool _zPrime;
  int _lod;
  Vector3 _offset;
  float _dist;

  LightList _lights;
  //float _zd;
  //float _gridSize;
  //@}
  
  VertexBufferLock _shapeLock;
  
public:
  WaterDrawTask(
    int resultIndex,
    Landscape *land, WaterSegment *seg, Scene &scene, Vector3Par offset,
    int x, int z, int lod, float dist, Vector3Val bCenter, float bRadius)
    :_resultIndex(resultIndex),_land(land),_seg(seg),_scene(scene),_x(x),_z(z),_lod(lod),_dist(dist),
    /*_groundPrimed(groundPrimed),*/
    /*_depthOnly(depthOnly),_zPrime(zPrime),_pos(pos),_minZDist(minZDist),_gridSize(gridSize),_zd(zd)*/
    _offset(offset)
  {
    // TODO: avoid code duplication - always use the task
    Shape &shape = land->_segCache->_wTable[lod];
    _shapeLock.Lock(&shape);
    scene.SelectAggLights(bCenter,bRadius,_lights);
  }
  
  virtual void operator () (IMTTContext *context, int thrIndex)
  {
    PROFILE_SCOPE_DETAIL_EX(rndMT,oSort);
    #if PIX_NAMED_CB_SCOPES
      // create ID which is likely to be unique (not clashing with object IDs)
      int debugId = 0x20000000+_x*0x1000+_z;
    #else
      int debugId = -1;
    #endif
    _cb = GEngine->StartCBRecording(_resultIndex,thrIndex,debugId);
    _land->WaterDrawHelper(_cb,_seg,_scene,_offset,_x,_z,_lod,_dist,_lights);
    GEngine->StopCBRecording(_cb,_resultIndex,"");
  }
  USE_FAST_ALLOCATOR
  
  friend void LandscapeManualCleanUp();
  friend void LandscapeSetManualCleanUp(bool manualCleanup);
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(Landscape::LandDrawTask)
DEFINE_FAST_ALLOCATOR(Landscape::WaterDrawTask)

// check sea textures in stages 4 and 5
struct SkyTexturesForSea
{
  static Texture *GetA(Shape *shape, TexMaterial *material) {return material->_stage[4]._tex;}
  static Texture *GetB(Shape *shape, TexMaterial *material) {return material->_stage[5]._tex;}
  
  static void SetA(Shape *shape, TexMaterial *material, Texture *tex){material->SetStageTexture(4,tex);}
  static void SetB(Shape *shape, TexMaterial *material, Texture *tex){material->SetStageTexture(5,tex);}
};

bool Landscape::PreloadWaterTextures(float dist2, float areaOverTex)
{
  AreaOverTex aot;
  aot.Set(0, areaOverTex);
  SkyTexturesContext<SkyTexturesForSea> skyOnSea(NULL,_seaMaterial);
  SkyTexturesContext<SkyTexturesForSea> skyOnShore(NULL,_shoreMaterial);
  if (_seaTexture && !_seaTexture->PreloadTexture(dist2,areaOverTex,false) ||
      _seaMaterial && !_seaMaterial->PreloadData(aot, dist2) ||
      _shoreMaterial && !_shoreMaterial->PreloadData(aot, dist2) ||
      _shoreFoamMaterial && !_shoreFoamMaterial->PreloadData(aot, dist2) ||
      _shoreWetMaterial && !_shoreWetMaterial->PreloadData(aot, dist2))
    return false;
  return true;
}

void Landscape::WaterDrawHelper(
  int cb, WaterSegment *seg, Scene &scene, Vector3Par offset, int x, int z, int lod, float dist, const LightList &lights
)
{
  Shape *shape = &_segCache->_wTable[lod];

  // calculate z2
  float minZMin = scene.GetCamera()->Near();
  float minDist = floatMax(dist,minZMin);
  float dist2 = Square(minDist);

  // make sure vertex buffer is not discarded while loading the textures
  VertexBufferLock lock(cb<0 ? shape : NULL);

  if (!shape->GetVertexBuffer())
  {
    // it is too late to create VB now - it already must be created
    return;
  }
  
  
  shape->PrepareTextures(dist2, NULL, 0, false);
  

#if _VBS3_LARGE_WATERSEG
  int waterArraySize = intMin(GetWaterSegmentSize(), MaxWaterSegmentSize);
  GEngine->SetWaterWaves(cb, seg->_waterDepth, waterArraySize+1, waterArraySize+1);
#else
  int waterSegmentSize = GetWaterSegmentSize();
  GEngine->SetWaterWaves(cb,seg->_waterDepth,waterSegmentSize+1,waterSegmentSize+1);
#endif

  #if _ENABLE_PERFLOG
  if (LogStatesOnce)
  {
    LogF("Draw water segment %d,%d, lod %d",x,z,lod);
  }
  #endif
  EngineShapeProperties props;
  DPRIM_STAT_SCOPE(cb<0 ? "Wat" : "",Wat);
  GEngine->SetInstanceInfo(cb, HWhite, 1);
  if (CheckDrawWater())
  {
    Vector3 offsetWave = offset+Vector3(0,_seaLevelWave,0);
    Matrix4 trans(MTranslation,offsetWave);
    // Prepare draw parameters
    DrawParameters dp;

	 //MIVA
    dp._canReceiveShadows = true; //false

    shape->Draw(cb, NULL, HWhite, props, lights, ClipAll, 0, trans, dist2, dp);
  }
}
      



void Landscape::DrawWater(const GridRectangle &bigRect, Scene &scene)
{
  /*
  // draw water diagnostics first
  #if _DEBUG
    for (int x=-20; x<=20; x+=4) for (int z=-20; z<=20; z+=4)
    {
      Vector3 dPos = scene.GetCamera()->Position()+Vector3(x,0,z);
      dPos[1] = GetSeaLevel(dPos.X(),dPos.Z());

      Ref<Object> obj=new ObjectColored(GScene->Preloaded(SphereModel),CreateObjectId());
      obj->SetPosition(dPos);
      obj->SetScale(0.3);
      obj->SetConstantColor(PackedColor(Color(1,0,1)));
      obj->Draw(0,ClipAll,*obj,obj);
    }
  #endif
  */
  PROFILE_SCOPE_GRF_EX(lDWat,land,SCOPE_COLOR_BLUE);

  if (!Glob.config._useCB)
  {
    // start capturing objects
    GEngine->StartCBScope(0,0);
  }
  
  SkyTexturesContext<SkyTexturesForSea> skyOnSea(NULL,_seaMaterial);
  SkyTexturesContext<SkyTexturesForSea> skyOnShore(NULL,_shoreMaterial);
  
  // calculate constants uniform for all segments
  // shape bounding information can be computed easily
  Vector3 minMax[2];
  int waterSegmentSize = GetWaterSegmentSize();
  const float waterSegRange = WaterGrid*waterSegmentSize;
  minMax[0] = Vector3(-waterSegRange*0.5f,-0.5f,-waterSegRange*0.5f);
  minMax[1] = Vector3(+waterSegRange*0.5f,+0.5f,+waterSegRange*0.5f);
  // TODO: avoid sqrt, use constant instead
  float bRadius=minMax[0].Size();
  //const float bRadius=sqrt(Square(waterSegRange*0.5f)*2+Square(0.5f));

  // Prepare draw parameters
  DrawParameters dp;
  dp._canReceiveShadows = false;



  GEngine->SetWaterDepth(
    _seaLevelWave,
    -0.5,-3, // shallow water transparency (note: visible water can never be under the ground)
    -1,-8, // wave effects
#if _VBS3_LARGE_WATERSEG
    waterSegmentSize+1, MaxWaterSegmentSize+1, WaterGrid
#else
    waterSegmentSize+1,waterSegmentSize+1,WaterGrid
#endif  
  );

#if _VBS3_NO_SEABED
  // when camera is submerged, it's not good to skip water segments
  bool underwater = scene.UnderWater();
#endif
  
  // TODO: traverse back to front, to minimize alpha artifacts?
  int nResults = 0;
  {
    PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);
    
    for (int x=bigRect.xBeg; x<bigRect.xEnd; x+=waterSegmentSize)
    for (int z=bigRect.zBeg; z<bigRect.zEnd; z+=waterSegmentSize)
    {
      GridRectangle sRect;
      sRect.xBeg=x,sRect.zBeg=z;
      sRect.xEnd=x+waterSegmentSize,sRect.zEnd=z+waterSegmentSize;
      
      int xw = x/waterSegmentSize, zw = z/waterSegmentSize;
      
      //static int XW = 28;
      //static int ZW = 104;

      //if (xw != XW || zw != ZW)
      //  continue;
      // clip water presence - note: repeating last segment is not accurate
      // we can get false positives this way when border is extended only

      float terrainLevelInside = FLT_MAX;      
      float terrainLevelOutside = FLT_MAX;     
      /*float waterRange = GetLandRange()*GetTerrainGrid()/(float)waterSegmentSize;
      GridRectangle wholeTerrain(0,0,waterRange,waterRange);*/
      // we could use sRect.IsOutside(wholeTerrain), but it appears to be broken - assert failing
      //DoAssert(sRect.IsOutside(wholeTerrain)==!(sRect&wholeTerrain).IsEmpty());
      
      if (xw >= 0 && xw < _waterInSeg.GetXRange() && zw >= 0 && zw < _waterInSeg.GetYRange())
      
      //if (!_terrain->_outsideData || !(sRect&wholeTerrain).IsEmpty())
      { // part inside
        /*saturate(xw,0,_waterInSeg.GetXRange()-1);
        saturate(zw,0,_waterInSeg.GetYRange()-1);*/
        terrainLevelInside = _waterInSeg(xw,zw)*(1.0f/WaterMapScale);
      }
      else if (_terrain->_outsideData/* && !sRect.IsInside(wholeTerrain)*/)
      { // part outside
        float scaleWaterToTerrain = WaterGrid*GetInvTerrainGrid();
        int xGMin = toIntFloor(x*scaleWaterToTerrain);
        int xGMax = toIntCeil((x+waterSegmentSize)*scaleWaterToTerrain);
        int zGMin = toIntFloor(z*scaleWaterToTerrain);
        int zGMax = toIntCeil((z+waterSegmentSize)*scaleWaterToTerrain);
        // ask outside terrain about height bounds
        terrainLevelOutside = _terrain->_outsideData->GetMinHeight(xGMin,zGMin,xGMax,zGMax);
      }
      
      float terrainLevel = floatMin(terrainLevelInside,terrainLevelOutside);
      
      // compare to actual tide level
      bool someWater = ( terrainLevel<=_seaLevel+MaxWave );
      
      // draw all water segments
#if _VBS3_NO_SEABED
      if (someWater || underwater)
#else
      if (someWater)
#endif
      {
        Ref<WaterSegment> seg = _segCache->WSegment(this,sRect);
        Vector3 offset(
          WaterGrid*0.5f*(sRect.xBeg+sRect.xEnd),
          0,
          WaterGrid*0.5f*(sRect.zBeg+sRect.zEnd)
        );
        
        // after drawing landscape draw water
        // make sure water shape/vertex buffer is ready

        // recreate water segment if necessary
        // TODO: select which one is needed
        // prototype: select LOD based on distance
        
        
        // water is generated around 0,0,0
        // See label WATER_TRANS
        Vector3Val bCenter = offset;

        // completely out of range (fog hidden), no need to render
        if (scene.GetCamera()->Position().Distance2(bCenter)>Square(Glob.config.horizontZ+bRadius))
          continue;

        Vector3 offsetWave = offset+Vector3(0,_seaLevelWave,0);
				Vector3 tminMax[2];
				tminMax[0] = offsetWave + minMax[0];
				tminMax[1] = offsetWave + minMax[1];
        if( !scene.GetCamera()->IsClippedByAnyPlane(bCenter,bRadius,tminMax) )
        {
          //Matrix4 trans(MTranslation,offsetWave);
          float dist=scene.GetCamera()->Position().Distance(bCenter)-bRadius;
          
          int lod = 0;
          if (LandCache::NWaterLods>1)
          {
          
            // replicate pixel shader LOD factor computations
            const Matrix4 &projMatrix = scene.GetCamera()->ProjectionNormal();
            float fov = InvSqrt(projMatrix(0,0)*projMatrix(1,1));
            float lodYFactor = 1-dist*fov*0.01f;
            // once we know the geometry is flat, we can start LODing it
            if (lodYFactor<=0)
            {
              float minZMin = scene.GetCamera()->Near();
              float minDist = floatMax(dist,minZMin);
              float screenSize = waterSegRange/minDist;

              const float invLog2 = 1.4426950409f;
              float logScreenSize = log(screenSize)*invLog2;

              static float adjustLod = -3.5f;
              float lodWanted = LandCache::NWaterLods-1-logScreenSize+adjustLod;

              lod = toInt(lodWanted);

              #if _ENABLE_CHEATS
                if( GInput.GetCheat3ToDo(DIK_T))
                {
                  MaxWaterLOD++;
                  if (MaxWaterLOD>LandCache::NWaterLods-1) MaxWaterLOD = 0;
                  // we need to recreate the LOD buffers on a LOD count switch
                  for (int i=0; i<LandCache::NWaterLods; i++)
                  {
                    _segCache->_wTable[i].ReleaseVBuffer();
                  }
                  // generate the best LOD so that we can check how many triangles is has
                  GenerateWaterSegment(0);
                  float nVertices = _segCache->_wTable[0].NVertex();
                  // segment size in faces
                  float sideResol = WaterGrid*waterSegmentSize/(sqrt(nVertices)-1);
                  GlobalShowMessage(500,"Water LOD count %d, resolution %.1f", MaxWaterLOD+1,sideResol);
                }
              #endif
              saturate(lod,0,MaxWaterLOD);
            }
          }
          GenerateWaterSegment(lod);
          
          if (Glob.config._useCB)
          {
            WaterDrawTask *task = new WaterDrawTask(nResults++,this,seg,scene,offset,x,z,lod,dist,bCenter,bRadius);
            SRef<IMicroJob> draw(task);
            GMicroJobsLandCB.Add(draw);
          }
          else
          {
            WaterDrawTask task(-1,this,seg,scene,offset,x,z,lod,dist,bCenter,bRadius);
            task(NULL,-1);
          }
          
        }
      }
    }
  }

  if (Glob.config._useCB)
  {
    GMicroJobsLandCB.Execute(nResults);
  }
  else
  {
    GEngine->EndCBScope();
  }
}


static inline float InvertColor(float x)
{
  if (x>0.01f) 
  {
    return 1/x;
  }
  else
  {
    return 100;
  }
}
static inline float FastSqrt(float x)
{
  if (x<=0) return 0;
  return x * InvSqrt(x);
}

/// store additional info together with a type
template <class Type, class Info>
struct StoreInfo
{
  Type *item;
  Info info;
  
  StoreInfo(){}
  StoreInfo(Type *i, Info d):item(i),info(d){}
  
  bool operator == (const StoreInfo &with) const {return item==with.item;}
  
  ClassIsSimple(StoreInfo);
};

/// traits for StoreInfo used in FindArrayKey
template <class Type, class Info>
struct StoreInfoKeyTraits
{
  typedef Type *KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  /// get a key from an item
  static KeyType GetKey(const StoreInfo<Type,Info> &a) {return a.item;}
};


/*!
\patch 5156 Date 5/12/2007 by Ondra
- Fixed: Reduced texture memory requirements for little varied parts of the terrain.
*/

void Landscape::PrepareGroundTextures(LandSegment *seg, float zMin, Vector3Par pos, int lod)
{
  // TODO: handle LODs
  PROFILE_SCOPE_EX(lDPGT,*);
  
  const float precisionThold = 0.7f;

  // in certain distance per-grid precision is no longer needed
  // we can process whole segment sections instead of triangles, which is much faster

  // if there is only one texture and one material, we can process whole segment at once with no precision loss

  LandSegment::SegLod &segLod = seg->_lod[lod];
  Shape &shape = segLod._table;
  if (shape.NSections()<=1)
  {
    // z = 0 can never be true
    shape.PrepareTextures(Square(zMin), NULL, 0, false);
    return;
  }
  // if nearest point is negative, detailed processing is certainly needed
  if (zMin>0)
  {
    // what distance precision we consider acceptable
    // check difference between nearest point and farthest point
    /*
    float distNearCoef = 1.0f/zMin;
    float distFarCoef = 1.0f/(zMin+GetLandGrid()*H_SQRT2*GetLandSegmentSize());
    Assert(distFarCoef<distNearCoef);
    if (distFarCoef/distNearCoef>=precisionThold)
    */
    // optimized to avoid division:
    //if ((1.0f/(zMin+GetLandGrid()*H_SQRT2*GetLandSegmentSize())/(1.0f/zMin))>=precisionThold)
    //if (zMin/(zMin+GetLandGrid()*H_SQRT2*GetLandSegmentSize())>=precisionThold)
    if (zMin>=precisionThold*(zMin+GetLandGrid()*H_SQRT2*GetLandSegmentSize()))
    {
      // TODO: after some limit distance preload is no longer important,
      // as textures will not show any detail anyway
  
      // far enough - per segment processing
      shape.PrepareTextures(Square(zMin), NULL, 0, false);
      return;
    }
  }
  
  // for near more accurate solution is needed
  // walk through all textures in the near vicinity
  
  seg->PrepareTexturesBasedOnIndex(this,seg->_index,seg->_rect,pos,zMin);

  // road textures need to be loaded only when in range
  if (zMin<Glob.config.roadsZ)  
  {
    seg->PrepareTexturesBasedOnIndex(this,seg->_indexRoads,seg->_rect,pos,zMin);

    // procedural textures for the grass
    // we may need to load them now
    // TODO: correct loading - based on distance

    if (DrawGrassLayer)
    {
      const Camera &camera=*GScene->GetCamera();
      float scrAreaOTex = Square(GetLandGrid())*camera.GetAOTFactor();
      for (int i=segLod._geometry[GTGrass]._beg; i<segLod._geometry[GTGrass]._end; i++)
      {
        const ShapeSection &sec = shape.GetSection(i);
        const TexMaterial *mat = sec.GetMaterialExt();
        if (!mat) continue;
        int nPasses = (mat->_stageCount+1-LandMatStageLayers)/LandMatStagePerLayer;
        for (int s=0; s<nPasses; s++)
        {
          Texture *tex1 = mat->_stage[LandMatStageLayers+s*LandMatStagePerLayer+LandMatNoInStage]._tex;
          if (tex1)
          {
            // mapping is 1 to GetLandGrid() in both directions
            tex1->PrepareMipmapLevelScr(Square(zMin),scrAreaOTex,false);
          }
        }
      }
    }
  }
}

void Landscape::PrepareRoadTextures(LandSegment *seg, float zMin, Vector3Par pos, int lod)
{
  PROFILE_SCOPE_EX(lDPRT,*);
  
  const float precisionThold = 0.7f;

  Shape &shape = seg->_lod[lod]._roads;
  // if there is only one texture and one material, we can process whole segment at once with no precision loss
  if (shape.NSections()<=1)
  {
    // z = 0 can never be true
    float minZMin = Square(GScene->GetCamera()->Near());
    float z2 = zMin<=minZMin ? Square(minZMin) : Square(zMin);
    shape.PrepareTextures(z2, NULL, 0, false);
    return;
  }
  // if nearest point is negative, detailed processing is certainly needed
  else if (zMin>0)
  {
    // what distance precision we consider acceptable
    if (zMin>=precisionThold*(zMin+GetLandGrid()*H_SQRT2*GetLandSegmentSize()))
    {
      // far enough - per segment processing
      shape.PrepareTextures(Square(zMin), NULL, 0, false);
      return;
    }
  }
  
  // for near more accurate solution is needed
  // walk through all textures in the near vicinity
  
  seg->PrepareTexturesBasedOnIndex(this,seg->_indexRoads,seg->_rect,pos,zMin);
}

void LandSegment::PrepareTexturesBasedOnIndex(
  Landscape *l, TexMatIndex &index, const GridRectangle &rect, Vector3Par pos, float zMin
)
{
  // TODO: use optimized texture + aot index
  if (index._textures.Size()==0) return;
  int dataShift = l->GetSubdivLog();
  
  // PrepareTexture can be quite slow
  // textures are heavily repeated - we gather info first, and PrepareTexture later

  // the minimum distance for individual textures from _textures
  AutoArray<float, MemAllocLocal<float,128> > textures;
  
  textures.Resize(index._textures.Size());
  for (int i=0; i<textures.Size(); i++) textures[i] = FLT_MAX;

  // pre-compute commonly used quantities  
  int dataShiftHalf = intMax(dataShift-1,0);
  int dataOffsetHalf = 1<<dataShiftHalf;
  float landGrid = l->GetLandGrid();
  for (int xx=rect.xBeg; xx<rect.xEnd; xx++)
  for (int zz=rect.zBeg; zz<rect.zEnd; zz++)
  {
    LandSegment::TexMPassInfo &grid = index._grid(xx-rect.xBeg,zz-rect.zBeg);
    if (grid._pass.Size()==0) continue;
    // check distance to all four corners?
    // check distance to center point only?
    // if we are using only the center point, why sampling all four corners anyway?
    // Y of the center point could do quite well
    int centerXX = (xx<<dataShift)+dataOffsetHalf;
    int centerZZ = (zz<<dataShift)+dataOffsetHalf;
    float centerY = l->ClippedDataXZ(centerXX,centerZZ);
    //float y00,y01,y10,y11;
    //l->GetRectY(xx<<dataShift,zz<<dataShift,1<<dataShift,1<<dataShift,y00,y01,y10,y11);
    //float centerY = (y00+y01+y10+y11)*0.25f;
    Vector3 center( (xx+0.5f)*landGrid, centerY, (zz+0.5f)*landGrid);
    float dist2 = center.Distance2Inline(pos);
    float dist = FastSqrt(dist2)-landGrid*(H_SQRT2/2);
    float z2 = dist>0 ? Square(dist) : 0;
    for (int i=0; i<grid._pass.Size(); i++)
    {
      int texIndex = grid._pass[i]._tex;
      if (texIndex>=0)
      {
        saturateMin(textures[texIndex],z2);
      }
    }
  }

  float aotFactor = GScene->GetCamera()->GetAOTFactor();
  float minZMin2 = Square(zMin);
  for(int i=0; i<textures.Size(); i++)
  {
    Texture *tex = index._textures[i]._tex;
    float aot = index._textures[i]._aot;
    float z2 = textures[i];
    Assert(z2<FLT_MAX);
    saturateMax(z2,minZMin2);
    tex->PrepareTextureScr(z2,0,aot*aotFactor);
  }
}

/*!
\patch 5160 Date 5/22/2007 by Ondra
- Optimized: Terrain rendering uses less CPU.
*/

void Landscape::DrawShore(const GridRectangle &bigRect, Scene &scene, GeometryType gt)
{
  GEventLogger.Add(bigRect,"DrawShore");
  // Don't draw shore in thermal vision
  if (GEngine->GetThermalVision()) return;

  Vector3Val pos = scene.GetCamera()->Position();
  Vector3Val dir = scene.GetCamera()->Direction();

  // Shape properties
  EngineShapeProperties selSections;
  EngineShapeProperties propsAllSections;

  // we can be almost sure the nearest ground point is below the camera
  // in some extreme cases (very steep hills) this may fail slightly,
  // but the error should be hard to notice

  float minZDist = floatMax(pos.Y()-SurfaceY(pos),scene.GetCamera()->Near());

  PROFILE_SCOPE_GRF_EX(lDSho,land,SCOPE_COLOR_YELLOW);
  int landSegmentSize = _segCache->_landSegmentSize;

  if (bigRect.IsEmpty()) return;

//#if _VBS3_NO_SEABED  
//  bool underwater = scene.UnderWater();
//#endif

  // flexible  loop - minor/major can be x or z
  bool zMajor = fabs(dir.Z())>fabs(dir.X());
  int zStep = dir.Z()<0 ? -landSegmentSize : +landSegmentSize;
  int xStep = dir.X()<0 ? -landSegmentSize : +landSegmentSize;

  int segGen = 0, segUse = 0;
  // traverse land grid
  FlexibleLoop loop(bigRect.xBeg,bigRect.zBeg,bigRect.xEnd,bigRect.zEnd,xStep,zStep,zMajor);

  FLEXIBLE_LOOP(loop,x,z)

//#if _VBS3_NO_SEABED
//    if (!underwater && IsSeabedSegment(x, z)) continue;
//#endif

    GridRectangle sRect;
    sRect.xBeg=x,sRect.zBeg=z;
    sRect.xEnd=x+landSegmentSize,sRect.zEnd=z+landSegmentSize;

    if (_yRange.GetXRange()!=0)
    {
      Vector3 minMaxEarly[2];
      GetSegmentMinMaxBox(sRect,minMaxEarly);
      
      Vector3 bCenter = (minMaxEarly[0]+minMaxEarly[1])*0.5f;
      float bRadius = minMaxEarly[0].Distance(minMaxEarly[1])*0.5f;
      if(scene.GetCamera()->IsClippedByAnyPlane(bCenter,bRadius,minMaxEarly) )
        continue;
      
      // completely out of range (fog hidden), no need to render
      if (scene.GetCamera()->Position().Distance2(bCenter)>Square(Glob.config.horizontZ+bRadius))
        continue;
    }
      
    PROFILE_SCOPE_EX(lDShS,land);
    // during landscape rendering many vertex buffers may be created
    // regular garbage collecting is necessary
    FreeOnDemandGarbageCollect(1024*1024,256*1024);
    
    int lod = 0;
    if (LandSegment::NLods>1)
    {
      float dist = _segCache->MinDistRect(this,pos,minZDist,sRect);
      float lodWanted = _segCache->LandLODWanted(this,dist);

      lod = toIntFloor(lodWanted);
      saturate(lod,0,_segCache->NLods()-1);
    }

    segGen++;
    GEventLogger.Add(sRect,"  shore at");
    Ref<LandSegment> seg=_segCache->Segment(this,sRect,lod);

    // terrain shape drawing
    {
      LandSegment::SegLod &segLod = seg->_lod[lod];
      Shape &shape = segLod._table;
      // often there is no shore (everything clipped out)
      // in such case there is no need to load textures for it or perform any other management
      if (segLod._geometry[gt]._beg>=segLod._geometry[gt]._end) continue;
      
  #if _ENABLE_PERFLOG
      if (LogStatesOnce)
      {
        LogF("Draw landscape segment %d,%d, lod %d",x,z,lod);
      }
  #endif
      // we assume bCenter and bRadius for roads is the same as for terrain
      Vector3Val bCenter=shape.BSphereCenter()+seg->Offset();
      float bRadius=shape.BSphereRadius();

      Vector3 offset = seg->Offset();
			Vector3 tminMax[2];
			tminMax[0] = offset + shape.MinMax()[0];
			tminMax[1] = offset + shape.MinMax()[1];
      if( !scene.GetCamera()->IsClippedByAnyPlane(bCenter,bRadius,tminMax) )
      {
        segUse++;
        // make sure vertex buffer is not discarded while loading the textures
        VertexBufferLock lockShape(&shape);

				Matrix4 trans(MTranslation,offset);

        int cb = -1;
        
        // select only most important light
        LightList work;
        const LightList &lights = scene.SelectAggLights(bCenter,bRadius,work);
        float zd = floatMax(pos.Distance(bCenter)-bRadius,minZDist);
        float zd2 = Square(zd);

        // no need to prepare textures - they were already prepared for corresponding DrawGround
        //PrepareGroundTextures(seg,zd,pos,lod);

#if 0 // No need to set land shadows for shore - it doesn't use it anyway
        // Prepare land shadow data and pass them to the engine
        {
          const int shadowSegmentSize = 12;
          DoAssert(landSegmentSize<=shadowSegmentSize);
          float shadow[(shadowSegmentSize+1)*(shadowSegmentSize+1)];
          int i = 0;
          for (int zz=0; zz<landSegmentSize+1; zz++) for (int xx=0; xx<landSegmentSize+1; xx++)
          {
            shadow[i++] = scene.GetLandShadowDepth(x+xx,z+zz);
          }
          GEngine->SetLandShadow(shadow, landSegmentSize+1, landSegmentSize+1, GetLandGrid());
        }
#endif

        // In case of shore prepare matrix that maps xz model-space position to the same uv like in water
        Ref<TexMaterial> material;
        switch (gt)
        {
        case GTShore:
          material = _shoreMaterial;
          break;
        case GTShoreFoam:
          material = _shoreFoamMaterial;
          break;
        }
        if (material.NotNull())
        {
          // Get the shore2UV matrix
          Matrix4 shore2UV;
          {
            float offX = fmod(offset.X() * 1.0f / WaterGrid, 1.0f);
            float offZ = fmod(offset.Z() * 1.0f / WaterGrid, 1.0f);
            shore2UV.SetScale(1.0f / WaterGrid, 1.0f / WaterGrid, 1.0f);
            shore2UV.SetPosition(Vector3(offX, offZ, 0));
          }

          // Set the matrix to the shore material
          Matrix4 &m = material->_texGen[1]._uvTransform;
          m.SetDirectionAside(Vector3(shore2UV.DirectionAside().Y(),  shore2UV.DirectionAside().X(),  shore2UV.DirectionAside().Z()));
          m.SetDirectionUp(   Vector3(shore2UV.DirectionUp().Y(),     shore2UV.DirectionUp().X(),     shore2UV.DirectionUp().Z()));
          m.SetDirection(     Vector3(shore2UV.Direction().Y(),       shore2UV.Direction().X(),       shore2UV.Direction().Z()));
          m.SetPosition(      Vector3(shore2UV.Position().Y(),        shore2UV.Position().X(),        shore2UV.Position().Z()));

          // Set the UV sources
          material->_texGen[1]._uvSource = UVTexShoreAnim;
          material->_texGen[2]._uvSource = UVTexShoreAnim;
          material->_texGen[3]._uvSource = UVTexShoreAnim;

          // Inform engine the material has been changed
          GEngine->MaterialHasChanged(cb);
        }

        // Draw the segment
        GEngine->SetInstanceInfo(cb, HWhite, 1);

        DPRIM_STAT_SCOPE(cb<0 ? "Shr" : "",Shr);

        if (CheckDrawTerrain() && shape.LockVertexBuffer())
        {
          GEngine->RefreshBuffer(shape);
          // Select simple geometry section interval
          selSections._begSection = segLod._geometry[gt]._beg;
          selSections._endSection = segLod._geometry[gt]._end;

          // Prepare draw parameters
          DrawParameters dp;
          dp._canReceiveShadows = false;

          shape.Draw(cb, NULL, HWhite, selSections, lights, ClipAll, 0, trans, zd2, dp);
        }
      }
    }
  FLEXIBLE_LOOP_END

  //DIAG_MESSAGE(1000,"Shore segments %d, used %d",segGen,segUse);
}

#if _ENABLE_CHEATS
extern int ZPriming = 1;
#else
extern int ZPriming = 1;
#endif

static float HTGrass = 60.0f * 60.0f * 0.1f;
static float HTSoil = 60.0f * 60.0f * 10.0f; // This is actually not used in the shader (terrain uses only max value - HTStone), but it could be
static float HTStone = 60.0f * 60.0f * 25.0f;
static float HTRoadMin = 60.0f * 60.0f * 2.0f;
static float HTRoadMax = 60.0f * 60.0f * 10.0f;

// terrain shape drawing
void Landscape::LandDrawHelper(
  int cb, LandSegment *seg, int x, int z, const GridRectangle &groundPrimed, Scene &scene,
  bool depthOnly, bool zPrime, int lod, Vector3Par pos, float minZDist, float gridSize, float zd,
  const LightList &lights
)
{
  LandSegment::SegLod &segLod = seg->_lod[lod];
  Shape &shape = segLod._table;

  #if _ENABLE_PERFLOG
  if (LogStatesOnce)
  {
    LogF("Draw landscape segment %d,%d, lod %d",x,z,lod);
  }
  #endif
  // we assume bCenter and bRadius for roads is the same as for terrain

  Matrix4 trans(MTranslation,seg->Offset());

  // Init the TI parameters
  DrawParameters dpTI = DrawParameters::_default;
  dpTI._htMin = HTSoil;
  dpTI._htMax = HTStone;
  dpTI._afMax = 0.0f;
  dpTI._mfMax = 0.0f;
  dpTI._mFact = 0.0f;
  dpTI._tBody = 0.0f;


  // make sure vertex buffer is not discarded while loading the textures
  VertexBufferLock lockShape(cb<0 ? &shape : NULL);
  
  if (!shape.LockVertexBuffer())
  {
    // it is too late to create VB now - it already must be created
    return;
  }
  
  float zd2 = Square(zd);
  
  if (!depthOnly && !zPrime)
  {
    PrepareGroundTextures(seg,zd,pos,lod);
  }
  // note: for z-priming we need to have prepare alpha grass mask ready
  // however, texture are loaded once per frame anyway, therefore one PrepareGroundTextures is enough

  // Prepare land shadow data and pass them to the engine
  {
#if !_VBS3_LAND_SHADOW
    int landSegmentSize = _segCache->_landSegmentSize;

    const int shadowSegmentSize = 12;
    float shadow[(shadowSegmentSize+1)*(shadowSegmentSize+1)];
#if _VBS3_LARGE_LANDSEG
    if (landSegmentSize > shadowSegmentSize)
    {
      // large land segment: we need to scale the shadow array :-(
      int i = 0;
      const int ssize = shadowSegmentSize;
      if (landSegmentSize == shadowSegmentSize * 2) // common case
      {
        for (int zz=0; zz<=ssize; zz++) for (int xx=0; xx<=ssize; xx++)
        {
          shadow[i++] = scene.GetLandShadowDepth(x + 2*xx, z + 2*zz);
        }
        GEngine->SetLandShadow(cb, shadow, ssize+1, ssize+1, 2*GetLandGrid());
      }
      else
      {
        float scale = (float) landSegmentSize / ssize;
        for (int zz=0; zz<=ssize; zz++) for (int xx=0; xx<=ssize; xx++)
        {
          shadow[i++] = scene.GetLandShadowDepth(x + (int) (xx * scale + 0.5), z + (int) (zz * scale + 0.5));
        }
        GEngine->SetLandShadow(cb, shadow, ssize+1, ssize+1, GetLandGrid() * scale);
      }
    }
    else
#endif
    {
      DoAssert(landSegmentSize<=shadowSegmentSize);
      int i = 0;
      if (!depthOnly)
      {
        for (int zz=0; zz<landSegmentSize+1; zz++) for (int xx=0; xx<landSegmentSize+1; xx++)
        {
          shadow[i++] = scene.GetLandShadowDepth(x+xx,z+zz);
        }
        GEngine->SetLandShadow(cb, shadow, landSegmentSize+1, landSegmentSize+1, GetLandGrid());
      }
      else
      {
        // In case of depth rendering, don't call the GetLandShadowDepth method (data need not to be prepared yet) - just set the shadows somehow
        for (int zz=0; zz<landSegmentSize+1; zz++) for (int xx=0; xx<landSegmentSize+1; xx++)
        {
          shadow[i++] = 0.0f;
        }
        GEngine->SetLandShadow(cb, shadow, landSegmentSize+1, landSegmentSize+1, GetLandGrid());
      }
    }
#endif
  }

  // we need to update the LRU
  GEngine->RefreshBuffer(shape);
  
  // Draw the segment
  GEngine->SetInstanceInfo(cb, HWhite, 1);
  
  DPRIM_STAT_SCOPE(cb<0 ? "Gnd" : "",Gnd);

  //const Camera *camera = scene.GetCamera();
  //float areaK = camera->InvLeft()*camera->InvTop()*scene.GetEngineArea();
  //float squareMeterInPixels = areaK / zd2;

  // render grass layer before the landscape, because it has a good chance of hiding it
  if (zd<Glob.config.roadsZ && !depthOnly /*&& !zPrime*/ && gridSize<49
    && !GEngine->GetThermalVision()
    )
  
  {
    if (DrawGrassLayer)
    {
      // alpha fog not really needed here
      GEngine->SetAlphaFog(cb, Glob.config.roadsZ,Glob.config.roadsZ);
      // we use dedicated constants instead (see EngineProperties::GetGrassPars)

      // terrain grass layer rendering
      // Select basic geometry section interval
      EngineShapeProperties selSections;

      selSections._begSection = segLod._geometry[GTGrass]._beg;
      selSections._endSection = segLod._geometry[GTGrass]._end;

      if (!depthOnly && !zPrime)
      {
        bool primingDone = Position(x,z).IsInside(groundPrimed);
        GEngine->SetZPrimingDone(cb,primingDone);
      }
      
      // for grass layer only - receive all shadows
      Ref<Texture> backup;
      Texture *tex = GPreloadedTextures.New(TextureHalf);
      for (int s=selSections._begSection; s<selSections._endSection; s++)
      {
        TexMaterial *mat = shape.GetSection(s).GetMaterialExt();
        // replace primary textures with default detail texture
        backup = mat->_stage[LandMatStageMidDet]._tex;
        mat->_stage[LandMatStageMidDet]._tex = tex;
      }
      
      int spec = NoBackfaceCull;
      shape.Draw(cb, NULL, HWhite, selSections, lights, ClipAll, spec, trans, zd2, dpTI); // _VBS3_TI

      for (int s=selSections._begSection; s<selSections._endSection; s++)
      {
        TexMaterial *mat = shape.GetSection(s).GetMaterialExt();
        // replace primary textures with NULLs
        mat->_stage[LandMatStageMidDet]._tex = backup;
      }

      if (!depthOnly && !zPrime)
      {
        GEngine->SetZPrimingDone(cb,false);
      }
      
    }
  }
  
  static bool allowSimple = true;
#if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_1))
  {
    allowSimple = !allowSimple;
  }
#endif
  if (CheckDrawTerrain())
  {
    // we prefer not linking to clutter settings
    // even if ground clutter is disabled, we still want to see detail textures
    GEngine->SetAlphaFog(cb, _fullDetailDist,_noDetailDist);
    bool simple = allowSimple && zd>Glob.config.roadsZ || depthOnly || zPrime;

    // Calculate number of simple sections
    int simpleCount = segLod._geometry[GTSimple]._end - segLod._geometry[GTSimple]._beg;

    if (!depthOnly && !zPrime)
    {
      bool primingDone = Position(x,z).IsInside(groundPrimed);
      GEngine->SetZPrimingDone(cb,primingDone);
    }
    // if simple section is empty, it means it is not present and we should use full version
    if (simple && simpleCount > 0)
    {
      EngineShapeProperties selSections;
      // Select simple geometry section interval
      selSections._begSection = segLod._geometry[GTSimple]._beg;
      selSections._endSection = segLod._geometry[GTSimple]._end;

      shape.Draw(cb, NULL, HWhite, selSections, lights, ClipAll, 0, trans, zd2,dpTI);
    }
    else
    {
      EngineShapeProperties selSections;
      // Select basic geometry section interval
      selSections._begSection = segLod._geometry[GTBasic]._beg;
      selSections._endSection = segLod._geometry[GTBasic]._end;

      shape.Draw(cb, NULL, HWhite, selSections, lights, ClipAll, 0, trans, zd2,dpTI);
    }
    if (!depthOnly && !zPrime)
    {
      GEngine->SetZPrimingDone(cb,false);
    }
  }

  if (zd<Glob.config.roadsZ && CheckDrawRoads() && !depthOnly && !zPrime)
  {
    Shape &roads = segLod._roads;
    // we do not want to call this for segments where there are no roads
    if (cb<0)
    {
      seg->GenerateRoad(lod,zd,this);
    }
    if (roads.NSections()>0)
    {
      DrawParameters dpTIRoad = DrawParameters::_default;
      dpTIRoad._htMin = HTRoadMin;
      dpTIRoad._htMax = HTRoadMax;
      dpTIRoad._afMax = 0.0f;
      dpTIRoad._mfMax = 0.0f;
      dpTIRoad._mFact = 0.0f;
      dpTIRoad._tBody = 0.0f;

      // make sure vertex buffer is not discarded while loading the textures
      VertexBufferLock lockRoads(cb<0 ? &roads : NULL);

      // it is too late to create VB now - it already must be created
      if (roads.GetVertexBuffer())
      {
        GEngine->RefreshBuffer(roads);
        float startRoadAlpha = floatMax(Glob.config.roadsZ*0.2f,400.0f);
        GEngine->SetAlphaFog(cb, startRoadAlpha,Glob.config.roadsZ);
        PrepareRoadTextures(seg,zd,pos,lod);

        // ??? priming is never done for roads - see condition above
        bool primingDone = Position(x,z).IsInside(groundPrimed);
        GEngine->SetZPrimingDone(cb,primingDone);
        
        EngineShapeProperties propsAllSections;
        
        roads.Draw(cb, NULL, HWhite, propsAllSections, lights, ClipAll, 0, trans, zd2, dpTIRoad);

        GEngine->SetZPrimingDone(cb,false);
      }
    
    }
  }

}

/*!
@param depthOnly when rendering depth only, some optimizations may be done (parts can be skipped)
@param zPrime true when z-priming (depthOnly must be false)

\patch_internal 1.21 Date 8/16/2001 by Ondra.
- Changed: Landscape drawing structure and object ordering changed
to better suit grass rendering.
\patch_internal 1.43 Date 1/29/2002 by Ondra
- Fixed: Support for high resolution terrain textures.
*/

void Landscape::DrawGround(const GridRectangle &bigRect, const GridRectangle &groundPrimed, Scene &scene, bool depthOnly, bool zPrime
#if _HELISIM_RENDER_TO_TEXTURE
                           , bool isR2T
#endif
                           )
{
  GEventLogger.Add(bigRect,"DrawGround");
  Assert(!depthOnly || !zPrime);
  //Debug Roads: regenerate segments every frame
  //_segCache->_segments.Clear();
  float viewDist,gridSize;
  GWorld->GetCurrentTerrainSettings(viewDist,gridSize,GWorld->GetMode()
#if _HELISIM_RENDER_TO_TEXTURE
,isR2T
#endif
    );

  Vector3Val pos = scene.GetCamera()->Position();
  Vector3Val dir = scene.GetCamera()->Direction();

  // Shape properties
  EngineShapeProperties selSections;
  EngineShapeProperties propsAllSections;

  // we can be almost sure the nearest ground point is below the camera
  // in some extreme cases (very steep hills) this may fail slightly,
  // but the error should be hard to notice
  
  float minZDist = floatMax(pos.Y()-SurfaceY(pos),scene.GetCamera()->Near());

  PROFILE_SCOPE_GRF_EX(lDGnd,land,SCOPE_COLOR_YELLOW);
  int landSegmentSize = _segCache->_landSegmentSize;
  
  if (bigRect.IsEmpty()) return;
  
  if (PROFILE_SCOPE_NAME(lDGnd).IsActive())
  {
    PROFILE_SCOPE_NAME(lDGnd).AddMoreInfo(Format("%d,%d..%d,%d",bigRect.xBeg,bigRect.zBeg,bigRect.xEnd,bigRect.zEnd));
  }
  
  if (!Glob.config._useCB)
  {
    // start capturing objects
    GEngine->StartCBScope(0,0);
  }
  
#if _VBS3_NO_SEABED  
  bool underwater = scene.UnderWater();
#endif

  // flexible  loop - minor/major can be x or z
  bool zMajor = fabs(dir.Z())>fabs(dir.X());
  int zStep = dir.Z()<0 ? -landSegmentSize : +landSegmentSize;
  int xStep = dir.X()<0 ? -landSegmentSize : +landSegmentSize;
  
  int nResults = 0;
  int segGen = 0, segUse = 0;
  // scope capture inner part of MergeInstances only - can be seen as micro-job preparation in PIX
  {
    PROFILE_SCOPE_DETAIL_GRF_EX(mjPrp,oSort,0);
    // traverse land grid
    FlexibleLoop loop(bigRect.xBeg,bigRect.zBeg,bigRect.xEnd,bigRect.zEnd,xStep,zStep,zMajor);
    
    FLEXIBLE_LOOP(loop,x,z)

#if _VBS3_NO_SEABED
      if (!underwater && IsSeabedSegment(x, z)) continue;
#endif
      
      GridRectangle sRect;
      sRect.xBeg=x,sRect.zBeg=z;
      sRect.xEnd=x+landSegmentSize,sRect.zEnd=z+landSegmentSize;
      
      if (_yRange.GetXRange()!=0)
      {
        Vector3 minMaxEarly[2];
        GetSegmentMinMaxBox(sRect,minMaxEarly);
        
        Vector3 bCenter = (minMaxEarly[0]+minMaxEarly[1])*0.5f;
        float bRadius = minMaxEarly[0].Distance(minMaxEarly[1])*0.5f;
        if(scene.GetCamera()->IsClippedByAnyPlane(bCenter,bRadius,minMaxEarly) )
          continue;
        
        // completely out of range (fog hidden), no need to render
        if (scene.GetCamera()->Position().Distance2(bCenter)>Square(Glob.config.horizontZ+bRadius))
          continue;
      }

#if _VBS3_NO_SEABED
     // if (IsSeabedSegment(x, z)) continue;
#endif
      
      // during landscape rendering many vertex buffers may be created
      // regular garbage collecting is necessary
      FreeOnDemandGarbageCollect(1024*1024,256*1024);
      
      int lod = 0;
      float dist = _segCache->MinDistRect(this,pos,minZDist,sRect);
      if (LandSegment::NLods>1)
      {
        float lodWanted = _segCache->LandLODWanted(this,dist);

        lod = toIntFloor(lodWanted);
        saturate(lod,0,_segCache->NLods()-1);
      }
      
      GEventLogger.Add(sRect,"  ground");
      Ref<LandSegment> seg=_segCache->Segment(this,sRect,lod);

      segGen++;

      // terrain shape drawing
      Shape &shape = seg->_lod[lod]._table;
      Vector3Val bCenter=shape.BSphereCenter()+seg->Offset();
      float bRadius=shape.BSphereRadius();

			Vector3 minMax[2];
			minMax[0] = shape.MinMax()[0] + seg->Offset();
			minMax[1] = shape.MinMax()[1] + seg->Offset();
			
			// note: minMax here includes seams, min. y is therefore lower than in minmaxEarly
      if( !scene.GetCamera()->IsClippedByAnyPlane(bCenter,bRadius,minMax) )
      {
#ifdef GEOM_OCCLUDERS
				if(CheckAgainstOccluders(enf::AAB(minMax[0], minMax[1])))
#endif
				{

        segUse++;
        float zd = floatMax(pos.Distance(bCenter)-bRadius,dist,minZDist);
        // select only most important light
        // no need to select lights when doing priming only
        if (Glob.config._useCB)
        {
          LandDrawTask *task = new LandDrawTask(
            nResults++,this,seg,x,z,groundPrimed,scene,depthOnly,zPrime,lod,pos,minZDist,gridSize,zd,bCenter,bRadius
          );
          SRef<IMicroJob> draw(task);
          GMicroJobsLandCB.Add(draw);
        }
        else
        {
          LandDrawTask task(
            -1,this,seg,x,z,groundPrimed,scene,depthOnly,zPrime,lod,pos,minZDist,gridSize,zd,bCenter,bRadius
          );
          task(NULL,-1);
        }
				}
      }
    FLEXIBLE_LOOP_END
  }
  if (Glob.config._useCB)
  {
    GMicroJobsLandCB.Execute(nResults);
  }
  else
  {
    GEngine->EndCBScope();
  }

  //DIAG_MESSAGE(1000,"Segments %d, used %d",segGen,segUse);
  GEngine->SetAlphaFog(-1, Glob.config.roadsZ,Glob.config.roadsZ);
}


void Landscape::GetRoadList( int x, int z, StaticArrayAuto< InitPtr<Object> > &roadways )
{
  // Check if there are any roadway objects
  int xMin = x-1, xMax = x+1;
  int zMin = z-1, zMax = z+1;
  if (xMin<0) xMin = 0; if (xMax>GetLandRangeMask()) xMax = GetLandRangeMask();
  if (zMin<0) zMin = 0; if (zMax>GetLandRangeMask()) zMax = GetLandRangeMask();
  for( int oz=zMin; oz<=zMax; oz++ ) for( int ox=xMin; ox<=xMax; ox++ )
  {
    if (!IsSomeRoadway(ox,oz)) continue;
    const ObjectListUsed &list=UseObjects(ox,oz);
    int nObj = list.Size();
    for (int i=0; i<nObj; i++)
    {
      Object *obj = list[i];
#if _VBS3_CRATERS_LATEINIT
      if (obj->HasRoadway())
#else
      if (obj->GetShape() && obj->GetShape()->FindRoadwayLevel()>=0 && obj->GetRadius()>IgnoreSmallRoadways)
#endif
      {
        roadways.Add(obj);
      }
    }
  }
}

#if 0 // _PROFILE
#pragma optimize("",off)
#endif

const SurfaceInfo *Landscape::CheckRoadway( const StaticArrayAuto< InitPtr<Object> > &roadways, Vector3Par pos) const
{
  PROFILE_SCOPE_DETAIL_EX(lndCR,*);
  // if there is any roadway on top of the landscape, assume there is no grass
  for( int ri=0; ri<roadways.Size(); ri++ )
  {
    Object *obj=roadways[ri];
    // check bounding box collision
    LODShape *lShape=obj->GetShape();
#if _VBS3_CRATERS_LATEINIT
    if (!lShape || lShape->FindRoadwayLevel() < 0 || obj->GetRadius<=IgnoreSmallRoadways) continue;
#endif
    float oRad=lShape->BoundingSphere();
    float distXZ2=(obj->FutureVisualState().Position()-pos).SquareSizeXZ();
    if( distXZ2>oRad*oRad ) continue;

    Matrix4Val invTransform=obj->FutureVisualState().GetInvTransform();
    Vector3 modelPos = invTransform.FastTransform(pos);

    const Shape *shape=lShape->RoadwayLevel();

    PROFILE_SCOPE_DETAIL_EX(lnCRO,*);
  #if _ENABLE_PERFLOG>1
    if (PROFILE_SCOPE_NAME(lnCRO).IsActive())
    {
      PROFILE_SCOPE_NAME(lnCRO).AddMoreInfo(lShape->GetName());
    }
    #endif

    bool animated = obj->IsAnimated(lShape->FindRoadwayLevel())>=AnimatedGeometry;
    AnimationContextStorageRoadway storage;
    AnimationContext animContext(obj->FutureVisualState(),storage);
    shape->InitAnimationContext(animContext, NULL, true); // no convex components for the roadway level

    obj->Animate(animContext, lShape->FindRoadwayLevel(), false, obj, -FLT_MAX);

    animContext.RecalculateNormalsAsNeeded(shape);
    
    const SurfaceInfo *ret = NULL;
    // TODO: allow OffTree for road-style (ClipLandOn/ClipLandKeep) animation, as it changes only y component
    if (!animated) // for animated objects it is not worth building the lookup tree again and again
    {
      const RoadwayFaces &rFaces = lShape->BuildRoadwayFaces();

      const SurfaceInfo *retN = NULL;
      PROFILE_SCOPE_DETAIL_EX(rcFQN,coll);
      OffTree<RoadwayFace>::QueryResult res;
      rFaces.Query(res,modelPos.X(),modelPos.Z(),modelPos.X()+0.0001f,modelPos.Z()+0.0001f);
      for (int r=0; r<res.regions.Size(); r++)
      {
        const RoadwayFace &rf = res.regions[r];

        const Poly &face=shape->Face(rf.o);
        const Plane &plane = animContext.GetPlane(shape, rf.i);
        const ShapeSection &sec = shape->GetSection(rf.section);

        float pdX, pdZ;
        float retY;
#if _VBS3 //fix for new roads
        bool isInside = (plane.Normal().Y() > 0) ? 
          face.InsideFromBottom(animContext.GetPosArray(shape), plane, modelPos, &retY, &pdX, &pdZ)
          : face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &retY, &pdX, &pdZ);
        if(isInside)
#else
        if (face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &retY, &pdX, &pdZ) )
#endif
        {
          // the face is not sure to be a triangle
          if (shape->GetOrHints() & ClipLandOn)
          {
            retN = sec.GetSurfaceInfo();
            break;
          }
          // ignore roadways that are very high above the landscape
          else if (retY <= pos.Y() + 1.5f)
          {
            retN = sec.GetSurfaceInfo();
            break;
          }
        }
      }
      ret = retN;
    }
    else
    {
      // many polygons can be incident if we look from the top  /'z
      PROFILE_SCOPE_DETAIL_EX(rcFQO,coll);
      for( int s = 0, i = 0; s<shape->NSections(); s++)
      {
        const ShapeSection &sec =  shape->GetSection(s);
        for( Offset f=sec.beg; f<sec.end; shape->NextFace(f),i++ )
        {
          const Poly &face=shape->Face(f);
          const Plane &plane = animContext.GetPlane(shape, i);
          float pdX, pdZ;
          float retY;
    #if _VBS3 //fix for new roads
          bool isInside = (plane.Normal().Y() > 0) ? 
                                face.InsideFromBottom(animContext.GetPosArray(shape), plane, modelPos, &retY, &pdX, &pdZ)
                              : face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &retY, &pdX, &pdZ);
          if(isInside)
    #else
          if (face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &retY, &pdX, &pdZ) )
    #endif
          {
            // the face is not sure to be a triangle
            if (shape->GetOrHints() & ClipLandOn)
            {
              ret = sec.GetSurfaceInfo();
              goto Break;
            }
            // ignore roadways that are very high above the landscape
            else if (retY <= pos.Y() + 1.5f)
            {
              ret = sec.GetSurfaceInfo();
              goto Break;
            }
          }
        }
      }
    }
    Break:
    obj->Deanimate(lShape->FindRoadwayLevel(),false);
    if (ret) return ret;
  }
  return NULL;
}


void GetGridRectangleAligned(GridRectangle &brect, Scene &scene, float distance, float grid, int landSegmentSize, int landSegmentAlign)
{
  scene.CalculBoundingRect(brect, distance, grid);
  brect.xBeg = RoundToSegment(brect.xBeg,landSegmentSize,0);
  brect.zBeg = RoundToSegment(brect.zBeg,landSegmentSize,landSegmentAlign);
  brect.xEnd = RoundToSegment(brect.xEnd+landSegmentSize-1,landSegmentSize,0);
  brect.zEnd = RoundToSegment(brect.zEnd+landSegmentSize-1,landSegmentSize,landSegmentAlign);
}

#if _ENABLE_CHEATS
#include "keyInput.hpp"
#include "dikCodes.h"
#endif

/// temporary - if there are any problems with grass predication, this can be turned false to hotfix them
const bool PredicateGrass = true;
/**
@param groundPrimed we need to know what exactly was rendered to be able to perform the 2nd pass
*/

void Landscape::PrimeDepthBuffer(GridRectangle &groundPrimed, Scene &scene, Scene::DrawContext &drawContext, bool zOnly, bool frameRepeated
#if _HELISIM_RENDER_TO_TEXTURE
                                 , int drawListId
#endif
                                 , bool debugMe)
{
  PROFILE_SCOPE_GRF_EX(DrawDepthPrime,*,SCOPE_COLOR_BLUE);
  if (LogStatesOnce) LogF("( DrawDepthPrime");

  // TODO: unify for FSAA as well
  // even with z priming we do not need to render a full distance
  // Consider distance limit upon objects used just for depth buffer creation
  // we only need enough distance to cover shadows + depth of field
  float distanceLimit = floatMax(Glob.config.GetShadowZ(),100);
  // for zPriming step we need to render the grass everywhere
  float grassDistanceLimit = zOnly || PredicateGrass ? FLT_MAX : floatMax(Glob.config.GetShadowZ(),100);

  // Calculate the land grid rectangle in a near distance
  GridRectangle rectGroundDepth;
  GetGridRectangleAligned(rectGroundDepth, scene, distanceLimit, GetLandGrid(), _segCache->_landSegmentSize, _segCache->_landSegmentAlign);
  
  groundPrimed = rectGroundDepth;

  // set default depth range for normal rendering
  GEngine->SetDepthRange(DepthBorderCommonMin,DepthBorderCommonMax);
  // Render ground and objects designed for depth rendering
  {
    // Start depth rendering, share depth buffer with normal rendering
    GEngine->BeginDepthMapRendering(zOnly
#if _HELISIM_RENDER_TO_TEXTURE
      , drawListId != 0
#endif
      );

    // when priming, we know from definition all primitives are primed
    GEngine->SetZPrimingDone(-1,true);

    //if (debugMe) GEngine->TrapFor0x8000("Landscape.PrimeDepthBuffer A");

    // X360 needs this, unsure why
    GEngine->SetDepthRange(DepthBorderCommonMin,DepthBorderCommonMax);
    
    // Draw ground - no special near clip required
    GEngine->SetDepthClip(-10);

    //if (debugMe) GEngine->TrapFor0x8000("Landscape.PrimeDepthBuffer X");
    
    Engine::PredicationMode pass = zOnly && GEngine->GetFSAA()>0 ? Engine::PrimingPass : Engine::DepthPass;
    
    if( !NoLandscape )
    {
      DrawGround(rectGroundDepth, rectGroundDepth, scene, false, true
#if _HELISIM_RENDER_TO_TEXTURE
        , drawListId != 0
#endif
        );

      //if (debugMe) GEngine->TrapFor0x8000("Landscape.PrimeDepthBuffer Y");

#if _VBS3_CRATERS_DEFORM_TERRAIN_SHADOWS
      scene.DrawPassOnSurface(drawContext, Engine::PrimingPass);
#endif

      bool doGrass = !PredicateGrass || GEngine->StartCBPredicatedScope(drawContext._lDGCl,pass);
      if (doGrass)
      {
        DrawGroundClutter(scene, grassDistanceLimit, debugMe);
        if (PredicateGrass) GEngine->EndCBPredicatedScope(drawContext._lDGCl);
      }
    }

    //if (debugMe) GEngine->TrapFor0x8000("Landscape.PrimeDepthBuffer B");

    // Draw objects designed for depth rendering
    bool svShadows = GEngine->GetFSAA()>0 ? !zOnly : true;
#if _HELISIM_RENDER_TO_TEXTURE
    scene.DrawDepthPrime(drawContext, svShadows, pass, drawListId);
#else
    scene.DrawDepthPrime(drawContext, svShadows, pass);
#endif

    GEngine->SetZPrimingDone(-1,false);
    // End depth rendering
    GEngine->EndDepthMapRendering(zOnly);

    //GEngine->RenderStencilShadows();
  }
  if (LogStatesOnce) LogF(") DrawDepthPrime");
}

void Landscape::DrawRectPrepare(Scene::DrawContext &drawContext, Scene &scene, float deltaT, bool frameRepeated)
{
  // Create the passes lists
  scene.DrawPreparation(drawContext, deltaT, frameRepeated);

  //! Prepare the ground clutter drawing
  if (!NoLandscape)
  {
    PrepareGroundClutter(scene);
  }

  // must be called after scene.DrawPreparation - uses object lists
  GEngine->PrepareShadowMapRendering(scene.GetSunClosestDistanceFrom0(drawContext, 0), 0, Glob.config.GetShadowZ(), false);
}

void Landscape::DrawRectShadowBuffer(Scene::DrawContext &drawContext, Scene &scene)
{
  // Render the shadow map
	if (!GEngine->GetThermalVision())
  {
		if (GEngine->IsSBEnabled())
		{
			PROFILE_SCOPE_GRF_EX(sssmC,*,SCOPE_COLOR_GREEN);
			// Initialize array of Z intervals
			float shadowsZLayer[ShadowBufferCascadeLayers + 1];
			{
				shadowsZLayer[0] = Glob.config.GetShadowZ();
				for (int i = 1; i < ShadowBufferCascadeLayers; i++)
				{
					shadowsZLayer[i] = shadowsZLayer[i - 1] * (1.0f/ShadowBufferCascadeRatio);
				}
				shadowsZLayer[ShadowBufferCascadeLayers] = 0.0f;
			}
  
			// Create shadow buffer for every layer and add it to SSSM
			// Revert the order for the layer of index 0 to be created in the last place - so that it will be in the map for later use (f.i. on alpha objects)
			for (int i = ShadowBufferCascadeLayers - 1; i >= 0 ; i--)
			{
				bool firstLayer = i == (ShadowBufferCascadeLayers - 1);
				bool lastLayer =  i == 0;
				GEngine->BeginShadowMapRendering(scene.GetSunClosestDistanceFrom0(drawContext, i), i, shadowsZLayer[i], firstLayer, false);
				scene.DrawSBShadows(drawContext, i);
				GEngine->EndShadowMapRendering(shadowsZLayer[i+1], shadowsZLayer[i], firstLayer, lastLayer, false);
			}
		}
		else
		{
			GEngine->RenderStencilShadows();
		}
	}

	#ifdef TEST_RAIN_MAP
		//is't raining, necessary to render? don't need to delete rain map because PP rain is controlled by the same function thus is always regenerated
		if (GEngine->IsRainMapEnabled() && GetRainDensity() > 0.001f)
		{
			float rainRadius = GEngine->GetRainRadius();
			GEngine->BeginShadowMapRendering(scene.GetSunClosestDistanceFrom0(drawContext, RAIN_MAP_FAKE_LAYER), RAIN_MAP_FAKE_LAYER, rainRadius, true, true);
				scene.DrawRainMap(drawContext);
			GEngine->EndShadowMapRendering(0.0f, rainRadius, true, true, true);
		}
	#endif
}

void Landscape::DrawRectDepthBuffer(Scene::DrawContext &drawContext, Scene &scene, bool frameRepeated)
{
//   // Render the depth map
//   if (GEngine->IsDepthOfFieldEnabled())
//   {
//     DrawDepthOfField(scene, drawContext, frameRepeated);
//   }
}

/*!
\patch 1.58 Date 5/20/2002 by Ondra
- Fixed: It was possible to see through coast in some places (e.g. Kolgujev Fd72)

\patch 5138 Date 3/8/2007 by Ondra
- Optimized: Grass depth of field is now performed only with Postprocess set to Very high.
*/

void Landscape::DrawRect(
  Scene::DrawContext &drawContext, const GridRectangle &groundPrimed, Scene &scene,
  const GridRectangle &rectG, const GridRectangle &rectW, float deltaT, bool frameRepeated
#if _HELISIM_RENDER_TO_TEXTURE
  , bool isR2T
#endif
)
{
#if _ENABLE_CHEATS
    static bool DrawGrassLayer = true;
    if (GInput.GetCheat3ToDo(DIK_G))
    {
      DrawGrassLayer = !DrawGrassLayer;
      GlobalShowMessage(500, "Draw grass layer %s", DrawGrassLayer ? "On" : "Off");
      // we need to rebuild all terrain shapes - to contain or not contain GTGrass
      while (LandSegment *seg=_segCache->_segments.Last())
      {
        _segCache->Delete(seg);
      }
    }
#endif
  //PROFILE_SCOPE_GRF_EX(lDraw,land,SCOPE_COLOR_GREEN);
  scene.SetRenderingDeltaT(deltaT);
  // Update the landscape shadows

#if _HELISIM_RENDER_TO_TEXTURE
  #if _VBS3_LAND_SHADOW
    if (!isR2T)
      scene.CalculateLandShadows();
  #else
    if (!isR2T)
      scene.CalculateLandShadows(rectG,false);
  #endif
#else
  #if _VBS3_LAND_SHADOW
      scene.CalculateLandShadows();
  #else
    scene.CalculateLandShadows(rectG,false);
  #endif
#endif

  // Rendering of SB - the final place to work on XBOX should be somewhere else (see the label), but expect asserts or float traps
  // of uninitialized variables when moving there (because the render engine's InitDraw was not called before)
  // Last observation: I tried to move it there and all seem to be working (maybe the formed problems expired)
  // Label: PLACEFORSBRENDERING
  //DrawRectShadowBuffer(drawContext, scene);

  {
    // split large rectangle into small segments
    #if LANDDRAW
    {
      float nightEye = (
        (1-scene.MainLight()->GetDiffuse().R())*
        scene.MainLight()->NightEffect()
      );

      if (!GEngine->GetNightVision())
      {
        GEngine->EnableNightEye(nightEye);
      }

    }
    #endif

    #define WATER_LAST 1
    #if !WATER_LAST
      #if LANDDRAW
      extern bool NoLandscape;
      #if _ENABLE_CHEATS
      if (!CHECK_DIAG(DETransparent) ||! CHECK_DIAG(DEForce))
      #endif
      if( !NoLandscape )
      {
        DrawWater(rectW,scene);

        DrawHorizont(scene);
      }
      #endif
    #endif

    GEngine->FlushQueues();
  }

   if( !NoLandscape )
  {
    // Draw ground
    {
      GEngine->SetZPrimingDone(-1,true);

      if (!PredicateGrass || !GEngine->CopyCBPredicatedScope(drawContext._lDGCl,Engine::RenderingPass) )
      {
         DrawGroundClutter(scene);
      }
      
      GEngine->SetZPrimingDone(-1,false);
    }
    
    // z-priming was done for terrain rendering as well
    DrawGround(rectG,groundPrimed,scene,false,false
#if _HELISIM_RENDER_TO_TEXTURE
      ,isR2T
#endif
      );
    
    // 3D craters are in passOnSurface, they need z-priming to be used on them (so that shadows can be casted on them)
    GEngine->SetZPrimingDone(-1,true);

    // draw roads and tracks  
    scene.DrawPassOnSurface(drawContext, Engine::RenderingPass);

    GEngine->SetZPrimingDone(-1,false);
  }

  // draw non-alpha objects
  scene.DrawPass1O(drawContext,frameRepeated);

  // rendering may take very long - make sure ping is updated often enough (GameSpy requirement)
  GetNetworkManager().FastUpdate();

  // we need to render the skybox before rendering objects with small alpha areas
  DrawSkyBox(scene
#if _HELISIM_RENDER_TO_TEXTURE
    ,isR2T
#endif 
    );

#if _VBS3_NO_SEABED
  if (!NoLandscape && !scene.UnderWater())
    DrawSeabed(scene);
#endif

#if _ARMA3_SIMUL_WEATHER
  bool drawSWCloudsBeforeWater = true;
  if (_enableSimulWeather && _simulWeather)
  {
    // if the camera is above the height of clouds , we have to draw clouds after the water is drawn (so that water is occluded by clouds)
    // othherwise we have to draw water first 
    float cloudBase = 1000;
    if (_simulWeather->GetCloudRenderer() && _simulWeather->GetCloudRenderer()->GetCloudInterface())
      cloudBase = _simulWeather->GetCloudRenderer()->GetCloudInterface()->GetCloudBaseZ();

    if (scene.GetCamera() && scene.GetCamera()->Position()[1] >= cloudBase)
      drawSWCloudsBeforeWater = false;
  }

  if (drawSWCloudsBeforeWater)
  {
    DrawSimulWeatherClouds(scene);
  }
#endif


  // Draw non-alpha objects (with several alpha faces)
  scene.DrawPass1A(drawContext,frameRepeated);

  GEngine->FlushQueues(); //???
  // make a snapshot of the scene used for water rendering and other refractions
  GEngine->CaptureRefractionsBackground();

  // Draw water and shore
  {
    #if LANDDRAW
      #if _ENABLE_CHEATS
      if (!CHECK_DIAG(DETransparent) ||! CHECK_DIAG(DEForce))
      #endif
      if( !NoLandscape )
      {
			GEngine->SetZPrimingDone(-1,true);

      // takes over 1ms in PiP
#if _HELISIM_RENDER_TO_TEXTURE
      if (!isR2T)
#endif
      {
        DrawShore(rectG, scene, GTShoreWet);
        DrawShore(rectG, scene, GTShore);
      }

			DrawWater(rectW, scene);			  			  

			GEngine->SetZPrimingDone(-1,false);
      }
    #endif
  }  

#if _ARMA3_SIMUL_WEATHER
  if (!drawSWCloudsBeforeWater)
  {
    GEngine->SetZPrimingDone(-1,true);
    DrawSimulWeatherClouds(scene);
    GEngine->SetZPrimingDone(-1,false);
  }
#endif

  #if 1
    if( !NoLandscape )
    {
      // draw alpha ground clutter does not perform any z-writes
      // it is therefore stencil buffer neutral
      // ground clutter uses alpha - it cannot be rendered before water
      // shadows need to be rendered after ground clutter rendering, so that they are casted over it
      DrawRainClutter(scene);
      if (_someClutterAlpha)
      {
        DrawGroundClutter(scene);
      }
    }
  #endif

#if _HELISIM_RENDER_TO_TEXTURE
  if (!isR2T) 
#endif
    GEngine->DrawSSAO();

	GEngine->DrawEnfDebugShapes();

	DrawHeatHazeEffect();
	
  GEngine->FlushQueues();
	
  // draw alpha objects and shadows
  scene.DrawPass2(drawContext);

  // clear any outstanding arrows (Buldozer ONLY)
  //_arrows.Clear();
  GEngine->FlushQueues();
}

/**
@return number of pixels used by the sun object including its halo
*/

float Landscape::TestSunVisibility(Scene &scene)
{
  if (!_sunVisTestObject) return 0;

  Camera &camera=*scene.GetCamera();

  // calculate sun position
  LightSun *sun=scene.MainLight();
  Vector3Val skyPosition=camera.Position();
  _skyObject->SetPosition(skyPosition+_skyObject->GetShape()->BoundingCenter());
  _starsObject->SetPosition(skyPosition+_starsObject->GetShape()->BoundingCenter());
  // rotate stars
  _starsObject->SetOrientation(sun->StarsOrientation());
  const float sunScale = 120.0/12000;
  // prepare sun
  Vector3 relPos = sun->SunDirection()*12000*sunScale;
  Vector3 sunPosition=camera.Position()-relPos;
  Matrix3 sunOrient;
  sunOrient.SetDirectionAndUp(-sun->SunDirection(), Vector3(0, 1, 0));
  _sunVisTestObject->SetOrientation(sunOrient);
  _sunVisTestObject->SetScale(sunScale);
  _sunVisTestObject->SetPosition(sunPosition);

  if (sun->SunDirection().Y()<+0.02f)
  {
		//doesn't work good, just an estimation, use real screen projection 

    // estimate area based on radius, distance and camera settings
    float dist2 = camera.Position().Distance2(_sunVisTestObject->FutureVisualState().Position());
    float radius = _sunVisTestObject->GetRadius()*H_INVSQRT2;

    // area of circle 
    float area = radius*radius;

    float areaK = camera.InvLeft()*camera.InvTop()*scene.GetEngineArea();
    float pixelArea = area*areaK/dist2;

    ShapeUsed shape = _sunVisTestObject->GetShape()->Level(0);
    PackedColor sunColor = shape->GetColor();
    float alpha  = sunColor.A8()*(1.0f/255);
    // 
    return 2.0f*pixelArea*alpha;
  }
  return 0;
}

struct SkyTexturesForSky
{
  static Texture *GetA(Shape *shape, TexMaterial *material) {return shape->GetSection(0).GetTexture();}
  static Texture *GetB(Shape *shape, TexMaterial *material) {return material->_stage[1]._tex;}
  
  static void SetA(Shape *shape, TexMaterial *material, Texture *tex)
  {
          for (int s = 0; s < shape->NSections(); s++)
          {
      shape->GetSection(s).SetTexture(tex);
          }
        }
  static void SetB(Shape *shape, TexMaterial *material, Texture *tex)
        {
    material->SetStageTexture(1, tex);
        }
};
/// handle if some of the textures is not ready yet
/** Used for both sky and horizon */
  

void Landscape::DrawHorizont(Scene &scene)
{
  GEngine->FlushQueues();

  // draw landscape background polygon
  if (_horizontObject.NotNull() && _horizontObject->GetShape() && _horizontObject->GetShape()->NLevels()>0)
  {
    Shape *shape = _horizontObject->GetShape()->InitLevelLocked(0);

    // compute the parallax. The higher the camera, the lower we want the horizon range to be placed
    Vector3 skyPosition=scene.GetCamera()->Position();
    static float experimentalParallax = 0.045f;
    skyPosition[1] -= skyPosition.Y()*experimentalParallax;
    _horizontObject->SetPosition(skyPosition+_horizontObject->GetShape()->BoundingCenter()); 

    SkyTexturesContext<SkyTexturesForSky> ctx(shape,_horizonMaterial);

    DrawParameters dp;
    dp._canReceiveShadows = false;
#if _HELISIM_RENDER_TO_TEXTURE
    DoAssert(CheckMainThread());
    _horizontObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _horizontObject->GetFrameBase(), NULL, scene.GetDrawObjectIndex());
#else
    _horizontObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _horizontObject->GetFrameBase(), NULL);
#endif
  }

  GEngine->FlushQueues();
}

#if _VBS3_NO_SEABED
void Landscape::DrawSeabed(Scene &scene)
{
  if (_seabedShape.IsNull() || (!_seabedShape->GetVertexBuffer() && !_seabedShape->IsGeometryLoaded()))
    InitSeabed();

  Vector3 camPos = scene.GetCamera()->Position();
  float dist2 = Square(camPos.Y());
  float scale = scene.GetCamera()->ClipFar();

  Vector3 pos = camPos;
  pos[1] = _seaLevelOffset - 20.0f;
  Matrix4 trans(MScale, scale);
  trans.SetPosition(pos);

  LightList empty;
  DrawParameters dp;
  dp._canReceiveShadows = false;

  EngineShapeProperties props;
  LightList lights; 

  _seabedShape->Draw(-1, NULL, HWhite, props, lights, ClipAll, 0, trans, dist2, dp);
  //_seabedShape->Draw(&GAnimatorDefault, EngineShapeProperties(),
  //  empty, ClipAll, 0, trans, dist2, dp);
}
#endif

/*!
size of sun and moon
*/
static const float origMoonRescale = 0.4f;			// rescale of original moon shape
static const float origSunRescale  = 1.0f;			// rescale of original sun shape
static const float maxSizeBias		 = 1.5f;			// relative size based on position, simulation of "moon/sun illusion"

_forceinline float GetRelativeSkyObjectSize(float dirY)
{
	const float invHALF_PI = 1.0f/H_PI2;
	return 1.0f + maxSizeBias*(asinf(1.0f - fabsf(dirY)) * invHALF_PI);
}

// draw sky using pre-builded cover
// load cover definition

void Landscape::DrawSky(Scene &scene)
{
  Camera &camera=*scene.GetCamera();

  // calculate sun position
  LightSun *sun=scene.MainLight();
  Vector3Val skyPosition=camera.Position();
  _skyObject->SetPosition(skyPosition+_skyObject->GetShape()->BoundingCenter());
  static float skyScale = 1.0f;
  _skyObject->SetScale(skyScale);
  _starsObject->SetPosition(skyPosition+_starsObject->GetShape()->BoundingCenter());
  // rotate stars
  _starsObject->SetOrientation(sun->StarsOrientation());
  const float sunScale = 120.0/12000;

  // prepare sun
  if (_sunObject)
  {
		float relSize = GetRelativeSkyObjectSize(sun->SunDirection().Y());

    Vector3 relPos = sun->SunDirection()*12000*sunScale;
    Vector3 sunPosition=camera.Position()-relPos;
    Matrix3 sunOrient;
    //sunOrient.SetDirectionAndUp(-sun->SunDirection(), Vector3(0, 1, 0));
    sunOrient.SetDirectionAndUp(camera.Direction(), VUp);
    _sunObject->SetOrientation(sunOrient);
    _sunObject->SetScale(sunScale*relSize);
    _sunObject->SetPosition(sunPosition);

		if (_sunVisTestObject)
		{
			static float sunDist = 12000.0f;
			Vector3 relPos = sun->SunDirection()*sunDist*sunScale;
			Vector3 sunPosition=camera.Position()-relPos;
			Matrix3 sunOrient;
			//sunOrient.SetDirectionAndUp(-sun->SunDirection(), Vector3(0, 1, 0));
			sunOrient.SetDirectionAndUp(camera.Direction(), VUp);

			_sunVisTestObject->SetOrientation(sunOrient);
			_sunVisTestObject->SetScale(sunScale*relSize);
			_sunVisTestObject->SetPosition(sunPosition);
		}

    //LogF("Sun rel pos %.2f,%.2f,%.2f",relPos[0],relPos[1],relPos[2]);
  }

  // prepare moon
  if (_moonObject)
  {
    Vector3 moonPosition=camera.Position()-sun->MoonDirection()*12000*sunScale;
    _moonObject->SetPosition(moonPosition);
    Matrix3 moonOrient;
    moonOrient.SetDirectionAndUp(camera.Direction(),sun->MoonDirectionUp());
    _moonObject->SetOrientation(moonOrient);
    _moonObject->SetScale(sunScale*GetRelativeSkyObjectSize(sun->MoonDirection().Y()));

    // TODO: check if modification of shape is legal
    ShapeUsed lock = _moonObject->GetShape()->Level(0);
    Assert(lock.NotNull());
    Shape *shape = lock.InitShape();
    for (int i=0; i<shape->NSections(); i++)
    {
#if _ENABLE_REPORT
      static bool reportMoonOnce = false;
      const Texture * tex = shape->GetSection(i).GetTexture();
      if (!reportMoonOnce && tex && tex->IsAnimated() && tex->AnimationLength()<2)
      {
        reportMoonOnce = true;
        LogF("not enough moon phases: %s",cc_cast(tex->GetName()));
      }
#endif
      shape->GetSection(i).AnimateTexture(sun->MoonPhase());
    }
  }

  //float clipLevel = skyPosition.Y();
  //scene.GetCamera()->SetUserClipPars(VUp,-clipLevel);

  DrawParameters dp;
  dp._canReceiveShadows = false;

  // Drawing of the sky (if one of the textures is not ready, replace it temporary with the second one)
  {

    if (_skyObject.NotNull() && _skyObject->GetShape() && _skyObject->GetShape()->NLevels()>0)
    {
      Shape *shape = _skyObject->GetShape()->InitLevelLocked(0);
      SkyTexturesContext<SkyTexturesForSky> ctx(shape,_skyMaterial);

      // Draw sky
#if _HELISIM_RENDER_TO_TEXTURE
      _skyObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _skyObject->GetFrameBase(), NULL, scene.GetDrawObjectIndex());
#else
      _skyObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _skyObject->GetFrameBase(), NULL);
#endif
    }

  }

  float starsVisibility=
  (
    // see TLVertexMesh::DoStarLighting
    // overcast limitation
    (1.5*SkyThrough()-0.5)*
    // daytime limitation
    sun->StarsVisibility()
  );

  if (GEngine->GetThermalVision()) starsVisibility = 0.0f;
  if( starsVisibility>=0.01 )
  {
    static float StarsVisiblityMult = 0.0015f;
    if (GEngine->GetNightVision()) starsVisibility *= StarsVisiblityMult;
    //_starsObject->DrawPoints(0,ClipAll&~ClipBack|ClipUser0,*_starsObject);
     Color color(0, 0, 0, starsVisibility);
     // TODO: check if modification of shape is legal
     ShapeUsed lock = _starsObject->GetShape()->Level(0);
     Assert(lock.NotNull());
     Shape *shape = lock.InitShape();

     shape->SetUniformMaterialAmbientColor(color);
    _starsObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _starsObject->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif
      );
  }
  //scene.GetCamera()->CancelUserClip();

  // Set the materials diffuse color (to hold transparency)
  Color color(0, 0, 0, SkyThrough() *  (GEngine->GetNightVision() ? 0.1f : 1.0f));
  if (_sunObject)
  {
    // TODO: check if modification of shape is legal
    ShapeUsed lock = _sunObject->GetShape()->Level(0);
    Assert(lock.NotNull());
    Shape *shape = lock.InitShape();
    shape->SetUniformMaterialDiffuseColor(color);
  }
  if (_moonObject)
  {
    // TODO: check if modification of shape is legal
    ShapeUsed lock = _moonObject->GetShape()->Level(0);
    Assert(lock.NotNull());
    Shape *shape = lock.InitShape();
    shape->SetUniformMaterialDiffuseColor(color);
  }

  // make sure sun and moon is not rendered below the horizon
  if (sun->SunDirection().Y()<+0.02f)
  {
	/*
		if (_sunVisTestObject)
		{
			scene.BeginSunPixelCounting(-1);
				_sunVisTestObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _sunVisTestObject->GetFrameBase(), NULL, scene.GetDrawObjectIndex());
			scene.EndSunPixelCounting(-1);
		}
	*/

    if (_sunObject)
      _sunObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _sunObject->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif
      );
	}
  if (sun->MoonDirection().Y()<+0.02f)
  {
    if (_moonObject && !GEngine->GetThermalVision())
      _moonObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _moonObject->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif
      );
  }

  // draw rainbow
  float density = SkyThrough()*GetRainDensity()*0.35f;

  // we cannot call SetRainTexture in the init only, as that would not provide reset recovery
	GEngine->SetGodraysKernel(GetGodraysTexture());
	GEngine->SetNewRainTexture(GetNewRainTexture());
  
  // when sun is low, there is no rainbow (we ignore moon rainbow - it is too faint to be interesting)
  density *= InterpolativC(sun->SunDirection().Y(),-0.05f,+0.0f,1,0);

  if (GEngine->GetThermalVision()) density = 0.0f;
  if (density>=0.01f)
  {
    saturateMin(density,1);
    // TODO: rainbow intensity should be derived from light intensity
    Color color(0, 0, 0, density);
    // TODO: check if modification of shape is legal
    ShapeUsed lock = _rainbowObject->GetShape()->Level(0);
    Assert(lock.NotNull());
    Shape *shape = lock.InitShape();

    shape->SetUniformMaterialDiffuseColor(color);
    const float rainbowScale = 1;
    const float rainbowDistance = 10;
    Vector3 relPos = -sun->SunDirection()*rainbowDistance*rainbowScale;
    Vector3 sunPosition=camera.Position()-relPos;
    Matrix3 sunOrient;
    sunOrient.SetDirectionAndUp(sun->SunDirection(), Vector3(0, 1, 0));
    _rainbowObject->SetOrientation(sunOrient);
    _rainbowObject->SetScale(rainbowScale);
    _rainbowObject->SetPosition(sunPosition);
    _rainbowObject->Draw(-1, 0, ClipAll&~ClipBack, dp, InstanceParameters::_default, 0.1f, _rainbowObject->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif      
      );
  }
}

void Landscape::DrawSkyBox(Scene &scene
#if _HELISIM_RENDER_TO_TEXTURE
                           , bool isR2T
#endif
                           )
{
  PROFILE_SCOPE_GRF_EX(lDSky,land,SCOPE_COLOR_GRAY);
  Camera &camera=*scene.GetCamera();

  Camera::ClipRangeState state;
  camera.SaveClipRange(state);
  // even when the cloud or skybox is far, we still need the clouds to be rendered
  camera.SetClipRange(10,7000,Glob.config.GetShadowZ(), Glob.config.GetProjShadowsZ(), 4000);

  // set everything to "as far as possible, but not 1"
  GEngine->SetDepthRange(1,1);

  // no cloud shadows during sky rendering - sky is above clouds
  scene.RecalculateLighting(0,NULL);

  static bool sky = true;
  if (sky)
    DrawSky(scene);
  if (!GEngine->GetThermalVision())
  {
    DrawClouds(scene);
  }

#if _ARMA3_SIMUL_WEATHER
  if (_simulWeather && _enableSimulWeather)
  {
    if (!GEngine->GetThermalVision())
    {
      // we have to recalculate lighting without cloud shadows 
      _simulWeather->RecalculateLighting();
    }
  }
#endif

  scene.RecalculateLighting(1,this);
  GEngine->FlushQueues();

  camera.RestoreClipRange(state);

  static bool horizont = true;
#if _HELISIM_RENDER_TO_TEXTURE
  if (horizont && !isR2T)
#else
  if (horizont)
#endif
    DrawHorizont(scene);
  
  // restore default range
  GEngine->SetDepthRange();
}

/*!
  /param intensity Intensity is specified in the same units as in shader VSPoint.hlsl
*/
void Landscape::DrawPoint(int cb, const Vector3 &pos, float intensity, const Color &color
#if _HELISIM_RENDER_TO_TEXTURE
                          , int drawListId
#endif
                          )
{
  // TODO:MC: implement MC compatible parametes passing
  // currently MC is disabled, see PointLight::CanObjDrawAsTask
  PROFILE_SCOPE_GRF_EX(POINT,*,SCOPE_COLOR_GREEN);
  _pointObject->SetPosition(pos + _pointObject->GetShape()->BoundingCenter());
  _pointObject->SetOrientation(M3Identity);

  // TODO: check if modification of shape is legal
  ShapeUsed lock = _pointObject->GetShape()->Level(0);
  Assert(lock.NotNull());
  Shape *shape = lock.InitShape();

  Color colorAmbient(0, 0, 0, intensity * 1.0f / GEngine->GetMaxStarIntensity());
  shape->SetUniformMaterialAmbientColor(colorAmbient);
  Color colorEmmisive = color;
  colorEmmisive.SetA(1.0f);
  shape->SetUniformMaterialEmmisiveColor(colorEmmisive);
  _pointObject->Draw(cb, 0, ClipAll&~ClipBack, DrawParameters::_noShadow, InstanceParameters::_default, 0.1f, _pointObject->GetFrameBase(), NULL
#if _HELISIM_RENDER_TO_TEXTURE
    , drawListId
#endif
    );
}

// there are three separate clouds levels
const int SkyLevels = 3;

const float SkyZ = 20000;

const float SkyGrid = 6000.0f;

const static float SkyY[SkyLevels]={6000,4000,2000};
const float SkySpCoef = 2.0f;
const static float SkyXSpeed[SkyLevels]={+0.020*SkySpCoef,+0.012*SkySpCoef,+0.010*SkySpCoef};
const static float SkyZSpeed[SkyLevels]={+0.004*SkySpCoef,-0.002*SkySpCoef,-0.004*SkySpCoef};

/// cloud scale is used to fit the clouds in the scene. It does not affect screen size at all
const float CloudScale=0.2f;


/**
@return 0 means no occlusion, 1 means fully occluded
*/
float Landscape::CheckCloudOcclusion(Vector3Par src, Vector3Par dir) const
{
#if _ARMA3_SIMUL_WEATHER
  if (_simulWeather && _enableSimulWeather)
  {
    if (_simulWeather->GetCloudRenderer())
    {
      // SIMUL WEATHE TODO - it seems that this value does not correspond to occlusion very well...
      float occlusion = _simulWeather->GetCloudRenderer()->GetSunOcclusion();
#if _ENABLE_CHEATS  
      if (  CHECK_DIAG(DESimulWeather)  )
      {
        DIAG_MESSAGE(50,"Sun occlusion %g", occlusion);
      }
#endif

      saturate(occlusion, 0, 1);

			_fCheckCloudOcclusionRes = occlusion;
      return occlusion;
    }
  }
#endif

	//if it's not neccessary, do not recalculate
	if (!_bRecalculateCloudsOcclusion)
	{
		return _fCheckCloudOcclusionRes;
	}

	//clear the flag, is set in World::Simulate
	_bRecalculateCloudsOcclusion = false;

  // if we are looking down or very low, there may be no occlusion, or it would be very inaccurate
  const float noOcclusionAngle = 0.1f;
  const float fullOcclusionAngle = 0.3f;
  if (dir.Y()<=noOcclusionAngle) 
	{
		_fCheckCloudOcclusionRes = 0.0f;
		return 0;
	}

  float lowFactor = dir.Y()>fullOcclusionAngle ? 1 : (dir.Y()-noOcclusionAngle)/(fullOcclusionAngle-noOcclusionAngle);
  // consider occlusion from all levels
  float totalTransparency = 1;
//  float maxDist = 0;
//  int minX = INT_MAX, maxX = -INT_MAX, minZ = INT_MAX, maxZ = -INT_MAX;

  float cPos=CloudsPosition();
  float height=CloudsHeight();
  float cloudSize = CloudsSize();
  
  for (int skyLevel = 0; skyLevel < SkyLevels; skyLevel++)
  {

    float xMove0 = cPos*SkyXSpeed[skyLevel];
    float zMove0 = cPos*SkyZSpeed[skyLevel];
    
    
    float posY = SkyY[skyLevel]*height;
      
    // calculate intersection with given cloud layer
    float t = (posY-src.Y())/dir.Y();
    Vector3 end = src+dir*t;
    // for each cloud type and each sky level the rectangle may be different
    // Calculate sky rectangle around the intersection
    
    float gridPosX = end.X()/SkyGrid;
    float gridPosZ = end.Z()/SkyGrid;
    GridRectangle skyBegEnd;
    skyBegEnd.xBeg = toIntFloor(gridPosX)-2;
    skyBegEnd.zBeg = toIntFloor(gridPosZ)-2;
    skyBegEnd.xEnd = toIntCeil(gridPosX)+2;
    skyBegEnd.zEnd = toIntCeil(gridPosZ)+2;

    // in each level select the cloud which occludes most
    float minTransparency = 1;
    // Go through all the cloud types
    for (int cloud = 0; cloud < NClouds; cloud++)
    {
      Object *object = _cloudObj[cloud];
      if( !object ) continue;
      if (!object->GetShape()) continue;
      //LODShape *cloudShape = object->GetShape();

      int xx,zz;
      int xMoveInt = toIntFloor(xMove0);
      int zMoveInt = toIntFloor(zMove0);
      float xMove = xMove0 - xMoveInt;
      float zMove = zMove0 - zMoveInt;

      xMove*=SkyGrid;
      zMove*=SkyGrid;

      for( zz=skyBegEnd.zBeg; zz<=skyBegEnd.zEnd; zz++ )
      {
        for( xx=skyBegEnd.xBeg; xx<=skyBegEnd.xEnd; xx++ )
        {
          int seedXZ=_randGen.GetSeed(xx-xMoveInt,zz-zMoveInt,skyLevel+cloud*SkyLevels);
          float isHereF=_randGen.RandomValue(seedXZ);
          int isHere=toIntFloor(isHereF * NClouds * 2 * 4); // 50% probability there will be one cloud
          if( isHere>=NClouds ) continue;

          // Count the offsets
          float xOffset=xMove+_randGen.RandomValue(seedXZ+1)*(SkyGrid*0.5);
          float zOffset=zMove+_randGen.RandomValue(seedXZ+2)*(SkyGrid*0.5);

          // Get position
          Vector3 pos(xx*SkyGrid+xOffset, posY, zz*SkyGrid+zOffset);

          // We can ignore the orientation, assume circular clouds
          //Matrix3 orient(MRotationY, _randGen.RandomValue(seedXZ+3)*(2*H_PI/2));

          // Set the object origin (to count the radius and clipping properly later)
          //object->SetOrient(orient);
          //object->SetPosition(pos);

          // Clip the cloud 
          float empiricalRadius = 0.6f;
          float radius = object->GetShape()->BoundingSphere()*(H_INVSQRT2*cloudSize*empiricalRadius);
          /// TODO: check possible collision with bbox and object
          // check if end may be within the cloud area
          float dist2 = pos.Distance2(end);

          //float dist = sqrt(dist2);
          //if (dist>maxDist) maxDist = dist;
          if (dist2>Square(radius)) continue;
//          if (minX>xx) minX = xx;if (maxX<xx) maxX = xx;
//          if (minZ>zz) minZ = zz;if (maxZ<zz) maxZ = zz;
          
          float transparency = sqrt(dist2)/radius;
          transparency = floatMax(transparency*2-1,0);
          if (minTransparency>transparency) minTransparency = transparency;

        }
      }
    }
    
    totalTransparency *= minTransparency;

  }
//  DIAG_MESSAGE(500,Format("MaxDist %g, x: %d..%d, z: %d..%d",maxDist,minX,maxX,minZ,maxZ));

	//save the value to use it again when necessary
	_fCheckCloudOcclusionRes = (1-totalTransparency)*lowFactor;
	return _fCheckCloudOcclusionRes;
}

/// cloud rendering cache
class CloudsCache: private NoCopy
{
  /// instance info for clouds of given type
  AutoArray<ObjectInstanceInfo> _clouds[NClouds];
  /// min. distance for clouds of given type
  float _minDist2[NClouds];
  
  friend class Landscape;
  struct CloudsCacheLevel
  {
    /// index in _clouds where given level ends
    int _end[NClouds];
    /// sky rectangle we are currently holding
    GridRectangle _rect;
    //@{ parameters of what we have generated
    Vector3 _off;
    Vector3 _camPos;
    //@}
  };
  CloudsCacheLevel _level[SkyLevels];
  
  public:
  void Calculate(
    Landscape *land, const GridRectangle &skyBegEnd, Scene &scene
  );
  void Clear();
};

void CloudsCache::Clear()
{
  for (int cloud = 0; cloud < NClouds; cloud++)
  {
    _minDist2[cloud] = FLT_MAX;
    AutoArray<ObjectInstanceInfo> &clouds = _clouds[cloud];
    // start the list from the scratch
    clouds.Resize(0);
  }
  for (int level=0; level<SkyLevels; level++)
  {
    for (int cloud = 0; cloud < NClouds; cloud++)
    {
      _level[level]._end[cloud] = 0;
    }
    _level[level]._rect = GridRectangle(0,0,0,0);
  }
}


void CloudsCache::Calculate(
  Landscape *land, const GridRectangle &skyBegEnd, Scene &scene
)
{
  PROFILE_SCOPE_EX(lDClG,land);
  const Camera &camera = *scene.GetCamera();

  float cPos = land->CloudsPosition();
  float height = land->CloudsHeight();
  float cloudSize = land->CloudsSize();

  GridRectangle rect[SkyLevels];
  
  bool isInside = true;
  for (int skyLevel=0; skyLevel<SkyLevels; skyLevel++)
  {
    CloudsCacheLevel &level = _level[skyLevel];
    float xMove = cPos*SkyXSpeed[skyLevel];
    float zMove = cPos*SkyZSpeed[skyLevel];
  
    int xMoveInt=toIntFloor(xMove);
    int zMoveInt=toIntFloor(zMove);
    rect[skyLevel] = GridRectangle(
      skyBegEnd.xBeg-xMoveInt, skyBegEnd.zBeg-zMoveInt,
      skyBegEnd.xEnd-xMoveInt, skyBegEnd.zEnd-zMoveInt
    );
    if (!rect[skyLevel].IsInside(level._rect)) isInside = false;
  }

    
  if (isInside)
  {
    for (int cloud = 0; cloud < NClouds; cloud++)
    {
      _minDist2[cloud] = FLT_MAX;

      Object *object = land->_cloudObj[cloud];
      if( !object || !object->GetShape()) continue;
      // Modify the material
      Color ambient = HWhite * land->CloudsBrightness();
      Color forcedDiffuse = HWhite * land->CloudsBrightness();
      ambient.SetA(land->CloudsAlpha());

      // TODO: check if modification of shape is legal
      ShapeUsed lock = object->GetShape()->Level(0);
      Assert(lock.NotNull());
      Shape *shape = lock.InitShape();

      if (shape)
      {
        shape->SetUniformMaterialAmbientColor(ambient);
        shape->SetUniformMaterialForcedDiffuseColor(forcedDiffuse);
      }
    }
    InitVal<int,0> beg[NClouds];
    for (int skyLevel=0; skyLevel<SkyLevels; skyLevel++)
    {
      CloudsCacheLevel &level = _level[skyLevel];

      float xMove = cPos*SkyXSpeed[skyLevel];
      float zMove = cPos*SkyZSpeed[skyLevel];
      Vector3Val camPos = camera.Position();
      float posY = SkyY[skyLevel]*height;
      Vector3 off(xMove*SkyGrid, posY, zMove*SkyGrid);
      // we can reuse what we have, we only need to adjust it
      Vector3 changePos = (off-level._off)*CloudScale + (1-CloudScale)*(camPos-level._camPos);

      for (int cloud = 0; cloud < NClouds; cloud++)
      {
        Object *object = land->_cloudObj[cloud];
        if( !object || !object->GetShape()) continue;
        float objRadius = object->GetShape()->BoundingSphere();
        int end = level._end[cloud];
        
        AutoArray<ObjectInstanceInfo> &clouds = _clouds[cloud];
        for (int i=beg[cloud]; i<end; i++)
        {
          ObjectInstanceInfo &info = clouds[i];
          // recalculate position
          Vector3 pos = info._origin.Position();
          pos += changePos;
          info._origin.SetPosition(pos);
          info._origin.SetScale(cloudSize*CloudScale);

          float radius = objRadius*CloudScale*cloudSize;
          // Calculate the object distance
          float dist = camera.Position().Distance(pos);
          float dist2 = Square(floatMax(dist - radius,0));

          // Remember the minimum distance
          if (_minDist2[cloud] > dist2) _minDist2[cloud] = dist2;
        }
        // next level starts where this one ends
        beg[cloud] = end;
      }
      level._off = off;
      level._camPos = camPos;
    }
    
  }
  else
  {
    //GlobalShowMessage(500,"Gen clouds");
    for (int cloud = 0; cloud < NClouds; cloud++)
    {
      _minDist2[cloud] = FLT_MAX;
      AutoArray<ObjectInstanceInfo> &clouds = _clouds[cloud];
      // start the list from the scratch
      clouds.Resize(0);

      Object *object = land->_cloudObj[cloud];
      if( !object || !object->GetShape()) continue;
      // Modify the material
      Color ambient = HWhite * land->CloudsBrightness();
      Color forcedDiffuse = HWhite * land->CloudsBrightness();
      ambient.SetA(land->CloudsAlpha());

      // TODO: check if modification of shape is legal
      ShapeUsed lock = object->GetShape()->Level(0);
      Assert(lock.NotNull());
      Shape *shape = lock.InitShape();

      if (shape)
      {
        shape->SetUniformMaterialAmbientColor(ambient);
        shape->SetUniformMaterialForcedDiffuseColor(forcedDiffuse);
      }
    }
    for (int skyLevel=0; skyLevel<SkyLevels; skyLevel++)
    {
      float xMove = cPos*SkyXSpeed[skyLevel];
      float zMove = cPos*SkyZSpeed[skyLevel];
      float posY = SkyY[skyLevel]*height;
      Vector3 off(xMove*SkyGrid, posY, zMove*SkyGrid);
      Vector3Val camPos = camera.Position();
      
      CloudsCacheLevel &level = _level[skyLevel];
      // Go through all the cloud types
      for (int cloud = 0; cloud < NClouds; cloud++)
      {
        Object *object = land->_cloudObj[cloud];
        if( !object || !object->GetShape()) continue;
        float objRadius = object->GetShape()->BoundingSphere();

        // safeguard: avoid excessive numbers of clouds
        const int maxClouds = 500;
        AutoArray<ObjectInstanceInfo> &clouds = _clouds[cloud];

        #if 0        
        if (cloud==0)
        {
          Log(
            "*** Level %d - clouds gen %d,%d..%d,%d",
            skyLevel,
            rect[skyLevel].xBeg,rect[skyLevel].zBeg,
            rect[skyLevel].xEnd,rect[skyLevel].zEnd
          );
        }
        #endif

        for( int zz=rect[skyLevel].zBeg; zz<=rect[skyLevel].zEnd; zz++ )
        for( int xx=rect[skyLevel].xBeg; xx<=rect[skyLevel].xEnd; xx++ )
        {
          int seedXZ=land->_randGen.GetSeed(xx,zz,skyLevel+cloud*SkyLevels);
          //Log("  %d - %d,%d: seed %x",skyLevel,xx,zz,seedXZ);
          float isHereF=land->_randGen.RandomValue(seedXZ);
          int isHere=toIntFloor(isHereF * NClouds * 2 * 4); // 50% probability there will be one cloud
          if( isHere>=NClouds ) continue;

          // Count the offsets
          float xOffset = land->_randGen.RandomValue(seedXZ+1)*(SkyGrid*0.5);
          float zOffset = land->_randGen.RandomValue(seedXZ+2)*(SkyGrid*0.5);

          // Get position
          Vector3 pos(xx*SkyGrid+xOffset, 0, zz*SkyGrid+zOffset);
          
          pos = (pos+off)*CloudScale+(1-CloudScale)*camPos;

          // Get orientation
          Matrix3 orient(MRotationY, land->_randGen.RandomValue(seedXZ+3)*(2*H_PI/2));
          orient *= CloudScale*cloudSize;

          // Set the object origin (to count the radius and clipping properly later)
          object->SetOrient(orient);
          object->SetPosition(pos);


          // we want to avoid reallocating the array - this is slow and could exhaust the memory  
          if (clouds.Size()>=maxClouds)
          {
            Log("Too many clouds");
            goto TooManyInstances;
          }
          // Add the object
          ObjectInstanceInfo &info = clouds.Append();

          // Fill out object information
          info._origin = object->GetFrameBase();
          // modulate instancing is used
          info._object = info._parentObject = object;
          info._color = HWhite;
          info._shadowCoef = 1.0f;

          //Log("Cloud ^)
          // Clip the cloud 
          float radius = objRadius*CloudScale*cloudSize;
          // Calculate the object distance
          float dist = camera.Position().Distance(pos);
          float dist2 = Square(floatMax(dist - radius,0));

          // Remember the minimum distance
          if (_minDist2[cloud] > dist2) _minDist2[cloud] = dist2;
        }
        TooManyInstances:;
        clouds.CompactIfNeeded(4,8);
        level._end[cloud] = clouds.Size();
      }
      level._rect = rect[skyLevel];
      level._off = off;
      level._camPos = camPos;
    }
  }
}

void Landscape::DrawClouds(Scene &scene)
{
#if _ARMA3_SIMUL_WEATHER
  if (_simulWeather && _enableSimulWeather)
  {
    // we are using simul weather clouds...
    return;
  }
#endif

  // special case optimization - no clouds at all
  if (CloudsAlpha()<1e-3) return;

  PROFILE_SCOPE_EX(lDClo,land);

  // Calculate sky rectangle
  GridRectangle skyBegEnd;
  scene.CalculBoundingRect(skyBegEnd, SkyZ, SkyGrid);

  // extend region slightly - we are unable to calculate it properly
  skyBegEnd.xBeg--;
  skyBegEnd.zBeg--;
  skyBegEnd.xEnd++;
  skyBegEnd.zEnd++;
  
  _cloudsCache->Calculate(this,skyBegEnd,scene);
  
  DrawParameters dp;
  dp._canReceiveShadows = false;
  LightList noLights;
  for (int cloud = 0; cloud < NClouds; cloud++)
  {
    Object *object=_cloudObj[cloud];
    if( !object ) continue;

    // Draw the clouds of given type
    object->DrawSimpleInstanced(
      -1, 0, ClipAll&~ClipBack, dp,
      _cloudsCache->_clouds[cloud].Data(), _cloudsCache->_clouds[cloud].Size(),
      _cloudsCache->_minDist2[cloud], noLights, NULL, FLT_MAX
#if _HELISIM_RENDER_TO_TEXTURE
      , scene.GetDrawObjectIndex()
#endif
    );
  }
}

/*!
\patch 1.30 Date 11/05/2001 by Ondra.
- Fixed: Sky cloud layer clipping.
*/

void Landscape::Draw(Scene::DrawContext &drawContext, const GridRectangle &groundPrimed, Scene &scene, float deltaT, bool frameRepeated
#if _HELISIM_RENDER_TO_TEXTURE
                     , bool isR2T
#endif
                     )
{
  #if _ENABLE_CHEATS
  static
  #endif
  int simulateError = 0;
  if (!GEngine->IsAbleToDrawCheckOnly() || simulateError>0 && (--simulateError,true))
  {
    LogF("Landscape::Draw - !IsAbleToDrawCheckOnly");
    if (drawContext._oPasO.IsEmpty() && drawContext._lDGCl.IsEmpty() && drawContext._lDGnd.IsEmpty())
    { // if any CBs are recorded, we need to use them
      return;
    }
  }
  //PROFILE_SCOPE_GRF_EX(lDTot,land,SCOPE_COLOR_GRAY);
  // when not rendering sky, we still need to set the default z-range
  GEngine->SetDepthRange();

  // Get ground grid rectangle
  GridRectangle begEndGround;
  GetGridRectangleAligned(begEndGround, scene, scene.GetFogMaxRange(), GetLandGrid(), _segCache->_landSegmentSize, _segCache->_landSegmentAlign);

  // project points limiting viewpoint
  GridRectangle begEndWater;
  scene.CalculBoundingRect(begEndWater, scene.GetFogMaxRange(), WaterGrid);

  int landSegmentSize = _segCache->_landSegmentSize;

  // use only rough rectangles - no generalization
#if _DEBUG
  GridRectangle old = begEndGround;
#endif
  
  Assert(old.IsInside(begEndGround));
  
  const int waterSegmentSize = GetWaterSegmentSize();  
#if !_VBS3_LARGE_WATERSEG
  begEndWater.xBeg &= ~(waterSegmentSize-1);
  begEndWater.zBeg &= ~(waterSegmentSize-1);
  begEndWater.xEnd = (begEndWater.xEnd+(waterSegmentSize-1))&~(waterSegmentSize-1);
  begEndWater.zEnd = (begEndWater.zEnd+(waterSegmentSize-1))&~(waterSegmentSize-1);
#else
  begEndWater.xBeg = RoundToSegment(begEndWater.xBeg, waterSegmentSize, 0);
  begEndWater.zBeg = RoundToSegment(begEndWater.zBeg, waterSegmentSize, 0);
  begEndWater.xEnd = RoundToSegment(begEndWater.xEnd+(waterSegmentSize-1), waterSegmentSize, 0);
  begEndWater.zEnd = RoundToSegment(begEndWater.zEnd+(waterSegmentSize-1), waterSegmentSize, 0);
#endif

  int requiredGCacheSize = (begEndGround.xEnd-begEndGround.xBeg)*(begEndGround.zEnd-begEndGround.zBeg)/(landSegmentSize*landSegmentSize);
  int requiredWCacheSize = (begEndWater.xEnd-begEndWater.xBeg)*(begEndWater.zEnd-begEndWater.zBeg)/(waterSegmentSize*waterSegmentSize);

  // recompute cache size to avoid overflowing it
  _segCache->_maxN = CalculateCacheSize(this,false,_segCache->_landSegmentSize*GetLandGrid());
  _segCache->_maxWaterN = CalculateCacheSize(this,false,waterSegmentSize*WaterGrid);

  saturateMax(_segCache->_maxN,requiredGCacheSize);
  saturateMax(_segCache->_maxWaterN,requiredWCacheSize);

  #if 0 // _ENABLE_REPORT
    GlobalShowMessage
    (
      500,"landrect %d,%d..%d,%d (%d,%d), segments %d, cache %d",
      begEndGround.xBeg,begEndGround.zBeg,
      begEndGround.xEnd,begEndGround.zEnd,
      begEndGround.xEnd-begEndGround.xBeg,begEndGround.zEnd-begEndGround.zBeg,
      requiredCacheSize,estimatedCacheSize
    );

  #endif

  // Draw part of the surface bounded by a rectangle
  DrawRect(drawContext,groundPrimed,scene,begEndGround,begEndWater,deltaT,frameRepeated
#if _HELISIM_RENDER_TO_TEXTURE
    , isR2T
#endif
    );

  // once we have used the clutter in the 2nd pass, there is no need to keep the cache any longer
  void ClearClutterDrawCache();
  ClearClutterDrawCache();

}

TerrainCache *Landscape::UpdateTerrainCache(const GridRectangle &rect)
{
  #if 1
  if (!_terrain->_outsideData) return NULL;
  int subdivLog = GetSubdivLog();
  GridRectangle begEndTerrain(rect.xBeg<<subdivLog,rect.zBeg<<subdivLog,rect.xEnd<<subdivLog,rect.zEnd<<subdivLog);
  GridRectangle inside(0,0,GetTerrainRange(),GetTerrainRange());
  if (!begEndTerrain.IsInside(inside))
  {
    GridRectangle rect = begEndTerrain;
    GridRectangle aligned(
      rect.xBeg&~(TerrainCacheItem::Dim-1),rect.zBeg&~(TerrainCacheItem::Dim-1),
      (rect.xEnd+TerrainCacheItem::Dim-1)&~(TerrainCacheItem::Dim-1),(rect.zEnd+TerrainCacheItem::Dim-1)&~(TerrainCacheItem::Dim-1)
    );
    if (!_terrain->_outsideCache || aligned!=_terrain->_outsideCache->_rect)
    {
      PROFILE_SCOPE_EX(otCch,*);
      if (PROFILE_SCOPE_NAME(otCch).IsActive())
      {
        PROFILE_SCOPE_NAME(otCch).AddMoreInfo(Format("%d,%d..%d,%d",aligned.xBeg,aligned.zBeg,aligned.xEnd,aligned.zEnd));
      }
      
      TerrainCache *cache = new TerrainCache(_terrain->_outsideData,aligned,_terrain->_outsideCache);
      // assignment to OutsideCache contains memory barrier
      _terrain->_outsideCache = cache;
    }
  }
  else
  {
    // if not outside, do not maintain the cache
    _terrain->_outsideCache.Free();
  }
  #endif
  return _terrain->_outsideCache;

}

void Landscape::DrawNull( Scene &scene )
{
}

void Landscape::RegisterTexture(int id, const char *name, int major)
{
  SetTexture(id,name,major);
  if (id!=0)
  {
    TexMaterial *mat = _terrain->_texture[id]._mat;
    if (mat) mat->CheckForChangedFiles();
  }
}
void Landscape::RegisterObjectType( const char *name )
{
  // add a shape into the bank
  //Ref<LODShape> shape=Shapes.New(name,false,true);
  //shape->OptimizeRendering();
}

void Landscape::Quit()
{
}

/*!
\patch 1.50 Date 4/5/2002 by Ondra
- Fixed: Landscape texture size in preferences was ignored,
Object texture settings was used instead.
*/

void Landscape::SetTexture(int i, const char *name, int major)
{
  if( i==0 )
  {
    // slot zero should never be used - it was reserved for sea texture
    _terrain->_texture.Access(i);
    return;
  }
  BString<512> aName(name);
  aName.StrLwr();
  // get multipass material name from a texture name
  bool isRvmat = false;
  if (aName[0])
  {
    // we expect rvmat or bimpas
    // make sure it is bimpas name, not a texture name
    const char *ext = GetFileExt(GetFilenameExt(aName));
    if (*ext==0)
    {
      // no extension - must be some bug
      _terrain->_texture.Access(i);
      _terrain->_texture[i]._mat = NULL;
      return;
    }
    isRvmat = !strcmp(ext,".rvmat");
    if (!isRvmat)
    {
      DoAssert(!strcmp(ext,".bimpas"));
    }
  }
  _terrain->_texture.Access(i);
  if (isRvmat)
  {
    // new Visitor - rvmat provided directly
    Ref<TexMaterial> mat = GTexMaterialBank.New(TexMaterialName(cc_cast(aName)));
    if (mat)
    {
      DoAssert(major>=0);
      InitLandMat(_midDetailTexture,mat);
      _terrain->_texture[i]._mat = mat;
      return;
    }
  }
  RptF("bimpas no longer expected on terrain - reexport %s",cc_cast(GetName()));
  _terrain->_texture[i]._mat = NULL;
}

bool Landscape::PreloadTextureInfo(const char *name)
{
  // check for malformed names
  if (!strchr(name,'.')) return true;
  // we could perform some sophisticated loading
  // however all we do now is we request the file should be loaded
  QIFStreamB in;
  in.AutoOpen(name);
  return in.PreRead(FileRequestPriority(1000));
}

float Landscape::GetRandomValueForObject(Object *obj, int offset)
{
  return _randGen.RandomValue(obj->ID(),offset);
}

float Landscape::GetRandomValueForCoord(int x, int z, int offset)
{
  return _randGen.RandomValue(x,z,offset);
}

/*!
\patch 1.05 Date 7/17/2001 by Ondra.
- Fixed: Random seed was used for landscape bump generator.
In MP this caused different simulation results on local and remote computer,
resulting in vehicle shaking.
*/

float Landscape::CalculateBump(
  float xC, float zC, float bumpy
) const
{
  // TODO: bilinear interpolation of texture roughness in corners
  //const float bumpScale=1.0/0.3;
  const float bumpScale=1.0;
  float bumpX=xC*bumpScale;
  float bumpZ=zC*bumpScale;
  int iBumpX=toIntFloor(bumpX);
  int iBumpZ=toIntFloor(bumpZ);
  // 
  //RandBegin(iBumpX,iBumpZ);
  float bump00=_randGen.RandomValue(_randGen.GetSeed(iBumpX,iBumpZ));
  float bump01=_randGen.RandomValue(_randGen.GetSeed(iBumpX+1,iBumpZ));
  float bump10=_randGen.RandomValue(_randGen.GetSeed(iBumpX,iBumpZ+1));
  float bump11=_randGen.RandomValue(_randGen.GetSeed(iBumpX+1,iBumpZ+1));

  #if 1
    bumpX-=iBumpX; // relative in-bump coordinates
    bumpZ-=iBumpZ;
    // bilinear interpolation of bumpY
    float bump0=bump00+(bump01-bump00)*bumpX;
    float bump1=bump10+(bump11-bump10)*bumpX;
    float bump=bump0+(bump1-bump0)*bumpZ; // result in range 0 .. 1
    bump-=0.5;
  #else
    const float bump=0;
  #endif
  return bump*bumpy;
}



float Landscape::BumpySurfaceY
(
  float xC, float zC, float &rdX, float &rdZ,
  Texture *&texture, float bumpy, float &bump
) const
{
  // fine rectangles are not used - use rough instead
  // calculate surface level on given coordinates
  float xRel=xC*GetInvTerrainGrid();
  float zRel=zC*GetInvTerrainGrid();
  int x=toIntFloor(xRel);
  int z=toIntFloor(zRel);
  float xIn=xRel-x; // relative 0..1 in square
  float zIn=zRel-z;

  bump = 0;
  float y00,y01,y10,y11;
  _terrain->GetRectY(x,z,y00,y01,y10,y11);

  // each face is divided to two triangles
  // determine which triangle contains point
  float y;
  if( xIn<=1-zIn )
  { // triangle 00,01,10
    rdX=(y10-y00)*GetInvTerrainGrid();
    rdZ=(y01-y00)*GetInvTerrainGrid();
    y=y00+(y01-y00)*zIn+(y10-y00)*xIn;
  }
  else
  {
    // triangle 01,10,11
    rdX=(y11-y01)*GetInvTerrainGrid();
    rdZ=(y11-y10)*GetInvTerrainGrid();
    y=y11+(y01-y11)*(1-xIn)+(y10-y11)*(1-zIn);
  }
  // depending on surface texture use roughness
  texture = PrimaryTexture(x, z);
  if (texture)
  {
    bump = CalculateBump(xC,zC,bumpy)*texture->Roughness();
  }
  return y;
}

void Landscape::SurfacePlane( Plane &plane, float xC, float zC ) const
{
  // calculate surface level on given coordinates
  float xRel=xC*GetInvTerrainGrid();
  float zRel=zC*GetInvTerrainGrid();
  int x=toIntFloor(xRel);
  int z=toIntFloor(zRel);
  float xIn=xRel-x; // relative 0..1 in square
  float zIn=zRel-z;

  float y00,y01,y10,y11;
  _terrain->GetRectY(x,z,y00,y01,y10,y11);
  
  // each face is divided to two triangles
  // determine which triangle contains point
  Vector3 normal=VUp;
  if( xIn<=1-zIn )
  { // triangle 00,01,10

    normal[0]=(y10-y00)*-GetInvTerrainGrid();
    normal[2]=(y01-y00)*-GetInvTerrainGrid();
  }
  else
  {
    // triangle 01,10,11
    normal[0]=(y11-y01)*-GetInvTerrainGrid();
    normal[2]=(y11-y10)*-GetInvTerrainGrid();
  }
  Vector3 point((x+1)*GetTerrainGrid(),y01,z*GetTerrainGrid());
  plane=Plane(normal,point);
}

float Landscape::SurfaceYSource( float x, float z ) const
{
  // code equivalent to
  // return SurfaceY(x,z,NULL,NULL,NULL);
  // as this case is quite common, it is optimized

  // it might be also worth to separate SurfaceY into two parts:
  // preparation and calculation
  // preparation would gather and return yii values
  // fine rectangles are not used - use rough instead
  // calculate surface level on given coordinates
  
  int subdivision = GetTerrainRangeLog()-_terrain->_terrainRangeLogSource;
  //float ratio = 1<<subdivision;
  float invRatio = ((1<<16)>>subdivision)*(1.0f/(1<<16));
  
  float invTerrainGridSource = GetInvTerrainGrid()*invRatio;
  float xRel=x*invTerrainGridSource;
  float zRel=z*invTerrainGridSource;
  int xi=toIntFloor(xRel);
  int zi=toIntFloor(zRel);
  float xIn=xRel-xi; // relative 0..1 in square
  float zIn=zRel-zi;

  int xiData = xi<<subdivision;
  int ziData = zi<<subdivision;
  int step = 1<<subdivision;
  
  float y00,y01,y10,y11;
  _terrain->GetRectY(xiData,ziData,step,step,y00,y01,y10,y11);
  return RectBilint(y00,y01,y10,y11,xIn,zIn);
}


#if 0 //_DEBUG
const float SeaWaveXScale = 0.2f/50;
const float SeaWaveZScale = 0.1f/50;
const float SeaWaveHScale = 10;
const int SeaWaveXDuration = 5000; // duration in ms
const int SeaWaveZDuration = 10000; // duration in ms
#else
const float SeaWaveXScale = 2.0f/50;
const float SeaWaveZScale = 1.0f/50;
const float SeaWaveHScale = 1;
const int SeaWaveXDuration = 5000; // duration in ms
const int SeaWaveZDuration = 10000; // duration in ms
#endif

void Landscape::GetSeaWaveMatrices(Matrix4 &matrix0, Matrix4 &matrix1, Matrix4 &matrix2, Matrix4 &matrix3, const EngineShapeProperties &prop) const
{
  
  const float waveXScale = SeaWaveXScale;
  const float waveZScale = SeaWaveZScale;
  const int xDuration = SeaWaveXDuration;
  const int zDuration = SeaWaveZDuration;
  
  matrix0(0,0) = waveXScale*WaterGrid;
  matrix0(1,1) = waveZScale*WaterGrid;
  matrix0.SetPosition(Vector3(
    Glob.time.ModMs(xDuration)*(1.0f/xDuration),
    // we need to maintain V loop point at 8, because of P(y) part of the wave func.
    Glob.time.ModMs(zDuration*8)*(1.0f/zDuration),
    0
  ));

  float mw2 = fastFmod(Glob.time.toFloat()*0.1f+sin(Glob.time.toFloat()*0.5f)*0.03f,10);

  // Set 3 matrices with different speed
  {
    const float speedX1 = 0.3f;
    const float speedY1 = 0.4f;
    matrix1.SetPosition(Vector3(
      sqrt(Square(matrix1(1,1))+Square(matrix1(1,2))) * mw2 * speedX1 + matrix1.Position().X(),
      sqrt(Square(matrix1(2,1))+Square(matrix1(2,2))) * mw2 * speedY1 + matrix1.Position().Y(),
      0));

    const float speedX2 = 0.2f;
    const float speedY2 = 0.3f;
    matrix2.SetPosition(Vector3(
      sqrt(Square(matrix2(1,1))+Square(matrix2(1,2))) * mw2 * speedX2 + matrix2.Position().X(),
      sqrt(Square(matrix2(2,1))+Square(matrix2(2,2))) * mw2 * speedY2 + matrix2.Position().Y(),
      0));

    const float speedX3 = 0.1f;
    const float speedY3 = 0.2f;
    matrix3.SetPosition(Vector3(
      sqrt(Square(matrix3(1,1))+Square(matrix3(1,2))) * mw2 * speedX3 + matrix3.Position().X(),
      sqrt(Square(matrix3(2,1))+Square(matrix3(2,2))) * mw2 * speedY3 + matrix3.Position().Y(),
      0));
  }
}

void Landscape::GetSeaWavePars(float &waveXScale, float &waveZScale, float &waveHeight) const
{
  waveHeight = GetWaveHeight()*0.5f*SeaWaveHScale;
  waveXScale = SeaWaveXScale;
  waveZScale = SeaWaveZScale;
}

/*!
  \patch 5115 Date 1/9/2007 by Ondra
  - Fixed: Terrain detail settings not working properly - could introduce holes on lower settings.
*/
float Landscape::GetLODBias() const
{
  return TerrainLODBias + _terrain->_lodBias;
}


float Landscape::GetMaxLod() const
{
  return _segCache->NLods() - 1;
}

float Landscape::GetSeaLevel(float x, float z) const
{
  if (GEngine->CanSeaWaves())
  {
    // from x/z calculate corresponding wave position
    // follow calculations done in the shader
    // on Xbox water is animated on the shader - see FPShadersWater.vsa
    const float waveXScale = SeaWaveXScale;
    const float waveZScale = SeaWaveZScale;
    const int xDuration = SeaWaveXDuration;
    const int zDuration = SeaWaveZDuration;
    
    const float waveXOffset = Glob.time.ModMs(xDuration)*(1.0f/xDuration);
    const float waveZOffset = Glob.time.ModMs(zDuration*8)*(1.0f/zDuration);

    // UV coordinated are generated by applying texgen over x,z 
    float u,v;
    int waterSegmentSize = GetWaterSegmentSize();
    MapSeaUVLogical(u,v,x*InvWaterGrid,z*InvWaterGrid,waterSegmentSize);
    
    const float pAdjustF1 = 2.0f/8; // see valWAVEGEN
    const float pAdjustF2 = 3.0f/8;
    const float pAdjustA1 = 0.2f;
    const float pAdjustA2 = 0.12f;
    
#if 0 //_DEBUG
      float tex0x = u*WaterGrid*waveXScale+waveXOffset;
      float tex0y = v*WaterGrid*waveZScale+waveZOffset;

      float waveHeight = GetWaveHeight()*0.5f*SeaWaveHScale;
      float phaseAdjust = 0;
      //float xWave = 0;
      float zWave = 0;
      //float xWave = fastFmod(tex0x,1)*waveHeight;
      //float zWave = fastFmod(tex0y,1)*waveHeight;

      float xWaveSinArg = (tex0x+phaseAdjust)*(2*H_PI)-H_PI;
      float zWaveSinArg = tex0y*(2*H_PI)-H_PI;
      
      float xWave = sin(xWaveSinArg)*waveHeight;
#else
      float tex0x = u*WaterGrid*waveXScale+waveXOffset;
      float tex0y = v*WaterGrid*waveZScale+waveZOffset;

      //  P(y) calculation
      float phaseAdjust = (
        pAdjustA1 * sin(tex0y*pAdjustF1*(2*H_PI) - H_PI)+
        pAdjustA2 * sin(tex0y*pAdjustF2*(2*H_PI) - H_PI)
      );
      
      // as the sin is able to cope with any value, fabs and fmod are not necessary
      float xWaveSinArg = (tex0x+phaseAdjust)*(2*H_PI)-H_PI;
      float zWaveSinArg = tex0y*(2*H_PI)-H_PI;
      
      float waveHeight = GetWaveHeight()*0.5f*SeaWaveHScale;
      float xWave = sin(xWaveSinArg)*waveHeight;
      float zWave = sin(zWaveSinArg)*waveHeight;
#endif
    
#if 0
    // calculate partial derivative in x,z directions
    float xWaveDer = cos(xWaveSinArg)*waveXScale*(2*H_PI)*waveHeight*SeaWaveHScale;
#endif
    return _seaLevelWave+xWave+zWave;
  }
  else
    return _seaLevelWave;
}

float Landscape::SurfaceY(float x, float z) const
{
  // code equivalent to
  // return SurfaceY(x,z,NULL,NULL,NULL);
  // as this case is quite common, it is optimized

  // it might be also worth to separate SurfaceY into two parts:
  // preparation and calculation
  // preparation would gather and return yii values
  // fine rectangles are not used - use rough instead
  // calculate surface level on given coordinates
  float xRel=x*GetInvTerrainGrid();
  float zRel=z*GetInvTerrainGrid();
  int xi=toIntFloor(xRel);
  int zi=toIntFloor(zRel);
  float xIn=xRel-xi; // relative 0..1 in square
  float zIn=zRel-zi;
  float y00,y01,y10,y11;
  _terrain->GetRectY(xi,zi,y00,y01,y10,y11);
  return RectBilint(y00,y01,y10,y11,xIn,zIn);
}

float Landscape::SurfaceYLod(float x, float z, int lod) const
{
  // calculate surface level on given coordinates

  Assert(lod<=16);
  // we want to avoid division
  // we assume lod range is somewhat limited
  int lodStep = 1<<lod;
  float invLodStep = (1<<(16-lod))/float(1<<16);
  
  float xRel=x*GetInvTerrainGrid()*invLodStep;
  float zRel=z*GetInvTerrainGrid()*invLodStep;
  int xi=toIntFloor(xRel);
  int zi=toIntFloor(zRel);
  float xIn=xRel-xi; // relative 0..1 in square
  float zIn=zRel-zi;

  float y00,y01,y10,y11;
  _terrain->GetRectY(xi<<lod,zi<<lod,lodStep,lodStep,y00,y01,y10,y11);
  return RectBilint(y00,y01,y10,y11,xIn,zIn);
}

float Landscape::GrassHeight(float x, float z) const
{
  //int lodStep = 1<<lod;
  //float invLodStep = (1<<(16-lod))/float(1<<16);
  const int lod = 0;
  const int lodStep = 1<<lod;
  const float invLodStep = (1<<(16-lod))/float(1<<16);

  float xRel=x*GetInvTerrainGrid()*invLodStep;
  float zRel=z*GetInvTerrainGrid()*invLodStep;
  int xi=toIntFloor(xRel);
  int zi=toIntFloor(zRel);
  float xIn=xRel-xi; // relative 0..1 in square
  float zIn=zRel-zi;

  float y00,y01,y10,y11;
  GetGrassRectY(xi<<lod,zi<<lod,lodStep,lodStep,y00,y01,y10,y11);
  return RectBilint(y00,y01,y10,y11,xIn,zIn);
}

/*
@param x [in] world space coordinate
@param z [in] world space coordinate
@param rdX [out] differential (slope)
@param rdZ[out] differential (slope)
@param texture [out] surface texture defining material properties at given coordinates
@return above see level at given coordinates
*/
float Landscape::SurfaceY(float x, float z, float *rdX, float *rdZ, Texture **texture) const
{ 
  // fine rectangles are not used - use rough instead
  // calculate surface level on given coordinates
  float xRel=x*GetInvTerrainGrid();
  float zRel=z*GetInvTerrainGrid();
  int xi=toIntFloor(xRel);
  int zi=toIntFloor(zRel);
  float xIn=xRel-xi; // relative 0..1 in square
  float zIn=zRel-zi;

  if (texture)
    *texture = PrimaryTexture(xi, zi);
  
  float y00,y01,y10,y11;
  _terrain->GetRectY(xi,zi,y00,y01,y10,y11);
  
  // each face is divided to two triangles
  // determine which triangle contains point
  if (xIn<=1-zIn)
  { // triangle 00,01,10
    float d1000=y10-y00;
    float d0100=y01-y00;
    if (rdX)
    {
      *rdX=d1000*GetInvTerrainGrid();
      *rdZ=d0100*GetInvTerrainGrid();
    }
    return y00+d0100*zIn+d1000*xIn;
  }
  else
  {
    // triangle 01,10,11
    float d1011=y10-y11;
    float d0111=y01-y11;
    if (rdX)
    {
      *rdX=d0111*-GetInvTerrainGrid();
      *rdZ=d1011*-GetInvTerrainGrid();
    }
    return y10+d0111-d0111*xIn-d1011*zIn;
  }
}

// surface normal at the given point
Vector3 Landscape::SurfaceNormal(float x, float z) const
{
  float dX, dZ;
  SurfaceY(x, z, &dX, &dZ);
  Vector3 normal(-dX, 1.0f, -dZ);
  normal.Normalize();
  return normal;
}


void Landscape::ObjectRoadSurfaceY(const Object *with, Object *obj, Vector3Par pos, float landY, float sdX, float sdZ,
  Vector3 &ret, Object *&dXdZRelToObj, Object **retObj, float &maxY, Texture *&maxTexture, float &maxDX, float &maxDZ) const
{
#if _VBS3
  // Ignore roadways of objects attach to the object
  if(with && (obj->GetAttachedTo() == with))  continue;
#endif
  // check bounding box collision
  LODShape *lShape=obj->GetShape();
  if( !lShape ) return;
  const Shape *shape=lShape->RoadwayLevel();
  if( !shape ) return;
  float oRad=lShape->BoundingSphere();
  float distXZ2=(obj->FutureVisualState().Position()-pos).SquareSizeXZ();
  if( distXZ2>Square(oRad) ) return;
  if (!obj->HasGeometry()) return; // Don't test collision with destructed tents (the matrix won't be orthogonal anyway)

  Matrix4Val invTransform=obj->FutureVisualState().GetInvTransform();
  //Assert(invTransform.Orientation().IsOrthogonal(NULL));
  // roadway ready - check collision
  // has top and sometimes side polygons
  // many polygons can coincide if we look from the top
  Vector3 modelPos=invTransform.FastTransform(pos);
  // caution: proxies itself may be out of roadway level bounds
  Vector3Val roadMin = shape->Min();
  Vector3Val roadMax = shape->Max();
  if (modelPos.X()>=roadMin.X() && modelPos.X()<=roadMax.X() && modelPos.Z()>=roadMin.Z() && modelPos.Z()<=roadMax.Z())
  {
    // when performing roadway testing we work top-down, assuming vertical is more or less vertical
    //Assert(obj->DirectionUp().Y()>=0.8f);

    //int i=0;
    AnimationContextStorageRoadway storage;
    AnimationContext animContext(obj->FutureVisualState(),storage);
    shape->InitAnimationContext(animContext, NULL, true); // no convex components for the roadway level
    obj->Animate(animContext, lShape->FindRoadwayLevel(), false, obj, -FLT_MAX);
    // note: plane may be animated in some cases
    // it may be necessary to recalculate it
    animContext.RecalculateNormalsAsNeeded(shape);
    int fi = 0;
    for (int s=0; s<shape->NSections(); s++)
    {
      const ShapeSection &ss = shape->GetSection(s);
      const PolyProperties &pp = animContext.GetSection(shape, s);
      for( Offset f=ss.beg; f<ss.end; shape->NextFace(f),fi++ )
      {
        const Poly &face=shape->Face(f);
        const Plane &plane = animContext.GetPlane(shape, fi);
        float pdX,pdZ;
        //DirectionUp was not correctly calculated for new roads

  #if _VBS3 //fix for new roadways
        if (-invTransform.Direction().CrossProduct(invTransform.DirectionAside()) 
          * plane.Normal() < 0.0f)
  #else
        if (-invTransform.DirectionUp() * plane.Normal() < 0.1)
  #endif
          continue; // roadway is probably on animated object and object is lying
  #if _VBS3 //fix for new roads
        bool isInside = (plane.Normal().Y() > 0) ? 
          face.InsideFromBottom(animContext.GetPosArray(), plane, modelPos, &modelPos[1], &pdX, &pdZ)
          : face.InsideFromTop(animContext.GetPosArray(), plane, modelPos, &modelPos[1], &pdX, &pdZ);
        if(isInside)
  #else
        if (face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ) )
  #endif
        {
          // the face is not sure to be a triangle
          if (shape->GetOrHints() & ClipLandOn )
          {
            if( maxY<=landY )
            {
              maxTexture=pp.GetTexture();
              maxY=landY;
              maxDX=sdX;
              maxDZ=sdZ;
              ret=Vector3(pos.X(),landY,pos.Z());
              if (retObj) *retObj = obj;
            }
          }
          else
          {
            Vector3 wPos = obj->FutureVisualState().PositionModelToWorld(modelPos);
            if (wPos[1]<=pos.Y() && maxY<=wPos[1])
            {
              // create normal, transform it, calculate dX, dZ as atan
              maxY=wPos[1],maxDX=pdX,maxDZ=pdZ;
              maxTexture=pp.GetTexture();
              dXdZRelToObj = obj;
              ret=wPos;
              if (retObj) *retObj = obj;
            }
          }
        }
      }
    }
    
    obj->Deanimate(lShape->FindRoadwayLevel(),false);
  }
  #if 1
  // scan proxies as well
  const EntityType *type = obj->GetEntityType();
  if (type)
  {
    int roadLevel = lShape->FindRoadwayLevel();
    int nProxies = type->GetProxyCountForLevel(roadLevel);

    if (nProxies>0)
    {
      Matrix4Val invTransform = obj->FutureVisualState().GetInvTransform();
      
      // values are returned by the proxy testing, all sharing the same parent space
      Vector3 retProxy;
      Object *dXdZRelToObjProxy = NULL;
      Object *retObjProxy = NULL;
      Texture *maxTextureProxy = NULL;
      float maxDXProxy = 0,maxDZProxy = 0;
      // convert the currently assumed "maximum" point into the parent space
      
      float maxYProxy = -1e10;
      if (maxY>-1e10)
      {
        maxYProxy = invTransform.FastTransform(ret).Y();
        // note: if the proxy is vertical, it should be roughly the same as: maxY - obj->Position.Y()
        #if _DEBUG
          float checkYProxy = maxY - obj->FutureVisualState().Position().Y();
          (void)checkYProxy;
        #endif
      }
      float maxYProxy0 = maxYProxy;

      for (int i=0; i<nProxies; i++)
      {
        // try a "roadway collision" with a proxy
        const ProxyObjectTyped &proxy = type->GetProxyForLevel(roadLevel,i);
        if (proxy.GetShape()->FindRoadwayLevel()>=0)
        {
          // convert modelPos into the proxy coordinate system
          //Vector3 proxyPos = proxy.invTransform.FastTransform(modelPos);
          // check if we have some chance of hit
          // currently we assume top direction is roughly identical
          // perform a test
          // assume proxy is not animated - if it should be, it would be a lot more complicated
          // we can however easily check for one particular animation - hiding by using zero scale
          // we may need to get an animated position
          Matrix4 trans = obj->GetProxyTransform(roadLevel,i,proxy.GetTransform(),obj->FutureVisualState().Transform());
          // if the object is collapsed, no need to test it
          if (trans.Orientation().Scale2()<1e-3f) continue;
          
          AnimationContext noAnimation(proxy.obj->FutureVisualState());

          ObjectRoadSurfaceY(
            with,proxy.obj,modelPos,landY,sdX,sdZ,
            retProxy,dXdZRelToObjProxy,&retObjProxy,maxYProxy,maxTextureProxy,maxDXProxy,maxDZProxy
          );
          
        }
      }
      // test if there is any proxy result
      if (maxYProxy>maxYProxy0)
      {
        // if it is, we need to convert the results into the world space
        // ret needs to be a parent space, retProxy is obj space
        ret = obj->FutureVisualState().PositionModelToWorld(retProxy);
        maxY = ret.Y();
        if (dXdZRelToObjProxy)
        {
          void ComputeDDFromNormal(float &maxDX, float &maxDZ, Vector3Par normal);
          // convert into the obj space
          Vector3 normalObj = dXdZRelToObjProxy->FutureVisualState().DirectionModelToWorld(Vector3(-maxDXProxy,1,-maxDZProxy));
          ComputeDDFromNormal(maxDX,maxDZ,normalObj);
          dXdZRelToObj = obj;
        
        }
        else
        {
          // surface based - already in model space
          maxDX = maxDXProxy;
          maxDZ = maxDZProxy;
        }
        if (retObj) *retObj = obj;
        maxTexture = maxTextureProxy;
      }

    }
    
  }
  #endif
}

/*!
  \patch 5153 Date 4/3/2007 by Flyman
  - Fixed: Tanks were thrown in the air when colliding with some destructed buildings
*/
float Landscape::RoadSurfaceY(Vector3Par pos, float *dX, float *dZ, Texture **texture,
  UseObjectsMode syncMode, Object **obj, const Object *with, const Object *ignore) const
{
  PROFILE_SCOPE_EX(cLRSu,coll);
  Texture *surfTexture=NULL;
  float sdX,sdZ;
  float landY=SurfaceY(pos.X(),pos.Z(),&sdX,&sdZ,&surfTexture);

  // if we are on road, return road surface parameters
  // check all near network object clipping boxes

  int xMin,xMax,zMin,zMax;
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,pos,pos,0);
  // prepare return variables
  float maxY=-1e10;
  float maxDX=0, maxDZ=0;
  Texture *maxTexture=NULL;
  Vector3 ret;
  Object *dXdZRelToObj = NULL;
  // scan for roads
  for( int z=zMin; z<=zMax; z++ ) for( int x=xMin; x<=xMax; x++ )
  {
    if (!IsSomeRoadway(x,z)) continue;
    const ObjectListUsed &list=UseObjects(x,z,syncMode);
    //const ObjectList &list=listU;
    // optimization - many slots can be skipped
    Assert (!list.IsNull() && list.GetRoadwayCount()>0);
    int n=list.Size();
    for( int i=0; i<n; i++ )
    {
      Object *o=list[i];
      if( !o ) continue; // no collisions with roads
      if (o == ignore) continue; // ignore this roadway
      ObjectRoadSurfaceY(with, o, pos, landY, sdX, sdZ, ret, dXdZRelToObj, obj, maxY, maxTexture, maxDX, maxDZ);
    }
  }

  if( maxY>=-1e3 )
  {
    //_world->GetScene()->DrawCollisionStar(ret);
    // if road is under surface, use normal surface
    if( ret.Y()>=landY )
    {
      if( dX )
      {
        if (dXdZRelToObj)
        {
          // transform maxDX, maxDZ to world space
          // create normal
          Vector3 normal(-maxDX,1,-maxDZ);
          //normal.Normalize(); // normalize not needed, we are dividing by Y anyway
          // transform normal to world space
          dXdZRelToObj->FutureVisualState().DirectionModelToWorld(normal,normal);
          if (fabs(normal.Y())>1e-2)
          {
            maxDX = -normal.X()/normal.Y();
            maxDZ = -normal.Z()/normal.Y();
          }
          else
          {
            maxDX = 0;
            maxDZ = 0;
          }
        }
        *dX=maxDX,*dZ=maxDZ;
      }
      if( texture ) *texture=maxTexture;
      return ret.Y();
    }
  }
  // otherwise return SurfaceY
  if( texture ) *texture=surfTexture;
  if( dX ) *dX=sdX,*dZ=sdZ;
  if (obj) *obj = NULL;
  return landY;
}

float Landscape::RoadSurfaceY(float xC, float zC, float *dX, float *dZ, Texture **texture, UseObjectsMode syncMode, const Object *ignore) const
{
  PROFILE_SCOPE_EX(cLRSu,coll);
  Texture *surfTexture=NULL;
  float sdX,sdZ;
  float landY=SurfaceY(xC,zC,&sdX,&sdZ,&surfTexture);

  // if we are on road, return road surface parameters
  // check all near network object clipping boxes

  int xMin,xMax,zMin,zMax;
  Vector3 pos(xC,0,zC);
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,pos,pos,0);
  // prepare return variables
  float maxY=-1e10;
  float maxDX=0, maxDZ=0;
  Texture *maxTexture=NULL;
  Point3 ret;
  // scan for roads
  for( int z=zMin; z<=zMax; z++ ) for( int x=xMin; x<=xMax; x++ )
  {
    // optimization - many slots can be skipped
    if (!IsSomeRoadway(x,z)) continue;
    const ObjectListUsed &list=UseObjects(x,z,syncMode);
    Assert (!list.IsNull() && list.GetRoadwayCount()>0);
    Point3 pos(xC,0,zC);
    int n=list.Size();
    for( int i=0; i<n; i++ )
    {
      Object *obj=list[i];
      if( !obj ) continue; // no collisions with roads
      if (obj == ignore) continue; // ignore this roadway
      // check bounding box collision
      const LODShape *lShape=obj->GetShape();
      if( !lShape ) continue;
      const Shape *shape=lShape->RoadwayLevel();
      if( !shape ) continue;
      float oRad=lShape->BoundingSphere();
      float distXZ2=(obj->FutureVisualState().Position()-pos).SquareSizeXZ();
      if( distXZ2>Square(oRad) ) continue;
      Matrix4Val invTransform=obj->FutureVisualState().GetInvTransform();
      AnimationContextStorageRoadway storage;
      AnimationContext animContext(obj->FutureVisualState(),storage);
      shape->InitAnimationContext(animContext, NULL, true); // no convex components for the roadway level
      obj->Animate(animContext, lShape->FindRoadwayLevel(), false, obj, -FLT_MAX);

      // roadway ready - check collision
      // has top and sometimes side polygons
      // many polygons can incide if we look from the top
      Vector3 modelPos=invTransform.FastTransform(Point3(xC,0,zC));
      //int i=0;
      animContext.RecalculateNormalsAsNeeded(shape);

      // optimization - search for maximum in model space, transfer to world space only once per object
      float localMaxY = -1e10;
      float localMaxDX = 0, localMaxDZ = 0;
      Texture *localMaxTexture = NULL;
      Vector3 localRet;

      int fi = 0;
      for (int s=0; s<shape->NSections(); s++)
      {
        const ShapeSection &ss = shape->GetSection(s);
        const PolyProperties &pp = animContext.GetSection(shape, s);
        for( Offset f=ss.beg; f<ss.end; shape->NextFace(f),fi++ )
        {
          const Poly &face=shape->Face(f);
          const Plane &plane = animContext.GetPlane(shape, fi);
          float pdX,pdZ;
          //DirectionUp was not correctly calculated for new roads
#if _VBS3 //fix for new roadways
          if (-invTransform.Direction().CrossProduct(invTransform.DirectionAside()) 
            * plane.Normal() < 0.0f)
#else
          if (-invTransform.DirectionUp() * plane.Normal() < 0.1)
#endif
          {
            continue; // roadway is probably on animated object and object is lying
          }

#if _VBS3 //fix for new roads
          bool isInside = (plane.Normal().Y() > 0) ? 
            face.InsideFromBottom(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ)
            : face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ);
          if(isInside)
#else
          if (face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ))
#endif
          {
            // the face is not sure to be a triangle
            if (shape->GetOrHints() & ClipLandOn)
            {
              float y = landY+modelPos[1];
              if( maxY<=y )
              {
                maxTexture=pp.GetTexture();
                maxY=y;
                maxDX=sdX;
                maxDZ=sdZ;
                ret=Vector3(xC,y,zC);
              }
            }
            else if (localMaxY < modelPos[1])
            {
              localMaxY = modelPos[1];
              localMaxDX = pdX;
              localMaxDZ = pdZ;
              localMaxTexture = pp.GetTexture();
              localRet = modelPos;
            }
          }
        }
      }
      if (localMaxY > -1e10)
      {
        // some candidate found
        Vector3 worldRet = obj->FutureVisualState().PositionModelToWorld(localRet);
        if (maxY < worldRet[1])
        {
          maxY = worldRet[1];
          maxDX = localMaxDX;
          maxDZ = localMaxDZ;
          maxTexture = localMaxTexture;
          ret = worldRet;
        }
      }

      obj->Deanimate(lShape->FindRoadwayLevel(),false);
    }
  }

  if( maxY>=-1e3 )
  {
    //_world->GetScene()->DrawCollisionStar(ret);
    // if road is under surface, use normal surface
    if( ret.Y()>=landY )
    {
      if( dX ) *dX=maxDX,*dZ=maxDZ;
      if( texture ) *texture=maxTexture;
      return ret.Y();
    }
  }
  // otherwise return SurfaceY
  if( texture ) *texture=surfTexture;
  if( dX ) *dX=sdX,*dZ=sdZ;
  return landY;
}

float Landscape::WaterSurfaceY(Vector3Par pos) const
{
  PROFILE_SCOPE(cLWSu);
  ADD_COUNTER(cLWSu,1);
  float waterY = GetSeaLevel(pos);

  // check all near network object clipping boxes

  int xMin,xMax,zMin,zMax;
  ObjRadiusRectangle(xMin,xMax,zMin,zMax,pos,pos,0);
  // scan for roads
  for( int z=zMin; z<=zMax; z++ ) for( int x=xMin; x<=xMax; x++ )
  {
    if (!IsSomeRoadway(x,z)) continue;
    const ObjectListUsed &list=UseObjects(x,z);
    //const ObjectList &list=listU;
    // optimization - many slots can be skipped
    Assert (!list.IsNull() && list.GetRoadwayCount()>0);
    int n=list.Size();
    for( int i=0; i<n; i++ )
    {
      Object *o=list[i];
      if( !o ) continue; // no collisions with roads
      // check bounding box collision
      const LODShape *lShape=o->GetShape();
      if( !lShape ) continue;
      const Shape *shape=lShape->RoadwayLevel();
      if( !shape ) continue;
      float oRad=lShape->BoundingSphere();
      float distXZ2=(o->FutureVisualState().Position()-pos).SquareSizeXZ();
      if( distXZ2>Square(oRad) ) continue;

      // first check if there is any water in the model at all
      // this is a short circuit optimization - model models do not contain any water
      // and there is no need to animate them and perform other complex processing here just to learn that later

      bool someWaterPresent = false;
      for (int s=0; s<shape->NSections(); s++)
      {
        // we assume here animation does not change "waterness" of the surface
        // we can therefore use the shape information directly
        const ShapeSection &ss = shape->GetSection(s);
        if (ss.GetTexture() && ss.GetTexture()->IsWater()) {someWaterPresent = true;break;}
      }
      
      if (!someWaterPresent) continue;

      Matrix4Val invTransform=o->FutureVisualState().GetInvTransform();
      // roadway ready - check collision
      // has top and sometimes side polygons
      // many polygons can be incident if we look from the top
      Vector3 modelPos=invTransform.FastTransform(pos);
      //int i=0;
      AnimationContextStorageRoadway storage;
      AnimationContext animContext(o->FutureVisualState(),storage);
      shape->InitAnimationContext(animContext, NULL, true); // no convex components for the roadway level
      o->Animate(animContext, lShape->FindRoadwayLevel(), false, o, -FLT_MAX);
      // note: plane may be animated in some cases
      // it may be necessary to recalculate it
      animContext.RecalculateNormalsAsNeeded(shape);
      int fi = 0;
      for (int s=0; s<shape->NSections(); s++)
      {
        const ShapeSection &ss = shape->GetSection(s);
        const PolyProperties &pp = animContext.GetSection(shape, s);
        // check only sections describing water surface
        if (!pp.GetTexture() || !pp.GetTexture()->IsWater()) continue;
        for (Offset f=ss.beg; f<ss.end; shape->NextFace(f),fi++)
        {
          const Poly &face=shape->Face(f);
          const Plane &plane = animContext.GetPlane(shape, fi);
          float pdX,pdZ;
#if _VBS3 //fix for new roadways
          if (-invTransform.Direction().CrossProduct(invTransform.DirectionAside()) 
            * plane.Normal() < 0.0f)
#else
          if (-invTransform.DirectionUp() * plane.Normal() < 0.1)
#endif
            continue; // roadway is probably on animated object and object is lying

#if _VBS3 //fix for new roads
          bool isInside = (plane.Normal().Y() > 0) ? 
            face.InsideFromBottom(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ)
            : face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ);
          if(isInside)
#else
          if (face.InsideFromTop(animContext.GetPosArray(shape), plane, modelPos, &modelPos[1], &pdX, &pdZ))
#endif
          {
            // the face is not sure to be a triangle
            if (shape->GetOrHints() & ClipLandOn )
            {
              float landY = SurfaceY(pos.X(), pos.Z());
              if (waterY <= landY)
                waterY = landY;
            }
            else
            {
              Vector3 wPos = o->FutureVisualState().PositionModelToWorld(modelPos);
              if (waterY<=wPos[1])
                // create normal, transform it, calculate dX, dZ as atan
                waterY = wPos[1];
            }
          }
        }
      }
      o->Deanimate(lShape->FindRoadwayLevel(),false);
    }
  }
  return waterY;
}


float Landscape::RoadSurfaceYAboveWater(Vector3Par pos, float *dX, float *dZ, Texture **texture, UseObjectsMode syncMode, const Object *ignore) const
{
  float y=RoadSurfaceY(pos,dX,dZ,texture,syncMode, NULL, NULL, ignore);
  if (y<MaxWave+MaxTide && y<GetSeaLevelMax())
  {
    float minY=GetSeaLevel(pos);
    if( y<minY )
    {
      y=minY;
      if( dX ) *dX=*dZ=0;
      if( texture ) *texture=GetSeaTexture();
    }
  }
  return y;
}

float Landscape::RoadSurfaceYAboveWater(float xC, float zC, float *dX, float *dZ, Texture **texture, UseObjectsMode syncMode, const Object *ignore) const
{
  float y=RoadSurfaceY(xC,zC,dX,dZ,texture,syncMode, ignore);
  if (y<MaxWave+MaxTide && y<GetSeaLevelMax())
  {
    float minY=GetSeaLevel(xC,zC);
    if( y<minY )
    {
      y=minY;
      if( dX ) *dX=*dZ=0;
      if( texture ) *texture = _seaTexture;
    }
  }
  return y;
}


// Function used for planes and similar situations, where we do not want to intersect with water
float Landscape::SurfaceYAboveWaterNoWaves(float x, float z) const
{
  float y = SurfaceY(x, z, NULL, NULL, NULL);
  // assume tops of the waves are the surface
  if (y < MaxWave + MaxTide)
  {
    float seaY = GetSeaLevelMax();
    if (y < seaY)
      return seaY;
  }
  return y;
}

float Landscape::SurfaceYAboveWater(float x, float z, float *rdX, float *rdZ, Texture **texture) const
{
  float y = SurfaceY(x, z, rdX, rdZ, texture);
  if ((y < MaxWave + MaxTide) && (y < GetSeaLevelMax()))
  {
    float minY = GetSeaLevel(x, z);
    if (y < minY)
    {
      y = minY;
      if (rdX)
        *rdX = *rdZ = 0.0f;
      if (texture)
        *texture = _seaTexture;
    }
  }
  return y;
}

Point3 Landscape::PointOnSurface(float x, float y, float z) const
{
  float surfaceY = SurfaceYAboveWater(x, z);
  return Point3(x, surfaceY + y, z);
}

float Landscape::AboveSurface(Vector3Val pos) const
{
  float surfaceY = SurfaceY(pos.X(),pos.Z());
  return pos.Y() - surfaceY;
}

float Landscape::AboveSurfaceOrWater(Vector3Val pos) const
{
  float surfaceY = SurfaceYAboveWater(pos.X(), pos.Z());
  return pos.Y() - surfaceY;
}

//#define LOG_OBJ_SHAPE "handgrenade"

inline bool IsShape(Object *obj, const char *name)
{
  if (!obj->GetShape()) return false;
  return strstr(obj->GetShape()->Name(),name)!=NULL;
}

ObjectId Landscape::NewObjectId(VisitorObjId visId, int x, int z) const
{
  // Check if the given grid reference is alright
  if ((x > ObjectId::MaxXLandGrid) || (z > ObjectId::MaxZLandGrid))
  {
    ErrorMessage("{error} LandGrid reference exceeded the range [%d, %d], it is [%d, %d]", ObjectId::MaxXLandGrid, ObjectId::MaxZLandGrid, x, z);
  }
  // good candidate 
  OIDInt idListUnique = ObjectId::GetObjIdCandidate(visId);
  const ObjectList &list=_objects(x,z);
  // list needs to be "used" (loaded) - but this is probably true, as otherwise
  // we would not be creating a new object, which is normally the only case
  // when this funtion is used
  for(int attemptsRest=ObjectId::MaxObjectId; --attemptsRest>=0;)
  {
    const int step = 211;
    bool isUnique = true;
    // check if given ID is already used
    for (int i=0; i<list.Size(); i++)
    {
      Object *tst = list[i];
      if (tst->GetObjectId().IsObject() && tst->GetObjectId().GetObjId()==idListUnique)
      {
        idListUnique = ObjectId::GetObjIdCandidate(idListUnique+step);
        isUnique = false;
        break;
      }
    }
    if (isUnique)
    {
      return ObjectId(x,z,idListUnique);
    }
  }
  ErrorMessage("Too many objects in grid rectangle %d,%d",x,z);
  return ObjectId(VehicleId,visId);
}

void Landscape::AddObject( Object *obj, int x, int z, bool avoidRecalculation)
{
  DoAssert(obj->CheckIntegrity());
  #if _ENABLE_REPORT
    // check if object is added into this list correctly
    const float eps = 0.001;
    DoAssert( obj->FutureVisualState().Position().X()>=x*GetLandGrid()-eps || x==0 );
    DoAssert( obj->FutureVisualState().Position().Z()>=z*GetLandGrid()-eps || z==0 );
    DoAssert( obj->FutureVisualState().Position().X()<=(x+1)*GetLandGrid()+eps || x==GetLandRange()-1);
    DoAssert( obj->FutureVisualState().Position().Z()<=(z+1)*GetLandGrid()+eps || z==GetLandRange()-1);
  #endif
  #ifdef LOG_OBJ_SHAPE
    if (IsShape(obj,LOG_OBJ_SHAPE))
    {
      LogF
      (
        "add obj %x:%s at %.2f,%.2f (%d,%d)",obj,obj->GetShape()->Name(),
        obj->Position().X(),obj->Position().Z(),x,z
      );
    }
  #endif

  extern bool LandEditor;
  if (!LandEditor)
  {
    if (obj->GetType()==Primary || obj->GetType()==Network
#if _ARMA3_TERRAIN_DECALS
      || obj->GetType()==LandDecal
#endif 
      )
    {
      DoAssert(obj->GetObjectId().IsObject());
    }
  }

#if _VBS3_LARGE_OBJECTS && !_BINARIZE_REDUCEOBJECT
  // add large dynamic and static objects to R-tree
  obj->AddToRTree();
#endif

  // int index = list.Add(obj,x,z,avoidRecalculation);
  const ObjectList *list = &_objects(x, z);
  if (!list->GetList())
  {
    ObjectList temp(new ObjectListFull(x, z));
    _objects.Set(x, z, temp);
    list = &_objects(x, z);
  }
  // following is not true: list may be locked by sot links and similiar things
  // list must be unlocked - if it is something is wrong, as we may be changing the pointer now
  // it might be cached (and in this case it is still marked as used)
  // if any ObjectListUsed exists into the list, we are deep in trouble
  //Assert(list->GetList()->_used==list->GetList()->IsInList());
  
  int index = list->GetList()->Add(obj, x, z, avoidRecalculation);

  Verify( index>=0 ); // <0  -> error
  DoAssert(obj->CheckIntegrity());

#if _ENABLE_PHYSICS
	if(GetNetworkManager().IsPhysicsMaster() && index >=0 && obj->CheckIntegrity())
	{
		if(!obj->GetPhysics())
			obj->CreatePhysics(EPL_BUILDING);
	}
#endif
}

void Landscape::AddObject(Object *obj, int *xr, int *zr, bool avoidRecalculation)
{
  int x, z;
  _terrain->SelectObjectList(x, z, obj->FutureVisualState().Position().X(), obj->FutureVisualState().Position().Z());
  if (xr)
    *xr = x;
  if (zr)
    *zr = z;
  // add into corresponding list
  AddObject(obj,x,z,avoidRecalculation);

}

void Landscape::Recalculate()
{
  for (int zz=0; zz<GetLandRange(); zz++)
  for (int xx=0; xx<GetLandRange(); xx++)
  {
    const ObjectList &list=_objects(xx,zz);
    list.Recalculate();
  }
}

#define ShapeName(s) ( (s) ? (const char *)(s)->Name() : "<null>" )

void Landscape::RemoveObject(Object *obj)
{
#if _ENABLE_PHYSICS
  obj->RemovePhysics();
#endif

  if (obj->GetObjectId().IsObject())
  {
    int xl = obj->GetObjectId().GetObjX();
    int zl = obj->GetObjectId().GetObjZ();
    const ObjectList &list=_objects(xl,zl);
    for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
    {
      #ifdef LOG_OBJ_SHAPE
        if (IsShape(obj,LOG_OBJ_SHAPE))
        {
          LogF
          (
            "del obj %x:%s at %.2f,%.2f (%d,%d)",obj,obj->GetShape()->Name(),
            obj->Position().X(),obj->Position().Z(),x,z
          );
        }
      #endif
      list.GetList()->Delete(i);
      if (list.CanBeDeleted()) 
        _objects.Set(xl, zl, ObjectList());
      return;
    }
    Fail("ObjId object was moved");
  }
  int x,z;
  _terrain->SelectObjectList(x,z,obj->FutureVisualState().Position().X(),obj->FutureVisualState().Position().Z());
  const ObjectList &list=_objects(x,z);
  for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
  {
    #ifdef LOG_OBJ_SHAPE
      if (IsShape(obj,LOG_OBJ_SHAPE))
      {
        LogF
        (
          "del obj %x:%s at %.2f,%.2f (%d,%d)",obj,obj->GetShape()->Name(),
          obj->Position().X(),obj->Position().Z(),x,z
        );
      }
    #endif
    // list.Delete(i);
    list.GetList()->Delete(i);
    if (list.CanBeDeleted()) 
      _objects.Set(x, z, ObjectList());
    return;
  }
  #if _ENABLE_REPORT
  #ifdef _MSC_VER
  const type_info &type = typeid(*obj);
  LogF
  (
    "Removed object %x:%s (%s) not in landscape %.2f,%.2f (%d,%d).",
    obj,ShapeName(obj->GetShape()),type.name(),
    obj->FutureVisualState().Position().X(),obj->FutureVisualState().Position().Z(),x,z
  );
  #else
  LogF
  (
    "Removed object %x:%s (%s) not in landscape %.2f,%.2f (%d,%d).",
    obj,ShapeName(obj->GetShape()),typeid(*obj).name(),
    obj->Position().X(),obj->Position().Z(),x,z
  );
  #endif
  #endif
  // try to patch it: first search in near slots
  int xMin = x-1, xMax = x+1;
  int zMin = z-1, zMax = z+1;
  saturateMax(xMin,0), saturateMin(xMax,GetLandRange()-1);
  saturateMax(zMin,0), saturateMin(zMax,GetLandRange()-1);
  for (int zz=zMin; zz<=zMax; zz++)
  for (int xx=xMin; xx<=xMax; xx++)
  {
    const ObjectList &list=_objects(xx,zz);
    for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
    {
      // list.Delete(i);
      list.GetList()->Delete(i);
      if (list.CanBeDeleted()) 
        _objects.Set(xx, zz, ObjectList());
      LogF("  found in (%d,%d).",xx,zz);
      return;
    }
  }
  // try to patch it: search for the object in all slots
  for (int zz=0; zz<GetLandRange(); zz++)
  for (int xx=0; xx<GetLandRange(); xx++)
  {
    const ObjectList &list=_objects(xx,zz);
    for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
    {
      // list.Delete(i);
      list.GetList()->Delete(i);
      if (list.CanBeDeleted()) 
        _objects.Set(xx, zz, ObjectList());
      LogF("  found in (%d,%d).",xx,zz);
      return;
    }
  }
}


void Landscape::MoveObject( Object *obj, const Matrix4 &transform )
{
  if (obj->GetObjectId().IsObject())
  {
    int xl = obj->GetObjectId().GetObjX();
    int zl = obj->GetObjectId().GetObjZ();
    const ObjectList &list=_objects(xl,zl);
    obj->SetTransform(transform);
    if( obj->Static() ) list->StaticChanged();
    return;
  }

  int xl,zl;
  _terrain->SelectObjectList(xl,zl,obj->FutureVisualState().Position().X(),obj->FutureVisualState().Position().Z());
  const ObjectList &list=_objects(xl,zl);

  int xm,zm;
  _terrain->SelectObjectList(xm,zm,transform.Position().X(),transform.Position().Z());
  const ObjectList *move=&_objects(xm,zm);

  #ifdef LOG_OBJ_SHAPE
    if (IsShape(obj,LOG_OBJ_SHAPE))
    {
      LogF
      (
        "move trn obj %x:%s %.2f,%.2f (%d,%d)->%.2f,%.2f (%d,%d)",
        obj,obj->GetShape()->Name(),
        obj->Position().X(),obj->Position().Z(),xl,zl,
        transform.Position().X(),transform.Position().Z(),xm,zm
      );
    }
  #endif
  obj->SetTransform(transform);
  if( &list==move)
  {
    // recalc b-sphere
    // this should happen only in Buldozer
    DoAssert(list.GetList());
    if( obj->Static() && list.GetList())
      list->StaticChanged();
    return;
  }

  // Verify( move.Add(obj,xm,zm)>=0 );
  if (!move->GetList())
  {
    ObjectList temp(new ObjectListFull(xm, zm));
    _objects.Set(xm, zm, temp);
    move = &_objects(xm, zm);
  }

#if _ENABLE_REPORT
  //reported NULL list from dedicated server, xm = 15, zm = 0
  if(!move->GetList()) ErrF("Move ObjectListFull is NULL.  xm=%d  zm=%d flag=%d", xm, zm, _objects.GetFlagRaw() );
#endif

  if (move->GetList()) Verify(move->GetList()->Add(obj,xm,zm) >= 0); // <0  -> error
  
  for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
  {
    // list.Delete(i);
    list.GetList()->Delete(i);
    if (list.CanBeDeleted())
      _objects.Set(xl, zl, ObjectList());
    return;
  }
#if _ENABLE_REPORT
#ifdef _MSC_VER
  const type_info &type = typeid(*obj);
  ErrF("Moved object %x:%s (%s) not in landscape %.2f,%.2f (%d,%d).",
    obj,ShapeName(obj->GetShape()),type.name(), obj->FutureVisualState().Position().X(),obj->FutureVisualState().Position().Z(),xl,zl);
#else
  ErrF("Moved object %x:%s (%s) not in landscape %.2f,%.2f (%d,%d).",
    obj,ShapeName(obj->GetShape()),typeid(*obj).name(),obj->Position().X(),obj->Position().Z(),xl,zl);
#endif
#endif
  // try to patch it: first search in near slots
  int xMin = xl-1, xMax = xl+1;
  int zMin = zl-1, zMax = zl+1;
  saturateMax(xMin,0), saturateMin(xMax,GetLandRange()-1);
  saturateMax(zMin,0), saturateMin(zMax,GetLandRange()-1);
  for (int zz=zMin; zz<=zMax; zz++)
  for (int xx=xMin; xx<=xMax; xx++)
  {
    const ObjectList &list=_objects(xx,zz);
    for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
    {
      // list.Delete(i);
      list.GetList()->Delete(i);
      if (list.CanBeDeleted())
        _objects.Set(xx, zz, ObjectList());
      RptF("  found in (%d,%d).",xx,zz);
      return;
    }
  }

  // try to patch it: search for the object in all slots
  for (int zz=0; zz<GetLandRange(); zz++)
  for (int xx=0; xx<GetLandRange(); xx++)
  {
    const ObjectList &list=_objects(xx,zz);
    for( int i=0; i<list.Size(); i++ ) if( list[i]==obj )
    {
      // list.Delete(i);
      list.GetList()->Delete(i);
      if (list.CanBeDeleted())
        _objects.Set(xx, zz, ObjectList());
      RptF("  found in (%d,%d).",xx,zz);
      return;
    }
  }
}

const ObjectList &Landscape::GetList(Object *obj) const
{
  if (obj->GetObjectId().IsObject())
  {
    int xl = obj->GetObjectId().GetObjX();
    int zl = obj->GetObjectId().GetObjZ();
    return _objects(xl,zl);
  }
  else
  {
    int xl,zl;
    _terrain->SelectObjectList(xl,zl,obj->FutureVisualStateRaw().Position().X(),obj->FutureVisualStateRaw().Position().Z());
    return _objects(xl,zl);
  }
}

void Landscape::MoveObject( Object *obj, Vector3Par pos )
{
  if (obj->GetObjectId().IsObject())
  {
    int xl = obj->GetObjectId().GetObjX();
    int zl = obj->GetObjectId().GetObjZ();
    const ObjectList &list=_objects(xl,zl);
    obj->SetPosition(pos);
    // recalc b-sphere
    // this should happen only in Buldozer
    if( obj->Static() ) list->StaticChanged();
    return;
  }
  int xl,zl;
  _terrain->SelectObjectList(xl,zl,obj->FutureVisualStateRaw().Position().X(),obj->FutureVisualStateRaw().Position().Z());
  const ObjectList &list=_objects(xl,zl);

  int xm,zm;
  _terrain->SelectObjectList(xm,zm,pos.X(),pos.Z());
  const ObjectList *move=&_objects(xm,zm);
#if _ENABLE_REPORT
  //const type_info &type = typeid(*obj);
  float oldX = obj->FutureVisualState().Position().X();
  float oldZ = obj->FutureVisualState().Position().Z();
#endif

  #ifdef LOG_OBJ_SHAPE
    if (IsShape(obj,LOG_OBJ_SHAPE))
    {
      LogF
      (
        "move pos obj %x:%s %.2f,%.2f (%d,%d)->%.2f,%.2f (%d,%d)",
        obj,obj->GetShape()->Name(),
        obj->Position().X(),obj->Position().Z(),xl,zl,
        pos.X(),pos.Z(),xm,zm
      );
    }
  #endif
  obj->SetPosition(pos);
  if( &list==move)
  {
    // recalc bsphere
    // this should happen only in buldozer
    if( obj->Static() ) list->StaticChanged();
    /*
    LogF
    (
      "Moved object %x:%s (%s) in same square %.2f,%.2f->%.2f,%.2f (%d,%d),(%d,%d).",
      obj,ShapeName(obj->GetShape()),type.name(),
      oldX,oldZ,pos.X(),pos.Z(),xl,zl,xm,zm
    );
    */
    return;
  }

  for (int i = 0; i < list.Size(); i++)
  {
    if (list[i] != obj)
      continue;
    //Verify( move.Add(obj,xm,zm)>=0 ); // <0  -> error
    if (!move->GetList())
    {
      ObjectList temp(new ObjectListFull(xm, zm));
      _objects.Set(xm, zm, temp);
      move = &_objects(xm, zm);
    }
    Verify(move->GetList()->Add(obj,xm,zm) >= 0); // <0  -> error

    // list.Delete(i);
    list.GetList()->Delete(i);
    if (list.CanBeDeleted())
      _objects.Set(xl, zl, ObjectList());
    /*
    LogF("Moved object %x:%s (%s) in diff square %.2f,%.2f->%.2f,%.2f (%d,%d),(%d,%d).",
      obj,ShapeName(obj->GetShape()),type.name(),oldX,oldZ,pos.X(),pos.Z(),xl,zl,xm,zm);
    */
    return;
  }

#if _ENABLE_REPORT
#ifdef _MSC_VER
  const type_info &type = typeid(*obj);
  LogF("Moved object %x:%s (%s) not in landscape %.2f,%.2f (%d,%d).",
    obj,ShapeName(obj->GetShape()),type.name(),oldX,oldZ,xl,zl);
#else
  LogF("Moved object %x:%s (%s) not in landscape %.2f,%.2f (%d,%d).",
    obj,ShapeName(obj->GetShape()),typeid(*obj).name(),oldX,oldZ,xl,zl);
#endif
  #endif
  // try to patch it: first search in near slots
  int xMin = xl-1, xMax = xl+1;
  int zMin = zl-1, zMax = zl+1;
  saturateMax(xMin,0), saturateMin(xMax,GetLandRange()-1);
  saturateMax(zMin,0), saturateMin(zMax,GetLandRange()-1);
  for (int zz=zMin; zz<=zMax; zz++)
  for (int xx=xMin; xx<=xMax; xx++)
  {
    const ObjectList &list = _objects(xx,zz);
    for (int i = 0; i < list.Size(); i++)
    {
      if (list[i] != obj)
        continue;
      //Verify( move.Add(obj,xm,zm)>=0 ); // <0  -> error
      if (!move->GetList())
      {
        ObjectList temp(new ObjectListFull(xm, zm));
        _objects.Set(xm, zm, temp);
        move = &_objects(xm, zm);
      }
      Verify(move->GetList()->Add(obj,xm,zm) >= 0); // <0  -> error

      // list.Delete(i);
      list.GetList()->Delete(i);
      if (list.CanBeDeleted())
        _objects.Set(xx, zz, ObjectList());
      LogF("  found in (%d,%d), moving to %.2f,%.2f (%d,%d)", xx,zz,pos.X(),pos.Z(),xm,zm);
      return;
    }
  }

  // try to patch it: search for the object in all slots
  for (int zz=0; zz<GetLandRange(); zz++)
  for (int xx=0; xx<GetLandRange(); xx++)
  {
    const ObjectList &list = _objects(xx, zz);
    for (int i = 0; i < list.Size(); i++)
    {
      if (list[i] != obj)
        continue;
      // Verify( move.Add(obj,xm,zm)>=0 ); // <0  -> error
      if (!move->GetList())
      {
        ObjectList temp(new ObjectListFull(xm, zm));
        _objects.Set(xm, zm, temp);
        move = &_objects(xm, zm);
      }
      Verify(move->GetList()->Add(obj,xm,zm) >= 0); // <0  -> error

      // list.Delete(i);
      list.GetList()->Delete(i);
      if (list.CanBeDeleted())
        _objects.Set(xx, zz, ObjectList());
      LogF("  found in (%d,%d), moving to %.2f,%.2f (%d,%d)",xx,zz,pos.X(),pos.Z(),xm,zm);
      return;
    }
  }
}

/** Function moves object. The main difference between Landscape::MoveObject is
 * that if object has hashed grid position into its ID and it is moved into another grid cell
 * object is replaced with one with new correct ID. 
 * param obj - (in) an object to be moved. BE WARE! If you want to use object after MoveObjectFar create reference to it object befour calling this function.
 * param pos - (in) a new object position
 * @see #MoveObjectFar( Object *, Matrix4Val  )
 * @see #MoveObject(Object *, Matrix4Val)
 * @see #MoveObject(Object *, Vector3Par)
 */
void Landscape::MoveObjectFar(Object *obj, Vector3Par pos)
{
  Matrix4 trans = obj->FutureVisualState().Transform();
  trans.SetPosition(pos);
  MoveObjectFar(obj, trans);
}

/** Function moves object. The main difference between Landscape::MoveObject is
* that if object has hashed grid position into its ID and it is moved into another grid cell
* object is replaced with one with new correct ID. 
* param obj - (in) an object to be moved. BE WARE! If you want to use object after MoveObjectFar create reference to it object befour calling this function.
* param pos - (in) a new object position
* @see #MoveObjectFar( Object *, Vector3Par  )
* @see #MoveObject(Object *, Matrix4Val)
* @see #MoveObject(Object *, Vector3Par)
*/
void Landscape::MoveObjectFar(Object *obj, Matrix4Val trans)
{
  if (obj->GetObjectId().IsObject())
  {
    int xl = obj->GetObjectId().GetObjX();
    int zl = obj->GetObjectId().GetObjZ();
    
    int xm,zm;
    _terrain->SelectObjectList(xm,zm,trans.Position().X(),trans.Position().Z());
    
    if (xm != xl || zm != zl)
    {
      // The main functionality of the function. Moving object into new square means to change his ID.            
      Ref<Object> objNew = ObjectCreate(obj->ID(), obj->GetShape()->Name(), obj->GetShape()->Remarks(), trans, xm, zm);

      // Delete old object. 
      ObjectDestroy(obj);
      return;    
    }
    else
    {
      const ObjectList &list=_objects(xl,zl);  
#if _ENABLE_BULDOZER
      Matrix4 pos = trans;
      obj->InitSkew(this,pos);
      obj->SetTransform(pos);
#else
      obj->SetTransform(trans);
#endif
      if (obj->Static())
        list->StaticChanged();
      return;
    }
  }
  else
    Landscape::MoveObject(obj, trans);
}

void Landscape::InitObjectVehicles()
{
  for( int x=0; x<GetLandRange(); x++ ) for( int z=0; z<GetLandRange(); z++ )
  {
    // we do not need to use UseObjects, because we know all vehicles are persistent or non-discardable
    const ObjectList &list=_objects(x,z);
    for( int i=0; i<list.Size(); i++ )
    {
      Object *obj=list[i];
      if( !obj ) continue;
      if( obj->GetType()!=Primary ) continue;
      if( !obj->GetShape() ) continue;

      const char *className=obj->GetShape()->GetPropertyClass();
      if( className && *className )
      {
        Entity *vehicle=dyn_cast<Entity>(obj);
        if (vehicle)
        {
#if _VBS3 //clear variables of map objects
          EntityAI* entAI = dyn_cast<EntityAI>(vehicle);
          if(entAI)
            entAI->GetVars()->_vars.Clear();
#endif
          const EntityType *type = vehicle->GetNonAIType();
          if (type->IsSimulationRequired() || type->IsSoundRequired())
          {
            vehicle->Init(vehicle->GetFrameBase(), true);
            vehicle->OnInitEvent();
            GWorld->InsertSlowVehicle(vehicle);
          }
        }
      }
    }
  }
}


Object *Landscape::AddObject(Vector3Par pos, float head, LODShape *shape, void *user)
{
  //Fail("Old positioning used.");
  int id=NewObjectID();
  Object *obj=NewObject(shape,VisitorObjId(id));
  //_objectIds.Access(id);
  //_objectIds[id]=obj;
  if( head ) obj->SetTransform( Matrix4(MRotationY, head*(H_PI/180) ) );
  Point3 nPos=pos+obj->FutureVisualState().DirectionModelToWorld(shape->BoundingCenter());
  obj->SetPosition(nPos);
  AddObject(obj);
  return obj;
}

static RStringB ForestString("forest");

static bool CheckSkew(LODShape *shape, const Matrix4 &transform)
{
  // if shape is not SW animated, there is nothing to optimize
  if (shape->GetAnimationType()!=AnimTypeSoftware) return false;
  // id shape uses other animation than ClipLandKeep, we are unable to optimize
  if ((shape->GetOrHints()&ClipLandMask)!=ClipLandKeep) return false;
  if ((shape->GetAndHints()&ClipLandMask)!=ClipLandKeep) return false;
  if (shape->GetPropertyClass()==ForestString) return false;
  // auto detect skew settings
  // scan all vertices in lod 0
  return true;
}

Object *Landscape::ObjectCreate(OIDInt id, const char *name, const Matrix4 &transform, bool avoidRecalculation, const char * forceName)
{
  int x,z;
  _terrain->SelectObjectList(x,z,transform.Position().X(),transform.Position().Z());
  // we need to know the object dimensions
  Ref<LODShape> shape = Shapes.New(name,ShapeShadow,forceName);
  ShapeParameters pars = ShapeShadow;
  if (shape)
  {
    if (CheckSkew(shape,transform))
      pars |= ShapeNotAnimated;
  }
  return ObjectCreate(id,name,pars,transform,x,z,avoidRecalculation);
}

Object *Landscape::ObjectCreate(OIDInt id, const char *name, ShapeParameters pars, const Matrix4 &transform, int x, int z, bool avoidRecalculation)
{
  // create object based on Class hint in LOD 0
  // if there is a corresponding class, there is no need to load the shape
#if _ENABLE_REPORT
    if (*name==0)
      RptF("Object with empty name at %.3f,%.3f",transform.Position().X(),transform.Position().Z());
#endif
  Ref<LODShape> shape=Shapes.New(name,pars);
  VisitorObjId visId = VisitorObjId(id);
  CreateObjectId oid(visId,NewObjectId(visId,x,z));

#if _ENABLE_REPORT
  if (CheckSkew(shape,transform))
    RptF("WARN: %s - Shape %s not skewed (Wrp file needs rebinarizing?)",(const char *)GetName(),(const char *)shape->GetName());
#endif

  Object *obj=NewObject(shape,oid);

  if( obj->GetType()!=Primary && obj->GetType()!=Network 
#if _ARMA3_TERRAIN_DECALS
    && obj->GetType()!=LandDecal
#endif    
    )
    obj->SetType(Primary);
  
  // object id list cleared after load is finished
  //_objectIds.Access(id);
  //_objectIds[id]=obj;
  Assert( obj->ID()==visId );
  Assert( obj->GetObjectId()==oid._objId );
  // check matrix type

  Matrix4 nTransform=transform;

  #if 0
    // "repair" matrix
    // matrix should be scaled rotation
    // (S*R, where S is scale matrix and R is rotation)
    // some matrices must not be repaired
    // this holds especially for forests
    if(shape->GetPropertyClass()!=ForestString)
    {
      float scale = obj->GetType() == Network ? 1.0f : nTransform.Scale();
      // if object is slope following, up should be VUp
      //if (shape->GetAndHints()&ClipLandKeep)
      if (shape->GetOrHints()&ClipLandKeep)
        nTransform.SetUpAndAside(VUp,nTransform.DirectionAside());
      else
        nTransform.SetUpAndAside(nTransform.DirectionUp(),nTransform.DirectionAside());
      nTransform.SetScale(scale);
      // if we are fixing a matrix, we need to keep bounding center on the ground
      // TODO: check if matrix was wrong and report it
      // TODO: repair only in Buldozer
    }
  #endif

  // while changing a transform, we need to keep bounding center alignment to the ground
  Vector3 bCenter = transform.FastTransform(-shape->BoundingCenter());
  Vector3 newBCenter = nTransform.FastTransform(-shape->BoundingCenter());
  Vector3 move = bCenter-newBCenter;
  
  nTransform.SetPosition(nTransform.Position()+VUp*move.Y());
  
#if 0 // _ENABLE_REPORT
  if (fabs(move.Y())>0.05f)
    LogF("%s moved: %g,%g,%g",cc_cast(obj->GetDebugName()),move[0],move[1],move[2]);
#endif
  
  obj->SetTransformPossiblySkewed(nTransform);
  obj->InitSkew(this);

  AddObject(obj,x,z,avoidRecalculation);

  Assert( obj->ID()==visId );
  Assert( obj->GetObjectId()==oid._objId );

  #ifdef INTERESTED_IN_ID
    if (obj->ID()==INTERESTED_IN_ID)
    {
      Vector3 pos = nTransform.Position();

      LODShape *shape = obj->GetShape();
      Vector3 bcWorld = shape ? nTransform.FastTransform(-shape->BoundingCenter()) : pos;
      float sourceSurfY = SurfaceYSource(pos.X(),pos.Z());
      LogF("Id %d: pos %.3f,%.3f,%.3f, bcWorld %.3f,%.3f,%.3f, surfY %.3f bc %.3f,%.3f,%.3f",
        obj->ID(), pos.X(),pos.Y(),pos.Z(), bcWorld.X(),bcWorld.Y(),bcWorld.Z(), sourceSurfY,
        -shape->BoundingCenter().X(),-shape->BoundingCenter().Y(),-shape->BoundingCenter().Z());
    }
  #endif

  return obj;
}

void Landscape::ObjectTypeChange(const EditorSelectionItem &id, const char *name )
{
  OLink(Object) obj = GetObject(id.id, id.pos);
  Matrix4Val transform=obj->FutureVisualState().Transform();
  RemoveObject(obj); // remove object from the old position
  // obj no longer valid
  LODShape *shape=Shapes.New(name,false,true);
  obj=NewObject(shape, id.id);
  obj->SetTransform(transform);
  AddObject(obj); // insert on new position
}

#if _ENABLE_PHYSICS
void Landscape::SimulatePhysics(float timeslice)
{
  PROFILE_SCOPE_EX(bulletSim,phys);

  if(_pPhysWorld && GetNetworkManager().IsPhysicsMaster())
	  _pPhysWorld->Advance(timeslice);
}
#endif

void Landscape::SetSelection( const AutoArray<EditorSelectionItem> &sel )
{
  PROFILE_SCOPE(arrow);

  if (GScene->CheckArrows()) return;
  // avoid destroying the arrow shape
  GameState *gstate = GWorld->GetGameState();
  GameValue list = gstate->CreateGameValue(GameArray);
  GameArrayType &array = list;
  array.Realloc(sel.Size());

  for( int index=0; index<sel.Size(); index++ )
  {
    OLink(Object) obj = GetObject(sel[index].id, sel[index].pos);
    // create a temporary object for drawing
    // use object min-max box to determine arrow position
    if( !obj ) continue;
    LODShape *shape=obj->GetShape();
    Entity *sel=GScene->CreateSelArrow();
    if (sel)
    {
      LODShape *arrow=sel->GetShape();
      float topLevelShape=shape->Max().Y();
      // arrow will be rotated, maxZ will become minY
      float bottomLevelArrow=arrow->Max().Z();
      sel->SetOrientation(Matrix3(MRotationX,H_PI/2));
      sel->SetPosition(obj->FutureVisualState().Position()+Vector3(0,topLevelShape+bottomLevelArrow,0));
      
      Vector3P selpos = sel->FutureVisualState().Position();
      
      GScene->ShowObject(sel);      
      
    }
    GameValue CreateGameObject(Object *obj);
    array.Add(CreateGameObject(obj));
  } 
  gstate->VarSet("bis_buldozer_selection", list, true, false, GWorld->GetMissionNamespace()); // what namespace?
}

void Landscape::ObjectMove(const EditorSelectionItem &id, const Matrix4 &transform)
{
  Object * obj = GetObject(id.id, id.pos);
  if (obj)
    MoveObjectFar(obj, transform);
  else
  {
#if _HELISIM_LARGE_OBJECTID
    LogF("Object not moved -> not found id %I64d", id.id.Encode());
#else
    LogF("Object not moved -> not found id %d", id.id.Encode());
#endif // _HELISIM_LARGE_OBJECTID
  }
}

/** Function destroys object and release all its refs. 
* param obj - (in) an object to be destroyed. 
*/
void Landscape::ObjectDestroy(Object * obj)
{
  // Release all refs and destroy object
  GameState *gstate = GWorld->GetGameState();
  GameValue list = gstate->CreateGameValue(GameArray);
  gstate->VarSet("bis_buldozer_selection", list, true, false, GWorld->GetMissionNamespace()); // what namespace?

#if _HELISIM_RENDER_TO_TEXTURE
  GScene->ReleaseDrawObject(obj);
#else
  SortObject * sortObject;
  if ((sortObject = obj->GetInList()) != NULL)
    GScene->ReleaseDrawObject(sortObject);
#endif

  RemoveObject(obj);
}

/** Function destroys object and release all its refs. 
* param id - (in) anID of object. 
*/
void Landscape::ObjectDestroy(const EditorSelectionItem &id)
{
  OLink(Object) obj = GetObject(id.id, id.pos);
  Assert( obj );
  if( obj )
    ObjectDestroy(obj); // remove object from the old position
}

DEFINE_FAST_ALLOCATOR(ObjectListFull)

int ObjectListFull::CountNonStatic() const
{
  int ret=0;
  int n=Size();
  for( int i=0; i<n; i++ )
  {
    Object *obj=Get(i);
    if( obj->Static() ) continue;
    ret++;
  }
  return ret;
}

int ObjectListFull::CountRoadways() const
{
  int ret=0;
  int n=Size();
  for( int i=0; i<n; i++ )
  {
    Object *obj=Get(i);
#if _VBS3_CRATERS_LATEINIT
    if (!obj->HasRoadway()) continue;
#else
    if( !obj->GetShape() || obj->GetShape()->FindRoadwayLevel()<0 || obj->GetRadius()<=IgnoreSmallRoadways) continue;
#endif
    ret++;
  }
  return ret;
}

int ObjectListFull::CountPrimary() const
{
  int ret=0;
  int n=Size();
  for( int i=0; i<n; i++ )
  {
    Object *obj=Get(i);
    if( !obj->GetObjectId().IsObject()) continue;
    ret++;
  }
  return ret;
}

void ObjectListFull::ChangeRoadwayCount(int val)
{
  _nRoadways += val;
#if _DEBUG
  if( CountRoadways()!=_nRoadways )
    Fail("ChangeRoadwayCount: _nRoadways not valid.");
#endif
}


void ObjectListFull::ChangeNonStaticCount(int val)
{
  _nNonStatic += val;
#if _DEBUG
  if( CountNonStatic()!=_nNonStatic )
    Fail("ChangeNonStaticCount: _nNonStatic not valid.");
#endif
}

void ObjectListFull::AddStaticBoundingVolume(Object *obj)
{
  // we need the radius information for rendering purposes only
  // invisible objects are not interesting in this respect
  if (obj->Invisible()) return;
  if (!obj->GetShape()) return;
  float radius = obj->GetRadius();
  float dist=obj->FutureVisualState().Position().Distance(_bCenter)+radius;
  saturateMax(_bRadius,dist);
  saturateMax(_maxStaticEstArea,obj->EstimateArea());
  // if we want to calculate world space min/max
  // we need to convert all 8 corners to world space
  // sometimes this is not needed, like when object bsSphere is already in the min-max box
  // TODO: is _bRadius was enlarged, we know we have to enlarge minmax as well
  // and there is no need to test sphere minmax
  Vector3 bSphereMin = obj->FutureVisualState().Position()-Vector3(radius,radius,radius);
  Vector3 bSphereMax = obj->FutureVisualState().Position()+Vector3(radius,radius,radius);
  if (
    bSphereMin.X()<_minMax[0].X() || bSphereMax.X()>_minMax[1].X() ||
    bSphereMin.Y()<_minMax[0].Y() || bSphereMax.Y()>_minMax[1].Y() ||
    bSphereMin.Z()<_minMax[0].Z() || bSphereMax.Z()>_minMax[1].Z()
  )
  {
    const Vector3 *minmax = obj->GetShape()->MinMax();
    for (int z=0; z<2; z++) for (int y=0; y<2; y++) for (int x=0; x<2; x++)
    {
      Vector3 pos(minmax[x].X(),minmax[y].Y(),minmax[z].Z());
      Vector3 wPos = obj->FutureVisualState().PositionModelToWorld(pos);
      SaturateMin(_minMax[0],wPos);
      SaturateMax(_minMax[1],wPos);
    }
  }
}

void ObjectListFull::StaticAdded(Object *obj)
{
  if (Size()==1)
  {
    // first estimation
    _bRadius = LandGrid;
    _minMax[0] = Vector3(+FLT_MAX,+FLT_MAX,+FLT_MAX);
    _minMax[1] = Vector3(-FLT_MAX,-FLT_MAX,-FLT_MAX);
  }
  Assert(obj->Static());
  AddStaticBoundingVolume(obj);
}

void ObjectListFull::StaticChanged() // recalculate what is necessary
{
  // recalculate bounding sphere
  _bRadius = 0;
  _maxStaticEstArea = 0;
  _minMax[0] = Vector3(+FLT_MAX,+FLT_MAX,+FLT_MAX);
  _minMax[1] = Vector3(-FLT_MAX,-FLT_MAX,-FLT_MAX);
  for( int i=0; i<Size(); i++ )
  {
    Object *obj=Get(i);
    if( !obj->Static() ) continue;
    AddStaticBoundingVolume(obj);
  }
  // we have a b-sphere which was computed from a terrain center point
  
  // we should be able to find a lot better solution

  UpdateGroups();
}

static inline int CmpObjectGroup(const Ref<Object> *o1, const Ref<Object> *o2)
{
  // non-static objects must go last
  Object *oo1 = *o1;
  Object *oo2 = *o2;
  bool isStatic1 = oo1->GetObjectId().IsObject();
  bool isStatic2 = oo2->GetObjectId().IsObject();
  if (isStatic1 != isStatic2)
    return isStatic2-isStatic1;
  // 
  const LODShape *s1 = oo1->GetShape();
  const LODShape *s2 = oo2->GetShape();
  // identical shape means the same group
  if (s1 == s2)
    return 0;
  // sort shapes based on shape size - EstimateArea used because of VASI
  float s1Size = s1 ? oo1->EstimateArea() : 0;
  float s2Size = s2 ? oo2->EstimateArea() : 0;
  int ret = sign(s2Size-s1Size);
  if (ret)
    return ret;
  // if same size, sort by shape pointer
  if (s1>s2) return +1;
  if (s1<s2) return -1;
  return 0;
}

void ObjectListFull::UpdateGroups()
{
  // sort by shape, big models first, non-static models last
  QSort(*this,CmpObjectGroup);
  // recalculate groups
  _groups.Resize(0);
  LODShape *grpShape = NULL;
  ObjectListGroup *grp = NULL;
  for (int i=0; i<Size(); i++)
  {
    Object *o = Get(i);
    // dynamic objects should be last
    if (!o->GetObjectId().IsObject())
      break;
    // non-shaped objects should be after the shaped ones
    if (!o->GetShape())
      break;
    if (grpShape && o->GetShape()==grpShape)
    {
      grp->_end = i+1;
      saturateMax(grp->_maxArea,o->EstimateArea());
    }
    else
    {
      // we need to start a new group
      grp = &_groups.Append();
      grp->_maxArea = o->EstimateArea();
      grp->_end = i+1;
      grpShape = o->GetShape();
    }
  }
  _groups.Compact();
}

void ObjectListFull::SetBSphere(Vector3Par center, float radius, const Vector3 *minmax)
{
  _bCenter=center;
  _bRadius=radius;
  _minMax[0] = minmax[0];
  _minMax[1] = minmax[1];
}

// default values for landscape grid x,z
void ObjectListFull::SetBSphere(int x, int z)
{
  _bCenter.Init();
  _bCenter[0]=x*LandGrid+LandGrid*0.5;
  _bCenter[2]=z*LandGrid+LandGrid*0.5;
  _minMax[0] = Vector3(+FLT_MAX,+FLT_MAX,+FLT_MAX);
  _minMax[1] = Vector3(-FLT_MAX,-FLT_MAX,-FLT_MAX);
  if (GLandscape)
    _bCenter[1]=GLandscape->SurfaceY(_bCenter[0],_bCenter[2]);
  else
    _bCenter[1]=50; // some estimation
  _bRadius=LandGrid; // default value
}

int ObjectListFull::Add(Object *object, int x, int z, bool avoidRecalculation)
{
  int index=base::Add(object);
  if( object->Static() )
  {
    if (!avoidRecalculation)
      StaticAdded(object);
  }
  else ChangeNonStaticCount(+1);
#if _VBS3_CRATERS_LATEINIT
  if (object->HasRoadway())
#else
  if (object->GetShape() && object->GetShape()->FindRoadwayLevel()>=0 && object->GetRadius()>IgnoreSmallRoadways)
#endif
  {
    ChangeRoadwayCount(+1);
    if (!avoidRecalculation && GLandscape->GrassMapPresent())
    {
      GLandscape->ForceClutterUpdate(object);
      GLandscape->UpdateGrassMap(x,z);
    }
  }
  return index;
}

void ObjectListFull::Delete(int index)
{
  Object *obj = Get(index);
  bool isStatic = obj->Static();
#if _VBS3_CRATERS_LATEINIT
  bool hasRoadway = obj->HasRoadway();
#else
  bool hasRoadway = obj->GetShape() && obj->GetShape()->FindRoadwayLevel()>=0 && obj->GetRadius()>IgnoreSmallRoadways;
#endif
  base::Delete(index);
  // if it is grouped, it needs to be static
  #if _ENABLE_REPORT
  if (_groups.Size()>0)
  {
    int lastGrouped = _groups.Last()._end;
    if (index<lastGrouped)
    {
      DoAssert(isStatic);
      if (!isStatic)
        StaticChanged();
    }
  }
  #endif
  if( isStatic ) StaticChanged();
  else ChangeNonStaticCount(-1);
  if (hasRoadway)
  {
    ChangeRoadwayCount(-1);
    // it might be wise to perform a clutter update for the grid
    //GLandscape->ForceClutterUpdate(object);
  }
}

/// helper functor for ObjectListFull::DoRelease
class ObjectListFullDoReleaseCheck
{
  Object *_obj;
public:
  explicit ObjectListFullDoReleaseCheck(Object *obj):_obj(obj){}
  
  bool operator()(Entity *vehicle) const
  {
    if (_obj == vehicle)
    {
      ObjectId id = _obj->GetObjectId();
      RptF("Error: Releasing vehicle %s at %d,%d",(const char *)vehicle->GetDebugName(),id.GetObjX(),id.GetObjZ());
      return true;
    }
    return false;
  }
};

/**
@param x [out] x coordinate of released rectangle
@param z [out] z coordinate of released rectangle
*/
void ObjectListFull::DoRelease(int &x, int &z)
{
  DoAssert(_used==0);
  x = z  = -1;
#if _ENABLE_REPORT
  bool error = false;
  if (GWorld) for (int oi=0; oi<Size(); oi++)
  {
    Object *obj = Get(oi);
    // if vehicle is in the vehicle list, it must be Entity
    if (obj->GetType()==Primary || obj->GetType()==Network
#if _ARMA3_TERRAIN_DECALS
      || obj->GetType()==LandDecal
#endif
      )
    {
      if (obj->RefCounter()==1) continue;
      ObjectId id = obj->GetObjectId();
      RptF("Error: DoRelease - RefCount of %s at %d,%d is %d",(const char *)obj->GetDebugName(),id.GetObjX(),id.GetObjZ(),obj->RefCounter());

      if (!dyn_cast<Entity>(obj)) continue;
      // if vehicle is in the vehicle list, it must have corresponding RefCount
      // try to find it in the list
      ObjectListFullDoReleaseCheck check(obj);
      if (GWorld->ForEachVehicle(check) || GWorld->ForEachSlowVehicle(check))
        error = true;
    }
  }
  DoAssert(!error);
  if (error)
    return;
#endif

  // during unloading we pretend we are loaded
  // this way anyone can still use links into this grid and similiar things
  _used++;
#if 0 //_ENABLE_REPORT
  MemoryBreakpoint watchUsed;
  watchUsed.Set(&_used);
#endif
  
  for (int oi=0; oi<Size();)
  {
    Object *obj = Get(oi);
    if (obj->GetType()==Primary || obj->GetType()==Network
#if _ARMA3_TERRAIN_DECALS
      || obj->GetType()==LandDecal
#endif 
      )
    {
      #if _ENABLE_REPORT
      // check if object can be released safely
      // check object state
      if (obj->GetDestroyed()>0)
        LogF("Warning: Object %s released while destroyed",(const char *)obj->GetDebugName());
      RString name = obj->GetDebugName();
      Ref<LODShape> shape = obj->GetShape();
      int wasUsed = _used;
      #endif
      const ObjectId &id = obj->GetObjectId();
      DoAssert(id.IsObject());
      if (id.IsObject())
      {
         x = id.GetObjX();
         z = id.GetObjZ();
      }
      
      Delete(oi);
#if _ENABLE_REPORT
      if (_used!=wasUsed)
        LogF("Refcount changed to %d while deleting %s:%s",_used,(const char *)name,shape ? (const char *)shape->GetName() : "");
#endif
    }
    else
      oi++;
  }
#if _ENABLE_REPORT
  //watchUsed.Clear();
#endif
  _used--;
  DoAssert(_used==0);
}

size_t ObjectListFull::GetMemoryControlled() const
{ // count only memory used by the list and object headers, not its shapes
  size_t size = GetMemoryAllocated()+sizeof(*this)+_memoryUsedByDiscardable;
  return size;
}

void ObjectListFull::Clear()
{
  base::Clear();
  _nNonStatic=0;
  _nRoadways=0;
  _groups.Clear();
  if( CountNonStatic()!=_nNonStatic )
    Fail("Clear: _nNonStatic not valid.");
  if( CountRoadways()!=_nRoadways )
    Fail("Clear: _nRoadways not valid.");
}

ObjectListFull::ObjectListFull(int x, int z)
:_bRadius(1e10),
_nNonStatic(0),
_nRoadways(0),
_used(0),
_maxStaticEstArea(0),
_memoryUsedByDiscardable(0)
{
  SetBSphere(x,z);
}

ObjectListFull::~ObjectListFull()
{
  // make sure list is no longer cached when it is being destroyed
  // note: some lists may be persistent
  //DoAssert(!IsUsed());
  DoAssert(!IsInList());
  Clear();
}

/// get x,z coordinates based on data inspection
void ObjectListFull::GetXZ(int &x, int &z)
{
  // if there is any unloadable object,
  // we should be able to tell the ID based on the object id
  int n = Size();
  x = -1;
  z = -1;
  for (int i=0; i<n; i++)
  {
    Object *obj = Get(i);
    const ObjectId &id = obj->GetObjectId();
    if (!id.IsObject()) continue;
    // if we have any object, it will do
    x = id.GetObjX();
    z = id.GetObjZ();
    return;
  }
}

/*!
\param list list to use
\param cache if false, no caching is used, whole landscape is permanently loaded
\param land when NULL is passed, no list management is allowed (MT safe variant)

\note constructor can be used only from Landscape::UseObjects
During existence of ObjectListUsed object the ObjectListFull object must not change.
This could happen when a new entity is inserted into a list which was empty before
*/
ObjectListUsed::ObjectListUsed(const ObjectList &list, bool cache, Landscape *land, int x, int z)
{
  // only lists containing real objects should be considered used
  _land = land;
  _x = x;
  _z = z;
  _list = list.GetList();
  if (_list && cache)
  {
    Assert(list->CountPrimary()>0);
    land->AddRefObjectList(_list);

#if _ENABLE_PHYSICS1
		if(GetNetworkManager().IsPhysicsMaster() && _list->Size())
		{
			enf::PhysicsWorld* pw = _list->Get(0)->GetWorldPhysics();
			for(int i = 0; i < _list->Size(); ++i)
			{
				if(_list->Get(i)->GetPhysics())
					pw->AddStaticBody(list->Get(i));
			}
		}
#endif

    _used = true;
  }
  else
    _used = false;
}

ObjectListUsed::~ObjectListUsed()
{
  // note: it is possible data were added during lifetime of ObjectListUsed
  // therefore it is necessary to Release only if we AddRef'ed before.
  if (_list)
  {
    if (_used)
      _land->ReleaseObjectList(_x,_z,_list);
    else if (_land && _list->Size()==0 && _list->IsUsed()==0)
      // we know list can be deleted, as it is empty and not locked
      _land->DeleteObjectList(_x,_z);
  }
}

ObjectListUsed::ObjectListUsed(const ObjectListUsed &src)
:_list(src._list)
{
  if (_list && src._used)
  {
    Assert(!src._list->IsInList());
    src._list->AddRef();
  }
  _used = src._used;
  _land = src._land;
  _x = src._x;
  _z = src._z;
}

void ObjectListUsed::operator =(const ObjectListUsed &src)
{
  if (src._list && src._used)
  {
    // if it is used, it cannot be cached
    Assert(!src._list->IsInList());
    src._list->AddRef();
  }
  if (_list && _used)
    _land->ReleaseObjectList(_x,_z,_list);
  _list = src._list;
  _used = src._used;
  _land = src._land;
  _x = src._x;
  _z = src._z;
}

//////////////////////////////////////////////////////////////////////////
// 
// Map Object List implementation

DEFINE_FAST_ALLOCATOR(MapObjectListFull)

MapObjectListFull::~MapObjectListFull()
{
  // make sure list is no longer cached when it is being destroyed
  DoAssert(!IsUsed());
  DoAssert(!IsInList());
  Clear();
}

int MapObjectList::AddRef()
{
  if (_list)
  {
    Assert(_list->Size() > 0);
    return _list->AddRef();
  }
  return 0;
}

void MapObjectList::OnEmpty()
{
  if (!_list) return;
  Assert(Size() == 0);
  if (_list->_used > 0)
  {
    //LogF("List is empty, but used");
    // List is empty, but used
    // it will be relesed once it is stopped being used
    return;
  }
  _list.Free();
}

size_t MapObjectListFull::GetMemoryControlled() const
{ // count only memory used by the list and object headers, not its shapes
  size_t size = MaxSize()*sizeof(DataType)+sizeof(*this);
  Assert(Size()>0);
  // empty list should not be controlled
  for (int oi=0; oi<Size(); oi++)
  {
    MapObject *obj = Get(oi);
    size += obj->GetMemoryControlled();
  }
  return size;
}

/*!
\note constructor can be used only from Landscape::UseMapObjects
*/
MapObjectListUsed::MapObjectListUsed(const MapObjectList &list, bool cache, Landscape *land, int x, int z)
{
  _list = list.GetList();
  _land = land;
  _x = x;
  _z = z;
  if (_list && cache)
  {
    Assert (_list->Size() > 0);
    _list->AddRef();
    _used = true;
  }
  else
    _used = false;
}

MapObjectListUsed::~MapObjectListUsed()
{
  if (!_used)
    return;
  if (_list)
  {
    _land->ReleaseMapObjects(_x,_z);
    //Assert (_list->Size() == 0)
  }
}

MapObjectListUsed::MapObjectListUsed(const MapObjectListUsed &src)
:_list(src._list)
{
  if (_list)
  {
    Assert (_list->Size() > 0);
    _list->AddRef();
  }
  _used = src._used;
}

void MapObjectListUsed::operator =(const MapObjectListUsed &src)
{
  if (src._list && src._used)
    src._list->AddRef();
  if (_list && _used)
    _list->Release();
  _list = src._list;
  _used = src._used;
}

/*!
\note constructor can be used only from Landscape::UseMapObjects
*/
MapObjectListRectUsed::MapObjectListRectUsed(const Landscape *land, int xBeg,int zBeg,int xEnd,int zEnd, bool cache)
{
  _land = land;
  if (!cache)
  {
    xEnd = xBeg;
    zEnd = zBeg;
  }
  _rect = GridRectangle(xBeg,zBeg,xEnd,zEnd);
  // AddRef already done in Landscape::UseMapObjects
  Assert(CheckIntegrity());
}

MapObjectListRectUsed::MapObjectListRectUsed()
{
  _land = NULL;
  _rect = GridRectangle(0,0,0,0);
}

MapObjectListRectUsed::~MapObjectListRectUsed()
{
  Assert(CheckIntegrity());
  for (int x=_rect.xBeg; x<_rect.xEnd; x++)
  for (int z=_rect.zBeg; z<_rect.zEnd; z++)
  {
    _land->ReleaseMapObjects(x,z);
  }
  _rect = GridRectangle(0,0,0,0);
  _land = NULL;
}

bool MapObjectListRectUsed::CheckRefCount(int addCount) const
{
  int minCount = addCount+1;
  for (int x=_rect.xBeg; x<_rect.xEnd; x++)
  for (int z=_rect.zBeg; z<_rect.zEnd; z++)
  {
    MapObjectList &list = const_cast<MapObjectList &>(_land->GetMapObjects(x,z));
    MapObjectListFull *listData = list.GetList();
    if (listData)
    {
      if(listData->GetUseCount()<minCount)
      {
        RptF("%d,%d: ref count below %d",x,z,minCount);
        return false;
      }
    }
    else
    {
      if (!_land->MapObjectsEmpty(x,z))
      {
        RptF("%d,%d: list was not loaded",x,z);
        return false;
      }
    }
  }
  return true;
}


MapObjectListRectUsed::MapObjectListRectUsed(const MapObjectListRectUsed &src)
{
  // AddRef all new fields
  for (int x=src._rect.xBeg; x<src._rect.xEnd; x++)
  for (int z=src._rect.zBeg; z<src._rect.zEnd; z++)
  {
    const MapObjectList &list = src._land->GetMapObjects(x,z);
    MapObjectListFull *listData = list.GetList();
    if (listData)
      listData->AddRef();
  }
  _land = src._land;
  _rect = src._rect;
  Assert(CheckRefCount(1));
}
void MapObjectListRectUsed::operator =(const MapObjectListRectUsed &src)
{
  Assert(src.CheckIntegrity());
  Assert(CheckIntegrity());
  // note: this function is normally not often used
  // it could be implemented better, using rectangle intersections
  // so that memory can be freed sooner
  // AddRef all new fields
  for (int x=src._rect.xBeg; x<src._rect.xEnd; x++)
  for (int z=src._rect.zBeg; z<src._rect.zEnd; z++)
  {
    const MapObjectList &list = src._land->GetMapObjects(x,z);
    MapObjectListFull *listData = list.GetList();
    if (listData)
    {
      listData->AddRef();
      Assert(listData->GetUseCount()>=2);
    }
  }
  // Release all old fields
  for (int x=_rect.xBeg; x<_rect.xEnd; x++)
  for (int z=_rect.zBeg; z<_rect.zEnd; z++)
  {
    _land->ReleaseMapObjects(x,z);
  }
  _land = src._land;
  _rect = src._rect;
  Assert(CheckRefCount(1));
}

// cache currently unused lists that have a chance to be used soon again
// actually they are released from the landscape only after released from the cache
Object *Landscape::AddObject(float x, float y, float z, float head, const char *name)
{
  LODShape *shape=Shapes.New(name,false,true);
  return AddObject(Vector3(x,y,z),head,shape,NULL);
}

struct IsSuitableTypeAndIgnoreContext
{ 
  Object *ignore;
  bool defLimit;
  ObjectType type;
};

static bool IsSuitableTypeAndIgnore(Object *obj, float dist2, void *context)
{
  IsSuitableTypeAndIgnoreContext *ctx = (IsSuitableTypeAndIgnoreContext *)context;
  if (!obj || !(obj->GetType()&ctx->type))
    return false;
  // default behavior: consider only objects
  // that include pos in their bounding sphere
  if (ctx->defLimit && dist2 > Square(obj->GetRadius()))
    return false;
  if (obj == ctx->ignore)
    return false;
  return true;
}

OLink(Object) Landscape::NearestObject(Vector3Par pos, float maxDist, ObjectType type, Object *ignore)
{
  IsSuitableTypeAndIgnoreContext ctx;
  // limit==0 means no limit, search whole landscape
  ctx.defLimit = false;
  if (maxDist < 0.0f)
  {
    ctx.defLimit = true;
    maxDist = 200.0f;
  }
  if (maxDist == 0.0f)
    maxDist = 1e10f;
  ctx.ignore = ignore;
  ctx.type = type;
  return NearestObject(pos,maxDist,IsSuitableTypeAndIgnore,&ctx);
}

OLink(Object) Landscape::NearestObjectAsync(bool &ready, bool async, Vector3Par pos, float maxDist, ObjectType type, Object *ignore)
{
  IsSuitableTypeAndIgnoreContext ctx;
  // limit==0 means no limit, search whole landscape
  ctx.defLimit = false;
  if (maxDist < 0.0f)
  {
    ctx.defLimit = true;
    maxDist = 200.0f;
  }
  if (maxDist == 0.0f)
    maxDist = 1e10f;
  ctx.ignore = ignore;
  ctx.type = type;
  return NearestObjectAsync(ready,async,pos,maxDist,IsSuitableTypeAndIgnore,&ctx);
}

OLink(Object) Landscape::NearestObject(Vector3Par pos, float maxDist, bool (*isSuitable)(Object *obj, float dist2, void *context), void *context, bool dist2D)
{
  bool dummy;
  return NearestObjectAsync(dummy,false,pos,maxDist,isSuitable,context,dist2D);
}

OLink(Object) Landscape::NearestObjectAsync(bool &ready, bool async, Vector3Par pos, float maxDist, bool (*isSuitable)(Object *obj,
  float dist2, void *context), void *context, bool dist2D)
{
  ready = true;
  // limit==0 means no limit, search whole landscape
  int x = toIntFloor(pos.X() * GetInvLandGrid());
  int z = toIntFloor(pos.Z() * GetInvLandGrid());

  if (maxDist == 0.0f)
    maxDist = 1e10f;
  Object *ret = NULL;
  float minDist2 = maxDist * maxDist;

  // increase searched area step-by-step
  float maxRangeF = maxDist*GetInvLandGrid()+1;
  saturateMin(maxRangeF,GetLandRange()+1);
  int maxRange = toIntCeil(maxRangeF);
  // until given range is preloaded, there is no need to continue searching
  for (int range = 0; range <= maxRange && ready; range++)
  {
    if (ret)
    {
      // we have some candidate
      // check if given range may find better solution
      // minX of max.x (right) border
      float distXRight = (x+range)*GetLandGrid()-pos.X();
      float distXLeft = pos.X()-(x+1)*GetLandGrid();
      float distZBottom = (z+range)*GetLandGrid()-pos.Z();
      float distZTop = pos.Z()-(z+1)*GetLandGrid();
      float minRangeDist = distXRight;
      saturateMin(minRangeDist,distXLeft);
      saturateMin(minRangeDist,distZBottom);
      saturateMin(minRangeDist,distZTop);
      // if our recent candidate is better than the closest ranged point, we may finish
      if (minDist2  <Square(minRangeDist))
        return ret;
    }
    for (int zz=z-range; zz<=z+range; zz++)
    {
      int xStep = (zz==z-range || zz==z+range) ? 1 : range * 2;
      for (int xx=x-range; xx<=x+range; xx+=xStep)
      {
        if (!land_InRange(zz,xx))
          continue;
        if (async)
        {
          PreloadResult result = PreloadObjects(FileRequestPriority(10),xx,zz);
          if (result==PreloadRequested)
          {
            ready = false;
            continue;
          }
        }
        const ObjectListUsed &list=UseObjects(xx,zz,async ? UONoWait : UOWait);
        int n=list.Size();
        for (int i=0; i<n; i++)
        {
          Object *obj=list[i];
          if (!obj) continue;
          float dist2 = dist2D ? obj->FutureVisualState().Position().DistanceXZ2(pos) : obj->FutureVisualState().Position().Distance2Inline(pos);
          if( minDist2<dist2 ) continue;
          if (!isSuitable(obj,dist2,context)) continue;
          // we found a better candidate, use it
          minDist2=dist2;
          ret=obj;
        }
      }
    }
    //if (range>=rangeFound+1)
  }
  return ret;
}

void ReleaseVBuffers()
{
  if (GEngine)
    GEngine->ReleaseAllVertexBuffers();
}


void Landscape::SetSound(int x, int z, EnvTypeInfo value)
{
  if (land_InRange(x,z))
    _soundMap.Set(x, z, value.packed);
}

EnvTypeInfo Landscape::GetSound(int x, int z) const
{
  if (land_InRange(x, z))
    return EnvTypeInfo(_soundMap(x,z));
  else
  {
    // check if the "island" is in the water or in the desert
    saturate(x, 0, GetTerrainRangeMask());
    saturate(z, 0, GetTerrainRangeMask());
    EnvTypeInfo info(_soundMap(x,z));
    info.u.houses = 0;
    info.u.trees = 0;
    return info;
  }
}

float Landscape::GetEnvTypeDensity(Vector3Par pos, EnvType type) const
{
  float xGrid=pos.X()*GetInvLandGrid()-0.5f;
  float zGrid=pos.Z()*GetInvLandGrid()-0.5f;
  int x=toIntFloor(xGrid);
  int z=toIntFloor(zGrid);
  float xFrac=xGrid-x;
  float zFrac=zGrid-z;
  
  EnvTypeInfo s00=GetSound(x,z);     // 00
  EnvTypeInfo s10=GetSound(x+1,z);   // 10
  EnvTypeInfo s01=GetSound(x,z+1);   // 01
  EnvTypeInfo s11=GetSound(x+1,z+1); // 11
  
  float v00=((1-xFrac)*(1-zFrac)); // 00
  float v10=((  xFrac)*(1-zFrac)); // 10
  float v01=((1-xFrac)*(  zFrac)); // 01
  float v11=((  xFrac)*(  zFrac)); // 11

  return (
    v00 * s00.GetEnvType(type)+
    v10 * s10.GetEnvType(type)+
    v01 * s01.GetEnvType(type)+
    v11 * s11.GetEnvType(type)
  );
}

Landscape::Landscape(Engine *engine, World *world, bool nets)
:_nets(nets),
_randGen(8799656,148756),
_soundMap(32, 32, 0),
_objects(32, 32, ObjectList()),
_mapObjects(32, 32, MapObjectList()),
_lzoCompression(false)
#if _ARMA3_SIMUL_WEATHER
,_enableSimulWeather(!IsDedicatedServer())
#endif
#if _VBS3_NO_SEABED
,_seaLevelOffset(0)
#endif
#if _ARMA3_HDR_IMPROVEMENTS
, _desiredLuminanceCoefOverride(-1.0f)
, _luminanceRectCoefOverride(-1.0f)
#endif
#if _ARMA3_HAZE_COMPUTATION
,_hazeFogCoef(-1.0f)
,_hazeDistCoef(-1.0f)
#endif
,_pPhysWorld(NULL)
{
  _terrain = new Scenery(GScene->ForceSphere(), VISITOR_NO_ID);
  Dim(32,32,32,32,50);
  DoConstruct(engine,world);
  Init(Pars >> "CfgWorlds" >> "DefaultWorld");
  InitGrassMapEmpty();
  InitPrimaryTexture(false);
  _cloudsCache = new CloudsCache;
  //_layerMaskCache = new LayerMaskCache;
  // make sure by default the terrain allows for some sea

  InitObjectIndex();
}

void Landscape::InitSkyObjects(ParamEntryPar cls)
{
  // empty texture list
  ClipFlags clipSky=ClipAll&~ClipBack|ClipUser0;
  {
    // load sky background shape
    RString name = cls >> "skyObject";
    Ref<LODShape> lShape=Shapes.New(name,false,false);
    lShape->OrSpecial(FilterLinear);
    //Ref<LODShape> lShape = GScene->Preloaded(SkySphere);
    lShape->DisableStreaming();
    if( (lShape->Special()&NoZWrite)==0 && lShape->NLevels()>0)
    { // this should be done only once
      Shape *shape=lShape->InitLevelLocked(0);
      shape->SetClipAll(clipSky|ClipFogDisable);
      for (int s=0; s<shape->NSections(); s++)
      {
        ShapeSection &ss = shape->GetSection(s);
        ss.OrSpecial(BestMipmap);
        //SetMipmapLevel(0,0);
      }
      vecAlign Matrix4 scale(MScale,100);
      lShape->InternalTransform(scale);
      lShape->SetSpecial(NoZWrite|ClampV|NoShadow|FilterLinear);
      shape->CalculateHints();
      lShape->CalculateHints();
      //lShape->SetAnimationType(AnimTypeSoftware);
    }
    _skyObject = new ObjectPlain(lShape,VISITOR_NO_ID);
    // dynamic material - no pushbuffer
    lShape->DisableStreaming();
    if (lShape->NLevels()>0)
      lShape->Level(0)->DisablePushBuffer();
    _skyMaterial = new TexMaterial;
    _skyMaterial->SetPixelShaderID(PSInterpolation);
    _skyMaterial->SetMainLight(ML_Sky);
    _skyMaterial->SetFogMode(FM_None);

    _horizonMaterial = new TexMaterial;
    _horizonMaterial->SetPixelShaderID(PSHorizon);
    _horizonMaterial->SetMainLight(ML_Horizon);
    _horizonMaterial->SetFogMode(FM_None);
  }
  ProgressRefresh();

  {
    // load star background shape
    RString name = cls >> "starsObject";
    Ref<LODShape> lShape=Shapes.New(name,false,false);
    if( (lShape->Special()&IsAlphaFog)==0 )
    { // this should be done only once
      lShape->DisableStreaming();
      if (lShape->NLevels()>0)
      {
        // TODO: check if modification of shape is legal
        ShapeUsed lock = lShape->Level(0);
        Assert(lock.NotNull());
        Shape *shape = lock.InitShape();
        shape->ExpandClip();
        for (int i=0; i<shape->NPos(); i++ )
        {
          ClipFlags hints=shape->Clip(i)&ClipUserMask;
          shape->SetClip(i,clipSky|ClipFogDisable|hints);
        }
        shape->CondenseClip();
        lShape->InternalTransform(Matrix4(MScale,1000));
        shape->CalculateHints();
        lShape->CalculateHints();
        lShape->SetAutoCenter(false);
        lShape->CalculateBoundingSphere();
        lShape->SetAnimationType(AnimTypeNone);

        // Add an empty section
        ShapeSection section;
        section.Init();
        section.beg = Offset(0);
        section.end = Offset(0);
        section.SetAreaOverTex(0, 1.0f);
        //section.SetSpecial(NoZBuf|IsAlphaFog|IsAlpha|NoClamp);
        shape->AddSection(section);

        // Set special flags
        lShape->SetSpecial(NoZWrite|IsAlphaFog|IsAlpha|NoClamp|FilterLinear);

        // Set material
        Ref<TexMaterial> material;
        material = new TexMaterial;
        material->SetVertexShaderID(VSPoint);
        material->SetPixelShaderID(PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        material->SetMainLight(ML_None);
        material->SetFogMode(FM_None);
        material->SetRenderFlag(RFAddBlend, true);
        material->SetStageTexture(0, GlobLoadTexture(RString("#(argb,64,64,5)point()")));
        material->_emmisive = Color(0.3, 0.3, 0.3, 1);
        material->_ambient = HBlack;
        shape->SetUniformMaterial(material);
        shape->DisablePushBuffer();
        shape->VertexDeclarationNeedsUpdate();
      }
    }
    _starsObject=new ObjectPlain(lShape,VISITOR_NO_ID);
  }

  ProgressRefresh();

  // Load the point object (used by VASI f.i.)
  {
    RString name = cls >> "pointObject";
    Ref<LODShape> lShape=Shapes.New(name,false,false);
    if ((lShape->Special()&IsAlphaFog)==0)
    { // this should be done only once
      lShape->DisableStreaming();
      if (lShape->NLevels()>0)
      {
        // TODO: check if modification of shape is legal
        ShapeUsed lock = lShape->Level(0);
        Assert(lock.NotNull());
        Shape *shape = lock.InitShape();

        // no sense to read user mask from this model, we always want 1.0 intensity
        shape->SetClipAll(clipSky|ClipFogDisable|ClipUserMask);
        lShape->InternalTransform(Matrix4(MScale,1000));
        shape->CalculateHints();
        lShape->CalculateHints();
        lShape->SetAutoCenter(false);
        lShape->CalculateBoundingSphere();
        lShape->SetAnimationType(AnimTypeNone);

        // Add an empty section
        ShapeSection section;
        section.Init();
        section.beg = Offset(0);
        section.end = Offset(0);
        section.SetAreaOverTex(0, 1.0f);
        //section.SetSpecial(NoZBuf|IsAlphaFog|IsAlpha|NoClamp);
        shape->AddSection(section);

        // Set special flags
        lShape->SetSpecial(/*NoZBuf|*/IsAlphaFog|IsAlpha|NoClamp|FilterLinear|NoZWrite);

        // Set material
        Ref<TexMaterial> material;
        material = new TexMaterial;
        material->SetVertexShaderID(VSPoint);
        material->SetPixelShaderID(PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        material->SetMainLight(ML_None);
        material->SetFogMode(FM_Alpha);
        material->SetRenderFlag(RFAddBlend, true);
        material->SetStageTexture(0, GlobLoadTexture(RString("#(argb,64,64,5)point()")));
        material->_emmisive = Color(2.0f, 2.0f, 2.0f, 1);
        material->_ambient = HBlack;
        shape->SetUniformMaterial(material);
        shape->DisablePushBuffer();
        shape->VertexDeclarationNeedsUpdate();
        shape->OrSpecial(ZBiasStep*3);
      }
    }
    _pointObject=new ObjectPlain(lShape,VISITOR_NO_ID);
  }

  ProgressRefresh();

  {
    // load blended horizon shape
    ClipFlags clipHor=ClipAll&~ClipBack/*|ClipUser0*/;
    //Ref<LODShape> lShape = GScene->Preloaded(HorizontObject);
    RString name = cls >> "horizontObject";
    Ref<LODShape> lShape=Shapes.New(name,false,false);
    if( (lShape->Special()&NoZWrite)==0 && lShape->NLevels()>0)
    { // this should be done only once
      lShape->DisableStreaming();
      Shape *shape=lShape->InitLevelLocked(0);
      shape->SetClipAll(clipSky|ClipFogDisable);
      for (int s=0; s<shape->NSections(); s++)
      {
        ShapeSection &ss = shape->GetSection(s);
        ss.OrSpecial(BestMipmap);
        //SetMipmapLevel(0,0);
      }
      vecAlign Matrix4 scale(MScale,60);
      lShape->InternalTransform(scale);
      lShape->SetSpecial(ClampV|NoShadow|NoZWrite|IsAlpha|FilterLinear);

      const ClipFlags ambFlags = (ClipUserStep*MSInShadow);
      shape->SetClipAll(ambFlags|clipHor);

      shape->SetUniformMaterial(_horizonMaterial);
      // this pixel shader is not compatible with pushbuffers (dynamic PS constant)
      shape->DisablePushBuffer();
      shape->CalculateHints();
      lShape->CalculateHints();
      lShape->SetAnimationType(AnimTypeNone);
      // Make the object to get the air temperature ASAP (in 1 sec)
      lShape->SetTIProperties(1, 1, 0, 0, 0, 0);
    }
    _horizontObject=new ObjectPlain(lShape,VISITOR_NO_ID);
  }
  ProgressRefresh();

  // load sun and moon shape
  const int sunSpec = IsAlpha|NoZWrite|ClampV|ClampU|NoShadow|IsAlphaFog|BestMipmap|FilterLinear;
  RString name = cls >> "haloObject";
  Ref<LODShape> haloShape=Shapes.New(name,false,false);
  haloShape->DisableStreaming();
  if (haloShape->NLevels()>0)
  {
    // TODO: check if modification of shape is legal
    ShapeUsed lock = haloShape->Level(0);
    Assert(lock.NotNull());
    Shape *haloShape0 = lock.InitShape();
    {
      // load sun shape
      RString name = cls >> "sunObject";
      Ref<LODShape> sunShape=Shapes.New(name,false,false);
      haloShape0->SetClipAll(ClipFogDisable|clipSky);
      Ref<LODShape> sunWithHalo=new LODShape(*haloShape);
      sunShape->DisableStreaming();
      // dynamic material - no pushbuffer
      if (sunShape->NLevels()>0)
      {
				sunShape->InternalTransform(Matrix4(MScale,origSunRescale));

        // TODO: check if modification of shape is legal
        ShapeUsed sunHaloLock = sunWithHalo->Level(0);
        Assert(sunHaloLock.NotNull());
        Shape *sunHaloShape0 = sunHaloLock.InitShape();

        // Set material to sun halo
        Ref<TexMaterial> sunHaloObjectMaterial;
        sunHaloObjectMaterial = new TexMaterial;
        sunHaloObjectMaterial->SetPixelShaderID( PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        sunHaloObjectMaterial->SetMainLight(ML_SunHaloObject);
        sunHaloObjectMaterial->SetFogMode(FM_None);
        sunHaloShape0->SetUniformMaterial(sunHaloObjectMaterial);
        sunHaloShape0->VertexDeclarationNeedsUpdate();
        sunHaloShape0->DisablePushBuffer();

        // Get the sun shape
        // TODO: check if modification of shape is legal
        ShapeUsed sunLock = sunShape->Level(0);
        Assert(sunLock.NotNull());
        Shape *sunShape0 = sunLock.InitShape();

        sunShape0->SetClipAll(ClipFogDisable|clipSky);

        // Set material to sun
        Ref<TexMaterial> sunObjectMaterial;
        sunObjectMaterial = new TexMaterial;
        sunObjectMaterial->SetPixelShaderID( PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        sunObjectMaterial->SetMainLight(ML_SunObject);
        sunObjectMaterial->SetFogMode(FM_None);
        sunShape0->SetUniformMaterial(sunObjectMaterial);
        sunShape0->VertexDeclarationNeedsUpdate();
        sunShape0->DisablePushBuffer();

        // Merge sun with halo
        sunHaloShape0->Merge(sunShape0,MIdentity);
        sunHaloShape0->CalculateHints();
      }
      sunWithHalo->CalculateHints();
      sunWithHalo->SetSpecial(sunSpec);
      sunWithHalo->SetAnimationType(AnimTypeNone);
      _sunObject = new ObjectPlain(sunWithHalo,VISITOR_NO_ID);

      // no color buffer write for the testing object
      sunShape->SetSpecial(sunSpec);
      sunShape->SetAnimationType(AnimTypeNone);
      sunShape->OrSpecial(NoColorWrite|NoAlphaWrite);
      sunShape->AndSpecial(~IsAlpha);
      sunShape->OrSpecial(IsTransparent);
      _sunVisTestObject = new ObjectPlain(sunShape,VISITOR_NO_ID);
    }
    {
      // load moon shape
      RString name = cls >> "moonObject";
      Ref<LODShape> lShape=Shapes.New(name,false,false);
      haloShape0->SetClipAll(ClipFogDisable|clipSky);
      Ref<LODShape> moonWithHalo=new LODShape(*haloShape);
      lShape->DisableStreaming();
      if (lShape->NLevels()>0)
      {
        // TODO: check if modification of shape is legal
        ShapeUsed moonHaloLock = moonWithHalo->Level(0);
        Assert(moonHaloLock.NotNull());
        Shape *moonHaloShape0 = moonHaloLock.InitShape();

        // Set material to moon halo
        Ref<TexMaterial> moonHaloObjectMaterial;
        moonHaloObjectMaterial = new TexMaterial;
        moonHaloObjectMaterial->SetPixelShaderID( PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        moonHaloObjectMaterial->SetMainLight(ML_MoonHaloObject);
        moonHaloObjectMaterial->SetFogMode(FM_None);
        moonHaloShape0->SetUniformMaterial(moonHaloObjectMaterial);
        moonHaloShape0->VertexDeclarationNeedsUpdate();
        moonHaloShape0->DisablePushBuffer();

        // Get the moon shape
        // TODO: check if modification of shape is legal
        ShapeUsed moonLock = lShape->Level(0);
        Assert(moonLock.NotNull());
        Shape *moonShape0 = moonLock.InitShape();
        if( (lShape->Special()&IsAlphaFog)==0 )
        {
          lShape->OrSpecial(IsAlphaFog);
          moonShape0->SetClipAll(ClipFogDisable|clipSky);
					lShape->InternalTransform(Matrix4(MScale,origMoonRescale));		//why here was 0.25? the moon was too small
        }

        // Set material to moon
        Ref<TexMaterial> moonObjectMaterial;
        moonObjectMaterial = new TexMaterial;
        moonObjectMaterial->SetPixelShaderID( PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        moonObjectMaterial->SetMainLight(ML_MoonObject);
        moonObjectMaterial->SetFogMode(FM_None);
        moonShape0->SetUniformMaterial(moonObjectMaterial);
        moonShape0->VertexDeclarationNeedsUpdate();
        moonShape0->DisablePushBuffer();

        // Merge moon with halo
        moonHaloShape0->Merge(moonShape0,MIdentity);
        moonHaloShape0->CalculateHints();
      }
      // halo behind the moon
      moonWithHalo->CalculateHints();
      //moonWithHalo->SetSpecial(sunSpec|IsFlare);
      moonWithHalo->SetSpecial(sunSpec|DstBlendOne);
      moonWithHalo->SetAnimationType(AnimTypeNone);
      _moonObject=new ObjectPlain(moonWithHalo,VISITOR_NO_ID);
    }
    {
      RString name = cls>>"rainbowObject";
      Ref<LODShape> lShape=Shapes.New(name,false,false);
      if (lShape && lShape->NLevels()>0)
      {
        // TODO: check if modification of shape is legal
        ShapeUsed lock = lShape->Level(0);
        Assert(lock.NotNull());
        Shape *level0 = lock.InitShape();

        lShape->DisableStreaming();
        // dynamic material - no pushbuffer
        level0->DisablePushBuffer();
        lShape->OrSpecial(
          IsAlpha|NoZWrite|ClampV|ClampU|
          NoShadow|IsAlphaFog|BestMipmap|DstBlendOne
          );
        Ref<TexMaterial> rainbowMaterial = new TexMaterial;
        rainbowMaterial->SetPixelShaderID( PSCloud); // This looks quite obscure, but PSCloud is just an ordinary shader (without any shadows or hemispherical lighting) so far
        rainbowMaterial->SetMainLight(ML_SunObject);
        rainbowMaterial->SetFogMode(FM_None);
        level0->SetUniformMaterial(rainbowMaterial);
        level0->VertexDeclarationNeedsUpdate();
      }
      _rainbowObject = new ObjectPlain(lShape,VISITOR_NO_ID);
    }
  }
}

#if _ENABLE_CONVERSATION
static void LoadSpeech(AutoArray<RString> &result, ParamEntryVal entry)
{
  Assert(entry.IsArray());
  int n = entry.GetSize();
  result.Realloc(n);
  result.Resize(n);
  for (int i=0; i<n; i++)
    result[i] = entry[i];
}
#endif

Ref<TexMaterial> Landscape::LoadOutsideMaterial(ParamEntryPar cls, TexMaterial *basedOn, bool &terSynth)
{
  ConstParamEntryPtr outside = cls.FindEntry("OutsideTerrain");
  if (!outside)
  {
    terSynth = false;
    return NULL;
  }
#if 1
  terSynth = (*outside)>>"enableTerrainSynth";
  RString satellite = (*outside)>>"satellite";
  satellite.Lower();
  // satellite is information for binarize, it need to be is source file format 
  const char *ext = GetFileExt(GetFilenameExt(satellite));
  if (strcmp(ext,".tga") && strcmp(ext,".png")) 
    LogF("OutsideTerrain>>satellite need to have TGA|PNG extension in config");

  // get satellite normal map name
  RString satelliteNormal;
  if (outside->FindEntry("satelliteNormalMap"))
  {
    satelliteNormal = (*outside)>>"satelliteNormalMap";  
  }
  bool useSatNormalMap = !satelliteNormal.IsEmpty();

  // read
  // OutsideTerrain
  // 1st version: read satellite + first layer from OutsideTerrain
  Ref<TexMaterial> mat = new TexMaterial;
  mat->Init();
  mat->SetPixelShaderID(PSTerrainX);
  mat->SetVertexShaderID(VSTerrain);
  mat->SetFogMode(FM_Fog);
  mat->SetRenderFlag(RFLandShadow,true);
  // override middle detail textures - not specified in the material
  mat->_stageCount = 5-1;
  mat->_stage[LandMatStageSat]._tex = GlobLoadTexture(GlobResolveTexture(satellite));
  mat->_stage[LandMatStageSat]._filter = TFAnizotropic;
  mat->_stage[LandMatStageSat]._texGen = 3;
  mat->_stage[LandMatStageMask]._tex = GlobLoadTexture("#(argb,1,1,1)color(0,0,0,1)");
  mat->_stage[LandMatStageMask]._filter = TFPoint; // we do not care
  mat->_stage[LandMatStageMask]._texGen = 4;
  Texture *midDetail = _midDetailTexture;
  mat->_stage[LandMatStageMidDet]._tex = midDetail ? midDetail : GPreloadedTextures.New(TextureHalf);
  mat->_stage[LandMatStageMidDet]._filter = TFAnizotropic;
  mat->_stage[LandMatStageMidDet]._texGen = 0;

  if (useSatNormalMap)
  {
    // setup satellite normal map
    mat->_stage[LandMatStageSatNorm]._tex = GlobLoadTexture(GlobResolveTexture(satelliteNormal));
    if (!mat->_stage[LandMatStageSatNorm]._tex.IsNull())
    {
      // setup stage
      mat->_stage[LandMatStageSatNorm]._filter = TFLinear;
      mat->_stage[LandMatStageSatNorm]._texGen = 3;

      // clear all stages that we do not need
      for (int i=mat->_stageCount+1; i<LandMatStageSatNorm; ++i)
      {
        mat->_stage[i]._tex = NULL;
        mat->_stage[i]._filter = TFAnizotropic;
        mat->_stage[i]._texGen = i;
      }

      // update stage count
      mat->_stageCount = LandMatStageSatNorm;
      // set new pixel shader
      mat->SetPixelShaderID( PSTerrainSNX);
    }
  }
  
  basedOn->Load();
  mat->_ambient = basedOn->_ambient;
  mat->_diffuse = basedOn->_diffuse;
  mat->_forcedDiffuse = basedOn->_forcedDiffuse;
  mat->_specular = basedOn->_specular;
  mat->_specularPower = basedOn->_specularPower;
  
  ParamEntryVal layer0 = (*outside)>>"Layers">>"Layer0"; // TODO: enumerate Layers
  RString noName = layer0>>"nopx";
  RString dtName = layer0>>"texture";
  noName.Lower();
  dtName.Lower();
  mat->_stage[LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage]._tex = GlobLoadTexture(GlobResolveTexture(noName));
  mat->_stage[LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage]._filter = TFAnizotropic;
  mat->_stage[LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage]._texGen = 1;
  mat->_stage[LandMatStageLayers+LandMatStagePerLayer*0+LandMatDtInStage]._tex = GlobLoadTexture(GlobResolveTexture(dtName));
  mat->_stage[LandMatStageLayers+LandMatStagePerLayer*0+LandMatDtInStage]._filter = TFAnizotropic;
  mat->_stage[LandMatStageLayers+LandMatStagePerLayer*0+LandMatDtInStage]._texGen = 2;
  
  mat->_texGen[0]._uvSource = UVTex;
  mat->_texGen[0]._uvTransform = basedOn->_texGen[0]._uvTransform;
  mat->_texGen[1]._uvSource = UVTex;
  mat->_texGen[1]._uvTransform = basedOn->_texGen[1]._uvTransform;
  mat->_texGen[2] = mat->_texGen[1];
  mat->_texGen[3]._uvSource = UVWorldPos;
  mat->_texGen[3]._uvTransform = basedOn->_texGen[3]._uvTransform;
  mat->_texGen[4] = mat->_texGen[3];
  mat->_nTexGen = 5;
  mat->SetCustomInt(1);
  
  // set usage for all textures
  for (int stage=0; stage<mat->_stageCount+1; stage++)
  {
    Texture *tex = mat->_stage[stage]._tex;
    if (tex) tex->SetUsageType(Texture::TexLandscape);
  }
  return mat;
#else
  return NULL;
#endif
}

#if _VBS3_NO_SEABED
void Landscape::InitSeabed()
{
  // create seabed shape - it is an N*N square mesh
  const int N = 40;
  Shape* shape = new Shape;
  _seabedShape = shape;

  Vector3 originalS(1, 0, 0);
  Vector3 originalT(0, 0, 1);

  // create vertices
  shape->Clear();
  shape->Reserve(Square(N+1));

  UVPair minUV(0, 0), maxUV(1, 1), invUV;
  shape->InitUV(invUV, minUV, maxUV);

  for (int z = 0; z <= N; z++)
  {
    float zz = (float) z / N * 2 - 1;
    for (int x = 0; x <= N; x++)
    {
      float xx = (float) x / N * 2 - 1;
      float yy = -sqrt(xx*xx + zz*zz) / 100; // prevent Z-fighting with the sea and conflict with earth curvature
      shape->AddVertex(Vector3(xx, yy, zz), VUp, ClipAll, invUV, 0, 0,NULL,STPair(originalS,originalT));
    }
  }

  // create faces
  Poly face;
  face.Init();
  face.SetN(3);
  for (int z = 0; z < N; z++)
  {
    int row = z*(N+1);
    for (int x = 0; x < N; x++)
    {
      int v00 = row+x;
      int v01 = row+x+1;
      int v10 = row+x+N+1;
      int v11 = row+x+N+2;

      face.Set(0, v11);
      face.Set(1, v01);
      face.Set(2, v00);
      shape->AddFace(face);

      face.Set(0, v00);
      face.Set(1, v10);
      face.Set(2, v11);
      shape->AddFace(face);      
    }
  }

  // create material
  Ref<TexMaterial> material = new TexMaterial;
  material->SetPixelShaderID( PSNormal);
  material->_ambient = Color(0,0.03,0.1);
  material->_diffuse = Color(0,0,0);

  ShapeSectionInfo prop;
  prop.Init();
  prop.SetMaterialExt(material);

  shape->SetAsOneSection(prop);
  shape->DisablePushBuffer();
//  shape->SetCanBeInstanced(false);

  shape->ConvertToVBuffer(VBStaticReused);
  if (shape->GetVertexBuffer())
    shape->UnloadGeometry();
}
#endif

void Landscape::Init(ParamEntryPar cls)
{
  //Assert( GTexMaterialBank.CheckIntegrity() );
    
  ProgressRefresh();

  // clear any virtual targets to make sure object are not linked by them any more
  for (int i=0; i<_buildings.Size(); i++)
    _buildings.GetInfo(i).ClearVirtualTarget();
  // it seems more logical to free caches before we free _objIndex
  // _objIndex provides low-level functionality for SoftLinks
  FreeCaches(); /*********/
  _roadNet.Free(); // no road yet
#if _HELISIM_AMBIENT_TRAFFIC
  _ambientRoadNetwork.Free();
#endif
  _objIndex.Free();;
  _mapObjIndex.Free();;
  _yRange.Clear();
  _persistentLoaded = false;
  //FreeCaches(); /*********/
  // if there are any unloaded grids, unload them
  for (int x=0; x<GetLandRange(); x++) for (int z=0; z<GetLandRange(); z++)
  {
    ObjectListFull *list = _objects(x,z).GetList();
    if (list)
    {
      //Assert(list->_used<=1);
      list->_used=0;
      int xx,zz;
      list->DoRelease(xx,zz);
    }
  }
  ProgressRefresh();

  LoadWeather(cls);
  
  ProgressRefresh();

#if _ARMA3_SIMUL_WEATHER
  if (_enableSimulWeather)
    LoadSimulWeather(cls);

  ProgressRefresh();
#endif

#if _ARMA3_HAZE_COMPUTATION
  _hazeDistCoef = cls >> "hazeDistCoef";
  _hazeFogCoef = cls >> "hazeFogCoef";
#endif

  // load sea variables by default cfgWorlds
  RString midDetailTextureName = cls>>"midDetailTexture";
  RString seaTextureName = cls>>"seaTexture";
  RString rainTextureName = cls>>"Rain">>"texture";
  RString seaMaterialName = cls>>"seaMaterial";
  RString shoreMaterialName = cls>>"shoreMaterial";
  RString shoreFoamMaterialName = cls>>"shoreFoamMaterial";
  RString shoreWetMaterialName = cls>>"shoreWetMaterial";
  RString terrainMaterialName = cls>>"terrainMaterial";
  midDetailTextureName.Lower();
  seaTextureName.Lower();
  rainTextureName.Lower();
  seaMaterialName.Lower();
  shoreMaterialName.Lower();
  shoreFoamMaterialName.Lower();
  shoreWetMaterialName.Lower();
  terrainMaterialName.Lower();
  
  _seaMaterial = GTexMaterialBank.New(TexMaterialName(seaMaterialName));
    
  _seaTexture = GlobLoadTexture(seaTextureName);
  _midDetailTexture = GlobLoadTexture(midDetailTextureName);

  _shoreMaterial = GTexMaterialBank.New(TexMaterialName(shoreMaterialName));
  _shoreFoamMaterial = GTexMaterialBank.New(TexMaterialName(shoreFoamMaterialName));
  _shoreWetMaterial = GTexMaterialBank.New(TexMaterialName(shoreWetMaterialName));

  // we cannot load "OutsideTerrain" yet, as we need to have rvmats loaded for it already
  _terrain->_outsideTextureInfo._mat = NULL;
  /// satellite map
  /*
  RString satelliteMapName = cls>>"satelliteMap";
  RString layerMaskName = cls>>"layerMask";
  if  (satelliteMapName.GetLength()>0)
  {
    _satelliteMap = GlobLoadTexture(satelliteMapName);
  }
  else
  {
    _satelliteMap.Free();
  }
  if  (layerMaskName.GetLength()>0)
  {
    _layerMask = GlobLoadTexture(layerMaskName);
  }
  else
  {
    _layerMask.Free();
  }
  */

  _rainTexture = GlobLoadTexture(rainTextureName);
  if (!_grassNoiseTexture)
  {
  ITextureSource *perlin = CreatePerlinNoise(256,256,8,256,256,0,1);
  _grassNoiseTexture = GlobLoadTexture(perlin);
  }

  ProgressRefresh();

  // caches need to be invalidated
  // most of them were invalidated in FreeCaches above
  if (_cloudsCache) _cloudsCache->Clear();
  
  for (int i=0; i<NClouds; i++)
  {
    RStringB name = (cls >> "clouds")[i];
    Ref<LODShape> lodShape = Shapes.New(name, false, false);
    lodShape->OrSpecial(NoZWrite|IsAlpha|IsAlphaFog);
    if (lodShape->NLevels()>0)
    {
      // TODO: check if modification of shape is legal
      ShapeUsed lock = lodShape->Level(0);
      Assert(lock.NotNull());
      Shape *shape = lock.InitShape();

      shape->SetClipAll(ClipFogSky|(ClipAll&~ClipBack));

      // Set material to clouds
      Ref<TexMaterial> material;
      material = new TexMaterial;
      material->SetPixelShaderID( PSCloud);
      material->_diffuse = HBlack;
      material->SetFogMode(FM_Alpha);
      shape->SetUniformMaterial(material);
      shape->VertexDeclarationNeedsUpdate();
      // dynamic material - no pushbuffer
      shape->DisablePushBuffer();

      shape->CalculateHints();
      lodShape->CalculateHints();
      lodShape->SetAnimationType(AnimTypeNone);
    }
    _cloudObj[i] = new ObjectPlain(lodShape, VISITOR_NO_ID);
  }
  ProgressRefresh();

  // clutters
  _clutterGrid = cls >> "clutterGrid";
  _clutterDist = cls >> "clutterDist";

  _noDetailDist = cls >> "noDetailDist";
  _fullDetailDist = cls >> "fullDetailDist";

  _satelliteNormalBlendStart = cls >> "satelliteNormalBlendStart";
  _satelliteNormalBlendEnd = cls >> "satelliteNormalBlendEnd";

  InitClutter(cls);

  ProgressRefresh();

  InitSkyObjects(cls);
#if _VBS3_NO_SEABED
  InitSeabed();
#endif

  ProgressRefresh();

  ProgressRefresh();

  _terrain->_texture.Clear();
  SetTexture(0, "", 0);

  // empty landscape
  for (int x=0; x<GetLandRange(); x++ ) for (int z=0; z<GetLandRange(); z++)
    SetTex(x,z,0);
  for (int x=0; x<GetTerrainRange(); x++ ) for (int z=0; z<GetTerrainRange(); z++)
    _terrain->SetData(x,z,0);
  
  InitWaterAndGrassMap(true);
  InitPrimaryTexture(false);
  
  _terrain->_outsideData.Free();
  
  // empty object list
  _buildingsOffTree.Clear();
  _buildings.Clear();
  _features.Clear();
  _objects.Init(ObjectList());
  _mapObjects.Init(MapObjectList());

  ParamEntryVal entry = Pars >> "CfgSurfaces" >> "Water";
  _waterSurface._name = entry >> "files";
  _waterSurface._roughness = entry >> "rough";
  _waterSurface._dustness = entry >> "dust";
  _waterSurface._soundEnv = entry >> "soundEnviron";
  _subdivHintsOffset = -1;

#if _ARMA3_ENV_MAPS
  RString envTextureName;
  ParamEntryPtr envTexturePar = cls.FindEntry("envTexture");
  if (envTexturePar.NotNull())
    envTextureName = *envTexturePar;

  if (envTextureName.IsEmpty())
  {
    _envTexture.Free();
  }
  else
  {
    envTextureName.Lower();
    _envTexture = GlobLoadTexture(GlobResolveTexture(envTextureName));
  } 

  if (_envTexture)
  {
    _envTexture->LoadHeaders();
    _envTexture->SetClamp(TexClampV);
  }
#endif

  _lzoCompression = false;

#if USE_LANDSCAPE_LOCATIONS
  _locations.Clear();

  ConstParamEntryPtr names = cls.FindEntry("Names");
  if (names)
  {
    int n = names->GetEntryCount();
    for (int i=0; i<n; i++)
    {
      ParamEntryVal entry = names->GetEntry(i);
      if (!entry.IsClass()) continue;
      // position
      Vector3 pos((entry >> "position")[0], 0, (entry >> "position")[1]);
      // name
      RString text = entry >> "name";
      // type
      RString typeName = "Name";
      ConstParamEntryPtr ptr = entry.FindEntry("type");
      if (ptr) typeName = *ptr;
      Ref<LocationType> type = LocationTypes.New(typeName);
      // size
      float a = 0;
      ptr = entry.FindEntry("radiusA");
      if (ptr) a = *ptr;
      float b = 0;
      ptr = entry.FindEntry("radiusB");
      if (ptr) b = *ptr;
      float angle = 0;
      ptr = entry.FindEntry("angle");
      if (ptr) angle = *ptr, angle = fastFmod(angle, 360.f), angle *= (H_PI / 180.f);

      Location *location = new Location(type, pos, a, b, angle);
      location->_text = text;
      location->_className = entry.GetName();
#if _ENABLE_CONVERSATION
      ptr = entry.FindEntry("speech");
      if (ptr) LoadSpeech(location->_speech, *ptr);

      location->_speechVariants.Resize(0);
      ptr = entry.FindEntry("SpeechVariants");
      if (ptr)
      {
        for (int i=0; i<ptr->GetEntryCount(); i++)
        {
          ParamEntryVal cls = ptr->GetEntry(i);
          if (!cls.IsClass()) continue;
          LocationSpeechVariant variant;
          variant._variant = cls.GetName();
          LoadSpeech(variant._speech, cls >> "speech");
          location->_speechVariants.AddUnique(variant);
        }
      }
      location->_speechVariants.Compact();
#endif
      location->_importance = (type ? type->_importance : 0) + (float)(n - i) / (float)n;
      _locations.AddLocation(location);
    }
    RebuildLocationDistances();
  }
#else
  _mountains.Clear();
#endif

  // good time to clean up allocator
  // now there should quite a lot of free memory
  MemoryCleanUp();
  _objectIdPrimary = _objectId = VisitorObjId();

  InitCache(true);
  
  _minTreesInForestSquare = 0;
  ConstParamEntryPtr limit = cls.FindEntry("minTreesInForestSquare");
  if (limit) _minTreesInForestSquare = *limit;

  _minRocksInRockSquare = 0;
  limit = cls.FindEntry("minRocksInRockSquare");
  if (limit) _minRocksInRockSquare = *limit;

  // reenter all vehicles
  if (GWorld)
  {
    GWorld->ForEachFastVehicle(LandscapeAddObject(this));
    GWorld->ForEachVehicle(LandscapeAddObject(this));
  }
  // any references from SurfaceCharacter need to be updated now
  
  // SurfaceCharacter always exists in TexMaterial
  
  // note: this may be too early
  // landscape surfaces not loaded yet
  ClearClutterMapping();
  ProgressRefresh();
}

/** process list of all buildins, extract most prominent "features" */
void Landscape::BuildFeatureList()
{
  struct IsFeature
  {
    bool operator () (Vector3Par pos, const EntityType *type) const
    {
      if (!type || type->GetFeatureSize(pos)<=0)
        return false;
      return true;
    }
  } isFeature;
  _features.Clear();
  for (int i=0; i<_buildings.Size(); i++)
  {
    const StaticEntityLink &building = _buildings.GetInfo(i);
    // to learn the size we need to load the shape (suitable for binarization, we have the shape loaded anyway)
    // or to check a list of featured class names (suitable for game, fast)
    
    // TODO: size or importance based culling
    if (isFeature(building._pos,building._type))
      _features.Add(FeatureInfo(building,building._pos,building._type));
  }
  // besides of discardable objects, some features may be non-discardable
  for( int x=0; x<GetLandRange(); x++ ) for( int z=0; z<GetLandRange(); z++ )
  {
    // we do not need to use UseObjects, because we are interested in non-discardable only
    const ObjectList &list=_objects(x,z);
    for( int i=0; i<list.Size(); i++ )
    {
      Object *obj=list[i];
      if( !obj ) continue;
      if( obj->GetType()!=Primary ) continue;
      if( !obj->GetShape() ) continue;
      const char *className=obj->GetShape()->GetPropertyClass();
      if (className && *className)
      {
        if (isFeature(obj->FutureVisualState().Position(),obj->GetEntityType()))
          _features.Add(FeatureInfo(OLinkL(Object)(obj),obj->FutureVisualState().Position(),obj->GetEntityType()));
      }
    }
  }
  _features.Compact();
}

/**
Used when building values in SurfaceCharacter
*/
int Landscape::FindClutter(const RStringB &name) const
{
  Assert(name.IsLowCase());
  for (int c=0; c<_clutter.Size(); c++)
  {
    if (_clutter[c]._name==name)
      return c;
  }
  // following log may be issued, because SurfaceInfo is global, but clutter is per-island only
  //LogF("Clutter type '%s' not found",cc_cast(name));
  return -1;
}

Landscape::~Landscape()
{
  FreeIds();
  /*
  for( int i=0; i<MAX_NETWORKS; i++ )
  {
    if( _networks[i] ) delete _networks[i],_networks[i]=NULL;
  }
  */
  _objIndex.Free();;
  _mapObjIndex.Free();;
  FreeRectCaches();
  //_listCache.Free();
  //_mapListCache.Free();
  // clear all objects
  _buildingsOffTree.Clear();
  _buildings.Clear();
  _features.Clear();
  _objects.Init(ObjectList());
  _mapObjects.Init(MapObjectList());
  _cloudsCache.Free();
  FreeCaches();
  _terrain->_outsideData.Free();

#if _ENABLE_PHYSICS
  //if(GetNetworkManager().IsPhysicsMaster())
    DestroyPhysics();
#endif
}

#if _ENABLE_PHYSICS
void Landscape::DestroyPhysics()
{
  if(_pPhysWorld && GetNetworkManager().IsPhysicsMaster()) 
  {
    _pPhysWorld->Destroy();
  }
}

void Landscape::InitPhysics(World *world)
{
  if(!GetNetworkManager().IsPhysicsMaster()) 
		return;

	DestroyPhysics();
  
  _pPhysWorld = world->GetPhysicsManager()->CreatePhysicsWorld("");
  if(_pPhysWorld)
	{
    enf::AAB bounds;
    if(!_pPhysWorld->InitWorld(this, false, bounds))
		{
			DoAssert(!"Can't create world collision model");
		}
	}

  if(_pPhysWorld)
	{
		// enable collision
		_pPhysWorld->SetInteractionLayer( EPL_WORLD, EPL_WORLD|EPL_DEFAULT|EPL_AI|EPL_CHARACTER|EPL_RAGDOLL|EPL_DEBRIS|EPL_SHELL|EPL_GRENADE|EPL_TERRAIN, true );
		_pPhysWorld->SetInteractionLayer( EPL_DEFAULT, EPL_WORLD|EPL_DEFAULT|EPL_AI|EPL_CHARACTER|EPL_RAGDOLL|EPL_DEBRIS|EPL_DOOR|EPL_GRENADE|EPL_TERRAIN, true );
		_pPhysWorld->SetInteractionLayer( EPL_CHARACTER, EPL_WORLD|EPL_DEFAULT|EPL_AI|EPL_FENCE|EPL_CHARACTER|EPL_DEBRIS|EPL_DOOR|EPL_GRENADE|EPL_TERRAIN|EPL_CARRIER, true );
    //_pPhysWorld->SetInteractionLayer( EPL_RAGDOLL, EPL_WORLD|EPL_DEFAULT|EPL_AI|EPL_FENCE|EPL_RAGDOLL|EPL_DEBRIS|EPL_DOOR|EPL_GRENADE|EPL_TERRAIN|EPL_BUILDING, true );
    _pPhysWorld->SetInteractionLayer( EPL_RAGDOLL, EPL_TERRAIN, true );
		_pPhysWorld->SetInteractionLayer( EPL_BUILDING, EPL_GRENADE|EPL_VEHICLE|EPL_AMPHIBIOUS_VEHICLE|EPL_WORLD|EPL_DEFAULT|EPL_AI|EPL_FENCE|EPL_CHARACTER|EPL_DEBRIS|EPL_WATERLAYER|EPL_TERRAIN|EPL_CARRIER, true );
		_pPhysWorld->SetInteractionLayer( EPL_WORLDBOUNDS, 0xffffffff, true);
		
    // disable collision
		_pPhysWorld->SetInteractionLayer( EPL_CHARACTER, EPL_WATERLAYER, false );
    _pPhysWorld->SetInteractionLayer( EPL_RAGDOLL, EPL_WATERLAYER, false );
		_pPhysWorld->SetInteractionLayer( EPL_NOCOLLISION, EPL_NOCOLLISION, false );
		
    /*
		PhysicsController* wc = world->GetWorldEntity()->GetPhysics();
		if(wc)
			wc->SetInteractionLayer(EPL_TERRAIN, 0);
    */

		//Create world bottom
/*
    enf::PhysicsMaterial* mat = _pPhysWorld->GetManager()->FindMaterial("material/ragdollflesh");//"material/worldbounds");
		if(mat != NULL)
		{
			AAB bounds = GetWorld()->GetBoundBox();

			//add six axis aligned boxes to keep world bounded
			Vector3 extents = bounds.GetSize();
			//make some space between outer bounds and world itself
			extents += Vector3(32.0f, 32.0f, 32.0f);
			Vector3 center = bounds.GetCenter();

			m_pBottom = (CGameEntity*)EntityManagerCC::New("GameEntity");

			const float thickness = 128.0f;

			enf::PhysicsGeomDef def;
			def.m_pMaterial = mat;
			def.m_iInteractionLayer = EPL_WORLDBOUNDS;
//XY box
			m_pBottom->SetMatrix(Matrix43(Vector3(center.x, center.y, bounds.GetMins().z)));
//			def.m_pGeom = mworld->CreateBoxGeom(Vector3(extents.x + thickness * 2.0f, extents.y + thickness * 2.0f, thickness) * 0.5f, true);
			def.m_pGeom = mworld->CreatePlaneGeom(Vector3(0.0f,0.0f,1.0f), 0);
			enf::PhysicsController* pc = mworld->CreateStaticBody(m_pBottom, &def, 1);
			pc->SetActive(enf::AS_INACTIVE);
		}
*/
	}

}
#endif

void Landscape::DoConstruct( Engine *engine, World *world )
{
#if _ENABLE_PHYSICS
	InitPhysics(world);
#endif

  _engine=engine;
  _world=world;
  _terrain->_texture.Clear();
  //for( int i=0; i<MAX_NETWORKS; i++ ) _networks[i]=NULL;
  CreateRectCaches();
  
  _segCache = new LandCache;
  CreateGroundClutterCache();
  CreateWeather();

#if _ARMA3_SIMUL_WEATHER
  CreateSimulWeather();
#endif

#if _VBS3_CPU_LAND_LIGHT
  _lightCache = new LandLightCache;
#endif


  _seaLevel=0; // sea level with tide
  _seaLevelWave=0; // sea level with wave effects
  _seaWaveSpeed=0.08;
  _terrain->_terSynth = false;

  _seaLevelOffset= 0.0f;
  _weatherUpdate = Glob.time;
}


void Landscape::FlushAICache()
{
#if _ENABLE_AI
  _operCache.Free();
  _operCache = CreateOperCache(this);
  _lockCache.Free();
  _lockCache = CreateLockCache(this);
  _suppressCache.Free();
  _suppressCache = CreateSuppressCache(this);
#endif
}

void Landscape::UpdateCaches()
{
  if (_operCache)
    _operCache->Update();
}
/**
@forceProcessing force parsing texture information (used after loading)
*/
void Landscape::InitCache(bool forceProcessing)
{
  _segCache->Init(this,forceProcessing);
  InitGroundClutterSegCache();

#if _VBS3_NO_SEABED
  ResetSeabedInfo();
#endif

  InitOutsideTextureInfo();
  // if there is no explicit outside material, we need to synthesize some
  // assume layered representation
}

void Landscape::InitOutsideTextureInfo()
{
  if (_terrain->_outsideTextureInfo._mat.IsNull())
  {
    const Scenery::TextureInfo &info = ClippedTextureInfo(0,0);
    _terrain->_outsideTextureInfo = info;
  }
}

void Landscape::FlushCache()
{
//  Assert( GWorld->NVehicles()==0 );
  FlushAICache();
  FlushRectCaches();
  _segCache->Clear();
  ClearGroundClutterCache();
  if( GScene )
  {
    GScene->FlushLandCache();
    //GScene->CleanUp();
  }
}

void Landscape::FlushSegCache()
{
  // we need to clear all caches which depend on terrain data
  _segCache->Clear();
  FlushGroundClutterSegCache();
}

void Landscape::FlushSegCache(int x, int z)
{
  if (_segCache)
    _segCache->ForceUpdate(x,z);
}

void Landscape::FreeCaches()
{
  // all caches become invalid now - release them
  _operCache.Free();
  _suppressCache.Free();

  FreeRectCaches();
  // make sure no AI units keep any locks now
  DoAssert(!_lockCache || _lockCache->IsEmpty());
  if (_segCache)
    _segCache->Clear();
  ClearGroundClutterCache();
}

bool Landscape::OnTerrainGridChanged()
{
  if (!GWorld)
  {
    // may be called in binarize, but it has no sense there
    // the cache is never used anyway there
    Assert(!_segCache || _segCache->FirstSegment()==NULL);
    return false;
  }
  float viewDist,gridSize;
  GWorld->GetCurrentTerrainSettings(viewDist,gridSize,GWorld->GetMode());

  // with low viewdistance there is no reason to use high lodBias
  float maxGrid = viewDist*(20.0f/1200);
  if (gridSize>maxGrid)
    gridSize = maxGrid;    

  float coarse = gridSize/GetTerrainGrid()*(10.0f/12.5f);
  const float invLog2 = 1.4426950409f;

  float lodBias = log(coarse)*invLog2;
  if (_terrain->_lodBias!=lodBias)
  {
    _terrain->_lodBias = lodBias;
    if (_segCache)
    {
      // we need to recreate all pushbuffers
      // TODO: not only Clear, also recreate
      _segCache->Clear();
    }
    return true;
  }
  return false;
}

#if USE_FAST_ALLOCATORS
  void LandscapeSetManualCleanUp(bool manualCleanup)
  {
    Landscape::WaterDrawTask::_allocatorF.SetManualCleanUp(manualCleanup);
    Landscape::LandDrawTask::_allocatorF.SetManualCleanUp(manualCleanup);
  }
  void LandscapeManualCleanUp()
  {
    Landscape::WaterDrawTask::_allocatorF.CleanUp();
    Landscape::LandDrawTask::_allocatorF.CleanUp();
  }
#endif

#if _ARMA3_SIMUL_WEATHER
void Landscape::CreateSimulWeather()
{
  DoAssert(GEngine);
  if (!GEngine)
    return;

  _enableSimulWeather = LandEditor ? false : (GEngine->GetSWGeometryLayersCount() != 0);

  if (!_enableSimulWeather)
    return;

  if (!_simulWeather)
  {
    _simulWeather = GEngine->CreateSimulWeather();
    DoAssert(_simulWeather);
    if (_simulWeather)
    {
      _simulWeather->Init();

      ParamEntryVal worlds = Pars >> "CfgWorlds";
      ParamEntryVal cls = worlds >> "DefaultWorld";
      if (strlen(Glob.header.worldname) > 0)
      {
        ConstParamEntryPtr clsE =  worlds.FindEntry(Glob.header.worldname);
        if (clsE)
        {
          cls = *clsE;
        }
      }
      LoadSimulWeather(cls);
    }
    else
    {
      Fail("Creation of Simul Weather failed!");
    }
  }
}

void Landscape::DrawDiags()
{
#if _ENABLE_CHEATS
  /*if (CHECK_DIAG(DEWeather))
  {
    int i=0;
    static InitVal<int,-1> handles[3];

    GlobalDiagMessage(handles[i++],0,500,Format("rain density: %f", GetRainDensity()));
    GlobalDiagMessage(handles[i++],0,500,Format("surface humidity: %f", _surfaceHumidity));
  }*/

#if _ARMA3_SIMUL_WEATHER
  if (CHECK_DIAG(DESimulWeather))
  {
    if (_simulWeather)
      _simulWeather->DiagDraw();
  }
#endif
#endif
}

void Landscape::LoadSimulWeather(ParamEntryPar entry, bool reInit)
{
  if (!_enableSimulWeather)
    return;

  if (reInit)
  {
    _simulWeather.Free();
    _simulWeather = GEngine->CreateSimulWeather();
    DoAssert(_simulWeather);
    if (_simulWeather)
    {
      _simulWeather->Init();
    }
  }

  DoAssert(_simulWeather);
  if (!_simulWeather)
    return;

  ParamEntryPtr par = entry.FindEntry("SimulWeather");
  if (par.NotNull())
  {
    _simulWeather->InitFromCfg(*par);
    _enableSimulWeather = true;
  }
  else
  {
    RptF("SimulWeather entry not found in world config - disabling simul weather.");
    _enableSimulWeather = false;
  }
}

bool Landscape::SetSimulWeatherLayersCount(int val)
{
#if _ARMA3_SIMUL_WEATHER
    if (GEngine->GetSWGeometryLayersCount() != val)
    {
      GEngine->SetSWGeometryLayersCount(val);
      EnableSimulWeather(val != 0);

      /*
      Do not free here, better to disable render and to set simulweather detail to lowest possible settings.
			Freeing here destroys memory and break other threads which use simulweather.

			 if (val == 0)
        _simulWeather.Free();
      else
      */
      {
        if (_simulWeather)
        {
          // update geometry helper
          int maxDistance = (int)_simulWeather->GetCloudRenderer()->GetHelperMaxDistance();
          _simulWeather->GetCloudRenderer()->SetHelperDetail(GEngine->GetSWGeometryLayersCount(), maxDistance);
          _simulWeather->ResetWeather();
        }
      }
      return true;
    }
#endif

  return false;
}

void Landscape::DrawSimulWeatherClouds(Scene &scene)
{
#if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_S))
  {
    _enableSimulWeather = !_enableSimulWeather;
    DIAG_MESSAGE(250, Format("Simul weather: %s", _enableSimulWeather ? "Enable" : "Disable"));
  }
#endif

  if (!_enableSimulWeather)
    return;
  if (!GEngine)
    return;
  if (GEngine->GetThermalVision())
    return;

  DoAssert(_simulWeather);
  if (!_simulWeather)
    return;

  Camera* camera=scene.GetCamera();
  DoAssert(camera);
  if (!camera)
    return;

  Camera::ClipRangeState state;
  camera->SaveClipRange(state);
  // even when the cloud or skybox is far, we still need the clouds to be rendered
  float maxZ = 100000;
  if (_simulWeather->GetCloudRenderer())
    maxZ = _simulWeather->GetCloudRenderer()->GetHelperMaxDistance() * 1.1f;

  camera->SetClipRange(camera->Near(),maxZ,Glob.config.GetShadowZ(), Glob.config.GetProjShadowsZ(), 4000);

  _simulWeather->Render();

  camera->RestoreClipRange(state);
}

#endif //_ARMA3_SIMUL_WEATHER



void Landscape::DrawHeatHazeEffect()
{
	HeatHazeRenderArray heatHazeInts;
	if (GScene->PrepareLightHeatHaze(heatHazeInts))
	{
		Texture *texture = GPreloadedTextures.New(HeatHazeNoiseTexture);

		if (!texture)
		{
			RptF("Landscape::DrawHeatHazeEffect() failed, no HeatHazeNoiseTexture");
			return;
		}

		MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 1, 0);

		if(!mip.IsOK() || mip._level != 0)
			return;

		GEngine->SetZPrimingDone(-1,true);
		GEngine->CaptureRefractionsBackground();
		GEngine->DrawLightHeatHaze(heatHazeInts, texture);
		GEngine->SetZPrimingDone(-1,false);
	}
}


#if _VBS3_LARGE_OBJECTS
void Landscape::SearchDynamicRTree(const float a_min[2], const float a_max[2], float r_min[2], float r_max[2])
{
  r_min[0] = FLT_MAX; r_min[1] = FLT_MAX;
  r_max[0] = FLT_MIN; r_max[1] = FLT_MIN;

  _largeDynamicObjects.Search(a_min,a_max,[this,&r_min,&r_max](Object *obj)->bool{
    Vector3 pos = obj->FutureVisualState().Position();
    r_min[0] = floatMin(pos.X(), r_min[0]);
    r_min[1] = floatMin(pos.Z(), r_min[1]);
    r_max[0] = floatMax(pos.X(), r_max[0]);
    r_max[1] = floatMax(pos.Z(), r_max[1]);
    return true;
  });
}

void Landscape::SearchStaticRTree(const float a_min[2], const float a_max[2], int r_min[2], int r_max[2])
{
  r_min[0] = INT_MAX; r_min[1] = INT_MAX;
  r_max[0] = INT_MIN; r_max[1] = INT_MIN;

  _largeStaticObjects.Search(a_min,a_max,[this,&r_min,&r_max](int largeObjectIndex)->bool{
    LargeObjectInfo &objInfo = _largeStaticObjectInfos[largeObjectIndex];
    int x = objInfo.posX;
    int z = objInfo.posZ;
    r_min[0] = intMin(x, r_min[0]);
    r_min[1] = intMin(z, r_min[1]);
    r_max[0] = intMax(x, r_max[0]);
    r_max[1] = intMax(z, r_max[1]);

    return true;
  });
}

void Landscape::OnObjectMove(Object* obj)
{
#if !_BINARIZE_REDUCEOBJECT
  obj->UpdatePrevBCPosRTree();
#endif
}

ObjectListUsedXXL::ObjectListUsedXXL(const ObjectListUsed &l, int x, int z) : _list(l)
{
#if _VBS3_LARGE_OBJECTS
  Landscape::RTreeStaticObject& largeObjects = GLandscape->LargeStaticObjects();
  float min[2]; min[0] = x*ObjGrid; min[1] = z*ObjGrid;
  float max[2]; max[0] = (x+1)*ObjGrid; max[1] = (z+1)*ObjGrid;

  largeObjects.Search(min,max,[this,&largeObjects](int largeObjectIndex)->bool {
    LargeObjectInfo &objInfo = GLandscape->_largeStaticObjectInfos[largeObjectIndex];
    if (!objInfo.object.IsReady()) return true;
    OLinkLPtr(Object) object = objInfo.object.GetLock();
    if (object.NotNull())
    {
      Object *obj = object.GetPointer();
      if (obj) 
        _largeObj.Add(obj);
    }
    return true;
  });
#endif
}

#endif


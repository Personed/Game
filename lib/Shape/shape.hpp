#ifdef _MSC_VER
#pragma once
#endif

#ifndef _SHAPE_HPP
#define _SHAPE_HPP

/*! \file 
Header file for Shape and LODShape class
*/

// object shape management

#include <El/elementpch.hpp>
#include <El/ParamFile/paramFileDecl.hpp>
#include <Es/Containers/boolArray.hpp>
#include <Es/Containers/staticArray.hpp>
#include <Es/Containers/streamArray.hpp>
#include <Es/Containers/rStringArray.hpp>
#include <Es/Containers/offTree.hpp>
#include <Es/Threads/pothread.hpp>
#include <El/Common/perfLog.hpp>

#include <El/Math/math3d.hpp>
#include <El/Common/perfLog.hpp>
#include "vertex.hpp"
#include "faceArray.hpp"
#include "poly.hpp"
#include "material.hpp"
#include "mapTypes.hpp"
#include "../engine.hpp"
#include "../types.hpp"

#include <El/QStream/qStream.hpp>

// defined below
class ConvexComponent;


#ifdef _MSC_VER
  #pragma warning(disable: 4355)
#endif

__forceinline void CheckMinMaxIter(Vector3 &min, Vector3 &max, Vector3Par val)
{
#if _KNI
  // KNI optimization
  min = _mm_min_ps(min.GetM128(), val.GetM128());
  max = _mm_max_ps(max.GetM128(), val.GetM128());
#elif __ICL
  if( min[0]>val[0] ) min[0]=val[0];
  if( max[0]<val[0] ) max[0]=val[0];
  if( min[1]>val[1] ) min[1]=val[1];
  if( max[1]<val[1] ) max[1]=val[1];
  if( min[2]>val[2] ) min[2]=val[2];
  if( max[2]<val[2] ) max[2]=val[2];
#else
  if( min[0]>val[0] ) min[0]=val[0];
  else if( max[0]<val[0] ) max[0]=val[0];
  if( min[1]>val[1] ) min[1]=val[1];
  else if( max[1]<val[1] ) max[1]=val[1];
  if( min[2]>val[2] ) min[2]=val[2];
  else if( max[2]<val[2] ) max[2]=val[2];
#endif
}

//not implemented
//#if _DEBUG
//  #define SKELETONSEPARATETYPE 1
//#else
  #define SKELETONSEPARATETYPE 0
//#endif

#if SKELETONSEPARATETYPE
//!{ SkeletonIndex
class SkeletonIndex
{
public:
  int _value;
  explicit SkeletonIndex(int value):_value(value){}

  SkeletonIndex() {_value = -1;}
  bool operator == (const SkeletonIndex &s) const {return _value == s._value;}
  bool operator != (const SkeletonIndex &s) const {return _value != s._value;}
  /// bones in the skeleton are ordered
  bool operator < (const SkeletonIndex &s) const {return _value < s._value;}
};
TypeIsSimple(SkeletonIndex)
#define SetSkeletonIndex(value) (SkeletonIndex(value))
#define SkeletonIndexNone (SkeletonIndex())
#define SkeletonIndexValid(skeletonIndex) ((skeletonIndex)._value>=0)
#define GetSkeletonIndex(skeletonIndex) ((skeletonIndex)._value)
//!}

//!{ SubSkeletonIndex
class SubSkeletonIndex
{
public:
  int _ssValue;
  SubSkeletonIndex() {_ssValue = -1;}
  bool operator== (const SubSkeletonIndex &s) const {return(_ssValue == s._ssValue);}
  bool operator!= (const SubSkeletonIndex &s) const {return(_ssValue != s._ssValue);}
};
TypeIsSimple(SubSkeletonIndex)
#define SetSubSkeletonIndex(subSkeletonIndex,value) ((subSkeletonIndex)._ssValue=(value))
#define GetSubSkeletonIndex(subSkeletonIndex) ((subSkeletonIndex)._ssValue)
//!}
#else
//!{ SkeletonIndex
typedef int SkeletonIndex;
#define SetSkeletonIndex(value) (value)
#define SkeletonIndexNone (-1)
#define SkeletonIndexValid(skeletonIndex) ((skeletonIndex)>=0)
#define GetSkeletonIndex(skeletonIndex) (skeletonIndex)
//!}

//!{ SubSkeletonIndex
typedef int SubSkeletonIndex;
#define SetSubSkeletonIndex(subSkeletonIndex,value) ((subSkeletonIndex)=(value))
#define GetSubSkeletonIndex(subSkeletonIndex) (subSkeletonIndex)
//!}
#endif

//! Set of sub-skeleton indices
//typedef AutoArray<SubSkeletonIndex> SubSkeletonIndexSet;
class SubSkeletonIndexSet : public AutoArray<SubSkeletonIndex>
{
public:
  static SubSkeletonIndexSet _default;
};

#define REPLICATE_BONE(matrices,ssis,matrix) \
  for (int i = 0; i < (ssis).Size(); i++) \
  { \
    (matrices)[GetSubSkeletonIndex((ssis)[i])] = (matrix); \
  }

#define REPLICATE_FIRST_BONE(matrices,ssis) \
  for (int i = 1; i < (ssis).Size(); i++) \
  { \
    (matrices)[GetSubSkeletonIndex((ssis)[i])] = (matrices)[GetSubSkeletonIndex((ssis)[0])]; \
  }

//! information about single vertex relation to set (see Selection)

struct SelInfo
{
  VertexIndex index;
  byte weight;
  SelInfo(){}
  SelInfo( VertexIndex i, byte w ):index(i),weight(w){}
};

TypeIsSimple(SelInfo);

class SerializeBinStream;

/// simple set of indices
class IndexSelection: public Buffer<VertexIndex>
{
  typedef Buffer<VertexIndex> base;
  
public:
  bool IsSelected(int i) const;
  bool IsSubset(const IndexSelection &sel) const;
  int Find(int i) const;
  
  IndexSelection &operator = (const base &src)
  {
    base::operator = (src);
    return *this;
  }
};

//! generic fuzzy set of indices
class Selection
{
protected:
  IndexSelection _sel; // short is enough to hold vertex index
  Buffer<byte> _weights; // byte is enough to hold weighting

  void DoDestruct();
  void DoConstruct(const Selection &src);

public:
  Selection() {}
  Selection(const SelInfo *sel, int nSel);
  Selection(const VertexIndex *sel, int nSel);
  void operator=(const Selection &src) {DoDestruct();DoConstruct(src);}
  Selection(const Selection &src) {DoConstruct(src);}
  void Clear() {DoDestruct();}

  void CreateIntervals();

  int Size() const {return _sel.Length();}
  VertexIndex operator[](int i) const {return _sel[i];}
  void Set(int i, VertexIndex index) {_sel[i] = index;}
  byte Weight(int i) const {return _weights ? _weights[i] : 255;}
  bool IsSelected(int i) const {return _sel.IsSelected(i);}
  //@ support for subset matching with other selections
  bool IsSubset(const IndexSelection &sel) const {return _sel.IsSubset(sel);}
  operator const IndexSelection &() const {return _sel;}
  //@}
  int Find(int i) const {return _sel.Find(i);}

  void Add(int vi, byte weigth=255);

  /// ConvexComponent needs to be able to get data
  void CopyVertexData(Buffer<VertexIndex> &tgt) const {tgt = _sel;}

  double GetMemoryUsed() const;
  size_t GetMemoryControlled() const;
  
  void SerializeBin(SerializeBinStream &f);
};
TypeIsMovable(Selection);

#include <Es/Containers/streamArray.hpp>

//! set of faces
class FaceSelection //: public Buffer<Offset>
{
  Buffer<int> _sections; // which sections are included in this selection
  bool _needsSections; // some selections have to include sections
public:
  FaceSelection();
  ~FaceSelection();
  void RescanSections(Shape *shape, const Selection &faces, const char *debugName);
  bool GetNeedsSections() const {return _needsSections;}
  void SetNeedsSections(bool needsSections) {_needsSections=needsSections;}
  int NSections() const {return _sections.Size();}
  int GetSection(int i) const {return _sections[i];}
  void SerializeBin(SerializeBinStream &f);
};

//! set of sections
class SectionSelection
{
  AutoArray<int> _sections;
public:
  SectionSelection();
  ~SectionSelection();
  void SerializeBin(SerializeBinStream &f);
  int NSections() const {return _sections.Size();}
  int GetSection(int i) const {return _sections.Get(i);}
  void AddSection(int s) {_sections.Add(s);}
  void SetSections(const AutoArray<int> &s) {_sections = s;}
};

#if _DEBUG || _PROFILE
#define NAMED_SEL_STATS 0
#endif

//! named selection - fuzzy set of vertices and set of faces
class NamedSelection: public Selection
{
  typedef Selection base;
private:
  RStringB _name;
  Selection _faces;
  mutable FaceSelection _faceSel;
  // note: section is face-based
#if _ENABLE_REPORT
    /// flag reporting if selection was ever used
    /** This is used to help creating model.cfg lists of unused selections. */
    mutable bool _wasUsed;
    /// flag reporting if per-vertex selection content (Selection base class) was ever used
    mutable bool _wasContentUsed;
#endif
#if NAMED_SEL_STATS
    /// additional info about creation
    RString _info;
#endif

protected:
  void DoConstruct();
  void DoDestruct();
  void DoConstruct( const NamedSelection &src );

public:
  // Mediate access of Selection from NamedSelection.
  /**
  Report which shape/selection is used
  */
  class Access
  {
    const NamedSelection &_sel;
    #if _ENABLE_REPORT
    const LODShape *_lShape;
    const Shape *_shape;
    #endif
    
    public:
    Access(const NamedSelection &sel, const LODShape *lShape, const Shape *shape)
    :
    #if _ENABLE_REPORT
    _lShape(lShape),_shape(shape),
    #endif
    _sel(sel)
    {
    }
    
    /// for some special LODs we always keep all selections
    static bool KeepAllSelections(const LODShape *lShape, const Shape *shape);
    
    /// called whenever we "touch" data
    void Touch() const;
    VertexIndex operator [] (int i) const {Touch();return _sel[i];}
    int Size() const {Touch();return _sel.Size();}
    bool IsSubset(const IndexSelection &sel) const {Touch();return _sel.IsSubset(sel);}
    operator const IndexSelection &() const {Touch();return _sel._sel;}
    operator const Selection &() const {Touch();return _sel;}
    void CopyVertexData(Buffer<VertexIndex> &tgt) const {Touch();_sel.CopyVertexData(tgt);}
    bool IsSelected( int i ) const {Touch();return _sel.IsSelected(i);}
    byte Weight( int i ) const {Touch();return _sel.Weight(i);}
    Selection &SetFaces() {Touch();return unconst_cast(_sel._faces);}
    const Selection &Faces() const {Touch();return _sel._faces;}
    void RescanSections(Shape *shape) const {Touch();unconst_cast(_sel).RescanSections(shape);}

    //@{ forward parts of NamedSelection interface
    const char *Name() const {return _sel.Name();}
    const RStringB &GetName() const {return _sel.GetName();}
    int NSections() const {return _sel.NSections();}
    int GetSection(int i) const {return _sel.GetSection(i);}
    void CalculateFaceOffsets(Buffer<Offset> &offsets, Shape *shape) const {return _sel.CalculateFaceOffsets(offsets,shape);}
    void ReportSelectionUsed() const {return _sel.ReportSelectionUsed();}
    bool GetNeedsSections() const {return _sel.GetNeedsSections();}
    //@}
  };
  
  NamedSelection(RString name, const SelInfo *points, int nPoints, const VertexIndex *faces, int nFaces);

  NamedSelection() {DoConstruct();}
  void operator=(const NamedSelection &src) {DoDestruct();DoConstruct(src);}
  NamedSelection(const NamedSelection &src) {DoConstruct(src);}
  ~NamedSelection() {DoDestruct();}
  
  const char *Name() const {return _name;}
  const RStringB &GetName() const {return _name;}
  
private:

  void RescanSections(Shape *shape) {_faceSel.RescanSections(shape,_faces,Name());}

public:

  Selection &Faces() {return _faces;}
  const Selection &Faces() const {return _faces;}

  bool GetNeedsSections() const {return _faceSel.GetNeedsSections();}
  void SetNeedsSections(bool needsSections) {_faceSel.SetNeedsSections(needsSections);}

  /// remove info no longer needed
  void RemoveAllButSections();
  int NSections() const {return _faceSel.NSections();}
  int GetSection(int i) const {return _faceSel.GetSection(i);}

  const FaceSelection &FaceOffsets(const Shape *shape) const {return _faceSel;}
  void CalculateFaceOffsets(Buffer<Offset> &offsets, Shape *shape) const;

  /// low-level access to selection data - be very careful using this
  Selection &SetData() {return *this;}

  double GetMemoryUsed() const;
  size_t GetMemoryControlled() const;
  void SerializeBin(SerializeBinStream &f);

#if _ENABLE_REPORT
    void CheckSectionsNeeded(LODShape *shape, const char *nameSel, const char *altName) const;
    void ReportSelectionUsed() const {_wasUsed=true;}
    void ReportSelectionContentUsed() const {_wasContentUsed=true;}
    bool WasUsed() const {return _wasUsed;}
    bool WasContentUsed() const {return _wasContentUsed;}
#else
    void ReportSelectionUsed() const {}
    bool WasUsed() const {return true;}
    void ReportSelectionContentUsed() const {}
    bool WasContentUsed() const {return true;}
#endif
};

/// provide a memory size calculation for AutoArray
template <>
struct GetMemoryUsedTraits<NamedSelection>
{
  static double GetMemoryUsed(const NamedSelection &src) {return src.GetMemoryUsed();}
};

TypeIsMovable(NamedSelection);

//! named property - generic string value
class NamedProperty
{
private:
  RStringB _name,_value;
public:
  NamedProperty( const char *name="", const char *value="" );
  const RStringB &Name() const {return _name;}
  const RStringB &Value() const {return _value;}
  void SerializeBin(SerializeBinStream &f);
};
TypeIsMovableZeroed(NamedProperty);

//! one frame of keyframe animation
class AnimationPhase
{
  AutoArray<Vector3> _points;
  // note: only ClipUserMask part of Clipped is used
  float _time;
  
public:
  AnimationPhase() {_time = 0.0f;}
  void SetTime(float time) {_time = time;}
  float Time() const {return _time;}
  Vector3Val operator[](int i) const {return _points[i];}
  Vector3 &operator[](int i) {return _points[i];}
  int Size() const {return _points.Size();}
  void Resize(int n) {_points.Resize(n);}
  void SerializeBin(SerializeBinStream &f);
};
TypeIsMovable(AnimationPhase);


class LightList;

#include <Es/Memory/normalNew.hpp>

//! proxy object slot
/*!
Slots for objects included as inner level of hierarchy (subobjects)
*/
struct ProxyObject: public RefCount
{
  // note: scale allowed in transform
  RStringB name; //!< original proxy name
  Matrix4 trans; //! relative proxy position
  Matrix4 invTransform; //!< inverse proxy position
  int id; //!< proxy index (second part of proxy name)
  int selection; //!< source selection index
  int section; //!< which section is this proxy in
  SkeletonIndex boneIndex; //!< bone index in the skeleton
  __forceinline const Matrix4 &GetTransform() const {return trans;}
  USE_FAST_ALLOCATOR

  ProxyObject();
  static ProxyObject *CreateObject(SerializeBinStream &f)
  {
    return new ProxyObject;
  }
  void SerializeBin(SerializeBinStream &f);
};

struct MTextureMap;

//TypeIsMovableZeroed(ProxyObject)

class ParamEntry;
class QFBankHandle;
class ShapeRef;
class ConvexComponents;
enum EVertexDecl;

/// flags used to tell which parts of geometry are not needed
enum GeometryOnly
{
  GONoNormals=1,
  GONoUV=2,
  GONoNormalsNoUV=GONoNormals|GONoUV
};

/// information about a single drawing instance for vertex shader instancing
struct ObjectInstanceInfo
{
  Matrix4 _origin;
  /// per instance coloring (including alpha?)
  Color _color;
  //! Shadow coefficient
  float _shadowCoef;
  /// object instance (it may be needed in some situations)
  Object *_object;
  Object *_parentObject;
  //! Flag to determine the sprite size is to be limited
  bool _limitSpriteSize;
};
TypeIsSimple(ObjectInstanceInfo)

const int PBInstancesLogMax = 3;
const int PBInstancesMax = 1<<PBInstancesLogMax;

class Skeleton;
class SkeletonSource;
struct GridRectangle;
struct RoadVertex;

//! Structure to hold the bone hierarchy member
struct SBoneRelation
{
  RStringB _bone;
  RStringB _parent;
};
TypeIsMovableZeroed(SBoneRelation);

/// traits for key comparison and searching
template <>
struct FindArrayKeyTraits<SBoneRelation>
{
  typedef const char *KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) {return !strcmp(a,b);}
  /// get a key from an item
  static KeyType GetKey(const SBoneRelation &a) {return a._bone;}
};

/// list of all textures in the Shape
/**
This is used to avoid having to traverse all materials and sections.
As some models change textures runtime, we do not keep Ref<Texture> here, but only a pointer to it.
*/
class TexAreaIndex
{
public:
  struct Item
  {
    /// where can be a Ref to the texture are we tracking found
    const RefR<Texture> *_tex;
    /// Area Over Texture for both principal and additional UV coordinates
    float _aot;
    /// sometimes we want some mip to be present
    bool _someDataWanted;
    
    ClassIsSimple(Item)
  };
  
  /// list of tracked textures
  AutoArray<Item> _items;
  /// is the list computed?
  bool _valid;
  
  TexAreaIndex();
  
  /// add or update a texture reference
  void AddItem(const RefR<Texture> *tex, float aot, bool someDataWanted);
  
  void Clear();
};

/// describe a hierarchy of bones
/** 
Representation suitable for fast hierarchical skeletal animation processing
Each parent is followed by his children.
For each parent we only record the number of children.
The index of each bone is given by the order in the sequence.
Example
A (root)
B   E
C D  F
is described as
2 2 0 0 1 0 
*/
class BoneTree
{
  /// 255 children seems more than enough - we have 255 bones per model limit anyway
  typedef unsigned char ChildrenCount;

  AutoArray<ChildrenCount> _children;

public:
  /// traverse part of the skeleton
  /*
  template <class Func>
  int Process(const Func &f, int bone=0, int parent=-1) const
  {
    f(bone,parent);
    // we start with bone 0, offset 0
    // recursive implementation for easy readability
    int children = _children[bone];
    parent = bone++;
    for (int i=0; i<children; i++)
    {
      // now process each child
      bone = Process(f,bone,parent);
    }
    return bone;
  }
  */

  /// traverse part of the skeleton, allow passing of additional arguments during the recursion
  template <class Func, class Context>
  int ProcessCtx(const Func &f, int bone=0, Context ctx=0) const
  {
    Context ctxChild = f(bone,ctx);
    // we start with bone 0, offset 0
    // recursive implementation for easy readability
    int children = _children[bone];
    bone++;
    for (int i=0; i<children; i++)
      // now process each child
      bone = ProcessCtx(f,bone,ctxChild);
    return bone;
  }

  template <class Func>
  int Process(const Func &f, int bone=0) const {return ProcessCtx(f,bone,bone);}

  /// no payload functor to be used as StorePayload in AddChildrenOf
  struct NoPayload {void operator()(SkeletonIndex bone) const {}};
  /// all pass predicate to be used as Predicate in AddChildrenOf
  struct AllPass {bool operator()(SkeletonIndex bone) const {return true;}};
  /// add all children of one bone as described by the set mask
  template <class Predicate, class StorePayload>
  int AddChildrenOf(const SkeletonSource *src, SkeletonIndex bone, const Predicate &test, const StorePayload &store);
  /// implementation of AddChildrenOf with default functors
  int AddChildrenOf(const SkeletonSource *src, SkeletonIndex bone);
  
  /// build a hierarchy from a source, including a reordered bone list
  void Build(const SkeletonSource *src, AutoArray<SBoneRelation> &ordered);
  /// build a hierarchy from a source, including a SkeltonIndex mapping
  void Build(const SkeletonSource *src, AutoArray<SkeletonIndex> &ordered);
  /// compact any memory used
  void Compact() {_children.Compact();}

#if _DEBUG
  private:
  struct UnitTest;
  static UnitTest unitTest;
#endif
};

/// Data of ConvexComponent modified by animations
struct ConvexComponentAnimationContext
{
  Vector3 _minMax[2]; // bounding box of the selection - used to accelerate tests
  Vector3 _center;
  float _radius;
  bool _enabled; // Used by collision detector, if false component is considered like passable.

  Vector3Val Min() const {return _minMax[0];}
  Vector3Val Max() const {return _minMax[1];}
  const Vector3 *MinMax() const {return _minMax;}

  Vector3Val GetCenter() const {return _center;}
  float GetRadius() const {return _radius;}

  bool IsEnabled() const {return _enabled;}
  void Enable() {_enabled = true;}
  void Disable() {_enabled = false;}

  void Recalculate(const ConvexComponent &cc, const Array<Vector3> &pos);
};
TypeIsSimple(ConvexComponentAnimationContext);

struct UVOffset
{
  float _u, _v;  
  bool _changed;
  UVOffset():_u(0.0f),_v(0.0f),_changed(false) {};
  UVOffset(float u, float v):_u(u),_v(v),_changed(true) {};
};
TypeIsSimple(UVOffset)

/// storage for MemAllocSA allocators used in VertexTableAnimationContext
template <int nVertices=3500, int nFaces=3500, int nSections=100, int nComponents=400>
struct AnimationContextStorage: public VertexTableAnimationContextStorage<nVertices>
{
  AUTO_STORAGE_ALIGNED(Plane,_plane,nFaces,AllocAlign16);
  AUTO_STORAGE_ALIGNED(PolyProperties,_sections,nSections,AllocAlign4);
  AUTO_STORAGE_ALIGNED(ConvexComponentAnimationContext,_convexComponents,nComponents,AllocAlign16);
  AUTO_STORAGE_ALIGNED(UVOffset,_UVOffsets,nSections,AllocAlign8);
  
  size_t SizeOfPlane() const {return sizeof(_plane);}
  size_t SizeOfSections() const {return sizeof(_sections);}
  size_t SizeOfConvexComponents() const {return sizeof(_convexComponents);}
  size_t SizeOfUVOffsets() const {return sizeof(_UVOffsets);}
  
  /// assume this type of storage always implies MT safety
  /** it is used for rendering and roadways, in both cases MT safety is needed*/
  bool NeedMTSafety() const {return true;}
};

/// similar to AnimationContextStorage, but using MemAllocDataStack memory
template <int nVertices=3500, int nFaces=3500, int nSections=100, int nComponents=400>
struct AnimationContextStorageDS: public VertexTableAnimationContextStorageDS<nVertices>
{
  void *_plane;
  void *_sections;
  void *_convexComponents;
  void *_UVOffsets;
  size_t _planeOld;
  size_t _sectionsOld;
  size_t _convexComponentsOld;
  size_t _UVOffsetsOld;
  
  /// this storage is not MT safe, therefore the caller must not require MT safety
  bool NeedMTSafety() const {return false;}
  
  AnimationContextStorageDS():VertexTableAnimationContextStorageDS<nVertices>()
  {
    _plane = GDataStack.Allocate(nFaces*sizeof(Plane),sizeof(AllocAlign16),_planeOld);
    _sections = GDataStack.Allocate(nSections*sizeof(PolyProperties),sizeof(AllocAlign4),_sectionsOld);
    _convexComponents = GDataStack.Allocate(nComponents*sizeof(ConvexComponentAnimationContext),sizeof(AllocAlign16),_convexComponentsOld);
    _UVOffsets = GDataStack.Allocate(nSections*sizeof(UVOffset),sizeof(AllocAlign8),_UVOffsetsOld);
  }
  ~AnimationContextStorageDS()
  {
    // stack - release order reversed
    GDataStack.Release(_UVOffsets,_UVOffsetsOld);
    GDataStack.Release(_convexComponents,_convexComponentsOld);
    GDataStack.Release(_sections,_sectionsOld);
    GDataStack.Release(_plane,_planeOld);
  }

  static size_t SizeOfPlane() {return nFaces*sizeof(Plane);}
  static size_t SizeOfSections() {return nSections*sizeof(PolyProperties);}
  static size_t SizeOfConvexComponents() {return nComponents*sizeof(ConvexComponentAnimationContext);}
  static size_t SizeOfUVOffsets() {return nSections*sizeof(UVOffset);}

};


/// storage when used in rendering
/**
in HW animation there are no vertices, no faces and no components - do not waste space on them
However when doing SW animation we may need to use some vertices.
MT safe.
*/
typedef AnimationContextStorage<64,64,100,4> AnimationContextStorageRender;
/// storage when used in collision testing
/** geometry may use substantial data (soldiers, complex vehicles), but need not be MT safe - use the data stack */
typedef AnimationContextStorageDS<3500,3500,100,400> AnimationContextStorageGeom;
//// roadway animation - little data needed, MT safety required (may be called from rendering)
typedef AnimationContextStorage<160,160,8,8> AnimationContextStorageRoadway;

class ModelConfig;
class AnimationContext;

/// used for collision testing
#if _ENABLE_PHYSICS
namespace enf
{
  struct PhysicsGeomDef;
  class PhysicsGeometry;
  struct PhysicsMaterial;
}
#endif

//! collection of all information connected to single level.
/*!
  Shape is mainly used to build LODShape LOD level set.
  It is based on VertexTable and besides of vertices contains also faces,
  named selections (NamedSelection), named properties (NamedProperty),
  key-frame animation (AnimationPhase) and proxy object (ProxyObject)
*/
class Shape: public VertexTable, public LinkBidirWithFrameStore
{
  typedef VertexTable base;

  friend class Object;
  friend class LODShape;
  friend class ShapeRef;
  friend class RefShapeRef;
  friend class ShapeUsed;
  friend class ShapeRefLoadable;
  friend class AnimationContext;
  
private:
  //! Subset of the skeleton (indices to skeleton)
  FindArray<SkeletonIndex> _subSkeletonToSkeleton;
  //! Indices to subskeleton (array)
  AutoArray<SubSkeletonIndexSet> _skeletonToSubSkeleton;

  /// hierarchical representation of subskeleton
  BoneTree _ssTree;
  /// SkeletonIndex corresponding to each bone in ssTree (excluding the first root entry)
  AutoArray<SkeletonIndex> _ssTreeSI;
  
  //! if Shape is managed, point to the managing ShapeRef
  ShapeRef *_trackRef;

  SRef<IShapeLoadSource> _source;
  /// load count for the geometry (vertex+face) data - see 
  mutable int _loadCount;
#if _ENABLE_REPORT
  /// load checking when CBs are used
  mutable AtomicInt _loadCountCB;
#endif
#if _ENABLE_PHYSICS
  enf::PhysicsGeomDef* _physGeoms;
  unsigned int _physGeomsLen;
#endif

  /// assert if geometry is locked
  #define CHECK_GLOCK() Assert(IsLoadLocked())
  
  ShapeRef *GetTrackRef();

  //! Stream of faces.
  /*!
    One face consists from several indices. Indices refer to vertices from vertex table.
  */
  FaceArray _face;
  /// tex.area information for all textures, used to optimize mipmap handling
  TexAreaIndex _texArea;
  
  /// area of all non-proxy faces in the shape
  float _faceArea;
  
  //! Declaration mask computed in the constructor from all sections
  //int _vertexDeclarationMask;
  //! Vertex declaration of the shape computed in the DefineSections method
  EVertexDecl _vertexDeclaration;

  /// or'ed EngineShapeProperties required by this shape
  int _shapePropertiesNeeded;
  //! Determines, if vertex declaration is calculated
  bool _isVertexDeclarationAvailable;
  //! Determines whether shape can be instanced or not
  bool _canBeInstanced;
  //! Determines whether push buffer can be used for this shape
  mutable bool _canUsePushBuffer;
  /// once all textures are ready, they will be ready forever
  mutable bool _allTexturesReady;
  //! temporary face properties - needed before conversion to sections is performed
  AutoArray<PolyProperties> _faceProperties;

  //! Array of planes (Normal vector, distance) corresponds to array of faces.
  mutable AutoArray<Plane> _plane;

  RefArray<Texture> _textures; // all textures used in this object

  // TODO: avoid conversion
  AutoArray<VertexIndex> _pointToVertex; // objective point index to vertex index conversion
  AutoArray<VertexIndex> _vertexToPoint;

  mutable Buffer<Offset> _faceIndexToOffset; // built when necessary

  // named selections - used for animations...
  AutoArray<NamedSelection> _sel;
  AutoArray<NamedProperty> _prop;
  AutoArray<AnimationPhase> _phase;
  PackedColor _colorTop;
  PackedColor _color;
  int _special;
  bool _faceNormalsValid;
  bool _loadWarning; // some warning during load - report filename

  // proxy objects
  RefArray<ProxyObject> _proxy;

protected:
  /// load shape with tags, return resolution (or -1 when some error occurred)
  float LoadTagged(QIStream &f, bool reversed, int ver, GeometryOnly geometryOnly, AutoArray<float> &massArray, bool tagged, const LODShape *lodShape, int level);

  /// binary save/load
  void SerializeBin(SerializeBinStream &f, bool loadGeometry, int &begGeom, int &endGeom, int &begFaces, int &endFaces, LODShape *lodShape);

  /// add a keyframe animation
  void AddPhase(const AnimationPhase &phase);

private:
  void operator=(const Shape &src); // assignment not defined

public:

  //@{ VertexTable implementation
  virtual bool IsLoadLocked() const;
  //@}

  //! Creates a shadow geometry from the specified shape
  void UpdateToShadowGeometry(const LODShape *lodShape, bool separateFaces, const char *errName);
  //! Generates ST according to current faces, positions and UV coordinates
  void GenerateSTArray(const LODShape *lodShape, int level);

  Shape();
  Shape(const Shape &src, bool copyAnimations=true, bool copySelections=true, bool copyST = true);

#if _ENABLE_PHYSICS
  const int GetPhysLen() const {return _physGeomsLen;}
  enf::PhysicsGeomDef* GetPhysComps() const {return _physGeoms;}
#endif

  virtual ~Shape();
  void Clear();

  /// clear selections and point-vertex conversion tables
  void ClearSelections();

  /// amount of memory controlled (directly and exclusively owner)
  size_t GetMemoryControlledByShape(bool includeUnloadable) const;
  
  /// amount of memory used
  double GetMemoryUsed() const;
  /// amount of memory which can be unloaded when necessary
  double GetMemoryUsedUnloadable() const;

  void Reverse();

  //! Sorts faces according to textures / materials to minimize state changes
  void Optimize();
  /// optimize, ignore alpha ordering
  void OptimizeIgnoreAlpha(); // sort by texture/render state
  /// concat sections with identical attributes, no reordering
  void ConcatSections(int start);
  // be careful: Optimize invalidates face offsets/indices
  void SortVertices(const LODShape *lodShape, int level);
  // be careful: SortVertices invalidates vertex indices
  //! sort faces to minimize section count and find sections - take care of animated textures
  void FindSections(const LODShape *lodShape, bool forceMaterial0=false);
  void RescanSections(const LODShape *lodShape);

  //! get properties of one specific face (used for section finding)
  const PolyProperties &FaceProperties(int f) const {return _faceProperties[f];}
  //! get properties of one specific face (used for section finding)
  PolyProperties &FaceProperties(int f) {return _faceProperties[f];}

  //! special case section - whole shape consist of one section
  void SetAsOneSection(const ShapeSectionInfo &sec);

  __forceinline int NProxies() const {return _proxy.Size();}
  __forceinline ProxyObject &Proxy(int i) {return *_proxy[i];}
  __forceinline const ProxyObject &Proxy(int i) const {return *_proxy[i];}
  
  
  __forceinline int NFaces() const {CHECK_GLOCK();return _face.Size();}
  __forceinline int FacesRawSize() const {CHECK_GLOCK();return _face.RawSize();}

  __forceinline float TotalFaceArea() const {return _faceArea;}

  /// called when section material is changed to indicate vertex declaration needs recalculation
  void VertexDeclarationNeedsUpdate() {_isVertexDeclarationAvailable = false;}
  void UpdateVertexDeclarationFromSections();

  EVertexDecl GetVertexDeclaration() const
  {
    if (!_isVertexDeclarationAvailable)
      unconst_cast(this)->UpdateVertexDeclarationFromSections();
    return _vertexDeclaration;
  }
  
  /// build TexAreaIndex from sections
  void BuildTexAreaIndex(TexAreaIndex &tgt) const;
  /// special case - build _texArea from sections
  void BuildTexAreaIndex(){BuildTexAreaIndex(_texArea);}

  /// check if _texArea is valid
  bool CheckTexAreaIndex() const;
  
  int GetShapePropertiesNeeded() const {return _shapePropertiesNeeded;}
  void UpdateShapePropertiesNeeded();

  //! _canUsePushBuffer wrapper
  bool CanUsePushBuffer() const {return _canUsePushBuffer;}
  //! Set flag to determine the pushbuffer cannot be created for this shape
  void DisablePushBuffer() const {_canUsePushBuffer = false;}
  //! Function will disable pushbuffer upon the shape properties
  /*!
    When calling this function, materials and other stuff must be prepared. The proper place is
    f.i. during creation of VB.
  */
  void CheckPushbufferCanBeUsed() const;

  void SetCanBeInstanced();
  bool CanBeInstanced() const {return _canBeInstanced;}

  /// conversion to and from original shape (Objektiv) points
  /** if there is no conversion, assume identity */
  int VertexToPoint(int i) const {return _vertexToPoint.Size()==0 ? i : _vertexToPoint[i];}
  int PointToVertex(int i) const {return _pointToVertex.Size()==0 ? i : _pointToVertex[i];}
  int NPoints() const {return _pointToVertex.Size()>0 ? _pointToVertex.Size() : NVertex();}
  bool ExplicitPoints() const {return _pointToVertex.Size()>0;}
  void InvalidateVertexToPoint(int i) 
  {
    _vertexToPoint.Access(i);
    _vertexToPoint[i]=-1;
  }
  void SetVertexToPoint(int i, VertexIndex vertexToPoint)
  {
    _vertexToPoint.Access(i);
    _vertexToPoint[i] = vertexToPoint;
  }
  //! Clears the _vertexToPoint array
  __forceinline void ClearVertexToPoint() {_vertexToPoint.Clear();}
  
  bool OptimizePointToVertex(bool pointsNeeded);

  // conversion between face indices and offsets
  void BuildFaceIndexToOffset(Buffer<Offset> &offsets) const;
  void BuildFaceIndexToOffset() const;
  Offset FaceIndexToOffset(int i) const {return _faceIndexToOffset[i];}

  __forceinline const ShapeSection &GetSection(int i) const {return _face._sections[i];}
  __forceinline ShapeSection &GetSection(int i) {return _face._sections[i];}
  __forceinline int NSections() const {return _face._sections.Size();}
  void AddSection(const ShapeSection &sec); // add section, respect selection boundaries
  void AddSection(NamedSelection *sel, const Offset *o, int n, const PolyProperties &prop); // add sections containing given faces
  
  __forceinline const Poly &Face(Offset i) const {CHECK_GLOCK(); return _face[i];}
  __forceinline Poly &Face(Offset i) {CHECK_GLOCK(); return _face[i];}
  __forceinline void SetFacesCanonical(bool canonical){_face.SetCanonical(canonical);}
  __forceinline bool GetFacesCanonical() const {return _face.GetCanonical();}

  __forceinline const Array<Plane> &GetPlanesArray() const {return _plane;}
  __forceinline const Plane &GetPlane(int i) const {return _plane[i];}
  __forceinline Plane &GetPlane(int i) {return _plane[i];}
  void InitPlanes(); // not all shapes need planes
  
  __forceinline const FaceArray &Faces() const {return _face;}
  __forceinline FaceArray &Faces() {return _face;}

  __forceinline Offset BeginFaces() const {CHECK_GLOCK(); return _face.Begin();}
  __forceinline Offset EndFaces() const {CHECK_GLOCK(); return _face.End();}
  __forceinline void NextFace(Offset &i) const {CHECK_GLOCK(); _face.Next(i);}
  __forceinline void NextFaceCanonical(Offset &i) const {CHECK_GLOCK(); _face.NextCanonical(i);}

  Offset FindFace(int i) const {CHECK_GLOCK(); return _face.Find(i);}
  const Poly &FaceIndexed(int i) const {CHECK_GLOCK(); return _face[_face.Find(i)];}
  Poly &FaceIndexed(int i) {CHECK_GLOCK(); return _face[_face.Find(i)];}
  //! Clears the face array represented by _face member.
  __forceinline void ClearFaces() {_face.Clear();}
  //! Clears the face properties array represented by _faceProperties member.
  __forceinline void ClearFaceProperties() {_faceProperties.Clear();}
  //! Add one face to the face array.
  __forceinline Offset AddFace(const Poly &face) {return _face.Add(face);}
  //! Add one face to the face array.
  __forceinline void AddFace(const Poly &face, const PolyProperties &prop)
  {
    _face.Add(face);
    _faceProperties.Add(prop);
  }
  //! Add one face to the face array and create a section for it
  void AddFaceAndCreateSection(const Poly &face, const ShapeSectionInfo &prop);
  //! Add new faces into the last section
  __forceinline void ExtendLastSection(const ShapeSectionInfo &prop, bool forceNewSection=false)
  {
    _face.ExtendSection(prop, forceNewSection);
    VertexDeclarationNeedsUpdate();
  }
  //! Reserves space for nFaces faces.
  /*!
    \param nFaces Number of faces to reserve space for.
  */
  __forceinline void ReserveFaces(int nFaces) {_face.Reserve(nFaces);}
  //! Reserves space for nFaces properties
  __forceinline void ReserveFaceProperties(int nFaces) {_faceProperties.Realloc(nFaces);}
  //! Return size of the _faceProperties array
  __forceinline int FacePropertiesSize() const {return _faceProperties.Size();}
  void MergeFaces(const StreamArray<Poly,StaticArray<char> > &faces)
  {
    _face.Merge(faces.RawData(),faces.RawSize(),faces.Size());
  }
  void FaceReserveRaw(int size) {_face.ReserveRaw(size);}
  
  void SetPoints(const Vector3 *point, const ClipFlags *clip, int nPoints, const Vector3 *normal, int nNormals);
  bool VerifyStructure() const;
  bool VerifyFaceStructure() const {return _face.VerifyStructure();}
  void SetFaces(const FaceArray &src);

  //__forceinline void SetLevel(int level){_level=level;}
  //__forceinline int GetLevel() const {return _level;}

  void MakeCockpit() const;

  void OrSpecial(int special);
  void OrSpecialNotRecurse(int special) {_special |= special;}
  void AndSpecial(int special);
  void SetSpecial(int special);
  __forceinline int Special() const {return _special;}

  //! Method to update references to vertex
  void UpdateVertexReferences(VertexIndex oldVI, VertexIndex newVI);

  //! Replicates specified vertex (updates selections as well), returns position of the new one
  VertexIndex ReplicateVertex(VertexIndex vi);

  //! Makes each face to have it's own vertices
  /*!
    Shape musn't contain keyframe animation. Original pos must not be valid.
    Selections are being updated. If the _pointToVertex
    and _vertexToPoint are present, then they're updated too.
  */
  void SeparateFaceVertices();
  //! Sets a specified material to all sections
  void SetUniformMaterial(TexMaterial *material);
  void SetUniformMaterial(TexMaterial *material, Texture *texture);
  void SetUniformMaterialEmmisiveColor(const Color &color);
  void SetUniformMaterialAmbientColor(const Color &color);
  void SetUniformMaterialDiffuseColor(const Color &color);
  void SetUniformMaterialForcedDiffuseColor(const Color &color);
  void SetUniformMaterialRenderFlag(RenderFlag rf, bool value);

  //! Creates array of bone references parallel to vertex array (bone indices refer to skeleton index - so it's not the final state)
  void CreateVertexBoneRef(const Ref<SkeletonSource> &skeletonSource, AutoArray<AnimationRTWeight> &vertexBoneRefTemp, const LODShape *lodShape);
  void CreateNighborBoneRef(const AutoArray<AnimationRTWeight> &vertexBoneRefTemp, AutoArray<VertexNeighborInfo> &neighborBoneRefTemp, const LODShape *lodShape, float resolution);

  /// create a hierarchical representation of the subskeleton
  void CreateSubskeletonHierarchy(const SkeletonSource *ss);
  
  /// create information about bones used in one LOD level
  void CreateSubskeleton(const Ref<SkeletonSource> &skeletonSource, bool includeNeighbourFaceVertices, int bonesInterval, const LODShape *lodShape, float resolution, const ModelConfig *modelConfig);
  SkeletonIndex GetSkeletonIndexFromSubSkeletonIndex(SubSkeletonIndex ssi) const
  {
    // skeleton should always exist
    Assert(_skeletonToSubSkeleton.Size() > 0);
    if (GetSubSkeletonIndex(ssi) < 0)
      return SkeletonIndexNone;
    else
    {
      Assert(_subSkeletonToSkeleton.Size() > 0);
      return _subSkeletonToSkeleton[GetSubSkeletonIndex(ssi)];
    }
  }
 
  const SubSkeletonIndexSet& GetSubSkeletonIndexFromSkeletonIndex(SkeletonIndex si) const
  {
    // skeleton should always exist
    Assert(_skeletonToSubSkeleton.Size() > 0);
    if (GetSkeletonIndex(si) < 0)
      return SubSkeletonIndexSet::_default;
    else
    {
      DoAssert(GetSkeletonIndex(si) < _skeletonToSubSkeleton.Size());
      return _skeletonToSubSkeleton[GetSkeletonIndex(si)];
    }
  }
  
  int GetSubSkeletonSize() const {return _subSkeletonToSkeleton.Size();}

  //@{ _ssTreeSI access - subskeleton as hierarchy ordered SkeletonIndex
  const BoneTree &GetSubSkeletonTree() const {return _ssTree;}
  int GetSubSkeletonTreeSize() const {return _ssTreeSI.Size();}
  SkeletonIndex GetSubSkeletonTreeBone(int i) const {return _ssTreeSI[i];}
  //@}
  
protected:
  // shape optimization
  //void RemoveFaces( int mustHave, int mustNotHave );
  //void CheckIsolatedPoints( AutoArray<int> &isolated );
  //void RemoveIsolatedPoints( const AutoArray<int> &include,  const AutoArray<int> &exclude );
  //void RemoveIsolatedPoints();

  //void CheckIsolatedNormals( AutoArray<int> &isolated );
  //void RemoveIsolatedNormals( const AutoArray<int> &include );
  //void RemoveIsolatedNormals();

  Shape *ExtractPath(LODShape *lodShape); // optimize this shape as path lod

  //void OptimizeMesh();

public:
  //void MergeFast( const Shape *with, const Matrix4 &transform );
  void Merge(const Shape *with, const Matrix4 &transform);
  //! Reallocates the space of all members according to it's real size.
  void Compact();

  PackedColor GetColor() const {return _color;}
  PackedColor GetColorTop() const {return _colorTop;}
  /// calculate average of all texture colors
  void CalculateColor();
  /// automatically determine clamping flags based on texture UV mapping
  void AutoClamp();
  /// after shapes have merged, automatic clamping infromation may need updating
  void AdjustAutoClamp();

  void SurfaceSplit(const Landscape *land, const Matrix4 &toWorld, float y, float useOrigY, const GridRectangle *rect=NULL, bool quickAdd=false, int lod=0);
  
  void Triangulate();

  //! Delete back faces (if possible) and mark them
  void DeleteBackFaces();

#if _VBS3_CRATERS_DEFORM_TERRAIN
  //! Function will use polygons from specified shape and create set of convex components aligned with ground
  void CreateAlignedConvexComponents(const Shape &src);
#endif

  void DefineSections(ParamEntryVal cfgModels, ParamEntryVal cfg);
  
  bool SerializeBinPushBuffer(SerializeBinStream &f);
  void ConvertToVBuffer(VBType type, const LODShape *lodShape=NULL, int level=-1) const;

  bool CustomTLRequired() const;

  void RecalculateFaceArea();

  void RecalculateAreas();
  void RecalculateNormals(bool full);
  /// check if normals are valid now
  bool VerifyNormals();

  /** Function recalculates faces planes. It also finds BBox and BSphere, 
    * but only for the vertexes selected by enabled convex components.
    * @param cc - convex components with selections from this shape
    * @param full - true recalculate normals an D-s for all planes, false only D-s.
    */
  void RecalculateNormals(const ConvexComponents& cc, bool full );
  
  void RecalculateNormalsAsNeeded()
  {
    if (!_faceNormalsValid)
      RecalculateNormals(true);
  };

  void RecalculateNormalsAsNeeded(const ConvexComponents& cc)
  {
    if (!_faceNormalsValid)
      RecalculateNormals(cc, true);
  };

  void CalculateMinMaxFromCC(const ConvexComponents& cc);

  void DeleteFace(int i)
  {
    Offset offset = _face.Find(i);
    _face.Delete(offset);
  }
  
  // maintain named selections (points and faces)
  int FindNamedSel(const char *name) const;
  int FindNamedSel(const char *name, const char *altName) const;
  void AddNamedSel(const NamedSelection &sel);
  NamedSelection &NamedSel(int i) {return _sel[i];}
  const NamedSelection &NamedSel(int i) const {return _sel[i];}
  int NNamedSel() const {return _sel.Size();}

  const NamedSelection::Access NamedSel(int i, const LODShape *lodShape) const {return NamedSelection::Access(_sel[i], lodShape, this);}

  int NTextures() const {return _textures.Size();}
  Texture *GetTexture(int i) const {return _textures[i];} // get texture
  int AddTexture(Texture *texture) {return _textures.AddUnique(texture);}
  int GetTextureIndex(Texture *tex) const {return _textures.Find(tex);}
  /// check if the texture is owned by the model (including materials)
  bool IsOwningTexture(Texture *texture) const;

  /// check if all texture headers are already loaded
  bool CheckTexturesReady() const {return _allTexturesReady;}

  void ForceTexturesReady();
  bool MakeTexturesReady() const;

  /// used for diagnostics - access AoT in the _texArea based on texture ID
  float GetTextureAOT(Texture *tex) const;
  
  Texture *FindTexture(const char *name) const;
  
  /// Initialize the storage for the shape data changed by animation
  void InitAnimationContext(AnimationContext &animContext, const ConvexComponents *cc, bool animateGeometry) const;

  //! Function to draw all sections in simple (non-instanced) version of drawing
  void DrawSections(int cb, const AnimationContext *animContext, ColorVal animColor, const EngineShapeProperties &prop, int spec, int bias, const DrawParameters &dp) const;
  //! Rendering of this shape
  /*!
    /param minDist2 Minimum distance of the object from camera
  */
  void Draw(int cb, AnimationContext *animContext, ColorVal animColor, 
    const EngineShapeProperties &prop, const LightList &lights, ClipFlags clip, int spec, const PositionRender &transform, float minDist2,
    const DrawParameters &dp=DrawParameters::_default) const;
  bool DrawUsingTL(int spec, ClipFlags clip) const;

  /// preload textures, report how many are left
  int PreloadTexturesLeft(float z2, AnimationContext *animContext, bool animated=false, float *quality=NULL) const;
  /// preload textures, report when done
  bool PreloadTextures(float z2, AnimationContext *animContext, bool animated=false, float *quality=NULL) const
  {
    return PreloadTexturesLeft(z2, animContext, animated, quality) == 0;
  }
  
  //! Retrieving of all textures used by the model to be ready to set
  void PrepareTextures(float z2, AnimationContext *animContext, int special, bool prepareBaseTextureOnly) const;

  void SetProperty(const char *name, const char *value);
  int FindProperty(const char *name) const;
  int FindProperty(const RStringB &name) const;
  const NamedProperty &NamedProp(int i) const {return _prop[i];}
  const RStringB &PropertyValue(const char *name) const;
  const RStringB &PropertyValue(const RStringB &name) const;

  /// VertexTable virtual function implementation
  virtual void GeometryLoadedHandler();
  virtual bool GeometryUnloadedHandler();
  
  /// we may need to adjust textures after model is loaded
  void AdjustTextures();

private:
  void PreparePhase(const AnimationPhase *&prevPos, const AnimationPhase *&nextPos, float &interpol, float time, float baseTime) const;

public:
  Vector3 PointPhase(int i, float time, float baseTime) const; // interpolate point position
  void SetPhase(AnimationContext &animContext, const Selection &anim, float time, float baseTime) const; // interpolate between two nearest phases
  void SetPhase(AnimationContext &animContext, float time, float baseTime) const; // interpolate between two nearest phases

  bool IsAnimated() const {return _phase.Size()>1;}
  void SetPhaseIndex(int index);
  int NAnimationPhases() const {return _phase.Size();}

  int PrevAnimationPhase(float time) const;
  int NextAnimationPhase(float time) const;
  float AnimationPhaseTime(int index) const {return _phase[index].Time();}
  const AnimationPhase &GetAnimationPhase(int i) const {return _phase[i];}

  // some object have hand defined destructed shape
  // (stored in special shape)
  
  Vector3 CalculateCenter(const Selection &sel) const; // calculate geometrical center
  Vector3 CalculateCenterOfMass(const Selection &sel) const; // calculate center of mass

  bool CheckIntegrity() const;

protected:
  /// helper for LoadFaces and SerializeBin - perform face array loading
  void DoFaceLoad(SerializeBinStream &f, LODShape *lodShape, int version);

public:
  //@{ face unloading support
  bool RequestFaces(const QFBankHandle &source, LODShape *lodShape, int version
#ifdef _M_PPC
    , QFileEndian endian
#endif
    , bool lzoCompression);
  void LoadFaces(const QFBankHandle &source, LODShape *lodShape, int version
#ifdef _M_PPC
    , QFileEndian endian
#endif
    , bool lzoCompression);
  void UnloadFaces();
  size_t FacesSize() const;
  //@}

  //@{ constant geometry loading/unloading
  /// load unloaded data
  void LoadGeometry(const ShapeRef *shapeRef);
  //! unload data that can be reloaded from the disk when necessary
  void UnloadGeometry();
  //! check if geometry is currently loaded
  bool IsGeometryLoaded() const;
  //! check if geometry is currently locked (guaranteed to be loaded)
  bool IsGeometryLocked() const {return _source.IsNull() || _loadCount>0 || _nVertex==0;}
  /// check if geometry can be unloaded
  bool GeometryCanBeUnloaded() const {return _source.NotNull();}
  /// request background loading
  bool RequestGeometry() const;

  /// lock geometry, return true when lock was just acquired
  bool AddRefLoad() const;
  /// unlock geometry, return true when all locks are released
  bool ReleaseLoad() const;
  
  /// set handle for geometry unloading
  void SetLoadHandle(const QFBankHandle &handle, const QFBankHandle &handleFaces, LODShape *lShape, int version
#ifdef _M_PPC
    , QFileEndian endian
#endif
    , bool lzoCompression);
  void ResetLoadHandle();
  void DisableGeometryStreaming(ShapeRef *shapeRef);

  /// debug name - needed for error messages
  RString GetDebugName() const;
  //@}

  /// implementation for LinkBidirWithFrameStore  
  __forceinline size_t GetMemoryControlled() const {return GeometrySize() + FacesSize();}
  
  USE_FAST_ALLOCATOR
};

class ObjectVisualState;

/// Data of Shape modified by animations
class AnimationContext : public VertexTableAnimationContext, private NoCopy
{
protected:
  const ObjectVisualState &_vs;
  /// Array of planes (Normal vector, distance) corresponds to array of faces in Shape.
  AutoArray<Plane, MemAllocSA> _plane;
  /// properties of Shape sections
  AutoArray<PolyProperties, MemAllocSA> _sections;
  /// modifications to convex components
  AutoArray<ConvexComponentAnimationContext, MemAllocSA> _convexComponents;
  /// UV offsets for each section, additional information for section to animate tracks on tanks
  AutoArray<UVOffset, MemAllocSA> _UVOffsets;

  template <class Storage>
  void SetStorage(Storage &storage)
  {
    VertexTableAnimationContext::SetStorage(storage);
    _plane.SetStorage(MemAllocSA(storage._plane,storage.SizeOfPlane()));
    _sections.SetStorage(MemAllocSA(storage._sections,storage.SizeOfSections()));
    _convexComponents.SetStorage(MemAllocSA(storage._convexComponents,storage.SizeOfConvexComponents()));
    _UVOffsets.SetStorage(MemAllocSA(storage._UVOffsets,storage.SizeOfUVOffsets()));
  }

  /// stored pointer to convex component for the late initialization (Load)
  const ConvexComponents *_cc;

  /// AnimationContext is initialized from Shape (to check if not uninitialized or initialized twice)
  bool _initialized;
  /// AnimationContext is initialized from Shape (to check if not uninitialized or initialized twice)
  bool _geometryInitialized;
  /// _sections array loaded
  bool _loadedSections;
  /// _UVOffsets array loaded
  bool _loadedUVOffsets;
  /// _plane array loaded
  bool _loadedPlanes;
  /// _convexComponents array loaded
  bool _loadedCC;
  /// the _plane array is up to date
  bool _faceNormalsValid;
  /// the _convexComponents array is up to date
  bool _convexComponentsValid;
  /// the caller may require MT safety of all computations done on the context
  bool _needMTSafety;

public:
  AnimationContext(const ObjectVisualState &vs):_vs(vs)
  {
    _initialized = false;
    _geometryInitialized = false;
    _loadedSections = false;
    _loadedPlanes = false;
    _loadedCC = false;
    _needMTSafety = false;
    _cc = NULL;
    _loadedUVOffsets = false;
  }
  template <class Storage>
  AnimationContext(const ObjectVisualState &vs, Storage &storage):_vs(vs)
  {
    SetStorage(storage);
    _initialized = false;
    _geometryInitialized = false;
    _loadedSections = false;
    _loadedPlanes = false;
    _loadedCC = false;
    _needMTSafety = storage.NeedMTSafety();
    _cc = NULL;
    _loadedUVOffsets = false;
  }

  /// basic initialization (copying of arrays not included)
  void Init(const Shape *shape, const ConvexComponents *cc, bool animateGeometry);
  /// late initialization (copying of _sections)
  void LoadSections(const Shape *shape);
  /// late initialization (copying of _plane)
  void LoadPlanes(const Shape *shape);
  /// late initialization (copying of _convexComponents)
  void LoadCC();
  /// initialize _UVOffsets
  void LoadUVOffsets(const Shape *shape);

  __forceinline const Array<Plane> &GetPlanesArray(const Shape *shape) const
  {
    return _loadedPlanes ? (const Array<Plane> &)_plane : (const Array<Plane> &)shape->_plane;
  }
  __forceinline const Plane &GetPlane(const Shape *shape, int i) const
  {
    return _loadedPlanes ? _plane[i] : shape->_plane[i];
  }

  __forceinline const PolyProperties &GetSection(const Shape *shape, int i) const
  {
    return _loadedSections ? _sections[i] : shape->_face._sections[i];
  }
  // load the context if needed
  __forceinline PolyProperties &SetSection(const Shape *shape, int i)
  {
    LoadSections(shape); return _sections[i];
  }

  __forceinline const float GetUOffset(const Shape *shape, int i) const
  {
    return _loadedUVOffsets ? _UVOffsets[i]._u : 0.0f;
  }
  __forceinline const float GetVOffset(const Shape *shape, int i) const
  {
    return _loadedUVOffsets ? _UVOffsets[i]._v : 0.0f;
  }
  __forceinline const bool GetUVchanged(const Shape *shape, int i) const
  {
    return _loadedUVOffsets ? _UVOffsets[i]._changed: false;
  }
  // load the context if needed
  __forceinline void SetUVOffset(const Shape *shape, int i, float u, float v )
  {
    LoadUVOffsets(shape); _UVOffsets[i]._u = u; _UVOffsets[i]._v = v;_UVOffsets[i]._changed = true;  
  }

  bool IsCCEnabled(int i) const
  {
    return _loadedCC ? _convexComponents[i].IsEnabled() : true; // by default, all CC are enabled
  }
  void CCEnable(int i)
  {
    LoadCC();
    _convexComponents[i].Enable();
  }
  void CCDisable(int i)
  {
    LoadCC();
    _convexComponents[i].Disable();
  }
  Vector3Val GetCCMin(int i) const;
  Vector3Val GetCCMax(int i) const;
  const Vector3 *GetCCMinMax(int i) const;
  Vector3Val GetCCCenter(int i) const;
  float GetCCRadius(int i) const;

  const ObjectVisualState &GetVisualState() const {return _vs;}

  /// access to animation context if available (otherwise, the caller will use the Shape directly)
  const VertexTableAnimationContext *GetVertexTableContext() const {return _geometryInitialized & AreVerticesLoaded() ? this : NULL;}

  bool NeedMTSafety() const {return _needMTSafety;}

  void InvalidateNormals(const Shape *shape);
  void RecalculateNormalsAsNeeded(const Shape *shape)
  {
    bool valid = _loadedPlanes ? _faceNormalsValid : shape->_faceNormalsValid;
    if (!valid)
      RecalculateNormals(shape);
  };
  void RecalculateNormalsAsNeeded(const Shape *shape, const ConvexComponents &cc)
  {
    bool valid = _loadedPlanes ? _faceNormalsValid : shape->_faceNormalsValid;
    if (!valid)
      RecalculateNormals(shape, cc);
  };
  void InvalidateConvexComponents() {_convexComponentsValid = false;}
  void RecalculateConvexComponentsAsNeeded(const Shape *shape, const ConvexComponents &cc)
  {
    if (!_convexComponentsValid)
      RecalculateConvexComponents(shape, cc);
  }

  void CalculateMinMaxFromCC(const Shape *shape, const ConvexComponents &cc);

protected:
  void RecalculateNormals(const Shape *shape);
  /** Function recalculates faces planes. It also finds BBox and BSphere, 
  * but only for the vertexes selected by enabled convex components.
  * @param cc - convex components with selections from this shape
  * @param full - true recalculate normals an D-s for all planes, false only D-s.
  */
  void RecalculateNormals(const Shape *shape, const ConvexComponents &cc);
  void RecalculateConvexComponents(const Shape *shape, const ConvexComponents &cc);
};

class ShapeRefManaged;

//! shape automatic loading/unloading - interface
class ShapeRef: public RefCountSafe
{
protected:
  friend class LODShape;
  Ref<Shape> _ref;

public:
  ShapeRef();
  ShapeRef(Shape *shape);
  virtual ~ShapeRef();

  void operator=(Shape *shape);

  //! access via operator ->
  __forceinline Shape * operator->() const {return _ref.GetRef();}
  __forceinline Shape * operator()() const {return _ref.GetRef();}
  __forceinline Shape * GetRef() const {return _ref.GetRef();}

  // some function may be handled either by Shape, or by ShapeRef

  virtual bool NVerticesAndAreaKnown() const {return true;}
  virtual int NVertices() const {return _ref.GetRef()->NVertex();}
  virtual float FaceArea() const {return _ref.GetRef()->TotalFaceArea();}
  virtual int NFaces() const {return _ref.GetRef()->NFaces();}
  virtual int Special() const {return _ref.GetRef()->Special();}
  virtual ClipFlags GetOrHints() const {return _ref.GetRef()->GetOrHints();}
  virtual PackedColor GetColor() const {return _ref.GetRef()->GetColor();}
  virtual bool HasSkeleton() const {return _ref.GetRef()->GetSubSkeletonSize()>0;}

  virtual void Lock() = 0;
  virtual void Unlock() = 0;
  virtual bool IsLocked() const = 0;
  virtual bool IsLoaded() const = 0;
  virtual bool RequestLoading(bool load) const = 0;
  virtual void OnGeometryLoaded(Shape *shape) const = 0;

  virtual ShapeRefManaged *GetManagedInterface() {return NULL;}
};


//! loadable shape - can be unloaded when not used
class ShapeRefManaged: public ShapeRef, public LinkBidirWithFrameStore
{
  typedef ShapeRef base;
  /// lock count - how many owner require the data to exist
  int _locked;
  
#if _ENABLE_REPORT
    /// CB debugging - verify Lock/Unlock is paired
    AtomicInt _lockedCB;
#endif

public:
  ShapeRefManaged(Shape *shape);
  ~ShapeRefManaged();

  // implements functions from ShapeRef
  virtual void Lock();
  virtual void Unlock();
  virtual bool IsLocked() const;

  int GetLockCount() const {return _locked;}

  // following functions should be implemented by derived class
  //@{{ loading/unloading - perform caching
  virtual void Load();
  virtual void Unload();
  //@}
  
  //@{ real loading/unloading (no caching)
  virtual void DoLoad() = 0;
  virtual void DoUnload() = 0;
  //@}
  //@{ managed shape is never NULL - it can be only unloaded
  virtual bool IsNull() {return false;}
  virtual bool NotNull() {return true;}
  
  virtual size_t GetMemoryControlled() const = 0;
  //virtual RString GetDebugName() const = 0;

  virtual ShapeRefManaged *GetManagedInterface() {return this;}
protected:
  //@{ temporary locking - can be used to pretend the shape is locked
  void TempLock() {_locked++;}
  void TempUnlock() {_locked--;}
  //@}
};

/// underlying implementation for ShapeUsed
class RefShapeRef: public Ref<ShapeRef>
{
  typedef Ref<ShapeRef> base;

public:
  RefShapeRef(){}
  RefShapeRef(Shape *shape)
  :base(shape ? shape->GetTrackRef() : NULL)
  {
  }
  RefShapeRef(const base &src)
  :base(src)
  {
  }
  void operator=(Shape *shape)
  {
    base::operator=(shape ? shape->GetTrackRef() : NULL);
  }
  void operator=(const base &src)
  {
    base::operator=(src);
  }
  Shape *GetRef() const
  {
    ShapeRef *ref = base::GetRef();
    return (ref) ? ref->GetRef() : NULL;
  }
  void SetPermanentRef(Shape *shape);

  //const ShapeRef &operator ->() const {return *base::GetRef();}
  Shape *operator->() const {return base::GetRef()->GetRef();}

  bool HasSkeleton() const {return base::GetRef()->HasSkeleton();}
  int NFaces() const {return base::GetRef()->NFaces();}
  int Special() const {return base::GetRef()->Special();}
  ClipFlags GetOrHints() const {return base::GetRef()->GetOrHints();}
  PackedColor GetColor() const {return base::GetRef()->GetColor();}

  void Lock() const {base::GetRef()->Lock();}
  void Unlock() const {base::GetRef()->Unlock();}
  bool IsLoaded() const {return base::GetRef()->IsLoaded();}
  bool IsLocked() const {return base::GetRef()->IsLocked();}
  bool RequestLoading(bool load=false) const {return base::GetRef()->RequestLoading(load);}
  bool RequestLoadingAndLoad() const
  {
    Assert(!base::GetRef()->IsLoaded());
    return base::GetRef()->RequestLoading(true);
  }
  //void IsNull() const {return base::IsNull();}
  ShapeRef *GetShapeRef() const {return base::GetRef();};
};

/// shape cannot be unloaded during lifetime of this object
class ShapeUsed
{
  Shape *_ref;
public:
  ShapeUsed():_ref(NULL){}
  ShapeUsed(const ShapeRef *ref):_ref(NULL)
  {
    // this is the main purpose of the object - locking the object loads it when needed
    if (ref)
    {
      unconst_cast(ref)->Lock();
    _ref = ref->GetRef();
  }
  }
  ShapeUsed(Shape *shape)
  :_ref(shape)
  {
    // shape is already locked
    if (_ref && _ref->GetTrackRef())
      _ref->GetTrackRef()->Lock();
  }
  ~ShapeUsed()
  {
    if (_ref && _ref->GetTrackRef())
      _ref->GetTrackRef()->Unlock();
  }

  ShapeUsed(const ShapeUsed &src)
  :_ref(src._ref)
  {
    if (_ref && _ref->GetTrackRef())
      _ref->GetTrackRef()->Lock();
  }
  void operator =(const ShapeUsed &src)
  {
    if (src._ref && src._ref->GetTrackRef())
      src._ref->GetTrackRef()->Lock();
    if (_ref && _ref->GetTrackRef())
      _ref->GetTrackRef()->Unlock();
    _ref = src._ref;
  }

  const Shape *GetShape() const {return _ref;}
  /// can be called only where modification of Shape is valid (InitShapeLevel / DoneShapeLevel etc.)
  Shape *InitShape() const {return _ref;}

  operator const Shape*() const {return _ref;}
  const Shape *operator->() const {return _ref;}
  bool IsNull() const {return _ref==NULL;}
  bool NotNull() const {return _ref!=NULL;}
};

/// reuse already used shape
/** similar to ShapeUsed, but no locking/unlocking is done here, we only verify the lock is already taken */
class ShapeUsedRe
{
  Shape *_ref;
public:
  ShapeUsedRe(const RefShapeRef &ref)
  {
    // we need to verify some lock exists
    if (ref.NotNull())
    {
      Assert(ref.IsLocked());
    }
    _ref = ref.GetRef();
  }
  ShapeUsedRe(Shape *shape)
  :_ref(shape)
  {
    // shape is already locked - nothing to verify
  }
  ~ShapeUsedRe()
  {
    // we would like to verify the shape is still valid
    // however this is more difficult than it sounds. How do we recognize if the pointer points to the same object?
    // The memory could be freed and allocated again by another Shape
  }

  ShapeUsedRe(const ShapeUsedRe &src)
  :_ref(src._ref)
  {
    // shape is already locked - nothing to verify
  }
  void operator =(const ShapeUsedRe &src)
  {
    // shape is already locked - nothing to verify
    _ref = src._ref;
  }

  const Shape *GetShape() const {return _ref;}
  /// can be called only where modification of Shape is valid (InitShapeLevel / DoneShapeLevel etc.)
  Shape *InitShape() const {return _ref;}

  operator const Shape*() const {return _ref;}
  const Shape *operator->() const {return _ref;}
  bool IsNull() const {return _ref==NULL;}
  bool NotNull() const {return _ref!=NULL;}
};

#define OCCLUSIONS_SUPPORTED_BY_SHAPE 0
#include "edges.hpp"

/// edge used for horizon occlusion
struct OcclusionEdge
{
  //@{ position indices for the edge vertices
  int a, b;
  //@}
  
  OcclusionEdge(){}
  OcclusionEdge(int v1, int v2)
  {
    // edges are not oriented - record each edge only once
    Assert(v1 != v2);
    if (v1 < v2)
    {
      a = v1;
      b = v2;
    }
    else
    {
      a = v2;
      b = v1;
    }
  }
  bool operator==(const OcclusionEdge &with) const {return a == with.a && b == with.b;}
};
TypeIsSimple(OcclusionEdge)

struct BlendBone
{
  SkeletonIndex bone;
  RString boneName;
  float weight;

  BlendBone(){}
  BlendBone(SkeletonIndex bone, RString boneName, float weight):bone(bone),boneName(boneName),weight(weight){}

  bool operator == (const BlendBone &x) const {return bone==x.bone && weight==x.weight;}
  __forceinline bool operator != (const BlendBone &x) const {return !operator ==(x);}
  ClassIsMovableZeroed(BlendBone)
};

struct BlendSkinning
{
  AutoArray<BlendBone> bones;

  bool operator != (const BlendSkinning &x) const
  {
    return bones!=x.bones;
  }
};

struct SkinnedPosition: public BlendSkinning
{
  // TODO: implement lazy evaluation, so that skinning is not determined unless the handler is called
  Object *obj;

  Vector3 originalPosition;

  SkinnedPosition(Object *obj=nullptr):obj(obj),originalPosition(VZero){}

  bool operator != (const SkinnedPosition &x) const
  {
    return obj!=x.obj || originalPosition.Distance2(x.originalPosition)>1e-6f || BlendSkinning::operator !=(x);
  }
};


class ConvexComponent: public RefCount, private NoCopy
{
  // name of original named selection
  RStringB _name;
  // numeric part of _name
  int _nameIndex;
  /// vertex data replicated from NamedSelection
  IndexSelection _sel;
  /// plane indices - define polyhedron using half spaces
  /** index in Shape::_plane */
  AutoArray<int> _planes; 
  
  Vector3 _minMax[2]; // bounding box of the selection - used to accelerate tests
  Vector3 _center;
  float _radius;
  Vector3 _centerOBB;
  Vector3 _whAngle;
  bool _isBox; //acceleration for beveled collision tests. Box don't need any extra planes
#if OCCLUSIONS_SUPPORTED_BY_SHAPE
  SRef<ComponentEdges> _edges;
#endif

  Ref<Texture> _texture; // by texture we distinguish some material properties
  Ref<const SurfaceInfo> _surfaceInfo; // physical properties / semantics
#if _ENABLE_PHYSICS
  enf::PhysicsMaterial* _physMat;
#endif

public:
  ConvexComponent();
  ~ConvexComponent();

  void Init(Shape *shape, const NamedSelection::Access &sel, int nameIndex);
  Vector3Val Min() const {return _minMax[0];}
  Vector3Val Max() const {return _minMax[1];}
  const Vector3 *MinMax() const {return _minMax;}
  Vector3Val GetCenter() const {return _center;}
  float GetRadius() const {return _radius;}
  Texture *GetTexture() const {return _texture;}
  const SurfaceInfo *GetSurfaceInfo() const {return _surfaceInfo;}
#if _ENABLE_PHYSICS
  enf::PhysicsMaterial* GetPhysMaterial() const {return _physMat;}
#endif
  //!Optimization for sweep-sphere tests.
  bool IsBox() const { return _isBox; }

#if _ENABLE_PHYSICS
  bool IsPrepared();
  bool GetPhysGeom(Shape *shape, enf::PhysicsGeomDef& def);
#endif

  void GetSkinning(BlendSkinning &skinning, const Shape *s, const LODShape *lShape) const;

  void CalcOBB(Vector3 *rect, float xResize, float zResize) const;

  bool IsInside(const Array<Plane> &planes, Vector3Val point) const;

  void Recalculate(Shape *shape); // recalculate after animation - if dirty
  void RecalculateNormals(Shape *shape) const;
  void CalculateOOB(Shape *shape);

  /// XZ axis reversal
  void Reverse();
  
  void GetOBB(float &w, float &h, float &angle) const { w = _whAngle[0]; h = _whAngle[1]; angle = _whAngle[2]; }
  Vector3Val GetOBBCenter() const { return _centerOBB; }

  //@{ NamedSelection like access
  int Size() const {return _sel.Size();}
  VertexIndex operator [] (int i) const {return _sel[i];}
  const RStringB &GetName() const {return _name;}
  //@}
  
  int GetNameIndex() const {return _nameIndex;}
  int NPlanes() const {return _planes.Size();}
  int GetPlaneIndex(int index) const {return _planes[index];}
  /** Function recalculates planes in whole _shape. Call it before loops with GetPlane. */
  void InitPlanes(Shape *shape) const {shape->InitPlanes();}
  const Plane &GetPlane(const Array<Plane> &planes, int i) const;
  const Poly &GetFace(const Shape *shape, int i) const;
  Offset GetFaceOffset(const Shape *shape, int i) const
  {
    return shape->FaceIndexToOffset(_planes[i]);
  }
 
  //@ support for subset matching with other selections
  bool IsSubset(const IndexSelection &sel) const {return _sel.IsSubset(sel);}
  operator const IndexSelection &() const {return _sel;}
  //@}
  
#if OCCLUSIONS_SUPPORTED_BY_SHAPE
  ComponentEdges *GetEdges() const {return _edges;}
  void SetEdges( ComponentEdges *edges ) {_edges=edges;}
#endif
  size_t GetMemoryControlled() const;
  double GetMemoryUsed() const {return _planes.GetMemoryUsed();}
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

//TypeIsMovable(ConvexComponent);

#define MAX_LOD_LEVELS 32

#define LOD_INVISIBLE 127

//#include "specLods.hpp"

#define ALPHA_SPLIT 0
#define N_ALPHA 1
#define for_each_alpha

//#ifndef DECL_ENUM_MAP_TYPE
//#define DECL_ENUM_MAP_TYPE
//DECL_ENUM(MapType)
//#endif

#include <Es/Memory/normalNew.hpp>
#include "BulletCollision\CollisionShapes\btCompoundShape.h"
class ConvexComponents: public RefCount, public RefArray<ConvexComponent>
{
  typedef RefArray<ConvexComponent> base;
  mutable bool _valid;

  //private:
  //ConvexComponents( const ConvexComponents &src );
  //void operator =( const ConvexComponents &src );

public:
  ConvexComponents();
  ~ConvexComponents();

  /// given XXX, find component corresponding to ComponentXXX
  int FindNameIndex(int nameIndex) const;
  bool RecalculateEdges( Shape *shape, const LODShape *lShape);
  void Recalculate( Shape *shape ) const;
  void Validate() {_valid=true;}
  void Invalidate() {_valid=false;}
  bool IsValid() const {return _valid;};
  void RecalculateAsNeeded( Shape *shape ) const;
  size_t GetMemoryControlled() const;
  
  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

struct RoadwayFace
{
  OffTreeRegion<float> region;
  Offset o;
  int i;
  int section;

  const OffTreeRegion<float> &GetRegion() const {return region;}
  bool operator == (const RoadwayFace &with) const {return o==with.o;}
  bool IsIntersection(const OffTreeRegion<float> &reg) const
  {
    return region.IsIntersection(reg);
  }
  ClassIsSimple(RoadwayFace)
};

/// roadway faces ordered for fast lookup
class RoadwayFaces: public RefCount, public OffTree<RoadwayFace>
{
};

struct MemoryUnloadableContext;

/// handle shape loading and unloading
class LODShapeLoadHandler
{
#if _ENABLE_REPORT
  int _registeredCount;
#endif
public:
  //@{ whole shape loaded/unloaded
  virtual void LODShapeLoaded(LODShape *shape) {}
  virtual void LODShapeUnloaded(LODShape *shape) {}
  //@}
  //@{ one lod level loaded/unloaded
  virtual void ShapeLoaded(LODShape *shape, int level) = 0;
  virtual void ShapeUnloaded(LODShape *shape, int level) = 0;
  //@}
  /// check if given shape is ready
  /** used in LODShape::CheckLevelLoaded */
  virtual bool IsShapeReady(const LODShape *lodShape, int level, const Ref<ProxyObject> *proxies, int nProxies, bool requestIfNotReady) {return true;}
  
#if _ENABLE_REPORT
  void HandlerRegistered() {_registeredCount++;}
  void HandlerUnregistered() {_registeredCount--;}
  LODShapeLoadHandler() {_registeredCount = 0;}
  ~LODShapeLoadHandler() {DoAssert(_registeredCount == 0);}
#else
  void HandlerRegistered(){}
  void HandlerUnregistered(){}
#endif
};

//! Determination where the geometry designed for shadow buffer rendering purposes in the model comes from
enum SBSource
{
  SBS_Visual,       // The geometry for SB rendering should be read from usual lods (the same way like in SW shadows)
  SBS_ShadowVolume, // The shadow volume is the source (but the geometry resides in special level created from shadow volume during  binarization)
  SBS_Explicit,     // Special shadow buffer levels are to be considered as a shadow geometry
  SBS_None,         // No SB shadows specified
};

/// what animation algorithm can be used for the shape
#ifndef DECL_ENUM_EANIMATION_TYPE
#define DECL_ENUM_EANIMATION_TYPE
DECL_ENUM(EAnimationType)
#endif
DEFINE_ENUM_BEG(EAnimationType)
  AnimTypeNone,
  AnimTypeSoftware,
  AnimTypeHardware
DEFINE_ENUM_END(EAnimationType)


struct TreeCrownInfo
{
  Vector3 _minmax[2];
};

class AnimationHolder;
class Skeleton;

/// list of possible shape properties which need to be set for the shaders
enum EngineShapeProperty
{
  ESPTreeCrown=1,
};
/// interface to get properties common for all LODs

class EngineShapeProperties
{
public:
  //!{ Interval of sections to be drawn (if _endSection <= 0 or _begSection < 0, all sections are to be drawn)
  int _begSection;
  int _endSection;
  //!}
  //! Constructor
  EngineShapeProperties() {_begSection = _endSection = -1;}
  /// check list of supported properties
  virtual int GetEngineShapeProperties() const;
  /// access to tree crown parameters
  virtual void GetTreeCrown(Vector3 *minMax) const;
  /// access to shadow offset
  virtual float GetShadowOffset() const {return 0;}
};

//! Target arguments the model is binarized for
struct ModelTarget
{
  //! Default value
  static const ModelTarget _defaultValue;
  
  //! Number of bones to be drawn at once
  int _bonesInterval;
  
  //! Constructor
  ModelTarget();
};

/// corner cover in a model
struct CoverModelCorner
{
  Vector3 pos;
  Vector3 out;
  int score;
  int component;

  CoverModelCorner(){}
  CoverModelCorner(Vector3Par pos, Vector3Par out, int score, int component)
  :pos(pos),out(out),score(score),component(component)
  {
  }
};

TypeIsMovableZeroed(CoverModelCorner)

/// edge cover in a model
struct CoverModelEdge
{
  Vector3 pos1;
  Vector3 pos2;
  Vector3 out;
  int score;
  int component;
  
  CoverModelEdge(){}
  CoverModelEdge(Vector3Par pos1, Vector3Par pos2, Vector3Par out, int score, int component)
  :pos1(pos1),pos2(pos2),out(out),score(score),component(component)
  {
  }
};

TypeIsMovableZeroed(CoverModelEdge)

/// describe possible cover positions in a model
struct CoverModel
{
  AutoArray<CoverModelCorner> _corners;
  AutoArray<CoverModelEdge> _edges;
  
public:
  CoverModel(){}
  CoverModel(const LODShape *shape){Build(shape);}
  void Build(const LODShape *shape);
};

static inline void ReverseVector( Vector3 &v ) {v[0] = -v[0],v[2] = -v[2];}
static inline Vector3 ReverseV( const Vector3 &v ) {return Vector3(-v[0],v[1],-v[2]);}

void ReverseMinMax( Vector3 *v );

struct BinarizeFolderContext;

//! collections of shapes for all LOD levels and special levels
/*!
Each LODShape consist of several LOD levels. There are also several special-purpose levels:

Memory (different points used for animation or simulation, usually named)

Geometry (collision geometry)

Fire Geometry (geometry used for bullet/shooting collisions)

View Geometry (geometry used for intersecting line of sight)

Pilot View Geometry (occlusion geometry from pilot's point of view)

Gunner View Geometry (occlusion geometry from gunner's point of view)

Commander View Geometry (occlusion geometry from commander's point of view)

Cargo View Geometry (occlusion geometry from cargo space view)

Land Contact (points used to calculate collision with ground or roadways)

Roadway (roadways used for collisions with entities) 

Paths (paths used for AI path-finding)

Hitpoints (points defining local damage)

Each level has number 'resolution' which is describing it function - resolution in
case of graphical LOD levels, special (very large) value in case of special levels.
*/

class LODShape: public RemoveLLinks, public LinkBidirWithFrameStore, public EngineShapeProperties
{
  friend class Object;
  // ShapeRefLoadable::DoUnload would be enough, but this is not possible, as it is not declared yet
  friend class ShapeRefLoadable;

protected:
  // first list most accessed items

  // properties common to all LOD levels
  int _special; //!< some special flags must be applied to whole object (NoFarClip)

  float _boundingSphere; //!< radius of bounding sphere
  float _geometrySphere; //!< radius of bounding sphere around geometry level

  int _remarks; //!< some remarks - e.g. reversed 
  int _andHints,_orHints; //!< and/or of all LOD level hints

  VBType _optimizeWanted; //!< OptimizeRendering was done on this shape
  bool _streaming; //!< can be streamed

  Vector3 _aimingCenter; //!< point at which AI/missiles should aim
  PackedColor _color; //!< shape average color
  PackedColor _colorTop; //!< shape average color when viewed from above
  /// logarithm of average optical density of the shape
  /** used for volumetric visibility attenuation */
  float _viewDensity;

  float _resolutions[MAX_LOD_LEVELS]; //!< resolutions of all levels
  
  //! all LOD and special levels
  RefShapeRef _lods[MAX_LOD_LEVELS];

  //! minmax box of all levels
  Vector3 _minMax[2];

  //! minmax box of all visual levels
  Vector3 _minMaxVisual[2];
  
  // we approximate it as (_min+_max)/2
  //! bounding sphere center of original shape
  /*!
  LODShape is usually adjusted so that bounding sphere is centered around (0,0,0).
  _boundingCenter is offset that is applied during adjusting.
  Therefore -_boundingCenter center is original (0,0,0) positioned in new coordinate system.
  */
  Vector3 _boundingCenter;
  Vector3 _boundingCenterGShrinkP3D;

  //! boundingCenter value as stored in the source file (used for streaming)
  Vector3 _boundingCenterOnLoad;
  //! bounding sphere or geometry center
  Vector3 _geometryCenter;

  //! center of mass
  Vector3 _centerOfMass;
  //! matrix of inverse inertia tensor
  Matrix3 _invInertia;
  //! matrix of inertia tensor
  Matrix3 _inertia;

  mutable SRef<CoverModel> _coverModel;
  
  /// max. factor to which some finer lod is know to have smaller area than a coarser one
  mutable float _lodsAreaFactor;
  
  //!{ TI properties
  // they get overwritten if there are config entries
public:
  float _htMin;
  float _htMax;
  float _afMax;
  float _mfMax;
  float _mFact;
  float _tBody;

protected:
  //!}
  
  signed char _nLods; //!< number of all (LOD+special) levels 
  signed char _memory; //!< index of memory level
  signed char _geometry; //!< index of geometry level
  signed char _geometryFire; //!< index of fire geometry level
  signed char _geometryView; //!< index of view geometry level

  signed char _geometryViewPilot; //!< index of pilot view geometry level
  signed char _geometryViewGunner; //!< index of gunner view geometry level
  signed char _geometryViewCargo; //!< index of cargo view geometry level

  signed char _shadowVolumeCargo;
  signed char _shadowVolumePilot;
  signed char _shadowVolumeGunner;

  signed char _landContact; //!< index of land contact level
  signed char _roadway; //!< index of roadway level
  signed char _paths; //!< index of paths level
  signed char _hitpoints; //!< index of hitpoints level
  signed char _shadowVolume; //!< index of shadow volume level
  signed char _shadowVolumeCount; //!< Number of shadow volume levels
  signed char _shadowBuffer;  //!< Index of first shadow buffer level
  signed char _shadowBufferCount; //!< Number of shadow buffer levels
  signed char _wreck; //!< index of wreck level

  signed char _minShadow; //!< first LOD that can be used for shadowing
  /// enable projected shadows - always true for objects with no volume shadow
  bool _projShadow;
  signed char _nGraphical; //!< number of normal (graphical) lods
  
  bool _autoCenter; //!< allow BoundingCenter!=(0,0,0)  
  bool _lockAutoCenter; //!< disable any future changes
  bool _canOcclude; //!< this shape can be used for occlusion culling
  bool _canBeOccluded; //!< this shape should be tested against occlusions

  //bool _canUsePushBuffer;

  //! Flag to determine the model is forced to be non-alpha (drawn in pass1)
  bool _forceNotAlphaModel;

  //! Flag to determine where the geometry designed for shadow buffer rendering purposes comes from
  SBSource _sbSource;
  //! Flag to determine whether shadow volume should be preferred prior shadow buffer solution for this model
  bool _preferShadowVolume;
  
  /// offset used to prevent shadow acne when rendering shadow buffers
  float _shadowOffset;

  //! shape is animated - any cache may need to refreshed when drawing
  //bool _allowAnimation;
  EAnimationType _animationType;
  //! List of bones used by all LODs together
  Ref<SkeletonSource> _skeletonSource;
  //! Skeleton associated with the LODShape
  Ref<Skeleton> _skeleton;
  InitVal<int,0> _skeletonAssignCount;
  /// animations on the skeleton
  SRef<AnimationHolder> _animations;

  SRef<TreeCrownInfo> _treeCrown;

  //! Flag to determine the model can blend between LODs (that it contains at least one section
  //! with blending pixel shader)
  bool _canBlend;

  // often needed property values
  RStringB _propertyClass; //!< value of property "class"
  RStringB _propertyDamage; //!< value of property "dammage"
  bool _propertyFrequent; //!< value of property "frequent"

  SizedEnum<MapType,char> _mapType; //!< map symbol used to draw this shape 

  //! array of mass assigned to all points of geometry level
  //! this is used to calculate angular inertia tensor (_invInertia)
  AutoArray<float> _massArray;

  //! list of convex components from geometry level
  mutable Ref<ConvexComponents> _geomComponents;
  //! list of convex components from view geometry level
  mutable Ref<ConvexComponents> _viewComponents;
  //! list of convex components from fire geometry level
  mutable Ref<ConvexComponents> _fireComponents;
  
  /// list of all roadway faces
  mutable RefR<RoadwayFaces> _roadwayFaces;
  
  
  /// outline to be used for horizon pruning
  FindArray<OcclusionEdge> _horizonEdges;
#ifdef GEOM_OCCLUDERS
	Vector3 _horizonBounds[2];
#endif
  /// view pilot and similar geometries are currently not used at all - no need to process them
#define VIEW_INTERNAL_GEOMETRIES 1
#if VIEW_INTERNAL_GEOMETRIES
  //! list of convex components from pilot view geometry level
  mutable Ref<ConvexComponents> _viewPilotComponents;
  //! list of convex components from gunner view geometry level
  mutable Ref<ConvexComponents> _viewGunnerComponents;
  //! list of convex components from cargo view geometry level
  mutable Ref<ConvexComponents> _viewCargoComponents;
#endif

  /// handlers to handle loading/unloading
  FindArrayKey< InitPtr<LODShapeLoadHandler> > _loadHandler;

#if _ENABLE_REPORT
    /// model config may disable some selections
    FindArrayRStringBCI _unusedSelectionNames;
    /// report used selections for each shape
    mutable FindArrayRStringBCI _usedSelectionNames;
    /// while loading non-binarized, we allow any selection access
    bool _selectionsOpen;
#endif

  //! initialize given component list from given level
  /*** create component list if necessary, use #InitConvexComponent to do real work */
  void InitCC( Ref<ConvexComponents> &cc, Shape *shape, bool fullReport );
  
  void DoBuildRoadwayFaces() const;

  //! source file name
  RStringB _name;
  // physical body constants
  float _mass; //!< total mass
  float _invMass; //!< total mass inverse

  float _armor; //!< armor (dammage resistance) value
  float _invArmor; //!< inverse armor value
  float _logArmor; //!< logarithm of armor value

  /// serialize headers (all but shapes)
  void SerializeBinHeaders(SerializeBinStream &f);
  
public:
  __forceinline PackedColor Color() const {return _color;} //!< access to #_color
  __forceinline PackedColor ColorTop() const {return _colorTop;} //!< access to #_

  __forceinline float Mass() const {return _mass;} //!< access to #_mass
  __forceinline float InvMass() const {return _invMass;} //!< access to #_invMass
  __forceinline float Armor() const {return _armor;} //!< access to #_armor
  __forceinline float InvArmor() const {return _invArmor;} //!< access to #_invArmor
  __forceinline float LogArmor() const {return _logArmor;} //!< access to #_logArmor

  __forceinline bool CanOcclude() const {return _canOcclude;} //!< access to #_canOcclude
  __forceinline bool CanBeOccluded() const {return _canBeOccluded;} //!< access to #_canBeOccluded

  //__forceinline bool CanUsePushBuffer() const {return _canUsePushBuffer;}

  __forceinline float ViewDensity() const {return _viewDensity;} //!< access to #_viewDensity
  Texture *FindTexture(const char *name) const;

  void SetCanOcclude(bool val) {_canOcclude = val;} //!< access to #_canOcclude
  void SetCanBeOccluded(bool val) {_canBeOccluded = val;} //!< access to #_canBeOccluded

  /** obsolete - was used on Xbox */
  void SetCanUsePushBuffer(bool val) {/*_canUsePushBuffer=val;*/}
  void DisablePushBuffer();

  Matrix3 Inertia() const {return _inertia;} //!< access to #_inertia
  Matrix3 InvInertia() const {return _invInertia;} //!< access to #_invInertia
  Vector3Val CenterOfMass() const {return _centerOfMass;} //!< access to #_centerOfMass

  //!{ Access to TI properties // _VBS3_TI
  float HTMin() const {return _htMin;}
  float HTMax() const {return _htMax;}
  float AFMax() const {return _afMax;}
  float MFMax() const {return _mfMax;}
  float MFact() const {return _mFact;}
  float TBody() const {return _tBody;}
  void SetTIProperties(float htMin, float htMax, float afMax, float mfMax, float mFact, float tBody)
  {
    _htMin = htMin;
    _htMax = htMax;
    _afMax = afMax;
    _mfMax = mfMax;
    _mFact = mFact;
    _tBody = tBody;
  }
  //!}

  //! access to #_geomComponents
  const ConvexComponents &GetGeomComponents() const {return *_geomComponents;}
  //! access to #_viewComponents
  const ConvexComponents &GetViewComponents() const {return *_viewComponents;}
  //! access to #_fireComponents
  const ConvexComponents &GetFireComponents() const {return *_fireComponents;}
  
  /// access to outline for horizon pruning
  const AutoArray<OcclusionEdge> &HorizonEdges() const {return _horizonEdges;}
#ifdef GEOM_OCCLUDERS
	const Vector3* HorizonMinMax() const { return _horizonBounds;}
#endif

  #if VIEW_INTERNAL_GEOMETRIES
  //! access to #_viewPilotComponents
  const ConvexComponents *GetViewPilotComponents() const {return _viewPilotComponents;}
  //! access to #_viewGunnerComponents
  const ConvexComponents *GetViewGunnerComponents() const {return _viewGunnerComponents;}
  //! access to #_viewCargoComponents
  const ConvexComponents *GetViewCargoComponents() const {return _viewCargoComponents;}
  #endif

  void FindHitComponents(FindArray<int> &hits, const char *name) const;

  ConvexComponents *GetConvexComponents(int level) const;
  RStringB GetSkeletonSourceName() const;
  bool IsSkeletonSourceDiscrete() const;

protected:
  void DoClear();
  void DoConstruct();
  void DoConstruct(const LODShape &src, bool copyAnimations, bool copyLods, bool copySelections, bool copyST);
  void DoDestruct();

  friend void BinarizeFolder(BinarizeFolderContext &ctx, const char *sdir, const char *tdir);
  // normal p3d file handling
  //! load from named file
  bool Load(const char *name, bool reversed, const ModelTarget &mt = ModelTarget::_defaultValue,const char * forceName = NULL);
private:
  //! Shape customization after loading
  void CustomizeShape(Shape *shape, int lod, float resolution, const ModelConfig *modelConfig, const ModelTarget &mt, GeometryOnly geometryOnly) const;
  //! load from stream
  /*!
    Note there's a bug inside. We cannot use this method outside this class, because
    filename doesn't exist.
  */
  bool Load(QIStream &f, const char *name, bool reversed, const ModelTarget &mt = ModelTarget::_defaultValue, SkeletonSource *ss = NULL);

  void AutodetectShadowOffsetIfNeeded();
  void ReadShadowOffsetFromProperty(float &shadowOffset);
protected:

  //! prepare properties from config file
  void PrepareProperties(ParamEntryPar cfg);
  
  // fast file handling
  void Reverse();
  //! load from / save to optimized binary file
  //!\return true if loaded OK
  void SerializeBin(SerializeBinStream &f);

  /// Extend components to reach the ground whenever possible
  void ExtendToGround(Shape *view, ConvexComponents &cc);
  
  /// Initialize vertically convex outline needed for horizon pruning
  void InitOcclusion(Shape *view, const ConvexComponents &cc);
  
  //! initialize all convex components (#_geomComponents etc.)
  void InitConvexComponents(bool fullReport);
  //! initialize single convex component set
  void InitConvexComponents( ConvexComponents &cc, Shape *geom, bool fullReport );
    
public:
#if _ENABLE_PHYSICS
  enf::PhysicsGeomDef* CreatePhysComponents(unsigned int& cnt, bool useView = false);
  //static unsigned int GetPhysShapeCnt();
#endif

  bool PreloadVBuffer(int level, float dist2, bool forceAnimated=false);

  /// request shadow volume
  bool PreloadShadow(float z2) const;
  
  /// background request
  static bool Preload(FileRequestPriority prior, const char *name);
  
  //! load from optimized binary file
  bool LoadOptimized(const char *name); // true if OK
  //! load from optimized binary stream
  bool LoadOptimized(QIStream &f); // true if OK

  //! save to optimized binary file
  void SaveOptimized(const char *name); // true if OK
  //! save to optimized binary stream
  void SaveOptimized(QOStream &f); // true if OK

  //! constructor - construct empty shape
  LODShape();
  //! copy shape (with or without key-frame animations)
  LODShape(const LODShape &src, bool copyAnimations=true, bool copyLods=true, bool copySelections=true, bool copyST=true);
  //! copy shape (with all key-frame animations)
  void operator=(const LODShape &src);
  //! destructor
  ~LODShape();

  //! construct by loading from stream
  LODShape(QIStream &f, const char *name, bool reversed=false, const ModelTarget &mt=ModelTarget::_defaultValue, SkeletonSource *ss=NULL);
  //! construct by loading from file
  LODShape(const char *name, bool reversed=false, const ModelTarget &mt=ModelTarget::_defaultValue);
  //! construct by loading from file, version for WRP binarize
  LODShape(const char *name, bool reversed, const ModelTarget &mt,const char * forceName);
  // special constructor constructor for binarize
  LODShape(const char *name, bool reversed, const char * forceName);

#if _DEBUG
  void OnUsed() const;
#endif
  void OnUnused() const;

#if _VBS3_CRATERS_DEFORM_TERRAIN
  //! Function that modifies the LODShape to be aligned with ground - should be called soon after construction (can also be part of the constructor)
  void AlignWithGround(const Landscape *landscape, const Matrix4 &transform, int bias);
#endif

  //! reload shape from file, name might change during this
  void Reload(QIStream &f, const char *name, bool reversed);

  /// memory used by the LODShape itself, not by its unloadable parts
  size_t GetMemoryControlled() const;
  
  double GetMemoryUsed() const;
  double GetMemoryUsedUnloadable(MemoryUnloadableContext &ctx) const;

  // access functions common to all LODs
  int Special() const {return _special;}
  void OrSpecial(int special);
  void AndSpecial(int special);
  void SetSpecial(int special);
  void RescanSpecial();
  /// set NoShadow part of the _special field
  void DisableShadow();

  //! as a result of some operations some lod level cannot be streamed, because it has to keep some internal state
  void DisableLevelStreaming(int level, bool enableGeometryStreaming=false);
  //! as a result of some operations shape the whole shape cannot be streamed, because it has to keep some internal state
  void DisableStreaming(bool enableGeometryStreaming=false);

  int Remarks() const {return _remarks;}
  void SetRemarks(int remarks) {_remarks = remarks;}

  void CalculateHints();
  void SetHints(ClipFlags orHints, ClipFlags andHints)
  {
    _orHints=orHints;
    _andHints=andHints;
  }
  ClipFlags GetOrHints() const {return _orHints;}
  ClipFlags GetAndHints() const {return _andHints;}

  void LockAutoCenter(bool autoCenter) {_lockAutoCenter = autoCenter;}
  void SetAutoCenter(bool autoCenter) {_autoCenter = autoCenter;}
  bool GetAutoCenter() const {return _autoCenter;}
  bool GetForceNotAlphaModel() const {return _forceNotAlphaModel;}
  SBSource GetSBSource() const {return _sbSource;}
  void SetAnimationType(EAnimationType animationType) {_animationType = animationType;}
  EAnimationType GetAnimationType() const {return _animationType;}
  
  /// check if section properties can be changed ("animated")

  Skeleton *GetSkeleton() const {return _skeleton;}
  const SkeletonSource *GetSkeletonSource() const {return _skeletonSource;}
  
  void SkeletonAssigned(Skeleton *skeleton);
  void SkeletonUnassigned(Skeleton *skeleton);
  
  //@{ skeleton source manipulation
  Skeleton *LoadSkeletonFromSource();
  SkeletonIndex FindBone(const RStringB &name) const;
  //@}
  /// load animations from the config file
  void LoadAnimations(ParamEntryPar anim);
  /// removed any animations unused by current shape
  void OptimizeAnimations();

  /// access to animations
  const AnimationHolder *GetAnimations() const {return _animations;}

  /// helper for PrepareShapeDrawMatrices implementation
  void InitDrawMatrices(Matrix4Array &matrices, int level) const;

  void OptimizeRenderingListed();
  void OptimizeLevelRendering(int level, bool forceAnimated=false);
  void OptimizeRendering(VBType type) {_optimizeWanted = type;}

  void RecreateVBuffer(int level);
  //void RecreateVBuffers();
  void ReleaseVBuffers(bool deferred=false);
  /// check if given level should use vertex buffer
  bool LevelUsesVBuffer(int level) const
  {
	  const RefShapeRef &shape = LevelRef(level);
	  return LevelUsesVBuffer(shape,level);
  }
  /// faster when shape is already know, esp. when it is not locked
  bool LevelUsesVBuffer(const RefShapeRef &shape, int level) const;

  /// check if point-vertex conversion is needed for given resolution
  static bool PointsNeeded(float resolution);
  /// check if given resolution lod needs to be permanent
  static bool IsPermanent(float resolution);

  void UndoOptimizeRendering();

  //@{ helper functions to simulate CalculateBoundingSphere after loading
  void ApplyBoundingCenterGeom(int level, Vector3Par changeBoundingCenter);
  void ApplyBoundingCenter(int level, Vector3Par changeBoundingCenter);
  //@}

  /// calculate bounding sphere center and radius
  void CalculateBoundingSphere(bool recalcRadius=true);
  /// calculate bounding sphere radius
  void CalculateBoundingSphereRadius();
  /// calculate min-max box for all lod levels
  void CalculateMinMax( bool recalcLevels=false );
  
  /// recompute min-max of all visual LODs currently loaded
  void CalculateMinMaxVisual();

  const RoadwayFaces &BuildRoadwayFaces() const
  {
    if (!_roadwayFaces) DoBuildRoadwayFaces();
    return *_roadwayFaces;
  }
  
  const CoverModel &GetCoverModel() const
  {
    if (!_coverModel)
    {
      _coverModel = new CoverModel(this);
    }
    return *_coverModel;
  }
  
  void CheckForcedProperties(ParamEntryPar modelConfig);
  void ScanProperties();
  void CopyProperties(const LODShape &src);
  void CalculateMass(); // always use best level
  void RescanLodsArea();

  void MakeShadow(LODShape *shadow, int level);
  LODShape *MakeShadow(int level);

  // properties
  Vector3Val Min() const {return _minMax[0];}
  Vector3Val Max() const {return _minMax[1];}
  const Vector3 *MinMax() const {return _minMax;}
  const Vector3 *MinMaxVisual() const {return _minMaxVisual;}
  float BoundingSphere() const {return _boundingSphere;}

  Vector3Val BoundingCenter() const {return _boundingCenter;}
  Vector3Val BoundingCenterOnLoad() const {return _boundingCenterOnLoad;}
  
  float GeometrySphere() const {return _geometrySphere;}
  Vector3Val GeometryCenter() const {return _geometryCenter;}
  Vector3Val AimingCenter() const {return _aimingCenter;}
  const char *Name() const {return _name;}
  const RStringB &GetName() const {return _name;}

  const RStringB &PropertyValue( const RStringB &name ) const;
  const RStringB &PropertyValue( const char *name ) const;
  MapType GetMapType() const {return _mapType.GetEnumValue();}

  //! Wrapper
  bool CanBlend() const {return _canBlend;}

  const RStringB &GetPropertyClass() const {return _propertyClass;}
  const RStringB &GetPropertyDammage() const {return _propertyDamage;}
  bool GetPropertyFrequent() const {return _propertyFrequent;}

  //! Transform all LODs (their vertex positions) according to specified matrix
  void InternalTransform(const Matrix4 &transform, bool recalculateMass = true);

  //! Returns true if any of the LODs is animated, false elsewhere
  bool IsAnimated();

  //void RegisterTexture( Texture *texture, const Animation &anim );

  //#define GET_LOD(x) (x)->GetRef()
  //#define G__Level(level) ( level>=0 ? _lods[level].GetRef() : NULL )

  #define GET_LOD(x) ((x).GetRef())
  #define G__Level(level) ( level>=0 ? GET_LOD(_lods[level]) : NULL )

  // LOD maintenance
  ShapeUsed Level(int level) const
  {
    Assert(level>=0 && level<NLevels());
    return _lods[level].GetShapeRef();
  }
  const RefShapeRef &LevelRef(int level) const
  {
    Assert(level>=0 && level<NLevels());
    return _lods[level];
  }
  void LockLevel(int level) const
  {
    Assert(level>=0 && level<NLevels());
    _lods[level].Lock();
  }
  void UnlockLevel(int level) const
  {
    Assert(level>=0 && level<NLevels());
    _lods[level].Unlock();
  }
  /// get given level, assuming it is locked (enable changes during initialization)
  __forceinline Shape *InitLevelLocked(int level)
  {
    Assert(level >= 0 && level < NLevels());
    return _lods[level].GetRef();
  }
  /// get given level, assuming it is locked
  __forceinline const Shape *GetLevelLocked(int level) const
  {
    Assert(level>=0 && level<NLevels());
    return _lods[level].GetRef();
  }
  
  /// check if given level is locked
  __forceinline bool IsLevelLocked(int level) const {return _lods[level].GetShapeRef()->IsLocked();}

  int NLevels() const {return _nLods;}
  float Resolution(int level) const {return _resolutions[level];}
  void SetResolution(int level, float res) {_resolutions[level] = res;}

  int GetMinShadowLevel() const {return _minShadow;}

  Shape *InitMemoryLevel() {return G__Level(_memory);}
  Shape *InitGeometryLevel() {return G__Level(_geometry);}
  Shape *InitFireGeometryLevel() {return G__Level(_geometryFire);}
  Shape *InitViewGeometryLevel() {return G__Level(_geometryView);}

  const Shape *MemoryLevel() const {return G__Level(_memory);}
  const Shape *GeometryLevel() const {return G__Level(_geometry);}
  const Shape *FireGeometryLevel() const {return G__Level(_geometryFire);}
  const Shape *ViewGeometryLevel() const {return G__Level(_geometryView);}

  Shape *InitViewPilotGeometryLevel() {return G__Level(_geometryViewPilot);}
  Shape *InitViewGunnerGeometryLevel() {return G__Level(_geometryViewGunner);}
  Shape *InitViewCargoGeometryLevel() {return G__Level(_geometryViewCargo);}

  const Shape *ViewPilotGeometryLevel() const {return G__Level(_geometryViewPilot);}
  const Shape *ViewGunnerGeometryLevel() const {return G__Level(_geometryViewGunner);}
  const Shape *ViewCargoGeometryLevel() const {return G__Level(_geometryViewCargo);}

  Shape *InitShadowVolumeCargoLevel() {return G__Level(_shadowVolumeCargo);}
  Shape *InitShadowVolumePilotLevel() {return G__Level(_shadowVolumePilot);}
  Shape *InitShadowVolumeGunnerLevel() {return G__Level(_shadowVolumeGunner);}

  const Shape *ShadowVolumeCargoLevel() {return G__Level(_shadowVolumeCargo);}
  const Shape *ShadowVolumePilotLevel() {return G__Level(_shadowVolumePilot);}
  const Shape *ShadowVolumeGunnerLevel() {return G__Level(_shadowVolumeGunner);}

  Shape *InitRoadwayLevel() {return G__Level(_roadway);}

  const Shape *LandContactLevel() const {return G__Level(_landContact);}
  const Shape *RoadwayLevel() const {return G__Level(_roadway);}
  const Shape *PathsLevel() const {return G__Level(_paths);}
  const Shape *HitpointsLevel() const {return G__Level(_hitpoints);}
  const Shape *ShadowVolumeLevel() const {return G__Level(_shadowVolume);}
  Shape *WreckLevel() const {return G__Level(_wreck);}

  int FindMemoryLevel() const {return _memory;}
  int FindGeometryLevel() const {return _geometry;}
  int FindFireGeometryLevel() const {return _geometryFire;}
  int FindViewGeometryLevel() const {return _geometryView;}

  int FindViewPilotGeometryLevel() const {return _geometryViewPilot;}
  int FindViewGunnerGeometryLevel() const {return _geometryViewGunner;}
  int FindViewCargoGeometryLevel() const {return _geometryViewCargo;}

  // Use of _shadowVolumeCargo is intentional - not bug.
  int FindShadowVolumeCargo() const {return _shadowVolumeCargo;}
  int FindShadowVolumePilot() const {return _shadowVolumeCargo;}
  int FindShadowVolumeGunner() const {return _shadowVolumeCargo;}

  int FindLandContactLevel() const {return _landContact;}
  int FindRoadwayLevel() const {return _roadway;}
  int FindPaths() const {return _paths;}
  int FindHitpoints() const {return _hitpoints;}
  int FindShadowVolume() const {return _shadowVolume;}
  int FindShadowVolumeCount() const {return _shadowVolumeCount;}
  int FindShadowBuffer() const {return _shadowBuffer;}
  int FindShadowBufferCount() const {return _shadowBufferCount;}
	#ifdef TEST_RAIN_MAP
		bool DrawRainMap(int passNum) const;
	#endif
  /// check if shadow buffer shadow should be rendered
  bool DrawProjShadow(int passNum) const;
  /// check if shadow volume should be rendered
  bool DrawVolumeShadow(int passNum) const;
  /// get offset needed to prevent shadow acne (implements EngineShapeProperties)
  float GetShadowOffset() const {return _shadowOffset;}
  
  int FindWreckLevel() const {return _wreck;}
  /// find the graphical LOD with minimum faces
  int FindSimplestLevel() const {return _nGraphical - 1;}
  
  bool IsShadowVolume(int level) const;
  
#if _ENABLE_REPORT
    int FindNamedSel(const Shape *shape, const char *name, const char *altName) const;
    int FindNamedSel(const Shape *shape, const char *name) const;
    bool CheckSelectionsOpen() const {return _selectionsOpen;}
    bool ReportSelectionContentUsed(const RStringB &name) const {return _usedSelectionNames.AddUnique(name) >= 0;}
#else
    __forceinline int FindNamedSel(const Shape *shape, const char *name, const char *altName) const
    {
      return shape->FindNamedSel(name,altName);
    }
    __forceinline int FindNamedSel(const Shape *shape, const char *name) const
    {
      return shape->FindNamedSel(name);
    }
    bool CheckSelectionsOpen() const {return true;}
    bool ReportSelectionContentUsed(const RStringB &name) const {return false;}
#endif
  Vector3 NamedPoint(int level, const char *name, const char *altName=NULL) const;
  int PointIndex(const Shape *shape, const char *name) const;
  Vector3 MemoryPoint(const char *name, const char *altName=NULL) const;
  bool MemoryPointExists(const char *name) const;

  //! Function searches for a LOD with the lower or the closest resolution. BE WARE! For special LODs it is often better to use function FindSpecLevel.
  int FindLevel(float resolution, bool noDecal=false) const;

  //! Function searches for a LOD with the similar resolution (maximal allowed difference 10%). 
  int FindSpecLevel(float resolution) const;

  //! Function finds LODs within specified interval
  signed char FindSpecLevelInterval(signed char &itemsCount, float spec, float specCount) const;

  //! Function checks if resolution of LOD matches given resolution. (Must be within 10% boundary).    
  bool IsSpecLevel(int level, float resolution) const;

  //@{ add/remove/execute load handler to be notified of the unloading
  bool CheckLoadHandler(const LODShapeLoadHandler *handler) const;
  void AddLoadHandler(LODShapeLoadHandler *handler);
  void RemoveLoadHandler(LODShapeLoadHandler *handler);
  void ShapeLoaded(int level);
  void ShapeUnloaded(int level);
  //@}
  
  /// calculate level complexity
  int Complexity(int level) const {return _lods[level].NFaces();}
  float ShaderComplexity(int level, float screenArea) const;

  /// find level with a given complexity
  int FindLevelWithComplexity(float coveredArea, float complexity, float maxDif) const
  {
    return FindLevelWithComplexity(coveredArea, complexity, maxDif, 0, _nGraphical);
  }
  int FindLevelWithDensity(float dist2, float scale, float density) const;
  int FindShadowLevelWithComplexity(float complexity, float maxDif) const;
  int FindShadowVolumeLevelWithComplexity(float complexity, float maxDif) const;
  int FindShadowLevelWithDensity(float dist2, float scale, float density) const;
  int FindShadowVolumeLevelWithDensity(float dist2, float scale, float density) const;

  void FindResolutionInterval(int &start, int &end, float minRes, float maxRes) const;
  //! Function returns LOD index with desired complexity with maximum specified difference
  /*!
    If no such LOD with specified difference is found, then either first of last LOD is returned.
    \param complexity Desired complexity
    \param maxDif Maximum required difference (if no such difference is found then either start or end-1 LOD is being returned)
    \param start First LOD to select from
    \param end LOD after last LOD to select from
  */
  int FindLevelWithComplexity(float coveredArea, float complexity, float maxDif, int start, int end) const;
  //! Function to return blending factor between this and next level. Factor lies within range <0, 1>
  float GetBlendingFactor(int level, float complexity) const;

  /// helper function for area estimation
  float EstimateArea(float scale) const;
  /// calculate on-screen area
  float CoveredArea(const Camera &cam, float dist2, float oScale) const;


  /// check if the level is loaded for purpose of CPU shadow generation, if not, request it
  bool CheckLevelLoadedAsShadow(int level, bool request) const;

  /// check if the level is loaded, if not, request it
  bool CheckLevelLoaded(int level, bool request) const;
  
  /// debugging variant of CheckLevelLoadedQuick
  int CheckLevelLoadedDegree(int level) const;
  /// check if the level is loaded and ready, if not, do nothing
  bool CheckLevelLoadedQuick(int level) const;
  /// request level loading, and use the level which is currently available
  int UseLevel(int level, bool needSomeData) const;
  /// request level loading, and use the level which is currently available
  /** Consider textures as well */
  int UseLevel(int level, float dist2, bool needSomeData, int previousLevel) const;

  /// find loaded level, never load anything
  /* MT safe */
  int UseLevelQuick(int level) const;

  /// geometry of the level will be used for shadow generation, vbuffer is not needed
  int UseLevelAsShadow(int level, bool needSomeData) const;
  /// geometry of the level will be used for shadow generation, vbuffer is not needed
  int UseLevelAsSBShadow(int level, bool needSomeData) const;

  /// helper for various UseLevel functions
  int UseLevelInRange(int level, bool needSomeData, int beg, int end) const;

  /// helper for various UseLevel functions - includes textures for given distance
  int UseLevelInRange(int level, bool needSomeData, int beg, int end, float dist2, int previousLevel) const;
  
  /// check if the level is normal graphical LOD (no view pilot, no special)
  bool IsNormalGraphicalLod(int level) const {return level<_nGraphical || level==_wreck;}

  /// request shadow level loading, and use the level which is currently available
  int UseShadowLevel(int level) const;
  
  // debugging description of given lod level
  const char *ShapeName(int level ) const;
  /// get user friendly level name  
  static const char *LevelName(float resolution);
  const char *LevelName(const Shape *level) const;
  bool IsCockpit(int level) const;

  //! Check if point is inside the shape geometry
  bool IsInside(Vector3Par pos);

  //int FindGeometryLevel() const;
  //int FindLandContactLevel() const;

  int AddShape(Shape *shape, float resolution);
  void ChangeShape(int level, Shape *shape);

  void Optimize(); // sort by texture/render state

  void ScanResolutions();
  void ScanShapes();
  void OptimizeShapes();
  void ScanProjShadow();
  //@{ removed selections needed only during binarization
  void RemoveSelections(const FindArrayRStringBCI &names);
  void RemoveVertexSelections(const FindArrayRStringBCI &names);
  void RemoveVertexSelectionsComplement(const FindArrayRStringBCI &names);
  //@}

  /// we need to adjust all references to selections once we remove some
  void OnSelectionRemoved(int level, int sel);
  
  /// tree crown specific parameters
  void GetTreeCrown(Vector3 *minMax) const;

private:
  /// prepare information for GetTreeCrown 
  void CalculateTreeCrown();
};

typedef int ShapeParameters;

#include <Es/Containers/bankArray.hpp>

template<>
struct BankTraits<LODShape>: public DefLLinkBankTraits<LODShape>
{
  // default name is character
  struct ShapeBankName
  {
    RStringB _name;
    ShapeParameters _pars;
    ShapeBankName(const RStringB &name, ShapeParameters pars)
    :_name(name),_pars(pars)
    {}   
  };
  typedef ShapeBankName NameType;
  // default name comparison
  static int CompareNames(const NameType &n1, const NameType &n2)
  {
    return n1._name!=n2._name || n1._pars!=n2._pars;
  }
  static NameType GetName(const Type *item)
  {
    // NULL items may exists - Link might have been discarded
    if (item==NULL)
    {
      Fail("Null shape in the hash table");
      return ShapeBankName(RStringB(),0);
    }
    return ShapeBankName(item->GetName(),item->Remarks());
  }
  static Type *Create(NameType name) {return new Type(name._name);}

  struct LODShapeHashTraits: public DefMapClassTraits< LLink<LODShape> >
  {
    //! key type
    typedef NameType KeyType;
    //! calculate hash value
    static unsigned int CalculateHashValue(const KeyType &key)
    {
      return CalculateStringHashValue(key._name);
    }

    //! compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
    static int CmpKey(const KeyType &k1, const KeyType &k2)
    {
      return CompareNames(k1,k2);
    }
    static KeyType GetKey(const LODShape *item)
    {
      return GetName(item);
    }
  };
  /// replace default container
  typedef MapStringToClass<
    LLink<LODShape>, LLinkArray<LODShape>,
    LODShapeHashTraits
  > ContainerType;
};

#include <El/FreeOnDemand/memFreeReq.hpp>


enum
{
  ShapeNormal = 0,
  ShapeReversed = 1,
  ShapeShadow = 2,
  /// shape explicitly animated
  ShapeAnimated = 4,
  ShapeSeparateShadowFaces =  8,
  /// shape is explicitly not animated (override auto-detection)
  ShapeNotAnimated = 16,
  ShapeOnSurface = 32,
  ShapeModified = 64,
};

MemHandlerInstance(Geom)
MemHandlerInstance(Shape)
MemHandlerInstance(LODShape)

/// manager of shapes
class ShapeBank: public BankArray<LODShape>, public MHInstanceGeom, public MHInstanceShape, public MHInstanceLODShape
{
  typedef BankArray<LODShape> base;

public:  
  typedef BankArrayTraits::ShapeBankName ShapeBankName;
  typedef BankArrayTraits::ContainerType ContainerType;
  
private:
  /// list of shapes that are loaded, but not currently used
  FramedMemoryControlledList<ShapeRefManaged> _cachedShapes;

  /// list of all shapes which have geometry loaded, but it can be released
  FramedMemoryControlledList<Shape> _geometryLoaded;

  FramedMemoryControlledList<LODShape> _lodShapeCache;
    
  /// check if there is any previous instance to be reused
  LODShape *FindLoaded(const RStringB &name, ShapeParameters pars, bool refresh);
  
public:
  //! constructor
  ShapeBank();
  //! destructor
  ~ShapeBank();
  
  //! load a new shape or reuse previous instance
  LODShape *New(const char *name, ShapeParameters pars, const char * forceName = NULL);

  /// get a shape  which is already loaded (for purpose of various tests)
  LODShape *GetLoaded(const char *name, ShapeParameters pars);
  
  /// preload request
  bool Preload(FileRequestPriority prior, const char *name, ShapeParameters pars, Ref<LODShape> &shape);
  //! load a new shape or reuse previous instance
  LODShape *New(const char *name, bool reversed, bool shadow, bool modified=false)
  {
    ShapeParameters pars = ShapeNormal;
    if (reversed) pars |= ShapeReversed;
    if (shadow) pars |= ShapeShadow;
    if (modified) pars |= ShapeModified;
    return New(name,pars);
  }
  //! get memory used by all shapes
  double GetMemoryUsedByShapes();
  //! optimize all registered shapes for rendering
  void OptimizeAll();
  //! release all registered vertex buffers
  void ReleaseAllVBuffers();
  //! when shape is used, it needs to be removed from the cache
  void ShapeLoaded(ShapeRefManaged *shapeRef);
  //! when shape is no longer used, it can report it before releasing
  void ShapeUnloaded(ShapeRefManaged *shapeRef);
  //! when managed shape is destructed or stops being managed, it must report it to us
  void ShapeReleased(ShapeRefManaged *shapeRef);
  //! when managed shape is destructed or stops, it must report it to us
  void ShapeGeometryReleased(Shape *shape);
  //! Shows space occupied by last LOD's (which are always loaded)
  //void ShowLastLODsSpace();

protected:  
  /// helper for FreeOneItemShape - may be called separately
  size_t FreeOneItemShape(ShapeRefManaged *shapeUnload);
  
public:
  size_t FreeOneItemShape(LODShape *shape, int level);
  //@{{ Shape implementation of MemoryFreeOnDemandHelper interface
  virtual size_t FreeOneItemShape();
  virtual float PriorityShape() const;
  virtual size_t MemoryControlledShape() const;
  virtual size_t MemoryControlledRecentShape() const;
  virtual void MemoryControlledFrameShape();
  virtual RString GetDebugNameShape() const;
  //@}

  //@{{ Geometry  implementation of MemoryFreeOnDemandHelper interface
  virtual float PriorityGeom() const;
  virtual size_t FreeOneItemGeom();
  virtual size_t MemoryControlledGeom() const;
  virtual size_t MemoryControlledRecentGeom() const;
  virtual void MemoryControlledFrameGeom();
  virtual RString GetDebugNameGeom() const;
  //@}
  
  //@{{ LODShape implementation of MemoryFreeOnDemandHelper interface
  virtual size_t FreeOneItemLODShape();
  virtual float PriorityLODShape() const;
  virtual size_t MemoryControlledLODShape() const;
  virtual size_t MemoryControlledRecentLODShape() const;
  virtual void MemoryControlledFrameLODShape();
  virtual RString GetDebugNameLODShape() const;
  //@}
  /// helper for LODShape::OnUnused
  void OnUnusedLODShape(LODShape *shape);

  /// unload geometry from a single shape
  void UnloadGeometry(Shape *shape);
  /// shape requires its geometry to be loaded - need to be paired with UnloadGeometry
  void AddRefGeometry(const Shape *shape, const ShapeRef *shapeRef);
  /// shape requires its geometry to be unloaded - need to be paired with LoadGeometry
  void ReleaseGeometry(const Shape *shape);
  
  void ClearCache();
  /// debugging - check integrity
  bool CheckIntegrity() const;
};

extern ShapeBank Shapes;

/// force geometry to be loaded during lifetime of this object
template <ShapeBank *bank=&Shapes>
class ShapeGeometryLock
{
  const Shape *_shape;
  
  void DoLock(const Shape *shape, const ShapeRef *shapeRef)
  {
    if (shape && shape->GeometryCanBeUnloaded())
    {
      _shape = shape;
      bank->AddRefGeometry(_shape,shapeRef);
    }
    else
    {
      _shape = NULL;
      Assert(shape && shape->IsGeometryLoaded());
    }
  }
  void DoUnlock()
  {
    if (_shape)
    {
      bank->ReleaseGeometry(_shape);
      _shape = NULL;
    }
  }
  
public:
  ShapeGeometryLock(){_shape = NULL;}
  __forceinline explicit ShapeGeometryLock(const LODShape *shape, int level){DoLock(shape->GetLevelLocked(level),shape->LevelRef(level));}
  __forceinline explicit ShapeGeometryLock(const Shape *shape, const ShapeRef *shapeRef){DoLock(shape,shapeRef);}
  __forceinline ~ShapeGeometryLock() {DoUnlock();}
  
  __forceinline void Lock(const LODShape *shape, int level)
  {
    DoUnlock();
    DoLock(shape->GetLevelLocked(level),shape->LevelRef(level));
  }
  
private:
  ShapeGeometryLock(const ShapeGeometryLock &src);
  void operator =(const ShapeGeometryLock &src);
};

/// combine both ShapeUsed and ShapeGeometryLock in one object
template <ShapeBank *bank=&Shapes>
class ShapeUsedGeometryLock: public ShapeUsed
{
  const ShapeRef *_ref;
  ShapeGeometryLock<bank> _lock;
  
public:
  __forceinline ShapeUsedGeometryLock():ShapeUsed(),_ref(NULL),_lock(){}
  __forceinline ShapeUsedGeometryLock(const LODShape *lShape, int level)
  :ShapeUsed(lShape->LevelRef(level)),_ref(lShape->LevelRef(level)),_lock(_ref->GetRef(),_ref)
  {
  }
  __forceinline ShapeUsedGeometryLock(const ShapeRef *ref)
  :ShapeUsed(ref),_ref(ref),_lock(_ref->GetRef(),_ref)
  {
  }
  ShapeUsedGeometryLock(const ShapeUsedGeometryLock &src) 
  :ShapeUsed(src.InitShape()), _ref(src._ref),_lock(_ref->GetRef(),_ref)
  {
  }
};

#if _ENABLE_CHEATS && _ENABLE_PERFLOG
  #define DPRIM_STATS 1
#else
  #define DPRIM_STATS 0
#endif

#if DPRIM_STATS
  #if _DEBUG || _PROFILE
    #define DPRIM_VERIFY_SINGLE_SCOPE 1
  #endif
  #include <El/Common/perfLog.hpp>
  
	void DPrimStatsCount(const char *name, int value);
	void TrisStatsCount(const char *name, int value);
  void ObjsStatsCount(const char *name, int value);
  void PixelStatsCount(const char *name, int value);
	
  /// function type for StatScope template
  typedef void StatScopeFunction(const char *name, int value);
  
  /// scope handling for DPrimStats
  template <StatScopeFunction *count, int column>
  class DrawStatScope: private NoCopy
  {
    const char *_name;
	  PerfCounter &_src;
	  int _startValue;
	  #if DPRIM_VERIFY_SINGLE_SCOPE
	    static int _instances;
	  #endif
  	
	  public:
	  DrawStatScope(const char *name, PerfCounter &src)
	  :_name(name),_src(src),_startValue(src.GetValue())
	  {
  	  #if DPRIM_VERIFY_SINGLE_SCOPE
	    if (*_name)
	    {
	      DoAssert(_instances==0);
	      _instances++;
	    }
	    #endif
	  }
	  ~DrawStatScope()
	  {
	    if (*_name)
	    {
      	#if DPRIM_VERIFY_SINGLE_SCOPE
	        _instances--;
	      #endif
	      if (GEngine->ShowFpsMode()==column)
	      {
	        count(_name,_src.GetValue()-_startValue);
	      }
	    }
	  }
	  /// check if current stats require this scope
	  static bool Enabled() {return GEngine->ShowFpsMode() == column;}
  };

  #if DPRIM_VERIFY_SINGLE_SCOPE
    template <StatScopeFunction *count, int column>
    int DrawStatScope<count,column>::_instances = 0;
	#endif
  
  class DPrimStatScope: public DrawStatScope<DPrimStatsCount,FpsDPrim>
  {
    public:
	  DPrimStatScope(const char *name, PerfCounter &src)
	  :DrawStatScope<DPrimStatsCount,FpsDPrim>(name,src) {}
  };

  class TrisStatScope: public DrawStatScope<TrisStatsCount,FpsTris>
  {
    public:
	  TrisStatScope(const char *name, PerfCounter &src)
	  :DrawStatScope<TrisStatsCount,FpsTris>(name,src){}
  };

  class ObjsStatScope: public DrawStatScope<ObjsStatsCount,FpsObjs>
  {
    public:
	  ObjsStatScope(const char *name, PerfCounter &src)
	  :DrawStatScope<ObjsStatsCount,FpsObjs>(name,src){}
  };

  /// if necessary, count dPrim + tris + objs information into a shape name slot
  #define DPRIM_STAT_SCOPE(name,scopeName) \
    ADD_COUNTER_SCOPE(d##scopeName,dPrim); \
    ADD_COUNTER_SCOPE(t##scopeName,tris); \
    ADD_COUNTER_SCOPE(o##scopeName,obj); \
    const char *dPrimStat__name##scopeName = ""; \
    if (DPrimStatScope::Enabled() || TrisStatScope::Enabled() || ObjsStatScope::Enabled()) dPrimStat__name##scopeName = name; \
    DPrimStatScope dPrimStat__##scopeName(dPrimStat__name##scopeName,COUNTER_NAME(dPrim)); \
    TrisStatScope trisStat__##scopeName(dPrimStat__name##scopeName,COUNTER_NAME(tris)); \
    ObjsStatScope objsStat__##scopeName(dPrimStat__name##scopeName,COUNTER_NAME(obj)); \
  
  // if necessary, count dPrim + tris information into a scope name slot
  #define DPRIM_STAT_SCOPE_SIMPLE(scopeName) \
    DPRIM_STAT_SCOPE(#scopeName,scopeName)
  
#else
  #define DPRIM_STAT_SCOPE(name,scopeName) \
    ADD_COUNTER_SCOPE(scopeName,dPrim)
  #define DPRIM_STAT_SCOPE_SIMPLE(scopeName) \
    ADD_COUNTER_SCOPE(scopeName,dPrim)
#endif

#endif

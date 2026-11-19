#ifdef _MSC_VER
#pragma once
#endif

#ifndef _VERTEX_HPP
#define _VERTEX_HPP

// untransformed and transformed vertices

#include "../wpch.hpp"
#include "../types.hpp"
#include <El/Math/math3d.hpp>
#include <El/Color/colors.hpp>
#include <El/QStream/qStream.hpp>
#include <Es/Types/pointers.hpp>
#include <Es/Containers/staticArray.hpp>
#include <El/Multicore/multicore.hpp>

#include <Es/Containers/condArray.hpp>
class SerializeBinStream;

//! material for T&L module
struct TLMaterial
{
	Color emmisive;
	Color ambient;
	Color diffuse;
	Color specular;
	Color forcedDiffuse; // diffuse to ambient transfer - used from special lighting - half lighted etc.
	int specFlags; // fog mode and some more special flags
	int specularPower;

	// TODO: check which special flags should be used here
	TLMaterial();
	void Init();
	bool IsEqual(const TLMaterial &src) const;
	bool operator==(const TLMaterial &src) const {return IsEqual(src);}
	bool operator!=(const TLMaterial &src) const {return !IsEqual(src);}

	/// default (white ambient+diffuse, no emmisive) material
	static TLMaterial _default;
	/// white emmisive
	static TLMaterial _shining;
	/// white emmisive + ambient + diffuse, simulates automatic adjusting
	static TLMaterial _shiningAdjustable;
};

void CreateMaterial(TLMaterial &mat, ColorVal col);

// hints: different handling of object/surface relations

// check transformed point for all six clipping planes
ClipFlags NeedsClipping(Vector3Par point, const Camera &camera);

/// representation of Vector3 using 10 bits for each component
class Vector3Compressed
{
protected:
  DWORD _value;

public:
  Vector3Compressed(){Set(VZero);}
  explicit Vector3Compressed(Vector3Par value){Set(value);}

  bool operator ==(Vector3Compressed cmp) const
  {
    return cmp._value == _value;
  }
  bool operator !=(Vector3Compressed cmp) const
  {
    return cmp._value != _value;
  }

  void Set(Vector3Par value);
  const Vector3 Get() const;

  DWORD GetRaw() const
  {
    return _value;
  }
  void SetRaw(DWORD val)
  {
    _value = val;
  }
  
  static Vector3Compressed Up;
  static Vector3Compressed ZeroV;
};
TypeIsSimple(Vector3Compressed);

//! Texture coordinates
struct UVPair
{
	float u,v;
	UVPair(){}
	UVPair(float uu, float vv)
  {
    u = uu;
    v = vv;
  }
  bool operator==(const UVPair &src) const {return (src.u == u) && (src.v == v);}
  bool operator!=(const UVPair &src) const {return (src.u != u) || (src.v != v);}
};
TypeIsSimple(UVPair);

//@{ UV compression range
extern const int UVCompressedMin;
extern const int UVCompressedMax;
//@}

struct UVPairCompressed
{
  union
  {
    struct
    {
      short u;
      short v;
    };
    DWORD packed;
  };

  bool operator ==(const UVPairCompressed &src) const {return src.packed == packed;}
  bool operator !=(const UVPairCompressed &src) const {return src.packed != packed;}
};
TypeIsSimple(UVPairCompressed);

/// Encapsulation of UV pairs array stored in compressed format
class UVPairs
{
protected:
  //! Maximum UV values the vertex buffer contains
  UVPair _maxUV;
  //! Minimum UV values the vertex buffer contains
  UVPair _minUV;
  /// UV pairs compressed to 2 x 8 bits in range given by <_minUV, _maxUV>
  CondensedAutoArray<UVPairCompressed> _uv;
public:
  int Size() const {return _uv.Size();}
  void Init(int n) {_uv.Init(n);}
  void Realloc(int size) {_uv.Realloc(size);}
  void Resize(int size) {_uv.Resize(size);}
  void Reserve(int needed, int wanted) {_uv.Reserve(needed, wanted);}
  void Compact() {_uv.Compact();}
  void Condense() {_uv.Condense();}
  void Expand() {_uv.Expand();}
  void Clear() {_uv.Clear();}
  bool IsCondensed() const {return _uv.IsCondensed();}

  float U(int i) const;
  float V(int i) const;
  UVPair UV(int i) const;
  const UVPair &MinUV() const {return _minUV;}
  const UVPair &MaxUV() const {return _maxUV;}

  /// Prepare array of UV to be set, calculate the inverse scale to be pass to other functions
  void InitUV(UVPair &invUV, const UVPair &minUV, const UVPair &maxUV);
  /// Recalculate array of UV to the new range
  void ChangeUVRange(UVPair &invUV, const UVPair &minUV, const UVPair &maxUV);
  /// Init _uv as an empty condensed array
  void SetCondensed(const UVPair &uv, int n);

  void SetU(int i, const UVPair &invUV, float u);
  void SetV(int i, const UVPair &invUV, float v);
  void SetUV(int i, const UVPair &invUV, float u, float v);
  void Add(const UVPair &invUV, float u, float v);

  /// direct access to compressed UV
  UVPairCompressed GetUVRaw(int i) const {return _uv[i];}
  /// direct access to compressed UV
  void SetUVRaw(int i, UVPairCompressed uv) {_uv.Set(i) = uv;}
  /// convert UV to compressed UV in own space (Shape::LoadTagged optimization)
  UVPairCompressed CalcUVRaw(const UVPair &invUV, float u, float v);

  size_t GetMemoryAllocated() const {return _uv.GetMemoryAllocated();}
  double GetMemoryUsed() const {return _uv.GetMemoryUsed();}

  void SerializeBin(SerializeBinStream &f);
};

//! ST vectors related to normal mapping
struct STPair
{
  Vector3Compressed s, t;
  
  STPair(){}
  STPair(Vector3 ss, Vector3 tt) : s(ss), t(tt) {}
};
TypeIsSimple(STPair);

#include <Es/Types/lLinks.hpp>

enum VBType
{
  VBNotOptimized, // shape is not optimized yet
	VBDynamic, // dynamic (animated), but never discarded
	VBDynamicDiscardable, // dynamic - animated, can be discarded
	VBStatic, // fully static - never animated, never discarded
	VBStaticReused, // fully static, frequently reused, but with no instancing
  VBStaticSkinned, // static buffer with skinning capability
	VBBigDiscardable, // never animated, may be discarded, big (may afford separate
	VBSmallDiscardable, // never animated, may be discarded, small
  VBNone, // no vertex buffer
  VBDefault, // same optimization as before
};

class EngineShapeProperties;

//! Version of vertex shader declaration
enum VSDVersion
{
  VSDV_Basic,
  VSDV_Skinning,
  VSDV_Instancing,
  VSDVCount,
};

//! Determine if engine should use push-buffers for better performance
#if _ENABLE_DX10
  #define USE_PUSHBUFFERS 0
#else
  #define USE_PUSHBUFFERS 0
#endif

//! Context used for push buffer drawing
struct PBContext
{
  //!{ Dynamic conditions variables
  int _materialLevel;
  int _instancesOffset;
  int _instancesCount;
  const Shape *_sMesh;
  //!}
};

//! Class to hold a single push-buffer
class PushBuffer : public RefCount, public TLinkBidirRef
{
protected:
  //! Key to identify this push-buffer - this key is retrieved from GetDynamicVariablesIdentifier function
  int _key;
public:
  //! Constructor
  PushBuffer(int key) {_key = key;}
  //! Function to compare the current key with the specified one
  bool IsKeyMatching(int key) const {return _key == key;}
  //! Function to compare the current key with the one in the specified push buffer
  bool IsKeyMatching(PushBuffer *pb) const {return _key == pb->_key;}
  //! Draw the push-buffer
  virtual void Draw(const PBContext &ctx) const = 0;
  //! Function to retrieve the size of this PushBuffer in bytes
  virtual size_t GetBufferSize() const = 0;
};

//! Push buffer manager that corresponds to one Shape
class PushBufferPool
{
protected:
  //! List of push buffers for special dynamic variables constellation
  /*!
  The list is sorted according to frequency of usage of a particular item.
  */
  mutable TListBidirRef<PushBuffer,SimpleCounter<PushBuffer> > _pbList;
  
public:
  //! Destructor
  ~PushBufferPool();
  //! Function to play push-buffer according to special key
  /*!
  Function returns true if the particular push-buffer was found and played.
  False means the push-buffer must be created first.
  */
  bool Draw(int key, const PBContext &ctx) const;
  //! Function to assign new push-buffer. Some old might be deleted at this point
  /*!
  Note that from this point further takes the push buffer pool responsibility for deleting
  the pointer.
  */
  void AssignNewPushBuffer(PushBuffer *pb);
  
  /// discard all push-buffers
  void DiscardAll();
  //! Function to retrieve the size of this PushBufferPool in bytes
  size_t GetBufferSize() const;
};

class VertexTableAnimationContext;

/// vertex buffer interface
class VertexBuffer: public RefCount
{
protected:
#if USE_PUSHBUFFERS
  //! Push-buffer associated with the vertex buffer
  PushBufferPool _pbPool;
#endif
public:
	//@{ ScopeLock can be used on this
	void Lock(const VertexTable *owner) const;
	void Unlock(const VertexTable *owner) const;
	bool ReleaseIfNotLocked(VertexTable *owner);
	//@}
	
  //! Constructor
	VertexBuffer(){}
  //! Destructor
	virtual ~VertexBuffer() {}

	virtual bool IsGeometryLoaded() const = 0;
	/// update vertices if necessary
  virtual void Update(int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, VSDVersion vsdVersion) = 0;
	/// before buffer is destructed
	virtual void Detach(const VertexTable &src) = 0;
  virtual void RemoveIndexBuffer() {}
  virtual void BindWithPushBuffer() {}
  /// called when associated resource (like pushbuffer) changes size or is allocated
  virtual void OnSizeChanged() {}

  virtual size_t GetBufferSize() const = 0;
  
  double GetMemoryUsed() const {return GetBufferSize();}

#if USE_PUSHBUFFERS
  //!{ Push buffer handling
  //! Function to play push-buffer according to special key
  /*!
    Function returns true if the particular push-buffer was found and played.
    False means the push-buffer must be created first.
  */
  bool DrawPushBuffer(int key, const PBContext &ctx) const {return _pbPool.Draw(key, ctx);}
  //! Function to assign new push-buffer. Some old might be deleted at this point
  /*!
    Note that from this point further takes the push buffer pool responsibility for deleting
    the pointer.
  */
  void AssignNewPushBuffer(PushBuffer *pb);
  
  /// discard push-buffers, because they need to recreated
  void DiscardPushBuffers();
  //!}
#endif

};


//! All existing vertex declaration types
/*!
  Note that all this declarations must be in the subset inclusion.
  That means: if A < B => B is not a subset of A (either A is a subset of B or they are different).
  That's because later we're trying to find a common vertex declaration for the whole model.
*/
enum EVertexDecl
{
  VD_Tex0,
  VD_Position_Tex0_Color,
  VD_Position_Normal_Tex0,
  VD_Position_Normal_Tex0_ST,
  VD_Position_Normal_Tex0_Tex1,
  VD_Position_Normal_Tex0_Tex1_ST,
  VD_Position_Normal_Tex0_ST_Float3,
  VD_Position_Normal_Tex0_WeightsAndIndices,
  VD_Position_Normal_Tex0_ST_WeightsAndIndices,
  VD_Position_Normal_Tex0_Tex1_WeightsAndIndices,
  VD_Position_Normal_Tex0_Tex1_ST_WeightsAndIndices,
  VD_ShadowVolume,
  VD_ShadowVolumeSkinned,
  VD_SimulWeatherClouds,
  VD_SimulWeatherCloudsCPU,
  VDCount,
  VD_None = -1
};

#include <Es/Strings/rString.hpp>

class Skeleton;
/// interface which provides loading from given source
class IShapeLoadSource
{
public:
  virtual void Load(Shape *shape) = 0;
  virtual bool Request(Shape *shape) = 0;
  virtual RString GetDebugName() const = 0;
  virtual Skeleton *GetSkeleton() const = 0;
  virtual ~IShapeLoadSource(){}
};


class QFBankHandle;
class LODShape;

#include <El/FreeOnDemand/memFreeReq.hpp>

const float PosEpsilon =  0.0001f; // was 0.005
const float NormEpsilon =  0.01f; // was 0.05
const float UVEpsilon =  0.00001f; // was 0.005

class AnimationRTWeight;
struct VertexNeighborInfo;
class VertexTableAnimationContext;

#ifndef _VBS3_MORETERRAINLODS
#error _VBS3_MORETERRAINLODS must be defined (use #define _VBS3_MORETERRAINLODS 0 to disable the feature)
#endif

#if _VBS3_MORETERRAINLODS
// number of items contained in VertexCustomInfo
// as we use VertexCustomInfo for keeping height values for all LODs, that number must be the same as LandSegment::NLods
static const int VertexCustomItems = 7;

struct VertexCustomInfo
{
  float _item[VertexCustomItems];
#if _VBS3_LAND_SHADOW
  float _landShadow;
#endif

  float Distance2(const VertexCustomInfo &other) const
  {
    float d2 = 0.0;
    for (int i = 0; i < VertexCustomItems; i++)
      d2 += Square(_item[i] - other._item[i]);
    return d2;
  }

  VertexCustomInfo operator+(const float &value) const
  {
    VertexCustomInfo out;
#if _VBS3_LAND_SHADOW
    out._landShadow = 0;
  #endif
    for (int i = 0; i < VertexCustomItems; i++)
      out._item[i] = _item[i] + value;
    return out;
  } 

};
TypeIsSimple(VertexCustomInfo);

typedef const VertexCustomInfo &VertexCustomInfoVal;
#endif // _VBS3_MORETERRAINLODS

/// data from vertex table which are used to create a vertex buffer
struct VertexTableData: public RefCount
{
  friend class VertexTable;
  friend class Shape;
  
  private:
	AutoArray<Vector3> _pos;
	CondensedAutoArray<Vector3Compressed> _norm;
	CondensedAutoArray<ClipFlags> _clip;
  /// ST vectors
  /** number of items either equals to number of items of the _pos array, or it is 0 which means it was not created. */
  AutoArray<STPair> _st;
  /// per-vertex alpha values
  AutoArray<unsigned char> _alpha;
  /// additional custom per-vertex attribute
#if _VBS3_MORETERRAINLODS
  AutoArray<VertexCustomInfo> _custom;
#else
  AutoArray<Vector3> _custom;
#endif
  /// primary UV set
	UVPairs _tex;
  /// Additional UV sets
  UVPairs _texCoord[1];

  /// Bone references for each vertex
  AutoArray<AnimationRTWeight> _vertexBoneRef;
  /// Bone references of neighbor vertices of a given vertex
  /** used for skinned shadows */
  AutoArray<VertexNeighborInfo> _neighborBoneRef;

  
  public:
  VertexTableData();
  VertexTableData(const VertexTableData &src, bool copyST, bool copySkinning);
	const AutoArray<Vector3> &GetPosArray() const {return _pos;}
	const CondensedAutoArray<Vector3Compressed> &GetNormArray() const {return _norm;}
	const AutoArray<STPair> &GetSTArray() const {return _st;}
	const AutoArray<unsigned char> &GetAlphaArray() const {return _alpha;}
#if _VBS3_MORETERRAINLODS
  const AutoArray<VertexCustomInfo> &GetCustomArray() const {return _custom;}
#else
  const AutoArray<Vector3> &GetCustomArray() const {return _custom;}
#endif
	const UVPairs &GetUVArray() const {return _tex;}
  const UVPairs &GetUVArray(int stage) const {Assert(stage > 0); return _texCoord[stage - 1];}

	ClipFlags Clip(int i) const {return _clip[i];}
	
  const AutoArray<AnimationRTWeight> &GetVertexBoneRef() const {return _vertexBoneRef;}
  const AutoArray<VertexNeighborInfo> &GetNeighborBoneRef() const {return _neighborBoneRef;}

  int NVertex() const {return _pos.Size();};
  bool IsSTPresent() const {Assert(_st.Size()==0 || _st.Size()==_pos.Size());return _st.Size()>0;};
  bool IsAlphaPresent() const {Assert(_alpha.Size()==0 || _alpha.Size()==_pos.Size());return _alpha.Size()>0;};
  bool IsCustomPresent() const {Assert(_custom.Size()==0 || _custom.Size()==_pos.Size());return _custom.Size()>0;};
  bool IsVertexBoneRefPresent() const {Assert(_vertexBoneRef.Size()==0 || _vertexBoneRef.Size()==_pos.Size());return _vertexBoneRef.Size()>0;};

	size_t GetMemoryAllocated() const;
};

//! array of vertices, corresponds to vertex buffer
/**
Contains also additional information like min-max box
*/

class VertexTable: public RefCount
{
	friend class TLVertexTable;
  friend class VertexTableAnimationContext;

	// this class stores basic geometry information
	// it is the basic class for building shapes and other visuals
  #ifdef _WIN32
  // must be 8B aligned so that we can use InterlockedExchange64 et. al.
  # define ALIGNREFVB __declspec(align(8))
  #else
  # define ALIGNREFVB
  #endif

public:
  ALIGNREFVB struct RefVertexBufferLocked: public Ref<VertexBuffer>
  {
    int _frameId;
    
    RefVertexBufferLocked():_frameId(0){}
    
    void Lock();
    bool ReleaseIfNotLocked();
  };
  
protected:
	mutable RefVertexBufferLocked _buffer; ///< Engine may store some additional information here

	/// vertex count - equals to _pos.Size(), unless geometry is unloaded
	int _nVertex;
	Ref<VertexTableData> _data;
	/// sometimes we need to keep the clipflags in memory
	/** with old format clipflags were not included in the geometry streaming region */
	SRef< CondensedAutoArray<ClipFlags> > _clipOldFormat;

  /// Flag to determine each vertex has got a reference to maximum one bone
  bool _vertexBoneRefIsSimple;

	ClipFlags _orHints,_andHints; // we can do some optimizations based on this

	Vector3 _minMax[2];
	Vector3 _bCenter;
	float _bRadius;
	
	// constructor helpers
	void AllocTables( int nPos );
	void DoConstruct(const VertexTable &src, bool copyST=true, bool copySkinning=true);
	
public:
	// initializers
	void ReleaseTables();
	// constructors
	VertexTable();
	VertexTable(int nPos);
	VertexTable(const VertexTable &src, bool copyST=true, bool copySkinning=true);

	// public constructor helper
  void InitVertexTableData();
  void FreeVertexTableData() { _data.Free(); }

	void operator=(const VertexTable &src);
	virtual ~VertexTable();

  /// ask Shape if geometry is loaded
  virtual bool IsLoadLocked() const {return true;}
  //! Calculates and returns maximum and minimum values of vertex position
  /*!
    If the interval is about to be too small, then it is artificially extended to be reasonable small.
  */
  void CalculateMaxAndMinPos(Vector3 &maxPos, Vector3 &minPos, bool dynamic=false) const;

  double GetMemoryUsed() const;
  double GetMemoryUsedUnloadable() const;

	void Init(int nPos);
	void Clear(){ReleaseTables();}

	void ReallocTable(int nPos);
	//! Reserves space for nPos vertices
  /*!
    Reallocates the space if necessary.
    \param nPos Number of vertices to allocate space for.
  */
	void Reserve(int nPos); // realloc if necessary
  void ReserveST(int nPos);
  void ReserveAlpha(int nPos);
  void ReserveCustom(int nPos);
	void Resize(int nPos);
	
	void Compact();
	void CalculateHints();
	void SetHints(ClipFlags orHints, ClipFlags andHints)
	{
		_orHints = orHints;
    _andHints = andHints;
	}
	ClipFlags GetOrHints() const {return _orHints;}
	ClipFlags GetAndHints() const {return _andHints;}

	Vector3Val Min() const {return _minMax[0];}
	Vector3Val Max() const {return _minMax[1];}
	const Vector3 *MinMax() const {return _minMax;}
	Vector3 *MinMax() {return _minMax;}

	Vector3 MinMaxCorner(int x, int y, int z) const
	{	// select coordinate sources (0-min, 1-max)
		return Vector3(_minMax[x][0],_minMax[y][1],_minMax[z][2]);
	}

  void ScanMinMax(Vector3 *minMax) const;
	void ScanBSphere(Vector3 &bCenter, float &bRadius) const;

	Vector3Val BSphereCenter() const {return _bCenter;}
	float BSphereRadius() const {return _bRadius;}

	VertexBuffer *LockVertexBuffer() const {_buffer.Lock();return _buffer;}
	
  bool VertexBufferReleaseIfNotLocked() {return _buffer.ReleaseIfNotLocked();}
  
  bool IsVertexBufferGeometryLoaded() const {return _buffer && _buffer->IsGeometryLoaded();}
  void ReleaseVBuffer(bool deferred=false) const;
  
	const VertexTableData *GetVertexTableData() const {return _data;}
	
	VertexBuffer *GetVertexBuffer() const
	{
	  //DoAssert(!_buffer || _buffer->IsLocked());
	  return _buffer;
	}

	
  //! Calculates bounding box and bounding sphere of all vertices.
  /*!
    As a result _minMax, bCenter and bRadius members are set.
  */
	void CalculateMinMax();
  /// copy min max from another Shape
  void CopyMinMax(const Shape &src);

	// properties
	int NVertex() const {return _nVertex;}
	int NPos() const
  {
    Assert(IsLoadLocked());
    Assert(!_data || _data->_pos.Size() == _nVertex);
    return _nVertex;
  } // TODO: remove pos/normal access
  bool IsSTPresent() const {return _nVertex==0 || _data->_st.Size() == _nVertex;};
  bool IsAlphaPresent() const {return _nVertex==0 || _data->_alpha.Size() == _nVertex;};
  bool IsCustomPresent() const {return _nVertex==0 || _data->_custom.Size() == _nVertex;};
  bool IsVertexBoneRefPresent() const {return _nVertex==0 || _data->_vertexBoneRef.Size() == _nVertex;};
  int NUVStages() const {return _nVertex > 0 && _data->_texCoord[0].Size() == _nVertex ? 2 : 1;};
	
	float U(int i) const {Assert(IsLoadLocked()); return _data->_tex.U(i);}
	float V(int i) const {Assert(IsLoadLocked()); return _data->_tex.V(i);}
	UVPair UV(int i) const {Assert(IsLoadLocked()); return _data->_tex.UV(i);}
  const UVPair &MinUV() const {return _data->_tex.MinUV();}
  const UVPair &MaxUV() const {return _data->_tex.MaxUV();}

  float U(int stage, int i) const {Assert(IsLoadLocked()); return stage == 0 ? _data->_tex.U(i) : _data->_texCoord[stage - 1].U(i);}
  float V(int stage, int i) const {Assert(IsLoadLocked()); return stage == 0 ? _data->_tex.V(i) : _data->_texCoord[stage - 1].V(i);}
  UVPair UV(int stage, int i) const {Assert(IsLoadLocked()); return stage == 0 ? _data->_tex.UV(i) : _data->_texCoord[stage - 1].UV(i);}
  const UVPair &MinUV(int stage) const {return stage == 0 ? _data->_tex.MinUV() : _data->_texCoord[stage - 1].MinUV();}
  const UVPair &MaxUV(int stage) const {return stage == 0 ? _data->_tex.MaxUV() : _data->_texCoord[stage - 1].MaxUV();}

  const STPair &ST(int i) const {return _data->_st[i];}
  unsigned char Alpha8(int i) const {return _data->_alpha[i];}

  void SetAndStretchAlpha8(int i, unsigned char a)
  {
    _data->_alpha.Access(i);
    _data->_alpha[i] = a;
  }
  void SetAndStretchAlpha(int i, float a)
  {
    int a8 = toInt(a*255);
    saturate(a8, 0, 255);
    SetAndStretchAlpha8(i, a8);
  }
  void ResizeCustom(int nVertex) {_data->_custom.Resize(nVertex);}

#if _VBS3_MORETERRAINLODS
  VertexCustomInfoVal Custom(int i) const {return _data->_custom[i];}
  void SetCustom(int i, VertexCustomInfoVal c) {_data->_custom[i] = c;}
  void SetAndStretchCustom(int i, VertexCustomInfoVal c) {_data->_custom.Access(i); _data->_custom[i] = c;}
#else
  Vector3Val Custom(int i) const {return _data->_custom[i];}
  void SetCustom(int i, Vector3Par c) {_data->_custom[i]=c;}
  void SetAndStretchCustom(int i, Vector3Par c)
  {
    _data->_custom.Access(i);
    _data->_custom[i] = c;
  }
#endif

  const AnimationRTWeight &Weight(int i) const;
	
  //! Returns array of vertex bone references
  const AutoArray<AnimationRTWeight> &GetVertexBoneRef() const {return _data->_vertexBoneRef;}
  //! Returns array of neighbor bone references
  const AutoArray<VertexNeighborInfo> &GetNeighborBoneRef() const {return _data->_neighborBoneRef;}
  
  bool VertexBoneRefIsSimple() const {return _vertexBoneRefIsSimple;}
  
  /// Prepare array of UV to be set, calculate the inverse scale to be pass to other functions
  void InitUV(UVPair &invUV, const UVPair &minUV, const UVPair &maxUV) {_data->_tex.InitUV(invUV, minUV, maxUV);}
  /// Prepare array of UV to be set, calculate the inverse scale to be pass to other functions
  void InitUV(int stage, UVPair &invUV, const UVPair &minUV, const UVPair &maxUV) {Assert(stage > 0); _data->_texCoord[stage - 1].InitUV(invUV, minUV, maxUV);}
  /// Recalculate array of UV to the new range
  void ChangeUVRange(UVPair &invUV, const UVPair &minUV, const UVPair &maxUV) {_data->_tex.ChangeUVRange(invUV, minUV, maxUV);}

  void SetU(int i, const UVPair &invUV, float u) {_data->_tex.SetU(i, invUV, u);}
  void SetV(int i, const UVPair &invUV, float v) {_data->_tex.SetV(i, invUV, v);}
  void SetUV(int i, const UVPair &invUV, float u, float v) {_data->_tex.SetUV(i, invUV, u, v);}
  void SetU(int stage, int i, const UVPair &invUV, float u) {Assert(stage > 0); _data->_texCoord[stage - 1].SetU(i, invUV, u);}
  void SetV(int stage, int i, const UVPair &invUV, float v) {Assert(stage > 0); _data->_texCoord[stage - 1].SetV(i, invUV, v);}
  void SetUV(int stage, int i, const UVPair &invUV, float u, float v) {Assert(stage > 0); _data->_texCoord[stage - 1].SetUV(i, invUV, u, v);}

  //! Sets the ST vectors at a specified position, enlarges array if necessary
  const STPair &ST(int i) {return _data->_st[i];}
  void SetAndStretchST(int i, const STPair &st) {_data->_st.Access(i); _data->_st[i] = st;}

	const AutoArray<Vector3> &GetPosArray() const {return _data->_pos;}
	const UVPairs &GetUVArray() const {return _data->_tex;}
  const UVPairs &GetUVArray(int stage) const {Assert(stage > 0); return _data->_texCoord[stage - 1];}
	const CondensedAutoArray<Vector3Compressed> &GetNormArray() const {return _data->_norm;}
	const AutoArray<STPair> &GetSTArray() const {return _data->_st;}
	const AutoArray<unsigned char> &GetAlphaArray() const {return _data->_alpha;}
#if _VBS3_MORETERRAINLODS
  const AutoArray<VertexCustomInfo>  &GetCustomArray() const {return _data->_custom;}
  AutoArray<VertexCustomInfo>  &GetCustomArray() {return _data->_custom;}
#else
  const AutoArray<Vector3> &GetCustomArray() const {return _data->_custom;}
  AutoArray<Vector3> &GetCustomArray() {return _data->_custom;}
#endif
	// vertex buffer style access
  //! add vertex, Objective point index known
  int AddVertex(Vector3Par pos, Vector3Par norm, ClipFlags clip, const UVPair &invUV, float u, float v,
    AutoArray<VertexIndex> *v2p, int pIndex, float precP, float precN, const UVPair *texCoord, const UVPair *invUV1);
  //! add vertex, Objective point index known
  int AddVertex(Vector3Par pos, Vector3Par norm, ClipFlags clip, const UVPair &invUV, float u, float v,
    AutoArray<VertexIndex> *v2p=NULL, int pIndex=-1, float precP=PosEpsilon, float precN=NormEpsilon)
  {
    // when texCoord is given, invUV1 need to be given as well
    return AddVertex(pos, norm, clip, invUV, u, v, v2p, pIndex, precP, precN, NULL, NULL);
  }
	//! add vertex - landscape
#if  _VBS3_MORETERRAINLODS
  int AddVertex(Vector3Par pos, Vector3Par norm, ClipFlags clip, const UVPair &invUV, float u, float v,
    const VertexCustomInfo *custom, STPair st);
#else
  int AddVertex(Vector3Par pos, Vector3Par norm, ClipFlags clip, const UVPair &invUV, float u, float v,
    const Vector3 *custom, STPair st);
#endif
	//! add vertex, no duplicate check
	int AddVertexFast(Vector3Par pos, Vector3Par norm, ClipFlags clip, const UVPair &invUV, float u, float v);
	
	Vector3Val Pos(int i) const {Assert(IsLoadLocked());return _data->_pos[i];}
	Vector3Compressed Norm(int i) const {Assert(IsLoadLocked());return _data->_norm[i];}
	Vector3 &SetPos(int i) {return _data->_pos[i];}
	Vector3Compressed &SetNorm(int i) {return _data->_norm.Set(i);}

	void RemoveNormalArrays(); // we will use only Q or only buffer

  void ExpandClip() {_data->_clip.Expand();}
  template <class ArrayType>
  void ExpandClipTo(ArrayType &dst) const {_data->_clip.ExpandTo(dst);}
  void CondenseClip() {_data->_clip.Condense();}
  void CondenseTex() {if (_data) _data->_tex.Condense();}
  void CondenseNorm() {if (_data) _data->_norm.Condense();}
	void SetClip(int i, ClipFlags clip) {_data->_clip.Set(i)=clip;}
	void SetClipAll(ClipFlags clip) {_data->_clip.SetAll(clip);}
	ClipFlags Clip(int i) const {Assert(IsLoadLocked());return _data->_clip[i];}
	
	//@{ perform the same operation on all clip flags, including andHints and orHints
	template <class Op>
	void ForEachClip(Op op)
	{
	  ClipFlags oldOrHints = _orHints;
	  ClipFlags oldAndHints = _andHints;
	  op(_orHints);
	  op(_andHints);
	  if (oldOrHints!=_orHints || oldAndHints!=_andHints)
	  {
	    _orHints &= ClipHints;
	    _andHints &= ClipHints;
	    if (_data) _data->_clip.ForEach(op);
	  }
	}
	
	void ClipAndAll(ClipFlags clip);
	void ClipOrAll(ClipFlags clip);
	void ClipAndOrAll(ClipFlags andClip, ClipFlags orClip);
	//@}

  bool CheckIntegrity() const;

  //! load unloaded data from a given location
  bool LoadVertices(const QFBankHandle &source, LODShape *lodShape, int version
#ifdef _M_PPC
    , QFileEndian endian
#endif
    , bool lzoCompression);
  /// unload what was loaded by LoadVertices
  void UnloadVertices();
  /// request unloaded data from a given location
  bool RequestVertices(const QFBankHandle &source, LODShape *lodShape, int version
#ifdef _M_PPC
    , QFileEndian endian
#endif
    , bool lzoCompression);

  
  /// post-load processing, called after geometry is loaded from the source
  virtual void GeometryLoadedHandler();

  /// pre-unload processing, called before geometry is released
  virtual bool GeometryUnloadedHandler();
  
  /// size used by geometry - valid both when loaded and unloaded
  size_t GeometrySize() const
  {
    return _nVertex * (sizeof(UVPair)+sizeof(Vector3P)*2);
  }
  /// memory controlled, but not unloaded with geometry
  size_t GetMemoryControlledByShape(bool includeUnloadable) const;
};

//! lock object passed as argument during lifetime of ScopeLock object
class VertexBufferLock: private NoCopy
{
protected:
	VertexBuffer *_vb;
	const VertexTable *_owner;
	
public:
	VertexBufferLock():_vb(NULL),_owner(NULL){}
	VertexBufferLock(const VertexTable *owner):_vb(owner ? owner->LockVertexBuffer() : NULL),_owner(owner){if (_vb) _vb->Lock(owner);}
	void Lock(const VertexTable *owner){Assert(!_vb);_vb=owner ? owner->LockVertexBuffer() : NULL;_owner=owner;if (_vb) _vb->Lock(_owner);}
	~VertexBufferLock() {if (_vb) _vb->Unlock(_owner);}
};



/// storage for MemAllocSA allocators used in VertexTableAnimationContext
template <int nVertices>
struct VertexTableAnimationContextStorage
{
  AUTO_STORAGE_ALIGNED(Vector3,_pos,nVertices,AllocAlign16);
  AUTO_STORAGE_ALIGNED(Vector3,_norm,nVertices,AllocAlign16);

  size_t SizeOfPos() const {return sizeof(_pos);}
  size_t SizeOfNorm() const {return sizeof(_norm);}
};

// template <class Type, class atype>
// inline int ByteSize(int size)
// {
//   return (size*sizeof(Type)+sizeof(atype)-1)/sizeof(atype);
// }
/// similar to VertexTableAnimationContextStorage, but using MemAllocDataStack memory
template <int nVertices>
struct VertexTableAnimationContextStorageDS
{
  void *_pos;
  void *_norm;
  size_t _posOld;
  size_t _normOld;
  
  VertexTableAnimationContextStorageDS()
  {
    _pos = GDataStack.Allocate(nVertices*sizeof(Vector3),sizeof(AllocAlign16),_posOld);
    _norm = GDataStack.Allocate(nVertices*sizeof(Vector3),sizeof(AllocAlign16),_normOld);
  }
  ~VertexTableAnimationContextStorageDS()
  {
    // stack - release order reversed
    GDataStack.Release(_norm,_normOld);
    GDataStack.Release(_pos,_posOld);
  }

  static size_t SizeOfPos() {return nVertices*sizeof(Vector3);}
  static size_t SizeOfNorm() {return nVertices*sizeof(Vector3);}
};

#if _ENABLE_REPORT
  /// check if array has overflown its pre-allocated storage
  #define CHECK_ARRAY_STORAGE(arr) \
    if (arr.Size()>arr.StorageCount()) \
      LogF(#arr ".Size() = %d (max. expected was %d)",arr.Size(),arr.StorageCount());
#else  
  #define CHECK_ARRAY_STORAGE(arr)
#endif


/// Data of VertexTable modified by animations
class VertexTableAnimationContext
{
private:
  // see class VertexTable for corespondent members
  AutoArray<Vector3, MemAllocSA > _pos;
  AutoArray<Vector3Compressed, MemAllocSA > _norm;

  /// Init() was called
  bool _initialized;
  /// Load() was called
  bool _loaded;

protected:
  bool _minMaxDirty;
  /// bounding box
  Vector3 _minMax[2];
  /// bounding sphere center
  Vector3 _bCenter;
  /// bounding sphere radius
  float _bRadius;

  template <class Storage>
  void SetStorage(Storage &storage)
  {
    _pos.SetStorage(MemAllocSA(storage._pos,storage.SizeOfPos()));
    _norm.SetStorage(MemAllocSA(storage._norm,storage.SizeOfNorm()));
  }

public:
  VertexTableAnimationContext()
  {
    _initialized = false;
    _loaded = false;
  }

  void Init(const VertexTable *vertexTable);
  void LoadVertices(const VertexTable *vertexTable);

  /// test whether Load was called
  bool AreVerticesLoaded() const {return _loaded;}

  const Array<Vector3> &GetPosArray(const VertexTable *vertexTable) const
  {
    return _loaded ? (const Array<Vector3> &)_pos : (const Array<Vector3> &)vertexTable->GetPosArray();
  }
  // can be called only on loaded context
  const Array<Vector3Compressed> &GetNormArray() const {DoAssert(_loaded); return _norm;}
  Vector3Val GetPos(const VertexTable *vertexTable, int i) const {return _loaded ? _pos[i] : vertexTable->Pos(i);}
  // load the context if needed
  Vector3 &SetPos(const VertexTable *vertexTable, int i)
  {
    LoadVertices(vertexTable);
    return _pos[i];
  }
  // load the context if needed
  Vector3Compressed &SetNorm(const VertexTable *vertexTable, int i)
  {
    LoadVertices(vertexTable);
    return _norm.Set(i);
  }

  Vector3Val Min() const {return _minMax[0];}
  Vector3Val Max() const {return _minMax[1];}
  const Vector3 *MinMax() const {return _minMax;}
  
  /// min-max box need to be recalculated
  void InvalidateMinMax() {_minMaxDirty = true;}
  /// tell the context of the min-max is valid (we need not to recalculate it)
  void ValidateMinMax() {_minMaxDirty = false;}
  void SetMinMax(Vector3Val min, Vector3Val max, Vector3Val bCenter, float bRadius);
  void CalculateMinMax(const VertexTable *vertexTable);
  void ScanBSphere(const VertexTable *vertexTable, Vector3 &bCenter, float &bRadius) const;
  void ScanMinMax(const VertexTable *vertexTable, Vector3 *minMax) const;
};

#endif

// Poseidon - shape management
// (C) 1998, SUMA

#include "../wpch.hpp"
#include "shape.hpp"
#include "modelConfig.hpp"
#include "../engine.hpp"
#include "../global.hpp"
#include "../rtAnimation.hpp"
#include <El/QStream/qbStream.hpp>
#include "../timeManager.hpp"
#include "../paramFileExt.hpp"
#include "../textbank.hpp"
#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "../animation.hpp"
#include <El/FileServer/fileServer.hpp>
#include "../txtPreload.hpp"
#include "material.hpp"
#include "../depMake.hpp"
#include "../scene.hpp"

#include <El/Common/perfLog.hpp>
#include <El/QStream/serializeBin.hpp>
#include <El/Evaluator/expressImpl.hpp>

#include "edges.hpp"
#include "specLods.hpp"

#include <Es/Algorithms/qsort.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/ReportStack/reportStack.hpp>

#include "data3d.h"

#include "mapTypes.hpp"

#if _ENABLE_PHYSICS
#include "Physics\enf_worldphysics.h"
#endif
#include "common/enf_mathlib.h"

#ifdef _MSC_VER
  #pragma warning( disable: 4355 )
#endif

#ifdef _WIN32
template LLink<TexMaterial>;
#endif

Offset ConvertOffset(SerializeBinStream &f, Offset o);

SubSkeletonIndexSet SubSkeletonIndexSet::_default;

void ConvexComponentAnimationContext::Recalculate(const ConvexComponent &cc, const Array<Vector3> &pos)
{
  if (!_enabled) return;

  int size = cc.Size();
  if (size > 0)
  {
    int index = cc[0];
    Vector3Val p = pos[index];
    
    _minMax[0] = p;
    _minMax[1] = p;
    _center = p; 

    for (int i=1; i<size; i++)
    {
      int index = cc[i];
      Vector3Val p = pos[index];

      CheckMinMaxIter(_minMax[0], _minMax[1], p);
      _center += p;
    }
    // find bounding center
    _center /= size;
    // find bounding sphere radius
    float maxRadius2 = 0;
    for (int i=0; i<size; i++)
    {
      int index = cc[i];
      float radius2 = pos[index].Distance2(_center);
      saturateMax(maxRadius2, radius2);
    }
    _radius = sqrt(maxRadius2);
  }
  else
  {
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    _center = VZero;
    _radius = 0;
  }
}

void AnimationContext::Init(const Shape *shape, const ConvexComponents *cc, bool animateGeometry)
{
  // initialize once
  Assert(!_initialized);
  if (_initialized) return;

  _cc = cc;

  if (animateGeometry)
  {
    // vertices properties
    VertexTableAnimationContext::Init(shape);
    _geometryInitialized = true;
  }

  _initialized = true;
}

void AnimationContext::LoadSections(const Shape *shape)
{
  if (_loadedSections) return;

  // section properties (animated even for drawing)
  int n = shape->_face._sections.Size();
  _sections.Realloc(n);
  _sections.Resize(n);
  for (int i=0; i<n; i++) _sections[i].PolyProperties::operator =(shape->_face._sections[i]);

  _loadedSections = true;

  CHECK_ARRAY_STORAGE(_sections);
}

void AnimationContext::LoadUVOffsets(const Shape *shape)
{
  if (_loadedUVOffsets) return;

  // section properties (animated even for drawing)
  int n = shape->_face._sections.Size();
  _UVOffsets.Realloc(n);
  _UVOffsets.Resize(n);
  for (int i=0; i<n; i++) _UVOffsets[i] = UVOffset();

  _loadedUVOffsets= true;

  CHECK_ARRAY_STORAGE(_UVOffsets);
}

void AnimationContext::LoadPlanes(const Shape *shape)
{
  if (_loadedPlanes) return;

  // do not animate for drawing
  if (!_geometryInitialized) return;

  // face normals
  if (shape->_plane.Size() > 0)
  {
    _plane = shape->_plane;
    _faceNormalsValid = shape->_faceNormalsValid;
  }
  else
  {
    int n = shape->_face.Size();
    _plane.Realloc(n);
    _plane.Resize(n);
    _faceNormalsValid = false; // calculate the _plane content whenever needed
  }

  _loadedPlanes = true;

  CHECK_ARRAY_STORAGE(_plane);
}

void AnimationContext::LoadCC()
{
  if (_loadedCC) return;

  // do not animate for drawing
  if (!_geometryInitialized) return;

  // convex components
  if (_cc)
  {
    int n = _cc->Size();
    _convexComponents.Realloc(n);
    _convexComponents.Resize(n);
    for (int i=0; i<n; i++)
    {
      const ConvexComponent *src = _cc->Get(i);
      ConvexComponentAnimationContext &dst = _convexComponents[i];
      dst._minMax[0] = src->Min();
      dst._minMax[1] = src->Max();
      dst._center = src->GetCenter();
      dst._radius = src->GetRadius();
      dst._enabled = true;
    }

    _convexComponentsValid = _cc->IsValid();
  }
  else _convexComponentsValid = true; // no convex components

  _loadedCC = true;
  //Removed because it spammed into debug log. I rather added performance warning into shape loading.
  //CHECK_ARRAY_STORAGE(_convexComponents);
}

Vector3Val AnimationContext::GetCCMin(int i) const
{
  return _loadedCC ? _convexComponents[i].Min() : _cc->Get(i)->Min();
}
Vector3Val AnimationContext::GetCCMax(int i) const
{
  return _loadedCC ? _convexComponents[i].Max() : _cc->Get(i)->Max();
}
const Vector3 *AnimationContext::GetCCMinMax(int i) const
{
  return _loadedCC ? _convexComponents[i].MinMax() : _cc->Get(i)->MinMax();
}
Vector3Val AnimationContext::GetCCCenter(int i) const
{
  return _loadedCC ? _convexComponents[i].GetCenter() : _cc->Get(i)->GetCenter();
}
float AnimationContext::GetCCRadius(int i) const
{
  return _loadedCC ? _convexComponents[i].GetRadius() : _cc->Get(i)->GetRadius();
}

void AnimationContext::InvalidateNormals(const Shape *shape)
{
  LoadPlanes(shape); // planes need to be changed

  _faceNormalsValid = false;
  InvalidateMinMax();
}

void AnimationContext::RecalculateNormals(const Shape *shape)
{
  PROFILE_SCOPE_DETAIL_EX(acReN,coll);
  LoadPlanes(shape); // planes need to be changed

  if (_plane.Size() > 0)
  {
    int p = 0;
    for (Offset i=shape->BeginFaces(), e=shape->EndFaces(); i<e; shape->NextFace(i), p++)
    {
      const Poly &face = shape->Face(i);
      Plane &plane = _plane[p];
      // ignore singular faces - we cannot compute them
      if (face.N() < 3)
      {
        plane = Plane(VZero, 0);
      }
      else
      {
        face.CalculateNormal(plane, GetPosArray(shape));
      }
    }
    Assert(p == _plane.Size());
  }
  _faceNormalsValid = true;

  // if necessary, recalculate bsphere and minmax box
  if (_minMaxDirty)
  {
    CalculateMinMax(shape);
  }
}

void AnimationContext::RecalculateNormals(const Shape *shape, const ConvexComponents &cc)
{
  PROFILE_SCOPE_DETAIL_EX(acReN,coll);
  LoadPlanes(shape); // planes need to be changed

  if (_plane.Size() > 0)
  {
    int p = 0;
    for (Offset i=shape->BeginFaces(), e=shape->EndFaces(); i<e; shape->NextFace(i), p++)
    {
      const Poly &face = shape->Face(i);
      Plane &plane = _plane[p];
      // ignore singular faces - we cannot compute them
      if (face.N() < 3)
      {
        plane = Plane(VZero, 0);
      }
      else
      {
        face.CalculateNormal(plane, GetPosArray(shape));
      }
    }
    Assert(p == _plane.Size());
  }
  _faceNormalsValid = true;

  // if necessary, recalculate bsphere and minmax box
  if (_minMaxDirty)
  {
    CalculateMinMaxFromCC(shape, cc);
  }
}

void AnimationContext::RecalculateConvexComponents(const Shape *shape, const ConvexComponents &cc)
{
  PROFILE_SCOPE_DETAIL_EX(acReC,coll);
  for (int i=0; i<_convexComponents.Size(); i++)
  {
    _convexComponents[i].Recalculate(*cc[i], GetPosArray(shape));
  }
  _convexComponentsValid = true;
}

void AnimationContext::CalculateMinMaxFromCC(const Shape *shape, const ConvexComponents &cc)
{
  if (shape->NPos() <= 0)
  {
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    _bCenter = VZero;
    _bRadius = 0;
  }
  else
  {
    bool init = false;    
    for (int i=0; i<cc.Size(); i++)
    {
      ConvexComponentAnimationContext &component = _convexComponents[i];
      if (!component.IsEnabled()) continue;

      const ConvexComponent &ccc = *(cc[i]);
      for (int j=0; j<ccc.Size(); j++)
      {
        Vector3Val pos = GetPos(shape, ccc[j]);
        if (init)
        {
          CheckMinMaxInline(_minMax[0], _minMax[1], pos);
        }
        else
        {
          init = true;
          _minMax[0] = pos;
          _minMax[1] = pos;
        }        
      }
    }

    if (!init)
    {
      _minMax[0] = VZero;
      _minMax[1] = VZero;
    }

    _bCenter = (_minMax[0] + _minMax[1]) * 0.5;
    float maxDist2 = 0;       
    for (int i=0; i<cc.Size(); i++)
    {
      ConvexComponentAnimationContext &component = _convexComponents[i];
      if (!component.IsEnabled()) continue;

      const ConvexComponent &ccc = *(cc[i]);
      for (int j=0; j<ccc.Size(); j++)
      {
        Vector3Val pos = GetPos(shape, ccc[j]);
        saturateMax(maxDist2, pos.Distance2Inline(_bCenter));
      }
    }
    _bRadius = sqrt(maxDist2);

  }
  _minMaxDirty = false;
}

ShapeRef::ShapeRef()
{
}

ShapeRef::ShapeRef(Shape *shape)
:_ref(shape)
{
  if (shape) shape->_trackRef = this;
}

ShapeRef::~ShapeRef()
{
  if (_ref)
  {
    Assert(_ref->_trackRef==this);
    _ref->_trackRef = NULL;
    _ref = NULL;
  }
}


void ShapeRef::operator = (Shape *shape)
{
  if (shape==_ref) return;
  if (_ref)
  {
    Assert(_ref->_trackRef==this);
    _ref->_trackRef = NULL;
    _ref = NULL;
  }
  _ref = shape;
  if (shape) shape->_trackRef = this;
}

#include <Es/Memory/normalNew.hpp>

//! permanent shape - no unloading
class ShapeRefPermanent: public ShapeRef
{
  public:
  ShapeRefPermanent(Shape *shape);

  virtual void Lock() {}
  virtual void Unlock() {}
  virtual bool IsLocked() const {return true;}

  virtual bool IsLoaded() const {return true;}
  virtual bool RequestLoading(bool load) const {return true;}
  virtual void OnGeometryLoaded(Shape *shape) const {}

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

DEFINE_FAST_ALLOCATOR(ShapeRefPermanent)

ShapeRefPermanent::ShapeRefPermanent(Shape *shape)
:ShapeRef(shape)
{
}




DEFINE_FAST_ALLOCATOR(Shape)

void RefShapeRef::SetPermanentRef(Shape *shape)
{
  ShapeRefPermanent *ref = new ShapeRefPermanent(shape);
  operator = (ref);
}
  
ShapeRef *Shape::GetTrackRef()
{
  if (_trackRef) return _trackRef;
  // TODO: use ShapeRefLoadable when possible
  return new ShapeRefPermanent(this);
}

//static unsigned int shapeCnt = 0;
Shape::Shape()
:_special(0),_faceNormalsValid(false),_trackRef(NULL),
_vertexDeclaration(VD_Position_Normal_Tex0),
_shapePropertiesNeeded(0),
_isVertexDeclarationAvailable(true),
_loadCount(0),
_canBeInstanced(true),
_canUsePushBuffer(true),
_allTexturesReady(true),
_color(PackedBlack),
_colorTop(PackedBlack),
_loadWarning(false)
#if _ENABLE_PHYSICS
,_physGeoms(0)
,_physGeomsLen(0)
#endif
{
  //LogF("shape cnt: %u\n", ++shapeCnt);
  #if _ENABLE_REPORT
  _loadCountCB = AtomicInt(0);
  #endif
}

// ShapeSection is a group of faces that can be drawn with one call
// this means they share at least texture and flags
// in case of T&L engine they also need to share animation properties


Shape::~Shape()
{
  //LogF("shape cnt: %u\n", --shapeCnt);
  #if _ENABLE_REPORT
  Assert(_loadCountCB == 0);
  #endif
  Assert(_loadCount==0);
  Assert (!IsInList());
  Clear();
}

Shape::Shape(const Shape &src, bool copyAnimations, bool copySelections, bool copyST)
:base(src, copyST, copySelections),
_loadCount(0),
_face(src._face),
_vertexDeclaration(src._vertexDeclaration),
_shapePropertiesNeeded(src._shapePropertiesNeeded),
_isVertexDeclarationAvailable(src._isVertexDeclarationAvailable),
_faceProperties(src._faceProperties),
_plane(src._plane),
_color(src._color),
_special(src._special),
_canBeInstanced(src._canBeInstanced),
_canUsePushBuffer(src._canUsePushBuffer),
_allTexturesReady(src._allTexturesReady),
_prop(src._prop), // copy named properties
_textures(src._textures), // copy texture list
_pointToVertex(src._pointToVertex),
_vertexToPoint(src._vertexToPoint),
_faceNormalsValid(src._faceNormalsValid),_trackRef(NULL)
#if _ENABLE_PHYSICS
,_physGeoms(0)
,_physGeomsLen(0)
#endif
{
  //LogF("shape cnt: %u\n", ++shapeCnt);
  #if _ENABLE_REPORT
  _loadCountCB = AtomicInt(0);
  #endif
  if( copyAnimations ) _phase=src._phase;
  if (copySelections) _sel = src._sel;
  
  _minMax[0]=src._minMax[0];
  _minMax[1]=src._minMax[1];
  //_boundingSphere=src._boundingSphere;
  for (int i=0; i<_proxy.Size(); i++)
  {
    _proxy[i] = new ProxyObject(*src._proxy[i]);
  }
}

#ifdef _MSC_VER
  #pragma warning( default: 4355 )
#endif


#if _DEBUG
#define VERBOSE 1
#else
#define VERBOSE 0
#endif

struct SortVertex
{
  int vertex,point,prior;
};
TypeIsSimple(SortVertex)

static int CmpSortVertex( const SortVertex *v0, const SortVertex *v1 )
{
  int d;
  d= v0->prior-v1->prior;
  if (d) return d;
  d = v0->point-v1->point;
  return d;
}

static bool LoadTag( QIStream &f, RString &name, int &size )
{
  int startTag = f.get();
  if (startTag<0)
  {
    name = RString();
    return false;
  }
  if (startTag!=1)
  {
    const int tagLen = 64;
    char tag64[tagLen+1];
    tag64[0] = startTag;
    tag64[tagLen] = 0;
    f.read(tag64+1,tagLen-1);
    name = tag64;
  }
  else
  {
    const int maxTaggLen = 1024;
    char tagg[maxTaggLen+1];
    tagg[maxTaggLen] = 0;
    int taggLen = 0;
    for(;;)
    {
      int c = f.get();
      if (c<0)
      {
        name = RString();
        return false;
      }
      if (taggLen<maxTaggLen)
      {
        tagg[taggLen++] = c;
      }
      else
      {
        RptF("Too long tag (selection) name %s",tagg);
      }
      if (c==0)
      {
        name = tagg;
        break;
      }
    }
  }
  f.read((char *)&size,sizeof(size));
  if( f.fail() || f.eof() ) return false;
  return true;
}

// convert old point attributes
// to new material indices (introduced on 12 Feb 2001)

static GeometryOnly ResolGeometryOnly(float resolution)
{
  if (resolution>900)
  {
    if (
      IsSpec(resolution,GEOMETRY_SPEC) ||
      IsSpec(resolution,VIEW_GEOM_SPEC) ||
      IsSpec(resolution,FIRE_GEOM_SPEC) ||
      IsSpec(resolution,VIEW_PILOT_GEOM_SPEC) ||
      IsSpec(resolution,VIEW_CARGO_GEOM_SPEC) ||
      IsSpec(resolution,VIEW_GUNNER_GEOM_SPEC) ||
      IsSpec(resolution,MEMORY_SPEC) ||
      IsSpec(resolution,LANDCONTACT_SPEC) ||
      IsSpec(resolution,ROADWAY_SPEC) ||
      IsSpec(resolution,PATHS_SPEC) ||
      IsSpec(resolution,HITPOINTS_SPEC)
    )
    {
      return GONoNormalsNoUV;
    }
    else if (IsSpecShadowVolume(resolution))
    {
      return GONoUV;
    }
    else if (IsSpecInterval(resolution, SHADOWBUFFER_SPEC, SHADOWBUFFER_MAX))
    {
      return GONoNormals;
    }
  }
  return GeometryOnly(0);
}

const int fullFlags = (MSFullLighted*ClipUserStep);
const int halfFlags = (MSHalfLighted*ClipUserStep);
const int ambFlags = (MSInShadow*ClipUserStep);
const int shineFlags = (MSShining*ClipUserStep);

struct OxygenLEOriginTag
{
  int prot_componumber;
  int prot_magicnumber;
  int prot_unlockcode;
  int prot_requestcode;
};


static RString ReadString(QIStream &f)
{
  char buf[4096];
  char *dst = buf;
  for(;;)
  {
    int c = f.get();
    if (c<0)
    {
      Fail("File read error");
      break;
    }
    if (c==0) break;
    if (dst<buf+sizeof(buf)-1)
    {
      *dst++=c;
    }
    else
    {
      buf[sizeof(buf)-1]=0;
      RptF("String overflow: %s",buf);
    }
  }
  *dst++=0;
  return buf;
}


static int FindSourceTriangle(unsigned short ta, unsigned short tb, unsigned short tc, const AutoArray<unsigned short> &indices)
{
  for (int i = 0; i < indices.Size(); i += 3)
  {
    unsigned short a = indices[i];
    unsigned short b = indices[i + 1];
    unsigned short c = indices[i + 2];
    if
    ( 
      // Triangles with various start
      ((ta == a) && (tb == b) && (tc == c)) ||
      ((ta == b) && (tb == c) && (tc == a)) ||
      ((ta == c) && (tb == a) && (tc == b)) ||
      // Degenerated triangles (if 2 of its vertices belongs to the triangle)
      ((ta == a) && (tb == a)) ||
      ((tb == a) && (tc == a)) ||
      ((tc == a) && (ta == a)) ||
      ((ta == b) && (tb == b)) ||
      ((tb == b) && (tc == b)) ||
      ((tc == b) && (ta == b)) ||
      ((ta == c) && (tb == c)) ||
      ((tb == c) && (tc == c)) ||
      ((tc == c) && (ta == c))
    ) return i/3;
  }
  return -1;
}

/// pair of original texture name and a resolved texture
struct ResolvedTextureName
{
  RString _name;
  Ref<Texture> _texture;
  
  const char *GetName() const {return _name;}
  
  ResolvedTextureName(){}
  ResolvedTextureName(RString name);
};

template <>
struct FindArrayKeyTraits<ResolvedTextureName>
{
  typedef const char *KeyType;
  static bool IsEqual(KeyType a, KeyType b) {return !strcmpi(a,b);}
  static KeyType GetKey(const ResolvedTextureName &a) {return a.GetName();}
};




TypeIsMovableZeroed(ResolvedTextureName)

ResolvedTextureName::ResolvedTextureName(RString name)
:_name(name)
{
  // TODO search across folder structure
  // perform file conversion if necessary
  _texture = GlobLoadTexture(GlobResolveTexture(name));
}

const ModelTarget ModelTarget::_defaultValue = ModelTarget();

ModelTarget::ModelTarget()
{
  _bonesInterval = 55;
}

struct FaceUV
{
  UVPair _uv[4];
};
TypeIsSimple(FaceUV);

/*!
\patch 2.01 Date 12/16/2002 by Ondra
- New: Support for materials including detail textures.
*/

float Shape::LoadTagged(
  QIStream &f, bool reversed, int ver, GeometryOnly geometryOnly,
  AutoArray<float> &massArray, bool tagged, const LODShape *lodShape, int level
)
{
  _loadWarning = false;
  _face.Clear();

  bool extended = false;
  bool material = false;
  DataHeaderEx head;
  f.read((char *)&head,sizeof(head));
  if (f.fail() || strncmp(head.magic,"SP3X",4))
  {
    // it might be a new O2 format ID:
    if (!strncmp(head.magic,"P3DM",4))
    {
      extended = true;
      material = true;
      if (head.version!=0x100)
      {
        RptF("Unsupported version %x",head.version);
        _loadWarning = true;
      }
      // skip rest of header
      f.seekg(head.headSize-sizeof(head),QIOS::cur);
    }
    else
    {
      // is not extended format, try old
      f.seekg(-(int)sizeof(head),QIOS::cur);
      DataHeader oHead;
      f.read((char *)&oHead,sizeof(oHead));
      if( f.fail() || strncmp(oHead.magic,"SP3D",4) )
      {
        char magicName[5];
        strncpy(magicName,head.magic,4);
        magicName[4]=0;
        WarningMessage("Bad file format (%s).",magicName);
        return -1; // file input error
      }
      head.version = 0;
      head.nPos = oHead.nPos;
      head.nNorm = oHead.nNorm;
      head.nFace = oHead.nFace;
      head.flags = 0;
      head.headSize = sizeof(oHead);
    }
  }
  else
  {
    extended=true;
    // skip rest of header
    f.seekg(head.headSize-sizeof(head),QIOS::cur);
  }
  
  if (head.nFace>0)
  {
    _face._data = new FaceData;
    _face._data->_faces.Realloc(head.nFace);
  }

  //bool specialLod=false;

  InitVertexTableData();    
  _data->_pos.Realloc(head.nPos);
  _data->_norm.Realloc(head.nNorm);
  _data->_clip.Realloc(head.nPos);
  _nVertex = 0;
  _faceArea = FLT_SNAN;

  #define maxPoints 2048
  AUTO_STATIC_ARRAY(Vector3,pos,maxPoints);
  AUTO_STATIC_ARRAY(Vector3,norm,maxPoints);
  AUTO_STATIC_ARRAY(ClipFlags,clip,maxPoints);
  AUTO_STATIC_ARRAY(bool,hidden,maxPoints);
  pos.Resize(head.nPos);
  clip.Resize(head.nPos);
  hidden.Resize(head.nPos);
  norm.Resize(head.nNorm);
  _pointToVertex.Resize(head.nPos);
  _vertexToPoint.Realloc(head.nPos);
  for( int i=0; i<_pointToVertex.Size(); i++ ) _pointToVertex[i]=-1;

  //if (head.version)

  // load points, normals and mapping pairs
  //bool hiddenFace=false;
  int reportInvalid = 5;
  for( int i=0; i<head.nPos; i++ )
  {
    if( !extended )
    {
      DataPoint src;
      f.read((char *)&src,sizeof(src));
      if( f.fail() || f.eof()) break;
      //SetPos(i)=Vector3(src.X(),src.Y(),src.Z());
      //SetClip(i,ClipAll);
      pos[i]=Vector3(src.X(),src.Y(),src.Z());
      clip[i]=ClipAll;
      hidden[i]=false;
    }
    else
    {
      DataPointEx src;
      f.read((char *)&src,sizeof(src));
      if( f.fail() || f.eof()) break;
      
      ClipFlags hints=ClipAll;
      const int allFlags=
      (
        POINT_ONLAND|POINT_UNDERLAND|POINT_ABOVELAND|POINT_KEEPLAND
        |POINT_DECAL|POINT_VDECAL
        |POINT_NOLIGHT|POINT_FULLLIGHT|POINT_HALFLIGHT|POINT_AMBIENT
        |POINT_NOFOG|POINT_SKYFOG|POINT_USER_MASK
        |POINT_SPECIAL_MASK
      );
      hidden[i]=false;
      // check for valid flags - detect Objektiv bug
      if( src.flags&~allFlags )
      {
        if (--reportInvalid>=0)
        {
          RptF("Point %d: Invalid point flags %x.",i,src.flags);
          _loadWarning = true;
        }
        src.flags=0;
      }
      if( src.flags&allFlags )
      {
        if( src.flags&POINT_ONLAND ) hints|=ClipLandOn;
        //if( src.flags&POINT_ONLAND ) hints|=ClipLandKeep;
        else if( src.flags&POINT_UNDERLAND ) hints|=ClipLandUnder;
        else if( src.flags&POINT_ABOVELAND ) hints|=ClipLandAbove;
        else if( src.flags&POINT_KEEPLAND ) hints|=ClipLandKeep;
        
        if( src.flags&POINT_DECAL ) hints|=ClipDecalNormal;
        else if( src.flags&POINT_VDECAL ) hints|=ClipDecalVertical;

        // convert lighting flags to materials
        if( src.flags&POINT_NOLIGHT ) hints|=shineFlags;
        else if( src.flags&POINT_FULLLIGHT ) hints|=fullFlags;
        else if( src.flags&POINT_HALFLIGHT ) hints|=halfFlags;
        else if( src.flags&POINT_AMBIENT ) hints|=ambFlags;


        if( src.flags&POINT_NOFOG ) hints|=ClipFogDisable;
        else if( src.flags&POINT_SKYFOG ) hints|=ClipFogSky;

        if( src.flags&POINT_USER_MASK )
        {
          int user=(src.flags&POINT_USER_MASK)/POINT_USER_STEP;
          hints|=user*ClipUserStep;
        }
        if( src.flags&POINT_SPECIAL_HIDDEN )
        {
          hidden[i]=true;
        }

        /*
        if (hints&ClipUserMask)
        {
          LogF("point %d",(hints&ClipUserMask)/ClipUserStep);
        }
        */
      }
      if( reversed ) src[0]=-src[0],src[2]=-src[2]; 
      pos[i]=Vector3(src.X(),src.Y(),src.Z());
      clip[i]=hints;
    }
  }
  
  
  for( int i=0; i<head.nNorm; i++ )
  {
    DataNormal src;
    f.read((char *)&src,sizeof(src));
    if( f.fail() || f.eof()) break;
    if( reversed ) src[0]=-src[0],src[2]=-src[2];
    // normals in geometry ignored to avoid vertex multiplication
    if (geometryOnly&GONoNormals) src[0]=0, src[1]=1, src[2]=0;
    //SetNorm(i)=Vector3(src.X(),src.Y(),src.Z());
    norm[i]=Vector3(src.X(),src.Y(),src.Z());
  }
  
  int andSpecial=~0;
  _faceProperties.Realloc(head.nFace);
  _faceProperties.Resize(head.nFace);
  // load faces
  _allTexturesReady = false;

  AutoArray<int> faceToOriginalFace;
  faceToOriginalFace.Realloc(head.nFace);
  faceToOriginalFace.Resize(head.nFace);
  
  // faces, including texture names, attributes, in raw state
  AutoArray<DataFaceVar> faces;
  faces.Realloc(head.nFace);
  
  const int allFaceFlags=
  (
    FACE_NOLIGHT|FACE_AMBIENT|FACE_FULLLIGHT|FACE_BOTHSIDESLIGHT|FACE_SKYLIGHT
    |FACE_REVERSELIGHT|FACE_FLATLIGHT
    |FACE_ISSHADOW|FACE_NOSHADOW
    |FACE_DISABLE_TEXMERGE|FACE_USER_MASK|FACE_Z_BIAS_MASK
    |FACE_COLORIZE_MASK|FACE_FANSTRIP_MASK
  );
  
  // resolve all texture names
  // if any name is already resolved, do not resolve it again
  FindArrayKey<ResolvedTextureName> resolved;
  
  // to handle correctly the UV pairs, we need to split the cycle to several phases:
  // 1. read all faces to the local storage
  // 2. calculate minUV, maxUV
  // 3. create the vertices

  UVPair invUV; // make it visible in other TAGGS as well
  {
    AutoArray<DataFaceVar, MemAllocLocal<DataFaceVar, 1024> > srcFaces;
    srcFaces.Resize(head.nFace);

    // pass 1
    for( int i=0; i<head.nFace; i++ )
    {
      DataFaceVar &srcFace = srcFaces[i];
      if (material)
      {
        // variable texture length
        //<n> - 4 B
        //<vs> - 4xDataVertex 
        f.read(&srcFace.vert,sizeof(srcFace.vert));
        //<flags> - 4 B
        f.read(&srcFace.flags,sizeof(srcFace.flags));
        // read two zero-terminated strings
        srcFace.texture = ReadString(f);
        srcFace.material = ReadString(f);
      }
      else
      {
        if( !extended )
        {
          DataFace oFace;
          f.read((char *)&oFace,sizeof(oFace));
          if( f.fail() ) break;
          srcFace.vert = oFace.vert;
          srcFace.flags = 0;
          srcFace.texture = oFace.texture;
        }
        else
        {
          DataFaceEx oFace;
          f.read((char *)&oFace,sizeof(oFace));
          if( f.fail() ) break;
          srcFace.vert = oFace.vert;
          srcFace.flags = oFace.flags;
          srcFace.texture = oFace.texture;
        }
        
      }
      srcFace.texture.Lower();
      srcFace.material.Lower();

      // check for valid flags - detect Objektiv bug
      if( srcFace.flags&~allFaceFlags )
      {
        // some invalid flags
        if (--reportInvalid>=0)
        {
          RptF("Face %d, Invalid face flags %x",i,srcFace.flags);
          _loadWarning = true;
        }

        srcFace.flags=0; // reset - assume it is a result of Objektiv bug
      }

      // check and fix the UV
      int reportThisFaceUV = 1;
      for( int j=0; j<srcFace.vert.n; j++ )
      {
        int srcPoint=srcFace.vert.vs[j].point;
        float &srcU = srcFace.vert.vs[j].mapU;
        float &srcV = srcFace.vert.vs[j].mapV;

        // uv in geometry ignored to avoid vertex multiplication
        if (geometryOnly & GONoUV) srcU = srcV = 0;

        if (!(fabs(srcU)<1e5) || !(fabs(srcV)<1e5) || !_finite(srcU) || !_finite(srcV))
        {
          if (--reportThisFaceUV>=0 && --reportInvalid>=0)
          {
            RptF(
              "Face %d, point %d, face points %d,%d,%d - invalid uv %g,%g",
              i,srcPoint,
              srcFace.vert.vs[0].point,
              srcFace.vert.vs[1].point,
              srcFace.vert.vs[2].point,
              srcU,srcV
              );
            _loadWarning = true;
          }
          srcU=srcV=0;
        }

        // Report point in case U or V tex is too big
        const float UVLimit = 100.0f;
        if ((fabs(srcU) > UVLimit) || (fabs(srcV) > UVLimit))
        {
          RptF("Warning: UV coordinate on point %d is too big UV(%f, %f) - the UV compression may produce inaccurate results", srcPoint, srcU, srcV);
          _loadWarning = true;
        }
      }
    }

    // pass 2
    UVPair minUV, maxUV;
    minUV.u = FLT_MAX;
    minUV.v = FLT_MAX;
    maxUV.u = -FLT_MAX;
    maxUV.v = -FLT_MAX;
    for( int i=0; i<head.nFace; i++ )
    {
      DataFaceVar &srcFace = srcFaces[i];
      for( int j=0; j<srcFace.vert.n; j++ )
      {
        float srcU = srcFace.vert.vs[j].mapU;
        float srcV = srcFace.vert.vs[j].mapV;

        if (srcU > maxUV.u) maxUV.u = srcU;
        if (srcV > maxUV.v) maxUV.v = srcV;
        if (srcU < minUV.u) minUV.u = srcU;
        if (srcV < minUV.v) minUV.v = srcV;
      }
    }
    // no vertices?
    if (minUV.u > maxUV.u) {minUV.u = 0; maxUV.u = 1;}
    if (minUV.v > maxUV.v) {minUV.v = 0; maxUV.v = 1;}
    InitUV(invUV, minUV, maxUV);

    // pass 3
    for( int i=0; i<head.nFace; i++ )
    {
      DataFaceVar &srcFace = srcFaces[i];

      int spec=0;
      Poly poly;
      Ref<Texture> texture;
      if( !*srcFace.texture && !*srcFace.material)
      {
        texture=DefaultTexture;
        _textures.AddUnique(texture);
      }
      else if (!*srcFace.texture)
      {
      }
      else
      {
        int index = resolved.FindKey(srcFace.texture);
        if (index>=0)
        {
          texture = resolved[index]._texture;
        }
        else
        {
          const ResolvedTextureName &newTex = resolved.Append(ResolvedTextureName(srcFace.texture));
          texture = newTex._texture;
          _textures.Add(texture);
        }
      }
      Assert( !texture || _textures.Find(texture)>=0 );
      poly.SetN(srcFace.vert.n);
      PolyProperties &prop = _faceProperties[i];
      prop.Init();
      prop.SetTexture(texture);
      bool skipPoly=true;

      ClipFlags clipLightAttr=0;
      if( srcFace.flags&allFaceFlags )
      {
        if( srcFace.flags&FACE_NOLIGHT ) clipLightAttr|=shineFlags;
        else if( srcFace.flags&FACE_AMBIENT ) clipLightAttr|=ambFlags;
        else if( srcFace.flags&FACE_FULLLIGHT ) clipLightAttr|=fullFlags;
        if( srcFace.flags&FACE_ISSHADOW ) spec|=IsShadow;
        if( srcFace.flags&FACE_NOSHADOW ) spec|=NoShadow;
        //if( srcFace.flags&FACE_DISABLE_TEXMERGE ) spec|=NoTexMerger;
        if (srcFace.flags&FACE_USER_MASK)
        {
          int material = ((srcFace.flags&FACE_USER_MASK)>>FACE_USER_SHIFT)&0xff;
          clipLightAttr = material*ClipUserStep;
        }
        if (srcFace.flags&FACE_Z_BIAS_MASK)
        {
          int bias = (srcFace.flags&FACE_Z_BIAS_MASK)/FACE_Z_BIAS_STEP;
          spec |= ZBiasStep*bias;
        }
      }

      // check minimal material of all vertices
      int minMaterial = INT_MAX;
      int reportThisFaceNo = 1;
      for( int j=0; j<srcFace.vert.n; j++ )
      {
        int srcPoint=srcFace.vert.vs[j].point;
        int srcNormal=srcFace.vert.vs[j].normal;
        float srcU=srcFace.vert.vs[j].mapU;
        float srcV=srcFace.vert.vs[j].mapV;

        // TODO: remove hidden points?
        if( !hidden[srcPoint] )
        {
          skipPoly=false;
        }

        ClipFlags c = clip[srcPoint];
        if (clipLightAttr)
        {
          // combine material with old flags
          // shining is offset +20
          // inShadow is offset +40
          ClipFlags user = c&ClipUserMask;
          if (user)
          {
            // note: offsetting is quite a hack
            // we should rather reserve some bits for special lighting
            // and some leave to user material definition
            int offset = 0;
            if (user==shineFlags) offset = 20;
            else if (user==ambFlags) offset = 40;
            c &= ~ClipUserMask;
            c |= clipLightAttr + offset * ClipUserStep;
          }
          else
          {
            c |= clipLightAttr;
          }
        }
        ClipFlags fog=c&ClipFogMask;
        if( fog==ClipFogSky || fog==ClipFogDisable ) c&=~ClipBack;

        int mat = (c&ClipUserMask)/ClipUserStep;
        if (minMaterial>mat) minMaterial = mat;


        // check the normal and fix it if needed
        Vector3 &normal = norm[srcNormal];
        if
        (
          !_finite(normal[0]) || !(fabs(normal[0])<1.5) ||
          !_finite(normal[1]) || !(fabs(normal[1])<1.5) ||
          !_finite(normal[2]) || !(fabs(normal[2])<1.5)
        )
        {
          if (--reportThisFaceNo>=0 && --reportInvalid>=0)
          {
            RptF(
              "Face %d, point %d, face points %d,%d,%d - invalid normal %g,%g,%g",
              i,srcPoint,
              srcFace.vert.vs[0].point,
              srcFace.vert.vs[1].point,
              srcFace.vert.vs[2].point,
              normal[0],normal[1],normal[2]
            );
            _loadWarning = true;
          }
          normal[0]=0, normal[1]=1, normal[2]=0;
        }
        else if (normal.SquareSize()<Square(0.1f))
        {
          if (--reportThisFaceNo>=0 && --reportInvalid>=0)
          {
            RptF(
              "Warning: %s:%s Face %d, point %d, face points %d,%d,%d - very small normal %g,%g,%g",
              lodShape->Name(),
              lodShape->LevelName(lodShape->Resolution(level)),
              i,srcPoint,
              srcFace.vert.vs[0].point,
              srcFace.vert.vs[1].point,
              srcFace.vert.vs[2].point,
              normal[0],normal[1],normal[2]
            );
            _loadWarning = true;
          }
        }

        //
        int vertex=AddVertex(
          pos[srcPoint], normal, c,
          invUV, srcU, srcV,
          &_vertexToPoint, srcPoint
        );

        if (vertex >= 32767)
        {
          WarningMessage("Error: %s: Too many vertices", lodShape->Name());
          _loadWarning = true;
          return -1;
        }


        #if VERBOSE>2
          LogF
          (
            "    vertex %d from %d,%d, clip %x",
            vertex,srcPoint,srcNormal,clip[srcPoint]
          );
        #endif

        poly.Set(j,vertex);

        _vertexToPoint.Access(vertex);
        _vertexToPoint[vertex]=srcPoint;
        _pointToVertex[srcPoint]=vertex; // Objektiv point index to vertex index conversion

        //const Poly &poly = _shape->Face(o);
      }
        
      // if material is defined in model, use it
      if (srcFace.material.GetLength()>0)
      {
        Ref<TexMaterial> mat = GTexMaterialBank.New(TexMaterialName(srcFace.material));
        prop.SetMaterialExt(mat);
      }
      
      if (minMaterial!=0)
      {
        RptF(
          "Old style material flags used - %d (with texture %s)",
          minMaterial,prop.GetTexture() ? cc_cast(prop.GetTexture()->Name()) : "<null>"
        );
        _loadWarning = true;
      }

      poly.Reverse(); // models have reversed geometry

      // lighting attributes must be applied to all vertices as well
      for( int j=0; j<srcFace.vert.n; j++ )
      {
        int srcPoint=srcFace.vert.vs[j].point;
        ClipFlags c=clip[srcPoint];
        //ClipFlags c=Clip(srcPoint);
        ClipFlags land=c&ClipLandMask;
        if( land==ClipLandOn ) spec|=OnSurface|NoZWrite;
      }

      // Determine pixel shader uses discretization. In such case set the IsTransparent flag.
      // In other case let the texture decide. Note that even faces with without texture->IsTransparent()
      // flag (like tree branches) could use the alpha testing now. But it shouldn't cause any visual
      // problem and moreover we save the alpha testing switching.
      TexMaterial *mat = prop.GetMaterialExt();
      if (mat && mat->IsDiscretized())
      {
        spec|=IsTransparent;
      }
      else
      {
        if (texture)
        {
          texture->LoadHeaders();
          if( texture->IsAlpha() )
          {
            spec |= IsAlpha|IsAlphaOrdered;
          }
          if( texture->IsTransparent() )
          {
            spec|=IsTransparent;
          }
          if (texture->IsAnimated()) spec|=::IsAnimated;
          // TODO: replace ".paa" check with alpha order Objektive flags
          // trick - disable alpha ordering by extension
          const char *name = texture->Name();
          const char *ext = GetFileExt(name);
          // do not sort .paa, but do sort _co.paa
          if (!strcmpi(ext,".paa") && ((ext<name+3) || strcmpi(ext-3,"_co")))
          {
            // another trick - merged textures should not be alpha ordered
            if (!strstr(texture->Name(),"\\000"))
            {
              spec |= IsAlphaOrdered;
            }
          }
          // check special case ".paa"
        }
      }

      if( skipPoly )
      {
        // this is currently used only for proxy objects
        // it is safe to disable texture merging on them
        spec|=IsHidden;
      }
      if (geometryOnly&GONoUV)
      {
        // no UV - no need to clamp
        spec &= ~(ClampU|ClampV);
        spec |= NoClamp;
      }

      prop.SetSpecial(spec);
      _special|=spec;
      andSpecial&=spec;
      _face.Add(poly);
      faceToOriginalFace[i] = i;
    }
  } // basic geometry loaded

  // we need to keep number of original faces for selection loading  
  int nOrigFace = _faceProperties.Size();
  // some materials may be multi passed - unwind the pass loop
  {
    int i;
    Offset o;
    for (i=0,o=_face.Begin(); i<nOrigFace; i++,_face.Next(o))
    {
      const PolyProperties &prop = _faceProperties[i];
      const Poly &face = _face[o];
      if (!prop.GetMaterialExt()) continue;
      TexMaterial *nextPass = prop.GetMaterialExt()->NextPass();
      if (!nextPass) continue;
      // copy the face and its properties
      // temporary copy is required, as references may become invalid
      PolyProperties tProp = prop;
      Poly tFace = face;
      int passCount = 1;
      while (nextPass)
      {
        tProp.SetMaterialExt(nextPass);
        _faceProperties.Add(tProp);
        _face.Add(tFace);
        faceToOriginalFace.Add(i);
        Assert(faceToOriginalFace.Size()==_faceProperties.Size());
        Assert(_face.Size()==_faceProperties.Size());
        nextPass = nextPass->NextPass();
        passCount++;
      }
      if (passCount>2)
      {
        RptF(
          "More than 2-pass material used - %s (%d passes)",
          cc_cast(tProp.GetMaterialExt()->GetName()._name),passCount
        );
        _loadWarning = true;
      }
    }
  }
  _face.Compact();
  _faceProperties.Compact();

  // make sure all points are represented with vertices

  for( int i=0; i<_pointToVertex.Size(); i++ )
  {
    //if( _pointToVertex[i]<0 && !hidden[i] )
    if (_pointToVertex[i]<0)
    {
      int vertex=AddVertex(
        pos[i],VZero,clip[i], // normal set to zero - should not be used
        invUV, 0, 0, // no u-v mapping
        &_vertexToPoint,i
      );
      if (vertex >= 32767)
      {
        WarningMessage("Error: %s: Too many vertices", lodShape->Name());
        _loadWarning = true;
        return -1;
      }
      _pointToVertex[i]=vertex;
      _vertexToPoint.Access(vertex);
      _vertexToPoint[vertex]=i;
    }
  }

  _data->_clip.Condense();
  _data->_norm.Condense();
  _data->_tex.Condense();
  
  Assert(CheckIntegrity());

//  if (geometryOnly==0)
//  {
//    // no sorting for geometry - this way we will keep identity
//    SortVertices(lodShape, level);
//  }

  if (NFaces()==0)
  {
    andSpecial = 0;
  }
  
  _special&=IsAlpha|IsTransparent|::IsAnimated|OnSurface;
  _special|=andSpecial&(NoShadow|ZBiasMask);
  CalculateHints(); // VertexTable optimization
  //if( GetOrHints()&ClipLandOn ) _special|=OnSurface;
  #if !_RELEASE
    if ((GetOrHints()&ClipLandOn) && NFaces()>0)
    {
      Assert( _special&OnSurface);
    }
  #endif
  if( f.fail() )
  {
    WarningMessage("Bad object.");
    Clear();
    return -1; // file input error
  }
  _data->_pos.Compact();
  _data->_norm.Compact();
  _data->_clip.Compact();
  _vertexToPoint.Compact();
  _pointToVertex.Compact();
  AutoClamp();
  // recalculate areas cannot be called now, as no sections are defined yet
  //RecalculateAreas();
  RecalculateNormals(true);
  CalculateMinMax();
  CalculateColor();

  // skip all tags (if any), terminate at #EndOfFile#
  // check for TAGG magic
  char magic[4];
  f.read(magic,sizeof(magic));
  if( f.fail() || f.eof() )
  {
    WarningMessage("Error loading tag");
    _loadWarning = true;
    return -1;
  }
  if( strncmp("TAGG",magic,sizeof(magic)) )
  {
    WarningMessage("Bad format");
    _loadWarning = true;
    return -1;
  }

  _sel.Clear(); // clear selections
  for(;;)
  {
    int tagSize;
    RString tagName;
    if( !LoadTag(f,tagName,tagSize) ) break;
    else if( !strcmpi(tagName,"#EndOfFile#") ) break;
    else if( !strcmpi(tagName,"#Mass#") )
    {
      if( ver==0 )
      {
        LogF("%s: Old mass no longer supported");
        f.seekg(tagSize,QIOS::cur); // skip tag
      }
      else
      {
        // only geometry level stores mass information in 1.xx
        int nMass=_pointToVertex.Size();
        Assert( tagSize==(int)sizeof(float)*nMass);
        Assert( massArray.Size()==0 );
        if( nMass>0 )
        {
          massArray.Realloc(nMass);
          massArray.Resize(nMass);
          f.read(massArray.Data(),sizeof(float)*nMass);
        }
      }
    }
    else if( !strcmpi(tagName,"#Animation#") )
    {
      int pSize=_pointToVertex.Size();
      Temp<DataVec> animTemp(pSize);
      AnimationPhase anim;
      float time=0;
      Assert( (size_t)tagSize==sizeof(*animTemp.Data())*pSize+sizeof(time) );
      f.read((char *)&time,sizeof(time));
      f.read((char *)animTemp.Data(),sizeof(*animTemp.Data())*pSize);
      anim.Resize(NPos());
      for( int a=0; a<NPos(); a++ )
      {
        int vp = _vertexToPoint[a];
        Assert (vp>=0);
        const DataVec &at=animTemp[vp];
        anim[a]=Vector3(at.X(),at.Y(),at.Z());
        if( reversed ) anim[a][0]=-anim[a][0],anim[a][2]=-anim[a][2]; 
      }
      anim.SetTime(time);
      AddPhase(anim);
    }
    else if(!strcmpi(tagName,"#UVSet#"))
    {
      int readBytes = 0;
      int stageID;
      f.read((char*)&stageID, sizeof(stageID));
      readBytes += sizeof(stageID);
      if (stageID > 1)
      {
        RptF("Warning: Unsupported UVSet %d", stageID);
      }
      if (stageID == 1)
      {
        if (geometryOnly&GONoUV)
        {
          RptF("Warning: special LOD contains 2nd UV set.");
          _loadWarning = true;
        }

        // Temporary array of uncompressed UVs
        AutoArray< UVPair, MemAllocLocal<UVPair, 2048> > uv1;
        uv1.Resize(_data->_tex.Size());

        // Read the uv set and remember it in temporary face array. Set also the _texCoord
        // regardless the fact there might be some collisions.
        AutoArray<FaceUV> faceUV;
        {
          // Allocate the temporary array
          faceUV.Realloc(nOrigFace);
          faceUV.Resize(nOrigFace);

          // make sure isolated vertices have UV set as well
          for (int i=0; i<_data->_tex.Size(); i++)
          {
            uv1[i].u = 0;
            uv1[i].v = 0;
          }

          // Go through all the faces (only original ones)
          Offset i;
          int faceIndex;
          for (faceIndex = 0, i = BeginFaces(); faceIndex < nOrigFace; faceIndex++, NextFace(i))
          {
            const Poly &originalFace = _face[i];
            Poly face = _face[i];
            face.Reverse(); // Make the reverse on local copy to be in the right order again
            int reportThisFaceUV2 = 1;
            for (int v = 0; v < face.N(); v++)
            {
              // Read the UV
              UVPair uvp;
              f.read(&uvp.u, sizeof(uvp.u));
              readBytes += sizeof(uvp.u);
              f.read(&uvp.v, sizeof(uvp.v));
              readBytes += sizeof(uvp.v);

              // Remember the vertex index
              VertexIndex vi = face.GetVertex(v);

              // Save the UV in _texCoord array
              
              if (!(fabs(uvp.u)<1e5) || !(fabs(uvp.v)<1e5) || !_finite(uvp.u) || !_finite(uvp.v))
              {
                if (--reportThisFaceUV2>=0 && --reportInvalid>=0)
                {
                  RptF(
                    "Face %d, face points %d,%d,%d - invalid uv(2) %g,%g",
                    i,
                    _vertexToPoint[originalFace.GetVertex(0)],
                    _vertexToPoint[originalFace.GetVertex(1)],
                    _vertexToPoint[originalFace.GetVertex(2)],
                    uvp.u,uvp.v
                  );
                  _loadWarning = true;
                }
                uvp.u = uvp.v = 0;
              }
              uv1[vi] = uvp;

              // Find the vertex reference in original face and save the UV in original
              // face vertex order in temporary array
              for (int ov = 0; ov < originalFace.N(); ov++)
              {
                if (originalFace.GetVertex(ov) == vi)
                {
                  faceUV[faceIndex]._uv[ov] = uvp;
                }
              }
            }
          }
          DoAssert(readBytes == tagSize);
        }

        // find the parameters for compression
        UVPair minUV1, maxUV1, invUV1;
        minUV1.u = FLT_MAX;
        minUV1.v = FLT_MAX;
        maxUV1.u = -FLT_MAX;
        maxUV1.v = -FLT_MAX;
        for (int i=0; i<uv1.Size(); i++)
        {
          if (uv1[i].u > maxUV1.u) maxUV1.u = uv1[i].u;
          if (uv1[i].v > maxUV1.v) maxUV1.v = uv1[i].v;
          if (uv1[i].u < minUV1.u) minUV1.u = uv1[i].u;
          if (uv1[i].v < minUV1.v) minUV1.v = uv1[i].v;
        }
        InitUV(1, invUV1, minUV1, maxUV1);

        // Allocate the tex-coord array
        _data->_texCoord[0].Realloc(_data->_tex.Size());
        _data->_texCoord[0].Resize(_data->_tex.Size());

        // set the compressed uv
        for (int i=0; i<uv1.Size(); i++)
        {
          _data->_texCoord[0].SetUV(i, invUV1, uv1[i].u, uv1[i].v);
        }

        // Check for collisions (that means faces which doesn't have matching extended UV's) and replicate such vertices
        {
          // Go through all the faces (only original ones). In case the UV values differs,
          // replicate the vertex
          Offset i;
          int faceIndex;
          for (faceIndex = 0, i = BeginFaces(); faceIndex < nOrigFace; faceIndex++, NextFace(i))
          {
            Poly &originalFace = _face[i];
            for (int v = 0; v < originalFace.N(); v++)
            {
              VertexIndex vi = originalFace.GetVertex(v);
              if (uv1[vi] != faceUV[faceIndex]._uv[v])
              {
                // Remember the number of vertices
                int oldNPos = NPos();

                // Either add new vertex or find one with the same values (including new UV set)
                // avoid invalid references by copying the values
                Vector3 posVi = _data->_pos[vi];
                Vector3 normVi = _data->_norm[vi].Get();
                DoAssert(_finite(faceUV[faceIndex]._uv[v].u));
                DoAssert(_finite(faceUV[faceIndex]._uv[v].v));
                int newVI = AddVertex(posVi, normVi, _data->_clip[vi], invUV, U(vi), V(vi),
                  NULL, -1, PosEpsilon, NormEpsilon, &faceUV[faceIndex]._uv[v], &invUV1);

                // If vertex was added, we must update the vertex references
                if (oldNPos != NPos())
                {
                  UpdateVertexReferences(vi, newVI);
                }

                // Update the vertex reference in original face
                originalFace.Set(v, newVI);
              }
            }
          }
          _data->_texCoord[0].Condense();

          // TODO: Go through the rest of the faces (multipass) and update their vertex references
        }

        if (geometryOnly&GONoUV)
        {
          _data->_texCoord[0].Clear();
        }

        // Report in case the UV values are suspicious
        {
          const float uvBorder = 50.0f;
          if (minUV1.u < -uvBorder || minUV1.v < -uvBorder || maxUV1.u > uvBorder || maxUV1.v > uvBorder)
          {
            RPTSTACK();
            RptF("Warning: Suspicious values in the UV set 1 (minU=%f, minV=%f, maxU=%f, maxV=%f). The UV set may appear broken because of the compression.", minUV1.u, minUV1.v, maxUV1.u, maxUV1.v);
          }
        }
      }
      else
      {
        // Skip the stage (either 0 or unsupported)
        f.seekg(tagSize - sizeof(stageID),QIOS::cur);
      }
    }
    else if( !strcmpi(tagName,"#Property#") )
    {
      struct {char name[64],value[64];} prop;
      Assert( (size_t)tagSize==sizeof(prop) );
      f.read((char *)&prop,sizeof(prop));
      strlwr(prop.name);
      strlwr(prop.value);
      _prop.Add(NamedProperty(prop.name,prop.value));
    }
    else if( !strcmpi(tagName,"#MaterialIndex#") )
    {
      OxygenLEOriginTag originTag;
      Assert( (size_t)tagSize==sizeof(originTag) );
      f.read(&originTag,sizeof(originTag));
      // convert to named properties

      _prop.Add(NamedProperty("__ambient",Format("08x",originTag.prot_componumber)));
      _prop.Add(NamedProperty("__diffuse",Format("08x",originTag.prot_magicnumber)));
      _prop.Add(NamedProperty("__specular",Format("08x",originTag.prot_unlockcode)));
      _prop.Add(NamedProperty("__emissive",Format("08x",originTag.prot_requestcode)));
    }
    else if( *tagName!='#' ) // named selection
    {
      int pSize=(int)sizeof(bool)*_pointToVertex.Size();
      int fSize=(int)sizeof(bool)*nOrigFace;
      if( tagSize!=pSize+fSize )
      {
        _loadWarning = true;
        RptF("Invalid named selection %s",(const char *)tagName);
        f.seekg(tagSize,QIOS::cur); // skip tag
      }
      else if( tagName[0]=='-' || tagName[0]=='.' )
      {
        // ignore selection
        f.seekg(tagSize,QIOS::cur); // skip tag
      }
      else
      {
        Temp<byte> tempPoints(pSize);
        Temp<bool> tempFaces(nOrigFace);
        f.read(tempPoints.Data(),pSize);
        f.read(tempFaces.Data(),fSize);
        //f.seekg(fSize,QIOS::cur);
        AUTO_STATIC_ARRAY(SelInfo,selPoints,2048);
        AUTO_STATIC_ARRAY(VertexIndex,selFaces,2048);
        int i;
        for( i=0; i<NPos(); i++ )
        {
          int vp = _vertexToPoint[i];
          Assert (vp>=0);
          byte val=tempPoints[vp];
          if( val ) selPoints.Add(SelInfo(i,-val));
        }

        for( i=0; i<NFaces(); i++ )
        {
          int fi = faceToOriginalFace[i];
          if(tempFaces[fi]) selFaces.Add(i);
        }

        NamedSelection sel(
          tagName,
          selPoints.Data(),selPoints.Size(),
          selFaces.Data(),selFaces.Size()
        );
        //Log("Create intervals in %s:%s",Name(),tagName);
        //sel.CreateIntervals();
        AddNamedSel(sel);
      }
    }
    else f.seekg(tagSize,QIOS::cur); // skip tag
  }

  if (_phase.Size()>0)
  {
    // check if key frame animation is enabled
    bool phaseEnabled = atoi(PropertyValue("keyframe"))!=0;
    if (!phaseEnabled)
    {
      RptF("Key frame animation, no 'keyframe' property");
      _loadWarning = true;
    }
  }
  /*
  if (shape->_sel.Size()<=0)
  {
    // no selections - no point indices may be referred
    shape->_vertexToPoint.Clear();
    shape->_pointToVertex.Clear();
  }
  */

  Assert(CheckIntegrity());

  if (geometryOnly==0)
  {
    // no sorting for geometry - this way we will keep identity
    SortVertices(lodShape, level);
  }

  Compact();

  float resolution=0;
  f.read((char *)&resolution,sizeof(resolution));
  if( f.fail() || f.eof() ) resolution=0.0f;

  /*
  for (int i=0; i<_nVertex; i++)
  {
    Vector3Val norm = _norm[i];
    if (!norm.IsFinite() || !(norm.SquareSize()<1e3))
    {
      LogF("%d: Bad normal %g,%g,%g",i,norm[0],norm[1],norm[2]);
    }
  }

  if (stricmp(lodShape->Name(),"ca\\roads\\most_bez_lamp.p3d")==0 && level==0)
  {
    Vector3Val norm = _norm[51];
    LogF("Most normal 51,%g,%g,%g",norm.X(),norm.Y(),norm.Z());
  }
  */

  return resolution;
}

//!{ Full info face support (simplified structure for geometry manipulation)
//const int maxFaceVertices = MaxPoly;
const int maxFaceVertices = 32; // Select some reasonable estimation to avoid stack overflow f.i. in function CreateAlignedConvexComponents
struct FullInfoVertex
{
  Vector3 _pos;
  Vector3 _norm;
  UVPair _tex;
  UVPair _tex1;
  ClipFlags _clip;
  VertexIndex _vertexToPoint;
  VertexIndex _oldVertexIndex;
  //!{ Constructor not wanted from performance reasons
//   FullInfoVertex()
//   {
//     _pos = VZero;
//     _norm = VUp;
//     _tex.u = _tex.v = 0.0f;
//     _tex1.u = _tex1.v = 0.0f;
//     _clip = ClipNone;
//     _vertexToPoint = -1;
//     _oldVertexIndex = -1 ;
//   }
  //!}
};
struct FullInfoFace
{
  //! Name of the selection the face and its vertices belongs to
  RStringB _namedSelection;
  //! Properties of the face
  PolyProperties _properties;
  //! Up to 4 vertices of the face
  FullInfoVertex _vertex[maxFaceVertices];
  //! Number of vertices of the face
  int _nVertex;
  //!{ Constructor not wanted from performance reasons
//   //! Constructor
//   FullInfoFace()
//   {
//     _properties.Init();
//     _nVertex = 0;
//   }
  //!}
  //! Function to verify the polygon normal points up
  bool IsNormalUp() const
  {
    if (_nVertex > 2)
    {
      Vector3 pos0 = _vertex[_nVertex - 2]._pos;
      Vector3 pos1 = _vertex[_nVertex - 1]._pos;
      for (int i = 0; i < _nVertex; i++)
      {
        // Get the pos2
        Vector3 pos2 = _vertex[i]._pos;

        // Calculate normal
        Vector3 vA = (pos1 - pos0).Normalized();
        Vector3 vB = (pos2 - pos0).Normalized();
        Vector3 n = vA.CrossProduct(vB).Normalized();

        // If normal is not pointing up, then forget the whole polygon and finish
        if (n.Y() < 0.001f)
        {
          return false;
        }

        // Advance to next item
        pos0 = pos1;
        pos1 = pos2;
      }

      // All polygon triangles point up
      return true;
    }
    else
    {
      // Normal cannot be calculated, say normal doesn't point up
      return false;
    }
  }
  ClassIsMovable(FullInfoFace);
};
void ConvertToFullInfoFaces(const Shape &s, StaticArrayAuto<FullInfoFace> &faces, bool secondUVStagePresent)
{
  // Clear the output structure
  //faces.Realloc(s.NFaces()); // Reallocation not wanted as long as faces is a static array
  faces.Resize(s.NFaces());

  // Prepare empty UV
  UVPair empty;
  empty.u = 0.0f;
  empty.v = 0.0f;

  // Fill out the structure with data
  {
    int fi;
    Offset i, e;
    for (i=s.BeginFaces(), e=s.EndFaces(), fi=0; i < e; s.NextFace(i), fi++)
    {
      const Poly &face = s.Face(i);
      FullInfoFace &fif = faces[fi];
      if (fi < s.FacePropertiesSize()) fif._properties = s.FaceProperties(fi); else fif._properties.Init();
      fif._nVertex = face.N();
      DoAssert(fif._nVertex <= maxFaceVertices);
      for (int v = 0; v < fif._nVertex; v++)
      {
        fif._vertex[v]._pos = s.Pos(face.GetVertex(v));
        fif._vertex[v]._norm = s.Norm(face.GetVertex(v)).Get();
        fif._vertex[v]._tex = s.UV(face.GetVertex(v));
        fif._vertex[v]._tex1 = secondUVStagePresent ? s.UV(1, face.GetVertex(v)) : empty;
        fif._vertex[v]._clip = s.Clip(face.GetVertex(v));
        fif._vertex[v]._vertexToPoint = s.VertexToPoint(face.GetVertex(v));
        fif._vertex[v]._oldVertexIndex = face.GetVertex(v);
      }
    }
  }
}
// This function restores geometry from array of FullInfoFace faces. Note that proper selection creation depends on order of faces - they must not switch from selection to another and back
void RestoreFromFullInfoFaces(Shape &s, StaticArrayAuto<FullInfoFace> &faces, bool secondUVStagePresent, bool fast = false)
{
  // Array to store selections
  AUTO_STATIC_ARRAY(SelInfo,selPoints,2048);
  AUTO_STATIC_ARRAY(VertexIndex,selFaces,2048);
  RStringB currentSelectionName;

  // Clear the reduced shape arrays
  s.ClearFaces();
  s.ClearFaceProperties();
  s.ReleaseTables();
  s.ClearVertexToPoint();

  // Allocate the reduced shape arrays
  s.Reserve(faces.Size() * 5); // It is a good upper estimation, that each face has got 5 points
  s.ReserveFaces(faces.Size());
  s.ReserveFaceProperties(faces.Size());

  // in the first pass, find how UV will be compressed
  UVPair invUV, minUV, maxUV;
  UVPair invUV1, minUV1, maxUV1;
  minUV.u = FLT_MAX;
  minUV.v = FLT_MAX;
  maxUV.u = -FLT_MAX;
  maxUV.v = -FLT_MAX;
  if (secondUVStagePresent)
  {
    minUV1.u = FLT_MAX;
    minUV1.v = FLT_MAX;
    maxUV1.u = -FLT_MAX;
    maxUV1.v = -FLT_MAX;
  }
  for (int i=0; i<faces.Size(); i++)
  {
    const FullInfoFace &fif = faces[i];
    for (int v = 0; v < fif._nVertex; v++)
    {
      const FullInfoVertex &fiv = fif._vertex[v];

      if (fiv._tex.u > maxUV.u) maxUV.u = fiv._tex.u;
      if (fiv._tex.v > maxUV.v) maxUV.v = fiv._tex.v;
      if (fiv._tex.u < minUV.u) minUV.u = fiv._tex.u;
      if (fiv._tex.v < minUV.v) minUV.v = fiv._tex.v;
      if (secondUVStagePresent)
      {
        if (fiv._tex1.u > maxUV1.u) maxUV1.u = fiv._tex1.u;
        if (fiv._tex1.v > maxUV1.v) maxUV1.v = fiv._tex1.v;
        if (fiv._tex1.u < minUV1.u) minUV1.u = fiv._tex1.u;
        if (fiv._tex1.v < minUV1.v) minUV1.v = fiv._tex1.v;
      }
    }
  }
  s.InitUV(invUV, minUV, maxUV);
  if (secondUVStagePresent) s.InitUV(1, invUV1, minUV1, maxUV1);

  // Create the reduced shape arrays
  for (int i = 0; i < faces.Size(); i++)
  {
    const FullInfoFace &fif = faces[i];

    // If face selection doesn't correspond to actual one, flush the actual one
    {
      if (currentSelectionName != fif._namedSelection)
      {
        if (!currentSelectionName.IsEmpty()) s.AddNamedSel(NamedSelection(currentSelectionName, selPoints.Data(), selPoints.Size(), selFaces.Data(), selFaces.Size()));
        selPoints.Clear();
        selFaces.Clear();
        currentSelectionName = fif._namedSelection;
      }
    }

    // Add points and faces
    Poly poly;
    poly.SetN(fif._nVertex);
    for (int v = 0; v < fif._nVertex; v++)
    {
      const FullInfoVertex &fiv = fif._vertex[v];
      float pEps, nEps;
      if (fast)
      {
        pEps = -1.0f;
        nEps = -1.0f;
      }
      else
      {
        pEps = PosEpsilon;
        nEps = NormEpsilon;
      }
      int vertexIndex;
      if (secondUVStagePresent)
      {
        vertexIndex = s.AddVertex(fiv._pos, fiv._norm, fiv._clip, invUV, fiv._tex.u, fiv._tex.v, NULL, -1, pEps, nEps, &fiv._tex1, &invUV1);
      }
      else
      {
        vertexIndex = s.AddVertex(fiv._pos, fiv._norm, fiv._clip, invUV, fiv._tex.u, fiv._tex.v, NULL, -1, pEps, nEps);
      }
      poly.Set(v, vertexIndex);
      s.SetVertexToPoint(vertexIndex, fiv._vertexToPoint);

      // Update the point selection
      selPoints.Add(SelInfo(vertexIndex,255));
    }
    s.AddFace(poly, fif._properties);

    // Update the face selection
    selFaces.Add(s.NFaces() - 1);
  }

  // Flush the last selection
  {
    if (!currentSelectionName.IsEmpty()) s.AddNamedSel(NamedSelection(currentSelectionName, selPoints.Data(), selPoints.Size(), selFaces.Data(), selFaces.Size()));
    selPoints.Clear();
    selFaces.Clear();
    currentSelectionName = RStringB();
  }

  // Compact arrays
  s.Compact();

  // Condense arrays
  s.CondenseNorm();
  s.CondenseTex();
  s.CondenseClip();
}
//!}

bool MaterialSupportsDoubleSiding(const TexMaterial *m)
{
  // Detect trees
  VertexShaderID vs = m->GetVertexShaderID();
  if (vs == VSNormalMapThrough ||
      vs == VSNormalMapSpecularThrough ||
      vs == VSNormalMapThroughNoFade ||
      vs == VSNormalMapSpecularThroughNoFade ||
      vs == VSTree ||
      vs == VSTreeNoFade ||
      vs == VSTreePRT ||
      vs == VSTreePRTNoFade ||
      vs == VSTreeAdv ||
      vs == VSTreeAdvTrunk
      )
  {
    return true;
  }

//   // Detect grass
//   if (m->GetPixelShaderID() == PSGrass)
//   {
//     return true;
//   }

  // No double siding supported
  return false;
}

void Shape::DeleteBackFaces()
{
  // Do the back face deletion only in case no selections are on the model (typical for trees - 
  // we need not to update the selections this way)
  if (_sel.Size() > 0) return;

  // Make sure the size of the both face arrays are equal
  DoAssert(_face.Size() == _faceProperties.Size());
  if (_face.Size() != _faceProperties.Size()) return;

  // Determine whether we use the second stage or not
  bool secondUVStagePresent = (NUVStages() > 1);

  // Go through the faces and find at least one double siding shader. If no such on is found then return.
  // This way we want to skip face deletion process for all the models apart of certain set like trees or grass
  {
    bool doubleSidingMaterialFound = false;
    for (int i = 0; i < _faceProperties.Size(); i++)
    {
      const TexMaterial *m = _faceProperties[i].GetMaterialExt();
      if (m)
      {
        if (MaterialSupportsDoubleSiding(m))
        {
          doubleSidingMaterialFound = true;
          break;
        }
      }
    }
    if (!doubleSidingMaterialFound) return;
  }

  // Create one structure that holds information about all the faces and vertices. This structure will
  // be later reduced (faces removed) and the shape structures like faces, pos, norm will be reconstructed
  // from it
  AUTO_STATIC_ARRAY(FullInfoFace,faces,512);
  ConvertToFullInfoFaces(*this, faces, secondUVStagePresent);

  // Initialize array with deleted flags
  AutoArray<bool> deleteFaces;
  deleteFaces.Realloc(faces.Size());
  deleteFaces.Resize(faces.Size());
  for (int i = 0; i < deleteFaces.Size(); i++) deleteFaces[i] = false;

  // Delete the back faces
  for (int i = 0; i < faces.Size(); i++) // faces.Size() must not be precomputed as it may change in the cycle
  {
    // Skip face in case it was marked as deleted
    if (deleteFaces[i]) continue;

    // Take the face - we can use reference here as we don't resize the array
    const FullInfoFace &fif = faces[i];

    // If material of the face doesn't support double siding then skip it
    const TexMaterial *m = fif._properties.GetMaterialExt();
    if (!m) continue;
    if (!MaterialSupportsDoubleSiding(m)) continue;

    // Delete the back faces
    bool someBackFacesWereDeleted = false;
    {
      // First approach (polyplanes): go through the face vertices and try to find back faces and delete them
      for (int v = 0; v < fif._nVertex; v++)
      {
        // Go through the rest of the faces ()
        for (int ri = i + 1; ri < faces.Size(); ri++) // faces.Size() must not be precomputed as it may change in the cycle
        {
          // Take the reverse face - we can use reference here as we don't resize the array
          const FullInfoFace &rfif = faces[ri];
          for (int rv = 0; rv < rfif._nVertex; rv++)
          {
            // If we've got the opposite vertex, delete the whole face
            if ((fif._vertex[v]._pos - rfif._vertex[v]._pos).SquareSize() < 0.0001f &&
              (-fif._vertex[v]._norm - rfif._vertex[v]._norm).SquareSize() < 0.0001f &&
              fif._vertex[v]._tex == rfif._vertex[v]._tex &&
              fif._vertex[v]._tex1 == rfif._vertex[v]._tex1 &&
              fif._properties == rfif._properties)
            {
              deleteFaces[ri] = true;
              someBackFacesWereDeleted = true;
              break;
            }
          }
        }
      }

      // Second approach (grass): try to determine faces as corresponding faces with reversed orientation
      for (int ri = i + 1; ri < faces.Size(); ri++)
      {
        // Skip face in case it was marked as deleted
        if (deleteFaces[i]) continue;

        // Take the reverse face - we can use reference here as we don't resize the array
        const FullInfoFace &rfif = faces[ri];

        // Skip face in case it doesn't have corresponding number of vertices
        if (rfif._nVertex != fif._nVertex) continue;

        // Test all possible rotations of the two faces against each other
        for (int rot = 0; rot < fif._nVertex; rot++)
        {
          // Test if vertices matches of the faces matches (in the back order)
          bool verticesMatches = true;
          for (int v = 0; v < fif._nVertex; v++)
          {
            const FullInfoVertex &vifif = fif._vertex[(v + rot) % fif._nVertex];
            const FullInfoVertex &virfif = rfif._vertex[fif._nVertex - v - 1];

            const Vector3 &vertex = vifif._pos;
            const Vector3 &rVertex = virfif._pos;

            if (vertex.Distance2(rVertex) > 0.0001f || vifif._tex != virfif._tex || vifif._tex1 != virfif._tex1)
            {
              verticesMatches = false;
              break;
            }
          }

          // If we found the back face, then mark it as deleted and finish testing
          if (verticesMatches)
          {
            deleteFaces[ri] = true;
            someBackFacesWereDeleted = true;
            break;
          }
        }
      }
    }

    // In case there were some back faces, mark the polygon with NoBackfaceCull flag
    if (someBackFacesWereDeleted)
    {
      faces[i]._properties.OrSpecial(NoBackfaceCull);
    }
  }

  // Real face deleting
  for (int i = 0; i < faces.Size(); i++)
  {
    if (deleteFaces[i])
    {
      faces.Delete(i);
      deleteFaces.Delete(i);
      i--; // This will ensure the new face with the same index will be tested in the next step
    }
  }

  // Restore geometry from full info faces
  RestoreFromFullInfoFaces(*this, faces, secondUVStagePresent);

  // Update the _pointToVertex array using the _vertexToPoint array
  {
    // Fill out the _pointToVertex array with impossible values
    for (int i = 0; i < _pointToVertex.Size(); i++) _pointToVertex[i] = -1;

    // Update the _pointToVertex array
    for (int i = 0; i < _vertexToPoint.Size(); i++)
    {
      _pointToVertex[_vertexToPoint[i]] = i;
    }
  }
}

#if _VBS3_CRATERS_DEFORM_TERRAIN

void Shape::CreateAlignedConvexComponents(const Shape &src)
{
  // Make sure there are not any selections at the moment
  DoAssert(_sel.Size() == 0);

  // Create one structure that holds information about all the faces and vertices. This structure will
  // be modified and the shape structures like faces, pos, norm will be reconstructed
  // from it
  AUTO_STATIC_ARRAY(FullInfoFace,srcFaces,512);
  ConvertToFullInfoFaces(src, srcFaces, false);

  //   // Create simple testing geometry (pyramid)
  //   faces.Clear();
  //   static float coef = 2.0f;
  //   FullInfoVertex fiv00; fiv00._pos = Vector3(-1, 0, -1) * coef;
  //   FullInfoVertex fiv01; fiv01._pos = Vector3(-1, 0,  1) * coef;
  //   FullInfoVertex fiv10; fiv10._pos = Vector3( 1, 0, -1) * coef;
  //   FullInfoVertex fiv11; fiv11._pos = Vector3( 1, 0,  1) * coef;
  //   FullInfoVertex fivT;  fivT._pos  = Vector3( 0, 1,  0) * coef;
  //   FullInfoFace fif;
  //   fif._nVertex = 4;
  //   fif._vertex[0] = fiv00;
  //   fif._vertex[1] = fiv10;
  //   fif._vertex[2] = fiv11;
  //   fif._vertex[3] = fiv01;
  //   faces.Add(fif);
  //   fif._nVertex = 3;
  //   fif._vertex[0] = fiv00;
  //   fif._vertex[1] = fiv01;
  //   fif._vertex[2] = fivT;
  //   faces.Add(fif);
  //   fif._vertex[0] = fiv01;
  //   fif._vertex[1] = fiv11;
  //   fif._vertex[2] = fivT;
  //   faces.Add(fif);
  //   fif._vertex[0] = fiv11;
  //   fif._vertex[1] = fiv10;
  //   fif._vertex[2] = fivT;
  //   faces.Add(fif);
  //   fif._vertex[0] = fiv10;
  //   fif._vertex[1] = fiv00;
  //   fif._vertex[2] = fivT;
  //   faces.Add(fif);

  // Move the geometry slightly below the original geometry - thats because original geometry is usually a roadway and engine has problems with geometries at the same level as roadways
  static float offsetY = -0.02;

  // Go through the source faces and create convex component out of every triangle
  AUTO_STATIC_ARRAY(FullInfoFace,faces,512);
  for (int i = 0; i < srcFaces.Size(); i++)
  {
    // If normal doesn't point up, then continue
    if (!srcFaces[i].IsNormalUp()) continue;

    // Get name of the named selection
    BString<256> selNum;
    sprintf(selNum, "component%d", i);
    RStringB selectionName = selNum;

    // Get the source face
    FullInfoFace srcFace = srcFaces[i];
    srcFace._namedSelection = selectionName;

    // Modify Y of the source face
    for (int i = 0; i < srcFace._nVertex; i++)
    {
      srcFace._vertex[i]._pos = srcFace._vertex[i]._pos + VUp * offsetY;
    }

    // Add the face itself to the output
    faces.Add(srcFace);

    // Y coordinate of the bottom of the convex components
    const float bottomLevel = -1.0f;

    // Go through all the edges, create squares orthogonal to the ground out of them and add them to the output
    {
      int pVertex = srcFace._nVertex - 1;
      for (int i = 0; i < srcFace._nVertex; i++)
      {
        // Face to be added
        FullInfoFace sideFace;
        sideFace._namedSelection = selectionName;
        sideFace._properties = srcFace._properties;
        sideFace._nVertex = 0;

        // Add current vertex
        sideFace._vertex[sideFace._nVertex++] = srcFace._vertex[i];

        // Add previous vertex
        sideFace._vertex[sideFace._nVertex++] = srcFace._vertex[pVertex];

        // Add previous vertex aligned to bottom
        FullInfoVertex previousBottom = srcFace._vertex[pVertex];
        previousBottom._pos[1] = bottomLevel;
        sideFace._vertex[sideFace._nVertex++] = previousBottom;

        // Add current vertex aligned to bottom
        FullInfoVertex currentBottom = srcFace._vertex[i];
        currentBottom._pos[1] = bottomLevel;
        sideFace._vertex[sideFace._nVertex++] = currentBottom;

        // Add new face
        faces.Add(sideFace);

        // Update previous index
        pVertex = i;
      }
    }

    // Create bottom face (reversed order, aligned to bottom)
    FullInfoFace bottomFace;
    bottomFace._namedSelection = selectionName;
    bottomFace._properties = srcFace._properties;
    bottomFace._nVertex = 0;
    for (int i = 0; i < srcFace._nVertex; i++)
    {
      FullInfoVertex newVertex = srcFace._vertex[srcFace._nVertex - 1 - i];
      newVertex._pos[1] = bottomLevel;
      bottomFace._vertex[bottomFace._nVertex++] = newVertex;
    }
    faces.Add(bottomFace);
  }

  // Restore geometry from full info faces
  RestoreFromFullInfoFaces(*this, faces, false, true);

  //   AUTO_STATIC_ARRAY(SelInfo,selPoints,2048);
  //   AUTO_STATIC_ARRAY(VertexIndex,selFaces,2048);
  //   for (int i = 0; i < NPos(); i++)
  //   {
  //     selPoints.Add(SelInfo(i,255));
  //   }
  //   for (int i = 0; i < NFaces(); i++)
  //   {
  //     selFaces.Add(i);
  //   }
  //   AddNamedSel(NamedSelection(RString("component01"), selPoints.Data(), selPoints.Size(), selFaces.Data(), selFaces.Size()));

  // Invalidate plane array
  _plane.Clear();
}

#endif

void Shape::DefineSections(ParamEntryVal cfgModels, ParamEntryVal cfg)
{
  DoAssert(_face._sections.Size()==0);
  
  // some named selections are marked as section-based
  // no section may split across any such selection

  ConstParamEntryPtr cfgI = &cfg;
  for(;;)
  {
    ParamEntryVal entry = (*cfgI)>>"sections";
    // check if sections need to be split
    // mark named selections as section aware
    for (int i=0; i<entry.GetSize(); i++)
    {
      RStringB selName = entry[i];
      int si = FindNamedSel(selName);
      if (si<0) continue;
      NamedSelection &sel = NamedSel(si);
      sel.SetNeedsSections(true);
    }
    const RStringB &inherit = (*cfgI)>>"sectionsInherit";
    if (inherit.GetLength()<=0) break;
    cfgI = &(cfgModels>>inherit);
  }

}

#if _ENABLE_PHYSICS
//static unsigned int physShapeCnt = 0;
#endif

void Shape::Clear()
{
#if _ENABLE_PHYSICS
	if(_physGeoms)
  {
    for(int i = 0; i < _physGeomsLen; ++i)
    {
      delete (btCollisionShape*)(_physGeoms[i].m_pGeom);
      _physGeoms[i].m_pGeom = 0;
    }
    
		GMemFunctions->Delete(_physGeoms);
    _physGeoms = 0;
  }
   
  _physGeomsLen = 0;
#endif

  ReleaseVBuffer();
  
  _plane.Clear();
  _textures.Clear();

  _sel.Clear();
  _prop.Clear();
  _phase.Clear();
  
  _face.Clear();
  _texArea.Clear();
  base::ReleaseTables();
}

void Shape::ClearSelections()
{
  _sel.Clear();
  _pointToVertex.Clear();
  _vertexToPoint.Clear();
}

double Shape::GetMemoryUsedUnloadable() const
{
  double sum = base::GetMemoryUsedUnloadable();
  return sum;
}

/**
@param includeUnloadable when set, calculate even parts which normally often considered unloadable
*/

size_t Shape::GetMemoryControlledByShape(bool includeUnloadable) const
{
  size_t sum = base::GetMemoryControlledByShape(includeUnloadable)-sizeof(base);

  if (includeUnloadable)
  {
    // _face is now considered a part of geometry
    sum += _face.GetMemoryAllocated();
  }

  sum += _faceProperties.GetMemoryAllocated();
  sum += _plane.GetMemoryAllocated();
  sum += _textures.GetMemoryAllocated();
  sum += _pointToVertex.GetMemoryAllocated();
  sum += _vertexToPoint.GetMemoryAllocated();

  sum += _faceIndexToOffset.GetMemoryAllocated();
  
  sum += _sel.GetMemoryAllocated();
  for (int i=0; i<_sel.Size(); i++)
  {
    sum += _sel[i].GetMemoryControlled();
  }
  sum += _prop.GetMemoryAllocated();
  
  sum += _phase.GetMemoryAllocated();
  sum += _proxy.GetMemoryAllocated();
  
  return sum + sizeof(*this);
}

double Shape::GetMemoryUsed() const
{
  double sum = base::GetMemoryUsed();

  sum += _face.GetMemoryUsed();

  sum += _faceProperties.GetMemoryUsed();
  sum += _plane.GetMemoryUsed();
  sum += _textures.GetMemoryUsed();
  sum += _pointToVertex.GetMemoryUsed();
  sum += _vertexToPoint.GetMemoryUsed();

  sum += _faceIndexToOffset.GetMemoryUsed();
  sum += _sel.GetMemoryUsed();
  sum += _prop.GetMemoryUsed();
  sum += _phase.GetMemoryUsed();

  sum += _proxy.GetMemoryUsed();

  return sum;
}

void Shape::CalculateColor()
{
  // calculate average of all texture colors
  // scan all faces and weight by area
  Color sum(HBlack),sumTop(HBlack);
  float sumA=0,sumATop=0;
  float sumArea=0,sumAreaTop=0;
  int fi;
  Offset i,e;
  for( i=BeginFaces(),e=EndFaces(),fi=0; i<e; NextFace(i),fi++ )
  {
    const Poly &face=_face[i];
    PolyProperties &prop = _faceProperties[fi];
    Texture *texture=prop.GetTexture();
    TexMaterial *mat = prop.GetMaterialExt();
    //float area=1;
    
    Color color=texture ? texture->GetColor() : Color(1, 1, 1, 1);
    // we should consider material color as well
    Color matColor = HWhite;
    if (mat)
    {
      mat->Load();
      const float diffuse = 0.25f;
      matColor = (
        // assume a blend between diffuse and ambient
        (mat->_diffuse+mat->_forcedDiffuse)*diffuse + mat->_ambient * (1-diffuse) +
        // emmisive is applied always
        mat->_emmisive
      );
      matColor.SetA(1);
    }
    
    
    // calculate area and area from top
    float area=face.GetArea(*this);
    float areaTop=face.GetAreaTop(*this);
    
    if (texture && !texture->IsAlpha())
    {
      // no alpha blending - transparent parts will be alpha tested, and discarded
      area *= color.A();
      areaTop *= color.A();
      color.SetA(1);
    }
    sumArea+=area;
    sumA+=color.A()*area;
    sum+=color*matColor*(color.A()*area);
    
    // calculate area from top
    sumAreaTop+=areaTop;
    sumATop+=color.A()*areaTop;
    sumTop+=color*matColor*(color.A()*areaTop);
  }
  if( sumA>0 ) sum=sum*(1/sumA);
  if( sumArea>0 ) sum.SetA(sumA/sumArea);
  //if( sum.A()<0.01 ) Fail("Alpha 0");
  _color=PackedColor(sum);
  if( sumATop>0 ) sumTop=sumTop*(1/sumATop);
  if( sumAreaTop>0 ) sumTop.SetA(sumATop/sumAreaTop);
  _colorTop=PackedColor(sumTop);
}


void Shape::AutoClamp()
{
  //const VertexMesh &mesh=_mesh;
  //if( (_special&(NoClamp|ClampU|ClampV))!=0 ) return;
  // make clamping flags per texture
  AUTO_STATIC_ARRAY(bool,tileU,256);
  AUTO_STATIC_ARRAY(bool,tileV,256);
  tileU.Resize(_textures.Size());
  tileV.Resize(_textures.Size());
  for( int i=0; i<_textures.Size(); i++ )
  {
    tileU[i]=tileV[i]=false;
  }
  int fi;
  Offset f;
  for( f=BeginFaces(),fi=0; f<EndFaces(); NextFace(f),fi++ )
  {
    const PolyProperties &prop=_faceProperties[fi];
    const Poly &face=Face(f);
    // ignore explicit faces
    if( prop.Special()&(NoClamp|ClampU|ClampV) ) continue;
    #define CLAMP_EPS ( 1.0/128 ) // when should we clamp
    //#define CLAMP_EPS ( 0.1 ) // when should we clamp
    Texture *texture=prop.GetTexture();
    if( !texture ) continue;
    int textureIndex=_textures.Find(texture);
    if( textureIndex<0 )
    {
      Fail("Texture?");
      continue;
    }
    float minU=1e10,maxU=-1e10;
    float minV=1e10,maxV=-1e10;
    for( int vi=0; vi<face.N(); vi++ )
    {
      int vv=face.GetVertex(vi);
      float u=U(vv), v=V(vv);
      saturateMin(minU,u);
      saturateMax(maxU,u);
      saturateMin(minV,v);
      saturateMax(maxV,v);
    }
    // force clamping if necessary    
    if( minU<-CLAMP_EPS || maxU>1+CLAMP_EPS ) tileU[textureIndex]=true;
    if( minV<-CLAMP_EPS || maxV>1+CLAMP_EPS ) tileV[textureIndex]=true;
  }

  for( f=BeginFaces(),fi=0; f<EndFaces(); NextFace(f),fi++ )
  {
    //Poly &face=Face(f);
    PolyProperties &prop = _faceProperties[fi];
    if( prop.Special()&(NoClamp|ClampU|ClampV) ) continue;
    if( !prop.GetTexture() )
    {
      prop.OrSpecial(NoClamp);
      continue;
    }
    int textureIndex=_textures.Find(prop.GetTexture());
    if( textureIndex<0 )
    {
      Fail("Texture?");
      continue;
    }
    int spec=0;
    if( !tileU[textureIndex] ) spec|=ClampU;
    if( !tileV[textureIndex] ) spec|=ClampV;
    if( !spec ) spec|=NoClamp;
    prop.OrSpecial(spec);
  }
}

void Shape::AdjustAutoClamp()
{
  // scan sections against each other
  for (int i=0; i<NSections(); i++)
  for (int j=0; j<i; j++)
  {
    ShapeSection &dst = GetSection(i);
    ShapeSection &src = GetSection(j);
    if (dst.GetTexture()!=src.GetTexture()) continue;
    // if clamping flags are conflicting, use less clamping for both sides
    const int clampFlags = ClampU|ClampV|NoClamp;
    int specClamp = dst.Special()&src.Special()&clampFlags;
    if (specClamp==0) specClamp=NoClamp;
    src.SetSpecial((src.Special()&~clampFlags)|specClamp);
    dst.SetSpecial((dst.Special()&~clampFlags)|specClamp);
  }
}

void Shape::BuildFaceIndexToOffset(Buffer<Offset> &offsets) const
{
  offsets.Realloc(NFaces());
  offsets.Resize(NFaces());
  int i=0;
  for( Offset o=BeginFaces(); o<EndFaces(); NextFace(o),i++ )
  {
    offsets[i] = o;
  }
}


void Shape::BuildFaceIndexToOffset() const
{
  // already builded
  if( NFaces()==_faceIndexToOffset.Size() ) return;
  // building necessary
  BuildFaceIndexToOffset(_faceIndexToOffset);
}

void Shape::SetPoints(const Vector3 *point, const ClipFlags *clip, int nPoints, const Vector3 *normal, int nNormals)
{
  Assert( nPoints==nNormals );
  ReallocTable(nPoints);
  int i;
  for( i=0; i<nPoints; i++ )
  {
    SetPos(i)=point[i];
    SetClip(i,clip[i]);
  }
  for( i=0; i<nNormals; i++ )
  {
    SetNorm(i)=Vector3Compressed(normal[i]);
  }
  CalculateHints(); // precalculate hints for possible optimizations
}

#ifdef _DEBUG
  Offset OffsetChangeBase(Offset a, int b)
  {
    a.Advance(b);
    return a;
  }
#else
  Offset OffsetChangeBase(Offset a, int b)
  {
    return a+b;
  }
#endif

void Shape::Merge( const Shape *with, const Matrix4 &transform )
{
  // assume this and with have the same coordinate space
  // merge points and normals
  // create a permutation

  // calculate the new UV range
  UVPair minUV = MinUV();
  UVPair maxUV = MaxUV();
  const UVPair &minUVWith = with->MinUV();
  const UVPair &maxUVWith = with->MaxUV();
  saturateMin(minUV.u, minUVWith.u);
  saturateMin(minUV.v, minUVWith.v);
  saturateMax(maxUV.u, maxUVWith.u);
  saturateMax(maxUV.v, maxUVWith.v);

  // adapt the UV to the new range
  UVPair invUV;
  ChangeUVRange(invUV, minUV, maxUV);

  _data->_clip.Expand();
  enum {maxTable=256};
  AUTO_STATIC_ARRAY(int,pointWithToThis,maxTable);
  pointWithToThis.Resize(with->NPos());

  //LogF("Merge from %d",NPos());
  Reserve(with->NPos()+NPos());
  Vector3 pos,norm;
  for( int i=0; i<with->NPos(); i++ )
  {
    pos.SetFastTransform(transform,with->Pos(i));
    norm.SetRotate(transform, with->Norm(i).Get());
    ClipFlags clip=with->Clip(i);
    int vertex=AddVertex
      (
      pos,norm,clip,invUV,with->U(i),with->V(i)
      );
    //LogF("  uv %.2f,%.2f",with->U(i),with->V(i));
    // no vertex information possible on merged shapes
    _vertexToPoint.Access(vertex);
    _vertexToPoint[vertex]=-1;
    pointWithToThis[i]=vertex;
  }
  Assert(_face.VerifyStructure());
  Assert(with->_face.VerifyStructure());
  int offsetDiff = OffsetDiff(EndFaces(), BeginFaces());
  _face.ReserveRaw(_face.RawSize()+with->_face.RawSize());
  // merge sections
  //Offset oldOffset = with->EndFaces();
  for (int s=0; s<with->NSections(); s++)
  {
    const ShapeSection &sec = with->_face._sections[s];
    for( Offset f=sec.beg,e=sec.end; f<e; with->NextFace(f) )
    {
      Offset index=_face.Add(with->Face(f));
      Poly &face=_face[index];
      for( int v=0; v<face.N(); v++ )
      {
        face.Set(v,pointWithToThis[face.GetVertex(v)]);
      }
    }
    if (with->NNamedSel()==0 && NNamedSel()==0)
    {
      // append section as necessary
      ExtendLastSection(sec);
    }
    else
    {
      // merge faces
      ShapeSection dstSec = sec;
      dstSec.beg = OffsetChangeBase(sec.beg, offsetDiff);
      dstSec.end = OffsetChangeBase(sec.end, offsetDiff);
      AddSection(dstSec);
    }
  }
  Assert(_face.VerifyStructure());
  _data->_clip.Condense();
  _data->_tex.Condense();
  _data->_norm.Condense();
  // Set the vertex declaration according to sections
  VertexDeclarationNeedsUpdate();
  // merge textures
  for( int i=0; i<with->_textures.Size(); i++ )
  {
    _textures.AddUnique(with->_textures[i]);
  }
  _special|=with->_special;
}

struct MergedVertex
{
  Vector3 pos;
  int n;
  int sel;
  int pointIndex;
};

TypeIsSimple(MergedVertex)

Shape *Shape::ExtractPath(LODShape *lodShape) // optimize this shape as path LOD
{
  Fail("Never used - Not tested properly");
  // scan all faces
  // replace vertices with degenerate
  // create a new shape with 2-vertex (degenerate) faces
  Shape *res = new Shape;

  UVPair minUV(0, 0), maxUV(1, 1), invUV;
  res->InitUV(invUV, minUV, maxUV);

  res->_pointToVertex.Realloc(_pointToVertex.Size());
  res->_pointToVertex.Resize(_pointToVertex.Size());
  for( int i=0; i<res->_pointToVertex.Size(); i++ )
  {
    res->_pointToVertex[i]=-1;
  }

  // first of all convert all points to their respective "merged" variants
  // TODO: use some heuristics to estimate critical distance
  // find longest of the shortest edges

  float maxMerge2  = 0;
  for ( Offset o=BeginFaces(); o<EndFaces(); NextFace(o))
  {
    float shortestEdge2 = 1e10;
    const Poly &face = Face(o);
    for (int v=0,p=face.N()-1; v<face.N(); p=v++)
    {
      Vector3Val vpos = Pos(face.GetVertex(v));
      Vector3Val ppos = Pos(face.GetVertex(p));
      float dist2 = vpos.Distance2(ppos);
      saturateMin(shortestEdge2,dist2);
    }
    if (shortestEdge2<Square(1.5))
    {
      saturateMax(maxMerge2,shortestEdge2);
    }
  }
  maxMerge2 *= 1.01;
  LogF("  longest shortest edge %.1f",sqrt(maxMerge2));
  saturate(maxMerge2,Square(0.5),Square(1.5));
  // or use property from model

  AutoArray<MergedVertex> vertex;
  Temp<int> vertexReplacedBy(NPos());

  res->_pointToVertex.Realloc(_pointToVertex.Size());
  res->_pointToVertex.Resize(_pointToVertex.Size());
  for (int i=0; i<res->_pointToVertex.Size(); i++)
  {
    res->_pointToVertex[i] = -1;
  }
  for (int i=0; i<NPos(); i++)
  {
    Vector3Val pos = Pos(i);
    // search if the point is already present
    // check i point selection
    // check i 
    int pointI = VertexToPoint(i);
    int selI = -1;
    const char *name = "";
    for (int s=0; s<NNamedSel(); s++)
    {
      if (NamedSel(s,lodShape).IsSelected(i))
      {
        Assert( selI<0 );
        selI = s;
        name = NamedSel(s).Name();
      }
    }
    float minDist2 = 1e10;
    int minJ = -1;
    for (int j=0; j<vertex.Size(); j++)
    {
      float dist2 = pos.Distance2(vertex[j].pos);
      if (dist2<minDist2)
      {
        minJ = j;
        minDist2 = dist2;
      }
    }
    if (minJ>=0 && minDist2<=maxMerge2)
    {
      MergedVertex &vtx = vertex[minJ];
      vtx.pos = (vtx.pos*vtx.n + pos) * (1/ (vtx.n+1));
      vtx.n++;

      if (selI>=0 )
      {
        if( vtx.sel>=0 && vtx.sel!=selI )
        {
          LogF
          (
            "Double selection %s, %s",
            NamedSel(selI).Name(),NamedSel(vtx.sel).Name()
          );
        }
        vtx.sel = selI;
        vtx.pointIndex = pointI;
      }
      vertexReplacedBy[i] = minJ;
    }
    else
    {
      int j = vertex.Add();
      vertex[j].pos = pos;
      vertex[j].n = 1;
      vertex[j].pointIndex = pointI;
      vertex[j].sel = selI;

      vertexReplacedBy[i] = j;
    }
  }
  // 

  for (int j=0; j<vertex.Size(); j++)
  {
    const MergedVertex &vtx = vertex[j];
    int vIndex = res->AddVertexFast(vtx.pos, VUp, ClipAll, invUV, 0, 0);
    res->_vertexToPoint.Access(vIndex);
    res->_vertexToPoint[vIndex] = vtx.pointIndex;
    res->_pointToVertex[vtx.pointIndex] = vIndex;
    if (vtx.sel>=0)
    {
      NamedSelection oSel=NamedSel(vtx.sel);
      SelInfo info(vIndex,255);
      NamedSelection sel(oSel.GetName(),&info,1,NULL,0);
      res->AddNamedSel(sel);
    }
  }

  for ( Offset o=BeginFaces(); o<EndFaces(); NextFace(o))
  {
    const Poly &face = Face(o);
    int n = face.N();
    //
    Poly resFace;
    PolyProperties resProp;
    resFace.Init();
    int resN = 0;
    // replace vertices 
    for (int v=0; v<n; v++)
    {
      // check edge pv,v
      int vIndex = face.GetVertex(v);
      int replaceBy = vertexReplacedBy[vIndex];
      // check if vertex is already present
      bool present = false;
      for (int p=0; p<resN; p++)
      {
        if (resFace.GetVertex(p)==replaceBy) {present = true;break;}
      }
      if (!present)
      {
        resFace.Set(resN,replaceBy);
        ++resN;
      }
    }
    resProp.SetSpecial(NoClamp);
    resFace.SetN(resN);
    // longest edge is from p to v
    // vertices to merge are: p-1 with p and v with v+1

    if( resN>1)
    {
      // note: resN should be 2
      res->AddFace(resFace);
      res->_faceProperties.Add(resProp);
    }
  }
  res->CalculateMinMax();
  res->CalculateHints();
  res->Compact();
  return res;
}

void Shape::SortVertices(const LODShape *lodShape, int level)
{
  // make permutation of vertices so that _vertexToPoint is sorted
  AUTO_STATIC_ARRAY(SortVertex,sort,2048);
  sort.Resize(_vertexToPoint.Size());
  for (int i=0; i<_vertexToPoint.Size(); i++)
  {
    sort[i].vertex = i; // Remember the old vertex index
    sort[i].point = _vertexToPoint[i];
    //sort[i].prior = _clip[i]&ClipLightMask;
    // sort first by UserMask (Material index), then by LightMask
    sort[i].prior = _data->_clip[i]&(ClipUserMask|ClipLightMask);
  }

  QSort(sort.Data(),sort.Size(),CmpSortVertex);
  // save current state
  CondensedAutoArray<ClipFlags> oClip = _data->_clip;
  AutoArray<Vector3> oPos = GetPosArray();
  CondensedAutoArray<Vector3Compressed> oNorm = GetNormArray();
  AutoArray<VertexIndex> oVertexToPoint = _vertexToPoint;
  UVPairs oTex = GetUVArray();
  if (NUVStages() > 2)
  {
    RptF("Error: %s:%s Number of uv stages is greater than 2", lodShape->Name(), lodShape->LevelName(lodShape->Resolution(level)));
    Fail("Error: Number of uv stages is greater than 2");
  }

  _data->_clip.Expand();
  _data->_norm.Expand();
  _data->_tex.Expand();
  // change vertices to respect a new permutation
  for (int i=0; i<_vertexToPoint.Size(); i++)
  {
    int newIndex = i;
    int oldIndex = sort[i].vertex;
    _data->_clip.Set(newIndex)  = oClip[oldIndex];
    _data->_pos[newIndex] = oPos[oldIndex];
    _data->_norm.Set(newIndex) = oNorm[oldIndex];
    _data->_tex.SetUVRaw(newIndex, oTex.GetUVRaw(oldIndex));
    _vertexToPoint[newIndex] = oVertexToPoint[oldIndex];
  }
  _data->_clip.Condense();
  _data->_norm.Condense();
  _data->_tex.Condense();

  // Change optional arrays to respect a new permutation
  {
    // TexCoord
    if (_data->_texCoord[0].Size() > 0)
    {
      UVPairs oTexCoord0 = _data->_texCoord[0];
      _data->_texCoord[0].Expand();
      for (int i = 0; i < _vertexToPoint.Size(); i++)
      {
        int newIndex = i;
        int oldIndex = sort[i].vertex;
        _data->_texCoord[0].SetUVRaw(newIndex, oTexCoord0.GetUVRaw(oldIndex));
      }
      _data->_texCoord[0].Condense();
    }

    // AnimationPhase
    if (_phase.Size() > 0)
    {
      AutoArray<AnimationPhase> oPhase = _phase;
      for (int p = 0; p < _phase.Size(); p++)
      {
        AnimationPhase &animationPhase = _phase[p];
        const AnimationPhase &oAnimationPhase = oPhase[p];
        DoAssert(_vertexToPoint.Size() == animationPhase.Size());
        for (int i = 0; i < _vertexToPoint.Size(); i++)
        {
          int newIndex = i;
          int oldIndex = sort[i].vertex;
          animationPhase[newIndex] = oAnimationPhase[oldIndex];
        }
      }
    }
  }

  // Calculate the inverse array to easily map old vertices to new vertices
  AUTO_STATIC_ARRAY(int,invSort,2048);
  invSort.Resize(sort.Size());
  for (int i=0; i<invSort.Size(); i++)
  {
    invSort[sort[i].vertex] = i;
  }
  
  // Change all existing references to vertices
  {
    // Change _pointToVertex array
    for (int i=0; i<_pointToVertex.Size(); i++)
    {
      int oldI = _pointToVertex[i];
      _pointToVertex[i] = invSort[oldI];
    }

    // Change face references
    for( Offset o=BeginFaces(),e=EndFaces(); o<e; NextFace(o) )
    {
      Poly &face = Face(o);
      for (int v=0; v<face.N(); v++)
      {
        face.Set(v,invSort[face.GetVertex(v)]);
      }
    }

    // Change references in named selections
    for (int j = 0; j < _sel.Size(); j++)
    {
      Selection &selection = _sel[j].SetData();
      for (int i = 0; i < selection.Size(); i++)
      {
        selection.Set(i, invSort[selection[i]]);
      }
    }
  }
}

static int GetLandPass(const TexMaterial *mat)
{
  if (mat->_specular.R()>0.5f) return 0;
  if (mat->_specular.G()>0.5f) return 1;
  if (mat->_specular.B()>0.5f) return 2;
  if (mat->_specular.A()>0.5f) return 3;
  Fail("PSTerrain: No pass given");
  return -1;
}
static int CompareMaterials(const TexMaterial *mat1,const TexMaterial *mat2)
{
  mat1->Load();
  mat2->Load();
  int d = mat1->GetVertexShaderID()-mat2->GetVertexShaderID();
  if (d) return d;
  d = mat1->GetPixelShaderID()-mat2->GetPixelShaderID();
  if (d) return d;
  // same shaders - compare by textures
  for (int i=0; i<TexMaterial::NStages; i++)
  {
    d = ComparePointerAddresses(mat1->_stage[i]._tex.GetRef(),mat2->_stage[i]._tex.GetRef());
    if (d) return d;
  }
  // identical textures
  return 0;
}

static inline int SpecToPass(int spec)
{
  // handle special cases which are common for landscape rendering
  // multi pass: dst-blend zero should go before dst-blend one
  // no-z-write means road rendering
  if (spec&DstBlendZero) return 0; // initial ARGB pass
  else if (spec&NoAlphaWrite) return 2; // additional lerp pass
  else if (spec&NoColorWrite) return 3; // additional ARGB pass
  else if (spec&NoZWrite) return  4; // roads
  return 1;
}

static int CompareTextureR(const ShapeSection **pp0, const ShapeSection **pp1)
{
  const ShapeSection *p0 = *pp0;
  const ShapeSection *p1 = *pp1;
  // check if some texture is alpha
  //const Texture *t0=p0->GetTexture();
  //const Texture *t1=p1->GetTexture();
  bool t0Alpha = (p0->Special()&IsAlphaOrdered)!=0;
  bool t1Alpha = (p1->Special()&IsAlphaOrdered)!=0;
  //bool t0Alpha=t0 && t0->IsAlpha();
  //bool t1Alpha=t1 && t1->IsAlpha();
  if( t0Alpha || t1Alpha )
  {
    if( t0Alpha )
    {
      if( t1Alpha )
      {
        // both alpha - keep order
        return ComparePointerAddresses(p0,p1);
      }
      return +1;
    }
    return -1;
  }
  int t0Pass = SpecToPass(p0->Special());
  int t1Pass = SpecToPass(p1->Special());
  if (t0Pass!=t1Pass)
  {
    return t0Pass-t1Pass;
  }
  // multi pass: alpha writing should go lazy
  
  int d;
  // consider TexMaterial as well
  d = ComparePointerAddresses(p0->GetMaterialExt(),p1->GetMaterialExt());
  if (d)
  {
    const TexMaterial *mat1 = p0->GetMaterialExt();
    const TexMaterial *mat2 = p1->GetMaterialExt();
    
    int m = mat1 && mat2 ? CompareMaterials(mat1,mat2) : 0;
    if (m) return m;
    return d;
  }
  d = p0->Special()-p1->Special();
  if (d) return d;
  d = ComparePointerAddresses(p0->GetTexture(),p1->GetTexture());
  if (d) return d;
  return 0;
}


void Shape::Optimize()
{
  DoAssert(NFaces()==0 || NSections()>0);
  Assert( _sel.Size()==0 );
  // sort sections
  Assert(CheckIntegrity());
  // sort by sections
  // note: face indices will become invalid
  _face.SortSections(CompareTextureR);
  Assert(CheckIntegrity());
}

void Shape::ConcatSections(int start)
{
  DoAssert(NFaces()==0 || NSections()>0);
  Assert( _sel.Size()==0 );
  _face.ConcatSections(start);
}

static int CompareTextureIgnoreAlpha(const ShapeSection **pp0, const ShapeSection **pp1)
{
  const ShapeSection *p0 = *pp0;
  const ShapeSection *p1 = *pp1;
  int d;
  // consider TexMaterial as well
  d = ComparePointerAddresses(p0->GetMaterialExt(),p1->GetMaterialExt());
  if (d)
  {
    const TexMaterial *mat1 = p0->GetMaterialExt();
    const TexMaterial *mat2 = p1->GetMaterialExt();

    int m = mat1 && mat2 ? CompareMaterials(mat1,mat2) : 0;
    if (m) return m;
    return d;
  }
  d = p0->Special()-p1->Special();
  if (d) return d;
  d = ComparePointerAddresses(p0->GetTexture(),p1->GetTexture());
  if (d) return d;
  return 0;
}


void Shape::OptimizeIgnoreAlpha()
{
  DoAssert(NFaces()==0 || NSections()>0);
  Assert( _sel.Size()==0 );
  // sort sections
  Assert(CheckIntegrity());
  // sort by sections
  // note: face indices will become invalid
  _face.SortSections(CompareTextureIgnoreAlpha);
  Assert(CheckIntegrity());
}

void Shape::AddSection(const ShapeSection &sec)
{
  // handle simple case - no named selections at all
  if (NNamedSel()==0)
  {
    _face._sections.Add(sec);
    return;
  }
  
  Fail("AddSection no longer supports selections");
  #if 0
  // add section, respect selection boundaries
  // scan if we match any selection
  // mark faces that need to be added
  // check if section may be split

  // faces: all face offsets from the shape
  AUTO_STATIC_ARRAY(Offset,faces,1024);
  int i=0;
  faces.Resize(NFaces());
  for( Offset o=BeginFaces(); o<EndFaces(); NextFace(o),i++ )
  {
    faces[i]=o;
  }

  // start with having one section
  AUTO_STATIC_ARRAY(ShapeSection,sections,256);
  sections.Add(sec);

  AUTO_STATIC_ARRAY(ShapeSection,selections,256);
  // selections are "sections" representing section-aware selections
  // convert all section-aware selections to section representation
  for (int i=0; i<NNamedSel(); i++)
  {
    const NamedSelection &sel = NamedSel(i);
    if (!sel.GetNeedsSections()) continue;
    // convert selection to section list
    Offset beg = Offset(0),end = Offset(0);
    bool open = false;
    for (int ff=0; ff<sel.Faces().Size(); ff++)
    {
      int fi = sel.Faces()[ff];
      Offset fo = faces[fi];
      Offset fn = fo;
      NextFace(fn);
      if (!open)
      {
        beg = fo;
        end = fn;
        open = true;
      }
      else if (fo==end)
      {
        // extend section
        end = fn;
      }
      else
      {
        // flush section
        ShapeSection &selSec = selections.Append();
        selSec.beg = beg;
        selSec.end = end;
        // open a new section
        beg = fo;
        end = fn;
      }
    }
    if (open)
    {
      ShapeSection &selSec = selections.Append();
      selSec.beg = beg;
      selSec.end = end;
    }

  }

  // we must split the section by all selections that are section aware
  // note: in most cases this will mean no splitting, but generally
  // when splitting by n sections, the result might be x^n parts


  for (int i=0; i<selections.Size(); i++)
  {
    const ShapeSection &sel = selections[i];
    // use sel to split all existing sections
    // each section
    int n = sections.Size(); // do not split sections added during splitting
    for (int s=0; s<n; s++)
    {
      ShapeSection &sec = sections[s];
      // nothing to do if sec and sel do not overlap
      if (sec.beg>=sel.end || sec.end<=sel.beg) continue;

      // split sec.beg...sec.end by two boundaries: sel.beg and sel.end
      if (sel.beg>sec.beg && sel.beg<sec.end)
      {
        // sel.beg inside sec - split necessary
        ShapeSection &secNew = sections.Append();
        secNew.SetInfo(sec);
        secNew.beg = sec.beg;
        secNew.end = sel.beg;
        sec.beg = sel.beg;
        // 
      }
      if (sel.end>sec.beg && sel.end<sec.end)
      {
        // sel.end inside sec - split necessary
        ShapeSection &secNew = sections.Append();
        secNew.SetInfo(sec);
        secNew.beg = sel.end;
        secNew.end = sec.end;
        sec.end = sel.end;
      }
      // splitting is necessary

    }

  }
  // add all constructed sections
  for (int i=0; i<sections.Size(); i++)
  {
    ShapeSection &section = sections[i];
    _face._sections.Add(section);
  }

  #endif
}

static int CmpSection(const ShapeSection *s1, const ShapeSection *s2)
{
  if (s1->beg>s2->beg) return +1;
  if (s1->beg<s2->beg) return -1;
  return 0;
}

//! integer range - interval
/*!
  used to represent set of integers: i>=beg && i<end
*/

struct Interval
{
  int beg,end;

  bool IsInside(int i) const {return i>=beg && i<end;}
  bool IsOverlapped(const Interval &j) const
  {
    return j.end>beg && j.beg<end;
    //if (j.end>beg) return false;
    //if (j.beg<end) return false;
    //return true;
  }
};

TypeIsSimple(Interval)

//! memory effective implementation of
//! integer set containing many continuous regions

class IntervalArray
{
  AutoArray<Interval> _data;

  public:
  IntervalArray();
  ~IntervalArray();

  void Delete(int i);

  void AddInterval(int beg, int end);
  bool IsPresent(int beg) const;
  bool IsEmpty() const {return _data.Size()==0;}

  int NIntervals() const {return _data.Size();}

  const Interval &GetInterval(int i) const {return _data.Get(i);}
  void Add(int i){AddInterval(i,i+1);}

  bool CheckIntegrity() const;
};

IntervalArray::IntervalArray()
{
}
IntervalArray::~IntervalArray()
{
}

#define LOG_INTERVAL_OPS 0

void IntervalArray::Delete(int index)
{
  Assert(CheckIntegrity());
  #if LOG_INTERVAL_OPS
    Log("%08x: delete %d",this,index);
  #endif
  #if _DEBUG
  bool present = false;
  for (int i=0; i<_data.Size(); i++)
  {
    Interval &ii = _data[i];
    if (!ii.IsInside(index)) continue;
    present = true;
  }
  Assert (present);
  #endif

  for (int i=0; i<_data.Size();)
  {
    //Interval &ii = _data[i];
    if (!_data[i].IsInside(index))
    {
      i++;
      continue;
    }
  #if LOG_INTERVAL_OPS
    int obeg = ii.beg;
    int oend = ii.end;
  #endif
    // we found given interval
    // if one of end-points, we may simply remove it
    if (_data[i].beg<index && _data[i].end>index+1)
    {
      // we need to split given interval
      Interval &ni = _data.Append();
      ni.beg = index+1; // new interval (index,end)
      ni.end = _data[i].end;
      _data[i].end = index; // old interval <beg,index)
      Assert(ni.end>ni.beg);
      Assert(_data[i].end>_data[i].beg);
      Assert(CheckIntegrity());
  #if LOG_INTERVAL_OPS
        Log("  %d..%d split to %d..%d, %d..%d",obeg,oend,ii.beg,ii.end,ni.beg,ni.end);
  #endif
    }
    else
    {
      Assert(_data[i].end>_data[i].beg);
      if (_data[i].beg==index) _data[i].beg++;
      else if (_data[i].end==index+1) _data[i].end--;
      if (_data[i].end==_data[i].beg)
      {
  #if LOG_INTERVAL_OPS
        Log("  %d..%d deleted",obeg,oend);
  #endif
        _data.Delete(i);
        Assert(CheckIntegrity());
        break;
      }
      else
      {
        #if LOG_INTERVAL_OPS
        Log("  %d..%d changed to %d..%d",obeg,oend,_data[i].beg,_data[i].end);
        #endif
        Assert(CheckIntegrity());
        Assert(_data[i].end>_data[i].beg);
      }
    }
    // assume any number is contained in max. one interval
    break;
  }
  // verify point is not present in any other selection
  #if _DEBUG
  for (int i=0; i<_data.Size(); i++)
  {
    Interval &ii = _data[i];
    if (!ii.IsInside(index)) continue;
    Fail("Index present twice");
  }
  #endif
  Assert(CheckIntegrity());
}

void IntervalArray::AddInterval(int beg, int end)
{
  #if LOG_INTERVAL_OPS
  Log("%08x: add %d..%d",this,beg,end);
  #endif
  Assert(end>beg);
  // if interval is overlapping or touching any other, merge it
  for (int i=0; i<_data.Size(); i++)
  {
    Interval &ii = _data[i];
    if (ii.end<beg) continue;
    if (ii.beg>end) continue;
    #if LOG_INTERVAL_OPS
    int obeg = ii.beg;
    int oend = ii.end;
    #endif
    saturateMin(ii.beg,beg);
    saturateMax(ii.end,end);
    Assert(ii.end>ii.beg);
    #if LOG_INTERVAL_OPS
      Log("  %d..%d changed to %d..%d",obeg,oend,ii.beg,ii.end);
    #endif
    Assert(CheckIntegrity());
    return;
  }

  Assert(end>beg);
  Interval &ni = _data.Append();
  ni.beg = beg;
  ni.end = end;
  Assert(CheckIntegrity());
}

bool IntervalArray::IsPresent(int index) const
{
  for (int i=0; i<_data.Size(); i++)
  {
    const Interval &ii = _data[i];
    if (ii.IsInside(index)) return true;
  }
  return false;
}

bool IntervalArray::CheckIntegrity() const
{
  for (int i=0; i<_data.Size(); i++)
  {
    const Interval &ii = _data[i];
    if (ii.beg>=ii.end)
    {
      ErrF("ii.beg>=ii.end: %d,%d",ii.beg,ii.end);
      return false;
    }
  }
  for (int i=0; i<_data.Size(); i++) for (int j=0; j<i; j++)
  {
    const Interval &ii = _data[i];
    const Interval &jj = _data[j];
    if (ii.IsOverlapped(jj))
    {
      ErrF
      (
        "Overlapping %d and %d (%d..%d, %d..%d)",
        i,j,ii.beg,ii.end,jj.beg,jj.end
      );
      return false;
    }
  }
  return true;
}

struct ShapeSectionWork: public ShapeSectionInfo
{
  // used during section creation
  //FindArray<int> _faces; // set of face indices
  IntervalArray _faces;

  void SetInfo(const PolyProperties &info)
  {
    PolyProperties::operator =(info);
  }
};

TypeIsMovable(ShapeSectionWork)

struct FindShapeSections
{
  const LODShape *_lShape;
  Shape *_shape;

  AutoArray<ShapeSectionWork> _sections;
  AutoArray<Offset> _offsets; // face index->offset helper

  FindShapeSections(const LODShape *lShape, Shape *shape);

  void CreateOffsets();
  int FindSection(const PolyProperties &prop) const;
  int AssignSection(const PolyProperties &prop);

  void SplitByNamedSelection(ShapeSectionWork &src, ShapeSectionWork &split, const NamedSelection::Access &sel);

  void SplitByNamedSelection(const NamedSelection::Access &sel);

  void Perform(bool forceMaterial0);
};

void FindShapeSections::CreateOffsets()
{
  _offsets.Resize(_shape->NFaces()+1);
  int i=0;
  for( Offset o=_shape->BeginFaces(); o<_shape->EndFaces(); _shape->NextFace(o),i++ )
  {
    _offsets[i]=o;
  }
  _offsets[i] = _shape->EndFaces();
}

int FindShapeSections::FindSection(const PolyProperties &prop) const
{
  for (int i=0; i<_sections.Size(); i++)
  {
    const ShapeSectionWork &sw = _sections[i];
    if (sw!=prop) continue;
    return i;
  }
  return -1;
}

int FindShapeSections::AssignSection(const PolyProperties &prop)
{
  int index = FindSection(prop);
  if (index>=0) return index;
  index = _sections.Add();
  ShapeSectionWork &sw = _sections[index];
  sw.SetInfo(prop);
  return index;
}

FindShapeSections::FindShapeSections(const LODShape *lShape, Shape *shape)
:_lShape(lShape),_shape(shape)
{
}

/**
@param split what is split-out
*/
void FindShapeSections::SplitByNamedSelection(ShapeSectionWork &src, ShapeSectionWork &split, const NamedSelection::Access &sel)
{
  split.SetInfo(src);
  const Selection &faces = sel.Faces();
  for (int i=0; i<faces.Size(); i++)
  {
    int fi = faces[i];
    // check if fo is in src
    // if it is in both section and selection, move it to split
    /*
    int index = src._faces.Find(fi);
    if (index>=0)
    {
      split._faces.Add(fi);
      src._faces.DeleteAt(index);
    }
    */
    if (src._faces.IsPresent(fi))
    {
      split._faces.Add(fi);
      src._faces.Delete(fi);
    }
  }
  // src and split are now both correct in relation to sel
  // one (both?) of them may be empty now
}

void FindShapeSections::SplitByNamedSelection(const NamedSelection::Access &sel)
{
  int ns = _sections.Size();
  // if _sections will grow, there is no need to test new sections
  for (int i=0; i<ns; i++)
  {
    ShapeSectionWork &sw = _sections[i];
    ShapeSectionWork split;
    SplitByNamedSelection(sw,split,sel);
    if (split._faces.IsEmpty()) continue;
    if (sw._faces.IsEmpty())
    {
      // nothing left in sw, use split instead of sw
      sw = split;
    }
    else
    {
      // two part - insert a new one
      _sections.Add(split);
    }
    
  }
}

struct SortSecPoly
{
  const ShapeSectionWork *sec;
  Offset polyBeg, polyEnd;
  int polyIndexBeg,polyIndexEnd;
};

TypeIsSimple(SortSecPoly)

static int CompareSecPolyNoA(const SortSecPoly *s1, const SortSecPoly *s2)
{
  // first sort by section
  const ShapeSectionWork *sec1 = s1->sec;
  const ShapeSectionWork *sec2 = s2->sec;
  // sections: first sort by texture
  const Texture *t1 = sec1->GetTexture();
  const Texture *t2 = sec2->GetTexture();
  // some faces must retain given order
  int d;
  // material is a primary criterion
  d = ComparePointerAddresses(sec1->GetMaterialExt(),sec2->GetMaterialExt());
  if (d)
  {
    // most important part of materials are shaders
    const TexMaterial *mat1 = sec1->GetMaterialExt();
    const TexMaterial *mat2 = sec1->GetMaterialExt();
    
    int m = mat1 && mat2 ? CompareMaterials(mat1,mat2) : 0;
    if (m) return m;
    return d;
  }
  // next by special
  d = sec1->Special()-sec2->Special();
  if (d) return d;
  d = ComparePointerAddresses(t1,t2);
  if (d) return d;
  // sections may have equal properties, but need to be
  // distinguished anyway
  d = ComparePointerAddresses(sec1,sec2);
  if (d) return d;
  // any order will do
  return 0;
}


static int CompareSecPoly(const SortSecPoly *s1, const SortSecPoly *s2)
{
  // first sort by section
  const ShapeSectionWork *sec1 = s1->sec;
  const ShapeSectionWork *sec2 = s2->sec;
  // some faces must retain given order
  bool t1Alpha = (sec1->Special()&IsAlphaOrdered)!=0;
  bool t2Alpha = (sec2->Special()&IsAlphaOrdered)!=0;
  if (t1Alpha!=t2Alpha)
  {
    // alpha transparent goes last
    return t1Alpha-t2Alpha;
  }
  if (t1Alpha)
  {
    // if both are marked as alpha ordered, but none of them is real alpha, we can sort them
    //if ((sec1->Special()&IsAlpha) || (sec2->Special()&IsAlpha))
    {
      // both alpha - retain order
      if (s1->polyBeg>s2->polyBeg) return +1;
      if (s1->polyBeg<s2->polyBeg) return -1;
      return 0;
    }
  }
  return CompareSecPolyNoA(s1,s2);
}

void FindShapeSections::Perform(bool forceMaterial0)
{
  bool neededSplit = false;
  for (int i=0; i<_shape->NNamedSel(); i++)
  {
    const NamedSelection &sel = _shape->NamedSel(i);
    if (!sel.GetNeedsSections()) continue;
    neededSplit = true;
  }
  bool someAlpha = false;
  // if there is no alpha blended face, alpha ordering may be ignored
  for (int fi=0; fi<_shape->NFaces(); fi++)
  {
    const PolyProperties &prop = _shape->FaceProperties(fi);
    if (prop.Special()&IsAlpha)
    {
      someAlpha = true;
      break;
    }
  }
  if (!someAlpha)
  {
    // if there is no alpha blending, we may remove alpha ordering flags
    for (int fi=0; fi<_shape->NFaces(); fi++)
    {
      PolyProperties &prop = _shape->FaceProperties(fi);
      prop.AndSpecial(~IsAlphaOrdered);
    }
  }
  
  if (!neededSplit)
  {
    // detect singular case: sections already contiguous
    // each section may start only once
    AUTO_FIND_ARRAY(ShapeSection,sections,256);
    ShapeSection *openSec = NULL;
    bool singular = true;
    Offset o;
    int fi;
    for (o = _shape->BeginFaces(),fi=0; o<_shape->EndFaces(); _shape->NextFace(o),fi++)
    {
      const PolyProperties &prop = _shape->FaceProperties(fi);
      // if alpha sorting is required, we cannot use singular case
      if (prop.Special()&IsAlphaOrdered)
      {
        singular = false;
        break;
      }

      if (!openSec)
      {
        // no section started yet
        openSec = &sections.Append();
        openSec->SetInfo(prop);
        openSec->beg = o;
        openSec->end = o;
      }
      else if ((*openSec)!=prop)
      {
        // check if section was already opened
        ShapeSection info;
        info.SetInfo(prop);
        if (sections.Find(info))
        {
          singular = false;
          break;
        }
        // close section
        openSec->end = o;
        _shape->NextFace(openSec->end);
        // section closed  - we need to open next section
        openSec = &sections.Append();
        openSec->SetInfo(prop);
        openSec->beg = o;
        openSec->end = o;
      }
    }
    if (singular)
    {
      // perform simplified conversion to sections
      // close last open sections if necessary
      if (openSec)
      {
        openSec->end = _shape->EndFaces();
      }
      // convert information from sections to _sections
      _shape->Faces().SetSections(sections.Data(),sections.Size());

      return;
    }
  }

  CreateOffsets();
  // define sections by properties
  int lastSection = -1;
  int lastSectionStart = -1;
  for (int i=0; i<_shape->NFaces(); i++)
  {
    //Offset fo = _offsets[i];
    //const Poly &poly = _shape->Face(fo);
    const PolyProperties &prop = _shape->FaceProperties(i);
    // note: polygons are often almost ordered
    // best candidate to search for section
    // is therefore section of last polygon
    if (lastSection>=0)
    {
      ShapeSectionWork &ls = _sections[lastSection];
      if (ls==prop)
      {
        continue;
      }
      Assert(i>lastSectionStart);
      ls._faces.AddInterval(lastSectionStart,i);
      lastSectionStart = i;
    }
    int sec = AssignSection(prop);
    //ShapeSectionWork &sw = _sections[sec];
    //sw._faces.Add(i);
    lastSection = sec;
    lastSectionStart = i;
  }
  if (lastSection>=0)
  {
    ShapeSectionWork &ls = _sections[lastSection];
    Assert(_shape->NFaces()>lastSectionStart);
    ls._faces.AddInterval(lastSectionStart,_shape->NFaces());
  }
  // check all sections against all section-aware selections
  for (int i=0; i<_shape->NNamedSel(); i++)
  {
    const NamedSelection::Access &sel = _shape->NamedSel(i,_lShape);
    if (!sel.GetNeedsSections()) continue;
    if (sel.Faces().Size()<=0) continue;
    // we must check all sections against this one
    SplitByNamedSelection(sel);
  }

  // sort faces to that sections are contiguous
  AUTO_STATIC_ARRAY(SortSecPoly,sortFaces,256);
  sortFaces.Realloc(_shape->NFaces());
  // insert polygons from all sections
  for (int i=0; i<_sections.Size(); i++)
  {
    const ShapeSectionWork &sw = _sections[i];
    for (int f=0; f<sw._faces.NIntervals(); f++)
    {
      const Interval &i = sw._faces.GetInterval(f);
      Assert (i.end>i.beg);
      Offset fBeg = _offsets[i.beg];
      Offset fEnd = _offsets[i.end];
      SortSecPoly &sp = sortFaces.Append();
      sp.polyBeg = fBeg;
      sp.polyEnd = fEnd;
      sp.polyIndexBeg = i.beg;
      sp.polyIndexEnd = i.end;
      Assert (fEnd>fBeg);
      sp.sec = &sw;
    }
  }
  Assert(sortFaces.Size()<=_shape->NFaces());
  // sort by alpha, texture, material ... as necessary
  QSort(sortFaces.Data(),sortFaces.Size(),CompareSecPoly);
  // sortFaces is sorted
  if (someAlpha)
  {
    //if there are several faces marked as IsAlphaSorted,
    // but texture contains no alpha information, we may resort them as necessary
    int begSort = -1;
    int endSort = -1;
    for (int i=0; i<sortFaces.Size(); i++)
    {
      const SortSecPoly &faceI = sortFaces[i];
      int spec = faceI.sec->Special();
      if ((spec&(IsAlphaOrdered|IsAlpha))==IsAlphaOrdered)
      {
        // the face is alpha ordered, but not alpha - we may sort it
        if (begSort<0)
        {
          begSort = i;
        }
        else
        {
          Assert(endSort==i);
        }
        endSort = i+1;
      }
      else if (endSort>0)
      {
        QSort(sortFaces.Data()+begSort,endSort-begSort,CompareSecPolyNoA);
        endSort = -1;
        begSort = -1;
      }
    }
    if (endSort>0)
    {
      QSort(sortFaces.Data()+begSort,endSort-begSort,CompareSecPolyNoA);
    }
  }
  
  // we have to reorder faces in shape now
  #if 0
  // dump info about textures
  for (int i=0; i<sortFaces.Size(); i++)
  {
    SortSecPoly &sp = sortFaces[i];
    LogF
    (
      "  %d:sp %s,%x",
      i,(const char *)sp.poly->GetDebugText(),sp.sec
    );
  }
  #endif

  AutoArray<int> reorder;
  reorder.Resize(_shape->NFaces());
  int order = 0;
  for (int i=0; i<sortFaces.Size(); i++)
  {
    int siBeg = sortFaces[i].polyIndexBeg;
    int siEnd = sortFaces[i].polyIndexEnd;
    for (int si=siBeg; si<siEnd; si++)
    {
      reorder[si] = order++;
    }
  }

  // this means reordering all named selections
  for (int s=0; s<_shape->NNamedSel(); s++)
  {
    NamedSelection::Access sel = _shape->NamedSel(s,_lShape);
    if (sel.Faces().Size()<=0) continue;
    Selection &faceSel = sel.SetFaces();

    AutoArray<VertexIndex> newSel;
    newSel.Resize(faceSel.Size());
    for (int f=0; f<faceSel.Size(); f++)
    {
      int of = faceSel[f];
      int nf = reorder[of];
      newSel[f] = nf;
    }
    faceSel = Selection(newSel.Data(),newSel.Size());
  }
  // now we can reorder all faces 
  // we may directly create sections during final sort
  FaceArray sorted;
  sorted.RawRealloc(_shape->FacesRawSize());
  ShapeSection sec;
  const ShapeSectionWork *lastSec = NULL;
  AutoArray<ShapeSection> sections;
  for (int i=0; i<sortFaces.Size(); i++)
  {
    const SortSecPoly &sp = sortFaces[i];
    Offset oBeg = sorted.End();
    sorted.Merge
    (
      (const char *)&_shape->Face(sp.polyBeg),
      OffsetDiff(sp.polyEnd,sp.polyBeg),
      sp.polyIndexEnd-sp.polyIndexBeg
    );
    Offset oEnd = sorted.End();

    if (sp.sec!=lastSec)
    {
      if (lastSec)
      {
        sections.Add(sec);
      }
      sec.SetInfo(*sp.sec);
      sec.beg = oBeg;
      sec.end = oEnd;
      lastSec = sp.sec;
    }
    else
    {
      Assert(sec.end==oBeg);
      sec.end = oEnd;
    }
  }
  if (lastSec)
  {
    sections.Add(sec);
  }
  sorted.SetSections(sections.Data(),sections.Size());
  _shape->SetFaces(sorted);
  // now we have to check which selections contain which sections
  // check on by-selection basis
  // it is guraanteed now section is either contained whole
  // or not at all in any section-aware selection

  #if 0
  // dump info about all sections
  for (int i=0; i<_shape->NSections(); i++)
  {
    const ShapeSection &sec = _shape->GetSection(i);
    LogF
    (
      "Section %d -  %s (%d..%d)",
      i,(const char *)sec.GetDebugText(),
      sec.beg,sec.end
    );
  }
  #endif
}

void Shape::RescanSections(const LODShape *lodShape)
{
  // check all sections against all section-aware selections
  for (int i=0; i<NNamedSel(); i++)
  {
    const NamedSelection::Access &sel = NamedSel(i,lodShape);
    if (!sel.GetNeedsSections()) continue;
    if (sel.Faces().Size()<=0) continue;
    //sel.FaceOffsets(_shape); // get ready face offsets
    sel.RescanSections(this);
  }
}

void Shape::AddFaceAndCreateSection(const Poly &face, const ShapeSectionInfo &prop)
{
  Offset o = _face.Add(face);

  ShapeSection ss;
  ss.SetInfo(prop);
  ss.beg = o;
  ss.end = o;
  NextFace(ss.end);
  _face._sections.Add(ss);
}

/*!
This function is relatively slow, as section against selections and sorting is necessary
*/
void Shape::FindSections(const LODShape *lodShape, bool forceMaterial0)
{
  FindShapeSections find(lodShape,this);
  find.Perform(forceMaterial0);

  _allTexturesReady = false;
  
  // convert textures to materials when necessary
  for (int i=0; i<NSections(); i++)
  {
    ShapeSection &ss = GetSection(i);
    if (ss.GetMaterialExt()) continue;
    ss.SetMaterialExt(GTexMaterialBank.TextureToMaterial(ss.GetTexture()));
  }

  // Set the vertex declaration according to sections
  VertexDeclarationNeedsUpdate();

  SetCanBeInstanced();
}

void Shape::SetAsOneSection(const ShapeSectionInfo &sec)
{
  ShapeSection ss;
  ss.SetInfo(sec);
  ss.beg = BeginFaces();
  ss.end = EndFaces();

  Faces().SetSections(&ss,1);

  // Set the vertex declaration according to sections
  VertexDeclarationNeedsUpdate();
}

void Shape::CheckPushbufferCanBeUsed() const
{
  // In case some selections need sections, it looks like the shape is animated and must not be pushbuffered
  for (int i = 0; i < NNamedSel(); i++)
  {
    const NamedSelection &sel = NamedSel(i);
    if (sel.GetNeedsSections())
    {
      DisablePushBuffer();
      return;
    }
  }

// HOTFIX: causing Multiple floating point traps in Texture::PrepareAndCheckMipmapLevel() - areaOTex == 1.#INF000
/*
  if (_canUsePushBuffer)
  {
    unconst_cast(this)->BuildTexAreaIndex();
  }
*/
}

void Shape::SetCanBeInstanced()
{
  _canBeInstanced = true;
  for (int i = 0; i < NSections(); i++)
  {
    TexMaterial *material = GetSection(i).GetMaterialExt();
    if (material)
    {
      if (!GEngine->CanBeInstanced(material->GetVertexShaderID()))
      {
        _canBeInstanced = false;
        break;
      }
    }
    else
    {
      if (!GEngine->CanBeInstanced(VSBasic))
      {
        _canBeInstanced = false;
        break;
      }
    }
  }
}

void Shape::UpdateShapePropertiesNeeded()
{
  _shapePropertiesNeeded = 0;
  for (int i = 0; i < NSections(); i++)
  {
    TexMaterial *material = GetSection(i).GetMaterialExt();
    if (material)
    {
      _shapePropertiesNeeded |= GEngine->ShapePropertiesNeeded(material);
    }
  }
}

void Shape::UpdateVertexDeclarationFromSections()
{
  _isVertexDeclarationAvailable = true;
  // Get the declaration mask from all the sections
  int vertexDeclarationMask = 0;
  for (int i = 0; i < NSections(); i++)
  {
    TexMaterial *material = GetSection(i).GetMaterialExt();
    if (material)
    {
      vertexDeclarationMask |= material->GetVertexDeclaration();
    }
    else
    {
      vertexDeclarationMask |= VertexDeclarations[ShaderVertexDeclarations[VSBasic]];
    }
  }
  if (vertexDeclarationMask==0)
  {
    // this is normal for an empty models (no sections) - quite common
    _vertexDeclaration = VD_Position_Normal_Tex0;
    return;
  }

  // Get and remember the closest declaration closure
  for (int i = 0; i < VDCount; i++)
  {
    if (!(~VertexDeclarations[i] & vertexDeclarationMask))
    {
      _vertexDeclaration = EVertexDecl(i);
      return;
    }
  }

  // Common declaration was not found, use default
  LogF("Warning: There is not a vertex declaration available to represent the shape. Using the default.");
  _vertexDeclaration = VD_Position_Normal_Tex0;
}

bool LODShape::IsCockpit(int level) const
{
  if (level==LOD_INVISIBLE || level<_nGraphical) return false;
  float resolution = _resolutions[level];
  if( IsSpec(resolution,VIEW_GUNNER) ) return true;
  else if( IsSpec(resolution,VIEW_PILOT) ) return true;
  else if( IsSpec(resolution,VIEW_CARGO) ) return true;
  return false;
}

const char *LODShape::LevelName(float resolution)
{
  static char buf[256];
  if( IsSpec(resolution,GEOMETRY_SPEC) ) return "geometry";
  else if( IsSpec(resolution,MEMORY_SPEC) ) return "memory";
  else if( IsSpec(resolution,LANDCONTACT_SPEC) ) return "landContact";
  else if( IsSpec(resolution,ROADWAY_SPEC) ) return "roadway";
  else if( IsSpec(resolution,PATHS_SPEC) ) return "paths";
  else if( IsSpec(resolution,HITPOINTS_SPEC) ) return "hitpoints";
  else if( IsSpec(resolution,VIEW_GEOM_SPEC) ) return "geometryView";
  else if( IsSpec(resolution,FIRE_GEOM_SPEC) ) return "geometryFire";
  else if( IsSpec(resolution,VIEW_PILOT_GEOM_SPEC) ) return "geometryViewPilot";
  else if( IsSpec(resolution,VIEW_GUNNER_GEOM_SPEC) ) return "geometryViewGunner";
  else if( IsSpec(resolution,VIEW_CARGO_GEOM_SPEC) ) return "geometryViewCargo";
  else if( IsSpec(resolution,VIEW_GUNNER) ) return "VIEW_GUNNER";
  else if( IsSpec(resolution,VIEW_PILOT) ) return "VIEW_PILOT";
  else if( IsSpec(resolution,VIEW_CARGO) ) return "VIEW_CARGO";
  else if( IsSpec(resolution,WRECK_SPEC) ) return "wreck";
  else if( IsSpecInterval(resolution,SHADOWVOLUME_SPEC,SHADOWBUFFER_MAX) )
  {
    sprintf(buf,"shadow(%g)",resolution-SHADOWVOLUME_SPEC);
    return buf;
  }
  
  sprintf(buf,"%g",resolution);
  return buf;
}

/**
@return pointer to static variable passed as a result
*/
const char *LODShape::ShapeName(int level ) const
{
  static char buf[256];
  strcpy(buf,GetName());
  strcat(buf," ");
  const RefShapeRef &lRef = LevelRef(level);
  Shape *shape = lRef.GetRef();
  int faces = LevelRef(level).NFaces();
  const char *prefix = "";
  if (IsShadowVolume(level)) prefix = "SV ";
  else if (level>=_nGraphical) prefix = "S ";
  if (shape)
  {
    sprintf(buf+strlen(buf), "%s%d|%d", prefix,faces,shape->NSections());
  }
  else
  {
    sprintf(buf+strlen(buf), "%s%d", prefix, faces);
  }
  return buf;
}

const char *LODShape::LevelName(const Shape *level) const
{
  for (int i=0; i<NLevels(); i++)
  {
    if (GetLevelLocked(i)==level) return LevelName(Resolution(i));
  }
  return "Error";
}


void LODShape::Optimize()
{
  Assert(!_streaming);
  for( int level=0; level<NLevels(); level++ )
  {
    Shape *shape = InitLevelLocked(level);
    if( !shape ) continue;
    shape->Optimize();
  }
}

void LODShape::InternalTransform(const Matrix4 &transform, bool recalculateMass)
{
  REPORTSTACKITEM(GetName());
  Assert(!_streaming);
  for( int level=0; level<NLevels(); level++ )
  {
    Shape *shape = InitLevelLocked(level);
    if( !shape ) continue;
    for( int i=0; i<shape->NPos(); i++ )
    {
      Vector3 &pos=shape->SetPos(i);
      pos+=_boundingCenter; // restore original position
      pos.SetFastTransform(transform,pos);
    }
    for( int p=0; p<shape->_phase.Size(); p++ )
    {
      for( int i=0; i<shape->_phase[p].Size(); i++ )
      {
        Vector3 &pos=shape->_phase[p][i];
        pos+=_boundingCenter;
        pos.SetFastTransform(transform,pos);
      }
    }
    _boundingCenter=VZero;
    shape->RecalculateNormals(true);
#if _VBS3_CRATERS_DEFORM_TERRAIN
    if (IsNormalGraphicalLod(level))
#endif
    {
      shape->RecalculateAreas();
    }
  }
  CalculateMinMax(true);
  if (recalculateMass) CalculateMass();
  //ScanProperties();
}

bool LODShape::IsAnimated()
{
  for(int level = 0; level < NLevels(); level++)
  {
    ShapeUsed shape=Level(level);
    if( shape.IsNull() ) continue;
    if (shape->IsAnimated()) return true;
  }
  return false;
}

bool Shape::VerifyStructure() const
{
  return true;
  //return _face.VerifyStructure();
}

void Shape::SetFaces(const FaceArray &src)
{
  _face = src;
  // all named selections must be reordered before calling this function
  //_sel.Clear();
  // any other face offset should be invalidated
  _faceIndexToOffset.Clear();
}
void Shape::SetSpecial( int special )
{
  _special=special;
  for (int s=0; s<_face._sections.Size(); s++)
  {
    _face._sections[s].SetSpecial(special);
  }
}

void Shape::MakeCockpit() const
{
  for (int s=0; s<NSections(); s++)
  {
    const ShapeSection &ss = GetSection(s);
    // some faces can be with no texture at all
    Texture *tex = ss.GetTexture();
    if (tex)
    {
      tex->SetUsageType(Texture::TexCockpit);
    }
  }
}

void Shape::UpdateVertexReferences(VertexIndex oldVI, VertexIndex newVI)
{
  // Find the selections the old vertex belongs to and add the new vertex to the same selections
  for (int j = 0; j < _sel.Size(); j++)
  {
    Selection &selection = _sel[j].SetData();
    int vertexInSelection;
    if ((vertexInSelection = selection.Find(oldVI)) >= 0)
    {
      selection.Add(newVI, selection.Weight(vertexInSelection));
    }
  }

  // Update _vertexToPoint
  if (_vertexToPoint.Size() > 0)
  {
    _vertexToPoint.Access(newVI);
    _vertexToPoint[newVI] = VertexToPoint(oldVI);
  }

  // Update keyframe animation
  for (int p = 0; p < _phase.Size(); p++)
  {
    AnimationPhase &animationPhase = _phase[p];
    DoAssert(animationPhase.Size() == newVI);
    animationPhase.Resize(newVI + 1);
    animationPhase[newVI] = animationPhase[oldVI];
  }
}

VertexIndex Shape::ReplicateVertex(VertexIndex vi)
{
  // Add new vertex
  Vector3 pos = Pos(vi);
  Vector3 norm = Norm(vi).Get();
  ClipFlags cf = Clip(vi);
  float u = U(vi);
  float v = V(vi);
  // optimization: minUV, maxUV will be unchanged, calculate invUV directly
  UVPair invUV(1 / (_data->_tex.MaxUV().u - _data->_tex.MinUV().u), 1 / (_data->_tex.MaxUV().v - _data->_tex.MinUV().v));
  VertexIndex newVi = AddVertexFast(pos, norm, cf, invUV, u, v);
  
  // Update additional UV sets (if any)
  for (int c=0; c<lenof(_data->_texCoord); c++)
  {
    // update UVs only when they are set
    if (_data->_texCoord[c].Size()>0)
    {
      UVPairs &uv = _data->_texCoord[c];
      uv.Expand();
      if (uv.Size() < newVi + 1) uv.Resize(newVi + 1);
      uv.SetUVRaw(newVi, uv.GetUVRaw(vi));
    }
    
  }

  // Update the references to vertices
  UpdateVertexReferences(vi, newVi);

  return newVi;
}

void Shape::SeparateFaceVertices()
{
  // Key frame animation must not exist
  Assert(_phase.Size() == 0);

  // Prepare a temporary array for marking of used vertices
  AutoArray<bool> vertexAlreadyUsed;
  vertexAlreadyUsed.Realloc(NVertex());
  vertexAlreadyUsed.Resize(NVertex());
  for (int i = 0; i < vertexAlreadyUsed.Size(); i++) vertexAlreadyUsed[i] = false;

  // Go through all the faces
  for (Offset offset = BeginFaces(); offset < EndFaces(); NextFace(offset))
  {
    Poly &face = Face(offset);

    // Go through all the vertices
    for (int i = 0; i < face.N(); i++)
    {
      VertexIndex vi = face.GetVertex(i);
      if (vertexAlreadyUsed[vi])
      {
        // Redirect the face to the new vertex
        face.Set(i, ReplicateVertex(vi));
      }
      else
      {
        // Mark the vertex as used
        vertexAlreadyUsed[vi] = true;
      }
    }
  }
}

void Shape::SetUniformMaterial(TexMaterial *material)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    sec.SetMaterialExt(material);
  }
}

void Shape::SetUniformMaterial(TexMaterial *material, Texture *texture)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    sec.SetMaterialExt(material);
    sec.SetTexture(texture);
  }
}

void Shape::SetUniformMaterialEmmisiveColor(const Color &color)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    TexMaterial *mat = sec.GetMaterialExt();
    Assert(mat);
    if (mat) mat->_emmisive = color;
  }
}

void Shape::SetUniformMaterialAmbientColor(const Color &color)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    TexMaterial *mat = sec.GetMaterialExt();
    //Assert(mat);
    if (mat) mat->_ambient = color;
  }
}

void Shape::SetUniformMaterialDiffuseColor(const Color &color)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    TexMaterial *mat = sec.GetMaterialExt();
    //Assert(mat);
    if (mat) mat->_diffuse = color;
  }
}

void Shape::SetUniformMaterialForcedDiffuseColor(const Color &color)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    TexMaterial *mat = sec.GetMaterialExt();
    //Assert(mat);
    if (mat) mat->_forcedDiffuse = color;
  }
}

void Shape::SetUniformMaterialRenderFlag(RenderFlag rf, bool value)
{
  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    TexMaterial *mat = sec.GetMaterialExt();
    //Assert(mat);
    if (mat) mat->SetRenderFlag(rf, value);
  }
}

void Shape::CreateVertexBoneRef(
  const Ref<SkeletonSource> &skeletonSource, AutoArray<AnimationRTWeight> &vertexBoneRefTemp, const LODShape *lodShape
)
{
  vertexBoneRefTemp.Realloc(NVertex());
  vertexBoneRefTemp.Resize(NVertex());

  int lastBone2 = -1;

  // Note that we need the backward cycle here, because there's a defined order on
  // matrices: the lower is the index of a matrix the higher is it in the
  // transformation hierarchy. In discrete mode we use only the first we meet
  // - the closest one to the leaf
  for (int i = skeletonSource->Structure().Size() - 1; i >= 0; i--)
  {
    RStringB name = skeletonSource->Structure()[i]._bone;
    int selIndex = FindNamedSel(name);
    if (selIndex >= 0)
    {
      const NamedSelection::Access &sel = NamedSel(selIndex,lodShape);
      for (int si = 0; si < sel.Size(); si++)
      {
        int index = sel[si];
        AnimationRTWeight &entry = vertexBoneRefTemp[index];
        // In case we use discrete skeleton then make sure we use only one weight
        if (!skeletonSource->IsDiscrete() || entry.Size() == 0)
        {
          unsigned char selW = sel.Weight(si);
          SubSkeletonIndex ssi;
          SetSubSkeletonIndex(ssi, i);
          AnimationRTPair pair(ssi, selW);  // Note that we store skeleton index here - will be replaced later
          int w = entry.Add(pair);
          if (w < 0)
          {
            // try to find bone with less influence than this one
            int minIW = selW;
            int minI = -1;
            for (int i = 0; i < entry.Size(); i++)
            {
              int iw = entry[i].GetWeight8b();
              if (minIW > iw) minIW = iw, minI = i;
            }
            int vIndex = _vertexToPoint[index];
            if (minI > 0)
            {
              // model diagnostic: report too many bones on one vertex
              int oldBone = GetSubSkeletonIndex(AnimationRTPair::SelToSubSkeletonIndex(entry[minI]._sel));
              RptF("Warning: %s:%s, too many bones (more than 4) on vertex %d, removed %s",cc_cast(lodShape->Name()),lodShape->LevelName(this),vIndex,cc_cast(skeletonSource->Structure()[oldBone]._bone));
              entry[minI] = pair;
            }
            else
            {
              RptF("Warning: %s:%s, too many bones (more than 4) on vertex %d, removed %s",cc_cast(lodShape->Name()),lodShape->LevelName(this),vIndex,cc_cast(skeletonSource->Structure()[i]._bone));
          }
        }
      }
        else
        {
          // model diagnostic: report all shared vertices between bones on discrete skeleton  
          int bone2 = GetSubSkeletonIndex(AnimationRTPair::SelToSubSkeletonIndex(entry[0]._sel));
          if (lastBone2!=bone2)
          {
            int parentBone = bone2;
            do
            {
              parentBone = GetSkeletonIndex(skeletonSource->GetParent(SetSkeletonIndex(parentBone)));
    }
            while (parentBone!=-1 && parentBone!=i);

            if (parentBone!=-1)
              RptF("Warning: %s:%s, vertices of bone %s are shared with parent bone %s",cc_cast(lodShape->Name()),lodShape->LevelName(this),cc_cast(skeletonSource->Structure()[bone2]._bone),cc_cast(name));
            else
              RptF("Error: %s:%s, vertices of bone %s are shared with bone %s",cc_cast(lodShape->Name()),lodShape->LevelName(this),cc_cast(skeletonSource->Structure()[bone2]._bone),cc_cast(name));
  }
          lastBone2 = bone2;
        }
      }
    }
  }

  // Normalize loaded weighting information
  _vertexBoneRefIsSimple = true;
  for (int i = 0; i < vertexBoneRefTemp.Size(); i++)
  {
    // model diagnostic: find and report wrong sum of weights on vertices
    if (vertexBoneRefTemp[i].Size()>0)
    {
      float sum=0;
      for( int j=0; j<vertexBoneRefTemp[i].Size(); j++ )
      {
        sum +=vertexBoneRefTemp[i].Get(j).GetWeight();
      }
      if (sum < 0.8 || sum > 1.2) // ignore small numeric inaccuracy
      {
        int vIndex = _vertexToPoint[i];
        RptF("Warning: %s:%s, vertex: %d, sum of weights is %.0f, should be 100 ",cc_cast(lodShape->Name()),lodShape->LevelName(this),vIndex,sum*100);
      }
    }
    vertexBoneRefTemp[i].Normalize();
    if (vertexBoneRefTemp[i].Size()>1) _vertexBoneRefIsSimple = false;
  }
}

void Shape::CreateNighborBoneRef(const AutoArray<AnimationRTWeight> &vertexBoneRefTemp, AutoArray<VertexNeighborInfo> &neighborBoneRefTemp, const LODShape *lodShape, float resolution)
{
  neighborBoneRefTemp.Realloc(vertexBoneRefTemp.Size());
  neighborBoneRefTemp.Resize(vertexBoneRefTemp.Size());

  // Initialize the array with some values
  for (int i = 0; i < neighborBoneRefTemp.Size(); i++)
  {
    VertexNeighborInfo &vni = neighborBoneRefTemp[i];
    vni._posA = -1;
    vni._posB = -1;
  }

  // Go through all the sections
  for (int i = 0; i < NSections(); i++)
  {
    const ShapeSection &sec = GetSection(i);

    // Skip sections not designed for drawing
    if (Special() & IsHiddenProxy) continue;

    // Go through all the faces in the section
    for (Offset o = sec.beg; o < sec.end; NextFace(o))
    {
      const Poly &poly = Face(o);
      for (int v = 0; v < poly.N(); v++)
      {
        VertexIndex vertex = poly.GetVertex(v);
        VertexIndex vertexA = (v >= poly.N() - 1) ? poly.GetVertex(0) : poly.GetVertex(v + 1);
        VertexIndex vertexB = (v <= 0) ? poly.GetVertex(poly.N() - 1) : poly.GetVertex(v - 1);

        // Consider only vertices with non degenerated neighborhood (the vertex will be most likely
        // filled another time)
        if ((Pos(vertex) != Pos(vertexA)) && (Pos(vertexA) != Pos(vertexB)) && (Pos(vertexB) != Pos(vertex)))
        {
          VertexNeighborInfo &vni = neighborBoneRefTemp[vertex];
          vni._posA = vertexA;
          vni._rtwA = vertexBoneRefTemp[vertexA];
          vni._posB = vertexB;
          vni._rtwB = vertexBoneRefTemp[vertexB];
        }
      }
    }
  }

  // Check the neighborBoneRefTemp array for non - initialized values. If such values are found,
  // then the source model contains degenerated faces and we must report such case.
  bool uninitializedFound = false;
  for (int i = 0; i < neighborBoneRefTemp.Size(); i++)
  {
    VertexNeighborInfo &vni = neighborBoneRefTemp[i];
    if ((vni._posA < 0) || (vni._posB < 0))
    {
      uninitializedFound = true;

      // Fill out with reasonable values
      vni._posA = 0;
      vni._posB = 0;
    }
  }
  if (uninitializedFound)
  {
    RptF("Warning: Degenerated faces found in model %s:%s ", lodShape->Name(), lodShape->LevelName(resolution));
  }
}

struct SectionBones
{
  ShapeSection _section;
  FindArray<SkeletonIndex> _bones;
};
TypeIsMovable(SectionBones);

//! bonesInterval limit
#define MAX_ALLOWED_BONES 128

//! Setting of section bone references
/*!
  The function tries to set sectionBones _minBoneIndex and _bonesCount members. If succeeded (all
  desired bones are within MAX_ALLOWED_BONES interval) it will add all required bones to
  subSkeletonToSkeleton array and returns true, otherwise it won't change anything and returns false.
*/
bool SetSectionBoneRef(int minBoneIndex, int bonesInterval, AutoArray<SkeletonIndex> &subSkeletonToSkeleton, SectionBones &sectionBones)
{

  // Remember the maximum of bones we can add
  const int maxNewBones = 
  #ifdef _WIN32
  __max(0, bonesInterval - (subSkeletonToSkeleton.Size() - minBoneIndex));
  #else
  std::max(0, bonesInterval - (subSkeletonToSkeleton.Size() - minBoneIndex));
  #endif
  //max(0, bonesInterval - (subSkeletonToSkeleton.Size() - minBoneIndex));

  // Try to use existing bones, possibly add new bones, remember maximum index of a used bone
  DoAssert(bonesInterval <= MAX_ALLOWED_BONES);
  AUTO_STATIC_ARRAY(SkeletonIndex, newBones, MAX_ALLOWED_BONES);
  int maxUsedIndex = -1;
  for (int i = 0; i < sectionBones._bones.Size(); i++)
  {
    bool boneFound = false;
    for (int j = minBoneIndex; (j < subSkeletonToSkeleton.Size()) && (j - minBoneIndex < bonesInterval); j++)
    {
      if (subSkeletonToSkeleton[j] == sectionBones._bones[i])
      {
        boneFound = true;
        if (maxUsedIndex < j) maxUsedIndex = j;
        break;
      }
    }
    if (!boneFound)
    {
      if (newBones.Size() < maxNewBones)
      {
        newBones.Add(sectionBones._bones[i]);
      }
      else
      {
        return false;
      }
    }
  }

  // We are going to succeed - add new bones (if any) to subskeleton, set the section references
  for (int i = 0; i < newBones.Size(); i++)
  {
    maxUsedIndex = subSkeletonToSkeleton.Add(newBones[i]);
  }
  sectionBones._section._minBoneIndex = minBoneIndex;
  sectionBones._section._bonesCount = maxUsedIndex - minBoneIndex + 1;
  return true;
}

static void AddVertexBones(const AnimationRTWeight &rtw, const SectionBones &newSection, FindArray<SkeletonIndex,MemAllocSA> &faceBonesNew, FindArray<SkeletonIndex,MemAllocSA> &faceBonesAll, AutoArray< FindArray<SkeletonIndex> > &boneGroups)
{
  for (int j = 0; j < rtw.Size(); j++)
  {
    int b = GetSubSkeletonIndex(rtw[j].GetSel()); // Don't be confused - we've got the SkeletonIndex here

    SkeletonIndex si = SetSkeletonIndex(b);

    // Check if the bone isn't already present in the faceBonesNew array
    if (newSection._bones.Find(si) < 0)
    {
      faceBonesNew.AddUnique(si);
    }
    // Unique add of all bones
    faceBonesAll.AddUnique(si);

    // check groups of bones (keep some bones together)
    for (int g=0; g<boneGroups.Size(); g++)
    {
      FindArray<SkeletonIndex> &group = boneGroups[g];
      
      int index = group.Find(si);
      if (index >= 0)
      {
        // bone is in group, add all bones from this group
        for (int i=0; i<group.Size(); i++)
        {
          if (i == index) continue; // already added

          SkeletonIndex si2 = group[i];

          // same as for si
          if (newSection._bones.Find(si2) < 0)
            faceBonesNew.AddUnique(si2);
          faceBonesAll.AddUnique(si2);
        }
      }
    }
  }
}

static AnimationRTWeight GetSubSkeletonVBR(const AnimationRTWeight &vbr, const Shape &shape, int minBoneIndex, int maxBoneIndex, const Ref<SkeletonSource> &skeletonSource)
{
  AnimationRTWeight vbrNew;
  vbrNew.Resize(vbr.Size());
  for (int j = 0; j < vbr.Size(); j++)
  {
    const AnimationRTPair &pair = vbr[j];
    int b = GetSubSkeletonIndex(pair.GetSel()); // Don't be confused - we've got the SkeletonIndex here
    SkeletonIndex si = SetSkeletonIndex(b);

    // Get the set of subskeleton indices
    const SubSkeletonIndexSet &ssis = shape.GetSubSkeletonIndexFromSkeletonIndex(si);

    // Find the best suitable SubSkeletonIndex (that one which lies in the section interval)
    // If not such one is found then it is a serious error.
    bool found = false;
    for (int s = 0; s < ssis.Size(); s++)
    {
      int index = GetSubSkeletonIndex(ssis[s]);
      if ((index >= minBoneIndex) && (index <= maxBoneIndex))
      {
        vbrNew[j] = AnimationRTPair(ssis[s], pair.GetWeight8b());
        found = true;
        break;
      }
    }
    if (!found)
    {
      RptF("Bone in range %d ... %d not found in subskeleton:", minBoneIndex, maxBoneIndex);
      for (int s = 0; s < ssis.Size(); s++)
      {
        int index = GetSubSkeletonIndex(ssis[s]);
        RptF("- %d", index);
      }
    }
  }
  return vbrNew;
}

/**
@param src source
@param parent bone we are processing now
@param test check if given bone should be included
@param store functor to allow storing additional payload for each bone

This function assumes selected is constructed in such a way that when a child is included, his parent is included as well.
*/
template <class Predicate, class StorePayload>
int BoneTree::AddChildrenOf(
  const SkeletonSource *src, SkeletonIndex bone, const Predicate &test, const StorePayload &store
)
{
  // create a fix-up
  int fixup = _children.Add(-1);
  // store a payload
  store(bone);
  int nChildren = 0;
  for (int i=0; i<src->Structure().Size(); i++)
  {
    SkeletonIndex si = SetSkeletonIndex(i);
    if (src->GetParent(si)!=bone) continue;
    if (!test(si)) continue;
    nChildren++;
    AddChildrenOf(src,si,test,store);
  }
  _children[fixup] = nChildren;
  return nChildren;
}

int BoneTree::AddChildrenOf(const SkeletonSource *src, SkeletonIndex bone)
{
  return AddChildrenOf(src,bone,AllPass(),NoPayload());
}


  struct AddBone
  {
    AutoArray<SBoneRelation> &_ordered;
    const SkeletonSource *_source;
    
    AddBone(AutoArray<SBoneRelation> &ordered, const SkeletonSource *source)
    :_ordered(ordered),_source(source)
    {}
    
    void operator () (SkeletonIndex bone) const
    {
      // do not store root - no real bone
      if (!SkeletonIndexValid(bone)) return;
      SBoneRelation &rel = _ordered.Append();
      rel = _source->GetBoneInfo(GetSkeletonIndex(bone));
    }
  };

void BoneTree::Build(const SkeletonSource *src, AutoArray<SBoneRelation> &ordered)
{
  // traverse the hierarchy
  // for each parent add all his children recursively
  BoneTree bones;

  AddChildrenOf(src,SkeletonIndexNone,AllPass(),AddBone(ordered,src));
}

struct AddBoneBuild
  {
    AutoArray<SkeletonIndex> &_ordered;
    const SkeletonSource *_source;
    
  AddBoneBuild(AutoArray<SkeletonIndex> &ordered, const SkeletonSource *source)
    :_ordered(ordered),_source(source)
    {}
    
    void operator () (SkeletonIndex bone) const
    {
      // do not store root - no real bone
      if (!SkeletonIndexValid(bone)) return;
      _ordered.Add(bone);
    }
  };

void BoneTree::Build(const SkeletonSource *src, AutoArray<SkeletonIndex> &ordered)
{
  // traverse the hierarchy
  // for each parent add all his children recursively
  BoneTree bones;

  AddChildrenOf(src,SkeletonIndexNone,AllPass(),AddBoneBuild(ordered,src));
}


#if _DEBUG


struct BoneTree::UnitTest
{
  static bool TestBasicTraversal()
  {
    BoneTree test;
    // 2 2 0 0 1 0 
    test._children.Add(2);
    test._children.Add(2);
    test._children.Add(0);
    test._children.Add(0);
    test._children.Add(1);
    test._children.Add(0);
    RString testResult;
    struct TestBone
    {
      RString &_result;
      TestBone(RString &result):_result(result){}
      int operator () (int bone, int &parent) const
      {
        char add[4];
        add[0] = 'A'+bone;
        add[1] = 'A'+parent;
        add[2] = ' ';
        add[3] = 0;
        _result = _result + add;
        parent = bone;
        return bone;
      }
    };
    RString result;
    test.Process(TestBone(result));
    return result=="AA BA CB DB EA FE ";
  }
  UnitTest()
  {
    Assert(TestBasicTraversal());
  }
};

BoneTree::UnitTest BoneTree::unitTest;
#endif


/// create a hierarchical representation for the subskeleton
/**
We need to store bone indices as well
We need to store all parents references by any subskeleton bones

Note: subskeleton is a subset of skeleton, with an identical ordering.
However, it does not contain all referenced parents. We may need to add them into the hierarchical representation.
*/

struct TestUsed
{
  const Array<bool> &_used;

  TestUsed(const Array<bool> &used):_used(used){}
  bool operator () (SkeletonIndex i) const
  {
    return _used[GetSkeletonIndex(i)];
  }

};

struct StoreIndex
{
  AutoArray<SkeletonIndex> &_si;

  StoreIndex(AutoArray<SkeletonIndex> &si):_si(si){}
  void operator () (SkeletonIndex i) const
  {
    if (!SkeletonIndexValid(i)) return;
    _si.Add(i);
  }
};

void Shape::CreateSubskeletonHierarchy(const SkeletonSource *ss)
{
  // traverse the skeleton hierarchy
  // add bones which are needed into the subskeleton hierarchy
  
  // create a list of needed bones, including the parents needed by bones used
  // the bones may be listed in any order
  AutoArray< bool, MemAllocLocal<bool,128> > boneUsed;
  boneUsed.Resize(ss->NBones());
  for (int i=0; i<boneUsed.Size(); i++) boneUsed[i] = false;
  for (int i=0; i<ss->NBones(); i++)
  {
    // check if the bone is used directly
    SkeletonIndex si = SetSkeletonIndex(i);
    if (_subSkeletonToSkeleton.Find(si)>=0)
    {
      // add the bone with all parents
      for(SkeletonIndex bone = si; SkeletonIndexValid(bone); )
      {
        boneUsed[GetSkeletonIndex(bone)] = true;
        // proceed to the parent
        bone = ss->GetParent(bone);
        // if there is no parent or the parent is already set, break
        if (!SkeletonIndexValid(bone) || boneUsed[GetSkeletonIndex(bone)]) break;
      }
      
    }
  }
  
  BoneTree ssTree;
  // now build the representation of the subskeleton
  // each bone represented as a skeleton index
  // representing using subskeleton index is not possible, as multiple subskeleton indices may match one bone
    
  // size may differ, however subskeleton size is a reasonable estimation
  _ssTreeSI.Realloc(_subSkeletonToSkeleton.Size());
  _ssTree.AddChildrenOf(ss,SkeletonIndexNone,TestUsed(boneUsed),StoreIndex(_ssTreeSI));
  _ssTreeSI.Compact();
  _ssTree.Compact();
}

void Shape::CreateSubskeleton(const Ref<SkeletonSource> &skeletonSource, bool includeNeighbourFaceVertices, int bonesInterval, const LODShape *lodShape, float resolution, const ModelConfig *modelConfig)
{
  // read group of bones we need to keep together
  AutoArray< FindArray<SkeletonIndex> > boneGroups;
  if (modelConfig)
  {
    ConstParamEntryPtr cls = (modelConfig->GetCfgSkeletons() >> skeletonSource->GetName()).FindEntry("BoneGroups");
    if (cls)
    {
      for (int i=0; i<cls->GetEntryCount(); i++)
      {
        ParamEntryVal entry = cls->GetEntry(i);
        if (!entry.IsArray()) continue;
        // new group of bones
        FindArray<SkeletonIndex> &group = boneGroups.Append();
        for (int j=0; j<entry.GetSize(); j++)
        {
          RString name = entry[j];
          name.Lower();
          int bone = skeletonSource->FindBone(name);
          if (bone >= 0) group.Add((SkeletonIndex)bone);
        }
      }
    }
  }

  // For each vertex create a reference to skeleton (SkeletonIndex - later it will be replaced by SubSkeletonIndex)
  AutoArray<AnimationRTWeight> vertexBoneRefTemp;
  CreateVertexBoneRef(skeletonSource, vertexBoneRefTemp,lodShape);

  // Create neighbor bone references (at this point we can rely on the fact that shared vertices are
  // really shared (not only they have got the same position))
  AutoArray<VertexNeighborInfo> neighborBoneRefTemp;
  if (includeNeighbourFaceVertices)
  {
    CreateNighborBoneRef(vertexBoneRefTemp, neighborBoneRefTemp, lodShape, resolution);
  }

  // Create an array of sections from original sections. If original section contains
  // more then max allowed number of bones then split it. Also get the set of bones
  // referenced by each section.
  AutoArray<SectionBones> sections;
  {

    // Go through the original sections
    for (int i = 0; i < NSections(); i++)
    {
      const ShapeSection &originalSection = GetSection(i);

      // Add new section and fill out its initial values
      SectionBones *newSection = &sections.Append();
      newSection->_section = originalSection;
      newSection->_section.beg = originalSection.beg;

      // Go through faces of the original section
      for (Offset f = originalSection.beg; f < originalSection.end; NextFace(f))
      {
        const Poly &poly = Face(f);

        // Get bones referenced by the face
        AUTO_FIND_ARRAY(SkeletonIndex, faceBonesNew, 16); // Only new bones
        AUTO_FIND_ARRAY(SkeletonIndex, faceBonesAll, 16); // All bones referenced by the face

        for (int v = 0; v < poly.N(); v++)
        {
          VertexIndex vi = poly.GetVertex(v);
          AddVertexBones(vertexBoneRefTemp[vi], *newSection, faceBonesNew, faceBonesAll, boneGroups);
          if (includeNeighbourFaceVertices)
          {
            AddVertexBones(neighborBoneRefTemp[vi]._rtwA, *newSection, faceBonesNew, faceBonesAll, boneGroups);
            AddVertexBones(neighborBoneRefTemp[vi]._rtwB, *newSection, faceBonesNew, faceBonesAll, boneGroups);
          }
        }

        // If there is space enough to hold the new bones then add them, else close the
        // current section and start a new one
        if (newSection->_bones.Size() + faceBonesNew.Size() <= bonesInterval)
        {
          for (int j = 0; j < faceBonesNew.Size(); j++)
          {
            newSection->_bones.Add(faceBonesNew[j]);
          }
        }
        else
        {
          DoAssert(faceBonesAll.Size() <= bonesInterval);

          // Close the current section
          newSection->_section.end = f;

          // Start a new one
          newSection = &sections.Append();
          newSection->_section = originalSection;
          newSection->_section.beg = f;

          // Insert all the bones into it
          for (int j = 0; j < faceBonesAll.Size(); j++)
          {
            newSection->_bones.Add(faceBonesAll[j]);
          }
        }
      }

      // Close the current section
      newSection->_section.end = originalSection.end;
    }
  }

  // Get the list of subskeleton bones and fill out the sections bone references
  {

    // Get selection index for each bone
    AutoArray<int> boneSelectionIndex;
    {
      boneSelectionIndex.Realloc(skeletonSource->Structure().Size());
      boneSelectionIndex.Resize(skeletonSource->Structure().Size());
      for (int i = skeletonSource->Structure().Size() - 1; i >= 0; i--)
      {
        boneSelectionIndex[i] = FindNamedSel(skeletonSource->Structure()[i]._bone);
      }
    }

    // Init some stuff
    _subSkeletonToSkeleton.Clear();
    int lastMinBoneIndex = 0;
    int lastBonesCount = 0;

    // Consider each section
    for (int i = 0; i < sections.Size(); i++)
    {
      SectionBones &sectionBones = sections[i];

      // Try to set the new section on the last place
      if (SetSectionBoneRef(lastMinBoneIndex, bonesInterval, _subSkeletonToSkeleton, sectionBones))
      {
        // If the new section has got less bones than the previous one, then update the new section.
        // If the new section has got more bones than the previous one, then update all relevant previous sections.
        if (sectionBones._section._bonesCount <= lastBonesCount)
        {
          sectionBones._section._bonesCount = lastBonesCount;
        }
        else
        {
          for (int j = i - 1; (j >= 0) && (sections[j]._section._minBoneIndex == lastMinBoneIndex); j--)
          {
            sections[j]._section._bonesCount = sectionBones._section._bonesCount;
          }
        }

        // Update the last bones count
        lastBonesCount = sectionBones._section._bonesCount;
      }
      else
      {
        // Try to set the section at every possible place
        bool setSucceeded = false;
        for (int j = 0; j <= _subSkeletonToSkeleton.Size(); j++)
        {
          if (SetSectionBoneRef(j, bonesInterval, _subSkeletonToSkeleton, sectionBones))
          {
            lastMinBoneIndex = sectionBones._section._minBoneIndex;
            lastBonesCount = sectionBones._section._bonesCount;
            setSucceeded = true;
            break;
          }
        }
        DoAssert(setSucceeded);
      }
    }

    // Add the rest of the existing present bones (for example those selections which refer only points)
    for (int b = 0; b < boneSelectionIndex.Size(); b++)
    {
      int bsi = boneSelectionIndex[b];
      if (bsi >= 0)
      {
        if (NamedSel(bsi,lodShape).Size() > 0)
        {
          // Try to find the bone b in the bonesArray, if not found then add it
          bool boneFound = false;
          for (int i = 0; i < _subSkeletonToSkeleton.Size(); i++)
          {
            SkeletonIndex &si = _subSkeletonToSkeleton[i];
            if (GetSkeletonIndex(si) == b)
            {
              boneFound = true;
              break;
            }
          }
          if (!boneFound)
          {
            SkeletonIndex si = SetSkeletonIndex(b);
            _subSkeletonToSkeleton.Add(si);
          }
        }
      }
    }

    _subSkeletonToSkeleton.Compact();
  }

  // Fill out the _skeletonToSubSkeleton array
  {
    _skeletonToSubSkeleton.Clear();
    _skeletonToSubSkeleton.Realloc(skeletonSource->Structure().Size());
    _skeletonToSubSkeleton.Resize(skeletonSource->Structure().Size());
    for (int i = 0; i < _skeletonToSubSkeleton.Size(); i++)
    {
      SubSkeletonIndexSet &ssis = _skeletonToSubSkeleton[i];
      SkeletonIndex si = SetSkeletonIndex(i);
      for (int j = 0; j < _subSkeletonToSkeleton.Size(); j++)
      {
        if (_subSkeletonToSkeleton[j] == si)
        {
          SubSkeletonIndex ssi;
          SetSubSkeletonIndex(ssi, j);
          ssis.Add(ssi);
        }
      }
      ssis.Compact();
    }
  }

  /// create a hierarchical representation for the subskeleton
  CreateSubskeletonHierarchy(skeletonSource);

  // Log change in sections count
  if (_face._sections.Size() != sections.Size())
  {
    LogF("Info: Old sections %d, new sections %d", _face._sections.Size(), sections.Size());
  }

  // Copy temporary sections to shape sections
  _face._sections.Realloc(sections.Size());
  _face._sections.Resize(sections.Size());
  for (int i = 0; i < sections.Size(); i++)
  {
    _face._sections[i] = sections[i]._section;
  }

  // Convert vertex bone reference from SkeletonIndex to SubSkeletonIndex. It is also possible that some
  // vertices will be replicated here - we must use the ReplicateVertex function to add the new vertex
  // and also update the array of vertex bone references.
  if (_data)
  {

    // Final array of vertex bone references
    DoAssert(NVertex() == vertexBoneRefTemp.Size());
    _data->_vertexBoneRef.Realloc(vertexBoneRefTemp.Size());
    _data->_vertexBoneRef.Resize(vertexBoneRefTemp.Size());

    // Final array of neighbor bone references
    if (includeNeighbourFaceVertices)
    {
      DoAssert(NVertex() == neighborBoneRefTemp.Size());
      _data->_neighborBoneRef.Realloc(neighborBoneRefTemp.Size());
      _data->_neighborBoneRef.Resize(neighborBoneRefTemp.Size());
    }

    // Array to determine the vertex bone reference is already converted to SubSkeletonIndex,
    // and what is the next converted item
    // -2 (not converted itself) -1 (converted without ancestor) >=0 (index of the ancestor)
    AutoArray<int> vertexIsConverted;
    vertexIsConverted.Realloc(vertexBoneRefTemp.Size());
    vertexIsConverted.Resize(vertexBoneRefTemp.Size());
    for (int i = 0; i < vertexIsConverted.Size(); i++)
    {
      vertexIsConverted[i] = -2;
    }

    // Go through all the sections
    for (int i = 0; i < NSections(); i++)
    {
      const ShapeSection &ss = GetSection(i);
      int maxBoneIndex = ss._minBoneIndex + ss._bonesCount - 1;

      // Go through faces of the original section
      for (Offset f = ss.beg; f < ss.end; NextFace(f))
      {
        Poly &poly = Face(f);
        for (int v = 0; v < poly.N(); v++)
        {
          VertexIndex vi = poly.GetVertex(v);
          AnimationRTWeight vbrNew = GetSubSkeletonVBR(vertexBoneRefTemp[vi], *this, ss._minBoneIndex, maxBoneIndex, skeletonSource);
          AnimationRTWeight vbrNewA;
          AnimationRTWeight vbrNewB;
          if (includeNeighbourFaceVertices)
          {
            vbrNewA = GetSubSkeletonVBR(neighborBoneRefTemp[vi]._rtwA, *this, ss._minBoneIndex, maxBoneIndex, skeletonSource);
            vbrNewB = GetSubSkeletonVBR(neighborBoneRefTemp[vi]._rtwB, *this, ss._minBoneIndex, maxBoneIndex, skeletonSource);
          }

          // We've got the new vbr. Find a place where to put it to (go through the chain) and put it
          if (vertexIsConverted[vi] == -2)
          {
            _data->_vertexBoneRef[vi] = vbrNew;
            if (includeNeighbourFaceVertices)
            {
              _data->_neighborBoneRef[vi]._posA = neighborBoneRefTemp[vi]._posA;
              _data->_neighborBoneRef[vi]._rtwA = vbrNewA;
              _data->_neighborBoneRef[vi]._posB = neighborBoneRefTemp[vi]._posB;
              _data->_neighborBoneRef[vi]._rtwB = vbrNewB;
            }
            vertexIsConverted[vi] = -1;
          }
          else
          {
            VertexIndex viNew = vi;
            bool convert = true;
            while (1)
            {
              bool currAndNewMatch = true;

              if (!_data->_vertexBoneRef[viNew].BoneRefMatches(vbrNew)) currAndNewMatch = false;
              if (includeNeighbourFaceVertices)
              {
                if (!_data->_neighborBoneRef[viNew]._rtwA.BoneRefMatches(vbrNewA)) currAndNewMatch = false;
                if (!_data->_neighborBoneRef[viNew]._rtwB.BoneRefMatches(vbrNewB)) currAndNewMatch = false;
              }

              if (currAndNewMatch)
              {
                convert = false;
                break;
              }
              if (vertexIsConverted[viNew] < 0) break;
              viNew = vertexIsConverted[viNew];
            }
            if (convert)
            {
              DoAssert(vertexIsConverted[viNew] == -1);
              VertexIndex viNewAncestor = ReplicateVertex(viNew);
              poly.Set(v, viNewAncestor);

              DoAssert(_data->_vertexBoneRef.Size() == viNewAncestor);
              _data->_vertexBoneRef.Add(vbrNew);

              if (includeNeighbourFaceVertices)
              {
                DoAssert(_data->_neighborBoneRef.Size() == viNewAncestor);
                VertexNeighborInfo vni;
                vni._posA = neighborBoneRefTemp[vi]._posA;
                vni._rtwA = vbrNewA;
                vni._posB = neighborBoneRefTemp[vi]._posB;
                vni._rtwB = vbrNewB;
                _data->_neighborBoneRef.Add(vni);
              }

              // Add new item to the vertexIsConverted array and make it the ancestor of viNew item
              // Note the expression cannot look like this: vertexIsConverted[viNew] = vertexIsConverted.Add(-1),
              // because the [] operator is created prior the = one.
              int newItemIndex = vertexIsConverted.Add(-1);
              vertexIsConverted[viNew] = newItemIndex;
              DoAssert(_data->_vertexBoneRef.Size() == vertexIsConverted.Size());
            }
            else
            {
              // The viNew might differ from vi, so set it to be sure it is up to date
              poly.Set(v, viNew);
            }
          }
        }
      }
    }

    // Convert separate vertices (those who wern't set in the section cycle). It doesn't matter which
    // bone from the set they will be connected to, so we take the first one.
    for (int i = 0; i < vertexIsConverted.Size(); i++)
    {
      if (vertexIsConverted[i] == -2)
      {
        const AnimationRTWeight &vbr = vertexBoneRefTemp[i];
        AnimationRTWeight &vbrNew = _data->_vertexBoneRef[i];
        vbrNew.Resize(vbr.Size());
        for (int j = 0; j < vbr.Size(); j++)
        {
          const AnimationRTPair &pair = vbr[j];
          int b = GetSubSkeletonIndex(pair.GetSel()); // Don't be confused - we've got the SkeletonIndex here
          SkeletonIndex si = SetSkeletonIndex(b);

          // Get the set of subskeleton indices
          const SubSkeletonIndexSet &ssis = GetSubSkeletonIndexFromSkeletonIndex(si);

          // Set the SubSkeletonIndex
          DoAssert(ssis.Size() >= 1);
          vbrNew[j] = AnimationRTPair(ssis[0], pair.GetWeight8b());
        }
        
        // Make the vertex neighbors to contain the same values as the vertex itself
        // (they must be initialized somehow and this is the last chance)
        if (includeNeighbourFaceVertices)
        {
          _data->_neighborBoneRef[i]._posA = i;
          _data->_neighborBoneRef[i]._rtwA = vbrNew;
          _data->_neighborBoneRef[i]._posB = i;
          _data->_neighborBoneRef[i]._rtwB = vbrNew;
        }
        vertexIsConverted[i] = -1;
      }
    }
  }
}

/*!
\patch_internal 1.21 Date 8/16/2001 by Ondra.
- Fixed: Or/AndSpecial functions did not modify T&L sections.
- This made grass effects experiments impossible.
- This change should not affect any older code.
*/

void Shape::OrSpecial( int special )
{
  _special|=special;
  for (int i=0; i<_face._sections.Size(); i++)
  {
    _face._sections[i].OrSpecial(special);
  }
}
void Shape::AndSpecial( int special )
{
  _special&=special;
  for (int i=0; i<_face._sections.Size(); i++)
  {
    _face._sections[i].AndSpecial(special);
  }
}

void Shape::Triangulate()
{
  const FaceArray source = _face;
  _face.Clear(); 
  for( Offset i=source.Begin(),e=source.End(); i<e; source.Next(i) )
  {
    Poly src=source[i];
    int nv=src.N();
    if( nv>3 )
    {
      #ifdef __ICL
      #pragma novector
      #endif
      // this one needs to be triangulated
      for( int v=2; v<nv-1; v++ )
      {
        // use triangle 0,v,v+1
        Poly dst;
        dst.Set(0,src.GetVertex(0));
        dst.Set(1,src.GetVertex(v));
        dst.Set(2,src.GetVertex(v+1));
        //dst.CopyProperties(src);
        dst.SetN(3);
      }
      src.SetN(3);
    }
    _face.Add(src);
  }
  // what about sections?
  Fail("What about sections?");
}

Texture *Shape::FindTexture( const char *name ) const
{
  for (int t=0; t<_textures.Size(); t++)
  {
    Texture *tex = _textures[t];
    if (tex && !strcmp(tex->Name(),name)) return tex;
  }
  return NULL;
}

float Shape::GetTextureAOT(Texture *tex) const
{
  for (int i=0; i<_texArea._items.Size(); i++)
  {
    if (*_texArea._items[i]._tex==tex)
    {
      return _texArea._items[i]._aot;
    }
  }
  // when index seach was not sucessfull, we may try searching the sections
  
  return 0;
}

Texture *LODShape::FindTexture( const char *name ) const
{
  for (int level=0; level<NLevels(); level++)
  {
    Texture *tex = Level(level)->FindTexture(name);
    if (tex) return tex;
  }
  return NULL;
}

int GetFaceIndex(FaceArray &face, Offset offset)
{
  int itemIndex = 0;
  for (Offset i = face.Begin(), e = face.End(); i < e; face.Next(i))
  {
    if (i == offset) return itemIndex;
    itemIndex++;
  }
  return -1;
}

struct AOTStat
{
  float _aot;
  float _area;
  
  AOTStat(){}
  AOTStat(float aot, float area){_aot=aot,_area=area;}
  
  
  /**
  We could alternatively use float as a key and FindArrayKeyTraits, but this was we would have
  to go into global scope, as templates cannot be local.
  */
  bool operator == (const AOTStat &with) const {return _aot==with._aot;}
  ClassIsSimple(AOTStat);
  
};

template <>
struct FindArrayKeyTraits<AOTStat>
{
  typedef float KeyType;
  static bool IsEqual(KeyType a, KeyType b) {return a==b;}
  static KeyType GetKey(const AOTStat &a) {return a._aot;}
};

static inline int CompareAOTStat(const AOTStat *a, const AOTStat *b)
{
  // sort big first, so that it is easy to skip big
  return sign(b->_aot-a->_aot);
}

void Shape::RecalculateFaceArea()
{
  // scan all faces with same texture
  // if there are some faces, some sections must exist as well
  DoAssert(NFaces()==0 || NSections()>0);
  _faceArea = 0;
  for (int s=0; s<NSections(); s++)
  {
    ShapeSection &ss = GetSection(s);

    // Go through all the UV stages
    float totalArea = 0;
    for( Offset i=ss.beg; i<ss.end; NextFace(i) )
    {
      const Poly &face=Face(i);
      float area = face.GetArea(*this);
        
      totalArea += area;
    }

    if (!(ss.Special()&IsHiddenProxy))
    {
      _faceArea += totalArea;
      if (ss.Special()&NoBackfaceCull)
        _faceArea += totalArea; // no-cull face will be rendered twice
    }
  }
}


void Shape::RecalculateAreas()
{
  // scan all faces with same texture
  // if there are some faces, some sections must exist as well
  DoAssert(NFaces()==0 || NSections()>0);
  _faceArea = 0;
  for (int s=0; s<NSections(); s++)
  {
    ShapeSection &ss = GetSection(s);

    // Go through all the UV stages
    for (int uvStage = 0; uvStage < NUVStages(); uvStage++)
    {
      // if width is lower than height, u pixel distance is lower
      // we should therefore increase vDensity (size/v pixel distance)
      
      // for each section we build a histogram of participating faces
      FindArrayKey<AOTStat> histogram;
      float totalArea = 0;
      for( Offset i=ss.beg; i<ss.end; NextFace(i) )
      {
        const Poly &face=Face(i);
        if (face.N()<3)
        {
          ErrF("Degenerate face - %d vertices",face.N());
          continue;
        }

        float area = face.GetArea(*this);
        float uvArea = face.CalculateUVArea(*this, uvStage);
        
        float aot = uvArea>0 ? area/uvArea : 1;
        
        int index = histogram.FindKey(aot);
        if (index>=0)
          histogram[index]._area += area;
        else
          histogram.Add(AOTStat(aot,area));
        totalArea += area;
      }

      if (uvStage==0 && !(ss.Special()&IsHiddenProxy))
      {
        _faceArea += totalArea;
      }

      QSort(histogram,CompareAOTStat);
      // skip until you reach the percentile needed
      float skippedArea = 0;
      const float percentileWanted = 0.70f;
      int startFrom = 0;
      for (int i=0; i<histogram.Size(); i++)
      {
        skippedArea += histogram[i]._area;
        if (skippedArea>=(1-percentileWanted)*totalArea)
        {
          // we want to use this item, to make sure we have something left (it might be the first one)
          startFrom = i;
          break;
        }
      }
      
      // now grab the interesting point from the histogram
      float aot = histogram[startFrom]._aot;
      ss.SetAreaOverTex(uvStage,aot);
      if (histogram[0]._aot>aot*4)
      {
        // if we skipped a lot, print a warning
        // detect which texture is bound to given stage
        Texture *tex = NULL;
        // special case: stage 0 - use not only material, but also a primary texture
        if (uvStage==0) tex = ss.GetTexture();
        TexMaterial *mat = ss.GetMaterialExt();
        if (!tex && mat)
        {
          Assert(uvStage<2); // currently only 0 or 1 is supported
          UVSource src = uvStage>0 ? UVTex1 : UVTex;
          for (int i=0; i<mat->_stageCount+1; i++)
          {
            const TexStageInfo &stage = mat->_stage[i];
            const TexGenInfo &texGen = mat->_texGen[stage._texGen];
            if (texGen._uvSource==src)
            {
              // do not report procedural textures - we do not care about them
              if (stage.GetTextureForDraw() && stage.GetTextureForDraw()->GetName()[0]!='#')
              {
                tex = stage.GetTextureForDraw();
                break;
              }
            }
          }
        }
        
        if (tex)
        {
          // compute texel size from aot
          // we know texture dimensions
          float texArea = tex->AWidth() * tex->AHeight();
          float texelSizeUsed = sqrt(aot/texArea);
          float texelSizeMax = sqrt(histogram[0]._aot/texArea);
          float texelSizeMin = sqrt(histogram.Last()._aot/texArea);
          LogF("%s: UV mapping too varied on %s, stage %d: texel size used %g, max %g, min %g",
            GETREPORTSTACK(),cc_cast(tex->GetName()),uvStage,texelSizeUsed,texelSizeMax,texelSizeMin);
        }
      }
    }
  }
}

TexAreaIndex::TexAreaIndex()
{
  _valid = false;
}

/**
We want to track aot min value (saturateMax in Shape::RecalculateAreas does min)
*/
void TexAreaIndex::AddItem(const RefR<Texture> *tex, float aot, bool someDataWanted)
{
  Assert(_finite(aot));
  // check if the texture is already there
  for (int i=0; i<_items.Size(); i++)
  {
    if (*_items[i]._tex==*tex)
    {
      saturateMax(_items[i]._aot,aot);
      if (someDataWanted) _items[i]._someDataWanted = someDataWanted;
      return;
    }
  }
   
  Item &item = _items.Append();
  item._tex = tex;
  item._aot = aot;
  item._someDataWanted = someDataWanted;
}

void TexAreaIndex::Clear()
{
  _valid = false;
  _items.Clear();
}

void Shape::BuildTexAreaIndex(TexAreaIndex &tgt) const
{
  for (int i=0; i<NSections(); i++)
  {
    const ShapeSection &sec = GetSection(i);
    if (sec.GetTexture())
    {
      tgt.AddItem(sec.GetTextureBackPointer(),sec.GetAreaOverTex(0),false);
    }
    if (sec.GetMaterialExt())
    {
      const TexMaterial *mat = sec.GetMaterialExt();
      // caution: material needs to be loaded for building the index
      Assert(mat->IsReady());
      mat->Load();
      for (int i=0; i<mat->_stageCount+1; i++)
      {
        if (!mat->_stage[i]._tex) continue;
        const RefR<Texture> *tex = &mat->_stage[i]._tex;
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
          case UVTexWaterAnim:
          case UVTexShoreAnim:
          case UVTex:
            aot = sec.GetAreaOverTex(0)/mat->_texGen[tg].GetUVScale2D();
            break;
          case UVTex1:
            aot = sec.GetAreaOverTex(1)/mat->_texGen[tg].GetUVScale2D();
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
        tgt.AddItem(tex,aot,mat->_stage[i]._tex->Type()==TT_Diffuse && i!=0);
      }
      
    }
  }
  tgt._items.Compact();
  tgt._valid = true;
}

bool Shape::CheckTexAreaIndex() const
{
  if (!_texArea._valid) return false;
  TexAreaIndex temp;
  BuildTexAreaIndex(temp);
  // compare the result - it should not change
  if (!temp._valid) return false;
  if (temp._items.Size()!=_texArea._items.Size()) return false;
  for (int i=0; i<temp._items.Size(); i++)
  {
    if (temp._items[i]._tex != _texArea._items[i]._tex)
    {
      return false;
    }
    if (fabs(temp._items[i]._aot - _texArea._items[i]._aot)>1e-10)
    {
      return false;
    }
  }
  return true;
}


void Shape::InitPlanes()
{
  if( _plane.Size()>0 ) return;
  _plane.Realloc(_face.Size());
  _plane.Resize(_face.Size());
  RecalculateNormals(true);
}

bool Shape::VerifyNormals()
{
  int maxLog = 3;
  bool ret = true;
  if( _plane.Size()>0 )
  {
    int p=0;
    for( Offset i=BeginFaces(),e=EndFaces(); i<e; NextFace(i),p++ )
    {
      const Poly &face=Face(i);
      Plane &plane=GetPlane(p);
      Plane oldPlane = plane;
      face.CalculateNormal(plane, GetPosArray());
      float normDist = plane.Normal().Distance(oldPlane.Normal());
      float dDist = plane.D()-oldPlane.D();
      if(fabs(dDist)>1e-2 || fabs(normDist)>1e-2)
      {
        ret = false;
        if (maxLog>0)
        {
          LogF(
            "N,D (%.3f,%.3f,%.3f),%.3f (%.3f,%.3f,%.3f),%.3f",
            plane.Normal()[0],plane.Normal()[1],plane.Normal()[2],plane.D(),
            oldPlane.Normal()[0],oldPlane.Normal()[1],oldPlane.Normal()[2],oldPlane.D()
          );
          maxLog--;
        }
      }
    }
    Assert( p==_plane.Size() );
  }
  return ret;
}

void Shape::RecalculateNormals( bool full )
{
  // when full == false, recalculate when marked as valid (if not marked, will be recalculated later)
  if( !full && !_faceNormalsValid ) return;
  if( _plane.Size()>0 )
  {
    int p=0;
    for( Offset i=BeginFaces(),e=EndFaces(); i<e; NextFace(i),p++ )
    {
      const Poly &face=Face(i);
      Plane &plane=GetPlane(p);
      if( full )
      {
        // ignore singular faces - we cannot compute them
        if (face.N()<3)
        {
          plane = Plane(VZero,0);
        }
        else
        {
          face.CalculateNormal(plane, GetPosArray());
        }
      }
      else face.CalculateD(plane, GetPosArray());
    }
    Assert( p==_plane.Size() );
  }
  _faceNormalsValid=true;
}

void Shape::RecalculateNormals( const ConvexComponents& cc, bool full )
{
  // when full == false, recalculate when marked as valid (if not marked, will be recalculated later)
  if( !full && !_faceNormalsValid ) return;
  if( _plane.Size()>0 )
  {
    int p=0;
    for( Offset i=BeginFaces(),e=EndFaces(); i<e; NextFace(i),p++ )
    {
      const Poly &face=Face(i);
      Plane &plane=GetPlane(p);
      if( full )
      {
        face.CalculateNormal(plane, GetPosArray());
      }
      else face.CalculateD(plane, GetPosArray());
    }
    Assert( p==_plane.Size() );
  }
  _faceNormalsValid=true;
}

void Shape::CalculateMinMaxFromCC(const ConvexComponents& cc)
{
  if( NPos()<=0 )
  {
    _minMax[0]=VZero;
    _minMax[1]=VZero;
    _bCenter=VZero;
    _bRadius=0;
  }
  else
  {
    //ScanMinMax(_minMax);
    bool init = false;    
    for( int i=0; i<cc.Size(); i++ )
    {
      const ConvexComponent& ccc = *(cc[i]);
      for(int j = 0; j < ccc.Size(); j++)
      {
        Vector3Val pos=Pos(ccc[j]);
        if (init)
        {
          CheckMinMaxInline(_minMax[0],_minMax[1],pos);
        }
        else
        {
          init = true;
          _minMax[0]=pos;
          _minMax[1]=pos;
        }        
      }
    }

    if (!init)
    {
      _minMax[0]=VZero;
      _minMax[1]=VZero;
    }
    //ScanBSphere(_bCenter,_bRadius);
    
    _bCenter=(_minMax[0]+_minMax[1])*0.5;
    float maxDist2=0;       
    for( int i=0; i<cc.Size(); i++ )
    {
      const ConvexComponent& ccc = *(cc[i]);
      for(int j = 0; j < ccc.Size(); j++)
      {
        Vector3Val pos=Pos(ccc[j]);
        saturateMax(maxDist2,pos.Distance2Inline(_bCenter));
      }
    }
    _bRadius=sqrt(maxDist2);
  }
}

void Shape::InitAnimationContext(AnimationContext &animContext, const ConvexComponents *cc, bool animateGeometry) const
{
  // basic initialization
  animContext.Init(this, cc, animateGeometry);
  // temporary - copying of arrays not supporting late initialization yet
  animContext.LoadCC();
}

void Shape::Compact()
{
  base::Compact();
  _face.Compact();
  _sel.Compact();
  _prop.Compact();
  _phase.Compact();
  _textures.Compact();
}

void Shape::AddPhase( const AnimationPhase &phase )
{
  int i=0;
  for( ; i<_phase.Size(); i++ )
  {
    if( _phase[i].Time()>phase.Time() ) break;
  }
  _phase.Insert(i,phase);
}

int Shape::PrevAnimationPhase( float time ) const
{
  int i,n=_phase.Size();
  int prev=n-1,next=0;
  float prevTime=-1e10,nextTime=+1e10;
  for( i=0; i<n; i++ )
  {
    float pTime=_phase[i].Time();
    if( pTime<=time && pTime>prevTime ) prevTime=pTime,prev=i;
    if( pTime>time && pTime<nextTime ) nextTime=pTime,next=i;
  }
  return prev;
}

int Shape::NextAnimationPhase( float time ) const
{
  int i,n=_phase.Size();
  int prev=n-1,next=0;
  float prevTime=-1e10,nextTime=+1e10;
  for( i=0; i<n; i++ )
  {
    float pTime=_phase[i].Time();
    if( pTime<=time && pTime>prevTime ) prevTime=pTime,prev=i;
    if( pTime>time && pTime<nextTime ) nextTime=pTime,next=i;
  }
  return next;
}

void Shape::SetPhaseIndex( int index )
{
  const AnimationPhase &pos=_phase[index];
  for( int i=0; i<NPos(); i++ )
  {
    SetPos(i)=pos[i];
  }
}

void Shape::PreparePhase
(
  const AnimationPhase *&prevPos, const AnimationPhase *&nextPos, float &interpol,
  float time, float baseTime
) const
{
  bool looped = baseTime>=0;
  // interpolate between two nearest phases
  prevPos=nextPos=&_phase[0]; // default to first phase
  int n=_phase.Size();
  if( n<2 ) return; // no animation
  int i = 0;
  int prev = 0, next = n-1;
  //float prevTime=_phase[0].Time(),nextTime=_phase[next].Time();
  float prevTime = -1e10, nextTime = +1e10;
  if (looped)
  {
    // ignore out of range negative phases
    for( i=0; i<n; i++ )
    {
      float pTime=_phase[i].Time();
      if( pTime>baseTime-0.1 ) break;
    }
    if( i>=n ) return; // no positive phase
    prev = n-1,next = i;
    prevTime = -1e10, nextTime = +1e10;
  }
  for( ; i<n; i++ )
  {
    float pTime=_phase[i].Time();
    if( looped && pTime>=baseTime+1 ) break;
    if( pTime<=time && pTime>prevTime ) prevTime=pTime,prev=i;
    if( pTime>time && pTime<nextTime ) nextTime=pTime,next=i;
  }
  prevPos=&_phase[prev];
  nextPos=&_phase[next];
  nextTime=nextPos->Time();
  prevTime=prevPos->Time();
  // cycled through 0 - we use mod 1 arithmetics
  if (prev==next )
  {
    interpol = 1;
  }
  else
  {
    float nextDelta=nextTime-prevTime;if( looped && nextDelta<=0 ) nextDelta+=1;
    float timeDelta=time-prevTime;if( looped && timeDelta<0 ) timeDelta+=1;
    interpol=timeDelta/nextDelta;
  }
}


void Shape::SetPhase(AnimationContext &animContext, float time, float baseTime) const
{
  if( IsAnimated() )
  {
    const AnimationPhase *prevPos,*nextPos;
    float interpol;
    PreparePhase(prevPos,nextPos,interpol,time,baseTime);
    for( int i=0; i<NPos(); i++ )
    {
      animContext.SetPos(this, i)=( (*prevPos)[i]+((*nextPos)[i]-(*prevPos)[i])*interpol );
    }
    animContext.InvalidateNormals(this);
  }
}

void Shape::SetPhase(AnimationContext &animContext, const Selection &anim, float time, float baseTime) const
{
  if( IsAnimated() )
  {
    const AnimationPhase *prevPos,*nextPos;
    float interpol;
    PreparePhase(prevPos,nextPos,interpol,time,baseTime);
    for( int ai=0; ai<anim.Size(); ai++ )
    {
      int i=anim[ai];
      animContext.SetPos(this, i)=( (*prevPos)[i]+((*nextPos)[i]-(*prevPos)[i])*interpol );
    }
    animContext.InvalidateNormals(this);
  }
}

Vector3 Shape::PointPhase( int i, float time, float baseTime ) const
{
  if( IsAnimated() )
  {
    const AnimationPhase *prevPos,*nextPos;
    float interpol;
    PreparePhase(prevPos,nextPos,interpol,time,baseTime);
    return (*prevPos)[i]+((*nextPos)[i]-(*prevPos)[i])*interpol;
  }
  else
  {
    return Pos(i);
  }
}

void LODShape::CalculateHints()
{
  _andHints=~0;
  _orHints=0;
  for( int level=0; level<NLevels(); level++ )
  {
    ShapeUsed shape=Level(level);
    if( shape.IsNull() ) continue;
    _andHints&=shape->GetAndHints();
    _orHints|=shape->GetOrHints();
  }
  ShapeUsed aShape=Level(0);
  _color=aShape->_color,_colorTop=aShape->_colorTop;
  float alpha = _color.A8()*(1.0/255);
  //float transparency = 1-alpha*2;
  float transparency = 1-alpha*1.5;
  
  if (transparency>=0.99) _viewDensity = 0;
  if (transparency>0.01) _viewDensity = log(transparency)*6;
  else _viewDensity = -10;

  /*
  // empirical adjustment:
  // forest density is significantly lesser than other objects
  float vis10m = exp(_viewDensity*10);
  LogF
  (
    "Shape %s, transp %.3f, density %.5f, vis10m %.5f",
    (const char *)Name(),transparency,_viewDensity,vis10m
  );
  */


}

void LODShape::LoadAnimations(ParamEntryPar anim)
{
  _animations = new AnimationHolder;
  
  _animations->Load(this,anim);
  // Note: this is done in binarization only, when all levels are always loaded
  // instead of adding a hander we could call `AnimationHolder::InitLevel` for all levels directly here, and remove the LODShapeLoadHandler interface from AnimationHolder
  AddLoadHandler(_animations);

}

void LODShape::OptimizeAnimations()
{
  // if there are any animations not linked to any bone, delete them
  for (int i=0; i<_animations->Size(); )
  {
    AnimationType *anim = (*_animations)[i];
    bool used = false;
    for (int level=0; level<NLevels(); level++)
    {
      SkeletonIndex bone = anim->GetBone(level);
      if (!SkeletonIndexValid(bone)) continue;
      used = true;
      break;
    }
    if (!used)
    {
      for (int level=0; level<NLevels(); level++)
      {
        anim->DeinitLevel(this,level);
      }
      _animations->Remove(i);
    }
    else
    {
      i++;
    }
  }
}

Skeleton *LODShape::LoadSkeletonFromSource()
{
  if (_skeleton == NULL)
  {
    if (GetSkeletonSourceName().GetLength()>0)
    {
      Ref<Skeleton> skeleton = Skeletons.New(GetSkeletonSourceName());
      SkeletonAssigned(skeleton);
    }
  }
  else
  {
    SkeletonAssigned(_skeleton);
  }
  return _skeleton;
}

SkeletonIndex LODShape::FindBone(const RStringB &name) const
{
  if (!_skeleton) return SkeletonIndexNone;
  return _skeleton->FindBone(name);
}

void LODShape::InitDrawMatrices(Matrix4Array &matrices, int level) const
{
  // Get the shape and make sure it already exists
  DoAssert(IsLevelLocked(level));
  const Shape *shape = GetLevelLocked(level);

  // Set the matrices size and fill it out with identities
  matrices.Resize(shape->GetSubSkeletonSize());
  for (int i = 0; i < matrices.Size(); i++)
  {
    matrices[i] = MIdentity; // init all with identity or zero?
  }
}

void LODShape::SkeletonAssigned(Skeleton *skeleton)
{
  if (_skeletonAssignCount++==0)
  {
    // the shape needs to have a skeleton already set by binarize
    // TODO: remove _skeletonAssignCount, SkeletonAssigned, SkeletonUnassigned
    DoAssert(!_skeleton || _skeleton==skeleton);
    _skeleton = skeleton;
  }
  else
  {
    DoAssert(_skeleton==skeleton);
  }
}
void LODShape::SkeletonUnassigned(Skeleton *skeleton)
{
  Assert(skeleton==_skeleton);
  if (--_skeletonAssignCount==0)
  {
    _skeleton = NULL;
  }
}

/*!
\patch_internal 1.30 Date 11/2/2001 by Ondra.
- Fixed: When shape was enlarged and bounding center was not change,
bounding radius was not updated. This caused blue fog in distance.
\patch 1.30 Date 11/2/2001 by Ondra.
- Fixed: Blue/dark gray horizon with large viewing distance.
*/

void LODShape::CalculateBoundingSphereRadius()
{
  float maxSphere2=0;
  for( int level=0; level<_nLods; level++ )
  {
    const Shape *shape=GetLevelLocked(level);
    if( !shape ) continue;
    if (!shape->IsGeometryLoaded()) continue;
    for( int i=0; i<shape->NPos(); i++ )
    {
      Vector3Val pos=shape->Pos(i);
      float sphere2=pos.SquareSize();
      saturateMax(maxSphere2,sphere2);
    }
    for( int p=0; p<shape->_phase.Size(); p++ )
    {
      for( int i=0; i<shape->_phase[p].Size(); i++ )
      {
        const Vector3 &pos=shape->_phase[p][i];
        float sphere2=pos.SquareSize();
        saturateMax(maxSphere2,sphere2);
      }
    }
  }
  _boundingSphere=sqrt(maxSphere2);
}

void LODShape::ApplyBoundingCenter(int level, Vector3Par changeBoundingCenter)
{
  Shape *shape = InitLevelLocked(level);
  if (!shape) return;
  // change min-max offsets of all lods
  shape->_minMax[0] -= changeBoundingCenter;
  shape->_minMax[1] -= changeBoundingCenter;
  shape->_bCenter -= changeBoundingCenter;
  // change proxy positions
  for( int i=0; i<shape->_proxy.Size(); i++ )
  {
    ProxyObject *proxy=shape->_proxy[i];
    //Object *pobj = proxy->obj;
    proxy->trans.SetPosition(proxy->trans.Position()-changeBoundingCenter);
    // recalc. inverse transform
    proxy->invTransform=proxy->trans.InverseScaled();
  }
}

void LODShape::ApplyBoundingCenterGeom(int level, Vector3Par changeBoundingCenter)
{
  Shape *shape = InitLevelLocked(level);
  if (!shape || !shape->IsGeometryLoaded()) return;
  for( int i=0; i<shape->NPos(); i++ )
  {
    Vector3 &pos=shape->SetPos(i);
    pos-=changeBoundingCenter;
  }
  for( int p=0; p<shape->_phase.Size(); p++ )
  {
    for( int i=0; i<shape->_phase[p].Size(); i++ )
    {
      Vector3 &pos=shape->_phase[p][i];
      pos-=changeBoundingCenter;
    }
  }
}

/**
@param radius radius calculation may be skipped when we know only center was changed
*/
void LODShape::CalculateBoundingSphere(bool recalcRadius)
{
  //Assert(!_streaming);
  // -_boundingCenter center is original zero positioned in new coordinate system
  // when you add _boundingCenter to object coordinates, you will get original position

  // calculate bounding sphere from min-max information
  Vector3 oldBoundingCenter=_boundingCenter;
  Vector3 changeBoundingCenter=VZero;
  if (!_lockAutoCenter)
  {
    if (_autoCenter && NLevels()>0)
    {
      // consider only clipflags of 
      changeBoundingCenter=(_minMax[0]+_minMax[1])*0.5;
      // for OnSurface shapes do not change y component
      const Shape *level0 = GetLevelLocked(0);
      if (level0 && (level0->GetAndHints()&ClipLandOn) && (level0->Special()&OnSurface))
      {
        changeBoundingCenter[1] = 0;
      }
    }
    else
    {
      // we might need to reset bounding center to zero
      changeBoundingCenter = -_boundingCenter;
    }

    if (changeBoundingCenter.SquareSize()<1e-10 && _boundingSphere>0 )
    {
      // note: bounding sphere may be changed even when center is not
      if (recalcRadius)
      {
        CalculateBoundingSphereRadius();
      }
      else
      {
        // when bcenter is changing, make sure radius is big enough
        // to cover the whole the original sphere
        _boundingSphere = changeBoundingCenter.Size()+_boundingSphere;
      }
      return; // no change
    }
  }
  _boundingCenter += changeBoundingCenter;
  _minMax[0] -= changeBoundingCenter;
  _minMax[1] -= changeBoundingCenter;
  _minMaxVisual[0] -= changeBoundingCenter;
  _minMaxVisual[1] -= changeBoundingCenter;
  _aimingCenter -= changeBoundingCenter;
  
  for( int level=0; level<_nLods; level++ )
  {
    const Shape *shape=GetLevelLocked(level);
    if( !shape ) continue;

//#if _BINARIZE_REDUCELODSHAPE
    extern bool GShrinkP3D;
    if (GShrinkP3D)
    {
      // If some of the previous LODs uses the same shape, skip this one, otherwise it will be recalculated twice
      bool alreadyProcessed = false;
      for (int j = 0; j < level; j++)
      {
        if (GetLevelLocked(j) == shape)
        {
          alreadyProcessed = true;
          break;
        }
      }
      if (alreadyProcessed) continue;
    }
//#endif

    // in case we are loaded, pretend we are locked
    shape->AddRefLoad();
    ApplyBoundingCenterGeom(level,changeBoundingCenter);
    ApplyBoundingCenter(level,changeBoundingCenter);

    shape->ReleaseLoad();
  }
  if (recalcRadius)
  {
    CalculateBoundingSphereRadius();
  }
  else
  {
    // when bcenter is changing, make sure radius is big enough
    // to cover the whole the original sphere
    _boundingSphere = changeBoundingCenter.Size()+_boundingSphere;
  }

  // invalidates Convex components 
  if (changeBoundingCenter != VZero)
  {  
    if (_geomComponents.NotNull())
      _geomComponents->Invalidate();

    if (_viewComponents.NotNull())
      _viewComponents->Invalidate();

    if (_fireComponents.NotNull())
      _fireComponents->Invalidate();

    #if VIEW_INTERNAL_GEOMETRIES
    if (_viewPilotComponents.NotNull())
      _viewPilotComponents->Invalidate();

    if (_viewGunnerComponents.NotNull())
      _viewGunnerComponents->Invalidate();
    #endif

    if (_treeCrown)
    {
      _treeCrown->_minmax[0] -= changeBoundingCenter;
      _treeCrown->_minmax[1] -= changeBoundingCenter;
    }
  }
 
  // d coefs of planes are changed
  if( changeBoundingCenter.SquareSize()>0.01 )
  {
    for( int level=0; level<_nLods; level++ )
    {
      Shape *shape = InitLevelLocked(level);
      if (shape && shape->IsGeometryLoaded())
      {
        // we know we are loaded, pretend we are locked
        shape->AddRefLoad();
        shape->RecalculateNormals(false);
        shape->ReleaseLoad();
      }
    }
  }
#if USER_Bebul
  RptF("BoundingCenter of %s: %.2f,%.2f", cc_cast(_name), _boundingCenter.X(),_boundingCenter.Z());
#endif
}

void LODShape::CalculateMinMax( bool recalcLevels )
{
  //Assert( _boundingCenter.SquareSize()<=0.0001 );
  // calculate only for first LOD-level
  // we assume this will be valid for the other too
    // scan through all vertices of all levels
  if( recalcLevels )
  {
    for( int level=0; level<NLevels(); level++ )
    {
      Shape *shape = InitLevelLocked(level);
      if( !shape ) continue;
      shape->CalculateMinMax();
    }
  }
  
  // calculate min-max of all levels
  _minMax[0]=Vector3(1e10,1e10,1e10); // min
  _minMax[1]=Vector3(-1e10,-1e10,-1e10); // max

  for( int level=0; level<NLevels(); level++ )
  {
    const Shape *shape=GetLevelLocked(level);
    if( !shape ) continue;
    SaturateMin(_minMax[0],shape->Min());
    SaturateMax(_minMax[1],shape->Max());
  }

  CalculateMinMaxVisual();

  CalculateBoundingSphere();
}

void LODShape::CalculateMinMaxVisual()
{
  // when all lods are graphical, there is no need to compute anything
  // this is done mostly to make it easier to skip helper objects during debugging
  if (_nGraphical==_nLods)
  {
    _minMaxVisual[0] = _minMax[0];
    _minMaxVisual[1] = _minMax[1];
    return;
  }
  _minMaxVisual[0]=Vector3(+1e10,+1e10,+1e10); // min
  _minMaxVisual[1]=Vector3(-1e10,-1e10,-1e10); // max

  bool none = true;
  for( int level=0; level<_nGraphical; level++ )
  {
    const Shape *shape=GetLevelLocked(level);
    if( !shape ) continue;
    SaturateMin(_minMaxVisual[0],shape->Min());
    SaturateMax(_minMaxVisual[1],shape->Max());
    none = false;
  }

  if (none)
  {
    // for case of no loaded LODs at all use total minmax
    _minMaxVisual[0] = _minMax[0];
    _minMaxVisual[1] = _minMax[1];
  }
}

void LODShape::CheckForcedProperties(ParamEntryPar modelConfig)
{
  if (GetName().GetLength() <= 0) return;
  ConstParamEntryPtr props = modelConfig.FindEntry("properties");
  if (props && NLevels()>0)
  {
    // scan properties and add them into geometry or topmost level
    DoAssert(!_streaming);

    // initialization - called from LODShape::Load
    Shape *geom = InitGeometryLevel();
    if (!geom) geom = InitLevelLocked(0);
    for (int i=0; i<props->GetSize()-1; i+=2)
    {
      RStringB propName = (*props)[i];
      RStringB propVal = (*props)[i+1];
      geom->SetProperty(propName,propVal);
    }
    // some force properties may change meaning of some shapes
    ScanShapes();
  }
}

void LODShape::CopyProperties(const LODShape &src)
{
  _armor = src._armor;
  _invArmor = src._invArmor;
  _logArmor = src._logArmor;
}

void LODShape::ScanProperties()
{
  const char *armor=PropertyValue("armor");
  if( armor && *armor ) _armor=atof(armor);
  else _armor=200;
  if (_armor>1e-10)
  {
    _invArmor=1/_armor;
    _logArmor = log(_armor);
  }
  else
  {
    _invArmor = 1e10;
    _logArmor = 25;
  }
}

void LODShape::RescanLodsArea()
{
  float lodsAreaFactor = 1.0f;
  float minAreaBefore = FLT_MAX;
  int minAreaI = 0;
  int bestA = -1,bestB = -1;
  for (int i=0; i<_nGraphical; i++)
  {
    if (!_lods[i].GetShapeRef()->NVerticesAndAreaKnown()) continue;
    float area = _lods[i].GetShapeRef()->FaceArea();
    if (minAreaBefore>area)
      minAreaBefore = area,minAreaI = i;
    else
    {
      float factor = area>0 ? minAreaBefore/area : 1.0f;
      if (lodsAreaFactor>factor)
        lodsAreaFactor = factor, bestA = minAreaI, bestB = i;;
    }
  }
  _lodsAreaFactor = lodsAreaFactor;
  #if _PROFILE || _DEBUG
    static float reportThold = 0.95f;
    if (lodsAreaFactor<reportThold)
    {
      LogF("LODs area %s: %.3f (%d:%d)",cc_cast(GetName()),lodsAreaFactor,bestA,bestB);
    }
  #endif
}


/*!
\patch 1.28 Date 10/22/2001 by Ondra.
- Fixed: Bug in angular inertia tensor calculation.
  This bug caused fuel truck turning very slow.
*/

void LODShape::CalculateMass()
{
  // mass should always be stored in geometry
  //Shape *shape=Level(0);
  const Shape *shape=GeometryLevel();
  if( !shape ) return;
  if (shape->NPoints()!=_massArray.Size())
  {
    RptF("CalculateMass %s",(const char *)GetName());
    Fail("Cannot recalculate mass");
    return;
  }
  float totalMass=0;
  // calculate position of center of mass
  Vector3 sum(VZero);
  for( int i=0; i<shape->NPoints(); i++ )
  {
    Vector3Val r = shape->Pos(shape->PointToVertex(i));
    float m = _massArray[i];
    totalMass += m;
    sum += r*m;
  }
  if( totalMass>0 ) _centerOfMass = sum*(1/totalMass);
  else _centerOfMass = VZero;

  Matrix3 totalInertia(M3Zero);
  for( int i=0; i<shape->NPoints(); i++ )
  {
    Vector3Val r = shape->Pos(shape->PointToVertex(i))-_centerOfMass;
    float m = _massArray[i];
    Matrix3Val rTilda = r.Tilda();
    totalInertia -= rTilda*rTilda*m;
  }
  _mass=totalMass;
  if (totalMass>0)
  {
    _inertia = totalInertia;
    _invInertia=totalInertia.InverseGeneral();
    _invMass=1/totalMass;
  }
  else
  {
    _inertia=M3Identity;
    _invInertia=M3Identity;
    _invMass=1;
  }
  // mass array will never be needed again - release it
  // TODO: solve it better for example calculate and store mass for each part etc... 
  _massArray.Clear(); 
}

// LOD implementation

void LODShape::DoClear()
{ // forget everything but name
  _canOcclude = false; // do not use for occlusions unless told otherwise
  _canBeOccluded = false;
  //_canUsePushBuffer = true;
  _autoCenter = true;
  _forceNotAlphaModel = false;
  _sbSource = SBS_Visual;
  _preferShadowVolume = true;
  _animationType = AnimTypeNone;
  _skeletonSource.Free();
  _canBlend = false;
  _lockAutoCenter = false;
  _lodsAreaFactor = 1.0f;
  _special=0;
  _andHints=_orHints=0; // default: no hints
  //strcpy(_name,":temp:");
  _nLods=0;
  #if _ENABLE_REPORT
    _selectionsOpen = false; // selections by default not open (open only when loading)
  #endif
  _remarks=0;
  _boundingCenter=VZero;
  _boundingCenterOnLoad = VZero;
  _geometryCenter=VZero;
  _boundingSphere = 0;
  _geometrySphere = 0;
  _minMax[0]=VZero;
  _minMax[1]=VZero;
  _minMaxVisual[0] = VZero;
  _minMaxVisual[1] = VZero;
  _geometry=_memory=_landContact=_roadway=_hitpoints=_paths=-1;
  _geometryFire=_geometryView=-1;
  _geometryViewPilot=-1;
  _geometryViewGunner=-1;
  _geometryViewCargo=-1;

  _wreck = -1;

  _shadowVolume = -1;
  _shadowBuffer = -1;
  _shadowBufferCount = 0;
  _shadowVolumeCount = 0;
  _projShadow = false;

  _propertyFrequent = false;

  _invInertia.SetIdentity();
  _inertia.SetIdentity();
  _centerOfMass=VZero;
  _mass=0;
  _invMass=1e10;
  _aimingCenter=VZero;

  _htMin = 0.0f;//600.0f;
  _htMax = 0.0f;//3600.0f;
  _afMax = 0.0f;
  _mfMax = 0.0f;
  _mFact = 0.0f;
  _tBody = 0.0f;

  _geomComponents=new ConvexComponents();
  _viewComponents=new ConvexComponents();
  _fireComponents=new ConvexComponents();
  _massArray.Clear();
  _viewDensity=-100;
  _color=PackedBlack;
  _colorTop=PackedBlack;
  _optimizeWanted = VBNotOptimized;
  _streaming = false;
  _minShadow = 0;
  _nGraphical = 0;
  _mapType = MapHide;
}

void LODShape::DoConstruct()
{
  _name="";
  _mapType = MapHide;
  DoClear();
}

void LODShape::DoConstruct(
  const LODShape &src, bool copyAnimations, bool copyLods, bool copySelections, bool copyST
)
{
  // copy shapes, not only references
  if (copyLods)
  {
    for( int i=0; i<src._nLods; i++ )
    {
      if( src._lods[i] )
      {
        ShapeUsedGeometryLock<> srcShape(&src,i);
        //src._lods[i].Lock();
        Shape *shape = new Shape(*srcShape.GetShape(),copyAnimations,copySelections,copyST);
        Assert(shape->CheckIntegrity());
        _lods[i].SetPermanentRef(shape);
        //src._lods[i].Unlock();
      }
      else
      {
        _lods[i]=NULL;
      }
      _resolutions[i]=src._resolutions[i];
    }
  }
  _nLods=src._nLods;
  #if _ENABLE_REPORT
    _selectionsOpen = false; // selections by default not open (open only when loading)
  #endif
  _autoCenter = src._autoCenter;
  _lockAutoCenter = src._lockAutoCenter;
  _forceNotAlphaModel = src._forceNotAlphaModel;
  _sbSource = src._sbSource;
  _preferShadowVolume = src._preferShadowVolume;
  _animationType = src._animationType;
  _skeletonSource = src._skeletonSource;
  _canBlend = src._canBlend;
  _minMax[0] = src._minMax[0],_minMax[1]=src._minMax[1];
  _minMaxVisual[0] = src._minMaxVisual[0],_minMaxVisual[1]=src._minMaxVisual[1];
  _boundingCenter = src._boundingCenter;
  _boundingSphere = src._boundingSphere;
  _special=src._special;
  _remarks=src._remarks;
  _andHints=src._andHints;
  _orHints=src._orHints;
  _geometry=src._geometry;
  _geometryFire=src._geometryFire;
  _geometryView=src._geometryView;

  _geometryViewPilot = src._geometryViewPilot;
  _geometryViewGunner = src._geometryViewGunner;
  _geometryViewCargo = src._geometryViewCargo;

  _memory=src._memory;
  _landContact=src._landContact;
  _roadway=src._roadway;
  _paths=src._paths;
  _hitpoints=src._hitpoints;
  _wreck = src._wreck;
  _shadowVolume = src._shadowVolume;
  _shadowBuffer = src._shadowBuffer;
  _shadowBufferCount = src._shadowBufferCount;
  _shadowVolumeCount = src._shadowVolumeCount;
  _projShadow = src._projShadow;
  //strcpy(_name,src._name);
#if 0//_VBS3_CRATERS_DEFORM_TERRAIN // Don't create own name, otherwise the shape would be stored in the shape cache instead of destruction
  _name = RString("Copy:") + src._name;
#else
  _name=""; // copy name is empty
#endif
  _invInertia = src._invInertia;
  _inertia = src._inertia;
  _mass=src._mass;
  _invMass=src._invMass;
  _centerOfMass=src._centerOfMass;
  _aimingCenter=src._aimingCenter;

#if _VBS3_CRATERS_DEFORM_TERRAIN
  _viewDensity=src._viewDensity;
#endif

  _htMin = src._htMin;
  _htMax = src._htMax;
  _afMax = src._afMax;
  _mfMax = src._mfMax;
  _mFact = src._mFact;
  _tBody = src._tBody;

  if( copyAnimations )
  {
    _geomComponents=new ConvexComponents(*src._geomComponents);
    _viewComponents=new ConvexComponents(*src._viewComponents);
    _fireComponents=new ConvexComponents(*src._fireComponents);
    if( src._massArray.Size()>0 && GeometryLevel() )
    {
      _massArray=src._massArray;
    }
    else _massArray.Clear();
  }
  else
  {
    _geomComponents=new ConvexComponents();
    _viewComponents=new ConvexComponents();
    _fireComponents=new ConvexComponents();
    _massArray.Clear();
  }
  _optimizeWanted = VBNotOptimized;
  _propertyFrequent = src._propertyFrequent;
  _streaming = false;
  _minShadow = src._minShadow;
  _nGraphical = src._nGraphical;
  _mapType = src._mapType;
}

void LODShape::DoDestruct()
{
  #if 0 //VERBOSE
    if( _name[0] )
    {
      LogF("## Destruct shape %s",(const char *)_name);
    }
  #endif
  #if 0 // VERBOSE
    if( _name[0] )
    {
      FindArrayRStringCI used;
      FindArrayRStringCI unused;
      // first gather all selections not used in some level
      for (int i=0; i<NLevels(); i++)
      {
        if (!LevelRef(i).IsLoaded()) continue;
        Shape *shape = LevelRef(i).GetRef();
        for (int s=0; s<shape->NNamedSel(); s++)
        {
          const NamedSelection &sel = shape->NamedSel(s);
          if (!sel.WasUsed())
          {
            unused.AddUnique(sel.GetName());
          }
        }
      }
      for (int i=0; i<NLevels(); i++)
      {
        if (!LevelRef(i).IsLoaded()) continue;
        Shape *shape = LevelRef(i).GetRef();
        for (int s=0; s<shape->NNamedSel(); s++)
        {
          const NamedSelection &sel = shape->NamedSel(s);
          if (sel.WasUsed())
          {
            used.AddUnique(sel.GetName());
            unused.DeleteKey(sel.GetName());
          }
        }
      }
      if (unused.Size()>0)
      {
        char shortName[256];
        GetFilename(shortName,GetName());
        while (strpbrk(shortName," -/()"))
        {
          *strpbrk(shortName," -/()")='_';
        }
        // report what was used / unused
        RptF("  class %s",shortName);
        RptF("    removeSelections[]={");
        for (int i=0; i<unused.Size(); i++)
        {
          RptF("    \"%s\",",cc_cast(unused[i]));
        }
        RptF("    };");
      }
    }
  #endif
  // notify anyone interested this shape is unloaded
  // while notifying pretend we are still locked
  TempAddRef();
  for (int i=0; i<_loadHandler.Size(); i++)
  {
    LODShapeLoadHandler *handler = _loadHandler[i];
    for (int i=0; i<NLevels(); i++)
    {
      if (!LevelRef(i).IsLoaded()) continue;
      handler->ShapeUnloaded(this, i);
    }
    handler->LODShapeUnloaded(this);
    // report handler is going to be unregistered because of destruction
    handler->HandlerUnregistered();
  }
  // loadhandlers are executed - clear them
  _loadHandler.Clear();
  TempRelease();
  if (_streaming)
  {
    for( int i=0; i<_nLods; i++ )
    {
      ShapeRefManaged *managed = _lods[i].GetShapeRef()->GetManagedInterface();
      Shape *shape = _lods[i].GetShapeRef()->GetRef();
      if (shape)
      {
        Shapes.ShapeGeometryReleased(shape);
      }
      if (managed)
      {
        Shapes.ShapeReleased(managed);
      }
      _lods[i].Free();
    }
  }
  else
  {
    for( int i=0; i<_nLods; i++ )
    {
      _lods[i].Free();
    }
  }

  _nLods=0;
}

/**
Adjust selection indices when some selection is removed.
selection indices are stored in animations.
*/
void LODShape::OnSelectionRemoved(int level, int sel)
{
  if (_animations)
  {
    ErrF(
      "%s: Cannot remove selections when animations exists",
      cc_cast(GetName())
    );
  }
}

void LODShape::RemoveSelections(const FindArrayRStringBCI &names)
{
  size_t saved = 0;
  for (int level=0; level<NLevels(); level++)
  {
    Shape *shape = InitLevelLocked(level);
    for (int s=0; s<shape->_sel.Size();)
    {
      if (names.FindKey(shape->_sel[s].GetName())>=0)
      {
        OnSelectionRemoved(level,s);
        
        saved += shape->_sel[s].GetMemoryControlled()+sizeof(shape->_sel[s]);
        
        shape->_sel.Delete(s);
      }
      else
      {
        s++;
      }
    }
  }
  if (saved>0)
  {
    LogF(
      "%s: %s saved in selections",
      cc_cast(GetName()),cc_cast(FormatByteSize(saved))
    );
  }
}

void LODShape::RemoveVertexSelections(const FindArrayRStringBCI &names)
{
  size_t saved = 0;
  for (int level=0; level<NLevels(); level++)
  {
    Shape *shape = InitLevelLocked(level);
    for (int s=0; s<shape->_sel.Size(); s++)
    {
      NamedSelection &sel = shape->_sel[s];
      if (names.FindKey(sel.GetName())>=0)
      {
        saved += sel.GetMemoryControlled();
        
        sel.RemoveAllButSections();

        saved -= sel.GetMemoryControlled();
      }
    }
  }
  if (saved>0)
  {
    LogF(
      "%s: %s saved in selections",
      cc_cast(GetName()),cc_cast(FormatByteSize(saved))
    );
  }
}

void LODShape::RemoveVertexSelectionsComplement(const FindArrayRStringBCI &names)
{
  size_t saved = 0;
  for (int level=0; level<NLevels(); level++)
  {
    Shape *shape = InitLevelLocked(level);
    if (NamedSelection::Access::KeepAllSelections(this,shape))
    {
      continue;
    }
    for (int s=0; s<shape->_sel.Size(); s++)
    {
      NamedSelection &sel = shape->_sel[s];
      // when selection not found in the list, remove its vertex data
      if (names.FindKey(sel.GetName())<0)
      {
        saved += sel.GetMemoryControlled();
        
        sel.RemoveAllButSections();

        saved -= sel.GetMemoryControlled();
      }
    }
  }
  if (saved>0)
  {
    LogF(
      "%s: %s saved in selections",
      cc_cast(GetName()),cc_cast(FormatByteSize(saved))
    );
  }
}

void LODShape::OptimizeShapes()
{
  if (!_streaming)
  {
    // delete LODs that are too complex
    // this reduces memory usage
    // not necessary for streaming shapes
    int i=0;
    // remove any NULL LODs
    int d=0;
    for( int s=0; s<_nLods; s++ )
    {
      if( _lods[s] )
      {

        // delete any vdecal lods (used for some trees)
        if
        (
          s!=0 && (GET_LOD(_lods[s])->GetAndHints()&ClipDecalMask)!=ClipDecalNone
          && GET_LOD(_lods[s])->NPos()>0
        )
        {
          LogF("VDecal  %s: %d (%f)",(const char *)_name,s,_resolutions[s]);
          // enabled only in binarize
        }
        else if (atoi(GET_LOD(_lods[s])->PropertyValue("notl"))>0)
        {
          LogF("TL dropped %s: %d (%f)",(const char *)_name,s,_resolutions[s]);
          // TL dropping enabled only in binarize
        }
        else if (_resolutions[s]>=20000 && _resolutions[s]<20999)
        {
          // drop EDIT lods
        }
        else
        {
          _resolutions[d]=_resolutions[s];
          _lods[d]=_lods[s];
          //GET_LOD(_lods[d])->SetLevel(d);
          d++;
        }
      }
    }
    for (int s=d; s<_nLods; s++)
    {
      _lods[s] = NULL;
    }
    _nLods=d;

    // optimize shadow LODs
    // if some lod is disabled for shadowing
    // all lods before it should be disabled to
    // scan shadow LODs 
    for( i=0; i<_nLods; i++ )
    {
      if( _resolutions[i]>=900 ) break;
      if (_lods[i].NFaces()<1024) break;
    }

    if (i>0 && _resolutions[i]>=900) --i;

    _minShadow = i;

    for(i=_minShadow; i<_nLods; i++ )
    {
      if (_resolutions[i]>900) break; // only normal LODs can be used for shadowing
      // disable shadows of too complex LODs
      const char *prop = GET_LOD(_lods[i])->PropertyValue("lodnoshadow");
      if (prop && atoi(prop)>0 )
      {
        _minShadow = i+1;
        if (_minShadow>=_nLods || _resolutions[_minShadow]>900)
        {
          _minShadow = _nLods;
        }
      }
    }
    
    if (_minShadow<_nLods)
    {
      int nFaces = _lods[_minShadow].NFaces();
      if (nFaces>=2000)
      {
        RptF("Too detailed shadow lod in %s (%d:%f : %d) - shadows disabled",
        (
          const char *)GetName(),
          _minShadow,Resolution(_minShadow),nFaces
        );
        _minShadow = _nLods;
      }
    }
  }
  ScanShapes();
}

DEFINE_FAST_ALLOCATOR(ProxyObject)

static RString ReplacesChars(RString shapeName, char c, char w)
{
  if (!strchr(shapeName,c)) return shapeName;
  shapeName.MakeMutable();
  char *str = shapeName.MutableData();
  while (*str)
  {
    if (*str==c) *str = w;
    str++;
  }
  return shapeName;
}

bool GReplaceProxies = true;


bool NamedSelection::Access::KeepAllSelections(const LODShape *lShape, const Shape *shape)
{
  return (
    // memory level contains lots of named selections
    shape==lShape->MemoryLevel()
    // all geometry levels need to scan component selections
    || shape==lShape->FireGeometryLevel()
    || shape==lShape->GeometryLevel()
    || shape==lShape->ViewGeometryLevel()
    #if VIEW_INTERNAL_GEOMETRIES
    || shape==lShape->ViewPilotGeometryLevel()
    || shape==lShape->ViewGunnerGeometryLevel()
    || shape==lShape->ViewCargoGeometryLevel()
    #endif
    // paths use named points
    || shape==lShape->PathsLevel()
    // hitpoints 
    || shape==lShape->HitpointsLevel()
  );
}


/** used to report when the selection content is used for the first time */
void NamedSelection::Access::Touch() const
{
#if _ENABLE_REPORT
  // during non-binarized file loading any access is OK
  if (_lShape->CheckSelectionsOpen())
  {
    return;
  }
  if (KeepAllSelections(_lShape,_shape))
  {
    return;
  }    
  if (!_sel.WasContentUsed())
  {
    if (_lShape->ReportSelectionContentUsed(_sel.GetName()))
    {
      LogF("'%s': used selection '%s'",cc_cast(_lShape->GetName()),cc_cast(_sel.GetName()));
    }
  }
#endif
}

void LODShape::ReadShadowOffsetFromProperty(float &shadowOffset)
{
  if (FindGeometryLevel()<0) return;
  const RStringB &off = PropertyValue("shadowoffset");
  if (!off.IsEmpty())
  {
    shadowOffset = atof(off);
  }
}


void LODShape::AutodetectShadowOffsetIfNeeded()
{
  if (_shadowOffset>=FLT_MAX)
  {
    float size = _minMax[0].Distance(_minMax[1]);
    // auto-detect shadow offset based on model size
    _shadowOffset = InterpolativC(size,2.5f,10.0f,0.02f,0.10f);
  }
}

/*!
\patch 5131 Date 2/21/2007 by Flyman
- Optimized: Trees can employ not-culling of back faces now, which helps their vertex rate
*/
void LODShape::CustomizeShape(Shape *shape, int lod, float resolution, const ModelConfig *modelConfig, const ModelTarget &mt, GeometryOnly geometryOnly) const
{
  // stack report seems not needed here - CustomizeShape called inside p3d loading, which already is reporting
  //REPORTSTACKITEM(GetName());

  // Detect an empty non-first LOD
  if ((resolution < 900.0f) && (shape->NVertex() == 0) && (lod != 0))
  {
    LogF("Error: Empty LOD (%s) detected in shape %s", LevelName(resolution), Name());
  }

  // Delete back faces if possible
  shape->DeleteBackFaces();

  // scan for sections
  if (modelConfig->IsDefined())
  {
    shape->DefineSections(modelConfig->GetCfgModels(),modelConfig->GetModel());
  }

  // scan for proxies
  for( int i=0; i<shape->_sel.Size(); i++ )
  {
    static const char proxyName[]="proxy:";
    static int proxyNameLen=strlen(proxyName);

    const NamedSelection::Access sel(shape->_sel[i],this,shape);
    const char *selName=sel.Name();

    if( strncmp(selName,proxyName,proxyNameLen) ) continue;
    selName+=proxyNameLen;

    if( sel.Size()!=3 )
    {
      RptF
        (
        "%s:%s: Bad proxy object definition %s",
        (const char *)_name,LevelName(resolution),sel.Name()
        );
      continue;
    }

    // check if proxy selection is hidden
    if (sel.Faces().Size()!=1)
    {
      RptF("%s: Proxy object should be single face %s",(const char *)_name,sel.Name());
      if (sel.Faces().Size()<1) continue;
    }

    // Mark proxy faces as IsHiddenProxy
    for (int i = 0; i < sel.Faces().Size(); i++)
    {
      shape->_faceProperties[sel.Faces()[i]].OrSpecial(IsHiddenProxy);
      // face must not be hidden
      shape->_faceProperties[sel.Faces()[i]].AndSpecial(~IsHidden);
    }

    // set corresponding section flags
    //sel.SetNeedsSections(true);
  }

  bool includeNeighbourFaceVertices;
  if (IsSpecShadowVolume(resolution))
  {
    shape->UpdateToShadowGeometry(this,true,GetName());
    for (int i=0; i<shape->_faceProperties.Size(); i++)
    {
      PolyProperties &prop = shape->_faceProperties[i];
      prop.SetTexture(NULL);
      prop.SetMaterialExt(NULL);
      prop.AndSpecial(IsHiddenProxy|NoShadow);
    }
    includeNeighbourFaceVertices = true;
  }
  else
  {
    includeNeighbourFaceVertices = false;
  }

  // If shadow buffer, set everything (apart of base texture if not opaque) to predefined values
  if (IsSpecInterval(resolution, SHADOWBUFFER_SPEC, SHADOWBUFFER_MAX))
  {
    for (int i = 0; i < shape->_faceProperties.Size(); i++)
    {
      PolyProperties &prop = shape->_faceProperties[i];
      int spec = prop.Special();
      Texture *texture = prop.GetTexture();
      if (texture)
      {
        // If texture is transparent or alpha, set the IsTransparent model flag (to invoke
        // alpha testing during SB rendering), else use no texture
        if (texture->IsTransparent() || texture->IsAlpha())
        {
          spec |= IsTransparent;
        }
        else
        {
          prop.SetTexture(NULL);
        }
      }
      prop.SetMaterialExt(NULL);
      prop.AndSpecial(IsHiddenProxy|NoShadow);
      prop.OrSpecial(spec&IsTransparent);
    }
  }

  shape->FindSections(this);

  // Create the subskeleton for the shape (it can also split some sections)
  if (_skeletonSource.NotNull())
  {
    shape->CreateSubskeleton(_skeletonSource, includeNeighbourFaceVertices, mt._bonesInterval, this, resolution, modelConfig);
  }

  // Update selections referencing sections
  shape->RescanSections(this);

  // If shadow volume then update the material
  if (IsSpecShadowVolume(resolution))
  {
    Ref<TexMaterial> material = GlobPreloadMaterial(ShadowMaterial);
    shape->SetUniformMaterial(material, NULL);
    shape->VertexDeclarationNeedsUpdate();
    shape->DisablePushBuffer();
  }

  // Generate ST coordinates in case the declaration contains the ST
  // Note that we must know the faces, declaration and UV coordinates
  EVertexDecl vd = shape->GetVertexDeclaration();
  if (VertexDeclarations[vd] & VDI_ST)
  {
    shape->GenerateSTArray(this, lod);
  }

  // Make sure the second UV set is present, if required by the declaration
  if (VertexDeclarations[vd] & VDI_TEX1)
  {
    if (shape->GetUVArray(1).Size() != shape->NVertex())
    {
      RptF("Warning: %s:%s: 2nd UV set needed, but not defined", (const char *)_name, LevelName(resolution));
    }
  }
  
  // Clear the point and vertex conversion arrays
  bool pointsNeeded = PointsNeeded(resolution);
  shape->OptimizePointToVertex(pointsNeeded);

  // scan selections for proxy objects
  for( int i=0; i<shape->_sel.Size(); i++ )
  {
    static const char proxyName[]="proxy:";
    static int proxyNameLen=strlen(proxyName);

    const NamedSelection::Access sel(shape->_sel[i], this, shape);
    const char *selName=sel.Name();

    if( strncmp(selName,proxyName,proxyNameLen) ) continue;
    selName+=proxyNameLen;

    if( sel.Size()!=3 )
    {
      RptF
        (
        "%s:%s: Bad proxy object definition %s",
        (const char *)_name,LevelName(resolution),sel.Name()
        );
      continue;
    }

    // check if proxy selection is hidden
    if (sel.Faces().Size()!=1)
    {
      RptF("%s: Proxy object should be single face %s",(const char *)_name,sel.Name());
      if (sel.Faces().Size()<1) continue;
    }

    // set corresponding section flags
    //Assert(sel.GetNeedsSections());
    // note: following loop probably does not do what it is intended for
    // as most proxies do not have NeedsSection set, therefore sel does not contain section list
    for (int si=0; si<sel.NSections(); si++)
    {
      int section = sel.GetSection(si);
      ShapeSection &ss = shape->GetSection(section);
      ss.SetTexture(NULL);
    }

    char shapeName[1024+1];
    strncpy(shapeName,selName,1024);
    shapeName[1024]=0;

    char *ext=strrchr(shapeName,'.');
    int id = -1;
    if (ext)
    {
      *(ext++)=0;
      id = atoi(ext);
    }
    // it is proxy: define proxy slot

    Ref<ProxyObject> obj = new ProxyObject;
    obj->name = shapeName;
    obj->id = id;
    // get proxy object coordinates
    // scan selection
    int pi0=sel[0],pi1=sel[1],pi2=sel[2];

    Vector3 p0=shape->Pos(pi0);
    Vector3 p1=shape->Pos(pi1);
    Vector3 p2=shape->Pos(pi2);

    float dist01=p0.Distance2(p1);
    float dist02=p0.Distance2(p2);
    float dist12=p1.Distance2(p2);

    // p0,p1 should be the shortest distance
    if( dist01>dist02 ) Swap(p1,p2),Swap(dist01,dist02); // swap points 1,2
    if( dist01>dist12 ) Swap(p0,p2),Swap(dist01,dist12); // swap points 0,2
    // p0,p2 should be the second shortest distance
    if( dist02>dist12 ) Swap(p0,p1),Swap(dist02,dist12);
    //  verify results

    // Trans
    Matrix4 trans;
    trans.SetPosition(p0);
    trans.SetDirectionAndUp((p1-p0),(p2-p0));
    obj->trans = trans;
    obj->invTransform=trans.InverseScaled();

    // Bone index
    if (_skeletonSource.NotNull())
    {
      DoAssert(sel.Size() > 0);
      int point = sel[0];
      const AnimationRTWeight &wg = shape->GetVertexBoneRef()[point];
      if (wg.Size()>0)
      {
        DoAssert(wg.Size()==1);
        obj->boneIndex = shape->GetSkeletonIndexFromSubSkeletonIndex(wg[0].GetSel());
      }
      else
      {
        // Proxy not bound to any bone
        obj->boneIndex = SkeletonIndexNone;
      }
    }
    else
    {
      // Proxy not bound to any bone
      obj->boneIndex = SkeletonIndexNone;
    }

    obj->selection = i;
    
    // sections were done in FindSections above, we can scan them here
    // we still have face offsets here, check which section is straightforward
    
    obj->section = -1;

    Buffer<Offset> offsets;
    sel.CalculateFaceOffsets(offsets,shape);
    Assert(offsets.Size()==1);

    if (offsets.Size()==1)
    {
      // one proxy is one face - it has to be in one section only
      for (int s=0; s<shape->NSections(); s++)
      {
        const ShapeSection &ss = shape->GetSection(s);
        if (ss.beg<=offsets[0] && ss.end>offsets[0])
        {
          obj->section = s;
        }
      }
    }
    shape->_proxy.Add(obj);
  }


  if (shape->_sel.Size()<=0)
  {
    // no selections: we may safely optimize
    shape->Optimize();
  }

  if (geometryOnly&GONoUV)
  {
    for (int s=0; s<shape->NSections(); s++)
    {
      ShapeSection &ss = shape->GetSection(s);
      for (int uvStage = 0; uvStage < shape->NUVStages(); uvStage++)
      {
        ss.SetAreaOverTex(uvStage, 1.0f);
      }
    }
  }
  else
  {
    shape->RecalculateAreas();
  }

  if (shape->_loadWarning)
  {
    RptF("Warnings in %s:%s",(const char *)_name,(const char *)LevelName(resolution));
  }

  // textures loaded - we may adjust them now
  shape->AdjustTextures();
}

DEF_RSB(visual);
DEF_RSB(explicit);
DEF_RSB(shadowvolume);
DEF_RSB(none);
DEF_RSB(hybrid);

bool LODShape::Load(QIStream &f, const char *name, bool reversed, const ModelTarget &mt, SkeletonSource *ss)
{
  _name = name;

  REPORTSTACKITEM(GetName());

  {
    DoClear();

    #if VERBOSE
    LogF("Load %s, %s",(const char *)_name,reversed ? "Reversed" : "");
    #endif

    // check optimized load
    int seekStart = f.tellg();
    if (LoadOptimized(f))
    {
      // reverse all vector data if necessary
      if (reversed)
      {
        Reverse();
        _remarks|=ShapeReversed;
      }
      return true;
    }
    // binarized loading failed - rewind and try loading non-binarized
    f.seekg(seekStart,QIOS::beg);

    #if _ENABLE_REPORT
    _selectionsOpen = true; // while non-binarized loading any selection access is OK
    #endif
    
    // check magic

    _special=0;
    _remarks=0;
    

    if( reversed ) _remarks|=ShapeReversed;

    // load all LODs and their setups respectively
    // check for LOD header 
    char magic[4];
    int nLods=1;
    bool tagged=false;
    int ver=0;

    _mass=0;
    _invMass=1e10;
    _invInertia.SetZero();
    _inertia.SetZero();
    _centerOfMass=Vector3(VZero);
    
    f.read(magic,sizeof(magic));
    if( f.fail() || f.eof() )
    {
      WarningMessage("Error loading %s (Magic)",(const char *)_name);
      goto Error;
    }
    if( !strncmp(magic,"NLOD",sizeof(magic)) )
    {
      // LOD header - we will load multiple LODs
      Log("Warning: NLOD format in object %s",(const char *)_name);
      tagged=true;
      f.read((char *)&nLods,sizeof(nLods));
      if( f.fail() || f.eof() ) {WarningMessage("Error loading %s (nLods)",(const char *)_name);goto Error;}
    }
    else if( !strncmp(magic,"MLOD",sizeof(magic)) )
    {
      // LOD header - we will load multiple LODs
      tagged=true;
      f.read((char *)&ver,sizeof(ver));
      f.read((char *)&nLods,sizeof(nLods));
      if( f.fail() || f.eof() ) {WarningMessage("Error loading %s (nLods)",(const char *)_name);goto Error;}
    }
    else
    {
      ErrorMessage("Warning: preNLOD format in object %s",(const char *)_name);
      f.seekg(-(int)sizeof(magic),QIOS::cur);
      Fail("Very old object loaded.");
    }
    int major,minor;
    major=ver>>8;
    minor=ver&0xff;
    //if( major>1 ) // only format versions 0.xx and 1.xx supported
    if( major>1 ) // only format versions 1.xx supported
    {
      WarningMessage("%s: Unsupported version %d.%02d",(const char *)_name,major,minor);
      goto Error;
    }

    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    // model config file may exist
    ModelConfig modelConfig(GetName(), &globals);

    // Load the TI properties from the model config
    if (modelConfig.IsDefined())
    {
      if (modelConfig.GetModel().FindEntry("htMin")) _htMin = modelConfig>>"htMin";
      if (modelConfig.GetModel().FindEntry("htMax")) _htMax = modelConfig>>"htMax";
      if (modelConfig.GetModel().FindEntry("afMax")) _afMax = modelConfig>>"afMax";
      if (modelConfig.GetModel().FindEntry("mfMax")) _mfMax = modelConfig>>"mfMax";
      if (modelConfig.GetModel().FindEntry("mFact")) _mFact = modelConfig>>"mFact";
      if (modelConfig.GetModel().FindEntry("tBody")) _tBody = modelConfig>>"tBody";
    }

    // If the skeleton source is specified via parameter then use it, else load it from config
    if (ss)
    {
      _skeletonSource = ss;
    }
    else
    {
      AnimationHolder holder;
      if (modelConfig.IsDefined())
      {
        // Get the skeleton name
        if (modelConfig.GetModel().FindEntry("skeletonName"))
        {
          RStringB skeletonName = modelConfig>>"skeletonName";
          // Create the shared skeleton source (if it has a name)
          if (skeletonName.GetLength() > 0)
          {
            SkeletonSourceName name(
              modelConfig.GetCfgSkeletons(),skeletonName
              );
            _skeletonSource = SkeletonSources.New(name);
            _animationType = AnimTypeHardware;
          }
        }
      }
    }

    // Shadow volume shape (loaded if present)
    RefArray<Shape,MemAllocLocal<Ref<Shape>,16> > shapeSV;

    bool treeCrownNeeded = false;
    bool canBlend = false;
    for( int i=0; i<nLods; i++ )
    {
      #if VERBOSE>1
      LogF("  Load level %d",i);
      #endif
      Ref<Shape> shape=new Shape();
      float resolution = 0;

      int startShapeInStream = f.tellg();

      bool wasMassArray = _massArray.Size()>0;
      resolution = shape->LoadTagged
      (
        f, reversed, ver, GeometryOnly(0), _massArray, tagged, this, i
      );
      if (resolution < 0) goto Error;

      GeometryOnly geometryOnly = ResolGeometryOnly(resolution);
      if (geometryOnly!=0 && shape->NFaces()>0)
      {
        // to avoid unnecessary vertex sharing,
        // reload geometry with no normals
        int endShapeInStream = f.tellg();
        f.seekg(startShapeInStream,QIOS::beg);

        // revert state - massArray might be set during LoadTagged
        if (!wasMassArray) _massArray.Clear();

        shape = new Shape();
        float res = shape->LoadTagged(f, reversed, ver, geometryOnly, _massArray, tagged, this, i);
        if (res < 0) goto Error;
        // revert to new position
        f.seekg(endShapeInStream,QIOS::beg);
      }

//#if _BINARIZE_REDUCELODSHAPE
      extern bool GShrinkP3D;
      if (GShrinkP3D)
      {
        // Save memory
        {
          if (IsSpecInterval(resolution, SHADOWVOLUME_SPEC, SHADOWVOLUME_MAX)) continue;
          if (IsSpec(resolution, SHADOW_VOLUME_CARGO)) continue;
          if (IsSpec(resolution, SHADOW_VOLUME_PILOT)) continue;
          if (IsSpec(resolution, SHADOW_VOLUME_GUNNER)) continue;
          if (IsSpec(resolution, VIEW_CARGO_GEOM_SPEC)) continue;
          if (IsSpec(resolution, VIEW_PILOT_GEOM_SPEC)) continue;
          if (IsSpec(resolution, VIEW_GUNNER_GEOM_SPEC)) continue;
          if (IsSpec(resolution, FIRE_GUNNER_GEOM_SPEC)) continue;
        }
      }
//#endif

      // In case of shadow volume remember shape using copy constructor (we may use it later)
      if (IsSpecShadowVolume(resolution)) shapeSV.Add(new Shape(*shape));

      // Add shape into shape list in the LODShape
      AddShape(shape, resolution);

      // Customize the particular shape
      CustomizeShape(shape, i, resolution, &modelConfig, mt, geometryOnly);

      // Set the treeCrownNeeded flag
      shape->UpdateShapePropertiesNeeded();
      if (shape->GetShapePropertiesNeeded()&ESPTreeCrown)
      {
        treeCrownNeeded = true;
      }

      // Check if any visual LOD contain pixel shader designed for blending. In such case we set
      // the appropriate flag in the end.
      if (resolution <= 900.0f)
      {
        for (int i = 0; i < shape->NSections(); i++)
        {
          TexMaterial *material = shape->GetSection(i).GetMaterialExt();
          if (material)
          {
            switch (material->GetPixelShaderID())
            {
            case PSNormalMapThrough:
            case PSNormalMapSpecularThrough:
            case PSNormalMapThroughSimple:
            case PSNormalMapSpecularThroughSimple:
            case PSTree:
            case PSTreeSN:
            case PSTreePRT:
            case PSTreeAdv:
            case PSTreeAdvSimple:
            case PSTreeAdvTrunk:
            case PSTreeAdvTrunkSimple:
              canBlend = true;
            }
          }
        }
      }
    }

    if (!_treeCrown && treeCrownNeeded)
    {
      CalculateTreeCrown();
    }

    // Set the _canBlend flag
    _canBlend = canBlend;

    // map type
    {
      const char *mapType = PropertyValue("map");
      if (!*mapType)
      {
        // TODO: Fail
        //LogF("%s: No map type %s",(const char *)(const char *)_name,mapType);
        _mapType = MapHide;
      }
      MAP_TYPES_ALL(MAP_TYPE_READ)
      else
      {
        RptF("%s: Unknown map type %s",(const char *)_name,mapType);
        //Fail("Unknown map type");
        _mapType = MapHide;
      }
    }
    // move (0,0,0) to the geometry center
    {
      const char *autoCenter=PropertyValue("autocenter");
      if( *autoCenter && atoi(autoCenter)==0 ) _autoCenter=false;
      CalculateMinMax();

      Assert( _nLods>=1 );
//#if _BINARIZE_REDUCELODSHAPE
      extern bool GShrinkP3D;
      if (GShrinkP3D)
      {
        _boundingCenterGShrinkP3D = _boundingCenter;
        // Save memory
        {
          // Replace all visual LODs with the roughest one
          int roughestVisualLOD = -1;
          for (int i = _nLods - 1; i >= 0; i--)
            if (_resolutions[i] < 900 && _lods[i])
            {
              if (roughestVisualLOD < 0)
              {
                roughestVisualLOD = i;
              }
              else
              {
                _lods[i].Free();
                if (!i)
                {
                  _lods[i] = _lods[roughestVisualLOD];
                  _lods[roughestVisualLOD].Free();
                }
              }
            }
        }
      }
//#endif

      CalculateHints();
      // calculate mass, center of mass and inertia
      if( _massArray.Size()>0 ) CalculateMass();
    }
    // auto detect if animation should be allowed
    if (_orHints&(ClipLandMask|ClipDecalMask))
    {
      if (_orHints&ClipLandMask)
      {
        if (
          (_orHints&ClipLandMask)==ClipLandKeep &&
          (_andHints&ClipLandMask)==ClipLandKeep
        )
        {
          // HW animation possible when Keep Height is used for all vertices
        } 
        else if ((_orHints&ClipLandMask)==ClipLandOn)
        {
          if ((_andHints&ClipLandMask)!=ClipLandOn)
          {
            RptF("%s - Not all levels have On Surface set",cc_cast(GetName()));
          }
          _animationType = AnimTypeSoftware;
        }
        else if ((_andHints&ClipLandMask)!=ClipLandOn)
        {
          RptF("SW animation used for %s - Not all levels have Keep Height set",cc_cast(GetName()));
          _animationType = AnimTypeSoftware;
        }
      }
      else
      {
        // SW animation needed for Decal models
        _animationType = AnimTypeSoftware;
        RptF("Warning: %s - Decal flags, no longer supported, forcing SW rendering",cc_cast(GetName()));
      }
    }
    if ((_orHints&ClipLightMask)==(_andHints&ClipLightMask))
    {
      ClipFlags light = _orHints&ClipLightMask;
      // display warning for flags which are no longer supported
      if (light&~ClipLightLine)
      {
        RptF("Warning: %s using obsolete lighting flags %x",cc_cast(GetName()),light);
      }
      if (light&ClipLightLine)
      {
        _animationType = AnimTypeSoftware;
      }
    }

    if (_animationType==AnimTypeNone)
    {
      const char *animated=PropertyValue("animated");
      if(*animated) {
        if (atoi(animated))
        {
          _animationType = AnimTypeSoftware;
        }
      }
    }

    // Load _forceNotAlphaModel flag
    {
      const char *forceNotAlpha = PropertyValue("forcenotalpha");
      if (*forceNotAlpha)
      {
        if (atoi(forceNotAlpha))
        {
          _forceNotAlphaModel = true;
        }
        else
        {
          _forceNotAlphaModel = false;
        }
      }
    }
  
    // Load _sbSource flag and customize _preferShadowVolume flag
    {
      if (shapeSV.Size() == 0) _preferShadowVolume = false;
      ScanProjShadow(); // Fill out the _projShadow flag
      RStringB sbSource = PropertyValue("sbsource");
      if (sbSource == RSB(visual))
      {
        _sbSource = SBS_Visual;
        if (_shadowBufferCount > 0)
        {
          LogF("Warning: %s: Shadow buffer levels are present, but visual LODs for SB rendering are required (in sbsource property)", Name());
        }
        if (!_projShadow)
        {
          RptF("Error: %s: Shadows cannot be drawn for this model, but 'visual' source is specified (in sbsource property)", Name());
          _sbSource = SBS_None;
        }
      }
      else if (sbSource == RSB(explicit))
      {
        if (_shadowBufferCount <= 0)
        {
          RptF("Error: %s: Explicit shadow buffer levels are required (in sbsource property), but no one is present - forcing 'none'", Name());
          _sbSource = SBS_None;
        }
        else
        {
          _sbSource = SBS_Explicit;
        }
      }
      else if (sbSource == RSB(shadowvolume))
      {
        if (shapeSV.Size() > 0)
        {
          for (int s = 0; s < shapeSV.Size(); s++)
          {
            // Add shape into shape list in the LODShape
            int newLevel = AddShape(shapeSV[s], SHADOWBUFFER_SPEC + s * 10.0f);

            // auto-center offset needs to be applied on the new shape
            ApplyBoundingCenterGeom(newLevel,_boundingCenter);
            shapeSV[s]->_minMax[0] -= _boundingCenter;
            shapeSV[s]->_minMax[1] -= _boundingCenter;

            // TODO: when multiple shadow volumes, create multiple shadow buffer LODs as well
            // Customize the particular shape
            CustomizeShape(shapeSV[s], newLevel, SHADOWBUFFER_SPEC, &modelConfig, mt, ResolGeometryOnly(SHADOWBUFFER_SPEC));

            _shadowBuffer = newLevel;
            _shadowBufferCount = 1;
            _sbSource = SBS_ShadowVolume;
          }
        }
        else
        {
          RptF("Error: %s: Shadow volume lod is required for shadow buffer (in sbsource property), but no shadow volume lod is present", Name());
          _sbSource = SBS_None;
        }
      }
      else if (sbSource == RSB(none))
      {
        _sbSource = SBS_None;
      }
      else /*Autodetect branch*/
      {
        if (PropertyValue("shadow") == RSB(hybrid)) // Detect trees
        {
          if (_projShadow)
          {
            _sbSource = SBS_Visual;
          }
          else
          {
            _sbSource = SBS_None;
          }
          _preferShadowVolume = false;
        }
        else if (shapeSV.Size() > 0) // Detect object with modeled shadow volume
        {
          for (int s = 0; s < shapeSV.Size(); s++)
          {
            // Add shape into shape list in the LODShape
            // Add shape into shape list in the LODShape
            int newLevel = AddShape(shapeSV[s], SHADOWBUFFER_SPEC + s * 10.0f);

            // auto-center offset needs to be applied on the new shape
            ApplyBoundingCenterGeom(newLevel,_boundingCenter);
            shapeSV[s]->_minMax[0] -= _boundingCenter;
            shapeSV[s]->_minMax[1] -= _boundingCenter;

            // Customize the particular shape
            CustomizeShape(shapeSV[s], newLevel, SHADOWBUFFER_SPEC, &modelConfig, mt, ResolGeometryOnly(SHADOWBUFFER_SPEC));

            _shadowBuffer = newLevel;
            _shadowBufferCount = 1;
            _sbSource = SBS_ShadowVolume;
          }
        }
        else
        {
          if (_projShadow)
          {
            _sbSource = SBS_Visual;
          }
          else
          {
            _sbSource = SBS_None;
          }
        }
      }
    }

    // Load _preferShadowVolume flag
    {
      const char *preferShadowVolume = PropertyValue("prefershadowvolume");
      if (*preferShadowVolume)
      {
        if (atoi(preferShadowVolume))
        {
          _preferShadowVolume = true;
        }
        else
        {
          _preferShadowVolume = false;
        }
      }
    }

    _shadowOffset  = FLT_MAX;
    ReadShadowOffsetFromProperty(_shadowOffset);
    AutodetectShadowOffsetIfNeeded();

    // remove too complex LODs
    OptimizeShapes();
    // scan only normal LODs
    //bool someShadow=false;

    for( int i=0; i<_nLods; i++ ) if( _resolutions[i]<900 && _lods[i] )
    {
      Shape *oShape=GET_LOD(_lods[i]);
      if( oShape ) _special|=oShape->Special();
    }
//#if _BINARIZE_REDUCELODSHAPE
    // Do not recalculate MinMax for LITE shrinked addons, as the boundingCenter is used it getPos and position of buildings is used to spawn loot on proper places
    // see https://projects.bistudio.com/dayz/Different_building_coordinates_from_dedicated_server_local.txt
    extern bool GShrinkP3D;
    if (!GShrinkP3D) 
    {
      CalculateMinMax();
    }
//#endif
    if( _massArray.Size()>0 ) CalculateMass();

    if (modelConfig.IsDefined())
    {
      CheckForcedProperties(modelConfig.GetModel());
    }

    {
      int complexity = LevelRef(0).NFaces();
      float size = BoundingSphere();
      const Shape *view = ViewGeometryLevel();
      int viewComplexity = view ? view->NFaces() : 0;
      // check size and complexity
      // large or simple objects may occlude
      // by default assume cannot occlude
      _canOcclude=false;
      // _viewDensity was calculated few lines above - CalculateHints
      if( viewComplexity>0 && _viewDensity<=-9)
      {
        if( size>5 ) _canOcclude = true;
        if( size>2 && viewComplexity<=6 ) _canOcclude = true;
      }
      // large or complex objects may be occluded
      if( complexity>=6 || size>5 ) _canBeOccluded = true;
      // allow override with property
      const char *expCanOcclude=PropertyValue("canocclude");
      const char *expCanBeOccluded=PropertyValue("canbeoccluded");
      if( *expCanOcclude ) _canOcclude = atoi(expCanOcclude)!=0;
      if( *expCanBeOccluded ) _canBeOccluded = atoi(expCanOcclude)!=0;
    }

    InitConvexComponents(true);
    ScanProperties();
    CalculateHints();
    RescanLodsArea();

    #if 0 //_DEBUG
    if (_paths>=0)
    {
      // optimize paths lod
      LogF("ExtractPath from %s",Name());
      _lods[_paths] = _lods[_paths]->ExtractPath();
      //_lods[_paths]->SetLevel(_paths);
    }
    #endif
    _propertyClass = PropertyValue("class");
    _propertyDamage = PropertyValue("damage");
    if (_propertyDamage.GetLength()<=0)
    {
      _propertyDamage = PropertyValue("dammage");
    }
    
    _propertyFrequent = atoi(PropertyValue("frequent")) != 0;

    if (_skeletonSource.NotNull())
    {
      SetAnimationType(AnimTypeHardware);
      LoadSkeletonFromSource();
      if (modelConfig.IsDefined())
      {
        // if we have a skeleton we might have some animations as well
        ConstParamEntryPtr anims = modelConfig->FindEntry("Animations");
        if (anims)
        {
          TempAddRef();
          LoadAnimations(*anims);
          OptimizeAnimations();
          TempRelease();
        }
        // animations are loaded and optimized - we can use or save them
      }
    }

#if _ENABLE_REPORT
    if (modelConfig.IsDefined())
    {
      // once all work is done, we may remove selections which are no longer needed
      ConstParamEntryPtr removeSel = modelConfig.GetModel().FindEntry("removeSelections");
      if (removeSel)
      {
        for (int i=0; i<removeSel->GetSize(); i++)
        {
          _unusedSelectionNames.AddUnique((*removeSel)[i]);
        }
        _unusedSelectionNames.Compact();
        RemoveSelections(_unusedSelectionNames);
      }
      #if 0
      // as vertexSelections will remove all selections but those listed,
      // using toghether with them has no sense
      FindArrayRStringBCI sectionOnly;
      ConstParamEntryPtr sectionSel = modelConfig.GetModel().FindEntry("sectionOnlySelections");
      if (sectionSel)
      {
        for (int i=0; i<sectionSel->GetSize(); i++)
        {
          sectionOnly.AddUnique((*sectionSel)[i]);
        }
        // some selections need only section info, no faces or vertices
        RemoveVertexSelections(sectionOnly);
      }
      #else
      FindArrayRStringBCI vertexDataNeeded;
      ConstParamEntryPtr vertexSel = modelConfig.GetModel().FindEntry("vertexSelections");
      if (vertexSel)
      {
        for (int i=0; i<vertexSel->GetSize(); i++)
        {
          vertexDataNeeded.AddUnique((*vertexSel)[i]);
        }
      }
      // some selections need only section info, no faces or vertices
      RemoveVertexSelectionsComplement(vertexDataNeeded);
      #endif
    }
    _selectionsOpen = false; // since this point selection access needs to be controlled
#endif
        
    return true;
  }
  Error:
  // create empty shape
  Shape *lod0 =new Shape();
  _lods[0] = lod0;
  //lod0->SetLevel(0);
  _resolutions[0]=0.0f;
  _nLods=1;
  return false;
}

void LODShape::PrepareProperties(ParamEntryPar cfg)
{
  // read names of selections that must not split across sections
}

#if _VBS3_CRATERS_DEFORM_TERRAIN

void LODShape::AlignWithGround(const Landscape *landscape, const Matrix4 &transform, int bias)
{
  // Remove possible OnSurface flag
  //AndSpecial(~OnSurface);

  //   if (_roadway >= 0)
  //   {
  //     Shape *s = GetLevelLocked(_roadway);
  //     s->SurfaceSplit(landscape, transform, 0.01f, 1.0f);
  //   }

  // Determine whether other than visual levels should be created or not
  static float limitSizeToCreateGeometries = 0.2f;
  bool visualOnly = (BoundingSphere() < limitSizeToCreateGeometries);

  // Go through all the levels and split shapes
  for (int i = 0; i < NLevels(); i++)
  {
    // Get the shape to be modified
    Shape *s = InitLevelLocked(i);

    // In case we want only visual LODs and we are already after them, then finish
    if (visualOnly && (i >= _nGraphical)) break;

    // Geometry will be handled later, don't do anything with that at the moment
    if ((i == _geometry) || (i == _geometryFire) || (i == _geometryView)) continue;

    // Split the level
    static float yOffset = 0.001f;
    float scale = transform.Scale();
    s->SurfaceSplit(landscape, transform, yOffset * bias * scale, scale, NULL, true);
  }

  // Either create geometry levels or delete them
  if (visualOnly)
  {
    // Delete other than graphical LODs
    for (int i = _nGraphical; i < _nLods; i++)
    {
      _lods[i].Free();
    }
    _nLods = _nGraphical;

    // LODShape layout has changed, update the _geometryFire, _geometryView, etc
    ScanResolutions();
  }
  else
  {
    // Create geometries out of the roadway triangles
    if (_roadway >= 0)
    {
      Shape *roadway = InitLevelLocked(_roadway);
      if (_geometry >= 0) InitLevelLocked(_geometry)->CreateAlignedConvexComponents(*roadway);
      if (_geometryFire >= 0 && _geometryFire != _geometry) InitLevelLocked(_geometryFire)->CreateAlignedConvexComponents(*roadway);
      if (_geometryView >= 0 && _geometryView != _geometry && _geometryView != _geometryFire) InitLevelLocked(_geometryView)->CreateAlignedConvexComponents(*roadway);
    }
    //   _geometry = _geometryFire = _geometryView = -1;
  }

  // Create the convex components
  InitConvexComponents(false);

  // Check integrity of all the levels
  for (int i = 0; i < NLevels(); i++)
  {
    GetLevelLocked(i)->CheckIntegrity();
  }
}

#endif

void LODShape::Reload( QIStream &f, const char *name, bool reversed )
{
  Log("Reload shape %s with %s",(const char *)_name,name);

  Load(f,name,reversed);
}

/**
By creating vertex buffer / shadow in advance we may save some time during rendering.
*/
bool LODShape::PreloadVBuffer(int level, float dist2, bool forceAnimated)
{
  ShapeUsed shape = Level(level);
  if (GTimeManager.TimeLeft(TCCreateVertexBuffer)>0)
  {
    //_animationType
    OptimizeLevelRendering(level,forceAnimated);
    return true;
  }
  return false;
}

bool LODShape::PreloadShadow(float z2) const
{
  int level = FindShadowVolume();
  if (level<0) return true;
  if (!CheckLevelLoaded(level,true)) return false;
  return unconst_cast(this)->PreloadVBuffer(level,z2);
}


//bool LODShape::Preload(FileRequestPriority prior, const char *name)
//{
//  // Preload push buffer data
//  bool plRequest = PLPushBuffer::RequestHeader(prior, name);
//
//  QIFStreamB in;
//  in.AutoOpen(name);
//  // currently whole shape data are needed when loading object - this should be changed
//  if (in.fail())
//  {
//    RptF("LODShape::Preload: shape '%s' not found",name);
//    return plRequest;
//  }
//  return plRequest && in.PreRead(prior);
//}

bool LODShape::Load(const char *name, bool reversed, const ModelTarget &mt, const char * forceName)
{
  // convert Data.. structures into Poly
  // load external file, convert ...
  // if applied outside constructor, Unload should be performed first
  // on error return empty object
  char nameTemp[1024];
  if( strlen(name)>sizeof(nameTemp) )
  {
    WarningMessage("Name '%s' too long.");
    return false;
  }
  strncpy(nameTemp,name,sizeof(nameTemp)); // save name
  nameTemp[sizeof(nameTemp)-1]=0;
  strlwr(nameTemp);
  //Log("Load shape %s",_name);

  QIFStreamB f;
  if (forceName) 
    f.AutoOpen(forceName);
  else
    f.AutoOpen(nameTemp);

  if( f.fail() )
  {
    // make sure the shape is named, even when not loaded
    // otherwise shape caching may be confused
    _name = nameTemp;
    ErrorMessage(EMMissingData, "Cannot open object %s",(const char *)nameTemp);
    return false;
  }
  return Load(f, nameTemp, reversed, mt);
}

ConvexComponents *LODShape::GetConvexComponents(int level) const
{
  if (level==FindGeometryLevel())
  {
    return _geomComponents;
  }
  else if (level==FindFireGeometryLevel())
  {
    return _fireComponents;
  }
  else if (level==FindViewGeometryLevel())
  {
    return _viewComponents;
  }
  #if VIEW_INTERNAL_GEOMETRIES
  else if (level==FindViewPilotGeometryLevel())
  {
    return _viewPilotComponents;
  }
  else if (level==FindViewGunnerGeometryLevel())
  {
    return _viewGunnerComponents;
  }
  else if (level==FindViewCargoGeometryLevel())
  {
    return _viewCargoComponents;
  }
  #endif
  return NULL;
}

RStringB LODShape::GetSkeletonSourceName() const
{
  RStringB name;
  if (_skeletonSource.NotNull()) name = _skeletonSource->GetName();
  return name;
}

bool LODShape::IsSkeletonSourceDiscrete() const
{
  if (_skeletonSource.NotNull()) return _skeletonSource->IsDiscrete();
  return true;
}

void LODShape::FindHitComponents(FindArray<int> &hits, const char *name) const
{
  // scan fire-geometry components
  hits.Resize(0);
  const Shape *geom = FireGeometryLevel();
  if (!geom) return;
  int sel = FindNamedSel(geom,name);
  if (sel<0) return;
  // check if it is in some convex component
  const NamedSelection::Access namedSel(geom->NamedSel(sel),this,geom);
  
  const ConvexComponents &ccGeom = GetFireComponents();

  for( int i=0; i<ccGeom.Size(); i++ )
  {
    const ConvexComponent &cc = *ccGeom[i];
    if (cc.IsSubset(namedSel) || namedSel.IsSubset(cc))
    {
      // selection in component or component in selection
      hits.AddUnique(i);
    }
  }
  hits.Compact();
}

void LODShape::InitConvexComponents(ConvexComponents &cc, Shape *geom, bool fullReport)
{ 
  geom->RecalculateNormalsAsNeeded();
  for( int i=0; i<geom->NNamedSel(); i++ )
  {    
    const NamedSelection::Access sel(geom->NamedSel(i),this,geom);
    const char * name = sel.Name();
   
    if (0 != strnicmp(name, "component", 9))  // not a component       
      continue;
    
    int iNamedIndex;
    sscanf(name + strlen("component"),"%d",&iNamedIndex);          
    
    if( sel.Faces().Size()<4 )
    {
      if( sel.Faces().Size()>0 )
      {
        RptF("Strange convex %s in %s:%s", (const char *)name,(const char *)_name,LevelName(geom));
      }
// report empty components only in internal version
#if _ENABLE_REPORT
      else if (fullReport && sel.Size()>=5)
      {
        RptF("None face in %s in %s:%s", (const char *)name,(const char *)_name,LevelName(geom));
      }
#endif

      continue;
    }
    ConvexComponent *component=new ConvexComponent;
    cc.Add(component);
    
    component->Init(geom,sel,iNamedIndex);
    
    // if not streaming, it is quite likely we will want to save the selections again    
    if (_streaming)
    {
      // we can delete the selection now - it is no longer needed
      #if _ENABLE_REPORT
        // turn on verification it is really not needed
        _unusedSelectionNames.AddUnique(sel.GetName());
      #endif
      geom->_sel.Delete(i);
      i--; // compenzate deleted item
    }
  }
  geom->_sel.Compact();
#if _ENABLE_REPORT
    _unusedSelectionNames.Compact();
#endif
  cc.Validate();
  if(cc.Size() >= 250)
  {
    RptF("PerfWarning: Way too much components (%d) in %s:%s", cc.Size(), (const char *)_name,LevelName(geom));
  }
}

DEF_RSB(treehard);
DEF_RSB(treesoft);
DEF_RSB(bushhard);
DEF_RSB(bushsoft);

#if _ENABLE_PHYSICS
enf::PhysicsGeomDef* LODShape::CreatePhysComponents(unsigned int& cnt, bool useView/* = false*/)
{
  bool isgeom = true;
	Shape* geom = G__Level(_geometry);
	if(!geom || !_geomComponents->Size())
  {
		geom = G__Level(_geometryView);
    isgeom = false;
  }
		 
	if(!isgeom && !useView)
		return 0;
	
	if (GetPropertyClass() == RSB(bushhard) || GetPropertyClass() == RSB(bushsoft) )//|| GetPropertyClass() == RSB(treesoft))
  {
    return 0;
  }

	if(!geom)
		return 0;
	
	//uz je to pripravene
	if(geom->GetPhysLen())
	{
		cnt = geom->GetPhysLen();
		return geom->_physGeoms;
	}
	
	geom->_physGeomsLen = isgeom ? _geomComponents->Size() : _viewComponents->Size();
	cnt = geom->_physGeomsLen;
  if(!cnt)
    return 0;
	

	for(int i = 0; i < geom->_physGeomsLen; ++i)
	{
	  const Ref<ConvexComponent> comp = isgeom ? _geomComponents->Get(i) : _viewComponents->Get(i);
		if(!comp->IsPrepared())
    {
      cnt = 0;
      geom->_physGeomsLen = 0;
      return 0;
    }
  }

	bool matOk = true;
	//geom->AddRef();
	
	geom->_physGeoms = (enf::PhysicsGeomDef*)GMemFunctions->New(sizeof(enf::PhysicsGeomDef) * geom->_physGeomsLen);
	
	for(int i = 0; i < cnt; ++i)
	{
		const Ref<ConvexComponent> comp = isgeom ? _geomComponents->Get(i) : _viewComponents->Get(i);
		if(!comp->GetPhysGeom(geom, geom->_physGeoms[i]))
			matOk = false;
	}

	return geom->_physGeoms;
}

//unsigned int LODShape::GetPhysShapeCnt()
//{
//  return physShapeCnt;
//}
#endif

void LODShape::DoBuildRoadwayFaces() const
{
  PROFILE_SCOPE_DETAIL_EX(rcBRF,coll);
  const Shape *shape = RoadwayLevel();
  Vector3Val sMin = shape->Min();
  Vector3Val sMax = shape->Max();
  _roadwayFaces = new RoadwayFaces();
  float size = floatMax(sMax.X()-sMin.X(),sMax.Z()-sMin.Z());
  float epsilon = size*0.001f;
  _roadwayFaces->Dim(sMin.X()-epsilon,sMin.Z()-epsilon,size+2*epsilon);
  for( int s = 0, i = 0; s<shape->NSections(); s++)
  {
    const ShapeSection &sec =  shape->GetSection(s);
    for( Offset f=sec.beg; f<sec.end; shape->NextFace(f),i++ )
    {
      const Poly &face=shape->Face(f);
      Vector3 fMin,fMax;
      face.GetMinMax(fMin,fMax,*shape);
      if (fMin.X()<fMax.X() && fMin.Z()<fMax.Z()) // for zero area there is no intersection possible, ignore
      {
      RoadwayFace rFace;
      rFace.o = f;
      rFace.i = i;
      rFace.section = s;
      rFace.region = OffTreeRegion<float>(fMin.X(),fMin.Z(),fMax.X(),fMax.Z());
      _roadwayFaces->Add(rFace);
    }    
  }
  }

}

void LODShape::InitCC( Ref<ConvexComponents> &cc, Shape *shape, bool fullReport )
{
  if (shape)
  {
    cc = new ConvexComponents();
    InitConvexComponents(*cc,shape,fullReport);
    if( cc->RecalculateEdges(shape, this) )
    {
      RptF
      (
        "Shape %s:%s - bad components",
        (const char *)Name(),LevelName(shape)
      );
    }
  }
}

/// used for QSort sorting
struct OcclusionEdgeSortInfo: public OcclusionEdge
{
  float area;
  
  OcclusionEdgeSortInfo(){}
  OcclusionEdgeSortInfo(const OcclusionEdge &src, float a):OcclusionEdge(src),area(a){}

  static int Compare(const OcclusionEdgeSortInfo *e1,const OcclusionEdgeSortInfo *e2)
  {
    return sign(e2->area-e1->area);
  }
};

TypeIsSimple(OcclusionEdgeSortInfo)

/** 
We currently check for very few basic cases.
If needed, we can implement more elaborate algorithm or provide artits-guided occlusion outlines
*/
void LODShape::InitOcclusion(Shape *view, const ConvexComponents &cc)
{
  // we need face offsets - make sure they exist
  view->BuildFaceIndexToOffset();
  // detect if the component is vertically convex
  // any faces facing down need to be fully below the ground
  // if not, the component is not usable for us

  FindArray<OcclusionEdge, MemAllocLocal<OcclusionEdge,128> > edges;
  
  AutoArray<Offset, MemAllocLocal<Offset,64> > faces;
  // faces facing down can be needed when checking against other components
  AutoArray<Offset, MemAllocLocal<Offset,64> > bottom;

  // indices of components which are still undecided
  AutoArray<int, MemAllocLocal<int,32> > left;
  // indices of components which were already used
  AutoArray<int, MemAllocLocal<int,32> > used;

  for (int i=0; i<cc.Size(); i++) left.Add(i);

  // avoid performing the loop if we know there are no components left
  while(left.Size()>0)
  {
    // count how many components we have added in this iteration
    int componentsAdded = 0;
    for (int i=0; i<left.Size(); i++)
    {
      int ci = left[i];
      const ConvexComponent &c = *cc[ci];
      // what is considered vertical
      const float verticalLimit = 1e-3f;
      faces.Resize(0);
      bottom.Resize(0);
      bool isAboveGround = false;
      for (int f=0; f<c.NPlanes(); f++)
      {
        const Plane &p = c.GetPlane(view->GetPlanesArray(), f);
        // planes point inwards
        // ignore vertical faces
        if (fabs(p.Normal().Y())<verticalLimit)
        {
          continue;
        }
        if (p.Normal().Y()<0)
        {
          // facing up - will be used for rendering
          faces.Add(c.GetFaceOffset(view,f));
        }
        else
        {
          // facing down - check if below the ground 
          const Poly &face = c.GetFace(view,f);
          // check all vertices in original O2 model space
          for (int v=0; v<face.N(); v++)
          {
            int vv = face.GetVertex(v);
            Vector3Val vPos = view->Pos(vv);
            if (vPos.Y()>=-_boundingCenter.Y())
            { // above ground, we need to skip this component
              bottom.Add(c.GetFaceOffset(view,f));
              isAboveGround = true;
              // no need to scan this face any more, but continue scanning other faces
              break;
            }
          }
        }
      }

      if (isAboveGround)
      {
        // check if it is withing some other component which is already included
        // TODO: more general test: check if it is within the currently know horizon
        // this may be more difficult, as the horizon can be non-convex
        // but it can find solution in more situations
        bool isInsideOther = false;
        for (int o=0; o<used.Size(); o++)
        {
          int ci = used[o];
          const ConvexComponent &cic = *cc[ci];
          // if all vertices of the face are withing a component, whole face is there
          bool isInsideThisCC = true;
          for (int f=0; f<bottom.Size(); f++)
          {
            const Poly &face = view->Face(bottom[f]);
            for (int v=0; v<face.N(); v++)
            {
              int vv = face.GetVertex(v);
              Vector3Val vPos = view->Pos(vv);
              if (!cic.IsInside(view->GetPlanesArray(), vPos))
              {
                isInsideThisCC = false;
                break;
              }
            }
          }
          if (isInsideThisCC)
          {
            isInsideOther = true;
            break;
          }
          
        }
        if (!isInsideOther)
        {
          // do not add this one, continue scanning other components
          continue;
        }
      }

      // count and record this one
      componentsAdded++;
      used.Add(ci);
      left.Delete(i),i--;

      // we know component is usable - now record the edges we want to use
      for (int e=0; e<faces.Size(); e++)
      {
        const Poly &face = view->Face(faces[e]);
        Assert(face.N()>=3);
        int prev = face.GetVertex(face.N()-1);
        for (int v=0; v<face.N(); v++)
        {
          int curr = face.GetVertex(v);
          edges.AddUnique(OcclusionEdge(prev,curr));
          prev = curr;
          
        }
      }
    }
    // if nothing was added, there is no need to iterate any more
    if (componentsAdded==0) break;
  }
  
  // if there are many edges, chance is some of them serve no real purpose
  // remove edges whose area is too small
  AutoArray<OcclusionEdgeSortInfo, MemAllocLocal<OcclusionEdgeSortInfo,64> > sort;
  for (int i=0; i<edges.Size(); i++)
  {
    Vector3Val a = view->Pos(edges[i].a);
    Vector3Val b = view->Pos(edges[i].b);
    // calculate area of the edge

    float aY = a.Y()+_boundingCenter.Y();
    float bY = b.Y()+_boundingCenter.Y();
    float area = a.DistanceXZ2(b)*((aY+bY)*0.5f);
    // ignore faces which are real small
    if (area>1.0f)
    {
      sort.Add(OcclusionEdgeSortInfo(edges[i],area));
    }
  }
  QSort(sort,OcclusionEdgeSortInfo::Compare);
  
  // sort the rest based on area
  
  _horizonEdges.Realloc(sort.Size());
  _horizonEdges.Resize(sort.Size());
  for (int i=0; i<sort.Size(); i++)
  {
    _horizonEdges[i] = sort[i];
  }
  // if there are no edges, occlusion is actually not possible
  if (_horizonEdges.Size())
	{
#ifdef GEOM_OCCLUDERS
		enf::AAB bounds(true);
		int count = 0;
		for (int i=0; i<cc.Size(); i++)
		{
			const ConvexComponent &c = *cc[i];

			//check out if we have box here
			if(c.NPlanes() == 6)
			{
				int mask = 0;
				static const Vector3 axials[] = {
					Vector3(1,0,0),Vector3(-1,0,0),
					Vector3(0,1,0),Vector3(0,-1,0),
					Vector3(0,0,1),Vector3(0,0,-1)
				};

				for(int n = 0; n < 6; n++)
				{
					const Plane& pl = c.GetPlane(view->GetPlanesArray(), n);
					for(int p = 0; p < 6; p++)
					{
						if((mask & (1 << p)) == 0) //don't repeat the same axes
							mask |= (fabs(1.0f - pl.Normal().DotProduct(axials[p])) < 0.001f) ? (1 << p) : 0;
					}
				}
				//we have all major axes. It must be a box
				if(mask == ((1 << 6) - 1))
				{
					bounds.AddMin(c.MinMax()[0]);
					bounds.AddMax(c.MinMax()[1]);
					count++;
				}
			}
		}
		if(count)
		{
			_horizonBounds[0] = bounds.GetMins();
			_horizonBounds[1] = bounds.GetMaxs();
		}
		else
		{
			_horizonBounds[0] = _minMaxVisual[0];
			_horizonBounds[1] = _minMaxVisual[1];
		}
#endif
	}
	else
		_canOcclude = false;
	
}

/**
In some cases we prefer components which reach ground (micro AI). This functions extends them in some typical cases.
*/
void LODShape::ExtendToGround(Shape *view, ConvexComponents &cc)
{
  // we need face offsets - make sure they exist
  view->BuildFaceIndexToOffset();
  // detect components reaching ground, or reaching other components reaching ground
  // ground is defined as "below zero in Objektiv space"
  
  
  AutoArray<Offset, MemAllocLocal<Offset,64> > faces;
  // faces facing down constitute a component "bottom"
  AutoArray<Offset, MemAllocLocal<Offset,64> > bottom;

  // indices of components which are still undecided
  AutoArray<int, MemAllocLocal<int,32> > left;
  // indices of components which were already used (known to be ground reaching)
  AutoArray<int, MemAllocLocal<int,32> > used;

  // start with all components undecided
  for (int i=0; i<cc.Size(); i++) left.Add(i);

  // find any ground reaching components
  // this is O(N^2) complexity. By pre-sorting based on min.Y we could get to O(N logN)
  while(left.Size()>0) // stop performing the loop once we know there are no components left
  {
    // count how many components we have added in this iteration
    int componentsAdded = 0;
    for (int i=0; i<left.Size(); i++)
    {
      int ci = left[i];
      ConvexComponent &c = *cc[ci];
      // what is considered vertical
      const float verticalLimit = 1e-3f;
      faces.Resize(0);
      bottom.Resize(0);
      bool isAboveGround = false;
      for (int f=0; f<c.NPlanes(); f++)
      {
        const Plane &p = c.GetPlane(view->GetPlanesArray(), f);
        // planes point inwards
        // ignore vertical faces
        if (fabs(p.Normal().Y())<verticalLimit)
        {
          continue;
        }
        if (p.Normal().Y()<0)
        {
          // facing up - will be used for rendering
          faces.Add(c.GetFaceOffset(view,f));
        }
        else
        {
          // facing down - check if below the ground 
          const Poly &face = c.GetFace(view,f);
          // check all vertices in original O2 model space
          for (int v=0; v<face.N(); v++)
          {
            int vv = face.GetVertex(v);
            Vector3Val vPos = view->Pos(vv);
            if (vPos.Y()>=-_boundingCenter.Y())
            { // above ground, we need to skip this component
              bottom.Add(c.GetFaceOffset(view,f));
              isAboveGround = true;
              // no need to scan this face any more, but continue scanning other faces
              break;
            }
          }
        }
      }

      if (isAboveGround)
      {
        // check if it is withing some other component which is already included
        // TODO: more general test: check if it is within the currently know horizon
        // this may be more difficult, as the horizon can be non-convex
        // but it can find solution in more situations
        bool isInsideOther = false;
        for (int o=0; o<used.Size(); o++)
        {
          int ci = used[o];
          const ConvexComponent &cic = *cc[ci];
          // if all vertices of the face are withing a component, whole face is there
          bool isInsideThisCC = true;
          for (int f=0; f<bottom.Size(); f++)
          {
            const Poly &face = view->Face(bottom[f]);
            for (int v=0; v<face.N(); v++)
            {
              int vv = face.GetVertex(v);
              Vector3Val vPos = view->Pos(vv);
              if (!cic.IsInside(view->GetPlanesArray(), vPos))
              {
                isInsideThisCC = false;
                break;
              }
            }
          }
          if (isInsideThisCC)
          {
            isInsideOther = true;
            break;
          }
          
        }
        if (!isInsideOther)
        {
          // do not add this one, continue scanning other components
          continue;
        }
        
        LogF("%s: extending %s below ground",cc_cast(Name()),cc_cast(c.GetName()));
        // we know component reaches the ground - extend it
        const float epsilon = 0.5f;
        // Y which we are sure is enough under the ground
        // To consider: we might use min.Y() instead
        float yBelowGround = -_boundingCenter.Y()-epsilon;
        
        // extend the component bottom under the ground
        for (int f=0; f<bottom.Size(); f++)
        {
          const Poly &face = view->Face(bottom[f]);
          for (int v=0; v<face.N(); v++)
          {
            int vv = face.GetVertex(v);
            //Vector3Val vPos = view->Pos(vv);
            // TODO: if the vertex is shared, we cannot replicate it
            // to detect this we would need to traverse all other components
            // to make sure the face stays flat we always reset the Y for all vertices
            view->SetPos(vv)[1] = yBelowGround;
          }
        }
        
        // we need to recompute some related properties (planes, minmax box)
        c.Recalculate(view);
      }

      // we know know this is not above ground - count and record this one
      componentsAdded++;
      used.Add(ci);
      left.Delete(i),i--;

    }
    // if nothing was added, there is no need to iterate any more
    if (componentsAdded==0) break;
  }
}

void LODShape::InitConvexComponents(bool fullReport)
{
  // TODO: move to binarize
  Shape *geom = InitGeometryLevel();
  Shape *fire = InitFireGeometryLevel();
  Shape *view = InitViewGeometryLevel();
  if( geom )
  {
    InitConvexComponents(*_geomComponents,geom,fullReport);
    // for Micro AI we need to reach the ground whenever possible
    ExtendToGround(geom,*_geomComponents);
  }
  if( fire )
  {
    if( fire==geom ) 
      _fireComponents=_geomComponents;
    else 
    {
      InitConvexComponents(*_fireComponents,fire,fullReport);
    }
  }
  if( view )
  {
    if( view==geom ) 
      _viewComponents=_geomComponents;
    else if( view==fire ) 
      _viewComponents=_fireComponents;
    else 
    {
      //bool bphys = _geomComponents && (_geomComponents->Size() > 0) || !usePhys;
      InitConvexComponents(*_viewComponents,view,fullReport);
    }

    if (_canOcclude)
    {
      if( _viewComponents->RecalculateEdges(view, this) )
      {
        RptF
        (
          "Shape %s:%s - bad components",
          (const char *)Name(),LevelName(view)
        );
      }
      InitOcclusion(view,*_viewComponents); // TODO: move to a more suitable spot - binarize preferably
    }
  }

  if( geom )
  {
    _geometryCenter=(geom->Max()+geom->Min())*0.5;
    _geometrySphere=geom->Max().Distance(geom->Min())*0.5;
    _aimingCenter=_geometryCenter;
  }
  else
  {
    _geometrySphere=_boundingSphere;
    _geometryCenter=VZero; // both geometry and aiming center is in the "new" coordinates
    _aimingCenter=VZero;
  }
  // override aiming point if necessary
  const char *aimName="zamerny";
  if( MemoryPointExists(aimName) )
  {
    _aimingCenter=MemoryPoint(aimName);
  }

#if VIEW_INTERNAL_GEOMETRIES
    InitCC(_viewPilotComponents, InitViewPilotGeometryLevel(),fullReport);
    InitCC(_viewGunnerComponents, InitViewGunnerGeometryLevel(),fullReport);
    InitCC(_viewCargoComponents, InitViewCargoGeometryLevel(),fullReport);
#endif

  Shape *shape = InitRoadwayLevel();
  if (shape) shape->InitPlanes();
}

const RStringB &LODShape::PropertyValue(const RStringB &name) const
{
  const Shape *geom = GeometryLevel();
  if( geom )
  {
    // if geometry is present, always use its properties
    const RStringB &value=geom->PropertyValue(name);
    if (value.GetLength()>0) return value;
  }
  return RStringBEmpty;
}

const RStringB &LODShape::PropertyValue( const char *name ) const
{
  const Shape *geom = GeometryLevel();
  if( geom )
  {
    // if geometry is present, always use its properties
    const RStringB &value=geom->PropertyValue(name);
    if (value.GetLength()>0) return value;
  }
  return RStringBEmpty;
}

/*!
\patch_interal 2.01 Date 12/12/2002 by Ondra
- Fixed: Crashes could be caused by models with no lod-levels,
because FindLevel always returned 0 even when no lod was found.
*/


/**Function searches for a LOD with the similar resolution. 
* If required resolution is less than 900 it returns the first LOD with lower resolution as required one or 0,
* if such LOD does not exist.  
* BE WARE! For special LODs it is often better to use function FindSpecLevel.
* @param resolution - resolution of LOD
* @param noDecal - if true function searches only for LODs without decal.
* @return - ID of found LOD
* @see FindSpecLevel
*/
int LODShape::FindLevel( float resolution, bool noDecal ) const
{
  // find first suitable LOD level
  if( resolution<900 )
  {
    // find normal LOD
    Assert( _nLods>=1 );
    int i=1;
    for( ; i<_nLods; i++ )
    {
      float aRes=_resolutions[i];
      if( aRes>resolution ) break; // this one is too rough
      if( aRes>=900 ) break; // memory or special LOD
    }
    i--;
    if( noDecal )
    {
      while
      (
        i>0 && _lods[i-1] && (GET_LOD(_lods[i])->GetAndHints()&ClipDecalMask)!=ClipDecalNone
      )
      {
        i--;
      }
    }
    return i;
  }
  else
  {
    // find special LOD
    float minDiff=1e20;
    int minI=-1;
    for( int i=0; i<_nLods; i++ )
    {
      float diff=fabs(_resolutions[i]-resolution);
      if( minDiff>diff ) minDiff=diff,minI=i;
    }
    return minI;
  }
}

/** Function checks if resolution of LOD matches given resolution. (Must be within 1% boundary).
* @param level - ID of LOD (if it is negative function returns false)
* @param resolution - resolution to verify
* @return - true, if LOD's resolution is similar to resolution. 
*/
bool LODShape::IsSpecLevel( int level, float spec ) const
{
  return
  (
    level>=0 &&
    _resolutions[level]>spec*0.99f && _resolutions[level]<spec*1.01f
  );
}

/** Function searches for a LOD with the similar resolution (maximal difference 10%). 
* Otherwise it returns -1.
* @param resolution - required resolution of LOD
* @return - ID of found LOD or -1
*/
int LODShape::FindSpecLevel( float spec ) const
{
  int level=FindLevel(spec);
  if( level>=0 && _resolutions[level]>spec*0.99f && _resolutions[level]<spec*1.01f )
  {
    return level;
  }
  return -1;
}

bool LODShape::IsShadowVolume(int level) const
{
  if (level>=_shadowVolume && level<_shadowVolume+_shadowVolumeCount) return true;
  if (level<_nGraphical) return false;
  float resolution = Resolution(level);
  if (IsSpec(resolution,SHADOW_VOLUME_CARGO)) return true;
  if (IsSpec(resolution,SHADOW_VOLUME_PILOT)) return true;
  if (IsSpec(resolution,SHADOW_VOLUME_GUNNER)) return true;
  return false;
}

signed char LODShape::FindSpecLevelInterval(signed char &itemsCount, float spec, float specCount) const
{
  signed char level = -1;
  itemsCount = 0;
  for (int i = 0; i < _nLods; i++)
  {
    if (IsSpecInterval(_resolutions[i], spec, specCount))
    {
      if (level == -1)
      {
        level = i;
      }
      itemsCount++;
    }
  }
  return level;
}

#define CHECK_OFFSET(o) ((o)>=BeginFaces() && (o)<=EndFaces())
#define CHECK_FACE_OFFSET(o) ((o)>=BeginFaces() && (o)<EndFaces())

#define ASSERT_OFFSET(o) \
  if (!CHECK_OFFSET(o)) \
  { \
    LogF(#o " invalid"); \
    return false; \
  }

#define ASSERT_FACE_OFFSET(o) \
  if (!CHECK_FACE_OFFSET(o)) \
  { \
    LogF(#o " invalid"); \
    return false; \
  }
  
bool Shape::CheckIntegrity() const
{
  // check if all offsets are valid
  if (_face._data) // check only when faces are loaded
  {
    for (int i=0; (size_t)i<_faceIndexToOffset.Size(); i++)
    {
      ASSERT_FACE_OFFSET(_faceIndexToOffset[i]);
    }
    for (int i=0; i<_face._sections.Size(); i++)
    {
      const ShapeSection &ss = _face._sections[i];
      ASSERT_FACE_OFFSET(ss.beg);
      ASSERT_OFFSET(ss.end);
    }
    if (_face._sections.Size()>0)
    {
      if(_face._sections[_face._sections.Size()-1].end!=EndFaces())
      {
        LogF("Final section offset incorrect");
        return false;
      }
    }
    for (int i=0; i<_sel.Size(); i++)
    {
      const NamedSelection &sel = _sel[i];
      const FaceSelection &faceSel = sel.FaceOffsets(this);

      for (int si=0; si<faceSel.NSections(); si++)
      {
        int s = faceSel.GetSection(si);
        const ShapeSection &ss = GetSection(s);
        ASSERT_FACE_OFFSET(ss.beg);
        ASSERT_OFFSET(ss.end);
      }
    }
  }

  return base::CheckIntegrity();
}

LODShape::LODShape(){DoConstruct();}
LODShape::LODShape(
  const LODShape &src, bool copyAnimations, bool copyLods, bool copySelections, bool copyST
)
{
  DoConstruct(src,copyAnimations,copyLods,copySelections,copyST);
  CopyProperties(src);
}
void LODShape::operator = ( const LODShape &src )
{
  DoDestruct();
  DoConstruct(src,true,true,true,true);
}
LODShape::~LODShape()
{
  if (_name.GetLength()>0)
  {
    Shapes.Remove(ShapeBank::ShapeBankName(_name,_remarks));
  }
  DoDestruct();
}

LODShape::LODShape(const char *name, bool reversed, const ModelTarget &mt)
{
  DoConstruct();
  Load(name, reversed, mt);
}

LODShape::LODShape( const char *name, bool reversed, const char * forceName)
{
  DoConstruct();
  Load(name, reversed, ModelTarget::_defaultValue, forceName);
}

LODShape::LODShape(const char *name, bool reversed, const ModelTarget &mt, const char * forceName)
{
  DoConstruct();
  Load(name, reversed, mt, forceName);
}
LODShape::LODShape(QIStream &f, const char *name, bool reversed, const ModelTarget &mt, SkeletonSource *ss)
{
  DoConstruct();
  Load(f,name,reversed,mt,ss);
}

/// toggle if all shadows should be shadow buffered
static bool AllShadowsSB = false;

bool LODShape::DrawProjShadow(int passNum) const
{
  if (GEngine->IsSBEnabled())
  {
    if (_sbSource == SBS_None)
    {
      return false;
    }
    else
    {
      if (AllShadowsSB)
      {
        return true;
      }
      else
      {
        // for 1st person view we prefer shadow volumes
        if (_shadowVolume>=0 && passNum==3) return false;
        return !_preferShadowVolume;
      }
    }
  }
  else
  {
    return _projShadow;
  }
}

#ifdef TEST_RAIN_MAP
bool LODShape::DrawRainMap(int passNum) const
{
	if (GEngine->IsRainMapEnabled())
	{
		//first person, do we block the rain?
		if (_shadowVolume >= 0 && passNum == 3) 
			return false;

		return true;
	}
	else
	{
		return false;
	}
}
#endif


bool LODShape::DrawVolumeShadow(int passNum) const
{
  if (_shadowVolume < 0)
  {
    return false;
  }
  else
  {
    if (GEngine->IsSBEnabled())
    {
      if (AllShadowsSB)
      {
        return false;
      }
      else
      {
        // for 1st person view we prefer shadow volumes
        if (passNum==3) return true;
        return _preferShadowVolume;
      }
    }
    else
    {
      return true;
    }
  }
}

#if _ENABLE_REPORT

int LODShape::FindNamedSel(const Shape *shape, const char *name, const char *altName) const
{
#if 0
  // no selection reporting for some special LODs
  if (shape!=MemoryLevel() && shape!=PathsLevel())
  {
    if (_usedSelectionNames.AddUnique(name)>=0)
    {
      // report each selection once
      LogF("'%s': using selection '%s'",cc_cast(GetName()),name);
      char shortName[256];
      GetFilename(shortName,shape->GetName());
      while (strpbrk(shortName," -/()"))
      {
        *strpbrk(shortName," -/()")='_';
      }

      RptF("Selection missing in CfgModels");
      RptF("  class %s",shortName);
      RptF("    \"%s\",",cc_cast(namedSel.GetName()));
    }
  }
#endif
  // list of all unused selections
  int ret = shape->FindNamedSel(name,altName);
  if (ret<0)
  {
    // check bad listings in disabled selections list
    int index = _unusedSelectionNames.Find(name);
    if (index<0 && altName) index = _unusedSelectionNames.Find(altName);
    if (index>=0)
    {
      RptF(
        "%s: used selection \"%s\" removed",
        cc_cast(GetName()),cc_cast(_unusedSelectionNames[index])
      );
    }
  }
  return ret;
}
int LODShape::FindNamedSel(const Shape *shape, const char *name) const
{
  // list of all unused selections
  int ret = shape->FindNamedSel(name);
  if (ret<0)
  {
    // check bad listings in disabled selections list
    int index = _unusedSelectionNames.Find(name);
    if (index>=0)
    {
      RptF(
        "%s: used selection \"%s\" removed",
        cc_cast(GetName()),cc_cast(_unusedSelectionNames[index])
      );
    }
  }
  return ret;
}
#endif

int LODShape::PointIndex( const Shape *shape, const char *name ) const
{
  int index=FindNamedSel(shape,name);
  if( index<0 ) return -1;
  const NamedSelection::Access sel(shape->NamedSel(index),this,shape);
  // selection should be one point only
  if( sel.Size()<1 )
  {
    ErrF("No point in selection %s.",name);
    return -1;
  }
  return sel[0];
}

Vector3 LODShape::NamedPoint( int level, const char *name, const char *altName ) const
{
  ShapeUsed shape=Level(level);

  int pIndex=PointIndex(shape,name);
  if( pIndex>=0 )
  {
    ShapeGeometryLock<> lock(this,level);
    return shape->Pos(pIndex);
  }
  if( altName )
  {
    int pIndex=PointIndex(shape,altName);
    if( pIndex>=0 )
    {
      ShapeGeometryLock<> lock(this,level);
      return shape->Pos(pIndex);
    }
  }
  return VZero;
}

Vector3 LODShape::MemoryPoint( const char *name, const char *altName ) const
{
  int memory = FindMemoryLevel();
  if( memory<=0 ) return VZero;
  return NamedPoint(memory,name,altName);
}

bool LODShape::MemoryPointExists( const char *name ) const
{
  const Shape *memory=MemoryLevel();
  if( !memory ) return false;
  int pIndex=PointIndex(memory,name);
  return pIndex>=0;
}

bool LODShape::IsInside(Vector3Par pos)
{
  // Called only from EntityAIType::InitShape, so using Shape directly is OK here
  Shape *geom = InitGeometryLevel();

  // make sure there is well defined geometry LOD
  if (_geomComponents->Size()<=0) return false;

  geom->RecalculateNormalsAsNeeded();
  _geomComponents->RecalculateAsNeeded(geom);

  // all calculation will be performed in model space
  for (int iThis=0; iThis<_geomComponents->Size(); iThis++)
  {
    const ConvexComponent &cThis = *(*_geomComponents)[iThis];
    // check intersection will all convex components
    if (cThis.IsInside(geom->GetPlanesArray(), pos))
    {
      return true;
    }
  }
  return false;
}

int EngineShapeProperties::GetEngineShapeProperties() const
{
  // by default there are no special properties
  return 0;
}

void EngineShapeProperties::GetTreeCrown(Vector3 *minMax) const
{
  Fail("Not a tree - cannot get tree crown property");
  // by default no properties are supported
}

void LODShape::CalculateTreeCrown()
{
  _treeCrown = new TreeCrownInfo;
  int viewGeom = FindViewGeometryLevel();
  if (viewGeom<0)
  {
    _treeCrown->_minmax[0] = _minMax[0];
    _treeCrown->_minmax[1] = _minMax[1];
  }
  else
  {
    const Shape *shape = ViewGeometryLevel();
    
    const ConvexComponents *cc = _viewComponents;
    bool found = false;
    _treeCrown->_minmax[0] = Vector3(+1e10,+1e10,+1e10);
    _treeCrown->_minmax[1] = Vector3(-1e10,-1e10,-1e10);
    for (int i=0; i<cc->Size(); i++)
    {
      const ConvexComponent *ci = cc->Get(i);
      if (ci->GetSurfaceInfo())
      {
        // if there is any surface info, assume tree crown
        found = true;
        SaturateMin(_treeCrown->_minmax[0],ci->Min());
        SaturateMax(_treeCrown->_minmax[1],ci->Max());
      }
      
    }
    if (!found)
    {
      _treeCrown->_minmax[0] = shape->MinMax()[0];
      _treeCrown->_minmax[1] = shape->MinMax()[1];
    }
  }
  
}

void LODShape::GetTreeCrown(Vector3 *minMax) const
{
  if (_treeCrown)
  {
    minMax[0] = _treeCrown->_minmax[0];
    minMax[1] = _treeCrown->_minmax[1];
  }
  else
  {
    Fail("Tree crown?");
    unconst_cast(this)->CalculateTreeCrown();
  }
}

void LODShape::DisableLevelStreaming(int level, bool enableGeometryStreaming)
{
  ShapeRef *shapeRef = _lods[level].GetShapeRef();
  ShapeRefManaged *managed = shapeRef->GetManagedInterface();
  // if not managed, streaming is already disabled
  if (managed)
  {
    // make sure shape is loaded
    managed->Lock();

    Ref<Shape> shape = shapeRef->GetRef();

    // we have reference in the shape - release the managed ref
    managed->Unlock();
    if (!enableGeometryStreaming)
    {
      shape->DisableGeometryStreaming(managed);
      Shapes.ShapeGeometryReleased(shape);
    }
    // remove managed from the managed list
    Shapes.ShapeReleased(managed);

    _lods[level] = NULL;
    _lods[level] = new ShapeRefPermanent(shape);
  }
  else
  {
    if (!enableGeometryStreaming)
    {
      Shape *shape = shapeRef->GetRef();
      shape->DisableGeometryStreaming(shapeRef);
      Shapes.ShapeGeometryReleased(shape);
    }
  }
}

/**
Disable streaming for Shape (ShapeUsed) only or for geometry as well
*/
void LODShape::DisableStreaming(bool enableGeometryStreaming)
{
  if (!_streaming) return;
  // convert all shapes to permanents ones
  for (int i=0; i<_nLods; i++)
  {
    DisableLevelStreaming(i,enableGeometryStreaming);
  }
  _streaming = false;
}

/*
void LODShape::CreateShadowGeometry(const Shape &source)
{
  // Clear the previously set geometry
  //_shadowGeometry->Clear();
  _shadowGeometry = new Shape;

  // Go through all the source polygons
  for (Offset srcOffset = source.BeginFaces(); srcOffset < source.EndFaces(); source.NextFace(srcOffset))
  {
    const Poly &srcPoly = source.Face(srcOffset);
    if (srcPoly.N() < 3) continue;

    // Get the normal of the source face
    Vector3 srcPolyNormal = (source.Pos(srcPoly.GetVertex(1)) - source.Pos(srcPoly.GetVertex(0))).
      CrossProduct(source.Pos(srcPoly.GetVertex(2)) - source.Pos(srcPoly.GetVertex(0))).Normalized();

    // Add new polygon and the vertices of the polygon to the destination
    Poly newPoly;
    newPoly.SetN(srcPoly.N());
    for (int i = 0; i < srcPoly.N(); i++)
    {
      int vertexIndex = _shadowGeometry->AddVertex(
        source.Pos(srcPoly.GetVertex(i)),
        srcPolyNormal,
        ClipNone, 0.0f, 0.0f);
      newPoly.Set(i, vertexIndex);
    }
    _shadowGeometry->AddFace(newPoly);

    // Go through all the source polygon edges
    for (int srcVertex = 1; srcVertex < srcPoly.N(); srcVertex++)
    {
      // Get the edge
      VertexIndex a = srcPoly.GetVertex(srcVertex - 1);
      VertexIndex b = srcPoly.GetVertex(srcVertex);

      // Get the opposite edge
      
      // Go through all the opposite polygons
      bool edgeFound = false;
      for (Offset oppositeOffset = source.BeginFaces(); oppositeOffset < source.EndFaces(); source.NextFace(oppositeOffset))
      {
        const Poly &oppositePoly = source.Face(oppositeOffset);
        if (oppositePoly.N() < 3) continue;

        // Get the normal of the opposite face
        Vector3 oppositePolyNormal = (source.Pos(oppositePoly.GetVertex(1)) - source.Pos(oppositePoly.GetVertex(0))).
          CrossProduct(source.Pos(oppositePoly.GetVertex(2)) - source.Pos(oppositePoly.GetVertex(0))).Normalized();

        // Go through all the opposite polygon edges
        for (int oppositeVertex = 1; oppositeVertex < oppositePoly.N(); oppositeVertex++)
        {
          // Get the edge
          VertexIndex aa = oppositePoly.GetVertex(oppositeVertex - 1);
          VertexIndex bb = oppositePoly.GetVertex(oppositeVertex);

          // If we found the opposite edge
          if ((source.Pos(aa) == source.Pos(b)) && (source.Pos(bb) == source.Pos(a)))
          {
            // Add degenerated triangle a, bb, b
            newPoly.SetN(3);
            newPoly.Set(0, _shadowGeometry->AddVertex(source.Pos(a),  srcPolyNormal,      ClipNone, 0.0f, 0.0f));
            newPoly.Set(1, _shadowGeometry->AddVertex(source.Pos(bb), oppositePolyNormal, ClipNone, 0.0f, 0.0f));
            newPoly.Set(2, _shadowGeometry->AddVertex(source.Pos(b),  srcPolyNormal,      ClipNone, 0.0f, 0.0f));
            _shadowGeometry->AddFace(newPoly);

            // Mark we found the edge, finish the cycle
            edgeFound = true;
            break;
          }
        }

        // Break the cycle if we found edge already
        if (edgeFound) break;
      }

      if (!edgeFound)
      {
        LogF("Opposite edge was not found - shadow geometry is not closed.");
      }
    }
  }
}
*/

struct STriangle
{
  bool _edges[3];
  STriangle() {_edges[0] = _edges[1] = _edges[2] = false;}
};
TypeIsBinary(STriangle)

typedef AutoArray<STriangle> TriangleArray;

struct DegenReference
{
  Offset _offset[3];
  int _index[3];
  int _count;
  DegenReference() {_count = 0;}
};
TypeIsBinary(DegenReference)

struct NewFaceIndex
{
  int _index;
  int _count;
  NewFaceIndex() {_count = 0;}
};
TypeIsBinary(NewFaceIndex)

void Shape::UpdateToShadowGeometry(const LODShape *lodShape, bool separateFaces, const char *errName)
{
  // Detect selection which needs a section (we cannot work with that currently)
  for (int i = 0; i < NNamedSel(); i++)
  {
    const NamedSelection &sel = NamedSel(i);
    if (sel.GetNeedsSections())
    {
      RptF("Warning: %s:%s, Selection %s needs a section. The shadow volume may not work well.", errName,lodShape->LevelName(this), sel.Name());
    }
  }

  if (NUVStages() != 1)
  {
    RptF("Warning: %s: Shadow volume LOD contains 2nd UV set.", errName);
    // remove the bogus UV set
    _data->_texCoord[0].Clear();
  }

  int originalNFaces = NFaces();
  if (originalNFaces>0) // no faces means _face._data is NULL - would crash
  {
  // Separate the face vertices
  if (separateFaces)
  {
    SeparateFaceVertices();
  }

  // List of triangles
  TriangleArray triangle;
  triangle.Realloc(NFaces());
  triangle.Resize(NFaces());
  AutoArray<DegenReference> degenerate;
  degenerate.Realloc(NFaces());
  degenerate.Resize(NFaces());

  // Go through all the faces
  Offset originalEndFaces = EndFaces();
  Offset offset;
  int index;
  for (offset = BeginFaces(), index = 0; offset < originalEndFaces; NextFace(offset), index++)
  {

    // Skip faces not designed for drawing
    int special = _faceProperties[index].Special();
    if (special & (IsHidden|IsHiddenProxy|NoShadow))
    {
      if ((special & NoShadow) && !(special & IsHiddenProxy))
      {
        RptF("Warning: %s: Some non proxy polygons doesn't have the 'Enable shadow' checked. The geometry might not be closed therefore.",
          errName);
      }
      continue;
    }

    // Because we add new faces, we must not use references
    //const Poly &poly = Face(offset);

    // Detect the face doesn't have 3 vertices
    if (Face(offset).N() != 3)
    {
      RptF(
        "Error: %s: Shadow polygon doesn't have 3 vertices - it has not been considered.",
        errName
        );
      continue;
    }

    // Get the reference to the reference to the degenerated triangle
    DegenReference &drSource = degenerate[index];

    // Remember the source normal
    Vector3 normal = Norm(Face(offset).GetVertex(0)).Get();

    // Detect the face to be degenerated one
    if ((Pos(Face(offset).GetVertex(0)) == Pos(Face(offset).GetVertex(1))) ||
      (Pos(Face(offset).GetVertex(1)) == Pos(Face(offset).GetVertex(2))) ||
      (Pos(Face(offset).GetVertex(2)) == Pos(Face(offset).GetVertex(0))))
    {
      RptF("Warning: %s: Degenerated triangle found on source triangles (%d,%d,%d)",
        errName, VertexToPoint(Face(offset).GetVertex(0)), VertexToPoint(Face(offset).GetVertex(1)), VertexToPoint(Face(offset).GetVertex(2))
        );
    }

    // Go through all the polygon edges
    for (int vertex = 0; vertex < 3; vertex++)
    {

      // If the edge was considered already, then skip it
      if (triangle[index]._edges[vertex]) continue;

      // Get the edge
      VertexIndex a;
      if (vertex > 0)
      {
        a = Face(offset).GetVertex(vertex - 1);
      }
      else
      {
        a = Face(offset).GetVertex(Face(offset).N() - 1);
      }
      VertexIndex b = Face(offset).GetVertex(vertex);

      // Get the opposite edge

      // Remember the best values
      float maxNormalDot = -FLT_MAX; // Initialize it with the worst case: -FLT_MAX
      Poly bestPolygonSource;
      bestPolygonSource.SetN(3);
      Poly bestPolygonDest;
      bestPolygonDest.SetN(3);
      int bestOppositeIndex = -1;
      int bestOppositeVertex = -1;

      // Go through all the sections
      bool edgeFound = false;

      // Go through all the opposite polygons in the opposite section
      Offset oppositeOffset;
      int oppositeIndex;
      for (oppositeOffset = BeginFaces(), oppositeIndex = 0; oppositeOffset < originalEndFaces; NextFace(oppositeOffset), oppositeIndex++)
      {

        // Skip faces not designed for drawing
        int special = _faceProperties[oppositeIndex].Special();
        if (special & (IsHidden|IsHiddenProxy|NoShadow))
        {
          if ((special & NoShadow) && !(special & IsHiddenProxy))
          {
            RptF("Warning: %s: Some non proxy polygons doesn't have the 'Enable shadow' checked. The geometry might not be closed therefore.",
              errName);
          }
          continue;
        }

        // Because we add new faces, we must not use references
        //const Poly &oppositePoly = Face(oppositeOffset);
        if (Face(oppositeOffset).N() != 3)
        {
          RptF(
            "Error: %s: Shadow polygon doesn't have 3 vertices - it has not been considered.",
            errName
            );
          continue;
        }

        // Remember the opposite normal
        Vector3 oppositeNormal = Norm(Face(oppositeOffset).GetVertex(0)).Get();

        // Go through all the opposite polygon edges
        for (int oppositeVertex = 0; oppositeVertex < 3; oppositeVertex++)
        {

          // If the edge was considered already, then skip it
          // (To imagine this situation consider 2 double-sided triangles sharing one edge)
          if (triangle[oppositeIndex]._edges[oppositeVertex]) continue;

          // Get the edge
          VertexIndex aa;
          if (oppositeVertex > 0)
          {
            aa = Face(oppositeOffset).GetVertex(oppositeVertex - 1);
          }
          else
          {
            aa = Face(oppositeOffset).GetVertex(Face(oppositeOffset).N() - 1);
          }
          VertexIndex bb = Face(oppositeOffset).GetVertex(oppositeVertex);

          // If we found the opposite edge
          Assert(VertexToPoint(a) >= 0);
          Assert(VertexToPoint(b) >= 0);
          Assert(VertexToPoint(aa) >= 0);
          Assert(VertexToPoint(bb) >= 0);
          if ((VertexToPoint(aa) == VertexToPoint(b)) && (VertexToPoint(bb) == VertexToPoint(a)))
          {

            // Calculate dot product and check if it's greater than we have
            // (note that we're trying to find a polygon in the same direction (if possible))
            float dot = normal.DotProduct(oppositeNormal);
            if (dot > maxNormalDot)
            {
              // Remember the best polygon ...
              bestPolygonSource.Set(0, a );
              bestPolygonSource.Set(1, bb);
              bestPolygonSource.Set(2, b);
              bestPolygonDest.Set(0, aa );
              bestPolygonDest.Set(1, b);
              bestPolygonDest.Set(2, bb);

              // ... and the best opposite index
              bestOppositeIndex = oppositeIndex;

              // ... and the best opposite vertex
              bestOppositeVertex = oppositeVertex;

              // Remember dot as maximal
              maxNormalDot = dot;

              // Mark we found an edge
              edgeFound = true;
            }
          }
        }
      }

      // The edge should be found, so add the best polygon
      if (edgeFound)
      {

        // Source edge
        {
          // Mark we use the edge
          triangle[index]._edges[vertex] = true;

          // Remember the index and offset to degenerated faces for the original face
          drSource._offset[drSource._count] = _face.End();
          drSource._index[drSource._count] = _faceProperties.Size();
          drSource._count++;

          // Add degenerated triangle a, bb, b
          _face.Add(bestPolygonSource); //AddFace(newPoly);

          // Add item to face properties
          PolyProperties pp;
          pp.Init();
          _faceProperties.Add(pp);
        }

        // Dest edge
        {
          // Mark we use the edge
          triangle[bestOppositeIndex]._edges[bestOppositeVertex] = true;

          // Remember the index and offset to degenerated faces for the original face
          DegenReference &drDest = degenerate[bestOppositeIndex];
          drDest._offset[drDest._count] = _face.End();
          drDest._index[drDest._count] = _faceProperties.Size();
          drDest._count++;

          // Add degenerated triangle a, bb, b
          _face.Add(bestPolygonDest); //AddFace(newPoly);

          // Add item to face properties
          PolyProperties pp;
          pp.Init();
          _faceProperties.Add(pp);
        }
      }
      else
      {
        if (NPoints()>0)
        {
          RptF(
            "Error: %s: Opposite edge to P(%d, %d) was not found - shadow geometry is not closed.",
            errName, VertexToPoint(a), VertexToPoint(b)
            );
        }
        else
        {
          RptF(
            "Error: %s: Opposite edge to V(%d, %d) was not found - shadow geometry is not closed.",
            errName, a, b
            );
        }
      }
    }
  }

  // Insert degenerated faces right after the source triangles (vertex buffer performance optimization (cache))
  FaceArray newFaceArray;
  newFaceArray.Reserve(originalNFaces);
  AutoArray<PolyProperties> newFaceProperties;
  AutoArray<NewFaceIndex> newFaceIndices; // Array stores relation between old and new face array
  newFaceIndices.Realloc(originalNFaces);
  newFaceIndices.Resize(originalNFaces);
  for (offset = BeginFaces(), index = 0; offset < originalEndFaces; NextFace(offset), index++)
  {
    // Add the source triangle
    newFaceArray.Add(_face[offset]);
    int newIndex = newFaceProperties.Add(_faceProperties[index]);
    newFaceIndices[index]._index = newIndex;
    newFaceIndices[index]._count++;

    // Add the degenerated triangles
    if (index < degenerate.Size())
    {
      for (int j = 0; j < degenerate[index]._count; j++)
      {
        newFaceArray.Add(_face[degenerate[index]._offset[j]]);
        newFaceProperties.Add(_faceProperties[degenerate[index]._index[j]]);
        newFaceIndices[index]._count++;
      }
    }
  }
    _face._data = new FaceData;
  _face._data->_faces.Move(newFaceArray._data->_faces);
  _faceProperties = newFaceProperties;

  // Update face selections to refer the new face array
  for (int i = 0; i < NNamedSel(); i++)
  {
    NamedSelection::Access ns = NamedSel(i,lodShape);
    Selection &sFaces = ns.SetFaces();
    Selection newSFaces;
    for (int s = 0; s < sFaces.Size(); s++)
    {
      NewFaceIndex &nfi = newFaceIndices[sFaces[s]];
      for (int j = 0; j < nfi._count; j++)
      {
        newSFaces.Add(nfi._index + j);
      }
    }
    sFaces = newSFaces;
  }
  }
}

void Shape::GenerateSTArray(const LODShape *lodShape, int level)
{
  _data->_st.Realloc(NPos());
  _data->_st.Resize(NPos());
  AutoArray<Vector3Compressed> norm;
  norm.Realloc(_data->_norm.Size());
  norm.Resize(_data->_norm.Size());
  // make sure each entry is initialized (important for isolated points)
  STPair stEmpty(VForward,VAside);
  for (int i=0; i<_data->_st.Size(); i++)
  {
    _data->_st[i] = stEmpty;
  }
  // make sure each entry is initialized (important for isolated points)
  for (int i=0; i<norm.Size(); i++)
  {
    norm[i] = Vector3Compressed::ZeroV;
  }

  // Go through all the sections
  for (int s = 0; s < NSections(); s++)
  {
    const ShapeSection &sec = GetSection(s);

    // Skip proxy sections
    if (sec.Special() & IsHiddenProxy) continue;

    // Get the texture bit precision
    int bppS = 8;
    int bppT = 8;
    PacFormat pacFormat = PacUnknown;
    TexMaterial *material = sec.GetMaterialExt();
    if (material)
    {
      RefR<Texture> tex = material->_stage[1]._tex;
      if (tex.NotNull())
      {
        tex->LoadHeaders();
        pacFormat = tex->GetFormat();
      }
      else
      {
//        RptF("Warning: %s:%s Stage 1 has no texture to retrieve format from (while generating ST coordinates)",
//          lodShape->Name(),
//          lodShape->LevelName(lodShape->Resolution(level)));
      }
    }
    else
    {
//      RptF("Warning: %s:%s Section without material is in shape with ST coordinates",
//        lodShape->Name(),
//        lodShape->LevelName(lodShape->Resolution(level)));
    }

    switch (pacFormat)
    {
    case PacRGB565:
    case PacDXT1: case PacDXT2: case PacDXT3:
      bppS = 5;
      bppT = 6;
      break;
    case PacDXT4: case PacDXT5: // DXT4/5 contains 8b encoded alpha used to hold S
      bppS = 8;
      bppT = 6;
      break;
    case PacARGB1555:
      bppS = 5;
      bppT = 5;
      break;
    case PacARGB4444:
      bppS = 4;
      bppT = 4;
      break;
    }

    // Go through all the section faces
    for (Offset o = sec.beg; o < sec.end; NextFace(o))
    {
      const Poly &poly = Face(o);
      if (poly.N() < 3) continue;

      // Detect and skip degenerated face
      if (poly.GetVertex(0) == poly.GetVertex(poly.N() - 1)) continue;
      bool degenerated = false;
      for (int i = 1; i < poly.N(); i++)
      {
        if (poly.GetVertex(i - 1) == poly.GetVertex(i))
        {
          degenerated = true;
          break;
        }
      }
      if (degenerated) continue;

      // Count ST coordinates for vertices of this face
      for (int i = 2; i < poly.N(); i++)
      {
        // Get triangle vertices

        VertexIndex v0 = poly.GetVertex(0);
        VertexIndex v1 = poly.GetVertex(i - 1);
        VertexIndex v2 = poly.GetVertex(i);
        UVPair aUV, bUV, cUV;
        const Vector3 *aPos, *bPos, *cPos;
        //Vector3 s, t;
        bool generationFailed = false;

        // A vertex
        aUV = UV(v0);
        bUV = UV(v1);
        cUV = UV(v2);
        aPos = &Pos(v0);
        bPos = &Pos(v1);
        cPos = &Pos(v2);
        norm[v0] = Norm(v0);
        if (!GenerateST(norm[v0], *aPos, *bPos, *cPos, aUV, bUV, cUV, _data->_st[v0].s, _data->_st[v0].t, bppS, bppT))
        {
          generationFailed = true;
        }
//        if (lastSection[v0] != s)
//        {
//          passCount[v0]++;
//          lastSection[v0] = s;
//        }

        // B vertex
        aUV = UV(v1);
        bUV = UV(v2);
        cUV = UV(v0);
        aPos = &Pos(v1);
        bPos = &Pos(v2);
        cPos = &Pos(v0);
        norm[v1] = Norm(v1);
        if (!GenerateST(norm[v1], *aPos, *bPos, *cPos, aUV, bUV, cUV, _data->_st[v1].s, _data->_st[v1].t, bppS, bppT))
        {
          generationFailed = true;
        }
//        if (lastSection[v1] != s)
//        {
//          passCount[v1]++;
//          lastSection[v1] = s;
//        }

        // C vertex
        aUV = UV(v2);
        bUV = UV(v0);
        cUV = UV(v1);
        aPos = &Pos(v2);
        bPos = &Pos(v0);
        cPos = &Pos(v1);
        norm[v2] = Norm(v2);
        if (!GenerateST(norm[v2], *aPos, *bPos, *cPos, aUV, bUV, cUV, _data->_st[v2].s, _data->_st[v2].t, bppS, bppT))
        {
          generationFailed = true;
        }
//        if (lastSection[v2] != s)
//        {
//          passCount[v2]++;
//          lastSection[v2] = s;
//        }

        // Report error if occurred
        if (generationFailed)
        {
          if (ExplicitPoints())
          {
            RptF(
              "Warning: %s:%s Error while trying to generate ST for points: %d, %d, %d",
              lodShape->Name(),
              lodShape->LevelName(lodShape->Resolution(level)),
              VertexToPoint(poly.GetVertex(0)),
              VertexToPoint(poly.GetVertex(i-1)),
              VertexToPoint(poly.GetVertex(i))
              );
          }
          else
          {
            RptF(
              "Warning: %s:%s Error while trying to generate ST for vertices: %d, %d, %d",
              lodShape->Name(),
              lodShape->LevelName(lodShape->Resolution(level)),
              poly.GetVertex(0),
              poly.GetVertex(i-1),
              poly.GetVertex(i)
              );
          }
        }
      }
    }
  }

  // Copy modified normals back to normal array
  _data->_norm.Expand();
  for (int i = 0; i < NPos(); i++)
  {
    SetNorm(i) = norm[i];
  }
  _data->_norm.Condense();
}

void LODShape::DisableShadow()
{
  _special |= NoShadow;
}

void LODShape::OrSpecial( int special )
{
  DisableStreaming();
  _special|=special;
  for( int i=0; i<_nLods; i++ ) if( _lods[i] ) GET_LOD(_lods[i])->OrSpecial(special);
}
void LODShape::AndSpecial( int special )
{
  DisableStreaming();
  _special&=special;
  for( int i=0; i<_nLods; i++ ) if( _lods[i] ) GET_LOD(_lods[i])->AndSpecial(special);
}
void LODShape::SetSpecial( int special )
{
  DisableStreaming();
  _special=special;
  for( int i=0; i<_nLods; i++ ) if( _lods[i] ) GET_LOD(_lods[i])->SetSpecial(special);
}
void LODShape::RescanSpecial()
{
  _special=0;
  for( int i=0; i<_nLods; i++ )
  {
    if( _resolutions[i]>900 ) continue;
    if( _lods[i] ) _special|=GET_LOD(_lods[i])->Special();
  }
}

void LODShape::DisablePushBuffer()
{
  DisableStreaming();
  for( int i=0; i<_nLods; i++ ) if( _lods[i] ) GET_LOD(_lods[i])->DisablePushBuffer();
}

/*
void LODShape::SetTexturePerformance( int performance)
{
  // set for all primary textures and for all textures referenced by materials as well
  DoAssert(performance==100 || performance==200 || performance==300);
  for (int i=0; i<NLevels(); i++)
  {
    Shape *level = GetLevelLocked(i);
    if (level) level->SetTexturePerformance(performance);
    LevelRef(i).
    if (performance==100)
    {
      SetPost
    }
  }
}
*/


DEF_RSB(shadow);

void LODShape::ScanResolutions()
{
  _geometry=-1;
  _geometryFire=-1;
  _geometryView=-1;

  _geometryViewPilot = -1;
  _geometryViewGunner = -1;
  _geometryViewCargo = -1;

  _shadowVolumePilot = -1;
  _shadowVolumeGunner = -1;
  _shadowVolumeCargo = -1;

  _memory=-1;
  _landContact=-1;
  _roadway=-1;
  _hitpoints=-1;
  _wreck = -1;
  _shadowVolume = -1;
  _shadowBuffer = -1;
  _shadowBufferCount = 0;
  _shadowVolumeCount = 0;
  _paths=-1;
  _nGraphical = _nLods;
  for( int i=0; i<_nLods; i++ )
  {
    float resolution=_resolutions[i];
    if( resolution>900 )
    {
      if (_nGraphical>i) _nGraphical = i;
      if( IsSpec(resolution,GEOMETRY_SPEC) ) _geometry=i;
      else if( IsSpec(resolution,MEMORY_SPEC) ) _memory=i;
      else if( IsSpec(resolution,LANDCONTACT_SPEC) ) _landContact=i;
      else if( IsSpec(resolution,ROADWAY_SPEC) ) _roadway=i;
      else if( IsSpec(resolution,PATHS_SPEC) ) _paths=i;
      else if( IsSpec(resolution,HITPOINTS_SPEC) ) _hitpoints=i;
      else if( IsSpecInterval(resolution,SHADOWVOLUME_SPEC,SHADOWVOLUME_MAX) )
      {
        // Note we rely here on the LODs sorted according to resolutions
        if (_shadowVolume<0) _shadowVolume = i;
        _shadowVolumeCount++;
      }
      else if( IsSpecInterval(resolution,SHADOWBUFFER_SPEC,SHADOWBUFFER_MAX) )
      {
        // Note we rely here on the LODs  sorted according to resolutions
        if (_shadowBuffer<0) _shadowBuffer = i;
        _shadowBufferCount++;
      }
      else if( IsSpec(resolution,VIEW_GEOM_SPEC) ) _geometryView=i;
      else if( IsSpec(resolution,FIRE_GEOM_SPEC) ) _geometryFire=i;
      else if( IsSpec(resolution,VIEW_PILOT_GEOM_SPEC) ) _geometryViewPilot=i;
      else if( IsSpec(resolution,VIEW_GUNNER_GEOM_SPEC) ) _geometryViewGunner=i;
      else if( IsSpec(resolution,VIEW_CARGO_GEOM_SPEC) ) _geometryViewCargo=i;
      else if( IsSpec(resolution,WRECK_SPEC) ) _wreck=i;
      else if( resolution<2000 ) // cockpit LOD
      {
      }
      else if( resolution<10000 ) // spec cockpit LOD
      {
      }
      else if (IsSpec(resolution,SHADOW_VOLUME_CARGO)) { _shadowVolumeCargo = i; }
      else if (IsSpec(resolution,SHADOW_VOLUME_PILOT)) { _shadowVolumePilot = i; }
      else if (IsSpec(resolution,SHADOW_VOLUME_GUNNER)) { _shadowVolumeGunner = i; }
      else
      {
        LogF("%s: Unknown spec lod (%g)",(const char *)Name(),resolution);
      }
    }
  }
}

void LODShape::ScanProjShadow()
{
  _projShadow = false;
  if (!(_special & OnSurface)) // No models with OnSurface flag are allowed to use the projected shadows
  {
    if (_minShadow<_nGraphical)
    {
      if (_shadowVolume<0)
      {
        _projShadow = true;
      }
      else if (_geometry>=0)
      {
        // we need to check property
        RStringB shadow = PropertyValue(RSB(shadow));
        if (shadow==RSB(hybrid))
        {
          _projShadow = true;
        }
      }
    }
  }
}

void LODShape::ScanShapes()
{
  ScanResolutions();
  ScanProjShadow();
  if (_geometry>=0)
  {
    // geometry is made alpha transparent
    Shape *shape = GET_LOD(_lods[_geometry]);
    if (shape) shape->OrSpecial(IsAlpha|IsAlphaFog|IsColored);
  }
  if (_geometryView>=0)
  {
    // geometry is made alpha transparent
    Shape *shape = GET_LOD(_lods[_geometryView]);
    if (shape) shape->OrSpecial(IsAlpha|IsAlphaFog|IsColored);
  }
  if (_geometryFire>=0)
  {
    // geometry is made alpha transparent
    Shape *shape = GET_LOD(_lods[_geometryFire]);
    if (shape) shape->OrSpecial(IsAlpha|IsAlphaFog|IsColored);
  }
  if( _geometryView<0 ) _geometryView=_geometry;
  if( _geometryFire<0 ) _geometryFire=_geometryView;
  if (_geometry>=0)
  {
    _lods[_geometry].Lock();
    const char *fireGeom = GET_LOD(_lods[_geometry])->PropertyValue("firegeometry");
    if (atoi(fireGeom)>0)
    {
      _geometryFire = _geometry;
    }
    const char *viewGeom = GET_LOD(_lods[_geometry])->PropertyValue("viewgeometry");
    if (atoi(viewGeom)>0)
    {
      _geometryView = _geometry;
    }
    _lods[_geometry].Unlock();
  }
}

int LODShape::AddShape( Shape *shape, float resolution )
{
  // added to the end of the LOD list
  Assert( _nLods<MAX_LOD_LEVELS );
  int ret = _nLods;
  _lods[_nLods].SetPermanentRef(shape);
  _resolutions[_nLods]=resolution;
  //shape->SetLevel(_nLods);
  _nLods++;
  ScanShapes();
  return ret;
}

void LODShape::ChangeShape( int level, Shape *shape )
{
  Assert( level<_nLods );
  _lods[level].SetPermanentRef(shape);
  //shape->SetLevel(_nLods);
  //ScanShapes();
}

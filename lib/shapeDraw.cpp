// Poseidon - shape management
// (C) 1998, SUMA

#include "wpch.hpp"
#include "Shape/shape.hpp"
#include "engine.hpp"
#include "rtAnimation.hpp"
#include "global.hpp"
#include "scene.hpp"
#include <El/QStream/qbStream.hpp>
#include <El/ParamFile/paramFile.hpp>
#include "textbank.hpp"
#include "keyInput.hpp"
#include "dikCodes.h"
#include "animation.hpp"
#include "Shape/material.hpp"
#include "tlVertex.hpp"
#include "camera.hpp"
#include "timeManager.hpp"

#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>

#include "Shape/edges.hpp"
#include "Shape/specLods.hpp"

#include <Es/Algorithms/qsort.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/ReportStack/reportStack.hpp>

//#include <malloc.h>

#include "Shape/data3d.h"

#include "Shape/mapTypes.hpp"

#include "progress.hpp"

#ifdef _MSC_VER
  #pragma warning( disable: 4355 )
#endif


#if _ENABLE_PERFLOG
  bool LogStatesOnce = false;
#endif

#define LOG_PROGRAM 0

/*!
\patch 1.33 Date 11/27/2001 by Ondra
 - Fixed: Dark notepad after load or Alt-Tab.
\patch 1.46 Date 3/1/2002 by Ondra
- Fixed: Bug in HW T&L code: Material change was sometimes not recognized
when polygons shared texture. This caused some trees were rendered darker.
*/

bool Shape::DrawUsingTL(int spec, ClipFlags clip) const
{
  if (spec&OnSurface)
  {
    if ((GetAndHints()&ClipLandMask)!=ClipLandOn)
    {
      return false;
    }
  }
  if (clip&ClipUser0) return false;

  Engine *engine=GEngine;

  return engine->GetTL();
}

void Shape::DrawSections(int cb, const AnimationContext *animContext, ColorVal animColor, const EngineShapeProperties &prop, int spec, int bias, const DrawParameters &dp) const
{
  // Get the beg and end section to draw
  int beg;
  int end;
  if (prop._endSection > 0)
  {
    beg = prop._begSection;
    end = prop._endSection;
  }
  else
  {
    beg = 0;
    end = NSections();
  }

  // Go through the sections and draw them
  // there was section merging working here - it cannot work
  // as we cannot access FaceArray _face here (it can be discarded)
  for (int i = beg; i < end; i++)
  {
    // const ShapeSection &sec = GetSection(i);
    const PolyProperties &pp = animContext ? animContext->GetSection(this, i) : GetSection(i);

    // If we render craters and the pixel shader is not designed for drawing craters (f.i. tree crowns), then go to the next section
    if (dp._cratersCount > 0)
    {
      // Tree crown is determined by backface drawing and special pixel shader
      if (pp.Special()&NoBackfaceCull)
      {
        const TexMaterial *mat = pp.GetMaterialExt();
        if (mat)
        {
          PixelShaderID ps = mat->GetPixelShaderID();
          switch (ps)
          {
          case PSNormalMapThrough:
          case PSNormalMapThroughSimple:
          case PSNormalMapSpecularThrough:
          case PSNormalMapSpecularThroughSimple:
          case PSTree:
          case PSTreeSN:
          case PSTreePRT:
          case PSTreeAdv:
          case PSTreeAdvSimple:
          case PSTreeAdvTrunk:
          case PSTreeAdvTrunkSimple:
            continue;
          }
        }
      }
    }

    // Proxy or hidden - don't draw
    if (pp.Special() & (IsHidden | IsHiddenProxy)) continue;

    // Calculate the bias for section
    int secBias = ((pp.Special() & ZBiasMask) / ZBiasStep) * 5;
    int maxBias = intMax(bias, secBias);

    // Get the material
    TLMaterial mat;
    CreateMaterial(mat,animColor);

    // Prepare and draw section
    bool uvchanged = false;
    if (animContext) uvchanged = animContext->GetUVchanged(this, i); 
    if (uvchanged)
    {
      // get information about UV offset if any and pass them to EngineDD9::PrepareDetailTex(...)
      float offsetU = animContext->GetUOffset(this, i);
      float offsetV = animContext->GetVOffset(this, i);
      pp.PrepareTL(cb, mat, spec, prop, dp, true ,offsetU, offsetV);
    }
    else
      pp.PrepareTL(cb, mat, spec, prop, dp);

    GEngine->DrawSectionTL(cb,*this, i, i+1, maxBias, 0, 0, dp);
  }
}

void Shape::Draw(
  int cb, AnimationContext *animContext,
  ColorVal animColor,
  const EngineShapeProperties &prop, const LightList &lights,
  ClipFlags clip, int spec, const PositionRender &transform, float minDist2,
  const DrawParameters &dp
) const
{
  #ifndef ACCESS_ONLY
  // if engine has T&L interface, use it
  // cannot use T&L on some surface types (OnSurface?)
  //Assert((clip&ClipUser0)==0);
  if (NSections() <= 0) return;

  if (DrawUsingTL(spec,clip) && _buffer.NotNull())
  {
    //static bool ShowShapeDraw = true;
    //if (!ShowShapeDraw) return;
    Engine *engine=GEngine;
    
    #if 0 //_ENABLE_PERFLOG
    if (LogStatesOnce)
    {
      LogF("Draw shape - HW T&L");
    }
    #endif
    // use engine T&L
    int bias;
    if (spec&(OnSurface|IsOnSurface))
    {
      bias = 0x10;
    }
    else
    {
      bias = ((spec&ZBiasMask)/ZBiasStep) * 5;
    }

#if _VBS3_CRATERS_BIAS
    // Use the maximum bias we have up to now
    saturateMax(bias, dp._bias);
#endif

    const VertexTableAnimationContext *src = animContext ? animContext->GetVertexTableContext() : NULL;

    engine->BeginInstanceTL(cb, transform, minDist2, spec, lights, dp);
    bool ok = engine->BeginMeshTL(cb, src, *this, prop, spec, lights, bias);
    if (ok)
    {

      PROFILE_SCOPE_GRF_EX(cuDrw,drw,SCOPE_COLOR_RED);
      
      DrawSections(cb,animContext, animColor, prop, spec, bias, dp);

      engine->EndMeshTL(cb, *this);
    }
  }
  else
  {
    if (cb>=0)
    {
      // SW rendering not compatible with CB - if we are here, it is because VB creation has failed
      return;
    }
#if _VBS3
    if (!dp._zprimeIsBeingRendered)
#endif
    {
      DoAssert(IsLoadLocked());
#if 0 //_ENABLE_PERFLOG
      if (LogStatesOnce)
      {
        LogF("Draw shape - SW T&L");
      }
#endif
      DoAssert
        (
        _face._sections.Size()==0 ||
        _face._sections[_face._sections.Size()-1].end==EndFaces()
        );

      // custom T&L
      Matrix4 invTransform = transform.position.InverseGeneral();
      _face.Draw(animColor,lights,animContext,*this,clip,spec,transform,invTransform);
    }
  }
  #endif
}

bool ShapeBank::Preload(
  FileRequestPriority prior, const char *name, ShapeParameters pars, Ref<LODShape> &shape
)
{
  PROFILE_SCOPE(sbPre);
  RString lowName(name);
  lowName.Lower();

  {
    PROFILE_SCOPE(sbFnd);
    shape = FindLoaded(lowName,pars,true);
    if (shape)
    {
      return true;
    }
  }
  
  PROFILE_SCOPE(sbShP);
  if (LODShape::Preload(prior,name))
  {
    shape = New(name,pars);
    return true;
  }
  return false;
}

/**
@param refresh When true and the shape is found in the cache, it is moved into the regular area
*/
LODShape *ShapeBank::FindLoaded(const RStringB &name, ShapeParameters pars, bool refresh)
{
  PROFILE_SCOPE(shFnd);
  ShapeBankName bankName(name,pars);
  const LLink<LODShape> &shape = Get(bankName);
  if (!IsNull(shape))
  {
    if (shape->IsInList())
    {
      // shape was cached - update it
      DoAssert(shape->RefCounter()==0);
      if (refresh)
      {
        _lodShapeCache.Delete(shape);
      }
      //LogF("## Reused cached shape %s",cc_cast(shape->GetName()));
    }
    if (PROFILE_SCOPE_NAME(shFnd).IsActive())
    {
      PROFILE_SCOPE_NAME(shFnd).AddMoreInfo(Format("%s:%d",cc_cast(name),pars));
    }
    return shape;
  }
  #if _DEBUG
  // even if the shape is cached, it should be found during the first loop
  for (LODShape *shape = _lodShapeCache.Last(); shape; shape=_lodShapeCache.Prev(shape))
  {
    if ( shape->GetName()!=name ) continue;
    if ( shape->Remarks()!=pars)
    {
      Log(
        "Shape %s search failed because of different flags (%d!=%d)",
        (const char *)name,shape->Remarks(),pars
      );
      continue;
    }
    Fail("Late cached shape discovery");
    Assert(shape->RefCounter()==0);
    if (refresh)
    {
      Add(shape);
      _lodShapeCache.Delete(shape);
    }
    return shape;
  }
  #endif
  if (PROFILE_SCOPE_NAME(shFnd).IsActive())
  {
    PROFILE_SCOPE_NAME(shFnd).AddMoreInfo(Format("N - %s:%d",cc_cast(name),pars));
  }
  return NULL;
}

LODShape *ShapeBank::GetLoaded(const char *name, ShapeParameters pars)
{
  return FindLoaded(name,pars,false);
}

LODShape *ShapeBank::New(const char *name, ShapeParameters pars, const char * forceName)
{
  RString lowName(name);
  lowName.Lower();
  
  LODShape *shape = FindLoaded(lowName,pars,true);
  if (shape) return shape;
  
  //LogF("## New shape %s",name);
  PROFILE_SCOPE(shNew);
  if (PROFILE_SCOPE_NAME(shNew).IsActive())
  {
    PROFILE_SCOPE_NAME(shNew).AddMoreInfo(lowName);
  }

// allow use model from different location for binarize
#if _BINARIZE || _ENABLE_SINGLE_BINARIZE_EXECUTABLE
  if (forceName)
    shape=new LODShape(lowName,(pars&ShapeReversed)!=0,forceName);
  else
#endif
    shape=new LODShape(lowName,(pars&ShapeReversed)!=0);

  shape->SetRemarks(pars);
  if (!(pars&ShapeShadow))
  {
    shape->DisableShadow();
  }
  if (pars&ShapeNotAnimated)
  {
    shape->SetAnimationType(AnimTypeNone);
  }
  Add(shape);
  // add shape into unloadable pool
  // simple: create a temporary lock for each level
  for (int level=0; level<shape->NLevels(); level++)
  {
    if (!shape->LevelRef(level).IsLoaded()) continue;
    ShapeUsedGeometryLock<> lock(shape,level);
  }
  
  return shape;
}

bool Shape::CustomTLRequired() const
{
  if (NVertex()==0) return true;
  ClipFlags globalLight=GetAndHints()&ClipLightMask;
  if (globalLight&ClipLightLine)
  {
    return true;
  }
  if (Special()&OnSurface)
  {
    // if whole object is split, HW TL can be used
    return (GetAndHints()&ClipLandMask)!=ClipLandOn;
  }
  return false;
}

/**
Currently this is used to adjust texture priorities based on pixel shader ID
*/
void Shape::AdjustTextures()
{
  for (int i=0; i<NSections(); i++)
  {
    ShapeSection &sec = GetSection(i);
    TexMaterial *mat = sec.GetMaterialExt();
    if (mat)
    {
      int performance = 0;
      // we have guaranteed material is already loaded
      // with binarized p3d materials are embedded into the file
      // textures may be not loaded yet, but that does not matter here
      PixelShaderID ps = mat->GetPixelShaderID();
      switch (ps)
      {
        case PSGrass:
        case PSGrassAToC:
          performance = 200;
          break;
        case PSNormalMapThrough:
        case PSNormalMapThroughSimple:
        case PSNormalMapSpecularThrough:
        case PSNormalMapSpecularThroughSimple:
        case PSTree:
        case PSTreeSN:
        case PSTreePRT:
        case PSTreeAdv:
        case PSTreeAdvSimple:
        case PSTreeAdvTrunk:
        case PSTreeAdvTrunkSimple:
        case PSTreeAToC:
        case PSTreeAdvAToC:
        case PSTreeSimple:
        case PSTreeAdvSimpleAToC:
          performance = 100;
          break;
      }

      if (performance>0)
      {
        Texture *tex = sec.GetTexture();
        if (tex) tex->SetTexturePerformance(performance);
        for (int i=0; i<mat->_stageCount+1; i++)
        {
          Texture *stageTex = mat->_stage[i].GetTextureForDraw();
          if (stageTex) stageTex->SetTexturePerformance(performance);
        }
      }
    }
  }
}

void Shape::GeometryLoadedHandler()
{
  // check what post-load processing is required
  // we may need to adjust texture priorities
  
}

bool Shape::GeometryUnloadedHandler()
{
  return true;
}

bool Shape::AddRefLoad() const
{
  if (_source.IsNull()) return false;

  if (GEngine->IsInCBScope())
  {
    // when locking inside of the CB rendering, do not actually do anything
#if _ENABLE_REPORT
    _loadCountCB++;
#endif
    // verify the data are really there
    DoAssert(IsGeometryLoaded());
    Assert(IsInList());
    return false;
  }

  return _loadCount++==0;
}

bool Shape::ReleaseLoad() const
{
  if (_source.IsNull()) return false;

  if (GEngine->IsInCBScope())
  {
    // when locking inside of the CB rendering, do not actually do anything
#if _ENABLE_REPORT
    _loadCountCB--;
    Assert(_loadCountCB>=0);
#endif
    return false;
  }

  return --_loadCount==0;
}

bool Shape::IsLoadLocked() const
{
  if (_source.IsNull()) return true;

  if (GEngine->IsInCBScope())
  {
#if _ENABLE_REPORT
    return _loadCountCB>0;
#else
    return true;
#endif
  }
  
  return _loadCount>0;
}

DEF_RSB(null)

void Shape::ConvertToVBuffer(VBType type, const LODShape *lodShape, int level) const
{
  Assert(type!=VBNotOptimized);
  if (_buffer)
  {
    // type should not change to VBNone once the buffer exists, but if it would, it would be an error
    Assert(type!=VBNone);
    // we need to refresh the buffer
    return GEngine->RefreshBuffer(*this);
  }
  if (NVertex()<=0 || type==VBNone) return;
  if (!GEngine->IsAbleToDrawCheckOnly()) return;
  bool frequent = lodShape ? lodShape->GetPropertyFrequent() : false;
  // skeleton based check - do we need skinning?
  if (type!=VBDynamic)
  {
    // override type based on skeleton in the LOD
    // if skinning is needed, we have the only choice
    if (GetSubSkeletonSize()>0)
    {
      type = VBStaticSkinned;
    }
    else
    {
      // if skinning not needed, use plain static
      if (type==VBStaticSkinned) type = VBStatic;
      // all other types leave untouched, to prevent e.g. instancing for landscapes
    }
    
  }
  
  // we cannot perform buffer creation inside of a CB scope
  if (GEngine->IsInCBScope())
  {
    RptF("Vertex buffer needs to be created before rendering. Out of memory? %s",cc_cast(lodShape->GetName()));
    return;
  }
    
  TimeManagerScope scope(TCCreateVertexBuffer);
  
  REPORTSTACKITEM(lodShape ? lodShape->GetName() : RSB(null));
  {
  ShapeGeometryLock<> lock;
  if (lodShape)
    lock.Lock(lodShape,level);
  _buffer.Ref<VertexBuffer>::operator =( GEngine->CreateVertexBuffer(*this, type, frequent) );
  }
  if (_buffer)
  {
    // there was also an additional condition, which is no longer needed any more - we are not using CPU shadows
    // // if the level is used for shadow casting, we cannot discard it
    // // if the level is not graphical, it cannot be used for shadow casting
    // if (anim!=AnimTypeSoftware && (level<_minShadow || level>=_nGraphical))
    // we cannot unload data if SW animation is used on them
    if (!lodShape)
    {
      unconst_cast(this)->UnloadGeometry();
    }
    else if (lodShape->GetAnimationType()!=AnimTypeSoftware)
    {
      Shapes.UnloadGeometry(unconst_cast(this));
      //unconst_cast(this)->UnloadGeometry();
    }
  }
#if 0
  // Detect we are creating the last LOD
  if ((type != VBSmallDiscardable) && lodShape)
  {
    if (level == lodShape->FindSimplestLevel())
    {
      LogF("%d: Created NonSD last LOD in LODShape %s", GlobalTickCount(), lodShape->Name());
    }
  }
#endif

  // Check if the shape can use a pushbuffer
  CheckPushbufferCanBeUsed();

}

static bool ShapeReleaseVBuffer(
  const LLink<LODShape> &shape, const ShapeBank::ContainerType *bank
)
{
  if (shape) shape->ReleaseVBuffers();
  return false;
};

void ShapeBank::ReleaseAllVBuffers()
{
  ForEachF(ShapeReleaseVBuffer);
}

void LODShape::UndoOptimizeRendering()
{
  if (_optimizeWanted==VBNotOptimized) return;
  _optimizeWanted = VBNotOptimized;
  //_optimized = VBNotOptimized;
  ReleaseVBuffers();
}

bool LODShape::LevelUsesVBuffer(const RefShapeRef &shape, int level) const
{
  // check if there are any special properties which may force us to do SW T&L
  // adapted from CustomTLRequired
  // VB disabled for empty shape? Why? Most likely it does not matter, as it is not rendered at all anyway
  //if (shape.NVertex()==0) return false;
  // check if there are any lighting flags
  ClipFlags globalLight=shape.GetOrHints()&ClipLightMask;
  // the only lighting flag left is the ClipLightLine
  if(globalLight&ClipLightLine)
  {
    return false;
  }
  if (shape.Special()&OnSurface)
  {
    // if whole object is split, HW TL can be used
    // this is already detected in LODShape::Load
    if (GetAnimationType()==AnimTypeSoftware) return false;
  }

  // ignore some special levels
  // rendered special levels are only view levels: pilot / gunner / commander / cargo, all <1500
  // and shadow volume levels
  if (Resolution(level)>1500)
  {
    if ((IsShadowVolume(level)) ||
        (level >= _shadowBuffer) && (level < _shadowBuffer + _shadowBufferCount))
    {
    }
    else
    {
      #if _ENABLE_REPORT
      // when someone is requesting rendering using some special LOD, we should comply
      return true;
      #else
      return false;
      #endif
    }
  }
  return true;
}


void LODShape::RecreateVBuffer(int level)
{
  
  ShapeUsed shape = Level(level);
  if (VertexBuffer *vb = shape->LockVertexBuffer())
  {
    GEngine->RefreshBuffer(*shape.GetShape());
  }
  else
  {
    if (LevelUsesVBuffer(level))
    {
      PROFILE_SCOPE(lsCVB);
      shape->ConvertToVBuffer(_optimizeWanted, this, level);
    }
    else
    {
      shape->ConvertToVBuffer(VBNone);
    }
  }
}

void LODShape::ReleaseVBuffers(bool deferred)
{
  PROFILE_SCOPE_DETAIL_EX(lsRVB,shape);
  //if (_optimized==VBNotOptimized) return;
  //_optimized = VBNotOptimized;
  for (int l=0; l<NLevels(); l++)
  {
    const Shape *shape = GetLevelLocked(l);
    if (shape)
      shape->ReleaseVBuffer(deferred);
  }
}

double LODShape::GetMemoryUsed() const
{
  double sum = 0;
  for (int i=0; i<NLevels(); i++)
  {
    const Shape *level = GetLevelLocked(i);
    if (!level) continue;
    sum += level->GetMemoryUsed();
  }
  return sum;
}

#define SHAPE_LOD_ENUM(type,prefix,XX) \
  XX(type, prefix, MemoryLevel) \
  XX(type, prefix, GeometryLevel) \
  XX(type, prefix, FireGeometryLevel) \
  XX(type, prefix, ViewGeometryLevel) \
  XX(type, prefix, ViewPilotGeometryLevel) \
  XX(type, prefix, ViewGunnerGeometryLevel) \
  XX(type, prefix, ViewCommanderGeometryLevel) \
  XX(type, prefix, ViewCargoGeometryLevel) \
  XX(type, prefix, LandContactLevel) \
  XX(type, prefix, RoadwayLevel) \
  XX(type, prefix, Paths) \
  XX(type, prefix, Hitpoints) \
  XX(type, prefix, Visual)

#ifndef DECL_ENUM_SHAPE_LOD
#define DECL_ENUM_SHAPE_LOD
DECL_ENUM(ShapeLodType)
#endif
DECLARE_DEFINE_ENUM(ShapeLodType,SLT,SHAPE_LOD_ENUM)

struct MemoryUnloadableContext
{
  double used[NShapeLodType];
  double permanent;
  
  MemoryUnloadableContext()
  {
    for (int i=0; i<NShapeLodType; i++) used[i] = 0;
    permanent = 0;
  }
};

size_t LODShape::GetMemoryControlled() const
{
  size_t sum = 0;
  for (int i=0; i<NLevels(); i++)
  {
    ShapeRef *refLevel = _lods[i];
    // if level is unloadable, it is managed by another cache
    if (refLevel->GetManagedInterface()) continue;
    Shape *level = refLevel->GetRef();
    size_t used = level->GetMemoryControlledByShape(true);
    sum += used;
  }
  if (_geomComponents)
  {
    sum += _geomComponents->GetMemoryControlled();
  }
  if (_viewComponents && _viewComponents!=_geomComponents)
  {
    sum += _viewComponents->GetMemoryControlled();
  }
  if (_fireComponents && _fireComponents!=_geomComponents && _fireComponents!=_viewComponents)
  {
    sum += _fireComponents->GetMemoryControlled();
  }
  #if VIEW_INTERNAL_GEOMETRIES
  if (_viewPilotComponents)
  {
    sum += _viewPilotComponents->GetMemoryControlled();
  }
  if (_viewGunnerComponents)
  {
    sum += _viewGunnerComponents->GetMemoryControlled();
  }
  if (_viewCargoComponents)
  {
    sum += _viewCargoComponents->GetMemoryControlled();
  }
  #endif
  // consider information used by animations
  if (_animations)
  {
    sum += _animations->GetMemoryControlled();
  }
  
  return sum+sizeof(*this);

}

double LODShape::GetMemoryUsedUnloadable(MemoryUnloadableContext &ctx) const
{
  double sum = 0;
  for (int i=0; i<NLevels(); i++)
  {
    const Shape *level = GetLevelLocked(i);
    if (!level) continue;
    ShapeLodType type = NShapeLodType;
    double used = level->GetMemoryUsedUnloadable();
    if (i<_nGraphical) type = SLTVisual;
    else if (i==FindMemoryLevel()) type = SLTMemoryLevel;
    else if (i==FindGeometryLevel()) type = SLTGeometryLevel;
    else if (i==FindFireGeometryLevel()) type = SLTFireGeometryLevel;
    else if (i==FindViewGeometryLevel()) type = SLTViewGeometryLevel;
    else if (i==FindViewPilotGeometryLevel()) type = SLTViewPilotGeometryLevel;
    else if (i==FindViewGunnerGeometryLevel()) type = SLTViewGunnerGeometryLevel;
    else if (i==FindViewCargoGeometryLevel()) type = SLTViewCargoGeometryLevel;
    else if (i==FindLandContactLevel()) type = SLTLandContactLevel;
    else if (i==FindRoadwayLevel()) type = SLTRoadwayLevel;
    else if (i==FindPaths()) type = SLTPaths;
    else if (i==FindHitpoints()) type = SLTHitpoints;
    
    //double memReallyUsed = level->GetMemoryUsed();
    //if (i==FindShadowVolume()) type = SLTShadowVolume;
    if (level->GeometryCanBeUnloaded())
    {
      sum += used;
    }
    else
    {
      if (type<NShapeLodType)
      {
        ctx.used[type] += used;
      }
      ctx.permanent += used;
      /*
      if (used>=10*1024)
      {
        LogF(
          "%s:%s uses %.0f (%.0f)",
          (const char *)GetName(),(const char *)LevelName(_resolutions[i]),
          used,memReallyUsed
        );
      }
      */
    }
  }
  return sum;
}

const RStringB TentString("tent");


void LODShape::OptimizeRenderingListed()
{
  VBType type;
  switch(GetAnimationType())
  {
  case AnimTypeSoftware:
    type = VBDynamic;
    break;
  case AnimTypeHardware:
    type = VBStaticSkinned;
    break;
  default:
    type = VBStatic;
    break;
  }
  OptimizeRendering(type);
}

void LODShape::OptimizeLevelRendering(int level, bool forceAnimated)
{
  if (_optimizeWanted!=VBNotOptimized)
  {
    // make sure given LOD vbuffer is really created
    RecreateVBuffer(level);
    return;
  }
#if _ENABLE_CHEATS
  if (!strcmp(_name,"ca\\animals\\racekt.p3d"))
  {
    __asm nop;
  }
#endif

  //VBType type = (GetAnimationType() == AnimTypeSoftware) || forceAnimated ? VBDynamic : VBStatic;
  VBType type;
  if ((GetAnimationType() == AnimTypeSoftware) || forceAnimated)
  {
    type = VBDynamic;
  }
  else if (GetAnimationType() == AnimTypeHardware)
  {
    type = VBStaticSkinned;
  }
  else
  {
    type = VBStatic;
  }


  OptimizeRendering(type);

  // make sure given LOD vbuffer is really created
  RecreateVBuffer(level);
}


ShapeBank::ShapeBank()
{
  RegisterFreeOnDemandMemory(safe_cast<MHInstanceGeom *>(this));
  RegisterFreeOnDemandMemory(safe_cast<MHInstanceShape *>(this));
  RegisterFreeOnDemandMemory(safe_cast<MHInstanceLODShape *>(this));
}
ShapeBank::~ShapeBank()
{
  // unload all geometries
  Assert(CheckIntegrity());
}

void ShapeBank::ShapeLoaded(ShapeRefManaged *shapeRef)
{
  if (shapeRef->IsInList())
  {
    AssertMainThread();
    _cachedShapes.Delete(shapeRef);
    return;
  }
  if (shapeRef->IsLoaded())
  {
    return;
  }
  // not in list - we need to load it
  shapeRef->DoLoad();
  
}

void ShapeBank::ShapeUnloaded(ShapeRefManaged *shapeRef)
{
  AssertMainThread();
  _cachedShapes.Add(shapeRef);
}

void ShapeBank::ShapeGeometryReleased(Shape *shape)
{
  AssertMainThread();
  if (shape->IsInList())
  {
    //_geometryLoadedSize -= shape->GeometrySize();
    // how do we know which list is this in?
    _geometryLoaded.Delete(shape);
  }
}

//void ShapeBank::ShowLastLODsSpace()
//{
//  int occupiedSpace = 0;
//  int occupiedSSpace = 0;
//  int occupiedHSpace = 0;
//  for (int i = 0; i < Size(); i++)
//  {
//    LODShape *shape = Get(i);
//    if (!shape) continue;
//
//    // Find the last LOD
//    float maxResolution = -10.0f;
//    int maxLod = -1;
//    for (int i = 0; i < shape->NLevels(); i++)
//    {
//      if ((shape->Resolution(i) < 900) && (maxResolution < shape->Resolution(i)))
//      {
//        maxResolution = shape->Resolution(i);
//        maxLod = i;
//      }
//    }
//
//    // Add vertex buffer space occupied by the LOD
//    ShapeUsed s = shape->Level(maxLod);
//    int estBufferSize = s->NVertex() * 20;
//    estBufferSize *= 8; // Multiply by maximum instances
//    saturateMin(estBufferSize, 2000 * 20);
//    occupiedSpace += estBufferSize;
//
//    // Add shape buffer space occupied by the LOD
//    occupiedSSpace += s->NVertex() * 32;
//
//    // Get the space occupied by the vertex buffer
//    if (s->GetVertexBuffer())
//    {
//      occupiedHSpace += estBufferSize;
//    }
//  }
//
//  // Show last lods space
//  ADD_COUNTER(LLVSpace,occupiedSpace);
//  ADD_COUNTER(LLSSpace,occupiedSSpace);
//  ADD_COUNTER(LLHSpace,occupiedHSpace);
//}

void ShapeBank::ShapeReleased(ShapeRefManaged *shapeRef)
{
  AssertMainThread();
  // find corresponding shape in the list
  // check if this shape is managed
  // if it is, remove it 
  if (shapeRef->IsInList())
  {
    _cachedShapes.Delete(shapeRef);
  }
}

void ShapeBank::UnloadGeometry(Shape *shape)
{
  if (shape->IsInList())
  {
    Assert(shape->GeometryCanBeUnloaded());
//     if (strstr(shape->GetDebugName(),"road"))
//     {
//       LogF("Unloading road %s",cc_cast(shape->GetDebugName()));
//     }
    shape->UnloadGeometry();
    _geometryLoaded.Delete(shape);
  }
}

float ShapeBank::PriorityGeom() const
{
  return 0.3;
}
RString ShapeBank::GetDebugNameGeom() const
{
  return "Geometries";
}
size_t ShapeBank::FreeOneItemGeom()
{
  Shape *unload = _geometryLoaded.First();
  if (!unload) return 0;
  
  #if _DEBUG
  size_t memUsedBefore = MemoryUsed();
  #endif
  
  size_t released = _geometryLoaded.Delete(unload);
  Assert(unload->GeometryCanBeUnloaded());
  unload->UnloadGeometry();
  
  #if _DEBUG
  size_t memUsedAfter = MemoryUsed();
  size_t exact = memUsedBefore-memUsedAfter;
  (void)exact;
  #endif

  return released;
}
size_t ShapeBank::MemoryControlledRecentGeom() const
{
  return _geometryLoaded.MemoryControlledRecent();
}
size_t ShapeBank::MemoryControlledGeom() const
{
  return _geometryLoaded.MemoryControlled();
}

void ShapeBank::MemoryControlledFrameGeom()
{
  _geometryLoaded.Frame();
  
#if 0
  size_t totalPB = 0;
  size_t discardPB = 0;
  size_t lastPB = 0;
  for (int i=0; i<Size(); i++)
  {
    LODShape *lShape = Get(i);
    if (lShape) for (int i=0; i<lShape->NLevels(); i++)
    {
      Shape *shape = lShape->GetLevelLocked(i);
      if (!shape) continue;
      totalPB += shape->GetMemoryControlledByProgram();
      if (lShape->LevelRef(i).GetShapeRef()->GetManagedInterface())
      {
        discardPB += shape->GetMemoryControlledByProgram();
      }
      if (i==lShape->FindSimplestLevel())
      {
        lastPB += shape->GetMemoryControlledByProgram();
      }
    }
  }
  DIAG_MESSAGE(
    200,Format(
      "PB total %d, discard %d, last %d",totalPB,discardPB,lastPB
    )
  );
#endif // _DEBUG
}



float ShapeBank::PriorityShape() const
{
  return 0.4;
}

RString ShapeBank::GetDebugNameShape() const
{
  return "ShapeLevels";
}


size_t ShapeBank::FreeOneItemShape()
{
  AssertMainThread();
  // if there is no geometry for unloading, unload the whole shape
  ShapeRefManaged *shapeUnload = _cachedShapes.First();
  if (!shapeUnload)
  {
    return 0;
  }
  return FreeOneItemShape(shapeUnload);
}

size_t ShapeBank::FreeOneItemShape(ShapeRefManaged *shapeUnload)
{
  AssertMainThread();
  Shape *shape = shapeUnload->GetRef();
  Assert(shape);
  if (!shape) return 0;
  ShapeGeometryReleased(shape);
  #if _DEBUG
  size_t beforeShapeUnload = MemoryUsed();
  #endif
  // removing the shape from the list does not affect shape existance
  Assert(shapeUnload->GetLockCount()==0);
  size_t released = _cachedShapes.Delete(shapeUnload);
  shapeUnload->DoUnload();
  #if _DEBUG
  size_t afterShapeUnload = MemoryUsed();
  size_t exactShapeUnload = beforeShapeUnload-afterShapeUnload;
  (void)exactShapeUnload;
  #endif
  return released;
}

size_t ShapeBank::FreeOneItemShape(LODShape *lShape, int level)
{
  ShapeRefManaged *shapeUnload = lShape->LevelRef(level).GetShapeRef()->GetManagedInterface();
  if (!shapeUnload) return 0;
  return FreeOneItemShape(shapeUnload);
}

bool ShapeBank::CheckIntegrity() const
{
  return _geometryLoaded.CheckIntegrity() && _cachedShapes.CheckIntegrity();
}

void ShapeBank::MemoryControlledFrameShape()
{
  AssertMainThread();
  _cachedShapes.Frame();
}

size_t ShapeBank::MemoryControlledRecentShape() const
{
  return _cachedShapes.MemoryControlledRecent();
}

size_t ShapeBank::MemoryControlledShape() const
{
  AssertMainThread();
  return _cachedShapes.MemoryControlled();
}

#if _DEBUG
void LODShape::OnUsed() const
{
  #if _DEBUG
    if (!strcmp(GetName(),"data3d\\mc vojakw2.p3d"))
    {
      LogF("Used %s",cc_cast(GetName()));
    }
  #endif
  DoAssert(!IsInList());
}
#endif

void LODShape::OnUnused() const
{
  DoAssert(RefCounter()==0);
  if (_name.GetLength()>0)
  {
    //LogF("## Unused shape %s",cc_cast(GetName()));
    // named - cache it
    //DestroyLinkChain();
    // LODShape links survive being in the cache
    Shapes.OnUnusedLODShape(unconst_cast(this));
  }
  else
  {
    // unnamed - destroy it
    Destroy();
  }
}

void ShapeBank::OnUnusedLODShape(LODShape *shape)
{
  Assert(shape);
  DoAssert(shape->RefCounter()==0);
  _lodShapeCache.Add(shape);
}
float ShapeBank::PriorityLODShape() const
{
  return 0.06f;
}
RString ShapeBank::GetDebugNameLODShape() const
{
  return "LODShapes";
}
size_t ShapeBank::FreeOneItemLODShape()
{
  LODShape *last = _lodShapeCache.First();
  if (!last) return 0;
  size_t ret = _lodShapeCache.Delete(last);
  last->Destroy();
  return ret;
}
size_t ShapeBank::MemoryControlledRecentLODShape() const
{
  return _lodShapeCache.MemoryControlledRecent();
}
size_t ShapeBank::MemoryControlledLODShape() const
{
  return _lodShapeCache.MemoryControlled();
}
void ShapeBank::MemoryControlledFrameLODShape()
{
  _lodShapeCache.Frame();
}


void ShapeBank::AddRefGeometry(const Shape *shape, const ShapeRef *shapeRef)
{
  if (!shape->AddRefLoad())
  {
    // TODO:MC: create a SLL of those which need the handling below to be done
    return;
  }
  if (shape->IsInList())
  {
    _geometryLoaded.Delete(unconst_cast(shape));
    DoAssert(shape->IsGeometryLoaded());
    //_geometryLoadedSize -= shape->GeometrySize();
    return;
  }
  // if shape was not controlled yet, it was not discarded, and is is loaded
  // we do not care - LoadGeometry should take care of this and do nothing
  unconst_cast(shape)->LoadGeometry(shapeRef);
}

void ShapeBank::ReleaseGeometry(const Shape *shape)
{
  //size_t size = shape->GeometrySize();
  if (!shape->ReleaseLoad())
  {
    // shape already tracked - refresh the cache
    // we may still be in list, as in the rendering we do not update the lists
    // Assert(!shape->IsInList());
    return;
  }
  /// instead of unloading add it into the unloaded cache
  DoAssert(!shape->IsInList());
  // pretend it is still loaded
  unconst_cast(shape)->AddRefLoad();
  _geometryLoaded.Add(unconst_cast(shape));
  unconst_cast(shape)->ReleaseLoad();
}



/// functor for ShapeBank::GetMemoryUsedByShapes
struct GetMemoryUsedByShapesF
{
  double sum;
  double unloadable;
  
  MemoryUnloadableContext &_ctx;
  
  GetMemoryUsedByShapesF(MemoryUnloadableContext &ctx)
  :_ctx(ctx)
  {
    sum = 0;
    unloadable = 0;
    
  }
  
  bool operator () (
    const LLink<LODShape> &shape, const ShapeBank::ContainerType *bank
  )
  {
    if (shape)
    {
      sum += shape->GetMemoryUsed();
      unloadable += shape->GetMemoryUsedUnloadable(_ctx);
    }
    return false;
  }
};
/*!
this functions differs from default RefArray<>::GetMemoryUsed, as it does not consider reference counts,
and therefore counts all memory owned by shapes with no regard to who owns the shape.
*/
double ShapeBank::GetMemoryUsedByShapes()
{
  double sum = 0;
  MemoryUnloadableContext ctx;
  GetMemoryUsedByShapesF used(ctx);
  ForEachF(used);
  for (int i=0; i<NShapeLodType; i++)
  {
    LogF(
      "In shapes %s - not unloadable %d",
      (const char *)FindEnumName((ShapeLodType)i),toLargeInt(ctx.used[i])
    );
  }
  LogF("In shapes - not unloadable %d",toLargeInt(ctx.permanent));

  return sum;
}

void ShapeBank::ClearCache()
{
  MHInstanceGeom::FreeAll();
  MHInstanceShape::FreeAll();
  MHInstanceLODShape::FreeAll();
}


static bool ShapeOptimizeRendering(
  const LLink<LODShape> &shape, const ShapeBank::ContainerType *bank
)
{
  if (shape)
  {
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();
    shape->OptimizeRenderingListed();
  }
  return false;
};

void ShapeBank::OptimizeAll()
{
  Optimize();
  // flush was done here before, but we prefer reusing as much as possible now
  
  // note: this is almost nop - no VB creation is done here, only variable is set
  ForEachF(ShapeOptimizeRendering);
}


ShapeBank Shapes;


/**
Used instead of:
const PolyProperties &ss = animContext ? animContext->GetSection(s) : GetSection(s);
As using ? : led to some unnecessary PolyProperties copy
*/

static inline const PolyProperties &GetSS(AnimationContext *animContext, const Shape &shape, int s)
{
  if (animContext) return animContext->GetSection(&shape, s);
  return shape.GetSection(s);
}

int Shape::PreloadTexturesLeft(float z2, AnimationContext *animContext, bool animated, float *quality) const
{
  PROFILE_SCOPE_DETAIL(prlTL);
  if (quality)
  {
    *quality = 1.0f;
  }
  // count how many textures are still not loaded
  int left = 0;
  // if there is a valid index, it is much faster to use it
  #if _ENABLE_REPORT
  static bool useIndex = true;
  #else
  const bool useIndex = true;
  #endif
  
  if (useIndex && _texArea._valid)
  {
    int left = 0;
    for (int i=0; i<_texArea._items.Size(); i++)
    {
      const TexAreaIndex::Item &item = _texArea._items[i];
      Texture *txt = *item._tex;
      // txt is usually not NULL, but some animation might have set it to NULL
      if (!txt) continue;
      float areaOTex = item._aot;
      Assert(areaOTex>=0);
      if (!txt->PreloadTexture(z2,areaOTex,item._someDataWanted,quality))
      {
        left++;
      }
      if (animated && txt->IsAnimated())
      {
        AnimatedTexture *anim = txt->GetAnimatedTexture();
        for (int i=0; i<anim->Size(); i++)
        {
          Texture *atex = anim->Get(i);
          if (!atex->PreloadTexture(z2,areaOTex,item._someDataWanted,quality))
          {
            left++;
          }
        }
      }
    }
    DoAssert(left==0 || !quality || *quality<1);
    return left;
  }
  // hint mipmap for all textures used on this shape
  for (int s=0; s<NSections(); s++)
  {
    // when animation context is available, use animated info
    const PolyProperties &ss = GetSS(animContext,*this,s);

    Texture *txt = ss.GetTexture();
    if (txt)
    {
      float areaOTex = ss.GetAreaOverTex(0);
      Assert(areaOTex>=0);
      if (!txt->PreloadTexture(z2,areaOTex,false,quality))
      {
        left++;
        DoAssert(left==0 || !quality || *quality<1);
      }
      if (animated && txt->IsAnimated())
      {
        AnimatedTexture *anim = txt->GetAnimatedTexture();
        for (int i=0; i<anim->Size(); i++)
        {
          Texture *atex = anim->Get(i);
          if (!atex->PreloadTexture(z2,areaOTex,false,quality))
          {
            left++;
            DoAssert(left==0 || !quality || *quality<1);
          }
        }
      }
    }
    // we need to prepare detail textures as well
    TexMaterial *tmat = ss.GetMaterialExt();
    if (tmat)
    {
      if (!tmat->PreloadData(ss.GetAreaOverTex(),z2,quality))
      {
        left++;
        DoAssert(left==0 || !quality || *quality<1);
      }
    }
  }
  DoAssert(left==0 || !quality || *quality<1);
  return left;
}

bool Shape::IsOwningTexture(Texture *texture) const
{
  for (int s=0; s<NSections(); s++)
  {
    const ShapeSection &sec = GetSection(s);
    Texture *tex = sec.GetTexture();
    if (tex==texture)
    {
      return true;
    }
    TexMaterial *mat = sec.GetMaterialExt();
    if (mat && mat->IsReady())
    {
      for (int i=0; i<mat->_stageCount+1; i++)
      {
        Texture *matTex = mat->_stage[i].GetTextureForDraw();
        if (matTex==texture)
        {
          return true;
        }
      }
    }
  }
  return false;
}

bool Shape::MakeTexturesReady() const
{
  if (_allTexturesReady) return true;
  PROFILE_SCOPE_EX(lsCTR,*);
  // if the level geometry is ready, we are almost done
  // we should check texture headers as well
  for (int s=0; s<NSections(); s++)
  {
    const ShapeSection &sec = GetSection(s);
    Texture *tex = sec.GetTexture();
    if (tex && !tex->HeadersReady())
    {
//            LogF("%s: tex not ready %s",cc_cast(GetName()),cc_cast(tex->GetName()));
      return false;
    }
    TexMaterial *mat = sec.GetMaterialExt();
    if (mat)
    {
      if (!mat->IsReady())
      {
//              LogF("%s: mat not ready %s",cc_cast(GetName()),cc_cast(mat->GetName()));
        return false;
      }
      for (int i=0; i<mat->_stageCount+1; i++)
      {
        Texture *matTex = mat->_stage[i].GetTextureForDraw();
        if (matTex && !matTex->HeadersReady())
        {
//                LogF("%s: matTex not ready %s",cc_cast(GetName()),cc_cast(matTex->GetName()));
          return false;
        }
      }
    }
  }
  _allTexturesReady = true;
  return true;
}

void Shape::ForceTexturesReady()
{
  if (_allTexturesReady) return;
  PROFILE_SCOPE_EX(lsCTR,*);
  // if the level geometry is ready, we are almost done
  // we should check texture headers as well
  for (int s=0; s<NSections(); s++)
  {
    const ShapeSection &sec = GetSection(s);
    Texture *tex = sec.GetTexture();
    if (tex && !tex->HeadersReady())
    {
      tex->LoadHeaders();
      DoAssert(tex->HeadersReady()); // verify it has completed (it would not if only sumbitted to other thread)
    }
    TexMaterial *mat = sec.GetMaterialExt();
    if (mat)
    {
      if (!mat->IsReady())
      {
        mat->Load();
      }
      for (int i=0; i<mat->_stageCount+1; i++)
      {
        Texture *matTex = mat->_stage[i].GetTextureForDraw();
        if (matTex && !matTex->HeadersReady())
        {
          matTex->LoadHeaders();
          DoAssert(matTex->HeadersReady());
        }
      }
    }
  }
  _allTexturesReady = true;
}

#define AREA_OF_TEXTURE_HOTFIX 1

static inline void RepairAreaOfTexture(float &areaOfTexture)
{
#if AREA_OF_TEXTURE_HOTFIX
  // hotfix of news:igfcgp$r10$1@new-server.localdomain
  // textures with low areaOTex did not choose mipmaps correctly
  // value was selected experimentally
  static const float MIN_AREA_OVER_TEX = 1.5f;
  if (areaOfTexture < MIN_AREA_OVER_TEX)
    areaOfTexture = MIN_AREA_OVER_TEX;
#endif
}

void Shape::PrepareTextures(float z2, AnimationContext *animContext, int special, bool prepareBaseTextureOnly) const
{
  PROFILE_SCOPE_GRF(prpTx,0);

  if( special&::BestMipmap )
  {
    z2 = 0; // force special handling
    special &= ~::BestMipmap; // we have handled it, do not pass it any more
  }

  float aotFactor = GScene->GetCamera()->GetAOTFactor();
  if (!prepareBaseTextureOnly && _texArea._valid)
  {
    for (int i=0; i<_texArea._items.Size(); i++)
    {
      const TexAreaIndex::Item &item = _texArea._items[i];
      Texture *txt = *item._tex;
      // txt is usually not NULL, but some animation might have set it to NULL
      if (!txt) continue;
      float areaOTex = item._aot;
      Assert(areaOTex>=0);
      txt->PrepareTextureScr(z2,special,areaOTex*aotFactor);
    }
    return;
  }
  // hint mipmap for all textures used on this shape
  for (int s=0; s<NSections(); s++)
  {
    // TI AOT and scale determined by the stage the TI texture will shader same UV coordinates with (this stage is determined as a "Mate")
    bool tiWasMatePreloaded = false; // If this is true, the other ti* must be valid and set
    bool tiPreloadWhole = false;
    float tiAOT = 1.0f;
    float tiUVScale = 1.0f;

    // when animation context is available, use animated info
    const PolyProperties &ss = GetSS(animContext,*this,s);

    Texture *txt = ss.GetTexture();
    TexMaterial *tmat = ss.GetMaterialExt();

    if (txt)
    {
      float areaOTex = ss.GetAreaOverTex(0);
      Assert(areaOTex>=0);
      txt->PrepareTextureScr(z2,special,areaOTex*aotFactor);
      // Mark the TI mate as preloaded and get it's preloading parameters
      if (tmat)
      {
        if (TITexSource[tmat->GetPixelShaderID()] == 0)
        {
          tiWasMatePreloaded = true;
          tiPreloadWhole = false;
          tiAOT = areaOTex*aotFactor;
          tiUVScale = 1.0f;
        }
      }
    }

    // If only base texture is to be prepared, then go to the next section
    if (prepareBaseTextureOnly) continue;

    // we need to prepare detail textures as well
    if (tmat)
    {
      tmat->Load();

      // Prepare textures on particular stages
      for (int i = 0; i < tmat->_stageCount+1; i++)
      {
        Texture* stageTexture = tmat->_stage[i].GetTextureForDraw();
        if (stageTexture)
        {
          float uvScale = tmat->GetUVScale(i);
          if (uvScale >= 0.0f)
          {
            int uvStage = tmat->GetUVStage(i);
            float areaOTex = ss.GetAreaOverTex(uvStage);
#if _ENABLE_REPORT
            DoAssert(areaOTex>=0);
            if (areaOTex < 0)
              RptF("Area of texture is less than 0, missing UV set?: uvStage=%d, value=%g, model='%s'",
                uvStage, areaOTex, tmat->_filename.Data());
#endif
            RepairAreaOfTexture(areaOTex);
            stageTexture->PrepareTextureScr(z2*uvScale,special,areaOTex*aotFactor);
            // Mark the TI mate as preloaded and get it's preloading parameters
            if (TITexSource[tmat->GetPixelShaderID()] == i)
            {
              tiWasMatePreloaded = true;
              tiPreloadWhole = false;
              tiAOT = floatMax(tiAOT, areaOTex);
              tiUVScale = uvScale;
            }
          }
          else
          {
            stageTexture->LoadHeaders();
            stageTexture->PrepareMipmap0(true);
            // Mark the TI mate as preloaded and get it's preloading parameters
            if (TITexSource[tmat->GetPixelShaderID()] == i)
            {
              tiWasMatePreloaded = true;
              tiPreloadWhole = true;
            }
          }
        }
      }

      // Preload TI texture - use the TI mate parameters
      // If the mate was not preloaded, we cannot do anything (because we don't know the preloading parameters)
      if (tiWasMatePreloaded && GEngine->GetThermalVision())
      {
        Texture *texture = tmat->_stageTI.GetTextureForDraw();
        if (texture)
        {
          if (!tiPreloadWhole)
          {
            texture->PrepareTextureScr(z2 * tiUVScale, special, tiAOT);
          }
          else
          {
            texture->LoadHeaders();
            texture->PrepareMipmap0(true);
          }
        }
      }
    }
  }
}


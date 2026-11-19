// Poseidon - Transformation and lighting
#include "wpch.hpp"

#include "tlVertex.hpp"
#include "camera.hpp"
#include "global.hpp"
#include "world.hpp"
#include "scene.hpp"
#include "lights.hpp"
#include "landscape.hpp"
#include "engine.hpp"
#include <El/Common/perfLog.hpp>
#include "keyInput.hpp"
#include "dikCodes.h"

#include "Shape/specLods.hpp"

#if _KNI
  #define PrefetchTemp(a,i) _mm_prefetch((char *)&(a[i]),_MM_HINT_NTA)
  #define Prefetch(a,i) _mm_prefetch((char *)&(a[i]),_MM_HINT_T0)
#else
  #define PrefetchTemp(a,i)
  #define Prefetch(a,i)
#endif

static StaticStorage<Vector3> PosTransStorage;

static StaticStorage<TLVertex> TLVertexStorage;

static StaticStorage<ClipFlags> ClipStorage;

// note: no need to allocate a lot, custom SW TL is no longer used
const int MaxTLVertices=1024;

void TLVertexTable::DoConstruct()
{
  _posTrans.SetStorage(PosTransStorage.Init(MaxTLVertices));

  _vert.SetStorage(TLVertexStorage.Init(MaxTLVertices));
  _clip.SetStorage(ClipStorage.Init(MaxTLVertices));
  
  _orHints=ClipHints; // assume worst case
  _andHints=0;
}

TLVertexTable::TLVertexTable()
{
  DoConstruct();
}

void TLVertexTable::Init(const VertexTableAnimationContext *animContext, const Shape &src, const Matrix4 &toView)
{
  DoTransformPoints(animContext, src, toView);
}

TLVertexTable::TLVertexTable(const VertexTableAnimationContext *animContext, const Shape &src, const Matrix4 &toView)
{
  DoConstruct();
  DoTransformPoints(animContext, src, toView);
}

void TLVertexTable::ReleaseTables()
{
  _clip.Clear();
  _posTrans.Clear();
  _vert.Clear();
}

int TLVertexTable::AddPos()
{
  // used for clipping
  // interpolation result must be stored somewhere
  Assert (_posTrans.Size()==_vert.Size());
  int index=_vert.Size();
  int newSize=index+1;
  _posTrans.Resize(newSize);
  _vert.Resize(newSize);
  _clip.Resize(newSize);
  return index;
}


void TLVertexTable::DoTransformPoints
( 
  const Array<Vector3> &srcPos, const Matrix4 &posView, int beg, int end
)
{
  // this is the most common case
  // simple mesh with no decal polygons
  // TODO: most common case -> pass processing to ITransformer
  int size=end-beg;
  Vector3 *d = _posTrans.Data() + beg;
  const Vector3 *s = srcPos.Data() + beg;
  while( --size>=0 ) (*d++).SetMultiply(posView,*s++);
}

void TLVertexTable::DoTransformPoints
(
  const VertexTableAnimationContext *animContext,
  const Shape &src, const Matrix4 &posView
)
{
  _orHints = src._orHints;
  _andHints = src._andHints;
  const Array<Vector3> &srcPos = animContext ? animContext->GetPosArray(&src) : src.GetPosArray();

  src.ExpandClipTo(_clip);
  int nPos = _clip.Size();

  _vert.Resize(nPos);
  _posTrans.Realloc(srcPos.Size());
  _posTrans.Resize(srcPos.Size());
  if ((_orHints & ClipDecalMask) == 0)
  {
    DoTransformPoints(srcPos, posView, 0, nPos);
    return;
  }

  if
  (
    (_orHints & ClipDecalMask) == ClipDecalVertical &&
    (_andHints & ClipDecalMask) == ClipDecalVertical
  )
  {
    Fail("Obsolete");
    // second quite often - whole object is vertical decal (used for trees)
    Matrix4 vDecal(MZero);
    // we want to use only vertical part of rotation
    vDecal.SetUpAndAside(posView.DirectionUp(),VAside);
    vDecal = vDecal * GLOB_SCENE->GetCamera()->ScaleMatrix();
    // decal transformation must have same scale and position as object
    vDecal.SetPosition(posView.Position());
    vDecal.SetScale(posView.Scale());
    //vDecal.SetOrientation(vDecal.Orientation()*Matrix3(MScale,Scale()));
    int size = _posTrans.Size();
    for (int i=0; i<size; i++)
    {
      Vector3Val pos = srcPos[i];
      // we want to use only vertical part of rotation
      _posTrans[i].SetFastTransform(vDecal, pos);
    }
  }
  else if
  (
    (_orHints & ClipDecalMask) == ClipDecalNormal &&
    (_andHints & ClipDecalMask) == ClipDecalNormal
  )
  {
    Fail("Obsolete");
    // whole object is decal
    Matrix4 decal = GLOB_SCENE->GetCamera()->ScaleMatrix();
    decal.SetPosition(posView.Position());
    decal.SetScale(posView.Scale());
    int size=_posTrans.Size();
    for (int i=0; i<size; i++)
    {
      Vector3Val pos = srcPos[i];
      // we want to use only vertical part of rotation
      _posTrans[i].SetFastTransform(decal, pos);
    }
  }
  else
  {
    Fail("Obsolete");
    // TODO: remove or optimize mixed models
    // non-decal part must be handles by anim
    // general case: not very often
    // mixed decals support removed
    Matrix4 decal(NoInit);
    Matrix4 vDecal(NoInit);
    bool vDecalSet=false;
    bool decalSet=false;
    int size=_posTrans.Size();
    for (int i=0; i<size; i++)
    {
      Vector3Val pos = srcPos[i];
      ClipFlags clip = _clip[i];
      Vector3 &dPos = _posTrans[i];
      switch (clip & ClipDecalMask)
      {
        default:
          dPos.SetFastTransform(posView, pos);
        break;
        case ClipDecalNormal:
          if( !decalSet )
          {
            decal=GLOB_SCENE->GetCamera()->ScaleMatrix();
            decal.SetPosition(posView.Position());
            decal.SetScale(posView.Scale());
            decalSet=true;
          }
          dPos.SetFastTransform(decal,pos);
        break;
        case ClipDecalVertical:
          // we want to use only vertical part of rotation
          // this can be achieved by setting matrix
          if( !vDecalSet )
          {
            //vDecal=Matrix4(MZero);
            vDecal.SetUpAndAside(posView.DirectionUp(),VAside);
            vDecal.SetPosition(VZero);
            vDecal=vDecal*GLOB_SCENE->GetCamera()->ScaleMatrix();
            vDecal.SetPosition(posView.Position());
            vDecal.SetScale(posView.Scale());
            vDecalSet=true;
          }
          dPos.SetFastTransform(vDecal,pos);
        break;
      }
    }
  }
}

inline void VerifyClip
(
  TLVertex *d, float minX, float maxX, float minY, float maxY
)
{
  #if 0
    const float eps = 0.01;
    if (d->pos[0]<minX-eps || d->pos[0]>maxX+eps)
    {
      LogF("Bad x coord %.2f (%.0f..%.0f)",d->pos[0],minX,maxX);
      if (d->pos[0]<minX-5 || d->pos[0]>maxX+5)
      {
        LogF(" *** clipping error");
      }
    }
    if (d->pos[1]<minY-eps || d->pos[1]>maxY+eps)
    {
      LogF("Bad y coord %.2f (%.0f..%.0f)",d->pos[1],minY,maxY);
      if (d->pos[1]<minY-5 || d->pos[1]>maxY+5)
      {
        LogF(" *** clipping error");
      }
    }
  #endif
  #if 1
    saturate(d->pos[0],minX,maxX);
    saturate(d->pos[1],minY,maxY);
  #endif
}

void TLVertexTable::DoPerspective( const Camera &camera, ClipFlags clip )
{
  const int cb = -1;
  float minX = GEngine->MinSatX(), maxX = GEngine->MaxSatX();
  float minY = GEngine->MinSatY(), maxY = GEngine->MaxSatY();
  // 
  const Matrix4 *mc = &camera.Projection();
  #define ADJUSTED_CAMERA_MAT 0
  #if !ADJUSTED_CAMERA_MAT
  Matrix4 mcAdjusted;
  int zBias = GEngine->GetBias(cb);
  float zCoef = 1.0f-zBias*1e-6f;
  if (zBias>0)
  {
    float zMult=1, zAdd=0;
    GEngine->GetZCoefs(cb,zAdd,zMult);

    mcAdjusted = *mc;

    mcAdjusted(2,2) = mcAdjusted(2,2)*zMult+zAdd;
    mcAdjusted.SetPosition
    (
      Vector3
      (
        mcAdjusted.Position().X(),
        mcAdjusted.Position().Y(),
        mcAdjusted.Position().Z()*zMult
      )
    );

    mc = &mcAdjusted;
  }
  #endif

  int size=_posTrans.Size();
  TLVertex *d=_vert.Data();
  const Vector3 *s=_posTrans.Data();
  // many meshes are not clipped at all - skip check
  if( (clip&ClipAll)==0 )
  {
    while( --size>=0 )
    {
      #if _KNI || _VEC4 || _M_PPC
      // TODO: better t-math support
      Vector3 dt;
      float rhw = dt.SetPerspectiveProject(*mc,*s);
      (*d).pos[0] = dt[0];
      (*d).pos[1] = dt[1];
      (*d).pos[2] = dt[2];
      #else
      float rhw = (*d).pos.SetPerspectiveProject(*mc,*s);
      #endif
      d->rhw = rhw;
      // TODO: skip saturate if not neccessary here
      VerifyClip(d,minX,maxX,minY,maxY);
      /*
      saturate(d->pos[0],minX,maxX);
      saturate(d->pos[1],minY,maxY);
      */
      #if !ADJUSTED_CAMERA_MAT
        d->pos[2] *= zCoef;
      #endif
      d++,s++;
      // check z bias coef
    }
  }
  else
  {
    const ClipFlags *c=_clip.Data();
    while( --size>=0 )
    {
      if( ((*c)&ClipAll)==ClipNone )
      {
        #if _KNI || _VEC4 || _M_PPC
        Vector3 dt;
        float rhw = dt.SetPerspectiveProject(*mc,*s);
        (*d).pos[0] = dt[0];
        (*d).pos[1] = dt[1];
        (*d).pos[2] = dt[2];
        #else
        float rhw = (*d).pos.SetPerspectiveProject(*mc,*s);
        #endif
        d->rhw = rhw;
        // TODO: skip saturate if not necessary here
        VerifyClip(d,minX,maxX,minY,maxY);
        /*
        saturate(d->pos[0],minX,maxX);
        saturate(d->pos[1],minY,maxY);
        */
        #if !ADJUSTED_CAMERA_MAT
          d->pos[2] *= zCoef;
        #endif
      }
      else
      {
        // invalid vertices can be copied into the vertex buffer as well
        // be sure to provide some valid values for them to avoid NaN
        d->pos = Vector3P(0.5f,0.5f,1.0f);
        d->rhw = 1.0f;
      }
      c++,d++,s++;
    }
  }
}




// ligting and fogging

const float StartLights=0.01; // NightEffect when lights start to be visible

#define TL_COUNTERS 0

#define DP(a,b) ((a).X()*(b).X()+(a).Y()*(b).Y()+(a).Z()*(b).Z())
#define SIZE2(x,y,z) (Square(x)+Square(y)+Square(z))

inline PackedColor PackedColor255( ColorVal color, int a=255 )
{
  int r=toInt(color.R());
  int g=toInt(color.G());
  int b=toInt(color.B());
  saturate(r,0,255);
  saturate(g,0,255);
  saturate(b,0,255);
  return PackedColor((a<<24)|(r<<16)|(g<<8)|b);
}

void TLVertexTable::DoMaterialLightingP
(
  const TLMaterial &mat,
  const Matrix4 &worldToModel, const LightList &lights,
  const VertexTable &mesh, int beg, int end
)
{
  if( end<=beg ) return; // safety: nothing to light

  #if 1 //TL_COUNTERS
    ADD_COUNTER(TLMat,end-beg);
  #endif
  // perform lighting
  // first of all apply directional light to all normals
  // this can be done at TLVertexTable level
  LightSun *sun=GScene->MainLight();
  float addLightsFactor=sun->NightEffect();
  if ((mat.specFlags&DisableSun)==0)
  {
    if ((mat.specFlags&SunPrecalculated)==0)
    {
      sun->SetMaterial(mat, GEngine->GetAccomodateEye());
    }
    else
    {
      TLMaterial temp;
      temp.ambient = HBlack;
      temp.diffuse = HBlack;
      temp.emmisive = HBlack;
      temp.forcedDiffuse = HBlack;
      temp.specFlags = mat.specFlags;

      sun->SetMaterial(temp, GEngine->GetAccomodateEye());
    }
    if (lights.Size()>0)
    {
      TLMaterial temp; //=mat;
      temp.ambient = mat.ambient*addLightsFactor;
      temp.diffuse = mat.diffuse*addLightsFactor;
      temp.specFlags = mat.specFlags;
      temp.emmisive = mat.emmisive;
      temp.forcedDiffuse = mat.forcedDiffuse*addLightsFactor;
      //temp.specular = mat.saddLightsFactor;
      for( int index=0; index<lights.Size(); index++ )
      {
        lights[index]->SetMaterial(temp, GEngine->GetAccomodateEye());
        lights[index]->Prepare(worldToModel);
      }
    }
  }
  else
  {
    addLightsFactor = 1;

    TLMaterial temp;
    temp.ambient = HBlack;
    temp.diffuse = HBlack;
    temp.emmisive = HBlack;
    temp.forcedDiffuse = HBlack;
    temp.specFlags = mat.specFlags;

    sun->SetMaterial(temp, GEngine->GetAccomodateEye());
    if (lights.Size()>0)
    {
      for( int index=0; index<lights.Size(); index++ )
      {
        lights[index]->SetMaterial(mat, GEngine->GetAccomodateEye());
        lights[index]->Prepare(worldToModel);
      }
    }
  }
  
  const Camera *camera = GScene->GetCamera();
  Matrix4Val invScale=camera->InvScaleMatrix();

  // normal lighting
  // for all vertices in mesh calculate positional lights and fog
  // check for special case: no lights
  bool someLights=( addLightsFactor>=StartLights && lights.Size()>0 );
  // assume dammage value is constant over whole object
  Vector3 sunDirection=worldToModel.Rotate(sun->LightDirection());
  sunDirection.Normalize();

  // TODO: use material properties
  Color diffuse=sun->DiffusePrecalc();
  Color ambient=sun->AmbientPrecalc()+mat.emmisive*GEngine->GetAccomodateEye();

  int spec = mat.specFlags;
  float aFactor = 1.0f;
  if (spec&IsColored)
  {
    aFactor = GScene->GetConstantColor().A();
  }
    
  if( !someLights )
  {
    #if TL_COUNTERS
      ADD_COUNTER(TLNof,end-beg);
    #endif
    // calculate per vertex fog
    //int n=NVertex();
    const UVPairs &tex = mesh.GetUVArray();
    for( int i=beg; i<end; i++ )
    {
      Coord cosFi = sunDirection * mesh.Norm(i).Get();
      saturateMax(cosFi,0);
      Color color = ambient+diffuse*cosFi;
      
      TLVertex &v=SetVertex(i);
      //ClipFlags clip=Clip(i);
      Vector3Val scalePos=TransPos(i);
      float dist2 = SIZE2( scalePos.X()*invScale(0,0),scalePos.Y()*invScale(1,1),scalePos.Z() );
      float fog=1.0f-GScene->Fog8(dist2)*(1.0f/255);
      // alpha is assumed 1 - this is always true for normal lighting
      PackedColor specular;
      if (spec&IsAlphaFog)
      {
        // IsAlphaFog
        fog = fog*aFactor;
        specular = PackedBlack;
      }
      else
      {
        // in some old implementations specular.A was the fog value
        specular = PackedColorRGB(Color(0,0,0,fog));
        // we still want to keep alpha
        fog = aFactor;
      }

      color.SetA(fog);
      // make sure we use the full 0..1 range as well as possible
      color.NormalizeColor();
#if _VBS3
      v.color=PackedColorRGB(color, color.A8());
#else
      v.color=PackedColorRGB(color);
#endif
      v.specular=specular;
      v.t0 = v.t1 = tex.UV(i);
      // check if there are not some unsupported lighting flags
    }
  }
  else
  {
    #if TL_COUNTERS
      ADD_COUNTER(TLNoL,end-beg);
    #endif

    const UVPairs &tex = mesh.GetUVArray();
    for( int i=beg; i<end; i++ )
    {
      Coord cosFi = sunDirection * mesh.Norm(i).Get();
      saturateMax(cosFi,0);
      Color color = ambient+diffuse*cosFi;
      
      TLVertex &v=SetVertex(i);

      Vector3 norm = mesh.Norm(i).Get();
      Vector3Val pos=mesh.Pos(i);
      for( int index=0; index<lights.Size(); index++ )
      {
        color+=lights[index]->ApplyPrecalc(pos,norm);
      }
      // lighting with normal defined
      // check if there are not some unsupported lighting flags
      Vector3Val scalePos=TransPos(i);
      float dist2 = SIZE2( scalePos.X()*invScale(0,0),scalePos.Y()*invScale(1,1),scalePos.Z() );
      float fog=1.0f-GScene->Fog8(dist2)*(1.0f/255);
      // alpha is assumed 1 - this is always true for normal lighting
      PackedColor specular;
      if (spec&IsAlphaFog)
      {
        // IsAlphaFog
        fog = fog*aFactor;
        specular = PackedBlack;
      }
      else
      {
        // in some old implementations specular.A was the fog value
        specular = PackedColorRGB(Color(0,0,0,fog));
        // we still want to keep alpha
        fog = aFactor;
      }

      color.SetA(fog);
      // make sure we use the full 0..1 range as well as possible
      color.NormalizeColor();
#if _VBS3
      v.color=PackedColorRGB(color, color.A8());
#else
      v.color=PackedColorRGB(color);
#endif
      v.specular=specular;
      v.t0 = v.t1 = tex.UV(i);
    }
  }
}

/*!
\patch 5131 Date 2/21/2007 by Flyman
- Fixed: Tracers are visible again early in the night
*/
void TLVertexTable::DoLineLighting
(
  const VertexTable &mesh, int spec
)
{
  //LightSun *sun=GScene->MainLight();
  // perform lighting
  // first of all apply directional light to all normals
  // this can be done at TLVertexTable level
  //
  int i;
  Matrix4Val invScale=GScene->GetCamera()->InvScaleMatrix();
  //Landscape *land=GScene->GetLandscape();
  // calculate common alpha correction
  float alphaCorrection=
  (
    // resolution correction
    GEngine->Width()*(1.0/640)*
    // zoom correction
    GScene->GetCamera()->InvLeft()
  );

  ColorVal lineColor=GScene->GetConstantColor();
  float width=lineColor.A(); // width (in m?)

  Assert( (GetAndHints()&ClipLightMask)==ClipLightLine );

  LightSun *sun = GScene->MainLight();
  Color lightAmbient = sun->SimulateDiffuse(0.5f) * GEngine->GetAccomodateEye();
  
  // Include emmisive color
  Color emmisive;
  if (spec&TracerLighting)
  {
    const float emmisiveCoef = 0.5f;
    emmisive = lineColor * Color(emmisiveCoef, emmisiveCoef, emmisiveCoef, 0.0f) * GEngine->GetAccomodateEye();
  }
  else
  {
    emmisive = Color(0.0f, 0.0f, 0.0f, 0.0f);
  }

  const int size=NVertex();
  const UVPairs &tex = mesh.GetUVArray();
  Assert (_posTrans.Size()==size);
  for( i=0; i<size; i++ )
  {
    TLVertex &v=SetVertex(i);
    Vector3Val scalePos=TransPos(i);
    float dist2 = SIZE2
    (
      scalePos.X()*invScale(0,0),scalePos.Y()*invScale(1,1),scalePos.Z()
    );
    // scale alpha with distance
    // correct it for finer resolutions
    float invCoef = 100.0/3;
    if (dist2>Square(3))
    {
      invCoef = 100*InvSqrt(dist2);
    }
    float a=alphaCorrection*width*invCoef;
    saturate(a,0,1);
    //LogF("Line point %.2f,%.2f - alpha %.2f, width %.2f",v.pos.X(),v.pos.Y(),a,width);

    // Get the color used to lit the vertex
    Color lineColorLit = lineColor * lightAmbient + emmisive;
    lineColorLit.SetA(a);

    // Normalize color (increment alpha instead of having color over 1)
    lineColorLit.NormalizeColor();

    v.color=PackedColor(lineColorLit);
    
    v.specular=PackedBlack;
    v.t0 = v.t1 = tex.UV(i);
  }
}

void TLVertexTable::DoLighting(
  ColorVal animColor,
  const Matrix4 &worldToModel, const LightList &lights,
  const Shape &mesh, int spec
)
{
  // note: worldToModel matrix is transformation from world space to model space
  // it is used to transform lights before applying them
  // so that lights can be applied in model space
  const int size=NVertex();

  if( size<=0 ) return; // nothing to light

  #if TL_COUNTERS
    ADD_COUNTER(TLTot,size);
  #endif
  
  if
  (
    (spec&IsShadow)==0 &&
    (GetOrHints()&(ClipLightMask|ClipFogMask))==(ClipLightNormal|ClipFogNormal)
  )
  {
    TLMaterial mat;
    CreateMaterial(mat,animColor);
    mat.specFlags = spec;

    DoMaterialLightingP(mat,worldToModel,lights,mesh,0,size);
  }
  else if( spec&IsShadow )
  { // shadows need not be lighted
    Fail("SW TL shadows no longer supported");
  }
  else
  {
    ClipFlags globalLight=GetAndHints()&ClipLightMask;
    if( globalLight!=(GetOrHints()&ClipLightMask) ) globalLight=0;

    switch( globalLight )
    {
      case ClipLightLine:
        DoLineLighting(mesh,spec);
      return;
    }

    #if TL_COUNTERS
      ADD_COUNTER(TLChk,size);
    #endif

    Fail("Mixed SW ligting - obsolete");

    TLMaterial mat;
    CreateMaterial(mat,animColor);
    mat.specFlags = spec;

    DoMaterialLightingP(mat,worldToModel,lights,mesh,0,size);
  }
}


__forceinline ClipFlags NeedsClippingInline( Vector3Par point, const Camera &camera )
{
  // check _posTrans for all six clipping planes
  float cNear=camera.ClipNear();
  float cFar=camera.ClipFar();
  ClipFlags flags=ClipNone;
  if( point.Z()<cNear ) flags|=ClipFront; // front clipping plane
  if( point.Z()>cFar ) flags|=ClipBack; // back clipping plane

  if( point.Z()<-point.X() ) flags|=ClipLeft; // left clipping plane
  if( point.Z()<+point.X() ) flags|=ClipRight; // right clipping plane
  
  if( point.Z()<-point.Y() ) flags|=ClipTop; // top clipping plane
  if( point.Z()<+point.Y() ) flags|=ClipBottom; // bottom clipping plane
  /*
  if( camera.IsUserClip() )
  {
    // check user clipping plane
    if( point*camera.UserClipDir()+camera.UserClipVal()<0 )
    {
      flags|=ClipUser0;
    }
  }
  */
  return flags;
  
}

ClipFlags NeedsClipping( Vector3Par point, const Camera &camera )
{
  return NeedsClippingInline(point,camera);
}

#define STATS 0

ClipFlags TLVertexTable::CheckClipping( const Camera &camera, ClipFlags mask, ClipFlags &andClip )
{
  int size = _clip.Size();
  #if STATS
    static int Done,Skipped;
    static int ClippedAll,ClippedNone;
    int count = size;
    int maxCount = 100000;
    //int count = 1;
    //int maxCount = 1000;
  #endif
  andClip=mask;
  if( !mask )
  {
    for( int i=0; i<size; i++ )
    {
      _clip[i]&=ClipHints|ClipUserMask;
    }
    #if STATS
      Skipped+=count;
    #endif
    return ClipNone;
  }
  else
  {
    ClipFlags orClip=ClipNone;
    for( int i=0; i<size; i++ )
    {
      ClipFlags clip=_clip[i];
      Vector3Val point=_posTrans[i];
      ClipFlags hints=clip&(ClipHints|ClipUserMask);
      //clip&=NeedsClipping(point,camera)&mask;
      ClipFlags clipRes = NeedsClippingInline(point,camera);
      clip&=clipRes&mask;
      orClip|=clip;
      andClip&=clip;
      _clip[i]=clip|hints;
    }
    #if STATS
      if( andClip )
      {
        ClippedAll+=count;
        /*
        if (count>100)
        {
          LogF("Late full clipping %d",count);
        }
        */
      }
      else if( !orClip )
      {
        ClippedNone+=count;
        /*
        if (count>100)
        {
          LogF("Late no clipping %d",count);
        }
        */
      }
      Done+=count;
      if( Done>=maxCount )
      {
        LogF
        (
          "Skipped ratio %.2f, noClip %.2f, fullClip %.2f",
          (float)Skipped/(Done+Skipped),
          (float)ClippedNone/Done,(float)ClippedAll/Done
        );
        Done=Skipped=0;
        ClippedAll=ClippedNone=0;
      }
    #endif
    return orClip;
  }
}


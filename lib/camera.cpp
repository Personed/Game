// Poseidon - camera
// (C) 1997, SUMA
#include "wpch.hpp"
#include "textbank.hpp"

#include "camera.hpp"
#include "engine.hpp"
#include <Es/Framework/debugLog.hpp>

// frame interface

Camera::Camera()
://_userClip(-1),
//_perspective(MIdentity),
_projection(MIdentity),
_projectionNormal(MIdentity),
_scale(MIdentity),_invScale(MIdentity),
_scaledInvTransform(MIdentity),
_camNormalTrans(M3Identity),
_aotFactor(1),_aotInvFactor(1),
_speed(VZero),
_camInvTrans(MIdentity),
_cNear(1),_cFar(100),
_cLeft(1),_invCLeft(1),
_cTop(1),_invCTop(1),
_cFarShadow(1),
_cFarSecShadow(1),
_cFarFog(1)
{
}

void Camera::SetPerspective(
  Coord cNear, Coord cFar, Coord cLeft, Coord cTop,
  Coord cFarShadow, Coord cFarSecShadow, Coord cFarFog
)
{
  const float maxLeft = 10;
#if _VBS3 //support higher zoom than 25x
  const float minLeft = 0.001f;
#else
  const float minLeft = 0.01f;
#endif
  // left is almost always wider than top - no need to test top
  if (cLeft>maxLeft)
  {
    cLeft = maxLeft;
    cTop = maxLeft*cTop/cLeft;
    LogF("Camera too wide %.3f/%.3f",cLeft,cNear);
  }
  if (cLeft<minLeft)
  {
    cLeft = minLeft;
    cTop = minLeft*cTop/cLeft;
    LogF("Camera too narrow %.3f/%.3f",cLeft,cNear);
  }
    
  _cNear=cNear,_cFar=cFar,_cLeft=cLeft,_cTop=cTop;
  _cFarShadow = cFarShadow;
  _cFarSecShadow = cFarSecShadow;
  _cFarFog = cFarFog;
}

/**
@param engine optional - when NULL, engine is not adjusted. This is used in preloads, where we want to compute camera,
but no engine adjustment is needed, as we will never render using such camera.
*/

void Camera::Adjust( Engine *engine )
{
  //_perspective=Matrix4(MPerspective,1,1);
  _invCLeft=1/_cLeft;
  _invCTop=1/_cTop;

  _projectionNormal = MZero;
  _projectionNormal(0,0) = _invCLeft;
  _projectionNormal(1,1) = _invCTop;
  // check engine abilities
  float ccFar = floatMax(500,_cFar*1.01);
  if (engine && engine->HasWBuffer() && engine->IsWBuffer())
  {
    // far is critical for precision of w-buffer
    ccFar = floatMax(100,_cFar*1.1);
  }


  float q = ccFar/(ccFar-_cNear);
  _projectionNormal(2,2) = q;
  _projectionNormal.SetPosition(Vector3(0,0,-q*_cNear));

  _projection.SetZero();
  // see DX docs: What Is the Projection Transformation?
  // (note: DX uses row vectors)
  // our setup of DX matrix
  // |1/fovW     0   0    0    |   |x|   |   don't care     |
  // |   0    1/fowH 0    0    |   |y|   |   don't care     |
  // |   0       0   q -q*cNear| * |z| = | q*z - q*cNear    |
  // |   0       0   1    0    |   |1|   |       z          |

  // perspective:       z = (q*z - q*cNear)/z = q - q*cNear/z

  // while we are actually rendering into a engine->Width(), engine->Height() window,
  // the vertices will be transformed back into -1-+1 window anyway
  // the transformation is done using WidthBB / WidthHH (see the renderer's AddVertices)
  
  // TODO: use -1 ... +1 for addressing
  
  // when engine not provided, use default, so that _aotFactor / _invAotFactor are computed correctly
  int w = engine ? engine->WidthBB() : GEngine->WidthBB();
  int h = engine ? engine->HeightBB() : GEngine->HeightBB();
  // y is negative - positive should go up
  // cLeft and cTop is ignored now - therefore sides of viewing frustum is viewport independent
  // this is used to simplify clipping tests

  // our setup of custom matrix
  // |  w/2   0   w/2   0    |   |x|   |  don't care      |
  // |   0   h/2  h/2   0    |   |y|   |  don't care      |
  // |   0    0    q -q*cNear| * |z| = | q*z - q*cNear    |
  // |   0    0    1    0    |   |1|   |       z          |

  // guarantee sky and clouds are visible
  // TODO: separate camera settings for sky & clouds
  //  direct projection construction
  _projection(0,0) =  w*0.5f;
  _projection(1,1) = -h*0.5f;
  _projection(2,2) = q;
  _projection(0,2) =  w*0.5f;
  _projection(1,2) =  h*0.5f;
  _projection.SetPosition(Vector3(0,0,-q*_cNear));

  _scale=Matrix4(MScale,_invCLeft,_invCTop,1);
  // after easy clipping rescale viewing frustum to get correct screen coordinates
  _invScale=Matrix4(MScale,_cLeft,_cTop,1);

  _scaledInvTransform=_scale*GetInvTransform();
  _camNormalTrans=Matrix3(MNormalTransform,_scaledInvTransform.Orientation());
  #if 0
    // TODO: BUG: _scaledInvTransform is sometimes not SR
    float mScale = _scaledInvTransform.Scale();
    _scaledInvTransform.SetUpAndAside
    (
      _scaledInvTransform.DirectionUp(),_scaledInvTransform.DirectionAside()
    );
    _scaledInvTransform.SetScale(mScale);
  #endif

  _camInvTrans=Matrix4(MInverseScaled,_scaledInvTransform);

  // calculate world space clipping planes
  Vector3 pl(+1,0,_cLeft);
  Vector3 pt(0,+1,_cTop);
  pl.Normalize();
  pt.Normalize();
  Vector3 pr(-pl[0],0,pl[2]);
  Vector3 pb(0,-pt[1],pt[2]);

  Vector3Val pos = Position();

  DirectionModelToWorld(pr,pr);
  DirectionModelToWorld(pl,pl);
  DirectionModelToWorld(pb,pb);
  DirectionModelToWorld(pt,pt);

  _rClipPlane=Plane(pr,pos);
  _lClipPlane=Plane(pl,pos);
  _tClipPlane=Plane(pt,pos);
  _bClipPlane=Plane(pb,pos);

  _nClipPlane = Plane(Direction(),pos+Direction()*_cNear);
  _fClipPlane = Plane(-Direction(),pos+Direction()*_cFar);
  _fClipPlaneShadow = Plane(-Direction(),pos+Direction()*_cFarShadow);
  _fClipPlaneSecShadow = Plane(-Direction(),pos+Direction()*_cFarSecShadow);
  
  if (engine) engine->ProjectionChanged(-1);
  
  // projection matrix contains only a half of the screen, same way as _invCLeft/_invCTop
  // y component is reversed (negative), hence the fabs to fix it
  _aotFactor = fabs(_projection(0,0)*_projection(1,1))*_invCLeft*_invCTop;
  _aotInvFactor = 1/_aotFactor;
}

void Camera::SetClipRange(
  Coord cNear, Coord cFar, Coord cFarShadow, Coord cFarSecShadow, Coord cFarFog
)
{
  _cNear = cNear;
  _cFar = cFar;
  _cFarShadow = cFarShadow;
  _cFarSecShadow = cFarSecShadow;
  _cFarFog = cFarFog;
  Adjust(GEngine);
}

#pragma optimize("t",on) // optimize for speed instead of size
#if !_RELEASE
  // it seems in VS 2005 optimize("t",on) applies /Oy as well (Omit frame pointers)
  // in Testing we need stack frames for memory call-stack and crash-dumps
  #pragma optimize("y",off)
#endif

inline ClipFlags Camera::IsClippedCustomBackPlane
(
  Vector3Par point, float radius, const Plane &backPlane, ClipFlags *mayBeClipped
) const
{
  ClipFlags clip=ClipNone;

  float nClipPlaneDistance = _nClipPlane.Distance(point);
  float fClipPlaneDistance = backPlane.Distance(point);
  float rClipPlaneDistance = _rClipPlane.Distance(point);
  float lClipPlaneDistance = _lClipPlane.Distance(point);
  float tClipPlaneDistance = _tClipPlane.Distance(point);
  float bClipPlaneDistance = _bClipPlane.Distance(point);

  if( nClipPlaneDistance<=-radius ) clip|=ClipFront;
  if( fClipPlaneDistance<=-radius ) clip|=ClipBack;
  if( rClipPlaneDistance<=-radius ) clip|=ClipRight;
  if( lClipPlaneDistance<=-radius ) clip|=ClipLeft;
  if( tClipPlaneDistance<=-radius ) clip|=ClipTop;
  if( bClipPlaneDistance<=-radius ) clip|=ClipBottom;

  if (mayBeClipped)
  {
    // no clip guaranteed in given axis
    ClipFlags noClip=ClipNone;
    if( nClipPlaneDistance>+radius ) noClip|=ClipFront;
    if( fClipPlaneDistance>+radius ) noClip|=ClipBack;
    if( rClipPlaneDistance>+radius ) noClip|=ClipRight;
    if( lClipPlaneDistance>+radius ) noClip|=ClipLeft;
    if( tClipPlaneDistance>+radius ) noClip|=ClipTop;
    if( bClipPlaneDistance>+radius ) noClip|=ClipBottom;
    *mayBeClipped = noClip^ClipAll;
  }
  
  return clip;
}

inline ClipFlags Camera::IsInboundCustomBackPlane(Vector3Par dir, const Plane &backPlane) const
{
  float nClipPlaneInbound = _nClipPlane.Normal()*dir;
  float fClipPlaneInbound = backPlane.Normal()*dir;
  float rClipPlaneInbound = _rClipPlane.Normal()*dir;
  float lClipPlaneInbound = _lClipPlane.Normal()*dir;
  float tClipPlaneInbound = _tClipPlane.Normal()*dir;
  float bClipPlaneInbound = _bClipPlane.Normal()*dir;
  ClipFlags clip = 0;
  if( nClipPlaneInbound<0 ) clip|=ClipFront;
  if( fClipPlaneInbound<0 ) clip|=ClipBack;
  if( rClipPlaneInbound<0 ) clip|=ClipRight;
  if( lClipPlaneInbound<0 ) clip|=ClipLeft;
  if( tClipPlaneInbound<0 ) clip|=ClipTop;
  if( bClipPlaneInbound<0 ) clip|=ClipBottom;
  return clip;  
}

inline bool Camera::IsClippedCustomBackPlane(Vector3Par p1, Vector3Par p2, float radius, const Plane &backPlane) const
{
  float t1 = 0, t2 = 1;
  backPlane.Clip(t1,t2,p1,p2,radius);
  if (t1>=t2) return true;
  _nClipPlane.Clip(t1,t2,p1,p2,radius);
  if (t1>=t2) return true;
  _rClipPlane.Clip(t1,t2,p1,p2,radius);
  if (t1>=t2) return true;
  _lClipPlane.Clip(t1,t2,p1,p2,radius);
  if (t1>=t2) return true;
  _tClipPlane.Clip(t1,t2,p1,p2,radius);
  if (t1>=t2) return true;
  _bClipPlane.Clip(t1,t2,p1,p2,radius);
  if (t1>=t2) return true;
  return false;
}

ClipFlags Camera::IsClippedShadow(Vector3Par point, float radius, ClipFlags *mayBeClipped) const
{
  return IsClippedCustomBackPlane(point,radius,_fClipPlaneShadow,mayBeClipped);
}
ClipFlags Camera::IsClippedShadowCustomBackPlane(Vector3Par point, float radius, ClipFlags *mayBeClipped, float shadowsZ) const
{
  Plane customZPlane = Plane(-Direction(),Position()+Direction()*shadowsZ);
  return IsClippedCustomBackPlane(point,radius,customZPlane,mayBeClipped);
}
ClipFlags Camera::IsInboundShadow(Vector3Par dir) const
{
  return IsInboundCustomBackPlane(dir,_fClipPlaneShadow);
}
ClipFlags Camera::IsInboundShadowCustomBackPlane(Vector3Par dir, float shadowsZ) const
{
  Plane customZPlane = Plane(-Direction(),Position()+Direction()*shadowsZ);
  return IsInboundCustomBackPlane(dir,customZPlane);
}
bool Camera::IsClippedShadow(Vector3Par p1, Vector3Par p2, float radius) const
{
  return IsClippedCustomBackPlane(p1,p2,radius,_fClipPlaneShadow);
}
bool Camera::IsClippedShadowCustomBackPlane(Vector3Par p1, Vector3Par p2, float radius, float shadowsZ) const
{
  Plane customZPlane = Plane(-Direction(),Position()+Direction()*shadowsZ);
  return IsClippedCustomBackPlane(p1,p2,radius,customZPlane);
}

ClipFlags Camera::IsClippedSecShadow(Vector3Par point, float radius, ClipFlags *mayBeClipped) const
{
  return IsClippedCustomBackPlane(point,radius,_fClipPlaneSecShadow,mayBeClipped);
}
ClipFlags Camera::IsInboundSecShadow(Vector3Par dir) const
{
  return IsInboundCustomBackPlane(dir,_fClipPlaneSecShadow);
}
bool Camera::IsClippedSecShadow(Vector3Par p1, Vector3Par p2, float radius) const
{
  return IsClippedCustomBackPlane(p1,p2,radius,_fClipPlaneSecShadow);
}

#if _ENABLE_CHEATS
//#include <El/Statistics/statistics.hpp>
#include "keyInput.hpp"
#include "dikCodes.h"
#endif

inline float	AABPlaneDist(const Plane& pl, Vector3Par mins, Vector3Par maxs)
{
  float nx = pl.Normal().X();
  float ny = pl.Normal().Y();
  float nz = pl.Normal().Z();

//  float px = (nx < 0) ? mins.x : maxs.x;
 // float py = (ny < 0) ? mins.y : maxs.y;
 // float pz = (nz < 0) ? mins.z : maxs.z;

  return pl.Distance(Vector3((nx < 0) ? mins.X() : maxs.X(), (ny < 0) ? mins.Y() : maxs.Y(), (nz < 0) ? mins.Z() : maxs.Z()));
}

ClipFlags Camera::IsClipped(Matrix4Par modelToWorld, const Vector3 *minmax, ClipFlags &mayBeClipped) const
{
  #if _ENABLE_CHEATS
  //static StatEventRatio clipped("BB Clip");
  
  static bool enable = true;
  if (GInput.GetCheat1ToDo(DIK_EQUALS))
  {
    enable= !enable; 
    DIAG_MESSAGE(1000,RString("BBox clipping ")+RString(enable ? "On" : "Off"));
  }
  if (!enable) return 0;
  #endif
  ClipFlags wholeClip = 0;
  ClipFlags noClip = 0;
  Vector3 halfDistances = (minmax[1]-minmax[0])*0.5f;
  Vector3 center = modelToWorld.FastTransform((minmax[1]+minmax[0])*0.5f);
  Vector3 vx = modelToWorld.DirectionAside();
  Vector3 vy = modelToWorld.DirectionUp();
  Vector3 vz = modelToWorld.Direction();
  // Do a SAT test along each frustum axis.
  const Plane *planes[] = {&_nClipPlane,&_fClipPlane,&_lClipPlane,&_rClipPlane,&_bClipPlane,&_tClipPlane};
  static const ClipFlags clipPlanes[] = {ClipFront,ClipBack,ClipLeft,ClipRight,ClipBottom,ClipTop};
  /* use separating axis: http://www.gamedev.net/topic/539116-optimized-obb-frustum/page__view__findpost__p__4479551
  there's a lot cleaner way to test oriented bounding box against frustum planes
  if box is defined by it's center [p] and 3 direction vectors spanning the box [vx,vy,vz] then you can test this box against plane like this
  calculate d = distance from box center to the plane
  calculate sx= absolute value of length of projection of vx on planes normal
  calculate sy= absolute value of length of projection of vy on planes normal
  calculate sz= absolute value of length of projection of vz on planes normal
  if d>sx+sy+sz then box is fully on one side of plane
  if d<-sx-sy-sz box is fully on other side of plane
  otherwise it intersects plane 
  */
  for(int i = 0; i < 6; i++)
{
    ClipFlags clipI = 1<<i;
    Assert(clipI==clipPlanes[i]);
    if (mayBeClipped&clipI)
{
      // Find the half-diagonal which is most aligned with the plane normal
      float sx = fabs(vx * planes[i]->Normal() * halfDistances[0]);
      float sy = fabs(vy * planes[i]->Normal() * halfDistances[1]);
      float sz = fabs(vz * planes[i]->Normal() * halfDistances[2]);
      float dist = planes[i]->Distance(center);
      if (dist<=-sx-sy-sz)
        wholeClip |= clipI;
      if (dist>=+sx+sy+sz)
        noClip |= clipI;
  }
}

  mayBeClipped &= ~noClip;
  #if _ENABLE_CHEATS
  //clipped.Count(andClip!=0);
  #endif
  return wholeClip;
}

ClipFlags Camera::IsClipped(const Vector3 *minmax, ClipFlags mayBeClipped) const
{
#if _ENABLE_CHEATS
	//static StatEventRatio clipped("BB Clip");

	static bool enable = true;
	if (GInput.GetCheat1ToDo(DIK_EQUALS))
	{
		enable= !enable; 
		DIAG_MESSAGE(1000,RString("BBox clipping ")+RString(enable ? "On" : "Off"));
	}
	if (!enable) return 0;
#endif
	ClipFlags andClip = mayBeClipped;

		ClipFlags clip = 0;
		// no need to test planes which we know cannot clip
		if( (andClip&ClipFront)!=0 && AABPlaneDist(_nClipPlane, minmax[0], minmax[1])<=0 ) clip|=ClipFront;
		if( (andClip&ClipBack)!=0 && AABPlaneDist(_fClipPlane, minmax[0], minmax[1])<=0 ) clip|=ClipBack;

		if( (andClip&ClipRight)!=0 && AABPlaneDist(_rClipPlane, minmax[0], minmax[1])<=0 ) clip|=ClipRight;
		if( (andClip&ClipLeft)!=0 && AABPlaneDist(_lClipPlane, minmax[0], minmax[1])<=0 ) clip|=ClipLeft;

		if( (andClip&ClipTop)!=0 && AABPlaneDist(_tClipPlane, minmax[0], minmax[1])<=0 ) clip|=ClipTop;
		if( (andClip&ClipBottom)!=0 && AABPlaneDist(_bClipPlane, minmax[0], minmax[1])<=0 ) clip|=ClipBottom;

		andClip &= clip;
#if _ENABLE_CHEATS
	//clipped.Count(andClip!=0);
#endif
	return andClip;
}

ClipFlags Camera::IsClipped(
  Vector3Par point, float radius,
  Matrix4Par modelToWorld, const Vector3 *minmax
) const
{
  ClipFlags isClipped,mayBeClipped;
  isClipped = IsClipped(point,radius,&mayBeClipped);
  return isClipped | (mayBeClipped & IsClipped(modelToWorld,minmax,mayBeClipped));
}

bool Camera::IsClippedByAnyPlane(
  Vector3Par point, float radius,
  Matrix4Par modelToWorld, const Vector3 *minmax, ClipFlags &mayBeClipped
) const
{
  ClipFlags isClipped;
  isClipped = IsClipped(point,radius,&mayBeClipped);
  if (isClipped) return true;
  if (mayBeClipped==0) return false;
  return IsClipped(modelToWorld,minmax,mayBeClipped)!=0;
}

float Camera::LeftByClipping(
  Vector3Par point, float radius,
  Matrix4Par modelToWorld, const Vector3 *minmax
) const
{
  Fail("Unused - inefficient, once used, consider replacing with SAT");
  ClipFlags isClipped,mayBeClipped;
  isClipped = IsClipped(point,radius,&mayBeClipped);
  // handle singular cases - we know all is on-screen/off-screen
  if (isClipped!=0) return 0;
  if (mayBeClipped==0) return 1;
  
  // check all 8 corners of the min-max box
  // this requires 8x6 = 48 tests
  const Plane *clipPlanes[3][2]={
    {&_nClipPlane,&_fClipPlane},
    {&_rClipPlane,&_lClipPlane},
    {&_tClipPlane,&_bClipPlane}
  };
  // compute max. distance from clipping planes
  InitValT<float,int> clipDistMax[3][2];
  InitValT<float,int> clipDistMin[3][2];
  for (int x=0; x<2; x++) for (int y=0; y<2; y++) for (int z=0; z<2; z++)
  {
    Vector3 modelPoint(minmax[x].X(),minmax[y].Y(),minmax[z].Z());
    Vector3 point(VFastTransform,modelToWorld,modelPoint);
    for (int c=0; c<3; c++) // TODO: unroll
    {
      // for each plane we are finding the most negative values - those are the most clipped
      // and the most positive values - this is how much is unclipped by the plane
      // TODO: do not check non-clipping planes, use clip-flags array
      float distanceNeg = clipPlanes[c][0]->Distance(point);
      float distancePos = clipPlanes[c][1]->Distance(point);
      saturateMax(clipDistMax[c][0],distanceNeg);
      saturateMax(clipDistMax[c][1],distancePos);
      saturateMin(clipDistMin[c][0],distanceNeg);
      saturateMin(clipDistMin[c][1],distancePos);
    }
  }
  // convert from distances from frustum into axis aligned distances
  // distance between negClip .. posClip is how much is outside
  float insideVolume = 1;
  float totalVolume = 1;
  for (int c=0; c<3; c++)
  {
    // for each axis check how much is outside
    float total0 = clipDistMax[c][0]-clipDistMin[c][0];
    float total1 = clipDistMax[c][1]-clipDistMin[c][1];
    float out0 = -clipDistMin[c][0];
    float out1 = -clipDistMin[c][1];

    totalVolume *= total0;
    totalVolume *= total1;
    
    insideVolume *= total0-out0;
    insideVolume *= total1-out1;
  }
  // avoid division if possible
  if (insideVolume<=0)
  {
    Assert(IsClipped(modelToWorld,minmax,mayBeClipped)!=0);
    return 0;
  }
  if (insideVolume>=totalVolume) return 1;
  return insideVolume/totalVolume;

}


ClipFlags Camera::MayBeClipped( Vector3Par point, float radius) const
{
  ClipFlags clip=ClipAll;
  float dn = _nClipPlane.Distance(point);
  float df = _fClipPlane.Distance(point);
  if( dn>=+radius ) clip&=~ClipFront;
  if( df>=+radius ) clip&=~ClipBack;
  
  // we know we are clipping
  // check against guard planes to known which planes we must clip

  float gr = _rClipPlane.Distance(point);
  float gl = _lClipPlane.Distance(point);
  float gt = _tClipPlane.Distance(point);
  float gb = _bClipPlane.Distance(point);
  if( gr>=+radius ) clip&=~ClipRight;
  if( gl>=+radius ) clip&=~ClipLeft;
  if( gt>=+radius ) clip&=~ClipTop;
  if( gb>=+radius ) clip&=~ClipBottom;

  return clip;
}

#pragma optimize( "", on ) // restore default setting


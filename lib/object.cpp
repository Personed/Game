/*!
\file
Implementation for Object class
Implements general and  drawing functions
*/
// (C) 1997, SUMA
#include "wpch.hpp"


#include "object.hpp"
#include "tlVertex.hpp"
#include "scene.hpp"
#include "engine.hpp"
#include "camera.hpp"
#include "landscape.hpp"
#include "lights.hpp"
#include "rtAnimation.hpp"
#include <El/Common/randomGen.hpp>
#include "global.hpp"
#include "keyInput.hpp"
#include "dikCodes.h"
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <Es/ReportStack/reportStack.hpp>
#include <El/ParamFile/paramFile.hpp>
#include <El/ParamArchive/paramArchive.hpp>
#include <El/Enum/enumNames.hpp>
#include "timeManager.hpp"
#include "diagModes.hpp"
#include "Shape/material.hpp"
#include <Es/Files/filenames.hpp>
#include "diagModes.hpp"
#include "objDestroy.hpp"
#include "rtAnimation.hpp"
#include "objectClasses.hpp"

#include "Shape/specLods.hpp"
#include "Network/network.hpp"
#include "AI/ai.hpp"

#include "stringtableExt.hpp"
#include "landscape.hpp"
#include "txtPreload.hpp"

#include "gameStateExt.hpp"
#include "drawText.hpp"

#include "Physics/enf_worldphysics.h"
#include "baseWorld/enf_shapes.h"

#if _VBS3
#include "soldierOld.hpp"
#endif

#ifdef PREPROCESS_DOCUMENTATION
#include "math3d.hpp"
#endif

#if _RELEASE || !_ENABLE_CHEATS
  #define STARS 0
  #define SHOT_STARS 0
#else
  #define STARS 0
  #define SHOT_STARS 1
#endif

#define PERF_ISECT 0

#if _ENABLE_PHYSICS
#include "BulletCollision\CollisionDispatch\btCollisionObject.h"
const float ENF_TEMPPHYS_LIFETIME = 10.5f;
#endif


#if _DEBUG
void VerifyMatrixArrayWasSet(Matrix4Array &matrices, const LODShape *shape, int level)
{
  for (int i = 0; i < matrices.Size(); i++)
  {
    if (fabs(matrices[i].Position().X()) > 1000000)
    {
      DoAssert(shape->IsLevelLocked(level));
      const Shape *shapeL = shape->GetLevelLocked(level);
      SubSkeletonIndex ssi;
      SetSubSkeletonIndex(ssi, i);
      SkeletonIndex si = shapeL->GetSkeletonIndexFromSubSkeletonIndex(ssi);
      RStringB boneName = shape->GetSkeleton()->GetBone(si);
      RptF("Error: Model %s LOD %f is referencing bone %s which has not been set during preparation", shape->Name(), shape->Resolution(level), boneName.Data());
      Fail("Error: Model is referencing bone which has not been set during preparation");
    }
  }
}
#else
void VerifyMatrixArrayWasSet(Matrix4Array &matrices, const LODShape *shape, int level)
{
}
#endif

static const EnumName SideNames[]=
{
  EnumName(TWest,"WEST"),
  EnumName(TEast,"EAST"),
  EnumName(TGuerrila,"GUER"),
  EnumName(TCivilian,"CIV"),
  EnumName(TSideUnknown,"UNKNOWN"),
  EnumName(TEnemy,"ENEMY"),
  EnumName(TFriendly,"FRIENDLY"),
  EnumName(TLogic,"LOGIC"),
  EnumName(TEmpty,"EMPTY"),
  EnumName(TAmbientLife,"AMBIENT LIFE"),
  EnumName()
};
template<>
const EnumName *GetEnumNames(TargetSide dummy)
{
  return SideNames;
}

#include "Shape/mapTypes.hpp"

enf::PhysicsWorld* Object::GetWorldPhysics() const
{
  if(GLandscape)
    return GLandscape->GetPhysicsWorld();
  return NULL;
}

enf::PhysicsController* Object::GetPhysics() const
{
  return _pPhysics;
}

Landscape* Object::GetWorld()
{
	return GLandscape;
}

#if _ENABLE_PHYSICS
//static int s_physCreated = 0;
//static int s_physRemoved = 0;
//static int s_physTmp = 0;
void Object::RemovePhysics()
{ 
    if(GetPhysics())
    {
			if(_pPhysics->IsDynamic())
				((enf::BulletController*)_pPhysics)->GetCollisionObject()->setUserPointer(0);
      enf::PhysicsWorld* pworld = (enf::PhysicsWorld*)GLandscape->GetPhysicsWorld(); 
			pworld->DestroyBody(this);
			_pPhysics = 0;
			//++s_physRemoved;
	  }
}

void Object::CreateDynamicPhysics(EPhysicsLayers layer)
{
	if(!GetShape())
		return;

	if(GetNetworkManager().IsPhysicsMaster() && GetShape()->GeometryLevel())
  {
    if(_pPhysics)
      RemovePhysics();

    unsigned int len = 0;
    enf::PhysicsGeomDef* comps = GetShape()->CreatePhysComponents(len, true);
		if(!comps)
      return;
    
    enf::PhysicsWorld* pworld = GLandscape->GetPhysicsWorld();
		if(!pworld)
			return;

		float mass = enf::enf_max(GetMass(), 5.f);
    pworld->CreateDynamicBody(this, Vector3(0.f), comps, len, mass, layer);
    
  }
}

void Object::CreatePhysics(EPhysicsLayers layer)
{
	if(_pPhysics || !GetShape() || !Static())
    return;

	if(GetNetworkManager().IsPhysicsMaster() && GetShape()->GeometryLevel())
  {
		unsigned int len = 0;
    enf::PhysicsGeomDef* comps = GetShape()->CreatePhysComponents(len);

    if(!comps)
		  return;
		
    enf::PhysicsWorld* pworld = GLandscape->GetPhysicsWorld();
    pworld->CreateStaticBody(this, comps, len, layer);
		//++s_physCreated; 		
	}
}
#endif

Matrix4P Object::IGetMatrix()
{
  return Matrix4P(FutureVisualState().Transform());
}

void Object::SetPhysicsController(enf::PhysicsController* pc)
{
  //Assert2(pc == NULL || GWorld != NULL, "Entity with physics must be insert into world!");
  
	if(_pPhysics)
		delete _pPhysics;
	_pPhysics = pc;
}

unsigned int Object::SetEventMask(unsigned int mask)
{
	unsigned int oldEventMask = _EventMask;
	_EventMask = oldEventMask | mask;

	if(_pPhysics != NULL)
	{
		_pPhysics->UpdateEntityEvents(this, oldEventMask, oldEventMask | mask);
	}
	return _EventMask;
}


/*
void Object::RemoveDiagObject(Object* obj)
{
  Landscape::RemoveDiagObject(obj);
}

void Object::AddDiagCapsule(const float sizex, const float sizey, const float sizez, const Matrix4& mx, Color color)
{
  Landscape::AddDiagCapsule(sizex, sizey, sizez, mx, color);
}

Object* Object::AddDiagSphere(const Vector3& pos, const float radius, Color color)
{
  return Landscape::AddDiagSphere(pos, radius, color);
}

Object* Object::AddDiagArrow(const Vector3& pos, const Vector3& end, Color color)
{
  return Landscape::AddDiagArrow(pos, end, color);
}
*/

RString Object::GetDisplayName() const
{
  static RString empty;
  if( !_shape ) return empty;
  MapType type=_shape->GetMapType();
  switch( type )
  {
    case MapTree: case MapSmallTree:
      return LocalizeString(IDS_DN_TREE);
    case MapBush:
      return LocalizeString(IDS_DN_BUSH);
    case MapBuilding:
      return LocalizeString(IDS_DN_BUILDING);
    case MapHouse:
      return LocalizeString(IDS_DN_HOUSE);
    case MapForestBorder: case MapForestTriangle: case MapForestSquare:
      return LocalizeString(IDS_DN_FOREST);
    case MapChurch:
      return LocalizeString(IDS_DN_CHURCH);
    case MapChapel:
      return LocalizeString(IDS_DN_CHAPEL);
    case MapCross:
      return LocalizeString(IDS_DN_CROSS);
    case MapRock:
      return LocalizeString(IDS_DN_ROCK);
    case MapBusStop:
      return LocalizeString(IDS_DN_BUS_STOP);
    // ignored maptypes
    //MapFountain,
    //MapFence, MapWall,
    // vehicle should handle some types
    case MapBunker: case MapFortress:
    case MapViewTower: case MapLighthouse: case MapQuay:
    case MapFuelstation: case MapHospital:
      LogF("%s: This should be handled by VehicleType",(const char *)GetDebugName());
    default:
      return empty;
  }
}

RString Object::GetShortName() const
{
  return GetDisplayName();
}

RString Object::GetNameSound() const
{
  static RString empty;
  if( !_shape ) return empty;

  MapType type=_shape->GetMapType();
  switch( type )
  {
    case MapTree: case MapSmallTree:
      return "obj_tree";
    case MapBush:
      return "obj_bush";
    case MapBusStop: case MapBuilding:
      return "obj_building";
    case MapHouse:
      return "obj_house";
    case MapForestBorder: case MapForestTriangle: case MapForestSquare:
      return "obj_forest";
    case MapChapel: case MapChurch:
      return "obj_church";
    case MapCross:
      return "obj_cross";
    case MapRock:
      return "obj_rock";

    // ignored maptypes
    //MapFountain,
    //MapFence, MapWall,
    // vehicle should handle some types
    case MapBunker: case MapFortress:
    case MapViewTower: case MapLighthouse: case MapQuay:
    case MapFuelstation: case MapHospital:
      LogF("%s: This should be handled by VehicleType",(const char *)GetDebugName());
    default:
      return empty;
  }
}

bool Object::IsMoveTarget() const
{
  if( !_shape ) return false;
  if( GetDisplayName().GetLength() == 0) return false;
  if( GetNameSound().GetLength() == 0) return false;
  return true;
}

RString Object::GetDebugName() const
{
  char nameS[128];
  if (GetShape())
  {
    strcpy(nameS,GetFilenameExt(GetShape()->Name()));
  }
  else
  {
    strcpy(nameS,"<no shape>");
  }
  char buf[256];
  if( ID()<0 )
  {
    sprintf(buf,"NOID %s",nameS);
  }
  else
  {
    sprintf(buf,"%d: %s",ID().Encode(),nameS);
  }
  return buf;
}

bool Object::IsAnimationIdentity(int level) const
{
  AnimationStyle style = IsAnimated(level);
  if (style<AnimatedGeometry) return true;
  // run the animation and check matrices for non-identity
  const VisualState &vs = RenderVisualStateScope();
  Matrix4Array matrices;
  if (!unconst_cast(this)->PrepareShapeDrawMatrices(matrices,vs,GetShape(),level)) return true;
  for (int m=0; m<matrices.Size(); m++)
  {
    float diff2 = matrices[m].Distance2(MIdentity);
    if (diff2>Square(1e-5f))
      return false;
  }
  return true;
} 

AnimationStyle Object::IsAnimated( int level ) const
{
  // avoid loading the level
  RefShapeRef shape = _shape->LevelRef(level);
  if (shape.NFaces()<=0) return NotAnimated;
  if (IsDestroyAnimated()) return AnimatedGeometry;
  switch (_shape->GetAnimationType())
  {
    case AnimTypeNone: return NotAnimated;
    case AnimTypeHardware:
      return shape.HasSkeleton() ? AnimatedGeometry : NotAnimated;
  }
  
  if( (shape.GetOrHints()&(ClipLandKeep|ClipLandOn))) return AnimatedGeometry;
  return NotAnimated;
}

static float TreeDensity = 0.5f;

#if _ENABLE_CHEATS

  #define OBJ_DIAG_ENABLE(type,prefix,XX) \
	  XX(type, prefix, Tree) \
	  XX(type, prefix, Water) \
	  XX(type, prefix, Terrain) \
	  XX(type, prefix, Road) \
	  XX(type, prefix, Object) \

  #ifndef DECL_ENUM_OBJ_DIAG_ENABLE
  #define DECL_ENUM_OBJ_DIAG_ENABLE
  DECL_ENUM(ObjDiagEnable)
  #endif
  DECLARE_DEFINE_ENUM(ObjDiagEnable,ODE,OBJ_DIAG_ENABLE)

  static int ObjDiagMode = -1;

  static InitValT<float,int,1> ObjDrawScale[NObjDiagEnable];

  #define CHECK_OBJ_ENABLE(x) ( (ObjDiagMode&(1<<(x)))!=0 )

  bool CheckDrawTerrain() {return CHECK_OBJ_ENABLE(ODETerrain);}
  bool CheckDrawWater() {return CHECK_OBJ_ENABLE(ODEWater);}
  bool CheckDrawRoads() {return CHECK_OBJ_ENABLE(ODERoad);}
#else

  #define CHECK_OBJ_ENABLE(x) true

#endif

#if _ENABLE_CHEATS

// advances debugging - via scripting
#include <El/evaluator/expressImpl.hpp>


static void ReportObjDiagMode(int mode)
{
  int mask = 1<<mode;
  const char *status = (ObjDiagMode&mask)!=0 ? "On" : "Off";
  const char *name = FindEnumName(ObjDiagEnable(mode));
  GlobalShowMessage(2000,"Object diag mode %s %s",name,status);
}

static GameValue SetObjDiagEnable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  const char *modeStr = (RString)oper1;
  bool onOff = oper2;
  int modeMask = ~0;
  int mode = GetEnumValue<ObjDiagEnable>(modeStr);
  if (mode<0) return NOTHING;
  modeMask = 1<<mode;

  if (onOff) ObjDiagMode |= modeMask;
  else ObjDiagMode &= ~modeMask;
  ReportObjDiagMode(mode);
  return NOTHING;
}

static GameValue SetObjDiagScale(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  const char *modeStr = (RString)oper1;
  float scale = oper2;
  int mode = GetEnumValue<ObjDiagEnable>(modeStr);
  if (mode<0) return NOTHING;
  ObjDrawScale[mode] = scale;
  return NOTHING;
}


static GameValue SetObjDiagToggle(const GameState *state, GameValuePar oper1)
{
  const char *modeStr = (RString)oper1;
  int mode = GetEnumValue<ObjDiagEnable>(modeStr);
  if (mode==-1) return NOTHING;
  int modeMask = 1<<mode;

  ObjDiagMode ^= modeMask;
  
  ReportObjDiagMode(mode);
  
  return NOTHING;
}

static GameValue SetTreeDensity(const GameState *state, GameValuePar oper1)
{
  TreeDensity = oper1;
  return GameValue();
}

#include <El/Modules/modules.hpp>

static const GameFunction DbgUnary[]=
{
  GameFunction(GameNothing,"diag_densTree",SetTreeDensity,GameScalar TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_objToggle",SetObjDiagToggle,GameString TODO_FUNCTION_DOCUMENTATION),
};

static const GameOperator DbgBinary[]=
{
  GameOperator(GameNothing,"diag_objEnable",function,SetObjDiagEnable,GameString,GameBool TODO_OPERATOR_DOCUMENTATION),
  GameOperator(GameNothing,"diag_objScale",function,SetObjDiagScale,GameString,GameScalar TODO_OPERATOR_DOCUMENTATION),
};


INIT_MODULE(GameStateDbgTree, 3)
{
  GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
  GGameState.NewOperators(DbgBinary,lenof(DbgBinary));
};

#endif

bool Object::CanBeInstancedShadow( int level ) const
{
#if !_ENABLE_SKINNEDINSTANCING
  if (IsAnimated(level)) return false;
#endif
  LODShape *lShape = GetShape();
  if (!lShape) return false;
#if !_ENABLE_SKINNEDINSTANCING
  if (lShape->GetAnimationType()!=AnimTypeNone) return false;
#endif
  // used for shadow volumes only. SB shadows use normal rendering path, and call CanBeInstanced
  return lShape->FindShadowVolume()>=0;
}


bool Object::CanBeInstanced(int level, float distance2, const DrawParameters &dp) const
{
  // If craters are to be drawn, disable instancing
  if (CheckCraterDrawing(distance2, dp) > 0)
  {
    return false;
  }

// If in thermal mode and own heat source is going to be used, disable instancing
  if (GEngine->GetThermalVision())
  {
    Vector3 dummy;
    if (GetOwnHeatSource(dummy))
    {
      return false;
    }
  }

  // only non-animated shapes may be instanced
#if !_ENABLE_SKINNEDINSTANCING
  if (IsAnimated(level)) return false;
#endif
  // proxies do not matter - we are able to instance them  
  LODShape *lShape = GetShape();
  if (!lShape) return false;
  /*
#if !_ENABLE_SKINNEDINSTANCING
  // animation type should be already handled by IsAnimated
  if (lShape->GetAnimationType()!=AnimTypeNone) return false;
#endif
  */
  return lShape->LevelUsesVBuffer(level);
}

bool Object::CanObjDrawAsTask(int level) const
{
  // by default we assume any object can be recorded
  if (!_shape) return false; // no shape - no MT, to make life for MT easier
  // when surface splitting, we cannot render using MT
  if (_shape->Special()&OnSurface)
  {
    return false;
  }
  // when using SW animation, we cannot render using MT
  if (_shape->GetAnimationType()==AnimTypeSoftware ) return false;
  // if all headers not ready yet, we must load the object in sync
  const Shape *shape = _shape->GetLevelLocked(level);
  if (!shape) return false; // LOD not loaded yet, no MT
  // if some texture is not ready yet, we cannot assume thread safety
  if (!shape->CheckTexturesReady()) return false;
  return true;
}
bool Object::CanObjDrawShadowAsTask(int level) const
{
  LODShape *shape = GetShape();
  if (!shape) return false; // no shape - no MT, to make life for MT easier
  return true;
}

bool Object::CheckShadowCaster() const
{
  if (!_shape) return false;
  if (_shape->FindShadowVolume()<0) return false;
  float minRadius = Static() ? 3.5f : 6.0f;
  return _shape->BoundingSphere()>minRadius;
}

#if _ENABLE_CHEATS
static bool DrawCraters = true;
#else
const bool DrawCraters = true;
#endif
int Object::CheckCraterDrawing(float distance2, const DrawParameters &dp) const
{
#if _BINARIZE_REDUCEOBJECT
  Fail("Waring: Object functionality reduced");
  return 0;
#else
  // Enable/disable crater drawing
#if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_C))
  {
    DrawCraters = !DrawCraters;
    GlobalShowMessage(500, "DrawCraters %s", DrawCraters ? "On" : "Off");
  }
#endif

  // Constant to define the maximum distance the craters should be drawn on objects
#if _VBS3_MODELCRATERDISTANCE
  float craterDistance = 40.0f / GScene->GetCamera()->Left();
#else
  const float craterDistance = 40.0f;
#endif

  // If craters are to be drawn, then check the shading quality
  // Make sure you don't return different values for z-priming and regular buffer for the same object
  // (otherwise z-fighting would occur due to numeric inaccuracies)
  if (
    !dp._sbIsBeingRendered
    && distance2 < Square(craterDistance) && _damage && _damage->_craters.Size() > 0 && DrawCraters)
  {
    return GEngine->GetMaxCraters();
  }
  else
  {
    return 0;
  }
#endif
}

DEF_RSB(house);
DEF_RSB(road);
DEF_RSB(forest);
DEF_RSB(bushsoft);
DEF_RSB(bushhard);
DEF_RSB(treesoft);
DEF_RSB(treehard);

/** based on Object only, full type not available in Binarize
See also EntityType::IsPersistenceRequired
@return PersistenceReason bitmask
*/
int Object::BinarizeCheckPersistance() const
{
  int persistenceReasonObj = 0;
  if (dyn_cast<const Road>(this))
  {
    persistenceReasonObj |= PersistentRoad;
  }
  else
  {
    RStringB simClass = GetShape()->GetPropertyClass();
    // per-class check for known classes
    if (simClass==RSB(house))
    {
      persistenceReasonObj |= PersistentHouse;
    }
    else if (simClass==RSB(road))
    {
      Fail("Late road detection");
    }
    else if (simClass==RSB(forest))
    {
      persistenceReasonObj |= PersistentForest;
    }
    else if (
      simClass==RSB(bushsoft) || simClass==RSB(bushhard) ||
      simClass==RSB(treesoft) || simClass==RSB(treehard)
    )
    {
      // no persistance for some object classes
    }
    else if (simClass.GetLength()>0)
    {
      // handle unknown classes
      LogF("Persistant: reason %s (%s)",cc_cast(simClass),cc_cast(GetShape()->GetName()));
      persistenceReasonObj |= PersistentSimulation|PersistentSound;
    }
  }
  return persistenceReasonObj;
}

bool Object::IsPointAnimated(int level, int index) const
{
  if(_shape->GetAnimationType()==AnimTypeSoftware) return true;
  return false;
}

/*!
*\param level lod level
*\param index vertex index
*\return Animated point model space coordinates.
*/

Vector3 Object::AnimatePoint(const ObjectVisualState &vs, int level, int index) const
{
  ShapeUsed shape = _shape->Level(level);

  // return animated point world coordinates
  // if the shape is not animation via SW animation,
  // land flags need to be handled via transform matrix
  if(
    _shape->GetAnimationType()==AnimTypeSoftware &&
    (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)) && GLOB_LAND
  )
  {
    Matrix4Val toWorld=FutureVisualState().Transform();
    ClipFlags clip=shape->Clip(index);
    Vector3Val pos=shape->Pos(index);
    Vector3 tPos(VFastTransform,toWorld,pos);
    if( clip&ClipLandKeep )
    {
      // shape y is relative to surface
      // calculate world coordinates
      float yPos=pos[1]+_shape->BoundingCenter().Y();
      tPos[1]=GLOB_LAND->SurfaceY(tPos[0],tPos[2])+yPos;
    }
    else if( clip&ClipLandOn )
    {
      // clamp y to surface
      tPos[1]=GLOB_LAND->SurfaceY(tPos[0],tPos[2]);
    }
    ErrF(
      "Object::AnimatePoint %s - KeepHeight: Function is quite slow",
      cc_cast(_shape->GetName())
    );
    return toWorld.InverseGeneral().FastTransform(tPos);
  }
  else
  {
    Vector3Val modelPos = shape->Pos(index);
    return modelPos;
  }
}

/*!
*\param mat matrix to be animated
*\param level lod level
*\param selection index of named selection this matrix is related to
*/

void Object::AnimateBoneMatrix( Matrix4 &mat, const ObjectVisualState &vs, int level, SkeletonIndex boneIndex ) const
{
}

Pair<Vector3,bool> Object::DeanimatePoint(const ObjectVisualState &vs, int level, const BlendSkinning &skinning, Vector3Par pos)
{
  #if _ENABLE_REPORT
    if (skinning.bones.Size()>1)
    {
      RptF("Error: %s: Multiple bones on component in %s", cc_cast(GetShape()->Name()),cc_cast(GetShape()->LevelName(GetShape()->GetLevelLocked(level))));
      for (int i=0; i<skinning.bones.Size(); i++)
      {
        RptF("  bone %s",cc_cast(skinning.bones[i].boneName));
      }
    }
  #endif
  // There must not be more than one bone
  // get dominant bone
  float domW = 0;
  SkeletonIndex domBone = SkeletonIndexNone;
  
  for (int i=0; i<skinning.bones.Size(); i++)
  {
    float w = skinning.bones[i].weight;
    if (w>domW)
    {
      domW = w;
      domBone = skinning.bones[i].bone;
    }
  }


  if (domBone!=SkeletonIndexNone)
  {
    // Get the transformation matrix from original to skinned space
    Matrix4 transform = MIdentity;
    AnimateBoneMatrix(transform, vs, level, domBone);
    // if the bone is singular, there is no crater we could draw
    if (transform.Orientation().Scale2()<1e-8)
      return MakePair(VZero,false);

    // Convert the position from skinned to un-skinned space
    return MakePair(transform.InverseGeneral().FastTransform(pos),true);
  }
  return MakePair(pos,true);
}



// access to world space transformation
Matrix4 Object::WorldTransform(ObjectVisualState const& vs) const
{
  // normal objects are not in any hierarchy
  // they have world-space transform same as in-hierarchy
  return vs.Transform();
}

Vector3 Object::WorldPosition(ObjectVisualState const& vs) const
{
  return vs.Position();
}

Vector3 Object::WorldSpeed() const
{
  return ObjectSpeed();
}


Matrix4 Object::ProxyWorldTransform(VisualStateAge age, const Object *obj) const
{
  ObjectVisualState const& vs = GetVisualStateByAge(age);
  ObjectVisualState const& objvs = obj->GetVisualStateByAge(age);
  return vs.Transform() * objvs.Transform();
}

Matrix4 Object::ProxyInvWorldTransform(VisualStateAge age, const Object *obj) const
{
  ObjectVisualState const& vs = GetVisualStateByAge(age);
  ObjectVisualState const& objvs = obj->GetVisualStateByAge(age);
  return objvs.GetInvTransform() * vs.GetInvTransform();
}


Matrix4 Object::WorldInvTransform(ObjectVisualState const& vs) const
{
  return vs.GetInvTransform();
}

float Object::CloudletClippingCoef() const
{
  return 1;
}

/**
@param setEngineStuff When this flag is set, animate should not perform any geometry animation,
  as this will be done by HW skinning. It should only setup section properties, like textures.

@patch 1.53 Date 4/27/2002 by Ondra
- Fixed: Geometry used for walking is now also deformed
during object destruction deformation.
*/

void Object::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
#if !_BINARIZE_REDUCEOBJECT
  AssertDebug( _animatedCount++<=2 );
#endif
  if (!_shape) return;
  ShapeUsed shape=_shape->Level(level);
  if( shape.IsNull() ) return;
  //ShapeGeometryLock<> lock(shape.GetShape());
  if (!setEngineStuff && IsDestroyAnimated())
  {
    MATRIX_4_ARRAY(matrices,128);
    if (parentObject->PrepareShapeDrawMatrices(matrices, animContext.GetVisualState(),_shape, level))
    {
      VerifyMatrixArrayWasSet(matrices, _shape, level);
      // use skinning for destruction animation
      AnimationRT::ApplyMatrices(animContext,_shape,level,matrices);
    }
  }

  // check if object needs surface animation
  if ((shape->GetAndHints() & ClipLandMask) == ClipLandOn)
  {
    // no animation required: will be done during SurfaceSplit
  }
  else if(
    // if HW animation is requested, no need to update any positions
    !setEngineStuff &&
    // check if some SW animation is needed
    (shape->GetOrHints() & (ClipLandKeep|ClipLandOn)) && GLandscape &&
    _shape->GetAnimationType()==AnimTypeSoftware
  )
  {
    // save original position
    Matrix4Val toWorld = animContext.GetVisualState().Transform();
    Matrix4Val fromWorld =animContext.GetVisualState().GetInvTransform();
    // change object shape to reflect surface

    float yOffset = 0;
    Vector3 bCenter = VZero;
    if (shape->GetOrHints() & ClipLandKeep)
    {
      // world space bounding center position
      bCenter.SetFastTransform(toWorld, -_shape->BoundingCenter());

      float bcSurfaceY = GLandscape->SurfaceY(bCenter[0],bCenter[2]);
      yOffset = bCenter.Y() - bcSurfaceY; //+_shape->BoundingCenter().Y();
      //LogF("%s: yOffset %g",(const char *)GetDebugName(),yOffset);
    }
    for (int i=0; i<shape->NPos(); i++)
    {
      ClipFlags clip = shape->Clip(i);
      if (clip & ClipLandKeep)
      {
        //Vector3Val pos=shape->OrigPos(i);
        Vector3Val pos = shape->Pos(i);
        // shape y is relative to surface
        // calculate world coordinates
        //float yPos = pos[1]+yOffset;

        Vector3 tPos(VFastTransform,toWorld,pos);
        // calculate transformed pos above world space bCenter
        float yAbove = tPos.Y() - bCenter.Y() + yOffset;

        Vector3 &dPos = animContext.SetPos(shape, i);

        //tPos[1]=GLandscape->SurfaceY(tPos[0],tPos[2])+yPos;
        tPos[1]=GLandscape->SurfaceY(tPos[0],tPos[2])+yAbove;

        dPos.SetFastTransform(fromWorld,tPos);
        //clip &= ~ClipLandKeep;
        //shape->SetClip(i,clip);
      }
      else if (clip & ClipLandOn)
      {
        Vector3Val pos = shape->Pos(i);
        // shape y is relative to surface
        // calculate world coordinates
        Vector3 tPos(VFastTransform,toWorld,pos);
        tPos[1]=GLOB_LAND->SurfaceY(tPos[0],tPos[2]);
        Vector3 &dPos = animContext.SetPos(shape, i);
        dPos.SetFastTransform(fromWorld,tPos);
        //clip&=~ClipLandOn;
        //shape->SetClip(i,clip);
      }
    }
    // remove keep flags from the whole model
    //shape->ClipAndAll(~ClipLandKeep);
    animContext.InvalidateNormals(shape);
  }
}

void Object::Deanimate( int level, bool setEngineStuff )
{
#if !_BINARIZE_REDUCEOBJECT
  AssertDebug( --_animatedCount>=0 );
#endif
}


/**
The result is not guaranteed to contain whole object.
It should contain vast majority of it, though.
Typical usage is to test if user is pointing at the object.
*/
float Object::BoundingInfoUI (Vector3 &bCenter, Vector3 *minMax) const
{
  float bSphere = ClippingInfo(RenderVisualState(),minMax,ClipVisual);
  // min-max box may provide closer sphere than bSphere
  float minMaxRad = minMax[1].Distance(minMax[0])*(0.5f*0.7f);
  if (minMaxRad<bSphere)
  {
    bSphere = minMaxRad;
    bCenter = (minMax[1]+minMax[0])*0.5f;
  }
  else
    bCenter = VZero;
  return bSphere;
}

//simplified method of Object::ClippingInfo, keep it in sync!
float Object::GetCollisionRadius(const ObjectVisualState &vs) const 
{ 
	LODShape *shape = GetShape(); 

	if (!shape) 
		return 0.0f; 

	float bRadius = shape->BoundingSphere()*vs.MaxScale(); 

	if( (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)) && GLOB_LAND ) 
		return bRadius*1.3f; 
	else  
	{ 
		if (IsDestroyAnimated()) 
			return bRadius*1.5f; 
		else  
		{ 
			if (shape->GetAnimationType()!=AnimTypeNone) 
				return bRadius*1.2f; 
		} 
	} 
	return bRadius; 
} 

float Object::ClippingInfo(const ObjectVisualState &vs, Vector3 *minMax, ClippingType clip ) const
{
  LODShape *shape = GetShape();
  if (!shape)
  {
    minMax[0]=minMax[1] = VZero;
    return 0;
  }
  float bRadius = shape->BoundingSphere()*vs.MaxScale();
  if (clip==ClipVisual)
  {
    minMax[0] = shape->MinMaxVisual()[0];
    minMax[1] = shape->MinMaxVisual()[1];
  }
  else
  {
    minMax[0] = shape->MinMax()[0];
    minMax[1] = shape->MinMax()[1];
  }
  if( (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)) && GLOB_LAND )
  {
    // assume slope max. 0.5
    const float maxSlope = 0.5f;
    const float sFactor = 1.3f;
    //Vector3Val cnt = (minMax[0]+minMax[1])*0.5f;
    float yChangeForMin = (fabs(minMax[0].X())+fabs(minMax[0].Z()))*maxSlope;
    float yChangeForMax = (fabs(minMax[0].X())+fabs(minMax[0].Z()))*maxSlope;
    float yChange = floatMax(yChangeForMin,yChangeForMax);
    minMax[0][1] -= yChange;
    minMax[1][1] += yChange;
    bRadius *= sFactor;
  }
  else if (IsDestroyAnimated())
  {
    // assume destroy animation may enlarge the object a lot
    // assume object is fallen down - it increases its width proportionally to its height
    // bounding sphere is not changed that much
    const float sFactor = 1.5f;
    float height = minMax[1].Y()-minMax[0].Y();
    const float sideFactor = 0.7f;

    minMax[0][0] -= sideFactor*height;
    minMax[0][2] -= sideFactor*height;
    minMax[1][0] += sideFactor*height;
    minMax[1][2] += sideFactor*height;

    bRadius *= sFactor;
  }
  else if (shape->GetAnimationType()!=AnimTypeNone)
  {
    // any animation may enlarge it a little bit
    const float sFactor = 1.2f;
    Vector3Val cnt = (minMax[0]+minMax[1])*0.5f;
    minMax[0] = cnt+(minMax[0]-cnt)*sFactor;
    minMax[1] = cnt+(minMax[1]-cnt)*sFactor;
    bRadius *= sFactor;
  }
  return bRadius;
}

void Object::AnimatedBSphere(
  int level, Vector3 &bCenter, float &bRadius, Matrix4Par pos, float posMaxScale
) const
{
  ShapeUsed shape = GetShape()->Level(level);
  bRadius = shape->BSphereRadius()*posMaxScale;
  bCenter = pos.FastTransform(shape->BSphereCenter());
  if (!IsAnimated(level))
  {
    return;
  }
  // old default implementation is way to slow and file demanding
  if( (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)) && GLOB_LAND )
  {
    // assume slope max. 0.5
    const float sFactor = 1.3f;
    bRadius *= sFactor;
  }
  else if (IsDestroyAnimated())
  {
    const float sFactor = 1.1f;
    bRadius *= sFactor;
  }
}

/*!
Purpose of this function is to "smooth out" small entity movement that have easily predictable results, like
when soldier is lying down or dying.
*/

Vector3 Object::PredictVelocity() const
{
  return ObjectSpeed();
}

float Object::GetFeatureSize() const
{
  return 0;
}

Object::Visible Object::VisibleStyle() const
{
#if _VBS3
  if (_inVisibilityFlags & VBS_IFVis) return ObjInvisible;
#endif
  return _shape.IsNull() ? ObjInvisible : ObjVisibleFast;
}
bool Object::OcclusionFire() const
{
  return true;
}
bool Object::OcclusionView() const
{
  // note: if object is animated as destructed, view geometry becomes inaccurate
  return true; //_destroyPhase==0;
}

/*!
  Default implementation uses LODShape::ViewDensity()
*/
float Object::ViewDensity() const
{
  LODShape *shape = GetShape();
  return shape ? shape->ViewDensity() : -100; 
}

/*!
* \note
  This function is currently called only for vehicles
  created in function CreateVehicle in aiCenter.cpp
*/
void Object::Init( Matrix4Par pos, bool init )
{
}

void Object::PlaceOnSurface(Matrix4 &trans)
{
  InitSkew(GLandscape,trans);
}

Vector3 Object::PlacingPoint() const
{
  if (!GetShape()) return VZero;
  // by default use original (Oxygen) zero as a placing point
  return -GetShape()->BoundingCenter();
}

void Object::SetupInstances(int cb, const Shape &sMesh, const ObjectInstanceInfo *instances, int nInstances)
{
  GEngine->SetupInstances(cb, instances, nInstances);
}


/*!\note If the animation is peformed, components are invalidated.*/

void Object::AnimateComponentLevel(AnimationContext &animContext, int level)
{
  //bool change = IsAnimated(level)>=AnimatedGeometry;
  Animate(animContext, level, false, this, -FLT_MAX);
  // no need to call InvalidateConvexComponents, was already done as part of the animation
  //if (change) animContext.InvalidateConvexComponents();
}

void Object::DeanimateComponentLevel(int level)
{
  Deanimate(level,false);
}

void Object::AnimateGeometry(AnimationContext &animContext)
{
  int level=_shape->FindGeometryLevel();
  if( level>=0 )
  {
    //bool change = IsAnimated(level)>=AnimatedGeometry;
    Animate(animContext, level, false, this, -FLT_MAX);
    //if (change) animContext.InvalidateConvexComponents();
  }
}

void Object::DeanimateGeometry()
{
  int level=_shape->FindGeometryLevel();
  if( level>=0 )
  {
    Deanimate(level,false);
  }
}

void Object::AnimateViewGeometry(AnimationContext &animContext)
{
  int level=_shape->FindViewGeometryLevel();
  if( level>=0 )
  {
    //bool change = IsAnimated(level)>=AnimatedGeometry;
    Animate(animContext, level, false, this, -FLT_MAX);
    //if (change) animContext.InvalidateConvexComponents();
  }
}

void Object::DeanimateViewGeometry()
{
  int level=_shape->FindViewGeometryLevel();
  if( level>=0 )
  {
    Deanimate(level,false);
  }
}

void Object::AnimateLandContact(AnimationContext &animContext)
{
  int level=_shape->FindLandContactLevel();
  if( level>=0 ) Animate(animContext, level, false, this, -FLT_MAX);
}

void Object::DeanimateLandContact()
{
  int level=_shape->FindLandContactLevel();
  if( level>=0 ) Deanimate(level,false);
}

void Object::Move(Matrix4Par transform)
{
/*
void CheckPosition(Object *obj, const Vector3 &newPos);
CheckPosition(this, transform.Position());
*/
#if _VBS3_LARGE_OBJECTS
  GLOB_LAND->OnObjectMove(this);
#endif

  GLOB_LAND->MoveObject(this,transform);

}

//!Change object position
/*!
* \overload void Object::Move(Matrix4Par transform)
*/

void Object::Move(Vector3Par position)
{
#if _VBS3_LARGE_OBJECTS
  GLOB_LAND->OnObjectMove(this);
#endif

  GLOB_LAND->MoveObject(this,position);
}

void Object::MoveNetAware(Matrix4Par transform )
{
  Move(transform);
  if (!IsLocal())
  {
    GetNetworkManager().AskForMove(this,transform);
  }
}

/*!
* \overload void Object::MoveNetAware(Matrix4Par transform)
*/

void Object::MoveNetAware(Vector3Par pos)
{
  Move(pos);
  if (!IsLocal())
  {
    GetNetworkManager().AskForMove(this,pos);
  }
}

/*!\todo implement corresponding GameStateExt function*/
void Object::SetPlateNumber( RString plate )
{
}

/**
Note: currently this works only for road pass objects.
*/
int Object::PassOrder( int lod ) const
{
  return 0;
}

/**
@return
  -2 drawing before landscape possible
  -1 drawing after landscape needed, no z-sorting
  0 rough z-sorting needed, shadow casting possible
  1 on-surface pass
  2 alpha-blending used - strict z-sorting, no shadow casting
  3 cockpit pass (may use separate z-space)
*/

int Object::GetAlphaLevel(int lod) const
{
  if( !_shape ) return -1;
  RefShapeRef shape = _shape->LevelRef(lod);
  if( shape.IsNull() ) return -1;
  int spec = GetObjSpecial(shape.Special());
  // surface objects (roads and craters) must be drawn after grass
  if( spec&(OnSurface|IsOnSurface) )
  {
    return 1;
  }
  // non-alpha objects may always be drawn before water
  const int alphaFlags=IsAlpha;
  if( (spec&alphaFlags)==0 ) return -3; // no alpha - normal object
  // alpha objects - after water
  // detect object is alpha blended
  float alpha=shape.GetColor().A8()*(1.0f/255);
  if( spec&IsColored )
  {
    alpha *= GetConstantColor().A();
  }
  if (alpha>0.94f)
  {
    // Marginal alpha - rendering after the landscape required, no z-sorting needed
    return -2;
  }
  if (alpha>0.81f || _shape->GetForceNotAlphaModel())
  {
    // slight alpha - rough z-order preferred
    return 0;
  }
  // significant alpha - z-order needed
  return 2;
}

int Object::PassNum( int lod )
{
  #if _ENABLE_CHEATS || _VBS3_CHEAT_DIAG
    if (DiagDrawModeState!=DDMNormal)
    {
      if (lod==_shape->FindGeometryLevel()) return 2;
      if (lod==_shape->FindViewGeometryLevel()) return 2;
      if (lod==_shape->FindFireGeometryLevel()) return 2;
    }
  #endif
  return GetAlphaLevel(lod);
}

bool Object::CastPass3VolShadow( int level, bool zSpace, float &castShadowDiff, float &castShadowAmb ) const
{
  castShadowDiff = 1.0f;
  castShadowAmb = 1.0f;
  // only objects participating in pass 3 are of type Transport and Man, and their proxies
  // if we are asked, it is because we serve as a proxy for someone
  // we are asked only when we can a SV in a normal scene
  // hence the default behaviour is to return "yes"
  return true;
}

  // virtual access to all proxy objects
int Object::GetProxyCount(int level) const
{
#if _ENABLE_REPORT 
  ShapeUsed sShape=_shape->Level(level);
  if (sShape->NProxies()>0)
    RptF("Proxy not supported when no type exists - %s",(const char *)GetDebugName());
#endif
  return 0;
}

Object *Object::GetProxy(VisualStateAge age, LODShape *&shape, Object *&parentObject,
  int level, Matrix4 &transform, const Matrix4 &parent, int i) const
{
  RptF("Proxy not supported when no type exists - %s",(const char *)GetDebugName());
  return NULL;
}


Matrix4 Object::GetProxyTransform(int level, int i, Matrix4Par proxyPos, Matrix4Val parent) const
{
  RptF("Proxy not supported when no type exists - %s",(const char *)GetDebugName());
  return parent*proxyPos;
}

bool Object::DrawProxiesNeeded(int level) const
{
  return false;
}


bool Object::PreloadProxies(int level, float dist2, bool preloadTextures) const
{
  return true;
}

/**
Default implementation assumes 1st person view is almost the same as finest LOD
*/
bool Object::PreloadInsideView() const
{
  return PreloadProxies(0,0);
}

/**
The purpose of this function is to check if PrepareProxiesForDrawing needs to be called.
If not, the caller is able to skip some preparation, which can be quite slow.
*/
bool Object::NeedsPrepareProxiesForDrawing() const
{
  return false;
}

void Object::PrepareProxiesForDrawing(
  Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform,int level, ClipFlags clipFlags, SortObject *so
)
{
}

ObjectTyped::ObjectTyped(LODShape *shape, const EntityType *type, const CreateObjectId &id, bool hasSimulation)
:base((type->VehicleAddRef(), (shape ? shape : type->GetShape())), id, false),
_type(const_cast<EntityType *>(type))
{
  Assert(!_futureVisualState);
  _futureVisualState = Type()->CreateVisualState();
}

ObjectTyped::~ObjectTyped()
{
	_type->VehicleRelease();
}


int ObjectTyped::GetProxyCount(int level) const
{
  //ShapeUsed sShape=_shape->Level(level);
  //return sShape->NProxies();
  return GetNonAIType()->GetProxyCountForLevel(level);
}

/**
@param parent [in] parent transformation
*/
Matrix4 ObjectTyped::GetProxyTransform(int level, int i, Matrix4Par proxyPos, Matrix4Val parent) const
{
  return parent*proxyPos;
}


Object *ObjectTyped::GetProxy(
  VisualStateAge age, LODShape *&shape, Object *&parentObject,
  int level, Matrix4 &transform, const Matrix4 &parent, int i
) const
{
  const ProxyObjectTyped &proxy=GetNonAIType()->GetProxyForLevel(level,i);

  // TODO: check if the corresponding proxy face is hidden
//   if (_type)
//   {
//     int section = _type->GetProxySectionForLevel(level,i);
//     if (section>=0)
//     {
//       int spec = animContext.GetSection(lock, section).Special();
//       if (spec & IsHidden) continue;
//     }
//   }

  transform = GetProxyTransform(level,i,proxy.GetTransform(),parent);
  DoAssert(transform.IsFinite())
  shape = proxy.GetShape();
  parentObject = proxy.obj;
  return proxy.obj;
}

bool ObjectTyped::DrawProxiesNeeded(int level) const
{
  if (_shape && !_shape->GetLevelLocked(level))
  {
    // dirty hack: if the level is not loaded, pretend there are some proxies
    return true;
  }
  return GetNonAIType()->GetProxyCountForLevel(level)>0;
}

float ObjectTyped::GetFeatureSize() const
{
  return _type->GetFeatureSize(RenderVisualState().Position());
}

bool ObjectTyped::NeedsPrepareProxiesForDrawing() const
{
  // TODO: store if there are any proxies rendered this way
  return true;
}

void ObjectTyped::PrepareProxiesForDrawing(
  Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so
)
{
  PROFILE_SCOPE(ppdOT);
  float dist2 = so->distance2;
  // draw all proxy objects
  const EntityType *entType = _type;
  ShapeUsed lock = _shape->Level(level);
  for( int i=0; i<entType->GetProxyCountForLevel(level); i++ )
  {
    // check proxy corresponding to this shape proxy in given type

    const ProxyObjectTyped &proxy=entType->GetProxyForLevel(level,i);
    
    const EntityType *type = proxy.obj->GetEntityType();
    if (type && !type->_alwaysShow)
    {
      // if proxy is typed, we cannot render it this way
      continue;
    }
    
    // check if the corresponding proxy face is hidden
    if (entType)
    {
      int section = entType->GetProxySectionForLevel(level,i);
      if (section>=0)
      {
        int spec = animContext.GetSection(lock, section).Special();
        if (spec & IsHidden) continue;
      }
    }

    PositionRender pTransform(GetProxyTransform(level,i,proxy.GetTransform(),transform.position),transform.camSpace);

    // LOD detection
    LODShape *pshape = proxy.obj->GetShape();
    if (!pshape) continue;
    float resol = _shape->Resolution(level);
    int pLevel = -1;
    if (resol>900)
    { // if the original object level is special, prefer using special level here as well
      pLevel = pshape->FindSpecLevel(resol);
      // if exact match is not found and it is some kind of view lod, use any view lod available
      if (pLevel<0 && IsSpecInterval(resol,VIEW_GUNNER,VIEW_CARGO))
      {
        signed char cnt;
        pLevel = pshape->FindSpecLevelInterval(cnt,VIEW_GUNNER,VIEW_CARGO);
      }

    }
    // TODO: propagate Scale2 instead of Scale() all way down from here
    if (pLevel<0)
    {
      pLevel=GScene->LevelFromDistance2(pshape,so->distance2,pTransform.position.Scale());
    }
    if( pLevel==LOD_INVISIBLE )
    {
      continue;
    }

    GScene->ProxyForDrawing(proxy.obj, rootObj, pLevel, rootLevel, clipFlags, dist2, pTransform);
  }
}

bool ObjectTyped::PreloadProxies(int level, float dist2, bool preloadTextures) const
{
  // note: we should consider Scale here, but as it is usually close to 1, we do not
  return _type->PreloadProxies(level,dist2,FutureVisualState().Scale(),preloadTextures);
}

float Object::EstimateArea() const
{
  return _shape->EstimateArea(RenderVisualState().Scale())*DrawingAreaRatio();
}


#if DPRIM_STATS
  #include <El/Statistics/statistics.hpp>
  StatisticsByName DPrimStats;
  StatisticsByName TrisStats;
  StatisticsByName ObjsStats;
  StatisticsByName PixelStats;
	void DPrimStatsCount(const char *name, int value)
	{
	  DPrimStats.Count(name,value);
	}
	void TrisStatsCount(const char *name, int value)
	{
	  TrisStats.Count(name,value);
	}
	void ObjsStatsCount(const char *name, int value)
	{
	  ObjsStats.Count(name,value);
	}
  void PixelStatsCount(const char *name, int value)
  {
    PixelStats.Count(name,value);
  }
#endif

#if 0
  // shape name only
  #define OBJECT_LEVEL_DPRIM_STAT_SCOPE(cb,lodShape,level) \
    DPRIM_STAT_SCOPE(cb<0 || Glob.config._mtSerial ? (lodShape)->GetName() : "",Obj);

#else
  // include lod level identification
  #define OBJECT_LEVEL_DPRIM_STAT_SCOPE(cb,lodShape,level) \
    DPRIM_STAT_SCOPE(cb<0 || Glob.config._mtSerial  ? (lodShape)->ShapeName((level)) : "",Obj);
    /*
    not MT safe - cannot be used when recording into CB
    by passing "" we make DrawStatScope to ignore the input
    */
#endif

#if _ENABLE_PERFLOG
  /// similar to PROFILE_SCOPE, but not tied to any object scope - start/stop is triggered explicitely  
  #define CREATE_STARTABLE_SCOPE(name,category,hier,tCat) \
    class ScopeProfilerStartable_##name \
    { \
      /*provide a memory which we will initialize using explicit constructor/destructor later*/ \
      mutable int _profMemory[(sizeof(ScopeProfiler)+sizeof(int)-1)/sizeof(int)]; \
      \
      public: \
      void StartProfiling() const \
      { \
        DEF_COUNTER_P_EX(name,category,PROF_COUNT_SCALE); \
        new(&_profMemory) ScopeProfiler(COUNTER_P_NAME(name),COUNTER_TIMINGENABLED_P(name)); \
      } \
      void StopProfiling() const \
      { \
        ScopeProfiler &scopeProf =  reinterpret_cast<ScopeProfiler &>(_profMemory); \
        scopeProf.~ScopeProfiler(); \
      } \
    };
#else
  #define CREATE_STARTABLE_SCOPE(name,category,hier,tCat) \
    class ScopeProfilerStartable_##name \
    { \
      public: \
      void StartProfiling() const {} \
      void StopProfiling() const {} \
    };
#endif
  
  
/// a critical section for which all time spend inside of it is measured while profiling
/** time includes the CS overhead (time to enter/leave) as well as any time spent while waiting */
template <class ScopeProfilerType>
class ProfiledCriticalSection: public CriticalSection, private ScopeProfilerType
{
  mutable int _locked;
  
  public:
  ProfiledCriticalSection()
  {
    // start with the section locked - we will unlock only when doing CB recording
    CriticalSection::Lock();
    _locked=1;
  }
  void Lock()const
  {
    ScopeProfilerType::StartProfiling();
    PROFILE_SCOPE_EX(oCSLk,oSort);
    CriticalSection::Lock();
    _locked++;
  }
  
  void Unlock()const {--_locked;CriticalSection::Unlock();ScopeProfilerType::StopProfiling();}
  bool IsLocked() const {return _locked>0;}

  //@{ used for locking outside of rendering - no profiling scope
  void LockNoProf(){CriticalSection::Lock();_locked++;}
  void UnlockNoProf(){--_locked;CriticalSection::Unlock();}
  //@}
};

#include <Es/Memory/normalNew.hpp>
CREATE_STARTABLE_SCOPE(obDCS,oSort,false,TCRender)
#include <Es/Memory/debugNew.hpp>

/// type is named to allow using ScopeLock<XX> with less typing
//typedef ProfiledCriticalSection<ScopeProfilerStartable_obDCS> ObjDrawCSType;
/// critical section to protect parts of Object::Draw tasks which are not MT safe
//ObjDrawCSType ObjDrawCS;

void Object::PrepareTextures(AnimationContext *animContext, LODShape *shape, float dist2, int level, bool prepareBaseTextureOnly)
{
  const Shape *sShape = shape->GetLevelLocked(level);
  int special = GetObjSpecial(sShape->Special());
  // scan minimal z2
  
  float minZ = shape->IsCockpit(level) ? 0.0f : 0.5f;
  float radius = sShape->BSphereRadius();
  float dist = dist2>0 ? dist2*InvSqrt(dist2) : 0;
  // we assume we can see no object from closer than 50 cm
  // this is not true when inside of the vehicle
  float distMin = floatMax(dist-radius,minZ);
  float z2Min = Square(distMin);

  sShape->PrepareTextures(z2Min, animContext, special, prepareBaseTextureOnly);
}

bool ForceTIAlive = false;
bool ForceTIMovement = false;
bool ForceTIMetabolism = false;

enum {ODPre,ODPost,ODTask,ODAll};

#if 0 
//note: code is not compilable, _locked(src._locked) ... error: 'src' was not declared in this scope
//! lock object passed as argument during lifetime of ScopeLock object
template<class LockType, class Traits=ScopeTraits<LockType> >
class ScopeLockOpt: private NoCopy
{
	protected:
	LockType &_lock;
	bool _locked;
	
	public:
	//ScopeLockOpt(LockType &lock, bool locked=true ):_lock(lock),_locked(locked){if (locked) Traits::Lock(_lock);}
	ScopeLockOpt(LockType &lock, bool locked=true ):_lock(lock),_locked(true){if (_locked) Traits::Lock(_lock);}
	/// copy constructor needed - we need to copy the value somehow
	/** we could transfer ownership instead (unlock source, lock destination)
	*/
	ScopeLockOpt(const ScopeLockOpt &scope):_lock(scope._lock),_locked(src._locked){if (_locked) Traits::Lock(_lock);}
	void Lock() {if (!_locked)Traits::Lock(_lock),_locked=true;}
	void Unlock() {if (_locked) Traits::Unlock(_lock),_locked=false;}
	~ScopeLockOpt() {if (_locked)Traits::Unlock(_lock);}
	
	//@{ UnlockTemp and LockTemp need always to be used in pairs
	/// begin temporary unlock - unlock for some area
	void UnlockTemp()
	{
	  if (_locked) Traits::Unlock(_lock);
	}
	/// end temporary unlock - unlock for some area
  void LockTemp()
	{
	  if (_locked) Traits::Lock(_lock);
	}
	//@}
};
#endif

/**
Function may change data in instances
*/
void Object::DrawSimpleInstanced(
  int cb, int forceLOD, ClipFlags clipFlags, const DrawParameters &dp,
  ObjectInstanceInfo *instances, int instanceCount, float dist2, const LightList &lights,
  SortObject *oi, float coveredArea
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
  )
{
  PROFILE_SCOPE_DETAIL_EX(obDrw,oSort)
  //REPORTSTACKITEM(_shape->GetName());

  if( forceLOD==LOD_INVISIBLE || instanceCount==0) return; // invisible LOD
#if _ENABLE_CHEATS
  if (!CheckDrawToggle()) return;
  float scale = CheckDrawScale();
  if (scale!=1.0f) for (int i=0; i<instanceCount; i++)
  {
    instances[i]._origin.SetOrientation(instances[i]._origin.Orientation() * CheckDrawScale());
  }
#endif

  //ShapeUsed sShape=_shape->Level(forceLOD);
  // TODO:MC: we need to be sure the LOD Shape or VertexBuffer was not evicted meanwhile.
  // to this we need to analyse who can evict LODs/VBs in the rendering loop
  const Shape *sShape=_shape->GetLevelLocked(forceLOD);
  if (!sShape || sShape->NVertex()==0) return;
  
  #if _ENABLE_PERFLOG
  if (LogStatesOnce)
  {
    LogF
    (
      "Draw object %s:%d, pass %d",
      _shape->Name(),forceLOD,forceLOD>=0 ? PassNum(forceLOD) : -1
    );
  }
  #endif

  #if _ENABLE_CHEATS
    if (CHECK_DIAG(DEModel))
    {
      Object *focus = GWorld->GetCameraFocusObject();
      if (focus)
      {
        for (int i=0; i<instanceCount; i++)
        {
          if (instances[i]._object==focus)
          {
            DIAG_MESSAGE_ID(0,forceLOD,"  %s: draw as %d (f %.3f)",cc_cast(focus->GetDebugName()),forceLOD,instances[i]._color.A());
            //LogF("  %s: draw as %d (f %.3f)",cc_cast(focus->GetDebugName()),forceLOD,instances[i]._color.A());
            // if the LOD was not requested, why are we drawing it?
            if (forceLOD!=oi->srcLOD && forceLOD!=oi->dstLOD)
            {
              __asm nop;
            }
            break;
          }
        }
      }
    }
  #endif

  //CurrentAppFrameFunctions->ProfileBeginGraphScope(0,"prolog");

#if _ENABLE_SKINNEDINSTANCING
  bool setEngineStuff = (
    _shape->GetAnimationType()==AnimTypeHardware &&
    _shape->LevelUsesVBuffer(forceLOD)
    );
  bool isSkinned = setEngineStuff && sShape->GetSubSkeletonSize()>0;
#else
  //Assert(_shape->GetAnimationType()==AnimTypeNone);
  //bool setEngineStuff = false;
  bool isSkinned = false;
  Assert(sShape->GetSubSkeletonSize()==0);
#endif

  int special = GetObjSpecial(sShape->Special());
  // if drawing using CPU, we should lock the CPU copy we will animate
  bool usingTL = (
    sShape->DrawUsingTL(special,clipFlags) && _shape->LevelUsesVBuffer(forceLOD)
  );
  bool usingGeometry = (
    !usingTL || _shape->GetAnimationType()==AnimTypeSoftware
  );
  Assert(usingTL);
  Assert(!usingGeometry);

  //ADD_COUNTER(bObj,sShape->NSections());
  OBJECT_LEVEL_DPRIM_STAT_SCOPE(cb,_shape,forceLOD);
  ADD_COUNTER(obj,instanceCount);

  ShapeGeometryLock<> lock;
  if (usingGeometry)
  {
    lock.Lock(_shape,forceLOD);
  }
  
  Assert(CanBeInstanced(forceLOD,dist2,dp)); // verify the function is valid to be called
  Assert(cb<0 || CanObjDrawAsTask(forceLOD)); // if recording CB, verify CBs are allowed for this object

  //CurrentAppFrameFunctions->ProfileEndGraphScope();
  
  // make sure texture loading does not evict our vertex buffer (if it exists)
  VertexBufferLock vbLock(sShape);
  
  sShape->PrepareTextures(dist2, NULL, special, dp._sbIsBeingRendered);

  // If some hints then draw non-instanced version
  if ((sShape->GetOrHints()&ClipLandOn)!=0)
  {
    DoAssert(cb<0);
    DoAssert(!isSkinned);
#if _HELISIM_RENDER_TO_TEXTURE
    UseDrawMatrices(cb, isSkinned, _shape, forceLOD, oi, drawListId);
#else
    UseDrawMatrices(cb, isSkinned, _shape, forceLOD, oi);
#endif
    
    Assert(special&OnSurface);
    Assert((sShape->GetAndHints()&ClipLandMask)==ClipLandOn);
    Assert((sShape->GetOrHints()&ClipLandMask)==ClipLandOn);
    // TODO: some optimized light selection
    for (int i=0; i<instanceCount; i++)
    {
      const ObjectInstanceInfo &info = instances[i];
      // TODO: improve light selection
      LightList work;
      const LightList &lights=GScene->SelectAggLights(info._origin.Position(),this,forceLOD,work);

      // perform actual drawing
      // Preparing of drawing of the shape

#if _ENABLE_PERFLOG
      if (LogStatesOnce)
      {
        LogF
          (
          "Pos %.1f,%.1f,%.1f, cam  %.1f,%.1f,%.1f, scale %.2f, bSphere %.6f",
          info._origin.Position().X(),
          info._origin.Position().Y(),
          info._origin.Position().Z(),
          GScene->GetCamera()->Position().X(),
          GScene->GetCamera()->Position().Y(),
          GScene->GetCamera()->Position().Z(),
          info._origin.Scale(),_shape->BoundingSphere()
          );
      }
#endif

      SCOPE_GRF("inst-road",0);

      GEngine->SetInstanceInfo(cb, info._color, info._shadowCoef);
      
      Ref<Shape> split = GScene->GetShadowCache().Split(info._object,VUp,forceLOD,OnSurface);
      int specAdj = (special&~OnSurface)|IsOnSurface;
      split->Draw(cb, NULL, HWhite, *_shape, lights, clipFlags, specAdj, info._origin, true, dp);
    }
    GEngine->SetInstanceInfo(cb, HWhite, 1);
    // Set skinning to default value (FLY Better than this clean-up would be better to set it before each drawing)
    GEngine->SetSkinningType(cb,STNone);
  }
  else
  {
    _shape->OptimizeLevelRendering(forceLOD,false);
#if _ENABLE_SKINNEDINSTANCING
    PrepareShapeDrawInstanced(cb,_shape, forceLOD, isSkinned, instances, instanceCount);
#else
    DoAssert(!isSkinned);
#if _HELISIM_RENDER_TO_TEXTURE
    UseDrawMatrices(cb, isSkinned, _shape, forceLOD, oi, drawListId);
#else
    UseDrawMatrices(cb, isSkinned, _shape, forceLOD, oi);
#endif
#endif

    Object *rootObject = NULL;
    if (oi) rootObject = oi->GetRootObject();
    if (!rootObject) rootObject = this;

    // Include TI properties into draw parameters
    DrawParameters dpTI = dp;
    float aliveFactor = ForceTIAlive ? 1.0f : (rootObject ? rootObject->GetAliveFactor() : GetAliveFactor());
    float movementFactor = ForceTIMovement ? 1.0f : (rootObject ? rootObject->GetMovementFactor() : GetMovementFactor());
    float metabolismFactor = ForceTIMetabolism ? 1.0f : (rootObject ? rootObject->GetMetabolismFactor() : GetMetabolismFactor());

    dpTI._htMin = rootObject ? rootObject->HTMin() : HTMin();
    dpTI._htMax = rootObject ? rootObject->HTMax() : HTMax(); 
    dpTI._afMax = aliveFactor * (rootObject ? rootObject->AFMax() : AFMax());
    dpTI._mfMax = movementFactor * (rootObject ? rootObject->MFMax() : MFMax());
    dpTI._mFact = metabolismFactor * (rootObject ? rootObject->MFact() : MFact());
    dpTI._tBody = rootObject ? rootObject->TBody() : TBody();

    int bias;
    if (special&(OnSurface|IsOnSurface))
    {
      bias = 0x10;
    }
    else
    {
      bias = ((special&ZBiasMask)/ZBiasStep) * 5;
    }
    
    GEngine->DrawMeshTLInstanced(
      cb,*sShape, *_shape, special, lights, bias,
      instances, instanceCount, dist2, dpTI
    );
    // Set skinning to default value (FLY Better than this clean-up would be better to set it before each drawing)
    GEngine->SetSkinningType(cb,STNone);
  }


}


/*!
* \param level LOD level index to draw.
* \param clipFlags Which clipping planes have to be tested.
* \param pos Position where the object should be draw.
* \note Parameter pos is used mainly for proper proxy object positioning.

* This is the main function used for object rendering.
* Default implementation performs Object::Animate before
* and Object::Deanimate after rendering,
* checks for fog optimization (early constant fog evaluation),
* selects lights that may influence this object,
* draws all proxies via Object::DrawProxies
* and draws given LOD level via Shape::Draw.

\patch 5160 Date 25/5/2007 by Flyman
- New: Craters are drawn on objects
*/


void Object::Draw(int cb, int forceLOD, ClipFlags clipFlags, const DrawParameters &dp, const InstanceParameters &ip, float dist2, const PositionRender &posRender, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
                  , int drawListId
#endif
                  )
{
  PROFILE_SCOPE_DETAIL_EX(obDrw,oSort)
  
  //REPORTSTACKITEM(_shape->GetName());

#if _ENABLE_CHEATS
  if (!CheckDrawToggle()) return;
  float scale = CheckDrawScale();
  PositionRender pos = posRender;
  if (scale!=1.0f)
  {
    pos.position.SetOrientation(pos.position.Orientation()*scale);
  }
#else
  const PositionRender &pos = posRender;
#endif
  #if _ENABLE_PERFLOG
  if (LogStatesOnce)
  {
    LogF
    (
      "Draw object %s:%d, pass %d",
      _shape->Name(),forceLOD,forceLOD>=0 ? PassNum(forceLOD) : -1
    );
  }
  #endif

  // Invisible LOD
  if (forceLOD==LOD_INVISIBLE) return;

  // Scale is zero
  float scale2 = pos.position.Orientation().Scale2();
  if (scale2 == 0.0f) return;

  Object *parentObject = NULL;
  if (oi) parentObject = oi->GetParentObject();
  if (!parentObject) parentObject = this;
  // If skeletons of this and the parent object don't matches, then fail
  if (_shape->GetSkeleton() != parentObject->_shape->GetSkeleton())
  {
    Fail("Error: Skeletons of this and the parent object not matching");
    return;
  }
  
  ShapeUsed sShape=_shape->Level(forceLOD);
  // avoid crash when data are corrupted
  if (sShape == NULL) return;

  bool setEngineStuff = (
    _shape->GetAnimationType()==AnimTypeHardware &&
    _shape->LevelUsesVBuffer(forceLOD)
  );
  bool isSkinned = setEngineStuff && sShape->GetSubSkeletonSize()>0;
  int special = GetObjSpecial(sShape->Special());
  
  // if drawing using CPU, we should lock the CPU copy we will animate
  bool usingTL = (
    sShape->DrawUsingTL(special,clipFlags) && _shape->LevelUsesVBuffer(forceLOD)
  );
  bool usingGeometry = (
    !usingTL || _shape->GetAnimationType()==AnimTypeSoftware
  );
  
  ShapeGeometryLock<> lock;
  if (usingGeometry)
  {
    lock.Lock(_shape,forceLOD);
  }
  // test if reference points to valid object
  // get object position in clipping coordinates
  AnimationContextStorageRender storage;
  AnimationContext animContext(RenderVisualState(),storage);
  sShape->InitAnimationContext(animContext, _shape->GetConvexComponents(forceLOD), usingGeometry);

  Animate(animContext, forceLOD, setEngineStuff, parentObject, dist2);

  // pos may be in camera space
  // dist2 is used for mipmap+proxy lod selection
  // Calculate the world position as well (it is not accurate though)

  // for clipping tests we want to check world space, as clipping planes are world space
  Vector3 worldPosition = pos.PositionWorld(*GScene->GetCamera());

  // some clipping was here, as we are not rendering via SW T&L any more, we no longer care about them

  Assert( forceLOD>=0 );

  //float radius = scale*_shape->BoundingSphere();

  #if _ENABLE_PERFLOG
  if (LogStatesOnce)
  {
    LogF
    (
      "Pos %.1f,%.1f,%.1f, cam  %.1f,%.1f,%.1f, scale %.2f, bSphere %.6f",
      worldPosition.X(),
      worldPosition.Y(),
      worldPosition.Z(),
      GScene->GetCamera()->Position().X(),
      GScene->GetCamera()->Position().Y(),
      GScene->GetCamera()->Position().Z(),
      pos.position.Scale(),_shape->BoundingSphere()
    );
  }
  #endif

  
  {
    // Modify the draw parameter to contain the own heat source (or not), don't override if current model doesn't have own heat source (inherit from the parent)

    DrawParameters dpTI = dp;
    Vector3 newHeatSource;
    if (GetOwnHeatSource(newHeatSource))
    {
      dpTI._heatSourceDirection = newHeatSource;
      dpTI._useOwnHeatSource = true;
    }

    LightList work;
    const LightList &lights=GScene->SelectAggLights(worldPosition, this, forceLOD, work);
    
    OBJECT_LEVEL_DPRIM_STAT_SCOPE(cb,_shape,forceLOD);
    ADD_COUNTER(obj,1);

    if( special&IsColored ) GScene->SetConstantColor(GetConstantColor()); 
    
#if _VBS3_CRATERS_BIAS
    // Set the model bias
    dpTI._bias = Bias();
#endif

    // Include TI properties into draw parameters
    float aliveFactor = ForceTIAlive ? 1.0f : (parentObject ? parentObject->GetAliveFactor() : GetAliveFactor());
    float movementFactor = ForceTIMovement ? 1.0f : (parentObject ? parentObject->GetMovementFactor() : GetMovementFactor());
    float metabolismFactor = ForceTIMetabolism ? 1.0f : (parentObject ? parentObject->GetMetabolismFactor() : GetMetabolismFactor());

    dpTI._htMin = parentObject ? parentObject->HTMin() : HTMin();
    dpTI._htMax = parentObject ? parentObject->HTMax() : HTMax(); 
    dpTI._afMax = aliveFactor * (parentObject ? parentObject->AFMax() : AFMax());
    dpTI._mfMax = movementFactor * (parentObject ? parentObject->MFMax() : MFMax());
    dpTI._mFact = metabolismFactor * (parentObject ? parentObject->MFact() : MFact());
    dpTI._tBody = parentObject ? parentObject->TBody() : TBody();
    dpTI._defTI = parentObject ? parentObject->GetDefTIMode() : GetDefTIMode();
    
    // if necessary, split it
    if (!(sShape->Special()&OnSurface) || (sShape->GetAndHints()&ClipLandMask)!=ClipLandOn)
    {
      // make sure texture loading does not evict our vertex buffer (if it exists)
      VertexBufferLock vbLock(sShape);
      
      PrepareTextures(&animContext, _shape, dist2/scale2, forceLOD, dp._sbIsBeingRendered);
      
      _shape->OptimizeLevelRendering(forceLOD,false);
      // Preparing of drawing of the shape
#if _HELISIM_RENDER_TO_TEXTURE
      UseDrawMatrices(cb, isSkinned, _shape, forceLOD, oi, drawListId);
#else
      UseDrawMatrices(cb, isSkinned, _shape, forceLOD, oi);
#endif

      sShape->Draw(cb,&animContext, GetConstantColor(), *_shape, lights, clipFlags, special, pos, dist2, dpTI);

      // Draw craters (omit DB and z-prime passes), when predicating, predication will take care of omitting it
      if (!dp._zprimeIsBeingRendered || Glob.config._predication)
      {
        int maxCraters = CheckCraterDrawing(dist2, dp);
        if (maxCraters > 0)
        {
#if _BINARIZE_REDUCEOBJECT
          Fail("Waring: Object functionality reduced");
#else
          // Setup craters PS constants
          static const AutoArray<CraterProperties> noCraters;
          
          const AutoArray<CraterProperties> &craters = _damage ? _damage->_craters : noCraters;
          GEngine->UseCraters(cb,craters, maxCraters,  CraterColor());

          // Draw it
          DrawParameters dpc = dp;
          dpc._cratersCount = std::min(craters.Size(), maxCraters);
          sShape->Draw(cb, &animContext, GetConstantColor(), *_shape, lights, clipFlags, special, pos, dist2, dpc);
#endif
        }
      }
    }
    else
    {
      // when rendering shadow buffer, rendering what is on surface has no sense
      if (!dp._sbIsBeingRendered)
      {
        PrepareTextures(&animContext, _shape, dist2/scale2, forceLOD, dp._sbIsBeingRendered);

#if _HELISIM_RENDER_TO_TEXTURE
        UseDrawMatrices(cb, false, _shape, forceLOD, oi, drawListId);
#else
        UseDrawMatrices(cb, false, _shape, forceLOD, oi);
#endif
        
        // if all point are LandOn, we can split cache
        Ref<Shape> split = GScene->GetShadowCache().Split(this,VUp,forceLOD,special);
        int specAdj = (special&~OnSurface)|IsOnSurface;
        split->Draw(cb, &animContext, GetConstantColor(), *_shape, lights, clipFlags, specAdj, pos, dist2, dpTI);
      }
    }

    // Set skinning to default value (FLY Better than this clean-up would be better to set it before each drawing)
    GEngine->SetSkinningType(cb,STNone);
  }


  Deanimate(forceLOD,setEngineStuff);
}

void Object::DrawShadowVolumeInstanced(
  int cb, LODShape *shape, int level, const ObjectInstanceInfo *instances, int instanceCount, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  PROFILE_SCOPE_DETAIL_EX(obDrw,oSort)

  shape->OptimizeLevelRendering(level);
#if _ENABLE_SKINNEDINSTANCING
  // Prepare matrices
  ShapeUsed sShape = shape->Level(level);
  bool setEngineStuff = (
    _shape->GetAnimationType()==AnimTypeHardware &&
    _shape->LevelUsesVBuffer(level)
    );
  PrepareShapeDrawInstanced(shape, level, setEngineStuff, instances, instanceCount);
#else
  Assert(_shape->GetAnimationType()==AnimTypeNone);
  // TODO: optimization: consider passing ShapeUsed into OptimizeLevelRendering
  ShapeUsed sShape = shape->Level(level);
//  UseDrawMatrices(shape, level, false, this);
#endif

#if _HELISIM_RENDER_TO_TEXTURE
  GEngine->DrawShadowVolumeInstanced(cb, shape, level, instances, instanceCount, oi, drawListId);
#else
  GEngine->DrawShadowVolumeInstanced(cb,shape, level, instances, instanceCount,oi);
#endif

  // Set skinning to default value (FLY Better than this clean-up to PrepareShapeDraw would be better to set it before each drawing)
  GEngine->SetSkinningType(cb,STNone);
}

void Object::DrawShadowVolume(int cb, LODShape *shape, int level, const PositionRender &frame, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
                              , int drawListId
#endif
                              )
{
  PROFILE_SCOPE_DETAIL_EX(obDrw,oSort)
  // Are we able to draw a HW shadow?

  shape->OptimizeLevelRendering(level);
  ShapeUsed shadow = shape->Level(level);
  if (!shadow->LockVertexBuffer() || shadow->NSections()<=0) return;
  bool setEngineStuff = shape->GetAnimationType()==AnimTypeHardware;
  bool usingGeometry = shape->GetAnimationType()==AnimTypeSoftware;
  ShapeGeometryLock<> lock;
  if (usingGeometry)
  {
    lock.Lock(_shape,level);
  }
  //Assert(setEngineStuff);
  AnimationContextStorageRender storage;
  AnimationContext animContext(RenderVisualState(),storage);
  shadow->InitAnimationContext(animContext, shape->GetConvexComponents(level), false);
  // assume distance does not matter for shadows
  Animate(animContext, level, setEngineStuff, this, -FLT_MAX);


  bool isSkinned = setEngineStuff && shadow->GetSubSkeletonSize()>0;
  // Set animation matrices and skinning type
#if _HELISIM_RENDER_TO_TEXTURE
  UseDrawMatrices(cb, isSkinned, shape, level, oi, drawListId);
#else
  UseDrawMatrices(cb, isSkinned, shape, level, oi);
#endif
  
  // Is there any geometry to draw?
  // Calculate shadow distance
  // for proxies, get shadow distance from the parent object
  Object *parentObject = NULL;
  if (oi) parentObject = oi->GetParentObject();
  if (!parentObject) parentObject = this;

  Vector3 minMax[2];  
  parentObject->ClippingInfo(parentObject->RenderVisualState(), minMax, ClipShadow);
  
  Matrix4 worldFrame = frame.TransformWorld(*GScene->GetCamera());
  float shadowSize = GScene->CheckShadowSize(minMax,worldFrame,0);
  
  ADD_COUNTER(oShdV,1);
  DPRIM_STAT_SCOPE(cb<0 ? (shape)->ShapeName((level)) : "",ShdV);
  
  // Prepare shadow volume drawing
  GEngine->BeginShadowVolume(cb,shadowSize);

  // ---------------
  // Draw back faces

  // Set shadow material
  TLMaterial shadowMat;
  shadowMat.diffuse = HBlack;
  shadowMat.ambient = HBlack;
  shadowMat.emmisive = HBlack;
  shadowMat.forcedDiffuse = HBlack;
  shadowMat.specFlags = 0;

  EngineShapeProperties props;
  
  // source of vertices
  const VertexTableAnimationContext *src = animContext.GetVertexTableContext();

  // Start the mesh
  int spec=IsShadow|IsAlphaFog;
  const LightList noLights;
  GEngine->BeginInstanceTL(cb,frame, -1.0f, 0, noLights);
  bool ok = GEngine->BeginMeshTL(cb,src, *shadow, props, spec, noLights, ShadowBias);
  if (ok)
  {

    // Draw back faces
    for (int i = 0; i < shadow->NSections(); i++)
    {
      const PolyProperties &sec = animContext.GetSection(shadow, i);

      // Skip sections not designed for drawing
      if (sec.Special() & (IsHidden|IsHiddenProxy|NoShadow)) continue;

      // Draw section
      PolyProperties section = sec;
      section.OrSpecial(IsShadow | IsShadowVolume /*| ShadowVolumeFrontFaces*/);
      section.SetTexture(NULL);
      section.PrepareTL(cb, shadowMat, 0, props);

      GEngine->DrawSectionTL(cb,*shadow, i, i+1, ShadowBias);
    }

    if (!GEngine->CanTwoSidedStencil())
    {
      // Draw front faces if needed
      for (int i = 0; i < shadow->NSections(); i++)
      {
        const PolyProperties &sec = animContext.GetSection(shadow, i);

        // Skip sections not designed for drawing
        if (sec.Special() & (IsHidden|IsHiddenProxy|NoShadow)) continue;

        // Draw section
        PolyProperties section = sec;
        section.OrSpecial(IsShadow | IsShadowVolume | ShadowVolumeFrontFaces);
        section.SetTexture(NULL);
        section.PrepareTL(cb, shadowMat, 0, props);

        GEngine->DrawSectionTL(cb,*shadow, i, i+1, ShadowBias);
      }
    }

    // Finish the mesh
    GEngine->EndMeshTL(cb,*shadow);
  }

  // Set skinning to default value (FLY Better than this clean-up to PrepareShapeDraw would be better to set it before each drawing)
  GEngine->SetSkinningType(cb,STNone);

  Deanimate(level, setEngineStuff);
}

void Object::DrawExShadow(
  int cb, LODShape *shape, const PositionRender &pos, int level, bool isVolume, bool usePilotSV, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  // If shadow volume exists, then use it
  if (isVolume)
  {
    // HW only. SW animation may break polygon normals, which are needed for volume extrusion
    if (shape->GetAnimationType()!=AnimTypeSoftware)
    {
#if _HELISIM_RENDER_TO_TEXTURE
      DrawShadowVolume(cb, shape, level, pos, oi, drawListId);
#else
      DrawShadowVolume(cb, shape, level, pos, oi);
#endif
    }
  }
  else
  {
    Fail("Projected shadows no longer supported");
  }

}

int Object::GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const
{
  return 0;
}

int ObjectTyped::GetProxyComplexity(int level, const Matrix4 &pos, float dist2) const
{
  int nFaces = 0;

  // calculate default proxies (included in shape)
  ShapeUsed lock = _shape->Level(level);
  int n = _type->GetProxyCountForLevel(level);
  for( int i=0; i<n; i++ )
  {
    const ProxyObjectTyped &proxy=_type->GetProxyForLevel(level,i);
    
    // smart clipping par of obj->Draw
    Matrix4Val pTransform=pos*proxy.obj->RenderVisualState().Transform();

    // LOD detection
    LODShape *pshape = proxy.obj->GetShape();
    if (!pshape) continue;
    int level=GScene->LevelFromDistance2(
      pshape,dist2,pTransform.Scale()
    );
    if( level==LOD_INVISIBLE ) continue;

    nFaces += proxy.obj->GetComplexity(level,pTransform);
  }

  return nFaces;
}

bool Object::IgnoreComplexity() const
{
  return false;
}

int Object::GetComplexity(int level, const Matrix4 &pos) const
{
  // preview draw with given level
  // return number of faces
  if( level==LOD_INVISIBLE ) return 0; // invisible LOD
  LODShape *lshape = GetShape();
  if (!lshape) return 0;
  //ShapeUsed shape = lshape->Level(level);
  RefShapeRef shape = lshape->LevelRef(level);
  int nFaces = shape.NFaces();
  if (!shape.IsLoaded()) return nFaces;
  /// TODO: manage this information out of Shape
  if (shape->NProxies()>0)
  {
    //Fail("Proxies without ObjectTyped");
    float dist2=GScene->GetCamera()->Position().Distance2(pos.Position());
    nFaces += GetProxyComplexity(level,pos,dist2);
  }
  return nFaces;

}

int ObjectTyped::GetComplexity(int level, const Matrix4 &pos) const
{
  // preview draw with given level
  // return number of faces
  if( level==LOD_INVISIBLE ) return 0; // invisible LOD
  LODShape *lshape = GetShape();
  if (!lshape) return 0;
  RefShapeRef shape = lshape->LevelRef(level);
  int nFaces = shape.NFaces();
  if (shape.IsNull()) return nFaces;
  shape.Lock();
  int nProxies = _type->GetProxyCountForLevel(level);
  if (nProxies>0)
  {
    float dist2=GScene->GetCamera()->Position().Distance2(pos.Position());
    nFaces += GetProxyComplexity(level,pos,dist2);
  }
  shape.Unlock();
  return nFaces;

}

void Object::DrawDecal( int cb, int forceLOD, ClipFlags clipFlags, const Matrix4 &pos )
{
  // Invisible LOD
  if( forceLOD==LOD_INVISIBLE ) return;

  ShapeUsed shape=_shape->Level(forceLOD);
  Assert( shape->NSections()==1 );
  if (shape->NSections()<=0) return;

  OBJECT_LEVEL_DPRIM_STAT_SCOPE(cb,_shape,forceLOD);
  ADD_COUNTER(obj,1);

  // many object are not fogged at all
  // disable fog calculation for them
  int special = GetObjSpecial(shape->Special());

  const Camera &camera=*GScene->GetCamera();

  // following line is faster version
  Vector3 posp=GScene->ScaledInvTransform()*pos.Position();

  // camera plane clip test
  // perform more distant clipping than normal
  float nearest=camera.Near()*10;
  if( posp.Z()<nearest ) return;

  float size=RenderVisualState().Scale();

  bool setEngineStuff = _shape->GetAnimationType()==AnimTypeHardware;

  AnimationContextStorageRender storage;
  AnimationContext animContext(RenderVisualState(),storage);
  shape->InitAnimationContext(animContext, _shape->GetConvexComponents(forceLOD), false);

  Animate(animContext, forceLOD, setEngineStuff, this, posp.SquareSize());

  // scale and 1/scale is kept in Frame

  // apply perspective on position and size
  Matrix4Val project=camera.Projection();

  float invW=1/posp[2];
  posp[0]=project(0,2)+project(0,0)*posp[0]*invW;
  posp[1]=project(1,2)+project(1,1)*posp[1]*invW;
  //pos[2]=(project.Position()[2]-size*0.1)*invW;
  float nearPos=floatMax(posp[2]-size,nearest);
  float invNearPos = 1/nearPos;
  posp[2]=project(2,2)+project.Position()[2]*invNearPos;
  float rhw = invNearPos;

  // perspective screen size
  float sizeX=+project(0,0)*size*invW*camera.InvLeft()*0.5;
  float sizeY=-project(1,1)*size*invW*camera.InvTop()*0.5;

  if( sizeX*sizeY>=0.5 )
  {
    // never draw too small objects
    // TODO: mip-map management
    // get texture from the section instead
    const PolyProperties &sec = animContext.GetSection(shape, 0);
    Texture *texture = sec.GetTexture();
    // calculate single-point single-normal lighting
    // world space normal is opposite to camera direction
    ClipFlags hints = shape->GetOrHints();
    Color colorI;
    int mat = (hints&ClipUserMask)/ClipUserStep;
    if (mat!=MSShining)
    {
      TLMaterial mat;
      mat.ambient = HWhite;
      mat.diffuse = HWhite*0.25;
      mat.emmisive = HBlack;
      mat.forcedDiffuse = HWhite*0.25;
      mat.specFlags = 0;
      LightSun *sun=GScene->MainLight();
      sun->SetMaterial(mat, GEngine->GetAccomodateEye());
      colorI=sun->FullResult(0.5);
      float addLightsFactor=sun->NightEffect(); // DAY_LIGHTS? here probably not
      if( addLightsFactor>0.01 )
      {
        const LightList &lights=GScene->ActiveAggLights();
        if( lights.Size()>0 )
        {
          Matrix4 worldToModel(MTranslation,-pos.Position());
          for( int index=0; index<lights.Size(); index++ )
          {
            // TODO: PrepareDecal function
            lights[index]->Prepare(worldToModel);
            lights[index]->SetMaterial(mat, GEngine->GetAccomodateEye());
          }

          // TODO: consider: special lighting modes
          for( int index=0; index<lights.Size(); index++ )
          {
            colorI+=lights[index]->ApplyPrecalc(VZero,VForward);
          }
        }
      }
      colorI.SetA(1);
    }
    else
    {
      colorI = GEngine->GetAccomodateEye();
    }
    if( special&IsColored ) colorI=colorI*(ColorVal)GetConstantColor();
    // check if there are not some unsupported lighting flags
    //float fog=GScene->Fog(pos.SquareSize());
    float dist2=RenderVisualState().Position().Distance2(camera.Position());
    float fog=GScene->Fog8(dist2)*(1.0/255);
    if( special&IsAlphaFog ) colorI.SetA((1-fog)*colorI.A());
    else colorI.SetA(1-(1-fog)*colorI.A());
    PackedColor color(colorI);
    // setup drawing
    MipInfo mip=GEngine->TextBank()->UseMipmap(texture,1,0);
    Assert( mip.IsOK() );
    if( !mip.IsOK() ) return;
    //GEngine->PrepareTriangle(mip,special);
    GEngine->DrawDecal(cb,posp,rhw,sizeX,sizeY,color,mip,GlobPreloadMaterial(NonTLMaterial),special);
    // drawn - clean up
    //GEngine->TextBank()->ReleaseMipmap();
  }
  Deanimate(forceLOD,setEngineStuff);
}

/*!
\note Only simple rectangles are allowed in 2D layers
\todo Move to LODShape class
\patch 1.63 Date 6/1/2002 by Ondra
- Fixed: Shining faces in optics and ironsight view did not shine.
\patch 5153 Date 4/10/2007 by Flyman
- Fixed: Optics were deformed when using other than 4:3 screen ratio
*/

#if _VBS3 //enable to uses different zoom
void Object::Draw2D(AnimationContext *animContext, LODShape *lShape, int lod, ColorVal cColor, float zoom)
#else
void Object::Draw2D(AnimationContext *animContext, LODShape *lShape, int lod, ColorVal cColor)
#endif
{
  // TODO: 2D via non-perspective projection 
  float xMin=+1e10,xMax=-1e10;
  float yMin=xMin,yMax=xMax;

  ShapeUsed shape=lShape->Level(lod);
  if (shape.IsNull()) return;

  if( shape->NFaces()<=0 ) return;

  OBJECT_LEVEL_DPRIM_STAT_SCOPE(-1,_shape,lod);
  ADD_COUNTER(obj,1);

  // scan all faces for max x,y coordinated
  for( int v=0; v<shape->NPos(); v++ )
  {
    Vector3Val vv=shape->Pos(v);
    saturateMin(xMin,vv.X()),saturateMax(xMax,vv.X());
    saturateMin(yMin,vv.Y()),saturateMax(yMax,vv.Y());
  }

  // Calculate coefficients used to multiply the coordinates
  float xCoef;
  float yCoef;
  {
    // Get the resize coefficients based on aspect settings
    float xResizeCoef;
    float yResizeCoef;
    {
      // Get the aspect settings
      AspectSettings as;
      GEngine->GetAspectSettings(as);

      // Detect whether aspect is wider (the usual situation) or higher and either make the image smaller in x or y dimension
      if (as.leftFOV > as.topFOV)
      {
        xResizeCoef = as.topFOV / as.leftFOV;
        yResizeCoef = 1.0f;
      }
      else
      {
        xResizeCoef = 1.0f;
        yResizeCoef = as.leftFOV / as.topFOV;
      }
    }

    // Calculate the coefficients - without *ResizeCoef it would behave like a square
    float enlargeCoef = 1.02f; // Some reserve at the borders
#if _VBS3
    enlargeCoef *= zoom;
#endif

    const float sourceModelAspect = 4.0f/3.0f;
    const float borderCoef = 5.0f; // How many times is the whole model bigger than the 4:3 screen inside
    xCoef=Inv(xMax-xMin) * enlargeCoef * xResizeCoef * sourceModelAspect * borderCoef;
    yCoef=Inv(yMax-yMin) * enlargeCoef * yResizeCoef * borderCoef;
  }

  // Get the model average XY coordinates - to know where the center is
  float xAvg=(xMax+xMin)*0.5;
  float yAvg=(yMax+yMin)*0.5;

  // Final coordinates are to be stretched to <0,w> interval, that's why we have to remember width and height
  float w=GEngine->WidthBB();
  float h=GEngine->HeightBB();

  // Draw all faces
  for (int s=0; s<shape->NSections(); s++)
  {
    const ShapeSection &ss = shape->GetSection(s);
    const PolyProperties &pp = animContext ? animContext->GetSection(shape, s) : ss;

    if (pp.Special() & (IsHidden|IsHiddenProxy)) continue;
    Texture *texture = pp.GetTexture();

    TLMaterial mat;
    CreateMaterial(mat,GetConstantColor());

    const int clampMask=NoClamp|ClampU|ClampV;
    int spec=NoZBuf|IsAlpha|IsAlphaFog|(clampMask & pp.Special());
    TexMaterial *secMat = pp.GetMaterialExt();
    TLMaterial matMod;
    if (secMat)
    {
      secMat->Combine(matMod,mat);
      if (secMat->GetRenderFlag(RFAddBlend)) spec |= DstBlendOne;
    }
    else
    {
      matMod = mat;
    }

#if 0
    LightSun *sun=GScene->MainLight();
    Color accom = GEngine->GetAccomodateEye();
    
    sun->SetMaterial(matMod, accom);

    Color colorI = sun->FullResult(0.5);
    colorI = colorI + matMod.emmisive * accom;
    PackedColor color(colorI);
#else
    PackedColor color(255, 255, 255, 255); // no change color defined by texture
#endif    

    //Draw2DParsExt pars;
    MipInfo mip=GEngine->TextBank()->UseMipmap(texture,0,0);
    //pars.SetColor(color);
    
    Vertex2DAbs vert[4];
    
    for( Offset o=ss.beg; o<ss.end; shape->NextFace(o))
    {
      const Poly &face=shape->Face(o);
      if( face.N()>4 )
      {
        RptF(
          "Bad poly in 2D object %s - %s",
          (const char *)lShape->GetName(),
          texture ? (const char *)texture->GetName() : "<NULL>"
        );
        Fail("Bad poly in 2D object");
        continue; // draw only rectangular faces
      }
      for (int i=0; i<face.N(); i++)
      {
        int vi = face.GetVertex(i);
        Vertex2DAbs &v = vert[i];
        v.x = ((shape->Pos(vi).X() - xAvg) * xCoef + 0.5f) * w;
        v.y = ((yAvg - shape->Pos(vi).Y()) * yCoef + 0.5f) * h;
        v.z = 0.5;
        v.color = color;
        v.u = shape->U(vi);
        v.v = shape->V(vi);
      }
      GEngine->DrawPoly(mip,vert,face.N(),Rect2DClipAbs,spec);
      // drawn - clean up
      //GEngine->TextBank()->ReleaseMipmap();
    }
  }
}

inline int GetAlpha(ClipFlags clip)
{
  return (clip&ClipUserMask)/ClipUserStep;
}

void Object::Draw2D(AnimationContext *animContext, int lod)
{
  LightSun *sun=GScene->MainLight();
  Color colorI = sun->SimulateDiffuse(0.5);
  ColorVal accom = GEngine->GetAccomodateEye();
  colorI = colorI*accom;
  colorI.SetA(1);
  int sSpecial = GetSpecial();
  ColorVal cColor = sSpecial&IsColored ? GetConstantColor() : HWhite;
  Draw2D(animContext, _shape, lod, cColor);
}

/**
used for diagnostics rendering
*/

void Object::DrawLines(int cb, int level, ClipFlags clipFlags, const Matrix4 &frame)
{
  // Invisible LOD
  if( level==LOD_INVISIBLE ) return;

  DoAssert(cb<0); // CBs not supported because of GEngine->DrawLine
  
  // test if reference points to valid object
  // get object position in clipping coordinates
  //Point3 pos(VFastTransform,GScene->ScaledInvTransform(),frame.Position());
  
  // determine which LOD will be used
  Assert( level>=0 );
  //bool alpha=false;
  ShapeUsed sShape=_shape->Level(level);
  if( sShape->NPos()<=0 ) return;
  
  //if( sShape->NPos()!=2 ) return; // TODO: general line drawing

  bool setEngineStuff = _shape->GetAnimationType()==AnimTypeHardware;

  AnimationContextStorageRender storage;
  AnimationContext animContext(RenderVisualState(),storage);
  sShape->InitAnimationContext(animContext, _shape->GetConvexComponents(level), false);

  // TODO: insert proper distance here
  // note: distance is currently used for texture animation, which is probably not used here at all
  Animate(animContext, level, setEngineStuff, this, 0);

  if( clipFlags )
  {
    // try to clip bounding sphere
    // if whole bounding sphere is out, object is already skipped
    clipFlags &= GScene->GetCamera()->MayBeClipped(frame.Position(),GetRadius());
  }
  
  Matrix4Val pointView=GScene->ScaledInvTransform()*frame;

  //LightSun *sun=GScene->MainLight();
  // perform lighting
  // first of all apply directional light to all normals
  // this can be done at TLVertexTable level
  //
  //Matrix4Val invScale=GScene->GetCamera()->InvScaleMatrix();

  // draw all points
  TLVertexTable tlTable(NULL, *sShape.GetShape(), pointView);
  ClipFlags clipAnd=clipFlags;
  clipFlags&=tlTable.CheckClipping(*GScene->GetCamera(),clipFlags,clipAnd);
  if( !clipAnd )
  {
    GScene->SetConstantColor(GetConstantColor());

    LightList noLights;
    tlTable.DoLighting(GetConstantColor(),MIdentity,noLights,*sShape.GetShape(),sShape->Special());

    // clip
    FaceArray clippedFaces(sShape->_face.Size(),false);
    clippedFaces.Clip(sShape->_face,tlTable,*GScene->GetCamera(),clipFlags,false);

    if( clippedFaces.Begin()<clippedFaces.End() )
    {
      GEngine->PrepareMesh(cb, sShape->Special());
      GEngine->SetBias(cb, 0);
      tlTable.DoPerspective(*GScene->GetCamera(),clipFlags);

      // TODO:MC: implement MC compatible drawing here, or remove the function
      
      // draw first edge of all faces
      if (GEngine->BeginMesh(cb, tlTable,sShape->Special()))
      {
        int spec = sShape->Special()&DstBlendOne;
        for( Offset o=clippedFaces.Begin(); o<clippedFaces.End(); clippedFaces.Next(o) )
        {
          const Poly &face=clippedFaces[o];
          Assert( face.N()>=2 );
          GEngine->DrawLine(tlTable,face.GetVertex(0),face.GetVertex(1),spec);
        }
        GEngine->EndMesh(cb, tlTable);
      }
      else
      {
        RptF("Cannot draw %s: too many vertices?",(const char *)GetShape()->GetName());
      }
    }
  }

  Deanimate(level, true);
}


ObjectDestroyAnim::ObjectDestroyAnim(LODShape *shape, ParamEntryPar cls)
{
  
  if (shape->GetAnimationType()!=AnimTypeHardware)
  {
    shape->SetAnimationType(AnimTypeHardware);
  }

  // Get and assign the skeleton
  Skeleton *skeleton = shape->LoadSkeletonFromSource();
  if (!skeleton)
  {
    RptF("No skeleton given for %s destruction",cc_cast(shape->GetName()));
    return;
  }
  
  REPORTSTACKITEM(skeleton->GetName());
  
  ParamEntryVal anims = cls>>"animations";
  _animations.Realloc(anims.GetSize());
  for (int i=0; i<anims.GetSize(); i++)
  {
    AnimationRTName name;
    if (anims[i].GetItemCount()!=3)
    {
      RptF("Bad animation in %s",(const char *)cls.GetContext());
      continue;
    }
    AnimInfo &info = _animations.Append();
    name.name = anims[i][0];
    name.skeleton = skeleton;
    name.reversed = false;
    name.streamingEnabled = false;
    info._anim = new AnimationRT(name);
    info._probab = anims[i][1];
    info._anim->Prepare(false);
    info._duration = anims[i][2];
  }
  _animations.Compact();
}

ObjectDestroyAnim::~ObjectDestroyAnim()
{
}

int ObjectDestroyAnim::Select(float kind) const
{
  Assert(_animations.Size()>0);
  for (int i=0; i<_animations.Size()-1; i++)
  {
    float p = _animations[i]._probab;
    kind -= p;
    if (kind<=0) return i;
  }
  return _animations.Size()-1;
}

float ObjectDestroyAnim::GetAnimTime(float kind) const
{
  return _animations[Select(kind)]._duration;
}

void ObjectDestroyAnim::PrepareShapeDrawMatrices(
  Matrix4Array &matrices, int level, float time, float kind, const Shape *shape, const Skeleton *skeleton
)
{
  _animations[Select(kind)]._anim->PrepareMatrices(matrices, time, 1, shape,skeleton);
}

DEFINE_CASTING(Object)
DEFINE_CASTING(ObjectTyped)

ObjectDestroyAnim *Object::GetDestroyAnimation() const
{
  return NULL;
}

bool Object::PrepareShapeDrawMatrices( Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level )
{
  if (!shape) return false;
  /// prepare identity matrix array
  shape->InitDrawMatrices(matrices,level);
  
  ObjectDestroyAnim *anim = GetDestroyAnimation();
  if (!anim) return false;
  
  if (GetDestroyed()<=0)
  {
    // if not destroyed, but there is a destruction animation leave identity
    return true;
  }
  DoAssert(shape->IsLevelLocked(level));
  const Shape *shapeL = shape->GetLevelLocked(level);
  float kind = GLandscape->GetRandomValueForObject(this);

  // Get the shape and make sure it already exists
  // make sure matrices are reset, not blended
  matrices.Resize(0);
  anim->PrepareShapeDrawMatrices(matrices, level, GetDestroyed(), kind, shapeL,shape->GetSkeleton());
  // if there is some bounding center, we need to adjust the animation it
  Vector3 bCenter =  shape->BoundingCenter();
  
  if (bCenter.SquareSize()>1e-8)
  {
    Matrix4 translate(MTranslation,bCenter);
    Matrix4 invTranslate(MTranslation,-bCenter);
    for (int i=0; i<matrices.Size(); i++)
    {
      Matrix4 &m = matrices[i];
      m = invTranslate*m*translate;
    }
  }
  return true;
}

bool Object::IsDestroyAnimated() const
{
#if _BINARIZE_REDUCEOBJECT
  //Fail("Waring: Object functionality reduced"); // slows down binarization
  return false;
#else
  return _destroyPhase>0 && GetDestroyAnimation()!=NULL;
#endif
}

DEF_RSB(no)
DEF_RSB(engine)
DEF_RSB(wreck)
DEF_RSB(building)
DEF_RSB(tree)
DEF_RSB(tent)
#if _VBS2
DEF_RSB(treez)
#endif


MatrixCache GMatrixCache;

const ObjectVisualState &Object::RenderVisualStateRaw() const
{
  // return the value from the current visual state
  return FutureVisualStateRaw();
}

bool Object::StoreDrawMatricesNeeded(bool setEngineStuff, int level, SortObject *so, SortObject *soRoot)
{
  #if _PROFILE || _DEBUG
    static bool UseMatrixCache = true;
    if (!UseMatrixCache) return false;
  #endif
  // no matrices are needed
  if (!setEngineStuff) return false;


  LODShape *lShape = GetShape();
  DoAssert(lShape->IsLevelLocked(level));
  const Shape *shape = lShape->GetLevelLocked(level);

  // Set the matrices size and fill it out with identities
  int nMatrices = shape->GetSubSkeletonSize();
  // if not skinned, no need to create a cache entry
  if (nMatrices<=0)
  {
    return false;
  }
  
  SortObject::MatrixCacheItem *mc;
  if (this==soRoot->GetObject())
  {
    // note: SortObjects persists between frames, the memory here persists as well
    soRoot->_matrixCache.Realloc(lShape->NLevels());
    soRoot->_matrixCache.Resize(lShape->NLevels());
    mc = &soRoot->_matrixCache[level];
  }
  else 
  {
    if (so->_rootIndex<0)
    {
      so->_rootIndex = soRoot->_nItems++;
      soRoot->_matrixCacheProxy.Access(so->_rootIndex);
    }
    // note: SortObjects persists between frames, the memory here persists as well
    soRoot->_matrixCacheProxy[so->_rootIndex].Realloc(lShape->NLevels());
    soRoot->_matrixCacheProxy[so->_rootIndex].Resize(lShape->NLevels());
    mc = &soRoot->_matrixCacheProxy[so->_rootIndex][level];
  }
  
  if (nMatrices!=mc->Size() && nMatrices>0)
  {
    // TODO: optimize memory allocations here
    mc->Resize(nMatrices);
    return true;
  }
  return mc->_frameId!=FreeOnDemandFrameID();
}

void Object::StoreDrawMatrices(LODShape *shape, int level, Object *parentObject, const SortObject *soRoot, int rootIndex)
{
  // calculate the matrices
  MATRIX_4_ARRAY(matrix, 128);
  const VisualState &vs = parentObject->RenderVisualState();
  if (parentObject->PrepareShapeDrawMatrices(matrix, vs, shape, level))
  {
    VerifyMatrixArrayWasSet(matrix, shape, level);
    SortObject::MatrixCacheItem *mc;
    if (this==soRoot->GetObject())
    {
      mc = &soRoot->_matrixCache[level];
    }
    else
    {
      mc = &soRoot->_matrixCacheProxy[rootIndex][level];
    }
    // copy matrices to the cache
    Assert(mc->Size()==matrix.Size());
    memcpy(mc->Data(),matrix.Data(),matrix.Size() * sizeof(Matrix4));
    mc->_frameId = FreeOnDemandFrameID();
  }
}

void Object::UseDrawMatrices(int cb, bool setEngineStuff, LODShape *shape, int level, SortObject *so
#if _HELISIM_RENDER_TO_TEXTURE
                             , int drawListId
#endif
                             ) const
{
  // isn't there possible the item is in the cache and we don't want it?
  if (!setEngineStuff)
  {
    GEngine->SetSkinningType(cb, STNone);
    return;
  }

  if (so)
  {
  
    // check root cache if needed
    const Object *root = so->GetRootObject();
    if (!root) root = this;
#if _HELISIM_RENDER_TO_TEXTURE
    const SortObject *soRoot = root->GetInList(drawListId);
#else
    const SortObject *soRoot = root->GetInList();
#endif
    if (!soRoot) soRoot = so;
    const Array<SortObject::MatrixCacheItem> *mc = NULL; 
    if (soRoot==so)
    {
      mc = &so->_matrixCache;
    }
    else if (so->_rootIndex>=0 && so->_rootIndex<soRoot->_matrixCacheProxy.Size())
    {
      mc = &soRoot->_matrixCacheProxy[so->_rootIndex];
    }
    if (mc && mc->Size()>level)
    {
      // the slot should be created
      const SortObject::MatrixCacheItem &item = mc->Get(level);
      // check if it was filled during this frame
      if (item._frameId == FreeOnDemandFrameID())
      {
        // stored in the cache
        if (item.Size() == 0)
          GEngine->SetSkinningType(cb, STNone);
        else
          GEngine->SetSkinningType(cb, STPoint4, item.Data(), item.Size());
        return;
      }
    }
  }

  // fall-back
  // TODO: try to avoid this
  {
    // not found (occurs in 1st person rendering)
    
    // if there exists a SortObject, it should be initialized
    // Assert(!so);

    // calculate the matrices
    MATRIX_4_ARRAY(matrix, 128);
    const Object *parentObject = NULL;
    if (so) parentObject = so->GetParentObject();
    if (!parentObject) parentObject = this;
    ProtectedVisualState<const VisualState> vs = parentObject->RenderVisualStateScope();
    // TODO: make PrepareShapeDrawMatrices const instead of using unconst_cast
    if (unconst_cast(parentObject)->PrepareShapeDrawMatrices(matrix, *vs, shape, level))
    {
      VerifyMatrixArrayWasSet(matrix, shape, level);
      GEngine->SetSkinningType(cb, STPoint4, matrix.Data(), matrix.Size());
    }
    else
    {
      GEngine->SetSkinningType(cb, STNone);
    }
  }
}

/*
void Object::PrepareShapeDraw(int cb, LODShape *shape, int level, bool setEngineStuff, Object *parentObject)
{
  // calculate matrices, store in the cache
  if (StoreDrawMatricesNeeded(setEngineStuff, level, parentObject))
    StoreDrawMatrices(shape, level, parentObject);

  // use the matrices from the cache
  UseDrawMatrices(cb, setEngineStuff, shape, level, parentObject);
}
*/

#if _ENABLE_SKINNEDINSTANCING
void Object::PrepareShapeDrawInstanced(LODShape *shape, int level, bool setEngineStuff, const ObjectInstanceInfo *instances, int instanceCount)
{
  if (!setEngineStuff)
  {
    GEngine->SetSkinningType(cb,STNone);
  }
  else
  {
    for (int i = 0; i < instanceCount; i++)
    {
      MATRIX_4_ARRAY(matrix,128);
      DoAssert(instances[i]._parentObject);
      const VisualState &vs = instances[i]._parentObject->RenderVisualStateScope();
      bool ok = instances[i]._parentObject->PrepareShapeDrawMatrices(matrix, vs, shape, level);
      if (ok)
      {
        VerifyMatrixArrayWasSet(matrix, shape, level);
        GEngine->SetSkinningTypeInstance(cb,STPoint4, i, matrix.Data(), matrix.Size());
      }
      else
      {
        Fail("Error: PrepareShapeDrawMatrices in instanced version has not succeeded");
        //GEngine->SetSkinningType(cb,STNone);
      }
    }
  }
}
#endif

//static int landscapeObjects = 0;

Object::Object( LODShape *shape, const CreateObjectId &id, bool allocateVisualState )
:_shape(shape),_static(true),_id(id._visitorId),_objId(id._objId),
_destrType(DestructBuilding),
#if !_BINARIZE_REDUCEOBJECT
#if !_RELEASE
  _animatedCount(0),
#endif
#endif
#if _VBS3_LARGE_OBJECTS && !_BINARIZE_REDUCEOBJECT
  _prevBCPos(VZero),
#endif
#if !_BINARIZE_REDUCEOBJECT
//_hasProxies(false),
#if !_HELISIM_RENDER_TO_TEXTURE
_inList(false),
#endif
_destroyPhase(0),
_canSmoke(true),
#endif
_isDestroyed(false)
,_pPhysics(0)
{
  //++landscapeObjects;
  //LogF("landscapeObjects %i", landscapeObjects);


  if (allocateVisualState)
  {
    _futureVisualState = new ObjectVisualState();  // and stays this way!
  }

#if _HELISIM_RENDER_TO_TEXTURE && !_BINARIZE_REDUCEOBJECT
  for (int i = 0; i < Object::NumDrawObjects; i++)
    _inList[i] = NULL;
#endif
  #if 0 // !_RELEASE
    const char *name="";
    if( _shape && _shape->Name() ) name=_shape->Name();
    Log("New Object id=%d %s",id,name);
  #endif
  // _type default to Primary type
  if( _shape && *_shape->_name ) _type=Primary;
  else _type=Temporary;

  // no destruction for forests
  if( _type!=Primary ) _destrType=DestructNo;
  else if( _shape )
  {
    RStringB damageName=_shape->GetPropertyDammage();
    if( damageName==RSB(no) ) _destrType=DestructNo;
    else if(damageName==RSB(engine) ) _destrType=DestructEngine;
    else if(damageName==RSB(wreck) ) _destrType=DestructWreck;
    else if(damageName==RSB(building) ) _destrType=DestructBuilding;
    else if(damageName==RSB(tree) ) _destrType=DestructTree;
    else if(damageName==RSB(tent) ) _destrType=DestructTent;
#if _VBS2 //add a new value to destruct along Z axis
    else if(damageName==RSB(treez) ) {_destrType=DestructTree; _destructAxis = Vector3(0,0,1);}
#endif
    else
    {
      // use autodetection
      if( GetMass()<10000 ) _destrType=DestructTree;
    }
  }
  // check if there are some proxies
  /*
  if( _shape )
  {
    // scan all LODs
    for( int l=0; l<_shape->NLevels(); l++ )
    {
      Shape *shape=_shape->Level(l);
      if( shape->_proxy.Size()>0 ) _hasProxies=true;
    }
  }
  */
}

Object::~Object()
{
#if _VBS3_LARGE_OBJECTS && !_BINARIZE_REDUCEOBJECT
  RemoveFromRTree();
#endif
#if _BINARIZE_REDUCEOBJECT
  //Fail("Waring: Object functionality reduced"); // We want to allow creating of object
#else
  // check if we have some shadow cached
  if (_shadow)
  {
    // remove all shadows from shadow cache
    GScene->GetShadowCache().ShadowChanged(this);
  }
#endif

#if _ENABLE_PHYSICS
  //SAFE_DELETE(_pPhysics);
  RemovePhysics();

  //--landscapeObjects;
  //LogF("landscapeObjects %i", landscapeObjects);
#endif
}

#if _ENABLE_PHYSICS
void Object::ItemDropPhysicaly(Vector3Par dir, float force)
{
  if(GetPhysics())
  {
    GetPhysics()->EnableCCD(true);
            
		GetPhysics()->ApplyImpulse(dir * force);
    ((enf::BulletController*)GetPhysics())->GetCollisionObject()->setLifeTime(ENF_TEMPPHYS_LIFETIME);
  }
}
#endif

#if _VBS3_LARGE_OBJECTS
void Object::CalcBRect(float& minX, float& minZ, float& maxX, float& maxZ, bool prevPos)
{
  DoAssert(_shape);

#if !_BINARIZE_REDUCEOBJECT
  Vector3 bCenter = BCenterPosition();
#else
  Vector3 bCenter = prevPos ? _prevBCPos : BCenterPosition();
#endif

  float radius = _shape->BoundingSphere();
  minX = bCenter.X()-radius;
  minZ = bCenter.Z()-radius;
  maxX = bCenter.X()+radius;
  maxZ = bCenter.Z()+radius;
}

#if !_BINARIZE_REDUCEOBJECT

bool FindStaticObject(int objIx, void *vobj)
{
  Object *obj = static_cast<Object *>(vobj);
  if (GLandscape->_largeStaticObjectInfos[objIx].id==obj->ID())
  {
    //LogF(" ... %d == %d on %d", GLandscape->_largeStaticObjectInfos[objIx].id.Encode(), obj->ID().Encode(), objIx);
    return false; //found
  }
  return true;
}

void Object::AddToRTree()
{
  DoAssert(GLandscape);
#if _ENABLE_CHEATS
  static bool diagnose = false;
#endif
  if ( GLandscape->IsObjectLarge(this) ) 
  {
    float min[2]; float max[2];
    CalcBRect(min[0], min[1], max[0], max[1], false);
    if ( _type!=Primary && _type!=Network )
      GLandscape->LargeDynamicObjects().Insert(min, max, this);
    else
    {
      // test duplicity first, do not add the same object twice
      if ( !GLandscape->LargeStaticObjects().Search(min, max, [this](int objIx)->bool{
        if (GLandscape->_largeStaticObjectInfos[objIx].id==ID())
        {
          //LogF(" ... %d == %d on %d", GLandscape->_largeStaticObjectInfos[objIx].id.Encode(), obj->ID().Encode(), objIx);
          return false; //found
        }
        return true;
        
      }) )
      {
        int objIx = GLandscape->_largeStaticObjectInfos.Add(LargeObjectInfo(this));
        GLandscape->LargeStaticObjects().Insert(min, max, objIx);
#if _ENABLE_CHEATS
        if (diagnose)
        {
          int id = ID().Encode();
          static int interestedVal = 4191; //space_needle
          if (id == interestedVal)
          {
            LogF("Adding Large Object %d - %d", id, GLandscape->IsObjectLarge(this));
          }
          LogF(" %d: Adding LARGE: (%d) - %s", objIx, id, cc_cast(GetDebugName()));

        }
#endif
      }
#if _ENABLE_CHEATS
      else if (diagnose)
      {
        LogF("SKIPPED Adding LARGE: (%d) - %s", ID().Encode(), cc_cast(GetDebugName()));
      }
#endif
    }
  }
}

void Object::RemoveFromRTree(bool prevPos)
{
  if (GLandscape && GLandscape->IsObjectLarge(this) && _type!=Primary && _type!=Network && _id>0)
  {
    float min[2]; float max[2];
    CalcBRect(min[0], min[1], max[0], max[1], prevPos);
    if (GLandscape->LargeDynamicObjects().Remove(min, max, this))
    {
      Fail((const char*)Format("Large object %s was not found at its position in RTree.", (const char*)GetDebugName()));
      min[0] = FLT_MIN; min[1] = FLT_MIN;
      max[0] = FLT_MAX; max[1] = FLT_MAX;
      if (GLandscape->LargeDynamicObjects().Remove(min, max, this))
      {
        Fail((const char*)Format("Large object %s was not found in RTree.", (const char*)GetDebugName()));
      }
    }
  }
}

LargeObjectInfo::LargeObjectInfo(Object *obj): object(obj), id(obj->ID())
{
  float invLandGrid = GLandscape->GetInvLandGrid();
  posX = toIntFloor(obj->FutureVisualState().Position().X()*invLandGrid);
  posZ = toIntFloor(obj->FutureVisualState().Position().Z()*invLandGrid);
}
#endif
#endif

void Object::ChangeVisitorId( const VisitorObjId &id )
{
  _id=id;
}


#define DEBUG_OBJECT_IDS 0
void Object::SetID( const VisitorObjId &id )
{
  _id=id;
  DoAssert(!_objId.IsObject());
  _objId = ObjectId(VehicleId,id);
#if DEBUG_OBJECT_IDS
  if (!GWorld->CheckObjectIds())
    Fail("Duplicate ObjectIds Detected!");
#endif
}

void Object::SetID( const CreateObjectId &id )
{
  _id = id._visitorId;
  _objId = id._objId;
#if DEBUG_OBJECT_IDS
  if (!GWorld->CheckObjectIds())
    Fail("Duplicate ObjectIds Detected!");
#endif
}

void Object::SetObjectId( const ObjectId &id )
{
  #if _ENABLE_REPORT
    if (_objId.IsObject())
    {
      DoAssert(_objId==id);
    }
  #endif
  _objId=id;
#if DEBUG_OBJECT_IDS
  if (!GWorld->CheckObjectIds())
    Fail("Duplicate ObjectIds Detected!");
#endif
} //!< Set object ID

void Object::AddCrater(Vector3Par pos, float radius)
{
#if _BINARIZE_REDUCEOBJECT
  Fail("Waring: Object functionality reduced");
#else
  if (GEngine->GetMaxCraters() > 0)
  {
    if (!_damage)
    {
      _damage = new DamageRegions;
    }
    // Create space for one crater
    while (_damage->_craters.Size() >= GEngine->GetMaxCraters()) _damage->_craters.Delete(0);

    // Add crater
    CraterProperties cp;
    cp._pos = pos;
    cp._invRadius2 = 1.0f / (radius * radius);
    cp._time = Glob.time.toInt();
    _damage->_craters.Add(cp);
  }
#endif
}

Object *Object::LoadRef(ParamArchive &ar)
{
  OIDInt id;
  if (ar.Serialize("oid", id, 1) != LSOK) return NULL;
  ObjectId oid;
  oid.Decode(id);
  Assert(!oid.IsObject());
  OLink(Object) obj = GWorld->FindObject(oid);
  return obj;
}

LSError Object::SaveRef(ParamArchive &ar)
{
  Assert(!_objId.IsNull());
  CHECK(_objId.Serialize(ar, "oid", 1))
  return LSOK;
}

bool Object::IgnoreObstacle(Object *obstacle, ObjIntersect type) const
{
  return false;
}

float Object::CollisionSizeEstimate(const LODShape *shape)
{
  const Shape *geom=shape->GeometryLevel();
  if( !geom ) return 0; // no collision possible
  Vector3Val min=geom->Min();
  Vector3Val max=geom->Max();
  return (max-min).SizeXZ()*0.5f;
}

float Object::CollisionSize() const
{
  return CollisionSizeEstimate(_shape)*FutureVisualStateScope(true)->Scale();
}

void Object::SetConstantColor(ColorVal color)
{
  ErrF("SetConstantColor not supported on %s",cc_cast(GetDebugName()));
}

ColorVal Object::GetConstantColor() const
{
  static const Color halfOpaqueWhite(Color(1,1,1,0.5f));
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DETransparent)) return halfOpaqueWhite;
#endif
  return HWhite;
}

float Object::VisibleSize(ObjectVisualState const& vs) const
{
  return GetShape()? GetShape()->GeometrySphere()*vs.Scale() : 0;
}

float Object::VisibleSizeRequired(ObjectVisualState const& vs) const
{
  return VisibleSize(vs);
}


Vector3 Object::VisiblePosition(ObjectVisualState const& vs) const {return vs.PositionModelToWorld(GetShape()->GeometryCenter());}

/*!
* \return Result is world space position to aim to.
*/
Vector3 Object::AimingPosition(ObjectVisualState const& vs, const AmmoType *ammo, Vector3Par dirToAim) const
{
  LODShape *lShape = GetShape();
  if (!lShape)
  {
    LogF("No shape object %s tested for aiming position", (const char *)GetDebugName());
    return vs.Position();
  }
  return vs.PositionModelToWorld(GetShape()->AimingCenter());
}

Vector3 Object::AimingPositionHeadShot(ObjectVisualState const& vs, const AmmoType *ammo, Vector3Par dirToAim) const
{
  return AimingPosition(vs, ammo, dirToAim);
}

/*!
* \return Result is world space position to aim to.
*/
Vector3 Object::CameraPosition() const
{
  return AimingPosition(RenderVisualState());
}

Vector3 Object::EyePosition(ObjectVisualState const& vs) const
{
  return AimingPosition(vs);
}

void Object::AttachWave(AbstractWave *wave, float freq)
{
  // lip-sync or wave source attach
}

bool Object::AttachWaveReady(const char *wave)
{
  return true;
}


float Object::GetSpeaking() const
{
  return 0;
}

float Object::GetCameraInterest() const
{
  return 0;
}

void ObjectActionsMapAction::Load(ParamEntryPar cls)
{
  _name = cls.GetName();

  _displayName = cls >> "displayName";
  _condition = cls >> "condition";
  _statement = cls >> "statement";
  _priority = cls >> "priority";
  _showWindow = cls >> "showWindow";
  _hideOnUse = cls >> "hideOnUse";

  ParamEntryPtr list = cls.FindEntry("memoryPoints");
  if (list && list->IsArray())
  {
    int n = list->GetSize();
    _memoryPoints.Resize(n);
    for (int i=0; i<n; i++) _memoryPoints[i] = (*list)[i];
  }
};

void ObjectActionsMap::Load(ParamEntryPar cls)
{
  for (int i=0; i<cls.GetEntryCount(); i++)
  {
    ParamEntryVal entry = cls.GetEntry(i);
    if (entry.IsClass())
    {
      RString model = entry >> "model";
      model.Lower();
      if (!presentKey(model)) put(model, new AutoArray<ObjectActionsMapAction>());
      ObjectActionsMapItem *item = get(model);
      for (int j=0; j<entry.GetEntryCount(); j++)
      {
        ParamEntryVal subentry = entry.GetEntry(j);
        if (subentry.IsClass())
        {
          ObjectActionsMapAction &action = (*item)->Append();
          action.Load(subentry);
        }
      }
    }
  }
}

ObjectActionsMapItem ExplicitMapTraits<RString, ObjectActionsMapItem>::null = nullptr;
RString ExplicitMapTraits<RString, ObjectActionsMapItem>::keyNull("::NULL::"); // regular values are valid paths
RString ExplicitMapTraits<RString, ObjectActionsMapItem>::zombie("::ZOMBIE::");

ObjectActionsMap ObjectActions;
ObjectActionsMap SurfaceActions;

void Object::GetActions(const std::function<void(const UIAction &action)> &add, AIBrain *unit)
{
  // no attached config, check the global map of models
  if (!_shape) return;
  RString model = _shape->GetName();
  model.Lower();

  ObjectActionsMapItem *item = ObjectActions.get(model);
  if (item)
  {
    GameState *state = GWorld->GetGameState();
    GameVarSpace local(state->GetContext(), false);
    int gshandle = state->BeginContext(&local);

    Person *person = unit->GetPerson();

    state->VarSetLocal("this", GameValueExt(this), true, true);
    state->VarSetLocal("_name", "ObjectAction", true, true);
    state->VarSetLocal("_owner", GameValueExt(person), true, true);
    InventoryItem *inHands = person ? person->GetItemInHands() : NULL;
    state->VarSetLocal("_inHands", GameValueExt(inHands), true, true);

    for (int i=0; i<(*item)->Size(); i++)
    {
      ObjectActionsMapAction &src = (**item)[i];
      if (!src._condition.IsEmpty() && !state->EvaluateBool(src._condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) continue; // mission namespace

      UIAction action(new ActionIndex(ATObjectAction, this, i));
      action.priority = src._priority;
      action.showWindow = src._showWindow;
      action.hideOnUse = src._hideOnUse;

      int n = src._memoryPoints.Size();
      if (n > 0)
      {
        const Shape *mem = _shape->MemoryLevel();
        if (mem)
        {
          for (int i=0; i<n; i++)
          {
            int pos = _shape->PointIndex(mem, src._memoryPoints[i]);
            if (pos >= 0) action._memoryPoints.Add(pos);
          }
          action._memoryPoints.Compact();
        }
      }

      add(action);
    }

    state->EndContext(gshandle);
  }
}

bool Object::GetActionParams(UIActionParams &params, const Action *action, AIBrain *unit) const
{
  if (action->GetType() == ATObjectAction)
  {
    if (!unit) return false;
    if (!_shape) return false;
    RString model = _shape->GetName();
    model.Lower();

    ObjectActionsMapItem *item = ObjectActions.get(model);
    if (item)
    {
      const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
      int index = actionTyped->GetIndex();
      if (index < (*item)->Size())
      {
        ObjectActionsMapAction &act = (**item)[index];
        params.Add(CreateTextStructured(NULL, act._displayName));
        params.Add(CreateTextStructured(NULL, act._displayName));
        return true;
      }
    }
    return false;
  }

  RptF("Unknown action %s", cc_cast(FindEnumName(action->GetType())));
  return false;
}

void Object::PerformAction(const Action *action, AIBrain *unit)
{
  if (action->GetType() == ATObjectAction)
  {
    if (!unit) return;
    if (!_shape) return;
    RString model = _shape->GetName();
    model.Lower();

    ObjectActionsMapItem *item = ObjectActions.get(model);
    if (item)
    {
      const ActionIndex *actionTyped = static_cast<const ActionIndex *>(action);
      int index = actionTyped->GetIndex();
      if (index < (*item)->Size())
      {
        ObjectActionsMapAction &act = (**item)[index];

        GameState *state = GWorld->GetGameState();
        GameVarSpace local(state->GetContext(), false);
        int gshandle = state->BeginContext(&local);

        Person *person = unit->GetPerson();

        state->VarSetLocal("this", GameValueExt(this), true, true);
        state->VarSetLocal("_name", act._name, true, true);
        state->VarSetLocal("_owner", GameValueExt(person), true, true);
        InventoryItem *inHands = person ? person->GetItemInHands() : NULL;
        state->VarSetLocal("_inHands", GameValueExt(inHands), true, true);
        state->Execute(act._statement, GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace

        state->EndContext(gshandle);
      }
    }
    return;
  }

  // note: action is performed by target of the action
  // unit is who activated the action
  ErrF
  (
    "%s (%s): Unknown action %s, target %s",
    (const char *)GetDebugName(),
    (const char *)GetDisplayName(),
    (const char *)FindEnumName(action->GetType()),
    (const char *)action->GetTarget()->GetDebugName()
  );
}

bool Object::IsPassable() const
{
  // soldier and other vehicles may pass through
  return GetMass()<10;
}

#if _ENABLE_ATTACHED_OBJECTS
bool Object::AttachTo(Object *obj, Vector3Par modelPos, int memIndex, int flags)
{
  // original VBS2 implementation was in Object - this made Object big 
  // alert on any unsupported use attempt
  if (obj)
  {
    RptF("%s: Cannot attach to object %s - entity needed",cc_cast(obj->GetDebugName()),cc_cast(GetDebugName()));
  }
  return false;
}
#endif

const float SCALE_NUM_BARIER = 0.001f;
#ifdef _DEBUG

void Frame::AssertOrientation(const char * func, const char * objectName) const
{
  Vector3 sqSize;
  if (!Orientation().IsOrthogonal(&sqSize))  
  {
    if (objectName)
    {
      Vector3 sizeOffDiagonal(Direction() * DirectionUp(), DirectionAside() * DirectionUp(), Direction() * DirectionAside());
      RptF("%s : Object %s, nonorthogonal matrix. (%lf, %lf, %lf)", func, objectName,sizeOffDiagonal[0],sizeOffDiagonal[1],sizeOffDiagonal[2]);
      Assert(false);
      
    }
    else
    {
      Vector3 sizeOffDiagonal(Direction() * DirectionUp(), DirectionAside() * DirectionUp(), Direction() * DirectionAside());
      RptF("%s : Frame unknown, nonorthogonal matrix, (%lf, %lf, %lf)", func, sizeOffDiagonal[0], sizeOffDiagonal[1], sizeOffDiagonal[2]);
      Assert(false);      
    }
  }
  else
  {
    // orthogonal so check scale.... 
    if (fabs((sqSize[0] - sqSize[1])/sqSize[0]) > SCALE_NUM_BARIER || fabs((sqSize[0] - sqSize[2])/sqSize[0]) > SCALE_NUM_BARIER)
    {     
      if (objectName)
      {
        RptF("%s : Object %s, scaled matrix. (%lf, %lf, %lf)",func, objectName, sqSize[0], sqSize[1], sqSize[2]);
        Assert(false);
      }
      else
      {
        RptF("%s : Frame unknown, scaled matrix. (%lf, %lf, %lf)",func,  sqSize[0], sqSize[1], sqSize[2]);
        Assert(false);
      }
    }
  }
}

#endif


FrameBase::FrameBase()
:_scale(1), _maxScale(-1)
{
}

void FrameBase::SetPosition( Vector3Par pos )
{
  base::SetPosition(pos);
}

void FrameBase::SetTransform( const Matrix4 &transform )
{
  base::SetTransform(transform);
  if (_maxScale < 0)
    _scale=base::Direction().Size();
  else
    base::ScaleAndMaxScale(_scale, _maxScale);

//  ASSERT_ORIENTATION("FrameBase::SetTransform", NULL, _maxScale < 0);   // If you need to print the name, good luck: start at `FrameBase::SetOrient` and `Object::SetOrient`
}

void FrameBase::SetTransformSkewed( const Matrix4 &transform )
{
  base::SetTransform(transform); 
  base::ScaleAndMaxScale(_scale, _maxScale);    // from now FrameBase is considered as skewed
}

void FrameBase::SetTransformPossiblySkewed(const Matrix4 &transform )
{
  base::SetTransform(transform);  
  if (_maxScale >= 0)
    base::ScaleAndMaxScale(_scale, _maxScale);
  else
  { 
    // is matrix scaled
    Vector3 sq;
    if (!Orientation().IsOrthogonal(&sq) || fabs(sq[0] - sq[1]) > SCALE_NUM_BARIER || fabs(sq[0] - sq[2]) > SCALE_NUM_BARIER)
      base::ScaleAndMaxScale(_scale, _maxScale);
    else
      _scale=base::Direction().Size(); 
  }
}

void FrameBase::SetOrient( const Matrix3 &dir, const char* name )
{
  base::SetOrientation(dir);
  if (_maxScale < 0)
    _scale=base::Direction().Size();
  else
    base::ScaleAndMaxScale(_scale, _maxScale);  

  ASSERT_ORIENTATION("FrameBase::SetOrient", name, _maxScale < 0);    
}

void FrameBase::SetOrientSkewed( const Matrix3 &dir )
{
  base::SetOrientation(dir);
  base::ScaleAndMaxScale(_scale, _maxScale);  // from now FrameBase is considered as skewed
}

void FrameBase::SetOrient( Vector3Par dir, Vector3Par up )
{
  base::SetDirectionAndUp(dir,up);
  _scale = 1;
  if (_maxScale >= 0)
    _maxScale = 1;
}
void FrameBase::SetOrientScaleOnly( float scale )
{
  SetOrientationScaleOnly(scale);
  _scale=scale;
  if (_maxScale >= 0)
    _maxScale = _scale;
}

Matrix4 FrameBase::GetInvTransform() const
{
  if (_maxScale < 0)
    return base::InverseScaled();
  else
    return base::InverseGeneral();
}

static inline Vector3 MinMaxCorner(const Vector3 *minMax, int x, int y, int z)
{
  return Vector3(minMax[x][0],minMax[y][1],minMax[z][2]);
}

// check if two shapes are in collision state


void DiagHalfSpace(Vector3Val from, Vector3Val to,const Frame &pos, Color color)
{
  Vector3 wFrom = pos.PositionModelToWorld(from);
  Vector3 wTo = pos.PositionModelToWorld(to);
  Vector3 arrowDir = wTo - wFrom;
  LODShape *forceArrow=GScene->Preloaded(HalfSpaceModel);
  float size=arrowDir.Size() * 0.5f; // halfspace model is 2x2x1m sized
  arrowDir.Normalize();
  
  Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
  Vector3 aside=arrowDir.CrossProduct(VAside);
  Vector3 zAside=arrowDir.CrossProduct(VForward);
  if( zAside.SquareSize()>aside.SquareSize() ) aside=zAside;
  arrow->SetPosition(wFrom);
  arrow->SetOrient(arrowDir,aside);
  arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
  arrow->SetScale(size);

  arrow->SetConstantColor(PackedColor(color));  
  GScene->ShowObject(arrow);
}

void DiagArrow(Vector3Val from, Vector3Val to,const Frame &pos, Color color)
{
  Vector3 wFrom = pos.PositionModelToWorld(from);
  Vector3 wTo = pos.PositionModelToWorld(to);
  Vector3 arrowDir = wTo - wFrom;
  LODShape *forceArrow=GScene->ForceArrow();
  float size=arrowDir.Size() * 0.1f; // force is in model 10m long
  arrowDir.Normalize();
  
  Ref<Object> arrow=new ObjectColored(forceArrow,VISITOR_NO_ID);
  Vector3 aside=arrowDir.CrossProduct(VAside);
  Vector3 zAside=arrowDir.CrossProduct(VForward);
  if( zAside.SquareSize()>aside.SquareSize() ) aside=zAside;
  arrow->SetPosition(wFrom);
  arrow->SetOrient(arrowDir,aside);
  arrow->SetPosition(arrow->FutureVisualState().PositionModelToWorld(forceArrow->BoundingCenter()*size));
  arrow->SetScale(size);

  arrow->SetConstantColor(PackedColor(color));  
  GScene->ShowObject(arrow);
}



static void EnlargeBBox(Vector3 * ptRetMinMax, const Vector3 tMinMax[2], Vector3Val cDeltaPos)
{
    ptRetMinMax[0] = tMinMax[0];
    ptRetMinMax[1] = tMinMax[1];

    for(int i = 0; i < 3; i++)
    {
        ptRetMinMax[cDeltaPos[i] < 0 ? 0 : 1][i] += cDeltaPos[i];
    }
}

bool IntersectBBox
(
  Vector3 *isect,
  const Vector3 tMinMax[2], const Vector3 wMinMax[2],
  const Matrix4 &withToThis
)
{
  #if 1
  // TODO: use separating axis for faster and more accurate detection
  // http://www.gamasutra.com/view/feature/3383/simple_intersection_tests_for_games.php?print=1
  // calculate bbox of transformed bbox
  Vector3 wtMin(+1e10,+1e10,+1e10),wtMax(-1e10,-1e10,-1e10); // in this coordinates

  Vector3 wCorners[2][2][2];
  for( int lr=0; lr<2; lr++ )
  for( int ud=0; ud<2; ud++ )
  for( int fb=0; fb<2; fb++ )
  {
    Vector3 wCorner=MinMaxCorner(wMinMax,lr,ud,fb);
    Vector3 wtCorner;
    wtCorner.SetFastTransform(withToThis,wCorner);
    wCorners[lr][ud][fb]=wtCorner;
    CheckMinMax(wtMin,wtMax,wtCorner);
  }


  // clip wtMinMax against tMinMax

  if (wtMin[0]>=tMinMax[1][0]) return false;
  if (wtMin[1]>=tMinMax[1][1]) return false;
  if (wtMin[2]>=tMinMax[1][2]) return false;
  if (wtMax[0]<=tMinMax[0][0]) return false;
  if (wtMax[1]<=tMinMax[0][1]) return false;
  if (wtMax[2]<=tMinMax[0][2]) return false;

  if (isect)
  {
    // the caller requires intersection result
    isect[0] = VectorMax(wtMin,tMinMax[0]);
    isect[1] = VectorMin(wtMax,tMinMax[1]);
  }

  #else

  // use planes with normals (1,0,0), (0,1,0) and (0,0,1)
  isect[0] = tMinMax[0];
  isect[1] = tMinMax[1];

  Vector3 wCorners[2][2][2];
  for( int lr=0; lr<2; lr++ )
  for( int ud=0; ud<2; ud++ )
  for( int fb=0; fb<2; fb++ )
  {
    Vector3 wCorner=MinMaxCorner(wMinMax,lr,ud,fb);
    Vector3 wtCorner;
    wtCorner.SetFastTransform(withToThis,wCorner);
    wCorners[lr][ud][fb]=wtCorner;
  }

  for( int c=0; c<3; c++ )
  {
    // boundbox of intersections between two planes and all edges
    // search for maximal point above minimum
    Vector3 cMin(+1e10,+1e10,+1e10);
    Vector3 cMax(-1e10,-1e10,-1e10);
    for( int diff=0; diff<3; diff++ )
    for( int same1=0; same1<2; same1++ )
    for( int same2=0; same2<2; same2++ )
    {
      int x1,y1,z1;
      int x2,y2,z2;
      if( diff==0 )      x1=0,y1=same1,z1=same2,x2=1,y2=same1,z2=same2;
      else if( diff==1 ) x1=same1,y1=0,z1=same2,x2=same1,y2=1,z2=same2;
      else x1=same1,y1=same2,z1=0,x2=same1,y2=same2,z2=1; // diff==2
      // calculate intersection between a,b and all six planes of cThis

      Vector3Val rA=wCorners[x1][y1][z1];
      Vector3Val rB=wCorners[x2][y2][z2];

      // t=(nA-nP)/(nA-nB)
      float aC=rA[c],bC=rB[c];
      // min plane - inX<0 => x is out
      float inMinA=aC-tMinMax[0][c];
      float inMinB=bC-tMinMax[0][c];
      bool isInMinA=( inMinA>0 );
      bool isInMinB=( inMinB>0 );
      // max plane - inX>0 => x is out
      float inMaxA=aC-tMinMax[1][c];
      float inMaxB=bC-tMinMax[1][c];
      bool isInMaxA=( inMaxA<0 );
      bool isInMaxB=( inMaxB<0 );
      if( isInMinA!=isInMinB )
      {
        float t=inMinA/(aC-bC);
        Assert( t>=-1e5 && t<=1+1e5 );
        Point3 iSect=(rB-rA)*t+rA;
        CheckMinMax(cMin,cMax,iSect);
      }
      if( isInMaxA!=isInMaxB )
      {
        float t=inMaxA/(aC-bC);
        Assert( t>=-1e5 && t<=1+1e5 );
        Point3 iSect=(rB-rA)*t+rA;
        CheckMinMax(cMin,cMax,iSect);
      }
      if( isInMinA && isInMaxA ) CheckMinMax(cMin,cMax,rA);
      if( isInMinB && isInMaxB ) CheckMinMax(cMin,cMax,rB);
    }
    // limit min-max box of this to the newly acquired region
    SaturateMax(isect[0],cMin);
    SaturateMin(isect[1],cMax);

    if( isect[0][0]>=isect[1][0] ) return false;
    if( isect[0][1]>=isect[1][1] ) return false;
    if( isect[0][2]>=isect[1][2] ) return false;
  }
  #endif
  return true;
}


//#pragma warning(default:4244;default:4305) // warn if __int64 to int conversion takes place

void LogicalPoly::Copy( const LogicalPoly &src )
{
  // caution: following line is broken, because _v may have some alignment
  //memcpy(this,&src,sizeof(_n)+sizeof(*_v)*src._n);
  // we could use on memcpy with (char *)&_src._v[src._n]-(char *)_src
  // however following seems to be cleaner and more readable
  _n = src._n;
  memcpy(_v,src._v,sizeof(*_v)*src._n);
}

// some operations require that inside test is performed, not pre-calculated
bool LogicalPoly::Clip( LogicalPoly &res, Vector3Par normal, Coord d ) const
{
  // initialize resulting polygon
  res._n=0;
  if( _n<3 ) {return true;}
  
  // search for first vertex inside clipping half-space
  int i;
  const Vector3 *pVertex; // previous vertex
  const Vector3 *aVertex; // actual vertex
  
  pVertex=_v+_n-1;
  bool pOut=normal*(*pVertex)+d<0;
  
  bool change=false;

  for( i=0; i<_n; i++ )
  {
    aVertex=_v+i;
    float aNom = normal*(*aVertex)+d;
    bool aOut = aNom<0;
    // four possible situations
    if( aOut!=pOut )
    {
      Vector3Val in=*aVertex;
      Vector3Val out=*pVertex;
      float denom = normal*(in-out);
      float t = denom!=0 ? aNom / denom : 0;
      saturateMin(t,1.0f);
      saturateMax(t,0.0f);

      res.Add(in+(out-in)*t);
      change=true;
    }
    if( !aOut )
    {
      // point in
      res.Add(*aVertex);
    }
    else change=true;
    pVertex=aVertex;
    pOut=aOut;
  }

  Assert( res._n<=MaxPoly );
  if( res._n<3 ) res._n=0; // polygon completely out
  return change;
}

void LogicalPoly::SumCrossProducts(Vector3 &sum) const
{
  // note: area is half of sum of crossproducts
  Vector3Val ip = _v[0];
  for (int i=2; i<_n; i++)
  {
    Vector3Val lp = _v[i-1];
    Vector3Val rp = _v[i];
    Vector3Val lmi = (lp-ip);
    sum += lmi.CrossProduct(rp-ip);
  }
}

void LogicalPoly::Clip( float cNear, float cFar, ClipFlags clip )
{
  // clip to all clip planes
  LogicalPoly tempResult;
  LogicalPoly *source=this;
  LogicalPoly *result=&tempResult;
  if( clip&ClipFront )
  {
    if( source->Clip(*result,Vector3(0,0,+1),-cNear) )
    {
      Swap(source,result);
    }
  }
  if( clip&ClipBack )
  {
    if( source->Clip(*result,Vector3(0,0,-1),cFar) )
    {
      Swap(source,result);
    }
  }
  
  if( clip&ClipLeft )
  {
    if( source->Clip(*result,Vector3(+1,0,+1),0) )
    {
      Swap(source,result);
    }
  }
  if( clip&ClipRight )
  {
    if( source->Clip(*result,Vector3(-1,0,+1),0) )
    {
      Swap(source,result);
    }
  }
  if( clip&ClipTop )
  {
    if( source->Clip(*result,Vector3(0,+1,+1),0) )
    {
      Swap(source,result);
    }
  }
  if( clip&ClipBottom )
  {
    if( source->Clip(*result,Vector3(0,-1,+1),0) )
    {
      Swap(source,result);
    }
  }

  if( source!=this ) *this=*source;
}

/**
Arguments are world space coordinates.
*/
bool Object::DetectRoadwayOverlap(float xMin, float xMax, float zMin, float zMax) const
{
// scan roadway
  const Shape *roadway = _shape->RoadwayLevel();
  // traverse all polygons
  // note: it might be faster to transform the four clipping planes into the world space
  // however, the function is used off-line and this way it is easier
  // constructed outside of the loop to avoid repeated construction/destruction
  LogicalPoly poly;
  LogicalPoly temp;
  for (Offset o=roadway->BeginFaces(); o<roadway->EndFaces(); roadway->NextFace(o))
  {
    // for each one create a world-space image
    const Poly &face = roadway->Face(o);
    // ignore singular polygons
    if (face.N()<3) continue;
    Vector3 minV(+FLT_MAX,+FLT_MAX,+FLT_MAX),maxV(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    poly.Clear();
    for (int i=0; i<face.N(); i++)
    {
      int curr = face.GetVertex(i);
      Vector3 currPos = FutureVisualState().PositionModelToWorld(roadway->Pos(curr));
      poly.Add(currPos);
      SaturateMax(maxV,currPos);
      SaturateMin(minV,currPos);
    }

    // quick test - check min-max box
    if (floatMax(xMin,minV.X())<floatMin(xMax,maxV.X()) && floatMax(zMin,minV.Z())<floatMin(zMax,maxV.Z()))
    {
#if 1
      // clip the polygon and check if something was left from it
      LogicalPoly *cur = &poly;
      LogicalPoly *tgt = &temp;

      // create four clipping planes
      if (cur->Clip(*tgt,Vector3(+1,0,0),-xMin)) SwapQ(&cur,&tgt);
      if (cur->Clip(*tgt,Vector3(-1,0,0),+xMax)) SwapQ(&cur,&tgt);
      if (cur->Clip(*tgt,Vector3(0,0,+1),-zMin)) SwapQ(&cur,&tgt);
      if (cur->Clip(*tgt,Vector3(0,0,-1),+zMax)) SwapQ(&cur,&tgt);
      // one any polygon has a non-empty intersection, no need to check anything more
      if (cur->N()>=3) return true;
#else
      return true;
#endif

    }

  }
  return false;
}

void DiagFace(const LogicalPoly &poly, Matrix4Par pos, PackedColor color)
{
  Ref<enf::Shape> sh = enf::Shape::CreatePolygon(poly.Data(), poly.N(), color, enf::SS_ONCE|enf::SS_WIREFRAME);
  sh->SetMatrix(pos);
  sh->SetOutlineColor(color);
}

#if _HELISIM_ROTORLIB_SIM_THREAD

Vector3 CollisionBuffer::GetContactPos(int fromIndx, int toIndx) const
{
  // the object seems to be strong enough to hold us
  // calculate average contact point - world space
  Vector3 sum = VZero;
  if (_buffer)
  {
    for (int i=fromIndx; i<toIndx; i++)
    {
      sum += _buffer->Get(i).pos;
    }
  }
  else
  {
    for (int i=fromIndx; i<toIndx; i++)
    {
      sum += _bufferEx->Get(i).pos;
    }
  }
  sum /= toIndx-fromIndx;
  return sum;
}

#else

Vector3 CollisionBuffer::GetContactPos(int fromIndx, int toIndx) const
{
  // the object seems to be strong enough to hold us
  // calculate average contact point - world space
  Vector3 sum = VZero;
  for (int i=fromIndx; i<toIndx; i++)
  {
    sum += Get(i).pos;
  }
  sum /= toIndx-fromIndx;
  return sum;
}

#endif

/*!
\patch 2.01 Date 1/14/2003 by Ondra
- Improved: More accurate result of object intersection calculated.
*/


static bool CalculateIntersectionsExact(
  CollisionBuffer &result, const Object *thisObj,
  const AnimationContext &acThis, const AnimationContext &acWith,
  const Shape *sThis, const Shape *sWith,
  const ConvexComponent &cThis, const ConvexComponent &cWith,
  Matrix4Val thisToWith, Matrix4Val withToThis,
  Matrix4Val thisToWorld,
  int hierLevel
)
{
  // calculate sum of center of volume*volume
  // all calculation done in this space
  // clip all faces of cThis with cWith

  // detect singularity - when zero matrix applied, all normals are singular
  if (cThis.NPlanes() < 1 || cThis.GetPlane(acThis.GetPlanesArray(sThis), 0).Normal().SquareSize() == 0) return false;
  if (cWith.NPlanes() < 1 || cWith.GetPlane(acWith.GetPlanesArray(sWith), 0).Normal().SquareSize() == 0) return false;

  // calculate direction
  Vector3 thisNormal = VZero;
  bool someThisLeft = false;
  LogicalPoly poly;
  LogicalPoly resClip;

  AUTO_STATIC_ARRAY(Vector3, thisVertexResult, 256);
  for (int i = 0; i < cThis.NPlanes(); i++)
  {
    const Poly &face = cThis.GetFace(sThis, i);
    // construct analytical poly
    poly.Clear();
    // face of this in with space
    for (int f=0; f<face.N(); f++)
    {
      Vector3Val pos = acThis.GetPos(sThis, face.GetVertex(f));
      // verify vertex is in plane
      poly.Add(thisToWith.FastTransform(pos));
    }
    // clip it with all planes of with
    for (int w=0; w<cWith.NPlanes(); w++)
    {
      const Plane &wPlane = cWith.GetPlane(acWith.GetPlanesArray(sWith), w);
      resClip.Clear();
      if (poly.Clip(resClip, wPlane.Normal(), wPlane.D()))
      {
        // TODO: more efficient handling with temp1, temp2 and pointer
        poly = resClip;
        if (poly.N() < 3) break;
      }
    }
    // check if something is rest after clipping
    if (poly.N() >= 3)
    {
      // transform poly to this space
      poly.Transform(withToThis);
      // calculate oriented area
      poly.SumCrossProducts(thisNormal);
      // calculate volume and center of volume
      someThisLeft = true;

      for (int v=0; v<poly.N(); v++)
      { 
        // add only unique vertices
        Vector3Val pos = poly.Get(v);
        bool alreadyIn = false;
        for (int i=0; i<thisVertexResult.Size(); i++)
        {
          if (thisVertexResult[i].Distance2(pos) < Square(1e-4))
          {
            alreadyIn = true;
            break;
          }
        }
        if (!alreadyIn) thisVertexResult.Add(poly.Get(v));
      }
    }
  }

  if (!someThisLeft)
  {
    // nothing left from this
    // no intersection
    return false;
  }
    //FIX +: 18.2.2003 Fehy: If thisNormal is numerical zero, direction is wrong number.
    float fThisNormalSize = thisNormal.Size();
    if (fThisNormalSize < 0.0001f) 
    {
        return false;
    }

  Vector3 direction = thisNormal / fThisNormalSize; // FIXME: unneeded, remove completely?
  // we may calculate max. d of this
  float maxDThis = -1e10;
  float minDWith = +1e10;
  for (int i=0; i<thisVertexResult.Size(); i++)
  {
    float d = direction * thisVertexResult[i];
    saturateMax(maxDThis, d);
  }

  bool someWithLeft = false;
  // clip all faces of cWith with cThis
  // scan all points of cWith

  #if 0
  LogF
  (
    "ThisPlane %.2f,%.2f,%.2f:%.2f",
    direction[0],direction[1],direction[2],maxDThis
  );
  #endif

  // accurate solution - return points of the clipped result
  Vector3 withCenter = VZero, withNormal = VZero;
  //LogicalPoly poly;
  //LogicalPoly resClip;
  for (int i=0; i<cWith.NPlanes(); i++)
  {
    const Poly &face = cWith.GetFace(sWith, i);
    // construct analytical poly
    poly.Clear();
    // face of this in with space
    for (int f=0; f<face.N(); f++)
    {
      Vector3Val pos = acWith.GetPos(sWith, face.GetVertex(f));
      poly.Add(withToThis.FastTransform(pos));
    }
    // clip it with all planes of with
    for (int w=0; w<cThis.NPlanes(); w++)
    {
      const Plane &wPlane = cThis.GetPlane(acThis.GetPlanesArray(sThis), w);
      resClip.Clear();
      if (poly.Clip(resClip, wPlane.Normal(), wPlane.D()))
      {
        // TODO: more efficient handling with temp1, temp2 and pointer
        poly = resClip;
        if (poly.N() < 3) break;
      }
    }
    // check if something is rest after clipping
    if (poly.N()>=3)
    {
      // calculate volume and center of volume
      someWithLeft = true;

      for (int v=0; v<poly.N(); v++)
      {
        Vector3Val pos = poly.Get(v);
        /*
        LogF("w %d: %.2f,%.2f,%.2f",v,pos[0],pos[1],pos[2]);
        float d = direction*pos;
        saturateMin(minDWith,d);
        */

        float under = maxDThis - pos*direction;

        if (under > 0.001)
        {
          //LogF("  %d: w under %.2f (pos %.3f,%.3f,%.3f,)",i,under,pos[0],pos[1],pos[2]);
          // add only unique vertices
          bool alreadyIn = false;
          for (int i=0; i<result.Size(); i++)
          {
            if (result[i].pos.Distance2(pos)<Square(1e-4)) alreadyIn = true;
          }

          if (!alreadyIn)
          {
            CollisionInfo &ret=result.Append();
            // calculate average of bounding boxes in this space
            ret.texture = NULL;
            ret.surface = NULL;
            ret.pos = pos;
            ret.dirOut = direction;
            ret.dirOutNotNorm = thisNormal;
            ret.hierLevel = hierLevel;
            ret.under = under;
            ret.component = -1;
            ret.geomLevel = -1;
            ret.entry = ret.exit = false;
            ret.object = unconst_cast(thisObj);
            #if STARS
              GLOB_SCENE->DrawCollisionStar(thisToWorld.FastTransform(ret.pos));
            #endif
          }
        }
      }
    }
  }
  //LogF("d %.3f..%.3f",maxDThis,minDWith);

  float under; // unneeded, remove completely?
  if (!someWithLeft)
  {
    // this is fully contained in with
    // special case
    // actual volume calculation would be exact here
    // we provide "large" values instead
    direction = VUp;
    under = 0.5f;
    return true; // special handling required for this situation
  }
  else
    under = maxDThis - minDWith;
  return true;
}

static bool CalculateIntersectionsExactEx(
  CollisionBuffer &result, const Object *thisObj,
  const AnimationContext &acThis, const AnimationContext &acWith,
  const Shape *sThis, const Shape *sWith,
  const ConvexComponent &cThis, const ConvexComponent &cWith,
  Matrix4Val thisToWith, Matrix4Val withToThis,
  Matrix4Val thisToWorld,
  int hierLevel
)
{
  Vector3 direction;
  // all calculation done in this space
  // clip all faces of cThis with cWith

#if 1
  AUTO_STATIC_ARRAY(Vector3,thisVertexResult,256);
#endif
#if 0 // it is too slow in debug, use it only it it is really needed
  //Assert(unconst_cast(sThis)->VerifyNormals()); it is 
  //Assert(unconst_cast(sWith)->VerifyNormals());
#endif
  #if _ENABLE_CHEATS
    if( CHECK_DIAG(DECollision) )
    {
      // draw all half-spaces
      // use some random midpoint
      Vector3 cWithCenter = (cWith.Min()+cWith.Max())*0.5f;

      LODShape *shape = GScene->Preloaded(SphereModel);

      Ref<Object> obj=new ObjectColored(shape,VISITOR_NO_ID);
      obj->SetPosition(thisToWorld*withToThis*cWithCenter);
      obj->SetScale( 0.05 );
      obj->SetConstantColor(PackedColor(Color(0,0,1,1)));
      GScene->ShowObject(obj);

      for (int w=0; w<cWith.NPlanes(); w++)
      {
        const Plane &wPlane = cWith.GetPlane(acWith.GetPlanesArray(sWith), w);
        // find a point on wPlane closest to cWithCenter
        float dist = wPlane.Distance(cWithCenter);
        Vector3 planePoint = cWithCenter-dist*wPlane.Normal();

        DiagHalfSpace(
          planePoint,planePoint-wPlane.Normal()*0.5f,thisToWorld*withToThis,
          Color(0,1,1,1)
        );
      }
      
      
    }
  #endif

  // detect singularity - when zero matrix applied, all normals are singular
  if (cThis.NPlanes() < 1 || cThis.GetPlane(acThis.GetPlanesArray(sThis), 0).Normal().SquareSize() == 0) return false;
  if (cWith.NPlanes() < 1 || cWith.GetPlane(acWith.GetPlanesArray(sWith), 0).Normal().SquareSize() == 0) return false;

  // calculate direction
  AUTO_STATIC_ARRAY(Vector3,cThisNormals,256);
  Vector3 thisNormal = VZero;
  bool someThisLeft = false;
  LogicalPoly poly;
  LogicalPoly resClip;
  for (int i=0; i<cThis.NPlanes(); i++)
  {
    const Poly &face = cThis.GetFace(sThis,i);
    // construct analytical poly
    poly.Clear();
    // face of this in with space
    for (int f=0; f<face.N(); f++)
    {
      Vector3Val pos = acThis.GetPos(sThis, face.GetVertex(f));
      poly.Add(thisToWith.FastTransform(pos));
      #if _ENABLE_REPORT
        // verify vertex is in plane
        const Plane &tPlane = cThis.GetPlane(acThis.GetPlanesArray(sThis), i);
        float dist = tPlane.Distance(pos);
        (void)dist;
        Assert(fabs(dist)<0.20f);
      #endif
    }
    
    #if _ENABLE_CHEATS
      if( CHECK_DIAG(DECollision) )
      {
        DiagFace(poly,thisToWorld*withToThis,PackedColor(Color(1,1,0,1)));
      }
    #endif
    
    // clip it with all planes of with
    for (int w=0; w<cWith.NPlanes(); w++)
    {
      const Plane &wPlane = cWith.GetPlane(acWith.GetPlanesArray(sWith), w);
      #if _ENABLE_REPORT
        const Poly &wFace = cWith.GetFace(sWith,w);
        for (int wf=0; wf<wFace.N(); wf++)
        {
          Vector3Val wPos = acWith.GetPos(sWith, wFace.GetVertex(wf));
          // verify vertex is in plane
          float dist = wPlane.Distance(wPos);
          (void)dist;
          Assert(fabs(dist)<0.20f);
        }
      #endif
      resClip.Clear();
      if (poly.Clip(resClip, wPlane.Normal(), wPlane.D()))
      {
        // TODO: more efficient handling with temp1, temp2 and pointer
        poly = resClip;
        if (poly.N() < 3) break;
      }
    }
    // check if something is rest after clipping
    if (poly.N() >= 3)
    {
      // transform poly to this space
      poly.Transform(withToThis);
      // calculate oriented area
      Vector3 &cNormal = cThisNormals.Append();
      cNormal = VZero;

      poly.SumCrossProducts(cNormal); // TODO: not very effective way
      thisNormal += cNormal;
      cNormal.Normalize();

      someThisLeft = true;

      for (int v=0; v<poly.N(); v++)
      { 
        // add only unique vertices
        Vector3Val pos = poly.Get(v);
        bool alreadyIn = false;
        for (int i=0; i<thisVertexResult.Size(); i++)
        {
          if (thisVertexResult[i].Distance2(pos)<Square(1e-4))
          {
            alreadyIn = true;
            break;
          }
        }
        if (!alreadyIn) thisVertexResult.Add(poly.Get(v));
      }

    }
  }

  if (cThisNormals.Size() == 0)
  {
    // nothing left from this
    // no intersection
    return false;
  }

  // Filter normals
  AutoArray<Vector3> cThisNormals1(cThisNormals.Size());
  AutoArray<Vector3> cThisNormals2(cThisNormals.Size());

  for(int i = 0; i < cThisNormals.Size(); i++)
  {        
    Vector3& cNormal = cThisNormals[i];
    int j = 0;
    for(; j < cThisNormals.Size(); j++)
    {
      if (cNormal * cThisNormals[j] < -0.2f)  // numerical barier
      {
        cThisNormals2.Add(cNormal);
        break;
      }
    }

    if (j == cThisNormals.Size())
      cThisNormals1.Add(cNormal);                    
  }

  float fThisNormalSize = thisNormal.Size();
  if (fThisNormalSize > 0.0001f && cThisNormals.Size() > 1 ) 
  {
    thisNormal /= fThisNormalSize;
    int j = 0;
    for(; j < cThisNormals.Size(); j++)
    {
      if (thisNormal * cThisNormals[0.2f] < -j)  // numerical barier
      {
        cThisNormals2.Add(thisNormal);
        break;
      }
    }

    if (j == cThisNormals.Size())
      cThisNormals1.Add(thisNormal);         
  }

  if (cThisNormals1.Size() > 0)
  {
    AUTO_STATIC_ARRAY(Vector3,withVertexResult,256);

    // accurate solution - return points of the clipped result
    //Vector3 withCenter = VZero, withNormal = VZero;
    for (int i=0; i<cWith.NPlanes(); i++)
    {
      const Poly &face = cWith.GetFace(sWith,i);
      // construct analytical poly
      poly.Clear();
      // face of this in with space
      for (int f=0; f<face.N(); f++)
      {
        Vector3Val pos = acWith.GetPos(sWith, face.GetVertex(f));
        poly.Add(withToThis.FastTransform(pos));
      }
      // clip it with all planes of with
      for (int w=0; w<cThis.NPlanes(); w++)
      {
        const Plane &wPlane = cThis.GetPlane(acThis.GetPlanesArray(sThis), w);
        resClip.Clear();
        if (poly.Clip(resClip,wPlane.Normal(),wPlane.D()))
        {
          // TODO: more efficient handling with temp1, temp2 and pointer
          poly = resClip;
          if (poly.N()<3) break;
        }
      }
      // check if something is rest after clipping
      if (poly.N() >= 3)
      {
        for (int v=0; v<poly.N(); v++)
        { 
          // add only unique vertices
          Vector3Val pos = poly.Get(v);
          bool alreadyIn = false;
          for (int i=0; i<withVertexResult.Size(); i++)
          {
            if (withVertexResult[i].Distance2(pos)<Square(1e-4))
            {
              alreadyIn = true;
              break;
            }
          }
          if (!alreadyIn) withVertexResult.Add(poly.Get(v));
        }            
      }
    }

    if (withVertexResult.Size() == 0)
    {
      // surprising fact is it OK?
      return false;
    }
    // Do through directions
    for(int i = 0; i < cThisNormals1.Size(); i++)
    {
      float fMaxDir = -100000.0f; 
      Vector3Val cNormal = cThisNormals1[i];
      for(int v = 0; v < thisVertexResult.Size(); v++)
      {
        saturateMax(fMaxDir,thisVertexResult[v]*cNormal);
      }

      float fMinDir = fMaxDir;
      int minVertex = 0;
      for(int v = 0; v < withVertexResult.Size(); v++)
      {
        float fresult = withVertexResult[v] * cNormal;
        if (fMinDir >= fresult)            
        {
          fMinDir = fresult;
          minVertex = v;
        }            
      }

      // Add to result
      CollisionInfo &ret=result.Append();
      // calculate average of bounding boxes in this space
      ret.texture = NULL;
      ret.surface = NULL;
      ret.pos = withVertexResult[minVertex];
      ret.dirOut = cNormal;
      ret.hierLevel = hierLevel;
      ret.under = fMaxDir - fMinDir;
      ret.component = -1;
      ret.geomLevel = -1;
      ret.entry = ret.exit = false;
      ret.object = unconst_cast(thisObj);        
    }
  }

  if (cThisNormals2.Size() > 0)
  {
    AUTO_STATIC_ARRAY(Vector3,withVertexResult,256);

    // accurate solution - return points of the clipped result
    for (int i=0; i<cWith.Size(); i++)
    {
      withVertexResult.Add(withToThis.FastTransform(acWith.GetPos(sWith, cWith[i])));
    }

    // Do through directions
    for (int i=0; i<cThisNormals2.Size(); i++)
    {
      Vector3Val cNormal = cThisNormals2[i];

      float fMaxDir = -100000.0f; 
      int maxVertex = 0;
      for (int v = 0; v < thisVertexResult.Size(); v++)
      {
        float fresult = thisVertexResult[v] * cNormal;
        if (fMaxDir <= fresult)            
        {
          fMaxDir = fresult;
          maxVertex = v;
        }
      }

      float fMinDir = fMaxDir;

      for (int v = 0; v < withVertexResult.Size(); v++)
      {
        saturateMin(fMinDir,withVertexResult[v]*cNormal);                        
      }

      // Add to result
      CollisionInfo &ret=result.Append();
      // calculate average of bounding boxes in this space
      ret.texture = NULL;
      ret.surface = NULL;
      ret.pos = thisVertexResult[maxVertex];
      ret.dirOut = cNormal;
      ret.hierLevel = hierLevel;
      ret.under = fMaxDir - fMinDir;
      ret.component = -1;
      ret.geomLevel = -1;
      ret.entry = ret.exit = false;
      ret.object = unconst_cast(thisObj);        
    }
  }    
  return true;
}

static int CalculateFirstTouchExact
(
  float &fDoneFrac, Vector3& cNormal, /*const Object *thisObj,*/
  const AnimationContext &acThis, const AnimationContext &acWith,
  const Shape *sThis, const Shape *sWith,
  const ConvexComponent &cThis, const ConvexComponent &cWith,
  Matrix4Val thisToWith, Matrix4Val withToThis,
  Vector3Val cDeltaPosWith 
)
{
  cNormal = VZero;
  Vector3 direction;
  // calculate sum of center of volume*volume
  //float volume = 0;
  // all calculation done in this space
  // clip all faces of cThis with cWith

#if 1
  AUTO_STATIC_ARRAY(Vector3,thisVertexResult,256);
#endif

  // calculate direction
  //AutoArray<Vector3> cThisNormals;
  //Vector3 thisNormal = VZero;
  //bool someThisLeft = false;
  LogicalPoly poly;
  LogicalPoly resClip;
  LogicalPoly muster;

  //Vector3 cDeltaPosWithNormal = cDeltaPosWith;
  //cDeltaPosWithNormal.Normalize();

  AutoArray<Plane> cWithPlanes(128);

  AutoArray<int> cMinWithFaces(cWith.NPlanes());
  AutoArray<int> cMinThisFaces(cThis.NPlanes());

  Vector3 cMinPos;

  int iFirstTouchPointPlane = -1;

  for (int i=0; i<cWith.NPlanes(); i++)
  {
    const Poly &faceWith = cWith.GetFace(sWith,i);
    const Plane &wPlaneWith = cWith.GetPlane(acWith.GetPlanesArray(sWith), i);

#ifdef _DEBUG
    Vector3 posBeginT = acWith.GetPos(sWith, faceWith.GetVertex(faceWith.N() - 1));
    Vector3 posEnd1 = acWith.GetPos(sWith, faceWith.GetVertex(0));
    Vector3 posEnd2 = acWith.GetPos(sWith, faceWith.GetVertex(1));

    Vector3 cPlaneNormal = -(posEnd1 - posBeginT).CrossProduct(posEnd2 - posBeginT);
    cPlaneNormal.Normalize();

    Assert(cPlaneNormal.Distance2(wPlaneWith.Normal()) < 0.01);
    Assert(fabsf((-cPlaneNormal * posBeginT) - wPlaneWith.D()) < 0.1);
#endif

    if (wPlaneWith.Normal() * cDeltaPosWith >= 0)
      continue; 

    // Create Planes
    cWithPlanes.Clear();

    Vector3 posBegin = acWith.GetPos(sWith, faceWith.GetVertex(faceWith.N() - 1));
    for(int j = 0; j < faceWith.N(); j ++)
    {
      Vector3Val posEnd = acWith.GetPos(sWith, faceWith.GetVertex(j));            

      Vector3 cPlaneNormal = (posBegin - posEnd).CrossProduct(cDeltaPosWith);
      cPlaneNormal.Normalize();

      Plane &cPlane = cWithPlanes.Append();           
      cPlane.SetNormal(cPlaneNormal, -cPlaneNormal * posEnd);                        

      posBegin = posEnd;
    }

    // Clip face
    for (int w=0; w<cThis.NPlanes(); w++)
    {
      const Poly &faceThis = cThis.GetFace(sThis,w);
      const Plane &wPlaneThis = cThis.GetPlane(acThis.GetPlanesArray(sThis),w);

#ifdef _DEBUG
      Vector3 posBeginT = acThis.GetPos(sThis, faceThis.GetVertex(faceThis.N() - 1));
      Vector3 posEnd1 = acThis.GetPos(sThis, faceThis.GetVertex(0));
      Vector3 posEnd2 = acThis.GetPos(sThis, faceThis.GetVertex(1));

      Vector3 cPlaneNormal = -(posEnd1 - posBeginT).CrossProduct(posEnd2 - posBeginT);
      cPlaneNormal.Normalize();

      Assert(cPlaneNormal.Distance2(wPlaneThis.Normal()) < 0.01);
      Assert(fabsf((-cPlaneNormal * posBeginT) - wPlaneThis.D()) < 0.1);
#endif

      Vector3 cPlaneNormalThisInWith = thisToWith.Rotate(wPlaneThis.Normal());

      if (cPlaneNormalThisInWith * cDeltaPosWith <= 0)
        continue;

      poly.Clear();
      // face of this in with space
      for (int f=0; f<faceThis.N(); f++)
      {
        Vector3Val pos = acThis.GetPos(sThis, faceThis.GetVertex(f));
        // verify vertex is in plane
        poly.Add(thisToWith.FastTransform(pos));
      }

      muster = poly;

      resClip.Clear();

      for(int k = 0; k < cWithPlanes.Size(); k++)
      {         
        const Plane& cPlane = cWithPlanes[k];
        if (poly.Clip(resClip,cPlane.Normal(),cPlane.D()))
        {
          // TODO: more efficent handling with temp1, temp2 and pointer
          poly = resClip;
          if (poly.N()<3) break;
        }
      }

      // check if something is rest after clipping
      if (poly.N()>=3)
      {

        for(int k = 0; k < poly.N(); k++)
        {
          Vector3Val pos = poly.Get(k);

          float fDoneFracNew = (wPlaneWith.D() + wPlaneWith.Normal() * pos) /
            (wPlaneWith.Normal() * cDeltaPosWith);

          if (fDoneFracNew < 0)
            continue;

          if (iFirstTouchPointPlane == -1)
          {
            if (fDoneFracNew <= fDoneFrac)
            {
              fDoneFrac = fDoneFracNew;
              cNormal = cPlaneNormalThisInWith;

              iFirstTouchPointPlane = i;
              cMinWithFaces.Add(i);
              cMinThisFaces.Add(w);

              cMinPos = pos;
            }
            continue;
          }

          // Is same point?
          if (pos.Distance2(cMinPos) < 0.000001 && fabsf(fDoneFracNew - fDoneFrac) < 0.01)
          {
            if (i == iFirstTouchPointPlane)
              cMinThisFaces.Add(w);
            else
            {                        
              if (i != cMinWithFaces[cMinWithFaces.Size() - 1])
                cMinWithFaces.Add(i);
            }


            if (cNormal * cDeltaPosWith < cPlaneNormalThisInWith * cDeltaPosWith)
              cNormal = cPlaneNormalThisInWith;

            continue;
          }

          // Different point
          if (fDoneFracNew <= fDoneFrac) 
          {
            fDoneFrac = fDoneFracNew;
            cNormal = cPlaneNormalThisInWith;

            iFirstTouchPointPlane = i;

            cMinWithFaces.Resize(0);
            cMinThisFaces.Resize(0);

            cMinWithFaces.Add(i);
            cMinThisFaces.Add(w);

            cMinPos = pos;
          }                                            
        }
      }
    }
  }

  if (iFirstTouchPointPlane != -1)
  {
    // we found something. now search for separatiom plane at new pos
    Vector3 move = cDeltaPosWith * fDoneFrac;

#define TOLERANCE_ZONE  0.005               
    if (move.SquareSize() > TOLERANCE_ZONE * TOLERANCE_ZONE) 
    {
      move -= cDeltaPosWith.Normalized() * TOLERANCE_ZONE;
    }
    else
    {
      move = VZero;
    }

    Matrix4 thisToWithNew(thisToWith);
    thisToWithNew.SetPosition(thisToWith.Position() + move);       

    Plane cSeparationPlane;

    for(int w = 0; w < cMinThisFaces.Size() == 1; w++)
    {        
      const Plane &cPlaneThis = cThis.GetPlane(acThis.GetPlanesArray(sThis), cMinThisFaces[w]);
      cSeparationPlane.SetNormal(thisToWithNew.Rotate(cPlaneThis.Normal()),0);
      cSeparationPlane.SetD(cPlaneThis.D() - cSeparationPlane.Normal() * thisToWithNew.Position());

      // Is it separation plane?

      // TODO: go through vertexes
      bool bSeparationPlane = true;
      for (int i = 0; i < cWith.NPlanes(); i++)
      {
        const Poly &faceWith = cWith.GetFace(sWith,i);

        for(int k = 0; k < faceWith.N(); k++)
        {
          Vector3Val pos = acWith.GetPos(sWith, faceWith.GetVertex(k));

          if (cSeparationPlane.Distance(pos) > 0)
          {
            bSeparationPlane = false;
            break;                        
          }

        }

        if (!bSeparationPlane)
        {
          break;
        }
      }

      if (bSeparationPlane)
      {
        cNormal = cSeparationPlane.Normal();
        return 1;
      }
    }

    for(int w = 0; w < cMinWithFaces.Size() == 1; w++)
    {        
      cSeparationPlane = cWith.GetPlane(acWith.GetPlanesArray(sWith), cMinWithFaces[w]);
      // Is it separation plane?

      // TODO: go through vertexes
      bool bSeparationPlane = true;
      for (int i = 0; i < cThis.NPlanes(); i++)
      {
        const Poly &faceThis = cThis.GetFace(sThis,i);

        for(int k = 0; k < faceThis.N(); k++)
        {
          Vector3Val pos = acThis.GetPos(sThis, faceThis.GetVertex(k));

          if (cSeparationPlane.Distance(thisToWithNew.FastTransform(pos)) > 0)
          {
            bSeparationPlane = false;
            break;                        
          }

        }

        if (!bSeparationPlane)
        {
          break;
        }
      }

      if (bSeparationPlane)
      {
        cNormal = -cSeparationPlane.Normal();
        return 2;
      }
    }
    // Take average normal form "this normals"
    cNormal = VZero;
    //GlobalShowMessage(20, "Average plane.");
    for (int i=0; i<cMinThisFaces.Size(); i++)
    {
      cNormal += cThis.GetPlane(acThis.GetPlanesArray(sThis), cMinThisFaces[i]).Normal();           
    }

    //cNormal /= cMinThisFaces.Size();
    cNormal = thisToWith.Rotate(cNormal);
    cNormal.Normalize();

    return 3;
  }

  return 0;
}


#if _ENABLE_CHEATS
#define DIAG_INTERSECT 1
#endif

#if _PROFILE
#pragma optimize( "", off )
#endif

void Object::Intersect(CollisionBuffer &result, const Object *with, const ObjectVisualState &thisPos, const ObjectVisualState &withPos, int hierLevel, bool bPerson) const
{
  // typical usage:
  // this is object in landscape
  // with is vehicle we test on collisions
  // with inverse transformation is typically cheap
  LODShape *withShape=with->GetShape();
  LODShape *thisShape=this->GetShape();

  if( !withShape || !thisShape ) return;
  // check all possible collisions between components of this and with
  // create transformation between with and this
  // first of all check object bounding spheres

  float dist2=thisPos.Position().Distance2(withPos.Position());
  float thisRad = this->GetCollisionRadius(thisPos);
  float withRad = with->GetCollisionRadius(withPos);
  float sumRadius2=Square(thisRad+withRad);
  if( dist2>sumRadius2 ) return; // no collision possible

  // make sure both objects have well defined geometry level
  const Shape *withGeom = with->IsWreck() ? withShape->WreckLevel() : withShape->GeometryLevel();
  const Shape *thisGeom = this->IsWreck() ? thisShape->WreckLevel() : thisShape->GeometryLevel();

  if( !withGeom || !thisGeom ) return;
  
  // with is typically a vehicle, animation is quite expensive and bounding sphere inaccurate
  Vector3 withMinMax[2];
  with->ClippingInfo(withPos,withMinMax,Object::ClipGeometry);

  // check "this" b-sphere against "with" min-max
  // quick and conservative test with no respect to axis
  if (
    Square(-withMinMax[0].X()+thisRad)<dist2 && Square(+withMinMax[1].X()+thisRad)<dist2 &&
    Square(-withMinMax[0].Y()+thisRad)<dist2 && Square(+withMinMax[1].Y()+thisRad)<dist2 &&
    Square(-withMinMax[0].Z()+thisRad)<dist2 && Square(+withMinMax[1].Z()+thisRad)<dist2
  )
  {
    return;
  }

  int thisLevel = this->IsWreck() ? thisShape->FindWreckLevel() : thisShape->FindGeometryLevel();
  int thisProxies = GetProxyCount(thisLevel);
  int withLevel = with->IsWreck() ? withShape->FindWreckLevel() : withShape->FindGeometryLevel();
  int withProxies = with->GetProxyCount(withLevel);
  
  Ref<ConvexComponents> thisComponents = this->IsWreck() ? new ConvexComponents() : thisShape->_geomComponents.GetRef();
  Ref<ConvexComponents> withComponents = with->IsWreck() ? new ConvexComponents() : withShape->_geomComponents.GetRef();

  // if geometry is empty on any side, no need to test them against each other
  if (thisComponents->Size() > 0 && withComponents->Size() > 0)
  {
    AnimationContextStorageGeom withContextStorage;
    AnimationContextStorageGeom thisContextStorage;
    AnimationContext withContext(withPos,withContextStorage);
    AnimationContext thisContext(thisPos,thisContextStorage);
    withGeom->InitAnimationContext(withContext, withComponents, true);
    thisGeom->InitAnimationContext(thisContext, thisComponents, true);

    unconst_cast(with)->AnimateGeometry(withContext);
    unconst_cast(this)->AnimateGeometry(thisContext);

    // minMax may be invalid now
    thisContext.RecalculateNormalsAsNeeded(thisGeom, *thisComponents);
    withContext.RecalculateNormalsAsNeeded(withGeom, *withComponents);

  #if PERF_ISECT
    ADD_COUNTER(isecO,1);
  #endif

    Matrix4Val withToThis = thisPos.GetInvTransform() * withPos;

    // draw min-max box positions
  #if DIAG_INTERSECT
    if (CHECK_DIAG(DECollision))
    {
      // draw bbox of this and with
      GScene->DiagBBox(thisContext.MinMax(), thisPos, PackedColor(Color(0,0,1,0.5)));
      GScene->DiagBBox(withContext.MinMax(), withPos, PackedColor(Color(0,1,0,0.5)));
    }
  #endif
    Vector3 isect[2];

    if (IntersectBBox(isect, thisContext.MinMax(), withContext.MinMax(), withToThis))
    {
  #if DIAG_INTERSECT
      if (CHECK_DIAG(DECollision))
      {
        GScene->DiagBBox(isect, thisPos, PackedColor(Color(0,0,0,0.5)));
      }
  #endif
      Matrix4Val thisToWith = withPos.GetInvTransform() * thisPos;    
      
      /* To speed up collision it will be used pre-calculated value of MaxScale from with and this.
        BE WARE: It is based on consideration  that max scale is not changed... 
      */
      float withPosScale = withPos.MaxScale();
      float thisPosScale = thisPos.MaxScale();

      thisContext.RecalculateConvexComponentsAsNeeded(thisGeom, *thisComponents);
      withContext.RecalculateConvexComponentsAsNeeded(withGeom, *withComponents);

      for (int iThis=0; iThis<thisComponents->Size(); iThis++)
      {
        if (!thisContext.IsCCEnabled(iThis)) continue;

        const ConvexComponent &cThis = *(*thisComponents)[iThis];

        // check thisComponent with "with" b-sphere
  #if PERF_ISECT
        ADD_COUNTER(isecT,1);
  #endif

        Vector3 thisCenter = thisPos.FastTransform(thisContext.GetCCCenter(iThis));
        float dist2 = thisCenter.Distance2(withPos.Position());
        float sumRadius2 = Square(thisContext.GetCCRadius(iThis) * thisPosScale + with->GetRadius());
        if (dist2 > sumRadius2) continue;

        for (int iWith=0; iWith<withComponents->Size(); iWith++)
        {
          if (!withContext.IsCCEnabled(iWith)) continue;

          const ConvexComponent &cWith=*(*withComponents)[iWith];
  
          // check this and with bounding sphere
  #if PERF_ISECT
          ADD_COUNTER(isecS,1);
  #endif

          float sphereDist2 = (thisPos * thisContext.GetCCCenter(iThis)).Distance2(withPos * withContext.GetCCCenter(iWith));        
          if (sphereDist2 > Square(thisContext.GetCCRadius(iThis) * thisPosScale + withContext.GetCCRadius(iWith) * withPosScale)) continue;
          // calculate in this space
          // edges have always one component different
          Vector3 tMinMax[2];

  #if PERF_ISECT
          ADD_COUNTER(isecB,1);
  #endif

          if( !IntersectBBox(tMinMax, thisContext.GetCCMinMax(iThis), withContext.GetCCMinMax(iWith), withToThis) ) continue;

  #if 0 //DIAG_INTERSECT
          if (CHECK_DIAG(DECollision))
          {
            GScene->DiagBBox(tMinMax,thisPos,PackedColor(Color(1,0,0,1)));
          }
  #endif

  #if PERF_ISECT
          ADD_COUNTER(isecP,1);
  #endif


          // Call new code and set up unique ID for two components
          int nNumberOfResults = result.Size();

          if (bPerson)
          {
            CalculateIntersectionsExactEx(
              result,this,
              thisContext, withContext,
              thisGeom,withGeom,
              cThis,cWith,thisToWith,withToThis,
              GetFrameBase(),hierLevel
            );
          }
          else
          {
            CalculateIntersectionsExact(
              result,this,
              thisContext, withContext,
              thisGeom,withGeom,
              cThis,cWith,thisToWith,withToThis,
              GetFrameBase(),hierLevel
            );
          }

          if (nNumberOfResults < result.Size())
          {
            for (; nNumberOfResults < result.Size(); nNumberOfResults++)
            {
              CollisionInfo& info = result[nNumberOfResults];
              info.component = iThis * 100000 + iWith;
              info.geomLevel = -1;

              // convert into world coord, it can be scaled so convert also under
              info.dirOut *= info.under;
              info.dirOut = thisPos.Rotate(info.dirOut);
              info.under = info.dirOut.Size();
              if (info.under != 0.0f)
                info.dirOut /= info.under;
              info.dirOutNotNorm = thisPos.Rotate(info.dirOutNotNorm);        
              info.pos = thisPos * info.pos;        
            }
          }       
        } // for( iWith )
      } // for( iThis )
    }
    unconst_cast(this)->DeanimateGeometry();
    unconst_cast(with)->DeanimateGeometry();
  }
  
  // scan all proxies in this geometry level
  if (thisProxies>0)
  {
    // perform all testing in this space (avoid world space to avoid precision problems)
    Ref<ObjectVisualState> withFrame = withPos.Clone();
    withFrame->SetTransform(thisPos.InverseGeneral() * withPos);
    
    for (int p=0; p<thisProxies; p++)
    {
      Matrix4 trans;
      // note: shape is ignored
      LODShape *shape = NULL;
      Object *parentObject = NULL;
      
      Object *obj = GetProxy(GetVisualStateAge(thisPos),shape,parentObject,thisLevel,trans,MIdentity,p);
      // if object is singular, do not test it
      if (!obj) continue;
      if (trans.Orientation().Scale2()<1e-6) continue;
      // pass frame
      Ref<ObjectVisualState> objFrame = obj->CloneFutureVisualState();
      objFrame->SetTransform(trans);

      // add intersection with given proxy
      int nResults = result.Size();
      obj->Intersect(result,with,*objFrame,*withFrame,hierLevel+1, bPerson);

      for(;nResults < result.Size(); nResults++)
      {
        // transform pos and dir to parent coordinates
        CollisionInfo& info = result[nResults];
        info.parentObject = unconst_cast(this);
        info.pos = thisPos.FastTransform(info.pos);
        info.dirOut = thisPos.Rotate(info.dirOut);
        info.dirOutNotNorm = thisPos.Rotate(info.dirOutNotNorm);
      }
    }
  }

  // scan all proxies in with geometry level
  if (withProxies>0)
  {
    // we can freely choose - this or with coordinate space?
    // for better code symmetry we compute all in with this time
    Ref<ObjectVisualState> thisFrame = thisPos.Clone();
    thisFrame->SetTransform(withPos.InverseGeneral() * thisPos);
    
    for (int p=0; p<withProxies; p++)
    {
      Matrix4 trans;
      // note: shape is ignored
      LODShape *shape = NULL;
      Object *parentObject = NULL;
      Object *obj = with->GetProxy(GetVisualStateAge(thisPos),shape,parentObject,withLevel,trans,MIdentity,p);
      // if object is singular, do not test it
      if (!obj) continue;
      if (trans.Orientation().Scale2()<1e-6) continue;
      // pass frame
      Ref<ObjectVisualState> objFrame = obj->CloneFutureVisualState();
      objFrame->SetTransform(trans);

      // add intersection with given proxy
      int nResults = result.Size();
      Intersect(result,obj,*thisFrame,*objFrame,hierLevel,bPerson);
      for(;nResults < result.Size(); nResults++)
      {
        // transform pos and dir to parent coordinates
        // we keep 'with' as the object, as that is what we are interested about
        CollisionInfo& info = result[nResults];
        info.pos = withPos.FastTransform(info.pos);
        info.dirOut = withPos.Rotate(info.dirOut);
        info.dirOutNotNorm = withPos.Rotate(info.dirOutNotNorm);
      }
    }
  }
}

#if _PROFILE
#pragma optimize( "", on )
#endif

int Object::FirstTouch(float &fDoneFrac, Vector3& cNormal, const ObjectVisualState &thisPos, const Object *with, const ObjectVisualState &withPos, Vector3Val cDeltaPos) const
{
  // typical usage:
  // this is object in landscape
  // with is vehicle we test on collisions
  // with inverse transformation is typically cheap
  int iReturn = -1;

  LODShape *withShape=with->GetShape();
  LODShape *thisShape=this->GetShape();
  if( !withShape || !thisShape ) return 0;
  // check all possible collisions between components of this and with
  // create transformation between with and this
  // first of all check object bounding spheres
  float dist2=thisPos.Position().Distance2(withPos.Position());
  float sumRadius2=Square(this->GetCollisionRadius(thisPos)+with->GetCollisionRadius(withPos));
  if( dist2>sumRadius2 ) return 0; // no collision possible

  const Shape *withGeom = with->IsWreck() ? withShape->WreckLevel() : withShape->GeometryLevel();
  const Shape *thisGeom = this->IsWreck() ? thisShape->WreckLevel() : thisShape->GeometryLevel();

  if( !withGeom || !thisGeom ) return 0;

  Ref<ConvexComponents> thisComponents = this->IsWreck() ? new ConvexComponents() : thisShape->_geomComponents.GetRef();
  Ref<ConvexComponents> withComponents = with->IsWreck() ? new ConvexComponents() : withShape->_geomComponents.GetRef();

  if( thisComponents->Size()>0 && withComponents->Size()>0 )
  {
    AnimationContextStorageGeom withStorage;
    AnimationContextStorageGeom thisStorage;
    AnimationContext withContext(withPos,withStorage);
    AnimationContext thisContext(thisPos,thisStorage);
    withGeom->InitAnimationContext(withContext, withComponents, true);
    thisGeom->InitAnimationContext(thisContext, thisComponents, true);

    // make sure both objects have well defined geometry level
    // note: we will deanimate it again
    unconst_cast(this)->AnimateGeometry(thisContext);
    unconst_cast(with)->AnimateGeometry(withContext);
    
    // minMax may be invalid now
    thisContext.RecalculateNormalsAsNeeded(thisGeom, *thisComponents);
    withContext.RecalculateNormalsAsNeeded(withGeom, *withComponents);
    
  #if PERF_ISECT
    ADD_COUNTER(isecO,1);
  #endif

    Matrix4Val withToThis = thisPos.GetInvTransform() * withPos;

    // draw min-max box positions
  #if DIAG_INTERSECT
    if (CHECK_DIAG(DECollision))
    {
      // draw bbox of this and with
      GScene->DiagBBox(thisContext.MinMax(), thisPos, PackedColor(Color(0,0,1,0.5)));
      GScene->DiagBBox(withContext.MinMax(), withPos, PackedColor(Color(0,1,0,0.5)));
    }
  #endif
    
    Vector3 cEnlargedWithMinMax[2];

    EnlargeBBox(cEnlargedWithMinMax, withContext.MinMax(), cDeltaPos);

    Vector3 isect[2];
    if (IntersectBBox(isect, thisContext.MinMax(), cEnlargedWithMinMax, withToThis))
    {
  #if DIAG_INTERSECT
      if (CHECK_DIAG(DECollision))
      {
        GScene->DiagBBox(isect, thisPos, PackedColor(Color(0,0,0,0.5)));
      }
  #endif
      Matrix4Val thisToWith = withPos.GetInvTransform() * thisPos;

      thisContext.RecalculateConvexComponentsAsNeeded(thisGeom, *thisComponents);
      withContext.RecalculateConvexComponentsAsNeeded(withGeom, *withComponents);

      for (int iThis=0; iThis<thisComponents->Size(); iThis++)
      {
        if (!thisContext.IsCCEnabled(iThis)) continue;
        // check thisComponent with "with" bsphere
  #if PERF_ISECT
        ADD_COUNTER(isecT,1);
  #endif

        const ConvexComponent &cThis = *(*thisComponents)[iThis];

        //TODO not valid now
        /*Vector3 thisCenter=thisPos.FastTransform(cThis.GetCenter());
        float dist2=thisCenter.Distance2(withPos.Position());
        float sumRadius2=Square(cThis.GetRadius()+with->GetRadius());
        if( dist2>sumRadius2 ) continue;
        */

        for (int iWith=0; iWith<withComponents->Size(); iWith++)
        {
          if (!withContext.IsCCEnabled(iWith)) continue;

          // check this and with bounding sphere
  #if PERF_ISECT
          ADD_COUNTER(isecS,1);
  #endif

          /*
          float sphereDist2=(thisToWith*thisComponent.GetCenter()).Distance2(withComponent.GetCenter());
          if( sphereDist2>Square(thisComponent.GetRadius()+withComponent.GetRadius()) ) continue;
          */
          // calculate in this space
          // edges have always one component different
          Vector3 tMinMax[2];

  #if PERF_ISECT
          ADD_COUNTER(isecB,1);
  #endif

          EnlargeBBox(cEnlargedWithMinMax, withContext.GetCCMinMax(iWith), cDeltaPos);

          if (!IntersectBBox(tMinMax, thisContext.GetCCMinMax(iThis), cEnlargedWithMinMax, withToThis)) continue;

  #if DIAG_INTERSECT
          if (CHECK_DIAG(DECollision))
          {
            GScene->DiagBBox(tMinMax, thisPos, PackedColor(Color(0.5,0.5,0.5,1)));
          }
  #endif

  #if PERF_ISECT
          ADD_COUNTER(isecP,1);
  #endif

          const ConvexComponent &cWith=*(*withComponents)[iWith];

          float fDoneFracNew = 1;
          Vector3 cNormalNew;

          int iReturnTemp = CalculateFirstTouchExact(
            fDoneFracNew, cNormalNew,
            thisContext, withContext,
            thisGeom,withGeom,
            cThis,cWith,thisToWith,withToThis,
            cDeltaPos
          );

          if (fDoneFrac > fDoneFracNew)
          {
            fDoneFrac = fDoneFracNew;
            cNormal = cNormalNew;
            iReturn = iReturnTemp;
          }

          /*
          if (bPerson)
          {

          // Call new code and set up unique ID for two components

          unsigned int nNumberOfResults = result.Size();

          CalculateIntersectionsExactEx
          (
          result,this,
          cThis,cWith,thisToWith,withToThis,
          *this,hierLevel
          );

          if (nNumberOfResults < result.Size())
          {
          for(; nNumberOfResults < result.Size(); nNumberOfResults++)
          {
          result[nNumberOfResults].component = iThis * 100000 + iWith;
          }
          }
          }
          else
          {
          CalculateIntersectionsExact
          (
          result,this,
          cThis,cWith,thisToWith,withToThis,
          *this,hierLevel
          );
          }
          */


        } // for( iWith )
      } // for( iThis )
    }
    unconst_cast(this)->DeanimateGeometry();
    unconst_cast(with)->DeanimateGeometry();

  }

  // scan all proxies in geometry level
  int geomLevel = this->IsWreck() ? thisShape->FindWreckLevel() : thisShape->FindGeometryLevel();
  int nProxies = GetProxyCount(geomLevel);
  if (nProxies>0)
  {
    //Ref<ObjectVisualState> trans0 = CloneFutureVisualState();
    for (int p=0; p<nProxies; p++)
    {
      Matrix4 trans;
      // note: shape is ignored
      LODShape *shape = NULL;
      Object *parentObject = NULL;
      Object *obj = GetProxy(GetVisualStateAge(thisPos), shape, parentObject, geomLevel, trans, GetFrameBase(), p);
      if (!obj) continue;

      // pass frame
      Ref<ObjectVisualState> objFrame = obj->CloneFutureVisualState();
      objFrame->SetTransform(trans);

      // add intersection with given proxy 
      float fDoneFracNew = 1;
      Vector3 cNormalNew;

      int iReturnTemp = obj->FirstTouch(fDoneFrac, cNormal, *objFrame, with, withPos, cDeltaPos); 

      if (fDoneFrac > fDoneFracNew)
      {            
        fDoneFrac = fDoneFracNew;
        cNormal = cNormalNew;
        iReturn = iReturnTemp;

      }

      /*
      obj->Intersect(result,with,objFrame,withPos,hierLevel+1, bPerson);

      for(;nResults < result.Size(); nResults++)
      {
      CollisionInfo& info = result[nResults];

      obj->DirectionModelToWorld(info.dirOut, info.dirOut);
      obj->PositionModelToWorld(info.pos, info.pos);  
      info.object = unconst_cast(this);
      }*/
    }
  }

  return iReturn;
}

void Object::IntersectLine(VisualStateAge age, CollisionBuffer &result, Vector3Par beg, Vector3Par end, float radius, ObjIntersect type, unsigned int collisionFlags) const
{
  IntersectLine(GetVisualStateByAge(age),result,beg,end,radius,type, 0, collisionFlags);
}

#define LOG_ISECT 0


void Object::IntersectLine(const ObjectVisualState &pos, CollisionBuffer &result, Vector3Par beg, Vector3Par end, float radius, ObjIntersect type, int hierLevel, unsigned int collisionFlags) const
{
  LODShape *thisShape=this->GetShape();
  if( !thisShape ) return;
  // handle singular case - zero sized components cannot be in collision
  if (pos.Scale()<=0) return;
  // make sure there is well defined geometry LOD
  ConvexComponents *cc=NULL;
  int geomLevel;
  if( type==ObjIntersectFire || type==ObjIntersectIFire )
  {
    cc=thisShape->_fireComponents;
    geomLevel = thisShape->_geometryFire;
  }
  else if( type==ObjIntersectView )
  {
    cc=thisShape->_viewComponents;
    geomLevel = thisShape->_geometryView;
  }
  else
  {
    cc=thisShape->_geomComponents;
    geomLevel = thisShape->_geometry;
  }
  if (geomLevel<0) return;

  if (_isDestroyed)
  {
    if (_destrType==DestructTree) return;
    if (_destrType==DestructTent) return;
  }

  IntersectLine(pos,result,beg,end,radius,type,cc,geomLevel,hierLevel, collisionFlags);
}

/**
@param type still needed for proxy level selection

\patch 5137 Date 3/6/2007 by Ondra
- Fixed: Hit was always assumed to be in lower part of the soldier body, even when head was hit.
*/
void Object::IntersectLine(const ObjectVisualState &pos, CollisionBuffer &result, Vector3Par beg, Vector3Par end, float radius,
  ObjIntersect type, const ConvexComponents *cc, int geomLevel, int hierLevel, unsigned int collisionFlags) const
{
  PROFILE_SCOPE_DETAIL_EX(clObI,coll);
  ADD_COUNTER(ssecT,1);
  
  LODShape *thisShape=this->GetShape();
  ShapeUsed geomShape=thisShape->Level(geomLevel);
  
  // scan all proxies in geometry level
  int nProxies = GetProxyCount(geomLevel);
  if (cc->Size()==0 && nProxies==0) return;

  #if 0 // _ENABLE_CHEATS
  #define DIAG_VERBOSE 1
  bool verbose = false;
  //if (CHECK_DIAG(DECollision))
  if (type==ObjIntersectFire)
  {
    if (!strcmpi(GetShape()->GetName(),"data3d\\scud_strela_proxy.p3d"))
    {
      verbose = true;
    }
  }
  #endif

  // check if distance of line beg..end from bounding sphere is low enough
  // distance of line and point:
  // find nearest point on line
  Vector3 rp = NearestPoint(beg,end,pos.Position());

  #if DIAG_VERBOSE
    if (verbose)
    {
      LODShape *shape = GScene->Preloaded(SphereModel);
      Ref<Object> obj=new ObjectColored(shape,VISITOR_NO_ID);
      obj->SetPosition(rp);
      obj->SetScale( 0.1 );
      obj->SetConstantColor(PackedColor(Color(1,0,0)));
      GScene->ShowObject(obj);
    }
    if (verbose)
    {
      LODShape *shape = GScene->Preloaded(SphereModel);
      Ref<Object> obj=new ObjectColored(shape,VISITOR_NO_ID);
      obj->SetPosition(pos.Position());
      obj->SetScale( 2.0 );
      obj->SetConstantColor(PackedColor(Color(0,0,1)));
      GScene->ShowObject(obj);
    }
  #endif

  float rDistFromLine2 = rp.Distance2(pos.Position());
  float bMaxRadius = GetCollisionRadius(pos);
  float rDistFromLine2Max = Square(bMaxRadius + radius);
  // quick rejection of very far objects
  if( rDistFromLine2>rDistFromLine2Max )
  {
    #if DIAG_VERBOSE
    if (verbose)
    {
      LogF
      (
        " obj missed, dist %.3f>%.3f, bRadius %.3f",
        sqrt(rDistFromLine2),sqrt(rDistFromLine2Max),
        thisShape->BoundingSphere()
      );
    }
    #endif
    return;
  }

  // a little bit more expensive and more accurate test
  Vector3 bCenter;
  float bRadius;
  AnimatedBSphere(geomLevel,bCenter,bRadius,pos,pos.MaxScale());
  Vector3 p = NearestPoint(beg,end,bCenter);

  #if DIAG_VERBOSE
    if (verbose)
    {
      LODShape *shape = GScene->Preloaded(SphereModel);
      Ref<Object> obj=new ObjectColored(shape,VISITOR_NO_ID);
      obj->SetPosition(p);
      obj->SetScale( 0.1 );
      obj->SetConstantColor(PackedColor(Color(1,0,0)));
      GScene->ShowObject(obj);
    }
  #endif
  
  float distFromLine2 = p.Distance2(bCenter);
  if( distFromLine2>Square(bRadius + radius) )
  {
    #if DIAG_VERBOSE
    if (verbose)
    {
      LogF(" line missed, dist %.3f>%.3f",sqrt(distFromLine2),bRadius);
    }
    #endif
    return;
  }

#if _ENABLE_PERFLOG>1
  if (PROFILE_SCOPE_NAME(clObI).IsActive())
    PROFILE_SCOPE_NAME(clObI).AddMoreInfo(_shape->GetName());
  #endif
  ADD_COUNTER(ssecO,1);

  // convert line end points into the model (this) space
  Matrix4Val invTransform=pos.GetInvTransform();
  Vector3Val begThis=invTransform.FastTransform(beg);
  Vector3Val endThis=invTransform.FastTransform(end);
  
  // consider: should we perform line against min-max box now?
  Vector3 minMax[2];
  ClippingInfo(pos,minMax,ClipGeometry);
  bool doBevelTest = radius > 0;

  if(doBevelTest)
  {
    Vector3 newMins, newMaxs;
    newMins = minMax[0] - Vector3(radius);
    newMaxs = minMax[1] + Vector3(radius);
    if (!IsIntersectionBoxWithLine(newMins,newMaxs,begThis,endThis))
    {
      return;
    }
  }
  else
  {
    if (!IsIntersectionBoxWithLine(minMax[0],minMax[1],begThis,endThis))
    {
      return;
    }
  }

  ADD_COUNTER(ssecM,1);
  
#if !_HELISIM_ROTORLIB_SIM_THREAD

  AnimationContextStorageGeom storage;
  AnimationContext animContext(pos,storage);
  {
    PROFILE_SCOPE_DETAIL_EX(clOIA,coll);
  geomShape->InitAnimationContext(animContext, cc, true);

  unconst_cast(this)->AnimateComponentLevel(animContext, geomLevel);  

    // TODO: perform some statistics: which object are recalculated most often (and why)
    animContext.RecalculateConvexComponentsAsNeeded(geomShape, *cc);
  // update normals, min-max and bSphere
    // note: uses ccs as well, should be done after RecalculateConvexComponentsAsNeeded
  animContext.RecalculateNormalsAsNeeded(geomShape, *cc);
  }

  #if LOG_ISECT
    //if (type==ObjIntersectFire)
    {
      LogF("begThis %.1f,%.1f,%.1f,",begThis.X(),begThis.Y(),begThis.Z());
      LogF("endThis %.1f,%.1f,%.1f,",endThis.X(),endThis.Y(),endThis.Z());
    };
  #endif

  for (int iThis=0; iThis<cc->Size(); iThis++)
  {
    if (!animContext.IsCCEnabled(iThis)) continue;

    const ConvexComponent &cThis = *cc->Get(iThis);
    if ((collisionFlags & CF_ONLY_WATER) && (cThis.GetTexture() && cThis.GetTexture()->IsWater()))
      continue; 

    ADD_COUNTER(ssecC,1);
    
    //we will need them later anyway
    Vector3 mins = animContext.GetCCMin(iThis);
    Vector3 maxs = animContext.GetCCMax(iThis);

    // check against min-max - a lot faster than walking all planes
    if(doBevelTest)
    {
      Vector3 newMins = mins - Vector3(radius);
      Vector3 newMaxs = maxs + Vector3(radius);
      if (!IsIntersectionBoxWithLine(newMins, newMaxs, begThis, endThis))
      {
        continue;
      }
    }
    else
    {
      if (!IsIntersectionBoxWithLine(mins, maxs, begThis, endThis))
      {
        continue;
      }
    }

    ADD_COUNTER(ssecX,1);
    
    // check intersection will all convex components
    // check line beg..end with all cThis faces
    Vector3 b=begThis,e=endThis;
    float bt=0,et=1;
    // clip the b..e line with all planes of cThis
    Assert( cThis.NPlanes()>=4 );
    Vector3 dirOut = VUp;
    bool entry = false;
    bool exit = false;

    #if LOG_ISECT
    LogF
    (
      "%s (%s): component %d",
      (const char *)GetDebugName(),(const char *)GetShape()->Name(),iThis
    );
    #endif
    // detect singularity - when zero matrix applied, all normals are singular
    if (cThis.NPlanes() < 1 || cThis.GetPlane(animContext.GetPlanesArray(geomShape), 0).Normal().SquareSize() == 0) continue;
    
    int numPlanes = cThis.NPlanes();

    if(doBevelTest && !cThis.IsBox()) numPlanes += 6; //beveled convex needs to have extra 6 planes to cut out sharp edges

    for (int i=0; i<numPlanes; i++)
    {
      static Plane beveledPlane;
      const Plane *plane;
      
      int extraPlane = i - cThis.NPlanes();
      if(extraPlane < 0)
      {
        plane = &cThis.GetPlane(animContext.GetPlanesArray(geomShape), i);
        if(doBevelTest)
        {
          beveledPlane = *plane;
          beveledPlane.SetD(beveledPlane.D() + radius);
          plane = &beveledPlane;
        }
      }
      else
      {
        //add extra plane
        switch(extraPlane)
        {
        case 0:
          beveledPlane.SetNormal(Vector3(1,0,0), -mins[0] + radius);
          break;
        case 1:
          beveledPlane.SetNormal(Vector3(-1,0,0), maxs[0] + radius);
          break;
        case 2:
          beveledPlane.SetNormal(Vector3(0,1,0), -mins[1] + radius);
          break;
        case 3:
          beveledPlane.SetNormal(Vector3(0,-1,0), maxs[1] + radius);
          break;
        case 4: 
          beveledPlane.SetNormal(Vector3(0,0,1), -mins[2] + radius);
          break;
        case 5:
          beveledPlane.SetNormal(Vector3(0,0,-1), maxs[2] + radius);
          break;
        };
        plane = &beveledPlane;
      }
      float distE = plane->Distance(e);
      float distB = plane->Distance(b);
      // dist<0 means point is in outer space
      if( distB<0 && distE<0 )
      {
        #if LOG_ISECT
          LogF("  NotInside");
        #endif
        goto NotInside;
      }
      // dist>=0 means point is in inner space
      if( distB>=0 && distE>=0 ) continue; // no clip
      Vector3Val normal=plane->Normal();
      Vector3 bme=b-e;
      float denom=bme*normal;
      if( fabs(denom)<1e-6 )
      {
        #if _ENABLE_REPORT
        if( bme.Normalized()*normal<1e-6 )
        {
          RptF(
            "Object::Intersect bme %.2f,%.2f,%.2f normal %.2f,%.2f,%.2f",
            bme.X(),bme.Y(),bme.Z(),
            normal.X(),normal.Y(),normal.Z()
          );
        }
        #endif
        #if LOG_ISECT
          LogF("  Parallel");
        #endif
        continue; // parallel - no clip
      }
      float t=(normal*b+plane->D())/denom;
      
      // there must be some intersection
      Assert( t>=-1e-3 );
      Assert( t<=1+1e-3 );
      //Vector3 pt=(b-e)*t+e;
      Vector3 pt=(e-b)*t+b;
      Assert( plane->Distance(pt)<1e-3 );
      // note: t is between bt and et
      float tt = bt+t*(et-bt);

      if( distB<0 )
      {
        // b is outside 
        //Assert(t<=et);
        b=pt,bt=tt;
        entry = true;
        #if LOG_ISECT
          LogF("  setb %.3f",tt);
        #endif
        dirOut=normal;
      }
      else
      {
        // e is outside
        Assert(distE<0);
        //Assert(bt<=t);
        e=pt,et=tt;
        exit = true;
        #if LOG_ISECT
          LogF("  sete %.3f",tt);
        #endif
      }

      #if 0 // _ENABLE_CHEATS
        if( CHECK_DIAG(DECollision) )
        {
          GScene->DrawCollisionStar(
            PositionModelToWorld(pt),0.05,PackedColor(Color(0.25,0,0.25))
          );
        }
      #endif
    }
    {
      // b,e interval is intersection
      /**/
      if (bt>et)
      {
        // there must be some degenerate planes?
        // probably non-convex component?
        #if LOG_ISECT
          LogF("  t %.3f..%.3f no hit",bt,et);
        #endif
        //continue;
      }
      /**/

      // transform into world coord, it can be scaled
      Vector3 worldPos = pos.FastTransform(b);

      if((collisionFlags & CF_NEAREST_CONTACT) == 0 || !result.Size() || beg.Distance2(worldPos) < beg.Distance2(result[0].pos))
      {
        // return collision information
        CollisionInfo &ret = ((collisionFlags & CF_NEAREST_CONTACT) && result.Size() > 0) ? result[0] : result.Append();
        // check nearest point of intersections
        Vector3 tPoint=b;
        float t=bt;
        #if 0
          // impossible hit detection
        float maxDist = _shape->GeometrySphere()*2;
        Vector3 geomCenter = _shape->GeometryCenter();
          // geometry lod worked somewhat better, however for animated objects it was bad as well
          // esp. for soldiers, where animation moves the body a lot
        if( tPoint.Distance2(geomCenter)>Square(maxDist) )
        {
    #if LOG_ISECT
            LogF("Impossible hit %.3f,%.3f,%.3f",tPoint[0],tPoint[1],tPoint[2]);
    #endif
          tPoint=geomCenter+(tPoint-geomCenter).Normalized()*maxDist;
        }
        #endif
        #if _ENABLE_CHEATS //SHOT_STARS
  #if LOG_ISECT
            LogF("  tPoint       %.3f,%.3f,%.3f",tPoint[0],tPoint[1],tPoint[2]);
  #endif
          if( CHECK_DIAG(DECollision) )
          {
            GScene->DrawCollisionStar(
              FutureVisualState().PositionModelToWorld(tPoint),0.05,PackedColor(Color(0.5,0,0.5))
            );
            GScene->DrawCollisionStar(
              FutureVisualState().PositionModelToWorld(b),0.025,PackedColor(Color(0.25,0,0.25))
            );
            GScene->DrawCollisionStar(
              FutureVisualState().PositionModelToWorld(e),0.025,PackedColor(Color(0.25,0,0.25))
            );
          }
        #endif
        ret.pos=worldPos;

        // get texture from any face of the component

        ret.texture = cThis.GetTexture();
        ret.surface = cThis.GetSurfaceInfo();
        /// caution: dirOut and under have different meaning here
        ret.dirOut = pos.Rotate(e-b);
        ret.dirOutNotNorm = -pos.Rotate(dirOut);
        ret.under = t;

        ret.hierLevel = hierLevel;
        ret.object = unconst_cast(this);
        ret.geomLevel = geomLevel;
        ret.component = iThis;
        ret.entry = entry;
        ret.exit = exit;
        #if 0
          Log("  t %.3f..%.3f hit %s",bt,et,(const char *)GetDebugName());
          // calculate distance from beg
          Log("  dist %.3f, %s",tPoint.Distance(begThis)/endThis.Distance(begThis),GetShape()->Name());
        #endif
        #if _ENABLE_CHEATS //SHOT_STARS
          if( CHECK_DIAG(DECollision) )
          {
            GScene->DrawCollisionStar(
              ret.pos,0.05,PackedColor(Color(1,0,1))
            );
            GScene->DrawDiagArrow(
              ret.pos,ret.dirOutNotNorm,1,PackedColor(Color(1,0,0.5))
            );
          }
        #endif
      }
      if(collisionFlags & CF_FIRST_CONTACT)
      {
        unconst_cast(this)->Deanimate(geomLevel, false);
        return;
      }
    }

    
    NotInside:;
  }
  if (nProxies>0)
  {
    Matrix4 trans0 = pos.Transform();
    VisualStateAge age = GetVisualStateAge(pos);
    for (int p=0; p<nProxies; p++)
    {
      // note: shape is ignored
      LODShape *shape = NULL;
      Matrix4 trans;
      Object *parentObject = NULL;
      Object *obj = GetProxy(age,shape,parentObject,geomLevel,trans,pos,p);
      if (!obj) continue;
      ObjectVisualState const& objvs = obj->GetVisualStateByAge(age);
      // if the object is collapsed, no need to test it
      if (objvs.Scale()<1e-3f) continue;
      if (trans.Orientation().Scale2()<1e-3f) continue;
      // pass frame
      Ref<ObjectVisualState> objFrame = obj->CloneFutureVisualState();
      objFrame->SetTransform(trans);
      // add intersection with given proxy
      int nResults = result.Size();
      obj->IntersectLine(*objFrame,result,beg,end,radius,type,hierLevel+1, collisionFlags);

      for(;nResults < result.Size(); nResults++)
      {
        // transform pos and dir to parent coordinates
        CollisionInfo& info = result[nResults];
        info.parentObject = unconst_cast(this);
      }
      if(result.Size() && collisionFlags & CF_FIRST_CONTACT)
        break;
    }
  }

  unconst_cast(this)->Deanimate(geomLevel, false);

#endif
}


bool Object::VerifyStructure() const
{
  return true;
}

bool Object::IsInside(Vector3Par pos, ObjIntersect type) const
{
  LODShape *thisShape=this->GetShape();
  if( !thisShape ) return false;
  // make sure there is well defined geometry LOD

  ConvexComponents *cc=NULL;
  int geomLevel;
  if( type==ObjIntersectFire || type==ObjIntersectIFire )
  {
    cc=thisShape->_fireComponents, geomLevel = thisShape->_geometryFire;
  }
  else if( type==ObjIntersectView )
  {
    cc=thisShape->_viewComponents, geomLevel = thisShape->_geometryView;
  }
  else
  {
    cc=thisShape->_geomComponents, geomLevel = thisShape->_geometry;
  }
  if (geomLevel<0) return false;
  ShapeUsed geomShape=thisShape->Level(geomLevel);

  if( cc->Size()<=0 ) return false;

  if (_isDestroyed)
  {
    if (_destrType==DestructTree) return false;
    if (_destrType==DestructTent) return false;
  }

  AnimationContextStorageGeom storage;
  AnimationContext animContext(FutureVisualState(),storage);
  geomShape->InitAnimationContext(animContext, cc, true);

  // note: we will deanimate it again
  unconst_cast(this)->AnimateComponentLevel(animContext, geomLevel);
  animContext.RecalculateNormalsAsNeeded(geomShape, *cc);
  animContext.RecalculateConvexComponentsAsNeeded(geomShape, *cc);

  // all calculation will be performed in this space
  // convert beg and end

  Matrix4Val invTransform=FutureVisualState().GetInvTransform();
  Vector3Val point=invTransform.FastTransform(pos);
  bool ret = false;
  
  for (int iThis=0; iThis<cc->Size(); iThis++)
  {
    if (!animContext.IsCCEnabled(iThis)) continue;

    // check intersection will all convex components
    const ConvexComponent &cThis=*(*cc)[iThis];
    if (cThis.IsInside(animContext.GetPlanesArray(geomShape), point))
    {
      ret=true;
      break;
    }
  }
  unconst_cast(this)->DeanimateComponentLevel(geomLevel);
  return ret;
}

#if _ENABLE_CHEATS
  bool Object::CheckDrawToggle() const
  {
    return CHECK_OBJ_ENABLE(ODEObject);
  }

  float Object::CheckDrawScale() const
  {
    return ObjDrawScale[ODEObject];
  }
#endif

void Object::DrawDiags()
{
  if(CHECK_DIAG(DEPhys) && _pPhysics)
  {
    if(_pPhysics->GetCCDRadius() > 0.00001f)
      enf::Shape::CreateSphere(FutureVisualState().Position(), GetPhysics()->GetCCDRadius(), 0x4f5f1f3f, enf::SS_NOZWRITE|enf::SS_TRANSP|enf::SS_BACKFACE|enf::SS_ONCE);
   
    Vector3 ppos = GWorld->GetCamaraBase().Position();
    Vector3 mypos = FutureVisualState().Position();
    Vector3 dir = mypos - ppos;

    if(ppos.Distance2(mypos) < Square(100.f))
      GLandscape->GetPhysicsWorld()->RenderEntity(_pPhysics);
  }
  #if 0
  // draw bounding sphere of geometry level
  const Shape *geom = _shape ? _shape->GeometryLevel() : NULL;
  if (geom)
  {
    Vector3 bCenter = FutureVisualState().PositionModelToWorld(geom->BSphereCenter());
    float bRadius = geom->BSphereRadius();
    enf::Shape::CreateSphere(bCenter, bRadius,PackedColor(Color(0,0,0,0.1)), enf::SS_ONCE|enf::SS_TRANSP|enf::SS_NOZWRITE);
  }
  #endif

}

void Object::DrawFocusedDiags()
{
}

bool Object::InitSkewReplace(Landscape *land, Matrix4 &trans)
{
  return false;
}

static RStringB SlopeProp = "slope";
static RStringB SlopePropZ = "slopez"; // change slope just in z-dir
static RStringB SlopePropX = "slopex"; // change slope just in x-dir
static RStringB SlopePropLandContact = "slopelandcontact"; // take first two or three land-contacts and change slope according to them... 

static RStringB PlacementProp = "placement";

bool Object::InitSkew(Landscape *land, Matrix4 &pos) const
{
  // check shape placement property
  LODShape *shape = GetShape();
  if (!shape) return false;
  const Shape *geom = shape->GeometryLevel();
  RStringB prop = geom ? geom->PropertyValue(PlacementProp) : RStringB();
  if (prop!=SlopeProp && prop!=SlopePropX && prop!= SlopePropZ && prop!= SlopePropLandContact )
  {
    // check animation type and surface alignment requirements
    if (GetType()==Primary)
    {
      if ((shape->GetOrHints()&ClipLandMask)!=ClipLandKeep || shape->GetAnimationType()==AnimTypeSoftware)
      {
        return false;
      }
    }
    else
    {
      if ((shape->GetOrHints()&ClipLandMask)!=ClipLandKeep || shape->GetAnimationType()==AnimTypeHardware)
      {
        return false;
      }
      shape->SetAnimationType(AnimTypeNone);
    }
    // some shapes are autodetected as skewed
    #if _ENABLE_REPORT
      if ((shape->GetAndHints()&ClipLandMask)!=ClipLandKeep)
      {
        LogF("Not all levels in %s have Keep Height set",(const char *)shape->GetName());
      }
    #endif
    //RptF("Property Placement=slope missing on %s",(const char *)shape->GetName());
  }
  // adjust skew
  

  Vector3 bCenter = pos.FastTransform(-GetShape()->BoundingCenter());
  if (prop==SlopePropLandContact)
  {  
    const Shape *landContact = shape->LandContactLevel();
    if (landContact && landContact->NPoints() >= 1)
    {
      float dX = 0;
      float dZ = 0;
      Vector3 pt0;
      if (landContact->NPoints() == 1)
      {
        // no skew, only place landcontact on ground
        pt0 = pos.FastTransform(landContact->Pos(0));
        pt0[1] = land->SurfaceY(pt0[0],pt0[2]);
      }
      else if (landContact->NPoints() == 2)
      {
        // skew only in direction between landcontacts, two landcontact points must lie on ground
        pt0 = pos.FastTransform(landContact->Pos(0));
        Vector3 pt1 = pos.FastTransform(landContact->Pos(1));

        pt0[1] = land->SurfaceY(pt0[0],pt0[2]);
        pt1[1] = land->SurfaceY(pt1[0],pt1[2]);

        Vector3 diff = pt0 - pt1;
       

        float slope = diff[1] / (diff[0]* diff[0] + diff[2]*diff[2]);
        dX = slope * diff[0];
        dZ = slope * diff[2];     
      }   
      else if (landContact && landContact->NPoints() == 3)
      {
        // skew all three points must lie on ground... 
        pt0 = pos.FastTransform(landContact->Pos(0));
        Vector3 pt1 = pos.FastTransform(landContact->Pos(1));
        Vector3 pt2 = pos.FastTransform(landContact->Pos(2));

        pt0[1] = land->SurfaceY(pt0[0],pt0[2]);
        pt1[1] = land->SurfaceY(pt1[0],pt1[2]);
        pt2[1] = land->SurfaceY(pt2[0],pt2[2]);

        Vector3 normal = (pt0 - pt1).CrossProduct(pt2 - pt1);
        Assert(fabs(normal[1]) > 0.00001f);

        dX = -normal[0]/normal[1];
        dZ = -normal[2]/normal[1];
      }
      else
      {
        RptF(
          "Placement slopelandcontact failed on model %s. Too many (%d) points in landcontact.",
          shape->Name(),landContact->NPoints()
        );
        return false;
      }

      Matrix3 skew = M3Identity;   
      skew(1,0) = dX;
      skew(1,2) = dZ;
      Matrix3 trans = skew * pos.Orientation();
      pos.SetOrientation(trans);      

      float relHeight = bCenter[1] - land->SurfaceY(bCenter[0], bCenter[2]);
      pos.SetPosition(pos.Position() + VUp * (pt0[1] + relHeight - pos.FastTransform(landContact->Pos(0))[1]));

      return true;      
    }
    else
    {
      RptF("Placement slopelandcontact failed on model %s. Missing landcontact or no points in it.", shape->Name());
      return false;
    }
  }
 
 
  float dX,dZ;
  land->SurfaceY(bCenter.X(),bCenter.Z(),&dX,&dZ);

  if (prop==SlopePropX)
  {     
    float slope = dX * pos.Orientation()(0,0) + dZ * pos.Orientation()(2,0);
    dX = slope * pos.Orientation()(0,0);
    dZ = slope * pos.Orientation()(2,0);    
  }else if (prop==SlopePropZ)
  {  
    
    float slope = dX * pos.Orientation()(0,2) + dZ * pos.Orientation()(2,2);
    dX = slope * pos.Orientation()(0,2);
    dZ = slope * pos.Orientation()(2,2);    
  } 
    
  Matrix3 skew = M3Identity;
  skew(1,0) = dX;
  skew(1,2) = dZ;
  Matrix3 trans = skew * pos.Orientation();
  pos.SetOrientation(trans);

  // position bCenter to same height... 
  Vector3 newBCenter = pos.FastTransform(-GetShape()->BoundingCenter());
  pos.SetPosition(pos.Position() + VUp * (bCenter[1] - newBCenter[1]));
  return true;
}

void Object::InitSkew(Landscape *land)
{
  Matrix4 trans = FutureVisualState().Transform();
  if (InitSkew(land,trans))
    SetTransformSkewed(trans);
}

void Object::DeSkew(const Landscape *land, Matrix4& trans, float& relHeight)
{
  LODShape *shape = GetShape();
  if (!shape) return;
  const Shape *geom = shape->GeometryLevel();
  RStringB prop = geom ? geom->PropertyValue(PlacementProp) : RStringB();
  Vector3 bCenter =  trans.FastTransform(-GetShape()->BoundingCenter());
  if (prop!=SlopeProp && prop!=SlopePropX && prop!= SlopePropZ && prop!= SlopePropLandContact )
  {
    if (GetType()==Primary)
    {
      if ((shape->GetOrHints()&ClipLandMask)!=ClipLandKeep || shape->GetAnimationType()==AnimTypeSoftware)
      {
        relHeight = bCenter[1] - land->SurfaceY(bCenter[0], bCenter[2]);
        return;
      }
    }
    else
    {
      if ((shape->GetOrHints()&ClipLandMask)!=ClipLandKeep || shape->GetAnimationType()==AnimTypeHardware)
      {
        relHeight = bCenter[1] - land->SurfaceY(bCenter[0], bCenter[2]);
        return;
      }
      
    }    
  }

  if (prop==SlopePropLandContact)
  {  
    const Shape *landContact = shape->LandContactLevel();
    if (landContact && landContact->NPoints() >= 1)
    {
      float dX = 0;
      float dZ = 0;
      Vector3 pt0;
      if (landContact->NPoints() == 1)
      {
        // no skew, only place landcontact on ground
        pt0 = trans.FastTransform(landContact->Pos(0));
        pt0[1] = land->SurfaceY(pt0[0],pt0[2]);
      }
      else if (landContact->NPoints() == 2)
      {
        // skew only in direction between landcontacts, two landcontact points must lie on ground
        pt0 = trans.FastTransform(landContact->Pos(0));
        Vector3 pt1 = trans.FastTransform(landContact->Pos(1));

        pt0[1] = land->SurfaceY(pt0[0],pt0[2]);
        pt1[1] = land->SurfaceY(pt1[0],pt1[2]);

        Vector3 diff = pt0 - pt1;

        float slope = diff[1] / (diff[0] * diff[0] + diff[2] * diff[2]);
        dX = slope * diff[0];
        dZ = slope * diff[2];     
      }   
      else if (landContact && landContact->NPoints() == 3)
      {
        // skew all three points must lie on ground... 
        pt0 = trans.FastTransform(landContact->Pos(0));
        Vector3 pt1 = trans.FastTransform(landContact->Pos(1));
        Vector3 pt2 = trans.FastTransform(landContact->Pos(2));

        pt0[1] = land->SurfaceY(pt0[0],pt0[2]);
        pt1[1] = land->SurfaceY(pt1[0],pt1[2]);
        pt2[1] = land->SurfaceY(pt2[0],pt2[2]);

        Vector3 normal = (pt0 - pt1).CrossProduct(pt2 - pt1);
        Assert(fabs(normal[0]) > 0.00001f);

        dX = -normal[0]/normal[1];
        dZ = -normal[2]/normal[1];
      }
      else
      {
        //RptF("Placement slopelandcontact failed on model %s. To many points in landcontact.", shape->Name());
        return;
      }

      /// keep relative height from point 0
      relHeight = trans.FastTransform(landContact->Pos(0))[1] - pt0[1];
      
      Vector3 pos = trans.Position();
      pos[1] = land->SurfaceY(pos[0], pos[2]) + relHeight;
      trans.SetPosition(pos); 

      Matrix3 deSkew = M3Identity;   
      deSkew(1,0) = -dX;
      deSkew(1,2) = -dZ;
      Matrix3 transRes = deSkew * trans.Orientation();
      trans.SetOrientation(transRes);                

      Vector3  newBCenter = trans.FastTransform(-GetShape()->BoundingCenter());
      trans.SetPosition(trans.Position() + VUp * ( land->SurfaceY(newBCenter.X(),newBCenter.Z()) + relHeight - newBCenter[1]));
    }
    return;          
  }
  
  relHeight = bCenter[1] - land->SurfaceY(bCenter[0], bCenter[2]);

  float dX,dZ;
  land->SurfaceY(bCenter.X(),bCenter.Z(),&dX,&dZ);

  if (prop==SlopePropX)
  {     
    float slope = dX * trans.Orientation()(0,0) + dZ * trans.Orientation()(2,0);
    dX = slope * trans.Orientation()(0,0);
    dZ = slope * trans.Orientation()(2,0);    
  }else if (prop==SlopePropZ)
  {  

    float slope = dX * trans.Orientation()(0,2) + dZ * trans.Orientation()(2,2);
    dX = slope * trans.Orientation()(0,2);
    dZ = slope * trans.Orientation()(2,2);    
  } 

  Matrix3 deSkew = M3Identity;
  deSkew(1,0) = -dX;
  deSkew(1,2) = -dZ;
  Matrix3 transRes = deSkew * trans.Orientation();
  trans.SetOrientation(transRes);

  Vector3  newBCenter = trans.FastTransform(-GetShape()->BoundingCenter());
  trans.SetPosition(trans.Position() + VUp * (bCenter[1] - newBCenter[1]));
  return;
}

DEFINE_FAST_ALLOCATOR(ObjectPlain)

ObjectPlain::ObjectPlain(LODShape *shape, const CreateObjectId &id)
:base(shape,id)
{
}

ObjectPlain::~ObjectPlain()
{
}

DEFINE_FAST_ALLOCATOR(ObjectPlainNoInstancing)

ObjectPlainNoInstancing::ObjectPlainNoInstancing(LODShape *shape, const CreateObjectId &id)
:base(shape,id)
{
}

ObjectPlainNoInstancing::~ObjectPlainNoInstancing()
{
}

WindObjectType::WindObjectType(
  float frequency0ms, float frequency10ms, float amplitude, float tilt,
  bool setOrientationLast
)
:_frequency0ms(frequency0ms),_frequency10ms(frequency10ms),
_amplitude(amplitude),_tilt(tilt),_setOrientationLast(setOrientationLast)
{
	float windChange = _amplitude + _tilt;
	float windChangeOrtho = _amplitude * 0.2f;

  _maxSkewFactor = sqrt(windChange*windChange + windChangeOrtho*windChangeOrtho);
}

void WindObjectType::AnimateOrigin(Matrix4 &origin, int seed, Vector3Par boundingCenter)
{
	Vector3 wind = GWorld->GetWindSlow(origin.Position(),GWorld->GetWindEmitterScope());

	//  LogF("wind %2.5g, %2.5g, %2.5g", wind.X(), wind.Y(), wind.Z());

	if (_setOrientationLast && _tilt>0)
	{
		// convert wind to model space
		wind = origin.Orientation().InverseGeneral()*wind;
	}

	// Frequency can change +-10%
	float rnd1 = GRandGen.RandomValue(seed + 0);
	float rnd2 = GRandGen.RandomValue(seed + 1);
	float rnd3 = GRandGen.RandomValue(seed + 2);

	float frequencyCoef = 1.0f + rnd1* 0.2f - 0.1f;

	// _frequency10ms is currently ignored - smooth changes in frequency change phase as well, look bad
	// float frequency = InterpolativC(wind.SquareSize(),0,10*10,_frequency0ms,_frequency10ms);

	// Input of harmonical function
	float x = (rnd2 + Glob.time.toFloat() * _frequency0ms * frequencyCoef) * 2 * H_PI;

	// Calculate the change in the wind direction
	float windChange = sin(x) * _amplitude + _tilt;

	// Orthogonal amplitude is between 0% and 20% of original amplitude, regardless of direction
	float orthoAmplitudeCoef = rnd3 * 0.4f - 0.2f;

	// Calculate the change in the direction orthogonal to the wind direction
	float windChangeOrtho = cos(x) * _amplitude * orthoAmplitudeCoef;

	// Calculate the skew matrix
	float skewX = wind.X() * windChange - wind.Z() * windChangeOrtho;
	float skewZ = wind.Z() * windChange + wind.X() * windChangeOrtho;
	float invSize = InvSqrtFast(1 + skewX * skewX + skewZ * skewZ);
//	float invSize = InvSqrt(1 + skewX * skewX + skewZ * skewZ);

	//this look much more simpler than matrix multiplications

	float sx = skewX * invSize;
	float sz = skewZ * invSize;

	if (_setOrientationLast)
	{
		//skew combined with translation up and down, using the matrix multiplication associativity and a lot of paper ;-)
		//        | 1                  0            0        | skewX*invSize*boundingCenter.y              |
		// skew = | skewX*invSize   invSize   skewX*invSize  | invSize*boundingCenter.y - boundingCenter.y |
		//        | 0                  0            1        | skewZ*invSize*boundingCenter.y              |
		Matrix4 windSkew;

		windSkew(1,0) = windSkew(2,0) = windSkew(0,2) = windSkew(1,2) = 0.0f;
		windSkew(0,0) = windSkew(2, 2) = 1.0f;

		windSkew(0,1) = sx;
		windSkew(1,1) = invSize;
		windSkew(2,1) = sz;

		float y = boundingCenter.Y();
		windSkew.SetPosition(Vector3(sx*y, invSize*y - y, sz*y));

		origin = origin * windSkew;
	}
	else
	{
    Matrix3Val ori = origin.Orientation();
    float t;

    t = ori(1, 0);
		origin(0, 0) = ori(0, 0) + sx * t;
		origin(1, 0) = invSize * t;
		origin(2, 0) = sz * t + ori(2, 0);

    t = ori(1, 1);
		origin(0, 1) = ori(0, 1) + sx * t;
		origin(1, 1) = invSize * t;
		origin(2, 1) = sz * t + ori(2, 1);

    t = ori(1, 2);
		origin(0, 2) = ori(0, 2) + sx * t;
		origin(1, 2) = invSize * t;
		origin(2, 2) = sz * t + ori(2, 2);

		float y = origin.DirectionAside().Y() * boundingCenter.X() + origin.DirectionUp().Y() * boundingCenter.Y() + origin.Direction().Y() * boundingCenter.Z();
    Vector3Val pos = origin.Position();
		origin.SetPosition(Vector3(pos.X() + sx * y, pos.Y() + invSize * y - y, pos.Z() + sz * y));
	}
}


float WindObjectType::EnlargeClippingInfo(const Matrix4 &origin, Vector3 *minmax, float sphere) const
{
  const WindEmitterList &emitorList = GWorld->GetWindEmitterScope();
  float windSize = emitorList.GetWindSlowSize();
	// Calculate the skew matrix
	float maxSkew = windSize*_maxSkewFactor;

  float factor = 1+maxSkew*2; // we are moving the bounding center, which is close to bottom, therefore the upper part may be moving 2x as much

  // when moving more wildly then 5 %, we do not care, nobody will notice anyway
  // 1.05 was used as a constant for very long time with no issue, the point of this function is to optimize
  // the common case of smaller values
  saturateMin(factor,1.05f);

  minmax[0][0] *= factor;
  minmax[0][2] *= factor;
  minmax[1][0] *= factor;
  minmax[1][2] *= factor;
  return sphere*factor;
}

#if defined(XBOX) && _RELEASE
// PGO causes some strange bug in the tree wind animation code - disable the animation until fixed
# define ENABLE_TREE_WIND 0
#else
# define ENABLE_TREE_WIND 1
#endif


DEFINE_FAST_ALLOCATOR(ObjectWindAnim)

ObjectWindAnim::ObjectWindAnim(
  LODShape *shape, const CreateObjectId &id
)
:base(shape,id)
{
}
ObjectWindAnim::~ObjectWindAnim()
{
}

#if _ENABLE_CHEATS
  bool ObjectWindAnim::CheckDrawToggle() const
  {
    return CHECK_OBJ_ENABLE(ODETree);
  }
  float ObjectWindAnim::CheckDrawScale() const
  {
    return ObjDrawScale[ODETree];
  }
#endif

void ObjectWindAnim::Draw(
  int cb, int forceLOD, ClipFlags clipFlags, const DrawParameters &dp,
  const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
  if (!CHECK_OBJ_ENABLE(ODETree)) return;

  ADD_COUNTER(tree,1);
  // Include wind animation into instance origin
  #if ENABLE_TREE_WIND
  Matrix4 posA = pos;
  if (!dp._sbIsBeingRendered)
  {
    WindObjectType type = GetWindType();
    type.AnimateOrigin(posA,-GetObjectId().Encode(),GetShape()->BoundingCenter());
  }
  #else
  Matrix4Val posA = pos;
  #endif

  // Run the base drawing
#if _HELISIM_RENDER_TO_TEXTURE
  base::Draw(cb,forceLOD, clipFlags, dp, ip, dist2, FrameBase(posA, false), oi, drawListId);
#else
  base::Draw(cb,forceLOD, clipFlags, dp, ip, dist2, FrameBase(posA, false), oi);
#endif
}

//#define TRACE_OBJECT_LOD 1

#ifdef TRACE_OBJECT_LOD
static int LastLod = -1;
#endif

void ObjectWindAnim::DrawSimpleInstanced(
  int cb, int forceLOD, ClipFlags clipFlags, const DrawParameters &dp,
  ObjectInstanceInfo *instances, int instanceCount,
  float dist2, const LightList &lights, SortObject *oi, float coveredArea
#if _HELISIM_RENDER_TO_TEXTURE
  , int drawListId
#endif
)
{
#if _ENABLE_CHEATS
  if (!CheckDrawToggle()) return;
#endif

  ADD_COUNTER(tree,instanceCount);
  // Include wind animation into instance origin
  // trees are animated only a small amount
  // for a tree tall 10 pixels we assume the animation will be 1 pixel at most 
  // TODOX360: consider implementing animation fully on GPU, including matrix computations
  #if ENABLE_TREE_WIND
  const float MinPixelAnimated = 20;
  
  if ((!dp._sbIsBeingRendered) && (coveredArea > Square(MinPixelAnimated)))
  {
    WindObjectType type = GetWindType();
    Vector3Val bcenter = GetShape()->BoundingCenter();
    for (int i=0; i<instanceCount; i++)
    {
      ObjectInstanceInfo &inst = instances[i];
      type.AnimateOrigin(inst._origin,-inst._object->GetObjectId().Encode(),bcenter);
#ifdef TRACE_OBJECT_LOD
      if (inst._object->ID().Encode() == 607217)
      {
        if (LastLod != forceLOD)
        {
          LogF("%d", forceLOD);
          LastLod = forceLOD;
        }
      }
#endif
    }
  }
  #endif

  // Run the base drawing
#if _HELISIM_RENDER_TO_TEXTURE
  base::DrawSimpleInstanced(cb,forceLOD,clipFlags,dp,instances,instanceCount,dist2,lights,oi,coveredArea, drawListId);
#else
  base::DrawSimpleInstanced(cb,forceLOD,clipFlags,dp,instances,instanceCount,dist2,lights,oi,coveredArea);
#endif
}

float ObjectWindAnim::DensityRatio() const
{
  return TreeDensity;
}

float ObjectWindAnim::GetCollisionRadius(const ObjectVisualState &vs) const 
{ 
	return base::GetCollisionRadius(vs)*1.05f; 
} 

float ObjectWindAnim::ClippingInfo(const ObjectVisualState &vs, Vector3 *minMax, ClippingType clip) const
{
  float ret = base::ClippingInfo(vs,minMax,clip);
  ret = GetWindType().EnlargeClippingInfo(vs,minMax,ret);
  return ret;
}


DEFINE_FAST_ALLOCATOR(ObjectColored)

ObjectColored::ObjectColored
(
  LODShape *shape, const CreateObjectId &id
)
:base(shape,id)
{
  _constantColor = PackedWhite;
  _specialOr = 0;
  // make sure pushbuffers are never used 
  shape->DisablePushBuffer();
}

ObjectColored::~ObjectColored()
{
}

ColorVal ObjectColored::GetConstantColor() const
{
  static const Color halfOpaqueWhite(Color(1,1,1,0.5f));
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DETransparent)) return halfOpaqueWhite;
#endif
  return _constantColor;
}

bool ObjectColored::CanBeInstanced(int level, float distance2, const DrawParameters &dp) const
{
  // instancing currently does not handle coloring
  // because CanBeInstancedTogether does not check individual CanBeInstanced
  // we need to prevent instancing even for white color objects
  return false;
}



Object::LinkId Object::GetLinkId() const
{
  if (!_objId.IsObject())
  {
    // using ID for non-primary objects is dangerous. Some non-primary objects (temporary entities) have no ID at all
    // as non-primary objects are not discardable, we want to always perform direct "object pointer" comparison for them
    return ObjectId();
  }
  return _objId;
}

void Object::LockLink() const
{
  // lock given object rectangle if necessary
  if (!_objId.IsObject()) return;
  // check which rectangle is this object in
  int x = _objId.GetObjX();
  int z = _objId.GetObjZ();
  GLandscape->AddRefObjectList(x,z);
}

void Object::UnlockLinkPrepare(UnlockLinkContext &context) const
{
  context.locked = false;
  if (!_objId.IsObject()) return;
  // check which rectangle is this object in
  int x = _objId.GetObjX();
  int z = _objId.GetObjZ();
  // make sure the rectangle is locked and the object is still in the rectangle
  #if _ENABLE_REPORT
    const ObjectList &list = GLandscape->GetObjects(z,x);
    if (!list.GetList())
    {
      ErrF("Locked link into no list: %s",(const char *)GetDebugName());
      return;
    }
    DoAssert(!GLandscape->IsStreaming() || list.GetList()->IsUsed());
    bool found = false;
    for (int i=0; i<list.Size(); i++)
    {
      Object *obj = list[i];
      if (obj==this) found = true;
    }
    DoAssert(found);
    if (!found) return;
  #endif
  context.locked = true;
  context.x = x;
  context.z = z;
}
void Object::UnlockLinkFinish(UnlockLinkContext &context)
{
  if (!context.locked) return;
  GLandscape->ReleaseObjectList(context.x,context.z);
}


void Object::UnlockLink() const
{
  UnlockLinkContext context;
  UnlockLinkPrepare(context);
  UnlockLinkFinish(context);
}

bool Object::NeedsRestoring() const
{
  return _objId.IsObject();
}

Object *Object::RestoreLink(const LinkId &id)
{
  Assert(id!=GetNullId());
  Assert (id.IsObject());
  return GLandscape->GetObjectLocked(id);
}

bool Object::RequestRestoreLink(const LinkId &id, bool noRequest)
{
  /// ObjectId
  Assert(id!=GetNullId());
  Assert (id.IsObject());
  int x = id.GetObjX();
  int z = id.GetObjZ();
  if (!noRequest)
    return GLandscape->PreloadObjects(FileRequestPriority(100),x,z)!=Landscape::PreloadRequested;
  else
    return GLandscape->ObjectsReady(x,z);
}

float Object::UpdateDistance() const
{
  return 1000;
}

int MatrixCache::Find(const Object *obj, int level, const Object *parentObject) const
{
  MatrixCacheEntry ce(obj, level, parentObject);

  int l = 0; // left bound
  int r = _entries.Size() - 1; // right bound
  // binary search
  // TODO: hash-map might be faster here?
  while (r >= l)
  {
    int i = (l + r) / 2;
    if (_entries[i] > ce)
      r = i - 1;
    else if (_entries[i] == ce)
      return i;
    else
      l = i + 1;
  }

  // not found
  return -1;
}

bool MatrixCache::Insert(const Object* obj, int level, const Object *parentObject, int* index)
{
  // operations not MT safe
  AssertMainThread();
  
  MatrixCacheEntry ce(obj, level, parentObject);

  int p = 0;
  int r = _entries.Size() - 1;
  int i;
  // binary search
  // TODO: hash-map might be faster here?
  for(;r >= p;)
  {
    i = (p + r) / 2;
    if(_entries[i] > ce)
    {
      r = i - 1;
    }
    else
    {
      if(_entries[i] == ce)
      {
        if (index) *index = i;
        _cacheReused++;
        return false;
      }
      p = i + 1;
    }
  }

  _entries.Insert(p, ce);
  if (index) *index = p;
  _cacheMissed++;
  return true;
}


ObjectVisualState::ObjectVisualState()
{
}


/** matrices expected to be orthonormal (rotation only) */
Matrix3 ObjectVisualState::Nlerp(Matrix3 const& t0, Matrix3 const& t1, float t)
{
  Matrix3 interpolated; // always used to that NRVO can be applied
  if (t0 != t1)
  {
    Quaternion<float> q0(t0), q1(t1);  // convert to quaternions, lerp, normalize, convert back to matrices
    q0.Nlerp(q1,t).UnitToMatrix(interpolated);
    return interpolated;
  }
  else
  {  // copy closer state
    interpolated = t1;
    return interpolated;
  }

}

/** matrices expected to be orthonormal (rotation + translation only) */
Matrix4 ObjectVisualState::Nlerp(Matrix4 const& t0, Matrix4 const& t1, float t)
{
  Matrix4 interpolated; // always used to that NRVO can be applied
  interpolated.SetPosition(t0.Position()*t+ t1.Position() *(1-t));  // no equality test - it's slower
  if (t0.Orientation() != t1.Orientation())
  {
    Quaternion<float> q0(t0.Orientation()), q1(t1.Orientation());  // convert to quaternions, lerp, normalize, convert back to matrices
    Matrix3 interpolated3;
    q0.Nlerp(q1,t).UnitToMatrix(interpolated3);
    interpolated.SetOrientation(interpolated3);
    return interpolated;
  }
  else
  {  // copy closer state
    interpolated.SetOrientation(t1.Orientation());
    return interpolated;
  }
}

void ObjectVisualState::Interpolate( Object *obj, ObjectVisualState const& t1state, float t, ObjectVisualState& interpolatedResult ) const
{
  // Interpolate position.
  interpolatedResult.SetPosition(Position() + (t1state.Position() - Position()) * t);  // no equality test - it's slower

  // Interpolate rotation.
  // Don't bother with skewed transformations - just use the closer state.
  if (Orientation() != t1state.Orientation())
  {
    if (GetOrthonormal() && t1state.GetOrthonormal())
    {
      Quaternion<float> q0(Orientation()), q1(t1state.Orientation());  // convert to quaternions, lerp, normalize, convert back to matrices
      Matrix3 interpolated;
      q0.Nlerp(q1,t).UnitToMatrix(interpolated);
#if _DEBUG
      Matrix3 interpolatedS;
      q0.Slerp(q1,t).UnitToMatrix(interpolatedS);
      Assert(interpolatedS.Distance2(interpolated)<Square(0.25f));
      Assert(interpolated.Distance2(Orientation())<=t1state.Orientation().Distance2(Orientation())*1.2f+0.01f);
#endif
      interpolatedResult.SetOrientation(interpolated);
      interpolatedResult._scale = 1.0f;
      interpolatedResult._maxScale = -1.0f;  // no scale/skew
    }
    else  // try to decompose the matrix to (rotation after non-uniform scale) and interpolate these separately
    {
      Matrix3 ortho0(MDirection, Direction(), DirectionUp());
      Matrix3 ortho1(MDirection, t1state.Direction(), t1state.DirectionUp());

      // do the unscaled matrices represent a rotation? === Is dot(orthogonalized,normalized) > 1-epsilon ?
      if (ortho0.DirectionAside() * DirectionAside().Normalized()         < 0.999 ||
          ortho0.DirectionUp()    * DirectionUp().Normalized()            < 0.999 ||
          ortho1.DirectionAside() * t1state.DirectionAside().Normalized() < 0.999 ||
          ortho1.DirectionUp()    * t1state.DirectionUp().Normalized()    < 0.999) 
      {
        // no - decomposition failed: use the closer state
        if (t>0.5)
        {
          interpolatedResult.SetOrientation(t1state.Orientation());
          interpolatedResult._scale = t1state._scale;
          interpolatedResult._maxScale = t1state._maxScale;
        }
        else
        {
          interpolatedResult.SetOrientation(Orientation());
          interpolatedResult._scale = _scale;
          interpolatedResult._maxScale = _maxScale;
        }
      }

      Quaternion<float> q0(ortho0), q1(ortho1);  // unscale, convert to quaternions, lerp, normalize, convert back to matrices, scale back
      Matrix3 interpolated;
      q0.Nlerp(q1,t).UnitToMatrix(interpolated);

      // put interpolated scale back in
      float sx = Lerp(DirectionAside().Size(), t1state.DirectionAside().Size(), t);
      float sy = Lerp(DirectionUp().Size(),    t1state.DirectionUp().Size(),    t);
      float sz = Lerp(Direction().Size(),      t1state.Direction().Size(),      t);
      interpolatedResult.SetDirectionAside(interpolated.DirectionAside() * sx);  
      interpolatedResult.SetDirectionUp(interpolated.DirectionUp() * sy);
      interpolatedResult.SetDirection(interpolated.Direction() * sz);
      interpolatedResult._scale = interpolatedResult._maxScale = floatMax(sx,sy,sz);
    }
  }
  else  // states are equal
  {
    interpolatedResult.SetOrientation(Orientation());
    interpolatedResult._scale = _scale;
    interpolatedResult._maxScale = _maxScale;
  }
}

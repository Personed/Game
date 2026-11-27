/*!
  \file
  Implementation of Entity
*/

#include "wpch.hpp"
#include "vehicle.hpp"
#include "world.hpp"
#include "global.hpp"
#include "landscape.hpp"
#include "scene.hpp"
#include "camera.hpp"
#include "animation.hpp"
#include "AI/ai.hpp"
#include "allVehicles.hpp"

#include "Network/network.hpp"
#include <El/Common/randomGen.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <Es/Common/delegate.hpp>
#include <Es/Files/filenames.hpp>
#include <Es/ReportStack/reportStack.hpp>
#include <Es/ErrorProp/errorProp.hpp>
#include "gameStateExt.hpp"
#include "paramArchiveExt.hpp"
#include "keyInput.hpp"
#include "Shape/specLods.hpp"
#include "tlVertex.hpp"
#include "diagModes.hpp"
#include "fileLocator.hpp"
#include "rtAnimation.hpp"
#include "objDestroy.hpp"

#include "engine.hpp"

#include "progress.hpp"

#include "fsmEntity.hpp"

#include <El/SimpleExpression/simpleExpression.hpp>
#include <El/Evaluator/express.hpp>
#include "dikCodes.h"

#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "Physics\enf_physicsbullet.h"

#if _PROFILE && USER_Ondra
#pragma optimize("",off)
#endif

#if _DEBUG
  #define ARROWS 0
#endif

extern bool GReplaceProxies;

const float DefMinHit = 0.01;

HitPoint::HitPoint()
{
  _selection=-1; // selection index (in Hitpoints LOD)
  _armor=10; // hit point armor
  _indexCC.Clear();
  _material = -1;
  _passThrough = 1;
  _minHit = DefMinHit;
  _invArmor = 1/_armor;
}

HitPoint::HitPoint(LODShape *lodShape, const Shape *shape, const char *name, const char *altName, float armor, int material, Ref<WoundInfo> wound)
{
  _selection=shape ? lodShape->FindNamedSel(shape,name,altName) : -1; 
  _armor=armor;
  _indexCC.Clear();
  _material = material;
  _passThrough = 1;
  _minHit = DefMinHit;
  _invArmor = 1/_armor;
  _visual.Init(lodShape,wound,name,altName);
}

HitPoint::HitPoint(LODShape *lodShape, const Shape *shape, ParamEntryPar par, float armor, Ref<WoundInfo> wound)
{
  _name = par.GetName();

  RStringB selName = par>>"name";
  RStringB visName = RStringB();

  if (par.FindEntry("visual"))
    visName = par>>"visual";
  _selection=shape ? lodShape->FindNamedSel(shape,selName) : -1; 
  _armor= par>>"armor";
  _armor *= armor;
  _material = par>>"material";
  _passThrough = par>>"passThrough";
  _minHit = par.ReadValue("minimalHit",DefMinHit);

  _visual.Init(lodShape,wound,visName,NULL);

  _indexCC.Clear();
  ConstParamEntryPtr entry = par.FindEntry("convexComponent");
  if (entry)
  {
    RString ccName = *entry;
    lodShape->FindHitComponents(_indexCC, ccName);
  }

  _invArmor = 1/_armor;
}

void HitPoint::CompileDepends(RString expression, const Array<HitPoint> &hitpoints)
{
  // create a variable name list (total damage + all hitpoints)
  AutoArray<RString, MemAllocLocal<RString,128> > vars;
  vars.Add("Total");
  for (int i=0; i<hitpoints.Size(); i++)
    vars.Add(hitpoints[i].GetName());
  if (!_depends.Compile(expression,vars.Data(),vars.Size()))
    RptF("Error compiling '%s' in '%s'",cc_cast(expression),cc_cast(GetName()));
}

float HitPoint::EvaluateDepends(float totalDamage, const Array<float> &hits) const
{
  // create a variable value list (total damage + all hitpoints)
  float res = 0;
  AutoArray<float, MemAllocLocal<float,128> > vars;
  vars.Add(totalDamage);
  vars.Merge(hits);
  _depends.Evaluate(res,vars.Data(),vars.Size());
  // if there is any error, we can hardly do anything with it
  return res;
}

/**
@return false once all running animations are done
This function is used as a helper for external animation for entities,
which do not have a simulation of their own.
*/
bool Entity::SimulateAnimations(float deltaT, SimulationImportance prec)
{
  bool notDone = false; // if all animations are done, terminate animator
  // TODO: create a separate list of the animations which need to be simulated
  const EntityType *type = Type();
  for (int i=0; i<type->_animSources.Size(); i++)
  {
    AnimationSource *animSource = type->_animSources[i];
    if (animSource->Simulate(this,deltaT))
      notDone = true; // animation is not done, continue with animator
  }  
  CalculateBoundingInfo();
  return notDone;
}

void Entity::CalculateBoundingInfo()
{
  const EntityType *type = Type();
  // recalculate bounding sphere / box if needed
  if (type->_bounding>=0)
  {
    const NamedSelection::Access bounding(_shape->MemoryLevel()->NamedSel(type->_bounding),_shape,_shape->MemoryLevel());
    // animate all points in the bounding
    // note: if they are many, it might be better to prepare a skeleton for it
    if (bounding.Size()>0)
    {
      int vi = bounding[0];
      Vector3 point = AnimatePoint(FutureVisualState(),_shape->FindMemoryLevel(),vi);
      float boundingSphere2 = point.SquareSize();
      _minmax[0] = _minmax[1] = point;
      for (int i=1; i<bounding.Size(); i++)
      {
        int vi = bounding[i];
        Vector3 point = AnimatePoint(FutureVisualState(),_shape->FindMemoryLevel(),vi);
        CheckMinMaxInline(_minmax[0],_minmax[1],point);
        saturateMax(boundingSphere2,point.SquareSize());
        
      }
      const float safety = 0.05f;
      _boundingSphere = sqrt(boundingSphere2)*(1+safety);
      Vector3 minmaxSafety = (_minmax[1]-_minmax[0])*(safety*0.5f);
      _minmax[0] -= minmaxSafety;
      _minmax[1] += minmaxSafety;
    }
    else
    {
      _minmax[0] = _minmax[1] = VZero;
      _boundingSphere = 0;
    }
  }

}

EntityAnimator::EntityAnimator(Entity *actor)
:base(NULL,GWorld->Preloaded(ETypeAnimator),CreateObjectId(GLandscape->NewObjectID())),
_actor(actor)
{
  SetTimeOffset(0);
}

EntityAnimator::~EntityAnimator()
{
}

void EntityAnimator::Simulate(float deltaT, SimulationImportance prec)
{
  if (_actor)
  {
    if (!_actor->SimulateAnimations(deltaT,prec))
    {
      _actor->OnAnimatorTerminated();
      SetDelete();
      _actor = NULL;
    }
  }
  else
    SetDelete();
}

LSError EntityAnimator::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))
	CHECK(ar.SerializeRef("active", _actor, 0))
	return LSOK;
}

void Entity::Simulate(float deltaT, SimulationImportance prec)
{
  const EntityType *type = Type();

  // animate textures
  for (int i=0; i<_animateTexturesTimes.Size(); i++)
  {
    float &phase = _animateTexturesTimes[i];
    phase += deltaT * type->_animateTextures[i].animSpeed;
    while (phase >= 1.0f) phase -= 1.0f;
  }

  if (!IsAnimatorRequired())
    SimulateAnimations(deltaT,prec); 
  
  if (_ambient && !ToDelete())
  {
    if (!_ambient->CheckInArea(*GScene->GetCamera(),this))
    {
      // we have left the area - delete ASAP
      SetDelete();
      // SetDelete calls OnEntityLeft
    }
  }
}

#if 0 // _PROFILE
#pragma optimize("",off)
#endif

Matrix4 Entity::GetTransformFromRemoteState(Matrix3Par orient, Vector3Par pos, const VisualState &vs) const
{
  #if _VBS3 // handle clipLandKeep objects
  bool clipLandKeep = 
      Static() && GetShape() && (GetShape()->GetOrHints()&(ClipLandKeep|ClipLandOn)
      || GetShape()->PropertyValue(RSB(placement))==RSB(slope)
      || GetShape()->PropertyValue(RSB(placement))==RSB(vertical)
      || Type()->_placement==PlacementVertical);
  #else
  const bool clipLandKeep = false;
  #endif
  // if the orientation is skewed or scaled, we cannot transfer it. Sender sent M3Zero instead in such case.
  // if we received a valid matrix, we should set it - if transmitter sent it, we have to obey
  // this would allow changing a "bad" matrix into a "good" one
  Matrix4 trans;
  trans.SetPosition(pos+HideOffset());
  if (!Static() || orient.Distance2(M3Zero) > Square(0.1f))
  {
    trans.SetOrientation(orient);
  }
  else
  {
    trans.SetOrientation(vs.Orientation());
  }
  if (clipLandKeep)
  {
    // ClipLandKeep objects up must point up in world space
    trans.SetUpAndDirection(VUp,trans.Direction());
    InitSkew(GLandscape,trans);
  }
  return trans;
}

void Entity::ApplyRemoteState(float deltaT, VisualState &vs)
{
  const bool defSmoothing = true;
  #if _ENABLE_CHEATS
  static bool smoothing = defSmoothing;
  if (GInput.GetCheat3ToDo(DIK_Q))
  {
    smoothing = !smoothing;
    DIAG_MESSAGE(1000, "MP interpolation %s", smoothing? "On" : "Off");
  }
  #else
  const bool smoothing = defSmoothing;
  #endif
  if (_remoteState)
  {
    DoAssert(GLandscape->CheckIntegrity());
    bool remoteStateExhausted = false;
    // avoid fighting of position updates with destruction animation
    if (!IsDestroyingByPos())
    {
      Matrix3 orientation = _remoteState->orient;
      Vector3 speed = vs.Speed()+_remoteState->speedOffset;
      Vector3 position = vs.Position()+_remoteState->posOffset;

      static float maxRelChange = 0.33f;
      static float maxAbsChange = 1.0f;
      float speedSize = vs._speed.Size()*deltaT;
      float maxPosChange = (speedSize*maxRelChange+maxAbsChange)*deltaT;
      static float maxRelCont = 20.0f;
      static float maxAbsCont = 10.0f;
      const float maxContMove = speedSize*maxRelCont+maxAbsCont; // When movement is too fast, it is better to jump then to interpolate
      const float maxVertDiff = 1.f;  // mainly for zombies
      const float maxHorDiff = 1.f;  // mainly for zombies
      if (IsLocal())
      {
        // object has become local - apply the last update and discard it
        position = _remoteState->pos;
        orientation = _remoteState->orient;
        speed = _remoteState->speed;
        remoteStateExhausted = true;
      }
      else if (_remoteState->posOffset.SquareSize()>Square(maxContMove) || 
              (dynamic_cast<const LightAI*>(this) && fabs(_remoteState->posOffset.Y()) > maxVertDiff) && (_remoteState->posOffset.SquareSizeXZ() < maxHorDiff))
      {
        DoAssert(GLandscape->CheckIntegrity());
        #if _PROFILE
          LogF(
            "**** %s %s: distance %.1f, dist forward %.1f, max cont %.1f",
            cc_cast(GetEntityType()->GetName()),cc_cast(GetDebugName()),
            position.Distance(vs.Position()),
            (position-vs.Position()).DotProduct(vs._speed.Normalized()),
            maxContMove
          );
          LogF("     pos sim %.2f, %.2f ex %.2f,%.2f, rem %.2f, %.2f",vs.Position().X(),vs.Position().Z(),position.X(),position.Z(),_remoteState->pos.X(),_remoteState->pos.Z());
        #endif
        VisualCut();
        static bool doIt = false;
        if (doIt || IsLocal())
        {
          position = _remoteState->pos; // when cutting, do not extrapolate
          orientation = _remoteState->orient;
          speed = _remoteState->speed;
          remoteStateExhausted = true;
        }
        DoAssert(GLandscape->CheckIntegrity());
        _remoteState->posOffset = VZero;
        _remoteState->speedOffset = VZero;
      }
      else if (_objectContact)
      {
        #if _PROFILE
          LogF(
            "**** %s %s: contact, dist %.1f",
            cc_cast(GetEntityType()->GetName()),cc_cast(GetDebugName()),
            _remoteState->pos.Distance(vs.Position())
          );
          LogF("     pos sim %.2f, %.2f ex %.2f,%.2f, rem %.2f, %.2f",vs.Position().X(),vs.Position().Z(),position.X(),position.Z(),_remoteState->pos.X(),_remoteState->pos.Z());
        #endif
        // local simulation has reported a collision - we need to obey the remote one
        // this can cause a jump, but we prefer it from being a lot inaccurate
        position = _remoteState->pos; // extrapolated position may be in collision, do not extrapolate
        orientation = _remoteState->orient;
        speed = _remoteState->speed;
        _remoteState->posOffset = VZero;
        _remoteState->speedOffset = VZero;
        remoteStateExhausted = true;
      }
      else
      {
        if (smoothing)
        {
          // update position continuously
          float posChangeWanted = position.Distance(vs.Position());
          if (posChangeWanted>=maxPosChange)
          {
            Vector3 changeWanted = position-vs.Position();
            Vector3 posChange = changeWanted*maxPosChange;
            position = vs.Position()+posChange;
            _remoteState->posOffset -= posChange;
          }
          else
          {
            _remoteState->posOffset = VZero;
          }

          if (_remoteState->orient.Distance2(M3Zero) > Square(0.1f))
          {
            #if ORIENT_PREDICT
            Matrix3 orientChange = _remoteState->orientOffset;
            #else
            Matrix3 orientChange = _remoteState->orient-vs.Orientation();
            #endif
            float angScale2 = orientChange.Scale2();
            float maxAngVel = GetEntityType()->GetMaxRemoteAngleCorrection();
            const float maxAngChange = maxAngVel*deltaT;
            if (angScale2>Square(maxAngChange))
            {
              orientChange *= InvSqrt(angScale2)*maxAngChange;
              #if ORIENT_PREDICT
              _remoteState->orientOffset -= orientChange;
              #endif
            }
            else
            {
              #if ORIENT_PREDICT
              _remoteState->orientOffset = M3Zero;
              #endif
            }
            orientation = vs.Orientation()+orientChange;
            orientation.Orthogonalize();
          }

          //TOH change - used to be G_CONST*0.5f*deltaT; but this was to small for RTD/ARMA helicopter differences
          float maxSpeedChange = G_CONST*2.0f*deltaT;
          Vector3 spdDif = _remoteState->speedOffset;
          if (spdDif.SquareSize()>Square(maxSpeedChange))
          {
            spdDif = spdDif.Normalized()*maxSpeedChange;
            _remoteState->speedOffset -= spdDif;
          }
          else
          {
            _remoteState->speedOffset = VZero;
          }
          speed = vs._speed+spdDif;
        }
        else
        {
          remoteStateExhausted = true;
        }
      }

      Matrix4 trans = GetTransformFromRemoteState(orientation,position,vs);
      if (&vs==&FutureVisualState())
      {
        Move(trans);
      }
      else
      {
        vs.SetTransform(trans);
      }
      //_remoteState->posOffset = trans.Position();
      vs._speed = speed;
    }


    // TODO: verify how are we affecting angular velocity
    // TODO: adjust angular velocity smoothly
    SetAngVelocity(_remoteState->angVelocity,vs.Orientation());
    if (remoteStateExhausted)
    {
      DoAssert(GLandscape->CheckIntegrity());
      _remoteState.Free();
    }
    DoAssert(GLandscape->CheckIntegrity());
  }
}


void Entity::ApplyRemoteStateAdjustSpeed(float deltaT, VisualState &moveTrans)
{
  if (_remoteState)
  {
    // The locally controlled player predicts its own movement. Applying an
    // older server snapshot here runs after CreatePotentialState and replaces
    // the freshly computed step with the previous position (walk animation in
    // place). Spawn/teleport cuts are applied directly by TransferMsg.
    if (GetNetworkManager().IsClient() && GWorld && this == GWorld->GetRealPlayer())
    {
      _remoteState.Free();
      return;
    }
    ApplyRemoteState(deltaT,moveTrans);
    FutureVisualState()._speed = moveTrans._speed;
  }
}
void Entity::ApplyRemoteState(float deltaT)
{
  // TODO: consider: handling without passing FutureVisualState() directly could be cleaner
  ApplyRemoteState(deltaT,FutureVisualState());
}

void Entity::ResetRemoteState()
{
  // note: most oftenthere should be no remote state, as this function should be called on a local entity
  // TODO: consider: we might apply the remote state before discarding it
  _remoteState.Free();
  VisualCut();
}

#if 0 // _PROFILE
#pragma optimize("",on)
#endif


#if _ENABLE_ATTACHED_OBJECTS

bool Entity::AttachTo(Object *sObj, Vector3Par modelPos, int memIndex, int flags)
{
  bool changed = false;
  Entity *obj = dyn_cast<Entity>(sObj);

  if (sObj && !obj)
    RptF("Object %s: Cannot attach to %s - entity needed",cc_cast(obj->GetDebugName()),cc_cast(GetDebugName()));

  // even when attached to the same vehicle, memIndex and modelPos could change
  if (_attachedTo!=obj)
  {
    changed = true;
    // avoid cycles in attachments
    Entity *ptr = obj;
    while (ptr)
    {
      if (ptr == this)
        return changed;
      ptr = ptr->GetAttachedTo();
    }

    if(_attachedTo)
      Detach();//it's already attached to another vehicle
    if(!obj)
      return changed;

    _attachedTo = obj;
    _attachedTrans.SetIdentity();

    obj->AddAttached(this); // register it in the list of attached objects
  }
  if (_attachedMemPointIndex!=memIndex || _attachedOffset.Distance2(modelPos)>Square(0.01f)) changed = true;
  _attachedMemPointIndex = memIndex;   
  _attachedOffset = modelPos;       
  return changed;
}

void Entity::Detach()
{
  if (_attachedTo)
  {
    _attachedTo->RemoveAttached(this);
    _attachedTo = NULL;
  }
}

bool Entity::AreAttached(const Object *obj) const
{ 
   return (_disableCollisionWith == obj || obj->GetDisableCollisionWith() == this ||
     GetAttachedTo() == obj || obj->GetAttachedTo() == this);
}

void Entity::AddAttached(Entity *attached)
{
  if (attached)
    _attachedObjects.AddUnique(attached);
}

void Entity::RemoveAttached(Entity *attached)
{
  if (!attached)
    return;
  if (!_attachedObjects.Delete(attached))
    LogF("[Error] Attachment not found!");
}

void Entity::CalcAttachedPosition(float deltaT)
{
  if (!_attachedTo) return;

  ProtectedVisualState<const VisualState> vs = _attachedTo->FutureVisualStateScope();

  // inherit orientation from the parent
  Matrix4 transform = M4Identity;

  transform.SetOrientation(vs->Orientation() * _attachedTrans);
  // calculate the offset based on _attachedOffset and _attachedMemPointIndex
  Vector3 position = _attachedOffset;
  if (_attachedMemPointIndex >= 0)
  {
    const LODShape *shape = _attachedTo->GetShape();
    if (shape)
    {
      int level = shape->FindMemoryLevel();
      if (level >= 0)
        position += _attachedTo->AnimatePoint(*vs,level, _attachedMemPointIndex);
    }
  }
  transform.SetPosition(vs->PositionModelToWorld(position));

  // call hierarchically
  Move(transform);
  MoveAttached(deltaT);

  //only update the model speed for static attachments
  FutureVisualState()._speed = vs->Speed();
  DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);    
}


void Entity::MoveAttached(float deltaT)
{
  for (int i=0; i<_attachedObjects.Size(); i++)
  {
    Entity *obj = _attachedObjects[i];
    if (obj)
    {
      obj->SimulateStep(deltaT,GetLastImportance());
#ifdef TEST_JANA
      //obj->SimulateRagdoll();
#endif
      obj->CalcAttachedPosition(deltaT);
      obj->_simulationSkipped = 0; // attached keeps skipped synchronized
    }
  }
}

#endif // _ENABLE_ATTACHED_OBJECTS

void Entity::DrawActivities()
{
  for (int i=0; i<_activities.Size(); i++)
    _activities[i]->Draw(this);
}

bool Entity::IsSuspendedByActivity() const
{
  for (int i=0; i<_activities.Size(); i++)
  {
    if (_activities[i]->SuspendEntity(this))
      return true;
  }
  return false;
}

void Entity::SimulateActivities(float deltaT, SimulationImportance prec)
{
  int dst = 0;
  for (int i=0; i<_activities.Size(); )
  {
    if (_activities[i]->Simulate(this,deltaT,prec))
      _activities[dst++] = _activities[i++];
    else
      i++;
  }
  _activities.Resize(dst);
}

void Entity::RandomizeSimulationTime()
{
  // this is used when mission is starting
  // if we delay the simulation a little bit, no harm should be done
  _simulationSkipped = -GRandGen.RandomValue()*_simulationPrecision;
  _simulationAISkipped = -GRandGen.RandomValue()*_simulationPrecisionAI;
}


void Entity::SetTimeOffset(float val)
{
  _timeOffset = val;
  _maxTimeOffset = val;
  // TODO: modify _simulationSkipped and _simulationSkippedAI.
}

void Entity::SetSimulationPrecision(float val)
{
  _simulationPrecision=val;
}

void Entity::SetSimulationPrecisionAI(float val)
{
  _simulationPrecisionAI=val;
}

void Entity::SetSimulationPrecision(float val, float valAI)
{
  _simulationPrecision = val;
  _simulationPrecisionAI = valAI;
}

void Entity::SimulateAI(float deltaT, SimulationImportance prec)
{
  SimulateActivities(deltaT,prec);
}

void Entity::CleanUp()
{
}

void Entity::CleanUpMoveOut()
{
}

bool Entity::ObjRectangleReady(int xMin, int xMax, int zMin, int zMax) const
{
  // when we are inside of a camera grid, one quick test is all we need
  if (GWorld->CheckObjectsReady(xMin,xMax,zMin,zMax,true))
  {
    // lock not needed - reset it
    _objLock.ChangeRectangle(GridRectangle(0,0,0,0));
    return true;
  }
  // note: CheckObjectsReady was doing exactly the same extending/clamping
	int objRange = ObjRange;
  // clamp to a useful range only
	if( xMin<0 ) xMin=0;if( xMin>objRange-1 ) xMin=objRange-1;
	if( xMax<0 ) xMax=0;if( xMax>objRange-1 ) xMax=objRange-1;
	if( zMin<0 ) zMin=0;if( zMin>objRange-1 ) zMin=objRange-1;
	if( zMax<0 ) zMax=0;if( zMax>objRange-1 ) zMax=objRange-1;
	// convert end inclusive to end exclusive
	xMax++,zMax++;

  // use _objLock to track only what was changed
	_objLock.ChangeRectangle(GridRectangle(xMin,zMin,xMax,zMax));
	return _objLock.DoWork();
}

bool Entity::SimulationReady(SimulationImportance prec) const
{
  // TODO: adjust parameters for _objLock
  // all data near any entity need to be ready so that it can be simulated
  int xMin, xMax, zMin, zMax;
  float radius = GetRadius() * 2.0f + 5.0f;
  ObjRadiusRectangle(xMin, xMax, zMin, zMax, FutureVisualState().Position(), FutureVisualState().Position(), radius);
  bool ret = ObjRectangleReady(xMin, xMax, zMin, zMax);
  return ret;
}

void Entity::SimulateAIOptimized(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(ximA,sim);
  float step = SimulationPrecisionAI();
  _simulationAISkipped += deltaT;
  if (_simulationAISkipped>=step && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    PROFILE_SCOPE_EX(ximAO,sim);
    //if (PROFILE_SCOPE_NAME(ximAO).IsActive()) PROFILE_SCOPE_NAME(ximAO).AddMoreInfo(Format("%.3f",step));
    SimulateAI(step,prec);
    _simulationAISkipped -= step;
  }
}
void Entity::SimulateAIRest(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(ximA,sim);
  _simulationAISkipped += deltaT;
  if (_simulationAISkipped>0)
  {
    PROFILE_SCOPE_EX(ximAO,sim);
    //if (PROFILE_SCOPE_NAME(ximAO).IsActive()) PROFILE_SCOPE_NAME(ximAO).AddMoreInfo(Format("%.3f",_simulationAISkipped));
    SimulateAI(_simulationAISkipped,prec);
    _simulationAISkipped = 0;
  }
}


void Entity::SimulateStep(float step, SimulationImportance prec)
{
  PROFILE_SCOPE_EX(ximO,sim);
  //if (PROFILE_SCOPE_NAME(ximO).IsActive()) PROFILE_SCOPE_NAME(ximO).AddMoreInfo(Format("%.3f",step));
  SimulationReadyReset(FreeOnDemandFrameID());
  AgeVisualStates(step);
  _simulationSkipped -= step;
  _simulationNeeded = false;
  Simulate(step, prec);
}

void Entity::SimulateOptimizedAdaptive(float deltaT, SimulationImportance prec, float step)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(xim,sim);
  Assert(step<=SimulationPrecision());
  _simulationSkipped+=deltaT;
  Assert(step>0); // SimulateOptimized should be always called for step==0 
  if ((_simulationSkipped>=step || _simulationNeeded) && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    SimulateStep(step, prec);
  }
}

void Entity::SimulateOptimized(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(xim,sim);
  float step = SimulationPrecision();
  _simulationSkipped+=deltaT;
  if ((_simulationSkipped>=step || _simulationNeeded) && _simulationSkipped>0 && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    // precision == 0 means lock to the framerate
    SimulateStep(step>0 ? floatMin(_simulationSkipped,step) : _simulationSkipped, prec);
  }
}

void Entity::SimulateOptimizedOrRest(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(xim,sim);
  float step = SimulationPrecision();
  _simulationSkipped+=deltaT;
  if (_maxTimeOffset==0 && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    SimulateStep(_simulationSkipped, prec);
  }
  else if ((_simulationSkipped>=step|| _simulationNeeded) && _simulationSkipped>0 && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    // precision == 0 means lock to the framerate
    SimulateStep(step>0 ? floatMin(_simulationSkipped,step) : _simulationSkipped, prec);
  }
}


void Entity::SimulateRest(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(xim,sim);
  _simulationSkipped+=deltaT;
  if( _simulationSkipped>1e-4 && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    SimulateStep(_simulationSkipped, prec);
  }
}

void Entity::SimulateFastOptimized(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(xim,sim);
  float step = SimulationPrecision();
  _simulationSkipped+=deltaT;
  while (_simulationSkipped>=step && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    SimulateStep(step, prec);
    if (ToDelete()) break;  // if the entity stopped existing, we can no longer simulate it
    Assert(!ToMoveOut()); // shots should never move out
  }
}

void Entity::SimulateFastRest(float deltaT, SimulationImportance prec)
{
  if (_simulationDisabled) return;

  PROFILE_SCOPE_EX(xim,sim);
  float step = SimulationPrecision();
  _simulationSkipped+=deltaT;
  // 1e-4 is here to prevent numerical problems due to rounding errors (could be caused by too fine step)
  while( _simulationSkipped>1e-4f && SimulationReadyCached(prec,FreeOnDemandFrameID()))
  {
    SimulateStep(floatMin(step,_simulationSkipped), prec);
    if (ToDelete()) break;  // if the entity stopped existing, we can no longer simulate it
    Assert(!ToMoveOut()); // shots should never move out
  }
  // set to zero to avoid numerical precision errors
  _simulationSkipped = 0;
}

void Entity::SimulateRestWhenVisible(float deltaT, SimulationImportance prec)
{
  // consider - we might use actual frame rate here?
  const float bestStep = 1.0f/20;
  float maxStep = SimulationPrecision();
  Assert(prec>SimulateVisibleNear); // this simulation function should never be called for near entities
  // when not rendered, or when requiring more precision than is our "best" frame-rate, drive by the requirements only
  
#if _HELISIM_RENDER_TO_TEXTURE
  DoAssert(CheckMainThread());
  if (!GetInList(GScene->GetDrawObjectIndex()) || maxStep<bestStep)
#else
  if (!GetInList() || maxStep<bestStep)
#endif
  {
    SimulateOptimized(deltaT,prec);
  }
  else
  {
#if _HELISIM_RENDER_TO_TEXTURE
    DoAssert(CheckMainThread());
    // check visible area, adjust simulation fps accordingly
    float area = GetInList(GScene->GetDrawObjectIndex())->coveredArea;
#else
    // check visible area, adjust simulation fps accordingly
    float area = GetInList()->coveredArea;
#endif    
    
    float radius = GetRadius();
    // 2 is used to boost the effect of speed
    // note: deltaT cannot be used here, as only fractional delta is passed here
    float frameMovement = FutureVisualState().Speed().Size()*(_simulationSkipped+deltaT);
    if (frameMovement>radius*0.2f && radius>0)
    {
      // if we are moving fast, increase area accordingly
      float relSpeed = (radius+frameMovement)/radius;
      area *= Square(relSpeed);
    }
    
    // lowest precision - given by SimulationPrecision()
    // higher precision - each frame, between up to 20 fps
    const float minArea = 4*4;
    const float maxArea = 10*10;
    if (area>maxArea)
      // detailed view - full simulation required
      SimulateRest(deltaT,prec);
    else if (area<minArea)
      SimulateOptimized(deltaT,prec); // step = maxStep
    else
    {
      float step = maxStep-(area-minArea)/(maxArea-minArea)*(maxStep-bestStep);
      static bool logArea = false;
      if (logArea)
#if _HELISIM_RENDER_TO_TEXTURE
      {
        DoAssert(CheckMainThread());
        LogF("%s: area %g, step %.4f, dist %.1f", cc_cast(GetDebugName()), area, step, sqrt(GetInList(GScene->GetDrawObjectIndex())->distance2));
      }
#else
        LogF("%s: area %g, step %.4f, dist %.1f", cc_cast(GetDebugName()), area, step, sqrt(GetInList()->distance2));
#endif
      // interpolated precision - between 
      SimulateOptimizedAdaptive(deltaT,prec,step);
    }
  }
}

FFEffects::FFEffects()
{
  engineFreq=engineMag=0;
  surfaceMag = 0;
  // auto-centering forces
  stiffnessX = 0.3f;
  stiffnessY = 0.3f;
}

void Entity::PerformFF(FFEffects &effects)
{
  effects=FFEffects();
}

void Entity::ResetFF()
{
}

EntityPlainType::EntityPlainType(ParamEntryPar param)
:base(param)
{
  _scopeLevel = 1;
}

EntityPlainType::EntityPlainType(LODShape *shape)
:base(shape)
{
  //_scopeLevel = 1;
  Assert(_scopeLevel>0);
}

EntityPlainType::EntityPlainType(const char *shapeName)
:base(shapeName)
{
  _scopeLevel = 1;
}

EntityPlainType::~EntityPlainType()
{
}

Object* EntityPlainType::CreateObject(bool unused) const
{
  Object* v;

  if (!stricmp(_simName,"smokesource")) v = new SmokeSourceVehicle(Pars >> "CfgCloudlets" >> "DefaultSmoke");
  else if (!stricmp(_simName,"explosion")) v = new Explosion(GetShape(), RString());
  else if (!stricmp(_simName,"crater")) { EffectVariables evars(EVarSet_Bullet); v = new Crater(M4Identity, GetShape(), this, RString(), evars, 0.1); }
  else if (!stricmp(_simName,"crateronvehicle")) { EffectVariables evars(EVarSet_Bullet); v = new CraterOnVehicle(GetShape(), this, evars); }
  else if (!stricmp(_simName,"slop")) v = new Slop(GetShape(), this, M4Identity);
  else if (!stricmp(_simName,"lightpoint")) v = new LightPointEntity();
  else if (!stricmp(_simName,"smoke")) v = new Smoke(GetShape(), this, 0.5);
  else if (!stricmp(_simName,"camera")) v = new CameraVehicle(this);
  else if (!stricmp(_simName,"camconstruct")) v = new CameraConstruct(this);
  else if (!stricmp(_simName,"dynamicsound")) v = new DynSoundSource(NULL);
  else if (!stricmp(_simName,"objectdestructed")) v = new ObjectDestructed(GetShape());
  else if (!stricmp(_simName,"destructioneffects")) v = new DestructionEffects();
  else if (!stricmp(_simName,"alwaysshow")) v = new ObjectTyped(GetShape(), this, VISITOR_NO_ID, false);
  else if (!stricmp(_simName,"alwayshide")) v = new ObjectTyped(NULL, this, VISITOR_NO_ID, false);
  else if (!stricmp(_simName,"scud")) v = new ObjectTyped(GetShape(), this, VISITOR_NO_ID, false);
  else 
  {
    LogF("Unknown vehicle simulation %s: type %s (EntityPlainType called)",
      cc_cast(_simName), cc_cast(GetName())
      );
    v = NULL;
  }

  return v;
}



EntityPlainTypeNoProxy::EntityPlainTypeNoProxy(ParamEntryPar param)
:base(param)
{
}

EntityInternalType::EntityInternalType()
:base((LODShape *)NULL)
{
  _scopeLevel = 0;
  _refVehicles = 0;
}

EntityInternalType::~EntityInternalType()
{
}


Object* EntityInternalType::CreateObject(bool unused) const
{
  Object* v;

  if (!stricmp(_simName,"smokesource")) v = new SmokeSourceVehicle(Pars >> "CfgCloudlets" >> "DefaultSmoke");
  else if (!stricmp(_simName,"explosion")) v = new Explosion(GetShape(), RString());
  else if (!stricmp(_simName,"crater")) { EffectVariables evars(EVarSet_Bullet); v = new Crater(M4Identity, GetShape(), this, RString(), evars, 0.1); }
  else if (!stricmp(_simName,"crateronvehicle")) { EffectVariables evars(EVarSet_Bullet); v = new CraterOnVehicle(GetShape(), this, evars); }
  else if (!stricmp(_simName,"smoke")) v = new Smoke(GetShape(), this, 0.5);
  else if (!stricmp(_simName,"dynamicsound")) v = new DynSoundSource(NULL);
  else if (!stricmp(_simName,"objectdestructed")) v = new ObjectDestructed(GetShape());
  else if (!stricmp(_simName,"destructioneffects")) v = new DestructionEffects();
  else if (!stricmp(_simName,"animator")) v = new EntityAnimator(NULL);
  else if (!stricmp(_simName,"particlesource")) v = new CParticleSource(this);
  else if (!stricmp(_simName,"slop")) v = new Slop(GetShape(), this, M4Identity);  // necessary?
  else if (!stricmp(_simName,"lightpoint")) v = new LightPointEntity();  // necessary?
  // "#mark" (Mark) - footsteps
  // "#track" (TrackStep)
  // "#thunderbolt" (Thunderbolt)
  // "#tempdiag" (TempDiag),
  // "#soundonvehicle" (SoundOnVehicle)
  // "#preloaddamagedbuilding" (PreloadDamagedBuilding)
  // ...are only created directly and never saved (thus never created with NewNonAIVehicleQuiet() --> EntityInternalType::Create()).
  else 
  {
    LogF("Unknown vehicle simulation %s: type %s (EntityInternalType called)",
      cc_cast(_simName), cc_cast(GetName())
    );
    v = NULL;
  }

  return v;
}


int EntityPlainTypeNoProxy::GetProxyCountForLevel(int level) const
{
  return 0;
}

const ProxyObjectTyped &EntityPlainTypeNoProxy::GetProxyForLevel(int level, int i) const
{
  Fail("No proxy, cannot get");
  return *(ProxyObjectTyped *)NULL;
}

SkeletonIndex EntityPlainTypeNoProxy::GetProxyBoneForLevel(int level, int i) const
{
  Fail("No proxy, cannot get");
  return SkeletonIndexNone;
}


Object* EntityPlainTypeNoProxy::CreateObject(bool unused) const
{ 
  return new ObjectTyped(GetShape(), this, VISITOR_NO_ID, false);  // for _simname = "maverickweapon" 
}  



void ProxiesHolder::Init(LODShape *shape)
{
  if (shape)
    _proxies.Init(shape->NLevels());
}

void ProxiesHolder::Deinit(LODShape *shape)
{
  for (int i=0; i<int(_proxies.Length()); i++)
    _proxies[i].Clear();
}

static RString GetProxyType(RString name)
{
  const char *ext = strrchr(name, '\\');
  return RString("Proxy") + (ext ? RString(ext + 1) : name);
}

extern Object *NewObject( RString typeName, RString shapeName );

void ProxiesHolder::InitLevel(LODShape *lodShape, int level)
{
  ShapeUsed shape = lodShape->Level(level);
  // convert shape proxies to 
  _proxies[level].Realloc(shape->NProxies());
  _proxies[level].Resize(shape->NProxies());
  for (int p=0; p<shape->NProxies(); p++)
  {
    const ProxyObject &src = shape->Proxy(p);
    ProxyObjectTyped *dst = new ProxyObjectTyped;
    _proxies[level][p] = dst;
    dst->id = src.id;
    dst->name = src.name;
    dst->selection = src.selection;

    // create object based on source shape and position
    // create object from shape name
    // shape name may be name of some vehicle class
    Ref<Object> pobj;
    RString proxyShape = ::GetShapeName(src.name);
    if (GReplaceProxies)
    {
      RString proxyType = ::GetProxyType(src.name);
      pobj = ::NewObject(proxyType,proxyShape);
      if (!pobj)
      {
        // if ProxyType does not exist try to  find it like a land object
        Ref<LODShape> pshape = Shapes.New(proxyShape,false,true);
        pobj = ::NewObject(pshape,CreateObjectId());
        if (!pobj)
        {        
          RptF("Cannot create proxy object %s",cc_cast(src.name));
          continue;
        }
      }
    }
    else
    {
      Ref<LODShape> pshape = Shapes.New(proxyShape,false,true);
      pobj = ::NewObject(pshape,CreateObjectId());
    }

    // calculate original object bounding center
    pobj->SetTransform(src.trans);
    if (pobj->GetShape())
    {
      pobj->SetPosition(pobj->FastTransform(pobj->GetShape()->BoundingCenter()));
      //transobj->SetTransform(trans);
    }
    dst->obj = pobj;
    dst->obj->SetDestructType(DestructNo);
    dst->invTransform = src.invTransform;

    // we have adjusted the transform, we need to adjust inverse transform as well  
    dst->invTransform=pobj->FutureVisualState().GetInvTransform();
  }
}

void ProxiesHolder::DeinitLevel(LODShape *shape, int level)
{
  _proxies[level].Clear();
}

int ProxiesHolder::GetProxyCountForLevel(LODShape *lodShape, int level) const
{
  // make sure shape is lock
  // TODO: move proxy count into ShapeRefManaged
  Assert(!lodShape || lodShape->GetLevelLocked(level));
  if (size_t(level) >= _proxies.Length())
    return 0;
  return _proxies[level].Size();
}

const ProxyObjectTyped &ProxiesHolder::GetProxyForLevel(LODShape *lodShape, int level, int i) const
{
  Assert(!lodShape || lodShape->GetLevelLocked(level));
  return *(_proxies[level][i]);
}

SkeletonIndex ProxiesHolder::GetProxyBoneForLevel(LODShape *lodShape, int level, int i) const
{
  // check corresponding proxy of given shape
  const Shape *shape = lodShape->GetLevelLocked(level);
  return shape->Proxy(i).boneIndex;
}

int ProxiesHolder::GetProxySectionForLevel(LODShape *lodShape, int level, int i) const
{
  // check corresponding proxy of given shape
  const Shape *shape = lodShape->GetLevelLocked(level);
  return shape->Proxy(i).section;
}

DEFINE_ENUM(EntityPlacement,Placement,ENT_PLACEMENT_ENUM)

EntityType::EntityType(ParamEntryPar param)
{
  _par = param.GetClassInterface();
  _name._className = _par->GetName();
  
  _refVehicles = 0;
  // 
  _shapeReversed = true;
  _shadowEnabled = true;
  _shapeAutoCentered = true;
  _useRoadwayForVehicles = false;
  _placement = PlacementDefault;
  _alwaysShow = false;
  _featureSize = 0;
  _scopeLevel = 0;
  _bounding = -1;
  _loadedFromCfg = false;
}

EntityType::EntityType(LODShape *shape)
{
  _shape = shape;
  _shapeWithHandler = shape;
  _par = ConstParamClassDeepPtr(); // explicit shape - no _par

  _refVehicles = 1; // pretend we are locked
  // 
  _shapeAnimated = AnimTypeNone;
  _shapeReversed = true;
  _shadowEnabled = true;
  _shapeAutoCentered = true;
  _useRoadwayForVehicles = false;
  _placement = PlacementDefault;
  _alwaysShow = false;
  _featureSize = 0;
  _scopeLevel = 1;
  // we need to call InitShape handler
  if (_shape) _shape->AddLoadHandler(this);
  _bounding = -1;
  _loadedFromCfg = false;
}

EntityType::EntityType(const char *shapeName)
{
  _name._className = RString();
  _name._shapeName = shapeName;
  _shape = NULL;
  _par = ConstParamClassDeepPtr();

  _refVehicles = 0; // explicit shape - no _par
  // 
  _shapeReversed = true;
  _shadowEnabled = true;
  _shapeAutoCentered = true;
  _useRoadwayForVehicles = false;
  _placement = PlacementDefault;
  _alwaysShow = false;
  _featureSize = 0;
  _scopeLevel = 0;
  _bounding = -1;
  _loadedFromCfg = false;
}

/**
Similar to EntityType::Load, but no config is needed.
*/
void EntityType::LoadInternal(const char *simName, const char *shapeName)
{
  _accuracy = 1.0f;

  _name._className = simName;
  _name._shapeName = shapeName;
  
  _shapeName = (_name._shapeName.GetLength() > 0) ? _name._shapeName : "";
  
#if _ENABLE_REPORT
    char lowName[256];
    strcpy(lowName,simName);
    strlwr(lowName);
    DoAssert(!strcmp(lowName,simName));
#endif
  _simName=simName+1;
  _alwaysShow = strcmp(_simName, "alwaysshow") == 0;

  _shapeReversed=false;
  _shadowEnabled = true;		
  _shapeAutoCentered = true;
  _shapeAnimated = AnimTypeNone; //AnimTypeSoftware;
  _useRoadwayForVehicles = false;
  _placement = PlacementDefault;
  _featureSize = 0;
  
  if (!AbstractOnly())
    _scopeLevel = 1;
  _bounding = -1;
}

void EntityType::Load(ParamEntryPar par, const char *shapeName)
{
  //base::Load(par,shape);

  _accuracy=1.0;

  _name._className=par.GetName();
  _name._shapeName=shapeName;
  
  if (_name._shapeName.GetLength()>0)
    _shapeName = _name._shapeName;
  else
  {
    RString model=par>>"model";
    _shapeName = model.GetLength()>0 ? ::GetShapeName(model) : RString();
  }
  
  _simName=par>>"simulation";

  _structuralDamageCoef = 1;
  ConstParamEntryPtr entry = par.FindEntry("armorStructural");
  if (entry)
  {
    float armorStructural = *entry;
    _structuralDamageCoef = armorStructural > 0 ? 1 / armorStructural : 1e10f;
  }

  ConstParamEntryPtr effects = par.FindEntry("DestructionEffects");
  _destructionEffects.Clear();
  if (effects)
  {
    Assert(effects->IsClass());

    for (ParamClass::Iterator<> i(effects->GetClassInterface()); i; ++i)
    {
      ParamEntryVal entry = *i;
      if (entry.IsClass())
      {
        int index = _destructionEffects.Add();
        _destructionEffects[index].Load(entry);
      }
    }
    _destructionEffects.Compact();
    _destructionEffectsValid = true;
  }
  else
    _destructionEffectsValid = false;
  
  _shapeReversed=false;
  _shadowEnabled = true;
  _shapeAutoCentered = true;
  _shapeAnimated = AnimTypeNone;//AnimTypeSoftware;
  _useRoadwayForVehicles = false;
  _placement = PlacementDefault;
  _featureSize = 0;
  if( par.FindEntry("featureSize") )
    _featureSize=par>>"featureSize";

  if( par.FindEntry("reversed") )
    _shapeReversed=par>>"reversed";
  if( par.FindEntry("shadow") )
    _shadowEnabled=par>>"shadow";
  if( par.FindEntry("autocenter") )
    _shapeAutoCentered=par>>"autocenter";
  if( par.FindEntry("animated") )
  {
    bool animated = par>>"animated";
    _shapeAnimated = animated ? AnimTypeSoftware : AnimTypeNone;
  }
  if( par.FindEntry("useRoadwayForVehicles") )
    _useRoadwayForVehicles = par>>"useRoadwayForVehicles";
  if( par.FindEntry("placement") )
  {
    RStringB placement = par>>"placement";
    _placement = GetEnumValue<EntityPlacement>(placement);
  }
  if (par.FindEntry("fsm"))
  {
    ParamEntryVal fsms = par >> "fsm";
    int n = fsms.GetSize();
    _fsm.Realloc(n);
    _fsm.Resize(n);
    for (int i=0; i<n; i++) 
    {
      FSMEntityType *fsm = FSMEntityTypes.New(FSMEntityTypeName(this,fsms[i]));
      _fsm[i] = fsm;
    }
  }

  // check if there is some parent
  const ParamClass *cls= par.GetClassInterface();
  if (cls)
  {
    const char *baseName=cls->GetBaseName();
    if (baseName && *baseName)
      // load parent type
      _parentType=VehicleTypes.New(baseName);
  }
  ConstParamEntryPtr scopeEntry = par.FindEntry("scope");
  if (AbstractOnly())
  {
    ERROR_TRY()
      int scope = scopeEntry ? *scopeEntry : 0;
      if (scope!=0)
        LogF("Simulation %s : type must be abstract",(const char *)par.GetContext());
    ERROR_CATCH_ANY(exc)
      RptF("Error: unable to load scope in %s", cc_cast(par.GetContext()));
    ERROR_END()
  }
  else
  {
    int scope = scopeEntry ? *scopeEntry : 1;
    if (scope>0)
      _scopeLevel = 1;
  }
  _bounding = -1;
#if _VBS3 //get new parameter MapUseRealSize
  _mapUseRealSize = par.ReadValue("MapUseRealSize", false);
#endif

  _loadedFromCfg = true;

  if (par.FindEntry("htMin"))
    _htMin = par >> "htMin";
  else 
    _loadedFromCfg = false;

  if (par.FindEntry("htMax"))
   _htMax = par >> "htMax"; 
  else 
    _loadedFromCfg = false;

  if (par.FindEntry("afMax"))
    _afMax = par >> "afMax";
  else 
    _loadedFromCfg = false;

  if (par.FindEntry("mfMax"))
    _mfMax = par >> "mfMax";
  else 
    _loadedFromCfg = false;

  if (par.FindEntry("mFact"))
    _mFact = par >> "mFact";
  else 
    _loadedFromCfg = false;

  if (par.FindEntry("tBody"))
  _tBody = par >> "tBody";
  else 
    _loadedFromCfg = false;

//   #if _ENABLE_REPORT
//   if (_loadedFromCfg)
//   {
//     LogF("*[TI] %s: htMin %g",cc_cast(_name._className),_htMin);
//     LogF("*[TI] %s: htMax %g",cc_cast(_name._className),_htMax);
//     LogF("*[TI] %s: afMax %g",cc_cast(_name._className),_afMax);
//     LogF("*[TI] %s: mfMax %g",cc_cast(_name._className),_mfMax);
//     LogF("*[TI] %s: mFact %g",cc_cast(_name._className),_mFact);
//     LogF("*[TI] %s: tBody %g",cc_cast(_name._className),_tBody);
//   }
//   #endif
}


Object* EntityType::CreateObject(bool unused) const
{
  Object* v;

  if (!stricmp(_simName,"particlesource")) v = new CParticleSource(this);
  else if (!stricmp(_simName,"animator")) v = new EntityAnimator(NULL);
  else if (!stricmp(_simName,"proxyinventory")) v = new ObjectTyped(GetShape(), this, VISITOR_NO_ID, false);
  else 
  {
    LogF("Unknown vehicle simulation %s: type %s (EntityType called)",
      cc_cast(_simName), cc_cast(GetName())
    );
    Assert(false);  // Please implement CreateObject(bool fullCreate) in newly created Entities.
    v = NULL;
  }

  return v;
}


void HitPoint::LoadExtPars(const Array<HitPoint> &hitpoints, ParamEntryPar par)
{
  ConstParamEntryPtr dep = par.FindEntry("depends");
  if (dep)
  {
    RStringB depends = *dep;
    CompileDepends(depends,hitpoints);
  }
  ConstParamEntryPtr effects = par.FindEntry("DestructionEffects");
  _destructionEffects.Clear();
  if (effects)
  {
    Assert(effects->IsClass());

    for (ParamClass::Iterator<> i(effects->GetClassInterface()); i; ++i)
    {
      ParamEntryVal entry = *i;
      if (entry.IsClass())
      {
        int index = _destructionEffects.Add();
        _destructionEffects[index].Load(entry);
      }
    }
    _destructionEffects.Compact();
  }
}

void DestructionEffects::ComputeDestructionEffectsPars(float &fuelAmount, float &fireIntensity, float &objSize, float &intensity, float &lifeTime, float &fireInterval, float &interval)
{
  intensity = 0;
  interval = 0;
  fireIntensity = 0;
  fireInterval = 0;
  lifeTime = 0;
  saturateMin(objSize, 10);
  saturateMin(fuelAmount, 1000);
  if (fuelAmount <= 0)
  {
    // random fire (-80 .. +20 l of fuel)
    fuelAmount = 100.0 * GRandGen.RandomValue() - 80.0;
  }
  if (fuelAmount > 0)
  {
    // fire depending on fuel amount
    fireIntensity = 0.3 * objSize + 0.005 * fuelAmount;
    intensity = 0.6 * objSize + 0.005 * fuelAmount;
    lifeTime = 60.0 * (0.1 * objSize + 0.001 * fuelAmount) * (0.5 + GRandGen.RandomValue());
  }
  else
  {
    fireIntensity = 0;
    intensity = 0.3 * objSize;
    lifeTime = 60.0 * (0.1 * objSize) * (0.5 + GRandGen.RandomValue());
  }
  fireInterval = 0.03 * fireIntensity;
  interval = 0.03 * intensity;
}


Entity *HitPoint::CreateDestructionEffects(Entity *parent, EntityAI *killer, bool doDamage) const
{
  // if there are none, do not create them
  if (_destructionEffects.Size()<=0) return NULL;
  float objSize;
  float fuelAmount;
  Vector3 defPosTmp;
  const Vector3 *defPos = NULL;
  // check size and location of the hitpoint component
  {
    const LODShape *shape = parent->GetShape();
    int hitLevel = shape->FindHitpoints();
    DoAssert(hitLevel>=0)
    if (hitLevel<=0) return NULL;
    const Shape *hitShape = shape->GetLevelLocked(hitLevel);
    const NamedSelection::Access hitSel(hitShape->NamedSel(_selection),shape,hitShape);
    // scan the selection so that we know its dimensions
    Vector3 hitMin(+FLT_MAX,+FLT_MAX,+FLT_MAX);
    Vector3 hitMax(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    for (int i=0; i<hitSel.Size(); i++)
    {
      int vi = hitSel[i];
      Vector3Val hitPos = hitShape->Pos(vi);
      SaturateMin(hitMin,hitPos);
      SaturateMax(hitMax,hitPos);
    }
    defPosTmp = (hitMin+hitMax)*0.5f;
    defPos = &defPosTmp;
    // approximate the radius from the bounding box
    objSize = hitMax.Distance(hitMin)*0.5f;
    // what if there is none?
    
    // should we check the component in such case?
    // in any case, prevent zero size here
    saturateMax(objSize,0.5f);
    
    // approximate the fuel amount based on relative size to the object
    fuelAmount = parent->GetFuelTotal()*floatMin(1,shape->GeometrySphere()>0 ? objSize/shape->GeometrySphere() : 1);
  }

  float intensity, interval, fireIntensity, fireInterval, lifeTime;
  DestructionEffects::ComputeDestructionEffectsPars(fuelAmount, fireIntensity, objSize, intensity, lifeTime, fireInterval, interval);
  return new DestructionEffects(_destructionEffects, killer, parent, intensity, interval, fireIntensity, fireInterval, lifeTime, defPos, doDamage);
}

int EntityType::AddHitPoint(ParamEntryPar par, float armor)
{
  int index = _hitPoints.Add(HitPoint(_shape, _shape->HitpointsLevel(), par, armor, _damageInfo));
  // now compile the dependency expression for the hitpoint
  _hitPoints[index].LoadExtPars(_hitPoints,par);
  return index;
}

int EntityType::AddHitPoint(const char *name, float armor, int material)
{
  int index = _hitPoints.Add(HitPoint(_shape, _shape->HitpointsLevel(), name, NULL, armor, material, _damageInfo));
  return index;
}

int EntityType::FindHitPoint(const char *name) const
{
  for (int i=0; i<_hitPoints.Size(); i++)
  {
    if (stricmp(name, _hitPoints[i].GetName()) == 0)
      return i;
  }
  RptF("class HitPoints::%s not found in %s", cc_cast(name), cc_cast(GetName()));
  return -1;
}

float EntityType::GetMaxRemoteAngleCorrection() const
{
  static float maxAngleSpeed = 0.4f;
  return maxAngleSpeed;
}

struct CheckNoHandler
{
  EntityType *_type;
  
  CheckNoHandler(EntityType *type):_type(type){}
  bool operator()(const LLink<LODShape> &shape, const ShapeBank::ContainerType *bank)
  {
    if (shape && shape->CheckLoadHandler(_type))
    {
      RptF("Handler for %s still present on %s",(const char *)_type->GetName(),(const char *)shape->GetName());
      Fail("Handler not deleted");
    }
    return false;
  }
};

EntityType::~EntityType()
{
  DoAssert(!VehicleTypes.Find(GetEntityTypeName()));
  Assert(_refVehicles==0 || _par.IsNull());
  if (_shapeWithHandler)
    _shapeWithHandler->RemoveLoadHandler(unconst_cast(this));
  else
  {
#if _DEBUG
    CheckNoHandler check(this);
    Shapes.ForEachF(check);
#endif
  }
}

bool EntityType::IsKindOf(const EntityType *predecessor) const
{
  const EntityType *cur = this;
  while (cur)
  {
    if (cur == predecessor)
      return true;
    cur = cur->_parentType;
  }
  return false;
}

bool EntityType::IsKindOf(const char *typeName) const
{
  const EntityType *cur = this;
  while (cur)
  {
    if (!strcmpi(cur->GetName(),typeName))
      return true;
    cur = cur->_parentType;
  }
  return false;
}

bool EntityType::IsPersistenceRequired() const
{
  return IsSimulationRequired() || IsSoundRequired();
}

bool EntityType::IsSimulationRequired() const
{
  return true;
}

bool EntityType::IsSoundRequired() const
{
  return true;
}

const ParamClass *EntityType::GetParamEntry() const
{
  return _par.GetPointer();
}

static RString ReplacesChars(RString shapeName, char c, char w)
{
  if (!strchr(shapeName,c)) return shapeName;
  shapeName.MakeMutable();
  char *str = shapeName.MutableData();
  while (*str)
  {
    if (*str==c)
      *str = w;
    str++;
  }
  return shapeName;
}

DEFINE_FAST_ALLOCATOR(ProxyObjectTyped)

ProxyObjectTyped::ProxyObjectTyped()
{
}

void EntityType::SetHardwareAnimation(ParamEntryPar skeleton)
{
  _shapeAnimated = AnimTypeHardware;
}

void EntityType::PrepareHardwareAnimation()
{
  if (_shapeAnimated==AnimTypeHardware)
  {
    // check if there is any bone in the skeleton
    // it has no sense creating an empty skeleton
    if (!_shape->GetSkeleton() || _shape->GetSkeleton()->NBones()==0)
    {
      LogF("Empty or no skeleton detected in %s",(const char *)GetName());
      //_shapeAnimated = AnimTypeNone;
      _shape->SetAnimationType(AnimTypeNone);
    }
  }
}

/// animations controlled via scripting
class AnimationSourceUserBase: public AnimationSource
{
  typedef AnimationSource base;
protected:
  int _dataIndex;
  
public:
  AnimationSourceUserBase(const RStringB &name, int dataIndex);
  virtual void SetPhaseWanted(Animated *obj, float phase);
  virtual int GetStateIndex() const {return _dataIndex;}
};

void AnimationSourceUserBase::SetPhaseWanted(Animated *obj, float phase)
{
  Assert(dynamic_cast<Entity *>(obj));
  Entity *entity = static_cast<Entity *>(obj);
  entity->SetAnimationInstancePhaseWanted(_dataIndex,phase);
  entity->OnAnimationStarted();
}

AnimationSourceUserBase::AnimationSourceUserBase(const RStringB &name, int dataIndex)
:base(name),_dataIndex(dataIndex)
{
}

/// animations controlled via scripting
class AnimationSourceUser: public AnimationSourceUserBase
{
  typedef AnimationSourceUserBase base;
protected:
  float _animSpeed;
  float _initPhase;
  
public:
  AnimationSourceUser(int dataIndex);
  AnimationSourceUser(int dataIndex, ParamEntryPar cfg);
  virtual void Init(Animated *obj);
  virtual float GetPhase(const ObjectVisualState &vs, const Animated *obj) const;
  virtual bool Simulate(Animated *obj, float deltaT) const;
};

AnimationSourceUser::AnimationSourceUser(int dataIndex)
:base(RStringB(),dataIndex)
{
  _animSpeed = 1;
  _initPhase = 0;
}

AnimationSourceUser::AnimationSourceUser(int dataIndex, ParamEntryPar cfg)
:base(RStringB(),dataIndex)
{
  float animPeriod = cfg>>"animPeriod";
  // duration 0 is nonsense
  if (animPeriod<=0)
    animPeriod = 1;
  _animSpeed = 1/animPeriod;

  _initPhase = 0;
  ConstParamEntryPtr entry = cfg.FindEntry("initPhase");
  if (entry)
    _initPhase = *entry;
}

float AnimationSourceUser::GetPhase(const ObjectVisualState &vs, const Animated *obj) const
{
  Assert(dynamic_cast<const Entity *>(obj));
  const Entity *entity = static_cast<const Entity *>(obj);
  return entity->GetAnimationInstancePhase(vs.cast<Entity>(), _dataIndex);
}

void AnimationSourceUser::Init(Animated *obj)
{
  Assert(dynamic_cast<Entity *>(obj));
  Entity *entity = static_cast<Entity *>(obj);
  entity->SetAnimationInstancePhaseWanted(_dataIndex, _initPhase);
  entity->SetAnimationInstancePhase(_dataIndex, _initPhase);
}

/**
@return false once animation reached stable state (is finished)
*/
bool AnimationSourceUser::Simulate(Animated *obj, float deltaT) const
{
  Assert(dynamic_cast<Entity *>(obj));
  Entity *entity = static_cast<Entity *>(obj);
  float phase = entity->GetAnimationInstancePhase(entity->FutureVisualState(), _dataIndex);
  float phaseWanted = entity->GetAnimationInstancePhaseWanted(_dataIndex);
  float delta = phaseWanted - phase;
  
  if (fabs(delta)<1e-6)
  {
    entity->SetAnimationInstancePhase(_dataIndex,phaseWanted);
    return false;
  }
  saturateMax(deltaT, 0);
  float maxDelta = _animSpeed*deltaT;
  saturate(delta,-maxDelta,maxDelta);
  phase += delta;
  entity->SetAnimationInstancePhase(_dataIndex,phase);
  return true;
}

AnimationSource *EntityType::CreateAnimationSourceUser(ParamEntryPar cfg)
{
  // create an instance data
  AnimationSourceUser *userSource = new AnimationSourceUser(_animSources.AddState(),cfg);
  return userSource;
}

/// animation controlled directly - no smooth animation
class AnimationSourceDirect: public AnimationSourceUserBase
{
  typedef AnimationSourceUserBase base;
  public:
  AnimationSourceDirect(RStringB name, int dataIndex);
  virtual float GetPhase(ObjectVisualState const& unused, const Animated *obj) const;
  virtual bool Simulate(Animated *obj, float deltaT) const;
};

AnimationSourceDirect::AnimationSourceDirect(RStringB name, int dataIndex)
:base(name,dataIndex)
{
}

float AnimationSourceDirect::GetPhase(ObjectVisualState const& unused, const Animated *obj) const
{
  Assert(dynamic_cast<const Entity *>(obj));
  const Entity *entity = static_cast<const Entity *>(obj);
  return entity->GetAnimationInstancePhaseWanted(_dataIndex);
}

bool AnimationSourceDirect::Simulate(Animated *obj, float deltaT) const
{
  // animation has no progress - it is always done
  return false;
}

/// animation of entity hitPoint state
class AnimationSourceHit : public AnimationSource
{
  typedef AnimationSource base;

protected:
  RString _hitPoint;
  bool _raw;

public:  
  AnimationSourceHit(const RStringB &name, RString hitPoint, bool raw);

  float GetPhase(const ObjectVisualState &vs, const Animated *obj) const;
  void SetPhaseWanted(Animated *obj, float phase) {}
};

AnimationSourceHit::AnimationSourceHit(const RStringB &name, RString hitPoint, bool raw)
: base(name), _hitPoint(hitPoint), _raw(raw)
{
}

float AnimationSourceHit::GetPhase(const ObjectVisualState &vs, const Animated *obj) const
{
  Assert(dynamic_cast<const Entity *>(obj));
  const Entity *entity = static_cast<const Entity *>(obj);
  const EntityType *type = entity->Type();
  if (!type)
    return 0;

  // find the hitPoint by name
  int index = type->FindHitPoint(_hitPoint);
  if (index < 0)
    return 0;

  return _raw ? entity->GetHitContRaw(index) : entity->GetHitCont(index);
}

AnimationSource *EntityType::CreateAnimationSourceDirect(const char *name)
{
  // if necessary, generate a unique name
  RString uniqueName;
  if (name)
  {
    // if source with the same name exists, reuse it
    int index = _animSources.FindName(name);
    if (index>=0)
      // note: we might actually replicate non-direct source here
      return _animSources[index];
  }
  if (!name)
  {
    int stateIndex = _animSources.AddState();
    RString uniqueName = Format("User%d",stateIndex);
    return new AnimationSourceDirect(uniqueName,stateIndex);
  }
  // create an instance data
  AnimationSourceDirect *userSource = new AnimationSourceDirect(name,_animSources.AddState());
  return userSource;
}

AnimationSource *EntityType::CreateAnimationSourcePar(const AnimationType *type, ParamEntryPar entry)
{
  RStringB source = entry >> "source";
  if (!strcmpi(source, "hit"))
  {
    RStringB hitPoint = entry >> "hitpoint";
    bool raw = false;
    ConstParamEntryPtr rawEntry = entry.FindEntry("raw");
    if (rawEntry) raw = *rawEntry;
    return new AnimationSourceHit(source, hitPoint, raw);
  }
  ErrF("%s: %s - unknown animation source %s (defined in AnimationSources::%s)", cc_cast(GetName()), cc_cast(type->GetName()), cc_cast(source), cc_cast(entry.GetName()));
  return CreateAnimationSourceDirect(source);
}


AnimationSource *EntityType::CreateAnimationSource(const AnimationType *type, RStringB source)
{
  // TODO: this needs to be converted to CurrentVisualState

  // check if such source exists in AnimationSources
  if (source[0] == ':' && source[1] == ':')
    // avoid check of AnimationSources
    source = source.Mid(2);
  else if (_par)
  {
    ConstParamEntryPtr animSources = _par->FindEntry("AnimationSources");
    if (animSources)
    {
      // first check if the source exists - if it does, replicate it
      int index = _animSources.FindName(source);
      if (index >= 0)
        return _animSources[index];

      ConstParamEntryPtr animSource = animSources->FindEntry(source);
      if (animSource)
        // entry specified in AnimationSources - load it from there
        return CreateAnimationSourcePar(type, *animSource);
    }
    // continue to directly defined sources
  }

  if (!strcmpi(source,"damage"))
    // explicit template argument used; compiler would assume Object because GetTotalDamage is actually Object::GetTotalDamage
    return _animSources.CreateAnimationSource<Entity>(&Entity::GetTotalDamageCtrl);
  AnimationSource *animSource = AnimatedType::CreateAnimationSource(type, source);
  if (animSource)
    return animSource;

  // anonymous user sources - each is unique
  if (!strcmpi(source,"user"))
  {
    if (_par)
    {
      ConstParamEntryPtr entry = ((*_par)>>"AnimationSources").FindEntry(type->GetName());
      
      // if the animation config is missing
      if (entry)
        // anim config exists - read it
        return CreateAnimationSourceUser(*entry);
      WarningMessage("Animation source %s not found in %s",cc_cast(type->GetName()),cc_cast(_par->GetContext()));
    }
    // if there is no type config, direct is the only possible implementation
    return CreateAnimationSourceDirect(NULL);
  }
  else if (!strcmpi(source,"direct"))
  {
    RptF("%s: %s - Anonymous direct sources are no longer supported",cc_cast(GetName()),cc_cast(type->GetName()));
    return CreateAnimationSourceDirect("");
  }
  else
  {
    // if there is no _par, there is no type config, and we cannot expect any sources defined
    if (_par)
    {
      RptF("%s: %s - unknown animation source %s",cc_cast(GetName()),cc_cast(type->GetName()),cc_cast(source));
    }
    return CreateAnimationSourceDirect(source);
  }
}

FSMEntityConditionFunc *EntityType::CreateEntityConditionFunc(RString name)
{
  ENTITY_FSM_CONDITIONS(REGISTER_ENTITY_FSM_CONDITION);

  RptF("%s: Unknown FSMEntity condition function: %s", GETREPORTSTACK(),cc_cast(name));
  return ::CreateEntityConditionFunc(&Entity::FSMFalse);
}

FSMEntityActionFunc *EntityType::CreateEntityActionInFunc(RString name)
{
  ENTITY_FSM_ACTIONS(REGISTER_ENTITY_FSM_ACTION_IN);

  RptF("%s: Unknown FSMEntity action function: %s", GETREPORTSTACK(),cc_cast(name));
  return ::CreateEntityActionFunc(&Entity::FSMNothingIn);
}

FSMEntityActionFunc *EntityType::CreateEntityActionOutFunc(RString name)
{
  ENTITY_FSM_ACTIONS(REGISTER_ENTITY_FSM_ACTION_OUT)

  RptF("%s: Unknown FSMEntity action function: %s", GETREPORTSTACK(),cc_cast(name));
  return ::CreateEntityActionFunc(&Entity::FSMNothingOut);
}

void Entity::FSMCreateFSMIn(FSMEntity *fsm, const float *params, int paramsCount)
{
  if (paramsCount>1) 
  {
    int slotNum = toInt(params[0]);
    int fsmNum =  toInt(params[1]);
    if (fsmNum>=Type()->_fsm.Size())
      return;
    if (slotNum>=_fsm.Size())
    {
      _fsm.Realloc(slotNum+1);
      _fsm.Resize(slotNum+1);
    }
    if (_fsm[slotNum]!=NULL && _fsm[slotNum]->GetFSMType()==Type()->_fsm[fsmNum])
      return; //this FSM is inside the specified slot yet
    //if there is some other FSM inside the slot, then it will be destroyed
    _fsm[slotNum] = new FSMEntity(Type()->_fsm[fsmNum]);
  }
}

void Entity::FSMDeleteFSMIn(FSMEntity *fsm, const float *params, int paramsCount)
{
  if (paramsCount>0) 
  {
    int slotNum = toInt(params[0]);
    if (slotNum>=_fsm.Size() || _fsm[slotNum]==NULL) 
    {
      RptF("Cannot delete FSM, no FSM in slot");
      return; //no fsm in this slot
    }
    _fsm[slotNum]=NULL;
  }
}

float Entity::FSMFinished(const FSMEntity *fsm, const float *params, int paramsCount) const
{
  if (paramsCount>0) 
  {
    int slotNum = toInt(params[0]);
    if (slotNum>=_fsm.Size() || _fsm[slotNum]==NULL) 
      return 1.0f; //no fsm in this slot
  }
  return 0.0f;
}

void Entity::FSMUnhandledAction(RString text)
{
  static const char *prefix = "script:";
  if (strnicmp(text, prefix, strlen(prefix)) == 0)
  {
    GameVarSpace local(false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.VarSetLocal("_this", GameValueExt(this), true);
    GGameState.Execute(cc_cast(text) + strlen(prefix), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    GGameState.EndContext(gshandle);
  }
}

float Entity::FSMUnhandledCondition(RString text) const
{
  static const char *prefix = "script:";
  if (strnicmp(text, prefix, strlen(prefix)) == 0)
  {
    GameVarSpace local(false);
    int gshandle = GGameState.BeginContext(&local);
    GGameState.VarSetLocal("_this", GameValueExt(unconst_cast(this)), true);
    GameValue value = GGameState.Evaluate(cc_cast(text) + strlen(prefix), GameState::EvalContext::_default, GWorld->GetMissionNamespace()); // mission namespace
    GGameState.EndContext(gshandle);
    DoAssert(value.GetType() == GameBool || value.GetType() == GameScalar);
    return value;
  }
  return -FLT_MAX; // will be compared to threshold, the result should be false
}

size_t EntityType::GetMemoryControlled() const
{
  // who knows how much memory is controlled?
  return sizeof(*this);
}

#if _ENABLE_CHEATS
static char InterestedIn[256] = ""; //"data3d\\truck5t2.p3d";
#endif

#if _DEBUG
void EntityType::OnUsed() const
{
  DoAssert(!IsInList());
}
#endif

void EntityType::OnUnused() const
{
  if (_name._className.GetLength()>0)
  {
    //LogF("##  Unlinked %s",cc_cast(_name._className));
    // remove all links to simulate entry destruction
    //DestroyLinkChain();
    // types which are stored in the bank
    VehicleTypes.OnUnused(unconst_cast(this));
  }
  else
    Destroy();
}

void EntityType::InitShape()
{
  Assert(_scopeLevel==1);
  _scopeLevel = 2;

  _proxies.Init(_shape);

  // must be first of InitShape chain
  _hitPoints.Clear();

  _animSources.Clear();
  
  if (_par)
  {
    ConstParamEntryPtr damage = _par->FindEntry("Damage");
    if (damage)
    {  
      if (_shape)
      {
        _damageInfo = new WoundInfo;
        _damageInfo->Load(_shape, (*_par) >> "Damage");
      }
    }

    // animate textures
    ConstParamEntryPtr array = _par->FindEntry("animateTextures");
    if (array)
    {
      int n = array->GetSize() / 2, index = 0;
      _animateTextures.Realloc(n);
      _animateTextures.Resize(n);
      for (int i=0; i<n; i++)
      {
        RString name = (*array)[index++];
        _animateTextures[i].animation.Init(_shape, name, NULL);
        float animTime = (*array)[index++];
        _animateTextures[i].animSpeed = animTime > 0 ? 1.0f / animTime : 0;
      }
    }

    array = _par->FindEntry("Animations");
    if (array)
    {
      RptF("Obsolete class Animations defined in %s",cc_cast(GetName()));
    }
    _animSources.Load(this, GetAnimations());
    if (_shape)
    {
      ConstParamEntryPtr destroyAnim = _par->FindEntry("destruction");
      if (destroyAnim)
      {
        _destroyAnim = new ObjectDestroyAnim(_shape,*destroyAnim);
        //LogF("Entity with destruction anim %s",cc_cast(par.GetName()));
      }
      //try to read thermal properties to override stored values in model.cfg
      float htMin = _par->ReadValue("htMin", _shape->_htMin);
      float htMax = _par->ReadValue("htMax", _shape->_htMax);
      float afMax = _par->ReadValue("afMax", _shape->_afMax);
      float mfMax = _par->ReadValue("mfMax", _shape->_mfMax);
      float mFact = _par->ReadValue("mFact", _shape->_mFact);
      float tBody = _par->ReadValue("tBody", _shape->_tBody);
      if ((htMin != _shape->_htMin) ||
        (htMax != _shape->_htMax) ||
        (afMax != _shape->_afMax) ||
        (mfMax != _shape->_mfMax) ||
        (mFact != _shape->_mFact) ||
        (tBody != _shape->_tBody))
      {
        _shape->_htMin = htMin;
        _shape->_htMax = htMax;
        _shape->_afMax = afMax;
        _shape->_mfMax = mfMax;
        _shape->_mFact = mFact;
        _shape->_tBody = tBody;
//         LogF("[m] Overwrite TI Parameters for model: %s from %s", cc_cast(_shape->GetName()),cc_cast(_par->GetContext()));
//         LogF("*[TI] %s: htMin %g",cc_cast(_name._className),_shape->_htMin);
//         LogF("*[TI] %s: htMax %g",cc_cast(_name._className),_shape->_htMax);
//         LogF("*[TI] %s: afMax %g",cc_cast(_name._className),_shape->_afMax);
//         LogF("*[TI] %s: mfMax %g",cc_cast(_name._className),_shape->_mfMax);
//         LogF("*[TI] %s: mFact %g",cc_cast(_name._className),_shape->_mFact);
//         LogF("*[TI] %s: tBody %g",cc_cast(_name._className),_shape->_tBody);
      }
    }

    // hitpoints
    ConstParamEntryPtr hitPoints = _par->FindEntry("HitPoints");
    if (hitPoints)
    {  
      for (ParamClass::Iterator<> i(hitPoints); i; ++i)
      {
        ParamEntryVal entry = *i;
        if (entry.IsClass())
          AddHitPoint(entry, GetEntityArmor());
      }      
    } 
    _hitPoints.Compact();
  }
  else if (_shape && _shape->GetAnimations())
    _animSources.Load(this, GetAnimations());
  _bounding = -1;
  if (_shape && _shape->FindMemoryLevel()>=0 && _par)
  {
    ConstParamEntryPtr bounding = _par->FindEntry("bounding");
    if (bounding)
    {
      RStringB boundingName = *bounding;
      _bounding = _shape->FindNamedSel(_shape->MemoryLevel(),boundingName);
    }
  }
}

void EntityType::DeinitShape()
{
  Assert(_scopeLevel==2);
  _scopeLevel = 1;
  _animSources.Clear();
  _animateTextures.Clear();
  _proxies.Deinit(_shape);
  _destroyAnim.Free();
  _bounding = -1;
}

void EntityType::ShapeLoaded(LODShape *shape, int level)
{
#if 0 //_ENABLE_CHEATS
  if (shape && !strcmp(shape->GetName(),InterestedIn))
  {
    LogF(
      "## Shape lod %s:%d loaded for type %s",
      cc_cast(shape->GetName()),level,cc_cast(GetName())
    );
  }
#endif
  DoAssert(!_shape || _shape->RefCounter()>0);
  if (!_shape && _shapeWithHandler)
  {
    // the shape is temporarily locked (TempAddRef)
    DoAssert(_shapeWithHandler->RefCounter()>0);
    DoAssert(shape==_shapeWithHandler);
    _shape = _shapeWithHandler;
    InitShapeLevel(level);
    _shape = NULL;
  }
  else
  {
    DoAssert(shape==_shape);
    InitShapeLevel(level);
  }
}

void EntityType::ShapeUnloaded(LODShape *shape, int level)
{
#if 0 // _ENABLE_CHEATS
  if (shape && !strcmp(shape->GetName(),InterestedIn))
    LogF("## Shape lod %s:%d unloaded for type %s",cc_cast(shape->GetName()),level,cc_cast(GetName()));
#endif
  if (!_shape && _shapeWithHandler)
  {
    // the shape is temporarily locked (TempAddRef)
    Assert(_shapeWithHandler->RefCounter()>0);
    _shape = _shapeWithHandler;
    DeinitShapeLevel(level);
    _shape = NULL;
  }
  else
  {
    Assert(shape==_shape);
    DeinitShapeLevel(level);
  }
}

void EntityType::LODShapeLoaded(LODShape *shape)
{
#if 0 //_ENABLE_CHEATS
  if (shape && !strcmp(shape->GetName(),InterestedIn))
    LogF("## Shape %s loaded for type %s",cc_cast(shape->GetName()),cc_cast(GetName()));
#endif
  PROFILE_SCOPE(etLLo);
  Assert(shape==_shapeWithHandler);
  if (!_shape && _shapeWithHandler)
  {
    // the shape is temporarily locked (TempAddRef)
    Assert(_shapeWithHandler->RefCounter()>0);
    _shape = _shapeWithHandler;
    Assert(shape==_shape);
    DoAssert(!_shape || _shape->RefCounter()>0);
    _shape->LoadSkeletonFromSource();
    InitShape();
    if (_shape)
      PrepareHardwareAnimation();
  }
  else
  {
    Assert(shape==_shape);
    DoAssert(!_shape || _shape->RefCounter()>0);
    if (_shape.NotNull())
      _shape->LoadSkeletonFromSource();
    InitShape();
    if (_shape)
      PrepareHardwareAnimation();
  }
}

void EntityType::LODShapeUnloaded(LODShape *shape)
{
#if 0 // _ENABLE_CHEATS
  if (shape && !strcmp(shape->GetName(),InterestedIn))
    LogF("## Shape %s unloaded for type %s",cc_cast(shape->GetName()),cc_cast(GetName()));
#endif
  Assert(shape==_shapeWithHandler);
  if (!_shape && _shapeWithHandler)
  {
    // the shape is temporarily locked (TempAddRef)
    Assert(_shapeWithHandler->RefCounter()>0);
    _shape = _shapeWithHandler;
    DeinitShape();
    _shape = NULL;
  }
  else
  {
    Assert(shape==_shape);
    DeinitShape();
  }
  // the shape will be destroyed anyway, and it would set the Link to NULL soon
  // but doing it here will do no harm
  // and it will prevent reusing the shape anywhere during destruction
  _shapeWithHandler = NULL;
}

bool EntityType::IsShapeReady(const LODShape *lodShape, int level, const Ref<ProxyObject> *proxies, int nProxies, bool requestIfNotReady)
{
  bool ret = true;
  // we are notified shape is to be loaded and what proxies it will contain.
  for (int p=0; p<nProxies; p++)
  {
    const ProxyObject &src = *proxies[p];
    if (GReplaceProxies)
    {
      RString proxyName = GetProxyType(src.name);
      Ref<EntityType> proxyType = VehicleTypes.New(proxyName);
      if (proxyType)
      {
        bool ready = proxyType->IsReadyForVehicle();
        if (!ready)
          ret = false;
      }
      else
      {
        RString proxyShape = ::GetShapeName(src.name);
        // no explicit proxy type - assume plain object
        Ref<LODShape> pShape;
        bool ready = Shapes.Preload(FileRequestPriority(100),proxyShape,ShapeShadow,pShape);
        if (!ready)
          ret = ready;
      }
    }
    else
    {
      RString proxyShape = ::GetShapeName(src.name);
      Ref<LODShape> pShape;
      bool ready = Shapes.Preload(FileRequestPriority(100),proxyShape,ShapeShadow,pShape);
      if (!ready)
        ret = ready;
    }
  }
  return ret;
}

bool EntityType::PreloadProxies(int level, float dist2, float scale, bool preloadTextures) const
{
  if (!_shape)
    return true;
  ShapeUsed lock = _shape->Level(level);
  bool ret = true;
  for( int i=0; i<GetProxyCountForLevel(level); i++ )
  {
    // check proxy corresponding to this shape proxy in given type

    const ProxyObjectTyped &proxy = GetProxyForLevel(level,i);
    
    // smart clipping part of obj->Draw

    // LOD detection
    LODShape *pshape = proxy.obj->GetShape();
    if (!pshape) continue;
    int level=GScene->PreloadLevelFromDistance2(pshape,dist2,proxy.obj->FutureVisualState().Scale()*scale);
    if( level==LOD_INVISIBLE ) continue;
    if (level<0)
    {
      ret = false;
      continue;
    }
    if (!pshape->PreloadVBuffer(level,dist2))
    {
      ret = false;
      continue;
    }

    // Preload textures
    if (preloadTextures)
    {
      ShapeUsed shape = pshape->Level(level);
      if (!shape->PreloadTextures(dist2, NULL))
        ret = false;
    }
  }
  return ret;
}

/** preload everything so that InitShapeLevel can be done */

bool EntityType::PreloadShapeLevel(FileRequestPriority prior, LODShape *shape, int level) const
{
  bool ret = true;
  
  // TODO: maybe: RefShapeRef shapeRef = shape->LevelRef(level);
  // should be enough, 

  ShapeUsed shapeRef = shape->Level(level);
  for (int p = 0; p<shapeRef->NProxies(); p++)
  {
    const ProxyObject &src = shapeRef->Proxy(p);

    if (GReplaceProxies)
    {
      RString proxyType = GetProxyType(src.name);
      // check if given type / shape is already loaded
      // check if corresponding type exists
      // note: preloading might be necessary here
      Ref<EntityType> type=VehicleTypes.New(proxyType);
      if (!type)
      {
        RString proxyShape = ::GetShapeName(src.name);
        Ref<LODShape> proxyShapeReady;
        if (!Shapes.Preload(prior,proxyShape,ShapeShadow,proxyShapeReady))
          ret = false;
      }
      else
      {
        // preload a shape from given type
        if (!type->PreloadShape(prior))
          ret = false;
      }
    }
  }
  return ret;
}

void EntityType::InitShapeLevel(int level)
{
  DoAssert(!_shape || _shape->RefCounter()>0);
  // load all proxies
  _proxies.InitLevel(_shape, level);

  for (int i=0; i<_animateTextures.Size(); i++)
    _animateTextures[i].animation.InitLevel(_shape, level);

  if (_damageInfo && !_damageInfo->IsEmpty())
  {
    for (int i=0; i<_hitPoints.Size(); i++)
      _hitPoints[i].InitLevelVisual(_shape,level);
  }
}

void EntityType::DeinitShapeLevel(int level)
{
  for (int i=0; i<_animateTextures.Size(); i++)
    _animateTextures[i].animation.DeinitLevel(_shape, level);
  _proxies.DeinitLevel(_shape, level);
  // _proxy[level].Clear();
  if (_damageInfo && !_damageInfo->IsEmpty())
  {
    for (int i=0; i<_hitPoints.Size(); i++)
      _hitPoints[i].DeinitLevelVisual(_shape,level);
  }
}

void EntityType::ReloadShape(QIStream &f, const char *filename)
{
  if (_refVehicles>0)
  {
    // removing/adding handler will execute Unload/Load handlers
    _shape->RemoveLoadHandler(this);
    _shape->Reload(f,filename,_shapeReversed);
    _shape->AddLoadHandler(this);
    _shapeWithHandler = _shape;
  }
}

void EntityType::AttachShape(LODShape *shape)
{
  if (_refVehicles != 0)
    LogF("Shape %s changed when used.",(const char *)shape->Name());
  _shape = shape;
  InitShape();
  if (_refVehicles <= 0)
    _refVehicles = 1;
}

bool EntityType::PreloadShape(FileRequestPriority prior, Ref<LODShape> &shape) const
{
  // if there is any object using the shape, the shape is certainly loaded
  if (_refVehicles>0)
  {
    shape = _shape;
    return true;
  }
  bool ret = true;
  if (_shapeName.GetLength()>0)
  {
    ShapeParameters pars = _shadowEnabled ? ShapeShadow : 0;
    if (_shapeReversed)
      pars |= ShapeReversed;
    ret = Shapes.Preload(prior,_shapeName,pars,shape);
  }
  else
    shape = _shape;
  if (ret && shape)
  {
    // model ready - we should preload proxies now
    // which proxies? which LOD?
    // we will preload proxies for all currently loaded lods
    for (int i=0; i<shape->NLevels(); i++)
    {
      RefShapeRef shapeRef = shape->LevelRef(i);
      if (!shapeRef.IsLoaded())
        continue;
      if (!PreloadShapeLevel(prior,shape,i))
        ret = false;
    }
  }
  return ret;
}

bool EntityType::PreloadData(FileRequestPriority prior, float distance) const
{
  Ref<LODShape> shape;
  if (!PreloadShape(prior,shape))
    return false;
  // shape is initialized now - preload textures
  float dist2 = Square(distance);
  int level = GScene->PreloadLevelFromDistance2(shape,dist2,1.0f);
  if (level<0) return false;
  if (level==LOD_INVISIBLE) return true;
  ShapeUsed shapeLevel = shape->Level(level);
  bool forceAnimated = _shapeAnimated==AnimTypeSoftware;
  return shapeLevel->PreloadTextures(Square(distance), NULL) && shape->PreloadVBuffer(level,dist2,forceAnimated);
}

bool EntityType::PreloadShape(FileRequestPriority prior) const
{
  Ref<LODShape> shape;
  return PreloadShape(prior,shape);
}

bool EntityType::IsReadyForVehicle() const
{
  // check if the shape is already loaded
  return PreloadShape(FileRequestPriority(100));
}

void EntityType::VehicleAddRef() const
{
  ProgressRefresh();

  // vehicle created
  if( _refVehicles++==0 )
  {
    if (_shapeName.GetLength()>0)
    {
      // check if shadow is enabled for this particular shape
      // generally there shadow setting for some "vehicles"
      // should be taken from objects
      ShapeParameters pars = _shadowEnabled ? ShapeShadow : 0;
      if (_shapeReversed) pars |= ShapeReversed;
      /*
      if (_shapeAnimated!=AnimTypeNone)
        pars |= ShapeAnimated|ShapeSeparateShadowFaces;
      */
      _shape = Shapes.New(_shapeName,pars);
      ProgressRefresh();
      if (!_shapeAutoCentered)
      {
        if (_shape->GetAutoCenter())
        {
          _shape->SetAutoCenter(false);
          // no need to recalculate radius - only center position is changed
          _shape->CalculateBoundingSphere(false);
        }
      }
      ProgressRefresh();

      // in case the shape was never really unloaded, the handler may be already there
      if (!_shape || !_shape->CheckLoadHandler(this))
      {
        Assert(_scopeLevel == 1);

        if (_shape && _shapeAnimated!=AnimTypeNone)
          _shape->SetAnimationType(_shapeAnimated);

        _shapeWithHandler = _shape;
        _shape->AddLoadHandler(unconst_cast(this));
        //LogF("Type %s shape loaded",(const char *)GetName());
        Assert(_scopeLevel == 2);
      }
      else
        Assert(_scopeLevel == 2);
    }
    else
    {
      //Assert(_shape);
      Assert(_scopeLevel==1);
      if (_shape)
      {
        _shapeWithHandler = _shape;
        _shape->AddLoadHandler(unconst_cast(this));
      }
      else
        unconst_cast(this)->LODShapeLoaded(_shape);
      Assert(_scopeLevel == 2);
    }
  }
  ProgressRefresh();
}

void EntityType::VehicleRelease() const
{
  Assert(_refVehicles > 0);
  // vehicle destroyed
  if (--_refVehicles == 0)
  {
    // 2 Refs - one in last vehicle, one in bank
    Assert(_scopeLevel == 2);
    if (_shape)
    {
      // do not remove the handler - all handlers will be removed once the shape is destroyed
      //_shape->RemoveLoadHandler(unconst_cast(this));
      _shape.Free(); // shape no longer needed
    }
    else
      unconst_cast(this)->LODShapeUnloaded(_shape);
    //Assert( _scopeLevel==1);
  }
}

int EntityType::GetProxyCountForLevel(int level) const
{
  return _proxies.GetProxyCountForLevel(_shape, level);
}

const ProxyObjectTyped &EntityType::GetProxyForLevel(int level, int i) const
{
  return _proxies.GetProxyForLevel(_shape, level, i);
}

SkeletonIndex EntityType::GetProxyBoneForLevel(int level, int i) const
{
  return _proxies.GetProxyBoneForLevel(_shape, level, i);
}

int EntityType::GetProxySectionForLevel(int level, int i) const
{
  return _proxies.GetProxySectionForLevel(_shape, level, i);
}


EntityVisualState::EntityVisualState(EntityType const& type)
:_speed(VZero),_modelSpeed(VZero),_acceleration(VZero),
_invTransform(MIdentity),_invDirty(false),_deltaT(0)
{
  int nStates = type._animSources.NStates();
  _animationStates.Realloc(nStates);
  _animationStates.Resize(nStates);

}


void EntityVisualState::Interpolate(Object *obj, ObjectVisualState const& t1state, float t, ObjectVisualState& interpolatedResult) const
{
  base::Interpolate(obj, t1state, t, interpolatedResult);

  EntityVisualState const& s = static_cast<EntityVisualState const&>(t1state);
  EntityVisualState& res = static_cast<EntityVisualState&>(interpolatedResult);

  // _deltaT won't be used
  res.InvDirty();  // => _invTransform won't be used, hopefully

  #define INTERPOLATE(x) res.x = x + t*(s.x - x)

  INTERPOLATE(_acceleration);
  INTERPOLATE(_modelSpeed);
  INTERPOLATE(_speed);

  for (int i=0; i<res._animationStates.Size(); i++)
  {
    float phase = _animationStates[i].GetPhase();
    res._animationStates[i].SetPhase( phase + t*(s._animationStates[i].GetPhase() - phase) );
  }

  #undef INTERPOLATE
}

// JNI scripting support
jweak JavaTypes<Vector3>::_class = NULL;
jmethodID JavaTypes<Vector3>::_constructor = NULL;
jfieldID JavaTypes<Vector3>::_x = NULL;
jfieldID JavaTypes<Vector3>::_y = NULL;
jfieldID JavaTypes<Vector3>::_z = NULL;

jweak JavaTypes<Entity>::_class = NULL;
jmethodID JavaTypes<Entity>::_constructor = NULL;
jfieldID JavaTypes<Entity>::_jID = NULL;

jobject JavaTypes<Entity>::FromNative(JNIEnv *env, const Entity *native)
{
  if (native == NULL) return NULL;

  // activate JVM FPU settings
  SCOPE_FPU_JVM

  jclass cls = InitClass(env);
  int jID = native->GetJID();

  jobject jni = env->NewObject(cls, _constructor);
  if (jni != NULL) env->SetIntField(jni, _jID, jID);
  return jni;
}
Entity *JavaTypes<Entity>::ToNative(JNIEnv *env, jobject jni)
{
  if (jni == NULL) return NULL;

  // activate JVM FPU settings
  SCOPE_FPU_JVM

  InitClass(env);
  int jID = env->GetIntField(jni, _jID);

  return GWorld->GetEntity(jID);
}

int Entity::GetJID() const
{
  if (_jID < 0) _jID = GWorld->AddEntity(unconst_cast(this));
  return _jID;
}

SkeletonIndex Entity::GetBone(const char* name)const
{
  //return SkeletonIndexNone;
  LODShape *sh = GetShape();
  if(sh)
  {
    Skeleton* sk = sh->GetSkeleton();
    if(sk)
      return sk->FindBone(name);
  }
  return SkeletonIndexNone;
}

#if _ENABLE_PHYSICS
void Entity::GetSkeletonMatrices(Matrix4* cmat, Matrix4* tmat)
{
  Skeleton *skeleton = GetShape()->GetSkeleton();
  Array<Vector3> pivArr = skeleton->GetPivots();

	Matrix4Array tmatArr = PrepareRagdollMatricesT();
  Matrix4Array matArr = PrepareRagdollMatrices();

  for(SkeletonIndex i = 0; i < skeleton->NBones(); ++i)
  {
		Vector3Par pos = Matrix4(matArr.Get(i)).FastTransform(pivArr.Get(i));
		cmat[i] = matArr.Get(i);
		cmat[i].SetPosition(pos);

    Vector3Par post = Matrix4(tmatArr.Get(i)).FastTransform(pivArr.Get(i));
		tmat[i] = tmatArr.Get(i);
		tmat[i].SetPosition(post);
	}
}

Matrix4Array Entity::PrepareRagdollMatrices()//Matrix4* tpose, Matrix4* cpose)
{
  Matrix4Array _matrices;

  Skeleton *_skeleton = GetShape()->GetSkeleton();
  Assert(_skeleton);
  if (!_skeleton)
    return _matrices;

  // IDEA behind this
  // -> first we prepare matrices for shape LOD level 0 -> but some matrices will not be included in this
  // -> then we prepare remaining matrices using BlendAnims
  // we cannot use all matrices from BlendAnims, because during matric preparation for shape level some additional animations are applies (lean,...)
  // and we need the ragdoll to start in exactly the same position as current animation

  // get object shape
  LODShape* lodshape = GetShape();
  Assert(lodshape);
  if (!lodshape)
    return _matrices;

  // get lod
  const int level = 0;
  bool wasLocked = lodshape->IsLevelLocked(level);

  // lock level if needed
  if (!wasLocked)
    lodshape->LockLevel(level);
  const Shape *shape = lodshape->GetLevelLocked(level);
  Assert(shape)
  if (!shape)
  {
    ErrF("Creating of ragdoll object context for skeleton \"%s\" failed - cannot get level %d of shape \"%s\".", 
      cc_cast(_skeleton->GetName()), level, cc_cast(lodshape->GetName()));
    return _matrices;
  }

  // prepare sub skeleton matrices (for given level)
  Matrix4Array subSkeletonMatrices;
	PrepareShapeDrawMatrices(subSkeletonMatrices, RenderVisualState(), lodshape, level);
	
	// unlock level if needed
  if (!wasLocked)
    lodshape->UnlockLevel(level);

  if (subSkeletonMatrices.Size() != shape->GetSubSkeletonSize())
  {
    ErrF("Creating of ragdoll object context for skeleton \"%s\" failed - number of bone matrices is different from number of bones in level %d of shape \"%s\"", 
      cc_cast(_skeleton->GetName()), level, cc_cast(lodshape->GetName()));
    return _matrices;
  }

  // create skeleton matrices 
  int nBones = _skeleton->NBones();
  _matrices.Resize(nBones);

  // try if the object is of type Man
  Man* manObj = dynamic_cast<Man *>(this);

  // get blend anims
	BlendAnims blendAnims;
  if (manObj)
  {
    manObj->GetBlendAnims(manObj->RenderVisualState(), blendAnims);
  }
	
	for (int i = 0; i<nBones; ++i)
  {
    SkeletonIndex si = SetSkeletonIndex(i);
    
    const SubSkeletonIndexSet& subSet = shape->GetSubSkeletonIndexFromSkeletonIndex(si);
    bool takenFromSubSkeleton = false;
    if (subSet.Size() > 0)
    {
      // matrix contained in subskeleton
      SubSkeletonIndex ssi = subSet[0];
      Assert(GetSubSkeletonIndex(ssi) >= 0 && GetSubSkeletonIndex(ssi) < subSkeletonMatrices.Size());
      if (GetSubSkeletonIndex(ssi) >= 0 && GetSubSkeletonIndex(ssi) < subSkeletonMatrices.Size())
      {
        _matrices[i] = subSkeletonMatrices[GetSubSkeletonIndex(ssi)];
        takenFromSubSkeleton = true;
      }
    }

		if (!takenFromSubSkeleton)
    {
      // matrix not contained in subskeleton -> take it from blendanims
      _matrices[i].SetIdentity();
			
      if (manObj)      
			{
				Soldier* solObj = dynamic_cast<Soldier *>(this);
				if(solObj && solObj->GetRagdollCtrl())
					;
				else
					blendAnims.DoBoneAnim(_matrices[i], _skeleton, si);
			}
    }
		//_matrices[i].Orthogonalize();
  }

  return _matrices;
}

Matrix4Array Entity::PrepareRagdollMatricesT()//Matrix4* tpose, Matrix4* cpose)
{
  Matrix4Array _matrices;

  Skeleton *_skeleton = GetShape()->GetSkeleton();
  Assert(_skeleton);
  if (!_skeleton)
    return _matrices;

  // IDEA behind this
  // -> first we prepare matrices for shape LOD level 0 -> but some matrices will not be included in this
  // -> then we prepare remaining matrices using BlendAnims
  // we cannot use all matrices from BlendAnims, because during matric preparation for shape level some additional animations are applies (lean,...)
  // and we need the ragdoll to start in exactly the same position as current animation

  // get object shape
  LODShape* lodshape = GetShape();
  Assert(lodshape);
  if (!lodshape)
    return _matrices;

  // get lod
  const int level = 0;
  bool wasLocked = lodshape->IsLevelLocked(level);

  // lock level if needed
  if (!wasLocked)
    lodshape->LockLevel(level);
  const Shape *shape = lodshape->GetLevelLocked(level);
  Assert(shape)
  if (!shape)
  {
    ErrF("Creating of ragdoll object context for skeleton \"%s\" failed - cannot get level %d of shape \"%s\".", 
      cc_cast(_skeleton->GetName()), level, cc_cast(lodshape->GetName()));
    return _matrices;
  }

  // prepare sub skeleton matrices (for given level)
  Matrix4Array subSkeletonMatrices;
	PrepareShapeDrawMatrices(subSkeletonMatrices, RenderVisualState(), lodshape, level);
	
	for(int i = 0; i < subSkeletonMatrices.Size(); ++i)
		subSkeletonMatrices[i] = MIdentity;

	// unlock level if needed
  if (!wasLocked)
    lodshape->UnlockLevel(level);

  if (subSkeletonMatrices.Size() != shape->GetSubSkeletonSize())
  {
    ErrF("Creating of ragdoll object context for skeleton \"%s\" failed - number of bone matrices is different from number of bones in level %d of shape \"%s\"", 
      cc_cast(_skeleton->GetName()), level, cc_cast(lodshape->GetName()));
    return _matrices;
  }

  // create skeleton matrices 
  int nBones = _skeleton->NBones();
  _matrices.Resize(nBones);

	// try if the object is of type Man
  Man* manObj = dynamic_cast<Man *>(this);

	// get blend anims
	BlendAnims blendAnims;
  if (manObj)
  {
    manObj->GetBlendAnimsNeutral(manObj->RenderVisualState(), blendAnims);
  }
	
	for (int i = 0; i<nBones; ++i)
  {
    SkeletonIndex si = SetSkeletonIndex(i);
    
    const SubSkeletonIndexSet& subSet = shape->GetSubSkeletonIndexFromSkeletonIndex(si);
    bool takenFromSubSkeleton = false;
    if (subSet.Size() > 0)
    {
      // matrix contained in subskeleton
      SubSkeletonIndex ssi = subSet[0];
      Assert(GetSubSkeletonIndex(ssi) >= 0 && GetSubSkeletonIndex(ssi) < subSkeletonMatrices.Size());
      if (GetSubSkeletonIndex(ssi) >= 0 && GetSubSkeletonIndex(ssi) < subSkeletonMatrices.Size())
      {
        _matrices[i] = subSkeletonMatrices[GetSubSkeletonIndex(ssi)];
        takenFromSubSkeleton = true;
      }
    }

		if (!takenFromSubSkeleton)
    {
      // matrix not contained in subskeleton -> take it from blendanims
      _matrices[i].SetIdentity();
			
      if (manObj)      
			{
				Soldier* solObj = dynamic_cast<Soldier *>(this);
				if(solObj && solObj->GetRagdollCtrl())
					;
				else
					blendAnims.DoBoneAnim(_matrices[i], _skeleton, si);
			}
    }
		_matrices[i].Orthogonalize();
  }

  return _matrices;
}
#endif

DEFINE_CASTING(Entity)

Entity::Entity(LODShape *shape, const EntityType *type, const CreateObjectId &id)
:base(shape,type,id,true),
_delete(false),
_moveOutState(MOIn),
_local(true),
_prec(SimulateDefault),
_posCut(false),

_objectContact(false),_landContact(true),_waterContact(false),

_constantColor(HWhite),

_frameID(Glob.frameID - 1),
_timeOffset(1.0/15),_maxTimeOffset(1.0/15),
_simulationPrecision(1.0/15),_simulationPrecisionAI(0.2),
_simulationSkipped(0),_simulationAISkipped(0),_simulationNeeded(false),

_invAngInertia(M3Identity),
_angVelocity(VZero),_angMomentum(VZero),
_invisible(false),

_simulationReadyFromFrame(-1),_simulationReadyResult(false),

_jID(-1),

_impulseForce(VZero),_impulseTorque(VZero),
_objLock(/*fields*/16,/*requests*/5),
#if _ENABLE_ATTACHED_OBJECTS
  _attachedOffset(VZero),
  _attachedMemPointIndex(-1),
  _attachedTrans(M3Identity),
#endif
_targetSide(TCivilian)
{
  _simulationDisabled = false;

#if 0
  if ( shape && stricmp(shape->GetName(), "ca\\air_e\\uh1h\\uh1h.p3d")==0 )
    _asm nop;
#endif
  _currentVisualState = _futureVisualState->Clone();
  _visualStateHistory.Add(_futureVisualState);

#if _ENABLE_CHEATS
  _lastGenericUpdate = Time(0);
  _lastPositionUpdate = Time(0);
  _lastDamageUpdate = Time(0);
#endif

  if (!type)
  {
    if (_shape)
      LogF("No type, shape %s",(const char *)_shape->Name());
    Fail("No type");
  }
  //_type->VehicleAddRef(); // already done in ObjectTyped
  _disableDamageUntil = Glob.time + 2.0f; // disable damage
  _invAngInertia = (_shape) ? _shape->InvInertia() : M3Identity;

  _static = false;
  _destrType = DestructNo;
  Object::_type = TypeVehicle; // change object type

  _animateTexturesTimes.Realloc(type->_animateTextures.Size());
  _animateTexturesTimes.Resize(type->_animateTextures.Size());
  for (int i = 0; i < type->_animateTextures.Size(); i++)
    _animateTexturesTimes[i] = 0;

  ResetAnimationStatus();
  
  // note: CalculateBoundingInfo cannot be called here because it relies on virtual functions
  // it is called in Init instead
  _minmax[0] = VZero;
  _minmax[1] = VZero;
  _boundingSphere = 0;

  if (type && type->_fsm.Size())
    _fsm.Add(new FSMEntity(type->_fsm[0]));

  if (type)
  {
    int nHitPoints = type->_hitPoints.Size();
    _hit.Realloc(nHitPoints);
    _hit.Resize(nHitPoints);
    for (int i = 0; i < nHitPoints; i++)
      _hit[i] = 0;
    _hitEffects.Realloc(nHitPoints);
    _hitEffects.Resize(nHitPoints);

#if 0
    {
      Shape *hitShape = _shape->HitpointsLevel();
      if (hitShape)
      {
        LogF("Hits %s", (const char *)_shape->Name());
        for (int i=0; i<type->_hitPoints.Size(); i++)
        {
          const HitPoint &hit = type->_hitPoints[i];
          int index = hit.GetSelection();
          if (index >= 0)
          {
            const NamedSelection &sel = hitShape->NamedSel(index);
            LogF("  %s", (const char *)sel.Name());
          }
        }
      }
    }
#endif
  }

  _avgAccels.Realloc(16);
  _avgAccels.Resize(16);
  memset(_avgAccels.Data(), 0, sizeof(AcclerationSample) * _avgAccels.Size());
  _avgAccelIndex = 0;
}

Entity::~Entity()
{
#if 0 // _ENABLE_ATTACHED_OBJECTS
  _attachedTo = NULL;
  _attachedObjects.Clear();
#endif
  //_type->VehicleRelease();  // already done in ObjectTyped
  // sound will self-destruct - Ref<> is used
  //UnloadSound();
}

Landscape* Entity::GetWorld() const 
{
  return GLandscape;
}

void EntityVisualState::SetPosition(Vector3Par pos)
{
  base::SetPosition(pos);
  InvDirty();
}

void EntityVisualState::SetTransform(const Matrix4 &transform)
{
  base::SetTransform(transform);
  InvDirty();
}

void EntityVisualState::SetTransformPossiblySkewed(const Matrix4 &transform)
{
  base::SetTransformPossiblySkewed(transform);
  InvDirty();
}

void EntityVisualState::SetOrient(const Matrix3 &dir, const char* name)
{
  base::SetOrient(dir,name);
  InvDirty();
}

void EntityVisualState::SetOrient(Vector3Par dir, Vector3Par up)
{
  base::SetOrient(dir,up);
  InvDirty();
}

void EntityVisualState::SetOrientScaleOnly(float scale)
{
  base::SetOrientScaleOnly(scale);
  InvDirty();
}

void EntityVisualState::CalculateInv() const
{
  _invTransform = (_scale == 0.0f) ? MIdentity : base::GetInvTransform();
}

void EntityVisualState::InvDirty() const
{ 
  _invDirty = true;
}

const Matrix4& EntityVisualState::InvTransform() const
{
  if (_invDirty)
  {
    _invDirty = false;
    CalculateInv();
  }
  return _invTransform;
}

Matrix4 EntityVisualState::GetInvTransform() const
{
  if (_invDirty)
  {
    _invDirty = false;
    CalculateInv();
  }
  return _invTransform;
}


ColorVal Entity::GetConstantColor() const
{
#if _ENABLE_CHEATS
  if (CHECK_DIAG(DETransparent))
  {
    static const Color halfOpaqueWhite(Color(1,1,1,0.5f));
    return halfOpaqueWhite;
  }
#endif
  return _constantColor;
}

VehicleListType Entity::GetListType() const
{
  return GWorld->GetListType(GetList());
}

static inline float TotalDamageToHit(float value)
{
  return InterpolativC(value,0.8,1.0,0,1);
}

float Entity::GetHit(int index) const
{
  float hit = TotalDamageToHit(GetTotalDamage());
  if (index >= 0 && index < _hit.Size()) saturateMax(hit, _hit[index]);
  // discrete hit simulation
  return hit >= 0.9 ? 1 : 0;
}

float Entity::GetHitCont(int index) const
{
  float hit = TotalDamageToHit(GetTotalDamage());
  if(index >= 0) saturateMax(hit, _hit[index]);
  return floatMin(hit,1);
}

float Entity::GetMaxHitCont() const
{
  float maxHit = 0;
  for(int i = 0; i<_hit.Size(); i++)
  {
    maxHit =floatMax(maxHit,_hit[i]);
  }
  return maxHit;
}

float Entity::GetHitContRaw(int index) const
{  
  return _hit[index];
}

void Entity::SetHitContRaw(int index, float value)
{  
  _hit[index] = value;
}

/*!\param kind
* - 0 hull
* - 1 engine
* - 2 left track
* - 3 right track
* - 4 turret
* - 5 gun
*/

float Entity::GetHitForDisplay(int kind) const
{
  return 0;
}

void Entity::HitDestructionEffects(EntityAI *killer, int i)
{
  Entity *effects = Type()->_hitPoints[i].CreateDestructionEffects(this,killer,true);
  if (effects)
  {
    effects->SetPosition(FutureVisualState().Position());
    GWorld->AddAnimal(effects);
    _hitEffects[i] = effects;
    IExplosion *exp = effects->GetExplosionInterface();
    if (exp)
      exp->Explode();
  }
}

inline bool CheckHit(float oldHit, float newHit, float boundary) {return (oldHit < boundary) && (newHit >= boundary);}

void Entity::ChangeHit(EntityAI *killer, int part, float newHit, bool showDamage)
{
  saturateMin(newHit, 1.0f);
  if (showDamage)
  {
    float oldHit = GetHitCont(part);
    _hit[part] = newHit;
    float newHit = GetHitCont(part);
    if (CheckHit(oldHit, newHit, 0.5f) || CheckHit(oldHit, newHit, 0.7f) || CheckHit(oldHit, newHit, 0.7f))
      ShowDamage(part, killer);
    if (CheckHit(oldHit, newHit, 1.0f))
      // crossing the destruction state - we may need to spawn some effects
      HitDestructionEffects(killer, part);
  }
  else
    _hit[part] = newHit;
}

void Entity::ReactToDamage()
{
}

void Entity::ShowDamage(int part, EntityAI *killer)
{
  // run destruction handler 
}

inline float CalcHitDamage(float distance2, float valRange2)
{
  return (distance2 > valRange2) ? Square(valRange2) / Square(distance2) : 1.0f;
}

#if 0 // _PROFILE
#pragma optimize("",off)
#endif

float Entity::LocalHit(DoDamageResult &result, Vector3Par pos, float val, float valRange)
{
  // scan all hitpoints and damage them
  const Shape *hitShape=_shape->HitpointsLevel();
  if(!hitShape)
    return 1.0f;

  //LogF("%s hit (%.1f,%.1f,%.1f) (val %.2f,%.2f)",(const char *)GetDebugName(),pos[0],pos[1],pos[2],val,valRange);

  AnimationContextStorageGeom storage;
  AnimationContext animContext(RenderVisualState(),storage);
  hitShape->InitAnimationContext(animContext, NULL, true); // no convex components for hitpoints level

  // TODO: rename to FindHitpointsLevel
  Animate(animContext, _shape->FindHitpoints(), false, this, -FLT_MAX);

  if (valRange<0)
  {
    // note: this change of local dammage model was done
    // to improve hit locality for soldiers
    // time showed in is suitable for all vehicles
    valRange *= 0.25; // smaller area around direct hit
    //val *= 2; // but stronger effect
  }

  float valRange2=Square(valRange);
  float scale = InvSqrt(valRange2);
  // simulate damage absorption by hitpoints (how much is passed through)
  float passThrough = 1;
  const HitPointList &hitpoints = Type()->GetHitPoints();
  for( int i=0; i<hitpoints.Size(); i++ )
  {
    const HitPoint &hit = hitpoints[i];
    int index=hit.GetSelection();
    if (index<0) continue;
    const NamedSelection::Access sel(hitShape->NamedSel(index),_shape,hitShape);
    if (sel.Size()<=0) continue;
    // calculate minimal, maximal and average distance
    float minDist2 = FLT_MAX;
    float avgDist2 = 0;
    float maxDist2 = 0;

    AutoArray<Vector3, MemAllocLocal<Vector3,32,AllocAlign16> > hitpointPos;
    hitpointPos.Resize(sel.Size());
    Vector3 minHit(+FLT_MAX,+FLT_MAX,+FLT_MAX),maxHit(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    
    // analyze the hitpoint dimensions
    for( int j=0; j<sel.Size(); j++ )
    {
      int pIndex=sel[j];
      Vector3Val hitPt = animContext.GetPos(hitShape, pIndex);
      hitpointPos[j] = hitPt;
      
      float distance2=hitPt.Distance2(pos);
      if (minDist2>distance2) minDist2 = distance2;
      if (maxDist2<distance2) maxDist2 = distance2;
      avgDist2 += distance2;
      
      SaturateMin(minHit,hitPt);
      SaturateMax(maxHit,hitPt);
    }
    avgDist2 /= sel.Size();
    
    // find sphere approximating the hitpoint, assume sphere center in the min-max center
    Vector3 hitCenter = (minHit+maxHit)*0.5f;
    float radius2 = 0;
    for( int j=0; j<hitpointPos.Size(); j++ )
    {
      Vector3Val hitPt = hitpointPos[j];
      saturateMax(radius2,hitPt.Distance2(hitCenter));
    }
    
    float hitRadius = sqrt(radius2);
    // if radius is too small, assume a proportional part of the object size
    float minHitRadius = CollisionSize()*0.05f;
    hitRadius = floatMax(hitRadius,minHitRadius);
    
    // compute sphere-sphere intersection volume, relative to the explosion size
    // - http://mathworld.wolfram.com/Sphere-SphereIntersection.html
    // - http://www.gamedev.net/community/forums/topic.asp?topic_id=443124
    float hitpointVolume = hitRadius*hitRadius*hitRadius*scale*scale*scale;
    
    float hitMin=CalcHitDamage(minDist2,valRange2);
    float hitAvg=CalcHitDamage(avgDist2,valRange2);
    float hitMax=CalcHitDamage(maxDist2,valRange2);
    float hitRes = (hitMin+hitAvg+hitMax)*(1.0f/3);
    
    float hitVal = hitRes*val*hit.GetInvArmor();
    // approximate consumed energy as computed hit value multiplied by a normalized volume of the hitpoint
    float hitEnergy = hitRes*hitpointVolume;
    // total energy: volume integral over whole space, use Mathematica or http://www.wolframalpha.com:
    // Integrate[sin(z)*x^2/(x^4), {x, 1, Infinity}, {y, 0, 2*pi}, {z, 0, pi}] + 4/3*pi
    // Integrate[sin(z)*x^2/((x^4+1+abs(x^4-1))/2), {x, 0, Infinity}, {y, 0, 2*pi}, {z, 0, pi}]
    // - result is (16 pi)/3
    const float totalEnergy = 16*H_PI/3;
    
    float hitPerTotal = floatMin(hitEnergy/totalEnergy,1);
    
    Assert(hitMin<=1.01f);
    passThrough = floatMin(passThrough,(1-hitPerTotal)+hit.GetPassThrough()*hitPerTotal);
    if (hitVal>=hit.GetMinHit())
    {
      result.Access(i);
      result.hits[i] += hitVal;
    }
  }
  Deanimate(_shape->FindHitpoints(),false);
  return Type()->GetStructuralDamageCoef()*passThrough;
}

float Entity::DirectLocalHit(DoDamageResult &result, int component, float val)
{
  if (component<0) return 1;
  // scan for corresponding hitpoint
  const HitPointList &hitpoints = Type()->GetHitPoints();
  for( int i=0; i<hitpoints.Size(); i++ )
  {
    const HitPoint &hit = hitpoints[i];
    if (!hit.IsConnectedCC(component)) continue;

    float hitVal=val*hit.GetInvArmor();
    //LogF("Hit %g, val %g, armor %g",hitVal,val,hit.GetArmor());

    if (hitVal>1e-4f)
    {
      result.Access(i);
      result.hits[i] += hitVal;

      //ChangeHit(i,newHit,true);

#if 0
      int index=hit.GetSelection();
      Shape *hitShape=_shape->HitpointsLevel();
      if (index>=0 && hitShape)
      {
        const NamedSelection &sel=hitShape->NamedSel(index);
        LogF("Direct local hit on %s: %.2f",sel.Name(),newHit);
      }
      else
        LogF("Direct local hit on hitpoint %d: %.2f",i,newHit);
#endif
    }
    return hit.GetPassThrough();
  }
  return 1;
}

void Entity::UpdateHitDependencies(EntityAI * owner)
{
  // we may need to recalculate all hitpoints for induced values
  for (int i=0; i<_hit.Size(); i++)
  {
    float induced = Type()->_hitPoints[i].EvaluateDepends(GetTotalDamage(),_hit);
    if (induced>_hit[i])
      ChangeHit(owner,i,induced,true);
  }
}

void Entity::ApplyDoDamageLocal(const DoDamageResult &result, EntityAI *owner, RString ammo, const SkinnedPosition &pos)
{
  DoAssert(GetNetworkManager().IsServerOwner());
  // apply hit information first
  Assert(result.hits.Size()<=_hit.Size());

  // apply the structural damage

  bool handled;
  float oldDamage = GetRawTotalDamage();
  float newDamage = HandleDamage(-1, oldDamage + result.damage, owner, ammo, pos, handled);
  // allow the handler to limit the damage, but never to heal a unit which has already been damaged
  if (handled)
  {
    // base::ApplyDoDamageLocal is interested only in total value, hitpoints are handled separately
    DoDamageResult resTotal;
    if (newDamage<oldDamage) newDamage = oldDamage;
    // adjust what we pass to the  ApplyDoDamageLocal 
    resTotal.damage = newDamage-oldDamage;
    base::ApplyDoDamageLocal(resTotal,owner,ammo,pos);
  }
  else
    base::ApplyDoDamageLocal(result,owner,ammo,pos);
  
#if _EXT_CTRL //ext. Controller like HLA, AAR
  //  LogF("ApplyDoDamageLocal to %s", cc_cast(Type()->GetName()));
  if(!IsExternalControlled())//no dammage for HLA units
  {
#endif

    for (int i=0; i<result.hits.Size(); i++)
    {
      if(i >= _hit.Size())
        break;
      float oldVal = _hit[i];
      bool dummy;
      float newVal = HandleDamage(i,oldVal + result.hits[i], owner, ammo, pos, dummy);
      // allow the handler to limit the damage, but never to heal a unit which has already been damaged
      if (newVal<oldVal) newVal = oldVal;
      ChangeHit(owner,i,newVal,true);
    }
    
    // give opportunity to set isDead flag
    ReactToDamage();

#if _EXT_CTRL //ext. Controller like HLA, AAR
  }
#endif
  UpdateHitDependencies(owner);
}

void Entity::Repair(float ammount)
{
  base::Repair(ammount);
  // repair all hitpoints gradually
  float totalDammage = GetTotalDamage();
  for( int i=0; i<_hit.Size(); i++ )
  {
    if (totalDammage<_hit[i])
      _hit[i] = totalDammage;
    if (_hit[i]<1)
    {
      if (_hitEffects[i])
        _hitEffects[i]->SetDelete();
    }
  }
}

void Entity::SetTotalDamageHandleDead(float damage, bool setHitpoints)
{
  if (setHitpoints) for (int i=0; i<_hit.Size(); i++) _hit[i] = damage;
  base::SetTotalDamageHandleDead(damage,setHitpoints);
  UpdateHitDependencies(NULL);
}

Object::Visible Entity::VisibleStyle() const
{
  if (_invisible) return ObjInvisible;
  return base::VisibleStyle();
}

bool Entity::HasGeometry() const
{
  return base::HasGeometry() && !_invisible;
}

bool Entity::OcclusionFire() const
{
  return base::OcclusionFire() && !_invisible;
}
bool Entity::OcclusionView() const
{
  return base::OcclusionView() && !_invisible;
}


void Entity::AddForce(Vector3Par pos, Vector3Par force, Color color)
{
  if (!CHECK_DIAG(DEForce))
    return;
  LODShape *forceArrow=GScene->ForceArrow();
  float size = force.Size()*0.05f;
  if (size > 1.0f)
    size = 1.0f;
  else if (size < 1e-4f)
    return; // no need to show zero sized or very small forces

  Ref<Object> arrow = new ObjectColored(forceArrow,VISITOR_NO_ID);
  Vector3 aside = force.CrossProduct(VAside);
  Vector3 zAside = force.CrossProduct(VForward);
  if (zAside.SquareSize() > aside.SquareSize())
    aside = zAside;
  arrow->SetPosition(pos);
  arrow->SetOrient(force, aside);
  arrow->SetPosition(arrow->RenderVisualState().PositionModelToWorld(forceArrow->BoundingCenter() * size));
  arrow->SetScale(size);
  arrow->SetConstantColor(PackedColor(Color(color)));
  //GScene->ObjectForDrawing(arrow);
  GScene->ShowObject(arrow);
}

Matrix4 Entity::ApplySpeed(float deltaT) const
{
  // change position according to speed
  Vector3 position = FutureVisualState().Position();
  position += FutureVisualState()._speed * deltaT;
  // change orientation according to angular velocity; we have to rotate around the center of mass, so this means:
  // translate so that center of mass is in (0,0,0) (using actual orientation), rotate and translate back (using new orientation)
  Matrix3Val origOrientation = FutureVisualState().Orientation();
  Matrix3 orientation = origOrientation;
  orientation += _angVelocity.Tilda() * orientation * deltaT;
  orientation.Orthogonalize();
  position += (origOrientation - orientation) * GetCenterOfMass();
  Matrix4 result;
  result.SetPosition(position);
  result.SetOrientation(orientation);
  return result;
}

#if _VBS2
void Entity::DrawAttached()
{
#if _VBS3_ROPESIM
  if(_attachFlags == ATTSling || _attachFlags == ATTTow)
  {
    if(!_attachedTo) return;
    GVBSVisuals.Add3dLine(
      PositionModelToWorld(_attachedFromPos),_attachedTo->PositionModelToWorld(_attachedToPos),
      PackedColor(0,0,0,255), -0.05f
    );
  }
#endif
}
#endif //VBS2


void Entity::OnAddImpulse(Vector3Par force, Vector3Par torque)
{
}


void Entity::ApplyImpulseAtPoint(Vector3Par cImpulse, Vector3Par cImpulsePoint)
{
    Vector3 wCenter(VFastTransform, FutureVisualState().ModelToWorld(), GetCenterOfMass());
    //AddImpulseNetAware(cImpulse, (cImpulsePoint - wCenter).CrossProduct(cImpulse));    
    FutureVisualState()._speed += cImpulse * GetInvMass();    
    DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);
    _angMomentum += (cImpulsePoint - wCenter).CrossProduct(cImpulse);
    Assert(_angMomentum.IsFinite());
    _angVelocity = _invAngInertia * _angMomentum;
#if ARROWS
    AddForce(cImpulsePoint, cImpulse / 100,Color(0,0,1));
#endif
}

void Entity::ApplyImpulseNetAware(Vector3Par cImpulse, Vector3Par cImpulseTorque)
{    
  if (!IsLocal())
    AddImpulseNetAware(cImpulse, cImpulseTorque);
  ApplyImpulse(cImpulse, cImpulseTorque);       
}

void Entity::ApplyImpulse(Vector3Par cImpulse, Vector3Par cImpulseTorque)
{   
  if (IsLocal())
    OnAddImpulse(cImpulse, cImpulseTorque);
  FutureVisualState()._speed += cImpulse * GetInvMass();    
  DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);
  _angMomentum += cImpulseTorque;
  Assert(_angMomentum.IsFinite());
  _angVelocity = _invAngInertia * _angMomentum;
}


void Entity::AddImpulse(Vector3Par force, Vector3Par torque)
{
  OnAddImpulse(force, torque);
#if 0
  if (!force.IsFinite() || !torque.IsFinite())
  {
    LogF("%s: AddImpulse",(const char *)GetDebugName());
    LogF("  force %g,%g,%g",force[0],force[1],force[2]);
    LogF("  torque %g,%g,%g",torque[0],torque[1],torque[2]);
    return;
  }
#endif
  _impulseForce += force;
  _impulseTorque += torque;
}

#if _ENABLE_CHEATS
  #define ENABLE_STATS 0
#endif

#if ENABLE_STATS
  #include <El/Statistics/statistics.hpp>
  static StatisticsByName impulseStats;
#endif

void Entity::AddImpulseNetAware(Vector3Par force, Vector3Par torque)
{
  if (Static())
    // if object is static, applying any impulse to it has no sense
    return;
#if ENABLE_STATS
  static Time nextReportTime = Glob.time+60;
  if (Glob.time>nextReportTime)
  {
    impulseStats.Report(true);
    nextReportTime = Glob.time+60;
  }
#endif

  // if impulse is very small, do not transmit it
  if (torque.SquareSize()<0.1f && force.SquareSize()<0.1f)
  {
#if ENABLE_STATS
    impulseStats.Count("impulse<0.1");
#endif
    return;
  }
#if ENABLE_STATS
  else if (torque.SquareSize()+force.SquareSize()<1.0f)
    impulseStats.Count("impulse<1.0");
  else if (torque.SquareSize()+force.SquareSize()<10.0f)
    impulseStats.Count("impulse<10.0");
  else if (torque.SquareSize()+force.SquareSize()<100.0f)
    impulseStats.Count("impulse<100.0");
  else
    impulseStats.Count("big impulse");
#endif
  DoAssert(GetNetworkManager().IsServerOwner());
  AddImpulse(force,torque);
}

void Entity::SetAngVelocity(Vector3Val angVelocity, Matrix3Val orientation)
{
  _angVelocity = angVelocity;
  // recalculate inverse angular inertia
  if (_shape)
  {
    // we know matrix it orthonormal - inverting it is easy
    Matrix3 invOrientation = orientation.InverseRotation();
    // recalculate inverse angular momentum for a new orientation
    _invAngInertia = orientation * _shape->InvInertia() * invOrientation;
    // recalculate angular momentum from angular velocity
    Matrix3 angInertia = orientation *_shape->Inertia() * invOrientation;
    _angMomentum = angInertia * _angVelocity;
  }
}

void Entity::ApplyForces(float deltaT, Vector3Par force, Vector3Par torque, Vector3Par friction, Vector3Par torqueFriction, float staticFric, bool aHist)
{
  // integrate quantities
  Vector3 accel = DirectionWorldToModel(force * GetInvMass());
  Vector3 fricAccel = DirectionWorldToModel(friction * GetInvMass());
  Vector3 oldSpeed = FutureVisualState()._speed;
  Vector3 speed(VMultiply, DirWorldToModel(), FutureVisualState()._speed);
  Friction(speed, fricAccel, accel, deltaT);
  FutureVisualState().DirectionModelToWorld(FutureVisualState()._speed, speed);
  FutureVisualState()._acceleration = (deltaT > 0.0f) ? (FutureVisualState()._speed - oldSpeed) / deltaT : VZero;

  static bool UseNewAcceleration = true;

  if (aHist)
  {
    _avgAccels[_avgAccelIndex].dt = deltaT;
    _avgAccels[_avgAccelIndex].speed = FutureVisualState()._speed - oldSpeed;
    _avgAccelIndex++;
    if (_avgAccelIndex >= _avgAccels.Size() - 1) _avgAccelIndex = 0;

    float dt = 0.0f;
    Vector3 dv = VZero;

    for (int i = 0; i < _avgAccels.Size(); i++)
    {
      dt += _avgAccels[i].dt;
      dv += _avgAccels[i].speed;
    }

    Vector3 a = dv / dt;
    
    if (UseNewAcceleration)
      FutureVisualState()._acceleration = a;
  }

  Friction(_angMomentum, torqueFriction, torque, deltaT);
  Assert(_angMomentum.IsFinite());

  // apply impulses
  float rigid = Rigid();
  FutureVisualState()._speed += _impulseForce * (GetInvMass() * rigid);
  _angMomentum += _impulseTorque * rigid;
  Assert(_angMomentum.IsFinite());
  _impulseTorque = VZero;
  _impulseForce = VZero;

  // calculate auxiliary quantities
  Matrix3Val orientation0 = FutureVisualState().Transform().Orientation();
  Matrix3Val invOrientation0 = FutureVisualState().InvTransform().Orientation();
  _invAngInertia = (_shape) ? orientation0 * _shape->InvInertia() * invOrientation0 : M3Zero;
  _angVelocity = _invAngInertia * _angMomentum;

#if 0 //_ENABLE_CHEATS
  if (this == GWorld->CameraOn())
  {
    // debugging tail rotor yaw
    //Assert(fabs(_angVelocity[0])<1e-3f);
    //Assert(fabs(_angVelocity[2])<1e-3f);
    LogF("angVelocity %g,%g,%g",_angVelocity[0],_angVelocity[1],_angVelocity[2]);
    LogF("angMomentum %g,%g,%g",_angMomentum[0],_angMomentum[1],_angMomentum[2]);
    LogF("torque      %g,%g,%g",torque[0],torque[1],torque[2]);
  }
#endif
  // apply static friction; max. speed that can be held by statical friction is staticFric/mass*deltaT
  float staticCanHold = staticFric * GetInvMass() * deltaT;  
  if (FutureVisualState()._speed.SquareSize() < Square(staticCanHold))
    FutureVisualState()._speed = VZero;

  // apply static friction on rotation perpendicular to land
  Vector3 up = orientation0.DirectionUp();
  float angVelocityModel = up * _angVelocity;
  if (_shape && (fabs(angVelocityModel) < staticCanHold / 2.0f))
  {
    _angVelocity -= angVelocityModel * up;
    // recalculate angular momentum
    Matrix3 angInertia = orientation0 * _shape->Inertia() * invOrientation0;
    _angMomentum = angInertia * _angVelocity;
  }
}

Vector3 Entity::SpeedAtPoint(Vector3Par cPoint) const
{    
  Vector3 wCenter(VFastTransform, FutureVisualState().ModelToWorld(), GetCenterOfMass());
  return FutureVisualState()._speed - ((cPoint - wCenter).CrossProduct(_angVelocity));
}

Vector3 Entity::SpeedAtPoint(Matrix4Par placerTrans, Vector3Par point) const
{    
  Vector3 wCenter(VFastTransform, FutureVisualState().ModelToWorld(), GetCenterOfMass());
  wCenter = point - placerTrans * wCenter;
  return placerTrans.Rotate(FutureVisualState()._speed) - wCenter.CrossProduct(placerTrans.Rotate(_angVelocity));
}

Vector3 Entity::DiffAccelerationAtPointOnForceAtPoint(Vector3Par cAccelPoint, Vector3Par cForce, Vector3Par cForcePoint) const
{
  Vector3 wCenter(VFastTransform, FutureVisualState().ModelToWorld(), GetCenterOfMass());
  return cForce * InvMass() + (_invAngInertia * ((cForcePoint - wCenter).CrossProduct(cForce))).CrossProduct(cAccelPoint - wCenter);
}

Vector3 Entity::DiffAccelerationOnForce(Matrix4Val trans, Vector3Par point, Vector3Par force) const
{
  Vector3 wCenter = point - Vector3(VFastTransform,trans,GetCenterOfMass());  
  Matrix3 actualInvAngInertia = trans.Orientation() * _invAngInertia * trans.Orientation().InverseRotation();
  return force * InvMass() + (actualInvAngInertia * wCenter.CrossProduct(force)).CrossProduct(wCenter);
}

// FIXME: why this implementation ignores Rigid() (compare to void Entity::ApplyForces())?
void Entity::ApplyImpulses()
{
  FutureVisualState()._speed += _impulseForce * GetInvMass();    
  DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);
  _angMomentum += _impulseTorque;
  Assert(_angMomentum.IsFinite());
  _angVelocity = _invAngInertia * _angMomentum;
  _impulseForce = VZero;
  _impulseTorque = VZero;
}

inline void SaturateAbs(float &v, float m)
{
  if (fabs(v) > fabs(m))
    v = fSign(v) * fabs(m);
}

void SaturateAbs(Vector3 &v, const Vector3 &m)
{
  SaturateAbs(v[0], m[0]);
  SaturateAbs(v[1], m[1]);
  SaturateAbs(v[2], m[2]);
}

#if _DEBUG
#define LOG_FRIC 0
#endif

void Entity::ApplyForcesAndFriction(float deltaT, Vector3Par force, Vector3Par torque, const FrictionPoint *fric, int nFric)
{
  // add parameter determining friction strength
  float frictionCoef = 15;
  float angularFrictionCoef = 1.0f;

  // integrate quantities
  Vector3 wAccel = force * GetInvMass();
  //Vector3 accel=DirectionWorldToModel(force*GetInvMass());
  Vector3 oldSpeed = FutureVisualState()._speed;
  Vector3 speed(VMultiply, DirWorldToModel(), FutureVisualState()._speed);

  //Friction(speed,VZero,accel,deltaT);
  _angMomentum += torque * deltaT;
  Assert(_angMomentum.IsFinite());

  FutureVisualState()._speed += wAccel * deltaT;

  float rigid = Rigid();
  FutureVisualState()._speed += _impulseForce * (GetInvMass() * rigid);
  _angMomentum += _impulseTorque * rigid;
  Assert(_angMomentum.IsFinite());

  _impulseTorque = VZero;
  _impulseForce = VZero;

  // calculate auxiliary quantities
  Matrix3Val orientation0 = FutureVisualState().Transform().Orientation();
  Matrix3Val invOrientation0 = FutureVisualState().InvTransform().Orientation();
  _invAngInertia = (_shape) ? orientation0 * _shape->InvInertia() * invOrientation0 : M3Zero;
  _angVelocity = _invAngInertia * _angMomentum;

#if 0 //ARROWS
  {
    Vector3 wCenter(VFastTransform,ModelToWorld(),GetCenterOfMass());
    Vector3 pCenter(1,1,1);
    Vector3 radVel = _angVelocity.CrossProduct(pCenter);
    Vector3 actVel = _speed + radVel;
    AddForce(wCenter+pCenter, radVel*2, Color(0,0.5,0,0.5));
    AddForce(wCenter+pCenter, actVel*2, Color(0,1,0,0.5));
    //AddForce(wCenter+pCenter, -pVelChange*(mass/deltaT), full ? Color(1,0,0,0.5) : Color(1,0.5,0,0.5));
  }
#endif

#if 1
  if (nFric>0)
  {
    //AUTO_STATIC_ARRAY(Vector3,pVelChange,128);
    //pVelChange.Resize(nFric);
    /*
    LogF("* deltaT %.3f, NF %d",deltaT,nFric);
    LogF("Old     V %.2f,%.2f,%.2f size %.2f",_speed[0],_speed[1],_speed[2],_speed.Size());
    LogF("    ang M %.2f,%.2f,%.2f size %.2f",_angMomentum[0],_angMomentum[1],_angMomentum[2],_angMomentum.Size());
    LogF("    ang V %.2f,%.2f,%.2f size %.2f",_angVelocity[0],_angVelocity[1],_angVelocity[2],_angVelocity.Size());
    */

    //Vector3 velChange(VZero);
    //Vector3 angMomChange(VZero);
    //Vector3Val com = GetCenterOfMass();
    //Matrix3 orientation=Orientation();
    //Vector3Val translateCOM=orientation*com;
    float mass = GetMass();

    //LogF("_speed %.3f,%.3f,%.3f",_speed[0],_speed[1],_speed[2]);
    //LogF("_angMomentum %.3f,%.3f,%.3f",_angMomentum[0],_angMomentum[1],_angMomentum[2]);

    Vector3 wCenter(VFastTransform, FutureVisualState().ModelToWorld(), GetCenterOfMass());
    // if there is more of them, each one 
    /**/
    float sumFricCoef = 0.0f;
    for (int f = 0; f < nFric; f++)
    {
      const FrictionPoint &fp = fric[f];
      sumFricCoef += fp.frictionCoef;
    }
    // sum of all frictionFactors is in range 0 .. 4
    // typically sumFricCoef is around 1
    // we may need it in range 0 .. 1
    // note: it may be higher when in water
    float invSumFricCoef = (sumFricCoef > 1.0f) ? 1.0f / sumFricCoef : 1.0f;
    /**/

    //float speedSize = _speed.Size()*1.2f;
    float angMomFriction = 0.0f;
#if 0
    float energy = _speed * _speed + _angVelocity * _angMomentum;
    float energyTr = _speed * _speed;
    float energyRot = _angVelocity * _angMomentum;
#endif
 
    for (int f=0; f<nFric; f++)
    {
      const FrictionPoint &fp = fric[f];
      Vector3 pCenter = fp.pos-wCenter;
      // calculate actual velocity of the point
      // relPos is in model coordinates
      Vector3 actVel = SpeedAtPoint(fp.pos);
      if (fp.obj && !fp.obj->Static() && dyn_cast<Entity>(fp.obj))
        actVel -= static_cast<Entity*>(fp.obj)->SpeedAtPoint(fp.pos);
      //Vector3 actVel = _speed;
      // apply friction opposite to actVel
#if ARROWS
        AddForce(wCenter+pCenter, actVel*2, Color(0,0,1,0.25));
#endif

      Vector3 actVelVert = fp.outDir*(actVel*fp.outDir);
      //Vector3 actVelVert = VZero;
      Vector3 actVelHoriz = actVel - actVelVert;

      float speedFrac = 30*deltaT;
      // friction may absorb max. all speed, not more
      saturateMin(speedFrac,1);

      float frictionCoefNorm = invSumFricCoef*fp.frictionCoef;

      Vector3 velChange = actVelVert*(speedFrac*frictionCoefNorm);

      Vector3 vDelta = sign(actVelHoriz)*frictionCoefNorm*frictionCoef*deltaT;

      SaturateAbs(vDelta, actVelHoriz*floatMin(frictionCoefNorm,1));

      velChange += vDelta;  

      // because of rotation effect result velocity change can be bigger.
      Vector3 realVelChange = DiffAccelerationAtPointOnForceAtPoint(fp.pos, velChange * mass, fp.pos);
      if (realVelChange.SquareSize() > velChange.SquareSize())
        // it is not physically correct, but lets use it like a approximation
        velChange *= velChange.Size() / realVelChange.Size(); 

      // Apply changes immediately so next points will have actual speed... 
      FutureVisualState()._speed -= velChange;
      _angMomentum -= pCenter.CrossProduct(velChange * mass);
      _angVelocity=_invAngInertia*_angMomentum;

      angMomFriction += frictionCoefNorm*angularFrictionCoef*deltaT; 

#if ARROWS
        AddForce(wCenter+pCenter, -velChange*(1.0/deltaT), Color(1,0,0,0.5));
#endif
    }  
    // cooling... It has no real physical background... just cooling to achieve more stability
    _angMomentum *= 1-floatMin(angMomFriction,1);
    _angVelocity=_invAngInertia*_angMomentum;

#if 0
    float energyAfter = _speed * _speed + _angVelocity * _angMomentum;
    if (energyAfter > energy * 1.1f)
    {
      LogF("energy %lf, energyAfter %lf, tr (%lf, %lf), rot (%lf, %lf)", energy, energyAfter, energyTr, _speed * _speed, energyRot,_angVelocity * _angMomentum);      
      float koef = sqrt(energy / energyAfter);
      _speed *= koef; 
      _angVelocity *= koef;
      _angMomentum *= koef; 
    }
#endif

    // note: we could check energy conservation here
    // we have speed changes (acceleration*time) in all points
    // special case: if angular velocity is very low,
    // total speed change should not be bigger than total speed

    /*for (int f=0; f<nFric; f++)
    {
      const FrictionPoint &fp = fric[f];
      Vector3 pCenter = fp.pos-wCenter;

      velChange -= pVelChange[f];
      // calculate partical change
      Vector3 pAngMomChange = pCenter.CrossProduct(pVelChange[f]*mass);
      angMomChange -= pAngMomChange;
      //LogF("  actVel %.3f,%.3f,%.3f",actVel[0],actVel[1],actVel[2]);
      //LogF("  pVelChange %.3f,%.3f,%.3f",pVelChange[0],pVelChange[1],pVelChange[2]);
      //LogF("  pAngMomChange %.3f,%.3f,%.3f",pAngMomChange[0],pAngMomChange[1],pAngMomChange[2]);
    }

#if LOG_FRIC
    Log("_speed %.3f,%.3f,%.3f velChange %.3f,%.3f,%.3f",_speed[0],_speed[1],_speed[2],velChange[0],velChange[1],velChange[2]);
#endif

    _speed += velChange;
    if (_speed.SquareSize()>1e6)
      Log("Rotating fast? %.2f,%.2f,%.2f",_angVelocity[0],_angVelocity[1],_angVelocity[2]);
    _angMomentum += angMomChange;
#if LOG_FRIC
    Log("_angMomentum %.3f,%.3f,%.3f angMomChange %.3f,%.3f,%.3f, fric %.2f",_angMomentum[0],_angMomentum[1],_angMomentum[2],angMomChange[0],angMomChange[1],angMomChange[2], angMomFriction);
#endif

    _angMomentum *= 1-floatMin(angMomFriction,1);
#if LOG_FRIC
    GlobalShowMessage(100,"AM %5.1f, M %5.1f, S %5.1f",_angMomentum.Size(),_speed.Size()*GetMass(),_angMomentum.Size()+_speed.Size()*GetMass());
#endif*/
  }
#endif

  // change momentum and angular momentum
  // momentum changed: recalculate auxiliary quantities
  /*if( _shape )
    _invAngInertia=orientation0*InvInertia()*invOrientation0;
  else
    _invAngInertia=M3Zero;
  _angVelocity=_invAngInertia*_angMomentum;  */

  FutureVisualState()._acceleration = (deltaT > 0.0f) ? (FutureVisualState()._speed - oldSpeed) * (1.0f / deltaT) : VZero;
}

void Friction(float &speed, float friction, float accel, float deltaT)
{
  // part of accel is consumed to overcome friction
  speed += accel * deltaT;
  // friction must be against the speed
  if (speed * friction > 0.0f)
  { 
    float ds = friction * deltaT;
    // friction can never change sign of the speed
    if (fabs(ds) < fabs(speed))
      speed -= ds;
    else
      speed = 0.0f;
  }
}

void Friction(Vector3 &speed, Vector3Par friction, Vector3Par accel, float deltaT)
{
  Friction(speed[0], friction[0], accel[0], deltaT);
  Friction(speed[1], friction[1], accel[1], deltaT);
  Friction(speed[2], friction[2], accel[2], deltaT);
}

AttachedOnVehicle::AttachedOnVehicle(Object *vehicle, Vector3Par pos, Vector3Par dir)
:_vehicle(vehicle),_pos(pos),_isAttached(vehicle!=NULL)
{
  _dir = dir.Normalized();
  if (_isAttached) GWorld->AddAttachment(this);
}

AttachedOnVehicle::~AttachedOnVehicle()
{
  if (_isAttached) GWorld->RemoveAttachment(this);
}

void AttachedOnVehicle::Detach()
{
  if (_isAttached) GWorld->RemoveAttachment(this),_isAttached=false;
  _vehicle = NULL;
}

void AttachedOnVehicle::UpdatePositionTo(Matrix4Par toWorld)
{
  // update absolute position based on relative position
  float scale=Scale();
  SetPosition(toWorld.FastTransform(_pos));
  //_speed=_vehicle->Speed();
  SetOrient(toWorld.Rotate(_dir),toWorld.DirectionUp());
  SetScale(scale);
}

void AttachedOnVehicle::UpdatePosition()
{
  if (_vehicle != NULL && _vehicle->IsInLandscape()) // when in some hierarchy, the root is handling it
  {
    // use RenderVisualState (LightPointOnVehicle is a plain Object)
    UpdatePositionTo(_vehicle->RenderVisualState().Transform());
  }
}

LSError AttachedOnVehicle::Serialize(ParamArchive &ar)
{
  CHECK(ar.SerializeRef("Entity", _vehicle, 1))
  CHECK(::Serialize(ar, "pos", _pos, 1))
  CHECK(::Serialize(ar, "dir", _dir, 1))
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond) UpdatePosition();
  return LSOK;
}

void Object::CamEffectFocus(float &distance, float &blur) const
{
  // by default there is no blur
  distance = 1;
  blur = 0;
}

float Object::CamEffectFOV() const
{
  return 0.7f;
}

bool Object::IsContinuous(CameraType camType) const
{
  return camType==CamGunner || camType==CamGroup;
}

bool Object::IsVirtual(CameraType camType) const
{
  return camType!=CamGunner;
}

Vector3 Object::GetCameraDirection(CameraType camType) const
{
  const VisualState &vs = RenderVisualState();
  return vs.Direction();
}

bool Object::IsVirtualX(CameraType camType) const
{
  // used only is IsVirtual is true
  // if mouse rotates, this should return false
  return true;
}

Vector3 Object::ExternalCameraPosition( CameraType camType ) const
{
  return Vector3(0,2,-20);
}

/**
@param base result is given relative to base - this is to avoid world space
*/
Matrix4 Object::InternalCameraTransform(Matrix4 &base, CameraType camType) const
{
  const VisualState &vs = RenderVisualState();
  base = vs.Transform();
  return InsideCamera(camType);
}

bool Object::HasFlares(CameraType camType) const
{
  return camType!=CamInternal;
}

bool Object::IsExternal(CameraType camType) const
{
  return camType==CamGroup;
}

int Object::InsideViewGeomLOD(CameraType camType) const
{
  if (!_shape)
    return LOD_INVISIBLE;
  // check inside LOD type
  int inside = InsideLOD(camType);
  int view = LOD_INVISIBLE;
  if (inside!=LOD_INVISIBLE && inside>=0)
  {
    if (_shape->IsSpecLevel(inside,VIEW_PILOT))
      view = _shape->FindViewPilotGeometryLevel();
    else if (_shape->IsSpecLevel(inside,VIEW_GUNNER))
      view = _shape->FindViewGunnerGeometryLevel();
    else if (_shape->IsSpecLevel(inside,VIEW_CARGO))
      view = _shape->FindViewCargoGeometryLevel();
  }
  return view;
}

bool Object::IsGunner(CameraType camType) const
{
  return camType==CamGunner || camType==CamInternal || camType==CamExternal;
  //return camType==CamGunner;
}

bool Object::IsTurret(CameraType camType) const
{
  return false;
}

bool Object::ShowAim(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  return true;
}

bool Object::ShowCursor(const TurretContextV &context, const WeaponModePtr &ptr, CameraType camType, Person *person) const
{
  return true;
}

Pair<Vector3,float> Object::ShowDrivingCursor(CameraType camType, Person *person) const
{
  return MakePair(VZero,0.0f);
}

void Object::OverrideCursor(Vector3 &dir) const
{
}

void Object::SimulateHUD(CameraType camType,float deltaT)
{
  // default - no simulation
}

void Object::LimitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  switch( camType )
  {
    case CamInternal:
    case CamExternal:
      saturate(fov,0.42,0.85);
      break;
    default:
      saturate(fov,0.01,1.5);
      break;
  }
  heading = AngleDifference(heading,0);
  saturate(dive,-H_PI*0.3,+H_PI*0.3);
}

void Object::InitVirtual(CameraType camType, float &heading, float &dive, float &fov) const
{
  switch( camType )
  {
    case CamExternal:
    case CamGroup:
      heading=0;
      dive=0.1;
      fov=0.7;
      break;
    default:
      heading=dive=0;
      fov=0.7;
      break;
  }
  if (fabs(heading)>2*H_PI)
    heading = fastFmod(heading, 2*H_PI);
  if (fabs(dive)>2*H_PI)
    dive = fastFmod(dive, 2*H_PI);
}

bool EnableVisualEffects(Vector3Par effPos, SimulationImportance prec, float maxDist)
{
  // on dedicated server there are no visual effects or sound effects
#if _ENABLE_DEDICATED_SERVER
    if (IsDedicatedServer())
      return false;
#endif
  //@{#if _DEDICATED_CLIENT
  if ( IsDedicatedClient() )
    return false;
  //@}
  // check distance from camera
  // to see if visual effect may be visible
  if (maxDist > Glob.config.objectsZ)
    maxDist = Glob.config.objectsZ;
  Vector3 pos = GScene->GetCamera()->Position();
  float dist2 = pos.Distance2(effPos);
  return dist2 < Square(maxDist);
}

bool Entity::EnableVisualEffects(SimulationImportance prec, float maxDist) const
{
  return ::EnableVisualEffects(FutureVisualState().Position(), prec, maxDist);
}

float Entity::HandleDamage(int part, float val, EntityAI *owner, RString ammo, const SkinnedPosition &pos, bool &handlerCalled)
{
  // by default we let the entity to damage as needed
  handlerCalled = false;
  return val;
}

SimulationImportance Entity::CalculateImportance(const Vector3 *viewerPos, int nViewers) const
{
  SimulationImportance maxPrec = WorstImportance();
  SimulationImportance minPrec = BestImportance();
  if (maxPrec<=minPrec)
    return maxPrec;

  SimulationImportance ret = SimulateVisibleNear;
  if(IsSimulationDisabled())
  {
    ret = SimulateInvisibleFar;
  }
  else
  {
    float dist2 = 1e20;
    for (int i=0; i<nViewers; i++)
    {
      float d2=viewerPos[i].Distance2(FutureVisualState().Position());
      saturateMin(dist2,d2);
    }
    float radius = GetRadius();
    if (Square(radius)>=dist2*Square(0.2) && dist2>0)
    {
      float dist=floatMax(dist2*InvSqrt(dist2)-radius,0);
      dist2=Square(dist);
      //LogF("Distance of big object is %.1f (r=%.1f)",dist,radius);
    }
    if (dist2>Square(Glob.config.objectsZ+500))
      ret = SimulateInvisibleFar;
    else if (dist2>Square(Glob.config.objectsZ))
      ret = SimulateInvisibleNear;
    else if (dist2>Square(floatMin(100,Glob.config.objectsZ*0.3)))
      ret = SimulateVisibleFar;
  }

  if (ret>maxPrec)
    ret = maxPrec;
  if (ret<minPrec)
    ret = minPrec;
  if (IsLocal())
    unconst_cast(this)->_prec = ret;
  return ret;
}

void Entity::Init( Matrix4Par pos, bool init )
{
  base::Init(pos, init);
  if(init) ResetAnimationStatus();
  CalculateBoundingInfo();
}

DEF_RSB(slope)
DEF_RSB(placement)
DEF_RSB(vertical)

Plane Entity::LandContactMinY(LODShape *shape) const
{
  int geomLevel = shape->FindLandContactLevel();
  bool landcontact = geomLevel>=0;
  if (geomLevel<0) geomLevel = shape->FindGeometryLevel();
  if (geomLevel>=0)
  {
    const Shape *geom = shape->GetLevelLocked(geomLevel);
    // note: geometry / landcontact may be animated
    // we need to animate it so that is reflects the state of the entity being created
    // this is important for plane landing gears
    if (geom->NVertex()==0)
      // handle a singular case - empty geometry -> return based on model min-max
      return Plane(VUp,-shape->Min().Y());
    
    AnimationContextStorageGeom storage;
    AnimationContext animContext(FutureVisualState(),storage);
    geom->InitAnimationContext(animContext, shape->GetConvexComponents(geomLevel), true); // TODO: check if convex components are used here
    unconst_cast(this)->Animate(animContext, geomLevel, false, unconst_cast(this), -FLT_MAX);

    // if it is a real landcontact, we want to check what Up direction should we have
    
    int bottomMost = -1;
    float minY = FLT_MAX;
    for (int i=0; i<geom->NVertex(); i++)
    {
      float y = animContext.GetPos(geom, i).Y();
      if (minY>y)
      {
        minY = y;
        bottomMost = i;
      }
    }
    DoAssert(minY<FLT_MAX);
      
    Plane plane(VUp,-minY);
    if (landcontact && geom->NVertex()>=3)
    {
      // generic solution
      // with more than 3 points, this is not very easy
      // we want a half-space so that:
      // 1) it contains all other points (no point is out of it)
      // 2) its normal is closest to VUp
      
      // we know it must contain the bottom-most point
      // it also must contain the point which has shallowest angle compared to up up,trans.Direction
      
      Vector3Val pos0 = animContext.GetPos(geom, bottomMost);
      
      int bottomMost2 = -1;
      float minTangentY = FLT_MAX;
      for (int i=0; i<geom->NVertex(); i++) if (i!=bottomMost)
      {
        Vector3Val pos1 = animContext.GetPos(geom, i);
        Vector3 tangent = pos1-pos0;
        Assert(tangent.Y()>=0); // we know pos1 is higher the pos0, which is the bottomMost
        tangent.Normalize();
        if (tangent.Y()<minTangentY)
        {
          minTangentY = tangent.Y();
          bottomMost2 = i;
        }
      }
      
      Vector3Val pos1 = animContext.GetPos(geom, bottomMost2);
      // as the 3rd point check all possibilities
      // complexity is O(N^2) because we need another loop to check if all points are in
      float bestUp = 0;
      for (int i=0; i<geom->NVertex(); i++) if (i!=bottomMost && i!=bottomMost2)
      {
        // now compute a plane going through those three points
        Vector3Val pos2 = animContext.GetPos(geom, i);
        Vector3 normal = (pos1-pos0).CrossProduct(pos2-pos0).Normalized();
        // point orientation is arbitrary - select the one in which normal is pointing more upwards
        if (normal.Y()<0)
          normal = -normal;
        // we can quick reject all variants which are not better than the current one
        if (normal.Y()<=bestUp)
          continue;
        float d = -normal*pos0;
        Plane ijkPlane = Plane(normal,d);
        // now check if the half-space contains all points
        bool allInside = true;
        for (int p=0; p<geom->NVertex(); p++)
        {
          float dist = ijkPlane.Distance(animContext.GetPos(geom, p));
          if (dist<-0.01f)
          {
            allInside=false;
            break;
          }
        }
        if (allInside)
        {
          bestUp = normal.Y();
          plane = ijkPlane;
        }
      }
    }
    unconst_cast(this)->Deanimate(geomLevel,false);
    return plane;
  }
  else if (shape->NLevels()>0)
  {
    ShapeUsed level0 = shape->Level(0);
    return Plane(VUp,-level0->Min().Y());
  }
  else
    return Plane(VUp,-shape->Min().Y());
}

Vector3 Entity::PlacingPoint() const
{
  if (!GetShape())
    return VZero;
  if (!Static())
  {
    // modelUp will contain model-space contact plane
    Plane modelUp =  LandContactMinY(GetShape());
    // compute a point at x=0,z=0
   
    // Vector3(0,y,0)*modelUp.Normal()+modelUp.D()=0
    // y*modelUp.Normal().Y()+modelUp.D() = 0
    
    float y = fabs(modelUp.Normal().Y())>1e-3f ? -modelUp.D()/modelUp.Normal().Y() : 0;
    return Vector3(0,y,0);
  }
  else
    return base::PlacingPoint();
}

void Entity::PlaceOnSurfacePoint(Matrix4 &trans, Vector3Val surfPos, float dx, float dz)
{
  Vector3 pos = surfPos;
  // use min coordinate to avoid submerging landscape
  LODShape *shape = GetShape();
  // check which convention should we use
  // moving vehicles should be adjusted for min. Y

  Matrix4 newTransform;
  // if placement slope or vertical is used, matrix should be vertical
  Vector3 up = VUp;
  if (shape->GetOrHints()&(ClipLandKeep|ClipLandOn)
    || shape->PropertyValue(RSB(placement))==RSB(slope)
    || shape->PropertyValue(RSB(placement))==RSB(vertical)
    || Type()->_placement==PlacementVertical)
  {
    // ClipLandKeep objects up must point up in world space
  }
  else
    up = Vector3(-dx,1,-dz);
  
  if (!Static())
  {
    // modelUp will contain model-space contact plane
    Plane modelUp =  LandContactMinY(shape);

    // move and change orientation
    // note: model with modelUp normal is already aligned and its coordinate space should stay untouched
    // we select a point on the model space contact plane which is close to 0,0
    float y = fabs(modelUp.Normal().Y())>1e-3f ? -modelUp.D()/modelUp.Normal().Y() : 0;
    Vector3 modelPos(0,y,0);
    // find a transformation invModelOrient which brings modelUp to Up
    Matrix3 modelOrient(MUpAndDirection,modelUp.Normal(),VForward);
    Matrix3 invModelOrient(MInverseRotation,modelOrient);
    Matrix3 newOrient = Matrix3(MUpAndDirection,up,trans.Direction())*invModelOrient;  
    newTransform.SetOrientation(newOrient);
    pos -= newOrient*modelPos;
  }
  else
    newTransform.SetUpAndDirection(up,trans.Direction());
  newTransform.SetPosition(pos);
  trans = newTransform;
  base::PlaceOnSurface(trans);
  if (Static())
  {
    pos = trans.FastTransform(GetShape()->BoundingCenter());
    trans.SetPosition(pos);
  }
}

void Entity::PlaceOnSurface(Matrix4 &trans, bool underWater)
{
  if (!GetShape())
    return;
  // find steady position on surface
  // use ground collision test to validate position
  Vector3 pos = trans.Position();
  float dx, dz;
  pos[1] = underWater ? GLandscape->RoadSurfaceY(pos, &dx, &dz) : GLandscape->RoadSurfaceYAboveWater(pos, &dx, &dz, NULL, Landscape::UOWait, this);
  PlaceOnSurfacePoint(trans, pos, dx, dz);
}

DEF_RSB(type)
DEF_RSB(shape)
DEF_RSB(oid)
DEF_RSB(Transform)
DEF_RSB(speed)
DEF_RSB(angMomentum)

float Entity::GetAnimationPhase(EntityVisualState const& vs, RString animation) const
{
  const AnimationHolder &animations = Type()->GetAnimations();
  for (int i=0; i<animations.Size(); i++)
  {
    if (!stricmp(animations[i]->GetName(), animation))
      return Type()->_animSources[i]->GetPhase(vs, this);
  }
  return 0;
}

void Entity::StartAnimator()
{
  Ref<EntityAnimator> anim = new EntityAnimator(this);
  anim->SetPosition(FutureVisualState().Position());
  GWorld->AddAnimal(anim);
}

void Entity::OnAnimationStarted()
{
  // by default all entities have simulation of their own
  Assert(Type()->IsSimulationRequired());
}

void Entity::OnAnimatorTerminated()
{
  Assert(Type()->IsSimulationRequired());
}

void Entity::SetAnimationPhase(RString animation, float phase)
{
  const AnimationHolder &animations = Type()->GetAnimations();
  for (int i=0; i<animations.Size(); i++)
  {
    if (stricmp(animations[i]->GetName(), animation) == 0)
      Type()->_animSources[i]->SetPhaseWanted(this,phase);
  }
}

bool Entity::NeedsPrepareProxiesForDrawing() const
{
  return base::NeedsPrepareProxiesForDrawing();
}

void Entity::PrepareProxiesForDrawing(Object *rootObj, int rootLevel, const AnimationContext &animContext,
  const PositionRender &transform, int level, ClipFlags clipFlags, SortObject *so)
{
  ///entity proxies are rendered using hierarchy walking by default
  base::PrepareProxiesForDrawing(rootObj, rootLevel, animContext, transform,level,clipFlags,so);
}

bool Entity::CanBeInstancedShadow(int level) const
{
#if _ENABLE_SKINNEDINSTANCING
  return true;
#else
  return false;
#endif
}

bool Entity::CanBeInstanced(int level, float distance2, const DrawParameters &dp) const
{
  // entity proxies are usually animated/switched and do matter
#if !_ENABLE_SKINNEDINSTANCING
  if (DrawProxiesNeeded(level)) return false;
#endif
  return base::CanBeInstanced(level, distance2, dp);
}

AnimationStyle Entity::IsAnimated( int level ) const
{
  AnimationStyle anim = base::IsAnimated(level);
  // TODO: handle somehow in EntityAI
  const EntityType *type = Type();
  AnimationStyle animHide = type->_damageInfo && !type->_damageInfo->IsEmpty() ? AnimatedTextures : NotAnimated;
  return anim>animHide ? anim : animHide;
}

void Entity::AnimateBoneMatrix(Matrix4 &mat, const ObjectVisualState &vs, int level, SkeletonIndex boneIndex) const
{
  base::AnimateBoneMatrix(mat,vs,level,boneIndex);

  const EntityType *type = Type();
  
  const AnimationHolder &animations = type->GetAnimations();
  if (boneIndex!=SkeletonIndexNone && animations.Size()>0)
  {
    // ShapeUsed is a relic here, BoneToAnimation no longer needs it
    //ShapeUsed lShape = _shape->Level(level);
    const Skeleton *sk = _shape->GetSkeleton();
    const AutoArray<int> &animIndices = animations.BoneToAnimation(level,boneIndex);
    if (animIndices.Size()>=0)
    {
      Matrix4 baseAnim = MIdentity;
      for (int i=0; i<animIndices.Size(); i++)
      {
        int animIndex = animIndices[i];
        const AnimationType *animType = animations[animIndex];
        Matrix4 animMatrix = type->_animSources[animIndex]->GetAnimMatrix(vs,animType,this,level);
        baseAnim = animMatrix * baseAnim;
      }
      // if there is some parent, animate it as well
      SkeletonIndex parent = sk->GetParent(boneIndex);
      // parent was almost certainly already calculated
      if (parent!=SkeletonIndexNone)
        AnimateBoneMatrix(baseAnim,vs,level,parent);
      mat = baseAnim * mat;
      return;
    }
    // if the animation itself was not found, animate its parent
    // this way a "hole" in the skeleton chain will not break the chain
    SkeletonIndex parent = sk->GetParent(boneIndex);
    if (parent!=SkeletonIndexNone)
    {
      Matrix4 baseAnim = MIdentity;
      AnimateBoneMatrix(baseAnim,vs,level,parent);
      mat = baseAnim*mat;
    }
  }
}

Vector3 Entity::AnimatePoint(const ObjectVisualState &vs, int level, int index) const
{
  if (!_shape->GetSkeleton() || Type()->GetAnimations().Size()==0)
    return base::AnimatePoint(vs,level,index);
  // this is usually used for memory and other permanent LODs
  ShapeUsed shape = _shape->Level(level);

  const AnimationRTWeight &wg = shape->GetVertexBoneRef()[index];
  if( wg.Size()<=0 ) return shape->Pos(index); // this point is not animated
  // TODO: bone blending
  Assert(wg.Size()==1);
  SubSkeletonIndex boneIndex = wg.Get(0).GetSel();
  SkeletonIndex bone = shape->GetSkeletonIndexFromSubSkeletonIndex(boneIndex);
  Matrix4 mat = MIdentity;
  AnimateBoneMatrix(mat,vs,level,bone);
  Vector3Val pos = shape->Pos(index);
  return mat.FastTransform(pos);
}

bool Entity::IsPointAnimated(int level, int index) const
{
  if (!_shape->GetSkeleton() || Type()->GetAnimations().Size()==0)
    return base::IsPointAnimated(level,index);
  ShapeUsed shape = _shape->Level(level);
  const AnimationRTWeight &wg = shape->GetVertexBoneRef()[index];
  return wg.Size()>0;
}

Vector3 Entity::AnimateProxyPoint(const Object *proxy, Vector3Par modelPos)
{
  return modelPos;
}

float Entity::GetTotalDamageCtrl(const ObjectVisualState &vs) const
{
  return GetTotalDamage();
}

ObjectDestroyAnim *Entity::GetDestroyAnimation() const
{
  return _type->_destroyAnim ? _type->_destroyAnim->Validate() : NULL;
}

bool Entity::PrepareShapeDrawMatrices(Matrix4Array &matrices, const ObjectVisualState &vs, LODShape *shape, int level)
{
  const Skeleton *sk = shape->GetSkeleton();
  if (!sk)
  {
    Fail("No skeleton for this shape");
    return false;
  }
  if (base::PrepareShapeDrawMatrices(matrices,vs,shape,level))
    // if there is some destruction animation, we cannot apply user animations
    return true;
  return Type()->_animSources.PrepareShapeDrawMatrices(matrices, shape, level, vs, this, Type()->GetAnimations());
}

void Entity::Animate(AnimationContext &animContext, int level, bool setEngineStuff, Object *parentObject, float dist2)
{
  const EntityType *type = Type();
  // animate textures
  for (int i=0; i<type->_animateTextures.Size(); i++)
    type->_animateTextures[i].animation.AnimateTexture(animContext, _shape, level, _animateTexturesTimes[i]);
  if (!setEngineStuff)
  {
    // we cannot cache animations during simulation, as animations need to change within a frame
    // (there may be multiple simulation steps in one frame)
    // SW animation is used for simulation animations, all rendering animations should use GPU + PrepareShapeDrawMatrices
    if (_shape && _shape->GetSkeleton() && _shape->GetAnimationType()!=AnimTypeNone)
    {
      DoAssert(_shape->IsLevelLocked(level));
      const Shape *shape = _shape->GetLevelLocked(level);
      
      if (shape->GetSubSkeletonSize()>0) // only when there are some animated bones do the animation
      {
        PROFILE_SCOPE_DETAIL_EX(enAni,coll);
        // SW skinning and PrepareShapeDrawMatrices
        MATRIX_4_ARRAY(matrices,128);

        // mismatch of visual states: animContext.GetVisualState() does not match to parentObject
        DoAssert(parentObject == this);
        if (parentObject->PrepareShapeDrawMatrices(matrices, animContext.GetVisualState(), _shape, level))
          AnimationRT::ApplyMatrices(animContext, _shape, level, matrices);
      }
    }
  }
  // animate materials based on hitpoints / total damage
  // check wound level
  if (type->_damageInfo && !type->_damageInfo->IsEmpty())
  {
    // Changed: use hit level 2 at once on the whole model
    float totalDamage = GetTotalDamage();
    for (int i=0; i<type->_hitPoints.Size(); i++)
    {
      const HitPoint &hitpoint = type->_hitPoints[i];
      float hit = GetHitCont(i);
      int hitLevel = 0;
      if (totalDamage >= 1.0) hitLevel = 2;
      else if (hit >= 0.5) hitLevel = 1;
      if (hitLevel > 0)
        hitpoint.ApplyVisual(animContext, _shape, level, hitLevel, dist2);
    }
  }
  base::Animate(animContext, level, setEngineStuff, parentObject, dist2);
}

void Entity::Deanimate(int level, bool setEngineStuff)
{
  base::Deanimate(level,setEngineStuff);
}

float Entity::GetCollisionRadius(const ObjectVisualState &vs) const 
{ 
	float radius = base::GetCollisionRadius(vs); 
	if (Type()->_bounding>=0) 
		return floatMax(radius,_boundingSphere); 
	return radius; 
} 

float Entity::ClippingInfo(const ObjectVisualState &vs, Vector3 *minMax, ClippingType clip) const
{
  if (Type()->_bounding>=0)
  {
    float radius = base::ClippingInfo(vs,minMax,clip);
    SaturateMin(minMax[0],_minmax[0]);
    SaturateMax(minMax[1],_minmax[1]);
    return floatMax(radius,_boundingSphere);
  }
  return base::ClippingInfo(vs,minMax,clip);
}

void Entity::AnimatedBSphere(int level, Vector3 &bCenter, float &bRadius, Matrix4Par pos, float posMaxScale) const
{
  base::AnimatedBSphere(level,bCenter,bRadius,pos,posMaxScale);
  if (Type()->_bounding>=0)
  {
    // we need to merge two spheres
    // to do so we create a new one with center 0,0,0
    // more optimal solution could be possible to find
    float bCenterRadius = bCenter.Distance(pos.Position())+bRadius;
    bRadius = floatMax(_boundingSphere*posMaxScale,bCenterRadius);
    bCenter = pos.Position();
  }
}

EntityActivity *EntityActivity::CreateObject(ParamArchive &ar)
{
  Activity *activity = Activity::CreateObject(ar);
  Assert(dynamic_cast<EntityActivity *>(activity));
  return static_cast<EntityActivity *>(activity);
}

bool Entity::MustBeSaved() const
{
  return _ambient == NULL;
}

extern const float CoefAdvanceUnit;

#if _ENABLE_ATTACHED_OBJECTS
DEF_RSB(attachedTo)
DEF_RSB(attachedOffset)
DEF_RSB(attachedTrans)
DEF_RSB(attachedMemPointIndex)
DEF_RSB(attachedObjects)
#endif


LSError Entity::Serialize(ParamArchive &ar)
{
  ProgressRefresh();

  // FIX - read position before Object::Serialize
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    if (Object::GetType() != Primary)
    {
      if (ar.IsSaving())
      {
        Matrix4 &trans = const_cast<Matrix4 &>(FutureVisualState().Transform());
        CHECK(::Serialize(ar, RSB(Transform), trans, 1))
      }
      else if (ar.GetPass() == ParamArchive::PassFirst)
      {
        Matrix4 trans;
        CHECK(::Serialize(ar, RSB(Transform), trans, 1))
        SetTransformPossiblySkewed(trans);
      }
    }
  }

  //((IDFIX make sure VisitorId and ObjectId are serialized before entering Object::Serialize
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    if (Object::GetType() != Primary)
    {
      if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
      {
        CHECK(_objId.Serialize(ar,RSB(oid), 1));
        // if we are not primary, we should not have a "static object" id
        Assert(!_objId.IsObject());
        if (!_objId.IsObject())
          _id = VisitorObjId(_objId.GetVehId());
      }
    }
  }
  //))IDFIX
  base::Serialize(ar);

  CHECK(ar.SerializeArray("hit", _hit, 1))
  CHECK(ar.SerializeRefs("hitEffects", _hitEffects, 1))
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
  {
    // FIX: _hit, _hitEffects need to be same size as Type()->_hitPoints
    int n = _hit.Size();
    int nNew = Type()->_hitPoints.Size();
    _hit.Realloc(nNew);
    _hit.Resize(nNew);
    for (int i=n; i<nNew; i++)
      _hit[i] = 0;
    _hitEffects.Realloc(nNew);
    _hitEffects.Resize(nNew);
  }

  if (ar.IsSaving())
  {
    const EntityTypeName &name = Type()->GetEntityTypeName();
    RString type = name._className;
    RString shape = name._shapeName;
    CHECK(ar.Serialize(RSB(type), type, 1))
    CHECK(ar.Serialize(RSB(shape), shape, 1))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    ProgressAdvance(CoefAdvanceUnit);
    ProgressRefresh();
  }

  CHECK(ar.Serialize("varName", _varName, 1, RString()))

  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    if (Object::GetType() != Primary)
    {
      CHECK(::Serialize(ar, RSB(speed), FutureVisualState()._speed, 1, VZero))
      CHECK(::Serialize(ar, RSB(angMomentum), _angMomentum, 1, VZero))
      //CHECK(ar.Serialize("moveHeight", _moveHeight, 1))
      //CHECK(ar.Serialize("disableDammageUntil", _disableDamageUntil, 1))
      if (ar.IsLoading())
      {
        // recalculate or initialize auxiliary properties
        FutureVisualState()._invDirty = true;
        SimulationReadyReset(FreeOnDemandFrameID());
        DirectionWorldToModel(FutureVisualState()._modelSpeed, FutureVisualState()._speed);

        Matrix3Val orientation0=FutureVisualState().Transform().Orientation();
        Matrix3Val invOrientation0=FutureVisualState().InvTransform().Orientation();
        if (_shape)
          _invAngInertia=orientation0*InvInertia()*invOrientation0;
        else
          _invAngInertia=M3Zero;
        _angVelocity=_invAngInertia*_angMomentum;
        FutureVisualState()._acceleration = VZero;
        _impulseForce = VZero;
        _impulseTorque = VZero;
        //_moveTrans.SetTransform(Transform());
      }
      CHECK(ar.SerializeRef("hierParent", _hierParent, 1));
      CHECK(ar.SerializeRef("ambient", _ambient, 1));
    }
    CHECK(ar.SerializeEnum("targetSide", _targetSide, 1))
    CHECK(ar.Serialize("invisible", _invisible, 1))
  }
  CHECK(ar.Serialize("activities", _activities, 1))
  if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
  {
    // remove activities with no serialization
    for (int i=0; i<_activities.Size();)
    {
      if (_activities[i])
        i++;
      else
        _activities.Delete(i);
    }
  }

#if _ENABLE_ATTACHED_OBJECTS
  CHECK(ar.SerializeRef(RSB(attachedTo), _attachedTo, 1))
  CHECK(::Serialize(ar, RSB(attachedOffset), _attachedOffset, 1, VZero))
  CHECK(::Serialize(ar, RSB(attachedTrans), _attachedTrans, 1, M3Identity))
  CHECK(ar.Serialize(RSB(attachedMemPointIndex), _attachedMemPointIndex, 1, -1))
  CHECK(ar.SerializeRefs(RSB(attachedObjects), _attachedObjects, 1))
#endif
  if (ar.IsSaving())
  {
    CHECK(ar.Serialize("animationStates", FutureVisualState()._animationStates, 1))
  }
  else
  {
    // avoid changing array size, this would lead to a crash.
    AutoArray<AnimationInstance> animationStates;
    CHECK(ar.Serialize("animationStates", animationStates, 1))

  CHECK(ar.Serialize("animationStates", FutureVisualState()._animationStates, 1))
    for (int i=0; i<FutureVisualState()._animationStates.Size(); i++)
    {
      if (i>=animationStates.Size()) break;
      FutureVisualState()._animationStates[i] = animationStates[i];
    }
  }
  CHECK(ar.Serialize("editorSimulationDisabled",_simulationDisabled,true))

/*
  // NOTE: inside Building::OnAnimationStarted() the Entity::StartAnimator() is called
  // which call GWorld->AddAnimal(anim);
  // Thus World::_animals are set during (phase 1 and 2) World::_animals serialization, which is Error
  if (ar.IsLoading())
  {
    OnAnimationStarted();
  }
*/
  return LSOK;
}

Entity *Entity::CreateObject(ParamArchive &ar)
{
  RString simulation, type, shape;
  //if (ar.Serialize("simulation", simulation, 1) != LSOK) return NULL;
  if (ar.Serialize("type", type, 1) != LSOK) return NULL;
  if (ar.Serialize("shape", shape, 1) != LSOK) return NULL;
  Entity *veh = GWorld->NewNonAIVehicle(type, shape,false);
  if (veh && veh->Object::GetType() == Primary)
    veh->SetType(TypeVehicle);
  return veh;
}

NetworkId Entity::GetNetworkId() const
{
  return (Object::_type == Primary) ? Object::GetNetworkId() : _networkId;
}

#define ENTITY_MSG_LIST(XX) \
  XX(Create, CreateVehicle) \
  XX(UpdateGeneric, UpdateVehicle) \
  XX(UpdateDamage, UpdateDamageVehicle) \
  XX(UpdatePosition, UpdatePositionVehicle)

DEFINE_NETWORK_OBJECT(Entity, base, ENTITY_MSG_LIST)

DEFINE_NET_INDICES_EX(CreateVehicle, NetworkObject, CREATE_VEHICLE_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateVehicle, UpdateObject, UPDATE_VEHICLE_MSG, NoErrorInitialFunc)
DEFINE_NET_INDICES_EX_ERR(UpdateDamageVehicle, UpdateDamageObject, UPDATE_DAMMAGE_VEHICLE_MSG, Entity::CalculateErrorInitialDamage)
DEFINE_NET_INDICES_EX_ERR(UpdatePositionVehicle, NetworkObject, UPDATE_POSITION_VEHICLE_MSG, NoErrorInitialFunc)

NetworkMessageFormat &Entity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  Vector3 temp = VZero;
  switch (cls)
  {
  case NMCCreate:
    NetworkObject::CreateFormat(cls, format);
    CREATE_VEHICLE_MSG(CreateVehicle, MSG_FORMAT)
    break;
  case NMCUpdateGeneric:
    base::CreateFormat(cls, format);
    UPDATE_VEHICLE_MSG(UpdateVehicle, MSG_FORMAT_ERR)
    break;
  case NMCUpdateDamage:
    base::CreateFormat(cls, format);
    UPDATE_DAMMAGE_VEHICLE_MSG(UpdateDamageVehicle, MSG_FORMAT_ERR)
    break;
  case NMCUpdatePosition:
    base::CreateFormat(cls, format);
    UPDATE_POSITION_VEHICLE_MSG(UpdatePositionVehicle, MSG_FORMAT_ERR)
    break;
  default:
    base::CreateFormat(cls, format);
    break;
  }
  return format;
}

void Entity::CancelMoveOutInProgress()
{
  DoAssert(_moveOutState==MOMovingOut);
  _moveOutState=MOIn;
}

void Entity::OnSetDelete()
{
#if _VBS3_LARGE_OBJECTS
  GLandscape->OnObjectMove(this);
#endif
  if (_ambient)
    _ambient->OnEntityLeft(*GScene->GetCamera(),this);
}

void Entity::SetDeleteRaw()
{
  if (_delete)
    return;
  _delete = true;
  MarkForMoveOutAndDeleteProcessing();
}

void Entity::SetDelete()
{
  if (_delete)
    return;
  OnSetDelete();
  SetDeleteRaw();
}

void Entity::SetMoveOut(Entity *parent)
{
  DoAssert(_moveOutState == MOIn);
  _moveOutState = MOMovingOut;
  _hierParent = parent;
  OnHierParentChange();
  MarkForMoveOutAndDeleteProcessing();
}

void Entity::MarkForMoveOutAndDeleteProcessing()
{
  // processing state for MoveOutAndDelete changed - we need to mark the entity for processing
  EntityList *list = GetList();
  if (list)
    // we must not be present in the list yet
    list->AddToMoveOutOrDelete(this);
}

void Entity::SetMoveOutDone(Entity *parent)
{
  // verify we are not present in any list and no processing is therefore needed
  DoAssert(GetList() == NULL);
  DoAssert(_moveOutState == MOIn);
  // mark our state
  _moveOutState = MOMovedOut;
  _hierParent = parent;
  OnHierParentChange();
}

void Entity::SetMoveOutFlag()
{
  // used only to mark a state which is already reached
  // verify we are not present in any list which would require handling
  DoAssert(GetList() == NULL);
  //Assert(_moveOutState == MOIn);
  // _hierParent will be transfered separately - do not set it to NULL
  _moveOutState = MOMovedOut;
}

void Entity::SetHierarchyParent(Entity *parent)
{
  // verify the current state
  DoAssert((_moveOutState == MOMovedOut && GetList() == NULL) || _moveOutState == MOMovingOut);

  _hierParent = parent;
  OnHierParentChange();
}


void Entity::OnHierParentChange()
{
  GetNetworkManager().OnHierParentChange(this);
}

const char *Entity::GetControllerScheme() const
{
  return "Characters";
}

#if 0 // _PROFILE
#pragma optimize("", off)
#endif

static bool CheckCameraOn(Entity *veh)
{
  return (
    veh==GWorld->CameraOn() || veh->GetHierarchyParent()==GWorld->CameraOn() ||
    veh->GetAttachedTo()==GWorld->CameraOn() || veh->ContainsAttached(dyn_cast<Entity>(GWorld->CameraOn()))
  );
}

float Entity::ComputeMaxTimeOffset() const
{
  if (_attachedTo) return _attachedTo->ComputeMaxTimeOffset(); // attached object must be rendered without the same interpolation as its parent
  float maxTimeOffset = _maxTimeOffset;
//   #if 1 // TODO: VSMP - allow interpolation once position updates are handled by simulation
//   if (!IsLocal()) maxTimeOffset = 0;
//   #endif
  if (JumpySim <3)
  {
    if (maxTimeOffset>0) // no sense to compute the condition when we already know the value is zero
    {
      // camera tracked object never uses any interpolation
//       if (this==GWorld->CameraOnVehicle() || GetHierarchyParent() && GetHierarchyParent()==GWorld->CameraOnVehicle()) maxTimeOffset = 0;
      if (GWorld->IsTrackedByCamera(this) || GetHierarchyParent() && GWorld->IsTrackedByCamera(GetHierarchyParent()))
      {
        maxTimeOffset = 0;
      }
    }
  }
  return maxTimeOffset;
}

void Entity::GetCurrentInterpolationInfo(int& index, float& coefficient)
{
  index = _visualStateHistory.Size()-1;
  if (index == 0)
  {
    index = 0; coefficient = 0.0f; // oldest state
    return;
  }

  float simulationSkipped = GetSimulationSkipped();

  float age = simulationSkipped;
  
  float timeOffset = ComputeMaxTimeOffset();

  _timeOffset = timeOffset;

  if (CHECK_DIAG(DEVisualStates) && CheckCameraOn(this))
  {
    float totalAge = simulationSkipped;
    for (int i=0; i<_visualStateHistory.Size(); i++)
    {
      totalAge += static_cast<EntityVisualState*>(_visualStateHistory[i].GetRef())->_deltaT;
    }
    DIAG_MESSAGE_ID(
      5000,ID().Encode(),"%s(%s): timeOff %.4f, age %.3f",
      cc_cast(GetDebugName()),cc_cast(Type()->GetName()),timeOffset,totalAge
    );
  }

  if (age >= timeOffset) {
    // happens naturally for StreetLamp, VASILights, Church with timeOffset=0
//     if (_timeOffset != 0.0f)
//       LogF("CurInterpInfo: newest (%.4f) older than timeOffset (%.4f) for \"%s\" (%p).", age, _timeOffset, cc_cast(Type()->_simName), this);

    coefficient = 0.0f;
    return;  // use newest state
  }

  // Compute: index = index of the newest state that's older than timeOffset, age = its age (in local time)
  while (--index >= 0)
  {
    age += static_cast<EntityVisualState*>(_visualStateHistory[index+1].GetRef())->_deltaT;
    if (age >= timeOffset) break;
  }
  
  if (index<0)
  {  
//    if (age > _timeOffset)
//      LogF("GetCurrentInterpolationInfo: oldest VisualState newer than timeOffset (age=%f > timeOffset=%f).", age, _timeOffset);
//    else  // happens if there's only one state (in which case the object *hasn't started existing* yet and shouldn't be drawn)
//      LogF("GetCurrentInterpolationInfo: single VisualState, no interpolation.");
    index = 0; coefficient = 0.0f; // oldest state
  }
  else
  {
    coefficient = (age - timeOffset) / static_cast<EntityVisualState*>(_visualStateHistory[index+1].GetRef())->_deltaT;
  }
}


CriticalSection Entity::_currentVisualStateCS;

void Entity::CurrentVisualStateHelper() const
{
  // consider: if contention is met, replace with a finer grained lock (e.g. critical section pool)
  ScopeLockSection lock(_currentVisualStateCS);
  if (_frameID != Glob.frameID)
  {
    unconst_cast(this)->CreateCurrentVisualState();
    MemoryPublish();
    _frameID = Glob.frameID;
  }
}

/** TODO: try to avoid virtual dispatch, if possible by using compile time information more extensively */

const ObjectVisualState &Entity::RenderVisualStateRaw() const
{
  // return the value from the current visual state
  if (JumpySim >= 0 && JumpySim <3 && _visualStateHistory.Size()>1 && ComputeMaxTimeOffset()>0)
    return CurrentVisualState();
  else
    return FutureVisualStateRaw();
}

void Entity::CreateCurrentVisualState()
{
  float t;
  int index;
  GetCurrentInterpolationInfo(index, t);
  if (index>0)
  { // delete all states older than the returned index
    _visualStateHistory.Delete(0, index);
    index = 0;
  }
  if (t>0)
  {
    _visualStateHistory[index]->Interpolate(this,*_visualStateHistory[index+1], t, *_currentVisualState);
  }
  else _currentVisualState->Copy(*_visualStateHistory[index]);
  if (CHECK_DIAG(DEVisualStates) && CheckCameraOn(this))
  {
    float age1 = GetSimulationSkipped();
    for (int i=2; i<_visualStateHistory.Size(); i++)
    {
      age1 += static_cast<EntityVisualState *>(_visualStateHistory[i].GetRef())->_deltaT;
    }
    float age0 = age1;
    float ageV = age1;
    if (_visualStateHistory.Size()>=2)
    {
      age0 = age1+static_cast<EntityVisualState *>(_visualStateHistory[1].GetRef())->_deltaT;
      ageV += static_cast<EntityVisualState *>(_visualStateHistory[1].GetRef())->_deltaT*(1-t);
    }
    float age = Lerp(age0,age1,t); // TODO: substitute?
    DIAG_MESSAGE_ID(5000,ID().Encode(),"%20s: %.3f (%.3f..%.3f), %d, %.3f",cc_cast(GetDebugName()),age,age0,age1,index,t);
  }
}

void Entity::AgeVisualStates(float deltaT)
 { 
  PROFILE_SCOPE_DETAIL_EX(enAVS,sim);
  float maxTimeOffset = ComputeMaxTimeOffset();
  if (IsVisualStateHistoryUsed() && maxTimeOffset>0)
  {
    _futureVisualState = CloneFutureVisualState();
    _visualStateHistory.Add(_futureVisualState);
    FutureVisualState()._deltaT = deltaT;

    #if 1
    int index = _visualStateHistory.Size()-1;
    if (index > 0)
    {
      float age = _simulationSkipped-deltaT;

      float timeOffset = maxTimeOffset;

      while (--index >= 0)
      {
        age += static_cast<EntityVisualState*>(_visualStateHistory[index+1].GetRef())->_deltaT;
        if (age >= timeOffset) break;
      }
      }
    if (index>1)
    {
      _visualStateHistory.Delete(0, index-1);
    }
    #endif
  }
  else if (_visualStateHistory.Size() > 1)
    _visualStateHistory.Delete(0, _visualStateHistory.Size()-1);
  // maintain delta history
  _deltaHistory.Add(deltaT);
  if (_deltaHistory.Size()>16) _deltaHistory.Delete(0);
}

float Entity::GetSimulationSkipped() const
{
  if (_attachedTo) return _attachedTo->GetSimulationSkipped();
  float skipped = _simulationSkipped;
  // when parent has skipped the simulation, our simulation is skipped as well (called hierarchically)
  for (Entity *parent = GetHierarchyParent(); parent; parent = parent->GetHierarchyParent())
  {
    skipped += parent->_simulationSkipped;
  }
  return skipped;
}

#if 0 // _PROFILE
#pragma optimize("", on)
#endif


VisualStateAge Entity::GetFutureVisualStateAge() const
{
  if (JumpySim >= 0 && JumpySim < 3 && ComputeMaxTimeOffset()>0)
  {
    return GetSimulationSkipped()-_timeOffset;
  }
  else  
  {
    return 0;
  }
}


VisualStateAge Entity::GetVisualStateAge(const ObjectVisualState &vs) const
{
  if (&vs==&RenderVisualStateRaw())  // bypass ProtectedVisualState check by using Raw
  {
    return 0;
  }
  else
  {
    //Assert(&vs==&FutureVisualStateRaw());  // bypass ProtectedVisualState check by using Raw
    return GetSimulationSkipped()-_timeOffset;
  }
}

const ObjectVisualState &Entity::GetVisualStateByAge(VisualStateAge age) const
{
  VisualStateAge ageOfCurrent = 0;

  if (
    age >= ageOfCurrent
    // when there is no history, current state must be the Future one. We prefer returning Future, as this avoids copying
    && _visualStateHistory.Size()>1
  )
  {
    return CurrentVisualState();
  }
  else
  {
    //DoAssert(age==GetSimulationSkipped());
    return FutureVisualState();
  }
}

void Entity::VisualCut()
{
  // cut is not enough. Object may have changed its position in high level structures (hierarchy parent, landscape grid)
  if (_visualStateHistory.Size()>1 && FutureVisualState()._deltaT!=0.0f)
  {
    _visualStateHistory.Resize(0);
    _visualStateHistory.Add(_futureVisualState);
  }
  _deltaHistory.Clear();
  //if (FutureVisualState()._deltaT != 0.0f) AgeVisualStates(0.0f);
  if (IsLocal())
  {
    _posCut = true;
  }
}


Entity *Entity::CreateObject(NetworkMessageContext &ctx, std::function<Entity *(RString,RString)> create)
{
  PREPARE_TRANSFER(CreateVehicle)

  VehicleListType list;
  if (TRANSF_BASE(list, (int &)list) != TMOK) return NULL;
  RString type;
  if (TRANSF_BASE(type, type) != TMOK) return NULL;
  RString shape;
  if (TRANSF_BASE(shape, shape) != TMOK) return NULL;
  Vector3 position;
  if (TRANSF_BASE(position, position) != TMOK) return NULL;
  Matrix3 orientation;
  if (TRANSF_BASE(orientation, orientation) != TMOK) return NULL;

  Entity *veh = create(type,shape);
  if (!veh) return NULL;
  Matrix4 trans;
  trans.SetOrientation(orientation);
  trans.SetPosition(position);
  veh->SetTransformPossiblySkewed(trans);
  veh->Init(trans, true);
  DoAssert(veh->GetEntityType());
  DoAssert((dyn_cast<Person>(veh)!=NULL)==(veh->GetEntityType()!=NULL && veh->GetEntityType()->IsPerson()));
  if (!dyn_cast<Person>(veh))
  { // Person has a brain - initialize it later, once the brain is created
    veh->OnInitEvent();
  }

  if (veh && veh->Object::GetType() == Primary)
    veh->SetType(TypeVehicle);
  switch (list)
  {
  case VLTVehicle:
    GWorld->AddVehicle(veh);
    break;
  case VLTAnimal:
    GWorld->AddAnimal(veh);
    break;
  case VLTBuilding:
    Fail("Network building creation not supported.");
    //GWorld->AddBuilding(veh->GetObjectId(),veh->Type());
    GWorld->AddSlowVehicle(veh);
    break;
  case VLTSlowVehicle:
    GWorld->AddSlowVehicle(veh);
    break;
  case VLTCloudlet:
    GWorld->AddCloudlet(veh);
    break;
  case VLTFast:
    GWorld->AddFastVehicle(veh);
    GWorld->AddSupersonicSource(veh);
    break;
  case VLTOut:
    GWorld->AddOutVehicle(veh);
    veh->SetMoveOutFlag();
    break;
  default:
    Fail("Bad list type");
    return NULL;
  }

  {
    Ref<Entity> hierParent;
    if (TRANSF_REF_BASE(hierParent, hierParent)==TMOK)
    {
      if (hierParent.NotNull())
      {
        DoAssert(list==VLTOut);
        veh->SetHierarchyParent(hierParent);
      }
    }
  }


  int idVeh;
  if (TRANSF_BASE(idVehicle, idVeh) != TMOK) return NULL;
  if (idVeh >= 0)
  {
    EntityAI *vai = dyn_cast<EntityAI>(veh);
    if (vai)
    {
      // we need other entities nearby to be notified of our presence
      GWorld->OnEntityMovedFar(vai); 
    }
    else
      ErrF("NonAI vehicle where AI expected - %s",(const char *)veh->GetDebugName());
  }

  RString name;
  if (TRANSF_BASE(varName, name) != TMOK) return NULL;
  if (name.GetLength() > 0)
  {
    veh->SetVarName(name);
    GWorld->GetGameState()->VarSet(name, GameValueExt(veh), true, false, GWorld->GetMissionNamespace()); // mission namespace
  }

  NetworkId objectId;
  if (TRANSF_BASE(objectCreator, objectId.creator) != TMOK) return NULL;
  if (TRANSF_BASE(objectId, objectId.id) != TMOK) return NULL;
  veh->SetNetworkId(objectId);
  veh->SetLocal(false);

#if _ENABLE_REPORT
    if (dyn_cast<Transport>(veh))
    {
      bool IsUpdateTransport(NetworkMessageType type);
      DoAssert(IsUpdateTransport(veh->GetNMType(NMCUpdateGeneric)));
    }
#endif
  return veh;
}

Entity *Entity::CreateObject(NetworkMessageContext &ctx)
{
  auto create = [](RString type, RString shape) {return GWorld->NewNonAIVehicleWithID(type, shape, false);};
  return CreateObject(ctx,create);
}
void Entity::DestroyObject()
{
  SetDelete();
}

#if 0 // _PROFILE
#pragma optimize("",off)
#endif

bool Entity::ChildUpdateByParent(const Entity *obj) const
{
  return false;
}


TMError Entity::TransferMsg(NetworkMessageContext &ctx)
{
#if _BINARIZE_REDUCEOBJECT
  Fail("Waring: Object functionality reduced");
#else
  switch (ctx.GetClass())
  {
  case NMCCreate:
    TMCHECK(NetworkObject::TransferMsg(ctx))
    if (ctx.IsSending())
    {
      PREPARE_TRANSFER(CreateVehicle)

      const EntityTypeName &name = Type()->GetEntityTypeName();
      RString type = name._className;
      RString shape = name._shapeName;
      TRANSF_EX(type, type)
      TRANSF_EX(shape, shape)

      Vector3 &position = const_cast<Vector3 &>(FutureVisualState().Position());
      TRANSF_EX(position, position)
      Matrix3 &orientation = unconst_cast(FutureVisualState().Orientation());
      TRANSF_EX(orientation, orientation)
      Ref<Object> hierParent = GetHierarchyParent();
      TRANSF_REF_EX(hierParent, hierParent)
    }
    break;
  case NMCUpdateGeneric:
    TMCHECK(base::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateVehicle)
      TRANSF(simulationDisabled)
      TRANSF_ENUM(targetSide)
      if (ctx.IsSending())
      {
        AUTO_STATIC_ARRAY(float,animations,32);
        animations.Resize(FutureVisualState()._animationStates.Size());
        for (int i=0; i<FutureVisualState()._animationStates.Size(); i++) animations[i] = FutureVisualState()._animationStates[i].GetPhaseWanted();
        TRANSF_EX(animations, animations)
      }
      else
      {
        AUTO_STATIC_ARRAY(float,animations,32);
        TRANSF_EX(animations, animations)
        for (int i=0; i<FutureVisualState()._animationStates.Size(); i++)
        {
          if (i>=animations.Size()) break;
          FutureVisualState()._animationStates[i].SetPhaseWanted(animations[i]);
          OnAnimationStarted();
        }
#if _ENABLE_CHEATS
        _lastGenericUpdate = Glob.time;
#endif
      }
    }
    break;
  case NMCUpdateDamage:
    {
      PREPARE_TRANSFER(UpdateDamageVehicle)
      // transfer hitpoints first, so that base::TransferMsg calling SetDamage already sees their value
      if (ctx.IsSending())
      {
        TRANSF(hit);
      }
      else
      {
        AUTO_STATIC_ARRAY(float,hit,32);
        TRANSF_EX(hit, hit);
        for (int i=0; i<_hit.Size(); i++)
        {
          if (i<hit.Size())
            // we want the effects to be performed only when this message is received in-game
            ChangeHit(NULL,i, hit[i], GetNetworkManager().GetClientState() == NCSPlaying);
        }
      }
      TMCHECK(base::TransferMsg(ctx))
#if _ENABLE_CHEATS
        if (!ctx.IsSending())
          _lastDamageUpdate = Glob.time;
#endif
    }
    break;
  case NMCUpdatePosition:
    {
      bool isFirstReceived = Glob.time>=_maxPredictionTime; // first, or first after a freeze
      bool positionValid = false;
      bool ignoreLocalPosition = false;
      TMCHECK(base::TransferMsg(ctx))
      PREPARE_TRANSFER(UpdatePositionVehicle)
      if (ctx.IsSending())
      {
        int posFlags = 0;
        if (_posCut) posFlags |= SimulatePosCut;
        positionValid = IsInLandscape();
        if (positionValid) posFlags |= SimulatePosValid;
        TRANSF_EX(posFlags, posFlags) // SizedEnum  
        _posCut = false;
      }
      else
      {
#if _ENABLE_CHEATS
        _lastPositionUpdate = Glob.time;
#endif
        int posFlags = 0;
        TRANSF_EX(posFlags, posFlags) // SizedEnum  
        if (posFlags&SimulatePosCut)
        { // force immediate update
          isFirstReceived = true;
          _remoteState.Free();
          VisualCut();
        }
        positionValid = (posFlags & SimulatePosValid) != 0;
        ignoreLocalPosition = GetNetworkManager().IsClient() && GWorld &&
          this == GWorld->GetRealPlayer() && !(posFlags & SimulatePosCut);
      }
      if (ctx.IsSending())
      {
        // some static objects are skewed or scaled. For such we are unable to encode their orientation matrix
        // we therefore encode identity instead and ignore it on the receiving side
        Matrix3 orientation = (Static() && !FutureVisualState().GetOrthonormal()) ? M3Zero : FutureVisualState().Orientation();
        if (IsAttached())
          orientation = _attachedTrans; // for attached objects, transfer _attachedTrans instead of orientation

        TRANSF_EX(orientation, orientation)
        // hide offset used as compensation
        // hiding is included in the position, but we do not want it there
        // as it may have different timing on remote clients
        Vector3 position = FutureVisualState().Position() - HideOffset();
        if (IsAttached())
        {
#if _VBS_TRACKING
          if (_isTracking)
            position = _attachedOffset + _trackerPos;
          else
#endif
            position = _attachedOffset; // for attached objects, transfer _attachedOffset instead of position
        }
        TRANSF_EX(position, position)
        TRANSF_EX(speed, FutureVisualState()._speed)

        // instead of angMomentum transfer angular velocity
        TRANSF(angVelocity)
      }
      else
      {
        RemoteState remoteState;
        TRANSF_EX(orientation,remoteState.orient);
        TRANSF_EX(position, remoteState.pos);
        TRANSF_EX(speed,remoteState.speed);
        TRANSF_EX(angVelocity,remoteState.angVelocity)
        remoteState.time = Glob.time; // TODO: consider more precise timing using message times
        remoteState.posOffset = VZero;
        remoteState.speedOffset = VZero;
        #if ORIENT_PREDICT
        remoteState.orientOffset = M3Zero;
        #endif

        // Read every field above before ignoring an ordinary server snapshot
        // for the owner. Returning early would leave the message stream unread.
        if (ignoreLocalPosition)
        {
          _remoteState.Free();
        }
        else if (positionValid && !IsInLandscape() && AcceptPositionOutsideLandscape())
        {
          // The object is not in the landscape on this client yet. This
          // happens when an item is dropped: the server sends the new
          // transform while the client may still have the item stored in an
          // inventory slot. Store the transform now, so ItemMoveIn() puts the
          // item at the position chosen by the server instead of leaving it
          // at the old inventory transform.
          _remoteState.Free();
          Matrix4 trans = GetTransformFromRemoteState(remoteState.orient,remoteState.pos,FutureVisualState());
          SetTransform(trans);
        }
        else if (positionValid && IsInLandscape()) // positionValid is IsInLandscape() on the sender side
        {
          VisualState &vs = FutureVisualState();
#if  _VBS2
          // real time editor
          if (!IsSimulationDisabled())
          {
#endif
            if (IsAttached())
            {
              _attachedTrans = remoteState.orient;  // for attached objects, transfer _attachedTrans instead of orientation
              _attachedOffset = remoteState.pos;  // for attached objects, transfer _attachedOffset instead of position
              FutureVisualState()._speed = remoteState.speed;
              SetAngVelocity(remoteState.angVelocity,remoteState.orient);
              // TODO: VSMP: use _remoteState for attached object simulation as well
            }
            else if(
              GetNetworkManager().GetClientState()!=NCSPlaying || !RemoteStateSupported()
              ||  
              !IsMoving() // if suspended and we have no reason to wake it up, apply the update directly
              && remoteState.speed.SquareSize()<Square(0.1)
              && remoteState.angVelocity.SquareSize()<Square(0.1)
              //&& remoteState.pos.Distance2(vs.Position())<Square(0.5f)
              //&& remoteState.orient.Distance2(vs.Orientation())<Square(0.1f)
            )
            { // defer processing only in-game, when simulation can take care of it
              _remoteState.Free();
              Matrix4 trans = GetTransformFromRemoteState(remoteState.orient,remoteState.pos,FutureVisualState());
              Move(trans);
            }
            else if (isFirstReceived && !_remoteState)
            {
              Matrix4 trans = GetTransformFromRemoteState(remoteState.orient,remoteState.pos,FutureVisualState());
              Move(trans);
            }
            else
            {
              if (!_remoteState) _remoteState = new RemoteState();
              _remoteState->orient = remoteState.orient;
              _remoteState->pos = remoteState.pos;
              _remoteState->speed = remoteState.speed;
              _remoteState->angVelocity = remoteState.angVelocity;
              _remoteState->time = remoteState.time;
              _remoteState->posOffset = _remoteState->pos-vs.Position();
              _remoteState->speedOffset = _remoteState->speed-vs.Speed();
              Vector3 speedFromPos = _remoteState->posOffset;
              static float maxSpeedFromPos = 1.0f;
              saturate(speedFromPos[0],-maxSpeedFromPos,+maxSpeedFromPos);
              saturate(speedFromPos[1],-maxSpeedFromPos,+maxSpeedFromPos);
              saturate(speedFromPos[2],-maxSpeedFromPos,+maxSpeedFromPos);
              _remoteState->speedOffset += speedFromPos;

              #if ORIENT_PREDICT
              _remoteState->orientOffset = _remoteState->orient-vs.Orientation();
              #endif

              OnMoved();
            }
#if _ENABLE_EDITOR2 && _VBS2
          }
#endif
        }
        /*else
        {
          LogF("Received update about entity %s - not in landscape, Remote=%d,Local=%d",(const char *)GetDebugName(),positionValid,IsInLandscape());
        }*/
      }
    }
    break;
  default:
    TMCHECK(base::TransferMsg(ctx))
    break;
  }
#endif
  return TMOK;
}

#if 0 // _PROFILE
#pragma optimize("",on)
#endif

bool Entity::CheckSendUpdateToServer(NetworkMessageClass cls)
{
  return cls==NMCUpdatePosition;
}

bool Entity::CheckSendUpdateToOwner(NetworkMessageClass cls)
{
  return !Entity::CheckSendUpdateToServer(cls);
}


Time Entity::GetLastSimulationTime() const
{
  return Glob.time-GetSimulationSkipped();
}

/*!
\patch 5156 Date 5/14/2007 by Ondra
- Fixed: MP - more reliable transfer of damage status of vehicles to other clients.
*/
float Entity::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = 0;
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    error += base::CalculateError(ctx);
    {
      PREPARE_TRANSFER(UpdateVehicle)

      ICALCERR_NEQ(bool, simulationDisabled, ERR_COEF_VALUE_MAJOR);

      const VisualState& vs = FutureVisualState();

      AUTO_STATIC_ARRAY(float,animations,32);
      float animError = 0;
      int minSize = std::min(vs._animationStates.Size(), message->_animations.Size());
      animError += vs._animationStates.Size() - minSize + message->_animations.Size() - minSize;
      for (int i=0; i<minSize; i++)
        animError += fabs(vs._animationStates[i].GetPhaseWanted() - message->_animations[i]);
      error += animError * ERR_COEF_VALUE_MAJOR;
    }
    break;
  case NMCUpdateDamage:
    {
      error += base::CalculateError(ctx);
      {
        PREPARE_TRANSFER(UpdateDamageVehicle)
        // hitpoints
        float hitError = 0;
        int minSize = std::min(_hit.Size(), message->_hit.Size());
        hitError += _hit.Size() - minSize + message->_hit.Size() - minSize;
        for (int i=0; i<minSize; i++)
          hitError += fabs(_hit[i] - message->_hit[i]);
        error += hitError * ERR_COEF_STRUCTURE;
      }
    }
    break;
  case NMCUpdatePosition:
    error += base::CalculateError(ctx);
    {
      PREPARE_TRANSFER(UpdatePositionVehicle)

      {
        int posFlags = 0;
        if (_posCut) posFlags |= SimulatePosCut;
        bool positionValid = IsInLandscape();
        if (positionValid) posFlags |= SimulatePosValid;
        error += (message->_posFlags!=posFlags)*ERR_COEF_VALUE_MAJOR;
      }

      if (IsInLandscape() && !Static())
      {
        const VisualState& vs = FutureVisualState();

        //@{ FIX: position used in error computation should match its message relative
        // hide offset used as compensation
        // hiding is included in the position, but we do not want it there
        // as it may have different timing on remote clients
        Vector3 position = vs.Position() - HideOffset();
        if (IsAttached())
        {
#if _VBS_TRACKING
          if (_isTracking)
            position = _attachedOffset + _trackerPos;
          else
#endif
            position = _attachedOffset; // for attached objects, transfer _attachedOffset instead of position
        }
        //@}
        error += message->_position.Distance(position);
        error += message->_orientation.DirectionUp().Distance(vs.Orientation().DirectionUp());
        error += message->_orientation.Direction().Distance(vs.Orientation().Direction());

        Time simTime = message->_simulationTime;
        float dt = Glob.time - simTime;
        error += dt * message->_speed.Distance(vs.Speed());
        error += dt * message->_angVelocity.Distance(AngVelocity());
      }

    }
    break;
  default:
    error += base::CalculateError(ctx);
    break;
  }
  return error;
}

float Entity::CalculateErrorInitial(NetworkMessageClass cls)
{
  float error = Object::CalculateErrorInitial(cls);
  switch (cls)
  {
  case NMCUpdateDamage:
    {
      // hitpoints
      float hitError = 0;
      for (int i=0; i<_hit.Size(); i++) hitError += fabs(_hit[i]);
      error += hitError * ERR_COEF_STRUCTURE;
    }
    break;
  }
  return error;
}

float Entity::CalculateErrorInitialDamage(NetworkMessage *msg)
{
  float error = Object::CalculateErrorInitialDamage(msg);

  NetworkMessageUpdateDamageVehicle *message = static_cast<NetworkMessageUpdateDamageVehicle *>(msg);

  // hitpoints
  float hitError = 0;
  for (int i=0; i<message->_hit.Size(); i++) hitError += fabs(message->_hit[i]);
  error += hitError * ERR_COEF_STRUCTURE;

  return error;
}

void Entity::ScanContactPoints(ContactArray &contacts, const ObjectVisualState &moveTrans, SimulationImportance prec, float above, bool ignoreObjects)
{
#if _BINARIZE_REDUCEOBJECT
  Fail("Waring: Object functionality reduced");
#else

  // check collision on new position

  _objectContact=false;

  if (!ignoreObjects)
  {
    #define MAX_IN 0.2
    #define MAX_IN_FORCE 0.1
    #define MAX_IN_FRICTION 0.2

    CollisionBuffer collision;
    GLandscape->ObjectCollision(collision,this,moveTrans);

    for( int i=0; i<collision.Size(); i++ )
    {
      const CollisionInfo &info = collision[i];
      Object *obj = info.object;
      if (!obj)
        continue;     
      ContactPoint &contact = contacts.Append();
      contact.under = info.under;
      contact.pos = info.pos;
      contact.dirOut = info.dirOut;
      contact.type = GroundSolid;
      contact.obj = info.object;

#if 0
      GScene->DrawCollisionStar(contact.pos,0.1,PackedColor(Color(0,1,0)));
      GScene->DrawCollisionStar(contact.pos+contact.dirOut*contact.under,0.1,PackedColor(Color(1,1,0)));
#endif

      //LogF("obj %d: %.3f",i,info.under);

      _objectContact=true;
    }
  } // if( object collisions enabled )
  
  // check for collisions

  GroundCollisionBuffer gCollision;
  //float softFactor = 0.2; //floatMin(4000/GetMass(),2.0);

  GLandscape->GroundCollision(gCollision,this,moveTrans,above,0,false);

  _landContact=false;
  _waterContact=false;
  for( int i=0; i<gCollision.Size(); i++ )
  {
    const UndergroundInfo &info=gCollision[i];
    ContactPoint &contact = contacts.Append();
    contact.under = info.under;
    contact.pos = info.pos;
    contact.dirOut = Vector3(0,info.dZ,1).CrossProduct(Vector3(1,info.dX,0)).Normalized();
    contact.type = info.type;
    contact.obj = NULL;

#if 0
    GScene->DrawCollisionStar(contact.pos,0.1,PackedColor(Color(0,1,0)));
    GScene->DrawCollisionStar(contact.pos+contact.dirOut*contact.under,0.1,PackedColor(Color(1,1,0)));
#endif

    if (contact.type == GroundWater)
      _waterContact = true;
    else
    {
      //LogF("lnd %d: %.3f",i,info.under);
      _landContact = true;
    }
  }
#endif
}

void Entity::ConvertContactsToFrictions(const ContactArray &contacts, FrictionArray &frictions,
  const ObjectVisualState &moveTrans, Vector3 &offset, Vector3 &force, Vector3 &torque, float crash, float maxColSpeed2)
{
  offset = VZero;
  crash = 0;
  maxColSpeed2 = 0;

#define MAX_UNDER 0.2
  const float maxUnderForce = 0.2;

  // scan all contact points
  // from all ground points select only the one nearest
  //to the center of mass in x,z axes
  // calculate minimal distance factor
  //float minDistXZ2 = FLT_MAX;
  int outForceFactorCount = 0;
  AUTO_STATIC_ARRAY(float,outForceFactor,256);
  // calculate distance to the nearest 
  for( int i=0; i<contacts.Size(); i++ )
  {
    const ContactPoint &info=contacts[i];
    if (info.type==GroundWater) continue;
    outForceFactorCount++;
  }
  float outForceFactorSum = 0;
  outForceFactor.Realloc(contacts.Size());
  outForceFactor.Resize(contacts.Size());
  if (outForceFactorCount>0)
  {
    // calculate distribution
    for( int i=0; i<contacts.Size(); i++ )
    {
      // info.pos is world space
      // select only 
      const ContactPoint &info=contacts[i];
      outForceFactor[i] = 0;
      if (info.type==GroundWater) continue;
      float factor = info.under;
      //LogF("under %i: %.2f, max %.2f",i,info.under,maxUnderForce);
      saturate(factor,0,maxUnderForce);
      outForceFactor[i] = factor*(2.0/maxUnderForce);
      outForceFactorSum += outForceFactor[i];
    }
    // keep outForceFactor in reasonable range
    // one point emerged to maxUnderForce should be able to push 4
    if (outForceFactorSum>4)
    {
      // some point present
      float invSum = 4/outForceFactorSum;
      for (int i=0; i<contacts.Size(); i++)
        outForceFactor[i] *= invSum;
    }
  }

  Vector3 wCenter(VFastTransform,moveTrans.ModelToWorld(),GetCenterOfMass());

#if LOG_FRIC
  float fricFactorSum = 0;
  float accelSum = 0;
#endif
  // when forces are balanced, under should keep on value: 0.5*maxUnderForce
  for (int i=0; i<contacts.Size(); i++)
  {
    // info.pos is world space
    // select only 
    const ContactPoint &info=contacts[i];
    // we consider two forces

    if (info.under<0)
      continue;

    float offsetInOutDir = info.dirOut*offset;
    saturateMax(offsetInOutDir,0);
    float under = info.under - offsetInOutDir;
    if (under<0)
      continue;

    Vector3 pCenter=info.pos-wCenter;

    // one is ground "pushing" everything out
    Vector3Val dirOut = info.dirOut;

    if (info.type==GroundWater)
    {
      // simulate water forces
      // force depends only on under-water volume
      // exact volume would help

      // estimate volume corresponding to each point
      float radius = GetRadius();
      float areaPerPoint = Square(radius) / _shape->GeometryLevel()->NPos();
      Vector3 pForce = dirOut * floatMin(under, radius) * areaPerPoint * 10000.0f;
      torque += pCenter.CrossProduct(pForce);
      force += pForce;

      // add friction point
      FrictionPoint &fp = frictions.Append();
      float contactFactor = under * 8.0f / radius;
      fp.frictionCoef = floatMin(contactFactor, 1.0f);
      fp.obj = info.obj;
      fp.outDir = dirOut;
      fp.pos = info.pos;

    }
    else
    {
#if 1 // enable offsetting (forced moving out of the object)
      if( MAX_UNDER<under )
        offset += info.dirOut*(under-MAX_UNDER);
#endif

      under=floatMin(under,maxUnderForce);

      // sum of all outForceFactors is in range 0 .. 2
      float contactFactor = outForceFactor[i];

      /// wake up body in contact... Will be not used in xbox...
      /*EntityAI * ent;
      if (contactFactor > 0 && info.obj && (ent = dyn_cast<EntityAI>(info.obj)))
      {
        if (ent->GetStopped())
        {                    
          // if there is large enough speed in contact wake it up... 
          const float maxSpeed = 0.1;
          if (SpeedAtPoint(info.pos) * info.dirOut > maxSpeed)
            ent->CancelStop();
        }
      }*/

      // some friction is caused by moving the land aside
      // this applies only to soft surfaces

      float accelSize = 10*contactFactor;
      Vector3 pForce = dirOut*GetMass()*accelSize;

#if LOG_FRIC
        accelSum += accelSize;
#endif

      torque += pCenter.CrossProduct(pForce);
      force += pForce;

#if ARROWS
        AddForce(wCenter+pCenter,pForce*InvMass(),Color(1,1,0));
#endif

      // add friction point
      FrictionPoint &fp = frictions.Append();
      fp.frictionCoef = floatMin(contactFactor,1);
      fp.obj = info.obj;
      fp.outDir = dirOut;
      fp.pos = info.pos;
      //fp.angularFriction = 0.1*fricFactor;
#if LOG_FRIC
        fricFactorSum += fp.frictionCoef;
#endif
    }
  }
#if LOG_FRIC
  Log("fricFactorSum %.2f, accelSum %.2f",fricFactorSum,accelSum);
#endif
}

bool Entity::IsInLandscape() const
{
  return _moveOutState == MOIn;
}

Matrix4 Entity::WorldTransform(ObjectVisualState const& vs) const
{
  // check if we are in landscape
  // if not, we should check our parent
  if (_moveOutState == MOIn)
    return vs.Transform();
  if (!_hierParent)
  {
    LogF("%s: no _hierParent",(const char *)GetDebugName());
    return MIdentity;
  }
  return _hierParent->ProxyWorldTransform(GetVisualStateAge(vs), this);
}

Vector3 Entity::WorldSpeed() const
{
  if (_moveOutState == MOIn)
    return ObjectSpeed();
  if (!_hierParent)
  {
    LogF("%s: no _hierParent",(const char *)GetDebugName());
    return VZero;
  }
  // TODO: add object speed to my speed
  // most often speed of soldier inside of vehicle is negligible, though
  return _hierParent->ObjectSpeed();
}

Vector3 Entity::WorldPosition(ObjectVisualState const& vs) const
{
  if (_moveOutState==MOIn)
    return vs.Position();
  if (!_hierParent)
  {
    LogF("%s: no _hierParent",(const char *)GetDebugName());
    return VZero;
  }
  return _hierParent->ProxyWorldTransform(GetVisualStateAge(vs), this).Position();
}

Matrix4 Entity::WorldInvTransform(ObjectVisualState const& vs) const
{
  if (_moveOutState==MOIn)
    return vs.GetInvTransform();

  if (!_hierParent)
  {
    LogF("%s: no _hierParent",(const char *)GetDebugName());
    return MIdentity;
  }
  // ask parent to get my position
  return _hierParent->ProxyInvWorldTransform(GetVisualStateAge(vs), this);
}

void Entity::ResetMoveOut()
{
  DoAssert(_moveOutState == MOMovedOut);
  _moveOutState = MOIn;
}

void Entity::ResetAnimationStatus()
{
  const EntityType *type = Type();
  for (int i=0; i<type->_animSources.Size(); i++)
  {
    AnimationSource *animSource = type->_animSources[i];
    animSource->Init(this);
  }
}

void Entity::ResetStatus()
{
  base::ResetStatus();
  SetInvisible(false);
  ResetAnimationStatus();
  int hitEfSize = _hitEffects.Size();
  for( int i=0; i<_hit.Size(); i++ )
  {
    _hit[i]=0;
    if (i<hitEfSize && _hitEffects[i]) //FIX: _hit and _hitEffects happened to have different size after Load from Save
      _hitEffects[i]->SetDelete();
  }
  // clear all but the latest
  if (_visualStateHistory.Size()>1)
  {
    _visualStateHistory.DeleteAt(0,_visualStateHistory.Size()-1);
  }
}

void Entity::MoveOut(Entity *parent)
{
  if(ToMoveOut())
  { // already set to move out - only change the parent
    _hierParent = parent;
    OnHierParentChange();
  }
  else
  {
    SetMoveOut(parent); // remove reference from the world
  }
}

const int EffectVariables::_count[EVarSet_COUNT] = {
  EVarBullet_COUNT, EVarExhaust_COUNT, EVarDust_COUNT, EVarWater_COUNT, EVarDamage_COUNT, EVarEffect_COUNT, EVarFire_COUNT,EVarSmokeShell_COUNT,EVarCounterMessure_COUNT
};
const RString EffectVariables::_namesBullet[EVarBullet_COUNT] = { EVAR_SET_BULLET(EVAR_NAME) };
const RString EffectVariables::_namesExhaust[EVarExhaust_COUNT] = { EVAR_SET_EXHAUST(EVAR_NAME) };
const RString EffectVariables::_namesDust[EVarDust_COUNT] = { EVAR_SET_DUST(EVAR_NAME) };
const RString EffectVariables::_namesWater[EVarWater_COUNT] = { EVAR_SET_WATER(EVAR_NAME) };
const RString EffectVariables::_namesDamage[EVarDamage_COUNT] = { EVAR_SET_DAMAGE(EVAR_NAME) };
const RString EffectVariables::_namesEffect[EVarEffect_COUNT] = { EVAR_SET_EFFECT(EVAR_NAME) };
const RString EffectVariables::_namesFire[EVarFire_COUNT] = { EVAR_SET_FIRE(EVAR_NAME) };
const RString EffectVariables::_namesSmokeShell[EVarSmokeShell_COUNT] = { EVAR_SET_SMOKESHELL(EVAR_NAME) };
const RString EffectVariables::_namesCounterMessure[EVarCounterMessure_COUNT] = { EVAR_SET_COUNTER_MESSURE(EVAR_NAME) };
const RString* EffectVariables::_names[EVarSet_COUNT] = {
  _namesBullet, _namesExhaust, _namesDust, _namesWater, _namesDamage, _namesEffect, _namesFire, _namesSmokeShell, _namesCounterMessure
};

int EffectVariables::GetCount(EVarSet set)
{
  Assert((set >= 0) && (set < lenof(_count)));
  return _count[set];
}

const RString* EffectVariables::GetNames(EVarSet set)
{
  Assert((set >= 0) && (set < lenof(_names)));
  return _names[set];
}

int EffectVariables::FindName(RStringVal name, EVarSet set)
{
  const RString *varNames = GetNames(set);
  for (int i = 0; i < _count[set]; i++)
  {
    if (varNames[i] == name)
      return i;
  }
  return -1;
}


EffectVariables::EffectVariables(EVarSet set, float defval)
: _set(set)
{
  int count = GetCount(_set);
  _values = new float[count];
  for (int i = 0; i < count; i++)
    _values[i] = defval;
}

EffectVariables::EffectVariables(const HitInfo& hitInfo, float defval)
: _set(EVarSet_Bullet)
{
  int count = GetCount(_set);
  _values = new float[count];
  for (int i = 0; i < count; i++)
    _values[i] = defval;
  UpdateVector(EVarBullet_surfNormalX, hitInfo._surfNormal);
  UpdateVector(EVarBullet_inSpeed, hitInfo._inSpeed, true);
  UpdateVector(EVarBullet_outSpeed, hitInfo._outSpeed, true);
}

EffectVariables::~EffectVariables()
{
  delete[] _values;
}

float EffectVariables::operator [](int index) const
{
  Assert((index >= 0) && (index < _count[_set]));
  return _values[index];
}

float& EffectVariables::operator [](int index)
{
  Assert((index >= 0) && (index < _count[_set]));
  return _values[index];
}

void EffectVariables::UpdateVector(int startIndex, Vector3Par vec, bool splitSize)
{
  if (splitSize)
  {
    Vector3 v = vec;
    _values[startIndex++] = v.NormalizeSize();
    _values[startIndex++] = v.X();
    _values[startIndex++] = v.Y();
    _values[startIndex++] = v.Z();
  }
  else
  {
    _values[startIndex++] = vec.X();
    _values[startIndex++] = vec.Y();
    _values[startIndex++] = vec.Z();
  }
}

void EffectVariables::UpdateBulletParameters(float intensity, float interval, float fireIntensity, float fireInterval, float lifeTime)
{
  _values[EVarBullet_intensity] = intensity;
  _values[EVarBullet_interval] = interval;
  _values[EVarBullet_fireIntensity] = fireIntensity;
  _values[EVarBullet_fireInterval] = fireInterval;
  _values[EVarBullet_lifeTime] = lifeTime;
}

float EffectVariables::GetValue(ParamEntryPar entry) const
{
  if (entry.IsArray())
  {
    RptF("Polynoms no longer supported - used in %s",cc_cast(entry.GetName()));
    return 0.0f;
  }
  ExpressionCode code;
  float result;
  code.Compile(entry, _names[_set], _count[_set]);
  code.Evaluate(result, _values, _count[_set]);
  return result;
}

void EffectVariables::GetValue(Color &color, ParamEntryPar entry) const
{
  int elems = entry.GetSize();
  if ((elems == 3) || (elems == 4))
  {
    color.SetR(GetValue(entry[0]));
    color.SetG(GetValue(entry[1]));
    color.SetB(GetValue(entry[2]));
    color.SetA((elems == 4) ? GetValue(entry[3]) : 1.0f);
  }
  else
    color = HWhite;
}

float EffectVariables::GetValue(const IParamArrayValue &entry) const
{
  if (entry.IsArrayValue())
  {
    RptF("Polynoms no longer supported");
    return 0.0f;
  }
  ExpressionCode code;
  float result;
  code.Compile(entry, _names[_set], _count[_set]);
  code.Evaluate(result, _values, _count[_set]);
  return result;
}

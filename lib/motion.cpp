#include "wpch.hpp"
#include "diagModes.hpp"

/*!
\file
Implementation file for movement diagram
*/

#include "motion.hpp"
#include <Es/Containers/bankArray.hpp>
#include <Es/Algorithms/qsort.hpp>
#include <Es/Framework/appFrame.hpp>
#include <El/Common/perfProf.hpp>
#include "fileLocator.hpp"
#include "progress.hpp"
#include "timeManager.hpp"
#include "global.hpp"
#include "paramFileExt.hpp"
#include "paramArchiveExt.hpp"

#if 0 // _PROFILE
# pragma optimize( "", off )
#endif

#if _VBS3
#include <El/Debugging/debugTrap.hpp>
#endif

#define NAME_THIS(i) ( (const char *)GetMoveName(i) )
#define NAME_T(t,i) ( (const char *)(t)->GetMoveName(i) )

BankArray<AnimationRT> AnimationRTBank;

const float Moves::_primaryFactor0 = 0.000001f;
const float Moves::_primaryFactor1 = 0.999999f;

// Motion path

MotionPath::MotionPath()
{
}

MotionPath::~MotionPath()
{
}

// Action map

DEFINE_FAST_ALLOCATOR(ActionMap)

ActionMap::ActionMap(const ActionMapName &name)
{
  _name = name;
  // load action map from an entry
  ParamEntryVal e = *_name.entry;

  _turnSpeed = e >> "turnSpeed";
  _upDegree = e >> "upDegree";
  _limitFast = e >> "limitFast";
  _useFastMove = e >> "useFastMove";

  ConstParamEntryPtr leanRRot = e.FindEntry("leanRRot");
  if (leanRRot) _leanRRot = *leanRRot;
  else _leanRRot = 0;

  ConstParamEntryPtr leanRShift = e.FindEntry("leanRShift");
  if (leanRShift) _leanRShift = *leanRShift;
  else _leanRShift = 0;

  ConstParamEntryPtr leanLRot = e.FindEntry("leanLRot");
  if (leanLRot) _leanLRot = *leanLRot;
  else _leanLRot = 0;
  
  ConstParamEntryPtr leanLShift = e.FindEntry("leanLShift");
  if (leanLShift) _leanLShift = *leanLShift;
  else _leanLShift = 0;

  const MotionType *motion = _name.motion;
  const DynEnum &actions = motion->GetActions();
  int n = actions.FirstInvalidValue();

  _actions.Realloc(n);
  _actions.Resize(n);
  for (int i=0; i<n; i++)
  {
    RString actName = actions.GetName(i);
    ParamEntryVal valueE = e >> actName;
    if (valueE.IsArray())
    {
      RStringB value = valueE[0];
      RStringB layerStr = valueE[1];
      // convert layer to enum
      MoveLayer layer = MovePrimary;
      if (strcmpi(layerStr,"primary"))
      {
        Assert(!strcmpi(layerStr,"gesture"));
        layer = MoveGesture;
      }
      _actions[i].layer = layer;
      _actions[i].id = motion->GetMoveId(layer,value);
#if _ENABLE_REPORT
    if (value.GetLength() > 0 && _actions[i].id == MoveIdNone)
    {
      RptF
        (
        "Invalid layered move %s,%s in action %s (%s)",
        (const char *)layerStr,(const char *)value, (const char *)actName, (const char *)e.GetContext()
        );
    }
#endif
    }
    else
    {
      RStringB value = valueE;
      _actions[i].layer = MovePrimary;
      _actions[i].id = motion->GetMoveId(value);
#if _ENABLE_REPORT
    if (value.GetLength() > 0 && _actions[i].id == MoveIdNone)
    {
      RptF
        (
        "Invalid move %s in action %s (%s)",
        (const char *)value, (const char *)actName, (const char *)e.GetContext()
        );
    }
#endif
    }
  }
}

// Blend animations

BlendAnimSelections::BlendAnimSelections()
{
}

static int CompBlendAnim( const BlendAnimInfo *i1, const BlendAnimInfo *i2 )
{
  return GetSkeletonIndex(i1->matrixIndex) - GetSkeletonIndex(i2->matrixIndex);
}

void BlendAnimSelections::Load( Skeleton *skelet, ParamEntryPar cfg )
{
  Clear();
  Realloc(cfg.GetSize()/2);
  for (int i=0; i<cfg.GetSize()-1; i+=2 )
  {
    RString namelow = cfg[i];
    namelow.Lower();
    RStringB name = namelow;
    float factor = cfg[i+1];
    BlendAnimInfo &info = Set(Add());
    // search skeleton
    SkeletonIndex index = skelet->FindBone(name);

    if (GetSkeletonIndex(index) < 0)
    {
      RptF("Error: Bad bone name %s in %s",cc_cast(name),cc_cast(cfg.GetContext()));
      info.matrixIndex = SkeletonIndexNone;
      info.factor = 0.0f;
      continue;
    }
    //info.name = name;
    info.matrixIndex = index;
    info.factor = factor;
  } 
  Compact();
  // sort by matrix index
  QSort(Data(),Size(),CompBlendAnim);
}

void BlendAnimSelections::AddOther( const BlendAnimSelections &src, float factor )
{
  // walk through this and src
  // both are sorted by size
  // if we always process the lower one, we stay in sync
  int i = 0, srcI = 0;

  while (i<Size() && srcI<src.Size())
  {
    BlendAnimInfo &info = Set(i);
    const BlendAnimInfo &srcInfo = src.Get(srcI);
    if (info.matrixIndex==srcInfo.matrixIndex)
    {
      // same matrix - blend and advance in both 
      // blen
      info.factor += srcInfo.factor*factor;
      i++;
      srcI++;
    }
    else if (GetSkeletonIndex(info.matrixIndex) > GetSkeletonIndex(srcInfo.matrixIndex))
    {
      // src should be processed now
      BlendAnimInfo &set = Set(Add());
      set.matrixIndex = srcInfo.matrixIndex;
      set.factor = srcInfo.factor*factor;
      srcI++;
    }
    else
    {
      // skip thisI, no corresponding entry in src 
      i++;
    }
  }
  // if anything is left in src, add it to this
  for (; srcI<src.Size(); srcI++)
  {
    const BlendAnimInfo &srcInfo = src.Get(srcI);
    BlendAnimInfo &set = Set(Add());
    set.matrixIndex = srcInfo.matrixIndex;
    set.factor = srcInfo.factor*factor;
  }
}

BlendAnimType::BlendAnimType(const BlendAnimTypeName &name)
{
  _name = name;
  //ParamEntryVal par = name.motion->GetEntry()>>name.type;
  Load(name.motion->GetSkeleton(),*name.cfg);
}

const static MotionEdge NoEdge;

// Move info

DEFINE_FAST_ALLOCATOR(MoveInfoBase)

class CompareAnimHooks
{
public:
  int operator () (const AnimHook *a, const AnimHook *b) const
  {
    if(a->_time < b->_time) return -1;
    if(a->_time > b->_time) return 1;
    return 0;
  }
};

MoveInfoBase::MoveInfoBase( MotionType *motion, ParamEntryPar entry )
{
  ParamEntryVal actionMaps = motion->GetEntry()>>"Actions";

  motion->InitNoActions(actionMaps>>"NoActions");

  RStringB file = entry>>"file";
  if( file.GetLength()>0 )
  {
    AnimationRTName name;
    name.name = GetAnimationName(file);
    name.skeleton = motion->GetSkeleton();
    name.reversed = true;
    ConstParamEntryPtr ptr = entry.FindEntry("reversed");
    if (ptr) name.reversed = *ptr;
    name.streamingEnabled = true;
    _move = AnimationRTBank.New(name);

    bool preload = entry>>"preload";
    if (preload) _move->AddPreloadCount();
  }

  RStringB actionsName = entry>>"Actions";
  _actions = motion->NewActionMap(actionMaps>>actionsName);

  _speed = entry>>"speed";
  if (_speed<0) _speed=-1/_speed;
	
	ParamEntryPtr fpsParam = entry.FindEntry("fps");
	if(fpsParam.IsNull())
		_fps = 0;
	else
		_fps = entry>>"fps";
	ParamEntryPtr speedMultParam = entry.FindEntry("speedMult");
	_speedMult = speedMultParam.IsNull() ? 1.0f : entry>>"speedMult";

  ParamEntryPtr coef =  entry.FindEntry("skillSpeedCoef");
  if (coef.NotNull())
    _skillSpeedCoef = entry>>"skillSpeedCoef";
  else
    _skillSpeedCoef = 1;

  _relSpeedMin = entry>>"relSpeedMin";
  _relSpeedMax = entry>>"relSpeedMax";

  _soundEnabled = entry>>"soundEnabled";
  _soundOverride = entry>>"soundOverride"; // default - no override
  //_soundEdge1 = entry>>"soundEdge1"; // default - no override
  //_soundEdge2 = entry>>"soundEdge2"; // default - no override

  ParamEntryVal soundEdgesPar = entry >> "soundEdge";

  if (soundEdgesPar.IsArray())
  {
    int n = soundEdgesPar.GetSize();
    _hooks.Realloc(n);
    _hooks.Resize(n);

    for (int i = 0; i < n; i++)
    {
      _hooks[i]._time = soundEdgesPar[i];
      _hooks[i]._type = AH_SOUNDEDGE;
    }
  }
  ParamEntryVal animHooksPar = entry >> "animHooks";
  if(animHooksPar.IsClass())
  {
    for (ParamClass::Iterator<> i(animHooksPar.GetClassInterface()); i; ++i)
    {
      if((*i).IsClass())
      {
        float time = (*i) >> "time";
        int userType = (*i) >> "type";
        RStringB param = (*i) >> "param";
        saturate(time, 0.0f, 1.0f);
        _hooks.Add(AnimHook(AH_EVENT, time, userType, param));
      }
    }
    _hooks.Compact();
  }

  QSort(_hooks, CompareAnimHooks());

  _equivalentTo = motion->GetMoveId(entry>>"equivalentTo");

  _interpolSpeed = entry>>"interpolationSpeed";
  _interpolRestart = entry>>"interpolationRestart";
  _walkcycles= entry>>"walkcycles";

  _terminal = entry>>"terminal"; // all movement stops here

  ParamEntryPtr const minPlayTimeE = entry.FindEntry("minPlayTime");
  _minPlayTime = minPlayTimeE ? (*minPlayTimeE) : 0.f;
  saturate(_minPlayTime,0.f,1.f);
}

void MoveInfoBase::LoadAsSubclass(MotionType *motion, ParamEntryPar entry, ParamEntryPar actionMaps)
{
  // animation subclass can redefine selected memebers
  // redefine: move
  ConstParamEntryPtr ptr = entry.FindEntry("file");
  if (ptr)
  {
    RStringB file = ptr->GetValue();
    if( file.GetLength()>0 )
    {
      AnimationRTName name;
      name.name = GetAnimationName(file);
      name.skeleton = motion->GetSkeleton();
      name.reversed = true;
      ptr = entry.FindEntry("reversed");
      if (ptr) name.reversed = *ptr;
      name.streamingEnabled = true;

      bool looped = false;
      ptr = entry.FindEntry("looped");
      if (ptr) looped = *ptr;
      else if (entry.FindParent())
      {
        ptr = entry.FindParent()->FindEntry("looped");
        if (ptr) 
          looped = *ptr;
      }

      _move = AnimationRTBank.New(name);
//      if (_move->GetLooped()!=looped)
//        RptF("### looped FIXED to %d for %s", looped, cc_cast(name.name));
      _move->SetLooped(looped); // looped from new entry or from original move preserved
  
      ptr = entry.FindEntry("preload");
      if (ptr)
      {
        bool preload = *ptr;
        if (preload) _move->AddPreloadCount();
      }
    }
  }

  // redefine: speed
  ptr = entry.FindEntry("speed");
  if (ptr)
  {
    _speed = float(*ptr);
    if (_speed<0) _speed=-1/_speed;
  }

  // redefine: actions
  ptr = entry.FindEntry("Actions");
  if (ptr)
  {
    RStringB actionsName = ptr->GetValue();
    _actions = motion->NewActionMap(actionMaps>>actionsName);
  }

  // redefine: soundEdge
  ptr = entry.FindEntry("soundEdge");
  if (ptr)
  {
    if (ptr->IsArray())
    {
      int n = ptr->GetSize();
      _hooks.Realloc(n);
      _hooks.Resize(n);
    
      for (int i = 0; i < n; i++)
      {
        _hooks[i]._time = (*ptr)[i];
        _hooks[i]._type = AH_SOUNDEDGE;
      }
    }
  }

  // copy constructed _subAnim are of no use, remove it
  _subAnim.Clear();
}

float MoveInfoBase::GetSpeed() const
{
  float configCoef = Glob.config.GetAnimSpeedCoef();
  return GetOriginalSpeed() * (((_skillSpeedCoef-1)*configCoef)+1);
}

bool MoveInfoBase::Preload(ParamEntryPar entry)
{
  bool done = true;
  RStringB file = entry >> "file";
  if (file.GetLength() > 0)
  {
    bool preload = entry >> "preload";
    RString name = GetAnimationName(file);
    if (!AnimationRT::Preload(FileRequestPriority(1000),name,preload)) done = false;
  }
  return done;
}

// Motion type

MotionType::MotionType()
{
  _entry = NULL;
}

MotionType::~MotionType()
{
}

RStringB MotionType::GetMoveName( MoveId id ) const
{
  if (id == MoveIdNone) return RStringB("<none>");
  return _moveIds.GetName(id);
}

RStringB MotionType::GetMoveNameEx( MoveId id ) const
{
  if (id == MoveIdNone) return RStringBEmpty;
  return _moveIds.GetName(id);
}

MoveId MotionType::GetMoveId( RStringB name ) const
{
  if (name.IsEmpty()) return MoveIdNone;
  int value = _moveIds.GetValue(name);
  if (value<0 && name.GetLength()>0)
  {
    LogF("Warning: unknown value '%s'",(const char *)name);
  }
  return MoveId(value);
}

const MotionEdge &MotionType::Edge( MoveId a, MoveId b ) const
{
  // find edge in edge list
  Assert( a>=0 ); 
  Assert( b>=0 ); 
  const MotionEdges &edges=_vertex[a];
  for( int i=0; i<edges.Size(); i++ ) if( edges[i].target==b ) return edges[i];
  return NoEdge;
}

MoveId MotionType::FindConnectedSource(MoveId targetMove) const
{
  for (int i=0; i<_vertex.Size(); ++i)
  {
    const MotionEdges &edges=_vertex[i];
    for( int j=0; j<edges.Size(); ++j ) 
    {
      if( edges[j].target==targetMove && edges[j].type == MEdgeSimple) 
        return MoveId(i);
    }
  }
  return MoveIdNone ;
}

int MotionType::EdgeCost( MoveId a, MoveId b ) const
{
  const MotionEdge &edge=Edge(a,b);
  if( edge.cost<0 ) return INT_MAX;
  return edge.cost;
}

void MotionType::AddEdge(MoveId a, MoveId b, MotionEdgeType type, float cost, bool immediate)
{
  int iCost = toInt(cost*MotionEdge::CostScale);
#if _ENABLE_REPORT
  if (iCost>32767)
  {
    RptF(
      "Cost for edge %s,%s is too high (%g>%g)",
      NAME_THIS(a),NAME_THIS(b),cost,32767.0f/MotionEdge::CostScale
      );
  }
#endif
  saturate(iCost,-32768,+32767);
  // resize matrix so that is contains both a and b
  // check if egde is already present
  MotionEdges &edges = _vertex[a];
  for( int i=0; i<edges.Size(); i++ )
  {
    MotionEdge &edge = edges[i];
    if( edge.target==b )
    {
      edge.type=type;
      edge.cost=iCost;
      edge._immediate = immediate;
      return;
    }
  }
  edges.Add(MotionEdge(b,iCost,type,immediate));
}

void MotionType::DeleteEdge( MoveId a, MoveId b )
{
  MotionEdges &edges = _vertex[a];
  for( int i=0; i<edges.Size(); i++ )
  {
    MotionEdge &edge = edges[i];
    if( edge.target==b )
    {
      edges.Delete(i);
      return;
    }
  }
}

/// functor for accessing cost table
struct HeapIntByCost
{
  const int *_cost;

  HeapIntByCost(const int *cost):_cost(cost){}
  bool IsLess(int a, int b) {return _cost[a]<_cost[b];}
};

bool MotionType::FindPath( MotionPath &path, MoveId from, MotionPathItem to) const
{
  PROFILE_SCOPE(mtFPa);

  path.Resize(0);
  if (from == to.id)
  {
    // searching path to itself
    return true;
  }
  DoAssert(to.id!=MoveIdNone && from!=MoveIdNone);
  if (to.id==MoveIdNone || from==MoveIdNone)
  {
    // searching path to no node - probably an error
    // handle it, function could crash
    return false;
  }

  //LogF("State search from %s to %s",NAME(from),NAME(to.id));
  // check direct connection
  const MotionEdge &edge = Edge(from,to.id);
  if (edge.type!=MEdgeNone)
  {
    // if direct connection is found, use it and do not search for shortest path

    // finished: check if we have some path found
    path.Add(to);
    if (CHECK_DIAG(DELogAnimPaths))
    {
      LogF("ANIM- Direct path from %s to %s",NAME_THIS(from),NAME_THIS(to.id));
      LogF("ANIM-   insert %s, cost %d",NAME_THIS(to.id),edge.cost);
    }
    return true;
  }

  // perform Dijkstra's search
  //LogF("State search from %s to %s - Dijkstra",NAME(from),NAME(to.id));

  // see very nice description at: http://ciips.ee.uwa.edu.au/~morris/Year2/PLDS210/dijkstra.html

  int nVerts=_vertex.Size();
  AUTO_STATIC_ARRAY(int,d,1024);
  AUTO_STATIC_ARRAY(int,p,1024);
  d.Resize(nVerts);
  p.Resize(nVerts);
  for( int i=0; i<nVerts; i++ ) d[i]=INT_MAX,p[i]=-1; // no paths yet
  // source has trivial solution
  d[from]=0;

  // use heap array for vs (need fast extract min)

  HeapIntByCost cost(d.Data());
  HeapArray<int,MemAllocDataStack<int,1024>,HeapIntByCost> vs(cost);

  // add all vertices except source
  //for( int i=0; i<nVerts; i++ ) if( i!=fromI ) vs.Add(i);
  for( int i=0; i<nVerts; i++ )
  {
    // only add vertices that have some edges connected to them
    if (_vertex[i].Size()<=0)
    {
      continue;
    }
    vs.HeapInsert(i);
  }
  const int unaccessible=INT_MAX/16;

#if _DEBUG
  int iter = 0;
#endif
  while (d[to.id] == INT_MAX)
  {
#if _DEBUG
    iter++;
#endif
    int v;
    if (!vs.HeapRemoveFirst(v)) break;
    int vCost=d[v];
    if (vCost>=unaccessible) break;
    // relax all adjacent vertices
    const MotionEdges &e=_vertex[v];
    for( int i=0; i<e.Size(); i++ )
    {
      const MotionEdge &ei=e[i];
      int target=ei.target;
      if( target<0 ) continue;
      int costToTarget=vCost+ei.cost;
      if( costToTarget<d[target] )
      {
        d[target]=costToTarget,p[target]=v;
        vs.HeapUpdateUp(target);
      }
    }
  }

  if( p[to.id]<0 )
  {
    // LogF("no path from %s to %s",NAME_THIS(from),NAME_THIS(to.id));
    return false;
  }

  // finished: check if we have some path found
  path.Add(to);
  if (CHECK_DIAG(DELogAnimPaths))
  {
    LogF("ANIM- State search from %s to %s",NAME_THIS(from),NAME_THIS(to.id));
    LogF("ANIM-   insert %s, cost %d",NAME_THIS(to.id),d[to.id]);
  }
  MoveId curr = to.id;
  for(;;)
  {
    Assert( p[curr]>=0 && p[curr]<_moveIds.FirstInvalidValue() );
    curr = (MoveId)p[curr];
    if( curr==from )
    {
      if (CHECK_DIAG(DELogAnimPaths))
      {
        LogF("ANIM-   found %s, cost %d",NAME_THIS(from),d[from]);
      }
      // LogF("Path from %s to %s - length %d, iters %d",NAME_THIS(from),NAME_THIS(end), path.Size(), iter);
      return true;
    }
    path.Insert(0, MotionPathItem(curr));
    if (CHECK_DIAG(DELogAnimPaths))
    {
      LogF("ANIM-   insert %s, cost %d",NAME_THIS(curr),d[curr]);
    }
  }
  /*NOTREACHED*/
}

ActionMap *MotionType::NewActionMap( ParamEntryPar cfg )
{
  ActionMapName name;
  name.motion = this;
  name.entry = cfg.GetClassInterface();
  Assert(name.entry);
  return _actionMaps.New(name);
}


BlendAnimType *MotionType::NewBlendAnimType(ParamEntryVal cfg)
{
  BlendAnimTypeName name;
  name.motion = this;
  name.cfg = &cfg;
  return _blendAnimTypes.New(name);
}


void MotionType::InitNoActions(ParamEntryPar cfg)
{
  _noActions = NewActionMap(cfg);
}

MoveIdExt MoveIdExt::none=MoveIdExt(MoveIdNone,MovePrimary);

const MoveIdExt &MotionType::GetMove(int upDegree, ActionMap::Index index) const
{
  if (index == ActionMap::IndexNone) return MoveIdExt::none;

  // search for any ActionMap that corresponds to upDegree
  for (int i=0; i<_actionMaps.Size(); i++)
  {
    const ActionMap *map = _actionMaps[i];
    if (map->GetUpDegree() == upDegree)
    {
      const MoveIdExt &move = map->GetAction(index);
      if (move.id!=MoveIdNone)
      {
        return move;
      }
    }
  }
  return MoveIdExt::none;
}

const MoveIdExt &MotionType::GetMove(const ActionMap *map, RString action) const
{
  int index = _actions.GetValue(action);
  if (index < 0)
  {
    if (action.GetLength()>0)
    {
      RptF("Bad action name %s - not found in %s",cc_cast(action),cc_cast(map->GetName().entry->GetName()));
    }
    return MoveIdExt::none;
  }

  return map->GetAction((ActionMap::Index)index);
}

static void CheckArrayMod(
  ParamEntryPar entry, int mod, ConstParamEntryPtr context=ConstParamEntryPtr()
)
{
  if (entry.GetSize()%mod!=0)
  {
    RptF
    (
      "%s: item count not multiple of %d (is %d)",
      context ? (const char *)context->GetContext(entry.GetName()) : (const char *)entry.GetName(),
      mod, entry.GetSize()
    );
  }
}

static void BadMove
(
  ParamEntryPar entry, const char *name, ConstParamEntryPtr context=ConstParamEntryPtr()
)
{
  RptF
  (
    "  %s: Bad move %s",
    context ? (const char *)context->GetContext(entry.GetName()) : (const char *)entry.GetName(),
    (const char *)name
  );
}

//! For loading ignoreMinPlayTime parameter of animation state
class IgnoreMinPlayTimeParser
{
public:
  IgnoreMinPlayTimeParser(ParamEntryVal const &states, MotionType const *type);

  bool operator ()(MoveId from, MoveId to) const;
private:

  AutoArray< FindArrayKey<MoveId> > _ignore;
};

//--------------------------------

IgnoreMinPlayTimeParser::IgnoreMinPlayTimeParser( ParamEntryVal const &states, MotionType const *type )
{
  AutoArray<RString> immediate;

  int count = states.GetEntryCount();
  _ignore.Resize(count);

  for (int i=0; i<count; ++i )
  {
    ParamEntryVal sub = states.GetEntry(i);

    if (sub.GetPointer())
    {
      sub.GetPointer()->ReadArray(immediate,"ignoreMinPlayTime");
      immediate.ForEachF([i,this,type](RString const &s) -> bool { _ignore[i].Add(type->GetMoveId(s)); return false; });
    }
  }
}

//--------------------------------

bool IgnoreMinPlayTimeParser::operator()( MoveId from, MoveId to ) const
{
  Assert(from >= 0 && from < _ignore.Size());
  return _ignore[from].FindKey(to) != -1;
}

//--------------------------------

static const EnumName AnimSubclassNames[]=
{ // NOTE: USE LOWERCASE ONLY
  EnumName(MotionType::ASOriginal,"*none*"), //cannot match to any class
  EnumName(MotionType::ASInjuredLight,"injuredlight"),
  EnumName(MotionType::ASInjuredHeavy,"injuredheavy"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(MotionType::EAnimSubclass dummy)
{
  return AnimSubclassNames;
}

void MotionType::Load( ParamEntryVal entry )
{
  /*
  LogF
  (
  "Load motion type %s",
  (const char *)entry.GetName()
  );
  */
  // assign skeleton
  RStringB skeletonName = entry.GetName();
  ConstParamEntryPtr explicitName = entry.FindEntry("skeletonName");
  if (explicitName)
  {
    skeletonName = *explicitName;
  }
  ProgressRefresh();
  _skeleton = Skeletons.New(skeletonName);
  _skeleton->LoadCollPatterns(entry);

  _entry = &entry;

  ParamEntryVal states = entry>>"States";

  // add all entries of class entry
  _moveIds.Clear();
  for (int i=0; i<states.GetEntryCount(); i++ )
  {
    ParamEntryVal sub = states.GetEntry(i);
    _moveIds.AddValue(sub.GetName());
  }
  _moveIds.Close();

  // animation subclasses - only subclasses registered in animationSubclasses[] array are allowed
  ConstParamEntryPtr animSubCls = entry.FindEntry("animationSubclasses");
  if (animSubCls && animSubCls->IsArray())
  {
		int n = animSubCls->GetSize();
		for (int i=0; i<n; i++) 
		{
			if ((*animSubCls)[i].IsTextValue())
			{
        _animSubclasses.AddValue((*animSubCls)[i].GetValue());
			};
		};
    _animSubclasses.Close();
  }
  _animEnumToSubIx.Realloc(MotionType::ASN);
  _animEnumToSubIx.Resize(MotionType::ASN);
  const EnumName *names = AnimSubclassNames;
  for (int i=0; i<_animEnumToSubIx.Size(); i++)
  {
    _animEnumToSubIx[i]=MotionType::NoSubclassIx; 
    for (int j=0; j<_animSubclasses.FirstInvalidValue(); j++)
    {
      if ( !stricmp(names[i].name, _animSubclasses.GetName(j)) ) // note: use stricmp instead of operator==, as programmer can do mistake and use some uppercase inside AnimSubclassNames
      {
        _animEnumToSubIx[i]=(MotionType::EAnimSubclass)j;
        break;
      }
    }
  }

  // the absence of InjuredHeavy should make the system fall back to InjuredLight, and only if that's missing, to the default state
  if (_animEnumToSubIx[MotionType::ASInjuredLight]!=MotionType::NoSubclassIx && _animEnumToSubIx[MotionType::ASInjuredHeavy]==MotionType::NoSubclassIx)
  { 
    _animEnumToSubIx[MotionType::ASInjuredHeavy] = _animEnumToSubIx[MotionType::ASInjuredLight];
  }

  // resize MotionType to contain all moves
  int moveIdN = MoveIdN();
  _vertex.Realloc(moveIdN);
  _vertex.Resize(moveIdN);

  IgnoreMinPlayTimeParser ignore(states,this);

  //LogF("Load motion matrix");

  // load interpolations information
  ParamEntryVal inter = entry>>"Interpolations";
  for( int i=0; i<inter.GetEntryCount(); i++ )
  {
    ParamEntryVal group = inter.GetEntry(i);
    float fCost = group[0];
    // insert all possible pairs of states
    for( int s=1; s<group.GetSize(); s++ )
    {
      RStringB sName = group[s];
      MoveId sMove = GetMoveId(sName);
      if( sMove==MoveIdNone ) continue;

      for( int r=1; r<group.GetSize(); r++ ) if( r!=s )
      {
        RStringB rName = group[r];
        MoveId rMove = GetMoveId(rName);
        if( rMove==MoveIdNone ) continue;

        AddEdge( rMove,sMove, MEdgeInterpol, fCost, ignore(rMove,sMove) );
      }
    }   
  }

  { // interpolated transitions
    ParamEntryVal trans = entry>>"transitionsInterpolated";
    CheckArrayMod(trans,3,&entry);
    for( int i=0; i<trans.GetSize(); i+=3 )
    {
      RStringB a = trans[i];
      RStringB b = trans[i+1];
      float fCost = trans[i+2];
      MoveId aId = GetMoveId(a);
      MoveId bId = GetMoveId(b);
      if( aId==MoveIdNone || bId==MoveIdNone )
      {
        RptF("Bad ipol transition from %s to %s",(const char *)a,(const char *)b);
        continue;
      }
      // get move id
      AddEdge( aId,bId,MEdgeInterpol,fCost,ignore(aId,bId) );
    }
  }
  { // simple transitions
    ParamEntryVal trans = entry>>"transitionsSimple";

    CheckArrayMod(trans,3,&entry);
    for( int i=0; i<trans.GetSize(); i+=3 )
    {
      RStringB a = trans[i];
      RStringB b = trans[i+1];
      float fCost = trans[i+2];
      MoveId aId = GetMoveId(a);
      MoveId bId = GetMoveId(b);
      if( aId==MoveIdNone || bId==MoveIdNone )
      {
        RptF("Bad simple transition from %s to %s",(const char *)a,(const char *)b);
        continue;
      }
      // get move id
      AddEdge( aId,bId,MEdgeSimple,fCost );
    }
  }

  // scan all states for inline connection information
  for (int i=0; i<moveIdN; i++)
  {
    // get state entry
    ParamEntryVal state = states.GetEntry(i);
    // get connectFrom
    ParamEntryVal conFrom = state>>"connectFrom";
    CheckArrayMod(conFrom,2,&state);
    for (int s=0; s<conFrom.GetSize()-1; s+=2)
    {
      RStringB name = conFrom[s];
      float cost = conFrom[s+1];
      MoveId id = GetMoveId(name);
      if (id==MoveIdNone)
      {
        BadMove(conFrom,name,&state);
      }
      else
      {
        AddEdge( id, (MoveId)i, MEdgeSimple, cost );
      }
    }
    ParamEntryVal conTo = state>>"connectTo";
    CheckArrayMod(conTo,2,&state);
    for (int s=0; s<conTo.GetSize()-1; s+=2)
    {
      RStringB name = conTo[s];
      float cost = conTo[s+1];
      MoveId id = GetMoveId(name);
      if (id==MoveIdNone)
      {
        BadMove(conTo,name,&state);
      }
      else
      {
        AddEdge( (MoveId)i, id, MEdgeSimple, cost );
      }
    }

    ParamEntryVal ipolW = state>>"interpolateWith";
    CheckArrayMod(ipolW,2,&state);
    for (int s=0; s<ipolW.GetSize()-1; s+=2)
    {
      RStringB name = ipolW[s];
      float cost = ipolW[s+1];
      MoveId id = GetMoveId(name);
      if (id==MoveIdNone)
      {
        BadMove(ipolW,name,&state);
      }
      else
      {
        AddEdge( (MoveId)i, id, MEdgeInterpol, cost, ignore((MoveId)i,id));
        AddEdge( id, (MoveId)i, MEdgeInterpol, cost, ignore(id,(MoveId)i));
      }
    }

    ParamEntryVal ipolT = state>>"interpolateTo";
    CheckArrayMod(ipolT,2,&state);
    for (int s=0; s<ipolT.GetSize()-1; s+=2)
    {
      RStringB name = ipolT[s];
      float cost = ipolT[s+1];
      MoveId id = GetMoveId(name);
      if (id==MoveIdNone)
      {
        BadMove(ipolT,name,&state);
      }
      else
      {
        AddEdge( (MoveId)i, id, MEdgeInterpol, cost, ignore((MoveId)i,id) );
      }
    }

    ParamEntryVal ipolF = state>>"interpolateFrom";
    CheckArrayMod(ipolF,2,&state);
    for (int s=0; s<ipolF.GetSize()-1; s+=2)
    {
      RStringB name = ipolF[s];
      float cost = ipolF[s+1];
      MoveId id = GetMoveId(name);
      if (id==MoveIdNone)
      {
        BadMove(ipolF,name,&state);
      }
      else
      {
        AddEdge( id, (MoveId)i, MEdgeInterpol, cost, ignore(id,(MoveId)i) );
      }
    }

  }

  // scan all states for equivalency information
  for (int i=0; i<moveIdN; i++)
  {
    ParamEntryVal state = states.GetEntry(i);
    const RStringB &asState = state>>"connectAs";
    if (asState.GetLength()<=0) continue;
    // use all connection information from as
    MoveId as = GetMoveId(asState);
    if (as==MoveIdNone) continue;
    // scan line
    for (int j=0; j<moveIdN; j++)
    {
      const MotionEdge &sEdge = Edge(as,(MoveId)j);
      if (sEdge.type==MEdgeNone) continue;
      // check if this edge already exists
      const MotionEdge &tEdge = Edge((MoveId)i,(MoveId)j);
      if (tEdge.type!=MEdgeNone) continue;
      // take edge from as state
      AddEdge(MoveId(i),MoveId(j),sEdge.type.GetEnumValue(),sEdge.GetCost());
    }
    // scan column
    for (int j=0; j<moveIdN; j++)
    {
      const MotionEdge &sEdge = Edge((MoveId)j,as);
      if (sEdge.type==MEdgeNone) continue;
      // check if this edge already exists
      const MotionEdge &tEdge = Edge((MoveId)j,(MoveId)i);
      if (tEdge.type!=MEdgeNone) continue;
      // take edge from as state
      AddEdge(MoveId(j),MoveId(i),sEdge.type.GetEnumValue(),sEdge.GetCost());
    }
  }

  { // disabled transitions
    ParamEntryVal trans = entry>>"transitionsDisabled";
    CheckArrayMod(trans,2,&entry);

    for( int i=0; i<trans.GetSize(); i+=2 )
    {
      RStringB a = trans[i];
      RStringB b = trans[i+1];
      MoveId aId = GetMoveId(a);
      MoveId bId = GetMoveId(b);
      if( aId==MoveIdNone || bId==MoveIdNone )
      {
        RptF("Bad disabled transition from %s to %s",(const char *)a,(const char *)b);
        continue;
      }
      // get move id
      DeleteEdge( aId,bId );
    }
  }

  // compact memory
  _vertex.Compact();
  for (int i=0; i<_vertex.Size(); i++)
  {
    _vertex[i].Compact();
  }

#if 0
  FILE *matrix = fopen("matrix.txt","w");
  if (matrix)
  {
    for (int i=0; i<_vertex.Size(); i++)
    {
      const MotionEdges &edges = _vertex[i];
      for (int j=0; j<edges.Size(); j++)
      {
        const MotionEdge &edge = edges[j];
        fprintf
          (
          matrix,"%-20s to %-20s: %d, %.3f\n",
          NAME(MoveId(i)),NAME(MoveId(edge.target)),
          edge.type,edge.GetCost()
          );
      }
    }
    fclose(matrix);
  }
#endif
}

void MotionType::Unload()
{
  _actions.Clear();
  _moveIds.Clear();
  _vertex.Clear();
  _entry = NULL;
}

// Moves type

ActionMap *MovesTypeBase::GetActionMap(MoveId move, int subIx) const
{
  const MoveInfoBase *anim = GetMoveInfoBase(move, subIx);
  if (!anim) return NULL; // no animation
  return anim->GetActionMap(); //can be called, as subAnim resolved
}

MovesTypeBase::MovesTypeBase()
{
}

MovesTypeBase::~MovesTypeBase()
{
}

/*!
\patch 5155 Date 5/9/2007 by Jirka
- Fixed: Movement config - entry primaryActionMaps telling what action maps will be loaded first (and so used for initialization of movement)
*/

void MovesTypeBase::Load(const MovesTypeName &name)
{
  _name = name;

  ParamEntryVal moves = Pars>>_name.skeletonName;

  // create list of actions
  ParamEntryVal actions = moves >> "ManActions";
  _actions.Clear();
  for (int i=0; i<actions.GetEntryCount(); i++)
  {
    ParamEntryVal entry = actions.GetEntry(i);
    _actions.AddValue(entry.GetName());
  }

  MotionType::Load(moves);

  // preload primary action maps (to be sure they are in _actionsMap prior other actions)
  ConstParamEntryPtr primaryActionMaps = moves.FindEntry("primaryActionMaps");
  if (primaryActionMaps)
  {
    for (int i=0; i<primaryActionMaps->GetSize(); i++)
    {
      RString mapName = (*primaryActionMaps)[i];
      NewActionMap(moves >> "Actions" >> mapName);
    }
  }

  // load RT animations
  _moves.Realloc(MoveIdN());
  _moves.Resize(MoveIdN());

  //const ParamEntry &moves=GetEntry();
  ParamEntryVal states=moves>>"States";

  ConstParamEntryPtr statesExt = moves.FindEntry("StatesExt");

  // preload all animations
  //LogF("* Anim preload start");
  MoveInfoPreloadFunc func = GetPreloadFunc();
  // real animation loading
  for( int i=0; i<_moves.Size(); i++ )
  {
    MoveId id = MoveId(i);
    RStringB moveName = GetMoveName(id);
    if (!states.FindEntry(moveName) ) continue;
    ConstParamEntryPtr entry = states.FindEntry(moveName);
    if (statesExt)
    {
      ConstParamEntryPtr entryExt = statesExt->FindEntry(moveName);
      if (entryExt) entry = entryExt;
    }
    ProgressRefresh();
    (*func)(*entry);
    ProgressRefresh();
  }
  //LogF("* Anim submit requests");
  GFileServerSubmitRequestsMinimizeSeek();

#if _ENABLE_REPORT
  DWORD start = GlobalTickCount();
#endif

  //LogF("* Anim load all");
  // real animation loading

  // we might want to load the entries in the same order as file requests were processed
  // otherwise one out of order entry could cause CPU wait for very long

  // this means: if an entry is not satisfied, we prefer not waiting for it
  // we rather proceed to the next entry
  // 

  // to avoid traversing the whole array again and again we rather keep a list of unprocessed items
  // this is somewhat less efficient for the whole array, however it improves reaction time for the last few entries
  FindArrayKey<int> notProcessed;
  notProcessed.Realloc(_moves.Size());
  notProcessed.Resize(_moves.Size());
  for( int i=0; i<_moves.Size(); i++ ) notProcessed[i] = i;
  
  #if 0 // _ENABLE_REPORT
    int reported = notProcessed.Size();
  #endif

#if _VBS3
    GDebugger.PauseCheckingAlive();
#endif

  while(notProcessed.Size()>0)
  {
    ProgressRefresh();
    GFileServerUpdate();
    GTimeManager.Frame(0.1);
    
    bool someProcessed = false;
    for( int ii=0; ii<notProcessed.Size(); ii++ )
    {
      int i = notProcessed[ii];
      // we can fast-skip entries which were already loaded
      Assert (_moves[i].IsNull());
      
      MoveId id = MoveId(i);
      RStringB moveName = GetMoveName(id);
      if (!states.FindEntry(moveName) ) continue;
      ConstParamEntryPtr entry = states.FindEntry(moveName);
      if (statesExt)
      {
        ConstParamEntryPtr entryExt = statesExt->FindEntry(moveName);
        if (entryExt) entry = entryExt;
      }

      if (!(*func)(*entry))
      {
        // we cannot process it until data are ready
        // keep pumping file handling + progress
        ProgressRefresh();
        GFileServerUpdate();
        GTimeManager.Frame(0.1);
        continue;
      }
      
      MoveInfoBase *move = NewMoveInfo(*entry);
      _moves[i] = move;

      AnimationRT *anim = *move;
      if( anim )
      {
        bool looped = (*entry)>>"looped";
        if (!anim->LoopNotInitialized() && looped!=anim->GetLooped())
        {
          if (!anim->CheckStatic()) // for static animation we do not care if they are looped or not
          {
            RptF("Warning: looped for animation: %s differs (looped now %d)! MoveName: %s", cc_cast(anim->GetName().name), looped, cc_cast(moveName));
          }
          // check if the animation is in fact a static one
        }
        anim->SetLooped(looped);
      }
      else
      {
        LogF("Warning: Animation %s not loaded", cc_cast(moveName));
      }

      // make sure progress bar is updated and request continue to be processed      
      ProgressRefresh();
      GFileServerUpdate();
      GTimeManager.Frame(0.1);
      // Fix: this line was causing a long startup time
      // FreeOnDemandFrame();
      
      someProcessed = true;
      
      // item was processed, remove it from the waiting list
      notProcessed.DeleteAt(ii);
      ii--;
      
    }
    
    // there is no need to waste CPU cycles while waiting for I/O
    // unless some request is processed, there is nothing to process
    GFileServerWaitForOneRequestDone(100);
    FreeOnDemandFrame();
#if 0 // _ENABLE_REPORT
    if (notProcessed.Size()<reported-50)
    {
      LogF("%d: Left %d",GlobalTickCount()-start,notProcessed.Size());
      reported = notProcessed.Size();
    }
#endif
    
  }

#if _VBS3
  GDebugger.ResumeCheckingAlive();
#endif


#if _ENABLE_REPORT
  DWORD duration = GlobalTickCount()-start;
  if (duration>10)
  {
    LogF("MoveInfo %s load time %d ms",cc_cast(name.skeletonName),duration);
  }
#endif
}

MoveId MovesTypeBase::GetEquivalent( MoveId move ) const
{
  const MoveInfoBase *info = GetMoveInfoBase(move, NoSubclassIx);
  if (!info) return MoveIdNone;
  MoveId id = info->GetEquivalentTo();
  if (id == MoveIdNone) return move;
  return id;
}

MovesTypeBaseBank MovesTypesBase;

// Moves

MovesVisualState::MovesVisualState()
:_primaryFactor(1),_finished(false),_paused(false),_blendTime(1)
{

}

inline float InterpolationFunctions::LerpWrap01(float a, float b, float r)
{
  // interpolate through the shortest way
  if (fabs(a-b)<=0.5f)
  {
    float ret = a * (1.0f-r) + b * r;
    Assert(ret>=0 && ret<=1);
    return ret;
  }
  else if (a-b>=0.5f)
  { // e.g. a=0.8, b=0.1
    float ret = Lerp(a,b+1,r);
    if (ret>=1) ret -= 1;
    Assert(ret>=0 && ret<=1);
    return ret;
  }
  else // if (b-a>0.5)
  { // e.g. a=0.1, b=0.8
    Assert(b-a>=0.5f)
    float ret = Lerp(a+1,b,r);
    if (ret>=1) ret -= 1;
    Assert(ret>=0 && ret<=1);
    return ret;
  }
}

RString MovesVisualState::DiagText() const
{
  return Format(
    "P %d:%.3f(+%.3f) x %.2f, S %d:%.3f(+%.3f)",
    ENUM_TO_INT(_primary.GetMove().id),_primary.GetTime(),_primary.GetTimeDelta(),
    _primaryFactor,ENUM_TO_INT(_secondary.GetMove().id),_secondary.GetTime(),_secondary.GetTimeDelta()
  );
}

void MovesVisualState::Interpolate(const MovesVisualState & t1state, float t, MovesVisualState& interpolatedResult) const
{
  if (t1state._primary.GetMove().id==_primary.GetMove().id && t1state._secondary.GetMove().id==_secondary.GetMove().id)
  {
    // interpolate when interpolation is easy: primary and secondary the same, interpolate time only
    interpolatedResult._primary = _primary;
    interpolatedResult._secondary = _secondary;
    interpolatedResult._primaryFactor = Lerp(_primaryFactor,t1state._primaryFactor,t);
    interpolatedResult._primary.SetTime(LerpWrapHelper::LerpWrap01(_primary.GetTime(),t1state._primary.GetTime(),t));
    interpolatedResult._secondary.SetTime(LerpWrapHelper::LerpWrap01(_secondary.GetTime(),t1state._secondary.GetTime(),t));
  }
  else if (t1state._primary.GetMove().id==_secondary.GetMove().id && t1state._secondary.GetMove().id==_primary.GetMove().id)
  { // reversed primary / secondary
    interpolatedResult._primary = _primary;
    interpolatedResult._secondary = _secondary;
    interpolatedResult._primaryFactor = Lerp(_primaryFactor,1-t1state._primaryFactor,t);
    interpolatedResult._primary.SetTime(LerpWrapHelper::LerpWrap01(_primary.GetTime(),t1state._secondary.GetTime(),t));
    interpolatedResult._secondary.SetTime(LerpWrapHelper::LerpWrap01(_secondary.GetTime(),t1state._primary.GetTime(),t));
  }
  else
  {
    const MotionInfo *primary0 = &_primary;
    const MotionInfo *secondary0 = &_secondary;
    float primary0Factor = _primaryFactor;

    const MotionInfo *primary1 = &t1state._primary;
    const MotionInfo *secondary1 = &t1state._secondary;
    float primary1Factor = t1state._primaryFactor;

    float t0 = t;

    // we want to fit to a configuration: primary0Factor=1, primary0->id == primary1->id
    // swap so that the less used is the secondary
    if (primary0Factor<Moves::_primaryFactor1)
    {
      Swap(primary0,secondary0);
      primary0Factor = 1-primary0Factor;
    }

    if (primary0Factor>=Moves::_primaryFactor1 && secondary1->GetMove().id==primary0->GetMove().id)
    {
      Swap(primary1,secondary1);
      primary1Factor = 1-primary1Factor;
    }
    else
    {
      // now try the same, but swapping 0 and 1 states
      Swap(primary0,primary1);
      Swap(secondary0,secondary1);
      Swap(primary0Factor,primary1Factor);
      t0 = 1-t0;

      if (primary0Factor<Moves::_primaryFactor1)
      {
        Swap(primary0,secondary0);
        primary0Factor = 1-primary0Factor;
      }

      if (primary0Factor>=Moves::_primaryFactor1 && secondary1->GetMove().id==primary0->GetMove().id)
      {
        Swap(primary1,secondary1);
        primary1Factor = 1-primary1Factor;
      }

    }

    if (primary0Factor>=Moves::_primaryFactor1 && primary0->GetMove().id==primary1->GetMove().id)
    { // secondary0 is unused
      interpolatedResult._primary = *primary0;
      interpolatedResult._primaryFactor = t0*primary1Factor+(1-t0) /* primary0Factor==1 */;
      interpolatedResult._primary.SetTime(LerpWrapHelper::LerpWrap01(primary0->GetTime(),primary1->GetTime(),t0));
      interpolatedResult._secondary = *secondary1;
      return;
    }

    // convert both into a canonical form: primary always stronger than secondary
    MovesVisualState t0c = *this;
    MovesVisualState t1c = t1state;
    if (t0c._primaryFactor<0.5f)
    {
      Swap(t0c._primary,t0c._secondary);
      t0c._primaryFactor = 1-t0c._primaryFactor;
    }
    if (t1c._primaryFactor<0.5f)
    {
      Swap(t1c._primary,t1c._secondary);
      t1c._primaryFactor = 1-t1c._primaryFactor;
    }
  
    if (t0c._primaryFactor>=Moves::_primaryFactor1 && t1c._primaryFactor>=Moves::_primaryFactor1)
    { // another typical case - switch between two primaries
      // TODO: each animation may use a different time
      float time = t1c._primary.GetTime()-t1c._primary.GetTimeDelta()*(1-t);
      // if smaller than 0, but only a very little bit, it is very likely 0 was intended: and if not, no harm is done by assuming it
      if (time<=-0.001f)
      {
        time -= toIntFloor(time);
      }
      Assert(time>=-0.001f && time<=1);
      interpolatedResult._primary.VisualCopy(t1c._primary);
      interpolatedResult._secondary.VisualCopy(t1c._secondary);
      interpolatedResult._primaryFactor = t1c._primaryFactor;
      interpolatedResult._primary.SetTime(time);
    }
    else 
    {
      if (t>0.5f)
        interpolatedResult = t1state;
      else
        interpolatedResult = *this;
      Assert(interpolatedResult._primary.GetTime()>=0 && interpolatedResult._primary.GetTime()<=1);
    }
  }
}
#define SMOOTHFACTOR(bTime) InterpolationFunctions::SmoothSimple3Parts(bTime)
void MovesVisualState::ReverseBlending()
{
  _blendTime = 1-_blendTime;
  _primaryFactor = SMOOTHFACTOR(_blendTime);
}

void MovesVisualState::Complete()
{
  _primaryFactor = _blendTime = 1;
}

Moves::Moves(int defaultUpDegree)
: _defaultUpDegree(defaultUpDegree),
_upDegreeChangeTime(Glob.time - 5),
_forceMove(ENUM_CAST(MoveId, MoveIdNone)),
_animSubIx(-1)
{}

MoveId Moves::GetDesiredMove() const
{
  int n = _queueMove.Size();
  if (n<=0) return MoveIdNone;
  return _queueMove[n - 1].id;
}

RString Moves::GetCurrentMove(const MovesVisualState &vs, const MovesTypeBase *type) const
{
  int n = _queueMove.Size();
  MoveId id = (n == 0 ? vs._primary.GetMove().id : _queueMove[n - 1].id);
  return id < 0 ? RString() : type->GetMoveName(id);
}

RString Moves::GetCurrentMoveEq(const MovesVisualState &vs, const MovesTypeBase *type) const
{
  int n = _queueMove.Size();
  MoveId id = (n == 0 ? vs._primary.GetMove().id : _queueMove[n - 1].id);
  const MoveInfoBase *move = type->GetMoveInfoBase(id, AnimSubIx());
  if (!move) return RString();
  MoveId eqId = move->GetEquivalentTo();
  if (eqId<0) eqId = id;
  return eqId < 0 ? RString() : type->GetMoveName(eqId);
}

float Moves::GetCurrentTime(const MovesVisualState &vs) const
{
  if (_queueMove.Size() > 0) return 0;
  return vs._primary.GetTime();
}

int Moves::GetActUpDegree(const MovesVisualState &vs, const MovesTypeBase *type) const
{
  // helper function
  ActionMap *map = type->GetActionMap(vs._primary.GetMove().id, AnimSubIx());
  return map ? map->GetUpDegree() : _defaultUpDegree;
}

void Moves::UpDegreeChanged()
{
  _upDegreeChangeTime = Glob.time;
}

void Moves::SetPrimaryMove(MovesVisualState &vs, const MovesTypeBase *type, MotionPathItem item)
{
  int oldUD = GetActUpDegree(vs,type);
  // DoAssert(CheckValidState(type));
  vs._primary.SetMove(item, true);
  // DoAssert(CheckValidState(type));

  int newUD = GetActUpDegree(vs,type);
  if (oldUD != newUD) UpDegreeChanged();
}

float Moves::GetPrimaryTimeLeft(const MovesVisualState &vs, const MovesTypeBase *type) const
{
  const MoveInfoBase *prim = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
  if (!prim) return 0;
  float animSpeed = prim->GetSpeed();
  if (animSpeed<1e-6) return 0;
  return (1-vs._primary.GetTime())/animSpeed;
}

void Moves::SetMoveQueueContext(ActionContextBase *context)
{
  int n = _queueMove.Size();
  Assert(n > 0);
  MotionPathItem &lastItem = _queueMove[n - 1];
  lastItem.context = context;
}

bool Moves::IsActionInProgress(const MovesVisualState &vs, MoveFinishF action) const
{
  ActionContextBase *context = GetPrimaryMove(vs).context;
  if (context && context->function == action) return true;
  if (_forceMove.context && _forceMove.context->function==action) return true;
  // check moves waiting in queues
  for (int i=0; i<_forcedQueue.Size(); i++)
  {
    if (_forcedQueue[i].context && _forcedQueue[i].context->function==action) return true;
  }
  return false;
}

const ActionContextBase *Moves::GetActionInProgress(const MovesVisualState &vs) const
{
  ActionContextBase *context = GetPrimaryMove(vs).context;
  if (context) 
    return context;
  if (_forceMove.context) 
    return _forceMove.context;
  // check moves waiting in queues
  for (int i=0; i<_forcedQueue.Size(); i++)
  {
    if (_forcedQueue[i].context) return _forcedQueue[i].context;
  }
  return nullptr;
}

void Moves::AddForceMove(MotionPathItem item, MovesVisualState& vs)
{
  if (_forceMove.id==MoveIdNone)
  {
    _forceMove = item;
    return;
  }
  Continue(vs);
  _forcedQueue.Add(item);
}

void Moves::ResetForcedQueue()
{
  _forceMove = MotionPathItem(MoveIdNone);
  _forcedQueue.Clear();
}

float Moves::GetMajorAnimationTime(const MovesVisualState &vs, const MovesTypeBase *type) const
{
  // find which animation is the most important one
  const MotionInfo &major = vs._primaryFactor>0.5f ? vs._primary : vs._secondary;
  const MoveInfoBase *moveInfo = type->GetMoveInfoBase(major.GetMove().id, AnimSubIx());
  if (!moveInfo) return 0;
  const AnimationRT *anim = *moveInfo;
  if (!anim) return 0;
  float speed = moveInfo->GetSpeed();
  if (speed<=0) return 0;
  return major.GetTime()/speed;
  
}

void Moves::CancelPrimaryAction(MovesVisualState &vs)
{
  if (vs._secondary.GetMove().context == vs._primary.GetMove().context) vs._secondary.ClearContext(false);
  vs._primary.ClearContext(true);
}

void Moves::CancelForcedActions()
{
  if (_forceMove.context)
  {
    _forceMove.context->Cancel();
    _forceMove = MotionPathItem(MoveIdNone);
    return;
  }
  for (int i=0; i<_forcedQueue.Size(); i++)
  {
    if (_forcedQueue[i].context)
    {
      _forcedQueue[i].context->Cancel();
      _forcedQueue.Delete(i);
      return;
    }
  }

}

void Moves::PatchNoMove(MovesVisualState &vs, MotionPathItem item)
{
  if (vs._primary.GetMove().id == MoveIdNone)
  {
    vs._primary.SetMove(item, true);
  }
}

void Moves::ResetMovement(MovesVisualState &vs, const MovesTypeBase *type, MotionPathItem item)
{
  ResetMoveQueue();
  SetPrimaryMove(vs, type, item);
  vs._primary.SetTime(0);
  // DoAssert(CheckValidState(type));
  vs._secondary.SetMove(vs._primary.GetMove(), false);
  vs._secondary.SetTime(0);
  // DoAssert(CheckValidState(type));
  vs.Complete();
  vs._finished = false;
  _forceMove = MotionPathItem(ENUM_CAST(MoveId, MoveIdNone));
  _forcedQueue.Clear();
}

void Moves::SwitchMove(MovesVisualState &vs, MoveId id, ActionContextBase *context)
{
  vs._primary.SetMove(MotionPathItem(id, context), true);
  vs._primary.SetTime(0);
  vs._secondary.SetMove(MotionPathItem(id, context), false);
  vs._secondary.SetTime(0);
  QueueMoveClear();
  _queueMove.Clear();
  vs.Complete();
  vs._finished = false;
}

//! To prevent laggy changes of direction
Vector3 const GStepAcceleration(20,20,20);
float const GStepThreshold = 1e-5;
float const GDeinterpolMultiplier = 3;
Vector3 AnimStepsBlender::UpdateAndGetStep( float deltaT, float adjustSpeed, MovesTypeBase const *type, MoveId primary, bool connected, MoveId queueEnd, int subIx )
{
  Vector3 step(0,0,0);

  // Add primary and queue end if they are not added already
  bool primFound = false, qEndFound = false;
  for (int i = 0; i < _steps.Size(); ++i)
  {
    primFound |= _steps[i]._move == primary;
    qEndFound |= _steps[i]._move == queueEnd;
  }

  if (!primFound) AddMove(primary,type,subIx,connected);
  if (!qEndFound) AddMove(queueEnd,type,subIx);

  float totalFactor=0;
  for (int i = 0; i < _steps.Size(); ++i)
  {
    BlendedStep &bs = _steps[i];
    MoveInfoBase const *move = type->GetMoveInfoBase(bs._move, subIx);

    if (move)
    {
      // Increase factor of primary or queueEnd
      if (bs._move == primary || bs._move == queueEnd)
      {
        bs._factor += deltaT*move->GetInterpolSpeed();
      }
      else
      {
        bs._factor -= GDeinterpolMultiplier*deltaT*move->GetInterpolSpeed();
      }

      saturateMin(bs._factor,1);
    }

    // Remove unused steps
    if (bs._factor <= 0)
    {
      _steps.Delete(i,1);
    }
    else if (move && (*move))
    {
      // Moves with very tiny step are not blended. However, their factor is still taken
      // into account for smooth blending.
      if (!bs._static)
      {
        step += adjustSpeed*bs._factor*move->GetSpeed()*(*move)->GetStep();        
      }
      totalFactor += bs._factor;
    }
  }

  // Normalize
  Vector3 stepDiff = (step / (totalFactor >= 1e-5 ? totalFactor : 1)) - _lastStep;
  SaturateMin(stepDiff,GStepAcceleration*deltaT);
  SaturateMax(stepDiff,-GStepAcceleration*deltaT);
  return (_lastStep = _lastStep + stepDiff)*deltaT;
}

void AnimStepsBlender::AddMove( MoveId id, MovesTypeBase const *type, int subIx, bool connected )
{
  MoveInfoBase const *move = type->GetMoveInfoBase(id, subIx);
  // Don't add empty move
  if (!move) return;
  if (move->GetSpeed()>1e5) return; // do not add static animation
  // Anim should be set for each move
  _steps.Add(BlendedStep(id,(fabs((*move)->GetStep().SquareSize()) <= GStepThreshold*GStepThreshold),connected));
}

void Moves::AdvanceForcedQueue()
{
  MoveId &playMove = _playMove._playing;
  if (playMove!=MoveIdNone && _forceMove.id==MoveIdNone)
  {
    _forceMove = playMove; // MotionPathItem(playMove);
    playMove = MoveIdNone;
  }
  if (_forceMove.id==MoveIdNone && _forcedQueue.Size()>0)
  {
    _forceMove = _forcedQueue[0];
    _forcedQueue.Delete(0);
  }
}

void Moves::ChangePlayMove( MoveId moveM )
{
  if (moveM!=_playMove._wanted)
  {
    //LogF("%s: receive _movesPlayMove %s",cc_cast(GetDebugName()),cc_cast(playM));
    _playMove._wanted = moveM; // wanted will stay until changed by a client, _playing will be reset when the animation is started
    if (moveM!=MoveIdNone) _playMove._playing = moveM;
  }
}

MoveId Moves::GetCurrentPlayMove(const MovesVisualState &vs) const
{
  auto contextPlayMove = [&vs](const MotionPathItem &item) -> MoveId
  {
    ActionContextBase *ctx = item.context;
    if (!ctx) return MoveIdNone;
    return ctx->GetPlayMove();
  };
  if (vs._primaryFactor>_primaryFactor0)
  {
    MoveId id=contextPlayMove(vs._primary.GetMove());
    if (id!=MoveIdNone) return id;
  }
  if (vs._primaryFactor<_primaryFactor1)
  {
    MoveId id=contextPlayMove(vs._secondary.GetMove());
    if (id!=MoveIdNone) return id;
  }

  // check queue instead
  for (int i=0; i<_queueMove.Size(); i++)
  {
    MoveId id=contextPlayMove(_queueMove[i]);
    if (id!=MoveIdNone) return id;
  }
  return MoveIdNone;
}


/*!
\patch_interal 1.90 Date 11/4/2002 by Ondra
- Fixed: Footstep marks reintroduced (Missing since April 2000)
*/

bool Moves::AdvanceMoveQueue(MovesVisualState &vs, const MovesTypeBase *type, float deltaT, float adjustSpeed, AdvanceQueueInfo &info, bool &done, bool blendception, Entity *man)
{
  // return true if position of any bone has changed
  bool change = false;
  //vs._finished = false;

  // Needed when multiple steps can are allowed to blend
  bool primWasConnected(false);

  // If next move in queue is the current secondary move, don't wait until blending is finished,
  // initiate reverse blending instead
  bool const nextInQueueIsSecondary = _queueMove.Size() > 0 && _queueMove[0].id == vs._secondary.GetMove().id;

  if (nextInQueueIsSecondary || (vs._primaryFactor>=_primaryFactor1 && _queueMove.Size()>0))
  {
    const MoveInfoBase *prim = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
    if (prim->GetTerminal())
    {
      goto ContinueAnim; // terminal state - no more advance possible
    }

    MotionPathItem nextMove = _queueMove[0];


    #if _DEBUG
      RString primName = NAME_T(type,vs._primary.GetMove().id);
      RString nextName = NAME_T(type,nextMove.id);
    #endif

    bool removeCurrent=true;

    if (vs._primary.GetMove().id == nextMove.id)
    {
      // DoAssert(CheckValidState(type));
      // identical move in queue may be safely ignored
      // we need to transfer context
      vs._primary.SetMove(nextMove, true);
      // DoAssert(CheckValidState(type));
    }
    else
    {
      const MotionEdge &edge = type->Edge(vs._primary.GetMove().id, nextMove.id);
      switch( edge.type.GetEnumValue() )
      {
      case MEdgeInterpol:
        {
          MoveInfoBase const *pri = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
          if (!(*pri)->GetLooped() 
            && !edge._immediate 
            && vs._primary.GetTime() < pri->GetMinPlayTime()
          )
          {
            removeCurrent=false;
            break;
          }
          // if transition to this move may be interpolated, interpolate
          // DoAssert(CheckValidState(type));
          vs._secondary.SetMove(vs._primary.GetMove(), false); // from
          // DoAssert(CheckValidState(type));
          vs._secondary.SetTime(vs._primary.GetTime());
          SetPrimaryMove(vs,type, nextMove); // to
          // vs._primary time should be played from the beginning
          vs._finished = false;

          pri = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
          const MoveInfoBase *sec = type->GetMoveInfoBase(vs._secondary.GetMove().id, AnimSubIx());

          switch (pri->GetInterpolRestart())
          {
          case 1:
            vs._primary.SetTime(0);
            break;
          case 2:
            vs._primary.SetTime(1 - vs._primary.GetTime());
            break;
          case 0:
            vs._primary.SetTime(fmod(vs._secondary.GetTime()*sec->GetWalkcycles(),1.0f)/pri->GetWalkcycles());
            break;
          }

          if (nextInQueueIsSecondary)
          {
            vs.ReverseBlending();
          }
          else
          {
            vs._primaryFactor = vs._blendTime = 0;
          }
        }
        break;
      case MEdgeNone:
        LogF
        (
          "No edge from %s to %s",
          NAME_T(type,vs._primary.GetMove().id), NAME_T(type,nextMove.id)
        );
        SetPrimaryMove(vs,type, nextMove);
        vs._secondary.SetMove(MotionPathItem(MoveIdNone),false);
        vs._finished = false;
        // vs._primary time should be played from the beginning
        vs._primary.SetTime(0);

        if (nextInQueueIsSecondary)
        {
          vs.ReverseBlending();
        }
        else
        {
          vs.Complete();
        }
        change = true;
        break;
      case MEdgeSimple:
        // wait until primary move is finished
        if( !vs._finished )
        {
          //LogF("Advance: waiting for finish of %s",NAME_T(type,vs._primaryMove.id));
          goto ContinueAnim;
        }
        // otherwise we have to perform move as it is
        SetPrimaryMove(vs, type, nextMove);
        // remove the secondary animation, so that interpolation is not tempted to use it
        vs._secondary.SetMove(MotionPathItem(MoveIdNone),false);
        vs._primary.SetTime(0);

        if (nextInQueueIsSecondary)
        {
          vs.ReverseBlending();
        }
        else
        {
          vs.Complete();
          primWasConnected = true;
        }

        vs._finished = false;
        // transition should be smooth - no change here
        break;
      }
    }
    // remove from queue
    if (removeCurrent) _queueMove.Delete(0);
    
    // we may continue advancing the animation right away
  }
  ContinueAnim:

  vs._finished = false;

  bool isBlending = false;

  // get speed from primary and secondary (interpolation)
  const MoveInfoBase *prim = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
#if 0
  LogF("%s AdvanceMoveQueue", cc_cast(type->GetMoveName(vs._primary.GetMove().id)));
#endif
  if (prim)
  {
    const AnimationRT *primA = *prim;
    float priSpeed = prim->GetSpeed();
    if (priSpeed>1e5)
    {
      // static looped animation
      // no sound
      // no movement
      vs._primary.SetTime(0,0);
      vs._finished = true;
    }
    else
    {
      change = true;

      float priPhaseChng = deltaT*priSpeed*adjustSpeed;
      if(vs._paused)
        priPhaseChng = 0.f;
      
      float oldPrimTime = vs._primary.GetTime();
      float newPrimTime = oldPrimTime + priPhaseChng;
      if (newPrimTime >= 1)
      {
        // check if move is looped
        vs._finished = true;
        done = true;
        if (primA && primA->GetLooped())
        {
          newPrimTime -= 1;
          // _moveTime should be < 1, if not it's moving too fast
          if (newPrimTime >= 1) newPrimTime = 0.5f;
        }
        else
        {
          // non-looped - stay at the end of the animation
          priPhaseChng -= newPrimTime - 1;
          newPrimTime = 1;
          Assert(fabs(newPrimTime-priPhaseChng-oldPrimTime)<1e-6);
          //Log("%s: %.4f+%.4f=%.4f",NAME_T(type, vs._primary.GetMove().id),oldPrimTime,priPhaseChng,newPrimTime);
        }
      }
      vs._primary.SetTime(newPrimTime,priPhaseChng);
      
      #if 0 // _ENABLE_CHEATS
        if (oldPrimTime<=0 && newPrimTime>0) 
        {
          Log("%s: %.4f+%.4f=%.4f",NAME_T(type, vs._primary.GetMove().id),oldPrimTime,priPhaseChng,newPrimTime);
        }
      #endif

      if (!blendception)
      {
        float moveFactor = priPhaseChng * vs._primaryFactor;

        if (primA)
        {
          info.moveZ -= moveFactor * primA->GetStepLength();
          info.moveX -= moveFactor * primA->GetStepLengthX();
        }
      }
#if 0
        LogF
        (
        "  pri %20s: deltaT %.3f, priSpeed %.3f, priPhaseChng %.3f",
        NAME_T(type, vs._primaryMove),
        deltaT,priSpeed,priPhaseChng
        );
#endif

      const AutoArray<AnimHook>& animHooks = prim->GetAnimHooks();
      int hooksCount = animHooks.Size();
      if(hooksCount > 0 && oldPrimTime <= animHooks[hooksCount - 1]._time)
      {
        float newPrimTime = oldPrimTime + priPhaseChng;
        int footstep = 0;
        //if current hook time is higher than newPrimtime, it's clear we will not catch anything
        for (int i = 0; i < hooksCount && newPrimTime >= animHooks[i]._time; i++) 
        {
          if (oldPrimTime < animHooks[i]._time && newPrimTime >= animHooks[i]._time)
          {
            if(animHooks[i]._type == AH_SOUNDEDGE)
            {
              if(prim->GetSoundEnabled())
              {
                info.step = true;
                info.soundOverride = prim->GetSoundOverride();
                info.stepLeft = (footstep % 2) == 0;
              }
            }
            else if(animHooks[i]._type == AH_EVENT)
            {
              if(vs._primary.GetMove().context)
              {
                vs._primary.GetMove().context->Event(animHooks[i]._userType, animHooks[i]._param);
              }
            }
          }
          if(animHooks[i]._type == AH_SOUNDEDGE)
            footstep++;
        }
      }
    }
  }
  else
  {
    // hotfix: no move, but some context is waiting to execute
    if (vs._primary.GetMove().context)
      vs._finished = true;
  }

  if (vs._primaryFactor < _primaryFactor1)
  {
    const MoveInfoBase *sec = type->GetMoveInfoBase(vs._secondary.GetMove().id, AnimSubIx());
    if (sec)
    {
      const AnimationRT *secA = *sec;
      float secSpeed = sec->GetSpeed();

      if (secSpeed>1e5)
      {
        // simple case: static animation
        vs._secondary.SetTime(0);
      }
      else
      {
        change = true;

        float secPhaseChng = deltaT*secSpeed*adjustSpeed;
        float newSecTime = vs._secondary.GetTime() + secPhaseChng;
        if (newSecTime >= 1)
        {
          // check if move is looped
          if (secA && secA->GetLooped())
          {
            newSecTime -= 1;
            // _moveTime should be < 1, if not it's moving too fast
            if (newSecTime >= 1) newSecTime = 0.5f;
          }
          else
          {
            // non-looped - stay at the end of the animation
            secPhaseChng -= newSecTime - 1;
            newSecTime = 1;
          }
        }
        vs._secondary.SetTime(newSecTime,secPhaseChng);

        if (!blendception)
        {
          float moveFactor = secPhaseChng*(1-vs._primaryFactor);
          if (secA)
          {
            info.moveZ -= moveFactor*secA->GetStepLength();
            info.moveX -= moveFactor*secA->GetStepLengthX();
          }
        }

        float moveFactor = secPhaseChng*(1-vs._primaryFactor);
        if (secA)
        {
          info.moveZ -= moveFactor*secA->GetStepLength();
          info.moveX -= moveFactor*secA->GetStepLengthX();
        }

#if 0
        LogF
          (
          "  sec %20s: deltaT %.3f, secSpeed %.3f, secPhaseChng %.3f",
          NAME_T(type, _secondaryMove),
          deltaT,secSpeed,secPhaseChng
          );
#endif
      }
    }
    
    // first finish transition to primary (should be quick)
    vs._blendTime += deltaT * GetInterpolSpeed(vs,type);

    bool finished = false;
    if (vs._blendTime > 1)
    {
      vs.Complete();
      finished = true;
    }

    vs._primaryFactor = SMOOTHFACTOR(vs._blendTime);
    if (finished) vs.Complete();

    //LogF("Advance: waiting for primary %s",NAME_T(type,vs._primaryMove.id));

    isBlending = true;
  }

  // Retrieve step with queue end included
  if (blendception)
  {
    MoveInfoBase const *primMove = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
    bool canBlendStep = primMove && primMove->CanBlendStep(); 
    Vector3 step = -_blendedSteps.UpdateAndGetStep(deltaT,adjustSpeed,type,vs._primary.GetMove().id
      , primWasConnected
      , _queueMove.Size() > 0 && canBlendStep ? _queueMove[_queueMove.Size()-1].id : MoveIdNone,
      AnimSubIx());
    info.moveX = step.X();
    info.moveZ = step.Z();
  }

  if (isBlending) return true;

  vs.Complete();

  if (type->GetEquivalent(vs._primary.GetMove().id) == type->GetEquivalent(_forceMove.id))
  {
    _forceMove.Done();
    _forceMove = MotionPathItem(ENUM_CAST(MoveId, MoveIdNone)); // forced move running as primary (no secondary)
  }

  if (vs._finished)
  {
    if (!vs._primary.GetMove().context && man)
    { // get special context (used when forceUnarmed=true)
      MoveInfoBase const *primMove = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
      vs._primary.GetMove().context = primMove->GetAutoContext();
    }
    if (vs._primary.GetMove().context)
    {
      info.context = vs._primary.GetMove().context;
      vs._primary.ClearContext(false);
    }
    //vs._finished = false;
  }

  return change;
}

float InterpolationFunctions::SmoothSimple3Parts(float currentTime,float totalTime)
{
  float halftime=totalTime/2;
  float thirdtime=totalTime/3;
  if (currentTime<thirdtime)
  {
    return currentTime*currentTime/(halftime*halftime)/2.0f;
  }
  else if (currentTime>2*thirdtime)
  {
    currentTime=totalTime-currentTime;
    return 1.0f-(currentTime*currentTime/(halftime*halftime)/2.0f);
  }
  else
  {
    float val1=thirdtime*thirdtime/(halftime*halftime)/2.0f;
    float val2=1.0f-(thirdtime*thirdtime/(halftime*halftime)/2.0f);

    return val1+(val2-val1)*((currentTime-thirdtime)/(totalTime-2*thirdtime));
  }
}

float InterpolationFunctions::SmoothSimple2Parts(float elpasedTime, float totalTime /* = 1 */)
{
  float halftime=totalTime/2;
  if (elpasedTime<halftime)
  {
    return elpasedTime*elpasedTime/(halftime*halftime)/2.0f;
  }
  else
  {
    elpasedTime=totalTime-elpasedTime;
    return 1.0f-(elpasedTime*elpasedTime/(halftime*halftime)/2.0f);
  }
}

float InterpolationFunctions::SmoothPower3( float x )
{
  static const float mult1p58 = 1.58740105f;
  return (x < 0.5f) ? powf(mult1p58*x,3) : (1+powf((mult1p58*(x - 1)),3));
}

void InterpolationFunctions::AccelerateBrakeConstant( float &outValue, float &outSpeed, float const targetValue, float const acc, float const dec, float deltaT )
{
  float speed = fabsf(outSpeed);
  float remTarValue = fabsf(targetValue);
  float totalChange = 0;

  bool positiveChange = (targetValue > 0);
  bool positiveSpeed = (outSpeed > 0);

  if (remTarValue < 0.00001f)
  {
    remTarValue = 0;
    positiveChange = positiveSpeed;
  }
  if (speed < 0.00001f)
  {
    speed = 0;
    positiveSpeed = positiveChange;
  }

  // Moving in wrong direction, need to reverse
  if (positiveSpeed != positiveChange)
  {
    float const timeToBrake = floatMin(speed/dec,deltaT);
    float const deceleration = dec*timeToBrake;
    float const brakeDistLocal = timeToBrake*(speed - 0.5f*deceleration);
    speed -= deceleration;
    deltaT -= timeToBrake;
    remTarValue += brakeDistLocal;
    totalChange -= brakeDistLocal;

    if (speed < 0.00001f) positiveSpeed = positiveChange;
  }

  float const brakeRotation = 0.5f*speed*speed/dec;

  // There is still space to accelerate
  if (remTarValue > brakeRotation)
  {
    float timeToMaxSpd = deltaT;
    float acceleration = timeToMaxSpd * acc;
    float valChange = timeToMaxSpd * (speed + 0.5f*acceleration);
    float const brakeDistNew = 0.5f*powf(speed + acceleration,2)/dec;

    // Not enough distance left to decelerate
    if (remTarValue - valChange < brakeDistNew)
    {
      float const aa = acc + acc*acc/dec;
      float const mbb = -2 * speed * (1 + acc/dec);
      float const cc = speed*speed/dec - 2*remTarValue;

      float const q = sqrtf(mbb*mbb - 4*aa*cc);
      float const r = 2*aa;

      float remainingTime = (mbb + q ) / r;
      if (remainingTime < 0) remainingTime = (mbb - q) / r;

      timeToMaxSpd = remainingTime;
      acceleration = acc*timeToMaxSpd;
      valChange = timeToMaxSpd*(speed + 0.5f*acceleration);
    }

    speed += acceleration;
    deltaT -= timeToMaxSpd;
    remTarValue -= valChange;
    totalChange += valChange;
  }    

  // Deceleration
  if (remTarValue > 0 || speed > 0)
  {
    float const timeToBrake = floatMin(speed/dec,deltaT);
    float const deceleration = dec*timeToBrake;
    float const brakeRotLocal = timeToBrake * (speed - 0.5f*deceleration);

    speed -= deceleration;
    deltaT -= timeToBrake;
    remTarValue -= brakeRotLocal;
    totalChange += brakeRotLocal;
  }

  outSpeed = speed * (positiveSpeed ? 1.f : -1.f);
  outValue += totalChange * (positiveChange ? 1.f : -1.f);
}

#define DIAG_QUEUE 0

bool Moves::ChangeMoveQueue(MovesVisualState &vs, const MovesTypeBase *type, bool &animChanged, MotionPathItem item)
{
  animChanged = false;

  // check for trivial case - soldier already performs required move
  if (item.id == vs._primary.GetMove().id)
  {
#if DIAG_QUEUE
    LogF("- Reset queue %s", NAME_T(type, item.id));
#endif
    // we are in target state
    if (item.context)
    {
      // DoAssert(CheckValidState(type));
      vs._primary.SetMove(item, true);
      // DoAssert(CheckValidState(type));
    }

    if (_queueMove.Size() > 0)
    {
      QueueMoveClear();
      animChanged = true;
    }
    return true;
  }


  const MoveInfoBase *info = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
  if (info && info->GetTerminal())
  {
    // we are in terminal state - nowhere to change
    return false;
  }

  if (item.id == vs._secondary.GetMove().id && vs._primaryFactor<1 && !vs._primary.GetMove().context)
  {
    // revert primary / secondary
    // check if interpolation is possible
    const MotionEdge &edge = type->Edge(vs._secondary.GetMove().id, vs._primary.GetMove().id);
    if (edge.type == MEdgeInterpol)
    {
      Swap(vs._secondary, vs._primary);
      if (item.context) vs._primary.SetMove(item, true);

      vs._blendTime = 1-vs._blendTime;
      vs._primaryFactor = InterpolationFunctions::SmoothSimple3Parts(vs._blendTime,1);

      QueueMoveClear();
/*
      if (item.context)
      {
        vs._primaryMove.context = item.context;
        vs._primaryMove.ReportStarted();
      }
*/
      animChanged = true;
      return true;
    }
  }

  int qSize = _queueMove.Size();
  if( qSize>0 && _queueMove[qSize-1].id==item.id )
  {
#if DIAG_QUEUE
    LogF("- Queue already valid - %s", NAME_T(type,item.id));
#endif
    // current path is valid
    return true;
  }

#if DIAG_QUEUE
  LogF("- Set queue %s", NAME_T(type,item.id));
#endif

  // find shortest path from current primary move to target move
  if (vs._primary.GetMove().id==MoveIdNone) return false;
  bool ret = type->FindPath(_queueMove, vs._primary.GetMove().id, item);
  if (ret) animChanged = true;
  return ret;
}

void Moves::PrepareMatrices(
  const MovesTypeBase *type, Matrix4Array &matrices, LODShape *shape, int level, const BlendAnims &blend
)
{
  DoAssert(shape->IsLevelLocked(level));
  const Shape *shapeL = shape->GetLevelLocked(level);

  // perform the traversal which will do a blending for all matrices
  AnimationRT::PrepareMatrices(matrices,blend,shapeL,shape->GetSkeleton());

  // Number of prepared matrices must match number of bones in the skeleton
  int nBones = shapeL->GetSubSkeletonSize();
  int matricesSize = matrices.Size();
  if (matricesSize != nBones)
  {
    LogF("Error: %s:%s Number of prepared matrices doesn't match number of subskeleton bones", cc_cast(shape->GetName()), cc_cast(shape->LevelName(shapeL)));
    if (matricesSize < nBones)
    {
      matrices.Realloc(nBones);
      matrices.Resize(nBones);
      for (int i = matricesSize; i < nBones; i++)
      {
        matrices[i] = MIdentity;
      }
    }
  }
}

void Moves::LoadAnimationPhases(MovesVisualState &vs, const MovesTypeBase *type, float pastTimeNeeded)
{
  if (vs._primaryFactor > _primaryFactor0)
  {
    // primary animation streaming
    const MoveInfoBase *info = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
    if (info)
    {
      AnimationRT *anim = *info;
      if (anim) anim->LoadNeededPhases(vs._primary.GetBuffer(), vs._primary.GetTime(), vs._primary.GetTime()-pastTimeNeeded*info->GetSpeed(),type->GetSkeleton());
    }
  }
  if (vs._primaryFactor < _primaryFactor1)
  {
    // primary animation streaming
    const MoveInfoBase *info = type->GetMoveInfoBase(vs._secondary.GetMove().id, AnimSubIx());
    if (info)
    {
      AnimationRT *anim = *info;
      if (anim) anim->LoadNeededPhases(vs._secondary.GetBuffer(), vs._secondary.GetTime(), vs._secondary.GetTime()-pastTimeNeeded*info->GetSpeed(),type->GetSkeleton());
    }
  }
}

bool Moves::CheckValidState(const MovesVisualState &vs, const MovesTypeBase * type) const
{
  if (vs._primaryFactor > _primaryFactor0)
  {
    AnimationRT *anim=type->GetAnimation(vs._primary.GetMove().id, AnimSubIx());
    if (anim)
    {
      bool ok = anim->CheckValidState(vs._primary.GetBuffer());
      if (!ok) return false;
    }
  }
  if (vs._primaryFactor < _primaryFactor1)
  {
    // check for simple cases
    AnimationRT *anim=type->GetAnimation(vs._secondary.GetMove().id, AnimSubIx());
    if (anim)
    {
      bool ok = anim->CheckValidState(vs._secondary.GetBuffer());
      if (!ok) return false;
    }
  }
  return true;
}

void Moves::GetBlendAnims(const MovesVisualState &vs, BlendAnims &blend, const MovesTypeBase * type, float maskFactor, const BlendAnimSelections *mask) const
{
  if (vs._primaryFactor > _primaryFactor0)
  {
    const MoveInfoBase *info = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
    if (info && *info)
    {
      (*info)->GetBlend(blend, vs._primary.GetTime(), vs._primaryFactor, maskFactor, mask, &vs._primary.GetBuffer(), info);
    }
  }
  if (vs._primaryFactor < _primaryFactor1)
  {
    const MoveInfoBase *info = type->GetMoveInfoBase(vs._secondary.GetMove().id, AnimSubIx());
    if (info && *info)
    {
      (*info)->GetBlend(blend, vs._secondary.GetTime(), 1 - vs._primaryFactor, maskFactor, mask, &vs._secondary.GetBuffer(), info);
    }
  }
}


/**
Purpose of this function is to provide some basis where relative movement is needed.
Currently the calculations are done only for looped movements.
For non-looped it returns the same as GetBlendAnims
*/

void Moves::GetBlendAnimsNeutral(const MovesVisualState &vs, BlendAnims &blend, const MovesTypeBase * type) const
{
  if (vs._primaryFactor > _primaryFactor0)
  {
    AnimationRT *anim = type->GetAnimation(vs._primary.GetMove().id, AnimSubIx());
    if (anim)
    {
      float time = anim->GetLooped() ? 0 : vs._primary.GetTime();
      anim->GetBlend(blend, time, vs._primaryFactor);
    }
  }
  if (vs._primaryFactor < _primaryFactor1)
  {
    AnimationRT *anim = type->GetAnimation(vs._secondary.GetMove().id, AnimSubIx());
    if (anim)
    {
      float time = anim->GetLooped() ? 0 : vs._secondary.GetTime();
      anim->GetBlend(blend, time, 1 - vs._primaryFactor);
    }
  }
}
void Moves::AnimateBoneMatrix(const BlendAnims &blend, const MovesTypeBase *type, Matrix4 &mat, SkeletonIndex boneIndex) const
{
  if (SkeletonIndexValid(boneIndex))
  {
    // now perform the blending
    blend.DoBoneAnim(mat,type->GetSkeleton(),boneIndex);

    // this is the only place where we orthogonalize animation matrix
    // we do it because we want to make sure proxies (esp. the gun) live in orthonormal space
    mat.Orthogonalize();
  }
}

Vector3 Moves::AnimatePoint(const BlendAnims &blend, const MovesTypeBase *type, LODShape *lShape, int level, int index ) const
{
  if (index<0)
  {
    return VZero;
  }

  ShapeUsed shape=lShape->Level(level);

  Assert(shape->IsLoadLocked());

  const AnimationRTWeight &wg = shape->GetVertexBoneRef()[index];
  if( wg.Size()<=0 ) return shape->Pos(index); // this point is not animated
  if (blend._list.Size()<=0) return shape->Pos(index); // not animated

  Vector3 pos = shape->Pos(index);


  // now perform the blending
  blend.DoPointAnim(pos,type->GetSkeleton(),shape,wg);

  return pos;
}


float Moves::ValueTest(const MovesVisualState &vs, const MovesTypeBase *type, TestValueBase func, float defValue) const
{
  // For synchronization purposes, interpolation speed is now taken from primary move ONLY.
  // (formerly, it was blended between primary and secondary according to primaryFactor)
  const MoveInfoBase *info = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
  return info ? (info->*func)() : defValue;
}

void Moves::GetRelSpeedRange(const MovesVisualState &vs, const MovesTypeBase *type, float &speedX, float &speedZ, float &minSpd, float &maxSpd)
{
  speedZ = 0;
  speedX = 0;
  minSpd = 0, maxSpd = 1000;
  const MoveInfoBase *pri = type->GetMoveInfoBase(vs._primary.GetMove().id, AnimSubIx());
  if (pri && (AnimationRT *)(*pri) && pri->GetSpeed()<1e5)
  {
    speedZ = pri->GetSpeed() * -(*pri)->GetStepLength();
    speedX = pri->GetSpeed() * -(*pri)->GetStepLengthX();
    saturateMax(minSpd,pri->GetRelSpeedMin());
    saturateMin(maxSpd,pri->GetRelSpeedMax());
    if (vs._primaryFactor < _primaryFactor1)
    {
      const MoveInfoBase *sec = type->GetMoveInfoBase(vs._secondary.GetMove().id, AnimSubIx());
      if (sec && sec->GetSpeed()<1e5)
      {
        float secSpeedZ = sec->GetSpeed() * -(*sec)->GetStepLength();
        float secSpeedX = sec->GetSpeed() * -(*sec)->GetStepLengthX();
        speedZ = (1-vs._primaryFactor)*secSpeedZ + vs._primaryFactor*speedZ;
        speedX = (1-vs._primaryFactor)*secSpeedX + vs._primaryFactor*speedX;
        saturateMax(minSpd,sec->GetRelSpeedMin());
        saturateMin(maxSpd,sec->GetRelSpeedMax());
      }
    }
  }
}


RString Moves::GetDiagInfo(const MovesVisualState &vs, const MovesTypeBase *type) const
{
#if _ENABLE_CHEATS
  ActionMap *map = type->GetActionMap(vs._primary.GetMove().id, AnimSubIx());
  RString buf = Format(" deg %d", map ? map->GetUpDegree() : _defaultUpDegree);

  AnimationRT *pri = type->GetAnimation(vs._primary.GetMove().id, AnimSubIx());
  RString log = Format
  (
    " %s \n Prim %d-%s:%.3f(+%.3f) \n Sec %d-%s:%.3f(+%.3f) \n PCoef %.4f ",
    pri ? (const char *)pri->Name() : "NULL",
    vs._primary.GetMove().id, NAME_T(type, vs._primary.GetMove().id), vs._primary.GetTime(), vs._primary.GetTimeDelta(),
    vs._secondary.GetMove().id, NAME_T(type, vs._secondary.GetMove().id), vs._secondary.GetTime(), vs._secondary.GetTimeDelta(),
    vs._primaryFactor
  );
  buf = buf + log;
  LogF(log);
  
  if (_queueMove.Size() > 0)
  {
    MoveId nextMove = _queueMove[0].id;
    buf = buf + Format("Next %s ", NAME_T(type, nextMove));
  }
  return buf;
#else
  return RString();
#endif
}

LSError MotionInfo::Serialize(const char *prefix, const MovesTypeBase *type, ParamArchive &ar)
{
  CHECK(_move.Serialize(prefix, type, ar))
  CHECK(ar.Serialize(RString(prefix)+"Time", _time, 1, 0))
  CHECK(ar.Serialize(RString(prefix)+"DTime", _dTime, 1, 0))
  return LSOK;
}

LSError MotionPathItem::Serialize(const char *prefix, const MovesTypeBase *type, ParamArchive &ar)
{
  if (ar.IsSaving())
  {
    RString moveName = id < 0 ? RString() : type->GetMoveName(id);
    CHECK(ar.Serialize(RString(prefix)+"id", moveName, 1, ""))
  }
  else
  {
    RString moveName;
    CHECK(ar.Serialize(RString(prefix)+"id", moveName, 1, ""))
    if (type && !moveName.IsEmpty())
    {
      id = type->GetMoveId(moveName);
    }
    else
      id = MoveIdNone;
  }
  CHECK(ar.Serialize("context", context, 1))

  return LSOK;
}

LSError Moves::Serialize(MovesVisualState &vs, const char *prefix, const MovesTypeBase *type, ParamArchive &ar)
{
  CHECK(ar.Serialize(RString(prefix)+"DefaultUpDegree", _defaultUpDegree, 1, 0))
  CHECK(::Serialize(ar, RString(prefix)+"UpDegreeChangeTime",_upDegreeChangeTime,1,Time(0)))

  //primary move
  if (ar.IsSaving())
  {
    CHECK(vs._primary.Serialize(RString(prefix)+"Primary", type, ar))
  }
  else if (ar.GetPass()==ParamArchive::PassFirst)
  {
    MotionInfo move;
    CHECK(move.Serialize(RString(prefix)+"Primary", type, ar))
    SetPrimaryMove(vs, type, move.GetMove());
    vs._primary.SetTime(move.GetTime());
  }

  //secondary move
  CHECK(vs._secondary.Serialize(RString(prefix)+"Secondary", type, ar))
  CHECK(ar.Serialize(RString(prefix)+"PrimaryFactor", vs._primaryFactor, 1, 1))
  CHECK(ar.Serialize(RString(prefix)+"Finished", vs._finished, 1, false))
  //MotionPathItem _forceMove;
  //MotionPathItem _externalMove;

  return LSOK;
}

// road path finding

#include "wpch.hpp"
#include "landscape.hpp"
#include "roads.hpp"
#include "global.hpp"
#include "progress.hpp"
#include "serializeBinExt.hpp"

DEFINE_FAST_ALLOCATOR(RoadLink)

DEFINE_FAST_ALLOCATOR(RoadListFull)

typedef RoadNet::QuadTreeExRootRoadList::QuadTreeType  QuadTreeExRoadList;
// DEFINE_FAST_ALLOCATOR(QuadTreeExRoadList)

RoadLink::RoadLink()
:_locksSoldier(0), _locksVehicle(0), _locksSoldierWaiting(0), _locksVehicleWaiting(0), _nCon(0)
{
  for (int i=0; i<NCon; i++) _type[i] = CTNone;
}

RoadLink::RoadLink( Object *object, Vector3 *pos, ConnectionType *type, int c )
:_object(object), _locksSoldier(0), _locksVehicle(0), _locksSoldierWaiting(0), _locksVehicleWaiting(0), _nCon(c),_lshape(object->GetShape()),_toWorld(object->FutureVisualState().Transform())
{
  Assert( c<=NCon );
  for (int i=0; i<c; i++) _pos[i] = pos[i];
  for (int i=0; i<c; i++) _type[i] = type[i];
  for (int i=0; i<NCon; i++) _con[i] = NULL;
}

bool RoadLink::IsInsideReady() const
{
  // check if the link is set or can be recovered quickly
  // see also  IsOnRoadReady
  return true;
  //return _object.IsReady();
}

bool RoadLink::MayBeInside(Vector3Val pos, float size) const
{
  // for purpose of planning ignore components which are not connected
  if (_nCon==0) return false;
  Vector3 center = GetCenter();
  float dist2 = pos.Distance2(center);
  return dist2<=Square(50+size);
}

bool RoadLink::IsInside(Vector3Val pos, float sizeXZ, float sizeY) const
{
  // for purpose of planning ignore components which are not connected
  if (_nCon==0) return false;
  LODShape *lShape=_lshape;
  const Shape *shape=_lshape->RoadwayLevel();
  if( !shape )
  {
    RptF("No roadway in road %s",_lshape->Name());
    return false;
  }

  float centerDist2=(pos-_toWorld.Position()).SquareSizeXZ();
  if( centerDist2>Square(lShape->BoundingSphere()+sizeXZ) ) return false;

  ClipFlags clip = shape->GetAndHints();
  if (clip & ClipLandOn)
  {
    // clamp y to surface
    float y = pos[1] - GLandscape->SurfaceY(pos[0], pos[2]);
    if (y > sizeY) return false;
  }
  // TODO: is handling of ClipLandKeep needed?

  Matrix4 invTrans=_toWorld.InverseGeneral();
  Vector3 rPos=invTrans.FastTransform(pos);
  Offset f;
  int p;

  float minDist2 = FLT_MAX;
  //float minHeight = FLT_MAX;

  for( f=shape->BeginFaces(),p=0; f<shape->EndFaces(); shape->NextFace(f),p++ )
  {
    const Poly &face=shape->Face(f);
    float dist2=face.DistanceFromTop(*shape,rPos);
    if (dist2>Square(sizeXZ)) continue;

    if ((clip & ClipLandOn) == 0)
    {
      const Plane &plane = shape->GetPlane(p);
      // positive means below the plane
      float height = plane.Distance(rPos);
      // we are not interested in surfaces above us
      if (height>0.5) continue;
      // if the surface is way below us, we are not interested in it
      if (height<-sizeY) continue;
    }
    // TODO: we are interested with topmost surface underneath us
    saturateMin(minDist2,dist2);
  }
  return minDist2<Square(sizeXZ);
}

void RoadLink::SerializeBin(SerializeBinStream &f, Landscape *land)
{
  // only object and positions are saved
  // we might also save connections, but it would be necessary to implement saving Refs to netlinks to this
  // which would require two pass loading
  if (f.IsSaving())
  {
    f.SaveShort(_nCon);
    for (int i=0; i<_nCon; i++)
    {
      f.Transfer(_pos[i]);
    }
    // connection types
    for (int i=0; i<_nCon; i++)
    {
      f.Transfer((char &)_type[i]);
    }

#if _HELISIM_LARGE_OBJECTID
    int idi = _object.GetId().Encode32();
#else
    int idi = _object.GetId().Encode();
#endif
    Assert(idi!=0);
#if _HELISIM_LARGE_OBJECTID
    f.SaveInt(idi);
#else
    f.SaveInt(idi);
#endif

    RString name = "(shape name not known)";
    Matrix4 objPos = MIdentity;
    OLinkLPtr(Object) obj = _object.GetLock();
    if (!obj)
    {
      if (_lshape) name = _lshape->GetName();
      Vector3 pos(VZero);
      if (_nCon>0)
      {
        for (int ix=0; ix<_nCon; ix++) pos = pos + _pos[ix];
        pos = pos / _nCon;
      }
      RptF("%s position=(%.2f,%.2f)", cc_cast(name), pos.X(), pos.Z());
      objPos.SetPosition(pos);
    }
    else
    {
      name = obj->GetShape()->GetName();
      objPos = obj->FutureVisualState().Transform();
    }
    f << name;
    f << objPos;  }
  else
  {
    _nCon = f.LoadShort();
    for (int i=0; i<_nCon; i++)
    {
      f.Transfer(_pos[i]);
    }
    if (land->GetFileVersion() >= 24)
    {
      // connection types
      for (int i=0; i<_nCon; i++)
      {
        f.Transfer((char &)_type[i]);
      }
    }
    for (int i=0; i<NCon; i++) _con[i]=NULL;
    int idi = f.LoadInt();
    ObjectId id;
#if _HELISIM_LARGE_OBJECTID
    id.Decode32(idi);
#else
    id.Decode(idi);
#endif
    _object = land->GetObjectNoLock(id);
    if (_object.IsNull())
    {
#if _HELISIM_LARGE_OBJECTID
      RptF("Road object %I64x not found",idi);
#else
      RptF("Road object %x not found",idi);
#endif
    }
    if (land->GetFileVersion()>=16) //road name and matrix serialized
    {
      RString name;
      f << name;
      _lshape = Shapes.New(name,false,true);
      Assert(_lshape.NotNull());
      f << _toWorld;
    }
    else
    {
      OLinkLPtr(Object) obj = _object.GetLock();
      if (obj.NotNull())
      {
        _lshape = obj->GetShape();
        Assert(_lshape.NotNull());
        _toWorld = obj->FutureVisualState().Transform();
      }
      else
      {
        Fail("Road object NULL");
      }
    }
  }
}

/*
inline RoadList &RoadNet::SelectRoadList( float x, float z )
{
  int xx=toIntFloor(x*InvLandGrid);
  int zz=toIntFloor(z*InvLandGrid);
  if( !InRange(xx,zz) )
  {
    // find nearest in-range square and use it
    if( xx<0 ) xx=0;else if( xx>LandRangeMask ) xx=LandRangeMask;
    if( zz<0 ) zz=0;else if( zz>LandRangeMask ) zz=LandRangeMask;
    Assert( InRange(xx,zz) );
  }
  return _roads(xx,zz);
}
*/

inline void RoadNet::AddRoad(float x, float z, RoadLink *elem)
{
  int xx=toIntFloor(x*InvLandGrid);
  int zz=toIntFloor(z*InvLandGrid);
  if( !InRange(xx,zz) )
  {
    // find nearest in-range square and use it
    if( xx<0 ) xx=0;else if( xx>LandRangeMask ) xx=LandRangeMask;
    if( zz<0 ) zz=0;else if( zz>LandRangeMask ) zz=LandRangeMask;
    Assert( InRange(xx,zz) );
  }
  RoadListFull *list = _roads(xx,zz).GetList();
  if (!list)
  {
    list = new RoadListFull();
    _roads.Set(xx, zz, RoadList(list));
  }
  list->Add(elem);
}

// build road net information

RoadNet::RoadNet()
: _roads(256, 256, RoadList())
{
}
RoadNet::~RoadNet()
{
  Clear();
}

/**
Similar to Object::Animate, but ignores animation type and always does animation on CPU
@return world space result
*/
static Vector3 RoadPoint(Object *obj, const Shape *shape, int vertex)
{
  Matrix4Val toWorld = obj->FutureVisualState().Transform();
  LODShape *lShape = obj->GetShape();
  ClipFlags clip = shape->Clip(vertex);
  Vector3Val pos = shape->Pos(vertex);
  Vector3 tPos(VFastTransform,toWorld,pos);
  if( clip&ClipLandKeep )
  {
    // shape y is relative to surface
    // calculate world coordinates
    float yPos=pos[1]+lShape->BoundingCenter().Y();
    tPos[1]=GLOB_LAND->SurfaceY(tPos[0],tPos[2])+yPos;
  }
  else if( clip&ClipLandOn )
  {
    // clamp y to surface
    tPos[1]=GLOB_LAND->SurfaceY(tPos[0],tPos[2]);
  }
  return tPos;
}



void RoadNet::Scan( Landscape *land )
{
  _roads.Dim(land->GetLandRange(),land->GetLandRange());
  // insert all road objects into the road list
  for( int x=0; x<LandRange; x++ ) for( int z=0; z<LandRange; z++ )
  {
    const ObjectList &list=land->GetObjects(z,x);
    for( int o=0; o<list.Size(); o++ )
    {
      Object *obj=list[o];
      if( obj->GetType()!=Network ) continue;
      if (obj->GetShape()==NULL) continue;
      // road with no roadway is not usable, avoid connecting it
      int d = 0;
      const int MaxPos=4;
      Vector3 pos[MaxPos];
      RoadLink::ConnectionType type[MaxPos];
      Vector3 avg=obj->FutureVisualState().Position();
      if (obj->GetShape()->RoadwayLevel()!=NULL)
      {
        // consider only networks
        // scan cross point name pairs
        static const char *pairs[MaxPos*2]=
        {
          "LB","PB", "LE","PE", // normal
          "LD","LH", "PD","PH", // crossing
        };
        //int memLevel = obj->GetShape()->FindMemoryLevel();
        const Shape *shape=obj->GetShape()->MemoryLevel();
        
        //float avgDistLR=0;
        if (!shape)
        {
          // synthesize LB, PB, LE, PE points from nothing if possible
          // use geometry min-max
          const Shape *geom = obj->GetShape()->GeometryLevel();
          if (!geom) continue;
          LogF
          (
            "Road shape %s missing connection points",
            obj->GetShape()->Name()
          );
          Vector3Val min=geom->Min();
          Vector3Val max=geom->Max();
          Vector3Val cnt = (min+max)*0.5;
          
          Vector3 sposB=Vector3(cnt.X(),max.Y(),min.Z());
          Vector3 sposE=Vector3(cnt.X(),max.Y(),max.Z());
          obj->FutureVisualState().PositionModelToWorld(sposB,sposB);
          obj->FutureVisualState().PositionModelToWorld(sposE,sposE);
          pos[0]=sposB;
          pos[1]=sposE;
          type[0] = RoadLink::CTBegin;
          type[2] = RoadLink::CTEnd;
          d = 2;
        }
        else
        {
          for( int c=0; c<sizeof(pairs)/sizeof(*pairs)/2; c++ )
          {
            const char *l=pairs[c*2],*r=pairs[c*2+1];
            int lSel = obj->GetShape()->FindNamedSel(shape,l);
            int rSel = obj->GetShape()->FindNamedSel(shape,r);
            if( lSel<0 ) continue;
            if( rSel<0 ) continue;
            const NamedSelection::Access &rNamedSel = shape->NamedSel(rSel,obj->GetShape());
            const NamedSelection::Access &lNamedSel = shape->NamedSel(lSel,obj->GetShape());
            if (rNamedSel.Size()<=0) continue;
            if (lNamedSel.Size()<=0) continue;
            Vector3Val lPos=RoadPoint(obj,shape,lNamedSel[0]);
            Vector3Val rPos=RoadPoint(obj,shape,rNamedSel[0]);
            //float distLR=lPos.Distance(rPos);
            //avgDistLR+=distLR;
            type[d] = (RoadLink::ConnectionType)c;
            pos[d++]=(lPos+rPos)*0.5f;
          }
          //avgDistLR/=c;
        }
      }
      RoadLink *elem=NULL;
      if (d<=RoadLink::NCon) elem = new RoadLink(obj, pos, type, d);
      if( !elem )
      {
        Fail("Bad road element.");
        continue;
      }

      // insert into corresponding square
/*
      RoadList &list=SelectRoadList(avg.X(),avg.Z());
      list.Add(elem);
*/
      AddRoad(avg.X(), avg.Z(), elem);
    }
  }
}

Vector3 RoadLink::AIPosition() const
{
  Vector3 center = _toWorld.Position();
  
  // update the Y of the center to look better
  // this is necessary for bridges - their Y is not on the road surface
  float yMin = FLT_MAX;
  float yMax = -FLT_MAX;
  for (int i=0; i<NConnections(); i++)
  {
    float y = PosConnections()[i].Y();
    saturateMin(yMin, y);
    saturateMax(yMax, y);
  }
  center[1] = 0.5 * (yMin + yMax);
  return center;
}

Vector3 RoadLink::GetCenter() const
{
  if (_nCon == 0) return VZero;

  Vector3 center = VZero;
  for (int i=0; i<_nCon; i++)
  {
    center += _pos[i];
  }
  center *= 1.0 / _nCon;

  return center;
}

bool RoadLink::IsBridge() const
{
  const Shape *geom = GetLODShape()->GeometryLevel();
  return geom && geom->NFaces() > 0;
}

RoadLink::RoadType RoadLink::GetRoadType() const
{
  switch (_nCon)
  {
  case 0:
    return RTDisconnected;
  case 1:
    if (_type[0] == CTBegin) return RTBegin;
    if (_type[0] == CTEnd) return RTEnd;
    return RTError;
  case 2:
    if (_type[0] != CTBegin) return RTError;
    if (_type[1] != CTEnd) return RTError;
    return RTI;
  case 3:
    if (_type[0] != CTBegin) return RTError;
    if (_type[1] != CTEnd) return RTError;
    if (_type[2] == CTLeft) return RTTL;
    if (_type[2] == CTRight) return RTTR;
    return RTError;
  case 4:
    return RTX;
  default:
    return RTError;
  }
}

int RoadLink::NNodes(bool soldier) const
{
  // TODO: maybe some cases may be joined
  switch (GetRoadType())
  {
  case RTError:
  case RTDisconnected:
  default:
    // no valid connection
    // keep only node in the center of RoadLink
    return 1;
  case RTBegin:
  case RTEnd:
    // one connection
    Assert(!IsBridge());
    if (_con[0] && _con[0]->IsBridge())
      return 4; // add nodes near bridge
    // node on each road side
    return 2;
  case RTI:
    // two connections
    if (IsBridge())
    {
      // make the road net dense enough
      float length = _pos[0].DistanceXZ(_pos[1]);
      // first node OperItemGrid from the begin, further OperItemGrid from previous
      int segments = toIntFloor(length * InvOperItemGrid) - 1;
      saturateMax(segments, 1);
      return 2 * segments;
    }
    {
      int nBridges = 0;
      if (_con[0] && _con[0]->IsBridge()) nBridges++;
      if (_con[1] && _con[1]->IsBridge()) nBridges++;
      return 2 + 2 * nBridges;
    }
  case RTTR:
  case RTTL:
    // three connections
    Assert(!IsBridge());
    if (soldier) return 3 + 2 * 3; // bridge nodes enforced to cross over
    {
      int nBridges = 0;
      if (_con[0] && _con[0]->IsBridge()) nBridges++;
      if (_con[1] && _con[1]->IsBridge()) nBridges++;
      if (_con[2] && _con[2]->IsBridge()) nBridges++;
      return 3 + 2 * nBridges;
    }
  case RTX:
    // four connections
    Assert(!IsBridge());
    if (soldier) return 4 + 2 * 4; // bridge nodes enforced to cross over
    {
      int nBridges = 0;
      if (_con[0] && _con[0]->IsBridge()) nBridges++;
      if (_con[1] && _con[1]->IsBridge()) nBridges++;
      if (_con[2] && _con[2]->IsBridge()) nBridges++;
      if (_con[3] && _con[3]->IsBridge()) nBridges++;
      return 4 + 2 * nBridges;
    }
  }
}

RoadNode RoadLink::FindBestNode(bool soldier, float vehWidth, Vector3Par position, Vector3Par direction) const
{
  int n = NNodes(soldier);
  if (n == 0) return RoadNode(NULL, -1);

  float minDist2 = FLT_MAX;
  int bestIndex = 0;
  for (int i=0; i<n; i++)
  {
    RoadNode node = GetNode(i);
    Vector3 pos = node.Position(soldier, vehWidth);
    if (!soldier)
    {
      // check if direction of road in this node is not opposite the vehicle direction
      Vector3 dir = GetNodeDirection(i);
      if (dir * direction < 0) continue;
    }
    float dist2 = pos.Distance2(position);
    if (dist2 < minDist2)
    {
      bestIndex = i;
      minDist2 = dist2;
    }
  }

  return RoadNode(this, bestIndex);
}

Vector3 RoadLink::GetNodeDirection(int index) const
{
  RoadType type = GetRoadType();
  if (type == RTError || type == RTDisconnected)
  {
    // no valid connection
    return VZero;
  }

  // based on primary connection, select the points to calculate direction from
  switch (type)
  {
  case RTBegin:
  case RTEnd:
    // one connection (Begin or End)
    if (index == 0) // semantics: 0 - center right, in 0
      return (AIPosition() - _pos[0]).Normalized();
    if (index == 1) // semantics: 1 - center left, out 0
      return (_pos[0] - AIPosition()).Normalized();
    if (index == 2) // semantics: 2 - bridge right, in 0
      return (AIPosition() - _pos[0]).Normalized();
    Assert(index == 3); // semantics: 3 - bridge left, out 0
    return (_pos[0] - AIPosition()).Normalized();
  case RTI:
    // two connections (Begin, End)
    if (IsBridge())
    {
      // semantics: 0 - begin right, in 0
      // semantics: 1 - begin left, out 0
      // semantics: (2 * segments - 2) - end right, out 1
      // semantics: (2 * segments - 1) - end left, in 1
      int side = index % 2;
      if (side == 0)
      {
        // right side
        return (_pos[1] - _pos[0]).Normalized();
      }
      else
      {
        // left side
        return (_pos[0] - _pos[1]).Normalized();
      }
    }

    if (index == 0) // semantics: 0 - center right, in 0, out 1
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 1) // semantics: 1 - center left, out 0, in 1
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 2) // semantics: 2 - bridge #0 right, in 0
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 3) // semantics: 3 - bridge #0 left, out 0
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 4) // semantics: 4|2 - bridge #1 right, out 1
      return (_pos[1] - _pos[0]).Normalized();
    Assert(index == 5); // semantics: 5|3 - bridge #1 left, in 1
    return (_pos[0] - _pos[1]).Normalized();
  case RTTR:
    // three connections (Begin, End, Right)
    if (index == 0) // semantics: 0 - center left, out 0, in 1
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 1) // semantics: 1 - begin right, in 0, out 2
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 2) // semantics: 2 - end right, out 1, in 2
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 3) // semantics: 3 - bridge #0 right, in 0
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 4) // semantics: 4 - bridge #0 left, out 0
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 5) // semantics: 5|3 - bridge #1 right, out 1
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 6) // semantics: 6|4 - bridge #1 left, in 1
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 7) // semantics: 7|5|3 - bridge #2 begin, out 2
      return (_pos[2] - AIPosition()).Normalized();
    Assert(index == 8); // semantics: 8|6|4 - bridge #2 end, in 2
    return (AIPosition() - _pos[2]).Normalized();
  case RTTL:
    // three connections (Begin, End, Left)
    if (index == 0) // semantics: 0 - center right, in 0, out 1
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 1) // semantics: 1 - begin left, out 0, in 2
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 2) // semantics: 2 - end left, in 1, out 2
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 3) // semantics: 3 - bridge #0 right, in 0
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 4) // semantics: 4 - bridge #0 left, out 0
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 5) // semantics: 5|3 - bridge #1 right, out 1
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 6) // semantics: 6|4 - bridge #1 left, in 1
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 7) // semantics: 7|5|3 - bridge #2 begin, in 2
      return (AIPosition() - _pos[2]).Normalized();
    Assert(index == 8); // semantics: 8|6|4 - bridge #2 end, out 2
    return (_pos[2] - AIPosition()).Normalized();
  case RTX:
    // four connections (Begin, End, Left, Right)
    if (index == 0) // semantics: 0 - begin right, in 0, out 3
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 1) // semantics: 1 - begin left, out 0, in 2
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 2) // semantics: 2 - end right, out 1, in 3
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 3) // semantics: 3 - end left, in 1, out 2
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 4) // semantics: 4 - bridge #0 right, in 0
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 5) // semantics: 5 - bridge #0 left, out 0
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 6) // semantics: 6|4 - bridge #1 right, out 1
      return (_pos[1] - _pos[0]).Normalized();
    if (index == 7) // semantics: 7|5 - bridge #1 left, in 1
      return (_pos[0] - _pos[1]).Normalized();
    if (index == 8) // semantics: 8|6|4 - bridge #2 begin, in 2
      return (AIPosition() - _pos[2]).Normalized();
    if (index == 9) // semantics: 9|7|5 - bridge #2 end, out 2
      return (_pos[2] - AIPosition()).Normalized();
    if (index == 10) // semantics: 10|8|6|4 - bridge #3 begin, out 3
      return (_pos[2] - AIPosition()).Normalized();
    Assert(index == 11); // semantics: 11|9|7|5 - bridge #3 end, in 3
    return (AIPosition() - _pos[2]).Normalized();
  }

  Fail("Cannot be reached");
  return VZero;
}

Vector3 RoadLink::GetNodePositionXZ(int index, bool soldier, float vehWidth) const
{
  Vector3 pos = _toWorld.Position();

  RoadType type = GetRoadType();
  if (type == RTError || type == RTDisconnected)
  {
    // no valid connection
    // keep only node in the center of RoadLink
    return pos;
  }

  // calculate the width of the road
  // TODO: make it more off-line if needed
  float roadWidth = 4; //default
  if (_lshape)
  {
    const Shape *shape = _lshape->MemoryLevel(); 
    if (shape)
    {
      int lSel = shape->FindNamedSel("LB");
      int rSel = shape->FindNamedSel("PB");
      if (lSel >= 0 && rSel >= 0)
      {
        const NamedSelection::Access rNamedSel(shape->NamedSel(rSel), _lshape, shape);
        const NamedSelection::Access lNamedSel(shape->NamedSel(lSel), _lshape, shape);
        if (rNamedSel.Size()>=0 && lNamedSel.Size() >= 0)
        {
          Vector3Val lpos = shape->Pos(lNamedSel[0]);
          Vector3Val rpos = shape->Pos(rNamedSel[0]);
          roadWidth = rpos.DistanceXZ(lpos);   
        }
      }
    }
  }

  // offset of the node from the road center
  float offset = 0;
  if (soldier) offset = (roadWidth * 0.5f) + 0.1f; // walk on the side
  else
  {
    if (vehWidth < roadWidth)
    {
      // try to follow the center of the lane
      offset = 0.25 * roadWidth;
      // be sure that vehicle fits on road
      saturateMin(offset, 0.5 * (roadWidth - vehWidth) - 0.3f);
    }
  }

  // road direction
  Vector3 dir;
  if (_nCon >= 2)
  {
    Assert(_type[0] == CTBegin);
    Assert(_type[1] == CTEnd);
    dir = (_pos[1] - _pos[0]).Normalized();
  }
  else
  {
    Assert(_nCon == 1);
    dir = (pos - _pos[0]).Normalized();
  }

  // transfer offset to world coordinates
  Matrix3 rot;
  rot.SetUpAndDirection(VUp, dir);
  Vector3 aside = rot * VAside;

  // how far from the bridge edge is the last point of neighbor road
  const float bridgeOffset = 1.5 * OperItemGrid; // avoid to be in rasterized area
  const float crossingOffset = 0.1; // where to cross over road in crossings

  // Vector semantics:
  // dir - from begin to end
  // aside - from left to right

  // Node semantics description:
  // "i - position, conection1, connection 2, ..."
  // i ... node index
  // position ... where on the RoadLink
  // connection ... type of connection to other RoadLinks
  // For soldiers, orientation of edges and in / out pairs are inverted

  // decide about the position by the road type
  switch (type)
  {
  case RTBegin:
  case RTEnd:
    // one connection (Begin or End)
    if (index == 0) return pos + offset * aside; // semantics: 0 - center right, in 0
    if (index == 1) return pos - offset * aside; // semantics: 1 - center left, out 0
    Assert(_con[0] && _con[0]->IsBridge());
    if (index == 2) return _pos[0] + bridgeOffset * dir + offset * aside; // semantics: 2 - bridge right, in 0
    Assert(index == 3);
    return _pos[0] + bridgeOffset * dir - offset * aside; // semantics: 3 - bridge left, out 0
  case RTI:
    // two connections (Begin, End)
    if (IsBridge())
    {
      // semantics: 0 - begin right, in 0
      // semantics: 1 - begin left, out 0
      // semantics: (2 * segments - 2) - end right, out 1
      // semantics: (2 * segments - 1) - end left, in 1
      int segment = index / 2;
      int side = index - 2 * segment;
      if (side == 0) return _pos[0] + (segment + 1) * OperItemGrid * dir + offset * aside;
      else return _pos[0] + (segment + 1) * OperItemGrid * dir - offset * aside;
    }
    if (index == 0) return pos + offset * aside; // semantics: 0 - center right, in 0, out 1
    if (index == 1) return pos - offset * aside; // semantics: 1 - center left, out 0, in 1
    if (_con[0] && _con[0]->IsBridge())
    {
      if (index == 2) return _pos[0] + bridgeOffset * dir + offset * aside; // semantics: 2 - bridge #0 right, in 0
      if (index == 3) return _pos[0] + bridgeOffset * dir - offset * aside; // semantics: 3 - bridge #0 left, out 0
    }
    else index += 2; // skip begin
    Assert(_con[1] && _con[1]->IsBridge());
    if (index == 4) return _pos[1] - bridgeOffset * dir + offset * aside; // semantics: 4|2 - bridge #1 right, out 1
    Assert(index == 5);
    return _pos[1] - bridgeOffset * dir - offset * aside; // semantics: 5|3 - bridge #1 left, in 1
  case RTTR:
    // three connections (Begin, End, Right)
    if (index == 0) return 0.5 * (_pos[0] + _pos[1]) - offset * aside; // semantics: 0 - center left, out 0, in 1
    if (index == 1) return 0.5 * (_pos[0] + _pos[1]) - offset * dir + offset * aside; // semantics: 1 - begin right, in 0, out 2
    if (index == 2) return 0.5 * (_pos[0] + _pos[1]) + offset * dir + offset * aside; // semantics: 2 - end right, out 1, in 2
    if (soldier || _con[0] && _con[0]->IsBridge())
    {
      float dist = (_con[0] && _con[0]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 3) return _pos[0] + dist * dir + offset * aside; // semantics: 3 - bridge #0 right, in 0
      if (index == 4) return _pos[0] + dist * dir - offset * aside; // semantics: 4 - bridge #0 left, out 0
    }
    else index += 2; // skip begin
    if (soldier || _con[1] && _con[1]->IsBridge())
    {
      float dist = (_con[1] && _con[1]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 5) return _pos[1] - dist * dir + offset * aside; // semantics: 5|3 - bridge #1 right, out 1
      if (index == 6) return _pos[1] - dist * dir - offset * aside; // semantics: 6|4 - bridge #1 left, in 1
    }
    else index += 2; // skip end
    Assert(soldier || _con[2] && _con[2]->IsBridge());
    {
      float dist = (_con[2] && _con[2]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 7) return _pos[2] - dist * aside - offset * dir; // semantics: 7|5|3 - bridge #2 begin, out 2
      Assert(index == 8);
      return _pos[2] - dist * aside + offset * dir; // semantics: 8|6|4 - bridge #2 end, in 2
    }
  case RTTL:
    // three connections (Begin, End, Left)
    if (index == 0) return 0.5 * (_pos[0] + _pos[1]) + offset * aside; // semantics: 0 - center right, in 0, out 1
    if (index == 1) return 0.5 * (_pos[0] + _pos[1]) - offset * dir - offset * aside; // semantics: 1 - begin left, out 0, in 2
    if (index == 2) return 0.5 * (_pos[0] + _pos[1]) + offset * dir - offset * aside; // semantics: 2 - end left, in 1, out 2
    if (soldier || _con[0] && _con[0]->IsBridge())
    {
      float dist = (_con[0] && _con[0]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 3) return _pos[0] + dist * dir + offset * aside; // semantics: 3 - bridge #0 right, in 0
      if (index == 4) return _pos[0] + dist * dir - offset * aside; // semantics: 4 - bridge #0 left, out 0
    }
    else index += 2; // skip begin
    if (soldier || _con[1] && _con[1]->IsBridge())
    {
      float dist = (_con[1] && _con[1]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 5) return _pos[1] - dist * dir + offset * aside; // semantics: 5|3 - bridge #1 right, out 1
      if (index == 6) return _pos[1] - dist * dir - offset * aside; // semantics: 6|4 - bridge #1 left, in 1
    }
    else index += 2; // skip end
    Assert(soldier || _con[2] && _con[2]->IsBridge());
    {
      float dist = (_con[2] && _con[2]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 7) return _pos[2] + dist * aside - offset * dir; // semantics: 7|5|3 - bridge #2 begin, in 2
      Assert(index == 8);
      return _pos[2] + dist * aside + offset * dir; // semantics: 8|6|4 - bridge #2 end, out 2
    }
  case RTX:
    // four connections (Begin, End, Left, Right)
    if (index == 0) return 0.5 * (_pos[0] + _pos[1]) - offset * dir + offset * aside; // semantics: 0 - begin right, in 0, out 3
    if (index == 1) return 0.5 * (_pos[0] + _pos[1]) - offset * dir - offset * aside; // semantics: 1 - begin left, out 0, in 2
    if (index == 2) return 0.5 * (_pos[0] + _pos[1]) + offset * dir + offset * aside; // semantics: 2 - end right, out 1, in 3
    if (index == 3) return 0.5 * (_pos[0] + _pos[1]) + offset * dir - offset * aside; // semantics: 3 - end left, in 1, out 2
    if (soldier || _con[0] && _con[0]->IsBridge())
    {
      float dist = (_con[0] && _con[0]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 4) return _pos[0] + dist * dir + offset * aside; // semantics: 4 - bridge #0 right, in 0
      if (index == 5) return _pos[0] + dist * dir - offset * aside; // semantics: 5 - bridge #0 left, out 0
    }
    else index += 2; // skip begin
    if (soldier || _con[1] && _con[1]->IsBridge())
    {
      float dist = (_con[1] && _con[1]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 6) return _pos[1] - dist * dir + offset * aside; // semantics: 6|4 - bridge #1 right, out 1
      if (index == 7) return _pos[1] - dist * dir - offset * aside; // semantics: 7|5 - bridge #1 left, in 1
    }
    else index += 2; // skip end
    if (soldier || _con[2] && _con[2]->IsBridge())
    {
      float dist = (_con[2] && _con[2]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 8) return _pos[2] + dist * aside - offset * dir; // semantics: 8|6|4 - bridge #2 begin, in 2
      if (index == 9) return _pos[2] + dist * aside + offset * dir; // semantics: 9|7|5 - bridge #2 end, out 2
    }
    else index += 2; // skip left
    Assert(soldier || _con[3] && _con[3]->IsBridge());
    {
      float dist = (_con[3] && _con[3]->IsBridge()) ? bridgeOffset : crossingOffset;
      if (index == 10) return _pos[3] - dist * aside - offset * dir; // semantics: 10|8|6|4 - bridge #3 begin, out 3
      Assert(index == 11);
      return _pos[3] - dist * aside + offset * dir; // semantics: 11|9|7|5 - bridge #3 end, in 3
    }
  }

  Fail("Cannot be reached");
  return pos;
}

Vector3 RoadLink::GetNodePosition(int index, bool soldier, float vehWidth) const
{
  Vector3 xzPos = GetNodePositionXZ(index,soldier,vehWidth);
  if (IsBridge())
  {
    xzPos[1] = AIPosition().Y();
  }
  else
  {
    // when not on the bridge, we have a guarantee the road is stitched to the ground
    xzPos[1] = GLandscape->SurfaceYAboveWaterNoWaves(xzPos.X(),xzPos.Z());
  }
  return xzPos;
}

int RoadLink::NNodeConnections(int index, bool soldier) const
{
  RoadType type = GetRoadType();
  
  // no valid connection
  if (type == RTError || type == RTDisconnected) return 0;

  // find the node semantic by the type, find number of connection based on the type and index
  if (soldier)
  {
    // for soldiers, all edges are inverted
    // also, soldiers can cross over the road and their passing through crossings is different
    switch (type)
    {
    case RTBegin:
    case RTEnd:
      // one connection (Begin or End)
      if (index == 0) // semantics: 0 - center right, out 0
        return _con[0] ? 2 : 1; // to the center left, bridge right or _con[0]
      if (index == 1) // semantics: 1 - center left, in 0
        return 1; // to the center right
      Assert(_con[0] && _con[0]->IsBridge());
      if (index == 2) // semantics: 2 - bridge right, out 0
        return 2; // to the bridge left, _con[0]
      Assert(index == 3); // semantics: 3 - bridge left, in 0
      return 2; // to the bridge right, center left
    case RTI:
      // two connections (Begin, End)
      if (IsBridge())
      {
        // number of segments
        float length = _pos[0].DistanceXZ(_pos[1]);
        int segments = toIntFloor(length * InvOperItemGrid) - 1;
        saturateMax(segments, 1);

        // semantics: 0 - begin right, out 0
        // semantics: 1 - begin left, in 0
        // semantics: (2 * segments - 2) - end right, in 1
        // semantics: (2 * segments - 1) - end left, out 1
        int segment = index / 2;
        int side = index - 2 * segment;
        if (side == 0)
        {
          // right side
          if (segment == 0) return _con[0] ? 2 : 1; // crossing, out 0
          return 2; // inner node
        }
        else
        {
          // left side
          if (segment == segments - 1) return _con[1] ? 2 : 1; // crossing, out 1
          return 2; // inner node
        }
      }
      if (index == 0) // semantics: 0 - center right, out 0, in 1
        return _con[0] ? 2 : 1; // to the center left, bridge #0 right or _con[0]
      if (index == 1) // semantics: 1 - center left, in 0, out 1
        return _con[1] ? 2 : 1; // to the center right, bridge #1 left or _con[1]
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 2) // semantics: 2 - bridge #0 right, out 0
          return 2; // to the bridge #0 left, _con[0]
        if (index == 3) // semantics: 3 - bridge #0 left, in 0
          return 2; // to the bridge #0 right, center left
      }
      else index += 2; // skip begin
      Assert(_con[1] && _con[1]->IsBridge());
      if (index == 4) // semantics: 4|2 - bridge #1 right, in 1
        return 2; // to the bridge #1 left, center right 
      Assert(index == 5); // semantics: 5|3 - bridge #1 left, out 1
      return 2; // to the bridge #1 right, _con[1] 
    case RTTR:
      // three connections (Begin, End, Right)
      // bridge nodes enforced to cross over
      if (index == 0) // semantics: 0 - center left
        return 1; // to the bridge #1 left
      if (index == 1) // semantics: 1 - begin right
        return 1; // to the bridge #0 right
      if (index == 2) // semantics: 2 - end right
        return 1; // to the bridge #2 end 
      if (index == 3) // semantics: 3 - bridge #0 right, out 0
        return _con[0] ? 2 : 1; // to the bridge #0 left, _con[0]
      if (index == 4) // semantics: 4 - bridge #0 left, in 0
        return 2; // to the bridge #0 right, center left
      if (index == 5) // semantics: 5 - bridge #1 right, in 1
        return 2; // to the bridge #1 left, end right 
      if (index == 6) // semantics: 6 - bridge #1 left, out 1
        return _con[1] ? 2 : 1; // to the bridge #1 right, _con[1] 
      if (index == 7) // semantics: 7 - bridge #2 begin, in 2
        return 2; // to the bridge #2 end, begin right 
      Assert(index == 8); // semantics: 8 - bridge #2 end, out 2
      return _con[2] ? 2 : 1; // to the bridge #2 begin, _con[2]
    case RTTL:
      // three connections (Begin, End, Left)
      // bridge nodes enforced to cross over
      if (index == 0) // semantics: 0 - center right
        return 1; // to the bridge #0 right
      if (index == 1) // semantics: 1 - begin left
        return 1; // to the bridge #2 begin
      if (index == 2) // semantics: 2 - end left
        return 1; // to the bridge #1 left
      if (index == 3) // semantics: 3 - bridge #0 right, out 0
        return _con[0] ? 2 : 1; // to the bridge #0 left, _con[0] 
      if (index == 4) // semantics: 4 - bridge #0 left, in 0
        return 2; // to the bridge #0 right, begin left
      if (index == 5) // semantics: 5 - bridge #1 right, in 1
        return 2; // to the bridge #1 left, center right
      if (index == 6) // semantics: 6 - bridge #1 left, out 1
        return _con[1] ? 2 : 1; // to the bridge #1 right, _con[1]
      if (index == 7) // semantics: 7 - bridge #2 begin, out 2
        return _con[2] ? 2 : 1; // to the bridge #2 end, _con[2] 
      Assert(index == 8); // semantics: 8 - bridge #2 end, in 2
      return 2; // to the bridge #2 begin, end left 
    case RTX:
      // four connections (Begin, End, Left, Right)
      // bridge nodes enforced to cross over
      if (index == 0) // semantics: 0 - begin right
        return 1; // to the bridge #0 right
      if (index == 1) // semantics: 1 - begin left
        return 1; // to the bridge #2 begin
      if (index == 2) // semantics: 2 - end right
        return 1; // to the bridge #3 end
      if (index == 3) // semantics: 3 - end left
        return 1; // to the bridge #1 left
      if (index == 4) // semantics: 4 - bridge #0 right, out 0
        return _con[0] ? 2 : 1; // to the bridge #0 left, _con[0]
      if (index == 5) // semantics: 5 - bridge #0 left, in 0
        return 2; // to the bridge #0 right, begin left
      if (index == 6) // semantics: 6 - bridge #1 right, in 1
        return 2; // to the bridge #1 left, end right 
      if (index == 7) // semantics: 7 - bridge #1 left, out 1
        return _con[1] ? 2 : 1; // to the bridge #1 right, _con[1]
      if (index == 8) // semantics: 8 - bridge #2 begin, out 2
        return _con[2] ? 2 : 1; // to the bridge #2 end, _con[2]
      if (index == 9) // semantics: 9 - bridge #2 end, in 2
        return 2; // to the bridge #2 begin, end left
      if (index == 10) // semantics: 10 - bridge #3 begin, in 3
        return 2; // to the bridge #3 end, begin right
      Assert(index == 11); // semantics: 11 - bridge #3 end, out 3
      return _con[3] ? 2 : 1; // to the bridge #3 begin, _con[3]
    }
  }
  else // vehicle
  {
    switch (type)
    {
    case RTBegin:
    case RTEnd:
      // one connection (Begin or End)
      if (index == 0) // semantics: 0 - center right, in 0
        return 0; // end node
      if (index == 1) // semantics: 1 - center left, out 0
        return _con[0] ? 1 : 0; // to the bridge left or _con[0]
      Assert(_con[0] && _con[0]->IsBridge());
      if (index == 2) // semantics: 2 - bridge right, in 0
        return 1; // to the center right
      Assert(index == 3); // semantics: 3 - bridge left, out 0
      return 1; // to the _con[0]
    case RTI:
      // two connections (Begin, End)
      if (IsBridge())
      {
        // number of segments
        float length = _pos[0].DistanceXZ(_pos[1]);
        int segments = toIntFloor(length * InvOperItemGrid) - 1;
        saturateMax(segments, 1);

        // semantics: 0 - begin right, in 0
        // semantics: 1 - begin left, out 0
        // semantics: (2 * segments - 2) - end right, out 1
        // semantics: (2 * segments - 1) - end left, in 1
        int segment = index / 2;
        int side = index - 2 * segment;
        if (side == 0)
        {
          // right side
          if (segment == segments - 1) return _con[1] ? 1 : 0; // out 1
          return 1; // inner node
        }
        else
        {
          // left side
          if (segment == 0) return _con[0] ? 1 : 0; // out 0
          return 1; // inner node
        }
      }
      if (index == 0) // semantics: 0 - center right, in 0, out 1
        return _con[1] ? 1 : 0; // to the bridge #1 right or _con[1]
      if (index == 1) // semantics: 1 - center left, out 0, in 1
        return _con[0] ? 1 : 0; // to the bridge #0 left or _con[0]
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 2) // semantics: 2 - bridge #0 right, in 0
          return 1; // to the center right
        if (index == 3) // semantics: 3 - bridge #0 left, out 0
          return 1; // to the _con[0]
      }
      else index += 2; // skip begin
      Assert(_con[1] && _con[1]->IsBridge());
      if (index == 4) // semantics: 4|2 - bridge #1 right, out 1
        return 1; // to the _con[1] 
      Assert(index == 5); // semantics: 5|3 - bridge #1 left, in 1
      return 1; // to the center left 
    case RTTR:
      // three connections (Begin, End, Right)
      if (index == 0) // semantics: 0 - center left, out 0, in 1
        return _con[0] ? 2 : 1; // to the begin right, bridge #0 left or _con[0]
      if (index == 1) // semantics: 1 - begin right, in 0, out 2
        return _con[2] ? 2 : 1; // to the end right, bridge #2 begin or _con[2]
      if (index == 2) // semantics: 2 - end right, out 1, in 2
        return _con[1] ? 2 : 1; // to the center left, bridge #1 right or _con[1] 
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 3) // semantics: 3 - bridge #0 right, in 0
          return 1; // to the begin right
        if (index == 4) // semantics: 4 - bridge #0 left, out 0
          return 1; // to the _con[0] 
      }
      else index += 2; // skip begin
      if (_con[1] && _con[1]->IsBridge())
      {
        if (index == 5) // semantics: 5|3 - bridge #1 right, out 1
          return 1; // to the _con[1] 
        if (index == 6) // semantics: 6|4 - bridge #1 left, in 1
          return 1; // to the center left 
      }
      else index += 2; // skip end
      Assert(_con[2] && _con[2]->IsBridge());
      if (index == 7) // semantics: 7|5|3 - bridge #2 begin, out 2
        return 1; // to the _con[2] 
      Assert(index == 8); // semantics: 8|6|4 - bridge #2 end, in 2
      return 1; // to the end right
    case RTTL:
      // three connections (Begin, End, Left)
      if (index == 0) // semantics: 0 - center right, in 0, out 1
        return _con[1] ? 2 : 1; // to the end left, bridge #1 right or _con[1]
      if (index == 1) // semantics: 1 - begin left, out 0, in 2
        return _con[0] ? 2 : 1; // to the center right, bridge #0 left or _con[0]
      if (index == 2) // semantics: 2 - end left, in 1, out 2
        return _con[2] ? 2 : 1; // to the begin left, bridge #2 end or _con[2]
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 3) // semantics: 3 - bridge #0 right, in 0
          return 1; // to the center right 
        if (index == 4) // semantics: 4 - bridge #0 left, out 0
          return 1; // to the _con[0] 
      }
      else index += 2; // skip begin
      if (_con[1] && _con[1]->IsBridge())
      {
        if (index == 5) // semantics: 5|3 - bridge #1 right, out 1
          return 1; // to the _con[1] 
        if (index == 6) // semantics: 6|4 - bridge #1 left, in 1
          return 1; // to the end left
      }
      else index += 2; // skip end
      Assert(_con[2] && _con[2]->IsBridge());
      if (index == 7) // semantics: 7|5|3 - bridge #2 begin, in 2
        return 1; // to the begin left 
      Assert(index == 8); // semantics: 8|6|4 - bridge #2 end, out 2
      return 1; // to the _con[2] 
    case RTX:
      // four connections (Begin, End, Left, Right)
      if (index == 0) // semantics: 0 - begin right, in 0, out 3
        return _con[3] ? 2 : 1; // to the end right, bridge #3 begin or _con[3]
      if (index == 1) // semantics: 1 - begin left, out 0, in 2
        return _con[0] ? 2 : 1; // to the begin right, bridge #0 left or _con[0]
      if (index == 2) // semantics: 2 - end right, out 1, in 3
        return _con[1] ? 2 : 1; // to the end left, bridge #1 right or _con[1]
      if (index == 3) // semantics: 3 - end left, in 1, out 2
        return _con[2] ? 2 : 1; // to the begin left, bridge #2 end or _con[2]
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 4) // semantics: 4 - bridge #0 right, in 0
          return 1; // to the begin right
        if (index == 5) // semantics: 5 - bridge #0 left, out 0
          return 1; // to the _con[0]
      }
      else index += 2; // skip begin
      if (_con[1] && _con[1]->IsBridge())
      {
        if (index == 6) // semantics: 6|4 - bridge #1 right, out 1
          return 1; // to the _con[1] 
        if (index == 7) // semantics: 7|5 - bridge #1 left, in 1
          return 1; // to the end left
      }
      else index += 2; // skip end
      if (_con[2] && _con[2]->IsBridge())
      {
        if (index == 8) // semantics: 8|6|4 - bridge #2 begin, in 2
          return 1; // to the begin left
        if (index == 9) // semantics: 9|7|5 - bridge #2 end, out 2
          return 1; // to the _con[2]
      }
      else index += 2; // skip left
      Assert(_con[3] && _con[3]->IsBridge());
      if (index == 10) // semantics: 10|8|6|4 - bridge #3 begin, out 3
        return 1; // to the _con[3]
      Assert(index == 11); // semantics: 11|9|7|5 - bridge #3 end, in 3
      return 1; // to the end right
    }
  }

  return 0;
}

RoadNode RoadLink::GetEnterNode(const RoadLink *from, bool soldier) const
{
  // find from where the path is searching
  int index = -1;
  for (int i=0; i<_nCon; i++)
  {
    if (_con[i] == from)
    {
      index = i; break;
    }
  }
  if (index < 0) return RoadNode(NULL, 0);

  RoadType type = GetRoadType();
  // no valid connection
  if (type == RTError || type == RTDisconnected) return RoadNode(NULL, 0);

  // find the node semantic by the type
  if (soldier)
  {
    // for soldiers, all edges are inverted
    switch (type)
    {
    case RTBegin:
    case RTEnd:
      // one connection (Begin or End)
      Assert(index == 0); // in 0
      if (from->IsBridge()) return RoadNode(this, 3); // bridge left
      return RoadNode(this, 1); // center left
    case RTI:
      // two connections (Begin, End)
      if (IsBridge())
      {
        if (index == 0)
        {
          // in 0
          return RoadNode(this, 1); // begin left
        }
        Assert(index == 1); // in 1
        // number of segments
        float length = _pos[0].DistanceXZ(_pos[1]);
        int segments = toIntFloor(length * InvOperItemGrid) - 1;
        saturateMax(segments, 1);
        return RoadNode(this, 2 * segments - 2); // end right
      }
      if (index == 0)
      {
        // in 0
        if (from->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(0)); // bridge #0 left
        return RoadNode(this, 1); // center left
      }
      Assert(index == 1); // in 1
      if (from->IsBridge()) return RoadNode(this, 2 + 2 * GetBridgeIndex(1)); // bridge #1 right
      return RoadNode(this, 0); // center right
    case RTTR:
      // three connections (Begin, End, Right)
      // bridge nodes enforced to cross over
      if (index == 0)
      {
        // in 0
        return RoadNode(this, 4); // bridge #0 left
      }
      if (index == 1)
      {
        // in 1
        return RoadNode(this, 5); // bridge #1 right
      }
      Assert(index == 2); // in 2
      return RoadNode(this, 7); // bridge #2 begin
    case RTTL:
      // three connections (Begin, End, Left)
      // bridge nodes enforced to cross over
      if (index == 0)
      {
        // in 0
        return RoadNode(this, 4); // bridge #0 left
      }
      if (index == 1)
      {
        // in 1
        return RoadNode(this, 5); // bridge #1 right
      }
      Assert(index == 2); // in 2
      return RoadNode(this, 8); // bridge #2 end
    case RTX:
      // four connections (Begin, End, Left, Right)
      // bridge nodes enforced to cross over
      if (index == 0)
      {
        // in 0
        return RoadNode(this, 5); // bridge #0 left
      }
      if (index == 1)
      {
        // in 1
        return RoadNode(this, 6); // bridge #1 right
      }
      if (index == 2)
      {
        // in 2
        return RoadNode(this, 9); // bridge #2 end
      }
      Assert(index == 3); // in 3
      return RoadNode(this, 10); // bridge #3 begin
    }
  }
  else // vehicle
  {
    switch (type)
    {
    case RTBegin:
    case RTEnd:
      // one connection (Begin or End)
      Assert(index == 0); // in 0
      if (from->IsBridge()) return RoadNode(this, 2); // bridge right
      return RoadNode(this, 0); // center right
    case RTI:
      // two connections (Begin, End)
      if (IsBridge())
      {
        if (index == 0)
        {
          // in 0
          return RoadNode(this, 0); // begin right
        }
        Assert(index == 1); // in 1
        // number of segments
        float length = _pos[0].DistanceXZ(_pos[1]);
        int segments = toIntFloor(length * InvOperItemGrid) - 1;
        saturateMax(segments, 1);
        return RoadNode(this, 2 * segments - 1); // end left
      }
      if (index == 0)
      {
        // in 0
        if (from->IsBridge()) return RoadNode(this, 2 + 2 * GetBridgeIndex(0)); // bridge #0 right
        return RoadNode(this, 0); // center right
      }
      Assert(index == 1); // in 1
      if (from->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(1)); // bridge #1 left
      return RoadNode(this, 1); // center left
    case RTTR:
      // three connections (Begin, End, Right)
      if (index == 0)
      {
        // in 0
        if (from->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(0)); // bridge #0 right
        return RoadNode(this, 1); // begin right
      }
      if (index == 1)
      {
        // in 1
        if (from->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(1)); // bridge #1 left
        return RoadNode(this, 0); // center left
      }
      Assert(index == 2); // in 2
      if (from->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(2)); // bridge #2 end
      return RoadNode(this, 2); // end right
    case RTTL:
      // three connections (Begin, End, Left)
      if (index == 0)
      {
        // in 0
        if (from->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(0)); // bridge #0 right
        return RoadNode(this, 0); // center right
      }
      if (index == 1)
      {
        // in 1
        if (from->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(1)); // bridge #1 left
        return RoadNode(this, 2); // end left
      }
      Assert(index == 2); // in 2
      if (from->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(2)); // bridge #2 begin
      return RoadNode(this, 1); // begin left
    case RTX:
      // four connections (Begin, End, Left, Right)
      if (index == 0)
      {
        // in 0
        if (from->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(0)); // bridge #0 right
        return RoadNode(this, 0); // begin right
      }
      if (index == 1)
      {
        // in 1
        if (from->IsBridge()) return RoadNode(this, 5 + 2 * GetBridgeIndex(1)); // bridge #1 left
        return RoadNode(this, 3); // end left
      }
      if (index == 2)
      {
        // in 2
        if (from->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(2)); // bridge #2 begin
        return RoadNode(this, 1); // begin left
      }
      Assert(index == 3); // in 3
      if (from->IsBridge()) return RoadNode(this, 5 + 2 * GetBridgeIndex(3)); // bridge #3 end
      return RoadNode(this, 2); // end right
    }
  }

  return RoadNode(NULL, 0);
}

int RoadLink::GetBridgeIndex(int conIndex) const
{
  Assert(conIndex < _nCon);
  int index = 0; // the order of the bridge
  Assert(_nCon > 0);
  if (_con[0] && _con[0]->IsBridge())
  {
    if (conIndex == 0) return index;
    index++;
  }
  Assert(_nCon > 1);
  if (_con[1] && _con[1]->IsBridge())
  {
    if (conIndex == 1) return index;
    index++;
  }
  Assert(_nCon > 2);
  if (_con[2] && _con[2]->IsBridge())
  {
    if (conIndex == 2) return index;
    index++;
  }
  Assert(_nCon > 3);
  Assert(_con[3] && _con[3]->IsBridge());
  Assert(conIndex == 3);
  return index;
}

RoadNode RoadLink::GetNodeConnection(int index, bool soldier, int i) const
{
  RoadType type = GetRoadType();

  // no valid connection
  if (type == RTError || type == RTDisconnected) return RoadNode(NULL, 0);

  // find the node semantic by the type, find number of connection based on the type and index
  if (soldier)
  {
    // for soldiers, all edges are inverted
    switch (type)
    {
    case RTBegin:
    case RTEnd:
      // one connection (Begin or End)
      if (index == 0) // semantics: 0 - center right, out 0
      {
        if (i == 0) return RoadNode(this, 1); // center left
        Assert(_con[0]);
        Assert(i == 1);
        if (_con[0]->IsBridge()) return RoadNode(this, 2); // bridge right
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      if (index == 1) // semantics: 1 - center left, in 0
      {
        return RoadNode(this, 0); // center right
      }
      Assert(_con[0] && _con[0]->IsBridge());
      if (index == 2) // semantics: 2 - bridge right, out 0
      {
        if (i == 0) return RoadNode(this, 3); // bridge left
        Assert(i == 1);
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      Assert(index == 3); // semantics: 3 - bridge left, in 0
      if (i == 0) return RoadNode(this, 2); // bridge right
      Assert(i == 1);
      return RoadNode(this, 1); // center left
    case RTI:
      // two connections (Begin, End)
      if (IsBridge())
      {
        // number of segments
        float length = _pos[0].DistanceXZ(_pos[1]);
        int segments = toIntFloor(length * InvOperItemGrid) - 1;
        saturateMax(segments, 1);

        // semantics: 0 - begin right, out 0
        // semantics: 1 - begin left, in 0
        // semantics: (2 * segments - 2) - end right, in 1
        // semantics: (2 * segments - 1) - end left, out 1
        int segment = index / 2;
        int side = index - 2 * segment;
        if (side == 0)
        {
          // right side
          if (i == 0) return RoadNode(this, index + 1); // crossing
          Assert(i == 1);
          if (segment == 0) return _con[0]->GetEnterNode(this, soldier); // out 0
          return RoadNode(this, index - 2); // inner node
        }
        else
        {
          // left side
          if (i == 0) return RoadNode(this, index - 1); // crossing
          Assert(i == 1);
          if (segment == segments - 1) return _con[1]->GetEnterNode(this, soldier); // out 1
          return RoadNode(this, index + 2); // inner node
        }
      }

      if (index == 0) // semantics: 0 - center right, out 0, in 1
      {
        if (i == 0) return RoadNode(this, 1); // center left
        Assert(i == 1);
        Assert(_con[0]);
        if (_con[0]->IsBridge()) return RoadNode(this, 2 + 2 * GetBridgeIndex(0)); // bridge #0 right
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      if (index == 1) // semantics: 1 - center left, in 0, out 1
      {
        if (i == 0) return RoadNode(this, 0); // center right
        Assert(i == 1);
        Assert(_con[1]);
        if (_con[1]->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(1)); // bridge #1 left
        return _con[1]->GetEnterNode(this, soldier); // _con[1]
      }
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 2) // semantics: 2 - bridge #0 right, out 0
        {
          if (i == 0) return RoadNode(this, 3 + 2 * GetBridgeIndex(0)); // bridge #0 left
          Assert(i == 1);
          return _con[0]->GetEnterNode(this, soldier); // _con[0]
        }
        if (index == 3) // semantics: 3 - bridge #0 left, in 0
        {
          if (i == 0) return RoadNode(this, 2 + 2 * GetBridgeIndex(0)); // bridge #0 right
          Assert(i == 1);
          return RoadNode(this, 1); // center left
        }
      }
      else index += 2; // skip begin
      Assert(_con[1] && _con[1]->IsBridge());
      if (index == 4) // semantics: 4|2 - bridge #1 right, in 1
      {
        if (i == 0) return RoadNode(this, 3 + 2 * GetBridgeIndex(1)); // bridge #1 left
        Assert(i == 1);
        return RoadNode(this, 0); // center right
      }
      Assert(index == 5); // semantics: 5|3 - bridge #1 left, out 1
      if (i == 0) return RoadNode(this, 2 + 2 * GetBridgeIndex(1)); // bridge #1 right
      Assert(i == 1);
      return _con[1]->GetEnterNode(this, soldier); // _con[1]
    case RTTR:
      // three connections (Begin, End, Right)
      // bridge nodes enforced to cross over
      if (index == 0) // semantics: 0 - center left
        return RoadNode(this, 6); // bridge #1 left
      if (index == 1) // semantics: 1 - begin right
        return RoadNode(this, 3); // bridge #0 right
      if (index == 2) // semantics: 2 - end right
        return RoadNode(this, 8); // bridge #2 end
      if (index == 3) // semantics: 3 - bridge #0 right, out 0
      {
        if (i == 0) return RoadNode(this, 4); // bridge #0 left
        Assert(i == 1);
        Assert(_con[0]);
        return _con[0]->GetEnterNode(this, soldier); // _con[0] 
      }
      if (index == 4) // semantics: 4 - bridge #0 left, in 0
      {
        if (i == 0) return RoadNode(this, 3); // bridge #0 right
        Assert(i == 1);
        return RoadNode(this, 0); // center left
      }
      if (index == 5) // semantics: 5 - bridge #1 right, in 1
      {
        if (i == 0) return RoadNode(this, 6); // bridge #1 left
        Assert(i == 1);
        return RoadNode(this, 2); // end right
      }
      if (index == 6) // semantics: 6 - bridge #1 left, out 1
      {
        if (i == 0) return RoadNode(this, 5); // bridge #1 right
        Assert(i == 1);
        Assert(_con[1]);
        return _con[1]->GetEnterNode(this, soldier); // _con[1] 
      }
      if (index == 7) // semantics: 7 - bridge #2 begin, in 2
      {
        if (i == 0) return RoadNode(this, 8); // bridge #2 end
        Assert(i == 1);
        return RoadNode(this, 1); // begin right
      }
      Assert(index == 8); // semantics: 8 - bridge #2 end, out 2
      if (i == 0) return RoadNode(this, 7); // bridge #2 begin
      Assert(i == 1);
      Assert(_con[2]);
      return _con[2]->GetEnterNode(this, soldier); // _con[2] 
    case RTTL:
      // three connections (Begin, End, Left)
      // bridge nodes enforced to cross over
      if (index == 0) // semantics: 0 - center right
        return RoadNode(this, 3); // bridge #0 right
      if (index == 1) // semantics: 1 - begin left
        return RoadNode(this, 7); // bridge #2 begin
      if (index == 2) // semantics: 2 - end left
        return RoadNode(this, 6); // bridge #1 left
      if (index == 3) // semantics: 3 - bridge #0 right, out 0
      {
        if (i == 0) return RoadNode(this, 4); // bridge #0 left
        Assert(i == 1);
        Assert(_con[0]);
        return _con[0]->GetEnterNode(this, soldier); // _con[0] 
      }
      if (index == 4) // semantics: 4 - bridge #0 left, in 0
      {
        if (i == 0) return RoadNode(this, 3); // bridge #0 right
        Assert(i == 1);
        return RoadNode(this, 1); // begin left
      }
      if (index == 5) // semantics: 5 - bridge #1 right, in 1
      {
        if (i == 0) return RoadNode(this, 6); // bridge #1 left
        Assert(i == 1);
        return RoadNode(this, 0); // center right
      }
      if (index == 6) // semantics: 6 - bridge #1 left, out 1
      {
        if (i == 0) return RoadNode(this, 5); // bridge #1 right
        Assert(i == 1);
        Assert(_con[1]);
        return _con[1]->GetEnterNode(this, soldier); // _con[1] 
      }
      if (index == 7) // semantics: 7 - bridge #2 begin, out 2
      {
        if (i == 0) return RoadNode(this, 8); // bridge #2 end
        Assert(i == 1);
        Assert(_con[2]);
        return _con[2]->GetEnterNode(this, soldier); // _con[2] 
      }
      Assert(index == 8); // semantics: 8 - bridge #2 end, in 2
      if (i == 0) return RoadNode(this, 7); // bridge #2 begin
      Assert(i == 1);
      return RoadNode(this, 2); // end left
    case RTX:
      // four connections (Begin, End, Left, Right)
      // bridge nodes enforced to cross over
      if (index == 0) // semantics: 0 - begin right
        return RoadNode(this, 4); // bridge #0 right
      if (index == 1) // semantics: 1 - begin left
        return RoadNode(this, 8); // bridge #2 begin
      if (index == 2) // semantics: 2 - end right
        return RoadNode(this, 11); // bridge #3 end
      if (index == 3) // semantics: 3 - end left
        return RoadNode(this, 7); // bridge #1 left
      if (index == 4) // semantics: 4 - bridge #0 right, out 0
      {
        if (i == 0) return RoadNode(this, 5); // bridge #0 left
        Assert(i == 1);
        Assert(_con[0]);
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      if (index == 5) // semantics: 5 - bridge #0 left, in 0
      {
        if (i == 0) return RoadNode(this, 4); // bridge #0 right
        Assert(i == 1);
        return RoadNode(this, 1); // begin left
      }
      if (index == 6) // semantics: 6 - bridge #1 right, in 1
      {
        if (i == 0) return RoadNode(this, 7); // bridge #1 left
        Assert(i == 1);
        return RoadNode(this, 2); // end right
      }
      if (index == 7) // semantics: 7 - bridge #1 left, out 1
      {
        if (i == 0) return RoadNode(this, 6); // bridge #1 right
        Assert(i == 1);
        Assert(_con[1]);
        return _con[1]->GetEnterNode(this, soldier); // _con[1]
      }
      if (index == 8) // semantics: 8 - bridge #2 begin, out 2
      {
        if (i == 0) return RoadNode(this, 9); // bridge #2 end
        Assert(i == 1);
        Assert(_con[2]);
        return _con[2]->GetEnterNode(this, soldier); // _con[2]
      }
      if (index == 9) // semantics: 9 - bridge #2 end, in 2
      {
        if (i == 0) return RoadNode(this, 8); // bridge #2 begin
        Assert(i == 1);
        return RoadNode(this, 3); // end left
      }
      if (index == 10) // semantics: 10 - bridge #3 begin, in 3
      {
        if (i == 0) return RoadNode(this, 11); // bridge #3 end
        Assert(i == 1);
        return RoadNode(this, 0); // begin right
      }
      Assert(index == 11); // semantics: 11|9|7|5 - bridge #3 end, out 3
      if (i == 0) return RoadNode(this, 10); // bridge #3 begin
      Assert(i == 1);
      Assert(_con[3]);
      return _con[3]->GetEnterNode(this, soldier); // _con[3]
    }
  }
  else // vehicle
  {
    switch (type)
    {
    case RTBegin:
    case RTEnd:
      // one connection (Begin or End)
      if (index == 0) // semantics: 0 - center right, in 0
        return RoadNode(NULL, 0); // end node
      Assert(_con[0]);
      if (index == 1) // semantics: 1 - center left, out 0
      {
        if (_con[0]->IsBridge()) return RoadNode(this, 3); // bridge left
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      Assert(_con[0]->IsBridge());
      if (index == 2) // semantics: 2 - bridge right, in 0
        return RoadNode(this, 0); // center right
      Assert(index == 3); // semantics: 3 - bridge left, out 0
      return _con[0]->GetEnterNode(this, soldier); // _con[0]
    case RTI:
      // two connections (Begin, End)
      if (IsBridge())
      {
        // number of segments
        float length = _pos[0].DistanceXZ(_pos[1]);
        int segments = toIntFloor(length * InvOperItemGrid) - 1;
        saturateMax(segments, 1);

        // semantics: 0 - begin right, in 0
        // semantics: 1 - begin left, out 0
        // semantics: (2 * segments - 2) - end right, out 1
        // semantics: (2 * segments - 1) - end left, in 1
        int segment = index / 2;
        int side = index - 2 * segment;
        if (side == 0)
        {
          // right side
          if (segment == segments - 1) return _con[1]->GetEnterNode(this, soldier); // out 1
          return RoadNode(this, index + 2); // inner node
        }
        else
        {
          // left side
          if (segment == 0) return _con[0]->GetEnterNode(this, soldier); // out 0
          return RoadNode(this, index - 2); // inner node
        }
      }

      if (index == 0) // semantics: 0 - center right, in 0, out 1
      {
        Assert(_con[1]);
        if (_con[1]->IsBridge()) return RoadNode(this, 2 + 2 * GetBridgeIndex(1)); // bridge #1 right
        return _con[1]->GetEnterNode(this, soldier); // _con[1]
      }
      if (index == 1) // semantics: 1 - center left, out 0, in 1
      {
        Assert(_con[0]);
        if (_con[0]->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(0)); // bridge #0 left
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 2) // semantics: 2 - bridge #0 right, in 0
          return RoadNode(this, 0); // center right
        if (index == 3) // semantics: 3 - bridge #0 left, out 0
          return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      else index += 2; // skip begin
      Assert(_con[1] && _con[1]->IsBridge());
      if (index == 4) // semantics: 4|2 - bridge #1 right, out 1
        return _con[1]->GetEnterNode(this, soldier); // _con[1]
      Assert(index == 5); // semantics: 5|3 - bridge #1 left, in 1
      return RoadNode(this, 1); // center left
    case RTTR:
      // three connections (Begin, End, Right)
      if (index == 0) // semantics: 0 - center left, out 0, in 1
      {
        if (i == 0) return RoadNode(this, 1); // begin right
        Assert(_con[0]);
        if (_con[0]->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(0)); // bridge #0 left
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      if (index == 1) // semantics: 1 - begin right, in 0, out 2
      {
        if (i == 0) return RoadNode(this, 2); // end right
        Assert(_con[2]);
        if (_con[2]->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(2)); // bridge #2 begin
        return _con[2]->GetEnterNode(this, soldier); // _con[2]
      }
      if (index == 2) // semantics: 2 - end right, out 1, in 2
      {
        if (i == 0) return RoadNode(this, 0); // center left
        Assert(_con[1]);
        if (_con[1]->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(1)); // bridge #1 right
        return _con[1]->GetEnterNode(this, soldier); // _con[1] 
      }
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 3) // semantics: 3 - bridge #0 right, in 0
          return RoadNode(this, 1); // begin right
        if (index == 4) // semantics: 4 - bridge #0 left, out 0
          return _con[0]->GetEnterNode(this, soldier); // _con[0] 
      }
      else index += 2; // skip begin
      if (_con[1] && _con[1]->IsBridge())
      {
        if (index == 5) // semantics: 5|3 - bridge #1 right, out 1
          return _con[1]->GetEnterNode(this, soldier); // _con[1] 
        if (index == 6) // semantics: 6|4 - bridge #1 left, in 1
          return RoadNode(this, 0); // center left 
      }
      else index += 2; // skip end
      Assert(_con[2] && _con[2]->IsBridge());
      if (index == 7) // semantics: 7|5|3 - bridge #2 begin, out 2
        return _con[2]->GetEnterNode(this, soldier); // _con[2] 
      Assert(index == 8); // semantics: 8|6|4 - bridge #2 end, in 2
      return RoadNode(this, 2); // end right
    case RTTL:
      // three connections (Begin, End, Left)
      if (index == 0) // semantics: 0 - center right, in 0, out 1
      {
        if (i == 0) return RoadNode(this, 2); // end left
        if (_con[1]->IsBridge()) return RoadNode(this, 3 + 2 * GetBridgeIndex(1)); // bridge #1 right
        return _con[1]->GetEnterNode(this, soldier); // _con[1] 
      }
      if (index == 1) // semantics: 1 - begin left, out 0, in 2
      {
        if (i == 0) return RoadNode(this, 0); // center right
        if (_con[0]->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(0)); // bridge #0 left
        return _con[0]->GetEnterNode(this, soldier); // _con[0] 
      }
      if (index == 2) // semantics: 2 - end left, in 1, out 2
      {
        if (i == 0) return RoadNode(this, 1); // begin left
        if (_con[2]->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(2)); // bridge #2 end
        return _con[2]->GetEnterNode(this, soldier); // _con[2] 
      }
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 3) // semantics: 3 - bridge #0 right, in 0
          return RoadNode(this, 0); // center right
        if (index == 4) // semantics: 4 - bridge #0 left, out 0
          return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      else index += 2; // skip begin
      if (_con[1] && _con[1]->IsBridge())
      {
        if (index == 5) // semantics: 5|3 - bridge #1 right, out 1
          return _con[1]->GetEnterNode(this, soldier); // _con[1]
        if (index == 6) // semantics: 6|4 - bridge #1 left, in 1
          return RoadNode(this, 2); // end left
      }
      else index += 2; // skip end
      Assert(_con[2] && _con[2]->IsBridge());
      if (index == 7) // semantics: 7|5|3 - bridge #2 begin, in 2
        return RoadNode(this, 1); // begin left
      Assert(index == 8); // semantics: 8|6|4 - bridge #2 end, out 2
      return _con[2]->GetEnterNode(this, soldier); // _con[2]
    case RTX:
      // four connections (Begin, End, Left, Right)
      if (index == 0) // semantics: 0 - begin right, in 0, out 3
      {
        if (i == 0) return RoadNode(this, 2); // end right
        if (_con[3]->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(3)); // bridge #3 begin
        return _con[3]->GetEnterNode(this, soldier); // _con[3]
      }
      if (index == 1) // semantics: 1 - begin left, out 0, in 2
      {
        if (i == 0) return RoadNode(this, 0); // begin right
        if (_con[0]->IsBridge()) return RoadNode(this, 5 + 2 * GetBridgeIndex(0)); // bridge #0 left
        return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      if (index == 2) // semantics: 2 - end right, out 1, in 3
      {
        if (i == 0) return RoadNode(this, 3); // end left
        if (_con[1]->IsBridge()) return RoadNode(this, 4 + 2 * GetBridgeIndex(1)); // bridge #1 right
        return _con[1]->GetEnterNode(this, soldier); // _con[1]
      }
      if (index == 3) // semantics: 3 - end left, in 1, out 2
      {
        if (i == 0) return RoadNode(this, 1); // begin left
        if (_con[2]->IsBridge()) return RoadNode(this, 5 + 2 * GetBridgeIndex(2)); // bridge #2 end
        return _con[2]->GetEnterNode(this, soldier); // _con[2]
      }
      if (_con[0] && _con[0]->IsBridge())
      {
        if (index == 4) // semantics: 4 - bridge #0 right, in 0
          return RoadNode(this, 0); // begin right
        if (index == 5) // semantics: 5 - bridge #0 left, out 0
          return _con[0]->GetEnterNode(this, soldier); // _con[0]
      }
      else index += 2; // skip begin
      if (_con[1] && _con[1]->IsBridge())
      {
        if (index == 6) // semantics: 6|4 - bridge #1 right, out 1
          return _con[1]->GetEnterNode(this, soldier); // _con[1]
        if (index == 7) // semantics: 7|5 - bridge #1 left, in 1
          return RoadNode(this, 3); // end left
      }
      else index += 2; // skip end
      if (_con[2] && _con[2]->IsBridge())
      {
        if (index == 8) // semantics: 8|6|4 - bridge #2 begin, in 2
          return RoadNode(this, 1); // begin left
        if (index == 9) // semantics: 9|7|5 - bridge #2 end, out 2
          return _con[2]->GetEnterNode(this, soldier); // _con[2]
      }
      else index += 2; // skip left
      Assert(_con[3] && _con[3]->IsBridge());
      if (index == 10) // semantics: 10|8|6|4 - bridge #3 begin, out 3
        return _con[3]->GetEnterNode(this, soldier); // _con[3]
      Assert(index == 11); // semantics: 11|9|7|5 - bridge #3 end, in 3
      return RoadNode(this, 2); // end right
    }
  }

  return RoadNode(NULL, 0);
}

/*!
\patch 5122 Date 1/24/2007 by Jirka
- Fixed: AI was planning the path over the destroyed bridge
*/

int RoadLink::IsLocked(bool soldier) const
{
  // return number of locks compressed to the form as LockField::IsLocked returns
  int locksAll = intMin(_locksVehicle, 0x0f); // lock used for both vehicles and soldiers
  int locksVeh = soldier ? 0 : intMin(_locksSoldier, 0x0f); // lock used for vehicles only (soldier - soldier lock disabled)
  return (locksAll << 4) | locksVeh;

  // TODO: increment _lockVehicle for damaged roads (bridges)

/*
  if (_object.IsNull()) return true;
  // if object is unloaded, it may not be destroyed, as loading it back will bring it in the original state
  if (!_object.IsLoaded()) return false;
  return _object.GetLock()->IsDamageDestroyed();
*/
}

int RoadLink::IsLockedByWaiting(bool soldier) const
{
  int locksAll = intMin(_locksVehicleWaiting, 0x0f); // lock used for both vehicles and soldiers
  int locksVeh = soldier ? 0 : intMin(_locksSoldierWaiting, 0x0f); // lock used for vehicles only (soldier - soldier lock disabled)
  return (locksAll << 4) | locksVeh;
}

// copy and add connection

void RoadLink::AddConnection( Vector3Par pos, RoadLink *con )
{
  if (_nCon>=NCon)
  {
    void PositionToAA11(Vector3Val pos, char *buffer);

    char buf[1024];
    PositionToAA11(pos,buf);
    RptF
    (
      "No more slot to add connection at %s (%.1f,%.1f)",
      buf,pos.X(),pos.Z()
      //(const char *)con->_object.GetLock()->GetDebugName(),
      //(const char *)_object.GetLock()->GetDebugName()
    );

    return; 
  }
  // try to guess connection type, reorder connections if needed
  int index = _nCon;
  _type[index] = CTNone;
  switch (_nCon)
  {
  case 0:
    _type[0] = CTBegin;
    break;
  case 1:
    if (_type[0] == CTBegin)
    {
      _type[1] = CTEnd;
    }
    else
    {
      // try to keep the order begin, end
      _con[1] = _con[0];
      _pos[1] = _pos[0];
      _type[1] = _type[0];
      // 0 will be the new node
      _type[0] = CTBegin;
      index = 0;
    }
    break;
  case 2:
    if (_type[0] != CTBegin) break;
    if (_type[1] != CTEnd) break;
    // decide left / right turn
    {
      Vector3 product = (_pos[1] - _pos[0]).CrossProduct(con->Transform().Position() - Transform().Position());
      if (product.Y() <= 0) _type[2] = CTLeft;
      else _type[2] = CTRight;
    }
    break;
  case 3:
    if (_type[0] != CTBegin) break;
    if (_type[1] != CTEnd) break;
    if (_type[2] == CTLeft)
    {
      _type[3] = CTRight;
    }
    else
    {
      // try to keep the order left, right
      _con[3] = _con[2];
      _pos[3] = _pos[2];
      _type[3] = _type[2];
      // 2 will be the new node
      _type[2] = CTLeft;
      index = 2;
    }
    break;
  }

  _con[index] = con;
  _pos[index] = pos;
  _nCon++;
}

static bool IsConnected(const RoadLink *tItem, const RoadLink *item, int maxDepth)
{
  --maxDepth;
  for (int jj=0; jj<tItem->NConnections(); jj++)
  {
    const RoadLink *cItem=tItem->Connections()[jj];
    if (cItem==item) return true;
    if (maxDepth>0 && cItem)
    {
      // TODO: optimize for bigger depths (closed list)
      if (IsConnected(cItem,item,maxDepth))
      {
        return true;
      }
    }
  }
  return false;
}


#define DIAGS 0
#define DIAG_COUNT 1
void RoadNet::Connect(bool pass1Only)
{
  void PositionToAA11(Vector3Val pos, char *buffer);
  #if DIAG_COUNT
    int count[6];
    memset(count,0,sizeof(count));
  #endif
  // connect as necessary
  const float maxDistCon=1.0;
  for( int x=0; x<LandRange; x++ ) for( int z=0; z<LandRange; z++ )
  {
    const RoadList &list=_roads(x,z);
    for( int o=0; o<list.Size(); o++ )
    {
      RoadLink *road=list[o];
      int nCon=road->NConnections();
      #if DIAG_COUNT
        count[nCon]++;
      #endif
      const Vector3 *pos=road->PosConnections();
      RoadLink *const* conn=road->Connections();
      // if there is already some connection, skip it
      for( int i=0; i<nCon; i++ ) if( !conn[i] )
      {
        Vector3Val val=pos[i];
        // search all other roads for same position
        // there should be max. one present
        for( int xx=x-1; xx<=x+1; xx++ )
        for( int zz=z-1; zz<=z+1; zz++ )
        if( InRange(xx,zz) )
        {
          const RoadList &search=_roads(xx,zz);
          for( int si=0; si<search.Size(); si++ )
          {
            RoadLink *sRoad=search[si];
            if( sRoad==road ) continue;
            int sCon=sRoad->NConnections();
            const Vector3 *sPos=sRoad->PosConnections();
            for( int ii=0; ii<sCon; ii++ )
            {
              // FIX: use 3D distance
              float dist2=(sPos[ii]-val).SquareSize();
              if( dist2<Square(maxDistCon) )
              {
                #if DIAGS
                  LogF("Connect %x:%d - %x:%d",road,i,sRoad,ii);
                #endif
                // connection found - mark it
                #if 0
                if( dist2>Square(0.1) )
                {
                  char buf[1024];
                  PositionToAA11(sPos[ii],buf);
                  LogF
                  (
                    "Bad connection %.1f,%.1f %.1f,%.1f - %s",
                    sPos[ii].X(),sPos[ii].Z(),
                    val.X(),val.Z(),buf
                  );
                }
                #endif

                // connect both together
                road->SetConnection(i,sRoad);
                sRoad->SetConnection(ii,road);
                #if _RELEASE
                  goto ConnFound;
                #endif
              }
            }
          }
        }
        #if _RELEASE
          ConnFound:;
        #endif
      }
    }
  }
  #if DIAG_COUNT
  {
    // some diagnostic information
    for( int i=0; i<6; i++ )
    {
      LogF("Road %d dirs: %d",i,count[i]);
    }
  } 
  #endif
  
  if (pass1Only) return;

  // second pass: try to connect road unconnected points to something

  const float maxDistConPass2=10.0;
  const float maxHeightDiffPass2=1.0;
  for (int x=0; x<LandRange; x++) for (int z=0; z<LandRange; z++)
  {
    const RoadList &list=_roads(x,z);

    int n = list.Size();
    for (int i=0; i<n; i++)
    {
      RoadLink *item = list[i];
      for (int j=0; j<item->NConnections(); j++)
      {
        if (!item->Connections()[j])
        {
          // something not connected
          // try to find some connection point
          Vector3 pos = item->PosConnections()[j];
          float minDist2 = Square(maxDistConPass2); // do not connect under this limit
          RoadLink *bestItem = NULL;
          int bestIndex = -1;

          // scan all near road points (+-1)
          for (int xx=x-1; xx<=x+1; xx++) for (int zz=z-1; zz<=z+1; zz++)
          {
            if (!InRange(xx,zz)) continue;
            
            const RoadList &tList=_roads(xx,zz);
            int nn = tList.Size();
            for (int ii=0; ii<nn; ii++)
            {
              // check connection with all connection points of tItem
              RoadLink *tItem = tList[ii];
              if (tItem == item) continue;
              // skip item 
              // skip items that are directly connected
              // TODO: search to certain depth
              if (IsConnected(tItem,item,1))
              {
                #if DIAGS
                  char buf[256];
                  PositionToAA11(pos,buf);
                  LogF
                  (
                    "%s: Already Connected %d to %d",
                    buf, item->GetObject()->ID(),
                    tItem->GetObject()->ID()
                  );
                #endif
                continue;
              }

              // now test all free connections of tList
              int count = 0; // number of free connections
              for (int jj=0; jj<tItem->NConnections(); jj++)
              {
                if (tItem->Connections()[jj]) continue; // ignore already used connections
                count++;

                // check distance to item point
                Vector3Val tPos = tItem->PosConnections()[jj];
                if (fabs(tPos.Y() - pos.Y()) > maxHeightDiffPass2) continue; // different heights

                float dist2 = pos.Distance2Inline(tPos);
                if (dist2 < minDist2)
                {
                  minDist2 = dist2;
                  bestItem = tItem;
                  bestIndex = jj;
                }
              } // for (jj)
              // if no free connection found, check if a new one can be created
              if (count == 0 && tItem->NConnections() < RoadLink::NCon)
              {
                Vector3 tPos = tItem->AIPosition();
                if (fabs(tPos.Y() - pos.Y()) > maxHeightDiffPass2) continue; // different heights

                float dist2 = pos.Distance2Inline(tPos);
                if (dist2 < minDist2)
                {
                  minDist2 = dist2;
                  bestItem = tItem;
                  bestIndex = -1;
                }
              }
            } // for (ii)
          } // for(xx,zz)

          if (bestItem)
          {
            // candidate found, connect
#if DIAGS
            char buf[256];
            PositionToAA11(pos,buf);
            LogF(
              "%s: Connect %d with %d",
              buf, item->GetObject()->ID(),
              bestItem->GetObject()->ID()
              );
#endif
            if (bestIndex >= 0)
            {
              // connection is empty
              item->SetConnection(j, bestItem);
              bestItem->SetConnection(bestIndex, item);
              // move connection points to match better (avoid 2nd pass for binarized wrp)
              const Vector3 &pos1 = item->PosConnections()[j];
              const Vector3 &pos2 = bestItem->PosConnections()[bestIndex];
              Vector3 pos = 0.5 * (pos1 + pos2);
              item->SetPosConnection(j, pos);
              bestItem->SetPosConnection(bestIndex, pos);
            }
            else
            {
              // connection not empty - we need to add another one
              item->SetConnection(j, bestItem);
              bestItem->AddConnection(pos, item); // add connection to the position matching to connected RoadLink
            }
          }
          else
          {
#if DIAGS
            char buf[256];
            PositionToAA11(pos,buf);
            LogF(
              "%s: No Connection found for %d",
              buf, item->GetObject()->ID(),
              );
#endif
          }
        }
      } // for (j) 
    } // for(i)
  } // for(x,z)

  // 3rd pass: make sure connections are symmetric
  
  for (int x=0; x<LandRange; x++) for (int z=0; z<LandRange; z++)
  {
    const RoadList &list=_roads(x,z);

    int n = list.Size();
    for (int i=0; i<n; i++)
    {
      RoadLink *item = list[i];
      for (int j=0; j<item->NConnections(); j++)
      {
        if (RoadLink *linkTo = item->Connections()[j])
        {
          // verify connection back to us exists
          // if not, add it
          bool found = false;
          for (int l=0; l<linkTo->NConnections(); l++)
          {
            if (linkTo->Connections()[l]==item)
            {
              found = true;
              break;
            }
          }
          if (!found)
          {
            Vector3 pos = item->PosConnections()[j];
            char buf[256];
            PositionToAA11(pos,buf);
            LogF(
              "Adding symmetric connection at %s between %d and %d",
              buf,item->GetObject()->ID().Encode(),linkTo->GetObject()->ID().Encode()
            );

            // TODO: if there is an empty slot with position pos ready in linkTo, use it
            linkTo->AddConnection(pos,item);
          }
          
        }
      }
    }
  }
}

void RoadNet::Optimize()
{
  // merge small straight elements
  // TODO: implement
}
void RoadNet::Compact()
{
  // optimize memory image
  Log("Before RoadNet::Compact: %d",MemoryUsed());
  for( int x=0; x<_roads.GetXRange(); x++ ) for( int z=0; z<_roads.GetYRange(); z++ )
  {
    const RoadList &list = _roads(x, z);
    RoadListFull *listFull = list.GetList();
    if (listFull)
    {
      listFull->Compact();
      if (listFull->Size() == 0) _roads.Set(x, z, RoadList());
    }
  }
  Log("After RoadNet::Compact: %d",MemoryUsed());
}

void RoadNet::Clear()
{
/*
  for( int x=0; x<_roads.GetXRange(); x++ ) for( int z=0; z<_roads.GetYRange(); z++ )
  {
    RoadList &list=_roads(x,z);
    list.Clear();
  }
*/
  _roads.Init(RoadList());
}


bool RoadNet::IsOnRoadReady(Vector3Par pos) const
{
  /*
  int xx = toIntFloor(pos.X() * InvLandGrid);
  int zz = toIntFloor(pos.Z() * InvLandGrid);
  for ( int zi=zz-1; zi<=zz+1; zi++) for ( int xi=xx-1; xi<=xx+1; xi++)
  {
    if (!InRange(xi, zi)) continue;
    const RoadList &roadList = _roads(xi, zi);
    for( int i=0; i<roadList.Size(); i++ )
    {
      const RoadLink *roadLink=roadList[i];
      if (!roadLink->IsInsideReady()) return false;
    }
  }
  */
  return true;
}

const RoadLink *RoadNet::IsOnRoad(Vector3Par pos, float sizeXZ, float sizeY) const
{
  int xx = toIntFloor(pos.X() * InvLandGrid);
  int zz = toIntFloor(pos.Z() * InvLandGrid);
  for ( int zi=zz-1; zi<=zz+1; zi++) for ( int xi=xx-1; xi<=xx+1; xi++)
  {
    if (!InRange(xi, zi)) continue;
    const RoadList &roadList = _roads(xi, zi);
    for( int i=0; i<roadList.Size(); i++ )
    {
      const RoadLink *roadLink=roadList[i];
      if( roadLink->IsInside(pos, sizeXZ, sizeY) ) return roadLink;
    }
  }
  return NULL;
}

// changed - search only for points in centers of road segments (nodes of path planning)
Vector3 RoadNet::GetNearestRoadPoint(Vector3Par from, Vector3Par pos, float minDist, float vehSize, bool soldier, float vehWidth) const
{
  Assert(LandRange==_roads.GetXRange());
  Assert(LandRange==_roads.GetYRange());
  // scan all roads near pos
  // check 1-1 connections
  float maxMaxDist2 = Square(30);
  float maxDist2 = maxMaxDist2;
  float maxDistFallback2 = maxMaxDist2;
  const RoadLink *onRoad = IsOnRoad(pos, vehSize, vehSize);
  //const RoadLink *bestLink = NULL;
  Vector3 best = pos;
  Vector3 bestFallback = pos;
  Vector3 dir = (pos - from).Normalized();

  // if nothing better is found, use the road on which the target position is located
  if (onRoad)
  {
    if (!onRoad->IsInside(from, vehSize, vehSize))
    {
      RoadNode node = onRoad->FindBestNode(soldier, vehWidth, pos, dir);
      if (node.IsValid())
      {
        bestFallback = node.Position(soldier, vehWidth);
        maxDistFallback2 = 0;
        //bestLink = onRoad;
      }
    }
    if (onRoad->IsBridge())
    {
      // extend the search, as bridge parts are often quite big
      maxMaxDist2 = maxDist2 = Square(80);
    }
  }
  
  int xx = toIntFloor(pos.X() * InvLandGrid);
  int zz = toIntFloor(pos.Z() * InvLandGrid);
  
  // TODO: instead of walking around through the landscape, we might want to traverse the road net
  // this would make sure the position we have found is accessible from the leaders position
  // guaranteeing we are not taking an unrelated path
  // currently we only test y to make sure we are not switching levels (bridge, under the bridge)
  for ( int zi=zz-1; zi<=zz+1; zi++) for ( int xi=xx-1; xi<=xx+1; xi++)
  {
    if (!InRange(xi, zi)) continue;
    const RoadList &roadList = _roads(xi, zi);
    for( int i=0; i<roadList.Size(); i++ )
    {
      const RoadLink *roadLink = roadList[i];
      RoadNode node = roadLink->FindBestNode(soldier, vehWidth, pos, dir);
      if (!node.IsValid()) continue;
      Vector3Val roadPos = node.Position(soldier, vehWidth);
      // we are interested in a "oriented distance"
      // we want to advance in the direction from..pos
      // check how much it moves us in the wanted direction
      
      float fromDist2 = roadPos.Distance2(from);
      // avoid the position we are already in
      if (fromDist2<Square(OperItemGrid)) continue;
      float moved = (roadPos-from).DotProduct(dir);
      // we prefer points which are moving us in desired direction
      // this is needed to make sure we are not taking side roads
      float dist2 = roadPos.Distance2Inline(pos);
      float cosAngle = dir.CosAngle(roadPos-from);
      float minCos = 0.96592582629f; // cos 15 deg
      // in case of bridge on any side , we want to avoid vertical difference 
      if (onRoad && onRoad->IsBridge() || roadLink->IsBridge())
      {
        float vertDiff = fabs(roadPos.Y()-pos.Y());
        // vertical difference indicates a different level, such field is effectively unreachable
        if (vertDiff>6.0f) continue;
      }
      if (moved>minDist && dist2<maxDist2 && cosAngle>minCos)
      {
        maxDist2 = dist2;
        best = roadPos;
        //bestLink = roadLink;
      }
      else if (dist2 < maxDistFallback2)
      {
        maxDistFallback2 = dist2;
        bestFallback = roadPos;
      }
    }
  }
  // if best was found, use it, otherwise use a fallback
  return maxDist2<maxMaxDist2 ? best : bestFallback;
}

void RoadNet::Build( Landscape *land )
{
  Scan(land);
  Connect(false); // all passes
#if 0
  // simulate the effect of SerializeBin
  for (int x=0; x<LandRange; x++) for (int z=0; z<LandRange; z++)
  {
    const RoadList &list=_roads(x,z);
    for (int o=0; o<list.Size(); o++)
    {
      RoadLink *road = list[o];
      for (int c=0; c<road->NConnections(); c++) road->SetConnection(c, NULL);
    }
  }
  Connect(true); // only the 1st pass (connections already found in the previous call of Connect)
#endif
  Optimize();
  Compact();
}

void RoadNet::SerializeBin(SerializeBinStream &f, Landscape *land)
{
  int landrange = land->GetLandRange();
  if (f.IsSaving())
  {
    for( int x=0; x<landrange; x++ ) for( int z=0; z<landrange; z++ )
    {
      // save given roadnet slot
      const RoadList &roadList = _roads(x,z);
      f.SaveInt(roadList.Size());
      // save all items
      for (int i=0; i<roadList.Size(); i++)
      {
        RoadLink *item = roadList[i];
        item->SerializeBin(f,land);
      }
      
    }
  }
  else
  {
    TimeScope time("Load Road");
    _roads.Dim(landrange,landrange);
    _roads.Init(RoadList());
    Assert(f.GetVersion()>=7);
    for( int x=0; x<landrange; x++ )
    {
      for( int z=0; z<landrange; z++ )
      {
        RoadListFull *temp = new RoadListFull();
        RoadList value = RoadList(temp);
        int n = f.LoadInt();
        // save all items
        temp->Realloc(n);
        for (int i=0; i<n; i++)
        {
          RoadLink *item = new RoadLink();
          item->SerializeBin(f,land);
          temp->Add(item);
        }
        temp->Compact();
        if (temp->Size() > 0)
          _roads.Set(x, z, value);
      }
      if (GProgress) GProgress->Refresh();
    }
    Connect(true); // only the 1st pass (connections already found in the previous call of Connect)
    Optimize();
    Compact();
  }
}

//The size of vertex enlargement for inserting into OffTree.
//This epsilon is added and subtracted from vertex coordinates.
float RoadVertex::epsilon = 0.5f;
Offset OffsetChangeBase(Offset a, int b);

struct MergeRoadInfo
{
  // required merge order
  int order;
  // selection index
  int selection;
};
TypeIsSimple(MergeRoadInfo);

static int CompareSelections(const MergeRoadInfo *a, const MergeRoadInfo *b)
{
  if (a->order < b->order) return -1;
  if (a->order > b->order) return +1;
  return 0;
}

void MergeRoads(Shape *roads, const Shape *with, const LODShape *lShape, const Matrix4 &transform, OffTreeRoad *vertexTree, bool quickMerge, bool callCondense)
{
  // assume this and with have the same coordinate space
  // merge points and normals
  // create a permutation
  if (with->NVertex()==0) return; //no merge needed

    UVPair invUV;
  if (roads->NVertex() == 0)
    {
      // copy UV range from with
      roads->InitUV(invUV, with->MinUV(), with->MaxUV());
    }
    else
    {
      // calculate the new UV range
      UVPair minUV = roads->MinUV();
      UVPair maxUV = roads->MaxUV();
      const UVPair &minUVWith = with->MinUV();
      const UVPair &maxUVWith = with->MaxUV();
      saturateMin(minUV.u, minUVWith.u);
      saturateMin(minUV.v, minUVWith.v);
      saturateMax(maxUV.u, maxUVWith.u);
      saturateMax(maxUV.v, maxUVWith.v);

      // adapt the UV to the new range
      roads->ChangeUVRange(invUV, minUV, maxUV);
    }

    roads->ExpandClip();
    enum {maxTable=256};
    AUTO_STATIC_ARRAY(int,pointWithToThis,maxTable);
    pointWithToThis.Resize(with->NPos());

    DoAssert(!roads->ExplicitPoints());
    DoAssert(!with->ExplicitPoints());
    //LogF("Merge from %d",NPos());
    roads->Reserve(with->NPos()+roads->NPos());
    bool setST=false;
    bool setAlpha=false;
    if (quickMerge)
    {
      if (with->IsSTPresent() && !roads->IsSTPresent())
      { //we should introduce ST into this mesh
        roads->ReserveST(roads->NPos()); //roads->_st.Resize(roads->NPos());
        STPair st;
        st.s = Vector3Compressed::ZeroV;
        st.t = Vector3Compressed::ZeroV;
        for (int i=0, siz=roads->NPos(); i<siz; i++) roads->SetAndStretchST(i, st);
        Assert(roads->IsSTPresent());
        setST=true;
      }
      if (with->IsAlphaPresent() && !roads->IsAlphaPresent())
      { //we should introduce alpha into this mesh
        roads->ReserveAlpha(roads->NPos()); //roads->_alpha.Resize(roads->NPos());
        for (int i=0, siz=roads->NPos(); i<siz; i++) roads->SetAndStretchAlpha8(i,1); //roads->_alpha[i]=1.0f;
        Assert(roads->IsAlphaPresent());
        setAlpha=true;
      }
    }
    Vector3 pos,norm;
    for( int i=0; i<with->NPos(); i++ )
    {
      pos.SetFastTransform(transform,with->Pos(i));
      norm.SetRotate(transform, with->Norm(i).Get());
      ClipFlags clip=with->Clip(i);
      int vertex = 0;
      if (quickMerge)
      {
        Vector3 withS = VZero;
        Vector3 withT = VZero;
        if (with->IsSTPresent())
        {
          setST=true;
          const STPair &withST = with->ST(i);
          // we need to rotate the S/T the same way we rotate the normal
          withS.SetRotate(transform ,withST.s.Get());
          withT.SetRotate(transform, withST.t.Get());
        } else if (roads->IsSTPresent()) setST=true;
        int withA = -1;
        if (with->IsAlphaPresent())
        {
          setAlpha=true;
          withA = with->Alpha8(i)*255; //with->_alpha[i]*255;
          saturate(withA, 0,255);
        } 
        else if (roads->IsAlphaPresent() && roads->NPos()>0) 
        {
          setAlpha=true;
          withA=255;
        }
        bool callAddVertex=true;
        //test vertex presence in OffTree
        if (vertexTree!=NULL)
        {
          typedef OffTreeRoad::QueryResult RoadVertexQR;
          RoadVertexQR q; vertexTree->SetStorage(q.regions);
          vertexTree->Query(
            q,pos.X()-RoadVertex::epsilon, pos.Z()-RoadVertex::epsilon,
            pos.X()+RoadVertex::epsilon, pos.Z()+RoadVertex::epsilon
            );
          if (q.regions.Size()>0)
          {
            int ix=-1;  //for index of possible close vertex found
            //AutoArray<RoadVertex> &reg=q.regions;
            OffTreeRoad::Container &reg=q.regions;
            float precTex=RoadPrecTex;
            float minDistance = FLT_MAX;
            for (int qi=0, qsiz=reg.Size(); qi<qsiz; qi++)
            { //test, whether some vertex is close to pos
              VertexIndex vertIx = reg[qi].vertex;
              Vector3Val posI=roads->Pos(vertIx);
              const float precP=RoadPrecP;
              float dist=posI.Distance2(pos);
              if( dist>precP ) continue;
              if (dist<minDistance) {
                ix = vertIx; //this vertex is closer to pos
                minDistance=dist;
              }
              Vector3 normI = roads->Norm(vertIx).Get();
              const float precN = NormEpsilon;
              if( normI.Distance2(norm)>precN ) continue;
              if( roads->Clip(vertIx)!=clip ) continue;
              const UVPair &uv=roads->UV(vertIx); //_tex[vertIx];
              if( fabs(with->U(i)-uv.u)>precTex ) continue;
              if( fabs(with->V(i)-uv.v)>precTex ) continue;
              if (roads->IsAlphaPresent() && withA!=roads->Alpha8(vertIx)) continue;
              if (roads->IsSTPresent())
              {
                const STPair &st=roads->ST(vertIx); //_st[vertIx];
                if (withS.Distance2(st.s.Get()) > precN) continue;
                if (withT.Distance2(st.t.Get()) > precN) continue;
              }
              callAddVertex=false;
              vertex = vertIx; //maybe, there was other vertex closer!
              break;
            }
            if (ix!=-1 && callAddVertex)
            { //there was some close vertex, something should be done
              pos = roads->Pos(ix); //_pos[ix]; //only snap new vertex coordinates to the found one
            }
          }
        }
        if (callAddVertex)
        {
          vertex = roads->AddVertex(pos,norm,clip,invUV,with->U(i),with->V(i),NULL,-1,-1.0f/*no search*/,-1.0f);
          if (setAlpha) roads->SetAndStretchAlpha8(vertex, withA);
          if (setST) {
            STPair st(withS,withT);
            roads->SetAndStretchST(vertex, st);
          }
          if (vertexTree!=NULL) vertexTree->Add(RoadVertex(pos.X(),pos.Z(),vertex));
        }
      }
      else 
        vertex = roads->AddVertex(pos,norm,clip,invUV,with->U(i),with->V(i));
      //LogF("  uv %.2f,%.2f",with->U(i),with->V(i));
      // no vertex information possible on merged shapes
      pointWithToThis[i]=vertex;
    }
    Assert(roads->VerifyFaceStructure());
    Assert(with->VerifyFaceStructure());
    //int offsetDiff = OffsetDiff(roads->EndFaces(), roads->BeginFaces());
    roads->FaceReserveRaw(roads->FacesRawSize()+with->FacesRawSize()); //_face.ReserveRaw(roads->FacesRawSize()+with->FacesRawSize());

    // merge selections, selection name should be ">0", where number determine merge order    
    if (with->NNamedSel())
    {
      AutoArray<MergeRoadInfo, MemAllocLocal<MergeRoadInfo, 16> > sort;
      sort.Resize(with->NNamedSel());

      // selection order
      for (int i = 0; i < with->NNamedSel(); i++)
      {
        const NamedSelection &nSelect = with->NamedSel(i);

        int order = 0;

        // expected order format: ">0"
        if (nSelect.GetName().GetLength() >= 2 && nSelect.GetName().Find('>') == 0)
        {
          const char* name = nSelect.Name();
          order = atoi(&name[1]);
        }
        else
        {
          #if _ENABLE_REPORT
          RptF("Warning: Road selection %s has wrong name format", nSelect.Name());
          #endif
        }

        sort[i].order = order;
        sort[i].selection = i;
      }

      // sort by wanted order
      QSort(sort.Data(), sort.Size(), CompareSelections);

      for (int s = 0; s < sort.Size(); s++)
      {
        const NamedSelection::Access &sel = with->NamedSel(sort[s].selection, lShape);
        // corresponding section
        const ShapeSection *section = NULL;
        const Selection &faces = sel.Faces();

        for (int i = 0; i < faces.Size(); i++)
        {
          int fi = faces[i];
          Offset index = roads->AddFace(with->FaceIndexed(fi));
          Poly &face = roads->Face(index);

          Offset selectionFaceOffset = with->FindFace(fi);

          if (!section)
          {
            // find face in section - selection should be added as section
            for (int j = 0; j < with->NSections(); j++)
            {
              const ShapeSection &sec = with->GetSection(j);

              for (Offset f = sec.beg, e = sec.end; f < e; with->NextFace(f))
              {
                if (f == selectionFaceOffset) { section = &sec; break; }
              }
            }
          }

          for (int v = 0; v < face.N(); v++)
          {
            face.Set(v, pointWithToThis[face.GetVertex(v)]);
          }
        }

        if (section)
          roads->ExtendLastSection(*section);
      }
    }
    else
    {
    // merge sections
    //Offset oldOffset = with->EndFaces();
    for (int s=0; s<with->NSections(); s++)
    {
      const ShapeSection &sec = with->GetSection(s); // _face.GetSection(s);

      for( Offset f=sec.beg,e=sec.end; f<e; with->NextFace(f) )
      {
        Offset index=roads->AddFace(with->Face(f)); // _face.Add(with->Face(f));
        Poly &face=roads->Face(index);
        for( int v=0; v<face.N(); v++ )
        {
          face.Set(v,pointWithToThis[face.GetVertex(v)]);
        }
      }
      //if (with->NNamedSel()==0 && roads->NNamedSel()==0)
      // append section as necessary, ignore any selections
      roads->ExtendLastSection(sec);
    }
    }
    Assert(roads->VerifyFaceStructure());
    if (callCondense)
    {
      roads->CondenseClip();
      roads->CondenseTex();
      roads->CondenseNorm();
    }
    // Set the vertex declaration according to sections
    roads->VertexDeclarationNeedsUpdate();
  // merge textures
  for( int i=0; i<with->NTextures(); i++ )
  {
    roads->AddTexture(with->GetTexture(i));
  }
  roads->OrSpecialNotRecurse(with->Special()); //cannot be used: roads->OrSpecial(with->Special()); 
  DoAssert(roads->NNamedSel()==0);
  DoAssert(!roads->ExplicitPoints());
}

/// compute "intersection" of two 3D line segments
/**
From
  http://www.softsurfer.com/Archive/algorithm_0106/algorithm_0106.htm
See also
  http://local.wasp.uwa.edu.au/~pbourke/geometry/lineline3d/ (Line-Line)
  http://homepage.univie.ac.at/Franz.Vesely/notes/hard_sticks/hst/hst.html (Segment-Segment, Fortran)

*/

// dist3D_Segment_to_Segment():
//    Input:  two 3D line segments S1 and S2
//    Return: the shortest distance between S1 and S2
static float DistSegmentSegment( Vector3Par s1Beg, Vector3Par s1End, Vector3Par s2Beg, Vector3Par s2End)
{
  Vector3   u = s1End - s1Beg;
  Vector3   v = s2End - s2Beg;
  Vector3   w = s1Beg - s2Beg;
  float a = u*u;        // always >= 0
  float b = u*v;
  float c = v*v;        // always >= 0
  float d = u*w;
  float e = v*w;
  float D = a*c - b*b;       // always >= 0
  float sN, sD = D;      // sc = sN / sD, default sD = D >= 0
  float tN, tD = D;      // tc = tN / tD, default tD = D >= 0

  // compute the line parameters of the two closest points
  const float eps = FLT_EPSILON;
  if (D < eps) 
  { // the lines are almost parallel
    sN = 0.0f;        // force using point P0 on segment S1
    sD = 1.0f;        // to prevent possible division by 0.0 later
    tN = e;
    tD = c;
  }
  else
  { // get the closest points on the infinite lines
    sN = (b*e - c*d);
    tN = (a*e - b*d);
    if (sN < 0.0f) 
    { // sc < 0 => the s=0 edge is visible
      sN = 0.0f;
      tN = e;
      tD = c;
    }
    else if (sN > sD) 
    { // sc > 1 => the s=1 edge is visible
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.0f) 
  { // tc < 0 => the t=0 edge is visible
    tN = 0.0f;
    // recompute sc for this edge
    if (-d < 0.0f)
    {
      sN = 0.0f;
    }
    else if (-d > a)
    {
      sN = sD;
    }
    else
    {
      sN = -d;
      sD = a;
    }
  }
  else if (tN > tD) 
  { // tc > 1 => the t=1 edge is visible
    tN = tD;
    // recompute sc for this edge
    if (b-d < 0.0f)
    {
      sN = 0;
    }
    else if (b-d > a)
    {
      sN = sD;
    }
    else
    {
      sN = b-d;
      sD = a;
    }
  }
  // finally do the division to get sc and tc
  float sc = (fabs(sN) < eps ? 0.0f : sN / sD);
  float tc = (fabs(tN) < eps ? 0.0f : tN / tD);

  // get the difference of the two closest points
  Vector3 dP = w + (sc * u) - (tc * v);  // = S1(sc) - S2(tc)

  // return the closest distance
  return dP.Size();
}
	
void RoadLink::TraceSuppression(
  EntityAI *source,  Vector3Par srcPos, Vector3Par srcDir,
  float ammoHit, float distanceLeft, Vector3Par tBeg, Vector3Par tEnd
)
{
  // check if there is any intersection
  
  // check distance of tBeg..tEnd from the bounding sphere of this RoadLink

  // find a nearest point on the line segment to the sphere  
  Vector3 nearest = NearestPoint(tBeg,tEnd,_toWorld.Position());
  
  if (nearest.Distance2(_toWorld.Position())<Square(_lshape->BoundingSphere()))
  {
    // remove the distance travelled before reaching the position
    distanceLeft -= nearest.Distance(tBeg);
    if (distanceLeft>0)
    {
      // the fire line intersects the road space, add ID
      if (!_suppressed) _suppressed = new SupressIdList;
      _suppressed->AddId(source,srcPos,srcDir,ammoHit,distanceLeft);
    }
    
  }
  
}

void RoadLink::Lock( bool soldier, bool waiting )
{
  if (soldier)
  {
    if (waiting) _locksSoldierWaiting++;
    else _locksSoldier++;
  }
  else
  {
    if (waiting) _locksVehicleWaiting++;
    else _locksVehicle++;
  }
}

void RoadLink::Unlock(bool soldier, bool waiting)
{
  if (soldier)
  {
    if (waiting) _locksSoldierWaiting--;
    else _locksSoldier--;
  }
  else
  {
    if (waiting) _locksVehicleWaiting--;
    else _locksVehicle--;
  }
}


#if _HELISIM_AMBIENT_TRAFFIC

void AmbientRoad::GenerateTraffic(float t, AutoArray<AmbientRoad::AmbientCarParams>& resultArray)
{
  if (_gap < AmbientRoadNetwork::minimumGap) return;
  float gap = _gap * GLandscape->GetAmbientTrafficAvgTrajectoriesPerRoad();  // correction for trajectory density and road doubling (TODO: use the true computed density)

  AmbientRoad* prev = 0;
  AmbientRoad* next = 0;
  for (int trajectory = 0, tprev = 0, tnext = 0; trajectory < _phases.Size(); trajectory++)
  {
    if (_prevMask & 1<<(_phases.Size()-trajectory-1)) prev = _prevR[tprev++];
    if (_nextMask & 1<<(_phases.Size()-trajectory-1)) next = _nextR[tnext++];

    float const _positionRandomization = 0.75;
    float phase = t * _speed - _phases[trajectory];
    int code = floor(phase / gap);

    // position on the line in meters: 0..gap
    for (float pos = phase - code*gap; ; pos += gap, code--) {
      float jitteredPos = pos - gap*_positionRandomization*CarHash(code);
      bool sticky = _sticky;

      if (jitteredPos < 0) continue;  // too early
      if (jitteredPos > _len) break;  // segment end

      // find car position and direction
      Vector3 worldPos, roadDir;

      float const maxTurnRadius = 8;  // TODO: based on road width? based on speed?
      float turnRadius = floatMin(_len*0.5, maxTurnRadius);

      // P0 = min(5 meters, edge length / 2) along the first edge
      // P1 = average of edge ends (they're the same if they're on a single Road)
      // P2 = min(5 meters, edge length / 2) along the second edge

      // smooth turning: quadratic Bezier curve
      // B(t) = lerp( lerp(P0,P1,t), lerp(P1,P2,t), t )
      // B'(t) = 2*lerp(P1-P0, P2-P1, t)

      #define LERP(x0,x1,t) ((1-(t))*(x0) + (t)*(x1))

      if (jitteredPos < turnRadius && prev) {
        if (!prev->_sticky) sticky = false;
        float t = 0.5 + jitteredPos * (0.5 / turnRadius);  // 0.5 .. 1
        float turnRadiusPrev = floatMin(prev->_len*0.5, maxTurnRadius);

        Vector3 p0 = prev->_len ? LERP( prev->_points[0], prev->_points[1], 1 - turnRadiusPrev/prev->_len ) : prev->_points[0];
        Vector3 p1 = (_points[0] + prev->_points[1]) * 0.5;
        Vector3 p2 = _len ? LERP( _points[0], _points[1], turnRadius/_len ) : _points[0];
        worldPos = LERP( LERP(p0,p1,t), LERP(p1,p2,t), t );
        roadDir = LERP(p1-p0, p2-p1, t);  // no need to scale, will be normalized
      }
      else if (jitteredPos > _len-turnRadius) {  // these compute the same function, but with shifted indexing
        if (!next->_sticky) sticky = false;
        float t = 0.5 - (_len - jitteredPos) * (0.5 / turnRadius);  // 0 .. 0.5
        float turnRadiusNext = floatMin(next->_len*0.5, maxTurnRadius);

        Vector3 p0 = _len ? LERP( _points[0], _points[1], 1 - turnRadius/_len ) : _points[0];
        Vector3 p1 = (_points[1] + next->_points[0]) * 0.5;
        Vector3 p2 = next->_len ? LERP( next->_points[0], next->_points[1], turnRadiusNext / next->_len ) : next->_points[0];
        worldPos = LERP( LERP(p0,p1,t), LERP(p1,p2,t), t );
        roadDir = LERP(p1-p0, p2-p1, t);  // no need to scale, will be normalized
      }
      else {
        worldPos = LERP( _points[0], _points[1], jitteredPos/_len );
        roadDir = _points[1] - _points[0];
      }
      #undef LERP

      AmbientCarParams par;
      par.position = worldPos;
      par.direction = roadDir;
      par.shapeHash = CarHash2(code);
      par.sticky = sticky;

      resultArray.Add(par);
    }
  }
}


void AmbientRoadNetwork::GenerateTraffic(float t, float x0, float x1, float z0, float z1)
{
  PROFILE_SCOPE_EX(ambTr,*);
  if (_initialized && _on) {

    {
      PROFILE_SCOPE_EX(atGen,*);
      OffTree< Ref<AmbientRoad> >::QueryResult res;
      _tree.Query(res,floatMin(x0,x1),floatMin(z0,z1),floatMax(x0,x1),floatMax(z0,z1));
      for (int i=0; i<res.regions.Size(); i++)
      {
        res.regions[i]->GenerateTraffic(t, _carParams);
      }
    }

    for (int i=0; i<_carTypes.Size(); i++) { _carTypes[i].carPoolLast = 0; _carTypes[i].lightPoolLast = 0; }

    {    
      PROFILE_SCOPE_EX(atDrw,*);
      for (int i=0; i<_carParams.Size(); i++)
      {
        AmbientRoad::AmbientCarParams& par = _carParams[i];
        AmbientCarType& type = _carTypes[int(par.shapeHash * _carTypes.Size())];

        // is there an unused car in the car pool? if not, add it
        if (type.carPoolLast >= type.carPool.Size()) { type.carPool.Add(new AmbientCar(type.shape, VISITOR_NO_ID)); }
        Ref<Object> obj = type.carPool[type.carPoolLast++];

        float y0,dx0=0,dz0=0, y1,dx1=0,dz1=0;

        if (par.sticky) {
          par.direction[1] = 0;
          par.direction.Normalize();
          y0 = GLandscape->SurfaceY(par.position.X() + par.direction.X()*type.landContactZFront, par.position.Z() + par.direction.Z()*type.landContactZFront, &dx0, &dz0);
          y1 = GLandscape->SurfaceY(par.position.X() + par.direction.X()*type.landContactZBack,  par.position.Z() + par.direction.Z()*type.landContactZBack,  &dx1, &dz1);
          par.direction[1] = (y1-y0)/(type.landContactZBack-type.landContactZFront);
          par.position[1] = type.landContactY + (y1+y0)*0.5;
        }
        else {
          par.position[1] += type.landContactY;
        }
        
        obj->SetDirectionAndUp(-par.direction, Vector3(-dx0-dx1,2,-dz0-dz1));  // pretty ad-hoc, but looks nice
        obj->SetPosition(par.position);
        GScene->ObjectForDrawing(obj, -1, ClipAll, true);

/*
        // create lights
        for (int i=0; i<type.lightsFront.Size(); i++) {
          if (type.lightPoolLast >= type.lightPool.Size()) { type.lightPool.Add(new LightPointEntity()); }
          Ref<LightPointEntity> white = type.lightPool[type.lightPoolLast++];
          white->SetAmbient(Color(0.03, 0.03, 0.03));
          white->SetDiffuse(Color(0.3, 0.3, 0.3));
          white->SetBrightness(0.01);
          white->SetPosition(obj->GetFrameBase() * type.lightsFront[i]);
          GScene->ObjectForDrawing(white, -1, ClipAll, true);
        }

        for (int i=0; i<type.lightsBack.Size(); i++) {
          if (type.lightPoolLast >= type.lightPool.Size()) { type.lightPool.Add(new LightPointEntity()); }
          Ref<LightPointEntity> red = type.lightPool[type.lightPoolLast++];
          red->SetAmbient(Color(0.03, 0.003, 0.003));
          red->SetDiffuse(Color(0.3, 0.03, 0.03));
          red->SetBrightness(0.01);
          red->SetPosition(obj->GetFrameBase() * type.lightsBack[i]);
          GScene->ObjectForDrawing(red, -1, ClipAll, true);
        }
*/

      }
    }

    #if 0  
      char s[1024];
      sprintf(s, "Ambient car pool usage:");
      for (int i=0; i<_carTypes.Size(); i++)
        sprintf(s, "%s %d/%d", s, _carTypes[i].carPoolLast, _carTypes[i].carPool.Size());
      LogF(s);
    #endif

    _carParams.Resize(0);  // delete all items, but keep allocated space
  }
}


void AmbientRoadNetwork::SetGap(float gap, float x0, float x1, float z0, float z1)
{
  if (_initialized) {
    OffTree< Ref<AmbientRoad> >::QueryResult res;
    _tree.Query(res,floatMin(x0,x1),floatMin(z0,z1),floatMax(x0,x1),floatMax(z0,z1));
    for (int i=0; i<res.regions.Size(); i++) res.regions[i]->_gap = gap;
    if (gap == 0) {
      LogF("Ambient traffic: disabling traffic on %d roads.", res.regions.Size());
    }
    else {
      LogF("Ambient traffic: changing the average gap of %d roads to %.1f m.", res.regions.Size(), gap);
    }
  }
}


void AmbientRoadNetwork::SetSpeed(float speedKmph, float x0, float x1, float z0, float z1)
{
  if (_initialized) {
    OffTree< Ref<AmbientRoad> >::QueryResult res;
    _tree.Query(res,floatMin(x0,x1),floatMin(z0,z1),floatMax(x0,x1),floatMax(z0,z1));
    for (int i=0; i<res.regions.Size(); i++) res.regions[i]->_speed = speedKmph * 0.277778f;
    LogF("Ambient traffic: changed speed of %d roads to %.2f km/h.", res.regions.Size(), speedKmph);
  }
}


int AmbientRoadNetwork::Load( ParamEntryPar par )
{
  Clear();  // clear old network

  if (!Glob.config.ambientTraffic)
  {
    LogF("Ambient traffic disabled in user cfg");
    return 0;
  }

  // get file name from config
  if (!par.CheckIfEntryExists("AmbientTraffic")) { LogF("Error: class AmbientTraffic missing in current world. Disabling ambient traffic."); return -1; }
  ParamEntryPar parAT = par >> "AmbientTraffic";
  if (!parAT.CheckIfEntryExists("roads")) { LogF("Error: class AmbientTraffic has no \"roads\" entry. Disabling ambient traffic."); return -1; }
  RString name = parAT >> "roads";
  if (name[0] == '\\' || name[0] == '/') name = name.Substring(1, INT_MAX);  // remove leading slash

  // read all the shapes
  if (!parAT.CheckIfEntryExists("shapes")) { LogF("Error: class AmbientTraffic has no \"shapes\" array entry. Disabling ambient traffic."); return -1; }
  ParamEntryPar parShapes = parAT >> "shapes";
  if (!parShapes.IsArray()) { LogF("Error: class AmbientTraffic has no \"shapes\" array entry. Disabling ambient traffic."); return -1; }

  RefArray<AmbientRoad> roads;

  for (int i=0; i<parShapes.GetSize(); i++) {
    if (!parShapes[i].IsTextValue()) { LogF("Error: \"shapes\" in AmbientTraffic must be text values. Disabling ambient traffic."); Clear(); return -1; }

    AmbientCarType type;

    type.shape = new LODShape(parShapes[i].GetValue().Data());

    // find land contact deltas
    int lv = type.shape->FindLandContactLevel();
    if (lv < 0) type.landContactY = 0;
    else {
      type.landContactY = -type.shape->Level(lv)->Min().Y();
      type.landContactZFront = -type.shape->Level(lv)->Min().Z();
      type.landContactZBack = -type.shape->Level(lv)->Max().Z();
    }

    // find light positions
    lv = type.shape->FindMemoryLevel();
    if (lv >= 0) {
      ShapeUsed memShape = type.shape->Level(lv);
      int selIndex;

      selIndex = memShape->FindNamedSel("front_lights");
      if (selIndex >= 0) {
        const NamedSelection::Access sel(memShape->NamedSel(selIndex), type.shape, memShape);
        for (int i=0; i<sel.Size(); i++) {
          if (sel[i] >= 0) type.lightsFront.Add(memShape->Pos(sel[i]));
        }
        type.lightsFront.Compact();
      }
      selIndex = memShape->FindNamedSel("back_lights");
      if (selIndex >= 0) {
        const NamedSelection::Access sel(memShape->NamedSel(selIndex), type.shape, memShape);
        for (int i=0; i<sel.Size(); i++) {
          if (sel[i] >= 0) type.lightsBack.Add(memShape->Pos(sel[i]));
        }
        type.lightsBack.Compact();
      }
    }

    _carTypes.Append(type);
  }

  // read the road definition file
  // Format: version(4), roadCount(4), roadCount*[ width_cm(2), segmentCount(2), segmentCount*[ x(2), z(2) ] ]
  QIFStreamB in;
  in.AutoOpen(name);
  if (in.fail()) { LogF("Disabling ambient traffic (can't open \"%s\").", name.Data()); return -1; }

  long version = in.getil(); if (version != 4) { LogF("Error: Unsupported ambientRoadDefinition version (%d).", version); return -1; }

  const float eps = 1;
  float minXtotal = FLT_MAX, maxXtotal = -FLT_MAX, minZtotal = FLT_MAX, maxZtotal = -FLT_MAX;

  long roadCount = in.getil();
  if (roadCount < 0 || roadCount > 500000) { LogF("Error: ambient road count (%d) didn't pass sanity check.", roadCount); Clear(); return -1; }

  {
    union { unsigned32 i; float f; } u;
    u.i = in.getil();
    _avgTrajectoriesPerRoad = u.f * 2;
  }

  // for all roads
  int totalSegmentCount = 0;
  for (int i = 0; i < roadCount; i++)
  {
    Ref<AmbientRoad> newRoad = new AmbientRoad();

    newRoad->_gap = 60;
    newRoad->_speed = 50 * 0.277778;
    newRoad->_sticky = true;

    // for all segments
    totalSegmentCount--;
    for (int j = 0; j < 2; j++)
    {
      totalSegmentCount++;
      if (in.eof()) { 
        LogF("Error: premature EOF in ambient traffic definition file.");
        Clear();
        return -1;
      }

      // TODO: read the correct transformation from config!
      float x,y,z;

      union { unsigned32 i; float f; } u;
      u.i = in.getil(); x = u.f; 
      u.i = in.getil(); z = 61440 - u.f;
      if (_isnan(x) || _isnan(z)) x = z = 0;  // only on corrupted data: better 0 than NaN

      y = GLandscape->RoadSurfaceY(x,z);
      float simpleY = GLandscape->SurfaceY(x,z);
      if (fabs(y - simpleY) > 1e-3) newRoad->_sticky = false;

      minXtotal = floatMin(x, minXtotal); maxXtotal = floatMax(x, maxXtotal);
      minZtotal = floatMin(z, minZtotal); maxZtotal = floatMax(z, maxZtotal);

      if (in.fail()) { 
        LogF("Error: reading ambient traffic definition failed.");
        Clear();
        return -1;
      }

      newRoad->_points[j] = Vector3(x,y,z);
    }

    Assert(sizeof(long) == sizeof(AmbientRoad*));

    newRoad->_prevMask = in.getiw();
    newRoad->_nextMask = in.getiw();

    int trajectoryCount = 0;
    while (1<<trajectoryCount <= newRoad->_prevMask) trajectoryCount++;  // trajectoryCount = position of top bit in newRoad->_prevMask

    for (int j=0; j<trajectoryCount; j++) {
      union { unsigned32 i; float f; } u;
      u.i = in.getil();
      newRoad->_phases.Append(u.f);
      
      // problem: can't get pointers to non-existing indices
      // dirty hack: read indices into _prevR and _nextR (assume sizeof(long) == sizeof(AmbientRoad*)), change them to pointers after the load
      if (newRoad->_prevMask & 1<<(trajectoryCount-j-1)) newRoad->_prevR.Append(reinterpret_cast<AmbientRoad*>(in.getil()));
      if (newRoad->_nextMask & 1<<(trajectoryCount-j-1)) newRoad->_nextR.Append(reinterpret_cast<AmbientRoad*>(in.getil()));
    }

    newRoad->Seal();
    roads.Append(newRoad);

    if (i && !(i & 0x7FFF))
    {
      LogF("Ambient traffic: read %d roads with %d segments...", i, totalSegmentCount);
    }
  }

  // pass 2 (compact arrays, convert indices in _prevR and _nextR into pointers)
  for (int i = 0; i < roadCount; i++) {
    roads[i]->_phases.Compact();
    roads[i]->_prevR.Compact();
    roads[i]->_nextR.Compact();
    for (int j = 0; j < roads[i]->_prevR.Size(); j++) {
      long prev = reinterpret_cast<long>(roads[i]->_prevR[j]);
      roads[i]->_prevR[j] = (prev==-1 ? 0 : roads[prev].GetRef());
    }
    for (int j = 0; j < roads[i]->_nextR.Size(); j++) {
      long next = reinterpret_cast<long>(roads[i]->_nextR[j]);
      roads[i]->_nextR[j] = roads[next].GetRef();
    }
  }

  LogF("Ambient traffic: read %d roads.", roadCount);
  LogF("X %f .. %f, Z %f .. %f", minXtotal, maxXtotal, minZtotal, maxZtotal);

#ifdef _WIN32
  {
    MEMORYSTATUSEX memstat;
    memstat.dwLength = sizeof(memstat);
    GlobalMemoryStatusEx(&memstat);
    size_t used = GetMemoryUsedSize();
    int oneMB = 1024*1024;
    LogF("Process working set %d MB",used/oneMB);
    LogF("Virtual memory free %d MB",memstat.ullAvailVirtual/oneMB);
    LogF("Physical memory free %d MB",memstat.ullAvailPhys/oneMB);
    LogF("Page file free %d MB",memstat.ullAvailPageFile/oneMB);

    int storageSize = 0;
    for (int i=0; i<roads.Size(); i++) storageSize += roads[i]->StorageSize();
    LogF("Storage size: %d B", storageSize);
  }
  #endif

  // +29 MB
  _tree.Dim(minXtotal-eps, minZtotal-eps, floatMax(maxXtotal-minXtotal, maxZtotal-minZtotal)+2*eps);
  for (int i = 0; i < roads.Size(); i++)
  {
    _tree.Add(roads[i]);
  }

  _initialized = true; 

  return 0;
}

#endif

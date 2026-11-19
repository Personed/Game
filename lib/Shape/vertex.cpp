#include "../wpch.hpp"

#include "../rtAnimation.hpp"

#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "vertex.hpp"
#include "specLods.hpp"
#include <El/Common/perfProf.hpp>

#if !_RELEASE
  // it seems in VS 2005 applies /Oy for this file (Omit frame pointers) - reason unknown
  // in Testing we need stack frames for memory call-stack and crash-dumps
  #pragma optimize("y",off)
#endif

static bool DummyPBHigh = false;

PushBufferPool::~PushBufferPool()
{
}

bool PushBufferPool::Draw(int key, const PBContext &ctx) const
{
#if _ENABLE_CHEATS
  if (GInput.GetCheat3ToDo(DIK_H))
  {
    DummyPBHigh = !DummyPBHigh;
    GlobalShowMessage(500, "DummyPBHigh %s", DummyPBHigh ? "On" : "Off");
  }
#endif

  if (DummyPBHigh) return true;

  // Try to find appropriate item and draw it (and move it at the first place in the list)
  PushBuffer *pb = _pbList.First();
  while (pb)
  {
    if (pb->IsKeyMatching(key))
    {
      // Draw the pushbuffer
      pb->Draw(ctx);

      // Remove item from the current place
      // Put the item at the first place
      _pbList.MakeFirst(pb);

      // Return true - item was drawn
      return true;
    }

    // Move to the next item
    pb = _pbList.Next(pb);
  }

  // Return false - not found - item must be created first
  return false;
}

void PushBufferPool::AssignNewPushBuffer(PushBuffer *pb)
{
#if _ENABLE_REPORT
  // Check if there is some pushbuffer in the pool with the same key and do assert in such case
  PushBuffer *pbCurrent = _pbList.First();
  while (pbCurrent)
  {
    DoAssert(!pbCurrent->IsKeyMatching(pb))
      pbCurrent = _pbList.Next(pbCurrent);
  }
#endif

  // Insert the pushbuffer in the first place
  _pbList.Insert(pb);

  // Keep only certain amount of pushbuffers in the pool
  while (_pbList.Size() > 10)
  {
    _pbList.Delete(_pbList.Last());
  }
}

void PushBufferPool::DiscardAll()
{
  _pbList.Clear();
}

size_t PushBufferPool::GetBufferSize() const
{
  size_t total = 0;

  // Estimate roughly the pushbuffer size
  total = _pbList.Size() * 64;

/*
  PushBuffer *pb = _pbList.First();
  while (pb)
  {
    // Add the size of the single pushbuffer
    total += pb->GetBufferSize();

    // Move to the next item
    pb = _pbList.Next(pb);
  }
*/

  return total;
}

#if USE_PUSHBUFFERS
void VertexBuffer::AssignNewPushBuffer(PushBuffer *pb)
{
  // Assign the new pushbuffer into the pushbuffer pool
  _pbPool.AssignNewPushBuffer(pb);

  // Remind the vertex buffer it's size has been changed
  OnSizeChanged();
}

void VertexBuffer::DiscardPushBuffers()
{
  _pbPool.DiscardAll();

  OnSizeChanged();
}

#endif


Vector3Compressed Vector3Compressed::Up(Vector3(0,1,0));
Vector3Compressed Vector3Compressed::ZeroV(Vector3(0,0,0));

void Vector3Compressed::Set(Vector3Par value)
{
  int x = toInt(511.0f * -value.X()); saturate(x, -511, 511);
  int y = toInt(511.0f * -value.Y()); saturate(y, -511, 511);
  int z = toInt(511.0f * -value.Z()); saturate(z, -511, 511);
  _value = (((DWORD)z & 0x3FF) << 20) | (((DWORD)y & 0x3FF) << 10) | ((DWORD)x & 0x3FF);
}

const Vector3 Vector3Compressed::Get() const
{

  static const DWORD extendSign[] = {0x00000000, 0xFFFFFC00};

  Vector3 value;
  DWORD element = _value & 0x3FF;
  value[0] = (int)(element | extendSign[element >> 9]) * (1.0f / 511.0f);
  element = (_value >> 10) & 0x3FF;
  value[1] = (int)(element | extendSign[element >> 9]) * (1.0f / 511.0f);
  element = (_value >> 20) & 0x3FF;
  value[2] = (int)(element | extendSign[element >> 9]) * (1.0f / 511.0f);
  return -value;
}

// avoid SHRT_MIN..SHRT_MAX  to make sure zero is well represented
//@{ UV compression range
const int UVCompressedMin = -SHRT_MAX;
const int UVCompressedMax = +SHRT_MAX;
const float InvUVCompressedRange = 1.0f / (UVCompressedMax - UVCompressedMin);
//@}

/*
static __forceinline void CopyUV(UVPairCompressed &tgt, const UVPair &src, const UVPair &srcMaxUV, const UVPair &srcMinUV, const UVPair &invUV)
{
  // UV compression equation - opposite to label UVCOMPDECOMP
  Assert(srcMaxUV.u != -1e6);
  Assert(srcMaxUV.v != -1e6);
  Assert(srcMinUV.u != 1e6);
  Assert(srcMinUV.v != 1e6);
  tgt.u = toInt(((src.u - srcMinUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  tgt.v = toInt(((src.v - srcMinUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
}
*/

float UVPairs::U(int i) const
{
  // convert to float <0, 1>
  float u = ((float)_uv[i].u - UVCompressedMin) * InvUVCompressedRange;
  // convert to <_minUV, _maxUV>
  return u * (_maxUV.u - _minUV.u) + _minUV.u;
}

float UVPairs::V(int i) const
{
  // convert to float <0, 1>
  float v = ((float)_uv[i].v - UVCompressedMin) * InvUVCompressedRange;
  // convert to <_minUV, _maxUV>
  return v * (_maxUV.v - _minUV.v) + _minUV.v;
}

UVPair UVPairs::UV(int i) const
{
  // convert to float <0, 1>
  float u = ((float)_uv[i].u - UVCompressedMin) * InvUVCompressedRange;
  float v = ((float)_uv[i].v - UVCompressedMin) * InvUVCompressedRange;
  // convert to <_minUV, _maxUV>
  return UVPair(u * (_maxUV.u - _minUV.u) + _minUV.u, v * (_maxUV.v - _minUV.v) + _minUV.v);
}

void UVPairs::InitUV(UVPair &invUV, const UVPair &minUV, const UVPair &maxUV)
{
  Assert(maxUV.u != -1e6);
  Assert(maxUV.v != -1e6);
  Assert(minUV.u != 1e6);
  Assert(minUV.v != 1e6);

  _minUV = minUV;
  _maxUV = maxUV;
  // If the interval is too small, then clamp it to some reasonable value
  const float minIntervalSize = 1e-6;
  if ((maxUV.u - minUV.u) < minIntervalSize) { _maxUV.u = nextFloat(minUV.u + minIntervalSize); }  // nextFloat makes large max/minUVs different
  if ((maxUV.v - minUV.v) < minIntervalSize) { _maxUV.v = nextFloat(minUV.v + minIntervalSize); }

  invUV = UVPair(1 / (_maxUV.u - _minUV.u), 1 / (_maxUV.v - _minUV.v));
}

void UVPairs::ChangeUVRange(UVPair &invUV, const UVPair &minUV, const UVPair &maxUV)
{
  invUV = UVPair(1 / (maxUV.u - minUV.u), 1 / (maxUV.v - minUV.v));
  if (minUV == _minUV && maxUV == _maxUV) return; // nothing need to be recalculated

  int n = _uv.Size();
  if (n > 0)
  {
    if (_uv.IsCondensed())
    {
      UVPair uv = UV(0);
      UVPairCompressed uvc;
      uvc.u = toInt(((uv.u - minUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
      uvc.v = toInt(((uv.v - minUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
      _uv.SetCondensed(uvc, n);
    }
    else
    {
      for (int i=0; i<n; i++)
      {
        UVPair uv = UV(i);
        _uv.Set(i).u = toInt(((uv.u - minUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
        _uv.Set(i).v = toInt(((uv.v - minUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
      }
    }
  }

  _minUV = minUV;
  _maxUV = maxUV;
}

void UVPairs::SetCondensed(const UVPair &uv, int n)
{
  const float minIntervalSize = 1e-6;
  _minUV = uv;
  _maxUV.u = uv.u + minIntervalSize;
  _maxUV.v = uv.v + minIntervalSize;

  UVPairCompressed val;
  val.u = UVCompressedMin;
  val.v = UVCompressedMin;
  _uv.SetCondensed(val, n);
}

void UVPairs::SetU(int i, const UVPair &invUV, float u)
{
  // UV compression equation - opposite to label UVCOMPDECOMP
  _uv.Set(i).u = toInt(((u - _minUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
}

void UVPairs::SetV(int i, const UVPair &invUV, float v)
{
  // UV compression equation - opposite to label UVCOMPDECOMP
  _uv.Set(i).v = toInt(((v - _minUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
}

void UVPairs::SetUV(int i, const UVPair &invUV, float u, float v)
{
  // UV compression equation - opposite to label UVCOMPDECOMP
  _uv.Set(i).u = toInt(((u - _minUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  _uv.Set(i).v = toInt(((v - _minUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
}

UVPairCompressed UVPairs::CalcUVRaw(const UVPair &invUV, float u, float v)
{
  UVPairCompressed result;
  result.u = toInt(((u - _minUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  result.v = toInt(((v - _minUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  return result;
}

void UVPairs::Add(const UVPair &invUV, float u, float v)
{
  UVPairCompressed uv;
  uv.u = toInt(((u - _minUV.u) * invUV.u) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  uv.v = toInt(((v - _minUV.v) * invUV.v) * (UVCompressedMax - UVCompressedMin) + UVCompressedMin);
  _uv.Add(uv);
}

// we need this to be sure EndianTraits<Vector3Compressed> is correctly defined
#include "../serializeBinExt.hpp"

void operator << (SerializeBinStream &f, UVPairCompressed &uv)
{
  f.TransferBinary(&uv.packed, sizeof(uv.packed));
}

void UVPairs::SerializeBin(SerializeBinStream &f)
{
  f.Transfer(_minUV.u);
  f.Transfer(_minUV.v);
  f.Transfer(_maxUV.u);
  f.Transfer(_maxUV.v);
  f.TransferBinaryCondensedArray(_uv, true);
}

#if _DEBUG
#include <El/Common/randomGen.hpp>

static struct Vector3CompressedUnitTest
{
  Vector3CompressedUnitTest()
  {
    Vector3 zero = VZero;
    Vector3 one(1, 1, 1);
    Vector3 up = VUp;
    Vector3 forward = VForward;
    Vector3 aside = VAside;
    Vector3 negative(-1, -1, -1);
    Vector3 random(GRandGen.PlusMinus(0, 1), GRandGen.PlusMinus(0, 1), GRandGen.PlusMinus(0, 1));

    Assert(zero == Vector3Compressed(zero).Get());
    Assert(one == Vector3Compressed(one).Get());
    Assert(up == Vector3Compressed(up).Get());
    Assert(forward == Vector3Compressed(forward).Get());
    Assert(aside == Vector3Compressed(aside).Get());
    Assert(negative == Vector3Compressed(negative).Get());
    Vector3 random2 = Vector3Compressed(random).Get(); 
    Assert(random2.Distance2(random) < Square(0.01f));
  }
} GVector3CompressedUnitTest;
#endif

VertexTable::VertexTable()
{
   // we know nothing about the table - assume worst case
  _orHints=ClipHints;
  _andHints=0;
  _minMax[0]=VZero;
  _minMax[1]=VZero;
  _bCenter=VZero;
  _bRadius=0;
  _nVertex = 0;
  _vertexBoneRefIsSimple = false;
}

VertexTable::VertexTable( int nPos )
{
  AllocTables(nPos);
  _minMax[0]=VZero;
  _minMax[1]=VZero;
  _bCenter=VZero;
  _bRadius=0;
  if (_data)
  {
    _data->_norm.SetCondensed(Vector3Compressed::ZeroV,0); // avoid uninitialized Vector
  }
  _vertexBoneRefIsSimple = false;
}

VertexTable::VertexTable(const VertexTable &src, bool copyST, bool copySkinning)
{
  DoConstruct(src, copyST, copySkinning);
}
void VertexTable::operator = ( const VertexTable &src )
{
  ReleaseTables();
  DoConstruct(src);
}

VertexTable::~VertexTable()
{
  ReleaseTables();
}

void VertexTable::InitVertexTableData()
{
  if (!_data)
    _data = new VertexTableData;
}

void VertexTable::AllocTables(int nPos)
{
  if (nPos>0)
  {
    InitVertexTableData();
    _data->_pos.Realloc(nPos);
    _data->_pos.Resize(nPos);
    _data->_norm.Realloc(nPos);
    _data->_norm.Resize(nPos);
    _data->_tex.Realloc(nPos);
    _data->_tex.Resize(nPos);
    _data->_clip.Realloc(nPos);
    _data->_clip.Resize(nPos);
  }
  _nVertex = nPos;
  DoAssert(_nVertex < 32768);
}

void VertexTable::Reserve(int nPos)
{
  if (nPos>0)
  {
    InitVertexTableData();
    _data->_pos.Reserve(nPos,nPos);
    _data->_norm.Reserve(nPos,nPos);
    _data->_tex.Reserve(nPos,nPos);
    _data->_clip.Reserve(nPos,nPos);
    if (_data->_st.Size()>0) _data->_st.Reserve(nPos,nPos);
    if (_data->_alpha.Size()>0) _data->_alpha.Reserve(nPos,nPos);
    if (_data->_custom.Size()>0) _data->_custom.Reserve(nPos,nPos);
  }
}

void VertexTable::ReserveCustom(int nPos)
{
  if (_data)
    _data->_custom.Reserve(nPos, nPos);
}

void VertexTable::ReserveAlpha(int nPos)
{
  if (_data)
    _data->_alpha.Reserve(nPos, nPos);
}

void VertexTable::ReserveST(int nPos)
{
  if (_data)
    _data->_st.Reserve(nPos, nPos);
}

void VertexTable::Resize(int nPos)
{
  if (nPos>0)
  {
    InitVertexTableData();
    _data->_pos.Resize(nPos);
    _data->_norm.Resize(nPos);
    _data->_tex.Resize(nPos);
    _data->_clip.Resize(nPos);
  }
  _nVertex = nPos;
  DoAssert(_nVertex < 32768);
}

void VertexTable::Compact()
{
  if (_data)
  {
    _data->_pos.Compact();
    _data->_norm.Compact();
    _data->_clip.Compact();
    _data->_st.Compact();
    _data->_alpha.Compact();
    _data->_custom.Compact();
    _data->_tex.Compact();
    _data->_vertexBoneRef.Compact();
    _data->_neighborBoneRef.Compact();
    Assert(_nVertex==_data->_clip.Size());
    Assert(_nVertex==_data->_pos.Size());
    Assert(_nVertex==_data->_norm.Size());
    Assert(_nVertex==_data->_st.Size() || _data->_st.Size()==0);
    Assert(_nVertex==_data->_tex.Size());
    Assert(_nVertex==_data->_alpha.Size() || _data->_alpha.Size()==0);
    Assert(_nVertex==_data->_custom.Size() || _data->_custom.Size()==0);
    for (int i=1; i<NUVStages(); i++)
    {
      _data->_texCoord[i-1].Compact();
      Assert(_nVertex==_data->_texCoord[i-1].Size() || _data->_texCoord[i-1].Size()==0);
    }
    Assert(_nVertex==_data->_vertexBoneRef.Size() || 0 == _data->_vertexBoneRef.Size());
    Assert(_nVertex==_data->_neighborBoneRef.Size() || 0 == _data->_neighborBoneRef.Size());
  }
}

void VertexTable::RefVertexBufferLocked::Lock()
{
  int now = FreeOnDemandFrameID();
  // prevent memory barrier unless necessary
  if (_frameId-now>=0) return; // compare difference to handle value looping smoothly
  _frameId = now;
  MemorySubscribe();
  // if we have locked a buffer which is not present, it did no harm at all
  // nobody will discard it anyway
}

bool VertexTable::RefVertexBufferLocked::ReleaseIfNotLocked()
{
  int now = FreeOnDemandFrameID();
  //STATIC_ASSERT(sizeof(*this)==sizeof(long long)); // works only on 32b platforms
  long long original = *(long long *)this;
  // lower DWORD: VB pointer
  // upper DWORD: frame id
  int originalFrameId = int(original>>32);
  VertexBuffer *vb = (VertexBuffer *)(int)original;
  if (originalFrameId-now>=0) return false;
  if (vb==NULL) return true;
  // caution: on x64 128b operation is needed
#ifdef _WIN32
  if (_InterlockedCompareExchange64((long long volatile *)this,0,original)==original)
#else
    //enterAtomicInt(); 
    long long volatile *cmpVal = (long long volatile *)this;
    if (*(long long *)this==original) unconst_cast(this) = NULL;
    //leaveAtomicInt();
    if (*cmpVal==original)
#endif
  {
    vb->Release();
    return true;
  }
  // if comparison failed, it must be because somebody has locked the VB (upper DWORD changed)
  // or released it (lower DWORD changed). In either case we no longer want to release it
  return false;
}

void VertexBuffer::Lock(const VertexTable *owner) const
{
  owner->LockVertexBuffer();
}
void VertexBuffer::Unlock(const VertexTable *owner) const
{
}
bool VertexBuffer::ReleaseIfNotLocked(VertexTable *owner)
{
  return owner->VertexBufferReleaseIfNotLocked();
}

void VertexTable::ReleaseTables()
{
  _data.Free();
  _nVertex = 0;
}
void VertexTable::ReallocTable( int nPos )
{
  // no need to release before allocating - we reuse content if possible
  if (nPos>0)
  {
    InitVertexTableData();
    _data->_pos.Realloc(nPos);
    _data->_pos.Resize(nPos);
    _data->_norm.Realloc(nPos);
    _data->_norm.Resize(nPos);
    _data->_tex.Realloc(nPos);
    _data->_tex.Resize(nPos);
    _data->_clip.Realloc(nPos);
    _data->_clip.Resize(nPos);
  }
  _nVertex = nPos;
  DoAssert(_nVertex < 32768);
}

void VertexTable::Init( int nPos )
{
  if (nPos>0)
  {
    InitVertexTableData();
    _data->_pos.Init(nPos),_data->_pos.Resize(nPos);
    _data->_norm.Init(nPos),_data->_norm.Resize(nPos);
    _data->_tex.Init(nPos),_data->_tex.Resize(nPos);
    _data->_clip.Init(nPos),_data->_clip.Resize(nPos);
    _data->_st.Clear();
    _data->_alpha.Clear();
    _data->_custom.Clear();
    for (int i=0; i<lenof(_data->_texCoord); i++)
    {
      _data->_texCoord[i].Clear();
    }
    _data->_vertexBoneRef.Clear();
    _data->_neighborBoneRef.Clear();
  }
  _nVertex = nPos;
  DoAssert(_nVertex < 32768);
}


double VertexTable::GetMemoryUsedUnloadable() const
{
  double sum = 0;

  if (_data)
  {
    sum += _data->GetMemoryAllocated();
  }

  return sum; 
}

double VertexTable::GetMemoryUsed() const
{
  double sum = 0;
  //sum += _buffer.GetMemoryUsed()

  if (_data)
  {
    sum += _data->GetMemoryAllocated();
  }

  return sum; 
}

VertexTableData::VertexTableData()
{
}
VertexTableData::VertexTableData(const VertexTableData &src, bool copyST, bool copySkinning)
{
  _pos = src._pos;
  _norm = src._norm;
  _alpha = src._alpha;
  _custom = src._custom;
  _tex=src._tex;
  _clip=src._clip;
  if (copyST)
  {
    _st = src._st;
    // assume copying ST is done in the same situations
    // as copying secondary tex-coords
    for (int i=0; i<lenof(_texCoord); i++)
    {
      _texCoord[i] = src._texCoord[i];
    }
  }
  if (copySkinning)
  {
    _vertexBoneRef = src._vertexBoneRef;
    _neighborBoneRef = src._neighborBoneRef;
  }
}

void VertexTable::DoConstruct(const VertexTable &src, bool copyST, bool copySkinning)
{
  // make sure geometry is loaded
  Assert(IsLoadLocked());
  
  if (copySkinning)
  {
  }
  if (src._data)
    _data = new VertexTableData(*src._data,copyST,copySkinning);
  _orHints=src._orHints;
  _andHints=src._andHints;
  _nVertex = src._nVertex;
  DoAssert(_nVertex < 32768);

  _minMax[0]=src._minMax[0];
  _minMax[1]=src._minMax[1];
  _bCenter=src._bCenter;
  _bRadius=src._bRadius;
  _vertexBoneRefIsSimple = src._vertexBoneRefIsSimple;
}

int VertexTable::AddVertex(
  Vector3Par pos, Vector3Par norm, ClipFlags clip,
  const UVPair &invUV, float u, float v,
  AutoArray<VertexIndex> *v2p, int pIndex,
  float precP, float precN,
  const UVPair *texCoord, const UVPair *invUV1
)
{
  InitVertexTableData();
  //PROFILE_SCOPE_EX(vtAdd,vb)
  bool doFind = (precP>0 && precN>0);
  precP*=precP;
  precN*=precN;
  // search if given point exists
  Assert( _data->_pos.Size()==_nVertex );
  Assert( _data->_norm.Size()==_nVertex );
  Assert( _data->_tex.Size()==_nVertex );
  Assert( _data->_clip.Size()==_nVertex );
  // first of all try to use slot given by pIndex
  if (v2p)
  {
    if (_nVertex<=pIndex)
    {
      // slot does not exist yet - create it
      int oldSize = _data->_pos.Size();
      _nVertex = pIndex+1;
      DoAssert(_nVertex < 32768);
      _data->_pos.Resize(_nVertex);
      _data->_norm.Resize(_nVertex);
      _data->_tex.Resize(_nVertex);
      _data->_clip.Resize(_nVertex);
      _data->_clip.Expand();
      _data->_norm.Expand();
      _data->_tex.Expand();
      v2p->Resize(_nVertex);
      // mark all newly created slots as unused
      for (int i=oldSize; i<_nVertex; i++)
      {
        v2p->Set(i) = -1;
      }
      _data->_clip.Set(pIndex)=clip;
      _data->_pos[pIndex]=pos;
      _data->_norm.Set(pIndex).Set(norm);
      _data->_tex.SetUV(pIndex, invUV, u, v);

      // Fill out optional values
      if (texCoord)
      {
        Assert(invUV1);
        _data->_texCoord[0].Resize(_nVertex);
        _data->_texCoord[0].Expand();
        _data->_texCoord[0].SetUV(pIndex, *invUV1, texCoord->u, texCoord->v);
      }

      return pIndex;
    }
    if (v2p->Get(pIndex)<0)
    {
      _data->_clip.Expand();
      _data->_norm.Expand();
      _data->_tex.Expand();
      // slot is not used yet - use it, and marked all previous slots as unused
      _data->_clip.Set(pIndex)=clip;
      _data->_pos[pIndex]=pos;
      _data->_norm.Set(pIndex).Set(norm);
      _data->_tex.SetUV(pIndex, invUV, u, v);

      // Fill out optional values
      if (texCoord)
      {
        Assert(invUV1);
        _data->_texCoord[0].Expand();
        _data->_texCoord[0].SetUV(pIndex, *invUV1, texCoord->u, texCoord->v);
      }

      return pIndex;
    }
  }
  int size=_data->_pos.Size();
  //test, whether the vertex already exists, if so, return such (or some close very vertex)
  if (doFind)
  {
    for( int i=0; i<size; i++ )
    {
      // identical objective point index required
      if( v2p && v2p->Get(i)!=pIndex ) continue;
      if( _data->_clip[i]!=clip ) continue;
      Vector3Val posI=_data->_pos[i];
      Vector3 normI = _data->_norm[i].Get();
      if( posI.Distance2(pos)>precP ) continue;
      if( normI.Distance2(norm)>precN ) continue;
      
      UVPairCompressed uvStored = _data->_tex.GetUVRaw(i);
      UVPairCompressed uv = _data->_tex.CalcUVRaw(invUV, u, v);
      if (uv != uvStored) continue;
      if (texCoord)
      {
        DoAssert(_data->_texCoord[0].Size() > i);
        UVPairCompressed uvStored = _data->_texCoord[0].GetUVRaw(i);
        UVPairCompressed uv = _data->_texCoord[0].CalcUVRaw(*invUV1, texCoord->u, texCoord->v);
        if (uv != uvStored) continue;
      }
      return i; //vertex already exists
    }
  }
  _data->_clip.Expand();
  _data->_norm.Expand();
  _data->_tex.Expand();
  int vIndex=_data->_pos.Size();
  _nVertex = vIndex+1;
  DoAssert(_nVertex < 32768);
  _data->_pos.Resize(_nVertex);
  _data->_norm.Resize(_nVertex);
  _data->_tex.Resize(_nVertex);
  _data->_clip.Resize(_nVertex);
  _data->_clip.Set(vIndex)=clip;
  _data->_pos[vIndex]=pos;
  _data->_norm.Set(vIndex).Set(norm);
  _data->_tex.SetUV(vIndex, invUV, u, v);
  if (texCoord)
  {
    Assert(invUV1);
    _data->_texCoord[0].Expand();
    _data->_texCoord[0].Resize(_nVertex);
    _data->_texCoord[0].SetUV(vIndex, *invUV1, texCoord->u, texCoord->v);
  }
  return vIndex;
}

/*!
 \param st not passed by reference, as it is copied _st.Add 
 
 */

#if  _VBS3_MORETERRAINLODS
int VertexTable::AddVertex(
                           Vector3Par pos, Vector3Par norm, ClipFlags clip,
                           const UVPair &invUV, float u, float v,
                           const VertexCustomInfo *custom, STPair st
                           )
#else
int VertexTable::AddVertex(
                           Vector3Par pos, Vector3Par norm, ClipFlags clip,
                           const UVPair &invUV, float u, float v,
                           const Vector3 *custom, STPair st
                           )
#endif
{
  //PROFILE_SCOPE_EX(vtAdC,vb)
  // search if given point exists
  Assert( _data->_pos.Size()==_nVertex );
  Assert( _data->_norm.Size()==_nVertex );
  Assert( _data->_st.Size()==_nVertex );
  Assert( _data->_tex.Size()==_nVertex );
  Assert( _data->_clip.Size()==_nVertex );
  if (custom)
  {
    Assert( _data->_custom.Size()==_nVertex );
  }
  // candidates no longer supported
  _data->_clip.Expand();
  _data->_norm.Expand();
  _data->_tex.Expand();
  int vIndex = _nVertex++;
  DoAssert(_nVertex < 32768);
  _data->_clip.Add(clip);
  _data->_pos.Add(pos);
  _data->_norm.Add(Vector3Compressed(norm));
  _data->_st.Add(st);
  _data->_tex.Add(invUV, u, v);
  if (custom)
  {
    _data->_custom.Add(*custom);
  }
  return vIndex;
}

int VertexTable::AddVertexFast // no duplicate check
(
  Vector3Par pos, Vector3Par norm, ClipFlags clip,
  const UVPair &invUV, float u, float v
)
{
  Assert( _data->_pos.Size()==_nVertex );
  Assert( _data->_norm.Size()==_nVertex );
  Assert( _data->_tex.Size()==_nVertex );
  Assert( _data->_clip.Size()==_nVertex );
  
  int vIndex=_data->_pos.Size();
  _nVertex = vIndex+1;
  DoAssert(_nVertex < 32768);
  _data->_clip.Expand();
  _data->_norm.Expand();
  _data->_tex.Expand();
  _data->_pos.Resize(vIndex+1);
  _data->_norm.Resize(vIndex+1);
  _data->_tex.Resize(vIndex+1);
  _data->_clip.Resize(vIndex+1);
  _data->_clip.Set(vIndex)=clip;
  _data->_pos[vIndex]=pos;
  _data->_norm.Set(vIndex).Set(norm);
  _data->_tex.SetUV(vIndex, invUV, u, v);
  return vIndex;
}

const AnimationRTWeight &VertexTable::Weight(int i) const
{
  return _data->_vertexBoneRef[i];
}

/// calculate and and or values of the array
class OpClipCalculateAndOr
{
  ClipFlags _orHints;
  ClipFlags _andHints;
  
  public:
  OpClipCalculateAndOr()
  {
    _orHints = 0;
    _andHints = ~0;
  }
  void operator () (ClipFlags clip)
  {
    _andHints &= clip;
    _orHints |= clip;
  }
  ClipFlags GetOr() const {return _orHints;}
  ClipFlags GetAnd() const {return _andHints;}
};

void VertexTable::CalculateHints()
{
  OpClipCalculateAndOr andOr;
  _data->_clip.ForEach(andOr);
  _orHints = andOr.GetOr()&ClipHints;
  _andHints = andOr.GetAnd()&ClipHints;
}

/// apply and and or on clipflags
class OpClipAndOr
{
  ClipFlags _and;
  ClipFlags _or;
  
  public:
  OpClipAndOr(ClipFlags and1, ClipFlags or1):_and(and1),_or(or1){}
  
  void operator() (ClipFlags &clip) const
  {
    clip &= _and;
    clip |= _or;
  }
};


/// apply and on clipflags
class OpClipAnd
{
  ClipFlags _and;
  
  public:
  OpClipAnd(ClipFlags and1):_and(and1){}
  
  void operator() (ClipFlags &clip) const
  {
    clip &= _and;
  }
};

/// apply or on clipflags
class OpClipOr
{
  ClipFlags _or;
  
  public:
  OpClipOr(ClipFlags or1):_or(or1){}
  
  void operator() (ClipFlags &clip) const
  {
    clip |= _or;
  }
};

void VertexTable::ClipAndAll(ClipFlags clip)
{
  ForEachClip(OpClipAnd(clip));
}
void VertexTable::ClipOrAll(ClipFlags clip)
{
  ForEachClip(OpClipOr(clip));
}
void VertexTable::ClipAndOrAll(ClipFlags clipAnd, ClipFlags clipOr)
{
  ForEachClip(OpClipAndOr(clipAnd,clipOr));
}

void VertexTable::CalculateMaxAndMinPos(Vector3 &maxPos, Vector3 &minPos, bool dynamic) const
{
  float maxPosX;
  float maxPosY;
  float maxPosZ;
  float minPosX;
  float minPosY;
  float minPosZ;

  if (dynamic)
  {
    const Vector3 *pos = GetPosArray().Data();
    maxPosX = -FLT_MAX;
    maxPosY = -FLT_MAX;
    maxPosZ = -FLT_MAX;
    minPosX = FLT_MAX;
    minPosY = FLT_MAX;
    minPosZ = FLT_MAX;
    Assert(_nVertex > 0);
    for (int i = 0; i < _nVertex; i++)
    {
      if (pos[i].X() > maxPosX) maxPosX = pos[i].X();
      if (pos[i].Y() > maxPosY) maxPosY = pos[i].Y();
      if (pos[i].Z() > maxPosZ) maxPosZ = pos[i].Z();
      if (pos[i].X() < minPosX) minPosX = pos[i].X();
      if (pos[i].Y() < minPosY) minPosY = pos[i].Y();
      if (pos[i].Z() < minPosZ) minPosZ = pos[i].Z();
    }
  }
  else
  {
    minPosX = _minMax[0].X();
    minPosY = _minMax[0].Y();
    minPosZ = _minMax[0].Z();
    maxPosX = _minMax[1].X();
    maxPosY = _minMax[1].Y();
    maxPosZ = _minMax[1].Z();
  }

#ifdef _DEBUG

  if (!dynamic)
  {
    const Vector3 *pos = GetPosArray().Data();

    Assert(_nVertex > 0);
    for (int i = 0; i < _nVertex; i++)
    {
      if (pos[i].X() > maxPosX) maxPosX = pos[i].X();
      if (pos[i].Y() > maxPosY) maxPosY = pos[i].Y();
      if (pos[i].Z() > maxPosZ) maxPosZ = pos[i].Z();
      if (pos[i].X() < minPosX) minPosX = pos[i].X();
      if (pos[i].Y() < minPosY) minPosY = pos[i].Y();
      if (pos[i].Z() < minPosZ) minPosZ = pos[i].Z();
    }

    Assert(minPosX >= _minMax[0].X());
    Assert(minPosY >= _minMax[0].Y());
    Assert(minPosZ >= _minMax[0].Z());
    Assert(maxPosX <= _minMax[1].X());
    Assert(maxPosY <= _minMax[1].Y());
    Assert(maxPosZ <= _minMax[1].Z());
  }

#endif

  // If the interval is too small, then clamp it to some reasonable value
  const float minIntervalSize = 1e-4;
  // note: 1e-6 was not enough, for interval around 50 addition was rounded off
  if ((maxPosX - minPosX) < minIntervalSize) maxPosX = minPosX + minIntervalSize;
  if ((maxPosY - minPosY) < minIntervalSize) maxPosY = minPosY + minIntervalSize;
  if ((maxPosZ - minPosZ) < minIntervalSize) maxPosZ = minPosZ + minIntervalSize;

  maxPos = Vector3(maxPosX, maxPosY, maxPosZ);
  minPos = Vector3(minPosX, minPosY, minPosZ);
}

void VertexTable::ScanBSphere( Vector3 &bCenter, float &bRadius ) const
{
  bCenter=(_minMax[0]+_minMax[1])*0.5;
  float maxDist2=0;
  for( int i=0; i<NPos(); i++ )
  {
    Vector3Val pos=Pos(i);
    saturateMax(maxDist2,pos.Distance2Inline(_bCenter));
  }
  bRadius=sqrt(maxDist2);
}

void VertexTable::ScanMinMax( Vector3 *minMax ) const
{
  if( NPos()<=0 )
  {
    minMax[0]=VZero;
    minMax[1]=VZero;
  }
  else
  {
    minMax[0]=Pos(0);
    minMax[1]=Pos(0);
    for( int i=1; i<NPos(); i++ )
    {
      Vector3Val pos=Pos(i);
      CheckMinMaxInline(minMax[0],minMax[1],pos);
    }
  }
}

void VertexTable::CalculateMinMax()
{
  if (NPos() <= 0)
  {
    _minMax[0]=VZero;
    _minMax[1]=VZero;
    _bCenter=VZero;
    _bRadius=0;
  }
  else
  {
    ScanMinMax(_minMax);
    ScanBSphere(_bCenter,_bRadius);
  }
}

/// copy min max from another Shape
void VertexTable::CopyMinMax(const Shape &src)
{
  _minMax[0] = src._minMax[0];
  _minMax[1] = src._minMax[1];
  _bCenter = src._bCenter;
  _bRadius = src._bRadius;
}

void VertexTable::ReleaseVBuffer(bool deferred) const
{
  if (_buffer.NotNull())
  {
    AssertMainThread();
    _buffer->Detach(*this);
    if (deferred)
      GEngine->DeferVBufferRelease(_buffer);
    _buffer.Free();
  }
}


bool VertexTable::CheckIntegrity() const
{
  if (_data)
  {
    if (_data->_clip.Size()!=_nVertex)
    {
      LogF("_clip.Size()!=_nVertex (%d!=%d)",_data->_clip.Size(),_nVertex);
      return false;
    }
    if (_data->_pos.Size()!=_nVertex)
    {
      LogF("_norm.Size()!=_nVertex (%d!=%d)",_data->_norm.Size(),_nVertex);
      return false;
    }
    if (_data->_norm.Size()!=_nVertex)
    {
      LogF("_norm.Size()!=_nVertex (%d!=%d)",_data->_norm.Size(),_nVertex);
      return false;
    }
    if (_data->_tex.Size()!=_nVertex)
    {
      LogF("_tex.Size()!=_nVertex (%d!=%d)",_data->_tex.Size(),_nVertex);
      return false;
    }
  }
  return true;
}

void VertexTableAnimationContext::Init(const VertexTable *vertexTable)
{
  DoAssert(vertexTable->IsLoadLocked());

  _minMax[0] = vertexTable->_minMax[0];
  _minMax[1] = vertexTable->_minMax[1];
  _bCenter = vertexTable->_bCenter;
  _bRadius = vertexTable->_bRadius;
  _minMaxDirty = false;

  _initialized = true;
}

void VertexTableAnimationContext::LoadVertices(const VertexTable *vertexTable)
{
  if (_loaded) return; // do not load twice
  DoAssert(_initialized); // Init() need to be called (to be sure the animation context want to animate geometry)

  DoAssert(vertexTable->IsLoadLocked());

  // will be animated
  if (vertexTable->_data)
  {
    _pos = vertexTable->_data->GetPosArray();
    vertexTable->_data->GetNormArray().ExpandTo(_norm);
  }

  _loaded = true;

  CHECK_ARRAY_STORAGE(_pos);
  CHECK_ARRAY_STORAGE(_norm);

}

void VertexTableAnimationContext::SetMinMax(Vector3Val min, Vector3Val max, Vector3Val bCenter, float bRadius)
{
  _minMax[0] = min;
  _minMax[1] = max;
  _bCenter = bCenter;
  _bRadius = bRadius;
  _minMaxDirty = false;
}

void VertexTableAnimationContext::ScanBSphere(const VertexTable *vertexTable, Vector3 &bCenter, float &bRadius) const
{
  bCenter = (_minMax[0] + _minMax[1]) * 0.5;
  float maxDist2 = 0;
  for (int i=0; i<vertexTable->NPos(); i++)
  {
    Vector3Val pos = GetPos(vertexTable, i);
    saturateMax(maxDist2, pos.Distance2Inline(_bCenter));
  }
  bRadius = sqrt(maxDist2);
}

void VertexTableAnimationContext::ScanMinMax(const VertexTable *vertexTable, Vector3 *minMax) const
{
  if (vertexTable->NPos() <= 0)
  {
    minMax[0] = VZero;
    minMax[1] = VZero;
  }
  else
  {
    minMax[0] = GetPos(vertexTable, 0);
    minMax[1] = GetPos(vertexTable, 0);
    for (int i=1; i<vertexTable->NPos(); i++)
    {
      Vector3Val pos = GetPos(vertexTable, i);
      CheckMinMaxInline(minMax[0], minMax[1], pos);
    }
  }
}

void VertexTableAnimationContext::CalculateMinMax(const VertexTable *vertexTable)
{
  if (vertexTable->NPos() <= 0)
  {
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    _bCenter = VZero;
    _bRadius = 0;
  }
  else
  {
    ScanMinMax(vertexTable, _minMax);
    ScanBSphere(vertexTable, _bCenter, _bRadius);
  }
  _minMaxDirty = false;
}


TLMaterial::TLMaterial()
:specular(HBlack),
specularPower(0)
{
}

/** there are no default values set for most members */
void TLMaterial::Init()
{
  specular = HBlack;
  specularPower = 0;
  ambient = HWhite;
  diffuse = HWhite;
  emmisive = HBlack;
  forcedDiffuse = Color(0, 0, 0, 0);;
  specFlags = 0;
}

TLMaterial TLMaterial::_default;
TLMaterial TLMaterial::_shining;
TLMaterial TLMaterial::_shiningAdjustable;

/// we need to call a function to init materials - static variable used for that
struct InitMaterials
{
  InitMaterials()
  {
    TLMaterial::_default.Init();
    TLMaterial::_shining.Init();
    TLMaterial::_shiningAdjustable.Init();
    // default can be left as is
    TLMaterial::_shining.ambient = HBlack;
    TLMaterial::_shining.diffuse = HBlack;
    TLMaterial::_shining.emmisive = HWhite;
    // shining adjustable is a combination of emissive and default
    float adjustableEmissive = 0.25f;
    float adjustableReflective = 14.0f;
    TLMaterial::_shiningAdjustable.emmisive = Color(adjustableEmissive,adjustableEmissive,adjustableEmissive);
    TLMaterial::_shiningAdjustable.ambient = Color(adjustableReflective,adjustableReflective,adjustableReflective);
    TLMaterial::_shiningAdjustable.diffuse = Color(adjustableReflective,adjustableReflective,adjustableReflective);
  }
} SInitMaterials;

bool TLMaterial::IsEqual(const TLMaterial &src) const
{
  // compare if material is the same

  if (specFlags!=src.specFlags) return false;

  if (fabs(ambient.R()-src.ambient.R())>0.001) return false;
  if (fabs(ambient.G()-src.ambient.G())>0.001) return false;
  if (fabs(ambient.B()-src.ambient.B())>0.001) return false;
  if (fabs(ambient.A()-src.ambient.A())>0.001) return false;

  if (fabs(diffuse.R()-src.diffuse.R())>0.001) return false;
  if (fabs(diffuse.G()-src.diffuse.G())>0.001) return false;
  if (fabs(diffuse.B()-src.diffuse.B())>0.001) return false;
  if (fabs(diffuse.A()-src.diffuse.A())>0.001) return false;

  if (fabs(forcedDiffuse.R()-src.forcedDiffuse.R())>0.001) return false;
  if (fabs(forcedDiffuse.G()-src.forcedDiffuse.G())>0.001) return false;
  if (fabs(forcedDiffuse.B()-src.forcedDiffuse.B())>0.001) return false;
  if (fabs(forcedDiffuse.A()-src.forcedDiffuse.A())>0.001) return false;

  if (fabs(emmisive.R()-src.emmisive.R())>0.001) return false;
  if (fabs(emmisive.G()-src.emmisive.G())>0.001) return false;
  if (fabs(emmisive.B()-src.emmisive.B())>0.001) return false;
  if (fabs(emmisive.A()-src.emmisive.A())>0.001) return false;

  if (fabs(specular.R()-src.specular.R())>0.001) return false;
  if (fabs(specular.G()-src.specular.G())>0.001) return false;
  if (fabs(specular.B()-src.specular.B())>0.001) return false;
  if (fabs(specular.A()-src.specular.A())>0.001) return false;

  if (specularPower!=src.specularPower) return false;

  return true;
}

void CreateMaterial(TLMaterial &mat, ColorVal col)
{
  mat.emmisive = HBlack;
  mat.ambient = col;
  mat.diffuse = col;
  mat.forcedDiffuse = Color(0, 0, 0, 0);
  mat.specFlags = 0; // fog mode and some more special flags
}

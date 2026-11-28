#include "../wpch.hpp"

#if _ENABLE_DX11

#include "ShaderConstants.h"
#include "../d3dCommon/d3dCommon.hpp"
namespace DX11
{

//===========================================================
// Definition of shader constants
//===========================================================

#define VCS_ENG_DEF_S(CBnum, Type, Name, RegNum) ShaderConstant Name(CBnum, #Name, ShaderConstant::SCTVertex, #Type, RegNum, 0);
#define VCS_ENG_DEF_C(CBnum, Type, Name, RegNum, SubComp) ShaderConstant Name(CBnum, #Name, ShaderConstant::SCTVertex, #Type, RegNum, #SubComp);
#define VCS_ENG_DEF_A(CBnum, Type, Name, RegNum, ArrSize) ShaderConstant Name(CBnum, #Name, ShaderConstant::SCTVertex, #Type, RegNum, 0, ArrSize);
#define VSC_ENG_DEF_CB_BEG(Name, Number, Multisize) ShaderConstantBufferInfo SCBNVS_##Name(ShaderConstant::SCTVertex, Number, #Name, Multisize);
#define VSC_ENG_DEF_CB_END(Name) 

VSC_LIST(VCS_ENG_DEF_S, VCS_ENG_DEF_C, VCS_ENG_DEF_A, VSC_ENG_DEF_CB_BEG, VSC_ENG_DEF_CB_END)

#define PCS_ENG_DEF_S(CBnum, Type, Name, RegNum) ShaderConstant Name(CBnum, #Name, ShaderConstant::SCTPixel, #Type, RegNum, 0);
#define PCS_ENG_DEF_C(CBnum, Type, Name, RegNum, SubComp) ShaderConstant Name(CBnum, #Name, ShaderConstant::SCTPixel, #Type, RegNum, #SubComp);
#define PCS_ENG_DEF_A(CBnum, Type, Name, RegNum, ArrSize) ShaderConstant Name(CBnum, #Name, ShaderConstant::SCTPixel, #Type, RegNum, 0, ArrSize);
#define PSC_ENG_DEF_CB_BEG(Name, Number, Multisize) ShaderConstantBufferInfo SCBNPS_##Name(ShaderConstant::SCTPixel, Number, #Name, Multisize);
#define PSC_ENG_DEF_CB_END(Name) 

PSC_LIST(PCS_ENG_DEF_S, PCS_ENG_DEF_C, PCS_ENG_DEF_A, PSC_ENG_DEF_CB_BEG, PSC_ENG_DEF_CB_END)

//===========================================================
// Implementation of ShaderConstant
//===========================================================
ShaderConstant::ShaderConstant(int cbNumber, const char *name, ShaderConstantType type, const char* hlslType, int regNum, const char* subComp, int arraySize)
:_constantBufferID(cbNumber), _name(name), _type(type)
{
  Assert(cbNumber >= 0 && cbNumber < ShaderConstantBuffer::MaxConstantBufferCount);
  
  int subComponentOffset = GetSubComponentOffset(subComp);

  _offset = regNum * sizeof(float) * 4 + subComponentOffset * sizeof(float);
  Assert(arraySize >= 1);
  if (arraySize < 1)
    arraySize = 1;
  _size = GetHLSLTypeSize(hlslType) * arraySize;

  GetShaderConstantBuffersCreationInfo().RegisterConstant(this, _type, _constantBufferID, _offset, _size);
}

int ShaderConstant::GetHLSLTypeSize(const char* hlslType) const
{
  static const int DefaultTypeSize = sizeof(float);

  Assert(hlslType);
  if (!hlslType)
    return DefaultTypeSize;

  int strSize = strlen(hlslType);
  Assert(strSize > 0);
  if (strSize < 1)  
    return DefaultTypeSize;
 
  int rowSize = 1;
  int colSize = 1;
  int typeSize = DefaultTypeSize;

  static const int tmpSize = 256;
  char tmp[tmpSize + 1];
  Assert(strSize < tmpSize);
  if (strSize >= tmpSize)
    return DefaultTypeSize;

  strncpy(tmp, hlslType, tmpSize);

  char c = tmp[strSize-1];
  if (isdigit(c))
  {
    rowSize = atoi(tmp + strSize-1);
    tmp[strSize-1] = '\0';

    if (strSize > 3 && tmp[strSize-2] == 'x')
    {
      c = tmp[strSize-3];
      if (isdigit(c))
      {
        tmp[strSize-2] = '\0'; //so that atoi can detect end of the string
        colSize = atoi(tmp + strSize-3);

        // end string just after the type
        tmp[strSize-3] = '\0';
      }
    }
  }

  // now tmp should be properly ended after the type
  if (stricmp(tmp, "float") == 0)
    typeSize = DefaultTypeSize;
  else
  {
    // we do not support ints or floats yet
    typeSize = DefaultTypeSize;
    LogF("Unsupported HLSL constant type: \"%s\"", tmp);
  }


  return typeSize * rowSize * colSize;
}

int ShaderConstant::GetSubComponentOffset(const char* subComp) const
{
  if (!subComp)
    return 0;

  int size = strlen(subComp);
  if (0 == size)
    return 0;

  if (size != 1)
  {
    LogF("Invalid subcomponent placement for shader constant : \"%s\"", subComp);
    return 0;
  }

  switch (tolower(subComp[0]))
  {
  case 'x':
    return 0;
  case 'y':
    return 1;
  case 'z':
    return 2;
  case 'w':
    return 3;
  default:
    LogF("Invalid subcomponent placement for shader constant : \"%s\"", subComp);
    return 0;
  }
}

#if _ARMA3_DX11_PROFILING

void ShaderConstant::ProfileNextFrame()
{
  _profileInfoPrev = _profileInfoCur;
  _profileInfoCur.Clear();
}

void ShaderConstant::ProfileReport()
{
  LogF("  -> \"%s\" - %d/%d", cc_cast(_name), _profileInfoPrev._profileNumChanges, _profileInfoPrev._profileNumSets);
}

#endif //#if _ARMA3_DX11_PROFILING

//===========================================================
// Implementation of ShaderConstantBuffer
//===========================================================

ShaderConstantBuffer::ShaderConstantBuffer(int constantBufferId, ShaderConstant::ShaderConstantType type, RString name, bool multisize)
:_constantBufferID(constantBufferId), _dirtyStartOffset(0), _dirtyEndOffset(0), _type(type), _name(name), _multisize(multisize)
{
}

void ShaderConstantBuffer::Init(int constantBufferID, ShaderConstant::ShaderConstantType type, int size, RString name, bool multisize)
{
  _constantBufferID = constantBufferID;
  _dirtyStartOffset = 0;
  _dirtyEndOffset = 0;
  _type = type;
  _data.Resize(size);
  _name = name;
  _multisize = multisize;
  ResetValues();
}

bool ShaderConstantBuffer::SetShaderConstantF(const ShaderConstant& constant, const float *data, int size, int additionalFloat4Offset, bool isCurrent)
{
  if (constant.GetConstantBufferID() != _constantBufferID)
  {
    LogF("Shader constant \"%s\" does not belong to constant buffer %d!", cc_cast(constant.GetName()), _constantBufferID);
    return false;
  }

  int startOffset = constant.GetOffset() + additionalFloat4Offset * sizeof(float)*4;
  int dataSize = sizeof(float) * size;

  if (startOffset < 0 || startOffset + dataSize > _data.Size())
  {
    LogF("Cannot set shader constant \"%s\" - offset or size out of bounds!", cc_cast(constant.GetName()));
    return false;
  }

#if _ARMA3_DX11_PROFILING
  if (isCurrent)
  {
    constant._profileInfoCur._profileNumSets++;
    _profileInfoCur._profileNumMemCmp++;
    _profileInfoCur._profileNumBytesMemCmp += dataSize;
  }
#endif

  Assert(dataSize % 4 == 0);
  if (MemCmp32(_data.Data() + startOffset, data, dataSize / 4) != 0)
  {

#if _ARMA3_DX11_PROFILING
    if (isCurrent)
    {
      constant._profileInfoCur._profileNumChanges++;
      _profileInfoCur._profileNumMemSet++;
      _profileInfoCur._profileNumBytesMemSet += dataSize;
    }
#endif

    memcpy(_data.Data() + startOffset, data, dataSize);
    if (!IsDirty())
    {
      _dirtyStartOffset = startOffset;
      _dirtyEndOffset = startOffset + dataSize;
    }
    else
    {
      if (startOffset < _dirtyStartOffset)
      {
        _dirtyStartOffset = startOffset;
      }
      if (startOffset + dataSize > _dirtyEndOffset)
      {
        _dirtyEndOffset = startOffset + dataSize;
      }
    }
    return true;
  }
  return false;
}

bool ShaderConstantBuffer::GetConstantValue(const ShaderConstant& constant, float *outValue, int numFloats)
{
  if (constant.GetConstantBufferID() != _constantBufferID)
  {
    LogF("Shader constant \"%s\" does not belong to constant buffer %d!", cc_cast(constant.GetName()), _constantBufferID);
    return false;
  }

  int startOffset = constant.GetOffset();
  int dataSize = sizeof(float) * numFloats;

  if (startOffset < 0 || startOffset + dataSize > _data.Size())
  {
    LogF("Cannot get shader constant \"%s\" - offset or size out of bounds!", cc_cast(constant.GetName()));
    return false;
  }

  memcpy(outValue, _data.Data() + startOffset, dataSize);
  return true;
}

void ShaderConstantBuffer::ResetDirty()
{
  _dirtyStartOffset = 0;
  _dirtyEndOffset = 0;
}

void ShaderConstantBuffer::ResetValues()
{
  Assert(_data.Size() % sizeof(float) == 0);
  for (int i=0; i<_data.Size() / sizeof(float); ++i)
  {
    ((float *)_data.Data())[i] = FLT_MAX;    
  }
    
  _dirtyStartOffset = 0;
  _dirtyEndOffset = 0;
}

void ShaderConstantBuffer::SetZeroAndDirty()
{
  Assert(_data.Size() % sizeof(float) == 0);
  for (int i=0; i<_data.Size() / sizeof(float); ++i)
  {
    ((float *)_data.Data())[i] = 0;    
  }
  _dirtyStartOffset = 0;
  _dirtyEndOffset = _data.Size();
}

void ShaderConstantBuffer::CopyInitializedValuesFrom(const ShaderConstantBuffer& source)
{
  Assert(source._data.Size() == _data.Size());
  int count = intMin(_data.Size(), source._data.Size());
  Assert(count % sizeof(float) == 0);
  int countFloat = count / sizeof(float);

  for (int i=0; i< countFloat; ++i)
  {
    float& val = ((float *)_data.Data())[i];
    const float& srcVal = ((float *)source._data.Data())[i];
    if (srcVal != FLT_MAX)
      val = srcVal;
  }

}

/// replaces non-initialized in this buffer by source buffer
void ShaderConstantBuffer::ReplaceNonInitializedValues(const ShaderConstantBuffer& source)
{
  Assert(source._data.Size() == _data.Size());
  int count = intMin(_data.Size(), source._data.Size());
  Assert(count % sizeof(float) == 0);
  int countFloat = count / sizeof(float);

  for (int i=0; i< countFloat; ++i)
  {
    float& val = ((float *)_data.Data())[i];
    const float& srcVal = ((float *)source._data.Data())[i];
    if (val == FLT_MAX)
      val = srcVal;
  }
}

#if _ARMA3_DX11_PROFILING

void ShaderConstantBuffer::ProfileNextFrame()
{
  _profileInfoPrev = _profileInfoCur;
  _profileInfoCur.Clear();

  // get shader constants for this buffer
  const AutoArray<ShaderConstant*>& constants = GetShaderConstantBuffersCreationInfo().GetBufferInfo(_type, _constantBufferID)._constants;
  for (int i = 0; i < constants.Size(); ++i)
  {
    if (constants[i])
      constants[i]->ProfileNextFrame();
  }
}

void ShaderConstantBuffer::ProfileReport()
{
  LogF("*** \"%s\" - %d/%d, %d/%d, %d/%d(%g), %d", 
    cc_cast(_name), 
    _profileInfoPrev._profileNumBytesMemSet,_profileInfoPrev._profileNumBytesMemCmp, 
    _profileInfoPrev._profileNumMemSet,_profileInfoPrev._profileNumMemCmp, 
    _profileInfoPrev._profileNumBytesUpdateNeeded,_profileInfoPrev._profileNumBytesUpdateDone, 
    (_profileInfoPrev._profileNumBytesUpdateDone > 0) ? (100.0f * _profileInfoPrev._profileNumBytesUpdateNeeded/_profileInfoPrev._profileNumBytesUpdateDone) : 0.0f,
    _profileInfoPrev._profileNumUpdatesDone
    ); 

  // get shader constants for this buffer
  const AutoArray<ShaderConstant*>& constants = GetShaderConstantBuffersCreationInfo().GetBufferInfo(_type, _constantBufferID)._constants;
  for (int i = 0; i < constants.Size(); ++i)
  {
    if (constants[i])
      constants[i]->ProfileReport();
  }

}

#endif //#if _ARMA3_DX11_PROFILING

//===========================================================
// Implementation of ShaderConstantBuffer
//===========================================================

ShaderConstantBuffersCreationInfo::ShaderConstantBuffersCreationInfo()
{
}

void ShaderConstantBuffersCreationInfo::RegisterConstant(ShaderConstant* constant,ShaderConstant::ShaderConstantType type, int constantBufferID, int offset, int size)
{
  if (type < 0 || type >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return;
  }
  if (constantBufferID < 0 || constantBufferID >= ShaderConstantBuffer::MaxConstantBufferCount)  
  {
    Assert(!"Invalid constantBufferID");
    return;
  }

  BufferInfo& info = _bufferInfo[type][constantBufferID];
  int& mySize = info._size;
  if (offset + size > mySize)
  {
    mySize = offset + size;
    // we want size to be at 16B boundaries (4xfloat);
    if (mySize % 16 != 0)
      mySize = ((mySize / 16) + 1) * 16 ;
  }

  info._constants.Add(constant);
}

void ShaderConstantBuffersCreationInfo::RegisterBufferInfo(ShaderConstant::ShaderConstantType type, int constantBufferID, RString name, bool multisize)
{
  if (type < 0 || type >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return;
  }
  if (constantBufferID < 0 || constantBufferID >= ShaderConstantBuffer::MaxConstantBufferCount)  
  {
    Assert(!"Invalid constantBufferID");
    return;
  }

  _bufferInfo[type][constantBufferID]._name = name;
  _bufferInfo[type][constantBufferID]._multisize = multisize;
}


const ShaderConstantBuffersCreationInfo::BufferInfo ShaderConstantBuffersCreationInfo::EmptyBufferInfo;

const ShaderConstantBuffersCreationInfo::BufferInfo& ShaderConstantBuffersCreationInfo::GetBufferInfo(ShaderConstant::ShaderConstantType type, int constantBufferID) const
{
  if (type < 0 || type >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return EmptyBufferInfo;
  }
  if (constantBufferID < 0 || constantBufferID >= ShaderConstantBuffer::MaxConstantBufferCount)  
  {
    Assert(!"Invalid constantBufferID");
    return EmptyBufferInfo;
  }

  return _bufferInfo[type][constantBufferID];
}

ShaderConstantBuffersCreationInfo& GetShaderConstantBuffersCreationInfo()
{
  static ShaderConstantBuffersCreationInfo info;
  return info;
}

//===========================================================
// Implementation of ShaderConstantBufferList
//===========================================================

void ShaderConstantBufferList::Create()
{
  ShaderConstantBuffersCreationInfo& info = GetShaderConstantBuffersCreationInfo();
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    // find last non-zero size ID
    int lastID;
    for (lastID = ShaderConstantBuffer::MaxConstantBufferCount-1; lastID >= 0; --lastID)
    {
      if (info.GetBufferInfo((ShaderConstant::ShaderConstantType)shaderType, lastID)._size > 0)
        break;
    }

    Assert(lastID >= 0);

    Assert(_bufferList[shaderType].Size() == 0);
    _bufferList[shaderType].Clear();
    
    for (int cbId = 0; cbId <= lastID; ++cbId)
    {
      int idx = _bufferList[shaderType].Add();
      Assert(idx == cbId);
      ShaderConstantBuffer& buf = _bufferList[shaderType][idx];
      const ShaderConstantBuffersCreationInfo::BufferInfo& bufferInfo = info.GetBufferInfo((ShaderConstant::ShaderConstantType)shaderType, cbId);
      buf.Init(cbId, (ShaderConstant::ShaderConstantType)shaderType, 
        bufferInfo._size,
        bufferInfo._name, 
        bufferInfo._multisize 
        );
    }
  }    
}

void ShaderConstantBufferList::Destroy()
{
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    _bufferList[shaderType].Clear();
  }    
}

bool ShaderConstantBufferList::SetShaderConstantF(const ShaderConstant& constant, const float *data, int size, int additionalFloat4Offset, bool isCurrent)
{
  if (constant.GetType() < 0 || constant.GetType() >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return false;
  }

  if (constant.GetConstantBufferID() < 0 || constant.GetConstantBufferID() >= _bufferList[constant.GetType()].Size())
  {
    Assert(!"Invalid constant buffer id");
    return false;
  }

  return _bufferList[constant.GetType()][constant.GetConstantBufferID()].SetShaderConstantF(constant, data, size, additionalFloat4Offset, isCurrent);
}

void ShaderConstantBufferList::ResetDirty(ShaderConstant::ShaderConstantType type)
{
  if (type < 0 || type >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return;
  }

  for (int i=0; i<_bufferList[type].Size(); ++i)
  {
    _bufferList[type][i].ResetDirty();
  }
}

void ShaderConstantBufferList::ResetDirty()
{
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i<_bufferList[shaderType].Size(); ++i)
    {
      _bufferList[shaderType][i].ResetDirty();
    }
  }
}

void ShaderConstantBufferList::ResetValues()
{
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i<_bufferList[shaderType].Size(); ++i)
    {
      _bufferList[shaderType][i].ResetValues();
    }
  }
}

int ShaderConstantBufferList::GetBufferCount(ShaderConstant::ShaderConstantType type) const
{
  if (type < 0 || type >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return 0;
  }
  
  return _bufferList[type].Size();
}

ShaderConstantBuffer* ShaderConstantBufferList::GetBuffer(ShaderConstant::ShaderConstantType type, int constantBufferId)
{
  if (type < 0 || type >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return 0;
  }
  if (constantBufferId < 0 || constantBufferId >= _bufferList[type].Size())
  {
    Assert(!"Invalid constant buffer id");
    return 0;
  }
  return &_bufferList[type][constantBufferId];
}

void ShaderConstantBufferList::CopyInitializedValuesFrom(ShaderConstantBufferList& source)
{
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i<_bufferList[shaderType].Size(); ++i)
    {
      ShaderConstantBuffer *srcBuffer = source.GetBuffer((ShaderConstant::ShaderConstantType)shaderType, i);
      Assert(srcBuffer);
      if (srcBuffer)
      {
        _bufferList[shaderType][i].CopyInitializedValuesFrom(*srcBuffer);
      }
    }
  }
}

void ShaderConstantBufferList::ReplaceNonInitializedValues(ShaderConstantBufferList& source,ShaderConstant::ShaderConstantType type, int constantBufferId)
{
  ShaderConstantBuffer *myBuffer = GetBuffer(type, constantBufferId);
  Assert(myBuffer);
  if (!myBuffer)
    return;

  ShaderConstantBuffer *srcBuffer = source.GetBuffer(type, constantBufferId);
  Assert(srcBuffer);
  if (!srcBuffer)
    return;

  myBuffer->ReplaceNonInitializedValues(*srcBuffer);
  
}

/** sets values of all constants to 0 and flags all buffers as dirty */
void ShaderConstantBufferList::SetZeroAndDirty()
{
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i<_bufferList[shaderType].Size(); ++i)
    {
      _bufferList[shaderType][i].SetZeroAndDirty();
    }
  }
}

bool ShaderConstantBufferList::GetConstantValue(const ShaderConstant& constant, float *outValue, int numFloats)
{
  if (constant.GetType() < 0 || constant.GetType() >= ShaderConstant::SCTCount)  
  {
    Assert(!"Invalid type");
    return false;
  }

  if (constant.GetConstantBufferID() < 0 || constant.GetConstantBufferID() >= _bufferList[constant.GetType()].Size())
  {
    Assert(!"Invalid constant buffer id");
    return false;
  }

  return _bufferList[constant.GetType()][constant.GetConstantBufferID()].GetConstantValue(constant, outValue, numFloats);
}

#if _ARMA3_DX11_PROFILING

void ShaderConstantBufferList::ProfileNextFrame()
{
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i<_bufferList[shaderType].Size(); ++i)
    {
      _bufferList[shaderType][i].ProfileNextFrame();
    }
  }
}

void ShaderConstantBufferList::ProfileReport()
{
  int totalBytesUpdateNeeded = 0;
  int totalBytesUpdateDone = 0;
  int totalUpdateCount = 0;
  for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
  {
    for (int i=0; i<_bufferList[shaderType].Size(); ++i)
    {
      _bufferList[shaderType][i].ProfileReport();
      totalBytesUpdateNeeded += _bufferList[shaderType][i]._profileInfoPrev._profileNumBytesUpdateNeeded;
      totalBytesUpdateDone += _bufferList[shaderType][i]._profileInfoPrev._profileNumBytesUpdateDone;
      totalUpdateCount += _bufferList[shaderType][i]._profileInfoPrev._profileNumUpdatesDone;
    }
  }
  LogF("============================================");
  LogF("Total: %d/%d, %d", totalBytesUpdateNeeded, totalBytesUpdateDone, totalUpdateCount);
  LogF("============================================");
}

#endif //#if _ARMA3_DX11_PROFILING

} //namespace DX11



#endif //_ENABLE_DX11
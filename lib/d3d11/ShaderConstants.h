#ifdef _MSC_VER
#pragma once
#endif

#ifndef __DX11_SHADER_CONSTANTS_HPP
#define __DX11_SHADER_CONSTANTS_HPP

#if _ENABLE_DX11

#include <D3D11.h>
#include "ShaderSources_4_0/common.h"
#include "ES/Strings/rString.hpp"

namespace DX11
{

//==============================================================
/** represents information about one shader constant */
class ShaderConstant
{
public:
  enum ShaderConstantType
  {
    SCTVertex,
    SCTPixel,
    SCTCount
  };

  /**
  \param cbNumber id of constant buffer (register number, where the cb is placed)
  \param name name of the constant
  \param type type of the constant
  \param hlslType HLSL type of the constant
  \param regNum number of the register, where the constant is placed in the buffer
  \parm subComp subcomponent placement of the constant (can be empty)
  \param arraySize size of the array (1 if no array)
  */
  ShaderConstant(int cbNumber, const char *name, ShaderConstantType type,  const char* hlslType, int regNum, const char* subComp, int arraySize = 1);

  int GetConstantBufferID() const {return _constantBufferID;}
  int GetOffset() const {return _offset;}
  int GetSize() const {return _size;}
  const RString& GetName() const {return _name;}
  ShaderConstantType GetType() const {return _type;}


#if _ARMA3_DX11_PROFILING
  //@{ Profiling counters
  struct ProfileInfo
  {
    ProfileInfo() {Clear();}

    /// how many times per frame was the constant value set (includes also the cases, when value is the same and is not really set)
    int _profileNumSets;
    /// how many times per frame was the constant value set (includes only cases, when the value chanded)
    int _profileNumChanges;

    void Clear()
    {
      _profileNumSets = 0;
      _profileNumChanges = 0;
    }
  };

  /// current profile info
  mutable ProfileInfo _profileInfoCur;
  /// profile info from previous frame (we need to use this, because info from current frame may not be filled correctly in the moment we ask for it (due to multithreaded rendering)
  mutable ProfileInfo _profileInfoPrev;
  /// needs to be called when next frame starts
  void ProfileNextFrame();
  /// reports profiling info to output
  void ProfileReport();

  //@}
#endif

private:
  /// returns size of type in bytes
  int GetHLSLTypeSize(const char* hlslType) const;
  /// returns subcomponent offset in bytes
  int GetSubComponentOffset(const char* subComp) const;

  // id of the constant buffer this constant belongs to
  int _constantBufferID;
  // offset of the constant from the beginning of constant buffer (in bytes)
  int _offset;
  // size of the constant (in bytes)
  int _size;
  // name of the constant (for debugging purposses)
  RString _name;
  // type of the constant 
  ShaderConstantType _type;
};

//==============================================================
/** represents information about one shader constant buffer */
class ShaderConstantBuffer
{
public:
  // maximum number of constant buffers per stage
  static const int MaxConstantBufferCount = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;

  typedef AutoArray<unsigned char> TDataType;

  ShaderConstantBuffer(int constantBufferId = -1, ShaderConstant::ShaderConstantType type = ShaderConstant::SCTCount, RString name = RString(), bool multisize = false);

  /** initializes data buffer 
  \param size size of buffer in bytes */
  void Init(int constantBufferID, ShaderConstant::ShaderConstantType type, int size, RString name, bool multisize);

  const TDataType& GetData() const {return _data;}
  /// return if buffer is dirty and needs to be uploaded to GPU
  bool IsDirty() const {return GetDirtySize() > 0;}
  int GetDirtyStartOffset() const {return _dirtyStartOffset;}
  int GetDirtyEndOffset() const {return _dirtyEndOffset;}
  int GetDirtySize() const {return _dirtyEndOffset - _dirtyStartOffset;}

  /// flags this buffer as non-dirty
  void ResetDirty();

  void ResetValues();

  /** sets values of all constants to 0 and flags all buffers as dirty */
  void SetZeroAndDirty();


  /** sets shader constant 
  \param constant shader constant to set
  \param data data for the constant
  \param size of the data array (number of floats in it)
  \param additionalFloat4Offset - additional offset that should be added to constant offset (!! number of float4 values !! not bytes or floats)
  \param isCurrent - if we are seting constant from current rendering context
  */
  bool SetShaderConstantF(const ShaderConstant& constant, const float *data, int size, int additionalFloat4Offset = 0, bool isCurrent = false);

  /// copies initialized values from source buffer to this buffer
  void CopyInitializedValuesFrom(const ShaderConstantBuffer& source);
  /// replaces non-initialized in this buffer by source buffer
  void ReplaceNonInitializedValues(const ShaderConstantBuffer& source);

  /// gets value of constant (fills outValue with maximum of numFloats float values)
  bool GetConstantValue(const ShaderConstant& constant, float *outValue, int numFloats);

  /// if buffer is multisize, that is we can create multiple versions of this buffer with different sizes and update only part of the buffer
  bool GetMultisize() const {return _multisize;}

#if _ARMA3_DX11_PROFILING
  //@{ Profiling counters

  struct ProfileInfo
  {
    ProfileInfo() {Clear();}

    /// how many bytes of memory we compared during one frame
    int _profileNumBytesMemCmp;
    /// how many times per frame we compared memory
    int _profileNumMemCmp;
    /// how many bytes of memory we set during one frame
    int _profileNumBytesMemSet;
    /// how many times per frame we set memory
    int _profileNumMemSet;

    /// how many bytes of memory were used to update resource on GPU during one frame
    int _profileNumBytesUpdateDone;
    /// how many bytes of memory were actually needed to update resource on GPU during one frame
    int _profileNumBytesUpdateNeeded;
    /// number of updates of this buffer
    int _profileNumUpdatesDone;

    void Clear()
    {
      _profileNumBytesMemCmp= 0;
      _profileNumMemCmp= 0;
      _profileNumBytesMemSet= 0;
      _profileNumMemSet= 0;
      _profileNumBytesUpdateDone = 0;
      _profileNumBytesUpdateNeeded = 0;
      _profileNumUpdatesDone = 0;
    }
  };

  /// current profile info
  mutable ProfileInfo _profileInfoCur;
  /// profile info from previous frame (we need to use this, because info from current frame may not be filled correctly in the moment we ask for it (due to multithreaded rendering)
  mutable ProfileInfo _profileInfoPrev;
  /// needs to be called when next frame starts
  void ProfileNextFrame();
  /// reports profiling info to output
  void ProfileReport();

  //@}
#endif

private:
  /// id of the constant buffer (corresponds to the register placement of the buffer in shader)
  int _constantBufferID;
  /// data of the constants 
  TDataType _data;
  /// start offset of changed data (in bytes)
  int _dirtyStartOffset;
  /// end offset of changed data (in bytes)
  int _dirtyEndOffset;
  /// type of this buffer
  ShaderConstant::ShaderConstantType _type;
  /// name 
  RString _name;
  /// if buffer is multisize, that is we can create multiple versions of this buffer with different sizes and update only part of the buffer
  bool _multisize;
};

//==============================================================
// contains information for constant buffers creation
class ShaderConstantBuffersCreationInfo
{
public:

  /// info about one buffer  
  struct BufferInfo
  {
    BufferInfo(): _size(0), _multisize(false) {}

    /// sizes of  constant buffer
    int _size;
    /// name of buffer
    RString _name;
    /// list of constants belonging to the buffer
    AutoArray<ShaderConstant*> _constants;
    /// if buffer is multisize, that is we can create multiple versions of this buffer with different sizes and update only part of the buffer
    bool _multisize;
  };

  ShaderConstantBuffersCreationInfo();  

  /** offset and size are in bytes */
  void RegisterConstant(ShaderConstant* constant, ShaderConstant::ShaderConstantType type, int constantBufferID, int offset, int size);
  void RegisterBufferInfo(ShaderConstant::ShaderConstantType type, int constantBufferID, RString name, bool multisize);

  /// empty info 
  static const BufferInfo EmptyBufferInfo;

  /// gets buffer info
  const BufferInfo& GetBufferInfo(ShaderConstant::ShaderConstantType type, int constantBufferID) const;
private:
  BufferInfo _bufferInfo[ShaderConstant::SCTCount][ShaderConstantBuffer::MaxConstantBufferCount];
};

extern ShaderConstantBuffersCreationInfo& GetShaderConstantBuffersCreationInfo();

//==============================================================
/// this is just a helper struct to register buffer info
struct ShaderConstantBufferInfo
{
  ShaderConstantBufferInfo(ShaderConstant::ShaderConstantType type, int constantBufferID, RString name, bool multisize)
  {
    GetShaderConstantBuffersCreationInfo().RegisterBufferInfo(type, constantBufferID, name, multisize);
  }
};
//==============================================================
//list of constant buffers
class ShaderConstantBufferList
{
public:
  /// creates lists
  void Create();
  /// destroys lists
  void Destroy();
    
  /** sets shader constant 
  \param constant shader constant to set
  \param data data for the constant
  \param size of the data array (number of floats in it)
  \param additionalFloat4Offset - additional offset that should be added to constant offset (!! number of float4 values !! not bytes or floats)
  \param isCurrent - if we are seting constant from current rendering context
  */
  bool SetShaderConstantF(const ShaderConstant& constant, const float *data, int size, int additionalFloat4Offset = 0, bool isCurrent = false);

  /** resets dirty flags of all buffers of given type*/
  void ResetDirty(ShaderConstant::ShaderConstantType type);
  /** resets dirty flags of all buffers*/
  void ResetDirty();

  /** resets all values of all constants */
  void ResetValues();

  /** sets values of all constants to 0 and flags all buffers as dirty */
  void SetZeroAndDirty();

  /// gets number of buffers of given type
  int GetBufferCount(ShaderConstant::ShaderConstantType type) const;
  /// returns buffer;
  ShaderConstantBuffer* GetBuffer (ShaderConstant::ShaderConstantType type, int constantBufferId);

  /// copies initialized values from source list to this list
  void CopyInitializedValuesFrom(ShaderConstantBufferList& source);
  /// replaces non-initialized in this buffer by source buffer
  void ReplaceNonInitializedValues(ShaderConstantBufferList& source,ShaderConstant::ShaderConstantType type, int constantBufferId);

  /// gets value of constant (fills outValue with maximum of maxFloat float values)
  bool GetConstantValue(const ShaderConstant& constant, float *outValue, int numFloats);

#if _ARMA3_DX11_PROFILING
  //@{ Profiling counters
  /// needs to be called when next frame starts
  void ProfileNextFrame();
  /// reports profiling info to output
  void ProfileReport();
  //@}
#endif

private:
  typedef AutoArray<ShaderConstantBuffer> TBufferList;
  // list of constant buffers for each shader type (vertex, pixel);
  TBufferList _bufferList[ShaderConstant::SCTCount];
};

//! Declaration of constants
#define VCS_ENG_DECL_S(CBnum, Type, Name, RegNum) extern ShaderConstant Name;
#define VCS_ENG_DECL_C(CBnum, Type, Name, RegNum, SubComp) extern ShaderConstant Name;
#define VCS_ENG_DECL_A(CBnum, Type, Name, RegNum, ArrSize) extern ShaderConstant Name;
#define VSC_ENG_DECL_CB_BEG(Name, Number, Multisize) 
#define VSC_ENG_DECL_CB_END(Name) 

VSC_LIST(VCS_ENG_DECL_S, VCS_ENG_DECL_C, VCS_ENG_DECL_A, VSC_ENG_DECL_CB_BEG, VSC_ENG_DECL_CB_END)

PSC_LIST(VCS_ENG_DECL_S, VCS_ENG_DECL_C, VCS_ENG_DECL_A, VSC_ENG_DECL_CB_BEG, VSC_ENG_DECL_CB_END)

} //namespace DX11

TypeIsGeneric(DX11::ShaderConstantBuffer);

#endif //_ENABLE_DX11
#endif //__DX11_SHADER_CONSTANTS_HPP
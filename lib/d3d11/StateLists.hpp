#ifdef _MSC_VER
#pragma once
#endif

#ifndef __DX11_STATE_LISTS_HPP
#define __DX11_STATE_LISTS_HPP

#if _ENABLE_DX11

#include <d3d11.h>

namespace DX11
{

/// =================================================================
/// Class that contains all available depth/stencil state objects
/// =================================================================
class DepthStencilStateList
{
public:
  enum DepthEnable
  {
    DEFalse,
    DETrue,
    DECount
  };
  enum DepthWriteMask
  {
    DWMAll,
    DWMZero,
    DWMCount
  };
  enum DepthFunc
  {
    DFEqual,
    DFLessEqual,
    DFGreaterEqual,
    DFAlways,
    DFCount
  };
  enum StencilEnable
  {
    SEFalse,
    SETrue,
    SECount
  };
  enum StencilReadMask
  {
    SRMZero, //0x0
    SRMFull, //0xff
    SRMCount 
  };
  enum StencilWriteMask
  {
    SWMZero, //0x0
    SWMFull, //0xff
    SWMCount
  };
  enum StencilPassOp
  {
    SPOKeep,
    SPOReplace,
    SPOIncr,
    SPODecr,
    SPOCount
  };
  enum StencilFunc
  {
    SFAlways,
    SFLess,
    SFCount
  };

  struct StateDesc
  {
    StateDesc() {}

    StateDesc(
      DepthEnable depthEnable,
      DepthWriteMask depthWriteMask,
      DepthFunc depthFunc,
      StencilEnable stencilEnable,
      StencilReadMask stencilReadMask,
      StencilWriteMask stencilWriteMask,
      StencilPassOp stencilPassOpFront,
      StencilFunc stencilFuncFront,
      StencilPassOp stencilPassOpBack,
      StencilFunc stencilFuncBack
      ):
        _depthEnable(depthEnable),
        _depthWriteMask(depthWriteMask),
        _depthFunc(depthFunc),
        _stencilEnable(stencilEnable),
        _stencilReadMask(stencilReadMask),
        _stencilWriteMask(stencilWriteMask),
        _stencilPassOpFront(stencilPassOpFront),
        _stencilFuncFront(stencilFuncFront),
        _stencilPassOpBack(stencilPassOpBack),
        _stencilFuncBack(stencilFuncBack)
      {}

    DepthEnable _depthEnable;
    DepthWriteMask _depthWriteMask;
    DepthFunc _depthFunc;
    StencilEnable _stencilEnable;
    StencilReadMask _stencilReadMask;
    StencilWriteMask _stencilWriteMask;
    StencilPassOp _stencilPassOpFront;
    StencilFunc _stencilFuncFront;
    StencilPassOp _stencilPassOpBack;
    StencilFunc _stencilFuncBack;
  };

  /// Creates all state objects
  void CreateStates();
  /// destroys all state objects
  void Clear();
  /// Get state object based on given state
  const ID3D11DepthStencilState* GetState(const StateDesc &desc) const;
  /// Gets state based on given render stated from DX9
  const ID3D11DepthStencilState* GetState(const AutoArray<DWORD>& info) const;

private:
  /// Returns if given state is usable.
  /// Used only for internal state creation - we do not want to create redundant states
  bool IsStateUsable(const StateDesc &desc) const;
  /// Returns if desctiption is valid (contains valid values)
  bool IsDescValid(const StateDesc &desc) const;

  /// Creates state object
  /// Returns if new state was created
  bool CreateState(const StateDesc &desc);


  ComRef<ID3D11DepthStencilState> _states
    [DECount]
    [DWMCount]
    [DFCount]
    [SECount]
    [SRMCount]
    [SWMCount]
    [SPOCount] //StencilPassOp FRONT
    [SFCount] //StencilFunc FRONT
    [SPOCount] //StencilPassOp BACK
    [SFCount]; //StencilFunc BACK

};

/// =================================================================
/// Class that contains all available depth/stencil state objects
/// =================================================================
class BlendStateList
{
public:
  enum AlphaToCoverageEnable
  {
    ATOCFalse,
    ATOCTrue,
    ATOCCount
  };

  enum BlendEnable
  {
    BEFalse,
    BETrue,
    BECount
  };

  enum SrcBlend
  {
    SBZero,
    SBOne,
    SBSrcAlpha,
    SBDestColor,
    SBCount
  };

  enum DestBlend
  {
    DBZero,
    DBOne,
    DBInvSrcAlpha,
    DBSrcColor,
    DBCount
  };

  enum BlendOp
  {
    BOAdd,
    BOCount
  };

  enum RenderTargetWriteMask
  {
    RTWMNone,
    RTWMFullNoAlpha,
    RTWMFull,
    RTWMCount
  };

  struct StateDesc
  {
    StateDesc() {}

    StateDesc(
      AlphaToCoverageEnable alphaToCoverageEnable,
      BlendEnable blendEnable,
      SrcBlend srcBlend,
      DestBlend destBlend,
      BlendOp blendOp,
      BlendEnable alphaBlendEnable,
      SrcBlend alphaSrcBlend,
      DestBlend alphaDestBlend,
      BlendOp alphaBlendOp,
      RenderTargetWriteMask renderTargetWriteMask
      ):
    _alphaToCoverageEnable(alphaToCoverageEnable),
      _blendEnable(blendEnable),
      _srcBlend(srcBlend),
      _destBlend(destBlend),
      _blendOp(blendOp),
      _alphaBlendEnable(alphaBlendEnable),
      _alphaSrcBlend(alphaSrcBlend),
      _alphaDestBlend(alphaDestBlend),
      _alphaBlendOp(alphaBlendOp),
      _renderTargetWriteMask(renderTargetWriteMask)
    {}

    AlphaToCoverageEnable _alphaToCoverageEnable;
    BlendEnable _blendEnable;
    SrcBlend _srcBlend;
    DestBlend _destBlend;
    BlendOp _blendOp;
    BlendEnable _alphaBlendEnable;
    SrcBlend _alphaSrcBlend;
    DestBlend _alphaDestBlend;
    BlendOp _alphaBlendOp;
    RenderTargetWriteMask _renderTargetWriteMask;
  };

  /// Creates all state objects
  void CreateStates();
  /// destroys all state objects
  void Clear();
  /// Get state object based on given state
  const ID3D11BlendState* GetState(const StateDesc &desc) const;
  /// Gets state based on given render stated from DX9
  const ID3D11BlendState* GetState(const AutoArray<DWORD>& info) const;

private:
  /// Returns if given state is usable.
  /// Used only for internal state creation - we do not want to create redundant states
  bool IsStateUsable(const StateDesc &desc) const;
  /// Returns if desctiption is valid (contains valid values)
  bool IsDescValid(const StateDesc &desc) const;

  /// Creates state object
  /// Returns if new state was created
  bool CreateState(const StateDesc &desc);


  ComRef<ID3D11BlendState> _states
    [ATOCCount]
    [BECount]
    [SBCount] 
    [DBCount]
    [BOCount]
    [BECount] // separate alpha blend
    [SBCount] // separate alpha blend
    [DBCount] // separate alpha blend
    [BOCount] // separate alpha blend
    [RTWMCount];

};


/// =================================================================
/// Class that contains all available rasterizer state objects
/// =================================================================
class RasterizerStateList
{
public:
  enum FillMode
  {
    FMWireframe,
    FMSolid,
    FMCount
  };

  enum CullMode
  {
    CMNone,
    CMBack,
    CMCount
  };

  enum FrontCounterClockwise
  {
    FCCWFalse,
    FCCWTrue,
    FCCWCount
  };

  enum DepthBias
  {
    DBNone,
    DBShadow,
    DBCount
  };

  struct StateDesc
  {
    StateDesc() {}

    StateDesc(
      FillMode fillMode,
      CullMode cullMode,
      FrontCounterClockwise frontCounterClockwise,
      DepthBias depthBias
      ):
      _fillMode(fillMode),
      _cullMode(cullMode),
      _frontCounterClockwise(frontCounterClockwise),
      _depthBias(depthBias)
    {}

    FillMode _fillMode;
    CullMode _cullMode;
    FrontCounterClockwise _frontCounterClockwise;
    DepthBias _depthBias;
  };

  /// Creates all state objects
  void CreateStates();
  /// destroys all state objects
  void Clear();
  /// Get state object based on given state
  const ID3D11RasterizerState* GetState(const StateDesc &desc) const;
  /// Gets state based on given render stated from DX9
  const ID3D11RasterizerState* GetState(const AutoArray<DWORD>& info) const;

private:
  /// Returns if given state is usable.
  /// Used only for internal state creation - we do not want to create redundant states
  bool IsStateUsable(const StateDesc &desc) const;
  /// Returns if desctiption is valid (contains valid values)
  bool IsDescValid(const StateDesc &desc) const;

  /// Creates state object
  /// Returns if new state was created
  bool CreateState(const StateDesc &desc);

  ComRef<ID3D11RasterizerState> _states
    [FMCount]
    [CMCount]
    [FCCWCount]
    [DBCount];
};

/// =================================================================
/// Class that contains all available sampler state objects
/// =================================================================

class SamplerStateList
{
public:
  enum Filter
  {
    F_MIN_MAG_MIP_POINT,
    F_MIN_MAG_POINT_MIP_LINEAR,
    F_MIN_POINT_MAG_LINEAR_MIP_POINT,
    F_MIN_POINT_MAG_MIP_LINEAR,
    F_MIN_LINEAR_MAG_MIP_POINT,
    F_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
    F_MIN_MAG_LINEAR_MIP_POINT,
    F_MIN_MAG_MIP_LINEAR,
    F_ANISOTROPIC,
    FCount
  };

  enum AddressMode
  {
    AMWrap,
    AMMirror, 
    AMClamp,
    AMBorder,
    AMCount
  };

  enum MipFiltering
  {
    MFFalse,
    MFTrue,
    MFCount
  };

  enum BorderColor
  {
    BCBlack, //0x00000000
    BCWhite, //0xffffffff
    BCCount
  };

  static const int MaxAnisotropy = 16;

  struct StateDesc
  {
    StateDesc() {}

    StateDesc(
      Filter filter,
      AddressMode addressModeU,
      AddressMode addressModeV,
      AddressMode addressModeW,
      MipFiltering mipFiltering,
      BorderColor borderColor,
      int anisotropy
      ):
    _filter(filter),
    _addressModeU(addressModeU),
    _addressModeV(addressModeV),
    _addressModeW(addressModeW),
    _mipFiltering(mipFiltering),
    _borderColor(borderColor),
    _anisotropy(anisotropy)
    {}

    Filter _filter;
    AddressMode _addressModeU;
    AddressMode _addressModeV;
    AddressMode _addressModeW;
    MipFiltering _mipFiltering;
    BorderColor _borderColor;
    int _anisotropy;
  };

  /// Creates all state objects
  void CreateStates();
  /// destroys all state objects
  void Clear();
  /// Get state object based on given state
  const ID3D11SamplerState* GetState(const StateDesc &desc) const;
  /// Gets state based on given render stated from DX9
  const ID3D11SamplerState* GetState(const AutoArray<DWORD>& info) const;

private:
  /// Returns if given state is usable.
  /// Used only for internal state creation - we do not want to create redundant states
  bool IsStateUsable(const StateDesc &desc) const;
  /// Returns if desctiption is valid (contains valid values)
  bool IsDescValid(const StateDesc &desc) const;

  /// Creates state object
  /// Returns if new state was created
  bool CreateState(const StateDesc &desc);

  ComRef<ID3D11SamplerState> _states
    [FCount] 
  [AMCount] //U
  [AMCount] //V
  [AMCount] //W
  [MFCount]
  [BCCount]
  [MaxAnisotropy+1];
};


} //namespace DX11

#endif //#_ENABLE_DX11
#endif //#__DX11_STATE_LISTS_HPP

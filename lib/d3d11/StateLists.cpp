#include "../wpch.hpp"

#if _ENABLE_DX11

#include "StateLists.hpp"
#include "engdd11.hpp"

namespace DX11
{

 
// maximum count of states that can be loaded to GPU
static const int MaxStateCount = 4096;

///=========================================================
// Implementation of DepthStencilStateList
///=========================================================
void DepthStencilStateList::CreateStates()
{
  LogF("DepthStencilStateList - creating states.");

  int numCreated = 0;

  for (int depthEnable = 0; depthEnable < DECount; ++depthEnable)
  {
    for (int depthWriteMask = 0; depthWriteMask < DWMCount; ++depthWriteMask)
    {
      for (int depthFunc = 0; depthFunc < DFCount; ++depthFunc)
      {
        for (int stencilEnable = 0; stencilEnable < SECount; ++stencilEnable)
        {
          for (int stencilReadMask = 0; stencilReadMask < SRMCount; ++stencilReadMask)
          {
            for (int stencilWriteMask = 0; stencilWriteMask < SWMCount; ++stencilWriteMask)
            {
              for (int stencilPassOpFront = 0; stencilPassOpFront < SPOCount; ++stencilPassOpFront)
              {
                for (int stencilFuncFront = 0; stencilFuncFront < SFCount; ++stencilFuncFront)
                {
                  for (int stencilPassOpBack = 0; stencilPassOpBack < SPOCount; ++stencilPassOpBack)
                  {
                    for (int stencilFuncBack = 0; stencilFuncBack < SFCount; ++stencilFuncBack)
                    {
                      if (CreateState(StateDesc(
                         (DepthEnable)depthEnable,
                         (DepthWriteMask)depthWriteMask,
                         (DepthFunc)depthFunc,
                         (StencilEnable)stencilEnable,
                         (StencilReadMask)stencilReadMask,
                         (StencilWriteMask)stencilWriteMask,
                         (StencilPassOp)stencilPassOpFront,
                         (StencilFunc)stencilFuncFront,
                         (StencilPassOp)stencilPassOpBack,
                         (StencilFunc)stencilFuncBack)))
                      {
                        numCreated++;                                                                                                              
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  LogF("DepthStencilStateList - created %d states.", numCreated);
  if (numCreated > MaxStateCount)
  {
    Assert(false);
    Fail("DepthStencilStateList - number of maximum states exceeded!");
  }
}

void DepthStencilStateList::Clear()
{
  for (int depthEnable = 0; depthEnable < DECount; ++depthEnable)
  {
    for (int depthWriteMask = 0; depthWriteMask < DWMCount; ++depthWriteMask)
    {
      for (int depthFunc = 0; depthFunc < DFCount; ++depthFunc)
      {
        for (int stencilEnable = 0; stencilEnable < SECount; ++stencilEnable)
        {
          for (int stencilReadMask = 0; stencilReadMask < SRMCount; ++stencilReadMask)
          {
            for (int stencilWriteMask = 0; stencilWriteMask < SWMCount; ++stencilWriteMask)
            {
              for (int stencilPassOpFront = 0; stencilPassOpFront < SPOCount; ++stencilPassOpFront)
              {
                for (int stencilFuncFront = 0; stencilFuncFront < SFCount; ++stencilFuncFront)
                {
                  for (int stencilPassOpBack = 0; stencilPassOpBack < SPOCount; ++stencilPassOpBack)
                  {
                    for (int stencilFuncBack = 0; stencilFuncBack < SFCount; ++stencilFuncBack)
                    {
                      _states
                      [depthEnable]
                      [depthWriteMask]
                      [depthFunc]
                      [stencilEnable]
                      [stencilReadMask]
                      [stencilWriteMask]
                      [stencilPassOpFront]
                      [stencilFuncFront]
                      [stencilPassOpBack]
                      [stencilFuncBack].Free();
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

bool DepthStencilStateList::CreateState(const DepthStencilStateList::StateDesc &desc)
{
  if (!IsStateUsable(desc))
    return false;
  
  D3D11_DEPTH_STENCIL_DESC dsDesc;
  
  dsDesc.DepthEnable = (desc._depthEnable == DETrue) ? TRUE : FALSE;

  dsDesc.DepthWriteMask = (desc._depthWriteMask == DWMZero) ? D3D11_DEPTH_WRITE_MASK_ZERO : D3D11_DEPTH_WRITE_MASK_ALL;

  switch (desc._depthFunc)
  {
  case DFEqual:
    dsDesc.DepthFunc = D3D11_COMPARISON_EQUAL;
    break;
  case DFLessEqual:
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    break;
  case DFGreaterEqual:
    dsDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    break;
  case DFAlways:
    dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    break;
  default:
    Fail("Invalid depth func!");      
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    break;
  }

  dsDesc.StencilEnable = (desc._stencilEnable == SETrue) ? TRUE : FALSE;

  switch (desc._stencilReadMask)
  {
  case SRMZero:
    dsDesc.StencilReadMask = 0;
    break;
  case SRMFull:
    dsDesc.StencilReadMask = 0xff;
    break;
  default:
    Fail("Invalid stencil read mask!");      
    dsDesc.StencilReadMask = 0;
    break;
  }

  switch (desc._stencilWriteMask)
  {
  case SWMZero:
    dsDesc.StencilWriteMask = 0;
    break;
  case SWMFull:
    dsDesc.StencilWriteMask = 0xff;
    break;
  default:
    Fail("Invalid stencil write mask!");      
    dsDesc.StencilWriteMask = 0;
    break;
  }

  dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

  switch (desc._stencilPassOpFront)
  {
  case SPOKeep:
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    break;
  case SPOReplace:
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    break;
  case SPOIncr:
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
    break;
  case SPODecr:
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
    break;
  default:
    Fail("Invalid stencil pass op front!");      
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    break;
  }

  switch (desc._stencilFuncFront)
  {
  case SFAlways:
    dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    break;
  case SFLess:
    dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_LESS;
    break;
  default:
    Fail("Invalid stencil func front!");      
    dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    break;
  }

  dsDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  dsDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

  switch (desc._stencilPassOpBack)
  {
  case SPOKeep:
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    break;
  case SPOReplace:
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    break;
  case SPOIncr:
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
    break;
  case SPODecr:
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
    break;
  default:
    Fail("Invalid stencil pass op back!");      
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    break;
  }

  switch (desc._stencilFuncBack)
  {
  case SFAlways:
    dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    break;
  case SFLess:
    dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_LESS;
    break;
  default:
    Fail("Invalid stencil func back!");      
    dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    break;
  }

  // Create state object
  HRESULT hr = GEngineDD11->GetD3DDeviceNonRec().CreateDepthStencilState(&dsDesc, 
    _states
    [desc._depthEnable]
    [desc._depthWriteMask]
    [desc._depthFunc]
    [desc._stencilEnable]
    [desc._stencilReadMask]
    [desc._stencilWriteMask]
    [desc._stencilPassOpFront]
    [desc._stencilFuncFront]
    [desc._stencilPassOpBack]
    [desc._stencilFuncBack].Init()
    );

  if (FAILED(hr))
  {
    GEngineDD11->DDError11("CreateDepthStencilState failed", hr);
    return false;
  }

  return true;
}

const ID3D11DepthStencilState* DepthStencilStateList::GetState(const DepthStencilStateList::StateDesc &desc) const
{
  DoAssert(IsDescValid(desc)); 
  DoAssert(IsStateUsable(desc));
  return _states
    [desc._depthEnable]
    [desc._depthWriteMask]
    [desc._depthFunc]
    [desc._stencilEnable]
    [desc._stencilReadMask]
    [desc._stencilWriteMask]
    [desc._stencilPassOpFront]
    [desc._stencilFuncFront]
    [desc._stencilPassOpBack]
    [desc._stencilFuncBack];
}

bool DepthStencilStateList::IsStateUsable(const DepthStencilStateList::StateDesc &desc) const
{
  DoAssert(IsDescValid(desc)); 

  if (desc._depthEnable == DEFalse && desc._depthFunc != DFAlways)
    return false;

  if (desc._stencilEnable == SEFalse)
  {
    if (desc._stencilReadMask != SRMZero || desc._stencilFuncFront != SFAlways
      || desc._stencilPassOpFront != SPOKeep || desc._stencilFuncBack != SFAlways
      || desc._stencilPassOpBack != SPOKeep)
      return false;
  }
  
  return true;
}

bool DepthStencilStateList::IsDescValid(const DepthStencilStateList::StateDesc &desc) const
{
  return desc._depthEnable >= 0 && desc._depthEnable < DECount &&
    desc._depthWriteMask >= 0 && desc._depthWriteMask < DWMCount &&
    desc._depthFunc >= 0 && desc._depthFunc < DFCount &&
    desc._stencilEnable >= 0 && desc._stencilEnable < SECount &&
    desc._stencilReadMask >= 0 && desc._stencilReadMask < SRMCount &&
    desc._stencilWriteMask >= 0 && desc._stencilWriteMask < SWMCount &&
    desc._stencilPassOpFront >= 0 && desc._stencilPassOpFront < SPOCount &&
    desc._stencilFuncFront >= 0 && desc._stencilFuncFront < SFCount &&
    desc._stencilPassOpBack >= 0 && desc._stencilPassOpBack < SPOCount &&
    desc._stencilFuncBack >= 0 && desc._stencilFuncBack < SFCount;
}

inline static DWORD GetRSValue(const AutoArray<DWORD>& info, int index)
{
  Assert(index >= 0 && index < info.Size());
  return info[index];
}

const ID3D11DepthStencilState* DepthStencilStateList::GetState(const AutoArray<DWORD>& info) const
{
  StateDesc desc;

  // DepthEnable
  if (GetRSValue(info,   D3DRS_ZENABLE_OLD))
  {
    desc._depthEnable = DETrue;
    // DepthFunc
    switch (GetRSValue(info,  D3DRS_ZFUNC_OLD))
    {
    case D3DCMP_EQUAL_OLD:
      desc._depthFunc = DFEqual;
      break;
    case D3DCMP_LESSEQUAL_OLD:
      desc._depthFunc = DFLessEqual;
      break;
    case D3DCMP_GREATEREQUAL_OLD:
      desc._depthFunc = DFGreaterEqual;
      break;
    case D3DCMP_ALWAYS_OLD:
      desc._depthFunc = DFAlways;
      break;
    default:
      desc._depthFunc = DFAlways;
      ErrF("DX11 state fallback: depth func=%u", (unsigned)GetRSValue(info, D3DRS_ZFUNC_OLD));
    }
  }
  else
  {
    desc._depthEnable = DepthStencilStateList::DEFalse;
    desc._depthFunc = DepthStencilStateList::DFAlways;
  }


  // DepthWriteMask
  if (GetRSValue(info,  D3DRS_ZWRITEENABLE_OLD))
  {
    desc._depthWriteMask = DWMAll;
  }
  else
  {
    desc._depthWriteMask = DWMZero;
  }


  // StencilEnable
  if (GetRSValue(info,  D3DRS_STENCILENABLE_OLD))
  {
    desc._stencilEnable = SETrue;

    // StencilReadMask
    switch (GetRSValue(info,  D3DRS_STENCILMASK_OLD))
    {
    case 0:
      desc._stencilReadMask = SRMZero;
      break;
    case 0xff:
      desc._stencilReadMask = SRMFull;
      break;
    default:
      desc._stencilReadMask = SRMZero;
      ErrF("DX11 state fallback: stencil read mask=%u", (unsigned)GetRSValue(info, D3DRS_STENCILMASK_OLD));
      break;
    }

    // FrontFace.StencilPassOp
    switch (GetRSValue(info,  D3DRS_STENCILPASS_OLD))
    {
    case D3DSTENCILOP_KEEP_OLD:
      desc._stencilPassOpFront = SPOKeep;
      break;
    case D3DSTENCILOP_REPLACE_OLD:
      desc._stencilPassOpFront = SPOReplace;
      break;
    case D3DSTENCILOP_INCR_OLD:
      desc._stencilPassOpFront = SPOIncr;
      break;
    case D3DSTENCILOP_DECR_OLD:
      desc._stencilPassOpFront = SPODecr;
      break;
    default:
      desc._stencilPassOpFront = SPOKeep;
      ErrF("DX11 state fallback: front stencil pass op=%u", (unsigned)GetRSValue(info, D3DRS_STENCILPASS_OLD));
    }

    // FrontFace.StencilFunc
    switch (GetRSValue(info,  D3DRS_STENCILFUNC_OLD))
    {
    case D3DCMP_LESS_OLD:
      desc._stencilFuncFront = SFLess;
      break;
    case D3DCMP_ALWAYS_OLD:
      desc._stencilFuncFront = SFAlways;
      break;
    default:
      desc._stencilFuncFront = SFAlways;
      ErrF("DX11 state fallback: front stencil func=%u", (unsigned)GetRSValue(info, D3DRS_STENCILFUNC_OLD));
    }

    if (GetRSValue(info,  D3DRS_TWOSIDEDSTENCILMODE_OLD))
    {
      // BackFace.StencilPassOp
      switch (GetRSValue(info,  D3DRS_CCW_STENCILPASS_OLD))
      {
      case D3DSTENCILOP_KEEP_OLD:
        desc._stencilPassOpBack = SPOKeep;
        break;
      case D3DSTENCILOP_REPLACE_OLD:
        desc._stencilPassOpBack = SPOReplace;
        break;
      case D3DSTENCILOP_INCR_OLD:
        desc._stencilPassOpBack = SPOIncr;
        break;
      case D3DSTENCILOP_DECR_OLD:
        desc._stencilPassOpBack = SPODecr;
        break;
      default:
        desc._stencilPassOpBack = SPOKeep;
          ErrF("DX11 state fallback: back stencil pass op=%u", (unsigned)GetRSValue(info, D3DRS_CCW_STENCILPASS_OLD));
      }

      // BackFace.StencilFunc
      switch (GetRSValue(info,  D3DRS_CCW_STENCILFUNC_OLD))
      {
      case D3DCMP_LESS_OLD:
        desc._stencilFuncBack = SFLess;
        break;
      case D3DCMP_ALWAYS_OLD:
        desc._stencilFuncBack = SFAlways;
        break;
      default:
        desc._stencilFuncBack = SFAlways;
          ErrF("DX11 state fallback: back stencil func=%u", (unsigned)GetRSValue(info, D3DRS_CCW_STENCILFUNC_OLD));
      }
    }
    else
    {
      desc._stencilPassOpBack = SPOKeep;
      desc._stencilFuncBack = SFAlways;
    }

  }
  else
  {
    desc._stencilEnable = SEFalse;
    desc._stencilReadMask = SRMZero;
    desc._stencilFuncFront = SFAlways;
    desc._stencilPassOpFront = SPOKeep;
    desc._stencilFuncBack = SFAlways;
    desc._stencilPassOpBack = SPOKeep;
  }

  // StencilWriteMask
  switch (GetRSValue(info,  D3DRS_STENCILWRITEMASK_OLD))
  {
  case 0:
    desc._stencilWriteMask = SWMZero;
    break;
  case 0xff:
    desc._stencilWriteMask = SWMFull;
    break;
  default:
    desc._stencilWriteMask = SWMZero;
    ErrF("DX11 state fallback: stencil write mask=%u", (unsigned)GetRSValue(info, D3DRS_STENCILWRITEMASK_OLD));
    break;
  }

  const ID3D11DepthStencilState* state = GetState(desc);
  if (!state)
  {
    DoAssert(state);
    Fail("DX11 Invalid depth/stencil state - cannot get state from description!");
  }
  return state;
}

///=========================================================
// Implementation of BlendStateList
///=========================================================
void BlendStateList::CreateStates()
{
  LogF("BlendStateList - creating states.");

  int numCreated = 0;

  for (int alphaToCoverageEnable = 0; alphaToCoverageEnable < ATOCCount; ++alphaToCoverageEnable)
  {
    for (int blendEnable = 0; blendEnable < BECount; ++blendEnable)
    {
      for (int srcBlend = 0; srcBlend < SBCount; ++srcBlend)
      {
        for (int destBlend = 0; destBlend < DBCount; ++destBlend)
        {
          for (int blendOp = 0; blendOp < BOCount; ++blendOp)
          {
            for (int alphaBlendEnable = 0; alphaBlendEnable < BECount; ++alphaBlendEnable)
            {
              for (int alphaSrcBlend = 0; alphaSrcBlend < SBCount; ++alphaSrcBlend)
              {
                for (int alphaDestBlend = 0; alphaDestBlend < DBCount; ++alphaDestBlend)
                {
                  for (int alphaBlendOp = 0; alphaBlendOp < BOCount; ++alphaBlendOp)
                  {

                    for (int renderTargetWriteMask = 0; renderTargetWriteMask < RTWMCount; ++renderTargetWriteMask)
                    {
                      if (CreateState(StateDesc(
                        (AlphaToCoverageEnable)alphaToCoverageEnable,
                        (BlendEnable)blendEnable,
                        (SrcBlend)srcBlend,
                        (DestBlend)destBlend,
                        (BlendOp)blendOp,
                        (BlendEnable)alphaBlendEnable,
                        (SrcBlend)alphaSrcBlend,
                        (DestBlend)alphaDestBlend,
                        (BlendOp)alphaBlendOp,
                        (RenderTargetWriteMask)renderTargetWriteMask
                        )))
                      {
                        numCreated++;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  LogF("BlendStateList - created %d states.", numCreated);
  if (numCreated > MaxStateCount)
  {
    Assert(false);
    Fail("BlendStateList - number of maximum states exceeded!");
  }

}

void BlendStateList::Clear()
{
  for (int alphaToCoverageEnable = 0; alphaToCoverageEnable < ATOCCount; ++alphaToCoverageEnable)
  {
    for (int blendEnable = 0; blendEnable < BECount; ++blendEnable)
    {
      for (int srcBlend = 0; srcBlend < SBCount; ++srcBlend)
      {
        for (int destBlend = 0; destBlend < DBCount; ++destBlend)
        {
          for (int blendOp = 0; blendOp < BOCount; ++blendOp)
          {
            for (int alphaBlendEnable = 0; alphaBlendEnable < BECount; ++alphaBlendEnable)
            {
              for (int alphaSrcBlend = 0; alphaSrcBlend < SBCount; ++alphaSrcBlend)
              {
                for (int alphaDestBlend = 0; alphaDestBlend < DBCount; ++alphaDestBlend)
                {
                  for (int alphaBlendOp = 0; alphaBlendOp < BOCount; ++alphaBlendOp)
                  {

                    for (int renderTargetWriteMask = 0; renderTargetWriteMask < RTWMCount; ++renderTargetWriteMask)
                    {
                      _states
                        [alphaToCoverageEnable]
                      [blendEnable]
                      [srcBlend]
                      [destBlend]
                      [blendOp]
                      [alphaBlendEnable]
                      [alphaSrcBlend]
                      [alphaDestBlend]
                      [alphaBlendOp]
                      [renderTargetWriteMask].Free();
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

const ID3D11BlendState* BlendStateList::GetState(const BlendStateList::StateDesc &desc) const
{
  DoAssert(IsDescValid(desc));
  DoAssert(IsStateUsable(desc));

  return _states
    [desc._alphaToCoverageEnable]
    [desc._blendEnable]
    [desc._srcBlend]
    [desc._destBlend]
    [desc._blendOp]
    [desc._alphaBlendEnable]
    [desc._alphaSrcBlend]
    [desc._alphaDestBlend]
    [desc._alphaBlendOp]
    [desc._renderTargetWriteMask];
}

const ID3D11BlendState* BlendStateList::GetState(const AutoArray<DWORD>& info) const
{
  StateDesc desc;

  // we use D3DRS_POINTSIZE_OLD to signal alpha to coverage usage
  int alphaToCoverageFlag = GetRSValue(info,   D3DRS_POINTSIZE_OLD);
  if (1 == alphaToCoverageFlag)
    desc._alphaToCoverageEnable =  ATOCTrue;
  else
    desc._alphaToCoverageEnable =  ATOCFalse;

  // blend enable
  if (GetRSValue(info, D3DRS_ALPHABLENDENABLE_OLD))
  {
    desc._blendEnable = BETrue;

    // SrcBlend
    switch (GetRSValue(info,  D3DRS_SRCBLEND_OLD))
    {
    case D3DBLEND_ZERO_OLD:
      desc._srcBlend = SBZero;
      break;
    case D3DBLEND_ONE_OLD:
      desc._srcBlend = SBOne;
      break;
    case D3DBLEND_SRCALPHA_OLD:
      desc._srcBlend = SBSrcAlpha;
      break;
    case D3DBLEND_DESTCOLOR_OLD:
      desc._srcBlend = SBDestColor;
      break;
    default:
      ErrF("DX11 state fallback: src blend=%u", (unsigned)GetRSValue(info, D3DRS_SRCBLEND_OLD));
      desc._srcBlend = SBOne;
      break;
    }

    // DestBlend
    switch (GetRSValue(info,  D3DRS_DESTBLEND_OLD))
    {
    case D3DBLEND_ZERO_OLD:
      desc._destBlend = DBZero;
      break;
    case D3DBLEND_ONE_OLD:
      desc._destBlend = DBOne;
      break;
    case D3DBLEND_INVSRCALPHA_OLD:
      desc._destBlend = DBInvSrcAlpha;
      break;
    case D3DBLEND_SRCCOLOR_OLD:
      desc._destBlend = DBSrcColor;
      break;
    default:
      ErrF("DX11 state fallback: dest blend=%u", (unsigned)GetRSValue(info, D3DRS_DESTBLEND_OLD));
      desc._destBlend = DBInvSrcAlpha;
      break;
    }

    // BlendOp
    switch (GetRSValue(info,  D3DRS_BLENDOP_OLD))
    {
    case D3DBLENDOP_ADD_OLD:
      desc._blendOp = BOAdd;
      break;
    default:
      ErrF("DX11 state fallback: blend op=%u", (unsigned)GetRSValue(info, D3DRS_BLENDOP_OLD));
      desc._blendOp = BOAdd;
      break;
    }

    // separate alpha BlendEnable
    if (GetRSValue(info,   D3DRS_SEPARATEALPHABLENDENABLE_OLD))
    {
      desc._alphaBlendEnable = BETrue;

      // SrcBlend
      switch (GetRSValue(info,  D3DRS_SRCBLENDALPHA_OLD))
      {
      case D3DBLEND_ZERO_OLD:
        desc._alphaSrcBlend = SBZero;
        break;
      case D3DBLEND_ONE_OLD:
        desc._alphaSrcBlend = SBOne;
        break;
      case D3DBLEND_SRCALPHA_OLD:
        desc._alphaSrcBlend = SBSrcAlpha;
        break;
      default:
        ErrF("DX11 state fallback: alpha src blend=%u", (unsigned)GetRSValue(info, D3DRS_SRCBLENDALPHA_OLD));
        desc._alphaSrcBlend = SBOne;
        break;
      }

      // DestBlend
      switch (GetRSValue(info,  D3DRS_DESTBLENDALPHA_OLD))
      {
      case D3DBLEND_ZERO_OLD:
        desc._alphaDestBlend = DBZero;
        break;
      case D3DBLEND_ONE_OLD:
        desc._alphaDestBlend = DBOne;
        break;
      case D3DBLEND_INVSRCALPHA_OLD:
        desc._alphaDestBlend = DBInvSrcAlpha;
        break;
      case D3DBLEND_SRCCOLOR_OLD:
        desc._alphaDestBlend = DBSrcColor;
        break;
      default:
        ErrF("DX11 state fallback: alpha dest blend=%u", (unsigned)GetRSValue(info, D3DRS_DESTBLENDALPHA_OLD));
        desc._alphaDestBlend = DBInvSrcAlpha;
        break;
      }

      // BlendOp
      switch (GetRSValue(info,  D3DRS_BLENDOPALPHA_OLD))
      {
      case D3DBLENDOP_ADD_OLD:
        desc._alphaBlendOp = BOAdd;
        break;
      default:
        ErrF("DX11 state fallback: alpha blend op=%u", (unsigned)GetRSValue(info, D3DRS_BLENDOPALPHA_OLD));
        desc._alphaBlendOp = BOAdd;
        break;
      }
    }
    else
    {
      desc._alphaBlendEnable = BEFalse;
      desc._alphaSrcBlend = SBZero;
      desc._alphaDestBlend = DBZero;
      desc._alphaBlendOp = BOAdd;
    }
  }
  else
  {
    desc._blendEnable = BEFalse;
    desc._srcBlend = SBZero;
    desc._destBlend = DBZero;
    desc._blendOp = BOAdd;
    desc._alphaBlendEnable = BEFalse;
    desc._alphaSrcBlend = SBZero;
    desc._alphaDestBlend = DBZero;
    desc._alphaBlendOp = BOAdd;
  }


  // write mask
  DWORD writeMask = GetRSValue(info,  D3DRS_COLORWRITEENABLE_OLD);
  switch (writeMask)
  {
    case 0:
      desc._renderTargetWriteMask = RTWMNone;
      break;
    case (D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD):
      desc._renderTargetWriteMask = RTWMFullNoAlpha;
      break;
    case (D3DCOLORWRITEENABLE_RED_OLD | D3DCOLORWRITEENABLE_GREEN_OLD | D3DCOLORWRITEENABLE_BLUE_OLD | D3DCOLORWRITEENABLE_ALPHA_OLD):
      desc._renderTargetWriteMask = RTWMFull;
      break;
    default:
      ErrF("DX11 state fallback: color write mask=%u", (unsigned)GetRSValue(info, D3DRS_COLORWRITEENABLE_OLD));
      desc._renderTargetWriteMask = RTWMFull;
      break;
  }

  const ID3D11BlendState* state = GetState(desc);
  if (!state)
  {
    DoAssert(state);
    Fail("DX11 Invalid blend state - cannot get state from description!");
  }
  return state;  
}

bool BlendStateList::IsStateUsable(const BlendStateList::StateDesc &desc) const
{
  if (desc._blendEnable == BEFalse)
  {
    if (desc._srcBlend != SBZero || desc._destBlend != SBZero ||
      desc._blendOp != BOAdd || desc._alphaBlendEnable != BEFalse ||
      desc._alphaSrcBlend != SBZero || desc._alphaDestBlend != SBZero ||
      desc._alphaBlendOp != BOAdd      
      )
      return false;
  }

  if (desc._alphaDestBlend == DBSrcColor || desc._alphaSrcBlend == SBDestColor)
    return false;

  return true;
}

bool BlendStateList::IsDescValid(const BlendStateList::StateDesc &desc) const
{
  return (desc._alphaToCoverageEnable >= 0 && desc._alphaToCoverageEnable < ATOCCount &&
    desc._blendEnable >= 0 && desc._blendEnable < BECount &&
    desc._srcBlend >=0 && desc._srcBlend < SBCount &&
    desc._destBlend >=0 && desc._destBlend < DBCount &&
    desc._blendOp >= 0 && desc._blendOp < BOCount &&
    desc._alphaBlendEnable >= 0 && desc._alphaBlendEnable < BECount &&
    desc._alphaSrcBlend >=0 && desc._alphaSrcBlend < SBCount &&
    desc._alphaDestBlend >=0 && desc._alphaDestBlend < DBCount &&
    desc._alphaBlendOp >= 0 && desc._alphaBlendOp < BOCount &&
    desc._renderTargetWriteMask >= 0 && desc._renderTargetWriteMask < RTWMCount
    );
}

bool BlendStateList::CreateState(const BlendStateList::StateDesc &desc)
{
  if (!IsStateUsable(desc))
    return false;

  D3D11_BLEND_DESC bDesc;

  bDesc.AlphaToCoverageEnable = desc._alphaToCoverageEnable == ATOCTrue ? TRUE : FALSE;
  bDesc.IndependentBlendEnable = FALSE;

  D3D11_RENDER_TARGET_BLEND_DESC& rtDesc = bDesc.RenderTarget[0];
  
  if (desc._blendEnable == BETrue)
  {
    rtDesc.BlendEnable = TRUE;
    switch (desc._srcBlend)
    {
    case SBZero:
      rtDesc.SrcBlend = D3D11_BLEND_ZERO;
      break;
    case SBOne:
      rtDesc.SrcBlend = D3D11_BLEND_ONE;
      break;
    case SBSrcAlpha:
      rtDesc.SrcBlend = D3D11_BLEND_SRC_ALPHA;
      break;
    case SBDestColor:
      rtDesc.SrcBlend = D3D11_BLEND_DEST_COLOR;
      break;
    default:
      Fail("Invalid src blend");
      rtDesc.SrcBlend = D3D11_BLEND_ONE;
      break;
    }

    switch (desc._destBlend)
    {
    case DBZero:
      rtDesc.DestBlend = D3D11_BLEND_ZERO;
      break;
    case DBOne:
      rtDesc.DestBlend = D3D11_BLEND_ONE;
      break;
    case DBInvSrcAlpha:
      rtDesc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
      break;
    case DBSrcColor:
      rtDesc.DestBlend = D3D11_BLEND_SRC_COLOR;
      break;
    default:
      Fail("Invalid dest blend");
      rtDesc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
      break;
    }

    switch (desc._blendOp)
    {
    case BOAdd:
      rtDesc.BlendOp = D3D11_BLEND_OP_ADD;
      break;
    default:
      Fail("Invalid blend op");
      rtDesc.BlendOp = D3D11_BLEND_OP_ADD;
      break;
    }

    // independent blend
    if (desc._alphaBlendEnable == BETrue)
    {
      bDesc.IndependentBlendEnable = TRUE;

      switch (desc._alphaSrcBlend)
      {
      case SBZero:
        rtDesc.SrcBlendAlpha = D3D11_BLEND_ZERO;
        break;
      case SBOne:
        rtDesc.SrcBlendAlpha = D3D11_BLEND_ONE;
        break;
      case SBSrcAlpha:
        rtDesc.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        break;
      default:
        Fail("Invalid src blend");
        rtDesc.SrcBlendAlpha = D3D11_BLEND_ONE;
        break;
      }

      switch (desc._alphaDestBlend)
      {
      case DBZero:
        rtDesc.DestBlendAlpha = D3D11_BLEND_ZERO;
        break;
      case DBOne:
        rtDesc.DestBlendAlpha = D3D11_BLEND_ONE;
        break;
      case DBInvSrcAlpha:
        rtDesc.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        break;
      case DBSrcColor:
        rtDesc.DestBlendAlpha = D3D11_BLEND_SRC_COLOR;
        break;
      default:
        Fail("Invalid dest blend");
        rtDesc.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        break;
      }

      switch (desc._alphaBlendOp)
      {
      case BOAdd:
        rtDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        break;
      default:
        Fail("Invalid blend op");
        rtDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        break;
      }
    }
    else
    {
      bDesc.IndependentBlendEnable = FALSE;
      rtDesc.SrcBlendAlpha = D3D11_BLEND_ZERO;
      rtDesc.DestBlendAlpha = D3D11_BLEND_ZERO;
      rtDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }

  }
  else
  {
    rtDesc.BlendEnable = FALSE;
    rtDesc.SrcBlend = D3D11_BLEND_ZERO;
    rtDesc.DestBlend = D3D11_BLEND_ZERO;
    rtDesc.BlendOp = D3D11_BLEND_OP_ADD;

    bDesc.IndependentBlendEnable = FALSE;
    rtDesc.SrcBlendAlpha = D3D11_BLEND_ZERO;
    rtDesc.DestBlendAlpha = D3D11_BLEND_ZERO;
    rtDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  }
  

  switch (desc._renderTargetWriteMask)
  {
  case RTWMNone:
    rtDesc.RenderTargetWriteMask = 0;
    break;
  case RTWMFullNoAlpha:
    rtDesc.RenderTargetWriteMask = 
      D3D11_COLOR_WRITE_ENABLE_RED |
      D3D11_COLOR_WRITE_ENABLE_GREEN |
      D3D11_COLOR_WRITE_ENABLE_BLUE;
    break;
  case RTWMFull:
    rtDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    break;
  default:
    Fail("Invalid render target write mask");
    rtDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    break;
  }

  // fill out default values for rest of the render targets
  for (int i=1; i<8; ++i)
  {
    D3D11_RENDER_TARGET_BLEND_DESC& rtDesc = bDesc.RenderTarget[i];
    rtDesc.BlendEnable = FALSE;
    rtDesc.SrcBlend = D3D11_BLEND_ONE;
    rtDesc.DestBlend = D3D11_BLEND_ZERO;
    rtDesc.BlendOp = D3D11_BLEND_OP_ADD;
    rtDesc.SrcBlendAlpha = D3D11_BLEND_ONE;
    rtDesc.DestBlendAlpha = D3D11_BLEND_ZERO;
    rtDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rtDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  }

  // Create state object
  HRESULT hr = GEngineDD11->GetD3DDeviceNonRec().CreateBlendState(&bDesc, 
    _states
    [desc._alphaToCoverageEnable]
    [desc._blendEnable]
    [desc._srcBlend]
    [desc._destBlend]
    [desc._blendOp]
    [desc._alphaBlendEnable]
    [desc._alphaSrcBlend]
    [desc._alphaDestBlend]
    [desc._alphaBlendOp]
    [desc._renderTargetWriteMask].Init()
    );

  if (FAILED(hr))
  {
    GEngineDD11->DDError11("CreateBlendState failed", hr);
    return false;
  }

  return true;
}

///=========================================================
// Implementation of RasterizerStateList
///=========================================================

void RasterizerStateList::CreateStates()
{
  LogF("RasterizerStateList - creating states.");

  int numCreated = 0;

  for (int fillMode = 0; fillMode < FMCount; ++fillMode)
  {
    for (int cullMode = 0; cullMode < CMCount; ++cullMode)
    {
      for (int frontCCW = 0; frontCCW < FCCWCount; ++frontCCW)
      {
        for (int depthBias = 0; depthBias < DBCount; ++depthBias)
        {
          if (CreateState(StateDesc(
            (FillMode)fillMode,
            (CullMode)cullMode,
            (FrontCounterClockwise)frontCCW,
            (DepthBias)depthBias
            )))
          {
            numCreated++;
          }
        }
      }
    }
  }

  LogF("RasterizerStateList - created %d states.", numCreated);
  if (numCreated > MaxStateCount)
  {
    Assert(false);
    Fail("RasterizerStateList - number of maximum states exceeded!");
  }
}

void RasterizerStateList::Clear()
{
  for (int fillMode = 0; fillMode < FMCount; ++fillMode)
  {
    for (int cullMode = 0; cullMode < CMCount; ++cullMode)
    {
      for (int frontCCW = 0; frontCCW < FCCWCount; ++frontCCW)
      {
        for (int depthBias = 0; depthBias < DBCount; ++depthBias)
        {
          _states
            [fillMode]
          [cullMode]
          [frontCCW]
          [depthBias].Free();
        }
      }
    }
  }
}

const ID3D11RasterizerState* RasterizerStateList::GetState(const RasterizerStateList::StateDesc &desc) const
{
  DoAssert(IsDescValid(desc));
  DoAssert(IsStateUsable(desc));
  return _states
    [desc._fillMode]
    [desc._cullMode]
    [desc._frontCounterClockwise]
    [desc._depthBias];
}

const ID3D11RasterizerState* RasterizerStateList::GetState(const AutoArray<DWORD>& info) const
{
  StateDesc desc;

  // FillMode
  switch (GetRSValue(info,   D3DRS_FILLMODE_OLD))
  {
  case D3DFILL_WIREFRAME_OLD:
    desc._fillMode = FMWireframe;
    break;
  case D3DFILL_SOLID_OLD:
    desc._fillMode = FMSolid;
    break;
  default:
    ErrF("DX11 state fallback: fill mode=%u", (unsigned)GetRSValue(info, D3DRS_FILLMODE_OLD));
    desc._fillMode = FMSolid;
    break;
  }

  // CullMode
  switch (GetRSValue(info,  D3DRS_CULLMODE_OLD))
  {
  case D3DCULL_NONE_OLD:
    desc._cullMode = CMNone;
    desc._frontCounterClockwise = FCCWTrue;
    break;
  case D3DCULL_CW_OLD:
    desc._cullMode = CMBack;
    desc._frontCounterClockwise = FCCWTrue;
    break;
  case D3DCULL_CCW_OLD:
    desc._cullMode = CMBack;
    desc._frontCounterClockwise = FCCWFalse;
    break;
  default:
    ErrF("DX11 state fallback: cull mode=%u", (unsigned)GetRSValue(info, D3DRS_CULLMODE_OLD));
    desc._cullMode = CMBack;
    desc._frontCounterClockwise = FCCWTrue;
    break;
  }

  DWORD depthBias = GetRSValue(info,   D3DRS_DEPTHBIAS_OLD);
  if (depthBias != 0)
    desc._depthBias = DBShadow;
  else
    desc._depthBias = DBNone;

  const ID3D11RasterizerState* state = GetState(desc);
  if (!state)
  {
    DoAssert(state);
    Fail("DX11 Invalid rasterizer state - cannot get state from description!");
  }
  return state;
}

bool RasterizerStateList::IsStateUsable(const RasterizerStateList::StateDesc &desc) const
{
  return true;  
}

bool RasterizerStateList::IsDescValid(const RasterizerStateList::StateDesc &desc) const
{
  return desc._fillMode >= 0 && desc._fillMode < FMCount &&
    desc._cullMode >= 0 && desc._cullMode < CMCount &&
    desc._frontCounterClockwise >= 0 && desc._frontCounterClockwise < FCCWCount &&
    desc._depthBias >= 0 && desc._depthBias < DBCount;
}

bool RasterizerStateList::CreateState(const RasterizerStateList::StateDesc &desc)
{
  if (!IsStateUsable(desc))
    return false;

  D3D11_RASTERIZER_DESC rDesc;
  
  switch (desc._fillMode)
  {
  case FMWireframe:
    rDesc.FillMode = D3D11_FILL_WIREFRAME;
    break;
  case FMSolid:
    rDesc.FillMode = D3D11_FILL_SOLID;
    break;
  default:
    Fail("Invalid fill mode");
    rDesc.FillMode = D3D11_FILL_SOLID;
    break;
  }

  switch (desc._cullMode)
  {
  case CMNone:
    rDesc.CullMode = D3D11_CULL_NONE;
    break;
  case CMBack:
    rDesc.CullMode = D3D11_CULL_BACK;
    break;
  default:
    Fail("Invalid cull mode");
    rDesc.CullMode = D3D11_CULL_BACK;
    break;
  }

  rDesc.FrontCounterClockwise = (desc._frontCounterClockwise == FCCWTrue) ? TRUE : FALSE;

  switch (desc._depthBias)
  {
  case DBNone:
    rDesc.DepthBias = 0;
    rDesc.SlopeScaledDepthBias = 0;
    rDesc.DepthBiasClamp = 0;
    break;
  case DBShadow:
    rDesc.DepthBias = ShadowMapDepthBias;
    rDesc.SlopeScaledDepthBias = ShadowMapSlopeScaleDepthBias;
    rDesc.DepthBiasClamp = 0;
    break;
  default:
    Fail("Invalid depth bias");
    rDesc.DepthBias = 0;
    rDesc.SlopeScaledDepthBias = 0;
    rDesc.DepthBiasClamp = 0;
    break;
  }

  rDesc.DepthClipEnable = TRUE;
  rDesc.ScissorEnable = FALSE;
  rDesc.MultisampleEnable = TRUE;
  rDesc.AntialiasedLineEnable = FALSE;

  // Create state object
  HRESULT hr = GEngineDD11->GetD3DDeviceNonRec().CreateRasterizerState(&rDesc, 
    _states
    [desc._fillMode]
    [desc._cullMode]
    [desc._frontCounterClockwise]
    [desc._depthBias].Init()
    );

  if (FAILED(hr))
  {
    GEngineDD11->DDError11("CreateRasterizerState failed", hr);
    return false;
  }

  return true;
}

///=========================================================
// Implementation of SamplerStateList
///=========================================================

void SamplerStateList::CreateStates()
{
  LogF("SamplerStateList - creating states.");

  int numCreated = 0;

  for (int filter = 0; filter<FCount; ++filter)
  {
    for (int addressModeU = 0; addressModeU < AMCount; ++addressModeU)
    {
      for (int addressModeV = 0; addressModeV < AMCount; ++addressModeV)
      {
        for (int addressModeW = 0; addressModeW < AMCount; ++addressModeW)
        {
          for (int mipFiltering = 0; mipFiltering < MFCount; ++mipFiltering)
          {
            for (int borderColor = 0; borderColor < BCCount; ++borderColor)
            {
              for (int anisotropy = 0; anisotropy <= MaxAnisotropy; ++anisotropy)
              {
                if (CreateState(StateDesc(
                  (Filter)filter,
                  (AddressMode)addressModeU,
                  (AddressMode)addressModeV,
                  (AddressMode)addressModeW,
                  (MipFiltering)mipFiltering,
                  (BorderColor)borderColor, 
                  anisotropy
                  )))
                {
                  numCreated++;
                }
              }
            }
          }
        }
      }
    }
  }

  LogF("SamplerStateList - created %d states.", numCreated);
  if (numCreated > MaxStateCount)
  {
    Assert(false);
    Fail("SamplerStateList - number of maximum states exceeded!");
  }

}

void SamplerStateList::Clear()
{
  for (int filter = 0; filter<FCount; ++filter)
  {
    for (int addressModeU = 0; addressModeU < AMCount; ++addressModeU)
    {
      for (int addressModeV = 0; addressModeV < AMCount; ++addressModeV)
      {
        for (int addressModeW = 0; addressModeW < AMCount; ++addressModeW)
        {
          for (int mipFiltering = 0; mipFiltering < MFCount; ++mipFiltering)
          {
            for (int borderColor = 0; borderColor < BCCount; ++borderColor)
            {
              for (int anisotropy = 0; anisotropy <= MaxAnisotropy; ++anisotropy)
              {
                _states
                  [filter]
                [addressModeU]
                [addressModeV]
                [addressModeW]
                [mipFiltering]
                [borderColor]
                [anisotropy].Free();
              }
            }
          }
        }
      }
    }
  }
}

const ID3D11SamplerState* SamplerStateList::GetState(const SamplerStateList::StateDesc &desc) const
{
  DoAssert(IsDescValid(desc));
  DoAssert(IsStateUsable(desc));
  return _states
    [desc._filter]
  [desc._addressModeU]
  [desc._addressModeV]
  [desc._addressModeW]
  [desc._mipFiltering]
  [desc._borderColor]
  [desc._anisotropy];
}

const ID3D11SamplerState* SamplerStateList::GetState(const AutoArray<DWORD>& info) const
{
  StateDesc desc;

  desc._mipFiltering = MFTrue;

  DWORD minFilter = GetRSValue(info,   D3DSAMP_MINFILTER_OLD);
  DWORD magFilter = GetRSValue(info,   D3DSAMP_MAGFILTER_OLD);
  DWORD mipFilter = GetRSValue(info,   D3DSAMP_MIPFILTER_OLD);

  // Filter
  if (minFilter == D3DTEXF_POINT_OLD &&
    magFilter == D3DTEXF_POINT_OLD &&
    mipFilter == D3DTEXF_POINT_OLD)
  {
    desc._filter = F_MIN_MAG_MIP_POINT;
  }
  else if (minFilter == D3DTEXF_POINT_OLD &&
    magFilter == D3DTEXF_POINT_OLD &&
    mipFilter == D3DTEXF_LINEAR_OLD)
  {
    desc._filter = F_MIN_MAG_POINT_MIP_LINEAR;
  }

  else if (minFilter == D3DTEXF_POINT_OLD &&
    magFilter == D3DTEXF_LINEAR_OLD &&
    mipFilter == D3DTEXF_POINT_OLD)
  {
    desc._filter = F_MIN_POINT_MAG_LINEAR_MIP_POINT;
  }
  else if (minFilter == D3DTEXF_POINT_OLD &&
    magFilter == D3DTEXF_LINEAR_OLD &&
    mipFilter == D3DTEXF_LINEAR_OLD)
  {
    desc._filter = F_MIN_POINT_MAG_MIP_LINEAR;
  }
  else if (minFilter == D3DTEXF_LINEAR_OLD &&
    magFilter == D3DTEXF_POINT_OLD &&
    mipFilter == D3DTEXF_POINT_OLD)
  {
    desc._filter = F_MIN_LINEAR_MAG_MIP_POINT;
  }
  else if (minFilter == D3DTEXF_LINEAR_OLD &&
    magFilter == D3DTEXF_POINT_OLD &&
    mipFilter == D3DTEXF_LINEAR_OLD)
  {
    desc._filter = F_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
  }
  else if (minFilter == D3DTEXF_LINEAR_OLD &&
    magFilter == D3DTEXF_LINEAR_OLD &&
    mipFilter == D3DTEXF_POINT_OLD)
  {
    desc._filter = F_MIN_MAG_LINEAR_MIP_POINT;
  }
  else if (minFilter == D3DTEXF_LINEAR_OLD &&
    magFilter == D3DTEXF_LINEAR_OLD &&
    mipFilter == D3DTEXF_LINEAR_OLD)
  {
    desc._filter = F_MIN_MAG_MIP_LINEAR;
  }

  else if (minFilter == D3DTEXF_ANISOTROPIC_OLD &&
    magFilter == D3DTEXF_ANISOTROPIC_OLD &&
    mipFilter == D3DTEXF_ANISOTROPIC_OLD)
  {
    desc._filter = F_ANISOTROPIC;
  }
  else if (minFilter == D3DTEXF_POINT_OLD &&
    magFilter == D3DTEXF_POINT_OLD &&
    mipFilter == D3DTEXF_NONE_OLD)
  {
    desc._filter = F_MIN_MAG_MIP_POINT;
    desc._mipFiltering = MFFalse;
  }
  else if (minFilter == D3DTEXF_LINEAR_OLD &&
    magFilter == D3DTEXF_LINEAR_OLD &&
    mipFilter == D3DTEXF_NONE_OLD)
  {
    desc._filter = F_MIN_MAG_LINEAR_MIP_POINT;
    desc._mipFiltering = MFFalse;
  }
  else if (minFilter == D3DTEXF_POINT_OLD &&
    magFilter == D3DTEXF_POINT_OLD &&
    mipFilter == D3DTEXF_ANISOTROPIC_OLD)
  {
    desc._filter = F_MIN_MAG_MIP_POINT;
  }
  else
  {
    desc._filter = F_MIN_MAG_MIP_LINEAR;
    ErrF("DX11 state fallback: sampler filter min=%u mag=%u mip=%u", (unsigned)minFilter, (unsigned)magFilter, (unsigned)mipFilter);
  }

  // AddressU
  switch (GetRSValue(info,   D3DSAMP_ADDRESSU_OLD))
  {
  case D3DTADDRESS_WRAP_OLD:
    desc._addressModeU = AMWrap;
    break;
  case D3DTADDRESS_MIRROR_OLD:
    desc._addressModeU = AMMirror;
    break;
  case D3DTADDRESS_CLAMP_OLD:
    desc._addressModeU = AMClamp;
    break;
  case D3DTADDRESS_BORDER_OLD:
    desc._addressModeU = AMBorder;
    break;
  default:
    ErrF("DX11 state fallback: sampler address U=%u", (unsigned)GetRSValue(info, D3DSAMP_ADDRESSU_OLD));
    desc._addressModeU = AMClamp;
    break;
  }

  // AddressV
  switch (GetRSValue(info,   D3DSAMP_ADDRESSV_OLD))
  {
  case D3DTADDRESS_WRAP_OLD:
    desc._addressModeV = AMWrap;
    break;
  case D3DTADDRESS_MIRROR_OLD:
    desc._addressModeV = AMMirror;
    break;
  case D3DTADDRESS_CLAMP_OLD:
    desc._addressModeV = AMClamp;
    break;
  case D3DTADDRESS_BORDER_OLD:
    desc._addressModeV = AMBorder;
    break;
  default:
    ErrF("DX11 state fallback: sampler address V=%u", (unsigned)GetRSValue(info, D3DSAMP_ADDRESSV_OLD));
    desc._addressModeV = AMClamp;
    break;
  }

  // AddressW
  switch (GetRSValue(info, D3DSAMP_ADDRESSW_OLD))
  {
  case D3DTADDRESS_WRAP_OLD:
    desc._addressModeW = AMWrap;
    break;
  case D3DTADDRESS_MIRROR_OLD:
    desc._addressModeW = AMMirror;
    break;
  case D3DTADDRESS_CLAMP_OLD:
    desc._addressModeW = AMClamp;
    break;
  case D3DTADDRESS_BORDER_OLD:
    desc._addressModeW = AMBorder;
    break;
  default:
    //Fail("DX11 Error: Not handled");
    // do not write message here - addressw don't have to be set
    desc._addressModeW = AMClamp;
    break;
  }

  // border color
  if (desc._addressModeU != AMBorder && desc._addressModeV != AMBorder && desc._addressModeW != AMBorder)
  {
    desc._borderColor = BCBlack;
  }
  else
  {
    switch (GetRSValue(info,   D3DSAMP_BORDERCOLOR_OLD))
    {
    case 0x0:
      desc._borderColor = BCBlack;
      break;
    case 0xFFFFFFFF:
      desc._borderColor = BCWhite;
      break;
    default:
      ErrF("DX11 state fallback: sampler border color=%u", (unsigned)GetRSValue(info, D3DSAMP_BORDERCOLOR_OLD));
      desc._borderColor = BCBlack;
      break;
    }
  }

  // MaxAnisotropy
  if (desc._filter == F_ANISOTROPIC)
  {
    desc._anisotropy = GetRSValue(info,   D3DSAMP_MAXANISOTROPY_OLD);
  }
  else
  {
    desc._anisotropy = 1;
  }

  const ID3D11SamplerState* state = GetState(desc);
  if (!state)
  {
    DoAssert(state);
    Fail("DX11 Invalid sampler state - cannot get state from description!");
  }
  return state;

}

bool SamplerStateList::IsStateUsable(const SamplerStateList::StateDesc &desc) const
{
  if (desc._filter != F_ANISOTROPIC)
  {
    if (desc._anisotropy != 1)
      return false;
  }
  if (desc._addressModeU != AMBorder && desc._addressModeV != AMBorder && desc._addressModeW != AMBorder)
  {
    if (desc._borderColor != BCBlack)
      return false;
  }
  if (desc._mipFiltering == MFFalse)
  {
    if (desc._filter != F_MIN_MAG_MIP_POINT && desc._filter != F_MIN_MAG_LINEAR_MIP_POINT)
      return false;
  }

  return true;
}

bool SamplerStateList::IsDescValid(const SamplerStateList::StateDesc &desc) const
{
  return desc._filter >= 0 && desc._filter < FCount &&
    desc._addressModeU >= 0 && desc._addressModeU < AMCount &&
    desc._addressModeV >= 0 && desc._addressModeV < AMCount &&
    desc._addressModeW >= 0 && desc._addressModeW < AMCount &&
    desc._mipFiltering >= 0 && desc._mipFiltering < MFCount &&
    desc._borderColor >= 0 && desc._borderColor < BCCount &&
    desc._anisotropy >= 0 && desc._anisotropy <= MaxAnisotropy;
}

bool SamplerStateList::CreateState(const SamplerStateList::StateDesc &desc)
{
  if (!IsStateUsable(desc))
    return false;

  D3D11_SAMPLER_DESC sDesc;

  switch (desc._filter)
  {
    case F_MIN_MAG_MIP_POINT:
      sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
      break;
    case F_MIN_MAG_POINT_MIP_LINEAR:
      sDesc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
      break;
    case F_MIN_POINT_MAG_LINEAR_MIP_POINT:
      sDesc.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
      break;
    case F_MIN_POINT_MAG_MIP_LINEAR:
      sDesc.Filter = D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
      break;
    case F_MIN_LINEAR_MAG_MIP_POINT:
      sDesc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
      break;
    case F_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
      sDesc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
      break;
    case F_MIN_MAG_LINEAR_MIP_POINT:
      sDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      break;
    case F_MIN_MAG_MIP_LINEAR:
      sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      break;
    case F_ANISOTROPIC:
      sDesc.Filter = D3D11_FILTER_ANISOTROPIC;
      break;
    default:
      Fail("Invalid filter.");     
      sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      break;
  }

  switch (desc._addressModeU)
  {
  case AMWrap:
    sDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    break;
  case AMMirror:
    sDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
    break;
  case AMClamp:
    sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    break;
  case AMBorder:
    sDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    break;
  default:
    Fail("Invalid address mode U.");     
    sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    break;
  }

  switch (desc._addressModeV)
  {
  case AMWrap:
    sDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    break;
  case AMMirror:
    sDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
    break;
  case AMClamp:
    sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    break;
  case AMBorder:
    sDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    break;
  default:
    Fail("Invalid address mode V.");     
    sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    break;
  }

  switch (desc._addressModeW)
  {
  case AMWrap:
    sDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    break;
  case AMMirror:
    sDesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
    break;
  case AMClamp:
    sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    break;
  case AMBorder:
    sDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    break;
  default:
    Fail("Invalid address mode W.");     
    sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    break;
  }

  // border color
  sDesc.BorderColor[0] = 0.0f;
  sDesc.BorderColor[1] = 0.0f;
  sDesc.BorderColor[2] = 0.0f;
  sDesc.BorderColor[3] = 0.0f;

  switch (desc._borderColor)
  {
  case BCBlack:
    break;
  case BCWhite:
    sDesc.BorderColor[0] = 1.0f;
    sDesc.BorderColor[1] = 1.0f;
    sDesc.BorderColor[2] = 1.0f;
    sDesc.BorderColor[3] = 1.0f;
    break;
  default:
    Fail("Invalid border color.");     
    break;
  }


  sDesc.MipLODBias = 0;
  sDesc.MaxAnisotropy = desc._anisotropy;
  sDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;


  if (desc._mipFiltering == MFFalse)
  {
    sDesc.MinLOD = 0;
    sDesc.MaxLOD = 0;
  }
  else
  {
    sDesc.MinLOD = -D3D11_FLOAT32_MAX;
    sDesc.MaxLOD = D3D11_FLOAT32_MAX;
  }

  // Create state object
  HRESULT hr = GEngineDD11->GetD3DDeviceNonRec().CreateSamplerState(&sDesc, 
    _states
    [desc._filter]
  [desc._addressModeU]
  [desc._addressModeV]
  [desc._addressModeW]
  [desc._mipFiltering]
  [desc._borderColor]
  [desc._anisotropy].Init()
    );

  if (FAILED(hr))
  {
    GEngineDD11->DDError11("CreateSamplerState failed", hr);
    return false;
  }

  return true;

}

} //namespace DX11

#endif //_ENABLE_DX11

#ifndef _DX11_HLSL_VSCONSTANTS_H
#define _DX11_HLSL_VSCONSTANTS_H

//! Constant definition
#define VCS_HLSL_DEF_S(CBnum, Type, Name, RegNum) Type Name : packoffset(c##RegNum);
#define VCS_HLSL_DEF_C(CBnum, Type, Name, RegNum, SubComp) Type Name : packoffset(c##RegNum.##SubComp);
#define VCS_HLSL_DEF_A(CBnum, Type, Name, RegNum, ArrSize) Type Name[ArrSize] : packoffset(c##RegNum);
#define VSC_HLSL_CB_BEG(Name, Number, Multisize) cbuffer Name : register(b##Number) {
#define VSC_HLSL_CB_END(Name) };

VSC_LIST(VCS_HLSL_DEF_S, VCS_HLSL_DEF_C, VCS_HLSL_DEF_A, VSC_HLSL_CB_BEG, VSC_HLSL_CB_END)

#endif //_DX11_HLSL_VSCONSTANTS_H
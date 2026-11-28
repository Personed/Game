#ifndef _DX11_HLSL_PSCONSTANTS_H
#define _DX11_HLSL_PSCONSTANTS_H

//! Constant definition
#define PCS_HLSL_DEF_S(CBnum, Type, Name, RegNum) Type Name : packoffset(c##RegNum);
#define PCS_HLSL_DEF_C(CBnum, Type, Name, RegNum, SubComp) Type Name : packoffset(c##RegNum.##SubComp);
#define PCS_HLSL_DEF_A(CBnum, Type, Name, RegNum, ArrSize) Type Name[ArrSize] : packoffset(c##RegNum);
#define PSC_HLSL_CB_BEG(Name, Number, Multisize) cbuffer Name : register(b##Number) {
#define PSC_HLSL_CB_END(Name) };

PSC_LIST(PCS_HLSL_DEF_S, PCS_HLSL_DEF_C, PCS_HLSL_DEF_A, PSC_HLSL_CB_BEG, PSC_HLSL_CB_END)

#endif //_DX11_HLSL_PSCONSTANTS_H

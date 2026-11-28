#include "ps.h"

#if _SM2
  //! Macro to create the specified PS
  #define CREATEPSSN(name,structureName) \
  OPT_INPUTS void PSSSSM##name(structureName input, out half4 oColor: COLOR) \
  { \
    FOut fout = PF##name(input,false,false); \
    oColor = GetFinalColor(fout); \
  }
  //! Macro to create the specified PS
  #define CREATEPSSN_DIRECT(name,structureName) \
  OPT_INPUTS void PSSSSM##name(structureName input, out half4 oColor: COLOR) \
  { \
    PF##name(input,false,input.tShadowBuffer,input.vPos,FinalColor,oColor); \
  }

  // skin shader
  OPT_INPUTS void PSSSSMSkin(SSkin i, out half4 oColor: COLOR)
  {
    half4 as = PFSkin0( i, false, false );
    half diffuseShadowCoef = as.b;
    half shadowCoef = diffuseShadowCoef * i.landShadow.a;
    SPFSkinOut o = PFSkin1( i, as, shadowCoef, false, false );
    oColor = PFSkinFinColor( o, shadowCoef );
  }
  // advanced tree PS
  OPT_INPUTS void PSSSSMTreeAdv(STreeAdv input, out half4 oColor: COLOR) { oColor = half4( 1, 0, 1, 1 ); }
  
#else
  //! Macro to create the specified PS
  #define CREATEPSSN(name,structureName) \
  OPT_INPUTS void PSSSSM##name(structureName input, out half4 oColor: COLOR) \
  { \
    RETURN_SIMPLE_COLOR \
    FOut fout = PF##name(input,true,false); \
    oColor = GetFinalColorSSSM(fout, input.tShadowBuffer, input.vPos); \
  }
  //! Macro to create the specified PS
  #define CREATEPSSN_DIRECT(name,structureName) \
  OPT_INPUTS void PSSSSM##name(structureName input, out half4 oColor: COLOR) \
  { \
    RETURN_SIMPLE_COLOR \
    PF##name(input,true,input.tShadowBuffer,input.vPos,FinalColorSSSM,oColor); \
  }
  
  // skin shader
  OPT_INPUTS void PSSSSMSkin(SSkin i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    half4 as = PFSkin0( i, true, false );
    half shadowCoef = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.landShadow, as.b );
    SPFSkinOut o = PFSkin1( i, as, shadowCoef, true, false );
    oColor = PFSkinFinColor( o, shadowCoef );
  }
  
  // advanced tree PS
  OPT_INPUTS void PSSSSMTreeAdv(STreeAdv i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.lightLocal_landShadow.w, diffShadowCoef );
    PFTreeAdv1( s, i, true, false, false, false );
    oColor = s.colorFinal;
  }  
  OPT_INPUTS void PSSSSMTreeAdvSimple(STreeAdv i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.lightLocal_landShadow.w, diffShadowCoef );
    PFTreeAdv1( s, i, true, true, false, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSSSMTreeAdvAToC(STreeAdv i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.lightLocal_landShadow.w, diffShadowCoef );
    PFTreeAdv1( s, i, true, false, true, false );
    oColor = s.colorFinal;
  }  
  OPT_INPUTS void PSSSSMTreeAdvSimpleAToC(STreeAdv i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.lightLocal_landShadow.w, diffShadowCoef );
    PFTreeAdv1( s, i, true, true, true, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSSSMTreeAdvTrunk(STreeAdv i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.lightLocal_landShadow.w, diffShadowCoef );
    PFTreeAdv1( s, i, false, false, false, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSSSMTreeAdvTrunkSimple(STreeAdv i, out half4 oColor: COLOR)
  {
    RETURN_SIMPLE_COLOR \
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = GetShadowBasicSSSM( i.tShadowBuffer, i.vPos, i.lightLocal_landShadow.w, diffShadowCoef );
    PFTreeAdv1( s, i, false, true, false, false );
    oColor = s.colorFinal;
  }
#endif

#define CREATESKINPS(name)


//! Define the shaders from list stored in header
#define PS_DEFC(name) CREATEPSSN(name,S##name)
#define PS_DEFS(name,structureName) CREATEPSSN(name,structureName)
#define PS_DEFN(name,structureName) CREATEPSSN_NS(name,structureName)
#define PS_DEFD(name) CREATEPSSN_DIRECT(name,S##name)
#define PS_DEFDS(name,structureName) CREATEPSSN_DIRECT(name,structureName)
PS_LIST(PS_DEFC,PS_DEFS,PS_DEFN,PS_DEFD,PS_DEFDS, CREATESKINPS)

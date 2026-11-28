#include "ps.h"

#if _SM2
  //! Macro to create the specified PS
  #define CREATEPSSN(name,structureName) \
  OPT_INPUTS void PSSR##name##_Default(structureName input, out half4 oColor: COLOR) \
  { \
    FOut fout = PF##name(input,false,false); \
    oColor = GetFinalColor(fout); \
  }
  //! Macro to create the specified PS
  #define CREATEPSSN_DIRECT(name,structureName) \
  OPT_INPUTS void PSSR##name##_Default(structureName input, out half4 oColor: COLOR) \
  { \
    PF##name(input,false,input.tShadowBuffer,input.vPos,FinalColor,oColor); \
  }
  //! Macro to create Skin PS
  #define CREATESKINPS(name) \
  OPT_INPUTS void PSSR##name##_Default(SSkin i, out half4 oColor: COLOR) \
  { \
    half4 as = PFSkin0( i, false, false ); \
    half shadowCoef = as.b * i.landShadow.a; \
    SPFSkinOut o = PFSkin1( i, as, shadowCoef, false, false ); \
    oColor = PFSkinFinColor( o, shadowCoef ); \
  }
  // advanced tree PS
  OPT_INPUTS void PSSRTreeAdv_Default(STreeAdv input, out half4 oColor: COLOR) { oColor = half4( 1, 0, 1, 1 ); }
  
#else
  //! Macro to create the specified PS
  #define CREATEPSSN(name,structureName) \
  OPT_INPUTS void PSSR##name##_Default(structureName input, out half4 oColor: COLOR) \
  { \
    FOut fout = PF##name(input,true,false); \
    oColor = GetFinalColorSR_Default(fout, input.tShadowBuffer, input.vPos); \
  }
  //! Macro to create the specified PS
  #define CREATEPSSN_DIRECT(name,structureName) \
  OPT_INPUTS void PSSR##name##_Default(structureName input, out half4 oColor: COLOR) \
  { \
    PF##name(input,true,input.tShadowBuffer,input.vPos,FinalColorSR_Default,oColor); \
  }
  //! Macro to create Skin PS
  #define CREATESKINPS(name) \
  OPT_INPUTS void PSSR##name##_Default(SSkin i, out half4 oColor: COLOR) \
  { \
    half4 as = PFSkin0( i, true, false ); \
    half shadowCoef = GetShadowSRDefault( i.tShadowBuffer, i.vPos, i.landShadow, as.b ); \
    SPFSkinOut o = PFSkin1( i, as, shadowCoef, true, false ); \
    oColor = PFSkinFinColor( o, shadowCoef ); \
  }

  // advanced tree
  OPT_INPUTS void PSSRTreeAdv_Default(STreeAdv input, out half4 oColor: COLOR)
{
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = 1;
    PFTreeAdv1( s, input, true, false, false, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSRTreeAdvSimple_Default(STreeAdv input, out half4 oColor: COLOR)
  {
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = 1;
    PFTreeAdv1( s, input, true, true, false, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSRTreeAdvAToC_Default(STreeAdv input, out half4 oColor: COLOR)
  {
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = 1;
    PFTreeAdv1( s, input, true, false, true, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSRTreeAdvSimpleAToC_Default(STreeAdv input, out half4 oColor: COLOR)
  {
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = 1;
    PFTreeAdv1( s, input, true, true, true, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSRTreeAdvTrunk_Default(STreeAdv input, out half4 oColor: COLOR)
  {
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = 1;
    PFTreeAdv1( s, input, false, false, false, false );
    oColor = s.colorFinal;
  }
  OPT_INPUTS void PSSRTreeAdvTrunkSimple_Default(STreeAdv input, out half4 oColor: COLOR)
  {
    SPFTreeAdv s = (SPFTreeAdv)0;
    half diffShadowCoef = 1;
    s.shadow = 1;
    PFTreeAdv1( s, input, false, true, false, false );
    oColor = s.colorFinal;
  }
#endif
  
  
//! Define the shaders from list stored in header
#define PS_DEFC(name) CREATEPSSN(name,S##name)
#define PS_DEFS(name,structureName) CREATEPSSN(name,structureName)
#define PS_DEFN(name,structureName) CREATEPSSN_NS(name,structureName)
#define PS_DEFD(name) CREATEPSSN_DIRECT(name,S##name)
#define PS_DEFDS(name,structureName) CREATEPSSN_DIRECT(name,structureName)
PS_LIST(PS_DEFC,PS_DEFS,PS_DEFN,PS_DEFD,PS_DEFDS, CREATESKINPS)

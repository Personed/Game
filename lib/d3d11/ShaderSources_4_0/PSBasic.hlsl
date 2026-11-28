#include "ps.h"

//! Macro to create the specified PS
#define CREATEPSSN(name,structureName) \
OPT_INPUTS void PS##name(structureName input, out half4 oColor: COLOR) \
{ \
  RETURN_SIMPLE_COLOR \
  FOut fout = PF##name(input,false,false); \
  oColor = GetFinalColor(fout); \
}

//! Macro to create the specified PS
#define CREATEPSSN_DIRECT(name,structureName) \
OPT_INPUTS void PS##name(structureName input, out half4 oColor: COLOR) \
{ \
  RETURN_SIMPLE_COLOR \
  PF##name(input,false,input.tShadowBuffer,input.vPos,FinalColor,oColor); \
}

//! Macro to create Skin PS
#define CREATESKINPS(name) \
OPT_INPUTS void PSSkin(SSkin i, out half4 oColor: COLOR) \
{ \
  RETURN_SIMPLE_COLOR \
  half4 as = PFSkin0( i, false, false ); \
  half shadowCoef = as.b * i.landShadow.a; \
  SPFSkinOut o = PFSkin1( i, as, shadowCoef, false, false ); \
  oColor = PFSkinFinColor( o, shadowCoef ); \
}

// advanced tree
OPT_INPUTS void PSTreeAdv(STreeAdv i, out half4 oColor: COLOR)
{
  RETURN_SIMPLE_COLOR \
  SPFTreeAdv s = (SPFTreeAdv)0;
  s.shadow = 1;
  PFTreeAdv1( s, i, true, false, false, false );
  oColor = s.colorFinal;
}
OPT_INPUTS void PSTreeAdvSimple(STreeAdv i, out half4 oColor: COLOR)
{
  RETURN_SIMPLE_COLOR \
  SPFTreeAdv s = (SPFTreeAdv)0;
  s.shadow = 1;
  PFTreeAdv1( s, i, true, true, false, false );
  oColor = s.colorFinal;
}
OPT_INPUTS void PSTreeAdvAToC(STreeAdv i, out half4 oColor: COLOR)
{
  RETURN_SIMPLE_COLOR \
  SPFTreeAdv s = (SPFTreeAdv)0;
  s.shadow = 1;
  PFTreeAdv1( s, i, true, false, true, false );
  oColor = s.colorFinal;
}
OPT_INPUTS void PSTreeAdvSimpleAToC(STreeAdv i, out half4 oColor: COLOR)
{
  RETURN_SIMPLE_COLOR \
  SPFTreeAdv s = (SPFTreeAdv)0;
  s.shadow = 1;
  PFTreeAdv1( s, i, true, true, true, false );
  oColor = s.colorFinal;
}
OPT_INPUTS void PSTreeAdvTrunk(STreeAdv i, out half4 oColor: COLOR)
{
  RETURN_SIMPLE_COLOR \
  SPFTreeAdv s = (SPFTreeAdv)0;
  s.shadow = 1;
  PFTreeAdv1( s, i, false, false, false, false );
  oColor = s.colorFinal;
}
OPT_INPUTS void PSTreeAdvTrunkSimple(STreeAdv i, out half4 oColor: COLOR)
{
  RETURN_SIMPLE_COLOR \
  SPFTreeAdv s = (SPFTreeAdv)0;
  s.shadow = 1;
  PFTreeAdv1( s, i, false, true, false, false );
  oColor = s.colorFinal;
}

//! Define the shaders from list stored in header
#define PS_DEFC(name) CREATEPSSN(name,S##name)
#define PS_DEFS(name,structureName) CREATEPSSN(name,structureName)
#define PS_DEFN(name,structureName) CREATEPSSN_NS(name,structureName)
#define PS_DEFD(name) CREATEPSSN_DIRECT(name,S##name)
#define PS_DEFDS(name,structureName) CREATEPSSN_DIRECT(name,structureName)
PS_LIST(PS_DEFC,PS_DEFS,PS_DEFN,PS_DEFD,PS_DEFDS, CREATESKINPS)

#include "ps.h"

//! Generic thermal shader - based on color and air temperature
#define CREATEPSSN(name,structureName) \
OPT_INPUTS void PSThermal##name(structureName input, out half4 oColor: COLOR) \
{ \
  FOut fout = PF##name(input,false,true); \
  oColor = GetFinalThermalSimple(fout); \
}

//! Macro to create the specified PS
#define CREATEPSSN_DIRECT(name,structureName) \
OPT_INPUTS void PSThermal##name(structureName input, out half4 oColor: COLOR) \
{ \
  PF##name(input,false,input.tShadowBuffer,input.vPos,FinalThermal,oColor); \
}

// Used for soldiers in last lod
OPT_INPUTS void PSSpecularAlphaThermal(SSpecularAlpha input, out half4 oColor: COLOR) \
{ \
	FOut fout = PFSpecularAlpha(input, false, true); \
	oColor = GetFinalThermal(fout); \
}

OPT_INPUTS void PSNormalMapSpecularDIMapThermal(SNormalMapSpecularDIMap input, out half4 oColor: COLOR) \
{ \
	FOut fout = PFNormalMapSpecularDIMap(input, false, true);\
	oColor = GetFinalThermalSimpleNoMultAlpha(fout); \
}

//! Thermal Super shader - exception for glass
OPT_INPUTS void PSSuperThermal(SSuper input, out half4 oColor: COLOR) \
{ \
  FOut fout = PFSuper(input, false, true); \
  fout.color.a = fout.color.a > 0.1 ? 1.0 : lerp(0.0, 1.0, fout.color.a * 10.0); \
  oColor = GetFinalThermal(fout); \
}

//! Macro to create Skin PS
#define CREATESKINPS(name) \
OPT_INPUTS void PSThermalSkin(SSkin i, out half4 oColor: COLOR) \
{ \
  half4 as = PFSkin0(i, false, true); \
  FOut fout = PFSkin1Ti(i, as); \
  oColor = GetFinalThermal(fout); \
}

// advanced tree
OPT_INPUTS void PSThermalTreeAdv(STreeAdv input, out half4 oColor: COLOR)
{
  SPFTreeAdv s = (SPFTreeAdv)0;
  PFTreeAdv1( s, input, true, false, false, true );
  oColor = GetFinalThermalSimple1(s.colorFinal, half4(s.diffuseShadowCoef, 0, input.tPSFog.x, 0), true, false);
}
OPT_INPUTS void PSThermalTreeAdvSimple(STreeAdv input, out half4 oColor: COLOR)
{
  SPFTreeAdv s = (SPFTreeAdv)0;
  PFTreeAdv1( s, input, true, true, false, true );  
  oColor = GetFinalThermalSimple1(s.colorFinal, half4(s.diffuseShadowCoef, 0, input.tPSFog.x, 0), true, false);
}
OPT_INPUTS void PSThermalTreeAdvAToC(STreeAdv input, out half4 oColor: COLOR)
{
  SPFTreeAdv s = (SPFTreeAdv)0;
  PFTreeAdv1( s, input, true, false, true, true );
  oColor = GetFinalThermalSimple1(s.colorFinal, half4(s.diffuseShadowCoef, 0, input.tPSFog.x, 0), true, false);
}
OPT_INPUTS void PSThermalTreeAdvSimpleAToC(STreeAdv input, out half4 oColor: COLOR)
{
  SPFTreeAdv s = (SPFTreeAdv)0;
  PFTreeAdv1( s, input, true, true, true, true );
  oColor = GetFinalThermalSimple1(s.colorFinal, half4(s.diffuseShadowCoef, 0, input.tPSFog.x, 0), true, false);
}
OPT_INPUTS void PSThermalTreeAdvTrunk(STreeAdv input, out half4 oColor: COLOR)
{
  SPFTreeAdv s = (SPFTreeAdv)0;
  PFTreeAdv1( s, input, false, false, false, true );
  oColor = GetFinalThermalSimple1(s.colorFinal, half4(s.diffuseShadowCoef, 0, input.tPSFog.x, 0), true, false);
}
OPT_INPUTS void PSThermalTreeAdvTrunkSimple(STreeAdv input, out half4 oColor: COLOR)
{
  SPFTreeAdv s = (SPFTreeAdv)0;
  PFTreeAdv1( s, input, false, true, false, true );
  oColor = GetFinalThermalSimple1(s.colorFinal, half4(s.diffuseShadowCoef, 0, input.tPSFog.x, 0), true, false);
}

//! Define the shaders from list stored in header
#define PS_DEFC(name) CREATEPSSN(name,S##name)
#define PS_DEFS(name,structureName) CREATEPSSN(name,structureName)
#define PS_DEFN(name,structureName) CREATEPSSN_NS(name,structureName)
#define PS_DEFD(name) CREATEPSSN_DIRECT(name,S##name)
#define PS_DEFDS(name,structureName) CREATEPSSN_DIRECT(name,structureName)
PS_LIST(PS_DEFC,PS_DEFS,PS_DEFN,PS_DEFD,PS_DEFDS, CREATESKINPS)

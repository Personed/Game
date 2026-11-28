#ifndef _DX11_HLSL_COMMON_H
#define _DX11_HLSL_COMMON_H

/*!
  aperture change is typically very small
  We want to encode it so that we are able to represent it with low precision (8b or 10b)

  We need to encode:
  - very small value, like exp(+_deltaT*(log2/getUsedToDark))
  - fps 100: exp(0.01f*(log2/getUsedToDark)) ~= 1.0013872
  - fps 1000: exp(0.001f*(log2/getUsedToDark)) ~= 1.0001386

  - quite large vae: 1.05
  - log(1.05)~=0.04879
  - ln(1.0001386)~=0.0001386
*/
#define EncodeApertureChange(x) (log(x)*(2.5e3/255)+128.0/255)
#define DecodeApertureChange(encoded) (exp(((encoded)-128.0/255)*(255/2.5e3)))

// Main light enumeration - vertex shader values
#define VML_None     0
#define VML_Sun      1
#define VML_Sky      2
#define VML_SetColor 3

//! Maximum number of lights to be used in one batch
//! See MAX_LIGHTS
#define MAX_LIGHTS 6

// CALM WATER defines
//! use parallax mapping
#define CALMWATER_PARALLAX
//! don't use "GetCurrRenderTargetAsTexture()"
//#define CALMWATER_SIMPLE_REFRACT
//! don't use shadow variants
//#define CALMWATER_NOSHADOWS
//! show undistort refraction only with blue tint (for debuging)
//#define CALMWATER_REFRACT_DEBUG

// TREE
// faster noise alpha for TreeAdv shader (use _CAN and _NO textures instead of _CA and _NON)
#define NEW_TREE_NOISE

// param arrays sizes
#define CALMWATER_PARS_SIZE_PS1 12
#define CALMWATER_PARS_SIZE_PS2 16
#define CALMWATER_PARS_SIZE (CALMWATER_PARS_SIZE_PS1 + CALMWATER_PARS_SIZE_PS2)

/** use this define for modifications of sea for Arma3
main modifications:
- change in refraction move value - it is now based only on sea surface normal, not on depth of object under water
- change in refractions visibility - based on the depth of object under water
*/
#define ARMA3_SEA_MOD

//! Enable/disable blur on the SSSM
//#define SSSMBLUR

// size of skinning/instancing data (number of float4 values)
#define SKINNING_INSTANCING_DATA_SIZE 168
// number of float4 in one instancing item
#define INSTANCING_ITEM_SIZE 5
// number of float4 in one sprite instancing item
#define SPRITE_INSTANCING_ITEM_SIZE 3
// number of float4 in one cloud instancing item
#define CLOUD_INSTANCING_ITEM_SIZE 3
// number of float4 in one skinning item
#define SKINNING_ITEM_SIZE 3

/// size of simul weather vertex shader parameters
#define SIMUL_WEATHER_VS_PARS_SIZE 5


/*
VSC_LIST - macro used to declare vertex shader constants.
This macro is used in 2 cases:
- for definition of constant buffers for vertex shaders
- for definition of vertex shader constant information for engine

XXBEG(Name, Number, Multisize) 
- used for definition of constant buffer start
Name - name of constant buffer
Number - number of register, where this buffer will be placed (0-14)
Multisize - true, if this constant buffer can be updated only partially (that is we can create multiple versions od CB with different sizes
            and use them as needed). 
Result will be cbuffer Name : register(b#Number);

XXEND(Name) 
- end of constant buffer deinition

XXS(CBnum, Type, Name, RegNum)
- used for definition of non-array shader constant, without sub-component placement

XXC(CBnum, Type, Name, RegNum, SubComp)
- used for definition of non-array shader constant, with sub-component placement

XXA(CBnum, Type, Name, RegNum, ArrSize)
- used for definition of array shader constant

CBNum - number of constant buffer this constant belongs to (needed for engine definitions)
Type - type of the constant (HLSL type)
Name - name of the constant
RegNum - number of register, where the constant will be packed
ArrSize - size of the items in array
SubComp - sub-component placement of constant

For more information see Shader Constants (DirectX HLSL) in DX help
*/

// Fog registers mapping
/*
FogMode:
A  B
0, 0 - FM_None
0, 1 - FM_Fog
1, 0 - FM_Alpha
1, 1 - FM_FogAlpha
*/

// DX11 TODO OPT
// Pack data better - use only registers, that are really used
// Pack single floats to sub-components (=> must be changed the way the shader constants are set in engine)

#define VSC_LIST(XXS, XXC, XXA,XXBEG,XXEND) \
  XXBEG(VSCB_NonFrequent, 0, false) \
  XXS(0, float4,    VSC_UnderwaterOcclusionPars,                       0)  /*Parameters for underwater occlusion object */\
  XXS(0, float4x3,  VSC_CausticsMatrix,                                1)  /*Caustics matrix*/\
  XXS(0, float4,    VSC_ResolX_ResolY_FovX_FovY,                       4)  /*Resolution and FOV parameters (for VSPoint shader)*/\
  XXA(0, float4,    VSC_SimulWeatherPars,                              5, SIMUL_WEATHER_VS_PARS_SIZE)     /*Simul Weather vertex shader parameters*/\
  XXEND(VSCB_NonFrequent) \
  XXBEG(VSCB_LittleFrequent, 1, false) \
  XXS(1, float4x3,  VSC_ShadowmapMatrix,                               0)  /*Matrix to transform from view space to shadowmap space*/\
  XXC(1, float,     VSC_FogModeA,                                      3, x) \
  XXC(1, float,     VSC_FogModeB,                                      3, y) \
  XXS(1, float4,    VSC_InstanceColor,                                 4)   /*Color (inc. alpha of given instance)*/\
  XXS(1, float4,    VSC_Free_ExpFog_AFogEnd_RCPAFogEndMinusAFogStart,  5)  /*alpha fog - register shared with shadowVolumeLength*/\
  XXC(1, float,     VSC_ShadowVolumeLength,                            6,x)    /**/\
  XXS(1, float4,    VSC_InstanceLandShadowIntensity,                   7)  /*Intensity of the land shadow of the given instance*/\
  XXS(1, float4x3,  VSC_LWSMatrix,                                     8) /*Local World Space matrix*/\
  XXEND(VSCB_LittleFrequent) \
  XXBEG(VSCB_MoreFrequent, 2, false) \
  XXS(2, float4x4,  VSC_ProjMatrix,                                    0)  /*Projection matrix*/\
  XXS(2, float4,    VSC_AE,                                            4)  /*(_lightAmbient * _matAmbient + _matEmmisive) in case one directional light is present, (_matEmmisive) elsewhere. A_E.w == diffuse back coeficient*/\
  XXS(2, float4,    VSC_GE,                                            5) /*Color of light reflected by ground and Emmisive color. Used by ordinary shaders, that means shader with hemispherical lighting*/\
  XXC(2, float,     VSC_ShadowReceiverFlag,                            6, x) \
  XXC(2, float,     VSC_SBTechniqueDefault,                            6, y) \
  XXC(2, float,     VSC_EnableAlignNormal,                             6, z) \
  XXC(2, float,     VSC_ShadowReceiverSSSM,                            6, w) \
  XXEND(VSCB_MoreFrequent) \
  XXBEG(VSCB_VeryFrequent, 3, false) \
  XXA(3, float4x2,  VSC_TexTransform,                                  0, 8) /*Texture coordinates*/\
  XXEND(VSCB_VeryFrequent) \
  XXBEG(VSCB_Object1, 4, false) \
  XXS(4, float4x4,  VSC_WorldMatrix,                                   0)  /*World matrix*/\
  XXS(4, float4x3,  VSC_ViewMatrix,                                    4)  /*World View matrix*/\
  XXS(4, float4,    VSC_CameraPosition,                                7)  /*Position of the camera in model space*/\
  XXS(4, float4,    VSC_UDirectionTransformedDir,                      8)   /*Direction UP in model space*/\
  XXS(4, float4,    VSC_LDirectionTransformedDir,                      9)   /*Directional light parameters*/\
  XXS(4, float4,    VSC_CameraDirection,                               10) /*Direction of the camera in model space*/\
  XXA(4, float4,    VSC_TexCoordType,                                  11, 2) /*Type of texcoord identification*/\
  XXEND(VSCB_Object1) \
  XXBEG(VSCB_Object2, 5, false) \
  XXS(5, float4,    VSC_LDirectionD,                                   0)    /*Directional light parameters*/\
  XXS(5, float4,    VSC_LDirectionS,                                   1)    /*Directional light parameters*/\
  XXS(5, float4,    VSC_LDirectionGround,                              2)    /*Directional light parameters*/\
  XXS(5, float4,    VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart, 3)  /*Specular power common for all lights & Alpha value & Fog end & 1/(fogEnd-fogStart)*/\
  XXEND(VSCB_Object2) \
  XXBEG(VSCB_Tree, 6, false) \
  XXA(6, float4,    VSC_TreeCrown,                                     0, 2) /*Tree crown parameters*/\
  XXS(6, float4,    VSC_TreeAdvPars,                                   2) /*advanced tree specific params*/\
  XXEND(VSCB_Tree) \
  XXBEG(VSCB_Water, 7, false) \
  XXS(7, float4,    VSC_Period_X_X_SeaLevel,                           0) /*Shore parameters*/\
  XXS(7, float4,    VSC_WaveHeight,                                    1)    /*Water parameters*/\
  XXS(7, float4,    VSC_WaveGrid,                                      2)    /*Water parameters - {0.9999f/grid,waterSegSize,1,(waterSegSize-1)*0.5f+0.0001f};*/\
  XXS(7, float4,    VSC_WaterPeakWhite,                                3)    /*Water parameters - {mul peak, add peak, mul shore, add shore};*/\
  XXS(7, float4,    VSC_WaterSeaLevel,                                 4)    /*Water parameters - {seaLevel, deep water mul, deep water add, deep water alpha};*/\
  XXA(7, float4,    VSC_WaterDepth,                                    5, 9*9) /*Water parameters - Depth*/\
  XXA(7, float4,    VSC_WaterPars,                                     86, CALMWATER_PARS_SIZE) /*calm water specific params*/\
  XXEND(VSCB_Water) \
  XXBEG(VSCB_Terrain1, 8, false) \
  XXS(8, float4,    VSC_TerrainAlphaAdd,                               0) /*Terrain parameters*/\
  XXS(8, float4,    VSC_TerrainAlphaMul,                               1) /*Terrain parameters*/\
  XXS(8, float4,    VSC_TerrainLODPars,                                2) /*Terrain parameters*/\
  XXS(8, float4,    VSC_TerrainStaticModulate_X,                       3) /*modulate value for static lights*/\
  XXS(8, float4,    VSC_LAND_SHADOWGRID_GRID__1__GRIDd2,               4) /*Land shadow parameters*/\
  XXEND(VSCB_Terrain1) \
  XXBEG(VSCB_Terrain2, 9, true) \
  XXA(9, float4,    VSC_LAND_SHADOW,                                   0, 144) /*Land shadow parameters*/\
  XXEND(VSCB_Terrain2) \
  XXBEG(VSCB_SkinningInstancing, 10, true) \
  XXC(10, float,     VSC_MatrixOffset,                                  0,x)    /*Offset of the matrices in the global array*/\
  XXA(10, float4,    VSC_SkinningInstancingData,                        1, SKINNING_INSTANCING_DATA_SIZE) /*Data for skining or instancing*/\
  XXEND(VSCB_SkinningInstancing) \
  XXBEG(VSCB_PostProcess, 11, false) \
  XXA(11, float4,    VSC_PPEdgeU,                                       0, 4) \
  XXA(11, float4,    VSC_PPEdgeV,                                       4, 4) \
  XXS(11, float4,    VSC_PPEdgePosOffset,                               8)\
  XXS(11, float4x3,  VSC_RainViewMatrix,                                9)\
  XXS(11, float4x4,  VSC_RainProjMatrix,                                12)\
  XXS(11, float4,    VSC_RainShiftScale,                                16)\
  XXS(11, float4,    VSC_RainMinBox,                                    17)\
  XXS(11, float4,    VSC_RainMaxBox,                                    18)\
  XXS(11, float4,    VSC_RainSeaLevel,                                  19)\
  XXS(11, float4,    VSC_SSAOPixelOffsets,                              20)\
  XXS(11, float4,    VSC_SSAONoisePixelScale,                           21)\
  XXS(11, float4,    VSC_RotBlur_mA0,                                   22)\
  XXS(11, float4,    VSC_RotBlur_mA1,                                   23)\
  XXS(11, float4,    VSC_RotBlur_mA2,                                   24)\
  XXS(11, float4,    VSC_ProjConsts,                                    25) /* projection data for aspect correction */\
  XXS(11, float4,    VSC_PPWetDistortPars,                              26) /* time, blur, powerTop, powerBottom */\
  XXS(11, float4,    VSC_PPWDSpeeds,                                    27) /* speeds */\
  XXS(11, float4,    VSC_PPWDAmps,                                      28) /* amplitudes */\
  XXS(11, float4,    VSC_PPWDPhases,                                    29) /* randX, randY, posX, posY */\
  XXS(11, float4,    VSC_PPDynBlurPars0,                                30) \
  XXS(11, float4,    VSC_PPDynBlurPars1,                                31) \
  XXEND(VSCB_PostProcess) \
  XXBEG(VSCB_Lights1, 12, false) \
  XXS(12, float4,    VSC_PointLoopCount,                                0)     /*holds number of point lights, it is being used for static looping*/\
  XXS(12, float4,    VSC_SpotLoopCount,                                 1)     /*holds number of spot lights, it is being used for static looping*/\
  XXEND(VSCB_Lights1) \
  XXBEG(VSCB_Lights2, 13, true) \
  XXA(13, float4,    VSC_LPSData,                                       0, MAX_LIGHTS*4 ) /* P&S lights array  */ \
  XXEND(VSCB_Lights2) \


#define VSC_LDirectionTransformedDirSky VSC_LDirectionTransformedDir  /*Directional Sky light parameters*/
#define VSC_LDirectionSunSkyColor       VSC_LDirectionD               /*Directional Sky light parameters*/
#define VSC_LDirectionSkyColor          VSC_LDirectionS               /*Directional Sky light parameters*/
#define VSC_LDirectionSetColorColor     VSC_LDirectionTransformedDir  /*Directional setColor light parameters*/
#define VSC_ShadowRampMulAdd            VSC_LDirectionTransformedDir  /*used in postprocess effect - shared location with other constant above*/


//! Maximum number of craters (beware when changing this value - engine depends on valur 14! (14 crater pixel shaders,...)
#define MAX_CRATERS 14

#define PSBLUR_MAX_KERNEL_SIZE 11

/// size of simul weather vertex shader parameters
#define SIMUL_WEATHER_PS_PARS_SIZE 8

/// size of caustics pars
#define CAUSTICS_PARS_SIZE 3
/// size of refraction pars
#define PS_REFRACT_PARS_SIZE 3

#define PSC_LIST(XXS, XXC, XXA,XXBEG,XXEND) \
  XXBEG(PSCB_NonFrequent, 0, false) \
  XXS(0, float4, PSC_ShadingDetail,                    0) /*Video options Shading detail setting is Low, Normal, High, Very High (or better) */\
  XXS(0, float4, PSC_TerrainSatNormDist,               1) /*Distances for blending of satellite normal texture with detail normal texture*/\
  XXS(0, float4, PSC_FogColor,                         2) /*Fog color*/\
  XXS(0, float4, PSC_WLight,                           3) /*Main light direction in world coordinates*/\
  XXS(0, float4, PSC_DepthClip,                       4) /*W stores the value of the z-clipping plane for depth buffer rendering*/\
  XXS(0, float4, PSC_HDRMultiply,                     5) /*x: amount of glow*/\
  XXS(0, float4, PSC_InvW_InvH_X_X,                   6) /*Inverse of backbuffers W and H*/\
  XXS(0, float4, PSC_SBTSize_invSBTSize_X_AlphaMult,  7) /*Alpha multiplier - used for AlphaToCoverage to make the trees thickers*/\
  XXS(0, float4, PSC_WaterFogColor,                   8) /*underwater fog color*/\
  XXA(0, float4, PSC_SimulWeatherPars,                9, SIMUL_WEATHER_PS_PARS_SIZE )  /*Simul Weather pixel shader parameters*/\
  XXEND(PSCB_NonFrequent) \
  XXBEG(PSCB_Special1, 1, false) \
  XXS(1, float3, PSC_GroundReflColor,                 0)  /**/\
  XXS(1, float3, PSC_SkyReflColor,                    1)  /**/\
  XXS(1, float4, PSC_GlassEnvColor,                   2)  /**/\
  XXA(1, float4, PSC_PRTConstantDirect,               3,3) /*Constants used by the TreePRT shaders - direct lights*/\
  XXA(1, float4, PSC_PRTConstantIndirect,             6,3) /*Constants used by the TreePRT shaders - indirect lights*/\
  XXEND(PSCB_Special1) \
  XXBEG(PSCB_Special2, 2, false) \
  XXA(2, float4, PSC_SkinEffectPars,                      0, 4 )  /*skin shader specific params*/\
  XXA(2, float4, PSC_CausticsPars,                        4, CAUSTICS_PARS_SIZE) /*Params for caustics */ \
  XXA(2, float4, PSC_RefractPars,                         7, PS_REFRACT_PARS_SIZE )  /*params for refraction shader*/\
  XXEND(PSCB_Special2) \
  XXBEG(PSCB_Tree, 3, false) \
  XXS(3, float4, PSC_TreeAdjust,                          0) /*Various parameters to adjust tree rendering*/\
  XXA(3, float4, PSC_TreeAdvPars,                         1, 5 )  /*tree adv specific*/\
  XXEND(PSCB_Tree) \
  XXBEG(PSCB_VeryFrequent, 4, false) \
  XXS(4, float4, PSC_T0AvgColor,                      0)  /**/\
  XXEND(PSCB_VeryFrequent) \
  XXBEG(PSCB_Object1, 5, false) \
  XXS(5, float4, PSC_DiscretizeAlpha,                  0) /*If alpha should be discretized*/\
  XXS(5, float4, PSC_AE,                               1)  /* Color of light reflected by air - formerly known as ambient color - and Emmisive color*/\
  XXS(5, float3, PSC_GE,                               2)  /* Color of light reflected by ground and Emmisive color. Used by ordinary shaders, that means shader with hemispherical lighting*/\
  XXS(5, float4, PSC_DForced,                         3)  /**/\
  XXS(5, float4, PSC_Diffuse,                         4)  /**/\
  XXS(5, float4, PSC_LDirectionGround_DiffuseBack,    5)  /* Used by ordinary shaders, they don't use "through" light calculation */\
  XXS(5, float4, PSC_Specular,                        6)  /**/\
  XXEND(PSCB_Object1) \
  XXBEG(PSCB_Object2, 6, false) \
  XXS(6, float4, PSC_GlassMatSpecular,                0)  /**/\
  XXS(6, float4, PSC_MaxColor,                        1)  /**/\
  XXS(6, float4, PSC_Shadow_Factor_ZHalf,             2) /**/\
  XXS(6, float4, PSC_X_X_X_AlphaRef,                  3) /**/\
  XXS(6, float4, PSC_AlphaTest,                       4) /*Alpha test info*/\
  XXS(6, float4, PSC_NightEmissiveCoef,               5) /*Alpha test info*/\
  XXEND(PSCB_Object2) \
  XXBEG(PSCB_TI, 7, false) \
  XXS(7, float4, PSC_HTMin_HTMax_AFMax_MFMax,         0) /*TI properties - _VBS3_TI*/\
  XXS(7, float4, PSC_MFact_TBody_X_HSDiffCoef,        1) /*TI properties - _VBS3_TI*/\
  XXEND(PSCB_TI) \
  XXBEG(PSCB_Terrain, 8, false) \
  XXA(8, float4, PSC_Layers,                           0,6) /*Toggle individual layers on/off */\
  XXA(8, float4, PSC_NoTexSizeLog2,                    6,2) /* Height maps size for individual layers */\
  XXEND(PSCB_Terrain) \
  XXBEG(PSCB_Water, 9, false) \
  XXS(9, float3, PSC_WaveColor,                           0)  /* Used by water, water doesn't use the hemispherical lighting */\
  XXA(9, float4, PSC_CalmWaterPars1,                      1, CALMWATER_PARS_SIZE_PS1 )  /*calm water specific params block1*/\
  XXA(9, float4, PSC_CalmWaterPars2,                      13, CALMWATER_PARS_SIZE_PS2 )  /*calm water specific params block2*/\
  XXEND(PSCB_Water) \
  XXBEG(PSCB_Lights1, 10, false) \
  XXS(10, float4, PSC_PointLoopCount,                      0)    /*holds number of point lights, it is being used for static looping*/\
  XXS(10, float4, PSC_SpotLoopCount,                       1)    /*holds number of spot lights, it is being used for static looping*/\
  XXEND(PSCB_Lights1) \
  XXBEG(PSCB_Lights2, 11, true) \
  XXA(11, float4, PSC_LPSData,                             0, 4*MAX_LIGHTS)    /*holds number of spot lights, it is being used for static looping*/\
  XXEND(PSCB_Lights2) \
  XXBEG(PSCB_Craters, 12, false) \
  XXA(12, float4, PSC_Crater,                             0, MAX_CRATERS) /*Craters from this to 32*/\
  XXA(12, float4, PSC_CraterTemp,                         14, MAX_CRATERS) /*crater temperate*/\
  XXEND(PSCB_Craters) \
  XXBEG(PSCB_PostProcess, 13, false) \
  XXS(13, float4, PSC_QueryDot,                            0) \
  XXS(13, float4, PSC_RgbEyeCoef,                          1) /*/// RGB eye sensitivity, alpha not used*/\
  XXS(13, float4, PSC_NightControl,                        2) /*/// x: multiplier - absolute level adjustment for rod/cell B/W effect simulation /// y: addition - offset to allow disabling rods completely*/ \
  XXS(13, float4, PSC_UIPars,                              3) /*UIPars.x ... > 0 enable outline and define outline texel offset || UIPars.y ... 1/texSizeX || UIPars.z ... 1/texSizeY || UIPars.w ... intensity*/ \
  XXS(13, float4, PSC_RainAlpha,                           4) \
  XXS(13, float2, PSC_RainScreenRes,                       5) \
  XXA(13, float4, PSC_BlurKernel,                          6, PSBLUR_MAX_KERNEL_SIZE) \
  XXA(13, float4, PSC_SceneOffsets2,                       17, 4) \
  XXA(13, float4, PSC_SceneOffsets4,                       21, 4*4) \
  XXS(13, float4, PSC_BloomPars,                           37) \
  XXS(13, float4, PSC_BicubicSourceSize,                   38) /*// resampled/filtered texture size*/\
  XXS(13, float4, PSC_BicubicTexelSize,                    39) /*// size of texels in format (texelX, 0, 0, texelY)*/ \
  XXS(13, float4, PSC_BlurFactor,                          40) \
  XXS(13, float4, PSC_TempRangeMin01_TempRangeMax01_InvTempRangeDiff_TIMode, 41) \
  XXS(13, float,  PSC_InvWidth,                            42) \
  XXS(13, float,  PSC_InvHeight,                           43) \
  XXS(13, float4, PSC_FOV,                                 44) \
  XXS(13, float4x3, PSC_ShadowmapMatrix,                   45) \
  XXS(13, float4, PSC_MaxDepth,                            48) \
  XXS(13, float4, PSC_ShadowmapLayerBorder,                49) \
  XXS(13, float4, PSC_BlurRadius,                          50) /*// Blur radius for X and Y direction (values can be like this: float2(1/800 * 2, 1/600 * 2)), in z is blur factor and in w is screen edge blur factor */ \
  XXS(13, float3, PSC_PlaneXBlur,                          51) \
  XXS(13, float4, PSC_ssaoPixelOffset,                     52) /*// to convert to screenpos */ \
  XXS(13, float4, PSC_ssaoDistDivParams,                   53) /*// to help with distance division */ \
  XXS(13, float4, PSC_ssaoMaxMinDist,                      54) /*// distance constants */ \
  XXS(13, float4, PSC_ssaoParams,                          55) \
  XXS(13, float4, PSC_ssaoFogParams,                       56) \
  XXS(13, float4, PSC_ScreenNoise,                         57) /*to convert noise texture to pixel size */ \
  XXS(13, float4, PSC_FilmGrainParams,                     58) \
  XXS(13, float4, PSC_FilmGrainParams2,                    59) \
  XXS(13, float4, PSC_AssumedLuminancePars1,               60) /*time used to time-dependend eye adaptation*/\
  XXS(13, float4, PSC_AssumedLuminancePars2,               61) \
  XXS(13, float4, PSC_ProjConsts,                          62) /*projection data for aspect correction*/\
  XXS(13, float4, PSC_RadialBlurPars,                      63) /*// xy... blur scale, zw... blur offset*/ \
  XXS(13, float4, PSC_PPColorsPars,                        64) /*x ..brightness, y ...contrast, z ...offset ( default: 1, 1, 0 )*/ \
  XXS(13, float4, PSC_LerpColor,                           65) \
  XXS(13, float4, PSC_ColorizeColor,                       66) /*a ...saturation*/\
  XXS(13, float4, PSC_RGBWeights,                          67) /*0.299, 0.587, 0.114*/ \
  XXS(13, float4, PSC_ChromAbberPars,                      68) /*// xy... abberation power*/\
  XXS(13, float4, PSC_ClrInversPars,                       69) /*r,g,b ... color inversion value*/\
  XXS(13, float4, PSC_PPWDAberration,                      70) \
  XXA(13, float4, PSC_PPDynBlurWeights,                    71, 6) \
  XXS(13, float4, PSC_PPColorRadPars1,                     77) /* color correction radial params 1 */ \
  XXS(13, float4, PSC_PPColorRadPars2,                     78) /* color correction radial params 2 */ \
  XXS(13, float4, PSC_BlurKernelSize,                      79) /* size of kernel for gaussian blur */ \
  XXEND(PSCB_PostProcess) \

 
//!{ Special texture ID's
/*!
Several textures at the end of the stages are designed to keep textures across the whole
rendering process. Neither other textures, nor NULL can be put on their places during rendering.
*/
#define TEXID_FIRST 15        //! First stage dedicated to special textures
#define TEXID_SHADOWMAP 15    //! Shadow map texture
#define TEXID_TC 15           //! Temperature Tc table // _VBS3_TI
#define TEXID_TI 14           //! TI texture can share space with regular textures // _VBS3_TI
//!}

//! Value used in Point vertex shader for stars to influence the point size. Inverse value is used for other than star purposes (f.i. for point drawing) where 
//! exact size need to be specified
#define MAX_STAR_INTENSITY 0.0015

// Temperature range to be handled by our system
// _VBS3_TI
#define TempMin 0.0f
#define TempMax 60.0f
#define ContrastMax 30.0f //(TempMax - TempMin)*0.5
#define InvTempDiff 0.016666667 //1.0f / (TempMax - TempMin)

#define TIConversionDimensionW 256
#define TIConversionDimensionH 64

#define EncodeTemperatureToRGB(temperature, R, G, B) \
  float temperature03 = saturate(((temperature) - TempMin) * InvTempDiff) * 3.0f; \
  if (temperature03 < 1) \
  { \
  R = temperature03; \
  G = 0.0f; \
  B = 0.0f; \
  } \
  else if (temperature03 < 2) \
  { \
  R = 1.0f; \
  G = temperature03 - 1.0f; \
  B = 0.0f; \
  } \
  else \
  { \
  R = 1.0f; \
  G = 1.0f; \
  B = temperature03 - 2.0f; \
  }

//! Conversion from int (or int4 f.i.) to float in interval <0, 1> (corresponds to impicit conversion under DX9 from D3DCOLOR in VB to float in VS)
#define BYTETOFLOAT01(x) (x * (1.0f/255.0f))

//! Conversion from int (or int4 f.i.) to float in interval <0, 1> together with D3DCOLOR swizzling
#define BYTETOFLOAT01_BGRA2RGBA(color) float4(color.b , color.g , color.r, color.a) * ( 1.0f/255.0f)


#endif //_DX11_HLSL_COMMON_H
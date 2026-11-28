//! Register designed for retrieving the diffuse color from VS
#define TEXCOORD_AMBIENT TEXCOORD6
#define TEXCOORD_SPECULAR TEXCOORD7
#define TEXCOORD_DIFFUSE_SI TEXCOORD4
//! Register designed for retrieving the shadow map coordinates from VS
#define TEXCOORDSR TEXCOORD5

struct SNonTL
{
  half4 ambientColor             : TEXCOORD_AMBIENT;
  float4 diffuseMap              : TEXCOORD0;
};

// matches basic output from VSShaderPool with TexCoord0
// DX11 ready
struct SBasic
{
  float4 position           : SV_POSITION;
  half4 ambient             : TEXCOORD_AMBIENT;
  half4 specular_landShadow : TEXCOORD_SPECULAR;
  half4 diffuse             : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer      : TEXCOORDSR;
  half4 color0              : COLOR0;
  half4 fog                 : COLOR1;
  float4 diffuseMap         : TEXCOORD0;
};

// DX11 ready
struct SDetail
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 color0                 : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_DetailMap : TEXCOORD0;
  float4 diffuseMap1           : TEXCOORD1;
  float4 diffuseMap2           : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

struct SDetailSpecularAlpha
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow: TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 color0                 : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_DetailMap : TEXCOORD0;
  float4 diffuseMap1           : TEXCOORD1;
  float4 diffuseMap2           : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

// DX11 ready
struct SNormal
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 tVector3AndFog        : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap           : TEXCOORD0;
  float4 diffuseMap1           : TEXCOORD1;
  float4 diffuseMap2           : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

// DX11 ready
struct SGrass
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 tVector3AndFog        : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap           : TEXCOORD0;
  half4 lightingColor          : TEXCOORD1;
  float4 diffuseMap2           : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

// DX11 ready
struct SSpecularAlpha
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow: TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 tVector3AndFog        : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap           : TEXCOORD0;
  half4 lightingColor          : TEXCOORD1;
  float4 diffuseMap2           : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

// DX11 ready
struct SSpecularNormalMapGrass
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 lightLocal_landShadow  : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 tVector3AndFog        : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
};

// DX11 ready
struct SSpecularNormalMapThrough
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 lightLocal_landShadow  : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 color0                 : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tDiffuseAtten_AmbientAtten : TEXCOORD1;
};

// DX11 ready
struct SSpecularNormalMapSpecularThrough
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 lightLocal_landShadow  : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 color0                 : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tDiffuseAtten_AmbientAtten : TEXCOORD1;
  float4 texCoord_Dummy1       : TEXCOORD1;
  float4 texCoord_Dummy2       : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

// DX11 ready
struct STree
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 lightLocal_landShadow  : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 color0                 : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tShadowMap            : TEXCOORD1;
  float4 diffuseMap_Dummy      : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

// DX11 ready
struct STreeAdv
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 lightLocal_landShadow  : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 col0                   : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tc0                   : TEXCOORD0;
  float4 tc1                   : TEXCOORD1;
  float4 tc2                   : TEXCOORD2;
  float4 tc3                   : TEXCOORD3;
};

// structure for data transfer between tree adv shader parts
struct SPFTreeAdv
{
  //half4 tex0;
  //half4 tex1;
  ////
  //half3 indirectLight; // hemispheric and bidir light
  //half3 vertexLight; // sum of point and spot lights
  ////half3 indirectSum; // sumVertLight + indirectLight
  //half4 colorFinal; // rgb ...composed material color, a ...final alpha (input.ambientColor.a * tex0.a)
  ////half3 colorFinal2;
  //half3 directLight;
  //half3 specularLight;
  //half3 specularIndirect; // specularLightHemi.rgb + specularEnvLight
  //half fogCoef; // input.tPSFog.w
  half diffuseShadowCoef;
  //half shadow;
  half shadow;
  half4 colorFinal; // rgb ...composed material color, a ...final alpha (input.ambientColor.a * tex0.a)
};

// DX11 ready
struct STreePRT
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 lightLocal_landShadow  : TEXCOORD_SPECULAR;
  half4 tPRTOffsfet            : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 color0                : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_prtMap    : TEXCOORD0;
  float4 tMacroMap             : TEXCOORD1;
};

// DX11 ready
struct SDetailMacroAS
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 color0                : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_DetailMap : TEXCOORD0;
  float4 tMacroMap_ShadowMap   : TEXCOORD1;
  float4 texCoord_Dummy        : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

// DX11 ready
struct SDetailSpecularAlphaMacroAS
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow : TEXCOORD_SPECULAR;
  half4 diffuse                : TEXCOORD_DIFFUSE_SI;
  float4 tShadowBuffer         : TEXCOORDSR;
  float4 color0                : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_DetailMap : TEXCOORD0;
  float4 tMacroMap_ShadowMap   : TEXCOORD1;
  float4 texCoord_Dummy        : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

// DX11 ready
struct SNormalMap
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 texCoord_Dummy1       : TEXCOORD1;
  float4 texCoord_Dummy2       : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

// DX11 ready
struct SNormalMapDetailMacroASSpecularMap
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tDetailMap_MacroMap   : TEXCOORD1;
  float4 tShadowMap_SpecularMap : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

#define SNormalMapDetailMacroASSpecularDIMap SNormalMapDetailMacroASSpecularMap

// DX11 ready
struct SNormalMapDetailSpecularMap
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tDetailMap_SpecularMap: TEXCOORD1;
  float4 texCoord_dummy        : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

#define SNormalMapDetailSpecularDIMap SNormalMapDetailSpecularMap

// DX11 ready
struct SNormalMapMacroAS
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tMacroMap             : TEXCOORD1;
  float4 tShadowMap            : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

struct SNormalMapMacroASSpecularMap
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tMacroMap_ShadowMap   : TEXCOORD1;
  float4 tSpecularMap          : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

// DX11 ready
struct SNormalMapSpecularMap
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap : TEXCOORD0;
  float4 tSpecularMap          : TEXCOORD1;
  float4 texCoord_Dummy        : TEXCOORD2;
  half4 tHalfway               : TEXCOORD3;
};

#define SNormalMapMacroASSpecularDIMap SNormalMapMacroASSpecularMap
#define SNormalMapSpecularDIMap SNormalMapSpecularMap

// DX11 ready
struct SSpecularNormalMapDiffuse
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow: TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tColorMap_NormalMap   : TEXCOORD0;
  float4 tDetailMap            : TEXCOORD1;
};

// DX11 ready
struct SSpecularNormalMapDiffuseMacroAS
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow: TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tColorMap_NormalMap   : TEXCOORD0;
  float4 tDetailMap_MacroMap   : TEXCOORD1;
  float4 tShadowMap            : TEXCOORD2;
};

// DX11 ready
struct SSprite
{
  float4 vPos                     : SV_POSITION;
  half tPSFog                    : COLOR1;
  float4 tShadowBuffer            : TEXCOORDSR;
  half4 ambientColor              : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow  : TEXCOORD_SPECULAR;
  half4 diffuseColor              : TEXCOORD_DIFFUSE_SI;
  float4 tDiffuseMap              : TEXCOORD0;
  half4 modulateColor             : TEXCOORD2;
  float4 tPosition                : TEXCOORD3;
};

// DX11 ready
struct STerrain
{
  float4 vPos                    : SV_POSITION;
  half4 ambientColor             : TEXCOORD_AMBIENT; //6
  half4 specularColor            : TEXCOORD_SPECULAR; //7
  float4 tSatAndMask_GrassMap    : TEXCOORD4;
  float4 tShadowBuffer           : TEXCOORDSR; //5
  half3 tLightLocal              : COLOR0;
  half4 tPSFog                   : COLOR1;
  float4 tColorMap_NormalMap     : TEXCOORD0;
  float4 tDetailMap_SpecularMap  : TEXCOORD1;
  float4 grassAlpha              : TEXCOORD2;
  half4 tEyeLocal                : TEXCOORD3;
};

// DX11 ready
struct SRoad
{
  float4 vPos                    : SV_POSITION;
  float4 tShadowBuffer           : TEXCOORDSR; //5
  half4 ambientColor             : TEXCOORD_AMBIENT; //6
  half4 specularColor            : TEXCOORD_SPECULAR; //7
  half3 tLightLocal              : COLOR0;
  half4 tPSFog                   : COLOR1;
  float4 tColorMap_NormalMap     : TEXCOORD0;
  float4 tDetailMap_SpecularMap  : TEXCOORD1;
  float4 tNormalLocal            : TEXCOORD2;
  float4 tEyeLocal               : TEXCOORD3;
};

/*
VertSShore and SShore are for VS and PS, respectively
*/
// DX11 ready
struct VertSShore
{
  float4 VertexPos_SkyLerp       : TEXCOORD0;      
  float4 Wave_X_X_Height_Fog     : TEXCOORD1;
  float4 tNormalMap1_2           : TEXCOORD2;
  float4 tNormalMap3             : TEXCOORD3;
  float4 CoastColorProjZ         : TEXCOORD4;
};

// DX11 ready
struct SShore
{
  float4 vPos                    : SV_POSITION;    
  float4 VertexPos_SkyLerp       : TEXCOORD0;      
  float4 Wave_X_X_Height_Fog     : TEXCOORD1;
  float4 tNormalMap1_2           : TEXCOORD2;
  float4 tNormalMap3             : TEXCOORD3;
  float4 CoastColorProjZ         : TEXCOORD4; 
};



//#define WATER_REAL_REFRACTION

/*
for water and shore, the multiplicator of color by coast
*/
#ifdef WATER_REAL_REFRACTION
  #define CoastColorFactor  0.05
#else
  #define CoastColorFactor  0.25
#endif

// DX11 ready
struct SWater
{
  float4 vPos                    : SV_POSITION;
  float4 VertexNormalFog         : TEXCOORD0;
  float4 VertexPosSkyLerp        : TEXCOORD1;
  float4 tWaveMap                : TEXCOORD2;
  float4 tNormalMap1_2           : TEXCOORD3;
  float4 tNormalMap3_LERP        : TEXCOORD4;
  float4 ColorProjZ              : TEXCOORD5;
  float4 SkinnedPosition : COLOR0;
  
  #ifdef WATER_REAL_REFRACTION
    float3 DeepColor               : TEXCOORD6;
  #endif
 
  //must be
  float4 tShadowBuffer           : TEXCOORD7;
};


#define SShoreWet SShore

// DX11 ready
struct SGlass
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 specularColor_landShadow: TEXCOORD_SPECULAR;
  half4 dummy                  : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 color0                 : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap           : TEXCOORD0;
  float4 lwsPosition           : TEXCOORD1;
  half4 lwsNormal              : TEXCOORD2;
  float4 x_x_nDotU_nDotL       : TEXCOORD3;
};

struct SCrater
{
  float4 vPos                   : SV_POSITION;
  float2 tDiffuseMap            : TEXCOORD0;
  float4 originalPosition       : TEXCOORD_SPECULAR;
  half tPSFog                  : COLOR1;
  float4 tShadowBuffer          : TEXCOORDSR;
};

// DX11 ready
struct SSuper
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 stn2lws2               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 stn2lws0               : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap  : TEXCOORD0;
  float4 tDetailMap_MacroMap    : TEXCOORD1;
  float4 tShadowMap_SpecularMap : TEXCOORD2;
  half4 stn2lws1                : TEXCOORD3;
};

#define SSuperExt SSuper
#define SSuperAToC SSuper

//_ARMA3_REFRACTION
struct SRefract
{
  float4 vPos                   : SV_POSITION;
  centroid half4 ambientColor            : TEXCOORDAMBIENT;
  centroid half4 landShadow              : TEXCOORDSPECULAR;
  centroid half4 stn2lws2                : TEXCOORD4;
  centroid float4 tShadowBuffer          : TEXCOORDSR;
  centroid half4 stn2lws0                : COLOR0;
  centroid half4 tPSFog                  : COLOR1;
  float4 tDiffuseMap_NormalMap           : TEXCOORD0;
  float4 tSpecularMap                    : TEXCOORD1;
  float4 projectedPos                    : TEXCOORD2;
  centroid half4 stn2lws1                : TEXCOORD3;
};


// DX11 ready
struct SSkin
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 landShadow             : TEXCOORD_SPECULAR;
  half4 stn2lws2               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 stn2lws0               : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tDiffuseMap_NormalMap  : TEXCOORD0;
  float4 tDetailMap_MacroMap    : TEXCOORD1;
  float4 tShadowMap_SpecularMap : TEXCOORD2;
  half4 stn2lws1                : TEXCOORD3;
};

struct SGenericVs2Ps
{
   float4 pos : POSITION0;
   float4 tc0 : TEXCOORD0;
   float4 tc1 : TEXCOORD1;
   float4 tc2 : TEXCOORD2;
   float4 tc3 : TEXCOORD3;
   float4 tc4 : TEXCOORD4;
   float4 tc5 : TEXCOORD5;
   float4 tc6 : TEXCOORD6;
   float4 tc7 : TEXCOORD7;
};

struct SCalmWaterVs2Ps
{
   float4 tc0 : TEXCOORD0;
   float4 tc1 : TEXCOORD1;
   float4 tc2 : TEXCOORD2;
   float4 tc3 : TEXCOORD3;
   float4 tc4 : TEXCOORD4;
   float4 tc5 : TEXCOORD5;
   float4 tc6 : TEXCOORD6;
   float4 tc7 : TEXCOORD7;
};

// struct for data transfer between pixel shader fragments
struct SCalmWaterPFT
{
  half shadow; // shadow coef
  half2 hMapA01; // sampler2 .ga
};

// structure for data transfer from PFSkin1() to PFSkinFinColor()
struct SPFSkinOut
{
  
  half3 indirectLight; // hemispheric and bidir light
  half3 vertexLight; // sum of point and spot lights
  //half3 indirectSum; // sumVertLight + indirectLight
  half4 colorFinal; // rgb ...composed material color, a ...final alpha (input.ambientColor.a * tex0.a)
  //half3 colorFinal2;
  half3 directLight;
  half3 specularLight;
  half3 specularIndirect; // specularLightHemi.rgb + specularEnvLight
  half fogCoef; // input.tPSFog
  half diffuseShadowCoef;
};

// DX11 ready
struct SMulti
{
  float4 vPos                  : SV_POSITION;
  half4 ambientColor           : TEXCOORD_AMBIENT;
  half4 halfway_landShadow     : TEXCOORD_SPECULAR;
  half4 tUpLocal               : TEXCOORD4;
  float4 tShadowBuffer         : TEXCOORDSR;
  half4 tLightLocal            : COLOR0;
  half4 tPSFog                 : COLOR1;
  float4 tCO0_CO1              : TEXCOORD0;
  float4 tCO2_CO3              : TEXCOORD1;
  float4 tMSK_DT0              : TEXCOORD2;
  float4 tDT1_DT2              : TEXCOORD3;
};

struct VSPP_OUTPUT
{
  float4 Position   : SV_POSITION;
  float2 TexCoord   : TEXCOORD;
};
struct VSPP_OUTPUT_SHADOWS
{
  float4 Position   : SV_POSITION;
  float2 TexCoord   : TEXCOORD0;
  float4 Color      : COLOR0;
};
struct VSPP4T_OUTPUT
{
  float4 Position   : SV_POSITION;
  float4 TexCoord0  : TEXCOORD0;
  float4 TexCoord1  : TEXCOORD1;
  float4 TexCoord2  : TEXCOORD2;
  float4 TexCoord3  : TEXCOORD3;
};

struct VS_SSAO 
{
  float4 m_vPos 			: SV_POSITION;
  float4 m_vTexCoord	: TEXCOORD0;
};

struct VS_SSAODown
{
  float4 m_vPos 			: SV_POSITION;
  float4 m_vTex1Tex2	: TEXCOORD0;
  float4 m_vTex3Tex4	: TEXCOORD1;
};

//==================================================================================================

struct VSPSRain
{
  float4	vPos: POSITION;
  float4	vTexCoordDistDepth: TEXCOORD0;
};

struct VSPP_OUT
{
  float4 pos : POSITION;
  float4 tc0 : TEXCOORD0;
  float4 tc1 : TEXCOORD1;
  float4 tc2 : TEXCOORD2;
  float4 tc3 : TEXCOORD3;
  float4 tc4 : TEXCOORD4;
  float4 tc5 : TEXCOORD5;
  float4 tc6 : TEXCOORD6;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

// vertex output / ps input of SimulWeather cloud rendering
struct SWCloudsVertexOutput
{
  float4 hPosition			: SV_POSITION;
  float2 texCoordsNoise		: TEXCOORD0;
  float2 layerFade_Fog				: TEXCOORD1;
  float4 texCoords			: TEXCOORD2;
  float3 wPosition			: TEXCOORD3;
  float3 texCoordLightning	: TEXCOORD4;
  float3 lightColour			: TEXCOORD5;
#if FADE_MODE==1
  float3 fade_texc			: TEXCOORD6;
#endif
#if FADE_MODE==0
  float3 loss					: TEXCOORD6;
  float3 inscatter			: TEXCOORD7;
#endif
};


//==================================================================================================
// sea wave function; computes 2 waves + 2 derivatives; ret.xy ..wave heigths, ret.zw ..deriv. output; p.xy = <-1;1>
half4 WaveFc1A( half2 p )
{
  return half4( p - p * p * p, 1.0h - 3.0h * p * p );
  //return half4( p - p * p * p * p * p, 1.0h - 5.0h * p * p * p * p );
}

// sea wave function; computes 4 waves; ret.xyzw ..wave heigths; p.xyzw = <-1;1>
half4 WaveFc1B( half4 p )
{
  return half4( p - p * p * p );
}

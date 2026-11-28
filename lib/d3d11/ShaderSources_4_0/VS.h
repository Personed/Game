#include "common.h"
#include "VSConstants.h"
#include "psvs.h"

#ifdef _XBOX
# define ISOLATE [isolate]
#else
# define ISOLATE
#endif

#define LPOINT_TransformedPos(index) VSC_LPSData[index*4 + 0]
#define LPOINT_Atten(index)          VSC_LPSData[index*4 + 1]
#define LPOINT_D(index)              VSC_LPSData[index*4 + 2]
#define LPOINT_A(index)              VSC_LPSData[index*4 + 3]

#define LSPOT_TransformedPos_CosPhiHalf(index)   VSC_LPSData[index*4 + 0]
#define LSPOT_TransformedDir_CosThetaHalf(index) VSC_LPSData[index*4 + 1]
#define LSPOT_D_InvCTHMCPH(index)                VSC_LPSData[index*4 + 2]
#define LSPOT_A_Atten(index)                     VSC_LPSData[index*4 + 3]


// An alternative to the intrinsic function D3DCOLORtoUBYTE4, but result values are from range <0, 1>
float4 D3DCOLORtoFLOAT4(float4 value)
{
  return float4(value.z, value.y, value.x, value.w);
}

float4 Multiply4x3OneVector(float4 a, float4 b0, float4 b1, float4 b2)
{
  return a.x * b0 + a.y * b1 + a.z * b2 + a.w * float4(0, 0, 0, 1);
}

void DecompressVector(inout float3 v)
{
  #ifndef _XBOX
  // on Xbox we are using D3DDECLTYPE_DEC3N, no range adjustment needed
  v = v * 2.0f - 1.0f; // values were read as D3DCOLOR, convert 0..1 range into -1..1 range
  #endif
}

void GetModelSTN(float3 S, float3 T, float3 skinnedNormal, float3 sm0, float3 sm1, float3 sm2, out float3 modelS, out float3 modelT, out float3 modelN)
{
  modelS.x = dot(-S, sm0);
  modelS.y = dot(-S, sm1);
  modelS.z = dot(-S, sm2);
  modelS = normalize(modelS);
  modelT.x = dot(-T, sm0);
  modelT.y = dot(-T, sm1);
  modelT.z = dot(-T, sm2);
  modelT = normalize(modelT);
  modelN = skinnedNormal;
}

void CalculateLocalLightAndHalfway(float3 S, float3 T, float3 skinnedPosition, float3 skinnedNormal, float3 sm0, float3 sm1, float3 sm2,
                                   out float3 lightLocal, out float3 halfwayLocal, out float3 eyeLocal, out float3 halfwayReflectLocal, out float3 upLocal)
{
  // Prepare the STN space matrix
  float3 modelS;
  float3 modelT;
  float3 modelN;
  GetModelSTN(S, T, skinnedNormal, sm0, sm1, sm2, modelS, modelT, modelN);

  // Calculate the local light
  lightLocal.x = dot(modelS, -VSC_LDirectionTransformedDir.xyz);
  lightLocal.y = dot(modelT, -VSC_LDirectionTransformedDir.xyz);
  lightLocal.z = dot(modelN, -VSC_LDirectionTransformedDir.xyz);

  // Calculate the local eye vector  
  float3 camDirection = VSC_CameraPosition.xyz - skinnedPosition;
  eyeLocal.x = dot(modelS, camDirection);
  eyeLocal.y = dot(modelT, camDirection);
  eyeLocal.z = dot(modelN, camDirection);

  // Calculate the local halfway vector  
  halfwayLocal = normalize(normalize(eyeLocal) + lightLocal);

  // Calculate the local halfway reflect vector
  float3 up = {0, 1, 0};
  float3 halfwayReflect = normalize(normalize(camDirection) + up);
  halfwayReflectLocal.x = dot(modelS, halfwayReflect);
  halfwayReflectLocal.y = dot(modelT, halfwayReflect);
  halfwayReflectLocal.z = dot(modelN, halfwayReflect);

  // Calculate the local UP vector
  upLocal.x = dot(modelS, VSC_UDirectionTransformedDir.xyz);
  upLocal.y = dot(modelT, VSC_UDirectionTransformedDir.xyz);
  upLocal.z = dot(modelN, VSC_UDirectionTransformedDir.xyz);
}

float Haze(float dist)
{
  // ln(1/512)/maxDist  ... maxDist = 10000
  //float hazeCoef = -0.00062383;
  float haze = exp(dist*VSC_Free_ExpFog_AFogEnd_RCPAFogEndMinusAFogStart.y);
  return haze;
}

void TexCoordTransform(in float3 vTexCoord0,
                       in float3 vTexCoord1,
                       in int count,
                       inout float4 vOutTexCoord[4])
{
  // Zero all texture coordinates - this should be removed by optimizer in case of even count of texcoords
  //for (int i = 0; i < 4; i++) vOutTexCoord[i] = float4(0, 0, 0, 0);

  // Cycle for all required texcoords
#if _SM2
  for (int i = 0; i < count; i++)
#else
  [unroll] for (int i = 0; i < count; i++)
#endif
  {
    // Get the texture coordinate XY indices within XYZW vector
    int xItem;
    int yItem;
    if ((i%2) == 0)
    {
      xItem = 0;
      yItem = 1;
    }
    else
    {
      xItem = 3;
      yItem = 2;
    };

    // Get the out vector index (each one vOutTexCoord holds 2 texture coordinates)
    int halfFloor = i / 2;

    // Get the type of the texcoord
    float tid = VSC_TexCoordType[i / 4][i % 4];

    // Perform one of the transformations
    if (tid < 0.5f)
    {
      vTexCoord0.z = 1;
      vOutTexCoord[halfFloor][xItem] = dot(vTexCoord0, VSC_TexTransform[i]._m00_m10_m30);
      vOutTexCoord[halfFloor][yItem] = dot(vTexCoord0, VSC_TexTransform[i]._m01_m11_m31);
    }
    else
    {
      vTexCoord1.z = 1;
      vOutTexCoord[halfFloor][xItem] = dot(vTexCoord1, VSC_TexTransform[i]._m00_m10_m30);
      vOutTexCoord[halfFloor][yItem] = dot(vTexCoord1, VSC_TexTransform[i]._m01_m11_m31);
    }
  }
}

// Structure to hold outputs from transform functions
struct TransformOutput
{
  float4 position;
  float4 skinnedMatrix0;
  float4 skinnedMatrix1;
  float4 skinnedMatrix2;
  float4 skinnedPosition;
  float3 skinnedNormal;
  float4 transformedPosition;
  float4 instanceColor;
  float instanceShadow;
  float4 projectedPosition;
  float4 worldPosition;
  bool normalAligned;
};

// Structure to hold light values to be passed to PS in the end
struct AccomLights
{
  float4 ambient;
  float4 specular;
  float4 diffuse;

  // Parameters below are used to influence computation of diffuse lighting from point and spot light sources (that is light sources other then main light).
  // Diffuse lighting coefficient is computed as:
  //
  // tempCoef = (dot(normal, lightDir) + diffuseLightCoefAdd) * diffuseLightCoefMul;
  // resultCoef = max(diffuseLightMinCoef, tempCoef);
  // 
  // This can be useful for special shaders (like TreeAdv), where we want to compute diffuse lighting in a special way
  float diffuseLightMinCoef; // 0 by default
  float diffuseLightCoefAdd; // 0 by default
  float diffuseLightCoefMul; // 1 by default
};

void VertexInitLights(in float alpha, in float instanceShadow, inout AccomLights al)
{
  // Fill out values
  al.ambient.xyz = float3(0, 0, 0);
  al.ambient.w = VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.y * alpha;
  al.specular = float4(0, 0, 0, instanceShadow);
  al.diffuse = float4(0, 0, 0, 1);
  al.diffuseLightMinCoef = 0.0f;
  al.diffuseLightCoefAdd = 0.0f;
  al.diffuseLightCoefMul = 1.0f;
}

void VertexInitLightsSimple(inout AccomLights al)
{
  // Fill out values
  al.ambient = float4(0, 0, 0, 1);
  al.specular = float4(0, 0, 0, 1);
  al.diffuse = float4(0, 0, 0, 1);
  al.diffuseLightMinCoef = 0.0f;
  al.diffuseLightCoefAdd = 0.0f;
  al.diffuseLightCoefMul = 1.0f;
}

void VDoneLights(in AccomLights al, out float4 vOutAmbient, out float4 vOutSpecular, out float4 vOutDiffuse)
{
  // Copy the colors to output
  vOutAmbient = al.ambient;
  vOutSpecular = al.specular;
  vOutDiffuse = al.diffuse;
}

void VDoneLightsSimple(in AccomLights al, out float4 vOutAmbient, out float4 vOutSpecular)
{
  // Copy the colors to output
  vOutAmbient = al.ambient;
  vOutSpecular = al.specular;
}

void VDoneLightsTerrain(in AccomLights al, in float3 bedColor, out float4 vOutAmbient, out float4 vOutSpecular)
{
  // Copy the colors to output
  vOutAmbient = float4(al.ambient.rgb * bedColor, al.ambient.a);
  vOutSpecular = al.specular;
}

// with shader model 3 we need to implement fog in shaders
void VFog(in TransformOutput to, out float vFog)
{
  float d = sqrt(dot(to.transformedPosition.xyz, to.transformedPosition.xyz));
  float haze = Haze(d);
  vFog = haze*saturate((VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.z - d) * VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.w);
}

void VFogAlpha(in TransformOutput to, inout AccomLights al, out float vFog)
{
  float d = sqrt(dot(to.transformedPosition.xyz, to.transformedPosition.xyz));
  float haze = Haze(d);
  float coef = haze * saturate((VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.z - d) * VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.w);
  al.ambient.w = saturate(al.ambient.w * coef);
  vFog = 1;
}

void VFogFogAlpha(in TransformOutput to, inout AccomLights al, out float vFog)
{
  float d = sqrt(dot(to.transformedPosition.xyz, to.transformedPosition.xyz));
  float haze = Haze(d);
  //float haze = d/1000;
  vFog = haze*saturate((VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.z - d) * VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.w);
  al.ambient.w = saturate(al.ambient.w * (VSC_Free_ExpFog_AFogEnd_RCPAFogEndMinusAFogStart.z - d) * VSC_Free_ExpFog_AFogEnd_RCPAFogEndMinusAFogStart.w);
}

void VFogNone(out float vFog)
{
  vFog = 1;
}

//! Rendering model enumeration (note, that this encapsulates rendering mode and crater rendering of the engine together)
/// common - SB space shadows
#define RMCommon        0
/// SB rendering
#define RMShadowBuffer  1
/// z priming
#define RMZPrime        2
/// depth priming
#define RMDPrime        3
/// crater rendering
#define RMCrater        4 // Care must be taken when changing this number (see ::DoSetupSectionTL function)
/// caustic rendering
#define RMCaustics      5

 
void VShadowReceiver(in TransformOutput to, out float4 oShadowMap)
{
  if (VSC_ShadowReceiverSSSM)
  {
    oShadowMap = to.projectedPosition;
  }
  else
  {
    // Write shadow map UV transformation
    oShadowMap.xyz = mul(to.transformedPosition, VSC_ShadowmapMatrix);
    oShadowMap.w = to.transformedPosition.z;
  }
}

// for explanation of diffuseLight coefs see AccomLights 
void VLPointSpotNAmbient(in TransformOutput to, inout float4 ambient,
                         float diffuseLightMinCoef = 0, float diffuseLightCoefAdd = 0, float diffuseLightCoefMul=1)
{
  // Color accumulator
  float3 sumColor = float3(0, 0, 0);

  // Saturation value
#if _VBS3_PERPIXELPSLIGHTS
  const float saturationCoef = 3;
#else
  const float saturationCoef = 0.3;
#endif

#if !(_DEBUG>1) // with /Od compiler requests impossible VSC_PointLoopCount binding
  // Point lights
  for (int i = 0; i < VSC_PointLoopCount.x; i++)
  {
    float3 pointToLightVector = LPOINT_TransformedPos(i).xyz - to.skinnedPosition.xyz;
    float lightSquareDistance = dot(pointToLightVector, pointToLightVector);
    float invLightDistance = rsqrt(lightSquareDistance);
    float3 lightDirection = pointToLightVector * invLightDistance;
    float attenuation = LPOINT_Atten(i).x * invLightDistance * invLightDistance;
    float diffuseCoef = (dot(to.skinnedNormal, lightDirection) + diffuseLightCoefAdd) * diffuseLightCoefMul;
    sumColor += (max(diffuseLightMinCoef, diffuseCoef) * LPOINT_D(i).xyz + LPOINT_A(i).xyz) * min(saturationCoef, attenuation);
  }

  // Spot lights
  for (int ii = 0; ii < VSC_SpotLoopCount.x; ii++)
  {
    int i = MAX_LIGHTS - ii - 1;
    float3 pointToLightVector =LSPOT_TransformedPos_CosPhiHalf(i).xyz - to.skinnedPosition.xyz;
    float lightSquareDistance = dot(pointToLightVector, pointToLightVector);
    float invLightDistance = rsqrt(lightSquareDistance);
    float3 lightDirection = pointToLightVector * invLightDistance;
    float rho = dot(-LSPOT_TransformedDir_CosThetaHalf(i).xyz, lightDirection);
    float spotlightFactor = (rho > LSPOT_TransformedDir_CosThetaHalf(i).w) ? 1 : (rho <= LSPOT_TransformedPos_CosPhiHalf(i).w) ? 0 : (rho - LSPOT_TransformedPos_CosPhiHalf(i).w) * LSPOT_D_InvCTHMCPH(i).w;
    float attenuation = LSPOT_A_Atten(i).w * invLightDistance * invLightDistance;
    float diffuseCoef = (dot(to.skinnedNormal, lightDirection) + diffuseLightCoefAdd) * diffuseLightCoefMul;
    sumColor += (max(diffuseLightMinCoef, diffuseCoef) * LSPOT_D_InvCTHMCPH(i).xyz + LSPOT_A_Atten(i).xyz) * min(saturationCoef, attenuation * spotlightFactor);
  }
#endif
  // Increment the ambient light
  ambient.xyz += sumColor;
}

void VLPointSpotN(in TransformOutput to, inout AccomLights al)
{
  VLPointSpotNAmbient(to, al.ambient, al.diffuseLightMinCoef, al.diffuseLightCoefAdd, al.diffuseLightCoefMul);
}

#if _VBS3_MORETERRAINLODS_SHADER
TransformOutput VTransformLodEx(in float4 vPosition, in float3 vNormal,
                                in float3 lod,
                                in float4 lod36,
                                out float2 grassAlphaOut,
                                uniform bool grass)
#else
TransformOutput VTransformLodEx(in float4 vPosition, in float3 vNormal,
                                in float3 lod,
                                out float2 grassAlphaOut,
                                uniform bool grass)
#endif
{
  // Define output
  TransformOutput to;

  // Original position
  to.position = vPosition;

  // Skinned matrix
  to.skinnedMatrix0 = float4(1, 0, 0, 0);
  to.skinnedMatrix1 = float4(0, 1, 0, 0);
  to.skinnedMatrix2 = float4(0, 0, 1, 0);

  // Skinned position and normal
  to.skinnedPosition = vPosition;
  to.skinnedNormal = vNormal;

  { // adjust vertex position based on terrain LOD
    float3 lodPos = to.skinnedPosition;
    //float3 relPos = VSC_CameraPosition.xyz-skinnedPosition.xyz;
    float2 relPos = VSC_CameraPosition.xz-to.skinnedPosition.xz;
    float distance2 = dot(relPos,relPos);

    float k = VSC_TerrainLODPars.x;
    float maxLod = VSC_TerrainLODPars.y;

    float logDistance = log2(distance2);
    float lodWanted = maxLod + k + logDistance*0.5;

    lodWanted = min(maxLod,max(lodWanted,0));

    // note: same code could handle 4 values, however we need the z component for other purposes
    // Sahrani does not support 4 lods anyway

    // array of 3 lod values
    float3 yArray = float3(to.skinnedPosition.y,lod.xy);


    // calculate selector coefficents for all four values
    // for each of the 4 points calculate distance from its index
    // weight is max(1-distance,0)
    float3 select = max(1-abs(lodWanted-float3(0,1,2)),0);

#if _VBS3_MORETERRAINLODS_SHADER
    float4 yArray36 = lod36;
    float4 select36 = max(1-abs(lodWanted-float4(3,4,5,6)),0);
    // lod.z contains height of the grass level
#endif

    // lod.z contains height of the grass level

    // first we can try is simply adding it
    // we need distance based attenuation
    // the distance should be matched to ground clutter rendering

#if _VBS3_MORETERRAINLODS_SHADER
    to.skinnedPosition.y = dot(yArray,select) + dot(yArray36,select36);
#else
    to.skinnedPosition.y = dot(yArray,select);
#endif

    if (grass)
    {
      float grassCoef = min(1,(sqrt(distance2)-15)*(1.0/15));
      // note: it is possible to include following line in the dot product above
      // however real gain is unlikely (mad will be used here) and this is much better readable
      // we subtract a small epsilon to be sure we end under the ground when close
      //skinnedPosition.y += lod.z*grassCoef - (1-grassCoef)*0.1;
      to.skinnedPosition.y += lod.z*grassCoef;
    }
  }

  // Transformed position
  to.transformedPosition = float4(mul(to.skinnedPosition, VSC_ViewMatrix), 1);

  if (grass)
  {
    float d = sqrt(dot(to.transformedPosition.xyz, to.transformedPosition.xyz));

    float3 grassAlpha = saturate((VSC_TerrainAlphaAdd - d) * VSC_TerrainAlphaMul);

    grassAlphaOut.x = grassAlpha.y*(1-grassAlpha.x); // disappering (both close and far)
    grassAlphaOut.y = grassAlpha.z; // color lerp (detail, satellite)
  }
  else
  {
    // not used, but compiler requires us to assign a value
    grassAlphaOut = 0;
  }

  // Instance shadow
  to.instanceShadow = 1;
  to.instanceColor = float4(1, 1, 1, 1);

  // Project the position
  to.projectedPosition = mul(VSC_ProjMatrix, to.transformedPosition);

  // World position
  to.worldPosition = mul(to.skinnedPosition, VSC_WorldMatrix);

  // Normal orientation was not changed
  to.normalAligned = false;

  // Return output
  return to;
}

// Modify water normal - see https://wiki.bistudio.com/index.php/Analysis_-_Kreslen%C3%AD_vody_%28Water_Rendering%29#Modifikace_norm.C3.A1ly
float3 ModifyWaterNormal(float3 normal, float3 camDirection, float modifCoef)
{
  float cosViewAngle = max(dot(float3(0, 1, 0), camDirection), 0);
  float normalLeanCoef = (log(0.01 + cosViewAngle) - log(0.01)) * (1 - cosViewAngle) * 0.2;
  return normalize(normal + camDirection * normalLeanCoef * modifCoef);
}

// Fresnel reflection is fully performed in PS. Here we're doing something similar:
// We influence the transparency of the shape according to viewing angle (if we're looking
// in a sharp angle, the water is not transparent)
float GetWaterViewA(float3 normal, float3 camDirection)
{
  // calculate alpha factor based on viewing angle
  float viewAngle = dot(camDirection, normal);

  const float minCosA = 0.17f; // cos 80 deg - full reflection, no transparency
  const float maxCosA = 0.86f; // cos 30 deg - maximum transparency

  // x1 = x0 *M + A
  // x0 = minCosA -> x1 = 0
  // x0 = maxCosA -> x1 = 1
  const float waterReflectAdd = -minCosA/(maxCosA-minCosA);
  const float waterReflectMul = 1/(maxCosA-minCosA);

  // Calculate the alpha
  return saturate(viewAngle * waterReflectMul + waterReflectAdd);
}



//------------------------------------------------------------------------------------
/*
texcoords for water
*/
//------------------------------------------------------------------------------------
void ComputeTexCoords(float2 uvpos, out float2 tex1, out float2 tex2, out float2 tex3, float time)
{
  const float speed1 = 0.004;
  const float speed2 = 0.003;
  const float speed3 = 0.002;

  const float size1  = 1.0/500.0f;
  const float size2  = 1.0/50.0f;
  const float size3  = 1.0/15.0f;
  
  tex1  = uvpos.xy*size1  + time*speed1;
  tex2  = uvpos.xy*size2  + time*speed2;
  tex3  = uvpos.xy*size3  + time*speed3;
    
//  tex1 = float2(dot(uvpos, VSC_TexTransform[1]._m00_m10_m30), dot(uvpos, VSC_TexTransform[1]._m01_m11_m31))*1.0;
//  tex2 = float2(dot(uvpos, VSC_TexTransform[2]._m00_m10_m30), dot(uvpos, VSC_TexTransform[2]._m01_m11_m31))*4.0;
//  tex3 = float2(dot(uvpos, VSC_TexTransform[3]._m00_m10_m30), dot(uvpos, VSC_TexTransform[3]._m01_m11_m31))*8.0;
}

//------------------------------------------------------------------------------------
/*
lerp distance
*/
//------------------------------------------------------------------------------------
float GetDistanceLERP(float dist, const float mindist, const float maxdist)
{
  //line equation
  const float K       = 1.0/(mindist - maxdist);
  const float Q       = -maxdist*K;

  return 1 - saturate(dist*K + Q);  
}

/// caustics
struct CausticsOut
{
  float4 oWorldPosition_LDot                : TEXCOORD6;
  float4 oTransformedPosition_Fog           : TEXCOORD7;
  float3 oCausticTexCoord_startDepth        : TEXCOORD5;
};

void FillCausticsOut(TransformOutput to, out CausticsOut cout, float depthStart = 0.0f)
{
  cout.oWorldPosition_LDot.xyz = to.worldPosition.xyz;
  cout.oWorldPosition_LDot.w = dot(to.skinnedNormal.xyz, -VSC_LDirectionTransformedDir.xyz);
  cout.oTransformedPosition_Fog.xyz = to.transformedPosition.xyz;
  VFog(to, cout.oTransformedPosition_Fog.w);
  cout.oCausticTexCoord_startDepth.xy = mul(to.worldPosition, VSC_CausticsMatrix).xy;
  cout.oCausticTexCoord_startDepth.z = depthStart;
}
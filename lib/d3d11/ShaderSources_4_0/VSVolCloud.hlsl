#include "VS.h"

// structure representing info about one instance of cloud puff in VSC_SkinningInstancingData
//struct CloudInstancingItem
//{
//  // xyz = position of cloud puff (relative to camera -> it is puffWorldPosition - cameraWorldPosition), w = scale
//  float4 position;
//  // xyz = center of the whole cloud (relative to camera), w = height of the cloud
//  float4 cloudCenter_height;
//  // xy = uv start, z = uv size, w = angle    
//  float4 mapping_angle;
//};

// vertex shader used to render cloud puffs (volumetric clouds)
void VSVolCloud(
  in uint4 i_UV_Index            : TEXCOORD,
  out float4 oProjectedPosition   : POSITION,
  out float oOutFog               : COLOR1,
  out float4 oShadowMap           : TEXCOORDSR,
  out float4 oOutAmbient          : TEXCOORD_AMBIENT,
  out float4 oOutSpecular         : TEXCOORD_SPECULAR,
  out float4 oOutDiffuse          : TEXCOORD_DIFFUSE_SI,
  out float4 vOutTexCoord01       : TEXCOORD0,
  out float4 vOutModulateColor    : TEXCOORD2,
  out float4 oPosition            : TEXCOORD3)
{
  // Do the vertex transformations
  TransformOutput to;

  // uv coordinates of the vertex (we use them also to determine which of the 4 vertices we are currently drawing)
  float2 uv = BYTETOFLOAT01(i_UV_Index).xy;
  // index of instance in VSC_SkinningInstancingData
  int index = i_UV_Index.z * CLOUD_INSTANCING_ITEM_SIZE;
  
  to.position = VSC_SkinningInstancingData[index + 0];  
  to.skinnedMatrix0 = float4(1, 0, 0, 0);
  to.skinnedMatrix1 = float4(0, 1, 0, 0);
  to.skinnedMatrix2 = float4(0, 0, 1, 0);

  float4 mapping_angle = VSC_SkinningInstancingData[index + 2];

  to.skinnedPosition.xyz = to.position.xyz;
  to.skinnedPosition.w = 1;
  to.skinnedNormal = normalize(VSC_CameraPosition.xyz - to.skinnedPosition.xyz);
  float scale = to.position.w;

  // Calculate diameter of the particle (no size limitation)
  float particleDiameter = abs(scale);

  // first of all calculate distance from camera, we need it for size limitation
  to.transformedPosition.z = mul(to.skinnedPosition, VSC_ViewMatrix).z;

  // Make the particle Z to be closer by the particle radius to the camera (consider the particle is of a sphere shape - it is
  // not correct for the particle to completely disappear when it's center reaches the some occlusion plane)
  // Note it is necessary to do that prior the size limit, because this modification influences final size (by perspective) and particle
  // size control wouldn't work as expected
  //to.transformedPosition.z -= particleDiameter * 0.5f;

  // calculate field of view as 1/sqrt(xScale*yScale)
  // float fov = rsqrt(VSC_ProjMatrix._m00*VSC_ProjMatrix._m11);
  // limit screen space size to avoid extreme fillrate load when close
  // float maxLocalPos = 0.75*fov*to.transformedPosition.z;
  // Make the limit much more benevolent if the particle is marked as no-limit (scale is negative)
  // if (scale < 0) maxLocalPos = maxLocalPos * 1000.0f;
  // float2 localPosition = min(particleDiameter, maxLocalPos) * (uv.xy - 0.5);
    
  float2 localPosition = particleDiameter * (uv.xy - 0.5);
    
  float globalAlpha = mapping_angle.w;
  float angle = 0;
  //float angle = mapping_angle.w ;
  
  // animate (rotate) the particle
  float2 sc; sincos(angle, sc.x, sc.y);
  float2 localRotatedPosition;
  localRotatedPosition.x = dot(float2(sc.y, -sc.x), localPosition);
  localRotatedPosition.y = dot(sc, localPosition);
  // fill the rest of the transformedPosition now
  to.transformedPosition.xy = mul(to.skinnedPosition, VSC_ViewMatrix).xy+localRotatedPosition;
  to.transformedPosition.w = 1;
  // project into the homogenous view space
  to.projectedPosition = mul(VSC_ProjMatrix, to.transformedPosition);

    // World position
  to.worldPosition = mul(to.skinnedPosition, VSC_WorldMatrix);

  // compute real uv coordinates 
  float2 uvScreen = float2(uv.x, 1 - uv.y);
  vOutTexCoord01.xy = uvScreen * mapping_angle.zz + mapping_angle.xy;
  vOutTexCoord01.zw = float2(0, 0);

  // Instance shadow
  to.instanceColor = VSC_InstanceColor;
  to.instanceShadow = VSC_InstanceLandShadowIntensity.x;

  // Normal orientation was not changed
  to.normalAligned = false;

  // LUKE TEST
  float4 deltaPos = float4(particleDiameter * (uv.xy - 0.5), 0, 1);
  float4x4 transformMat;

  float3 lookVector = normalize(to.skinnedPosition.xyz);
  //float3 lookVector = normalize(VSC_CameraDirection);
  //float3 upVector = normalize(float3(lookVector.x,-lookVector.z,lookVector.y)); 
  float3 upVector = float3(0,1,0);
  
  transformMat[2] = float4(lookVector.xyz, 0);
  transformMat[0] = float4(normalize(cross(upVector, transformMat[2].xyz)), 0);
  transformMat[1] = float4(normalize(cross(transformMat[2].xyz, transformMat[0].xyz)), 0);  
  transformMat[3] = float4(0,0,0,1);
 
  transformMat = transpose(transformMat);  
  
  deltaPos = mul(transformMat, deltaPos);  
  
  //to.transformedPosition.xyz = mul(to.skinnedPosition + deltaPos, VSC_ViewMatrix);
  //to.transformedPosition.w = 1;
  //to.projectedPosition = mul(VSC_ProjMatrix, to.transformedPosition);   
  
  oProjectedPosition = to.projectedPosition;

  // Calculate mapping coordinates to screen space depth map (out of projectedPosition) and Z value to pass to PSVolCloud
  oPosition.xy = (to.projectedPosition.xy / to.projectedPosition.w + 1.0f) * 0.5f;  // Calculate mapping coordinates of the depth buffer
  oPosition.y = 1.0f - oPosition.y;                                                 // Revert Y axis
  oPosition.z = to.transformedPosition.z+particleDiameter;                          // Pass the Z value to PS
  oPosition.w = particleDiameter;                                                   // Pass the particle diameter

  // Initialize ligths
  AccomLights al;
  VertexInitLights(to.instanceColor.a,to.instanceShadow, al);
  // LUKE HACK
  al.ambient.a = 1.0f;

  // Apply fog
  float oFog;
  if (VSC_FogModeA)
  {
    if (VSC_FogModeB)
    {
      VFogFogAlpha(to, al, oFog);
    }
    else
    {
      VFogAlpha(to, al, oFog);
    }
  }
  else
  {
    if (VSC_FogModeB)
    {
      VFog(to, oFog);
    }
    else
    {
      VFogNone(oFog);
    }
  }

  // Initialize shadow variable designed for shadow receiving
  if (VSC_ShadowReceiverFlag)
  {
    VShadowReceiver(to, oShadowMap);
  }
  else
  {
    oShadowMap = 0;
  }

  // Include P&S lights
  VLPointSpotN(to, al);


  // transform position to object space (in our case object space = world space, but position is relative to camera position)
  float3x4 invView = transpose(VSC_ViewMatrix);
  float3 objSpacePos = mul(to.transformedPosition.xyz, invView).xyz;
  float4 vertexPos = float4(objSpacePos, 1);

  float4 center_height = VSC_SkinningInstancingData[index + 1];

  // darkening based on vertex height (lower = darker)
  float heightCoef = saturate((vertexPos.y - center_height.y + center_height.w*0.5f) / center_height.w);  
  heightCoef = 0.1f + heightCoef;

  float3 dirFromCenter = center_height.xyz - vertexPos.xyz;
  float dirFromCenterLength = length(dirFromCenter);

  // darkening based on the distance from center
  float distCoef = dirFromCenterLength / (center_height.w * 0.5f);
  distCoef = saturate(distCoef);

  // compute darkening coef based on the sun direction
  float sunCoef = max(dot(normalize(dirFromCenter), VSC_LDirectionTransformedDir.xyz), 0);
  sunCoef *= 0.5f;
  sunCoef = sunCoef*sunCoef*distCoef*distCoef; 
  sunCoef = 0.1f + sunCoef;
 
  // alpha coef
  float alphaCoef = max(saturate(1.2f - saturate(distCoef*distCoef)), 0.5f);
  
  float dissipationCoef = saturate(2*globalAlpha - distCoef);
  alphaCoef = 1.0f;
  alphaCoef *= dissipationCoef;
 
 
  float colCoef = heightCoef + sunCoef;
  //colCoef = 0.2f + colCoef*0.8f;
  
  float4 modulateColor = float4(colCoef, colCoef, colCoef, alphaCoef);

  // Write lights to output
  VDoneLights(al, oOutAmbient, oOutSpecular, oOutDiffuse);
  vOutModulateColor = modulateColor;
  oOutFog = oFog;
}

#include "VS.h"

// structure representing one item in VSC_SkinningInstancingData
//struct SpriteInstancingItem
//{
//  float4 position;
//  float4 color;
//  float4 mapping_angle;
//};

void VSSprite(
  in uint4 i_UV_Index            : TEXCOORD,
  out float4 oProjectedPosition   : SV_POSITION,
  out float oOutFog              : COLOR1,
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
  float4 modulateColor;
  float particleDiameter;
  {
    float2 uv = BYTETOFLOAT01(i_UV_Index).xy;
    int index = i_UV_Index.z * SPRITE_INSTANCING_ITEM_SIZE;
    to.position = VSC_SkinningInstancingData[index + 0];
    to.skinnedMatrix0 = float4(1, 0, 0, 0);
    to.skinnedMatrix1 = float4(0, 1, 0, 0);
    to.skinnedMatrix2 = float4(0, 0, 1, 0);
    modulateColor = VSC_SkinningInstancingData[index + 1];
    float4 mapping_angle = VSC_SkinningInstancingData[index + 2];
    to.skinnedPosition.xyz = to.position.xyz;
    to.skinnedPosition.w = 1;
    to.skinnedNormal = normalize(VSC_CameraPosition.xyz - to.skinnedPosition.xyz);
    float scale = to.position.w;

    // Calculate diameter of the particle (no size limitation)
    particleDiameter = abs(scale);

    // first of all calculate distance from camera, we need it for size limitation
    to.transformedPosition.z = mul(to.skinnedPosition, VSC_ViewMatrix).z;

    // Make the particle Z to be closer by the particle radius to the camera (consider the particle is of a sphere shape - it is
    // not correct for the particle to completely disappear when it's center reaches the some occlusion plane)
    // Note it is necessary to do that prior the size limit, because this modification influences final size (by perspective) and particle
    // size control wouldn't work as expected
    to.transformedPosition.z -= particleDiameter * 0.5f;

    // calculate field of view as 1/sqrt(xScale*yScale)
    float fov = rsqrt(VSC_ProjMatrix._m00*VSC_ProjMatrix._m11);
    // limit screen space size to avoid extreme fillrate load when close
    float maxLocalPos = 0.75*fov*to.transformedPosition.z;

    // Make the limit much more benevolent if the particle is marked as no-limit (scale is negative)
    if (scale < 0) maxLocalPos = maxLocalPos * 1000.0f;

    float2 localPosition = min(particleDiameter, maxLocalPos) * (uv.xy - 0.5);
    // animate (rotate) the particle
    float2 sc; sincos(mapping_angle.w, sc.x, sc.y);
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

    float2 uvScreen = float2(uv.x, 1 - uv.y);
    vOutTexCoord01.xy = uvScreen * mapping_angle.zz + mapping_angle.xy;
    vOutTexCoord01.zw = float2(0, 0);

    // Instance shadow
    to.instanceColor = VSC_InstanceColor;
    to.instanceShadow = VSC_InstanceLandShadowIntensity.x;

    // Normal orientation was not changed
    to.normalAligned = false;
  }
  oProjectedPosition = to.projectedPosition;

  // Calculate mapping coordinates to screen space depth map (out of projectedPosition) and Z value to pass to PSSprite
  oPosition.xy = (to.projectedPosition.xy / to.projectedPosition.w + 1.0f) * 0.5f;  // Calculate mapping coordinates of the depth buffer
  oPosition.y = 1.0f - oPosition.y;                                                 // Revert Y axis
  oPosition.z = to.transformedPosition.z+particleDiameter;                          // Pass the Z value to PS
  oPosition.w = particleDiameter;                                                   // Pass the particle diameter

  // Initialize ligths
  AccomLights al;
  VertexInitLights(to.instanceColor.a,to.instanceShadow, al);

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

  // Write lights to output
  VDoneLights(al, oOutAmbient, oOutSpecular, oOutDiffuse);
  vOutModulateColor = modulateColor;
  oOutFog = oFog;
}


/// variation of sprite shader, where the sprite is not handled as billboard, but is placed horizontally on surface (with normal always pointing up)
void VSSpriteOnSurface(
  in uint4 i_UV_Index            : TEXCOORD,
  out float4 oProjectedPosition   : SV_POSITION,
  out float oOutFog              : COLOR1,
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
  float4 modulateColor;
  float particleDiameter;
  {
    float2 uv = BYTETOFLOAT01(i_UV_Index).xy;
    int index = i_UV_Index.z * SPRITE_INSTANCING_ITEM_SIZE;
    to.position = VSC_SkinningInstancingData[index + 0];
    to.skinnedMatrix0 = float4(1, 0, 0, 0);
    to.skinnedMatrix1 = float4(0, 1, 0, 0);
    to.skinnedMatrix2 = float4(0, 0, 1, 0);
    modulateColor = VSC_SkinningInstancingData[index + 1];
    float4 mapping_angle = VSC_SkinningInstancingData[index + 2];
    to.skinnedPosition.xyz = to.position.xyz;
    to.skinnedPosition.w = 1;
    to.skinnedNormal = float3(0, 1, 0);

    // we need transformed position for size limitation
    to.transformedPosition.xyz = mul(to.skinnedPosition, VSC_ViewMatrix).xyz;

    float scale = to.position.w;

    // Calculate diameter of the particle (no size limitation)
    particleDiameter = abs(scale);

    // calculate field of view as 1/sqrt(xScale*yScale)
    float fov = rsqrt(VSC_ProjMatrix._m00*VSC_ProjMatrix._m11);
    // limit screen space size to avoid extreme fillrate load when close
    float maxLocalPos = 0.75*fov*to.transformedPosition.z;

    // Make the limit much more benevolent if the particle is marked as no-limit (scale is negative)
    if (scale < 0) maxLocalPos = maxLocalPos * 1000.0f;

    float2 localPosition = min(particleDiameter, maxLocalPos) * (uv.xy - 0.5);
    // animate (rotate) the particle
    float2 sc; sincos(mapping_angle.w, sc.x, sc.y);
    float2 localRotatedPosition;
    localRotatedPosition.x = dot(float2(sc.y, -sc.x), localPosition);
    localRotatedPosition.y = dot(sc, localPosition);

    // update skinned position
    to.skinnedPosition.x += localRotatedPosition.x;
    to.skinnedPosition.z += localRotatedPosition.y;

    // compute final transformed position
    to.transformedPosition.xyz = mul(to.skinnedPosition, VSC_ViewMatrix).xyz;

    to.transformedPosition.w = 1;
    // project into the homogenous view space
    to.projectedPosition = mul(VSC_ProjMatrix, to.transformedPosition);

    // World position
    to.worldPosition = mul(to.skinnedPosition, VSC_WorldMatrix);

    float2 uvScreen = float2(uv.x, 1 - uv.y);
    vOutTexCoord01.xy = uvScreen * mapping_angle.zz + mapping_angle.xy;
    vOutTexCoord01.zw = float2(0, 0);

    // Instance shadow
    to.instanceColor = VSC_InstanceColor;
    to.instanceShadow = VSC_InstanceLandShadowIntensity.x;

    // Normal orientation was not changed
    to.normalAligned = false;
  }
  oProjectedPosition = to.projectedPosition;

  // Calculate mapping coordinates to screen space depth map (out of projectedPosition) and Z value to pass to PSSprite
  oPosition.xy = (to.projectedPosition.xy / to.projectedPosition.w + 1.0f) * 0.5f;  // Calculate mapping coordinates of the depth buffer
  oPosition.y = 1.0f - oPosition.y;                                                 // Revert Y axis
  oPosition.z = to.transformedPosition.z+particleDiameter;                          // Pass the Z value to PS
  oPosition.w = particleDiameter;                                                   // Pass the particle diameter

  // Initialize ligths
  AccomLights al;
  VertexInitLights(to.instanceColor.a,to.instanceShadow, al);

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

  // Write lights to output
  VDoneLights(al, oOutAmbient, oOutSpecular, oOutDiffuse);
  vOutModulateColor = modulateColor;
  oOutFog = oFog;
}

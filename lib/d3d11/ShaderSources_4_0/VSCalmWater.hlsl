#include "VS.h"

struct SCalmWaterVsIn
{
  float4 pos : POSITION;

  uint4 iNormal : NORMAL;
  int2 iTexCoord0 : TEXCOORD0;
  int2 iTexCoord1 : TEXCOORD1;
  uint4 iS : TANGENT0;
  uint4 iT : TANGENT1;
};

SCalmWaterVs2Ps VSCalmWater( SCalmWaterVsIn i, out float4 outPos : POSITION0 )
{
  SCalmWaterVs2Ps o = (SCalmWaterVs2Ps)0;
  
  float4 pos = i.pos;
  
  //float4 posV0 = float4( mul(pos, VSC_ViewMatrix), 1);
  //float2 posW = posV0.xz + VSC_WaterPars[23].xy;
  float2 posW = pos.xz + VSC_WaterPars[26].xy - VSC_CameraPosition.xz;
  
  //float2 tc = i.pos.xz * VSC_WaterPars[4].xy;
  float2 tc = posW.xy * VSC_WaterPars[4].xy;
  
  o.tc1.xy = tc.xy + VSC_WaterPars[5].xy;
  
  float2 tc1D = tc * VSC_WaterPars[7].xy; // detail wrap scale
  float2 tc2D = tc1D.yx;
  tc1D += VSC_WaterPars[6].xy;
  tc2D += VSC_WaterPars[6].zw;
  
// VERTEX ANIMATION
  //float4 vapA = float4( 0.5, 0.1, 1, 0 ); //VSC_CalmWaterPars[0];
  float4 vapA = VSC_WaterPars[0];
  float4 vapB = VSC_WaterPars[1];
  //float phA = vapA.x * pos.x + vapA.y * pos.z + vapA.w;
  //float phB = vapB.x * pos.x + vapB.y * pos.z + vapB.w;
  float phA = vapA.x * posW.x + vapA.y * posW.y + vapA.w;
  float phB = vapB.x * posW.x + vapB.y * posW.y + vapB.w;
  float dzAB = vapA.z * sin( phA ) + vapB.z * sin( phB );
  pos.y += dzAB;
  
  float4 posV = float4( mul(pos, VSC_ViewMatrix), 1);
  float4 posS = mul( VSC_ProjMatrix, posV );
  
  // direction to camera in object-space
  float3 dirToCamO = VSC_CameraPosition.xyz - pos.xyz;
  // direction from camera in object-space normalized to screen distance
  float3 dirFromCamD = -dirToCamO;
  float rd = 1.0 / dot( VSC_CameraDirection.xyz, dirFromCamD.xyz );
  dirFromCamD *= rd;
  
  
  outPos = posS;
  // base wrap
  o.tc0.xy = tc;
  // wrap wave A
  o.tc1.xy = tc.xy + VSC_WaterPars[5].xy;
  // wrap wave B
  o.tc1.zw = tc.yx + VSC_WaterPars[5].zw;
  // detail wrap
  o.tc2.xy = tc1D;
  o.tc2.zw = tc2D;
  o.tc3.xyz = posV.xyz;
  
  //o.tc3.xyz = dirToCam.xyz;
  o.tc3.w = posS.w; // depth of surface
  o.tc4.xyz = dirToCamO;
  
  //o.tc5.xyz = dirFromCamD;
  o.tc5.xyz = VSC_CameraDirection.xyz;
  o.tc6.xyz = VSC_CameraPosition.xyz;
  
  o.tc4.w = pos.x;
  o.tc5.w = pos.y;
  o.tc6.w = pos.z;
  
  float4 tcShadowmap;
  
  /*if( ShadowReceiverSSSM ) // reg b7
  {
    tcShadowmap = posS;
  }
  else
  {*/
    // Write shadow map UV transformation
    tcShadowmap.xyz = mul( posV, VSC_ShadowmapMatrix );
    tcShadowmap.w = posV.z;
  //}
  o.tc7.xyzw = tcShadowmap.xyzw;
  
// prepare fog lerp factor
  float d = sqrt( dot( posV.xyz, posV.xyz ) );
  float haze = Haze( d );
  half fogLerpFactor = haze * saturate( ( VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.z - d )
    * VSC_SpecularPower_Alpha_FogEnd_RCPFogEndMinusFogStart.w );
  o.tc0.w = fogLerpFactor;
  
  return o;
}

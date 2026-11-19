#include "psvs.h"
#include "common.h"
#include "VSConstants.h"

// ===================================================================================
// NonTL drawing
// ===================================================================================

SNonTL VSNonTL(
  in float4 vPosition   : POSITION,
  in uint4 vAmbient    : COLOR0,
  in uint4 vSpecular   : COLOR1,
  in float2 vTexCoord0  : TEXCOORD0,
  in float2 vTexCoord1  : TEXCOORD1,
  out float4 oPosition : POSITION
)
{
  // final position
	oPosition.xyz = vPosition.xyz;
	oPosition.w = 1.0f;
	oPosition = oPosition / vPosition.w;

  // fill output structure
  SNonTL output;
  output.ambientColor = BYTETOFLOAT01_BGRA2RGBA(vAmbient);
	output.diffuseMap = float4(vTexCoord0.xy, 1, 1);
  return output;
}



// ===================================================================================
// Postprocess sruff
// ===================================================================================

VSPP4T_OUTPUT VSPostProcessCustomEdge4T(
  in float3 vPosition  : POSITION,
  in float  vEdgeIndex : BLENDINDICES
)
{
	VSPP4T_OUTPUT output;
	output.Position.xyzw = vPosition.xyzz+VSC_PPEdgePosOffset;
	output.TexCoord0.x = VSC_PPEdgeU[vEdgeIndex].x;
	output.TexCoord0.y = VSC_PPEdgeV[vEdgeIndex].x;
	output.TexCoord1.x = VSC_PPEdgeU[vEdgeIndex].y;
	output.TexCoord1.y = VSC_PPEdgeV[vEdgeIndex].y;
	output.TexCoord2.x = VSC_PPEdgeU[vEdgeIndex].z;
	output.TexCoord2.y = VSC_PPEdgeV[vEdgeIndex].z;
	output.TexCoord3.x = VSC_PPEdgeU[vEdgeIndex].w;
	output.TexCoord3.y = VSC_PPEdgeV[vEdgeIndex].w;
  return output;
}

VSPP_OUTPUT VSPostProcess(
  in float3 vPosition : POSITION,
  in float2 vTexCoord : TEXCOORD
)
{
	VSPP_OUTPUT output;
	output.Position.xyzw = vPosition.xyzz;
	output.TexCoord = vTexCoord;
  return output;
}

void VSPostProcessRescaleBicubic(
  float4 iPos : POSITION0, float2 iUV	: TEXCOORD0,
	out float4 oPos	: POSITION0, out float2 oUV	: TEXCOORD0
)
{
	oPos = iPos.xyzz; //*float4(2,2,1,1);
	oUV.xy = iUV.xy;
}

// ===================================================================================
// Shaders for rain3D
// ===================================================================================
 
struct VSRainInput
{
	float3	vPos: POSITION;
	float3	vTexCoordLayer: TEXCOORD0;
};

VSPSRain VSPostProcessRain3D(VSRainInput input)
{
	VSPSRain output;
	
	//XYZ scale
	float3 boxScale = lerp(VSC_RainMinBox, VSC_RainMaxBox, input.vTexCoordLayer.z);
	
	//the real UV coords
	float2 uv = input.vTexCoordLayer.xy * boxScale.xy;

	//apply UV animation
	output.vTexCoordDistDepth.xy = uv * VSC_RainShiftScale.zw + VSC_RainShiftScale.xy;
	
	float3 mpos = input.vPos;

	//clip by sea level
	//we can precompute it only in case we cannot use different Y sizes of certain layers.
	float localSeaLevel = VSC_RainSeaLevel.y / boxScale.y;
	mpos.y = max(mpos.y, localSeaLevel);

	//store Y difference, to fadeout top/bottom edges
	output.vTexCoordDistDepth.z = mpos.y;
	
	//the real model position
	float3 pos = mpos * boxScale;
	//camera position
	float3 cpos = mul(float4(pos, 1), VSC_RainViewMatrix);
	//projected position
	output.vPos = mul(VSC_RainProjMatrix, float4(cpos, 1));
	
	//store depth for soft depth clipping in PS
	output.vTexCoordDistDepth.w = output.vPos.z;
	return output;
}

// ===================================================================================
// SSAO
// ===================================================================================

/*!
pixel offsets
*/
#define ssaoHalfScreenOffset		VSC_SSAOPixelOffsets.xy
#define ssaoHalfNoiseOffset		  VSC_SSAOPixelOffsets.zw
#define ssaoHalfDepthOffset		  VSC_SSAOPixelOffsets.zw 

/*!
SSAO vertex shader
*/

VS_SSAO VSPostProcessSSAO(in float3 vPosition : POSITION, in float2 uv : TEXCOORD)
{
	VS_SSAO output;

	output.m_vPos 				= vPosition.xyzz;
	output.m_vTexCoord.xy	= uv + ssaoHalfScreenOffset;
	output.m_vTexCoord.zw	= uv*VSC_SSAONoisePixelScale.xy + ssaoHalfNoiseOffset;
	
	return output;
}

/*!
SSAO downsampling
*/

VS_SSAODown VSPostProcessSSAODown(in float3 vPosition : POSITION, in float2 uv : TEXCOORD)
{
	VS_SSAODown output;

	output.m_vPos 			  = vPosition.xyzz;

  float2 uv2            = uv + ssaoHalfScreenOffset;
	output.m_vTex1Tex2.xy = uv2;
	output.m_vTex1Tex2.zw	= uv2 + float2(VSC_SSAOPixelOffsets.z , 0.0);
	output.m_vTex3Tex4.xy = uv2 + float2(0.0, VSC_SSAOPixelOffsets.w);
	output.m_vTex3Tex4.zw = uv2 + float2(VSC_SSAOPixelOffsets.z , VSC_SSAOPixelOffsets.w);
	
	return output;
}

//==================================================================================================
// POST PROCESS RotBlur
//==================================================================================================

/*
////////////////////////////////////////////////////////////////////////////////////////////////////
float4x4 GetRMatrix( float t )
{
  float4 Speeds = float4( 1.55, 0, 1.55, 1 );
   float4x4 mA = (float4x4)0;
   float4x4 mB = (float4x4)0;
   mA[0].x = 1; mA[1].y = 1; mA[2].z = 1; mA[3].w = 1;
   mB[0].x = 1; mB[1].y = 1; mB[2].z = 1; mB[3].w = 1;

   float angleA = 0.0;
   float angleB = 0.0;
   angleA = Speeds.x * t;
   angleB = Speeds.z * cos( t * Speeds.y );
   angleB = 1.57;
   
   float sinA = sin( angleA );
   float cosA = cos( angleA );
   float sinB = sin( angleB );
   float cosB = cos( angleB );
   
   mA[0].x = cosA;
   mA[2].z = cosA;
   mA[0].z = sinA;
   mA[2].x = -sinA;
   
   mB[1].y = cosB;
   mB[2].z = cosB;
   mB[1].z = sinB;
   mB[2].y = -sinB;
   
   mA = mul( mA, mB );
   return mA;
}
*/
/*
//////////////////////////////////////////////////////////////////////////////////////////
float4x4 MirrorMat( float4x4 m )
{
   float4x4 n;
   n[0][0] = m[0][0]; n[0][1] = m[1][0]; n[0][2] = m[2][0]; n[0][3] = m[3][0];
   n[1][0] = m[0][1]; n[1][1] = m[1][1]; n[1][2] = m[2][1]; n[1][3] = m[3][1];
   n[2][0] = m[0][2]; n[2][1] = m[1][2]; n[2][2] = m[2][2]; n[2][3] = m[3][2];
   n[3][0] = m[0][3]; n[3][1] = m[1][3]; n[3][2] = m[2][3]; n[3][3] = m[3][3];
   return n;
}
*/
float3x3 MirrorMat3( float3x3 m )
{
   float3x3 n;
   n[0][0] = m[0][0]; n[0][1] = m[1][0]; n[0][2] = m[2][0];
   n[1][0] = m[0][1]; n[1][1] = m[1][1]; n[1][2] = m[2][1];
   n[2][0] = m[0][2]; n[2][1] = m[1][2]; n[2][2] = m[2][2];
   return n;
}

/*
//////////////////////////////////////////////////////////////////////////////////////////
float3x3 GetRotMatrix( float x, float y, float z )
{
   float3x3 mA = (float3x3)0;
   float3x3 mB = (float3x3)0;
   mA[0].x = 1; mA[1].y = 1; mA[2].z = 1; //mA[3].w = 1;
   mB[0].x = 1; mB[1].y = 1; mB[2].z = 1; //mB[3].w = 1;

   float angleA = -y;
   float angleB = x;
   
   float sinA = sin( angleA );
   float cosA = cos( angleA );
   float sinB = sin( angleB );
   float cosB = cos( angleB );
   
   mA[0].x = cosA;
   mA[2].z = cosA;
   mA[0].z = sinA;
   mA[2].x = -sinA;
   
   mB[1].y = cosB;
   mB[2].z = cosB;
   mB[1].z = sinB;
   mB[2].y = -sinB;
   
   mA = mul( mB, mA );
   return mA;
}*/


//==================================================================================================
// POST PROCESS RotBlur
//==================================================================================================
VSPP_OUT VsPpRotBlur(
  in float3 inPos : POSITION,
  in float2 inTc0 : TEXCOORD
)
{
	VSPP_OUT o = (VSPP_OUT)0;
  half3 pos = inPos.xyz;
  half depthThreshold = VSC_RotBlur_mA0.w;
  half blurPower = VSC_RotBlur_mA1.w;
 
  // ------------------------------------------------------------------------------
  // description of modification can be found in file postProccess.cpp at line 3844
  // ------------------------------------------------------------------------------
  
  float3x3 AinvB;
  AinvB[0] = VSC_RotBlur_mA0.xyz;
  AinvB[1] = VSC_RotBlur_mA1.xyz;
  AinvB[2] = VSC_RotBlur_mA2.xyz;

  pos.xy *= VSC_ProjConsts.xy;  
  
  float3 dir = pos - mul(pos, AinvB);
    
  dir.y *= VSC_ProjConsts.z; // aspect correction
    
  o.pos.xyzw = float4( inPos.xyzz );
  o.tc0.xy = inTc0.xy;
  o.tc0.z = depthThreshold;
  o.tc1.xy = half2( dir.x, -dir.y ) * blurPower;
  
  //o.tc1.zw = half2( mA0.w, mA1.w );
  
  return o;
}

//==================================================================================================
// PPColors
//==================================================================================================
VSPP_OUT VsPpColors(
  in float3 inPos : POSITION,
  in float2 inTc0 : TEXCOORD
)
{
  VSPP_OUT o = (VSPP_OUT)0;
  half4 pos = half4( inPos.xy, 1.0, 1.0 );
  o.pos.xyzw = pos;
  o.tc0.xy = inTc0;  
  return o;
}

//==================================================================================================
// PPWetDistort
//==================================================================================================
VSPP_OUT VsPpWetDistort(
  in float3 inPos : POSITION,
  in float2 inTc0 : TEXCOORD
)
{
	VSPP_OUT o = (VSPP_OUT)0;
  half4 pos = half4( inPos.xy, 1.0, 1.0 );
  half2 tc;
  tc.x = 0.5 * inPos.x + 0.5;
  tc.y = -0.5 * inPos.y + 0.5;
  
  o.pos.xyzw = pos;
	o.tc0.xy = tc;
  
  float t = VSC_PPWetDistortPars.x;
  float a0 = sin( t * VSC_PPWDSpeeds.x + inTc0.x * VSC_PPWDPhases.x + inPos.x * VSC_PPWDPhases.z ) * VSC_PPWDAmps.x;
  float a1 = sin( t * VSC_PPWDSpeeds.y + inTc0.y * VSC_PPWDPhases.x + inPos.x * VSC_PPWDPhases.z ) * VSC_PPWDAmps.y;
  float a2 = sin( t * VSC_PPWDSpeeds.z + inPos.z * VSC_PPWDPhases.y + inPos.y * VSC_PPWDPhases.w ) * VSC_PPWDAmps.z;
  float a3 = sin( t * VSC_PPWDSpeeds.w + inTc0.x * VSC_PPWDPhases.y + inPos.y * VSC_PPWDPhases.w ) * VSC_PPWDAmps.w;
  half gPhase = lerp( VSC_PPWetDistortPars.z, VSC_PPWetDistortPars.w, tc.y );
  o.tc0.w = VSC_PPWetDistortPars.y * gPhase;
  o.tc1.x = ( a0 + a1 ) * gPhase;
  o.tc1.y = ( a2 + a3 ) * gPhase;
  o.tc1.zw = inPos.xy;
  o.tc2.xyz = half3( inTc0.x, inTc0.y, inPos.z );
  o.tc2.w = t;
  
  return o;
}

//==================================================================================================
// Dynamic blur
//==================================================================================================
VSPP_OUT VsPpDynamicBlur(
  in float3 inPos : POSITION,
  in float2 inTc0 : TEXCOORD
)
{
	VSPP_OUT o = (VSPP_OUT)0;
  float4 pos = half4( inPos.xy, 1.0, 1.0 );
  float2 tc = inTc0.xy;
  tc.y *= VSC_PPDynBlurPars0.y;
  o.tc0.zw = tc;
  tc += VSC_PPDynBlurPars1.zw;
  float2 dir = VSC_PPDynBlurPars1.xy;
  
  o.pos.xyzw = pos;
	o.tc0.xy = tc;
  o.tc1.xy = tc + dir;
  o.tc1.zw = tc - dir;
  o.tc2.xy = o.tc1.xy + dir;
  o.tc2.zw = o.tc1.zw - dir;
  o.tc3.xy = o.tc2.xy + dir;
  o.tc3.zw = o.tc2.zw - dir;
  o.tc4.xy = o.tc3.xy + dir;
  o.tc4.zw = o.tc3.zw - dir;
  o.tc5.xy = o.tc4.xy + dir;
  o.tc5.zw = o.tc4.zw - dir;
  o.tc6.xyzw = float4( 1, VSC_PPDynBlurPars0.z, 1, VSC_PPDynBlurPars0.z );
  return o;
}

//==================================================================================================
VSPP_OUT VsPpDynamicBlurFinal(
  in float3 inPos : POSITION,
  in float2 inTc0 : TEXCOORD
)
{
	VSPP_OUT o = (VSPP_OUT)0;
  float4 pos = half4( inPos.xy, 1.0, 1.0 );
  float2 tc = inTc0.xy;
  half weight0 = saturate( VSC_PPDynBlurPars0.x );
  float2 aspect = float2( 0, VSC_PPDynBlurPars0.y );
  o.pos.xyzw = pos;
	o.tc0.xy = tc + VSC_PPDynBlurPars0.zw;
  o.tc1.xy = tc + VSC_PPDynBlurPars1.xy * aspect + VSC_PPDynBlurPars1.zw;
  o.tc2.xy = tc - VSC_PPDynBlurPars1.xy * aspect + VSC_PPDynBlurPars1.zw;
  o.tc3.xy = tc + VSC_PPDynBlurPars1.yx * aspect + VSC_PPDynBlurPars1.zw;
  o.tc4.xy = tc - VSC_PPDynBlurPars1.yx * aspect + VSC_PPDynBlurPars1.zw;
  o.tc0.w = weight0;
  return o;
}

//==================================================================================================
// FXAA Vertex Shader
//==================================================================================================
void VsPpFxaa3_11(in float3 iPos: POSITION0, in float2 itc0: TEXCOORD0, 
	out float4 oPos: SV_POSITION, out float2 otc0: TEXCOORD0)
{
	oPos = float4(iPos.xy, 1.0, 1.0);
	otc0.xy = itc0.xy + VSC_VsFxaa.xy;
}

//==================================================================================================
// SMAA Vertex Shaders
//==================================================================================================
#define SMAA_PIXEL_SIZE (VSC_SmaaPixelSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_ONLY_COMPILE_VS 1
#include "PPAA\SMAA.h"

void DX11_SMAAEdgeDetectionVS(in float3 position : POSITION, in float2 texcoord : TEXCOORD0,
    out float4 oPos : SV_POSITION, out float2 oTexcoord : TEXCOORD0, out float4 offset[3] : TEXCOORD1) 
{
    float4 pos = float4(position.xy, 0.0, 1.0);
    oTexcoord = texcoord;
    SMAAEdgeDetectionVS(pos, oPos, oTexcoord, offset);
}

void DX11_SMAABlendingWeightCalculationVS(in float3 position : POSITION, in float2 texcoord : TEXCOORD0,
    out float4 oPos : SV_POSITION, out float2 oTexcoord : TEXCOORD0, out float2 pixcoord : TEXCOORD1, out float4 offset[3] : TEXCOORD2) 
{
    float4 pos = float4(position.xy, 0.0, 1.0);
    oTexcoord = texcoord;
    SMAABlendingWeightCalculationVS(pos, oPos, oTexcoord, pixcoord, offset);
}

void DX11_SMAANeighborhoodBlendingVS(in float3 position : POSITION, in float2 texcoord : TEXCOORD0,
    out float4 oPos : SV_POSITION, out float2 oTexcoord : TEXCOORD0, out float4 offset[2] : TEXCOORD1) 
{
    float4 pos = float4(position.xy, 0.0, 1.0);
    oTexcoord = texcoord;
    SMAANeighborhoodBlendingVS(pos, oPos, oTexcoord, offset);
}



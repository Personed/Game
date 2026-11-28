#include "VS.h"

struct SWCloudsVertexInput
{
    float3 position			: POSITION;
    float3 texCoords		: TEXCOORD0;
    float layerFade			: TEXCOORD1;
    float2 texCoordsNoise	: TEXCOORD2;
    float3 lightColour		: TEXCOORD3;
#if FADE_MODE==0
    float3 loss				: TEXCOORD4;
    float3 inscatter		: TEXCOORD5;
#endif
};

/// definition of constants
#define illuminationOrigin  VSC_SimulWeatherPars[0].xyz
#define altitudeTexCoord VSC_SimulWeatherPars[0].w
#define illuminationScales  VSC_SimulWeatherPars[1].xyz
#define maxFadeDistanceMetres VSC_SimulWeatherPars[1].w
#define fractalScale  VSC_SimulWeatherPars[2]
#define eyePosition  VSC_SimulWeatherPars[3]
#define fogPars VSC_SimulWeatherPars[4]

SWCloudsVertexOutput VSSimulWeatherClouds(SWCloudsVertexInput IN)
{
  SWCloudsVertexOutput OUT;

  //OUT.hPosition = mul( worldViewProj, float4(IN.position.xyz , 1.0));

  // Transformed position
  float4 transformedPosition = float4(mul(float4(IN.position.xyz , 1.0), VSC_ViewMatrix), 1);
  // Projected position
  OUT.hPosition = mul(VSC_ProjMatrix, transformedPosition);

		OUT.texCoords.xyz=IN.texCoords;
	OUT.texCoords.w=0.5f+0.5f*saturate(IN.texCoords.z);
	const float c=fractalScale.w;
	OUT.texCoordsNoise=IN.texCoordsNoise;
	OUT.wPosition=(IN.position.xyz-eyePosition.xyz);
	OUT.layerFade_Fog.x=IN.layerFade;
	// Note position.xzy is used!
	float3 texCoordLightning=(IN.position.xzy-illuminationOrigin.xyz)/illuminationScales.xyz;
	texCoordLightning.z=0.5f;
	OUT.texCoordLightning=texCoordLightning;
	float3 view=normalize(OUT.wPosition.xyz);
	float sine=view.y;
	OUT.lightColour=IN.lightColour;
// Fade mode ZERO - fade values come from the vertex. So we pass them on to the pixel shader:
#if FADE_MODE==0
    OUT.loss			=IN.loss;
    OUT.inscatter		=IN.inscatter;
#endif
// Fade mode ONE - fade is calculated from the fade textures. So we send a texture coordinate:
#if FADE_MODE==1
	OUT.fade_texc=float3(length(OUT.wPosition.xyz)/maxFadeDistanceMetres,0.5f*(1.f-sine),altitudeTexCoord);
	//OUT.fade_texc=float3(.5f,0.5f,0.5f);
#endif
  // FOG
  float fogCoef = saturate((length(OUT.wPosition.xyz) - fogPars.x) * fogPars.y);
  OUT.layerFade_Fog.y = fogCoef * fogCoef;

  return OUT;
}

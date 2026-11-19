#include "ps.h"

// we need a value which we can add 4x together with no overflow, and with some more headroom for error
#define MAX_HALF_4 (MAX_HALF/10/4)
// similar to MAX_HALF_4, but can be used when adding 16 values
#define MAX_HALF_16 (MAX_HALF_4/4)


/*!
simple squares
*/
float sqr(float s)  { return s*s; }
float sqr4(float4 s)  { return s*s; }


// ===================================================================================
// Shaders for rain3D
// =================================================================================== 
#define PSRainTexture sampler0
#define PSRainDepthTexture sampler1

float4 PSPostProcessRain3D(VSPSRain input): COLOR0
{
  float4 screenCoord = input.vPos;

	//realize depth distance
	float2 screenUV = ( screenCoord + float2( 0.5, 0.5 ) ) * PSC_RainScreenRes.xy;
	float sampledDepth = tex2D(PSRainDepthTexture, screenUV);
 	float depth = 1.0/(sampledDepth + saturate(0.001 - sampledDepth)); 
	float diff = input.vTexCoordDistDepth.w - depth;
	//fade-out the ends of geometry to hide edges
	float scale = 1 - saturate(abs(input.vTexCoordDistDepth.z));
	scale = pow(scale,3);
	
	//fade-out pixels sinking into geometry
	float fadeOut = 1 - saturate(diff / 0.3);
//if(diff > 0)
	//return float4(fadeOut, 0,0,1);

	//float fadeOut = 1;
	float4 tcolor = tex2D(PSRainTexture, input.vTexCoordDistDepth.xy) * PSC_RainAlpha * scale * fadeOut;
	
	//premultiplied alpha
	return float4(tcolor.rgb * tcolor.a, tcolor.a);
}

// ===================================================================================
// Shader for universal blur
// ===================================================================================

#define PSScene sampler0

float4 PSPostProcessGaussBlur(VSPP4T_OUTPUT input): COLOR0
{
  float2 uv = input.TexCoord0;
	float3 p = float4(0.0f, 0.0f, 0.0f, 0.0f);
	for(int s = 0; s < PSC_BlurKernelSize.x; s++)
	{
		p += tex2D( PSScene, float2(uv.x + PSC_BlurKernel[s].x, uv.y + PSC_BlurKernel[s].y)) * PSC_BlurKernel[s].a;
	}
	return float4(p,1);
}

#define PSGamm sampler1

// input: HDR scene

float4 PSPostProcessBloomDownsample2(VSPP4T_OUTPUT input): COLOR0
{
  half2 uv = input.TexCoord0;
#if 1 //_VBS3 // Hotfix - input values are sometimes very wrong (infinite?) which break the whole downsizing and avg/max color determination
	float3 p =	(saturate(tex2D( PSScene, uv + PSC_SceneOffsets2[0] ).rgb) +
       			   saturate(tex2D( PSScene, uv + PSC_SceneOffsets2[1] ).rgb) +
       			   saturate(tex2D( PSScene, uv + PSC_SceneOffsets2[2] ).rgb) +
       			   saturate(tex2D( PSScene, uv + PSC_SceneOffsets2[3] ).rgb) ) * 0.25f;
#else
	float3 p =	(tex2D( PSScene, uv + PSC_SceneOffsets2[0] ).rgb +
       			tex2D( PSScene, uv + PSC_SceneOffsets2[1] ).rgb +
       			tex2D( PSScene, uv + PSC_SceneOffsets2[2] ).rgb +
       			tex2D( PSScene, uv + PSC_SceneOffsets2[3] ).rgb) * 0.25f;
#endif
  
  p = min(p,MAX_HALF_4);
  
  float aperture = ApertureControl(tex2D(sampler1,float2(0,0)).x);
  
  // the p will become p*aperture
  p *= aperture;
  
  // approximate perceived brightness
  half i = p.r*0.299+p.g*0.587+p.b*0.114;
  
  float overbrightF = saturate((i-PSC_BloomPars.x)*PSC_BloomPars.y);
 	return float4(min(p*overbrightF/aperture,MAX_HALF_4),1);
 	//return p * tex1D( PSGamma, imax + PSC_GammaBias);
}

// input: output of previous bloom downsampling step

float4 PSPostProcessBloomDownsample4(VSPP4T_OUTPUT input): COLOR0
{
  half2 uv = input.TexCoord0;

	half3 p = float4(0.0f, 0.0f, 0.0f, 0.0f);

	for(int v = 0; v < 4; v++)
	{
		for(int h = 0; h < 4; h++)
		{
			p += tex2D( PSScene, uv + PSC_SceneOffsets4[v*4 + h]);
		}
	}
	return half4(p * (1.0/16), 1);
}

#define PSBlurredScene sampler1

float4 PSPostProcessBloomCombine(VSPP4T_OUTPUT input): COLOR0
{
  half2 uv = input.TexCoord0;
  half2 uv2 = input.TexCoord1;
	return float4( tex2D(PSBlurredScene, uv).rgb * PSC_BloomPars.z + tex2D(PSScene, uv2).rgb * PSC_BloomPars.w, 1);
}

// ===================================================================================
// Shaders designed for SB rendering
// ===================================================================================

#define VSM 0

float4 PSShadowBufferAlpha_Default(float4 vPos: SV_POSITION, float4 tPosition:TEXCOORD7, float2 tDiffuseMap:TEXCOORD0) : COLOR0
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);

  // Kill output in case of alfa
  clip(diffuseMap.a - 0.5);

  #if VSM
  // Write out the Z and Z^2 (for VSM)
  return float4(tPosition.z,tPosition.z*tPosition.z,1,1);
  #else
  return float4(tPosition.z,1,1,1);
  #endif
}

//////////////////////////////////////////////

half4 PSShadowBufferAlpha_nVidia(float4 vPos: SV_POSITION, float4 tPosition:TEXCOORD7, float2 tDiffuseMap:TEXCOORD0) : COLOR0
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);

  // It doesn't matter what we write to color map
  return diffuseMap;
}

// ===================================================================================
// Shaders designed for depth map rendering
// ===================================================================================

float4 PSInvDepthAlphaTest(float4 vPos: SV_POSITION, float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0) : COLOR0
{
  // Kill output in case it is too close
  clip(tPosition.z - PSC_DepthClip.w);

  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);

  // Kill output in case of alfa
  clip(diffuseMap.a - 0.5);

  // Write out the inverse of Z  
  return float4(1.0f / tPosition.zzz, 1);
}

//! pixel shader with no output
float4 PSEmpty() : COLOR0
{
  clip(-1); // kill them all!
  return 0;
}

//! pixel shader with no output (_Default variant)
float4 PSEmpty_Default() : COLOR0
{
  clip(-1); // kill them all!
  return 0;
}

//! pixel shader with no output (_nVidia variant)
float4 PSEmpty_nVidia() : COLOR0
{
  clip(-1); // kill them all!
  return 0;
}


// we are interested in primary texture alpha only, nothing else
#ifdef _XBOX
float4 PSAlphaOnly(float2 tDiffuseMap:TEXCOORD0) : COLOR0
#else
float4 PSAlphaOnly(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0) : COLOR0
#endif
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);

  // Kill output, consider the alphaRef value
  clip(diffuseMap.a - PSC_X_X_X_AlphaRef.a);

  #ifdef _XBOX
  return diffuseMap;
  #else
  return float4(1.0f / tPosition.zzz, diffuseMap.a);
  #endif
}

#ifdef _XBOX
float4 PSAlphaOnlyMod(float2 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#else
float4 PSAlphaOnlyMod(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#endif
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);

  // Calculate the final alpha
  half finalAlpha = diffuseMap.a*alpha.a;

  // Kill output, consider the alphaRef value
  clip(finalAlpha - PSC_X_X_X_AlphaRef.a);

  #ifdef _XBOX
  return diffuseMap;
  #else
  return half4(1.0f / tPosition.zzz, finalAlpha);
  #endif
}
#ifdef _XBOX
float4 PSAlphaOnlyModAToC(float2 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#else
half4 PSAlphaOnlyModAToC(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#endif
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);

  // Calculate the final alpha
  half finalAlpha = diffuseMap.a*alpha.a;

#if ARMA3_ATOC_FIX
  clip(finalAlpha-ATOC_ALPHA_CLIP);
#endif

  #ifdef _XBOX
  return diffuseMap;
  #else
  return half4(1.0f / tPosition.zzz, finalAlpha);
  #endif
}

#ifdef _XBOX
float4 PSAlphaOnlyTree(float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#else
float4 PSAlphaOnlyTree(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#endif
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  half4 normalMap = tex2D(sampler1, tDiffuseMap.wzyx);

  // GetColorDiffuseDiscreteNoise takes care of alpha computation and pixel discarding
  half4 color = GetColorDiffuseDiscreteNoise(diffuseMap,alpha.a,normalMap.a, false);
#ifdef _XBOX
  return color;
#else
  return half4(1.0f / tPosition.zzz, color.a);
#endif
}

#ifdef _XBOX
float4 PSAlphaOnlyTreeAToC(float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#else
float4 PSAlphaOnlyTreeAToC(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#endif
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  half4 normalMap = tex2D(sampler1, tDiffuseMap.wzyx);
  half4 color = GetColorDiffuseDiscreteNoise(diffuseMap,alpha.a,normalMap.a, true);
#if ARMA3_ATOC_FIX
  clip(color.a-ATOC_ALPHA_CLIP);
#endif

  //color.a = 0.95;
  //clip(color.a-0.5);
#ifdef _XBOX
  return color;
#else
  return half4(1.0f / tPosition.zzz, color.a);
#endif
}

#ifdef _XBOX
float4 PSAlphaOnlyTreeAdv(float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#else
float4 PSAlphaOnlyTreeAdv(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#endif
{
    // Sample the diffuse map
    half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  #ifdef NEW_TREE_NOISE
    // GetColorDiffuseDiscreteNoiseAdv takes care of alpha computation and pixel discarding
    half4 color = GetColorDiffuseDiscreteNoiseAdv(diffuseMap,alpha.a, false);
  #else
    // sample NON map (for noise)
    half4 normalMap = tex2D(sampler1, tDiffuseMap.wzyx);
    // GetColorDiffuseDiscreteNoise takes care of alpha computation and pixel discarding
    half4 color = GetColorDiffuseDiscreteNoise(diffuseMap,alpha.a,normalMap.a, false);
  #endif
#ifdef _XBOX
  return color;
#else

  return half4(1.0f / tPosition.zzz, color.a);
#endif
}

#ifdef _XBOX
float4 PSAlphaOnlyTreeAdvAToC(float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#else
float4 PSAlphaOnlyTreeAdvAToC(float4 vPos: SV_POSITION,float4 tPosition:TEXCOORD7, float4 tDiffuseMap:TEXCOORD0, half4 alpha:TEXCOORD_AMBIENT) : COLOR0
#endif
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  // GetColorDiffuseDiscreteNoiseAdv takes care of alpha computation and pixel discarding
  half4 color = GetColorDiffuseDiscreteNoiseAdv(diffuseMap,alpha.a, true);

#if ARMA3_ATOC_FIX
  clip(color.a-ATOC_ALPHA_CLIP);
#endif

#ifdef _XBOX
  return color;
#else
  return half4(1.0f / tPosition.zzz, color.a);
#endif
}


/// light version of STerrain - only values output by alpha vertex shader are used here
struct STerrainAlphaOnly
{
#ifndef _XBOX
  float4 tShadowBuffer           : TEXCOORDSR; //5
#endif
  float4 vPos                    : VPOS;
//   half4 ambientColor             : TEXCOORD_AMBIENT; //6** not for alpha
//   half4 specularColor            : TEXCOORD_SPECULAR; //7** not for alpha
//   half3 tLightLocal              : COLOR0; // ** not for alpha
//   half4 tPSFog                   : COLOR1; // ** not for alpha
  float4 tSatAndMask_GrassMap    : TEXCOORD4;
  float4 tColorMap_NormalMap     : TEXCOORD0;
  float4 tDetailMap_SpecularMap  : TEXCOORD1;
  float4 grassAlpha              : TEXCOORD2;
//   half3 tEyeLocal                : TEXCOORD3; // // ** not for alpha
};

float4 PSTerrainGrassAlphaX(STerrainAlphaOnly input): COLOR0
{
  // extend real input to match the signature of the body
  // the values we are are not used anyway, therefore they should not matter
  STerrain inBody;
  inBody.vPos  = input.vPos;
  inBody.ambientColor = 0;
  inBody.specularColor = 0;
  inBody.tShadowBuffer = 0;
  inBody.tLightLocal = 0;
  inBody.tPSFog  = 0;
  inBody.tSatAndMask_GrassMap = input.tSatAndMask_GrassMap;
  inBody.tColorMap_NormalMap = input.tColorMap_NormalMap;
  inBody.tDetailMap_SpecularMap  = input.tDetailMap_SpecularMap;
  inBody.grassAlpha  = input.grassAlpha;
  inBody.tEyeLocal = 0;
  
  const bool layers[6]={true,true,true,true,true,true};
  // texkill is the only thing we are interested in
  PFTerrainBodyX(inBody, layers, true, true, false, true, false, 0, false);
  // besides of that, we also need to return projected z for depth map
  // we assume input.specularColor is receiving projected position instead of usual specular
  //return float4(1.0f / input.specularColor.zzz, 1);
  #ifdef _XBOX
  return 1;
  #else
  return float4(1.0f / input.tShadowBuffer.zzz, 1);
  #endif
}


// ===================================================================================
// Post process
// ===================================================================================

/// pass input directly to output
float4 PSPostProcessCopy(VSPP_OUTPUT input) : COLOR
{
  return tex2D(sampler0, input.TexCoord);
}



float4 PSPostProcessRescaleBicubic(float4 iPos: SV_POSITION, float2 iUV : TEXCOORD0) : COLOR0
{
  iUV += PSC_BicubicTexelSize.xw*float2(-0.5,-0.5);
	// determination of source texture coordinates
	float2 coeffCoord = iUV.xy * PSC_BicubicSourceSize.xy - float2(0.5, 0.5);

	// offsets and weights from coefficient texture
  // LUKE HACK - will this work?
	float3 hgX = tex2D(sampler1, float2(coeffCoord.x, 0)).xyz;
	float3 hgY = tex2D(sampler1, float2(coeffCoord.y, 0)).xyz;
	//float3 hgX = tex1D(sampler1, coeffCoord.x).xyz;
	//float3 hgY = tex1D(sampler1, coeffCoord.y).xyz;

	// sampling coordinates 
	float2 coordSource10 = iUV.xy + hgX.x * PSC_BicubicTexelSize.xy;
	float2 coordSource00 = iUV.xy - hgX.y * PSC_BicubicTexelSize.xy;

	float2 coordSource11 = coordSource10 + hgY.x * PSC_BicubicTexelSize.zw;
	float2 coordSource01 = coordSource00 + hgY.x * PSC_BicubicTexelSize.zw;

	coordSource10 = coordSource10 - hgY.y * PSC_BicubicTexelSize.zw;
	coordSource00 = coordSource00 - hgY.y * PSC_BicubicTexelSize.zw;

	// four linear source texture fetches
	float4 texSource00 = tex2D(sampler0, coordSource00);
	float4 texSource10 = tex2D(sampler0, coordSource10);
	float4 texSource01 = tex2D(sampler0, coordSource01);
	float4 texSource11 = tex2D(sampler0, coordSource11);

	// lin. interpolation along y
	texSource00 = lerp(texSource00, texSource01, hgY.z);
	texSource10 = lerp(texSource10, texSource11, hgY.z);

	// lin. interpolation along x
	texSource00 = lerp(texSource00, texSource10, hgX.z);

	return texSource00;
}


// Shader to present thermal image (compose it from blurred and original version)
// _VBS3_TI
float4 PSPostProcessThermalPresent(VSPP_OUTPUT input) : COLOR
{
  float4 c = lerp(tex2D(sampler0, input.TexCoord), tex2D(sampler1, input.TexCoord), PSC_BlurFactor.w);
  c.rgb = pow((c.rgb+0.055)/1.055, 2.4);
  return c;
}

// Thermal visualisation shader
// _VBS3_TI
float4 PSPostProcessThermal(VSPP_OUTPUT input) : COLOR
{
  // Decode the temperature from the RGB into 01 range
  float3 rgbEncodedTemperature = tex2D(sampler0, input.TexCoord).rgb;
  float temperature01 = (rgbEncodedTemperature.r + rgbEncodedTemperature.g + rgbEncodedTemperature.b) * 1.0f / 3.0f;

  // Stretch the temperature into the range that should be displayed and display it
  float intensity = saturate((temperature01 - PSC_TempRangeMin01_TempRangeMax01_InvTempRangeDiff_TIMode.x) * PSC_TempRangeMin01_TempRangeMax01_InvTempRangeDiff_TIMode.z);

  // Do the color conversion, write the output
  return float4(tex2D(sampler1, float2(intensity + (0.5f/TIConversionDimensionW), PSC_TempRangeMin01_TempRangeMax01_InvTempRangeDiff_TIMode.w / TIConversionDimensionH + (0.5f/TIConversionDimensionH))).rgb, 1.0f);
}

/////////////////////////////////////////////////////////////////////////////////////////////

float4 PSPostProcessGaussianBlurH(VSPP_OUTPUT input) : COLOR
{
  const int gaussSize = 5;
  half gaussV[5] = { 0.1016,  0.25,   0.2969, 0.25,   0.1016};
  float gaussP[5] = {-4,      -2,      0,      2,      4};

  half4 result = half4(0, 0, 0, 0);
  for (int i = 0; i < gaussSize; i++)
  {
    result += (half4)tex2D(sampler0, input.TexCoord + float2(PSC_InvWidth * gaussP[i], 0)) * gaussV[i];
  }
  return result;
}
float4 PSPostProcessGaussianBlurV(VSPP_OUTPUT input) : COLOR
{
  const int gaussSize = 5;
  half gaussV[5] = { 0.1016,  0.25,   0.2969, 0.25,   0.1016};
  float gaussP[5] = {-4,      -2,      0,      2,      4};

  half4 result = half4(0, 0, 0, 0);
  for (int i = 0; i < gaussSize; i++)
  {
    result += (half4)tex2D(sampler0, input.TexCoord + float2(0, PSC_InvHeight * gaussP[i])) * gaussV[i];
  }
  return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////


half4 PSPostProcessSSSM_Default(VSPP_OUTPUT input) : COLOR
{
#if _SM2
  return half4(1, 1, 0, 1);
#endif

  // Retrieve the transformed position
  float4 transformedPosition;
  {
    // Z is what is written in the depth map (depth map contains 1/z - this is done to make DOF effects easy)
    // TODO: shadows processed in more passes - prefer optimization here over DOF
    transformedPosition.z = 1.0f / tex2D(sampler0, input.TexCoord).r;

    // X and Y depends on the Z and FOV
    {
      // Get the screenspace XY in <-1,1>
      float2 screenspaceXY;
      screenspaceXY.x = input.TexCoord.x * 2.0f - 1.0f;
      //screenspaceXY.y = (1.0f - input.TexCoord.y) * 2.0f - 1.0f;
      screenspaceXY.y = 1.0f - input.TexCoord.y * 2.0f;

      // Get the transformed position XY
      transformedPosition.xy = screenspaceXY * PSC_FOV.xy * transformedPosition.z;
    }

    // W is supposed to be 1
    transformedPosition.w = 1.0;
  }

  // Calculate the shadow coefficient
  half shadowCoef = 1;
  half blurCoef = 0;
  if ((transformedPosition.z >= PSC_ShadowmapLayerBorder.w) && (transformedPosition.z < PSC_ShadowmapLayerBorder.x))
  {
    // Convert point into the shadow map
    float4 shadowMap;
    shadowMap.xyz = mul(transformedPosition, PSC_ShadowmapMatrix);
    shadowMap.w = transformedPosition.z;

#if VSM
    // VSM  - simply fetch z and z^2 filtered from the map
    float4 vsm = tex2Dlod(samplerShadowMap, shadowMap.xyzw);
    
    float  fAvgZ  = vsm.r; // Filtered z
    float  fAvgZ2 = vsm.g; // Filtered z-squared

    // Standard shadow map comparison
    shadowCoef = ( shadowMap.z <= fAvgZ );
    

    // Use variance shadow mapping to compute the maximum probability that the
    // pixel is in shadow
    float variance = ( fAvgZ2 ) - ( fAvgZ * fAvgZ );
    
    const float epsilonVSM = 0.000020f;
    variance       = min( 1.0f, max( 0.0f, variance + epsilonVSM ) );
    
    float mean     = fAvgZ;
    float d        = shadowMap.z - mean;
    float p_max    = variance / ( variance + d*d );

    // To combat light-bleeding, experiment with raising p_max to some power
    // (Try values from 0.1 to 100.0, if you like.)
    shadowCoef = max(shadowCoef,pow( p_max, 2.0f ));

    // special case - shadow map is empty (farthers possible)
    if (fAvgZ>=1) shadowCoef = 1;
    
#else

#ifdef _XBOX
    // Fetch the bilinear filter fractions and four samples from the depth texture. The LOD for the 
    // fetches from the depth texture is computed using aniso filtering so that it is based on the 
    // minimum of the x and y gradients (instead of the maximum).  
    float4 Weights;
    float4 SampledDepth;
    asm {
      tfetch2D SampledDepth.x___, shadowMap.xy, samplerShadowMap, OffsetX = -0.5, OffsetY = -0.5, UseComputedLOD=true, UseRegisterLOD=false
        tfetch2D SampledDepth._x__, shadowMap.xy, samplerShadowMap, OffsetX =  0.5, OffsetY = -0.5, UseComputedLOD=true, UseRegisterLOD=false
        tfetch2D SampledDepth.__x_, shadowMap.xy, samplerShadowMap, OffsetX = -0.5, OffsetY =  0.5, UseComputedLOD=true, UseRegisterLOD=false
        tfetch2D SampledDepth.___x, shadowMap.xy, samplerShadowMap, OffsetX =  0.5, OffsetY =  0.5, UseComputedLOD=true, UseRegisterLOD=false
        getWeights2D Weights, shadowMap.xy, samplerShadowMap, MagFilter=linear, MinFilter=linear, UseComputedLOD=true, UseRegisterLOD=false
    };

    
  //   float3 pnz = float3(+1,-1,0); // XXX this computation is not correct for this version of depth sampling (but it could be updated easily)
  //   float4 fxy = Weights.xyxy * pnz.xxyy + pnz.zzxx;
  //   Weights = fxy.zzxx * fxy.wywy;
    Weights = float4( (1-Weights.x)*(1-Weights.y), Weights.x*(1-Weights.y), (1-Weights.x)*Weights.y, Weights.x*Weights.y );
    float4 Attenuation = step(shadowMap.z, SampledDepth);
    shadowCoef = dot(Attenuation, Weights);
#else
    // Precomputed constants related to texture size
    const float texsize = PSC_SBTSize_invSBTSize_X_AlphaMult.x;
    const float invTexsize = PSC_SBTSize_invSBTSize_X_AlphaMult.y;

    // Calculate the fractional part of the texel addressing  
    float2 dxy = frac(shadowMap.xy * texsize);

    // Calculate weights for bilinear interpolation

    //float4 fxy = half4(dxy, half2(1,1)-dxy);

    float3 pnz = float3(+1,-1,0);
    float4 fxy = dxy.xyxy * pnz.xxyy + pnz.zzxx;

    // float4((1 - dxy.x) * (1 - dxy.y), (1 - dxy.x) * dxy.y, dxy.x * (1 - dxy.y), dxy.x * dxy.y)
    // float4(fxy.z       * fxy.w,       fxy.z       * fxy.y, fxy.x * fxy.w,       fxy.x * fxy.y)
    float4 weights = fxy.zzxx*fxy.wywy;
    // Get 4 shadow depths
    float4 shadowDepth = float4(
      tex2Dlod(samplerShadowMap, shadowMap.xyzw).r,
      tex2Dlod(samplerShadowMap, shadowMap.xyzw + float4(0,           invTexsize,0,0)).r,
      tex2Dlod(samplerShadowMap, shadowMap.xyzw + float4(invTexsize,  0,0,0)).r,
      tex2Dlod(samplerShadowMap, shadowMap.xyzw + float4(invTexsize,  invTexsize,0,0)).r
    );

    // where 1 is contained in the shadow map, we want to handle it as a practical infinity
    // any large number should do
    shadowDepth += (shadowDepth>=1)*1000;

    // Get 4 shadow/not shadow values
    float4 sc = shadowDepth>=shadowMap.z;

    // Use weights to get the shadow coefficient
    shadowCoef = dot(sc, weights);
#endif

#endif

  #ifdef SSSMBLUR
    // Get the blur coefficient
    const float maxDiff = 0.1; // 10 cm difference decides
    const float invW = 1.0/1174.0;
    const float invH = 1.0/881.0;
    bool disableBlur = false;
    if (abs(1.0f / tex2D(sampler0, input.TexCoord + float2( invW * 5,  invH * 5)).r - transformedPosition.z) > maxDiff) disableBlur = true;
    if (abs(1.0f / tex2D(sampler0, input.TexCoord + float2(-invW * 5,  invH * 5)).r - transformedPosition.z) > maxDiff) disableBlur = true;
    if (abs(1.0f / tex2D(sampler0, input.TexCoord + float2(-invW * 5, -invH * 5)).r - transformedPosition.z) > maxDiff) disableBlur = true;
    if (abs(1.0f / tex2D(sampler0, input.TexCoord + float2( invW * 5, -invH * 5)).r - transformedPosition.z) > maxDiff) disableBlur = true;
    if (!disableBlur)
    {
      blurCoef = saturate(/*(1.0/10.0) **/ PSC_MaxDepth.w * dot(max(shadowMap.zzzz - shadowDepth, 0), 0.25) /*/ transformedPosition.z*/);
    }
  #endif
  
  }
  else
  {
#ifdef _XBOX
    return tex2D(sampler1, input.TexCoord);
#else
    clip(-1);
#endif
  }
#ifdef _XBOX
  shadowCoef *= tex2D(sampler1, input.TexCoord).r;
#endif

  // Write shadow value to output
  return half4(shadowCoef, blurCoef, 0, 1);
}

half4 PSPostProcessSSSM_nVidia(VSPP_OUTPUT input) : COLOR
{
#if _SM2
  return half4(1, 1, 0, 1);
#endif

  // Retrieve the transformed position
  float4 transformedPosition;
  {
    // Z is what is written in the depth map
    transformedPosition.z = 1.0f / tex2D(sampler0, input.TexCoord).r;

    // X and Y depends on the Z and FOV
    {
      // Get the screenspace XY in <-1,1>
      float2 screenspaceXY;
      screenspaceXY.x = input.TexCoord.x * 2.0f - 1.0f;
      screenspaceXY.y = (1.0f - input.TexCoord.y) * 2.0f - 1.0f;

      // Get the transformed position XY
      transformedPosition.xy = screenspaceXY * PSC_FOV.xy * transformedPosition.z;
    }

    // W is supposed to be 1
    transformedPosition.w = 1.0;
  }

  // Calculate shadow coefficient, sample the particular layer
  half shadowCoef = 1;
  if ((transformedPosition.z >= PSC_ShadowmapLayerBorder.w) && (transformedPosition.z < PSC_ShadowmapLayerBorder.x))
  {
    float3 shadowMap = mul(transformedPosition, PSC_ShadowmapMatrix);
    float4 nVidiaSTRQ = float4(shadowMap, 1);
    shadowCoef = tex2Dlod(samplerShadowMap, nVidiaSTRQ).r;
  }
  else
  {
#ifdef _XBOX
    return tex2D(sampler1, input.TexCoord);
#else
    clip(-1);
#endif
  }
#ifdef _XBOX
  shadowCoef *= tex2D(sampler1, input.TexCoord).r;
#endif

  // Return the shadow coefficient
  return half4(shadowCoef, 0, 0, 1);

//   // Visualise chessboard
//   float2 rounded = round(transformedPosition.xz);
//   return half4(fmod(rounded.x + rounded.y, 2) < 0.5, 0, 0, 1);
}

/////////////////////////////////////////////////////////////////////////////////////////////

half4 PSPostProcessSSSMStencil(VSPP_OUTPUT input) : COLOR
{
#ifdef SSSMBLUR
  return half4(0, 1, 0, 1);
#else
  return half4(0, 0, 0, 1);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

float4 PSPostProcessSSSMBH(VSPP_OUTPUT input) : COLOR
{
  const int gaussSize = 5;
  half gaussV[5] = { 0.1016,  0.25,   0.2969, 0.25,   0.1016};
  float gaussP[5] = {-4,      -2,      0,      2,      4};

  half4 result = half4(0, 0, 0, 1);
  for (int i = 0; i < gaussSize; i++)
  {
    half4 original = (half4)tex2D(sampler0, input.TexCoord + float2(PSC_InvWidth * gaussP[i], 0));
    result.r += original.r * gaussV[i];
    if (i == 0)
    {
      result.g = original.g;
    }
    else
    {
      result.g = min(result.g, original.g);
    }
  }
  return result;
}

float4 PSPostProcessSSSMBV(VSPP_OUTPUT input) : COLOR
{
  const int gaussSize = 5;
  half gaussV[5] = { 0.1016,  0.25,   0.2969, 0.25,   0.1016};
  float gaussP[5] = {-4,      -2,      0,      2,      4};

  half4 result = half4(0, 0, 0, 1);
  for (int i = 0; i < gaussSize; i++)
  {
    half4 original = (half4)tex2D(sampler0, input.TexCoord + float2(0, PSC_InvHeight * gaussP[i]));
    result.r += original.r * gaussV[i];
    if (i == 0)
    {
      result.g = original.g;
    }
    else
    {
      result.g = min(result.g, original.g);
    }
  }
  return result;
}

float4 PSPostProcessSSSMBFinalBlur(VSPP_OUTPUT input) : COLOR
{
  //return tex2D(sampler0, input.TexCoord);
  float4 sssmOriginal = tex2D(sampler0, input.TexCoord);
  float4 sssmBlurred = tex2D(sampler1, input.TexCoord);
  return lerp(sssmOriginal, sssmBlurred, sssmOriginal.g) * float4(2, 1, 1, 1);
}

/////////////////////////////////////////////////////////////////////////////////////////////

half Gauss(half2 sample)
{
  const float value = 1.0f - sqrt(0.5f);
  return value;

  //this is original computation, x and y are now always +-0.5 so doesn't need to compute it 
  //return 1.0f - sqrt(sample.x*sample.x + sample.y*sample.y)/* * 0.5*/;
}


#define BlurScale		PSC_BlurRadius.z

#define invFocalPlane	PSC_PlaneXBlur.x
#define XBlurMul			PSC_PlaneXBlur.y
#define XBlurPower		PSC_PlaneXBlur.z



#ifdef _XBOX
  const float4 linearZ : register(c349);

  // near plane in world coordinates
  #define zNear			linearZ.x

  // far plane in world coordinates
  #define zFar			linearZ.y

  // far - near in world coordinates
  #define zNearSubFar	linearZ.z
#endif



//convert depth from nonlinear space into linear
half ConvertDepth(float depth01)
{	
	#ifdef _XBOX
		// <zNear, zFar> -> <0, 1> = nonlinear depth01 -> need linear <0, 1>
		// zNear/(zFar - depth01*zNearSubFar) is the right transformation, but this is from interval <zNear, zFar>
		// in PC version is depth buffer till 100, thus zNear is multiplied by (zFar - zNear)/(100 [-x]) and saturated

		return saturate(zNear/(zFar - depth01*zNearSubFar));

	#else
		return depth01;
	#endif
}




half4 PSPostProcessDOF(VSPP_OUTPUT input) : COLOR
{

/*    float ddd = ConvertDepth(tex2D(sampler2, input.TexCoord).r);
    half b = saturate(blurRadius.z*abs(ddd - invFocalPlane.x) );
    return half4(b, b, b, 1);
*/

//  if (input.TexCoord.y > 0.5)
//  {
//    return float4(tex2D(sampler0, input.TexCoord).rgb, 1);
//  }
//  else
  {
    //return float4(tex2D(sampler1, input.TexCoord).rgb, 1);
    
    // Poisson samples  
    const int samples = 4;
    const float2 poisson[4] = {float2(0.5, 0.5), float2(0.5, -0.5), float2(-0.5, -0.5), float2(-0.5, 0.5)};

    // Common constants
    const half baseBlur = 0.0f;
    const half smartBlurScale = 5.0f; // Coefficient to influence the blurrines with samples behind the center

	 //additional blur in x, screen01 is 1 at screen vertical edges and 0 in center
	 float screen01 = (abs(input.TexCoord.x - 0.5f))*2.0;
	 float xblur    = pow(screen01, XBlurPower)*XBlurMul;

    // Calculate the depth of the center
    float centerInvDepth = ConvertDepth(tex2D(sampler2, input.TexCoord).r);

    // Calculate the blurFactor
    half blurFactor = 0.5f*(xblur + saturate(BlurScale * abs(centerInvDepth - invFocalPlane.x) + baseBlur));

    // Fetch the color from Hi and Lo textures from the center
    half3 colorHi = tex2D(sampler0, input.TexCoord).rgb;
    half3 colorLo = tex2D(sampler1, input.TexCoord).rgb;

    // Mix Hi and Lo color
    half3 color = lerp(colorHi, colorLo, blurFactor);

    // Center is the only place where weight doesn't depend on blur factor
    half centerWeight = 1.0f;//Gauss(float2(0, 0));

    // Create the center color
    half4 cOut = float4(color * centerWeight, centerWeight);

    // Go through the poisson samples and accumulate the color
    for (int i = 0; i < samples; i++)
    {
      // Texture coordinates for both Hi and Lo textures
      float2 coord = input.TexCoord + (poisson[i] * PSC_BlurRadius.xy);

      // Calculate the depth of the sample
      float sampleInvDepth = ConvertDepth(tex2D(sampler2, coord).r);

      // Calculate the smart blur factor (if sample depth is closer than center depth, then the factor is 1,
      // else it slides linearly to 0 with some coefficient)
      // float smartBlurFactor = (sampleInvDepth >= centerInvDepth) ? 1.0f : 1.0f - (sampleDepth - centerDepth) * smartBlurScale;

		  #ifdef _XBOX
		  	half zdiff = sampleInvDepth - centerInvDepth;
		  #else
		  	half zdiff = centerInvDepth - sampleInvDepth;
		  #endif

      half smartBlurFactor = 1.0f - saturate(smartBlurScale * zdiff);

      // Calculate the sample blur factor
      half blurFactor = 0.5f*(xblur + saturate(BlurScale * abs(sampleInvDepth - invFocalPlane) + baseBlur));

      // Fetch the color from Hi and Lo textures from the sample
      half3 colorHi = tex2D(sampler0, coord);
      half3 colorLo = tex2D(sampler1, coord);

      // Mix Hi and Lo color
      half3 color = lerp(colorHi, colorLo, blurFactor);

      // Weight of the sample depends on blurFactor and on weight of the sample according to it's distance
      // to center (with Gaussian values)
      half sampleWeight = blurFactor * Gauss(poisson[i]) * smartBlurFactor;

      // Add sample color with weight
      cOut.rgb += color * sampleWeight;
      cOut.a += sampleWeight;
    }

    // Normalize the color
    return half4(cOut.rgb / cOut.a, 1);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////

half4 PSPostProcessDistanceDOF(VSPP_OUTPUT input) : COLOR
{
/*    float ddd = ConvertDepth(tex2D(sampler2, input.TexCoord).r);
    half b = saturate(blurRadius.z*abs(ddd - invFocalPlane.x) );
    return half4(b, b, b, 1);
*/

  // Poisson samples  
  const int samples = 4;
  const float2 poisson[4] = {float2(0.5, 0.5), float2(0.5, -0.5), float2(-0.5, -0.5), float2(-0.5, 0.5)};

  // Common constants
  const half baseBlur   = 0.0f;
  
  // Calculate the depth of the center
  float centerInvDepth = ConvertDepth(tex2D(sampler2, input.TexCoord).r);

  // Calculate the blurFactor
  //float blurFactor = saturate(blurScale * (invFocalPlane - centerInvDepth));
  #ifdef _XBOX
    half diff = centerInvDepth - invFocalPlane;
  #else 
    half diff = invFocalPlane - centerInvDepth;
  #endif
  half blurFactor = min(max(BlurScale * diff, 0) + baseBlur, 1);

  // Fetch the color from Hi and Lo textures from the center
  half3 colorHi = tex2D(sampler0, input.TexCoord).rgb;
  half3 colorLo = tex2D(sampler1, input.TexCoord).rgb;

  // Mix Hi and Lo color
  half3 color = lerp(colorHi, colorLo, blurFactor);

  // Center is the only place where weight doesn't depend on blur factor
  half centerWeight = 1.0;//Gauss(float2(0, 0));

  // Create the center color
  half4 cOut = float4(color * centerWeight, centerWeight);

  // Go through the poisson samples and accumulate the color
  for (int i = 0; i < samples; i++)
  {
    // Texture coordinates for both Hi and Lo textures
    float2 coord = input.TexCoord + (poisson[i] * PSC_BlurRadius);

    // Calculate the depth of the sample
    float sampleInvDepth = ConvertDepth(tex2D(sampler2, coord).r);

    // Calculate the sample blur factor
    //float blurFactor = saturate(blurScale * (invFocalPlane - sampleInvDepth));
    #ifdef _XBOX
      diff = centerInvDepth - invFocalPlane;
    #else 
      diff = invFocalPlane - centerInvDepth;
    #endif

    half blurFactor = min(max(BlurScale * diff, 0) + baseBlur, 1);

    // Fetch the color from Hi and Lo textures from the sample
    half3 colorHi = tex2D(sampler0, coord);
    half3 colorLo = tex2D(sampler1, coord);

    // Mix Hi and Lo color
    half3 color = lerp(colorHi, colorLo, blurFactor);

    // Weight of the sample depends on blurFactor and on weight of the sample according to it's distance
    // to center (with Gaussian values)
    half sampleWeight = blurFactor * Gauss(poisson[i]);

    // Add sample color with weight
    cOut.rgb += color * sampleWeight;
    cOut.a += sampleWeight;
  }

  // Normalize the color
  return half4(cOut.rgb / cOut.a, 1);
}

/////////////////////////////////////////////////////////////////////////////////////////////



/*!
samplers binding
*/
#define ssaoScreen       sampler0
#define ssaoSSAO         sampler1
#define ssaoRandTexture  sampler2


//if using different length, it produces more random results, but looking more natural
#define SSAO_DIFF_LENGTH 	

//number of passes, pass is 8 rays sampling
#define SSAO_PASSES 4 

#define ssaoFarMulNear		      PSC_ssaoDistDivParams.x
#define ssaoDeltaRadius		      PSC_ssaoDistDivParams.y
#define ssaoFarRadius		    	  PSC_ssaoDistDivParams.z

#define ssaoInvMaxDistance	  PSC_ssaoMaxMinDist.x
#define ssaoMinDistance		    PSC_ssaoMaxMinDist.y
#define ssaoFarMul				    PSC_ssaoMaxMinDist.z
#define ssaoBilCoef           PSC_ssaoMaxMinDist.w


#define ssaoParamK				    PSC_ssaoParams.x
#define ssaoParamQ				    PSC_ssaoParams.y
#define ssaoIntensity			    PSC_ssaoParams.z


#define ssaoFogEnd            PSC_ssaoFogParams.x
#define ssaoInvFogDelta       PSC_ssaoFogParams.y
#define ssaoFogExp            PSC_ssaoFogParams.z


/*!
rays
*/
#define SSAO_MAX_RAYS		8

#define ssaoStep 			1.0/SSAO_MAX_RAYS
#define ssaoV    			1.4142

static const half2 ssao_ray[SSAO_MAX_RAYS] = 
{
	#ifdef SSAO_DIFF_LENGTH			
		half2(-1.0f,   0.0f)*ssaoStep*4,
		half2(-ssaoV, ssaoV)*ssaoStep*4.5,
		half2( 0.0f,   1.0f)*ssaoStep*5,
		half2(ssaoV,  ssaoV)*ssaoStep*5.5,
		half2( 1.0f,   0.0f)*ssaoStep*6,
		half2(ssaoV, -ssaoV)*ssaoStep*6.5,
		half2( 0.0f,  -1.0f)*ssaoStep*7.5,
		half2(-ssaoV, ssaoV)*ssaoStep*8
	#else             
		half2(-1.0f,  0.0f),
		half2(-ssaoV, ssaoV),
		half2( 0.0f,  1.0f),
		half2( ssaoV, ssaoV),
		half2( 1.0f,  0.0f),
		half2(ssaoV, -ssaoV),
		half2( 0.0f, -1.0f),
		half2(-ssaoV, ssaoV)
	#endif   
};


/*!
get random ray 
*/
half2 ssaoGetRandRay(int index, const half4 rotmatrix)
{
	return half2(dot(ssao_ray[index], rotmatrix.xy), dot(ssao_ray[index], rotmatrix.zw));
}


/*!
get depth 
*/
half ssaoGetDepth(const half2 center, const half2 offset)
{
	half2 texcoord = center + offset;
	//return 1 - tex2Dlod(ssaoScreen, half4(texcoord, 0, 0)).x;
  return tex2Dlod(ssaoScreen, texcoord.xyyy).x;
}


/*!
one packet of rays
*/
float4 ssaoGetSum4(
	int starti, 
	const float2 center, 
	const half4 rotmatrix, 
	const float2 xyscale, 
	const float4 centerdepth,
	const float4 testeddepth)
{
	float4 currdepth;

	currdepth.x  	= ssaoGetDepth(center, xyscale*ssaoGetRandRay(starti + 0, rotmatrix));
	currdepth.y  	= ssaoGetDepth(center, xyscale*ssaoGetRandRay(starti + 1, rotmatrix));
	currdepth.z  	= ssaoGetDepth(center, xyscale*ssaoGetRandRay(starti + 2, rotmatrix));
	currdepth.w  	= ssaoGetDepth(center, xyscale*ssaoGetRandRay(starti + 3, rotmatrix));

	//current pixel is farther than centerpixel
	float4 mask 	= (currdepth > testeddepth) ? (float4) 1 : (float4) 0;
	float4 diff 	= currdepth - centerdepth;

// 	float4 mask 	= (currdepth < testeddepth) ? (float4) 1 : (float4) 0;
//	float4 diff 	= centerdepth - currdepth;
 
  
	//FarMul is multiplier, where at "fardist" the function is 0.025, thus almost zero
  float4 diff2  = diff*diff;
	float4 dist		= 1/(1 + ssaoFarMul*diff2);
	float4 val		= saturate(dist);

	//pow4 fallof
	//float4 dist01 	= diff*InvMaxDistance;
	//float4 dist		= enf_pow4(dist01);
	//float4 val		= saturate(1.0 - dist);
	
	return val*mask;		
}


/*!
smooth step
*/
float ssao_smoothstep(float x)
{
  float x2 = x*x;	
  return 3*x2 - 2*x*x2; 
}



/*!
pixel intensity recalculation
*/
float ssaoRecalcPixel(half pixel)
{
	float ssao = ssao_smoothstep(saturate(pixel*ssaoParamK + ssaoParamQ));
	
	return saturate(ssaoIntensity*ssao*ssao);
}


/*!
fog (VFogAlpha)
*/
float SSAOFog(float d)
{
  //linear -> return 1 - saturate(1.0 * (ssaoFogEnd - d) * ssaoInvFogDelta);
  return 1 - exp(d * ssaoFogExp);
}



/*!
SSAO
*/
half4 PSPostProcessSSAO(VS_SSAO input) : COLOR
{
	#define CenterPosition 		input.m_vTexCoord.xy
	#define NoisePosition		  input.m_vTexCoord.zw

	//center depth, 1 is at camera, 0 is 200m far (engine setting)
	float4 centerdepth = ssaoGetDepth(CenterPosition, 0);
	half4 testeddepth  = centerdepth + ssaoMinDistance;

	//final influence according the distance
	half infl = saturate(1 - 50.0*centerdepth.x);

	//pixel scale according the distance with 1/z function
	float2 xyscale = ssaoFarMulNear/(ssaoDeltaRadius*(1-centerdepth.x) + ssaoFarRadius);
	xyscale	*= PSC_ssaoPixelOffset;
	
  float2 next = float2(0.125, 0.0);
  
	//for each pass
	float4 sum = 0.0;
	
	[unroll] for (int currpass = 0; currpass < SSAO_PASSES; currpass++)
	{
		//random ray
		half4 rotmatrix = tex2Dlod(ssaoRandTexture, half4(NoisePosition + next*currpass, 0, 0))*2 - 1;

		sum += ssaoGetSum4(0, CenterPosition, rotmatrix, xyscale, centerdepth, testeddepth);
		sum += ssaoGetSum4(4, CenterPosition, rotmatrix, xyscale, centerdepth, testeddepth);	
		
		xyscale *= (1 - 1.0/SSAO_PASSES);
	}
	
	float ssum = dot(sum, 1)/(SSAO_MAX_RAYS*SSAO_PASSES);
	
	return lerp(1 - ssaoRecalcPixel(ssum), 1, saturate(infl + SSAOFog(1 - centerdepth.x)));
}



/*!
size of kernel to both sides
*/
#define SSAO_KERNEL_SIZE  3

/*!
bilateral filtering of depth to avoid white pixel around edges, controlled by depthtest now
*/
#define SSAO_DEPTH


/*!
blur
*/
half4 SSAOBlur(const float4 Pixel, bool vertical, bool depthtest)
{
  #define DepthCenter   Pixel.zw
  #define SSAOCenter    Pixel.xy

	float4 ssao1 = 0, ssao2 = 0;
	float2 pixcoord;

	#ifdef SSAO_DEPTH
		float4 depthvec1 = 0, depthvec2 = 0, centerdepth = depthtest ? tex2Dlod (ssaoScreen, Pixel.zwww).xxxx + ssaoMinDistance : 0;
		float2 depthcoord;
    
    depthvec2.w = 1.0f;
	#endif
	
	//sample ssao values
	int j = 0;
	[unroll] for (int i = -3; i <= 3; i++, j++)
	{
		half2 offs;
		if (vertical)
			offs = half2(0, i);
		else
			offs = half2(i, 0);
		
		pixcoord	  = SSAOCenter  + PSC_ssaoPixelOffset.xy*offs;
		float ssao = tex2Dlod(ssaoSSAO, pixcoord.xyyy).x;
		
		if (j == 0)	ssao1.x = ssao; else
		if (j == 1) ssao1.y = ssao; else
		if (j == 2) ssao1.z = ssao; else
		if (j == 3) ssao1.w = ssao; else
		if (j == 4) ssao2.x = ssao; else
		if (j == 5) ssao2.y = ssao; else
		if (j == 6) ssao2.z = ssao; 
		
		#ifdef SSAO_DEPTH
    if (depthtest)
    {
   		depthcoord  = DepthCenter + PSC_ssaoPixelOffset.zw*offs;
   		float depth = tex2Dlod(ssaoScreen, depthcoord.xyyy).x;

			if (j == 0)	depthvec1.x = depth; else
			if (j == 1) depthvec1.y = depth; else
			if (j == 2) depthvec1.z = depth; else
			if (j == 3) depthvec1.w = depth; else
			if (j == 4) depthvec2.x = depth; else
			if (j == 5) depthvec2.y = depth; else
			if (j == 6) depthvec2.z = depth; 		
    } 
		#endif
	}
 
	float4 mask1 = 1, mask2 = 1;
	float divider = 1/7.0f;
	#ifdef SSAO_DEPTH
  if (depthtest)
  {
		float4 coef = centerdepth < depthvec1 ? (float4) ssaoBilCoef : 0.0f;
		mask1 = 1/(1 + coef*sqr4(centerdepth - depthvec1));

		coef  = centerdepth < depthvec2 ? (float4) ssaoBilCoef: 0.0f;
		mask2 = 1/(1 + coef*sqr4(centerdepth - depthvec2));
		
		divider = 1.0/(dot(mask1, 1) + dot(mask2, float4(1, 1, 1, 0)));
  }  
	#endif

	float sum = dot(ssao1*mask1, 1) + dot(ssao2*mask2, 1);
	
	return sum*divider;  
  
  
/*  
	#ifdef SSAO_DEPTH
		float centerdepth = depthtest ? tex2D (ssaoScreen, DepthCenter) + ssaoMinDistance : 0;
	#endif

	float sum = 0;
	float sub = 0;
	float coefssum = 0;
	float4 pixcoord = (float4) 0;
	float4 depthcoord = (float4) 0;
	
	[unroll] for (int i = -SSAO_KERNEL_SIZE; i <= SSAO_KERNEL_SIZE; i++)
	{
		half2 offs = vertical ? half2(0, i) :  half2(i, 0);
		
		float coef = 1;

		pixcoord.xy = SSAOCenter  + ssaoPixelOffset.xy*offs;
    float ssao  = tex2Dlod(ssaoSSAO, pixcoord);
		
		//bilateral filtering according the depth, new pixel is in front of center pixel
		#ifdef SSAO_DEPTH
      if (depthtest)
    	{
        depthcoord.xy	= DepthCenter + ssaoPixelOffset.zw*offs;
        float depth   = tex2Dlod(ssaoScreen, depthcoord);
      
        if (centerdepth < depth)
        { 
          float zdiff = (centerdepth - depth);
          coef = 1/(1 + ssaoBilCoef*zdiff*zdiff);
        }
      }
		#endif
		
		coefssum += coef;
		sum 		 += coef*ssao;
	}

	return (half4) saturate(sum/coefssum);
*/  
}



/*!
vertical blur
*/
half4 PSPostProcessSSAOBlurVert(VS_SSAO input) : COLOR
{
	return SSAOBlur(input.m_vTexCoord , true, false);
}

half4 PSPostProcessSSAOBlurVertDepth(VS_SSAO input) : COLOR
{
	return SSAOBlur(input.m_vTexCoord, true, true);
}

/*!
horizontal blur
*/
half4 PSPostProcessSSAOBlurHorz(VS_SSAO input) : COLOR
{
  return SSAOBlur(input.m_vTexCoord, false, false);
}

half4 PSPostProcessSSAOBlurHorzDepth(VS_SSAO input) : COLOR
{
  return SSAOBlur(input.m_vTexCoord, false, true);
}


/*!
final, just return
*/
half4 PSPostProcessSSAOFinal(VS_SSAO input) : COLOR
{
  return (half4) tex2D(ssaoSSAO, input.m_vTexCoord.xy).x;
}

half4 PSPostProcessSSAODebug(VS_SSAO input) : COLOR
{
  return (half4) tex2D(ssaoSSAO, input.m_vTexCoord.xy).x*0.5;
}



/*!
SSAO downsample pixel shader 
*/
half4 PSPostProcessSSAODown(VS_SSAODown input) : COLOR
{
	half4 color = 0.25*(
		tex2Dlod(ssaoScreen, input.m_vTex1Tex2.xyyy) + 
		tex2Dlod(ssaoScreen, input.m_vTex1Tex2.zwww) + 
		tex2Dlod(ssaoScreen, input.m_vTex3Tex4.xyyy) + 
		tex2Dlod(ssaoScreen, input.m_vTex3Tex4.zwww));

	return color;
}



/////////////////////////////////////////////////////////////////////////////////////////////

#define ScreenNoiseScale		PSC_ScreenNoise.xy
#define ScreenNoiseOffset		PSC_ScreenNoise.zw

/*!
parameters of effect
*/
#define Intensity         PSC_FilmGrainParams.x
#define Sharpness         PSC_FilmGrainParams.y
#define GrainSize         PSC_FilmGrainParams.z


#define LineK             PSC_FilmGrainParams2.x
#define LineQ             PSC_FilmGrainParams2.y


/*!
compute noise
\param noise	noise from texture in <0, 1>
*/
half3 ComputeFilmGrain(half3 noise, half3 screensample)
{
  //noise is uniformly distributed from <0, 1>, convert to <-1.0, 1.0>
  noise = (noise - 0.5)*2.0;

  //make power of absolute value
  half3 pownoise = pow (abs(noise), Sharpness);

  //back to <-1.0, 1.0>
  noise = sign(noise)*pownoise;

  //apply intensity 
  noise *= Intensity;

  //noise more visible in black, make quadtratic function
  //half3 blackmul  = saturate(BlackOffset - screensample);
  //noise *= blackmul*blackmul;

  //now, try to use linear interpolation, but the line is user specific
  const half3 lumcoef = half3(0.299, 0.587, 0.114);
  half lum            = dot(lumcoef, screensample);
  half lumintensity   = saturate(lum*LineK + LineQ);

  noise *= lumintensity;

  return noise;
}


/*!
color noise
*/
half4 PSPostProcessFilmGrainColor(VSPP_OUTPUT input) : COLOR
{
  half4 screensample 	= tex2D (sampler0, input.TexCoord);
  half3 noise 			= tex2Dlod (sampler1, float4(input.TexCoord*ScreenNoiseScale + ScreenNoiseOffset, 0, GrainSize));

  noise = ComputeFilmGrain(noise, screensample.rgb);
  return half4(screensample.rgb + noise, screensample.a);
}


/*!
monochromatic noise
*/
half4 PSPostProcessFilmGrainMono(VSPP_OUTPUT input) : COLOR
{
  half4 screensample 	= tex2D (sampler0, input.TexCoord);
  half3 noise 			= tex2Dlod (sampler1, float4(input.TexCoord*ScreenNoiseScale + ScreenNoiseOffset, 0, GrainSize));

  // .xxx is important, the compiler optimize the code and the pow function is called just once
  //for one channel instead for each of them -> about 7 instructions less
  noise = ComputeFilmGrain(noise, screensample.rgb).xxx;

  return half4(screensample.rgb + noise, screensample.a);
}



/////////////////////////////////////////////////////////////////////////////////////////////


/// calculate and downsample luminace
/**
input: HDR image (RGB respresents color)
output: luminance in all channels
*/
half4 PSPostProcessDownSampleLuminance(VSPP4T_OUTPUT input) : COLOR
{
  // note: PS 2.0 have 16 texture samplers
  // using this and bilinear filtering we could perform 8x8 downsampling in one pass
  // this would require some offset calculation in the shader
  half3 x1 = tex2D(sampler0, input.TexCoord0);
  half3 x2 = tex2D(sampler1, input.TexCoord1);
  half3 x3 = tex2D(sampler2, input.TexCoord2);
  half3 x4 = tex2D(sampler3, input.TexCoord3);
#if 1 //_VBS3 // Hotfix - input values are sometimes very wrong (infinite?) which break the whole downsizing and avg/max color determination
  x1 = saturate(x1);
  x2 = saturate(x2);
  x3 = saturate(x3);
  x4 = saturate(x4);
#endif
  // note: this is daytime sensitivity, night time would be different (blue shift)
  half3 eyeSensitivityD4 = half3(0.299f/4, 0.587f/4, 0.114f/4);
  half luminance = dot(x1+x2+x3+x4,eyeSensitivityD4);
  // make sure the result fits in the half range
  luminance = min(luminance,MAX_HALF_4);
  return half4(luminance.xxx,1);
  // debugging - preserve colors from original image
  //return float4((x1+x2+x3+x4)*0.25,luminance);
}

/// calculate and downsample luminace, where source is encoded temperature
/**
output: luminance in all channels
*/
half4 PSPostProcessDownSampleThermalLuminance(VSPP4T_OUTPUT input) : COLOR
{
  // Get encoded temperature and scale it to <0,1> interval
  half x1 = dot(tex2D(sampler0, input.TexCoord0).rgb, 1) * 1.0f / 3.0f;
  half x2 = dot(tex2D(sampler1, input.TexCoord1).rgb, 1) * 1.0f / 3.0f;
  half x3 = dot(tex2D(sampler2, input.TexCoord2).rgb, 1) * 1.0f / 3.0f;
  half x4 = dot(tex2D(sampler3, input.TexCoord3).rgb, 1) * 1.0f / 3.0f;

  // Calculate the average temperature and return it
  half luminance = (x1 + x2 + x3 + x4) * 1.0f/4.0f;
  luminance = min(luminance,MAX_HALF_4);
  return half4(luminance.xxx,1);
}

/// luminance downsampling shader
/**
input: average in all components (only r really used)
output: average in all components
*/

half4 PSPostProcessDownSampleAvgLuminance(VSPP4T_OUTPUT input) : COLOR
{
  half4 x1 = tex2D(sampler0, input.TexCoord0);
  half4 x2 = tex2D(sampler1, input.TexCoord1);
  half4 x3 = tex2D(sampler2, input.TexCoord2);
  half4 x4 = tex2D(sampler3, input.TexCoord3);
  half luminance = (x1.r+x2.r+x3.r+x4.r)*0.25;
  return half4(luminance.xxx,1);
  // debugging - preserve colors from original image
  //return half4((x1.rgb+x2.rgb+x3.rgb+x4.rgb)*0.25,luminance);
}

/// luminance gathering shader
/**
Downsample for average, min and max luminance in r,g,b
Assume input has the same structure (avg,min,max) in r,g,b

Point sampling should be used here - min/max cannot be interpolated
*/

half4 PSPostProcessDownSampleMaxAvgMinLuminance(VSPP4T_OUTPUT input) : COLOR
{
  half3 x1 = tex2D(sampler0, input.TexCoord0);
  half3 x2 = tex2D(sampler1, input.TexCoord1);
  half3 x3 = tex2D(sampler2, input.TexCoord2);
  half3 x4 = tex2D(sampler3, input.TexCoord3);
  return half4(
    max(max(x1.r,x2.r),max(x3.r,x4.r)),
    (x1.g+x2.g+x3.g+x4.g)*0.25,
    min(min(x1.b,x2.b),min(x3.b,x4.b)),
    1
  );
}

float AssumedLuminance(float3 maxAvgMin)
{
  // use mostly maximum to avoid overexposure
  // let average value have some influence as well
  // desiredLuminance tells us where we want the max. luminace to be
  // average luminace should probably be something like half of this
  // The result must not be 0, because it will be used as a divisor
  // - that's why epsilon is introduced
  float maxFactor = 0.9;
  float epsilon = 0.0001f;
  return max(maxAvgMin.r*maxFactor+maxAvgMin.g*2*(1-maxFactor), epsilon);
}

/*
  To transform from last frame space to this frame space we need:
    relValueLastFrame*eyeAccomOld/eyeAccom


/// calculate "assumed" luminance value based on avg/min/max
/**
  constant: deltaT - frameTime
  constant: eyeAccomOld/eyeAccom - convert from last frame to this frame

  sampler0: 1x1 texture - current avg/min/max in this frame space
  sampler1: 1x1 texture - aperture used for the last frame in last frame space
  
  To get absolute value we need to use relValue*eyeAccom
  
  This shader works in full precision.
*/

float4 PSPostProcessAssumedLuminance(VSPP4T_OUTPUT input) : COLOR
{
  float eyeAccomLastToThis = PSC_AssumedLuminancePars1[0];
  //float deltaT = PSC_AssumedLuminancePars1[1];
  float desiredLuminance = PSC_AssumedLuminancePars1[2];
  
  float eyeAccomMin = PSC_AssumedLuminancePars2[0];
  float eyeAccomMax = PSC_AssumedLuminancePars2[1];
  float minRatio = PSC_AssumedLuminancePars2[2];
  float maxRatio = PSC_AssumedLuminancePars2[3];
  
  float3 maxAvgMin = tex2D(sampler0, input.TexCoord0);
  
  // check for any possible overflow
  float maxAperture = 1e3;
  // when any overflow is detected, truncate
  if (!(maxAvgMin.r<maxAperture)) maxAvgMin.r = maxAperture;
  if (!(maxAvgMin.g<maxAperture)) maxAvgMin.g = maxAperture;
  if (!(maxAvgMin.b<maxAperture)) maxAvgMin.b = maxAperture;
  
  float assumedLuminance = AssumedLuminance(maxAvgMin);
  
  // sample previous aperture texture
  float newEyeAccom = desiredLuminance/assumedLuminance;
  if (eyeAccomLastToThis>0)
  {
    // last aperture valid - update it
    // caculate desired apreture (in current render target space)
    // this is desired aperture = desired luminance / measure luminance
    // precision is critical here - partial precision not enough

    float lastFrameEyeAccom = DecodeApertureChange(tex2D(sampler1, float2(0,0)).x)*eyeAccomLastToThis;
    float ratio = newEyeAccom/lastFrameEyeAccom;

    float logRatio = abs(log2(ratio));
    // for small ratios make the change very slow
    if (logRatio<1)
    {
      logRatio = lerp(0,logRatio,logRatio);
    }
    float maxChange = exp2(logRatio);
    ratio = max(-maxChange,min(ratio,maxChange));
    // perform change based on deltaT and lastFrameAperture here
    ratio = max(minRatio,min(ratio,maxRatio));

    newEyeAccom = lastFrameEyeAccom*ratio;
  }
  // saturate into a valid range
  newEyeAccom = max(eyeAccomMin,min(newEyeAccom,eyeAccomMax));

  // if there is any NAN or infinity, make sure we truncate it to prevent spreading into following frames
  return float4(EncodeApertureChange(newEyeAccom).xxx,1);
}

/// Simplified version of PSPostProcessAssumedLuminance
/**
if there is no float render target support,
we cannot calculate change on the GPU
*/
half4 PSPostProcessAssumedLuminanceDirect(VSPP4T_OUTPUT input) : COLOR
{
  half desiredLuminance = PSC_AssumedLuminancePars1[2];
  
  half eyeAccomMin = PSC_AssumedLuminancePars2[0];
  half eyeAccomMax = PSC_AssumedLuminancePars2[1];
  
  half3 maxAvgMin = tex2D(sampler0, input.TexCoord0);
  
  half assumedLuminance = AssumedLuminance(maxAvgMin);
  
  // sample previous aperture texture
  half newEyeAccom = desiredLuminance/assumedLuminance;
  // saturate into a valid range
  newEyeAccom = max(eyeAccomMin,min(newEyeAccom,eyeAccomMax));
  return half4(EncodeApertureChange(newEyeAccom).xxx,1);
}

/// apply night blue shift + black and white vision
half3 NightBlueShift(half3 c, half3 maxAvgMin)
{
  half4 intensity;
  intensity.a = dot(c, PSC_RgbEyeCoef.rgb);
  intensity.rgb = c;
  
  half4 factor = saturate(intensity*PSC_NightControl.x + PSC_NightControl.y);
  // players (and Holywood) like the night blue - let them have it (to certain extent)
  half3 color = half3(intensity.a*0.95, intensity.a*0.95, intensity.a*1.05)*(1-factor.a) + c*factor.rgb;
  return ToneMapping(color,intensity.a,maxAvgMin);
}

/**
textures:
  sampler 0 - source image
  sampler 1 - glow image
  sampler 2 - aperture modificator (1x1)
  sampler 3 - 1x1 texture - current avg/min/max in this frame space
*/

half4 PSPostProcessGlow(VSPP4T_OUTPUT input) : COLOR
{
  half4 cOriginal = tex2D(sampler0, input.TexCoord0);
  half4 cGlow = tex2D(sampler1, input.TexCoord1);
  
  float aperture = ApertureControl(tex2D(sampler2,float2(0,0)).x);
  
  // read min/max/avg, transform it into the new color space
  half3 maxAvgMin = tex2D(sampler3,float2(0,0))*aperture;


  // original color, modified by aperture
  half3 colorOrigAper = cOriginal.rgb * aperture;
  // luminance
  half lum = saturate(colorOrigAper.r*0.299+colorOrigAper.g*0.587+colorOrigAper.b*0.114);

  // we modify glow value by luminance, so that we do not overbright already bright areas
  half3 colorWGlow = (1 - lum) * cGlow.rgb * PSC_BloomPars.x + cOriginal.rgb*PSC_BloomPars.y;

  half3 color = colorWGlow.rgb*aperture;
 
  return half4(ToneMapping(color, dot(color, PSC_RgbEyeCoef.rgb),maxAvgMin),cOriginal.a);
}

half4 PSPostProcessGlowNight(VSPP4T_OUTPUT input) : COLOR
{
  half4 cOriginal = tex2D(sampler0, input.TexCoord0);
  half4 cGlow = tex2D(sampler1, input.TexCoord1);
  float aperture = ApertureControl(tex2D(sampler2,float2(0,0)).x);
  half3 maxAvgMin = tex2D(sampler3,float2(0,0))*aperture;
  return half4(NightBlueShift(max(cOriginal,cGlow)*aperture,maxAvgMin),cOriginal.a);
}

/// B&W, transformed to a single color

half4 PSPostProcessNVG(VSPP4T_OUTPUT input) : COLOR
{
  half4 cOriginal = tex2D(sampler0, input.TexCoord0);
  half4 cGlow = tex2D(sampler1, input.TexCoord1);

  float aperture = ApertureControl(tex2D(sampler2,float2(0,0)).x);
  half3 afterAperture = max(cOriginal.rgb,cGlow.rgb)*aperture;
  half3 maxAvgMin = tex2D(sampler3,float2(0,0))*aperture;

  half intensity = dot(afterAperture, PSC_RgbEyeCoef.rgb);
  
  // simulate different response in different RGB components
  //half3 color = half3(intensity*0.3, intensity*1, intensity*0.1); // yellowish
  half3 color = half3(intensity*0.15, intensity*1, intensity*0.2); // cyanish

  // used by _VBS3_TI
  //half3 color = cOriginal.rgb;

  return half4(color,cOriginal.a);
}


////////////////////////////////////////////////////////////////////////////////////////////////////

//==================================================================================================
// POST PROCESS RotBlur
//==================================================================================================

//==================================================================================================
float4 PsPpRotBlur( VSPP_OUT i ) : COLOR
{
#if _SM2
  return tex2D( sampler0, i.tc0.xy );
#else
  const int STEPS = 7;
  half4 final;
  half zt = i.tc0.z; // depthThreshold
  half2 dir0 = i.tc1.xy;
  half2 tcP = i.tc0.xy;
  half2 tcN = i.tc0.xy;
  half z0 = tex2D( sampler1, i.tc0.xy ).r;
  half3 accu = tex2D( sampler0, i.tc0.xy ).rgb;
  half d = 1;
  half z1, z2;

  #ifdef _XBOX

	  if( z0 > zt )
	  {
		 for( int i = 0; i < STEPS; i++ )
		 {
			tcP += dir0;
			if( tex2D( sampler1, tcP ).r > zt )
			{
			  accu += tex2D( sampler0, tcP ).rgb;
			  d += 1;
			}
			tcN -= dir0;
			if( tex2D( sampler1, tcN ).r > zt )
			{
			  accu += tex2D( sampler0, tcN ).rgb;
			  d += 1;
			}
		 }
	  }

  #else

    if( z0 < zt )
    {
      [unroll]
      for( int i = 0; i < STEPS; i++ )
      {
        tcP += dir0;
        if( tex2D( sampler1, tcP ).r < zt )
        {
          accu += tex2D( sampler0, tcP ).rgb;
          d += 1;
        }
        tcN -= dir0;
        if( tex2D( sampler1, tcN ).r < zt )
        {
          accu += tex2D( sampler0, tcN ).rgb;
          d += 1;
        }
      }
    }

  #endif

  final.a = 1;
  final.rgb = accu.rgb / d;

  return final;
#endif
}


//==================================================================================================
float4 PsPpRadialBlur( VSPP_OUTPUT i ) : COLOR
{
  // number of steps (number of sampled texels)
  static const int RadialBlurSteps = 11;

  // original tex coord
  half2 tc = i.TexCoord.xy;

  // dir from center to our pixel
  half2 dir = 0.5f - tc;

  // border coefficient (0 = center, 1 = border)
  half borderCoef = saturate(max(abs(dir.x), abs (dir.y)) * 2.0f);

  // we want to shift the center of the kernel based on the distance from center of the screen (to avoid artefacts near center and screen borders)

  // compute start step
  // first compute middle step value
  half startStep = (RadialBlurSteps-1)/2;
  // modify it by border coef
  startStep *= (1.0f - borderCoef*borderCoef*borderCoef);
  // modify by strength
  startStep *= -PSC_RadialBlurPars.x;
   
  // "tranformed" direction - used to determine if we are in the area specified by offsets (=non blur area)
  half2 dirTrans = dir;

  // distance from center
  half dist = length(dir);
  // normalize
  dir = dir/(dist + 0.001);

  // compute transformed direction
  // modify it so that we can compute with unit circle instead of ellipse
  dirTrans.x /= (PSC_RadialBlurPars.z + 0.001);
  dirTrans.y /= (PSC_RadialBlurPars.w + 0.001);
  half distTrans = length(dirTrans);

  // coeff of blur
  // we want this coeff to be 0 inside of ellipse given by radialBlurPars.zw
  // near the edges we want the ellipse outside of rectangle (=radius oc circle sqrt(2) = 1.4142), near the center we want to be inside of rectangle (radius = 1)
  float radius = 1.0f + 0.4142f * max(PSC_RadialBlurPars.z, PSC_RadialBlurPars.w)*2.0f;
  float coef = saturate(distTrans - radius );
  // modify by distance
  coef = dist * coef;

  // compute color
  half3 accum = 0;

  for (int i=0; i<RadialBlurSteps; ++i)
  {
    half2 coords = tc + dir*startStep*coef;
    accum += tex2D(sampler0, coords).rgb;
    startStep += PSC_RadialBlurPars.x;
  }

  accum /= RadialBlurSteps; 
  return float4(accum.rgb, 1);
}



//==================================================================================================
float4 PsPpColors( VSPP_OUT i ) : COLOR
{
  half4 final;
  half3 src = tex2D( sampler0, i.tc0.xy ).rgb;
// CONTRAST + OFFSET
  src.rgb = saturate( src.rgb * PSC_PPColorsPars.y + PSC_PPColorsPars.z );
  // 
  half mono = src.r * PSC_RGBWeights.r + src.g * PSC_RGBWeights.g + src.b * PSC_RGBWeights.b;
  half3 mono3 = mono * PSC_ColorizeColor.rgb;
// SATURATION / COLORIZATION
  final.rgb = lerp( mono3.rgb, src.rgb, PSC_ColorizeColor.a );
// BRIGHTNESS
  half brightness = PSC_PPColorsPars.x;
  final.rgb *= saturate( brightness );
  final.rgb = lerp( final.rgb, half3( 1.0, 1.0, 1.0 ), saturate( brightness - 1.0 ) );
// COLOR BLEND
  final.rgb = lerp( final.rgb, PSC_LerpColor.rgb, PSC_LerpColor.a );
  
  final.a = 1;
  return final;
}

//==================================================================================================
// version of collor correction shader with radial (ellipsoid) mask
float4 PsPpColorsRadial( VSPP_OUT i ) : COLOR
{
  // original value
  half3 orig = tex2D( sampler0, i.tc0.xy ).rgb;; 

// CONTRAST + OFFSET
  half3 src = saturate( orig.rgb * PSC_PPColorsPars.y + PSC_PPColorsPars.z );
  // 
  half mono = src.r * PSC_RGBWeights.r + src.g * PSC_RGBWeights.g + src.b * PSC_RGBWeights.b;
  half3 mono3 = mono * PSC_ColorizeColor.rgb;
// SATURATION / COLORIZATION
  half4 final;
  final.rgb = lerp( mono3.rgb, src.rgb, PSC_ColorizeColor.a );
// BRIGHTNESS
  half brightness = PSC_PPColorsPars.x;
  final.rgb *= saturate( brightness );
  final.rgb = lerp( final.rgb, half3( 1.0, 1.0, 1.0 ), saturate( brightness - 1.0 ) );
// COLOR BLEND
  final.rgb = lerp( final.rgb, PSC_LerpColor.rgb, PSC_LerpColor.a );

  // compute radial mask

  //PSC_PPColorRadPars1: x..center x, y..center y, z..x cord scale, w..y coord scale
  //PSC_PPColorRadPars2: x..sin of angle, y..cos of angle, z..inner radius coef, w..color interpolation coef

  // compute point from tex coords and move by center
  half2 pointTrans = i.tc0.xy - 0.5f - PSC_PPColorRadPars1.xy;
  // rotate point
  pointTrans = float2(pointTrans.x*PSC_PPColorRadPars2.y - pointTrans.y*PSC_PPColorRadPars2.x, pointTrans.y*PSC_PPColorRadPars2.y + pointTrans.x*PSC_PPColorRadPars2.x);
  // scale
  pointTrans = float2(pointTrans.x * PSC_PPColorRadPars1.z, pointTrans.y * PSC_PPColorRadPars1.w);

  float size = length(pointTrans);

  // compute interp coef
  float coef = saturate((saturate(size-PSC_PPColorRadPars2.z)) / (1.0f - PSC_PPColorRadPars2.z));
  // smooth interpolation with sinus function
  coef = 0.5f * (sin( (coef-0.5f) * 3.14 ) + 1);
  // apply interpolation power coef
  coef = pow(coef, PSC_PPColorRadPars2.w);
   
  // compute final color
  final.rgb = lerp(orig, final.rgb, coef);
  final.a = 1;
  return final; 
}

//==================================================================================================
float4 PsPpChromAber( VSPP_OUTPUT i ) : COLOR
{
  // color masks for samples
  const half3 mask1 = half3( 0.8, 0.2, 0.0 );
  const half3 mask2 = half3( 0.2, 0.6, 0.2 );
  const half3 mask3 = half3( 0.0, 0.2, 0.8 );
  //
  half4 final;
  half2 tc = i.TexCoord.xy;
  half2 dir = tc - 0.5;
  dir.xy *= PSC_ChromAbberPars.xy;
  //dir.y *= projConsts.z; // aspect correction
  //dir.x *= projConsts.x; // aspect correction
  
  final.rgb = tex2D( sampler0, tc - dir ).rgb * mask1.rgb;
  final.rgb += tex2D( sampler0, tc ).rgb * mask2.rgb;
  final.rgb += tex2D( sampler0, tc + dir ).rgb * mask3.rgb;
  final.a = 1;
  return final;
}

//==================================================================================================

float4 PsPpClrInversion(VSPP_OUTPUT i): COLOR0
{
	float3 clr = abs(PSC_ClrInversPars.xyz - tex2D(sampler0, i.TexCoord.xy).xyz);
	return float4(saturate(clr.xyz), 1);
}

//==================================================================================================



//==================================================================================================
float4 PsPpWetDistort( VSPP_OUT i ) : COLOR
{
  half4 final;
  half2 tc = i.tc0.xy;
  half2 dtc = i.tc1.xy;
  
  half3 src;
  src.r = tex2D( sampler0, tc + dtc * PSC_PPWDAberration.x ).r;
  src.g = tex2D( sampler0, tc + dtc * PSC_PPWDAberration.y ).g;
  src.b = tex2D( sampler0, tc + dtc * PSC_PPWDAberration.z ).b;
  
  half3 blur;
  blur.r = tex2D( sampler1, tc + dtc * PSC_PPWDAberration.x ).r;
  blur.g = tex2D( sampler1, tc + dtc * PSC_PPWDAberration.y ).g;
  blur.b = tex2D( sampler1, tc + dtc * PSC_PPWDAberration.z ).b;
  
  final.rgb = lerp( src.rgb, blur.rgb, i.tc0.w );
  final.a = 1;
  return final;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// DYNAMIC BLUR
////////////////////////////////////////////////////////////////////////////////////////////////////

//==================================================================================================
float4 PsPpDynamicBlur( VSPP_OUT i ) : COLOR
{
  half4 final;
  float4 lim = i.tc6.xyzw;
  float4 tc0 = min( i.tc0, lim );
  float4 tc1 = min( i.tc1, lim );
  float4 tc2 = min( i.tc2, lim );
  float4 tc3 = min( i.tc3, lim );
  float4 tc4 = min( i.tc4, lim );
  float4 tc5 = min( i.tc5, lim );
  final = tex2D( sampler0, tc0.xy ) * PSC_PPDynBlurWeights[0].x;
  final += tex2D( sampler0, tc1.xy ) * PSC_PPDynBlurWeights[1].x;
  final += tex2D( sampler0, tc1.zw ) * PSC_PPDynBlurWeights[1].x;
  final += tex2D( sampler0, tc2.xy ) * PSC_PPDynBlurWeights[2].x;
  final += tex2D( sampler0, tc2.zw ) * PSC_PPDynBlurWeights[2].x;
  final += tex2D( sampler0, tc3.xy ) * PSC_PPDynBlurWeights[3].x;
  final += tex2D( sampler0, tc3.zw ) * PSC_PPDynBlurWeights[3].x;
  final += tex2D( sampler0, tc4.xy ) * PSC_PPDynBlurWeights[4].x;
  final += tex2D( sampler0, tc4.zw ) * PSC_PPDynBlurWeights[4].x;
  final += tex2D( sampler0, tc5.xy ) * PSC_PPDynBlurWeights[5].x;
  final += tex2D( sampler0, tc5.zw ) * PSC_PPDynBlurWeights[5].x;
  return final;
}


//==================================================================================================
float4 PsPpDynamicBlurFinal( VSPP_OUT i ) : COLOR
{
  half4 final;
  half weight = i.tc0.w;
  half4 src0 = tex2D( sampler0, i.tc0.xy );
  half4 src1 = tex2D( sampler1, i.tc1.xy );
  src1 += tex2D( sampler1, i.tc2.xy );
  src1 += tex2D( sampler1, i.tc3.xy );
  src1 += tex2D( sampler1, i.tc4.xy );
  src1 *= 0.25;
  final.rgb = lerp( src0.rgb, src1.rgb, saturate( weight ) );
  final.a = 1;
  return final;
}

//==================================================================================================
float4 PSPostProcessDiagDepthBuffer(VSPP_OUTPUT input) : COLOR
{
  float val = tex2D(sampler0, input.TexCoord);
  return float4(val, val, val, 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FXAA post process
////////////////////////////////////////////////////////////////////////////////////////////////////

#if (FXAA_SHADER_SOURCE == 2)
	#define FXAA_PC					1
	#define FXAA_HLSL_3				1
	#define FXAA_GREEN_AS_LUMA		1

	#include "Fxaa3_11.h"
#endif

#define FxaaSampler0 sampler0

float4 PsPpFxaa3_11(in float2 pos: TEXCOORD0): COLOR 
{	
	float4 clr = (float4) 0;
	
#if (FXAA_SHADER_SOURCE == 2)

	clr = FxaaPixelShader(
		pos.xy,						// FxaaFloat2 pos
		0,							// FxaaFloat4 fxaaConsolePosPos
		FxaaSampler0, 
		FxaaSampler0,
		FxaaSampler0,
		PSC_FxaaPars.xy,			// FxaaFloat2 fxaaQualityRcpFrame
		0,							// FxaaFloat4 fxaaConsoleRcpFrameOpt
		0,							// FxaaFloat4 fxaaConsoleRcpFrameOpt2
		0,							// FxaaFloat4 fxaaConsole360RcpFrameOpt2
		PSC_FxaaQuality.x,			// FxaaFloat fxaaQualitySubpix
		PSC_FxaaQuality.y,			// FxaaFloat fxaaQualityEdgeThreshold
		PSC_FxaaQuality.z,			// FxaaFloat fxaaQualityEdgeThresholdMin
		0,							// FxaaFloat fxaaConsoleEdgeSharpness
		0,							// FxaaFloat fxaaConsoleEdgeThreshold
		0,							// FxaaFloat fxaaConsoleEdgeThresholdMin
		0							// fxaaConsole360ConstDir
	);
#endif
	
#if (FXAA_SHADER_DEBUG == 1) 
	if (PSC_FxaaQuality.w > 0.5) 
	{
		clr = clr - tex2D(FxaaSampler0, pos.xy);
	}
#endif
		
	return clr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Modified Sharpen complex v2 from Media Player Classic Home Cinema
////////////////////////////////////////////////////////////////////////////////////////////////////

#define CoefGauss 2
#define CoefClr (1 + CoefGauss)
#define Sharpen_val0 2
#define Sharpen_val1 ((Sharpen_val0 - 1) / 8.0)

#define SharpenSampler0 sampler0

float4 PsSharpenFilter(in float2 tex: TEXCOORD0): COLOR
{
	float3 clr = tex2D(SharpenSampler0, tex).rgb;

	// calculate image floue (filtre gaussien)
	float3 c1 = tex2D(SharpenSampler0, tex + float2(-PSC_FxaaPars.z, -PSC_FxaaPars.w)).rgb;
	float3 c2 = tex2D(SharpenSampler0, tex + float2(  		   0, -PSC_FxaaPars.w)).rgb;
	float3 c3 = tex2D(SharpenSampler0, tex + float2( PSC_FxaaPars.z,-PSC_FxaaPars.w)).rgb;
	float3 c4 = tex2D(SharpenSampler0, tex + float2(-PSC_FxaaPars.z, 0 )).rgb;
	float3 c5 = tex2D(SharpenSampler0, tex + float2( PSC_FxaaPars.z, 0 )).rgb;
	float3 c6 = tex2D(SharpenSampler0, tex + float2(-PSC_FxaaPars.z, PSC_FxaaPars.w)).rgb;
	float3 c7 = tex2D(SharpenSampler0, tex + float2(  		   0, PSC_FxaaPars.w)).rgb;
	float3 c8 = tex2D(SharpenSampler0, tex + float2( PSC_FxaaPars.z, PSC_FxaaPars.w)).rgb;

	// 1 / (1+2+1+2+4+2+1+2+1) = 1 / 16 = .0625
	float3 clrGauss = (c1+c3+c6+c8+ 2*(c2+c4+c5+c7) + 4*clr) * 0.0625;

	// substract gauss - image
	float3 cori = CoefClr*clr - CoefGauss*clrGauss;

	c1 = tex2D(SharpenSampler0, tex + float2(-PSC_FxaaPars.x, -PSC_FxaaPars.y)).rgb;
	c2 = tex2D(SharpenSampler0, tex + float2(			0, -PSC_FxaaPars.y)).rgb;
	c3 = tex2D(SharpenSampler0, tex + float2( PSC_FxaaPars.x, -PSC_FxaaPars.y)).rgb;
	c4 = tex2D(SharpenSampler0, tex + float2(-PSC_FxaaPars.x, 0)).rgb;
	c5 = tex2D(SharpenSampler0, tex + float2( PSC_FxaaPars.x, 0)).rgb;
	c6 = tex2D(SharpenSampler0, tex + float2(-PSC_FxaaPars.x, PSC_FxaaPars.y)).rgb;
	c7 = tex2D(SharpenSampler0, tex + float2(			0, PSC_FxaaPars.y)).rgb;
	c8 = tex2D(SharpenSampler0, tex + float2( PSC_FxaaPars.x, PSC_FxaaPars.y)).rgb;

	float3 delta1 = (c3 + 2*c5 + c8)-(c1 + 2*c4 + c6);	
	float3 delta2 = (c6 + 2*c7 + c8)-(c1 + 2*c2 + c3);
	
	if (sqrt(dot(delta1, delta1) + dot(delta2, delta2)) > PSC_FxaaQuality.x) {	
		return float4(clr * Sharpen_val0 - (c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8) * Sharpen_val1, 1);
	} else {
		return float4(cori.rgb, 1.0);
	}
}

//==================================================================================================
// SMAA Pixel Shaders
//==================================================================================================
#define SMAA_PIXEL_SIZE (PSC_SmaaPixelSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_ONLY_COMPILE_PS 1
#include "PPAA\SMAA.h"

#define SmaaColorGammaTex sampler0
#define SmaaEdgesTex sampler0
#define SmaaAreaTex sampler1
#define SmaaSearchTex sampler2
#define SmaaColorTex sampler0
#define SmaaBlendTex sampler1

float4 DX11_SMAALumaEdgeDetectionPS(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0, float4 offset[3] : TEXCOORD1) : COLOR0 
{
	return SMAALumaEdgeDetectionPS(texcoord, offset, SmaaColorGammaTex);
}

float4 DX11_SMAAColorEdgeDetectionPS(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0, float4 offset[3] : TEXCOORD1) : COLOR0 
{
	return SMAAColorEdgeDetectionPS(texcoord, offset, SmaaColorGammaTex);
}

float4 DX11_SMAABlendingWeightCalculationPS(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0, float2 pixcoord : TEXCOORD1, float4 offset[3] : TEXCOORD2) : COLOR0 
{
	return SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, SmaaEdgesTex, SmaaAreaTex, SmaaSearchTex, 0);
}

float4 DX11_SMAANeighborhoodBlendingPS(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0, float4 offset[2] : TEXCOORD1) : COLOR0 
{
	return SMAANeighborhoodBlendingPS(texcoord, offset, SmaaColorTex, SmaaBlendTex);
}


